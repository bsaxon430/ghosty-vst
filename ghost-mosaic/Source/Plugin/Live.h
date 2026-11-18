// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
//
// MOVING NOW (build 15): the words and numbers for the original controls TERRAIN moves (Engine::Live), shared by the knobs, the
// UNDER THE HOOD rows and the TERRAIN page's strip.
#pragma once
#include "../Engine/Engine.h"
#include "../Engine/Params.h"

namespace mosaic::live
{
inline const char* name (int i)
{
    static const char* n[Engine::kLiveCount] = { "NOVELTY", "BOREDOM", "SMOOTH", "REPEAT", "ORIGINAL MIX", "TONE / TIMBRE", "SHAPE / PITCH ONLY", "SEARCH STYLE",
                                                     "LOWEST BAND", "HIGHEST BAND", "CONNECTIONS", "WAIT UNTIL CLOSE", "AUTOTUNE", "FLATTEN" };
    return i >= 0 && i < Engine::kLiveCount ? n[i] : "";
}
inline juce::String choice (float v)
{
    const auto items = juce::StringArray::fromTokens (kAlgoChoices, "|", "");
    return items[juce::jlimit (0, items.size() - 1, juce::roundToInt (v))].upToFirstOccurrenceOf (" (", false, false);
}
inline juce::String text (int i, float v)
{
    if (i == Engine::kLiveRepeat || (i >= Engine::kLiveFftStart && i <= Engine::kLiveSlideError)) return juce::String (juce::roundToInt (v));
    if (i == Engine::kLiveAlgo) return choice (v);
    return juce::String (v, 2);
}
}
