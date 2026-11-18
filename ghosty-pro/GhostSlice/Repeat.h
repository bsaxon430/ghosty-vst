#pragma once
// GHOST REPEAT (GHOST-SLICE-STUDY.md 7.3): a retrigger span on the output timeline. The plan compiler cuts the span out of the
// arrangement and fills it with repetitions of the audio at its start (RETRIGGER) or of the audio under each trigger (gate).
// Every repetition is an ordinary piece (with its own fingerprints: equal repetitions share one STUDIO render), so a REPEAT is
// nondestructive, plays LIVE at once and renders bit-exact. The trigger maths is Shared/SliceVoice.h (retrig::).
#include "RenderPlan.h"

namespace gslice::repeat
{
struct Trigger
{
    double t = 0, len = 0;          // seconds from the span start; the repetition's length
    int n = 0;                      // repetition index
    double pitch = 0;               // semitones
    bool reverse = false;
    double gain = 1.0;
    double fadeIn = 0.001, fadeOut = 0.004;   // seconds (fitted to short periods)
};
double period (const RepeatOp&, double bpm);                       // the base period (s)
std::vector<Trigger> triggers (const RepeatOp&, double bpm, double sampleRate);   // sampleRate: the op's PF rate (start / length units)
// the compiler pass (on the sorted pieces of the arrangement, HF): cuts each span and inserts its repetitions
void expand (std::vector<Piece>& pieces, const Project& p, double hostRate, double bpm);
}

namespace gslice::ops
{
R addRepeat (const Project&, double a, double b, int64* newId = nullptr, juce::String* why = nullptr);   // a range (PF) -> a REPEAT span
R setRepeat (const Project&, const RepeatOp&);                                                          // every parameter (by id)
R removeRepeat (const Project&, int64 id);
}
