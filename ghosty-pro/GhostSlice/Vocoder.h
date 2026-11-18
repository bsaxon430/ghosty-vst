#pragma once
// GHOST SLICE build 6 - the voice vocoder: WORLD analysis + our own harmonic-plus-noise resynthesis.
//
// Why (docs/sound-research.md has the full story): the old INSTRUMENT held a vowel by TD-PSOLA grains re-ordered by a random walk
// over the vowel's periods. Every period came from a different place, so neighbouring grains did not line up (roughness, "buzz"),
// grains were normalised sample by sample (a gain that pulses at the pitch rate), and keys between zones were resampled (formants
// moved). Measured, that is what made it sound distorted. Build 6 analyses the voice ONCE into a smooth description and plays that:
//   analysis   WORLD v1.0.1 (M. Morise, modified BSD licence; vendored / fetched pinned, its licence ships with the plug-in):
//              our own pitch track (the analysis' pYIN-style tracker) refined by StoneMask, the spectral envelope by CheapTrick
//              (pitch-adaptive, so harmonics do not leak into the envelope) and the aperiodicity (how much of each band is noise) by
//              D4C, every 5 ms. Stored compactly on a mel-spaced grid (192 envelope points, 48 aperiodicity points).
//   synthesis  our own, after the sinusoidal + noise model (X. Serra & J. Smith, "Spectral Modeling Synthesis", CMJ 14(4) 1990;
//              McAulay & Quatieri, IEEE TASSP 34 (1986)): every harmonic of the target pitch is a sine whose level is read off the
//              envelope at ITS frequency (so the formants stay where the voice put them at any pitch: no chipmunk, no munchkin),
//              phase-continuous, fixed random start phases (a soft, non-buzzy wave), nothing above 0.47 x the sample rate (no
//              aliasing, ever) and a gentle roll-off of the very top (no harsh fizz); the noisy share (breath, consonants) is shaped
//              noise from the same envelope x aperiodicity (STFT overlap-add). The result is clean at any pitch and any length.
// Offline (a worker thread; it allocates). Deterministic per seed, bit for bit on one build.
#include "GenDsp.h"
#include <array>
#include <memory>

namespace gslice::gen::voc
{
using Buf = juce::AudioBuffer<float>;
constexpr int kEnvPts = 192, kApPts = 48;
constexpr double kFrameS = 0.005;                 // 5 ms frames

// the frequency (Hz) of envelope / aperiodicity grid point i (mel-spaced, 0 .. fs / 2)
double envHz (int i, double fs);
double apHz (int i, double fs);

// one frame of a voice: pitch + the envelope (natural log of power) + the aperiodicity (0 = all harmonic .. 1 = all noise)
struct Frame
{
    double f0 = 0;                                // Hz (0 = unvoiced: only the noise part sounds)
    std::array<float, kEnvPts> env {};
    std::array<float, kApPts> ap {};
    float gain = 1.0f;                            // linear amplitude
    double logEnvAt (double hz, double fs) const; // interpolated on the grid
    double apAt (double hz, double fs) const;
};
using Track = std::vector<Frame>;                 // frames kFrameS apart

// the analysis of a sound (mono, fs): one Frame per 5 ms
struct Frames
{
    double fs = 48000;
    std::vector<Frame> f;
    int frames() const { return (int) f.size(); }
    // frame at a fractional index: envelope / aperiodicity interpolated, f0 log-interpolated when both sides are voiced
    Frame at (double idx) const;
    // the mean of the voiced frames in [i0, i1): log-mean envelope, mean aperiodicity, median f0
    Frame average (int i0, int i1) const;
};
// x = mono at fs; f0At(sample) = Hz from the analysis' pitch track (<= 0 = unvoiced). Runs on up to 8 cores, deterministic.
std::shared_ptr<const Frames> analyse (const float* x, int n, double fs, const std::function<double (double)>& f0At);

// a = (1 - w) a + w b (log envelopes, aperiodicity, gain; f0 is left alone)
void blend (Frame& a, const Frame& b, double w);

struct SynthSpec
{
    double formantRatio = 1.0;   // > 1: the envelope read lower (formants move up, CHIPMUNK), < 1: down (DEEP)
    double noise = 1.0;          // the aperiodic (breath) part's level x
    double breathFloor = 0.0;    // aperiodicity never below this (a little air) ...
    double breathCeil = 1.0;     // ... nor above this in voiced frames (CLEAN: less hiss)
    double topHz = 17500.0;      // harmonics roll off (raised cosine) from 0.8 x topHz to topHz
    double tiltDbPerOct = 0.0;   // a gentle spectral tilt above 1 kHz (negative: softer top)
    double airHz = 0.0;          // > 0: in voiced frames the noise (breath) is kept out of the low bands, fading in up to airHz:
                                 // low-band aperiodicity is what made a held vowel sound grainy / raspy; air above it sounds soft
    uint32_t seed = 1;
    bool harmonics = true;       // false: only the aperiodic (breath) part (VoiceMorph's instrument air)
};
// the instrument's amplitude of a harmonic at hz (fundamental f0) as synth() makes it: ln of the linear amplitude (-inf: silent)
double harmonicLnAmp (const Frame& F, double hz, double f0, const SynthSpec& s, double fs);
// the track as audio (mono, track.size() x 5 ms + a little tail)
std::vector<float> synth (const Track& t, double fs, const SynthSpec& s);
} // namespace gslice::gen::voc
