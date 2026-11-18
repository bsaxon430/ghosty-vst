// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX shared DSP: thin stereo wrappers around the Surge Synth Team's header-only libraries (GPL-3.0, see THIRD_PARTY.md):
// sst-filters (the real Surge filters: ladder, OB-Xd, K35, diode, comb, tri-pole, vintage, cutoff / resonance warp, SVF ...)
// and sst-waveshapers (its 40+ saturators, folders, rectifiers and fuzzes). The libraries are used as they are; only the
// per-channel plumbing is ours.
#pragma once
#include <juce_dsp/juce_dsp.h>
#include "sst/basic-blocks/simd/setup.h"
#include "sst/filters.h"
#include "sst/filters/CytomicSVF.h"
#include "sst/waveshapers.h"
#include <array>
#include <vector>

namespace phantom
{
// ---------------------------------------------------------------- one stereo Surge filter (lanes 0 and 1 of the quad unit)
class SstFilter
{
public:
    static constexpr int kBlock = 32;   // coefficient update interval (the maker interpolates between updates)
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        maker.setSampleRateAndBlockSize ((float) sampleRate, kBlock);
        const int dbLen = sst::filters::utilities::MAX_FB_COMB + sst::filters::utilities::SincTable::FIRipol_N;
        for (auto& d : delayBuf) d.assign ((size_t) dbLen, 0.0f);
        reset();
    }
    void reset()
    {
        for (int i = 0; i < 4; ++i) { std::fill (delayBuf[(size_t) i].begin(), delayBuf[(size_t) i].end(), 0.0f); state.DB[i] = delayBuf[(size_t) i].data(); state.active[i] = (int) 0xffffffff; state.WP[i] = 0; }
        for (int i = 0; i < sst::filters::n_filter_registers; ++i) state.R[i] = SIMD_MM(setzero_ps)();
        maker.Reset(); counter = 0; first = true;
    }
    void setType (sst::filters::FilterType t, sst::filters::FilterSubType st) { if (t != type || st != sub) { type = t; sub = st; fn = sst::filters::GetQFPtrFilterUnit (t, st); reset(); } }
    // cutoff in Hz, resonance 0..1 (Surge's own scale); call once per block before processing
    void set (float cutoffHz, float reso) noexcept
    {
        const float pitch = 12.0f * std::log2 (juce::jlimit (8.0f, 22000.0f, cutoffHz) / 440.0f);
        maker.MakeCoeffs (pitch, juce::jlimit (0.0f, 1.0f, reso), type, sub, nullptr, false);
        if (first) { maker.updateState (state); first = false; } else maker.updateState (state);
    }
    inline void process (float& l, float& r) noexcept
    {
        if (fn == nullptr) return;
        auto y = fn (&state, SIMD_MM(set_ps) (0.0f, 0.0f, r, l));
        float o alignas (16)[4]; SIMD_MM(store_ps) (o, y); l = o[0]; r = o[1];
        if (++counter >= kBlock) { counter = 0; maker.updateCoefficients (state); }
    }
    sst::filters::FilterType type = sst::filters::fut_none; sst::filters::FilterSubType sub = sst::filters::st_Standard;
private:
    sst::filters::FilterCoefficientMaker<> maker; sst::filters::QuadFilterUnitState state {}; sst::filters::FilterUnitQFPtr fn = nullptr;
    std::array<std::vector<float>, 4> delayBuf; double sr = 48000.0; int counter = 0; bool first = true;
};

// ---------------------------------------------------------------- one stereo Surge waveshaper
class SstShaper
{
public:
    void setType (sst::waveshapers::WaveshaperType t)
    {
        if (t == type && fn != nullptr) return;
        type = t; fn = sst::waveshapers::GetQuadWaveshaper (t);
        float R[sst::waveshapers::n_waveshaper_registers]; sst::waveshapers::initializeWaveshaperRegister (t, R);
        for (int i = 0; i < sst::waveshapers::n_waveshaper_registers; ++i) state.R[i] = SIMD_MM(set1_ps) (R[i]);
        state.init = SIMD_MM(cmpneq_ps) (SIMD_MM(setzero_ps)(), SIMD_MM(setzero_ps)());
    }
    void reset() { auto t = type; type = sst::waveshapers::WaveshaperType::wst_none; fn = nullptr; setType (t); }
    // drive is a linear gain applied inside the shaper (Surge convention)
    inline void process (float& l, float& r, float drive) noexcept
    {
        if (fn == nullptr || type == sst::waveshapers::WaveshaperType::wst_none) return;
        auto y = fn (&state, SIMD_MM(set_ps) (0.0f, 0.0f, r, l), SIMD_MM(set1_ps) (drive));
        float o alignas (16)[4]; SIMD_MM(store_ps) (o, y); l = o[0]; r = o[1];
    }
    sst::waveshapers::WaveshaperType type = sst::waveshapers::WaveshaperType::wst_none;
private:
    sst::waveshapers::QuadWaveshaperState state {}; sst::waveshapers::QuadWaveshaperPtr fn = nullptr;
};

// ---------------------------------------------------------------- stereo Cytomic SVF (clean, cheap; for formant banks and tone shaping)
struct Svf
{
    sst::filters::CytomicSVF f;
    void prepare (double sampleRate) { srInv = 1.0f / (float) sampleRate; f.init(); }
    void reset() { f.init(); }
    // gain = the bell's / shelf's linear gain at its peak or plateau (the Cytomic design takes its square root, A, as the coefficient)
    void set (sst::filters::CytomicSVF::Mode m, float hz, float res, float gain = 1.0f) noexcept { f.setCoeff (m, juce::jlimit (5.0f, 0.49f / srInv, hz), juce::jlimit (0.0f, 0.99f, res), srInv, std::sqrt (juce::jmax (1.0e-6f, gain))); }
    // the same with Q (0.5 .. 25: the Cytomic k = 1 / Q = 2 - 2 res)
    void setQ (sst::filters::CytomicSVF::Mode m, float hz, float q, float gain = 1.0f) noexcept { set (m, hz, 1.0f - 0.5f / juce::jlimit (0.5f, 25.0f, q), gain); }
    inline void process (float& l, float& r) noexcept { sst::filters::CytomicSVF::step (f, l, r); }
    float srInv = 1.0f / 48000.0f;
};

// one-pole DC blocker, stereo
struct DcBlock
{
    float xl = 0, yl = 0, xr = 0, yr = 0, R = 0.995f;
    void prepare (double sr) noexcept { R = 1.0f - (float) (2.0 * juce::MathConstants<double>::pi * 10.0 / sr); reset(); }
    void reset() noexcept { xl = yl = xr = yr = 0; }
    inline void process (float& l, float& r) noexcept { yl = l - xl + R * yl; xl = l; l = yl; yr = r - xr + R * yr; xr = r; r = yr; }
};
} // namespace phantom

// ---------------------------------------------------------------- the rack's saturation stage: chowdsp's 2nd-order ADAA engine (anti-aliased by
// construction). The plain curves use chowdsp's own analytic clippers; the character curves (the Surge saturators: zamsat, OJD, asym, and
// Surge's soft) are the sst-waveshapers functions themselves, run after a tanh limiter, sampled into chowdsp's ADAA tables together with
// their two antiderivatives (integrated numerically once per process). So every curve is the upstream curve, and every curve is ADAA.
// ADAA adds one sample of latency at the processing rate; the stage pads every path to exactly `latencySamples` so a module's latency is
// the same whatever curve is chosen. The tables live in one shared, read-only cache (deterministic data, no per-instance state).
#include <chowdsp_waveshapers/chowdsp_waveshapers.h>
#include <mutex>
#include <functional>
namespace phantom
{
// one Surge waveshaper evaluated on a scalar (table-building time only)
inline double sstCurve (sst::waveshapers::WaveshaperType t, double x)
{
    sst::waveshapers::QuadWaveshaperState st {}; float R[sst::waveshapers::n_waveshaper_registers]; sst::waveshapers::initializeWaveshaperRegister (t, R);
    for (int i = 0; i < sst::waveshapers::n_waveshaper_registers; ++i) st.R[i] = SIMD_MM(set1_ps) (R[i]);
    st.init = SIMD_MM(cmpneq_ps) (SIMD_MM(setzero_ps)(), SIMD_MM(setzero_ps)());
    auto y = sst::waveshapers::GetQuadWaveshaper (t) (&st, SIMD_MM(set1_ps) ((float) x), SIMD_MM(set1_ps) (1.0f));
    float o alignas (16)[4]; SIMD_MM(store_ps) (o, y); return (double) o[0];
}

// any memoryless curve as a chowdsp ADAA waveshaper: the curve and its first two antiderivatives go into chowdsp's lookup tables
class CurveADAA : public chowdsp::ADAAWaveshaper<float>
{
public:
    CurveADAA (chowdsp::LookupTableCache* cache, const std::string& id, const std::function<double (double)>& f, float range)
        : chowdsp::ADAAWaveshaper<float> (cache, id)
    {
        const int M = 1 << 18; const double lo = -range, hi = range, dx = (hi - lo) / (M - 1);
        // (chowdsp fills its tables on worker threads, so the integrals are shared-owned by the functions it is handed)
        auto F1 = std::make_shared<std::vector<double>> ((size_t) M), F2 = std::make_shared<std::vector<double>> ((size_t) M);
        double prev = f (lo), prevF1 = 0.0;   // (prevF1 starts at F1[0] after anchoring: set below) (*F1)[0] = 0.0; (*F2)[0] = 0.0;
        for (int i = 1; i < M; ++i) { const double fx = f (lo + i * dx); (*F1)[(size_t) i] = (*F1)[(size_t) i - 1] + 0.5 * (prev + fx) * dx; prev = fx; }
        // anchored at x = 0 (F1(0) = F2(0) = 0, as chowdsp's analytic antiderivatives are), which keeps the ADAA differences well conditioned
        const int mid = (M - 1) / 2; { const double c = (*F1)[(size_t) mid]; for (auto& v : *F1) v -= c; }
        prevF1 = (*F1)[0];
        for (int i = 1; i < M; ++i) { (*F2)[(size_t) i] = (*F2)[(size_t) i - 1] + 0.5 * (prevF1 + (*F1)[(size_t) i]) * dx; prevF1 = (*F1)[(size_t) i]; }
        { const double c = (*F2)[(size_t) mid]; for (auto& v : *F2) v -= c; }
        auto interp = [lo, dx, M] (const std::vector<double>& t, double x) { const double p = juce::jlimit (0.0, (double) (M - 1) - 1.0e-9, (x - lo) / dx); const int i = (int) p; const double fr = p - i; return t[(size_t) i] + (t[(size_t) i + 1] - t[(size_t) i]) * fr; };
        initialise (std::function<double (double)> (f), [F1, interp] (double x) { return interp (*F1, x); }, [F2, interp] (double x) { return interp (*F2, x); }, (float) lo, (float) hi, 1 << 17);
    }
};

class Shaper
{
public:
    enum class Kind { Tanh, Soft3, Soft5, Hard, Sine, Rect, HalfRect, Tape, Diode, Transistor, Console, None };
    void prepare (int channels)
    {
        ch = juce::jmax (1, channels);
        static std::mutex m; std::lock_guard<std::mutex> g (m);   // the shared cache is filled on the message thread, one instance at a time
        auto* c = &cache();
        tanhC = std::make_unique<chowdsp::ADAATanhClipper<float>> (c, kRange, kTable); tanhC->prepare (ch);
        soft3 = std::make_unique<chowdsp::ADAASoftClipper<float, 3>> (c, kRange, kTable); soft3->prepare (ch);
        soft5 = std::make_unique<chowdsp::ADAASoftClipper<float, 7>> (c, kRange, kTable); soft5->prepare (ch);
        hard = std::make_unique<chowdsp::ADAAHardClipper<float>> (c, kRange, kTable); hard->prepare (ch);
        sine = std::make_unique<chowdsp::ADAASineClipper<float>> (c, kRange, kTable); sine->prepare (ch);
        rect = std::make_unique<chowdsp::ADAAFullWaveRectifier<float>> (c, kRange, kTable); rect->prepare (ch);
        using W = sst::waveshapers::WaveshaperType;
        auto after = [] (W t) { return [t] (double x) { return sstCurve (t, std::tanh (x)); }; };
        tape = std::make_unique<CurveADAA> (c, "phantom_sst_zamsat", after (W::wst_zamsat), kRange); tape->prepare (ch);
        diode = std::make_unique<CurveADAA> (c, "phantom_sst_ojd", after (W::wst_ojd), kRange); diode->prepare (ch);
        trans = std::make_unique<CurveADAA> (c, "phantom_sst_asym", after (W::wst_asym), kRange); trans->prepare (ch);
        console = std::make_unique<CurveADAA> (c, "phantom_sst_soft", after (W::wst_soft), kRange); console->prepare (ch);
        reset();
    }
    void reset()
    {
        for (auto* s : all()) if (s) s->reset();
        padL = padR = 0.0f;
    }
    void setKind (Kind k) { kind = k; }
    // gain = drive into the stage (1 = none); bias = DC offset (asymmetry)
    inline void process (float& l, float& r, float gain, float bias) noexcept
    {
        // the ADAA tables span +-kRange: beyond it the shapes are flat, so the input is held there (no change to the output, the tables stay valid)
        const float xl = juce::jlimit (-kRange + 1.0f, kRange - 1.0f, l * gain + bias), xr = juce::jlimit (-kRange + 1.0f, kRange - 1.0f, r * gain + bias);
        if (kind == Kind::None) { l = padL; padL = xl; r = padR; padR = xr; return; }   // keep the one-sample pad so every curve has the same latency
        auto* w = pick();
        if (kind == Kind::HalfRect) { l = 0.5f * (xlPrev[0] + w->processSample (xl, 0)); r = 0.5f * (xlPrev[1] + w->processSample (xr, ch > 1 ? 1 : 0)); xlPrev[0] = xl; xlPrev[1] = xr; return; }   // half-wave = (x + |x|) / 2, x delayed to match
        l = w->processSample (xl, 0); r = w->processSample (xr, ch > 1 ? 1 : 0);
    }
    // the stage out of the way: only its one-sample pad, so switching drive to 0 keeps the latency and nulls against the dry signal
    inline void pass (float& l, float& r) noexcept { const float tl = padL, tr = padR; padL = l; padR = r; l = tl; r = tr; }
    static constexpr int latencySamples = 1;   // at the processing rate
    static constexpr int kTable = 1 << 17;   // table points (one shared copy per curve per process)
    static constexpr float kRange = 64.0f;   // input span of the tables (0 dBFS at the hardest drive, x61, stays inside)
    Kind kind = Kind::Tanh;
private:
    static chowdsp::LookupTableCache& cache() { static chowdsp::LookupTableCache c; return c; }
    chowdsp::ADAAWaveshaper<float>* pick() const noexcept
    {
        switch (kind)
        {
            case Kind::Soft3: return soft3.get(); case Kind::Soft5: return soft5.get(); case Kind::Hard: return hard.get(); case Kind::Sine: return sine.get();
            case Kind::Rect: case Kind::HalfRect: return rect.get(); case Kind::Tape: return tape.get(); case Kind::Diode: return diode.get();
            case Kind::Transistor: return trans.get(); case Kind::Console: return console.get(); default: return tanhC.get();
        }
    }
    std::array<chowdsp::ADAAWaveshaper<float>*, 10> all() { return { tanhC.get(), soft3.get(), soft5.get(), hard.get(), sine.get(), rect.get(), tape.get(), diode.get(), trans.get(), console.get() }; }
    int ch = 2; float padL = 0, padR = 0, xlPrev[2] { 0, 0 };
    std::unique_ptr<chowdsp::ADAATanhClipper<float>> tanhC; std::unique_ptr<chowdsp::ADAASoftClipper<float, 3>> soft3; std::unique_ptr<chowdsp::ADAASoftClipper<float, 7>> soft5;
    std::unique_ptr<chowdsp::ADAAHardClipper<float>> hard; std::unique_ptr<chowdsp::ADAASineClipper<float>> sine; std::unique_ptr<chowdsp::ADAAFullWaveRectifier<float>> rect;
    std::unique_ptr<CurveADAA> tape, diode, trans, console;
};
} // namespace phantom
