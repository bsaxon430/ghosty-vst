/*
 * Ghost Bass b10 - SUB layer = Bombaz (GPL-3.0, (c) Matthijs Hollemans, https://github.com/hollance/bombaz).
 * The upstream files below are compiled exactly as they are (pinned submodule libs/ghostbass-layers/bombaz).
 * They sit in a namespace only so their names (Oscillator, Voice, Synth, Parameters...) cannot clash with Surge's.
 */
#include "shim/JuceHeader.h"
#include "LayerEngines.h"
#include "LayerHostProcessor.h"

namespace upstream_bombaz
{
#include "DCBlockFilter.h"
#include "DSP.h"
#include "Oscillator.h"
#include "Parameters.h"
#include "Voice.h"
#include "Synth.h"
#include "Parameters.cpp"
#include "Synth.cpp"
} // namespace upstream_bombaz

namespace ghost::layers
{
struct SubEngine::Impl
{
    GhostLayerHostProcessor host;
    juce::AudioProcessorValueTreeState apvts;
    upstream_bombaz::Parameters params;
    upstream_bombaz::Synth synth;
    Impl()
        : apvts(host, nullptr, "Parameters", upstream_bombaz::Parameters::createParameterLayout()), params(apvts), synth(params)
    {
    }
};

SubEngine::SubEngine() : impl(std::make_unique<Impl>()) {}
SubEngine::~SubEngine() = default;
juce::AudioProcessorValueTreeState &SubEngine::apvts() { return impl->apvts; }

// same order as BassSynthAudioProcessor::prepareToPlay upstream
void SubEngine::prepare(double sr, int maxBlock)
{
    impl->params.prepareToPlay((float)sr);
    impl->params.reset();
    impl->synth.prepareToPlay((float)sr, maxBlock);
    impl->synth.reset();
}
void SubEngine::reset() { impl->synth.reset(); }
void SubEngine::midi(uint8_t d0, uint8_t d1, uint8_t d2) { impl->synth.midiMessage(d0, d1, d2); }
void SubEngine::render(float *mono, int n)
{
    juce::ScopedNoDenormals noDenormals; // as upstream's processBlock
    impl->params.update(); // upstream does this once per processBlock
    impl->synth.render(mono, n);
}
} // namespace ghost::layers
