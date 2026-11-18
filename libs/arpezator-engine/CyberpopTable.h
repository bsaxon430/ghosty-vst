#pragma once
// CYBERPOP (MUTANT ARP mode): the style table our generator samples from. It holds aggregate, low-order statistics only, learned
// from MIDI packs by tools/learn_pack.py (which also documents what is never kept: no sequences, phrases, n-grams or names).
// The numbers live in CyberpopData.inc, written by that tool; the generator (Mutant.cpp, generateOne with M.learned) turns them
// into a cell: an onset grid, a note-length draw, a move drawn from the interval histogram snapped to the chord / scale, the
// velocity curve, ratchets where the packs put them, and the repeat period / variation the packs show.
namespace arp
{
struct CyberTable
{
    const char* source; int files, notes;        // where it came from (a label, never a track or artist name) and how much
    double perBar, perBarSd, swing;             // notes per bar (mean, sd); the mean offbeat-16th position (0.5 = straight)
    double onset[16];                           // P(an onset at each 1/16 of the bar)
    double dur[6];                              // note-length shares: < 1/16, 1/16, 1/8, 1/4 (3..5 sixteenths), 1/2, longer
    double interval[25];                        // P(the move to the next note is -12 .. +12 semitones; larger folded into +-12)
    double leap, oct, reversal, repeat, step;   // |move| >= 7, an octave, direction change after a leap, the same note, a step (1-2)
    double chordStrong, chordWeak, scale, minor;   // tonic-triad share on strong / weak steps, in-scale share, share of minor-key tracks
    double degree[12];                          // pitch-class weights relative to the key (0 = tonic)
    double velPos[16]; double velSd;            // mean velocity (0..1) by 1/16 position; its spread
    double ratchet; double ratchetPos[4];       // share of onsets under a 1/16 after the previous one; where in the beat they fall
    double motifBars[3]; double barRepeat, barRepeatExact, variation;   // repeat period shares (1 / 2 / 4 bars); bars repeating an earlier bar's rhythm (and pitches); notes changed on a repeat
    int range, reg;                             // median range (semitones) and median pitch (MIDI)
};
inline const CyberTable& cyberTable()
{
    static const CyberTable T =
#include "CyberpopData.inc"
    ;
    return T;
}
} // namespace arp
