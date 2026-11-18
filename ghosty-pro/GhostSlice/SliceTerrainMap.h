// GHOST SLICE build 13: the TERRAIN landscape drives the whole sound, live (Brandon: "move terrain around and hear the sound
// change in real time ... these mountains and movements and blends actually change the sound and move around all the toggles").
//
// What the land does to a take, besides the spectral shape (SliceTerrain.h), from its features (Shared/Terrain/TerrainFeatures.h):
//   plateaus (flat-topped columns)     -> DREAM (the take's longer, softer space) and a held wash (FREEZE)
//   spires (very thin, very tall)      -> STUTTER (slices repeating) and SHIMMER (an octave-up grain spray)
//   jaggedness (grit, rough mountains) -> GRIT (drive) and backwards repeats (the chops' randomness)
//   how much merges                    -> SPACE (blends into the room)
//   how many mountains                 -> CHORUS (the take's ensemble: more voices)
//   the spread                         -> WIDTH; the land's lean across frequency -> TONE; its mass -> WARMTH
//   movement (drag speed, EVOLVE)      -> WOBBLE
// Everything is an offset on top of what the take / the knobs already are; a flat land (or none) changes nothing at all, so
// every older session sounds the same. Realtime: the FX offsets and the live chain follow every change of the land at once.
#pragma once
#include "TerrainFeatures.h"
#include "TerrainDsp.h"
#include <vector>

namespace gslice::land
{
struct LandMod
{
    float tone = 0, space = 0, width = 0, warmth = 0, ensemble = 0, dream = 0;   // offsets on the FX
    ghost::terrain::LiveParams live;                                           // the live chain (STUTTER, SHIMMER, GRIT, WOBBLE, FREEZE)
    std::vector<ghost::terrain::Meter> meters;                                 // the strongest 4, for the readout
    bool any = false;                                                          // false: nothing to do (flat land)
    bool operator== (const LandMod& o) const;
};
LandMod mapLand (const ghost::terrain::Terrain& t, float motion = 0.0f);
}
