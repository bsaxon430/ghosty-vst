#pragma once
// TERRAIN (de)serialisation: JSON-able juce::var, a compact string, and a ValueTree (for plug-in state / presets / sessions).
// Versioned ("v": 1). Unknown future fields are ignored; missing fields take defaults; everything is clamped on read.
#include "TerrainModel.h"
#include <juce_data_structures/juce_data_structures.h>

namespace ghost::terrain
{
juce::var toVar (const Terrain& t);
bool fromVar (const juce::var& v, Terrain& out);

juce::String toString (const Terrain& t);                 // compact JSON on one line
bool fromString (const juce::String& s, Terrain& out);

juce::ValueTree toValueTree (const Terrain& t, const juce::Identifier& type = "TERRAIN");
bool fromValueTree (const juce::ValueTree& v, Terrain& out);
}
