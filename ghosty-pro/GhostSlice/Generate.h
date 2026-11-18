#pragma once
// GHOST SLICE GENERATE: drop a sound, press GENERATE, get a playable instrument / pad / chop phrase / texture made from it.
//
// Offline (a worker thread; it allocates), deterministic per seed (the same source + recipe gives the same take, bit for bit on one
// build). Built from the engine Ghost Slice already had plus the new voice / grain DSP (GenDsp.h):
//   material   the analysis' syllables (vowel / consonant), notes, onsets, pitch track and key (Analysis.h, Syllables.h). Every piece
//              of the sound is scored for pitch clarity (voicing), stability (cents spread), noise (spectral flatness), loudness and
//              length; breaths and clicks never become pitched material; the stable core of a vowel is found with the HOLD picker.
//   INSTRUMENT the most singable vowel, held by TD-PSOLA grains (formant-true pitching), one sample zone every 3 semitones across the
//              keyboard, looped sustain while a key is held, then the release. Preview: a short melody in the key.
//   PAD        many small grains from the best pitched parts, pitched in key (Tukey grains, spray, scatter), over a PSOLA body and a
//              phase-vocoder freeze of it (random phases: long glassy tones), wide and slowly evolving. Preview: a chord progression
//              for the mood (major / minor), smooth voice-leading, with a chord-following grain layer.
//   CHOPS      bigger chops (syllables) rearranged on a 16th grid into a call / answer phrase in key and tempo, pitched by the phase
//              vocoder (Signalsmith Stretch, formants kept), with repeats, reverses and stutters. Keys (FL names, middle C = C5): C3 up = the chops, C5 = the phrase.
//   TEXTURE    a frozen drone of a longer span, a sub layer and a sparse cloud of long grains sprayed across the whole sound.
// MUSICAL (1) snaps everything to the key / chords and processes more; NATURAL (0) keeps the voice's own pitch movement and character.
// CHARACTER: NATURAL keeps the formants, CHIPMUNK lets them follow the pitch (and plays an octave up), DEEP lowers them (an octave down).
// CLEAR (Recipe::clear, on for new instances, off for takes saved before it existed): the cleanest, truest take. A rendered zone on
// every semitone (no key is resampled from a neighbour: right length, formants in place), steady pitch (no vibrato / drift / detuned
// pair / chorus; the voice's own scoop and a smoothed micro-pitch stay), sinc reads, mono where the source is mono (width only from
// the real stereo and SPACE), no warmth saturation, no limiter on the finish, PAD / TEXTURE: longer, calmer grains over a direct
// voice layer, CHOPS: a chop that needs no repitching plays untouched (tiny corrections: r8brain varispeed, not the phase vocoder).
// The audio thread never sees any of this: it plays the finished Take (GenerateEngine.h).
#include "SourceAudio.h"
#include "Analysis.h"
#include "GenDsp.h"
#include "Melody.h"
#include "Vocoder.h"
#include "VoiceMorph.h"
#include "PhaseLock.h"
#include "PlayEdit.h"
#include <array>
#include <mutex>
#include <juce_audio_basics/juce_audio_basics.h>
#include <functional>
#include <memory>

namespace gslice::gen
{
enum class Type { Auto = 0, Instrument = 1, Pad = 2, Chops = 3, Texture = 4 };
constexpr int kTypes = 5;
const char* typeName (Type t);                      // AUTO / INSTRUMENT / PAD / CHOPS / TEXTURE
Type typeFromName (const juce::String& s);

enum class Character { Natural = 0, Chipmunk = 1, Deep = 2 };
const char* characterName (Character c);            // NATURAL / CHIPMUNK / DEEP
Character characterFromName (const juce::String& s);

constexpr double kRate = 48000.0;                   // every take is made at this rate (the engine resamples to the host's)

// what the user asked for (saved with the project: a reopened session re-renders the same takes from these)
struct Recipe
{
    Type type = Type::Auto;         // AUTO resolves per take (it rotates so takes differ): Take::type says what it became
    double musical = 0.7;           // 0 = NATURAL .. 1 = MUSICAL
    int keyTonic = -1;              // -1 = the detected key; else 0 = C .. 11 = B
    bool keyMinor = false;
    Character character = Character::Natural;
    double bpm = 120.0;             // the DAW's tempo, or the standalone's own
    uint32_t seed = 1;              // the take's structure: material, progression, phrase (GENERATE picks a new one)
    uint32_t detail = 1;            // the details: grain walks, voicing, pattern variations (VARY picks a new one)
    int rotation = 0;               // AUTO: which type this press gets
    int variation = 0;              // 0 = an original take, n = the n-th VARY of it
    juce::String name;              // "Take 3 - PAD in A MIN"
    bool favourite = false;
    bool clear = false;             // CLEAR (high fidelity). A recipe without it (saved before CLEAR existed) loads with it off.
    // the user's parts (the boxes on the waveform, seconds of the whole sound, sorted, not overlapping): when set they replace the
    // automatic choice. CHOPS: one chop per part, in order (MIDI 36 up). INSTRUMENT: the part it holds. PAD / TEXTURE: the tone and
    // grain sources. Empty = the generator picks (GENERATE always starts empty; VARY keeps them).
    std::vector<std::pair<double, double>> parts;
    static int maxParts (Type t);                 // INSTRUMENT 8 (the first = the held tone, all = the sung syllables), PAD 8, CHOPS 16, TEXTURE 5
    // build 6 (engine 6: the vocoder voice, the melody styles, the third knob). A recipe saved before build 6 has no "engine": it
    // loads as engine 5 and renders exactly as it did (the old code paths are kept, bit for bit).
    int engine = 8;                 // build 9: 7 = the in-order PSOLA at VOICE 0, chop notes that change octave (6 renders exactly as build 8)
                                    // build 10: 8 = that PSOLA up to an octave, a CHORDS track in the MIDI (7 renders exactly as build 9)
    double knob = -1;               // the third knob: INSTRUMENT VOICE (0 = the voice singing its words .. 1 = the pure instrument),
                                    // PAD DREAM, TEXTURE DRIFT, CHOPS STUTTER (0..1; -1 = the type's default)
    Style style = Style::Auto;      // the melody style (AUTO: picked per take, Take::style says which)
    static constexpr int kDeep = 10;
    std::array<double, kDeep> deep { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };   // the UNDER THE HOOD knobs (per type, see deepName; -1 = default)
    static int deepCount (Type t);                   // how many a type uses
    int bars = 4;                   // LENGTH: 1 / 2 / 4 / 8 / 16 bars of melody, chords or phrase (the preview and DRAG MIDI follow it)
    // build 8 (PLAY IT): your notes on the roll, the chord swaps, MASTER, ARP, SIMPLE / RICH (PlayEdit.h). Default = none: the take
    // renders exactly as before (a recipe saved before build 8 has none)
    NoteEdit edit;
    double knobOr (Type t) const;                     // knob, or the type's default
    double deepOr (Type t, int i) const;              // deep[i], or its default
    static double knobDefault (Type t);
    static double deepDefault (Type t, int i, bool clear);
    static const char* knobName (Type t);             // VOICE / DREAM / STUTTER / DRIFT
    static const char* knobLeft (Type t);             // the words at the knob's ends ("VOICE" .. "INSTRUMENT")
    static const char* knobRight (Type t);
    static const char* knobTip (Type t);
    static const char* deepName (Type t, int i);      // FORMANT / BREATH / VIBRATO / GLIDE, ...
    static const char* deepTip (Type t, int i);
    juce::var toVar() const;
    static Recipe fromVar (const juce::var& v);
};

// one sample zone of a playable take: a sustained sample at rootNote, looped [loopStart, loopEnd) (the crossfade is baked in)
struct Zone
{
    int rootNote = 60;
    juce::AudioBuffer<float> audio;   // stereo at kRate
    int loopStart = 0, loopEnd = 0;   // loopEnd <= loopStart = one shot
};

// a finished take (immutable once published: the audio thread reads it)
struct Take
{
    Recipe recipe;
    Type type = Type::Instrument;      // the resolved type
    int keyTonic = 0; bool keyMinor = false;
    double bpm = 120, rate = kRate;
    int rootNote = 60;                 // where the sound's own pitch sits on the keyboard
    double attackS = 0.005, releaseS = 0.3;
    bool pairDetune = false;           // PAD / TEXTURE: two detuned voices per key, panned apart
    float ensemble = 0.3f;             // how much chorus the FX chain gives this take (0..1)
    std::vector<Zone> zones;           // chromatic playback (the nearest root, resampled the rest of the way)
    std::vector<juce::AudioBuffer<float>> chops;   // CHOPS: one-shots on MIDI 36 upward (FL Studio: C3)
    juce::AudioBuffer<float> preview;  // the audition (dry): melody / progression / phrase / drone
    juce::MidiFile midi;               // the melody (INSTRUMENT), chords (PAD / TEXTURE) or phrase (CHOPS)
    juce::String info;                 // plain words: what it used and how to play it
    juce::String digest;               // SHA-256 of the preview (determinism)
    // build 6
    std::vector<std::vector<Zone>> sylZones;   // INSTRUMENT with VOICE below the far right: one zone set per syllable; each key-press
                                               // plays the next syllable (a vocal chop instrument). Empty: the keys play `zones`.
    float dream = 0.0f;                        // PAD DREAM / TEXTURE DRIFT: how much bigger the space gets in the FX (0..1)
    Style style = Style::Auto;                 // the resolved melody style
    std::vector<NoteEvent> notes;              // what the preview plays (the UNDER THE HOOD piano roll)
    std::vector<Chord> chords; double beatsPerChord = 4.0;
    std::vector<Chord> writtenChords;          // build 8: the chords as the style wrote them (before SIMPLE / RICH / your swaps)
    juce::String progName, form;               // "Am9 - Fmaj7 - ...", "A A' A B"
    juce::StringArray chain;                   // the processing chain, in order (UNDER THE HOOD): "VOICE PARTS", "RESYNTH (WORLD)", ...
    std::vector<std::pair<double, double>> used;   // seconds of the source that went in (the waveform highlights them)
    juce::String keyName() const;      // "A MIN"
    bool clear() const { return recipe.clear; }
    // build 8: what a re-render after an edit on the roll can reuse (the zones, the syllables' models): only the preview is made again
    std::shared_ptr<const void> cache; const void* prepTag = nullptr;
    static constexpr int kChopNote = 36, kPhraseNote = 60;   // MIDI 36 = FL Studio's C3, 60 = C5 (middle C)

    size_t bytes() const;
};

// ---------------------------------------------------------------- material (exposed for tests)
struct Segment
{
    int64 s0 = 0, s1 = 0;        // SF: the whole piece (consonant + vowel + coda)
    int64 v0 = 0, v1 = 0;        // SF: the vowel / stable part (v0 == v1: none)
    double f0 = 0, midi = 0;     // median of the voiced frames (0 = unvoiced)
    double voiced = 0;           // share of voiced frames 0..1 (pitch clarity)
    double stabilityCents = 0;   // spread of the pitch inside the vowel (lower = steadier)
    double rmsDb = -100;
    double flatness = 0;         // spectral flatness 0..1 (1 = noise)
    bool breath = false;         // quiet + unvoiced + noisy: never pitched material
    int kind = 0;                // 0 syllable, 1 note, 2 onset span, 3 equal part
    double instrument = 0, pad = 0, chop = 0;   // scores 0..1
    double seconds (double sr) const { return (double) (s1 - s0) / sr; }
};
std::vector<Segment> findMaterial (const SourceAudio& src, const analysis::Result& a);
// one piece of material exactly where the user put it ([s0, s1) source frames): its vowel, pitch and scores, never rejected
Segment segmentAt (const SourceAudio& src, const analysis::Result& a, int64 s0, int64 s1);

// ---------------------------------------------------------------- music (exposed for tests)
const std::vector<int>& scaleOf (bool minor);
const std::vector<int>& pentatonicOf (bool minor);
bool inScale (int note, const Key& k);
int snapToScale (int note, const Key& k);           // the nearest scale note (ties go down)
double snapToScale (double note, const Key& k);
std::vector<Chord> progression (const Key& k, int centreNote, bool rich, uint32_t seed, uint32_t detail);
std::vector<NoteEvent> melody (const Key& k, int centreNote, int bars, uint32_t seed, uint32_t detail);
std::vector<NoteEvent> phrase (const Key& k, int centreNote, int nChops, const std::vector<bool>& chopIsLong, int bars, uint32_t seed, uint32_t detail);
juce::MidiFile toMidi (const std::vector<NoteEvent>& ev, double bpm, int channel = 1);
std::vector<NoteEvent> notesOf (const Take& t);
// build 10: the chords as notes (each chord held for its span, the whole length) and a second MIDI track of them (channel 2, "CHORDS")
std::vector<NoteEvent> chordNotes (const std::vector<Chord>& chords, double beatsPerChord, double lengthBeats);
void addChordTrack (juce::MidiFile& f, const std::vector<NoteEvent>& ev, int channel = 2);   // the notes a take plays (build 6+: Take::notes; older: read back from its MIDI)

// ---------------------------------------------------------------- the generator
// everything a source needs once (analysis, ranked material, band-limited copies): made on the worker, shared by every take
struct Prepared
{
    std::shared_ptr<const SourceAudio> src;
    std::shared_ptr<const WorkingAudio> work;          // the source at kRate
    std::shared_ptr<const analysis::Result> analysis;
    std::vector<Segment> material;
    dsp::BandBank bank;                                // of work
    Key detected;                                      // the key the sound is in (or the one its pitch suggests)
    bool keyFromAnalysis = false;
    double kS = 1.0;                                   // working frames per source frame
    double f0AtWork (double wf) const;                 // Hz of the analysis' pitch track at a working frame (0 = unvoiced)
    double wf (int64 sf) const { return (double) sf * kS; }
    // build 6: the vocoder analysis of the working audio (WORLD, made once, the first time a take needs it; thread-safe)
    std::shared_ptr<const voc::Frames> vocoder() const;
    mutable std::mutex vocMutex; mutable std::shared_ptr<const voc::Frames> vocFrames;
};
struct Progress { std::function<void (float)> report; std::function<bool()> cancelled; };
// a = the analysis (null: it is made here). Returns null when cancelled or when the sound is too short / silent.
std::shared_ptr<const Prepared> prepare (std::shared_ptr<const SourceAudio> src, std::shared_ptr<const analysis::Result> a, const Progress& p = {});
// reuse (build 8): the take this one replaces. When only the edit (PLAY IT: notes, chords, MASTER, ARP) differs, its zones / models are
// reused and only the preview, the notes and the MIDI are made again (bit for bit what a full render gives, in a fraction of the time)
std::shared_ptr<Take> generate (const Prepared& P, const Recipe& r, const Progress& p = {}, std::shared_ptr<const Take> reuse = nullptr);
bool sameSound (const Recipe& a, const Recipe& b);   // the two differ only in their edit (and name / star)
Type resolveType (const Recipe& r);
Key keyFor (const Prepared& P, const Recipe& r);
juce::String takeName (int index, Type t, const Key& k, int variation = 0);
} // namespace gslice::gen
