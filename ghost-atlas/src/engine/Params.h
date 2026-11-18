// SPDX-License-Identifier: AGPL-3.0-or-later
// GHOST ATLAS - every control a layer has, declared once: its saved name, its range, how it is shown.
// The screen, the presets, the macros and MUTATE all read this table.
#pragma once
#include "../core/Common.h"

namespace atlas
{
enum LayerParam
{
    lpOn, lpGain, lpPan, lpCoarse, lpFine, lpKeyLo, lpKeyHi, lpVelLo, lpVelHi, lpVelSens, lpVelCurve,
    lpAttack, lpRelease, lpStart,
    lpFilterType, lpCutoff, lpReso, lpWidth,
    lpLfoShape, lpLfoSync, lpLfoRate, lpLfoDiv, lpLfoAmp, lpLfoPan, lpLfoCutoff, lpLfoPitch,
    lpSendA, lpSendB, lpSendC, lpMute, lpSolo,
    kNumLayerParams
};

struct ParamDef
{
    const char* id; const char* name; float min, max, def; const char* unit;
    enum Kind { Float, Int, Bool, Choice } kind;
    bool log;
    std::vector<const char*> choices;
};

inline const ParamDef& layerParam (int p)
{
    static const std::vector<ParamDef> d = {
        { "on",        "ON",        0, 1, 1, "", ParamDef::Bool, false, {} },
        { "gain",      "LEVEL",     -60, 12, 0, "dB", ParamDef::Float, false, {} },
        { "pan",       "PAN",       -1, 1, 0, "", ParamDef::Float, false, {} },
        { "coarse",    "PITCH",     -36, 36, 0, "st", ParamDef::Int, false, {} },
        { "fine",      "FINE",      -100, 100, 0, "ct", ParamDef::Float, false, {} },
        { "keyLo",     "LOW KEY",   0, 127, 0, "", ParamDef::Int, false, {} },
        { "keyHi",     "HIGH KEY",  0, 127, 127, "", ParamDef::Int, false, {} },
        { "velLo",     "LOW VEL",   1, 127, 1, "", ParamDef::Int, false, {} },
        { "velHi",     "HIGH VEL",  1, 127, 127, "", ParamDef::Int, false, {} },
        { "velSens",   "TOUCH",     0, 1, 1, "%", ParamDef::Float, false, {} },
        { "velCurve",  "CURVE",     -1, 1, 0, "", ParamDef::Float, false, {} },
        { "attack",    "ATTACK",    0, 1, 0, "s", ParamDef::Float, false, {} },
        { "release",   "RELEASE",   -1, 1, 0, "", ParamDef::Float, false, {} },
        { "start",     "START",     0, 1, 0, "%", ParamDef::Float, false, {} },
        { "filter",    "FILTER",    0, 4, 0, "", ParamDef::Choice, false, { "OFF", "LOW PASS", "LOW PASS 24", "HIGH PASS", "BAND PASS" } },
        { "cutoff",    "CUTOFF",    20, 20000, 20000, "Hz", ParamDef::Float, true, {} },
        { "reso",      "RESONANCE", 0, 1, 0.15f, "%", ParamDef::Float, false, {} },
        { "width",     "WIDTH",     0, 2, 1, "%", ParamDef::Float, false, {} },
        { "lfoShape",  "SHAPE",     0, 4, 0, "", ParamDef::Choice, false, { "SINE", "TRIANGLE", "RAMP", "SQUARE", "DRIFT" } },
        { "lfoSync",   "SYNC",      0, 1, 0, "", ParamDef::Bool, false, {} },
        { "lfoRate",   "RATE",      0.02f, 20, 0.5f, "Hz", ParamDef::Float, true, {} },
        { "lfoDiv",    "TIME",      0, 9, 4, "", ParamDef::Choice, false, { "8 BARS", "4 BARS", "2 BARS", "1 BAR", "1/2", "1/4", "1/8", "1/8T", "1/16", "1/32" } },
        { "lfoAmp",    "TO LEVEL",  0, 1, 0, "%", ParamDef::Float, false, {} },
        { "lfoPan",    "TO PAN",    0, 1, 0, "%", ParamDef::Float, false, {} },
        { "lfoCutoff", "TO FILTER", 0, 1, 0, "%", ParamDef::Float, false, {} },
        { "lfoPitch",  "TO PITCH",  0, 1, 0, "%", ParamDef::Float, false, {} },
        { "sendA",     "MOTION",    0, 1, 0, "%", ParamDef::Float, false, {} },
        { "sendB",     "ECHO",      0, 1, 0, "%", ParamDef::Float, false, {} },
        { "sendC",     "SPACE",     0, 1, 0, "%", ParamDef::Float, false, {} },
        { "mute",      "MUTE",      0, 1, 0, "", ParamDef::Bool, false, {} },
        { "solo",      "SOLO",      0, 1, 0, "", ParamDef::Bool, false, {} },
    };
    return d[(size_t) juce::jlimit (0, kNumLayerParams - 1, p)];
}
inline int layerParamIndex (const juce::String& id)
{
    for (int p = 0; p < kNumLayerParams; ++p) if (id == layerParam (p).id) return p;
    return -1;
}
inline float toNorm (const ParamDef& d, float v)
{
    v = juce::jlimit (d.min, d.max, v);
    if (d.log) return std::log (v / d.min) / std::log (d.max / d.min);
    return (v - d.min) / (d.max - d.min);
}
inline float fromNorm (const ParamDef& d, float n)
{
    n = clamp01 (n);
    float v = d.log ? d.min * std::pow (d.max / d.min, n) : d.min + (d.max - d.min) * n;
    if (d.kind != ParamDef::Float) v = std::round (v);
    return v;
}
// what the attack / release / start controls mean in time
inline float attackSeconds (float v) { return 6.0f * v * v; }
inline float releaseAddSeconds (float v) { return v > 0 ? 10.0f * v * v : 0.0f; }

inline juce::String formatParam (int p, float v)
{
    const auto& d = layerParam (p);
    switch (p)
    {
        case lpGain:    return v <= -59.5f ? juce::String ("off") : juce::String (v, 1) + " dB";
        case lpPan:     return std::abs (v) < 0.01f ? juce::String ("centre") : juce::String ((int) std::lround (std::abs (v) * 100.0f)) + (v < 0 ? " L" : " R");
        case lpCoarse:  return (v > 0 ? "+" : "") + juce::String ((int) v) + " st";
        case lpFine:    return (v > 0 ? "+" : "") + juce::String ((int) std::lround (v)) + " ct";
        case lpKeyLo: case lpKeyHi: return noteName ((int) v);
        case lpVelLo: case lpVelHi: return juce::String ((int) v);
        case lpAttack:  return attackSeconds (v) < 0.01f ? juce::String ("as made") : "+" + juce::String (attackSeconds (v), 2) + " s";
        case lpRelease: return std::abs (v) < 0.01f ? juce::String ("as made") : (v > 0 ? "+" + juce::String (releaseAddSeconds (v), 2) + " s" : juce::String ((int) std::lround ((1.0f + v * 0.97f) * 100.0f)) + " %");
        case lpCutoff:  return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz" : juce::String ((int) v) + " Hz";
        case lpLfoRate: return juce::String (v, 2) + " Hz";
        case lpWidth:   return juce::String ((int) std::lround (v * 100.0f)) + " %";
        default: break;
    }
    if (d.kind == ParamDef::Choice) return d.choices[(size_t) juce::jlimit (0, (int) d.choices.size() - 1, (int) v)];
    if (d.kind == ParamDef::Bool) return v > 0.5f ? "on" : "off";
    if (juce::String (d.unit) == "%") return juce::String ((int) std::lround (v * 100.0f)) + " %";
    return juce::String (v, 2);
}
} // namespace atlas
