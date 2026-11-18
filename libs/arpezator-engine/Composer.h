#pragma once
// ArpEZator melody side (its own engine, not the web arp):
//  - ARP lane: an ostinato FIGURE = 3..8 steps of chord-tone LADDER indices (plus pedal / rest / octave / accent / tie),
//    looped against the bar (polymeter: a 6-step figure drifts against 16 steps and re-syncs at phrase starts),
//    realised over the chords with a fixed ladder floor so only 1-2 notes move per chord change; optional EVOLVE
//    (one small change every 2-4 bars, baked into 16 / 32 bars).
//  - MELODY lane: a phrase-based hook composer (rhythm groupings like 3-3-2, contour, call / response, A A' A B,
//    suspensions, anticipations, octave answers) + HARVEST from the arp.
//  - BASS lane: steady root notes.
//  - a "taste" score; every GENERATE / tweak makes N candidates and keeps the best one.
// Plain C++17, deterministic from seeds, message thread only.
#include "ArpCore.h"
#include <array>

namespace arp
{
enum Mood { Sunrise = 0, Dusk = 1, Midnight = 2, Trap = 3, HipHop = 4 };   // TRAP + HIP HOP appended (saved indices keep their meaning)
constexpr int kMoods = 5;
inline bool urbanMood (int m) { return m == Trap || m == HipHop; }   // the two moods with their own engines (UrbanMoods.cpp)
// internal shorthands (the engines were designed as happy / bittersweet / sad+dark)
constexpr int Happy = Sunrise, Bittersweet = Dusk, Sad = Midnight, Dark = Midnight;
enum Lane { LaneChords = 0, LaneArp = 1, LaneMelody = 2, LaneBass = 3, kLanes = 4 };

struct Link { int type = 0; float w = 0.55f, f = 0.5f; };   // 0 STEP, 1 GLIDE, 2 FATTEN
struct Note
{
    int t = 0, d = 24, p = 60; float v = 0.75f; Link l; bool lock = false, acc = false;
};
float playVel (const std::vector<Note>& notes, size_t i);   // own velocity + the FATTEN of the line coming into it
std::vector<MidiNote> laneMidiNotes (const std::vector<Note>& notes, int loopTicks);   // glides -> overlapping notes, FATTEN -> velocity
std::vector<MidiCtl> laneMidiCtl (const std::vector<Note>& notes, int loopTicks, bool bend, int bendRange);   // glide bends, FATTEN CC11 swells

// ---------------------------------------------------------------- RECIPE: every table / weight the engines use is DATA.
// Defaults below are the built-in placeholders; the plugin overwrites them from the embedded recipe JSON (RecipeJson.h).
struct ModeRecipe
{
    std::string label, words;                       // UI name, words fed to the chord generator
    std::vector<std::string> scales;                // allowed scales (others are re-rolled with the next seed)
    std::vector<std::string> progs;                 // preferred progressions (numerals, e.g. "I V vi IV"); empty = any
    double progBias = 0.75;                         // how hard to look for a preferred progression
    int chordBars = 1, voicing = 0, chordRhythm = 0;   // harmonic rhythm, 0 full / 1 power / 2 open, 0 sustain / 1 offbeat / 2 syncopated
    int phraseBars = 0, form = 0, staggerTicks = 0; double stutter = 0;
    int arpRange = 2, melRange = 2; bool forceArp16 = true;
    bool mute[4] { false, false, false, true };     // chords, arp, melody, bass
    std::string sound = "neon"; bool stabs = false;
    std::vector<double> cycleW { 2.5, 1, 2, 3, 2, 0.8 };   // figure length 3..8 (AUTO)
    std::vector<double> ruleW { 3.5, 3, 1, 1.2, 2, 1 };    // high-pedal, pedal-walk, run, broken, bounce, cascade
    std::vector<double> groupW;                            // per entry of groupings16 (empty = equal)
    double stepTarget = 0.45, antP = 0.2, susP = 0.15;
    // ---- from the composer's recipe (empty = the built-in engines are used)
    std::vector<double> progW;                             // weight per entry of progs
    struct Event { int fromBar = 0; std::vector<std::string> pattern; std::string pedal; };
    struct Fig { std::vector<std::string> tokens; int resync = 1; std::string pedal; std::vector<Event> evolve; double weight = 1; };
    std::vector<Fig> figures;                              // arp figure templates (ladder index 0-based, P, -, ~, >, ^)
    std::vector<std::pair<std::string, double>> pedalDeg;  // pedal degree tokens + weights
    std::vector<std::pair<std::string, double>> cellsBeat, cellsTwo;   // rhythm cells on the 1/16 grid (x onset, - held, . rest)
    std::vector<std::pair<std::vector<int>, double>> openSteps, openDeg;   // motif openings (scale steps / degrees)
    std::vector<std::pair<int, double>> formW;             // form index (0 AAAB 1 ABAB 2 ABAC 3 AAAA) + weight
    double contour[4] { 0.545, 0.273, 0.121, 0.061 };      // arch, descending, ascending, level
    double staggerP = 0, tieInP = 0.9; int maxStutterPerPhrase = 1; double rangeLo = 9, rangeHi = 14, maxLeap = 9;
    double gateLo = 0.45, gateHi = 0.5; double liftP = 0.85;
    std::vector<std::pair<std::string, double>> evolveOps;  // swap_one_index_by_1, fill_a_rest, raise_top_index, pedal_lift
    struct Target { double lo = 0, hi = 0, w = 0; };
    std::vector<std::pair<std::string, Target>> targets;   // metric -> winners' range + weight (the composer's scoring function)
    bool hasRecipe() const { return ! targets.empty(); }
};
struct Recipe
{
    ModeRecipe mode[kMoods];
    std::vector<std::vector<int>> groupings16, groupings8, groupings24, sunriseHalves;
    // taste weights
    double wBigLeap = 3, wOct = 1, wClash = 6, wWeakStrong = 1.2, wNarrow = 1.5, wTopShare = 20, wRep = 15, wStep = 6, wDir = 10, wFlatBar = 3, wLoop = 1.5, wStatic = 3, wRandom = 15;
    int arpCandidates = 48, melodyCandidates = 44;
    Recipe();
};
const Recipe& recipe();
void setRecipe (const Recipe& r);

// harmony seen by the melody engines (absolute pitch classes)
struct HChord { int t = 0, tEnd = 0, root = 0, bassNote = 36; std::vector<int> pcs, voicing, scale; std::string name; };
struct Harmony
{
    int key = 9; std::string scaleName = "minor"; std::vector<int> keyPcs; std::vector<HChord> chords; int loopTicks = BAR * 4;
    int offset = 0;   // STAGGER: the melody is composed in its own phrase time; at(t) looks at t + offset
    const HChord& at (int t) const;
    const HChord& next (int t) const;   // the chord after the one at t (wraps)
    bool chordTone (int p, int t) const;
    bool scaleTone (int p, int t) const;
    bool clash (int p, int t) const;   // a semitone against a chord tone
};
Harmony makeHarmony (int key, const std::string& scale, const std::vector<Resolved>& chords, int voicingMode /*0 smooth 1 power 2 open 3 web*/);

// ---------------------------------------------------------------- ARP figure
struct Step { int idx = 1; bool pedal = false, rest = false, acc = false, tie = false, hold = false; int oct = 0; };
struct Figure
{
    int len = 6; std::array<Step, 8> s {};
    int rule = 0;   // which rule made it (for VARY)
    // recipe figures: ladder = chord tones from a floor (tonic >= 55 + floorOff), pedal = a key degree token ("1'", "5", "7,", "b3'")
    bool recipe = false; int floorOff = 0, resync = 1; std::string pedalDeg;
};
struct FigEvent { int fromBar = 0; bool hasFig = false; Figure fig; std::string pedalDeg; };   // the slow event(s) inside the loop
struct ArpParams
{
    uint32_t seed = 1; int mood = Sad; int spb = 16; int range = 2; int reg = 0; double gate = 0.45; int cycle = 0;   // 0 = AUTO
    int resync = 0;   // 0 AUTO, else bars (1 2 4 8), -1 never
    bool holdPedal = true; int evolveBars = 0; double master = 0.5;
};
struct ArpResult { Figure fig; std::vector<FigEvent> events; std::vector<Note> notes; int pedalPc = 0; double score = 0; };
int ladderSize (const ArpParams& P);
ArpResult composeArp (const Harmony& H, const ArpParams& P, int totalTicks);
std::vector<Note> realizeArp (const Figure& f, const Harmony& H, const ArpParams& P, int totalTicks, int pedalPc, const std::vector<FigEvent>* events = nullptr);
Figure figureFromTokens (const std::vector<std::string>& tokens);
int degreePitch (const std::string& tok, int key, const std::string& scale, int base);   // '5' 'b3' "1'" '7,' -> MIDI
int tonicAtOrAbove (int key, int lo);
int choosePedalPc (const Harmony& H);
Figure figureVary (const Figure& f, const ArpParams& P, uint32_t seed, int amount, const Harmony& H, int totalTicks, int pedalPc);
Figure figureNewNotes (const Figure& f, const ArpParams& P, uint32_t seed, const Harmony& H, int totalTicks, int pedalPc);
Figure figureNewRhythm (const Figure& f, const ArpParams& P, uint32_t seed, const Harmony& H, int totalTicks, int pedalPc);
Figure figureSimpler (const Figure& f);
Figure figureBusier (const Figure& f, const ArpParams& P);

// ---------------------------------------------------------------- MELODY (hooks)
struct MelodyParams
{
    uint32_t seed = 1; int mood = Sad; int spb = 16; int range = 2; int reg = 0; double master = 0.5; double gate = 0.6;
    double stutter = 0;   // HAPPY: repeated-note bounces inside the hook
    int stagger = 0;      // ticks the phrase is offset against the chord loop (0, 2 beats, 1 bar)
    int form = 0;         // 0 AAAB, 1 ABAB, 2 ABAC, 3 AAAA (with variation)
    int phraseBars = 0;   // 0 = as long as the chord loop
};
std::vector<Note> composeMelody (const Harmony& H, const MelodyParams& P, int totalTicks, bool evolve);

// ---------------------------------------------------------------- BASS
struct BassParams { int rate = 0; /* 0 = 1/8, 1 = 1/16, 2 = offbeat 1/8 */ double gate = 0.6; uint32_t seed = 0; /* 0 = the plain root line; else a seeded take (NEW on the BASS lane) */ };
std::vector<Note> composeBass (const Harmony& H, const BassParams& P, int totalTicks);

// ---------------------------------------------------------------- note-level tools (respect locks; sel = optional selection mask)
struct EditCtx { const Harmony* H = nullptr; int totalTicks = BAR * 4; int spb = 16; int mood = Sad; std::vector<bool> barLocks; const std::vector<bool>* sel = nullptr; bool pedalMode = false; };
bool editable (const EditCtx& c, const std::vector<Note>& notes, size_t i);
std::vector<Note> toolVary (const std::vector<Note>& notes, const EditCtx& c, uint32_t seed, int amount /*1 little, 2 more*/);
std::vector<Note> toolNewNotes (const std::vector<Note>& notes, const EditCtx& c, uint32_t seed);
std::vector<Note> toolNewRhythm (const std::vector<Note>& notes, const EditCtx& c, uint32_t seed);
std::vector<Note> toolSimpler (const std::vector<Note>& notes, const EditCtx& c);
std::vector<Note> toolBusier (const std::vector<Note>& notes, const EditCtx& c, uint32_t seed);
std::vector<Note> toolOctave (const std::vector<Note>& notes, const EditCtx& c, int dir);
std::vector<Note> refitToHarmony (const std::vector<Note>& notes, const EditCtx& c);   // after a chord change: keep the rhythm, fix clashes
std::vector<Note> mergeLocked (const std::vector<Note>& fresh, const std::vector<Note>& old, const EditCtx& c);
void sortNotes (std::vector<Note>& n);

// ---------------------------------------------------------------- the composer's metrics (port of lib.analyse) + scoring function
struct Metrics { std::vector<std::pair<std::string, double>> v; double get (const std::string& k, double d = 0) const { for (auto& e : v) if (e.first == k) return e.second; return d; } void set (const std::string& k, double x) { for (auto& e : v) if (e.first == k) { e.second = x; return; } v.push_back ({ k, x }); } };
struct ArpInfo { int len = 0, resync = 1, pedalSteps = 0, events = 0; bool valid = false; };
Metrics analyseLane (const std::vector<Note>& notes, const Harmony& H, int totalTicks, const ArpInfo* arp = nullptr);
double recipeScore (const Metrics& m, const ModeRecipe& M);   // sum w * fit / sum w, fit = 1 inside [lo, hi], 0 one width outside
bool arpHardRules (const std::vector<Note>& notes, int figLen, int totalTicks, std::string* why = nullptr);
bool melodyHardRules (const std::vector<Note>& notes, const Harmony& H, int totalTicks, int mode, bool hasB, int aPeak, int bPeak, std::string* why = nullptr);

// ---------------------------------------------------------------- taste
struct Taste { double score = 0; int bigLeaps = 0, clashes = 0; double stepRatio = 0, range = 0, topShare = 0; };
Taste taste (const std::vector<Note>& notes, const Harmony& H, int mood, bool pedalMode, int totalTicks);
int snapToKey (int p, int t, const Harmony& H, int dir);   // nearest pitch in the key / chord (SNAP KEY)
// GROOVE: 0 = exactly on the grid (applied at playback / export, never stored)
std::vector<Note> applyGroove (const std::vector<Note>& notes, double groove, int spb, uint32_t seed);
} // namespace arp
