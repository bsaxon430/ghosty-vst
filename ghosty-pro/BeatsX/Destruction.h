#pragma once
// Per-hit DESTRUCTION phase 2, rendered offline on the message thread (never on the audio thread) and cached per sound:
//   SPECTRAL FREEZE: an STFT snapshot of the hit's loudest early frame, resynthesised with seeded random phases into a
//                    held, stationary tail (2.5 s; the step's freeze length gates it, with a 60 ms fade).
//   FREQUENCY-DEPENDENT REVERSE: the hit split into low / mid / high (250 Hz, 3 kHz, bands sum back exactly), the selected
//                    bands played backwards.
#include "DrumKit.h"
#include <juce_dsp/juce_dsp.h>

namespace gb::destruct {

constexpr int kOrder = 11, kN = 1 << kOrder, kHop = kN / 4;   // 2048-point frames, 75 % overlap
constexpr double kHoldSec = 2.5;

inline SoundPtr freeze(const Sound& src, uint32_t seed) {
    const auto& x = src.data; const int n = (int) x.size(); if (n < 64) return nullptr;
    juce::dsp::FFT fft(kOrder);
    std::vector<float> win((size_t) kN); for (int i = 0; i < kN; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * i / kN);
    // the loudest frame in the first 80 ms (or the whole sound if shorter) is the one we freeze
    const int search = juce::jmin(n, (int) (src.rate * 0.08)); int best = 0; double be = -1;
    for (int a = 0; a < juce::jmax(1, search); a += 128) { double e = 0; for (int k = 0; k < kN && a + k < n; ++k) e += (double) x[(size_t) (a + k)] * x[(size_t) (a + k)] * win[(size_t) k]; if (e > be) { be = e; best = a; } }
    std::vector<float> buf((size_t) kN * 2, 0.0f);
    for (int k = 0; k < kN; ++k) buf[(size_t) k] = (best + k < n ? x[(size_t) (best + k)] : 0.0f) * win[(size_t) k];
    fft.performRealOnlyForwardTransform(buf.data(), true);
    std::vector<float> mag((size_t) kN / 2 + 1); for (int b = 0; b <= kN / 2; ++b) mag[(size_t) b] = std::hypot(buf[(size_t) (2 * b)], buf[(size_t) (2 * b + 1)]);
    // head: the original up to the middle of the frozen frame, then the held texture (overlap-add, random phases)
    const int freezeAt = juce::jmin(n, best + kN / 2), total = freezeAt + (int) (src.rate * kHoldSec);
    auto out = std::make_shared<Sound>(); out->name = src.name + " (freeze)"; out->category = src.category; out->rate = src.rate; out->data.assign((size_t) total, 0.0f);
    std::vector<float> tex((size_t) (total - freezeAt + kN * 2), 0.0f);
    uint32_t r = seed * 2654435761u + 1;
    auto rnd = [&] { r ^= r << 13; r ^= r >> 17; r ^= r << 5; return (float) (r & 0xffffff) / 16777216.0f; };
    for (int f = 0; f * kHop < (int) tex.size() - kN; ++f) {
        std::vector<float> spec((size_t) kN * 2, 0.0f);
        for (int b = 1; b < kN / 2; ++b) { const float ph = rnd() * juce::MathConstants<float>::twoPi; spec[(size_t) (2 * b)] = mag[(size_t) b] * std::cos(ph); spec[(size_t) (2 * b + 1)] = mag[(size_t) b] * std::sin(ph); }
        fft.performRealOnlyInverseTransform(spec.data());
        for (int k = 0; k < kN; ++k) tex[(size_t) (f * kHop + k)] += spec[(size_t) k] * win[(size_t) k] / 1.5f;   // hann^2 at 75 % overlap sums to 1.5
    }
    // level match the texture to the frozen frame, crossfade 20 ms from the original into it, fade the very end
    double ef = 0, et = 0; for (int k = 0; k < kN; ++k) ef += best + k < n ? (double) x[(size_t) (best + k)] * x[(size_t) (best + k)] * win[(size_t) k] : 0.0;
    for (int k = kN; k < kN * 3 && k < (int) tex.size(); ++k) et += (double) tex[(size_t) k] * tex[(size_t) k];
    const float g = (float) std::sqrt((ef / (0.5 * kN) + 1e-12) / (et / (2.0 * kN) + 1e-12));   // mean power of the frozen frame = mean power of the tail
    const int xf = (int) (src.rate * 0.02);
    for (int i = 0; i < total; ++i) {
        const float orig = i < n ? x[(size_t) i] : 0.0f; const int j = i - freezeAt + xf; const float tx = j >= 0 && j + kN < (int) tex.size() ? tex[(size_t) (j + kN)] * g : 0.0f;
        const float w = juce::jlimit(0.0f, 1.0f, (float) (i - (freezeAt - xf)) / (float) xf);
        out->data[(size_t) i] = orig * (1.0f - w) + tx * w;
    }
    const int fade = (int) (src.rate * 0.1); for (int i = 0; i < fade && i < total; ++i) out->data[(size_t) (total - 1 - i)] *= (float) i / (float) fade;
    float pk = 1e-6f; for (auto v : out->data) pk = juce::jmax(pk, std::abs(v)); float ps = 1e-6f; for (auto v : x) ps = juce::jmax(ps, std::abs(v));
    for (auto& v : out->data) v *= ps / pk;
    return out;
}

// the three bands of a sound (they add back up to the original exactly)
inline std::array<std::vector<float>, 3> bands(const Sound& src) {
    std::array<std::vector<float>, 3> b; const int n = (int) src.data.size();
    for (auto& v : b) v.assign((size_t) n, 0.0f);
    auto svf = [&](double f) {   // zero-phase (forward + backward) low-pass, so the bands line up and leak little into each other
        const double g = std::tan(juce::MathConstants<double>::pi * f / src.rate), k = 1.41421356, a1 = 1 / (1 + g * (g + k)), a2 = g * a1, a3 = g * a2;
        std::vector<float> lp(src.data);
        for (int pass = 0; pass < 2; ++pass) { double s1 = 0, s2 = 0;
            for (int j = 0; j < n; ++j) { const int i = pass == 0 ? j : n - 1 - j; const double v3 = lp[(size_t) i] - s2, v1 = a1 * s1 + a2 * v3, v2 = s2 + a2 * s1 + a3 * v3; s1 = 2 * v1 - s1; s2 = 2 * v2 - s2; lp[(size_t) i] = (float) v2; } }
        return lp; };
    const auto lo = svf(250.0), lm = svf(3000.0);
    for (int i = 0; i < n; ++i) { b[0][(size_t) i] = lo[(size_t) i]; b[1][(size_t) i] = lm[(size_t) i] - lo[(size_t) i]; b[2][(size_t) i] = src.data[(size_t) i] - lm[(size_t) i]; }
    return b;
}
inline SoundPtr bandReverseFrom(const Sound& src, const std::array<std::vector<float>, 3>& bandsIn, int mask) {
    const int n = (int) src.data.size(); if (n < 64 || (mask & 7) == 0) return nullptr;
    auto b = bandsIn;
    for (int k = 0; k < 3; ++k) if (mask >> k & 1) std::reverse(b[(size_t) k].begin(), b[(size_t) k].end());
    auto out = std::make_shared<Sound>(); out->name = src.name + " (band reverse " + juce::String(mask) + ")"; out->category = src.category; out->rate = src.rate; out->data.resize((size_t) n);
    for (int i = 0; i < n; ++i) out->data[(size_t) i] = b[0][(size_t) i] + b[1][(size_t) i] + b[2][(size_t) i];
    const int f = juce::jmin(n / 4, (int) (src.rate * 0.003)); for (int i = 0; i < f; ++i) { out->data[(size_t) i] *= (float) i / (float) f; out->data[(size_t) (n - 1 - i)] *= (float) i / (float) f; }
    return out;
}
inline SoundPtr bandReverse(const Sound& src, int mask) { if ((int) src.data.size() < 64 || (mask & 7) == 0) return nullptr; return bandReverseFrom(src, bands(src), mask); }
// all 7 masks from one band split (the message thread never does this: see BeatsXProcessor's variant worker)
inline std::array<SoundPtr, 7> bandReverseAll(const Sound& src) {
    std::array<SoundPtr, 7> out {}; if ((int) src.data.size() < 64) return out;
    const auto b = bands(src); for (int m = 1; m <= 7; ++m) out[(size_t) (m - 1)] = bandReverseFrom(src, b, m); return out;
}

} // namespace gb::destruct
