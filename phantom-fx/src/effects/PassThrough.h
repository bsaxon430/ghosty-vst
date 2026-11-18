// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX: the pass-through effect the Phase 1 test harness runs the whole check table against.
// A clean gain + width stage: at defaults it nulls against the input; its controls exercise smoothing, stereo handling and latency honesty.
#pragma once
#include "../dsp/Smoothing.h"

namespace phantom
{
class PassThrough final : public EffectBase
{
public:
    enum { pGain, pWidth, pDelay, pPolarity };
    const char* key() const override { return "pass"; }
    const char* title() const override { return "PASS"; }
    std::vector<ParamSpec> parameters() const override
    {
        return { ParamSpec::range ("gain", "Gain", -24.0f, 24.0f, 0.0f, "dB"),
                 ParamSpec::range ("width", "Width", 0.0f, 2.0f, 1.0f, "x"),
                 ParamSpec::choice ("delay", "Test Latency", { "0", "1 SAMPLE", "64 SAMPLES", "1000 SAMPLES" }, 0),
                 ParamSpec::toggle ("polarity", "Polarity Flip", false) };
    }
    int getLatencySamples() const override { return latencyFor (choice (pDelay)); }

    void process (juce::dsp::AudioBlock<float> block, const ModulationContext& ctx) noexcept override
    {
        beginBlock (ctx);
        const int n = (int) block.getNumSamples(), ch = (int) block.getNumChannels();
        const int lat = latencyFor (choice (pDelay));
        const float pol = on (pPolarity) ? -1.0f : 1.0f;
        for (int i = 0; i < n; ++i)
        {
            const float g = p (pGain).nextGain() * pol, w = p (pWidth).next();
            float l = block.getChannelPointer (0)[i], r = ch > 1 ? block.getChannelPointer (1)[i] : l;
            if (lat > 0) { l = push (0, l, lat); r = push (1, r, lat); }
            const float m = 0.5f * (l + r), s = 0.5f * (l - r) * w;
            block.getChannelPointer (0)[i] = (m + s) * g;
            if (ch > 1) block.getChannelPointer (1)[i] = (m - s) * g;
        }
    }
protected:
    void prepareEffect (double, int, int) override { ring.setSize (2, 1024); ring.clear(); wpos = 0; }
    void resetEffect() override { ring.clear(); wpos = 0; }
private:
    static int latencyFor (int c) noexcept { static const int l[] { 0, 1, 64, 1000 }; return l[juce::jlimit (0, 3, c)]; }
    inline float push (int c, float x, int lat) noexcept
    {
        auto* d = ring.getWritePointer (c); const int N = ring.getNumSamples();
        int ri = wpos - lat; if (ri < 0) ri += N;
        const float out = d[ri]; d[wpos] = x; if (c == 1 || ring.getNumChannels() == 1) { if (++wpos == N) wpos = 0; }
        return out;
    }
    juce::AudioBuffer<float> ring; int wpos = 0;
};
} // namespace phantom
