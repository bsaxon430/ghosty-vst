#pragma once
// helpers shared by the analysis files (not a public API)
#include "Analysis.h"

namespace gslice::analysis::detail
{
// YIN difference function + cumulative mean normalised difference of one frame x[0..W) for lags 0..W/2 (de Cheveigne &
// Kawahara 2002, eq. 6 and 8), computed through an FFT cross-correlation: d(tau) = E0 + E(tau) - 2 r(tau).
struct Yin
{
    explicit Yin (int W);
    int W, M;
    FFT fft;
    std::vector<double> re, im, sq, cr, ci;
    void cmnd (const double* x, std::vector<double>& dprime);   // dprime.size() = W/2 + 1
};

std::vector<double> hann (int N);                                    // periodic Hann
double percentile (std::vector<double> v, double q);                 // q in [0, 1]; empty -> 0
inline double median (std::vector<double> v) { return percentile (std::move (v), 0.5); }
void zeroPhaseLowpass (std::vector<double>& v, double fc, double fs); // 2nd-order Butterworth, forward + backward
void zeroPhaseHighpass (std::vector<double>& v, double fc, double fs);
// sample-accurate onset inside [a, b) of x (1 ms-hop, 2 ms RMS envelope; last local minimum before the rise, then the first
// sample above 15 % of the local peak above the floor). Returns a when there is nothing to refine.
int64 refineOnset (const float* x, int64 n, double sr, int64 a, int64 b);
double levelDb (const float* x, int64 n, int64 a, int64 b);          // rms of [a, b) in dBFS
double flatness (const float* x, int64 n, double sr, int64 a);       // spectral flatness of the 20 ms from a (0 tonal .. 1 noise)
inline double interp (const std::vector<double>& v, double i)
{
    if (i < 0 || v.empty()) return 0.0;
    const auto k = (size_t) i; if (k + 1 >= v.size()) return k < v.size() ? v[k] : 0.0;
    const double f = i - (double) k; return v[k] * (1.0 - f) + v[k + 1] * f;
}
} // namespace gslice::analysis::detail
