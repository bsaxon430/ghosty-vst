#pragma once
// Ghosty Beats RHYTHM DNA: the feature vector of a pattern (density per track / beat position, syncopation, velocities, swing, retrigger
// placement, pitch movement, silence, accent map) and the distance between two. Split out of Rhythm.h so the style engine (StyleGen.h,
// included through BeatModel.h -> GenreKits.h) can score candidates with it; Rhythm.h includes this and keeps the same gb::rhythm API.
#include "BeatModel.h"

namespace gb::rhythm {

constexpr int kPos = 16;   // DNA works on 16 positions per bar (1/16 grid)

inline int posOf(const Pattern& p, int s) { return juce::jlimit(0, kPos - 1, (int) std::floor(std::fmod(s * stepBeats(p.res), 4.0) / 0.25 + 1e-6)); }
inline float metric(int pos) { pos &= 15; if (pos == 0) return 1.0f; if (pos == 8) return 0.8f; if (pos == 4 || pos == 12) return 0.6f; if ((pos & 1) == 0) return 0.4f; return 0.2f; }
inline float roleWeight(int role) { return role == RoleKick || role == RoleSnare ? 1.3f : (role == RoleHat ? 0.8f : 1.0f); }

struct DNA {
    std::array<float, kTracks> density {};                          // hits per step, per track
    std::array<float, kPos> posDensity {};                          // hits per bar position (all tracks)
    std::array<std::array<float, kPos>, kTracks> trackPos {};       // probability of a hit per track and position
    float syncopation = 0, velMean = 0, velStd = 0, swing = 0, retrig = 0, pitchMove = 0, silence = 0;
    std::array<float, 4> velHist {};
    std::array<float, kPos> retrigPos {}, accent {};
    int hits = 0;
};

inline DNA extract(const Pattern& p, const std::array<uint8_t, kTracks>& roles) {
    DNA d; float vSum = 0, v2 = 0, sync = 0, swing = 0, swingN = 0, pm = 0, pmN = 0, empty = 0, cells = 0;
    std::array<float, kPos> accN {};
    for (int t = 0; t < kTracks; ++t) {
        const int L = p.lenOf(t); std::array<float, kPos> pc {}; int lastPitch = 999;
        for (int s = 0; s < L; ++s) { pc[(size_t) posOf(p, s)] += 1.0f; }
        bool any = false;
        for (int s = 0; s < L; ++s) {
            const auto& st = p.steps[(size_t) t][(size_t) s]; cells += 1;
            if (!st.vel) { empty += 1; continue; }
            any = true; const int pos = posOf(p, s); ++d.hits;
            d.density[(size_t) t] += 1.0f / L; d.trackPos[(size_t) t][(size_t) pos] += 1.0f / juce::jmax(1.0f, pc[(size_t) pos]);
            d.posDensity[(size_t) pos] += 1.0f; vSum += st.vel; v2 += (float) st.vel * st.vel; d.velHist[(size_t) juce::jmin(3, st.vel / 32)] += 1;
            // syncopation: a hit on a weak position followed by silence on the next stronger one
            const int nx = (pos + 1) & 15; if (metric(nx) > metric(pos) && !p.steps[(size_t) t][(size_t) ((s + 1) % L)].vel) sync += metric(nx) - metric(pos);
            if (pos & 1) { swing += st.nudge / 100.0f; swingN += 1; }
            if (st.roll > 1 || st.rate) { d.retrig += 1; d.retrigPos[(size_t) pos] += 1; }
            if (st.vel >= 110) { d.accent[(size_t) pos] += st.vel / 127.0f; accN[(size_t) pos] += 1; }
            if (lastPitch != 999) { pm += std::abs(st.pitch - lastPitch); pmN += 1; } lastPitch = st.pitch;
        }
        juce::ignoreUnused(any, roles);
    }
    const float n = (float) juce::jmax(1, d.hits);
    d.velMean = vSum / n / 127.0f; d.velStd = std::sqrt(juce::jmax(0.0f, v2 / n - (vSum / n) * (vSum / n))) / 127.0f;
    for (auto& h : d.velHist) h /= n;
    d.syncopation = sync / n; d.swing = swingN > 0 ? swing / swingN : 0; d.retrig /= n; d.pitchMove = pmN > 0 ? pm / pmN / 12.0f : 0; d.silence = cells > 0 ? empty / cells : 1;
    const float bars = (float) juce::jmax(1.0, p.beats() / 4.0);
    for (int i = 0; i < kPos; ++i) { d.posDensity[(size_t) i] /= bars * kTracks; d.retrigPos[(size_t) i] /= n; if (accN[(size_t) i] > 0) d.accent[(size_t) i] /= accN[(size_t) i]; }
    return d;
}

inline float distance(const DNA& a, const DNA& b) {
    float d = 0;
    for (int t = 0; t < kTracks; ++t) { d += 4.0f * std::abs(a.density[(size_t) t] - b.density[(size_t) t]); for (int i = 0; i < kPos; ++i) d += 0.6f * std::abs(a.trackPos[(size_t) t][(size_t) i] - b.trackPos[(size_t) t][(size_t) i]); }
    for (int i = 0; i < kPos; ++i) d += 2.0f * std::abs(a.posDensity[(size_t) i] - b.posDensity[(size_t) i]) + std::abs(a.accent[(size_t) i] - b.accent[(size_t) i]) + std::abs(a.retrigPos[(size_t) i] - b.retrigPos[(size_t) i]);
    d += 3.0f * std::abs(a.syncopation - b.syncopation) + 2.0f * std::abs(a.velMean - b.velMean) + 2.0f * std::abs(a.velStd - b.velStd) + 2.0f * std::abs(a.swing - b.swing)
       + 2.0f * std::abs(a.retrig - b.retrig) + std::abs(a.pitchMove - b.pitchMove) + 2.0f * std::abs(a.silence - b.silence);
    for (int i = 0; i < 4; ++i) d += std::abs(a.velHist[(size_t) i] - b.velHist[(size_t) i]);
    return d;
}
inline DNA lerp(const DNA& a, const DNA& b, float w) {
    DNA d = a; auto L = [w](float x, float y) { return x + (y - x) * w; };
    for (int t = 0; t < kTracks; ++t) { d.density[(size_t) t] = L(a.density[(size_t) t], b.density[(size_t) t]); for (int i = 0; i < kPos; ++i) d.trackPos[(size_t) t][(size_t) i] = L(a.trackPos[(size_t) t][(size_t) i], b.trackPos[(size_t) t][(size_t) i]); }
    for (int i = 0; i < kPos; ++i) { d.posDensity[(size_t) i] = L(a.posDensity[(size_t) i], b.posDensity[(size_t) i]); d.accent[(size_t) i] = L(a.accent[(size_t) i], b.accent[(size_t) i]); d.retrigPos[(size_t) i] = L(a.retrigPos[(size_t) i], b.retrigPos[(size_t) i]); }
    d.syncopation = L(a.syncopation, b.syncopation); d.velMean = L(a.velMean, b.velMean); d.velStd = L(a.velStd, b.velStd); d.swing = L(a.swing, b.swing);
    d.retrig = L(a.retrig, b.retrig); d.pitchMove = L(a.pitchMove, b.pitchMove); d.silence = L(a.silence, b.silence);
    for (int i = 0; i < 4; ++i) d.velHist[(size_t) i] = L(a.velHist[(size_t) i], b.velHist[(size_t) i]);
    return d;
}

inline int hamming(const Pattern& a, const Pattern& b) { int d = 0; for (int t = 0; t < kTracks; ++t) for (int s = 0; s < kMaxSteps; ++s) if ((a.steps[(size_t) t][(size_t) s].vel > 0) != (b.steps[(size_t) t][(size_t) s].vel > 0)) ++d; return d; }

} // namespace gb::rhythm
