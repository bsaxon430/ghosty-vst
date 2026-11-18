#pragma once
// SPIRIT EQ: engine class shared by FxEq.cpp (DSP) and FxEqPage.cpp (editor).
#include "FxEqDsp.h"

namespace gpfx {
namespace eq {

enum Mode { mZeroLatency, mLinearLow, mLinearMed, mLinearHigh, kNumModes };
enum Character { chClean, chSubtle, chWarm };
enum Detect { dtBand, dtWide };

inline juce::String bid (int k, const char* s) { return "fx_eq_b" + juce::String (k + 1) + "_" + s; }
const juce::StringArray& typeNames();
const juce::StringArray& slopeNames();
const juce::StringArray& placementNames();
const juce::StringArray& modeNames();
juce::StringArray bandParamSuffixes();   // every per-band parameter suffix ("on", "type", ...)
juce::StringArray allParamIds();          // every fx_eq_* parameter this file adds

// Lock-free single-producer / single-consumer sample FIFO for the analyser.
struct AnaFifo
{
    static constexpr int kSize = 1 << 15;
    std::vector<float> buf = std::vector<float> ((size_t) kSize, 0.0f);
    juce::AbstractFifo fifo { kSize };
    void push (const float* L, const float* R, int n) noexcept
    {
        int s1, z1, s2, z2; fifo.prepareToWrite (n, s1, z1, s2, z2);
        for (int i = 0; i < z1; ++i) buf[(size_t) (s1 + i)] = 0.5f * (L[i] + R[i]);
        for (int i = 0; i < z2; ++i) buf[(size_t) (s2 + i)] = 0.5f * (L[z1 + i] + R[z1 + i]);
        fifo.finishedWrite (z1 + z2);
    }
    int pull (float* dst, int maxN) noexcept
    {
        int s1, z1, s2, z2; fifo.prepareToRead (maxN, s1, z1, s2, z2);
        for (int i = 0; i < z1; ++i) dst[i] = buf[(size_t) (s1 + i)];
        for (int i = 0; i < z2; ++i) dst[z1 + i] = buf[(size_t) (s2 + i)];
        fifo.finishedRead (z1 + z2);
        return z1 + z2;
    }
};

// STFT engine for spectral dynamics (sqrt-Hann, 75% overlap, latency = N).
struct Spectral
{
    struct BandInfo { int type; float f0, q, thr, range, ratio; int band; };
    int N = 2048, hop = 512, order = 11; double sr = 48000;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> win, inL, inR, accL, accR, wl, wr, lvl, gs;
    juce::int64 pos = 0; float invNorm = 1, binNorm = 1, attC = 0.5f, relC = 0.9f;
    std::array<BandInfo, kBands> bands {}; int numBands = 0;
    std::array<float, kBands> bandMaxDb {};
    static constexpr int kDisp = 128;
    std::array<float, kDisp> disp {};

    void prepare (double sampleRate);
    void reset();
    void process (float* L, float* R, int n) noexcept;
    void frame() noexcept;
};

class EqUnit : public UnitDsp
{
public:
    EqUnit();
    ~EqUnit() override;
    void attach (juce::AudioProcessorValueTreeState&) override;
    void prepare (double sr, int maxBlock) override;
    void reset() override;
    void clearLines() override { for (auto& r : dryRing) r.clear(); }
    void process (float* L, float* R, int n, const BlockCtx&) override;
    int latency() const override { return curLatency.load(); }
    void delayDry (float* L, float* R, int n) override;
    void idle (const float* L, const float* R, int n) override;

    // ---------------- shared with the editor
    struct BandParams { P on, byp, type, slope, freq, gain, q, stereo, dyn, thr, range, ratio, att, rel, autoThr, detect, spec; };
    std::array<BandParams, kBands> bp;
    P mode, character, scale, autoGain, invert, out;
    std::array<std::atomic<float>, kBands> dynDb {};      // live dynamic gain (dB) per band (gain types) / spectral max
    std::array<std::atomic<float>, kBands> dynFrac {};    // live dynamic amount 0..1 (cut types)
    std::array<std::atomic<float>, kBands> thrUsed {};    // threshold in use (auto threshold shows here)
    std::array<std::atomic<float>, Spectral::kDisp> specDisp {};   // spectral gain change per display slot (dB)
    std::atomic<int> soloBand { -1 };
    std::atomic<float> autoGainDb { 0.0f };
    std::atomic<double> sampleRate { 48000.0 };
    std::atomic<juce::uint32> uiStamp { 0 };
    AnaFifo preFifo, postFifo;
    bool analyserWanted() const noexcept { return juce::Time::getMillisecondCounter() - uiStamp.load (std::memory_order_relaxed) < 400; }

    // non-parameter editor state (message thread; guarded for saveState from other threads)
    juce::CriticalSection uiLock;
    juce::ValueTree abSlot[2] { juce::ValueTree ("A"), juce::ValueTree ("B") };
    int abActive = 0, anaMode = 3, dispRange = 1; bool peakHold = true;

    // effective static spec of a band (as the audio engine designs it), for the editor
    BandSpec specOf (int k, bool withDynamic) const;
    bool bandActive (int k) const { return bp[(size_t) k].on.b() && ! bp[(size_t) k].byp.b(); }

private:
    struct BandState
    {
        int type = -1, slope = -1, place = 0; bool firStatic = false;
        float amt = 0;                       // crossfade amount 0..1
        double fLog = std::log (1000.0), g = 0, q = 1; bool first = true;
        Sections sec; double lastF = -1, lastG = 1e9, lastQ = -1;
        std::array<std::array<Biquad, kMaxSections>, 2> st {};
        std::array<Biquad, 2> det {}; BiquadCoefs detC; int detType = -2; double detF = -1, detQ = -1;
        float env = 0, envAt = 0, envRt = 0, dyn = 0, frac = 0, avgDb = -60;
    };
    std::array<BandState, kBands> bs;
    double sr = 48000; bool attached = false;

    // config (latency) switching
    struct Config { int mode = 0; bool spectral = false; int firLen = 0; bool operator!= (const Config& o) const { return mode != o.mode || spectral != o.spectral || firLen != o.firLen; } };
    Config cur, target;
    enum TState { tsRun, tsFadeOut, tsWarm, tsFadeIn };
    TState ts = tsRun; float tGain = 1; int warm = 0;
    std::atomic<int> curLatency { 0 };
    int latencyOf (const Config& c) const { return (c.mode > 0 ? c.firLen / 2 : 0) + (c.spectral ? spec.N : 0); }
    int firLenFor (int m) const;
    Config computeTarget() const;
    juce::uint32 seq = 0, drySeq = 0;

    // dry delay
    std::array<Ring, 2> dryRing; int dryLat = 0; float dryGain = 1; int dryState = 0;

    // linear phase
    juce::dsp::Convolution conv;
    std::atomic<juce::AudioBuffer<float>*> irSlot { nullptr }, irTrash { nullptr };
    std::atomic<int> irPendingLen { 0 };
    int irLoadedLen = 0;

    Spectral spec;
    // character / output
    DcBlock dc[2]; Biquad warmLo[2], warmHi[2]; BiquadCoefs warmLoC, warmHiC;
    ExpSmooth outSm, polSm, soloSm, subtleSm, warmSm;
    Biquad soloF[2]; BiquadCoefs soloC; int soloFor = -1;
    std::array<float, kSub> tmpL {}, tmpR {};

    void processBands (float* L, float* R, int n) noexcept;
    void updateBand (int k, BandState& s, int n) noexcept;

    // background designer (linear-phase IR + auto gain)
    struct Worker;
    std::unique_ptr<Worker> worker;
    friend struct Worker;
};

} // namespace eq
} // namespace gpfx
