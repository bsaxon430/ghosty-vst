// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX host shell: the parameter tree, state (presets, scenes, snapshots, A/B, undo), latency reporting, the rack.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../rack/RackEngine.h"
#include "../spatial/SpatialAutomation.h"
#include <atomic>

namespace phantom
{
struct Meter { std::atomic<float> peakL { 0 }, peakR { 0 }, truePeakL { 0 }, truePeakR { 0 }; };
constexpr int kScenes = 8;
inline const char* sceneDefaultName (int i) { static const char* n[kScenes] { "CLEAN", "FAT", "DREAM", "BROKEN", "SPACE", "FREEZE", "CHAOS", "DROP" }; return n[juce::jlimit (0, kScenes - 1, i)]; }
inline const char* macroDefaultName (int i) { static const char* n[kNumMacros] { "DARKNESS", "CHAOS", "SIZE", "IMPACT", "MOVEMENT", "WIDTH", "DIRT", "AIR" }; return n[juce::jlimit (0, kNumMacros - 1, i)]; }

class PhantomProcessor : public juce::AudioProcessor, private juce::AudioProcessorValueTreeState::Listener, private juce::AsyncUpdater, private juce::Timer
{
public:
    PhantomProcessor();
    ~PhantomProcessor() override;

    static juce::String versionString();
    static constexpr int kDefaultWidth = 1200, kDefaultHeight = 760;   // house rule: opens at about 1200 x 760, never above 1240 x 780

    // ---- AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void reset() override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Phantom FX"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 12.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Init"; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- the rack
    RackEngine rack;                                    // declared before apvts: createLayout() reads its slots while apvts is built
    juce::UndoManager undo;
    juce::AudioProcessorValueTreeState apvts;
    static juce::String pid (const char* key, const std::string& param) { return juce::String ("fx_") + key + "_" + param; }
    static juce::String pid (int unit, const std::string& param) { return pid (unitDef (unit).key, param); }
    int totalLatency() const noexcept { return reportedLatency.load(); }
    Meter inMeter, outMeter;
    TempoInfo lastTempo() const noexcept { return tempo; }

    // topology (message thread)
    Topology getTopology() const;
    void setTopology (const Topology& t, bool undoable = true);
    void moveUnit (int unit, int toIndex);             // drag in the strip
    void setUnitLane (int unit, int lane);

    // modulation routes (message thread)
    std::vector<ModRoute> getRoutes() const;
    void setRoutes (const std::vector<ModRoute>& r);
    void addRoute (const ModRoute& r) { auto v = getRoutes(); for (auto& x : v) if (x.source == r.source && x.unit == r.unit && x.param == r.param) { x = r; setRoutes (v); return; } v.push_back (r); setRoutes (v); }
    void removeRoute (int index) { auto v = getRoutes(); if (index >= 0 && index < (int) v.size()) { v.erase (v.begin() + index); setRoutes (v); } }
    // drawn data of the Rhythm units (patterns, painted grids, pads, bend lanes): message thread; saved in the project, not undoable
    juce::var getUnitData (int unit) const;
    void setUnitData (int unit, const juce::var& v);
    void setStepValue (int seq, int step, float v);
    float getStepValue (int seq, int step) const;

    // scenes (8 per preset), A/B, snapshots, presets (all message thread)
    bool sceneFilled (int i) const;
    juce::String sceneName (int i) const;
    void storeScene (int i);
    juce::String drawnSnapshot() const; void applyDrawnSnapshot (const juce::String& jsonText);   // every unit's drawn patterns as one JSON text (scenes, A/B, presets)
    std::atomic<int> drawnGeneration { 0 };   // counts up whenever drawn data was replaced from outside its editor
    void clearScene (int i);
    void renameScene (int i, const juce::String& name);
    void recallScene (int i);                           // honours the scene time (0 = cut) and quantise parameters
    int activeScene() const noexcept { return activeSceneIdx.load(); }
    float sceneMorphProgress() const noexcept { return morphProgress.load(); }
    int abCurrent() const noexcept { return abIdx; }
    void abToggle();
    void abCopy();                                      // copies what you hear into the other slot
    void unitAbToggle (int unit); void unitAbCopy (int unit); int unitAbCurrent (int unit) const { return unitAb[(size_t) unit]; }
    juce::var toJson (const juce::String& name, const juce::StringArray& tags) const;   // a whole-rack preset
    bool fromJson (const juce::var& v);                 // backwards-compatible: unknown keys ignored, missing values stay at their defaults
    bool savePreset (const juce::File& f, const juce::String& name, const juce::StringArray& tags) const;
    bool loadPreset (const juce::File& f);
    // the factory bank (built in; FactoryPresets.h): a preset starts from INIT, then sets what it names. false = a name in it is unknown
    static int numFactoryPresets(); static juce::String factoryPresetName (int i); static juce::StringArray factoryPresetTags (int i);
    bool loadFactoryPreset (int i, juce::String* problem = nullptr);
    static juce::File presetFolder();                   // ~/Documents/GhostBlock/Phantom FX/Presets
    static juce::File snapshotFolder();
    juce::var unitToJson (int unit) const; bool unitFromJson (int unit, const juce::var& v);   // per-effect presets
    void saveSnapshot (const juce::String& name);
    juce::Array<juce::File> listSnapshots() const;
    void initAll();                                     // factory defaults (every parameter, topology, routes, scenes untouched)
    void setParamNormalised (const juce::String& id, float v01);

    // ---- SPATIAL (Phase 6b): tiles' host parameters are spid (u, "azimuth" / "elevation" / "distance"); the switch is "fx_spatial_on"
    static juce::String spid (int unit, const char* what) { return juce::String ("fx_spatial_") + unitDef (unit).key + "_" + what; }
    SpatialAutomation spatialAuto;
    struct SpatialView { std::atomic<float> az { 0 }, el { 0 }, dist { 0 }; std::atomic<bool> driven { false }, recording { false }; };
    std::array<SpatialView, kNumUnits> spatialView;      // where each tile really is this block (its path or its parameters), for the UI
    std::atomic<double> spatialPpq { 0.0 }; std::atomic<bool> spatialPlaying { false };
    void drainSpatial() { spatialAuto.drain(); }          // the 60 Hz timer does this; tests call it directly
    float getParamNormalised (const juce::String& id) const;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void parameterChanged (const juce::String& id, float value) override;
    void handleAsyncUpdate() override;
    void timerCallback() override;                      // scene morph + quantised triggers (60 Hz, message thread)
    void pushHostValues() noexcept;
    void updateSpatial() noexcept;
    std::atomic<float>* pSpOn = nullptr; std::atomic<float>* pSpOut = nullptr;
    std::array<std::atomic<float>*, kNumUnits> pSpAz {}, pSpEl {}, pSpDist {};
    std::array<bool, kNumUnits> spRecording {}; bool spatialSwitchPending = false;
    void refreshLatency();
    void readTopologyFromState();
    void readRoutesFromState();
    void readStepsFromState();
    juce::ValueTree child (const juce::Identifier& id) const;
    void readDataFromState();
    std::array<NoteEvent, 256> noteEvents {};
    juce::ValueTree captureState() const;               // every rack parameter + topology (for scenes / A/B)
    void applyCapturedState (const juce::ValueTree& t, bool topologyToo);
    void beginMorph (const juce::ValueTree& target, double seconds);

    struct Binding { std::vector<std::atomic<float>*> values; std::atomic<float>* on = nullptr; std::atomic<float>* mix = nullptr; std::atomic<float>* trim = nullptr; };
    std::vector<Binding> bindings;
    std::atomic<float>* pIn = nullptr; std::atomic<float>* pOut = nullptr; std::atomic<float>* pMix = nullptr; std::atomic<float>* pBypass = nullptr; std::atomic<float>* pOs = nullptr;
    std::atomic<float>* pScene = nullptr; std::atomic<float>* pSceneTime = nullptr; std::atomic<float>* pSceneQuant = nullptr;
    std::array<std::atomic<float>*, kNumMacros> pMacro {};
    std::array<std::atomic<float>*, kLanes> pLaneGain {}, pLanePan {}, pLaneMute {};
    std::array<std::atomic<float>*, kNumLfos> pLfoShape {}, pLfoSync {}, pLfoDiv {}, pLfoRate {};
    std::array<std::atomic<float>*, kNumSteps> pStepCount {}, pStepLen {}, pStepGlide {};
    std::array<std::atomic<float>*, kNumEnvs> pEnvAtk {}, pEnvRel {};
    ParamSmoother inSm, outSm, mixSm, bypassSm;
    IntegerDelay globalDry; juce::AudioBuffer<float> dryBuf;
    std::unique_ptr<juce::dsp::Oversampling<float>> tpOs; juce::AudioBuffer<float> tpBuf;   // 4x inter-sample peak metering
    std::atomic<int> reportedLatency { 0 };
    std::atomic<bool> topologyDirty { false };
    double sr = 48000.0; int maxBlock = 0;
    TempoInfo tempo;

    // scenes / morph
    std::atomic<int> activeSceneIdx { -1 }, midiSceneRequest { -1 }, quantisedFire { -1 };
    std::atomic<double> quantiseTargetPpq { -1.0 }; int quantisePending = -1;
    std::atomic<float> morphProgress { 1.0f };
    struct MorphTarget { juce::String id; float from = 0, to = 0; bool stepped = false; };
    std::vector<MorphTarget> morph; Topology morphTopology; bool morphHasTopology = false, morphTopologyDone = false; double morphSeconds = 0, morphStart = 0;
    int lastSceneParam = 0;
    // A/B
    int abIdx = 0; juce::ValueTree abOther; std::array<int, kNumUnits> unitAb {}; std::array<juce::ValueTree, kNumUnits> unitAbOther;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhantomProcessor)
};
} // namespace phantom
