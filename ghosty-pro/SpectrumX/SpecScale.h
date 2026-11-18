#pragma once
// Scales + snap-to-key, shared by the pad / loop engine (sp::) and the SPECTRAL LAB (lab::). Same order as the key menu.
#include <juce_core/juce_core.h>
#include <vector>
#include <algorithm>
#include <cmath>

namespace sp {
inline const std::vector<int>& scaleSteps(int s) {
    static const std::vector<std::vector<int>> S { { 0, 2, 4, 5, 7, 9, 11 }, { 0, 2, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 8, 11 }, { 0, 2, 3, 5, 7, 9, 10 }, { 0, 3, 5, 7, 10 }, { 0, 2, 4, 7, 9 }, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } };
    return S[(size_t) juce::jlimit(0, 6, s)];
}
inline juce::StringArray scaleNames() { return { "Major", "Minor", "Harmonic minor", "Dorian", "Pentatonic minor", "Pentatonic major", "Chromatic" }; }
inline float snapPitch(float m, int root, int scale) {
    const int n = (int) std::round(m); const auto& sc = scaleSteps(scale);
    int best = n, bd = 99;
    for (int k = n - 6; k <= n + 6; ++k) { const int rel = ((k - root) % 12 + 12) % 12; if (std::find(sc.begin(), sc.end(), rel) != sc.end() && std::abs(k - n) < bd) { bd = std::abs(k - n); best = k; } }
    return (float) best;
}
} // namespace sp
