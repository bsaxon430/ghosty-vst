/*
 * Poltergeist 2 (GhostBlock) - SUB checks: upstream Bombaz, compiled as it is (see bombaz-reference.h).
 *
 * Bombaz by M.I. Hollemans - https://github.com/hollance/bombaz - GPL-3.0-or-later. The four includes at the bottom
 * of this comment's namespace are upstream's own files from libs/ghostblock-sub/bombaz/src, unmodified; they sit in a
 * namespace so their global names (Synth, Voice, Parameters, Oscillator) cannot meet anything else in the program.
 */
#include <JuceHeader.h> // (tests/juce-header: the real JUCE module)
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include "bombaz-reference.h"

namespace bombaz_upstream
{
#include "Parameters.cpp"
#include "Synth.cpp"
} // namespace bombaz_upstream

namespace p2sub_ref
{
namespace
{
// an AudioProcessorValueTreeState needs a processor to belong to
struct Holder : juce::AudioProcessor
{
    const juce::String getName() const override { return "bombaz-reference"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override {}
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor *createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String &) override {}
    void getStateInformation(juce::MemoryBlock &) override {}
    void setStateInformation(const void *, int) override {}
};
} // namespace

struct BombazRef::Impl
{
    Holder holder;
    juce::AudioProcessorValueTreeState apvts{holder, nullptr, "Parameters", bombaz_upstream::Parameters::createParameterLayout()};
    bombaz_upstream::Parameters params{apvts};
    bombaz_upstream::Synth synth{params};
};

BombazRef::BombazRef() : impl(std::make_unique<Impl>()) {}
BombazRef::~BombazRef() = default;
void BombazRef::prepare(float sampleRate)
{
    impl->params.prepareToPlay(sampleRate);
    impl->params.reset();
    impl->synth.prepareToPlay(sampleRate, 512);
    impl->synth.reset();
}
void BombazRef::set(float widthPercent, float drivePercent, int octaveIndex, float tuneCents, float glideMs)
{
    *impl->params.widthParam = widthPercent;
    *impl->params.driveParam = drivePercent;
    *impl->params.octaveParam = octaveIndex;
    *impl->params.tuningParam = tuneCents;
    *impl->params.glideTimeParam = glideMs;
}
void BombazRef::midi(uint8_t d0, uint8_t d1, uint8_t d2) { impl->synth.midiMessage(d0, d1, d2); }
void BombazRef::update() { impl->params.update(); }
void BombazRef::render(float *out, int n) { impl->synth.render(out, n); }
float BombazRef::width() const { return impl->params.widthParam->get(); }
float BombazRef::drive() const { return impl->params.driveParam->get(); }
float BombazRef::glideMs() const { return impl->params.glideTimeParam->get(); }
int BombazRef::transpose() const { return impl->params.transpose; }
float BombazRef::pitchModulation() const { return impl->params.pitchModulation; }
} // namespace p2sub_ref
