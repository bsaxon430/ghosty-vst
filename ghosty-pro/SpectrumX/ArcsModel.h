#pragma once
// ARCS page (Ghosty Spectrum): the drawing model.
// A page is a sheet of LINES ("arcs"): polylines with x = page position (0..1 of the page length) and y = pitch (MIDI, so
// the screen is log frequency). Each line is one voice that glides along what was drawn while the playhead crosses it:
//   source  - its own hand-drawn single-cycle WAVEFORM, or NOISE
//   shaping - UNISON (1..7, detune, stereo spread), SUB (sine an octave down), DRIVE, FILTER (LP / BP / HP, a drawn
//             CUTOFF lane over the span, resonance, optional pitch tracking)
//   lanes   - drawn amplitude ENVELOPE and PAN over the span
//   time    - its own RATE (polyrhythm against the page) and REPEAT (the line loops at its own length)
//   FM      - another line can bend this one's phase (arc-to-arc FM)
// The page has a LENGTH (beats when tempo-synced, else seconds), a loop region, reverse, a drawn playhead SPEED lane, a
// quantise-to-key option, a grid, MIDI-in transpose and a few UI preferences. Everything here is plain data; the engine
// turns a Page into an immutable Snapshot (ArcsEngine.h). Message thread only.
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <atomic>
#include <cstring>
#include <vector>
#include <cmath>
#include <algorithm>

namespace arcs {

constexpr int kWaveLen = 256;            // points of a drawn single cycle
constexpr int kMaxArcs = 256;            // lines per page = voices the engine holds
constexpr int kMaxPts = 16384;           // points per line / lane (more is decimated on load)
constexpr int kMaxUnison = 7;
constexpr int kStateVersion = 2;         // pages from a newer build are refused (with a message), never half-read
constexpr double kMinBeats = 1.0, kMaxBeats = 4096.0;         // page length range in beats (1 beat .. ~30 min at 120)
constexpr double kMinSeconds = 0.25, kMaxSeconds = 1800.0;    // page length range in seconds when not synced
constexpr float kMinMidi = 12.0f, kMaxMidi = 132.0f;          // drawable pitch range (~16 Hz .. ~17 kHz)
constexpr float kMaxSpeedOct = 2.0f;                          // speed lane: 2^-2 .. 2^+2 (quarter .. four times)

struct Pt { float x = 0, y = 0; };

// every waveform edit gets a process-wide unique revision, so a wavetable cached by (line id, revision) can never be
// confused with a different drawing (undo/redo, load and copy all hand out fresh revisions)
inline juce::uint32 nextWaveRev() { static std::atomic<juce::uint32> r { 1 }; return ++r; }

inline float midiToHz(float m) { return 440.0f * std::exp2((m - 69.0f) / 12.0f); }
inline float hzToMidi(float hz) { return 69.0f + 12.0f * std::log2(juce::jmax(1.0e-6f, hz) / 440.0f); }
inline float finiteOr(float v, float def) { return std::isfinite(v) ? v : def; }

// scales (same order as Spectrum's key menu: Major, Minor, Harmonic minor, Dorian, Pentatonic minor, Pentatonic major, Chromatic)
inline const std::vector<int>& scaleSteps(int s) {
    static const std::vector<std::vector<int>> S { { 0, 2, 4, 5, 7, 9, 11 }, { 0, 2, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 8, 11 }, { 0, 2, 3, 5, 7, 9, 10 }, { 0, 3, 5, 7, 10 }, { 0, 2, 4, 7, 9 }, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } };
    return S[(size_t) juce::jlimit(0, 6, s)];
}
inline juce::StringArray scaleNames() { return { "Major", "Minor", "Harmonic minor", "Dorian", "Pentatonic minor", "Pentatonic major", "Chromatic" }; }
inline juce::StringArray keyNames() { return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; }
inline bool inScale(int midi, int root, int scale) { const int rel = ((midi - root) % 12 + 12) % 12; const auto& sc = scaleSteps(scale); return std::find(sc.begin(), sc.end(), rel) != sc.end(); }
inline float snapToKey(float m, int root, int scale) {   // nearest scale note to the drawn pitch (ties go down)
    if (!std::isfinite(m)) return 69.0f;
    const int n = (int) std::round(m); int best = n; float bd = 99.0f;
    for (int k = n - 6; k <= n + 6; ++k) if (inScale(k, root, scale) && std::abs((float) k - m) < bd - 1.0e-6f) { bd = std::abs((float) k - m); best = k; }
    return (float) best;
}

// ---------------------------------------------------------------- grid / rate menus
inline juce::StringArray gridNames() { return { "GRID AUTO", "1 BAR", "1/2", "1/4", "1/8", "1/16", "1/32", "1/4 T", "1/8 T", "1/16 T", "1/5", "1/7", "FREE" }; }
inline double gridBeats(int idx) {   // -1 = follow the zoom, 0 = free (no snap)
    static const double B[] = { -1, 4, 2, 1, 0.5, 0.25, 0.125, 2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0, 0.2, 1.0 / 7.0, 0 };
    return B[(size_t) juce::jlimit(0, 12, idx)];
}
inline juce::StringArray rateNames() { return { "x1/4", "x1/3", "x1/2", "x2/3", "x3/4", "x4/5", "x1", "x5/4", "x4/3", "x3/2", "x2", "x3", "x4" }; }
inline float rateValue(int idx) { static const float R[] = { 0.25f, 1.0f / 3, 0.5f, 2.0f / 3, 0.75f, 0.8f, 1, 1.25f, 4.0f / 3, 1.5f, 2, 3, 4 }; return R[(size_t) juce::jlimit(0, 12, idx)]; }
inline int rateIndex(float r) { int best = 6; float bd = 1e9f; for (int i = 0; i < 13; ++i) { const float d = std::abs(std::log(rateValue(i) / juce::jmax(1.0e-3f, r))); if (d < bd) { bd = d; best = i; } } return best; }

// ---------------------------------------------------------------- curves
// piecewise-linear curve lookup: pts sorted by x; flat beyond the ends; empty -> def
inline float curveAt(const std::vector<Pt>& pts, float x, float def) {
    if (pts.empty()) return def;
    if (x <= pts.front().x) return pts.front().y;
    if (x >= pts.back().x) return pts.back().y;
    auto it = std::lower_bound(pts.begin(), pts.end(), x, [](const Pt& p, float v) { return p.x < v; });
    const Pt& b = *it; const Pt& a = *(it - 1); const float w = b.x - a.x;
    return w <= 1.0e-9f ? b.y : a.y + (b.y - a.y) * (x - a.x) / w;
}
// drop points that are not finite (a damaged or hostile file must never reach the audio)
inline void dropNonFinite(std::vector<Pt>& pts) { pts.erase(std::remove_if(pts.begin(), pts.end(), [](const Pt& p) { return !std::isfinite(p.x) || !std::isfinite(p.y); }), pts.end()); }
// drop points that would make x go backwards (a line is a function of time) and merge duplicates
inline void makeMonotonic(std::vector<Pt>& pts) {
    std::vector<Pt> out; out.reserve(pts.size());
    for (const auto& p : pts) { if (!out.empty() && p.x <= out.back().x) { if (!(p.x < out.back().x)) { out.back().y = p.y; } continue; } out.push_back(p); }
    pts.swap(out);
}
inline void sortByX(std::vector<Pt>& pts) { std::stable_sort(pts.begin(), pts.end(), [](const Pt& a, const Pt& b) { return a.x < b.x; }); makeMonotonic(pts); }
inline void capPoints(std::vector<Pt>& pts, size_t maxN = (size_t) kMaxPts) {   // keep the ends, decimate evenly
    if (pts.size() <= maxN) { return; }
    std::vector<Pt> out; out.reserve(maxN); const double step = (double) (pts.size() - 1) / (double) (maxN - 1);
    for (size_t i = 0; i < maxN; ++i) out.push_back(pts[(size_t) std::round((double) i * step)]);
    pts.swap(out);
}
// clean a lane: finite, sorted, clamped to x 0..1 and y lo..hi, capped
inline void cleanLane(std::vector<Pt>& pts, float lo, float hi) {
    dropNonFinite(pts);
    for (auto& p : pts) { p.x = juce::jlimit(0.0f, 1.0f, p.x); p.y = juce::jlimit(lo, hi, p.y); }
    sortByX(pts); capPoints(pts);
}
// a lane cut to [u0, u1] of its span and stretched back to 0..1 (what a fragment of an erased line keeps)
inline std::vector<Pt> cropLane(const std::vector<Pt>& pts, float u0, float u1, float def) {
    if (pts.empty()) return {};
    std::vector<Pt> out { { 0.0f, curveAt(pts, u0, def) } }; const float w = juce::jmax(1.0e-6f, u1 - u0);
    for (auto& p : pts) if (p.x > u0 && p.x < u1) out.push_back({ (p.x - u0) / w, p.y });
    out.push_back({ 1.0f, curveAt(pts, u1, def) }); makeMonotonic(out); return out;
}
inline std::vector<Pt> reversedLane(const std::vector<Pt>& pts) { std::vector<Pt> out; for (auto it = pts.rbegin(); it != pts.rend(); ++it) out.push_back({ 1.0f - it->x, it->y }); makeMonotonic(out); return out; }

// ---------------------------------------------------------------- the waveform shapes you can start a drawing from
enum WaveShape { ShapeSine = 0, ShapeTriangle, ShapeSaw, ShapeSquare, ShapePulse, ShapeSoft };
inline juce::StringArray waveShapeNames() { return { "Sine", "Triangle", "Saw", "Square", "Pulse", "Soft" }; }
inline std::array<float, kWaveLen> waveShape(int shape) {
    std::array<float, kWaveLen> w {};
    for (int i = 0; i < kWaveLen; ++i) {
        const float ph = (float) i / kWaveLen;
        switch (shape) {
            case ShapeTriangle: w[(size_t) i] = ph < 0.25f ? ph * 4 : (ph < 0.75f ? 2 - ph * 4 : ph * 4 - 4); break;
            case ShapeSaw:      w[(size_t) i] = 2 * ph - 1; break;
            case ShapeSquare:   w[(size_t) i] = ph < 0.5f ? 1.0f : -1.0f; break;
            case ShapePulse:    w[(size_t) i] = ph < 0.2f ? 1.0f : -0.25f; break;
            case ShapeSoft:     { const float s = std::sin(juce::MathConstants<float>::twoPi * ph); w[(size_t) i] = s + 0.25f * std::sin(2 * juce::MathConstants<float>::twoPi * ph) + 0.1f * std::sin(3 * juce::MathConstants<float>::twoPi * ph); } break;
            default:            w[(size_t) i] = std::sin(juce::MathConstants<float>::twoPi * ph); break;
        }
    }
    return w;
}

// ---------------------------------------------------------------- a line
enum Source { SrcWave = 0, SrcNoise };
enum FilterMode { FiltOff = 0, FiltLP, FiltBP, FiltHP };
inline juce::StringArray filterNames() { return { "FILTER OFF", "LOW PASS", "BAND PASS", "HIGH PASS" }; }

struct Arc {
    int id = 0;                                   // stable: voices keep their phase across edits, FM sources point at ids
    std::vector<Pt> pts;                          // x 0..1 page position (strictly increasing), y MIDI pitch
    std::array<float, kWaveLen> wave = waveShape(ShapeSine);   // one hand-drawn cycle, -1..1
    std::vector<Pt> env;                          // amplitude over the span: x 0..1, y 0..1 (empty = flat 1)
    std::vector<Pt> pan;                          // pan over the span: x 0..1, y -1..1 (empty = centre)
    std::vector<Pt> cutoff;                       // filter cutoff over the span: x 0..1, y 0..1 (empty = 0.7)
    float gain = 0.8f;                            // 0..1
    int fmSource = -1;                            // id of the line that modulates this one's phase (-1 none)
    float fmDepth = 0;                            // 0..1
    bool mute = false;                            // silent (still a modulator)
    bool modOnly = false;                         // only heard through the lines it modulates
    int colour = 0;                               // palette index (UI)
    int source = SrcWave;                         // drawn cycle or noise
    int unison = 1; float detune = 0.3f, spread = 0.6f;          // 1..7 voices, detune 0..1 (= 0..50 cents), stereo spread 0..1
    float sub = 0;                                // sine one octave down, 0..1
    float drive = 0;                              // 0..1 (oversampled soft clip)
    int filter = FiltOff; float res = 0.2f; bool track = false;  // track: the cutoff lane is relative to the line's pitch
    float rate = 1.0f; bool repeat = false;       // the line's own clock against the page (polyrhythm); repeat = loop at its length
    juce::uint32 waveRev = nextWaveRev();         // changes whenever the waveform changes (the engine caches wavetables by it)

    float x0() const { return pts.empty() ? 0.0f : pts.front().x; }
    float x1() const { return pts.empty() ? 0.0f : pts.back().x; }
    float span() const { return x1() - x0(); }
    float pitchAt(float x) const { return curveAt(pts, x, 69.0f); }                   // MIDI at a page position
    float pitchAtU(float u) const { return pitchAt(x0() + u * span()); }           // u = 0..1 along the line
    float envAt(float u) const { return juce::jlimit(0.0f, 1.0f, curveAt(env, u, 1.0f)); }
    float panAt(float u) const { return juce::jlimit(-1.0f, 1.0f, curveAt(pan, u, 0.0f)); }
    float cutoffAt(float u) const { return juce::jlimit(0.0f, 1.0f, curveAt(cutoff, u, 0.7f)); }
    float minPitch() const { float m = 1.0e9f; for (auto& p : pts) m = juce::jmin(m, p.y); return pts.empty() ? 69.0f : m; }
    float maxPitch() const { float m = -1.0e9f; for (auto& p : pts) m = juce::jmax(m, p.y); return pts.empty() ? 69.0f : m; }
    bool valid() const { return pts.size() >= 2 && span() > 1.0e-6f; }
    void setWave(const std::array<float, kWaveLen>& w) { wave = w; waveRev = nextWaveRev(); }
    // where the line plays on the page with its rate / repeat: instance k covers [x0 + k * len, x0 + k * len + len)
    float playLen() const { return span() / juce::jmax(0.05f, rate); }
    void clampAll() {
        dropNonFinite(pts);
        for (auto& p : pts) { p.x = juce::jlimit(0.0f, 1.0f, p.x); p.y = juce::jlimit(kMinMidi, kMaxMidi, p.y); }
        makeMonotonic(pts); capPoints(pts);
        cleanLane(env, 0.0f, 1.0f); cleanLane(pan, -1.0f, 1.0f); cleanLane(cutoff, 0.0f, 1.0f);
        for (auto& v : wave) v = std::isfinite(v) ? juce::jlimit(-1.0f, 1.0f, v) : 0.0f;
        gain = juce::jlimit(0.0f, 1.0f, finiteOr(gain, 0.8f)); fmDepth = juce::jlimit(0.0f, 1.0f, finiteOr(fmDepth, 0.0f));
        source = juce::jlimit(0, 1, source); unison = juce::jlimit(1, kMaxUnison, unison);
        detune = juce::jlimit(0.0f, 1.0f, finiteOr(detune, 0.3f)); spread = juce::jlimit(0.0f, 1.0f, finiteOr(spread, 0.6f));
        sub = juce::jlimit(0.0f, 1.0f, finiteOr(sub, 0.0f)); drive = juce::jlimit(0.0f, 1.0f, finiteOr(drive, 0.0f));
        filter = juce::jlimit(0, 3, filter); res = juce::jlimit(0.0f, 1.0f, finiteOr(res, 0.2f));
        rate = juce::jlimit(0.25f, 4.0f, finiteOr(rate, 1.0f));
    }
};

// ---- one line <-> ValueTree (pages, clipboard)
inline juce::var ptsVar(const std::vector<Pt>& p) { juce::MemoryBlock m(p.size() * 2 * sizeof(float)); auto* f = (float*) m.getData(); for (size_t i = 0; i < p.size(); ++i) { f[2 * i] = p[i].x; f[2 * i + 1] = p[i].y; } return m; }
inline std::vector<Pt> ptsFrom(const juce::var& v) {
    std::vector<Pt> p; if (auto* m = v.getBinaryData()) { const size_t n = juce::jmin(m->getSize() / (2 * sizeof(float)), (size_t) 4 * kMaxPts); auto* f = (const float*) m->getData(); p.reserve(n);
        for (size_t i = 0; i < n; ++i) { const Pt q { f[2 * i], f[2 * i + 1] }; if (std::isfinite(q.x) && std::isfinite(q.y)) p.push_back(q); } }
    return p;
}
inline juce::ValueTree arcToTree(const Arc& a) {
    juce::ValueTree at("ARC"); at.setProperty("id", a.id, nullptr); at.setProperty("pts", ptsVar(a.pts), nullptr);
    juce::MemoryBlock w(a.wave.data(), sizeof(float) * kWaveLen); at.setProperty("wave", w, nullptr);
    at.setProperty("env", ptsVar(a.env), nullptr); at.setProperty("pan", ptsVar(a.pan), nullptr); at.setProperty("cutoff", ptsVar(a.cutoff), nullptr); at.setProperty("gain", a.gain, nullptr);
    at.setProperty("fmSource", a.fmSource, nullptr); at.setProperty("fmDepth", a.fmDepth, nullptr); at.setProperty("mute", a.mute, nullptr); at.setProperty("modOnly", a.modOnly, nullptr); at.setProperty("colour", a.colour, nullptr);
    at.setProperty("source", a.source, nullptr); at.setProperty("unison", a.unison, nullptr); at.setProperty("detune", a.detune, nullptr); at.setProperty("spread", a.spread, nullptr);
    at.setProperty("sub", a.sub, nullptr); at.setProperty("drive", a.drive, nullptr); at.setProperty("filter", a.filter, nullptr); at.setProperty("res", a.res, nullptr); at.setProperty("track", a.track, nullptr);
    at.setProperty("rate", a.rate, nullptr); at.setProperty("repeat", a.repeat, nullptr);
    return at;
}
inline Arc arcFromTree(const juce::ValueTree& at) {
    Arc a; a.id = at.getProperty("id", 0); a.pts = ptsFrom(at.getProperty("pts"));
    if (auto* m = at.getProperty("wave").getBinaryData()) if (m->getSize() == sizeof(float) * kWaveLen) { std::memcpy(a.wave.data(), m->getData(), m->getSize()); a.waveRev = nextWaveRev(); }
    a.env = ptsFrom(at.getProperty("env")); a.pan = ptsFrom(at.getProperty("pan")); a.cutoff = ptsFrom(at.getProperty("cutoff"));
    a.gain = (float) (double) at.getProperty("gain", 0.8); a.fmSource = at.getProperty("fmSource", -1); a.fmDepth = (float) (double) at.getProperty("fmDepth", 0.0);
    a.mute = at.getProperty("mute", false); a.modOnly = at.getProperty("modOnly", false); a.colour = at.getProperty("colour", 0);
    a.source = at.getProperty("source", 0); a.unison = at.getProperty("unison", 1); a.detune = (float) (double) at.getProperty("detune", 0.3); a.spread = (float) (double) at.getProperty("spread", 0.6);
    a.sub = (float) (double) at.getProperty("sub", 0.0); a.drive = (float) (double) at.getProperty("drive", 0.0); a.filter = at.getProperty("filter", 0); a.res = (float) (double) at.getProperty("res", 0.2); a.track = at.getProperty("track", false);
    a.rate = (float) (double) at.getProperty("rate", 1.0); a.repeat = at.getProperty("repeat", false);
    return a;
}

// ---------------------------------------------------------------- the page
struct LoadInfo { bool ok = true, futureVersion = false; int truncatedLines = 0, renumbered = 0, droppedLines = 0;
    juce::String message() const {
        if (futureVersion) return "This page was saved by a newer version of Ghosty Spectrum: update to open it";
        juce::StringArray s; if (truncatedLines) s.add(juce::String(truncatedLines) + " lines over the " + juce::String(kMaxArcs) + "-line limit were left out");
        if (renumbered) { s.add(juce::String(renumbered) + " duplicate line ids renumbered"); } if (droppedLines) s.add(juce::String(droppedLines) + " damaged lines skipped");
        return s.joinIntoString("; "); } };

struct Page {
    std::vector<Arc> arcs;
    double lengthBeats = 16.0;                    // when tempoSync
    double lengthSeconds = 8.0;                   // when not
    bool tempoSync = true;
    double ownBpm = 120.0;                        // used when the host never reported a tempo
    bool loopOn = false; float loopA = 0.0f, loopB = 1.0f;     // page positions
    bool reverse = false;
    bool speedOn = false; std::vector<Pt> speed;  // playhead speed lane: x 0..1 page position, y = log2(speed) in -2..2
    bool quantise = false; int root = 0, scale = 0;           // snap every line's pitch to the key (non-destructive)
    float master = 0.5f;                          // output gain
    bool autoGain = false;                        // level follows 1/sqrt(lines sounding) so a crowd does not pile into the limiter
    bool midiIn = false;                          // MIDI notes restart + transpose the page (held note plays it)
    int grid = 0;                                 // gridNames() index
    int ink = 1;                                  // freehand smoothing 0 raw / 1 soft / 2 smooth
    int jumpQuant = 2;                            // clicking the ruler jumps 0 now / 1 next beat / 2 next bar
    int bendRange = 1;                            // MIDI export bend range: 0 +-12 / 1 +-24 (MPE 48) / 2 +-48 (MPE 96)
    int nextId = 1;
    juce::String name { "Untitled page" };

    double seconds(double bpm) const { return tempoSync ? lengthBeats * 60.0 / juce::jmax(1.0, bpm) : lengthSeconds; }
    float speedAt(float x) const { return speedOn ? std::exp2(juce::jlimit(-kMaxSpeedOct, kMaxSpeedOct, curveAt(speed, x, 0.0f))) : 1.0f; }
    Arc* find(int id) { for (auto& a : arcs) if (a.id == id) return &a; return nullptr; }
    const Arc* find(int id) const { for (auto& a : arcs) if (a.id == id) return &a; return nullptr; }
    int indexOf(int id) const { for (size_t i = 0; i < arcs.size(); ++i) if (arcs[i].id == id) return (int) i; return -1; }
    int room() const { return juce::jmax(0, kMaxArcs - (int) arcs.size()); }
    // callers check room() first; add() past the limit is refused (returns nullptr) instead of dropping a line later
    Arc* add(Arc a) { if (room() <= 0) return nullptr; a.id = nextId++; a.clampAll(); arcs.push_back(std::move(a)); return &arcs.back(); }
    void remove(int id) { const int i = indexOf(id); if (i >= 0) arcs.erase(arcs.begin() + i); for (auto& a : arcs) if (a.fmSource == id) { a.fmSource = -1; } }
    void clampAll() {   // never drops lines (only fromTree truncates, and it says so)
        if (!std::isfinite(lengthBeats)) { lengthBeats = 16.0; } lengthBeats = juce::jlimit(kMinBeats, kMaxBeats, lengthBeats);
        if (!std::isfinite(lengthSeconds)) { lengthSeconds = 8.0; } lengthSeconds = juce::jlimit(kMinSeconds, kMaxSeconds, lengthSeconds);
        if (!std::isfinite(ownBpm)) { ownBpm = 120.0; } ownBpm = juce::jlimit(20.0, 999.0, ownBpm);
        loopA = juce::jlimit(0.0f, 1.0f, finiteOr(loopA, 0.0f)); loopB = juce::jlimit(0.0f, 1.0f, finiteOr(loopB, 1.0f));
        if (loopB < loopA) { std::swap(loopA, loopB); } if (loopB - loopA < 0.001f) { loopA = 0; loopB = 1; }
        cleanLane(speed, -kMaxSpeedOct, kMaxSpeedOct);
        root = juce::jlimit(0, 11, root); scale = juce::jlimit(0, 6, scale); master = juce::jlimit(0.0f, 1.0f, finiteOr(master, 0.5f));
        grid = juce::jlimit(0, 12, grid); ink = juce::jlimit(0, 2, ink); jumpQuant = juce::jlimit(0, 2, jumpQuant); bendRange = juce::jlimit(0, 2, bendRange);
        for (auto& a : arcs) a.clampAll();
        arcs.erase(std::remove_if(arcs.begin(), arcs.end(), [](const Arc& a) { return a.pts.empty(); }), arcs.end());
        for (auto& a : arcs) { if (a.fmSource >= 0 && (a.fmSource == a.id || !find(a.fmSource))) a.fmSource = -1; }
        for (auto& a : arcs) nextId = juce::jmax(nextId, a.id + 1);
    }

    // ---- state
    juce::ValueTree toTree() const {
        juce::ValueTree t("ARCSPAGE"); t.setProperty("version", kStateVersion, nullptr); t.setProperty("name", name, nullptr);
        t.setProperty("lengthBeats", lengthBeats, nullptr); t.setProperty("lengthSeconds", lengthSeconds, nullptr); t.setProperty("tempoSync", tempoSync, nullptr); t.setProperty("ownBpm", ownBpm, nullptr);
        t.setProperty("loopOn", loopOn, nullptr); t.setProperty("loopA", loopA, nullptr); t.setProperty("loopB", loopB, nullptr); t.setProperty("reverse", reverse, nullptr);
        t.setProperty("speedOn", speedOn, nullptr); t.setProperty("speed", ptsVar(speed), nullptr);
        t.setProperty("quantise", quantise, nullptr); t.setProperty("root", root, nullptr); t.setProperty("scale", scale, nullptr); t.setProperty("master", master, nullptr); t.setProperty("nextId", nextId, nullptr);
        t.setProperty("autoGain", autoGain, nullptr); t.setProperty("midiIn", midiIn, nullptr); t.setProperty("grid", grid, nullptr); t.setProperty("ink", ink, nullptr);
        t.setProperty("jumpQuant", jumpQuant, nullptr); t.setProperty("bendRange", bendRange, nullptr);
        for (const auto& a : arcs) t.appendChild(arcToTree(a), nullptr);
        return t;
    }
    static Page fromTree(const juce::ValueTree& t, LoadInfo* info = nullptr) {
        Page p; LoadInfo li;
        if (!t.hasType("ARCSPAGE")) { li.ok = false; if (info) *info = li; return p; }
        if ((int) t.getProperty("version", 1) > kStateVersion) { li.ok = false; li.futureVersion = true; if (info) *info = li; return p; }
        p.name = t.getProperty("name", p.name).toString().substring(0, 200);
        p.lengthBeats = t.getProperty("lengthBeats", p.lengthBeats); p.lengthSeconds = t.getProperty("lengthSeconds", p.lengthSeconds);
        p.tempoSync = t.getProperty("tempoSync", p.tempoSync); p.ownBpm = t.getProperty("ownBpm", p.ownBpm);
        p.loopOn = t.getProperty("loopOn", false); p.loopA = (float) (double) t.getProperty("loopA", 0.0); p.loopB = (float) (double) t.getProperty("loopB", 1.0); p.reverse = t.getProperty("reverse", false);
        p.speedOn = t.getProperty("speedOn", false); p.speed = ptsFrom(t.getProperty("speed"));
        p.quantise = t.getProperty("quantise", false); p.root = t.getProperty("root", 0); p.scale = t.getProperty("scale", 0); p.master = (float) (double) t.getProperty("master", 0.5); p.nextId = t.getProperty("nextId", 1);
        p.autoGain = t.getProperty("autoGain", false); p.midiIn = t.getProperty("midiIn", false); p.grid = t.getProperty("grid", 0); p.ink = t.getProperty("ink", 1);
        p.jumpQuant = t.getProperty("jumpQuant", 2); p.bendRange = t.getProperty("bendRange", 1);
        p.nextId = juce::jlimit(1, 1 << 28, p.nextId);
        for (const auto& at : t) {
            if (!at.hasType("ARC")) continue;
            if ((int) p.arcs.size() >= kMaxArcs) { ++li.truncatedLines; continue; }
            Arc a = arcFromTree(at);
            if (a.pts.size() < 2) { ++li.droppedLines; continue; }
            p.arcs.push_back(std::move(a));
        }
        // ids: positive and unique (FM links of a renumbered duplicate are dropped: which twin they meant is unknowable)
        std::vector<int> seen; int maxId = 0; for (auto& a : p.arcs) maxId = juce::jmax(maxId, a.id);
        p.nextId = juce::jmax(p.nextId, maxId + 1);
        for (auto& a : p.arcs) { if (a.id <= 0 || std::find(seen.begin(), seen.end(), a.id) != seen.end()) { a.id = p.nextId++; a.fmSource = -1; ++li.renumbered; } seen.push_back(a.id); }
        p.clampAll();
        if (info) *info = li;
        return p;
    }
};

// ---------------------------------------------------------------- stamp tools (work on a group of line ids)
struct Box { float x0 = 1, x1 = 0, y0 = 1.0e9f, y1 = -1.0e9f; bool empty() const { return x1 < x0; } float cx() const { return 0.5f * (x0 + x1); } float cy() const { return 0.5f * (y0 + y1); } };
inline Box boxOf(const Page& p, const std::vector<int>& ids) {
    Box b; for (int id : ids) if (auto* a = p.find(id)) for (auto& q : a->pts) { b.x0 = juce::jmin(b.x0, q.x); b.x1 = juce::jmax(b.x1, q.x); b.y0 = juce::jmin(b.y0, q.y); b.y1 = juce::jmax(b.y1, q.y); }
    return b;
}
// duplicates the group right after itself (or by dx), returns the new ids. Copies only what fits on the page:
// *skipped (if given) gets how many lines did not fit.
inline std::vector<int> copyArcs(Page& p, const std::vector<int>& ids, float dx = -1.0f, int* skipped = nullptr) {
    const Box b = boxOf(p, ids); if (skipped) { *skipped = 0; } if (b.empty()) return {};
    if (dx < 0) { dx = b.x1 - b.x0; } if (b.x1 + dx > 1.0f) { dx = 1.0f - b.x1; }    // never off the page; the copy may overlap when there is no room
    std::vector<int> out, src;
    for (int id : ids) if (auto* a = p.find(id)) {
        if (p.room() <= 0) { if (skipped) ++*skipped; continue; }
        Arc c = *a; for (auto& q : c.pts) q.x += dx; c.fmSource = -1; src.push_back(id); out.push_back(p.add(std::move(c))->id);
    }
    // copied FM links point at the copies
    for (size_t i = 0; i < src.size(); ++i) if (auto* s = p.find(src[i])) if (s->fmSource >= 0) { auto it = std::find(src.begin(), src.end(), s->fmSource); if (it != src.end()) if (auto* c = p.find(out[i])) c->fmSource = out[(size_t) (it - src.begin())]; }
    return out;
}
inline void mirrorArcs(Page& p, const std::vector<int>& ids) {      // play the group backwards in time (lanes too)
    const Box b = boxOf(p, ids); if (b.empty()) return;
    for (int id : ids) if (auto* a = p.find(id)) {
        for (auto& q : a->pts) { q.x = b.x0 + b.x1 - q.x; } std::reverse(a->pts.begin(), a->pts.end()); makeMonotonic(a->pts);
        a->env = reversedLane(a->env); a->pan = reversedLane(a->pan); a->cutoff = reversedLane(a->cutoff);
    }
}
inline void invertArcs(Page& p, const std::vector<int>& ids) {      // flip pitch inside the group's box
    const Box b = boxOf(p, ids); if (b.empty()) return;
    for (int id : ids) if (auto* a = p.find(id)) for (auto& q : a->pts) q.y = b.y0 + b.y1 - q.y;
}
// largest time factor that keeps the group on the page
inline float maxStretch(const Page& p, const std::vector<int>& ids) { const Box b = boxOf(p, ids); return b.empty() || b.x1 - b.x0 < 1.0e-6f ? 1.0f : (1.0f - b.x0) / (b.x1 - b.x0); }
inline void stretchArcs(Page& p, const std::vector<int>& ids, float timeFactor, float pitchFactor) {   // about the box's left edge / centre pitch
    const Box b = boxOf(p, ids); if (b.empty()) return;
    timeFactor = juce::jlimit(0.01f, juce::jmax(0.01f, maxStretch(p, ids)), timeFactor);
    for (int id : ids) if (auto* a = p.find(id)) { for (auto& q : a->pts) { q.x = juce::jlimit(0.0f, 1.0f, b.x0 + (q.x - b.x0) * timeFactor); q.y = juce::jlimit(kMinMidi, kMaxMidi, b.cy() + (q.y - b.cy()) * pitchFactor); } makeMonotonic(a->pts); }
}
// rotate the group about its centre (degrees, positive = anticlockwise on screen). Screen space: x in page units, y in
// octaves scaled by `aspect` page-widths per octave so the shape turns like it does on the canvas. Points keep order; the
// result is re-made monotonic in time (a line can't go back in time), so a near-vertical result becomes a steep slide.
inline void rotateArcs(Page& p, const std::vector<int>& ids, float degrees, float aspect = 0.1f) {
    const Box b = boxOf(p, ids); if (b.empty()) return;
    const float rad = degrees * juce::MathConstants<float>::pi / 180.0f, c = std::cos(rad), s = std::sin(rad);
    for (int id : ids) if (auto* a = p.find(id)) {
        for (auto& q : a->pts) { const float dx = q.x - b.cx(), dy = (q.y - b.cy()) / 12.0f * aspect; const float nx = dx * c - dy * s, ny = dx * s + dy * c; q.x = juce::jlimit(0.0f, 1.0f, b.cx() + nx); q.y = juce::jlimit(kMinMidi, kMaxMidi, b.cy() + ny / aspect * 12.0f); }
        if (a->pts.size() >= 2 && a->pts.back().x < a->pts.front().x) std::reverse(a->pts.begin(), a->pts.end());
        makeMonotonic(a->pts);
    }
}
inline void moveArcs(Page& p, const std::vector<int>& ids, float dx, float dy) {
    const Box b = boxOf(p, ids); if (b.empty()) return;
    dx = juce::jlimit(-b.x0, 1.0f - b.x1, dx); dy = juce::jlimit(kMinMidi - b.y0, kMaxMidi - b.y1, dy);
    for (int id : ids) if (auto* a = p.find(id)) for (auto& q : a->pts) { q.x += dx; q.y += dy; }
}
inline void quantiseArcs(Page& p, const std::vector<int>& ids, int root, int scale) {   // destructive snap of the drawn points
    for (int id : ids) if (auto* a = p.find(id)) for (auto& q : a->pts) q.y = snapToKey(q.y, root, scale);
}
// controlled chaos: jitter pitch (smoothly), timing, pan and the cycle by `amount` 0..1 with a seed (undoable by the caller)
inline void randomiseArcs(Page& p, const std::vector<int>& ids, float amount, juce::int64 seed) {
    juce::Random r(seed); amount = juce::jlimit(0.0f, 1.0f, amount);
    for (int id : ids) if (auto* a = p.find(id)) {
        const float shift = (r.nextFloat() * 2 - 1) * amount * 2.0f;
        for (auto& q : a->pts) q.y = juce::jlimit(kMinMidi, kMaxMidi, q.y + shift + (r.nextFloat() * 2 - 1) * amount * 1.5f);
        const float dx = (r.nextFloat() * 2 - 1) * amount * 0.01f; if (a->x0() + dx >= 0 && a->x1() + dx <= 1) for (auto& q : a->pts) q.x += dx;
        const float pn = (r.nextFloat() * 2 - 1) * amount; a->pan = { { 0, juce::jlimit(-1.0f, 1.0f, curveAt(a->pan, 0, 0) + pn) }, { 1, juce::jlimit(-1.0f, 1.0f, curveAt(a->pan, 1, 0) - pn) } };
        auto w = a->wave; for (auto& v : w) v = juce::jlimit(-1.0f, 1.0f, v + (r.nextFloat() * 2 - 1) * amount * 0.15f); a->setWave(w);
        a->gain = juce::jlimit(0.05f, 1.0f, a->gain * (1.0f + (r.nextFloat() * 2 - 1) * amount * 0.4f));
    }
}
// erase: remove what is inside an ellipse (x in page units, y in MIDI, radii in each unit). The line is cut exactly where it
// enters and leaves the brush, so a line erased in the middle becomes two (each fragment keeps its own part of the envelope /
// pan / cutoff). A split that would need more room than the page has is refused: that line stays whole and *refused is set.
inline bool eraseAt(Page& p, float x, float y, float rx, float ry, bool* refused = nullptr) {
    bool changed = false; std::vector<Arc> add; if (refused) { *refused = false; }
    auto in = [&](const Pt& q) { const float dx = (q.x - x) / rx, dy = (q.y - y) / ry; return dx * dx + dy * dy <= 1.0f; };
    for (size_t i = 0; i < p.arcs.size();) {
        Arc& a = p.arcs[i]; std::vector<std::vector<Pt>> runs(1); bool hit = false;
        if (!a.pts.empty() && !in(a.pts[0])) runs.back().push_back(a.pts[0]); else if (!a.pts.empty()) hit = true;
        for (size_t k = 0; k + 1 < a.pts.size(); ++k) {
            const Pt& s0 = a.pts[k]; const Pt& s1 = a.pts[k + 1]; bool prevIn = in(s0); constexpr int N = 32;
            for (int j = 1; j <= N; ++j) {
                const float t = (float) j / N; const Pt q { s0.x + (s1.x - s0.x) * t, s0.y + (s1.y - s0.y) * t }; const bool qi = in(q);
                if (qi && !prevIn) { const float tp = (float) (j - 1) / N; runs.back().push_back({ s0.x + (s1.x - s0.x) * tp, s0.y + (s1.y - s0.y) * tp }); runs.emplace_back(); hit = true; }
                else if (!qi && prevIn) { runs.back().push_back(q); hit = true; }
                prevIn = qi;
            }
            if (!in(s1)) { if (runs.back().empty() || runs.back().back().x < s1.x) runs.back().push_back(s1); }
        }
        if (!hit) { ++i; continue; }
        for (auto& r : runs) makeMonotonic(r);
        runs.erase(std::remove_if(runs.begin(), runs.end(), [](const std::vector<Pt>& r) { return r.size() < 2 || r.back().x - r.front().x < 1.0e-6f; }), runs.end());
        if (runs.empty()) { p.remove(a.id); changed = true; continue; }
        if ((int) runs.size() - 1 > p.room() - (int) add.size()) { if (refused) { *refused = true; } ++i; continue; }
        const float ax0 = a.x0(), aw = juce::jmax(1.0e-6f, a.span());
        auto fragment = [&](const std::vector<Pt>& r) { Arc c = a; const float u0 = (r.front().x - ax0) / aw, u1 = (r.back().x - ax0) / aw; c.pts = r;
            c.env = cropLane(a.env, u0, u1, 1.0f); c.pan = cropLane(a.pan, u0, u1, 0.0f); c.cutoff = cropLane(a.cutoff, u0, u1, 0.7f); return c; };
        changed = true;
        for (size_t k = 1; k < runs.size(); ++k) { Arc c = fragment(runs[k]); c.fmSource = -1; add.push_back(std::move(c)); }
        Arc first = fragment(runs[0]); a.pts = first.pts; a.env = first.env; a.pan = first.pan; a.cutoff = first.cutoff;
        ++i;
    }
    for (auto& c : add) p.add(std::move(c));
    return changed;
}
// freehand ink: smooth the pitch of a stroke (amount 0 raw .. 1 smooth), ends kept
inline void smoothStroke(std::vector<Pt>& pts, float amount) {
    if (pts.size() < 5 || amount <= 0) return;
    const int r = juce::jlimit(1, 8, (int) std::round(amount * 6)); auto src = pts;
    for (size_t i = 1; i + 1 < pts.size(); ++i) { float s = 0, w = 0;
        for (int k = -r; k <= r; ++k) { const int j = (int) i + k; if (j < 0 || j >= (int) src.size()) continue; const float g = (float) (r + 1 - std::abs(k)); s += src[(size_t) j].y * g; w += g; }
        pts[i].y = s / w; }
}
// simplify a freshly drawn stroke: drop points closer than minDx in time unless the pitch turns
inline void simplifyStroke(std::vector<Pt>& pts, float minDx, float minDy) {
    if (pts.size() < 3) { return; } std::vector<Pt> out { pts.front() };
    for (size_t i = 1; i + 1 < pts.size(); ++i) { const Pt& l = out.back(); if (std::abs(pts[i].x - l.x) >= minDx || std::abs(pts[i].y - l.y) >= minDy) out.push_back(pts[i]); }
    out.push_back(pts.back()); pts.swap(out);
}
// a shaped line from A to B (risers): 0 exponential (slow then fast), 1 S-curve, 2 logarithmic (fast then slow)
inline juce::StringArray curveShapeNames() { return { "EXP", "S", "LOG" }; }
inline std::vector<Pt> makeCurve(Pt a, Pt b, int shape, int n = 48) {
    std::vector<Pt> out; if (b.x < a.x) std::swap(a, b);
    for (int k = 0; k <= n; ++k) { const float t = (float) k / n; float s;
        switch (shape) { case 1: s = t * t * (3 - 2 * t); break; case 2: s = 1.0f - (std::exp2(5.0f * (1.0f - t)) - 1.0f) / 31.0f; break; default: s = (std::exp2(5.0f * t) - 1.0f) / 31.0f; break; }
        out.push_back({ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * s }); }
    makeMonotonic(out); return out;
}

// ---------------------------------------------------------------- clipboard: lines as XML (FM links inside the group kept)
inline juce::String linesToText(const Page& p, const std::vector<int>& ids) {
    juce::ValueTree t("ARCLINES"); const Box b = boxOf(p, ids); t.setProperty("x0", b.x0, nullptr);
    for (int id : ids) if (auto* a = p.find(id)) t.appendChild(arcToTree(*a), nullptr);
    return t.toXmlString();
}
// pastes so the group starts at page position `atX`; copies only what fits (*skipped gets the rest); returns the new ids
inline std::vector<int> pasteLines(Page& p, const juce::String& text, float atX, int* skipped = nullptr) {
    if (skipped) { *skipped = 0; }
    auto xml = juce::parseXML(text); if (!xml) return {};
    const auto t = juce::ValueTree::fromXml(*xml); if (!t.hasType("ARCLINES")) return {};
    const float x0 = finiteOr((float) (double) t.getProperty("x0", 0.0), 0.0f);
    std::vector<Arc> src; for (const auto& c : t) if (c.hasType("ARC")) { Arc a = arcFromTree(c); a.clampAll(); if (a.valid()) src.push_back(std::move(a)); }
    if (src.empty()) return {};
    float hi = 0; for (auto& a : src) hi = juce::jmax(hi, a.x1());
    float dx = juce::jlimit(0.0f, 1.0f, atX) - x0; if (hi + dx > 1.0f) { dx = 1.0f - hi; } if (x0 + dx < 0) { dx = -x0; }
    std::vector<int> out, oldIds;
    for (auto& a : src) { if (p.room() <= 0) { if (skipped) ++*skipped; continue; } const int old = a.id; for (auto& q : a.pts) q.x = juce::jlimit(0.0f, 1.0f, q.x + dx);
        const int keepFm = a.fmSource; a.fmSource = -1; auto* n = p.add(std::move(a)); n->fmSource = keepFm; out.push_back(n->id); oldIds.push_back(old); }
    for (int id : out) if (auto* a = p.find(id)) { auto it = std::find(oldIds.begin(), oldIds.end(), a->fmSource); a->fmSource = it != oldIds.end() ? out[(size_t) (it - oldIds.begin())] : -1; }
    return out;
}

} // namespace arcs
