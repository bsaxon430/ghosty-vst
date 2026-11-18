// SPDX-License-Identifier: AGPL-3.0-or-later
// GHOST ATLAS - the plug-in: the engine, the library and preset list every instance shares, the controls the host can
// automate (master, four macros, and each layer's level: the layers come last, so more of them can be added without
// moving the others), and the project state.
#pragma once
#include "../engine/Engine.h"
#include "../core/Library.h"
#include "../core/Preset.h"

namespace atlas
{
// what every instance in this process shares: the library database and the preset list (loaded when first needed)
struct Shared
{
    Library& library();
    PresetBank& bank();
    juce::CriticalSection bankLock;     // the preset list is read and refreshed from more than one thread
private:
    Library lib;
    PresetBank presets;
    std::once_flag libOnce, bankOnce;
};

class Processor final : public juce::AudioProcessor,
                        public juce::ChangeBroadcaster,     // tells the screen that a different sound is loaded
                        private juce::Timer
{
public:
    enum HostParam { hpMaster, hpMacro1, hpMacro2, hpMacro3, hpMacro4, hpLevel1, kHostParams = hpLevel1 + kLayers };

    Processor();
    ~Processor() override;

    // ---- juce::AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override { eng.panic(); }
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Ghost Atlas"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- for the screen (message thread)
    Engine& engine() noexcept { return eng; }
    Library& library() { return shared->library(); }
    PresetBank& bank() { return shared->bank(); }
    juce::CriticalSection& bankLock() { return shared->bankLock; }

    void loadPreset (const Preset& p);                  // becomes the sound; the host's controls follow
    bool loadPresetFile (const juce::String& file);     // a path, or "factory:<name>"
    Preset capture() const;                             // the sound as it stands now
    juce::String presetFile() const { return currentFile; }
    juce::String presetName() const { return capture().name; }

    void setHost (int index, float value);              // in the control's own units; tells the host
    float getHost (int index) const;
    void beginGesture (int index);
    void endGesture (int index);

    bool noteHeld (int note) const noexcept { return (heldBits[(size_t) (note >> 5) & 3].load (std::memory_order_relaxed) >> (note & 31)) & 1u; }
    juce::var uiState;                                  // the window's size and page, kept with the project

private:
    void timerCallback() override;
    void followPreset (const Preset& p);

    juce::SharedResourcePointer<Shared> shared;
    Engine eng;
    std::vector<juce::AudioParameterFloat*> host;
    std::array<float, kHostParams> pushed;              // audio thread: what the engine was last told
    std::array<std::atomic<juce::uint32>, 4> heldBits;
    phantom::TempoInfo tempo;
    double sr = 48000.0;
    juce::String currentFile;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Processor)
};
} // namespace atlas
