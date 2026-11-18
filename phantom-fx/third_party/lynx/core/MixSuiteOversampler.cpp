// SPDX-License-Identifier: AGPL-3.0-or-later
// PHANTOM FX EXCERPT (not a whole upstream file): the Oversampler of LYNX SHARE's core/MixSuite.cpp (Ka1-S/LYNX-SHARE @ d141370,
// lines 144-176 and 179-282, verbatim, with the file's own pi constant from line 40).
#include "core/MixSuite.h"
#include <algorithm>
#include <cmath>

namespace lynx::mixsuite
{
namespace
{
constexpr double pi = 3.141592653589793;

// ---- oversampler design ----
constexpr int stageTaps[Oversampler::maxStages] = { 95, 23, 15 }; // N = 4k + 3: the centre index c = (N − 1) / 2 is odd
constexpr double stageBeta[Oversampler::maxStages] = { 10.0, 9.0, 8.5 };

double besselI0 (double x)
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 50; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < 1e-12 * sum)
            break;
    }
    return sum;
}

// latency of a factor's path in units of the highest rate, before the pad
int rawUnits (int stages)
{
    const int f = 1 << stages;
    int u = 0;
    for (int s = 0; s < stages; ++s)
        u += ((stageTaps[s] - 1) / 2) * f / (1 << s);
    return u;
}

int pathLatency (int stages)
{
    const int f = 1 << stages;
    const int u = rawUnits (stages);
    return (u + (f - u % f) % f) / f;
}
} // namespace

// ================================================================== Oversampler

Oversampler::Oversampler()
{
    for (int s = 0; s < maxStages; ++s)
    {
        auto& stage = st[(std::size_t) s];
        const int n = stageTaps[s];
        const int c = (n - 1) / 2;
        const auto i0b = besselI0 (stageBeta[s]);
        double sum = 0.0;
        std::array<double, 64> h {};

        for (int j = 0; 2 * j < n; ++j)
        {
            const int t = 2 * j - c; // odd offset from the centre
            const auto r = (double) (2 * j - c) / c;
            const auto w = besselI0 (stageBeta[s] * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0b;
            h[(std::size_t) j] = std::sin (0.5 * pi * t) / (0.5 * pi * t) * w; // sinc(t/2), scaled below
            sum += h[(std::size_t) j];
        }

        stage.taps = (n + 1) / 2;
        for (int j = 0; j < stage.taps; ++j)
            stage.coef[(std::size_t) j] = (float) (h[(std::size_t) j] * 0.5 / sum); // the side taps sum to 0.5
        stage.centreDelay = (c - 1) / 2;
    }
    setFactor (4);
}

int Oversampler::latency() noexcept { return pathLatency (maxStages); }

void Oversampler::setFactor (int factor) noexcept
{
    const int s = factor >= 8 ? 3 : (factor >= 4 ? 2 : 1);
    if (s == stages && (hiPadLen != 0 || loPadLen != 0 || s == maxStages))
        return;
    stages = s;
    const int f = 1 << s;
    const int u = rawUnits (s);
    hiPadLen = (f - u % f) % f;
    loPadLen = latency() - pathLatency (s);
    reset();
}

void Oversampler::reset() noexcept
{
    for (auto& s : st)
    {
        s.upHist.fill (0.0f);
        s.evenHist.fill (0.0f);
        s.oddHist.fill (0.0f);
        s.upPos = s.downPos = 0;
    }
    hiPad.fill (0.0f);
    loPad.fill (0.0f);
    hiPadPos = loPadPos = 0;
}

void Oversampler::up (Stage& s, float x, float& a, float& b) noexcept
{
    const int L = s.taps;
    s.upPos = s.upPos == 0 ? L - 1 : s.upPos - 1;
    s.upHist[(std::size_t) s.upPos] = s.upHist[(std::size_t) (s.upPos + L)] = x;
    const float* hist = s.upHist.data() + s.upPos; // hist[j] = x[m − j]
    float acc = 0.0f;
    for (int j = 0; j < L; ++j)
        acc += s.coef[(std::size_t) j] * hist[j];
    a = 2.0f * acc;              // y[2m]
    b = hist[s.centreDelay];     // y[2m + 1] = x[m − (c − 1) / 2]
}

float Oversampler::down (Stage& s, float a, float b) noexcept
{
    const int L = s.taps;
    s.downPos = s.downPos == 0 ? L - 1 : s.downPos - 1;
    s.evenHist[(std::size_t) s.downPos] = s.evenHist[(std::size_t) (s.downPos + L)] = a;
    s.oddHist[(std::size_t) s.downPos] = s.oddHist[(std::size_t) (s.downPos + L)] = b;
    const float* even = s.evenHist.data() + s.downPos;
    float acc = 0.0f;
    for (int j = 0; j < L; ++j)
        acc += s.coef[(std::size_t) j] * even[j];
    return acc + 0.5f * s.oddHist[(std::size_t) (s.downPos + s.centreDelay + 1)]; // v[2m − c]
}

float Oversampler::padIn (float x) noexcept
{
    if (hiPadLen == 0)
        return x;
    const auto y = hiPad[(std::size_t) hiPadPos];
    hiPad[(std::size_t) hiPadPos] = x;
    hiPadPos = (hiPadPos + 1) % hiPadLen;
    return y;
}

float Oversampler::padOut (float x) noexcept
{
    if (loPadLen == 0)
        return x;
    const auto y = loPad[(std::size_t) loPadPos];
    loPad[(std::size_t) loPadPos] = x;
    loPadPos = (loPadPos + 1) % loPadLen;
    return y;
}
} // namespace lynx::mixsuite
