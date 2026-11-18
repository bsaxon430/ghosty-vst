#pragma once
// ARCS page engine: turns a Page into an immutable Snapshot (message thread) and plays it (audio thread).
//  - wavetable voices: the drawn cycle (a closed polyline) is resampled to 4096 points, its spectrum taken with an FFT and
//    10 mip levels rebuilt (512 >> L harmonics); a voice reads the level whose top harmonic stays under the limit,
//    crossfaded into the next one so glides never step (Hermite reads). NOISE lines use white noise instead.
//  - per line: unison (1..7, detune, spread), sub, drive, state-variable filter (LP / BP / HP, drawn cutoff lane, pitch
//    tracking), drawn envelope and pan, its own rate / repeat
//  - control rate: targets, smoothing and the mip choice run every kCtl samples; inside a sub-block the phase increment,
//    gains, level mix, FM depth and filter coefficients ramp linearly per sample (nothing steps, no zipper)
//  - pitch is read straight from the drawn polyline (no table), so detail never depends on the page length
//  - arc-to-arc FM: lines that take part in FM (carriers and their sources) and driven lines run in an OVERSAMPLED group
//    (4x at <= 50 kHz, 2x at <= 100 kHz) and come back through a Kaiser-windowed FIR decimator; the carrier's mip level is
//    chosen from its peak instantaneous frequency (pitch + FM deviation), so high-index FM does not fold back.
//    The plain path is delayed to match: the engine has a constant latency of kLatency samples.
//  - transport: host-synced (ppq; free-time pages follow the host's seconds, so tempo changes never jump), count-in
//    silent, loop region, reverse, quantised jump (now / next beat / next bar), drawn playhead speed lane, MIDI-in
//    (a held note restarts the page transposed; notes 0..7 pick page slots A..H)
//  - real-time safe: no allocation or locks in process(); snapshots are handed over through atomic pointers, retired ones
//    are kept for a grace second on the message thread (so a deleted line can fade out on its own wavetable) and then freed
//    there, never by the audio thread
#include "ArcsModel.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <memory>
#include <map>
#include <functional>

namespace arcs {

constexpr int kTableLen = 4096, kMipLevels = 10, kTopHarmonic = 512;   // level L keeps 512 >> L harmonics (8x oversampled tables)
constexpr int kEnvRes = 1024;
constexpr int kSlots = 8;                                            // page bank A..H

struct Wavetable {
    std::array<std::array<float, kTableLen + 4>, kMipLevels> t {};   // 4 guard points for the Hermite read
    std::array<float, kMipLevels> slope {};                          // max |d y / d phase| (per cycle) of each level: FM bandwidth estimate
    static int harmonics(int level) { return kTopHarmonic >> level; }
};
std::shared_ptr<const Wavetable> buildWavetable(const std::array<float, kWaveLen>& wave);

// one line, ready to play; immutable and shared between snapshots while the line does not change
struct SnapArc {
    int id = 0; float x0 = 0, x1 = 1, span = 1;
    std::vector<Pt> pts;                   // the drawn polyline (x page, y MIDI)
    std::vector<float> env, pan, cut;      // kEnvRes + 1 points along the span
    float gain = 0.8f, fmDepth = 0; int fmSourceId = -1; bool audible = true;
    int source = SrcWave, unison = 1; float detune = 0, spread = 0, sub = 0, drive = 0; int filter = FiltOff; float res = 0; bool track = false;
    float rate = 1; bool repeat = false;
    std::shared_ptr<const Wavetable> wt;
    juce::uint64 key = 0;
    float pitchAt(float x, int& seg) const;   // MIDI at page position x inside the span (seg = cursor hint, kept by the voice)
    float pitchAt(float x) const { int s = 0; return pitchAt(x, s); }
    // the page position inside the line for a page position p (rate / repeat); false when the line is silent there
    bool local(float p, float& x) const {
        float d = (p - x0) * rate; if (!(d >= 0)) return false;
        if (repeat) d = std::fmod(d, span); else if (d >= span) return false;
        x = x0 + d; return true;
    }
};
using SnapArcPtr = std::shared_ptr<const SnapArc>;

struct Snapshot {
    std::vector<SnapArcPtr> arcs; std::vector<int> fmIdx; std::vector<float> durUnits; std::vector<char> os;
    bool anyOs = false;
    double lengthBeats = 16, lengthSeconds = 8; bool tempoSync = true; double ownBpm = 120;
    bool loopOn = false; float loopA = 0, loopB = 1; bool reverse = false; float master = 0.5f; bool autoGain = false, midiIn = false;
    bool quantise = false; std::array<char, 12> keyMask {};
    float snap(float m) const { const int n = (int) std::round(m); int best = n; float bd = 99.0f;
        for (int k = n - 6; k <= n + 6; ++k) { if (keyMask[(size_t) (((k % 12) + 12) % 12)] && std::abs((float) k - m) < bd - 1.0e-6f) { bd = std::abs((float) k - m); best = k; } } return (float) best; }
    // the speed lane as a warp: timeAt[i] = page time (in nominal page lengths) when the playhead reaches position i / kWarp;
    // posAt[j] is the inverse (position at time j / kWarp * total). Flat lane: both identity, total = 1.
    static constexpr int kWarp = 4096;
    std::vector<double> timeAt, posAt; double total = 1.0;   // empty = no speed lane (exact identity)
    double seconds(double bpm) const { return tempoSync ? lengthBeats * 60.0 / juce::jmax(1.0, bpm) : lengthSeconds; }
    float timeAtPos(float p) const;        // p 0..1 -> time units 0..total
    float posAtTime(float u) const;        // time units 0..total -> p 0..1
    double durationSeconds(double bpm) const { return seconds(bpm) * total; }   // one pass of the whole page
    float heardMidi(const SnapArc& a, float x, int& seg) const { float m = a.pitchAt(x, seg); return quantise ? snap(m) : m; }
};

// wavetables are shared by content (lines from the same preset or copies share one table); built lines are reused while
// they do not change, so a live edit of one line rebuilds one line
class SnapCache {
public:
    std::shared_ptr<const Wavetable> table(const Arc& a);
    void prune(const Page& p);             // drop what no line on the page uses
    size_t tables() const { return tableMap.size(); }
    static juce::uint64 hashOf(const std::array<float, kWaveLen>& w);
    std::map<juce::uint64, std::shared_ptr<const Wavetable>> tableMap;
    std::map<int, std::pair<juce::uint32, juce::uint64>> keyOf;      // line id -> (waveRev, content hash)
    std::map<int, SnapArcPtr> built;                                 // line id -> last built line
};
// builds the snapshot; *ms gets the build time, *rebuilt how many lines had to be rebuilt
std::unique_ptr<Snapshot> buildSnapshot(const Page& p, SnapCache& cache, double* ms = nullptr, int* rebuilt = nullptr);

// single-producer (message thread) / single-consumer (audio thread) hand-off; the consumer never frees anything.
// Retired snapshots wait a grace period on the message thread before they are freed (a fading voice may still read them).
template <typename T> class Exchange {
public:
    ~Exchange() { delete pending.load(); delete current; delete retired.load(); for (auto& g : grave) delete g.first; }
    void publish(std::unique_ptr<T> p) { collect(); delete pending.exchange(p.release()); }        // message thread
    T* get() { if (retired.load(std::memory_order_acquire) == nullptr) if (T* p = pending.exchange(nullptr)) { retired.store(current, std::memory_order_release); current = p; } return current; }   // audio thread
    void collect(double nowMs = juce::Time::getMillisecondCounterHiRes(), double graceMs = 1000.0) {   // message thread
        if (T* r = retired.exchange(nullptr)) grave.push_back({ r, nowMs });
        for (size_t i = 0; i < grave.size();) { if (nowMs - grave[i].second >= graceMs) { delete grave[i].first; grave.erase(grave.begin() + (long) i); } else ++i; }
    }
    T* peekCurrent() const { return current; }   // tests (not while the audio thread runs)
    size_t graveSize() const { return grave.size(); }
private:
    std::atomic<T*> pending { nullptr }, retired { nullptr }; T* current = nullptr;
    std::vector<std::pair<T*, double>> grave;
};

// what the host says. timeSec: the host's position in seconds when it reports one (NaN = unknown)
struct HostInfo { double bpm = 120; double ppq = 0; bool playing = false; double timeSec = std::numeric_limits<double>::quiet_NaN(); };

class Engine {
public:
    Engine();
    void prepare(double sampleRate, int maxBlock);
    // adds (or replaces) the page's sound in L / R; midi (optional) drives MIDI-in when the page has it on
    void process(float* L, float* R, int n, const HostInfo& host, bool add = true, const juce::MidiBuffer* midi = nullptr);
    static constexpr int kLatency = 36;        // samples (the oversampled group's decimator; the plain path is delayed to match)
    // ---- message thread
    void publish(std::unique_ptr<Snapshot> s, int toSlot = 0) { ex[(size_t) juce::jlimit(0, kSlots - 1, toSlot)].publish(std::move(s)); }
    void collect() { const double now = juce::Time::getMillisecondCounterHiRes(); for (auto& e : ex) e.collect(now); }
    std::atomic<bool> followHost { true };     // the page runs with the host transport when the host plays
    std::atomic<bool> internalPlay { false };  // the page's own PLAY (cleared when a followed host stops)
    std::atomic<float> jumpTo { -1.0f };       // set a page position 0..1 to jump there
    std::atomic<float> jumpQuantBeats { 0.0f };// 0 = now, else the jump waits for the next multiple of this many beats
    std::atomic<bool> stopRequest { false };   // internal transport back to the start (loop start)
    std::atomic<int> slotRequest { 0 };        // page bank slot to play
    // ---- status
    std::atomic<int> playingSlot { 0 };
    std::atomic<float> playPos { -1.0f };      // page position 0..1 of the playhead (-1 when silent)
    std::atomic<float> meterL { 0 }, meterR { 0 };
    std::atomic<bool> kneeActive { false };    // the output soft-limiter worked in the last blocks
    std::atomic<int> activeVoices { 0 };
    std::atomic<bool> isPlaying { false };
    std::atomic<bool> hostRunning { false };   // a followed host is playing (the page's own PLAY does nothing then)
    std::atomic<int> midiNote { -1 };          // held MIDI-in note (-1 none)
    std::atomic<bool> pendingJumpStatus { false };
    double sampleRate() const { return sr; }
    int oversampling() const { return os; }
    // offline: one pass of the whole page (loop ignored, reverse + speed lane honoured), streamed block by block into sink
    // (return false from sink to stop). Latency is removed. Returns false when aborted.
    static bool render(const Snapshot& s, double sampleRate, double bpm, const std::function<bool(const float*, const float*, int)>& sink,
                       std::atomic<int>* progress = nullptr, const std::atomic<bool>* abort = nullptr);
    static void render(const Snapshot& s, double sampleRate, double bpm, std::vector<float>& L, std::vector<float>& R);   // tests / short pages
    static double renderSeconds(const Snapshot& s, double bpm) { return s.durationSeconds(bpm > 1 ? bpm : s.ownBpm) + 0.1; }
private:
    struct Voice {
        int id = -1; int arc = -1; const SnapArc* sa = nullptr; bool os = false; bool fresh = true, wasOn = false; float lastX = 0; int seg = 0;
        std::array<double, kMaxUnison> ph {}; double subPh = 0; juce::uint32 rng = 0x9e3779b9u;
        float hzS = 0, gainS = 0, panS = 0, cutS = 0, fmS = 0, lastOut = 0;
        float inc = 0, dInc = 0, gm = 0, dGm = 0, gl = 0, dGl = 0, gr = 0, dGr = 0, mix = 0, dMix = 0, fm = 0, dFm = 0;
        float a1 = 0, a2 = 0, a3 = 0, da1 = 0, da2 = 0, da3 = 0, fk = 2; float s1L = 0, s2L = 0, s1R = 0, s2R = 0;
        int L0 = 0, L1 = 0; std::array<float, kMaxUnison> uL {}, uR {}, ratio {};
    };
    void remap(const Snapshot* s, bool slotChanged);
    void voiceSample(Voice& v, const SnapArc& a, float fmOff, float& l, float& r);
    std::array<Exchange<Snapshot>, kSlots> ex; const Snapshot* cur = nullptr; int slot = 0;
    std::array<Voice, kMaxArcs> voices; std::array<int, kMaxArcs> slotOf {};   // slotOf[arc index] -> voice
    bool oneShot = false;                      // offline render: the smoothing lookahead never wraps past the page end
    double sr = 48000; int os = 4; double u = 0;   // transport position in page-time units (0..total)
    double jumpOffset = 0; bool wasHostDriven = false, wasHostPlaying = false, jumped = true;
    double lastPpq = 0, lastBpm = 120; int lastN = 0; double lastCycle = 0;
    float pendingJump = -1, pendingQ = 0;
    std::array<float, 17> kH {}, kG {}, kP {};                 // one-pole coefficients per sub-block length (pitch / gain / pan+cutoff)
    float peakL = 0, peakR = 0, autoG = 1; int kneeHold = 0;
    // MIDI-in
    std::array<int, 16> held {}; int nHeld = 0; float transpose = 0; bool midiGate = false;
    // oversampled group + plain-path delay
    std::vector<float> fir; int firN = 1;
    std::vector<float> osL, osR;               // decimator input: firN - 1 history + kCtl * os new samples
    std::array<std::vector<float>, 2> dly; int dlyPos = 0;
    int osQuiet = 1 << 30;
    static constexpr int kCtl = 16;
};

} // namespace arcs
