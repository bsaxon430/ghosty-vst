#pragma once
#include "Engine.h"
#include "Summon.h"
#include "MidiFx.h"

class PoltergeistProcessor : public juce::AudioProcessor {
public:
    PoltergeistProcessor();
    ~PoltergeistProcessor() override;
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
    // user sounds: the whole plugin state (patch + Sample Ghost + Lab + keys) in Documents/Ghosty/Poltergeist
    juce::File userDir() const { auto d = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Ghosty").getChildFile("Poltergeist"); d.createDirectory(); return d; }
    bool saveUserSound(const juce::String& name) { juce::MemoryBlock mb; getStateInformation(mb); patchName = name; return userDir().getChildFile(juce::File::createLegalFileName(name) + ".ghostpatch").replaceWithData(mb.getData(), mb.getSize()); }
    bool loadUserSound(const juce::File& f) { juce::MemoryBlock mb; if (!f.loadFileAsData(mb) || mb.getSize() == 0) return false; setStateInformation(mb.getData(), (int) mb.getSize()); patchName = f.getFileNameWithoutExtension(); return true; }
    // Sample Ghost
    bool loadSampleFile(const juce::File& f, juce::String& err);
    bool resampleRecent(double seconds = 4.0);   // the last few seconds of Poltergeist's own output
    void clearSample() { engine.setSample(nullptr); }
    void setSampleGhost(std::shared_ptr<gs::SampleData> s);   // also starts the keep-tempo copies in the background

    // ---- Sample Lab (message thread)
    gs::Lab lab;
    juce::String keyNames[128];
    int labAddFile(const juce::File& f, juce::String& err);
    void labMapKey(int note, std::shared_ptr<gs::SampleData> s);
    void labClearKey(int note);
    void labClearKeys();
    void labRenderPreview();                 // bounce the timeline (background) -> preview + drag-out
    std::shared_ptr<gs::SampleData> labRendered;
    std::atomic<bool> previewOn { false };
    juce::File writeTempWav(const gs::SampleData& s, const juce::String& name);
    std::function<void()> onLabRendered;     // UI refresh hook (called on the message thread)
    double previewPosUi() const { return previewPos; }

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
    std::vector<std::shared_ptr<gs::SampleData>> keepAlive;      // everything ever given to the audio thread
    std::vector<std::unique_ptr<gs::KeyMap>> keymaps;             // old maps stay alive for the session
    std::shared_ptr<gs::SampleData> keySamples[128];
    void publishKeys();
    std::atomic<const gs::SampleData*> previewPtr { nullptr }; double previewPos = 0;
    std::atomic<int> pitchGen { 0 }, renderGen { 0 };
    std::shared_ptr<std::atomic<bool>> pitchCancel;
    std::shared_ptr<std::atomic<bool>> aliveFlag = std::make_shared<std::atomic<bool>>(true);
    double bpm = 120, ppq = 0; bool playing = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PoltergeistProcessor)
};
