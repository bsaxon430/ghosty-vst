#pragma once
// TERRAIN -> features: what a landscape IS, in a few numbers each plug-in maps onto its own engine (GhostBlock's own code).
// Per mountain: where it stands (x = across / frequency, z = depth / time), how tall, how big, how much of a spire (very
// thin and tall) or a plateau (taller than the rings go: a flat-topped column), how rough, how much it merges with others.
// The whole land: how many, the tallest and where, the spread, the balance, the mass, how much merges, how many spires /
// plateaus / knolls / valleys, the jaggedness. Movement: how fast it is being pulled about right now, the EVOLVE drift.
// Pure functions on a copy, no allocation: any thread (the audio thread may call landFeatures() on its own copy).
#include "TerrainModel.h"

namespace ghost::terrain
{
struct MountainFeatures
{
    float x = 0.5f, z = 0.5f;   // where (0..1 across, 0..1 deep)
    float height = 0.0f;        // -1..1.2 as set (valleys below 0)
    float presence = 1.0f;      // how much of it is there at the moment asked for (1 when no moment is given)
    float size = 0.0f;          // the reference's r: radius in island radii, 0.04 (spire) .. 1.5 (mound)
    float spire = 0.0f;         // 0..1 thin and tall
    float plateau = 0.0f;       // 0..1 taller than the rings: a flat top
    float knoll = 0.0f;         // 0..1 small and low
    float rough = 0.0f;         // 0..1 its own grit (plus the land's)
    float merge = 0.0f;         // 0..1 how far it runs into its nearest neighbour
    bool valley = false;
};

struct LandFeatures
{
    int count = 0, valleys = 0;
    float tallest = 0.0f, tallestX = 0.5f, tallestZ = 0.5f;   // the highest mountain (0 when none)
    float centreX = 0.5f, centreZ = 0.5f;                     // the land's centre of mass
    float spread = 0.0f;       // 0 one cluster .. 1 scattered over the whole island
    float balance = 0.5f;      // 0 all on the left .. 1 all on the right (across)
    float symmetry = 1.0f;     // 1 balanced around the middle .. 0 lopsided
    float mass = 0.0f;         // 0..1 how much land there is (height x area)
    float merges = 0.0f;       // 0..1 how much the mountains run together
    float spires = 0.0f, plateaus = 0.0f, knolls = 0.0f;      // soft counts
    float jagged = 0.0f;       // 0..1 the grit of the land
    float meanSize = 0.0f;     // average r (island radii)
    float motion = 0.0f;       // 0..1 how fast it is being changed right now (TerrainEditor::motion())
    float evolve = 0.0f, phase = 0.0f;
    MountainFeatures m[Terrain::kMaxPeaks];
};

// z < 0: the whole landscape; z in 0..1: weighted by what is there at that moment (EVOLVE drift applied)
LandFeatures landFeatures (const Terrain& t, float z = -1.0f, float motion = 0.0f);

// helpers for mappings
inline float lerpf (float a, float b, float t) { return a + (b - a) * t; }
inline float clamp01 (float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
inline float smooth01 (float v) { v = clamp01 (v); return v * v * (3.0f - 2.0f * v); }

// A live meter for the "what this is doing" readout: a short name and how far the land pushes it (0..1).
struct Meter
{
    const char* name = "";     // e.g. "DRIVE" (a string that lives for ever: a literal)
    float value = 0.0f;
};
}
