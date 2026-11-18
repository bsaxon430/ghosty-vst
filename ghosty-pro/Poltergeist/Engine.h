#pragma once
// Poltergeist engine: 4-operator phase-modulation FM (any ghost can modulate any other), unison, per-voice
// filter, pitch envelope, glide, a tempo-syncable "drift" LFO, and the macro-driven effects chain.
// Voices run at 2x the host rate (JUCE oversampling) so hard FM and drive alias less.
// BASS update: 6 FM ghosts + a NOISE ghost, 31 band-limited waves, DAHDSR (+ loop) envelopes, two filters, a SUB,
// DIST + MULTIBAND drive, low-end mono, a drawn tempo-synced WOBBLE, two LFOs and an 8-slot mod matrix.
// With all of it off, the original path runs unchanged (old sounds render identically).
#include "Params.h"
#include "BassDsp.h"
#include "Sample.h"
#include "Lab.h"
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <random>

namespace gs {

// ---- small building blocks -------------------------------------------------------------------------
struct SineTable {
    static constexpr int N = 4096;
    float t[N + 1];
    SineTable() { for (int i = 0; i <= N; ++i) t[i] = (float) std::sin(juce::MathConstants<double>::twoPi * i / N); }
    inline float operator()(float cycles) const {   // any real phase in cycles
        cycles -= std::floor(cycles);
        const float x = cycles * N; int i = juce::jmin((int) x, N - 1); if (i < 0) i = 0; const float f = x - (float) i;   // cycles can round to exactly 1.0f (and a NaN never indexes out)
        return t[i] + (t[i + 1] - t[i]) * f;
    }
};
inline const SineTable& sineTable() { static SineTable s; return s; }

// The four ghost waves, morphed: 0 sine, 1 triangle, 2 saw-ish (phase distortion), 3 square-ish.
inline float waveAt(float p, float shape) {
    const auto& S = sineTable();
    auto one = [&](int w) -> float {
        switch (w) {
            case 0: return S(p);
            case 1: { float q = p - std::floor(p); return q < 0.25f ? 4 * q : (q < 0.75f ? 2 - 4 * q : 4 * q - 4); }
            case 2: { float q = p - std::floor(p); const float k = 0.12f; float d = q < k ? q * 0.5f / k : 0.5f + (q - k) * 0.5f / (1 - k); return S(d); }
            default: { float s = S(p); return std::tanh(4.0f * s) * 1.0366f; }
        }
    };
    const int a = juce::jlimit(0, 3, (int) shape);
    const float f = shape - (float) a;
    if (f < 0.001f || a == 3) return one(a);
    return one(a) * (1 - f) + one(a + 1) * f;
}

struct Env {
    enum Stage { Idle, Attack, Decay, Sustain, Release, Delay, Hold } stage = Idle;
    float level = 0, a = 0.01f, d = 0.3f, s = 0.5f, r = 0.3f, sr = 88200;
    float aInc = 0, dCoef = 0, rCoef = 0, dlyFade = 0.99f;
    int dlyN = 0, holdN = 0, cnt = 0; bool loop = false;   // BASS update: DAHDSR + loop (0 / 0 / off = the original ADSR)
    void setExtra(float dly, float hold, bool lp, float rate) { dlyN = (int) (dly * rate); holdN = (int) (hold * rate); loop = lp; }
    void set(float A, float D, float S, float R, float rate) {
        a = A; d = D; s = S; r = R; sr = rate;
        aInc = 1.0f / juce::jmax(1.0f, a * sr);
        dCoef = std::exp(-6.9f / juce::jmax(1.0f, d * sr));
        rCoef = std::exp(-6.9f / juce::jmax(1.0f, r * sr));
        dlyFade = std::exp(-6.9f / juce::jmax(1.0f, 0.004f * sr));   // a retriggered note fades out (4 ms) before its DELAY
    }
    // voice stealing: let the old note go in a few ms
    void fastRelease(float secs) { if (stage != Idle) { rCoef = std::exp(-6.9f / juce::jmax(1.0f, secs * sr)); relFrom = level; stage = Release; } }
    // where the envelope is, for the editor's tracer dot: stage (0 idle, 1 delay, 2 attack, 3 hold, 4 decay, 5 sustain, 6 release)
    // and the position inside it (0..1), from the same laws the envelope runs (attack linear, decay / release exp(-6.9 t / time))
    void position(int& st, float& frac) const {
        switch (stage) {
            case Delay: st = 1; frac = dlyN > 0 ? (float) cnt / (float) dlyN : 1.0f; break;
            case Attack: st = 2; frac = level; break;
            case Hold: st = 3; frac = holdN > 0 ? (float) cnt / (float) holdN : 1.0f; break;
            case Decay: st = 4; frac = 1.0f - s > 1e-4f ? -std::log(juce::jmax(1.0e-6f, (level - s) / (1.0f - s))) / 6.9f : 1.0f; break;
            case Sustain: st = 5; frac = 0.0f; break;
            case Release: st = 6; frac = relFrom > 1e-6f && level > 0.0f ? std::log(relFrom / level) / 6.9f : 1.0f; break;
            default: st = 0; frac = 0.0f; break;
        }
        frac = juce::jlimit(0.0f, 1.0f, frac);
    }
    void trigger(bool legato) { if (!legato || stage == Idle || stage == Release) { if (dlyN > 0) { stage = Delay; cnt = 0; } else stage = Attack; } }
    void release() { if (stage != Idle) { relFrom = level; stage = Release; } }
    float relFrom = 0;   // (display only: the level the release started from, for the envelope tracer)
    bool active() const { return stage != Idle; }
    inline float next() {
        switch (stage) {
            case Attack: level += aInc; if (level >= 1) { level = 1; if (holdN > 0) { stage = Hold; cnt = 0; } else stage = Decay; } break;
            case Decay: level = s + (level - s) * dCoef; if (std::abs(level - s) < 1e-4f) { level = s; stage = loop ? Attack : Sustain; } break;
            case Delay: level *= dlyFade; if (level < 1e-6f) level = 0; if (++cnt >= dlyN) stage = Attack; break;   // silent during the delay
            case Hold: if (++cnt >= holdN) stage = Decay; break;
            case Sustain: level = s; break;
            case Release: level *= rCoef; if (level < 1e-5f) { level = 0; stage = Idle; } break;
            default: break;
        }
        return level;
    }
};

// Zero-delay-feedback state variable filter (Simper), stereo, per voice.
struct SVF {
    float ic1[2] {}, ic2[2] {};
    float g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0;
    void setup(float cutoff, float res, float sr) {
        cutoff = juce::jlimit(20.0f, sr * 0.45f, cutoff);
        g = std::tan(juce::MathConstants<float>::pi * cutoff / sr);
        k = 2.0f - 1.96f * res;
        a1 = 1 / (1 + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    inline float tick(int ch, float v0, int type) {
        float v3 = v0 - ic2[ch];
        float v1 = a1 * ic1[ch] + a2 * v3;
        float v2 = ic2[ch] + a2 * ic1[ch] + a3 * v3;
        ic1[ch] = 2 * v1 - ic1[ch]; ic2[ch] = 2 * v2 - ic2[ch];
        if (type == 0) return v2;
        if (type == 1) return v1 * k;
        return v0 - k * v1 - v2;
    }
    void reset() { ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0; }
};

// ---- voice -----------------------------------------------------------------------------------------
constexpr int kMaxUnison = 7;
constexpr int kMaxVoices = 16;

struct Voice {
    int note = -1; float vel = 0; bool held = false; uint64_t age = 0;
    float curNote = 60, targetNote = 60;
    Env opEnv[kOps], fEnv; float penv = 0;
    float phase[kMaxUnison][kOps] {}, prev[kMaxUnison][kOps] {}, prev2[kMaxUnison][kOps] {};
    SVF filt;
    float outMeter[kOps] {};
    // v0.2: sample ghost, talking filter, bubbles, hard sync
    Env smpEnv; float ghostGain = 1, smpGain = 1; double spos = 0; int slice = 0; float wtPhase = 0, grainClock = 0;
    struct Grain { double pos = 0, inc = 1; int age = 0, len = 0; float pan = 0; bool on = false; } grains[16];
    SVF vf[3];
    float bubPhase = 0, bubNote = 0, bubSweep = 0, bubAmp = 0, bubClock = 0, bubPan = 0; double bubStepAt = -1;
    float fph[kMaxUnison] {};
    const SampleData* key = nullptr; double kpos = 0;   // a Sample Lab key: this voice plays that section only
    const ChopKit::Slot* chop = nullptr; double cpos = 0, cinc = 1; float cgain = 1; int cslot = -1; bool cgate = false;   // a CHOP slice
    // BASS update
    uint8_t useMask = 0x0F;              // which ghosts keep the voice alive: 1-4 always (as before), 5 / 6 / noise when used, bit 7 = the sub's own env
    BassFilter bf[2];
    NoiseGen nz; SVF2 nzF; Env nzEnv;
    float subPh = 0; SVF2 subF; Env subEnv;
    bool sustained = false;              // released while the sustain pedal is down
    float syncHold[kMaxUnison] {};       // band-limited sync: ghost 1's previous output, waiting for its correction
    const float* tblCur[kOps] {}; const float* tblOld[kOps] {}; float tblX[kOps] {};   // wave table now / before, crossfade left
    static constexpr int kMC = 11;       // modulation: 0 FM, 1-6 ghost levels, 7 pitch, 8 noise, 9 sub, 10 volume
    float mc[kMC] {}, mi[kMC] {}; bool modInit = false;
    float rnd = 0; double wobClock = 0;
    bool active() const {
        if (key || chop) return smpEnv.active();
        for (int o = 0; o < kOps; ++o) if (((useMask >> o) & 1) && opEnv[o].active()) return true;
        if (((useMask >> 6) & 1) && nzEnv.active()) return true;
        if (((useMask >> 7) & 1) && subEnv.active()) return true;
        return smpEnv.active();
    }
};

// ---- engine ----------------------------------------------------------------------------------------
class Engine {
public:
    // the oversampling the engine will really use: 2x / 4x / 8x, capped so the voices never run above ~400 kHz
    static int effectiveFactor(double rate, int want) { int f = want >= 8 ? 8 : (want >= 4 ? 4 : 2); while (f > 2 && rate * f > 400000.0) f /= 2; return f; }
    int oversampling() const { return osF; }
    // (message thread / prepareToPlay only: allocates) osFactor = 2 is the original engine
    void prepare(double hostRate, int maxBlock, int osFactor = 2) {
        sr = hostRate; osF = effectiveFactor(hostRate, osFactor); osr = hostRate * osF; maxBlk = juce::jmax(1, maxBlock);
        ctlLen = 8 * osF; ctlShift = osF == 2 ? 4 : (osF == 4 ? 5 : 6); ctlMask = ctlLen - 1; ctlInv = 1.0f / (float) ctlLen;
        const size_t stages = osF == 2 ? 1 : (osF == 4 ? 2 : 3);
        os = std::make_unique<juce::dsp::Oversampling<float>>(2, stages, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);
        os->initProcessing((size_t) maxBlock);
        juce::dsp::ProcessSpec spec { hostRate, (juce::uint32) maxBlock, 2 };
        reverb.prepare(spec);
        delayL.assign((size_t) (hostRate * 2.5), 0.0f); delayR = delayL; dPos = 0;
        for (auto& v : voices) { v = Voice(); }
        rng.seed(1234);
        lfoPhase = 0; lfoHold = 0;
        dcX[0] = dcX[1] = dcY[0] = dcY[1] = 0;
        crushHold[0] = crushHold[1] = 0; crushCount = 0;
        gateLevel = 1; gateStep = -1;
        ring.assign((size_t) (hostRate * 8.0), 0.0f); ringPos = 0;
        for (auto& c : combBuf) c.assign((size_t) (hostRate / 20.0) + 4, 0.0f);
        combPos = 0; combHz = 220; seqVal = 0; freeClock = 0;
        juce::dsp::ProcessSpec mono { hostRate, (juce::uint32) maxBlock, 2 };
        for (auto& x : xLow) { x.prepare(mono); x.setCutoffFrequency(120.0f); }
        for (auto& x : xHigh) { x.prepare(mono); x.setCutoffFrequency(2500.0f); }
        xLow[0].setType(juce::dsp::LinkwitzRileyFilterType::lowpass); xLow[1].setType(juce::dsp::LinkwitzRileyFilterType::highpass);
        xHigh[0].setType(juce::dsp::LinkwitzRileyFilterType::lowpass); xHigh[1].setType(juce::dsp::LinkwitzRileyFilterType::highpass);
        for (auto& e : ottEnv) e = 0;
        bands.setSize(6, maxBlock);
        // BASS update
        waveBank();   // build the tables here, never on the audio thread
        osSub = std::make_unique<juce::dsp::Oversampling<float>>(1, stages, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);
        osSub->initProcessing((size_t) maxBlock);
        subHost.assign((size_t) maxBlock + 16, 0.0f);
        combN = (int) (osr / 18.0) + 8;
        combStore.assign((size_t) kMaxVoices * 4 * (size_t) combN, 0.0f);
        for (int vi = 0; vi < kMaxVoices; ++vi)
            for (int f = 0; f < 2; ++f) { auto& F = voices[(size_t) vi].bf[f]; F.combN = combN; for (int ch = 0; ch < 2; ++ch) F.comb[ch] = combStore.data() + (((size_t) vi * 4 + (size_t) f * 2 + (size_t) ch) * (size_t) combN); }
        for (int vi = 0; vi < kMaxVoices; ++vi) voices[(size_t) vi].nz.s = 0x9E3779B9u * (uint32_t) (vi + 1);
        const int ctlMax = (maxBlock * 2) / 16 + 4;
        for (auto* a : { &wobArr, &lfo1Arr, &lfo2Arr, &driveArr }) a->assign((size_t) ctlMax, 0.0f);
        juce::dsp::ProcessSpec ospec { osr, (juce::uint32) (maxBlock * osF), 2 };
        for (auto& x : mbX) { x.prepare(ospec); }
        mbAp.prepare(ospec); mbAp.setType(juce::dsp::LinkwitzRileyFilterType::allpass);
        mbX[0].setType(juce::dsp::LinkwitzRileyFilterType::lowpass); mbX[1].setType(juce::dsp::LinkwitzRileyFilterType::highpass);
        mbX[2].setType(juce::dsp::LinkwitzRileyFilterType::lowpass); mbX[3].setType(juce::dsp::LinkwitzRileyFilterType::highpass);
        mbLo = mbHi = -1;
        lfoPh[0] = lfoPh[1] = 0; lfoRnd[0][0] = lfoRnd[0][1] = lfoRnd[1][0] = lfoRnd[1][1] = 0; lfoCyc[0] = lfoCyc[1] = -1; wobFree = 0;
        dsHold[0] = dsHold[1] = 0; dsCount = 0; distLp[0] = distLp[1] = 0;
        lmA.reset(); lmB.reset();
        // review round: squeeze phase match, ramps, pedal, safety
        for (auto& x : ottAp) { x.prepare(mono); x.setType(juce::dsp::LinkwitzRileyFilterType::allpass); x.setCutoffFrequency(2500.0f); }
        primed = false; masterPrimed = false; sustain = false; badBlocks = 0;
    }
    int latency() const { return os ? (int) os->getLatencyInSamples() : 0; }
    void setBpm(double b) { if (b > 20 && b < 400) bpm = b; }
    void setPpq(double p, bool playing) { ppq = p; hostPlaying = playing; }
    void setModWheel(float m) { modWheel = m; }
    // Sample Ghost: swapped in from the message thread
    void setSample(std::shared_ptr<const SampleData> s) { std::atomic_store(&sample, std::move(s)); }
    std::shared_ptr<const SampleData> getSample() const { return std::atomic_load(&sample); }
    // Resample: the last few seconds of output (mono), copied on the message thread
    std::vector<float> recent(double seconds) const {
        const int N = (int) ring.size(); if (!N) return {};
        const int n = juce::jmin(N, (int) (seconds * sr)); std::vector<float> out((size_t) n);
        const int end = ringPos.load();
        for (int i = 0; i < n; ++i) out[(size_t) i] = ring[(size_t) ((end - n + i + N) % N)];
        return out;
    }
    double sampleRate() const { return sr; }
    std::atomic<int> seqStepNow { -1 };
    void setKeyMap(const KeyMap* k) { keymap.store(k); }
    void setChopKit(const ChopKit* k) { chopKit.store(k); }   // CHOP mode slices (raw pointer, kept alive by the processor)
    std::atomic<int> chopSounding { -1 };                       // UI: the slice playing now
    std::atomic<float> smpPlayhead { -1.0f };

    // Plays one block. midi events are host-rate sample positions.
    // A host may send a bigger block than it prepared: it is played in pieces of at most the prepared size.
    void process(juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, const Snap& p) {
        P = p;
        const int n = out.getNumSamples();
        if (n <= maxBlk) { processChunk(out, midi, 0, n, true); if (traceOn.load(std::memory_order_relaxed)) publishTrace(); return; }
        const double ppq0 = ppq;
        for (int off = 0; off < n; off += maxBlk) {
            const int len = juce::jmin(maxBlk, n - off);
            ppq = ppq0 + off / sr * bpm / 60.0;   // each piece starts at its own beat position
            juce::AudioBuffer<float> view(out.getArrayOfWritePointers(), 2, off, len);   // refers to the host buffer, no allocation
            processChunk(view, midi, off, len, off + len >= n);
        }
        ppq = ppq0;
        if (traceOn.load(std::memory_order_relaxed)) publishTrace();
    }

    void hardReset() { recoverFromBlowUp(); }   // every voice and effect tail cleared at once (BYPASS once it has faded out)
    std::atomic<bool> wobView { false };                  // the editor shows the WOBBLE page: keep its playhead moving

    // ---- ENVELOPE TRACER (display only): the newest voices' envelope positions, published once per block (seqlock, no locks,
    // no allocation) while the editor's envelope view is showing. Envelopes: 0-5 the ghosts, 6 noise, 7 filter.
    static constexpr int kTraceVoices = 4, kTraceEnvs = 8;
    struct TraceVoice { int id = -1, note = -1; int stage[kTraceEnvs] {}; float frac[kTraceEnvs] {}, level[kTraceEnvs] {}; };
    struct TraceSnap { int count = 0; uint32_t seq = 0; bool torn = false; TraceVoice v[kTraceVoices]; };
    std::atomic<bool> traceOn { false };
    static uint32_t bitsOf(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }
    static uint32_t mixCheck(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u + 0x9e3779b9u; }   // (exact: integer only)
    bool readTrace(TraceSnap& out) const {   // any thread; false if nothing was published yet or the writer kept it busy (try again next frame)
        out.torn = false;
        for (int attempt = 0; attempt < 8; ++attempt) {
            const uint32_t s0 = tr.seq.load(std::memory_order_acquire);
            if (s0 == 0) { out.count = 0; return false; }   // never published (the defaults carry no checksum)
            if (s0 & 1u) continue;
            out.count = tr.count.load(std::memory_order_relaxed); out.seq = s0;
            uint32_t chk = 0;
            for (int i = 0; i < kTraceVoices; ++i) {
                auto& d = out.v[i]; const auto& sv = tr.v[i];
                d.id = sv.id.load(std::memory_order_relaxed); d.note = sv.note.load(std::memory_order_relaxed); chk = mixCheck(chk, (uint32_t) d.id);
                for (int e = 0; e < kTraceEnvs; ++e) { d.stage[e] = sv.stage[e].load(std::memory_order_relaxed); d.frac[e] = sv.frac[e].load(std::memory_order_relaxed); d.level[e] = sv.level[e].load(std::memory_order_relaxed);
                                                       chk = mixCheck(mixCheck(mixCheck(chk, bitsOf(d.level[e])), bitsOf(d.frac[e])), (uint32_t) d.stage[e]); }
            }
            const uint32_t chkW = tr.check.load(std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_acquire);
            if (tr.seq.load(std::memory_order_relaxed) == s0) { out.torn = chk != chkW; return !out.torn; }   // (the checksum is a tear detector for the tests)
        }
        return false;
    }

    // live view for the UI: how hard each ghost is sounding, and a short scope buffer
    std::array<std::atomic<float>, kOps + 1> meter {};   // 6 FM ghosts + noise
    std::atomic<float> wobUi { -1.0f };                   // wobble phase now (UI playhead)
    static constexpr int kScope = 512;
    float scope[kScope] {}; std::atomic<int> scopePos { 0 };

private:
    struct TraceVoiceA { std::atomic<int> id { -1 }, note { -1 }; std::atomic<int> stage[kTraceEnvs] {}; std::atomic<float> frac[kTraceEnvs] {}, level[kTraceEnvs] {}; };
    struct TraceA { std::atomic<uint32_t> seq { 0 }; std::atomic<int> count { 0 }; std::atomic<uint32_t> check { 0 }; TraceVoiceA v[kTraceVoices]; } tr;
    void publishTrace() {
        // the newest active synth voices (by age)
        const Voice* pick[kTraceVoices] {}; int n = 0;
        for (auto& v : voices) {
            if (!v.active() || v.key || v.chop) continue;
            if (n < kTraceVoices) { pick[n++] = &v; }
            else { int oldest = 0; for (int i = 1; i < n; ++i) if (pick[i]->age < pick[oldest]->age) oldest = i; if (v.age > pick[oldest]->age) pick[oldest] = &v; }
        }
        for (int i = 1; i < n; ++i) for (int j = i; j > 0 && pick[j]->age > pick[j - 1]->age; --j) std::swap(pick[j], pick[j - 1]);   // newest first
        const uint32_t s0 = tr.seq.load(std::memory_order_relaxed);
        tr.seq.store(s0 + 1, std::memory_order_relaxed); std::atomic_thread_fence(std::memory_order_release);
        uint32_t chk = 0;
        for (int i = 0; i < kTraceVoices; ++i) {
            auto& d = tr.v[i];
            const int id = i < n ? (int) (pick[i]->age & 0x7fffffff) : -1;
            d.id.store(id, std::memory_order_relaxed); d.note.store(i < n ? pick[i]->note : -1, std::memory_order_relaxed); chk = mixCheck(chk, (uint32_t) id);
            for (int e = 0; e < kTraceEnvs; ++e) {
                int st = 0; float fr = 0, lv = 0;
                if (i < n) { const Env& en = e < kOps ? pick[i]->opEnv[e] : (e == kOps ? pick[i]->nzEnv : pick[i]->fEnv); en.position(st, fr); lv = en.level; }
                d.stage[e].store(st, std::memory_order_relaxed); d.frac[e].store(fr, std::memory_order_relaxed); d.level[e].store(lv, std::memory_order_relaxed);
                chk = mixCheck(mixCheck(mixCheck(chk, bitsOf(lv)), bitsOf(fr)), (uint32_t) st);
            }
        }
        tr.count.store(n, std::memory_order_relaxed); tr.check.store(chk, std::memory_order_relaxed);
        tr.seq.store(s0 + 2, std::memory_order_release);
    }
    void processChunk(juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, int off, int n, bool last) {
        out.clear();
        subThisBlock = false;
        if (P.subOn || P.subSolo) std::fill(subHost.begin(), subHost.begin() + juce::jmin(n, (int) subHost.size()), 0.0f);
        int pos = 0;
        for (const auto md : midi) {
            const int sp = md.samplePosition - off;
            if (sp >= n && !last) break;                 // a later piece plays it
            if (sp < 0 && off > 0) continue;              // an earlier piece played it
            const int at = juce::jlimit(0, n, sp);
            if (at > pos) { renderSegment(out, pos, at - pos); pos = at; }
            handle(md.getMessage());
        }
        if (pos < n) renderSegment(out, pos, n - pos);
        effects(out);
        // safety: a non-finite sample (should never happen) would stick in every effect; drop the block and start clean
        float acc = 0; const float* L = out.getReadPointer(0); const float* R = out.getReadPointer(1);
        for (int i = 0; i < n; ++i) acc += std::abs(L[i]) + std::abs(R[i]);
        if (!std::isfinite(acc)) { out.clear(); recoverFromBlowUp(); ++badBlocks; }
    }
    void recoverFromBlowUp() {
        reverb.reset(); std::fill(delayL.begin(), delayL.end(), 0.0f); std::fill(delayR.begin(), delayR.end(), 0.0f); dLp[0] = dLp[1] = 0;
        for (auto& c : combBuf) std::fill(c.begin(), c.end(), 0.0f);
        dcX[0] = dcX[1] = dcY[0] = dcY[1] = 0; crushHold[0] = crushHold[1] = 0;
        for (auto& e : ottEnv) e = 0;
        for (auto* xs : { &xLow[0], &xLow[1], &xHigh[0], &xHigh[1], &ottAp[0], &ottAp[1], &mbX[0], &mbX[1], &mbX[2], &mbX[3], &mbAp }) xs->reset();
        lmA.reset(); lmB.reset(); distLp[0] = distLp[1] = 0; dsHold[0] = dsHold[1] = 0;
        if (os) os->reset();
        if (osSub) osSub->reset();
        for (auto& v : voices) {
            for (auto& e : v.opEnv) { e.stage = Env::Idle; e.level = 0; }
            for (auto* e : { &v.fEnv, &v.smpEnv, &v.nzEnv, &v.subEnv }) { e->stage = Env::Idle; e->level = 0; }
            v.filt.reset(); v.subF.reset(); v.nzF.reset();
            for (auto& f : v.vf) f.reset();
            for (auto& f : v.bf) f.reset(true);
            std::memset(v.prev, 0, sizeof(v.prev)); std::memset(v.prev2, 0, sizeof(v.prev2));
            std::memset(v.phase, 0, sizeof(v.phase)); for (auto& x : v.fph) x = 0; for (auto& x : v.syncHold) x = 0;
            v.held = false; v.sustained = false;
        }
        heldCount = 0;
    }
    Snap P {};
    double sr = 44100, osr = 88200, bpm = 120, ppq = 0; bool hostPlaying = false;
    int osF = 2, maxBlk = 512, ctlLen = 16, ctlShift = 4, ctlMask = 15; float ctlInv = 1.0f / 16.0f;
    // review round state
    juce::dsp::LinkwitzRileyFilter<float> ottAp[2];      // SQUEEZE: the low band's phase match at the high split (engine revision 2)
    bool primed = false, masterPrimed = false, sustain = false; int badBlocks = 0;
    float lastCar[kOps] {}, lastSub = 0, lastNz = 0, lastSmp = 0, lastDistDrive = 0, lastDistMix = 0, lastMbMix = 0, lastMaster = 1;
    float segDT = 0, dDrA = 0, dDrD = 0, dMxA = 1, dMxD = 0, mbMxA = 1, mbMxD = 0;   // per segment: ramp step and DIST / MULTIBAND ramps
    float modWheel = 0, bendSemis = 0;
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    std::array<Voice, kMaxVoices> voices;
    uint64_t ageCounter = 0;
    int heldStack[128] {}; int heldCount = 0;
    std::mt19937 rng;
    float lfoPhase = 0, lfoHold = 0, lfoValue = 0;
    // fx state
    juce::dsp::Reverb reverb;
    std::vector<float> delayL, delayR; int dPos = 0; float dLp[2] {};
    float dcX[2], dcY[2];
    float crushHold[2]; int crushCount = 0;
    float gateLevel = 1; long gateStep = -1; bool gateOn = true;
    std::shared_ptr<const SampleData> sample;
    std::atomic<const KeyMap*> keymap { nullptr };
    std::atomic<const ChopKit*> chopKit { nullptr };
    std::vector<float> ring; std::atomic<int> ringPos { 0 };
    std::vector<float> combBuf[2]; int combPos = 0; float combHz = 220, lastHz = 220;
    float seqVal = 0; double freeClock = 0;
    juce::dsp::LinkwitzRileyFilter<float> xLow[2], xHigh[2];
    float ottEnv[3] {}; juce::AudioBuffer<float> bands;
    // BASS update state
    std::unique_ptr<juce::dsp::Oversampling<float>> osSub;   // the clean sub's own 2x path (same filters = same phase as the main path)
    std::vector<float> subHost; float* subChan[1] { nullptr }; bool subThisBlock = false;
    std::vector<float> combStore; int combN = 0;
    std::vector<float> wobArr, lfo1Arr, lfo2Arr, driveArr;
    double lfoPh[2] {}; float lfoRnd[2][2] {}; long lfoCyc[2] { -1, -1 }; double wobFree = 0;
    juce::dsp::LinkwitzRileyFilter<float> mbX[4], mbAp; float mbLo = -1, mbHi = -1;   // mbAp: the low band's phase match at the high split
    float dsHold[2] {}; int dsCount = 0; float distLp[2] {};
    SVF2 lmA, lmB;

    // ---- macro helpers ----
    float haunt() const { return juce::jlimit(0.0f, 1.0f, P.macro[0] + modWheel * 0.6f); }
    float spectre() const { return P.macro[1]; }
    float possess() const { return P.macro[2]; }
    float drift() const { return P.macro[3]; }
    float veil() const { return P.macro[4]; }
    float curse() const { return P.macro[5]; }

    void handle(const juce::MidiMessage& m) {
        if (m.isNoteOn()) noteOn(m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff()) noteOff(m.getNoteNumber());
        else if (m.isPitchWheel()) bendSemis = (float) ((m.getPitchWheelValue() - 8192) / 8192.0) * P.bend;
        else if (m.isController() && m.getControllerNumber() == 1) modWheel = m.getControllerValue() / 127.0f;
        else if (m.isAllNotesOff() || m.isAllSoundOff()) { for (auto& v : voices) { releaseVoice(v); v.held = false; v.sustained = false; } heldCount = 0; sustain = false; }
        else if (m.isSustainPedalOn() && P.engVer >= 2) sustain = true;   // (the original engine ignored the pedal)
        else if (m.isSustainPedalOff() && P.engVer >= 2) {
            sustain = false;
            for (auto& v : voices) if (v.sustained) { v.sustained = false; if (!v.held && !v.chop && !v.key) releaseVoice(v); }
        }
    }
    void releaseVoice(Voice& v) { for (auto& e : v.opEnv) e.release(); v.fEnv.release(); v.smpEnv.release(); v.nzEnv.release(); v.subEnv.release(); }

    void setEnvs(Voice& v) {
        for (int o = 0; o < kOps; ++o) { v.opEnv[o].set(P.op[o].a, P.op[o].d, P.op[o].s, P.op[o].r, (float) osr); v.opEnv[o].setExtra(P.op[o].dly, P.op[o].hold, P.op[o].loop, (float) osr); }
        v.nzEnv.set(P.nzA, P.nzD, P.nzS, P.nzR, (float) osr);
        v.subEnv.set(P.subA, 0.1f, 1.0f, P.subR, (float) osr);
        v.fEnv.set(P.fa, P.fd, P.fs, P.fr, (float) osr);
        v.smpEnv.set(P.smpA, P.smpD, P.smpS, P.smpR, (float) osr);
    }
    bool opUsed(int o) const {
        if (P.op[o].level > 0.0001f) return true;
        for (int t = 0; t < kOps; ++t) if (t != o && P.mod[o][t] > 0.0001f) return true;
        return false;
    }
    bool noiseUsed() const {
        if (P.nzLevel > 0.0001f) return true;
        for (int t = 0; t < kOps; ++t) if (P.nzMod[t] > 0.0001f) return true;
        return false;
    }
    uint8_t usedMask() const {
        uint8_t m = 0x0F; for (int o = kLegacyOps; o < kOps; ++o) if (opUsed(o)) m = (uint8_t) (m | (1 << o)); if (noiseUsed()) m |= 0x40;
        if ((P.subOn || P.subSolo) && P.subEnv == 2) m |= 0x80;
        return m;
    }
    // BASS update: noise env, sub, filters, modulation for a (re)triggered note (never touches the shared rng)
    void startBass(Voice& v, bool legato) {
        v.useMask = usedMask();
        v.nzEnv.trigger(legato);
        v.subEnv.trigger(legato);
        v.sustained = false;
        if (legato) return;
        v.subPh = 0; v.subF.reset(); v.nzF.reset(); v.modInit = false; v.wobClock = 0;
        for (int f = 0; f < 2; ++f) if (P.flt[f].on) v.bf[f].reset(BassFilter::isComb(P.flt[f].type));   // only a comb needs its delay line cleared
        v.rnd = v.nz.white();
    }
    // start the sample ghost, bubbles and sync for a (re)triggered note
    void startExtras(Voice& v, int note, bool legato) {
        lastHz = 440.0f * std::exp2((note + 12 * P.octave - 69.0f) / 12.0f);
        if (legato) return;
        auto smp = std::atomic_load(&sample);
        if (smp && smp->size() > 0) {
            const int n = smp->size();
            v.slice = ((note - 36) % P.smpSlices + P.smpSlices) % P.smpSlices;
            const double a = P.smpStart * n, len = juce::jmax(64.0, P.smpLen * (n - a));
            v.spos = P.smpMode == 3 ? (double) v.slice / P.smpSlices * n : (P.smpRev ? a + len - 1 : a);
        }
        for (auto& g : v.grains) g.on = false;
        v.grainClock = 0; v.wtPhase = 0;
        if (P.smpLevel > 0.001f || P.smpFm > 0.001f) v.smpEnv.trigger(false);
        v.bubStepAt = -1; v.bubAmp = 0; v.bubClock = 0;
        for (auto& f : v.fph) f = 0;
        for (auto& f : v.vf) f.reset();
    }

    void noteOn(int note, float vel) {
        // remember held notes (for mono last-note priority)
        for (int i = 0; i < heldCount; ++i) if (heldStack[i] == note) { std::memmove(heldStack + i, heldStack + i + 1, sizeof(int) * (size_t) (heldCount - i - 1)); --heldCount; break; }
        if (heldCount < 128) heldStack[heldCount++] = note;
        const float playNote = (float) (note + 12 * P.octave);
        const KeyMap* km = keymap.load();
        if (km && km->keys[note & 127]) {   // a Sample Lab key: play its section at its own pitch and length
            Voice* pick = nullptr;
            for (auto& v : voices) if (!v.active()) { pick = &v; break; }
            if (!pick) { uint64_t best = UINT64_MAX; for (auto& v : voices) if (v.age < best) { best = v.age; pick = &v; } }
            Voice& v = *pick;
            for (auto& e : v.opEnv) { e.stage = Env::Idle; e.level = 0; }
            v.note = note; v.vel = vel; v.held = true; v.age = ++ageCounter; v.key = km->keys[note & 127]; v.kpos = 0; v.chop = nullptr;
            v.smpEnv.set(0.002f, 30.0f, 1.0f, 0.04f, (float) osr); v.smpEnv.trigger(false);
            return;
        }
        if (P.smpMode == 4)   // CHOP: each key plays its slice (rendered offline with its own pitch), one-shot or gated
            if (const ChopKit* ck = chopKit.load(); ck && ck->n > 0 && ck->slotOf[note & 127] >= 0) {
                const int si = ck->slotOf[note & 127]; const auto& sl = ck->slot[si];
                if (!sl.d || sl.len < 2) return;
                if (ck->choke)   // a new slice cuts the one before (6 ms fade)
                    for (auto& o : voices) if (o.chop && o.smpEnv.active()) { o.smpEnv.set(0.001f, 30.0f, 1.0f, 0.006f, (float) osr); o.smpEnv.release(); }
                Voice* pick = nullptr;
                for (auto& o : voices) if (!o.active()) { pick = &o; break; }
                if (!pick) { uint64_t best = UINT64_MAX; for (auto& o : voices) if (o.age < best) { best = o.age; pick = &o; } }
                Voice& v = *pick;
                for (auto& e : v.opEnv) { e.stage = Env::Idle; e.level = 0; }
                v.key = nullptr; v.chop = &sl; v.cslot = si; v.cpos = 0; v.cgate = ck->gate || sl.loop;
                v.cinc = ck->rate / osr * std::exp2((ck->octOf[note & 127] * 12.0 + P.smpPitch) / 12.0);
                v.cgain = 0.25f + 0.75f * vel;
                v.note = note; v.vel = vel; v.held = true; v.age = ++ageCounter;
                v.smpEnv.set(0.0008f, 30.0f, 1.0f, juce::jmax(0.01f, P.smpR), (float) osr); v.smpEnv.trigger(false);
                return;
            }
        if (P.mono) {
            Voice& v = voices[0];
            if (v.key || v.chop) { v.key = nullptr; v.chop = nullptr; v.smpEnv.stage = Env::Idle; }
            const bool legato = v.active() && v.held && !P.monoRetrig;   // RETRIG: every note restarts the envelopes
            v.note = note; v.vel = vel; v.held = true; v.age = ++ageCounter;
            v.targetNote = playNote;
            if (!v.active() || P.glide < 0.001f) v.curNote = playNote;
            setEnvs(v);
            for (auto& e : v.opEnv) e.trigger(legato);
            v.fEnv.trigger(legato);
            if (!legato) { v.penv = 1; resetPhases(v); v.filt.reset(); }
            startExtras(v, note, legato);
            startBass(v, legato);
            return;
        }
        const int limit = juce::jlimit(1, kMaxVoices, P.voices);
        Voice* pick = nullptr;
        for (int i = 0; i < limit; ++i) if (voices[(size_t) i].note == note && voices[(size_t) i].active()) { pick = &voices[(size_t) i]; break; }
        if (!pick) for (int i = 0; i < limit; ++i) if (!voices[(size_t) i].active()) { pick = &voices[(size_t) i]; break; }
        bool stolen = false;
        if (!pick) { // steal: oldest released first, else oldest
            uint64_t best = UINT64_MAX;
            for (int i = 0; i < limit; ++i) { auto& v = voices[(size_t) i]; uint64_t score = v.age + (v.held ? (1ull << 40) : 0); if (score < best) { best = score; pick = &v; } }
            stolen = pick != nullptr && pick->active();
        }
        // engine revision 2: the stolen note moves to a spare voice and fades out in 4 ms, the new one starts from silence
        if (stolen && P.engVer >= 2)
            for (int i = limit; i < kMaxVoices; ++i) {
                Voice& sp = voices[(size_t) i];
                if (sp.active()) continue;
                float* keep[2][2]; for (int f = 0; f < 2; ++f) for (int ch = 0; ch < 2; ++ch) keep[f][ch] = sp.bf[f].comb[ch];
                const uint32_t seed = sp.nz.s;
                sp = *pick;   // the whole voice (its filters' delay lines move with it: pointers swapped below)
                for (int f = 0; f < 2; ++f) for (int ch = 0; ch < 2; ++ch) pick->bf[f].comb[ch] = keep[f][ch];
                pick->nz.s = seed;
                sp.held = false; sp.sustained = false; sp.note = -1;
                for (auto& e : sp.opEnv) e.fastRelease(0.004f);
                for (auto* e : { &sp.smpEnv, &sp.nzEnv, &sp.subEnv }) e->fastRelease(0.004f);
                for (auto& e : pick->opEnv) { e.stage = Env::Idle; e.level = 0; }
                for (auto* e : { &pick->smpEnv, &pick->nzEnv, &pick->subEnv, &pick->fEnv }) { e->stage = Env::Idle; e->level = 0; }
                for (int f = 0; f < 2; ++f) pick->bf[f].reset(true);
                break;
            }
        Voice& v = *pick;
        v.key = nullptr; v.chop = nullptr;
        v.note = note; v.vel = vel; v.held = true; v.age = ++ageCounter;
        v.curNote = v.targetNote = playNote; v.penv = 1;
        setEnvs(v);
        for (auto& e : v.opEnv) e.trigger(false);
        v.fEnv.trigger(false);
        resetPhases(v); v.filt.reset();
        startExtras(v, note, false);
        startBass(v, false);
    }

    void resetPhases(Voice& v) {
        std::uniform_real_distribution<float> u(0.0f, 1.0f);
        for (int k = 0; k < kMaxUnison; ++k) {
            for (int o = 0; o < kLegacyOps; ++o) { v.phase[k][o] = k == 0 ? 0.0f : u(rng); v.prev[k][o] = v.prev2[k][o] = 0; }
            for (int o = kLegacyOps; o < kOps; ++o) { v.phase[k][o] = k == 0 ? 0.0f : 0.5f + 0.5f * v.nz.white(); v.prev[k][o] = v.prev2[k][o] = 0; }   // own random source: the shared one stays as before
        }
    }

    void noteOff(int note) {
        for (int i = 0; i < heldCount; ++i) if (heldStack[i] == note) { std::memmove(heldStack + i, heldStack + i + 1, sizeof(int) * (size_t) (heldCount - i - 1)); --heldCount; break; }
        for (auto& v : voices) if (v.chop && v.note == note && v.held) { v.held = false; if (v.cgate) v.smpEnv.release(); }   // CHOP: gate / loop slices stop on release
        if (P.mono) {
            Voice& v = voices[0];
            if (v.note != note || v.chop) return;
            if (heldCount > 0) {   // fall back to the previous held note (legato)
                const int back = heldStack[heldCount - 1];
                v.note = back; v.targetNote = (float) (back + 12 * P.octave);
                if (P.glide < 0.001f) v.curNote = v.targetNote;
                return;
            }
            v.held = false;
            if (sustain) { v.sustained = true; return; }   // the pedal keeps it (engine revision 2)
            releaseVoice(v);
            return;
        }
        for (auto& v : voices) if (v.note == note && v.held) {
            v.held = false;
            if (v.key) { if (!P.labOneShot) v.smpEnv.release(); continue; }   // one-shot keys play to the end
            if (v.chop) continue;   // handled above
            if (sustain) { v.sustained = true; continue; }
            for (auto& e : v.opEnv) e.release();
            v.fEnv.release(); v.smpEnv.release(); v.nzEnv.release(); v.subEnv.release();
        }
    }

    float vowG[3] { 1, 0.5f, 0.1f };

    // one sample of the Sample Ghost for voice v (classic / granular / wavetable / slice)
    float sampleTick(Voice& v, const SampleData& S, float noteNow, float scanOff) {
        const int n = S.size();
        const double rb = S.rate / osr;
        const double track = P.smpTrack ? std::exp2((noteNow - 60.0) / 12.0) : 1.0;
        const double pitch = std::exp2(P.smpPitch / 12.0);
        const double a = juce::jlimit(0.0, (double) n - 64, (double) P.smpStart * n);
        const double len = juce::jmax(64.0, (double) P.smpLen * (n - a));
        switch (P.smpMode) {
            case 1: {   // GRANULAR: overlapping windowed grains around the position, sprayed
                std::uniform_real_distribution<float> u(-1.0f, 1.0f);
                v.grainClock += P.smpDens / (float) osr;
                if (v.grainClock >= 1) {
                    v.grainClock -= 1;
                    for (auto& g : v.grains) if (!g.on) {
                        const double centre = a + juce::jlimit(0.0, 1.0, (double) P.smpPos + scanOff) * len;
                        g.pos = juce::jlimit(a, a + len - 1, centre + u(rng) * P.smpSpray * len * 0.25);
                        g.inc = rb * track * pitch * (P.smpRev ? -1 : 1);
                        g.len = juce::jmax(32, (int) (P.smpGrain * osr)); g.age = 0; g.on = true; break;
                    }
                }
                float out = 0;
                for (auto& g : v.grains) if (g.on) {
                    const float w = 0.5f - 0.5f * sineTable()((float) g.age / g.len + 0.25f);   // Hann window
                    out += S.at(g.pos) * w;
                    g.pos += g.inc; if (++g.age >= g.len || g.pos < 0 || g.pos >= n - 1) g.on = false;
                }
                return out / std::sqrt(juce::jmax(1.0f, P.smpDens * P.smpGrain));
            }
            case 2: {   // WAVETABLE: the sample cut into 64 single cycles, scanned by POSITION (+ drift)
                const float fpos = juce::jlimit(0.0f, 1.0f, P.smpPos + scanOff) * (SampleData::kFrames - 1);
                const int f0 = (int) fpos; const float t = fpos - (float) f0;
                v.wtPhase += (float) (440.0 * std::exp2((noteNow - 69.0) / 12.0) * pitch / osr);
                if (v.wtPhase >= 1) v.wtPhase -= std::floor(v.wtPhase);
                v.spos = (a + fpos / (SampleData::kFrames - 1) * len);
                const float x0 = S.frame(f0, v.wtPhase), x1 = S.frame(juce::jmin(f0 + 1, SampleData::kFrames - 1), v.wtPhase);
                return (x0 + (x1 - x0) * t) * 0.8f;
            }
            case 3: {   // SLICE: chopped across the keys, each key plays its slice
                const double sl = (double) n / P.smpSlices, s0 = v.slice * sl;
                const double inc = rb * pitch * (P.smpRev ? -1 : 1);
                if (v.spos < s0 || v.spos >= s0 + sl) return 0.0f;
                const float out = S.at(v.spos);
                v.spos += inc;
                const double edge = juce::jmin(v.spos - s0, s0 + sl - v.spos);   // tiny fades at the slice edges
                return out * (float) juce::jmin(1.0, edge / 64.0);
            }
            default: {  // CLASSIC: played back at the key's pitch, optional loop / reverse
                if (P.smpKeep && S.pitched.size() == 49) {   // KEEP TEMPO: higher keys = higher pitch, same length
                    const int off = juce::jlimit(-24, 24, (int) std::round((P.smpTrack ? noteNow - 60.0 : 0.0) + P.smpPitch));
                    const auto& B = S.pitched[(size_t) (off + 24)];
                    const double end = juce::jmin(a + len, (double) B.size() - 1);
                    const double inc2 = rb * (P.smpRev ? -1 : 1);
                    double lS = a, lE = end; loopRange(n, a, end, lS, lE);
                    if (v.spos < a || v.spos >= end) { if (!P.smpLoop) return 0.0f; v.spos = P.smpRev ? lE - 1 : lS; }
                    else if (P.smpLoop && (P.smpRev ? v.spos < lS : v.spos >= lE)) v.spos = P.smpRev ? lE - 1 : lS;
                    const int i0 = (int) v.spos; const float t = (float) (v.spos - i0);
                    const float out = B[(size_t) i0] + (B[(size_t) juce::jmin(i0 + 1, (int) B.size() - 1)] - B[(size_t) i0]) * t;
                    v.spos += inc2;
                    return out;
                }
                const double inc = rb * track * pitch * (P.smpRev ? -1 : 1);
                double lS = a, lE = a + len; loopRange(n, a, a + len, lS, lE);
                if (v.spos < a || v.spos >= a + len) {
                    if (!P.smpLoop) return 0.0f;
                    v.spos = P.smpRev ? lE - 1 : lS;
                } else if (P.smpLoop && (P.smpRev ? v.spos < lS : v.spos >= lE)) v.spos = P.smpRev ? lE - 1 : lS;   // the LOOP region (default: all of START..END)
                const float out = S.at(v.spos);
                v.spos += inc;
                return out;
            }
        }
    }

    // the Sample Ghost's LOOP region inside START..END (smp_lst / smp_lend as fractions; the defaults 0..1 = the whole window)
    void loopRange(int n, double a, double end, double& lS, double& lE) const {
        lS = juce::jmax(a, (double) P.smpLst * n); lE = juce::jmin(end, (double) P.smpLend * n);
        if (lE - lS < 64.0) { lS = a; lE = end; }
    }
    float lfoTick(int count) {
        double rate = P.lfoRate;
        if (P.lfoSync) rate = (bpm / 60.0) / lfoDivBeats(P.lfoDiv);
        float inc = (float) (rate / sr) * (float) count;
        if (P.lfoSync && hostPlaying) {   // lock the phase to the host's beat position
            const double cyc = ppq / lfoDivBeats(P.lfoDiv);
            lfoPhase = (float) (cyc - std::floor(cyc));
        } else {
            lfoPhase += inc;
        }
        if (lfoPhase >= 1) { lfoPhase -= std::floor(lfoPhase); std::uniform_real_distribution<float> u(-1.0f, 1.0f); lfoHold = u(rng); }
        float ph = lfoPhase;
        switch (P.lfoShape) {
            case 0: return sineTable()(ph);
            case 1: return ph < 0.5f ? 4 * ph - 1 : 3 - 4 * ph;
            case 2: return 1 - 2 * ph;
            case 3: return ph < 0.5f ? 1.0f : -1.0f;
            default: return lfoHold;
        }
    }

    // ---- BASS update helpers ------------------------------------------------------------------------------
    NoiseGen noiseRng;   // the LFOs' random shapes (kept apart from the shared rng so old sounds stay the same)
    // the wobble's points, sorted, for this block (the shape is evaluated exactly: sharp steps stay sharp)
    float wobXs[kWobPts] {}, wobYs[kWobPts] {}; int wobNum = 2; float wobSm = 1;
    void buildWobTable() { wobblePoints(P, wobXs, wobYs, wobNum, wobSm); }
public:
    static void wobblePoints(const Snap& S, float* xs, float* ys, int& n, float& sm) {
        n = juce::jlimit(2, kWobPts, S.wobN);
        for (int i = 0; i < n; ++i) { xs[i] = i == 0 ? 0.0f : juce::jlimit(0.0f, 0.9999f, S.wobX[i]); ys[i] = juce::jlimit(0.0f, 1.0f, S.wobY[i]); }
        for (int i = 2; i < n; ++i) for (int j = i; j > 1 && xs[j] < xs[j - 1]; --j) { std::swap(xs[j], xs[j - 1]); std::swap(ys[j], ys[j - 1]); }
        sm = juce::jlimit(0.0f, 1.0f, S.wobSmooth);
    }
    static float wobbleShape(const float* xs, const float* ys, int n, float sm, double phd) {
        float ph = (float) (phd - std::floor(phd));
        int seg = 0; while (seg + 1 < n && ph >= xs[seg + 1]) ++seg;
        const float x0 = xs[seg], y0 = ys[seg];
        const float x1 = seg + 1 < n ? xs[seg + 1] : 1.0f, y1 = seg + 1 < n ? ys[seg + 1] : ys[0];
        float t = x1 - x0 > 1e-7f ? juce::jlimit(0.0f, 1.0f, (ph - x0) / (x1 - x0)) : 0.0f;
        if (sm > 0.0f) { const float ts = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::pi * t); t += (ts - t) * sm; }
        return y0 + (y1 - y0) * t;
    }
    // the wobble shape at a phase (0..1)
    float wobbleAt(double ph) const { return wobbleShape(wobXs, wobYs, wobNum, wobSm, ph); }
private:
    float lfoShapeVal(int l, double ph, long cyc) {
        if (cyc != lfoCyc[l]) { lfoCyc[l] = cyc; lfoRnd[l][0] = lfoRnd[l][1]; lfoRnd[l][1] = noiseRng.white(); }
        const float p = (float) ph;
        switch (P.lfo[l].shape) {
            case 0: return sineTable()(p);
            case 1: return p < 0.5f ? 4 * p - 1 : 3 - 4 * p;
            case 2: return 2 * p - 1;
            case 3: return 1 - 2 * p;
            case 4: return p < 0.5f ? 1.0f : -1.0f;
            case 5: return lfoRnd[l][1];
            default: { const float t = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::pi * p); return lfoRnd[l][0] + (lfoRnd[l][1] - lfoRnd[l][0]) * t; }
        }
    }
    // wobble / LFO values at every control step (8 host samples) of a segment, locked to the host beat when it plays
    void fillControl(int start, int count, int nCtl, bool needWob, bool needL1, bool needL2) {
        const double bps = bpm / 60.0;
        for (int c = 0; c < nCtl; ++c) {
            const double hs = start + c * 8.0;
            const double beats = hostPlaying ? ppq + hs / sr * bps : wobFree + c * 8.0 / sr * bps;
            if (needWob) wobArr[(size_t) c] = wobbleAt(beats / wobDivBeats(P.wobDiv));
            for (int l = 0; l < 2; ++l) {
                if (!(l == 0 ? needL1 : needL2)) continue;
                const double cyc = P.lfo[l].sync ? beats / wobDivBeats(P.lfo[l].div) : lfoPh[l] + c * 8.0 * P.lfo[l].rate / sr;
                const double fl = std::floor(cyc);
                (l == 0 ? lfo1Arr : lfo2Arr)[(size_t) c] = lfoShapeVal(l, cyc - fl, (long) fl);
            }
        }
        wobFree += count / sr * bps;
        for (int l = 0; l < 2; ++l) { lfoPh[l] += count * (double) P.lfo[l].rate / sr; if (lfoPh[l] > 1.0e6) lfoPh[l] -= 1.0e6; }
    }
    struct ModOut { float cut[2] {}, res[2] {}, vowel = 0, fm = 0, lvl[kOps] {}, lvlAll = 0, pitch = 0, noise = 0, sub = 0, bend = 0, mainCut = 0, vol = 1; };
    float srcVal(int src, const Voice& v, int c, float wobV) const {
        switch (src) {
            case 1: return wobV;
            case 2: return lfo1Arr[(size_t) c];
            case 3: return lfo2Arr[(size_t) c];
            case 4: return lfoValue;
            case 5: return v.fEnv.level;
            case 6: return v.opEnv[0].level;
            case 7: return v.vel;
            case 8: return modWheel;
            case 9: return (v.targetNote - 60.0f) / 24.0f;
            case 10: return v.rnd;
            default: return 0.0f;
        }
    }
    void evalMods(const Voice& v, int c, float wobV, ModOut& m) const {
        for (int k = 0; k <= kModSlots; ++k) {   // the 8 matrix slots + the WOBBLE page's own slot
            const auto& S = P.mm[k];
            if (S.src <= 0 || S.dst <= 0 || S.amt == 0.0f) continue;
            const float x = S.amt * srcVal(S.src, v, c, wobV);
            switch (S.dst) {
                case mdF1Cut: m.cut[0] += x * 6; break;
                case mdF2Cut: m.cut[1] += x * 6; break;
                case mdBothCut: m.cut[0] += x * 6; m.cut[1] += x * 6; break;
                case mdF1Res: m.res[0] += x; break;
                case mdF2Res: m.res[1] += x; break;
                case mdVowel: m.vowel += x; break;
                case mdFm: m.fm += x; break;
                case mdLvlAll: m.lvlAll += x; break;
                case mdPitch: m.pitch += x * 24; break;
                case mdNoise: m.noise += x; break;
                case mdSub: m.sub += x; break;
                case mdBend: m.bend += x; break;
                case mdMainCut: m.mainCut += x * 6; break;
                case mdVolume: { const float sv = srcVal(S.src, v, c, wobV); m.vol *= S.amt >= 0 ? 1.0f - S.amt + S.amt * sv : 1.0f + S.amt * sv; break; }   // DEPTH 1 = the shape is the volume
                default: if (S.dst >= mdLvl1 && S.dst <= mdLvl6) m.lvl[S.dst - mdLvl1] += x; break;
            }
        }
    }

    void renderSegment(juce::AudioBuffer<float>& out, int start, int count) {
        // drift LFO updates per segment chunk of 32 host samples for smoothness
        juce::dsp::AudioBlock<float> hostBlock(out.getArrayOfWritePointers(), 2, (size_t) start, (size_t) count);
        auto up = os->processSamplesUp(hostBlock);   // gives an oversampled block; we overwrite it with the synth
        up.clear();
        const int on = (int) up.getNumSamples();
        float* L = up.getChannelPointer(0); float* R = up.getChannelPointer(1);

        // per-block constants
        const float h = haunt();
        const float modScale = 0.15f + h * h * 2.6f + h * 0.4f;
        const float s = spectre();
        const int uni = 1 + (int) std::round(s * (kMaxUnison - 1));
        const float detuneCents = s * s * 38.0f;
        const float d = drift();
        lfoValue = lfoTick(count);
        const float lfo = lfoValue;
        const float lfoMod = juce::jmax(0.0f, 1.0f + lfo * d * 0.95f);
        const float vib = lfo * d * d * 0.35f;
        const float fbBoost = h * 0.35f;
        const float glideCoef = P.glide < 0.001f ? 1.0f : 1.0f - std::exp(-6.9f / (float) (P.glide * osr));
        const float penvCoef = std::exp(-6.9f / juce::jmax(1.0f, P.penvDec * (float) osr));
        const float norm = 1.0f / std::sqrt((float) uni);
        float detune[kMaxUnison], panL[kMaxUnison], panR[kMaxUnison];
        for (int k = 0; k < uni; ++k) {
            const float t = uni == 1 ? 0.0f : (float) k / (float) (uni - 1) * 2.0f - 1.0f;
            detune[k] = std::pow(2.0f, t * detuneCents / 1200.0f);
            const float pan = t * s;
            panL[k] = std::sqrt(0.5f * (1 - pan)); panR[k] = std::sqrt(0.5f * (1 + pan));
        }
        float ratio[kOps], mIdx[kOps][kOps], fb[kOps], carrier[kOps];
        for (int o = 0; o < kOps; ++o) {
            ratio[o] = P.op[o].ratio * std::pow(2.0f, P.op[o].fine / 1200.0f);
            fb[o] = juce::jlimit(0.0f, 1.0f, P.op[o].fb + (P.op[o].fb > 0.01f ? fbBoost : 0.0f)) * 0.35f;
            carrier[o] = P.op[o].level;
            for (int t = 0; t < kOps; ++t) mIdx[o][t] = P.mod[o][t] * P.mod[o][t] * modScale * lfoMod;
        }
        float meterAcc[kOps + 1] {};
        // level ramps across the segment (engine revision 2): no steps when OUT / SUB / NOISE / SAMPLE levels move
        const bool ramp = P.engVer >= 2;
        if (!primed) { for (int o = 0; o < kOps; ++o) lastCar[o] = P.op[o].level; lastSub = P.subLevel; lastNz = P.nzLevel; lastSmp = P.smpLevel;
                       lastDistDrive = P.distDrive; lastDistMix = P.distMix; lastMbMix = P.mbMix; primed = true; }
        float carA[kOps], carD[kOps];
        for (int o = 0; o < kOps; ++o) { carA[o] = ramp ? lastCar[o] : carrier[o]; carD[o] = carrier[o] - carA[o]; lastCar[o] = carrier[o]; }
        const float subA = ramp ? lastSub : P.subLevel, subD = P.subLevel - subA; lastSub = P.subLevel;
        const float nzLA = ramp ? lastNz : P.nzLevel, nzLD = P.nzLevel - nzLA; lastNz = P.nzLevel;
        const float smpA = ramp ? lastSmp : P.smpLevel, smpD = P.smpLevel - smpA; lastSmp = P.smpLevel;
        segDT = 1.0f / (float) juce::jmax(1, on);
        dDrA = ramp ? lastDistDrive : P.distDrive; dDrD = P.distDrive - dDrA; lastDistDrive = P.distDrive;
        dMxA = ramp ? lastDistMix : P.distMix; dMxD = P.distMix - dMxA; lastDistMix = P.distMix;
        mbMxA = ramp ? lastMbMix : P.mbMix; mbMxD = P.mbMix - mbMxA; lastMbMix = P.mbMix;

        // RHYTHM sequencer: one value per step, synced to the host beat (or free-running when stopped)
        const float seqPrev = seqVal;
        float fltOff = 0, vowOff = 0, pitchOff = 0, gateA = 1, gateB = 1;
        if (P.seqOn) {
            const double stepBeats = lfoDivBeats(P.seqDiv);
            const long step = hostPlaying ? (long) std::floor(ppq / stepBeats) : (long) std::floor(freeClock / (stepBeats * 60.0 / bpm * sr));
            const int idx = (int) (((step % P.seqSteps) + P.seqSteps) % P.seqSteps);
            seqStepNow.store(idx);
            const float target = P.seq[idx];
            const float k = P.seqGlide < 0.01f ? 1.0f : 1.0f - std::exp(-(float) count / (P.seqGlide * 0.12f * (float) sr));
            seqVal += (target - seqVal) * k;
            const float a = P.seqAmt;
            switch (P.seqDest) {
                case 0: gateA = 1 - a + a * seqPrev; gateB = 1 - a + a * seqVal; break;
                case 1: fltOff = (seqVal - 0.5f) * a * 8.0f; break;
                case 2: for (int o = 0; o < kOps; ++o) for (int t = 0; t < kOps; ++t) mIdx[o][t] *= (1 - a + a * seqVal * 1.8f); break;
                case 3: vowOff = (seqVal - 0.5f) * a; break;
                default: pitchOff = std::round((seqVal - 0.5f) * a * 24.0f); break;
            }
        } else seqStepNow.store(-1);
        freeClock += count;

        // ---- BASS update: what is on this block
        const uint8_t blockMask = usedMask();
        int act[kOps], nAct = 0;                     // the ghosts that need computing (heard or haunting)
        for (int o = 0; o < kOps; ++o) {
            bool use = P.op[o].level > 0.0f;
            for (int t = 0; t < kOps && !use; ++t) use = t != o && P.mod[o][t] > 0.0f;
            if (use) act[nAct++] = o;
        }
        int srcN[kOps] {}, srcF[kOps][kOps]; float srcI[kOps][kOps];   // per ghost: who haunts it (ascending order, as before)
        for (int a = 0; a < nAct; ++a) { const int o = act[a]; for (int b = 0; b < nAct; ++b) { const int f = act[b]; if (f != o && mIdx[f][o] != 0.0f) { srcF[o][srcN[o]] = f; srcI[o][srcN[o]++] = mIdx[f][o]; } } }
        const bool useNoise = (blockMask & 0x40) != 0;
        float nzIdx[kOps] {}; bool nzHaunts = false;
        if (useNoise) for (int t = 0; t < kOps; ++t) { nzIdx[t] = P.nzMod[t] * P.nzMod[t] * modScale * lfoMod; nzHaunts = nzHaunts || nzIdx[t] != 0.0f; }
        const bool useTables = [&] { for (int a = 0; a < nAct; ++a) if (P.op[act[a]].wtype > 0) return true; return false; }();
        const bool fltOn[2] = { P.flt[0].on, P.flt[1].on };
        const bool anyFlt = fltOn[0] || fltOn[1];
        const bool split = anyFlt && P.fltRoute == 2;
        bool anyMod = false, modWob = false, modL1 = false, modL2 = false, modMainCut = false, modDrive = false;
        for (auto& S : P.mm) if (S.src > 0 && S.dst > 0 && S.amt != 0.0f) {
            if (S.dst == mdDrive) modDrive = true; else anyMod = true;
            if (S.dst == mdMainCut) modMainCut = true;
            modWob = modWob || S.src == 1; modL1 = modL1 || S.src == 2; modL2 = modL2 || S.src == 3;
        }
        const int nCtl = (on + ctlLen - 1) / ctlLen + 1;
        // the WOBBLE page's playhead: whenever something uses the wobble, or the page is open
        if (modWob || wobView.load(std::memory_order_relaxed)) {
            double ph = -1;
            if (P.wobRetrig) { for (auto& v : voices) if (v.active() && !v.key && !v.chop) { ph = v.wobClock * (bpm / 60.0 / osr) / wobDivBeats(P.wobDiv); break; } }
            else ph = (hostPlaying ? ppq + start / sr * (bpm / 60.0) : wobFree) / wobDivBeats(P.wobDiv);
            wobUi.store(ph < 0 ? -1.0f : (float) (ph - std::floor(ph)));
        } else wobUi.store(-1.0f);
        if (anyMod || modDrive) {
            if (modWob) buildWobTable();
            fillControl(start, count, nCtl, modWob && !P.wobRetrig, modL1, modL2);
            if (modDrive)
                for (int c = 0; c < nCtl; ++c) {
                    float x = 0;
                    for (auto& S : P.mm) if (S.dst == mdDrive && S.src > 0 && S.amt != 0.0f) {
                        const float sv = S.src == 1 ? (P.wobRetrig ? 0.0f : wobArr[(size_t) c]) : S.src == 2 ? lfo1Arr[(size_t) c] : S.src == 3 ? lfo2Arr[(size_t) c] : S.src == 4 ? lfoValue : S.src == 8 ? modWheel : 0.0f;
                        x += S.amt * sv;
                    }
                    driveArr[(size_t) c] = x;
                }
        } else { wobFree += count / sr * bpm / 60.0; }
        const bool subOn = P.subOn || P.subSolo;
        const bool subClean = P.subClean || P.subSolo;
        float* subOs = nullptr; juce::dsp::AudioBlock<float> subBlock;
        if (subOn && subClean) {
            subChan[0] = subHost.data();   // a member: the block keeps a pointer to this channel array
            subBlock = juce::dsp::AudioBlock<float>(subChan, 1, (size_t) start, (size_t) count);
            auto su = osSub->processSamplesUp(subBlock); su.clear(); subOs = su.getChannelPointer(0); subThisBlock = true;
        }
        const double wobBeatsOsr = bpm / 60.0 / osr;   // beats per voice sample (retriggered wobble)
        const float normCut[2] = { std::log2(juce::jmax(20.0f, P.flt[0].cut) / 20.0f) / 9.9658f, std::log2(juce::jmax(20.0f, P.flt[1].cut) / 20.0f) / 9.9658f };
        const auto& WB = waveBank();

        auto smp = std::atomic_load(&sample);
        const SampleData* S = (smp && smp->size() > 64) ? smp.get() : nullptr;
        const bool useSmp = S && (P.smpLevel > 0.001f || P.smpFm > 0.001f);
        const float scanOff = lfo * P.smpScan * 0.5f;
        const int smpFmOp = juce::jlimit(0, kOps - 1, P.smpFmTo);
        // ADD SAMPLE "AS WAVE": that ghost reads the sample's single-cycle frames (built with every sample); -1 = no ghost does
        const bool smpFramesOk = S && S->frames.size() == (size_t) SampleData::kFrames * SampleData::kFrameLen;
        const int smpWaveOp = smpFramesOk && P.smpWaveTo > 0 ? juce::jmin(kOps, P.smpWaveTo) - 1 : -1;
        const float smpFpos = juce::jlimit(0.0f, 1.0f, P.smpStart + scanOff) * (SampleData::kFrames - 1);
        const int smpF0 = (int) smpFpos, smpF1 = juce::jmin(smpF0 + 1, SampleData::kFrames - 1); const float smpFt = smpFpos - (float) smpF0;
        const float syncMul = 1.0f + P.syncAmt * 7.0f;
        const bool useSync = P.syncAmt > 0.001f;
        const bool blepSync = useSync && P.engVer >= 2;
        const bool useVow = P.vowAmt > 0.001f;
        const bool useBub = P.bubAmt > 0.001f;
        const float bubStep = (float) (lfoDivBeats(P.bubDiv) * 60.0 / bpm * osr);
        const float bubSweepK = std::exp(-1.0f / (0.012f * (1 + P.bubTone * 2) * (float) osr));
        const float bubAmpK = std::exp(-1.0f / ((0.05f + P.bubTone * 0.08f) * (float) osr));
        static const float FORM[5][3] = { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 300, 2300, 3000 }, { 450, 800, 2830 }, { 325, 700, 2530 } };
        static const float FGAIN[5][3] = { { 1, 0.50f, 0.10f }, { 1, 0.40f, 0.20f }, { 1, 0.30f, 0.20f }, { 1, 0.35f, 0.08f }, { 1, 0.25f, 0.06f } };
        float playheadUi = -1; int chopUi = -1;

        for (auto& v : voices) {
            if (!v.active()) continue;
            if (v.key) {   // Sample Lab key voice
                const SampleData& K = *v.key; const double inc = K.rate / osr; const float g = 0.45f * (0.4f + 0.6f * v.vel);
                for (int i = 0; i < on; ++i) {
                    if (v.kpos >= K.size() - 1) { v.smpEnv.stage = Env::Idle; v.smpEnv.level = 0; break; }
                    const float x = K.at(v.kpos) * v.smpEnv.next() * g; v.kpos += inc;
                    L[i] += x; R[i] += x;
                }
                continue;
            }
            if (v.chop) {   // CHOP slice voice
                const ChopKit::Slot& C = *v.chop; const float g = v.cgain * P.smpLevel * 0.9f;
                const float mute = P.muteSample ? 0.0f : 1.0f;
                for (int i = 0; i < on; ++i) {
                    if (v.cpos >= C.len - 1) {
                        if (C.loop && v.held) v.cpos -= (double) (C.len - 1);
                        else { v.smpEnv.stage = Env::Idle; v.smpEnv.level = 0; break; }
                    }
                    const int ip = (int) v.cpos; const float fr = (float) (v.cpos - ip);
                    v.smpGain += (mute - v.smpGain) * 0.002f;
                    const float x = (C.d[ip] + (C.d[ip + 1] - C.d[ip]) * fr) * v.smpEnv.next() * g * v.smpGain; v.cpos += v.cinc;
                    L[i] += x; R[i] += x;
                }
                if (v.smpEnv.active()) chopUi = v.cslot;
                continue;
            }
            v.useMask = (uint8_t) (v.useMask | blockMask);
            float velAmt[kOps];
            for (int o = 0; o < kOps; ++o) velAmt[o] = 1 - P.op[o].vel * (1 - v.vel);
            const float keyOct = (v.targetNote - 60.0f) / 12.0f * P.fkey;
            const float* tbl[kOps] {}; float bend[kOps] {}, bendH[kOps] {};
            for (int i = 0; i < on; ++i) {
                v.curNote += (v.targetNote - v.curNote) * glideCoef;
                v.penv *= penvCoef;
                float noteNow = v.curNote + bendSemis + v.penv * P.penvAmt + vib + pitchOff;
                if (anyMod) noteNow += v.mc[7];
                const float freq = 440.0f * std::exp2((noteNow - 69.0f) / 12.0f);
                const float baseInc = freq / (float) osr;
                float env[kOps];
                for (int o = 0; o < kOps; ++o) env[o] = v.opEnv[o].next() * velAmt[o];
                const float fe = v.fEnv.next();
                if ((i & ctlMask) == 0) {
                    ModOut mo;
                    if (anyMod) {
                        const int c = i >> ctlShift;
                        float wobV = 0;
                        if (modWob) wobV = P.wobRetrig ? wobbleAt(v.wobClock * wobBeatsOsr / wobDivBeats(P.wobDiv)) : wobArr[(size_t) c];
                        evalMods(v, c, wobV, mo);
                        float tgt[Voice::kMC];
                        tgt[0] = juce::jmax(0.0f, 1.0f + 2.0f * mo.fm);
                        for (int o = 0; o < kOps; ++o) tgt[1 + o] = juce::jmax(0.0f, 1.0f + mo.lvl[o] + mo.lvlAll);
                        tgt[7] = mo.pitch; tgt[8] = juce::jmax(0.0f, 1.0f + mo.noise); tgt[9] = juce::jmax(0.0f, 1.0f + mo.sub); tgt[10] = juce::jmax(0.0f, mo.vol);
                        if (!v.modInit) { for (int j = 0; j < Voice::kMC; ++j) { v.mc[j] = tgt[j]; v.mi[j] = 0; } v.modInit = true; }
                        else for (int j = 0; j < Voice::kMC; ++j) v.mi[j] = (tgt[j] - v.mc[j]) * ctlInv;
                    }
                    float ce = P.fenv * 6.0f * fe + keyOct + lfo * d * 1.6f + fltOff;
                    if (modMainCut) ce += mo.mainCut;
                    const float c = P.cut * std::exp2(juce::jlimit(-10.0f, 10.0f, ce));
                    v.filt.setup(c, P.res, (float) osr);
                    if (useVow) {   // TALK: three formant band-passes sliding a-e-i-o-u
                        const float vp = juce::jlimit(0.0f, 1.0f, P.vowPos + lfo * P.vowMod * 0.5f + vowOff + modWheel * 0.3f + mo.vowel) * 3.999f;
                        const int a = (int) vp; const float t = vp - (float) a;
                        for (int j = 0; j < 3; ++j) v.vf[j].setup(FORM[a][j] + (FORM[a + 1][j] - FORM[a][j]) * t, 0.78f + 0.19f * P.vowRes, (float) osr);
                        for (int j = 0; j < 3; ++j) vowG[j] = FGAIN[a][j] + (FGAIN[a + 1][j] - FGAIN[a][j]) * t;
                    }
                    // the two new filters
                    for (int f = 0; f < 2; ++f) if (fltOn[f]) {
                        const auto& F = P.flt[f];
                        const float oct = F.env * 6.0f * fe + (v.targetNote - 60.0f) / 12.0f * F.key + mo.cut[f];
                        v.bf[f].setup(F.type, F.cut * std::exp2(juce::jlimit(-10.0f, 10.0f, oct)), juce::jlimit(0.0f, 1.0f, F.res + mo.res[f]), F.drive,
                                      normCut[f] + oct * 0.1f + mo.vowel + vowOff, (float) osr);
                    }
                    if (useTables)
                        for (int a = 0; a < nAct; ++a) {
                            const int o = act[a]; const int w = P.op[o].wtype;
                            if (w <= 0) { tbl[o] = nullptr; continue; }
                            bend[o] = juce::jlimit(0.0f, 1.0f, P.op[o].wave / 3.0f + mo.bend);
                            const float incMax = baseInc * ratio[o] * (1.0f + detuneCents * 0.0006f) * (useSync && o == 0 ? syncMul : 1.0f);
                            // BEND squeezes half the cycle into k of it: that half plays up to 0.5/k times faster, so drop that many octaves of harmonics
                            int extra = 0;
                            if (bend[o] > 0.001f) { const float k = 0.5f * (1.0f - 0.92f * bend[o]); extra = juce::jmax(0, (int) std::ceil(std::log2(0.5f / k) - 1.0e-3f)); }
                            bendH[o] = 1.5f * incMax;   // round the bend's corners over ~1.5 samples
                            // FM bends a table's phase: its harmonics momentarily run up to (1 + 2 pi x depth x ratio) times faster.
                            // Pick the table for that peak speed, so the FM'd harmonics stay under the voice Nyquist instead of folding
                            // back as fizz. Above 2x the table also keeps no more than the 2x content (up to the host rate).
                            float dev = 0;
                            for (int j = 0; j < srcN[o]; ++j) { const int f = srcF[o][j]; dev += srcI[o][j] * ratio[f] * v.opEnv[f].level * velAmt[f] * (anyMod ? v.mc[1 + f] : 1.0f); }
                            dev *= (anyMod ? v.mc[0] : 1.0f) / juce::jmax(0.01f, ratio[o]);
                            const float speed = juce::jmax((float) (osF / 2), 1.0f + juce::MathConstants<float>::twoPi * dev);
                            const float* nt = WB.table(juce::jmin(w, WaveBank::kWaves) - 1, juce::jmin(WaveBank::kLevels - 1, WaveBank::levelFor(incMax * speed) + extra));
                            if (nt != v.tblCur[o]) { v.tblOld[o] = v.tblCur[o] ? v.tblCur[o] : nt; v.tblCur[o] = nt; v.tblX[o] = 1.0f; }   // crossfade table changes over one control step
                            tbl[o] = nt;
                        }
                    if (subOn) v.subF.setupK(P.subCut, 1.414f, (float) osr);
                    if (useNoise) v.nzF.setup(P.nzCut, P.nzRes, (float) osr);
                }
                if (anyMod) for (int j = 0; j < Voice::kMC; ++j) v.mc[j] += v.mi[j];
                if (useTables) for (int a = 0; a < nAct; ++a) { const int o = act[a]; if (v.tblX[o] > 0.0f) v.tblX[o] = juce::jmax(0.0f, v.tblX[o] - ctlInv); }
                // SAMPLE GHOST
                float smpOut = 0;
                if (useSmp) smpOut = sampleTick(v, *S, noteNow, scanOff) * v.smpEnv.next();
                else if (v.smpEnv.active()) v.smpEnv.next();
                // NOISE GHOST
                float nzSig = 0;
                if (useNoise) {
                    float n = v.nz.tick(P.nzType);
                    if (P.nzCut < 19990.0f || P.nzFtype != 0) n = v.nzF.tick(0, n, P.nzFtype);
                    nzSig = n * v.nzEnv.next();
                }
                const float fmMul = v.mc[0];
                const float rt = (float) (i + 1) * segDT;   // ramp position in this segment
                const float env0 = env[0];
                float car[kOps];
                for (int a = 0; a < nAct; ++a) { const int o = act[a]; car[o] = carA[o] + carD[o] * rt; }
                // a level mod scales the ghost itself, so a modulator's FM depth follows it too (relative: it scales OUT)
                if (anyMod) for (int a = 0; a < nAct; ++a) { const int o = act[a]; env[o] *= v.mc[1 + o]; }
                float sl = 0, sr2 = 0, slB = 0, srB = 0;
                for (int k = 0; k < uni; ++k) {
                    float outs[kOps];
                    bool syncEdge = false; float syncD = 0, syncOld = 0, syncPh = 0;
                    if (useSync) {   // ghost 1 hard-syncs to the note: it restarts every cycle of the played pitch
                        const float mInc = baseInc * detune[k];
                        v.fph[k] += mInc;
                        if (v.fph[k] >= 1) {
                            v.fph[k] -= 1;
                            if (blepSync) {   // revision 2: restart at the exact sub-sample point (and smooth the jump below)
                                const float sInc = baseInc * ratio[0] * detune[k] * syncMul;
                                syncEdge = true; syncD = juce::jlimit(0.0f, 1.0f, v.fph[k] / juce::jmax(1.0e-9f, mInc));
                                syncPh = v.phase[k][0] + sInc; syncPh -= std::floor(syncPh);   // where the wave would have gone on
                                v.phase[k][0] = syncD * sInc - sInc;
                            } else v.phase[k][0] = 0;
                        }
                    }
                    for (int a = 0; a < nAct; ++a) {
                        const int o = act[a];
                        float pm = fb[o] * 0.5f * (v.prev[k][o] + v.prev2[k][o]);
                        if (anyMod) { float m = 0; for (int j = 0; j < srcN[o]; ++j) m += srcI[o][j] * v.prev[k][srcF[o][j]]; pm += m * fmMul; }
                        else for (int j = 0; j < srcN[o]; ++j) pm += srcI[o][j] * v.prev[k][srcF[o][j]];
                        if (o == smpFmOp) pm += P.smpFm * smpOut * 1.5f;   // the sample haunts its ghost (ghost 1 unless ADD SAMPLE picks another)
                        if (nzHaunts) pm += nzIdx[o] * nzSig * (anyMod ? fmMul : 1.0f);
                        v.phase[k][o] += baseInc * ratio[o] * detune[k] * (o == 0 && useSync ? syncMul : 1.0f);
                        if (v.phase[k][o] >= 1) v.phase[k][o] -= std::floor(v.phase[k][o]);
                        if (o == smpWaveOp) {   // ADD SAMPLE "AS WAVE": one cycle of the sample (at START, + DRIFT x SCAN) is this ghost's waveform
                            float ph = v.phase[k][o] + pm; ph -= std::floor(ph);
                            outs[o] = (S->frame(smpF0, ph) * (1.0f - smpFt) + S->frame(smpF1, ph) * smpFt) * env[o];
                        } else if (tbl[o]) {
                            const float bp = bendPhase(v.phase[k][o] + pm, bend[o], bendH[o]);
                            float y = WaveBank::lookup(tbl[o], bp);
                            if (v.tblX[o] > 0.0f) y += (WaveBank::lookup(v.tblOld[o], bp) - y) * v.tblX[o];
                            outs[o] = y * env[o];
                        } else outs[o] = waveAt(v.phase[k][o] + pm, P.op[o].wave) * env[o];
                        if (o == 0 && syncEdge) syncOld = (tbl[0] ? WaveBank::lookup(tbl[0], bendPhase(syncPh + pm, bend[0], bendH[0])) : waveAt(syncPh + pm, P.op[0].wave)) * env[0];
                    }
                    float mono = 0, monoB = 0;
                    for (int a = 0; a < nAct; ++a) {
                        const int o = act[a];
                        v.prev2[k][o] = v.prev[k][o]; v.prev[k][o] = outs[o];
                        float heard = outs[o];
                        if (o == 0 && blepSync) {   // band-limited sync: a 2-point polyBLEP on the reset step (ghost 1 is heard one sample late)
                            float y = outs[0];
                            if (syncEdge) { const float h = outs[0] - syncOld; v.syncHold[k] += 0.5f * h * syncD * syncD; y -= 0.5f * h * (1.0f - syncD) * (1.0f - syncD); }
                            heard = v.syncHold[k]; v.syncHold[k] = y;
                        }
                        if (split && P.op[o].f2) monoB += heard * car[o]; else mono += heard * car[o];
                    }
                    sl += mono * panL[k]; sr2 += mono * panR[k];
                    if (split) { slB += monoB * panL[k]; srB += monoB * panR[k]; }
                }
                v.ghostGain += ((P.muteGhosts ? 0.0f : 1.0f) - v.ghostGain) * 0.002f; v.smpGain += ((P.muteSample ? 0.0f : 1.0f) - v.smpGain) * 0.002f;   // click-free mutes
                sl *= norm * v.ghostGain; sr2 *= norm * v.ghostGain;
                if (split) { slB *= norm * v.ghostGain; srB *= norm * v.ghostGain; }
                const float smpLvl = smpA + smpD * rt;
                sl += smpOut * smpLvl * 0.9f * v.smpGain; sr2 += smpOut * smpLvl * 0.9f * v.smpGain;
                if (useBub) {   // BUBBLES: pitch-dropping blips on every step
                    if (v.bubStepAt < 0 || (v.bubClock += 1) >= bubStep) {
                        static const float OFF[] = { 0, 7, 12, 19, 24, 31 };
                        std::uniform_real_distribution<float> u(0.0f, 1.0f);
                        v.bubClock = 0; v.bubStepAt = 1;
                        v.bubNote = v.targetNote + 12 + OFF[(int) (u(rng) * 5.99f)];
                        v.bubSweep = P.bubPitch; v.bubAmp = 1; v.bubPan = (u(rng) * 2 - 1) * 0.7f;
                    }
                    v.bubSweep *= bubSweepK; v.bubAmp *= bubAmpK;
                    v.bubPhase += 440.0f * std::exp2((v.bubNote + v.bubSweep - 69.0f) / 12.0f) / (float) osr;
                    if (v.bubPhase >= 1) v.bubPhase -= std::floor(v.bubPhase);
                    const auto& T = sineTable();
                    const float b = T(v.bubPhase + P.bubTone * 0.25f * T(v.bubPhase * 2)) * v.bubAmp * P.bubAmt * 0.25f;
                    sl += b * (1 - v.bubPan); sr2 += b * (1 + v.bubPan);
                }
                if (useNoise && P.nzLevel > 0.0001f) {   // the noise ghost, heard
                    const float nOut = nzSig * (nzLA + nzLD * rt) * (anyMod ? v.mc[8] : 1.0f) * 0.6f * v.ghostGain;
                    if (split && P.nzF2) { slB += nOut; srB += nOut; } else { sl += nOut; sr2 += nOut; }
                }
                if (subOn) {   // SUB: mono, under everything, follows ghost 1's envelope
                    v.subPh += baseInc * (P.subOct == 0 ? 0.5f : 0.25f);
                    if (v.subPh >= 1) v.subPh -= std::floor(v.subPh);
                    const float ph = v.subPh; float x;
                    switch (P.subWave) {
                        case 1: x = ph < 0.25f ? 4 * ph : (ph < 0.75f ? 2 - 4 * ph : 4 * ph - 4); break;
                        case 2: {   // square, poly-BLEP band-limited
                            const float dt = baseInc * (P.subOct == 0 ? 0.5f : 0.25f);
                            auto blep = [dt](float t) { if (t < dt) { t /= dt; return t + t - t * t - 1.0f; } if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; } return 0.0f; };
                            float q = ph + 0.5f; q -= std::floor(q);
                            x = (ph < 0.5f ? 1.0f : -1.0f) + blep(ph) - blep(q); x *= 0.8f; break; }
                        case 3: x = std::tanh(2.2f * sineTable()(ph)) * 1.0249f; break;
                        default: x = sineTable()(ph); break;
                    }
                    float se = env0;   // SUB FOLLOWS: ghost 1 (as before) / the longest-sounding heard ghost / its own attack + release
                    if (P.subEnv == 1) { se = 0; for (int a = 0; a < nAct; ++a) { const int o = act[a]; if (P.op[o].level > 0.0f) se = juce::jmax(se, v.opEnv[o].level * velAmt[o]); } }
                    else if (P.subEnv == 2) se = v.subEnv.next() * velAmt[0];
                    x = v.subF.tick(0, x, 0) * (subA + subD * rt) * se * (anyMod ? v.mc[9] : 1.0f);
                    if (anyMod && subClean) x *= v.mc[10];   // (the other path gets VOLUME with the ghosts below)
                    if (subClean) subOs[i] += x * 0.5f; else { sl += x; sr2 += x; }
                }
                if (anyFlt) {   // FILTER 1 / FILTER 2: series, parallel or split
                    if (P.fltRoute == 0) {
                        if (fltOn[0]) { sl = v.bf[0].tick(0, sl); sr2 = v.bf[0].tick(1, sr2); }
                        if (fltOn[1]) { sl = v.bf[1].tick(0, sl); sr2 = v.bf[1].tick(1, sr2); }
                    } else if (P.fltRoute == 1) {
                        if (fltOn[0] && fltOn[1]) { const float a0 = v.bf[0].tick(0, sl), a1 = v.bf[0].tick(1, sr2); sl = (a0 + v.bf[1].tick(0, sl)) * 0.6f; sr2 = (a1 + v.bf[1].tick(1, sr2)) * 0.6f; }
                        else { auto& F = v.bf[fltOn[0] ? 0 : 1]; sl = F.tick(0, sl); sr2 = F.tick(1, sr2); }
                    } else {
                        if (fltOn[0]) { sl = v.bf[0].tick(0, sl); sr2 = v.bf[0].tick(1, sr2); }
                        if (fltOn[1]) { slB = v.bf[1].tick(0, slB); srB = v.bf[1].tick(1, srB); }
                        sl += slB; sr2 += srB;
                    }
                }
                if (P.cut < 19990.0f || P.fenv != 0.0f || P.ftype != 0 || fltOff != 0.0f || modMainCut) { sl = v.filt.tick(0, sl, P.ftype); sr2 = v.filt.tick(1, sr2, P.ftype); }
                if (useVow) {
                    float tl = 0, tr = 0;
                    for (int j = 0; j < 3; ++j) { tl += v.vf[j].tick(0, sl, 1) * vowG[j]; tr += v.vf[j].tick(1, sr2, 1) * vowG[j]; }
                    sl = sl * (1 - P.vowAmt) + tl * P.vowAmt * 1.7f; sr2 = sr2 * (1 - P.vowAmt) + tr * P.vowAmt * 1.7f;
                }
                if (anyMod) { sl *= v.mc[10]; sr2 *= v.mc[10]; }   // VOLUME target (1 unless used)
                L[i] += sl * 0.5f; R[i] += sr2 * 0.5f;
                v.wobClock += 1.0;
            }
            for (int o = 0; o < kOps; ++o) meterAcc[o] = juce::jmax(meterAcc[o], v.opEnv[o].level);
            meterAcc[kOps] = juce::jmax(meterAcc[kOps], useNoise ? v.nzEnv.level : 0.0f);
            if (useSmp && v.smpEnv.active()) playheadUi = (float) (v.spos / S->size());
        }
        for (int o = 0; o <= kOps; ++o) meter[(size_t) o].store(meterAcc[o]);
        smpPlayhead.store(playheadUi); chopSounding.store(chopUi);
        if (P.seqOn && P.seqDest == 0)   // rhythm gate, ramped across the segment so it never clicks
            for (int i = 0; i < on; ++i) { const float g = gateA + (gateB - gateA) * (float) i / (float) on; L[i] *= g; R[i] *= g; }

        // POSSESS: drive at 2x (tanh, folding into wavefold past 60%)
        const float pz = possess();
        if (pz > 0.001f) {
            const float g = 1.0f + pz * pz * 28.0f;
            const float fold = juce::jlimit(0.0f, 1.0f, (pz - 0.6f) / 0.4f);
            const float comp = 1.0f / (1.0f + pz * 0.9f);
            for (int ch = 0; ch < 2; ++ch) {
                float* x = up.getChannelPointer((size_t) ch);
                for (int i = 0; i < on; ++i) {
                    const float t = std::tanh(x[i] * g);
                    const float f = fold > 0 ? sineTable()(x[i] * g * 0.18f) : 0.0f;
                    x[i] = (t * (1 - fold) + f * fold) * comp;
                }
            }
        }
        if (P.distOn) distort(up, on, modDrive);
        if (P.mbOn) multiband(up, on);
        os->processSamplesDown(hostBlock);
        if (subOs) osSub->processSamplesDown(subBlock);
    }

    // DIST (2x): overdrive, tube, hard clip, fold, bitcrush, downsample; TONE darkens after it
    void distort(juce::dsp::AudioBlock<float>& up, int on, bool modDrive) {
        const float toneHz = 120.0f * std::exp2(P.distTone * 7.5f);
        const bool tone = P.distTone < 0.995f;
        const float tk = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * toneHz / (float) osr);
        for (int i = 0; i < on; ++i) {
            const float rt = (float) (i + 1) * segDT;
            const float mix = dMxA + dMxD * rt;
            const float dr = juce::jlimit(0.0f, 1.0f, dDrA + dDrD * rt + (modDrive ? driveArr[(size_t) (i >> ctlShift)] : 0.0f));
            const bool hold = P.distType == 5 && (dsCount++ % (1 + (int) (dr * dr * 40.0f))) != 0;
            for (int ch = 0; ch < 2; ++ch) {
                float* xp = up.getChannelPointer((size_t) ch);
                const float x = xp[i]; float y;
                switch (P.distType) {
                    case 1: { const float g = 1.0f + dr * dr * 30.0f; const float z = x * g; y = (z > 0 ? std::tanh(z) : std::tanh(z * 0.6f) / 0.6f * 0.8f) / (1.0f + dr * 0.8f); break; }
                    case 2: { const float g = 1.0f + dr * dr * 40.0f; y = juce::jlimit(-0.8f, 0.8f, x * g) / (1.0f + dr * 0.4f); break; }
                    case 3: { const float g = 1.0f + dr * dr * 24.0f; y = sineTable()(x * g * 0.2f) * 0.85f; break; }
                    case 4: { const float q = std::exp2(15.0f - dr * 12.0f); y = std::round(x * q) / q; break; }
                    case 5: { if (!hold) dsHold[ch] = x; y = dsHold[ch]; break; }
                    default: { const float g = 1.0f + dr * dr * 40.0f; y = std::tanh(x * g) / (1.0f + dr * 1.1f); break; }
                }
                if (tone) { distLp[ch] += (y - distLp[ch]) * tk; y = distLp[ch]; }
                xp[i] = x + (y - x) * mix;
            }
        }
    }
    // MULTIBAND (2x): three bands, each with its own drive; the low band stays clean at drive 0
    void multiband(juce::dsp::AudioBlock<float>& up, int on) {
        if (P.mbXlo != mbLo) { mbLo = P.mbXlo; mbX[0].setCutoffFrequency(mbLo); mbX[1].setCutoffFrequency(mbLo); }
        if (P.mbXhi != mbHi) { mbHi = P.mbXhi; mbX[2].setCutoffFrequency(mbHi); mbX[3].setCutoffFrequency(mbHi); mbAp.setCutoffFrequency(mbHi); }
        auto shape = [](float x, float dr) { if (dr < 0.001f) return x; const float g = 1.0f + dr * dr * 30.0f; return std::tanh(x * g) / std::pow(g, 0.62f); };
        for (int ch = 0; ch < 2; ++ch) {
            float* x = up.getChannelPointer((size_t) ch);
            for (int i = 0; i < on; ++i) {
                const float in = x[i], mix = mbMxA + mbMxD * (float) (i + 1) * segDT;
                // the low band also goes through the high split's allpass, so the three bands sum back flat; the dry signal for
                // MIX is that same band sum (mixing the raw input against phase-shifted bands would comb-filter at the splits)
                const float lo = mbAp.processSample(ch, mbX[0].processSample(ch, in)), rest = mbX[1].processSample(ch, in);
                const float mid = mbX[2].processSample(ch, rest), hi = mbX[3].processSample(ch, rest);
                const float dry = lo + mid + hi;
                const float y = shape(lo, P.mbLo) + shape(mid, P.mbMid) + shape(hi, P.mbHi);
                x[i] = dry + (y - dry) * mix;
            }
        }
    }

    void effects(juce::AudioBuffer<float>& out) {
        const int n = out.getNumSamples();
        float* L = out.getWritePointer(0); float* R = out.getWritePointer(1);
        // DC blocker
        for (int ch = 0; ch < 2; ++ch) {
            float* x = out.getWritePointer(ch);
            for (int i = 0; i < n; ++i) { const float y = x[i] - dcX[ch] + 0.9995f * dcY[ch]; dcX[ch] = x[i]; dcY[ch] = y; x[i] = y; }
        }
        // COMB: a tuned feedback delay (metal tearing), following the last played note
        if (P.cmbAmt > 0.001f) {
            const float target = juce::jlimit(30.0f, 4000.0f, (P.cmbKey ? lastHz : 220.0f) * std::exp2(P.cmbTune / 12.0f));
            const int N = (int) combBuf[0].size();
            for (int i = 0; i < n; ++i) {
                combHz += (target - combHz) * 0.002f;
                const float dly = juce::jlimit(2.0f, (float) N - 2, (float) sr / combHz);
                for (int ch = 0; ch < 2; ++ch) {
                    float* x = out.getWritePointer(ch); auto& B = combBuf[ch];
                    float rp = (float) combPos - dly; if (rp < 0) rp += (float) N;
                    const int r0 = (int) rp; const float t = rp - (float) r0;
                    const float d = B[(size_t) r0] + (B[(size_t) ((r0 + 1) % N)] - B[(size_t) r0]) * t;
                    const float y = std::tanh((x[i] + P.cmbFb * d) * 0.9f) / 0.9f;
                    B[(size_t) combPos] = y;
                    x[i] = x[i] * (1 - P.cmbAmt) + y * P.cmbAmt * (1 - P.cmbFb * 0.45f);
                }
                combPos = (combPos + 1) % N;
            }
        }
        // SQUEEZE: three-band squash (down above, up below) - loud, bright, in your face
        if (P.ottAmt > 0.001f) {
            const bool ottFix = P.engVer >= 2;
            const float att = std::exp(-1.0f / (0.004f * (float) sr)), rel = std::exp(-1.0f / (0.09f * (float) sr));
            for (int i = 0; i < n; ++i) {
                float b[3][2];
                for (int ch = 0; ch < 2; ++ch) {
                    const float x = out.getSample(ch, i);
                    const float lo = xLow[0].processSample(ch, x), rest = xLow[1].processSample(ch, x);
                    b[0][ch] = ottFix ? ottAp[0].processSample(ch, lo) : lo;   // revision 2: phase-matched, so the bands add back flat
                    b[1][ch] = xHigh[0].processSample(ch, rest); b[2][ch] = xHigh[1].processSample(ch, rest);
                }
                float sum[2] = { 0, 0 };
                for (int k = 0; k < 3; ++k) {
                    const float lvl = juce::jmax(std::abs(b[k][0]), std::abs(b[k][1]));
                    ottEnv[k] = lvl > ottEnv[k] ? att * ottEnv[k] + (1 - att) * lvl : rel * ottEnv[k] + (1 - rel) * lvl;
                    const float e = juce::Decibels::gainToDecibels(ottEnv[k] + 1e-9f);
                    float gdb = 0;
                    if (e > -24.0f) gdb -= (e + 24.0f) * 0.75f;                          // 4:1 above -24 dB
                    else if (e > -70.0f) gdb += juce::jmin(16.0f, (-30.0f - e) * 0.5f) * (e < -30.0f ? 1.0f : 0.0f);   // lift the quiet parts
                    const float g = juce::Decibels::decibelsToGain(gdb * P.ottAmt + 3.0f * P.ottAmt);
                    sum[0] += b[k][0] * g; sum[1] += b[k][1] * g;
                }
                for (int ch = 0; ch < 2; ++ch) {
                    const float dry = ottFix ? b[0][ch] + b[1][ch] + b[2][ch] : out.getSample(ch, i);   // revision 2: no comb at partial amounts
                    out.setSample(ch, i, dry * (1 - P.ottAmt) + sum[ch] * P.ottAmt);
                }
            }
        }
        // CURSE: bit crush + decimation, then a 1/16 ghost-stutter past 65%
        const float c = curse();
        if (c > 0.02f) {
            const float bits = 16.0f - c * 12.0f;
            const float q = std::exp2(bits - 1);
            const int hold = 1 + (int) (c * c * 18.0f);
            for (int i = 0; i < n; ++i) {
                if (crushCount++ % hold == 0) { crushHold[0] = std::round(L[i] * q) / q; crushHold[1] = std::round(R[i] * q) / q; }
                const float mix = juce::jmin(1.0f, c * 1.6f);
                L[i] = L[i] * (1 - mix) + crushHold[0] * mix; R[i] = R[i] * (1 - mix) + crushHold[1] * mix;
            }
        }
        if (c > 0.65f) {
            const double sixteenth = 60.0 / bpm / 4.0 * sr;
            const float chance = (c - 0.65f) / 0.35f * 0.6f;
            std::uniform_real_distribution<float> u(0.0f, 1.0f);
            for (int i = 0; i < n; ++i) {
                const long step = (long) ((hostPlaying ? ppq * 4.0 * sixteenth : (double) crushCount) / sixteenth);
                if (step != gateStep) { gateStep = step; gateOn = u(rng) > chance; }
                gateLevel += ((gateOn ? 1.0f : 0.0f) - gateLevel) * 0.02f;
                L[i] *= gateLevel; R[i] *= gateLevel;
            }
        }
        // VEIL: ping-pong echo (dotted 1/8) + reverb
        const float v = veil();
        if (v > 0.01f) {
            const int dly = juce::jlimit(1, (int) delayL.size() - 1, (int) (60.0 / bpm * 0.75 * sr));
            const float mix = v * 0.22f, fbk = 0.25f + v * 0.35f;
            const int N = (int) delayL.size();
            for (int i = 0; i < n; ++i) {
                const int rp = (dPos - dly + N) % N;
                const float yl = delayL[(size_t) rp], yr = delayR[(size_t) rp];
                dLp[0] += (yr - dLp[0]) * 0.35f; dLp[1] += (yl - dLp[1]) * 0.35f;   // ping-pong with darkening
                delayL[(size_t) dPos] = L[i] + dLp[0] * fbk; delayR[(size_t) dPos] = R[i] * 0.3f + dLp[1] * fbk;
                L[i] += yl * mix; R[i] += yr * mix;
                dPos = (dPos + 1) % N;
            }
            juce::dsp::Reverb::Parameters rp;
            rp.roomSize = 0.55f + v * 0.43f; rp.damping = 0.45f; rp.wetLevel = v * 0.42f; rp.dryLevel = 1.0f - v * 0.3f; rp.width = 1.0f;
            reverb.setParameters(rp);
            juce::dsp::AudioBlock<float> b(out);
            reverb.process(juce::dsp::ProcessContextReplacing<float>(b));
        }
        // BASS update: SUB SOLO, the clean sub (its own 2x path, after every effect), LOW-END MONO
        if (P.subSolo) out.clear();
        if (subThisBlock) for (int i = 0; i < n; ++i) { L[i] += subHost[(size_t) i]; R[i] += subHost[(size_t) i]; }
        if (P.monoBelow >= 20.0f) {
            lmA.setupK(P.monoBelow, 1.414f, (float) sr); lmB.setupK(P.monoBelow, 1.414f, (float) sr);
            for (int i = 0; i < n; ++i) {
                const float m = (L[i] + R[i]) * 0.5f, sd = lmB.tick(0, lmA.tick(0, (L[i] - R[i]) * 0.5f, 2), 2);
                L[i] = m + sd; R[i] = m - sd;
            }
        }
        // master + safety. The original engine: a gentle clip on every sample. Revision 2: linear (ramped) master, and a soft knee
        // only above -1 dBFS (0.9) that tops out at 1.2, so a clean sub stays clean
        const float g = juce::Decibels::decibelsToGain(P.masterDb);
        const bool v2 = P.engVer >= 2;
        if (!masterPrimed) { lastMaster = g; masterPrimed = true; }
        const float gA = v2 ? lastMaster : g, gD = g - gA; lastMaster = g;
        const float invN = 1.0f / (float) juce::jmax(1, n);
        auto knee = [](float x) { const float a = std::abs(x); if (a <= 0.9f) return x; const float o = a - 0.9f; return std::copysign(0.9f + o / (1.0f + o / 0.3f), x); };
        for (int i = 0; i < n; ++i) {
            float l, r;
            if (v2) { const float gi = gA + gD * (float) (i + 1) * invN; l = knee(L[i] * gi); r = knee(R[i] * gi); }
            else { l = L[i] * g; r = R[i] * g; l = l / (1.0f + std::abs(l) * 0.25f) * 1.0f; r = r / (1.0f + std::abs(r) * 0.25f); }
            L[i] = juce::jlimit(-1.2f, 1.2f, l); R[i] = juce::jlimit(-1.2f, 1.2f, r);
            if ((i & 1) == 0) { int sp = scopePos.load(); scope[sp] = (l + r) * 0.5f; scopePos.store((sp + 1) % kScope); }
            if (!ring.empty()) { const int rp = ringPos.load(); ring[(size_t) rp] = (L[i] + R[i]) * 0.5f; ringPos.store((rp + 1) % (int) ring.size()); }
        }
    }
};

} // namespace gs
