#pragma once
// Ghosty Spectrum: DETECT KEY.
// A chromagram (FFT peaks folded onto the 12 pitch classes, bass weighted a little) correlated with the 24 rotated
// Krumhansl-Kessler key profiles (Krumhansl-Schmuckler key finding). Runs on the message thread from samples the audio
// thread copied into a lock-free ring, so nothing here ever touches the audio thread.
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>

namespace sp {

class KeyDetector {
public:
    static constexpr int kOrder = 13, kN = 1 << kOrder, kHop = kN / 2;
    KeyDetector() : fft(kOrder), acc((size_t) kN, 0.0f), work((size_t) kN * 2, 0.0f), win((size_t) kN) {
        for (int i = 0; i < kN; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * i / (kN - 1));
        reset();
    }
    void reset() { chroma.fill(0); fill = 0; frames = 0; energy = 0; samples = 0; }
    void add(const float* x, int n, double sr) {
        for (int i = 0; i < n; ++i) {
            acc[(size_t) fill++] = x[i]; energy += (double) x[i] * x[i]; ++samples;
            if (fill == kN) { analyse(sr); std::copy(acc.begin() + kHop, acc.end(), acc.begin()); fill = kN - kHop; }
        }
    }
    struct Result { int root = -1; bool minor = false; float score = 0, margin = 0; };
    Result result() const {
        Result r; double sum = 0; for (double c : chroma) sum += c;
        if (frames < 2 || sum <= 0) return r;
        static const double maj[] { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
        static const double mnr[] { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
        double best = -2, second = -2;
        for (int mode = 0; mode < 2; ++mode) for (int k = 0; k < 12; ++k) {
            const double* prof = mode ? mnr : maj; double mx = 0, my = 0;
            for (int i = 0; i < 12; ++i) { mx += chroma[(size_t) i]; my += prof[(i - k + 12) % 12]; }
            mx /= 12; my /= 12; double sxy = 0, sxx = 0, syy = 0;
            for (int i = 0; i < 12; ++i) { const double a = chroma[(size_t) i] - mx, b = prof[(i - k + 12) % 12] - my; sxy += a * b; sxx += a * a; syy += b * b; }
            const double c = sxy / std::sqrt(juce::jmax(1e-12, sxx * syy));
            if (c > best) { second = best; best = c; r.root = k; r.minor = mode == 1; } else if (c > second) second = c;
        }
        r.score = (float) best; r.margin = (float) (best - second);
        return r;
    }
    double rms() const { return samples > 0 ? std::sqrt(energy / samples) : 0.0; }
    int frames = 0; std::array<double, 12> chroma {};
private:
    void analyse(double sr) {
        double e = 0; for (int i = 0; i < kN; ++i) e += (double) acc[(size_t) i] * acc[(size_t) i];
        if (std::sqrt(e / kN) < 1e-4) return;   // silence
        for (int i = 0; i < kN; ++i) work[(size_t) i] = acc[(size_t) i] * win[(size_t) i];
        std::fill(work.begin() + kN, work.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform(work.data());
        const int lo = juce::jmax(2, (int) (55.0 * kN / sr)), hi = juce::jmin(kN / 2 - 2, (int) (5000.0 * kN / sr));
        float mx = 0; for (int k = lo; k <= hi; ++k) mx = juce::jmax(mx, work[(size_t) k]);
        if (mx <= 0) return;
        std::array<double, 12> c {};
        for (int k = lo; k <= hi; ++k) {
            const float m = work[(size_t) k];
            if (m < mx * 0.02f || m < work[(size_t) k - 1] || m < work[(size_t) k + 1]) continue;   // spectral peaks only
            const float a = work[(size_t) k - 1], b = work[(size_t) k + 1], den = a - 2 * m + b;
            const float off = std::abs(den) > 1e-12f ? 0.5f * (a - b) / den : 0.0f;
            const double hz = (k + off) * sr / kN; if (hz < 50) continue;
            const double midi = 69.0 + 12.0 * std::log2(hz / 440.0);
            const int pc = (((int) std::lround(midi)) % 12 + 12) % 12;
            const double w = std::sqrt((double) m / mx) * (1.0 + 1.5 / (1.0 + hz / 220.0));   // compressed, bass a bit heavier
            c[(size_t) pc] += w;
        }
        double s = 0; for (double v : c) s += v; if (s <= 0) return;
        for (int i = 0; i < 12; ++i) chroma[(size_t) i] += c[(size_t) i] / s;
        ++frames;
    }
    juce::dsp::FFT fft; std::vector<float> acc, work, win; int fill = 0; double energy = 0; juce::int64 samples = 0;
};

} // namespace sp
