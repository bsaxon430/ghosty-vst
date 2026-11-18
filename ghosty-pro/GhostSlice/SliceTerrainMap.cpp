#include "SliceTerrainMap.h"
#include <algorithm>

namespace gslice::land
{
using namespace ghost::terrain;

bool LandMod::operator== (const LandMod& o) const
{
    return tone == o.tone && space == o.space && width == o.width && warmth == o.warmth && ensemble == o.ensemble && dream == o.dream && live == o.live && any == o.any;
}

LandMod mapLand (const Terrain& t, float motion)
{
    LandMod m;
    if (t.empty()) return m;
    const auto f = landFeatures (t, -1.0f, motion);
    float maxPlateau = 0, spireThin = 0;
    for (int i = 0; i < f.count; ++i) { maxPlateau = std::max (maxPlateau, f.m[i].plateau); spireThin = std::max (spireThin, f.m[i].spire * clamp01 ((0.1f - f.m[i].size) / 0.06f)); }
    const float spires = clamp01 (f.spires / 3.0f), plateaus = clamp01 (f.plateaus / 1.5f);
    const float lean = (f.balance - 0.5f) * 2.0f;   // -1 all low .. +1 all high
    m.dream = 0.75f * plateaus;
    m.space = 0.35f * f.merges + 0.12f * f.mass;
    m.width = 0.45f * (f.spread - 0.25f);
    m.tone = 0.55f * lean * clamp01 (f.tallest / 0.6f);
    m.warmth = 0.35f * f.mass - 0.2f * spires;
    m.ensemble = 0.08f * (float) std::min (6, f.count - 1);
    auto& L = m.live;
    L.shatter = clamp01 (0.45f * spires + 0.35f * f.jagged);
    L.shatterSize = clamp01 (0.55f - 0.45f * spireThin + 0.2f * f.spread);
    L.reverse = clamp01 (0.7f * f.jagged + 0.2f * f.merges);
    L.shimmer = clamp01 (0.4f * spires + 0.15f * spireThin);
    L.freeze = clamp01 (maxPlateau);
    L.smear = clamp01 (0.25f * plateaus);
    L.drive = clamp01 (0.7f * f.jagged + 0.1f * f.mass);
    L.wobble = clamp01 (0.9f * f.motion + 0.35f * f.evolve);
    L.wobbleHz = 2.5f + 5.5f * f.spread;
    m.any = true;
    // the readout: what is pushed hardest
    std::vector<Meter> all = { { "DREAM", m.dream }, { "SPACE", m.space * 2.0f }, { "STUTTER", L.shatter }, { "SHIMMER", L.shimmer }, { "GRIT", L.drive },
                               { "WIDTH", std::abs (m.width) * 2.0f }, { "TONE", std::abs (m.tone) }, { "CHORUS", m.ensemble * 2.0f }, { "WOBBLE", L.wobble },
                               { "FREEZE", L.freeze * 0.8f } };
    for (auto& a : all) a.value = clamp01 (a.value);
    std::stable_sort (all.begin(), all.end(), [] (const Meter& a, const Meter& b) { return a.value > b.value; });
    for (int i = 0; i < 4; ++i) if (all[(size_t) i].value > 0.02f) m.meters.push_back (all[(size_t) i]);
    return m;
}
}
