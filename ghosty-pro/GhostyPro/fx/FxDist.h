#pragma once
// DISTORTION (Ghosty Pro, FX RACK round 3, 2026-10-25). Original DSP (no third-party code).
//   10 modes: TUBE, SOFT CLIP, HARD CLIP, DIODE, LIN FOLD, SIN FOLD, ZERO-SQUARE, DOWNSAMPLE, ASYM and X-SHAPER (two drawn transfer
//   curves A / B, DRIVE morphs A -> B, symmetric or asymmetric). Every curve runs 4x oversampled (two half-band IIR stages, -100 /
//   -90 dB stop bands) AND with first-order antiderivative anti-aliasing (ADAA: y = (F(x) - F(x1)) / (x - x1), F = the curve's
//   integral, in double), so a hard clip at full drive keeps its aliases below -70 dB. DOWNSAMPLE is a deliberate sample-and-hold.
//   Filter OFF / PRE / POST (the position glides, no click): a state-variable filter whose TYPE morphs LP -> BP -> HP, FREQ follows the
//   last played note by KEY TRACK, Q. AUTO GAIN is static (no pumping): for each mode and drive the loudness the curve adds on a -6 dBFS
//   sine is measured once and halved, so the sound gets louder half as fast as it gets denser. LEVEL trims after it; the rack's MIX
//   blends with the (latency-aligned) dry: MIX 0 = bypass, bit for bit.
// Audio-thread rule: prepare() allocates, process() never does; X-SHAPER's curves come in through a try-lock (never waits).
#include "FxProCommon.h"

#if ! GPFX_BOX
namespace gpfx
{
enum DistMode { dmTube, dmSoft, dmHard, dmDiode, dmLinFold, dmSinFold, dmZeroSq, dmDownsample, dmAsym, dmXShaper, kNumDistModes };
inline const juce::StringArray& distModeNames()
{
    static juce::StringArray s { "TUBE", "SOFT CLIP", "HARD CLIP", "DIODE", "LIN FOLD", "SIN FOLD", "ZERO-SQUARE", "DOWNSAMPLE", "ASYM", "X-SHAPER" };
    return s;
}
inline const char* distModeBlurb (int m)
{
    static const char* b[] { "warm, lopsided tube curve: even + odd harmonics", "smooth round clipping, the classic overdrive",
                             "flat-topped clipping: bright and aggressive", "fast silicon knee, the negative side clips harder",
                             "folds back in straight lines past full scale: metallic", "folds back along a sine: rich, vocal, moving",
                             "squares the wave around zero: gated, buzzy, broken-speaker", "sample-and-hold rate reduction: digital grit",
                             "soft on top, hard underneath: thick and asymmetric", "your two drawn curves, DRIVE morphs A into B" };
    return b[juce::jlimit (0, (int) kNumDistModes - 1, m)];
}

// ---------------------------------------------------------------- the curves: f (transfer) and F (its antiderivative), double
namespace distshape
{
inline double logcosh (double x) noexcept { const double a = std::abs (x); return a + std::log1p (std::exp (-2.0 * a)) - 0.69314718055994531; }
constexpr double kTubeB = 0.25, kDiodeK = 0.6;
inline double f (int m, double x) noexcept
{
    switch (m)
    {
        case dmTube:    return std::tanh (x + kTubeB) - std::tanh (kTubeB);
        case dmSoft:    return std::tanh (x);
        case dmHard:    return x > 1.0 ? 1.0 : (x < -1.0 ? -1.0 : x);
        case dmDiode:   return x >= 0.0 ? 1.0 - std::exp (-x) : -kDiodeK * (1.0 - std::exp (x / kDiodeK));
        case dmLinFold: { const double u = positiveMod (x + 1.0, 4.0); return 1.0 - std::abs (u - 2.0); }
        case dmSinFold: return std::sin (x);
        case dmZeroSq:  { const double a = std::abs (x); return a <= 1.0 ? x * a : (x > 0 ? 1.0 : -1.0); }
        case dmAsym:    return x >= 0.0 ? std::tanh (x) : 0.5 * std::tanh (2.0 * x);
        default:        return x;
    }
}
inline double F (int m, double x) noexcept
{
    switch (m)
    {
        case dmTube:    return logcosh (x + kTubeB) - logcosh (kTubeB) - x * std::tanh (kTubeB);
        case dmSoft:    return logcosh (x);
        case dmHard:    { const double a = std::abs (x); return a <= 1.0 ? 0.5 * x * x : a - 0.5; }
        case dmDiode:   return x >= 0.0 ? x + std::exp (-x) - 1.0 : -kDiodeK * x + kDiodeK * kDiodeK * (std::exp (x / kDiodeK) - 1.0);
        case dmLinFold:
        {   // the triangle's integral over a period is 0: G (u) on [0, 4), F (0) = 0
            const double u = positiveMod (x + 1.0, 4.0);
            const double G = u <= 2.0 ? 0.5 * u * u - u : 3.0 * (u - 2.0) - 0.5 * (u * u - 4.0);
            return G + 0.5;
        }
        case dmSinFold: return 1.0 - std::cos (x);
        case dmZeroSq:  { const double a = std::abs (x); return a <= 1.0 ? a * a * a / 3.0 : a - 2.0 / 3.0; }
        case dmAsym:    return x >= 0.0 ? logcosh (x) : 0.25 * logcosh (2.0 * x);
        default:        return 0.5 * x * x;
    }
}
} // namespace distshape

// ---------------------------------------------------------------- X-SHAPER: two drawn curves (17 points on -1 .. 1 each)
struct DistCurves
{
    static constexpr int kPts = 17, kTab = 512;
    std::array<float, kPts> a {}, b {}; bool symmetric = true;
    static DistCurves defaults()
    {
        DistCurves c;
        for (int k = 0; k < kPts; ++k)
        {
            const double x = -1.0 + 2.0 * k / (kPts - 1);
            c.a[(size_t) k] = (float) (std::tanh (2.2 * x) / std::tanh (2.2));                      // A: a round clip
            c.b[(size_t) k] = (float) (0.95 * std::sin (2.5 * juce::MathConstants<double>::pi * x)); // B: a double fold
        }
        return c;
    }
    bool operator== (const DistCurves& o) const { return a == o.a && b == o.b && symmetric == o.symmetric; }
    static juce::String toString (const std::array<float, kPts>& v) { juce::StringArray s; for (auto x : v) s.add (juce::String::formatted ("%.9g", (double) x)); return s.joinIntoString (","); }
    static void fromString (std::array<float, kPts>& v, const juce::String& t)
    {
        const auto s = juce::StringArray::fromTokens (t, ",", "");
        if (s.size() == kPts) for (int k = 0; k < kPts; ++k) v[(size_t) k] = clampf (s[k].getFloatValue(), -1.0f, 1.0f);
    }
};
// The tables the audio thread reads: a 513-point piecewise-linear curve and its exact running integral, per curve, plus the
// auto-gain over the morph (17 steps).
struct DistTables
{
    std::array<double, DistCurves::kTab + 1> TA {}, CA {}, TB {}, CB {};
    std::array<float, 17> comp {};
    static void build (const std::array<float, DistCurves::kPts>& pts, bool sym, std::array<double, DistCurves::kTab + 1>& T, std::array<double, DistCurves::kTab + 1>& C)
    {
        constexpr int N = DistCurves::kTab; constexpr int P = DistCurves::kPts;
        auto pt = [&] (int k) -> double
        {   // symmetric: the right half mirrored (point 8 = the centre is 0)
            k = juce::jlimit (0, P - 1, k);
            if (! sym) return pts[(size_t) k];
            const int c = (P - 1) / 2;
            if (k == c) return 0.0;
            return k > c ? pts[(size_t) k] : -pts[(size_t) (2 * c - k)];
        };
        for (int i = 0; i <= N; ++i)
        {   // Catmull-Rom through the points
            const double pos = (double) i / N * (P - 1); const int k = std::min (P - 2, (int) pos); const double t = pos - k;
            const double p0 = pt (k - 1), p1 = pt (k), p2 = pt (k + 1), p3 = pt (k + 2);
            const double y = 0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t + (-p0 + 3 * p1 - 3 * p2 + p3) * t * t * t);
            T[(size_t) i] = juce::jlimit (-1.25, 1.25, y);
        }
        const double h = 2.0 / N; C[0] = 0.0;
        for (int i = 0; i < N; ++i) C[(size_t) i + 1] = C[(size_t) i] + h * 0.5 * (T[(size_t) i] + T[(size_t) i + 1]);
    }
    static double f (const std::array<double, DistCurves::kTab + 1>& T, double x) noexcept
    {
        constexpr int N = DistCurves::kTab;
        if (x >= 1.0) return T[N];
        if (x <= -1.0) return T[0];
        const double pos = (x + 1.0) * 0.5 * N; const int i = std::min (N - 1, (int) pos); const double fr = pos - i;
        return T[(size_t) i] + fr * (T[(size_t) i + 1] - T[(size_t) i]);
    }
    static double F (const std::array<double, DistCurves::kTab + 1>& T, const std::array<double, DistCurves::kTab + 1>& C, double x) noexcept
    {
        constexpr int N = DistCurves::kTab; const double h = 2.0 / N;
        if (x >= 1.0) return C[N] + (x - 1.0) * T[N];
        if (x <= -1.0) return (x + 1.0) * T[0];
        const double pos = (x + 1.0) * 0.5 * N; const int i = std::min (N - 1, (int) pos); const double fr = pos - i;
        return C[(size_t) i] + h * (T[(size_t) i] * fr + 0.5 * (T[(size_t) i + 1] - T[(size_t) i]) * fr * fr);
    }
    void make (const DistCurves& c);
};

// rms gain (dB) a static curve adds to a -6 dBFS sine at input gain g (no aliasing question here: just loudness)
template <typename Fn> inline double curveGainDb (Fn&& fn, double g)
{
    constexpr int M = 1024; double si = 0, so = 0;
    for (int i = 0; i < M; ++i)
    {
        const double x = 0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * (i + 0.5) / M);
        const double y = fn (x * g); si += x * x; so += y * y;
    }
    // DC the curve makes is removed by the unit's DC blocker: measure without it
    double mean = 0; for (int i = 0; i < M; ++i) mean += fn (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * (i + 0.5) / M) * g);
    mean /= M; so -= M * mean * mean;
    return 10.0 * std::log10 (std::max (1e-20, so) / std::max (1e-20, si));
}
inline void DistTables::make (const DistCurves& c)
{
    build (c.a, c.symmetric, TA, CA); build (c.b, c.symmetric, TB, CB);
    for (int k = 0; k < 17; ++k)
    {
        const double m = k / 16.0;
        const double gdb = curveGainDb ([&] (double x) { return (1.0 - m) * f (TA, x) + m * f (TB, x); }, 1.0);
        comp[(size_t) k] = (float) (-0.5 * gdb);
    }
}

// Shared by every DISTORTION instance (the rack's and a splitter's): message thread edits, audio threads copy on a version change.
struct DistCurveStore
{
    juce::SpinLock lock; std::atomic<int> version { 1 };
    DistCurves curves = DistCurves::defaults(); DistTables tables;   // (under the lock)
    DistCurveStore() { tables.make (curves); }
    void set (const DistCurves& c)
    {
        DistTables t; t.make (c);                                    // (built outside the lock)
        { const juce::SpinLock::ScopedLockType sl (lock); curves = c; tables = t; }
        version.fetch_add (1, std::memory_order_release);
    }
    DistCurves get() { const juce::SpinLock::ScopedLockType sl (lock); return curves; }
};

// static auto-gain of the fixed modes: dB per drive step (0 .. 36 dB in 1.5 dB steps), the loudness the curve adds, halved
inline const std::array<std::array<float, 25>, kNumDistModes>& distCompTable()
{
    static const auto t = []
    {
        std::array<std::array<float, 25>, kNumDistModes> r {};
        for (int m = 0; m < kNumDistModes; ++m)
            for (int k = 0; k < 25; ++k)
            {
                if (m == dmDownsample || m == dmXShaper) { r[(size_t) m][(size_t) k] = 0.0f; continue; }
                const double g = std::pow (10.0, (m == dmLinFold ? 0.75 : 1.5) * k / 20.0);   // (LIN FOLD: half the drive, see modeGain)
                r[(size_t) m][(size_t) k] = (float) (-0.5 * curveGainDb ([m] (double x) { return distshape::f (m, x); }, g));   // half of what it adds
            }
        return r;
    }();
    return t;
}

class DistUnit : public UnitDsp
{
public:
    Telemetry* tm = nullptr; DistCurveStore* store = nullptr;
    std::atomic<float>* hostOn = nullptr;   // fx_dist_on: the oversampler's latency is reported only while the unit is on (null = always: a SPLITTER slot)
    void attach (juce::AudioProcessorValueTreeState& s) override
    {
        mode.bind (s, pid (uDist, "mode"), 0); drive.bind (s, pid (uDist, "drive"), 12.0f); level.bind (s, pid (uDist, "level"), 0.0f);
        fpos.bind (s, pid (uDist, "fpos"), 0); ftype.bind (s, pid (uDist, "ftype"), 0.0f); freq.bind (s, pid (uDist, "freq"), 1000.0f);
        q.bind (s, pid (uDist, "q"), 0.707f); fkey.bind (s, pid (uDist, "fkey"), 0.0f);
        hostOn = s.getRawParameterValue (pid (uDist, "on"));
    }
    void prepare (double sr_, int) override
    {
        sr = sr_;
        os.clearOversamplingStages();
        os.addOversamplingStage (juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, 0.05f, -100.0f, 0.05f, -100.0f);
        os.addOversamplingStage (juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, 0.10f, -90.0f, 0.10f, -90.0f);
        os.setUsingIntegerLatency (true);
        os.initProcessing ((size_t) kSub); os.reset();
        lat = (int) std::lround (os.getLatencyInSamples());
        dry.prepare (lat + 8, sr); dry.cur = dry.old = latency();
        const double osr = sr * 4.0;
        driveSm.setTime (osr, 25.0f); levelSm.setTime (sr, 20.0f); preSm.setTime (sr, 15.0f); postSm.setTime (sr, 15.0f);
        typeSm.setTime (sr, 20.0f); freqSm.setTime (sr, 20.0f); qSm.setTime (sr, 20.0f);
        for (auto& d : dc) d.prepare (sr);
        reset();
    }
    void reset() override
    {
        os.reset();
        for (auto& d : dc) d.reset();
        for (auto& f : svf) for (auto& c : f) c.reset();
        x1[0] = x1[1] = 0.0; hold[0] = hold[1] = 0.0f; holdPh = 1.0;
        curMode = prevMode = juce::jlimit (0, kNumDistModes - 1, mode.i()); fade = 0;
        driveSm.snap (drive()); levelSm.snap (dbToGain (level()));
        preSm.snap (fpos.i() == 1 ? 1.0f : 0.0f); postSm.snap (fpos.i() == 2 ? 1.0f : 0.0f);
        typeSm.snap (ftype()); freqSm.snap (std::log2 (cutHz (60))); qSm.snap (q());
        syncCurves (true);
    }
    // (only while on: an older sound that never uses DISTORTION keeps its exact timing; switching on crossfades the dry's delay)
    int latency() const override { return hostOn == nullptr || hostOn->load (std::memory_order_relaxed) > 0.5f ? lat : 0; }
    void delayDry (float* L, float* R, int n) override { dry.process (L, R, n, latency()); }
    void clearLines() override { dry.clear(); }
    void process (float* L, float* R, int n, const BlockCtx& ctx) override
    {
        syncCurves (false);
        const int m = juce::jlimit (0, kNumDistModes - 1, mode.i());
        if (m != curMode) { prevMode = curMode; curMode = m; fadeLen = fade = (int) (sr * 4.0 * 0.02); }
        if (ctx.lastNote >= 0) note = ctx.lastNote;
        // ---- smoothed controls (base rate)
        levelSm.target = dbToGain (juce::jlimit (-24.0f, 12.0f, level()));
        preSm.target = fpos.i() == 1 ? 1.0f : 0.0f; postSm.target = fpos.i() == 2 ? 1.0f : 0.0f;
        typeSm.target = clampf (ftype(), 0.0f, 1.0f); freqSm.target = std::log2 (cutHz (note)); qSm.target = juce::jlimit (0.5f, 12.0f, q());
        const bool preOn = preSm.cur > 1.0e-5f || preSm.target > 0.0f, postOn = postSm.cur > 1.0e-5f || postSm.target > 0.0f;
        if (preOn || postOn)
            for (int i = 0; i < n; ++i) { tArr[(size_t) i] = typeSm.next(); fArr[(size_t) i] = std::exp2 (freqSm.next()); qArr[(size_t) i] = qSm.next(); }
        else { typeSm.snap (typeSm.target); freqSm.snap (freqSm.target); qSm.snap (qSm.target); }
        // ---- PRE filter
        if (preOn) filterRun (L, R, n, 0, preSm); else preSm.snap (preSm.target);
        float inPk = 0; for (int i = 0; i < n; ++i) inPk = std::max (inPk, std::max (std::abs (L[i]), std::abs (R[i])));
        // ---- the curve, 4x oversampled + ADAA
        driveSm.target = juce::jlimit (0.0f, 36.0f, drive());
        float* chans[2] { L, R };
        juce::dsp::AudioBlock<float> blk (chans, 2, (size_t) n);
        auto up = os.processSamplesUp (blk);
        float* u0 = up.getChannelPointer (0); float* u1 = up.getChannelPointer (1);
        const int un = (int) up.getNumSamples();
        for (int i = 0; i < un; ++i)
        {
            const float dDb = driveSm.next();
            float w = 0.0f; if (fade > 0) { w = (float) fade / (float) fadeLen; --fade; w = 0.5f - 0.5f * std::cos (kPi * w); }
            const double g = modeGain (curMode, dDb), gp = modeGain (prevMode, dDb);
            const bool hNew = holdTick (dDb);
            for (int c = 0; c < 2; ++c)
            {
                float* u = c == 0 ? u0 : u1;
                const double xin = (double) u[i];
                double y = shape (curMode, xin * g, x1[c] * g, dDb, c, hNew) * (double) compOf (curMode, dDb);
                if (w > 0.0f) y = (1.0 - w) * y + w * shape (prevMode, xin * gp, x1[c] * gp, dDb, c, hNew) * (double) compOf (prevMode, dDb);
                x1[c] = xin;
                u[i] = (float) y;
            }
        }
        os.processSamplesDown (blk);
        // ---- DC, POST filter, level
        for (int i = 0; i < n; ++i) { L[i] = dc[0].process (L[i]); R[i] = dc[1].process (R[i]); }
        if (postOn) filterRun (L, R, n, 1, postSm); else postSm.snap (postSm.target);
        float outPk = 0;
        for (int i = 0; i < n; ++i) { const float lv = levelSm.next(); L[i] *= lv; R[i] *= lv; outPk = std::max (outPk, std::max (std::abs (L[i]), std::abs (R[i]))); }
        if (tm != nullptr) { tm->distIn.store (inPk, std::memory_order_relaxed); tm->distOut.store (outPk, std::memory_order_relaxed); tm->distDrive.store (driveSm.cur, std::memory_order_relaxed); }
    }
    // (for the editor and the tests) the static curve the current mode draws at drive dB, x in -1 .. 1 (before auto gain)
    static double curveAt (int m, double x, float driveDb, const DistTables* t)
    {
        if (m == dmXShaper && t != nullptr) { const double mm = juce::jlimit (0.0, 1.0, driveDb / 36.0); return (1.0 - mm) * DistTables::f (t->TA, x) + mm * DistTables::f (t->TB, x); }
        if (m == dmDownsample) return x;
        return distshape::f (m, x * modeGain (m, driveDb));
    }
    const DistTables& tablesNow() const { return tab; }
private:
public:
    // the input gain a mode applies at DRIVE dB. LIN FOLD runs at half the drive in dB (0 .. 18 dB = up to 8 folds): its straight
    // folds have a slowly falling (1 / n^2) spectrum, and beyond 8 folds at 0 dBFS the aliases would pass -70 dB even at 4x + ADAA
    static double modeGain (int m, float dDb) noexcept
    {
        if (m == dmXShaper || m == dmDownsample) return 1.0;
        return (double) dbToGain (m == dmLinFold ? 0.5f * dDb : dDb);
    }
private:
    float cutHz (int nt) const { return juce::jlimit (20.0f, 20000.0f, freq() * std::exp2 (clampf (fkey(), 0.0f, 1.0f) * (float) (nt - 60) / 12.0f)); }
    float compOf (int m, float dDb) const noexcept
    {
        if (m == dmXShaper)
        {
            const float pos = clampf (dDb / 36.0f, 0.0f, 1.0f) * 16.0f; const int k = std::min (15, (int) pos); const float fr = pos - (float) k;
            return dbToGain (tab.comp[(size_t) k] + fr * (tab.comp[(size_t) k + 1] - tab.comp[(size_t) k]));
        }
        const auto& t = distCompTable()[(size_t) m];
        const float pos = clampf (dDb / 1.5f, 0.0f, 24.0f); const int k = std::min (23, (int) pos); const float fr = pos - (float) k;
        return dbToGain (t[(size_t) k] + fr * (t[(size_t) k + 1] - t[(size_t) k]));
    }
    // DOWNSAMPLE: the hold length grows with DRIVE (1 .. 48 base samples); true on the samples where a new value is taken
    bool holdTick (float dDb) noexcept
    {
        const double d01 = juce::jlimit (0.0, 1.0, (double) dDb / 36.0);
        const double len = 1.0 + 47.0 * d01 * d01;
        holdPh += 1.0 / (4.0 * len);
        if (holdPh >= 1.0) { holdPh -= std::floor (holdPh); return true; }
        return false;
    }
    double shape (int m, double x, double xp, float dDb, int c, bool hNew) noexcept
    {
        if (m == dmDownsample) { if (hNew) hold[c] = (float) x; return hold[c]; }
        const double dx = x - xp;
        if (m == dmXShaper)
        {
            const double mm = juce::jlimit (0.0, 1.0, (double) dDb / 36.0);
            if (std::abs (dx) < 1.0e-7) { const double xm = 0.5 * (x + xp); return (1.0 - mm) * DistTables::f (tab.TA, xm) + mm * DistTables::f (tab.TB, xm); }
            const double Fa = DistTables::F (tab.TA, tab.CA, x) - DistTables::F (tab.TA, tab.CA, xp), Fb = DistTables::F (tab.TB, tab.CB, x) - DistTables::F (tab.TB, tab.CB, xp);
            return ((1.0 - mm) * Fa + mm * Fb) / dx;
        }
        if (std::abs (dx) < 1.0e-7) return distshape::f (m, 0.5 * (x + xp));
        return (distshape::F (m, x) - distshape::F (m, xp)) / dx;
    }
    void filterRun (float* L, float* R, int n, int which, ExpSmooth& amt)
    {
        auto& f0 = svf[(size_t) which][0]; auto& f1 = svf[(size_t) which][1];
        for (int i = 0; i < n; ++i)
        {
            const float a = amt.next(), t = tArr[(size_t) i];
            if ((i & 3) == 0) { f0.set (sr, fArr[(size_t) i], qArr[(size_t) i]); f1.g = f0.g; f1.k = f0.k; f1.a1 = f0.a1; f1.a2 = f0.a2; f1.a3 = f0.a3; }
            const float wl = t < 0.5f ? 1.0f - 2.0f * t : 0.0f, wb = (t < 0.5f ? 2.0f * t : 2.0f - 2.0f * t) * f0.k, wh = t < 0.5f ? 0.0f : 2.0f * t - 1.0f;
            float lp, bp, hp;
            f0.tick (L[i], lp, bp, hp); L[i] += a * (wl * lp + wb * bp + wh * hp - L[i]);
            f1.tick (R[i], lp, bp, hp); R[i] += a * (wl * lp + wb * bp + wh * hp - R[i]);
        }
    }
    void syncCurves (bool force)
    {
        if (store == nullptr) return;
        const int v = store->version.load (std::memory_order_acquire);
        if (! force && v == curveVer) return;
        const juce::SpinLock::ScopedTryLockType tl (store->lock);
        if (! tl.isLocked()) return;                                   // the editor is writing: next block
        tab = store->tables; curveVer = v;
    }
    P mode, drive, level, fpos, ftype, freq, q, fkey;
    double sr = 48000; int lat = 0, note = 60;
    juce::dsp::Oversampling<float> os { 2 };
    SwitchDelay dry; DcBlock dc[2];
    std::array<std::array<Svf, 2>, 2> svf {};
    ExpSmooth driveSm, levelSm, preSm, postSm, typeSm, freqSm, qSm;
    double x1[2] {}, holdPh = 1.0; float hold[2] {};
    std::array<float, kSub> tArr {}, fArr {}, qArr {};
    int curMode = 0, prevMode = 0, fade = 0, fadeLen = 1, curveVer = -1;
    DistTables tab;
};

inline void addDistParameters (juce::AudioProcessorValueTreeState::ParameterLayout& l)
{
    auto T = [] (const char* s) { return juce::String ("FX ") + unitInfo (uDist).title + " " + s; };
    addChoice (l, pid (uDist, "mode"), T ("Mode"), distModeNames(), 0);
    addFloat (l, pid (uDist, "drive"), T ("Drive"), juce::NormalisableRange<float> (0.0f, 36.0f), 12.0f, "dB", 1);
    addFloat (l, pid (uDist, "level"), T ("Level"), juce::NormalisableRange<float> (-24.0f, 12.0f), 0.0f, "dB", 1);
    addChoice (l, pid (uDist, "fpos"), T ("Filter"), { "OFF", "PRE", "POST" }, 0);
    addPct (l, pid (uDist, "ftype"), T ("Filter Type"), 0.0f);
    addFloat (l, pid (uDist, "freq"), T ("Filter Freq"), skewRange (20.0f, 20000.0f, 1000.0f), 1000.0f, "Hz", 0);
    addFloat (l, pid (uDist, "q"), T ("Filter Q"), skewRange (0.5f, 12.0f, 1.5f), 0.707f, "", 2);
    addPct (l, pid (uDist, "fkey"), T ("Filter Key Track"), 0.0f);
}
} // namespace gpfx
#endif // ! GPFX_BOX
