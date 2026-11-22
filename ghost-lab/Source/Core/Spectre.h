#pragma once
// SPECTRE (build 17): Ghost Lab's real-time resynthesis room, and the page Ghost Lab opens on.
// It listens to the sound and splits it in three, the way spectral modelling synthesis does (Serra & Smith, 1990):
//   the partials   up to 1,024 sine waves found by a short-time Fourier analysis (peak picking + tracking: McAulay &
//                  Quatieri, 1986), played back by an oscillator bank - each one can be bent on the way out
//   the noise      what is left once the partials are taken out (breath, hiss, the body of a drum), played back as
//                  shaped noise (NOISE)
//   the attacks    the first few milliseconds of each hit, let through as they were (TRANSIENT), so drums stay drums
// The controls:
//   PHASE      (pad, left / right)  left locks every partial's phase together (robot buzz), right scatters them (whisper)
//   CEPSTRUM   (pad, down / up)     the cepstrum splits the sound's body (its spectral envelope: the vowel, the formants)
//                                   from its fine detail (the harmonics). Up keeps only the body, down only the detail.
//   SHIFT / TRANSPOSE               every partial moves by the same number of Hz (metallic) / by semitones (in tune).
//                                   The body stays where it was, so a transposed voice still sounds like a voice.
//   CLOUD / SMOOTH                  partials ring on and drift apart (a wash) / every change glides
//   MID / SIDE                      how much of the middle and of the sides of the stereo picture is rebuilt
//   INF                             hold: at the top the sound freezes forever
//   WAVETABLE                       each partial plays a single-cycle wavetable instead of a sine (band-limited)
// Our own code (no third-party DSP). Real-time safe: everything is allocated in prepare(), nothing locks.
// AGPL-3.0-only with the rest of Ghost Lab (see LICENSE, NOTICE.md).
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <vector>

namespace glab::spectre
{
enum Param { PhaseX = 0, CepstrumY, Shift, Transpose, Cloud, Smooth, Mid, Side, Inf, Partials, Output, Noise, Transient, kParams };

struct ParamInfo
{
    const char *key, *name, *plain;
    float lo, hi, def;
    const char* unit;
    bool bipolar;
};
const ParamInfo& param (int p);
int paramIndex (const juce::String& key);   // -1 when unknown
// how far along its travel a control sits (0..1), and back: SHIFT is cubed (fine near zero), PARTIALS logarithmic.
// The page's controls and FL's automation lanes use the same travel.
float toNorm (int p, float v);
float fromNorm (int p, float t);
juce::String display (int p, float v);      // "+7 st", "512", "INF" ...

struct Params
{
    std::array<float, kParams> v {};
    Params();                                         // every control at its default
    float operator[] (int p) const { return v[(size_t) p]; }
};

constexpr int kMaxOscs = 1024;

// ---- the wavetables: one cycle of 2048 samples, kept as 11 band-limited copies (all its harmonics, then half, a
// quarter ... down to a sine), so a partial high up never folds back below Nyquist
constexpr int kTableSize = 2048, kTableLevels = 11;
struct Wavetable
{
    std::array<std::array<float, kTableSize + 1>, kTableLevels> level {};
    bool sine = true;                                // (no table: the oscillators are plain sines)
    void build (const std::vector<float>& cycle);    // empty: sine. Message / worker thread only.
};
const juce::StringArray& tableNames();               // the built-in ones: SINE first
std::vector<float> tableCycle (int index);           // one cycle of a built-in table (empty for SINE)
std::vector<float> cycleFromFile (const juce::AudioBuffer<float>& b);   // one cycle from a WAV (Serum / Vital style: the first 2048)
juce::String tableToText (const std::vector<float>& cycle);              // (saved with the session / a PRINT IT step)
std::vector<float> tableFromText (const juce::String& text);

// one mono resynthesizer: analysis, partial tracking, the oscillator bank, the noise and the attacks
class Voice
{
public:
    void prepare (double sampleRate);
    void reset();
    // n samples in -> n samples out (in may equal out). Runs latency() samples behind.
    void process (const float* in, float* out, int n, const Params& p, const Wavetable* table);
    int latency() const { return fftSize; }
    int fftSize = 2048, hop = 512;

    // what the page draws (relaxed atomics; the audio thread writes them once per hop)
    static constexpr int kShow = 256;
    std::array<std::atomic<float>, kShow> showFreq {}, showAmp {};
    std::atomic<int> showCount { 0 };
    std::atomic<int> activeCount { 0 };
    std::atomic<float> level { 0.0f };
    std::atomic<int> onsets { 0 };           // how many attacks it has found (tests, and the page's flash)

private:
    double sr = 48000.0;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> ring, window, work, cep, logMag, prevLog, env, synth, resid, noiseEnv, noiseBuf, outRing, gainRing;
    int ringPos = 0, synthPos = 0, order = 11, outPos = 0, ringOut = 0;
    float winSum = 1.0f, prevFlux = 0.0f;
    bool onsetNow = false;
    juce::Random rng { 1234 };

    struct Peak { float fSrc, amp, fOut, aOut, bin; };
    std::vector<Peak> peaks;
    std::vector<int> order_;

    // the oscillators (structure of arrays)
    std::array<float, kMaxOscs> fSrc {}, fCur {}, fTgt {}, aCur {}, aTgt {}, re {}, im {}, detune {};
    std::array<bool, kMaxOscs> active {}, matched {};
    std::vector<int> freeList, sortedActive;
    // packed copies for the inner loop (multiple of 8)
    std::array<float, kMaxOscs + 8> pRe {}, pIm {}, pAmp {}, pDa {}, pC {}, pS {}, pF0 {}, pF1 {}, pDw {}, pPh {}, pInc {};
    std::array<int, kMaxOscs + 8> pIdx {};
    std::array<const float*, kMaxOscs + 8> pTab {};

    void analyse (const Params& p);
    void renderHop (const Params& p, const Wavetable* table);
    void renderNoise (const Params& p);
    float envAt (float hz) const;
};

// the stereo engine: mid and side each through their own Voice, with the dry signal delayed to line up
class Engine
{
public:
    Engine();
    void prepare (double sampleRate, int maxBlock);
    void reset();
    void process (float* left, float* right, int n, const Params& p);
    int latency() const { return mid.latency(); }
    bool prepared() const { return isPrepared; }
    // the wavetable the partials play (empty: sines). Message / worker thread; the audio thread picks it up at its next
    // block without a lock (three slots: it never reads the one being written).
    void setTable (const std::vector<float>& cycle);

    Voice mid, side;
    // the output, for the page's scope (the last kScope samples of mid, written as they are made)
    static constexpr int kScope = 2048;
    std::array<std::atomic<float>, kScope> scope {};
    std::atomic<int> scopePos { 0 };
    std::atomic<float> sideLevel { 0.0f };   // how much is in the sides of the input (0 = mono)

private:
    bool isPrepared = false;
    std::vector<float> wetM, wetS, inM, inS;
    std::vector<float> delayM, delayS;
    int delayPos = 0;
    float gainNow = 1.0f, limEnv = 0.0f, limRelease = 0.999f;   // the safety limiter (never past -0.2 dBFS)
    std::unique_ptr<std::array<Wavetable, 3>> tables;
    std::atomic<int> tableNow { 0 }, tableInUse { 0 };
};

// PRINT IT: the whole sound through the engine, offline (worker thread). Returns the result (same rate, stereo), with
// the latency taken off the front and room at the end for CLOUD / INF to ring out.
juce::AudioBuffer<float> renderOffline (const juce::AudioBuffer<float>& in, double sampleRate, const Params& p,
                                        const std::vector<float>& table = {}, const std::atomic<bool>* cancel = nullptr);
}
