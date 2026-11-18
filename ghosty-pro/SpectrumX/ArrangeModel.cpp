#include "ArrangeModel.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <limits>
#include <set>

namespace arr {

// ------------------------------------------------------------------ payloads
juce::uint64 newPayloadUid() { static std::atomic<juce::uint64> n { 1 }; return n.fetch_add(1); }
static void sanitiseNotes(std::vector<Note>& notes) {
    std::vector<Note> ok; ok.reserve(std::min<size_t>(notes.size(), 100000));
    for (auto& n : notes) {
        if (ok.size() >= 100000) break;
        if (!std::isfinite(n.start) || !std::isfinite(n.length) || !std::isfinite(n.velocity)) continue;
        Note x = n; x.pitch = juce::jlimit(0, 127, x.pitch); x.velocity = juce::jlimit(0.0f, 1.0f, x.velocity);
        x.start = juce::jlimit(0.0, kMaxSoundSeconds, x.start); x.length = juce::jlimit(0.0, kMaxSoundSeconds, x.length); ok.push_back(x);
    }
    notes.swap(ok);
}
PayloadPtr makePayload(const juce::AudioBuffer<float>& audio, double sampleRate, const juce::String& name, std::vector<Note> notes, SourceRef source, double bpm) {
    auto p = std::make_shared<Payload>();
    const double sr = clampF(sampleRate, 8000.0, 384000.0, 48000.0);
    const int n = (int) std::min<juce::int64>(audio.getNumSamples(), (juce::int64) (kMaxSoundSeconds * sr));
    const int nc = juce::jlimit(0, 2, audio.getNumChannels());
    p->audio.setSize(std::max(1, nc), std::max(0, n)); p->audio.clear();
    for (int ch = 0; ch < nc; ++ch) p->audio.copyFrom(ch, 0, audio, ch, 0, n);
    quantise24(p->audio, flacExponent(p->audio));   // also clears NaN / Inf; inaudible (24-bit), makes save / load bit-exact
    p->sampleRate = sr; p->name = name.substring(0, 120); sanitiseNotes(notes); p->notes = std::move(notes); p->source = std::move(source);
    p->bpm = clampF(bpm, 0.0, 999.0, 0.0); if (p->bpm < 20) p->bpm = 0;
    if (p->source.name.isEmpty()) p->source.name = p->name;
    p->rendered = ! (p->source.page == "File" || p->source.page == "Shared");   // a file the user brought in vs anything a page rendered
    return p;
}

// ------------------------------------------------------------------ re-render registry (one per Session)
void SourceRegistration::reset() {
    if (!entry) return;
    { const std::lock_guard<std::recursive_mutex> l(entry->m); entry->fn = nullptr; }   // waits for a running re-render
    entry.reset();
}
SourceRegistration SourceRegistry::add(const juce::String& page, Rerender fn) {
    SourceRegistration r; if (!fn) return r;
    r.entry = std::make_shared<SourceRegistration::Entry>(); r.entry->fn = std::move(fn); r.entry->page = page;
    const std::lock_guard<std::mutex> l(m);
    entries.erase(std::remove_if(entries.begin(), entries.end(), [](auto& w) { return w.expired(); }), entries.end());
    entries.push_back(r.entry); return r;
}
PayloadPtr SourceRegistry::rerender(const SourceRef& ref) {
    std::shared_ptr<SourceRegistration::Entry> e;
    { const std::lock_guard<std::mutex> l(m); for (auto it = entries.rbegin(); it != entries.rend(); ++it) if (auto x = it->lock()) if (x->page == ref.page) { e = x; break; } }
    if (!e) return nullptr;
    const std::lock_guard<std::recursive_mutex> l(e->m);   // the owner can't unregister (or die) while it runs
    return e->fn ? e->fn(ref) : nullptr;
}
bool SourceRegistry::has(const juce::String& page) {
    const std::lock_guard<std::mutex> l(m);
    for (auto& w : entries) if (auto x = w.lock()) { const std::lock_guard<std::recursive_mutex> g(x->m); if (x->page == page && x->fn) return true; }
    return false;
}

// ------------------------------------------------------------------ fades / envelopes
const char* curveName(Curve c) { static const char* N[] = { "LINEAR", "EQUAL POWER", "EXPONENTIAL", "S-CURVE" }; return N[juce::jlimit(0, 3, (int) c)]; }
float fadeInGain(Curve c, double x) {
    x = clampF(x, 0.0, 1.0, 0.0);
    switch (c) {
        case Curve::EqualPower:  return (float) std::sin(x * juce::MathConstants<double>::halfPi);
        case Curve::Exponential: return (float) (x * x * x);                                   // slow start, quick end (a log-like swell in dB)
        case Curve::SCurve:      return (float) (x * x * (3.0 - 2.0 * x));
        case Curve::Linear: case Curve::Count: default: return (float) x;
    }
}
const char* morphShapeName(MorphShape s) { static const char* N[] = { "S-CURVE", "LINEAR", "EARLY", "LATE" }; return N[juce::jlimit(0, 3, (int) s)]; }
double morphCurve(MorphShape s, double x) {
    x = clampF(x, 0.0, 1.0, 0.0);
    switch (s) {
        case MorphShape::Linear: return x;
        case MorphShape::Early:  return 1.0 - (1.0 - x) * (1.0 - x);   // reaches the next sound early
        case MorphShape::Late:   return x * x;                         // holds the first sound longer
        case MorphShape::SCurve: case MorphShape::Count: default: return x * x * (3 - 2 * x);
    }
}
float evalEnvelope(const Envelope& e, double beat, float def) {
    if (e.empty()) return def;
    if (beat <= e.front().beat) return e.front().value;
    if (beat >= e.back().beat) return e.back().value;
    auto it = std::upper_bound(e.begin(), e.end(), beat, [](double b, const EnvPoint& p) { return b < p.beat; });
    const auto& b = *it; const auto& a = *(it - 1); const double d = b.beat - a.beat;
    return d > 1e-12 ? (float) (a.value + (b.value - a.value) * (beat - a.beat) / d) : b.value;
}
void sortEnvelope(Envelope& e) { std::stable_sort(e.begin(), e.end(), [](const EnvPoint& a, const EnvPoint& b) { return a.beat < b.beat; }); }

// ------------------------------------------------------------------ clip
double Clip::processedSeconds(double bpm) const {
    if (!source) return 0;
    const double rate = keepLength ? 1.0 : std::pow(2.0, pitch / 12.0);
    return source->seconds() * juce::jlimit(0.25f, 4.0f, stretch) * warpRatio(bpm) / rate;
}
juce::String Clip::baseKey() const {
    return juce::String((juce::int64) (source ? source->uid : 0)) + "|" + juce::String(pitch, 3) + "|" + (keepLength ? "k" : "r") + "|" + juce::String(stretch, 4) + "|" + (reverse ? "R" : "F");
}
juce::String Clip::processKey(double bpm) const { return baseKey() + "|w" + juce::String(warpRatio(bpm), 6); }

// ------------------------------------------------------------------ arrangement
Arrangement::Arrangement() { for (int i = 0; i < 4; ++i) addTrack(); }
Track& Arrangement::addTrack(const juce::String& name) {
    tracks.emplace_back(); auto& t = tracks.back(); t.id = nextId++; t.colour = (int) tracks.size() - 1;
    t.name = name.isNotEmpty() ? name : "TRACK " + juce::String(tracks.size());
    return t;
}
void Arrangement::removeTrack(int index) { if (index >= 0 && index < (int) tracks.size()) tracks.erase(tracks.begin() + index); }
Clip* Arrangement::addClip(int track, PayloadPtr source, double startBeats, double lengthBeats) {
    if (!source || !source->valid()) return nullptr;
    while (track >= (int) tracks.size() && (int) tracks.size() < kMaxTracks) addTrack();
    if (track < 0 || track >= (int) tracks.size()) return nullptr;
    Clip c; c.id = nextId++; c.source = source; c.name = source->name; c.colour = (int) (c.id % 8); c.start = clampF(startBeats, 0.0, kMaxBeats, 0.0);
    c.warp = source->bpm > 0;
    c.length = lengthBeats > 0 && std::isfinite(lengthBeats) ? lengthBeats : std::max(kMinClipBeats, c.naturalBeats(bpm));
    c.length = juce::jlimit(kMinClipBeats, kMaxBeats, c.length);
    auto& v = tracks[(size_t) track].clips; v.push_back(c); sortClips();
    return findClip(c.id);
}
Clip* Arrangement::findClip(juce::uint32 id, int* trackIndex) {
    for (size_t t = 0; t < tracks.size(); ++t) for (auto& c : tracks[t].clips) if (c.id == id) { if (trackIndex) *trackIndex = (int) t; return &c; }
    return nullptr;
}
const Clip* Arrangement::findClip(juce::uint32 id, int* trackIndex) const { return const_cast<Arrangement*>(this)->findClip(id, trackIndex); }
bool Arrangement::removeClip(juce::uint32 id) {
    for (auto& t : tracks) { auto it = std::find_if(t.clips.begin(), t.clips.end(), [id](const Clip& c) { return c.id == id; }); if (it != t.clips.end()) { t.clips.erase(it); return true; } }
    return false;
}
Clip* Arrangement::duplicateClip(juce::uint32 id, double newStart) {
    int ti = 0; auto* c = findClip(id, &ti); if (!c) return nullptr;
    Clip d = *c; d.id = nextId++; d.start = clampF(newStart, 0.0, kMaxBeats, 0.0); tracks[(size_t) ti].clips.push_back(d); sortClips(); return findClip(d.id);
}
Clip* Arrangement::splitClip(juce::uint32 id, double at) {
    int ti = 0; auto* c = findClip(id, &ti); if (!c || !std::isfinite(at) || at <= c->start + kMinClipBeats || at >= c->end() - kMinClipBeats) return nullptr;
    Clip r = *c; r.id = nextId++; const double cut = at - c->start;
    r.start = at; r.length = c->length - cut; r.offset = c->offset + beatsToSeconds(cut); r.fadeIn = 0;
    // the envelope: each half keeps its part (a point at the cut keeps the level continuous)
    if (!c->gainEnv.empty()) {
        const float v = evalEnvelope(c->gainEnv, cut, 0); Envelope L, R;
        for (auto& p : c->gainEnv) { if (p.beat < cut) L.push_back(p); else R.push_back({ p.beat - cut, p.value }); }
        L.push_back({ cut, v }); R.insert(R.begin(), { 0.0, v }); c->gainEnv = L; r.gainEnv = R;
    }
    if (c->loop && c->source) { const double len = c->processedSeconds(bpm); if (len > 0) r.offset = std::fmod(r.offset, len); }
    c->length = cut; c->fadeOut = 0; c->morph = false;
    tracks[(size_t) ti].clips.push_back(r); sortClips(); return findClip(r.id);
}
bool Arrangement::moveClip(juce::uint32 id, int toTrack, double newStart) {
    int ti = 0; auto* c = findClip(id, &ti); if (!c || tracks.empty()) return false;
    toTrack = juce::jlimit(0, (int) tracks.size() - 1, toTrack); newStart = clampF(newStart, 0.0, kMaxBeats, c->start);
    if (toTrack == ti) { c->start = newStart; sortClips(); return true; }
    Clip m = *c; m.start = newStart; removeClip(id); tracks[(size_t) toTrack].clips.push_back(m); sortClips(); return true;
}
void Arrangement::sortClips() { for (auto& t : tracks) std::stable_sort(t.clips.begin(), t.clips.end(), [](const Clip& a, const Clip& b) { return a.start < b.start; }); }
double Arrangement::legalSnap(double v, int bpb) {
    const double vals[] = { (double) bpb, 1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0 };
    if (!std::isfinite(v)) return 1.0;
    double best = 1.0, d = 1e300; for (double x : vals) if (std::abs(x - v) < d) { d = std::abs(x - v); best = x; }
    return d < 1e-6 ? best : 1.0;
}
static void sanitiseEnvelope(Envelope& e, double maxBeat, float lo, float hi, float def) {
    Envelope ok; for (auto& p : e) { if ((int) ok.size() >= kMaxEnvPoints) break; if (!std::isfinite(p.beat)) continue; ok.push_back({ juce::jlimit(0.0, maxBeat, p.beat), (float) clampF(p.value, lo, hi, def) }); }
    sortEnvelope(ok); e.swap(ok);
}
void Arrangement::sanitise() {
    bpm = clampF(bpm, 20.0, 400.0, 120.0); beatsPerBar = juce::jlimit(1, 16, beatsPerBar);
    loopStart = clampF(loopStart, 0.0, kMaxBeats, 0.0); loopEnd = clampF(loopEnd, 0.0, kMaxBeats, 16.0);
    if (loopEnd < loopStart + kMinClipBeats) loopEnd = std::min(kMaxBeats + 1, loopStart + 4.0 * beatsPerBar);
    snapBeats = legalSnap(snapBeats, beatsPerBar);
    while ((int) tracks.size() > kMaxTracks) tracks.pop_back();
    for (auto& t : tracks) {
        t.gainDb = (float) clampF(t.gainDb, -60.0, 12.0, 0.0); t.pan = (float) clampF(t.pan, -1.0, 1.0, 0.0); t.autoLane = juce::jlimit(0, 1, t.autoLane); t.colour = juce::jlimit(0, 7, t.colour);
        sanitiseEnvelope(t.volEnv, kMaxBeats, -60.0f, 12.0f, 0.0f); sanitiseEnvelope(t.panEnv, kMaxBeats, -1.0f, 1.0f, 0.0f);
        for (auto& c : t.clips) {
            c.start = clampF(c.start, 0.0, kMaxBeats, 0.0); c.length = clampF(c.length, kMinClipBeats, kMaxBeats, 4.0); c.offset = clampF(c.offset, 0.0, kMaxOffsetSeconds, 0.0);
            c.pitch = (float) clampF(c.pitch, -24.0, 24.0, 0.0); c.stretch = (float) clampF(c.stretch, 0.25, 4.0, 1.0);
            c.fadeIn = clampF(c.fadeIn, 0.0, c.length, 0.0); c.fadeOut = clampF(c.fadeOut, 0.0, c.length, 0.0);
            c.gainDb = (float) clampF(c.gainDb, -60.0, 24.0, 0.0); c.pan = (float) clampF(c.pan, -1.0, 1.0, 0.0); c.colour = juce::jlimit(0, 7, c.colour);
            c.morphAmount = (float) clampF(c.morphAmount, 0.0, 1.0, 1.0); c.morphSmear = (float) clampF(c.morphSmear, 0.0, 1.0, 0.0);
            c.fadeInCurve = (Curve) juce::jlimit(0, (int) Curve::Count - 1, (int) c.fadeInCurve); c.fadeOutCurve = (Curve) juce::jlimit(0, (int) Curve::Count - 1, (int) c.fadeOutCurve);
            c.morphShape = (MorphShape) juce::jlimit(0, (int) MorphShape::Count - 1, (int) c.morphShape);
            sanitiseEnvelope(c.gainEnv, c.length, -60.0f, 12.0f, 0.0f);
        }
    }
    std::vector<Marker> ms; for (auto& m : markers) if (std::isfinite(m.beat) && ms.size() < 256) ms.push_back({ juce::jlimit(0.0, kMaxBeats, m.beat), m.name.substring(0, 40) });
    std::stable_sort(ms.begin(), ms.end(), [](const Marker& a, const Marker& b) { return a.beat < b.beat; }); markers.swap(ms);
    sortClips();
}
double Arrangement::lengthBeats() const { double e = 0; for (auto& t : tracks) for (auto& c : t.clips) e = std::max(e, c.end()); return e; }
bool Arrangement::anySolo() const { for (auto& t : tracks) if (t.solo) return true; return false; }
bool Arrangement::trackAudible(int i) const { if (i < 0 || i >= (int) tracks.size()) return false; const auto& t = tracks[(size_t) i]; return anySolo() ? t.solo : !t.mute; }
int Arrangement::clipCount() const { int n = 0; for (auto& t : tracks) n += (int) t.clips.size(); return n; }

// ------------------------------------------------------------------ FLAC
// Lossless for what Arrange holds: sounds are kept on a 24-bit grid scaled by a power of two (quantise24, done once when a
// sound arrives), so FLAC stores them bit-exactly and a reloaded project renders exactly like the one that was saved.
int flacExponent(const juce::AudioBuffer<float>& audio) {
    float peak = 0; for (int ch = 0; ch < audio.getNumChannels(); ++ch) { auto* d = audio.getReadPointer(ch); for (int i = 0; i < audio.getNumSamples(); ++i) if (std::isfinite(d[i])) peak = std::max(peak, std::abs(d[i])); }
    if (!(peak > 1e-30f)) return 0;
    return juce::jlimit(-40, 40, (int) std::floor(std::log2((1.0 - 1.0 / 4194304.0) / peak)));
}
void quantise24(juce::AudioBuffer<float>& audio, int e) {
    const double s = std::ldexp(1.0, e + 23);
    for (int ch = 0; ch < audio.getNumChannels(); ++ch) { auto* d = audio.getWritePointer(ch); for (int i = 0; i < audio.getNumSamples(); ++i) {
        const double q = juce::jlimit(-8388608.0, 8388607.0, std::round(std::isfinite(d[i]) ? d[i] * s : 0.0)); d[i] = (float) (q / s); } }
}
juce::MemoryBlock encodeFlac(const juce::AudioBuffer<float>& audio, double sampleRate, int e) {
    juce::MemoryBlock out; const int n = audio.getNumSamples(), nc = juce::jlimit(1, 2, audio.getNumChannels()); if (n <= 0 || audio.getNumChannels() <= 0) return out;
    auto* os = new juce::MemoryOutputStream(out, false); juce::FlacAudioFormat flac;
    std::unique_ptr<juce::AudioFormatWriter> w(flac.createWriterFor(os, clampF(sampleRate, 8000.0, 384000.0, 48000.0), (unsigned) nc, 24, {}, 5));   // NOT-EXPORT: the clip audio inside the project state (memory, FLAC)
    if (!w) { delete os; out.reset(); return out; }
    const double s = std::ldexp(1.0, e + 23); constexpr int B = 8192; std::vector<int> buf((size_t) (nc * B));
    for (int p = 0; p < n; p += B) {
        const int m = std::min(B, n - p); const int* chans[3] = { nullptr, nullptr, nullptr };
        for (int ch = 0; ch < nc; ++ch) { auto* src = audio.getReadPointer(ch); int* d = buf.data() + ch * B;
            for (int i = 0; i < m; ++i) { const double v = std::isfinite(src[p + i]) ? src[p + i] * s : 0.0; d[i] = (int) juce::jlimit(-8388608.0, 8388607.0, std::round(v)) * 256; } chans[ch] = d; }
        if (!w->write(chans, m)) break;
    }
    w.reset(); return out;
}
juce::MemoryBlock encodeFlac(const juce::AudioBuffer<float>& audio, double sampleRate) { return encodeFlac(audio, sampleRate, std::min(0, flacExponent(audio))); }
bool decodeFlac(const juce::MemoryBlock& flacData, juce::AudioBuffer<float>& audio, double& sampleRate, int e) {
    try {
        juce::FlacAudioFormat flac;
        std::unique_ptr<juce::AudioFormatReader> r(flac.createReaderFor(new juce::MemoryInputStream(flacData, false), true));
        if (!r || r->lengthInSamples <= 0 || r->numChannels < 1 || !(r->sampleRate >= 8000 && r->sampleRate <= 384000)) return false;
        // never trust STREAMINFO: cap the length by time and by what the blob can possibly hold (a FLAC frame of up to 65535
        // samples costs at least ~16 bytes), and grow the buffer as it decodes instead of allocating the claimed size up front
        const juce::int64 cap = std::min((juce::int64) (kMaxSoundSeconds * r->sampleRate), (juce::int64) flacData.getSize() * 4096 + 65536);
        if (r->lengthInSamples >= (juce::int64) std::numeric_limits<int>::max() || r->lengthInSamples > cap) return false;
        const juce::int64 claimed = r->lengthInSamples;
        const int nc = (int) std::min(2u, r->numChannels); constexpr int B = 8192; std::vector<int> buf((size_t) (2 * B));
        juce::AudioBuffer<float> out(nc, 0); juce::int64 n = 0;
        const double s = std::ldexp(1.0, e + 23);
        while (n < claimed) {
            const int m = (int) std::min<juce::int64>(B, claimed - n); int* chans[3] = { buf.data(), nc > 1 ? buf.data() + B : nullptr, nullptr };
            if (!r->read(chans, nc, n, m, true)) break;
            if (out.getNumSamples() < n + m) out.setSize(nc, (int) std::min<juce::int64>(claimed, std::max<juce::int64>(n + m, (juce::int64) out.getNumSamples() * 2)), true, false, true);
            for (int ch = 0; ch < nc; ++ch) { float* d = out.getWritePointer(ch, (int) n); const int* src = chans[ch]; for (int i = 0; i < m; ++i) d[i] = (float) ((src[i] / 256) / s); }
            n += m;
        }
        if (n <= 0) return false;
        audio.setSize(nc, (int) n); for (int ch = 0; ch < nc; ++ch) audio.copyFrom(ch, 0, out, ch, 0, (int) n);
        sampleRate = r->sampleRate; return true;
    } catch (const std::bad_alloc&) { return false; }
}
bool decodeFlac(const juce::MemoryBlock& flacData, juce::AudioBuffer<float>& audio, double& sampleRate) { return decodeFlac(flacData, audio, sampleRate, 0); }

// ------------------------------------------------------------------ state
namespace {
constexpr int kStateVersion = 2;
juce::MemoryBlock envBlob(const Envelope& e) { juce::MemoryOutputStream m; m.writeInt((int) e.size()); for (auto& p : e) { m.writeDouble(p.beat); m.writeFloat(p.value); } return m.getMemoryBlock(); }
Envelope envFrom(const juce::var& v) {
    Envelope e; auto* mb = v.getBinaryData(); if (!mb) return e; juce::MemoryInputStream m(*mb, false);
    const int n = m.readInt(); if (n < 0 || n > kMaxEnvPoints || (size_t) n * 12 > mb->getSize()) return e;
    for (int i = 0; i < n; ++i) { EnvPoint p; p.beat = m.readDouble(); p.value = m.readFloat(); e.push_back(p); }
    return e;
}
juce::ValueTree payloadTree(const Payload& p, int index, bool keepRendered = true) {
    juce::ValueTree s("sound"); s.setProperty("index", index, nullptr); s.setProperty("name", p.name, nullptr); s.setProperty("rate", p.sampleRate, nullptr);
    s.setProperty("bpm", p.bpm, nullptr); s.setProperty("page", p.source.page, nullptr); s.setProperty("srcName", p.source.name, nullptr);
    if (p.rendered) s.setProperty("made", true, nullptr);
    if (p.rendered && !keepRendered) { s.setProperty("stripped", true, nullptr); return s; }   // demo save: no audio / notes / re-render state
    if (p.source.state.getSize() > 0) s.setProperty("srcState", juce::var(p.source.state), nullptr);
    const int e = flacExponent(p.audio); s.setProperty("exp", e, nullptr); s.setProperty("len", p.audio.getNumSamples(), nullptr);
    s.setProperty("flac", juce::var(encodeFlac(p.audio, p.sampleRate, e)), nullptr);
    if (!p.notes.empty()) { juce::MemoryOutputStream m; m.writeInt((int) p.notes.size()); for (auto& n : p.notes) { m.writeInt(n.pitch); m.writeFloat(n.velocity); m.writeDouble(n.start); m.writeDouble(n.length); } s.setProperty("notes", juce::var(m.getMemoryBlock()), nullptr); }
    return s;
}
PayloadPtr payloadFrom(const juce::ValueTree& s) {
    auto p = std::make_shared<Payload>(); p->name = s["name"].toString().substring(0, 120); p->bpm = clampF((double) s["bpm"], 0.0, 999.0, 0.0); if (p->bpm < 20) p->bpm = 0;
    p->source.page = s["page"].toString().substring(0, 64); p->source.name = s["srcName"].toString().substring(0, 120);
    p->rendered = (bool) s.getProperty("made", false);   // older states: imported
    if (auto* mb = s["srcState"].getBinaryData()) p->source.state = *mb;
    auto* fl = s["flac"].getBinaryData(); if (!fl) return nullptr;
    double rate = 48000; if (!decodeFlac(*fl, p->audio, rate, juce::jlimit(-40, 40, (int) s.getProperty("exp", 0)))) return nullptr; p->sampleRate = rate;
    const int len = (int) s.getProperty("len", 0); if (len > 0 && len < p->audio.getNumSamples()) p->audio.setSize(p->audio.getNumChannels(), len, true);
    if (auto* nb = s["notes"].getBinaryData()) {
        juce::MemoryInputStream m(*nb, false); const int n = m.readInt();
        if (n > 0 && n <= 100000 && (size_t) n * 24 <= nb->getSize()) for (int i = 0; i < n; ++i) { Note x; x.pitch = m.readInt(); x.velocity = m.readFloat(); x.start = m.readDouble(); x.length = m.readDouble(); p->notes.push_back(x); }
        sanitiseNotes(p->notes);
    }
    return p;
}
}
juce::ValueTree toTree(const Arrangement& a) { return toTree(a, true, nullptr); }
juce::ValueTree toTree(const Arrangement& a, bool keepRendered, int* stripped) {
    juce::ValueTree t("GhostyArrange"); t.setProperty("version", kStateVersion, nullptr);
    t.setProperty("bpm", a.bpm, nullptr); t.setProperty("beatsPerBar", a.beatsPerBar, nullptr); t.setProperty("loopStart", a.loopStart, nullptr); t.setProperty("loopEnd", a.loopEnd, nullptr);
    t.setProperty("loopOn", a.loopOn, nullptr); t.setProperty("snapBeats", a.snapBeats, nullptr); t.setProperty("snapOn", a.snapOn, nullptr); t.setProperty("nextId", (int) a.nextId, nullptr);
    std::map<const Payload*, int> index; juce::ValueTree sounds("sounds");
    for (auto& tr : a.tracks) for (auto& c : tr.clips) if (c.source && !index.count(c.source.get())) { const int i = (int) index.size(); index[c.source.get()] = i; sounds.addChild(payloadTree(*c.source, i, keepRendered), -1, nullptr); if (stripped && c.source->rendered && !keepRendered) ++*stripped; }
    t.addChild(sounds, -1, nullptr);
    juce::ValueTree ms("markers"); for (auto& m : a.markers) { juce::ValueTree x("marker"); x.setProperty("beat", m.beat, nullptr); x.setProperty("name", m.name, nullptr); ms.addChild(x, -1, nullptr); }
    t.addChild(ms, -1, nullptr);
    for (auto& tr : a.tracks) {
        juce::ValueTree v("track"); v.setProperty("id", (int) tr.id, nullptr); v.setProperty("name", tr.name, nullptr); v.setProperty("colour", tr.colour, nullptr);
        v.setProperty("gainDb", tr.gainDb, nullptr); v.setProperty("pan", tr.pan, nullptr); v.setProperty("mute", tr.mute, nullptr); v.setProperty("solo", tr.solo, nullptr);
        v.setProperty("collapsed", tr.collapsed, nullptr); v.setProperty("showAuto", tr.showAuto, nullptr); v.setProperty("autoLane", tr.autoLane, nullptr);
        if (!tr.volEnv.empty()) v.setProperty("volEnv", juce::var(envBlob(tr.volEnv)), nullptr);
        if (!tr.panEnv.empty()) v.setProperty("panEnv", juce::var(envBlob(tr.panEnv)), nullptr);
        for (auto& c : tr.clips) {
            juce::ValueTree k("clip"); k.setProperty("id", (int) c.id, nullptr); k.setProperty("sound", c.source ? index[c.source.get()] : -1, nullptr); k.setProperty("name", c.name, nullptr); k.setProperty("colour", c.colour, nullptr);
            k.setProperty("start", c.start, nullptr); k.setProperty("length", c.length, nullptr); k.setProperty("offset", c.offset, nullptr); k.setProperty("pitch", c.pitch, nullptr); k.setProperty("keepLength", c.keepLength, nullptr);
            k.setProperty("stretch", c.stretch, nullptr); k.setProperty("warp", c.warp, nullptr); k.setProperty("loop", c.loop, nullptr); k.setProperty("reverse", c.reverse, nullptr);
            k.setProperty("fadeIn", c.fadeIn, nullptr); k.setProperty("fadeOut", c.fadeOut, nullptr);
            k.setProperty("fadeInCurve", (int) c.fadeInCurve, nullptr); k.setProperty("fadeOutCurve", (int) c.fadeOutCurve, nullptr); k.setProperty("gainDb", c.gainDb, nullptr); k.setProperty("pan", c.pan, nullptr);
            if (!c.gainEnv.empty()) k.setProperty("gainEnv", juce::var(envBlob(c.gainEnv)), nullptr);
            k.setProperty("morph", c.morph, nullptr); k.setProperty("morphShape", (int) c.morphShape, nullptr); k.setProperty("morphAmount", c.morphAmount, nullptr); k.setProperty("morphSmear", c.morphSmear, nullptr);
            k.setProperty("mute", c.mute, nullptr); v.addChild(k, -1, nullptr);
        }
        t.addChild(v, -1, nullptr);
    }
    return t;
}
bool fromTree(const juce::ValueTree& t, Arrangement& a, juce::String* error) {
    if (!t.hasType("GhostyArrange")) { if (error) *error = "Not an Arrange state"; return false; }
    try {
        Arrangement n; n.tracks.clear();
        n.bpm = (double) t.getProperty("bpm", 120.0); n.beatsPerBar = (int) t.getProperty("beatsPerBar", 4);
        n.loopStart = (double) t.getProperty("loopStart", 0.0); n.loopEnd = (double) t.getProperty("loopEnd", 16.0);
        n.loopOn = t.getProperty("loopOn", false); n.snapBeats = (double) t.getProperty("snapBeats", 1.0); n.snapOn = t.getProperty("snapOn", true);
        std::vector<PayloadPtr> sounds; auto st = t.getChildWithName("sounds");
        for (int i = 0; i < st.getNumChildren() && i < 4096; ++i) { auto p = payloadFrom(st.getChild(i)); if (!p && error) *error = "A sound could not be decoded"; sounds.push_back(p); }
        auto ms = t.getChildWithName("markers"); for (int i = 0; i < ms.getNumChildren(); ++i) n.markers.push_back({ (double) ms.getChild(i)["beat"], ms.getChild(i)["name"].toString() });
        juce::uint32 maxId = 0;
        for (int i = 0; i < t.getNumChildren(); ++i) {
            auto v = t.getChild(i); if (!v.hasType("track") || (int) n.tracks.size() >= kMaxTracks) continue;
            Track tr; tr.id = (juce::uint32) (int) v["id"]; tr.name = v["name"].toString().substring(0, 40); tr.colour = v["colour"]; tr.gainDb = (float) (double) v.getProperty("gainDb", 0.0);
            tr.pan = (float) (double) v.getProperty("pan", 0.0); tr.mute = v["mute"]; tr.solo = v["solo"]; tr.collapsed = v["collapsed"]; tr.showAuto = v["showAuto"]; tr.autoLane = v["autoLane"];
            tr.volEnv = envFrom(v["volEnv"]); tr.panEnv = envFrom(v["panEnv"]); maxId = std::max(maxId, tr.id);
            for (int j = 0; j < v.getNumChildren() && tr.clips.size() < 20000; ++j) {
                auto k = v.getChild(j); if (!k.hasType("clip")) continue; const int si = k["sound"]; if (si < 0 || si >= (int) sounds.size() || !sounds[(size_t) si]) continue;
                Clip c; c.id = (juce::uint32) (int) k["id"]; c.source = sounds[(size_t) si]; c.name = k["name"].toString().substring(0, 120); c.colour = k["colour"];
                c.start = k["start"]; c.length = k["length"]; c.offset = k["offset"]; c.pitch = (float) (double) k["pitch"]; c.keepLength = k.getProperty("keepLength", true);
                c.stretch = (float) (double) k.getProperty("stretch", 1.0); c.warp = k.getProperty("warp", false); c.loop = k.getProperty("loop", false);
                c.reverse = k["reverse"]; c.fadeIn = k["fadeIn"]; c.fadeOut = k["fadeOut"];
                c.fadeInCurve = (Curve) (int) k["fadeInCurve"]; c.fadeOutCurve = (Curve) (int) k["fadeOutCurve"];
                c.gainDb = (float) (double) k["gainDb"]; c.pan = (float) (double) k["pan"]; c.gainEnv = envFrom(k["gainEnv"]);
                c.morph = k["morph"]; c.morphShape = (MorphShape) (int) k.getProperty("morphShape", 0); c.morphAmount = (float) (double) k.getProperty("morphAmount", 1.0); c.morphSmear = (float) (double) k.getProperty("morphSmear", 0.0);
                c.mute = k["mute"]; maxId = std::max(maxId, c.id); tr.clips.push_back(c);
            }
            n.tracks.push_back(tr);
        }
        if (n.tracks.empty()) n.addTrack();
        n.sanitise();
        n.nextId = std::max((juce::uint32) (int) t.getProperty("nextId", 1), maxId + 1);
        a = std::move(n); return true;
    } catch (const std::bad_alloc&) { if (error) *error = "The state is too large"; return false; }
}
juce::MemoryBlock saveState(const Arrangement& a) { return saveState(a, true, nullptr); }
juce::MemoryBlock saveState(const Arrangement& a, bool keepRendered, int* stripped) { juce::MemoryBlock mb; juce::MemoryOutputStream os(mb, false); toTree(a, keepRendered, stripped).writeToStream(os); return mb; }   // NOT-EXPORT: project state
bool loadState(const void* data, size_t size, Arrangement& a, juce::String* error) {
    try {
        if (data == nullptr || size == 0) { if (error) *error = "Empty state"; return false; }
        auto t = juce::ValueTree::readFromData(data, size); if (!t.isValid()) { if (error) *error = "Unreadable state"; return false; }
        return fromTree(t, a, error);
    } catch (const std::bad_alloc&) { if (error) *error = "The state is too large"; return false; }
}

// ------------------------------------------------------------------ undo
size_t History::payloadBytesHeld() const {
    std::set<const Payload*> now; if (!past.empty()) for (auto& t : past.back().tracks) for (auto& c : t.clips) now.insert(c.source.get());
    std::set<const Payload*> held; size_t bytes = 0;
    for (auto* v : { &past, &future }) for (auto& a : *v) for (auto& t : a.tracks) for (auto& c : t.clips)
        if (c.source && !now.count(c.source.get()) && held.insert(c.source.get()).second) bytes += (size_t) c.source->audio.getNumSamples() * (size_t) c.source->audio.getNumChannels() * sizeof(float);
    return bytes;
}
void History::trim() {
    while (past.size() > kMax) past.erase(past.begin());
    while (past.size() > 1 && payloadBytesHeld() > maxBytes) past.erase(past.begin());
}
void History::push(const Arrangement& before) { past.push_back(before); future.clear(); trim(); }
bool History::undo(Arrangement& a) { if (past.empty()) return false; future.push_back(a); a = past.back(); past.pop_back(); return true; }
bool History::redo(Arrangement& a) { if (future.empty()) return false; past.push_back(a); a = future.back(); future.pop_back(); return true; }

} // namespace arr
