// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX: the CURVES engine of WRECKER, CRACKLE and TUBE GLOW (DECISIONS.md 85). It plays transfer-curve sets: each set is a few
// curves (input -1..+1 -> output) from mild to heavy, and DRIVE morphs through them (from 0 to 10 % it fades the first one in from the
// clean signal, so DRIVE 0 is the dry signal, delayed one sample like every curve). The sets in this build are the 52 waveshaper presets
// Brandon downloaded (assets/curves: PRIVATE, never in a public build; README there).
// Every curve is anti-aliased with chowdsp's second-order ADAA (the same method and arithmetic as chowdsp::ADAAWaveshaper, Direct mode,
// its ill-condition fallbacks and tolerance), evaluated here without per-curve state: the output of the two curves either side of DRIVE
// is formed from the shared input history and each curve's first and second antiderivative, so morphing and changing set never glitch.
// The antiderivatives are built (message thread) only for the set that is chosen, then kept.
#pragma once
#include "AltEngine.h"
#include "../dsp/SstWrap.h"
#include "../effects/ColourSst.h"
#include <array>
#include <atomic>
#include <mutex>

namespace phantom::engines
{
struct CurveSet
{
    std::string name; int lanes = 0, points = 0; std::vector<std::vector<float>> curve;   // lanes x points, x = -1 + 2 i / (points - 1)
};
// the library, parsed once from the embedded files
const std::vector<CurveSet>& curveLibrary();

// one set ready for ADAA: per curve f, F1, F2 on a grid over [-kSpan, kSpan] (beyond +-1 the curve holds its end value)
struct BuiltCurves
{
    static constexpr double kSpan = 1.25;
    int lanes = 0, n = 0; double lo = -kSpan, dx = 1.0, inv = 1.0;
    std::vector<double> f, F1, F2;   // lanes x n each
    // beyond the grid the curve is flat, so its integrals continue in closed form (F1 a line, F2 a parabola): a hot input (DRAW's push,
    // BIAS) is held by the curve itself, not clipped hard at the grid's edge
    inline double at (const std::vector<double>& t, int lane, double x) const noexcept
    {
        const double hi = lo + (n - 1) * dx;
        if (x > hi || x < lo)
        {
            const double e = x > hi ? hi : lo, u = x - e, fe = grid (f, lane, e); if (&t == &f) return fe;
            const double F1e = grid (F1, lane, e); if (&t == &F1) return F1e + fe * u; return grid (F2, lane, e) + F1e * u + 0.5 * fe * u * u;
        }
        return grid (t, lane, x);
    }
    inline double grid (const std::vector<double>& t, int lane, double x) const noexcept
    {
        const double p = juce::jlimit (0.0, (double) (n - 1) - 1.0e-9, (x - lo) * inv); const int i = (int) p; const double fr = p - i;
        const double* row = t.data() + (size_t) lane * (size_t) n; return row[i] + (row[i + 1] - row[i]) * fr;
    }
};
const BuiltCurves* builtCurves (int set);           // nullptr until built (audio thread: never blocks)
void buildCurves (int set);                          // message thread: builds once, then a no-op
std::unique_ptr<BuiltCurves> buildTables (const CurveSet& set);   // message thread: the ADAA tables of any set

class CurveEngine final : public AltEngine
{
public:
    // drawn = the DRAW engine: one curve the user draws on the page (DRIVE pushes the input into it, 0 .. +12 dB), saved with the project
    CurveEngine (int unit, const std::vector<ParamSpec>& specs, bool drawnCurve = false) : u (unit), drawn (drawnCurve)
    {
        for (auto& s : specs) defs[s.id] = s.def;
        if (drawn) setData ({});
    }
    ~CurveEngine() override { delete drawnTables.load(); }
    // ---- DRAW: the points (x, y in -1..1; x ascending, the ends at x = -1 and +1), as the editor sends them
    static std::vector<juce::Point<float>> factoryPoints()
    {   // a soft saturation to start from
        std::vector<juce::Point<float>> p; for (float x : { -1.0f, -0.6f, -0.25f, 0.0f, 0.25f, 0.6f, 1.0f }) p.push_back ({ x, (float) (std::tanh (1.8 * x) / std::tanh (1.8)) }); return p;
    }
    static juce::var pointsToVar (const std::vector<juce::Point<float>>& pts)
    {   // stored as whole numbers (x 10 000) so the project's text round trip is exact
        juce::Array<juce::var> a; for (auto& q : pts) { juce::Array<juce::var> xy; xy.add ((int) std::lround (q.x * 10000.0f)); xy.add ((int) std::lround (q.y * 10000.0f)); a.add (juce::var (xy)); }
        auto* o = new juce::DynamicObject(); o->setProperty ("points", a); return juce::var (o);
    }
    static std::vector<juce::Point<float>> varToPoints (const juce::var& v)
    {
        std::vector<juce::Point<float>> p;
        if (auto* a = v["points"].getArray()) for (auto& e : *a) if (auto* xy = e.getArray(); xy != nullptr && xy->size() >= 2)
            p.push_back ({ juce::jlimit (-1.0f, 1.0f, (float) (int) (*xy)[0] / 10000.0f), juce::jlimit (-1.5f, 1.5f, (float) (int) (*xy)[1] / 10000.0f) });
        std::sort (p.begin(), p.end(), [] (auto& a, auto& b) { return a.x < b.x; });
        if (p.size() < 2 || p.front().x > -1.0f + 1.0e-4f || p.back().x < 1.0f - 1.0e-4f) return factoryPoints();
        return p;
    }
    // the curve through the points (Catmull-Rom, so it passes through every point), sampled for the tables
    static float evalCurve (const std::vector<juce::Point<float>>& p, float x)
    {
        if (p.size() < 2) return x; if (x <= p.front().x) return p.front().y; if (x >= p.back().x) return p.back().y;
        size_t k = 0; while (k + 2 < p.size() && x > p[k + 1].x) ++k;
        const auto& a = p[k]; const auto& b = p[k + 1]; const float h = juce::jmax (1.0e-6f, b.x - a.x), t = (x - a.x) / h;
        // the two ends level off (slope 0): the curve is held at its end values beyond them, so the hand-over has no corner to alias
        const float ma = k > 0 ? (b.y - p[k - 1].y) / juce::jmax (1.0e-6f, b.x - p[k - 1].x) : 0.0f;
        const float mb = k + 2 < p.size() ? (p[k + 2].y - a.y) / juce::jmax (1.0e-6f, p[k + 2].x - a.x) : 0.0f;
        const float t2 = t * t, t3 = t2 * t;
        return (2 * t3 - 3 * t2 + 1) * a.y + (t3 - 2 * t2 + t) * h * ma + (-2 * t3 + 3 * t2) * b.y + (t3 - t2) * h * mb;
    }
    bool hasData() const override { return drawn; }
    juce::var getData() const override { return pointsToVar (points); }
    void setData (const juce::var& v) override
    {
        if (! drawn) return;
        points = v.isVoid() ? factoryPoints() : varToPoints (v);
        CurveSet cs; cs.name = "DRAW"; cs.lanes = 1; cs.points = 8193; cs.curve.assign (1, std::vector<float> (8193));
        for (int i = 0; i < cs.points; ++i) cs.curve[0][(size_t) i] = juce::jlimit (-1.5f, 1.5f, evalCurve (points, -1.0f + 2.0f * (float) i / (float) (cs.points - 1)));
        smooth (cs.curve[0]);
        auto* old = drawnTables.exchange (buildTables (cs).release(), std::memory_order_acq_rel);
        // (the audio thread may still be reading the old tables this block: they are freed a few edits later)
        retired[(size_t) (retiredAt++ % retired.size())].reset (old);
    }
    const std::vector<juce::Point<float>>& drawnPoints() const noexcept { return points; }
    // the spline's bends change abruptly at each point (its curvature jumps): a Gaussian (sigma 0.02 of the input range; the ends held)
    // rounds them off, so the drawn curve makes no more high harmonics than a smooth one (a step in curvature aliases at -70 dB)
    static void smooth (std::vector<float>& y)
    {
        const int n = (int) y.size(); const double sg = 0.02 * (n - 1) / 2.0; const int r = (int) std::ceil (3.0 * sg);
        std::vector<double> k ((size_t) (2 * r + 1)); double ks = 0; for (int j = -r; j <= r; ++j) ks += (k[(size_t) (j + r)] = std::exp (-0.5 * j * j / (sg * sg)));
        std::vector<float> o ((size_t) n);
        for (int i = 0; i < n; ++i) { double a = 0; for (int j = -r; j <= r; ++j) a += k[(size_t) (j + r)] * y[(size_t) juce::jlimit (0, n - 1, i + j)]; o[(size_t) i] = (float) (a / ks); }
        y.swap (o);
    }
    void prepare (double sampleRate, int) override
    {
        sr = sampleRate; dc.prepare (sampleRate); ag.prepare (sampleRate); for (auto& f : filt) f.prepare (sampleRate);
        dSm.prepare (sampleRate, 300.0f, 0.0f, false); bSm.prepare (sampleRate, 300.0f, 0.0f, false); for (auto& f : fSm) f.prepare (sampleRate, 40.0f, 0.0f, false);   // DRIVE / BIAS glide 300 ms: they move the curve itself oSm.prepare (sampleRate, 20.0f, 0.0f, false); reset();
    }
    void reset() noexcept override { dryL = dryR = 0.0f; for (auto& h : hist) h = {}; dc.reset(); ag.reset(); for (auto& f : filt) f.reset(); primed = false; fPrimed = false; }
    void update (const Controls&, int voice) override { if (! drawn) buildCurves (voice); }
    int latencySamples (int) const noexcept override { return Shaper::latencySamples; }
    bool appliesLevel() const noexcept override { return true; }
    void setLevel (float g) noexcept override { level = g; }   // ADAA's one sample, as on the PHANTOM engine
    void process (float* L, float* R, int n, const Controls& c, int voice, const TempoInfo&) noexcept override
    {
        const BuiltCurves* bc = drawn ? drawnTables.load (std::memory_order_acquire) : builtCurves (voice);
        // the effect's own controls: DRIVE (CRACKLE: band 1's), BIAS, the cuts / tone, AUTO GAIN and OUTPUT where it has them
        const float drive = c.get ("drive", c.get ("b1_drive", 0.3f)), bias = c.has ("bias") ? c.get ("bias", 0.0f) * 0.5f : 0.0f;
        const float outDb = c.get ("out", 0.0f); const bool autoGain = ! c.has ("auto") || c.get ("auto", 1.0f) >= 0.5f;
        if (! primed) { dSm.snap (drive); bSm.snap (bias); oSm.snap (outDb); primed = true; }
        dSm.setTarget (drive); bSm.setTarget (bias); oSm.setTarget (outDb);
        float ft[6]; targets (c, ft); shelfTube = c.has ("bass");
        if (! fPrimed) { for (int k = 0; k < 6; ++k) { fSm[(size_t) k].snap (ft[k]); coeffs (k); } fPrimed = true; }
        for (int k = 0; k < 6; ++k) fSm[(size_t) k].setTarget (ft[k]);
        for (int i = 0; i < n; ++i)
        {
            float l = L[i], r = R[i];
            for (int k = 0; k < 6; ++k) { auto& sm = fSm[(size_t) k]; if (sm.isSmoothing()) { sm.next(); if ((i & 7) == 0) coeffs (k); } }
            cut (filt[0], l, r, lowAmt (fSm[0].current)); cut (filt[1], l, r, highAmt (fSm[1].current));
            const float d = dSm.next(), b = bSm.next(); const float og = juce::Decibels::decibelsToGain (oSm.next());
            ag.feedIn (l, r);
            const float dl = dryL, dr = dryR; dryL = l; dryR = r;   // the dry, one sample back (the curves' ADAA latency)
            float w = juce::jlimit (0.0f, 1.0f, d / 0.1f);   // DRIVE 0 .. 10 %: the curve fades in from the clean signal
            float yl = dl, yr = dr;
            if (bc != nullptr) { const float g = drawn ? juce::Decibels::decibelsToGain (juce::jlimit (0.0f, 1.0f, (d - 0.1f) / 0.9f) * 12.0f) : 1.0f; yl = shape (*bc, 0, l * g + b, d); yr = shape (*bc, 1, r * g + b, d); } else { push (0, l); push (1, r); w = 0.0f; }   // (a set still being built: the dry)
            yl = dl * (1.0f - w) + yl * w * level; yr = dr * (1.0f - w) + yr * w * level;
            if (w > 0.0f && std::abs (b) + d > 0.0005f) dc.process (yl, yr);
            if (autoGain && w > 0.0f) { const float g = ag.gain (yl, yr); yl *= g; yr *= g; }
            cut (filt[2], yl, yr, highAmt (fSm[2].current)); cut (filt[3], yl, yr, lowAmt (fSm[3].current));
            if (std::abs (fSm[4].current) > 0.001f) filt[4].process (yl, yr); if (std::abs (fSm[5].current) > 0.001f) filt[5].process (yl, yr);
            L[i] = yl * og; R[i] = yr * og;
        }
    }
private:
    struct Hist { double x1 = 0.0, x2 = 0.0; };
    inline void push (int ch, double x) noexcept { hist[(size_t) ch].x2 = hist[(size_t) ch].x1; hist[(size_t) ch].x1 = x; }
    // chowdsp::ADAAWaveshaper's second-order ADAA (Direct mode), for one curve, from the input history
    static double adaa (const BuiltCurves& b, int lane, double x, double x1, double x2) noexcept
    {
        constexpr double TOL = 1.0e-2;   // chowdsp's default: ScientificRatio<1, -2>
        auto d1 = [&] (double a, double c) { return std::abs (a - c) < TOL ? b.at (b.F1, lane, 0.5 * (a + c)) : (b.at (b.F2, lane, a) - b.at (b.F2, lane, c)) / (a - c); };
        if (std::abs (x - x2) < TOL)
        {   // fallback
            const double xBar = 0.5 * (x + x2), delta = xBar - x1;
            return std::abs (delta) < TOL ? b.at (b.f, lane, 0.5 * (xBar + x1)) : (2.0 / delta) * (b.at (b.F1, lane, xBar) + (b.at (b.F2, lane, x1) - b.at (b.F2, lane, xBar)) / delta);
        }
        return (2.0 / (x - x2)) * (d1 (x, x1) - d1 (x1, x2));
    }
    // the morph: DRIVE 10 .. 100 % walks through the set's curves (mild to heavy), two neighbours blended
    inline float shape (const BuiltCurves& b, int ch, float in, float d) noexcept
    {
        const double x = juce::jlimit (-64.0, 64.0, (double) in);
        auto& h = hist[(size_t) ch];
        const double pos = juce::jlimit (0.0, 1.0, (d - 0.1) / 0.9) * (b.lanes - 1); const int a = juce::jmin ((int) pos, b.lanes - 1), c = juce::jmin (a + 1, b.lanes - 1); const double fr = pos - a;
        double y = adaa (b, a, x, h.x1, h.x2); if (fr > 1.0e-6 && c != a) y += fr * (adaa (b, c, x, h.x1, h.x2) - y);
        push (ch, x); return (float) y;
    }
    // the cuts (before the curve: WRECKER's LOW CUT, CRACKLE's PRE cuts; after it: WRECKER's HIGH CUT, CRACKLE's POST cuts) and the tone
    // (WRECKER's TONE: +-6 dB tilt at 250 Hz / 3 kHz; TUBE GLOW's BASS / TREBLE: +-12 dB at 120 Hz / 6 kHz) glide per sample as WRECKER's
    // own do (coefficients recomputed every 8 samples while a knob moves); a cut fades out of the path at its end of the range
    void targets (const Controls& c, float* t) const noexcept
    {
        t[0] = c.get ("lowcut", c.get ("prehp", 10.0f)); t[1] = c.get ("prelp", 22000.0f); t[2] = c.get ("highcut", c.get ("postlp", 22000.0f)); t[3] = c.get ("posthp", 10.0f);
        float lowDb = 0.0f, highDb = 0.0f;
        if (c.has ("tone")) { const float tn = c.get ("tone", 0.0f); lowDb = -tn * 6.0f; highDb = tn * 6.0f; }
        if (c.has ("bass")) { lowDb = c.get ("bass", 0.0f); highDb = c.get ("treble", 0.0f); }
        t[4] = lowDb; t[5] = highDb;
    }
    void coeffs (int k) noexcept
    {
        using M = sst::filters::CytomicSVF::Mode; const bool tube = shelfTube;
        const float v = fSm[(size_t) k].current;
        switch (k)
        {
            case 0: filt[0].set (M::Highpass, v, 0.0f); break; case 1: filt[1].set (M::Lowpass, v, 0.0f); break;
            case 2: filt[2].set (M::Lowpass, v, 0.0f); break; case 3: filt[3].set (M::Highpass, v, 0.0f); break;
            case 4: filt[4].set (M::LowShelf, tube ? 120.0f : 250.0f, tube ? 0.0f : 0.3f, juce::Decibels::decibelsToGain (v)); break;
            default: filt[5].set (M::HighShelf, tube ? 6000.0f : 3000.0f, tube ? 0.0f : 0.3f, juce::Decibels::decibelsToGain (v)); break;
        }
    }
    static float lowAmt (float hz) noexcept { return juce::jlimit (0.0f, 1.0f, (hz - 10.0f) * 0.5f); }
    static float highAmt (float hz) noexcept { return juce::jlimit (0.0f, 1.0f, (22000.0f - hz) * 0.002f); }
    static void cut (Svf& f, float& l, float& r, float amt) noexcept { if (amt <= 0.0f) return; float fl = l, fr = r; f.process (fl, fr); l += (fl - l) * amt; r += (fr - r) * amt; }
    int u; bool drawn = false; std::map<std::string, float> defs; double sr = 48000.0; bool primed = false;
    std::vector<juce::Point<float>> points; std::atomic<const BuiltCurves*> drawnTables { nullptr }; std::array<std::unique_ptr<const BuiltCurves>, 8> retired; int retiredAt = 0;
    std::array<Hist, 2> hist {}; float dryL = 0.0f, dryR = 0.0f, level = 1.0f; DcBlock dc; AutoGain ag; std::array<Svf, 6> filt; std::array<ParamSmoother, 6> fSm; bool fPrimed = false, shelfTube = false;
    ParamSmoother dSm, bSm, oSm;
};
} // namespace phantom::engines
