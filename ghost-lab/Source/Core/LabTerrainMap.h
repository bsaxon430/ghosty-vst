// Ghost Lab's TERRAIN mapping: the landscape drives the lab's own processes, not just an EQ (Brandon: "these mountains and
// movements and blends actually change the sound and move around all the toggles"). From the land's features
// (Shared/Terrain/TerrainFeatures.h):
//   spires (thin, tall)      -> SHATTER (the sound broken into pieces; more spires, more pieces) + a live octave shimmer
//   plateaus (flat-topped)   -> FREEZE (one moment held, where the tallest plateau stands in time) and SMEAR
//   rough land / grit        -> GRAIN CLOUD (scatter grows with the jaggedness; grain size with the mountains' size)
//   valleys                  -> the spectral carve (the sculpt cuts) - and a darker tilt
//   merging mountains        -> SMEAR, and with a second specimen loaded: BREED (the more they merge, the more of the other)
//   across (x)               -> where in frequency the sculpt works; the land's lean -> TWIST's bend
//   depth (z)                -> when in the sound (the sculpt follows time; FREEZE holds the tallest plateau's moment)
//   height                   -> how much; movement (drag speed, EVOLVE) -> a live wobble while you listen
// The exact render runs these as Ghost Lab steps (CDP8 + our own sculpt); while you drag, the live chain plays an approximation
// of the same (TerrainLive) so you hear every move at once, and the exact render cross-fades in when it lands.
#pragma once
#include "Actions.h"
#include "TerrainFeatures.h"
#include "TerrainDsp.h"

namespace glab
{
struct LabLand
{
    std::vector<Step> steps;                     // CDP steps after the sculpt (empty: the sculpt only)
    ghost::terrain::LiveParams live;             // the live approximation
    std::vector<ghost::terrain::Meter> meters;   // the strongest 4 (the readout)
    bool any = false;
};
LabLand mapLabLand (const ghost::terrain::Terrain& t, float motion = 0.0f, bool partner = false);
}
