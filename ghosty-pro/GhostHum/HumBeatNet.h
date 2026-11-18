#pragma once
// GHOST HUM - BEATBOX, build 4: the new ears. Real mouths, not synthesised ones.
//
// Build 3 classified 15 hand-made numbers per hit with a softmax fitted on synthesised beatboxers only. On real amateur beatboxing (the
// Amateur Vocal Percussion dataset, 28 people, CC BY 4.0, kept outside the repo) it got the drum right 41 % of the time, found 83 % of
// the hits and added 51 % extra ones (tests/bench/score.py). These ears (each person scored by a net that never heard them): 71 % right,
// 89 % found, 20 % extra; 77 % right on the improvisations after TEACH. Three stages:
//
// 1. SPECTROGRAM  mono, high-passed at 40 Hz, a 40-band log-mel spectrogram (50 Hz .. 8 kHz, HTK mel, Hann window of ~23 ms, hop 5 ms;
//                 juce::dsp::FFT). Rate-independent: the bands are in Hz, so 44.1 / 48 / 96 kHz takes all look the same to the net.
// 2. ONSETS       log-mel spectral flux (each band's rise over the louder of the frames 10 / 15 ms before, so a band's own wobble never
//                 counts: the SuperFlux idea), peaks over the local mean by an adaptive margin, one mouth = one sound per 90 ms.
// 3. WHICH DRUM   a small network (tests/bench/train_beat.py fits it; HumBeatNetWeights.h holds the numbers): the 20 x 40 log-mel patch
//                 from 10 ms before the onset to 90 ms after plus four 40 ms rows of tail to 250 ms (floored after the next onset, so an
//                 open hat's ring is seen and the next hit is not), levelled to its own peak (a soft hit looks like a loud one), plus the
//                 hit's level against the take's loudest, the room before it and the gap to the next onset -> 64 -> 32 -> { KICK, SNARE,
//                 HI-HAT, OPEN HAT, CLAP, NOT A DRUM }. Trained on the AVP hits and our own synthesised mouths (claps, which AVP has none
//                 of, more variety, leaked metronome clicks), with the onsets that were nothing (breaths, mouth noise, clicks) as NOT A DRUM.
// TEACH (HumBeat.h classify2) matches the owner's examples in the net's 32-number embedding (cosine) and asks the build 3 ears too.
// Inference is plain loops on the analysis thread (never the audio thread), ~30 ms of work per second of take; nothing beyond JUCE.
#include "HumModel.h"
#include "HearCore.h"
#include "HumClicks.h"
#include <juce_dsp/juce_dsp.h>

namespace hum::beat2 {

constexpr int kMel = 40, kPre = 2, kPost = 18, kPatch = kPre + kPost, kTail = 4, kTailHop = 8, kScalars = 3, kIn = kMel * kPatch + kMel * kTail + kScalars;   // 963 numbers in
// (the tail: 4 rows of 40 ms each, 90 .. 250 ms after the onset, every band's mean; what comes after the NEXT onset is floored, so an open
// hat's ring is seen and the next hit is not)
constexpr int kH1 = 64, kH2 = 32, kOut = kDrums + 1, kJunk = kDrums;
constexpr double kHop = 0.005;

struct Spec {
    double sr = 48000, hop = kHop; int frames = 0; std::vector<float> mel;   // frames x kMel, log10 of the band energy
    const float* row(int f) const { return mel.data() + (size_t) juce::jlimit(0, juce::jmax(0, frames - 1), f) * kMel; }
    std::vector<float> flux;    // per frame
    float peakDb = -200;        // the loudest frame's loudest band (dB)
};

// HTK mel filterbank over an FFT of size n at rate sr: kMel triangles 50 Hz .. min(8 kHz, 0.45 sr)
inline std::vector<std::vector<std::pair<int, float>>> melBank(int n, double sr) {
    auto mel = [](double f) { return 2595.0 * std::log10(1.0 + f / 700.0); }; auto hz = [](double m) { return 700.0 * (std::pow(10.0, m / 2595.0) - 1.0); };
    const double lo = mel(50.0), hi = mel(juce::jmin(8000.0, sr * 0.45)); std::vector<double> edges; for (int i = 0; i <= kMel + 1; ++i) edges.push_back(hz(lo + (hi - lo) * i / (kMel + 1)));
    std::vector<std::vector<std::pair<int, float>>> bank((size_t) kMel); const int bins = n / 2 + 1;
    for (int m = 0; m < kMel; ++m) { const double a = edges[(size_t) m], c = edges[(size_t) m + 1], z = edges[(size_t) m + 2];
        for (int k = 0; k < bins; ++k) { const double f = k * sr / n; double w = 0; if (f >= a && f <= c) w = (f - a) / juce::jmax(1e-9, c - a); else if (f > c && f <= z) w = (z - f) / juce::jmax(1e-9, z - c);
            if (w > 0) bank[(size_t) m].push_back({ k, (float) (w * 2.0 / juce::jmax(1e-9, z - a)) }); } }   // (area-normalised: every band weighs the same)
    return bank;
}

inline Spec spectrogram(const std::vector<float>& x0, double sr) {
    Spec S; S.sr = sr; S.hop = kHop; const int hop = juce::jmax(1, (int) std::lround(sr * kHop));
    int order = 9; while ((1 << order) < sr * 0.023) ++order; const int n = 1 << order;   // ~23 ms: 1024 at 44.1 / 48 kHz, 2048 at 96 kHz, 512 at 16 / 22 kHz
    auto x = gb::a2m::filtered(x0, { gb::a2m::Biquad::make(true, 40.0, sr), gb::a2m::Biquad::make(true, 40.0, sr) });
    juce::dsp::FFT fft(order); std::vector<float> win((size_t) n); for (int i = 0; i < n; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * (float) i / (float) n);
    const auto bank = melBank(n, sr); std::vector<float> buf((size_t) n * 2);
    S.frames = (int) x.size() / hop + 1; S.mel.assign((size_t) S.frames * kMel, 0.0f);
    for (int f = 0; f < S.frames; ++f) {
        const int c = f * hop; std::fill(buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < n; ++i) { const int k = c - n / 2 + i; if (k >= 0 && k < (int) x.size()) buf[(size_t) i] = x[(size_t) k] * win[(size_t) i]; }
        fft.performFrequencyOnlyForwardTransform(buf.data(), true);   // magnitudes in buf[0 .. n/2]
        float* row = S.mel.data() + (size_t) f * kMel;
        for (int m = 0; m < kMel; ++m) { double e = 0; for (auto& q : bank[(size_t) m]) { const double v = buf[(size_t) q.first]; e += v * v * q.second; } row[m] = (float) std::log10(e / n + 1e-12); S.peakDb = juce::jmax(S.peakDb, 10.0f * row[m]); }
    }
    // flux: every band's rise over the louder of the frames 2 and 3 hops before (10 / 15 ms), half-wave rectified, summed. The bands are
    // floored 60 dB under the take's loudest band first: the room's hiss wobbles by several dB per band and must never count.
    S.flux.assign((size_t) S.frames, 0.0f); const float fl = (S.peakDb - 60.0f) / 10.0f;
    for (int f = 3; f < S.frames; ++f) { const float* a = S.row(f); const float* b = S.row(f - 2); const float* c = S.row(f - 3); float s = 0;
        for (int m = 0; m < kMel; ++m) s += juce::jmax(0.0f, juce::jmax(a[m], fl) - juce::jmax(fl, juce::jmax(b[m], c[m]))); S.flux[(size_t) f] = s; }
    return S;
}

// onsets: local maxima of the flux (+-15 ms) that stand over the mean of the 100 ms before (and 10 ms after) by `margin` (log10 units
// summed over the bands; 0.1 = 1 dB in one band), at least 30 ms apart, in frames
// (`rel`: a peak must also reach this share of the take's typical hit, the 90th percentile of all peaks, so the wobble of a decay or a
// breath 20 dB under the hits never counts while a ghost note still does; `mouth`: one mouth makes one sound at a time, so a smaller
// peak within this many frames after a bigger one is that sound's body, not a new hit)
inline std::vector<int> onsets(const Spec& S, float margin = 4.0f, int minGap = 18, float rel = 0.12f) {
    std::vector<int> out; const int F = S.frames; if (F < 10) return out;
    struct Pk { int f; float v; }; std::vector<Pk> pk;
    for (int f = 3; f + 3 < F; ++f) { const float v = S.flux[(size_t) f]; if (v <= 0) continue;
        bool mx = true; for (int k = f - 3; k <= f + 3 && mx; ++k) if (S.flux[(size_t) k] > v || (S.flux[(size_t) k] == v && k < f)) mx = false; if (!mx) continue;
        double mean = 0; int cnt = 0; for (int k = juce::jmax(0, f - 20); k <= juce::jmin(F - 1, f + 2); ++k) { mean += S.flux[(size_t) k]; ++cnt; } mean /= juce::jmax(1, cnt);
        if (v < mean + margin) continue;
        // the floor: the loudest band near the peak must be within 60 dB of the take's loudest (digital silence and the room never trigger)
        float top = -200; for (int k = f; k <= juce::jmin(F - 1, f + 4); ++k) for (int m = 0; m < kMel; ++m) top = juce::jmax(top, 10.0f * S.row(k)[m]); if (top < S.peakDb - 60.0f) continue;
        pk.push_back({ f, v }); }
    if (pk.empty()) return out;
    std::vector<float> vs; for (auto& p : pk) vs.push_back(p.v); std::sort(vs.begin(), vs.end()); const float typical = vs[(size_t) ((vs.size() - 1) * 9 / 10)];
    int last = -1000;
    // one mouth, one sound at a time: a "t-s" hat or a "p-ff" snare is a click and a noise burst 30..80 ms apart, and real hits are never
    // under 90 ms apart (0.1 % of 9,500 amateur hits), so every peak within minGap frames of a kept onset is that sound's second part.
    // A clearly bigger peak (1.3x) inside the window replaces a WEAK kept one (under 0.4 of the take's typical hit): the first was a mouth
    // click or a breath before the hit. A strong first part keeps its time even when the noise burst after it is louder (a "t-s" hat).
    for (auto& p : pk) { if (p.v < typical * rel) continue;
        if (p.f - last < minGap) { const float kept = S.flux[(size_t) last]; if (kept < 0.4f * typical && p.v > kept * 1.3f) { out.back() = p.f; last = p.f; } continue; }
        out.push_back(p.f); last = p.f; }
    return out;
}

// the patch the net sees for an onset at frame f: kPatch rows of kMel log-mel values levelled to their maximum (so 0 = the patch's peak, negative
// elsewhere, clipped at -6 = 60 dB down), then the hit's level against the take's loudest (dB / 20, <= 0) and the room's level just before it
// (the mean of the 10 frames before the patch against the patch's peak, dB / 20)
using In = std::array<float, kIn>;
// next: the frame of the next onset (or -1 = none), so neither the patch's last rows nor the tail show the next hit
inline In patch(const Spec& S, int f, int next = -1) {
    In p {}; float mx = -1e9f; const int stop = next > f ? next : S.frames + 1000;
    for (int r = 0; r < kPatch; ++r) { const int k = f - kPre + r; const float* row = S.row(k); for (int m = 0; m < kMel; ++m) { const float v = k < stop - 1 ? row[m] : -1e9f; p[(size_t) (r * kMel + m)] = v; mx = juce::jmax(mx, v); } }
    for (int t = 0; t < kTail; ++t) for (int m = 0; m < kMel; ++m) { double e = 0; int c = 0; for (int q = 0; q < kTailHop; ++q) { const int k = f + kPost + t * kTailHop + q; if (k >= stop - 1 || k >= S.frames) break; e += S.row(k)[m]; ++c; }
        p[(size_t) (kMel * kPatch + t * kMel + m)] = c ? (float) (e / c) : -1e9f; }
    for (int i = 0; i < kMel * (kPatch + kTail); ++i) p[(size_t) i] = juce::jmax(-6.0f, p[(size_t) i] - mx);
    double room = 0; int cnt = 0; for (int k = f - kPre - 10; k < f - kPre; ++k) { if (k < 0) continue; const float* row = S.row(k); for (int m = 0; m < kMel; ++m) room += row[m]; cnt += kMel; }
    constexpr int L = kMel * (kPatch + kTail);
    p[(size_t) L] = juce::jmax(-4.0f, (10.0f * mx - S.peakDb) / 20.0f);
    p[(size_t) L + 1] = cnt ? juce::jmax(-6.0f, (float) (room / cnt) - mx) : -6.0f;
    p[(size_t) L + 2] = (float) std::log2(juce::jlimit(0.03, 2.0, (next > f ? next - f : 400) * S.hop) / 0.25);   // the gap to the next onset (log2 of quarter seconds)
    return p;
}

// ---------------------------------------------------------------- the net
struct Net {
    std::vector<float> W1, b1, W2, b2, W3, b3;   // W1: kH1 x kIn (row-major), W2: kH2 x kH1, W3: kOut x kH2
    bool ready() const { return W1.size() == (size_t) kH1 * kIn && W2.size() == (size_t) kH2 * kH1 && W3.size() == (size_t) kOut * kH2; }
    // forward: probabilities over the kOut classes; emb = the kH2 hidden layer (TEACH's space)
    std::array<float, kOut> run(const In& x, std::array<float, kH2>* emb = nullptr) const {
        std::array<float, kH1> h1 {}; for (int j = 0; j < kH1; ++j) { const float* w = W1.data() + (size_t) j * kIn; float s = b1[(size_t) j]; for (int i = 0; i < kIn; ++i) s += w[i] * x[(size_t) i]; h1[(size_t) j] = juce::jmax(0.0f, s); }
        std::array<float, kH2> h2 {}; for (int j = 0; j < kH2; ++j) { const float* w = W2.data() + (size_t) j * kH1; float s = b2[(size_t) j]; for (int i = 0; i < kH1; ++i) s += w[i] * h1[(size_t) i]; h2[(size_t) j] = juce::jmax(0.0f, s); }
        if (emb) *emb = h2;
        std::array<float, kOut> o {}; float mx = -1e30f; for (int j = 0; j < kOut; ++j) { const float* w = W3.data() + (size_t) j * kH2; float s = b3[(size_t) j]; for (int i = 0; i < kH2; ++i) s += w[i] * h2[(size_t) i]; o[(size_t) j] = s; mx = juce::jmax(mx, s); }
        float sum = 0; for (auto& v : o) { v = std::exp(juce::jmax(-60.0f, v - mx)); sum += v; } for (auto& v : o) v /= juce::jmax(1e-20f, sum); return o;
    }
};
inline const Net& defaultNet();   // HumBeatNetWeights.h

} // namespace hum::beat2
