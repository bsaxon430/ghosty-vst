#include "PhaseLock.h"
#include <signalsmith-linear/fft.h>   // (Signalsmith's FFT, MIT, the one Signalsmith Stretch ships with: fast on every platform)
#include <complex>

namespace gslice::gen::pl
{
namespace
{
constexpr double kPi = 3.14159265358979323846, k2Pi = 2.0 * kPi;
inline double princ (double x) { x = std::fmod (x + kPi, k2Pi); if (x < 0) x += k2Pi; return x - kPi; }
// one channel's windowed frame centred on input position p (linear interpolation for the fraction), into buf (2N floats)
void frameAt (const float* x, int n, double p, const std::vector<float>& w, std::vector<float>& buf)
{
    const int N = (int) w.size(); std::fill (buf.begin(), buf.end(), 0.0f);
    const double start = p - N / 2; const int i0 = (int) std::floor (start); const float fr = (float) (start - i0);
    for (int i = 0; i < N; ++i)
    {
        const int k = i0 + i; const float a = k >= 0 && k < n ? x[k] : 0.0f, b = k + 1 >= 0 && k + 1 < n ? x[k + 1] : 0.0f;
        buf[(size_t) i] = (a + (b - a) * fr) * w[(size_t) i];
    }
}
} // namespace

Buf process (const Buf& in, double sr, int nOut, const std::function<double (int)>& readAt, const Spec& s)
{
    juce::ignoreUnused (sr);
    const int order = juce::jlimit (10, 13, s.fftOrder), N = 1 << order, Hs = N / 4, K = N / 2 + 1, chs = in.getNumChannels(), n = in.getNumSamples();
    Buf out (chs, juce::jmax (1, nOut)); out.clear();
    if (n == 0 || nOut <= 0) return out;
    signalsmith::linear::Pow2RealFFT<float> fft ((size_t) N);
    std::vector<std::complex<float>> spec ((size_t) N / 2);
    // forward: the frame in buf[0..N) -> interleaved re / im for bins 0..N/2 in buf[0..N+2) (the layout the code below reads)
    auto forward = [&] (std::vector<float>& buf)
    {
        fft.fft (buf.data(), spec.data());
        const float dc = spec[0].real(), ny = spec[0].imag();
        for (int k = 1; k < N / 2; ++k) { buf[(size_t) 2 * k] = spec[(size_t) k].real(); buf[(size_t) 2 * k + 1] = spec[(size_t) k].imag(); }
        buf[0] = dc; buf[1] = 0; buf[(size_t) N] = ny; buf[(size_t) N + 1] = 0;
    };
    auto inverse = [&] (std::vector<float>& buf)
    {
        spec[0] = { buf[0], buf[(size_t) N] };
        for (int k = 1; k < N / 2; ++k) spec[(size_t) k] = { buf[(size_t) 2 * k], buf[(size_t) 2 * k + 1] };
        fft.ifft (spec.data(), buf.data());
        const float sc = 1.0f / (float) N; for (int i = 0; i < N; ++i) buf[(size_t) i] *= sc;
    };
    std::vector<float> w ((size_t) N); for (int i = 0; i < N; ++i) w[(size_t) i] = (float) (0.5 - 0.5 * std::cos (k2Pi * i / N));
    // mid (for the shared phase advance)
    std::vector<float> mid ((size_t) n, 0.0f); for (int c = 0; c < chs; ++c) { const float* x = in.getReadPointer (c); for (int i = 0; i < n; ++i) mid[(size_t) i] += x[i] / (float) chs; }
    std::vector<float> bm ((size_t) N * 2), bp ((size_t) N * 2), bc ((size_t) N * 2);
    std::vector<double> mag ((size_t) K), ph ((size_t) K), phPrev ((size_t) K), theta ((size_t) K, 0.0), lastMag ((size_t) K, 0.0);
    std::vector<int> peakOf ((size_t) K, 0); std::vector<int> peaks;
    dsp::Rng rng ((uint64) s.seed * 2654435761u + 3u);
    double fluxAvg = 0; bool first = true;
    const float norm = 1.0f / 1.5f;   // (Hann x Hann at hop N / 4)
    for (int t = -N / 2; t < nOut + N / 2; t += Hs)
    {
        const double p = readAt (juce::jlimit (0, nOut - 1, t));
        frameAt (mid.data(), n, p, w, bm); forward (bm);
        frameAt (mid.data(), n, p - Hs, w, bp); forward (bp);
        double mx = 0, flux = 0, tot = 0;
        for (int k = 0; k < K; ++k)
        {
            const double re = bm[(size_t) 2 * k], im = bm[(size_t) 2 * k + 1];
            mag[(size_t) k] = std::sqrt (re * re + im * im); ph[(size_t) k] = std::atan2 (im, re);
            mx = juce::jmax (mx, mag[(size_t) k]);
            flux += juce::jmax (0.0, mag[(size_t) k] - lastMag[(size_t) k]); tot += mag[(size_t) k];
        }
        // an onset: the flux jumps well above its running average -> every phase back to the analysis phase
        const double fl = tot > 1e-9 ? flux / tot : 0.0;
        const bool onset = first || (s.transients && fl > 0.35 && fl > 2.5 * fluxAvg);
        fluxAvg = 0.9 * fluxAvg + 0.1 * fl; lastMag = mag; first = false;
        if (onset) theta = ph;
        else
        {
            // the peaks, and each bin's region of influence (the nearest peak; split halfway between peaks)
            peaks.clear(); const double floorMag = mx * 3e-4;
            for (int k = 2; k + 2 < K; ++k) if (mag[(size_t) k] > floorMag && mag[(size_t) k] >= mag[(size_t) k - 1] && mag[(size_t) k] > mag[(size_t) k + 1] && mag[(size_t) k] >= mag[(size_t) k - 2] && mag[(size_t) k] > mag[(size_t) k + 2]) peaks.push_back (k);
            std::vector<double> newTheta ((size_t) K);
            if (peaks.empty()) for (int k = 0; k < K; ++k) newTheta[(size_t) k] = theta[(size_t) k] + Hs * (k2Pi * k / N);
            else
            {
                size_t pi = 0;
                for (int k = 0; k < K; ++k)
                {
                    while (pi + 1 < peaks.size() && std::abs (peaks[pi + 1] - k) <= std::abs (peaks[pi] - k)) ++pi;
                    peakOf[(size_t) k] = peaks[pi];
                }
                for (int pk : peaks)
                {   // the peak's instantaneous frequency (rad / sample) and its new phase
                    phPrev[(size_t) pk] = std::atan2 ((double) bp[(size_t) 2 * pk + 1], (double) bp[(size_t) 2 * pk]);   // (only the peaks need it)
                    const double om = k2Pi * pk / N + princ (ph[(size_t) pk] - phPrev[(size_t) pk] - Hs * k2Pi * pk / N) / Hs;
                    newTheta[(size_t) pk] = theta[(size_t) pk] + Hs * om;
                }
                for (int k = 0; k < K; ++k) { const int pk = peakOf[(size_t) k]; if (k != pk) newTheta[(size_t) k] = newTheta[(size_t) pk] + (ph[(size_t) k] - ph[(size_t) pk]); }
            }
            if (s.random > 1e-6) for (int k = 0; k < K; ++k) newTheta[(size_t) k] += s.random * kPi * rng.sym();
            for (int k = 0; k < K; ++k) theta[(size_t) k] = princ (newTheta[(size_t) k]);
        }
        // every channel: its own magnitudes and phases, turned by the shared (mid) rotation
        for (int c = 0; c < chs; ++c)
        {
            if (chs == 1) std::copy (bm.begin(), bm.end(), bc.begin());   // (mono: the mid frame is the channel)
            else { frameAt (in.getReadPointer (c), n, p, w, bc); forward (bc); }
            for (int k = 0; k < K; ++k)
            {
                const double d = theta[(size_t) k] - ph[(size_t) k], cr = std::cos (d), si = std::sin (d);
                const double re = bc[(size_t) 2 * k], im = bc[(size_t) 2 * k + 1];
                bc[(size_t) 2 * k] = (float) (re * cr - im * si); bc[(size_t) 2 * k + 1] = (float) (re * si + im * cr);
            }
            inverse (bc);
            float* y = out.getWritePointer (c);
            for (int i = 0; i < N; ++i) { const int o = t - N / 2 + i; if (o >= 0 && o < nOut) y[o] += bc[(size_t) i] * w[(size_t) i] * norm; }
        }
    }
    return out;
}

Buf stretch (const Buf& in, double sr, double ratio, const Spec& s)
{
    const int nOut = (int) std::lround (in.getNumSamples() * ratio);
    return process (in, sr, nOut, [ratio] (int t) { return t / ratio; }, s);
}

Buf freeze (const Buf& in, double sr, double a, double b, double seconds, double cycleS, const Spec& s)
{
    const int nOut = (int) std::lround (seconds * sr);
    const double span = juce::jmax (1.0, b - a), cyc = juce::jmax (0.5, cycleS) * sr;
    // back and forth through the part (a raised cosine: it slows at the ends, never jumps)
    return process (in, sr, nOut, [=] (int t) { return a + span * (0.5 - 0.5 * std::cos (k2Pi * t / cyc)); }, s);
}
} // namespace gslice::gen::pl
