// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Catalog.h"
#include "../dsp/Modulators.h"
#include "ColourTape.h"
#include "ColourEq.h"
#include "SpaceCloud.h"
#include "SpaceDoor.h"
#include "SpaceMod.h"
#include "SpaceSpectral.h"
#include "SpaceGrain.h"
#include "SpaceHalls.h"
#include "../rhythm/RhythmGate.h"
#include "../rhythm/RhythmGrid.h"
#include "../rhythm/RhythmHaunt.h"
#include "../rhythm/RhythmWarp.h"
#include "../master/Master.h"
#include "../engines/Engines.h"

namespace phantom
{
const UnitDef& unitDef (int u)
{
    static const UnitDef k[kNumUnits] {
        { "gate",    "GHOST GATE",     "GATE",    "16 drawable volume + time patterns per bar",        gRhythm, 0xff3CFF8F, 3,  true, 1.0f },
        { "grid",    "GHOST GRID",     "GRID",    "paint effects onto a 1-bar step grid",              gRhythm, 0xff45E6FF, 7,  true, 1.0f },
        { "haunt",   "HAUNT EDIT",     "HAUNT",   "hold a pad for tempo-synced stutter gestures",      gRhythm, 0xffFF4FD8, 11, true, 1.0f },
        { "warp",    "WARP",           "WARP",    "draw a tempo-synced pitch bend: PITCH keeps time, TAPE bends time too", gRhythm, 0xffC6FF4F, 15, true, 1.0f },
        { "crackle", "CRACKLE",        "CRACKLE", "multiband destroyer: 14 curves, speaker boxes",     gColour, 0xffFF6A3D, 6,  true, 1.0f },
        { "wreck",   "WRECKER",        "WRECK",   "analog saturation from warm to slammed",           gColour, 0xffFFC35A, 5,  true, 1.0f },
        { "tube",    "TUBE GLOW",      "TUBE",    "vintage tube console warmth",                       gColour, 0xffFF9E5A, 9,  true, 1.0f },
        { "crush",   "SOUL CRUSH",     "CRUSH",   "level-smashing grit compressor",                    gColour, 0xffFF5A6E, 13, true, 0.6f },
        { "retro",   "RETRO",          "RETRO",   "vintage colour: noise, wobble, dirt, dust and age", gColour, 0xffE8C07A, 8,  true, 1.0f },
        { "fatten",  "FATTEN",         "FATTEN",  "one big knob: turn it and it gets fatter",          gColour, 0xffFF8FB1, 4,  true, 1.0f },
        { "basslab", "BASS LAB",       "BASS",    "low / mid / high bass tool: mono sub, growl, edge, TRANSLATE for small speakers", gColour, 0xffFF7A3D, 12, true, 1.0f },
        { "formant", "GHOST FORMANT",  "FORMANT", "talking vowel filter: A-E-I-O-U, mouth size, drawn or synced movement", gColour, 0xffFF5AC8, 14, true, 1.0f },
        { "eq",      "SPIRIT EQ",      "EQ",      "precision dynamic EQ over a live analyser",         gColour, 0xff45E6FF, 1,  true, 1.0f },
        { "filter",  "PHANTOM FILTER", "FILTER",  "resonant analog multimode filter with rhythm LFO",  gColour, 0xffA06BFF, 17, true, 1.0f },
        { "chorus",  "CHORUS",         "CHORUS",  "multi-voice ghost chorus and ensemble",             gSpace,  0xff7FD8FF, 19, true, 0.5f },
        { "crystal", "CRYSTAL",        "CRYSTAL", "reverse + pitched granular splices",               gSpace,  0xffB8F4FF, 21, true, 0.4f },
        { "echo",    "ECTO ECHO",      "ECHO",    "tape / analog / BBD / digital character delay",    gSpace,  0xff3CFF8F, 23, true, 0.3f },
        { "space",   "SPECTRAL SPACE", "SPACE",   "blendable ambience with glimmering tails",          gSpace,  0xff9C8BFF, 25, true, 0.3f },
        { "shimmer", "SHIMMER",        "SHIMMER", "lush octave-shimmer reverb with freeze",            gSpace,  0xffE6B8FF, 27, true, 0.35f },
        { "halls",   "HAUNTED HALLS",  "HALLS",   "hall, room, plate, chamber and more, from tight to endless", gSpace, 0xff8FA8FF, 2, true, 0.3f },
        { "spectra", "GHOST SPECTRA",  "SPECTRA", "spectral freeze, smear, scatter and shift; GHOST keeps the hits", gSpace, 0xff6BFFE6, 16, true, 0.7f },
        { "portal",  "DOORWAY",        "DOOR",    "morphing spaces: shapes, early reflections, movement, transform, freeze", gSpace, 0xffB06BFF, 18, true, 0.35f },
        { "ott",     "GHOST SQUEEZE",  "SQUEEZE", "3-band upward + downward compression",              gMaster, 0xffFF4FD8, 29, true, 1.0f },
        { "lim",     "GHOST LIMITER",  "LIMIT",   "transparent true-peak lookahead limiter, no overs", gMaster, 0xffF2F4F5, 31, false, 1.0f },
    };
    return k[juce::jlimit (0, kNumUnits - 1, u)];
}
const char* groupName (int g) { static const char* n[] { "RHYTHM", "COLOUR", "SPACE", "MASTER" }; return n[juce::jlimit (0, 3, g)]; }

namespace
{
    using P = ParamSpec; using V = std::vector<std::string>;
    V divs() { return divisionNames(); }
    V lengths() { return { "1/2 BAR", "1 BAR", "2 BARS", "4 BARS" }; }
    P dB (const char* id, const char* name, float lo, float hi, float def = 0.0f) { return P::range (id, name, lo, hi, def, "dB"); }
    P hz (const char* id, const char* name, float lo, float hi, float def, float centre) { return P::range (id, name, lo, hi, def, "Hz", centre); }
    P ms (const char* id, const char* name, float lo, float hi, float def, float centre = 0.0f) { return P::range (id, name, lo, hi, def, "ms", centre); }
    P bipolar (const char* id, const char* name, float def = 0.0f) { return P::range (id, name, -1.0f, 1.0f, def, ""); }
}

std::vector<ParamSpec> unitParams (int u, bool withEngineMenu)
{
    std::vector<P> v;
    switch (u)
    {
        case uGate:
            v = { P::integer ("pattern", "Pattern", 1, 16, 1), P::choice ("length", "Length", { "1/2 BAR", "1 BAR", "2 BARS" }, 1), P::pct ("smooth", "Smooth", 0.2f), P::pct ("time", "Time Amount", 1.0f),
                  P::choice ("pmode", "Pump Mode", { "OFF", "4 ON FLOOR", "HALF-TIME", "FAST PULSE", "DRAWN", "SIDECHAIN" }, 0), P::pct ("pdepth", "Pump Depth", 0.7f), P::pct ("pshape", "Pump Shape", 0.5f),
                  P::choice ("plen", "Pump Length", { "1/2 BEAT", "1 BEAT", "2 BEATS", "1 BAR" }, 1), ms ("prel", "Pump Release", 10, 1000, 180, 180), ms ("patk", "Pump Attack", 0.5f, 50, 4, 4), ms ("phold", "Pump Hold", 0, 200, 0),
                  dB ("pthr", "Sidechain Threshold", -60, 0, -24), P::toggle ("ponly", "Pump Only", true), P::toggle ("plook", "Sidechain Lookahead", false), P::pct ("swing", "Swing", 0.0f),
                  P::choice ("trig", "Trigger", { "HOST SYNC", "AUDIO" }, 0), dB ("tthr", "Trigger Level", -60, 0, -30), P::pct ("tsens", "Trigger Sense", 0.5f) }; break;
        case uGrid:
            v = { P::integer ("pattern", "Pattern", 1, 8, 1), P::choice ("steps", "Steps", { "16 STEPS", "32 STEPS" }, 0), P::pct ("loop", "Loop Length", 0.35f), P::pct ("stutter", "Stutter Rate", 0.5f),
                  P::pct ("scratch", "Scratch Depth", 0.5f), P::pct ("reverse", "Reverse Amount", 1.0f), P::pct ("tape", "Tape Stop Speed", 0.4f), P::pct ("filter", "Filter Depth", 0.6f), P::pct ("crush", "Crush Amount", 0.55f),
                  P::pct ("delay", "Delay Feedback", 0.5f), P::pct ("verb", "Verb Size", 0.6f), P::pct ("tonal", "Tonal Pitch", 0.5f), P::integer ("seed", "Seed", 0, 999, 0),
                  P::pct ("gate", "Gate Rate", 0.5f), P::pct ("freeze", "Freeze Mix", 1.0f) }; break;
        case uHaunt:
            v = { P::integer ("pad", "Pad", 0, 16, 0), P::toggle ("sync", "Sync Start", true), P::toggle ("latch", "Latch", false), P::pct ("motion", "Motion", 0.5f),
                  P::choice ("ptype", "Pad Type", { "STUTTER", "BUILD", "GATE", "GLITCH", "REVERSE", "TAPE STOP", "LP SWEEP", "HP SWEEP", "CRUSH", "PITCH DROP" }, 0),
                  P::choice ("psize", "Pad Size", { "1/64", "1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4T", "1/4", "1/2", "1 BAR", "2 BARS" }, 7),
                  // FOLLOW GHOST BREAK (0.1.26): the stutter moves this audio by the NUDGE of one of Ghost Break's bays (rhythm/NudgeLink.h)
                  P::toggle ("follow", "Follow Ghost Break", false), P::choice ("fbay", "Follow Bay", { "BAY A", "BAY B", "BAY C", "BAY D" }, 0) }; break;
        case uWarp:
            v = { P::choice ("mode", "Mode", { "PITCH", "TAPE" }, 0), P::pct ("amount", "Amount", 1.0f), P::choice ("length", "Length", lengths(), 1),
                  P::choice ("range", "Range", { "+-2 ST", "+-7 ST", "+-12 ST", "+-24 ST" }, 2), P::toggle ("formant", "Keep Formants", false) }; break;
        case uCrackle:
        {
            v = { P::choice ("bands", "Bands", { "1 BAND", "2 BANDS", "3 BANDS" }, 0), hz ("xlo", "Low Crossover", 40, 2000, 220, 250), hz ("xhi", "High Crossover", 400, 12000, 2800, 2500) };
            const V curves { "TUBE", "TAPE", "DIODE", "HARD CLIP", "FUZZ", "RECTIFY", "HALF RECT", "FOLD", "SINE FOLD", "BITCRUSH", "DOWNSAMPLE", "BROKEN SPEAKER", "CRACKED", "DYING BATTERY" };
            const int defCurve[3] { 0, 4, 12 };
            for (int k = 1; k <= 3; ++k)
            {
                const std::string pre = "b" + std::to_string (k) + "_", nm = "Band " + std::to_string (k) + " ";
                v.push_back (P::choice (pre + "curve", nm + "Curve", curves, defCurve[k - 1])); v.push_back (P::pct (pre + "drive", nm + "Drive", 0.4f));
                v.push_back (P::pct (pre + "mix", nm + "Mix", 1.0f)); v.push_back (P::range (pre + "level", nm + "Level", -24, 12, 0, "dB"));
            }
            v.push_back (hz ("prehp", "Pre Low Cut", 10, 2000, 10, 150)); v.push_back (hz ("prelp", "Pre High Cut", 1000, 22000, 22000, 6000));
            v.push_back (hz ("posthp", "Post Low Cut", 10, 2000, 10, 150)); v.push_back (hz ("postlp", "Post High Cut", 1000, 22000, 22000, 6000));
            v.push_back (P::choice ("box", "Box", { "OFF", "SMALL SPEAKER", "4x12 CAB", "TELEPHONE", "AM RADIO", "TIN CAN", "CARDBOARD BOX", "MEGAPHONE", "LAPTOP", "WALKIE-TALKIE" }, 0));
            v.push_back (P::pct ("boxmix", "Box Mix", 1.0f)); v.push_back (P::toggle ("auto", "Auto Gain", true)); v.push_back (dB ("out", "Output", -24, 12)); break;
        }
        case uWreck:
            v = { P::choice ("style", "Style", { "TAPE", "TUBE", "CONSOLE", "TRANSISTOR", "PENTODE", "FUZZ", "FOLD", "CRUSH", "DECIMATE" }, 0), P::pct ("drive", "Drive", 0.3f), bipolar ("tone", "Tone"),
                  hz ("lowcut", "Low Cut", 10, 1000, 10, 90), hz ("highcut", "High Cut", 1000, 22000, 22000, 6000), P::toggle ("thump", "Thump", false), P::toggle ("steep", "Steep", false), P::toggle ("punish", "Punish", false),
                  bipolar ("bias", "Bias"), P::pct ("sag", "Sag", 0.0f), P::toggle ("auto", "Auto Gain", true), dB ("out", "Output", -24, 12) }; break;
        case uTube:
            v = { P::pct ("drive", "Drive", 0.35f), P::choice ("char", "Character", { "LINE", "MIC" }, 0), dB ("bass", "Bass", -12, 12), dB ("treble", "Treble", -12, 12), bipolar ("bias", "Bias"), P::pct ("air", "Air", 0.0f), dB ("out", "Output", -24, 12) }; break;
        case uCrush:
            v = { P::pct ("crush", "Crush", 0.5f), P::pct ("dark", "Darkness", 0.3f), ms ("release", "Release", 5, 1000, 120, 120), P::toggle ("prog", "Program Release", true), P::pct ("blend", "Parallel Blend", 1.0f), dB ("out", "Output", -24, 12) }; break;
        case uRetro:
            v = { P::pct ("magic", "Magic", 0.5f), P::toggle ("noise_on", "Noise On", true), P::pct ("noise", "Noise", 0.3f), P::choice ("ntype", "Noise Type", { "VINYL", "DUST", "TAPE HISS", "WHITE", "PINK", "HUM 50", "HUM 60", "RADIO" }, 0),
                  bipolar ("ntone", "Noise Tone"), P::toggle ("nfollow", "Noise Follows Input", false), P::toggle ("wobble_on", "Wobble On", true), P::pct ("wobble", "Wobble", 0.3f), P::pct ("flutter", "Flutter", 0.3f),
                  P::toggle ("dist_on", "Distort On", true), P::pct ("dist", "Distort", 0.3f), P::choice ("dtype", "Distort Type", { "TAPE", "TUBE", "CONSOLE", "TRANSISTOR", "FUZZ" }, 0),
                  P::toggle ("digi_on", "Digital On", false), P::pct ("digi", "Digital", 0.3f), P::toggle ("space_on", "Space On", false), P::pct ("space", "Space", 0.3f), P::toggle ("mag_on", "Magnetic On", false), P::pct ("mag", "Magnetic", 0.3f),
                  P::pct ("bias", "Tape Bias", 0.5f), P::choice ("speed", "Tape Speed", { "7.5 IPS", "15 IPS", "30 IPS" }, 1), P::pct ("degrade", "Degrade", 0.0f), bipolar ("tone", "Tone"), dB ("out", "Output", -24, 12) }; break;
        case uFatten:
            v = { P::pct ("fat", "Fat", 0.4f), P::choice ("mode", "Mode", { "CLASSIC", "SUB", "BODY", "SMASH", "SATURATE", "STACK", "TRANSIENT", "SAUSAGE" }, 0), bipolar ("color", "Color"), P::pct ("crunch", "Crunch", 0.2f),
                  dB ("drive", "Input", 0, 24), P::toggle ("punch", "Punch", false), P::toggle ("lowclean", "Low Clean", false), dB ("out", "Output", -12, 12) }; break;
        case uBassLab:
            v = { hz ("xlo", "Low Split", 40, 400, 120, 120), hz ("xhi", "High Split", 700, 8000, 2200, 2200), P::pct ("lowwidth", "Low Width", 1.0f), P::pct ("sub", "Sub", 0.0f), P::pct ("growl", "Growl", 0.0f),
                  hz ("gfreq", "Growl Frequency", 300, 1500, 750, 750), P::pct ("gmove", "Growl Move", 0.0f), P::choice ("gdiv", "Growl Sync", divs(), 6), P::pct ("edge", "Edge", 0.0f), P::pct ("translate", "Translate", 0.0f),
                  dB ("low", "Low Level", -18, 12), dB ("mid", "Mid Level", -18, 12), dB ("high", "High Level", -18, 12), P::pct ("lowsat", "Low Saturation", 0.0f), P::toggle ("auto", "Gain Compensation", true), dB ("out", "Output", -24, 12) }; break;
        case uFormant:
            v = { P::range ("vowel", "Vowel", 0, 4, 0, ""), P::pct ("mouth", "Mouth Size", 0.5f), P::pct ("reso", "Resonance", 0.5f), P::range ("shift", "Formant Shift", -12, 12, 0, "st"), P::pct ("drive", "Drive", 0.2f),
                  P::choice ("move", "Movement", { "STILL", "LFO", "DRAWN" }, 0), P::choice ("mdiv", "Movement Division", divs(), 9), P::pct ("mdepth", "Movement Depth", 0.5f), P::choice ("mlen", "Drawn Length", lengths(), 1),
                  P::pct ("body", "Body", 0.3f), P::choice ("mshape", "LFO Shape", { "SINE", "TRIANGLE", "SAW", "SQUARE", "S&H", "ENV" }, 0), P::pct ("offset", "Stereo Offset", 0.0f) }; break;
        case uEq:
        {
            const float freqs[24] { 40, 100, 250, 600, 1500, 3500, 8000, 14000, 60, 300, 2000, 10000, 30, 80, 160, 400, 800, 1200, 2500, 5000, 6500, 9000, 12000, 16000 };
            for (int k = 0; k < 24; ++k)
            {
                const std::string b = std::to_string (k + 1) + "_", n = "Band " + std::to_string (k + 1) + " ";
                v.push_back (P::toggle (b + "on", n + "On", false)); v.push_back (P::toggle (b + "byp", n + "Bypass", false));
                v.push_back (P::choice (b + "type", n + "Type", { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch", "Band Pass", "Tilt Shelf", "Flat Tilt" }, 0));
                v.push_back (P::choice (b + "slope", n + "Slope", { "6 dB", "12 dB", "18 dB", "24 dB", "36 dB", "48 dB", "72 dB", "96 dB" }, 1));
                v.push_back (hz ((b + "freq").c_str(), (n + "Freq").c_str(), 10, 30000, freqs[k], 1000)); v.push_back (P::range (b + "gain", n + "Gain", -30, 30, 0, "dB"));
                v.push_back (P::range (b + "q", n + "Q", 0.025f, 40, 1, "", 1.0f)); v.push_back (P::choice (b + "stereo", n + "Stereo", { "Stereo", "Left", "Right", "Mid", "Side" }, 0));
                v.push_back (P::toggle (b + "dyn", n + "Dynamic", false)); v.push_back (P::range (b + "thr", n + "Threshold", -80, 0, -24, "dB")); v.push_back (P::range (b + "range", n + "Range", -30, 30, -6, "dB"));
                v.push_back (P::range (b + "ratio", n + "Ratio", 1, 20, 2, ":1", 3.0f)); v.push_back (P::range (b + "att", n + "Attack", 0.1f, 200, 5, "ms", 10.0f)); v.push_back (P::range (b + "rel", n + "Release", 5, 2000, 120, "ms", 150.0f));
                v.push_back (P::toggle (b + "auto", n + "Auto Threshold", false)); v.push_back (P::choice (b + "detect", n + "Detect", { "Band", "Wide" }, 0)); v.push_back (P::toggle (b + "spec", n + "Spectral", false));
            }
            v.push_back (dB ("out", "Output", -24, 24)); v.push_back (P::choice ("mode", "Mode", { "Zero Latency", "Linear Low", "Linear Medium", "Linear High" }, 0));
            v.push_back (P::choice ("char", "Character", { "Clean", "Subtle", "Warm" }, 0)); v.push_back (P::range ("scale", "Gain Scale", -2, 2, 1, "x"));
            v.push_back (P::toggle ("autogain", "Auto Gain", false)); v.push_back (P::toggle ("invert", "Phase Invert", false)); break;
        }
        case uFilter:
            v = { P::choice ("type", "Type", { "LOW PASS", "BAND PASS", "HIGH PASS", "NOTCH", "LADDER LP", "LADDER HP", "LADDER BP", "OB LP", "OB HP", "OB BP", "OB NOTCH", "K35 LP", "K35 HP", "DIODE LP", "COMB +", "COMB -", "TRI-POLE", "VINTAGE LP", "VINTAGE HP", "VINTAGE BP", "CUTOFF WARP LP", "CUTOFF WARP HP", "CUTOFF WARP BP", "CUTOFF WARP N", "RES WARP LP", "RES WARP HP", "RES WARP BP", "RES WARP N", "ALLPASS", "SAMPLE & HOLD", "NOTCH 24",
                  "SAMPLER LP", "SAMPLER HP", "SAMPLER BP", "SAMPLER NOTCH", "ACID LP", "WERNER LP 24", "WERNER LP 12", "WERNER BP", "WERNER HP",
                  "RESONATOR LP", "RESONATOR BP", "RESONATOR HP", "RESONATOR NOTCH" }, 0),
                  P::choice ("poles", "Slope", { "12 dB", "24 dB" }, 1), P::choice ("routing", "Routing", { "SINGLE", "SERIES", "PARALLEL" }, 0), hz ("cutoff", "Cutoff", 20, 20000, 1200, 1000), P::pct ("reso", "Resonance", 0.4f),
                  P::range ("spread", "Spread", -4, 4, 1, "oct"), P::pct ("drive", "Drive", 0.2f), bipolar ("lfoamt", "LFO Amount"), P::choice ("lfoshape", "LFO Shape", lfoShapeNames(), 0), P::toggle ("lfosync", "LFO Sync", true),
                  P::choice ("lfodiv", "LFO Division", divs(), 9), P::range ("lforate", "LFO Rate", 0.05f, 20, 1, "Hz", 1.0f), bipolar ("envamt", "Env Amount"), ms ("envrel", "Env Release", 10, 1000, 200, 200), ms ("envatk", "Env Attack", 0.1f, 200, 5, 10),
                  P::toggle ("safe", "Self-Oscillation Safe", true) }; break;
        case uChorus:
            v = { P::range ("rate", "Rate", 0.05f, 5, 0.4f, "Hz", 0.6f), P::pct ("depth", "Depth", 0.5f), ms ("delay", "Delay", 2, 30, 12), P::pct ("fb", "Feedback", 0.0f), P::pct ("width", "Width", 1.0f),
                  P::integer ("voices", "Voices", 1, 8, 3), P::choice ("mode", "Mode", { "CHORUS", "ENSEMBLE" }, 0) }; break;
        case uCrystal:
            v = { P::range ("pitch", "Pitch", -24, 24, 12, "st"), ms ("splice", "Splice", 20, 600, 180, 150), ms ("delay", "Delay", 0, 1500, 250, 300), P::pct ("fb", "Feedback", 0.4f), P::pct ("reverse", "Reverse", 0.5f), P::pct ("spread", "Spread", 0.6f),
                  P::pct ("position", "Position", 0.0f), P::pct ("density", "Density", 0.5f), P::pct ("texture", "Texture", 0.5f), P::toggle ("freeze", "Freeze", false) }; break;
        case uEcho:
            v = { P::choice ("mode", "Mode", { "TAPE", "ANALOG", "BBD", "DIGITAL" }, 0), P::toggle ("sync", "Sync", true), ms ("time", "Time", 1, 2000, 375, 400), P::choice ("div", "Division", divs(), 7), P::pct ("fb", "Feedback", 0.45f),
                  P::toggle ("ping", "Ping-Pong", true), P::pct ("width", "Width", 1.0f), hz ("lowcut", "Low Cut", 20, 2000, 120, 200), hz ("highcut", "High Cut", 1000, 20000, 6500, 6000), P::pct ("char", "Character", 0.5f), P::pct ("duck", "Ducking", 0.0f),
                  P::integer ("nodes", "Nodes", 1, 8, 1), P::pct ("spread", "Node Spread", 0.0f), P::pct ("sat", "Node Saturation", 0.0f), P::pct ("pan", "Node Pan", 0.0f) }; break;
        case uSpace:
            v = { P::pct ("size", "Size", 0.5f), P::pct ("blend", "Blend", 0.6f), P::range ("decay", "Decay", 0.3f, 12, 2.5f, "s", 2.5f), bipolar ("tone", "Tone"), P::pct ("mod", "Modulation", 0.4f), P::pct ("glimmer", "Glimmer", 0.2f),
                  P::pct ("width", "Width", 1.0f), ms ("predelay", "Pre-delay", 0, 250, 20, 40), P::pct ("duck", "Ducking", 0.0f), P::pct ("damp", "Damping", 0.4f) }; break;
        case uShimmer:
            v = { P::pct ("size", "Size", 0.7f), P::range ("decay", "Decay", 0.5f, 30, 5, "s", 5.0f), P::pct ("damp", "Damping", 0.4f), P::pct ("shimmer", "Shimmer", 0.5f), ms ("predelay", "Pre-delay", 0, 250, 30, 40), P::pct ("mod", "Modulation", 0.5f),
                  P::pct ("width", "Width", 1.0f), P::toggle ("freeze", "Freeze", false), P::choice ("interval", "Shift", { "OCTAVE", "FIFTH", "OCTAVE + FIFTH", "OCTAVE DOWN" }, 0), P::toggle ("hold", "Infinite Hold", false) }; break;
        case uHalls:
            v = { P::choice ("mode", "Mode", { "ROOM", "HALL", "PLATE", "CHAMBER", "CATHEDRAL", "CLOUD", "GALAXY", "REVERSE", "GATED", "NONLINEAR", "DARK TAPE" }, 1), P::pct ("size", "Size", 0.6f), P::range ("decay", "Decay", 0.1f, 60, 3, "s", 3.0f),
                  ms ("predelay", "Pre-delay", 0, 500, 20, 60), P::pct ("diffusion", "Diffusion", 0.7f), P::pct ("damp", "Damping", 0.4f), hz ("lowcut", "Low Cut", 20, 1000, 80, 150), P::pct ("mod", "Modulation", 0.4f), P::pct ("width", "Width", 1.0f),
                  P::pct ("early", "Early", 0.5f), P::pct ("shimmer", "Shimmer", 0.0f), P::toggle ("freeze", "Freeze", false) }; break;
        case uSpectra:
            v = { P::toggle ("freeze", "Freeze", false), P::pct ("smear", "Smear", 0.3f), P::pct ("scatter", "Scatter", 0.0f), P::range ("shift", "Shift", -24, 24, 0, "st"), P::pct ("ghost", "Ghost", 0.5f), P::pct ("motion", "Motion", 0.0f),
                  P::choice ("mdiv", "Motion Division", divs(), 14), bipolar ("tilt", "Tilt"), P::toggle ("hq", "HQ Bounce", false), P::choice ("fsync", "Freeze Sync", { "OFF", "1/16", "1/8", "1/4", "1/2", "1 BAR", "2 BARS" }, 0),
                  dB ("thresh", "Threshold", -90, 0, -90), P::range ("pitch", "Pitch", -24, 24, 0, "st"), P::pct ("phase", "Phase Randomise", 0.0f), P::pct ("binshift", "Bin Shift", 0.0f) }; break;
        case uPortal:
            v = { P::pct ("size", "Size", 0.6f), P::choice ("shape", "Shape", { "METALLIC", "ORGANIC", "HOLLOW", "DIFFUSE", "CRYSTALLINE" }, 1), P::choice ("move", "Movement", { "STATIC", "DRIFT", "ROTATE", "EXPAND" }, 1), P::pct ("density", "Density", 0.7f),
                  P::range ("decay", "Decay", 0.3f, 30, 4, "s", 4.0f), P::pct ("transform", "Transform", 0.0f), P::toggle ("freeze", "Freeze", false), ms ("predelay", "Pre-delay", 0, 250, 20, 40), bipolar ("tone", "Tone"), P::pct ("early", "Early Reflections", 0.5f),
                  P::choice ("morph", "Morph Mode", { "WEIGHTED", "CROSSFADE", "SPECTRAL" }, 0), P::range ("stretch", "IR Stretch", 0.5f, 2.0f, 1.0f, "x") }; break;
        case uOtt:
            v = { P::pct ("time", "Time", 0.5f), P::pct ("up", "Upward", 0.6f), P::pct ("down", "Downward", 0.7f), dB ("in", "Input", -12, 12), dB ("out", "Output", -12, 12),
                  dB ("lowgain", "Low Gain", -12, 12), dB ("midgain", "Mid Gain", -12, 12), dB ("highgain", "High Gain", -12, 12), hz ("xlo", "Low Crossover", 40, 400, 120, 120), hz ("xhi", "High Crossover", 1000, 8000, 2500, 2500) }; break;
        case uLim:
            v = { dB ("ceiling", "Ceiling", -12, 0, -0.3f), dB ("gain", "Gain", 0, 18, 0), ms ("release", "Release", 1, 500, 80, 80), P::toggle ("adaptive", "Adaptive Release", true), ms ("lookahead", "Lookahead", 0.5f, 10, 2, 2),
                  P::toggle ("truepeak", "True Peak", true), P::choice ("quality", "Oversampling", { "1X", "2X", "4X", "8X", "16X", "32X" }, 1) }; break;
    }
    // a reverb's SIZE re-forms the room when it settles (DECISIONS.md 21): not a modulation target
    if (u == uSpace || u == uShimmer) for (auto& q : v) if (q.id == "size") q.modDepth = 0.0f;
    // the ENGINE menu, always last (ids before it never move): PHANTOM (this effect's own engine) and the extra engines (DECISIONS.md 85)
    if (withEngineMenu) if (auto labels = engines::engineLabels (u); ! labels.empty()) v.push_back (P::choice ("engine", "Engine", labels, 0));
    return v;
}

std::unique_ptr<PhantomEffect> makeEffect (int u) { return makeEffectOn (u, 0); }

static std::unique_ptr<PhantomEffect> makeOwn (int u)
{
    switch (u)
    {
        case uGate: return std::make_unique<GhostGateFx>();
        case uGrid: return std::make_unique<GhostGridFx>();
        case uHaunt: return std::make_unique<HauntEditFx>();
        case uWarp: return std::make_unique<WarpFx>();
        case uFilter: return std::make_unique<PhantomFilterFx>();
        case uFormant: return std::make_unique<GhostFormantFx>();
        case uCrackle: return std::make_unique<CrackleFx>();
        case uWreck: return std::make_unique<WreckerFx>();
        case uTube: return std::make_unique<TubeGlowFx>();
        case uRetro: return std::make_unique<RetroFx>();
        case uEq: return std::make_unique<SpiritEqFx>();
        case uCrush: return std::make_unique<SoulCrushFx>();
        case uFatten: return std::make_unique<FattenFx>();
        case uBassLab: return std::make_unique<BassLabFx>();
        case uSpace: return std::make_unique<SpectralSpaceFx>();
        case uShimmer: return std::make_unique<ShimmerFx>();
        case uPortal: return std::make_unique<DoorwayFx>();
        case uChorus: return std::make_unique<ChorusFx>();
        case uSpectra: return std::make_unique<GhostSpectraFx>();
        case uCrystal: return std::make_unique<CrystalFx>();
        case uHalls: return std::make_unique<HauntedHallsFx>();
        case uEcho: return std::make_unique<EctoEchoFx>();
        case uOtt: return std::make_unique<GhostSqueezeFx>();
        case uLim: return std::make_unique<GhostLimiterFx>();
        default: break;
    }
    return std::make_unique<PlaceholderEffect> (u);   // units whose phase has not come yet
}
std::unique_ptr<PhantomEffect> makeEffectOn (int u, int engineEntry) { return engines::withEngines (u, makeOwn (u), engineEntry); }
} // namespace phantom
