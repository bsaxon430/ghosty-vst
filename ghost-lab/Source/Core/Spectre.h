#pragma once
// SPECTRE (build 17): Ghost Lab's real-time resynthesis room, and the page Ghost Lab opens on.
// It works the way CRQL's RESYNTH does (from its manual): the sound is rebuilt from up to 512 oscillators per side
// (mid and side, 1,024 in all), and instead of a dry / wet knob an XY pad MORPHS between your sound and the rebuilt one
// in two separate parts of the spectrum:
//   CEPSTRUM (pad, up / down)   the tone: the log magnitude spectrum (its cepstrum) blended from your sound's to the
//                               rebuilt sound's
//   PHASE    (pad, left / right) the phase, blended the same way (around the circle)
// so one corner is your sound untouched, the opposite corner is all oscillators, and everything between is a morph of the
// two - not a crossfade. The rest:
//   MODE        IF: one oscillator per spectrum bin, its frequency from the bin's instantaneous frequency (it can only
//               move within its own bin): clean. PEAK: oscillator n plays the n-th loudest peak, so peaks swap oscillators
//               (with SMOOTH: sparkly). TRACK: each peak followed by the same oscillator (smooth glides).
//   SHIFT       formant shift (the cepstral envelope moved up or down, up to an octave), on the morphed sound
//   TRANSPOSE   the oscillators, in semitones (the tone from your sound keeps its formants where they were)
//   LOCD        every phase locked to a sawtooth at the note's own pitch (best on bass)
//   SMOOTH      each partial glides from where it was;  CLOUD  partials ring on and drift apart;  INF  hold / freeze
//   MID / SIDE  how many oscillators rebuild the middle and the sides (0: that part is left as it was)
//   UPDATE RATE how often the oscillators take a new picture of the sound: free (ms) or snapped to the song's tempo
//   NOISE / TRANSIENT  what is not a partial (breath, hiss, the body of a drum) played back as noise; the first few ms of
//               every hit let through as they were
//   WAVETABLE   each oscillator plays a single-cycle wavetable instead of a sine (band-limited)
// The methods are textbook (short-time Fourier analysis, McAulay & Quatieri's sinusoidal model, Serra & Smith's
// deterministic + stochastic split, cepstral smoothing); the code is our own. Real-time safe: everything is allocated in
// prepare(), nothing locks. AGPL-3.0-only with the rest of Ghost Lab (see LICENSE, NOTICE.md).
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <vector>

namespace glab::spectre
{
enum Param { PhaseX = 0, CepstrumY, Mode, Shift, Transpose, Locd, Smooth, Cloud, Inf, MidOscs, SideOscs, Link,
             Rate, Sync, Division, Noise, Transient, Output, kParams };
enum ModeKind { ModeIF = 0, ModePeak, ModeTrack };

struct ParamInfo
{
    const char *key, *name, *plain;
    float lo, hi, def;
    const char* unit;
    bool bipolar;
};
const ParamInfo& param (int p);
int paramIndex (const juce::String& key);   // -1 when unknown
// how far along its travel a control sits (0..1), and back. The page's controls and FL's automation use the same travel.
float toNorm (int p, float v);
float fromNorm (int p, float t);
juce::String display (int p, float v);      // "+7 st", "IF", "1/8", "INF" ...
bool isStepped (int p);                     // whole numbers only (MODE, TRANSPOSE, MID, SIDE, the switches, DIVISION)
const juce::StringArray& modeNames();
const juce::StringArray& divisionNames();   // UPDATE RATE when SNAPPED: 1/64 .. 1 bar

struct Params
{
    std::array<float, kParams> v {};
    Params();                                         // every control at its default
    float operator[] (int p) const { return v[(size_t) p]; }
};

constexpr int kMaxOscs = 1024;                        // (per voice: IF mode can use any of the 1,024 bins; at most 512 play)

// ---- the wavetables: one cycle of 2048 samples, kept as 11 band-limited copies
constexpr int kTableSize = 2048, kTableLevels = 11;
struct Wavetable
{
    std::array<std::array<float, kTableSize + 1>, kTableLevels> level {};
    bool sine = true;
    void build (const std::vector<float>& cycle);    // empty: sine. Message / worker thread only.
};
const juce::StringArray& tableNames();
std::vector<float> tableCycle (int index);
std::vector<float> cycleFromFile (const juce::AudioBuffer<float>& b);
juce::String tableToText (const std::vector<float>& cycle);
std::vector<float> tableFromText (const juce::String& text);

// one mono resynthesizer: analysis, the oscillators, the noise and the attacks (the "wet" sound)
class Voice
{
public:
    void prepare (double sampleRate);
    void reset();
    void process (const float* in, float* out, int n, const Params& p, const Wavetable* table, int oscs, double bpm);
    int latency() const { return fftSize; }
    int fftSize = 2048, hop = 512;

    static constexpr int kShow = 256;
    std::array<std::atomic<float>, kShow> showFreq {}, showAmp {};
    std::atomic<int> showCount { 0 };
    std::atomic<int> activeCount { 0 };
    std::atomic<float> level { 0.0f };
    std::atomic<int> onsets { 0 };

private:
    double sr = 48000.0;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> ring, window, work, cep, logMag, phase, prevPhase, prevLog, env, synth, resid, noiseEnv, noiseBuf, outRing, gainRing;
    int ringPos = 0, synthPos = 0, order = 11, ringOut = 0, holdCount = 0, lastMode = -1;
    float winSum = 1.0f, prevFlux = 0.0f;
    bool onsetNow = false, freshFrame = true;
    juce::Random rng { 1234 };

    struct Peak { float fSrc, amp, fOut, aOut, bin, ph; };
    std::vector<Peak> peaks;
    std::vector<int> order_;

    std::array<float, kMaxOscs> fSrc {}, fCur {}, fTgt {}, aCur {}, aTgt {}, re {}, im {}, detune {}, tgtPh {};
    std::array<bool, kMaxOscs> active {}, matched {}, hasTgt {};
    std::vector<int> freeList, sortedActive;
    std::array<float, kMaxOscs + 8> pRe {}, pIm {}, pAmp {}, pDa {}, pC {}, pS {}, pF0 {}, pF1 {}, pDw {}, pPh {}, pInc {};
    std::array<int, kMaxOscs + 8> pIdx {};
    std::array<const float*, kMaxOscs + 8> pTab {};

    void analyse (const Params& p, int oscs, double bpm);
    void renderHop (const Params& p, const Wavetable* table);
    void renderNoise (const Params& p);
    float envAt (float hz) const;
    void freeAll();
};

// the morph: your sound and the rebuilt one, blended in tone (cepstrum) and phase, then SHIFT and LOCD, per frame
class Morph
{
public:
    void prepare (double sampleRate, int fftOrder);
    void reset();
    void process (const float* dry, const float* wet, float* out, int n, const Params& p);   // out may be dry or wet
    int latency() const { return fftSize; }
    int fftSize = 2048, hop = 512;

private:
    double sr = 48000.0;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> dryRing, wetRing, window, D, W, cep, lm, phs, env, outRing;
    int pos = 0, count = 0, outPos = 0;
    double f0Phase = 0.0;
    void frame (const Params& p);
};

// the stereo engine: mid and side each rebuilt by their own Voice, then each morphed with your sound
class Engine
{
public:
    Engine();
    void prepare (double sampleRate, int maxBlock);
    void reset();
    void process (float* left, float* right, int n, const Params& p);
    int latency() const { return mid.latency() + morphM.latency(); }
    bool prepared() const { return isPrepared; }
    void setTable (const std::vector<float>& cycle);   // message / worker thread
    void setTempo (double bpm) { tempo.store (bpm > 20.0 ? bpm : 120.0); }

    Voice mid, side;
    static constexpr int kScope = 2048;
    std::array<std::atomic<float>, kScope> scope {};
    std::atomic<int> scopePos { 0 };
    std::atomic<float> sideLevel { 0.0f };

private:
    bool isPrepared = false;
    Morph morphM, morphS;
    std::vector<float> wetM, wetS, inM, inS, dryM, dryS;
    std::vector<float> delayM, delayS;
    int delayPos = 0;
    float gainNow = 1.0f, limEnv = 0.0f, limRelease = 0.999f;
    std::unique_ptr<std::array<Wavetable, 3>> tables;
    std::atomic<int> tableNow { 0 }, tableInUse { 0 };
    std::atomic<double> tempo { 120.0 };
};

juce::AudioBuffer<float> renderOffline (const juce::AudioBuffer<float>& in, double sampleRate, const Params& p,
                                        const std::vector<float>& table = {}, const std::atomic<bool>* cancel = nullptr, double bpm = 120.0);
}
