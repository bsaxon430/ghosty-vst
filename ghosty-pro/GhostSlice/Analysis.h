#pragma once
// GHOST SLICE analysis (phases 7, 13, 9): our own GPL-free implementations of the study's algorithms (GHOST-SLICE-STUDY.md 3).
// Everything here is plain maths on a mono mixdown at the source rate: no GUI, no audio thread, deterministic (same samples and
// settings -> the same results, bit for bit on one build), and cached under cache/analysis/<sha256>/ in versioned files.
//
//   onsets    spectral flux on a log-frequency filterbank (SuperFlux idea) + high-frequency content, adaptive median threshold,
//             peak picking with a minimum distance, sample-accurate refinement on a 1 ms RMS envelope. Material profiles
//             DRUMS / VOCAL / MELODIC (+ AUTO, which picks one). VOCAL adds syllable onsets (energy + spectral change + voicing).
//             References: Bello et al., "A Tutorial on Onset Detection in Music Signals", IEEE TSAP 2005; Dixon, "Onset
//             Detection Revisited", DAFx-06; Boeck & Widmer, "Maximum filter vibrato suppression for onset detection", DAFx-13;
//             syllable nuclei after De Jong & Wempe, Behavior Research Methods 2009.
//   tempo     onset-strength autocorrelation over 60-200 BPM with metrical support, a mild log-tempo prior and a loop-length
//             prior; beat phase by comb search, downbeat from low-band onset energy (after Ellis, JNMR 2007).
//   pitch     a YIN / pYIN-style monophonic tracker: FFT difference function, cumulative mean normalised difference, a
//             threshold distribution (Beta 2/18) over the troughs, parabolic interpolation, Viterbi smoothing over pitch-bin
//             states x voiced / unvoiced, octave-error guard, energy gate. (de Cheveigne & Kawahara, JASA 2002; Mauch & Dixon,
//             ICASSP 2014 - the papers only; no reference code was used.)
//   notes     segmentation of the f0 track (voicing, onsets, pitch-plateau changes), per-note median pitch, drift, vibrato.
//   key       pitch-class profile of note durations x loudness against Krumhansl-Kessler (1982) templates, with confidence;
//             with chords (phase 17, Chords.h) blended with the chord track's pitch classes, diatonic fit and tonic chord.
//   chords    read-only chord segments (multi-f0 -> pitch-class profile -> templates + Viterbi), Chords.h.
#include "SliceCore.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace gslice { struct SourceAudio; }

namespace gslice::analysis
{
// bump on purpose when a change alters results (the cache files carry these; a mismatch = recompute)
constexpr int kOnsetVersion = 1, kTempoVersion = 2, kPitchVersion = 1;   // tempo v2: octave disambiguation (slow loops)

// ---------------------------------------------------------------- our own FFT (radix 2, double; deterministic everywhere)
class FFT
{
public:
    explicit FFT (int n);
    int size() const { return n; }
    void transform (double* re, double* im, bool inverse) const;   // in place, unscaled
private:
    int n = 0; std::vector<int> rev; std::vector<double> cs, sn;
};

std::vector<float> mono (const juce::AudioBuffer<float>& audio);   // mean of the channels
inline int nextPow2 (int v) { int p = 1; while (p < v) p <<= 1; return p; }

// ---------------------------------------------------------------- material profiles + onsets
enum class Material { Auto = 0, Drums = 1, Vocal = 2, Melodic = 3 };
const char* materialName (Material m);                  // AUTO / DRUMS / VOCAL / MELODIC
Material materialFromName (const juce::String& s);

struct Profile
{
    int N = 1024, H = 256;                               // STFT frame / hop at the analysis rate
    double wHfc = 0.5, delta = 0.05, lambda = 1.4;       // ODF = flux + wHfc * hfc; threshold = delta + lambda * median
    double medPreMs = 100, medPostMs = 50, maxPreMs = 30, maxPostMs = 30, minGapMs = 30;
};
Profile profileFor (Material m, double sampleRate);      // the study's table (3.2), frame sizes scaled to the rate

enum EventFlags : uint8_t { kBreath = 1, kSyllable = 2, kSoft = 4 };
struct Event
{
    int64 pos = 0;           // SF (source frames), sample accurate
    float strength = 0;      // 0..1 (onset strength = confidence)
    float flatness = 0;      // spectral flatness of the 20 ms after (noisiness)
    uint8_t flags = 0;       // EventFlags
    int group = -1;          // word / phrase group: events of one group move together in ALIGN (-1 = alone)
    bool operator== (const Event& o) const = default;
};

struct OnsetSet
{
    Material asked = Material::Auto, used = Material::Drums;
    std::vector<Event> events;                           // sorted by position
    juce::String digest() const;
};

// the onset strength function (exposed for tests / tempo): per frame of hop H, centred at f * H
struct OdfFrames { int N = 0, H = 0; double sr = 48000; std::vector<double> flux, hfc, low; };
OdfFrames odfFrames (const float* x, int64 n, double sr, int N, int H, const std::atomic<bool>* cancel = nullptr);

Material classify (const float* x, int64 n, double sr);  // AUTO: DRUMS / VOCAL / MELODIC from voicing + syllable-rate cues
OnsetSet detectOnsets (const float* x, int64 n, double sr, Material m, const std::atomic<bool>* cancel = nullptr);
// VOCAL extra: syllable onsets (flag kSyllable), sample accurate
std::vector<Event> syllableOnsets (const float* x, int64 n, double sr, const std::atomic<bool>* cancel = nullptr);

// ---------------------------------------------------------------- tempo
struct Tempo
{
    double bpm = 0;              // 0 = none
    double confidence = 0;       // 0..1
    double firstBeat = 0;        // seconds into the source: the first beat
    double downbeat = 0;         // seconds into the source: the first downbeat (bar start)
    int bars = 0;                // > 0 when the loop prior decided (the file is this many bars long)
    double octaveRatio = -1;     // the share of strong off-beats / strong beats at the slower reading of the octave pair (-1 = not tested)
    bool operator== (const Tempo& o) const = default;
};
constexpr double kOctaveSlow = 0.5, kOctaveFast = 0.9;   // tempo octave test: below = the slower reading, above = the faster
Tempo estimateTempo (const float* x, int64 n, double sr, const std::atomic<bool>* cancel = nullptr);

// ---------------------------------------------------------------- pitch
struct PitchTrack
{
    double sr = 48000; int hop = 256, win = 2048;
    double fmin = 50, fmax = 1500;
    std::vector<float> f0;           // Hz per frame (0 = unvoiced); frame i is centred at i * hop (SF)
    std::vector<float> voicedProb;   // 0..1
    std::vector<float> rmsDb;        // frame level, dBFS
    int frames() const { return (int) f0.size(); }
    double timeOf (int i) const { return (double) i * hop / sr; }
    juce::String digest() const;
};
PitchTrack trackPitch (const float* x, int64 n, double sr, double fmin = 50, double fmax = 1500, const std::atomic<bool>* cancel = nullptr);

struct Note
{
    int64 s0 = 0, s1 = 0;        // SF span
    double pitch = 0;            // MIDI note number (fractional: 69.12 = A4 + 12 cents), energy-weighted median of the middle 60 %
    double driftCents = 0;       // rms of the slow (< 1.5 Hz) wander around the centre
    double vibratoCents = 0;     // peak-to-peak depth (0 = none)
    double vibratoHz = 0;
    float confidence = 0;        // 0..1 (voicing x stability)
    float loudness = 0;          // linear rms
    bool operator== (const Note& o) const = default;
};
std::vector<Note> segmentNotes (const PitchTrack& t, const std::vector<Event>& onsets);

struct Key
{
    int tonic = -1;              // 0 = C .. 11 = B; -1 = unknown
    bool minor = false;
    double confidence = 0;       // 0..1
    std::array<double, 24> r {}; // correlation per key (0..11 major, 12..23 minor)
    bool valid() const { return tonic >= 0; }
    juce::String name() const;   // "F# MIN" / "C MAJ" / "--"
    juce::String readout() const;   // "F# MIN 82 %"
    bool operator== (const Key& o) const { return tonic == o.tonic && minor == o.minor && confidence == o.confidence; }
};
Key estimateKey (const std::array<double, 12>& pitchClassWeights);
Key estimateKey (const std::vector<Note>& notes);
juce::String pitchClassName (int pc);                    // C C# D ... B
int parseKey (const juce::String& s, bool& minor);       // "F# MIN" -> 6 (minor = true); -1 when not a key
// the smallest chromatic shift (-6..+5 semitones) that moves the source key onto the project key (modes differ: onto its relative)
int keyShift (int srcTonic, bool srcMinor, int dstTonic, bool dstMinor);

// ---------------------------------------------------------------- syllables (phase 16; Syllables.h has the detector)
constexpr int kSyllableVersion = 1;
struct Syllable
{
    int64 start = 0;             // SF: the syllable's first sample (its onset consonant, or the vowel when there is none)
    int64 vowelStart = 0;        // SF: consonant | vowel
    int64 nucleus = 0;           // SF: the vowel's band-energy peak
    int64 vowelEnd = 0;          // SF: vowel | coda
    int64 end = 0;               // SF: the next syllable's start, or where the sound stops
    float confidence = 0;        // 0..1 (prominence x voicing)
    bool operator== (const Syllable& o) const = default;
    int64 consonantLength() const { return vowelStart - start; }
};

// ---------------------------------------------------------------- everything for one source + material
struct ChordTrack;   // Chords.h
struct Result
{
    juce::String fingerprint;
    Material material = Material::Auto;
    OnsetSet onsets;
    Tempo tempo;
    PitchTrack pitch;
    std::vector<Note> notes;
    Key key;
    double medianF0 = 0;         // Hz over the voiced frames (FORMANT base); 0 = no pitch
    std::vector<Syllable> syllables;   // phase 16 (material free; voicing from the pitch track)
    std::shared_ptr<const ChordTrack> chords;         // phase 17 (material free; Chords.h); key = noteKey blended with them
    Key noteKey;                 // the key from the monophonic notes alone
    bool fromCache = false;
    juce::String digest() const;
};

// Runs every analyser (or reads them from cacheDir/analysis/<sha>/: onsets-v1-<material>.bin, tempo-v2.json, pitch-v1.bin, notes-v1-<material>.bin,
// syllables-v1.bin, chords-v1.bin).
// Any thread except the audio thread. cancel is polled; a cancelled run returns null.
std::shared_ptr<const Result> analyse (const SourceAudio& src, Material m, const juce::File& cacheDir, const std::atomic<bool>* cancel = nullptr);

namespace cache
{
    juce::File dirFor (const juce::File& cacheDir, const juce::String& fingerprint);
    bool writeOnsets (const juce::File& dir, const OnsetSet& o, int64 frames);
    bool readOnsets (const juce::File& dir, Material asked, int64 frames, OnsetSet& out);
    bool writeTempo (const juce::File& dir, const Tempo& t, int64 frames);
    bool readTempo (const juce::File& dir, int64 frames, Tempo& out);
    bool writePitch (const juce::File& dir, const PitchTrack& t, int64 frames);                 // pitch-v1.bin (material free)
    bool readPitch (const juce::File& dir, int64 frames, PitchTrack& t);
    // notes-v1-<material>.bin: the notes (they use the material's onsets), the key and the median f0
    bool writeNotes (const juce::File& dir, Material m, const std::vector<Note>& notes, const Key& k, double medianF0, int64 frames);
    bool readNotes (const juce::File& dir, Material m, int64 frames, std::vector<Note>& notes, Key& k, double& medianF0);
    bool writeSyllables (const juce::File& dir, const std::vector<Syllable>& s, int64 frames);   // syllables-v1.bin
    bool readSyllables (const juce::File& dir, int64 frames, std::vector<Syllable>& out);
}

// ---------------------------------------------------------------- the background worker the processor owns
class Analyzer
{
public:
    Analyzer() = default;
    ~Analyzer();
    // (message thread) start analysing; a running job is cancelled first. onDone is called on the worker when a result lands.
    void start (std::shared_ptr<const SourceAudio> src, Material m, const juce::File& cacheDir);
    void stop();
    std::shared_ptr<const Result> result() const { std::lock_guard<std::mutex> l (m); return res; }
    bool busy() const { return running.load(); }
    bool waitIdle (int timeoutMs) const;
    std::function<void()> onDone;
private:
    mutable std::mutex m;
    std::shared_ptr<const Result> res;
    std::thread worker;
    std::shared_ptr<std::atomic<bool>> cancelFlag;
    std::atomic<bool> running { false };
};
} // namespace gslice::analysis
