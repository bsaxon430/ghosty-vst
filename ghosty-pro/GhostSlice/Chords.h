#pragma once
// GHOST SLICE chord track (phase 17 GO, docs/research/ghost-slice/POLYPHONIC.md): read-only chord segments of the loaded audio.
//
//   frames   a 170 ms frame (2^k >= 0.17 s at the source rate), hop a quarter of it, Hann window, whitened magnitude spectrum
//   multi-f0 per frame: harmonic-sum salience per MIDI note 36-88 with Klapuri's partial weighting, pick the best, cancel its
//            partials, repeat while the salience stays above 30 % of the first (Klapuri, "Multiple fundamental frequency
//            estimation by summing harmonic amplitudes", ISMIR 2006). The stop rule is relative to the strongest note of the
//            chord register (a loud melody note on top does not end the search) and notes above MIDI 69 weigh less. The notes
//            are folded into a 12-bin pitch-class profile (chroma: Fujishima, "Realtime chord recognition of musical sound",
//            ICMC 1999) and blended with the plain whitened-spectrum profile, which keeps the third / fifth that the
//            cancellation removes together with a bass note's partials. Unpitched frames (the first salience not standing out
//            of the mean: noise) are N.C.
//   chords   cosine match of the profile against binary chord templates (maj / min / 7 / dim / sus4 on 12 roots) plus a
//            no-chord state, smoothed over time by Viterbi decoding with a chord-change penalty (the template + HMM scheme of
//            Sheh & Ellis, ISMIR 2003, and Cho & Bello, IEEE TASLP 2014). Silence and single-pitch frames are N.C.
//   bounds   each chord change is snapped to the strongest spectral-flux rise within one hop (sample accurate after that)
//   quality  shown only when it is clearly better than the other qualities on the same root; otherwise the root alone ("C?")
//   key      the note key (Krumhansl-Kessler on the monophonic notes) blended with the chords: the chord tones' pitch-class
//            profile against the same templates, plus how much of the music is diatonic to each key and sits on its tonic chord
// Our own code from the papers above only: no third-party model, runtime or (GPL / AGPL) reference implementation.
// Deterministic: the same samples give the same chords, bit for bit on one build. Any thread except the audio thread.
#include "Analysis.h"

namespace gslice::analysis
{
constexpr int kChordVersion = 1;

enum class ChordQuality : int { Maj = 0, Min = 1, Dom7 = 2, Dim = 3, Sus = 4, RootOnly = 5 };
struct Chord
{
    int64 s0 = 0, s1 = 0;            // SF span
    int root = -1;                   // 0 = C .. 11 = B; -1 = N.C. (no chord: silence, one pitch, noise)
    ChordQuality quality = ChordQuality::RootOnly;
    float confidence = 0;            // 0..1 (how clearly this chord beats the next best one over the segment)
    bool isChord() const { return root >= 0; }
    juce::String name (int transpose = 0) const;   // "C", "Am", "G7", "Bdim", "Dsus", "F?" (root only), "N.C."
    bool operator== (const Chord& o) const = default;
};

struct ChordTrack
{
    std::vector<Chord> chords;       // contiguous, sorted, covering [0, frames)
    int64 frames = 0;
    // the chord at source frame s (nullptr outside)
    const Chord* at (int64 s) const;
    double chordShare() const;       // the share of the time with a chord (not N.C.)
    juce::String digest() const;
    bool operator== (const ChordTrack& o) const = default;
};

struct ChordSettings
{
    int minMidi = 36, maxMidi = 88, maxNotes = 6;
    double stopRatio = 0.30;         // the multi-f0 stops below this share of the first note's salience
    double noChord = 0.50;           // the N.C. state's score (cosine units): a profile that fits no template better is N.C.
    double changePenalty = 1.0;      // Viterbi: the cost of a chord change (in cosine units x frames)
    double silenceDb = -55.0;        // frames below this (or 45 dB under the loudest frame) are silent = N.C.
    double qualityMargin = 0.035;    // a quality is named when it beats the others on its root by this much
    double minPeakiness = 1.8;       // the first note's salience / the mean salience: below = unpitched (noise) = N.C.
    double rawShare = 0.8;           // the plain spectral pitch-class profile's share in the blend with the multi-f0 one
    int melodyFrom = 69;             // notes above this MIDI note count less (a melody on top passes through the chord)
};

ChordTrack detectChords (const float* x, int64 n, double sr, const ChordSettings& s = {}, const std::atomic<bool>* cancel = nullptr);

// the chord tones (pitch classes) of a quality, root first
std::vector<int> chordTones (ChordQuality q);
// key detection with chords: the note key's pitch-class weights (notes) blended with the chords (see the header comment).
// No chords = estimateKey (notes), unchanged.
Key estimateKeyWithChords (const std::vector<Note>& notes, const ChordTrack& chords);
// how well a chord (root, quality) fits a key: 1 = one of its diatonic chords, 0.6 = its root is on the scale, 0 = neither
double diatonicFit (int root, ChordQuality q, int tonic, bool minor);

namespace cache
{
    bool writeChords (const juce::File& dir, const ChordTrack& t, int64 frames);   // chords-v1.bin (material free)
    bool readChords (const juce::File& dir, int64 frames, ChordTrack& out);
}
} // namespace gslice::analysis
