#pragma once
// Ghost FX rack: COLOUR units - WRECKER (analog saturation + digital section), TUBE GLOW, SOUL CRUSH, PHANTOM FILTER.
#include "FxCore.h"

namespace gpfx {

// Matches output loudness to input loudness (slow RMS), used for auto gain compensation.
struct AutoGain
{
    float inE = 0, outE = 0, a = 0.001f; ExpSmooth g;
    void prepare (double sr, float ms = 300.0f) { a = 1.0f - (float) std::exp (-1.0 / (ms * 0.001 * sr)); g.setTime (sr, 120.0f); reset(); }
    void reset() { inE = outE = 0; g.snap (1.0f); }
    void feedIn (float l, float r) noexcept { inE += a * (0.5f * (l * l + r * r) - inE); }
    float next (float l, float r) noexcept
    {
        outE += a * (0.5f * (l * l + r * r) - outE);
        if (inE > 1.0e-8f && outE > 1.0e-8f) g.target = clampf (std::sqrt (inE / outE), 0.06f, 4.0f);
        return g.next();
    }
};

// ================================================================ WRECKER
enum WreckStyle { wsTape, wsTube, wsConsole, wsTransistor, wsPentode, wsFuzz, wsFold, wsCrush, wsDecimate, kNumWreckStyles };
inline const juce::StringArray& wreckStyleNames()
{
    static juce::StringArray s { "TAPE", "TUBE", "CONSOLE", "TRANSISTOR", "PENTODE", "FUZZ", "FOLD", "CRUSH", "DECIMATE" };
    return s;
}
// The static transfer curve of each style at linear drive gain g (also drawn by the editor).
inline float wreckShape (int style, float x, float g, float amount01) noexcept
{
    const float d = x * g;
    switch (style)
    {
        case wsTape:       return fastTanh (d * 0.9f) * 1.05f - 0.04f * fastTanh (d * d * d * 0.2f);
        case wsTube:       { const float b = 0.32f; return (fastTanh (d + b) - fastTanh (b)) * 1.15f; }
        case wsConsole:    { const float y = clampf (d, -1.5f, 1.5f); return (y - y * y * y * (4.0f / 27.0f)) * 1.12f; }
        case wsTransistor: { const float d2 = d * d; return d / std::sqrt (std::sqrt (1.0f + d2 * d2)); }
        case wsPentode:    { const float s1 = fastTanh (1.8f * d + 0.28f) - fastTanh (0.28f); const float s2 = s1 * 1.6f;
                             const float q2 = s2 * s2; return s2 / std::sqrt (std::sqrt (1.0f + q2 * q2)); }
        case wsFuzz:       return d > 0 ? 1.0f - std::exp (-3.0f * d) : -(1.0f - std::exp (2.2f * d)) * 0.82f;
        case wsFold:       return std::sin (d * 1.3f) * 0.9f;
        case wsCrush:      { const float q = std::pow (2.0f, 15.0f - 13.0f * amount01); return std::round (x * q) / q; }
        default:           return x;
    }
}

class WreckerUnit : public UnitDsp
{
public:
    Telemetry* tm = nullptr;
    void attach (juce::AudioProcessorValueTreeState& s) override
    {
        style.bind (s, pid (uWreck, "style")); drive.bind (s, pid (uWreck, "drive"), 0.3f); tone.bind (s, pid (uWreck, "tone"));
        lowcut.bind (s, pid (uWreck, "lowcut"), 10); highcut.bind (s, pid (uWreck, "highcut"), 22000);
        thump.bind (s, pid (uWreck, "thump")); steep.bind (s, pid (uWreck, "steep")); punish.bind (s, pid (uWreck, "punish"));
        autoG.bind (s, pid (uWreck, "auto"), 1); out.bind (s, pid (uWreck, "out"));
    }
    void prepare (double sr_, int) override
    {
        sr = sr_;
        os.initProcessing ((size_t) kSub); os.reset();
        lat = (int) std::lround (os.getLatencyInSamples());
        dry.prepare (lat);
        driveSm.setTime (sr * 4.0, 20.0f); compSm.setTime (sr * 4.0, 20.0f); outSm.setTime (sr, 20.0f); toneSm.setTime (sr, 20.0f);
        ag.prepare (sr); tilt[0].setLP (sr, 800); tilt[1].setLP (sr, 800);
        for (auto& d : dc) d.prepare (sr * 4.0);
        for (auto& c : consoleLp) c.setLP (sr * 4.0, 220.0);
        reset();
    }
    void reset() override
    {
        os.reset(); ag.reset();
        for (auto& t : tilt) t.reset();
        for (auto& d : dc) d.reset();
        for (auto& c : consoleLp) c.reset();
        for (auto& c : cut) for (auto& b : c) b.reset();
        for (auto& o : opLc) o.reset();
        for (auto& o : opHc) o.reset();
        for (auto& b : thumpBq) b.reset();
        curStyle = prevStyle = style.i(); fade = 0; holdC = 0; hl = hr = 0;
        driveSm.snap (targetDrive()); compSm.snap (std::pow (targetDrive(), -0.15f)); outSm.snap (1.0f); toneSm.snap (tone());
        lastLc = lastHc = -1; lastSteep = lastThump = -1;
    }
    int latency() const override { return lat; }
    void delayDry (float* L, float* R, int n) override { dry.process (L, R, n); }
    void clearLines() override { dry.clear(); }
    void process (float* L, float* R, int n, const BlockCtx&) override
    {
        const int st = juce::jlimit (0, kNumWreckStyles - 1, style.i());
        if (st != curStyle) { prevStyle = curStyle; curStyle = st; fade = (int) (sr * 4.0 * 0.02); fadeLen = fade; }
        driveSm.target = targetDrive(); compSm.target = std::pow (driveSm.target, -0.15f);
        const float amt = juce::jlimit (0.0f, 1.0f, drive() + (punish.b() ? 0.3f : 0.0f));
        updateFilters();
        for (int i = 0; i < n; ++i) ag.feedIn (L[i], R[i]);
        float inPk = 0; for (int i = 0; i < n; ++i) inPk = std::max (inPk, std::abs (L[i]));

        float* chans[2] { L, R };
        juce::dsp::AudioBlock<float> blk (chans, 2, (size_t) n);
        auto up = os.processSamplesUp (blk);
        float* ul = up.getChannelPointer (0); float* ur = up.getChannelPointer (1);
        const int un = (int) up.getNumSamples();
        const int decim = 1 + (int) (amt * amt * 40.0f) * 4;
        for (int i = 0; i < un; ++i)
        {
            const float g = driveSm.next(); comp = compSm.next();
            float a = shapeStyle (curStyle, ul[i], ur[i], g, amt, decim, 0);
            float b = shapeStyle (curStyle, ul[i], ur[i], g, amt, decim, 1);
            if (fade > 0)
            {
                const float w = (float) fade / (float) fadeLen; --fade;
                a = a * (1.0f - w) + w * shapeStyle (prevStyle, ul[i], ur[i], g, amt, decim, 0);
                b = b * (1.0f - w) + w * shapeStyle (prevStyle, ul[i], ur[i], g, amt, decim, 1);
            }
            ul[i] = dc[0].process (a); ur[i] = dc[1].process (b);
        }
        os.processSamplesDown (blk);

        toneSm.target = tone(); outSm.target = dbToGain (out());
        const bool useAuto = autoG.b() && curStyle <= wsFold;
        float outPk = 0;
        for (int i = 0; i < n; ++i)
        {
            const float t = toneSm.next();
            if (changed (t, lastTone)) { lastTone = t; tgl = dbToGain (-6.0f * t); tgh = dbToGain (6.0f * t); }
            const float gl = tgl, gh = tgh;
            float x[2] { L[i], R[i] };
            for (int c = 0; c < 2; ++c)
            {
                const float lo = tilt[c].lp (x[c]); x[c] = lo * gl + (x[c] - lo) * gh;
                if (lcOn) { if (steepNow) { x[c] = cut[c][0].process (x[c], lcC[0]); x[c] = cut[c][1].process (x[c], lcC[1]); } else x[c] = opLc[c].hp (x[c]); }
                if (thumpNow) x[c] = thumpBq[c].process (x[c], thumpC);
                if (hcOn) { if (steepNow) { x[c] = cut[c][2].process (x[c], hcC[0]); x[c] = cut[c][3].process (x[c], hcC[1]); } else x[c] = opHc[c].lp (x[c]); }
            }
            const float a = useAuto ? ag.next (x[0], x[1]) : 1.0f;
            const float o = outSm.next() * a;
            L[i] = x[0] * o; R[i] = x[1] * o;
            outPk = std::max (outPk, std::abs (L[i]));
        }
        if (tm) { tm->wreckIn.store (inPk); tm->wreckOut.store (outPk); }
    }
private:
    float targetDrive() const { return dbToGain (drive() * 36.0f + (punish.b() ? 20.0f : 0.0f)); }
    float shapeStyle (int s, float l, float r, float g, float amt, int decim, int ch) noexcept
    {
        const float x = ch == 0 ? l : r;
        switch (s)
        {
            case wsConsole:
            {
                const float lo = consoleLp[ch].lp (x);
                const float y = x + 0.8f * (wreckShape (wsConsole, lo * 1.6f, g, amt) / (1.6f * g) - lo);
                return wreckShape (wsConsole, y, g, amt);
            }
            case wsCrush: return wreckShape (wsCrush, x, 1.0f, amt);
            case wsDecimate:
            {
                if (ch == 0 && ++holdC >= decim) { holdC = 0; hl = l; hr = r; }
                return ch == 0 ? hl : hr;
            }
            default: return wreckShape (s, x, g, amt) * comp;
        }
    }
    void updateFilters()
    {
        const float lc = lowcut(), hc = highcut(); const int stp = steep.b() ? 1 : 0, th = thump.b() ? 1 : 0;
        if (! changed (lc, lastLc) && ! changed (hc, lastHc) && stp == lastSteep && th == lastThump) return;
        lastLc = lc; lastHc = hc; lastSteep = stp; lastThump = th;
        lcOn = lc > 11.0f; hcOn = hc < 21000.0f; steepNow = stp == 1; thumpNow = th == 1;
        lcC[0] = rbj (rbjHP, sr, lc, 0.541); lcC[1] = rbj (rbjHP, sr, lc, 1.307);
        hcC[0] = rbj (rbjLP, sr, hc, 0.541); hcC[1] = rbj (rbjLP, sr, hc, 1.307);
        for (auto& o : opLc) o.setLP (sr, lc);
        for (auto& o : opHc) o.setLP (sr, hc);
        thumpC = rbj (rbjBell, sr, lcOn ? lc * 1.4 : 70.0, 1.1, 5.0);
    }
    P style, drive, tone, lowcut, highcut, thump, steep, punish, autoG, out;
    double sr = 48000; int lat = 0;
    juce::dsp::Oversampling<float> os { 2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true };
    FixedDelay dry; ExpSmooth driveSm, compSm, outSm, toneSm; AutoGain ag; float comp = 1, lastTone = -99, tgl = 1, tgh = 1;
    OnePole tilt[2], consoleLp[2], opLc[2], opHc[2]; DcBlock dc[2];
    Biquad cut[2][4], thumpBq[2]; BiquadCoefs lcC[2], hcC[2], thumpC;
    bool lcOn = false, hcOn = false, steepNow = false, thumpNow = false;
    float lastLc = -1, lastHc = -1; int lastSteep = -1, lastThump = -1;
    int curStyle = 0, prevStyle = 0, fade = 0, fadeLen = 1, holdC = 0; float hl = 0, hr = 0;
};

// ================================================================ TUBE GLOW
class TubeUnit : public UnitDsp
{
public:
    Telemetry* tm = nullptr;
    void attach (juce::AudioProcessorValueTreeState& s) override
    {
        drive.bind (s, pid (uTube, "drive"), 0.35f); mic.bind (s, pid (uTube, "char")); bass.bind (s, pid (uTube, "bass"));
        treble.bind (s, pid (uTube, "treble")); out.bind (s, pid (uTube, "out"));
    }
    void prepare (double sr_, int) override
    {
        sr = sr_;
        os.initProcessing ((size_t) kSub); os.reset();
        lat = (int) std::lround (os.getLatencyInSamples()); dry.prepare (lat);
        driveSm.setTime (sr * 2.0, 30.0f); compSm.setTime (sr * 2.0, 30.0f); outSm.setTime (sr, 20.0f); ag.prepare (sr, 500.0f);
        for (auto& e : sag) e.set (sr * 2.0, 8.0f, 120.0f);
        for (auto& d : dc) d.prepare (sr * 2.0);
        for (auto& p : post) p.setLP (sr, 9000);
        reset();
    }
    void reset() override
    {
        os.reset(); ag.reset();
        for (auto& e : sag) e.reset();
        for (auto& d : dc) d.reset();
        for (auto& p : post) p.reset();
        for (auto& b : eqs) for (auto& q : b) q.reset();
        driveSm.snap (gainNow()); compSm.snap (std::pow (gainNow(), -0.7f)); outSm.snap (1.0f); lastB = lastT = -99; lastMic = -1;
    }
    int latency() const override { return lat; }
    void delayDry (float* L, float* R, int n) override { dry.process (L, R, n); }
    void clearLines() override { dry.clear(); }
    void process (float* L, float* R, int n, const BlockCtx&) override
    {
        const bool isMic = mic.b();
        if (changed (bass(), lastB) || changed (treble(), lastT) || (int) isMic != lastMic)
        {
            lastB = bass(); lastT = treble(); lastMic = (int) isMic;
            bc = rbj (rbjLowShelf, sr, 110.0, 0.6, lastB); tc = rbj (rbjHighShelf, sr, isMic ? 5000.0 : 7000.0, 0.6, lastT);
            for (auto& p : post) p.setLP (sr, isMic ? 7500.0 : 16000.0);
        }
        for (int i = 0; i < n; ++i)
        {
            ag.feedIn (L[i], R[i]);
            L[i] = eqs[0][1].process (eqs[0][0].process (L[i], bc), tc);
            R[i] = eqs[1][1].process (eqs[1][0].process (R[i], bc), tc);
        }
        driveSm.target = gainNow(); compSm.target = std::pow (driveSm.target, -0.7f);
        const float bias0 = isMic ? 0.24f : 0.12f;
        float* chans[2] { L, R };
        juce::dsp::AudioBlock<float> blk (chans, 2, (size_t) n);
        auto up = os.processSamplesUp (blk);
        for (int ch = 0; ch < 2; ++ch)
        {
            float* d = up.getChannelPointer ((size_t) ch);
            ExpSmooth ds = driveSm, cs = compSm;
            for (size_t i = 0; i < up.getNumSamples(); ++i)
            {
                const float g = ds.next(), cmp = cs.next();
                const float x = d[i] * g;
                const float e = sag[ch].process (x);
                const float b = bias0 + 0.2f * clampf (e, 0.0f, 1.5f);     // dynamic bias = bloom on loud notes
                const float y = (fastTanh (x + b) - fastTanh (b)) / (1.0f - fastTanh (b) * fastTanh (b));
                d[i] = dc[ch].process (y) * cmp;
            }
            if (ch == 1) { driveSm = ds; compSm = cs; }
        }
        os.processSamplesDown (blk);
        outSm.target = dbToGain (out());
        float pk = 0;
        for (int i = 0; i < n; ++i)
        {
            const float l = post[0].lp (L[i]), r = post[1].lp (R[i]);
            const float a = ag.next (l, r), o = outSm.next() * (0.35f + 0.65f * a);
            L[i] = l * o; R[i] = r * o; pk = std::max (pk, std::abs (L[i]));
        }
        if (tm) tm->tubeLevel.store (pk);
    }
private:
    float gainNow() const { return dbToGain (drive() * 26.0f + (mic.b() ? 6.0f : 0.0f)); }
    P drive, mic, bass, treble, out;
    double sr = 48000; int lat = 0;
    juce::dsp::Oversampling<float> os { 2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true };
    FixedDelay dry; ExpSmooth driveSm, compSm, outSm; AutoGain ag; EnvFollow sag[2]; DcBlock dc[2]; OnePole post[2];
    Biquad eqs[2][2]; BiquadCoefs bc, tc; float lastB = -99, lastT = -99; int lastMic = -1;
};

// ================================================================ SOUL CRUSH
class CrushUnit : public UnitDsp
{
public:
    Telemetry* tm = nullptr;
    void attach (juce::AudioProcessorValueTreeState& s) override
    {
        crush.bind (s, pid (uCrush, "crush"), 0.5f); dark.bind (s, pid (uCrush, "dark"), 0.3f);
        release.bind (s, pid (uCrush, "release"), 120); out.bind (s, pid (uCrush, "out"));
    }
    void prepare (double sr_, int) override
    {
        sr = sr_; gainSm.setTime (sr, 20.0f); outSm.setTime (sr, 20.0f); darkSm.setTime (sr, 30.0f); reset();
    }
    void reset() override
    {
        env = 0; gr = 1; svf[0].reset(); svf[1].reset(); gainSm.snap (dbToGain (crush() * 36.0f)); outSm.snap (1); darkSm.snap (dark());
    }
    void process (float* L, float* R, int n, const BlockCtx&) override
    {
        gainSm.target = dbToGain (crush() * 36.0f);
        outSm.target = dbToGain (out() - 3.0f);
        darkSm.target = dark();
        const float rel = 1.0f - (float) std::exp (-1.0 / (std::max (5.0f, release()) * 0.001 * sr));
        const float thr = 0.25f;
        float minG = 1;
        for (int i = 0; i < n; ++i)
        {
            if ((i & 15) == 0)
            {
                const float dk = darkSm.next();
                const double f = 18000.0 * std::pow (750.0 / 18000.0, (double) dk);
                svf[0].set (sr, f, 0.6 + dk * 0.5); svf[1].set (sr, f, 0.6 + dk * 0.5);
            }
            const float g = gainSm.next();
            float l = L[i] * g, r = R[i] * g;
            const float pk = std::max (std::abs (l), std::abs (r));
            env = pk > env ? pk : env + rel * (pk - env);           // instant attack, smashing release
            const float target = env > thr ? thr / env : 1.0f;
            gr += (target < gr ? 0.6f : rel) * (target - gr);
            minG = std::min (minG, gr);
            l *= gr; r *= gr;
            l = fastTanh (l * 2.2f) * 0.6f + 0.08f * fastTanh (l * 9.0f);   // grit
            r = fastTanh (r * 2.2f) * 0.6f + 0.08f * fastTanh (r * 9.0f);
            float lp, bp, hp;
            svf[0].tick (l, lp, bp, hp); l = lp;
            svf[1].tick (r, lp, bp, hp); r = lp;
            const float o = outSm.next();
            L[i] = l * o; R[i] = r * o;
        }
        if (tm) tm->crushGr.store (gainToDb (minG));
    }
private:
    P crush, dark, release, out;
    double sr = 48000; float env = 0, gr = 1; Svf svf[2]; ExpSmooth gainSm, outSm, darkSm;
};

// ================================================================ PHANTOM FILTER
enum FilterType { ftLP, ftBP, ftHP, ftNotch };
enum LfoShape { lsSine, lsTri, lsSawUp, lsSawDown, lsSquare, lsStep, lsSmooth, kNumLfoShapes };
inline float lfoValue (int shape, double phase, uint32_t cycleSeed) noexcept   // phase in cycles; -1..1
{
    const double ph = phase - std::floor (phase);
    const auto cyc = (uint32_t) (int64_t) std::floor (phase);
    switch (shape)
    {
        case lsSine:    return (float) std::sin (ph * juce::MathConstants<double>::twoPi);
        case lsTri:     return (float) (ph < 0.5 ? ph * 4.0 - 1.0 : 3.0 - ph * 4.0);
        case lsSawUp:   return (float) (ph * 2.0 - 1.0);
        case lsSawDown: return (float) (1.0 - ph * 2.0);
        case lsSquare:  return ph < 0.5 ? 1.0f : -1.0f;
        case lsStep:    return hashFloat (cycleSeed + cyc * 9973u) * 2.0f - 1.0f;
        default:
        {
            const float a = hashFloat (cycleSeed + cyc * 9973u) * 2.0f - 1.0f, b = hashFloat (cycleSeed + (cyc + 1) * 9973u) * 2.0f - 1.0f;
            const float u = 0.5f - 0.5f * std::cos (kPi * (float) ph);
            return a + (b - a) * u;
        }
    }
}
// Display helper: magnitude of one SVF stage of a type at frequency f.
inline float svfMag (int type, double f, double fc, double q)
{
    const double w = f / fc, k = 1.0 / q;
    const std::complex<double> s (0.0, w);
    const auto den = s * s + k * s + 1.0;
    std::complex<double> num;
    switch (type) { case ftLP: num = 1.0; break; case ftBP: num = k * s; break; case ftHP: num = s * s; break; default: num = s * s + 1.0; break; }
    return (float) std::abs (num / den);
}

class FilterUnit : public UnitDsp
{
public:
    Telemetry* tm = nullptr;
    void attach (juce::AudioProcessorValueTreeState& s) override
    {
        type.bind (s, pid (uFilter, "type")); poles.bind (s, pid (uFilter, "poles")); routing.bind (s, pid (uFilter, "routing"));
        cutoff.bind (s, pid (uFilter, "cutoff"), 1200); reso.bind (s, pid (uFilter, "reso"), 0.4f); spread.bind (s, pid (uFilter, "spread"));
        drive.bind (s, pid (uFilter, "drive"), 0.2f); lfoAmt.bind (s, pid (uFilter, "lfoamt")); lfoShape.bind (s, pid (uFilter, "lfoshape"));
        lfoSync.bind (s, pid (uFilter, "lfosync"), 1); lfoDiv.bind (s, pid (uFilter, "lfodiv"), 9); lfoRate.bind (s, pid (uFilter, "lforate"), 1);
        envAmt.bind (s, pid (uFilter, "envamt")); envRel.bind (s, pid (uFilter, "envrel"), 200);
    }
    void prepare (double sr_, int) override
    {
        sr = sr_; cutSm.setTime (sr, 15.0f); resoSm.setTime (sr, 20.0f); driveSm.setTime (sr, 20.0f); reset();
    }
    void reset() override
    {
        for (auto& f : st) for (auto& s : f) for (auto& v : s) v.reset();
        cutSm.snap (std::log2 (cutoff())); resoSm.snap (reso()); driveSm.snap (drive()); freePhase = 0; env.reset(); lfoSmooth = 0;
    }
    void process (float* L, float* R, int n, const BlockCtx& c) override
    {
        const int ft = type.i(), nStages = poles.i() == 1 ? 2 : 1, route = routing.i();
        cutSm.target = std::log2 (juce::jlimit (20.0f, 20000.0f, cutoff()));
        resoSm.target = reso(); driveSm.target = drive();
        env.set (sr, 4.0f, std::max (10.0f, envRel()));
        const bool sync = lfoSync.b();
        const double divBeats = echoDivs()[(size_t) juce::jlimit (0, (int) echoDivs().size() - 1, lfoDiv.i())].beats;
        const double hz = lfoRate();
        const float la = lfoAmt(), ea = envAmt(), sp = spread();
        const int shape = lfoShape.i();
        float lfo = 0, e01 = 0; double fcNow = 1000;
        for (int i = 0; i < n; ++i)
        {
            const double phase = sync ? c.ppqAt (i) / divBeats : freePhase;
            if (! sync) freePhase += hz / sr;
            lfo = lfoValue (shape, phase, 0x51f3u);
            if (shape == lsStep || shape == lsSquare) { lfoSmooth += 0.02f * (lfo - lfoSmooth); lfo = lfoSmooth; } else lfoSmooth = lfo;
            const float e = env.process (0.5f * (std::abs (L[i]) + std::abs (R[i])));
            e01 = clampf ((gainToDb (e) + 48.0f) / 48.0f, 0.0f, 1.0f);
            const float lc = cutSm.next(), rs = resoSm.next(), dv = driveSm.next();
            if ((i & 7) == 0)
            {
                preG = dbToGain (dv * 24.0f); postG = 1.0f / std::pow (preG, 0.8f);
                const double oct = lc + la * lfo * 4.0f + ea * e01 * 6.0f;
                fcNow = juce::jlimit (20.0, 20000.0, std::pow (2.0, oct));
                const double q = 0.5 + rs * rs * 24.0;
                for (int f = 0; f < 2; ++f)
                {
                    const double ff = f == 0 ? fcNow : juce::jlimit (20.0, 20000.0, fcNow * std::pow (2.0, (double) sp));
                    for (int s = 0; s < nStages; ++s)
                    {
                        const double qq = nStages == 2 ? (s == 0 ? 0.54 + 0.3 * rs : q) : q;
                        for (int ch = 0; ch < 2; ++ch) st[f][s][ch].set (sr, ff, qq);
                    }
                }
            }
            const float pre = preG, post = postG;
            float x[2] { L[i] * pre, R[i] * pre };
            for (int ch = 0; ch < 2; ++ch)
            {
                const float in = fastTanh (x[ch]);
                float y1 = runFilter (0, ch, ft, nStages, in);
                float y = y1;
                if (route == 1) y = runFilter (1, ch, ft, nStages, fastTanh (y1));
                else if (route == 2) y = 0.5f * (y1 + runFilter (1, ch, ft, nStages, in));
                x[ch] = y * post;
            }
            L[i] = x[0]; R[i] = x[1];
        }
        if (tm) { tm->filterCut.store ((float) fcNow); tm->filterLfo.store (lfo); tm->filterEnv.store (e01); }
    }
private:
    float runFilter (int f, int ch, int ft, int nStages, float x) noexcept
    {
        float y = x;
        for (int s = 0; s < nStages; ++s)
        {
            float lp, bp, hp; st[f][s][ch].tick (y, lp, bp, hp);
            switch (ft) { case ftLP: y = lp; break; case ftBP: y = bp * st[f][s][ch].k; break; case ftHP: y = hp; break; default: y = lp + hp; break; }
            y = clampf (y, -8.0f, 8.0f);
        }
        return y;
    }
    P type, poles, routing, cutoff, reso, spread, drive, lfoAmt, lfoShape, lfoSync, lfoDiv, lfoRate, envAmt, envRel;
    double sr = 48000, freePhase = 0; float lfoSmooth = 0, preG = 1, postG = 1;
    Svf st[2][2][2]; ExpSmooth cutSm, resoSm, driveSm; EnvFollow env;
};

} // namespace gpfx
