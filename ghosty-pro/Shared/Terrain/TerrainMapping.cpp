#include "TerrainMapping.h"
#include <cmath>

namespace ghost::terrain
{
// the width -> Q line keeps the first TERRAIN's ends (0.012 .. 0.35), so saved landscapes sound the same; thinner spires and
// broader mounds than those sit at its ends
static constexpr float kQMinWidth = 0.012f, kQMaxWidth = 0.35f;

float xToHz (float x, const Range& r) { return r.minHz * std::pow (r.maxHz / r.minHz, juce::jlimit (0.0f, 1.0f, x)); }

float hzToX (float hz, const Range& r)
{
    if (hz <= r.minHz) return 0.0f;
    return juce::jlimit (0.0f, 1.0f, std::log (hz / r.minHz) / std::log (r.maxHz / r.minHz));
}

float qFromWidth (float width, const Range& r)
{
    const float u = (std::log (juce::jlimit (kQMinWidth, kQMaxWidth, width)) - std::log (kQMinWidth)) / (std::log (kQMaxWidth) - std::log (kQMinWidth));
    return std::exp (std::log (r.maxQ) + u * (std::log (r.minQ) - std::log (r.maxQ)));
}

float widthFromQ (float q, const Range& r)
{
    const float u = (std::log (juce::jlimit (r.minQ, r.maxQ, q)) - std::log (r.maxQ)) / (std::log (r.minQ) - std::log (r.maxQ));
    return std::exp (std::log (kQMinWidth) + u * (std::log (kQMaxWidth) - std::log (kQMinWidth)));
}

int bandsAt (const Terrain& t, float z, Band* out, const Range& r)
{
    const float zz = t.effectiveZ (z);
    for (int i = 0; i < t.size(); ++i)
    {
        const Peak p = t.peakAt (i, z);
        const float dz = (zz - p.z) / p.depth;
        const float presence = profile (dz * dz);   // how much of the peak is here at this moment (the same shape the land has in time)
        auto& b = out[i];
        b.height = p.height * presence;
        b.hz = xToHz (p.x, r);
        b.gainDb = b.height * r.maxDb;
        b.q = qFromWidth (p.width, r);
        b.drive = juce::jlimit (0.0f, 1.0f, t.grit + p.rough * presence);
    }
    return t.size();
}

void bandsAt (const Terrain& t, float z, std::vector<Band>& out, const Range& r)
{
    out.resize ((size_t) t.size());
    if (! out.empty()) bandsAt (t, z, out.data(), r);
}

void gainCurveDb (const Terrain& t, float z, const float* hz, int n, float* outDb, const Range& r)
{
    for (int i = 0; i < n; ++i) outDb[i] = juce::jlimit (-1.0f, 1.0f, t.heightAt (hzToX (hz[i], r), z)) * r.maxDb;
}

juce::String describeHz (float hz)
{
    return hz >= 1000.0f ? juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz";
}

juce::String describeBand (const Band& b)
{
    return describeHz (b.hz) + "  " + (b.gainDb >= 0 ? "+" : "") + juce::String (b.gainDb, 1) + " dB  Q " + juce::String (b.q, b.q < 10 ? 1 : 0);
}
}
