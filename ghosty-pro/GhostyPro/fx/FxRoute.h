#pragma once
// GHOST SPLIT routing (Ghost Box only): the signal is split into 2 or 3 BRANCHES and every branch runs its own chain of the rack's
// existing units (the units are assigned to a branch; one instance each, with its normal page and settings). Original code.
//   FREQUENCY       Linkwitz-Riley 4th order crossover(s): 2 bands (LOW / HIGH at X LOW) or 3 (LOW / MID / HIGH, X LOW + X HIGH; the low
//                   band is phase-matched with the high crossover's allpass), so the summed bands are flat (an allpass). With every branch
//                   empty (no unit on, levels 0 dB) the stage passes its input unchanged (crossfaded in / out over 10 ms).
//                   KEEP IN BAND (default on): what a branch CHANGES (its output minus its input, aligned) is filtered back into the
//                   branch's own band, so distortion products of the HIGH branch never reach the sub (an identity branch stays exact)
//   TRANSIENT / BODY an envelope transient detector (SENSITIVITY, ATTACK LENGTH) masks each sample: ATTACK = x * m, BODY = x * (1 - m)
//   MID / SIDE      MID = (M, M), SIDE = (S, -S): their sum is L / R exactly
//   LEVEL           a peak follower (1 ms attack, 80 ms release) against THRESHOLD with a soft KNEE: LOUD = x * w, QUIET = x * (1 - w)
//   PARALLEL        A and B both get the input; out = (1 - BLEND) A + BLEND B (identity when both are empty)
// The branches are delayed to the slowest one (the units' own latency), crossfaded when that changes; the stage's latency (the slowest
// branch) is part of what the rack reports to the host. Each branch has a LEVEL (dB). No allocation on the audio thread.
#include "FxCore.h"
#include "FxBoxUnits2.h"   // LatentDry

#if GPFX_BOX
namespace gpfx {

constexpr int kRouteBranches = 3;
enum RouteMode { rmFreq, rmTransient, rmMidSide, rmLevel, rmParallel };
inline const juce::StringArray& routeModeNames() { static juce::StringArray s { "FREQUENCY", "TRANSIENT / BODY", "MID / SIDE", "LEVEL", "PARALLEL" }; return s; }
inline const juce::StringArray& routeBandsNames() { static juce::StringArray s { "2 BANDS", "3 BANDS" }; return s; }
inline int routeBranchCount (int mode, int bandsIdx) noexcept { return mode == rmFreq && bandsIdx == 1 ? 3 : 2; }
inline juce::String routeBranchName (int mode, int bandsIdx, int b)
{
    switch (mode)
    {
        case rmFreq:      return bandsIdx == 1 ? (b == 0 ? "LOW" : b == 1 ? "MID" : "HIGH") : (b == 0 ? "LOW" : "HIGH");
        case rmTransient: return b == 0 ? "ATTACK" : "BODY";
        case rmMidSide:   return b == 0 ? "MID" : "SIDE";
        case rmLevel:     return b == 0 ? "LOUD" : "QUIET";
        default:          return b == 0 ? "A" : "B";
    }
}

// Linkwitz-Riley 4th order (two cascaded Butterworth sections, TPT state-variable form): LP + HP = an allpass. Coefficients may move per sample.
struct RouteLr4
{
    struct Sec { float ic1 = 0, ic2 = 0; void reset() { ic1 = ic2 = 0; } };
    std::array<Sec, 2> lpS, hpS; float g = 0, k = 1.41421356f, a1 = 0, a2 = 0, a3 = 0;
    void set (double sr, double f)
    {
        g = (float) std::tan (juce::MathConstants<double>::pi * juce::jlimit (10.0, sr * 0.45, f) / sr);
        a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    void reset() { for (auto& s : lpS) s.reset(); for (auto& s : hpS) s.reset(); }
    inline void tick (Sec& s, float v0, float& lp, float& hp) const noexcept
    {
        const float v3 = v0 - s.ic2, v1 = a1 * s.ic1 + a2 * v3, v2 = s.ic2 + a2 * s.ic1 + a3 * v3;
        s.ic1 = 2 * v1 - s.ic1; s.ic2 = 2 * v2 - s.ic2; lp = v2; hp = v0 - k * v1 - v2;
    }
    void split (float x, float& low, float& high) noexcept
    {
        float l1, h1, l2, h2, d;
        tick (lpS[0], x, l1, d); tick (lpS[1], l1, l2, d); low = l2;
        tick (hpS[0], x, d, h1); tick (hpS[1], h1, d, h2); high = h2;
    }
    float allpass (float x) noexcept { float lo, hi; split (x, lo, hi); return lo + hi; }
};

class RouteStage
{
public:
    // host parameters (fx_route_*, appended at the very end of the plugin's parameter list); bound through the MORPH / SCENES shadows
    P on, mode, bands, xlo, xhi, sens, len, thr, knee, blend, keep; std::array<P, kRouteBranches> gain;
    bool keepNow = true;   // KEEP IN BAND as the rack latched it (a change goes through the rack's click-free switch)
    void attach (juce::AudioProcessorValueTreeState& s)
    {
        on.bind (s, "fx_route_on", 0); mode.bind (s, "fx_route_mode", 0); bands.bind (s, "fx_route_bands", 0);
        xlo.bind (s, "fx_route_xlo", 120.0f); xhi.bind (s, "fx_route_xhi", 3000.0f); sens.bind (s, "fx_route_sens", 0.5f); len.bind (s, "fx_route_len", 30.0f);
        thr.bind (s, "fx_route_thr", -18.0f); knee.bind (s, "fx_route_knee", 6.0f); blend.bind (s, "fx_route_blend", 0.5f); keep.bind (s, "fx_route_keep", 1.0f);
        for (int b = 0; b < kRouteBranches; ++b) gain[(size_t) b].bind (s, "fx_route_g" + juce::String (b + 1), 0.0f);
    }
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (auto& d : delay) d.prepare (16384, sr);
        for (auto& d : preDelay) d.prepare (16384, sr);
        dryDelay.prepare (16384, sr);
        xloSm.setTime (sr, 30.0f); xhiSm.setTime (sr, 30.0f); blendSm.setTime (sr, 25.0f); passSm.setTime (sr, 10.0f);
        for (auto& g : gainSm) g.setTime (sr, 20.0f);
        envA = coef (1.0f); envR = coef (80.0f); wSmA = coef (2.0f);
        reset();
    }
    void reset()
    {
        for (auto& x : xLo) x.reset(); for (auto& x : xHi) x.reset(); for (auto& x : apHi) x.reset();
        for (auto& b : post) for (auto& c : b) for (auto& f : c) f.reset();
        for (auto& d : delay) d.clear(); for (auto& d : preDelay) d.clear(); dryDelay.clear();
        fast = slow = mask = 0; env = 0; wNow = 0;
        xloSm.snap (std::log2 (juce::jlimit (30.0f, 2000.0f, xlo()))); xhiSm.snap (std::log2 (juce::jlimit (500.0f, 16000.0f, xhi())));
        blendSm.snap (clampf (blend(), 0.0f, 1.0f));
        for (int b = 0; b < kRouteBranches; ++b) gainSm[(size_t) b].snap (dbToGain (juce::jlimit (-24.0f, 12.0f, gain[(size_t) b]())));
        passSm.snap (passTarget);
    }
    void snap() { blendSm.snap (clampf (blend(), 0.0f, 1.0f)); for (int b = 0; b < kRouteBranches; ++b) gainSm[(size_t) b].snap (dbToGain (juce::jlimit (-24.0f, 12.0f, gain[(size_t) b]()))); passSm.snap (passTarget); }
    float coef (float ms) const { return 1.0f - (float) std::exp (-1.0 / std::max (1.0, ms * 0.001 * sr)); }

    // ---- audio thread: split the input into the branch buffers (mode / branch count as latched by the rack)
    std::array<std::array<std::array<float, kSub>, 2>, kRouteBranches> br {};
    std::array<float, kSub> dryL {}, dryR {};
    void split (const float* L, const float* R, int n, int m, int nb) noexcept
    {
        std::copy (L, L + n, dryL.begin()); std::copy (R, R + n, dryR.begin());
        switch (m)
        {
            case rmFreq:
            {
                xloSm.target = std::log2 (juce::jlimit (30.0f, 2000.0f, xlo()));
                xhiSm.target = std::log2 (juce::jlimit (500.0f, 16000.0f, std::max (xhi(), xlo() * 1.5f)));
                for (int i = 0; i < n; ++i)
                {
                    const float flo = std::exp2 (xloSm.next()), fhi = std::exp2 (xhiSm.next());
                    if ((i & 3) == 0) { for (auto& x : xLo) x.set (sr, flo); for (auto& x : xHi) x.set (sr, fhi); for (auto& x : apHi) x.set (sr, fhi); }
                    const float in[2] { L[i], R[i] };
                    for (int c = 0; c < 2; ++c)
                    {
                        float lo, hi; xLo[(size_t) c].split (in[c], lo, hi);
                        if (nb == 2) { br[0][(size_t) c][(size_t) i] = lo; br[1][(size_t) c][(size_t) i] = hi; br[2][(size_t) c][(size_t) i] = 0.0f; }
                        else { float mid, top; xHi[(size_t) c].split (hi, mid, top); br[0][(size_t) c][(size_t) i] = apHi[(size_t) c].allpass (lo); br[1][(size_t) c][(size_t) i] = mid; br[2][(size_t) c][(size_t) i] = top; }
                    }
                }
                for (int b = 0; b < nb; ++b) for (int c = 0; c < 2; ++c) std::copy (br[(size_t) b][(size_t) c].begin(), br[(size_t) b][(size_t) c].begin() + n, pre[(size_t) b][(size_t) c].begin());
                break;
            }
            case rmTransient:
            {   // a level envelope (0.1 ms attack, 8 ms release) against a slow copy that rises over ATTACK LENGTH: onsets run ahead = ATTACK
                const float xo = clampf (len(), 2.0f, 400.0f), fa = coef (0.1f), fr = coef (8.0f), sa = coef (xo), sRel = coef (8.0f), mRel = coef (xo * 0.5f), mAtt = coef (0.25f);
                const float sn = clampf (sens(), 0.0f, 1.0f), kk = 1.5f + 6.0f * sn, th = 0.08f + 0.5f * (1.0f - sn);
                for (int i = 0; i < n; ++i)
                {
                    const float x = 0.5f * (std::abs (L[i]) + std::abs (R[i]));
                    fast += (x > fast ? fa : fr) * (x - fast);
                    slow += (fast > slow ? sa : sRel) * (fast - slow);
                    const float tgt = clampf ((fast / (slow + 1.0e-5f) - 1.0f - th) * kk, 0.0f, 1.0f) * (fast > 1.0e-5f ? 1.0f : 0.0f);
                    mask += (tgt > mask ? mAtt : mRel) * (tgt - mask);
                    const float al = L[i] * mask, ar = R[i] * mask;
                    br[0][0][(size_t) i] = al; br[0][1][(size_t) i] = ar; br[1][0][(size_t) i] = L[i] - al; br[1][1][(size_t) i] = R[i] - ar;
                }
                break;
            }
            case rmMidSide:
                for (int i = 0; i < n; ++i)
                {
                    const float mid = 0.5f * (L[i] + R[i]), side = 0.5f * (L[i] - R[i]);
                    br[0][0][(size_t) i] = br[0][1][(size_t) i] = mid; br[1][0][(size_t) i] = side; br[1][1][(size_t) i] = -side;
                }
                break;
            case rmLevel:
            {
                const float t = juce::jlimit (-60.0f, 0.0f, thr()), kn = juce::jlimit (0.0f, 24.0f, knee());
                for (int i = 0; i < n; ++i)
                {
                    const float x = std::max (std::abs (L[i]), std::abs (R[i]));
                    env += (x > env ? envA : envR) * (x - env);
                    const float db = 20.0f * std::log10 (env + 1.0e-9f);
                    float w;
                    if (kn < 0.01f) w = db >= t ? 1.0f : 0.0f;
                    else { const float u = clampf ((db - (t - 0.5f * kn)) / kn, 0.0f, 1.0f); w = u * u * (3.0f - 2.0f * u); }
                    wNow += wSmA * (w - wNow);
                    const float ll = L[i] * wNow, lr = R[i] * wNow;
                    br[0][0][(size_t) i] = ll; br[0][1][(size_t) i] = lr; br[1][0][(size_t) i] = L[i] - ll; br[1][1][(size_t) i] = R[i] - lr;
                }
                break;
            }
            default:
                for (int b = 0; b < 2; ++b) { std::copy (L, L + n, br[(size_t) b][0].begin()); std::copy (R, R + n, br[(size_t) b][1].begin()); }
                break;
        }
    }
    // ---- audio thread: align the branches (lat = each branch's latency), apply the levels, sum into L / R. 'pass' (FREQUENCY only):
    // every branch is empty, so the stage glides to its (aligned) input instead of the allpass sum
    int sum (float* L, float* R, int n, int m, int nb, const std::array<int, kRouteBranches>& lat, bool pass) noexcept
    {
        int maxLat = 0; for (int b = 0; b < nb; ++b) maxLat = std::max (maxLat, lat[(size_t) b]);
        maxLat = std::min (maxLat, 16384);
        if (m == rmFreq && keepNow)
        {   // KEEP IN BAND: band + its own band filter (branch output - band input, the input delayed by the branch's latency)
            const float flo = std::exp2 (xloSm.cur), fhi = std::exp2 (xhiSm.cur);
            for (int b = 0; b < nb; ++b)
            {
                preDelay[(size_t) b].process (pre[(size_t) b][0].data(), pre[(size_t) b][1].data(), n, std::min (lat[(size_t) b], 16384));
                for (int c = 0; c < 2; ++c)
                {
                    auto& f = post[(size_t) b][(size_t) c];
                    f[0].set (sr, flo); f[1].set (sr, fhi);
                    float* x = br[(size_t) b][(size_t) c].data(); const float* in = pre[(size_t) b][(size_t) c].data();
                    for (int i = 0; i < n; ++i)
                    {
                        const float d = x[i] - in[i]; float lo, hi, y;
                        if (b == 0) { f[0].split (d, lo, hi); y = lo; }                                   // LOW: below X LOW
                        else if (b == nb - 1) { (nb == 2 ? f[0] : f[1]).split (d, lo, hi); y = hi; }     // HIGH: above the top crossover
                        else { f[0].split (d, lo, hi); float l2, h2; f[1].split (hi, l2, h2); y = l2; }   // MID: between them
                        x[i] = in[i] + y;
                    }
                }
            }
        }
        for (int b = 0; b < nb; ++b) delay[(size_t) b].process (br[(size_t) b][0].data(), br[(size_t) b][1].data(), n, std::max (0, maxLat - lat[(size_t) b]));
        for (int b = 0; b < kRouteBranches; ++b) gainSm[(size_t) b].target = dbToGain (juce::jlimit (-24.0f, 12.0f, gain[(size_t) b]()));
        passTarget = m == rmFreq && pass ? 1.0f : 0.0f; passSm.target = passTarget;
        const bool usePass = m == rmFreq && (passSm.cur > 0.0f || passTarget > 0.0f);
        if (usePass) dryDelay.process (dryL.data(), dryR.data(), n, maxLat);
        blendSm.target = clampf (blend(), 0.0f, 1.0f);
        for (int i = 0; i < n; ++i)
        {
            float l = 0, r = 0;
            if (m == rmParallel)
            {
                const float bl = blendSm.next(), ga = gainSm[0].next() * (1.0f - bl), gb = gainSm[1].next() * bl;
                l = ga * br[0][0][(size_t) i] + gb * br[1][0][(size_t) i]; r = ga * br[0][1][(size_t) i] + gb * br[1][1][(size_t) i];
            }
            else
                for (int b = 0; b < nb; ++b) { const float g = gainSm[(size_t) b].next(); l += g * br[(size_t) b][0][(size_t) i]; r += g * br[(size_t) b][1][(size_t) i]; }
            if (usePass)
            {
                const float w = passSm.next(); if (passTarget == 1.0f && w > 0.9999f) passSm.snap (1.0f);
                l += passSm.cur * (dryL[(size_t) i] - l); r += passSm.cur * (dryR[(size_t) i] - r);
            }
            L[i] = l; R[i] = r;
        }
        if (! usePass || (passTarget == 0.0f && passSm.cur < 1.0e-6f)) passSm.snap (0.0f);
        return maxLat;
    }
    bool gainsUnity() const noexcept { for (auto& g : gain) if (std::abs (g()) > 1.0e-4f) return false; return true; }
private:
    double sr = 48000;
    std::array<RouteLr4, 2> xLo, xHi, apHi;
    std::array<LatentDry, kRouteBranches> delay, preDelay; LatentDry dryDelay;
    std::array<std::array<std::array<float, kSub>, 2>, kRouteBranches> pre {};
    std::array<std::array<std::array<RouteLr4, 2>, 2>, kRouteBranches> post;   // per branch, per channel: the X LOW and X HIGH filters for KEEP IN BAND
    ExpSmooth xloSm, xhiSm, blendSm, passSm; std::array<ExpSmooth, kRouteBranches> gainSm;
    float fast = 0, slow = 0, mask = 0, env = 0, wNow = 0, envA = 0, envR = 0, wSmA = 0, passTarget = 1.0f;
};

inline void addRouteParameters (juce::AudioProcessorValueTreeState::ParameterLayout& l)
{   // (appended at the very end of the Ghost Box parameter list: no existing automation index moves; defaults = off, today's sound)
    addBool (l, "fx_route_on", "FX Ghost Split On", false);
    addChoice (l, "fx_route_mode", "FX Ghost Split Mode", routeModeNames(), 0);
    addChoice (l, "fx_route_bands", "FX Ghost Split Bands", routeBandsNames(), 0);
    addFloat (l, "fx_route_xlo", "FX Ghost Split Low Crossover", skewRange (30.0f, 2000.0f, 200.0f), 120.0f, "Hz", 0);
    addFloat (l, "fx_route_xhi", "FX Ghost Split High Crossover", skewRange (500.0f, 16000.0f, 3000.0f), 3000.0f, "Hz", 0);
    addPct (l, "fx_route_sens", "FX Ghost Split Sensitivity", 0.5f);
    addFloat (l, "fx_route_len", "FX Ghost Split Attack Length", skewRange (5.0f, 300.0f, 50.0f), 30.0f, "ms", 0);
    addFloat (l, "fx_route_thr", "FX Ghost Split Threshold", juce::NormalisableRange<float> (-60.0f, 0.0f), -18.0f, "dB", 1);
    addFloat (l, "fx_route_knee", "FX Ghost Split Knee", juce::NormalisableRange<float> (0.0f, 24.0f), 6.0f, "dB", 1);
    addPct (l, "fx_route_blend", "FX Ghost Split Blend", 0.5f);
    addBool (l, "fx_route_keep", "FX Ghost Split Keep In Band", true);
    for (int b = 0; b < kRouteBranches; ++b)
        addFloat (l, "fx_route_g" + juce::String (b + 1), "FX Ghost Split Branch " + juce::String (b + 1) + " Level", juce::NormalisableRange<float> (-24.0f, 12.0f), 0.0f, "dB", 1);
}

} // namespace gpfx
#endif
