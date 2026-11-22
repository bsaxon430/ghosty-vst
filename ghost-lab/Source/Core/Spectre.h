#pragma once
// SPECTRE (build 17): Ghost Lab's real-time resynthesis room, and the page Ghost Lab opens on.
// It listens to the sound, finds up to 1,024 of its sine-wave partials (a short-time Fourier analysis with peak picking),
// and plays the sound back from a bank of 1,024 oscillators - so every partial can be bent on the way out:
//   PHASE      (pad, left / right)  left locks every partial's phase together (robot buzz), right scatters them (whisper)
//   CEPSTRUM   (pad, down / up)     the cepstrum splits the sound's body (its spectral envelope: the vowel, the formants)
//                                   from its fine detail (the harmonics). Up keeps only the body, down only the detail.
//   SHIFT / TRANSPOSE               every partial moves by the same number of Hz (metallic) / by semitones (in tune).
//                                   The body stays where it was, so a transposed voice still sounds like a voice.
//   CLOUD / SMOOTH                  partials ring on and drift apart (a wash) / every change glides
//   MID / SIDE                      how much of the middle and of the sides of the stereo picture is rebuilt
//   INF                             hold: at the top the sound freezes forever
// Our own code (no third-party DSP): the methods are the textbook ones (McAulay & Quatieri's sinusoidal model; cepstral
// smoothing of the log spectrum). Real-time safe: everything is allocated in prepare(), nothing locks.
// AGPL-3.0-only with the rest of Ghost Lab (see LICENSE, NOTICE.md).
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <vector>

namespace glab::spectre
{
enum Param { PhaseX = 0, CepstrumY, Shift, Transpose, Cloud, Smooth, Mid, Side, Inf, Partials, Output, kParams };

struct ParamInfo
{
    const char *key, *name, *plain;
    float lo, hi, def;
    const char* unit;
    bool bipolar;
};
const ParamInfo& param (int p);
int paramIndex (const juce::String& key);   // -1 when unknown

struct Params
{
    std::array<float, kParams> v {};
    Params();                                         // every control at its default
    float operator[] (int p) const { return v[(size_t) p]; }
};

constexpr int kMaxOscs = 1024;

// one mono resynthesizer: analysis, partial tracking and the oscillator bank
class Voice
{
public:
    void prepare (double sampleRate);
    void reset();
    // n samples in -> n samples out (in may equal out). The resynthesis runs a fixed latency behind (latency()).
    void process (const float* in, float* out, int n, const Params& p);
    int latency() const { return fftSize / 2 + hop; }
    int fftSize = 2048, hop = 512;

    // what the page draws (relaxed atomics; the audio thread writes them once per hop)
    static constexpr int kShow = 256;
    std::array<std::atomic<float>, kShow> showFreq {}, showAmp {};
    std::atomic<int> showCount { 0 };
    std::atomic<int> activeCount { 0 };
    std::atomic<float> level { 0.0f };

private:
    double sr = 48000.0;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> ring, window, work, cep, logMag, env, synth;
    int ringPos = 0, synthPos = 0, order = 11;
    float winSum = 1.0f;
    juce::Random rng { 1234 };

    struct Peak { float fSrc, amp, fOut, aOut; };
    std::vector<Peak> peaks;
    std::vector<int> order_;

    // the oscillators (structure of arrays)
    std::array<float, kMaxOscs> fSrc {}, fCur {}, fTgt {}, aCur {}, aTgt {}, re {}, im {}, detune {};
    std::array<bool, kMaxOscs> active {}, matched {};
    std::vector<int> freeList, sortedActive;
    // packed copies for the inner loop (multiple of 8)
    std::array<float, kMaxOscs + 8> pRe {}, pIm {}, pAmp {}, pDa {}, pC {}, pS {}, pF0 {}, pF1 {}, pDw {};
    std::array<int, kMaxOscs + 8> pIdx {};

    void analyse (const Params& p);
    void renderHop (const Params& p);
    float envAt (float hz) const;
};

// the stereo engine: mid and side each through their own Voice, with the dry signal delayed to line up
class Engine
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset();
    void process (float* left, float* right, int n, const Params& p);
    int latency() const { return mid.latency(); }
    bool prepared() const { return isPrepared; }

    Voice mid, side;
    // the output, for the page's scope (the last kScope samples of mid, written as they are made)
    static constexpr int kScope = 2048;
    std::array<std::atomic<float>, kScope> scope {};
    std::atomic<int> scopePos { 0 };
    std::atomic<float> sideLevel { 0.0f };   // how much is in the sides of the input (0 = mono)

private:
    bool isPrepared = false;
    std::vector<float> dryM, dryS, wetM, wetS, inM, inS;
    std::vector<float> delayM, delayS;
    int delayPos = 0;
    float gainNow = 1.0f, limEnv = 0.0f, limRelease = 0.999f;   // the safety limiter (never past -0.2 dBFS)
};

// PRINT IT: the whole sound through the engine, offline (worker thread). Returns the result (same rate, stereo), with
// the latency taken off the front and room at the end for CLOUD / INF to ring out.
juce::AudioBuffer<float> renderOffline (const juce::AudioBuffer<float>& in, double sampleRate, const Params& p, const std::atomic<bool>* cancel = nullptr);
}
