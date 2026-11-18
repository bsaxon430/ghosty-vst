#pragma once
// Ghosty Spectrum: the PAD painter's sound.
//   Every painted dot is a ringing voice. y = pitch, x = tone colour (left = pure, right = bright / driven).
//   Brushes (voice types): BELL (inharmonic partials that ring and decay long), PIANO (stiff-string partials, strings ring
//   each other when harmonically related), PAD (bowed swell that holds) and FEEDBACK (blooms and creeps up the harmonic
//   series like a guitar near the amp).
//   Then the pad bus: gentle drive -> tape delay -> shimmer reverb (with FREEZE) + open "pedal" strings tuned to the key.
// The UI writes dots into fixed PadSlots (atomics); the audio thread matches voices to slots by id. No locks, no allocation
// on the audio thread (everything is sized in prepare()), click-free ramps on every voice start / stop / re-strike.
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <array>
#include <cmath>
#include <vector>

namespace sp {

constexpr int kMaxDots = 32, kPadVoices = 64, kPartials = 10;
enum PadVoiceType { VBell = 0, VPiano, VPad, VFeedback, kVoiceTypes };
inline const char* voiceName(int v) { static const char* N[] = { "BELL", "PIANO", "PAD", "FEEDBACK" }; return N[juce::jlimit(0, 3, v)]; }
inline bool voiceRings(int v) { return v == VBell || v == VPiano; }   // decays (re-struck each loop) vs holds

struct PadSlot {
    std::atomic<uint32_t> id { 0 };                  // 0 = empty
    std::atomic<float> pitch { 60 }, tone { 0.3f }, gain { 0.8f };
    std::atomic<int> type { 0 };
    std::atomic<float> level { 0 };                  // audio -> UI: how loud this dot rings right now
};

struct PadParams {
    float bloom = 0.45f, space = 0.6f, shimmer = 0.35f, tape = 0.25f, drive = 0.15f, pedal = 0.3f, drift = 0.3f, level = 0.75f, strum = 0.3f;
    bool hold = true, freeze = false;
    int root = 0; int scale = 1; double bpm = 120;
};

// ---- the partial recipe for one dot (shared with the UI so the drawn overtones match what you hear)
struct PartialSet { int count = 0; float ratio[kPartials] {}, amp[kPartials] {}, decay[kPartials] {}; };
inline PartialSet designPartials(int type, float t, float hz) {
    PartialSet P; t = juce::jlimit(0.0f, 1.0f, t);
    auto add = [&P](float r, float a, float d) { if (P.count < kPartials && a > 0.0005f) { P.ratio[P.count] = r; P.amp[P.count] = a; P.decay[P.count] = d; ++P.count; } };
    const float ds = juce::jlimit(0.35f, 2.2f, std::pow(hz / 261.6f, -0.35f));
    switch (type) {
        case VBell: {   // left: glass / tubular (1, 2.76, 5.40), right: a full church bell (hum, tierce, quint, nominal...)
            static const float r[]  { 1.0f, 2.0f, 2.76f, 0.5f, 1.19f, 1.5f, 3.0f, 4.07f, 5.40f, 6.8f };
            static const float a0[] { 1.0f, 0.12f, 0.30f, 0.08f, 0.0f, 0.0f, 0.0f, 0.0f, 0.08f, 0.0f };
            static const float a1[] { 1.0f, 0.65f, 0.45f, 0.5f, 0.42f, 0.32f, 0.36f, 0.28f, 0.26f, 0.18f };
            static const float d[]  { 9.0f, 6.0f, 5.0f, 11.0f, 4.5f, 4.0f, 3.0f, 2.4f, 1.8f, 1.3f };
            for (int k = 0; k < 10; ++k) add(r[k], a0[k] + (a1[k] - a0[k]) * t, d[k] * ds);
        } break;
        case VPiano: {  // stiff strings: slightly sharp overtones; left = soft felt hammer, right = hard bright hammer
            const float B = 0.00035f * juce::jlimit(0.5f, 3.0f, hz / 261.6f), p = 2.6f - 1.5f * t, base = 7.5f * juce::jlimit(0.3f, 2.5f, std::pow(hz / 261.6f, -0.55f));
            for (int k = 1; k <= 10; ++k) add(k * std::sqrt(1.0f + B * k * k), std::pow((float) k, -p) * (0.35f + 0.65f * std::abs(std::sin(juce::MathConstants<float>::pi * k * 0.13f + 0.4f))), base / (1.0f + 0.4f * (k - 1)));
        } break;
        case VPad: {    // bowed swell: harmonic, warm sub on the left
            add(1.0f, 1.0f, 0);
            for (int k = 2; k <= 9; ++k) add((float) k, (0.06f + 0.94f * t) * std::pow((float) k, -(2.3f - 1.3f * t)), 0);
            add(0.5f, 0.28f * (1.0f - t), 0);
        } break;
        default: {      // feedback: the amps get re-weighted live as it creeps up the series
            for (int k = 1; k <= 10; ++k) add((float) k, std::pow((float) k, -(1.5f - 0.6f * t)), 0);
        } break;
    }
    return P;
}
inline float voiceDrive(int type, float t) {
    switch (type) {
        case VBell: case VPiano: return 1.0f + 2.5f * juce::jmax(0.0f, (t - 0.6f) / 0.4f);
        case VPad: return 1.0f + 6.0f * std::pow(juce::jmax(0.0f, (t - 0.55f) / 0.45f), 1.5f);
        default: return 1.6f + 5.0f * t;
    }
}
// how strongly two pitches share overtones (0..1): drives sympathetic ringing and the "calm" glow
inline float consonance(float hz1, float hz2) {
    float c = 0;
    for (int a = 1; a <= 6; ++a) for (int b = 1; b <= 6; ++b) {
        const float d = std::abs(12.0f * std::log2((a * hz1) / (b * hz2)));
        if (d < 0.3f) c = juce::jmax(c, (1.0f - d / 0.3f) / std::sqrt((float) (a * b)));
    }
    return c;
}

inline float fastTanh(float x) { x = juce::jlimit(-3.0f, 3.0f, x); return x * (27.0f + x * x) / (27.0f + 9.0f * x * x); }

class PadSynth {
public:
    void prepare(double rate, int maxBlock) {
        sr = (float) rate; invSr = 1.0f / sr;
        sineTable();   // build once
        for (auto& v : voices) v = Voice();
        dry.setSize(2, maxBlock); wet.setSize(2, maxBlock); shim.setSize(2, maxBlock); shimPrev.setSize(2, maxBlock); shimPrev.clear();
        juce::dsp::ProcessSpec spec { rate, (juce::uint32) maxBlock, 2 };
        reverb.prepare(spec); reverb.reset();
        tapeLen = (int) (rate * 2.6) + 8; tapeBuf.assign((size_t) tapeLen * 2, 0.0f); tapeW = 0; tapeTime = (float) (rate * 0.375);
        for (auto& o : oct) { std::fill(std::begin(o.buf), std::end(o.buf), 0.0f); o.w = 0; o.p = 0; }
        strLen = (int) (rate / 30.0) + 4; strBuf.assign((size_t) strLen * kStrings, 0.0f); for (auto& s : strings) s = Str(); strW = 0; tunedRoot = -1;
        tapeLp[0] = tapeLp[1] = 0; lfo1 = lfo2 = 0; rvKey = -1; dryEnv = 0; fbGain = 1; limEnv = 0;
        limAtk = 1.0f - std::exp(-1.0f / (0.004f * sr)); limRel = std::exp(-1.0f / (0.35f * sr));
    }

    // adds the pad into L/R. slots: the dots. gate: hold or a MIDI key down. transpose in semitones.
    // restrikeAt: sample offset where the loop wrapped this block (-1 = none); midiStrike: a key was pressed (velocity 0..1, 0 = none)
    void process(float* L, float* R, int n, PadSlot* slots, const PadParams& P, float transpose, bool gate, int restrikeAt, float midiStrike) {
        n = juce::jmin(n, dry.getNumSamples());
        dry.clear(0, n);
        // 1. read the dots
        uint32_t ids[kMaxDots]; float pit[kMaxDots], ton[kMaxDots], gn[kMaxDots]; int typ[kMaxDots];
        for (int s = 0; s < kMaxDots; ++s) { ids[s] = slots[s].id.load(); pit[s] = slots[s].pitch.load(); ton[s] = slots[s].tone.load(); gn[s] = slots[s].gain.load(); typ[s] = slots[s].type.load(); }
        // 2. existing voices follow their dot, or let go
        for (auto& v : voices) {
            if (v.state != 1) continue;
            int s = (v.slot >= 0 && ids[v.slot] == v.id) ? v.slot : -1;
            if (s < 0) for (int k = 0; k < kMaxDots; ++k) if (ids[k] == v.id) { s = k; break; }
            if (s < 0 || !gate || typ[s] != v.type) { v.state = 2; continue; }
            v.slot = s; v.fTarget = hzOf(pit[s] + transpose); v.toneTarget = ton[s]; v.gain = gn[s];
        }
        // 3. new dots start a voice (and ring their harmonic neighbours)
        if (gate) for (int s = 0; s < kMaxDots; ++s) {
            if (ids[s] == 0) continue;
            bool has = false; for (auto& v : voices) if (v.state == 1 && v.id == ids[s]) { has = true; break; }
            if (has) continue;
            Voice* nv = nullptr; for (auto& v : voices) if (v.state == 0) { nv = &v; break; }
            if (!nv) { for (auto& v : voices) if (v.state == 2 && (!nv || v.env < nv->env)) nv = &v; }
            if (!nv) break;
            startVoice(*nv, ids[s], s, typ[s], hzOf(pit[s] + transpose), ton[s], gn[s]);
            for (auto& o : voices) if (&o != nv && o.state == 1 && voiceRings(o.type)) { const float c = consonance(nv->fTarget, o.fTarget); if (c > 0.05f) strike(o, 0.35f * c, 0); }
        }
        // 4. the loop came round (or a key was pressed): ring the bells / strings again, strummed low -> high
        if (restrikeAt >= 0 || midiStrike > 0) {
            const float lvl = midiStrike > 0 ? 0.5f + 0.5f * midiStrike : 1.0f;
            for (auto& v : voices) if (v.state == 1 && voiceRings(v.type)) {
                int rank = 0; for (auto& o : voices) if (o.state == 1 && voiceRings(o.type) && o.fTarget < v.fTarget) ++rank;
                strike(v, lvl, juce::jmax(0, restrikeAt) + (int) (rank * P.strum * 0.11f * sr));
            }
        }
        // 5. render
        const float atkPad = 0.08f + P.bloom * P.bloom * 5.0f, relPad = 0.35f + P.bloom * 7.0f;
        for (auto& v : voices) if (v.state != 0) renderVoice(v, n, P, atkPad, relPad);
        // 6. tell the UI how loud each dot is
        for (int s = 0; s < kMaxDots; ++s) {
            float lv = 0; for (auto& v : voices) if (v.state == 1 && v.slot == s && v.id == ids[s]) lv = juce::jmax(lv, v.meter);
            slots[s].level.store(lv);
        }
        fx(n, P);
        const float g = P.level * 1.1f;
        for (int i = 0; i < n; ++i) { L[i] += dry.getSample(0, i) * g; R[i] += dry.getSample(1, i) * g; }
    }
    int activeVoices() const { int c = 0; for (auto& v : voices) c += v.state != 0; return c; }

private:
    struct Voice {
        uint32_t id = 0; int slot = -1, type = 0, state = 0;   // 0 free, 1 on, 2 releasing
        float env = 0, f = 440, fTarget = 440, tone = 0.3f, toneTarget = 0.3f, gain = 0.8f, designedTone = -1, designedF = 0;
        PartialSet P; float base[kPartials] {}, penv[kPartials] {}, pstart[kPartials] {}, dcoef[kPartials] {}, phase[kPartials] {}, pl[kPartials] {}, pr[kPartials] {};
        float norm = 1, drive = 1, age = 0, lfoPh = 0, lfoRate = 0.2f, pan = 0, meter = 0, tilt = 1;
        int ramp = 0, rampLen = 96, strikeDelay = -1; float rampLevel = 1, pendingLevel = 0;
    };
    static constexpr int kTable = 4096;
    static const float* sineTable() {
        static const std::vector<float> t = [] { std::vector<float> v(kTable + 1); for (int i = 0; i <= kTable; ++i) v[(size_t) i] = (float) std::sin(juce::MathConstants<double>::twoPi * i / kTable); return v; }();
        return t.data();
    }
    static inline float sinT(const float* T, float ph) { const float x = ph * kTable; const int i = (int) x; const float f = x - (float) i; return T[i] + (T[i + 1] - T[i]) * f; }
    float hzOf(float midi) const { return 440.0f * std::exp2((midi - 69.0f) / 12.0f); }

    void startVoice(Voice& v, uint32_t id, int slot, int type, float hz, float tone, float gain) {
        v = Voice(); v.id = id; v.slot = slot; v.type = type; v.state = 1; v.f = v.fTarget = hz; v.tone = v.toneTarget = tone; v.gain = gain;
        juce::Random r((juce::int64) id * 7919 + 17);
        v.lfoRate = 0.07f + r.nextFloat() * 0.25f; v.lfoPh = r.nextFloat(); v.pan = (r.nextFloat() - 0.5f) * 0.9f;
        for (int k = 0; k < kPartials; ++k) v.phase[k] = r.nextFloat();
        redesign(v);
        if (voiceRings(type)) { for (int k = 0; k < kPartials; ++k) v.penv[k] = 0; strike(v, 1.0f, 0); }
        else for (int k = 0; k < kPartials; ++k) v.penv[k] = 1;
    }
    void strike(Voice& v, float level, int delaySamples) {
        if (delaySamples > 0) { v.strikeDelay = delaySamples; v.pendingLevel = juce::jmax(v.pendingLevel, level); return; }
        float cur = 0; for (int k = 0; k < v.P.count; ++k) cur = juce::jmax(cur, v.penv[k]);
        if (level <= cur * 0.9f && v.ramp == 0) return;
        for (int k = 0; k < kPartials; ++k) v.pstart[k] = v.penv[k];
        v.rampLevel = level; v.rampLen = juce::jmax(16, (int) (sr * 0.0025f)); v.ramp = v.rampLen;
    }
    void redesign(Voice& v) {
        v.P = designPartials(v.type, v.tone, v.f); v.designedTone = v.tone; v.designedF = v.f;
        float s2 = 0;
        for (int k = 0; k < v.P.count; ++k) {
            const bool ok = v.P.ratio[k] * v.f < sr * 0.45f;
            v.base[k] = ok ? v.P.amp[k] : 0.0f; s2 += v.base[k] * v.base[k];
            v.dcoef[k] = v.P.decay[k] > 0 ? std::exp(-1.0f / (v.P.decay[k] * sr)) : 1.0f;
            const float pp = juce::jlimit(-1.0f, 1.0f, v.pan + (k % 2 ? 0.35f : -0.35f) * juce::jmin(1.0f, k * 0.5f));
            v.pl[k] = std::cos((pp + 1) * juce::MathConstants<float>::pi * 0.25f); v.pr[k] = std::sin((pp + 1) * juce::MathConstants<float>::pi * 0.25f);
        }
        v.norm = 1.0f / std::sqrt(juce::jmax(1e-6f, s2)); v.drive = voiceDrive(v.type, v.tone);
        v.tilt = 1.0f / (1.0f + std::pow(v.f / 2200.0f, 0.8f));
    }
    void renderVoice(Voice& v, int n, const PadParams& P, float atkPad, float relPad) {
        const float* T = sineTable();
        // per-block: tone / pitch tracking, envelopes, feedback creep, drift
        v.tone += (v.toneTarget - v.tone) * 0.25f;
        if (std::abs(v.tone - v.designedTone) > 0.01f || std::abs(v.f / juce::jmax(1.0f, v.designedF) - 1.0f) > 0.03f) {
            float keep[kPartials]; std::copy(std::begin(v.penv), std::end(v.penv), keep); redesign(v); std::copy(std::begin(keep), std::end(keep), v.penv);
        }
        const float bt = n * invSr; v.age += bt;
        float atk = 0.002f, rel = 0.5f + P.bloom * 2.5f;
        if (v.type == VPad) { atk = atkPad; rel = relPad; } else if (v.type == VFeedback) { atk = 0.15f + P.bloom * 2.5f; rel = 0.4f + P.bloom * 4.0f; }
        const float aC = 1.0f - std::exp(-1.0f / (atk * sr)), rC = std::exp(-1.0f / (rel * sr));
        float amp[kPartials];
        for (int k = 0; k < v.P.count; ++k) amp[k] = v.base[k] * v.norm;
        float drive = v.drive;
        if (v.type == VFeedback) {   // blooms, then climbs: 1st -> 2nd -> 3rd harmonic, vibrato and drive growing
            const float t = v.tone, climb = 7.0f - 4.5f * t, h = 1.0f + juce::jmin(1.0f + 3.0f * t, v.age / climb * (1.0f + 3.0f * t));
            float s2 = 0;
            for (int k = 0; k < v.P.count; ++k) { const float d = (float) (k + 1) - h; amp[k] = v.base[k] * (0.3f + 1.6f * std::exp(-d * d / 0.5f)); s2 += amp[k] * amp[k]; }
            const float nn = 1.0f / std::sqrt(juce::jmax(1e-6f, s2)); for (int k = 0; k < v.P.count; ++k) amp[k] *= nn;
            drive *= 1.0f + juce::jmin(1.5f, v.age * 0.25f);
        }
        v.lfoPh += v.lfoRate * bt; if (v.lfoPh > 1) v.lfoPh -= 1;
        float vib = std::sin(v.lfoPh * juce::MathConstants<float>::twoPi) * P.drift * 0.005f;
        if (v.type == VFeedback) vib += std::sin(v.age * 31.0f) * juce::jmin(0.006f, v.age * 0.0012f) * (0.4f + v.tone);
        const float fMul = 1.0f + vib, glideC = 1.0f - std::exp(-1.0f / (0.035f * sr));
        const float vg = 0.24f * v.gain * v.tilt * (v.type == VPiano ? 1.25f : v.type == VBell ? 1.05f : v.type == VFeedback ? 0.8f : 1.0f);
        const float dn = 1.0f / fastTanh(drive);
        float* oL = dry.getWritePointer(0); float* oR = dry.getWritePointer(1); float peak = 0;
        const bool rings = voiceRings(v.type);
        for (int i = 0; i < n; ++i) {
            if (v.strikeDelay >= 0 && v.strikeDelay-- == 0) { strike(v, v.pendingLevel, 0); v.pendingLevel = 0; v.strikeDelay = -1; }
            if (v.state == 1) v.env += (1.02f - v.env) * aC; else v.env *= rC;
            if (v.env > 1) v.env = 1;
            v.f += (v.fTarget - v.f) * glideC;
            const float inc = v.f * fMul * invSr;
            float l = 0, r = 0;
            if (v.ramp > 0) { const float a = 1.0f - (float) (--v.ramp) / (float) v.rampLen; for (int k = 0; k < v.P.count; ++k) v.penv[k] = v.pstart[k] + (v.rampLevel - v.pstart[k]) * a; }
            for (int k = 0; k < v.P.count; ++k) {
                float& ph = v.phase[k]; ph += inc * v.P.ratio[k]; if (ph >= 1.0f) ph -= (float) (int) ph;
                const float s = sinT(T, ph) * amp[k] * v.penv[k];
                if (rings && v.ramp == 0) v.penv[k] *= v.dcoef[k];
                l += s * v.pl[k]; r += s * v.pr[k];
            }
            if (drive > 1.02f) { l = fastTanh(l * drive) * dn; r = fastTanh(r * drive) * dn; }
            const float g = v.env * vg; l *= g; r *= g;
            oL[i] += l; oR[i] += r; peak = juce::jmax(peak, std::abs(l) + std::abs(r));
        }
        v.meter = juce::jmin(1.0f, peak * 3.0f);
        if (rings && v.state == 1) { float mx = 0; for (int k = 0; k < v.P.count; ++k) mx = juce::jmax(mx, v.penv[k] * v.base[k]); if (mx < 0.0005f && v.ramp == 0 && v.strikeDelay < 0) v.meter = 0; }
        if (v.state == 2 && v.env < 1e-4f) v.state = 0;
    }

    // ---------------- the pad bus effects
    struct Oct {   // octave-up pitch shifter for the shimmer (two crossfaded taps)
        static constexpr int N = 8192; float buf[N] {}; int w = 0; float p = 0;
        float tick(float x, float win) {
            buf[w] = x; w = (w + 1) & (N - 1);
            p -= 1.0f / win; if (p < 0) p += 1.0f;
            auto tap = [&](float ph) { const float d = ph * win + 2; float rp = (float) w - d; while (rp < 0) rp += N; const int i = (int) rp; const float f = rp - i; const float a = buf[i & (N - 1)], b = buf[(i + 1) & (N - 1)]; const float s = std::sin(ph * juce::MathConstants<float>::pi); return (a + (b - a) * f) * s * s; };
            float q = p + 0.5f; if (q >= 1) q -= 1;
            return tap(p) + tap(q);
        }
    };
    static constexpr int kStrings = 22;
    struct Str { float delay = 100, lp = 0, g = 0.995f; };
    void tuneStrings(int root, int scale) {
        static const int S[][12] = { { 0, 2, 4, 5, 7, 9, 11, -1 }, { 0, 2, 3, 5, 7, 8, 10, -1 }, { 0, 2, 3, 5, 7, 8, 11, -1 }, { 0, 2, 3, 5, 7, 9, 10, -1 }, { 0, 3, 5, 7, 10, -1 }, { 0, 2, 4, 7, 9, -1 }, { 0, 2, 4, 5, 7, 9, 11, -1 } };
        const int* sc = S[juce::jlimit(0, 6, scale)]; int c = 0;
        for (int m = 36; m <= 84 && c < kStrings; ++m) {
            const int rel = ((m - root) % 12 + 12) % 12; bool in = false; for (int k = 0; k < 12 && sc[k] >= 0; ++k) if (sc[k] == rel) in = true;
            if (!in) continue;
            const float hz = hzOf((float) m); strings[(size_t) c].delay = juce::jlimit(2.0f, (float) strLen - 3, sr / hz); strings[(size_t) c].g = 0.9985f - 0.0025f * (m - 36) / 48.0f; ++c;
        }
        for (; c < kStrings; ++c) strings[(size_t) c].delay = 0;
        tunedRoot = root; tunedScale = scale;
    }
    void fx(int n, const PadParams& P) {
        float* a = dry.getWritePointer(0); float* b = dry.getWritePointer(1);
        // drive
        if (P.drive > 0.01f) { const float d = 1.0f + P.drive * 5.0f, mk = (1.0f + 0.6f * P.drive) / d; for (int i = 0; i < n; ++i) { a[i] = fastTanh(a[i] * d) * mk; b[i] = fastTanh(b[i] * d) * mk; } }
        // open strings (pedal down): resonators tuned to the key, rung by the pad
        if (tunedRoot != P.root || tunedScale != P.scale) tuneStrings(P.root, P.scale);
        if (P.pedal > 0.01f) {
            for (int i = 0; i < n; ++i) {
                const float in = (a[i] + b[i]) * 0.5f * 0.0025f; float oa = 0, ob = 0;
                for (int s = 0; s < kStrings; ++s) {
                    auto& S = strings[(size_t) s]; if (S.delay <= 0) continue;
                    float* buf = strBuf.data() + (size_t) s * (size_t) strLen;
                    float rp = (float) strW - S.delay; if (rp < 0) rp += (float) strLen; const int k = (int) rp; const float f = rp - k;
                    const float y = buf[k] + (buf[(k + 1) % strLen] - buf[k]) * f;
                    S.lp += (y - S.lp) * 0.55f;
                    buf[strW] = in + S.lp * S.g;
                    if (s & 1) oa += y; else ob += y;
                }
                strW = (strW + 1) % strLen;
                a[i] += oa * P.pedal * 0.8f; b[i] += ob * P.pedal * 0.8f;
            }
        }
        // tape delay: dotted 1/8, ping-pong, wow + flutter, darker and dirtier each repeat
        if (P.tape > 0.01f) {
            const float target = juce::jlimit(0.05f, 2.4f, (float) (60.0 / juce::jmax(30.0, P.bpm) * 0.75)) * sr, fb = 0.2f + 0.5f * P.tape, mix = 0.25f + 0.35f * P.tape;
            for (int i = 0; i < n; ++i) {
                tapeTime += (target - tapeTime) * 0.0002f;
                lfo1 += 0.55f * invSr; if (lfo1 > 1) lfo1 -= 1; lfo2 += 5.3f * invSr; if (lfo2 > 1) lfo2 -= 1;
                const float d = tapeTime + sr * (0.0022f * std::sin(lfo1 * 6.2831853f) + 0.00035f * std::sin(lfo2 * 6.2831853f));
                float rp = (float) tapeW - d; while (rp < 0) rp += (float) tapeLen; const int k = (int) rp; const float f = rp - k; const int k1 = (k + 1) % tapeLen;
                const float yl = tapeBuf[(size_t) k * 2] + (tapeBuf[(size_t) k1 * 2] - tapeBuf[(size_t) k * 2]) * f;
                const float yr = tapeBuf[(size_t) k * 2 + 1] + (tapeBuf[(size_t) k1 * 2 + 1] - tapeBuf[(size_t) k * 2 + 1]) * f;
                tapeLp[0] += (yl - tapeLp[0]) * 0.32f; tapeLp[1] += (yr - tapeLp[1]) * 0.32f;
                tapeBuf[(size_t) tapeW * 2] = fastTanh(a[i] * 0.7f + tapeLp[1] * fb); tapeBuf[(size_t) tapeW * 2 + 1] = fastTanh(b[i] * 0.3f + tapeLp[0] * fb);
                tapeW = (tapeW + 1) % tapeLen;
                a[i] += tapeLp[0] * mix; b[i] += tapeLp[1] * mix;
            }
        }
        // shimmer reverb (+ freeze)
        const int key = (int) (P.space * 100) * 4 + (P.freeze ? 1 : 0);
        if (key != rvKey) { juce::dsp::Reverb::Parameters rp; rp.roomSize = 0.72f + 0.27f * P.space; rp.damping = 0.32f; rp.wetLevel = 1.0f; rp.dryLevel = 0.0f; rp.width = 1.0f; rp.freezeMode = P.freeze ? 1.0f : 0.0f; reverb.setParameters(rp); rvKey = key; }
        const float send = 0.35f + 0.65f * P.space, sfb = P.shimmer * 0.2f;
        // shimmer feedback with a level guard: the octave-up loop may never grow louder than (a slow memory of) what goes in
        {   const float dr = blockRms(dry, n) * send, fr = blockRms(shimPrev, n) * sfb;
            dryEnv = juce::jmax(dr, dryEnv * std::exp(-(float) n / (3.0f * sr)));
            const float limit = 0.5f * dryEnv, want = fr > limit && fr > 1e-9f ? limit / fr : 1.0f;
            fbGain += (want - fbGain) * (want < fbGain ? 0.5f : 0.05f); }
        for (int c = 0; c < 2; ++c) { const float* d = dry.getReadPointer(c); const float* sp = shimPrev.getReadPointer(c); float* w = wet.getWritePointer(c); for (int i = 0; i < n; ++i) w[i] = d[i] * send + fastTanh(sp[i] * sfb * fbGain); }
        juce::dsp::AudioBlock<float> wb(wet.getArrayOfWritePointers(), 2, (size_t) n); reverb.process(juce::dsp::ProcessContextReplacing<float>(wb));
        const float win = sr * 0.06f;
        for (int c = 0; c < 2; ++c) { const float* w = wet.getReadPointer(c); float* s = shim.getWritePointer(c); for (int i = 0; i < n; ++i) s[i] = oct[c].tick(w[i], win); }
        shimPrev.copyFrom(0, 0, shim, 0, 0, n); shimPrev.copyFrom(1, 0, shim, 1, 0, n);
        const float dryG = 1.0f - 0.35f * P.space, wetG = 0.12f + 0.4f * P.space, shG = P.shimmer * 0.2f;
        for (int i = 0; i < n; ++i) {
            a[i] = a[i] * dryG + wet.getSample(0, i) * wetG + shim.getSample(0, i) * shG * fbGain;
            b[i] = b[i] * dryG + wet.getSample(1, i) * wetG + shim.getSample(1, i) * shG * fbGain;
            // pad bus limiter (-5 dBFS): many bright dots + every effect stay musical instead of slamming the output
            const float pk = juce::jmax(std::abs(a[i]), std::abs(b[i]));
            limEnv = pk > limEnv ? limEnv + (pk - limEnv) * limAtk : limEnv * limRel;
            const float g = limEnv > 0.56f ? 0.56f / limEnv : 1.0f; a[i] *= g; b[i] *= g;
        }
    }

    static float blockRms(const juce::AudioBuffer<float>& b, int n) { double e = 0; for (int c = 0; c < 2; ++c) { const float* x = b.getReadPointer(c); for (int i = 0; i < n; ++i) e += (double) x[i] * x[i]; } return (float) std::sqrt(e / juce::jmax(1, 2 * n)); }
    float sr = 48000, invSr = 1.0f / 48000, dryEnv = 0, fbGain = 1, limEnv = 0, limAtk = 0.1f, limRel = 0.9999f;
    std::array<Voice, kPadVoices> voices;
    juce::AudioBuffer<float> dry, wet, shim, shimPrev;
    juce::dsp::Reverb reverb; int rvKey = -1;
    std::vector<float> tapeBuf; int tapeLen = 0, tapeW = 0; float tapeTime = 0, tapeLp[2] {}, lfo1 = 0, lfo2 = 0;
    Oct oct[2];
    std::array<Str, kStrings> strings; std::vector<float> strBuf; int strLen = 0, strW = 0, tunedRoot = -1, tunedScale = -1;
};

} // namespace sp
