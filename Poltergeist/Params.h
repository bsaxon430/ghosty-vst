#pragma once
// Poltergeist parameters: ids, ranges, and a plain snapshot the engine reads each block.
#include <juce_audio_processors/juce_audio_processors.h>

namespace gs {

constexpr int kOps = 4;
inline juce::String opId(int op, const char* what) { return "op" + juce::String(op + 1) + "_" + what; }
inline juce::String modId(int from, int to) { return "mod_" + juce::String(from + 1) + "_" + juce::String(to + 1); }

static const juce::StringArray kMacroIds { "haunt", "spectre", "possess", "drift", "veil", "curse" };
static const juce::StringArray kMacroNames { "HAUNT", "SPECTRE", "POSSESS", "DRIFT", "VEIL", "CURSE" };
static const juce::StringArray kMacroHints {
    "FM growl - how hard the ghosts hit each other",
    "Unison - more ghosts, wider and thicker",
    "Drive - warm grit up to full possession",
    "Movement - wobble, wow and vibrato",
    "Space - reverb and echo",
    "Glitch - crush, decimate and stutter" };
static const juce::StringArray kLfoDivs { "1/1", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/8D", "1/16", "1/16T", "1/32" };
inline double lfoDivBeats(int i) {
    static const double b[] = { 4.0, 2.0, 1.0, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.75, 0.25, 1.0 / 6.0, 0.125 };
    return b[juce::jlimit(0, 9, i)];
}

inline juce::NormalisableRange<float> timeRange(float lo, float hi) {
    juce::NormalisableRange<float> r(lo, hi);
    r.setSkewForCentre(lo + (hi - lo) * 0.06f);
    return r;
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout() {
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout L;
    auto F = [&](const String& id, const String& name, NormalisableRange<float> r, float def) {
        L.add(std::make_unique<AudioParameterFloat>(ParameterID { id, 1 }, name, r, def));
    };
    for (int o = 0; o < kOps; ++o) {
        const String n = "Ghost " + String(o + 1) + " ";
        NormalisableRange<float> ratio(0.25f, 16.0f); ratio.setSkewForCentre(2.0f);
        F(opId(o, "ratio"), n + "Ratio", ratio, o == 0 ? 1.0f : (o == 1 ? 2.0f : (o == 2 ? 1.0f : 3.0f)));
        F(opId(o, "fine"), n + "Fine", { -50.0f, 50.0f }, 0.0f);
        F(opId(o, "level"), n + "Out", { 0.0f, 1.0f }, o == 0 ? 0.8f : 0.0f);
        F(opId(o, "wave"), n + "Wave", { 0.0f, 3.0f }, 0.0f);
        F(opId(o, "fb"), n + "Feedback", { 0.0f, 1.0f }, 0.0f);
        F(opId(o, "a"), n + "Attack", timeRange(0.001f, 10.0f), 0.003f);
        F(opId(o, "d"), n + "Decay", timeRange(0.005f, 10.0f), 0.6f);
        F(opId(o, "s"), n + "Sustain", { 0.0f, 1.0f }, o == 0 ? 0.8f : 0.4f);
        F(opId(o, "r"), n + "Release", timeRange(0.005f, 10.0f), 0.35f);
        F(opId(o, "vel"), n + "Velocity", { 0.0f, 1.0f }, 0.5f);
    }
    for (int from = 0; from < kOps; ++from)
        for (int to = 0; to < kOps; ++to)
            if (from != to)
                F(modId(from, to), "Ghost " + String(from + 1) + " > " + String(to + 1), { 0.0f, 1.0f }, (from == 1 && to == 0) ? 0.35f : 0.0f);

    F("penv_amt", "Pitch Env", { -48.0f, 48.0f }, 0.0f);
    F("penv_dec", "Pitch Env Decay", timeRange(0.005f, 2.0f), 0.06f);

    NormalisableRange<float> cut(20.0f, 20000.0f); cut.setSkewForCentre(1000.0f);
    F("flt_cut", "Filter Cutoff", cut, 20000.0f);
    F("flt_res", "Filter Reso", { 0.0f, 1.0f }, 0.1f);
    L.add(std::make_unique<AudioParameterChoice>(ParameterID { "flt_type", 1 }, "Filter Type", StringArray { "Low", "Band", "High" }, 0));
    F("flt_env", "Filter Env", { -1.0f, 1.0f }, 0.0f);
    F("flt_a", "Filter Attack", timeRange(0.001f, 10.0f), 0.005f);
    F("flt_d", "Filter Decay", timeRange(0.005f, 10.0f), 0.4f);
    F("flt_s", "Filter Sustain", { 0.0f, 1.0f }, 0.0f);
    F("flt_r", "Filter Release", timeRange(0.005f, 10.0f), 0.3f);
    F("flt_key", "Filter Keytrack", { 0.0f, 1.0f }, 0.3f);

    NormalisableRange<float> lr(0.02f, 30.0f); lr.setSkewForCentre(3.0f);
    F("lfo_rate", "Drift Rate", lr, 2.0f);
    L.add(std::make_unique<AudioParameterBool>(ParameterID { "lfo_sync", 1 }, "Drift Sync", false));
    L.add(std::make_unique<AudioParameterChoice>(ParameterID { "lfo_div", 1 }, "Drift Division", kLfoDivs, 4));
    L.add(std::make_unique<AudioParameterChoice>(ParameterID { "lfo_shape", 1 }, "Drift Shape", StringArray { "Sine", "Triangle", "Saw", "Square", "Random" }, 0));

    for (int m = 0; m < kMacroIds.size(); ++m)
        F("macro_" + kMacroIds[m], kMacroNames[m], { 0.0f, 1.0f }, m == 4 ? 0.15f : 0.0f);

    F("glide", "Glide", timeRange(0.0f, 1.0f), 0.0f);
    L.add(std::make_unique<AudioParameterBool>(ParameterID { "mono", 1 }, "Mono", false));
    L.add(std::make_unique<AudioParameterInt>(ParameterID { "octave", 1 }, "Octave", -3, 3, 0));
    L.add(std::make_unique<AudioParameterInt>(ParameterID { "voices", 1 }, "Voices", 1, 16, 8));
    F("bend", "Bend Range", { 0.0f, 24.0f }, 2.0f);
    F("master", "Master", { -24.0f, 6.0f }, -6.0f);
    return L;
}

struct OpSnap { float ratio, fine, level, wave, fb, a, d, s, r, vel; };
struct Snap {
    OpSnap op[kOps];
    float mod[kOps][kOps] {};   // mod[from][to]
    float penvAmt, penvDec;
    float cut, res; int ftype; float fenv, fa, fd, fs, fr, fkey;
    float lfoRate; bool lfoSync; int lfoDiv, lfoShape;
    float macro[6];
    float glide; bool mono; int octave, voices; float bend, masterDb;
};

// Reads every parameter as a raw float pointer once, then snapshots quickly on the audio thread.
struct ParamReader {
    struct OpPtrs { std::atomic<float>* p[10]; };
    OpPtrs op[kOps];
    std::atomic<float>* mod[kOps][kOps] {};
    std::atomic<float>* g[64] {};
    juce::StringArray gNames;
    void attach(juce::AudioProcessorValueTreeState& s) {
        const char* opw[] = { "ratio", "fine", "level", "wave", "fb", "a", "d", "s", "r", "vel" };
        for (int o = 0; o < kOps; ++o)
            for (int k = 0; k < 10; ++k) op[o].p[k] = s.getRawParameterValue(opId(o, opw[k]));
        for (int f = 0; f < kOps; ++f)
            for (int t = 0; t < kOps; ++t) mod[f][t] = f == t ? nullptr : s.getRawParameterValue(modId(f, t));
        gNames = { "penv_amt", "penv_dec", "flt_cut", "flt_res", "flt_type", "flt_env", "flt_a", "flt_d", "flt_s", "flt_r", "flt_key",
                   "lfo_rate", "lfo_sync", "lfo_div", "lfo_shape", "glide", "mono", "octave", "voices", "bend", "master" };
        for (auto& m : kMacroIds) gNames.add("macro_" + m);
        for (int i = 0; i < gNames.size(); ++i) g[i] = s.getRawParameterValue(gNames[i]);
    }
    float v(int i) const { return g[i]->load(); }
    void read(Snap& n) const {
        for (int o = 0; o < kOps; ++o) {
            auto& P = op[o].p;
            n.op[o] = { P[0]->load(), P[1]->load(), P[2]->load(), P[3]->load(), P[4]->load(), P[5]->load(), P[6]->load(), P[7]->load(), P[8]->load(), P[9]->load() };
        }
        for (int f = 0; f < kOps; ++f)
            for (int t = 0; t < kOps; ++t) n.mod[f][t] = mod[f][t] ? mod[f][t]->load() : 0.0f;
        n.penvAmt = v(0); n.penvDec = v(1); n.cut = v(2); n.res = v(3); n.ftype = (int) v(4); n.fenv = v(5);
        n.fa = v(6); n.fd = v(7); n.fs = v(8); n.fr = v(9); n.fkey = v(10);
        n.lfoRate = v(11); n.lfoSync = v(12) > 0.5f; n.lfoDiv = (int) v(13); n.lfoShape = (int) v(14);
        n.glide = v(15); n.mono = v(16) > 0.5f; n.octave = (int) v(17); n.voices = (int) v(18); n.bend = v(19); n.masterDb = v(20);
        for (int m = 0; m < 6; ++m) n.macro[m] = v(21 + m);
    }
};

} // namespace gs
