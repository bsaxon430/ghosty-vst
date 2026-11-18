// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX 13 SPIRIT EQ: 24 bands on the Surge Synth Team's Cytomic SVF (sst-filters, GPL-3.0; a trapezoidal state-variable design, so
// every band can glide and be modulated without zipper). Per band: 9 shapes, cuts from 6 to 96 dB/oct (Butterworth cascades), Q,
// stereo / left / right / mid / side, and dynamics (threshold, range, ratio, attack, release, auto threshold, band or wide detection).
// Global: output, gain scale, character, auto gain (from the static curve), phase invert. Zero latency, or
// LINEAR PHASE (Low / Medium / High = 1024 / 2048 / 4096 taps): the same bands' static curve as a symmetric FIR. The curve is not a
// second filter design: each band's gain at each frequency is worked out from the very coefficients its filters run with, so the
// two modes have the same magnitude and differ only in phase and latency (1.5 x the tap count, reported). Dynamics are a
// zero-latency feature: in the linear modes a dynamic band plays its static gain. A tap for the editor's spectrum display.
// The brief's primary engine, ZL Equalizer, was not wrapped: its filters depend on its own vector / container / FFT layers and Google
// Highway, a wrapper larger than the algorithm (DECISIONS.md 16). Its linear-phase modes and the per-band Spectral switch are not built yet.
#pragma once
#include "ColourAw.h"

namespace phantom
{
class SpiritEqFx final : public CatalogEffect
{
public:
    static constexpr int kBands = 24;
    SpiritEqFx() : CatalogEffect (uEq) {}
    void prepareEffect (double sampleRate, int, int) override
    {
        sr = sampleRate;
        for (int b = 0; b < kBands; ++b)
        {
            auto& B = band[(size_t) b]; const auto pre = std::to_string (b + 1) + "_";
            static const char* names[kNum] { "on", "byp", "type", "slope", "freq", "gain", "q", "stereo", "dyn", "thr", "range", "ratio", "att", "rel", "auto", "detect", "spec" };
            for (int k = 0; k < kNum; ++k) B.p[(size_t) k] = id ((pre + names[k]).c_str());
            for (auto& s : B.st) s.prepare (sampleRate); B.det.prepare (sampleRate); B.reset();
        }
        pOut = id ("out"); pChar = id ("char"); pScale = id ("scale"); pAuto = id ("autogain"); pInv = id ("invert"); pMode = id ("mode");
        agNow = 1.0f; scratch.reset(); for (auto& s : scratch.st) s.prepare (sampleRate);
        lin.prepare(); linMode = -1; modeNow.store (choice (pMode)); tap.sampleRate.store (sampleRate);
    }
    int getLatencySamples() const override { const int m = modeNow.load(); return m <= 0 ? 0 : Lin::tapsFor (m, sr) * 3 / 2; }
    void updateParameters (const ModulationContext& ctx) override { CatalogEffect::updateParameters (ctx); modeNow.store (choice (pMode)); }
    AnalyserTap* analyserTap() override { return &tap; }
    // ---- for the painted visual (message thread; nothing here touches the sound): the bands' curve now in dB at n places, even in
    // log f from 20 Hz to 20 kHz (gain scale and the dynamic gains in, the output out), and one { x, on, dyn } per band (x 0..1 on that axis)
    std::array<std::atomic<float>, kBands> uiDyn {}; std::atomic<int> uiSel { 0 };
    void uiCurve (std::vector<float>& db, std::vector<std::array<float, 3>>& bands, int n = 64) const
    {
        db.assign ((size_t) n, 0.0f); bands.clear(); Band s; for (auto& st : s.st) st.prepare (sr); const float scale = target (pScale);
        for (int b = 0; b < kBands; ++b)
        {
            const auto& B = band[(size_t) b]; const bool act = on (B.p[kOn]) && ! on (B.p[kByp]); const int type = choice (B.p[kType]); const float f = target (B.p[kFreq]);
            const bool dyn = on (B.p[kDyn]) && (type <= 2 || type >= 7); bands.push_back ({ (float) juce::jlimit (0.0, 1.0, std::log (f / 20.0) / std::log (1000.0)), act ? 1.0f : 0.0f, dyn ? 1.0f : 0.0f });
            if (! act) continue;
            s.design (type, choice (B.p[kSlope]), f, target (B.p[kQ]), target (B.p[kGain]) * scale + (dyn ? uiDyn[(size_t) b].load (std::memory_order_relaxed) : 0.0f), sr);
            for (int i = 0; i < n; ++i) { const double hz = 20.0 * std::pow (1000.0, (double) i / (double) (n - 1)); db[(size_t) i] += (float) (20.0 * std::log10 (juce::jmax (1.0e-6, s.magnitude (juce::MathConstants<double>::twoPi * juce::jmin (hz, 0.49 * sr) / sr)))); }
        }
    }
    AnalyserTap tap;
    void resetEffect() override { for (auto& B : band) B.reset(); lin.reset(); }
    void process (juce::dsp::AudioBlock<float> block, const ModulationContext& ctx) noexcept override
    {
        beginBlock (ctx);
        const int n = (int) block.getNumSamples(), ch = (int) block.getNumChannels();
        auto* L = block.getChannelPointer (0); auto* R = ch > 1 ? block.getChannelPointer (1) : L;
        const float scaleT = target (pScale); const int character = choice (pChar); const bool autoGain = on (pAuto), invert = on (pInv);
        bool any = false; for (auto& B : band) if (on (B.p[kOn]) && ! on (B.p[kByp])) any = true;
        const int mode = choice (pMode); modeNow.store (mode);
        if (mode != linMode) { linMode = mode; if (mode > 0) lin.start (Lin::tapsFor (mode, sr)); else for (auto& B : band) B.reset(); }
        for (int i0 = 0; i0 < n; i0 += kSub)
        {
            const int len = juce::jmin (kSub, n - i0);
            const float scale = p (pScale).skip (len); juce::ignoreUnused (scaleT);
            double agPow = 0.0; int agN = 0;
            if (mode > 0)
            {   // LINEAR PHASE: the smoothers keep moving (so going back to Zero Latency never jumps); the sound goes through the FIR
                for (int b = 0; b < kBands; ++b)
                {
                    auto& B = band[(size_t) b]; for (int k : { (int) kFreq, (int) kQ, (int) kThr, (int) kRange, (int) kRatio, (int) kAtt, (int) kRel }) p (B.p[(size_t) k]).skip (len);
                    const float gainDb = p (B.p[kGain]).skip (len) * scale;
                    if (autoGain && on (B.p[kOn]) && ! on (B.p[kByp])) { agPow += B.meanPower (choice (B.p[kType]), gainDb); ++agN; }
                }
                for (int i = i0; i < i0 + len; ++i) if (lin.step (L[i], R[i])) { const auto sg = signature (scale); if (sg != lin.sig || ! lin.have) { designKernels (scale); lin.sig = sg; } lin.frame(); }
            }
            else for (int b = 0; b < kBands; ++b)
            {
                auto& B = band[(size_t) b];
                const bool act = on (B.p[kOn]) && ! on (B.p[kByp]);
                // a band switched on / off fades over ~20 ms (its amount is a glide of its own) instead of jumping
                const float a0 = B.amount; B.amount = act ? juce::jmin (1.0f, B.amount + (float) len / (0.02f * (float) sr)) : juce::jmax (0.0f, B.amount - (float) len / (0.02f * (float) sr));
                const float freq = p (B.p[kFreq]).skip (len), q = p (B.p[kQ]).skip (len), gainDb = p (B.p[kGain]).skip (len) * scale;
                const float thr = p (B.p[kThr]).skip (len), range = p (B.p[kRange]).skip (len), ratio = p (B.p[kRatio]).skip (len), att = p (B.p[kAtt]).skip (len), rel = p (B.p[kRel]).skip (len);
                if (a0 <= 0.0f && B.amount <= 0.0f) { B.idle = true; continue; }
                if (B.idle) { for (auto& s : B.st) s.reset(); B.det.reset(); B.env = -120.0f; B.idle = false; }
                const int type = choice (B.p[kType]), slope = choice (B.p[kSlope]), stereo = choice (B.p[kStereo]);
                const bool dyn = on (B.p[kDyn]) && (type <= 2 || type >= 7);   // dynamics act on the gain shapes
                // ---- dynamics: the detector (the band's own region, or the whole signal) -> level in dB -> gain change
                float dynDb = 0.0f;
                if (dyn)
                {
                    const bool wide = choice (B.p[kDetect]) == 1; B.det.setQ (sst::filters::CytomicSVF::Mode::Bandpass, freq, juce::jmax (0.7f, q));
                    const float ca = std::exp (-1.0f / (0.001f * att * (float) sr)), cr = std::exp (-1.0f / (0.001f * rel * (float) sr)), cl = std::exp (-1.0f / (1.0f * (float) sr));
                    for (int i = i0; i < i0 + len; ++i)
                    {
                        float x = 0.5f * (L[i] + R[i]), y = x; if (! wide) B.det.process (x, y);
                        const float lv = 20.0f * std::log10 (std::abs (x) + 1.0e-9f);
                        B.env = lv > B.env ? ca * B.env + (1.0f - ca) * lv : cr * B.env + (1.0f - cr) * lv;
                        B.longLv = cl * B.longLv + (1.0f - cl) * lv;
                    }
                    const float t = on (B.p[kAuto]) ? B.longLv + (thr + 24.0f) * 0.25f : thr;   // AUTO: the threshold rides the band's long-term level
                    const float over = juce::jmax (0.0f, B.env - t), change = juce::jmin (std::abs (range), over * (1.0f - 1.0f / juce::jmax (1.0f, ratio)));
                    dynDb = range < 0.0f ? -change : change;
                }
                B.dynNow += 0.3f * (dynDb - B.dynNow); uiDyn[(size_t) b].store (B.dynNow, std::memory_order_relaxed);
                B.design (type, slope, freq, q, gainDb + B.dynNow, sr);
                if (autoGain) { agPow += B.meanPower (type, gainDb); ++agN; }
                for (int i = i0; i < i0 + len; ++i)
                {
                    const float a = a0 + (B.amount - a0) * (float) (i - i0 + 1) / (float) len;
                    float l = L[i], r = R[i], x0, x1;
                    // the band's channels: stereo, left, right, mid or side
                    if (stereo == 0) { x0 = l; x1 = r; } else if (stereo == 1) { x0 = l; x1 = 0; } else if (stereo == 2) { x0 = r; x1 = 0; } else if (stereo == 3) { x0 = 0.5f * (l + r); x1 = 0; } else { x0 = 0.5f * (l - r); x1 = 0; }
                    float y0 = x0, y1 = x1; B.run (y0, y1);
                    if (stereo == 0) { l += (y0 - l) * a; r += (y1 - r) * a; }
                    else if (stereo == 1) l += (y0 - l) * a; else if (stereo == 2) r += (y0 - r) * a;
                    else { const float d = (y0 - x0) * a; if (stereo == 3) { l += d; r += d; } else { l += d; r -= d; } }
                    L[i] = l; R[i] = r;
                }
            }
            // AUTO GAIN: the static curve's average power (pink-weighted) taken back off
            const float agT = autoGain && agN > 0 ? (float) juce::jlimit (0.06, 16.0, 1.0 / std::sqrt (std::pow (10.0, agPow / 10.0))) : 1.0f;
            const float out0 = outNow, out1 = p (pOut).skipGain (len) * (invert ? -1.0f : 1.0f);
            for (int i = i0; i < i0 + len; ++i)
            {
                agNow += 0.002f * (agT - agNow);
                const float og = out0 + (out1 - out0) * (float) (i - i0 + 1) / (float) len;
                float l = L[i], r = R[i];
                if (character > 0 && any) { const float c = character == 1 ? 0.15f : 0.4f; l += c * (std::tanh (l) - l); r += c * (std::tanh (r) - r); }   // SUBTLE / WARM: a little tube-like rounding
                L[i] = l * agNow * og; R[i] = r * agNow * og;
            }
            outNow = out1;
        }
        tap.push (L, R, n);
    }
private:
    static constexpr int kSub = 16;
    enum K { kOn, kByp, kType, kSlope, kFreq, kGain, kQ, kStereo, kDyn, kThr, kRange, kRatio, kAtt, kRel, kAuto, kDetect, kSpec, kNum };
    struct OnePole { float s = 0, g = 0; bool hp = false; void set (float hz, float srate, bool high) { const float w = std::tan (juce::MathConstants<float>::pi * juce::jmin (hz, 0.49f * srate) / srate); g = w / (1.0f + w); hp = high; }
                     inline float run (float x) noexcept { const float v = (x - s) * g; const float lp = v + s; s = lp + v; return hp ? x - lp : lp; } };
    struct Band
    {
        std::array<int, kNum> p {}; std::array<Svf, 8> st; int nStages = 0; OnePole op[2]; bool odd = false; Svf det;
        float amount = 0, env = -120, longLv = -60, dynNow = 0; bool idle = true;
        void reset() { for (auto& s : st) s.reset(); det.reset(); op[0].s = op[1].s = 0; env = -120; longLv = -60; dynNow = 0; amount = 0; idle = true; }
        // shapes: Bell, Low Shelf, High Shelf, Low Cut, High Cut, Notch, Band Pass, Tilt Shelf, Flat Tilt
        void design (int type, int slope, float f, float q, float gDb, double sr)
        {
            using M = sst::filters::CytomicSVF::Mode; const float g = dbToGain (gDb); odd = false;
            switch (type)
            {
                case 0: nStages = 1; st[0].setQ (M::Bell, f, q, g); break;
                case 1: nStages = 1; st[0].setQ (M::LowShelf, f, juce::jmax (0.5f, q * 0.707f), g); break;
                case 2: nStages = 1; st[0].setQ (M::HighShelf, f, juce::jmax (0.5f, q * 0.707f), g); break;
                case 3: case 4:
                {   // 6 / 12 / 18 / 24 / 36 / 48 / 72 / 96 dB per octave: Butterworth, built from 2nd-order sections (+ one first-order for the odd ones)
                    static const int orders[8] { 1, 2, 3, 4, 6, 8, 12, 16 }; const int order = orders[juce::jlimit (0, 7, slope)];
                    odd = (order & 1) != 0; nStages = juce::jmin (8, order / 2);
                    for (int k = 0; k < nStages; ++k) { const double th = juce::MathConstants<double>::pi * (2 * k + 1) / (2.0 * order); st[(size_t) k].setQ (type == 3 ? M::Highpass : M::Lowpass, f, (float) (1.0 / (2.0 * std::cos (th)))); }
                    if (odd) { op[0].set (f, (float) sr, type == 3); op[1].set (f, (float) sr, type == 3); }
                    break;
                }
                case 5: nStages = 1; st[0].setQ (M::Notch, f, q); break;
                case 6: nStages = 1; st[0].setQ (M::Bandpass, f, q); break;
                case 7: nStages = 2; st[0].setQ (M::LowShelf, f, 0.5f, 1.0f / std::sqrt (g)); st[1].setQ (M::HighShelf, f, 0.5f, std::sqrt (g)); break;   // tilt around f
                default: nStages = 2; st[0].setQ (M::LowShelf, f / 8.0f, 0.5f, 1.0f / std::sqrt (g)); st[1].setQ (M::HighShelf, f * 8.0f, 0.5f, std::sqrt (g)); break;   // a broad, flat tilt
            }
        }
        inline void run (float& a, float& b) noexcept { for (int k = 0; k < nStages; ++k) st[(size_t) k].process (a, b); if (odd) { a = op[0].run (a); b = op[1].run (b); } }
        // the band's gain at w radians per sample, from the coefficients its filters run with (each stage is a trapezoidal SVF:
        // out = m0 in + m1 band + m2 low, with s = j tan (w / 2) / g; the odd stage is a trapezoidal one-pole)
        double magnitude (double w) const noexcept
        {
            const double t = std::tan (0.5 * juce::jmin (w, juce::MathConstants<double>::pi - 1.0e-6)); std::complex<double> h (1.0, 0.0);
            for (int k = 0; k < nStages; ++k)
            {
                const auto& f = st[(size_t) k].f; const double g = SIMD_MM (cvtss_f32) (f.g), kk = SIMD_MM (cvtss_f32) (f.k), m0 = SIMD_MM (cvtss_f32) (f.m0), m1 = SIMD_MM (cvtss_f32) (f.m1), m2 = SIMD_MM (cvtss_f32) (f.m2);
                const std::complex<double> s (0.0, t / juce::jmax (1.0e-9, g)), d = s * s + kk * s + 1.0; h *= m0 + (m1 * s + m2) / d;
            }
            if (odd) { const double g1 = op[0].g, w1 = g1 / juce::jmax (1.0e-9, 1.0 - g1); const std::complex<double> s (0.0, t / juce::jmax (1.0e-9, w1)); h *= op[0].hp ? s / (1.0 + s) : 1.0 / (1.0 + s); }
            return std::abs (h);
        }
        // average power gain in dB of a gain shape (pink-weighted, so a bell's share follows its width), for AUTO GAIN
        double meanPower (int type, float gDb) const noexcept
        {
            if (type == 3 || type == 4 || type == 5 || type == 6) return 0.0;   // cuts and band shapes are not compensated
            const float share = type == 0 ? 1.0f / 6.0f : 0.5f;   // a bell at Q 1 spans about a sixth of the audible octaves; a shelf half
            return (double) gDb * share;
        }
    };
    // ---- LINEAR PHASE: frames of N samples through an FFT of 2 N; the kernel is redesigned at a frame boundary when a band moved,
    // and that frame cross-fades from the old kernel to the new one. Everything is allocated in prepare.
    struct Lin
    {
        static constexpr int kMaxN = 4096, kMaxF = 2 * kMaxN;
        static int tapsFor (int mode, double sampleRate) { const int base = mode == 1 ? 1024 : mode == 2 ? 2048 : 4096; return juce::jmin (kMaxN, sampleRate > 60000.0 ? base * 2 : base); }
        int N = 0, F = 0, pos = 0; bool have = false, crossNext = false, cross2x2 = false, oldCross2x2 = false; juce::uint64 sig = 0; juce::dsp::FFT* fft = nullptr;
        std::unique_ptr<juce::dsp::FFT> ffts[3]; std::vector<float> acc[2], outQ[2], tail[2], X[4], Y, kern[4], kernOld[4], win; std::vector<double> M[4];
        void prepare()
        {
            for (int i = 0; i < 3; ++i) ffts[i] = std::make_unique<juce::dsp::FFT> (11 + i);
            for (auto* v : { &acc[0], &acc[1], &outQ[0], &outQ[1], &tail[0], &tail[1], &win }) v->assign ((size_t) kMaxN, 0.0f);
            for (auto* v : { &X[0], &X[1], &X[2], &X[3], &Y, &kern[0], &kern[1], &kern[2], &kern[3], &kernOld[0], &kernOld[1], &kernOld[2], &kernOld[3] }) v->assign ((size_t) (2 * kMaxF), 0.0f);
            for (auto& m : M) m.assign ((size_t) (kMaxF / 2 + 1), 0.0);
            N = 0; have = false;
        }
        void reset() { for (auto* v : { &acc[0], &acc[1], &outQ[0], &outQ[1], &tail[0], &tail[1] }) std::fill (v->begin(), v->end(), 0.0f); pos = 0; }
        void start (int taps) { N = taps; F = 2 * N; fft = ffts[N == 1024 ? 0 : N == 2048 ? 1 : 2].get(); for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N); have = false; reset(); }
        // one sample in, one out (delayed by 1.5 N); true when a frame of N is full and frame() must run
        inline bool step (float& l, float& r) noexcept
        {
            const float ol = outQ[0][(size_t) pos], orr = outQ[1][(size_t) pos]; acc[0][(size_t) pos] = l; acc[1][(size_t) pos] = r; l = ol; r = orr;
            return ++pos >= N;
        }
        void frame() noexcept
        {
            pos = 0; const bool cross = crossNext; crossNext = false;
            auto fwd = [this] (std::vector<float>& x, const std::vector<float>& in, int shape)   // shape 0 whole, 1 fading out, 2 fading in
            {
                for (int i = 0; i < N; ++i) { const float r = ((float) i + 0.5f) / (float) N; x[(size_t) i] = in[(size_t) i] * (shape == 0 ? 1.0f : shape == 1 ? 1.0f - r : r); }
                std::fill (x.begin() + N, x.begin() + 2 * F, 0.0f); fft->performRealOnlyForwardTransform (x.data());
            };
            auto mulAdd = [this] (const std::vector<float>& k, const std::vector<float>& x) { for (int b = 0; b < F; ++b) { const float kr = k[(size_t) (2 * b)], ki = k[(size_t) (2 * b + 1)], xr = x[(size_t) (2 * b)], xi = x[(size_t) (2 * b + 1)]; Y[(size_t) (2 * b)] += kr * xr - ki * xi; Y[(size_t) (2 * b + 1)] += kr * xi + ki * xr; } };
            if (cross) { fwd (X[0], acc[0], 2); fwd (X[1], acc[1], 2); fwd (X[2], acc[0], 1); fwd (X[3], acc[1], 1); } else { fwd (X[0], acc[0], 0); fwd (X[1], acc[1], 0); }
            for (int c = 0; c < 2; ++c)
            {
                std::fill (Y.begin(), Y.begin() + 2 * F, 0.0f);
                mulAdd (kern[c == 0 ? 0 : 3], X[c]); if (cross2x2) mulAdd (kern[c == 0 ? 1 : 2], X[1 - c]);
                if (cross) { mulAdd (kernOld[c == 0 ? 0 : 3], X[2 + c]); if (oldCross2x2) mulAdd (kernOld[c == 0 ? 1 : 2], X[2 + (1 - c)]); }
                fft->performRealOnlyInverseTransform (Y.data());
                for (int i = 0; i < N; ++i) { outQ[c][(size_t) i] = Y[(size_t) i] + tail[c][(size_t) i]; tail[c][(size_t) i] = Y[(size_t) (N + i)]; }
            }
        }
    } lin;
    // what the kernel depends on: every sounding band's shape and numbers
    juce::uint64 signature (float scale) const noexcept
    {
        juce::uint64 h = 1469598103934665603ull; auto mix = [&h] (juce::uint64 v) { h = (h ^ v) * 1099511628211ull; };
        auto fb = [] (float f) { juce::uint32 u; std::memcpy (&u, &f, 4); return (juce::uint64) u; };
        for (int b = 0; b < kBands; ++b)
        {
            const auto& B = band[(size_t) b]; if (! on (B.p[kOn]) || on (B.p[kByp])) continue;
            mix ((juce::uint64) (b + 1)); mix ((juce::uint64) choice (B.p[kType])); mix ((juce::uint64) choice (B.p[kSlope])); mix ((juce::uint64) choice (B.p[kStereo]));
            mix (fb (target (B.p[kFreq]))); mix (fb (target (B.p[kQ]))); mix (fb (target (B.p[kGain]) * scale));
        }
        return h;
    }
    // the bands' static curve as four symmetric FIRs (left to left, right to left, left to right, right to right)
    void designKernels (float scale) noexcept
    {
        const int N = lin.N, F = lin.F, H = F / 2;
        for (int e = 0; e < 4; ++e) std::swap (lin.kern[e], lin.kernOld[e]); lin.oldCross2x2 = lin.cross2x2; lin.crossNext = lin.have; lin.have = true;
        for (int k = 0; k <= H; ++k) { lin.M[0][(size_t) k] = 1.0; lin.M[1][(size_t) k] = 0.0; lin.M[2][(size_t) k] = 0.0; lin.M[3][(size_t) k] = 1.0; }   // [LL LR; RL RR]
        bool two = false;
        for (int b = 0; b < kBands; ++b)
        {
            const auto& B = band[(size_t) b]; if (! on (B.p[kOn]) || on (B.p[kByp])) continue;
            const int stereo = choice (B.p[kStereo]); if (stereo >= 3) two = true;
            scratch.design (choice (B.p[kType]), choice (B.p[kSlope]), target (B.p[kFreq]), target (B.p[kQ]), target (B.p[kGain]) * scale, sr);
            for (int k = 0; k <= H; ++k)
            {
                const double m = scratch.magnitude (juce::MathConstants<double>::twoPi * (double) k / (double) F);
                double& a = lin.M[0][(size_t) k]; double& bb = lin.M[1][(size_t) k]; double& c = lin.M[2][(size_t) k]; double& d = lin.M[3][(size_t) k];
                if (stereo == 0) { a *= m; bb *= m; c *= m; d *= m; }
                else if (stereo == 1) { a *= m; bb *= m; }
                else if (stereo == 2) { c *= m; d *= m; }
                else { const double pp = 0.5 * (m + 1.0), q = (stereo == 3 ? 0.5 : -0.5) * (m - 1.0); const double na = pp * a + q * c, nb = pp * bb + q * d, nc = q * a + pp * c, nd = q * bb + pp * d; a = na; bb = nb; c = nc; d = nd; }
            }
        }
        lin.cross2x2 = two;
        for (int e = 0; e < 4; ++e)
        {
            auto& K = lin.kern[e]; if (! two && (e == 1 || e == 2)) { std::fill (K.begin(), K.begin() + 2 * F, 0.0f); continue; }
            auto& W = lin.Y;   // (work space)
            for (int k = 0; k <= H; ++k) { W[(size_t) (2 * k)] = (float) lin.M[e][(size_t) k]; W[(size_t) (2 * k + 1)] = 0.0f; }
            for (int k = H + 1; k < F; ++k) { W[(size_t) (2 * k)] = W[(size_t) (2 * (F - k))]; W[(size_t) (2 * k + 1)] = 0.0f; }
            lin.fft->performRealOnlyInverseTransform (W.data());   // the zero-phase response, centred on sample 0 (and wrapped)
            for (int i = 0; i < N; ++i) K[(size_t) i] = W[(size_t) ((i - N / 2 + F) % F)] * lin.win[(size_t) i];
            std::fill (K.begin() + N, K.begin() + 2 * F, 0.0f);
            lin.fft->performRealOnlyForwardTransform (K.data());
        }
    }
    Band scratch; std::atomic<int> modeNow { 0 }; int linMode = -1, pMode = 0;
    double sr = 48000.0; std::array<Band, kBands> band; float agNow = 1.0f, outNow = 1.0f;
    int pOut = 0, pChar = 0, pScale = 0, pAuto = 0, pInv = 0;
};
} // namespace phantom
