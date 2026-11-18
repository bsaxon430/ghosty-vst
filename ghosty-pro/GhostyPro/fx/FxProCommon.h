#pragma once
// FX RACK (round 3): small helpers shared by the Pro-only units (COMPRESSOR, SPLITTER). Original code.
#include "FxCore.h"

#if ! GPFX_BOX
namespace gpfx
{
// A stereo delay whose length may change while audio runs: a change crossfades (raised cosine, 10 ms) from the old read head
// to the new one, so a unit whose latency appears / goes (and the dry line that matches it) never steps.
struct SwitchDelay
{
    std::array<Ring, 2> r; int cur = 0, old = 0, xf = 0, xfLen = 480;
    void prepare (int maxDelay, double sr) { for (auto& x : r) x.prepare (maxDelay + 8); xfLen = std::max (16, (int) (0.010 * sr)); cur = old = 0; xf = 0; }
    void clear() { for (auto& x : r) x.clear(); xf = 0; old = cur; }
    void process (float* L, float* R, int n, int d) noexcept
    {
        d = juce::jlimit (0, r[0].size() - 8, d);
        if (d != cur) { old = xf > 0 ? old : cur; cur = d; xf = xfLen; }
        if (cur == 0 && xf == 0) { for (int i = 0; i < n; ++i) { r[0].push (L[i]); r[1].push (R[i]); } return; }
        for (int i = 0; i < n; ++i)
        {
            r[0].push (L[i]); r[1].push (R != nullptr ? R[i] : L[i]);
            float l = r[0].at (cur), rr = r[1].at (cur);
            if (xf > 0)
            {
                const float t = (float) xf / (float) xfLen, w = 0.5f - 0.5f * std::cos (kPi * t);   // old weight 1 -> 0
                l += w * (r[0].at (old) - l); rr += w * (r[1].at (old) - rr); --xf;
            }
            L[i] = l; if (R != nullptr) R[i] = rr;
        }
    }
};

// Linkwitz-Riley 4th order (two cascaded 2nd-order Butterworth sections) LP / HP pair from one state-variable core per section:
// LP + HP sum to an allpass (flat magnitude). Coefficients may move every sample (TPT form, no zipper).
struct Lr4
{
    struct Sec { float ic1 = 0, ic2 = 0; void reset() { ic1 = ic2 = 0; } };
    std::array<Sec, 2> lpS, hpS; float g = 0, k = 1.41421356f, a1 = 0, a2 = 0, a3 = 0;
    void set (double sr, double f)
    {
        g = (float) std::tan (juce::MathConstants<double>::pi * juce::jlimit (10.0, sr * 0.45, f) / sr);
        k = 1.41421356f; a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    void reset() { for (auto& s : lpS) s.reset(); for (auto& s : hpS) s.reset(); }
    inline void tick (Sec& s, float v0, float& lp, float& hp) const noexcept
    {
        const float v3 = v0 - s.ic2, v1 = a1 * s.ic1 + a2 * v3, v2 = s.ic2 + a2 * s.ic1 + a3 * v3;
        s.ic1 = 2 * v1 - s.ic1; s.ic2 = 2 * v2 - s.ic2; lp = v2; hp = v0 - k * v1 - v2;
    }
    // x -> low (LP4), high (HP4); low + high = allpass
    void split (float x, float& low, float& high) noexcept
    {
        float l1, h1, l2, h2, dummy;
        tick (lpS[0], x, l1, dummy); tick (lpS[1], l1, l2, dummy); low = l2;
        tick (hpS[0], x, dummy, h1); tick (hpS[1], h1, dummy, h2); high = h2;
    }
};
// The allpass a LR4 split at the same frequency applies (LP4 + HP4), for phase-matching the other bands of a 3-band split
struct Lr4Allpass
{
    Lr4 x; void set (double sr, double f) { x.set (sr, f); } void reset() { x.reset(); }
    float process (float v) noexcept { float lo, hi; x.split (v, lo, hi); return lo + hi; }
};
} // namespace gpfx
#endif
