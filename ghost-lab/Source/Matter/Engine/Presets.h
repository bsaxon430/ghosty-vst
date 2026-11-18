// Ghost Matter - AGPL-3.0-only (see LICENSE, NOTICE.md).
// The factory presets (build 2): each starts from the open-source engines' own defaults (Presets.cpp). No samples.
#pragma once
#include "Settings.h"
#include <map>

namespace matter
{
struct Preset
{
    juce::String name, group;
    RenderSettings render;
    std::map<juce::String, float> params;   // host parameters (plain values); missing ones go to their defaults
    juce::String terrainShape;              // a TERRAIN shape name (empty = a flat land)
};
const std::vector<Preset>& factoryPresets();
}
