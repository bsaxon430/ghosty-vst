#pragma once
// Mr. Mickens engine (real-time safe).
//   4 BREAK BAYS: each break is cut into 16th slices; every 16th of the host grid triggers a slice, so breaks lock
//                 to any tempo. NUDGE shifts the start (snapped to a grid); RANDOM START picks a new start every
//                 bar; RANDOM CHOPS swaps slices at random. Each bay ducks from the kick / snare / hats on its own.
//   DRUMS:        kick, snare, hat, open hat on a 16-step grid (hard-hitting sounds from the Ghosty library).
//   BREAK FX:     crunch -> parallel filters (low/high/band/notch/comb/formant, each with its own mix)
//                 -> chorus -> stutter (with its own filter + envelope) -> reverb.
#include "BreakLib.h"
#include "DrumKit.h"

namespace mk {

constexpr int kBays = 4, kDrums = 4, kSteps = 16;

struct BayParams { int breakIndex = 0; int nudge = 0, grid = 1; float pitch = 0, level = 0.8f, decay = 1, pan = 0; bool rndStart = false, rndChops = false; float rndAmt = 0.3f, duck[3] {}; bool mute = false, fit = true; };
struct FxParams {
    float crunch = 0, bits = 0;
    float dry = 1, lpCut = 0.4f, lpMix = 0, hpCut = 0.5f, hpMix = 0, bpCut = 0.5f, bpMix = 0, notchCut = 0.5f, notchMix = 0, combCut = 0.4f, combMix = 0, formant = 0.3f, formMix = 0, res = 0.5f;
    float chorus = 0, reverb = 0, size = 0.6f;
    bool stutter = false; int stutDiv = 2; float stutCut = 0.6f, stutEnv = 0.6f, stutDecay = 0.4f;
    float drumLvl[kDrums] { 1, 1, 0.8f, 0.7f }; bool drumMute[kDrums] {}; bool drumsMute = false; float duckRelease = 0.18f, breaksLevel = 0.85f, drumsLevel = 0.9f, volume = 0.8f; float bpm = 172;
};
struct Snapshot {
    std::array<const Break*, kBays> brk {};
    std::array<std::array<uint8_t, kSteps>, kDrums> drums {};   // velocity 0..127
    std::array<const gb::Sound*, kDrums> drumSound {};
};

class Engine {
public:
    void prepare(double rate, int block) {
        sr = rate; maxBlock = juce::jmax(64, block);
        busBuf.setSize(2, maxBlock); drumBufs.setSize(kDrums * 2, maxBlock);
        juce::dsp::ProcessSpec spec { rate, (juce::uint32) maxBlock, 2 };
        chorus.prepare(spec); chorus.setRate(0.9f); chorus.setCentreDelay(7.0f); chorus.setFeedback(0.15f);
        reverb.prepare(spec);
        combBuf[0].assign((size_t) (rate / 25.0) + 4, 0.0f); combBuf[1] = combBuf[0]; combPos = 0;
        stutBuf[0].assign((size_t) (rate * 1.0), 0.0f); stutBuf[1] = stutBuf[0];
        for (auto& b : bays) b = Bay();
        for (auto& v : dv) v = DV();
        for (int e = 0; e < 3; ++e) { duckCurve[e].assign((size_t) maxBlock, 0.0f); duckHit[e].assign((size_t) maxBlock, 0); duckEnv[e] = 0; duckAtk[e] = 0; }
        clock = 0; lastStep = -1;
    }
    void setSnapshot(const Snapshot* s) { snap.store(s); }
    void restart() { restartFlag.store(true); }   // message thread: start the internal clock from bar 1
    std::atomic<bool> restartFlag { false };
    std::atomic<bool> playing { false };
    std::atomic<int> uiStep { -1 }; std::atomic<int> uiSlice[kBays] { { -1 }, { -1 }, { -1 }, { -1 } };
    std::atomic<uint32_t> drumHits[kDrums] {};
    // for stems: which parts to output (bit 0 = breaks, bits 1..4 = drums)
    uint32_t outMask = 0xffffffffu;

    void process(juce::AudioBuffer<float>& out, const std::array<BayParams, kBays>& B, const FxParams& F, double hostBpm, double hostPpq, bool hostPlaying, const juce::MidiBuffer& midi) {
        const int n = out.getNumSamples(); out.clear();
        const Snapshot* S = snap.load(); if (!S) return;
        const double bpm = hostPlaying ? hostBpm : F.bpm; curBpm = bpm; for (int d = 0; d < kDrums; ++d) { lvl[d] = F.drumLvl[d]; muted[d] = F.drumsMute || F.drumMute[d]; } const double spb = sr * 60.0 / juce::jmax(40.0, bpm);
        if (restartFlag.exchange(false)) { clock = 0; for (auto& b : bays) b.lastBar = -1; }
        const bool run = hostPlaying || playing;
        const double b0 = hostPlaying ? hostPpq : clock, b1 = b0 + n / spb;
        busBuf.clear(0, n); drumBufs.clear(0, n);
        // live MIDI: C1 D1 F#1 A#1 = kick snare hat open hat (GM-ish), anything else ignored
        for (const auto md : midi) { const auto m = md.getMessage(); if (!m.isNoteOn()) continue; const int nn = m.getNoteNumber();
            const int d = nn == 36 ? 0 : nn == 38 ? 1 : nn == 42 ? 2 : nn == 46 ? 3 : -1; if (d >= 0) trigDrum(S, d, m.getFloatVelocity(), md.samplePosition); }
        if (run) {
            // every 16th that starts in this block
            for (long s = (long) std::ceil(b0 * 4.0 - 1e-9); s < b1 * 4.0; ++s) {
                const int at = juce::jlimit(0, n - 1, (int) ((s / 4.0 - b0) * spb));
                const int st = (int) (((s % kSteps) + kSteps) % kSteps);
                uiStep.store(st);
                for (int d = 0; d < kDrums; ++d) if (const uint8_t v = S->drums[(size_t) d][(size_t) st]) trigDrum(S, d, v / 127.0f, at);
                for (int b = 0; b < kBays; ++b) stepBay(S, B[(size_t) b], b, s, at);
            }
            if (!hostPlaying) clock = b1;
        } else uiStep.store(-1);

        // render bays (with their own ducking) into the break bus, drums into their own buffers
        const float rel = std::exp(-1.0f / (juce::jmax(0.02f, F.duckRelease) * (float) sr));
        buildDucks(n, rel);
        for (int b = 0; b < kBays; ++b) renderBay(B[(size_t) b], b, n);
        renderDrums(n);
        breakFx(F, n, spb);
        // mix
        float* L = out.getWritePointer(0); float* R = out.getWritePointer(1);
        if (outMask & 1u) for (int i = 0; i < n; ++i) { L[i] += busBuf.getSample(0, i) * F.breaksLevel; R[i] += busBuf.getSample(1, i) * F.breaksLevel; }
        for (int d = 0; d < kDrums; ++d) if (outMask & (2u << d)) for (int i = 0; i < n; ++i) { L[i] += drumBufs.getSample(d * 2, i) * F.drumsLevel; R[i] += drumBufs.getSample(d * 2 + 1, i) * F.drumsLevel; }
        for (int i = 0; i < n; ++i) { L[i] = soft(L[i] * F.volume * 1.2f); R[i] = soft(R[i] * F.volume * 1.2f); }
    }

private:
    struct SV { const Break* brk = nullptr; double pos = 0, end = 0, rate = 1; float gain = 0, fadeIn = 0, fade = -1; };
    struct Bay { SV v[2]; int cur = 0; int barOffset = 0; long lastBar = -1; float duck = 1; std::mt19937 rng { 99 }; };
    struct DV { const gb::Sound* s = nullptr; double pos = 0, rate = 1; float gain = 1; int drum = 0, delay = 0; float fade = -1; };
    struct TPT { float s1[2] {}, s2[2] {}, a1 = 0, a2 = 0, a3 = 0, k = 1;
        void setup(float f, float q, float rate) { const float g = std::tan(juce::MathConstants<float>::pi * juce::jlimit(20.0f, rate * 0.45f, f) / rate); k = 2.0f - 1.9f * q; a1 = 1 / (1 + g * (g + k)); a2 = g * a1; a3 = g * a2; }
        // type: 0 low 1 band 2 high 3 notch
        float tick(int c, float x, int type) { const float v3 = x - s2[c], v1 = a1 * s1[c] + a2 * v3, v2 = s2[c] + a2 * s1[c] + a3 * v3; s1[c] = 2 * v1 - s1[c]; s2[c] = 2 * v2 - s2[c];
            switch (type) { case 0: return v2; case 1: return v1 * k; case 2: return x - k * v1 - v2; default: return x - k * v1; } } };

    float lvl[kDrums] { 1, 1, 1, 1 }; bool muted[kDrums] {}; double sr = 48000, clock = 0, curBpm = 172; int maxBlock = 512; long lastStep = -1;
    std::atomic<const Snapshot*> snap { nullptr };
    Bay bays[kBays]; DV dv[48];
    float duckEnv[3] {};   // kick, snare, hats
    juce::AudioBuffer<float> busBuf, drumBufs;
    TPT lp, hp, bp, notch, form[3], stutF;
    std::vector<float> combBuf[2]; int combPos = 0;
    juce::dsp::Chorus<float> chorus; juce::dsp::Reverb reverb;
    std::vector<float> stutBuf[2]; int stutLen = 0, stutRec = 0, stutPos = 0, stutRep = 0; bool stutWas = false; float stutEnvNow = 0;

    static float soft(float x) { return std::abs(x) < 0.9f ? x : std::tanh(x); }
    static float cutHz(float v) { return 30.0f * std::pow(600.0f, juce::jlimit(0.0f, 1.0f, v)); }   // 30 Hz .. 18 kHz

    void stepBay(const Snapshot* S, const BayParams& P, int b, long s, int at) {
        const Break* br = S->brk[(size_t) b]; if (!br || br->data.empty() || P.mute) return;
        auto& bay = bays[b];
        static const int G[] = { 1, 2, 4, 8, 16 };   // grid in 16ths: 1/16, 1/8, 1/4, 1/2, 1 bar
        const int grid = G[juce::jlimit(0, 4, P.grid)];
        const long bar = s / 16;
        if (P.rndStart && bar != bay.lastBar) { std::uniform_int_distribution<int> U(0, juce::jmax(0, br->slices / grid - 1)); bay.barOffset = U(bay.rng) * grid; }
        if (!P.rndStart) bay.barOffset = 0;
        bay.lastBar = bar;
        int slice = (int) (((s + (long) P.nudge * grid + bay.barOffset) % br->slices + br->slices) % br->slices);
        if (P.rndChops) { std::uniform_real_distribution<float> U(0, 1); if (U(bay.rng) < P.rndAmt) slice = (int) (U(bay.rng) * br->slices) % br->slices; }
        uiSlice[b].store(slice);
        // crossfade into the new slice
        auto& old = bay.v[bay.cur]; if (old.brk) old.fade = 1.0f;
        bay.cur ^= 1; auto& v = bay.v[bay.cur];
        const int L = br->sliceLen();
        v.brk = br; v.pos = slice * (double) L; v.end = slice * (double) L + L * juce::jlimit(0.05f, 1.0f, P.decay) + (P.decay >= 0.99f ? L * 0.6 : 0);
        // FIT: re-pitch the break to the song tempo (the classic jungle sound); off = keep its own speed
        v.rate = std::exp2(P.pitch / 12.0) * br->rate / sr * (P.fit ? curBpm / juce::jmax(40.0, br->bpm) : 1.0); v.gain = 1; v.fadeIn = 0; v.fade = -1;
        delayStart[b] = at;
    }
    int delayStart[kBays] {};

    void renderBay(const BayParams& P, int b, int n) {
        auto& bay = bays[b];
        const float pl = std::cos((P.pan + 1) * juce::MathConstants<float>::pi * 0.25f) * 1.414f, pr = std::sin((P.pan + 1) * juce::MathConstants<float>::pi * 0.25f) * 1.414f;
        const int start = delayStart[b]; delayStart[b] = 0;
        for (int k = 0; k < 2; ++k) {
            auto& v = bay.v[k]; if (!v.brk) continue;
            const int len = (int) v.brk->data.size();
            const bool newest = (k == bay.cur);
            for (int i = newest ? start : 0; i < n; ++i) {
                if (v.pos >= v.end || v.pos >= len - 1) { v.brk = nullptr; break; }
                const int idx = (int) v.pos; const float t = (float) (v.pos - idx);
                float x = v.brk->data[(size_t) idx] + (v.brk->data[(size_t) juce::jmin(idx + 1, len - 1)] - v.brk->data[(size_t) idx]) * t;
                float g = v.gain;
                if (v.fadeIn < 1) { v.fadeIn = juce::jmin(1.0f, v.fadeIn + 1.0f / 96.0f); g *= v.fadeIn; }
                if (v.fade >= 0) { v.fade -= 1.0f / 96.0f; if (v.fade <= 0) { v.brk = nullptr; break; } g *= v.fade; }
                const double remain = v.end - v.pos; if (remain < 240) g *= (float) (remain / 240.0);   // tail fade at the slice end
                // per-element sidechain: only this bay ducks
                const float duck = (1 - P.duck[0] * duckCurve[0][(size_t) i]) * (1 - P.duck[1] * duckCurve[1][(size_t) i]) * (1 - P.duck[2] * duckCurve[2][(size_t) i]);
                x *= g * P.level * duck;
                busBuf.addSample(0, i, x * pl); busBuf.addSample(1, i, x * pr);
                v.pos += v.rate;
            }
        }
    }
    // per-element sidechain envelopes for this block: a fast 2 ms attack on each hit, then the release
    std::vector<float> duckCurve[3]; std::vector<uint8_t> duckHit[3]; int duckAtk[3] {};
    void buildDucks(int n, float rel) {
        const float atk = 1.0f - std::exp(-1.0f / (0.002f * (float) sr));
        for (int e = 0; e < 3; ++e) {
            float env = duckEnv[e];
            for (int i = 0; i < n; ++i) {
                if (duckHit[e][(size_t) i]) { duckAtk[e] = (int) (0.004 * sr); duckHit[e][(size_t) i] = 0; }
                if (duckAtk[e] > 0) { env += (1 - env) * atk; --duckAtk[e]; } else env *= rel;
                duckCurve[e][(size_t) i] = env;
            }
            duckEnv[e] = env;
        }
    }

    void trigDrum(const Snapshot* S, int d, float vel, int at) {
        if (muted[d]) return;   // a muted drum neither plays nor ducks
        const gb::Sound* s = S->drumSound[(size_t) d]; if (!s || s->data.empty()) return;
        drumHits[d].fetch_add(1);
        const int e = d == 0 ? 0 : (d == 1 ? 1 : 2);
        duckHit[e][(size_t) at] = 1;
        if (d == 2) for (auto& v : dv) if (v.s && v.drum == 3 && v.fade < 0) v.fade = 1.0f;   // closed hat chokes the open hat
        DV* pick = &dv[0]; for (auto& v : dv) { if (!v.s) { pick = &v; break; } if (v.pos > pick->pos) pick = &v; }
        *pick = { s, 0, s->rate / sr, (0.3f + 0.7f * vel) * lvl[d], d, at, -1 };
    }
    void renderDrums(int n) {
        for (auto& v : dv) {
            if (!v.s) continue; const int len = (int) v.s->data.size();
            for (int i = v.delay; i < n; ++i) {
                if (v.pos >= len - 1) { v.s = nullptr; break; }
                float g = v.gain; if (v.fade >= 0) { v.fade -= 1.0f / 200.0f; if (v.fade <= 0) { v.s = nullptr; break; } g *= v.fade; }
                const int k = (int) v.pos; const float t = (float) (v.pos - k); const float x = (v.s->data[(size_t) k] + (v.s->data[(size_t) k + 1] - v.s->data[(size_t) k]) * t) * g;
                drumBufs.addSample(v.drum * 2, i, x); drumBufs.addSample(v.drum * 2 + 1, i, x); v.pos += v.rate;
            }
            v.delay = 0;
        }
    }

    void breakFx(const FxParams& F, int n, double spb) {
        float* L = busBuf.getWritePointer(0); float* R = busBuf.getWritePointer(1);
        // CRUNCH: drive + bit crush
        if (F.crunch > 0.01f || F.bits > 0.01f) {
            const float g = 1 + F.crunch * F.crunch * 18, c = 1 / (1 + F.crunch * 1.3f), q = std::exp2(14.0f - F.bits * 11.0f);
            for (int i = 0; i < n; ++i) { L[i] = std::tanh(L[i] * g) * c; R[i] = std::tanh(R[i] * g) * c; if (F.bits > 0.01f) { L[i] = std::round(L[i] * q) / q; R[i] = std::round(R[i] * q) / q; } }
        }
        // PARALLEL FILTERS: each has its own mix; the dry knob sets how much untouched break stays
        const bool anyF = F.lpMix + F.hpMix + F.bpMix + F.notchMix + F.combMix + F.formMix > 0.01f;
        if (anyF) {
            lp.setup(cutHz(F.lpCut), F.res, (float) sr); hp.setup(cutHz(F.hpCut), F.res, (float) sr); bp.setup(cutHz(F.bpCut), F.res, (float) sr); notch.setup(cutHz(F.notchCut), F.res * 0.8f, (float) sr);
            static const float FORM[5][3] = { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 300, 2300, 3000 }, { 450, 800, 2830 }, { 325, 700, 2530 } };
            const float fp = juce::jlimit(0.0f, 1.0f, F.formant) * 3.999f; const int fa = (int) fp; const float ft = fp - fa;
            for (int j = 0; j < 3; ++j) form[j].setup(FORM[fa][j] + (FORM[fa + 1][j] - FORM[fa][j]) * ft, 0.85f, (float) sr);
            const int N = (int) combBuf[0].size(); const float combHz = 40.0f * std::pow(40.0f, F.combCut); const float cd = juce::jlimit(2.0f, (float) N - 2, (float) sr / combHz);
            for (int i = 0; i < n; ++i) {
                for (int c = 0; c < 2; ++c) {
                    float* X = c ? R : L; const float x = X[i];
                    float y = x * F.dry;
                    if (F.lpMix > 0.01f) y += lp.tick(c, x, 0) * F.lpMix;
                    if (F.hpMix > 0.01f) y += hp.tick(c, x, 2) * F.hpMix;
                    if (F.bpMix > 0.01f) y += bp.tick(c, x, 1) * F.bpMix;
                    if (F.notchMix > 0.01f) y += notch.tick(c, x, 3) * F.notchMix;
                    if (F.combMix > 0.01f) { auto& B = combBuf[c]; float rp = combPos - cd; if (rp < 0) rp += N; const int r0 = (int) rp; const float t = rp - r0;
                        const float dd = B[(size_t) r0] + (B[(size_t) ((r0 + 1) % N)] - B[(size_t) r0]) * t; const float cy = std::tanh(x + dd * (0.55f + F.res * 0.4f)); B[(size_t) combPos] = cy; y += cy * F.combMix * 0.7f; }
                    if (F.formMix > 0.01f) { float s = 0; for (int j = 0; j < 3; ++j) s += form[j].tick(c, x, 1) * (j == 0 ? 1.0f : (j == 1 ? 0.5f : 0.2f)); y += s * F.formMix * 1.5f; }
                    X[i] = y;
                }
                combPos = (combPos + 1) % N;
            }
        } else if (F.dry < 0.99f) busBuf.applyGain(0, n, F.dry);
        // CHORUS
        if (F.chorus > 0.01f) { chorus.setMix(F.chorus * 0.6f); chorus.setDepth(0.25f + F.chorus * 0.5f); juce::dsp::AudioBlock<float> blk(busBuf.getArrayOfWritePointers(), 2, (size_t) n); chorus.process(juce::dsp::ProcessContextReplacing<float>(blk)); }
        // STUTTER: grab a slice and repeat it; each repeat re-triggers its own filter envelope
        if (F.stutter) {
            static const double D[] = { 0.5, 0.25, 0.125, 0.0625, 1.0 / 6.0 };   // 1/8, 1/16, 1/32, 1/64, 1/16T
            if (!stutWas) { stutLen = juce::jlimit(32, (int) stutBuf[0].size(), (int) (spb * D[juce::jlimit(0, 4, F.stutDiv)])); stutRec = 0; stutPos = 0; stutRep = 0; stutEnvNow = 1; stutWas = true; }
            const float decay = std::exp(-1.0f / (juce::jmax(0.01f, F.stutDecay * 0.3f) * (float) sr));
            for (int i = 0; i < n; ++i) {
                float a = L[i], b = R[i];
                if (stutRec < stutLen) { stutBuf[0][(size_t) stutRec] = a; stutBuf[1][(size_t) stutRec] = b; ++stutRec; }
                else { const float w = juce::jmin(1.0f, juce::jmin((float) stutPos, (float) (stutLen - stutPos)) / 48.0f); a = stutBuf[0][(size_t) stutPos] * w; b = stutBuf[1][(size_t) stutPos] * w;
                       if (++stutPos >= stutLen) { stutPos = 0; stutEnvNow = 1; } }
                stutEnvNow *= decay;
                if ((i & 15) == 0) stutF.setup(cutHz(juce::jlimit(0.0f, 1.0f, F.stutCut + F.stutEnv * stutEnvNow * 0.5f)), 0.6f, (float) sr);
                L[i] = stutF.tick(0, a, 0); R[i] = stutF.tick(1, b, 0);
            }
        } else stutWas = false;
        // REVERB
        if (F.reverb > 0.01f) { juce::dsp::Reverb::Parameters rp; rp.roomSize = 0.4f + F.size * 0.58f; rp.damping = 0.45f; rp.wetLevel = F.reverb * 0.5f; rp.dryLevel = 1; rp.width = 1;
            reverb.setParameters(rp); juce::dsp::AudioBlock<float> blk(busBuf.getArrayOfWritePointers(), 2, (size_t) n); reverb.process(juce::dsp::ProcessContextReplacing<float>(blk)); }
    }
};

} // namespace mk
