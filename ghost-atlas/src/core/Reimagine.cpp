// SPDX-License-Identifier: AGPL-3.0-or-later
// GHOST ATLAS - REIMAGINE (see Reimagine.h). The mosaic itself is Samplebrain's brain and renderer, unmodified
// (third_party/samplebrain, GPL-2.0-or-later, Then Try This); this file hands them sounds from memory and turns what
// comes back into an instrument.
#include "Reimagine.h"
#include "Dsp.h"

#include <sndfile.h>      // third_party/samplebrain/compat: the few libsndfile calls the engine makes
#include <lo/lo.h>        // ... and the two liblo calls of its status reporter
#include "brain.h"
#include "renderer.h"
#include "search_params.h"
#include "window.h"
#include <map>
#include <mutex>

// ---------------------------------------------------------------- libsndfile stand-in
// brain::load_sound opens a file by name with sf_open and reads it with sf_read_float. Here the "files" are mono
// sounds kept in memory by the thread that is rebuilding a note; nothing is read from disk.
static thread_local std::map<std::string, std::vector<float>>* tAtlasSounds = nullptr;
struct SNDFILE_tag { const std::vector<float>* data = nullptr; sf_count_t pos = 0; };
static thread_local std::unique_ptr<SNDFILE_tag> tAtlasOpen;      // (the engine never closes what it opens: freed at the next open)

SNDFILE* sf_open (const char* path, int mode, SF_INFO* info)
{
    if (mode != SFM_READ || path == nullptr || info == nullptr || tAtlasSounds == nullptr) return nullptr;
    const auto it = tAtlasSounds->find (path);
    if (it == tAtlasSounds->end()) return nullptr;
    tAtlasOpen = std::make_unique<SNDFILE_tag>();
    tAtlasOpen->data = &it->second;
    info->frames = (sf_count_t) it->second.size(); info->channels = 1; info->samplerate = 44100;
    info->format = 0; info->sections = 1; info->seekable = 1;
    return tAtlasOpen.get();
}
sf_count_t sf_read_float (SNDFILE* f, float* ptr, sf_count_t items)
{
    if (f == nullptr || f->data == nullptr || ptr == nullptr) return 0;
    const auto n = std::min<sf_count_t> (items, (sf_count_t) f->data->size() - f->pos);
    if (n <= 0) return 0;
    std::copy (f->data->begin() + f->pos, f->data->begin() + f->pos + n, ptr);
    f->pos += n;
    return n;
}
int sf_close (SNDFILE* f) { if (f != nullptr && f == tAtlasOpen.get()) tAtlasOpen.reset(); return 0; }

// ---------------------------------------------------------------- liblo stand-in: the engine's status lines go nowhere
struct lo_address_compat { int unused = 0; };
lo_address lo_address_new_from_url (const char*) { static lo_address_compat a; return &a; }
void lo_address_free (lo_address) {}
int lo_send (lo_address, const char*, const char*, ...) { return 0; }

namespace atlas
{
using namespace measure;

namespace
{
constexpr double kFs = 44100.0;
constexpr int kBlock = 3000;            // Samplebrain's own default block
constexpr int kHop = 1500;              // half a block: Hann windows that overlap by half add up level
constexpr double kHold = 4.5, kGap = 7.0, kPre = 0.05;     // each note: held, then room to ring out; a moment of silence first

// loudness shape: RMS every `hop` samples over a window of `win`
std::vector<float> shapeOf (const float* x, int n, int win, int hop)
{
    std::vector<float> e;
    for (int i = 0; i + win <= n; i += hop) e.push_back (rms (x + i, win));
    return e;
}

// one note rebuilt by Samplebrain: `target` out of the blocks in `source`. Comes back as long as the target less its last blocks.
std::vector<float> mosaicOf (const std::vector<float>& target, spiralcore::brain& source, std::map<std::string, std::vector<float>>& sounds, float novelty, float stickyness)
{
    sounds["target"] = target;
    spiralcore::brain tgt;
    tgt.load_sound ("target", spiralcore::brain::MIX);
    tgt.init (kBlock, kBlock - kHop, spiralcore::window::HANN);
    if (tgt.get_num_blocks() < 4 || source.get_num_blocks() < 4) return {};
    spiralcore::renderer r (source, tgt);
    *r.get_params() = spiralcore::search_params (0.6f, 0.8f, 0, 100, novelty);     // leaning on the MFCC, level mostly taken out
    r.get_params()->m_stickyness = stickyness;                                         // some loyalty to the source's own next block: smoother
    r.set_search_algo (spiralcore::renderer::BASIC);
    r.set_n_mix (1.0f);            // the level-matched blocks: the loudness shape is the original's, put on afterwards
    r.set_target_mix (0.0f);       // none of the target itself: only the sources' blocks
    r.set_volume (1.0f);
    r.set_playing (true);
    const int total = ((int) tgt.get_num_blocks() - 2) * kHop;
    std::vector<float> out ((size_t) juce::jmax (0, total), 0.0f), buf (512, 0.0f);
    r.process (512, buf.data());                                                       // (its first call only finds its feet)
    for (int pos = 0; pos < total; pos += 512)
    {
        const int n = juce::jmin (512, total - pos);
        std::fill (buf.begin(), buf.end(), 0.0f);
        r.process ((unsigned int) n, buf.data());
        std::copy (buf.begin(), buf.begin() + n, out.begin() + pos);
    }
    for (auto& v : out) if (! std::isfinite (v)) v = 0.0f;
    return out;
}

// A note brought to pitch. Recordings are seldom dead on their key, and four sources each a little out would make a
// cloud instead of a note: the note's partial is read in a fine spectrum, and the note is resampled onto its key
// (only when the partial is clear, and only within 60 cents: more than that is a different note, not a tuning).
float bringToPitch (std::vector<float>& x, float expectedHz)
{
    const int from = juce::jmin ((int) x.size(), (int) (0.2 * kFs)), n = juce::jmin ((int) x.size() - from, (int) (3.0 * kFs));
    if (n < (int) (0.5 * kFs)) return 0.0f;
    float strength = 0.0f;
    const float cents = tuningOf (x.data() + from, n, kFs, expectedHz, strength);
    if (strength < 0.2f || std::abs (cents) < 2.0f || std::abs (cents) > 60.0f) return 0.0f;
    const double step = std::pow (2.0, -(double) cents / 1200.0);          // sharp: read it more slowly; flat: faster
    const int outN = (int) ((double) x.size() / step) - 8;
    if (outN < (int) (0.5 * kFs)) return 0.0f;
    std::vector<float> in (x); in.resize (x.size() + 64, 0.0f);
    std::vector<float> y ((size_t) outN, 0.0f);
    juce::WindowedSincInterpolator sinc;
    sinc.process (step, in.data(), y.data(), outN);
    x.swap (y);
    return cents;
}

// takes out anything standing still under a sound (a few source recordings sit off centre)
void centre (std::vector<float>& x)
{
    const float k = std::exp (-juce::MathConstants<float>::twoPi * 20.0f / (float) kFs);
    float px = 0.0f, py = 0.0f;
    for (auto& v : x) { const float y = v - px + k * py; px = v; py = y; v = y; }
}

// gives `x` the loudness shape of `like` (both mono, same length)
void shapeLike (std::vector<float>& x, const std::vector<float>& like)
{
    const int n = (int) juce::jmin (x.size(), like.size()), win = 1024, hop = 256;
    if (n < win * 2) return;
    const auto a = shapeOf (like.data(), n, win, hop), b = shapeOf (x.data(), n, win, hop);
    std::vector<float> g (a.size());
    for (size_t i = 0; i < a.size(); ++i) g[i] = juce::jmin (30.0f, a[i] / juce::jmax (1.0e-4f, b[i]));
    for (int i = 0; i < n; ++i)
    {
        const float at = juce::jlimit (0.0f, (float) g.size() - 1.001f, ((float) i - (float) win * 0.5f) / (float) hop);
        const int k = (int) at; const float f = at - (float) k;
        x[(size_t) i] *= g[(size_t) k] + (g[(size_t) juce::jmin (k + 1, (int) g.size() - 1)] - g[(size_t) k]) * f;
    }
}

bool writeRecording (const juce::File& f, const juce::AudioBuffer<float>& b)
{
    f.getParentDirectory().createDirectory();
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> os (f.createOutputStream());
    if (os == nullptr) return false;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), kFs, 2u, 24, {}, 0));
    if (w == nullptr) return false;
    os.release();
    return w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

LibrarySource madeHere()
{
    LibrarySource s;
    s.id = "reimagined"; s.folder = "reimagined"; s.name = "Reimagined (made here)";
    s.author = "Ghost Atlas REIMAGINE, with Samplebrain's mosaic engine (Then Try This)";
    s.credit = "Instruments rebuilt from sounds in this library. Each one's notes say what it was made from.";
    s.license = "made from library sounds: each instrument's notes list its sources and their licences";
    s.restrictions = "whatever the listed sources ask";
    s.category = "Hybrid Instruments";
    return s;
}
} // namespace

bool discardReimagined (Library& library, const Mutation& m)
{
    if (m.madeInstrument.isEmpty()) return false;
    library.remove (m.madeInstrument);
    library.save();
    const juce::File folder (m.madeFolder);
    if (folder.isDirectory() && folder.isAChildOf (libraryDir().getChildFile ("reimagined"))) folder.deleteRecursively();
    return true;
}

Mutation reimagine (Library& library, const Engine::Resolver& resolver, const Preset& original, juce::uint32 seed, const Progress& progress)
{
    Mutation out;
    auto say = [&progress] (float f, const juce::String& s) { return progress ? progress (f, s) : true; };
    Mutator mutator (library, resolver);
    const int mi = mutator.mainLayer (original);
    Instrument main;
    if (mi < 0 || ! library.findLoose (original.layers[(size_t) mi].instrument, main)) { out.failure = "This sound has no layer from the library to start from."; return out; }
    juce::Random rng ((juce::int64) seed * 2654435761LL + 99);

    if (! say (0.03f, "Listening to the sound as it is")) return out;
    const Heard before = mutator.listen (original);
    out.originalLoudnessDb = before.loudnessDb;

    // ---- the notes, and the original played at each of them (send effects and master effects off: the instrument is dry)
    std::vector<int> keys; for (int k = 24; k <= 96; k += 6) keys.push_back (k);
    const int segLen = (int) (kGap * kFs), nKeys = (int) keys.size();
    auto playAll = [&] (Preset p) {
        for (auto& r : p.returns) r = 0.0f;
        for (auto& m : p.master) m.on = false;
        std::vector<Note> notes;
        for (int i = 0; i < nKeys; ++i) notes.push_back ({ keys[(size_t) i], 0.8f, kGap * i + kPre, kHold });
        return Engine::bounce (p, resolver, notes, kGap * nKeys + 0.5, kFs, true);
    };
    if (! say (0.08f, "Playing the sound at thirteen notes")) return out;
    const auto targetAudio = playAll (original);
    const auto targetMono = monoOf (targetAudio);
    std::vector<bool> sounds ((size_t) nKeys, false);
    int sounding = 0;
    for (int i = 0; i < nKeys; ++i)
        if (rms (targetMono.data() + (size_t) i * (size_t) segLen, (int) (kHold * kFs)) > 2.0e-4f) { sounds[(size_t) i] = true; ++sounding; }
    if (sounding < 3) { out.failure = "This sound plays too few notes to be rebuilt."; return out; }

    // ---- the sources: far from the main sound in timbre, pitched, and able to play most of the keyboard
    juce::StringArray inUse;
    for (const auto& l : original.layers) { Instrument i; if (l.used() && library.findLoose (l.instrument, i)) inUse.add (i.id); }
    std::vector<Instrument> pool;
    for (float nearest : { 0.3f, 0.0f })          // far from it first; in a small library, whatever there is
    {
        pool.clear();
        for (const auto& c : mutator.byLikeness (main, nearest, nearest > 0.0f ? 0.97f : 1.0f))
            if (! inUse.contains (c.id) && c.libraryId != "reimagined" && c.f.harmonicity >= 0.6f && c.f.loopClick <= 0.3f && c.loKey <= 48 && c.hiKey >= 72 && c.f.pitchSpread <= 14.0f) pool.push_back (c);
        if (pool.size() >= 4) break;
    }
    if (pool.size() < 4) { out.failure = "REIMAGINE needs at least four other pitched instruments in the library to build from."; return out; }

    const int attempts = 2;
    for (int attempt = 0; attempt < attempts; ++attempt)
    {
        ++out.tried;
        // four of them: two that hold and two of any kind, so there are bodies and edges to choose blocks from
        std::vector<Instrument> src;
        {
            auto left = pool;
            auto take = [&] (bool mustHold) {
                std::vector<int> can;
                for (int i = 0; i < (int) left.size(); ++i) if (! mustHold || left[(size_t) i].f.sustain >= 0.35f) can.push_back (i);
                if (can.empty()) return;
                const int at = can[(size_t) rng.nextInt ((int) can.size())];
                src.push_back (left[(size_t) at]); left.erase (left.begin() + at);
            };
            take (true); take (true); take (false); take (false);
            while (src.size() < 4 && ! left.empty()) take (false);
        }
        std::vector<std::vector<float>> srcMono;
        for (size_t j = 0; j < src.size(); ++j)
        {
            if (! say (0.12f + 0.45f * ((float) attempt + (float) j / (float) src.size() * 0.5f) / (float) attempts, "Playing " + src[j].name + " at the same notes")) return out;
            Preset p = Preset::blank();
            auto& l = p.layers[0];
            l.instrument = src[j].id; l.p[lpOn] = 1.0f; l.p[lpGain] = juce::jlimit (-18.0f, 9.0f, -19.0f - src[j].f.loudnessDb);
            l.p[lpSendA] = 0.0f; l.p[lpSendB] = 0.0f; l.p[lpSendC] = 0.0f;
            if (src[j].f.pitchSpread <= 8.0f && std::abs (src[j].f.tuneCents) >= 3.0f) l.p[lpFine] = juce::jlimit (-60.0f, 60.0f, -std::round (src[j].f.tuneCents));       // every source at concert pitch
            srcMono.push_back (monoOf (playAll (p)));
        }

        // ---- note by note, Samplebrain rebuilds the original out of the sources
        const int noteLen = segLen - (int) (0.3 * kFs);
        std::vector<juce::AudioBuffer<float>> made ((size_t) nKeys);
        float loudest = 0.0f;
        for (int i = 0; i < nKeys; ++i)
        {
            if (! sounds[(size_t) i]) continue;
            if (! say (0.4f + 0.4f * ((float) attempt + (float) i / (float) nKeys) / (float) attempts, "Rebuilding note " + juce::MidiMessage::getMidiNoteName (keys[(size_t) i], true, true, 4))) return out;
            const size_t at = (size_t) i * (size_t) segLen;
            juce::AudioBuffer<float> note (2, noteLen); note.clear();
            // does the original hold at this note, or strike and fade? A held note is rebuilt from the held bodies of the
            // sources only (their attacks, dropped into the middle of a note, would stutter)
            const float earlyT = rms (targetMono.data() + at + (size_t) ((kPre + 0.8) * kFs), (int) kFs), lateT = rms (targetMono.data() + at + (size_t) ((kPre + 3.3) * kFs), (int) kFs);
            const bool targetHolds = lateT > 1.0e-3f && lateT > earlyT * 0.2f;
            {
                const std::lock_guard<std::mutex> sb (samplebrainLock());       // FFTW's planner and the engine's FFT scratch: one user at a time
                std::map<std::string, std::vector<float>> mem;
                tAtlasSounds = &mem;
                spiralcore::brain brain;
                int loaded = 0;
                for (size_t j = 0; j < srcMono.size(); ++j)
                {
                    if (srcMono[j].size() < at + (size_t) segLen) continue;
                    const float* x = srcMono[j].data() + at;
                    // only the part of the source's note that sounds (silence has nothing to offer the search)
                    const float top = peak (x, segLen);
                    if (top < 1.0e-3f) continue;
                    int begin = 0, end = segLen; while (end > kBlock * 2 && std::abs (x[end - 1]) < top * 0.003f) --end;
                    if (targetHolds)
                    {
                        begin = (int) ((kPre + 0.15) * kFs); end = juce::jmin (end, (int) ((kPre + kHold) * kFs));
                        // a source that has died away by then has no body to give
                        if (end - begin < kBlock * 4 || rms (x + begin + (end - begin) / 2, (end - begin) / 2) < top * 0.02f) continue;
                    }
                    const auto name = "source" + std::to_string (j);
                    auto& held = mem[name];
                    held.assign (x + begin, x + end);
                    bringToPitch (held, midiToHz ((float) keys[(size_t) i]));
                    brain.load_sound (name, spiralcore::brain::MIX);
                    ++loaded;
                }
                if (loaded > 0)
                {
                    brain.init (kBlock, kBlock - kHop, spiralcore::window::HANN);
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        const size_t shift = ch == 0 ? 0 : (size_t) kHop / 2;      // the right side is cut half a hop later: different blocks, a wide result
                        std::vector<float> target (targetMono.begin() + (long) (at + shift), targetMono.begin() + (long) (at + shift + (size_t) noteLen));
                        // loyal to a source's own next block: long true stretches of each instrument (a block repeated out of step would
                        // buzz at the block rate instead of sounding its note), moving on when another fits much better
                        auto m = mosaicOf (target, brain, mem, 0.3f, 0.85f);
                        if (m.empty()) continue;
                        m.resize ((size_t) noteLen, 0.0f);
                        centre (m);
                        shapeLike (m, target);
                        std::copy (m.begin(), m.end(), note.getWritePointer (ch));
                    }
                }
                tAtlasSounds = nullptr;
            }
            {   // two sides rebuilt apart share nothing, and that folds badly to mono: each side is given some of the other
                float* a = note.getWritePointer (0); float* b = note.getWritePointer (1);
                for (int k = 0; k < noteLen; ++k) { const float l = a[k], r = b[k]; a[k] = 0.959f * l + 0.282f * r; b[k] = 0.282f * l + 0.959f * r; }
            }
            const float top = peakOf (note);
            if (top < 1.0e-4f) { sounds[(size_t) i] = false; continue; }
            loudest = juce::jmax (loudest, top);
            made[(size_t) i] = std::move (note);
        }
        int usable = 0; for (int i = 0; i < nKeys; ++i) if (sounds[(size_t) i] && made[(size_t) i].getNumSamples() > 0) ++usable;
        if (usable < 3 || loudest <= 0.0f) { ++out.rejected; out.failure = "The sources had nothing to rebuild this sound from."; continue; }

        // ---- recordings and an SFZ: looped where the original held, left to ring where it did not
        if (! say (0.82f, "Writing the new instrument")) return out;
        juce::String name = original.name + " Reimagined";
        juce::File folder = libraryDir().getChildFile ("reimagined").getChildFile (safeName (name));
        for (int n = 2; folder.exists() && n < 200; ++n) { name = original.name + " Reimagined " + juce::String (n); folder = libraryDir().getChildFile ("reimagined").getChildFile (safeName (name)); }
        folder.getChildFile ("samples").createDirectory();
        const float gain = 0.7f / loudest;                              // one gain for every note: the balance across the keyboard is the original's
        juce::String sfz;
        sfz << "// " << name << "\n// made by Ghost Atlas REIMAGINE (Samplebrain's mosaic engine) from the sound \"" << original.name << "\"\n// rebuilt out of: ";
        for (size_t j = 0; j < src.size(); ++j) sfz << (j > 0 ? "; " : "") << src[j].name << " (" << src[j].libraryName << ", " << (src[j].license.isEmpty() ? juce::String ("licence not stated") : src[j].license) << ")";
        sfz << "\n\n<control>\ndefault_path=samples/\n\n<global>\nampeg_attack=0.004\nampeg_release=0.9\namp_veltrack=70\n\n";
        std::vector<int> have; for (int i = 0; i < nKeys; ++i) if (sounds[(size_t) i] && made[(size_t) i].getNumSamples() > 0) have.push_back (i);
        bool wrote = true;
        for (size_t h = 0; h < have.size(); ++h)
        {
            const int i = have[h];
            auto& note = made[(size_t) i];
            note.applyGain (gain);
            const size_t at = (size_t) i * (size_t) segLen;
            auto level = [&] (double t0, double t1) { return rms (targetMono.data() + at + (size_t) ((kPre + t0) * kFs), (int) ((t1 - t0) * kFs)); };
            const float early = level (0.8, 1.8), late = level (3.3, 4.3);
            const bool holds = late > 1.0e-3f && late > early * 0.2f;                    // still there near the end of the held note
            const int first = (int) ((kPre - 0.01) * kFs);
            int loopStart = -1, loopEnd = -1, length = note.getNumSamples() - first;
            if (holds)
            {
                // the loop is made seamless in the recording itself: its last stretch fades into what came before its start
                const int a = (int) ((kPre + 1.6) * kFs), b = (int) ((kPre + 4.3) * kFs), x = (int) (0.6 * kFs);
                for (int ch = 0; ch < 2; ++ch)
                {
                    float* d = note.getWritePointer (ch);
                    for (int k = 0; k < x; ++k)
                    {
                        const float t = (float) k / (float) x;
                        d[b - x + k] = d[b - x + k] * std::sqrt (1.0f - t) + d[a - x + k] * std::sqrt (t);
                    }
                }
                loopStart = a - first; loopEnd = b - first - 1; length = b - first;
            }
            juce::AudioBuffer<float> file (2, length);
            for (int ch = 0; ch < 2; ++ch) file.copyFrom (ch, 0, note, ch, first, length);
            file.applyGainRamp (0, juce::jmin (64, length), 0.0f, 1.0f);
            if (! holds) { const int f = juce::jmin (length, (int) (0.03 * kFs)); file.applyGainRamp (length - f, f, 1.0f, 0.0f); }
            const auto fileName = "k" + juce::String (keys[(size_t) i]).paddedLeft ('0', 3) + ".wav";
            wrote = writeRecording (folder.getChildFile ("samples").getChildFile (fileName), file) && wrote;
            // each recording covers the keys up to half way to its neighbours
            const int lo = h == 0 ? juce::jmax (0, keys[(size_t) i] - 9) : (keys[(size_t) have[h - 1]] + keys[(size_t) i]) / 2 + 1;
            const int hi = h + 1 == have.size() ? juce::jmin (127, keys[(size_t) i] + 9) : (keys[(size_t) i] + keys[(size_t) have[h + 1]]) / 2;
            sfz << "<region> sample=" << fileName << " pitch_keycenter=" << keys[(size_t) i] << " lokey=" << lo << " hikey=" << hi;
            {   // what is left of being off the key after the rebuild is put right where the recording is played
                const auto mono = monoOf (file);
                float strength = 0.0f;
                const int from = juce::jmin ((int) mono.size(), (int) (0.3 * kFs)), n = juce::jmin ((int) mono.size() - from, (int) (3.0 * kFs));
                const float cents = n > (int) (0.5 * kFs) ? tuningOf (mono.data() + from, n, kFs, midiToHz ((float) keys[(size_t) i]), strength) : 0.0f;
                if (strength >= 0.2f && std::abs (cents) >= 3.0f && std::abs (cents) <= 60.0f) sfz << " tune=" << (int) std::lround (-cents);
            }
            if (holds) sfz << " loop_mode=loop_continuous loop_start=" << loopStart << " loop_end=" << loopEnd;
            else sfz << " loop_mode=no_loop";
            sfz << "\n";
        }
        const auto sfzFile = folder.getChildFile (safeName (name) + ".sfz");
        if (! wrote || ! sfzFile.replaceWithText (sfz)) { out.failure = "The new instrument could not be written to " + folder.getFullPathName(); folder.deleteRecursively(); return out; }

        // ---- into the library like any other instrument (it is listened to there), with where it came from
        if (! say (0.88f, "Listening to the new instrument")) { folder.deleteRecursively(); return out; }
        juce::StringArray notes;
        notes.add ("made by REIMAGINE from the sound \"" + original.name + "\"");
        for (const auto& s : src) notes.add ("rebuilt out of " + s.name + " (" + s.libraryName + ", " + (s.license.isEmpty() ? juce::String ("licence not stated") : s.license) + ")");
        Instrument inst; juce::String error;
        if (! library.addMade (madeHere(), sfzFile, name, "Hybrid Instruments", notes, inst, error))
        { out.failure = "The new instrument would not play" + (error.isNotEmpty() ? " (" + error + ")" : juce::String()) + "."; folder.deleteRecursively(); return out; }
        out.madeInstrument = inst.id; out.madeFolder = folder.getFullPathName();

        // ---- a sound that plays it: the original's effects and sends, as loud as the original
        Preset p = Preset::blank();
        p.sends = original.sends; p.returns = original.returns; p.master = original.master;
        p.name = original.name + ", reimagined"; p.category = original.category; p.world = original.world; p.tags = original.tags;
        p.description = "Rebuilt by REIMAGINE out of " + src[0].name + (src.size() > 1 ? ", " + src[1].name : juce::String()) + (src.size() > 2 ? " and others" : juce::String());
        p.author = "REIMAGINE"; p.factory = false; p.file.clear();
        auto& l = p.layers[0];
        l.instrument = inst.id; l.role = "primary"; l.p[lpOn] = 1.0f;
        l.p[lpGain] = juce::jlimit (-18.0f, 9.0f, std::round ((-19.0f - inst.f.loudnessDb) * 2.0f) * 0.5f);
        const auto& ml = original.layers[(size_t) mi];
        l.p[lpSendA] = ml.p[lpSendA]; l.p[lpSendB] = ml.p[lpSendB]; l.p[lpSendC] = juce::jmax (0.3f, ml.p[lpSendC]);
        l.p[lpWidth] = 1.0f;
        mutator.autoMacros (p);

        if (! say (0.94f, "Playing it and listening")) { Mutation gone; gone.madeInstrument = inst.id; gone.madeFolder = out.madeFolder; discardReimagined (library, gone); return out; }
        Heard heard = mutator.listen (p);
        if (heard.ok && (heard.peakDb > -0.5f || heard.peakDb < -9.0f)) { p.masterDb = juce::jlimit (-30.0f, 12.0f, p.masterDb + juce::jlimit (-18.0f, 12.0f, -3.0f - heard.peakDb)); heard = mutator.listen (p); }
        const auto checks = mutator.judge (heard, &heard, p);
        bool passed = heard.ok; for (const auto& c : checks) passed = passed && c.passed;
        out.checks = checks;
        if (! passed)
        {
            ++out.rejected;
            discardReimagined (library, out);
            out.madeInstrument.clear(); out.madeFolder.clear();
            continue;
        }
        const float want = before.loudnessDb - heard.loudnessDb;
        const float move = juce::jlimit (-30.0f, juce::jmax (0.0f, -1.0f - heard.peakDb), want);
        if (before.ok) p.masterDb = juce::jlimit (-30.0f, 12.0f, p.masterDb + move);
        out.loudnessDb = heard.loudnessDb + (before.ok ? move : 0.0f);
        out.originalTrimDb = before.ok ? juce::jmin (0.0f, move - want) : 0.0f;
        out.changes.add ("Played " + original.name + " at " + juce::String (usable) + " notes and rebuilt each one out of small pieces of other instruments (Samplebrain's mosaic)");
        for (const auto& s : src) out.changes.add ("Pieces taken from " + s.name + " (" + s.libraryName + ")");
        out.changes.add ("Gave the result the original's loudness shape, and looped it where the original held");
        out.changes.add ("Added it to your library as the instrument \"" + name + "\"");
        out.ok = true; out.preset = p; out.failure.clear();
        say (1.0f, "Done");
        return out;
    }
    juce::String why;
    for (const auto& c : out.checks) if (! c.passed) why << (why.isEmpty() ? "" : "; ") << c.name.toLowerCase() << " (" << c.found << ")";
    if (why.isNotEmpty()) out.failure = "What came out did not hold up: " + why + ". Press Reimagine again for other sources.";
    else if (out.failure.isEmpty()) out.failure = "Nothing came of it this time. Press Reimagine again for other sources.";
    return out;
}
} // namespace atlas
