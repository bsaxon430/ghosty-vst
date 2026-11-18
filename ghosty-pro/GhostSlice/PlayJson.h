#pragma once
// JSON of the PLAY / REPEAT / HOLD types (project.json; exact round trip of every double)
#include "PlayTypes.h"

namespace gslice::playjson
{
    juce::var holdsToVar (const std::vector<HoldOp>&);
    std::vector<HoldOp> holdsFromVar (const juce::var&);
    juce::var repeatsToVar (const std::vector<RepeatOp>&);
    std::vector<RepeatOp> repeatsFromVar (const juce::var&);
    juce::var playToVar (const PlayMap&);
    PlayMap playFromVar (const juce::var&);
}
