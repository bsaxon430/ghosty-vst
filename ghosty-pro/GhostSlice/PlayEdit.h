#pragma once
// GHOST SLICE build 8 - PLAY IT: your hands on the take (the editable piano roll on UNDER THE HOOD + ArpEZator's controls).
//   the roll    notes you move / resize / add / link are YOURS ("locked", ArpEZator's word): they stay through MASTER, ARP, a chord swap
//               and VARY. Notes of the take you delete (or move away) are remembered as removed. Every pitch is snapped to the take's
//               key (an out-of-key note cannot exist), every time to the roll's grid.
//   MASTER      ArpEZator's SIMPLE <-> WILD slider: its own note tools (toolSimpler / toolBusier) thin the line out or fill it in
//   CHORDS      SIMPLE (plain triads) / RICH (7ths, 9ths, 11ths) / AS WRITTEN; a chord in the chord lane can be swapped the way
//               ArpEZator does it (its alternatives: sus, 7th, lush, over the 3rd, swap down / up) or for any other chord in the key.
//               Only chords inside the key are offered (ArpEZator's "borrow" is left out). The melody re-fits to a new chord.
//   ARP         ArpEZator's arp engine (composeArp + shapeArp): FIGURE (its own figure), UP, DOWN, UP-DOWN, RANDOM, PENDULUM,
//               CONVERGE; RATE 1/8 or 1/16; 1-3 OCTAVES. The arp becomes the line the take plays.
//   LINES       ArpEZator's GLIDE / FATTEN / STEP between two notes (NoteEvent::link): GLIDE slides into the next note, FATTEN
//               makes the next note louder / fuller, STEP is the plain change.
// Everything here is deterministic and runs on the worker that makes the take (never on the audio thread). A recipe with no edit
// renders exactly as before (each step is skipped).
#include "Melody.h"
#include <utility>
#include <vector>

namespace gslice::gen
{
struct NoteEdit
{
    std::vector<NoteEvent> locked;                 // your notes (beats), in the take's key
    std::vector<std::pair<double, int>> removed;   // the take's own notes you took away (beat, note)
    std::vector<int> chordAlt;                     // per chord of the take: -1 = as written, else a ChordOption::code (degree x 8 + alternative)
    int chords = -1;                               // -1 AS WRITTEN, 0 SIMPLE (triads), 1 RICH (7ths / 9ths)
    double master = 0.5;                           // SIMPLE (0) <-> WILD (1); 0.5 = as composed
    int arp = 0;                                   // 0 OFF, 1 FIGURE, 2 UP, 3 DOWN, 4 UP-DOWN, 5 RANDOM, 6 PENDULUM, 7 CONVERGE
    int arpRate = 1;                               // 0 = 1/8, 1 = 1/16
    int arpOct = 2;                                // 1..3 octaves
    int grid = 2;                                  // the roll's time grid: 0 1/4, 1 1/8, 2 1/16, 3 1/32 (the UI's; it changes no note)
    // build 9
    std::vector<std::pair<double, double>> rests;  // (beat, length) you emptied by deleting a note of a line: kept empty, also after VARY
    bool onlyMine = false;                         // the line is only your notes (an older take upgraded to edit: its melody became yours)
    double bed = 0.0;                              // INSTRUMENT: the CHORD BED level, 0 = off (a soft pad of the chords from the same voice)
    bool notesEdited() const { return ! locked.empty() || ! removed.empty() || ! rests.empty() || onlyMine; }
    bool music() const;                            // anything that changes what the take plays
    bool operator== (const NoteEdit& o) const;
    bool operator!= (const NoteEdit& o) const { return ! (*this == o); }
    juce::var toVar() const;
    static NoteEdit fromVar (const juce::var& v);
};

namespace edit
{
constexpr int kArps = 8;
const char* arpName (int a);                       // OFF / FIGURE / UP / ...
const char* chordsName (int c);                    // AS WRITTEN / SIMPLE / RICH
double gridBeats (int grid);                       // 1, 0.5, 0.25, 0.125
const char* gridName (int grid);                   // 1/4 .. 1/32
double snapTime (double beat, int grid);
int snapPitch (int note, const Key& k);            // the nearest note of the key (ties: down)

// the chords you can swap one chord of the take for, all in the key (ArpEZator's alternatives of that chord, then every other chord of
// the key). written = the chord the style wrote; kind = SIMPLE / RICH / AS WRITTEN (how much colour the options get)
struct ChordOption { Chord chord; juce::String label; int code = -1; };   // code: what NoteEdit::chordAlt stores (SIMPLE / RICH still colour it)
Chord chordFor (const Key& k, const Chord& written, int code, int kind);   // a code -> the chord (the written one when it is not in the key)
std::vector<ChordOption> chordOptions (const Key& k, const Chord& written, int kind);
// the take's chords with CHORDS (SIMPLE / RICH) and the swaps applied; changed[i] = chord i is not the written one
std::vector<Chord> editChords (const std::vector<Chord>& written, const Key& k, const NoteEdit& e, std::vector<bool>* changed = nullptr);
// the notes over the chords that changed re-fit to them (on-beat / long notes onto a chord tone, the rest onto the key)
void refit (std::vector<NoteEvent>& line, const std::vector<Chord>& chords, double beatsPerChord, const Key& k, const std::vector<bool>& changed);
// ArpEZator's arp over the chords (MIDI around centre), bars long
std::vector<NoteEvent> arpOver (const std::vector<Chord>& chords, double beatsPerChord, const Key& k, int bars, int centre, const NoteEdit& e, uint32_t seed);
// MASTER: ArpEZator's SIMPLER / BUSIER tools on the line (the further from 0.5, the more)
std::vector<NoteEvent> master (const std::vector<NoteEvent>& line, const std::vector<Chord>& chords, double beatsPerChord, const Key& k, int bars, double amount, uint32_t seed);
// the music step (before the take's own processing): chords, refit, ARP, MASTER. Writes the written chords to `written` (for the UI).
// arpOk / masterOk: whether the take type uses them. Returns true when something changed.
bool editSong (Song& s, const Key& k, int centre, int bars, uint32_t seed, const NoteEdit& e, bool arpOk, bool masterOk, std::vector<Chord>* written = nullptr);
// the line step (what the roll shows, what plays): the removed notes out, your notes in (one line when mono: what they overlap is cut),
// everything in the key and inside the length, sorted
std::vector<NoteEvent> editLine (std::vector<NoteEvent> gen, const NoteEdit& e, const Key& k, int bars, bool mono);
// what plays: FATTEN lifts the note after the line (ArpEZator's playVel); GLIDE is the renderer's (and the MIDI's: the notes overlap)
std::vector<NoteEvent> playLine (const std::vector<NoteEvent>& line);
std::vector<NoteEvent> midiLine (const std::vector<NoteEvent>& line);   // playLine + GLIDE as overlapping notes (a DAW's legato / glide)
} // namespace edit
} // namespace gslice::gen
