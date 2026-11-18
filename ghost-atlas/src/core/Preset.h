// SPDX-License-Identifier: AGPL-3.0-or-later
// GHOST ATLAS - a preset: the finished, layered sound. It names up to eight instruments from the library and holds
// everything about how they are played together: each layer's controls, its effects, the three shared effect sends,
// the master chain, the macros, and where the sound lives in the atlas (world, category, tags, author).
// An instrument (core/Library.h) is raw material; a preset is what is made from it.
#pragma once
#include "Common.h"
#include "../engine/Params.h"

namespace atlas
{
struct FxState
{
    juce::String unit;                       // Phantom FX's unit key ("halls", "chorus", "echo" ...), "shift" for VOICE SHIFT, "" = empty
    bool on = false;
    float mix = 1.0f;
    std::map<juce::String, float> values;    // by the unit's own control ids; missing = the unit's default
    juce::var data;                          // drawn patterns and the like, as the unit saves them

    juce::var toVar() const;
    static FxState fromVar (const juce::var&);
    bool operator== (const FxState& o) const { return unit == o.unit && on == o.on && mix == o.mix && values == o.values; }
};

struct LayerState
{
    juce::String instrument;                 // library id; "" = empty layer
    juce::String label;                      // shown name (the instrument's, unless renamed)
    juce::String role;                       // primary | support | transient | atmosphere | shimmer | "" (MUTATE's reading)
    std::array<float, kNumLayerParams> p;
    // settings that need the instrument to be loaded again when they change
    float startRange = 0.0f;                 // seconds of sample start the START control can reach (0 = off)
    int loopMode = 0;                        // 0 as made, 1 forced loop, 2 one shot
    float loopStart = 0.35f, loopEnd = 0.94f, loopFade = 0.2f;
    bool reverse = false;
    int voices = 32;
    // layer crossfades (0 = a hard edge): how many keys the layer takes to fade in from its LOW KEY and to fade out
    // towards its HIGH KEY, and the same in velocity steps at the two ends of its velocity range
    float keyFadeIn = 0.0f, keyFadeOut = 0.0f, velFadeIn = 0.0f, velFadeOut = 0.0f;
    std::array<FxState, kInserts> inserts;

    LayerState() { for (int i = 0; i < kNumLayerParams; ++i) p[(size_t) i] = layerParam (i).def; p[lpOn] = 0.0f; }
    bool used() const { return instrument.isNotEmpty() && p[lpOn] > 0.5f; }
    juce::var toVar() const;
    static LayerState fromVar (const juce::var&);
    // what would need the instrument reloaded
    bool sameSource (const LayerState& o) const
    {
        return instrument == o.instrument && startRange == o.startRange && loopMode == o.loopMode && loopStart == o.loopStart
            && loopEnd == o.loopEnd && loopFade == o.loopFade && reverse == o.reverse && voices == o.voices
            && keyFadeIn == o.keyFadeIn && keyFadeOut == o.keyFadeOut && velFadeIn == o.velFadeIn && velFadeOut == o.velFadeOut
            && inserts[0].unit == o.inserts[0].unit && inserts[1].unit == o.inserts[1].unit;
    }
};

struct MacroTarget { juce::String param; float amount = 0.0f; };   // "L3.cutoff", "R2" (return of send 2), amount in the control's own 0..1 travel
struct Macro
{
    juce::String name;
    float value = 0.0f;
    std::vector<MacroTarget> targets;
};

struct Preset
{
    juce::String name { "Untitled" }, author, category { "Pads" }, description;
    juce::String world;                      // a world's key, or "" = the world its category lives in
    juce::StringArray tags;
    std::array<LayerState, kLayers> layers;
    std::array<FxState, kSends> sends;
    std::array<float, kSends> returns { { 0.7f, 0.7f, 0.7f } };
    std::array<FxState, kMasterSlots> master;
    float masterDb = 0.0f;
    std::array<Macro, kMacros> macros;
    // not saved in the file
    bool factory = false;
    juce::String file;

    juce::var toVar() const;
    static Preset fromVar (const juce::var&);
    static Preset blank();                   // no layers; chorus / echo / halls on the sends, limiter on the master
    int layerCount() const { int n = 0; for (const auto& l : layers) if (l.used()) ++n; return n; }
    int worldIndex() const { return world.isNotEmpty() ? atlas::worldIndex (world) : worldOfCategory (category); }
};

struct PresetInfo
{
    juce::String name, category, world, author, description, file;   // file: a path, or "factory:<name>"
    juce::StringArray tags, instruments;
    bool factory = false, favourite = false;
    int layers = 0;
};

// the presets the browser shows: the factory set (built into the plug-in) and the user's own files
class PresetBank
{
public:
    void refresh();
    const std::vector<PresetInfo>& list() const { return items; }
    bool load (const PresetInfo&, Preset& out) const;
    bool load (const juce::String& file, Preset& out) const;
    juce::File saveUser (const Preset&);                  // returns the file written (invalid on failure)
    bool removeUser (const PresetInfo&);
    void setFavourite (const juce::String& file, bool on);
    bool isFavourite (const juce::String& file) const { return favourites.contains (file); }
    void touchRecent (const juce::String& file);
    const juce::StringArray& recent() const { return recents; }
    static juce::var factoryData();                       // the built-in presets (an array)

private:
    void loadSettings(); void saveSettings() const;
    std::vector<PresetInfo> items;
    juce::StringArray favourites, recents;
};
} // namespace atlas
