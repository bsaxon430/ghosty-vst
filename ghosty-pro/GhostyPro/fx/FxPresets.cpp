// Ghost FX rack: per-unit presets, whole-rack scenes (user files) and the undo stack.
#include "FxPresets.h"

namespace gpfx {

static std::array<bool, kNumUnits> justUnit (int u) { std::array<bool, kNumUnits> a {}; a[(size_t) u] = true; return a; }
static std::array<bool, kNumUnits> allUnits() { std::array<bool, kNumUnits> a; a.fill (true); return a; }

// ================================================================ factory unit presets
namespace {
using V = std::vector<std::pair<const char*, float>>;

std::function<void (Rack::Impl&)> padKit (std::array<PadDef, kPads> kit)
{
    return [kit] (Rack::Impl& d) { for (int i = 0; i < kPads; ++i) d.setPad (i, kit[(size_t) i]); };
}

std::vector<UnitPreset> makeGate()
{   // pattern is 1-based: 2 8TH, 3 16TH, 4 TRANCE, 5 PUMP, 6 HALF-TIME, 7 REV TAIL, 8 STUTTER FILL, 9 TAPE STOP, 11 DOTTED, 13 SWELL
    return { { "8th Chop",        V { { "pattern", 2 },  { "smooth", 0.15f } } },
             { "16th Gate",       V { { "pattern", 3 },  { "smooth", 0.12f } } },
             { "Trance Gate",     V { { "pattern", 4 },  { "smooth", 0.10f } } },
             { "Sidechain Pump",  V { { "pattern", 5 },  { "smooth", 0.30f } } },
             { "Half-Time Drop",  V { { "pattern", 6 } } },
             { "Reverse Tail",    V { { "pattern", 7 } } },
             { "Stutter Fill",    V { { "pattern", 8 } } },
             { "Tape Stop End",   V { { "pattern", 9 } } },
             { "Dotted Groove",   V { { "pattern", 11 }, { "smooth", 0.20f } } },
             { "Slow Swell (2 bars)", V { { "pattern", 13 }, { "smooth", 0.5f }, { "length", 2 } } }
#if GPFX_BOX
             // Ghost Box: the PUMP (pattern gate bypassed while PUMP ONLY is on)
             , { "Pump 4 On The Floor", V { { "pmode", 1 }, { "pdepth", 0.75f }, { "pshape", 0.45f } } },
             { "Pump Half-Time",      V { { "pmode", 2 }, { "pdepth", 0.8f }, { "pshape", 0.4f } } },
             { "Pump Fast 1/8",       V { { "pmode", 3 }, { "pdepth", 0.6f }, { "pshape", 0.6f } } },
             { "Pump Drawn Curve",    V { { "pmode", 4 }, { "pdepth", 0.8f }, { "plen", 1 } } },
             { "Sidechain Duck",      V { { "pmode", 5 }, { "pdepth", 0.75f }, { "pthr", -30.0f }, { "prel", 160.0f } } },
             { "Gentle Pump",         V { { "pmode", 1 }, { "pdepth", 0.35f }, { "pshape", 0.7f } } }
#endif
           };
}
std::vector<UnitPreset> makeGrid()
{
    return { { "Breakdown",    V { { "pattern", 1 } } },
             { "Snare Roll",   V { { "pattern", 2 }, { "stutter", 0.7f } } },
             { "Scratchy",     V { { "pattern", 3 }, { "scratch", 0.6f } } },
             { "Tape Out",     V { { "pattern", 4 }, { "tape", 0.5f } } },
             { "Reverso",      V { { "pattern", 5 } } },
             { "Dub Throws",   V { { "pattern", 6 }, { "delay", 0.65f }, { "verb", 0.7f } } },
             { "Tonal Glitch", V { { "pattern", 7 }, { "tonal", 0.6f } } },
             { "Chaos (32 steps)", V { { "pattern", 8 }, { "steps", 1 } } } };
}
std::vector<UnitPreset> makeHaunt()
{   // pad divisions: 1 1/32, 2 1/16T, 3 1/16, 4 1/8T, 5 1/8, 6 1/4T, 7 1/4, 8 1/2, 9 1 BAR, 10 2 BARS
    std::array<PadDef, kPads> classic; for (int i = 0; i < kPads; ++i) classic[(size_t) i] = factoryPad (i);
    std::array<PadDef, kPads> glitch { { { htGlitch, 5, 0.3f }, { htGlitch, 3, 0.5f }, { htGlitch, 2, 0.6f }, { htGlitch, 1, 0.8f },
        { htStutter, 3, 0.2f }, { htStutter, 1, 0.4f }, { htStutter, 0, 0.6f }, { htReverse, 3, 0.0f },
        { htCrush, 3, 0.7f }, { htCrush, 1, 0.9f }, { htGate, 1, 0.7f }, { htGate, 0, 0.8f },
        { htPitchDrop, 3, 0.7f }, { htTapeStop, 3, 0.8f }, { htGlitch, 4, 0.4f }, { htGlitch, 6, 0.3f } } };
    std::array<PadDef, kPads> builds { { { htBuild, 9, 0.0f }, { htBuild, 10, 0.0f }, { htBuild, 8, 0.2f }, { htBuild, 7, 0.4f },
        { htLpSweep, 9, 0.5f }, { htLpSweep, 10, 0.7f }, { htHpSweep, 9, 0.5f }, { htHpSweep, 10, 0.7f },
        { htStutter, 7, 0.0f }, { htStutter, 5, 0.0f }, { htStutter, 3, 0.0f }, { htStutter, 1, 0.0f },
        { htGate, 5, 0.6f }, { htGate, 3, 0.6f }, { htTapeStop, 9, 0.3f }, { htReverse, 8, 0.0f } } };
    std::array<PadDef, kPads> gates { { { htGate, 7, 0.3f }, { htGate, 6, 0.3f }, { htGate, 5, 0.4f }, { htGate, 4, 0.4f },
        { htGate, 3, 0.5f }, { htGate, 2, 0.5f }, { htGate, 1, 0.6f }, { htGate, 0, 0.6f },
        { htGate, 7, 0.0f }, { htGate, 5, 0.0f }, { htGate, 3, 0.0f }, { htGate, 1, 0.0f },
        { htGate, 5, 0.9f }, { htGate, 3, 0.9f }, { htGate, 4, 0.7f }, { htGate, 2, 0.7f } } };
    std::array<PadDef, kPads> tape { { { htTapeStop, 7, 0.2f }, { htTapeStop, 8, 0.3f }, { htTapeStop, 9, 0.5f }, { htTapeStop, 5, 0.7f },
        { htPitchDrop, 5, 0.3f }, { htPitchDrop, 7, 0.5f }, { htPitchDrop, 8, 0.7f }, { htPitchDrop, 3, 0.9f },
        { htReverse, 5, 0.0f }, { htReverse, 7, 0.0f }, { htReverse, 8, 0.0f }, { htReverse, 9, 0.0f },
        { htStutter, 7, 0.0f }, { htStutter, 5, 0.0f }, { htLpSweep, 8, 0.4f }, { htHpSweep, 8, 0.4f } } };
    std::array<PadDef, kPads> lofi { { { htCrush, 7, 0.3f }, { htCrush, 5, 0.5f }, { htCrush, 3, 0.7f }, { htCrush, 9, 0.9f },
        { htLpSweep, 7, 0.3f }, { htLpSweep, 9, 0.6f }, { htGlitch, 5, 0.3f }, { htGlitch, 3, 0.5f },
        { htStutter, 5, 0.0f }, { htStutter, 3, 0.0f }, { htTapeStop, 7, 0.3f }, { htPitchDrop, 7, 0.4f },
        { htGate, 3, 0.4f }, { htGate, 5, 0.4f }, { htReverse, 7, 0.0f }, { htCrush, 1, 1.0f } } };
    return { { "Classic Stutter", V { { "sync", 1 } }, padKit (classic) },
             { "Glitch Kit",      V { { "sync", 1 } }, padKit (glitch) },
             { "Build-Ups",       V { { "sync", 1 } }, padKit (builds) },
             { "Gate Rhythms",    V { { "sync", 1 } }, padKit (gates) },
             { "Tape & Pitch",    V { { "sync", 1 } }, padKit (tape) },
             { "Lo-Fi Crush",     V { { "sync", 1 }, { "mix", 0.8f } }, padKit (lofi) } };
}
std::vector<UnitPreset> makeWreck()
{
    std::vector<UnitPreset> v;
    for (int st = 0; st < kNumWreckStyles; ++st)
    {
        const auto nm = wreckStyleNames()[st].substring (0, 1) + wreckStyleNames()[st].substring (1).toLowerCase();
        const bool digital = st == wsCrush || st == wsDecimate;
        v.push_back ({ nm + " Warm", V { { "style", (float) st }, { "drive", digital ? 0.3f : 0.25f }, { "tone", -0.1f },
                                         { "mix", digital ? 0.5f : 1.0f } } });
        v.push_back ({ nm + " Slammed", V { { "style", (float) st }, { "drive", 0.78f }, { "tone", 0.1f }, { "lowcut", 40.0f },
                                            { "highcut", 14000.0f }, { "punish", st == wsPentode || st == wsFuzz ? 1.0f : 0.0f },
                                            { "thump", st == wsTape || st == wsTube ? 1.0f : 0.0f }, { "mix", digital ? 0.7f : 0.9f } } });
    }
    return v;
}
std::vector<UnitPreset> makeTube()
{
    return { { "Gentle Glow",    V { { "drive", 0.2f } } },
             { "Warm Bus",       V { { "drive", 0.35f }, { "bass", 1.5f }, { "treble", 1.0f } } },
             { "Mic Pre Hot",    V { { "drive", 0.6f }, { "char", 1 }, { "out", -2.0f } } },
             { "Bright Console", V { { "drive", 0.3f }, { "treble", 4.0f }, { "bass", -1.0f } } },
             { "Fat Bottom",     V { { "drive", 0.45f }, { "bass", 5.0f }, { "treble", -2.0f } } },
             { "Radiate",        V { { "drive", 0.55f }, { "char", 1 }, { "bass", 3.0f }, { "treble", 2.0f } } },
             { "Overdriven",     V { { "drive", 0.85f }, { "char", 1 }, { "out", -4.0f } } },
             { "Parallel Heat",  V { { "drive", 0.7f }, { "mix", 0.4f } } } };
}
std::vector<UnitPreset> makeCrush()
{
    return { { "Glue",           V { { "crush", 0.3f }, { "dark", 0.2f }, { "release", 200.0f }, { "mix", 0.5f } } },
             { "Drum Smash",     V { { "crush", 0.7f }, { "dark", 0.3f }, { "release", 80.0f }, { "mix", 0.6f } } },
             { "Parallel Grit",  V { { "crush", 0.8f }, { "dark", 0.5f }, { "release", 150.0f }, { "mix", 0.35f } } },
             { "Dark Pump",      V { { "crush", 0.6f }, { "dark", 0.7f }, { "release", 300.0f }, { "mix", 0.55f } } },
             { "Bright Squeeze", V { { "crush", 0.5f }, { "dark", 0.0f }, { "release", 60.0f }, { "mix", 0.6f } } },
             { "Full Destroy",   V { { "crush", 1.0f }, { "dark", 0.4f }, { "release", 40.0f }, { "out", -3.0f }, { "mix", 1.0f } } } };
}
std::vector<UnitPreset> makeEq()
{
    // band helper: suffix strings must outlive the table, so they come from a static pool
    static std::vector<std::unique_ptr<std::string>> pool;
    auto key = [] (int k, const char* f) { pool.push_back (std::make_unique<std::string> ("b" + std::to_string (k) + "_" + f)); return pool.back()->c_str(); };
    auto band = [&key] (V& v, int k, int type, float freq, float gain = 0.0f, float q = 0.71f, int slope = 1)
    {
        v.push_back ({ key (k, "on"), 1.0f }); v.push_back ({ key (k, "type"), (float) type }); v.push_back ({ key (k, "freq"), freq });
        v.push_back ({ key (k, "gain"), gain }); v.push_back ({ key (k, "q"), q }); v.push_back ({ key (k, "slope"), (float) slope });
    };
    using namespace eq;
    std::vector<UnitPreset> p;
    auto add = [&p] (const char* n, V v) { p.push_back ({ n, std::move (v) }); };
    V a; band (a, 8, btHighShelf, 10000.0f, 4.0f, 0.7f);                                                         add ("Air Lift", a);
    V b; band (b, 1, btLowCut, 80.0f, 0, 0.71f, 3);                                                              add ("Low-Cut Clean", b);
    V c; band (c, 1, btLowShelf, 100.0f, 3.0f); band (c, 4, btBell, 800.0f, -2.0f, 0.8f); band (c, 8, btHighShelf, 8000.0f, 3.0f); add ("Smile", c);
    V d; band (d, 1, btLowCut, 400.0f, 0, 0.71f, 3); band (d, 5, btBell, 1500.0f, 4.0f, 1.2f); band (d, 8, btHighCut, 3200.0f, 0, 0.71f, 3); add ("Telephone", d);
    V e; band (e, 1, btLowCut, 30.0f); band (e, 3, btBell, 300.0f, -4.0f, 1.2f);                                 add ("De-mud", e);
    V f; band (f, 6, btBell, 3500.0f, 3.0f, 1.0f);                                                               add ("Presence", f);
    V g; band (g, 1, btLowCut, 700.0f, 0, 0.71f, 2); band (g, 5, btBell, 2000.0f, 3.0f, 1.0f); band (g, 8, btHighCut, 5000.0f, 0, 0.71f, 2); add ("Radio Thin", g);
    V h; band (h, 1, btLowCut, 25.0f); band (h, 2, btLowShelf, 60.0f, 4.0f);                                     add ("Sub Boost", h);
    V i; band (i, 2, btLowShelf, 120.0f, 2.0f); band (i, 8, btHighCut, 7000.0f, 0, 0.71f, 1);                     add ("Dark Tape", i);
    V j; band (j, 1, btLowCut, 100.0f); band (j, 3, btBell, 300.0f, -2.0f, 1.0f); band (j, 6, btBell, 4000.0f, 2.5f, 0.9f);
         band (j, 8, btHighShelf, 12000.0f, 2.0f);                                                               add ("Vocal Polish", j);
    return p;
}
std::vector<UnitPreset> makeFilter()
{   // type 0 LP 1 BP 2 HP 3 NOTCH; lfoshape 0 sine 1 tri 2 saw up 3 saw dn 4 square 5 S&H 6 smooth; lfodiv 6 1/8, 9 1/4, 14 1/1, 15 2/1
    return { { "Slow Sweep",     V { { "type", 0 }, { "poles", 1 }, { "cutoff", 800.0f }, { "reso", 0.35f }, { "lfoamt", 0.6f }, { "lfoshape", 0 }, { "lfosync", 1 }, { "lfodiv", 15 } } },
             { "Auto-Wah",       V { { "type", 1 }, { "poles", 1 }, { "cutoff", 600.0f }, { "reso", 0.6f }, { "envamt", 0.8f }, { "envrel", 150.0f } } },
             { "Resonant Pulse", V { { "type", 0 }, { "poles", 1 }, { "cutoff", 700.0f }, { "reso", 0.75f }, { "lfoamt", 0.5f }, { "lfoshape", 4 }, { "lfosync", 1 }, { "lfodiv", 6 } } },
             { "Lo-Fi Band",     V { { "type", 1 }, { "poles", 0 }, { "cutoff", 1200.0f }, { "reso", 0.2f }, { "drive", 0.4f } } },
             { "Talkbox-ish",    V { { "type", 1 }, { "routing", 2 }, { "cutoff", 900.0f }, { "spread", 1.2f }, { "reso", 0.7f }, { "lfoamt", 0.4f }, { "lfoshape", 6 }, { "lfosync", 1 }, { "lfodiv", 9 } } },
             { "S&H Freak",      V { { "type", 0 }, { "poles", 1 }, { "cutoff", 900.0f }, { "reso", 0.6f }, { "lfoamt", 0.5f }, { "lfoshape", 5 }, { "lfosync", 1 }, { "lfodiv", 6 }, { "drive", 0.3f } } },
             { "HP Riser",       V { { "type", 2 }, { "poles", 1 }, { "cutoff", 300.0f }, { "reso", 0.4f }, { "lfoamt", 0.7f }, { "lfoshape", 2 }, { "lfosync", 1 }, { "lfodiv", 14 } } },
             { "Notch Phaser",   V { { "type", 3 }, { "cutoff", 1500.0f }, { "reso", 0.5f }, { "lfoamt", 0.6f }, { "lfoshape", 1 }, { "lfosync", 0 }, { "lforate", 0.3f } } },
             { "Dark Lowpass",   V { { "type", 0 }, { "poles", 1 }, { "cutoff", 500.0f }, { "reso", 0.1f } } },
             { "Series Acid",    V { { "type", 0 }, { "routing", 1 }, { "cutoff", 600.0f }, { "reso", 0.8f }, { "drive", 0.5f }, { "envamt", 0.6f }, { "envrel", 120.0f } } } };
}
std::vector<UnitPreset> makeChorus()
{
    return { { "Subtle Width",   V { { "rate", 0.3f }, { "depth", 0.25f }, { "delay", 12.0f }, { "mix", 0.3f } } },
             { "Classic Chorus", V { { "rate", 0.6f }, { "depth", 0.5f }, { "delay", 14.0f }, { "mix", 0.5f } } },
             { "Lush 80s",       V { { "rate", 0.9f }, { "depth", 0.7f }, { "delay", 18.0f }, { "fb", 0.15f }, { "mix", 0.55f } } },
             { "Slow Ensemble",  V { { "rate", 0.15f }, { "depth", 0.6f }, { "delay", 20.0f }, { "fb", 0.1f }, { "mix", 0.5f } } },
             { "Flanger-ish",    V { { "rate", 0.25f }, { "depth", 0.7f }, { "delay", 3.0f }, { "fb", 0.6f }, { "mix", 0.5f } } },
             { "Vibrato",        V { { "rate", 4.0f }, { "depth", 0.35f }, { "delay", 6.0f }, { "width", 0.5f }, { "mix", 1.0f } } },
             { "Seasick",        V { { "rate", 2.5f }, { "depth", 0.8f }, { "delay", 10.0f }, { "mix", 0.6f } } } };
}
std::vector<UnitPreset> makeCrystal()
{
    return { { "Glass Octave",      V { { "pitch", 12 }, { "splice", 180 }, { "delay", 250 }, { "fb", 0.5f }, { "reverse", 0.6f }, { "spread", 0.6f }, { "mix", 0.45f } } },
             { "Reverse Swells",    V { { "pitch", 0 }, { "splice", 400 }, { "delay", 500 }, { "fb", 0.5f }, { "reverse", 1.0f }, { "spread", 0.7f }, { "mix", 0.4f } } },
             { "Fifth Sparkle",     V { { "pitch", 7 }, { "splice", 120 }, { "delay", 200 }, { "fb", 0.4f }, { "reverse", 0.3f }, { "spread", 0.8f }, { "mix", 0.35f } } },
             { "Octave Down Ghost", V { { "pitch", -12 }, { "splice", 250 }, { "delay", 300 }, { "fb", 0.45f }, { "reverse", 0.5f }, { "spread", 0.6f }, { "mix", 0.35f } } },
             { "Granular Wash",     V { { "pitch", 12 }, { "splice", 60 }, { "delay", 800 }, { "fb", 0.7f }, { "reverse", 0.5f }, { "spread", 1.0f }, { "mix", 0.4f } } },
             { "Tight Splices",     V { { "pitch", 0 }, { "splice", 30 }, { "delay", 80 }, { "fb", 0.3f }, { "reverse", 0.2f }, { "spread", 0.4f }, { "mix", 0.3f } } },
             { "Two Octaves Up",    V { { "pitch", 24 }, { "splice", 150 }, { "delay", 350 }, { "fb", 0.55f }, { "reverse", 0.4f }, { "spread", 0.8f }, { "mix", 0.3f } } } };
}
std::vector<UnitPreset> makeEcho()
{   // mode 0 TAPE 1 ANALOG 2 BBD 3 DIGITAL; div 3 1/16, 7 1/8D, 9 1/4, 10 1/4D, 12 1/2
    return { { "Tape Slap",          V { { "mode", 0 }, { "sync", 0 }, { "time", 110 }, { "fb", 0.15f }, { "ping", 0 }, { "lowcut", 120 }, { "highcut", 5000 }, { "char", 0.6f }, { "mix", 0.3f } } },
             { "Dub Throw",          V { { "mode", 0 }, { "div", 10 }, { "fb", 0.62f }, { "ping", 1 }, { "lowcut", 250 }, { "highcut", 3200 }, { "char", 0.7f }, { "mix", 0.35f } } },
             { "Ping Pong 1/8d",     V { { "mode", 3 }, { "div", 7 }, { "fb", 0.45f }, { "ping", 1 }, { "mix", 0.3f } } },
             { "BBD Dark",           V { { "mode", 2 }, { "div", 9 }, { "fb", 0.5f }, { "highcut", 2500 }, { "char", 0.7f }, { "mix", 0.3f } } },
             { "Analog Quarter",     V { { "mode", 1 }, { "div", 9 }, { "fb", 0.4f }, { "ping", 0 }, { "mix", 0.25f } } },
             { "Ducked Vocal Echo",  V { { "mode", 3 }, { "div", 9 }, { "fb", 0.4f }, { "duck", 0.7f }, { "mix", 0.35f } } },
             { "Long Tape Echo",     V { { "mode", 0 }, { "div", 12 }, { "fb", 0.7f }, { "highcut", 4000 }, { "char", 0.5f }, { "mix", 0.3f } } },
             { "Clean Digital 1/16", V { { "mode", 3 }, { "div", 3 }, { "fb", 0.3f }, { "ping", 1 }, { "mix", 0.2f } } } };
}
std::vector<UnitPreset> makeSpace()
{
    return { { "Small Room",    V { { "size", 0.2f }, { "blend", 0.3f }, { "decay", 0.6f }, { "mod", 0.2f }, { "glimmer", 0.05f }, { "width", 0.8f }, { "predelay", 5 }, { "mix", 0.2f } } },
             { "Plate",         V { { "size", 0.45f }, { "blend", 0.7f }, { "decay", 1.8f }, { "tone", 0.3f }, { "predelay", 15 }, { "mix", 0.25f } } },
             { "Big Hall",      V { { "size", 0.8f }, { "blend", 0.6f }, { "decay", 4.0f }, { "predelay", 30 }, { "mix", 0.3f } } },
             { "Deep Space",    V { { "size", 0.85f }, { "blend", 0.8f }, { "decay", 6.0f }, { "glimmer", 0.35f }, { "mix", 0.35f } } },
             { "Dark Chamber",  V { { "size", 0.5f }, { "blend", 0.5f }, { "decay", 2.5f }, { "tone", -0.6f }, { "mod", 0.3f }, { "glimmer", 0.0f }, { "predelay", 10 }, { "mix", 0.3f } } },
             { "Glimmer Tails", V { { "size", 0.6f }, { "blend", 0.5f }, { "decay", 3.5f }, { "tone", 0.2f }, { "mod", 0.6f }, { "glimmer", 0.5f }, { "mix", 0.4f } } },
             { "Ducked Wash",   V { { "size", 0.7f }, { "blend", 0.7f }, { "decay", 5.0f }, { "mod", 0.5f }, { "glimmer", 0.3f }, { "predelay", 40 }, { "duck", 0.6f }, { "mix", 0.45f } } },
             { "Endless",       V { { "size", 1.0f }, { "blend", 0.9f }, { "decay", 12.0f }, { "mod", 0.5f }, { "glimmer", 0.3f }, { "predelay", 0 }, { "mix", 0.35f } } } };
}
std::vector<UnitPreset> makeShimmer()
{
    return { { "Heaven",        V { { "size", 0.85f }, { "decay", 9.0f }, { "damp", 0.3f }, { "shimmer", 0.7f }, { "mix", 0.45f } } },
             { "Subtle Halo",   V { { "size", 0.6f }, { "decay", 3.0f }, { "damp", 0.5f }, { "shimmer", 0.3f }, { "predelay", 20 }, { "mix", 0.2f } } },
             { "Octave Cloud",  V { { "size", 0.9f }, { "decay", 15.0f }, { "damp", 0.2f }, { "shimmer", 0.9f }, { "predelay", 40 }, { "mod", 0.6f }, { "mix", 0.4f } } },
             { "Dark Shimmer",  V { { "size", 0.7f }, { "decay", 6.0f }, { "damp", 0.75f }, { "shimmer", 0.5f }, { "mix", 0.3f } } },
             { "Frozen Pad",    V { { "size", 0.9f }, { "decay", 20.0f }, { "damp", 0.3f }, { "shimmer", 0.6f }, { "predelay", 0 }, { "freeze", 1 }, { "mix", 0.5f } } },
             { "Short Sparkle", V { { "size", 0.4f }, { "decay", 1.5f }, { "damp", 0.4f }, { "shimmer", 0.6f }, { "predelay", 10 }, { "mod", 0.3f }, { "mix", 0.25f } } } };
}
std::vector<UnitPreset> makeOtt()
{
    return { { "Classic (full)",  V { { "mix", 1.0f } } },
             { "Gentle Glue",     V { { "mix", 0.3f } } },
             { "Upward Only",     V { { "up", 0.8f }, { "down", 0.0f }, { "mix", 0.5f } } },
             { "Downward Only",   V { { "up", 0.0f }, { "down", 0.8f }, { "mix", 0.6f } } },
             { "Fast Squash",     V { { "time", 0.2f }, { "up", 0.8f }, { "down", 0.8f }, { "out", -2.0f }, { "mix", 0.6f } } },
             { "Slow Breathing",  V { { "time", 0.9f }, { "mix", 0.5f } } },
             { "Hot Input",       V { { "in", 6.0f }, { "out", -4.0f }, { "mix", 0.5f } } } };
}
std::vector<UnitPreset> makeLim()
{
    return { { "Transparent",     V { { "ceiling", -0.3f }, { "gain", 0.0f }, { "release", 80.0f } } },
             { "Safe -1 dB",      V { { "ceiling", -1.0f }, { "gain", 0.0f }, { "release", 100.0f } } },
             { "Streaming",       V { { "ceiling", -1.0f }, { "gain", 3.0f }, { "release", 120.0f } } },
             { "Loud",            V { { "ceiling", -0.3f }, { "gain", 4.0f }, { "release", 60.0f } } },
             { "Louder",          V { { "ceiling", -0.3f }, { "gain", 8.0f }, { "release", 40.0f } } },
             { "Brickwall Smash", V { { "ceiling", -0.1f }, { "gain", 14.0f }, { "release", 10.0f } } } };
}
#if GPFX_BOX
#include "FxBoxPresets.inc"
#else
#include "FxFattenPresets.inc"   // Ghosty Pro: FATTEN (2026-10-23)
#include "FxProPresets.inc"      // FX RACK (round 3): DISTORTION, COMPRESSOR, SPLITTER
#endif
} // namespace

const std::vector<UnitPreset>& unitPresets (int u)
{
    static const std::array<std::vector<UnitPreset>, kNumUnits> all {
        makeGate(), makeGrid(), makeHaunt(),
#if GPFX_BOX
        makeWarp(), makeCrackle(),
#endif
        makeWreck(), makeTube(), makeCrush(),
#if GPFX_BOX
        makeRetro(), makeFatten(), makeSplit(), makeBassLab(), makeFormant(),
#endif
        makeEq(), makeFilter(), makeChorus(), makeCrystal(), makeEcho(), makeSpace(), makeShimmer(),
#if GPFX_BOX
        makeHalls(), makeSpectra(), makePortal(),
#endif
        makeOtt(),
#if ! GPFX_BOX
        makeFatten(),
        {},                                   // HYPER (no factory presets yet; FX RACK round 3: this slot used to hold the limiter's list)
        makeDist(), makeComp(), makeBands(), makeEq3(),  // FX RACK (round 3)
#endif
        makeLim() };
    return all[(size_t) juce::jlimit (0, kNumUnits - 1, u)];
}

const std::vector<FactoryScene>& factoryScenes()
{
    static const std::vector<FactoryScene> s {
        { "dry", "Dry (all off)" },
#if GPFX_BOX
        { "retrofat", "Retro Fat" }, { "crackedhall", "Cracked Hall" }, { "dustydream", "Dusty Dream" }, { "galaxyecho", "Galaxy Echo" },
        { "phonebooth", "Phone Booth" }, { "fatcrush", "Fat Crush" }, { "warpflick", "Warp Flick" }, { "tapedrop", "Tape Drop" },
        { "talkingbass", "Talking Bass" }, { "portalcloud", "Doorway Cloud" }, { "spectralfreeze", "Spectral Freeze" }, { "bassphone", "Bass For Phones" },
        { "pumpfloor", "Pump 4/4" }, { "parallelwreck", "Parallel Wreck" },
#endif
        { "wide", "Wide Chorus" }, { "tape", "Tape Warmth" }, { "dub", "Dub Delay" },
        { "shimmer", "Shimmer Heaven" }, { "gate8", "Gate 1/8" }, { "gate16", "Gate 1/16" }, { "halftime", "Half-Time" },
        { "stutter", "Stutter Fill" }, { "lofi", "Lo-Fi Dust" }, { "ott", "Ghost Squeeze" }, { "space", "Deep Space" },
        { "crystal", "Crystal Glass" }, { "wreck", "Wrecked" }, { "wreckclean", "Wrecked Clean" }, { "grid", "Grid Breakdown" }, { "radiate", "Tube Radiate" },
        { "freak", "Filter Freak" }, { "crush", "Soul Crush" }, { "spaceblend", "Space Blend" }, { "cleanlimit", "Clean Limit" } };
    return s;
}
juce::String sceneDisplayName (const juce::String& key)
{
    const auto k = key.trim().toLowerCase();
    if (k.isEmpty()) return "Dry (all off)";
    for (auto& s : factoryScenes()) if (k == s.key) return s.display;
    return key;
}

static juce::File ghostyRoot() { return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Ghosty").getChildFile (GPFX_BOX ? "Ghost Box" : "Ghosty Pro"); }
juce::File unitPresetDir (int u)
{
    const juce::String t (unitInfo (u).title);
    juce::StringArray w = juce::StringArray::fromTokens (t.toLowerCase(), " ", "");
    for (auto& x : w) x = x.substring (0, 1).toUpperCase() + x.substring (1);
    return ghostyRoot().getChildFile ("FX Presets").getChildFile (w.joinIntoString (" "));
}
juce::File sceneDir() { return ghostyRoot().getChildFile ("FX Scenes"); }
juce::File boardDir() { return ghostyRoot().getChildFile ("Spirit Boards"); }
juce::Array<juce::File> listXml (const juce::File& dir)
{
    auto a = dir.findChildFiles (juce::File::findFiles, false, "*.xml");
    std::sort (a.begin(), a.end(), [] (const juce::File& x, const juce::File& y) { return x.getFileName().compareNatural (y.getFileName()) < 0; });
    return a;
}

// ================================================================ Rack::Impl: labels, baselines, undo
void Rack::Impl::collectParams (juce::AudioProcessorValueTreeState& s)
{
    for (auto* p : gestureHooked) p->removeListener (&gestureUndo);
    gestureHooked.clear();
    allParams.clear(); for (auto& v : unitParams) v.clear();
    for (auto* p : s.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const auto id = rp->getParameterID();
            if (! id.startsWith ("fx_")) continue;
            allParams.push_back (rp);
            rp->addListener (&gestureUndo); gestureHooked.push_back (rp);
            for (int u = 0; u < kNumUnits; ++u)
                if (id.startsWith (juce::String ("fx_") + unitInfo (u).key + "_")) { unitParams[(size_t) u].push_back (rp); break; }
        }
    baselinePending.store (true);
}

namespace {
bool countsAsEdit (int u, const juce::RangedAudioParameter* p)
{
    const auto& id = p->getParameterID();
    if (id == "fx_haunt_pad") return false;                 // performance pad, not a setting
    return u < 0 || id != pid (u, "on");                    // a unit's own on/off doesn't make its preset "edited"
}
bool differs (float a, float b) { return std::abs (a - b) > 1.0e-6f; }
}

Rack::Impl::Snapshot Rack::Impl::capture() const
{
    const juce::ScopedLock sl (presetLock);
    Snapshot s; s.values.reserve (allParams.size());
    for (auto* p : allParams) s.values.push_back (p->getValue());
    s.state = owner->saveState();
    return s;
}
void Rack::Impl::restore (const Snapshot& snap)
{
    const juce::ScopedLock sl (presetLock);
#if GPFX_BOX
    box.scenes.requestClear();   // GHOST SCENES: an undo / redo replaces the sound, the scene layer lets go
#endif
    duckAround (allUnits(), [&]
    {
        for (size_t i = 0; i < allParams.size() && i < snap.values.size(); ++i) setNorm (allParams[i], snap.values[i]);
        owner->loadState (snap.state);
    });
    baselinePending.store (false);
    rebaseline (-1);
}
void Rack::Impl::pushUndo()
{
    const juce::ScopedLock sl (presetLock);
    updateEdited();
    auto snap = capture();
    if (! undoStack.empty() && undoStack.back().values == snap.values && undoStack.back().state.isEquivalentTo (snap.state)) { redoStack.clear(); return; }   // nothing changed since the last step
    undoStack.push_back (std::move (snap));
    while (undoStack.size() > kUndoMax) undoStack.erase (undoStack.begin());
    redoStack.clear();   // a new edit forks the history
}
bool Rack::Impl::undo()
{
    const juce::ScopedLock sl (presetLock);
    if (undoStack.empty()) return false;
    auto s = std::move (undoStack.back()); undoStack.pop_back();
    redoStack.push_back (capture());
    while (redoStack.size() > kUndoMax) redoStack.erase (redoStack.begin());
    restore (s);
    return true;
}
bool Rack::Impl::redo()
{
    const juce::ScopedLock sl (presetLock);
    if (redoStack.empty()) return false;
    auto s = std::move (redoStack.back()); redoStack.pop_back();
    undoStack.push_back (capture());
    while (undoStack.size() > kUndoMax) undoStack.erase (undoStack.begin());
    restore (s);
    return true;
}
int Rack::Impl::undoDepth() const { const juce::ScopedLock sl (presetLock); return (int) undoStack.size(); }
int Rack::Impl::redoDepth() const { const juce::ScopedLock sl (presetLock); return (int) redoStack.size(); }

void Rack::Impl::rebaseline (int which)
{
    const juce::ScopedLock sl (presetLock);
    for (int u = 0; u < kNumUnits; ++u)
        if (which < 0 || which == u)
        {
            auto& b = unitBase[(size_t) u]; b.clear();
            for (auto* p : unitParams[(size_t) u]) b.push_back (p->getValue());
        }
    if (which < 0)
    {
        sceneBase.clear(); for (auto* p : allParams) sceneBase.push_back (p->getValue());
        sceneBaseVer = dataVersion.load();
    }
}
void Rack::Impl::updateEdited()
{
    const juce::ScopedLock sl (presetLock);
    if (baselinePending.exchange (false)) { rebaseline (-1); return; }
    for (int u = 0; u < kNumUnits; ++u)
    {
        if (unitPreset[(size_t) u].isEmpty() || unitEdited[(size_t) u]) continue;
        const auto& ps = unitParams[(size_t) u]; const auto& b = unitBase[(size_t) u];
        if (b.size() != ps.size()) continue;
        for (size_t i = 0; i < ps.size(); ++i)
            if (countsAsEdit (u, ps[i]) && differs (ps[i]->getValue(), b[i])) { unitEdited[(size_t) u] = true; break; }
    }
    if (sceneName.isNotEmpty() && ! sceneEdited && sceneBase.size() == allParams.size())
    {
        if (dataVersion.load() != sceneBaseVer) sceneEdited = true;
        else
            for (size_t i = 0; i < allParams.size(); ++i)
                if (countsAsEdit (-1, allParams[i]) && differs (allParams[i]->getValue(), sceneBase[i])) { sceneEdited = true; break; }
    }
}
juce::String Rack::Impl::unitLabel (int u) const
{
    const juce::ScopedLock sl (presetLock);
    const auto& n = unitPreset[(size_t) juce::jlimit (0, kNumUnits - 1, u)];
    if (n.isEmpty()) return {};
    return unitEdited[(size_t) u] ? n + " (edited)" : n;
}
juce::String Rack::Impl::sceneLabel() const
{
    const juce::ScopedLock sl (presetLock);
    if (sceneName.isEmpty()) return {};
    return sceneEdited ? sceneName + " (edited)" : sceneName;
}

void Rack::Impl::setNorm (juce::RangedAudioParameter* p, float v01)
{
    if (p == nullptr || ! differs (p->getValue(), v01)) return;
    ++internalEdit; p->beginChangeGesture(); p->setValueNotifyingHost (v01); p->endChangeGesture(); --internalEdit;
}

// ================================================================ click-free loads
void Rack::Impl::duckAround (const std::array<bool, kNumUnits>& which, const std::function<void()>& change)
{
    auto* mm = juce::MessageManager::getInstanceWithoutCreating();
    const bool canWait = prepared && attached && mm != nullptr && mm->isThisTheMessageThread();   // never block the audio thread
    bool any = false;
    for (int u = 0; u < kNumUnits; ++u)
        if (which[(size_t) u] && u != uLim && attached && onP[(size_t) u].b())
        { duckLow[(size_t) u].store (false); duckReq[(size_t) u].store (1, std::memory_order_release); any = true; }
    if (any && canWait)
    {
        const double start = juce::Time::getMillisecondCounterHiRes(), bm = (double) blockMs.load();
        const double limit = juce::jlimit (8.0, 150.0, 2.5 * bm + 6.0);
        const int64_t c0 = processCount.load();
        for (;;)
        {
            bool all = true;
            for (int u = 0; u < kNumUnits; ++u)
                if (duckReq[(size_t) u].load() != 0 && ! duckLow[(size_t) u].load (std::memory_order_acquire)) { all = false; break; }
            const double el = juce::Time::getMillisecondCounterHiRes() - start;
            if (all || el > limit || (el > 10.0 + 2.0 * bm && processCount.load() == c0)) break;   // done, too slow, or audio not running
            juce::Thread::sleep (1);
        }
    }
    change();
    for (auto& r : duckReq) r.store (0, std::memory_order_release);
}

// ================================================================ unit presets
namespace {
// target value for each of the unit's parameters: defaults (on/off kept) overridden by `vals` (real values)
std::vector<float> unitTargets (const std::vector<juce::RangedAudioParameter*>& ps, int u, const std::function<float (const juce::String&, float, bool&)>& lookup)
{
    std::vector<float> t;
    for (auto* p : ps)
    {
        const auto id = p->getParameterID();
        const juce::String suffix = id.fromFirstOccurrenceOf (juce::String ("fx_") + unitInfo (u).key + "_", false, false);
        float v = (id == pid (u, "on") || id == "fx_haunt_pad") ? p->getValue() : p->getDefaultValue();
        bool found = false; const float real = lookup (suffix, 0.0f, found);
        if (found) v = p->convertTo0to1 (real);
        t.push_back (v);
    }
    return t;
}
}

void Rack::Impl::resetUnit (int u, bool alsoOn)
{
    for (auto* p : unitParams[(size_t) u])
        if (alsoOn || p->getParameterID() != pid (u, "on")) setNorm (p, p->getDefaultValue());
}

void Rack::Impl::applyUnitPreset (int u, int idx)
{
#if GPFX_BOX
    otherLoad();
#endif
    if (u < 0 || u >= kNumUnits) return;
    const juce::ScopedLock sl (presetLock);
    pushUndo();
    const auto& list = unitPresets (u);
    const UnitPreset* pr = juce::isPositiveAndBelow (idx, (int) list.size()) ? &list[(size_t) idx] : nullptr;
    const auto& ps = unitParams[(size_t) u];
    auto t = unitTargets (ps, u, [pr] (const juce::String& sfx, float, bool& found) -> float
    {
        if (pr != nullptr) for (auto& [k, v] : pr->vals) if (sfx == k) { found = true; return v; }
        found = false; return 0.0f;
    });
    duckAround (justUnit (u), [&]
    {
        for (size_t i = 0; i < ps.size(); ++i) setNorm (ps[i], t[i]);
        if (pr != nullptr && pr->extra) pr->extra (*this);
        if (pr == nullptr && u == uHaunt) for (int i = 0; i < kPads; ++i) setPad (i, factoryPad (i));
#if GPFX_BOX
        if (pr == nullptr && u == uWarp) warpShape.set (bendlane::defaultShape());
#endif
        if (pr != nullptr) if (auto* on = apvts->getParameter (pid (u, "on"))) setNorm (on, 1.0f);
    });
    unitPreset[(size_t) u] = pr != nullptr ? pr->name : juce::String ("Init");
    unitEdited[(size_t) u] = false;
    rebaseline (u);
}

bool Rack::Impl::saveUserPreset (int u, const juce::String& name)
{
    if (u < 0 || u >= kNumUnits || name.trim().isEmpty()) return false;
    auto dir = unitPresetDir (u);
    if (! dir.createDirectory()) return false;
    juce::XmlElement x ("GHOSTFXPRESET");
    x.setAttribute ("unit", unitInfo (u).key); x.setAttribute ("name", name.trim()); x.setAttribute ("version", 1);
    const juce::String pre = juce::String ("fx_") + unitInfo (u).key + "_";
    for (auto* p : unitParams[(size_t) u])
    {
        const auto id = p->getParameterID();
        if (id == pid (u, "on") || id == "fx_haunt_pad") continue;
        auto* e = x.createNewChildElement ("PARAM");
        e->setAttribute ("id", id.fromFirstOccurrenceOf (pre, false, false));
        e->setAttribute ("value", (double) p->convertFrom0to1 (p->getValue()));
    }
    if (u == uHaunt)
        for (int i = 0; i < kPads; ++i)
        {
            const auto d = getPad (i); auto* e = x.createNewChildElement ("PAD");
            e->setAttribute ("i", i); e->setAttribute ("type", d.type); e->setAttribute ("div", d.div); e->setAttribute ("motion", (double) d.motion);
        }
#if GPFX_BOX
    if (u == uWarp)
    {
        const auto w = warpShape.get(); auto* e = x.createNewChildElement ("CURVE");
        e->setAttribute ("points", w.toString()); e->setAttribute ("snap", w.snap); e->setAttribute ("grid", w.grid);
    }
#endif
    if (! x.writeTo (dir.getChildFile (juce::File::createLegalFileName (name.trim()) + ".xml"))) return false;
    const juce::ScopedLock sl (presetLock);
    unitPreset[(size_t) u] = name.trim(); unitEdited[(size_t) u] = false; rebaseline (u);
    return true;
}

bool Rack::Impl::loadUserPreset (int u, const juce::File& f)
{
#if GPFX_BOX
    otherLoad();
#endif
    auto x = juce::XmlDocument::parse (f);
    if (u < 0 || u >= kNumUnits || x == nullptr || ! x->hasTagName ("GHOSTFXPRESET") || x->getStringAttribute ("unit") != unitInfo (u).key) return false;
    const juce::ScopedLock sl (presetLock);
    pushUndo();
    const auto& ps = unitParams[(size_t) u];
    auto t = unitTargets (ps, u, [&x] (const juce::String& sfx, float, bool& found) -> float
    {
        for (auto* e : x->getChildWithTagNameIterator ("PARAM"))
            if (e->getStringAttribute ("id") == sfx) { found = true; return (float) e->getDoubleAttribute ("value"); }
        found = false; return 0.0f;
    });
    duckAround (justUnit (u), [&]
    {
        for (size_t i = 0; i < ps.size(); ++i) setNorm (ps[i], t[i]);
        if (u == uHaunt)
            for (auto* e : x->getChildWithTagNameIterator ("PAD"))
            {
                const int i = e->getIntAttribute ("i", -1);
                if (i >= 0 && i < kPads)
                    setPad (i, { juce::jlimit (0, kNumHauntTypes - 1, e->getIntAttribute ("type")), juce::jlimit (0, (int) padDivs().size() - 1, e->getIntAttribute ("div", 7)),
                                 clampf ((float) e->getDoubleAttribute ("motion"), 0.0f, 1.0f) });
            }
#if GPFX_BOX
        if (u == uWarp)
            if (auto* e = x->getChildByName ("CURVE"))
                warpShape.set (bendlane::Shape::fromString (e->getStringAttribute ("points"), e->getIntAttribute ("snap", 1), e->getIntAttribute ("grid", 2)));
#endif
        if (auto* on = apvts->getParameter (pid (u, "on"))) setNorm (on, 1.0f);
    });
    unitPreset[(size_t) u] = x->getStringAttribute ("name", f.getFileNameWithoutExtension());
    unitEdited[(size_t) u] = false;
    rebaseline (u);
    return true;
}

// ================================================================ scenes
void Rack::Impl::afterFactoryScene (const juce::String& key)
{
#if GPFX_BOX
    otherLoad();
#endif
    const juce::ScopedLock sl (presetLock);
    sceneName = sceneDisplayName (key); sceneEdited = false;
    for (int u = 0; u < kNumUnits; ++u) { unitPreset[(size_t) u] = {}; unitEdited[(size_t) u] = false; }
    baselinePending.store (false);
    rebaseline (-1);
}

bool Rack::Impl::saveUserScene (const juce::String& name)
{
    if (name.trim().isEmpty()) return false;
    auto dir = sceneDir();
    if (! dir.createDirectory()) return false;
    juce::String oldName; bool oldEd;
    {
        const juce::ScopedLock sl (presetLock);
        oldName = sceneName; oldEd = sceneEdited;
        sceneName = name.trim(); sceneEdited = false;        // the saved file carries its own name
    }
    juce::XmlElement x ("GHOSTFXSCENE");
    x.setAttribute ("name", name.trim()); x.setAttribute ("version", 1);
    auto* pe = x.createNewChildElement ("PARAMS");
    for (auto* p : allParams)
    {
        auto* e = pe->createNewChildElement ("PARAM");
        e->setAttribute ("id", p->getParameterID()); e->setAttribute ("value", (double) p->convertFrom0to1 (p->getValue()));
    }
    if (auto st = owner->saveState().createXml()) x.addChildElement (st.release());
    const bool ok = x.writeTo (dir.getChildFile (juce::File::createLegalFileName (name.trim()) + ".xml"));
    const juce::ScopedLock sl (presetLock);
    if (ok) rebaseline (-1);
    else { sceneName = oldName; sceneEdited = oldEd; }
    return ok;
}

bool Rack::Impl::loadUserScene (const juce::File& f)
{
#if GPFX_BOX
    otherLoad();
#endif
    auto x = juce::XmlDocument::parse (f);
    if (x == nullptr || ! x->hasTagName ("GHOSTFXSCENE")) return false;
    const juce::ScopedLock sl (presetLock);
    pushUndo();
    std::map<juce::String, float> vals;
    if (auto* pe = x->getChildByName ("PARAMS"))
        for (auto* e : pe->getChildWithTagNameIterator ("PARAM")) vals[e->getStringAttribute ("id")] = (float) e->getDoubleAttribute ("value");
    duckAround (allUnits(), [&]
    {
        for (auto* p : allParams)
        {
            auto it = vals.find (p->getParameterID());
            setNorm (p, it != vals.end() ? p->convertTo0to1 (it->second) : p->getDefaultValue());
        }
#if GPFX_BOX
        // MORPH's A / B belong to the session, not to a scene: a user scene file also carries the MORPH states that were set when
        // it was saved, and loading them here replaced the A just captured ("A FROM SCENE", then "B FROM SCENE" = no morph).
        // Keep the current A / B; MORPH lets go like it does for a factory scene (it comes back with the next SET / FROM SCENE).
        const bool hadA = box.hasState (0), hadB = box.hasState (1);
        const BoxMod::Snap keepA = box.state (0), keepB = box.state (1);
        const int keepCurve = box.getMorphCurve();
        const auto keepBoard = box.saveBoard();
#endif
        if (auto* st = x->getChildByName ("GHOSTFX")) owner->loadState (juce::ValueTree::fromXml (*st));
#if GPFX_BOX
        box.clearMorph();
        if (hadA) box.setState (0, keepA);
        if (hadB) box.setState (1, keepB);
        box.setMorphCurve (keepCurve);
        box.setMorphOn (false);
        box.loadBoard (keepBoard); box.setBoardOn (false);   // the SPIRIT BOARD belongs to the session too (and lets go like MORPH)
#endif
    });
    sceneName = x->getStringAttribute ("name", f.getFileNameWithoutExtension()); sceneEdited = false;
    baselinePending.store (false);
    rebaseline (-1);
    return true;
}

} // namespace gpfx

#if GPFX_BOX
// ================================================================ Ghost Box 3: MORPH actions
namespace gpfx {
static std::array<int, kNumUnits> orderFromKeys (const juce::String& s)
{
    std::array<int, kNumUnits> o; o.fill (-1); int k = 0;
    for (auto& key : juce::StringArray::fromTokens (s, ",", ""))
        for (int u = 0; u < kNumUnits; ++u)
            if (key == unitInfo (u).key && k < kNumUnits) o[(size_t) k++] = u;
    return o;
}
static void setBoxParam (juce::AudioProcessorValueTreeState* s, const juce::String& id, float v)
{
    if (s == nullptr) return;
    if (auto* p = s->getParameter (id)) { p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (v)); p->endChangeGesture(); }
}
void Rack::Impl::morphSet (int which)
{
    pushUndo();
    box.setState (which, box.snapNow (getOrder()));
    setBoxParam (apvts, "box_morph", which == 0 ? 0.0f : 1.0f);   // you hear what you just captured
    if (box.hasState (0) && box.hasState (1))
    {
        box.setMorphOn (true);
        setOrder (orderFromKeys (box.state (0).order));               // the chain order comes from A
    }
}
int Rack::Impl::boardAdd (float x, float y)
{
    if (box.board.full()) return -1;
    pushUndo();
    const int i = box.board.add (x, y);
    if (i >= 0) { auto sn = box.snapNow (getOrder()); sn.rack = rackStateForLayer(); box.boardSnaps[box.board.ghosts[(size_t) i].id] = sn; }
    ++box.boardContentVersion;
    box.boardChanged();
    return i;
}
void Rack::Impl::boardSetFrom (int g, const std::function<void()>& load)
{
    if (! juce::isPositiveAndBelow (g, (int) box.board.ghosts.size())) return;
    pushUndo();
    const bool was = box.isBoardOn();
    const auto keep = box.saveBoard();
    if (load) load();
    box.loadBoard (keep);   // a scene load must not touch the board itself
    { auto sn = box.snapNow (getOrder()); sn.rack = rackStateForLayer(); box.boardSnaps[box.board.ghosts[(size_t) g].id] = sn; }
    ++box.boardContentVersion;
    box.setBoardOn (was);
    box.boardChanged();
}
void Rack::Impl::boardRemove (int g)
{
    if (! juce::isPositiveAndBelow (g, (int) box.board.ghosts.size())) return;
    pushUndo();
    box.boardSnaps.erase (box.board.ghosts[(size_t) g].id);
    box.board.remove (g); ++box.boardContentVersion;
    if (box.board.ghosts.empty()) box.setBoardOn (false);
    box.boardChanged();
}
void Rack::Impl::boardEnable (bool on)
{
    pushUndo();
    std::array<bool, kNumUnits> all; all.fill (true);
    duckAround (all, [this, on] { box.boardShared.publish (box.board); box.setBoardOn (on); });
    if (box.isBoardOn() && ! box.board.ghosts.empty())
        if (auto it = box.boardSnaps.find (box.board.ghosts[0].id); it != box.boardSnaps.end() && it->second.order.isNotEmpty()) setOrder (orderFromKeys (it->second.order));
}
void Rack::Impl::boardEdited() { box.boardChanged(); }
bool Rack::Impl::saveBoardFile (const juce::String& name)
{
    if (name.trim().isEmpty() || ! boardDir().createDirectory()) return false;
    auto t = box.saveBoard(); t.setProperty ("name", name.trim(), nullptr);
    if (auto x = t.createXml()) return x->writeTo (boardDir().getChildFile (juce::File::createLegalFileName (name.trim()) + ".xml"));
    return false;
}
bool Rack::Impl::loadBoardFile (const juce::File& f)
{
    auto x = juce::XmlDocument::parse (f);
    if (x == nullptr || ! x->hasTagName ("SPIRITBOARD")) return false;
    otherLoad();
    pushUndo();
    const bool was = box.isBoardOn();
    std::array<bool, kNumUnits> all; all.fill (true);
    duckAround (all, [&] { box.loadBoard (juce::ValueTree::fromXml (*x)); box.setBoardOn (was || box.isBoardOn()); });
    return true;
}
void Rack::Impl::morphSwap()
{
    pushUndo();
    { std::array<bool, kNumUnits> all; all.fill (true); duckAround (all, [this] { box.swapStates(); }); }
    if (apvts != nullptr) if (auto* v = apvts->getRawParameterValue ("box_morph")) setBoxParam (apvts, "box_morph", 1.0f - v->load());
    if (box.hasState (0)) setOrder (orderFromKeys (box.state (0).order));
}
void Rack::Impl::morphClear()
{
    pushUndo();
    std::array<bool, kNumUnits> all; all.fill (true);
    duckAround (all, [this] { box.clearMorph(); });   // the shadows jump back to the host values inside the duck
}
void Rack::Impl::morphEnable (bool on)
{
    pushUndo();
    std::array<bool, kNumUnits> all; all.fill (true);
    duckAround (all, [this, on] { box.setMorphOn (on); });
}
} // namespace gpfx
#endif
