// SPDX-License-Identifier: AGPL-3.0-or-later
// GHOST ATLAS - measuring tools used by the analysis and by MUTATE's quality checks (off the audio thread only).
#pragma once
#include "Common.h"

namespace atlas::measure
{
constexpr int kBands = 24;

// 24 bands, 40 Hz .. 16 kHz, equal steps on a log scale (about 0.36 octave each)
inline float bandEdge (int i) { return 40.0f * std::pow (400.0f, (float) i / (float) kBands); }
inline float bandCentre (int i) { return std::sqrt (bandEdge (i) * bandEdge (i + 1)); }

inline std::vector<float> monoOf (const juce::AudioBuffer<float>& b, int start = 0, int length = -1)
{
    const int n = length < 0 ? b.getNumSamples() - start : juce::jmin (length, b.getNumSamples() - start);
    std::vector<float> m ((size_t) juce::jmax (0, n), 0.0f);
    if (n <= 0 || b.getNumChannels() == 0) return m;
    const float g = 1.0f / (float) b.getNumChannels();
    for (int c = 0; c < b.getNumChannels(); ++c)
    {
        const float* s = b.getReadPointer (c, start);
        for (int i = 0; i < n; ++i) m[(size_t) i] += s[i] * g;
    }
    return m;
}

inline float rms (const float* x, int n)
{
    if (n <= 0) return 0.0f;
    double s = 0; for (int i = 0; i < n; ++i) s += (double) x[i] * x[i];
    return (float) std::sqrt (s / n);
}
inline float peak (const float* x, int n) { float p = 0; for (int i = 0; i < n; ++i) p = juce::jmax (p, std::abs (x[i])); return p; }
inline float rmsOf (const juce::AudioBuffer<float>& b, int start, int length)
{
    double s = 0; int count = 0;
    const int n = juce::jmin (length, b.getNumSamples() - start);
    for (int c = 0; c < b.getNumChannels(); ++c) { const float* x = b.getReadPointer (c, start); for (int i = 0; i < n; ++i) s += (double) x[i] * x[i]; count += juce::jmax (0, n); }
    return count > 0 ? (float) std::sqrt (s / count) : 0.0f;
}
inline float peakOf (const juce::AudioBuffer<float>& b)
{
    float p = 0; for (int c = 0; c < b.getNumChannels(); ++c) p = juce::jmax (p, b.getMagnitude (c, 0, b.getNumSamples())); return p;
}

// ---- loudness the way a meter hears it: the two K-weighting filters of ITU-R BS.1770 (shelf + high-pass), then RMS.
// (Not gated and not a certified LUFS meter: it is used to compare sounds with each other, which is what matters here.)
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    float tick (float x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return (float) y; }
    static Biquad highShelf (double fs, double f0, double gainDb, double q)
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = 2.0 * juce::MathConstants<double>::pi * f0 / fs, cs = std::cos (w), sn = std::sin (w), al = sn / (2.0 * q), sq = 2.0 * std::sqrt (A) * al;
        Biquad f; const double a0 = (A + 1) - (A - 1) * cs + sq;
        f.b0 = A * ((A + 1) + (A - 1) * cs + sq) / a0; f.b1 = -2 * A * ((A - 1) + (A + 1) * cs) / a0; f.b2 = A * ((A + 1) + (A - 1) * cs - sq) / a0;
        f.a1 = 2 * ((A - 1) - (A + 1) * cs) / a0; f.a2 = ((A + 1) - (A - 1) * cs - sq) / a0; return f;
    }
    static Biquad highPass (double fs, double f0, double q)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * f0 / fs, cs = std::cos (w), al = std::sin (w) / (2.0 * q);
        Biquad f; const double a0 = 1 + al;
        f.b0 = (1 + cs) / 2 / a0; f.b1 = -(1 + cs) / a0; f.b2 = (1 + cs) / 2 / a0; f.a1 = -2 * cs / a0; f.a2 = (1 - al) / a0; return f;
    }
    static Biquad lowPass (double fs, double f0, double q)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * f0 / fs, cs = std::cos (w), al = std::sin (w) / (2.0 * q);
        Biquad f; const double a0 = 1 + al;
        f.b0 = (1 - cs) / 2 / a0; f.b1 = (1 - cs) / a0; f.b2 = (1 - cs) / 2 / a0; f.a1 = -2 * cs / a0; f.a2 = (1 - al) / a0; return f;
    }
};
inline float loudnessDb (const juce::AudioBuffer<float>& b, double fs, int start = 0, int length = -1)
{
    const int n = length < 0 ? b.getNumSamples() - start : juce::jmin (length, b.getNumSamples() - start);
    if (n <= 0) return -120.0f;
    double sum = 0;
    for (int c = 0; c < b.getNumChannels(); ++c)
    {
        Biquad shelf = Biquad::highShelf (fs, 1681.97, 4.0, 0.7072), hp = Biquad::highPass (fs, 38.135, 0.5003);
        const float* x = b.getReadPointer (c, start);
        double s = 0; for (int i = 0; i < n; ++i) { const float y = hp.tick (shelf.tick (x[i])); s += (double) y * y; }
        sum += s / n;
    }
    return (float) (-0.691 + 10.0 * std::log10 (juce::jmax (1.0e-12, sum)));
}

// ---- spectrum: the average power in each of the 24 bands over a stretch of sound (Hann window, half-overlapped frames)
struct Spectrum
{
    std::array<double, kBands> band {};   // mean power per band
    double total = 0, centroid = 0, flatness = 0, flux = 0;   // flux: how much the band picture changes frame to frame (0..1)
    std::vector<float> average;           // mean magnitude spectrum (bins 0 .. N/2)
    double binHz = 0;
};
inline Spectrum spectrumOf (const float* x, int n, double fs, int order = 12)
{
    Spectrum s;
    const int N = 1 << order, hop = N / 2;
    if (n < N) return s;
    juce::dsp::FFT fft (order);
    juce::dsp::WindowingFunction<float> win ((size_t) N, juce::dsp::WindowingFunction<float>::hann, true);
    std::vector<float> buf ((size_t) N * 2), acc ((size_t) N / 2 + 1, 0.0f);
    std::array<double, kBands> prev {}; bool havePrev = false; double fluxSum = 0; int frames = 0;
    s.binHz = fs / N;
    std::vector<int> bandOf ((size_t) N / 2 + 1, -1);
    for (int k = 1; k <= N / 2; ++k)
    {
        const double f = k * s.binHz;
        for (int b = 0; b < kBands; ++b) if (f >= bandEdge (b) && f < bandEdge (b + 1)) { bandOf[(size_t) k] = b; break; }
    }
    for (int pos = 0; pos + N <= n; pos += hop)
    {
        std::copy (x + pos, x + pos + N, buf.begin()); std::fill (buf.begin() + N, buf.end(), 0.0f);
        win.multiplyWithWindowingTable (buf.data(), (size_t) N);
        fft.performFrequencyOnlyForwardTransform (buf.data());
        std::array<double, kBands> now {}; double tot = 0;
        for (int k = 1; k <= N / 2; ++k)
        {
            const double p = (double) buf[(size_t) k] * buf[(size_t) k];
            acc[(size_t) k] += buf[(size_t) k];
            if (bandOf[(size_t) k] >= 0) { now[(size_t) bandOf[(size_t) k]] += p; tot += p; }
        }
        for (int b = 0; b < kBands; ++b) s.band[(size_t) b] += now[(size_t) b];
        if (havePrev && tot > 1.0e-12)
        {
            double d = 0, ptot = 0; for (int b = 0; b < kBands; ++b) ptot += prev[(size_t) b];
            if (ptot > 1.0e-12) { for (int b = 0; b < kBands; ++b) d += std::abs (now[(size_t) b] / tot - prev[(size_t) b] / ptot); fluxSum += 0.5 * d; }
        }
        prev = now; havePrev = true; ++frames;
    }
    if (frames == 0) return s;
    for (auto& b : s.band) { b /= frames; s.total += b; }
    s.flux = frames > 1 ? fluxSum / (frames - 1) : 0.0;
    s.average.resize ((size_t) N / 2 + 1);
    double num = 0, den = 0, logSum = 0, linSum = 0; int cnt = 0;
    for (int k = 1; k <= N / 2; ++k)
    {
        const double m = acc[(size_t) k] / frames; s.average[(size_t) k] = (float) m;
        const double f = k * s.binHz, p = m * m;
        if (f >= 40.0 && f <= 16000.0) { num += f * p; den += p; logSum += std::log (p + 1.0e-20); linSum += p; ++cnt; }
    }
    s.centroid = den > 0 ? num / den : 0;
    s.flatness = (cnt > 0 && linSum > 0) ? std::exp (logSum / cnt) / (linSum / cnt) : 0;
    return s;
}
inline double shareBetween (const Spectrum& s, float lo, float hi)
{
    if (s.total <= 0) return 0; double e = 0;
    for (int b = 0; b < kBands; ++b) { const float c = bandCentre (b); if (c >= lo && c < hi) e += s.band[(size_t) b]; }
    return e / s.total;
}

// ---- pitch: the YIN method on one stretch (de Cheveigne & Kawahara 2002). Returns Hz, 0 when it is not clearly pitched;
// clarity 0..1 says how sure it is.
inline float pitchOf (const float* x, int n, double fs, float& clarity, float minHz = 30.0f, float maxHz = 2200.0f)
{
    clarity = 0.0f;
    const int W = juce::jmin (n / 2, (int) (fs * 0.09));
    const int tauMax = juce::jmin (W - 1, (int) (fs / minHz)), tauMin = juce::jmax (2, (int) (fs / maxHz));
    if (W < 256 || tauMax <= tauMin) return 0.0f;
    std::vector<float> d ((size_t) tauMax + 1, 0.0f);
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        double sum = 0; for (int i = 0; i < W; ++i) { const double v = (double) x[i] - x[i + tau]; sum += v * v; }
        d[(size_t) tau] = (float) sum;
    }
    double running = 0; d[0] = 1.0f;
    for (int tau = 1; tau <= tauMax; ++tau) { running += d[(size_t) tau]; d[(size_t) tau] = running > 0 ? (float) (d[(size_t) tau] * tau / running) : 1.0f; }
    int best = -1;
    for (int tau = tauMin; tau < tauMax; ++tau)
        if (d[(size_t) tau] < 0.15f) { while (tau + 1 < tauMax && d[(size_t) tau + 1] < d[(size_t) tau]) ++tau; best = tau; break; }
    if (best < 0)
    {
        float mn = 1.0e9f; for (int tau = tauMin; tau < tauMax; ++tau) if (d[(size_t) tau] < mn) { mn = d[(size_t) tau]; best = tau; }
        if (best < 0 || mn > 0.45f) return 0.0f;
    }
    clarity = clamp01 (1.0f - d[(size_t) best]);
    float t = (float) best;
    if (best > 1 && best < tauMax - 1)
    {
        const float a = d[(size_t) best - 1], b = d[(size_t) best], c = d[(size_t) best + 1], den = a - 2 * b + c;
        if (std::abs (den) > 1.0e-9f) t += 0.5f * (a - c) / den;
    }
    return (float) (fs / t);
}
inline float centsBetween (float hz, float refHz) { return (hz > 0 && refHz > 0) ? 1200.0f * std::log2 (hz / refHz) : 0.0f; }
inline float midiToHz (float note) { return 440.0f * std::pow (2.0f, (note - 69.0f) / 12.0f); }

// ---- tuning: how far the note's lowest partial sits from where its key says it should be, in cents (within a
// semitone). It looks at the partial itself in a fine spectrum - the fundamental, or the octave above it when the
// fundamental is weak - so a bell-like sound with inharmonic overtones is not mistaken for a mistuned one.
// strength 0..1 says how clearly a partial stands there (0: nothing to measure).
inline float tuningOf (const float* x, int n, double fs, float expectedHz, float& strength)
{
    strength = 0.0f;
    int order = 16; while ((1 << order) > n && order > 12) --order;
    const int N = 1 << order;
    if (n < N || expectedHz <= 0.0f) return 0.0f;
    const int M = N << 2;                                   // four times zero-padded: a smooth curve to read the peak from
    juce::dsp::FFT fft (order + 2);
    std::vector<float> buf ((size_t) M * 2, 0.0f);
    for (int i = 0; i < N; ++i) buf[(size_t) i] = x[i] * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (N - 1)));
    fft.performFrequencyOnlyForwardTransform (buf.data());
    const double binHz = fs / M, edge = std::pow (2.0, 90.0 / 1200.0);
    struct Hit { float cents = 0, mag = 0, stands = 0; };
    auto look = [&] (double target) {
        Hit h;
        const int lo = juce::jmax (2, (int) (target / edge / binHz)), hi = juce::jmin (M / 2 - 2, (int) (target * edge / binHz) + 1);
        if (hi - lo < 6) return h;
        int best = lo; for (int k = lo; k <= hi; ++k) if (buf[(size_t) k] > buf[(size_t) best]) best = k;
        h.mag = buf[(size_t) best];
        if (best <= lo + 1 || best >= hi - 1) return h;     // highest at the edge: the partial is not inside this window
        // the centre, by power, of everything that stands near the height of the highest point: for one clean partial that
        // is the partial; for voices set slightly apart (a unison, a chorus) or a vibrato it is their middle, which is the
        // pitch that is heard - the highest single voice would read as mistuned when nothing is
        double sp = 0.0, sc = 0.0;
        for (int k = lo; k <= hi; ++k)
            if (buf[(size_t) k] >= 0.25f * h.mag) { const double p = (double) buf[(size_t) k] * buf[(size_t) k]; sp += p; sc += p * 1200.0 * std::log2 ((double) k * binHz / target); }
        h.cents = sp > 0.0 ? (float) (sc / sp) : 0.0f;
        h.stands = h.mag / (juce::jmax (buf[(size_t) lo], buf[(size_t) hi]) + 1.0e-20f);     // against the two ends of the window
        return h;
    };
    const Hit h1 = look ((double) expectedHz), h2 = look (2.0 * (double) expectedHz);
    const Hit* use = nullptr;
    if (h1.stands > 4.0f && h1.mag > 0.1f * h2.mag) use = &h1; else if (h2.stands > 4.0f) use = &h2; else if (h1.stands > 4.0f) use = &h1;
    if (use == nullptr) return 0.0f;
    strength = clamp01 ((use->stands - 4.0f) / 20.0f);
    return use->cents;
}

// ---- the outline of a sound over time: RMS in windows of `ms`, as plain gain
inline std::vector<float> envelopeOf (const std::vector<float>& mono, double fs, double ms = 10.0)
{
    const int w = juce::jmax (1, (int) (fs * ms / 1000.0));
    std::vector<float> e;
    for (size_t i = 0; i + (size_t) w <= mono.size(); i += (size_t) w) e.push_back (rms (mono.data() + i, w));
    return e;
}
// how badly two sounds cancel when folded to mono: dB lost by (L+R)/2 against the stereo power
inline float monoLossDb (const juce::AudioBuffer<float>& b, int start, int length)
{
    if (b.getNumChannels() < 2) return 0.0f;
    const int n = juce::jmin (length, b.getNumSamples() - start); if (n <= 0) return 0.0f;
    const float* l = b.getReadPointer (0, start); const float* r = b.getReadPointer (1, start);
    double st = 0, mo = 0;
    for (int i = 0; i < n; ++i) { st += 0.5 * ((double) l[i] * l[i] + (double) r[i] * r[i]); const double m = 0.5 * (l[i] + r[i]); mo += m * m; }
    if (st < 1.0e-12) return 0.0f;
    return (float) juce::jlimit (0.0, 40.0, 10.0 * std::log10 (st / juce::jmax (1.0e-12, mo)));
}
inline float widthOf (const juce::AudioBuffer<float>& b, int start, int length)
{
    if (b.getNumChannels() < 2) return 0.0f;
    const int n = juce::jmin (length, b.getNumSamples() - start); if (n <= 0) return 0.0f;
    const float* l = b.getReadPointer (0, start); const float* r = b.getReadPointer (1, start);
    double m = 0, s = 0; for (int i = 0; i < n; ++i) { const double a = 0.5 * (l[i] + r[i]), d = 0.5 * (l[i] - r[i]); m += a * a; s += d * d; }
    return (m + s) > 1.0e-12 ? (float) (s / (m + s)) : 0.0f;
}
// one narrow band standing out of its neighbours between 1.5 and 10 kHz (sixth-octave bands, each by its strongest partial)
inline float ringOf (const Spectrum& sp)
{
    if (sp.average.empty() || sp.binHz <= 0) return 0.0f;
    std::vector<float> e;
    for (double f = 1500.0; f < 10000.0; f *= std::pow (2.0, 1.0 / 6.0))
    {
        const int a = (int) (f / sp.binHz), b = juce::jmin ((int) sp.average.size() - 1, (int) (f * std::pow (2.0, 1.0 / 6.0) / sp.binHz));
        float mx = 1.0e-9f; for (int k = a; k <= b; ++k) mx = juce::jmax (mx, sp.average[(size_t) k]);
        e.push_back (20.0f * std::log10 (mx));
    }
    float worst = 0.0f;
    float top = -200.0f; for (float v : e) top = juce::jmax (top, v);
    for (int i = 0; i < (int) e.size(); ++i)
    {
        if (e[(size_t) i] < top - 24.0f) continue;                   // only what is loud enough to be heard as a ring
        double s = 0; int n = 0;
        for (int k = i - 3; k <= i + 3; ++k) if (k != i && k >= 0 && k < (int) e.size()) { s += e[(size_t) k]; ++n; }
        if (n > 0) worst = juce::jmax (worst, e[(size_t) i] - (float) (s / n));
    }
    return worst;
}

} // namespace atlas::measure
