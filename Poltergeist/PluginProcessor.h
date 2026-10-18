#pragma once
#include "Engine.h"
#include "Summon.h"
#include "MidiFx.h"

class PoltergeistProcessor : public juce::AudioProcessor {
public:
    PoltergeistProcessor();
    const juce::String getName() const override { return "Poltergeist"; }
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& l) const override { return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }
    int getNumPrograms() override { return (int) gs::factoryPresets().size(); }
    int getCurrentProgram() override { return currentPreset; }
    void setCurrentProgram(int i) override { loadPreset(i); }
    const juce::String getProgramName(int i) override { return juce::isPositiveAndBelow(i, getNumPrograms()) ? gs::factoryPresets()[(size_t) i].name : ""; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // patches
    void applyPatch(const gs::Patch& p);
    juce::String summon(const juce::String& words);   // returns what it made
    void loadPreset(int i);
    // Sample Ghost
    bool loadSampleFile(const juce::File& f, juce::String& err);
    bool resampleRecent(double seconds = 4.0);   // the last few seconds of Poltergeist's own output
    void clearSample() { engine.setSample(nullptr); }

    juce::AudioProcessorValueTreeState apvts;
    gs::Engine engine;
    juce::MidiKeyboardState keyboard;   // the on-screen keyboard plays here too
    int currentPreset = 0;
    juce::String patchName { "Init" };
    uint32_t summonSeed = 1;

private:
    gs::ParamReader reader;
    gs::Snap snap {};
    gs::MidiFx midiFx;
    double bpm = 120, ppq = 0; bool playing = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PoltergeistProcessor)
};
