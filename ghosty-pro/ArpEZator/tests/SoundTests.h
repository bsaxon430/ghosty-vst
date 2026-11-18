#pragma once
// Preview-sound checks: aliasing of a held lead note (FFT), BS.1770 loudness (K-weighting + gating) of the mood sounds.
// Included by ArpTests.cpp (after render / setP / Counter).
// (needs <complex>, included by ArpTests.cpp)

namespace soundtest
{
using namespace arp;

inline void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap (a[i], a[j]); }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2 * 3.14159265358979323846 / (double) len; const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len) { std::complex<double> w (1); for (size_t k = 0; k < len / 2; ++k) { auto u = a[i + k], v = a[i + k + len / 2] * w; a[i + k] = u + v; a[i + k + len / 2] = u - v; w *= wl; } }
    }
}

// a held note of one preset straight through the engine (no FX); returns dB of the energy that is not near a harmonic of f0
// (aliases fold to arbitrary places; the preset's own unison spread is allowed around every harmonic)
inline double aliasDb (int preset, int pitch, double sr = 48000.0, double* totalDb = nullptr)
{
    arpsyn::Engine e; e.prepare (sr, 512);
    arpsyn::Params P; P.preset = preset; P.delay = 0; P.reverb = 0; P.pump = 0; P.lfoDepth = 0; P.kick = false; P.volume = 0.8f;
    const int N = 16384, pre = 4096;
    std::vector<float> L ((size_t) (N + pre), 0.0f), R ((size_t) (N + pre), 0.0f);
    e.leadStart (2, 1, (float) pitch, 0.9f, (int) (sr * 3), P);
    for (int i = 0; i < N + pre; i += 512) e.render (L.data() + i, R.data() + i, std::min (512, N + pre - i), P, 120, 0, 0.0, false);
    std::vector<std::complex<double>> a ((size_t) N);
    for (int i = 0; i < N; ++i) { const double w = 0.5 - 0.5 * std::cos (2 * 3.14159265358979 * i / (N - 1)); a[(size_t) i] = (L[(size_t) (pre + i)] + R[(size_t) (pre + i)]) * 0.5 * w; }
    fft (a);
    const double f0 = 440.0 * std::pow (2.0, (pitch - 69) / 12.0), bin = sr / N;
    double tot = 0, bad = 0;
    for (int k = 2; k < N / 2; ++k)
    {
        const double f = k * bin; if (f < 40 || f > 20000) continue;
        const double pw = std::norm (a[(size_t) k]); tot += pw;
        const double h = std::round (f / f0);
        const double win = 0.022 * std::max (1.0, h) * f0 + 4 * bin;     // unison spread (up to ~ +-30 cents) + the window's main lobe
        const double sub = std::round (f / (f0 * 0.5));                   // a sub-octave layer's harmonics are legit too
        const bool legit = (h >= 1 && std::abs (f - h * f0) <= win) || (sub >= 1 && std::abs (f - sub * f0 * 0.5) <= 0.022 * sub * f0 * 0.5 + 4 * bin);
        if (! legit) bad += pw;
    }
    if (totalDb) *totalDb = 10 * std::log10 (tot + 1e-30);
    return 10 * std::log10 ((bad + 1e-30) / (tot + 1e-30));
}

// ITU-R BS.1770 integrated loudness (LUFS) of a stereo buffer
struct Biquad { double b0, b1, b2, a1, a2, z1 = 0, z2 = 0; double run (double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; } };
inline double lufs (const juce::AudioBuffer<float>& b, double sr)
{
    // K-weighting (the standard's two stages, re-derived for any rate)
    auto shelf = [&] () { const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196, K = std::tan (3.14159265358979 * f0 / sr);
        const double Vh = std::pow (10.0, G / 20), Vb = std::pow (Vh, 0.4996667741545416), a0 = 1 + K / Q + K * K;
        return Biquad { (Vh + Vb * K / Q + K * K) / a0, 2 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0 }; };
    auto hp = [&] () { const double f0 = 38.13547087602444, Q = 0.5003270373238773, K = std::tan (3.14159265358979 * f0 / sr), a0 = 1 + K / Q + K * K;
        return Biquad { 1, -2, 1, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0 }; };
    const int n = b.getNumSamples(), blk = (int) (0.4 * sr), hop = (int) (0.1 * sr);
    std::vector<double> z ((size_t) n, 0.0);
    for (int c = 0; c < std::min (2, b.getNumChannels()); ++c)
    {
        Biquad s1 = shelf(), s2 = hp();
        for (int i = 0; i < n; ++i) { const double y = s2.run (s1.run (b.getSample (c, i))); z[(size_t) i] += y * y; }
    }
    std::vector<double> ms;
    for (int s = 0; s + blk <= n; s += hop) { double a = 0; for (int i = s; i < s + blk; ++i) a += z[(size_t) i]; ms.push_back (a / blk); }
    auto L = [] (double m) { return -0.691 + 10 * std::log10 (m + 1e-30); };
    double sum = 0; int cnt = 0; for (double m : ms) if (L (m) > -70) { sum += m; ++cnt; }
    if (cnt == 0) return -100;
    const double rel = L (sum / cnt) - 10; sum = 0; cnt = 0;
    for (double m : ms) if (L (m) > -70 && L (m) > rel) { sum += m; ++cnt; }
    return cnt ? L (sum / cnt) : -100;
}
} // namespace soundtest
