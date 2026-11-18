/*
 * GhostBlock BLEND GUARD (owner, 2026-11-16: "sound going thin when using morph - build something that blends better or at least
 * says it's cancelling each other out"). Header only, no allocation after prepare(), used by the blend boards that mix REAL sounds
 * (Poltergeist 2 MORPH, Ghost Bass BLEND BOARD). It never touches an engine: it works on the sounds after their engines, where the
 * board mixes them.
 *
 * Why blends go thin: two sounds playing the same note are not independent. Where their waves run against each other they cancel
 * (often most of the low end), so the middle of the board sounds weaker than either corner; where they run together the middle comes
 * out louder. The equal-power law (gain = sqrt(weight)) is only right for unrelated sounds.
 *
 * What it does, while two or more sounds are playing:
 *   PHASE   it measures how the sounds line up (their covariance, about 0.3 s of memory) and, when flipping a sound's polarity
 *           (upside down: inaudible on its own) makes the mix fuller by 1.5 dB or more for a moment, flips it - but only while that
 *           sound is silent, between notes (2026-11-16: a flip in the middle of a note clicked on sub basses). The loudest
 *           sound is never flipped.
 *   BODY    six bands (120 / 300 / 700 / 1600 / 4000 Hz, split by subtraction so the bands add back to exactly the input): in each
 *           band the mix is held at the level the sounds should add up to at this puck position (the sum of their own powers, i.e. the
 *           weighted average of the corners' loudness), within -6 .. +6 dB, gains gliding over ~120 ms.
 *   SAYS    lossDb: how much the raw blend is losing to cancellation right now (dB, <= 0), worst pair; fixed: whether a flip is in.
 * With one sound playing (the puck on a corner) the output is exactly that sound: nothing is split, measured or changed.
 */
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace ghostblock
{
class BlendGuard
{
  public:
    static constexpr int kSrc = 5, kBands = 6, kCross = kBands - 1;

    std::atomic<bool> on{true};
    // what it hears (any thread reads; the audio thread writes)
    std::atomic<float> lossDb{0.f};    // the raw blend's loss to cancellation (<= 0), smoothed
    std::atomic<float> gainDb{0.f};    // the average body correction it applies now
    std::atomic<int> pairA{-1}, pairB{-1}; // the two sources that cancel most (-1 none)
    std::atomic<int> flips{0};         // bit per source: polarity flipped
    std::atomic<bool> active{false};   // two or more sounds playing

    BlendGuard() { prepare(48000.0); }
    void prepare(double sampleRate)
    {
        sr = sampleRate > 0 ? sampleRate : 48000.0;
        static const float fc[kCross] = {120.f, 300.f, 700.f, 1600.f, 4000.f};
        for (int b = 0; b < kCross; ++b)
            lp[b].design(fc[b], (float)sr);
        reset();
    }
    void reset()
    {
        for (auto &s : srcF)
            for (auto &c : s)
                for (auto &f : c)
                    f.clear();
        for (auto &c : outF)
            for (auto &f : c)
                f.clear();
        for (auto &r : C)
            r.fill(0.f);
        T.fill(0.f);
        A.fill(0.f);
        g.fill(1.f);
        for (int i = 0; i < kSrc; ++i)
        {
            sign[i] = sgn[i] = want[i] = 1.f;
        }
        holdBlocks = 0;
        bypassed = true;
        lossDb = 0.f;
        gainDb = 0.f;
        pairA = pairB = -1;
        flips = 0;
        active = false;
    }

    // ---- audio thread, once per block. srcL / srcR: each source's contribution to the mix (gains already applied), nullptr = not
    // playing. 1) phase(): measures and flips polarity IN PLACE in the source buffers; then the caller mixes them (any way it likes)
    // into outL / outR; 2) body(): holds each band of that mix at the level the sources should add up to.
    void phase(float *const *srcL, float *const *srcR, int n)
    {
        int nAct = 0, loud = -1;
        float pw[kSrc] = {};
        for (int i = 0; i < kSrc; ++i)
        {
            if (!srcL[i])
                continue;
            ++nAct;
            double e = 0.0;
            for (int s = 0; s < n; ++s)
                e += 0.25 * (srcL[i][s] + srcR[i][s]) * (srcL[i][s] + srcR[i][s]);
            pw[i] = (float)(e / std::max(1, n));
            if (pw[i] < 1e-10f)
                sgn[i] = sign[i] = want[i]; // a flip only ever happens while that sound is silent (between notes): it can't click
            if (loud < 0 || pw[i] > pw[loud])
                loud = i;
        }
        multi = nAct >= 2 && on.load(std::memory_order_relaxed);
        active.store(nAct >= 2, std::memory_order_relaxed);
        if (nAct < 2)
        {
            for (int i = 0; i < kSrc; ++i)
                want[i] = 1.f; // (a sound on its own goes back to normal at its next silence)
            decayStats(n);
            applySigns(srcL, srcR, n); // (a lone flipped source stays flipped until it is silent: no click)
            return;
        }
        // covariance of the sources as they are (before any flip): mono, one-pole over ~0.3 s
        const float a = blockCoef(n, 0.3f);
        for (int i = 0; i < kSrc; ++i)
            for (int j = i; j < kSrc; ++j)
            {
                float c = 0.f;
                if (srcL[i] && srcL[j])
                {
                    double acc = 0.0;
                    for (int s = 0; s < n; ++s)
                        acc += 0.25 * (srcL[i][s] + srcR[i][s]) * (srcL[j][s] + srcR[j][s]);
                    c = (float)(acc / std::max(1, n));
                }
                C[i][j] += (c - C[i][j]) * a;
                C[j][i] = C[i][j];
            }
        // the raw loss, and the worst pair
        double tgt = 0.0, raw = 0.0;
        int wa = -1, wb = -1;
        float worst = -0.25f;
        for (int i = 0; i < kSrc; ++i)
        {
            if (!srcL[i])
                continue;
            tgt += C[i][i];
            for (int j = 0; j < kSrc; ++j)
                if (srcL[j])
                    raw += C[i][j];
            for (int j = i + 1; j < kSrc; ++j)
                if (srcL[j] && C[i][i] > 1e-12f && C[j][j] > 1e-12f)
                {
                    const float r = C[i][j] / std::sqrt(C[i][i] * C[j][j]);
                    if (r < worst)
                    {
                        worst = r;
                        wa = i;
                        wb = j;
                    }
                }
        }
        const float ld = tgt > 1e-14 ? std::max(-40.f, (float)(10.0 * std::log10(std::max(raw, 1e-12 * tgt) / tgt))) : 0.f;
        lossDb.store(lossDb.load(std::memory_order_relaxed) + (std::min(0.f, ld) - lossDb.load(std::memory_order_relaxed)) * 0.2f,
                     std::memory_order_relaxed);
        pairA.store(wa, std::memory_order_relaxed);
        pairB.store(wb, std::memory_order_relaxed);
        if (multi)
        {
            // the best polarity for every source but the loudest (which keeps its own)
            int idx[kSrc], m = 0;
            for (int i = 0; i < kSrc; ++i)
                if (srcL[i] && i != loud)
                    idx[m++] = i;
            auto power = [&](const float *s) {
                double p = 0.0;
                for (int i = 0; i < kSrc; ++i)
                    if (srcL[i])
                        for (int j = 0; j < kSrc; ++j)
                            if (srcL[j])
                                p += (double)s[i] * s[j] * C[i][j];
                return p;
            };
            float best[kSrc], cur[kSrc], trial[kSrc];
            for (int i = 0; i < kSrc; ++i)
                best[i] = cur[i] = trial[i] = want[i];
            double bestP = power(cur);
            const double curP = bestP;
            for (int mask = 0; mask < (1 << m); ++mask)
            {
                for (int k = 0; k < m; ++k)
                    trial[idx[k]] = (mask >> k) & 1 ? -want[loud] : want[loud];
                trial[loud] = want[loud];
                const double p = power(trial);
                if (p > bestP)
                {
                    bestP = p;
                    for (int i = 0; i < kSrc; ++i)
                        best[i] = trial[i];
                }
            }
            bool differs = false;
            for (int i = 0; i < kSrc; ++i)
                differs = differs || (srcL[i] && best[i] != want[i]);
            if (differs && bestP > curP * 1.41 + 1e-15) // +1.5 dB
            {
                if (++holdBlocks * n >= (int)(0.12 * sr)) // and for a moment (not a single note's start)
                {
                    for (int i = 0; i < kSrc; ++i)
                        if (srcL[i])
                            want[i] = best[i]; // (taken at that sound's next silence)
                    holdBlocks = 0;
                }
            }
            else
                holdBlocks = 0;
        }
        else
        {
            for (int i = 0; i < kSrc; ++i)
                want[i] = 1.f; // (switched off: back to normal at each sound's next silence)
            holdBlocks = 0;
        }
        int fl = 0;
        for (int i = 0; i < kSrc; ++i)
            fl |= sgn[i] < 0.f ? (1 << i) : 0;
        flips.store(fl, std::memory_order_relaxed);
        applySigns(srcL, srcR, n);
    }

    void body(float *const *srcL, float *const *srcR, float *outL, float *outR, int n)
    {
        for (int s0 = 0; s0 < n; s0 += kMaxBlock)
        {
            float *l[kSrc]{}, *r[kSrc]{};
            for (int i = 0; i < kSrc; ++i)
                if (srcL[i])
                {
                    l[i] = srcL[i] + s0;
                    r[i] = srcR[i] + s0;
                }
            bodyChunk(l, r, outL + s0, outR + s0, std::min(kMaxBlock, n - s0));
        }
    }
    static constexpr int kMaxBlock = 256;

  private:
    void bodyChunk(float *const *srcL, float *const *srcR, float *outL, float *outR, int n)
    {
        bool unity = true;
        for (auto v : g)
            unity = unity && std::fabs(v - 1.f) < 1e-4f;
        if (!multi && unity)
        { // one sound (or the guard off, settled): untouched
            if (!bypassed)
            {
                for (auto &s : srcF)
                    for (auto &c : s)
                        for (auto &f : c)
                            f.clear();
                for (auto &c : outF)
                    for (auto &f : c)
                        f.clear();
                g.fill(1.f);
                bypassed = true;
            }
            gainDb.store(0.f, std::memory_order_relaxed);
            return;
        }
        bypassed = false;
        float tb[kBands] = {}, ab[kBands] = {};
        // what each band should be: the sum of the sources' own band powers
        float band[kBands];
        for (int i = 0; i < kSrc; ++i)
        {
            if (!srcL[i])
                continue;
            for (int ch = 0; ch < 2; ++ch)
            {
                const float *x = ch ? srcR[i] : srcL[i];
                for (int s = 0; s < n; ++s)
                {
                    split(srcF[i][ch], x[s], band);
                    for (int b = 0; b < kBands; ++b)
                        tb[b] += band[b] * band[b];
                }
            }
        }
        // what the mix has
        float ob[2][kBands][kMaxBlock];
        const int nn = n;
        for (int ch = 0; ch < 2; ++ch)
        {
            const float *x = ch ? outR : outL;
            for (int s = 0; s < nn; ++s)
            {
                split(outF[ch], x[s], band);
                for (int b = 0; b < kBands; ++b)
                {
                    ob[ch][b][s] = band[b];
                    ab[b] += band[b] * band[b];
                }
            }
        }
        const float a = blockCoef(n, 0.15f);
        float wantG[kBands];
        double gsum = 0.0, wsum = 0.0;
        for (int b = 0; b < kBands; ++b)
        {
            T[b] += (tb[b] / (float)n - T[b]) * a;
            A[b] += (ab[b] / (float)n - A[b]) * a;
            float w = 1.f;
            if (multi && T[b] > 1e-12f)
                w = std::clamp(std::sqrt(T[b] / std::max(A[b], 1e-12f)), 0.5f, 2.0f);
            wantG[b] = w;
            gsum += (double)T[b] * 20.0 * std::log10(w);
            wsum += T[b];
        }
        gainDb.store(wsum > 1e-12 ? (float)(gsum / wsum) : 0.f, std::memory_order_relaxed);
        const float k = 1.f - std::exp(-1.f / ((multi ? 0.12f : 0.03f) * (float)sr)); // (slow while blending: a level correction, never a click; back to untouched quickly when one sound is left)
        for (int s = 0; s < nn; ++s)
        {
            float l = 0.f, r = 0.f;
            for (int b = 0; b < kBands; ++b)
            {
                g[b] += (wantG[b] - g[b]) * k;
                l += g[b] * ob[0][b][s];
                r += g[b] * ob[1][b][s];
            }
            outL[s] = l;
            outR[s] = r;
        }
    }

    struct LP // 2nd-order Butterworth low-pass (RBJ), transposed direct form II
    {
        float b0{1}, b1{0}, b2{0}, a1{0}, a2{0};
        void design(float fc, float fs)
        {
            const float w = 2.f * 3.14159265358979f * std::min(fc, 0.45f * fs) / fs, c = std::cos(w), al = std::sin(w) / (2.f * 0.70710678f);
            const float a0 = 1.f + al;
            b0 = (1.f - c) * 0.5f / a0;
            b1 = (1.f - c) / a0;
            b2 = b0;
            a1 = -2.f * c / a0;
            a2 = (1.f - al) / a0;
        }
    };
    struct St
    {
        float z1{0}, z2{0};
        void clear() { z1 = z2 = 0.f; }
    };
    std::array<LP, kCross> lp;
    using Chain = std::array<St, kCross>;
    // subtractive split: band 0 = LP0(x), rest = x - band 0, band 1 = LP1(rest) ... the last band is what is left: they add back to x
    void split(Chain &st, float x, float *out) const
    {
        float rest = x;
        for (int b = 0; b < kCross; ++b)
        {
            const auto &f = lp[(size_t)b];
            auto &z = st[(size_t)b];
            const float y = f.b0 * rest + z.z1;
            z.z1 = f.b1 * rest - f.a1 * y + z.z2;
            z.z2 = f.b2 * rest - f.a2 * y;
            out[b] = y;
            rest -= y;
        }
        out[kCross] = rest;
    }
    float blockCoef(int n, float tau) const { return 1.f - std::exp(-(float)n / (tau * (float)sr)); }
    void decayStats(int n)
    {
        const float a = blockCoef(n, 0.3f);
        lossDb.store(lossDb.load(std::memory_order_relaxed) * (1.f - a), std::memory_order_relaxed);
        pairA.store(-1, std::memory_order_relaxed);
        pairB.store(-1, std::memory_order_relaxed);
        int fl = 0;
        for (int i = 0; i < kSrc; ++i)
            fl |= sgn[i] < 0.f ? (1 << i) : 0;
        flips.store(fl, std::memory_order_relaxed);
    }
    void applySigns(float *const *srcL, float *const *srcR, int n)
    {
        const float step = 1.f / std::max(1.f, 0.02f * (float)sr); // a flip glides over 20 ms
        for (int i = 0; i < kSrc; ++i)
        {
            if (!srcL[i])
                continue;
            if (sign[i] == 1.f && sgn[i] == 1.f)
                continue; // (normal: untouched, bit for bit)
            for (int s = 0; s < n; ++s)
            {
                if (sign[i] < sgn[i])
                    sign[i] = std::min(sgn[i], sign[i] + 2.f * step);
                else if (sign[i] > sgn[i])
                    sign[i] = std::max(sgn[i], sign[i] - 2.f * step);
                srcL[i][s] *= sign[i];
                srcR[i][s] *= sign[i];
            }
        }
    }
    double sr{48000.0};
    std::array<std::array<float, kSrc>, kSrc> C{};
    std::array<float, kBands> T{}, A{}, g{};
    float sign[kSrc]{1, 1, 1, 1, 1}, sgn[kSrc]{1, 1, 1, 1, 1}; // the polarity now (gliding) and where it is going
    float want[kSrc]{1, 1, 1, 1, 1};                             // the polarity decided, taken when that sound is next silent
    std::array<std::array<Chain, 2>, kSrc> srcF{};
    std::array<Chain, 2> outF{};
    int holdBlocks{0};
    bool bypassed{true}, multi{false};
};
} // namespace ghostblock
