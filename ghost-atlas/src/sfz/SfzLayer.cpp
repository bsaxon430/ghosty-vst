// SPDX-License-Identifier: AGPL-3.0-or-later
// GHOST ATLAS - the bridge to sfizz (see SfzLayer.h). Compiled as C++14 with sfizz's own settings; no JUCE here.
#include "SfzLayer.h"

#include "Synth.h"
#include "Resources.h"
#include "FilePool.h"
#include "Region.h"
#include "Opcode.h"
#include "AudioSpan.h"
#include "BitArray.h"
#include "parser/Parser.h"
#include "parser/ParserListener.h"
#include "st_audiofile.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <sstream>

namespace atlas
{
namespace
{
struct Block
{
    std::string header;
    std::vector<std::pair<std::string, std::string>> ops;
};

struct Collector final : public sfz::ParserListener
{
    std::vector<Block> blocks;
    std::vector<std::string> errors, warnings;
    void onParseFullBlock (const std::string& header, const std::vector<sfz::Opcode>& opcodes) override
    {
        Block b;
        b.header = header;
        b.ops.reserve (opcodes.size());
        for (const auto& o : opcodes) b.ops.emplace_back (o.name, o.value);
        blocks.push_back (std::move (b));
    }
    void onParseError (const sfz::SourceRange&, const std::string& message) override { if (errors.size() < 40) errors.push_back (message); }
    void onParseWarning (const sfz::SourceRange&, const std::string& message) override { if (warnings.size() < 40) warnings.push_back (message); }
};

std::string lower (std::string s) { for (auto& c : s) c = (char) std::tolower ((unsigned char) c); return s; }

// "c#4", "Db3", "60" -> MIDI note (middle C = C4 = 60, the SFZ convention); -1 when it is not a note
int parseNote (const std::string& v)
{
    if (v.empty()) return -1;
    char* end = nullptr;
    const long n = std::strtol (v.c_str(), &end, 10);
    if (end != v.c_str() && *end == 0) return (n >= 0 && n <= 127) ? (int) n : -1;
    static const int base[7] = { 9, 11, 0, 2, 4, 5, 7 };   // a b c d e f g
    const char l = (char) std::tolower ((unsigned char) v[0]);
    if (l < 'a' || l > 'g') return -1;
    int semis = base[l - 'a'];
    size_t i = 1;
    if (i < v.size() && (v[i] == '#' || v[i] == 's')) { ++semis; ++i; }
    else if (i < v.size() && v[i] == 'b') { --semis; ++i; }
    if (i >= v.size()) return -1;
    const long oct = std::strtol (v.c_str() + i, &end, 10);
    if (end == v.c_str() + i || *end != 0) return -1;
    const long note = (oct + 1) * 12 + semis;
    return (note >= 0 && note <= 127) ? (int) note : -1;
}

float parseFloat (const std::string& v, float fallback)
{
    char* end = nullptr;
    const double d = std::strtod (v.c_str(), &end);
    return end == v.c_str() ? fallback : (float) d;
}

struct FileFacts { bool exists = false; double rate = 0; long long frames = 0; int channels = 0; long long bytes = 0; };

FileFacts factsFor (const fs::path& p, bool wantAudio)
{
    FileFacts f;
    std::error_code ec;
    if (! fs::exists (p, ec)) return f;
    f.exists = true;
    f.bytes = (long long) fs::file_size (p, ec);
    if (wantAudio)
    {
        if (st_audio_file* af = st_open_file (p.u8string().c_str()))
        {
            f.rate = st_get_sample_rate (af);
            f.frames = (long long) st_get_frame_count (af);
            f.channels = (int) st_get_channels (af);
            st_close (af);
        }
    }
    return f;
}

// a sample path as the SFZ wrote it -> the file on disk (default_path applied; a folder or file name whose letter case
// differs from the SFZ's is still found, as sfizz itself does)
fs::path resolveSample (const fs::path& root, const std::string& defaultPath, const std::string& sample)
{
    std::string rel = defaultPath + sample;
    std::replace (rel.begin(), rel.end(), '\\', '/');
    fs::path direct = (root / fs::u8path (rel)).lexically_normal();
    std::error_code ec;
    if (fs::exists (direct, ec)) return direct;
    fs::path cur = root;
    std::stringstream ss (rel);
    std::string part;
    while (std::getline (ss, part, '/'))
    {
        if (part.empty() || part == ".") continue;
        if (part == "..") { cur = cur.parent_path(); continue; }
        fs::path next = cur / fs::u8path (part);
        if (! fs::exists (next, ec))
        {
            const std::string want = lower (part);
            bool found = false;
            if (fs::is_directory (cur, ec))
                for (const auto& e : fs::directory_iterator (cur, ec))
                    if (lower (e.path().filename().u8string()) == want) { next = e.path(); found = true; break; }
            if (! found) return direct;
        }
        cur = next;
    }
    return cur;
}

struct Parsed
{
    bool ok = false;
    std::string error;
    fs::path root;
    Collector col;
    size_t includes = 0;
    std::vector<std::string> included;
};

bool parseTo (Parsed& out, const fs::path& path, const std::string* text)
{
    sfz::Parser parser;
    parser.setListener (&out.col);
    if (text != nullptr) parser.parseString (path, *text);
    else
    {
        std::error_code ec;
        if (! fs::exists (path, ec)) { out.error = "file not found"; return false; }
        parser.parseFile (path);
    }
    out.root = parser.originalDirectory();
    out.includes = parser.getIncludedFiles().size();
    for (const auto& f : parser.getIncludedFiles()) out.included.push_back (f);
    out.ok = ! out.col.blocks.empty();
    if (! out.ok) out.error = out.col.errors.empty() ? "no SFZ headers found" : out.col.errors.front();
    return out.ok;
}

// the opcodes (of the ones the hooks care about) in force for a region: region over group over master over global
struct Inherit
{
    std::map<std::string, std::string> global, master, group;
    void enter (const Block& b)
    {
        auto take = [&b] (std::map<std::string, std::string>& m) { m.clear(); for (const auto& o : b.ops) m[o.first] = o.second; };
        if (b.header == "global") { take (global); master.clear(); group.clear(); }
        else if (b.header == "master") { take (master); group.clear(); }
        else if (b.header == "group") take (group);
    }
    std::string get (const Block& region, const std::string& name, const std::string& fallback = {}) const
    {
        for (auto it = region.ops.rbegin(); it != region.ops.rend(); ++it) if (it->first == name) return it->second;
        auto g = group.find (name); if (g != group.end()) return g->second;
        auto m = master.find (name); if (m != master.end()) return m->second;
        auto gl = global.find (name); if (gl != global.end()) return gl->second;
        return fallback;
    }
};

std::string numberText (double v)
{
    char buf[48];
    std::snprintf (buf, sizeof (buf), "%.6g", v);
    return buf;
}

// the SFZ text the engine is given: the instrument's own blocks in their own order (includes and $variables already
// resolved by sfizz's parser), with the shape's opcodes added at the end of each region so nothing in the file overrides them
std::string buildText (const Parsed& p, const SfzShape& shape)
{
    std::string out;
    out.reserve (p.col.blocks.size() * 96);
    Inherit inh;
    std::string defaultPath;
    std::map<std::string, FileFacts> facts;
    // (BACKWARDS needs each file's length too: the instrument's own loop / offset / end points are written for the sample
    // played forwards, and have to be turned round with it)
    const bool needAudio = shape.startRange > 0.0f || shape.loopMode == 1 || shape.reverse;

    for (const auto& b : p.col.blocks)
    {
        inh.enter (b);
        if (b.header == "control")
            for (const auto& o : b.ops) if (o.first == "default_path") defaultPath = o.second;

        out += '<'; out += b.header; out += '>';
        for (const auto& o : b.ops) { out += ' '; out += o.first; out += '='; out += o.second; }

        if (b.header == "region" && ! shape.isNeutral())
        {
            const std::string sample = inh.get (b, "sample");
            const bool generator = ! sample.empty() && sample[0] == '*';
            const std::string trig = inh.get (b, "trigger", "attack");
            const bool isRelease = trig == "release" || trig == "release_key";
            if (shape.envelopeHooks)
            {
                if (! isRelease) { out += " ampeg_attack_oncc" + std::to_string (kCcAttack) + "=" + numberText (shape.attackRange); }
                out += " ampeg_release_oncc" + std::to_string (kCcReleaseUp) + "=" + numberText (shape.releaseRange);
                const float rel = parseFloat (inh.get (b, "ampeg_release", "0"), 0.0f);
                if (rel > 0.004f) out += " ampeg_release_oncc" + std::to_string (kCcReleaseDown) + "=" + numberText (-0.97 * rel);
            }
            if (! generator && ! isRelease && (needAudio || shape.reverse))
            {
                FileFacts ff;
                if (needAudio)
                {
                    auto it = facts.find (sample);
                    if (it == facts.end()) it = facts.emplace (sample, factsFor (resolveSample (p.root, defaultPath, sample), true)).first;
                    ff = it->second;
                }
                if (shape.startRange > 0.0f && ff.frames > 0)
                {
                    const double reach = std::min ((double) ff.frames * 0.95, (double) shape.startRange * ff.rate);
                    out += " offset_oncc" + std::to_string (kCcStart) + "=" + std::to_string ((long long) reach);
                }
                if (shape.reverse)
                {
                    out += " direction=reverse";
                    // BACKWARDS without clicks (owner, 2026-11-14: "if I switch this to backwards I get some clicking").
                    // sfizz reads the file backwards, but the region's own loop / offset / end opcodes are still counted
                    // from the front: a sustain loop then lands on the wrong part of the sound and its seam clicks every
                    // time round, and offset / end trim the wrong end. So they are turned round with the sample:
                    // forwards frame f is backwards frame (frames - 1 - f). A loop the file only carries in its own
                    // metadata is not known here (sfizz drops those when reversed) - such a region plays once through.
                    const long long last = ff.frames - 1;
                    if (last > 0)
                    {
                        auto num = [&] (const char* a, const char* b2, long long dflt) {
                            std::string v = inh.get (b, a);
                            if (v.empty() && b2 != nullptr) v = inh.get (b, b2);
                            return v.empty() ? dflt : (long long) parseFloat (v, (float) dflt);
                        };
                        const long long off = std::max (0LL, std::min (last, num ("offset", nullptr, 0)));
                        const long long endRaw = num ("end", nullptr, last);
                        const long long end = std::max (0LL, std::min (last, endRaw));
                        if (endRaw > 0 && (off > 0 || end < last)) // (end=0 / -1 means a silent region: left as written)
                            out += " offset=" + std::to_string (last - end) + " end=" + std::to_string (last - off);
                        if (shape.loopMode == 0)
                        {
                            const std::string lm = inh.get (b, "loop_mode", inh.get (b, "loopmode"));
                            const bool loops = lm == "loop_continuous" || lm == "loop_sustain";
                            const long long ls = num ("loop_start", "loopstart", -1), le = num ("loop_end", "loopend", -1);
                            if (ls >= 0 && le > ls && le <= last)
                                out += " loop_start=" + std::to_string (last - le) + " loop_end=" + std::to_string (last - ls);
                            else if (loops)
                                out += " loop_mode=no_loop"; // (its loop points are in the file's metadata: unknown backwards)
                        }
                    }
                    // and a few milliseconds of fade-in: backwards the sound starts where the recording was cut off
                    const float att = parseFloat (inh.get (b, "ampeg_attack", "0"), 0.0f);
                    if (att < 0.004f) out += " ampeg_attack=0.004";
                }
                if (shape.loopMode == 1 && ff.frames > 2048)
                {
                    const long long a = (long long) (std::max (0.0f, std::min (0.98f, shape.loopStart)) * (float) ff.frames);
                    const long long e = (long long) (std::max (0.02f, std::min (1.0f, shape.loopEnd)) * (float) ff.frames) - 1;
                    if (e - a > 1024)
                    {
                        const double maxFade = 0.9 * (double) std::min (a, e - a) / std::max (1.0, ff.rate);
                        out += " loop_mode=loop_continuous loop_start=" + std::to_string (a) + " loop_end=" + std::to_string (e)
                             + " loop_crossfade=" + numberText (std::max (0.001, std::min ((double) shape.loopCrossfade, maxFade)));
                    }
                }
            }
            if (shape.loopMode == 2 && ! generator) out += " loop_mode=no_loop";
            if (shape.pitchHook)
            {
                out += " pitch_oncc" + std::to_string (kCcPitchUp) + "=" + numberText (shape.pitchRange);
                out += " pitch_oncc" + std::to_string (kCcPitchDown) + "=" + numberText (-shape.pitchRange);
            }
            auto clampKey = [] (int k) { return std::max (0, std::min (127, k)); };
            if (shape.xfKeyInHi >= 0) out += " xfin_lokey=" + std::to_string (clampKey (shape.xfKeyInLo)) + " xfin_hikey=" + std::to_string (clampKey (shape.xfKeyInHi));
            if (shape.xfKeyOutLo >= 0) out += " xfout_lokey=" + std::to_string (clampKey (shape.xfKeyOutLo)) + " xfout_hikey=" + std::to_string (clampKey (shape.xfKeyOutHi));
            if (shape.xfVelInHi >= 0) out += " xfin_lovel=" + std::to_string (clampKey (shape.xfVelInLo)) + " xfin_hivel=" + std::to_string (clampKey (shape.xfVelInHi));
            if (shape.xfVelOutLo >= 0) out += " xfout_lovel=" + std::to_string (clampKey (shape.xfVelOutLo)) + " xfout_hivel=" + std::to_string (clampKey (shape.xfVelOutHi));
        }
        out += '\n';
    }
    return out;
}
} // namespace

// =====================================================================================================================
struct SfzLayer::Impl
{
    sfz::Synth synth;
    SfzInfo info;
    std::string text;
    double sampleRate = 48000.0;
    int maxBlock = 1024;
    bool offline = false, hostOffline = false;
    int gaveUp = 0;                   // (waits for a recording that never arrived, while the host renders offline)
    // where in the block being put together each key was struck (-1: not in this block). See noteOff().
    std::array<int, 128> struckAt;
    bool anyStruck = false;
    Impl() { struckAt.fill (-1); }

    void applyRates()
    {
        synth.setSampleRate ((float) sampleRate);
        synth.setSamplesPerBlock (maxBlock);
    }
    // While a host renders faster than real time, a note can outrun the disk. sfizz hands a starting note the first part
    // of its recording and reads the rest in behind it; its freewheeling mode waits for those reads before each block, but
    // a read asked for a moment ago may not have reached its loading thread yet, and is then not waited for - the note runs
    // off the end of what it was given and stops. So the recordings a note can play are asked for through sfizz's own call
    // (the one a starting voice makes) and waited for until sfizz marks them read. Nothing is loaded differently and
    // nothing of sfizz's is changed: the note simply starts once its sound is there.
    void awaitRecordings (int note, float velocity)
    {
        if (gaveUp >= 2) return;      // (a recording that cannot be read must not stall a whole export: see below)
        auto& pool = synth.getResources().getFilePool();
        const auto key = (uint8_t) std::max (0, std::min (127, note));
        const float vel = std::max (0.0f, std::min (1.0f, velocity));
        std::array<sfz::FileDataHolder, 24> held;
        size_t have = 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds (5);
        auto wait = [&] {
            for (size_t i = 0; i < have; ++i)
            {
                while (held[i]->status.load() != sfz::FileData::Status::Done)
                {
                    if (std::chrono::steady_clock::now() >= deadline) { ++gaveUp; break; }
                    std::this_thread::sleep_for (std::chrono::microseconds (200));
                }
                held[i].reset();
            }
            have = 0;
        };
        const int n = synth.getNumRegions();
        for (int i = 0; i < n; ++i)
        {
            const auto* r = synth.getRegionView (i);
            if (r == nullptr || r->sampleId == nullptr || r->isGenerator() || r->isOscillator()) continue;
            if (! r->keyRange.containsWithEnd (key) || ! r->velocityRange.containsWithEnd (vel)) continue;
            auto h = pool.getFilePromise (r->sampleId);
            if (! h) continue;
            if ((int64_t) h->preloadedData.getNumFrames() > h->information.end) continue;      // short enough to be in memory whole
            held[have++] = std::move (h);
            if (have == held.size()) wait();
        }
        wait();
    }
    bool finish (bool loaded, const SfzShape& shape)
    {
        info = SfzInfo();
        info.ok = loaded && synth.getNumRegions() > 0;
        if (! info.ok) info.error = loaded ? "the instrument has no playable regions (are its samples missing?)" : "the SFZ could not be loaded";
        info.regions = synth.getNumRegions();
        info.preloadedSamples = (int) synth.getNumPreloadedSamples();
        info.unknownOpcodes = synth.getUnknownOpcodes();
        const auto& used = synth.getUsedCCs();
        for (int cc = 0; cc < 128; ++cc) if (used.test ((size_t) cc)) info.usedCCs.push_back (cc);
        // the hooks rest at their neutral position
        if (shape.envelopeHooks) { synth.hdcc (0, kCcAttack, 0.0f); synth.hdcc (0, kCcReleaseUp, 0.0f); synth.hdcc (0, kCcReleaseDown, 0.0f); }
        if (shape.startRange > 0.0f) synth.hdcc (0, kCcStart, 0.0f);
        if (shape.pitchHook) { synth.hdcc (0, kCcPitchUp, 0.0f); synth.hdcc (0, kCcPitchDown, 0.0f); }
        return info.ok;
    }
};

SfzLayer::SfzLayer() : impl (new Impl()) { impl->applyRates(); }
SfzLayer::~SfzLayer() = default;

void SfzLayer::prepare (double sampleRate, int maxBlock)
{
    impl->sampleRate = sampleRate > 0 ? sampleRate : 48000.0;
    impl->maxBlock = std::max (16, maxBlock);
    impl->applyRates();
}
void SfzLayer::setVoices (int voices) { impl->synth.setNumVoices (std::max (1, std::min (256, voices))); }
void SfzLayer::setQuality (int live, int offline)
{
    impl->synth.setSampleQuality (sfz::Synth::ProcessLive, std::max (1, std::min (10, live)));
    impl->synth.setSampleQuality (sfz::Synth::ProcessFreewheeling, std::max (1, std::min (10, offline)));
}
void SfzLayer::setOffline (bool on)
{
    impl->offline = on;
    if (on) impl->synth.enableFreeWheeling(); else impl->synth.disableFreeWheeling();
}

bool SfzLayer::load (const std::string& sfzPath, const SfzShape& shape)
{
    const fs::path path = fs::u8path (sfzPath);
    if (shape.isNeutral())
    {
        impl->text.clear();
        return impl->finish (impl->synth.loadSfzFile (path), shape);
    }
    Parsed p;
    if (! parseTo (p, path, nullptr)) { impl->info = SfzInfo(); impl->info.error = p.error; return false; }
    impl->text = buildText (p, shape);
    return impl->finish (impl->synth.loadSfzString (path, impl->text), shape);
}

bool SfzLayer::loadText (const std::string& virtualPath, const std::string& sfzText, const SfzShape& shape)
{
    const fs::path path = fs::u8path (virtualPath);
    if (shape.isNeutral())
    {
        impl->text = sfzText;
        return impl->finish (impl->synth.loadSfzString (path, impl->text), shape);
    }
    Parsed p;
    if (! parseTo (p, path, &sfzText)) { impl->info = SfzInfo(); impl->info.error = p.error; return false; }
    impl->text = buildText (p, shape);
    return impl->finish (impl->synth.loadSfzString (path, impl->text), shape);
}

const SfzInfo& SfzLayer::info() const { return impl->info; }
const std::string& SfzLayer::loadedText() const { return impl->text; }

void SfzLayer::noteOn (int delay, int note, float v)
{
    if (impl->hostOffline) impl->awaitRecordings (note, v);
    if (note >= 0 && note < 128) { impl->struckAt[(size_t) note] = delay; impl->anyStruck = true; }
    impl->synth.hdNoteOn (delay, note, std::max (0.0f, std::min (1.0f, v)));
}

void SfzLayer::setHostOffline (bool on)
{
    if (impl->hostOffline == on) return;
    impl->hostOffline = on; impl->gaveUp = 0;
    if (on) impl->synth.enableFreeWheeling(); else if (! impl->offline) impl->synth.disableFreeWheeling();
}

// Offline, a note must sound in full however busy the machine is. sfizz streams a note's recording in behind a hand-off
// between two of its threads, and when rendering runs far faster than real time a note can outrun that hand-off and be
// cut short. So before an offline render starts, the recordings its notes will play are read whole (sfizz's own
// FilePool::loadFile, the call it uses for wavetables): the engine then finds them already there.
// Only before the first note: reading more in while notes sound would move what they are reading from.
void SfzLayer::primeFor (const std::vector<std::pair<int, float>>& notes)
{
    if (! impl->offline || notes.empty()) return;
    auto& pool = impl->synth.getResources().getFilePool();
    const int n = impl->synth.getNumRegions();
    int read = 0;
    for (int i = 0; i < n; ++i)
    {
        const auto* r = impl->synth.getRegionView (i);
        if (r == nullptr || r->sampleId == nullptr || r->isGenerator()) continue;
        for (const auto& nt : notes)
            if (r->keyRange.containsWithEnd ((uint8_t) std::max (0, std::min (127, nt.first))) && r->velocityRange.containsWithEnd (std::max (0.0f, std::min (1.0f, nt.second))))
            { pool.loadFile (*r->sampleId); ++read; break; }
    }
    if (std::getenv ("GHOST_ATLAS_DEBUG") != nullptr) std::fprintf (stderr, "primed: %d of %d regions read whole for %d notes\n", read, n, (int) notes.size());
}
// A key let go on the very sample it was struck (a note with no length, somewhere after the start of a block) is never
// let go by sfizz 1.2.3: its envelope is still counting down to the strike and the release is not taken up afterwards,
// so the note sounds for ever. Upstream fixed this after its last release (commit 4055582, "Fixes an issue that caused
// stuck notes"); here the same is avoided from outside, with sfizz untouched: such a release is handed over one sample
// before the strike, which sfizz takes as a note let go before it began, and drops (Voice::Impl::release).
void SfzLayer::noteOff (int delay, int note, float v)
{
    if (note >= 0 && note < 128 && delay > 0 && impl->struckAt[(size_t) note] == delay) --delay;
    impl->synth.hdNoteOff (delay, note, std::max (0.0f, std::min (1.0f, v)));
}
void SfzLayer::controller (int delay, int cc, float v) { if (cc >= 0 && cc < sfz::config::numCCs) impl->synth.hdcc (delay, cc, std::max (0.0f, std::min (1.0f, v))); }
void SfzLayer::pitchWheel (int delay, float bipolar) { impl->synth.hdPitchWheel (delay, std::max (-1.0f, std::min (1.0f, bipolar))); }
void SfzLayer::aftertouch (int delay, float v) { impl->synth.hdChannelAftertouch (delay, std::max (0.0f, std::min (1.0f, v))); }
void SfzLayer::tempo (float bpm) { impl->synth.bpmTempo (0, std::max (20.0f, std::min (400.0f, bpm))); }
void SfzLayer::allSoundOff() { impl->synth.allSoundOff(); }
void SfzLayer::setTuningHz (float a4) { impl->synth.setTuningFrequency (std::max (300.0f, std::min (600.0f, a4))); }
int SfzLayer::activeVoices() const { return impl->synth.getNumActiveVoices(); }

void SfzLayer::render (float* left, float* right, int frames)
{
    float* chans[2] = { left, right };
    int done = 0;
    while (done < frames)   // never more than the block size the engine was prepared with
    {
        const int n = std::min (impl->maxBlock, frames - done);
        float* c[2] = { chans[0] + done, chans[1] + done };
        sfz::AudioSpan<float> span (c, 2, 0, (size_t) n);
        impl->synth.renderBlock (span);
        done += n;
    }
    if (impl->anyStruck) { impl->struckAt.fill (-1); impl->anyStruck = false; }
}

bool SfzLayer::audioFileInfo (const std::string& path, double& sampleRate, long long& frames, int& channels)
{
    const FileFacts f = factsFor (fs::u8path (path), true);
    sampleRate = f.rate; frames = f.frames; channels = f.channels;
    return f.exists && f.frames > 0;
}

SfzScan SfzLayer::scan (const std::string& sfzPath, bool sizeOnDisk)
{
    SfzScan s;
    Parsed p;
    if (! parseTo (p, fs::u8path (sfzPath), nullptr)) { s.error = p.error; s.parseErrors = p.col.errors; return s; }
    s.ok = true;
    s.includes = (int) p.includes;
    s.included = p.included;
    s.parseErrors = p.col.errors;
    s.parseWarnings = p.col.warnings;

    Inherit inh;
    std::string defaultPath;
    std::set<std::string> opcodes, headers, sampleSet, missingSet;
    std::set<std::pair<int, int>> velRanges;
    bool sounding[128] = {};
    int maxSeq = 1;
    for (const auto& b : p.col.blocks)
    {
        inh.enter (b);
        headers.insert (b.header);
        for (const auto& o : b.ops) opcodes.insert (o.first);
        if (b.header == "control")
        {
            for (const auto& o : b.ops)
            {
                if (o.first == "default_path") defaultPath = o.second;
                else if (o.first.compare (0, 8, "label_cc") == 0) s.ccLabels.emplace_back (std::atoi (o.first.c_str() + 8), o.second);
                else if (o.first.compare (0, 9, "label_key") == 0) s.keyLabels.emplace_back (std::atoi (o.first.c_str() + 9), o.second);
            }
        }
        else if (b.header == "global") ++s.globals;
        else if (b.header == "master") ++s.masters;
        else if (b.header == "group") ++s.groups;
        else if (b.header == "curve") ++s.curves;
        else if (b.header == "effect") ++s.effects;
        else if (b.header == "region")
        {
            ++s.regions;
            const std::string sample = inh.get (b, "sample");
            const bool generator = ! sample.empty() && sample[0] == '*';
            if (generator || inh.get (b, "oscillator") == "on") ++s.oscillatorRegions;
            int lo = parseNote (inh.get (b, "lokey", "0")), hi = parseNote (inh.get (b, "hikey", "127"));
            const int key = parseNote (inh.get (b, "key"));
            if (key >= 0) { lo = key; hi = key; }
            if (lo >= 0 && hi >= 0 && hi >= lo) { s.loKey = std::min (s.loKey, lo); s.hiKey = std::max (s.hiKey, hi); }
            const std::string trig = inh.get (b, "trigger", "attack");
            if (trig == "release" || trig == "release_key") ++s.releaseRegions;
            else
            {
                const int lv = (int) parseFloat (inh.get (b, "lovel", "0"), 0), hv = (int) parseFloat (inh.get (b, "hivel", "127"), 127);
                velRanges.insert (std::make_pair (lv, hv));
                if (lo >= 0 && hi >= lo) for (int k = lo; k <= hi && k < 128; ++k) sounding[k] = true;
            }
            const std::string lm = inh.get (b, "loop_mode");
            if (lm == "loop_continuous" || lm == "loop_sustain") ++s.loopedRegions;
            if (! inh.get (b, "sw_last").empty() || ! inh.get (b, "sw_down").empty() || ! inh.get (b, "sw_lokey").empty()) ++s.keyswitchRegions;
            maxSeq = std::max (maxSeq, (int) parseFloat (inh.get (b, "seq_length", "1"), 1));
            s.maxRelease = std::max (s.maxRelease, parseFloat (inh.get (b, "ampeg_release", "0"), 0));
            s.maxAttack = std::max (s.maxAttack, parseFloat (inh.get (b, "ampeg_attack", "0"), 0));
            if (! generator && ! sample.empty())
            {
                const fs::path f = resolveSample (p.root, defaultPath, sample);
                const std::string abs = f.u8string();
                if (sampleSet.insert (abs).second)
                {
                    const FileFacts ff = factsFor (f, false);
                    if (! ff.exists) missingSet.insert (abs);
                    else if (sizeOnDisk) s.sampleBytes += ff.bytes;
                }
            }
        }
    }
    if (s.regions == 0) { s.ok = false; s.error = "no regions"; }
    if (s.loKey > s.hiKey) { s.loKey = 0; s.hiKey = 127; }
    for (int k = 0; k < 128; ++k) if (sounding[k]) s.keys.push_back (k);
    s.velocityLayers = std::max (1, (int) velRanges.size());
    s.roundRobins = maxSeq;
    s.samples.assign (sampleSet.begin(), sampleSet.end());
    s.missing.assign (missingSet.begin(), missingSet.end());
    s.opcodes.assign (opcodes.begin(), opcodes.end());
    s.headers.assign (headers.begin(), headers.end());
    return s;
}
} // namespace atlas
