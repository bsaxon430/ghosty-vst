#pragma once
// GHOST SLICE build 6 - VOICE <-> INSTRUMENT without ever mixing two different syntheses (no comb, no phasiness).
//
// The voice end is the REAL recording. A syllable is analysed once into a phase-preserving harmonic model + a residual
// (sinusoidal + noise, X. Serra & J. Smith, "Spectral Modeling Synthesis", CMJ 14(4) 1990; R. McAulay & T. Quatieri, IEEE TASSP
// 34(4) 1986 - our own implementation from the papers):
//   harmonics  every 5 ms, along the pitch track, each harmonic's amplitude AND phase, measured by a DFT at exactly h x f0 under a
//              Hann window 4 periods long (its zeros fall on the neighbouring harmonics, so nothing leaks between them)
//   residual   the recording minus the harmonics resynthesised with their own amplitudes and phases (McAulay-Quatieri cubic phase):
//              breath, consonants, the room. Original + nothing lost: harmonics + residual = the recording (to rounding).
// One synthesis then plays the morph:
//   pitch      shape-invariant: harmonic h = h x the new fundamental phase + its own phase offset to the fundamental (the waveform
//              shape of each period stays the voice's: no "phasey" vocoder sound), new f0 = the note, the voice's own scoops and
//              wobble kept by (1 - VOICE)
//   timbre     the formants stay where the voice put them at any pitch: the amplitude of harmonic h is read off the voice's own
//              harmonic-peak envelope (log amplitude, joined peak to peak: a true-envelope estimate, Robel & Rodet 2005's goal)
//              at h x new f0; VOICE moves it (in log amplitude) to the instrument's smooth envelope (WORLD CheapTrick, Vocoder.h)
//   phase      VOICE moves each harmonic's offset from the voice's own to a fixed one (the instrument's calm, buzz-free wave)
//   noise      the residual itself where time runs 1:1 (consonants, breath, as said); stretched vowels get noise shaped like the
//              residual there; the residual fades by (1 - VOICE), the instrument's air comes in with VOICE x BREATH
//   time       consonants 1:1, the vowel stretched to the note by VOICE (at 0 the syllable keeps its own length)
// VOICE 0 with no pitch change is not synthesised at all: the original samples, bit for bit (GenerateV6.inl's exact path).
// Offline (a worker thread). Deterministic.
#include "Vocoder.h"
#include <functional>

namespace gslice::gen::vm
{
constexpr int kHop = 240;            // 5 ms at 48 kHz (the working rate)
constexpr int kMaxH = 160;
constexpr int kBands = 32;           // the residual's noise shape (mel bands)

struct HFrame { double f0 = 0; int nh = 0; std::vector<float> a, ph; std::array<float, kBands> rb {}; };   // amplitude, phase (at the frame centre), residual band levels
struct Model
{
    double fs = 48000; int n = 0;
    std::vector<HFrame> fr;          // frame j centred on sample j x kHop
    std::vector<float> x, harm, resid;   // the segment, its harmonic resynthesis, the residual (x = harm + resid)
    double envAt (const HFrame& f, double hz) const;   // ln amplitude of the harmonic-peak envelope at hz (0 Hz .. top)
};
// x: the segment (mono, fs); f0At (sample) = Hz (<= 0 unvoiced)
// periods: the analysis window in pitch periods (3 = quick, follows fast changes .. 6 = the finest detail; 4 = the default)
Model analyse (const float* x, int n, double fs, const std::function<double (double)>& f0At, int periods = 4);

// what one rendered note does, per output frame (kHop samples)
struct Plan
{
    std::vector<double> src;         // the model frame (fractional) this output frame plays
    std::vector<double> f0;          // the target fundamental (Hz; 0 = no harmonics here)
    std::vector<float> gain;         // overall level
    std::vector<float> inst;         // 0..1: amplitude toward the instrument envelope (log domain)
    std::vector<float> phase;        // 0..1: phase offsets toward the fixed (instrument) ones
    std::vector<float> resid;        // residual level
    std::vector<float> air;          // the instrument's breath level (shaped by `instFrame`)
    std::vector<uint8_t> copy;       // 1: time runs 1:1 here, the residual is the recording's own samples
    std::vector<float> lock;         // 0..1: the fundamental's phase locked to the recording's (only where pitch and time are its own:
                                     // then harmonics + residual give the recording back)
    std::function<voc::Frame (int outFrame)> instFrame;   // the instrument timbre at an output frame (WORLD)
    double formantRatio = 1.0;       // CHIPMUNK / DEEP / FORMANT (1 = the voice's own)
    double breathAirHz = 2400.0;
    uint32_t seed = 1;
};
std::vector<float> render (const Model& m, const Plan& p);
// build 9: lift z's smoothed spectrum, frame by frame, to src's at the same moment (+-12 dB): the PSOLA path's formant guard
void envelopeGuard (const std::vector<float>& src, std::vector<double>& z, int len, double sr, double f0hi, bool smoothEdges = false);   // smoothEdges: build 10 (engine 8)
} // namespace gslice::gen::vm
