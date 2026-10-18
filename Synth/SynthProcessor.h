#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "Waves.h"
#include "Fx.h"
#include "Performer.h"

class SynthProcessor;

struct GhostSound : public juce::SynthesiserSound
{
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int) override { return true; }
};

class GhostVoice : public juce::SynthesiserVoice
{
public:
    explicit GhostVoice (SynthProcessor& p) : proc (p) {}
    void prepare (double sr, int block);
    bool canPlaySound (juce::SynthesiserSound* s) override { return dynamic_cast<GhostSound*> (s) != nullptr; }
    void startNote (int midi, float vel, juce::SynthesiserSound*, int wheel) override;
    void stopNote (float, bool allowTail) override;
    void pitchWheelMoved (int v) override { bend = std::pow (2.0, ((v - 8192) / 8192.0) * 2.0 / 12.0); }
    void controllerMoved (int, int) override {}
    void renderNextBlock (juce::AudioBuffer<float>&, int start, int num) override;

private:
    SynthProcessor& proc;
    juce::ADSR amp, fenv;
    juce::dsp::StateVariableTPTFilter<float> filter;
    std::array<double, 7> phase {};
    double subPhase = 0, freq = 440, bend = 1;
    float velocity = 1;
    double sr = 44100;
};

class SynthProcessor : public juce::AudioProcessor,
                       private juce::AudioProcessorValueTreeState::Listener,
                       private juce::AsyncUpdater
{
public:
    SynthProcessor();
    ~SynthProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Ghosty"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}
    static juce::String getPresetCategory (int index);

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // waves
    ghosty::Frame frameA, frameB;
    ghosty::WaveBank bank;
    void rebuildWaves() { bank.rebuild (frameA, frameB, sampleRateNow); waveVersion++; }
    std::atomic<int> waveVersion { 0 };

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> level { 0.0f };

    // fast parameter access for the voices
    std::atomic<float>* morph, *unison, *width, *sub, *attack, *decay, *sustain, *release, *cutoff, *res, *fenvAmt, *gain;
    std::atomic<float>* drive, *crush, *chorus, *phaser, *echo, *verb, *haunt;
    std::atomic<float>* keyP, *scaleP, *chordP, *arpP, *arpRateP, *arpModeP, *arpOctP, *arpGateP;

    juce::MidiKeyboardState keyboardState;     // the on-screen flow strip plays through this
    ghosty::Performer performer;

    static float cutoffHz (float v) { return 50.0f * std::pow (2.0f, v * 8.5f); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    void parameterChanged (const juce::String& id, float) override;
    void handleAsyncUpdate() override;

    juce::Synthesiser synth;
    ghosty::FxChain fx;
    juce::MidiBuffer performed;
    double sampleRateNow = 44100.0;
    int currentProgram = 0;
    std::atomic<bool> needA { false }, needB { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthProcessor)
};
