// SPDX-License-Identifier: AGPL-3.0-or-later
// PHANTOM FX SHIM (not an upstream LYNX file): the two definitions core/LynxVerbs.{h,cpp} take from LYNX SHARE's
// core/Song.h, cut down to the effects Phantom FX uses (six reverbs, the echo suite, Prism Multiband). The names and the parameter array are LYNX's own
// (Ka1-S/LYNX-SHARE @ d141370, src/core/Song.h lines 922-1071), so the vendored reverb files compile unchanged.
#pragma once
#include <array>
#include <cstdint>

namespace lynx
{
enum class EffectKind : std::uint8_t { epochVerb, atrium, foilPlate, nebula, halo, horizon, echoform, spiralEcho, orbit, latticeMod, prismMultiband, crestCompressor, sentryGate };
constexpr int ppq = 960; // ticks per quarter note (Song.h line 25)
constexpr int maxEffectParams = 192;
using EffectParams = std::array<float, maxEffectParams>;
} // namespace lynx
