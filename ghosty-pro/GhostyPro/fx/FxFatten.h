#pragma once
// FATTEN (one-knob fattener, 8 modes) and the Tilt EQ it uses. Built in BOTH racks: Ghost Box (where it always was) and
// Ghosty Pro (added 2026-10-23 as the last unit before the limiter; off by default, its parameters appended at the very end
// of Ghosty Pro's layout so no existing parameter index, preset or saved state moves).
// Original design. Audio-thread rule as everywhere in the rack: no allocation, no locks inside process().
#include "FxCore.h"
#include "FxColour.h"

namespace gpfx {

// Tilt EQ around ~900 Hz: t -1 dark .. +1 bright
struct Tilt
{
    OnePole lp; float gl = 1, gh = 1;
    void prepare (double sr, double hz = 900.0) { lp.setLP (sr, hz); }
    void set (float t) { gh = t < 0 ? dbToGain (t * 12.0f) : dbToGain (t * 7.0f); gl = t < 0 ? dbToGain (-t * 2.0f) : dbToGain (-t * 3.0f); }
    float process (float x) noexcept { const float lo = lp.lp (x); return lo * gl + (x - lo) * gh; }
    void reset() { lp.reset(); }
};

// ================================================================ FATTEN
// MODE: CLASSIC = the original unit (bit-identical; low shelf + glue + drive, level matched to the input).
// The other modes run a second engine built around what makes a sound feel fat at the same peak level: more sustained
// body / loudness density (lower crest factor), a longer sub tail, harmonic density, a mono low end. FAT is staged:
// 0-30 % glue + weight, 30-70 % saturation (even + odd), 70-100 % heavy clip / fold; the level trim lets SAUSAGE come out
// up to ~+6 dB louder and a soft ceiling keeps every mode under 0 dBFS. Modes cross-fade (block weights glide), no clicks.
inline const juce::StringArray& fattenModeNames() { static juce::StringArray s { "CLASSIC", "SUB", "BODY", "SMASH", "SATURATE", "STACK", "TRANSIENT", "SAUSAGE" }; return s; }
enum FattenMode { fmClassic, fmSub, fmBody, fmSmash, fmSaturate, fmStack, fmTransient, fmSausage, kNumFattenModes };
// one line per mode, in plain words (UI description line + tooltips)
inline const char* fattenModeBlurb (int m)
{
    static const char* b[] { "the original FATTEN: low shelf, glue and drive, same loudness as the input",
                             "an octave-down sub layer + a squeezed low end: deep, long bass",
                             "a 140 Hz thump + a held low end: chest-hitting body",
                             "parallel heavy compression + clip: dense, loud, in your face",
                             "tape / tube drive (even + odd harmonics): warm and thick",
                             "two micro-delayed copies: wide and thick",
                             "softer attack, longer tail: round and full",
                             "everything at once: glue, sub, body, drive, clip - the maximum fat" };
    return b[juce::jlimit (0, (int) kNumFattenModes - 1, m)];
}
class FattenUnit : public UnitDsp
{
public:
    Telemetry* tm = nullptr;
    void attach (juce::AudioProcessorValueTreeState& s) override
    {
        fat.bind (s, pid (uFatten, "fat"), 0.4f); color.bind (s, pid (uFatten, "color"), 0); crunch.bind (s, pid (uFatten, "crunch"), 0.2f); out.bind (s, pid (uFatten, "out"), 0);
        if (s.getParameter (pid (uFatten, "mode")) != nullptr) mode.bind (s, pid (uFatten, "mode"), 0);
        if (s.getParameter (pid (uFatten, "drive")) != nullptr) drive.bind (s, pid (uFatten, "drive"), 0);
        if (s.getParameter (pid (uFatten, "punch")) != nullptr) punch.bind (s, pid (uFatten, "punch"), 0);
        if (s.getParameter (pid (uFatten, "lowclean")) != nullptr) lowclean.bind (s, pid (uFatten, "lowclean"), 0);
    }
    void prepare (double sr_, int) override
    {
        sr = sr_; fatSm.setTime (sr, 60.0f); crSm.setTime (sr, 60.0f); outSm.setTime (sr, 30.0f);
        det.set (sr, 8.0f, 140.0f); ag.prepare (sr, 350.0f);
        for (auto& t : tilt) t.prepare (sr, 1000.0);
        for (auto& d : dc) d.prepare (sr);
        modeSm.setTime (sr, 120.0f); drvSm.setTime (sr, 40.0f); punchSm.setTime (sr, 30.0f); lcSm.setTime (sr, 30.0f);
        for (auto& w : wSm) w.setTime (sr, 80.0f);
        // the new engine
        lp120 = rbj (rbjLP, sr, 120.0, 0.7071); lp150 = rbj (rbjLP, sr, 150.0, 0.7071); lp90 = rbj (rbjLP, sr, 90.0, 0.7071); hp90 = rbj (rbjHP, sr, 90.0, 0.7071);
        bodyC = rbj (rbjBell, sr, 140.0, 1.1, 9.0); bodyShC = rbj (rbjLowShelf, sr, 90.0, 0.7, 4.0);
        subLp1.setLP (sr, 120.0); subLp2.setLP (sr, 120.0); subOutLp1.setLP (sr, 90.0); subOutLp2.setLP (sr, 90.0); subEnv.set (sr, 3.0f, 90.0f);
        glueHpC = rbj (rbjHP, sr, 60.0, 0.6); satLvl.set (sr, 3.0f, 400.0f); subAct.setTime (sr, 15.0f); subHpC = rbj (rbjHP, sr, 30.0, 0.7071); lcEnv.set (sr, 1.0f, 220.0f); det2.set (sr, 3.0f, 160.0f); smashEnv.set (sr, 0.5f, 60.0f); trFast.set (sr, 0.3f, 25.0f); trSlow.set (sr, 15.0f, 250.0f);
        pFast.set (sr, 0.2f, 30.0f); pSlow.set (sr, 12.0f, 300.0f); pIn.set (sr, 5.0f, 250.0f); pOut.set (sr, 5.0f, 250.0f);
        agN.prepare (sr, 200.0f); fatN.setTime (sr, 120.0f);
        for (auto& r : stackRing) r.prepare ((int) (sr * 0.03) + 16);
        for (auto& d : dcN) d.prepare (sr);
        reset();
    }
    void reset() override
    {
        fatSm.snap (fat()); crSm.snap (crunch()); outSm.snap (dbToGain (out())); det.reset(); ag.reset();
        for (auto& b : lowSh) b.reset();
        for (auto& b : midB) b.reset();
        for (auto& t : tilt) t.reset();
        for (auto& d : dc) d.reset();
        lastF = -1;
        const int md = juce::jlimit (0, (int) kNumFattenModes - 1, mode.i());
        modeSm.snap (md == fmClassic ? 0.0f : 1.0f); drvSm.snap (drive()); punchSm.snap (punch.b() ? 1.0f : 0.0f); lcSm.snap (lowclean.b() ? 1.0f : 0.0f);
        lastMode = md; setWeights (md, true);
        resetNew();
    }
    void process (float* L, float* R, int n, const BlockCtx&) override
    {
        fatSm.target = clampf (fat(), 0.0f, 1.0f); crSm.target = clampf (crunch(), 0.0f, 1.0f); outSm.target = dbToGain (out());
        if (std::abs (fatSm.cur - lastF) > 0.002f)
        {
            lastF = fatSm.cur;
            lowC = rbj (rbjLowShelf, sr, 110.0, 0.7, lastF * 7.0);
            midC = rbj (rbjBell, sr, 320.0, 0.8, lastF * 3.5);
        }
        for (auto& t : tilt) t.set (color());
        for (auto& t : tiltN) t.set (color());
        const int md = juce::jlimit (0, (int) kNumFattenModes - 1, mode.i());
        if (md != lastMode) { lastMode = md; if (md != fmClassic) setWeights (md, false); }   // CLASSIC keeps the old weights so the new engine just fades out
        modeSm.target = md == fmClassic ? 0.0f : 1.0f;
        drvSm.target = clampf (drive(), 0.0f, 24.0f); punchSm.target = punch.b() ? 1.0f : 0.0f; lcSm.target = lowclean.b() ? 1.0f : 0.0f;
        const bool runNew = md != fmClassic || modeSm.cur > 1.0e-5f;   // the mode engine is heard (it always runs, so it is warm when a mode fades in)
        const bool extras = drvSm.target > 0.0f || drvSm.cur > 1.0e-4f || punchSm.target > 0.0f || punchSm.cur > 1.0e-5f || lcSm.target > 0.0f || lcSm.cur > 1.0e-5f;
        float pk = 0, grMax = 0, heat = 0;
        for (int i = 0; i < n; ++i)
        {
            if ((i & 31) == 0)
            {   // the mode engine's weight shelf (every 32 samples: no zipper when FAT moves fast)
                const float a = wSm[wShelf].cur * weightCurve (fatSm.cur);
                if (std::abs (a - lastShelf) > 0.002f) { lastShelf = a; nLowC = rbj (rbjLowShelf, sr, 100.0, 0.7, a * 8.0); nMidC = rbj (rbjBell, sr, 300.0, 0.8, a * 3.0); }
            }
            const float f = fatSm.next(), cr = crSm.next(), og = outSm.next();
            float l = L[i], r = R[i];
            if (! runNew && ! extras)
            {   // CLASSIC, untouched: exactly the original unit
                classic (l, r, l, r, f, cr, og, grMax);
                float nl = L[i], nr = R[i], gDummy = 0; engine (nl, nr, f, cr, og, gDummy);   // kept warm, not heard
                L[i] = l; R[i] = r;
                heat = std::max (heat, std::max (std::abs (l), std::abs (r)));
                pk = std::max (pk, std::abs (L[i]));
                continue;
            }
            // INPUT drive
            const float dv = drvSm.next();
            if (dv > 1.0e-4f) { const float gi = dbToGain (dv); l *= gi; r *= gi; }
            const float xin[2] { l, r };
            // LOW CLEAN: the sub (below ~90 Hz, mono) skips every drive / clip stage and comes back clean
            const float lc = lcSm.next();
            float sub = 0;
            if (lc > 1.0e-5f)
            {   // Linkwitz-Riley split at 90 Hz: low (mono) + high sum back flat in level
                sub = lcB[1].process (lcB[0].process (0.5f * (l + r), lp90), lp90);
                const float hl = lcH[1].process (lcH[0].process (l, hp90), hp90), hr = lcH[3].process (lcH[2].process (r, hp90), hp90);
                l += lc * (hl - l); r += lc * (hr - r);
            }
            const float cin[2] { l, r };
            // the engines
            float yl = l, yr = r;
            const float b = modeSm.next();
            classic (yl, yr, cin[0], cin[1], f, cr, og, grMax);   // (always run: warm for a fade back to CLASSIC)
            {
                float nl = cin[0], nr = cin[1], gDummy = 0;
                engine (nl, nr, f, cr, og, runNew ? grMax : gDummy);
                if (b >= 1.0f) { yl = nl; yr = nr; } else { yl += b * (nl - yl); yr += b * (nr - yr); }
            }
            if (lc > 1.0e-5f) { const float sg = lc * (1.0f + 0.3f * f) * og; yl += sg * sub; yr += sg * sub; }   // the clean sub, +2.3 dB at FAT 100 %
            // PUNCH: on attacks the (level-matched) unprocessed hit is blended back in, so the transient survives
            const float pa = punchSm.next();
            {
                const float mi = 0.5f * (std::abs (xin[0]) + std::abs (xin[1])) + 1.0e-9f, mo = 0.5f * (std::abs (yl) + std::abs (yr)) + 1.0e-9f;
                const float fe = pFast.process (mi), se = pSlow.process (mi), ei = pIn.process (mi), eo = pOut.process (mo);
                if (pa > 1.0e-5f)
                {
                    const float tr = clampf ((fe / (se + 1.0e-6f) - 1.1f) * 1.2f, 0.0f, 1.0f) * pa * 0.85f;
                    const float kd = clampf (eo / (ei + 1.0e-6f), 0.0f, 4.0f), room = 1.0f - 0.33f * pa;   // the fat body steps back ~3.5 dB: headroom for the hit
                    yl = yl * room + tr * xin[0] * kd; yr = yr * room + tr * xin[1] * kd;   // + the unprocessed hit, in parallel
                }
            }
            heat = std::max (heat, std::max (std::abs (yl), std::abs (yr)));
            L[i] = ceiling (yl); R[i] = ceiling (yr);   // never above 0 dBFS
            pk = std::max (pk, std::abs (L[i]));
        }
        if (tm)
        {
            tm->fatAmount.store (fatSm.cur); if (grMax < tm->fatGr.load()) tm->fatGr.store (grMax); tm->fatLevel.store (pk);   // GR held until the editor reads it
            if (heat > tm->fatHeat.load()) tm->fatHeat.store (heat);   // the level going into the ceiling (the clip LED), held until read
        }
    }
    // the soft ceiling: untouched below 0.8, rounds off to at most 0.98 (-0.18 dBFS)
    static float ceiling (float x) noexcept
    {
        const float a = std::abs (x);
        if (a <= 0.8f) return x;
        const float t = (a - 0.8f) * (1.0f / 0.18f), y = 0.8f + 0.18f * t / (1.0f + t);
        return x < 0 ? -y : y;
    }
private:
    // ---------------------------------------------------------------- CLASSIC (the original chain, unchanged arithmetic)
    void classic (float& l, float& r, float inL, float inR, float f, float cr, float og, float& grMax) noexcept
    {
        ag.feedIn (inL, inR);
        // low / low-mid thickening
        l = midB[0].process (lowSh[0].process (l, lowC), midC); r = midB[1].process (lowSh[1].process (r, lowC), midC);
        // glue compression (soft knee)
        const float e = det.process (0.5f * (std::abs (l) + std::abs (r)) + 1.0e-9f);
        const float lv = gainToDb (e), thr = -10.0f - 16.0f * f, ratio = 1.0f + 5.0f * f, knee = 6.0f;
        float over = lv - thr, gr = 0;
        if (over > knee * 0.5f) gr = over * (1.0f / ratio - 1.0f);
        else if (over > -knee * 0.5f) { const float x = over + knee * 0.5f; gr = (1.0f / ratio - 1.0f) * x * x / (2.0f * knee); }
        grMax = std::min (grMax, gr);
        const float cg = dbToGain (gr);
        l *= cg; r *= cg;
        // drive + saturation (smooth -> crunchy asymmetric edges)
        const float g = dbToGain (f * 18.0f + cr * 14.0f);
        for (int c = 0; c < 2; ++c)
        {
            float& x = c == 0 ? l : r;
            const float d = x * g;
            const float smooth = fastTanh (d);
            const float hard = d > 0 ? clampf (d * 1.25f, 0.0f, 0.92f) : -clampf (-d * 0.85f, 0.0f, 0.7f) + 0.06f * fastTanh (d * d);
            x = dc[c].process (smooth + cr * (hard - smooth));
            x = fastTanh (x * 1.1f) * 0.95f;   // final soft clip
            x = tilt[c].process (x);
        }
        const float a = ag.next (l, r) * og;
        l = l * a; r = r * a;
    }
    // ---------------------------------------------------------------- the mode engine
    enum W { wBody, wShelf, wLowComp, wSub, wGlue, wSat, wClip, wSmash, wStack, wTr, wLu, wStaged, kW };
    void setWeights (int md, bool snap)
    {   //                       BODY SHELF LCOMP SUB  GLUE  SAT  CLIP SMASH STACK TR    LU  STAGED
        static const float t[kNumFattenModes][kW] {
            { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },                                   // CLASSIC (not used)
            { 0.0f, 0.7f, 1.3f, 1.0f, 0.6f, 0.8f, 0.6f, 0.3f, 0, 0, 0.7f, 0 },         // SUB
            { 1.0f, 0.9f, 1.4f, 0.0f, 0.7f, 0.85f, 0.6f, 0.4f, 0, 0, 0.7f, 0 },        // BODY
            { 0.0f, 0.3f, 0.6f, 0.0f, 1.0f, 0.6f, 0.8f, 1.0f, 0, 0, 0.8f, 0 },         // SMASH
            { 0.0f, 0.4f, 0.5f, 0.0f, 0.5f, 1.0f, 0.65f, 0.0f, 0, 0, 0.6f, 0 },        // SATURATE
            { 0.0f, 0.4f, 0.8f, 0.0f, 0.6f, 0.7f, 0.5f, 0.0f, 1, 0, 0.6f, 0 },         // STACK
            { 0.0f, 0.4f, 1.0f, 0.0f, 0.6f, 0.6f, 0.4f, 0.0f, 0, 1, 0.6f, 0 },         // TRANSIENT
            { 0.7f, 1.0f, 1.3f, 0.7f, 1.0f, 1.0f, 1.0f, 0.7f, 0, 0, 0.9f, 1 } };       // SAUSAGE
        for (int k = 0; k < kW; ++k) { wSm[k].target = t[md][k]; if (snap) wSm[k].snap (t[md][k]); }
    }
    static float sstep (float x) noexcept { x = clampf (x, 0.0f, 1.0f); return x * x * (3.0f - 2.0f * x); }
    static float weightCurve (float f) noexcept { return 0.6f * sstep (f / 0.3f) + 0.4f * f; }   // glue + weight: mostly in the first 30 %
    void resetNew()
    {
        for (auto& b : monoB) b.reset();
        for (auto& b : lcompB) b.reset();
        for (auto& b : lcB) b.reset();
        for (auto& b : lcH) b.reset();
        for (auto& b : bodyB) b.reset();
        for (auto& b : bodySh) b.reset();
        for (auto& b : nLowSh) b.reset();
        for (auto& b : nMidB) b.reset();
        for (auto& d : dcN) d.reset();
        subLp1.reset(); subLp2.reset(); subOutLp1.reset(); subOutLp2.reset(); subEnv.reset(); subFlip = 1.0f; subArmed = true; subHp.reset(); subAct.snap (0.0f); sinceFlip = 1000000;
        glueHp.reset(); fatN.snap (fat()); satLvl.reset(); lcEnv.reset(); det2.reset(); smashEnv.reset(); trFast.reset(); trSlow.reset(); agN.reset();
        pFast.reset(); pSlow.reset(); pIn.reset(); pOut.reset();
        for (auto& r : stackRing) r.clear();
        stackPh = 0; lastShelf = -1;
        for (auto& t : tiltN) { t.prepare (sr, 1000.0); t.reset(); }
    }
    void engine (float& l, float& r, float f, float cr, float og, float& grMax) noexcept
    {
        float w[kW]; for (int k = 0; k < kW; ++k) w[k] = wSm[k].next();
        fatN.target = f; f = fatN.next();   // the engine glides a little slower than CLASSIC (its level match keeps up with FAT jumps)
        const float wa = weightCurve (f);
        const float st = w[wStaged];
        const float satC = st * sstep ((f - 0.3f) / 0.4f) + (1.0f - st) * f;            // saturation: 30-70 % when staged
        const float clipC = st * sstep ((f - 0.7f) / 0.3f) + (1.0f - st) * f * f;      // clip / fold: 70-100 % when staged
        agN.feedIn (l, r);
        // mono low end (below ~120 Hz both sides get the mid low band)
        {
            const float lo0 = monoB[1].process (monoB[0].process (l, lp120), lp120), lo1 = monoB[3].process (monoB[2].process (r, lp120), lp120);
            const float m = 0.5f * (lo0 + lo1); l += m - lo0; r += m - lo1;
        }
        const float mono = 0.5f * (l + r);
        // BODY: a resonant 140 Hz thump + low shelf
        if (const float a = w[wBody] * wa; a > 1.0e-5f)
            for (int c = 0; c < 2; ++c) { float& x = c == 0 ? l : r; const float y = bodySh[c].process (bodyB[c].process (x, bodyC), bodyShC); x += a * (y - x); }
        // weight shelf (100 Hz + 300 Hz)
        if (w[wShelf] > 1.0e-5f) { l = nMidB[0].process (nLowSh[0].process (l, nLowC), nMidC); r = nMidB[1].process (nLowSh[1].process (r, nLowC), nMidC); }
        // LOW-BAND PARALLEL COMPRESSOR: the band below ~150 Hz squeezed 10:1 (fast attack, slow release) and added back:
        // the low end of every hit is held up = longer, denser body at the same peak
        {
            const float lo = lcompB[1].process (lcompB[0].process (mono, lp150), lp150);
            const float e = lcEnv.process (lo) + 1.0e-9f, lv = gainToDb (e);
            if (const float a = w[wLowComp] * wa; a > 1.0e-5f)
            {
                const float thr = -34.0f, gr = lv > thr ? (lv - thr) * (1.0f / 10.0f - 1.0f) : 0.0f;
                const float gate = clampf ((lv + 62.0f) * 0.1f, 0.0f, 1.0f);   // no lift of the noise floor
                const float y = fastTanh (lo * dbToGain (gr + 22.0f) * 0.9f) * gate * a * 0.8f;
                l += y; r += y;
            }
        }
        // SUB: the low band driven into a soft clip and added back + the octave below (flip-flop on its zero crossings)
        {
            const float lo = subLp2.lp (subLp1.lp (mono));
            const float e = subEnv.process (lo);
            if (subArmed && lo > 0.1f * e) { subFlip = -subFlip; subArmed = false; sinceFlip = 0; } else if (lo < -0.1f * e) subArmed = true;
            if (sinceFlip < 1000000) ++sinceFlip;
            subAct.target = (float) sinceFlip < 0.04f * (float) sr ? 1.0f : 0.0f;   // no zero crossings for 40 ms (below ~12 Hz / silence): the layer fades out
            const float s = subHp.process (subOutLp2.lp (subOutLp1.lp (subFlip * e)) * subAct.next(), subHpC);   // (high-passed at 30 Hz: no rumble / DC)
            if (const float a = w[wSub] * wa; a > 1.0e-5f)
            {
                const float gate = clampf ((gainToDb (e + 1.0e-9f) + 60.0f) * (1.0f / 12.0f), 0.0f, 1.0f);   // the octave layer dies with its source
                const float add = fastTanh (lo * (1.0f + 10.0f * a)) * 0.6f * a + s * 1.8f * a * gate;
                l += add; r += add;
            }
        }
        // TRANSIENT: attack down, sustain up
        {
            const float m = std::abs (0.5f * (l + r)) + 1.0e-9f, fe = trFast.process (m), se = trSlow.process (m);
            if (const float a = w[wTr] * f; a > 1.0e-5f)
            {
                const float g = juce::jlimit (0.25f, 4.0f, std::pow ((se + 1.0e-6f) / (fe + 1.0e-6f), 1.5f * a));
                l *= g; r *= g;
            }
        }
        // STACK: two micro-delayed, slowly moving copies
        {
            stackRing[0].push (l); stackRing[1].push (r);
            stackPh += 0.3 / sr; if (stackPh >= 1.0) stackPh -= 1.0;
            if (const float a = w[wStack] * f; a > 1.0e-5f)
            {
                const double m1 = (0.007 + 0.0015 * std::sin (6.2831853 * stackPh)) * sr, m2 = (0.013 + 0.002 * std::sin (6.2831853 * (stackPh + 0.37))) * sr;
                const float a1 = stackRing[1].readLinear (m1), a2 = stackRing[0].readLinear (m2);
                l += 0.55f * a * (a1 + 0.6f * a2); r += 0.55f * a * (a2 + 0.6f * a1);
            }
        }
        // SMASH: parallel heavy compression (fast attack, 8:1 from -30 dB, make-up), mixed in before the drive stages
        {
            const float pre[2] { l, r };
            const float e2 = smashEnv.process (0.5f * (std::abs (l) + std::abs (r)) + 1.0e-9f);
            if (const float a = w[wSmash] * f; a > 1.0e-5f)
            {
                const float lv2 = gainToDb (e2), ov = lv2 + 30.0f, g2 = dbToGain ((ov > 0 ? ov * (1.0f / 8.0f - 1.0f) : 0.0f) + 14.0f);
                const float gate = clampf ((lv2 + 66.0f) * 0.1f, 0.0f, 1.0f);
                l += a * 0.8f * fastTanh (pre[0] * g2) * gate; r += a * 0.8f * fastTanh (pre[1] * g2) * gate;
            }
        }
        // GLUE (soft knee, 0-30 %)
        {
            const float a = w[wGlue] * wa;
            const float e = det2.process (std::abs (glueHp.process (0.5f * (l + r), glueHpC)) + 1.0e-9f);   // (sidechain high-passed: the kick's body is not pumped away)
            const float lv = gainToDb (e), thr = -8.0f - 20.0f * a, ratio = 1.0f + 5.0f * a, knee = 8.0f;
            const float over = lv - thr; float gr = 0;
            if (over > knee * 0.5f) gr = over * (1.0f / ratio - 1.0f);
            else if (over > -knee * 0.5f) { const float x = over + knee * 0.5f; gr = (1.0f / ratio - 1.0f) * x * x / (2.0f * knee); }
            grMax = std::min (grMax, gr * a);
            const float cg = dbToGain (gr * std::min (1.0f, a * 4.0f));
            l *= cg; r *= cg;
        }
        // the drive stages hit the signal at a steady level whatever the input level (gain staging: the level is undone after)
        const float lvl = satLvl.process (0.5f * (std::abs (l) + std::abs (r))) + 1.0e-6f;
        const float gn = clampf (0.25f / lvl, 0.25f, 8.0f) * (lvl > 1.0e-4f ? 1.0f : lvl * 1.0e4f);
        // SATURATION (30-70 %): asymmetric tube / tape curve = even + odd harmonics; unity for small signals, loud parts squeezed
        if (const float a = w[wSat] * satC; a > 1.0e-5f && gn > 1.0e-6f)
        {
            const float gd = dbToGain (a * 22.0f + cr * 10.0f) * gn, bias = 0.35f * a, tb = fastTanh (bias), comp = 0.3f / gn;   // full drive -> about the steady level
            for (int c = 0; c < 2; ++c) { float& x = c == 0 ? l : r; const float y = dcN[c].process ((fastTanh (x * gd + bias) - tb) * comp); x += a * (y - x); }
        }
        // CLIP / FOLD (70-100 %): driven hard clip; CRUNCH folds the overs back (gritty)
        if (const float a = w[wClip] * clipC; a > 1.0e-5f && gn > 1.0e-6f)
        {
            const float gc = dbToGain (a * 18.0f) * gn * 1.6f, fo = clampf ((cr - 0.3f) * (1.0f / 0.7f), 0.0f, 1.0f) * 0.7f, inv = 0.35f / gn;   // folding starts above CRUNCH 30 %
            for (int c = 0; c < 2; ++c)
            {
                float& x = c == 0 ? l : r;
                const float d = x * gc;
                const float hard = clampf (d, -1.0f, 1.0f);
                float fold = d; if (fold > 1.0f) fold = 2.0f - fold; else if (fold < -1.0f) fold = -2.0f - fold;
                fold = clampf (fold, -1.0f, 1.0f);
                const float y = (hard + fo * (fold - hard)) * inv;
                x += a * (y - x);
            }
        }
        l = tiltN[0].process (l); r = tiltN[1].process (r);
        // level: matched to the input, then allowed louder with FAT (up to +6 dB in SAUSAGE)
        const float g = agN.next (l, r) * dbToGain (6.0f * w[wLu] * f) * og;
        l *= g; r *= g;
    }

    P fat, color, crunch, out, mode, drive, punch, lowclean; double sr = 48000; ExpSmooth fatSm, crSm, outSm, modeSm, drvSm, punchSm, lcSm; EnvFollow det; AutoGain ag;
    Biquad lowSh[2], midB[2]; BiquadCoefs lowC, midC; Tilt tilt[2]; DcBlock dc[2]; float lastF = -1;
    int lastMode = 0; ExpSmooth wSm[kW];
    BiquadCoefs lp120, lp150, lp90, hp90, bodyC, bodyShC, nLowC, nMidC; float lastShelf = -1;
    Biquad monoB[4], lcompB[2], lcB[2], lcH[4], bodyB[2], bodySh[2], nLowSh[2], nMidB[2]; DcBlock dcN[2]; Tilt tiltN[2];
    OnePole subLp1, subLp2, subOutLp1, subOutLp2; EnvFollow satLvl, subEnv, lcEnv, det2, smashEnv, trFast, trSlow, pFast, pSlow, pIn, pOut; float subFlip = 1.0f; bool subArmed = true;
    Biquad subHp, glueHp; BiquadCoefs subHpC, glueHpC; ExpSmooth fatN; ExpSmooth subAct; int sinceFlip = 1000000;
    AutoGain agN; std::array<Ring, 2> stackRing; double stackPh = 0;
};

inline void addFattenParameters (Layout& l)
{
    auto T = [] (int u, const char* s) { return juce::String ("FX ") + unitInfo (u).title + " " + s; };
    auto dB = [] (float lo, float hi) { return juce::NormalisableRange<float> (lo, hi); };
    // FATTEN
    addPct (l, pid (uFatten, "fat"), T (uFatten, "Fat"), 0.4f);
    addFloat (l, pid (uFatten, "color"), T (uFatten, "Color"), juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f, "", 2);
    addPct (l, pid (uFatten, "crunch"), T (uFatten, "Crunch"), 0.2f);
    addFloat (l, pid (uFatten, "out"), T (uFatten, "Output"), dB (-12.0f, 12.0f), 0.0f, "dB", 1);
    // FATTEN MODE / INPUT / PUNCH / LOW CLEAN (2026-10-23): defaults = the original unit (old sessions load CLASSIC, 0 dB, off, off: bit-identical)
    addChoice (l, pid (uFatten, "mode"), T (uFatten, "Mode"), fattenModeNames(), 0);
    addFloat (l, pid (uFatten, "drive"), T (uFatten, "Input"), juce::NormalisableRange<float> (0.0f, 24.0f), 0.0f, "dB", 1);
    addBool (l, pid (uFatten, "punch"), T (uFatten, "Punch"), false);
    addBool (l, pid (uFatten, "lowclean"), T (uFatten, "Low Clean"), false);
}

} // namespace gpfx
