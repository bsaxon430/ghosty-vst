#pragma once
// GHOST VOICE - the voice fusion engine (shared: Ghosty Spectrum's GHOST VOICE page now, a standalone Ghost Voice plugin later).
//
// The idea (the owner, after a 2014 interview): take the "voice information" out of two singers and add it together, so a
// performance comes out sung by a voice nobody has heard before. Classic DSP, no models, no GPU:
//   * voice A = the performance (you). WORLD analysis (Shared/VoiceEngine.h: Harvest f0, CheapTrick envelope, D4C aperiodicity)
//     gives its pitch, timing, words and vibrato - those are kept EXACTLY (the f0 track and the frame timing are A's own).
//   * voice B (and C) = an IDENTITY: the "fingerprint" of a voice = what its vocal tract does for each vowel. Extracted from a
//     recording by clustering its voiced frames by vowel (log F1 / F2 / F3, k-means) and keeping, per cluster, the mean log
//     spectral envelope on a log-frequency grid (60 Hz .. 16 kHz, 256 points, frame level removed), the formants F1..F4, the
//     breath (aperiodicity in 8 bands) and the spectral tilt. Designed identities (the factory presets) are built from
//     formant tables + tilt + bandwidths + character bumps: no recordings of anyone.
//   * fusion, per frame of A: find the identity frames that sing the same vowel as A right now (soft nearest neighbours on the
//     formant pattern + the envelope shape; codebook mapping, Abe et al. 1988), then MORPH A's envelope with that "B view":
//       1. target formants = the log-domain weighted mean of A's and B's (and C's) formants at the blend percentages,
//       2. each voice's envelope is warped along frequency so its formants land on the targets (piecewise-linear dynamic
//          frequency warping anchored at the formants - Pfitzinger 2004 / Shechtman & Sorin 2010 style - which is what keeps
//          a 50/50 blend from sounding like two singers at once: naive averaging leaves BOTH sets of peaks in place),
//       3. the warped log envelopes are averaged with the blend weights (log-spectral interpolation, as in STRAIGHT morphing,
//          Kawahara & Matsui 2003), the frame's level comes from A (your dynamics), the aperiodicity is blended the same way.
//     Formants and envelope peaks of the result therefore land BETWEEN A's and B's by the blend percentage (tests check it).
//   * VOICE DESIGN knobs shape the blended envelope (size / brightness / warmth / resonance / weight, breath, grit, gender-age),
//     PITCH tunes / transposes A's f0 track (formants stay independent), STACK renders detuned / delayed copies and octave layers,
//     and WORLD's pulse + noise synthesis puts it back together (voice::StreamSynth). Consonants: frames where A is unvoiced
//     keep A's own envelope (KEEP MY CONSONANTS), and A's own attacks are spliced back over the onsets.
// Everything here is our own code on top of WORLD (modified BSD, see Voice.cmake / THIRD_PARTY_LICENSES.md). Offline, any thread.
#include "VoiceEngine.h"
#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <vector>

namespace fusion {

constexpr int kGrid = 256;                       // identity envelope grid: log-spaced 60 Hz .. 16 kHz
constexpr int kApBands = 8;                      // breath bands (log-spaced 100 Hz .. 12 kHz)
constexpr int kMaxFrames = 24;                   // identity frames (vowel clusters) per identity
constexpr double kGridLo = 60.0, kGridHi = 16000.0;
inline double gridHz(double i) { return kGridLo * std::exp(i / (kGrid - 1) * std::log(kGridHi / kGridLo)); }
inline double gridPos(double hz) { return (kGrid - 1) * std::log(juce::jmax(kGridLo, hz) / kGridLo) / std::log(kGridHi / kGridLo); }
inline double bandHz(int b) { return 100.0 * std::exp2(b * std::log2(12000.0 / 100.0) / (kApBands - 1)); }

// one vowel of an identity: what the vocal tract does there
struct Frame {
    std::array<float, kGrid> logEnv {};          // natural-log power on the grid, level removed (mean over 200 Hz .. 4 kHz = 0)
    voice::Formants F {};                        // F1..F4 (hz 0 = not found)
    std::array<float, kApBands> ap {};           // aperiodicity per band 0..1 (0 = clean, 1 = all breath)
    float weight = 1;                            // how many source frames it stands for (soft matching prior)
    float f0 = 0;                                // the typical pitch there (Hz; display only, never applied)
};

struct Identity {
    juce::String name; bool designed = false;    // designed = a factory / built identity (no recording behind it)
    std::vector<Frame> frames;
    float medianF0 = 0;                          // Hz of the voice (display / GENDER-AGE hints)
    float tilt = 0;                              // dB per octave 300 Hz .. 3 kHz (negative = darker)
    float breath = 0.15f;                        // overall breathiness 0..1
    float formantScale = 1;                      // fitted vocal-tract size vs the adult-male table (< 1 bigger, > 1 smaller)
    bool empty() const { return frames.empty(); }
    juce::ValueTree toTree() const;              // "IDENTITY" (envelopes as base64 float16 - small)
    static Identity fromTree(const juce::ValueTree&);
    bool operator==(const Identity&) const;
};

// the identity of an analysed sound (voiced frames above the silence gate, within [t0, t1] seconds when given)
Identity extractIdentity(const voice::Analysis& a, float voicing = 0.45f, int maxFrames = kMaxFrames, double t0 = -1, double t1 = -1);

// designed identities (no real singers): Glass Angel, Midnight, Electric Soul, Dream Machine, Velvet, Tiny, Giant, Alien, Choir Of One ...
struct Design {
    const char* name; const char* blurb;
    int vowelSet;                                // voice::VowelsMale / Female / Child as the base table
    float scale;                                 // formant scale (vocal-tract size)
    float f1Semis, f2Semis, f3Semis, f4Semis;    // per-formant offsets
    float bwScale;                               // bandwidths x (narrow = ringing / resonant, wide = soft)
    float tiltDb;                                // dB / oct added to the natural glottal roll-off (+ = brighter)
    float breath;                                // 0..1
    float singerBump;                            // a resonance around 2.8-3.2 kHz ("ring"), dB
    float chestBump;                             // a resonance around 180-260 Hz, dB
    float f0;                                    // typical pitch (display)
};
const std::vector<Design>& factoryDesigns();
Identity makeIdentity(const Design& d);
const std::vector<Identity>& factoryIdentities();   // built once

// ---------------------------------------------------------------------------------------------------------------- params
struct Params {
    // BLEND: A gets 1 - wB - wC
    float wB = 0.5f, wC = 0.0f;
    bool keepConsonants = true;                  // unvoiced frames keep A's own envelope + breath; A's attacks are spliced back
    float clarity = 0.5f;                        // with KEEP: above ~5 kHz the blend leans back to A (s, f, breath detail), 0..1
    // VOICE DESIGN
    float formant = 0;                           // semitones: the whole envelope moves (- = bigger, + = smaller voice)
    float brightness = 0, warmth = 0, breath = 0, grit = 0, resonance = 0, weight = 0, genderAge = 0;   // -1 .. +1 (grit 0 .. 1)
    // PITCH
    bool tune = false; int tuneMode = 0;         // 0 NATURAL (vibrato kept, glide), 1 HARD (flat, instant)
    int root = 0, scale = 1;                     // key for TUNE (scale ids: voice::scaleNames)
    float retuneMs = 60, humanize = 0.3f; bool keepVibrato = true;
    int octave = 0; float semis = 0;             // transpose; the formants stay where VOICE DESIGN puts them
    float voicing = 0.45f;                       // voiced / unvoiced gate (periodicity)
    // STACK
    int voices = 1;                              // 1 .. 4 doubled voices (the ghost choir)
    float detuneCents = 10, spreadMs = 14, width = 0.8f;
    float octDown = 0, octUp = 0;                // levels of the octave layers 0..1
    // region + output
    double trimStart = -1, trimEnd = -1;         // seconds of A (< 0 = the whole take)
    bool transients = true;
    float gainDb = 0; bool normalise = true;
    bool stereoOut() const { return voices >= 2 || octDown > 0.001f || octUp > 0.001f; }
    float wA() const { return juce::jlimit(0.0f, 1.0f, 1.0f - wB - wC); }
    juce::ValueTree toTree() const; static Params fromTree(const juce::ValueTree&);
    static void sanitise(Params&);
    bool operator==(const Params&) const;
};

struct Info {
    std::vector<double> f0;                      // the main voice's pitch per output frame (0 = unvoiced)
    std::vector<voice::Formants> formants;       // the result's formants per output frame
    std::vector<float> mixUsed;                  // the blend weight (1 - wA) actually used per frame (0 on consonants)
    double framePeriod = 5.0; int fs = 0; double t0 = 0;   // t0 = seconds of A where the output starts (trim)
    int frames() const { return (int) f0.size(); }
};

// the performance A (analysis + its audio for the transient splice) fused with B / C. Returns false when cancelled.
bool render(const voice::Analysis& a, const voice::AudioRef& aAudio, const Identity* b, const Identity* c, const Params& p,
            voice::Rendered& out, voice::Progress prog = {}, Info* info = nullptr);

// ---------------------------------------------------------------------------------------------------------------- pieces (tests, SAVE VOICE)
// the "B view" of one A frame: the identity's envelope / formants / breath for the vowel A sings there (soft nearest neighbours)
struct View { std::array<double, kGrid> logEnv {}; voice::Formants F {}; std::array<float, kApBands> ap {}; bool valid = false; };
View viewFor(const Identity& id, const voice::Formants& aF, const double* aLogGrid /* kGrid, level removed, may be null */);
// morph n voices' log envelopes (on the identity grid) with formant-anchored warping; weights sum to 1. Output on the grid.
void morphGrid(const std::array<double, kGrid>* logEnvs, const voice::Formants* formants, const double* weights, int n, double* outLogEnv, voice::Formants* outF = nullptr);
// formants of a frame's power envelope by an all-pole fit (LPC order 14 on the envelope below 5.5 kHz, peaks of the fitted
// spectrum, merged with the envelope's own clear peaks): separates F1 / F2 of open vowels better than the envelope's smoothed peaks
voice::Formants formantsLpc(const double* spec, int bins, int fs, double f0 = 0);
// A's frame envelope (bins over 0..fs/2) -> the grid, level removed (returns the removed level, natural log)
double toGrid(const double* spec, int bins, int fs, double* outLogGrid);
// grid -> bins (natural-log power out)
void fromGrid(const double* logGrid, int bins, int fs, double* outLog);
// the identity of A fused with B / C at the blend (SAVE VOICE): every A frame morphed with its B / C views
Identity blendIdentity(const Identity& a, const Identity* b, const Identity* c, const Params& p);
// the formant frequencies after the GENDER / AGE + FORMANT knobs (factor on the frequency axis) and the pitch shift they add (semitones)
double designWarp(const Params& p);
double designPitchSemis(const Params& p);
// a short description of an identity for the UI ("pitch A2 . bright . breathy")
juce::String describe(const Identity& id);

} // namespace fusion
