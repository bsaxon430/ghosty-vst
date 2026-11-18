// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX shared DSP: per-parameter smoothing and the EffectBase helper every module builds on.
#pragma once
#include "../core/PhantomEffect.h"

namespace phantom
{
// Second-order smoother (two one-poles in cascade) in the parameter's own units: the glide has no kink at a new target, so even
// block-rate staircases from the host leave no broadband residue. Continuous parameters glide; stepped ones (choice / bool / int) snap.
struct ParamSmoother
{
    float current = 0.0f, mid = 0.0f, target = 0.0f, coeff = 0.0f;
    bool stepped = false;

    void prepare (double sampleRate, float smoothMs, float initial, bool isStepped) noexcept
    {
        stepped = isStepped || smoothMs <= 0.0f;
        coeff = stepped ? 1.0f : 1.0f - std::exp (-1.0f / (float) (sampleRate * smoothMs * 0.0005));   // each stage half the time
        current = mid = target = initial;
    }
    void setTarget (float v) noexcept { target = v; if (stepped) current = mid = v; }
    void snap (float v) noexcept { current = mid = target = v; }
    inline float next() noexcept
    {
        if (stepped || (current == target && mid == target)) return current;   // settled
        mid += coeff * (target - mid);
        current += coeff * (mid - current);
        if (std::abs (current - target) < 1.0e-7f && std::abs (mid - target) < 1.0e-7f) current = mid = target;   // settle exactly
        return current;
    }
    // advance n samples at once (for modules that only need block-rate values but still want zipper-free glides)
    inline float skip (int n) noexcept
    {
        if (stepped || (current == target && mid == target)) return current;   // settled: nothing to advance
        for (int i = 0; i < n; ++i) next();
        return current;
    }
    // next() as a gain from dB, recomputed only when the value moves (a settled level costs no pow per sample)
    inline float nextGain() noexcept { const float v = next(); if (v != gainDb) { gainDb = v; gainLin = std::pow (10.0f, v * 0.05f); } return gainLin; }
    inline float skipGain (int n) noexcept { const float v = skip (n); if (v != gainDb) { gainDb = v; gainLin = std::pow (10.0f, v * 0.05f); } return gainLin; }
    float gainDb = 0.0f, gainLin = 1.0f;
    bool isSmoothing() const noexcept { return ! stepped && (std::abs (current - target) > 1.0e-6f || std::abs (mid - target) > 1.0e-6f); }
};

// Holds the declared ParamSpecs and one smoother per parameter. A module calls `beginBlock (ctx)` first thing in process(),
// then `p (i).next()` per sample or `p (i).skip (n)` per sub-block.
class EffectBase : public PhantomEffect
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels) override
    {
        sr = sampleRate; maxBlock = maxBlockSize; channels = numChannels;
        specs = parameters();
        smoothers.resize (specs.size());
        for (size_t i = 0; i < specs.size(); ++i)
            smoothers[i].prepare (sampleRate, specs[i].smoothMs, specs[i].def, ! specs[i].isContinuous());
        prepareEffect (sampleRate, maxBlockSize, numChannels);
    }
    void reset() override
    {
        for (auto& s : smoothers) s.snap (s.target);
        primed = false;
        resetEffect();
    }
    int getLatencySamples() const override { return 0; }
    void updateParameters (const ModulationContext& ctx) override { beginBlock (ctx); }

protected:
    virtual void prepareEffect (double, int, int) {}
    virtual void resetEffect() {}

    // call at the top of process(): takes this block's targets from the rack
    void beginBlock (const ModulationContext& ctx) noexcept
    {
        const int n = juce::jmin ((int) smoothers.size(), ctx.numValues);
        // after prepare / reset the module starts where the host put it (no glide from the declared defaults); from then on everything glides
        if (! primed) { for (int i = 0; i < n; ++i) smoothers[(size_t) i].snap (ctx.values[i]); primed = n > 0; return; }
        for (int i = 0; i < n; ++i) smoothers[(size_t) i].setTarget (ctx.values[i]);
    }
    ParamSmoother& p (int i) noexcept { return smoothers[(size_t) i]; }
    float target (int i) const noexcept { return smoothers[(size_t) i].target; }
    int choice (int i) const noexcept { return (int) std::lround (smoothers[(size_t) i].target); }
    bool on (int i) const noexcept { return smoothers[(size_t) i].target >= 0.5f; }

    double sr = 48000.0; int maxBlock = 0, channels = 2; bool primed = false;
    std::vector<ParamSpec> specs;
    std::vector<ParamSmoother> smoothers;
};

inline float dbToGain (float dB) noexcept { return std::pow (10.0f, dB * 0.05f); }
inline float gainToDb (float g) noexcept { return 20.0f * std::log10 (juce::jmax (1.0e-9f, g)); }
} // namespace phantom
