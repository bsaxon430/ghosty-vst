#pragma once
// Ghost FX rack: per-unit presets (factory + user files) and whole-rack scenes (factory + user files).
// Factory unit presets touch only their own unit's fx_<unit>_* parameters; scenes change the whole rack.
#include "FxRackImpl.h"
#include <functional>
#include <map>

namespace gpfx {

struct UnitPreset
{
    juce::String name;
    std::vector<std::pair<const char*, float>> vals;      // parameter suffix (after fx_<unit>_) -> real value
    std::function<void (Rack::Impl&)> extra;              // non-parameter state (HAUNT pad kits)
    UnitPreset (juce::String n, std::vector<std::pair<const char*, float>> v, std::function<void (Rack::Impl&)> x = {})
        : name (std::move (n)), vals (std::move (v)), extra (std::move (x)) {}
};
const std::vector<UnitPreset>& unitPresets (int unit);

struct FactoryScene { const char* key; const char* display; };
const std::vector<FactoryScene>& factoryScenes();
juce::String sceneDisplayName (const juce::String& key);

// Documents/Ghosty/Ghosty Pro/FX Presets/<Unit>/ and Documents/Ghosty/Ghosty Pro/FX Scenes/ (created on demand)
juce::File unitPresetDir (int unit);
juce::File sceneDir();
juce::File boardDir();   // SPIRIT BOARD files (Ghost Box)
juce::Array<juce::File> listXml (const juce::File& dir);   // sorted by name

} // namespace gpfx
