#pragma once
// GHOST SLICE core types (no GUI): fingerprints, the warp map maths and the units every layer agrees on.
//
// Units (never mix them; every conversion is in RenderPlan.cpp):
//   SF  source frames   int64 at the SOURCE file's own rate (the original audio, never modified)
//   PF  project frames  double at Project::sampleRate (output-time positions in the model: region dstStart, marker t)
//   HF  host frames     int64 at the host / device rate (what the player and the STUDIO renders produce)
//   WF  working frames  the decoded source resampled to the host rate (a cache; WF = SF * hostRate / sourceRate)
#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>
#include <cstdint>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstring>

namespace gslice
{
using int64 = std::int64_t;
using uint64 = std::uint64_t;

// engine version of the render path: bump it on purpose when a change alters rendered samples (goldens, cache keys)
constexpr int kEngineVersion = 2;   // v2 (2026-10-26): r8brain working copy, ELASTIC hold joins (pre-roll, plateau, phase steer)
constexpr int kFormatVersion = 1;
#ifndef GSLICE_VERSION
 #define GSLICE_VERSION "0.1.0"
#endif

// ---------------------------------------------------------------- fingerprints
// SHA-256 over a canonical byte stream (doubles by bit pattern, little-endian ints, strings with their length).
struct Hasher
{
    juce::MemoryOutputStream m;
    Hasher& add (double v)          { uint64 b; std::memcpy (&b, &v, 8); return addU (b); }
    Hasher& add (float v)           { return add ((double) v); }
    Hasher& add (int64 v)           { return addU ((uint64) v); }
    Hasher& add (int v)             { return addU ((uint64) (int64) v); }
    Hasher& add (bool v)            { return addU (v ? 1u : 0u); }
    Hasher& add (const juce::String& s) { auto u = s.toUTF8(); const auto n = (uint64) u.sizeInBytes(); addU (n); m.write (u.getAddress(), (size_t) n); return *this; }
    Hasher& add (const char* s)     { return add (juce::String (s)); }
    Hasher& addU (uint64 v)         { unsigned char b[8]; for (int i = 0; i < 8; ++i) b[i] = (unsigned char) ((v >> (8 * i)) & 0xff); m.write (b, 8); return *this; }
    juce::String hex() const        { return juce::SHA256 (m.getData(), m.getDataSize()).toHexString(); }
    uint64 key() const
    {
        const auto raw = juce::SHA256 (m.getData(), m.getDataSize()).getRawData();
        uint64 k = 0; for (int i = 0; i < 8; ++i) k |= (uint64) (unsigned char) raw[i] << (8 * i);
        return k == 0 ? 1 : k;   // 0 = "none" everywhere
    }
};

// ---------------------------------------------------------------- the warp map: output time -> source position
// Piecewise linear through strictly increasing (t, s) points. Beyond the ends it extrapolates with the end segments' rates
// (the stretcher's look-ahead reads a little past the region; Direct reads clamp to the region).
// rate = ds/dt: > 1 plays faster (shorter), < 1 slower (longer).
struct WarpMap
{
    std::vector<double> t, s;

    bool valid() const
    {
        if (t.size() < 2 || t.size() != s.size()) return false;
        for (size_t i = 1; i < t.size(); ++i) if (! (t[i] > t[i - 1]) || ! (s[i] > s[i - 1])) return false;
        return std::isfinite (t.front()) && std::isfinite (t.back()) && std::isfinite (s.front()) && std::isfinite (s.back());
    }
    size_t segAt (double u) const   // the segment index i with t[i] <= u < t[i+1] (clamped to the end segments)
    {
        if (u <= t.front()) return 0;
        if (u >= t.back()) return t.size() - 2;
        const auto it = std::upper_bound (t.begin(), t.end(), u);
        return (size_t) juce::jlimit<std::ptrdiff_t> (0, (std::ptrdiff_t) t.size() - 2, (it - t.begin()) - 1);
    }
    double rateOf (size_t i) const { return (s[i + 1] - s[i]) / (t[i + 1] - t[i]); }
    double rateAt (double u) const { return rateOf (segAt (u)); }
    double srcAt (double u) const
    {
        const size_t i = segAt (u);
        return s[i] + (u - t[i]) * rateOf (i);
    }
    size_t segAtSrc (double v) const
    {
        if (v <= s.front()) return 0;
        if (v >= s.back()) return s.size() - 2;
        const auto it = std::upper_bound (s.begin(), s.end(), v);
        return (size_t) juce::jlimit<std::ptrdiff_t> (0, (std::ptrdiff_t) s.size() - 2, (it - s.begin()) - 1);
    }
    double outAt (double v) const   // the inverse map
    {
        const size_t i = segAtSrc (v);
        return t[i] + (v - s[i]) / rateOf (i);
    }
    double length() const { return t.back() - t.front(); }
    bool unity (double tol = 1.0e-9) const { for (size_t i = 0; i + 1 < t.size(); ++i) if (std::abs (rateOf (i) - 1.0) > tol) return false; return true; }
    double minRate() const { double r = 1e30; for (size_t i = 0; i + 1 < t.size(); ++i) r = std::min (r, rateOf (i)); return r; }
    double maxRate() const { double r = 0; for (size_t i = 0; i + 1 < t.size(); ++i) r = std::max (r, rateOf (i)); return r; }
    void hashInto (Hasher& h) const { h.add ((int) t.size()); for (size_t i = 0; i < t.size(); ++i) h.add (t[i]).add (s[i]); }
};

// stretch-ratio limits (rate = source / output speed)
namespace limits
{
    constexpr double kHardMin = 0.25, kHardMax = 4.0;     // drags clamp here
    constexpr double kLiveMin = 0.5,  kLiveMax = 2.0;     // the comfortable LIVE range; outside = amber warning
    inline bool extreme (double r) { return r < kLiveMin - 1e-9 || r > kLiveMax + 1e-9; }
}

// raised-cosine fade gain, x in [0, 1]
inline float fadeShape (double x) { x = juce::jlimit (0.0, 1.0, x); return (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * x)); }

inline juce::String shortFp (const juce::String& fp) { return fp.substring (0, 10); }
} // namespace gslice
