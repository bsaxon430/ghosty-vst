#pragma once
// Ghost FX rack: MASTER units - OTT (3-band upward + downward compression) and GHOST LIMITER (lookahead, no overs).
#include "FxCore.h"

namespace gpfx {

// ================================================================ OTT
class OttUnit : public UnitDsp
{
public:
    Telemetry* tm = nullptr;
    bool selfMix() const override { return true; }   // fx_ott_mix is the DEPTH, applied per band
    void attach (juce::AudioProcessorValueTreeState& s) override
    {
        depth.bind (s, pid (uOtt, "mix"), 0.5f); time.bind (s, pid (uOtt, "time"), 0.5f); up.bind (s, pid (uOtt, "up"), 0.6f);
        down.bind (s, pid (uOtt, "down"), 0.7f); inG.bind (s, pid (uOtt, "in")); outG.bind (s, pid (uOtt, "out"));
    }
    void prepare (double sr_, int) override
    {
        sr = sr_;
        juce::dsp::ProcessSpec spec { sr, (juce::uint32) kSub, 2 };
        for (auto* f : { &x1, &x2, &ap2 }) f->prepare (spec);
        x1.setType (juce::dsp::LinkwitzRileyFilterType::lowpass); x1.setCutoffFrequency (120.0f);
        x2.setType (juce::dsp::LinkwitzRileyFilterType::lowpass); x2.setCutoffFrequency (2500.0f);
        ap2.setType (juce::dsp::LinkwitzRileyFilterType::allpass); ap2.setCutoffFrequency (2500.0f);
        depthSm.setTime (sr, 30.0f); inSm.setTime (sr, 20.0f); outSm.setTime (sr, 20.0f);
        reset();
    }
    void reset() override
    {
        x1.reset(); x2.reset(); ap2.reset();
        for (auto& e : env) e = dbToGain (-36.0f);   // neutral: between the up and down thresholds
        prime = 96;                                    // follow instantly at first so a fresh enable doesn't pump
        for (auto& g : gdb) g = 0;
        depthSm.snap (depth()); inSm.snap (dbToGain (inG())); outSm.snap (dbToGain (outG()));
    }
    void process (float* L, float* R, int n, const BlockCtx&) override
    {
        const float ts = std::pow (10.0f, (time() - 0.5f) * 2.0f);   // 0.1x .. 10x
        static const float attMs[3] { 8.0f, 4.0f, 2.0f }, relMs[3] { 160.0f, 90.0f, 60.0f }, thrD[3] { -30.0f, -31.0f, -33.0f };
        float at[3], rt[3];
        for (int b = 0; b < 3; ++b)
        {
            at[b] = 1.0f - (float) std::exp (-1.0 / (attMs[b] * ts * 0.001 * sr));
            rt[b] = 1.0f - (float) std::exp (-1.0 / (relMs[b] * ts * 0.001 * sr));
        }
        const float ratioD = 1.0f + down() * 60.0f, ratioU = 1.0f + up() * 3.0f, maxUp = 6.0f + up() * 24.0f, thrU = -42.0f;
        depthSm.target = depth(); inSm.target = dbToGain (inG()); outSm.target = dbToGain (outG());
        float lvl[3] { 0, 0, 0 };
        for (int i = 0; i < n; ++i)
        {
            const float dp = depthSm.next(), gi = inSm.next(), go = outSm.next();
            float band[3][2];
            const float in[2] { L[i] * gi, R[i] * gi };
            for (int c = 0; c < 2; ++c)
            {
                float lo, rest, mid, hi;
                x1.processSample (c, in[c], lo, rest);
                x2.processSample (c, rest, mid, hi);
                band[0][c] = ap2.processSample (c, lo); band[1][c] = mid; band[2][c] = hi;
            }
            float o[2] { 0, 0 };
            for (int b = 0; b < 3; ++b)
            {
                const float pk = std::max (std::abs (band[b][0]), std::abs (band[b][1]));
                if (prime > 0) env[b] = std::max (pk, b == 0 && prime == 96 ? 0.0f : env[b]);
                else env[b] += (pk > env[b] ? at[b] : rt[b]) * (pk - env[b]);
                const float ld = gainToDb (env[b] + 1.0e-7f);
                float g = 0;
                if (ld > thrD[b]) g -= (ld - thrD[b]) * (1.0f - 1.0f / ratioD);
                if (ld < thrU) g += std::min (maxUp, (thrU - ld) * (1.0f - 1.0f / ratioU)) * clampf ((ld + 78.0f) / 18.0f, 0.0f, 1.0f);
                gdb[b] = g;
                const float lin = dbToGain (g * dp);
                o[0] += band[b][0] * lin; o[1] += band[b][1] * lin;
                lvl[b] = std::max (lvl[b], ld);
            }
            L[i] = o[0] * go; R[i] = o[1] * go;
            if (prime > 0) --prime;
        }
        if (tm) for (int b = 0; b < 3; ++b) { tm->ottLevel[(size_t) b].store (lvl[b]); tm->ottGain[(size_t) b].store (gdb[b] * depthSm.cur); }
    }
private:
    P depth, time, up, down, inG, outG; double sr = 48000;
    juce::dsp::LinkwitzRileyFilter<float> x1, x2, ap2;
    float env[3] {}, gdb[3] {}; int prime = 0; ExpSmooth depthSm, inSm, outSm;
};

// ================================================================ GHOST LIMITER
// Sliding-minimum gain over the lookahead window, release smoothing that never rises above it, then a box average of the
// same length: the gain is guaranteed to be at or below the needed gain when each peak leaves the delay line -> no overs.
class LimiterUnit : public UnitDsp
{
public:
    Telemetry* tm = nullptr;
    bool selfMix() const override { return true; }
    void attach (juce::AudioProcessorValueTreeState& s) override
    {
        ceiling.bind (s, pid (uLim, "ceiling"), -0.3f); gain.bind (s, pid (uLim, "gain")); release.bind (s, pid (uLim, "release"), 80);
    }
    void prepare (double sr_, int) override
    {
        sr = sr_; W = std::max (2, (int) std::lround (sr * 0.0015)) + 1;
        for (auto* r : { &dl, &dr }) r->prepare (W + 8);
        dry.prepare (W - 1);
        int cap = 16; while (cap < W + 4) cap <<= 1; qMask = cap - 1;
        qv.assign ((size_t) cap, 1.0f); qi.assign ((size_t) cap, 0); box.assign ((size_t) cap, 1.0f);
        gainSm.setTime (sr, 20.0f); ceilSm.setTime (sr, 20.0f);
        reset();
    }
    void reset() override
    {
        dl.clear(); dr.clear(); head = tail = 0; idx = 0; rel = 1.0f; sum = (double) W; bw = 0;
        std::fill (box.begin(), box.end(), 1.0f);
        gainSm.snap (dbToGain (gain())); ceilSm.snap (dbToGain (ceiling()));
    }
    int latency() const override { return W - 1; }
    void delayDry (float* L, float* R, int n) override { dry.process (L, R, n); }
    void clearLines() override { dry.clear(); }
    void process (float* L, float* R, int n, const BlockCtx&) override
    {
        gainSm.target = dbToGain (gain()); ceilSm.target = dbToGain (std::min (0.0f, ceiling()));
        const float rc = 1.0f - (float) std::exp (-1.0 / (std::max (1.0f, release()) * 0.001 * sr));
        float minG = 1.0f, pk = 0;
        for (int i = 0; i < n; ++i)
        {
            const float gi = gainSm.next(), cl = ceilSm.next();
            const float xl = L[i] * gi, xr = R[i] * gi;
            dl.push (xl); dr.push (xr);
            const float a = std::max (std::abs (xl), std::abs (xr));
            const float need = a > cl ? cl / a : 1.0f;
            // sliding minimum over the last W samples (monotonic queue)
            while (head != tail && qv[(size_t) ((tail - 1) & qMask)] >= need) tail = (tail - 1) & qMask;
            qv[(size_t) tail] = need; qi[(size_t) tail] = idx; tail = (tail + 1) & qMask;
            while (qi[(size_t) head] <= idx - W) head = (head + 1) & qMask;
            const float m = qv[(size_t) head];
            rel = std::min (m, rel + rc * (1.0f - rel));
            sum += (double) rel - (double) box[(size_t) bw]; box[(size_t) bw] = rel; bw = (bw + 1) % W;
            const float g = (float) (sum / (double) W);
            ++idx;
            float ol = dl.at (W - 1) * g, orr = dr.at (W - 1) * g;
            ol = clampf (ol, -cl, cl); orr = clampf (orr, -cl, cl);   // numerical safety only
            L[i] = ol; R[i] = orr;
            minG = std::min (minG, g); pk = std::max (pk, std::max (std::abs (ol), std::abs (orr)));
        }
        if (tm) tm->limGr.store (gainToDb (minG));
        if ((++resync & 1023) == 0) { double s = 0; for (int k = 0; k < W; ++k) s += box[(size_t) k]; sum = s; }
    }
private:
    P ceiling, gain, release; double sr = 48000; int W = 72;
    Ring dl, dr; FixedDelay dry;
    std::vector<float> qv, box; std::vector<int64_t> qi; int qMask = 15, head = 0, tail = 0, bw = 0; int64_t idx = 0; unsigned resync = 0;
    float rel = 1.0f; double sum = 0; ExpSmooth gainSm, ceilSm;
};

} // namespace gpfx
