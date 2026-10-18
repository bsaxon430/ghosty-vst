#pragma once
// Poltergeist engine: 4-operator phase-modulation FM (any ghost can modulate any other), unison, per-voice
// filter, pitch envelope, glide, a tempo-syncable "drift" LFO, and the macro-driven effects chain.
// Voices run at 2x the host rate (JUCE oversampling) so hard FM and drive alias less.
#include "Params.h"
#include "Sample.h"
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
        const float x = cycles * N; const int i = (int) x; const float f = x - (float) i;
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
    enum Stage { Idle, Attack, Decay, Sustain, Release } stage = Idle;
    float level = 0, a = 0.01f, d = 0.3f, s = 0.5f, r = 0.3f, sr = 88200;
    float aInc = 0, dCoef = 0, rCoef = 0;
    void set(float A, float D, float S, float R, float rate) {
        a = A; d = D; s = S; r = R; sr = rate;
        aInc = 1.0f / juce::jmax(1.0f, a * sr);
        dCoef = std::exp(-6.9f / juce::jmax(1.0f, d * sr));
        rCoef = std::exp(-6.9f / juce::jmax(1.0f, r * sr));
    }
    void trigger(bool legato) { if (!legato || stage == Idle || stage == Release) stage = Attack; }
    void release() { if (stage != Idle) stage = Release; }
    bool active() const { return stage != Idle; }
    inline float next() {
        switch (stage) {
            case Attack: level += aInc; if (level >= 1) { level = 1; stage = Decay; } break;
            case Decay: level = s + (level - s) * dCoef; if (std::abs(level - s) < 1e-4f) { level = s; stage = Sustain; } break;
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
    Env smpEnv; double spos = 0; int slice = 0; float wtPhase = 0, grainClock = 0;
    struct Grain { double pos = 0, inc = 1; int age = 0, len = 0; float pan = 0; bool on = false; } grains[16];
    SVF vf[3];
    float bubPhase = 0, bubNote = 0, bubSweep = 0, bubAmp = 0, bubClock = 0, bubPan = 0; double bubStepAt = -1;
    float fph[kMaxUnison] {};
    bool active() const {
        for (auto& e : opEnv) if (e.active()) return true;
        return smpEnv.active();
    }
};

// ---- engine ----------------------------------------------------------------------------------------
class Engine {
public:
    void prepare(double hostRate, int maxBlock) {
        sr = hostRate; osr = hostRate * 2.0;
        os = std::make_unique<juce::dsp::Oversampling<float>>(2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);
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
    std::atomic<float> smpPlayhead { -1.0f };

    // Plays one block. midi events are host-rate sample positions.
    void process(juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, const Snap& p) {
        P = p;
        const int n = out.getNumSamples();
        out.clear();
        int pos = 0;
        for (const auto md : midi) {
            const int at = juce::jlimit(0, n, md.samplePosition);
            if (at > pos) { renderSegment(out, pos, at - pos); pos = at; }
            handle(md.getMessage());
        }
        if (pos < n) renderSegment(out, pos, n - pos);
        effects(out);
    }

    // live view for the UI: how hard each ghost is sounding, and a short scope buffer
    std::array<std::atomic<float>, kOps> meter {};
    static constexpr int kScope = 512;
    float scope[kScope] {}; std::atomic<int> scopePos { 0 };

private:
    Snap P {};
    double sr = 44100, osr = 88200, bpm = 120, ppq = 0; bool hostPlaying = false;
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
    std::vector<float> ring; std::atomic<int> ringPos { 0 };
    std::vector<float> combBuf[2]; int combPos = 0; float combHz = 220, lastHz = 220;
    float seqVal = 0; double freeClock = 0;
    juce::dsp::LinkwitzRileyFilter<float> xLow[2], xHigh[2];
    float ottEnv[3] {}; juce::AudioBuffer<float> bands;

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
        else if (m.isAllNotesOff() || m.isAllSoundOff()) { for (auto& v : voices) { for (auto& e : v.opEnv) e.release(); v.fEnv.release(); v.smpEnv.release(); v.held = false; } heldCount = 0; }
    }

    void setEnvs(Voice& v) {
        for (int o = 0; o < kOps; ++o) v.opEnv[o].set(P.op[o].a, P.op[o].d, P.op[o].s, P.op[o].r, (float) osr);
        v.fEnv.set(P.fa, P.fd, P.fs, P.fr, (float) osr);
        v.smpEnv.set(P.smpA, P.smpD, P.smpS, P.smpR, (float) osr);
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
        if (P.mono) {
            Voice& v = voices[0];
            const bool legato = v.active() && v.held;
            v.note = note; v.vel = vel; v.held = true; v.age = ++ageCounter;
            v.targetNote = playNote;
            if (!v.active() || P.glide < 0.001f) v.curNote = playNote;
            setEnvs(v);
            for (auto& e : v.opEnv) e.trigger(legato);
            v.fEnv.trigger(legato);
            if (!legato) { v.penv = 1; resetPhases(v); v.filt.reset(); }
            startExtras(v, note, legato);
            return;
        }
        const int limit = juce::jlimit(1, kMaxVoices, P.voices);
        Voice* pick = nullptr;
        for (int i = 0; i < limit; ++i) if (voices[(size_t) i].note == note && voices[(size_t) i].active()) { pick = &voices[(size_t) i]; break; }
        if (!pick) for (int i = 0; i < limit; ++i) if (!voices[(size_t) i].active()) { pick = &voices[(size_t) i]; break; }
        if (!pick) { // steal: oldest released first, else oldest
            uint64_t best = UINT64_MAX;
            for (int i = 0; i < limit; ++i) { auto& v = voices[(size_t) i]; uint64_t score = v.age + (v.held ? (1ull << 40) : 0); if (score < best) { best = score; pick = &v; } }
        }
        Voice& v = *pick;
        v.note = note; v.vel = vel; v.held = true; v.age = ++ageCounter;
        v.curNote = v.targetNote = playNote; v.penv = 1;
        setEnvs(v);
        for (auto& e : v.opEnv) e.trigger(false);
        v.fEnv.trigger(false);
        resetPhases(v); v.filt.reset();
        startExtras(v, note, false);
    }

    void resetPhases(Voice& v) {
        std::uniform_real_distribution<float> u(0.0f, 1.0f);
        for (int k = 0; k < kMaxUnison; ++k)
            for (int o = 0; o < kOps; ++o) { v.phase[k][o] = k == 0 ? 0.0f : u(rng); v.prev[k][o] = v.prev2[k][o] = 0; }
    }

    void noteOff(int note) {
        for (int i = 0; i < heldCount; ++i) if (heldStack[i] == note) { std::memmove(heldStack + i, heldStack + i + 1, sizeof(int) * (size_t) (heldCount - i - 1)); --heldCount; break; }
        if (P.mono) {
            Voice& v = voices[0];
            if (v.note != note) return;
            if (heldCount > 0) {   // fall back to the previous held note (legato)
                const int back = heldStack[heldCount - 1];
                v.note = back; v.targetNote = (float) (back + 12 * P.octave);
                if (P.glide < 0.001f) v.curNote = v.targetNote;
                return;
            }
            v.held = false; for (auto& e : v.opEnv) e.release(); v.fEnv.release(); v.smpEnv.release();
            return;
        }
        for (auto& v : voices) if (v.note == note && v.held) { v.held = false; for (auto& e : v.opEnv) e.release(); v.fEnv.release(); v.smpEnv.release(); }
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
                const double inc = rb * track * pitch * (P.smpRev ? -1 : 1);
                if (v.spos < a || v.spos >= a + len) {
                    if (!P.smpLoop) return 0.0f;
                    v.spos = P.smpRev ? a + len - 1 : a;
                }
                const float out = S.at(v.spos);
                v.spos += inc;
                return out;
            }
        }
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
        float meterAcc[kOps] {};

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

        auto smp = std::atomic_load(&sample);
        const SampleData* S = (smp && smp->size() > 64) ? smp.get() : nullptr;
        const bool useSmp = S && (P.smpLevel > 0.001f || P.smpFm > 0.001f);
        const float scanOff = lfo * P.smpScan * 0.5f;
        const float syncMul = 1.0f + P.syncAmt * 7.0f;
        const bool useSync = P.syncAmt > 0.001f;
        const bool useVow = P.vowAmt > 0.001f;
        const bool useBub = P.bubAmt > 0.001f;
        const float bubStep = (float) (lfoDivBeats(P.bubDiv) * 60.0 / bpm * osr);
        const float bubSweepK = std::exp(-1.0f / (0.012f * (1 + P.bubTone * 2) * (float) osr));
        const float bubAmpK = std::exp(-1.0f / ((0.05f + P.bubTone * 0.08f) * (float) osr));
        static const float FORM[5][3] = { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 300, 2300, 3000 }, { 450, 800, 2830 }, { 325, 700, 2530 } };
        static const float FGAIN[5][3] = { { 1, 0.50f, 0.10f }, { 1, 0.40f, 0.20f }, { 1, 0.30f, 0.20f }, { 1, 0.35f, 0.08f }, { 1, 0.25f, 0.06f } };
        float playheadUi = -1;

        for (auto& v : voices) {
            if (!v.active()) continue;
            const float velAmt[kOps] = {
                1 - P.op[0].vel * (1 - v.vel), 1 - P.op[1].vel * (1 - v.vel), 1 - P.op[2].vel * (1 - v.vel), 1 - P.op[3].vel * (1 - v.vel) };
            const float keyOct = (v.targetNote - 60.0f) / 12.0f * P.fkey;
            for (int i = 0; i < on; ++i) {
                v.curNote += (v.targetNote - v.curNote) * glideCoef;
                v.penv *= penvCoef;
                const float noteNow = v.curNote + bendSemis + v.penv * P.penvAmt + vib + pitchOff;
                const float freq = 440.0f * std::exp2((noteNow - 69.0f) / 12.0f);
                const float baseInc = freq / (float) osr;
                float env[kOps];
                for (int o = 0; o < kOps; ++o) env[o] = v.opEnv[o].next() * velAmt[o];
                const float fe = v.fEnv.next();
                if ((i & 15) == 0) {
                    const float c = P.cut * std::exp2(juce::jlimit(-10.0f, 10.0f, P.fenv * 6.0f * fe + keyOct + lfo * d * 1.6f + fltOff));
                    v.filt.setup(c, P.res, (float) osr);
                    if (useVow) {   // TALK: three formant band-passes sliding a-e-i-o-u
                        const float vp = juce::jlimit(0.0f, 1.0f, P.vowPos + lfo * P.vowMod * 0.5f + vowOff + modWheel * 0.3f) * 3.999f;
                        const int a = (int) vp; const float t = vp - (float) a;
                        for (int j = 0; j < 3; ++j) v.vf[j].setup(FORM[a][j] + (FORM[a + 1][j] - FORM[a][j]) * t, 0.78f + 0.19f * P.vowRes, (float) osr);
                        for (int j = 0; j < 3; ++j) vowG[j] = FGAIN[a][j] + (FGAIN[a + 1][j] - FGAIN[a][j]) * t;
                    }
                }
                // SAMPLE GHOST
                float smpOut = 0;
                if (useSmp) smpOut = sampleTick(v, *S, noteNow, scanOff) * v.smpEnv.next();
                else if (v.smpEnv.active()) v.smpEnv.next();
                float sl = 0, sr2 = 0;
                for (int k = 0; k < uni; ++k) {
                    float outs[kOps];
                    if (useSync) {   // ghost 1 hard-syncs to the note: it restarts every cycle of the played pitch
                        v.fph[k] += baseInc * detune[k];
                        if (v.fph[k] >= 1) { v.fph[k] -= 1; v.phase[k][0] = 0; }
                    }
                    for (int o = 0; o < kOps; ++o) {
                        float pm = fb[o] * 0.5f * (v.prev[k][o] + v.prev2[k][o]);
                        for (int f = 0; f < kOps; ++f) if (f != o) pm += mIdx[f][o] * v.prev[k][f];
                        if (o == 0) pm += P.smpFm * smpOut * 1.5f;   // the sample haunts ghost 1
                        v.phase[k][o] += baseInc * ratio[o] * detune[k] * (o == 0 && useSync ? syncMul : 1.0f);
                        if (v.phase[k][o] >= 1) v.phase[k][o] -= std::floor(v.phase[k][o]);
                        outs[o] = waveAt(v.phase[k][o] + pm, P.op[o].wave) * env[o];
                    }
                    float mono = 0;
                    for (int o = 0; o < kOps; ++o) { v.prev2[k][o] = v.prev[k][o]; v.prev[k][o] = outs[o]; mono += outs[o] * carrier[o]; }
                    sl += mono * panL[k]; sr2 += mono * panR[k];
                }
                sl *= norm; sr2 *= norm;
                sl += smpOut * P.smpLevel * 0.9f; sr2 += smpOut * P.smpLevel * 0.9f;
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
                if (P.cut < 19990.0f || P.fenv != 0.0f || P.ftype != 0 || fltOff != 0.0f) { sl = v.filt.tick(0, sl, P.ftype); sr2 = v.filt.tick(1, sr2, P.ftype); }
                if (useVow) {
                    float tl = 0, tr = 0;
                    for (int j = 0; j < 3; ++j) { tl += v.vf[j].tick(0, sl, 1) * vowG[j]; tr += v.vf[j].tick(1, sr2, 1) * vowG[j]; }
                    sl = sl * (1 - P.vowAmt) + tl * P.vowAmt * 1.7f; sr2 = sr2 * (1 - P.vowAmt) + tr * P.vowAmt * 1.7f;
                }
                L[i] += sl * 0.5f; R[i] += sr2 * 0.5f;
            }
            for (int o = 0; o < kOps; ++o) meterAcc[o] = juce::jmax(meterAcc[o], v.opEnv[o].level);
            if (useSmp && v.smpEnv.active()) playheadUi = (float) (v.spos / S->size());
        }
        for (int o = 0; o < kOps; ++o) meter[(size_t) o].store(meterAcc[o]);
        smpPlayhead.store(playheadUi);
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
        os->processSamplesDown(hostBlock);
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
        // OTT: three-band squash (down above, up below) - loud, bright, in your face
        if (P.ottAmt > 0.001f) {
            const float att = std::exp(-1.0f / (0.004f * (float) sr)), rel = std::exp(-1.0f / (0.09f * (float) sr));
            for (int i = 0; i < n; ++i) {
                float b[3][2];
                for (int ch = 0; ch < 2; ++ch) {
                    const float x = out.getSample(ch, i);
                    const float lo = xLow[0].processSample(ch, x), rest = xLow[1].processSample(ch, x);
                    b[0][ch] = lo; b[1][ch] = xHigh[0].processSample(ch, rest); b[2][ch] = xHigh[1].processSample(ch, rest);
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
                for (int ch = 0; ch < 2; ++ch) out.setSample(ch, i, out.getSample(ch, i) * (1 - P.ottAmt) + sum[ch] * P.ottAmt);
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
        // master + gentle safety clip
        const float g = juce::Decibels::decibelsToGain(P.masterDb);
        for (int i = 0; i < n; ++i) {
            float l = L[i] * g, r = R[i] * g;
            l = l / (1.0f + std::abs(l) * 0.25f) * 1.0f; r = r / (1.0f + std::abs(r) * 0.25f);
            L[i] = juce::jlimit(-1.2f, 1.2f, l); R[i] = juce::jlimit(-1.2f, 1.2f, r);
            if ((i & 1) == 0) { int sp = scopePos.load(); scope[sp] = (l + r) * 0.5f; scopePos.store((sp + 1) % kScope); }
            if (!ring.empty()) { const int rp = ringPos.load(); ring[(size_t) rp] = (L[i] + R[i]) * 0.5f; ringPos.store((rp + 1) % (int) ring.size()); }
        }
    }
};

} // namespace gs
