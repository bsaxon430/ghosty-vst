#pragma once
// GHOST SLICE build 6 - THE MELODY BRAIN: every melody / chord / arp generator the house has made, in one place, as C++.
// Each style is the original's rules (or its learned model), not a copy of any song:
//   GHOST SLICE  our own (Melody.cpp): DREAMY, MUSIC BOX, HOOK, SOUL, ARP, AMBIENT, VOICE
//   PIANO        Block Step / Ghost House's piano page (genPiano's style table, ported from app.js): ANTHEM, PROG, PIANO HOUSE, DEEP,
//                EUPHORIC, LULLABY, RAIN, PREPARED, DRIFT, SKITTER, + BUILD and BRIDGE (Ghost House's song sections as piano shapes)
//   SYNTH        Block Step / Ghost House's synth page (synRows): ACID, AFX ACID, 101 ROLL, FM BELLS, SUPERSAW, PLUCKS, STABS
//   REVERIE      Block Step's original piano composer (revCompose): six emotions, five textures
//   LEARNED      the n-gram brains (Block Step / Ghost House): POP BRAIN (POP909, MIT), ACID BRAIN (Aicd's 303 model, MIT)
//   PARLOUR PIANO build 7: Block Step's learned piano (n-grams from 12 old piano scores on Mutopia; 11 public domain, 1 CC BY-SA 4.0,
//                attributed in THIRD_PARTY_LICENSES.md): WISTFUL, STILL NIGHT
//   ARPEZATOR    Professor Bernstein's engine itself (linked: ArpEZator/*.cpp, JUCE-free): SUNRISE / DUSK / MIDNIGHT arps and hooks,
//                TRAP's six flavours, HIP HOP's five, the HOOK LAB composition styles
// Every style gives chords (one per bar or longer), a lead line (what INSTRUMENT sings, what CHOPS play) and the accompaniment, for
// 1..16 bars, in the take's key (major / minor: anything the original wrote outside it is moved to the nearest scale note).
// Deterministic per seed / detail / variation. Thread-safe (ArpEZator's engine runs under a lock).
#include "Melody.h"
#include <mutex>

namespace gslice::gen::brain
{
struct Info { const char* name; const char* group; const char* tip; };
int count();                            // brain styles (they follow the Melody.h own styles in the Style numbering)
const Info& info (int i);
constexpr int kFirst = 9;               // Style value of brain style 0
inline bool isBrain (Style s) { return (int) s >= kFirst && (int) s < kFirst + count(); }

struct Out
{
    std::vector<Chord> chords; double beatsPerChord = 4.0;   // in order, covering the whole length (chord i at beat i x beatsPerChord)
    std::vector<NoteEvent> lead, accomp;                      // beats; lead is one line (no overlaps)
    juce::String progName, form;
};
// style: a brain index (0..count()-1). centre: where the lead should sit (MIDI). bpm: some styles move slower at fast tempos.
Out make (int style, const Key& k, int centre, int bars, uint32_t seed, uint32_t detail, int variation, double bpm);
// build 8 (PLAY IT): a chord in the key (root = MIDI or pc, iv = intervals, voicing = MIDI notes, low to high), named the house way
Chord chordOf (const Key& k, int root, const std::vector<int>& iv, const std::vector<int>& voicing);
// ArpEZator's engine is not re-entrant: hold this while calling it (its recipe is loaded on the first call)
std::unique_lock<std::mutex> arpEngine();
} // namespace gslice::gen::brain
