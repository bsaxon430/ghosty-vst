#pragma once
// Block Step: the vertical drum tracker (the web app) as a VST3 / AU instrument.
// The page (data/index.html, the same Block Step UI) runs in a WebBrowserComponent; this processor plays its pattern
// through the DAW with BlockStepEngine, follows the host's tempo + transport, and saves the page state in the project.
#include <juce_audio_processors/juce_audio_processors.h>
#include "BlockStepEngine.h"
#include "License.h"
#include "Bypass.h"

// One copy of the kit index, decoder thread and synth drums for every Block Step instance in the session.
struct BlockStepKit {
    bstep::SampleStore store; bstep::SynthKit synth;
    BlockStepKit();
    static const void* resource(const juce::String& originalFilename, int& size);
};

class BlockStepProcessor : public juce::AudioProcessor, private juce::Timer {
public:
    BlockStepProcessor();
    ~BlockStepProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }   // the host's own bypass drives the same switch
    const juce::String getName() const override { return "Block Step"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 1.5; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // message thread: the page's full state (pattern, sounds, settings) as JSON
    void setUiState(const juce::String& json);
    juce::String getUiState() const { const juce::ScopedLock sl(stateLock); return uiState; }
    std::shared_ptr<const bstep::Pattern> getUiPattern() const { const juce::ScopedLock sl(stateLock); return uiPattern; }
    bstep::Engine& getEngine() { return engine; }
    bstep::SampleStore& kitStore() { return kit->store; }
    const bstep::SynthKit& kitSynth() const { return kit->synth; }

    std::atomic<bool> pianoPage { false }; std::atomic<int> pageKind { 0 };   // 0 drums, 1 piano, 2 synth
    std::atomic<double> dawBpm { 0.0 };   // the DAW's tempo, updated every block even when stopped   // the page shows the piano page (for drag-out names)
    int uiW = 0, uiH = 0;   // this instance's window size (saved in the project; a new instance opens at the fitting default)
    static constexpr int kSizeMark = 0x47485A32;   // "GHZ2"
    juce::AudioParameterBool* bypassParam = nullptr;   // BYPASS (Shared/Bypass.h): mutes the whole plugin, never saved in the page state
    ghosty::License license { "Block Step" };

private:
    void timerCallback() override { engine.freeRetired(); if (kit->store.userLibraryChanged()) kit->store.loadUserLibrary(); }   // MY BANKS changed in another plugin
    juce::SharedResourcePointer<BlockStepKit> kit;
    bstep::Engine engine;
    ghosty::DemoGate demo;
    ghosty::BypassFader bypass;
    juce::CriticalSection stateLock; juce::String uiState; std::shared_ptr<const bstep::Pattern> uiPattern;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BlockStepProcessor)
};
