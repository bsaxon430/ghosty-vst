#pragma once
#include "SpecEngine.h"
#include <juce_audio_utils/juce_audio_utils.h>

class SpecProcessor : public juce::AudioProcessor {
public:
    SpecProcessor();
    ~SpecProcessor() override;
    const juce::String getName() const override { return "Ghosty Spectrum"; }
    void prepareToPlay(double sr, int block) override { engine.prepare(sr, juce::jmax(64, block)); publish(); }
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& l) const override { return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }
    int getNumPrograms() override { return 1; } int getCurrentProgram() override { return 0; } void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; } void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    sp::Model model; std::shared_ptr<gb::DrumLibrary> drums;
    juce::AudioProcessorValueTreeState apvts; sp::SpecEngine engine;
    void publish();
    int addLayer(int kind, const juce::String& name, const juce::String& words, uint32_t seed, std::shared_ptr<gs::SampleData> sample = nullptr);
    int layerFor(int kind, const juce::String& name);                // existing layer with that sound, or a new one (-1 if full)
    void removeLayer(int i);
    void audition(int layer, int note = 60) { engine.auditionNote.store(note); engine.auditionLayer.store(layer); }
    void auditionDrum(const juce::String& name) { if (auto s = drums->byName(name)) engine.auditionDrum.store(s.get()); }
    juce::ValueTree toTree() const;
    void fromTree(const juce::ValueTree& t);
    juce::File userDir() const;
    bool saveCanvas(const juce::String& name);
    bool loadCanvas(const juce::File& f);
    juce::File bounce();
    juce::StringArray bounceStems();
    double bpm() const { return apvts.getRawParameterValue("bpm")->load(); }
    std::vector<std::shared_ptr<gs::SampleData>> userSamples;
private:
    juce::File renderTo(const sp::SpecSnapshot* s, const juce::File& f);
    std::vector<std::unique_ptr<sp::SpecSnapshot>> snaps;
    std::vector<std::shared_ptr<const gs::SampleData>> keep;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpecProcessor)
};
