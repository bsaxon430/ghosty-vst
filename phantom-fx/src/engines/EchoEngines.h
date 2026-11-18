// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX: LYNX SHARE's echo suite (Ka1-S/LYNX-SHARE @ d141370, src/core/EchoSuite.{h,cpp}, AGPL-3.0, vendored unchanged) as extra engines
// (DECISIONS.md 86): ECTO ECHO gains ECHOFORM (its twelve echo modes: tape, lo-fi, hi-fi, bucket brigade, analog, digital, clean, phaser,
// shifter, pitch, reverse, detune) and SPIRAL (its frequency-shifting echo); CHORUS gains ORBIT (its eleven modulation modes: flangers,
// through-zero flangers, choruses, doubler, ensemble). Each reads the effect's own controls through LYNX's own laws; the delay time is
// ours (SYNC + DIVISION at the host tempo, or TIME), handed to the engine in milliseconds, so it follows the host as the PHANTOM engine does.
#pragma once
#include "AltEngine.h"
#include "../dsp/Modulators.h"
#include "core/EchoSuite.h"
#include "core/Effects.h"

namespace phantom::engines
{
class LynxEchoEngine final : public AltEngine
{
public:
    enum Kind { Echoform, Spiral, Orbit };
    static int voiceCount (Kind k) noexcept { return k == Echoform ? lynx::echosuite::echoform::modeCount : k == Orbit ? lynx::echosuite::orbit::modeCount : 1; }
    static std::string voiceName (Kind k, int i)
    {
        static const char* modes[12] { "Tape", "Lo-Fi", "Hi-Fi", "Bucket Brigade", "Analog", "Digital", "Clean", "Phaser", "Shifter", "Pitch", "Reverse", "Detune" };
        static const char* orbits[11] { "Rise", "Rise / Fall", "Through Zero +", "Through Zero -", "Tri", "Octo", "Doubler", "Drift Rise", "Drift Rise / Fall", "Ensemble", "Symphonic" };
        if (k == Echoform) return modes[juce::jlimit (0, 11, i)]; if (k == Orbit) return orbits[juce::jlimit (0, 10, i)]; return {};
    }
    LynxEchoEngine (Kind k, const std::vector<ParamSpec>& specs) : kind (k)
    {
        for (auto& s : specs) defs[s.id] = s.def;
        const auto& inf = k == Echoform ? lynx::echosuite::echoformInfo() : k == Spiral ? lynx::echosuite::spiralInfo() : lynx::echosuite::orbitInfo();
        base.fill (0.0f); for (size_t i = 0; i < inf.params.size(); ++i) base[i] = inf.params[i].defaultValue;
        base[0] = 1.0f;   // MIX 100 %: the slot's MIX blends the dry (the effects are wet-only)
        for (auto& f : fx) f = lynx::echosuite::make (k == Echoform ? lynx::EffectKind::echoform : k == Spiral ? lynx::EffectKind::spiralEcho : lynx::EffectKind::orbit);
    }
    void prepare (double sampleRate, int maxBlock) override
    {
        sr = sampleRate; for (auto& f : fx) { f->prepare (sampleRate); f->reset(); }
        const int mb = juce::jmax (maxBlock, 64); for (auto& b : tmp) b.assign ((size_t) mb, 0.0f);
        measureOffsets(); reset();
    }
    void reset() noexcept override { for (auto& f : fx) f->reset(); primed = false; cur = 0; next = -1; xf = 1.0f; settle = 0; built = { -1.0f, -1.0f }; pending = -1.0f; }
    void process (float* L, float* R, int n, const Controls& c, int voice, const TempoInfo& t) noexcept override
    {
        using namespace lynx::echosuite;
        const double bpm = t.bpm > 0.0 ? t.bpm : 120.0; auto p = base;
        // our controls glide (40 ms; RATE / DEPTH 300 ms, ORBIT's 1 s and its DELAY 1 s: they move its taps), handed every 32 samples
        float tg[kCtl]; read (c, bpm, tg); if (! primed) { std::copy (tg, tg + kCtl, sm); primed = true; }
        const float a = 1.0f - std::exp (-32.0f / (0.04f * (float) sr)), aSlow = 1.0f - std::exp (-32.0f / ((kind == Orbit ? 1.0f : 0.3f) * (float) sr));
        // ECHO's delay time (TIME, or SYNC + DIVISION at the host tempo): a new time is taken by a second engine once it has settled
        // (100 ms), the two crossfading over 150 ms, so turning TIME or a tempo change never smears the repeats (ORBIT's DELAY glides)
        const bool echo = kind != Orbit;
        if (echo)
        {
            const float want = (float) (std::round ((double) tg[cMs] * 1000.0) / 1000.0);   // (to 1 us: a synced time is exact)
            if (want != pending) { pending = want; settle = 0; } else settle += n;
            if (built[(size_t) cur] < 0.0f) built[(size_t) cur] = want;
            if (next < 0 && want != built[(size_t) cur] && settle >= (int) (0.1 * sr)) { next = 1 - cur; built[(size_t) next] = want; fx[(size_t) next]->reset(); xf = 0.0f; }
        }
        const float aMs = 1.0f - std::exp (-32.0f / (1.0f * (float) sr));
        // ORBIT reads DEPTH and DELAY once per call to place its taps: while either is still moving it is handed them every sample
        // (a 32-sample step in a tap's position is a click); the cuts glide at 300 ms (a filter step is audible above 6 kHz)
        const float a1 = 1.0f - std::exp (-1.0f / (0.04f * (float) sr)), aSlow1 = 1.0f - std::exp (-1.0f / ((kind == Orbit ? 1.0f : 0.3f) * (float) sr)),
                    aMs1 = 1.0f - std::exp (-1.0f / (1.0f * (float) sr)), aCut = 1.0f - std::exp (-32.0f / (0.3f * (float) sr)), aCut1 = 1.0f - std::exp (-1.0f / (0.3f * (float) sr));
        for (int i0 = 0; i0 < n;)
        {
            const bool fine = kind == Orbit && (std::abs (tg[cDepth] - sm[cDepth]) > 1.0e-4f || std::abs (tg[cMs] - sm[cMs]) > 1.0e-3f);
            const int m = juce::jmin (fine ? 1 : 32, n - i0);
            for (int k = 0; k < kCtl; ++k) sm[k] += (k == cDepth || k == cRate ? (fine ? aSlow1 : aSlow) : k == cMs ? (fine ? aMs1 : aMs) : k == cLow || k == cHigh ? (fine ? aCut1 : aCut) : (fine ? a1 : a)) * (tg[k] - sm[k]);
            if (next >= 0)
            {
                std::copy (L + i0, L + i0 + m, tmp[0].begin()); std::copy (R + i0, R + i0 + m, tmp[1].begin());
                auto pn = p; configure (pn, voice, built[(size_t) next]); fx[(size_t) next]->process (tmp[0].data(), tmp[1].data(), m, pn, bpm);
            }
            configure (p, voice, echo ? built[(size_t) cur] : sm[cMs]); fx[(size_t) cur]->process (L + i0, R + i0, m, p, bpm);
            if (next >= 0)
            {
                const float step = 1.0f / (0.15f * (float) sr);
                for (int i = 0; i < m; ++i) { xf = juce::jmin (1.0f, xf + step); const float x = std::cos (xf * juce::MathConstants<float>::halfPi), y = std::sin (xf * juce::MathConstants<float>::halfPi); L[i0 + i] = L[i0 + i] * x + tmp[0][(size_t) i] * y; R[i0 + i] = R[i0 + i] * x + tmp[1][(size_t) i] * y; }
                if (xf >= 1.0f) { cur = next; next = -1; }
            }
            i0 += m;
        }
        const float wt = clamp01 (c.get ("width", 1.0f)); if (! wPrimed) { w = wt; wPrimed = true; }   // WIDTH: ours, mid / side
        const float ws = (wt - w) / (float) juce::jmax (1, n);
        for (int i = 0; i < n; ++i) { w += ws; const float mm = 0.5f * (L[i] + R[i]), sd = 0.5f * (L[i] - R[i]) * w; L[i] = mm + sd; R[i] = mm - sd; }
    }
private:
    enum Ctl { cMs, cFb, cLow, cHigh, cChar, cDuck, cSat, cPing, cRate, cDepth, kCtl };
    // each echo voice's own arrival offset (ms) against its nominal time: the modes carry the measured timing of the echoes they model
    // (Clean 2.5 ms late, Tape 0.5 ms early ...). Measured once here (an impulse, the first echo's peak at a 300 ms setting), and taken
    // off the time we ask for, so a synced echo lands on the host's grid as the PHANTOM engine's does
    void measureOffsets()
    {
        offsetMs.fill (0.0f); if (kind == Orbit) return;
        const int nv = voiceCount (kind), N = (int) (sr * 0.45); std::vector<float> l ((size_t) N), r ((size_t) N);
        for (int v = 0; v < nv; ++v)
        {
            auto p = base; std::fill (sm, sm + kCtl, 0.0f); sm[cFb] = 0.0f; sm[cLow] = 20.0f; sm[cHigh] = 20000.0f; sm[cChar] = 0.0f; sm[cPing] = 0.0f; sm[cMs] = 300.0f;
            if (kind == Echoform) sm[cChar] = 0.0f;
            configure (p, v, 300.0f); if (kind == Echoform) p[lynx::echosuite::echoform::feedback] = 0.0f; else p[lynx::echosuite::spiral::feedback] = 0.0f;
            fx[0]->reset(); std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f); l[0] = r[0] = 1.0f;
            for (int o = 0; o < N; o += 64) fx[0]->process (l.data() + o, r.data() + o, juce::jmin (64, N - o), p, 120.0);
            int best = 0; float bv = 0.0f; for (int i = 1; i < N; ++i) if (std::abs (l[(size_t) i]) > bv) { bv = std::abs (l[(size_t) i]); best = i; }
            double frac = 0.0;   // the peak between samples (parabola through the three around it)
            if (best > 0 && best + 1 < N) { const double a0 = std::abs (l[(size_t) best - 1]), c0 = std::abs (l[(size_t) best]), d0 = std::abs (l[(size_t) best + 1]); if (a0 - 2 * c0 + d0 != 0.0) frac = 0.5 * (a0 - d0) / (a0 - 2 * c0 + d0); }
            if (bv > 1.0e-4f) offsetMs[(size_t) v] = juce::jlimit (-20.0f, 20.0f, (float) ((best + frac - 0.3 * sr) * 1000.0 / sr));
        }
        fx[0]->reset(); std::fill (sm, sm + kCtl, 0.0f);
    }
    std::array<float, 16> offsetMs {};
    float def (const char* id, float fb) const { auto it = defs.find (id); return it == defs.end() ? fb : it->second; }
    void read (const Controls& c, double bpm, float* v) const noexcept
    {
        if (kind == Orbit)
        {   // CHORUS: RATE (Hz), DEPTH (0..1), DELAY (ms), FEEDBACK
            v[cMs] = c.get ("delay", 12.0f); v[cFb] = c.get ("fb", 0.0f); v[cRate] = c.get ("rate", 0.4f); v[cDepth] = c.get ("depth", 0.5f);
            v[cLow] = v[cHigh] = v[cChar] = v[cDuck] = v[cSat] = v[cPing] = 0.0f; return;
        }
        // ECTO ECHO: the delay is SYNC + DIVISION at the host tempo, or TIME
        double ms = c.get ("time", 375.0f);
        if (c.get ("sync", 1.0f) >= 0.5f) { const auto& d = divisions(); const int di = juce::jlimit (0, (int) d.size() - 1, (int) std::lround (c.get ("div", 7.0f))); ms = d[(size_t) di].beats * 60000.0 / bpm; }
        v[cMs] = (float) ms; v[cFb] = c.get ("fb", 0.45f); v[cLow] = c.get ("lowcut", 120.0f); v[cHigh] = c.get ("highcut", 6500.0f);
        v[cChar] = c.get ("char", 0.5f); v[cDuck] = c.get ("duck", 0.0f); v[cSat] = c.get ("sat", 0.0f); v[cPing] = c.get ("ping", 1.0f); v[cRate] = v[cDepth] = 0.0f;
    }
    void configure (lynx::EffectParams& p, int voice, float ms) const noexcept
    {
        using namespace lynx::echosuite;
        if (kind == Echoform)
        {
            using namespace echoform;
            p[style] = sm[cPing] >= 0.5f ? 4.5f / 5.0f : 0.5f / 5.0f;   // ping-pong / single
            p[syncL] = p[syncR] = 0.5f / 4.0f;                           // milliseconds (the time is ours)
            p[timeL] = p[timeR] = timeValue (juce::jlimit (1.0, 4000.0, (double) ms - offsetMs[(size_t) juce::jlimit (0, 15, voice)]));
            // FEEDBACK: our default lands on its own default; at full it stays just under unity (no runaway)
            p[feedback] = around (sm[cFb], 0.0f, def ("fb", 0.45f), 1.0f, 0.0f, base[feedback], 0.49f);
            p[lowCut] = inverse01 ([] (float x) { return lowCutHz (x); }, sm[cLow]); p[highCut] = inverse01 ([] (float x) { return highCutHz (x); }, sm[cHigh]);
            p[age] = clamp01 (sm[cChar]); p[ducking] = clamp01 (sm[cDuck]); p[drive] = clamp01 (sm[cSat]);
            p[modDepth] = 0.0f;   // its LFO off (ECTO ECHO has no wobble control; it would move the repeats off the grid). TAPE keeps its wow / flutter
            p[mode] = ((float) juce::jlimit (0, modeCount - 1, voice) + 0.5f) / (float) modeCount;
        }
        else if (kind == Spiral)
        {   // SPIRAL: TIME / SYNC are ours; CHARACTER is its frequency shift (0.5 = none, either side up to +-1 kHz)
            using namespace spiral;
            p[spiral::delay] = delayValue (juce::jlimit (0.01, 1000.0, (double) ms - offsetMs[0])); p[spiral::sync] = 0.0f;
            p[feedback] = around (sm[cFb], 0.0f, def ("fb", 0.45f), 1.0f, 0.0f, base[feedback], 0.62f);
            p[shift] = clamp01 (0.5f + (sm[cChar] - 0.5f) * 0.7f);
            p[lowCut] = inverse01 ([] (float x) { return lowCutHz (x); }, juce::jlimit (20.0, 3000.0, (double) sm[cLow]));
            p[highCut] = inverse01 ([] (float x) { return highCutHz (x); }, juce::jlimit (50.0, 15000.0, (double) sm[cHigh]));
        }
        else
        {   // ORBIT (CHORUS): RATE, DEPTH (our default = its own), DELAY = its manual offset, FEEDBACK
            using namespace orbit;
            p[rate] = inverse01 ([] (float x) { return rateHz (x); }, (double) sm[cRate]);
            p[depth] = around (sm[cDepth], 0.0f, def ("depth", 0.5f), 1.0f, 0.0f, base[depth], 1.0f);
            p[manual] = inverse01 ([] (float x) { return manualMs (x); }, juce::jlimit (0.0, 50.0, (double) ms - def ("delay", 12.0f)));
            p[feedback] = inverse01 ([] (float x) { return feedbackGain (x); }, 0.6 * (double) sm[cFb]);
            p[orbit::mode] = modeValue ((Mode) juce::jlimit (0, modeCount - 1, voice));
        }
    }
    Kind kind; std::array<std::unique_ptr<lynx::Effect>, 2> fx; std::array<std::vector<float>, 2> tmp; int cur = 0, next = -1, settle = 0; float xf = 1.0f, pending = -1.0f; std::array<float, 2> built { -1.0f, -1.0f }; lynx::EffectParams base {}; std::map<std::string, float> defs;
    double sr = 48000.0; float sm[kCtl] {}, w = 1.0f; bool primed = false, wPrimed = false;
};
} // namespace phantom::engines
