#pragma once
// Ghosty Pro's processor: Vital's own SynthPlugin (engine, parameters, MIDI, state, preset format), unchanged, with
// Ghosty Pro's features around it (KEYS only rewrites the MIDI notes Vital gets). Everything that makes sound is Vital's. Our extras (MORPH board, GHOST GATE,
// HAUNT) are OFF by default; while they are off, processBlock is exactly SynthPlugin::processBlock.
#include "JuceHeader.h"
#include "synth_plugin.h"
#include "GhostyMorph.h"
#include "GhostyExtras.h"
#include "GhostyKeys.h"
#include <atomic>

class GhostyProcessor : public SynthPlugin {
  public:
    GhostyProcessor();
    ~GhostyProcessor() override;

    void prepareToPlay(double sample_rate, int buffer_size) override;
    // A synth: no input, one stereo output, switched ON. (JUCE's default bus is OFF; Vital's own Projucer build forces {0, 2} through
    // JucePlugin_PreferredChannelConfigurations, which the CMake build does not define: FL then ran the synth with 0 output channels.)
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(AudioSampleBuffer& buffer, MidiBuffer& midi) override;
    AudioProcessorEditor* createEditor() override;
    const String getName() const override { return "Ghosty Pro 2"; }
    void getStateInformation(MemoryBlock& dest) override;
    void setStateInformation(const void* data, int size) override;

    // presets (message thread). Every path ends in Vital's own SynthBase::loadFromFile / LoadSave::jsonToState.
    bool loadPresetFile(const File& file, String& error);                  // any .vital (ours or the user's own)
    bool loadPresetText(const String& text, const String& name, String& error);
    bool loadFactoryPreset(int index, String& error);                     // our presets (BinaryData)
    void loadInit();
    bool savePresetFile(const File& file);                                // Vital's own saveToFile
    String presetText();                                                  // the current sound as .vital json
    String currentPresetName() { return getPresetName(); }
    int factoryIndex() const { return factory_index_; }

    // controls (message thread): through Vital's valueChangedInternal, so the host and Vital's own editor follow
    void setControl(const std::string& name, float value);
    float getControl(const std::string& name);
    bool hasControl(const std::string& name) { return controls_.count(name) > 0; }

    // a counter the editor watches: +1 after every preset load / state restore
    std::atomic<int> sound_epoch { 0 };

    // TEMPO: 1/2x | 1x | 2x. Everything tempo-synced (KEYS, MORPH motion, GATE / HAUNT, and the engine's own synced LFOs and
    // delays through its normal host-tempo input) runs at the host tempo times this. Saved with the project; a new one is 1x.
    // At 1x nothing changes (the engine sees the host's own transport, bit for bit).
    void setTempoMultiple(float m) { tempo_mult_ = m < 0.75f ? 0.5f : m > 1.5f ? 2.0f : 1.0f; }
    float tempoMultiple() const { return tempo_mult_.load(); }
    double hostBpm() const { return host_bpm_.load(); }   // the host's tempo (0 = not known yet)

    ghosty::Morph& morph() { return morph_; }
    ghosty::Extras& extras() { return extras_; }
    ghosty::Keys& keys() { return keys_; }

    // the keyboard on our SYNTH page plays through Vital's own keyboard state
    MidiKeyboardState* keyboardState() { return getKeyboardState(); }

    // tests: the engine output with nothing of ours in the chain
    void processBlockVitalOnly(AudioSampleBuffer& buffer, MidiBuffer& midi) { SynthPlugin::processBlock(buffer, midi); }

  private:
    void soundChanged();

    ghosty::Morph morph_;
    ghosty::Extras extras_;
    ghosty::Keys keys_;            // ARP / CHORDS / SCALE LOCK / HOLD: MIDI in front of Vital (off by default)
    MidiBuffer keys_out_;
    int factory_index_ = -1;
    double free_seconds_ = 0.0;
    std::atomic<float> tempo_mult_ { 1.0f };
    std::atomic<double> host_bpm_ { 0.0 };   // MORPH's free-running clock (per instance)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GhostyProcessor)
};
