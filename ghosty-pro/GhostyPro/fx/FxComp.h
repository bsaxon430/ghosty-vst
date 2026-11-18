#pragma once
// COMPRESSOR (Ghosty Pro, FX RACK round 3, 2026-10-25). Original DSP (no third-party code).
//   SINGLE: one feed-forward compressor. Peak detector -> soft-knee gain computer (THRESH, RATIO, KNEE) -> gain smoothed in dB
//           with ATTACK / RELEASE -> MAKEUP. KEY: the unit's own input, or the RACK INPUT (the dry synth before any effect, aligned
//           to this unit's place in the chain): the effects after it duck to the playing, not to their own tails.
//   MULTIBAND: 3 bands (Linkwitz-Riley 4th-order crossovers LOW / HIGH, the low band phase-matched, so the bands sum flat), each
//           with its own threshold offset, DOWN amount (downward compression) and UP amount (upward compression: quiet parts
//           are lifted towards the threshold, up to 18 dB, never the noise floor), band level.
//   RATIO at its maximum = LIMIT: a true-peak limiter (4x interpolated peak detector, sliding-minimum gain over the ATTACK window
//           0 .. 10 ms, release smoothing that never rises above it, box-averaged over the same window), MAKEUP 0 .. 36 dB drives
//           into the ceiling = THRESH. Guaranteed no overs at the unit's output.
//   Whenever the unit is on it runs 10 ms behind (the limiter's look-ahead) and reports that latency; the rack delays the dry to
//   match. Turning it on / off crossfades the delay (no step). Every control is smoothed per sample; switching SINGLE / MULTI or
//   into / out of LIMIT crossfades the two paths over 20 ms.
#include "FxProCommon.h"

#if ! GPFX_BOX
namespace gpfx
{
inline const juce::StringArray& compModeNames() { static juce::StringArray s { "SINGLE", "MULTIBAND" }; return s; }
constexpr float kCompRatioMax = 20.0f;   // the top of RATIO = LIMIT

class CompUnit : public UnitDsp
{
public:
    Telemetry* tm = nullptr;
    const std::array<Ring, 2>* rackIn = nullptr;   // the chain input (Rack::Impl::xfRing): KEY = RACK INPUT
    int keyLatency = 0;                            // set by the rack before process(): the latency of every unit before this one
    std::atomic<float>* hostOn = nullptr;          // fx_comp_on: the latency is there while the unit is on
    void attach (juce::AudioProcessorValueTreeState& s) override
    {
        mode.bind (s, pid (uComp, "mode"), 0); thresh.bind (s, pid (uComp, "thresh"), -18.0f); ratio.bind (s, pid (uComp, "ratio"), 4.0f);
        attack.bind (s, pid (uComp, "attack"), 10.0f); release.bind (s, pid (uComp, "release"), 150.0f); makeup.bind (s, pid (uComp, "makeup"), 0.0f);
        knee.bind (s, pid (uComp, "knee"), 3.0f); key.bind (s, pid (uComp, "key"), 0); xlo.bind (s, pid (uComp, "xlo"), 150.0f); xhi.bind (s, pid (uComp, "xhi"), 2500.0f);
        for (int b = 0; b < 3; ++b)
        {
            const juce::String k = "b" + juce::String (b + 1) + "_";
            bThr[(size_t) b].bind (s, pid (uComp, (k + "thr").toRawUTF8()), 0.0f); bDown[(size_t) b].bind (s, pid (uComp, (k + "down").toRawUTF8()), 1.0f);
            bUp[(size_t) b].bind (s, pid (uComp, (k + "up").toRawUTF8()), 0.0f); bGain[(size_t) b].bind (s, pid (uComp, (k + "gain").toRawUTF8()), 0.0f);
        }
        hostOn = s.getRawParameterValue (pid (uComp, "on"));
    }
    void prepare (double sr_, int) override
    {
        sr = sr_; D = (int) std::lround (0.010 * sr) + 12;   // 10 ms look-ahead (+ the interpolator's centre and a margin)
        for (auto& r : xd) r.prepare (D + kSub + 16);
        for (auto& r : kd) r.prepare (D + kSub + 16);
        gRing.prepare (D + 16); relRing.prepare (D + 16);
        int cap = 16; while (cap < D + 8) cap <<= 1; qMask = cap - 1; qv.assign ((size_t) cap, 1.0f); qi.assign ((size_t) cap, 0);
        dryDelay.prepare (D, sr); dryDelay.cur = dryDelay.old = latency();   // (on at load: no fade-in of the delay)
        for (auto* e : { &thrSm, &mkSm, &kneeSm, &ratSm }) e->setTime (sr, 20.0f);
        for (auto& e : bThrSm) e.setTime (sr, 20.0f);
        for (auto& e : bDownSm) e.setTime (sr, 20.0f);
        for (auto& e : bUpSm) e.setTime (sr, 20.0f);
        for (auto& e : bGainSm) e.setTime (sr, 20.0f);
        xloSm.setTime (sr, 30.0f); xhiSm.setTime (sr, 30.0f);
        // 4x true-peak interpolator: 3 fractional phases, 16-tap windowed sinc each (centred between h[7] and h[8])
        for (int ph = 1; ph < 4; ++ph)
            for (int j = 0; j < kTp; ++j)
            {
                const double t = (double) (j - 7) - ph / 4.0, x = juce::MathConstants<double>::pi * t;
                const double w = 0.5 + 0.5 * std::cos (juce::MathConstants<double>::pi * t / 8.5);
                tpK[(size_t) ph - 1][(size_t) j] = (float) ((std::abs (t) < 1e-9 ? 1.0 : std::sin (x) / x) * w);
            }
        reset();
    }
    void reset() override
    {
        // (xd / kd keep flowing: idle() feeds them while the unit is off, so switching on starts with the last 10 ms in place)
        gRing.clear(); relRing.clear(); head = tail = 0; idx = 0; rel = 1.0f; boxSum = 0; boxLen = -1;
        for (int i = 0; i < D + 8; ++i) { gRing.push (1.0f); relRing.push (1.0f); }
        env = 0; gDb = 0; for (int b = 0; b < 3; ++b) { bEnv[(size_t) b] = 0; bGdb[(size_t) b] = 0; }
        for (auto& x : xLo) x.reset();
        for (auto& x : xHi) x.reset();
        for (auto& x : apHi) x.reset();
        thrSm.snap (thresh()); mkSm.snap (makeup()); kneeSm.snap (knee()); ratSm.snap (clampRatio());
        for (int b = 0; b < 3; ++b) { bThrSm[(size_t) b].snap (bThr[(size_t) b]()); bDownSm[(size_t) b].snap (bDown[(size_t) b]()); bUpSm[(size_t) b].snap (bUp[(size_t) b]()); bGainSm[(size_t) b].snap (bGain[(size_t) b]()); }
        xloSm.snap (std::log2 (xlo())); xhiSm.snap (std::log2 (xhi()));
        curPath = pathNow(); prevPath = curPath; fade = 0;
        for (auto& h : tpHist) h.fill (0.0f);
    }
    // the latency is there while the unit is on (a crossfading delay hides the switch)
    int latency() const override { return hostOn == nullptr || hostOn->load (std::memory_order_relaxed) > 0.5f ? D : 0; }
    void delayDry (float* L, float* R, int n) override { dryDelay.process (L, R, n, latency()); }
    void clearLines() override { dryDelay.clear(); for (auto& r : xd) r.clear(); for (auto& r : kd) r.clear(); }
    void idle (const float* L, const float* R, int n) override { for (int i = 0; i < n; ++i) { xd[0].push (L[i]); xd[1].push (R[i]); kd[0].push (L[i]); kd[1].push (R[i]); } }
    void process (float* L, float* R, int n, const BlockCtx&) override
    {
        const int p = pathNow();
        if (p != curPath) { prevPath = curPath; curPath = p; fade = fadeLen = (int) (0.02 * sr); }
        thrSm.target = juce::jlimit (-60.0f, 0.0f, thresh()); mkSm.target = juce::jlimit (0.0f, 36.0f, makeup()); kneeSm.target = juce::jlimit (0.0f, 24.0f, knee());
        ratSm.target = clampRatio();
        for (int b = 0; b < 3; ++b)
        {
            bThrSm[(size_t) b].target = bThr[(size_t) b](); bDownSm[(size_t) b].target = clampf (bDown[(size_t) b](), 0.0f, 1.0f);
            bUpSm[(size_t) b].target = clampf (bUp[(size_t) b](), 0.0f, 1.0f); bGainSm[(size_t) b].target = bGain[(size_t) b]();
        }
        xloSm.target = std::log2 (juce::jlimit (30.0f, 2000.0f, xlo())); xhiSm.target = std::log2 (juce::jlimit (500.0f, 16000.0f, xhi()));
        const float atMs = juce::jlimit (0.0f, 100.0f, attack()), reMs = juce::jlimit (5.0f, 2000.0f, release());
        const float aC = atMs <= 0.01f ? 1.0f : 1.0f - (float) std::exp (-1.0 / (atMs * 0.001 * sr)), rC = 1.0f - (float) std::exp (-1.0 / (reMs * 0.001 * sr));
        const float pkRel = (float) std::exp (-1.0 / (std::max (20.0f, reMs) * 0.001 * sr));                      // the peak detector's own fall
        const bool rackKey = key.i() == 1 && rackIn != nullptr;
        const int A = juce::jlimit (1, D - 8, (int) std::lround (juce::jlimit (0.5f, 10.0f, atMs) * 0.001 * sr) + 1);    // LIMIT's window (at least 0.5 ms: a gain that jumps sample by sample would make its own inter-sample peaks)
        const float limRel = 1.0f - (float) std::exp (-1.0 / (std::max (1.0f, reMs) * 0.001 * sr));
        if (A != boxLen) { boxLen = A; boxSum = 0; for (int k = 0; k < A; ++k) boxSum += relRing.at (k); }
        float grPk = 0, bandGrPk[3] { 0, 0, 0 };
        for (int i = 0; i < n; ++i)
        {
            const float inL = L[i], inR = R[i];
            // the key, undelayed (the limiter looks ahead on it); KEY = RACK INPUT reads the chain input at this unit's place
            float kL = inL, kR = inR;
            if (rackKey) { const int back = n - 1 - i + keyLatency; kL = (*rackIn)[0].at (back); kR = (*rackIn)[1].at (back); }
            xd[0].push (inL); xd[1].push (inR); kd[0].push (kL); kd[1].push (kR);
            const float xL = xd[0].at (D), xR = xd[1].at (D);           // the audio, 10 ms behind
            const float keyL = kd[0].at (D), keyR = kd[1].at (D);       // the key at the same moment (no look-ahead for the compressor)
            const float thr = thrSm.next(), mk = mkSm.next(), kn = kneeSm.next(), rat = ratSm.next();
            float bt[3], bdn[3], bup[3], bg[3];
            for (int b = 0; b < 3; ++b) { bt[b] = bThrSm[(size_t) b].next(); bdn[b] = bDownSm[(size_t) b].next(); bup[b] = bUpSm[(size_t) b].next(); bg[b] = bGainSm[(size_t) b].next(); }
            const float flo = std::exp2 (xloSm.next()), fhi = std::exp2 (xhiSm.next());
            if ((i & 3) == 0) { for (auto& x : xLo) x.set (sr, flo); for (auto& x : xHi) x.set (sr, fhi); for (auto& x : apHi) x.set (sr, fhi); }
            // the limiter's gain (always computed, so switching into LIMIT finds it settled)
            const float lg = limiterGain (kL, kR, thr, A, limRel);
            float oL = 0, oR = 0, wPrev = 0;
            if (fade > 0) { wPrev = (float) fade / (float) fadeLen; wPrev = 0.5f - 0.5f * std::cos (kPi * wPrev); --fade; }
            for (int pass = 0; pass < (fade > 0 || wPrev > 0 ? 2 : 1); ++pass)
            {
                const int path = pass == 0 ? curPath : prevPath;
                float pl = 0, pr = 0;
                if (path == 2) { const float g = lg * dbToGain (mk); pl = clampf (xL * g, -limCeil, limCeil); pr = clampf (xR * g, -limCeil, limCeil); }
                else if (path == 0)
                {
                    const float g = singleGain (std::max (std::abs (keyL), std::abs (keyR)), thr, kn, rat, aC, rC, pkRel, pass == 0);
                    const float lin = dbToGain (g + mk); pl = xL * lin; pr = xR * lin;
                    if (pass == 0) grPk = std::min (grPk, g);
                }
                else
                {   // three bands; the key splits the same way (its own filters)
                    float band[3][2], kb[3][2];
                    splitBands (xL, xR, band, 0); splitBands (keyL, keyR, kb, 1);
                    for (int b = 0; b < 3; ++b)
                    {
                        const float g = bandGain (b, std::max (std::abs (kb[b][0]), std::abs (kb[b][1])), thr + bt[b], kn, rat, bdn[b], bup[b], aC, rC, pkRel, pass == 0);
                        const float lin = dbToGain (g + bg[b] + mk);
                        pl += band[b][0] * lin; pr += band[b][1] * lin;
                        if (pass == 0) bandGrPk[b] = std::abs (g) > std::abs (bandGrPk[b]) ? g : bandGrPk[b];
                    }
                }
                if (pass == 0) { oL = pl; oR = pr; }
                else { oL += wPrev * (pl - oL); oR += wPrev * (pr - oR); }
            }
            if (curPath == 2) grPk = std::min (grPk, gainToDb (lg));
            L[i] = oL; R[i] = oR;
        }
        if (tm != nullptr)
        {
            tm->compGr.store (grPk, std::memory_order_relaxed);
            for (int b = 0; b < 3; ++b) tm->compBandGr[(size_t) b].store (bandGrPk[b], std::memory_order_relaxed);
            tm->compPath.store (curPath, std::memory_order_relaxed);
        }
    }
    // static curve of the compressor (dB out - dB in) at input level x dB: for the editor and the tests
    static float curve (float xDb, float thr, float kneeDb, float rat)
    {
        const float over = xDb - thr, slope = 1.0f / rat - 1.0f;
        if (kneeDb > 0.0f && 2.0f * std::abs (over) <= kneeDb) return slope * (over + 0.5f * kneeDb) * (over + 0.5f * kneeDb) / (2.0f * kneeDb);
        return over > 0.0f ? slope * over : 0.0f;
    }
    float limCeil = 1.0f;
private:
    int pathNow() const { return ratio() >= kCompRatioMax - 0.01f ? 2 : (mode.i() == 1 ? 1 : 0); }   // 0 single, 1 multiband, 2 limit
    float clampRatio() const { return juce::jlimit (1.0f, kCompRatioMax, ratio()); }
    // SINGLE: peak detector -> gain computer -> attack / release on the gain (dB)
    float singleGain (float k, float thr, float kn, float rat, float aC, float rC, float pkRel, bool advance)
    {
        float e = std::max (k, env * pkRel);
        const float target = curve (gainToDb (e + 1.0e-9f), thr, kn, std::min (rat, kCompRatioMax - 0.01f));
        float g = gDb + (target < gDb ? aC : rC) * (target - gDb);
        if (advance) { env = e; gDb = g; }
        return g;
    }
    float bandGain (int b, float k, float thr, float kn, float rat, float down, float up, float aC, float rC, float pkRel, bool advance)
    {
        float e = std::max (k, bEnv[(size_t) b] * pkRel);
        const float xDb = gainToDb (e + 1.0e-9f);
        float target = down * curve (xDb, thr, kn, std::min (rat, kCompRatioMax - 0.01f));
        if (up > 0.0f && xDb < thr)
        {   // upward: lift the quiet parts towards the threshold (by the same ratio), at most 18 dB, fading out below -66 dBFS
            const float lift = std::min (18.0f * up, (thr - xDb) * (1.0f - 1.0f / rat) * up);
            target += lift * clampf ((xDb + 78.0f) / 12.0f, 0.0f, 1.0f);
        }
        float g = bGdb[(size_t) b];
        g += ((target < g) ? aC : rC) * (target - g);
        if (advance) { bEnv[(size_t) b] = e; bGdb[(size_t) b] = g; }
        return g;
    }
    void splitBands (float l, float r, float band[3][2], int set) noexcept
    {
        const float in[2] { l, r };
        for (int c = 0; c < 2; ++c)
        {
            float lo, rest, mid, hi;
            xLo[(size_t) (set * 2 + c)].split (in[c], lo, rest);
            xHi[(size_t) (set * 2 + c)].split (rest, mid, hi);
            band[0][c] = apHi[(size_t) (set * 2 + c)].process (lo); band[1][c] = mid; band[2][c] = hi;
        }
    }
    // LIMIT: gain for the sample leaving the 10 ms delay. Needed gain from the 4x true peak of the (undelayed) key, sliding minimum
    // over A samples, release that never rises above it, box average over A: at or below the needed gain when each peak leaves.
    float limiterGain (float kL, float kR, float thrDb, int A, float limRel) noexcept
    {
        limCeil = dbToGain (thrDb);
        for (int c = 0; c < 2; ++c) { auto& h = tpHist[(size_t) c]; for (int j = 0; j < kTp - 1; ++j) h[(size_t) j] = h[(size_t) j + 1]; h[kTp - 1] = c == 0 ? kL : kR; }
        // the peak around the sample 8 back (h[7]: the interpolator is centred there): the sample itself and 3 points between it and the next
        float tp = 0;
        for (int c = 0; c < 2; ++c)
        {
            const auto& h = tpHist[(size_t) c];
            tp = std::max (tp, std::abs (h[7]));
            for (int ph = 0; ph < 3; ++ph) { float acc = 0; for (int j = 0; j < kTp; ++j) acc += h[(size_t) j] * tpK[(size_t) ph][(size_t) j]; tp = std::max (tp, std::abs (acc)); }
        }
        tp *= 1.0233f;                                                   // + 0.2 dB margin
        const float need = tp * dbToGain (mkSm.cur) > limCeil ? limCeil / (tp * dbToGain (mkSm.cur)) : 1.0f;
        while (head != tail && qv[(size_t) ((tail - 1) & qMask)] >= need) tail = (tail - 1) & qMask;
        qv[(size_t) tail] = need; qi[(size_t) tail] = idx; tail = (tail + 1) & qMask;
        while (qi[(size_t) head] <= idx - A) head = (head + 1) & qMask;
        const float m = qv[(size_t) head];
        rel = std::min (m, rel + limRel * (1.0f - rel));
        boxSum += (double) rel - (double) relRing.at (A - 1); relRing.push (rel);
        const float g = (float) (boxSum / (double) A);
        gRing.push (g); ++idx;
        // g (now) <= the need of the key sample A - 1 + 8 back (h[7] is 8 back); the audio leaving the delay is D back:
        // read the g computed D - A - 7 samples ago
        return gRing.at (juce::jlimit (0, gRing.size() - 8, D - A - 7));
    }
    P mode, thresh, ratio, attack, release, makeup, knee, key, xlo, xhi;
    std::array<P, 3> bThr, bDown, bUp, bGain;
    double sr = 48000; int D = 480;
    std::array<Ring, 2> xd, kd; Ring gRing, relRing;
    std::vector<float> qv; std::vector<int64_t> qi; int qMask = 15, head = 0, tail = 0; int64_t idx = 0; float rel = 1.0f; double boxSum = 0; int boxLen = -1;
    SwitchDelay dryDelay;
    ExpSmooth thrSm, mkSm, kneeSm, ratSm, xloSm, xhiSm; std::array<ExpSmooth, 3> bThrSm, bDownSm, bUpSm, bGainSm;
    float env = 0, gDb = 0; std::array<float, 3> bEnv {}, bGdb {};
    std::array<Lr4, 4> xLo, xHi; std::array<Lr4Allpass, 4> apHi;   // [set * 2 + channel], set 0 audio, 1 key
    static constexpr int kTp = 16; std::array<std::array<float, kTp>, 3> tpK {}; std::array<std::array<float, kTp>, 2> tpHist {};
    int curPath = 0, prevPath = 0, fade = 0, fadeLen = 1;
};

inline void addCompParameters (juce::AudioProcessorValueTreeState::ParameterLayout& l)
{
    auto T = [] (const char* s) { return juce::String ("FX ") + unitInfo (uComp).title + " " + s; };
    addChoice (l, pid (uComp, "mode"), T ("Mode"), compModeNames(), 0);
    addFloat (l, pid (uComp, "thresh"), T ("Threshold"), juce::NormalisableRange<float> (-60.0f, 0.0f), -18.0f, "dB", 1);
    {
        auto attrs = juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return v >= kCompRatioMax - 0.01f ? juce::String ("LIMIT") : juce::String (v, v < 10.0f ? 1 : 0) + " : 1"; })
            .withValueFromStringFunction ([] (const juce::String& t) { return t.trim().toLowerCase().startsWith ("lim") ? kCompRatioMax : (float) t.getDoubleValue(); });
        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { pid (uComp, "ratio"), 1 }, T ("Ratio"), skewRange (1.0f, kCompRatioMax, 4.0f), 4.0f, attrs));
    }
    addFloat (l, pid (uComp, "attack"), T ("Attack"), skewRange (0.0f, 100.0f, 10.0f), 10.0f, "ms", 1);
    addFloat (l, pid (uComp, "release"), T ("Release"), skewRange (5.0f, 2000.0f, 150.0f), 150.0f, "ms", 0);
    addFloat (l, pid (uComp, "makeup"), T ("Makeup"), juce::NormalisableRange<float> (0.0f, 36.0f), 0.0f, "dB", 1);
    addFloat (l, pid (uComp, "knee"), T ("Knee"), juce::NormalisableRange<float> (0.0f, 24.0f), 3.0f, "dB", 1);
    addChoice (l, pid (uComp, "key"), T ("Key"), { "UNIT INPUT", "RACK INPUT" }, 0);
    addFloat (l, pid (uComp, "xlo"), T ("Low Crossover"), skewRange (30.0f, 2000.0f, 200.0f), 150.0f, "Hz", 0);
    addFloat (l, pid (uComp, "xhi"), T ("High Crossover"), skewRange (500.0f, 16000.0f, 3000.0f), 2500.0f, "Hz", 0);
    static const char* bn[] { "Low", "Mid", "High" };
    for (int b = 0; b < 3; ++b)
    {
        const juce::String k = "b" + juce::String (b + 1) + "_";
        addFloat (l, pid (uComp, (k + "thr").toRawUTF8()), T (bn[b]) + " Threshold", juce::NormalisableRange<float> (-24.0f, 24.0f), 0.0f, "dB", 1);
        addPct (l, pid (uComp, (k + "down").toRawUTF8()), T (bn[b]) + " Down", 1.0f);
        addPct (l, pid (uComp, (k + "up").toRawUTF8()), T (bn[b]) + " Up", 0.0f);
        addFloat (l, pid (uComp, (k + "gain").toRawUTF8()), T (bn[b]) + " Level", juce::NormalisableRange<float> (-12.0f, 12.0f), 0.0f, "dB", 1);
    }
}
} // namespace gpfx
#endif // ! GPFX_BOX
