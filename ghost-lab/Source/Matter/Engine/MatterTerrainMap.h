// Ghost Matter - AGPL-3.0-only (see LICENSE, NOTICE.md).
//
// Ghost Matter's TERRAIN mapping. Brandon's rule for the open-source instruments (2026-11-04): TERRAIN never adds our own
// sound on top; it only MOVES the original engines' own controls. So the land moves:
//   live, while you drag (the audio thread):
//     SPACE (Pertylizer's spatializer, early reflections and FDN): where it sits (the land's balance and depth), ROOM (how
//     much land), TAIL (plateaus hold it), WIDTH (merging mountains), WANDER (movement and EVOLVE)
//     PATTERN (Pertylizer's Turing machine): CHANGE follows how jagged the land is
//   in the matter (re-rendered a moment after each move):
//     STRETCH (PaulXStretch): the tallest mountain stretches it further, merging widens SPREAD, spires add OCTAVE UP,
//     valleys add OCTAVE DOWN
//     MAKE (Flues): rough land roughens it, the land's left / right lean darkens / brightens it
//     MORPH: the land's mass takes it further towards the other sound
// Pure functions (any thread, no allocation).
#pragma once
#include "TerrainFeatures.h"
#include "Settings.h"
#include <array>

namespace matter
{
struct MatterLand
{
    // live (offsets added to the host parameters, then clamped)
    float spaceX = 0, spaceY = 0, room = 0, tail = 0, width = 0, wander = 0, mutate = 0;
    // the matter
    float stretchMul = 1, spread = 0, up = 0, down = 0, rough = 0, bright = 0, morph = 0;
    std::array<ghost::terrain::Meter, 4> meters {};
    int meterCount = 0;
    bool any = false;
    bool sameRender (const MatterLand& o) const;   // the matter would be the same
};
MatterLand mapMatterLand (const ghost::terrain::Terrain& t, float motion = 0.0f, float amount = 1.0f);
// the render settings with the land's moves applied
RenderSettings applyLand (const RenderSettings& base, const MatterLand& land);
}
