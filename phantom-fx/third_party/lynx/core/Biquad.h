#pragma once

// Biquad (classic formulas from R. Bristow-Johnson's "Audio EQ Cookbook"), in double,
// stereo (per-channel state). Used by the built-in effects and the mixer tracks' EQ.

#include <algorithm>
#include <cmath>

#if defined(__SSE__) || defined(_M_X64) || defined(__x86_64__) || defined(__aarch64__) || defined(_M_ARM64) // x86 SSE2, or ARM64 through core/Simd.h (sse2neon)
 #include "core/Simd.h"
 #define LYNX_BIQUAD_SSE2 1
#endif

namespace lynx
{
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1[2] {}, z2[2] {};

    void reset() noexcept { z1[0] = z1[1] = z2[0] = z2[1] = 0.0; }

    float process (float x, int ch) noexcept
    {
        const auto y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return (float) y;
    }

    // both channels of a block, with state in locals (same result as per-sample process())
    void processBlock (float* l, float* r, int n) noexcept
    {
       #if LYNX_BIQUAD_SSE2
        // both channels side by side in a 2-double register (same math, same order)
        auto s1 = _mm_set_pd (z1[1], z1[0]), s2 = _mm_set_pd (z2[1], z2[0]);
        const auto c0 = _mm_set1_pd (b0), c1 = _mm_set1_pd (b1), c2 = _mm_set1_pd (b2), d1 = _mm_set1_pd (a1), d2 = _mm_set1_pd (a2);

        for (int i = 0; i < n; ++i)
        {
            const auto x = _mm_set_pd ((double) r[i], (double) l[i]);
            const auto y = _mm_add_pd (_mm_mul_pd (c0, x), s1);
            s1 = _mm_add_pd (_mm_sub_pd (_mm_mul_pd (c1, x), _mm_mul_pd (d1, y)), s2);
            s2 = _mm_sub_pd (_mm_mul_pd (c2, x), _mm_mul_pd (d2, y));
            double out[2];
            _mm_storeu_pd (out, y);
            l[i] = (float) out[0];
            r[i] = (float) out[1];
        }

        double a[2], b[2];
        _mm_storeu_pd (a, s1);
        _mm_storeu_pd (b, s2);
        z1[0] = a[0]; z1[1] = a[1]; z2[0] = b[0]; z2[1] = b[1];
       #else
        auto l1 = z1[0], l2 = z2[0], r1 = z1[1], r2 = z2[1];
        const auto c0 = b0, c1 = b1, c2 = b2, d1 = a1, d2 = a2;

        for (int i = 0; i < n; ++i)
        {
            const double xl = l[i], xr = r[i];
            const auto yl = c0 * xl + l1;
            const auto yr = c0 * xr + r1;
            l1 = c1 * xl - d1 * yl + l2;
            r1 = c1 * xr - d1 * yr + r2;
            l2 = c2 * xl - d2 * yl;
            r2 = c2 * xr - d2 * yr;
            l[i] = (float) yl;
            r[i] = (float) yr;
        }

        z1[0] = l1; z2[0] = l2; z1[1] = r1; z2[1] = r2;
       #endif
    }

    void set (double nb0, double nb1, double nb2, double a0, double na1, double na2) noexcept
    {
        b0 = nb0 / a0; b1 = nb1 / a0; b2 = nb2 / a0; a1 = na1 / a0; a2 = na2 / a0;
    }

    // low/high/band-pass (cookbook); the band-pass has 0 dB gain at the peak
    void filter (double sr, int type, double hz, double q) noexcept
    {
        constexpr double pi = 3.141592653589793;
        const auto w = 2.0 * pi * std::min (hz, sr * 0.45) / sr;
        const auto cw = std::cos (w);
        const auto alpha = std::sin (w) / (2.0 * q);

        if (type == 0)
            set ((1 - cw) / 2, 1 - cw, (1 - cw) / 2, 1 + alpha, -2 * cw, 1 - alpha);
        else if (type == 1)
            set ((1 + cw) / 2, -(1 + cw), (1 + cw) / 2, 1 + alpha, -2 * cw, 1 - alpha);
        else
            set (alpha, 0, -alpha, 1 + alpha, -2 * cw, 1 - alpha);
    }

    void peaking (double sr, double hz, double q, double db) noexcept
    {
        constexpr double pi = 3.141592653589793;
        const auto a = std::pow (10.0, db / 40.0);
        const auto w = 2.0 * pi * hz / sr;
        const auto alpha = std::sin (w) / (2.0 * q);
        set (1 + alpha * a, -2 * std::cos (w), 1 - alpha * a, 1 + alpha / a, -2 * std::cos (w), 1 - alpha / a);
    }

    // shelf; `slope` is the cookbook's S (1 = steepest without overshoot)
    void shelf (double sr, double hz, double db, bool high, double slope = 1.0) noexcept
    {
        constexpr double pi = 3.141592653589793;
        const auto a = std::pow (10.0, db / 40.0);
        const auto w = 2.0 * pi * hz / sr;
        const auto cw = std::cos (w);
        // (a slope steeper than the gain allows makes the root negative: the shelf's Q would be infinite and the
        // coefficients NaN. Capped at Q 10, round 111; the cookbook range S ≤ 1 never gets near it.)
        const auto alpha = std::sin (w) / 2.0 * std::sqrt (std::max ((a + 1.0 / a) * (1.0 / slope - 1.0) + 2.0, 0.01));
        const auto sq = 2.0 * std::sqrt (a) * alpha;

        if (high)
            set (a * ((a + 1) + (a - 1) * cw + sq), -2 * a * ((a - 1) + (a + 1) * cw), a * ((a + 1) + (a - 1) * cw - sq),
                 (a + 1) - (a - 1) * cw + sq, 2 * ((a - 1) - (a + 1) * cw), (a + 1) - (a - 1) * cw - sq);
        else
            set (a * ((a + 1) - (a - 1) * cw + sq), 2 * a * ((a - 1) - (a + 1) * cw), a * ((a + 1) - (a - 1) * cw - sq),
                 (a + 1) + (a - 1) * cw + sq, -2 * ((a - 1) + (a + 1) * cw), (a + 1) + (a - 1) * cw - sq);
    }
};
} // namespace lynx
