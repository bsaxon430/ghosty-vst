#pragma once
// Poltergeist engine: 4-operator phase-modulation FM (any ghost can modulate any other), unison, per-voice
// filter, pitch envelope, glide, a tempo-syncable "drift" LFO, and the macro-driven effects chain.
// Voices run at 2x the host rate (JUCE oversampling) so hard FM and drive alias less.
#include "Params.h"
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
    bool active() const {
        for (auto& e : opEnv) if (e.active()) return true;
        return false;
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
    }
    int latency() const { return os ? (int) os->getLatencyInSamples() : 0; }
    void setBpm(double b) { if (b > 20 && b < 400) bpm = b; }
    void setPpq(double p, bool playing) { ppq = p; hostPlaying = playing; }
    void setModWheel(float m) { modWheel = m; }

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
        else if (m.isAllNotesOff() || m.isAllSoundOff()) { for (auto& v : voices) { for (auto& e : v.opEnv) e.release(); v.fEnv.release(); v.held = false; } heldCount = 0; }
    }

    void setEnvs(Voice& v) {
        for (int o = 0; o < kOps; ++o) v.opEnv[o].set(P.op[o].a, P.op[o].d, P.op[o].s, P.op[o].r, (float) osr);
        v.fEnv.set(P.fa, P.fd, P.fs, P.fr, (float) osr);
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
            v.held = false; for (auto& e : v.opEnv) e.release(); v.fEnv.release();
            return;
        }
        for (auto& v : voices) if (v.note == note && v.held) { v.held = false; for (auto& e : v.opEnv) e.release(); v.fEnv.release(); }
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

        for (auto& v : voices) {
            if (!v.active()) continue;
            const float velAmt[kOps] = {
                1 - P.op[0].vel * (1 - v.vel), 1 - P.op[1].vel * (1 - v.vel), 1 - P.op[2].vel * (1 - v.vel), 1 - P.op[3].vel * (1 - v.vel) };
            const float keyOct = (v.targetNote - 60.0f) / 12.0f * P.fkey;
            for (int i = 0; i < on; ++i) {
                v.curNote += (v.targetNote - v.curNote) * glideCoef;
                v.penv *= penvCoef;
                const float noteNow = v.curNote + bendSemis + v.penv * P.penvAmt + vib;
                const float freq = 440.0f * std::exp2((noteNow - 69.0f) / 12.0f);
                const float baseInc = freq / (float) osr;
                float env[kOps];
                for (int o = 0; o < kOps; ++o) env[o] = v.opEnv[o].next() * velAmt[o];
                const float fe = v.fEnv.next();
                if ((i & 15) == 0) {
                    const float c = P.cut * std::exp2(juce::jlimit(-10.0f, 10.0f, P.fenv * 6.0f * fe + keyOct + lfo * d * 1.6f));
                    v.filt.setup(c, P.res, (float) osr);
                }
                float sl = 0, sr2 = 0;
                for (int k = 0; k < uni; ++k) {
                    float outs[kOps];
                    for (int o = 0; o < kOps; ++o) {
                        float pm = fb[o] * 0.5f * (v.prev[k][o] + v.prev2[k][o]);
                        for (int f = 0; f < kOps; ++f) if (f != o) pm += mIdx[f][o] * v.prev[k][f];
                        v.phase[k][o] += baseInc * ratio[o] * detune[k];
                        if (v.phase[k][o] >= 1) v.phase[k][o] -= 1;
                        outs[o] = waveAt(v.phase[k][o] + pm, P.op[o].wave) * env[o];
                    }
                    float mono = 0;
                    for (int o = 0; o < kOps; ++o) { v.prev2[k][o] = v.prev[k][o]; v.prev[k][o] = outs[o]; mono += outs[o] * carrier[o]; }
                    sl += mono * panL[k]; sr2 += mono * panR[k];
                }
                sl *= norm; sr2 *= norm;
                if (P.cut < 19990.0f || P.fenv != 0.0f || P.ftype != 0) { sl = v.filt.tick(0, sl, P.ftype); sr2 = v.filt.tick(1, sr2, P.ftype); }
                L[i] += sl * 0.5f; R[i] += sr2 * 0.5f;
            }
            for (int o = 0; o < kOps; ++o) meterAcc[o] = juce::jmax(meterAcc[o], v.opEnv[o].level);
        }
        for (int o = 0; o < kOps; ++o) meter[(size_t) o].store(meterAcc[o]);

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
        }
    }
};

} // namespace gs
