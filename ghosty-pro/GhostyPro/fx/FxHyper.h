#pragma once
// HYPER / DIMENSION (Ghosty Pro, round 2 item 6, 2026-10-24). Original DSP (no third-party code).
//   HYPER      1-7 micro-delay chorus voices: each voice is a short delay (4 .. 11 ms) whose time wobbles with its own slow sine, voices
//              alternating sharp / flat, panned across WIDTH, power-normalised (adding voices never gets louder): the width and
//              thickness of a big unison without the oscillator CPU. RETRIG restarts the wobble on every note so every hit starts alike.
//   DIMENSION  4 delay lines (5.5 .. 15.7 ms, SIZE stretches them and adds a second longer layer) moved by one slow LFO in quadrature,
//              each line slowly amplitude-modulated out of phase with the others, left = lines 1 - 3, right = lines 2 - 4.
// The unit is 100 % wet (the rack mixes). Audio-thread rule: prepare() allocates, process() never does.
#include "FxCore.h"

#if ! GPFX_BOX
namespace gpfx
{
inline const juce::StringArray& hyperModeNames() { static juce::StringArray s { "HYPER", "DIMENSION" }; return s; }

class HyperUnit : public UnitDsp
{
public:
    Telemetry* tm = nullptr;
    void attach (juce::AudioProcessorValueTreeState& s) override
    {
        mode.bind (s, pid (uHyper, "mode"), 0); voices.bind (s, pid (uHyper, "voices"), 4); detune.bind (s, pid (uHyper, "detune"), 0.35f);
        rate.bind (s, pid (uHyper, "rate"), 0.6f); width.bind (s, pid (uHyper, "width"), 1.0f); retrig.bind (s, pid (uHyper, "retrig"), 1);
        size.bind (s, pid (uHyper, "size"), 0.4f);
    }
    void prepare (double sr_, int) override
    {
        sr = sr_;
        for (auto& r : ring) r.prepare ((int) (0.09 * sr) + 64);        // up to 90 ms (DIMENSION's second layer at full SIZE)
        gA = 1.0f - (float) std::exp (-1.0 / (0.030 * sr));              // 30 ms glides on the per-voice gains and the depths
        reset();
    }
    void reset() override
    {
        for (auto& r : ring) r.clear();
        for (int k = 0; k < kMax; ++k) { ph[k] = (double) k / kMax; g[k] = 0; }
        dPh = 0; amPh = 0; depthSm = 0; sizeSm = size(); widthSm = width();
    }
    void process (float* L, float* R, int n, const BlockCtx& ctx) override
    {
        const bool dim = mode.i() == 1;
        const int nv = juce::jlimit (1, kMax, voices.i());
        const float rt = juce::jlimit (0.05f, 8.0f, rate());
        // a pitch wobble of +-cents at the LFO rate needs a delay swing of cents / (1731 * 2 pi * rate) seconds (d phi / dt = ratio - 1)
        const float cents = 2.0f + 38.0f * juce::jlimit (0.0f, 1.0f, detune());
        const float depthT = juce::jmin (0.008f, cents / (1731.0f * kTwoPi * rt)) * (float) sr;
        const float sizeT = juce::jlimit (0.0f, 1.0f, size()), widthT = juce::jlimit (0.0f, 1.0f, width());
        const int retrigAt = (retrig.b() && ctx.noteOns > 0) ? juce::jlimit (0, n - 1, ctx.noteOnAt) : -1;   // sample-exact: every note starts the wobble alike
        // per-voice targets: gains 1 / sqrt (N) over the active voices (power-normalised), pans across WIDTH, sharp / flat alternating
        float gT[kMax], pl[kMax], pr[kMax]; double inc[kMax]; float base[kMax];
        const float norm = 1.0f / std::sqrt ((float) nv);
        for (int k = 0; k < kMax; ++k)
        {
            gT[k] = k < nv ? norm : 0.0f;
            const float pos = nv == 1 ? 0.0f : ((float) k / (float) (nv - 1) * 2.0f - 1.0f) * widthT;       // -1 .. 1 across the voices
            const float a = (juce::jlimit (-1.0f, 1.0f, pos) + 1.0f) * 0.25f * kPi;
            pl[k] = std::cos (a); pr[k] = std::sin (a);
            inc[k] = (double) rt * (0.8 + 0.07 * k) / sr;                                                   // each voice its own slow rate
            base[k] = (float) ((0.004 + 0.001 * k) * sr);                                                   // 4 .. 10 ms
        }
        const double dimInc = (double) juce::jlimit (0.05f, 2.0f, rt * 0.5f) / sr, amInc = dimInc / 3.0;
        for (int i = 0; i < n; ++i)
        {
            if (i == retrigAt) { for (int k = 0; k < kMax; ++k) ph[k] = (double) k / kMax; dPh = 0; }
            const float inL = L[i], inR = R != nullptr ? R[i] : inL;
            ring[0].push (inL); ring[1].push (inR);
            depthSm += gA * (depthT - depthSm); sizeSm += gA * (sizeT - sizeSm); widthSm += gA * (widthT - widthSm);
            float oL = 0, oR = 0;
            if (! dim)
            {
                for (int k = 0; k < kMax; ++k)
                {
                    g[k] += gA * (gT[k] - g[k]);
                    if (g[k] < 1.0e-4f) continue;
                    ph[k] += inc[k]; if (ph[k] >= 1.0) ph[k] -= 1.0;
                    const float sgn = (k & 1) ? -1.0f : 1.0f;                                                 // sharp / flat pairs
                    const double d = base[k] + sgn * depthSm * std::sin (ph[k] * kTwoPi) + depthSm + 2.0;
                    const float v = ((k & 1) ? ring[1] : ring[0]).read (d) * g[k];
                    oL += v * pl[k]; oR += v * pr[k];
                }
            }
            else
            {
                dPh += dimInc; if (dPh >= 1.0) dPh -= 1.0;
                amPh += amInc; if (amPh >= 1.0) amPh -= 1.0;
                static const float baseMs[4] { 5.5f, 8.3f, 11.9f, 15.7f };
                const float stretch = 1.0f + 1.5f * sizeSm, dep = (float) (0.0006 * sr) * (0.5f + 0.5f * juce::jlimit (0.0f, 1.0f, detune()));
                float d4[4];
                for (int k = 0; k < 4; ++k)
                {
                    const double lfo = std::sin ((dPh + 0.25 * k) * kTwoPi);                                  // quadrature: the four lines never move together
                    const double am = 0.75 + 0.25 * std::sin ((amPh + 0.25 * k) * kTwoPi);                   // slow, out-of-phase level breathing
                    const double d = baseMs[k] * 0.001 * sr * stretch + dep * lfo + dep + 2.0;
                    d4[k] = ((k & 1) ? ring[1] : ring[0]).read (d) * (float) am;
                    if (sizeSm > 0.5f)                                                                        // SIZE past half: a second, longer layer
                        d4[k] += ((k & 1) ? ring[0] : ring[1]).read (d * 2.0 + 7.0) * (float) am * (sizeSm - 0.5f) * 1.2f;
                }
                oL = (d4[0] - 0.5f * d4[2]) * 0.8f; oR = (d4[1] - 0.5f * d4[3]) * 0.8f;
                // WIDTH: M/S on the four-line field
                const float m = 0.5f * (oL + oR), sd = 0.5f * (oL - oR) * widthSm; oL = m + sd; oR = m - sd;
            }
            L[i] = oL; if (R != nullptr) R[i] = oR;
        }
        if (tm != nullptr) { float pk = 0; for (int i = 0; i < n; ++i) pk = std::max (pk, std::abs (L[i])); tm->hyperLevel.store (pk, std::memory_order_relaxed); }
    }
private:
    static constexpr int kMax = 7;
    P mode, voices, detune, rate, width, retrig, size;
    double sr = 48000; Ring ring[2];
    double ph[kMax] {}, dPh = 0, amPh = 0; float g[kMax] {}, gA = 0.001f, depthSm = 0, sizeSm = 0, widthSm = 1;
};

inline void addHyperParameters (juce::AudioProcessorValueTreeState::ParameterLayout& l)
{
    auto T = [] (const char* s) { return juce::String ("FX ") + unitInfo (uHyper).title + " " + s; };
    addChoice (l, pid (uHyper, "mode"), T ("Mode"), hyperModeNames(), 0);
    l.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { pid (uHyper, "voices"), 1 }, T ("Voices"), 1, 7, 4));
    addPct (l, pid (uHyper, "detune"), T ("Detune"), 0.35f);
    addFloat (l, pid (uHyper, "rate"), T ("Rate"), skewRange (0.05f, 8.0f, 0.6f), 0.6f, "Hz", 2);
    addPct (l, pid (uHyper, "width"), T ("Width"), 1.0f);
    addBool (l, pid (uHyper, "retrig"), T ("Retrig"), true);
    addPct (l, pid (uHyper, "size"), T ("Size"), 0.4f);
}
} // namespace gpfx
#endif // ! GPFX_BOX
