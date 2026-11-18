#pragma once
// GHOST PITCH, phase 15: the formant correction of edited voiced frames (GHOST-SLICE-STUDY.md 3.8). The stretcher's own formant
// compensation drifts on narrow-formant vowels (4 - 10 % of the spectral centroid in phase 14), and PSOLA grains see the
// envelope through their 2-period window (narrow formants come out blurred, +5 % on pitch-ups), so both engines' edited
// frames are re-shaped toward the SOURCE's envelope:
//   per STFT frame (1024 / 256 at 48 kHz, scaled with the rate; Hann analysis + synthesis, normalised overlap-add), from a
//   2048 frame at the same place: the source frame's harmonic peaks (at the mapped source position, f0 from the track) and the
//   output frame's (f0 x the shift), the target at each output harmonic = the source's harmonic amplitudes resampled as a
//   sequence over the harmonic number with a Lanczos-3 kernel at f / phi / f0 (a band-limited "envelope through the
//   harmonics": a symmetric kernel keeps the envelope's area and first moment, so the centroid stays), phi = 2^(FORMANT / 1200)
//   (a FORMANT offset still moves the envelope on purpose). Gains flat across each harmonic's lobe, changed in the middle third
//   between harmonics, clamped to +-18 dB, level-neutral per frame, smoothed over 3 bins; two passes (the second takes out
//   what the first left). Where the harmonics do not resolve (f0 below 2.5 bins) the LPC envelopes of the two frames are used
//   instead (order 28 at 48 k, pre-emphasis 0.97, a Gaussian lag window of f0 / 2; Lpc.h). Only frames where the curve shifts
//   (or moves the formant) on a voiced frame are touched; elsewhere the stream stays sample for sample (per-frame fade).
// References: J. Makhoul, "Linear prediction: a tutorial review", Proc. IEEE 63 (1975) (LPC); A. Roebel, X. Rodet, "Efficient
// spectral envelope estimation and its application to pitch shifting and envelope preservation", DAFx-05 (the envelope ratio
// correction, an envelope through the harmonic peaks); our own code, offline (the STUDIO worker), deterministic.
#include "RenderPlan.h"
#include "PitchEdit.h"
#include "SourceAudio.h"
#include <atomic>
#include "Analysis.h"

namespace gslice::formantfix
{
extern std::atomic<bool> enabled;   // tests / A-B renders: false = the phase-14 WIDE stream (no correction)
struct Settings { int order = 28; int fftSize = 1024, hop = 256; double maxDb = 18.0, preEmph = 0.97, maxHz = 8000.0; int passes = 2; };
// correct the WIDE stream of a curve piece in place (L / R: len frames from pc.u0). pitch / formantSemis: the global PITCH / FORMANT.
// weightAt(u): 0..1 how much of the correction a region-local frame u takes (the caller masks the other engine out); null = 1.
void apply (const Piece& pc, const WorkingAudio& w, double pitchSemis, double formantSemis, float* L, float* R, int64 len,
            const std::function<float (int64)>& weightAt = {}, const Settings& s = {});
namespace detail
{
struct Harm { std::vector<double> f, a; double f0 = 0; };   // the peaks (Hz, amplitude) of harmonics 1, 2, ... + the re-fitted f0
bool harmonics (const float* x, int n, double sr, double f0, const analysis::FFT& fft, std::vector<double>& re, std::vector<double>& im, double fmax, Harm& out);
// the harmonic amplitudes as a sequence over the harmonic number, read at a fractional harmonic x with a Lanczos-3 windowed
// sinc stretched by `stretch` >= 1 (an anti-aliased resampling when the new harmonics are sparser): a symmetric kernel keeps the
// envelope's area and first moment, so the spectral centroid of the resampled harmonics stays the source's
double bandLimitedAt (const Harm& h, double x, double stretch);
}
}
