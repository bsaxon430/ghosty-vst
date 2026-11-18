#pragma once
// CLEAN SOUND (2026-10-23): one shared module for layered sounds that stay crisp and never clip.
// Original DSP (no third-party code). Audio-thread rule: prepare() allocates, everything else is allocation- and lock-free.
//
//   Ceiling      the hidden final stage, ALWAYS on: a true-peak (4x) look-ahead limiter at -0.5 dBTP with a program-dependent
//                release, then a 4x oversampled soft clip that only ever acts on what is left above the knee (a residual: when
//                nothing is over, the output is the input, delayed, bit for bit). Fixed latency (reported to the host).
//   LevelMatch   auto-gain for a drive / saturation stage: the output follows the input's loudness (slow RMS ratio).
//   LayerPocket  one layer's place: a gentle high-pass when it does not own the bass, a stereo lean + width (its pocket),
//                and a side gain the SIDE BUDGET can pull down.
//   SumPockets   on the sum: DC / rumble high-pass, a dynamic dip in the mud band (150-400 Hz, only when it piles up),
//                MONO BELOW x Hz (mid / side), and mono safety (the width backs off when the mono sum would lose > 3 dB).
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <vector>
#include <atomic>
#include <algorithm>

namespace ghosty::clean
{
inline float coef (double sr, double ms) noexcept { return 1.0f - (float) std::exp (-1.0 / (std::max (1.0e-3, ms) * 0.001 * sr)); }
inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g) noexcept { return 20.0f * std::log10 (std::max (1.0e-9f, g)); }

// ---------------------------------------------------------------- 4x polyphase interpolator (windowed sinc, 32 taps / phase)
// at(): the three in-between points of x[n-H-1] .. x[n-H] (H = 15: the stream is delayed by H samples); phase 0 is x[n-H] itself.
struct Interp4
{
    static constexpr int T = 32, H = T / 2 - 1;
    float h[4][T] {};
    float hist[2][2 * T] {}; int w = 0;
    Interp4()
    {
        for (int p = 0; p < 4; ++p)
            for (int j = 0; j < T; ++j)
            {
                const double t = (double) (H - j) + (double) p / 4.0;     // distance of tap j from the point (n - H) - p/4 ... see push()
                const double x = juce::MathConstants<double>::pi * t;
                const double s = std::abs (t) < 1e-12 ? 1.0 : std::sin (x) / x;
                const double win = 0.5 + 0.5 * std::cos (juce::MathConstants<double>::pi * t / (H + 1.5));
                h[p][j] = p == 0 ? (j == H ? 1.0f : 0.0f) : (float) (s * win);
            }
        for (int p = 1; p < 4; ++p) { double sum = 0; for (int j = 0; j < T; ++j) sum += h[p][j]; for (int j = 0; j < T; ++j) h[p][j] = (float) (h[p][j] / sum); }
    }
    void reset() noexcept { std::fill (&hist[0][0], &hist[0][0] + 4 * T, 0.0f); w = 0; }
    // push one sample per channel; returns the delayed sample (phase 0) and fills the 3 points just BEFORE it (p = 1..3: n-H-p/4)
    inline void push (float l, float r) noexcept { hist[0][w] = hist[0][w + T] = l; hist[1][w] = hist[1][w + T] = r; w = (w + 1) % T; }
    inline float at (int ch, int p) const noexcept
    {   // tap j = x[n - j]  (newest first)
        const float* x = &hist[ch][w + T - 1];
        float acc = 0; for (int j = 0; j < T; ++j) acc += h[p][j] * x[-j];
        return acc;
    }
    inline float delayed (int ch) const noexcept { return hist[ch][w + T - 1 - H]; }
};

// ---------------------------------------------------------------- the hidden final stage
class Ceiling
{
public:
    float ceilingDb = -0.7f;        // limiter target (true peak; the soft clip above it ends at -0.3 dBFS)
    float clipTo = 0.966f;          // the soft clip's ceiling (-0.3 dBFS)
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        W = std::max (8, (int) std::lround (sr * 0.0010));            // look-ahead / attack window: 1 ms
        int cap = 16; while (cap < W + 8) cap <<= 1; qMask = cap - 1;
        qv.assign ((size_t) cap, 1.0f); qi.assign ((size_t) cap, 0); box.assign ((size_t) W, 1.0f);
        dlen = 1; while (dlen < W + Interp4::T + 8) dlen <<= 1;
        dl[0].assign ((size_t) dlen, 0.0f); dl[1].assign ((size_t) dlen, 0.0f);
        // decimation low pass for the residual (at 4x): windowed sinc, 49 taps (delay 24 = 6 base samples), cut at 0.45 fs (base)
        dec.assign ((size_t) DL, 0.0f);
        double sum = 0;
        for (int k = 0; k < DL; ++k)
        {
            const double t = k - (DL - 1) / 2.0, fc = 0.45 / 4.0;
            const double s = std::abs (t) < 1e-12 ? 2 * fc : std::sin (2 * juce::MathConstants<double>::pi * fc * t) / (juce::MathConstants<double>::pi * t);
            const double win = 0.42 - 0.5 * std::cos (2 * juce::MathConstants<double>::pi * k / (DL - 1)) + 0.08 * std::cos (4 * juce::MathConstants<double>::pi * k / (DL - 1));
            dec[(size_t) k] = (float) (s * win); sum += dec[(size_t) k];
        }
        for (auto& v : dec) v = (float) (v / sum * 4.0);       // x4: a zero-stuffed residual keeps its level after decimation
        res[0].assign ((size_t) RL * 2, 0.0f); res[1].assign ((size_t) RL * 2, 0.0f);
        cd[0].assign ((size_t) 64, 0.0f); cd[1].assign ((size_t) 64, 0.0f);
        relFast = coef (sr, 60.0); relSlow = coef (sr, 1200.0); grAvgA = coef (sr, 400.0);
        reset();
    }
    void reset()
    {
        for (auto& d : dl) std::fill (d.begin(), d.end(), 0.0f);
        for (auto& d : res) std::fill (d.begin(), d.end(), 0.0f);
        for (auto& d : cd) std::fill (d.begin(), d.end(), 0.0f);
        sc.reset(); up.reset();
        std::fill (box.begin(), box.end(), 1.0f); nonUnity = 0; boxSum = (double) W; bw = 0;
        head = tail = 0; idx = 0; rel = 1.0f; grAvg = 0; dw = 0; rw = 0; cw = 0; lastNz = -1000000; hotSc = hotUp = 0; n4 = 0;
    }
    // latency in samples (constant)
    int latency() const noexcept { return Interp4::H + (W - 1) + Interp4::H + Q; }

    // telemetry (read by the UI; written once per block)
    std::atomic<float> grDb { 0.0f }, inPeak { 0.0f }, outPeak { 0.0f };
    std::atomic<int> overs { 0 };                 // blocks in which the ceiling worked (limiter > 0.05 dB or the soft clip touched)
    float lastBlockGrDb = 0;                      // audio thread copy (tests)

    void process (float* L, float* R, int n) noexcept
    {
        const float cl = dbToGain (ceilingDb), scHot = cl * 0.5f;
        float minG = 1.0f, pin = 0.0f, pout = 0.0f; bool clipped = false;
        for (int i = 0; i < n; ++i)
        {
            float xl = L[i], xr = R[i];
            if (! (std::abs (xl) < 1.0e9f)) xl = 0.0f;          // NaN / inf guard (as the old output clip did)
            if (! (std::abs (xr) < 1.0e9f)) xr = 0.0f;
            pin = std::max (pin, std::max (std::abs (xl), std::abs (xr)));
            // --- side chain: true peak of x around (i - H)
            sc.push (xl, xr);
            if (std::abs (xl) > scHot || std::abs (xr) > scHot) hotSc = Interp4::T + 2;
            float a = std::max (std::abs (sc.delayed (0)), std::abs (sc.delayed (1)));
            if (hotSc > 0)
            {
                --hotSc;
                for (int p = 1; p < 4; ++p) a = std::max (a, std::max (std::abs (sc.at (0, p)), std::abs (sc.at (1, p))));
            }
            const float need = a > cl ? cl / a : 1.0f;
            // sliding minimum over W (monotonic queue)
            while (head != tail && qv[(size_t) ((tail - 1) & qMask)] >= need) tail = (tail - 1) & qMask;
            qv[(size_t) tail] = need; qi[(size_t) tail] = idx; tail = (tail + 1) & qMask;
            while (qi[(size_t) head] <= idx - W) head = (head + 1) & qMask;
            const float m = qv[(size_t) head];
            // program-dependent release: fast after a short peak, slow once the gain reduction has been going on (sustained pads)
            const float grNow = 1.0f - rel;
            grAvg += grAvgA * (grNow - grAvg);
            const float sustain = std::min (1.0f, grAvg * 8.0f);          // ~1 dB average gain reduction -> fully slow
            const float rc = relFast + (relSlow - relFast) * sustain;
            rel = std::min (m, rel + rc * (1.0f - rel));
            if (rel > 0.99999f && m >= 1.0f) rel = 1.0f;
            // box average over W: the gain at the peak's exit is at or below what it needs
            const float old = box[(size_t) bw];
            if (old != 1.0f) --nonUnity;
            if (rel != 1.0f) ++nonUnity;
            boxSum += (double) rel - (double) old; box[(size_t) bw] = rel; bw = (bw + 1 == W) ? 0 : bw + 1;
            float g = 1.0f;
            if (nonUnity > 0) g = (float) (boxSum / (double) W); else boxSum = (double) W;
            ++idx;
            // audio: x delayed by H (side chain) + W - 1 (look-ahead)
            dl[0][(size_t) dw] = xl; dl[1][(size_t) dw] = xr;
            const int rd = (dw - (Interp4::H + W - 1)) & (dlen - 1);
            dw = (dw + 1) & (dlen - 1);
            float yl = dl[0][(size_t) rd], yr = dl[1][(size_t) rd];
            if (g != 1.0f) { yl *= g; yr *= g; }
            minG = std::min (minG, g);
            // --- residual soft clip at 4x: only what is above the knee, filtered and added back (zero residual = exact pass)
            up.push (yl, yr);
            if (std::abs (yl) > knee * 0.7f || std::abs (yr) > knee * 0.7f) hotUp = Interp4::T + 2;
            for (int c = 0; c < 2; ++c)
            {
                // the four 4x points of base time (i - H): phases 3, 2, 1 lie before it, then the sample itself
                float r4[4];
                for (int p = 0; p < 4; ++p)
                {
                    float u;
                    if (p == 0) u = up.delayed (c);
                    else u = hotUp > 0 ? up.at (c, p) : 0.0f;
                    r4[p] = residual (u);
                }
                // store in time order: n-H-3/4, n-H-2/4, n-H-1/4, n-H
                float* rb = res[c].data();
                const int base = rw;
                rb[(base + 0) % RL] = rb[(base + 0) % RL + RL] = r4[3];
                rb[(base + 1) % RL] = rb[(base + 1) % RL + RL] = r4[2];
                rb[(base + 2) % RL] = rb[(base + 2) % RL + RL] = r4[1];
                rb[(base + 3) % RL] = rb[(base + 3) % RL + RL] = r4[0];
                if (r4[0] != 0.0f || r4[1] != 0.0f || r4[2] != 0.0f || r4[3] != 0.0f) lastNz = n4 + 3;
            }
            if (hotUp > 0) --hotUp;
            rw = (rw + 4) % RL; n4 += 4;
            // the dry path: y delayed by H (the interpolator) + Q (half the decimation filter)
            cd[0][(size_t) cw] = up.delayed (0); cd[1][(size_t) cw] = up.delayed (1);
            const int cr = (cw - Q) & 63; cw = (cw + 1) & 63;
            float ol = cd[0][(size_t) cr], orr = cd[1][(size_t) cr];
            if (n4 - 1 - lastNz < DL + 4)
            {   // the decimated residual centred on base time (i - H - Q): 4x index (n4 - 1) - 4Q
                const int newest = (rw - 1 + RL) % RL;
                for (int c = 0; c < 2; ++c)
                {
                    const float* rb = res[c].data() + newest + RL;     // rb[-k] = residual k 4x-samples ago
                    double acc = 0; for (int k = 0; k < DL; ++k) acc += (double) dec[(size_t) k] * rb[-k];
                    (c == 0 ? ol : orr) += (float) acc;
                }
                clipped = true;
            }
            ol = std::max (-clipTo, std::min (clipTo, ol)); orr = std::max (-clipTo, std::min (clipTo, orr));   // numerical safety only
            L[i] = ol; R[i] = orr;
            pout = std::max (pout, std::max (std::abs (ol), std::abs (orr)));
        }
        lastBlockGrDb = gainToDb (minG);
        grDb.store (lastBlockGrDb, std::memory_order_relaxed);
        inPeak.store (pin, std::memory_order_relaxed); outPeak.store (pout, std::memory_order_relaxed);
        if (lastBlockGrDb < -0.05f || clipped) overs.fetch_add (1, std::memory_order_relaxed);
    }

private:
    static constexpr int DL = 49, Q = (DL - 1) / 8, RL = 64;
    static constexpr float knee = 0.923f;              // -0.7 dBFS: the limiter's target; the soft clip only catches what slips past
    inline float residual (float u) const noexcept
    {
        const float a = std::abs (u);
        if (a <= knee) return 0.0f;
        const float K = clipTo - knee;
        const float y = knee + K * std::tanh ((a - knee) / K);
        return std::copysign (y - a, u);
    }
    double sr = 48000; int W = 48;
    std::vector<float> qv, box; std::vector<int64_t> qi; int qMask = 15, head = 0, tail = 0, bw = 0, nonUnity = 0; int64_t idx = 0; double boxSum = 0;
    float rel = 1.0f, grAvg = 0, relFast = 0.001f, relSlow = 0.0001f, grAvgA = 0.0001f;
    std::vector<float> dl[2]; int dlen = 64, dw = 0;
    std::vector<float> dec, res[2], cd[2]; int rw = 0, cw = 0; int64_t lastNz = -1000000, n4 = 0;
    Interp4 sc, up; int hotSc = 0, hotUp = 0;
};

// ---------------------------------------------------------------- auto-gain for a drive stage
// Feed the stage's input power and output power per sample; gain() follows sqrt (in / out) (slow, ~200 ms), within [lo, hi].
struct LevelMatch
{
    float a = 0.0003f, ga = 0.002f, inP = 0, outP = 0, g = 1.0f, target = 1.0f; int cnt = 0; bool primed = false;
    void prepare (double sr) { a = coef (sr, 200.0); ga = coef (sr, 25.0); reset(); }
    void reset() noexcept { inP = outP = 0; g = target = 1.0f; cnt = 0; primed = false; }
    inline float step (float inPow, float outPow, float lo, float hi) noexcept
    {
        if (! primed && (inPow > 1.0e-10f || outPow > 1.0e-10f)) { inP = inPow; outP = outPow; primed = true; }
        inP += a * (inPow - inP) + 1.0e-24f; outP += a * (outPow - outP) + 1.0e-24f;
        if (++cnt >= 16)
        {
            cnt = 0;
            target = (outP > 1.0e-12f && inP > 1.0e-12f) ? std::sqrt (inP / outP) : target;
            target = std::max (lo, std::min (hi, target));
        }
        g += ga * (target - g);
        return g;
    }
    // the same with a loudness-like weighting (a first-difference tilt: about +2 dB at 5 kHz, +5 dB at 10 kHz, like K-weighting),
    // so a saturator's added top end does not read as "quieter"
    float x1[4] {};
    inline float stepWeighted (float inL, float inR, float outL, float outR, float lo, float hi) noexcept
    {
        constexpr float k = 0.8f;
        const float a = inL + k * (inL - x1[0]), b = inR + k * (inR - x1[1]), c = outL + k * (outL - x1[2]), d = outR + k * (outR - x1[3]);
        x1[0] = inL; x1[1] = inR; x1[2] = outL; x1[3] = outR;
        return step (a * a + b * b, c * c + d * d, lo, hi);
    }
};

// ---------------------------------------------------------------- simple biquad (TDF2), stereo
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; float z1[2] {}, z2[2] {};
    void reset() noexcept { z1[0] = z1[1] = z2[0] = z2[1] = 0; }
    void highpass (double sr, double f, double q)
    {
        const double w = 2 * juce::MathConstants<double>::pi * f / sr, c = std::cos (w), s = std::sin (w), al = s / (2 * q), a0 = 1 + al;
        b0 = (float) ((1 + c) / 2 / a0); b1 = (float) (-(1 + c) / a0); b2 = b0; a1 = (float) (-2 * c / a0); a2 = (float) ((1 - al) / a0);
    }
    void lowpass (double sr, double f, double q)
    {
        const double w = 2 * juce::MathConstants<double>::pi * f / sr, c = std::cos (w), s = std::sin (w), al = s / (2 * q), a0 = 1 + al;
        b0 = (float) ((1 - c) / 2 / a0); b1 = (float) ((1 - c) / a0); b2 = b0; a1 = (float) (-2 * c / a0); a2 = (float) ((1 - al) / a0);
    }
    void bandpass (double sr, double f, double q)      // unity gain at the centre
    {
        const double w = 2 * juce::MathConstants<double>::pi * f / sr, c = std::cos (w), s = std::sin (w), al = s / (2 * q), a0 = 1 + al;
        b0 = (float) (al / a0); b1 = 0; b2 = (float) (-al / a0); a1 = (float) (-2 * c / a0); a2 = (float) ((1 - al) / a0);
    }
    inline float tick (float x, int ch) noexcept
    {
        const float y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch]; z2[ch] = b2 * x - a2 * y;
        return y;
    }
};

// ---------------------------------------------------------------- one layer's pocket
struct LayerPocket
{
    // set per block
    float hpHz = 100.0f;      // LOW CUT
    float hpAmt = 0.0f;       // 0 = owns the bass (no cut) .. 1 = cut (smoothed inside)
    float lean = 0.0f;        // -1 .. 1 stereo lean (smoothed inside)
    float width = 1.0f;       // side gain from its pocket (smoothed inside)
    float budget = 1.0f;      // SIDE BUDGET: 1 = free, < 1 = narrowed (smoothed inside)
    // measured (audio thread): low-band (< 120 Hz) power and overall power, slow
    float lowPow = 0, allPow = 0;
    void prepare (double s) { sr = s; sm = coef (sr, 60.0); meas = coef (sr, 250.0); lp.lowpass (sr, 120.0, 0.7071); setHp (hpHz, true); reset(); }
    void reset() noexcept { hpS = 0; hpSm = hpAmt; leanSm = lean; widthSm = width; budgetSm = budget; lowPow = allPow = 0; hp1.reset(); lp.reset(); }
    void snap() noexcept { hpSm = hpAmt; leanSm = lean; widthSm = width; budgetSm = budget; }
    void process (float* L, float* R, int n) noexcept
    {
        setHp (hpHz, false);
        for (int i = 0; i < n; ++i)
        {
            float l = L[i], r = R[i];
            {   // measure (before any change)
                const float m = 0.5f * (l + r), lo = lp.tick (m, 0);
                lowPow += meas * (lo * lo - lowPow) + 1.0e-24f; allPow += meas * (m * m - allPow) + 1.0e-24f;
            }
            hpSm += sm * (hpAmt - hpSm); leanSm += sm * (lean - leanSm); widthSm += sm * (width - widthSm); budgetSm += sm * (budget - budgetSm);
            if (hpSm > 1.0e-4f)
            {
                const float hl = hp1.tick (l, 0), hr = hp1.tick (r, 1);
                l += hpSm * (hl - l); r += hpSm * (hr - r);
            }
            else { hp1.tick (l, 0); hp1.tick (r, 1); }      // keep the filter warm: no click when it comes in
            const float sg = widthSm * budgetSm;
            if (sg != 1.0f) { const float m = 0.5f * (l + r), s = 0.5f * (l - r) * sg; l = m + s; r = m - s; }
            if (leanSm != 0.0f)
            {   // equal-power lean: the layer moves to its side without getting quieter
                const float a = (leanSm + 1.0f) * 0.785398f;
                l *= 1.41421f * std::cos (a); r *= 1.41421f * std::sin (a);
            }
            L[i] = l; R[i] = r;
        }
    }
    float rmsDb() const noexcept { return 10.0f * std::log10 (allPow + 1.0e-20f); }
private:
    void setHp (float f, bool force) { if (force || std::abs (f - hpSet) > 0.5f) { hpSet = f; hp1.highpass (sr, f, 0.7071); } }
    double sr = 48000; float sm = 0.001f, meas = 0.0001f, hpS = 0, hpSm = 0, leanSm = 0, widthSm = 1, budgetSm = 1, hpSet = -1;
    Biquad hp1, lp;
};

// ---------------------------------------------------------------- the sum
struct SumPockets
{
    float monoHz = 120.0f;    // MONO BELOW (0 = off)
    float mud = 0.5f;         // MUD (0 = off .. 1 = up to -6 dB dip)
    float layers = 1.0f;      // how many layers sound right now: the dip only acts when two or more pile up (a single sound keeps its body)
    std::atomic<float> mudDipDb { 0.0f }, widthGain { 1.0f }, corr { 1.0f }, monoLossDb { 0.0f };
    void prepare (double s)
    {
        sr = s; dc.highpass (sr, 22.0, 0.7071); dc2 = dc;
        bp.bandpass (sr, 250.0, 0.85); bpDet = bp;
        mudA = coef (sr, 50.0); mudGa = coef (sr, 40.0); mudGr = coef (sr, 250.0);
        mlp.lowpass (sr, 4000.0, 0.7071); wA = coef (sr, 200.0); wG = coef (sr, 80.0); corrA = coef (sr, 300.0);
        setMono (monoHz, true); reset();
    }
    void reset() noexcept
    {
        dc.reset(); dc2.reset(); bp.reset(); bpDet.reset(); s1.reset(); s2.reset(); s3.reset(); mlp.reset();
        bandP = allP = 0; dip = 0; mP = sP = 0; wg = 1; cLR = cLL = cRR = 0;
    }
    void process (float* L, float* R, int n) noexcept
    {
        setMono (monoHz, false);
        const float maxDip = std::max (0.0f, std::min (1.0f, mud)) * 6.0f * std::max (0.0f, std::min (1.0f, layers - 1.0f)), thrDb = -7.0f;
        float dipMax = 0;
        for (int i = 0; i < n; ++i)
        {
            float l = dc.tick (L[i], 0), r = dc.tick (R[i], 1);          // DC / rumble: 2nd order at 22 Hz (no overshoot to speak of)
            // --- MUD: how much of the power sits in 150-400 Hz; above the threshold a peaking cut there grows (gently)
            if (maxDip > 0.0f || dip > 0.001f)
            {
                const float m = 0.5f * (l + r), b = bpDet.tick (m, 0);
                bandP += mudA * (b * b - bandP) + 1.0e-24f; allP += mudA * (m * m - allP) + 1.0e-24f;
                const float share = 10.0f * std::log10 (bandP / allP);
                const float want = std::min (maxDip, std::max (0.0f, (share - thrDb) * 1.5f));
                dip += (want > dip ? mudGa : mudGr) * (want - dip);
                const float gcut = dbToGain (-dip) - 1.0f;
                l += gcut * bp.tick (l, 0); r += gcut * bp.tick (r, 1);
                dipMax = std::max (dipMax, dip);
            }
            // --- MONO BELOW: the side signal is high-passed (4th order)
            float m = 0.5f * (l + r), s = 0.5f * (l - r);
            if (monoHz > 1.0f) s = s3.tick (s2.tick (s1.tick (s, 0), 0), 0);
            // --- MONO SAFETY: below ~4 kHz the side may not outweigh the mid (the mono sum would lose more than ~3 dB)
            const float ml = mlp.tick (m, 0), sl = mlp.tick (s, 1);
            mP += wA * (ml * ml - mP) + 1.0e-24f; sP += wA * (sl * sl - sP) + 1.0e-24f;
            const float wt = sP > 0.8f * mP ? std::sqrt (0.8f * mP / sP) : 1.0f;
            wg += wG * (wt - wg);
            if (wg < 0.9999f) s = (s - sl) + sl * wg;
            l = m + s; r = m - s;
            cLR += corrA * (l * r - cLR); cLL += corrA * (l * l - cLL) + 1.0e-24f; cRR += corrA * (r * r - cRR) + 1.0e-24f;
            L[i] = l; R[i] = r;
        }
        mudDipDb.store (dipMax, std::memory_order_relaxed); widthGain.store (wg, std::memory_order_relaxed);
        corr.store (cLR / std::sqrt (cLL * cRR), std::memory_order_relaxed);
        monoLossDb.store (10.0f * std::log10 ((mP + 1.0e-20f) / (mP + sP * wg * wg + 1.0e-20f)), std::memory_order_relaxed);
    }
private:
    // the side high-pass sits a little above the MONO BELOW frequency (x1.3, 6th order): below the knob's value the side is > 17 dB down
    void setMono (float f, bool force) { if (force || std::abs (f - monoSet) > 0.5f) { monoSet = f; if (f > 1.0f) { s1.highpass (sr, juce::jmin (sr * 0.2, f * 1.3), 0.7071); s2.b0 = s1.b0; s2.b1 = s1.b1; s2.b2 = s1.b2; s2.a1 = s1.a1; s2.a2 = s1.a2; s3.b0 = s1.b0; s3.b1 = s1.b1; s3.b2 = s1.b2; s3.a1 = s1.a1; s3.a2 = s1.a2; } } }
    double sr = 48000; Biquad dc, dc2, bp, bpDet, s1, s2, s3, mlp;
    float mudA = 0, mudGa = 0, mudGr = 0, bandP = 0, allP = 0, dip = 0, monoSet = -1;
    float wA = 0, wG = 0, corrA = 0, mP = 0, sP = 0, wg = 1, cLR = 0, cLL = 0, cRR = 0;
};
} // namespace ghosty::clean
