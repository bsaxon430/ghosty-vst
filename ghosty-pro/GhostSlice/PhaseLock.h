#pragma once
// GHOST SLICE build 6 - the identity phase-locked vocoder (J. Laroche & M. Dolson, "Improved phase vocoder time-scale modification of
// audio", IEEE TSAP 7(3) 1999; our own implementation from the paper) for DREAM's freeze and long stretches:
//   peaks       local maxima of the magnitude spectrum (within +-2 bins, above -70 dB of the frame's loudest)
//   phase       each peak's phase advances by its instantaneous frequency (measured from two analysis frames one hop apart) x the
//               synthesis hop; every bin in a peak's region of influence keeps its phase relative to that peak (the identity lock:
//               the partials stay coherent, no "phasey", washed-out sound)
//   transients  a spectral-flux onset resets every phase to the analysis phase (attacks stay sharp)
//   stereo      one phase advance, computed from mid, rotates L and R alike (the image stays where it was)
//   random      0 = the clean locked vocoder; toward 1 each bin's phase gets a fresh random turn every frame (at 1: Paulstretch's
//               smear - P. Nasca's published algorithm, re-implemented from its description, no code taken)
// FFT 2048 (transients), 4096 (voices, the default), 8192 (pads and freezes). Offline. Deterministic per seed.
#include "GenDsp.h"
#include <functional>

namespace gslice::gen::pl
{
using Buf = juce::AudioBuffer<float>;
struct Spec
{
    int fftOrder = 12;              // 11 / 12 / 13
    double random = 0.0;            // 0 = locked .. 1 = random phases
    bool transients = true;         // reset at onsets
    uint32_t seed = 1;
};
// read position (input samples, fractional) for every output sample is readAt (t); out has n samples (all channels of in)
Buf process (const Buf& in, double sr, int nOut, const std::function<double (int)>& readAt, const Spec& s);
// a plain time stretch by `ratio` (output = ratio x input length)
Buf stretch (const Buf& in, double sr, double ratio, const Spec& s);
// a freeze: `seconds` of the part [a, b) (samples), read back and forth slowly (one pass every `cycleS` seconds)
Buf freeze (const Buf& in, double sr, double a, double b, double seconds, double cycleS, const Spec& s);
} // namespace gslice::gen::pl
