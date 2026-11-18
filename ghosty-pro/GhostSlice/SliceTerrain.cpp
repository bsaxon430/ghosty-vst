#include "SliceTerrain.h"
#include "TerrainDsp.h"
#include <cmath>
#include <thread>
#include <atomic>

namespace gslice::land
{
ghost::terrain::Range range()
{
    ghost::terrain::Range r;
    r.minHz = 40.0f; r.maxHz = 16000.0f; r.maxDb = 12.0f;   // (a vocal take: a full mountain is +12 dB, plus its ring)
    return r;
}

bool flat (const ghost::terrain::Terrain& t) { return t.empty() && t.grit <= 0.0f; }

void shapeBuffer (juce::AudioBuffer<float>& buf, double rate, const ghost::terrain::Terrain& t)
{
    const int n = buf.getNumSamples(), ch = buf.getNumChannels();
    if (n < 64 || ch <= 0 || flat (t)) return;
    float dryRms = 0.0f, dryPeak = 0.0f;
    for (int c = 0; c < ch; ++c) { dryRms += buf.getRMSLevel (c, 0, n); dryPeak = juce::jmax (dryPeak, buf.getMagnitude (c, 0, n)); }
    dryRms /= (float) ch;
    if (dryRms < 1e-6f) return;
    std::vector<const float*> in; std::vector<float*> out;
    for (int c = 0; c < ch; ++c) { in.push_back (buf.getReadPointer (c)); out.push_back (buf.getWritePointer (c)); }
    ghost::terrain::SculptOptions o; o.range = range();
    ghost::terrain::sculpt (t, in.data(), out.data(), ch, n, rate, o);
    float wetRms = 0.0f, wetPeak = 0.0f;
    for (int c = 0; c < ch; ++c)
    {
        auto* p = buf.getWritePointer (c);
        for (int i = 0; i < n; ++i) if (! std::isfinite (p[i])) p[i] = 0.0f;
        wetRms += buf.getRMSLevel (c, 0, n); wetPeak = juce::jmax (wetPeak, buf.getMagnitude (c, 0, n));
    }
    wetRms /= (float) ch;
    if (wetRms < 1e-7f) return;
    // as loud as it was (within +12 dB of making up), and never past the dry peak (the engine's ceiling stays where it was)
    float g = juce::jlimit (0.0625f, 4.0f, dryRms / wetRms);
    if (wetPeak * g > dryPeak && dryPeak > 0.0f) g = dryPeak / wetPeak;
    buf.applyGain (g);
}

static juce::String digestOf (const juce::AudioBuffer<float>& b)
{
    juce::MemoryOutputStream mo;
    for (int ch = 0; ch < b.getNumChannels(); ++ch) mo.write (b.getReadPointer (ch), sizeof (float) * (size_t) b.getNumSamples());
    return juce::SHA256 (mo.getData(), mo.getDataSize()).toHexString();
}

std::shared_ptr<const gen::Take> shapeTake (const gen::Take& dry, const ghost::terrain::Terrain& t, bool withKeys)
{
    auto out = std::make_shared<gen::Take> (dry);
    if (flat (t)) return out;
    juce::ScopedNoDenormals nd;
    out->recipe.terrain = t;
    shapeBuffer (out->preview, out->rate, t);
    for (auto& c : out->chops) shapeBuffer (c, out->rate, t);
    out->digest = digestOf (out->preview);
    if (! withKeys) return out;
    return shapeKeys (*out, dry, t);
}

std::shared_ptr<const gen::Take> shapeKeys (const gen::Take& shaped, const gen::Take& dry, const ghost::terrain::Terrain& t)
{
    auto out = std::make_shared<gen::Take> (shaped);
    if (flat (t)) return out;
    // every key's sample, from the dry ones, a few at a time across the cores
    std::vector<juce::AudioBuffer<float>*> bufs;
    for (size_t i = 0; i < out->zones.size() && i < dry.zones.size(); ++i) { out->zones[i].audio.makeCopyOf (dry.zones[i].audio); bufs.push_back (&out->zones[i].audio); }
    for (size_t s = 0; s < out->sylZones.size() && s < dry.sylZones.size(); ++s)
        for (size_t i = 0; i < out->sylZones[s].size() && i < dry.sylZones[s].size(); ++i) { out->sylZones[s][i].audio.makeCopyOf (dry.sylZones[s][i].audio); bufs.push_back (&out->sylZones[s][i].audio); }
    const int threads = juce::jlimit (1, 8, (int) std::thread::hardware_concurrency() - 1);
    std::atomic<size_t> next { 0 };
    auto work = [&]
    {
        juce::ScopedNoDenormals nd;
        for (size_t i = next.fetch_add (1); i < bufs.size(); i = next.fetch_add (1)) shapeBuffer (*bufs[i], out->rate, t);
    };
    std::vector<std::thread> pool;
    for (int k = 1; k < threads && (size_t) k < bufs.size(); ++k) pool.emplace_back (work);
    work();
    for (auto& th : pool) th.join();
    return out;
}
}
