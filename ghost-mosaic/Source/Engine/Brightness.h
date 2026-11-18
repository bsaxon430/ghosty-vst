// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
// A piece's brightness: its spectral centroid (power-weighted mean frequency), for TERRAIN's darker / brighter
// preference. Kept in its own file so juce_dsp stays out of the engine's other code.
#pragma once
namespace mosaic
{
// x[0..n) at `rate`; up to the first 4096 samples are used. Any thread (its FFT is per thread).
float spectralCentroidHz (const float* x, int n, double rate);
}
