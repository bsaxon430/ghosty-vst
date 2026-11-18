#pragma once
// MUTANT ARP: the melodic sequence generator for fast, futuristic, video-game-like synth lines (modern rage / futuristic
// trap / hypertrap ostinatos). Not an up/down arpeggiator: a chord-aware step cell ("DNA") of 8..32 steps is generated
// from weighted musical rules (chord-tone ladders across octaves, octave displacement, a pedal / high ping, neighbour and
// chromatic approach notes that resolve, fragment echoes, a 3-3-2 style accent pattern, ratchets and 1/64 bursts), then
// realised over the idea's chords with a variation every 2 or 4 bars and an optional EVOLVE over 4 / 8 / 16 bars.
// LOCK NOTES keeps chosen steps exactly through GENERATE / MUTATE / EVOLVE. Plain C++17, JUCE-free, deterministic from
// seeds, message thread only (the audio thread only ever sees the realised notes through the normal lane pattern).
// Numbers come from work/arp/RESEARCH.md (section 3).
#include "Composer.h"

namespace arp
{
// FLUTE (3.2.0) is hidden since 3.2.1: sessions saved with it still generate and render exactly as before, the panel shows
// CYBERPOP in its place. CYBERPOP generates from the style table learned from MIDI packs (CyberpopTable.h, tools/learn_pack.py).
enum MutantMode { MutRage = 0, MutAlien, MutDigital, MutDream, MutHyper, MutFlute, MutCyberpop, kMutantModes };
constexpr int kMutantButtons = 6;     // the panel's mode buttons: RAGE ALIEN DIGITAL DREAM HYPER CYBERPOP
const char* mutantModeName (int m);   // RAGE ALIEN DIGITAL DREAM HYPER FLUTE CYBERPOP
const char* mutantModeBlurb (int m);  // one line for the panel
inline bool mutantModeShown (int m) { return m != MutFlute; }
inline int mutantButtonOfMode (int m) { return m == MutCyberpop ? 5 : m == MutFlute ? -1 : m; }   // -1 = no button (an older session's FLUTE)
inline int mutantModeOfButton (int b) { return b == 5 ? (int) MutCyberpop : b; }

// one step of the cell. Pitch is relative to the chord sounding at the step (CHORD FOLLOW): chord tone `idx` in octave `oct`
// above the floor (C3), coloured by `col` (0 chord tone, +1 / -1 the scale neighbour above / below, +2 / -2 a chromatic approach
// from below / above that resolves on the next step).
struct MutantStep
{
    int idx = 0, oct = 2, col = 0;
    int role = 0;            // 0 free, 1 pedal (the root, low), 2 ping (the top tone)
    bool rest = false, hold = false, acc = false, lock = false, glide = false;
    bool grace = false;      // a grace note (a scale step away, a 1/32 before the note): FLUTE ornament
    bool trill = false;      // the note trills with its upper scale neighbour (FLUTE ornament)
    int sus = 1;             // the note lasts this many steps (the steps it covers rest); 1 = one step (hold = 2). CYBERPOP's longer notes
    int ratchet = 1;         // 1 = one note, 2 = a 1/32 pair, 3 = a triplet, 4 = a 1/64 burst
    double prob = 1.0;       // step probability (STEP SKIP)
    double gate = 0.5;       // fraction of the step (> 1 overlaps the next)
    double vel = 0.75;       // base velocity (accents above it)
};
struct MutantCell
{
    int len = 16;            // steps
    std::vector<MutantStep> s;
    bool valid() const { return len >= 1 && (int) s.size() == len; }
};

struct MutantParams
{
    int mode = MutRage; uint32_t seed = 1;
    int div = 16;            // steps per bar: 4 8 16 32 64 (1/4 .. 1/64)
    int cellSteps = 0;       // 0 = AUTO (the mode's lengths), else 8 12 16 24 32
    // the ten knobs, 0..1
    double density = 0.5, octaveJump = 0.5, intervalChaos = 0.3, retrigger = 0.3, stepSkip = 0.0, velRandom = 0.3, gate = 0.5, swing = 0.0, evolve = 0.4, glide = 0.0;
    int direction = 0;       // 0 forward, 1 reverse, 2 ping-pong
    int rotate = 0;          // steps
    bool scaleLock = true, chordFollow = true;
    int evolveBars = 0;      // 0 off, 4, 8, 16: the cell mutates a little more every variation period, wrapping after this many bars
    int variationBars = 0;   // 0 = the mode's own (RAGE / DIGITAL 2, ALIEN / DREAM 4)
    int reg = 0;             // -1 / 0 / +1 octave
    // TWO LINES: a second line in the MELODY lane that plays against the first. 0 off, 1 ANSWER (call and response: the first line
    // plays the first half of every variation period, the second answers in the other half, an octave away, contour kept or
    // mirrored), 2 INTERLOCK (a hocket: the second line's own cell, a register lower, sounding only where the first rests),
    // 3 SHADOW (a canon: the first line again, late by `shadowSteps`, an octave away, softer)
    int twoLine = 0; int shadowSteps = 3; int lineBReg = -1;
};
enum MutantTwoLine { TwoOff = 0, TwoAnswer, TwoInterlock, TwoShadow, kTwoLines };
const char* mutantTwoLineName (int k);
// what the Idea keeps (saved with the session; absent = the classic arp engine, exactly as before)
struct MutantState
{
    bool active = false;
    MutantParams P; MutantCell cell, cellB;   // cellB: the INTERLOCK line's own cell (empty otherwise)
    int generation = 0;      // MUTATE count since GENERATE (the panel shows it)
    std::string harm;        // the harmony the lane was realised over (a change re-realises it)
};

int mutantVariationBars (const MutantParams& P);
int mutantStepTicks (int div);                         // BAR / div
// GENERATE: a new cell. `keep` = the previous cell: its locked steps are copied exactly (LOCK NOTES); null = nothing kept
MutantCell mutantGenerate (const Harmony& H, const MutantParams& P, const MutantCell* keep = nullptr);
// MUTATE: a variation of the cell (locked steps untouched). amount 0..1 (~ share of unlocked steps that change, at least one).
// kind: 0 any, 1 OCTAVE CHAOS (register / octave pops only), 2 RHYTHM CHAOS (rests, holds, ratchets, fills, density only)
MutantCell mutantMutate (const MutantCell& c, const Harmony& H, const MutantParams& P, uint32_t seed, double amount, int kind = 0);
// the cell -> notes over the whole idea (variation every 2 / 4 bars, EVOLVE, direction, rotation, swing, step probability)
std::vector<Note> mutantRealize (const MutantCell& c, const Harmony& H, const MutantParams& P, int totalTicks);
// TWO LINES: both lines at once (A -> the ARP lane, B -> the MELODY lane). With twoLine off, B is empty and A = mutantRealize.
// cellB is the INTERLOCK line's cell (make it with mutantSecondCell); ANSWER and SHADOW derive B from A
void mutantRealizeTwo (const MutantCell& a, const MutantCell& cellB, const Harmony& H, const MutantParams& P, int totalTicks, std::vector<Note>& outA, std::vector<Note>& outB);
MutantCell mutantSecondCell (const MutantCell& a, const Harmony& H, const MutantParams& P);   // the INTERLOCK line's cell (same length, complementary rhythm, a register lower)
// the MIDI pitch of a step over the chord at tick t (what mutantRealize uses)
int mutantPitch (const MutantStep& s, const Harmony& H, int t, const MutantParams& P);
// which cell step a realised note at tick t came from (-1 = none / off the cell), for LOCK SELECTED from the roll
int mutantStepAt (const MutantCell& c, const MutantParams& P, int t);
void mutantLockFirst (MutantCell& c, int n);           // lock steps 0..n-1 (n <= 0 = unlock all)
int mutantLockedCount (const MutantCell& c);
// deterministic text print (tests) and statistics (the prototype compares them with RESEARCH.md)
std::string mutantPrint (const MutantCell& c);
// how two lines relate (tests + the panel): share of B onsets that land on an A onset, and B's register relative to A
struct MutantDuo { double collide = 0; double gapFill = 0; int medianGap = 0; };
MutantDuo mutantDuo (const std::vector<Note>& a, const std::vector<Note>& b, int stepTicks);
struct MutantStats
{
    int notes = 0, bars = 0; double notesPerBar = 0;
    double leapShare = 0;     // intervals >= 7 semitones
    double octShare = 0;      // intervals that are exactly an octave (or two)
    double chordShare = 0;    // onsets on a chord tone
    double scaleShare = 0;    // onsets in the key / chord (chord tones included)
    double reversal = 0;      // leaps followed by a direction change
    double ratchetShare = 0;  // onsets that are part of a ratchet / burst
    double accentShare = 0;   // notes with v >= 0.85
    double repeatFrag = 0;    // 1 if some 3-note fragment (intervals + rhythm) appears twice in the first 2 bars
    int range = 0;            // semitones
};
MutantStats mutantAnalyse (const std::vector<Note>& notes, const Harmony& H, int totalTicks);
} // namespace arp
