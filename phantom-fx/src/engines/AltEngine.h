// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX: extra engines inside an effect (DECISIONS.md 85). An effect keeps its own engine ("PHANTOM", always the default) and gains
// more behind its ENGINE menu. An alternative engine reads the effect's own controls (by id, in their own units) and runs another
// project's real DSP; it never declares controls of its own, so presets, automation and the page stay as they are.
#pragma once
#include "../core/PhantomEffect.h"
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace phantom::engines
{
// what an alternative engine is handed every block: the effect's controls (host value + modulation, own units)
struct Controls
{
    const float* values = nullptr; int count = 0; const std::map<std::string, int>* index = nullptr;
    float get (const char* id, float fallback) const noexcept
    {
        auto it = index->find (id); if (it == index->end() || it->second >= count) return fallback; return values[it->second];
    }
    bool has (const char* id) const noexcept { return index->find (id) != index->end(); }
};

class AltEngine
{
public:
    virtual ~AltEngine() = default;
    virtual void prepare (double sampleRate, int maxBlock) = 0;     // message thread (may allocate)
    virtual void reset() noexcept = 0;                              // clears the state (audio thread safe: no allocation)
    // stereo in place; voice = the engine's own mode / preset chosen in the ENGINE menu
    virtual void process (float* L, float* R, int n, const Controls& c, int voice, const TempoInfo& t) noexcept = 0;
    virtual int latencySamples (int voice) const noexcept { juce::ignoreUnused (voice); return 0; }
    // message thread, whenever the rack refreshes the parameters (before playback, after any menu change): the chance to prepare what
    // the chosen voice needs (tables) away from the audio thread
    virtual void update (const Controls& c, int voice) { juce::ignoreUnused (c, voice); }
    // the menu entry's level match: by default the wrapper applies it to the whole output (a wet-only engine); an engine with a clean
    // path (DRIVE 0 = the dry signal) applies it to its shaped part only, so it still nulls
    virtual bool appliesLevel() const noexcept { return false; }
    // a control whose sweep makes sound by design (a tape-style delay bending pitch as its time glides): the zipper check judges it against
    // the engine's own floor only
    virtual bool zipperEventControl (const std::string&) const { return false; }
    virtual bool zipperEventControl (const std::string& c, int voice) const { juce::ignoreUnused (voice); return zipperEventControl (c); }
    // (DECISIONS.md 95) per voice: a check this voice fails by design (its reason), and whether it runs under the rack's oversampling
    virtual std::string byDesign (const std::string& check, int voice) const { juce::ignoreUnused (check, voice); return {}; }
    virtual bool wantsOversampling() const noexcept { return true; }
    virtual void setLevel (float gain) noexcept { juce::ignoreUnused (gain); }
    // non-parameter state saved with the project (a drawn curve): message thread only, as PhantomEffect's own
    virtual bool hasData() const { return false; }
    virtual juce::var getData() const { return {}; }
    virtual void setData (const juce::var&) {}
};

// one entry of an ENGINE menu: "PHANTOM" (the effect's own module) or an alternative engine with one of its voices.
// gain = the level match against the PHANTOM engine (pink noise at the defaults, measured by the tests' calibration run)
struct Entry { std::string label; int engine = -1; int voice = 0; float gain = 1.0f; };

// a 0..1 normalised value for a law v -> unit that rises with v (LYNX's laws are monotonic): bisection, 40 steps
template <typename F> inline float inverse01 (F law, double unit) noexcept
{
    double lo = 0.0, hi = 1.0; if (unit <= law (0.0f)) return 0.0f; if (unit >= law (1.0f)) return 1.0f;
    for (int i = 0; i < 40; ++i) { const double m = 0.5 * (lo + hi); if (law ((float) m) < unit) lo = m; else hi = m; }
    return (float) (0.5 * (lo + hi));
}
// piecewise-linear map of one of our controls onto an engine's control so that our default lands on the engine's own default:
// ours lo..def..hi -> theirs a..d..b
inline float around (float v, float lo, float def, float hi, float a, float d, float b) noexcept
{
    if (v <= def) return def > lo ? a + (d - a) * (v - lo) / (def - lo) : d;
    return hi > def ? d + (b - d) * (v - def) / (hi - def) : d;
}
inline float clamp01 (float v) noexcept { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
} // namespace phantom::engines
