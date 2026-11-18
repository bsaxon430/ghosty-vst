// Ghost Matter - AGPL-3.0-only (see LICENSE, NOTICE.md).
#include "MatterTerrainMap.h"
#include <algorithm>
#include <cmath>

namespace matter
{
using namespace ghost::terrain;

bool MatterLand::sameRender (const MatterLand& o) const
{
    auto near = [] (float a, float b, float tol) { return std::abs (a - b) < tol; };
    return near (stretchMul, o.stretchMul, 0.03f) && near (spread, o.spread, 0.02f) && near (up, o.up, 0.02f) && near (down, o.down, 0.02f)
        && near (rough, o.rough, 0.02f) && near (bright, o.bright, 0.02f) && near (morph, o.morph, 0.02f);
}

MatterLand mapMatterLand (const Terrain& t, float motion, float amount)
{
    MatterLand m;
    amount = clamp01 (amount);
    if (t.empty() || amount <= 0.0f) return m;
    const auto f = landFeatures (t, -1.0f, motion);
    float tallest = 0, valleyDepth = 0, plateauH = 0, spireH = 0;
    for (int i = 0; i < f.count; ++i)
    {
        const auto& mf = f.m[i];
        if (mf.valley) valleyDepth = std::max (valleyDepth, -mf.height);
        else tallest = std::max (tallest, mf.height);
        plateauH = std::max (plateauH, mf.plateau * std::max (0.0f, mf.height));
        spireH = std::max (spireH, mf.spire * std::max (0.0f, mf.height));
    }
    const float a = amount;
    // live: SPACE + PATTERN
    m.spaceX = a * (f.balance - 0.5f) * 0.8f;
    m.spaceY = a * (f.centreZ - 0.5f) * 0.6f;
    m.room = a * clamp01 (0.7f * f.mass + 0.3f * f.spread) * 0.6f;
    m.tail = a * clamp01 (0.9f * plateauH + 0.3f * f.merges) * 0.6f;
    m.width = a * clamp01 (f.merges + 0.5f * f.spread) * 0.5f;
    m.wander = a * clamp01 (0.9f * motion + 0.7f * f.evolve);
    m.mutate = a * clamp01 (0.8f * f.jagged + 0.2f * t.grit);
    // the matter: STRETCH, MAKE, MORPH
    m.stretchMul = 1.0f + a * clamp01 (tallest) * 3.0f;
    m.spread = a * clamp01 (0.8f * f.merges);
    m.up = a * clamp01 (0.4f * f.spires + 0.6f * spireH);
    m.down = a * clamp01 (valleyDepth);
    m.rough = a * clamp01 (0.7f * f.jagged + 0.3f * t.grit) * 0.5f;
    m.bright = a * (f.balance - 0.5f) * 0.6f;
    m.morph = a * clamp01 (f.mass) * 0.4f;
    m.any = true;
    std::array<Meter, 8> all { Meter { "STRETCH", (m.stretchMul - 1.0f) / 3.0f }, Meter { "ROOM", m.room / 0.6f }, Meter { "TAIL", m.tail / 0.6f },
                               Meter { "OCTAVE UP", m.up }, Meter { "OCTAVE DOWN", m.down }, Meter { "ROUGH", m.rough * 2.0f },
                               Meter { "WIDTH", m.width * 2.0f }, Meter { "WANDER", m.wander } };
    std::sort (all.begin(), all.end(), [] (const Meter& x, const Meter& y) { return x.value > y.value; });
    for (int i = 0; i < 4; ++i) if (all[(size_t) i].value > 0.02f) m.meters[(size_t) m.meterCount++] = all[(size_t) i];
    return m;
}

RenderSettings applyLand (const RenderSettings& base, const MatterLand& l)
{
    if (! l.any) return base;
    RenderSettings r = base;
    r.stretch.amount = std::clamp (base.stretch.amount * l.stretchMul, 1.0f, 64.0f);
    r.stretch.spread = clamp01 (base.stretch.spread + l.spread);
    r.stretch.up = clamp01 (base.stretch.up + l.up);
    r.stretch.down = clamp01 (base.stretch.down + l.down);
    r.make.rough = clamp01 (base.make.rough + l.rough);
    r.make.bright = clamp01 (base.make.bright + l.bright);
    if (base.morph.timbre != Timbre::none) r.morph.amount = clamp01 (base.morph.amount + l.morph);
    return r;
}
}
