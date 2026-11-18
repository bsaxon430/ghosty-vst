#pragma once
// GHOST MOTION timeline (Ghost Box only; part of the MOTION tab): ONE loop of 1 .. 8 bars for the whole rack. Every lane drives one
// rack setting (a continuous parameter the user adds) with a drawn CURVE or STEPS base plus EVENTS (bumps placed on the loop by drag,
// or by an AUDIO-AWARE rule on the hits found in the input). Original code (no third-party sources).
//
// Real-time rule: the audio thread only reads immutable data (BoxMod's ModConfig carries the lanes, events and step values; the
// curves are lock-free SharedShapes) and the onset FEED writes into a ring allocated at prepare(). The ANALYSIS (onset detection,
// rules) runs off the audio thread on a copy of the ring.
#include "FxCore.h"
#include "BendLane.h"

#if GPFX_BOX
namespace gpfx {

constexpr int kTlLanes = 12;           // lanes in one timeline
constexpr int kTlEvents = 64;          // events per lane
constexpr int kTlMaxSteps = 64;
inline const juce::StringArray& tlBarsNames() { static juce::StringArray s { "1 BAR", "2 BARS", "3 BARS", "4 BARS", "5 BARS", "6 BARS", "7 BARS", "8 BARS" }; return s; }
inline const juce::StringArray& tlRuleNames() { static juce::StringArray s { "EVERY 2ND HIT", "LAST HIT OF BAR", "QUIET HITS", "LOUD HITS", "EVERY HIT" }; return s; }
enum TlRule { trEvery2nd, trLastOfBar, trQuiet, trLoud, trEvery };
inline const juce::StringArray& tlEventLenNames() { static juce::StringArray s { "1/32", "1/16", "1/8", "1/4", "1/2" }; return s; }
inline double tlEventLenBeats (int i) noexcept { static const double b[] { 0.125, 0.25, 0.5, 1.0, 2.0 }; return b[juce::jlimit (0, 4, i)]; }
inline const juce::StringArray& tlStepsNames() { static juce::StringArray s { "8", "16", "32", "64" }; return s; }
inline int tlStepCount (int i) noexcept { return 8 << juce::jlimit (0, 3, i); }

// an event: a bump of the lane's setting at a place in the loop (beats from the loop start), AMOUNT -1 .. 1 of the lane's depth,
// with short raised-cosine edges (kTlEdgeMs) so it never steps
struct TlEvent { double pos = 0, len = 0.25; float amt = 1.0f; bool autoPlaced = false; };
constexpr double kTlEdgeMs = 4.0;

struct TlLane
{
    juce::String id;                   // the rack parameter (fx_...)
    bool on = true;
    float depth = 0.5f;                // -1 .. 1: how far the lane moves the setting (normalised range)
    int mode = 0;                      // base: 0 CURVE (shape), 1 STEPS
    int stepsIdx = 1;                  // 8 / 16 / 32 / 64 steps over the loop
    std::array<float, kTlMaxSteps> steps {};
    bendlane::Shape shape;             // the drawn base (also lives in BoxMod::tlShapes for the editor; this copy is for state)
    std::vector<TlEvent> events;
};

struct TimelineDef
{
    int bars = 1;                      // 1 .. 8 (the loop)
    int rule = trEvery2nd, eventLen = 1;
    float eventAmt = 1.0f;
    std::vector<TlLane> lanes;
    bool isDefault() const { return lanes.empty() && bars == 1 && rule == trEvery2nd && eventLen == 1 && std::abs (eventAmt - 1.0f) < 1.0e-6f; }
};

// the lane value (before depth) at a place in the loop: base (curve / steps) + events, clamped to -1 .. 1.
// beat = beats from the loop start (0 .. loopBeats), edge = the event edge length in beats.
inline float tlEventsAt (const TlEvent* ev, int n, double beat, double loopBeats, double edge) noexcept
{
    float v = 0.0f;
    for (int k = 0; k < n; ++k)
    {
        const auto& e = ev[k];
        double x = beat - e.pos;
        if (x < 0) x += loopBeats;                               // an event that runs over the loop end wraps to its start
        if (x < 0 || x >= e.len) continue;
        const double ed = std::min (edge, e.len * 0.5);
        float w = 1.0f;
        if (ed > 0 && x < ed) w = 0.5f - 0.5f * std::cos (kPi * (float) (x / ed));
        else if (ed > 0 && x > e.len - ed) w = 0.5f - 0.5f * std::cos (kPi * (float) ((e.len - x) / ed));
        v += e.amt * w;
    }
    return v;
}
inline float tlStepsAt (const float* st, int count, double ph) noexcept
{
    const int n = juce::jlimit (1, kTlMaxSteps, count);
    return st[juce::jlimit (0, n - 1, (int) (ph * n))];
}

// ---------------------------------------------------------------- AUDIO-AWARE: the onset feed + analysis
// The feed (audio thread) sums the input to mono, splits it into 4 bands (one-pole crossovers at 150 Hz, 1 kHz, 5 kHz), follows each
// band's energy twice (fast: 0.3 ms attack, 40 / 25 / 15 / 15 ms release; smoothed: 8 ms) and writes one record per hop (~1.33 ms: sr / 750) with the song position,
// the 4 band energies and the peak: a 65536-record ring (87 s).
struct OnsetRec { double ppq = 0; float e[4] {}; float f[4] {}; float peak = 0; };   // e: smoothed (8 ms) band energy, f: fast follower
class OnsetFeed
{
public:
    static constexpr int kRing = 65536;
    void prepare (double sampleRate)
    {
        sr = sampleRate; hop = std::max (16, (int) std::lround (sr / 750.0));
        if ((int) ring.size() != kRing) ring.assign ((size_t) kRing, OnsetRec());
        lp1.setLP (sr, 150.0); lp2.setLP (sr, 1000.0); lp3.setLP (sr, 5000.0);
        att = coef (sr, 0.3); smA = coef (sr, 8.0); rel = { coef (sr, 40.0), coef (sr, 25.0), coef (sr, 15.0), coef (sr, 15.0) };
        reset();
    }
    void reset() { lp1.reset(); lp2.reset(); lp3.reset(); env = {}; sm = {}; pk = 0; cnt = 0; w.store (0, std::memory_order_release); hopStartPpq = 0; }
    // audio thread: n samples starting at song position ppq (samplesPerBeat for the per-sample position)
    void feed (const float* L, const float* R, int n, double ppq, double samplesPerBeat) noexcept
    {
        if (ring.empty()) return;
        for (int i = 0; i < n; ++i)
        {
            if (cnt == 0) hopStartPpq = ppq + (double) i / samplesPerBeat;
            const float x = 0.5f * (L[i] + R[i]);
            const float a = lp1.lp (x), b = lp2.lp (x), c = lp3.lp (x);
            const float bands[4] { a, b - a, c - b, x - c };
            for (int k = 0; k < 4; ++k)
            {   // band energy: a fast follower (0.3 ms attack, slow release: where a hit starts) and an 8 ms smoothed one (detection)
                const float q = bands[k] * bands[k]; float& e = env[(size_t) k];
                e += (q > e ? att : rel[(size_t) k]) * (q - e);
                sm[(size_t) k] += smA * (q - sm[(size_t) k]);
            }
            pk = std::max (pk, std::abs (x));
            if (++cnt >= hop)
            {
                const int wi = w.load (std::memory_order_relaxed);
                auto& r = ring[(size_t) (wi & (kRing - 1))];
                r.ppq = hopStartPpq; for (int k = 0; k < 4; ++k) { r.e[k] = sm[(size_t) k]; r.f[k] = env[(size_t) k]; } r.peak = pk;
                w.store (wi + 1, std::memory_order_release);
                pk = 0; cnt = 0;
            }
        }
    }
    // any thread: a copy of the newest records (oldest first); records being overwritten while copying are dropped
    std::vector<OnsetRec> snapshot (int maxRecs = kRing - 1024) const
    {
        std::vector<OnsetRec> v;
        if (ring.empty()) return v;
        const int end = w.load (std::memory_order_acquire), n = std::min (end, std::min (maxRecs, kRing - 1024));
        v.reserve ((size_t) n);
        for (int i = end - n; i < end; ++i) v.push_back (ring[(size_t) (i & (kRing - 1))]);
        const int end2 = w.load (std::memory_order_acquire), lost = (end2 - kRing + 1) - (end - n);   // the writer may have lapped the first records
        if (lost > 0) v.erase (v.begin(), v.begin() + std::min ((int) v.size(), lost));
        return v;
    }
    double hopSamples() const noexcept { return (double) hop; }
    double rate() const noexcept { return sr; }
private:
    std::vector<OnsetRec> ring; std::atomic<int> w { 0 };
    OnePole lp1, lp2, lp3; std::array<float, 4> env {}, rel {}, sm {}; float att = 1.0f, smA = 1.0f, pk = 0; int cnt = 0, hop = 64; double sr = 48000, hopStartPpq = 0;
    static float coef (double sr, double ms) { return 1.0f - (float) std::exp (-1.0 / std::max (1.0, ms * 0.001 * sr)); }
};

struct TlHit { double beat = 0; float levelDb = -100.0f; float strength = 0.0f; };   // beat: from the loop start
struct TlAnalysis { bool ok = false; juce::String why; double loopStart = 0; std::vector<TlHit> hits; };

// Onset detection over ONE loop window [loopStart, loopStart + loopBeats) of the feed's records: the rise of each band's smoothed log
// energy over 8 ms (positive parts), weighted and summed; a hit is a local maximum (+-8 hops) above an adaptive threshold
// (the largest of 6 dB, a tenth of the strongest rise and median + 3 x the mean absolute deviation), at least 40 ms after the
// previous hit, with the signal above -60 dBFS. It starts at the first hop whose fast energy (in the band that rose most) is 6 dB
// over its level just before the rise. Its level = the peak in the 30 ms after it (dBFS). Pure function (any thread).
TlAnalysis tlAnalyse (const std::vector<OnsetRec>& recs, double loopStart, double loopBeats, double samplesPerBeat, double hopSamples);
// The window: the last COMPLETE loop the records cover (loops counted from 'origin' in steps of loopBeats). Fails with a reason
// when the records do not cover one loop without a jump.
TlAnalysis tlAnalyseLastLoop (const std::vector<OnsetRec>& recs, double origin, double loopBeats, double samplesPerBeat, double hopSamples);
// The hits a rule picks (indices into hits). beatsPerBar for LAST HIT OF BAR. QUIET / LOUD: below / at or above the middle of the
// quietest and loudest hit level (dB); when every hit is within 3 dB, all are LOUD and none is QUIET.
std::vector<int> tlPickHits (const std::vector<TlHit>& hits, int rule, double beatsPerBar);

} // namespace gpfx
#endif
