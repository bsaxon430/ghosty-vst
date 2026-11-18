#pragma once
#include <array>
// One ArpEZator "idea": the mode (SUNRISE / DUSK / MIDNIGHT), the generated chords (web-identical generator),
// and the lanes (CHORDS, ARP, MELODY, BASS) with their settings, locks and notes. JUCE-free so the tests and the
// plugin share exactly the same pipeline. Message thread only.
#include "ArpCore.h"
#include "Composer.h"
#include "Mutant.h"

namespace arp
{
struct LaneState
{
    std::vector<Note> notes; std::vector<bool> barLocks; bool edited = false; bool mute = false;
};

struct Idea
{
    // what the user picked
    int mode = Midnight; std::string words; uint32_t seed = 128;
    // LANE MOOD (arp, melody, bass by lane index; chords follow the harmony): -1 = the idea's mood, else that lane composes in its own
    // mood; laneVar = the lane's own variation (0 = the idea's). Defaults = exactly the old behaviour.
    std::array<int, 4> laneMood { -1, -1, -1, -1 }; std::array<int, 4> laneVar { 0, 0, 0, 0 };
    // generator output (the chords match the web version for prompt + seedUsed)
    std::string prompt; uint32_t seedUsed = 128; Gen g;
    // chords lane
    int chordBars = 2;        // harmonic rhythm: each chord lasts 1 or 2 bars
    int voicing = 0;          // 0 smooth (voice-led), 1 power, 2 open (wide), 3 the web version's voicing
    int chordRhythm = 0;      // 0 sustain, 1 offbeat stabs, 2 syncopated stabs
    int chordOct = 0;         // CHORDS OCT - / OCT +: the whole chord lane moved -2..+2 octaves (after the voicing; names, progression and the other lanes stay)
    bool simple = true;       // SIMPLE chords (plain major / minor triads, classic loops) vs RICH (the full generator, as the web app)
    std::vector<Slot> simpleProg;   // SIMPLE: the classic loop for generated ideas; empty = simplify g.prog chord by chord (LISTEN, starters)
    // arp lane
    Figure fig; std::vector<FigEvent> figEvents; int pedalPc = 0; int arpSpb = 16, arpRange = 2, cycle = 0, resync = 0; bool holdPedal = true; double arpGate = 0.45;
    // melody lane
    int melSpb = 16, melRange = 2, form = 0, phraseBars = 0, stagger = 0; double stutter = 0;
    // bass lane
    int bassRate = 0;
    // whole idea
    double master = 0.5, groove = 0; bool evolve = false; int evolveBars = 16; int reg = 0;
    int songBars = 0;
    int keyShift = 0;         // TRANSPOSE BAKE: semitones the chords (and the key everything is written in) were moved, exactly (voicings + names)
    std::vector<float> energy;   // the drawn ENERGY lane (one value per bar, 0..1; empty = the EVOLVE shape's own curve)
    int arpMeter = 0, arpEuclid = 0, arpEuclidRot = 0;   // IDM: the arp's meter cycle (accents + euclid length) and a euclidean rhythm (hits, 0 = off)         // MAKE IT A SONG: the take is at least this long (0 = as the chords / phrase / EVOLVE make it)
    // HOOK LAB shaping (HookLab.h): 0 / -1 = as the engine composes (the classic behaviour)
    int style = 0, candy = 0; double tension = -1;          // COMPOSITION STYLE, EAR CANDY, MELODIC TENSION (-1 = AUTO)
    int evolveMode = 0;                                     // EVOLVE: 0 SUBTLE (classic), 1 GROW, 2 WAVE, 3 JOURNEY
    int arpDir = 0, arpContour = 0, arpAccent = 0;          // arp DIRECTION / CONTOUR / ACCENT (0 = as the figure)
    std::vector<Note> motif; int motifT0 = 0, motifLen = 0; // LOCK MOTIF (melody): notes relative to motifT0 inside the phrase
    // TRAP / HIP HOP (UrbanMoods.h): FLAVOUR (-1 = AUTO, picked per idea), the one in use, the colour scale ("penta" = minor
    // pentatonic + the blue note over minor chords), double-time hip hop (every pattern on the 1/8 grid), and what the lanes used
    int flavour = -1, flavourUsed = 0; std::string colour; bool doubleTime = false;
    int darkness = -1;   // DARKNESS 0..100 (-1 = the mood's default: TRAP 70, HIP HOP 40)
    int urbanProg = -1, urbanBassArch = -1, urbanMelArch = -1;
    // the TRAP melody writer: 0 = as shipped (every session saved before 3.2.1 renders exactly as it did), 1 = the 3.2.1 trap-flute
    // writer (UrbanMoods.cpp trapFluteMelody: slow singing phrases). newIdea / a NEW melody take set it to newIdeaMelVer()
    int melVer = 0;
    // MUTANT ARP (Mutant.h): when active, the ARP lane (and with TWO LINES the MELODY lane) is realised from the mutant cell instead of
    // the figure engine; absent / inactive = exactly the classic behaviour
    MutantState mutant;
    LaneState lanes[kLanes];
    // derived (rebuilt by rebuild())
    std::vector<Resolved> chords; Harmony H; int total = BAR * 8;
    int chordOctUsed = 0;     // the part of chordOct that fits (every chord note stays in MIDI 24..108)
    // EDITED CHORDS (lanes[LaneChords].notes + edited): the chord lane as notes, stored at octave 0 (chordOctUsed moves them on the way out).
    // chordSig = the chords as generated (voicings + timing + rhythm, before BAKE / OCT): when a rebuild changes it, the edits are replaced
    // (locked notes / locked bars kept) and chordRewrites counts up (chordRewriteKind 1 = replaced, 2 = kept locked notes) for the toast
    uint64_t chordSig = 0; int chordRewrites = 0, chordRewriteKind = 0;
};

std::string modeName (int mode);              // SUNRISE / DUSK / MIDNIGHT
std::string modeWords (int mode);             // the prompt words a mode feeds the generator
Gen generateForMode (int mode, const std::string& words, uint32_t seed, std::string& promptOut, uint32_t& seedUsed);
void applyModeDefaults (Idea& I);             // lane settings that suit the mode (called on mode change / first idea)
void newIdea (Idea& I);                       // GENERATE: chords + every lane from I.mode / words / seed
// the melody writer version newIdea stamps on an idea (Idea::melVer). Process-wide, default 0 (= exactly the shipped writers), so a
// host that links this engine for its own older takes (Ghost Slice) is untouched; the ArpEZator plug-in sets kMelVerCurrent at start
constexpr int kMelVerCurrent = 1;
void setNewIdeaMelVer (int v);
int newIdeaMelVer();
// PUSH FURTHER (the variety rule): GENERATE that moves away from the last ideas. Candidates come from I.seed, I.seed + 7919, ...;
// one is rejected when its melody is closer than `threshold` to any of the last <= 3 ideas (distance = mean of the normalised
// edit distances of the first 4 bars' pitch classes and rhythm), or when it repeats the last idea's scale + bass + melody
// archetypes or (TRAP / HIP HOP, key not typed) its key. The first candidate that passes wins, else the furthest one.
// I.seed ends as the seed that made it, so recalling that seed later gives the same idea. Always on for TRAP / HIP HOP,
// opt-in (MORE: PUSH FURTHER) for the other moods, whose plain GENERATE stays exactly as before.
struct RecentIdea
{
    std::vector<Note> melody; int key = 0; std::string scale; int bassArch = -1, melArch = -1;
    // TRAP / HIP HOP style rotation (UrbanMoods.h rotationDistance): the flavour, tempo, GROOVE, 808 slides (first 8 bars), lead
    // sound, first chord (absolute pitch class), progression, guitar-bed archetype (EMO)
    int flavour = -1, bpm = 0, slides = 0, lead = 0, firstChord = -1, prog = -1, arpArch = -1; double groove = 0;
};
RecentIdea recentOf (const Idea& I);
double melodyDistance (const std::vector<Note>& a, int keyA, const std::vector<Note>& b, int keyB);
int newIdeaFurther (Idea& I, const std::vector<RecentIdea>& recent, double threshold = 0.62, int maxTries = 10);   // returns the tries used
void rebuildChords (Idea& I);                 // chords + harmony + total length from the current prog / settings
void composeLane (Idea& I, int lane, bool keepLocks = true);   // fresh notes for one lane (respects locks)
void refitLanesToChords (Idea& I);            // chords changed: keep every lane's rhythm, fix the pitches
bool laneHardOk (const Idea& I, int lane);    // the lane passes its hard rules (arp: arpHardRules, melody: melodyHardRules)
void rerealizeArp (Idea& I);                  // the arp FIGURE changed (or a setting it depends on): notes again, locks kept
int  chordLoopTicks (const Idea& I);
int  melodyPhraseTicks (const Idea& I);      // the melody's phrase (LOCK MOTIF / EVOLVE repeat it)
// SIMPLE / RICH: the slots the chords lane uses (simpleProg in SIMPLE when there is one, else g.prog) and the scale they resolve in
std::vector<Slot>& chordProg (Idea& I);
const std::vector<Slot>& chordProg (const Idea& I);
std::string chordScale (const Idea& I);
ProgState progState (const Idea& I);          // what rebuildChords resolves (each slot's length already times chordBars)
void pickSimpleProg (Idea& I, uint32_t seed, bool avoidCurrent = false);   // a classic loop for the mode (deterministic per seed)
void setSimple (Idea& I, bool simple);        // flip SIMPLE / RICH: chords again, lanes re-fit
int  altCount (const Idea& I, const Slot& s); // tap-a-chord versions (SIMPLE: auto / swap down / swap up / borrow)
int  altAt (const Idea& I, const Slot& s, int k);   // k-th version -> Slot::alt
int  altIndex (const Idea& I, const Slot& s);       // Slot::alt -> k
void nextAlt (Idea& I, int slot);             // tap a chord: the next version
// CHORDS OCTAVE: how far (towards `want`, -2..+2) the chord lane can move with every chord note inside MIDI 24..108 (uses the
// harmony as rebuildChords leaves it, minus the octave already applied); setChordOct moves it there, returns false when it had to stop short
int chordOctFit (const Idea& I, int want);
bool setChordOct (Idea& I, int want);
void applyChordOct (Idea& I);                // chordOct again as far as it fits now (after the chord notes changed / were loaded)
// EDITED CHORDS: the CHORDS roll edits notes like MELODY. chordsEdited = the stored notes are the chord lane; chordDisplayNotes = what the
// roll shows (index-aligned with the stored notes once edited); materializeChords = the first edit turns the generated chords into notes
bool chordsEdited (const Idea& I);
std::vector<Note> chordDisplayNotes (const Idea& I);
void materializeChords (Idea& I);
int foldChordPitch (int p);                   // a chord note kept in MIDI 24..108 (octaves)
std::vector<MidiNote> chordLaneNotes (const Idea& I);   // with the chord rhythm (sustain / stabs), over the whole length
ArpParams arpParams (const Idea& I);
// ---------------------------------------------------------------- TRANSPOSE
std::string transposeChordName (const std::string& name, int semis, int newKey, const std::string& scale);   // "F#m7/A" + 3 -> "Am7/C"
void shiftHarmony (Harmony& H, std::vector<Resolved>& chords, int semis);   // the key + every chord moved exactly (voicings, roots, names)
int foldPitch (int p, int lane);              // a transposed note kept audible: leads fold under ~C7, the bass over ~C1, always 0..127
bool foldsAt (int p, int lane);               // would p need folding?
void bakeTranspose (Idea& I, int semis, int laneMask);   // write a transpose into the notes (+ the chords via keyShift); exact
int keyShiftTo (int fromKey, int toRoot);     // the smallest move from one root to another: -5..+6
MelodyParams melodyParams (const Idea& I);
EditCtx editCtx (const Idea& I, int lane);
void trimOverlaps (std::vector<MidiTrack>& tracks);   // no same-pitch overlaps on one channel (across tracks)
struct MidiExport { bool glideBend = true; int bendRange = 12; float laneGain[4] { 1, 1, 1, 1 }; };   // bends on MELODY / BASS; lane VOL as velocity
std::vector<uint8_t> ideaMidi (const Idea& I, int laneMask, bool oneTrack, double bpm, const MidiExport& X = {}, int secStart = -1, int secEnd = -1);   // laneMask bit = lane; a section [secStart, secEnd) exports from 0
} // namespace arp
