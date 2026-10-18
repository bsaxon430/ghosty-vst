#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "DrumSynth.h"
#include "Fx.h"

class BeatsProcessor : public juce::AudioProcessor
{
public:
    static constexpr int TRACKS = 8, STEPS = 16;

    BeatsProcessor();
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Ghosty Beats"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<uint8_t> steps[TRACKS][STEPS];
    std::atomic<int> playStep { -1 };
    std::atomic<int> hitCount[TRACKS];
    std::atomic<float> level { 0 };
    std::atomic<bool> internalPlay { false };
    std::atomic<bool> tapeHeld { false };
    std::atomic<int> stutterLen { 0 };     // in 32nd notes: 8, 4, 2, 1 (0 = off)
    std::atomic<int> patternVersion { 0 };

    void previewTrack (int t) { previewQueue.store (t); }
    void toggleStep (int t, int s) { steps[t][s].store (steps[t][s].load() ? 0 : 1); }
    void setStep (int t, int s, bool on) { steps[t][s].store (on ? 1 : 0); }
    void randomise();
    void clearAll() { for (auto& tr : steps) for (auto& s : tr) s.store (0); }
    bool hostIsPlaying() const { return hostPlaying.load(); }
    static juce::String trackId (const char* what, int t) { return juce::String (what) + juce::String (t + 1); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    void triggerStep (int s, int sampleOffset);
    void startVoice (int track, int sampleOffset, float vel, bool forceReverse = false);

    struct Voice { const std::vector<float>* data = nullptr; double pos = 0, rate = 1; float gain = 0, panL = 0.7f, panR = 0.7f; int track = -1; int delay = 0; bool active = false; };
    std::array<Voice, 48> voices;

    ghosty::DrumBank bank;
    ghosty::FxChain fx;
    double sr = 44100;
    double pos16 = 0;            // position in 16th notes
    long long nextStep = 0;
    long long stutterStart = 0, stutterBase = 0; int lastStutter = 0; double stutterPos = 0;
    double tapeRate = 1.0;
    std::vector<float> tapeBuf;
    std::atomic<bool> hostPlaying { false };
    std::atomic<int> previewQueue { -1 };
    bool wasPlaying = false;
    int currentProgram = 0;
    juce::Random rng;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BeatsProcessor)
};
