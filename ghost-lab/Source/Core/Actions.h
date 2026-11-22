#pragma once
// Every Ghost Lab action is a recipe of CDP8 programs. The recipes, their plain-word controls and the processing itself.
#include "AudioTools.h"
#include "Cdp.h"
#include <map>

namespace glab
{
enum class Op
{
    Pull, Twist, Freeze, Smear, Shatter, Mutate,            // the six cards (and the body gestures)
    GrainCloud, SpectralMelt, TimeWarp, VoiceFusion,        // the experiment table
    Pinch, Breed,                                           // gestures only: pinch the body, merge two specimens
    Hood,                                                   // UNDER THE HOOD: one real CDP process with all its own parameters (Step.data = the request)
    Terrain,                                                // the TERRAIN page: the landscape as a spectral sculpt (our own, no CDP)
    Grow, XStretch, Reborn, Space,                          // the rooms (build 12, Rooms.h): the engines that moved in from Ghost Matter
    Spectre,                                                // build 17: SPECTRE, the resynthesis room (Spectre.h), our own engine
    Count
};

struct Control
{
    juce::String id, label;   // label in plain words ("How long")
    float min, max, def;
    juce::String unit;        // shown after the number ("x", "s", "Hz", "")
    bool deep;                // only shown with DEEP on
};

struct OpInfo
{
    Op op;
    juce::String id, title, subtitle, cdp;   // cdp: the programs it runs (shown with DEEP on)
    std::vector<Control> controls;
};

const OpInfo& info (Op op);
Op opFromId (const juce::String& id);

// one processing step: what, with which settings, and its random seed
struct Step
{
    Op op = Op::Pull;
    std::map<juce::String, float> values;   // missing values use the control's default
    juce::uint32 seed = 1;
    juce::String data;                       // TERRAIN: the landscape (JSON); empty for every other action

    float get (const juce::String& id) const;
    juce::var toVar() const;
    static bool fromVar (const juce::var& v, Step& out);
    juce::String describe() const;           // "PULL 2.5x"
};

struct RenderResult
{
    std::shared_ptr<Audio> audio;
    juce::String error;
    juce::StringArray commands;   // every CDP command that ran (tests + deep view)
};

// renders one step on `in` (partner: the second specimen for VOICE FUSION / BREED). Runs CDP programs, so it takes a
// while: call it on a worker thread, never the audio thread. The result is gain-staged (finish()).
RenderResult render (const Step& s, const Audio& in, const Audio* partner, const Cdp& cdp, const std::atomic<bool>* cancel = nullptr);
}
