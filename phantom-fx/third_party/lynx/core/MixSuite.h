// SPDX-License-Identifier: AGPL-3.0-or-later
// PHANTOM FX EXCERPT (not a whole upstream file): the Oversampler class from LYNX SHARE's core/MixSuite.h (Ka1-S/LYNX-SHARE @ d141370,
// lines 34-94, verbatim), the only part of MixSuite that core/ApexLimiter.cpp uses. Its code: core/MixSuiteOversampler.cpp.
#pragma once
#include <array>

namespace lynx
{
namespace mixsuite
{
// ---- shared helpers (exposed for the tests and the editor graphs) ----

// Multi-stage half-band FIR oversampler for one channel (2x, 4x or 8x). The latency in base-rate samples is the
// same for every factor (the shorter paths are padded), so switching the quality never moves the PDC.
class Oversampler
{
public:
    static constexpr int maxStages = 3;
    Oversampler();
    void setFactor (int factor) noexcept; // 2, 4 or 8
    int factor() const noexcept { return 1 << stages; }
    void reset() noexcept;
    static int latency() noexcept;        // base-rate samples (the same for every factor)

    // Runs `f` (float → float) at the oversampled rate on each sample of `x` (in place).
    template <typename F>
    void process (float* x, int n, F&& f) noexcept
    {
        for (int i = 0; i < n; ++i)
            x[i] = padOut (run<0> (x[i], f));
    }

private:
    struct Stage
    {
        int taps = 0;                       // non-zero side taps of the half-band (centre excluded)
        std::array<float, 64> coef {};      // h[2j], j = 0 .. taps − 1
        int centreDelay = 0;                // (c − 1) / 2 and (c + 1) / 2 in input samples
        std::array<float, 128> upHist {}, evenHist {}, oddHist {};
        int upPos = 0, downPos = 0;
    };

    template <int level, typename F>
    float run (float x, F& f) noexcept
    {
        if constexpr (level == maxStages)
            return f (padIn (x));
        else
        {
            if (level == stages)
                return f (padIn (x));
            float a, b;
            up (st[(std::size_t) level], x, a, b);
            const auto ya = run<level + 1> (a, f);
            const auto yb = run<level + 1> (b, f);
            return down (st[(std::size_t) level], ya, yb);
        }
    }

    static void up (Stage& s, float x, float& a, float& b) noexcept;
    static float down (Stage& s, float a, float b) noexcept;
    float padIn (float x) noexcept;  // a short delay at the highest rate (makes the latency whole)
    float padOut (float x) noexcept; // base-rate delay up to the common latency

    std::array<Stage, maxStages> st;
    int stages = 2;
    std::array<float, 8> hiPad {};
    int hiPadLen = 0, hiPadPos = 0;
    std::array<float, 64> loPad {};
    int loPadLen = 0, loPadPos = 0;
};
} // namespace mixsuite
} // namespace lynx
