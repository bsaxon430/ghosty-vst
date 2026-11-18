#pragma once
// TERRAIN: a landscape you shape with the mouse that IS the control surface (GhostBlock's own code; closed).
// Shared by Ghost Lab (offline spectral sculpting), Ghost Slice (TERRAIN tab) and, as a copy, Ghost Bass (realtime).
//
// The model, in plain words (it follows Brandon's reference, the "Moving mountains" page, measured and re-derived; none of its
// code is used). The ground is an island with a gentle floor; every mountain is a Gaussian bell whose radius breathes with a
// slow noise, piled onto what already stands there (the land is the SUM, so a peak pulled up on a flank rises out of the
// flank and its rings run on into the flank's rings); valleys carve into it. A mountain's lit region is its own shape, a
// quarter wider.
//   x      across  = frequency (log scale, mapped by TerrainMapping.h)
//   z      depth   = time (across a note / a take / a sound). FREEZE holds one moment for the whole time.
//   height         = gain / resonance (mountains boost and ring, valleys cut)
//   width          = bandwidth (Q): a narrow peak is a sharp resonance, a wide one a broad lift (a plateau)
//   depth          = how long a peak lasts along the time axis
//   rough / grit   = saturation and harmonic complexity (the terrain gets jagged)
//   EVOLVE         = slow automatic drift of the peaks (a phase that advances while it runs)
//
// Everything here is plain data + pure functions: no GUI, no heap. A Terrain is a small POD (at most 8 peaks) that can be
// copied to the audio thread as it is; heightAt() / sample() never allocate.
#include "TerrainStd.h"
#include <array>
#include <cstdint>

namespace ghost::terrain
{
struct Peak
{
    float x = 0.5f;        // 0..1 across (frequency)
    float z = 0.5f;        // 0..1 depth (time)
    float height = 0.6f;   // -1..1 (negative = a valley)
    float width = 0.12f;   // kMinWidth..kMaxWidth (x units; the bell's radius R is 2 widths, it is eased out to nothing by 3.2)
    float depth = 0.12f;   // kMinDepth..kMaxDepth (z units, the same way along time; large = lasts the whole time)
    float rough = 0.0f;    // 0..1 local grit
    bool operator== (const Peak& o) const;
    float r() const { return 4.0f * width; }   // the reference's size: the bell's radius in island radii (the width tag shows it)
    bool operator!= (const Peak& o) const { return ! (*this == o); }
};

// sizes: the reference's r runs 0.04 (a thin spire) .. 1.5 (a broad mound) island radii; width = r / 4
constexpr float kMinWidth = 0.01f, kMaxWidth = 0.375f, kMinDepth = 0.01f, kMaxDepth = 2.0f;
// heights: the reference's 0 .. 1.2 (rings stop at 1.0, so a mountain taller than 1 has a flat top: a plateau / column);
// below 0 is ours (a valley: a cut)
constexpr float kMinHeight = -1.0f, kMaxHeight = 1.2f;

// A ring label (the reference's TAG tool): text written on the contour ring of `level` (1..25) nearest to (x, z).
struct RingLabel
{
    float x = 0.5f, z = 0.5f;
    int8_t level = 0;          // 0 = unused
    char text[15] {};          // UTF-8, zero-terminated
    bool operator== (const RingLabel& o) const;
    bool operator!= (const RingLabel& o) const { return ! (*this == o); }
};

struct Terrain
{
    static constexpr int kMaxPeaks = 16;
    static constexpr int kMaxLabels = 8;
    // 1 = the first TERRAIN (sessions saved before the islands: its own noise, mountains not held in by the shore);
    // 2 = the reference's islands (its noise, every mountain held in by the island's shore). New land is 2.
    static constexpr uint8_t kVersion = 2;
    uint8_t version = kVersion;
    std::array<RingLabel, kMaxLabels> labels {};
    std::array<Peak, kMaxPeaks> peaks {};
    int count = 0;
    float grit = 0.0f;           // 0..1 everywhere
    float evolve = 0.0f;         // 0..1 how far peaks drift
    float phase = 0.0f;          // 0..1 the drift's position (advance() moves it)
    bool frozen = false;         // FREEZE: the landscape is the same at every moment (uses freezeZ)
    float freezeZ = 0.5f;
    uint32_t seed = 1;           // the roughness pattern

    // ---- the peaks as a small container
    int size() const { return count; }
    bool empty() const { return count == 0; }
    Peak& operator[] (int i) { return peaks[(size_t) i]; }
    const Peak& operator[] (int i) const { return peaks[(size_t) i]; }
    bool add (const Peak& p);              // false when full
    void remove (int i);
    void clear() { count = 0; }
    Peak* begin() { return peaks.data(); }
    Peak* end() { return peaks.data() + count; }
    const Peak* begin() const { return peaks.data(); }
    const Peak* end() const { return peaks.data() + count; }

    bool operator== (const Terrain& o) const;
    bool sameLand (const Terrain& o) const;      // equal apart from the ring labels (what the sound hears)
    int labelCount() const;
    bool addLabel (const RingLabel& l);
    bool operator!= (const Terrain& o) const { return ! (*this == o); }

    // the surface: -1..1 for a single mountain, up to +-1.3 where mountains pile up (limitHeight). Safe from any thread on a copy.
    float heightAt (float x, float z) const;
    // the smooth part only (no grit; `skip` leaves one mountain out: the land it stands on), and the local roughness 0..1
    float smoothHeightAt (float x, float z, int skip = -1) const;
    float roughAt (float x, float z) const;
    // the same with the shape noise handed in (a view interpolates its shapeGrid(): no noise evaluated, ~10x cheaper)
    float heightShaped (float x, float z, float wobble, float relief) const;
    float smoothHeightShaped (float x, float z, float wobble, float relief, int skip = -1) const;
    // mountain i's own shape at a point (1 at its summit, 0 past its foot), the radius wobble included
    float shapeAt (int i, float x, float z) const;
    // the slow radius wobble (-1..1) and the floor's relief (0..1) at a point, repeatable per seed
    float wobbleAt (float x, float z) const;
    float reliefAt (float x, float z) const;
    // the island's floor at a point given its relief there (a gentle dome, nothing at the shore)
    static float floorAt (float x, float z, float relief);
    // the shore's hold at a point: 1 in the middle of the island, 0 at the shore (smoothstep on the radius)
    static float shoreAt (float x, float z);
    // peak i as it is at depth z (EVOLVE drift applied)
    Peak peakAt (int i, float z) const;
    float effectiveZ (float z) const { return frozen ? freezeZ : z; }

    // The whole surface on a grid, fast (the view, the offline render): heights[j * nx + i] for x in x0..x1, z in z0..z1.
    // `owner` (optional, same size) gets the index of the peak that dominates each sample (-1 = flat ground).
    // Same numbers as heightAt(); `noise` is the grit pattern from noiseGrid() for this grid (nullptr = computed here).
    // `rowSpan` (optional, 2 ints per row) gets the first and last column that is not flat ground on each row (-1, -1 = none).
    // With `focus` >= 0 and `focusTint` (same size as heights), focusTint gets how much each point belongs to that mountain
    // (its own shape, a quarter wider: 1 at its summit, 0 past its foot) - the lit region. `shape` is the shapeGrid() for
    // this grid (wobble and relief per point; nullptr = computed here, slow). `floorWhenEmpty` draws the island's floor even
    // with no mountains (the view; the sound leaves an empty land flat).
    void sample (int nx, int nz, float x0, float x1, float z0, float z1, float* heights, int8_t* owner = nullptr,
                 const float* noise = nullptr, int* rowSpan = nullptr, int focus = -1, float* focusTint = nullptr,
                 const float* shape = nullptr, bool floorWhenEmpty = false, bool shoreHold = false) const;
    // (shoreHold: the reference's picture - every mountain held in by the island's shore, one near the edge sinks into the
    // sea. The view draws it so; the sound leaves mountains whole wherever they stand, so a peak at a far frequency or at
    // the very start of a sound is not lost.)
    // the grit pattern for sample(): depends only on the seed and the grid, so a view caches it
    static void noiseGrid (uint32_t seed, int nx, int nz, float x0, float x1, float z0, float z1, bool frozen, float freezeZ, float* out);
    // the radius wobble and the relief for sample() (2 floats per point), likewise cached per seed and grid
    static void shapeGrid (uint32_t seed, int nx, int nz, float x0, float x1, float z0, float z1, float* out, uint8_t version = kVersion);

    void clampAll();
};

// ---- starting points and edits (all deterministic; seeds make RANDOMIZE / EVOLVE repeatable)
Terrain defaultTerrain();                                        // two soft mountains and a valley
Terrain flatTerrain();                                           // RESET
// RANDOMIZE. count < 0: the reference's 2 or 3. wild: some of the mountains become spires (very thin, very tall), plateaus
// (taller than 1: flat-topped columns) or little knolls, scattered over the whole island - musically safe (heights capped)
Terrain randomTerrain (uint32_t seed, int count = -1, bool wild = false);
// the reference's shuffle button: as many mountains as now plus two (at least the same number, at most `most`), all new
Terrain shuffleTerrain (const Terrain& now, uint32_t seed, bool wild = true, int most = 6);
void smooth (Terrain& t, float amount);                          // SMOOTH (> 0) / ROUGHEN (< 0), amount -1..1
void advance (Terrain& t, float seconds);                        // EVOLVE: move the drift phase (slow: ~40 s a cycle)
constexpr float kEvolveSeconds = 40.0f;                          // one drift cycle
Terrain breed (const Terrain& a, const Terrain& b, float lean, uint32_t seed);   // two terrains make a child
// SHAPE PRESETS: landscapes of one character each, the same in every plug-in (each maps them onto its own sound).
// Names: SPIRE FOREST, VOLCANO, PLATEAU, ARCHIPELAGO, TWIN PEAKS, RIDGE, CANYON, LONE SPIRE, BADLANDS, GHOST
int shapePresetCount();
const char* shapePresetName (int i);                   // "SPIRE FOREST" ...
Terrain shapePreset (const char* name, uint32_t seed = 1);
int nearestPeak (const Terrain& t, float x, float z, float maxDist);              // -1 if none within maxDist

// value noise used for the grit (0..1), repeatable per seed
float noise2 (float x, float y, uint32_t seed);
// the fast maths every part of TERRAIN shares (the picture and the sound agree to ~1e-6)
float fastExp (float x);        // x <= 0 (clamped), ~2e-7 relative error
float softLimit (float x);      // tanh, ~1e-7 absolute error
float limitHeight (float h);    // the land's ceiling: identity up to |1|, then a gentle knee to at most 1.3 (piled-up mountains)
float profile (float i2);       // a mountain's shape (1 at its summit, 0 from i = 1.6) against the squared normalised distance i^2 = (dx / 2w)^2 + (dz / 2d)^2
float profileRadius (float share);   // the inverse: how far out (in i, i.e. units of 2 widths) a mountain has fallen to this share of its height
}
