#pragma once
// HOOK LAB: the "wait, play that again" layer on top of the recipe engine (composeMelody / composeArp stay the source
// of every line; this file shapes, scores, varies and combines what they make). Plain C++17, JUCE-free, deterministic
// from seeds, message thread only.
//   - EAR CANDY (SUBTLE / HOOK / EUPHORIC / UNHINGED): rare special events with a budget per 4 / 8 bars
//   - COMPOSITION STYLE (HYPNOTIC / ANTHEMIC / MINIMAL / EMOTIONAL / PLAYFUL): independent of the mood
//   - MELODIC TENSION: chord tones -> passing tones, suspensions, extensions, resolved chromatic notes
//   - GENERATE RESPONSE + SIMILARITY, lane relationships (register, onsets vs the arp, rests)
//   - arp DIRECTION / CONTOUR / ACCENT, EVOLVE GROW / WAVE / JOURNEY + LOCK MOTIF
//   - MOTIF HARVESTER that learns feature weights from the user's keeps, PIANO TEST
//   - the Hook Lab itself: 16 hooks, FIND THE MAGIC, MAKE IT A SONG, COMBINE
#include "Idea.h"
#include <array>
#include <atomic>
#include <string>

namespace arp
{
enum EarCandy { CandyOff = 0, CandySubtle, CandyHook, CandyEuphoric, CandyUnhinged, kCandies };
enum CompStyle { StyleAuto = 0, StyleHypnotic, StyleAnthemic, StyleMinimal, StyleEmotional, StylePlayful,
                 StyleDark, StyleTriumphant, StyleHypnoLoop,          // TRAP styles (appended: saved indices keep their meaning)
                 StyleBoomBap, StyleSoulful, StyleLaidBack,           // HIP HOP styles
                 StyleEmo, kStyles };                                  // TRAP EMO (appended after the HIP HOP three: saved indices keep their meaning)
constexpr int kClassicStyles = 6;
int baseStyle (int style);              // a TRAP / HIP HOP style used in the other moods behaves like its closest classic style
int styleFlavour (int style, int mood); // TRAP / HIP HOP style in its own mood -> the FLAVOUR its hooks are written in (-1 = none)
int styleCountFor (int mood);           // the style buttons shown for a mood: the classic six (+ the mood's three; TRAP + EMO)
int styleAt (int mood, int i);          // button i -> style
int styleButton (int mood, int style);  // style -> button (0 = AUTO when the style isn't shown)
enum EvolveMode { EvolveClassic = 0, EvolveGrow, EvolveWave, EvolveJourney, kEvolveModes };
enum ArpDir { DirFigure = 0, DirUp, DirDown, DirUpDown, DirRandom, DirPendulum, DirConverge, kArpDirs };
enum ArpContour { ContFlat = 0, ContRise, ContFall, ContArch, ContValley, kArpContours };
enum ArpAccent { AccFigure = 0, AccDownbeat, AccBuild, AccOffbeat, AccGrow, kArpAccents };
const char* candyName (int c);
const char* styleName (int s);
const char* evolveModeName (int m);
const char* arpDirName (int d);
const char* arpContourName (int c);
const char* arpAccentName (int a);

// ---------------------------------------------------------------- features of a line (what FIND THE MAGIC keeps)
struct HookFeatures
{
    std::vector<int> rhythm;      // onsets of the opening motif (first bar, ticks from its start)
    std::vector<int> contour;     // sign of each interval (-1 0 1)
    std::vector<int> intervals;   // the first 4 intervals (semitones): the interval signature
    double peakPos = 0;           // where the highest note sits (0..1 of the span)
    double density = 0, range = 0, repeat = 0, avgDur = 0, sync = 0, leap = 0, nct = 0, rest = 0, step = 0;
};
HookFeatures hookFeatures (const std::vector<Note>& n, const Harmony& H, int spanTicks);
double noteShare (const std::vector<Note>& a, const std::vector<Note>& b, int spanTicks);   // shared (t, p) / the larger count
double phraseSimilarity (const std::vector<Note>& a, const std::vector<Note>& b, int spanTicks);   // 0..1: onsets + contour
double nonChordShare (const std::vector<Note>& n, const Harmony& H);
int outOfKey (const std::vector<Note>& n, const Harmony& H, bool countResolved);   // notes outside key + chord
bool isResolvedChromatic (const std::vector<Note>& n, size_t i, const Harmony& H);
int medianPitch (const std::vector<Note>& n);

// ---------------------------------------------------------------- EAR CANDY
struct CandyEvent { int t = 0, kind = 0; };   // kind: 1 grace 2 echo 3 octave answer 4 stutter 5 run-up 6 octave pop 7 glide 8 ratchet 9 chromatic slide 10 drop
int candyWindowBars (int candy);   // SUBTLE counts per 8 bars, the rest per 4
int candyBudget (int candy);       // max events per window
const char* candyEventName (int kind);
std::vector<Note> applyEarCandy (const std::vector<Note>& notes, const Harmony& H, int candy, int totalTicks, uint32_t seed, std::vector<CandyEvent>* events = nullptr);

// ---------------------------------------------------------------- STYLE, TENSION, lane relationships
std::vector<Note> applyStyle (const std::vector<Note>& notes, const Harmony& H, int style, int totalTicks, uint32_t seed);
double styleFit (const HookFeatures& f, int style);   // 0..1, how much a line sounds like the style
std::vector<Note> applyTension (const std::vector<Note>& notes, const Harmony& H, double tension, uint32_t seed);
std::vector<Note> keyGuard (const std::vector<Note>& notes, const Harmony& H, bool allowChromatic);   // snap anything out of key that isn't a resolved chromatic note
std::vector<Note> separateRegister (const std::vector<Note>& mel, const std::vector<Note>& arpNotes, int minGap = 7);
// REGISTER (LOW / MID / HIGH): where a lead line sits. MID keeps the median in [67, 78] (G4..F#5), every window tops out at E6 (88);
// octShift moves the whole window (the MELODY lane's own OCT +/-). The arp's register only breaks ties (complementRhythm handles overlap).
enum HookRegister { RegLow = 0, RegMid, RegHigh, kRegisters };
const char* registerName (int r);
struct RegWindow { int lo = 67, hi = 78, top = 88; };
RegWindow registerWindow (int reg, int octShift = 0);
std::vector<Note> placeRegister (const std::vector<Note>& mel, const std::vector<Note>& arpNotes, int reg, int octShift = 0);
double collisionShare (const std::vector<Note>& mel, const std::vector<Note>& arpNotes);   // melody onsets that land on an arp onset
std::vector<Note> complementRhythm (const std::vector<Note>& mel, const std::vector<Note>& arpNotes, const Harmony& H, int spb);
std::vector<Note> ensureRests (const std::vector<Note>& mel, int totalTicks);   // every 2 bars breathe (a gap of a beat)

// ---------------------------------------------------------------- GENERATE RESPONSE
// the notes of [c0, c1) answered at [d0, d0 + (c1 - c0)): onsets and contour kept with probability ~ similarity, refit to the harmony
std::vector<Note> generateResponse (const std::vector<Note>& line, const Harmony& H, int c0, int c1, int d0, double similarity, uint32_t seed);

// ---------------------------------------------------------------- ARP shape
std::vector<Note> shapeArp (const std::vector<Note>& notes, const Harmony& H, int dir, int contour, int accent, int figLen, int cycleTicks, uint32_t seed);
struct AlignMark { int t = 0, kind = 0; };   // 0 the figure starts again, 1 the chord changes, 2 both on the same tick
std::vector<AlignMark> arpAlignment (const Idea& I);
double arpAlignmentScore (const Idea& I);   // share of chord changes that meet a figure start

// ---------------------------------------------------------------- EVOLVE + LOCK MOTIF
struct MotifSpan { int t0 = -1, t1 = -1; bool valid() const { return t1 > t0; } };
MotifSpan strongestMotif (const std::vector<Note>& phrase, const Harmony& H, int phraseTicks);
std::vector<Note> evolveLine (const std::vector<Note>& phrase, const Harmony& H, int phraseTicks, int totalTicks, int mode, MotifSpan keep, uint32_t seed, int spb, int mood);
// EVOLVE on the ARP: the figure (the arp's motif) stays exactly as it is; its energy (velocity, gate, accents on the figure starts)
// moves over the take. GROW opens up, WAVE breathes every 8 bars, JOURNEY travels to a peak and comes home. Pitches and onsets never change.
double arpEnergy (int mode, int t, int totalTicks);   // 0..1
double arpEnergyAt (const Idea& I, int t);            // the drawn ENERGY lane when there is one, else the EVOLVE shape
void setArpEnergy (Idea& I, const std::vector<float>& curve);   // draw (or clear) the lane: the arp's level / gate follow it, nothing else changes
std::vector<Note> evolveArp (const std::vector<Note>& notes, int totalTicks, int mode, int figTicks, int resyncTicks);
std::vector<Note> evolveArp (const Idea& I, const std::vector<Note>& notes);   // figure length + resync from the idea
bool arpEvolving (const Idea& I);                                                // GROW / WAVE / JOURNEY on: the arp has an energy curve
std::vector<MidiCtl> arpEnergyCtl (const Idea& I);                               // CC74 (brightness) + CC11 (expression) per bar, for the live MIDI out and the .mid
int arpResyncTicks (const Idea& I);   // where the figure starts again from step 1
std::vector<Note> placeMotif (const std::vector<Note>& line, const std::vector<Note>& motif, int motifT0, int motifLen, int phraseTicks, int totalTicks);

// ---------------------------------------------------------------- MOTIF HARVESTER (learns from keeps)
constexpr int kHarvestFeatures = 6;
using HFeat = std::array<double, kHarvestFeatures>;
const char* harvestFeatureName (int k);   // repetition, rhythm, contour, intervals, landing, range
struct HarvestWeights { HFeat w { 1.0, 1.0, 1.0, 1.0, 1.0, 1.0 }; int keeps = 0; };
struct Fragment { int t0 = 0, t1 = 0, lane = LaneMelody; std::vector<Note> notes; HFeat f {}; double score = 0; };
HFeat fragmentFeatures (const std::vector<Note>& frag, const std::vector<Note>& whole, const Harmony& H, int t0, int t1);
std::vector<Fragment> harvestFragments (const Idea& I, const HarvestWeights& W, int maxN = 8);
void learnKeep (HarvestWeights& W, const HFeat& f, bool kept);
std::string weightsToString (const HarvestWeights& W);
HarvestWeights weightsFromString (const std::string& s);

// ---------------------------------------------------------------- PIANO TEST: does it still work as plain piano (no glides, flat velocity, decaying notes)?
double pianoTest (const std::vector<Note>& notes, const Harmony& H, int spanTicks);
constexpr double kPianoMin = 0.42;

// ---------------------------------------------------------------- HOOK LAB
struct HookParams
{
    int bars = 4; int style = StyleAuto; int candy = CandyOff; double tension = -1; uint32_t seed = 1; int count = 16; const HarvestWeights* taste = nullptr;
    int reg = RegMid;                                   // REGISTER
    int meter = 0, euclidHits = 0, euclidRot = 0, feel = 0;   // IDM + FEEL (Meter, HookFeel below)
    const std::atomic<bool>* cancel = nullptr;          // background generation: stop early
    std::atomic<int>* progress = nullptr;               // tiles made so far
};
struct Hook
{
    std::vector<Note> notes; int bars = 4; uint32_t seed = 0; double score = 0, piano = 0; HookFeatures f; bool star = false; std::string label;
    std::string harm;          // the harmony it was written over (harmonyPrint); a tile over other chords is refit or dropped
    bool kept = false;         // a starred tile carried over from older chords (refit)
    double fresh = -1;         // FIND THE MAGIC: share of notes that are new vs the source (-1 = not a variation)
    int meter = 0, feel = 0;   // what it was made with (the tile shows the meter cycle)
    int ticks() const { return bars * BAR; }
};
std::string harmonyPrint (const Harmony& H);
// ---------------------------------------------------------------- IDM + FEEL: odd meters (cycles against the 4/4 grid), euclidean rhythms, half-time / drop
enum Meter { Meter44 = 0, Meter34, Meter54, Meter78, Meter98, kMeters };
const char* meterName (int m);
int meterSteps (int m);                         // 16ths per cycle: 16 12 20 14 18
std::vector<int> meterGroups (int m);           // accent groups in 16ths (7/8 = 4 4 6, 5/4 = 12 8 ...)
std::vector<bool> euclid (int hits, int steps, int rotate);
enum HookFeel { FeelNormal = 0, FeelHalf, FeelDrop, kFeels };
const char* feelName (int f);
std::vector<Note> meterLine (const std::vector<Note>& src, const Harmony& H, int meter, int totalTicks);   // the first cycle, repeated (refit) every meter-cycle, group accents
std::vector<Note> euclidLine (const std::vector<Note>& src, const Harmony& H, int hits, int steps, int rotate, int totalTicks);   // onsets on the euclidean 16ths, pitches from the line
std::vector<Note> halfTimeLine (const std::vector<Note>& src, const Harmony& H, int totalTicks);   // half the speed: the first half stretched over the whole
std::vector<Note> dropLine (const std::vector<Note>& src, const Harmony& H, int totalTicks, uint32_t seed);   // on a 3-3-2 kick grid, sparser, wider leaps, lower
std::vector<Note> meterAccents (const std::vector<Note>& n, int meter);
std::vector<Note> euclidArp (const std::vector<Note>& notes, const Harmony& H, int hits, int steps, int rotate, int stepTicks, int totalTicks);
std::vector<Note> cleanArp (const std::vector<Note>& notes, const Harmony& H);   // no same note twice in a row (wrap included), span <= 15
std::vector<Note> shapeArpIdm (const Idea& I, const std::vector<Note>& notes);   // the idea's arp METER + EUCLID                 // key, scale, chord loop: what a hook depends on
void refitHook (const Idea& I, Hook& h, int reg = RegMid);    // the hook over the idea's current chords (pitches only), re-scored
std::string hookCharacter (const Hook& h);                    // "riff", "rising", "call & answer"... (the tile label)
HookParams hookParams (const Idea& I, int bars, uint32_t seed);
std::vector<Note> shapeLine (const Idea& I, const std::vector<Note>& notes, int totalTicks, int style, int candy, double tension, uint32_t seed, int reg = RegMid, int octShift = 0, bool keepRhythm = false);   // style -> tension -> candy -> register -> lanes -> key guard
double hookScore (const Idea& I, const std::vector<Note>& notes, int ticks, int style, const HarvestWeights* taste, double* pianoOut = nullptr);
std::vector<Hook> generateHooks (const Idea& I, const HookParams& hp);
std::vector<Hook> findMagic (const Idea& I, const Hook& h, const HookParams& hp, int count);
bool keepsMagic (const Hook& from, const Hook& to, const Harmony& H);   // motif rhythm, contour, peak, interval signature all kept
std::vector<Note> makeSong (const Idea& I, const Hook& h, int bars, const HookParams& hp);
Hook combineHooks (const Idea& I, const Hook& rhythm, const Hook& opening, const Hook& ending);
std::vector<Note> loopHook (const std::vector<Note>& hook, int hookTicks, int totalTicks, const Harmony& H);   // repeats refit to the chords under them
} // namespace arp
