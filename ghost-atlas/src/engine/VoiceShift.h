// SPDX-License-Identifier: AGPL-3.0-or-later
// GHOST ATLAS - VOICE SHIFT: pitch and formant shifting for a layer, done by Signalsmith Stretch (MIT; the copy Phantom
// FX already pins, v1.3.2) through its documented calls only: setTransposeSemitones() moves the pitch,
// setFormantSemitones() moves the vowel colour, and its formant compensation keeps a voice sounding like the same throat
// while the pitch moves. It is one more effect a layer can hold, shaped like Phantom FX's own units so it sits in the
// same slots.
//
// What it costs: the stretcher works on a window of sound, so the layer it sits on speaks late by that window
// (SMOOTH: 120 ms, the library's own default preset; TIGHT: 60 ms, a little rougher on low notes). The slot reports
// that as its latency, so MIX stays in time with the untouched signal.
#pragma once
#include "core/PhantomEffect.h"
#include "signalsmith-stretch/signalsmith-stretch.h"

namespace atlas
{
class VoiceShiftFx final : public phantom::PhantomEffect
{
public:
    enum { iPitch, iFormant, iKeep, iFine, iSize };
    const char* key() const override { return "shift"; }
    const char* title() const override { return "VOICE SHIFT"; }
    std::vector<phantom::ParamSpec> parameters() const override
    {
        using P = phantom::ParamSpec;
        return { P::range ("pitch", "PITCH", -24.0f, 24.0f, 0.0f, "st"),
                 P::range ("formant", "FORMANT", -12.0f, 12.0f, 0.0f, "st"),
                 P::toggle ("keep", "KEEP VOICE", true),
                 P::range ("fine", "FINE", -100.0f, 100.0f, 0.0f, "ct"),
                 P::choice ("size", "WINDOW", { "SMOOTH", "TIGHT" }, 0) };
    }
    void prepare (double sampleRate, int maxBlockSize, int numChannels) override
    {
        sr = sampleRate; channels = juce::jlimit (1, 2, numChannels);
        scratch.setSize (2, juce::jmax (16, maxBlockSize));
        configure();
    }
    void reset() override { stretch.reset(); scratch.clear(); lastPitch = lastFormant = 1.0e9f; lastKeep = -1; }
    // message / prepare thread (never while process() runs): the window is the one setting that needs memory
    void updateParameters (const phantom::ModulationContext& ctx) override
    {
        const int want = ctx.numValues > iSize ? juce::jlimit (0, 1, (int) std::lround (ctx.value (iSize))) : size;
        if (want != size) { size = want; configure(); }
    }
    void process (juce::dsp::AudioBlock<float> block, const phantom::ModulationContext& ctx) noexcept override
    {
        const int n = (int) block.getNumSamples(), ch = juce::jmin (channels, (int) block.getNumChannels());
        if (n <= 0 || n > scratch.getNumSamples() || ch <= 0) return;
        const float pitch = ctx.value (iPitch) + ctx.value (iFine) * 0.01f, formant = ctx.value (iFormant);
        const int keep = ctx.value (iKeep) >= 0.5f ? 1 : 0;
        if (pitch != lastPitch) { stretch.setTransposeSemitones (pitch); lastPitch = pitch; }
        if (formant != lastFormant || keep != lastKeep) { stretch.setFormantSemitones (formant, keep != 0); lastFormant = formant; lastKeep = keep; }
        float* in[2] = { block.getChannelPointer (0), block.getChannelPointer ((size_t) juce::jmin (1, ch - 1)) };
        float* out[2] = { scratch.getWritePointer (0), scratch.getWritePointer (1) };
        stretch.process (in, n, out, n);
        for (int c = 0; c < ch; ++c) juce::FloatVectorOperations::copy (block.getChannelPointer ((size_t) c), scratch.getReadPointer (c), n);
    }
    int getLatencySamples() const override { return latency; }
    bool hasNullSetting() const override { return false; }
    int warmupSamples() const override { return latency; }

private:
    void configure()
    {
        if (size == 1) stretch.configure (channels, (int) (sr * 0.06), (int) (sr * 0.015));
        else stretch.presetDefault (channels, (float) sr);
        latency = stretch.inputLatency() + stretch.outputLatency();
        reset();
    }
    signalsmith::stretch::SignalsmithStretch<float> stretch { 0x61a5 };
    juce::AudioBuffer<float> scratch;
    double sr = 48000.0;
    int channels = 2, latency = 0, lastKeep = -1, size = 0;
    float lastPitch = 1.0e9f, lastFormant = 1.0e9f;
};
} // namespace atlas
