#pragma once
// Linear prediction (our own implementation, from J. Makhoul, "Linear prediction: a tutorial review", Proc. IEEE 63(4), 1975:
// the autocorrelation method, eq. 37 - 38, solved by the Levinson-Durbin recursion, eq. 45 - 46) and the all-pole spectral
// envelope E(w) = G / |A(e^jw)|. Used by the PSOLA epoch marker (the LPC residual) and the WIDE engine's formant correction.
// Double precision, no allocation (the caller owns the buffers), deterministic.
#include <cmath>
#include <vector>

namespace gslice::lpc
{
// r[0..p] of x[0, n) (already windowed by the caller)
inline void autocorr (const double* x, int n, int p, double* r)
{
    for (int k = 0; k <= p; ++k)
    {
        double s = 0; for (int i = k; i < n; ++i) s += x[i] * x[i - k];
        r[k] = s;
    }
}

// a Gaussian lag window (a smoothing of the power spectrum by a Gaussian of bwHz standard deviation, Makhoul 1975 sec. VI-B)
// + a white-noise floor (r[0] * (1 + floor)): keeps the recursion stable on near-periodic frames
inline void lagWindow (double* r, int p, double bwHz, double sr, double floor = 1.0e-6)
{
    const double c = 2.0 * 3.14159265358979323846 * bwHz / sr;
    for (int k = 1; k <= p; ++k) r[k] *= std::exp (-0.5 * (c * k) * (c * k));
    r[0] *= 1.0 + floor;
}

// Levinson-Durbin: a[0] = 1, a[1..p] the predictor polynomial A(z) = sum a_k z^-k; returns the prediction error power (G^2).
// tmp: p + 1 doubles of scratch. A zero frame gives A = 1, error 0.
inline double levinson (const double* r, int p, double* a, double* tmp)
{
    for (int k = 0; k <= p; ++k) a[k] = 0.0;
    a[0] = 1.0;
    double err = r[0];
    if (! (err > 0.0)) return 0.0;
    for (int i = 1; i <= p; ++i)
    {
        double acc = r[i]; for (int j = 1; j < i; ++j) acc += a[j] * r[i - j];
        const double k = -acc / err;
        for (int j = 0; j <= i; ++j) tmp[j] = a[j];
        for (int j = 1; j < i; ++j) a[j] = tmp[j] + k * tmp[i - j];
        a[i] = k;
        err *= 1.0 - k * k;
        if (! (err > 0.0)) { err = 1.0e-30; break; }
    }
    return err;
}

// |A(e^{jw})| at w = 2 pi f / sr, by a complex power recurrence (no trig per coefficient)
inline double magA (const double* a, int p, double w)
{
    const double cr = std::cos (w), ci = -std::sin (w);
    double zr = 1.0, zi = 0.0, sr = 0.0, si = 0.0;
    for (int k = 0; k <= p; ++k)
    {
        sr += a[k] * zr; si += a[k] * zi;
        const double nr = zr * cr - zi * ci, ni = zr * ci + zi * cr; zr = nr; zi = ni;
    }
    return std::sqrt (sr * sr + si * si);
}

// the analysis of one frame: pre-emphasis, Hann window, autocorrelation, lag window, Levinson. x: n samples; a: p + 1 out.
// scratch: >= n + 2 (p + 1) doubles. Returns the error power.
inline double analyseFrame (const float* x, int n, int p, double sr, double lagBwHz, double preEmph, double* a, double* scratch)
{
    double* y = scratch; double* r = scratch + n; double* tmp = r + p + 1;
    double prev = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double v = (double) x[i];
        const double w = 0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * (i + 0.5) / n);
        y[i] = (v - preEmph * prev) * w; prev = v;
    }
    autocorr (y, n, p, r);
    lagWindow (r, p, lagBwHz, sr);
    return levinson (r, p, a, tmp);
}
} // namespace gslice::lpc
