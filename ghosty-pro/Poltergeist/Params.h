#pragma once
// Poltergeist parameters: ids, ranges, and a plain snapshot the engine reads each block.
#include <juce_audio_processors/juce_audio_processors.h>

namespace gs {

constexpr int kOps = 6;          // FM ghosts (1-4 are the original ones; 5 and 6 came with the BASS update)
constexpr int kLegacyOps = 4;    // the parameter order of the first release
constexpr int kNoise = 6;        // the NOISE ghost (index 6): can be heard and can haunt the FM ghosts
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
static const juce::StringArray kSmpGhosts { "Ghost 1", "Ghost 2", "Ghost 3", "Ghost 4", "Ghost 5", "Ghost 6" };
static const juce::StringArray kSmpWaveTo { "Off", "Ghost 1", "Ghost 2", "Ghost 3", "Ghost 4", "Ghost 5", "Ghost 6" };
static const juce::StringArray kSampleModes { "Classic", "Granular", "Wavetable", "Slice", "Chop" };
static const juce::StringArray kSeqDests { "Gate", "Filter", "Haunt", "Talk", "Pitch" };
static const juce::StringArray kNoteNames { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static const juce::StringArray kScaleNames { "Major", "Minor", "Harmonic minor", "Dorian", "Phrygian", "Pentatonic minor", "Pentatonic major", "Blues" };
static const juce::StringArray kChordNames { "Off", "Power", "Triad", "7th", "9th", "Sus2", "Sus4", "Octaves" };
static const juce::StringArray kLfoDivs { "1/1", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/8D", "1/16", "1/16T", "1/32" };
inline double lfoDivBeats(int i) {
    static const double b[] = { 4.0, 2.0, 1.0, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.75, 0.25, 1.0 / 6.0, 0.125 };
    return b[juce::jlimit(0, 9, i)];
}

// ---- BASS update tables
// ghost waveforms: 0 = the original morph (sine > tri > saw > square, set with WAVE); 1.. = band-limited tables (WAVE bends their phase)
static const juce::StringArray kWaveNames { "Classic Morph", "Sine", "Triangle", "Saw", "Square", "Pulse 25", "Pulse 12", "Half Sine", "Abs Sine", "Quarter Sine",
    "Parabola", "Sine Saw", "Double Sine", "Camel Sine", "Steps 4", "Steps 8", "Steps 16", "Formant 2", "Formant 3", "Formant 5", "Noisy", "Wail", "Moan",
    "Hollow", "Octave Stack", "Buzz", "Fold Sine", "Spike", "Throat O", "Throat E", "Soft Square", "Ghost Choir" };
static const juce::StringArray kFltTypes { "Low 12", "Low 24 Ladder", "Band", "High 12", "High 24", "Notch", "Comb +", "Comb -", "Formant", "Dirty" };
static const juce::StringArray kFltRoutes { "Series", "Parallel", "Split" };
static const juce::StringArray kNoiseTypes { "White", "Pink", "Crackle" };
static const juce::StringArray kSubWaves { "Sine", "Triangle", "Square", "Rounded" };
static const juce::StringArray kDistTypes { "Overdrive", "Tube", "Hard Clip", "Fold", "Bitcrush", "Downsample" };
static const juce::StringArray kWobDivs { "4/1", "2/1", "1/1", "1/2", "1/2T", "1/2D", "1/4", "1/4T", "1/4D", "1/8", "1/8T", "1/8D", "1/16", "1/16T", "1/16D", "1/32", "1/32T" };
inline double wobDivBeats(int i) {
    static const double b[] = { 16.0, 8.0, 4.0, 2.0, 4.0 / 3.0, 3.0, 1.0, 2.0 / 3.0, 1.5, 0.5, 1.0 / 3.0, 0.75, 0.25, 1.0 / 6.0, 0.375, 0.125, 1.0 / 12.0 };
    return b[juce::jlimit(0, 16, i)];
}
static const juce::StringArray kLfoShapes { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Random", "Smooth Random" };
static const juce::StringArray kModSrcs { "Off", "Wobble", "LFO 1", "LFO 2", "Drift LFO", "Filter Env", "Ghost 1 Env", "Velocity", "Mod Wheel", "Key", "Random" };
enum ModDst { mdOff, mdF1Cut, mdF2Cut, mdBothCut, mdF1Res, mdF2Res, mdVowel, mdFm, mdLvl1, mdLvl2, mdLvl3, mdLvl4, mdLvl5, mdLvl6, mdLvlAll,
              mdPitch, mdNoise, mdSub, mdDrive, mdBend, mdMainCut, mdVolume, mdNumDst };
static const juce::StringArray kModDsts { "Off", "Filter 1 Cutoff", "Filter 2 Cutoff", "Filters 1+2 Cutoff", "Filter 1 Reso", "Filter 2 Reso", "Vowel", "FM Amount",
    "Ghost 1 Level", "Ghost 2 Level", "Ghost 3 Level", "Ghost 4 Level", "Ghost 5 Level", "Ghost 6 Level", "All Ghost Levels", "Pitch", "Noise Level", "Sub Level",
    "Dist Drive", "Wave Bend", "Main Filter Cutoff", "Volume" };
constexpr int kModSlots = 8, kWobPts = 8;
// the engine revision a patch was made for. 0 = the original engine (old projects, the original presets, Ghosty Spectrum):
// those keep every quirk so they sound exactly as before. 2 = the BASS update fixes (linear master with a safety knee,
// smoothed level changes, click-free voice stealing, sustain pedal, phase-matched SQUEEZE bands, band-limited sync).
constexpr int kEngVer = 2;
static const juce::StringArray kQualities { "2x", "4x", "8x" };
static const juce::StringArray kRenderQualities { "Same as live", "4x", "8x" };
static const juce::StringArray kSubEnvs { "Ghost 1", "Longest Ghost", "Own" };
static const juce::StringArray kVintageModes { "Off", "8-bit Home Computer", "12-bit Classic Sampler", "Early-90s Lo-Fi" };   // = vc::kModeNames

// knob read-outs with units (what the popup and the type-in box show), and the way back from typed text
inline juce::String paramUnitKind(const juce::String& id) {
    auto ends = [&](const char* x) { return id.endsWith(x); };
    if (id == "master") return "db";
    if (id == "mono_below") return "hzoff";
    if (id.contains("cut") || id == "mb_xlo" || id == "mb_xhi") return "hz";
    if (id == "glide" || id == "penv_dec" || id == "smp_grain" || ends("_a") || ends("_d") || ends("_r") || ends("_dly") || ends("_hold")) return "time";
    if (ends("_fine")) return "cents";
    if (id == "penv_amt" || id == "smp_pitch" || id == "cmb_tune" || id == "bend") return "st";
    if (id == "lfo_rate" || id == "lfo1_rate" || id == "lfo2_rate") return "rate";
    if (ends("_ratio")) return "ratio";
    if (id == "smp_dens") return "dens";
    if (id == "bub_pitch") return "st";
    if ((id.startsWith("mm") && ends("_amt")) || id == "wob_amt" || ends("_env")) return "bipct";
    return "pct";
}
inline juce::String formatParam(const juce::String& kind, float v) {
    if (kind == "db") return juce::String(v, 1) + " dB";
    if (kind == "hzoff" && v < 20.0f) return "Off";
    if (kind == "hz" || kind == "hzoff") return v < 1000.0f ? juce::String(juce::roundToInt(v)) + " Hz" : juce::String(v / 1000.0f, v < 10000.0f ? 2 : 1) + " kHz";
    if (kind == "time") return v < 1.0f ? juce::String(v * 1000.0f, v < 0.1f ? 1 : 0) + " ms" : juce::String(v, 2) + " s";
    if (kind == "cents") return (v > 0 ? "+" : "") + juce::String(juce::roundToInt(v)) + " ct";
    if (kind == "st") return (v > 0 ? "+" : "") + juce::String(v, 1) + " st";
    if (kind == "rate") return juce::String(v, 2) + " Hz";
    if (kind == "ratio") return "x" + juce::String(v, 2);
    if (kind == "dens") return juce::String(juce::roundToInt(v)) + " /s";
    if (kind == "bipct") return (v > 0.0005f ? "+" : "") + juce::String(juce::roundToInt(v * 100.0f)) + " %";
    return juce::String(juce::roundToInt(v * 100.0f)) + " %";
}
inline float parseParam(const juce::String& kind, const juce::String& text) {
    const auto t = text.trim().toLowerCase();
    float v = t.retainCharacters("0123456789.-+").getFloatValue();
    if (kind == "hzoff" && t.startsWith("off")) return 0.0f;
    if ((kind == "hz" || kind == "hzoff") && t.contains("k")) v *= 1000.0f;
    if (kind == "time") { if (t.contains("ms")) v *= 0.001f; else if (!t.contains("s") && v > 20.0f) v *= 0.001f; }   // a bare big number means ms
    if (kind == "pct" || kind == "bipct") { if (t.contains("%") || std::abs(v) > 1.0f) v *= 0.01f; }
    return v;
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
        const String kind = paramUnitKind(id);
        L.add(std::make_unique<AudioParameterFloat>(ParameterID { id, 1 }, name, r, def,
            AudioParameterFloatAttributes().withStringFromValueFunction([kind](float v, int) { return formatParam(kind, v); })
                                           .withValueFromStringFunction([kind](const String& t) { return parseParam(kind, t); })));
    };
    auto addOp = [&](int o) {
        const String n = "Ghost " + String(o + 1) + " ";
        NormalisableRange<float> ratio(0.25f, 16.0f); ratio.setSkewForCentre(2.0f);
        static const float defRatio[kOps] = { 1.0f, 2.0f, 1.0f, 3.0f, 0.5f, 4.0f };
        F(opId(o, "ratio"), n + "Ratio", ratio, defRatio[o]);
        F(opId(o, "fine"), n + "Fine", { -50.0f, 50.0f }, 0.0f);
        F(opId(o, "level"), n + "Out", { 0.0f, 1.0f }, o == 0 ? 0.8f : 0.0f);
        F(opId(o, "wave"), n + "Wave", { 0.0f, 3.0f }, 0.0f);
        F(opId(o, "fb"), n + "Feedback", { 0.0f, 1.0f }, 0.0f);
        F(opId(o, "a"), n + "Attack", timeRange(0.001f, 10.0f), 0.003f);
        F(opId(o, "d"), n + "Decay", timeRange(0.005f, 10.0f), 0.6f);
        F(opId(o, "s"), n + "Sustain", { 0.0f, 1.0f }, o == 0 ? 0.8f : 0.4f);
        F(opId(o, "r"), n + "Release", timeRange(0.005f, 10.0f), 0.35f);
        F(opId(o, "vel"), n + "Velocity", { 0.0f, 1.0f }, 0.5f);
    };
    for (int o = 0; o < kLegacyOps; ++o) addOp(o);
    for (int from = 0; from < kLegacyOps; ++from)
        for (int to = 0; to < kLegacyOps; ++to)
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

    // ---- v0.2: Sample Ghost
    L.add(std::make_unique<AudioParameterChoice>(ParameterID { "smp_mode", 1 }, "Sample Mode", kSampleModes, 0));
    F("smp_level", "Sample Out", { 0.0f, 1.0f }, 0.0f);
    F("smp_pitch", "Sample Pitch", { -24.0f, 24.0f }, 0.0f);
    L.add(std::make_unique<AudioParameterBool>(ParameterID { "smp_track", 1 }, "Sample Key Track", true));
    L.add(std::make_unique<AudioParameterBool>(ParameterID { "smp_loop", 1 }, "Sample Loop", false));
    L.add(std::make_unique<AudioParameterBool>(ParameterID { "smp_rev", 1 }, "Sample Reverse", false));
    L.add(std::make_unique<AudioParameterBool>(ParameterID { "smp_keep", 1 }, "Keep Tempo", false));
    L.add(std::make_unique<AudioParameterBool>(ParameterID { "lab_oneshot", 1 }, "Keys One-Shot", true));
    L.add(std::make_unique<AudioParameterBool>(ParameterID { "mute_ghosts", 1 }, "Mute Ghosts", false));
    L.add(std::make_unique<AudioParameterBool>(ParameterID { "mute_sample", 1 }, "Mute Sample", false));
    F("smp_start", "Sample Start", { 0.0f, 1.0f }, 0.0f);
    F("smp_len", "Sample Length", { 0.01f, 1.0f }, 1.0f);
    F("smp_pos", "Sample Position", { 0.0f, 1.0f }, 0.0f);
    F("smp_scan", "Sample Scan", { 0.0f, 1.0f }, 0.0f);
    F("smp_grain", "Grain Size", timeRange(0.01f, 0.5f), 0.08f);
    F("smp_spray", "Grain Spray", { 0.0f, 1.0f }, 0.1f);
    NormalisableRange<float> dens(2.0f, 120.0f); dens.setSkewForCentre(25.0f);
    F("smp_dens", "Grain Density", dens, 30.0f);
    L.add(std::make_unique<AudioParameterChoice>(ParameterID { "smp_slices", 1 }, "Slices", StringArray { "4", "8", "16", "32" }, 1));
    F("smp_fm", "Sample Haunts Ghost 1", { 0.0f, 1.0f }, 0.0f);
    F("smp_a", "Sample Attack", timeRange(0.001f, 10.0f), 0.002f);
    F("smp_d", "Sample Decay", timeRange(0.005f, 10.0f), 1.0f);
    F("smp_s", "Sample Sustain", { 0.0f, 1.0f }, 1.0f);
    F("smp_r", "Sample Release", timeRange(0.005f, 10.0f), 0.2f);
    // ---- talking (vowel) filter
    F("vow_amt", "Talk Amount", { 0.0f, 1.0f }, 0.0f);
    F("vow_pos", "Vowel", { 0.0f, 1.0f }, 0.0f);
    F("vow_mod", "Talk Move", { 0.0f, 1.0f }, 0.5f);
    F("vow_res", "Talk Reso", { 0.0f, 1.0f }, 0.6f);
    // ---- bubbles
    F("bub_amt", "Bubbles", { 0.0f, 1.0f }, 0.0f);
    L.add(std::make_unique<AudioParameterChoice>(ParameterID { "bub_div", 1 }, "Bubble Rate", kLfoDivs, 7));
    F("bub_pitch", "Bubble Pop", { 0.0f, 48.0f }, 24.0f);
    F("bub_tone", "Bubble Tone", { 0.0f, 1.0f }, 0.4f);
    // ---- rhythm sequencer
    L.add(std::make_unique<AudioParameterBool>(ParameterID { "seq_on", 1 }, "Rhythm On", false));
    L.add(std::make_unique<AudioParameterChoice>(ParameterID { "seq_div", 1 }, "Rhythm Rate", kLfoDivs, 7));
    L.add(std::make_unique<AudioParameterChoice>(ParameterID { "seq_dest", 1 }, "Rhythm Target", kSeqDests, 0));
    F("seq_amt", "Rhythm Amount", { 0.0f, 1.0f }, 1.0f);
    F("seq_glide", "Rhythm Smooth", { 0.0f, 1.0f }, 0.1f);
    L.add(std::make_unique<AudioParameterInt>(ParameterID { "seq_steps", 1 }, "Rhythm Steps", 2, 16, 16));
    for (int i = 0; i < 16; ++i) F("seq_" + String(i + 1), "Step " + String(i + 1), { 0.0f, 1.0f }, (i % 4 == 0) ? 1.0f : (i % 2 == 0 ? 0.6f : 0.25f));
    // ---- crunch
    F("cmb_amt", "Comb", { 0.0f, 1.0f }, 0.0f);
    F("cmb_fb", "Comb Feedback", { 0.0f, 0.97f }, 0.75f);
    F("cmb_tune", "Comb Tune", { -24.0f, 24.0f }, 0.0f);
    L.add(std::make_unique<AudioParameterBool>(ParameterID { "cmb_key", 1 }, "Comb Follows Key", true));
    F("sync_amt", "Ghost 1 Sync", { 0.0f, 1.0f }, 0.0f);
    F("ott_amt", "Squeeze", { 0.0f, 1.0f }, 0.0f);   // (id kept: old projects and automation still find it)
    // ---- play helpers
    L.add(std::make_unique<AudioParameterBool>(ParameterID { "scale_on", 1 }, "Scale Lock", false));
    L.add(std::make_unique<AudioParameterChoice>(ParameterID { "scale_root", 1 }, "Key", kNoteNames, 0));
    L.add(std::make_unique<AudioParameterChoice>(ParameterID { "scale_type", 1 }, "Scale", kScaleNames, 1));
    L.add(std::make_unique<AudioParameterChoice>(ParameterID { "chord", 1 }, "Chord", kChordNames, 0));
    L.add(std::make_unique<AudioParameterBool>(ParameterID { "arp_on", 1 }, "Arp", false));
    L.add(std::make_unique<AudioParameterChoice>(ParameterID { "arp_div", 1 }, "Arp Rate", kLfoDivs, 7));
    L.add(std::make_unique<AudioParameterChoice>(ParameterID { "arp_pat", 1 }, "Arp Pattern", StringArray { "Up", "Down", "Up-Down", "Random", "As played" }, 0));
    L.add(std::make_unique<AudioParameterInt>(ParameterID { "arp_oct", 1 }, "Arp Octaves", 1, 3, 1));
    F("arp_gate", "Arp Gate", { 0.1f, 1.0f }, 0.6f);

    F("glide", "Glide", timeRange(0.0f, 1.0f), 0.0f);
    L.add(std::make_unique<AudioParameterBool>(ParameterID { "mono", 1 }, "Mono", false));
    L.add(std::make_unique<AudioParameterInt>(ParameterID { "octave", 1 }, "Octave", -3, 3, 0));
    L.add(std::make_unique<AudioParameterInt>(ParameterID { "voices", 1 }, "Voices", 1, 16, 8));
    F("bend", "Bend Range", { 0.0f, 24.0f }, 2.0f);
    F("master", "Master", { -24.0f, 6.0f }, -6.0f);

    // ================= BASS update (everything below defaults to off / neutral: old sounds stay the same)
    auto B = [&](const String& id, const String& name, bool def) { L.add(std::make_unique<AudioParameterBool>(ParameterID { id, 1 }, name, def)); };
    auto C = [&](const String& id, const String& name, const StringArray& ch, int def) { L.add(std::make_unique<AudioParameterChoice>(ParameterID { id, 1 }, name, ch, def)); };
    for (int o = kLegacyOps; o < kOps; ++o) addOp(o);                 // ghosts 5 and 6
    for (int from = 0; from < kOps; ++from)                             // the new links (any ghost onto any other)
        for (int to = 0; to < kOps; ++to)
            if (from != to && (from >= kLegacyOps || to >= kLegacyOps))
                F(modId(from, to), "Ghost " + String(from + 1) + " > " + String(to + 1), { 0.0f, 1.0f }, 0.0f);
    for (int o = 0; o < kOps; ++o) {                                    // per-ghost: wave table, DAHDSR extras, filter-2 route
        const String n = "Ghost " + String(o + 1) + " ";
        C(opId(o, "wtype"), n + "Waveform", kWaveNames, 0);
        F(opId(o, "dly"), n + "Delay", timeRange(0.0f, 4.0f), 0.0f);
        F(opId(o, "hold"), n + "Hold", timeRange(0.0f, 4.0f), 0.0f);
        B(opId(o, "loop"), n + "Env Loop", false);
        B(opId(o, "f2"), n + "To Filter 2", false);
    }
    // NOISE ghost
    for (int to = 0; to < kOps; ++to) F(modId(kNoise, to), "Noise > " + String(to + 1), { 0.0f, 1.0f }, 0.0f);
    C("nz_type", "Noise Type", kNoiseTypes, 0);
    F("nz_level", "Noise Out", { 0.0f, 1.0f }, 0.0f);
    NormalisableRange<float> cut2(20.0f, 20000.0f); cut2.setSkewForCentre(1000.0f);
    F("nz_cut", "Noise Cutoff", cut2, 20000.0f);
    F("nz_res", "Noise Reso", { 0.0f, 1.0f }, 0.1f);
    C("nz_ftype", "Noise Filter", StringArray { "Low", "Band", "High" }, 0);
    F("nz_a", "Noise Attack", timeRange(0.001f, 10.0f), 0.001f);
    F("nz_d", "Noise Decay", timeRange(0.005f, 10.0f), 0.3f);
    F("nz_s", "Noise Sustain", { 0.0f, 1.0f }, 1.0f);
    F("nz_r", "Noise Release", timeRange(0.005f, 10.0f), 0.2f);
    B("nz_f2", "Noise To Filter 2", false);
    // two filters
    for (int f = 1; f <= 2; ++f) {
        const String p = "f" + String(f) + "_", n = "Filter " + String(f) + " ";
        B(p + "on", n + "On", false);
        C(p + "type", n + "Type", kFltTypes, f == 1 ? 1 : 8);
        F(p + "cut", n + "Cutoff", cut2, f == 1 ? 800.0f : 1000.0f);
        F(p + "res", n + "Reso", { 0.0f, 1.0f }, 0.3f);
        F(p + "drive", n + "Drive", { 0.0f, 1.0f }, 0.0f);
        F(p + "key", n + "Keytrack", { 0.0f, 1.0f }, 0.0f);
        F(p + "env", n + "Env", { -1.0f, 1.0f }, 0.0f);
    }
    C("flt_route", "Filter Routing", kFltRoutes, 0);
    // SUB + low-end mono
    B("sub_on", "Sub On", false);
    C("sub_wave", "Sub Wave", kSubWaves, 0);
    C("sub_oct", "Sub Octave", StringArray { "-1 oct", "-2 oct" }, 0);
    F("sub_level", "Sub Level", { 0.0f, 1.0f }, 0.7f);
    NormalisableRange<float> subCut(40.0f, 2000.0f); subCut.setSkewForCentre(250.0f);
    F("sub_cut", "Sub Low-pass", subCut, 400.0f);
    B("sub_clean", "Clean Sub", true);
    B("sub_solo", "Sub Solo", false);
    F("mono_below", "Low-End Mono", { 0.0f, 300.0f }, 0.0f);
    // DIST + MULTIBAND (before SQUEEZE)
    B("dist_on", "Dist On", false);
    C("dist_type", "Dist Type", kDistTypes, 0);
    F("dist_drive", "Dist Drive", { 0.0f, 1.0f }, 0.5f);
    F("dist_tone", "Dist Tone", { 0.0f, 1.0f }, 1.0f);
    F("dist_mix", "Dist Mix", { 0.0f, 1.0f }, 1.0f);
    B("mb_on", "Multiband On", false);
    F("mb_lo", "Low Band Drive", { 0.0f, 1.0f }, 0.0f);
    F("mb_mid", "Mid Band Drive", { 0.0f, 1.0f }, 0.5f);
    F("mb_hi", "High Band Drive", { 0.0f, 1.0f }, 0.4f);
    NormalisableRange<float> xlo(60.0f, 400.0f); xlo.setSkewForCentre(150.0f);
    NormalisableRange<float> xhi(800.0f, 6000.0f); xhi.setSkewForCentre(2500.0f);
    F("mb_xlo", "Low Crossover", xlo, 150.0f);
    F("mb_xhi", "High Crossover", xhi, 2500.0f);
    F("mb_mix", "Multiband Mix", { 0.0f, 1.0f }, 1.0f);
    // WOBBLE: a drawn, tempo-synced shape (up to 8 points)
    C("wob_div", "Wobble Rate", kWobDivs, 9);
    B("wob_retrig", "Wobble Retrigger", false);
    F("wob_smooth", "Wobble Smooth", { 0.0f, 1.0f }, 1.0f);
    L.add(std::make_unique<AudioParameterInt>(ParameterID { "wob_n", 1 }, "Wobble Points", 2, kWobPts, 2));
    for (int i = 0; i < kWobPts; ++i) {
        F("wob_x" + String(i + 1), "Wobble Point " + String(i + 1) + " Time", { 0.0f, 1.0f }, i == 0 ? 0.0f : (i == 1 ? 0.5f : 0.5f + (float) (i - 1) * 0.07f));
        F("wob_y" + String(i + 1), "Wobble Point " + String(i + 1) + " Level", { 0.0f, 1.0f }, i % 2 == 0 ? 1.0f : 0.0f);
    }
    // two more LFOs
    NormalisableRange<float> lr2(0.02f, 30.0f); lr2.setSkewForCentre(3.0f);
    for (int l = 1; l <= 2; ++l) {
        const String p = "lfo" + String(l) + "_", n = "LFO " + String(l) + " ";
        F(p + "rate", n + "Rate", lr2, l == 1 ? 1.0f : 0.25f);
        B(p + "sync", n + "Sync", false);
        C(p + "div", n + "Division", kWobDivs, 6);
        C(p + "shape", n + "Shape", kLfoShapes, 0);
    }
    // MOD MATRIX
    for (int m = 1; m <= kModSlots; ++m) {
        const String p = "mm" + String(m) + "_", n = "Mod " + String(m) + " ";
        C(p + "src", n + "Source", kModSrcs, 0);
        C(p + "dst", n + "Target", kModDsts, 0);
        F(p + "amt", n + "Amount", { -1.0f, 1.0f }, 0.0f);
    }
    // ================= BASS update, review round (all default to the old behaviour)
    // the WOBBLE page's own target + depth (a 9th, dedicated mod slot with the wobble as its source)
    C("wob_dst", "Wobble Target", kModDsts, 0);
    F("wob_amt", "Wobble Depth", { -1.0f, 1.0f }, 0.0f);
    B("mono_retrig", "Mono Retrigger", false);
    C("sub_env", "Sub Follows", kSubEnvs, 0);
    F("sub_a", "Sub Attack", timeRange(0.001f, 2.0f), 0.002f);
    F("sub_r", "Sub Release", timeRange(0.005f, 4.0f), 0.12f);
    // engine revision (per patch) and oversampling (per instance, never automated)
    L.add(std::make_unique<AudioParameterInt>(ParameterID { "eng_ver", 1 }, "Engine Revision", 0, kEngVer, 0, AudioParameterIntAttributes().withAutomatable(false)));
    L.add(std::make_unique<AudioParameterChoice>(ParameterID { "quality", 1 }, "Quality", kQualities, 0, AudioParameterChoiceAttributes().withAutomatable(false)));
    L.add(std::make_unique<AudioParameterChoice>(ParameterID { "render_q", 1 }, "Render Quality", kRenderQualities, 0, AudioParameterChoiceAttributes().withAutomatable(false)));
    // VINTAGE character of the Sample Ghost (Shared/VintageCharacter.h); off by default, so old projects stay identical
    L.add(std::make_unique<AudioParameterChoice>(ParameterID { "smp_vc", 1 }, "Sample Character", kVintageModes, 0, AudioParameterChoiceAttributes().withAutomatable(false)));
    F("smp_vc_amt", "Sample Character Amount", { 0.0f, 1.0f }, 1.0f);
    F("smp_vc_drive", "Sample Character Drive", { 0.0f, 1.0f }, 0.0f);
    // the Sample Ghost's LOOP region (WaveRegion on the SAMPLE page), fractions of the sample; 0..1 = loop the whole START..END (as before)
    F("smp_lst", "Sample Loop Start", { 0.0f, 1.0f }, 0.0f);
    F("smp_lend", "Sample Loop End", { 0.0f, 1.0f }, 1.0f);
    // MORPH BOARD (Morph.h): off by default, so every old project / preset plays exactly as before; X / Y = the puck (automatable)
    L.add(std::make_unique<AudioParameterBool>(ParameterID { "morph_on", 1 }, "Morph On", false));
    F("morph_x", "Morph X", { 0.0f, 1.0f }, 0.5f);
    F("morph_y", "Morph Y", { 0.0f, 1.0f }, 0.5f);
    // ADD SAMPLE (the FM page's sample slot): which ghost the sample HAUNTS (was always ghost 1) and which ghost plays one cycle of
    // the sample as its waveform. The defaults (Ghost 1 / Off) are the old behaviour, so every old project and preset plays as before.
    C("smp_fm_to", "Sample Haunts Ghost", kSmpGhosts, 0);
    C("smp_wave_to", "Sample As Wave Of", kSmpWaveTo, 0);
    // BYPASS (Shared/Bypass.h): always LAST, so every existing parameter keeps its index; never part of a sound
    L.add(std::make_unique<AudioParameterBool>(ParameterID { "bypass", 1 }, "Bypass", false));
    return L;
}

struct OpSnap { float ratio, fine, level, wave, fb, a, d, s, r, vel; int wtype = 0; float dly = 0, hold = 0; bool loop = false, f2 = false; };
struct FltSnap { bool on = false; int type = 0; float cut = 1000, res = 0, drive = 0, key = 0, env = 0; };
struct LfoSnap { float rate = 1; bool sync = false; int div = 6, shape = 0; };
struct ModSlot { int src = 0, dst = 0; float amt = 0; };
struct Snap {
    OpSnap op[kOps];
    float mod[kOps][kOps] {};   // mod[from][to]
    float penvAmt, penvDec;
    float cut, res; int ftype; float fenv, fa, fd, fs, fr, fkey;
    float lfoRate; bool lfoSync; int lfoDiv, lfoShape;
    float macro[6];
    float glide; bool mono; int octave, voices; float bend, masterDb;
    // v0.2
    int smpMode; float smpLevel, smpPitch; bool smpTrack, smpLoop, smpRev; float smpStart, smpLen, smpPos, smpScan, smpGrain, smpSpray, smpDens; int smpSlices;
    float smpFm, smpA, smpD, smpS, smpR;
    float vowAmt, vowPos, vowMod, vowRes;
    float bubAmt; int bubDiv; float bubPitch, bubTone;
    bool seqOn; int seqDiv, seqDest; float seqAmt, seqGlide; int seqSteps; float seq[16];
    float cmbAmt, cmbFb, cmbTune; bool cmbKey; float syncAmt, ottAmt;
    bool scaleOn; int scaleRoot, scaleType, chord; bool arpOn; int arpDiv, arpPat, arpOct; float arpGate;
    bool smpKeep, labOneShot, muteGhosts = false, muteSample = false;
    // BASS update
    float nzMod[kOps] {}; int nzType = 0; float nzLevel = 0, nzCut = 20000, nzRes = 0.1f; int nzFtype = 0; float nzA = 0.001f, nzD = 0.3f, nzS = 1, nzR = 0.2f; bool nzF2 = false;
    FltSnap flt[2]; int fltRoute = 0;
    bool subOn = false; int subWave = 0, subOct = 0; float subLevel = 0.7f, subCut = 400; bool subClean = true, subSolo = false; float monoBelow = 0;
    bool distOn = false; int distType = 0; float distDrive = 0.5f, distTone = 1, distMix = 1;
    bool mbOn = false; float mbLo = 0, mbMid = 0.5f, mbHi = 0.4f, mbXlo = 150, mbXhi = 2500, mbMix = 1;
    int wobDiv = 9; bool wobRetrig = false; float wobSmooth = 1; int wobN = 2; float wobX[kWobPts] {}, wobY[kWobPts] {};
    LfoSnap lfo[2]; ModSlot mm[kModSlots + 1];   // [kModSlots] = the WOBBLE page's own slot (source: wobble)
    float smpLst = 0, smpLend = 1;
    bool monoRetrig = false; int subEnv = 0; float subA = 0.002f, subR = 0.12f; int engVer = 0;
    int smpFmTo = 0, smpWaveTo = 0;   // ADD SAMPLE: the ghost the sample haunts (0 = ghost 1, as before); the ghost that plays it as its wave (0 = none)
};

// Reads every parameter as a raw float pointer once, then snapshots quickly on the audio thread.
struct ParamReader {
    static constexpr int kOpFields = 15;
    struct OpPtrs { std::atomic<float>* p[kOpFields]; };
    OpPtrs op[kOps];
    std::atomic<float>* mod[kOps][kOps] {};
    std::atomic<float>* g[128] {};
    juce::StringArray gNames;
    // BASS update parameters: (pointer, kind, byte offset into Snap)
    struct Bind { std::atomic<float>* p; int kind; std::ptrdiff_t off; };   // kind 0 float, 1 int, 2 bool
    std::vector<Bind> binds;
    void attach(juce::AudioProcessorValueTreeState& s) { attachWith([&s](const juce::String& id) { return s.getRawParameterValue(id); }); }
    // any source of named atomics (used to play a saved patch without a plugin around it)
    template <class Get> void attachWith(Get get) {
        const char* opw[] = { "ratio", "fine", "level", "wave", "fb", "a", "d", "s", "r", "vel", "wtype", "dly", "hold", "loop", "f2" };
        for (int o = 0; o < kOps; ++o)
            for (int k = 0; k < kOpFields; ++k) op[o].p[k] = get(opId(o, opw[k]));
        for (int f = 0; f < kOps; ++f)
            for (int t = 0; t < kOps; ++t) mod[f][t] = f == t ? nullptr : get(modId(f, t));
        gNames = { "penv_amt", "penv_dec", "flt_cut", "flt_res", "flt_type", "flt_env", "flt_a", "flt_d", "flt_s", "flt_r", "flt_key",
                   "lfo_rate", "lfo_sync", "lfo_div", "lfo_shape", "glide", "mono", "octave", "voices", "bend", "master" };
        for (auto& m : kMacroIds) gNames.add("macro_" + m);
        for (auto* id : { "smp_mode", "smp_level", "smp_pitch", "smp_track", "smp_loop", "smp_rev", "smp_start", "smp_len", "smp_pos", "smp_scan", "smp_grain", "smp_spray",
                          "smp_dens", "smp_slices", "smp_fm", "smp_a", "smp_d", "smp_s", "smp_r", "vow_amt", "vow_pos", "vow_mod", "vow_res", "bub_amt", "bub_div", "bub_pitch",
                          "bub_tone", "seq_on", "seq_div", "seq_dest", "seq_amt", "seq_glide", "seq_steps", "cmb_amt", "cmb_fb", "cmb_tune", "cmb_key", "sync_amt", "ott_amt",
                          "scale_on", "scale_root", "scale_type", "chord", "arp_on", "arp_div", "arp_pat", "arp_oct", "arp_gate" }) gNames.add(id);
        for (int i = 0; i < 16; ++i) gNames.add("seq_" + juce::String(i + 1));
        gNames.add("smp_keep"); gNames.add("lab_oneshot"); gNames.add("mute_ghosts"); gNames.add("mute_sample");
        for (int i = 0; i < gNames.size(); ++i) g[i] = get(gNames[i]);
        // BASS update
        binds.clear();
        Snap d {}; auto* base = reinterpret_cast<const char*>(&d);
        auto bf = [&](const juce::String& id, const float& m) { binds.push_back({ get(id), 0, reinterpret_cast<const char*>(&m) - base }); };
        auto bi = [&](const juce::String& id, const int& m) { binds.push_back({ get(id), 1, reinterpret_cast<const char*>(&m) - base }); };
        auto bb = [&](const juce::String& id, const bool& m) { binds.push_back({ get(id), 2, reinterpret_cast<const char*>(&m) - base }); };
        for (int t = 0; t < kOps; ++t) bf(modId(kNoise, t), d.nzMod[t]);
        bi("nz_type", d.nzType); bf("nz_level", d.nzLevel); bf("nz_cut", d.nzCut); bf("nz_res", d.nzRes); bi("nz_ftype", d.nzFtype);
        bf("nz_a", d.nzA); bf("nz_d", d.nzD); bf("nz_s", d.nzS); bf("nz_r", d.nzR); bb("nz_f2", d.nzF2);
        for (int f = 0; f < 2; ++f) {
            const juce::String p = "f" + juce::String(f + 1) + "_"; auto& F = d.flt[f];
            bb(p + "on", F.on); bi(p + "type", F.type); bf(p + "cut", F.cut); bf(p + "res", F.res); bf(p + "drive", F.drive); bf(p + "key", F.key); bf(p + "env", F.env);
        }
        bi("flt_route", d.fltRoute);
        bb("sub_on", d.subOn); bi("sub_wave", d.subWave); bi("sub_oct", d.subOct); bf("sub_level", d.subLevel); bf("sub_cut", d.subCut);
        bb("sub_clean", d.subClean); bb("sub_solo", d.subSolo); bf("mono_below", d.monoBelow);
        bb("dist_on", d.distOn); bi("dist_type", d.distType); bf("dist_drive", d.distDrive); bf("dist_tone", d.distTone); bf("dist_mix", d.distMix);
        bb("mb_on", d.mbOn); bf("mb_lo", d.mbLo); bf("mb_mid", d.mbMid); bf("mb_hi", d.mbHi); bf("mb_xlo", d.mbXlo); bf("mb_xhi", d.mbXhi); bf("mb_mix", d.mbMix);
        bi("wob_div", d.wobDiv); bb("wob_retrig", d.wobRetrig); bf("wob_smooth", d.wobSmooth); bi("wob_n", d.wobN);
        for (int i = 0; i < kWobPts; ++i) { bf("wob_x" + juce::String(i + 1), d.wobX[i]); bf("wob_y" + juce::String(i + 1), d.wobY[i]); }
        for (int l = 0; l < 2; ++l) { const juce::String p = "lfo" + juce::String(l + 1) + "_"; bf(p + "rate", d.lfo[l].rate); bb(p + "sync", d.lfo[l].sync); bi(p + "div", d.lfo[l].div); bi(p + "shape", d.lfo[l].shape); }
        for (int m = 0; m < kModSlots; ++m) { const juce::String p = "mm" + juce::String(m + 1) + "_"; bi(p + "src", d.mm[m].src); bi(p + "dst", d.mm[m].dst); bf(p + "amt", d.mm[m].amt); }
        bi("wob_dst", d.mm[kModSlots].dst); bf("wob_amt", d.mm[kModSlots].amt);
        bf("smp_lst", d.smpLst); bf("smp_lend", d.smpLend);
        bb("mono_retrig", d.monoRetrig); bi("sub_env", d.subEnv); bf("sub_a", d.subA); bf("sub_r", d.subR); bi("eng_ver", d.engVer);
        bi("smp_fm_to", d.smpFmTo); bi("smp_wave_to", d.smpWaveTo);
    }
    float v(int i) const { return g[i]->load(); }
    void read(Snap& n) const {
        for (int o = 0; o < kOps; ++o) {
            auto& P = op[o].p;
            n.op[o] = { P[0]->load(), P[1]->load(), P[2]->load(), P[3]->load(), P[4]->load(), P[5]->load(), P[6]->load(), P[7]->load(), P[8]->load(), P[9]->load(),
                        (int) P[10]->load(), P[11]->load(), P[12]->load(), P[13]->load() > 0.5f, P[14]->load() > 0.5f };
        }
        for (int f = 0; f < kOps; ++f)
            for (int t = 0; t < kOps; ++t) n.mod[f][t] = mod[f][t] ? mod[f][t]->load() : 0.0f;
        n.penvAmt = v(0); n.penvDec = v(1); n.cut = v(2); n.res = v(3); n.ftype = (int) v(4); n.fenv = v(5);
        n.fa = v(6); n.fd = v(7); n.fs = v(8); n.fr = v(9); n.fkey = v(10);
        n.lfoRate = v(11); n.lfoSync = v(12) > 0.5f; n.lfoDiv = (int) v(13); n.lfoShape = (int) v(14);
        n.glide = v(15); n.mono = v(16) > 0.5f; n.octave = (int) v(17); n.voices = (int) v(18); n.bend = v(19); n.masterDb = v(20);
        for (int m = 0; m < 6; ++m) n.macro[m] = v(21 + m);
        int k = 27;
        auto B = [&] { return v(k++) > 0.5f; }; auto I = [&] { return (int) v(k++); }; auto Fv = [&] { return v(k++); };
        n.smpMode = I(); n.smpLevel = Fv(); n.smpPitch = Fv(); n.smpTrack = B(); n.smpLoop = B(); n.smpRev = B(); n.smpStart = Fv(); n.smpLen = Fv(); n.smpPos = Fv();
        n.smpScan = Fv(); n.smpGrain = Fv(); n.smpSpray = Fv(); n.smpDens = Fv(); n.smpSlices = 4 << I(); n.smpFm = Fv(); n.smpA = Fv(); n.smpD = Fv(); n.smpS = Fv(); n.smpR = Fv();
        n.vowAmt = Fv(); n.vowPos = Fv(); n.vowMod = Fv(); n.vowRes = Fv(); n.bubAmt = Fv(); n.bubDiv = I(); n.bubPitch = Fv(); n.bubTone = Fv();
        n.seqOn = B(); n.seqDiv = I(); n.seqDest = I(); n.seqAmt = Fv(); n.seqGlide = Fv(); n.seqSteps = I(); n.cmbAmt = Fv(); n.cmbFb = Fv(); n.cmbTune = Fv(); n.cmbKey = B();
        n.syncAmt = Fv(); n.ottAmt = Fv(); n.scaleOn = B(); n.scaleRoot = I(); n.scaleType = I(); n.chord = I(); n.arpOn = B(); n.arpDiv = I(); n.arpPat = I(); n.arpOct = I(); n.arpGate = Fv();
        for (int i = 0; i < 16; ++i) n.seq[i] = Fv();
        n.smpKeep = B(); n.labOneShot = B(); n.muteGhosts = B(); n.muteSample = B();
        auto* base = reinterpret_cast<char*>(&n);
        for (auto& b : binds) {
            const float x = b.p->load();
            if (b.kind == 0) *reinterpret_cast<float*>(base + b.off) = x;
            else if (b.kind == 1) *reinterpret_cast<int*>(base + b.off) = (int) x;
            else *reinterpret_cast<bool*>(base + b.off) = x > 0.5f;
        }
        n.mm[kModSlots].src = 1;   // the WOBBLE page's slot always listens to the wobble
    }
};

} // namespace gs
