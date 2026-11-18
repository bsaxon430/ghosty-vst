/*
 * Poltergeist 2 (GhostBlock) - checks: Spectral Corruptor's own rack engine, compiled unmodified from
 * libs/ghostblock-spectral as the REFERENCE our hosting is compared with. Built with -Dscrr=scrr_ref so its classes
 * cannot be confused with the ones inside the plugin. Test program only; never part of the plugin.
 */
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "dsp/SpectralProcessor.cpp"
#include "dsp/FFTEngine.cpp"
#include "dsp/PartialProcessor.cpp"
#include "dsp/DryWetMixer.cpp"
#include "dsp/GainStage.cpp"
#include "dsp/Oversampler.cpp"
#include "parameters/ParameterLayout.cpp"
#include "sc-reference.h"

namespace p2ref
{
namespace
{
struct Host : juce::AudioProcessor // only there to own upstream's parameter tree
{
    Host() : apvts(*this, nullptr, "PARAMS", scrr::params::createParameterLayout()) {}
    const juce::String getName() const override { return "ref"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override {}
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return false; }
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
    juce::AudioProcessorValueTreeState apvts;
    void set(const juce::String &id, float v)
    {
        auto *p = apvts.getParameter(id);
        p->setValueNotifyingHost(p->convertTo0to1(v));
    }
};
} // namespace

void run(const Setup &s, float *L, float *R, int n)
{
    using namespace scrr::params;
    Host host;
    host.set(id::oversample, 0);                        // 1x
    host.set(id::fftSizeExtended, (float)(s.fftIndex + 1)); // 128 << index
    host.set(id::windowTypeExtended, (float)(s.window + 1));
    host.set(id::overlapFactor, (float)s.overlap);
    host.set(id::frequencyLimit, (float)s.limitIndex);
    host.set(id::drywet, 100.0f);
    host.set(id::freqSplitNumSplits, (float)s.slices);
    host.set(id::freqSplitCrossfade, (float)s.crossfade);

    const char *names[5] = {"modules", "ch1", "ch2", "ch3", "ch4"};
    juce::ValueTree chains[5];
    for (int c = 0; c < 5; ++c)
    {
        chains[c] = juce::ValueTree(names[c]);
        if (c == 0 || (c > 1 && !s.allRacks)) continue;
        int k = 0;
        for (auto &m : s.chain)
        {
            auto vt = scrr::dsp::createDefaultModuleState(m.type);
            vt.setProperty("uid", juce::String(names[c]) + "-" + juce::String(k++), nullptr);
            vt.setProperty("enabled", true, nullptr);
            for (auto &p : m.props)
                vt.setProperty(juce::Identifier(p.id), p.kind == 0 ? juce::var(p.value) : p.kind == 2 ? juce::var(p.value > 0.5) : juce::var((int)p.value), nullptr);
            if (!m.remapCurve.empty()) vt.setProperty("remapCurve", juce::String(m.remapCurve), nullptr);
            vt.setProperty("moduleMix", 100.0, nullptr);
            vt.setProperty("rangeLow", 0.0, nullptr);
            vt.setProperty("rangeHigh", 96000.0, nullptr);
            chains[c].appendChild(vt, nullptr);
        }
    }
    scrr::dsp::SpectralProcessor proc;
    // the order of upstream's prepareToPlay()
    proc.prepare({s.sampleRate, 64u, 2u});
    for (int c = 0; c < 5; ++c) proc.updateModuleChain(c, chains[c]);
    proc.setFormulaPrograms({});
    proc.setMacroMappings({});
    proc.applyMacroValues(ModulationValues{});
    proc.updateGlobalParameters(host.apvts);
    proc.applyPendingReconfigure();
    // ... and of its processBlock(): 64-sample steps
    proc.setTempo(120);
    for (int pos = 0; pos < n; pos += 64)
    {
        const int count = std::min(64, n - pos);
        proc.applyMacroValues(ModulationValues{});
        proc.updateGlobalParameters(host.apvts);
        float *ch[2] = {L + pos, R + pos};
        juce::AudioBuffer<float> block(ch, 2, count);
        proc.process(block);
    }
}
} // namespace p2ref
