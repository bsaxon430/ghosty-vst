// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ControlUse.h"
#include "Catalog.h"
#include "../engines/Engines.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace phantom
{
namespace
{
    using Ids = std::set<std::string>;
    int idx (const ControlValue& v, const std::string& id) { return (int) std::lround (v (id)); }
    bool onOf (const ControlValue& v, const std::string& id) { return v (id) >= 0.5f; }
    bool in (const Ids& s, const std::string& id) { return s.count (id) > 0; }

    // the controls each alternative engine reads (from its source: engines/*.h, readControls / read / process). Every other control of
    // the unit is left out while that engine plays. ENGINE itself, and the slot's ON / MIX / TRIM, are never in these lists' scope.
    const Ids* engineReads (int u, const std::string& engine)
    {
        static const std::map<std::string, Ids> k {
            { "19|VINTAGE",  { "size", "decay", "predelay", "diffusion", "damp", "lowcut", "mod", "width", "freeze" } },            // LYNX Epoch
            { "19|ROOMS",    { "size", "decay", "predelay", "diffusion", "damp", "lowcut", "mod", "width", "early", "freeze" } },   // LYNX Atrium
            { "19|PLATES",   { "size", "decay", "predelay", "damp", "lowcut", "mod", "width", "freeze" } },                         // LYNX Foil
            { "19|CLASSIC",  { "size", "decay", "damp", "lowcut", "early", "predelay", "freeze", "width" } },                       // Classic Reverb RE-04
            { "17|NEBULA",   { "size", "blend", "decay", "tone", "damp", "mod", "width", "predelay" } },
            { "17|HORIZON",  { "size", "decay", "tone", "damp", "mod", "width", "predelay" } },
            { "18|HALO",     { "decay", "damp", "shimmer", "predelay", "mod", "width", "freeze", "interval", "hold" } },             // DECAY is its size
            { "23|APEX",     { "ceiling", "gain", "lookahead", "release", "truepeak" } },
            { "23|CLASSIC",  { "ceiling", "gain" } },                                                                                 // Classic Master Limiter RE-01
            { "23|GRANITE",  { "ceiling", "gain", "lookahead", "release", "truepeak", "quality" } },
            { "16|ECHOFORM", { "sync", "time", "div", "fb", "lowcut", "highcut", "char", "duck", "sat", "ping", "width" } },
            { "16|SPIRAL",   { "sync", "time", "div", "fb", "char", "lowcut", "highcut", "width" } },
            { "14|ORBIT",    { "rate", "depth", "delay", "fb", "width" } },
            { "7|CRUSHER",   { "crush", "dark", "blend", "out" } },
            { "5|CURVES",    { "drive", "tone", "lowcut", "highcut", "bias", "auto", "out" } },
            { "5|DRAW",      { "drive", "tone", "lowcut", "highcut", "bias", "auto", "out" } },
            { "4|CURVES",    { "b1_drive", "prehp", "prelp", "posthp", "postlp", "auto", "out" } },
            { "4|DRAW",      { "b1_drive", "prehp", "prelp", "posthp", "postlp", "auto", "out" } },
            { "6|CURVES",    { "drive", "bias", "out", "bass", "treble" } },
            { "6|DRAW",      { "drive", "bias", "out", "bass", "treble" } },
            // (PRISM reads every GHOST SQUEEZE control)
        };
        auto it = k.find (std::to_string (u) + "|" + engine); return it == k.end() ? nullptr : &it->second;
    }
    std::string engineOf (int u, const ControlValue& v)
    {
        const auto labels = engines::engineLabels (u); if (labels.empty()) return "PHANTOM";
        const int e = std::clamp (idx (v, "engine"), 0, (int) labels.size() - 1); const auto& l = labels[(size_t) e];
        const auto dot = l.find (" \xc2\xb7"); return dot == std::string::npos ? l : l.substr (0, dot);
    }

    // the PHANTOM engine's own rules: a mode, type or switch that leaves a control out
    std::string ownRule (int u, const std::string& id, const ControlValue& v)
    {
        switch (u)
        {
            case uFilter:
            {
                const int t = idx (v, "type");
                if (id == "reso" && t >= 31 && t <= 34) return "THE SAMPLER FILTERS HAVE NO RESONANCE";
                if (id == "poles") { static const Ids uses { "0", "1", "2", "3", "4", "7", "13", "31", "32", "33", "34" }; if (! in (uses, std::to_string (t))) return "THIS FILTER TYPE HAS ONE SLOPE"; }
                if (id == "spread" && idx (v, "routing") == 0) return "SPREAD MOVES THE SECOND FILTER: ROUTING IS SINGLE";
                if (id == "lforate" && onOf (v, "lfosync")) return "LFO SYNC IS ON: THE DIVISION SETS THE RATE";
                if (id == "lfodiv" && ! onOf (v, "lfosync")) return "LFO SYNC IS OFF: THE RATE IN HZ IS USED";
                break;
            }
            case uGate:
            {
                const int pm = idx (v, "pmode"); const bool audio = idx (v, "trig") == 1;
                static const Ids pump { "pdepth", "pshape", "plen", "prel", "patk", "phold", "pthr", "ponly" };
                if (in (pump, id) && pm == 0) return "PUMP MODE IS OFF";
                if (id == "pthr" && pm != 5) return "ONLY FOR THE SIDECHAIN PUMP";
                if (id == "prel" && pm != 5) return "ONLY FOR THE SIDECHAIN PUMP";
                if (id == "plen" && pm != 4) return "ONLY FOR THE DRAWN PUMP";
                if ((id == "pshape" || id == "phold") && pm == 4) return "THE DRAWN PUMP USES ITS DRAWING";
                if (id == "plook" && ! audio && pm != 5) return "ONLY WITH AN AUDIO TRIGGER OR THE SIDECHAIN PUMP";
                if ((id == "tthr" || id == "tsens") && ! audio) return "TRIGGER IS HOST SYNC";
                static const Ids pattern { "length", "smooth", "time", "swing", "trig" };
                if (in (pattern, id) && pm != 0 && onOf (v, "ponly")) return "PUMP ONLY IS ON: THE PATTERN IS NOT PLAYED";
                break;
            }
            case uWarp: if (id == "formant" && idx (v, "mode") == 1) return "TAPE MODE BENDS TIME AND PITCH TOGETHER"; break;
            case uCrackle:
            {
                const int bands = idx (v, "bands") + 1;
                if (bands < 2 && (id == "xlo" || id.rfind ("b2_", 0) == 0)) return "BANDS IS 1";
                if (bands < 3 && (id == "xhi" || id.rfind ("b3_", 0) == 0)) return bands == 1 ? "BANDS IS 1" : "BANDS IS 2";
                if (id == "boxmix" && idx (v, "box") == 0) return "BOX IS OFF";
                break;
            }
            case uRetro:
            {
                struct S { const char* sw; Ids ids; const char* why; };
                static const S sec[] { { "noise_on", { "noise", "ntype", "ntone", "nfollow" }, "NOISE IS OFF" }, { "wobble_on", { "wobble", "flutter" }, "WOBBLE IS OFF" },
                                       { "dist_on", { "dist", "dtype" }, "DISTORT IS OFF" }, { "digi_on", { "digi" }, "DIGITAL IS OFF" }, { "space_on", { "space" }, "SPACE IS OFF" },
                                       { "mag_on", { "mag", "bias" }, "MAGNETIC IS OFF" } };
                for (auto& s : sec) if (in (s.ids, id) && ! onOf (v, s.sw)) return s.why;
                if (id == "speed" && ! onOf (v, "mag_on") && ! onOf (v, "wobble_on")) return "TAPE SPEED SHAPES MAGNETIC AND WOBBLE: BOTH ARE OFF";
                break;
            }
            case uFatten: if (id == "punch" && idx (v, "mode") == 6) return "TRANSIENT MODE ALWAYS PUNCHES"; break;
            case uFormant:
            {
                const int mv = idx (v, "move");
                if ((id == "mdiv" || id == "mdepth" || id == "mshape") && mv == 0) return "MOVEMENT IS STILL";
                if (id == "mlen") return "THE DRAWN LANE IS NOT BUILT YET (DRAWN MOVES WITH THE LFO SHAPE)";
                break;
            }
            case uEq:
            {
                const auto us = id.find ('_'); if (us == std::string::npos) break;
                const std::string b = id.substr (0, us + 1), k = id.substr (us + 1);
                if (k == "spec") return "NOT BUILT YET";
                if (k != "on" && ! onOf (v, b + "on")) return "THIS BAND IS OFF";
                const int t = idx (v, b + "type"); const bool cut = t == 3 || t == 4, shapesGain = t <= 2 || t >= 7;
                if (k == "slope" && ! cut) return "ONLY THE LOW AND HIGH CUTS HAVE A SLOPE";
                if (k == "gain" && ! shapesGain) return "CUTS, NOTCH AND BAND PASS HAVE NO GAIN";
                if (k == "q" && (cut || t >= 7) && ! (onOf (v, b + "dyn") && shapesGain)) return cut ? "THE CUTS' SHAPE COMES FROM THEIR SLOPE" : "THE TILTS HAVE A FIXED SHAPE";
                static const Ids dyn { "thr", "range", "ratio", "att", "rel", "auto", "detect" };
                if (k == "dyn" && ! shapesGain) return "DYNAMICS WORK ON THE GAIN SHAPES (BELL, SHELVES, TILTS)";
                if (in (dyn, k) && (! onOf (v, b + "dyn") || ! shapesGain)) return "DYNAMIC IS OFF";
                break;
            }
            case uChorus: if (id == "voices" && idx (v, "mode") == 1) return "ENSEMBLE HAS ITS OWN THREE VOICES"; break;
            case uEcho:
                if ((id == "spread" || id == "pan") && idx (v, "nodes") <= 1) return "ONE NODE: NOTHING TO SPREAD";
                if (id == "char" && idx (v, "mode") == 3) return "DIGITAL HAS NO CHARACTER";
                break;
            case uHalls: if (id == "early" && idx (v, "mode") == 9) return "NONLINEAR IS ALL EARLY REFLECTIONS"; break;
            case uSpectra: if (id == "fsync" && ! onOf (v, "freeze")) return "FREEZE IS OFF"; break;
            default: break;
        }
        return {};
    }
}

std::string controlIdleReason (int u, const std::string& id, const ControlValue& v)
{
    if (id == "engine" || id == "on" || id == "mix" || id == "trim") return {};
    const auto engine = engineOf (u, v);
    if (engine != "PHANTOM")
    {
        if (const Ids* r = engineReads (u, engine); r != nullptr && ! in (*r, id)) return "NOT USED BY THE " + engine + " ENGINE";
        // the engine's own reads still follow their switches
        if (u == uEcho && id == "time" && onOf (v, "sync")) return "SYNC IS ON: THE DIVISION SETS THE TIME";
        if (u == uEcho && id == "div" && ! onOf (v, "sync")) return "SYNC IS OFF: TIME IS USED";
        return {};
    }
    if (u == uEcho && id == "time" && onOf (v, "sync")) return "SYNC IS ON: THE DIVISION SETS THE TIME";
    if (u == uEcho && id == "div" && ! onOf (v, "sync")) return "SYNC IS OFF: TIME IS USED";
    return ownRule (u, id, v);
}

std::vector<std::string> controlRuleIds (int u)
{
    std::vector<std::string> out;
    for (auto& e : engines::engineLabels (u))
    {
        const auto dot = e.find (" \xc2\xb7"); const auto name = dot == std::string::npos ? e : e.substr (0, dot);
        if (const Ids* r = engineReads (u, name)) for (auto& id : *r) out.push_back (id);
    }
    static const std::map<int, std::vector<std::string>> own {
        { uFilter, { "type", "reso", "poles", "routing", "spread", "lforate", "lfosync", "lfodiv" } },
        { uGate, { "pmode", "trig", "pdepth", "pshape", "plen", "prel", "patk", "phold", "pthr", "ponly", "plook", "tthr", "tsens", "length", "smooth", "time", "swing" } },
        { uWarp, { "mode", "formant" } },
        { uCrackle, { "bands", "xlo", "xhi", "b2_drive", "b3_drive", "box", "boxmix" } },
        { uRetro, { "noise_on", "noise", "ntype", "ntone", "nfollow", "wobble_on", "wobble", "flutter", "dist_on", "dist", "dtype", "digi_on", "digi", "space_on", "space", "mag_on", "mag", "bias", "speed" } },
        { uFatten, { "mode", "punch" } },
        { uFormant, { "move", "mdiv", "mdepth", "mshape", "mlen" } },
        { uEq, { "1_on", "1_type", "1_slope", "1_gain", "1_q", "1_dyn", "1_thr", "1_range", "1_ratio", "1_att", "1_rel", "1_auto", "1_detect", "1_spec" } },
        { uChorus, { "mode", "voices" } },
        { uEcho, { "nodes", "spread", "pan", "mode", "char", "sync", "time", "div" } },
        { uHalls, { "mode", "early" } },
        { uSpectra, { "freeze", "fsync" } },
    };
    if (auto it = own.find (u); it != own.end()) out.insert (out.end(), it->second.begin(), it->second.end());
    return out;
}
} // namespace phantom
