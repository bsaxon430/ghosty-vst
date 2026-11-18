#pragma once
// GHOST SLICE build 6 - melodies and harmony from rules (our own code; nothing copied from any song, record or MIDI file).
// The rules are the house's own, ported from the owner's apps (Block Step / Ghost House piano + acid generators, Professor
// Bernstein's ArpEZator HookLab / Composer) and the textbook craft they follow:
//   harmony    progressions per mood (major / minor) and per style, 7ths / 9ths / 11ths where the style wants colour; open voicings
//              (the root low, nothing closer than a 4th under middle C: no mud) voice-led from chord to chord (each voice moves to
//              the nearest note of the next chord)
//   motif      a 2-bar motif (a rhythm CELL per bar + a contour on the scale), then the form A A' A B: A' keeps the rhythm and
//              answers (the second bar's contour mirrored: call / answer), the third phrase is A again or A sequenced a scale step
//              up, B is new and cadences. Strong beats sit on chord tones, every phrase end lands on a chord tone, the last note is
//              long on the tonic chord.
//   line       singable: about an octave and a half around the centre, mostly steps, a leap is followed by a step back the other way
//              (gap fill), no more than two leaps in a row
//   feel       human timing (a few ms early / late, more in LULLABY: the player piano), velocity accents on 1 and 3, phrases that
//              swell to the middle
//   styles     DREAMY, LULLABY (the Aphex piano lullaby), HOOK, SOUL, ARP, ACID (the acid motif form motif A1 motif B motif A2 A1 B2),
//              AMBIENT, VOICE (the sound's own detected melody is the motif). AUTO picks one that suits the take type.
//   VARY       the SAME idea developed: same key / style / first motif, but A' and B get new cells and contours (inversion,
//              sequence or a new answer), the progression gets a related substitute chord and new inversions.
#include <juce_core/juce_core.h>
#include <cstdint>
#include <vector>

namespace gslice::gen
{
struct Key { int tonic = 0; bool minor = false; };
struct Chord { int degree = 0; std::vector<int> notes; juce::String name; std::vector<int> pcs; };   // notes = absolute MIDI (voiced)
struct NoteEvent { int note = 60; double beat = 0, length = 1; float velocity = 0.8f; int chop = -1; bool reverse = false; int stutter = 0; };

// the styles: our own (0..8), then the melody brain's (Brain.h, from 9 on: PIANO, SYNTH, REVERIE, LEARNED, ARPEZATOR, ...). Saved
// by name. Style::Acid is the Block Step acid generator (the brain's ACID); Lullaby is our own MUSIC BOX.
enum class Style { Auto = 0, Dreamy, Lullaby, Hook, Soul, Arp, Acid, Ambient, Voice };
constexpr int kStyles = 9;                           // (our own; styleCount() is every style)
int styleCount();
const char* styleGroup (Style s);
int styleGroupIndex (Style s);                       // the groups in the browser's order                    // GHOST SLICE / PIANO / SYNTH / REVERIE / LEARNED / ARPEZATOR / TRAP / HIP HOP / HOOK LAB
const char* styleName (Style s);                     // AUTO / DREAMY / ...
const char* styleTip (Style s);                      // one line in plain words
Style styleFromName (const juce::String& s);
// AUTO -> a style that suits the take type (1 INSTRUMENT, 2 PAD, 3 CHOPS, 4 TEXTURE), picked by the seed
Style resolveStyle (Style s, int type, uint32_t seed);

struct SeedNote { double midi = 60; double seconds = 0.2; };   // the sound's own melody, in order

struct Song
{
    Style style = Style::Hook;
    std::vector<Chord> chords; double beatsPerChord = 4.0;   // chord i starts at beat i x beatsPerChord
    std::vector<NoteEvent> melody;                            // the lead line
    std::vector<NoteEvent> accomp;                            // the style's accompaniment (brain styles: comp, bass, counter-lines)
    juce::String progName, form;
    int bars = 8;
};
// bars: the LENGTH, 1..16 (longer patterns develop: A A' A B, then the theme lifted and answered anew, a phrase-level arch, a cadence
// at the very end; they never just loop). bpm: a few styles move slower at fast tempos.
// colour 0..1: plain triads .. 7ths / 9ths / 11ths. spread 0..1: close .. wide open voicings. variation > 0: a VARY of the take.
Song compose (const Key& k, int centre, Style st, int bars, uint32_t seed, uint32_t detail, int variation, double colour, double spread,
              const std::vector<SeedNote>& voice = {}, double bpm = 120.0);
// just the harmony (PAD / TEXTURE use it): the style's progression voiced around centre
std::vector<Chord> harmony (const Key& k, int centre, Style st, uint32_t seed, uint32_t detail, int variation, double colour, double spread, double& beatsPerChord, juce::String& name);
bool isChordTone (int note, const Chord& c);
} // namespace gslice::gen
