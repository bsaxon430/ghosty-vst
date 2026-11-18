#pragma once
// Ghost FX rack: RHYTHM units - GHOST GATE, GHOST GRID, HAUNT EDIT. All tempo-synced to the host ppq / bpm.
#include "FxPatterns.h"
#include "FxSpaceDsp.h"

namespace gpfx {

// ================================================================ GHOST GATE
class GateUnit : public UnitDsp
{
public:
    GateStore* store = nullptr; Telemetry* tm = nullptr;
    void attach (juce::AudioProcessorValueTreeState& s) override
    {
        pattern.bind (s, pid (uGate, "pattern"), 1); smooth.bind (s, pid (uGate, "smooth"), 0.2f);
        time.bind (s, pid (uGate, "time"), 1); len.bind (s, pid (uGate, "length"), 1);
    }
    void prepare (double sr_, int) override
    {
        sr = sr_;
        for (auto* r : { &rl, &rr }) r->prepare ((int) (sr * 6.0));
        reader.prepare (sr); volSm.setTime (sr, 3.0f); timeSm.setTime (sr, 20.0f);
        reset();
    }
    // rings keep their history across reset(): idle() records while the unit is off, so time effects can reach back
    void reset() override { reader.reset(); volSm.snap (1.0f); timeSm.snap (time()); lastStep = lastPat = -1; }
    void idle (const float* L, const float* R, int n) override { for (int i = 0; i < n; ++i) { rl.push (L[i]); rr.push (R[i]); } }
    void process (float* L, float* R, int n, const BlockCtx& c) override
    {
        if (store == nullptr) return;
        const int pat = juce::jlimit (0, kGatePatterns - 1, pattern.i() - 1);
        static const double beatsFor[] { 2.0, 4.0, 8.0 };
        const double beats = beatsFor[juce::jlimit (0, 2, len.i())];
        const double stepLen = beats / kGateSteps * c.samplesPerBeat;
        volSm.setTime (sr, 0.3f + 30.0f * smooth() * smooth());
        timeSm.target = time();
        double pos = 0;
        for (int i = 0; i < n; ++i)
        {
            rl.push (L[i]); rr.push (R[i]);
            pos = positiveMod (c.ppqAt (i), beats) / beats;
            const double p16 = pos * kGateSteps;
            const int step = juce::jlimit (0, kGateSteps - 1, (int) p16);
            const double oStep = (p16 - step) * stepLen;
            if (step != lastStep || pat != lastPat)
            {
                lastStep = step; lastPat = pat;
                mode = store->steps[(size_t) pat][(size_t) step].load (std::memory_order_relaxed);
                runStart = step; while (runStart > 0 && store->steps[(size_t) pat][(size_t) (runStart - 1)].load (std::memory_order_relaxed) == mode) --runStart;
                runEnd = step; while (runEnd < kGateSteps - 1 && store->steps[(size_t) pat][(size_t) (runEnd + 1)].load (std::memory_order_relaxed) == mode) ++runEnd;
            }
            const double oRun = (step - runStart) * stepLen + oStep, runLen = (runEnd - runStart + 1) * stepLen;
            reader.advance (mode == tmNormal ? 0.0 : timeModeDelay (mode, oRun, runLen, stepLen, oStep));
            const float ta = timeSm.next();
            float sl = L[i], sr2 = R[i];
            if (ta > 0.0005f && (mode != tmNormal || reader.wOld > 0))
            {
                sl += ta * (reader.tap (rl) - sl); sr2 += ta * (reader.tap (rr) - sr2);
            }
            volSm.target = store->vol (pat, pos);
            const float v = volSm.next();
            L[i] = sl * v; R[i] = sr2 * v;
        }
        if (tm) { tm->gatePos.store ((float) pos); tm->gatePattern.store (pat); }
    }
private:
    P pattern, smooth, time, len;
    double sr = 48000; Ring rl, rr; DelayReader reader; ExpSmooth volSm, timeSm;
    int lastStep = -1, lastPat = -1, mode = 0, runStart = 0, runEnd = 0;
};

// ================================================================ GHOST GRID
class GridUnit : public UnitDsp
{
public:
    GridStore* store = nullptr; Telemetry* tm = nullptr;
    void attach (juce::AudioProcessorValueTreeState& s) override
    {
        pattern.bind (s, pid (uGrid, "pattern"), 1); steps.bind (s, pid (uGrid, "steps"), 0);
        for (int r = 0; r < kGridRows; ++r) amt[(size_t) r].bind (s, pid (uGrid, gridRow (r).key), gridRow (r).def);
    }
    void prepare (double sr_, int) override
    {
        sr = sr_;
        for (auto* r : { &rl, &rr }) r->prepare ((int) (sr * 6.0));
        for (auto* r : { &dl, &dr }) r->prepare ((int) (sr * 3.0));
        for (auto* r : { &cl, &cr }) r->prepare ((int) (sr / 30.0) + 8);
        reader.prepare (sr); verb.prepare (sr, 1.6f); verb.diffuse = true;
        for (auto& e : en) e.setTime (sr, 4.0f);
        sendD.setTime (sr, 4.0f); sendV.setTime (sr, 4.0f); stutG.setTime (sr, 1.0f);
        dlp[0].setLP (sr, 4500); dlp[1].setLP (sr, 4500);
        reset();
    }
    void reset() override
    {
        dl.clear(); dr.clear(); cl.clear(); cr.clear(); reader.reset(); verb.clear();
        for (auto& e : en) { e.snap (0); }
        sendD.snap (0); sendV.snap (0); stutG.snap (1);
        svf[0].reset(); svf[1].reset(); lastStep = -1; lastPat = -1; holdL = holdR = 0; holdC = 0; tail = 0;
        dlp[0].reset(); dlp[1].reset(); clp[0].reset(); clp[1].reset();
    }
    void idle (const float* L, const float* R, int n) override { for (int i = 0; i < n; ++i) { rl.push (L[i]); rr.push (R[i]); } }
    void process (float* L, float* R, int n, const BlockCtx& c) override
    {
        if (store == nullptr) return;
        const int pat = juce::jlimit (0, kGridPatterns - 1, pattern.i() - 1);
        const int nSteps = steps.i() == 1 ? 32 : 16;
        const double stepLen = 4.0 / nSteps * c.samplesPerBeat;
        std::array<float, kGridRows> a; for (int r = 0; r < kGridRows; ++r) a[(size_t) r] = amt[(size_t) r]();
        // send effects settings
        const double dTime = std::min (dl.maxDelay() - 4, 0.75 * c.samplesPerBeat);
        const float dFb = 0.15f + 0.72f * a[grDelay];
        verb.set (0.35f + 0.6f * a[grVerb], 0.6f + 5.0f * a[grVerb] * a[grVerb], 6500.0f, 0.3f, false);
        verb.updateGains (verb.sizeSm.cur);
        const float tonalHz = 440.0f * std::pow (2.0f, ((36.0f + 48.0f * a[grTonal]) - 69.0f) / 12.0f);
        const double combD = juce::jlimit (2.0, cl.maxDelay() - 2, sr / tonalHz);
        double pos = 0;
        int activeMask = 0;
        for (int i = 0; i < n; ++i)
        {
            rl.push (L[i]); rr.push (R[i]);
            pos = positiveMod (c.ppqAt (i), 4.0) / 4.0;
            const double ps = pos * nSteps;
            const int st = juce::jlimit (0, nSteps - 1, (int) ps);
            const double oStep = (ps - st) * stepLen;
            if (st != lastStep || pat != lastPat || nSteps != lastN)
            {
                lastStep = st; lastPat = pat; lastN = nSteps; timeRow = -1;
                for (int r = 0; r < kGridRows; ++r) on[(size_t) r] = store->cell (pat, r, st);
                for (int r = grLoop; r <= grTapeStop; ++r)
                    if (on[(size_t) r]) { timeRow = r; break; }
                if (timeRow >= 0)
                {
                    runStart = st; while (runStart > 0 && store->cell (pat, timeRow, runStart - 1)) --runStart;
                    runEnd = st; while (runEnd < nSteps - 1 && store->cell (pat, timeRow, runEnd + 1)) ++runEnd;
                }
            }
            for (int r = 0; r < kGridRows; ++r) if (on[(size_t) r]) activeMask |= (1 << r);
            // ---- time rows
            double D = 0; float stutTarget = 1.0f, revMix = 1.0f;
            if (timeRow >= 0)
            {
                const double oRun = (st - runStart) * stepLen + oStep, runLen = (runEnd - runStart + 1) * stepLen;
                switch (timeRow)
                {
                    case grLoop:
                    {
                        static const double lens[] { 1.0, 0.5, 0.25, 0.125 };
                        const double Lb = lens[juce::jlimit (0, 3, (int) std::lround ((1.0f - a[grLoop]) * 3.0f))] * c.samplesPerBeat;
                        D = oRun - std::fmod (oRun, std::max (16.0, Lb)); break;
                    }
                    case grStutter:
                    {
                        const double Ls = std::max (16.0, stepLen * (0.5 - 0.4 * a[grStutter]));
                        D = oRun - std::fmod (oRun, Ls);
                        stutTarget = std::fmod (oRun, Ls) < Ls * 0.62 ? 1.0f : 0.0f; break;
                    }
                    case grScratch: D = timeModeDelay (tmScratch, oRun, runLen, stepLen, oStep, a[grScratch]); break;
                    case grReverse: D = 2.0 * oRun; revMix = a[grReverse]; break;
                    case grTapeStop: D = timeModeDelay (tmStop, oRun, runLen, stepLen, oStep, a[grTapeStop]); break;
                    default: break;
                }
            }
            reader.advance (D);
            float xl = L[i], xr = R[i];
            if (timeRow >= 0 || reader.wOld > 0)
            {
                const float tl = reader.tap (rl), tr = reader.tap (rr);
                xl += revMix * (tl - xl); xr += revMix * (tr - xr);
            }
            stutG.target = stutTarget; const float sg = stutG.next(); xl *= sg; xr *= sg;
            // ---- insert rows (filter, crush, tonal) with click-free enables
            for (int r : { (int) grFilter, (int) grCrush, (int) grTonal }) en[(size_t) r].target = on[(size_t) r] ? 1.0f : 0.0f;
            const float eF = en[grFilter].next(), eC = en[grCrush].next(), eT = en[grTonal].next();
            if (eF > 1.0e-4f)
            {
                if (on[grFilter] && eF < 1.0e-3f) { svf[0].reset(); svf[1].reset(); }
                if ((i & 15) == 0 && on[grFilter])
                {
                    const double runLen = std::max (1.0, (runEndOf (pat, grFilter, st, nSteps) - runStartOf (pat, grFilter, st) + 1) * stepLen);
                    const double oRun = (st - runStartOf (pat, grFilter, st)) * stepLen + oStep;
                    const double u = juce::jlimit (0.0, 1.0, oRun / runLen);
                    const double target = 9000.0 * std::pow (0.02, a[grFilter]);
                    svf[0].set (sr, 14000.0 * std::pow (target / 14000.0, u), 2.2); svf[1] = svfCopy (svf[1], svf[0]);
                }
                float lp, bp, hp;
                svf[0].tick (xl, lp, bp, hp); const float fl = lp;
                svf[1].tick (xr, lp, bp, hp); const float fr = lp;
                xl += eF * (fl - xl); xr += eF * (fr - xr);
            }
            if (eC > 1.0e-4f)
            {
                const float k = a[grCrush];
                const int hold = 1 + (int) (k * k * 24.0f);
                if (++holdC >= hold) { holdC = 0; holdL = xl; holdR = xr; }
                const float q = std::pow (2.0f, 11.0f - 8.0f * k);
                const float cl2 = std::round (holdL * q) / q, cr2 = std::round (holdR * q) / q;
                xl += eC * (cl2 - xl); xr += eC * (cr2 - xr);
            }
            if (eT > 1.0e-4f)
            {
                const float yl = cl.read (combD), yr = cr.read (combD);
                cl.push (xl + 0.9f * clp[0].lp (yl)); cr.push (xr + 0.9f * clp[1].lp (yr));
                xl += eT * (0.5f * xl + 0.6f * yl - xl); xr += eT * (0.5f * xr + 0.6f * yr - xr);
            }
            else { cl.push (0.0f); cr.push (0.0f); }
            // ---- send rows (delay / verb throws keep their tails)
            sendD.target = on[grDelay] ? 1.0f : 0.0f; sendV.target = on[grVerb] ? 1.0f : 0.0f;
            const float sD = sendD.next(), sV = sendV.next();
            float outL = xl, outR = xr;
            if (sD > 1.0e-4f || tail > 1.0e-5f || sV > 1.0e-4f)
            {
                const float yl = dl.read (dTime), yr = dr.read (dTime);
                dl.push (xl * sD + dFb * dlp[1].lp (yr)); dr.push (xr * sD * 0.3f + dFb * dlp[0].lp (yl));
                float vl, vr; verb.process (xl * sV, xr * sV, vl, vr);
                outL += yl * 0.8f + vl * 0.9f; outR += yr * 0.8f + vr * 0.9f;
                tail = tail * 0.9995f + std::abs (yl) + std::abs (yr) + std::abs (vl) + std::abs (vr);
            }
            else { dl.push (0.0f); dr.push (0.0f); }
            L[i] = outL; R[i] = outR;
        }
        if (tm) { tm->gridPos.store ((float) pos); tm->gridPattern.store (pat); tm->gridActive.store (activeMask); }
    }
private:
    static Svf svfCopy (Svf state, const Svf& coeffs) { state.g = coeffs.g; state.k = coeffs.k; state.a1 = coeffs.a1; state.a2 = coeffs.a2; state.a3 = coeffs.a3; return state; }
    int runStartOf (int pat, int row, int st) const { int s = st; while (s > 0 && store->cell (pat, row, s - 1)) --s; return s; }
    int runEndOf (int pat, int row, int st, int nSteps) const { int s = st; while (s < nSteps - 1 && store->cell (pat, row, s + 1)) ++s; return s; }
    P pattern, steps; std::array<P, kGridRows> amt;
    double sr = 48000; Ring rl, rr, dl, dr, cl, cr; DelayReader reader; Fdn8 verb;
    std::array<ExpSmooth, kGridRows> en; ExpSmooth sendD, sendV, stutG;
    std::array<bool, kGridRows> on {};
    Svf svf[2]; OnePole dlp[2], clp[2];
    int lastStep = -1, lastPat = -1, lastN = 16, timeRow = -1, runStart = 0, runEnd = 0, holdC = 0;
    float holdL = 0, holdR = 0, tail = 0;
};

// ================================================================ HAUNT EDIT
class HauntUnit : public UnitDsp
{
public:
    PadStore* pads = nullptr; Telemetry* tm = nullptr;
    std::atomic<int> uiPad { -1 };   // pad held with the mouse (-1 = none)
    void attach (juce::AudioProcessorValueTreeState& s) override
    {
        padParam.bind (s, pid (uHaunt, "pad"), 0); sync.bind (s, pid (uHaunt, "sync"), 1);
    }
    void prepare (double sr_, int) override
    {
        sr = sr_;
        for (auto* r : { &rl, &rr }) r->prepare ((int) (sr * 6.0));
        reader.prepare (sr); env.setTime (sr, 2.0f); gateSm.setTime (sr, 1.2f);
        reset();
    }
    void reset() override
    {
        reader.reset(); env.snap (0); gateSm.snap (1); held = -1; gPad = -1; o = 0;
        svf[0].reset(); svf[1].reset();
    }
    void idle (const float* L, const float* R, int n) override { for (int i = 0; i < n; ++i) { rl.push (L[i]); rr.push (R[i]); } }
    void process (float* L, float* R, int n, const BlockCtx& c) override
    {
        if (pads == nullptr) return;
        const int ui = uiPad.load (std::memory_order_relaxed);
        const int want = ui >= 0 ? ui : padParam.i() - 1;
        if (want != held)
        {
            held = want;
            if (held >= 0) start (held, c);
        }
        env.setTime (sr, held >= 0 ? 2.0f : 12.0f);
        env.target = held >= 0 ? 1.0f : 0.0f;
        const double spb = c.samplesPerBeat;
        const double Lsl = std::max (16.0, padDivs()[(size_t) juce::jlimit (0, (int) padDivs().size() - 1, def.div)].beats * spb);
        for (int i = 0; i < n; ++i)
        {
            rl.push (L[i]); rr.push (R[i]);
            const float e = env.next();
            if (gPad < 0) continue;
            float gl = L[i], gr = R[i];
            gesture (gl, gr, L[i], R[i], Lsl, spb, i);
            L[i] += e * (gl - L[i]); R[i] += e * (gr - R[i]);
            o += 1.0;
            if (held < 0 && e < 1.0e-4f) { gPad = -1; reader.reset(); }
        }
        if (tm)
        {
            tm->hauntPad.store (held); tm->hauntEnv.store (env.cur);
            tm->hauntProgress.store (gPad >= 0 ? (float) std::fmod (o / Lsl, 1.0) : 0.0f);
        }
    }
private:
    void start (int pad, const BlockCtx& c)
    {
        def = pads->get (pad); gPad = pad; seed = hash32 ((uint32_t) (++starts * 2654435761u));
        o = 0; tapeD = 0; holdC = 0;
        const double beats = padDivs()[(size_t) juce::jlimit (0, (int) padDivs().size() - 1, def.div)].beats;
        const bool timeType = def.type == htStutter || def.type == htBuild || def.type == htGlitch || def.type == htReverse || def.type == htPitchDrop || def.type == htGate;
        if (sync.b() && timeType)
        {
            const double grid = std::min (beats, 1.0) * c.samplesPerBeat;
            o = std::min (positiveMod (c.ppq, std::min (beats, 1.0)) * c.samplesPerBeat, grid);
            o = std::min (o, rl.maxDelay() * 0.25);
        }
        if (def.type == htLpSweep || def.type == htHpSweep) { svf[0].reset(); svf[1].reset(); }
    }
    void gesture (float& gl, float& gr, float dryL, float dryR, double L, double spb, int i)
    {
        const float m = def.motion;
        double D = 0; float g = 1.0f;
        switch (def.type)
        {
            case htStutter:
            {
                const double ph = std::fmod (o, L); D = o - ph;
                const double duty = 1.0 - m * 0.8;
                if (duty < 0.999) g = (float) juce::jlimit (0.0, 1.0, (duty * L - ph) / (0.0015 * sr));
                break;
            }
            case htBuild:
            {
                const double segLen = 4.0 * spb * (1.0 - 0.5 * m) / 4.0;
                const int seg = std::min (4, (int) (o / segLen));
                const double Lk = std::max (16.0, L / std::pow (2.0, seg)), oo = o - seg * segLen;
                D = oo - std::fmod (oo, Lk); break;
            }
            case htGate:
            {
                const double ph = std::fmod (o, L) / L, duty = 0.15 + 0.7 * m;
                gateSm.target = ph < duty ? 1.0f : 0.0f; g = gateSm.next(); break;
            }
            case htGlitch:
            {
                const auto k = (uint32_t) (o / L); const double oo = std::fmod (o, L);
                const float h = hashFloat (seed + k * 7919u);
                const int act = h < m ? 1 + (int) (hash32 (seed ^ (k * 104729u)) % 4u) : 0;
                if (act == 1) D = L;
                else if (act == 2) D = 2.0 * oo;
                else if (act == 3) D = oo * 0.5;
                gateSm.target = act == 4 ? 0.0f : 1.0f; g = gateSm.next();
                break;
            }
            case htReverse: { const double oo = std::fmod (o, L); D = 2.0 * oo; break; }
            case htPitchDrop:
            {
                const double k = std::floor (o / L), oo = o - k * L;
                const double sp = std::pow (0.5, k * (0.25 + 0.75 * m) * 0.5);
                D = std::min (o - oo * sp, rl.maxDelay() * 0.9); break;
            }
            case htTapeStop:
            {
                const double sp = std::pow (std::max (0.0, 1.0 - o / L), 1.0 + m * 2.0);
                tapeD = std::min (tapeD + (1.0 - sp), rl.maxDelay() * 0.9); D = tapeD;
                g = (float) std::min (1.0, sp * 5.0); break;
            }
            case htLpSweep: case htHpSweep:
            {
                const double u = std::min (1.0, o / L);
                if ((i & 15) == 0 || o < 1.0)
                {
                    const double f = def.type == htLpSweep ? 18000.0 * std::pow (120.0 / 18000.0, u) : 20.0 * std::pow (6000.0 / 20.0, u);
                    svf[0].set (sr, f, 0.7 + m * 7.0); svf[1].set (sr, f, 0.7 + m * 7.0);
                }
                float lp, bp, hp;
                svf[0].tick (dryL, lp, bp, hp); gl = def.type == htLpSweep ? lp : hp;
                svf[1].tick (dryR, lp, bp, hp); gr = def.type == htLpSweep ? lp : hp;
                return;
            }
            case htCrush:
            {
                const double u = std::min (1.0, o / L);
                const int hold = 1 + (int) (u * u * (10.0 + m * 50.0));
                if (++holdC >= hold) { holdC = 0; hl = dryL; hr = dryR; }
                const float q = std::pow (2.0f, (float) (15.0 - u * (8.0 + m * 5.0)));
                gl = std::round (hl * q) / q; gr = std::round (hr * q) / q; return;
            }
            default: break;
        }
        reader.advance (D);
        gl = (D < 0.001 && reader.wOld <= 0 ? dryL : reader.tap (rl)) * g;
        gr = (D < 0.001 && reader.wOld <= 0 ? dryR : reader.tap (rr)) * g;
    }
    P padParam, sync;
    double sr = 48000, o = 0, tapeD = 0; Ring rl, rr; DelayReader reader; ExpSmooth env, gateSm;
    PadDef def; int held = -1, gPad = -1, holdC = 0; uint32_t seed = 1, starts = 0; float hl = 0, hr = 0;
    Svf svf[2];
};

} // namespace gpfx
