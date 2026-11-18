// Ghost Break: each bay's CRUSH — Brandon's two favourite Phantom FX dirt sounds side by side, blended:
//   TUBE: WRECKER on its TUBE style (the PHANTOM engine)
//   DOOM: CRACKLE on its CURVES engine, set DOOMCOMPPARALLEL
// Both run as the real Phantom FX modules (their own oversampling, X4 as in Phantom FX, and their own auto gain), in parallel on the
// same input, lined up to the sample, then BLEND crossfades from TUBE (0) to DOOM (1). DRIVE drives both; OUTPUT trims the result.
#pragma once
#include "core/PhantomEffect.h"
#include "effects/Catalog.h"
#include "engines/Engines.h"
#include "rack/EffectSlot.h"
#include "dsp/DelayLine.h"

namespace gbk
{
class CrushFx final : public phantom::PhantomEffect
{
public:
    CrushFx()
    {
        tube = std::make_unique<phantom::EffectSlot> (phantom::makeEffect (phantom::uWreck));
        doom = std::make_unique<phantom::EffectSlot> (phantom::makeEffect (phantom::uCrackle));
        for (auto* s : { tube.get(), doom.get() }) { s->setPolicy (phantom::OversamplingPolicy::X4); s->setEnabled (true); s->setMix (1.0f); }
        // the fixed settings: WRECKER's TUBE on its own engine; CRACKLE's CURVES engine on DOOMCOMPPARALLEL
        set (*tube, "engine", 0.0f); set (*tube, "style", 1.0f);
        const auto labels = phantom::engines::engineLabels (phantom::uCrackle);
        for (size_t i = 0; i < labels.size(); ++i) if (labels[i] == "CURVES \xc2\xb7 DOOMCOMPPARALLEL") doomEntry = (int) i;
        set (*doom, "engine", (float) juce::jmax (0, doomEntry));
        tubeDrive = index (*tube, "drive"); doomDrive = index (*doom, "b1_drive");
    }
    const char* key() const override { return "crush"; }
    const char* title() const override { return "CRUSH"; }
    std::vector<phantom::ParamSpec> parameters() const override
    {
        return { phantom::ParamSpec::pct ("drive", "Drive", 0.4f), phantom::ParamSpec::pct ("blend", "Blend", 0.5f),
                 phantom::ParamSpec::range ("out", "Output", -24.0f, 12.0f, 0.0f, "dB") };
    }
    void prepare (double sr, int block, int ch) override
    {
        chans = juce::jmax (1, ch);
        tube->prepare (sr, block, chans); doom->prepare (sr, block, chans);   // (their applyPolicy builds DOOMCOMPPARALLEL's curve tables)
        const int lt = tube->getLatencySamples(), ld = doom->getLatencySamples(); lat = juce::jmax (lt, ld);
        padTube.prepare (lat + 1, chans); padDoom.prepare (lat + 1, chans); padTube.setDelay (lat - lt); padDoom.setDelay (lat - ld);
        other.setSize (chans, block); outG.reset (sr, 0.02); blendS.reset (sr, 0.02);
    }
    void reset() override { tube->reset(); doom->reset(); padTube.reset(); padDoom.reset(); }
    void updateParameters (const phantom::ModulationContext&) override {}
    void process (juce::dsp::AudioBlock<float> block, const phantom::ModulationContext& ctx) noexcept override
    {
        const int n = (int) block.getNumSamples(), ch = juce::jmin ((int) block.getNumChannels(), chans);
        if (n > other.getNumSamples()) return;
        const float drive = ctx.value (0); blendS.setTargetValue (juce::jlimit (0.0f, 1.0f, ctx.value (1))); outG.setTargetValue (juce::Decibels::decibelsToGain (ctx.value (2)));
        tube->setValue (tubeDrive, drive); doom->setValue (doomDrive, drive);
        juce::dsp::AudioBlock<float> b2 (other); b2 = b2.getSubBlock (0, (size_t) n).getSubsetChannelBlock (0, (size_t) ch);
        b2.copyFrom (block.getSubsetChannelBlock (0, (size_t) ch));
        auto a = block.getSubsetChannelBlock (0, (size_t) ch);
        tube->process (a, ctx.tempo); padTube.process (a);
        doom->process (b2, ctx.tempo); padDoom.process (b2);
        for (int i = 0; i < n; ++i)
        {
            const float m = blendS.getNextValue(), g = outG.getNextValue();
            for (int c = 0; c < ch; ++c) { float* x = a.getChannelPointer ((size_t) c); x[i] = (x[i] + (b2.getChannelPointer ((size_t) c)[i] - x[i]) * m) * g; }
        }
    }
    int getLatencySamples() const override { return lat; }
    bool changesLevelOnPurpose() const override { return true; }
    bool hasNullSetting() const override { return false; }
    int doomEngineEntry() const noexcept { return doomEntry; }   // (the tests: the set was found)
private:
    static int index (const phantom::EffectSlot& s, const char* id) { const auto& p = s.params(); for (size_t i = 0; i < p.size(); ++i) if (p[i].id == id) return (int) i; return -1; }
    static void set (phantom::EffectSlot& s, const char* id, float v) { s.setValue (index (s, id), v); }
    std::unique_ptr<phantom::EffectSlot> tube, doom; phantom::IntegerDelay padTube, padDoom;
    juce::AudioBuffer<float> other; juce::SmoothedValue<float> outG { 1.0f }, blendS { 0.5f };
    int chans = 2, lat = 0, doomEntry = -1, tubeDrive = -1, doomDrive = -1;
};
} // namespace gbk
