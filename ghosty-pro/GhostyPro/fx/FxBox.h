#pragma once
// Ghost Box units (GPFX_BOX builds only): WARP (drawn tempo-synced pitch bend: signalsmith-stretch pitch shift or
// tape-speed read), RETRO (vintage colour box), FATTEN (one-knob fattener),
// CRACKLE (multiband destroyer with synthesized speaker boxes) and HAUNTED HALLS (16-line FDN reverb, 11 modes).
// Original designs. Audio-thread rule as everywhere in the rack: no allocation, no locks inside process().
#include "FxCore.h"
#include "FxSpaceDsp.h"
#include "FxColour.h"
#include "BendLane.h"
#include "FxFatten.h"
#include "FxHyper.h"   // FATTEN + Tilt: shared with Ghosty Pro (built in both)

#if GPFX_BOX
#include "signalsmith-stretch/signalsmith-stretch.h"   // Signalsmith Stretch (MIT), already a dependency of the repo
namespace gpfx {

// ---------------------------------------------------------------- small shared helpers
struct Rng
{
    uint32_t s = 0x1234567u;
    float uni() noexcept { s = s * 1664525u + 1013904223u; return (float) (s >> 8) * (1.0f / 16777216.0f); }   // 0..1
    float bi() noexcept { return uni() * 2.0f - 1.0f; }
};
// Paul Kellet's economy pink filter
struct Pink
{
    float b0 = 0, b1 = 0, b2 = 0;
    float process (float w) noexcept { b0 = 0.99765f * b0 + w * 0.0990460f; b1 = 0.96300f * b1 + w * 0.2965164f; b2 = 0.57000f * b2 + w * 1.0526913f; return (b0 + b1 + b2 + w * 0.1848f) * 0.25f; }
    void reset() { b0 = b1 = b2 = 0; }
};

// ================================================================ RETRO
enum RetroNoise { rnVinyl, rnDust, rnHiss, rnWhite, rnPink, rnHum50, rnHum60, rnRadio, kNumRetroNoise };
inline const juce::StringArray& retroNoiseNames() { static juce::StringArray s { "VINYL", "DUST", "TAPE HISS", "WHITE", "PINK", "HUM 50", "HUM 60", "RADIO" }; return s; }
inline const juce::StringArray& retroDistNames() { static juce::StringArray s { "TAPE", "TUBE", "CONSOLE", "TRANSISTOR", "FUZZ" }; return s; }

class RetroUnit : public UnitDsp
{
public:
    Telemetry* tm = nullptr;
    void attach (juce::AudioProcessorValueTreeState& s) override
    {
        auto b = [&s] (P& p, const char* k, float d) { p.bind (s, pid (uRetro, k), d); };
        b (magic, "magic", 0.5f); b (tone, "tone", 0); b (out, "out", 0);
        b (nOn, "noise_on", 1); b (nAmt, "noise", 0.3f); b (nType, "ntype", 0); b (nTone, "ntone", 0); b (nFollow, "nfollow", 0);
        b (wOn, "wobble_on", 1); b (wAmt, "wobble", 0.3f); b (wFlut, "flutter", 0.3f);
        b (dOn, "dist_on", 1); b (dAmt, "dist", 0.3f); b (dType, "dtype", 0);
        b (gOn, "digi_on", 0); b (gAmt, "digi", 0.3f);
        b (sOn, "space_on", 0); b (sAmt, "space", 0.3f);
        b (mOn, "mag_on", 0); b (mAmt, "mag", 0.3f);
    }
    void prepare (double sr_, int) override
    {
        sr = sr_;
        for (auto& r : wob) r.prepare ((int) (sr * 0.04) + 16);
        for (auto* e : { &eN, &eW, &eD, &eG, &eS, &eM, &onW, &onD, &onG, &onM }) e->setTime (sr, 40.0f);
        inEnv.set (sr, 5.0f, 250.0f);
        ag.prepare (sr, 250.0f);
        verb.prepare (sr, 1.2f);
        for (auto& t : tilt) t.prepare (sr);
        for (auto& t : nTilt) t.prepare (sr, 1500.0);
        for (int c = 0; c < 2; ++c) { hissHp[c].setLP (sr, 1800.0); vinHp[c].setLP (sr, 900.0); dustLp[c].setLP (sr, 2500.0); radioBp[c].setLP (sr, 3500.0); radioHp[c].setLP (sr, 600.0); rumble[c].setLP (sr, 35.0); }
        driftSm.setTime (sr, 900.0f);
        reset();
    }
    void reset() override
    {
        for (auto& r : wob) r.clear();
        verb.clear(); ag.reset(); inEnv.reset();
        for (auto* e : { &eN, &eW, &eD, &eG, &eS, &eM }) e->snap (0.0f);
        onW.snap (wOn.b() ? 1.0f : 0.0f); onD.snap (dOn.b() ? 1.0f : 0.0f); onG.snap (gOn.b() ? 1.0f : 0.0f); onM.snap (mOn.b() ? 1.0f : 0.0f);
        for (auto& t : tilt) t.reset();
        for (auto& t : nTilt) t.reset();
        for (int c = 0; c < 2; ++c) { pink[c].reset(); hissHp[c].reset(); vinHp[c].reset(); dustLp[c].reset(); radioBp[c].reset(); radioHp[c].reset(); rumble[c].reset(); click[c] = 0; bump[c].reset(); magLp[c].reset(); held[c] = 0; }
        wowO.setPhase (0); flO.setPhase (0.25); fl2.setPhase (0.6); humO.setPhase (0); driftSm.snap (0); holdPh = 0;
        dropLeft = 0; dropLen = 1; dropDepth = 0; outSm.snap (dbToGain (out()));
    }
    void process (float* L, float* R, int n, const BlockCtx&) override
    {
        const float mg = clampf (magic(), 0.0f, 1.0f) * 2.0f;
        auto eff = [mg] (const P& on, const P& amt) { return on.b() ? clampf (amt() * mg, 0.0f, 1.0f) : 0.0f; };
        eN.target = eff (nOn, nAmt); eW.target = eff (wOn, wAmt); eD.target = eff (dOn, dAmt);
        eG.target = eff (gOn, gAmt); eS.target = eff (sOn, sAmt); eM.target = eff (mOn, mAmt);
        onW.target = wOn.b() ? 1.0f : 0.0f; onD.target = dOn.b() ? 1.0f : 0.0f; onG.target = gOn.b() ? 1.0f : 0.0f; onM.target = mOn.b() ? 1.0f : 0.0f;
        outSm.target = dbToGain (out());
        for (auto& t : tilt) t.set (tone());
        const float nt = nTone();
        for (auto& t : nTilt) t.set (nt);
        const int ntype = juce::jlimit (0, kNumRetroNoise - 1, nType.i());
        const bool follow = nFollow.b();
        static const int dmap[5] { wsTape, wsTube, wsConsole, wsTransistor, wsFuzz };
        const int dstyle = dmap[juce::jlimit (0, 4, dType.i())];
        // space: a small dark character room whose size and decay grow with the amount
        verb.set (0.12f + 0.35f * eS.cur, 0.35f + 1.6f * eS.cur, 4200.0f + 1500.0f * eS.cur, 0.25f, false);
        verb.updateGains (verb.sizeSm.cur);
        const float flut = clampf (wFlut(), 0.0f, 1.0f);
        wowO.setFreq (0.55, sr); flO.setFreq (8.7, sr); fl2.setFreq (13.3, sr); wowO.normalise(); flO.normalise(); fl2.normalise();
        humO.setFreq (ntype == rnHum60 ? 60.0 : 50.0, sr); humO.normalise();
        float pkOut = 0, pkNoise = 0, wowNow = 0, crushNow = 0;
        for (int i = 0; i < n; ++i)
        {
            const float en = eN.next(), ew = eW.next(), ed = eD.next(), eg = eG.next(), es = eS.next(), em = eM.next();
            const float ow = onW.next(), od = onD.next(), og = onG.next(), om = onM.next();
            float l = L[i], r = R[i];
            const float env = inEnv.process (0.5f * (l + r));
            // ---- DISTORT
            if (od > 0.0f)
            {
                ag.feedIn (l, r);
                const float gd = dbToGain (ed * 30.0f);
                float dl = wreckShape (dstyle, l, gd, ed), dr = wreckShape (dstyle, r, gd, ed);
                const float a = ag.next (dl, dr);
                dl *= a; dr *= a;
                l += od * (dl - l); r += od * (dr - r);
            }
            // ---- DIGITAL (bit depth + sample-rate reduction)
            if (og > 0.0f)
            {
                const float hold = 1.0f + 23.0f * eg * eg;
                holdPh += 1.0f;
                if (holdPh >= hold) { holdPh -= hold; held[0] = l; held[1] = r; }
                const float q = std::pow (2.0f, 15.0f - 12.0f * eg);
                const float cl = std::round (held[0] * q) / q, cr = std::round (held[1] * q) / q;
                const float mix = og * clampf (eg * 4.0f, 0.0f, 1.0f);
                l += mix * (cl - l); r += mix * (cr - r);
                crushNow = std::max (crushNow, eg * og);
            }
            // ---- MAGNETIC (tape saturation, head bump, HF loss, dropouts)
            if (om > 0.0f)
            {
                if (dropLeft <= 0 && rng.uni() < em * 0.9f / (float) sr)
                {
                    dropLen = (int) ((0.04 + 0.22 * rng.uni()) * sr); dropLeft = dropLen; dropDepth = em * (0.35f + 0.6f * rng.uni());
                }
                float dg = 1.0f;
                if (dropLeft > 0) { dg = 1.0f - dropDepth * hannWin (1.0f - (float) dropLeft / (float) dropLen); --dropLeft; }
                dropNow = dg;
                const float k = 1.0f + 2.5f * em;
                for (int c = 0; c < 2; ++c)
                {
                    float& x = c == 0 ? l : r;
                    float y = fastTanh (x * k) / k;
                    y = bump[c].process (y, bumpC);
                    y = magLp[c].lp (y);
                    y *= dg;
                    x += om * (y - x);
                }
            }
            // ---- WOBBLE (wow + flutter on a short tape loop)
            wowO.step(); flO.step(); fl2.step();
            if ((i & 31) == 0) driftSm.target = rng.bi();
            const float drift = driftSm.next();
            wob[0].push (l); wob[1].push (r);
            if (ow > 0.0f)
            {
                const double wowD = ew * 0.0034 * sr * (1.0 - 0.6 * flut), flD = ew * 0.00045 * sr * (0.25 + 1.5 * flut);
                const double m = wowD * (1.0 + 0.75 * wowO.s + 0.35 * drift) + flD * (1.0 + 0.7 * flO.s + 0.3 * fl2.s);
                const double d = 2.0 + m;
                const float wl = wob[0].read (d), wr = wob[1].read (d + 0.3 * flD * fl2.c);
                const float a = ow * clampf (ew * 6.0f, 0.0f, 1.0f);
                l += a * (wl - l); r += a * (wr - r);
                wowNow = (float) (wowO.s * ew);
            }
            // ---- SPACE
            if (es > 0.0005f)
            {
                float vl, vr; verb.process (l, r, vl, vr);
                l += vl * es * 0.8f; r += vr * es * 0.8f;
            }
            // ---- NOISE
            if (en > 0.0005f)
            {
                float nl = 0, nr = 0;
                noiseSample (ntype, nl, nr);
                const float lvl = dbToGain (-52.0f + 40.0f * en) * clampf (en * 25.0f, 0.0f, 1.0f) * (follow ? clampf (env * 6.0f, 0.0f, 1.0f) : 1.0f);
                nl = nTilt[0].process (nl) * lvl; nr = nTilt[1].process (nr) * lvl;
                l += nl; r += nr;
                pkNoise = std::max (pkNoise, std::abs (nl));
            }
            const float og2 = outSm.next();
            l = tilt[0].process (l) * og2; r = tilt[1].process (r) * og2;
            L[i] = l; R[i] = r;
            pkOut = std::max (pkOut, std::abs (l));
        }
        // magnetic filters follow the amount (block rate)
        bumpC = rbj (rbjBell, sr, 95.0, 0.9, eM.cur * 5.0);
        for (auto& f : magLp) f.setLP (sr, 20000.0 - 13500.0 * eM.cur);
        if (tm)
        {
            tm->retroLevel.store (pkOut); tm->retroNoise.store (pkNoise); tm->retroWow.store (wowNow);
            tm->retroDrop.store (dropNow); tm->retroCrush.store (crushNow);
        }
    }
private:
    void noiseSample (int type, float& nl, float& nr) noexcept
    {
        const float wl = rng.bi(), wr = rng.bi();
        switch (type)
        {
            case rnVinyl:
            {
                for (int c = 0; c < 2; ++c)
                {
                    if (rng.uni() < 160.0f / (float) sr) { const float a = rng.uni(); click[c] += (rng.uni() < 0.5f ? -1.0f : 1.0f) * a * a * a * 3.0f; }
                    if (c == 0 && rng.uni() < 25.0f / (float) sr) { const float a = rng.uni(); click[0] += a * a * 2.0f; click[1] += a * a * 1.6f; }
                    click[c] *= 0.82f;
                }
                const float sl = vinHp[0].hp (pink[0].process (wl)) * 0.25f, sr2 = vinHp[1].hp (pink[1].process (wr)) * 0.25f;
                const float rb = rumble[0].lp (wl) * 2.5f;
                nl = vinHp[0].hp (click[0] * (0.6f + 0.4f * wl)) * 1.6f + sl + rb;
                nr = vinHp[1].hp (click[1] * (0.6f + 0.4f * wr)) * 1.6f + sr2 + rb;
                break;
            }
            case rnDust:
                for (int c = 0; c < 2; ++c)
                {
                    if (rng.uni() < 5.0f / (float) sr) { const float a = rng.uni(); click[c] += (rng.uni() < 0.5f ? -1.0f : 1.0f) * (0.4f + a * a * 2.6f); }
                    click[c] *= 0.985f;
                }
                nl = dustLp[0].lp (click[0] * (0.7f + 0.3f * wl)) * 1.8f + pink[0].process (wl) * 0.03f;
                nr = dustLp[1].lp (click[1] * (0.7f + 0.3f * wr)) * 1.8f + pink[1].process (wr) * 0.03f;
                break;
            case rnHiss:  nl = hissHp[0].hp (pink[0].process (wl)) * 1.6f; nr = hissHp[1].hp (pink[1].process (wr)) * 1.6f; break;
            case rnWhite: nl = wl * 0.5f; nr = wr * 0.5f; break;
            case rnPink:  nl = pink[0].process (wl) * 1.2f; nr = pink[1].process (wr) * 1.2f; break;
            case rnHum50: case rnHum60:
            {
                humO.step();
                const float s1 = (float) humO.s, c1 = (float) humO.c, s2 = 2.0f * s1 * c1, c2 = c1 * c1 - s1 * s1, s3 = s2 * c1 + c2 * s1;
                const float h = s1 * 0.7f + s2 * 0.35f + s3 * 0.25f;
                const float buzz = clampf (h * 3.0f, -0.6f, 0.6f) * 0.4f;
                nl = nr = (h + buzz) * 0.7f + pink[0].process (wl) * 0.02f;
                break;
            }
            case rnRadio:
            {
                if (rng.uni() < 2.0f / (float) sr) radioGust = 0.4f + rng.uni();
                radioGust += (0.35f - radioGust) * 0.00005f;
                const float b0 = radioHp[0].hp (radioBp[0].lp (wl)), b1 = radioHp[1].hp (radioBp[1].lp (wr));
                for (int c = 0; c < 2; ++c) { if (rng.uni() < 60.0f / (float) sr) click[c] += rng.bi() * 1.5f; click[c] *= 0.9f; }
                nl = b0 * radioGust * 1.8f + click[0]; nr = b1 * radioGust * 1.8f + click[1];
                break;
            }
            default: break;
        }
    }

    P magic, tone, out, nOn, nAmt, nType, nTone, nFollow, wOn, wAmt, wFlut, dOn, dAmt, dType, gOn, gAmt, sOn, sAmt, mOn, mAmt;
    double sr = 48000;
    std::array<Ring, 2> wob; ExpSmooth eN, eW, eD, eG, eS, eM, onW, onD, onG, onM, outSm, driftSm;
    EnvFollow inEnv; AutoGain ag; Fdn8 verb; Tilt tilt[2], nTilt[2];
    Pink pink[2]; OnePole hissHp[2], vinHp[2], dustLp[2], radioBp[2], radioHp[2], rumble[2], magLp[2]; Biquad bump[2]; BiquadCoefs bumpC;
    float click[2] {}, held[2] {}, holdPh = 0, radioGust = 0.35f, dropDepth = 0, dropNow = 1; int dropLeft = 0, dropLen = 1;
    Phasor wowO, flO, fl2, humO; Rng rng;
};

// ================================================================ CRACKLE (multiband destroyer)
enum CrackleCurve { ccTube, ccTape, ccDiode, ccHard, ccFuzz, ccRectify, ccHalfRect, ccFold, ccSineFold, ccBitcrush, ccDownsample,
                    ccBroken, ccCracked, ccBattery, kNumCrackleCurves };
inline const juce::StringArray& crackleCurveNames()
{
    static juce::StringArray s { "TUBE", "TAPE", "DIODE", "HARD CLIP", "FUZZ", "RECTIFY", "HALF RECT", "FOLD", "SINE FOLD", "BITCRUSH",
                                 "DOWNSAMPLE", "BROKEN SPEAKER", "CRACKED", "DYING BATTERY" };
    return s;
}
enum CrackleBox { cbOff, cbSmallSpeaker, cbCab, cbPhone, cbRadio, cbTinCan, cbCardboard, cbMegaphone, cbLaptop, cbWalkie, kNumCrackleBoxes };
inline const juce::StringArray& crackleBoxNames()
{
    static juce::StringArray s { "OFF", "SMALL SPEAKER", "4x12 CAB", "TELEPHONE", "AM RADIO", "TIN CAN", "CARDBOARD BOX", "MEGAPHONE", "LAPTOP", "WALKIE-TALKIE" };
    return s;
}
inline bool crackleUsesGain (int c) { return c != ccBitcrush && c != ccDownsample; }
// Static part of each curve (also drawn by the editor). d = driven input, a = drive 0..1.
inline float crackleStatic (int c, float d, float a) noexcept
{
    switch (c)
    {
        case ccTube:     return (fastTanh (d + 0.3f) - fastTanh (0.3f)) * 1.1f;
        case ccTape:     return wreckShape (wsTape, d, 1.0f, a);
        case ccDiode:    return d > 0 ? fastTanh (d * 1.6f) * 0.85f : fastTanh (d * 0.55f) * 0.95f;
        case ccHard:     return clampf (d, -1.0f, 1.0f);
        case ccFuzz:     return wreckShape (wsFuzz, d, 1.0f, a);
        case ccRectify:  return std::abs (fastTanh (d)) * 1.4f;
        case ccHalfRect: return std::max (0.0f, fastTanh (d)) * 1.6f;
        case ccFold:     { const float t = d * 0.25f + 0.25f; return 4.0f * std::abs (t - std::floor (t + 0.5f)) - 1.0f; }
        case ccSineFold: return std::sin (d * 1.5f);
        case ccBroken:   { const float dz = 0.04f + 0.25f * a; const float m = std::max (0.0f, std::abs (d) - dz); return fastTanh ((d < 0 ? -m : m) * 1.6f); }
        case ccCracked:  return d >= 0 ? clampf (d * 1.3f, 0.0f, 0.7f) / 0.7f : -std::abs (std::sin (d * 2.2f)) * 0.8f;
        case ccBattery:  return fastTanh (d * 1.4f);
        default:         return d;
    }
}

// Short synthesized impulse responses for the BOX section (no recorded IRs).
inline void crackleMakeIr (int box, double sr, std::vector<float>& ir)
{
    const int N = (int) ir.size();
    std::fill (ir.begin(), ir.end(), 0.0f);
    if (box == cbOff || N == 0) { if (N > 0) ir[0] = 1.0f; return; }
    struct Res { float hz, ms, amp; };
    struct Def { float hp; int hpN; float lp; int lpN; std::vector<Res> res; float comb; float combMs; float direct; };
    static const Def defs[kNumCrackleBoxes] {
        { 0, 0, 0, 0, {}, 0, 0, 1 },
        { 180, 1, 6500, 1, { { 900, 1.5f, 0.5f }, { 2500, 1.0f, 0.35f }, { 4200, 0.6f, 0.2f } }, 0.0f, 0.0f, 0.7f },
        { 80, 1, 4200, 2, { { 110, 6.0f, 0.5f }, { 2200, 1.2f, 0.4f }, { 3300, 0.8f, 0.2f } }, -0.4f, 0.6f, 0.6f },
        { 350, 2, 3400, 2, { { 1200, 1.5f, 0.4f }, { 2400, 0.8f, 0.2f } }, 0.0f, 0.0f, 0.7f },
        { 200, 1, 4500, 2, { { 1800, 1.4f, 0.4f }, { 700, 1.0f, 0.2f } }, 0.25f, 1.3f, 0.7f },
        { 400, 1, 9000, 1, { { 1400, 8.0f, 0.5f }, { 3100, 6.0f, 0.45f }, { 5300, 4.0f, 0.3f } }, 0.3f, 0.4f, 0.4f },
        { 60, 1, 2500, 1, { { 250, 4.0f, 0.5f }, { 600, 2.5f, 0.3f } }, 0.2f, 1.8f, 0.6f },
        { 500, 2, 5000, 1, { { 1600, 3.0f, 0.6f }, { 2800, 2.0f, 0.35f } }, 0.3f, 2.2f, 0.5f },
        { 600, 2, 9000, 1, { { 1100, 1.5f, 0.4f }, { 4500, 1.0f, 0.4f } }, 0.0f, 0.0f, 0.7f },
        { 450, 2, 2800, 2, { { 1500, 2.0f, 0.5f } }, 0.0f, 0.0f, 0.6f },
    };
    const auto& d = defs[juce::jlimit (0, kNumCrackleBoxes - 1, box)];
    ir[0] = d.direct;
    juce::Random rnd ((juce::int64) (box * 7919 + 3));
    for (auto& r : d.res)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * r.hz / sr, tau = r.ms * 0.001 * sr;
        const double ph = rnd.nextDouble() * 0.5;
        for (int k = 0; k < N; ++k) ir[(size_t) k] += (float) (r.amp * std::exp (-k / tau) * std::sin (w * k + ph) * w * 2.0);
    }
    if (std::abs (d.comb) > 0.0f) { const int off = (int) (d.combMs * 0.001 * sr); for (int k = N - 1; k >= off; --k) ir[(size_t) k] += d.comb * ir[(size_t) (k - off)]; }
    auto run = [&ir, N] (const BiquadCoefs& c) { Biquad b; for (int k = 0; k < N; ++k) ir[(size_t) k] = b.process (ir[(size_t) k], c); };
    for (int k = 0; k < d.hpN; ++k) run (rbj (rbjHP, sr, d.hp, 0.707));
    for (int k = 0; k < d.lpN; ++k) run (rbj (rbjLP, sr, d.lp, 0.707));
    // gentle fade-out of the tail, then unit energy (white noise keeps its power) with the loudest frequency capped at
    // +6 dB, so resonant boxes don't throw big peaks; auto gain does the rest
    for (int k = N * 3 / 4; k < N; ++k) ir[(size_t) k] *= 0.5f + 0.5f * std::cos (kPi * (float) (k - N * 3 / 4) / (float) (N / 4));
    double mx = 1e-9;
    for (int b = 0; b < 160; ++b)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * 30.0 * std::pow (600.0, b / 159.0) / sr;
        double re = 0, im = 0; for (int k = 0; k < N; ++k) { re += ir[(size_t) k] * std::cos (w * k); im -= ir[(size_t) k] * std::sin (w * k); }
        mx = std::max (mx, std::sqrt (re * re + im * im));
    }
    double e = 0; for (auto v : ir) e += (double) v * v;
    const double scale = std::min (e > 1e-12 ? 1.0 / std::sqrt (e) : 1.0, 2.0 / mx);   // unit energy, but never more than +6 dB at any frequency
    for (auto& v : ir) v = (float) (v * scale);
}

// Direct-form FIR with a doubled history buffer (contiguous dot product, vectorises well).
struct Fir
{
    std::vector<float> h, hist; int n = 0, pos = 0;
    void prepare (int taps) { n = (taps + 7) & ~7; h.assign ((size_t) n, 0.0f); hist.assign ((size_t) n * 2, 0.0f); pos = 0; }
    void clear() { std::fill (hist.begin(), hist.end(), 0.0f); pos = 0; }
    float process (float x) noexcept
    {
        pos = pos == 0 ? n - 1 : pos - 1;
        hist[(size_t) pos] = x; hist[(size_t) (pos + n)] = x;
        const float* hp = h.data(); const float* xp = hist.data() + pos;
        float acc[8] {};
        for (int k = 0; k < n; k += 8)
            for (int j = 0; j < 8; ++j) acc[j] += hp[k + j] * xp[k + j];
        return ((acc[0] + acc[1]) + (acc[2] + acc[3])) + ((acc[4] + acc[5]) + (acc[6] + acc[7]));
    }
};

class CrackleUnit : public UnitDsp
{
public:
    Telemetry* tm = nullptr;
    void attach (juce::AudioProcessorValueTreeState& s) override
    {
        auto b = [&s] (P& p, const juce::String& k, float d) { p.bind (s, pid (uCrackle, k.toRawUTF8()), d); };
        b (bands, "bands", 0); b (xlo, "xlo", 220); b (xhi, "xhi", 2800);
        for (int k = 0; k < 3; ++k)
        {
            const juce::String pre = "b" + juce::String (k + 1) + "_";
            b (curve[k], pre + "curve", 0); b (drive[k], pre + "drive", 0.4f); b (bmix[k], pre + "mix", 1); b (level[k], pre + "level", 0);
        }
        b (prehp, "prehp", 10); b (prelp, "prelp", 22000); b (posthp, "posthp", 10); b (postlp, "postlp", 22000);
        b (box, "box", 0); b (boxmix, "boxmix", 1); b (autoG, "auto", 1); b (out, "out", 0);
    }
    void prepare (double sr_, int) override
    {
        sr = sr_;
        for (auto& o : os) { o.initProcessing ((size_t) kSub); o.reset(); }
        lat = (int) std::lround (os[0].getLatencyInSamples());
        dry.prepare (lat);
        const int taps = (juce::jlimit (64, 1024, (int) (sr * 0.0107)) + 7) & ~7;
        irs.assign ((size_t) kNumCrackleBoxes, std::vector<float> ((size_t) taps, 0.0f));
        for (int b = 0; b < kNumCrackleBoxes; ++b) crackleMakeIr (b, sr, irs[(size_t) b]);
        for (auto& c : firs) for (auto& f : c) f.prepare (taps);
        ag.prepare (sr, 300.0f); outSm.setTime (sr, 30.0f); boxSm.setTime (sr, 30.0f);
        for (int k = 0; k < 3; ++k) { drvSm[k].setTime (sr * 2.0, 30.0f); mixSm[k].setTime (sr * 2.0, 30.0f); lvlSm[k].setTime (sr * 2.0, 30.0f); actSm[k].setTime (sr * 2.0, 15.0f); }
        for (auto& c : bandDc) for (auto& d : c) d.prepare (sr);
        reset();
    }
    void reset() override
    {
        for (auto& o : os) o.reset();
        dry.clear(); ag.reset();
        for (auto& c : xf) for (auto& b : c) b.reset();
        for (auto& c : filt) for (auto& b : c) b.reset();
        for (auto& c : firs) for (auto& f : c) f.clear();
        for (auto& c : bandDc) for (auto& d : c) d.reset();
        for (auto& c : st) for (auto& x : c) x = {};
        curBox = juce::jlimit (0, kNumCrackleBoxes - 1, box.i()); prevBox = curBox; boxFade = 0;
        loadFir (0, curBox);
        outSm.snap (dbToGain (out())); boxSm.snap (boxmix());
        for (int k = 0; k < 3; ++k) { drvSm[k].snap (drive[k]()); mixSm[k].snap (bmix[k]()); lvlSm[k].snap (dbToGain (level[k]())); actSm[k].snap (k <= bands.i() ? 1.0f : 0.0f); }
        lastLo = lastHi = lastPh = lastPl = lastQh = lastQl = -1; warmMask = 0; noWarm = true;   // a fresh start begins from rest, as before
    }
    int latency() const override { return lat; }
    void delayDry (float* L, float* R, int n) override { dry.process (L, R, n); }
    void clearLines() override { dry.clear(); }
    void process (float* L, float* R, int n, const BlockCtx&) override
    {
        updateFilters();
        const int nb = juce::jlimit (0, 2, bands.i()) + 1;
        const int bx = juce::jlimit (0, kNumCrackleBoxes - 1, box.i());
        if (bx != curBox && boxFade == 0) { prevBox = curBox; curBox = bx; loadFir (1 - firSlot, curBox); firSlot = 1 - firSlot; boxFade = (int) (sr * 0.02); boxFadeLen = boxFade; }
        outSm.target = dbToGain (out()); boxSm.target = boxmix();
        const bool useAuto = autoG.b();
        // 1) pre filters and band split (Linkwitz-Riley 4th order)
        for (int i = 0; i < n; ++i)
        {
            float x[2] { L[i], R[i] };
            for (int c = 0; c < 2; ++c)
            {
                float v = x[c];
                if (warmMask & 3)
                {
                    if (warmMask & 1) filt[c][0].warm (v, preHpC);
                    if (warmMask & 2) filt[c][1].warm (v, preLpC);
                    if (c == 1) warmMask &= ~3;
                }
                v = filt[c][1].process (filt[c][0].process (v, preHpC), preLpC);
                float lo = xf[c][1].process (xf[c][0].process (v, lo1C), lo1C), rest = xf[c][3].process (xf[c][2].process (v, hi1C), hi1C);
                float mid = rest, hi = 0;
                if (nb == 3)
                {
                    mid = xf[c][5].process (xf[c][4].process (rest, lo2C), lo2C); hi = xf[c][7].process (xf[c][6].process (rest, hi2C), hi2C);
                    // keep the low band in phase with mid + high: run it through the same crossover as an allpass
                    lo = xf[c][9].process (xf[c][8].process (lo, lo2C), lo2C) + xf[c][11].process (xf[c][10].process (lo, hi2C), hi2C);
                }
                if (nb == 1) { lo = v; mid = 0; hi = 0; }
                band[0][c][i] = lo; band[1][c][i] = mid; band[2][c][i] = hi;
            }
            if (useAuto) ag.feedIn (L[i], R[i]);
        }
        // 2) shape every active band at 2x
        float bandPk[3] {};
        for (int b = 0; b < 3; ++b)
        {
            actSm[b].target = b < nb ? 1.0f : 0.0f;
            if (b >= nb && actSm[b].cur < 1.0e-4f) { actSm[b].snap (0.0f); continue; }
            float* chans[2] { band[b][0], band[b][1] };
            juce::dsp::AudioBlock<float> blk (chans, 2, (size_t) n);
            auto up = os[(size_t) b].processSamplesUp (blk);
            const int cv = juce::jlimit (0, kNumCrackleCurves - 1, curve[b].i());
            drvSm[b].target = drive[b](); mixSm[b].target = bmix[b](); lvlSm[b].target = dbToGain (level[b]());
            const int un = (int) up.getNumSamples();
            float* u0 = up.getChannelPointer (0); float* u1 = up.getChannelPointer (1);
            for (int i = 0; i < un; ++i)
            {
                const float a = drvSm[b].next(), m = mixSm[b].next(), lg = lvlSm[b].next(), act = actSm[b].next();
                const float g = crackleUsesGain (cv) ? dbToGain (a * 40.0f) : 1.0f;
                const float comp = crackleUsesGain (cv) ? 1.0f / std::sqrt (g) : 1.0f;
                for (int c = 0; c < 2; ++c)
                {
                    float* u = c == 0 ? u0 : u1;
                    const float x = u[i];
                    const float y = shape (cv, x, g, a, st[b][c]) * comp;
                    u[i] = (x + m * (y - x)) * lg * act;
                }
            }
            os[(size_t) b].processSamplesDown (blk);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < n; ++i) { auto& v = band[b][c][i]; v = bandDc[b][c].process (v); bandPk[b] = std::max (bandPk[b], std::abs (v)); }
        }
        // 3) sum, post filters, box, auto gain, output
        float pk = 0;
        for (int i = 0; i < n; ++i)
        {
            float y[2];
            for (int c = 0; c < 2; ++c)
            {
                float v = band[0][c][i] + band[1][c][i] + band[2][c][i];
                if (warmMask & 12)
                {
                    if (warmMask & 4) filt[c][2].warm (v, postHpC);
                    if (warmMask & 8) filt[c][3].warm (v, postLpC);
                    if (c == 1) warmMask &= ~12;
                }
                v = filt[c][3].process (filt[c][2].process (v, postHpC), postLpC);
                if (curBox != cbOff || boxFade > 0)
                {
                    float bv = firs[(size_t) firSlot][(size_t) c].process (v);
                    if (boxFade > 0)
                    {
                        const float w = (float) boxFade / (float) boxFadeLen;   // 1 -> 0
                        const float old = prevBox == cbOff ? v : firs[(size_t) (1 - firSlot)][(size_t) c].process (v);
                        const float nw = curBox == cbOff ? v : bv;
                        bv = nw + w * (old - nw);
                    }
                    v += boxSm.cur * (bv - v);
                }
                y[c] = v;
            }
            if (boxFade > 0) --boxFade;
            boxSm.next();
            const float a = useAuto ? ag.next (y[0], y[1]) : 1.0f;
            const float og = outSm.next() * a;
            L[i] = y[0] * og; R[i] = y[1] * og;
            pk = std::max (pk, std::abs (L[i]));
        }
        if (tm) { for (int b = 0; b < 3; ++b) tm->crackleBand[(size_t) b].store (bandPk[b]); tm->crackleOut.store (pk); }
    }
private:
    struct ChanState { float hold = 0, holdPh = 0, env = 0, spl = 0; uint32_t rnd = 0x9e3779b9u; };
    float shape (int cv, float x, float g, float a, ChanState& s) noexcept
    {
        const float d = x * g;
        switch (cv)
        {
            case ccBitcrush: { const float q = std::pow (2.0f, 15.0f - 14.0f * a); return std::round (clampf (x * (1.0f + a), -1.0f, 1.0f) * q) / q; }
            case ccDownsample:
            {
                const float hold = 1.0f + a * a * 60.0f; s.holdPh += 1.0f;
                if (s.holdPh >= hold) { s.holdPh -= hold; s.hold = x; }
                return s.hold;
            }
            case ccBroken:
            {
                float y = crackleStatic (ccBroken, d, a);
                const float over = std::abs (y) - 0.45f;
                if (over > 0) { s.rnd = hash32 (s.rnd + 1u); y += ((float) (s.rnd & 0xffff) / 32768.0f - 1.0f) * over * a * 1.2f; }
                return y;
            }
            case ccCracked:
            {
                float y = crackleStatic (ccCracked, d, a);
                s.rnd = hash32 (s.rnd + 7u);
                const float r = (float) (s.rnd & 0xffffff) / 16777215.0f;
                if (s.spl > 0.0f) { s.spl -= 1.0f; y = -y * 1.4f; }
                else if (r < a * 0.004f * clampf (std::abs (x) * 10.0f, 0.0f, 1.0f)) s.spl = 4.0f + 30.0f * r / std::max (1.0e-4f, a * 0.004f);
                return y + 0.08f * a * (r - 0.5f) * clampf (std::abs (d), 0.0f, 1.0f);
            }
            case ccBattery:
            {
                s.env += 0.002f * (std::abs (x) - s.env);
                s.rnd = hash32 (s.rnd + 13u);
                const float r = (float) (s.rnd & 0xffff) / 65535.0f;
                const float thr = 0.02f + 0.25f * a * a;
                if (s.env < thr * (0.5f + r)) return crackleStatic (ccBattery, d, a) * 0.1f * r;
                return crackleStatic (ccBattery, d, a);
            }
            default: return crackleStatic (cv, d, a);
        }
    }
    void loadFir (int slot, int b)
    {
        auto& src = irs[(size_t) juce::jlimit (0, kNumCrackleBoxes - 1, b)];
        for (auto& f : firs[(size_t) slot]) { std::copy (src.begin(), src.end(), f.h.begin()); f.clear(); }
        // FIR history must be warm for a click-free fade: prefill is not possible without input, so the fade covers it
    }
    void updateFilters()
    {
        const float lo = clampf (xlo(), 20.0f, 20000.0f), hi = std::max (clampf (xhi(), 20.0f, 20000.0f), lo * 1.5f);
        if (changed (lo, lastLo)) { lastLo = lo; lo1C = rbj (rbjLP, sr, lo, 0.7071); hi1C = rbj (rbjHP, sr, lo, 0.7071); }
        if (changed (hi, lastHi)) { lastHi = hi; lo2C = rbj (rbjLP, sr, hi, 0.7071); hi2C = rbj (rbjHP, sr, hi, 0.7071); }
        // a filter leaving its pass-through setting (automation sweeping off the end stop) starts warm, so it does not step
        auto upd = [this] (BiquadCoefs& cf, BiquadCoefs nw, int k) { if (isIdentity (cf) && ! isIdentity (nw)) warmMask |= 1 << k; cf = nw; };
        if (changed (prehp(), lastPh)) { lastPh = prehp(); upd (preHpC, prehp() <= 10.5f ? BiquadCoefs {} : rbj (rbjHP, sr, prehp(), 0.7071), 0); }
        if (changed (prelp(), lastPl)) { lastPl = prelp(); upd (preLpC, prelp() >= 21900.0f ? BiquadCoefs {} : rbj (rbjLP, sr, prelp(), 0.7071), 1); }
        if (changed (posthp(), lastQh)) { lastQh = posthp(); upd (postHpC, posthp() <= 10.5f ? BiquadCoefs {} : rbj (rbjHP, sr, posthp(), 0.7071), 2); }
        if (changed (postlp(), lastQl)) { lastQl = postlp(); upd (postLpC, postlp() >= 21900.0f ? BiquadCoefs {} : rbj (rbjLP, sr, postlp(), 0.7071), 3); }
        if (noWarm) { warmMask = 0; noWarm = false; }
    }
    int warmMask = 0; bool noWarm = true;
    P bands, xlo, xhi, curve[3], drive[3], bmix[3], level[3], prehp, prelp, posthp, postlp, box, boxmix, autoG, out;
    double sr = 48000; int lat = 0;
    std::array<juce::dsp::Oversampling<float>, 3> os { { { 2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true },
                                                         { 2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true },
                                                         { 2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true } } };
    FixedDelay dry; AutoGain ag; ExpSmooth outSm, boxSm, drvSm[3], mixSm[3], lvlSm[3], actSm[3];
    Biquad xf[2][12], filt[2][4];
    BiquadCoefs lo1C, hi1C, lo2C, hi2C, preHpC, preLpC, postHpC, postLpC;
    float lastLo = -1, lastHi = -1, lastPh = -1, lastPl = -1, lastQh = -1, lastQl = -1;
    float band[3][2][kSub] {};
    DcBlock bandDc[3][2]; ChanState st[3][2] {};
    std::vector<std::vector<float>> irs; std::array<std::array<Fir, 2>, 2> firs; int firSlot = 0, curBox = 0, prevBox = 0, boxFade = 0, boxFadeLen = 1;
};

// ================================================================ HAUNTED HALLS
enum HallsMode { hmRoom, hmHall, hmPlate, hmChamber, hmCathedral, hmCloud, hmGalaxy, hmReverse, hmGated, hmNonlinear, hmDarkTape, kNumHallsModes };
inline const juce::StringArray& hallsModeNames()
{
    static juce::StringArray s { "ROOM", "HALL", "PLATE", "CHAMBER", "CATHEDRAL", "CLOUD", "GALAXY", "REVERSE", "GATED", "NONLINEAR", "DARK TAPE" };
    return s;
}
struct HallsModeDef { float minMs, maxMs, apMs, apGain, modMs, modHz, dampMul, er, erScale, decayMul, inGain; };
inline const HallsModeDef& hallsMode (int m)
{
    static const HallsModeDef d[kNumHallsModes] {
        { 6,  34,  3.0f, 0.70f, 0.12f, 0.6f, 1.00f, 0.80f, 0.45f, 1.0f, 1.0f },   // ROOM
        { 19, 92,  6.0f, 0.65f, 0.45f, 0.4f, 0.90f, 0.45f, 1.00f, 1.0f, 1.0f },   // HALL
        { 5,  48,  2.2f, 0.78f, 0.25f, 0.8f, 1.60f, 0.00f, 0.50f, 1.0f, 1.2f },   // PLATE
        { 11, 58,  4.0f, 0.70f, 0.20f, 0.5f, 0.80f, 0.60f, 0.80f, 1.0f, 1.0f },   // CHAMBER
        { 38, 185, 9.0f, 0.65f, 0.60f, 0.3f, 0.70f, 0.40f, 2.00f, 1.0f, 0.9f },   // CATHEDRAL
        { 33, 160, 22.f, 0.72f, 1.60f, 0.25f, 0.85f, 0.00f, 1.00f, 1.0f, 0.9f },  // CLOUD
        { 45, 210, 16.f, 0.70f, 2.60f, 0.2f, 0.95f, 0.00f, 1.00f, 2.5f, 0.8f },   // GALAXY
        { 19, 92,  6.0f, 0.65f, 0.40f, 0.4f, 0.90f, 0.00f, 1.00f, 1.0f, 1.0f },   // REVERSE
        { 8,  48,  3.0f, 0.75f, 0.15f, 0.6f, 1.10f, 0.50f, 0.60f, 1.0f, 1.1f },   // GATED
        { 4,  20,  2.0f, 0.75f, 0.05f, 0.6f, 1.20f, 0.00f, 1.00f, 1.0f, 1.0f },   // NONLINEAR
        { 17, 88,  6.0f, 0.65f, 0.40f, 0.5f, 0.35f, 0.40f, 0.90f, 1.0f, 1.0f },   // DARK TAPE
    };
    return d[juce::jlimit (0, kNumHallsModes - 1, m)];
}

class HallsUnit : public UnitDsp
{
public:
    static constexpr int N = 16;
    Telemetry* tm = nullptr;
    void attach (juce::AudioProcessorValueTreeState& s) override
    {
        auto b = [&s] (P& p, const char* k, float d) { p.bind (s, pid (uHalls, k), d); };
        b (mode, "mode", 1); b (size, "size", 0.6f); b (decay, "decay", 3.0f); b (predelay, "predelay", 20); b (diffusion, "diffusion", 0.7f);
        b (damp, "damp", 0.4f); b (lowcut, "lowcut", 80); b (mod, "mod", 0.4f); b (width, "width", 1); b (early, "early", 0.5f);
        b (shimmer, "shimmer", 0); b (freeze, "freeze", 0);
    }
    void prepare (double sr_, int) override
    {
        sr = sr_;
        for (int i = 0; i < N; ++i) { lines[(size_t) i].prepare ((int) (0.215 * 1.6 * sr) + (int) (0.004 * sr) + 64); lfo[(size_t) i].setPhase (i * 0.071); }
        pre.prepare ((int) (1.25 * sr)); preRt.prepare ((int) (1.25 * sr));
        tapeWowR.prepare ((int) (0.01 * sr));
        for (int c = 0; c < 2; ++c) { nlAp[c].prepare ((int) (0.006 * sr * (c ? 1.13 : 1.0)), 0.6f); outLp[c].setLP (sr, 4200.0); }
        for (auto& c : ap) for (auto& a : c) a.prepare ((int) (0.024 * 1.6 * sr) + 8);
        rev.prepare ((int) (2.1 * sr));
        tapeWow.prepare ((int) (0.01 * sr));
        ps.prepare (sr, 70.0f); ps.setSemis (12.0f);
        sizeSm.setTime (sr, 300.0f); freezeSm.setTime (sr, 80.0f); widthSm.setTime (sr, 30.0f); erSm.setTime (sr, 30.0f); preSm.setTime (sr, 150.0f);
        gateEnv.set (sr, 1.0f, 40.0f); agcEnv.set (sr, 20.0f, 400.0f); gateGain.setTime (sr, 3.0f);
        wowO.setPhase (0); reset();
    }
    void reset() override
    {
        for (auto& l : lines) l.clear();
        for (auto& z : dz) z = 0;
        pre.clear(); preRt.clear(); rev.clear(); tapeWow.clear(); tapeWowR.clear(); ps.clear(); nlAp[0].clear(); nlAp[1].clear();
        for (auto& c : ap) for (auto& a : c) a.clear();
        for (auto& c : lc) c.reset();
        for (auto& c : outLp) c.reset();
        curMode = juce::jlimit (0, kNumHallsModes - 1, mode.i()); pendingMode = curMode; switchFade = 1.0f; switching = 0;
        sizeSm.snap (sizeScale()); freezeSm.snap (freeze.b() ? 1.0f : 0.0f); widthSm.snap (width()); erSm.snap (early()); preSm.snap (predelay() * 0.001f * (float) sr);
        gateEnv.reset(); agcEnv.reset(); gateGain.snap (0.0f); gateHold = 0; revPh = 0; lastLc = -1; ramp.start ((int) (sr * 0.01));
        setupMode();
    }
    void process (float* L, float* R, int n, const BlockCtx&) override
    {
        const int want = juce::jlimit (0, kNumHallsModes - 1, mode.i());
        if (want != curMode && switching == 0) { pendingMode = want; switching = 1; }
        const auto& md = hallsMode (curMode);
        sizeSm.target = sizeScale(); freezeSm.target = freeze.b() ? 1.0f : 0.0f; widthSm.target = width(); erSm.target = early() * md.er;
        preSm.target = (float) (predelay() * 0.001 * sr);
        if (snapPre) { preSm.snap (preSm.target); snapPre = false; }
        if (changed (lowcut(), lastLc)) { lastLc = lowcut(); lcC = rbj (rbjHP, sr, lowcut(), 0.7071); }
        // per-block gains: T60 per line from its current length
        const float scale = sizeSm.cur;
        // x1.18: the interpolated, modulated, damped lines lose a little each pass; measured so the tail matches DECAY
        float t60 = std::max (0.1f, decay()) * md.decayMul * 1.18f;
        if (curMode == hmGated) t60 = std::min (t60, 4.0f);
        if (curMode == hmNonlinear) t60 = 0.25f;
        for (int i = 0; i < N; ++i)
        {
            const double len = baseLen[(size_t) i] * scale / sr;
            g[(size_t) i] = (float) std::pow (10.0, -3.0 * len / t60);
            lfo[(size_t) i].setFreq (md.modHz * (0.6 + 0.09 * i), sr); lfo[(size_t) i].normalise();
        }
        const float dampHz = juce::jlimit (900.0f, 20000.0f, (float) (20000.0 * std::pow (1.0 - 0.92 * clampf (damp(), 0.0f, 1.0f), 2.0)) * md.dampMul + 600.0f);
        const float da = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * std::min ((double) dampHz, sr * 0.45) / sr);
        const float modS = clampf (mod(), 0.0f, 1.0f) * md.modMs * 0.001f * (float) sr;
        const float sh = clampf (shimmer(), 0.0f, 1.0f);
        const float apg = md.apGain * clampf (diffusion(), 0.0f, 1.0f);
        for (auto& c : ap) for (auto& a : c) a.g = apg;
        const float gateTime = (float) ((0.08 + 0.7 * clampf (size(), 0.0f, 1.0f)) * sr);
        const float revLen = (float) ((0.15 + 0.65 * clampf (size(), 0.0f, 1.0f)) * sr);
        const float slope = clampf ((decay() - 1.0f) / 4.0f, -1.0f, 1.0f);
        const float nlLen = (float) ((0.05 + 0.55 * clampf (size(), 0.0f, 1.0f)) * sr / 1.55);
        wowO.setFreq (0.7, sr); wowO.normalise();
        float pk = 0, inPk = 0;
        for (int i = 0; i < n; ++i)
        {
            const float fz = freezeSm.next(), sc = sizeSm.next(), w = widthSm.next(), er = erSm.next(), pd = preSm.next();
            const float rg = ramp.next();
            float inL = lc[0].process (L[i], lcC) * rg, inR = lc[1].process (R[i], lcC) * rg;
            inPk = std::max (inPk, std::abs (inL));
            // pre-delay
            pre.push (inL); preRt.push (inR);
            float dl = pre.readLinear ((double) pd), dr = preRt.readLinear ((double) pd);
            // early reflections (from the pre-delayed signal)
            float eL = 0, eR = 0;
            if (er > 0.001f)
                for (int k = 0; k < 8; ++k)
                {
                    const double t = (double) pd + erTap[(size_t) k] * sc;
                    eL += pre.readLinear (t) * erGain[(size_t) k];
                    eR += preRt.readLinear (t * 1.07 + 13.0) * erGain[(size_t) (7 - k)];
                }
            // input diffusion
            for (int k = 0; k < kAp; ++k) { dl = ap[0][(size_t) k].process (dl); dr = ap[1][(size_t) k].process (dr); }
            const float ing = md.inGain * (1.0f - fz) * switchFade;
            // shimmer: an octave up of the tank fed back in, kept in check by a level follower
            float inject = 0;
            if (sh > 0.001f)
            {
                const float a = agcEnv.process (lastOut);
                const float agc = a > 0.25f ? 0.25f / a : 1.0f;
                inject = ps.process (lastOut) * sh * 0.55f * agc * (1.0f - fz);
            }
            // tank
            std::array<float, N> v;
            for (int k = 0; k < N; ++k)
            {
                auto& o = lfo[(size_t) k]; o.step();
                const double d = baseLen[(size_t) k] * sc + modS * o.s * (k & 1 ? 1.0 : 0.7) + modS + 2.0;
                const float x = lines[(size_t) k].readLinear (d);
                float& z = dz[(size_t) k];
                z = x + da * (z - x);
                v[(size_t) k] = z + fz * (x - z);
            }
            float oL = 0, oR = 0;
            for (int k = 0; k < N; k += 2) { oL += v[(size_t) k] * outSign[(size_t) k]; oR += v[(size_t) k + 1] * outSign[(size_t) k + 1]; }
            oL *= 0.3f; oR *= 0.3f;
            std::array<float, N> u;
            for (int k = 0; k < N; ++k) { const float gk = g[(size_t) k]; u[(size_t) k] = v[(size_t) k] * (gk + fz * (1.0f - gk)); }
            for (int len = 1; len < N; len <<= 1)
                for (int j = 0; j < N; j += len << 1)
                    for (int k = j; k < j + len; ++k) { const float a = u[(size_t) k], b = u[(size_t) (k + len)]; u[(size_t) k] = a + b; u[(size_t) (k + len)] = a - b; }
            for (int k = 0; k < N; ++k)
            {
                float x = u[(size_t) k] * 0.25f + (((k & 1) ? dr : dl) * inSign[(size_t) k] * 0.35f + inject * 0.25f) * ing;
                if (curMode == hmDarkTape) x = fastTanh (x * 1.3f) / 1.3f;
                else if (std::abs (x) > 1.5f) x = 1.5f * fastTanh (x / 1.5f);   // safety: the tank can never run away
                lines[(size_t) k].push (x);
            }
            lastOut = 0.5f * (oL + oR);
            // mode specials
            if (curMode == hmGated)
            {
                const float e = gateEnv.process (0.5f * (inL + inR));
                if (e > 0.02f) gateHold = gateTime;
                gateGain.target = gateHold > 0 ? 1.0f : 0.0f;
                if (gateHold > 0) gateHold -= 1.0f;
                const float gg = gateGain.next();
                oL *= gg; oR *= gg;
            }
            else if (curMode == hmNonlinear)
            {
                // a dense cloud of taps over the nonlinear length, flat / rising / falling with DECAY
                float tl = 0, tr = 0;
                for (int k = 0; k < 24; ++k)
                {
                    const float pos = nlTap[(size_t) k];
                    const double t = (double) pd + pos * nlLen * sc;
                    const float gk = (1.0f + slope * (2.0f * pos - 1.0f)) * nlGain[(size_t) k];
                    const float s = (k & 1) ? preRt.readLinear (t) : pre.readLinear (t);
                    if (k & 1) tr += s * gk; else tl += s * gk;
                }
                oL = oL * 0.25f + ap2(0, tl * 0.28f); oR = oR * 0.25f + ap2(1, tr * 0.28f);
            }
            else if (curMode == hmReverse)
            {
                rev.push (0.5f * (oL + oR) * 1.4f);
                revPh += 1.0f; if (revPh >= revLen) revPh -= revLen;
                const float p2 = revPh + revLen * 0.5f >= revLen ? revPh - revLen * 0.5f : revPh + revLen * 0.5f;
                const float a1 = revPh / revLen, a2 = p2 / revLen;
                // each head reads backwards through the last chunk; its window swells then cuts (reverse-reverb shape)
                const float r1 = rev.readLinear (2.0 * revPh + 1.0) * hannWin (a1) * (0.3f + 0.7f * a1);
                const float r2 = rev.readLinear (2.0 * p2 + 1.0) * hannWin (a2) * (0.3f + 0.7f * a2);
                const float rv = (r1 + r2) * 2.0f;
                oL = rv + 0.15f * (oL - oR); oR = rv - 0.15f * (oL - oR);
            }
            else if (curMode == hmDarkTape)
            {
                wowO.step();
                tapeWow.push (oL); const float wl = tapeWow.readLinear (0.0025 * sr * (1.0 + 0.8 * wowO.s) + 2.0);
                tapeWowR.push (oR); const float wr = tapeWowR.readLinear (0.0025 * sr * (1.0 + 0.8 * wowO.s) + 2.0);
                oL = outLp[0].lp (wl); oR = outLp[1].lp (wr);
            }
            oL += eL * er * 0.35f; oR += eR * er * 0.35f;
            // width
            const float m = 0.5f * (oL + oR), s = 0.5f * (oL - oR) * w;
            float yl = (m + s) * switchFade, yr = (m - s) * switchFade;
            if (switching == 1) { switchFade -= 1.0f / (float) (0.02 * sr); if (switchFade <= 0.0f) { switchFade = 0.0f; switching = 2; } }
            else if (switching == 3) { switchFade += 1.0f / (float) (0.03 * sr); if (switchFade >= 1.0f) { switchFade = 1.0f; switching = 0; } }
            L[i] = yl; R[i] = yr;
            pk = std::max (pk, std::abs (yl) + std::abs (yr));
        }
        if (switching == 2)
        {
            curMode = pendingMode;
            for (auto& l : lines) l.clear();
            for (auto& z : dz) z = 0;
            for (auto& c : ap) for (auto& a : c) a.clear();
            rev.clear(); setupMode(); switching = 3;
        }
        if (tm) { tm->hallsLevel.store (pk * 0.5f); tm->hallsIn.store (inPk); }
    }
private:
    struct VarAp   // allpass with a fixed maximum, delay set per mode (no allocation)
    {
        Ring r; int d = 1; float g = 0.6f;
        void prepare (int maxD) { r.prepare (maxD + 4); }
        void clear() { r.clear(); }
        float process (float x) noexcept { const float z = r.at (d - 1); const float v = x + g * z; r.push (v); return z - g * v; }
    };
    float ap2 (int c, float x) noexcept { return nlAp[c].process (x); }
    float sizeScale() const { return 0.3f + 1.25f * clampf (size(), 0.0f, 1.0f); }
    void setupMode()
    {
        const auto& md = hallsMode (curMode);
        juce::Random rnd ((juce::int64) (curMode * 131 + 7));
        for (int i = 0; i < N; ++i)
        {
            const double f = (double) i / (N - 1);
            const double ms = md.minMs * std::pow ((double) md.maxMs / md.minMs, f) * (1.0 + 0.06 * (rnd.nextDouble() - 0.5));
            baseLen[(size_t) i] = ms * 0.001 * sr;
        }
        // shuffle so neighbouring lines (L/R pairs) are not similar lengths
        for (int i = N - 1; i > 0; --i) std::swap (baseLen[(size_t) i], baseLen[(size_t) rnd.nextInt (i + 1)]);
        static const float apBase[kAp] { 1.0f, 0.77f, 0.61f, 0.47f, 1.31f, 0.89f };
        for (int k = 0; k < kAp; ++k)
        {
            ap[0][(size_t) k].d = std::max (1, (int) (md.apMs * apBase[k] * 0.001 * sr));
            ap[1][(size_t) k].d = std::max (1, (int) (md.apMs * apBase[k] * 1.11 * 0.001 * sr));
        }
        for (int k = 0; k < 8; ++k)
        {
            erTap[(size_t) k] = (float) ((3.0 + 4.1 * k + 2.0 * rnd.nextDouble()) * md.erScale * 0.001 * sr);
            erGain[(size_t) k] = (k % 2 ? -1.0f : 1.0f) * (1.0f - 0.09f * (float) k);
        }
        for (int k = 0; k < N; ++k) { outSign[(size_t) k] = (rnd.nextBool() ? 1.0f : -1.0f); inSign[(size_t) k] = ((k >> 1) & 1) ? -1.0f : 1.0f; }
        for (int k = 0; k < 24; ++k) { nlTap[(size_t) k] = (float) ((k + 0.5 + 0.6 * (rnd.nextDouble() - 0.5)) / 24.0); nlGain[(size_t) k] = (rnd.nextBool() ? 1.0f : -1.0f) * (0.7f + 0.3f * rnd.nextFloat()); }
    }
    static constexpr int kAp = 6;
    P mode, size, decay, predelay, diffusion, damp, lowcut, mod, width, early, shimmer, freeze; bool snapPre = false;
    void jumpSmoothing() override { snapPre = true; }
    double sr = 48000;
    std::array<Ring, N> lines; std::array<float, N> dz {}, g {}, outSign {}, inSign {}; std::array<double, N> baseLen {}; std::array<Phasor, N> lfo;
    std::array<std::array<VarAp, kAp>, 2> ap; Allpass nlAp[2];
    Ring pre, preRt, rev, tapeWow, tapeWowR; PitchShifter ps; Biquad lc[2]; BiquadCoefs lcC; OnePole outLp[2]; float lastLc = -1;
    std::array<float, 8> erTap {}, erGain {}; std::array<float, 24> nlTap {}, nlGain {};
    ExpSmooth sizeSm, freezeSm, widthSm, erSm, preSm, gateGain; EnvFollow gateEnv, agcEnv; InRamp ramp; Phasor wowO;
    int curMode = 1, pendingMode = 1, switching = 0; float switchFade = 1.0f, gateHold = 0, revPh = 0, lastOut = 0;
};


// ================================================================ WARP (drawn tempo-synced pitch bend on the audio)
// PITCH: Signalsmith Stretch transposes and keeps the timing; its latency is reported to the rack (and the dry path is
//        delayed to match) while the unit is on in PITCH mode. The transpose is read at the lane position the audio
//        entering the analysis window belongs to, so after compensation the bend lines up with the beat.
// TAPE:  variable-speed read from a ring (pitch and time move together, like a variable-speed tape). The read delay depends only
//        on the lane position (bendlane::TapeTable), so it is identical at every lane start: no drift, ever. When the
//        line goes up before it comes down the tape needs a little head start ("lag", shown on the page).
class WarpUnit : public UnitDsp
{
public:
    Telemetry* tm = nullptr;
    const bendlane::SharedShape* shape = nullptr;
    static constexpr double kTapeSeconds = 12.0;
    void attach (juce::AudioProcessorValueTreeState& s) override
    {
        mode.bind (s, pid (uWarp, "mode"), 0); amount.bind (s, pid (uWarp, "amount"), 1); len.bind (s, pid (uWarp, "length"), 1);
        range.bind (s, pid (uWarp, "range"), 2); formant.bind (s, pid (uWarp, "formant"), 0); hostOn = s.getRawParameterValue (pid (uWarp, "on"));
    }
    void prepare (double sr_, int) override
    {
        sr = sr_;
        // shorter blocks than presetDefault (120 / 30 ms): the transpose can move every 12.5 ms and the latency stays ~50 ms
        st.configure (2, (int) std::lround (sr * 0.05), (int) std::lround (sr * 0.0125), false);
        lat = st.inputLatency() + st.outputLatency();
        seekLen = std::min (lat, st.outputSeekLength (1.0f));
        for (auto& b : seekBuf) b.assign ((size_t) seekLen + 1, 0.0f);
        for (auto& r : dryRing) r.prepare (lat + kSub + 16);
        for (auto& r : tapeRing) r.prepare ((int) (sr * kTapeSeconds) + 64);
        reader.prepare (sr);
        reset();
    }
    void reset() override
    {
        st.reset();
        for (auto& r : tapeRing) r.clear();
        reader.reset(); tableSeen = 0xffffffffu; lastMode = -1; tableScale = -1.0f;
        primePitch = true; declL = declR = 0; lastOutL = lastOutR = 0;
    }
    int latency() const override { return latent.load (std::memory_order_relaxed) || (hostOn != nullptr && hostOn->load() > 0.5f) ? lat : 0; }   // reported on enable, not after the first block
    int pitchLatency() const noexcept { return lat; }
    void clearLines() override { for (auto& r : dryRing) r.clear(); dryXf = 0; dryFrom = dryD; }
    int maxLatency() const noexcept { return lat; }
    void delayDry (float* L, float* R, int n) override
    {
        const int d = latency();
        if (d != dryD) { dryFrom = dryD; dryD = d; dryXf = dryXfLen; }   // latency switched (WARP on/off, mode): crossfade, no jump
        for (int i = 0; i < n; ++i)
        {
            dryRing[0].push (L[i]); dryRing[1].push (R[i]);
            float l = d > 0 ? dryRing[0].at (d) : L[i], r = d > 0 ? dryRing[1].at (d) : R[i];
            if (dryXf > 0)
            {
                const float w = 0.5f - 0.5f * std::cos (kPi * (float) dryXf / (float) dryXfLen);   // 1 -> 0
                l += w * ((dryFrom > 0 ? dryRing[0].at (dryFrom) : L[i]) - l); r += w * ((dryFrom > 0 ? dryRing[1].at (dryFrom) : R[i]) - r);
                --dryXf;
            }
            L[i] = l; R[i] = r;
        }
    }
    void idle (const float*, const float*, int) override { latent.store (false, std::memory_order_relaxed); if (tm) tm->warpPos.store (-1.0f); }
    void process (float* L, float* R, int n, const BlockCtx& ctx) override
    {
        if (shape != nullptr && shape->fetch (cur, seen)) tableSeen = 0xffffffffu;
        if (cur.n == 0) cur.add (0, 0);
        const int md = juce::jlimit (0, 1, mode.i());
        const bool switched = lastMode >= 0 && md != lastMode;
        if (md != lastMode) { if (md == 0) primePitch = true; reader.reset(); lastMode = md; }
        latent.store (md == 0, std::memory_order_relaxed);
        const float scale = bendlane::rangeOf (range.i()) * clampf (amount(), 0.0f, 1.0f);
        const double lenBeats = bendlane::barsOf (len.i()) * 4.0, spb = ctx.samplesPerBeat;
        double phase = 0; float semis = 0;
        if (md == 0)
        {
            st.setFormantFactor (1.0f, formant.b());
            const double inLat = (double) st.inputLatency();
            if (primePitch)
            {
                // just switched on / to PITCH: pre-roll the stretcher with the input it missed (the dry ring always has it),
                // so the output starts with sound instead of 50 ms of silence
                st.reset();
                float* sk[2] { seekBuf[0].data(), seekBuf[1].data() };
                for (int c = 0; c < 2; ++c) for (int j = 0; j < seekLen; ++j) sk[c][j] = dryRing[(size_t) c].at (seekLen - 1 - j);
                st.outputSeek (sk, seekLen);
                primePitch = false;
            }
            for (int i = 0; i < n; ++i) { tapeRing[0].push (L[i]); tapeRing[1].push (R[i]); }   // keep the tape fed for a switch to TAPE
            for (int off = 0; off < n; off += kChunk)
            {
                const int m = std::min (kChunk, n - off);
                phase = positiveMod ((ctx.ppqAt (off) - inLat / spb) / lenBeats, 1.0);
                semis = cur.valueAt (phase) * scale;
                st.setTransposeFactor (std::exp2 (semis / 12.0f));
                float* in[2] { L + off, R + off }; float* out[2] { outL.data() + off, outR.data() + off };
                st.process (in, m, out, m);
            }
            std::copy (outL.begin(), outL.begin() + n, L); std::copy (outR.begin(), outR.begin() + n, R);
            phase = positiveMod ((ctx.ppqAt (n) - (double) lat / spb) / lenBeats, 1.0);   // what is heard now
            semis = cur.valueAt (phase) * scale;
            if (tm) tm->warpLagMs.store (1000.0f * (float) lat / (float) sr);
        }
        else
        {
            if (tableSeen != seen || std::abs (scale - tableScale) > 1.0e-5f) { table.build (cur, scale); tableSeen = seen; tableScale = scale; }
            const double cyc = lenBeats * spb, maxD = tapeRing[0].maxDelay() - 4.0;
            const double head = std::min (maxD, (double) table.maxO * cyc);
            for (int i = 0; i < n; ++i)
            {
                tapeRing[0].push (L[i]); tapeRing[1].push (R[i]);
                phase = positiveMod (ctx.ppqAt (i) / lenBeats, 1.0);
                const double D = juce::jlimit (0.0, maxD, head - (double) table.at (phase) * cyc);
                reader.advance (D);
                L[i] = reader.tap (tapeRing[0]); R[i] = reader.tap (tapeRing[1]);
            }
            semis = cur.valueAt (phase) * scale;
            if (tm) tm->warpLagMs.store (1000.0f * (float) (head / sr));
        }
        // mode switch: the new output starts where the old one ended (decaying offset, ~3 ms)
        if (switched) { declL = lastOutL - L[0]; declR = lastOutR - R[0]; }
        if (std::abs (declL) + std::abs (declR) > 1.0e-6f)
        {
            const float k = (float) std::exp (-1.0 / (0.003 * sr));
            for (int i = 0; i < n; ++i) { L[i] += declL; R[i] += declR; declL *= k; declR *= k; }
        }
        lastOutL = L[n - 1]; lastOutR = R[n - 1];
        if (tm)
        {
            float pk = 0; for (int i = 0; i < n; i += 4) pk = std::max (pk, std::abs (L[i]));
            tm->warpPos.store ((float) phase); tm->warpSemis.store (semis); tm->warpLevel.store (pk);
        }
    }
private:
    static constexpr int kChunk = 32;
    std::atomic<float>* hostOn = nullptr; P mode, amount, len, range, formant;
    double sr = 48000; int lat = 0;
    signalsmith::stretch::SignalsmithStretch<float> st { 0x6a057 };
    std::array<Ring, 2> dryRing, tapeRing; DelayReader reader;
    std::array<float, kSub> outL {}, outR {};
    bendlane::Shape cur; uint32_t seen = 0xffffffffu, tableSeen = 0xffffffffu; float tableScale = -1.0f;
    bendlane::TapeTable table; int lastMode = -1;
    std::atomic<bool> latent { false };
    std::array<std::vector<float>, 2> seekBuf; int seekLen = 0; bool primePitch = true;
    float declL = 0, declR = 0, lastOutL = 0, lastOutR = 0;
    int dryD = 0, dryFrom = 0, dryXf = 0; static constexpr int dryXfLen = 240;
};

// ================================================================ parameters
inline void addBoxParameters (Layout& l)
{
    auto T = [] (int u, const char* s) { return juce::String ("FX ") + unitInfo (u).title + " " + s; };
    auto dB = [] (float lo, float hi) { return juce::NormalisableRange<float> (lo, hi); };
    // WARP
    addChoice (l, pid (uWarp, "mode"), T (uWarp, "Mode"), { "PITCH", "TAPE" }, 0);
    addPct (l, pid (uWarp, "amount"), T (uWarp, "Amount"), 1.0f);
    addChoice (l, pid (uWarp, "length"), T (uWarp, "Length"), bendlane::lengthNames(), 1);
    addChoice (l, pid (uWarp, "range"), T (uWarp, "Range"), bendlane::rangeNames(), 2);
    addBool (l, pid (uWarp, "formant"), T (uWarp, "Keep Formants"), false);
    // CRACKLE
    addChoice (l, pid (uCrackle, "bands"), T (uCrackle, "Bands"), { "1 BAND", "2 BANDS", "3 BANDS" }, 0);
    addFloat (l, pid (uCrackle, "xlo"), T (uCrackle, "Low Crossover"), skewRange (40.0f, 2000.0f, 250.0f), 220.0f, "Hz", 0);
    addFloat (l, pid (uCrackle, "xhi"), T (uCrackle, "High Crossover"), skewRange (400.0f, 12000.0f, 2500.0f), 2800.0f, "Hz", 0);
    static const int defCurve[3] { ccTube, ccFuzz, ccCracked };
    for (int k = 0; k < 3; ++k)
    {
        const juce::String pre = "b" + juce::String (k + 1) + "_", nm = "Band " + juce::String (k + 1) + " ";
        addChoice (l, pid (uCrackle, (pre + "curve").toRawUTF8()), T (uCrackle, (nm + "Curve").toRawUTF8()), crackleCurveNames(), defCurve[k]);
        addPct (l, pid (uCrackle, (pre + "drive").toRawUTF8()), T (uCrackle, (nm + "Drive").toRawUTF8()), 0.4f);
        addPct (l, pid (uCrackle, (pre + "mix").toRawUTF8()), T (uCrackle, (nm + "Mix").toRawUTF8()), 1.0f);
        addFloat (l, pid (uCrackle, (pre + "level").toRawUTF8()), T (uCrackle, (nm + "Level").toRawUTF8()), dB (-24.0f, 12.0f), 0.0f, "dB", 1);
    }
    addFloat (l, pid (uCrackle, "prehp"), T (uCrackle, "Pre Low Cut"), skewRange (10.0f, 2000.0f, 150.0f), 10.0f, "Hz", 0);
    addFloat (l, pid (uCrackle, "prelp"), T (uCrackle, "Pre High Cut"), skewRange (1000.0f, 22000.0f, 6000.0f), 22000.0f, "Hz", 0);
    addFloat (l, pid (uCrackle, "posthp"), T (uCrackle, "Post Low Cut"), skewRange (10.0f, 2000.0f, 150.0f), 10.0f, "Hz", 0);
    addFloat (l, pid (uCrackle, "postlp"), T (uCrackle, "Post High Cut"), skewRange (1000.0f, 22000.0f, 6000.0f), 22000.0f, "Hz", 0);
    addChoice (l, pid (uCrackle, "box"), T (uCrackle, "Box"), crackleBoxNames(), 0);
    addPct (l, pid (uCrackle, "boxmix"), T (uCrackle, "Box Mix"), 1.0f);
    addBool (l, pid (uCrackle, "auto"), T (uCrackle, "Auto Gain"), true);
    addFloat (l, pid (uCrackle, "out"), T (uCrackle, "Output"), dB (-24.0f, 12.0f), 0.0f, "dB", 1);
    // RETRO
    addPct (l, pid (uRetro, "magic"), T (uRetro, "Magic"), 0.5f);
    addBool (l, pid (uRetro, "noise_on"), T (uRetro, "Noise On"), true);
    addPct (l, pid (uRetro, "noise"), T (uRetro, "Noise"), 0.3f);
    addChoice (l, pid (uRetro, "ntype"), T (uRetro, "Noise Type"), retroNoiseNames(), 0);
    addFloat (l, pid (uRetro, "ntone"), T (uRetro, "Noise Tone"), juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f, "", 2);
    addBool (l, pid (uRetro, "nfollow"), T (uRetro, "Noise Follows Input"), false);
    addBool (l, pid (uRetro, "wobble_on"), T (uRetro, "Wobble On"), true);
    addPct (l, pid (uRetro, "wobble"), T (uRetro, "Wobble"), 0.3f);
    addPct (l, pid (uRetro, "flutter"), T (uRetro, "Flutter"), 0.3f);
    addBool (l, pid (uRetro, "dist_on"), T (uRetro, "Distort On"), true);
    addPct (l, pid (uRetro, "dist"), T (uRetro, "Distort"), 0.3f);
    addChoice (l, pid (uRetro, "dtype"), T (uRetro, "Distort Type"), retroDistNames(), 0);
    addBool (l, pid (uRetro, "digi_on"), T (uRetro, "Digital On"), false);
    addPct (l, pid (uRetro, "digi"), T (uRetro, "Digital"), 0.3f);
    addBool (l, pid (uRetro, "space_on"), T (uRetro, "Space On"), false);
    addPct (l, pid (uRetro, "space"), T (uRetro, "Space"), 0.3f);
    addBool (l, pid (uRetro, "mag_on"), T (uRetro, "Magnetic On"), false);
    addPct (l, pid (uRetro, "mag"), T (uRetro, "Magnetic"), 0.3f);
    addFloat (l, pid (uRetro, "tone"), T (uRetro, "Tone"), juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f, "", 2);
    addFloat (l, pid (uRetro, "out"), T (uRetro, "Output"), dB (-24.0f, 12.0f), 0.0f, "dB", 1);
    addFattenParameters (l);   // FATTEN (FxFatten.h; same ids, names, ranges and position as before)
    // HAUNTED HALLS
    addChoice (l, pid (uHalls, "mode"), T (uHalls, "Mode"), hallsModeNames(), 1);
    addPct (l, pid (uHalls, "size"), T (uHalls, "Size"), 0.6f);
    addFloat (l, pid (uHalls, "decay"), T (uHalls, "Decay"), skewRange (0.1f, 60.0f, 3.0f), 3.0f, "s", 2);
    addFloat (l, pid (uHalls, "predelay"), T (uHalls, "Pre-delay"), skewRange (0.0f, 500.0f, 60.0f), 20.0f, "ms", 0);
    addPct (l, pid (uHalls, "diffusion"), T (uHalls, "Diffusion"), 0.7f);
    addPct (l, pid (uHalls, "damp"), T (uHalls, "Damping"), 0.4f);
    addFloat (l, pid (uHalls, "lowcut"), T (uHalls, "Low Cut"), skewRange (20.0f, 1000.0f, 150.0f), 80.0f, "Hz", 0);
    addPct (l, pid (uHalls, "mod"), T (uHalls, "Modulation"), 0.4f);
    addPct (l, pid (uHalls, "width"), T (uHalls, "Width"), 1.0f);
    addPct (l, pid (uHalls, "early"), T (uHalls, "Early"), 0.5f);
    addPct (l, pid (uHalls, "shimmer"), T (uHalls, "Shimmer"), 0.0f);
    addBool (l, pid (uHalls, "freeze"), T (uHalls, "Freeze"), false);
}

} // namespace gpfx
#endif
