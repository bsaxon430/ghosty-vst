// Ghost Matter - AGPL-3.0-only (see LICENSE, NOTICE.md).
#include "Limiter.h"
#include <algorithm>
#include <cmath>

namespace matter
{
void TruePeakLimiter::prepare (double r, float ceilingDb)
{
    sr = r;
    ceiling = std::pow (10.0f, ceilingDb / 20.0f);
    look = std::max (16, (int) std::lround (0.0015 * sr));
    releaseCoef = std::exp (-1.0f / (0.12f * (float) sr));
    // the 8x interpolator: phase p (p = 0..7) gives the sample at (p / 8) between the input samples, from a windowed sinc
    const int half = kTaps / 2;
    for (int p = 0; p < kPhases; ++p)
    {
        float sum = 0;
        for (int k = 0; k < kTaps; ++k)
        {
            const double t = (double) (k - half + 1) - p / (double) kPhases;          // distance from the tap to the point
            const double x = 3.14159265358979 * t;
            const double sinc = std::abs (t) < 1e-9 ? 1.0 : std::sin (x) / x;
            const double w = 0.5 + 0.5 * std::cos (3.14159265358979 * t / (half + 0.5));   // Hann over the span
            phase[(size_t) p][(size_t) k] = (float) (sinc * w);
            sum += phase[(size_t) p][(size_t) k];
        }
        for (auto& c : phase[(size_t) p]) c /= sum;
    }
    delayLen = look + kTaps / 2 - 1;
    dL.assign ((size_t) delayLen, 0.0f); dR.assign ((size_t) delayLen, 0.0f);
    need.assign ((size_t) (look + kTaps / 2), 1.0f); avg.assign ((size_t) look, 1.0f);
    reset();
}

void TruePeakLimiter::reset()
{
    std::fill (dL.begin(), dL.end(), 0.0f); std::fill (dR.begin(), dR.end(), 0.0f);
    std::fill (need.begin(), need.end(), 1.0f); std::fill (avg.begin(), avg.end(), 1.0f);
    histL.fill (0); histR.fill (0); histPos = 0;
    pos = 0; dpos = 0; npos = 0; avgSum = look; held = 1.0f; lastGain = 1.0f;
}

float TruePeakLimiter::truePeak (std::array<float, kTaps>& h, float x) noexcept
{
    // h is a ring of the last kTaps inputs (written at histPos by the caller)
    float pk = std::abs (x);
    for (int p = 1; p < kPhases; ++p)
    {
        float y = 0;
        const auto& c = phase[(size_t) p];
        for (int k = 0; k < kTaps; ++k) y += c[(size_t) k] * h[(size_t) ((histPos + 1 + k) % kTaps)];
        pk = std::max (pk, std::abs (y));
    }
    return pk;
}

void TruePeakLimiter::process (float* L, float* R, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        const float xl = L[i], xr = R ? R[i] : xl;
        histL[(size_t) histPos] = xl; histR[(size_t) histPos] = xr;
        // the true peak around the sample half a filter back (the interpolators' centre), plus the sample itself
        const float pk = std::max ({ truePeak (histL, xl), truePeak (histR, xr), std::abs (xl), std::abs (xr) });
        histPos = (histPos + 1) % kTaps;
        const float g = pk > ceiling ? ceiling / pk : 1.0f;
        // min-hold over the look-ahead window
        need[(size_t) npos] = g;
        npos = (npos + 1) % (int) need.size();
        float m = 1.0f;
        for (float v : need) m = std::min (m, v);
        // release: rise slowly, fall at once
        held = m < held ? m : m + (held - m) * releaseCoef;
        // box average over the window
        avgSum += held - avg[(size_t) pos];
        avg[(size_t) pos] = held;
        const float gain = std::min (1.0f, (float) (avgSum / look));
        // the delayed sample gets the gain; the interpolator's own lag (half its taps) is inside the window
        const float ol = dL[(size_t) dpos], orr = dR[(size_t) dpos];
        dL[(size_t) dpos] = xl; dR[(size_t) dpos] = xr;
        dpos = (dpos + 1) % delayLen;
        pos = (pos + 1) % look;
        L[i] = ol * gain;
        if (R) R[i] = orr * gain;
        lastGain = gain;
    }
}
}
