#include "TerrainFeatures.h"
#include <cmath>

namespace ghost::terrain
{
LandFeatures landFeatures (const Terrain& t, float z, float motion)
{
    LandFeatures f;
    f.motion = clamp01 (motion);
    f.evolve = t.evolve; f.phase = t.phase;
    f.count = t.size();
    const float zz = z >= 0.0f ? t.effectiveZ (z) : 0.5f;
    float wSum = 0.0f, cx = 0.0f, cz = 0.0f, rough = 0.0f, sizeSum = 0.0f, massSum = 0.0f;
    for (int i = 0; i < t.size(); ++i)
    {
        const Peak p = t.peakAt (i, z >= 0.0f ? z : t[i].z);
        auto& m = f.m[i];
        m.x = p.x; m.z = p.z; m.height = p.height; m.size = p.r(); m.rough = juce::jmin (1.0f, p.rough + t.grit);
        if (z >= 0.0f) { const float dz = (zz - p.z) / juce::jmax (1e-4f, p.depth); m.presence = profile (dz * dz); }
        m.valley = p.height < 0.0f;
        const float h = std::abs (p.height) * m.presence;
        // a spire: tall (0.5 .. 1) and thin (r 0.12 .. 0.04); a plateau: past the top ring (1 .. 1.15) and not thin;
        // a knoll: low (under 0.35) and small (r under 0.2)
        m.spire = smooth01 ((h - 0.45f) / 0.45f) * smooth01 ((0.13f - m.size) / 0.08f);
        m.plateau = smooth01 ((h - 1.0f) / 0.15f) * smooth01 ((m.size - 0.07f) / 0.08f);
        m.knoll = smooth01 ((0.38f - h) / 0.2f) * smooth01 ((0.22f - m.size) / 0.1f) * (h > 0.02f ? 1.0f : 0.0f);
        if (m.valley) { ++f.valleys; m.spire = m.plateau = m.knoll = 0.0f; }
        else
        {
            if (h > f.tallest) { f.tallest = h; f.tallestX = p.x; f.tallestZ = p.z; }
            f.spires += m.spire; f.plateaus += m.plateau; f.knolls += m.knoll;
        }
        const float w = h * (0.05f + m.size);
        wSum += w; cx += w * p.x; cz += w * p.z;
        rough += m.rough * h;
        sizeSum += m.size;
        massSum += (p.height > 0.0f ? h : 0.0f) * m.size * m.size;
    }
    // how far each one runs into its nearest neighbour (overlap of their feet, in island radii)
    float mergeSum = 0.0f;
    for (int i = 0; i < t.size(); ++i)
    {
        auto& a = f.m[i];
        for (int j = 0; j < t.size(); ++j)
        {
            if (j == i) continue;
            const auto& b = f.m[j];
            const float d = std::hypot ((a.x - b.x) * 2.0f, (a.z - b.z) * 2.0f);
            const float reach = 0.9f * (a.size + b.size);
            if (reach > 1e-4f) a.merge = juce::jmax (a.merge, clamp01 (1.0f - d / reach) * (b.height > 0.05f && a.height > 0.05f ? 1.0f : 0.0f));
        }
        mergeSum += a.merge;
    }
    if (t.size() > 0)
    {
        f.merges = mergeSum / (float) t.size();
        f.meanSize = sizeSum / (float) t.size();
        f.jagged = clamp01 (t.grit + (wSum > 0.0f ? rough / juce::jmax (1e-4f, [&] { float hs = 0; for (int i = 0; i < t.size(); ++i) hs += std::abs (f.m[i].height) * f.m[i].presence; return hs; }()) : 0.0f) * 0.8f);
    }
    else f.jagged = clamp01 (t.grit);
    if (wSum > 1e-6f)
    {
        f.centreX = cx / wSum; f.centreZ = cz / wSum;
        float var = 0.0f;
        for (int i = 0; i < t.size(); ++i)
        {
            const auto& m = f.m[i];
            const float w = std::abs (m.height) * m.presence * (0.05f + m.size);
            var += w * ((m.x - f.centreX) * (m.x - f.centreX) + (m.z - f.centreZ) * (m.z - f.centreZ));
        }
        f.spread = clamp01 (std::sqrt (var / wSum) / 0.32f);
        f.balance = clamp01 (f.centreX);
        f.symmetry = clamp01 (1.0f - 2.0f * std::abs (f.centreX - 0.5f) * 1.6f);
    }
    f.mass = clamp01 (massSum / 0.25f);
    return f;
}
}
