#pragma once
// CHAIN BRAIN v1 (Ghost Box only): SOURCE + GOAL -> 3 rule-based chains (unit choice, order, parameter ranges),
// seeded so the same seed always gives the same three chains. Applying a chain is a whole-rack change (undoable).
#include "FxRackImpl.h"

#if GPFX_BOX
namespace gpfx {
namespace brain {

inline const juce::StringArray& sourceNames() { static juce::StringArray s { "BASS", "DRUMS", "VOCAL", "SYNTH LEAD", "PAD", "FULL MIX" }; return s; }
inline const juce::StringArray& goalNames() { static juce::StringArray s { "AGGRESSIVE", "HUGE", "CINEMATIC", "NOSTALGIC", "HIT HARDER", "MOVE", "DISAPPEAR INTO SPACE", "LO-FI" }; return s; }

struct Setting { int unit; float mix; std::vector<std::pair<juce::String, float>> vals; };   // suffix after fx_<unit>_ -> real value
struct Chain
{
    juce::String name, recipe, why;       // e.g. "A", "Crackle Rage", one line on what the chain does
    std::vector<Setting> units;           // in processing order
    juce::String describe() const;        // "CRACKLE > SQUEEZE"
};

std::array<Chain, 3> generate (int source, int goal, uint32_t seed);
void apply (Rack::Impl& rack, const Chain& c);   // whole rack: every unit back to default and off, then the chain (limiter kept)
// the audition lives in Rack::Impl (brainAudition / brainMatch): start one, end one (fold = keep the matched loudness as make-up gain)
void audition (Rack::Impl& rack, int which);
void endAudition (Rack::Impl& rack, bool foldTrim);

} // namespace brain
} // namespace gpfx
#endif
