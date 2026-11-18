// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX: LYNX SHARE's Prism Multiband (Ka1-S/LYNX-SHARE @ d141370, src/core/DynamicsSuite.{h,cpp} with FacetEq, ParametricEqLinear,
// ParametricEqFl, AGPL-3.0, vendored unchanged) as the PRISM engine of GHOST SQUEEZE (DECISIONS.md 86). GHOST SQUEEZE squeezes both ways
// at once (down on the loud parts, up on the quiet ones, per band); a Prism band works one way, so two Prisms run in series on the same
// three bands (our LOW / HIGH CROSSOVER): the first pushes down, the second lifts up. DOWNWARD and UPWARD scale each one's range and ratio,
// TIME their attack and release, INPUT / OUTPUT and the band GAINs are Prism's own trims and band levels. The voices are our own settings
// of Prism's controls (thresholds, ratios, knees, speeds) from gentle glue to a full smash.
#pragma once
#include "AltEngine.h"
#include "core/DynamicsSuite.h"
#include "core/Effects.h"

namespace phantom::engines
{
class PrismEngine final : public AltEngine
{
public:
    // per voice: downward threshold dB, ratio, range dB (at our DOWNWARD default); upward threshold, ratio, range (at UPWARD's default);
    // knee dB; attack / release 0..1 (Prism's own law); the low band's share (1 = as the others)
    struct Voice { const char* name; float dThr, dRatio, dRange, uThr, uRatio, uRange, knee, att, rel, low; };
    static const Voice& voice (int i)
    {
        static const Voice k[] {
            { "Glue", -20.0f, 2.0f, -6.0f, -50.0f, 1.5f, 4.0f, 24.0f, 0.35f, 0.35f, 1.0f },
            { "Punch", -18.0f, 4.0f, -10.0f, -45.0f, 2.0f, 6.0f, 12.0f, 0.55f, 0.2f, 1.0f },
            { "Squash", -26.0f, 6.0f, -18.0f, -48.0f, 3.0f, 12.0f, 18.0f, 0.15f, 0.15f, 1.0f },
            { "Smash", -26.0f, 10.0f, -30.0f, -42.0f, 4.0f, 16.0f, 6.0f, 0.05f, 0.08f, 1.0f },
            { "Lift", -14.0f, 2.0f, -4.0f, -40.0f, 4.0f, 18.0f, 18.0f, 0.3f, 0.3f, 0.6f },
            { "Air & Body", -22.0f, 3.0f, -10.0f, -46.0f, 3.0f, 12.0f, 24.0f, 0.3f, 0.25f, 1.4f },
        };
        return k[juce::jlimit (0, kVoices - 1, i)];
    }
    static constexpr int kVoices = 6;
    explicit PrismEngine (const std::vector<ParamSpec>& specs)
    {
        for (auto& s : specs) defs[s.id] = s.def;
        const auto& inf = lynx::dyn::prismInfo(); base.fill (0.0f); for (size_t i = 0; i < inf.params.size(); ++i) base[i] = inf.params[i].defaultValue;
        using namespace lynx::dyn::prism;
        base[(size_t) bandCountParam] = bandCountValue (3); base[(size_t) mix] = 1.0f; base[(size_t) lookahead] = 0.0f;
        for (auto& f : fx) f = lynx::dyn::make (lynx::EffectKind::prismMultiband, &base);
    }
    void prepare (double sampleRate, int) override { sr = sampleRate; for (auto& f : fx) { f->prepare (sampleRate); f->reset(); } primed = false; }
    void reset() noexcept override { for (auto& f : fx) f->reset(); primed = false; }
    int latencySamples (int) const noexcept override { return fx[0]->latencySamples() + fx[1]->latencySamples(); }
    void process (float* L, float* R, int n, const Controls& c, int v, const TempoInfo& t) noexcept override
    {
        float tg[kCtl] { c.get ("time", 0.5f), c.get ("up", 0.6f), c.get ("down", 0.7f), c.get ("in", 0.0f), c.get ("out", 0.0f),
                         c.get ("lowgain", 0.0f), c.get ("midgain", 0.0f), c.get ("highgain", 0.0f), c.get ("xlo", 120.0f), c.get ("xhi", 2500.0f) };
        if (! primed) { std::copy (tg, tg + kCtl, sm); primed = true; }
        const float a = 1.0f - std::exp (-32.0f / (0.06f * (float) sr));
        const auto& vo = voice (v);
        for (int i0 = 0; i0 < n; i0 += 32)
        {
            const int m = juce::jmin (32, n - i0); for (int k = 0; k < kCtl; ++k) sm[k] += a * (tg[k] - sm[k]);
            auto pd = base, pu = base; configure (pd, pu, vo);
            fx[0]->process (L + i0, R + i0, m, pd, t.bpm); fx[1]->process (L + i0, R + i0, m, pu, t.bpm);
        }
    }
private:
    enum Ctl { cTime, cUp, cDown, cIn, cOut, cLow, cMid, cHigh, cXlo, cXhi, kCtl };
    float def (const char* id, float fb) const { auto it = defs.find (id); return it == defs.end() ? fb : it->second; }
    void configure (lynx::EffectParams& pd, lynx::EffectParams& pu, const Voice& vo) const noexcept
    {
        using namespace lynx::dyn::prism;
        const float down = sm[cDown] / juce::jmax (0.01f, def ("down", 0.7f)), up = sm[cUp] / juce::jmax (0.01f, def ("up", 0.6f));
        const float speed = juce::jlimit (0.0f, 1.0f, sm[cTime]);   // TIME: 0 fast .. 1 slow, our default = the voice's own
        const float att = juce::jlimit (0.0f, 1.0f, vo.att + (speed - def ("time", 0.5f)) * 0.8f), rel = juce::jlimit (0.0f, 1.0f, vo.rel + (speed - def ("time", 0.5f)) * 0.8f);
        const float xlo = crossoverValue (juce::jlimit (30.0, 18000.0, (double) sm[cXlo])), xhi = crossoverValue (juce::jlimit (30.0, 18000.0, (double) juce::jmax (sm[cXhi], sm[cXlo] * 1.5f)));
        auto common = [&] (lynx::EffectParams& p)
        {
            p[(size_t) xover1] = xlo; p[(size_t) xover2] = xhi;
            for (int b = 0; b < 3; ++b)
            {
                p[(size_t) param (b, fKnee)] = inverse01 ([] (float x) { return kneeDb (x); }, (double) vo.knee);
                p[(size_t) param (b, fAttack)] = att; p[(size_t) param (b, fRelease)] = rel; p[(size_t) param (b, fMode)] = 0.0f; p[(size_t) param (b, fEnabled)] = 1.0f;
                p[(size_t) param (b, fLevel)] = 0.5f;
            }
        };
        common (pd); common (pu);
        const float shares[3] { vo.low, 1.0f, 1.0f };
        for (int b = 0; b < 3; ++b)
        {   // the downward stage (range < 0) and the upward stage (range > 0), scaled by DOWNWARD / UPWARD; 0 = that stage does nothing
            const float dr = juce::jlimit (-30.0f, 0.0f, vo.dRange * down * shares[b]), ur = juce::jlimit (0.0f, 30.0f, vo.uRange * up * shares[b]);
            pd[(size_t) param (b, fThreshold)] = thresholdValue (vo.dThr); pd[(size_t) param (b, fRatio)] = inverse01 ([] (float x) { return ratioOf (x); }, 1.0 + (vo.dRatio - 1.0) * juce::jlimit (0.0f, 2.0f, down));
            pd[(size_t) param (b, fRange)] = inverse01 ([] (float x) { return rangeDb (x); }, (double) dr);
            pu[(size_t) param (b, fThreshold)] = thresholdValue (vo.uThr); pu[(size_t) param (b, fRatio)] = inverse01 ([] (float x) { return ratioOf (x); }, 1.0 + (vo.uRatio - 1.0) * juce::jlimit (0.0f, 2.0f, up));
            pu[(size_t) param (b, fRange)] = inverse01 ([] (float x) { return rangeDb (x); }, (double) ur);
        }
        // INPUT into the first stage; OUTPUT and the band GAINs after the second
        pd[(size_t) input] = inverse01 ([] (float x) { return trimDb (x); }, (double) sm[cIn]);
        pu[(size_t) output] = inverse01 ([] (float x) { return trimDb (x); }, (double) sm[cOut]);
        const float gains[3] { sm[cLow], sm[cMid], sm[cHigh] };
        for (int b = 0; b < 3; ++b) pu[(size_t) param (b, fLevel)] = inverse01 ([] (float x) { return levelDb (x); }, (double) gains[b]);
    }
    std::array<std::unique_ptr<lynx::Effect>, 2> fx; lynx::EffectParams base {}; std::map<std::string, float> defs; double sr = 48000.0; float sm[kCtl] {}; bool primed = false;
};
} // namespace phantom::engines
