// Ghost Voice - AGPL-3.0-only (see LICENSE, NOTICE.md).
//
// Build 2 (Brandon 2026-11-05 09:59: "keep them all, put it all in there"): three engines in one Ghost Voice.
//   SEED-VC   AI re-sing: a new singer sings the performance (Seed-VC, GPL-3.0, the separate helper: VoiceEngine.h)
//   NATURAL   keeps the performer's own recording and reshapes it: subtle blends, instant (our own DSP)
//   CLASSIC   re-synthesises the voice (WORLD vocoder morph): instant (our own DSP)
//   BEST OF ALL  per blend: SEED-VC near voice A and voice B, NATURAL in the middle, crossfaded smoothly in between
// NATURAL and CLASSIC are Ghosty Spectrum's own engines, copied byte for byte into Source/Fusion (from ghosty-pro/Shared on
// main) and called exactly as Spectrum's A/B renderer (ghosty-pro/GhostVoice/research/GhostVoiceABC.cpp) calls them: the same
// file decode, the same working rate, the same 0.7 input peak, takes joined with 0.3 s of silence, the performance's own weight
// 0, identity B = voice A, identity C = voice B. The tests and CI null-test our renders against that program's.
// This header keeps the fusion headers out (their VoiceEngine.h shares a name with ours); DspEngine.cpp includes them.
#pragma once
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <vector>

namespace gv
{
enum class Engine { seedvc = 0, natural = 1, classic = 2, best = 3 };
constexpr int kEngines = 4;
juce::String engineName (Engine);     // SEED-VC / NATURAL / CLASSIC / BEST OF ALL
juce::String engineBlurb (Engine);    // plain words, one line
inline bool isDsp (Engine e) { return e == Engine::natural || e == Engine::classic; }
inline Engine engineFromInt (int i) { return (Engine) juce::jlimit (0, kEngines - 1, i); }

// the DSP engines' own controls that Ghost Voice shows (all 0 = the engine's defaults, exactly as Spectrum renders them)
struct DspSettings
{
    float tone = 0;            // TONE    = the engine's BRIGHTNESS (-1..1)
    float timbre = 0;          // TIMBRE  = the engine's FORMANT shift in semitones (-6..6): bigger / smaller voice
    float texture = 0;         // TEXTURE = the engine's GRIT (0..1)
    float breath = 0;          // BREATH  = the engine's BREATH (-1..1)
    float resonance = 0;       // RESONANCE (-1..1): moved by TERRAIN only
    float warmth = 0;          // WARMTH (-1..1): moved by TERRAIN only
    bool keepConsonants = true;   // the engine's KEEP MY CONSONANTS
    bool operator== (const DspSettings&) const = default;
};

// a DSP render job: the performance sung at each blend by NATURAL or CLASSIC
struct DspJob
{
    juce::StringArray a, b;
    juce::String performance;
    std::vector<float> mixes;  // B weights (0 = A, 1 = B)
    Engine engine = Engine::natural;   // natural or classic
    DspSettings settings;
    int semitones = 0;         // PITCH (the engine's own transpose; formants stay put)
    bool rights = false;
    juce::File outDir;
};
juce::String whyNotDsp (const DspJob&);

// ---------------------------------------------------------------- the render, exactly as Spectrum's GhostVoiceABC does it
// decode (any format JUCE reads, channels averaged, at most 180 s), resample to the working rate (or wantFs), 0.7 peak
bool loadForFusion (const juce::File&, std::vector<float>& x, int& fs, int wantFs, juce::String& err);
// renders job.mixes into job.outDir/<engine>_<mix>.wav (24-bit, like Spectrum). Returns mix -> file; times in ms.
// cancel may be null. Thread-safe (no shared state except the analysis cache, which has its own lock).
bool renderDsp (const DspJob&, std::map<float, juce::File>& out, std::map<float, double>& ms, juce::String& err,
                std::atomic<float>* progress = nullptr, std::atomic<bool>* cancel = nullptr);
// Spectrum's A/B layout (GhostVoiceABC's file names): classic_ / natural_ x anchor_a, anchor_b, fusion_025 / 050 / 075
// + timings.json, through renderDsp. The null test compares this to Spectrum's own program, sample for sample.
int renderAbcLayout (const juce::File& outDir, const juce::File& source, const juce::StringArray& a, const juce::StringArray& b);

// ---------------------------------------------------------------- the background runner (one DSP task at a time)
class DspRunner : private juce::Thread
{
public:
    DspRunner();
    ~DspRunner() override;
    enum class State { idle, running, done, failed };
    bool start (const DspJob&);
    void cancel();
    bool busy() const { return isThreadRunning(); }
    State state() const { return st.load(); }
    float progress() const { return prog.load(); }
    juce::String message() const;
    std::map<float, juce::File> results() const;
    double elapsed() const { return secs.load(); }
    DspJob job() const;
    std::function<void()> onFinished;     // on the message thread
    bool waitDone (int ms) { return waitForThreadToExit (ms); }
private:
    void run() override;
    DspJob j;
    std::atomic<State> st { State::idle };
    std::atomic<float> prog { 0 };
    std::atomic<bool> stop { false };
    std::atomic<double> secs { 0 };
    mutable std::mutex m;
    juce::String msg;
    std::map<float, juce::File> res;
};

// ---------------------------------------------------------------- BEST OF ALL
// how much of SEED-VC a blend gets: 1 within 10 % of A or of B, 0 from 25 % to 75 % (NATURAL alone), a smooth S-curve between
float seedWeight (float mixB);
// one take from a SEED-VC take and a NATURAL take of the same performance: at each sample w = seedWeight (mixAt (t)) and
// out = w * seed + (1 - w) * g * natural, where g is ONE constant gain that brings the NATURAL take to the SEED-VC take's
// loudness (so the crossfade doesn't jump in level; nothing else is done to either sound). natural is resampled to fs first
// when its rate differs. Length = the SEED-VC take's (or the NATURAL take's when there is no SEED-VC take).
std::vector<float> bestOfAll (const std::vector<float>& seed, const std::vector<float>& natural, int fs, int naturalFs,
                              const std::function<float (double)>& mixAt, float* gainUsed = nullptr);
std::vector<float> resampleTo (const std::vector<float>& x, int from, int to);   // the engines' own resampler
}
