#pragma once
// TRAP and HIP HOP: the two moods with their own engines (chords, melody, arp, bass). Every pattern is an original archetype
// written from general genre grammar (half-time trap motifs, 808 slides and rolls, swung loop-based hip hop), stored as data
// below in UrbanMoods.cpp. Each mood has five FLAVOURS (musical names only). Deterministic from the idea's seeds; message thread.
#include "Idea.h"

namespace arp
{
constexpr int kFlavours = 5;          // the shipped five of each mood (HIP HOP AUTO + a borrowed lane's seeded flavour pick from these)
constexpr int kEmo = 5;               // TRAP's sixth flavour (appended): EMO
inline int flavourCount (int mood) { return mood == Trap ? 6 : kFlavours; }
const char* flavourName (int mood, int flavour);     // TRAP: DARK TRIUMPHANT HYPNOTIC DRILL RATCHET EMO; HIP HOP: BOOM BAP MEMPHIS WEST GLIDE UNDERWATER JAZZ SOUL
struct FlavourInfo { int bpmLo = 0, bpmHi = 0; double swingLo = 50, swingHi = 50; bool doubleTimeOk = false; };
FlavourInfo flavourInfo (int mood, int flavour);   // tempo + swing ranges (tests / UI)
int urbanBassArchCount (int mood);
int urbanMelArchCount (int mood);
const char* urbanBassArchName (int mood, int i, int flavour = -1);   // "and-of-2 push", "rolling", ... (flavour = EMO: its own tables)
const char* urbanMelArchName (int mood, int i, int flavour = -1);
// ---- EMO (TRAP): major-diatonic loops heard as minor (P1..P11), a fingerpicked guitar bed (GA archetypes) on the arp lane, a
// singable sad melody, a softer 808 that follows the roots (EB archetypes). Grids at the felt tempo: one felt bar = 2 bars.
int emoProgCount();
const char* emoProgName (int i);
std::string emoProgText (int i);                  // the progression's tokens (tests)
int emoArpArchCount();
const char* emoArpArchName (int i);
int emoBassArchCount();
int emoMelArchCount();
const char* emoBassName (int i);
const char* emoMelName (int i);
int emoArchFor (const Idea& I);                   // the guitar bed's archetype (GA) for this idea
std::vector<Note> emoMelody (const Idea& I, uint32_t seed, int* archOut = nullptr);
std::vector<Note> emoArp (const Idea& I, uint32_t seed);
std::vector<Note> emoBass (const Idea& I, uint32_t seed, int* archOut = nullptr);
constexpr int kEmoFeltBar = 2 * 384;              // ticks (= 2 bars at the session tempo)
inline bool isEmo (const Idea& I) { return I.mode == Trap && I.flavourUsed == kEmo; }
int emoLead (const Idea& I);                      // the melody's sound: 0 = the soft lead (TRAP FLUTE), 1 = CLEAN GUITAR PLUCK
int trapAutoFlavour (uint32_t seedUsed);          // TRAP AUTO: DARK / DRILL / EMO by the seed (0.40 / 0.35 / 0.25); PUSH FURTHER rotates
// the 8 rotation settings (style table, section C): tempo band, scale, snare grammar, hat grammar, 808 archetype, 808 slides,
// lead timbre, melodic archetype. How many differ between two ideas.
int rotationDistance (const RecentIdea& a, const RecentIdea& b);
// the flavour a lane composes in (the idea's own one, or a seeded one when the lane borrows TRAP / HIP HOP via LANE MOOD)
int flavourFor (const Idea& I, int mood);
// DARKNESS (0..100): scale choice toward phrygian / harmonic minor, b2 / b5 devices, the melody up to an octave lower, sparser
// rhythms, longer 808 sustains, slower slides, darker progressions. 0 = a brighter minor (dorian / pentatonic). Never major in TRAP.
inline int defaultDarkness (int mood) { return mood == Trap ? 70 : 40; }
inline int darknessOf (const Idea& I, int mood) { return I.mode == mood && I.darkness >= 0 ? clampv (I.darkness, 0, 100) : defaultDarkness (mood); }
// GENERATE (from newIdea): flavour, scale, key, tempo, swing, progression, chord shape + lane defaults
void urbanSetup (Idea& I);
void urbanPickProg (Idea& I, uint32_t seed, bool avoidCurrent);   // another progression from the flavour's pool
void urbanVoicings (Idea& I);                                    // after rebuildChords: the moods' own chord voicings
// lanes (mood = TRAP / HIP HOP; seed = the lane's seed). arch out = the archetype used (variety bookkeeping)
std::vector<Note> urbanMelody (const Idea& I, int mood, uint32_t seed, int* archOut = nullptr);
// the 3.2.1 TRAP flute writer (Idea::melVer 1; DARK / TRIUMPHANT / HYPNOTIC / DRILL): its gestures (urbanMelArch = 100 + gesture)
const char* fluteGestName (int i);
int fluteGestCount();
std::vector<Note> urbanArp (const Idea& I, int mood, uint32_t seed, const std::vector<Note>& melody);
std::vector<Note> urbanBass (const Idea& I, int mood, uint32_t seed, int* archOut = nullptr);
// swing (%) <-> GROOVE (the playback swing: an offbeat 1/16 moves by groove * 33 % of a 1/16)
inline double swingToGroove (double pct) { return clampv ((pct - 50.0) / 16.5, 0.0, 1.0); }
inline double grooveToSwing (double g) { return 50.0 + 16.5 * g; }
} // namespace arp
