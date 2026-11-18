#pragma once
// GHOST HUM - speaker click masking (build 2). When the metronome (or the count-in's last click) plays through the speakers while the
// mic is open, the click leaks into the take a few milliseconds late, coloured by the speaker and the room. The plugin knows exactly when
// it clicked (HumProcessor keeps every click's time with the take), so the analysis can take the clicks back out before it listens:
//
//   1. SUBTRACT  at every click time the click itself (HumSounds makeClick: a 2000 Hz accent / 1400 Hz beat tone burst, 30 ms) is looked
//                for in the next 40 ms (the speaker -> mic delay; the first 8 ms of the burst are matched, lag by lag). Where it clearly
//                correlates (> 0.6) the matched burst is subtracted with the least-squares gain. The direct sound is gone.
//   2. NOTCH     2000 / 1400 Hz are notched (what the subtraction left: the room's tail, the speaker's colour).
//   3. WINDOW    `Mask::near(t)` says whether an onset sits inside a click's window (-5 ms .. +80 ms): the beatbox classifier drops such
//                an onset unless it is clearly a drum (HumBeat.h), the melody engine drops a note that starts there and is shorter than
//                a real sung note (HumMelody.h). Nothing else is touched: a kick on the beat is louder than a leaked click and keeps its
//                features after the subtraction, so it stays.
// Own code; no allocation beyond the working copies; runs on the analysis thread.
#include "HumSounds.h"
#include "HearCore.h"
#include <vector>
#include <cmath>
#include <algorithm>

#ifndef HUM_CLICK_SUBTRACT
 #define HUM_CLICK_SUBTRACT 1   // (0 = build 1 behaviour, notch only: the "before" numbers in the tests)
#endif

namespace hum::clicks {

struct Mask {
    const std::vector<double>* times = nullptr; double before = 0.005, after = 0.08;
    bool any() const { return times != nullptr && !times->empty(); }
    bool near(double t) const { if (!any() || !HUM_CLICK_SUBTRACT) return false; for (double c : *times) if (t >= c - before && t <= c + after) return true; return false; }
};

inline gb::a2m::Biquad notch(double f, double sr, double q) {
    gb::a2m::Biquad B; const double w = juce::MathConstants<double>::twoPi * f / sr, c = std::cos(w), al = std::sin(w) / (2 * q), a0 = 1 + al;
    B.b0 = 1 / a0; B.b1 = -2 * c / a0; B.b2 = 1 / a0; B.a1 = -2 * c / a0; B.a2 = (1 - al) / a0; return B;
}

// step 1: the matched clicks taken out (x is modified in place). Returns how many clicks were subtracted.
// Two passes: the clicks that stand clear (nothing else on them) give the speaker -> mic delay and the leak's level, which are the same
// for every click of the take; then every click is subtracted at that delay with that level (a kick right on the click hides the click
// from the matcher, but not from the subtraction).
inline int subtract(std::vector<float>& x, double sr, const std::vector<double>& clicks) {
    if (clicks.empty() || x.size() < (size_t) (sr * 0.1)) return 0;
    const std::vector<float> tA = sound::makeClick(true, sr), tB = sound::makeClick(false, sr); const std::vector<float>* tm[2] { &tA, &tB };
    const int M = (int) (sr * 0.008), lagMax = (int) (sr * 0.04), N = (int) x.size(), n = (int) tA.size();
    double et[2] { 0, 0 }; for (int k = 0; k < 2; ++k) for (int i = 0; i < M; ++i) et[k] += (double) (*tm[k])[(size_t) i] * (*tm[k])[(size_t) i];
    double etFull[2] { 0, 0 }; for (int k = 0; k < 2; ++k) for (int i = 0; i < n; ++i) etFull[k] += (double) (*tm[k])[(size_t) i] * (*tm[k])[(size_t) i];
    // pass 1: the clear clicks (the first 8 ms of a template correlate > 0.6 at some lag within 40 ms) -> lag, gain per template
    std::vector<int> lags; std::vector<double> gains[2];
    for (double c : clicks) { const int a0 = (int) std::lround(c * sr); if (a0 < 0 || a0 + lagMax + n >= N) continue;
        int bestK = -1, bestLag = 0; double bestCorr = 0, bestGain = 0;
        for (int k = 0; k < 2; ++k) for (int L = 0; L <= lagMax; ++L) { const float* xs = x.data() + a0 + L; double dot = 0, ex = 0; const float* T = tm[k]->data();
            for (int i = 0; i < M; ++i) { dot += (double) xs[i] * T[i]; ex += (double) xs[i] * xs[i]; }
            if (dot <= 0 || ex <= 1e-12) continue; const double corr = dot / std::sqrt(ex * et[k]); if (corr > bestCorr) { bestCorr = corr; bestLag = L; bestGain = dot / et[k]; bestK = k; } }
        if (bestK >= 0 && bestCorr > 0.6) { lags.push_back(bestLag); gains[bestK].push_back(bestGain); } }
    if (lags.size() < 2 && clicks.size() > 2) return 0;   // (nothing clearly leaked: leave the take alone)
    if (lags.empty()) return 0;
    std::nth_element(lags.begin(), lags.begin() + (long) lags.size() / 2, lags.end()); const int lag = lags[lags.size() / 2];
    double gain[2]; for (int k = 0; k < 2; ++k) { if (gains[k].empty()) gain[k] = -1; else { auto& v = gains[k]; std::nth_element(v.begin(), v.begin() + (long) v.size() / 2, v.end()); gain[k] = v[v.size() / 2]; } }
    if (gain[0] < 0 && gain[1] < 0) return 0; if (gain[0] < 0) gain[0] = gain[1] * 1.5; if (gain[1] < 0) gain[1] = gain[0] / 1.5;   // (the accent click is 1.5x the beat click)
    // pass 2: every click, at that lag (+-1 ms searched), the template that fits better; the gain of the take's clear clicks, trimmed to what
    // least squares says here when that is smaller (never more: a kick must not be carved)
    int found = 0; const int wob = (int) (sr * 0.001);
    for (double c : clicks) { const int a0 = (int) std::lround(c * sr) + lag; if (a0 - wob < 0 || a0 + wob + n >= N) continue;
        int bestK = -1, bestL = a0; double bestDot = 0;
        for (int k = 0; k < 2; ++k) for (int L = a0 - wob; L <= a0 + wob; ++L) { double dot = 0; const float* T = tm[k]->data(); const float* xs = x.data() + L; for (int i = 0; i < M; ++i) dot += (double) xs[i] * T[i];
            if (dot / std::sqrt(et[k]) > bestDot) { bestDot = dot / std::sqrt(et[k]); bestK = k; bestL = L; } }
        if (bestK < 0) continue;
        double dot = 0; const float* T = tm[bestK]->data(); for (int i = 0; i < n; ++i) dot += (double) x[(size_t) (bestL + i)] * T[i];
        const double g = juce::jlimit(0.0, gain[bestK], dot / etFull[bestK]); if (g <= 0) continue;
        for (int i = 0; i < n; ++i) x[(size_t) (bestL + i)] -= (float) (g * T[i]); ++found; }
    return found;
}

// steps 1 + 2 together: the take with the clicks taken out (a copy)
inline std::vector<float> removed(const std::vector<float>& x0, double sr, const std::vector<double>* clicks) {
    if (clicks == nullptr || clicks->empty()) return x0;
    auto x = x0; if (HUM_CLICK_SUBTRACT) subtract(x, sr, *clicks);
    return gb::a2m::filtered(x, { notch(2000.0, sr, 6.0), notch(2000.0, sr, 6.0), notch(1400.0, sr, 6.0), notch(1400.0, sr, 6.0) });
}

} // namespace hum::clicks
