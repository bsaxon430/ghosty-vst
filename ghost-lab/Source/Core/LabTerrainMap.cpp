#include "LabTerrainMap.h"
#include <algorithm>

namespace glab
{
using namespace ghost::terrain;

LabLand mapLabLand (const Terrain& t, float motion, bool partner)
{
    LabLand L;
    if (t.empty()) return L;
    const auto f = landFeatures (t, -1.0f, motion);
    float maxPlateau = 0, plateauZ = 0.5f, thin = 0, valleyDepth = 0, sizeSum = 0;
    for (int i = 0; i < f.count; ++i)
    {
        const auto& m = f.m[i];
        if (m.plateau > maxPlateau) { maxPlateau = m.plateau; plateauZ = m.z; }
        thin = std::max (thin, m.spire * clamp01 ((0.1f - m.size) / 0.06f));
        if (m.valley) valleyDepth = std::max (valleyDepth, -m.height);
        sizeSum += m.size;
    }
    const float spires = clamp01 (f.spires / 3.0f), plateaus = clamp01 (f.plateaus / 1.5f), lean = (f.balance - 0.5f) * 2.0f;
    auto step = [&] (Op op, std::initializer_list<std::pair<const char*, float>> vals, uint32_t seed)
    {
        Step s; s.op = op; s.seed = seed;
        for (auto& [k, v] : vals) s.values[k] = v;
        L.steps.push_back (s);
    };
    const uint32_t seed = t.seed * 31u + (uint32_t) f.count;
    // in this order: hold, blur, scatter, break, cross
    if (maxPlateau > 0.55f) step (Op::Freeze, { { "where", plateauZ }, { "hold", 0.8f + 2.5f * maxPlateau } }, seed + 1);
    if (plateaus > 0.25f || f.merges > 0.45f) step (Op::Smear, { { "amount", clamp01 (0.35f * plateaus + 0.55f * f.merges) }, { "shimmer", clamp01 (0.2f + 0.6f * spires) } }, seed + 2);
    if (f.jagged > 0.12f) step (Op::GrainCloud, { { "density", 0.3f + 0.6f * f.jagged }, { "size", 25.0f + 220.0f * clamp01 (f.meanSize / 0.5f) }, { "scatter", clamp01 (0.3f + 0.7f * f.jagged) },
                                                  { "stretch", 1.0f + 0.8f * f.spread }, { "pitch", 7.0f * spires } }, seed + 3);
    if (spires > 0.15f) step (Op::Shatter, { { "amount", clamp01 (0.25f + 0.55f * spires) }, { "scatter", 0.4f + 2.2f * f.spread } }, seed + 4);
    if (std::abs (lean) > 0.35f && f.tallest > 0.4f) step (Op::Twist, { { "bend", juce::jlimit (-1.0f, 1.0f, 0.8f * lean) }, { "above", 180.0f } }, seed + 5);
    if (partner && f.merges > 0.25f) step (Op::Breed, { { "lean", clamp01 (0.25f + 0.5f * f.merges) } }, seed + 6);
    // the live approximation of the same
    auto& V = L.live;
    V.shatter = clamp01 (0.55f * spires + 0.15f * f.jagged);
    V.shatterSize = clamp01 (0.5f - 0.4f * thin + 0.25f * f.spread);
    V.reverse = clamp01 (0.4f * f.jagged + 0.3f * f.merges);
    V.shimmer = clamp01 (0.35f * spires + 0.1f * thin);
    V.smear = clamp01 (0.45f * plateaus + 0.4f * f.merges);
    V.freeze = clamp01 (maxPlateau);
    V.drive = clamp01 (0.55f * f.jagged);
    V.tilt = juce::jlimit (-1.0f, 1.0f, 0.5f * lean - 0.6f * valleyDepth);
    V.width = clamp01 (0.8f * f.spread);
    V.wobble = clamp01 (0.9f * f.motion + 0.35f * f.evolve);
    V.wobbleHz = 2.0f + 6.0f * f.spread;
    L.any = true;
    std::vector<Meter> all = { { "SHATTER", V.shatter }, { "FREEZE", V.freeze * (maxPlateau > 0.55f ? 1.0f : 0.5f) }, { "SMEAR", V.smear }, { "GRAIN CLOUD", f.jagged > 0.12f ? 0.3f + 0.7f * f.jagged : 0.0f },
                               { "SHIMMER", V.shimmer }, { "TWIST", std::abs (lean) > 0.35f ? std::abs (lean) : 0.0f }, { "CARVE", valleyDepth }, { "WIDTH", V.width },
                               { "WOBBLE", V.wobble }, { "BREED", partner && f.merges > 0.25f ? f.merges : 0.0f } };
    for (auto& a : all) a.value = clamp01 (a.value);
    std::stable_sort (all.begin(), all.end(), [] (const Meter& a, const Meter& b) { return a.value > b.value; });
    for (int i = 0; i < 4; ++i) if (all[(size_t) i].value > 0.02f) L.meters.push_back (all[(size_t) i]);
    return L;
}
}
