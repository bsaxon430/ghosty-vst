// SPDX-License-Identifier: AGPL-3.0-or-later
// GHOST GRID's lane effects are Mango's own (odoare/Mango, AGPL-3.0-or-later; FxmeTools core, LGPL-3.0-or-later; WDL FFT, zlib), compiled
// unchanged from third_party/mango and third_party/fxmetools. Mango binds its effects to an AudioProcessorValueTreeState; GHOST GRID owns a
// small private processor that holds exactly those parameters (it is never shown to the host), and writes them from its own row knobs
// when a painted block starts. Mango's own model: a block on a lane enters (random draws keyed on seed / lane / block), processes, exits.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <FxmeTools/dsp/DeterministicRandom.h>
#include <FxmeTools/midi/NoteDuration.h>
#include <FxmeTools/dsp/Biquad.h>
#include <FxmeTools/dsp/BitCrusher.h>
#include <FxmeTools/dsp/Downsampler.h>
#include <FxmeTools/dsp/DelayLine.h>
#include <FxmeTools/dsp/FormantFilter.h>
#include <FxmeTools/dsp/GrainLooper.h>
#include <FxmeTools/dsp/Reverb.h>
#include <FxmeTools/dsp/PitchShifter.h>
#include "../../third_party/mango/Effects/GaterEffect.h"
#include "../../third_party/mango/Effects/GrainDupEffect.h"
#include "../../third_party/mango/Effects/ReverserEffect.h"
#include "../../third_party/mango/Effects/FreezeEffect.h"
#include "../../third_party/mango/Effects/QuantizerEffect.h"
#include "../../third_party/mango/Effects/FilterEnvEffect.h"
#include "../../third_party/mango/Effects/DelayEffect.h"

namespace phantom
{
// the private parameter holder Mango's effects bind to (never hosted, never saved: GHOST GRID's own parameters are the saved state)
class MangoParamHost final : public juce::AudioProcessor
{
public:
    explicit MangoParamHost (juce::AudioProcessorValueTreeState::ParameterLayout l) : apvts (*this, nullptr, "MANGO", std::move (l)) {}
    const juce::String getName() const override { return "lanes"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
    // audio thread: Mango's effects read these atomics when a block enters (denormalised values, as APVTS stores them)
    void set (const juce::String& id, float v) noexcept { if (auto* a = apvts.getRawParameterValue (id)) a->store (v, std::memory_order_relaxed); }
    std::atomic<float>* raw (const juce::String& id) { return apvts.getRawParameterValue (id); }
    juce::AudioProcessorValueTreeState apvts;
};
} // namespace phantom
