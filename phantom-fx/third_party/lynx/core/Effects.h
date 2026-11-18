// SPDX-License-Identifier: AGPL-3.0-or-later
// PHANTOM FX SHIM (not an upstream LYNX file): the part of LYNX SHARE's core/Effects.h that core/LynxVerbs.cpp uses —
// the Effect base class (prepare / reset / process with 0..1 parameters) and the parameter-info structs, with LYNX's own
// declarations (Ka1-S/LYNX-SHARE @ d141370, src/core/Effects.h lines 19-31, 87-113 and its latencySamples()).
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx
{
struct EffectParamInfo
{
    const char* name;
    float defaultValue;
};

struct EffectInfo
{
    const char* name;
    std::vector<EffectParamInfo> params;
};

using NoteMask = std::array<std::uint64_t, 2>;

class Effect
{
public:
    virtual ~Effect() = default;
    virtual void prepare (double sampleRate) noexcept = 0;
    virtual void reset() noexcept = 0;
    virtual void process (float* left, float* right, int n, const EffectParams& params, double bpm) noexcept = 0;
    // (Effects.h lines 105-113 and the latency report: the parts core/ApexLimiter.cpp overrides)
    virtual float controlOutput (int /*output*/) const noexcept { return 0.0f; }
    virtual void setSidechain (const float* /*left*/, const float* /*right*/) noexcept {}
    virtual int latencySamples() const noexcept { return 0; }
    virtual void setSongPosition (double /*ticks*/) noexcept {}
    virtual void setPlaying (bool /*playing*/) noexcept {}
    virtual void setHeldNotes (const NoteMask* /*channels*/, int /*count*/) noexcept {}
};
} // namespace lynx
