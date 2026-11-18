#pragma once
// Poltergeist BASS update: DSP building blocks.
//   WaveBank   - 31 band-limited ghost waveforms, mip-mapped per octave (no aliasing from the tables themselves; 4096-point
//                tables hold at most 1024 harmonics, so the top harmonic always has 4+ points per cycle: low interpolation images)
//   BassFilter - the two new per-voice filters: low 12 / low 24 ladder (drive) / band / high 12 / high 24 / notch /
//                comb + / comb - (keytracked) / formant (a-e-i-o-u morph) / dirty
//   NoiseGen   - white / pink / crackle
// Everything is original code (standard textbook forms: TPT state-variable filter, a 4-pole cascade with feedback,
// a feedback comb, three band-passes on vowel formant frequencies).
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <complex>

namespace gs {

// ---------------------------------------------------------------------------------------------------------
struct WaveBank {
    static constexpr int N = 4096, kLevels = 11, kWaves = 31, kMaxHarm = 1024;
    std::vector<float> data;   // [wave][level][N + 1]
    const float* table(int w, int lvl) const { return data.data() + ((size_t) w * kLevels + (size_t) lvl) * (N + 1); }

    // the shape of each wave over one cycle, q in [0, 1)
    static double naive(int w, double q, const std::vector<std::pair<double, double>>& noisy) {
        const double tp = juce::MathConstants<double>::twoPi;
        auto S = [&](double x) { return std::sin(tp * x); };
        auto harm = [&](std::initializer_list<std::pair<int, double>> hs) { double y = 0; for (auto& [h, a] : hs) y += a * S(h * q); return y; };
        switch (w) {
            case 0: return S(q);                                                                       // sine
            case 1: return q < 0.25 ? 4 * q : (q < 0.75 ? 2 - 4 * q : 4 * q - 4);                      // triangle
            case 2: { double x = q + 0.5; x -= std::floor(x); return 2 * x - 1; }                       // saw (0 at the start)
            case 3: return q < 0.5 ? 1.0 : -1.0;                                                       // square
            case 4: return q < 0.25 ? 1.0 : -1.0;                                                      // pulse 25
            case 5: return q < 0.125 ? 1.0 : -1.0;                                                     // pulse 12
            case 6: return q < 0.5 ? S(q) : 0.0;                                                       // half sine
            case 7: return std::abs(S(q));                                                             // abs sine
            case 8: return std::fmod(q, 0.5) < 0.25 ? S(q) : 0.0;                                      // quarter sine
            case 9: return q < 0.5 ? 1 - (4 * q - 1) * (4 * q - 1) : -(1 - (4 * q - 3) * (4 * q - 3)); // parabola
            case 10: { double x = q + 0.5; x -= std::floor(x); return 0.6 * S(q) + 0.4 * (2 * x - 1); } // sine saw
            case 11: return q < 0.5 ? S(2 * q) : 0.0;                                                  // double sine
            case 12: return q < 0.5 ? std::abs(S(2 * q)) : 0.0;                                        // camel sine
            case 13: return S(std::floor(q * 4) / 4 + 0.125);                                          // steps 4
            case 14: return S(std::floor(q * 8) / 8 + 0.0625);                                         // steps 8
            case 15: return S(std::floor(q * 16) / 16 + 0.03125);                                      // steps 16
            case 16: case 17: case 18: {                                                               // formant 2 / 3 / 5
                const int k = w == 16 ? 2 : (w == 17 ? 3 : 5);
                return (0.5 - 0.5 * std::cos(tp * q)) * S(k * q); }
            case 19: { double y = 0; for (size_t h = 0; h < noisy.size(); ++h) y += noisy[h].first * std::sin(tp * (double) (h + 1) * q + noisy[h].second); return y; }   // noisy
            case 20: return std::sin(tp * q + 1.2 * S(2 * q));                                         // wail
            case 21: return std::tanh(2.5 * S(q) + 0.6 * S(3 * q));                                    // moan
            case 22: return harm({ { 1, 1.0 }, { 3, 0.6 }, { 5, 0.4 }, { 7, 0.25 }, { 9, 0.12 } });    // hollow
            case 23: return harm({ { 1, 1.0 }, { 2, 0.7 }, { 4, 0.5 }, { 8, 0.35 }, { 16, 0.25 } });   // octave stack
            case 24: { double y = 0; for (int h = 1; h <= 40; ++h) y += S(h * q); return y; }          // buzz
            case 25: return std::sin(juce::MathConstants<double>::halfPi * 2.6 * S(q));                // fold sine
            case 26: { const double d = (q - 0.5) / 0.035; return std::exp(-d * d); }                  // spike
            case 27: case 28: {                                                                        // throat O / E: a saw with two formant bumps
                const double f1 = w == 27 ? 3.0 : 3.0, f2 = w == 27 ? 6.0 : 15.0, b2 = w == 27 ? 1.5 : 3.0;
                double y = 0;
                for (int h = 1; h <= 60; ++h) y += (1.0 / h) * (0.4 + 4 * std::exp(-std::pow((h - f1) / 1.0, 2)) + 3 * std::exp(-std::pow((h - f2) / b2, 2))) * S(h * q);
                return y; }
            case 29: return std::tanh(4 * S(q));                                                       // soft square
            default: {                                                                                 // ghost choir
                double y = 0;
                for (int h = 1; h <= 40; ++h) y += (1.0 / std::sqrt((double) h)) * (0.3 + std::exp(-std::pow(h - 2.0, 2)) + 0.8 * std::exp(-std::pow((h - 5.0) / 1.2, 2)) + 0.5 * std::exp(-std::pow((h - 9.0) / 1.5, 2))) * S(h * q + 0.13 * h);
                return y; }
        }
    }

    WaveBank() {
        data.assign((size_t) kWaves * kLevels * (N + 1), 0.0f);
        std::vector<std::pair<double, double>> noisy;
        juce::Random r(0x9057);
        for (int h = 1; h <= 200; ++h) noisy.push_back({ (0.3 + 0.7 * r.nextDouble()) / std::sqrt((double) h), r.nextDouble() * juce::MathConstants<double>::twoPi });
        juce::dsp::FFT fft(12);   // 4096
        std::vector<std::complex<float>> t((size_t) N), f((size_t) N), lv((size_t) N), out((size_t) N);
        for (int w = 0; w < kWaves; ++w) {
            for (int i = 0; i < N; ++i) t[(size_t) i] = { (float) naive(w, (double) i / N, noisy), 0.0f };
            fft.perform(t.data(), f.data(), false);
            float norm = 1;
            for (int L = 0; L < kLevels; ++L) {
                const int H = kMaxHarm >> L;
                for (int k = 0; k < N; ++k) {
                    const int h = k <= N / 2 ? k : N - k;
                    lv[(size_t) k] = (h >= 1 && h <= H && !(h == N / 2)) ? f[(size_t) k] : std::complex<float>(0, 0);
                }
                fft.perform(lv.data(), out.data(), true);
                float* T = data.data() + ((size_t) w * kLevels + (size_t) L) * (N + 1);
                float pk = 1e-9f;
                for (int i = 0; i < N; ++i) { T[i] = out[(size_t) i].real(); pk = juce::jmax(pk, std::abs(T[i])); }
                if (L == 0) norm = 1.0f / pk;
                for (int i = 0; i < N; ++i) T[i] *= norm;
                T[N] = T[0];
            }
        }
    }
    // which mip level for a phase increment (cycles per sample): keeps every harmonic below Nyquist (level L holds 1024 >> L)
    static int levelFor(float inc) {
        const float x = std::abs(inc) * (float) (2 * kMaxHarm);
        if (x <= 1.0f) return 0;
        return juce::jlimit(0, kLevels - 1, (int) std::ceil(std::log2(x)));
    }
    static inline float lookup(const float* T, float p) {
        p -= std::floor(p);
        const float x = p * N; int i = (int) x; if ((unsigned) i >= (unsigned) N) i = i < 0 ? 0 : N - 1;   // (NaN-safe)
        const float fr = x - (float) i;
        return T[i] + (T[i + 1] - T[i]) * fr;
    }
};
inline const WaveBank& waveBank() { static WaveBank b; return b; }

// phase bend (the WAVE knob for table waves): squeezes the first half of the cycle
// h > 0 rounds the two corners of the bend over +-h of the cycle (a couple of samples): the bent wave's slope no longer jumps,
// so the corners stop spraying harmonics past Nyquist (that aliasing was audible as fizz on bent tables)
inline float bendPhase(float p, float bend, float h = 0.0f) {
    p -= std::floor(p);
    if (bend < 0.001f) return p;
    const float k = 0.5f * (1.0f - 0.92f * juce::jmin(1.0f, bend));
    const float s1 = 0.5f / k, s2 = 0.5f / (1.0f - k);
    h = juce::jmin(h, 0.5f * k, 0.5f * (1.0f - k));
    if (h > 1.0e-6f) {
        const float x = p - k;
        if (std::abs(x) < h) return 0.5f + s1 * x + (s2 - s1) * (x + h) * (x + h) / (4.0f * h);   // the corner in the middle
        const float w = p < 0.5f ? p : p - 1.0f;                                                    // the corner at the wrap
        if (std::abs(w) < h) return s2 * w + (s1 - s2) * (w + h) * (w + h) / (4.0f * h);
    }
    return p < k ? p * s1 : 0.5f + (p - k) * s2;
}

// ---------------------------------------------------------------------------------------------------------
// TPT state-variable filter (stereo), with notch
struct SVF2 {
    float ic1[2] {}, ic2[2] {};
    float g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0;
    void setup(float cutoff, float res, float sr) {
        cutoff = juce::jlimit(16.0f, sr * 0.45f, cutoff);
        g = std::tan(juce::MathConstants<float>::pi * cutoff / sr);
        k = 2.0f - 1.96f * juce::jlimit(0.0f, 1.0f, res);
        a1 = 1 / (1 + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    void setupK(float cutoff, float kk, float sr) { cutoff = juce::jlimit(16.0f, sr * 0.45f, cutoff); g = std::tan(juce::MathConstants<float>::pi * cutoff / sr); k = kk; a1 = 1 / (1 + g * (g + k)); a2 = g * a1; a3 = g * a2; }
    // type: 0 low, 1 band (unity peak), 2 high, 3 notch
    inline float tick(int ch, float v0, int type) {
        const float v3 = v0 - ic2[ch];
        const float v1 = a1 * ic1[ch] + a2 * v3;
        const float v2 = ic2[ch] + a2 * ic1[ch] + a3 * v3;
        ic1[ch] = 2 * v1 - ic1[ch]; ic2[ch] = 2 * v2 - ic2[ch];
        switch (type) { case 0: return v2; case 1: return v1 * k; case 2: return v0 - k * v1 - v2; default: return v0 - k * v1; }
    }
    // the DIRTY variant: the integrators saturate, so the resonance growls and folds instead of whistling
    inline float tickDirty(int ch, float v0) {
        const float v3 = v0 - ic2[ch];
        const float v1 = a1 * ic1[ch] + a2 * v3;
        const float v2 = ic2[ch] + a2 * ic1[ch] + a3 * v3;
        ic1[ch] = std::tanh(2 * v1 - ic1[ch]); ic2[ch] = 2 * v2 - ic2[ch];
        return v2;
    }
    void reset() { ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0; }
};

inline float fastTanh(float x) { x = juce::jlimit(-3.0f, 3.0f, x); const float x2 = x * x; return x * (27.0f + x2) / (27.0f + 9.0f * x2); }

struct BassFilter {
    enum Type { LP12, LP24, BP, HP12, HP24, Notch, CombPos, CombNeg, Formant, Dirty };
    int type = 0;
    SVF2 a, b, f[3];
    float fg[3] { 1, 0.5f, 0.1f };
    float lad[2][4] {}, ladOut[2] {}, G = 0, kLad = 0;
    float* comb[2] { nullptr, nullptr }; int combN = 0, combPos = 0; float combDly = 100, combFb = 0;
    float drive = 0, preG = 1, postG = 1, dryMix = 0, wetG = 1.6f, ladOutG = 1.1f; bool combClean = true;
    static bool isComb(int t) { return t == CombPos || t == CombNeg; }

    // vowel formant frequencies / gains a-e-i-o-u (three formants each)
    static const float (&formants())[5][3] { static const float F[5][3] = { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 300, 2300, 3000 }, { 450, 800, 2830 }, { 325, 700, 2530 } }; return F; }
    static const float (&formantGains())[5][3] { static const float G[5][3] = { { 1, 0.50f, 0.10f }, { 1, 0.40f, 0.20f }, { 1, 0.30f, 0.20f }, { 1, 0.35f, 0.08f }, { 1, 0.25f, 0.06f } }; return G; }

    // cutHz: the cutoff (or the comb pitch); vowel: 0..1 (formant); sr: the voice rate
    void setup(int t, float cutHz, float res, float drv, float vowel, float sr) {
        if (isComb(t) && !isComb(type) && !combClean) clearComb();   // switched to a comb mid-note: no stale echoes
        type = t; drive = drv;
        preG = 1.0f + drv * drv * 24.0f;
        postG = 1.0f / std::sqrt(preG);
        // DRIVE fades in over its first quarter: at 0 the filter is clean and at unity, so the knob never jumps in level
        const float wet = juce::jlimit(0.0f, 1.0f, drv * 4.0f);
        dryMix = 1.0f - wet;
        wetG = postG * (1.0f + 0.6f * wet);   // the drive's make-up gain grows with it (1.6 from a quarter up, as before)
        ladOutG = 1.1f * (1.0f - wet) + postG * 1.4f * wet;
        switch (t) {
            case LP12: case BP: case HP12: case Notch: a.setup(cutHz, res, sr); break;
            case HP24: a.setup(cutHz, res * 0.8f, sr); b.setupK(cutHz, 1.414f, sr); break;
            case Dirty: a.setup(cutHz, juce::jmin(1.0f, res * 1.05f), sr); break;
            case LP24: {
                const float fc = juce::jlimit(16.0f, sr * 0.42f, cutHz);
                const float g = std::tan(juce::MathConstants<float>::pi * fc / sr);
                G = g / (1 + g); kLad = juce::jlimit(0.0f, 1.0f, res) * 3.85f; break; }
            case CombPos: case CombNeg: {
                combDly = juce::jlimit(2.0f, (float) combN - 3, sr / juce::jlimit(20.0f, sr * 0.25f, cutHz));
                combFb = juce::jlimit(0.0f, 1.0f, res) * 0.96f * (t == CombNeg ? -1.0f : 1.0f); break; }
            case Formant: {
                const float vp = juce::jlimit(0.0f, 1.0f, vowel) * 3.999f; const int i = (int) vp; const float fr = vp - (float) i;
                const auto& F = formants(); const auto& FG = formantGains();
                for (int j = 0; j < 3; ++j) { f[j].setup(F[i][j] + (F[i + 1][j] - F[i][j]) * fr, 0.80f + 0.18f * juce::jlimit(0.0f, 1.0f, res), sr); fg[j] = FG[i][j] + (FG[i + 1][j] - FG[i][j]) * fr; }
                break; }
            default: break;
        }
    }
    inline float tick(int ch, float x) {
        if (drive > 0.0f && type != LP24) x = dryMix * x + (1.0f - dryMix) * std::tanh(x * preG) * wetG;
        switch (type) {
            case LP12: return a.tick(ch, x, 0);
            case BP: return a.tick(ch, x, 1);
            case HP12: return a.tick(ch, x, 2);
            case Notch: return a.tick(ch, x, 3);
            case HP24: return b.tick(ch, a.tick(ch, x, 2), 2);
            case Dirty: return a.tickDirty(ch, x * (1.0f + drive * 2.0f)) * 0.9f;
            case LP24: {
                float u = std::tanh(x * preG * 0.9f - kLad * ladOut[ch]);
                float* s = lad[ch];
                for (int i = 0; i < 4; ++i) { const float v = (u - s[i]) * G; const float y = v + s[i]; s[i] = y + v; u = y; }
                ladOut[ch] = u;
                return u * (1.0f + kLad * 0.55f) * ladOutG;
            }
            case CombPos: case CombNeg: {
                float* B = comb[ch]; if (!B) return x;
                float rp = (float) combPos - combDly; if (rp < 0) rp += (float) combN;
                const int r0 = (int) rp; const float t = rp - (float) r0; const int r1 = r0 + 1 >= combN ? 0 : r0 + 1;
                const float d = B[r0] + (B[r1] - B[r0]) * t;
                const float y = x + combFb * d;
                B[combPos] = fastTanh(y * 0.8f) * 1.25f; combClean = false;
                if (ch == 1) { if (++combPos >= combN) combPos = 0; }
                return y * (1.0f - std::abs(combFb) * 0.55f);
            }
            case Formant: {
                float y = 0; for (int j = 0; j < 3; ++j) y += f[j].tick(ch, x, 1) * fg[j];
                return y * 1.9f;
            }
            default: return x;
        }
    }
    void clearComb() { for (auto* B : comb) if (B) std::fill(B, B + combN, 0.0f); combPos = 0; combClean = true; }
    // clearComb: also empty the comb's delay line (only needed when the filter is a comb)
    void reset(bool clear) {
        a.reset(); b.reset(); for (auto& s : f) s.reset();
        for (auto& c : lad) for (auto& s : c) s = 0;
        ladOut[0] = ladOut[1] = 0;
        if (clear && !combClean) clearComb();
    }
};

// ---------------------------------------------------------------------------------------------------------
struct NoiseGen {
    uint32_t s = 0x12345u; float b0 = 0, b1 = 0, b2 = 0, crack = 0;
    inline float white() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (int32_t) s * (1.0f / 2147483648.0f); }
    inline float tick(int type) {
        const float w = white();
        if (type == 0) return w;
        if (type == 1) { b0 = 0.99765f * b0 + w * 0.0990460f; b1 = 0.96300f * b1 + w * 0.2965164f; b2 = 0.57000f * b2 + w * 1.0526913f; return (b0 + b1 + b2 + w * 0.1848f) * 0.22f; }
        // crackle: sparse random pops that ring off quickly
        const float u = white();
        if (std::abs(u) > 0.9965f) crack = (w > 0 ? 1.0f : -1.0f) * (0.5f + 0.5f * std::abs(w));
        crack *= 0.93f;
        return crack;
    }
};

} // namespace gs
