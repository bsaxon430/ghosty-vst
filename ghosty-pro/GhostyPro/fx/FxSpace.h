#pragma once
// Ghost FX rack: SPACE units - CHORUS, CRYSTAL, ECTO ECHO, SPECTRAL SPACE, SHIMMER.
#include "FxSpaceDsp.h"

namespace gpfx {

// ================================================================ CHORUS (3 voices per side)
class ChorusUnit : public UnitDsp
{
public:
    Telemetry* tm = nullptr;
    void attach (juce::AudioProcessorValueTreeState& s) override
    {
        rate.bind (s, pid (uChorus, "rate"), 0.4f); depth.bind (s, pid (uChorus, "depth"), 0.5f); delay.bind (s, pid (uChorus, "delay"), 12);
        fb.bind (s, pid (uChorus, "fb")); width.bind (s, pid (uChorus, "width"), 1);
#if ! GPFX_BOX
        // FX RACK (round 3): 4 TAPS with a second delay time, LPF / HPF on the wet (late parameters; their defaults = the classic chorus, bit for bit)
        if (s.getParameter (pid (uChorus, "taps")) != nullptr)
        { taps.bind (s, pid (uChorus, "taps"), 0); delay2.bind (s, pid (uChorus, "delay2"), 18.0f); lpf.bind (s, pid (uChorus, "lpf"), 20000.0f); hpf.bind (s, pid (uChorus, "hpf"), 20.0f); }
#endif
    }
    void prepare (double sr_, int) override
    {
        sr = sr_; for (auto* r : { &rl, &rr }) r->prepare ((int) (sr * 0.08));
        delSm.setTime (sr, 80.0f); depSm.setTime (sr, 40.0f);
#if ! GPFX_BOX
        del2Sm.setTime (sr, 80.0f); lpfSm.setTime (sr, 20.0f); hpfSm.setTime (sr, 20.0f); fltAmt.setTime (sr, 10.0f);
#endif
        reset();
    }
    void reset() override
    {
        rl.clear(); rr.clear(); osc.setPhase (0); yl1 = yr1 = 0; delSm.snap (delay()); depSm.snap (depth()); ramp.start ((int) (sr * 0.01));
#if ! GPFX_BOX
        del2Sm.snap (delay2()); lpfSm.snap (std::log2 (juce::jlimit (1000.0f, 20000.0f, lpf()))); hpfSm.snap (std::log2 (juce::jlimit (20.0f, 2000.0f, hpf())));
        fltAmt.snap (filterWanted() ? 1.0f : 0.0f); for (auto& f : flt) f.reset();
        curTaps = taps.i(); tapFade = 0; wetG = 1.0f;
#endif
    }
#if ! GPFX_BOX
    void process (float* L, float* R, int n, const BlockCtx& ctx) override
    {
        if (taps.p != nullptr && (taps.i() == 1 || curTaps == 1 || tapFade > 0 || fltAmt.cur > 0.0f || filterWanted())) { processPro (L, R, n, ctx); return; }
#else
    void process (float* L, float* R, int n, const BlockCtx&) override
    {
#endif
        delSm.target = delay(); depSm.target = depth();
        if (snapNext) { delSm.snap (delSm.target); snapNext = false; }
        const float f = fb() * 0.85f, w = width();
        osc.setFreq (rate(), sr); osc.normalise();
        // voice offsets (120 degrees apart) and the right channel's extra offset (width * 90 degrees)
        double cl[3], sl[3], cr[3], sr2[3];
        for (int v = 0; v < 3; ++v)
        {
            const double a = juce::MathConstants<double>::twoPi * v / 3.0, b = a + juce::MathConstants<double>::halfPi * w;
            cl[v] = std::cos (a); sl[v] = std::sin (a); cr[v] = std::cos (b); sr2[v] = std::sin (b);
        }
        float pk = 0;
        for (int i = 0; i < n; ++i)
        {
            osc.step();
            const float dms = delSm.next(), dp = depSm.next();
            const float rg = ramp.next();
            rl.push ((L[i] + f * yl1) * rg); rr.push ((R[i] + f * yr1) * rg);
            float yl = 0, yr = 0;
            const double ms2s = 0.001 * sr;
            for (int v = 0; v < 3; ++v)
            {
                const float ml = (float) (osc.s * cl[v] + osc.c * sl[v]), mr = (float) (osc.s * cr[v] + osc.c * sr2[v]);
                yl += rl.read (std::max (0.5f, dms * (1.0f + 0.85f * dp * ml)) * ms2s);
                yr += rr.read (std::max (0.5f, dms * (1.0f + 0.85f * dp * mr)) * ms2s);
            }
            yl *= 0.4f; yr *= 0.4f; yl1 = yl; yr1 = yr;
            const float m = 0.5f * (yl + yr), s = 0.5f * (yl - yr) * (0.4f + 1.2f * w);
            L[i] = 0.5f * L[i] + (m + s); R[i] = 0.5f * R[i] + (m - s);
            pk = std::max (pk, std::abs (yl));
        }
        if (tm) tm->chorusLevel.store (pk);
    }
private:
    void jumpSmoothing() override { snapNext = true; }
    bool snapNext = false;
    P rate, depth, delay, fb, width; double sr = 48000; Ring rl, rr; Phasor osc; InRamp ramp; float yl1 = 0, yr1 = 0; ExpSmooth delSm, depSm;
#if ! GPFX_BOX
    // ---- FX RACK (round 3): the upgraded chorus. Runs only once 4 TAPS or a wet filter is in use (the classic loop above stays
    // untouched, so every older sound renders bit for bit). 4 TAPS: taps 1 / 2 at DELAY, taps 3 / 4 at DELAY 2, their wobbles a
    // quarter cycle apart, taps 1 + 3 left, 2 + 4 right; FEEDBACK from the sum. LPF / HPF (one-pole pairs, TPT) on the wet only,
    // faded in / out over 10 ms. Switching 3 VOICES <-> 4 TAPS dips the wet for 2 x 10 ms (the delay lines keep running).
    P taps, delay2, lpf, hpf; ExpSmooth del2Sm, lpfSm, hpfSm, fltAmt;
    struct Tpt1 { float s = 0; float lp (float x, float g) noexcept { const float v = (x - s) * g / (1.0f + g); const float y = v + s; s = y + v; return y; } void reset() { s = 0; } };
    std::array<Tpt1, 4> flt;   // [L lp, R lp, L hp, R hp]
    int curTaps = 0, tapFade = 0; float wetG = 1.0f;
    bool filterWanted() const { return lpf.p != nullptr && (lpf() < 19990.0f || hpf() > 20.5f); }
    void processPro (float* L, float* R, int n, const BlockCtx&)
    {
        delSm.target = delay(); depSm.target = depth(); del2Sm.target = delay2();
        if (snapNext) { delSm.snap (delSm.target); del2Sm.snap (del2Sm.target); snapNext = false; }
        lpfSm.target = std::log2 (juce::jlimit (1000.0f, 20000.0f, lpf())); hpfSm.target = std::log2 (juce::jlimit (20.0f, 2000.0f, hpf()));
        fltAmt.target = filterWanted() ? 1.0f : 0.0f;
        const int want = taps.i() == 1 ? 1 : 0;
        if (want != curTaps && tapFade == 0) tapFade = 2 * (int) (0.010 * sr);
        const int half = (int) (0.010 * sr);
        const float f = fb() * 0.85f, w = width();
        osc.setFreq (rate(), sr); osc.normalise();
        double cl[4], sl[4], cr[4], sr2[4];
        for (int v = 0; v < 4; ++v)
        {   // classic: 3 voices 120 degrees apart (right + width * 90); 4 TAPS: a quarter cycle apart
            const double a = curTaps == 1 ? juce::MathConstants<double>::halfPi * v : juce::MathConstants<double>::twoPi * v / 3.0, b = a + juce::MathConstants<double>::halfPi * w;
            cl[v] = std::cos (a); sl[v] = std::sin (a); cr[v] = std::cos (b); sr2[v] = std::sin (b);
        }
        float pk = 0;
        for (int i = 0; i < n; ++i)
        {
            osc.step();
            const float dms = delSm.next(), dp = depSm.next(), d2 = del2Sm.next();
            const float rg = ramp.next();
            rl.push ((L[i] + f * yl1) * rg); rr.push ((R[i] + f * yr1) * rg);
            float yl = 0, yr = 0;
            const double ms2s = 0.001 * sr;
            if (curTaps == 1)
            {
                for (int v = 0; v < 4; ++v)
                {
                    const float base = v < 2 ? dms : d2;
                    const float m = (float) (osc.s * cl[v] + osc.c * sl[v]);
                    const float y = (v & 1 ? rr : rl).read (std::max (0.5f, base * (1.0f + 0.85f * dp * m)) * ms2s);
                    if (v & 1) yr += y; else yl += y;
                }
                yl *= 0.5f; yr *= 0.5f;
            }
            else
            {
                for (int v = 0; v < 3; ++v)
                {
                    const float ml = (float) (osc.s * cl[v] + osc.c * sl[v]), mr = (float) (osc.s * cr[v] + osc.c * sr2[v]);
                    yl += rl.read (std::max (0.5f, dms * (1.0f + 0.85f * dp * ml)) * ms2s);
                    yr += rr.read (std::max (0.5f, dms * (1.0f + 0.85f * dp * mr)) * ms2s);
                }
                yl *= 0.4f; yr *= 0.4f;
            }
            yl1 = yl; yr1 = yr;
            // the wet filters (always running, faded in / out)
            const float a = fltAmt.next();
            const float gl = std::tan (kPi * std::exp2 (lpfSm.next()) / (float) sr), gh = std::tan (kPi * std::exp2 (hpfSm.next()) / (float) sr);
            const float fl = flt[0].lp (yl, gl), fr = flt[1].lp (yr, gl);
            const float hl = fl - flt[2].lp (fl, gh), hr = fr - flt[3].lp (fr, gh);
            yl += a * (hl - yl); yr += a * (hr - yr);
            // 3 VOICES <-> 4 TAPS: the wet dips out, switches, comes back
            if (tapFade > 0)
            {
                --tapFade;
                if (tapFade == half) curTaps = want;
                wetG = 0.5f + 0.5f * std::cos (kPi * (float) std::abs (tapFade - half) / (float) half);   // 1 -> 0 -> 1
                wetG = 1.0f - wetG;
                if (tapFade == 0) wetG = 1.0f;
            }
            const float m = 0.5f * (yl + yr) * wetG, s = 0.5f * (yl - yr) * (0.4f + 1.2f * w) * wetG;
            L[i] = 0.5f * L[i] + (m + s); R[i] = 0.5f * R[i] + (m - s);
            pk = std::max (pk, std::abs (yl));
        }
        if (fltAmt.target <= 0.0f && fltAmt.cur < 1.0e-6f) fltAmt.snap (0.0f);
        if (tm) tm->chorusLevel.store (pk);
    }
#endif
};
#if ! GPFX_BOX
inline void addChorusProParameters (juce::AudioProcessorValueTreeState::ParameterLayout& l)
{
    auto T = [] (const char* s) { return juce::String ("FX ") + unitInfo (uChorus).title + " " + s; };
    addChoice (l, pid (uChorus, "taps"), T ("Taps"), { "3 VOICES", "4 TAPS" }, 0);
    addFloat (l, pid (uChorus, "delay2"), T ("Delay 2"), juce::NormalisableRange<float> (2.0f, 30.0f), 18.0f, "ms", 1);
    addFloat (l, pid (uChorus, "lpf"), T ("Wet Low Pass"), skewRange (1000.0f, 20000.0f, 6000.0f), 20000.0f, "Hz", 0);
    addFloat (l, pid (uChorus, "hpf"), T ("Wet High Pass"), skewRange (20.0f, 2000.0f, 200.0f), 20.0f, "Hz", 0);
}
#endif

// ================================================================ CRYSTAL (granular reverse / pitched splices)
class CrystalUnit : public UnitDsp
{
public:
    Telemetry* tm = nullptr;
    void attach (juce::AudioProcessorValueTreeState& s) override
    {
        pitch.bind (s, pid (uCrystal, "pitch"), 12); splice.bind (s, pid (uCrystal, "splice"), 180); delay.bind (s, pid (uCrystal, "delay"), 250);
        fb.bind (s, pid (uCrystal, "fb"), 0.4f); reverse.bind (s, pid (uCrystal, "reverse"), 0.5f); spread.bind (s, pid (uCrystal, "spread"), 0.6f);
    }
    void prepare (double sr_, int) override
    {
        sr = sr_; for (auto& b : buf) b.prepare ((int) (sr * 4.0));
        for (auto& f : fbLp) f.setLP (sr, 9000);
        for (auto& h : fbHp) h.setLP (sr, 90);
        reset();
    }
    void reset() override
    {
        for (auto& b : buf) b.clear();
        for (auto& c : grains) for (auto& g : c) g.active = false;
        for (auto& f : fbLp) f.reset();
        for (auto& h : fbHp) h.reset();
        spawnIn = 0; seed = 12345; yl1 = yr1 = 0; ramp.start ((int) (sr * 0.01));
    }
    void process (float* L, float* R, int n, const BlockCtx&) override
    {
        const float f = fb() * 0.95f;
        float pk = 0;
        for (int i = 0; i < n; ++i)
        {
            if (--spawnIn <= 0) spawn();
            float y[2] { 0, 0 };
            for (int c = 0; c < 2; ++c)
                for (auto& g : grains[(size_t) c])
                {
                    if (! g.active) continue;
                    const float u = (float) g.age / (float) g.len;
                    const float w = hannWin (u);
                    y[c] += buf[(size_t) c].read (g.pos) * w;
                    g.pos += 1.0 - g.rate;
                    if (++g.age >= g.len || g.pos > buf[(size_t) c].maxDelay() - 4 || g.pos < 0) g.active = false;
                }
            const float fl = fbHp[0].hp (fbLp[0].lp (fastTanh (y[0]))), fr = fbHp[1].hp (fbLp[1].lp (fastTanh (y[1])));
            const float rg = ramp.next();
            buf[0].push ((L[i] + f * fl) * rg); buf[1].push ((R[i] + f * fr) * rg);
            L[i] = y[0]; R[i] = y[1];
            pk = std::max (pk, std::abs (y[0]));
        }
        if (tm) tm->crystalLevel.store (pk);
    }
private:
    struct Grain { double pos = 0, rate = 1; int len = 1, age = 0; bool active = false; };
    float rnd() { seed = hash32 (seed + 0x9e3779b9u); return (float) (seed & 0xffffff) / 16777215.0f; }
    void spawn()
    {
        const int len = std::max (64, (int) (splice() * 0.001 * sr));
        spawnIn = len / 2;
        const double ratio = std::pow (2.0, pitch() / 12.0), D = delay() * 0.001 * sr;
        const float rev = reverse(), spr = spread();
        float jit = rnd(), rv = rnd();
        for (int c = 0; c < 2; ++c)
        {
            if (c == 1 && rnd() < spr) { jit = rnd(); rv = rnd(); }
            Grain* slot = nullptr;
            for (auto& g : grains[(size_t) c]) if (! g.active) { slot = &g; break; }
            if (slot == nullptr) continue;
            const bool isRev = rv < rev;
            slot->rate = isRev ? -ratio : ratio;
            double start = D + jit * 0.3 * len + 4.0 + (isRev ? 0.0 : std::max (0.0, (ratio - 1.0) * len));
            const double travel = (1.0 + ratio) * len;
            start = std::min (start, buf[(size_t) c].maxDelay() - travel - 8.0);
            slot->pos = std::max (1.0, start); slot->len = len; slot->age = 0; slot->active = true;
        }
    }
    P pitch, splice, delay, fb, reverse, spread;
    double sr = 48000; std::array<Ring, 2> buf; std::array<std::array<Grain, 6>, 2> grains;
    OnePole fbLp[2], fbHp[2]; InRamp ramp; int spawnIn = 0; uint32_t seed = 1; float yl1 = 0, yr1 = 0;
};

// ================================================================ ECTO ECHO
enum EchoMode { emTape, emAnalog, emBbd, emDigital };
class EchoUnit : public UnitDsp
{
public:
    Telemetry* tm = nullptr;
    void attach (juce::AudioProcessorValueTreeState& s) override
    {
        mode.bind (s, pid (uEcho, "mode")); sync.bind (s, pid (uEcho, "sync"), 1); timeMs.bind (s, pid (uEcho, "time"), 375);
        div.bind (s, pid (uEcho, "div"), 7); fb.bind (s, pid (uEcho, "fb"), 0.45f); ping.bind (s, pid (uEcho, "ping"), 1);
        width.bind (s, pid (uEcho, "width"), 1); lowcut.bind (s, pid (uEcho, "lowcut"), 120); highcut.bind (s, pid (uEcho, "highcut"), 6500);
        character.bind (s, pid (uEcho, "char"), 0.5f); duck.bind (s, pid (uEcho, "duck"));
    }
    void prepare (double sr_, int) override
    {
        sr = sr_; for (auto* r : { &rl, &rr }) r->prepare ((int) (sr * 4.2));
        timeSm.setTime (sr, 120.0f); duckEnv.set (sr, 6.0f, 280.0f); duckSm.setTime (sr, 30.0f); driftSm.setTime (sr, 400.0f);
        reset();
    }
    void reset() override
    {
        rl.clear(); rr.clear(); for (auto& c : flt) for (auto& b : c) b.reset();
        for (auto& o : modeLp) o.reset();
        timeSm.snap ((float) targetDelay (120.0)); wowO.setPhase (0); flO.setPhase (0.3); ramp.start ((int) (sr * 0.01)); duckEnv.reset(); duckSm.snap (1); driftSm.snap (0); lastLc = lastHc = -1; lastMode = -1; noise = 1;
    }
    void process (float* L, float* R, int n, const BlockCtx& c) override
    {
        const int md = juce::jlimit (0, 3, mode.i());
        timeSm.setTime (sr, md == emDigital ? 40.0f : (md == emTape ? 220.0f : 120.0f));
        timeSm.target = (float) targetDelay (c.bpm);
        if (snapNext) { timeSm.snap (timeSm.target); snapNext = false; }
        updateFilters (md);
        const float f = fb(), ch = character(), dk = duck(), w = width();
        const bool pp = ping.b();
        const double wowD = (md == emTape ? 0.0024 : md == emBbd ? 0.0007 : md == emAnalog ? 0.0003 : 0.0) * sr * ch;
        const double flD = (md == emTape ? 0.00016 : 0.0) * sr * ch;
        float inPk = 0;
        wowO.setFreq (0.55, sr); flO.setFreq (6.1, sr); wowO.normalise(); flO.normalise();
        float pk = 0;
        for (int i = 0; i < n; ++i)
        {
            wowO.step(); flO.step();
            if ((i & 63) == 0) { noise = hash32 (noise + 1013904223u); driftSm.target = ((float) (noise & 0xffff) / 65535.0f - 0.5f) * (md == emTape ? 1.0f : 0.0f); }
            const float drift = driftSm.next();
            const double d = timeSm.next();
            const double wow = wowD * wowO.s + flD * flO.s + wowD * 0.6 * drift;
            float yl = rl.read (d + wow), yr = rr.read (d * (pp ? 1.0 : 1.0) + wow * (md == emBbd ? -1.0 : 1.0));
            yl = colour (0, yl, md, ch); yr = colour (1, yr, md, ch);
            const float inMono = 0.5f * (L[i] + R[i]);
            inPk = std::max (inPk, std::abs (inMono));
            const float rg = ramp.next();
            if (pp) { rl.push (inMono * rg + f * yr); rr.push (f * yl); }
            else    { rl.push (L[i] * rg + f * yl); rr.push (R[i] * rg + f * yr); }
            const float e = duckEnv.process (inMono);
            duckSm.target = 1.0f - dk * clampf (e * 4.0f, 0.0f, 1.0f);
            const float dg = duckSm.next();
            const float m = 0.5f * (yl + yr), s = 0.5f * (yl - yr) * w;
            L[i] = (m + s) * dg; R[i] = (m - s) * dg;
            pk = std::max (pk, std::abs (yl) + std::abs (yr));
        }
        if (tm) { tm->echoMs.store ((float) (timeSm.cur * 1000.0 / sr)); tm->echoLevel.store (pk * 0.5f); if (inPk > tm->echoIn.load()) tm->echoIn.store (inPk); }   // held until the editor reads it
    }
private:
    double targetDelay (double bpm) const
    {
        double secs = timeMs() * 0.001;
        if (sync.b()) secs = echoDivs()[(size_t) juce::jlimit (0, (int) echoDivs().size() - 1, div.i())].beats * 60.0 / juce::jlimit (20.0, 400.0, bpm);
        return juce::jlimit (8.0, rl.maxDelay() - 0.004 * sr, secs * sr);
    }
    float colour (int c, float y, int md, float ch) noexcept
    {
        y = flt[c][0].process (y, hpC); y = flt[c][1].process (y, lpC);
        switch (md)
        {
            case emTape:   y = modeLp[c].lp (y); return fastTanh (y * (1.0f + ch)) / (1.0f + 0.6f * ch);
            case emAnalog: y = modeLp[c].lp (y); return softClip (y * (1.0f + 0.8f * ch)) * (1.0f + 0.3f * ch);
            case emBbd:    { y = modeLp[c].lp (y); noise = hash32 (noise + 7u); const float hiss = ((float) (noise & 0xffff) / 65535.0f - 0.5f) * 4.0e-4f * ch;
                             return fastTanh (y * 1.15f) / 1.1f + hiss; }
            default:       return clampf (y, -2.0f, 2.0f);
        }
    }
    void updateFilters (int md)
    {
        const float lc = lowcut(), hc = highcut();
        const double secs = timeSm.target / sr;
        const double bbdCut = juce::jlimit (1400.0, 9000.0, 7500.0 * std::sqrt (0.08 / std::max (0.01, secs)));
        if (changed (lc, lastLc) || changed (hc, lastHc) || md != lastMode || (md == emBbd && std::abs (bbdCut - lastBbd) > 50))
        {
            lastLc = lc; lastHc = hc; lastMode = md; lastBbd = bbdCut;
            hpC = rbj (rbjHP, sr, lc, 0.6); lpC = rbj (rbjLP, sr, hc, 0.6);
            const double mc = md == emTape ? 7800.0 : md == emAnalog ? 5200.0 : md == emBbd ? bbdCut : 20000.0;
            for (auto& o : modeLp) o.setLP (sr, mc);
        }
    }
    void jumpSmoothing() override { snapNext = true; }
    bool snapNext = false;
    P mode, sync, timeMs, div, fb, ping, width, lowcut, highcut, character, duck;
    double sr = 48000, lastBbd = 0; Phasor wowO, flO; InRamp ramp; Ring rl, rr; Biquad flt[2][2]; BiquadCoefs hpC, lpC; OnePole modeLp[2];
    ExpSmooth timeSm, duckSm, driftSm; EnvFollow duckEnv; float lastLc = -1, lastHc = -1; int lastMode = -1; uint32_t noise = 1;
};

// ================================================================ SPECTRAL SPACE (blendable ambience)
class SpaceUnit : public UnitDsp
{
public:
    Telemetry* tm = nullptr;
    void attach (juce::AudioProcessorValueTreeState& s) override
    {
        size.bind (s, pid (uSpace, "size"), 0.5f); blend.bind (s, pid (uSpace, "blend"), 0.6f); decay.bind (s, pid (uSpace, "decay"), 2.5f);
        tone.bind (s, pid (uSpace, "tone")); mod.bind (s, pid (uSpace, "mod"), 0.4f); glimmer.bind (s, pid (uSpace, "glimmer"), 0.2f);
        width.bind (s, pid (uSpace, "width"), 1); predelay.bind (s, pid (uSpace, "predelay"), 20); duck.bind (s, pid (uSpace, "duck"));
    }
    void prepare (double sr_, int) override
    {
        sr = sr_; fdn.prepare (sr, 2.6f);
        for (auto* r : { &pl, &pr }) r->prepare ((int) (sr * 0.6));
        sh12.prepare (sr, 70.0f); sh12.setSemis (12.0f); sh7.prepare (sr, 90.0f); sh7.setSemis (7.0f);
        for (auto& o : toneLp) o.setLP (sr, 1500);
        glimHp.setLP (sr, 400); duckEnv.set (sr, 8.0f, 300.0f); duckSm.setTime (sr, 40.0f); sizeSm.setTime (sr, 300.0f);
        reset();
    }
    void reset() override
    {
        fdn.clear(); pl.clear(); pr.clear(); sh12.clear(); sh7.clear(); glimHp.reset();
        for (auto& o : toneLp) o.reset();
        duckEnv.reset(); duckSm.snap (1); sizeSm.snap (size()); glim = 0; ramp.start ((int) (sr * 0.01)); erInit = false;
    }
    void process (float* L, float* R, int n, const BlockCtx&) override
    {
        sizeSm.target = size();
        const float sz = size(), dec = decay() * (0.6f + 0.8f * sz), md = mod(), gm = glimmer(), bl = blend(), tn = tone(), w = width(), dk = duck();
        fdn.modRate = 0.15f + md * 1.1f;
        fdn.set (sz, dec, 16000.0f * std::pow (0.12f, 0.5f - 0.5f * tn), md * 1.6f, false);
        fdn.updateGains (fdn.sizeSm.cur);
        const double pd = juce::jlimit (1.0, pl.maxDelay() - 2, predelay() * 0.001 * sr);
        const float gl = dbToGain (-7.0f * tn), gh = dbToGain (7.0f * tn);
        // early reflections: 10 taps that grow with size (delays ramp linearly across the block)
        const float sEnd = sizeSm.target + (sizeSm.cur - sizeSm.target) * (float) std::exp (-(double) n * sizeSm.a);
        double tapEnd[10], tapStep[10];
        for (int k = 0; k < 10; ++k)
        {
            const double tms = (4.0 + 7.3 * k + 1.7 * (k * k % 5)) * (0.3 + 1.4 * sEnd);
            tapEnd[k] = std::min (pd + tms * 0.001 * sr, pl.maxDelay() - 2);
            if (! erInit) tapNow[k] = tapEnd[k];
            tapStep[k] = (tapEnd[k] - tapNow[k]) / n;
        }
        erInit = true;
        static const float gk[10] { 0.8f, 0.656f, 0.538f, 0.441f, 0.362f, 0.296f, 0.243f, 0.199f, 0.163f, 0.134f };
        float pk = 0;
        for (int i = 0; i < n; ++i)
        {
            const float rg = ramp.next();
            pl.push (L[i] * rg); pr.push (R[i] * rg);
            sizeSm.next();
            const float xl = pl.read (pd), xr = pr.read (pd);
            float el = 0, er = 0;
            for (int k = 0; k < 10; ++k)
            {
                tapNow[k] += tapStep[k];
                if ((k & 1) == 0) el += gk[k] * pl.readLinear (tapNow[k]); else er += gk[k] * pr.readLinear (tapNow[k]);
            }
            el *= 0.45f; er *= 0.45f;
            float vl, vr; fdn.process (xl + 0.4f * er, xr + 0.4f * el, vl, vr, glim);
            const float mono = glimHp.hp (0.5f * (vl + vr));
            const float g12 = sh12.process (mono), g7 = sh7.process (mono);
            glim = gm * 0.55f * (0.7f * g12 + 0.3f * g7);
            float yl = el * (1.0f - bl) * 1.4f + vl * bl + glim * 0.5f;
            float yr = er * (1.0f - bl) * 1.4f + vr * bl + glim * 0.5f;
            const float ll = toneLp[0].lp (yl), lr = toneLp[1].lp (yr);
            yl = ll * gl + (yl - ll) * gh; yr = lr * gl + (yr - lr) * gh;
            const float e = duckEnv.process (0.5f * (L[i] + R[i]));
            duckSm.target = 1.0f - dk * clampf (e * 4.0f, 0.0f, 1.0f);
            const float dg = duckSm.next();
            const float m = 0.5f * (yl + yr), sd = 0.5f * (yl - yr) * w;
            L[i] = (m + sd) * dg; R[i] = (m - sd) * dg;
            pk = std::max (pk, std::abs (L[i]));
        }
        if (tm) tm->spaceLevel.store (pk);
    }
private:
    P size, blend, decay, tone, mod, glimmer, width, predelay, duck;
    double sr = 48000; double tapNow[10] {}; bool erInit = false; InRamp ramp; Fdn8 fdn; Ring pl, pr; PitchShifter sh12, sh7; OnePole toneLp[2], glimHp; EnvFollow duckEnv; ExpSmooth duckSm, sizeSm; float glim = 0;
};

// ================================================================ SHIMMER REVERB
class ShimmerUnit : public UnitDsp
{
public:
    Telemetry* tm = nullptr;
    void attach (juce::AudioProcessorValueTreeState& s) override
    {
        size.bind (s, pid (uShimmer, "size"), 0.7f); decay.bind (s, pid (uShimmer, "decay"), 5); damp.bind (s, pid (uShimmer, "damp"), 0.4f);
        shimmer.bind (s, pid (uShimmer, "shimmer"), 0.5f); predelay.bind (s, pid (uShimmer, "predelay"), 30); mod.bind (s, pid (uShimmer, "mod"), 0.5f);
        width.bind (s, pid (uShimmer, "width"), 1); freeze.bind (s, pid (uShimmer, "freeze"));
    }
    void prepare (double sr_, int) override
    {
        sr = sr_; fdn.prepare (sr, 2.8f);
        for (auto* r : { &pl, &pr }) r->prepare ((int) (sr * 0.5));
        shift.prepare (sr, 80.0f); shift.setSemis (12.0f); shHp.setLP (sr, 600); shLp.setLP (sr, 9000);
        reset();
    }
    void reset() override { fdn.clear(); pl.clear(); pr.clear(); shift.clear(); shHp.reset(); shLp.reset(); fbS = 0; ramp.start ((int) (sr * 0.01)); }
    void process (float* L, float* R, int n, const BlockCtx&) override
    {
        const bool fz = freeze.b();
        fdn.modRate = 0.25f + mod() * 0.6f;
        fdn.set (size(), decay(), 20000.0f * std::pow (1200.0f / 20000.0f, damp()), mod() * 2.0f, fz);
        fdn.updateGains (fdn.sizeSm.cur);
        const double pd = juce::jlimit (1.0, pl.maxDelay() - 2, predelay() * 0.001 * sr);
        const float sh = shimmer(), w = width();
        float pk = 0;
        for (int i = 0; i < n; ++i)
        {
            const float rg = ramp.next();
            pl.push (L[i] * rg); pr.push (R[i] * rg);
            float vl, vr; fdn.process (pl.read (pd), pr.read (pd), vl, vr, fbS);
            const float s = shLp.lp (shHp.hp (shift.process (0.5f * (vl + vr))));
            fbS = sh * 0.62f * s * (fz ? 0.0f : 1.0f);
            const float yl = vl + sh * 0.35f * s, yr = vr + sh * 0.35f * s;
            const float m = 0.5f * (yl + yr), sd = 0.5f * (yl - yr) * w;
            L[i] = m + sd; R[i] = m - sd;
            pk = std::max (pk, std::abs (L[i]));
        }
        if (tm) tm->shimmerLevel.store (pk);
    }
private:
    P size, decay, damp, shimmer, predelay, mod, width, freeze;
    double sr = 48000; InRamp ramp; Fdn8 fdn; Ring pl, pr; PitchShifter shift; OnePole shHp, shLp; float fbS = 0;
};

} // namespace gpfx
