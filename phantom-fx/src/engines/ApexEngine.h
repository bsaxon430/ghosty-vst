// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX: LYNX SHARE's Apex Limiter (Ka1-S/LYNX-SHARE @ d141370, src/core/ApexLimiter.{h,cpp}, AGPL-3.0, vendored unchanged with the
// oversampler excerpt of MixSuite it uses) as the APEX engine of GHOST LIMITER, its eight styles as voices. GAIN, CEILING, RELEASE,
// LOOKAHEAD (up to its 5 ms) and TRUE PEAK are LYNX's own controls; its latency is constant (its longest look-ahead), as ours is.
#pragma once
#include "AltEngine.h"
#include "core/ApexLimiter.h"

namespace phantom::engines
{
class ApexEngine final : public AltEngine
{
public:
    static constexpr int kVoices = lynx::apex::styleCount;
    static std::string voiceName (int i) { std::string s = lynx::apex::styleName ((lynx::apex::Style) juce::jlimit (0, kVoices - 1, i)); if (! s.empty()) s[0] = (char) std::toupper ((unsigned char) s[0]); return s; }
    ApexEngine() { const auto& inf = lynx::apex::info(); base.fill (0.0f); for (size_t i = 0; i < inf.params.size(); ++i) base[i] = inf.params[i].defaultValue; for (auto& f : fx) f = lynx::apex::make (&base); }
    void prepare (double sampleRate, int maxBlock) override
    {
        sr = sampleRate; for (auto& f : fx) { f->prepare (sampleRate); f->reset(); }
        const int mb = juce::jmax (maxBlock, 64); for (auto& b : tmp) b.assign ((size_t) mb, 0.0f); reset();
    }
    void reset() noexcept override { for (auto& f : fx) f->reset(); primed = false; cur = 0; next = -1; xf = 1.0f; settle = 0; built = { -1.0f, -1.0f }; pending = -1.0f; }
    int latencySamples (int) const noexcept override { return fx[0]->latencySamples(); }   // (constant: its longest look-ahead)
    void process (float* L, float* R, int n, const Controls& c, int voice, const TempoInfo& t) noexcept override
    {
        // GAIN is applied here, per sample, gliding 150 ms (a fast push into a limiter is heard as its gain reduction jumping); CEILING glides
        // 30 ms and reaches the limiter every 32 samples. LOOKAHEAD sizes its gain window: a second limiter takes a new one once the control
        // has settled (200 ms) and the two crossfade over 50 ms (both have the same constant latency)
        const float gT = c.get ("gain", 0.0f), cT = c.get ("ceiling", -0.3f), laT = std::round (juce::jlimit (0.0f, 5.0f, c.get ("lookahead", 2.0f)) * 10.0f) / 10.0f;
        if (! primed) { gSm = gT; cSm = cT; primed = true; }
        if (laT != pending) { pending = laT; settle = 0; } else settle += n;
        if (built[(size_t) cur] < 0.0f) built[(size_t) cur] = laT;
        if (next < 0 && laT != built[(size_t) cur] && settle >= (int) (0.2 * sr)) { next = 1 - cur; built[(size_t) next] = laT; fx[(size_t) next]->reset(); xf = 0.0f; }
        const float ag = 1.0f - std::exp (-1.0f / (0.15f * (float) sr)), ac = 1.0f - std::exp (-32.0f / (0.03f * (float) sr));
        using namespace lynx::apex; auto p = base;
        p[pRelease] = releaseValue (c.get ("release", 80.0f));
        p[pStyle] = (float) juce::jlimit (0, kVoices - 1, voice) / (float) (kVoices - 1);
        p[pTruePeak] = c.get ("truepeak", 1.0f) >= 0.5f ? 1.0f : 0.0f; p[pGain] = 0.0f;
        for (int i0 = 0; i0 < n; i0 += 32)
        {
            const int m = juce::jmin (32, n - i0); cSm += ac * (cT - cSm);
            for (int i = i0; i < i0 + m; ++i) { gSm += ag * (gT - gSm); const float g = juce::Decibels::decibelsToGain (gSm); L[i] *= g; R[i] *= g; }
            p[pCeiling] = inverse01 ([] (float v) { return ceilingDb (v); }, cSm);
            if (next >= 0)
            {
                std::copy (L + i0, L + i0 + m, tmp[0].begin()); std::copy (R + i0, R + i0 + m, tmp[1].begin());
                auto pn = p; pn[pLookahead] = inverse01 ([] (float v) { return lookaheadMs (v); }, built[(size_t) next]); fx[(size_t) next]->process (tmp[0].data(), tmp[1].data(), m, pn, t.bpm);
            }
            p[pLookahead] = inverse01 ([] (float v) { return lookaheadMs (v); }, built[(size_t) cur]); fx[(size_t) cur]->process (L + i0, R + i0, m, p, t.bpm);
            if (next >= 0)
            {
                const float step = 1.0f / (0.05f * (float) sr);
                for (int i = 0; i < m; ++i) { xf = juce::jmin (1.0f, xf + step); const float a = std::cos (xf * juce::MathConstants<float>::halfPi), b = std::sin (xf * juce::MathConstants<float>::halfPi); L[i0 + i] = L[i0 + i] * a + tmp[0][(size_t) i] * b; R[i0 + i] = R[i0 + i] * a + tmp[1][(size_t) i] * b; }
                if (xf >= 1.0f) { cur = next; next = -1; }
            }
        }
    }
private:
    std::array<std::unique_ptr<lynx::Effect>, 2> fx; lynx::EffectParams base {}; std::array<std::vector<float>, 2> tmp; double sr = 48000.0;
    float gSm = 0.0f, cSm = 0.0f, xf = 1.0f, pending = -1.0f; std::array<float, 2> built { -1.0f, -1.0f }; int cur = 0, next = -1, settle = 0; bool primed = false;
};
} // namespace phantom::engines
