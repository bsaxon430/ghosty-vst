/*
 * Ghost Bass b11 - LAYERS: the main engine (Surge XT) + SUB (Bombaz) side by side (b11: the 808 layer was removed).
 * Copyright 2026 the Ghost Bass authors. GPL-3.
 *
 * The host only does four things: it hands the notes Surge plays to the SUB engine, runs the upstream
 * engine, and mixes (on/off, level, pan). GHOST LOCK (on by default, but only while SUB is on) keeps the two from clashing.
 * With SUB off and SURGE at unity the output buffer is not touched at all (bit-identical to build 9).
 */
#ifndef GHOSTBASS_SRC_GHOST_LAYERS_GHOSTLAYERS_H
#define GHOSTBASS_SRC_GHOST_LAYERS_GHOSTLAYERS_H

#include "LayerEngines.h"
#include "GhostMixer.h"
#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ghost
{
class GhostEngine;
}

namespace ghost::layers
{
class Host
{
  public:
    explicit Host(ghost::GhostEngine &g);
    ~Host();
    struct BlendDsp;

    // mix controls (any thread). level is a linear gain 0..1.5 (1 = as the engine renders), pan -1..1, octave (SUB's own control)
    struct Mix
    {
        std::atomic<bool> on{false};
        std::atomic<float> level{1.f};
        std::atomic<float> pan{0.f};
        std::atomic<int> octave{0};
        // GHOST LOCK (mix stage, JUCE dsp Linkwitz-Riley crossovers; all off by default): MONO sums the layer to mono (for SUB: centres it),
        // PHASE flips its polarity, LOW CUT / HIGH CUT (Hz, 0 = off) keep only the part above / below that frequency
        std::atomic<bool> mono{false}, phase{false};
        std::atomic<float> lowCut{0.f}, highCut{0.f};
    };
    // master GHOST LOCK (b11: ON by default, and only ever acts while the SUB layer is on, so the engine alone stays untouched):
    // STACK MODE gives everything below SPLIT to SUB and everything above to the engine (crossovers that sum flat);
    // MONO BASS sums everything below MONO BELOW to mono; AUTO GAIN trims the layers when more of them are on
    struct Blend
    {
        std::atomic<bool> stack{true};
        std::atomic<float> split{110.f};
        std::atomic<bool> monoBass{true};
        std::atomic<float> monoBelow{120.f};
        std::atomic<bool> autoGain{true};
        std::atomic<bool> meterOn{false};      // set by the BLEND page while it is open
        std::atomic<float> meterDb[2]{{-120.f}, {-120.f}}; // low-band level of SURGE, SUB (dB)
        std::atomic<float> meterCorr{0.f};                // low-band correlation: SURGE-SUB
    };
    Blend blend;
    // one-tap stack presets: the list of (control key, value) they set after resetting the blend controls
    struct StackPreset
    {
        const char *name;
        std::vector<std::pair<std::string, float>> set;
    };
    static const std::vector<StackPreset> &stackPresets();
    // the clash meter's advice for the current low-band readings ("" = nothing to say); level 0 clean, 1 watch, 2 clash
    std::string clashHint(int &level) const;
    Mix surge, sub; // surge starts ON (the engine), sub OFF
    SubEngine subEngine;
    Mixer mixer; // the BLEND BOARD: up to four sounds, each in its own engine, mixed by the puck

    void prepare(double sampleRate);
    // audio thread: after Surge rendered a block, `L`/`R` (BLOCK_SIZE samples) get the layers mixed in
    void block(float *L, float *R, int n);
    void touch() { layerWantedSync(); }
    // host automation (build 10): every mix control and every control of both engines is a host parameter ("layer_sub_width",
    // "layer_sub_width", "layer_sub_level"...). The processor adds these to its parameter tree; the UI binds to the same objects
    // (so a drag is a host gesture and is recorded). They are views: the value lives in the engine.
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> makeHostParams();
    juce::RangedAudioParameter *hostParam(const juce::String &id) const;
    // the parameter the UI should bind to: the host parameter if there is one, else the engine's own
    juce::RangedAudioParameter *uiParam(const juce::String &engineId);
    juce::RangedAudioParameter *mixParam(const juce::String &key) const { return hostParam("layer_" + key); }
    // TERRAIN / MOVEMENT coverage of the layers' own controls: name, moved (true) or why not
    struct Cover
    {
        std::string name, label, why; // why empty = moved
    };
    std::vector<Cover> terrainCoverage() const;
    bool anyExtraOn() const { return sub.on.load(); }
    // tests only: run the mix stage on a given SUB signal (mono input) with every gain at unity and both layers on
    void blendTest(float *L, float *R, const float *subIn, int n);
    bool isDefault() const; // everything as a fresh instance (nothing is saved then)

    std::string saveText();
    void loadText(const std::string &);
    void resetAll();

  private:
    struct Ramp
    {
        float g{0.f};
        bool sounding{false};
    };
    void layerWantedSync();
    void registerTerrain();
    std::unordered_map<std::string, juce::RangedAudioParameter *> hostParams;
    std::vector<Cover> cover;
    ghost::GhostEngine &gb;
    double sr{44100};
    float gSurge{1.f}, gSub{0.f};
    bool subWasOn{false};
    bool prepared{false};
    bool surgeSetUp{true};
    std::array<float, 64> bufSub{};
    std::unique_ptr<BlendDsp> dsp;
    bool blendRequested() const;
    bool blendIsDefault() const;
    void resetBlend();
    void blendBlock(float *L, float *R, int n, float tSurge, float tSub, bool sOn);
};
} // namespace ghost::layers

#endif
