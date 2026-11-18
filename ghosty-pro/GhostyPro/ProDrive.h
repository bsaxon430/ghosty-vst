#pragma once
// FILTER DRIVE, round 2 (2026-10-24): the drive lives INSIDE the filter, per voice. Original DSP (no third-party code).
// Audio-thread rule: prepare() allocates, everything else is allocation- and lock-free.
//
//   satS          (ProProcessor.h) a bounded soft saturator: exactly linear below S / 2, a smooth tanh knee up to S = the
//                 circuit's headroom. Odd, slope <= 1 everywhere (so a filter loop with it inside stays passive / stable).
//   OversamplerN  F x polyphase up / FIR down (Kaiser sinc). Constant, whole-sample delay (17 at 4x, 23 at 2x), so a clean path
//                 next to it only needs that delay to stay phase-aligned (the cross-fade at low drive, SUB / NOISE).
//   Decimator     the QUALITY engine's Q x -> 1 x low pass (High = 2x, Ultra = 4x; 11 base samples of delay).
//   FilterChain   the voice filter (all 10 types) as one object. The linear tick() is the voice's old code, op for op
//                 (the CLASSIC drive law and drive = 0 stay bit-exact). With S > 0 the SVF / ladder states and the output
//                 saturate per stage: that copy runs inside the oversampler and is what the DRIVE knob pushes into.
#include <cmath>
#include <array>
#include <vector>
#include <algorithm>

namespace gpro
{
// (satS lives in ProProcessor.h next to Svf / Ladder, which use it in their saturating ticks)

// ---------------------------------------------------------------- F x oversampler (stereo): F = 4 for the Draft engine, 2 for High
template <int F, int T_, int LD_>
struct OversamplerN
{
    static constexpr int T = T_, LU = F * T - 1, LD = LD_, DELAY = (2 * T - 1 + (LD - 1) / 2) / F;   // whole input samples
    static_assert ((2 * T - 1 + (LD - 1) / 2) % F == 0, "the total delay must be a whole number of input samples");
    float hu[F][T] {};
    std::array<float, LD> hd {};
    float xh[2][2 * T] {}; int xw = 0;
    std::array<float, 2 * 128> dh[2] {}; int dw = 0;
    static double bessel0 (double x) { double sum = 1, term = 1; for (int k = 1; k < 40; ++k) { term *= (x / (2.0 * k)) * (x / (2.0 * k)); sum += term; if (term < 1e-12 * sum) break; } return sum; }
    // Kaiser-windowed sinc, cut at the input Nyquist (0.5 / F of the output rate)
    static void proto (int L, float* out, double beta)
    {
        const double fc = 0.5 / F, M = (L - 1) / 2.0, denom = bessel0 (beta);
        double sum = 0;
        for (int i = 0; i < L; ++i)
        {
            const double t = i - M;
            const double s = std::abs (t) < 1e-12 ? 2 * fc : std::sin (2 * 3.14159265358979323846 * fc * t) / (3.14159265358979323846 * t);
            const double r = t / M, w = bessel0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / denom;
            out[i] = (float) (s * w); sum += out[i];
        }
        for (int i = 0; i < L; ++i) out[i] = (float) (out[i] / sum);
    }
    void prepare()
    {
        // the up filter is short (beta 5: flat to 18 kHz at 48 k, images -50 dB), the down filter longer (beta 7: what folds back is -75 dB)
        float h[LU]; proto (LU, h, 5.0);
        for (int p = 0; p < F; ++p)
        {
            double sum = 0;
            for (int j = 0; j < T; ++j) { const int i = F * j + p; hu[p][j] = i < LU ? h[i] : 0.0f; sum += hu[p][j]; }
            for (int j = 0; j < T; ++j) hu[p][j] = (float) (hu[p][j] / sum);        // every phase at unity DC: a flat pass band
        }
        proto (LD, hd.data(), 7.0);
        reset();
    }
    void reset() noexcept { for (auto& c : xh) std::fill (std::begin (c), std::end (c), 0.0f); for (auto& c : dh) c.fill (0.0f); xw = 0; dw = 0; }
    // one input sample in -> the F output samples (time order) for one channel; call for ch 0 then 1, then advance()
    inline void up (int ch, float x, float* outF) noexcept
    {
        xh[ch][xw] = xh[ch][xw + T] = x;
        const float* src = &xh[ch][xw + T];           // src[-j] = x[n - j]
        for (int p = 0; p < F; ++p) { float acc = 0; for (int j = 0; j < T; ++j) acc += hu[p][j] * src[-j]; outF[p] = acc; }
    }
    inline void advance() noexcept { xw = (xw + 1) % T; dw = (dw + F) & 127; }
    // F samples in (time order) -> one output sample for one channel (evaluated at the first of the F: whole-sample delay)
    inline float down (int ch, const float* inF) noexcept
    {
        auto& r = dh[ch];
        for (int p = 0; p < F; ++p) { const int w = (dw + p) & 127; r[(size_t) w] = r[(size_t) w + 128] = inF[p]; }
        const float* src = &r[(size_t) dw + 128];     // src[-i] = u[F n - i]
        float acc = 0; for (int i = 0; i < LD; ++i) acc += hd[(size_t) i] * src[-i];
        return acc;
    }
};
using Oversampler4 = OversamplerN<4, 12, 91>;      // 17 input samples of delay
using Oversampler2 = OversamplerN<2, 12, 51>;      // 24 input samples of delay (even: the High engine's latency stays a whole base sample)

// ---------------------------------------------------------------- the engine decimator (QUALITY High / Ultra): Q engine samples -> 1
// Kaiser (beta 7) low pass at the base Nyquist: 101 taps at 2x (transition 4 kHz, what folds back is -70 dB; 25 base samples of
// delay), 97 taps at 4x (12 base samples of delay)
struct Decimator
{
    int Q = 1, LD = 1; std::array<float, 128> hd {};
    std::array<float, 2 * 128> ring[2] {}; int w = 0;
    static constexpr int DELAY_BASE_2X = 25, DELAY_BASE_4X = 12;
    void prepare (int q)
    {
        Q = q; LD = q == 2 ? 101 : q == 4 ? 97 : 1;
        if (q > 1)
        {
            const double fc = 0.46 / q, M = (LD - 1) / 2.0, denom = Oversampler4::bessel0 (7.0);
            double sum = 0;
            for (int i = 0; i < LD; ++i)
            {
                const double t = i - M;
                const double s = std::abs (t) < 1e-12 ? 2 * fc : std::sin (2 * 3.14159265358979323846 * fc * t) / (3.14159265358979323846 * t);
                const double r = t / M, wk = Oversampler4::bessel0 (7.0 * std::sqrt (std::max (0.0, 1.0 - r * r))) / denom;
                hd[(size_t) i] = (float) (s * wk); sum += hd[(size_t) i];
            }
            for (int i = 0; i < LD; ++i) hd[(size_t) i] = (float) (hd[(size_t) i] / sum);
        }
        reset();
    }
    void reset() noexcept { for (auto& r : ring) r.fill (0.0f); w = 0; }
    inline void push (float l, float r) noexcept { ring[0][(size_t) w] = ring[0][(size_t) w + 128] = l; ring[1][(size_t) w] = ring[1][(size_t) w + 128] = r; w = (w + 1) & 127; }
    // after Q pushes: the output sample (the filter centred (LD-1)/2 engine samples back)
    inline void out (float& l, float& r) const noexcept
    {
        const int newest = (w - 1) & 127;
        const float* a = &ring[0][(size_t) newest + 128]; const float* b = &ring[1][(size_t) newest + 128];
        float al = 0, ar = 0;
        for (int i = 0; i < LD; ++i) { al += hd[(size_t) i] * a[-i]; ar += hd[(size_t) i] * b[-i]; }
        l = al; r = ar;
    }
};

// a first-order TILT pair around the drive (base rate, stereo): pre() takes the lows down by `db` below `hz` before the saturator,
// post() is its exact inverse. Linear and time-invariant, so the pair is transparent for a clean signal; the saturator in between
// sees relatively less bass and more top, so the drive adds presence rather than low-mid weight (the bass keeps its shape)
struct TiltPair
{
    float p = 0, c = 0, g0 = 1; float zPre[2] {}, zPost[2] {};
    void set (double sr, double hz, double db)
    {
        p = (float) std::exp (-2.0 * 3.14159265358979323846 * hz / sr);         // the one-pole low pass pole
        c = 1.0f - (float) std::pow (10.0, -db / 20.0);                           // how much of the low pass is taken away
        g0 = 1.0f - c * (1.0f - p);                                               // the inverse's leading coefficient
    }
    void reset() noexcept { zPre[0] = zPre[1] = zPost[0] = zPost[1] = zPostY[0] = zPostY[1] = 0; }
    inline float pre (float x, int ch) noexcept { float& z = zPre[ch]; z = p * z + (1.0f - p) * x; return x - c * z; }
    // the inverse of (1 - c LP(z)), LP = (1-p) / (1 - p z^-1), i.e. (1 - p z^-1) / (g0 - p z^-1):  x[n] = (y[n] - p y[n-1] + p x[n-1]) / g0
    inline float post (float y, int ch) noexcept { float& z = zPost[ch]; const float out = (y - p * z + p * zPostY[ch]) / g0; z = y; zPostY[ch] = out; return out; }
    float zPostY[2] {};
};

// a short whole-sample delay (stereo) to keep a clean path aligned with the oversampled one
struct AlignDelay
{
    static constexpr int N = 64;
    float buf[2][N] {}; int w = 0;
    void reset() noexcept { for (auto& c : buf) std::fill (std::begin (c), std::end (c), 0.0f); w = 0; }
    inline void push (float l, float r, float& ol, float& orr, int delay) noexcept
    {
        buf[0][w] = l; buf[1][w] = r;
        const int rd = (w - delay + N) & (N - 1);
        ol = buf[0][rd]; orr = buf[1][rd];
        w = (w + 1) & (N - 1);
    }
};

// ---------------------------------------------------------------- the voice filter as one object
struct FilterChain
{
    Svf svfA, svfB, svfC; Ladder ladder;
    std::vector<float> comb[2]; int combW = 0;
    int type = 0; float fl0 = 1, combFb = 0, combMix = 1, kF = 1; double combD = 100; float formG[3] {};
    float S = 0;          // > 0: the saturating copy (states + output), 0 = linear (the old code)
    // LP 24 OPEN TOP (sound audit 2026-10-26, the 2x / 4x engine only): four poles AT the cut-off put the ladder -12 dB there, so the
    // knob fully open (18.9 kHz) still lost the top octave (-3 dB at 7.8 kHz on a saw). Above 8 kHz the poles move up smoothly, to 2.3 x
    // the knob at 18 kHz (where the -3 dB point lands at ~18.7 kHz); at and below 8 kHz the ladder (and its resonance's pitch) is as before.
    bool ladderOpen = false;      // (also: the V2 HP 24 resonance law, see set())
    static float ladderPoleHz (float fc, double rate) noexcept
    {
        const float t = std::min (1.0f, std::max (0.0f, (float) (std::log2 (std::max (1.0f, fc) / 8000.0f) / std::log2 (18000.0 / 8000.0))));
        const float sm = t * t * (3.0f - 2.0f * t);
        return std::min ((float) (0.45 * rate), fc * std::exp2 (1.2f * sm));
    }
    void prepare (double rate)
    {
        const int size = 1 << (int) std::ceil (std::log2 ((double) ((int) (rate / 18.0) + 8)));
        for (auto& c : comb) c.assign ((size_t) size, 0.0f);
        combW = 0; reset();
    }
    void reset() noexcept { resetFilters(); for (auto& cb : comb) std::fill (cb.begin(), cb.end(), 0.0f); }
    void resetFilters() noexcept { svfA.reset(); svfB.reset(); svfC.reset(); ladder.reset(); }
    // coefficients, exactly as the voice computed them before: fc in Hz (already clamped), res 0..1, formantPos 0..1 (type 8),
    // rate = the rate this copy runs at, maxHz = the voice's top frequency
    void set (int ft, float fc, float res, float formantPos, double rate, float maxHz)
    {
        type = ft;
        const float g = (float) std::tan (3.14159265358979323846 * fc / rate);
        const float q = 0.7f + res * 11.0f, k = 1.0f / q;
        switch (type)
        {
            case 4: ladder.set (ladderOpen && fc > 8000.0f ? (float) std::tan (3.14159265358979323846 * ladderPoleHz (fc, rate) / rate) : g, res); break;
            case 5:
            {   // HP 24 at max RES was two Q 7.6 stages: a +35 dB peak with no saturation anywhere (a sweep hit +18 dBFS before the ceiling).
                // V2 (sound audit 2026-10-26): Q up to 4 per stage, +24 dB at most - the LP 12 peak range (RES 0 is the same curve)
                const float kk = ladderOpen ? 1.0f / (0.6f + res * 3.4f) : 1.0f / (0.6f + res * 7.0f);
                svfA.set (g, kk); svfB.set (g, kk); break;
            }
            case 6: case 7:
                combD = std::min ((double) comb[0].size() - 4, std::max (2.0, rate / (double) std::max (25.0f, fc)));
                combFb = (type == 6 ? 1.0f : -1.0f) * (0.25f + res * 0.7f);
                combMix = std::pow (1.0f - std::abs (combFb), 0.6f);
                svfA.set ((float) std::tan (3.14159265358979323846 * std::min (maxHz, 9000.0f) / rate), 1.2f);
                break;
            case 8:
            {
                static const float V[5][3] { { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 290, 2290, 3010 }, { 570, 840, 2410 }, { 320, 870, 2240 } };
                const float vp = std::min (1.0f, std::max (0.0f, formantPos)) * 3.999f;
                const int a = (int) vp; const float t = vp - (float) a;
                const float Q = 4.0f + res * 14.0f; kF = 1.0f / Q;
                float ff[3];
                for (int j = 0; j < 3; ++j) ff[j] = std::min (maxHz, V[a][j] + (V[a + 1][j] - V[a][j]) * t);
                svfA.set ((float) std::tan (3.14159265358979323846 * ff[0] / rate), kF);
                svfB.set ((float) std::tan (3.14159265358979323846 * ff[1] / rate), kF);
                svfC.set ((float) std::tan (3.14159265358979323846 * ff[2] / rate), kF);
                formG[0] = 1.0f; formG[1] = 0.75f; formG[2] = 0.45f;
                break;
            }
            case 9: break;
            default: svfA.set (g, k); break;
        }
        fl0 = k;
    }
    // the linear filter: the voice's old code, op for op
    inline void tick (float l, float r, float& yl, float& yr) noexcept
    {
        yl = l; yr = r;
        float bp, hp;
        switch (type)
        {
            case 0: yl = svfA.tick (l, 0, bp, hp); yr = svfA.tick (r, 1, bp, hp); break;
            case 1: svfA.tick (l, 0, bp, hp); yl = bp * fl0 * 2.5f; svfA.tick (r, 1, bp, hp); yr = bp * fl0 * 2.5f; break;
            case 2: svfA.tick (l, 0, bp, hp); yl = hp; svfA.tick (r, 1, bp, hp); yr = hp; break;
            case 3: svfA.tick (l, 0, bp, hp); yl = l - fl0 * bp; svfA.tick (r, 1, bp, hp); yr = r - fl0 * bp; break;
            case 4: yl = ladder.tick (l, 0); yr = ladder.tick (r, 1); break;
            case 5: svfA.tick (l, 0, bp, hp); svfB.tick (hp, 0, bp, yl);
                    svfA.tick (r, 1, bp, hp); svfB.tick (hp, 1, bp, yr); break;
            case 6: case 7:
            {
                const int mask = (int) comb[0].size() - 1;
                const double rp = combW - combD; int ri = (int) std::floor (rp); const float t = (float) (rp - ri);
                for (int ch = 0; ch < 2; ++ch)
                {
                    auto& buf = comb[ch];
                    const float del = buf[(size_t) (ri & mask)] * (1 - t) + buf[(size_t) ((ri + 1) & mask)] * t;
                    const float damped = svfA.tick (del, ch, bp, hp);
                    const float x = ch == 0 ? l : r;
                    const float y = x + combFb * std::tanh (damped);
                    buf[(size_t) (combW & mask)] = y;
                    (ch == 0 ? yl : yr) = y * combMix;
                }
                combW = (combW + 1) & mask;
                break;
            }
            case 8:
            {
                float b1, b2, b3, h;
                svfA.tick (l, 0, b1, h); svfB.tick (l, 0, b2, h); svfC.tick (l, 0, b3, h);
                yl = (b1 * formG[0] + b2 * formG[1] + b3 * formG[2]) * kF * 2.2f;
                svfA.tick (r, 1, b1, h); svfB.tick (r, 1, b2, h); svfC.tick (r, 1, b3, h);
                yr = (b1 * formG[0] + b2 * formG[1] + b3 * formG[2]) * kF * 2.2f;
                break;
            }
            default: break;
        }
    }
    // the saturating filter (S > 0): every integrator state passes through satS inside the loop (so a resonant peak can never run
    // away and the drive gets the "circuit" rounding) and the output passes through it once more (the harmonics that cut through).
    // Measured on the 55 / 440 Hz sweeps: states at S (not 2 S) keeps the top end brightest for the same loudness.
    // Types 6..9 are not run saturating (the voice saturates in front of them instead).
    inline void tickSat (float l, float r, float& yl, float& yr) noexcept
    {
        yl = l; yr = r;
        const float S2 = S;
        float bp, hp;
        switch (type)
        {
            case 0: yl = svfA.tickS (l, 0, bp, hp, S2); yr = svfA.tickS (r, 1, bp, hp, S2); break;
            case 1: svfA.tickS (l, 0, bp, hp, S2); yl = bp * fl0 * 2.5f; svfA.tickS (r, 1, bp, hp, S2); yr = bp * fl0 * 2.5f; break;
            case 2: svfA.tickS (l, 0, bp, hp, S2); yl = hp; svfA.tickS (r, 1, bp, hp, S2); yr = hp; break;
            case 3: svfA.tickS (l, 0, bp, hp, S2); yl = l - fl0 * bp; svfA.tickS (r, 1, bp, hp, S2); yr = r - fl0 * bp; break;
            case 4: yl = ladder.tickS (l, 0, S); yr = ladder.tickS (r, 1, S); break;
            case 5: svfA.tickS (l, 0, bp, hp, S2); svfB.tickS (hp, 0, bp, yl, S2);
                    svfA.tickS (r, 1, bp, hp, S2); svfB.tickS (hp, 1, bp, yr, S2); break;
            default: tick (l, r, yl, yr); break;
        }
        yl = satS (yl, S); yr = satS (yr, S);
    }
};
} // namespace gpro
