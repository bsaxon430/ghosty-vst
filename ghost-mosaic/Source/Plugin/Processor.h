// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "Engine.h"
#include "Osc.h"
#include "TerrainInteraction.h"

#ifndef GHOSTMOSAIC_FX
 #define GHOSTMOSAIC_FX 0
#endif

namespace mosaic
{
struct Preset
{
    const char* name;
    const char* blurb;
    std::vector<std::pair<const char*, float>> values;
    const char* kit = nullptr;       // built-in sounds to load as the library (nullptr = keep yours)
    const char* target = nullptr;    // built-in target ("beat", "vowels", "piano")
};
const std::vector<Preset>& factoryPresets();

// UNDO / REDO for the settings. Every change is caught, from any knob, menu, button, preset,
// MUTATE or A / B: a burst of changes becomes one step once things are still for a moment
// (or as soon as something new begins). Your sounds are not part of it.
class SettingsHistory
{
public:
    explicit SettingsHistory (juce::AudioProcessorValueTreeState& p) : m_params (p) {}
    void beginNewTransaction (const juce::String& = {}) { commit(); }
    void poll();                     // the processor's timer
    bool canUndo() const;
    bool canRedo() const { return ! m_redo.empty(); }
    void undo();
    void redo();
    void reset() { m_undo.clear(); m_redo.clear(); m_base = m_last = capture(); }

private:
    std::vector<float> capture() const;
    void restore (const std::vector<float>& v);
    void commit();
    juce::AudioProcessorValueTreeState& m_params;
    std::vector<std::vector<float>> m_undo, m_redo;
    std::vector<float> m_base, m_last;
    double m_lastChange = 0;
};

class Processor : public juce::AudioProcessor, private juce::Timer, private juce::ValueTree::Listener
{
public:
    Processor();
    ~Processor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return ! GHOSTMOSAIC_FX; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& dest) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& params() { return m_params; }
    Engine& engine() { return *m_engine; }
    OscBridge& osc() { return *m_osc; }
    SettingsHistory& undo() { return *m_history; }
    bool isEffect() const { return GHOSTMOSAIC_FX != 0; }

    // GhostBlock buttons
    void mutate (juce::uint32 seed = 0);            // MUTATE: new search settings around the current ones
    void storeAB();                                 // remember the current settings in the active slot
    void switchAB();                                // A <-> B
    bool abIsB() const { return m_abB; }
    void applyPreset (int index);
    void generate();                                // GENERATE (the button): no target = the built-in one; plays when built
    juce::String stateLine() const;                 // the status line: "Building library... 40%", "Playing", "Silent: ..."
    static inline bool preloadNewInstances = true;  // a new instance opens ready to play (built-in sounds + target); tests switch it off
    void loadKit (const juce::String& which);
    void loadDemoTarget (const juce::String& which);
    void setParam (const char* id, float value, bool undoable = true);
    float getParam (const char* id) const;
    juce::File userFolder() const;                  // Documents/GhostBlock/Ghost Mosaic
    juce::File nextRecordingFile() const;

    std::atomic<int> uiShockwave { 0 };
    std::atomic<float> uiInputLevel { 0 };         // the effect: how loud the track coming in is (lights the wordmark)             // GENERATE / MUTATE bursts in the view

    // TERRAIN: the landscape (both A/B slots, its own undo). Starts flat (= the engine untouched);
    // every change goes straight to the engine, so the sound follows a drag.
    ghost::terrain::TerrainEditor terrain;
    void applyTerrainState (const juce::String& json);   // "" = flat (older sessions)
    juce::String terrainStateString() const;
    void setTerrainOn (bool on);                          // the page's ON switch: off = the engine untouched, the land kept
    bool terrainOn() const { return m_terrainOn; }
    void pushTerrain();

    static void registerInstance (Processor*, bool add);
    static Processor* newestInstance();

private:
    static BusesProperties buses();
    void timerCallback() override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override {}

    juce::AudioProcessorValueTreeState m_params;
    std::unique_ptr<SettingsHistory> m_history;
    std::unique_ptr<Engine> m_engine;
    std::unique_ptr<OscBridge> m_osc;
    juce::ValueTree m_slotA, m_slotB;
    bool m_abB = false;
    bool m_terrainOn = true;
    int m_reportedLatency = -1;
    mutable int m_recordId = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Processor)
};
}
