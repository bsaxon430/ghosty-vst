/*
 * Ghost Bass - a bass instrument built on Surge XT.
 * Copyright 2026 the Ghost Bass authors. Released under the GNU General Public Licence v3 or later (see LICENSE).
 *
 * TERRAIN in Ghost Bass: a copy of GhostBlock's own Shared/Terrain component (written by us; the copy here is GPL as part of
 * this fork, and nothing from this fork goes back the other way). The engine side (model, mapping, DSP, serialisation) runs
 * inside surge-common, which does not link JUCE, so the few JUCE helpers the shared code used are these.
 */
#ifndef GHOSTBASS_SRC_GHOST_TERRAIN_TERRAINSTD_H
#define GHOSTBASS_SRC_GHOST_TERRAIN_TERRAINSTD_H
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace ghost::terrain::tj
{
template <class T> inline T jlimit(T lo, T hi, T v) { return v < lo ? lo : (v > hi ? hi : v); }
template <class T> inline T jmin(T a, T b) { return b < a ? b : a; }
template <class T> inline T jmax(T a, T b) { return a < b ? b : a; }
inline int roundToInt(float v) { return (int)std::lround(v); }
inline int roundToInt(double v) { return (int)std::lround(v); }
constexpr float twoPi = 6.283185307179586f;
constexpr double pi = 3.141592653589793;

// a small deterministic generator (the shared code used juce::Random with a 64-bit seed; the sequence differs, the use
// is the same: RANDOMIZE and breed are repeatable per seed)
struct Random
{
    explicit Random(uint64_t seed) : s(seed * 6364136223846793005ULL + 1442695040888963407ULL) {}
    uint32_t nextU32()
    {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        return (uint32_t)(s >> 32);
    }
    float nextFloat() { return (float)(nextU32() >> 8) / 16777216.0f; }
    bool nextBool() { return (nextU32() & 0x80000000U) != 0; }
    uint64_t s;
};
} // namespace ghost::terrain::tj
#endif
