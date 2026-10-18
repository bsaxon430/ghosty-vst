#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "Waves.h"
#include "DrumSynth.h"
#include "Fx.h"
#include "Performer.h"

// Ghosty Spectrum: tones and drums placed at their true frequency on the 20 Hz - 20 kHz spectrum.
class SpectrumProcessor : public juce::AudioProcessor
{
public:
    static constexpr int MAX_EL = 24;
    static constexpr int NUM_TIMBRES = 16;
    static constexpr int RING = 8192;

    struct Element
    {
        std::atomic<bool> active { false };
        std::atomic<int> kind { 0 };          // 0 tone, 1 drum
        std::atomic<float> freq { 440.0f }, pan { 0.0f }, vol { 1.0f };
        std::atomic<int> timbre { 0 };        // wave shape (tone) or drum index (drum)
        std::atomic<bool> drone { true };
        std::atomic<int> steps { 0 };         // 16-bit step mask
        std::atomic<int> hits { 0 };
        std::atomic<float> level { 0.0f };    // for the UI glow
        int look = 0;
        // audio thread only
        double phase = 0; float env = 0, gainNow = 0, freqNow = 440;
    };
    std::array<Element, MAX_EL> els;

    SpectrumProcessor();
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Ghosty Spectrum"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentScene; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- used by the editor (message thread)
    int addTone (float freq, float pan, int timbre, bool drone, int steps = 0x1111, float vol = 1.0f);
    int addDrum (int drum, float freq, float pan, int steps = -1, float vol = 1.0f);
    void removeElement (int i) { if (juce::isPositiveAndBelow (i, MAX_EL)) els[(size_t) i].active.store (false); }
    void clearElements() { for (auto& e : els) e.active.store (false); }
    void liveOn (float freq, float pan) { liveFreq.store (freq); livePan.store (pan); liveGate.store (true); liveTrig.fetch_add (1); }
    void liveMove (float freq, float pan) { liveFreq.store (freq); livePan.store (pan); }
    void liveOff() { liveGate.store (false); }

    float ref() const;
    float fOf (float midi) const { return ref() * std::pow (2.0f, (midi - 69.0f) / 12.0f); }
    float midiOf (float f) const { return 69.0f + 12.0f * std::log2 (f / ref()); }
    float snap (float f) const;
    bool isTonalDrum (int d) const { return d == 2 || d == 7 || d == 9 || d == 12; }
    static float drumCenter (int d);
    static juce::Point<float> drumBand (int d);
    static const juce::StringArray& timbreNames() { return ghosty::shapeNames(); }

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<bool> internalPlay { false };
    std::atomic<bool> hostPlaying { false };
    std::atomic<int> playStep { -1 };
    std::atomic<float> level { 0.0f };
    std::array<float, RING> ring {};
    std::atomic<int> ringPos { 0 };
    std::atomic<int> sceneVersion { 0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    void buildScene (int index);
    void triggerStep (int s, int offset);

    std::array<ghosty::MipSet, NUM_TIMBRES> mips;
    ghosty::DrumBank bank;
    ghosty::FxChain fx;
    juce::dsp::StateVariableTPTFilter<float> lpF, hpF;
    double sr = 44100, pos16 = 0, mipRate = 0;
    long long nextStep = 0;
    bool wasPlaying = false;
    int currentScene = 0;

    struct DrumVoice { const std::vector<float>* data = nullptr; double pos = 0, rate = 1; float gain = 0, pl = .7f, pr = .7f; int delay = 0; bool active = false; };
    std::array<DrumVoice, 32> drums;
    struct LiveVoice { bool active = false, held = false; int note = -1; double phase = 0; float freq = 440, freqNow = 440, env = 0, pan = 0; int stage = 0; };
    std::array<LiveVoice, 9> live;     // slot 0 = on-screen PLAY, others = MIDI
    std::atomic<float> liveFreq { 440.0f }, livePan { 0.0f };
    std::atomic<bool> liveGate { false };
    std::atomic<int> liveTrig { 0 };
    int liveTrigSeen = 0;
    juce::Random rng;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumProcessor)
};
