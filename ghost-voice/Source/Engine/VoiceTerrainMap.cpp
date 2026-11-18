// Ghost Voice - AGPL-3.0-only (see LICENSE, NOTICE.md).
#include "VoiceTerrainMap.h"
#include <algorithm>
#include <cmath>

namespace gv
{
using namespace ghost::terrain;

VoiceLand mapVoiceLand (const Terrain& t, float motion, float amount, Engine engine)
{
    VoiceLand v;
    amount = clamp01 (amount);
    if (t.empty() || amount <= 0.0f) return v;
    const auto f = landFeatures (t, -1.0f, motion);
    float tallest = 0, valley = 0, spire = 0;
    for (int i = 0; i < f.count; ++i)
    {
        const auto& m = f.m[i];
        if (m.valley) valley = std::max (valley, -m.height);
        else tallest = std::max (tallest, m.height);
        spire = std::max (spire, m.spire * std::max (0.0f, m.height));
    }
    const float a = amount;
    v.follow = a * clamp01 (tallest) * 0.3f;
    v.steps = (int) std::lround (a * clamp01 (f.mass) * 30.0f);
    v.lean = a * (f.balance - 0.5f) * 0.5f;
    const float down = a * clamp01 (valley), up = a * clamp01 (0.5f * f.spires + spire);
    v.semitones = (int) std::lround (12.0f * (up - down));
    v.detail = a * clamp01 (0.8f * f.jagged + 0.2f * t.grit) > 0.5f;
    v.motionDepth = a * clamp01 (0.8f * motion + 0.7f * f.evolve) * 0.25f;
    v.resonance = a * clamp01 (tallest) * 0.6f;
    v.warmth = a * clamp01 (f.mass) * 0.5f;
    v.grit = a * clamp01 (0.8f * f.jagged + 0.2f * t.grit) * 0.4f;
    v.any = true;
    const bool dsp = isDsp (engine);
    std::array<Meter, 6> all { dsp ? Meter { "RESONANCE", v.resonance / 0.6f } : Meter { "FOLLOW", v.follow / 0.3f },
                               dsp ? Meter { "WARMTH", v.warmth / 0.5f } : Meter { "QUALITY", (float) v.steps / 30.0f },
                               Meter { v.lean < 0 ? "TOWARDS A" : "TOWARDS B", std::abs (v.lean) / 0.25f },
                               Meter { v.semitones < 0 ? "PITCH DOWN" : "PITCH UP", (float) std::abs (v.semitones) / 12.0f },
                               dsp ? Meter { "GRIT", v.grit / 0.4f } : Meter { "SINGER DETAIL", v.detail ? 1.0f : 0.0f }, Meter { "MOTION", v.motionDepth / 0.25f } };
    std::sort (all.begin(), all.end(), [] (const Meter& x, const Meter& y) { return x.value > y.value; });
    for (int i = 0; i < 4; ++i) if (all[(size_t) i].value > 0.02f) v.meters[(size_t) v.meterCount++] = all[(size_t) i];
    return v;
}

EngineSettings applyLand (const EngineSettings& base, const VoiceLand& l)
{
    if (! l.any) return base;
    EngineSettings s = base;
    s.follow = std::clamp (base.follow + l.follow, 0.0f, 1.0f);
    s.steps = std::clamp (base.steps + l.steps, 5, 100);
    s.semitones = std::clamp (base.semitones + l.semitones, -24, 24);
    s.detail = base.detail || l.detail;
    return s;
}
}

namespace gv
{
DspSettings applyLand (const DspSettings& base, const VoiceLand& l)
{
    if (! l.any) return base;
    DspSettings s = base;
    s.resonance = std::clamp (base.resonance + l.resonance, -1.0f, 1.0f);
    s.warmth = std::clamp (base.warmth + l.warmth, -1.0f, 1.0f);
    s.texture = std::clamp (base.texture + l.grit, 0.0f, 1.0f);
    return s;
}
}
