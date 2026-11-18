#pragma once
// Spectral core: a real-time-safe stereo STFT engine (Hann analysis + synthesis windows, overlap-add) plus small
// spectral helpers. Written for Ghost Box's GHOST SPECTRA and meant to be shared with Ghosty Spectrum later, so it only
// depends on juce_dsp (juce::dsp::FFT). All buffers are allocated in prepare(); process() never allocates or locks.
//
// Latency: exactly fftSize samples (an input sample at time t comes out at t + fftSize), independent of the overlap,
// so the overlap can be raised for an offline high-quality bounce without changing the reported latency.
#include <juce_dsp/juce_dsp.h>
#include <complex>
#include <vector>
#include <cmath>

namespace spectral {

using Cpx = std::complex<float>;

class Stft
{
public:
    // order 11 = 2048-point frames; maxOverlap = largest overlap setOverlap() will be asked for (4 = 75 %, 8 = 87.5 %)
    void prepare (int order, int maxOverlap = 8)
    {
        fft = std::make_unique<juce::dsp::FFT> (order);
        N = 1 << order; bins = N / 2 + 1; maxOv = std::max (2, maxOverlap);
        win.assign ((size_t) N, 0.0f);
        for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) N);   // periodic Hann
        for (int c = 0; c < 2; ++c)
        {
            in[c].assign ((size_t) N, 0.0f); acc[c].assign ((size_t) N, 0.0f);
            frame[c].assign ((size_t) N * 2, 0.0f);
        }
        // measure the round-trip gain of this FFT implementation once (some scale the inverse by 1/N, some don't)
        std::vector<float> t ((size_t) N * 2, 0.0f); t[3] = 1.0f;
        fft->performRealOnlyForwardTransform (t.data(), true);
        fft->performRealOnlyInverseTransform (t.data());
        fftGain = std::abs (t[3]) > 1.0e-9f ? t[3] : 1.0f;
        setOverlap (4);
        reset();
    }
    void reset()
    {
        for (int c = 0; c < 2; ++c) { std::fill (in[c].begin(), in[c].end(), 0.0f); std::fill (acc[c].begin(), acc[c].end(), 0.0f); }
        w = 0; count = 0; ov = pendingOv;
    }
    // takes effect at the next frame boundary; latency is unchanged
    void setOverlap (int o) { pendingOv = juce::jlimit (2, maxOv, o); if (count == 0) applyOverlap(); }
    int overlap() const noexcept { return ov; }
    int size() const noexcept { return N; }
    int numBins() const noexcept { return bins; }
    int hop() const noexcept { return N / ov; }
    int latency() const noexcept { return N; }

    // Fill the input history (oldest first) so a freshly started engine has sound after one hop instead of N samples.
    void prime (const float* histL, const float* histR, int len)
    {
        const int n = std::min (len, N);
        for (int i = 0; i < n; ++i) { in[0][(size_t) ((w + N - n + i) % N)] = histL[i]; in[1][(size_t) ((w + N - n + i) % N)] = histR[i]; }
    }

    // fn (Cpx* left, Cpx* right, int numBins) is called once per hop with the spectra of the newest frame; it edits them
    // in place. Bin k is k * sampleRate / size Hz.
    template <class Fn>
    void process (float* L, float* R, int n, Fn&& fn)
    {
        for (int i = 0; i < n; ++i)
        {
            in[0][(size_t) w] = L[i]; in[1][(size_t) w] = R[i];
            L[i] = acc[0][(size_t) w]; R[i] = acc[1][(size_t) w];
            acc[0][(size_t) w] = 0.0f; acc[1][(size_t) w] = 0.0f;
            w = (w + 1) % N;
            if (++count >= N / ov) { count = 0; doFrame (fn); applyOverlap(); }
        }
    }

private:
    template <class Fn>
    void doFrame (Fn& fn)
    {
        for (int c = 0; c < 2; ++c)
        {
            float* f = frame[c].data();
            for (int i = 0; i < N; ++i) f[i] = in[c][(size_t) ((w + i) % N)] * win[(size_t) i];
            std::fill (f + N, f + 2 * N, 0.0f);
            fft->performRealOnlyForwardTransform (f, true);
        }
        fn (reinterpret_cast<Cpx*> (frame[0].data()), reinterpret_cast<Cpx*> (frame[1].data()), bins);
        const float norm = 1.0f / (fftGain * 0.375f * (float) ov);   // Hann^2 overlap-add sums to 0.375 * overlap
        for (int c = 0; c < 2; ++c)
        {
            float* f = frame[c].data();
            for (int k = bins; k < N; ++k) { f[2 * k] = 0.0f; f[2 * k + 1] = 0.0f; }   // only the non-negative half is meaningful
            fft->performRealOnlyInverseTransform (f);
            for (int i = 0; i < N; ++i) acc[c][(size_t) ((w + i) % N)] += f[i] * win[(size_t) i] * norm;
        }
    }
    void applyOverlap() { ov = pendingOv; }

    std::unique_ptr<juce::dsp::FFT> fft;
    int N = 2048, bins = 1025, maxOv = 8, ov = 4, pendingOv = 4, w = 0, count = 0;
    float fftGain = 1.0f;
    std::vector<float> win, in[2], acc[2], frame[2];
};

// helpers
inline float wrapPi (float p) noexcept { p = std::fmod (p + juce::MathConstants<float>::pi, juce::MathConstants<float>::twoPi); if (p < 0) p += juce::MathConstants<float>::twoPi; return p - juce::MathConstants<float>::pi; }
// spectral flux of a magnitude frame against the previous one (0 = steady, ~1 = a new onset); updates prev
inline float flux (const float* mag, float* prev, int n) noexcept
{
    double up = 0, tot = 1.0e-12;
    for (int k = 0; k < n; ++k) { const float d = mag[k] - prev[k]; if (d > 0) up += d; tot += mag[k]; prev[k] = mag[k]; }
    return (float) (up / tot);
}

} // namespace spectral
