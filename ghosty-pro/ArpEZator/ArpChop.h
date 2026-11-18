#pragma once
// SLICE TO NOTES: transient slicing for YOUR SAMPLE. A copy of the BREAK CHOP algorithm from BeatsX/Rhythm.h (Ghosty Beats),
// taking a mono buffer instead of a Beats Sound (the algorithm is unchanged; keep the two in step if one is retuned):
// 3-band (low / mid / high) energy in 2.5 ms frames, onset strength = summed dB rise over 5 ms, local-maximum picking with a
// dB threshold set by the sensitivity, min gap 30 ms, each onset moved to the first sample that rises above the ringing
// before it (then to the zero crossing). Returns slice starts in samples (first = 0).
#include <juce_core/juce_core.h>
#include <array>
#include <vector>
#include <cmath>
#include <algorithm>

namespace arpsmp
{
inline std::vector<int> chop(const std::vector<float>& x, double sr, float sensitivity, int minSlices = 8, int maxSlices = 64) {
    const int n = (int) x.size();
    const int hop = juce::jmax(16, (int) (sr * 0.0025)), frames = n / hop;
    if (frames < 8) return { 0 };
    std::array<std::vector<float>, 3> L; for (auto& v : L) v.assign((size_t) frames, -120.0f);
    const float a1 = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * 200.0f / (float) sr), a2 = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * 2500.0f / (float) sr);
    // peak envelopes: instant attack, release longer than each band's waveform period (so a ringing tail reads as smooth)
    constexpr float RL = 0.06f, RM = 0.03f, RH = 0.015f, TB = 9.0f, TS = 8.0f; constexpr int LB = 3;   // tuned on synthetic breaks (tests)
    const float rel[3] { std::exp(-1.0f / (RL * (float) sr)), std::exp(-1.0f / (RM * (float) sr)), std::exp(-1.0f / (RH * (float) sr)) };
    float l1 = 0, l2 = 0, peak = 1e-9f, env[3] {};
    for (int f = 0; f < frames; ++f) {
        for (int k = 0; k < hop; ++k) { const float v = x[(size_t) (f * hop + k)]; l1 += a1 * (v - l1); l2 += a2 * (v - l2); const float band[3] { std::abs(l1), std::abs(l2 - l1), std::abs(v - l2) };
            for (int b = 0; b < 3; ++b) env[b] = juce::jmax(band[b], env[b] * rel[b]); peak = juce::jmax(peak, std::abs(v)); }
        for (int b = 0; b < 3; ++b) L[(size_t) b][(size_t) f] = 20.0f * std::log10(env[b] + 1e-6f);
    }
    const float floorDb = 20.0f * std::log10(peak) - 45.0f;
    std::vector<float> flux((size_t) frames, 0.0f);
    for (int f = 2; f < frames; ++f) { float r = 0; for (int b = 0; b < 3; ++b) { const float now = L[(size_t) b][(size_t) f]; if (now < floorDb) continue; r += juce::jmax(0.0f, now - juce::jmax(L[(size_t) b][(size_t) juce::jmax(0, f - LB)], floorDb)); } flux[(size_t) f] = r; }
    const int gap = (int) std::ceil(0.03 * sr / hop);
    auto pick = [&](float thrDb) {
        std::vector<int> on;
        for (int f = 2; f < frames - 1; ++f) {
            if (flux[(size_t) f] < thrDb) continue;
            bool isMax = true; for (int k = juce::jmax(2, f - 4); k <= juce::jmin(frames - 1, f + 4); ++k) if (flux[(size_t) k] > flux[(size_t) f] || (flux[(size_t) k] == flux[(size_t) f] && k < f)) { isMax = false; break; }
            if (!isMax) continue;
            float lvl = -200; for (int b = 0; b < 3; ++b) lvl = juce::jmax(lvl, L[(size_t) b][(size_t) f]); if (lvl < floorDb + 6) continue;
            if (!on.empty() && f - on.back() < gap) { if (flux[(size_t) f] > flux[(size_t) on.back()]) on.back() = f; continue; }
            on.push_back(f);
        }
        return on;
    };
    float thr = juce::jlimit(1.0f, 30.0f, TB - sensitivity * TS);
    auto on = pick(thr);
    for (int it = 0; it < 10 && (int) on.size() < minSlices - 1 && thr > 1.6f; ++it) { thr *= 0.75f; on = pick(thr); }
    if ((int) on.size() > maxSlices - 1) {   // keep the strongest
        std::vector<int> idx = on; std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) { return flux[(size_t) a] > flux[(size_t) b]; });
        idx.resize((size_t) maxSlices - 1); std::sort(idx.begin(), idx.end()); on = idx;
    }
    std::vector<int> out { 0 };
    for (int f : on) {
        // the rise happened between frames f-2 and f: find the first sample clearly above what was ringing before
        const int w0 = juce::jmax(0, (f - LB) * hop), w1 = juce::jmin(n, (f + 2) * hop);
        float pre = 0; for (int k = juce::jmax(0, w0 - (int) (sr * 0.003)); k < w0; ++k) pre = juce::jmax(pre, std::abs(x[(size_t) k]));
        float post = 0; for (int k = w0; k < w1; ++k) post = juce::jmax(post, std::abs(x[(size_t) k]));
        int s0 = w0; const float lim = pre + 0.25f * juce::jmax(0.0f, post - pre); while (s0 < w1 - 1 && std::abs(x[(size_t) s0]) <= lim) ++s0;
        const int back = juce::jmax(w0 - hop, s0 - (int) (sr * 0.001)); while (s0 > back && (x[(size_t) s0] > 0) == (x[(size_t) (s0 - 1)] > 0)) --s0;   // to the zero crossing
        if (s0 - out.back() > (int) (sr * 0.02)) out.push_back(s0);
    }
    return out;
}

} // namespace arpsmp
