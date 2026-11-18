#include "TerrainModel.h"
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <cctype>
#include <vector>

namespace ghost::terrain
{
static constexpr float kTwoPi = 6.283185307f;
static constexpr float kReach = 1.6f, kReach2 = kReach * kReach;   // a mountain ends at i = 1.6 (3.2 widths across; the bell is eased to zero from 1.2)
static constexpr float kWobble = 0.22f;                              // the radius breathes +-22 % with the wobble noise
static constexpr float kFloor = 0.07f, kRelief = 0.03f;             // the island's floor dome and the relief on it
static constexpr float kReachSize = 1.24f, kReachFalloff = 1.05f;  // the lit region: the mountain's own shape, a quarter wider

// ------------------------------------------------------------------------------------------------ fast maths
float fastExp (float x)
{
    x = x < -87.0f ? -87.0f : (x > 0.0f ? 0.0f : x);
    const float t = x * 1.44269504f;
    const float fi = std::floor (t), f = t - fi;
    const float p = 1.0f + f * (0.69314718f + f * (0.24022651f + f * (0.05550411f + f * (0.00961813f + f * (0.00133336f + f * 0.00015404f)))));
    const uint32_t bits = (uint32_t) ((int) fi + 127) << 23;
    float s; std::memcpy (&s, &bits, 4);
    return p * s;
}

float softLimit (float x)
{
    if (x > 4.97f) return 1.0f;
    if (x < -4.97f) return -1.0f;
    const float x2 = x * x;
    const float a = x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)));
    const float b = 135135.0f + x2 * (62370.0f + x2 * (3150.0f + 28.0f * x2));
    return a / b;
}

float limitHeight (float h)
{
    // a single mountain keeps its exact cone (heights up to 1 pass straight through); only where mountains pile up past 1 does
    // the land ease off, to at most 1.3
    const float a = std::abs (h);
    if (a <= 1.0f) return h;
    const float over = 1.0f + 0.3f * softLimit ((a - 1.0f) * 2.0f);
    return h < 0.0f ? -over : over;
}

// ---- the mountain's shape, the reference's ("Moving mountains" / topographically): a Gaussian bell against the normalised
// distance i = r / R (R = 2 widths across, 2 depths along time): h * exp (-3 i^2), eased out to nothing between i = 1.2 and
// 1.6. The radius breathes with a slow noise (+-22 %, see wobbleAt) so the rings wander organically yet stay nested, the way
// the reference's do. Read from a table uniform in i^2 (what the hot loop has).
namespace
{
struct ProfileTable
{
    static constexpr int kN = 1024;
    float v[kN + 2];
    ProfileTable()
    {
        for (int i = 0; i <= kN + 1; ++i)
        {
            const double i2 = kReach2 * (double) i / (double) kN, r = std::sqrt (i2);
            const double ease = r <= 1.2 ? 1.0 : (r >= 1.6 ? 0.0 : 1.0 - smoothstepD ((r - 1.2) / 0.4));
            v[i] = (float) (std::exp (-3.0 * i2) * ease);
        }
        v[kN] = v[kN + 1] = 0.0f;
    }
    static double smoothstepD (double t) { return t <= 0 ? 0 : (t >= 1 ? 1 : t * t * (3 - 2 * t)); }
};
const ProfileTable& profileTable() { static const ProfileTable t; return t; }
}

float profile (float i2)
{
    if (i2 <= 0.0f) return 1.0f;
    if (i2 >= kReach2) return 0.0f;
    const auto& tb = profileTable();
    const float u = i2 * ((float) ProfileTable::kN / kReach2);
    const int i = (int) u;
    const float f = u - (float) i;
    return tb.v[i] + f * (tb.v[i + 1] - tb.v[i]);
}

float profileRadius (float share)
{
    if (share >= 1.0f) return 0.0f;
    if (share <= 0.0f) return kReach;
    return juce::jmin (kReach, std::sqrt (-std::log (share) / 3.0f));
}

// ---- 2D simplex noise (the classic lattice method, our own code), -1..1, seeded
namespace
{
// a small counter-hash generator (32-bit). The reference seeds its lattice and its shuffles with the same arithmetic, so a
// seed gives the same landscape here as there.
struct RefRandom
{
    uint32_t t;
    explicit RefRandom (uint32_t seed) : t (seed) {}
    double next()
    {
        t += 0x6D2B79F5U;
        uint32_t e = t;
        e = (e ^ (e >> 15)) * (e | 1U);
        e ^= e + (e ^ (e >> 7)) * (e | 61U);
        return (double) (e ^ (e >> 14)) / 4294967296.0;
    }
    float between (float lo, float hi) { return lo + (hi - lo) * (float) next(); }
};

struct Simplex
{
    uint8_t perm[512];
    explicit Simplex (uint32_t seed, bool reference = false)
    {
        uint8_t p[256];
        for (int i = 0; i < 256; ++i) p[i] = (uint8_t) i;
        if (reference)
        {
            // the reference's lattice: a seed gives the same ground here as on topographically.vercel.app
            RefRandom rnd (seed);
            for (int i = 255; i > 0; --i)
            {
                const int j = (int) std::floor (rnd.next() * (double) (i + 1));
                std::swap (p[i], p[j]);
            }
        }
        else
        {
            uint32_t st = seed * 0x9E3779B1U + 0x7F4A7C15U;
            for (int i = 255; i > 0; --i)
            {
                st ^= st << 13; st ^= st >> 17; st ^= st << 5;
                const int j = (int) (st % (uint32_t) (i + 1));
                std::swap (p[i], p[j]);
            }
        }
        for (int i = 0; i < 512; ++i) perm[i] = p[i & 255];
    }
    static float corner (int g, float x, float y)
    {
        static const float gx[8] = { 1, -1, 1, -1, 1, -1, 0, 0 }, gy[8] = { 1, 1, -1, -1, 0, 0, 1, -1 };
        float t = 0.5f - x * x - y * y;
        if (t < 0.0f) return 0.0f;
        t *= t;
        return t * t * (gx[g & 7] * x + gy[g & 7] * y);
    }
    float operator() (float x, float y) const
    {
        constexpr float F2 = 0.3660254f, G2 = 0.2113249f;
        const float s = (x + y) * F2;
        const float xi = std::floor (x + s), yi = std::floor (y + s);
        const float t = (xi + yi) * G2;
        const float x0 = x - (xi - t), y0 = y - (yi - t);
        const int i1 = x0 > y0 ? 1 : 0, j1 = 1 - i1;
        const float x1 = x0 - (float) i1 + G2, y1 = y0 - (float) j1 + G2, x2 = x0 - 1.0f + 2.0f * G2, y2 = y0 - 1.0f + 2.0f * G2;
        const int ii = (int) xi & 255, jj = (int) yi & 255;
        return 70.0f * (corner (perm[ii + perm[jj]], x0, y0) + corner (perm[ii + i1 + perm[jj + j1]], x1, y1) + corner (perm[ii + 1 + perm[jj + 1]], x2, y2));
    }
};
const Simplex& simplexFor (uint32_t seed, bool reference = false)
{
    static thread_local uint32_t cachedSeed[2] = { 0xffffffffU, 0xffffffffU };
    static thread_local std::unique_ptr<Simplex> sx[2];
    const int k = reference ? 1 : 0;
    if (sx[k] == nullptr || cachedSeed[k] != seed) { sx[k] = std::make_unique<Simplex> (seed, reference); cachedSeed[k] = seed; }
    return *sx[k];
}

// the reference's ground noise (0..1): four octaves (gain 0.16) warped by themselves, at a scale of 0.93, shaped
float refRelief (const Simplex& n, float u, float v)
{
    constexpr float kScale = 0.93f, kGain = 0.16f, kWarp = 0.6f;
    constexpr float norm = 1.0f + kGain + kGain * kGain + kGain * kGain * kGain;
    auto fbm = [&n] (float x, float y)
    {
        float s = 0.0f, a = 1.0f, f = 1.0f;
        for (int o = 0; o < 4; ++o, a *= kGain, f *= 2.0f) s += a * n (x * f, y * f);
        return s / norm;
    };
    const float x = u * kScale, y = v * kScale;
    const float a = fbm (x + kWarp * fbm (x + 3.1f, y + 7.7f), y + kWarp * fbm (x - 5.3f, y + 1.9f));
    const float s = juce::jlimit (0.0f, 1.0f, 0.5f + 0.8f * a);
    return s * s * (1.6f - 0.6f * s);
}
}

float Terrain::wobbleAt (float x, float z) const
{
    // in the island's own coordinates (-1..1 across the ground), two octaves: the reference's proportions
    // (2: the reference's lattice and ground, its y running towards the viewer, so a seed gives the reference's land)
    const bool ref = version >= 2;
    const auto& n = simplexFor (seed, ref);
    const float u = 2.0f * x - 1.0f, v = ref ? 1.0f - 2.0f * z : 2.0f * z - 1.0f;
    return 0.7f * n (u * 3.1f + 11.3f, v * 3.1f - 4.1f) + 0.3f * n (u * 7.3f - 2.2f, v * 7.3f + 9.4f);
}

float Terrain::reliefAt (float x, float z) const
{
    if (version >= 2) return refRelief (simplexFor (seed, true), 2.0f * x - 1.0f, 1.0f - 2.0f * z);
    // (1) a faint rolling relief over the floor (three octaves of the same noise, 0..1)
    const auto& n = simplexFor (seed + 101);
    const float u = (2.0f * x - 1.0f) * 0.93f, v = (2.0f * z - 1.0f) * 0.93f;
    const float a = (n (u, v) + 0.5f * n (u * 2.0f + 3.1f, v * 2.0f + 7.7f) + 0.25f * n (u * 4.0f - 5.3f, v * 4.0f + 1.9f)) / 1.75f;
    const float s = juce::jlimit (0.0f, 1.0f, 0.5f + 0.8f * a);
    return s * s * (1.6f - 0.6f * s);
}

float Terrain::shoreAt (float x, float z)
{
    const float u = 2.0f * x - 1.0f, v = 2.0f * z - 1.0f, r = std::sqrt (u * u + v * v);
    if (r >= 1.0f) return 0.0f;
    return 1.0f - r * r * (3.0f - 2.0f * r);
}

float Terrain::floorAt (float x, float z, float relief)
{
    // the ground is an island: a gentle dome (kFloor at the centre, nothing at the shore) with the relief on it
    const float u = 2.0f * x - 1.0f, v = 2.0f * z - 1.0f, r = std::sqrt (u * u + v * v);
    if (r >= 1.0f) return 0.0f;
    const float mask = 1.0f - r * r * (3.0f - 2.0f * r);
    return mask * (kFloor + kRelief * relief);
}

void Terrain::shapeGrid (uint32_t seed, int nx, int nz, float x0, float x1, float z0, float z1, float* out, uint8_t version)
{
    Terrain t; t.seed = seed; t.version = version;
    for (int j = 0; j < nz; ++j)
    {
        const float z = nz > 1 ? z0 + (z1 - z0) * (float) j / (float) (nz - 1) : z0;
        for (int i = 0; i < nx; ++i)
        {
            const float x = nx > 1 ? x0 + (x1 - x0) * (float) i / (float) (nx - 1) : x0;
            out[(size_t) (j * nx + i) * 2] = t.wobbleAt (x, z);
            out[(size_t) (j * nx + i) * 2 + 1] = t.reliefAt (x, z);
        }
    }
}

// one mountain's height at (dx, dz) from its centre, with the radius wobble w (-1..1)
static inline float bump (const Peak& p, float dx, float dz, float w)
{
    const float sx = dx / (2.0f * p.width), sz = dz / (2.0f * p.depth), k = 1.0f + kWobble * w;
    return p.height * profile ((sx * sx + sz * sz) * k * k);
}

static inline uint32_t hash (uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return x;
}

static inline float lattice (int ix, int iy, uint32_t seed)
{
    return (float) (hash ((uint32_t) ix * 0x9E3779B1U ^ hash ((uint32_t) iy + seed * 0x85EBCA77U)) & 0xffffff) / (float) 0xffffff;
}

float noise2 (float x, float y, uint32_t seed)
{
    const float fx = std::floor (x), fy = std::floor (y);
    const int ix = (int) fx, iy = (int) fy;
    float tx = x - fx, ty = y - fy;
    tx = tx * tx * (3.0f - 2.0f * tx); ty = ty * ty * (3.0f - 2.0f * ty);
    const float a = lattice (ix, iy, seed), b = lattice (ix + 1, iy, seed), c = lattice (ix, iy + 1, seed), d = lattice (ix + 1, iy + 1, seed);
    const float top = a + tx * (b - a), bot = c + tx * (d - c);
    return top + ty * (bot - top);
}

static inline float gritNoise (float x, float zz, uint32_t seed)
{
    return (noise2 (x * 46.0f, zz * 9.0f, seed) - 0.5f) + 0.5f * (noise2 (x * 113.0f, zz * 21.0f, seed + 7) - 0.5f);
}

// ------------------------------------------------------------------------------------------------ the surface
bool Peak::operator== (const Peak& o) const
{
    return x == o.x && z == o.z && height == o.height && width == o.width && depth == o.depth && rough == o.rough;
}

bool RingLabel::operator== (const RingLabel& o) const
{
    return level == o.level && (level == 0 || (x == o.x && z == o.z && std::strncmp (text, o.text, sizeof (text)) == 0));
}

bool Terrain::sameLand (const Terrain& o) const
{
    if (count != o.count || grit != o.grit || evolve != o.evolve || phase != o.phase || frozen != o.frozen || freezeZ != o.freezeZ || seed != o.seed
        || version != o.version)
        return false;
    for (int i = 0; i < count; ++i) if (peaks[(size_t) i] != o.peaks[(size_t) i]) return false;
    return true;
}

bool Terrain::operator== (const Terrain& o) const
{
    return sameLand (o) && labels == o.labels;
}

int Terrain::labelCount() const
{
    int n = 0;
    for (auto& l : labels) if (l.level != 0) ++n;
    return n;
}

bool Terrain::addLabel (const RingLabel& l)
{
    for (auto& s : labels) if (s.level == 0) { s = l; return true; }
    return false;
}

bool Terrain::add (const Peak& p)
{
    if (count >= kMaxPeaks) return false;
    peaks[(size_t) count++] = p;
    return true;
}

void Terrain::remove (int i)
{
    if (i < 0 || i >= count) return;
    for (int k = i; k + 1 < count; ++k) peaks[(size_t) k] = peaks[(size_t) k + 1];
    --count;
}

Peak Terrain::peakAt (int i, float z) const
{
    Peak p = peaks[(size_t) i];
    if (evolve > 0.0f)
    {
        const float zz = effectiveZ (z);
        const float k = (float) i * 0.37f + (float) (seed % 97) * 0.01f;
        p.x += evolve * 0.07f * std::sin (kTwoPi * (phase + zz * 0.8f + k));
        p.height *= 1.0f + evolve * 0.3f * std::sin (kTwoPi * (phase * 0.7f + zz * 1.3f + k * 1.7f));
        p.width *= 1.0f + evolve * 0.25f * std::sin (kTwoPi * (phase * 1.3f + zz * 0.5f + k * 2.3f));
    }
    return p;
}

float Terrain::smoothHeightAt (float x, float z, int skip) const
{
    if (count == 0) return 0.0f;
    const float zz = effectiveZ (z);
    return smoothHeightShaped (x, z, wobbleAt (x, zz), reliefAt (x, zz), skip);
}

float Terrain::heightShaped (float x, float z, float w, float relief) const
{
    float h = smoothHeightShaped (x, z, w, relief, -1);
    const float r = roughAt (x, z);
    if (r > 0.0f) h += r * gritNoise (x, effectiveZ (z), seed) * ((version >= 2 ? 0.03f : 0.16f) + 0.5f * std::abs (h));
    return limitHeight (h);
}

float Terrain::smoothHeightShaped (float x, float z, float w, float relief, int skip) const
{
    // one landmass: the island's floor, and every mountain piled onto it (a peak pulled up on a flank rises out of the flank)
    const float zz = effectiveZ (z);
    if (count == 0) return 0.0f;
    const float h = floorAt (x, zz, relief);
    float m = 0.0f;
    for (int i = 0; i < count; ++i)
    {
        if (i == skip) continue;
        const Peak p = peakAt (i, z);
        const float dx = x - p.x, dz = zz - p.z;
        if (std::abs (dx) >= 2.0f * kReach * p.width || std::abs (dz) >= 2.0f * kReach * p.depth) continue;
        m += bump (p, dx, dz, w);
    }
    return h + m;   // (the picture holds mountains in by the shore - sample (..., shoreHold) - the sound does not)
}

float Terrain::roughAt (float x, float z) const
{
    const float zz = effectiveZ (z);
    float r = grit;
    for (int i = 0; i < count; ++i)
    {
        const Peak p = peakAt (i, z);
        if (p.rough <= 0.0f) continue;
        const float sx = (x - p.x) / (3.0f * p.width), sz = (zz - p.z) / (2.0f * p.depth);
        const float i2 = sx * sx + sz * sz;
        if (i2 < kReach2) r += p.rough * profile (i2);
    }
    return r < 0.0f ? 0.0f : (r > 1.0f ? 1.0f : r);
}

float Terrain::heightAt (float x, float z) const
{
    float h = smoothHeightAt (x, z);
    const float r = roughAt (x, z);
    if (r > 0.0f) h += r * gritNoise (x, effectiveZ (z), seed) * ((version >= 2 ? 0.03f : 0.16f) + 0.5f * std::abs (h));
    return limitHeight (h);
}

float Terrain::shapeAt (int i, float x, float z) const
{
    if (i < 0 || i >= count) return 0.0f;
    const float zz = effectiveZ (z);
    const Peak p = peakAt (i, z);
    const float dx = x - p.x, dz = zz - p.z, w = wobbleAt (x, zz);
    const float sx = dx / (2.0f * p.width), sz = dz / (2.0f * p.depth), k = 1.0f + kWobble * w;
    return profile ((sx * sx + sz * sz) * k * k);
}

void Terrain::noiseGrid (uint32_t seed, int nx, int nz, float x0, float x1, float z0, float z1, bool frozen, float freezeZ, float* out)
{
    for (int j = 0; j < nz; ++j)
    {
        const float z = nz > 1 ? z0 + (z1 - z0) * (float) j / (float) (nz - 1) : z0;
        const float zz = frozen ? freezeZ : z;
        for (int i = 0; i < nx; ++i)
        {
            const float x = nx > 1 ? x0 + (x1 - x0) * (float) i / (float) (nx - 1) : x0;
            out[(size_t) (j * nx + i)] = gritNoise (x, zz, seed);
        }
    }
}

void Terrain::sample (int nx, int nz, float x0, float x1, float z0, float z1, float* heights, int8_t* owner, const float* noise, int* rowSpan,
                      int focus, float* focusTint, const float* shape, bool floorWhenEmpty, bool shoreHold) const
{
    constexpr int kStackNx = 640;
    const float xStep = nx > 1 ? (x1 - x0) / (float) (nx - 1) : 0.0f, zStep = nz > 1 ? (z1 - z0) / (float) (nz - 1) : 0.0f;
    // the squared x distance (in R = 2 widths) of every column to every peak, computed once when the peaks do not drift (EVOLVE off)
    static thread_local float sx2[kMaxPeaks][kStackNx];
    const bool cacheX = evolve <= 0.0f && nx <= kStackNx;
    bool anyRough = grit > 0.0f;
    for (int i = 0; i < count; ++i) anyRough = anyRough || peaks[(size_t) i].rough > 0.0f;
    if (cacheX)
        for (int p = 0; p < count; ++p)
        {
            const Peak& pk = peaks[(size_t) p];
            for (int i = 0; i < nx; ++i) { const float dx = (x0 + xStep * (float) i - pk.x) / (2.0f * pk.width); sx2[p][i] = dx * dx; }
        }
    const bool withFloor = count > 0 || floorWhenEmpty;
    // FREEZE: every row is the landscape at freezeZ, so the shape grid's row for that moment serves all rows
    const int jFreeze = zStep > 0 ? juce::jlimit (0, nz - 1, juce::roundToInt ((freezeZ - z0) / zStep)) : 0;
    float big[kStackNx], hold[kStackNx];
    const bool held = shoreHold && version >= 2;
    for (int j = 0; j < nz; ++j)
    {
        float* row = heights + (size_t) j * (size_t) nx;
        int8_t* own = owner != nullptr ? owner + (size_t) j * (size_t) nx : nullptr;
        float* tint = focusTint != nullptr ? focusTint + (size_t) j * (size_t) nx : nullptr;
        const float* shp = shape != nullptr ? shape + (size_t) (frozen ? jFreeze : j) * (size_t) nx * 2 : nullptr;
        const float z = z0 + zStep * (float) j, zz = effectiveZ (z);
        if (frozen && j > 0)   // every moment is the same landscape
        {
            std::memcpy (row, heights, sizeof (float) * (size_t) nx);
            if (own != nullptr) std::memcpy (own, owner, (size_t) nx);
            if (tint != nullptr) std::memcpy (tint, focusTint, sizeof (float) * (size_t) nx);
            if (rowSpan != nullptr) { rowSpan[j * 2] = rowSpan[0]; rowSpan[j * 2 + 1] = rowSpan[1]; }
            continue;
        }
        const int nn = nx < kStackNx ? nx : kStackNx;
        // the floor first
        int spanLo = nx, spanHi = -1;
        for (int i = 0; i < nn; ++i)
        {
            big[i] = 0.0f;
            if (withFloor)
            {
                const float x = x0 + xStep * (float) i;
                const float rel = shp != nullptr ? shp[i * 2 + 1] : reliefAt (x, zz);
                row[i] = floorAt (x, zz, rel);
                if (held) hold[i] = shoreAt (x, zz);
                if (row[i] > 0.0f) { if (i < spanLo) spanLo = i; spanHi = i; }
            }
            else { row[i] = 0.0f; if (held) hold[i] = shoreAt (x0 + xStep * (float) i, zz); }
            if (tint != nullptr) tint[i] = 0.0f;
        }
        if (own != nullptr) std::memset (own, -1, (size_t) nx);
        for (int p = 0; p < count; ++p)
        {
            const Peak pk = peakAt (p, z);
            if (std::abs (pk.height) < 1e-5f) continue;
            const float sz = (zz - pk.z) / (2.0f * pk.depth), ez = sz * sz;
            // the widest the bell can be with the wobble: i^2 (1 - 0.22)^2 ... < 2.56 -> plain r^2 < 2.56 / 0.78^2
            const float maxR2 = kReach2 / ((1.0f - kWobble) * (1.0f - kWobble));
            if (ez >= maxR2) continue;
            const float reach = std::sqrt (maxR2 - ez) * 2.0f * pk.width;
            int i0 = xStep > 0 ? (int) std::floor ((pk.x - reach - x0) / xStep) : 0, i1 = xStep > 0 ? (int) std::ceil ((pk.x + reach - x0) / xStep) : nx - 1;
            i0 = i0 < 0 ? 0 : i0; i1 = i1 > nn - 1 ? nn - 1 : i1;
            if (i0 < spanLo) spanLo = i0;
            if (i1 > spanHi) spanHi = i1;
            const bool isFocus = tint != nullptr && p == focus;
            for (int i = i0; i <= i1; ++i)
            {
                float ex;
                if (cacheX) ex = sx2[p][i];
                else { const float dx = (x0 + xStep * (float) i - pk.x) / (2.0f * pk.width); ex = dx * dx; }
                const float r2 = ex + ez;
                if (r2 >= maxR2) continue;
                const float x = x0 + xStep * (float) i;
                const float w = shp != nullptr ? shp[i * 2] : wobbleAt (x, zz);
                const float k = 1.0f + kWobble * w, i2 = r2 * k * k;
                if (i2 >= kReach2) { if (isFocus) { const float t = profile (i2 / (kReachSize * kReachSize)); if (t > 0.0f) tint[i] = std::pow (t, kReachFalloff); } continue; }
                const float c = pk.height * profile (i2), a = std::abs (c);
                row[i] += held ? c * hold[i] : c;
                if (own != nullptr && a > big[i]) { big[i] = a; own[i] = (int8_t) p; }   // the land here belongs to whoever gives it most
                if (isFocus) tint[i] = std::pow (profile (i2 / (kReachSize * kReachSize)), kReachFalloff);   // the lit region: this mountain's own shape, a quarter wider
            }
        }
        if (anyRough)
        {
            float* rough = big;   // reuse: the peaks are summed up
            for (int i = 0; i < nn; ++i) rough[i] = grit;
            for (int p = 0; p < count; ++p)
            {
                const Peak pk = peakAt (p, z);
                if (pk.rough <= 0.0f) continue;
                const float sz = (zz - pk.z) / (2.0f * pk.depth), ez = sz * sz;
                if (ez >= kReach2) continue;
                const float wr = pk.width * 3.0f, reach = std::sqrt (kReach2 - ez) * wr;
                int i0 = xStep > 0 ? (int) std::floor ((pk.x - reach - x0) / xStep) : 0, i1 = xStep > 0 ? (int) std::ceil ((pk.x + reach - x0) / xStep) : nx - 1;
                i0 = i0 < 0 ? 0 : i0; i1 = i1 > nn - 1 ? nn - 1 : i1;
                if (i0 < spanLo) spanLo = i0;
                if (i1 > spanHi) spanHi = i1;
                for (int i = i0; i <= i1; ++i)
                {
                    const float dx = (x0 + xStep * (float) i - pk.x) / wr, i2 = dx * dx + ez;
                    if (i2 < kReach2) rough[i] += pk.rough * profile (i2);
                }
            }
            if (grit > 0.0f) { spanLo = 0; spanHi = nn - 1; }
            for (int i = 0; i < nn; ++i)
            {
                const float r = rough[i] < 0.0f ? 0.0f : (rough[i] > 1.0f ? 1.0f : rough[i]);
                if (r > 0.0f)
                {
                    const float n = noise != nullptr ? noise[(size_t) j * (size_t) nx + (size_t) i] : gritNoise (x0 + xStep * (float) i, zz, seed);
                    row[i] += r * n * ((version >= 2 ? 0.03f : 0.16f) + 0.5f * std::abs (row[i]));   // (2: grit roughens the mountains, not the flat sea floor)
                }
            }
        }
        for (int i = spanLo; i <= spanHi; ++i) row[i] = limitHeight (row[i]);
        if (rowSpan != nullptr) { rowSpan[j * 2] = spanHi >= spanLo ? spanLo : -1; rowSpan[j * 2 + 1] = spanHi >= spanLo ? spanHi : -1; }
    }
}

static inline float clampf (float v, float lo, float hi, float def) { return std::isfinite (v) ? (v < lo ? lo : (v > hi ? hi : v)) : def; }

void Terrain::clampAll()
{
    if (count < 0) count = 0;
    if (count > kMaxPeaks) count = kMaxPeaks;
    for (int i = 0; i < count; ++i)
    {
        auto& p = peaks[(size_t) i];
        p.x = clampf (p.x, 0.0f, 1.0f, 0.5f);
        p.z = clampf (p.z, 0.0f, 1.0f, 0.5f);
        p.height = clampf (p.height, kMinHeight, kMaxHeight, 0.0f);
        p.width = clampf (p.width, kMinWidth, kMaxWidth, 0.05f);
        p.depth = clampf (p.depth, kMinDepth, kMaxDepth, 0.14f);
        p.rough = clampf (p.rough, 0.0f, 1.0f, 0.0f);
    }
    grit = clampf (grit, 0.0f, 1.0f, 0.0f);
    evolve = clampf (evolve, 0.0f, 1.0f, 0.0f);
    phase = std::isfinite (phase) ? phase - std::floor (phase) : 0.0f;
    freezeZ = clampf (freezeZ, 0.0f, 1.0f, 0.5f);
    if (version < 1 || version > kVersion) version = kVersion;
    for (auto& l : labels)
    {
        if (l.level < -25 || l.level > 25) l.level = 0;
        if (l.level == 0) { l = RingLabel(); continue; }
        l.x = clampf (l.x, -0.2f, 1.2f, 0.5f); l.z = clampf (l.z, -0.2f, 1.2f, 0.5f);
        l.text[sizeof (l.text) - 1] = 0;
    }
}

// ------------------------------------------------------------------------------------------------ starting points, edits
Terrain flatTerrain() { return {}; }

Terrain defaultTerrain()
{
    // the reference's opening land: one mountain in the middle (r 0.38 of the island, 0.55 tall) on the ground of seed 7
    Terrain t;
    t.seed = 7;
    t.add ({ 0.50f, 0.50f, 0.55f, 0.095f, 0.095f, 0.0f });
    return t;
}

Terrain randomTerrain (uint32_t seed, int count, bool wild)
{
    // The reference's shuffle (re-derived): the first mountain big and near the middle, the others smaller and further out,
    // each lower than the last, each placed (the best of 40 throws) so they do not pile up. Its numbers come from the same
    // generator as the reference's, so a seed and a count give its landscape. `wild` (ours) turns some of them into spires,
    // plateaus and knolls and scatters them wider - still safe: heights within 0 .. 1.2, nothing on top of anything else.
    RefRandom r (seed ^ 1597463007U);
    Terrain t;
    t.seed = seed;
    const int n = juce::jlimit (1, Terrain::kMaxPeaks, count > 0 ? count : (r.next() < 0.5 ? 2 : 3));
    const float decay = 2.0f / (float) juce::jmax (2, n - 1), shrink = n > 3 ? 0.8f : 1.0f;
    float h = r.between (0.5f, 0.65f);
    struct Spot { float x, y, rad; };
    std::vector<Spot> placed;
    for (int i = 1; i <= n; ++i)
    {
        float rad = i == 1 ? r.between (0.3f, 0.38f) : r.between (0.18f, 0.28f) * shrink;   // in island radii
        Spot best { 0, 0, rad }; float bestGap = -1e9f;
        for (int k = 0; k < 40; ++k)
        {
            const float ang = r.between (0.0f, juce::MathConstants<float>::twoPi);
            const float dist = i == 1 ? r.between (0.0f, 0.2f) : (wild ? r.between (0.25f, 0.72f) : r.between (0.3f, 0.6f));
            const Spot s { std::cos (ang) * dist, std::sin (ang) * dist, rad };
            float gap = 1.0f;
            for (auto& q : placed) gap = juce::jmin (gap, std::hypot (q.x - s.x, q.y - s.y) / (0.8f * (q.rad + s.rad)) - 1.0f);
            if (gap > bestGap) { bestGap = gap; best = s; }
            if (gap >= 0.0f) break;
        }
        placed.push_back (best);
        Peak p;
        p.x = 0.5f + 0.5f * best.x; p.z = 0.5f - 0.5f * best.y;   // (the reference's y runs towards the viewer)
        p.height = h; p.width = p.depth = rad * 0.25f; p.rough = 0.0f;
        if (wild && i > 1)
        {
            const double kind = r.next();
            if (kind < 0.3) { p.width = p.depth = r.between (0.04f, 0.075f) * 0.25f; p.height = r.between (0.75f, 1.2f); }        // a spire
            else if (kind < 0.45) { p.width = p.depth = r.between (0.16f, 0.24f) * 0.25f; p.height = r.between (1.12f, 1.2f); }  // a plateau
            else if (kind < 0.65) { p.width = p.depth = r.between (0.09f, 0.14f) * 0.25f; p.height = r.between (0.18f, 0.32f); } // a knoll
            else if (kind < 0.72) p.rough = r.between (0.25f, 0.6f);                                                              // jagged
        }
        t.add (p);
        h *= std::pow (r.between (0.5f, 0.7f), decay);
    }
    t.clampAll();
    return t;
}

Terrain shuffleTerrain (const Terrain& now, uint32_t seed, bool wild, int most)
{
    const int n = juce::jmax (now.size(), juce::jmin (juce::jmin (most, Terrain::kMaxPeaks), now.size() + 2));
    Terrain t = randomTerrain (seed, juce::jmax (1, n), wild);
    t.grit = now.grit; t.evolve = now.evolve; t.phase = now.phase; t.frozen = now.frozen; t.freezeZ = now.freezeZ;
    t.seed = now.seed;   // the ground stays (the reference's shuffle moves the mountains, not the ground)
    return t;
}

void smooth (Terrain& t, float amount)
{
    amount = juce::jlimit (-1.0f, 1.0f, amount);
    if (amount >= 0.0f)
    {
        const float k = 1.0f - 0.6f * amount;
        t.grit *= k;
        for (auto& p : t) { p.rough *= k; p.width *= 1.0f + 0.35f * amount; p.depth *= 1.0f + 0.2f * amount; p.height *= 1.0f - 0.12f * amount; }
    }
    else
    {
        const float k = -amount;
        t.grit = juce::jmin (1.0f, t.grit + 0.25f * k);
        for (auto& p : t) { p.rough = juce::jmin (1.0f, p.rough + 0.3f * k); p.width *= 1.0f - 0.2f * k; }
    }
    t.clampAll();
}

void advance (Terrain& t, float seconds)
{
    if (t.evolve <= 0.0f) return;
    t.phase += seconds / kEvolveSeconds;
    t.phase -= std::floor (t.phase);
}

Terrain breed (const Terrain& a, const Terrain& b, float lean, uint32_t seed)
{
    // each peak of the child comes from one parent (the nearer one in frequency wins), pulled toward the other's nearest
    // peak by `lean`; the global character (grit, evolve) is blended
    juce::Random r ((juce::int64) hash (seed * 2654435761U + 17));
    lean = juce::jlimit (0.0f, 1.0f, lean);
    Terrain c;
    c.seed = r.nextBool() ? a.seed : b.seed;
    c.grit = a.grit + lean * (b.grit - a.grit);
    c.evolve = a.evolve + lean * (b.evolve - a.evolve);
    c.phase = a.phase;
    auto nearestIn = [] (const Terrain& t, const Peak& p) -> const Peak*
    {
        const Peak* best = nullptr; float bd = 1e9f;
        for (auto& q : t) { const float d = std::abs (q.x - p.x) + 0.3f * std::abs (q.z - p.z); if (d < bd) { bd = d; best = &q; } }
        return best;
    };
    auto mix = [] (const Peak& p, const Peak& q, float k)
    {
        Peak o;
        o.x = p.x + k * (q.x - p.x); o.z = p.z + k * (q.z - p.z); o.height = p.height + k * (q.height - p.height);
        o.width = std::exp (std::log (p.width) + k * (std::log (q.width) - std::log (p.width)));
        o.depth = p.depth + k * (q.depth - p.depth); o.rough = p.rough + k * (q.rough - p.rough);
        return o;
    };
    for (auto& p : a)
        if (r.nextFloat() >= lean * 0.8f)
        {
            const Peak* q = nearestIn (b, p);
            c.add (q != nullptr ? mix (p, *q, lean * 0.5f) : p);
        }
    for (auto& q : b)
        if (c.size() < Terrain::kMaxPeaks && r.nextFloat() < 0.2f + lean * 0.8f)
        {
            const Peak* p = nearestIn (a, q);
            c.add (p != nullptr ? mix (q, *p, (1.0f - lean) * 0.5f) : q);
        }
    if (c.empty()) { if (! a.empty()) c.add (a[0]); else c = b; }
    c.clampAll();
    return c;
}

static const char* const kShapeNames[] = { "SPIRE FOREST", "VOLCANO", "PLATEAU", "ARCHIPELAGO", "TWIN PEAKS", "RIDGE", "CANYON", "LONE SPIRE", "BADLANDS", "GHOST" };
int shapePresetCount() { return (int) (sizeof (kShapeNames) / sizeof (kShapeNames[0])); }
const char* shapePresetName (int i) { return i >= 0 && i < shapePresetCount() ? kShapeNames[i] : ""; }

Terrain shapePreset (const char* nameIn, uint32_t seed)
{
    RefRandom r (seed * 977u + 13u);
    Terrain t;
    t.seed = 7;
    auto add = [&] (float x, float z, float h, float rad, float rough = 0.0f) { Peak p; p.x = x; p.z = z; p.height = h; p.width = p.depth = rad * 0.25f; p.rough = rough; t.add (p); };
    auto onIsland = [&] (float maxR, float& x, float& z) { const float a = r.between (0, 6.2831853f), d = maxR * std::sqrt ((float) r.next()); x = 0.5f + 0.5f * d * std::cos (a); z = 0.5f + 0.5f * d * std::sin (a); };
    std::string n (nameIn != nullptr ? nameIn : "");
    for (auto& c : n) c = (char) std::toupper ((unsigned char) c);
    if (n == "SPIRE FOREST")
    {
        add (0.5f, 0.5f, 0.35f, 0.45f);
        for (int i = 0; i < 9; ++i) { float x, z; onIsland (0.62f, x, z); add (x, z, r.between (0.85f, 1.2f), r.between (0.04f, 0.065f)); }
    }
    else if (n == "VOLCANO")
    {
        add (0.5f, 0.5f, 1.2f, 0.42f);
        add (0.5f, 0.5f, -0.75f, 0.12f);                 // the crater
        add (0.66f, 0.38f, 0.22f, 0.14f);
    }
    else if (n == "PLATEAU")
    {
        add (0.47f, 0.52f, 1.2f, 0.3f); add (0.47f, 0.52f, 1.2f, 0.22f);   // stacked: a broad flat top on steep sides
        add (0.72f, 0.4f, 0.3f, 0.14f);
    }
    else if (n == "ARCHIPELAGO")
    {
        for (int i = 0; i < 11; ++i) { float x, z; onIsland (0.68f, x, z); add (x, z, r.between (0.16f, 0.36f), r.between (0.09f, 0.17f)); }
    }
    else if (n == "TWIN PEAKS")
    {
        add (0.36f, 0.5f, 0.95f, 0.26f); add (0.64f, 0.5f, 0.9f, 0.24f);
    }
    else if (n == "RIDGE")
    {
        for (int i = 0; i < 6; ++i) { const float u = (float) i / 5.0f; add (0.22f + 0.56f * u, 0.36f + 0.28f * u, 0.55f + 0.35f * std::sin (u * 3.14159f), 0.2f); }
    }
    else if (n == "CANYON")
    {
        add (0.5f, 0.5f, 0.6f, 0.62f);
        for (int i = 0; i < 4; ++i) { const float u = (float) i / 3.0f; add (0.3f + 0.4f * u, 0.5f + 0.06f * std::sin (u * 6.0f), -0.55f, 0.13f); }
    }
    else if (n == "LONE SPIRE")
    {
        add (0.5f, 0.5f, 1.2f, 0.045f); add (0.5f, 0.5f, 0.25f, 0.3f);
    }
    else if (n == "BADLANDS")
    {
        for (int i = 0; i < 9; ++i) { float x, z; onIsland (0.6f, x, z); add (x, z, r.between (0.3f, 0.75f), r.between (0.1f, 0.22f), r.between (0.4f, 0.9f)); }
        t.grit = 0.35f;
    }
    else if (n == "GHOST")
    {
        // a ghost seen from above: a round head, a body, three scallops at the hem, two eye pits
        add (0.5f, 0.62f, 0.9f, 0.3f);
        add (0.5f, 0.45f, 0.75f, 0.28f);
        for (int i = 0; i < 3; ++i) add (0.36f + 0.14f * (float) i, 0.28f, 0.6f, 0.13f);
        add (0.43f, 0.66f, -0.6f, 0.06f); add (0.57f, 0.66f, -0.6f, 0.06f);
    }
    else return defaultTerrain();
    t.clampAll();
    return t;
}

int nearestPeak (const Terrain& t, float x, float z, float maxDist)
{
    int best = -1; float bd = maxDist;
    for (int i = 0; i < t.size(); ++i)
    {
        const auto& p = t[i];
        const float d = std::hypot (p.x - x, (p.z - z) * 0.6f);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}
}
