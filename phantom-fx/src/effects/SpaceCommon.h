// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX Space group (effects 15-22): small shared pieces of our own glue (ducking detector). The engines are in SpaceCloud.h,
// SpaceHalls.h, SpaceMod.h, SpaceSpectral.h and SpaceDoor.h.
#pragma once
#include "ColourSst.h"
#include "../dsp/DelayLine.h"

namespace phantom
{
// Ducking: the wet level dips while the input plays (2 ms attack, 300 ms release on the input's peak), by DUCK amount, up to -24 dB.
class Ducker
{
public:
    void prepare (double sampleRate) { att = 1.0f - std::exp (-1.0f / (float) (sampleRate * 0.002)); rel = 1.0f - std::exp (-1.0f / (float) (sampleRate * 0.3)); reset(); }
    void reset() noexcept { env = 0.0f; }
    inline void push (float peak) noexcept { env += (peak > env ? att : rel) * (peak - env); }
    inline float gain (float amount) const noexcept
    {
        if (amount <= 0.0f) return 1.0f;
        const float k = juce::jlimit (0.0f, 1.0f, env * 4.0f);       // full duck from about -12 dBFS of input
        return 1.0f - amount * 0.94f * k;
    }
private:
    float env = 0.0f, att = 0.1f, rel = 0.001f;
};
// a pre-delay that moves like a slow tape transport: its read point changes by at most 2 % of real time (pitch within +-2 % while it
// moves), so turning PRE-DELAY bends pitch slightly instead of clicking or swooping. One per unit (state in the caller).
struct PreGlide
{
    float cur = -1.0f;
    inline float operator() (float want) noexcept { if (cur < 0.0f) cur = want; else cur += juce::jlimit (-0.02f, 0.02f, (want - cur) * 0.001f); return cur; }
};
} // namespace phantom
