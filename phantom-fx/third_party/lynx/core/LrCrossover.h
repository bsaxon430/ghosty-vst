#pragma once

// Linkwitz-Riley crossovers (S. Linkwitz, JAES 24(1), 1976): low = (Butterworth low-pass)², high = (Butterworth
// high-pass)² at the same frequency, so |low + high| = 1 (an all-pass). 12 dB/oct = LR2 (one section of Q 0.5 per
// side, its high side polarity-flipped), 24 = LR4 (two of Q 0.7071), 48 = LR8 (four: the 4th-order Butterworth twice). Used by Prism Multiband and
// Ember's multiband mode (REA_rodada108). Stereo, no allocation.
// Round 150 (Pro-MB's slopes, measured: REA_rodada150 §1): 6 dB/oct = the first-order pair (low + high = 1 exactly) and
// 36 dB/oct = LR6 (the 3rd-order Butterworth twice: a Q 1 section and a first-order one per Butterworth, the high side
// polarity-flipped like LR2). 12 / 24 / 48 are unchanged.

#include <algorithm>
#include <array>
#include <complex>

#include "core/Biquad.h"

namespace lynx
{
struct LrSplit
{
    static constexpr int maxSections = 4;
    std::array<Biquad, maxSections> lp, hp;
    int sections = 2;
    bool flip = false; // LR2 / LR6: the high side's polarity is flipped so low + high is the all-pass

    void design (double sr, double hz, int slopeDbOct) noexcept
    {
        hz = std::min (hz, sr * 0.45);
        static constexpr double q8[] { 0.54119610, 1.30656296, 0.54119610, 1.30656296 };
        flip = false;
        if (slopeDbOct <= 6) // round 150: the first-order pair
        {
            sections = 1;
            const auto K = std::tan (3.14159265358979323846 * hz / sr);
            const auto a1 = (K - 1.0) / (K + 1.0);
            lp[0].b0 = lp[0].b1 = K / (1.0 + K); lp[0].b2 = 0.0; lp[0].a1 = a1; lp[0].a2 = 0.0;
            hp[0].b0 = 1.0 / (1.0 + K); hp[0].b1 = -1.0 / (1.0 + K); hp[0].b2 = 0.0; hp[0].a1 = a1; hp[0].a2 = 0.0;
            return;
        }
        if (slopeDbOct > 24 && slopeDbOct <= 36) // round 150: LR6 = (Q 1 section + first-order section)²
        {
            sections = 4;
            flip = true;
            const auto K = std::tan (3.14159265358979323846 * hz / sr);
            const auto a1 = (K - 1.0) / (K + 1.0);
            for (int k = 0; k < 4; k += 2)
            {
                lp[(std::size_t) k].filter (sr, 0, hz, 1.0);
                hp[(std::size_t) k].filter (sr, 1, hz, 1.0);
                auto& l1 = lp[(std::size_t) (k + 1)];
                auto& h1 = hp[(std::size_t) (k + 1)];
                l1.b0 = l1.b1 = K / (1.0 + K); l1.b2 = 0.0; l1.a1 = a1; l1.a2 = 0.0;
                h1.b0 = 1.0 / (1.0 + K); h1.b1 = -1.0 / (1.0 + K); h1.b2 = 0.0; h1.a1 = a1; h1.a2 = 0.0;
            }
            return;
        }
        sections = slopeDbOct <= 12 ? 1 : (slopeDbOct <= 24 ? 2 : 4);
        flip = sections == 1;
        for (int k = 0; k < sections; ++k)
        {
            const auto q = sections == 1 ? 0.5 : (sections == 2 ? 0.70710678 : q8[k]);
            lp[(std::size_t) k].filter (sr, 0, hz, q);
            hp[(std::size_t) k].filter (sr, 1, hz, q);
        }
    }

    void reset() noexcept
    {
        for (auto& b : lp)
            b.reset();
        for (auto& b : hp)
            b.reset();
    }

    void run (float x, int channel, float& lo, float& hi) noexcept
    {
        lo = x;
        hi = x;
        for (int k = 0; k < sections; ++k)
        {
            lo = lp[(std::size_t) k].process (lo, channel);
            hi = hp[(std::size_t) k].process (hi, channel);
        }
        if (flip) // LR2's (LR6's) high side has the opposite polarity: flip it so low + high is the all-pass
            hi = -hi;
    }

    // the split's all-pass (low + high), for keeping the other bands in phase
    float allpass (float x, int channel) noexcept
    {
        float lo, hi;
        run (x, channel, lo, hi);
        return lo + hi;
    }

    std::complex<double> response (bool high, double w) const noexcept
    {
        std::complex<double> h { 1.0, 0.0 };
        const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
        const auto& s = high ? hp : lp;
        for (int k = 0; k < sections; ++k)
        {
            const auto& q = s[(std::size_t) k];
            h *= (q.b0 + q.b1 * z1 + q.b2 * z2) / (1.0 + q.a1 * z1 + q.a2 * z2);
        }
        return high && flip ? -h : h;
    }
};
} // namespace lynx
