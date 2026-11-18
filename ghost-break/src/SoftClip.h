// Ghost Break: each bay's CLIP — FL Studio's Fruity Soft Clipper, the same maths.
//
// Ported from Lemon Studio's recreation (crates/os-engine/src/effects/soft_clipper.rs and crates/os-dsp/src/level.rs,
// GPL-3.0-or-later, https://github.com/ItsAlphaNeon/Lemon-Studio), which nulls sample for sample against FL Studio's own
// plugin (13 of 13 cases). Ghost Break stays private on Brandon's Mac.
//
// No memory, no oversampling (FL runs it at the session rate, its grit included): each sample above the threshold is bent
// onto an exponential curve that leaves the threshold with slope 1 and approaches full scale; below it the sample passes
// unchanged. Then the post gain. THRESHOLD 1..127 (FL's default 100 = 0.78125), POST 0..160 (128 = unity), as FL stores them.
#pragma once
#include "core/PhantomEffect.h"
#include <cmath>

namespace gbk
{
struct FlSoftClip
{
    float t = 0.78125f, k = 0.21875f, r = 1.0f / 0.21875f, gain = 1.0f; int thres = 100, post = 128;

    void set (int th, int po) noexcept
    {
        if (th == thres && po == post) return;
        thres = th; post = po;
        t = (float) th * 0.0078125f; k = 1.0f - t; if (k <= 0.0f) k = 1.0f / 65536.0f; r = 1.0f / k;
        gain = postGain (po);
    }
    // FL's level curve (11^x - 1) / 10 in the host's precision; 128 is exactly 1
    static float postGain (int po) noexcept
    {
        const float x = (float) po * 0.0078125f; if (x == 1.0f) return 1.0f;
        const float p = x * (float) std::log (11.0);
        return (float) ((std::exp ((double) p) - 1.0) * (double) 0.1f);
    }
    static float clip (float s, float t, float k, float r) noexcept
    {
        const float a = std::abs (s);
        if (a > t || std::isnan (a))
        {
            double m = -std::expm1 (((double) t - (double) a) * (double) r);
            if (m <= 0.5) { constexpr double two64 = 18446744073709551616.0; m = std::nearbyint (m * two64) / two64; }   // FL's 80-bit exponential: 1 - exp on a 2^-64 grid
            return std::copysign ((float) (m * (double) k + (double) t), s);
        }
        return s;
    }
    float sample (float s) const noexcept { const float y = clip (s, t, k, r); return gain != 1.0f ? y * gain : y; }
};

class SoftClipFx final : public phantom::PhantomEffect
{
public:
    const char* key() const override { return "softclip"; }
    const char* title() const override { return "SOFT CLIPPER"; }
    std::vector<phantom::ParamSpec> parameters() const override
    {   // continuous for the host (no re-latency on every turn), rounded to FL's whole steps here
        auto th = phantom::ParamSpec::range ("thres", "Threshold", 1.0f, 127.0f, 100.0f, ""); th.smoothMs = 0; th.modDepth = 0;
        auto po = phantom::ParamSpec::range ("post", "Post Gain", 0.0f, 160.0f, 128.0f, ""); po.smoothMs = 0; po.modDepth = 0;
        return { th, po };
    }
    void prepare (double, int, int) override {}
    void reset() override {}
    void process (juce::dsp::AudioBlock<float> block, const phantom::ModulationContext& ctx) noexcept override
    {
        c.set ((int) std::lround (ctx.value (0)), (int) std::lround (ctx.value (1)));   // nothing glides: a change acts from this block on, as in FL
        for (size_t ch = 0; ch < block.getNumChannels(); ++ch)
        {
            float* x = block.getChannelPointer (ch);
            for (size_t i = 0; i < block.getNumSamples(); ++i) x[i] = c.sample (x[i]);
        }
    }
    int getLatencySamples() const override { return 0; }
    bool changesLevelOnPurpose() const override { return true; }
    bool hasNullSetting() const override { return false; }
private:
    FlSoftClip c;
};
} // namespace gbk
