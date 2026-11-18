#pragma once
// GHOST HUM - TEMPO, SWING and CLEAN.
//
// TEMPO (FREE recordings): the onsets (note starts / hits) weighted by their strength against the 1/16 grid of every tempo 55..220 BPM
//   (the comb score of Ghosty Beats' RE-CREATE: Gaussian distance to the grid, sigma 10 ms, swung off-beats allowed, minus what random
//   onsets would score), times a gentle preference for 70..170 BPM. The grid's beat 0 = the first onset. We OFFER the best tempo and its
//   double / half ([KEEP ORIGINAL] [94 BPM] [188 BPM]) because a comb cannot tell them apart; the owner auditions.
// SWING heard: the average delay of the off-beat 8ths (50 = straight, 66 = triplet).
// CLEAN (always one undo step away):
//   melody  blips < 70 ms away; a short out-of-key note between in-key notes -> the nearest key note; touching same-pitch notes joined;
//           shaky notes (same pitch, tiny gap) smoothed; velocities evened (half way to the average). The melody itself is kept.
//   drums   double hits (same drum within 60 ms) -> the louder one; timing tightened half way to the 1/16 grid (KEEP GROOVE: only the
//           ones more than 1/64 off); velocities evened half way per drum.
#include "HumModel.h"

namespace hum::tempo {

struct Estimate { double bpm = 120, origin = 0, score = 0; std::vector<double> offers; };

inline double combScore(const std::vector<double>& on, const std::vector<float>& w, double bpm, double origin) {
    const double beat = 60.0 / bpm, sig = 0.010; double best = -1;
    for (double sw : { 0.0, 0.04, 0.08, 0.12, 0.167 }) {
        const double pts[] { 0.0, 0.25, 0.5 + sw, 0.75, 1.0 }; double s = 0, tot = 0;
        for (size_t i = 0; i < on.size(); ++i) { const double b = (on[i] - origin) / beat, fr = b - std::floor(b); double d = 1; for (double q : pts) d = juce::jmin(d, std::abs(fr - q)); d *= beat;
            s += w[i] * std::exp(-(d * d) / (sig * sig)); tot += w[i]; }
        best = juce::jmax(best, tot > 0 ? s / tot - 4.0 * sig * std::sqrt(juce::MathConstants<double>::pi) / beat : 0.0);
    }
    return best;
}
inline Estimate estimate(const std::vector<double>& on, const std::vector<float>& strength, double lo = 55, double hi = 220) {
    Estimate E; if (on.size() < 3) { E.offers = { 120 }; return E; }
    E.origin = on.front(); std::vector<float> w = strength; if (w.size() != on.size()) w.assign(on.size(), 1.0f);
    auto prior = [](double b) { const double x = std::log2(b / 110.0); return std::exp(-0.5 * x * x / (0.75 * 0.75)); };
    double best = 120, bs = -1e9;
    for (double b = lo; b <= hi; b += 0.1) { const double s = combScore(on, w, b, E.origin) * (0.6 + 0.4 * prior(b)); if (s > bs) { bs = s; best = b; } }
    for (double b = best - 0.12; b <= best + 0.12; b += 0.005) { const double s = combScore(on, w, b, E.origin) * (0.6 + 0.4 * prior(b)); if (s > bs) { bs = s; best = b; } }
    E.bpm = std::round(best * 10) / 10; E.score = bs;
    E.offers.push_back(E.bpm); if (E.bpm * 2 <= 240) E.offers.push_back(std::round(E.bpm * 2 * 10) / 10); if (E.bpm / 2 >= 50) E.offers.push_back(std::round(E.bpm / 2 * 10) / 10);
    return E;
}
inline double swingHeard(const std::vector<double>& on, double bpm, double origin) {
    const double beat = 60.0 / bpm; double sum = 0; int k = 0;
    for (double t : on) { const double off = std::fmod(juce::jmax(0.0, t - origin) / beat, 1.0); if (off > 0.4 && off < 0.75) { sum += off; ++k; } }
    return k >= 2 ? juce::jlimit(50.0, 75.0, 100.0 * sum / k) : 50.0;
}
// a take's onsets with strengths
inline void onsetsOf(const Take& T, std::vector<double>& on, std::vector<float>& w) {
    on.clear(); w.clear();
    if (T.mode == Melody) for (auto& n : T.notes) { on.push_back(n.t); w.push_back(n.vel); } else for (auto& h : T.hits) { on.push_back(h.t); w.push_back(h.vel); }
}
inline void estimateFor(Take& T) {
    std::vector<double> on; std::vector<float> w; onsetsOf(T, on, w); const auto E = estimate(on, w);
    T.bpm = E.bpm; T.origin = E.origin; T.bpmOffers = E.offers; T.bpmKnown = false; T.swingHeard = swingHeard(on, T.bpm, T.origin);
}

} // namespace hum::tempo

namespace hum::clean {

inline void melody(Take& T) {
    auto& ns = T.notes; sortTake(T);
    // blips
    { std::vector<Note> keep; for (auto& n : ns) if (n.len >= 0.07) keep.push_back(n); if (!keep.empty()) ns.swap(keep); }
    // touching same-pitch notes (gap < 25 ms) join; shaky: a note < 120 ms touching a same-pitch neighbour joins it
    for (size_t i = 0; i + 1 < ns.size();) { auto& a = ns[i]; auto& b = ns[i + 1]; const double gap = b.t - (a.t + a.len);
        if (a.midi == b.midi && gap < 0.025 && (a.len < 0.12 || b.len < 0.12 || gap < 0.012)) { a.len = b.t + b.len - a.t; a.vel = juce::jmax(a.vel, b.vel); ns.erase(ns.begin() + (long) i + 1); } else ++i; }
    // wrong pitches: a short (< 180 ms) out-of-key note between two in-key notes -> the nearest key note (the melody's shape is kept)
    if (T.key >= 0) for (size_t i = 0; i < ns.size(); ++i) { auto& n = ns[i]; if (inScale(n.midi, T.key, T.scale) || n.len >= 0.18) continue;
        const bool prevOk = i == 0 || inScale(ns[i - 1].midi, T.key, T.scale), nextOk = i + 1 >= ns.size() || inScale(ns[i + 1].midi, T.key, T.scale);
        if (prevOk && nextOk) n.midi = snapToScale(n.midi, T.key, T.scale, n.cents); }
    // velocities half way to the average
    if (!ns.empty()) { float m = 0; for (auto& n : ns) m += n.vel; m /= (float) ns.size(); for (auto& n : ns) n.vel = juce::jlimit(0.1f, 1.0f, n.vel + (m - n.vel) * 0.5f); }
}

inline int removeDoubleHits(Take& T, double window = 0.06) {
    sortTake(T); int removed = 0; auto& hs = T.hits;
    for (size_t i = 0; i + 1 < hs.size();) { bool gone = false;
        for (size_t j = i + 1; j < hs.size() && hs[j].t - hs[i].t < window; ++j) if (hs[j].drum == hs[i].drum) {
            if (hs[j].vel > hs[i].vel) { hs[j].t = hs[i].t; hs.erase(hs.begin() + (long) i); } else hs.erase(hs.begin() + (long) j); ++removed; gone = true; break; }
        if (!gone) ++i; }
    return removed;
}
inline void drums(Take& T, bool keepGroove = true) {
    removeDoubleHits(T);
    if (T.bpm > 0) { const double g = 60.0 / T.bpm * 0.25;   // the 1/16 grid
        for (auto& h : T.hits) { const double b = (h.t - T.origin) / g, q = T.origin + std::round(b) * g, off = q - h.t;
            if (!keepGroove || std::abs(off) > g * 0.25) h.t += off * 0.5; } }
    std::array<float, kDrums> sum {}; std::array<int, kDrums> cnt {}; for (auto& h : T.hits) { sum[(size_t) h.drum] += h.vel; ++cnt[(size_t) h.drum]; }
    for (auto& h : T.hits) if (cnt[(size_t) h.drum] > 1) { const float m = sum[(size_t) h.drum] / (float) cnt[(size_t) h.drum]; h.vel = juce::jlimit(0.1f, 1.0f, h.vel + (m - h.vel) * 0.5f); }
    sortTake(T);
}

} // namespace hum::clean
