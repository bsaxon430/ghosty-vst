/*
 * SEANCE (GhostBlock) - the last thing on the output: a level meter and a safety limiter.
 *
 * The limiter only acts when the sound would go over the ceiling (just under full scale): it turns the whole signal
 * down for that moment (instantly, both channels together) and lets it back up over about a tenth of a second. Below
 * the ceiling the sound passes through bit-for-bit. It is a guard for loud patches, not a mastering tool.
 * The meter keeps the highest level since the screen last looked. Audio thread: process(). JUCE-free, no allocation.
 */
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace ghostblock::seance
{
struct OutGuard
{
    static constexpr float kCeiling = 0.97f; // about -0.26 dB
    std::atomic<bool> on{true};
    std::atomic<float> peakL{0.f}, peakR{0.f}; // highest |sample| since the screen last took it (before the limiter)
    std::atomic<float> reduction{1.f};         // lowest gain since the screen last took it (1 = untouched)

    void setRate(double sr) { rel = (float)std::exp(-1.0 / (0.12 * std::max(1000.0, sr))); }
    void process(float *L, float *R, uint32_t n)
    {
        const bool lim = on.load(std::memory_order_relaxed);
        float pl = 0.f, pr = 0.f, gmin = 1.f;
        for (uint32_t i = 0; i < n; ++i)
        {
            float l = L[i], r = R ? R[i] : L[i];
            if (!std::isfinite(l)) l = 0.f; // (never let a broken number out)
            if (!std::isfinite(r)) r = 0.f;
            const float al = std::fabs(l), ar = std::fabs(r), a = std::max(al, ar);
            pl = std::max(pl, al);
            pr = std::max(pr, ar);
            env = std::max(a, env * rel);
            if (lim && env > kCeiling)
            {
                const float g = kCeiling / env;
                l *= g; r *= g;
                gmin = std::min(gmin, g);
            }
            L[i] = l;
            if (R) R[i] = r;
        }
        if (pl > peakL.load(std::memory_order_relaxed)) peakL.store(pl, std::memory_order_relaxed);
        if (pr > peakR.load(std::memory_order_relaxed)) peakR.store(pr, std::memory_order_relaxed);
        if (gmin < reduction.load(std::memory_order_relaxed)) reduction.store(gmin, std::memory_order_relaxed);
    }

  private:
    float env{0.f}, rel{0.9998f};
};
} // namespace ghostblock::seance
