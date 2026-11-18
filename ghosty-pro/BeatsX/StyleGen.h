#pragma once
// Ghosty Beats: the STYLE ENGINE (GENERATE <STYLE> and VARY). One shared, deterministic generator driven by a data Profile per kit:
// every kit that has no generator of its own (DRILL = DrillStyle.h, LATIN / Reggaeton = LatinStyle.h, TRAP CLASSIC = TrapClassicStyle.h)
// gets a profile here, matched by kit name. Original archetypes and music theory only; nothing here is transcribed from any recording.
//
// A profile holds: a tempo lane, the feel per role (swing for the hats, the kicks, the snares and the percussion separately, plus a
// humanize amount: tight for machine styles, loose for dusty ones), SEVERAL pattern families (kick cells for bars 1/3 and 2/4, the snare
// placement, a hat vocabulary, open-hat spots, shaker and percussion cells, how many hat rolls, the 808 degrees, the phrase-end fill),
// the roll vocabulary allowed, ghost-note probability, how the 808 behaves, crash / impact / swell / crackle behaviour, and the scoring
// ranges (kicks per bar, syncopation). One press = one 4-bar phrase on the 1/16 grid: the family rotates with the seed; several candidates
// (sub-seeds) are written and scored (anchors present, densities in range, no kick under the main snare where the style forbids it,
// syncopation in range, a phrase ending, novelty against the pattern that was there) and the best one is kept. Same seed + same kit = same beat.
// VARY keeps the family and the kick foundation of the current pattern and re-rolls everything else.
//
// Cells are 16-character bar strings on the 1/16 grid: x hit, X accent, o soft / ghost, f flam, 2..8 a roll of that many hits, . off.
// Percussion cells may start with "B:" (bars 2 and 4 only), "4:" (bar 4 only) or "L<n>:" (a polymeter track of n steps).
// Included from GenreKits.h (after BeatModel.h): include BeatModel.h, never this header directly.
#include "BeatModel.h"
#include "RhythmDNA.h"
#include <set>

namespace gb::sty {

constexpr int kStyleBase = 100;   // GENERATE style index = kStyleBase + profile index
constexpr int kVaryBase = 300;    // VARY style index = kVaryBase + profile index (same family + kick foundation as the current pattern)
constexpr int kBars = 4, kLen = kBars * 16, kCandidates = 6;
inline uint32_t mix(uint32_t x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x; }

// ---------------- what each track plays (from the sound's category and name)
enum Kind : uint8_t { KNone = 0, KKick, KKick2, KBass, KSnare, KSnare2, KClap, KClap2, KRim, KHat, KOpen, KShaker, KRide, KCrash, KTomLo, KTomMid, KTomHi,
                      KConga, KBongo, KClave, KCowbell, KBell, KTamb, KSnap, KPerc, KVox, KCrackle, KSwell, KImpact, KFx, KBrass, kKinds };
inline bool isPercKind(int k) { return k == KRim || (k >= KTomLo && k <= KPerc) || k == KVox || k == KBrass; }
inline uint8_t kindOf(const juce::String& name, const juce::String& category) {
    const auto n = name.toLowerCase(), c = category;
    auto has = [&](const char* s) { return n.contains(s); };
    if (c == "808" || has("sub pulse") || (has("808") && !has("kick"))) return KBass;
    if (c == "FX" && (has("reverse") || has("riser") || has("sweep") || has("swell") || has("downlifter") || has("phaser"))) return KSwell;
    if (c == "Kick" || (c != "Snare" && c != "Clap" && c != "Hat" && c != "Open Hat" && c != "Perc" && c != "Tom" && c != "Cymbal" && c != "FX" && has("kick") && !has("reverse"))) return KKick;
    if (c == "Open Hat" || (has("open hat") && !has("closed"))) return KOpen;
    if (c == "Hat" || c == "Pedal Hat" || has("hat")) return KHat;
    if (has("snap") && !has("snare")) return KSnap;
    if (has("rim") && !has("snare")) return KRim;   // (a rim / sidestick is a percussion colour here, whatever its library category)
    if (c == "Clap" || (has("clap") && !has("reverse"))) return KClap;
    if (c == "Snare" || (has("snare") && !has("reverse"))) return KSnare;
    if (c == "Tom" || has("tom") || has("timp")) return has("hi") || has("high") || has("top") || has("rack") ? KTomHi : (has("mid") || has("body") ? KTomMid : KTomLo);
    if (c == "Cymbal") return has("ride") ? KRide : (has("reverse") || has("swell") ? KSwell : KCrash);
    if (has("crackle")) return KCrackle;
    if (has("vox")) return KVox;
    if (has("brass") || has("stab") || has("orch")) return KBrass;
    if (has("riser") || has("sweep") || has("swell") || has("reverse") || has("downlifter") || has("phaser")) return KSwell;
    if (has("impact") || has("gong") || has("sub drop") || (c == "FX" && (has("boom") || has("thunder") || has("slam")))) return KImpact;   // ("boom" alone is a kit name: ANALOG BOOM, BOOM BAP)
    if (has("zap") || has("laser") || has("blaster") || has("chirp") || has("blip") || has("zip")) return KFx;
    if (has("shaker") || has("maracas") || has("cabasa") || has("egg")) return KShaker;
    if (has("rim")) return KRim;
    if (has("tick")) return KPerc;
    if (has("conga")) return KConga;
    if (has("bongo")) return KBongo;
    if (has("clave") || has("wood") || has("stick") || has("knock") || has("log")) return KClave;
    if (has("cowbell")) return KCowbell;
    if (has("bell") || has("chime") || has("tin") || has("anvil") || has("pipe") || has("metal") || has("crush sine")) return KBell;
    if (has("tamb")) return KTamb;
    if (has("ride")) return KRide;
    if (has("crash") || has("china") || has("cymbal")) return KCrash;
    if (c == "Perc" || c == "Glitch" || has("click") || has("perc") || has("burst") || has("grain") || has("drop")) return KPerc;
    return KNone;
}
// the kinds of a model's 16 tracks: the first kick / snare / clap / hat / open hat / 808 is the main one, a second kick is KKick2, a second snare KSnare2, a second clap KClap2, further ones play nothing
inline std::array<uint8_t, kTracks> kindsOf(const Model& m, const std::function<juce::String(const juce::String&)>& categoryOf) {
    std::array<uint8_t, kTracks> k {}; bool kick = false, snare = false, clap = false, hat = false, open = false, bass = false, kick2 = false, snare2 = false, clap2 = false;
    for (int t = 0; t < kTracks; ++t) {
        const auto& nm = m.tracks[(size_t) t].sound; uint8_t x = kindOf(nm, categoryOf ? categoryOf(nm) : juce::String());
        if (x == KKick) { if (!kick) kick = true; else if (!kick2) { x = KKick2; kick2 = true; } else x = KNone; }
        else if (x == KSnare) { if (!snare) snare = true; else if (!snare2) { x = KSnare2; snare2 = true; } else x = KNone; }
        else if (x == KClap) { if (!clap) clap = true; else if (!clap2) { x = KClap2; clap2 = true; } else x = KNone; }
        else if (x == KHat) { if (!hat) hat = true; else x = KNone; }
        else if (x == KOpen) { if (!open) open = true; else x = KNone; }
        else if (x == KBass) { if (!bass) bass = true; else x = KNone; }
        k[(size_t) t] = x;
    }
    return k;
}

// ---------------- vocabularies
enum Roll : uint8_t { R32 = 0, R32x4, RTrip, RAccel, RPitch, R64, kRolls };
enum RollBit : uint8_t { B32 = 1, B32x4 = 2, BTrip = 4, BAccel = 8, BPitch = 16, B64 = 32, BAll = 63 };
inline bool rollTwoSteps(int r) { return r == R32x4 || r == RTrip || r == RAccel || r == RPitch; }
inline void applyRoll(Step& st, int r) {
    switch (r) {
        case R32:    st.roll = 2; st.rate = 8;  st.velRamp = 35; break;
        case R32x4:  st.roll = 4; st.rate = 8;  st.velRamp = 45; break;
        case RTrip:  st.roll = 3; st.rate = 6;  st.velRamp = 30; break;
        case RAccel: st.roll = 6; st.rate = 8;  st.accel = 60; st.velRamp = 50; break;
        case RPitch: st.roll = 4; st.rate = 8;  st.pitchRamp = 7; st.velRamp = 40; break;
        default:     st.roll = 4; st.rate = 16; st.velRamp = 55; break;
    }
}
// hat vocabulary (x normal, o soft, . off; the velocity map adds the metric accents)
enum Hat : uint8_t { HNone = 0, H8, H16, HOff8, H332, HBroken, HTrapHoles, HShuffle, HQuarter, H8Run, HTripHoles, HOff16, HBounce, HSampler, HLofi, HProg, HElectro, HSoftHoles, HRun16, HHouse909, HCrunch, HSwing, kHats };
inline const char* hatCell(int id) {
    static const char* c[] { "................", "x.x.x.x.x.x.x.x.", "xxxxxxxxxxxxxxxx", "..x...x...x...x.", "x..x..x.x..x..x.", "xx.xxx.xxx.xxx.x", "x.xxx.x.x.xxx.x.", "x..xx..xx..xx..x",
                             "x...x...x...x...", "x.x.x.x.x.x.xxxx", "x..x..x.x.x..x.x", ".x.x.x.x.x.x.x.x", "ooxoooxoxoxoooxo", "x.x.o.x.x.x.o.x.", "x.o.x.o.x.o.x.o.", "xo.oxo.oxo.oxo.o",
                             "oxo.oxo.oxo.oxo.", "oo.ooo.ooo.ooo.o", "x.xxx.xxx.xxx.xx", "xx.xox.xxx.xox.x", "x.x.o.x.x.x.o.xo", "x.xxo.x.x.xxo.x." };
    return c[juce::jlimit(0, (int) kHats - 1, id)];
}
// open-hat spots: cell + whether it plays in every bar (else bars 2 and 4)
struct OpenSpot { const char* cell; bool allBars; };
inline const OpenSpot& openSpot(int id) {
    static const OpenSpot o[] { { "..x...x...x...x.", true }, { "..............x.", false }, { "......x.........", false }, { "..x.......x.....", false }, { ".......x.......x", true },
                                { "...x...x...x...x", true }, { "......x.......x.", true }, { "............x...", false } };
    return o[juce::jlimit(0, 7, id)];
}
// shaker vocabulary: 0 none, 1 16ths accented on the 8ths, 2 16ths accented on the "e", 3 off-8ths, 4 8ths, 5 flat soft 16ths, 6 16ths accented on the "a"
inline const char* shakerCell(int id) { static const char* c[] { "................", "xoxoxoxoxoxoxoxo", "oxoooxoooxoooxoo", "..x...x...x...x.", "x.x.x.x.x.x.x.x.", "oooooooooooooooo", "o.oxo.oxo.oxo.ox" }; return c[juce::jlimit(0, 6, id)]; }
// phrase-end fills (bar 4)
enum Fill : uint8_t { FNone = 0, FSnare16, FSnareRoll, FToms, FTomsBig, FKickRun, FHardKick, FHouse, FBreak, FGlitch, FDrop, FMachine, FJersey, FGhostFlam, FConga, FGarage, kFills };
inline int fillFrom(int f) {   // the first 16th of bar 4 the fill owns (kicks and hats make room from here), 16 = none
    switch (f) { case FSnare16: case FToms: case FKickRun: case FBreak: case FGlitch: case FDrop: case FConga: return 12; case FTomsBig: case FMachine: case FHardKick: case FJersey: return 8; default: return 16; }
}

// ---------------- a pattern family and a style profile
struct Fam {
    const char* name;
    const char* kA; const char* kB;            // kick cells: bars 1 / 3 and bars 2 / 4
    const char* sn; const char* snB;           // snare cells: all bars, and bars 2 / 4 when not empty
    uint8_t hat; int8_t open; uint8_t shaker;  // hat vocabulary, open-hat spot (-1 none), shaker vocabulary
    const char* p1; const char* p2; const char* p3; const char* p4;   // percussion cells, handed to the profile's percussion order
    uint8_t rolls;                             // hat rolls per phrase
    std::array<int8_t, 3> deg;                 // 808 degrees cycling (semitones from the root)
    uint8_t fill;                              // phrase-end fill
    uint8_t hatFrom = 0;                       // the first bar the hats play (a build)
};
enum Flag : uint8_t { FNoKickUnderSnare = 1, FFourFloor = 2, FLoose = 4, FFxEnds = 8, FHatSoftUnderSnare = 16 };
enum Bass : uint8_t { BassNone = 0, BassFollow, BassSlides, BassShort, BassDrill, BassSustain, BassBoom };   // BassBoom: the long analog boom on the downbeats of bars 1 and 3
enum Kick2 : uint8_t { K2Off = 0, K2Downbeat, K2Double, K2OffPulse, K2Boom, K2Soft };   // K2Soft: the second kick plays the cell's soft 'o' kicks
enum Clap : uint8_t { ClapIfNoSnare = 0, ClapLayer, ClapBackbeat, ClapLast };          // ClapLast: layered on the last main snare of bars 2 / 4
enum Snare2 : uint8_t { S2Off = 0, S2Gated, S2Ghosts };                                  // S2Gated: doubles the main snares of bars 2 / 4; S2Ghosts: plays the ghosts
struct Profile {
    const char* kit;
    int bpmLo, bpmHi;
    int8_t swHat, swKick, swSn, swPerc;   // swing % per role (50 = straight): the off-16ths are pushed late by (sw - 50) * 2 % of a step
    uint8_t human;                        // random nudge (+-) on hats, percussion and ghosts; half of it on kicks / snares when FLoose
    int8_t snLate;                        // the main snare's lean (nudge, % of a step): laid-back styles
    uint8_t ghost, ghostVel;              // ghost-snare chance per bar (%), ghost velocity
    uint8_t bass; float slide;            // 808 behaviour, slide chance
    uint8_t hatVel;                       // 0 flat (machine), 1 normal, 2 strong contrast
    uint8_t kick2, clap, snare2, ride;    // second kick, clap, second snare and ride behaviour (ride: 0 none, 1 8ths in bars 3-4, 2 quarters)
    uint8_t crash;                        // crash on the phrase start
    uint8_t rollMask; const char* rollSpots;   // roll kinds allowed, 16ths where a roll may start
    const char* kickExtra;                // 16ths where bars 2 / 4 may add a soft kick (variation)
    uint8_t flags;
    uint8_t kickLo, kickHi; float syncLo, syncHi;   // scoring: kicks per bar, syncopation range (rhythm::extract)
    std::vector<uint8_t> percOrder;       // which percussion kinds take p1..p4 (in this order, then by track)
    std::vector<Fam> fams;
};

// ---------------- the profiles (one per kit; see each kit in BeatModel.h / GenreKits.h)
inline const std::vector<Profile>& profiles() {
    using D = std::array<int8_t, 3>;
    static const std::vector<Profile> P {
        // ---- trap family: 140-150, half time (snare + clap on 3), 16th hats with rolls, the 808 on the kicks
        { "Trap", 140, 150, 50, 50, 50, 50, 1, 0, 25, 46, BassSlides, 0.25f, 1, K2Off, ClapLayer, S2Off, 0, 0, BAll, "...x..xx...x..xx", "...x.....x...x.x", FNoKickUnderSnare, 1, 5, 0.02f, 0.4f,
          { KRim, KPerc, KSnap, KCowbell, KTomLo, KTomHi },
          { { "T1 bounce",    "x......x..x.....", "x......x..x..x..", "........X.......", "",                 H16, 4, 0, "", "", "", "", 3, D { 0, 0, -5 }, FSnare16 },
            { "T2 double",    "x.x.......x.....", "x.x......x...x..", "........X.......", "........X.....o.", H16, 1, 0, "", "", "", "", 4, D { 0, 0, 0 }, FSnareRoll },
            { "T3 triplet",   "x..x..x.........", "x..x..x...x.....", "........X.......", "",                 H8Run, 4, 0, "", "", "", "", 2, D { 0, 3, 0 }, FSnare16 },
            { "T4 late",      "x.........x..x..", "x......x..x....x", "........X.......", "........X....o..", HTrapHoles, 1, 0, "", "", "", "", 3, D { 0, 0, 7 }, FToms },
            { "T5 minimal",   "x.............x.", "x.........x.....", "........X.......", "",                 H8, 2, 0, "", "", "", "", 2, D { 0, 7, 0 }, FSnareRoll },
            { "T6 rolling",   "x.....x...x.x...", "x.....x..x..x...", "........X.......", "",                 H16, 4, 0, "", "", "", "", 5, D { 0, 0, 5 }, FSnare16 } } },
        { "Trap Hard", 142, 152, 50, 50, 50, 50, 0, 0, 10, 50, BassSlides, 0.35f, 2, K2Off, ClapLayer, S2Off, 0, 1, B32x4 | B64 | BAccel, "...x..xx...x..xx", ".........x...x.x", FNoKickUnderSnare, 1, 5, 0.0f, 0.4f,
          { KRim, KPerc, KBell, KSnap, KTomLo, KTomHi },
          { { "TH1 stomp",    "x.........x.....", "x......x..x..x..", "........X.......", "",                 H16, 7, 0, "", "", "", "", 4, D { 0, 0, 0 }, FSnare16 },
            { "TH2 machine",  "x.x.x.....x.....", "x.x.x.....x.x...", "........X.......", "",                 H16, 1, 0, "..x.......x.....", "", "", "", 5, D { 0, -5, 0 }, FToms },
            { "TH3 dark",     "x..x..x.........", "x..x..x...x..x..", "........X.......", "........X.....o.", HTripHoles, 4, 0, "", "", "", "", 3, D { 0, 0, 3 }, FSnare16 },
            { "TH4 pushed",   "x......x..x.x...", "x......x..x.xx..", "........X.......", "",                 HTrapHoles, 1, 0, "", "", "", "", 4, D { 0, 0, -2 }, FSnareRoll },
            { "TH5 heavy",    "x...............", "x.........x.....", "........X.......", "",                 H8, 2, 0, "", "", "", "", 3, D { 0, 0, 0 }, FSnare16 } } },
        { "TRAP GOLD", 138, 148, 50, 50, 50, 50, 0, 0, 15, 48, BassFollow, 0.15f, 1, K2Off, ClapLayer, S2Off, 0, 0, B32 | BTrip | B32x4, "...x..xx...x..xx", ".........x...x..", FNoKickUnderSnare, 1, 5, 0.0f, 0.4f,
          { KBell, KRim, KSnap, KShaker, KPerc },
          { { "TG1 gold",      "x......x..x.....", "x......x..x..x..", "........X.......", "",                 H16, 1, 0, "..x.......x.....", "", "", "", 3, D { 0, 0, 0 }, FSnare16 },
            { "TG2 slow burn", "x.........x.....", "x.........x...x.", "........X.......", "",                 H8, 7, 0, "......x.........", "", "", "", 2, D { 0, 0, -5 }, FSnareRoll },
            { "TG3 stutter",   "x.x......x......", "x.x......x..x.x.", "........X.......", "........X.....o.", HTrapHoles, 1, 0, "", "", "", "", 4, D { 0, 3, 0 }, FSnare16 },
            { "TG4 triplets",  "x..x..x.........", "x..x..x...x.....", "........X.......", "",                 H16, 4, 0, "..........x.....", "", "", "", 3, D { 0, 0, 7 }, FSnareRoll },
            { "TG5 answer",    "x.............x.", "x......x......x.", "........X.......", "",                 H8Run, 2, 0, "..x.......x...x.", "", "", "", 2, D { 0, 5, 0 }, FSnare16 } } },
        { "TEXAS BOUNCE", 108, 120, 54, 50, 50, 52, 2, 0, 20, 46, BassShort, 0.0f, 2, K2Off, ClapLayer, S2Off, 0, 0, B32, "..............x.", "...x.........x.x", FNoKickUnderSnare, 2, 6, 0.05f, 0.5f,
          { KSnap, KCowbell, KRim, KShaker, KTomLo, KTomHi },
          { { "TX1 bounce",    "x..x..x...x..x..", "x..x..x...x..x.x", "....X.......X...", "",                 HBounce, -1, 0, "...........x...o", ".x.......x.....x", "", "", 1, D { 0, 0, 0 }, FSnareRoll },
            { "TX2 skip",      "x.....x...x.....", "x.....x...x..x..", "....X.......X..o", "",                 HBounce, 1, 0, "...........x...o", "", "", "", 0, D { 0, 0, 0 }, FGhostFlam },
            { "TX3 double-up", "x.x...x...x.x...", "x.x...x...x.x.x.", "....X.......X...", "",                 H16, -1, 0, "..x.....x.x.....", ".x.......x.....x", "", "", 1, D { 0, 0, 0 }, FSnare16 },
            { "TX4 lean back", "x......x..x.....", "x......x..x...x.", "....X.......X...", "....X......oX...", H8, 2, 0, "...........x...o", "", "", "", 0, D { 0, 0, 0 }, FGhostFlam } } },
        // ---- drill (the plain Drill and Drill Dark kits; the DRILL kit keeps DrillStyle.h): 140-146 half time, the bar-2 snare displaced, 3-3-2 hats with triplet bursts, a sliding 808 in the kick's holes
        { "Drill", 140, 146, 54, 50, 50, 50, 1, 0, 10, 44, BassDrill, 0.5f, 1, K2Off, ClapLayer, S2Off, 0, 0, BTrip, "..x...x....x..x.", ".........x......", FNoKickUnderSnare, 1, 4, 0.02f, 0.5f,
          { KRim, KPerc, KSnap, KShaker, KTomLo, KBell },
          { { "D1 two-step",   "x.........x.....", "...x..........x.", "........X.......", ".......X......x.", H332, -1, 0, "...x.......x....", "", "", "", 2, D { 0, 7, 0 }, FSnareRoll },
            { "D2 counter",    "x.............x.", "...x..x.........", "........X.......", "........X.....x.", H332, -1, 0, "...x.......x...x", "", "", "", 2, D { 0, 3, 0 }, FNone },
            { "D3 bounce",     "x..x......x..x..", "x.....x.......x.", "........X.......", "........X....x..", HTripHoles, 1, 0, "", "", "", "", 3, D { 0, 0, 10 }, FSnareRoll },
            { "D4 808-led",    "x...............", "..........x.....", "........X.......", ".......X.....x..", H332, -1, 0, "...x.......x....", "", "", "", 2, D { 0, 8, 7 }, FNone },
            { "D5 early",      "x.....x........x", "x.........x.....", ".......X........", "........X.....x.", H332, 1, 0, "", "", "", "", 3, D { 0, 1, 0 }, FSnareRoll } } },
        { "Drill Dark", 140, 146, 52, 50, 50, 50, 2, 0, 0, 44, BassDrill, 0.7f, 0, K2Off, ClapLayer, S2Off, 0, 0, BTrip, "..x........x....", ".........x...x..", FNoKickUnderSnare, 1, 3, 0.0f, 0.5f,
          { KRim, KPerc, KShaker, KSnap, KBell, KTomLo },
          { { "DD1 cold",      "x...............", "...x..........x.", "........X.......", ".......X......x.", H332, 1, 0, "...x.......x....", "", "", "", 1, D { 0, -12, 3 }, FDrop },
            { "DD2 knock",     "x.........x.....", "x.....x.........", "........X.......", "........X....x..", HShuffle, -1, 0, "", "..........o.....", "", "", 2, D { 0, 8, 7 }, FNone },
            { "DD3 stab",      "x......x........", "..........x...x.", "........X.......", ".......X.....x..", H332, -1, 0, "...x.......x....", "", "", "", 3, D { 0, 1, -12 }, FDrop },
            { "DD4 dirge",     "x.............x.", "x.........x.....", "........X.......", "........X......x", H8, -1, 0, "", "......x.........", "", "", 2, D { 0, 0, -5 }, FNone } } },
        // ---- boom bap / lofi: 76-96, swung hats, ghost notes, loose timing
        { "Boom Bap", 86, 96, 58, 52, 50, 56, 5, 2, 55, 40, BassNone, 0.0f, 2, K2Off, ClapIfNoSnare, S2Off, 0, 0, B32, "..............x.", "...x.....x...x..", FNoKickUnderSnare | FLoose, 2, 5, 0.05f, 0.6f,
          { KTamb, KRim, KShaker, KConga, KCowbell, KSnap },
          { { "BB1 classic",   "x.......xx......", "x.....x.xx...x..", "....X.......X...", "....X..o....X...", H8, -1, 0, "", "", "", "", 0, D { 0, 0, 0 }, FGhostFlam },
            { "BB2 stumble",   "x..x......x.....", "x..x.....xx.....", "....X.......X...", "....X.......X.o.", H8, -1, 0, "....x.......x...", "", "", "", 0, D { 0, 0, 0 }, FGhostFlam },
            { "BB3 double",    "x.x.......x.....", "x.x......x.x....", "....X.......X...", "",                 H8, 1, 0, "", "", "", "", 0, D { 0, 0, 0 }, FNone },
            { "BB4 late boom", "x.........x..x..", "x......x..x..x..", "....X.......X...", "....X...o...X...", H8, 1, 0, "....x.......x...", "", "", "", 0, D { 0, 0, 0 }, FGhostFlam },
            { "BB5 pickup",    "x.......x.x.....", "x.......x.x..x..", "....X.......X...", "",                 H16, -1, 0, "", "", "", "", 0, D { 0, 0, 0 }, FGhostFlam },
            { "BB6 shuffle",   "x.....x...x.....", "x.....x...x.x...", "....X.......X...", "",                 HShuffle, -1, 0, "", "", "", "", 1, D { 0, 0, 0 }, FNone } } },
        { "Boom Bap Dusty", 88, 94, 62, 54, 50, 58, 7, 3, 70, 38, BassNone, 0.0f, 2, K2Off, ClapIfNoSnare, S2Off, 0, 0, B32, "..............x.", "...x.........x..", FNoKickUnderSnare | FLoose, 2, 4, 0.05f, 0.6f,
          { KShaker, KRim, KSnap, KBell, KConga, KTomLo },
          { { "BD1 dust",      "x.........x.....", "x.......x.x.....", "....X.......X...", "....X.......X..o", H8, 1, 0, "", "", "", "", 0, D { 0, 0, 0 }, FGhostFlam },
            { "BD2 drag",      "x......x..x.....", "x......x..x...x.", "....X.......X...", "",                 H8, -1, 0, "", "..x.......x.....", "", "", 0, D { 0, 0, 0 }, FNone },
            { "BD3 boom",      "x.......xx......", "x.....x.xx......", "....X..o....X...", "",                 HBroken, -1, 0, "", "", "", "", 0, D { 0, 0, 0 }, FGhostFlam },
            { "BD4 broken",    "x..x......x..x..", "x..x.....x..x...", "....X.......X.o.", "",                 H8, 1, 0, "", "", "", "", 0, D { 0, 0, 0 }, FGhostFlam } } },
        { "BOOM BAP CLASSIC", 88, 94, 58, 50, 50, 54, 3, 1, 35, 42, BassNone, 0.0f, 1, K2Off, ClapIfNoSnare, S2Off, 0, 0, B32, "..............x.", ".........x...x..", FNoKickUnderSnare, 2, 4, 0.05f, 0.6f,
          { KRim, KShaker, KSnap, KBell, KConga, KTomLo },
          { { "BC1 sampler",   "x.........x..x..", "x.........x..x..", "....X.......X...", "",                 HCrunch, -1, 0, "", "", "", "", 0, D { 0, 0, 0 }, FGhostFlam },
            { "BC2 head nod",  "x......x..x.....", "x......x..x...x.", "....X.......X...", "....X.......X.o.", HCrunch, 1, 0, "", "", "", "", 0, D { 0, 0, 0 }, FNone },
            { "BC3 kick pair", "x.x.......x.....", "x.x.......x.x...", "....X.......X...", "",                 H8, -1, 0, ".......o.o......", "", "", "", 0, D { 0, 0, 0 }, FGhostFlam },
            { "BC4 off-kick",  "x.......x..x....", "x.......x..x..x.", "....X.......X...", "",                 HCrunch, -1, 0, "", "", "", "", 0, D { 0, 0, 0 }, FGhostFlam },
            { "BC5 stagger",   "x..x......x.....", "x..x.....x.x....", "....X.......X...", "....X..o....X...", H8, 1, 0, "......x.......x.", "", "", "", 0, D { 0, 0, 0 }, FNone } } },
        { "Lofi", 76, 88, 60, 54, 52, 56, 6, 5, 40, 36, BassNone, 0.0f, 2, K2Off, ClapIfNoSnare, S2Off, 0, 0, B32, "..............x.", ".........x...x..", FNoKickUnderSnare | FLoose, 1, 4, 0.03f, 0.6f,
          { KShaker, KRim, KSnap, KTamb, KPerc, KConga, KClave },
          { { "LF1 drift",     "x......x.x......", "x......x.x....x.", "....X.......X...", "",                 HLofi, -1, 0, "", "", "", "", 0, D { 0, 0, 0 }, FGhostFlam },
            { "LF2 slow",      "x.........x.....", "x.........x..x..", "....X.......X...", "....X.......X..o", HLofi, 2, 0, "", "", "", "", 0, D { 0, 0, 0 }, FNone },
            { "LF3 pickup",    "x......x..x.....", "x......x..x...x.", "....X.......X...", "",                 H8, -1, 0, "o..o............", "", "", "", 0, D { 0, 0, 0 }, FGhostFlam },
            { "LF4 two-kick",  "x.x.......x.....", "x.x......x......", "....X.......X...", "",                 HLofi, -1, 0, "", "", "..x.......x.....", "", 0, D { 0, 0, 0 }, FNone },
            { "LF5 bounce",    "x..x......x..x..", "x..x......x....x", "....X.......X...", "",                 HLofi, -1, 0, "", "", "", "....x.......x...", 0, D { 0, 0, 0 }, FGhostFlam } } },
        { "Lo-fi Tape", 74, 84, 64, 56, 54, 60, 9, 8, 45, 34, BassNone, 0.0f, 2, K2Off, ClapIfNoSnare, S2Off, 1, 0, B32, "..............x.", ".........x......", FNoKickUnderSnare | FLoose, 1, 3, 0.03f, 0.6f,
          { KShaker, KRim, KConga, KSnap, KPerc, KClave },
          { { "LT1 tape",       "x......x.x......", "x......x.x......", "....X.......X...", "....X.......x...", HShuffle, -1, 0, "", "", "", "", 0, D { 0, 0, 0 }, FGhostFlam },
            { "LT2 wobble",     "x.........x.....", "x......x..x.....", "....X.......X...", "",                 HLofi, 1, 0, "o..o............", "", "", "", 0, D { 0, 0, 0 }, FNone },
            { "LT3 late night", "x.............x.", "x.........x.....", "....X.......X...", "....X......oX...", H8, -1, 0, "", "", "...o......o.....", "", 0, D { 0, 0, 0 }, FNone },
            { "LT4 hiss",       "x..x......x.....", "x..x......x...x.", "....X.......X...", "",                 HShuffle, 1, 0, "", "", "", "", 0, D { 0, 0, 0 }, FGhostFlam } } },
        // ---- house / techno family: four on the floor stays, the rest is hats, claps and percussion
        { "House", 122, 128, 54, 50, 50, 54, 1, 0, 0, 40, BassNone, 0.0f, 2, K2Off, ClapBackbeat, S2Off, 0, 1, B32, "..............x.", "", FFourFloor, 4, 5, 0.0f, 0.35f,
          { KRim, KConga, KTamb, KCowbell, KPerc, KSnap },
          { { "H1 classic",    "X...x...x...x...", "", "....X.......X...", "", HOff8, 0, 1, "", "", "", "", 0, D { 0, 0, 0 }, FHouse },
            { "H2 sixteenths", "X...x...x...x...", "", "....X.......X...", "", H16, 0, 0, "...x..x....x....", "", "", "", 0, D { 0, 0, 0 }, FHouse },
            { "H3 skip",       "X...x...x...x...", "X...x...x...x.x.", "....X.......X...", "", HBroken, 0, 1, "", "..x.x....x.x....", "", "", 0, D { 0, 0, 0 }, FHouse },
            { "H4 deep",       "X...x...x...x...", "", "....X.......X...", "", HOff8, 1, 2, "......x.......x.", "", "....x.......x...", "", 0, D { 0, 0, 0 }, FNone },
            { "H5 jack",       "X...x...x...x...", "", "....X.......X...", "", HOff16, 0, 1, "", "", "", "B:..........x.....", 1, D { 0, 0, 0 }, FHouse } } },
        { "Tech House", 124, 128, 56, 50, 50, 56, 1, 0, 0, 40, BassNone, 0.0f, 2, K2Off, ClapBackbeat, S2Off, 0, 0, B32, "..............x.", "", FFourFloor, 4, 5, 0.0f, 0.4f,
          { KRim, KConga, KClave, KPerc, KBell, KSnap },
          { { "TK1 roller",     "X...x...x...x...", "", "....X.......X...", "", HBroken, 0, 2, "...x..x....x..x.", "......x.......x.", "", "", 0, D { 0, 0, 0 }, FHouse },
            { "TK2 bump",       "X...x...x...x...", "X...x...x...x..o", "....X.......X...", "", HOff8, 0, 2, "...x.......x....", "", "..x.......x.....", "", 0, D { 0, 0, 0 }, FHouse },
            { "TK3 minimal",    "X...x...x...x...", "", "....X.......X...", "", HOff16, 3, 0, "", "", "", "..........x.....", 0, D { 0, 0, 0 }, FNone },
            { "TK4 percussive", "X...x...x...x...", "", "....X.......X...", "", HBroken, 0, 2, "...x..x....x..x.", "..x.x....x.x....", "", "", 0, D { 0, 0, 0 }, FHouse },
            { "TK5 swing",      "X...x...x...x...", "", "....X.......X...", "", HOff8, 0, 1, ".......x.......x", "", "", "", 1, D { 0, 0, 0 }, FNone } } },
        { "Afro House", 118, 124, 54, 50, 50, 55, 2, 0, 0, 40, BassNone, 0.0f, 1, K2Off, ClapBackbeat, S2Off, 1, 0, B32, "..............x.", "", FFourFloor, 4, 5, 0.0f, 0.45f,
          { KRim, KConga, KClave, KBell, KPerc, KTomLo },
          { { "AH1 tribal",     "X...x...x...x...", "", "....x.......x...", "", HOff8, -1, 2, "...x..x....x..x.", "x.....x...x.....", "..x.x....x.x....", "x..x..x...x.x...", 0, D { 0, 0, 0 }, FConga },
            { "AH2 ritual",     "X...x...x...x...", "", "....x.......x...", "", HOff16, -1, 2, "...x..x....x..x.", "x.....x...x...x.", "...........x....", "x..x..x...x.x...", 0, D { 0, 0, 0 }, FConga },
            { "AH3 deep roll",  "X...x...x...x...", "X...x...x...x.x.", "....x.......x...", "", HOff8, 0, 1, "", "x.....x...x.....", "..x.x....x.x..x.", "x..x..x...x.x...", 0, D { 0, 0, 0 }, FNone },
            { "AH4 call",       "X...x...x...x...", "", "....x.......x...", "", HBroken, -1, 2, "", "x.....x...x.....", "..x.x....x.x....", "..........x.....", 0, D { 0, 0, 0 }, FConga },
            { "AH5 shaker-led", "X...x...x...x...", "", "....x.......x...", "", HNone, 0, 2, "...x..x....x..x.", "x.....x...x.....", "..x.x....x.x....", "x..x..x...x.x...", 0, D { 0, 0, 0 }, FConga } } },
        { "Club Progressive", 124, 128, 52, 50, 50, 52, 0, 0, 0, 40, BassNone, 0.0f, 1, K2Off, ClapBackbeat, S2Off, 0, 1, B32, "..............x.", "", FFourFloor | FFxEnds, 4, 5, 0.0f, 0.35f,
          { KRim, KPerc, KTomLo, KTomHi, KSnap },
          { { "CP1 progressive", "X...x...x...x...", "", "....X.......X...", "", HProg, 0, 6, "...x......x..x..", "", "", "", 0, D { 0, 0, 0 }, FHouse },
            { "CP2 driving",     "X...x...x...x...", "", "....X.......X...", "", H16, 0, 6, "", "..........o.....", "B:..............x.", "", 0, D { 0, 0, 0 }, FHouse },
            { "CP3 airy",        "X...x...x...x...", "", "....X.......X...", "", HOff8, 0, 1, ".......x.......x", "", "", "", 0, D { 0, 0, 0 }, FNone },
            { "CP4 pumping",     "X...x...x...x...", "X...x...x...x..x", "....X.......X...", "", HProg, 5, 6, "...x......x.....", "", "", "", 0, D { 0, 0, 0 }, FHouse } } },
        { "Big Room", 126, 130, 50, 50, 50, 50, 0, 0, 0, 40, BassNone, 0.0f, 2, K2Off, ClapLayer, S2Off, 0, 1, B32, "..............x.", "", FFourFloor | FFxEnds, 4, 5, 0.0f, 0.3f,
          { KSnap, KTomLo, KTomHi, KPerc },
          { { "BR1 arena",    "X...x...x...x...", "", "....X.......X...", "", HOff8, 0, 5, "", "", "", "", 0, D { 0, 0, 0 }, FKickRun },
            { "BR2 festival", "X...x...x...x...", "", "....X.......X...", "....X.......X.x.", HBroken, 0, 5, "", "", "", "", 0, D { 0, 0, 0 }, FToms },
            { "BR3 drop",     "X...x...x...x...", "", "....X.......X...", "", H16, 0, 0, "..x.....x.x.....", "", "", "", 0, D { 0, 0, 0 }, FKickRun },
            { "BR4 lead-in",  "X...x...x...x...", "X...x...x...x.x.", "....X.......X...", "", HOff8, 1, 5, "", "", "", "", 0, D { 0, 0, 0 }, FSnare16 } } },
        { "Electro House", 126, 130, 50, 50, 50, 50, 0, 0, 0, 40, BassNone, 0.0f, 2, K2Off, ClapLayer, S2Off, 0, 1, B32x4, "..............x.", "", FFourFloor | FFxEnds, 4, 5, 0.0f, 0.4f,
          { KRim, KTomLo, KTomHi, KPerc, KSnap },
          { { "EH1 electro",    "X...x...x...x...", "", "....X.......X...", "", HElectro, 5, 0, "..x.....x.x.....", "", "", "", 0, D { 0, 0, 0 }, FSnare16 },
            { "EH2 fidget",     "X...x...x...x...", "X...x...x...x.x.", "....X.......X...", "", HOff16, 5, 5, "...x.......x....", "", "", "", 0, D { 0, 0, 0 }, FKickRun },
            { "EH3 complextro", "X...x...x...x...", "", "....X.......X...", "", H16, 0, 0, "..x...x...x...x.", "", "", "", 2, D { 0, 0, 0 }, FSnare16 },
            { "EH4 dutch",      "X...x...x...x...", "", "....X.......X...", "", HElectro, 0, 5, "", "B:..............x.", "B:.............x..", "", 0, D { 0, 0, 0 }, FKickRun },
            { "EH5 bounce",     "X...x...x...x...", "X...x...x...x..o", "....X.......X...", "", HElectro, 5, 0, "", "", "", "", 0, D { 0, 0, 0 }, FSnare16 } } },
        { "Melodic Techno", 120, 124, 50, 50, 50, 50, 0, 0, 0, 40, BassNone, 0.0f, 0, K2OffPulse, ClapBackbeat, S2Off, 0, 0, B32, "..............x.", "", FFourFloor, 4, 5, 0.0f, 0.3f,
          { KRim, KBell, KPerc, KTomLo, KClave },
          { { "MT1 hypnotic", "X...x...x...x...", "", "....x.......x...", "", HSoftHoles, 0, 0, "L12:..x....x.x..", "L10:x.........", "", "", 0, D { 0, 0, 0 }, FNone },
            { "MT2 drive",    "X...x...x...x...", "", "....x.......x...", "", HOff16, 0, 1, "......x.......x.", "", "", "B:..............x.", 0, D { 0, 0, 0 }, FNone },
            { "MT3 wide",     "X...x...x...x...", "", "....x.......x...", "", HOff8, 1, 0, "", "", "L14:......x.......", "", 0, D { 0, 0, 0 }, FNone },
            { "MT4 pulse",    "X...x...x...x...", "", "....x.......x...", "", HSoftHoles, 0, 0, "L12:x.....x..x..", "L10:....x.....", "", "", 0, D { 0, 0, 0 }, FNone },
            { "MT5 minimal",  "X...x...x...x...", "", "....x.......x...", "", HNone, 0, 2, "...x.......x....", "", "L14:x.........x...", "", 0, D { 0, 0, 0 }, FNone } } },
        { "Hardstyle", 148, 155, 50, 50, 50, 50, 0, 0, 0, 40, BassNone, 0.0f, 2, K2Downbeat, ClapLayer, S2Off, 0, 1, B32, "..............x.", "", FFourFloor | FFxEnds, 4, 6, 0.0f, 0.3f,
          { KSnap, KTomLo, KTomHi, KPerc },
          { { "HS1 euphoric",     "X...x...x...x...", "", "....X.......X...", "", HOff8, -1, 5, "", "", "", "", 0, D { 0, 0, 0 }, FHardKick },
            { "HS2 raw",          "X...x...x...x...", "X...x...x...x..x", "....X.......X...", "", HOff8, 0, 0, "..x.....x.x.....", "", "", "", 0, D { 0, 0, 0 }, FHardKick },
            { "HS3 reverse bass", "X...x...x...x...", "", "....X.......X...", "", HOff16, 3, 0, "", "", "", "", 0, D { 0, 0, 0 }, FKickRun },
            { "HS4 anthem",       "X...x...x...x...", "", "....X.......X...", "....X.......X..x", HOff8, 1, 5, "", "B:............x.x.", "B:..............xx", "", 0, D { 0, 0, 0 }, FHardKick } } },
        { "ANALOG PUNCH", 120, 128, 50, 50, 50, 50, 0, 0, 10, 46, BassNone, 0.0f, 1, K2Downbeat, ClapLayer, S2Off, 0, 1, B32, "..............x.", "", FFourFloor, 4, 5, 0.0f, 0.35f,
          { KRim, KShaker, KTomLo, KTomMid, KTomHi, KPerc },
          { { "AP1 acid",      "X...x...x...x...", "", "....X.......X...", "", HHouse909, 0, 0, "...x......x.....", "", "", "", 0, D { 0, 0, 0 }, FSnare16 },
            { "AP2 chicago",   "X...x...x...x...", "X...x...x...x.x.", "....X.......X...", "", HOff8, 0, 1, "", "", "", "", 0, D { 0, 0, 0 }, FToms },
            { "AP3 techno",    "X...x...x...x...", "", "....X.......X...", "", HOff16, 0, 0, "..x...x...x...x.", "", "", "", 0, D { 0, 0, 0 }, FKickRun },
            { "AP4 detroit",   "X...x...x...x...", "", "....X.......X...", "", HHouse909, 1, 1, ".......x.......x", "", "", "", 0, D { 0, 0, 0 }, FSnare16 },
            { "AP5 pushed",    "X...x...x...x...", "X...x...x.x.x...", "....X.......X...", "", H8, 0, 0, "..x.......x.....", "", "", "", 0, D { 0, 0, 0 }, FToms } } },
        // ---- bass music: 138-148 half time (snare on 3), heavy kicks, fills into the drop
        { "Dubstep", 138, 142, 50, 50, 50, 50, 1, 0, 15, 48, BassSustain, 0.0f, 1, K2Off, ClapLayer, S2Off, 0, 1, B32 | B32x4, "...x..xx...x..xx", ".........x...x..", FNoKickUnderSnare | FFxEnds, 1, 4, 0.0f, 0.4f,
          { KPerc, KShaker, KRim, KTomLo, KTomHi },
          { { "DS1 classic",  "x.........x.....", "x.....x...x.....", "........X.......", "........X.....o.", H8, -1, 0, "..............x.", "", "", "", 1, D { 0, 0, 0 }, FSnare16 },
            { "DS2 wobble",   "x...............", "x.........x.....", "........X.......", "",                 HOff8, -1, 3, "", "", "", "", 0, D { 0, 0, -5 }, FDrop },
            { "DS3 stomp",    "x.........x..x..", "x.........x..x..", "........X.......", "",                 H16, 1, 0, "", "", "", "", 2, D { 0, 0, 0 }, FSnare16 },
            { "DS4 swung",    "x.........x.....", "x......x..x.....", "........X.......", "",                 HShuffle, -1, 0, "", "", "", "", 1, D { 0, 3, 0 }, FSnareRoll },
            { "DS5 two-kick", "x.x.......x.....", "x.x......x......", "........X.......", "",                 H8, -1, 0, "", "", "", "", 2, D { 0, 0, 0 }, FDrop } } },
        { "Dubstep Heavy", 138, 142, 50, 50, 50, 50, 0, 0, 10, 50, BassNone, 0.0f, 2, K2Downbeat, ClapLayer, S2Off, 0, 1, B32 | B64, "...x..xx...x..xx", ".........x...x..", FNoKickUnderSnare | FFxEnds, 1, 5, 0.0f, 0.4f,
          { KBell, KPerc, KTomLo, KTomHi, KRim },
          { { "DH1 slam",    "X.........x.....", "X.....x...x..x..", "........X.......", "........X.....o.", H8, 2, 0, "...........x....", "", "", "", 2, D { 0, 0, 0 }, FToms },
            { "DH2 march",   "X...x.....x.....", "X...x.....x...x.", "........X.......", "",                 H16, -1, 0, "..............x.", "", "", "", 2, D { 0, 0, 0 }, FSnare16 },
            { "DH3 trash",   "X.........x..x..", "X.........x.xx..", "........X.......", "",                 H8, 1, 0, ".......x......x.", "", "", "", 3, D { 0, 0, 0 }, FSnare16 },
            { "DH4 thunder", "X...............", "X.........x.....", "........X.......", "",                 HOff8, -1, 0, "", "B:............x.x.", "", "", 0, D { 0, 0, 0 }, FToms } } },
        { "Riddim", 138, 142, 50, 50, 50, 50, 0, 0, 0, 48, BassNone, 0.0f, 1, K2Double, ClapLayer, S2Off, 0, 1, B32, "..............x.", "", FNoKickUnderSnare | FFxEnds, 1, 5, 0.05f, 0.5f,
          { KRim, KClave, KBell, KPerc, KTomLo, KTomHi },
          { { "RD1 riddim",  "X..x......x.....", "X..x..x...x..x..", "........X.......", "", HOff8, -1, 0, "......x.......x.", "B:............x...", "", "", 0, D { 0, 0, 0 }, FDrop },
            { "RD2 triplet", "X..x..x.........", "X..x..x...x.....", "........X.......", "", H8, -1, 0, "...x..x...x...x.", "", "", "", 0, D { 0, 0, 0 }, FSnare16 },
            { "RD3 knock",   "X.........x..x..", "X..x......x..x..", "........X.......", "", HOff8, -1, 0, "..x...x...x...x.", "......x.........", "", "", 0, D { 0, 0, 0 }, FDrop },
            { "RD4 minimal", "X...............", "X..x......x.....", "........X.......", "", HOff8, 1, 0, "......x.......x.", "", "..............x.", "", 0, D { 0, 0, 0 }, FDrop } } },
        { "Brostep", 142, 148, 50, 50, 50, 50, 0, 0, 20, 50, BassNone, 0.0f, 2, K2Downbeat, ClapLayer, S2Off, 0, 1, B32 | B64 | BAccel, "...x..xx...x..xx", ".........x...x..", FNoKickUnderSnare | FFxEnds, 1, 5, 0.0f, 0.45f,
          { KRim, KBell, KTomLo, KTomMid, KPerc },
          { { "BS1 blast",     "X.........x.....", "X..x......x..x..", "........X.......", "........X...o...", HRun16, -1, 0, "", "", "B:............x.x.", "B:.............x.x", 3, D { 0, 0, 0 }, FSnare16 },
            { "BS2 chug",      "X.x.......x.x...", "X.x.......x.x.x.", "........X.......", "",                 H16, 1, 0, "", "", "", "", 4, D { 0, 0, 0 }, FToms },
            { "BS3 lurch",     "X......x........", "X......x..x.....", "........X.......", "",                 HRun16, 2, 0, "..............x.", "", "", "", 2, D { 0, 0, 0 }, FSnare16 },
            { "BS4 half-step", "X.........x.....", "X.........x.....", "........X.......", "........X.....xo", H8, -1, 0, "", "...........x....", "", "", 2, D { 0, 0, 0 }, FToms } } },
        { "Future Bass", 145, 155, 50, 50, 50, 50, 0, 0, 10, 50, BassNone, 0.0f, 1, K2Downbeat, ClapLayer, S2Off, 0, 1, BTrip | B32, "...x..xx...x..xx", ".........x...x..", FNoKickUnderSnare | FFxEnds, 1, 4, 0.0f, 0.4f,
          { KSnap, KFx, KRim, KTomLo, KTomHi },
          { { "FB1 lift",     "X.........x.....", "X.....x...x.....", "........X.......", "",                 H8, 1, 0, "....x.......x...", "", "", "", 2, D { 0, 0, 0 }, FSnare16 },
            { "FB2 bounce",   "X......x..x.....", "X......x..x..x..", "........X.......", "",                 H16, 1, 0, "....x.......x...", "..x.......x.....", "", "", 2, D { 0, 0, 0 }, FSnare16 },
            { "FB3 supersaw", "X...............", "X.........x.....", "........X.......", "",                 HOff8, -1, 0, "....x.......x...", "", "", "", 1, D { 0, 0, 0 }, FSnareRoll },
            { "FB4 trapstep", "X.........x..x..", "X.....x...x..x..", "........X.......", "........X.....o.", HTrapHoles, 1, 0, "....x.......x...", "", "", "", 3, D { 0, 0, 0 }, FSnare16 } } },
        // ---- drum & bass: 170-176, snares on 2 and 4 (the two-step), ghost snares, fast hats, the break fill
        { "Drum & Bass", 170, 176, 50, 50, 50, 50, 0, 0, 50, 38, BassNone, 0.0f, 1, K2Downbeat, ClapIfNoSnare, S2Off, 1, 1, B32, "..............x.", ".........x......", FNoKickUnderSnare, 2, 4, 0.02f, 0.5f,
          { KRim, KTomLo, KTomHi, KFx, KPerc },
          { { "DB1 two-step",   "X.........x.....", "", "....X.......X...", "....X..o....X..o", H8, -1, 5, "", "", "", "", 0, D { 0, 0, 0 }, FBreak },
            { "DB2 roller",     "X......x..x.....", "", "....X.......X...", "....X.......X.o.", H16, -1, 0, "", "", "", "", 0, D { 0, 0, 0 }, FBreak },
            { "DB3 dancefloor", "X.........x..x..", "", "....X.......X...", "", H8, 1, 5, "", "", "", "", 0, D { 0, 0, 0 }, FSnare16 },
            { "DB4 breaks",     "X.........x.x...", "X.......o.x.....", "....X.....o.X..o", "....X..o..o.X.o.", H8, -1, 0, "", "", "", "", 0, D { 0, 0, 0 }, FBreak },
            { "DB5 liquid",     "X.........x.....", "X.....x...x.....", "....X.......X...", "....X.......X..o", HOff8, -1, 1, "", "", "", "", 0, D { 0, 0, 0 }, FBreak } } },
        // ---- garage: 2-step kicks, 16th hats swung 60-64 %, skipping ghost snares
        { "Garage", 130, 136, 62, 54, 52, 60, 2, 0, 30, 40, BassNone, 0.0f, 2, K2Off, ClapLayer, S2Off, 0, 0, BTrip, "..............x.", ".........x...x..", FNoKickUnderSnare, 2, 4, 0.03f, 0.55f,
          { KRim, KSnap, KClave, KBell, KPerc, KTomHi },
          { { "G1 two-step",   "x......x..x.....", "x......x..x..x..", "....X.......X...", "....X..o....X...", HRun16, 1, 6, "", "", "", "", 0, D { 0, 0, 0 }, FGarage },
            { "G2 4x4",        "X...X...X...X...", "", "....X.......X...", "",                 HBroken, 0, 1, "...x.......x....", "", "", "", 0, D { 0, 0, 0 }, FGarage },
            { "G3 skippy",     "x.........x..x..", "x......x..x.....", "....X.......X...", "....X.......X.x.", H16, -1, 0, "..x.......x.....", "......x.......x.", "", "", 1, D { 0, 0, 0 }, FGarage },
            { "G4 broken",     "x.....x...x.....", "x.....x...x...x.", "....X.......X...", "",                 HRun16, 1, 6, "", "", "", "", 0, D { 0, 0, 0 }, FGhostFlam },
            { "G5 bump",       "x......x.x......", "x......x.x...x..", "....X.......X...", "....X......oX...", HShuffle, 1, 0, "", "", "", "", 1, D { 0, 0, 0 }, FGarage } } },
        // ---- jersey club: the five-kick bounce, triplet-feel kicks, chopped vox, kick runs into the phrase end
        { "Jersey Club", 135, 142, 50, 50, 50, 50, 0, 0, 0, 40, BassShort, 0.0f, 1, K2Off, ClapLayer, S2Off, 0, 0, BTrip, "..............x.", "", FNoKickUnderSnare, 3, 7, 0.08f, 0.7f,
          { KVox, KCowbell, KSnap, KShaker, KRim, KTomLo },
          { { "J1 bounce",      "x..x..x.x.x.....", "x..x..x.x.x..x.x", "....x.......x...", "",                 H8, -1, 0, "..x.....x.x.....", "", "", "", 0, D { 0, 0, 0 }, FJersey },
            { "J2 triplet",     "x..x..x..x..x...", "x..x..x..x..x.x.", "....x.......x...", "",                 H8, 1, 0, "..x.....x.x.....", "", "", "", 0, D { 0, 0, 0 }, FJersey },
            { "J3 bed squeak",  "x..x..x.x.x.....", "x..x..x.x.x.x.x.", "....x.......x...", "",                 HOff8, -1, 0, "...x...x...x...x", "..x.....x.x.....", "", "", 0, D { 0, 0, 0 }, FKickRun },
            { "J4 half bounce", "x.....x.x.x.....", "x..x..x.x.x.....", "....x.......x...", "....x.......x.x.", H8, -1, 0, "..x.......x.x...", "", "", "", 0, D { 0, 0, 0 }, FJersey },
            { "J5 stutter",     "x.xx..x.x.x.....", "x.xx..x.x.x..x..", "....x.......x...", "",                 H8, -1, 0, "..x.....x.x....x", "", "", "", 1, D { 0, 0, 0 }, FJersey } } },
        // ---- afrobeats: 100-112, the 3-3-2-ish kick, accented shaker 16ths, conga / bongo / clave interlock
        { "Afrobeats", 100, 112, 52, 50, 50, 54, 3, 0, 0, 40, BassShort, 0.0f, 1, K2Off, ClapLayer, S2Off, 0, 0, B32, "..............x.", "", 0, 2, 5, 0.05f, 0.6f,
          { KRim, KConga, KClave, KBongo, KCowbell, KTamb, KPerc },
          { { "AF1 lagos",    "x..x...x..x.....", "x..x...x..x...x.", "....x.......x...", "",                 HBroken, -1, 1, "...x..x....x..x.", "x.....x.........", "..x.x....x.x....", "x..x..x...x.x...", 0, D { 0, 0, 0 }, FConga },
            { "AF2 log",      "x.......x.......", "x.......x.....x.", "....x.......x...", "",                 H8, -1, 2, "...x......x..x..", "..x.x.....x.x...", "", "......x.......x.", 0, D { 0, 0, 0 }, FNone },
            { "AF3 highlife", "x..x..x...x..x..", "x..x..x...x..x.x", "....x.......x...", "",                 H8, -1, 1, "", "..x.x....x.x....", "......x...x.....", "x..x..x...x.x...", 0, D { 0, 0, 0 }, FConga },
            { "AF4 bounce",   "x......x..x.....", "x......x..x..x..", "....x.......x...", "....x......ox...", HOff8, -1, 2, "...x..x....x..x.", "x.....x...x.....", "..x.......x.x...", "", 0, D { 0, 0, 0 }, FConga },
            { "AF5 dembow",   "x...x...x...x...", "x...x...x...x.x.", "...x..x....x..x.", "",                 H8, -1, 1, "", "..x.x....x.x....", "", "x..x..x...x.x...", 0, D { 0, 0, 0 }, FConga } } },
        // ---- phonk: the cowbell leads; Memphis vs drift
        { "Phonk", 128, 134, 50, 50, 50, 50, 1, 0, 15, 46, BassFollow, 0.0f, 1, K2Off, ClapLayer, S2Off, 0, 0, B32 | B32x4, "...x..xx...x..xx", ".........x...x..", FNoKickUnderSnare, 2, 5, 0.03f, 0.5f,
          { KCowbell, KVox, KPerc, KRim, KShaker, KSnap },
          { { "P1 memphis",  "x..x..x...x.....", "x..x..x...x..x..", "....X.......X...", "",                 H8, -1, 0, "x.xx.x.xx.x.x.x.", "", "", "", 2, D { 0, 0, 0 }, FSnare16 },
            { "P2 ride",     "x.....x...x.....", "x.....x...x..x..", "....X.......X...", "",                 H8, -1, 0, "x.x.xx.x.x.xx.x.", "B:..............x.", "", "", 1, D { 0, 0, 0 }, FSnareRoll },
            { "P3 drive",    "x..x......x..x..", "x..x......x..x.x", "....X.......X...", "....X.......X..o", H16, -1, 0, "x.xx.x.xx.xxx.xx", "", "", "", 3, D { 0, 0, 0 }, FSnare16 },
            { "P4 dark",     "x......x..x.....", "x......x..x...x.", "....X.......X...", "",                 H8, 1, 0, "x..x..x.x..x..x.", "", "", "", 2, D { 0, 0, 0 }, FSnareRoll },
            { "P5 bounce",   "x.x...x...x.x...", "x.x...x...x.x.x.", "....X.......X...", "",                 H8, -1, 0, "x.xx.x.xx.x.x.x.", "", "..x...x...x...x.", "", 2, D { 0, 0, 0 }, FSnare16 } } },
        { "Phonk Drift", 126, 132, 50, 50, 50, 50, 0, 0, 5, 46, BassSlides, 0.3f, 0, K2Off, ClapLayer, S2Off, 0, 0, B32, "..............x.", ".........x......", FNoKickUnderSnare | FFxEnds, 2, 5, 0.03f, 0.5f,
          { KCowbell, KPerc, KRim, KShaker, KSnap, KBell },
          { { "PD1 drift",      "x..x..x...x.....", "x..x..x...x..x..", "....X.......X...", "",                 HOff8, -1, 0, "x.x.x.x.x.x.x.x.", "", "", "", 0, D { 0, 0, -5 }, FDrop },
            { "PD2 night",      "x.....x...x.....", "x.....x...x..x..", "....X.......X...", "",                 H8, -1, 0, "x.xx.x.xx.x.x.x.", "", "", "", 1, D { 0, 0, 0 }, FDrop },
            { "PD3 aggressive", "x.x...x...x.x...", "x.x...x...x.x.x.", "....X.......X...", "",                 H16, -1, 0, "x.x.x.x.x.x.x.x.", "", "", "", 2, D { 0, 3, 0 }, FSnare16 },
            { "PD4 wide",       "x......x..x.....", "x......x..x....x", "....X.......X..o", "",                 HOff8, 7, 0, "x.xx.x.xx.x.x.x.", "", "", "......x.......x.", 0, D { 0, 0, 0 }, FDrop } } },
        // ---- pop: 100-114, clean backbeats, programmed tom fills
        { "Pop", 100, 110, 50, 50, 50, 50, 1, 0, 10, 46, BassNone, 0.0f, 1, K2Downbeat, ClapLayer, S2Off, 0, 1, B32, "..............x.", ".........x...x..", FNoKickUnderSnare, 1, 5, 0.0f, 0.4f,
          { KSnap, KRim, KTomLo, KTomHi, KFx, KPerc },
          { { "PP1 radio",      "x.......x.x.....", "x.......x.x..x..", "....X.......X...", "",                 H8, -1, 5, "....x.......x...", "", "", "", 0, D { 0, 0, 0 }, FToms },
            { "PP2 four",       "X...X...X...X...", "", "....X.......X...", "",                                 HOff8, 0, 1, "", "", "", "", 0, D { 0, 0, 0 }, FMachine },
            { "PP3 half-time",  "x.........x.....", "x.........x..x..", "........X.......", "",                 H8, -1, 5, "....x.......x...", "", "", "", 0, D { 0, 0, 0 }, FToms },
            { "PP4 bounce",     "x..x....x..x....", "x..x....x..x..x.", "....X.......X...", "",                 H16, -1, 0, "", "..x.....x.x.....", "", "", 0, D { 0, 0, 0 }, FSnare16 },
            { "PP5 syncopated", "x.....x...x.....", "x.....x...x...x.", "....X.......X...", "....X.......X.o.", H8, 1, 5, "", "", "", "", 0, D { 0, 0, 0 }, FToms } } },
        { "POP SHINE", 106, 114, 50, 50, 50, 50, 0, 0, 5, 46, BassNone, 0.0f, 1, K2Downbeat, ClapLayer, S2Gated, 0, 1, B32, "..............x.", ".........x...x..", FNoKickUnderSnare, 1, 5, 0.0f, 0.4f,
          { KSnap, KTomLo, KTomHi, KPerc },
          { { "PS1 shine",   "x.......x.x.....", "x.......x.x..x..", "....X.......X...", "", HSampler, -1, 5, "", "", "", "", 0, D { 0, 0, 0 }, FMachine },
            { "PS2 gated",   "x.........x.....", "x.........x..x..", "....X.......X...", "", H8, -1, 5, "", "", "", "", 0, D { 0, 0, 0 }, FMachine },
            { "PS3 dance",   "X...X...X...X...", "X...X...X...X.x.", "....X.......X...", "", HOff8, 0, 1, "", "", "", "", 0, D { 0, 0, 0 }, FToms },
            { "PS4 ballad",  "x.............x.", "x.........x.....", "....X.......X...", "", H8, -1, 0, "..x.....x.x.....", "", "", "", 0, D { 0, 0, 0 }, FToms },
            { "PS5 uptempo", "X...X...X...X...", "", "....X.......X...", "", H16, -1, 1, "....x.......x...", "", "", "", 0, D { 0, 0, 0 }, FMachine } } },
        // ---- cinematic: 80-96, sparse impacts, tom ostinatos, a build into the phrase end
        { "Cinematic", 80, 96, 50, 50, 50, 50, 2, 0, 0, 40, BassNone, 0.0f, 0, K2Downbeat, ClapLayer, S2Off, 0, 1, B32, "..............x.", "", FFxEnds, 1, 4, 0.0f, 0.5f,
          { KTomLo, KTomMid, KTomHi, KBell, KPerc },
          { { "C1 march",     "x.......x.......", "", "........X.......", "", HNone, -1, 0, "x..x..x.x..x..x.", "..x.....x.x.....", "", "", 0, D { 0, 0, 0 }, FToms },
            { "C2 pulse",     "X...o...x...o...", "", "........X.......", "", HQuarter, -1, 0, "x.......x.......", "", "......x.......x.", "", 0, D { 0, 0, 0 }, FTomsBig, 2 },
            { "C3 tension",   "x...............", "x.........x.....", "........X.......", "........X.....xx", H16, -1, 0, "", "", "", "", 0, D { 0, 0, 0 }, FTomsBig, 2 },
            { "C4 war drums", "x.....x.x.......", "x.....x.x...x...", "........X.......", "", HNone, -1, 0, "x.x.x.x.x.x.x.x.", "..x...x...x...x.", "B:........x.x.x.x.", "", 0, D { 0, 0, 0 }, FTomsBig },
            { "C5 heartbeat", "x.x.............", "x.x.....x.x.....", "........X.......", "", HNone, 1, 0, "......x.......x.", "", "", "", 0, D { 0, 0, 0 }, FToms, 3 } } },
        // ---- IDM: controlled irregularity, retrigs, odd groupings, gaps with a logic
        { "IDM Glitch", 150, 170, 50, 50, 50, 50, 0, 0, 20, 50, BassSustain, 0.0f, 1, K2Off, ClapLayer, S2Off, 0, 0, B64 | BAccel | BPitch | B32x4, "..x...xx..x...xx", ".....x.....x..x.", 0, 2, 6, 0.08f, 0.9f,
          { KPerc, KFx, KBell, KRim, KSnap },
          { { "I1 stutter", "x.....x...x.....", "x.....x.x.x...x.", "....X.......X...", "....X.......X..3", HTrapHoles, -1, 0, "x..x..x..x..x..x", "......x.........", "..x.......x...x.", "", 4, D { 0, 0, 0 }, FGlitch },
            { "I2 fives",   "x....x....x....x", "x....x....x.....", "....X.......X...", "",                 H16, -1, 0, "x..x..x..x..x..x", "x...x...x...x...", ".....x.....x....", "", 5, D { 0, 7, 0 }, FGlitch },
            { "I3 sevens",  "x......x......x.", "", "....X.......X...", "....X...8...X...", HTrapHoles, -1, 0, "", "..............x.", "..x.......x.x.x.", "............x...", 6, D { 0, 0, 0 }, FGlitch },
            { "I4 gaps",    "x.....x.........", "........x.x...x.", "....X.......X...", "....4.......x.x.", HBroken, -1, 0, "x..x..x..x..x..x", "", "..x.......x...x.", "", 3, D { 0, 0, 0 }, FGlitch },
            { "I5 machine", "x.x...x.x...x.x.", "x.x...x.x...x.8.", "....X.......X...", "",                 H16, -1, 0, "x...x...x...x...", "......x.......x.", "", "", 4, D { 0, 0, 5 }, FGlitch } } },
        // ---- classic drum machines of their eras (ClassicPacks.h kits)
        // ANALOG BOOM: the 8-series voice: the electro / old-school programming idiom (kick on 1 and the "a" of 2, snare + clap, the cowbell / claves interlock, tom runs, the long BOOM on the downbeat)
        { "ANALOG BOOM", 108, 118, 50, 50, 50, 50, 0, 0, 0, 44, BassBoom, 0.0f, 0, K2Off, ClapLayer, S2Off, 0, 1, B32, "..............x.", "", FNoKickUnderSnare, 2, 5, 0.03f, 0.5f,
          { KCowbell, KClave, KRim, KConga, KTomLo, KTomMid, KTomHi },
          { { "AB1 electro",    "x.....x...x.....", "x.....x...x..x..", "....X.......X...", "",                 HSampler, 1, 0, "x..x..x...x.x...", "..x....x..x....x", "", "", 0, D { 0, 0, 0 }, FMachine },
            { "AB2 planet",     "x..x......x..x..", "x..x.....x...x..", "....X.......X...", "",                 H8, 4, 2, "..x.......x.....", "", "", "..........x.....", 0, D { 0, 0, 0 }, FToms },
            { "AB3 old school", "x.......x.x.....", "x.......x.x..x..", "....X.......X...", "",                 HSampler, 1, 2, "", "", "...x......x.....", "", 0, D { 0, 0, 0 }, FMachine },
            { "AB4 boom",       "x.........x.....", "x.........x..x..", "....X.......X...", "....X.......X..o", H8, 2, 0, "x..x..x...x.x...", "....x.......x...", "", "", 0, D { 0, 0, 0 }, FMachine },
            { "AB5 miami",      "x..x..x...x..x..", "x..x..x...x..x.x", "....X.......X...", "",                 H16, 4, 0, "..x...x...x...x.", "", "", "..x.x....x.x....", 0, D { 0, 0, 0 }, FToms } } },
        // DIGITAL EIGHTIES: the first sampled machines: tight, dry, programmed (kick on 1, 3 and the "and" of 3, big backbeat, cabasa 16ths, tambourine, cowbell, tom runs)
        { "DIGITAL EIGHTIES", 100, 108, 50, 50, 50, 50, 0, 0, 0, 44, BassNone, 0.0f, 0, K2Off, ClapLayer, S2Off, 0, 1, B32, "..............x.", "", FNoKickUnderSnare, 2, 5, 0.0f, 0.45f,
          { KTamb, KCowbell, KClave, KConga, KRim, KTomLo, KTomMid, KTomHi },
          { { "DE1 linn",      "x.......x.x.....", "x.......x.x..x..", "....X.......X...", "",                 HSampler, 1, 2, "....x.......x...", "", "", "", 0, D { 0, 0, 0 }, FMachine },
            { "DE2 synthpop",  "X...X...X...X...", "", "....X.......X...", "",                                 H8, 0, 2, "", "..x...x...x...x.", "", "", 0, D { 0, 0, 0 }, FToms },
            { "DE3 funk",      "x.....x...x..x..", "x.....x...x..x.x", "....X.......X...", "....X.......X.o.", H16, -1, 0, "", "", "..x....x..x....x", "..........x.x...", 0, D { 0, 0, 0 }, FMachine },
            { "DE4 ballad",    "x.........x.....", "x.........x...x.", "....X.......X...", "",                 H8, 1, 2, "x.x.x.x.x.x.x.x.", "", "", "", 0, D { 0, 0, 0 }, FToms },
            { "DE5 half-time", "x......x........", "x......x..x.....", "........X.......", "",                 HSampler, 2, 2, "", "x...x...x...x...", "", "", 0, D { 0, 0, 0 }, FMachine } } },
        // CRUNCH TWELVE (identity decided from its kit + starter beats: a 12-bit 26 kHz sampler with dropped-sample pitch, a FILTERED second snare and an OPEN second kick, hats
        // swung at 58 % in the starter, vinyl crackle): the gritty late-80s / early-90s sampler hip hop idiom: the kick on 1 and the "a of 3" with an occasional OPEN kick answer,
        // the filtered snare playing the ghost notes under the main snare, hats swung 60 % with soft "o" steps and a "xo" skip at the bar end, conga / rim colour, sparse, loose
        { "CRUNCH TWELVE", 88, 96, 60, 52, 50, 58, 4, 2, 50, 44, BassNone, 0.0f, 2, K2Soft, ClapIfNoSnare, S2Ghosts, 0, 0, B32, "..............x.", ".........x......", FNoKickUnderSnare | FLoose, 2, 4, 0.05f, 0.6f,
          { KShaker, KRim, KConga, KSnap, KTomLo },
          { { "CT1 crunch",     "x.........x..x..", "x......o..x..x..", "....X.......X...", "....X..o.o..X...", HCrunch, 1, 0, "", "", "", "", 0, D { 0, 0, 0 }, FGhostFlam },
            { "CT2 chop",       "x.....x...x.....", "x.....x...x...o.", "....X.......X...", "",                 H8, -1, 3, "", "", "", "", 0, D { 0, 0, 0 }, FNone },
            { "CT3 heavy",      "x.......xx......", "x.....o.xx......", "....X.......X...", "....X.......X.o.", HCrunch, 1, 0, "", "", "..........x.....", "", 0, D { 0, 0, 0 }, FGhostFlam },
            { "CT4 stagger",    "x..x......x.....", "x..x.....o.x....", "....X.......X...", "",                 H8, 2, 0, "", "..x.......x.....", "", "", 0, D { 0, 0, 0 }, FGhostFlam },
            { "CT5 slow grind", "x.............x.", "x.........x.....", "....X.......X...", "....X......oX...", HCrunch, -1, 0, "", "", "", "", 0, D { 0, 0, 0 }, FNone } } },
        // SWING TWELVE (identity decided from its kit + starter beats: the 12-bit 40 kHz production centre, swung at 61 % in the starter, a shaker and conga, a rim figure):
        // the golden-era pad-sampler idiom: heavier 16th swing (63 %) than BOOM BAP CLASSIC, a doubled kick on 1 and the "and of 2", snare on 2 and 4 with its own ghost notes,
        // soft shaker 16ths and conga / rim / wood-block colour, a clap only on the last backbeat of bars 2 and 4
        { "SWING TWELVE", 90, 98, 63, 55, 52, 60, 3, 1, 40, 44, BassNone, 0.0f, 2, K2Off, ClapLast, S2Off, 0, 0, B32, "..............x.", ".........x...x..", FNoKickUnderSnare | FLoose, 2, 4, 0.05f, 0.6f,
          { KRim, KConga, KClave, KBell, KSnap, KTomLo },
          { { "ST1 swing",    "x......x..x.....", "x......x..x..x..", "....X.......X...", "....X..o....X...", HSwing, -1, 5, "", "", "", "", 0, D { 0, 0, 0 }, FGhostFlam },
            { "ST2 lope",     "x.........x..x..", "x.........x..x..", "....X.......X...", "",                 H16, -1, 0, "..x.......x.....", "", "", "", 0, D { 0, 0, 0 }, FNone },
            { "ST3 knock",    "x.x.......x.....", "x.x......x.x....", "....X.......X...", "",                 HSampler, -1, 5, "", "......x.......x.", "", "", 0, D { 0, 0, 0 }, FGhostFlam },
            { "ST4 stroll",   "x.....x...x.....", "x.....x...x...x.", "....X.......X...", "....X.......X.o.", HSwing, 1, 0, "", "", "..x.......x.....", "", 0, D { 0, 0, 0 }, FGhostFlam },
            { "ST5 backspin", "x..x......x.....", "x..x.....x.x....", "....X.......X...", "",                 H16, -1, 5, "...x.......x....", "", "", "", 0, D { 0, 0, 0 }, FNone } } },
    };
    return P;
}
inline int profileIndex(const juce::String& kit) { const auto& P = profiles(); for (int i = 0; i < (int) P.size(); ++i) if (kit == P[(size_t) i].kit) return i; return -1; }
// the GENERATE style index for a kit: the kits with a generator of their own keep it; every other kit goes through its profile (unknown kit names: the Trap profile)
inline int styleFor(const juce::String& kit) {
    if (kit == "DRILL") return 2; if (kit == "LATIN" || kit == "Reggaeton") return 3; if (kit == "TRAP CLASSIC") return 4;
    const int i = profileIndex(kit); return kStyleBase + juce::jmax(0, i);
}
inline bool isStyle(int style) { return style >= kStyleBase && style < kVaryBase + (int) profiles().size(); }
inline bool isVary(int style) { return style >= kVaryBase; }
inline const Profile& profileOf(int style) { const int i = style >= kVaryBase ? style - kVaryBase : style - kStyleBase; return profiles()[(size_t) juce::jlimit(0, (int) profiles().size() - 1, i)]; }
inline int tempoFor(int style, int seed) { const auto& P = profileOf(style); return P.bpmLo + (int) (mix((uint32_t) seed * 2654435761u + (uint32_t) (style * 977 + 13)) % (uint32_t) (P.bpmHi - P.bpmLo + 1)); }
inline int family(const Profile& P, int seed) { const int n = (int) P.fams.size(); return (int) ((((int64_t) seed * 7) % n + n) % n); }   // consecutive seeds always change the family (7 is coprime to every family count here)
inline int rollOrder(int seed) { return (int) (((int64_t) seed * 5 % kRolls + kRolls) % kRolls); }

// ---------------- helpers
inline int cellLen(const char* c, int& bars, int& from) {   // decodes a cell prefix: bars = 0 all, 1 bars 2/4, 2 bar 4 only; from = index of the first step char; returns a polymeter length (0 = none)
    bars = 0; from = 0; int len = 0;
    if (!c || !*c) return 0;
    if (c[0] == 'B' && c[1] == ':') { bars = 1; from = 2; } else if (c[0] == '4' && c[1] == ':') { bars = 2; from = 2; }
    else if (c[0] == 'L' && c[1] >= '0' && c[1] <= '9') { len = std::atoi(c + 1); while (c[from] && c[from] != ':') ++from; if (c[from]) ++from; }
    return juce::jlimit(0, 16, len);
}
inline bool barPlays(int which, int bar) { return which == 0 || (which == 1 && (bar & 1)) || (which == 2 && bar == kBars - 1); }
inline int velOf(char c, int x, int X, int o) { return c == 'X' ? X : (c == 'x' ? x : (c == 'o' ? o : (c == 'f' ? x : (c >= '2' && c <= '8' ? x - 10 : 0)))); }

struct Ctx {   // one candidate's working state
    Pattern& p; const Profile& P; const Fam& F; const std::array<uint8_t, kTracks>& kinds; const std::array<uint8_t, kTracks>& roles; const std::array<bool, kTracks>& lockedTrack;
    juce::Random R; int rollO;
    std::array<bool, kLen> kick {}, kickSoft {}, kickAcc {}, snMain {}, snAny {}, hatMute {};
    std::array<int8_t, kLen> snChar {};
    int trackOf(int kind) const { for (int t = 0; t < kTracks; ++t) if (kinds[(size_t) t] == kind && !lockedTrack[(size_t) t]) return t; return -1; }
    bool has(int kind) const { for (int t = 0; t < kTracks; ++t) if (kinds[(size_t) t] == kind) return true; return false; }
    std::array<Step, kMaxSteps>* row(int kind) { const int t = trackOf(kind); return t < 0 ? nullptr : &p.steps[(size_t) t]; }
    int jit(int v, int r) { return r > 0 ? v + R.nextInt(2 * r + 1) - r : v; }
    Step& hit(std::array<Step, kMaxSteps>& row, int s, int v) { auto& st = row[(size_t) (((s % kLen) + kLen) % kLen)]; st = Step(); st.vel = (uint8_t) juce::jlimit(1, 127, v); return st; }
    int swingNudge(int sw, int s) const { return (s & 1) ? juce::jlimit(-50, 50, (sw - 50) * 2) : 0; }
    void human(Step& st, int amount) { if (amount > 0) st.nudge = (int8_t) juce::jlimit(-50, 50, st.nudge + R.nextInt(2 * amount + 1) - amount); }
};

// a 16-char cell laid over the 4 bars onto a row (percussion / toms / fx); honours the B: / 4: / L<n>: prefixes
inline void layCell(Ctx& c, std::array<Step, kMaxSteps>& row, int t, const char* cell, int vx, int vX, int vo, int sw, int hum, bool onlyAfter = false) {
    int which = 0, from = 0; const int len = cellLen(cell, which, from); const char* s = cell + from; const int n = (int) std::strlen(s); if (n == 0) return;
    if (len > 0) {   // polymeter: one cycle of its own length
        c.p.trackLen[(size_t) t] = (uint8_t) len;
        for (int k = 0; k < len && k < n; ++k) { const int v = velOf(s[k], vx, vX, vo); if (!v) continue; auto& st = c.hit(row, k, c.jit(v, 3)); st.nudge = (int8_t) c.swingNudge(sw, k); c.human(st, hum); }
        return;
    }
    for (int bar = 0; bar < kBars; ++bar) {
        if (!barPlays(which, bar)) continue;
        for (int k = 0; k < 16 && k < n; ++k) { const int v = velOf(s[k], vx, vX, vo); if (!v) continue; const int step = bar * 16 + k;
            if (onlyAfter && c.hatMute[(size_t) step]) continue;
            auto& st = c.hit(row, step, c.jit(v, 3)); st.nudge = (int8_t) c.swingNudge(sw, k); c.human(st, hum);
            if (s[k] >= '2' && s[k] <= '8') { st.roll = (uint8_t) (s[k] - '0'); st.rate = 16; st.velRamp = 40; } }
    }
}

// ---------------- the phrase-end fill (bar 4)
inline void writeFill(Ctx& c) {
    const int o = (kBars - 1) * 16, f = c.F.fill;
    auto* sn = c.row(KSnare); if (!sn) sn = c.row(KClap); auto* kk = c.row(KKick); auto* hh = c.row(KHat); auto* oh = c.row(KOpen); auto* cl = c.row(KClap);
    auto* tl = c.row(KTomLo); auto* tm = c.row(KTomMid); auto* th = c.row(KTomHi); if (!tl) tl = tm ? tm : th; if (!th) th = tm ? tm : tl; if (!tm) tm = th;
    auto* cg = c.row(KConga); auto* bg = c.row(KBongo); if (!cg) cg = bg; auto* vx = c.row(KVox); auto* fx = c.row(KFx);
    auto toms = [&](int fromK, bool big) {   // a tom run high -> low, building
        const int n = 16 - fromK; for (int i = 0; i < n; ++i) { auto* r = i < n / 3 ? th : (i < 2 * n / 3 ? tm : tl); if (!r) continue; auto& st = c.hit(*r, o + fromK + i, 70 + i * (big ? 5 : 8)); st.nudge = (int8_t) c.swingNudge(c.P.swPerc, fromK + i); }
        if (!tl && !th && !tm && sn) for (int i = 0; i < n; ++i) { auto& st = c.hit(*sn, o + fromK + i, 60 + i * 8); st.decay = 60; }   // (no toms: a snare build)
    };
    switch (f) {
        case FSnare16: if (sn) for (int k = 12; k < 16; ++k) c.hit(*sn, o + k, 56 + (k - 12) * 14); if (cl && cl != sn) c.hit(*cl, o + 15, 100); break;
        case FSnareRoll: if (sn) { auto& st = c.hit(*sn, o + 14, 80); st.roll = c.R.nextBool() ? 4 : 3; st.rate = (uint8_t) (st.roll == 3 ? 6 : 8); st.velRamp = 60; if (c.R.nextBool()) st.pitchRamp = 3; } break;
        case FToms: toms(c.R.nextBool() ? 12 : 10, false); break;
        case FTomsBig: toms(8, true); if (sn) { c.hit(*sn, o + 14, 100); c.hit(*sn, o + 15, 118); } break;
        case FKickRun: if (kk) for (int k = 12; k < 16; ++k) { auto& st = c.hit(*kk, o + k, 90 + (k - 12) * 8); c.kick[(size_t) (o + k)] = true; juce::ignoreUnused(st); } break;
        case FHardKick: if (kk) { for (int k : { 8, 10, 12, 13, 14, 15 }) { c.hit(*kk, o + k, k < 12 ? 104 : 96 + (k - 12) * 8); c.kick[(size_t) (o + k)] = true; } } if (sn) c.hit(*sn, o + 12, 110); break;
        case FHouse: if (cl || sn) { auto* r = cl ? cl : sn; for (int k : { 10, 12, 14, 15 }) c.hit(*r, o + k, k == 10 ? 70 : 80 + (k - 12) * 10); } if (oh) c.hit(*oh, o + 14, 84); break;
        case FBreak: if (sn) { c.hit(*sn, o + 10, 72); c.hit(*sn, o + 13, 96); c.hit(*sn, o + 15, 60); } if (kk) { c.hit(*kk, o + 12, 100); c.kick[(size_t) (o + 12)] = true; } break;
        case FGlitch: {
            for (int k = 12; k < 16; ++k) { auto* r = (k & 1) ? (hh ? hh : sn) : (sn ? sn : hh); if (!r) continue; auto& st = c.hit(*r, o + k, 70 + (k - 12) * 10); st.roll = (uint8_t) (2 + c.R.nextInt(5)); st.rate = (uint8_t) (c.R.nextBool() ? 16 : 32); st.pitchRamp = (int8_t) (c.R.nextInt(15) - 7); st.velRamp = 50; st.accel = (int8_t) (c.R.nextInt(80) - 20); }
            if (fx) { auto& st = c.hit(*fx, o + 15, 90); st.roll = 4; st.rate = 32; st.pitchRamp = 12; }
            break; }
        case FDrop: if (fx) c.hit(*fx, o + 12, 90); if (sn && c.R.nextBool()) c.hit(*sn, o + 14, 64); break;   // the last beat opens up (only the swell and a fx hit)
        case FMachine: if (sn) { c.hit(*sn, o + 8, 96); c.hit(*sn, o + 10, 100); } toms(12, false); break;
        case FJersey: if (kk) { for (int k : { 8, 10, 12, 13, 14, 15 }) { c.hit(*kk, o + k, k < 12 ? 100 : 92 + (k - 12) * 8); c.kick[(size_t) (o + k)] = true; } } if (vx) { c.hit(*vx, o + 12, 90); c.hit(*vx, o + 14, 96); } break;
        case FGhostFlam: if (sn) { auto& g = c.hit(*sn, o + 14, c.P.ghostVel + 8); g.nudge = (int8_t) c.swingNudge(c.P.swSn, 14); auto& st = c.hit(*sn, o + 15, 70); st.flam = 16; } if (kk && c.R.nextBool()) { c.hit(*kk, o + 13, 84); c.kick[(size_t) (o + 13)] = true; } break;
        case FConga: if (cg) for (int k = 12; k < 16; ++k) { auto* r = (k & 1) && bg && bg != cg ? bg : cg; auto& st = c.hit(*r, o + k, 76 + (k - 12) * 10); st.nudge = (int8_t) c.swingNudge(c.P.swPerc, k); } break;
        case FGarage: if (sn) { c.hit(*sn, o + (c.kick[(size_t) (o + 13)] ? 14 : 13), 92); auto& g = c.hit(*sn, o + 15, 56); g.nudge = (int8_t) c.swingNudge(c.P.swSn, 15); } if (oh) c.hit(*oh, o + 14, 80); break;   // (the skipped snare lands where the kick is not)
        default: break;
    }
}

// ---------------- one candidate
inline void writeCandidate(Ctx& c, const std::array<Step, kMaxSteps>* keepKick, int keepLen) {
    auto& p = c.p; const auto& P = c.P; const auto& F = c.F;
    p.length = kLen; p.res = 1; p.evolveStyle = 0; p.evolve = 0.0f; p.fillEvery = 0;
    for (int t = 0; t < kTracks; ++t) { if (c.lockedTrack[(size_t) t] || c.roles[(size_t) t] == RoleBreak) continue; for (auto& st : p.steps[(size_t) t]) st = Step(); p.trackLen[(size_t) t] = 0; }
    const int fillAt = fillFrom(F.fill), o3 = (kBars - 1) * 16;
    // ---- the snare map first (the kicks keep out of its way where the style says so)
    for (int bar = 0; bar < kBars; ++bar) { const char* cell = ((bar & 1) && F.snB && *F.snB) ? F.snB : F.sn;
        for (int k = 0; k < 16 && cell[k]; ++k) { const char ch = cell[k]; if (ch == '.') continue; const int s = bar * 16 + k; c.snChar[(size_t) s] = (int8_t) ch; c.snAny[(size_t) s] = true; if (ch == 'X' || ch == 'x' || ch == 'f') c.snMain[(size_t) s] = true; } }
    // ---- the kick map: the family's cells, bar 4 making room for the fill, soft extras in bars 2 / 4, never under the main snare where forbidden
    if (keepKick) { for (int s = 0; s < kLen; ++s) { const auto& st = (*keepKick)[(size_t) (s % juce::jmax(1, keepLen))]; c.kick[(size_t) s] = st.vel > 0; c.kickSoft[(size_t) s] = st.vel > 0 && st.vel < 85; c.kickAcc[(size_t) s] = st.vel >= 110; } }
    else for (int bar = 0; bar < kBars; ++bar) {
        const char* cell = ((bar & 1) && F.kB && *F.kB) ? F.kB : F.kA;
        for (int k = 0; k < 16 && cell[k]; ++k) { const int s = bar * 16 + k; char ch = cell[k];
            if (bar == kBars - 1 && k >= fillAt) ch = '.';
            if ((P.flags & FNoKickUnderSnare) && c.snMain[(size_t) s] && ch != 'X') ch = '.';   // (an accented X kick may sit under the snare: the 4x4 garage / dance-pop families)
            if (ch == '.' && (bar & 1) && P.kickExtra && P.kickExtra[k] == 'x' && !c.snMain[(size_t) s] && !(bar == kBars - 1 && k >= fillAt) && c.R.nextFloat() < 0.3f) ch = 'o';
            if (ch == 'x' || ch == 'X' || ch == 'o') { c.kick[(size_t) s] = true; c.kickSoft[(size_t) s] = ch == 'o'; c.kickAcc[(size_t) s] = ch == 'X'; } }
    }
    for (int s = 0; s < kLen; ++s) c.hatMute[(size_t) s] = s >= o3 + fillAt && F.fill != FHouse && F.fill != FGhostFlam && F.fill != FGarage && F.fill != FSnareRoll;
    const int humK = (P.flags & FLoose) ? P.human / 2 : 0;
    // ---- kick (and the second kick)
    if (auto* r = c.row(KKick)) for (int s = 0; s < kLen; ++s) if (c.kick[(size_t) s] && !(P.kick2 == K2Soft && c.kickSoft[(size_t) s] && c.has(KKick2))) {
        const int k = s % 16; const bool down = k == 0 || c.kickAcc[(size_t) s]; auto& st = c.hit(*r, s, c.kickSoft[(size_t) s] ? c.jit(74, 6) : (down ? 118 : c.jit(104, 4)));
        st.nudge = (int8_t) c.swingNudge(P.swKick, k); if (!down) c.human(st, humK); }
    if (auto* r = c.row(KKick2)) switch (P.kick2) {
        case K2Downbeat: for (int b = 0; b < kBars; ++b) c.hit(*r, b * 16, 100); break;
        case K2Double: for (int s = 0; s < kLen; ++s) if (c.kick[(size_t) s]) c.hit(*r, s, c.kickSoft[(size_t) s] ? 60 : 88); break;
        case K2OffPulse: for (int s = 2; s < kLen; s += 4) c.hit(*r, s, c.jit(58, 4)); break;
        case K2Boom: c.hit(*r, 0, 104); c.hit(*r, 32, 100); if (c.R.nextFloat() < 0.5f) c.hit(*r, 16 + 10, 80); break;
        case K2Soft: for (int s = 0; s < kLen; ++s) if (c.kick[(size_t) s] && c.kickSoft[(size_t) s]) c.hit(*r, s, c.jit(92, 4)); break;
        default: break;
    }
    // ---- the 808
    if (auto* r = c.row(KBass); r && P.bass != BassNone) {
        static const int roots[] { -2, -1, 0, 1, 2, -4, -3 }; const int root = roots[c.R.nextInt(7)];
        std::vector<int> at; std::vector<bool> slide;
        if (P.bass == BassBoom) { at.push_back(0); at.push_back(32); if (c.R.nextFloat() < 0.5f) at.push_back(16 + 10); }
        else if (P.bass == BassSustain) { for (int b = 0; b < kBars; ++b) { at.push_back(b * 16); if (c.R.nextFloat() < 0.6f) { int k = 10; if (c.snMain[(size_t) (b * 16 + k)] || c.R.nextBool()) k = 11; at.push_back(b * 16 + k); } } }
        else { for (int s = 0; s < kLen; ++s) if (c.kick[(size_t) s] && !c.kickSoft[(size_t) s]) { if (P.bass == BassShort && ((!at.empty() && s - at.back() < 2) || s >= o3 + fillAt)) continue; at.push_back(s); }   // (short 808s: no 16th doubles, not under the kick run)
            if (P.bass == BassDrill) for (int b = 0; b < kBars; ++b) {   // a note in the kick's holes answering the snare (the sliding line)
                static const int holes[] { 6, 7, 10, 11, 13, 14, 3 }; int tries = 0; while (tries++ < 5) { const int k = holes[c.R.nextInt(7)], s = b * 16 + k; if (c.kick[(size_t) s] || c.snMain[(size_t) s] || (b == kBars - 1 && k >= fillAt)) continue;
                    bool near = false; for (int x : at) if (std::abs(x - s) < 2) near = true; if (near) continue; at.push_back(s); break; } } }
        std::sort(at.begin(), at.end()); at.erase(std::unique(at.begin(), at.end()), at.end());
        int di = 0, lastPitch = 999; std::vector<int> pitch;
        for (size_t i = 0; i < at.size(); ++i) { int pt = root + F.deg[(size_t) (di++ % 3)]; bool sl = false;
            if ((P.bass == BassSlides || P.bass == BassDrill) && i > 0 && at[i] - at[i - 1] >= 2 && c.R.nextFloat() < P.slide) { sl = true; if (pt == lastPitch) { static const int I[] { 7, 12, -5, -12, 3 }; pt = pt + I[c.R.nextInt(P.bass == BassDrill ? 5 : 3)]; } }
            pt = juce::jlimit(-14, 12, pt); pitch.push_back(pt); slide.push_back(sl); lastPitch = pt; }
        for (size_t i = 0; i < at.size(); ++i) { const int s = at[i]; auto& st = c.hit(*r, s, s % 16 == 0 ? 96 : c.jit(98, 4)); st.pitch = (int8_t) pitch[i];
            if (P.bass == BassShort) { st.vel = (uint8_t) (s % 16 == 0 ? 88 : c.jit(80, 4)); st.hold = 2; st.flags = 32; continue; }   // (short notes under the kick: a layer, not a second kick)
            st.flags = (uint8_t) (slide[i] ? 16 : 32); if (slide[i]) st.glideT = (uint8_t) (std::abs(pitch[i] - pitch[i - 1]) >= 12 ? 18 : 11);
            const int next = i + 1 < at.size() ? at[i + 1] : kLen; st.hold = (uint8_t) juce::jlimit(1, kMaxSteps, next - s + (i + 1 < at.size() && slide[i + 1] ? 1 : 0)); }
    }
    // ---- snare (main hits, written ghosts, probability ghosts), clap, second snare
    {   auto* sn = c.row(KSnare); auto* s2 = c.row(KSnare2); auto* cl = c.row(KClap); auto* c2 = c.row(KClap2);
        auto* main = sn ? sn : cl; auto* ghostRow = (P.snare2 == S2Ghosts && s2) ? s2 : main;
        static const int ghostSpots[] { 3, 7, 10, 11, 15, 2, 6, 14, 9 };
        for (int bar = 0; bar < kBars; ++bar) {
            for (int k = 0; k < 16; ++k) { const int s = bar * 16 + k; const char ch = (char) c.snChar[(size_t) s]; if (!ch) continue; if (bar == kBars - 1 && k >= fillAt && F.fill != FNone && F.fill != FHouse) continue;
                if (ch == 'o') { if (ghostRow) { auto& st = c.hit(*ghostRow, s, c.jit(P.ghostVel, 6)); st.nudge = (int8_t) c.swingNudge(P.swSn, k); c.human(st, P.human); } continue; }
                if (!main) continue;
                const bool clapLeads = P.clap == ClapBackbeat && cl && cl != main;   // (house-type styles: the clap is the backbeat, a snare under it stays tucked)
                auto& st = c.hit(*main, s, ch == 'X' ? c.jit(clapLeads ? 96 : 116, 3) : (ch >= '2' && ch <= '8' ? 90 : c.jit(clapLeads ? 88 : 100, 4)));
                if (ch == 'f') st.flam = 16; if (ch >= '2' && ch <= '8') { st.roll = (uint8_t) (ch - '0'); st.rate = 16; st.velRamp = 50; }
                st.nudge = (int8_t) juce::jlimit(-50, 50, P.snLate + c.swingNudge(P.swSn, k)); if (P.flags & FLoose) c.human(st, humK);
                if (cl && cl != main && (P.clap == ClapLayer || (P.clap == ClapLast && (bar & 1) && k >= 8 && (ch == 'X' || ch == 'x')))) { auto& cs = c.hit(*cl, s, c.jit(96, 4)); cs.nudge = st.nudge; }
                if (s2 && P.snare2 == S2Gated && (bar & 1) && (ch == 'X' || ch == 'x')) c.hit(*s2, s, 100);
                if (c2 && bar == kBars - 1 && k >= 8 && ch == 'X') c.hit(*c2, s, 100);   // the big clap on the phrase's last backbeat
            }
            if (ghostRow && P.ghost > 0 && c.R.nextInt(100) < P.ghost) {   // an extra ghost or two in this bar
                const int n = 1 + (P.ghost >= 50 && c.R.nextBool() ? 1 : 0);
                for (int i = 0, tries = 0; i < n && tries < 8; ++tries) { const int k = ghostSpots[c.R.nextInt(9)], s = bar * 16 + k; if (c.snAny[(size_t) s] || (*ghostRow)[(size_t) s].vel || (bar == kBars - 1 && k >= fillAt)) continue;
                    auto& st = c.hit(*ghostRow, s, c.jit(P.ghostVel, 6)); st.nudge = (int8_t) c.swingNudge(P.swSn, k); c.human(st, P.human); ++i; }
            }
        }
        if (cl && cl != main && P.clap == ClapBackbeat) for (int bar = 0; bar < kBars; ++bar) for (int k : { 4, 12 }) if (!(bar == kBars - 1 && k >= fillAt)) { if (!(*cl)[(size_t) (bar * 16 + k)].vel) c.hit(*cl, bar * 16 + k, c.jit(100, 4)); }
    }
    // ---- hats: the vocabulary, the velocity map, swing / humanize, soft under the snare, out of the fill, the rolls
    if (auto* r = c.row(KHat); r && F.hat != HNone) {
        const char* cell = hatCell(F.hat);
        for (int s = F.hatFrom * 16; s < kLen; ++s) { const int k = s % 16; const char ch = cell[k]; if (ch == '.' || c.hatMute[(size_t) s]) continue;
            int v = P.hatVel == 0 ? (k % 4 == 0 ? 86 : (k % 2 == 0 ? 82 : 76)) : (P.hatVel == 1 ? (k % 4 == 0 ? 94 : (k % 2 == 0 ? 78 : 58)) : (k % 4 == 0 ? 100 : (k % 2 == 0 ? 80 : 46)));
            if (F.hat == HOff8 || F.hat == HOff16) v = P.hatVel == 2 ? 100 : 90;   // (an off-beat pattern is the accent itself)
            if (ch == 'o') v = v * 2 / 3;
            if (c.snMain[(size_t) s]) v = v * 7 / 10;
            auto& st = c.hit(*r, s, c.jit(v, P.hatVel == 0 ? 2 : 5)); st.nudge = (int8_t) c.swingNudge(P.swHat, k); c.human(st, P.human); }
        if (F.rolls > 0 && P.rollMask) {
            std::vector<int> allowed; for (int i = 0; i < kRolls; ++i) if (P.rollMask & (1 << i)) allowed.push_back(i);
            std::vector<int> spots; for (int b = 0; b < kBars; ++b) for (int k = 0; k < 16; ++k) if (P.rollSpots[k] == 'x' && !c.snMain[(size_t) (b * 16 + k)] && !c.hatMute[(size_t) (b * 16 + k)] && !(k + 1 < 16 && c.snMain[(size_t) (b * 16 + k + 1)])) spots.push_back(b * 16 + k);
            for (int i = (int) spots.size() - 1; i > 0; --i) std::swap(spots[(size_t) i], spots[(size_t) c.R.nextInt(i + 1)]);
            int used = 0; std::set<int> taken;
            for (int s : spots) { if (used >= F.rolls) break; if (taken.count(s) || taken.count(s - 1) || taken.count(s + 1)) continue;
                const int rk = allowed[(size_t) ((c.rollO + used) % (int) allowed.size())]; const bool two = rollTwoSteps(rk); if (two && (s % 16 == 15 || c.hatMute[(size_t) (s + 1)])) continue;
                auto& st = c.hit(*r, s, c.jit(84, 4)); applyRoll(st, rk); st.nudge = (int8_t) c.swingNudge(P.swHat, s % 16); if (two) (*r)[(size_t) (s + 1)] = Step(); taken.insert(s); if (two) taken.insert(s + 1); ++used; }
        }
    }
    if (auto* r = c.row(KOpen); r && F.open >= 0) { const auto& sp = openSpot(F.open); const bool skip = F.open > 0 && c.R.nextFloat() < 0.2f;   // (a sparse open hat sometimes rests)
        if (!skip) for (int b = 0; b < kBars; ++b) { if (!sp.allBars && !(b & 1)) continue; for (int k = 0; k < 16; ++k) if (sp.cell[k] == 'x' && !c.hatMute[(size_t) (b * 16 + k)]) { auto& st = c.hit(*r, b * 16 + k, c.jit(80, 4)); st.nudge = (int8_t) c.swingNudge(P.swHat, k); } } }
    if (auto* r = c.row(KShaker); r && F.shaker > 0) { const char* cell = shakerCell(F.shaker);
        for (int s = 0; s < kLen; ++s) { const int k = s % 16; const char ch = cell[k]; if (ch == '.') continue; const int v = ch == 'x' ? (F.shaker == 5 ? 60 : 84) : (F.shaker == 5 ? 52 : 50);
            auto& st = c.hit(*r, s, c.jit(v, 4)); st.nudge = (int8_t) c.swingNudge(P.swPerc, k); c.human(st, P.human); } }
    // ---- percussion: p1..p4 go, in order, to the kit's percussion tracks taken in the profile's kind order (at most two tracks of one kind, e.g. a low and a high conga); an empty cell = that track rests
    {   const char* cells[] { F.p1, F.p2, F.p3, F.p4 }; int next = 0;
        for (uint8_t kind : P.percOrder) { int ofKind = 0; for (int t = 0; t < kTracks && next < 4 && ofKind < 2; ++t) {
            if (c.kinds[(size_t) t] != kind) continue; ++ofKind; const int slot = next++; if (c.lockedTrack[(size_t) t] || !cells[slot] || !*cells[slot]) continue;
            const bool soft = kind == KVox || kind == KBell;
            if (!(slot == 3 && c.R.nextFloat() < 0.25f)) layCell(c, p.steps[(size_t) t], t, cells[slot], soft ? 80 : 88, 110, 50, P.swPerc, P.human, true); } }   // (the fourth colour sometimes rests)
    }
    if (auto* r = c.row(KRide)) { if (P.ride == 2) for (int s = 0; s < kLen; s += 4) c.hit(*r, s, 72); else if (P.ride == 1) for (int s = 32; s < kLen; s += 2) if (!c.hatMute[(size_t) s]) { auto& st = c.hit(*r, s, s % 4 == 0 ? 74 : 52); st.nudge = (int8_t) c.swingNudge(P.swHat, s % 16); } }
    if (auto* r = c.row(KCrash); r && P.crash) c.hit(*r, 0, 72);
    if (auto* r = c.row(KImpact); r && (P.crash || (P.flags & FFxEnds))) c.hit(*r, 0, 92);
    if (auto* r = c.row(KSwell); r && (P.crash || (P.flags & FFxEnds) || F.fill == FDrop)) c.hit(*r, o3 + 12, 84);
    if (auto* r = c.row(KCrackle)) for (int b = 0; b < kBars; ++b) if (!(*r)[(size_t) (b * 16)].vel) c.hit(*r, b * 16, 60);
    if (auto* r = c.row(KFx); r && (P.flags & FFxEnds) && F.fill != FGlitch) for (int b = 1; b < kBars; b += 2) if (c.R.nextFloat() < 0.5f && !c.hatMute[(size_t) (b * 16 + 14)]) c.hit(*r, b * 16 + 14, 78);
    if (auto* r = c.row(KBrass)) { c.hit(*r, 0, 76); if (c.R.nextBool()) c.hit(*r, 32, 90); }
    writeFill(c);
}

// ---------------- scoring (style rules + novelty), the generation
struct Score { float total = 0; int kickPerBarBad = 0, underSnare = 0, ghosts = 0; float sync = 0, novelty = 0; };
inline Score score(const Pattern& cand, const Profile& P, const Fam& F, const std::array<uint8_t, kTracks>& kinds, const std::array<uint8_t, kTracks>& roles, const rhythm::DNA* current) {
    Score s; int tK = -1, tS = -1, tC = -1; for (int t = 0; t < kTracks; ++t) { if (kinds[(size_t) t] == KKick && tK < 0) tK = t; if (kinds[(size_t) t] == KSnare && tS < 0) tS = t; if (kinds[(size_t) t] == KClap && tC < 0) tC = t; }
    if (tS < 0) tS = tC;
    const int fillAt = fillFrom(F.fill);
    if (tK >= 0) for (int b = 0; b < kBars; ++b) { int n = 0; for (int k = 0; k < 16; ++k) { if (b == kBars - 1 && k >= fillAt) break; const auto& st = cand.steps[(size_t) tK][(size_t) (b * 16 + k)]; if (st.vel) { ++n;
            if ((P.flags & FNoKickUnderSnare) && tS >= 0 && cand.steps[(size_t) tS][(size_t) (b * 16 + k)].vel >= 90) ++s.underSnare; } }
        if (b < kBars - 1 && (n < P.kickLo || n > P.kickHi)) ++s.kickPerBarBad; }
    if (tS >= 0) for (int st = 0; st < kLen; ++st) { const auto& x = cand.steps[(size_t) tS][(size_t) st]; if (x.vel && x.vel < 60) ++s.ghosts; }
    const auto dna = rhythm::extract(cand, roles); s.sync = dna.syncopation;
    s.total = -12.0f * (float) s.kickPerBarBad - 40.0f * (float) s.underSnare;
    if (s.sync < P.syncLo) s.total -= 30.0f * (P.syncLo - s.sync); if (s.sync > P.syncHi) s.total -= 30.0f * (s.sync - P.syncHi);
    if (P.ghost >= 30 && s.ghosts == 0) s.total -= 4.0f;
    if (current) { s.novelty = rhythm::distance(dna, *current); s.total += juce::jmin(3.0f, s.novelty); }
    return s;
}
// what a press rotates: the family (seed), the roll order (seed); the candidates (sub-seeds) differ in the details and the best-scoring one wins
inline void generate(Pattern& p, const std::array<uint8_t, kTracks>& roles, const std::array<bool, kTracks>& lockedTrack, int style, int seed, const std::array<uint8_t, kTracks>* kindsIn = nullptr) {
    const Profile& P = profileOf(style); const bool vary = isVary(style);
    std::array<uint8_t, kTracks> kinds {}; if (kindsIn) kinds = *kindsIn; else for (int t = 0; t < kTracks; ++t) { const int r = roles[(size_t) t];
        kinds[(size_t) t] = r == RoleKick ? KKick : r == Role808 ? KBass : r == RoleSnare ? KSnare : r == RoleHat ? KHat : r == RoleOpenHat ? KOpen : r == RolePerc ? KPerc : KNone; }
    const Pattern before = p;
    const int fam = family(P, vary ? p.seed : seed);
    // VARY keeps the kick foundation of the pattern that is there (tiled to 4 bars when it is shorter)
    std::array<Step, kMaxSteps> keep {}; int keepLen = 0; const std::array<Step, kMaxSteps>* keepKick = nullptr;
    if (vary) { int tK = -1; for (int t = 0; t < kTracks; ++t) if (kinds[(size_t) t] == KKick) { tK = t; break; }
        if (tK >= 0 && before.res == 1) { keepLen = before.lenOf(tK); keep = before.steps[(size_t) tK]; bool any = false; for (int s = 0; s < keepLen; ++s) any = any || keep[(size_t) s].vel > 0; if (any && keepLen >= 16) keepKick = &keep; } }
    int anyHits = 0; for (int t = 0; t < kTracks; ++t) for (int s = 0; s < before.lenOf(t); ++s) anyHits += before.steps[(size_t) t][(size_t) s].vel > 0;
    const rhythm::DNA cur = anyHits ? rhythm::extract(before, roles) : rhythm::DNA(); const rhythm::DNA* curP = anyHits ? &cur : nullptr;
    Pattern best = before; float bestScore = -1e9f;
    const int n = (int) P.fams.size(), sib0 = 1 + (int) (mix((uint32_t) seed * 747796405u + 2891336453u) % (uint32_t) juce::jmax(1, n - 1));
    for (int pass = 0; pass < (vary ? juce::jmax(1, n - 1) : 1); ++pass) {
        Fam F = P.fams[(size_t) fam];
        if (vary && n > 1) {   // VARY: this family's kicks and snares, the colour of a sibling family (hats, open hat, shaker, percussion, rolls, fill); the next sibling if that made no audible change
            const auto& sib = P.fams[(size_t) ((fam + (sib0 + pass - 1) % (n - 1) + 1) % n)];
            F.hat = sib.hat; F.open = sib.open; F.shaker = sib.shaker; F.p1 = sib.p1; F.p2 = sib.p2; F.p3 = sib.p3; F.p4 = sib.p4; F.rolls = sib.rolls; F.fill = sib.fill; F.hatFrom = sib.hatFrom;
        }
        for (int cnd = 0; cnd < kCandidates; ++cnd) {
            Pattern cand = before;
            Ctx c { cand, P, F, kinds, roles, lockedTrack, juce::Random((juce::int64) seed * 92821 + cnd * 7919 + style * 31 + pass * 977 + 11), rollOrder(seed) + cnd };
            writeCandidate(c, keepKick, keepLen);
            const float sc = score(cand, P, F, kinds, roles, curP).total;
            if (sc > bestScore) { bestScore = sc; best = cand; }
        }
        if (!vary || rhythm::hamming(best, before) >= 2) break;
    }
    p = best; p.seed = vary ? before.seed : seed;
    for (int t = 0; t < kTracks; ++t) if (lockedTrack[(size_t) t]) p.steps[(size_t) t] = before.steps[(size_t) t];   // (locked tracks exactly as they were)
    if (vary && keepKick) { int tK = -1; for (int t = 0; t < kTracks; ++t) if (kinds[(size_t) t] == KKick) { tK = t; break; } if (tK >= 0 && !lockedTrack[(size_t) tK]) { for (int s = 0; s < kLen; ++s) p.steps[(size_t) tK][(size_t) s] = keep[(size_t) (s % keepLen)]; p.trackLen[(size_t) tK] = 0; } }
}

// (UI) a short description of what a press made
inline juce::String describe(int style, int seed) { const auto& P = profileOf(style); const auto& F = P.fams[(size_t) family(P, seed)]; return juce::String(F.name).fromFirstOccurrenceOf(" ", false, false); }
inline juce::String labelFor(const juce::String& kit) { return "GENERATE " + kit.toUpperCase(); }

} // namespace gb::sty
