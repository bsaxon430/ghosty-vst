#pragma once
// TERRAIN (de)serialisation for Ghost Bass: a compact one-line text form that rides in the patch XML.
//   "v2 n=<peaks> g=<grit> e=<evolve> p=<phase> f=<0|1> z=<freezeZ> s=<seed> [L=x,z,level,hex ...] | x z h w d r | ..."
// ("v1": a land saved before the islands, read as such: its sound is kept)
// Unknown keys are ignored, missing ones take defaults, everything is clamped on read.
#include "TerrainModel.h"
#include <string>

namespace ghost::terrain
{
std::string toString (const Terrain& t);
bool fromString (const std::string& s, Terrain& out);
}
