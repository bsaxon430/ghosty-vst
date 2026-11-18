// SPDX-License-Identifier: AGPL-3.0-or-later
// GHOST ATLAS - SPECTRAL FX (see SpectralRack.h). Poltergeist 2's rack object does all the sound and all the careful
// work round it (its effects are built off the audio thread and swapped in between blocks, every change is faded, the
// dry side is held back by the window). This file only stands between that object and a Phantom FX slot.
#include "SpectralRack.h"
#include "ghostblock/spectral/spectral-fx.h"
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>

namespace atlas
{
namespace p2 = ghostblock::p2::spectral;
using Rack = p2::SpectralFx;

namespace
{
static_assert (spectral::kPlaces == p2::kSlots && spectral::kKnobs == p2::kHostParamsPerSlot && spectral::kWarp == p2::kWarpParams,
               "the rack has changed shape: SpectralRack.h's numbers are Poltergeist 2's");

std::string unitOf (const p2::ParamDesc& d) { std::string u = d.unit; while (! u.empty() && u.front() == ' ') u.erase (u.begin()); return u; }
const p2::TypeDesc& type (int effect) { const auto& t = p2::types(); return t[(size_t) juce::jlimit (0, (int) t.size() - 1, effect)]; }
const p2::ParamDesc* desc (int effect, int k) { const auto& t = type (effect); return k >= 0 && k < (int) t.params.size() ? &t.params[(size_t) k] : nullptr; }

// Where an effect starts when it is put in a place here. Upstream starts nearly every effect at "Amount 0 %": in its own
// plug-in the amount is the first thing one turns. Here an effect that is chosen is heard at once, a little: each starts
// about where it moved a held piano chord by three to six decibels (GhostAtlasTests spectral "<sound>" curve prints the
// measurements). Every other control starts where upstream starts it. Three wait for a hand, as upstream has them:
// Frequency Remap (nothing moves until its points do), the Spectral Equalizer (flat) and Utility (a tool).
struct Start { const char* effect; const char* control; float value; };
const Start kStarts[] = {
    { "BinShuffle", "amount", 0.02f },          { "RandomBinDeath", "amount", 15.0f },    { "SpectralDropout", "amount", 35.0f },
    { "SpectralPixelation", "amount", 15.0f },  { "SpectralJPEG", "amount", 15.0f },      { "SpectralClipper", "threshold", -40.0f },
    { "FrequencyShift", "shift", 40.0f },       { "FrequencyWarp", "amount", 10.0f },     { "FrequencyDrift", "depth", 2.0f },
    { "SpectralMirror", "amount", 25.0f },      { "BinTeleport", "amount", 10.0f },       { "PhaseCorruption", "amount", 10.0f },
    { "PhaseNoise", "amount", 15.0f },          { "ComplexRotation", "amount", 80.0f },   { "MagnitudePhaseSwap", "amount", 15.0f },
    { "SpectralDispersion", "amount", 25.0f },  { "BinHold", "amount", 15.0f },           { "FrameHold", "amount", 35.0f },
    { "TemporalSmear", "amount", 70.0f },       { "TimeDesync", "amount", 35.0f },        { "SpectralBloom", "amount", 10.0f },
    { "SpectralComb", "amount", 35.0f },        { "SpectralContrast", "amount", 20.0f },  { "HarmonicSculpt", "amount", 100.0f },
    { "HarmonicSculpt", "even", -12.0f },       { "HarmonicMatch", "amount", 50.0f },     { "PartialStretcher", "amount", 3.0f },
    { "PartialStiffString", "amount", 8.0f },   { "PartialCentroid", "amount", 4.0f },    { "PartialPGrid", "amount", 8.0f },
    { "PartialFGrid", "amount", 8.0f },
};
// knob k of an effect, where it starts here, as it is kept (between 0 and 1)
float startOf (int effect, int k)
{
    const auto* d = desc (effect, k);
    if (d == nullptr) return 0.0f;
    const auto& t = type (effect);
    for (const auto& st : kStarts) if (t.id == st.effect && d->id == st.control) return d->toNorm (st.value);
    return d->toNorm (d->def);
}

// the rack's own parameter for one of our controls, and what our number is multiplied by on the way
struct Wire { uint32_t id; float scale; };
Wire wireOf (int i)
{
    using namespace spectral;
    switch (i)
    {
        case iWindow:   return { Rack::pFft, 1.0f };
        case iShape:    return { Rack::pWindow, 1.0f };
        case iOverlap:  return { Rack::pOverlap, 1.0f };
        case iReach:    return { Rack::pLimit, 1.0f };
        case iChainMix: return { Rack::pChainMix, 100.0f };
        case iOrder:    return { Rack::pOrder, 1.0f };
        default: break;
    }
    if (i < iWarpOn)
    {
        const int place = (i - iPlaces) / kPerPlace, part = (i - iPlaces) % kPerPlace;
        if (part == 0) return { Rack::pSlotType (place), 1.0f };
        if (part == 1) return { Rack::pSlotOn (place), 1.0f };
        if (part == 2) return { Rack::pSlotMix (place), 100.0f };
        return { Rack::pSlotParam (place, part - 3), 1.0f };          // (kept between 0 and 1, as the rack's host takes them)
    }
    if (i == iWarpOn) return { Rack::pWarpOn, 1.0f };
    return { Rack::pWarpParam (i - iWarpOn - 1), 1.0f };              // (WARP CORE's own units)
}

class SpectralRackFx final : public phantom::PhantomEffect
{
public:
    SpectralRackFx()
    {
        rack.setParam (Rack::pOn, 1.0);          // the slot's own ON and MIX do what the rack's master ON and MIX do in Poltergeist 2
        rack.setParam (Rack::pMix, 100.0);
        told.fill (std::numeric_limits<float>::quiet_NaN());
    }
    const char* key() const override { return "spectral"; }
    const char* title() const override { return "SPECTRAL FX"; }
    std::vector<phantom::ParamSpec> parameters() const override { return spectralRackParams(); }

    void prepare (double sampleRate, int maxBlockSize, int) override
    {
        const std::lock_guard<std::mutex> lock (offAudio);
        rate = sampleRate;
        spare.assign ((size_t) juce::jmax (16, maxBlockSize), 0.0f);
        rack.activate (rate);                    // (everything is built again for this rate, and its latency is fixed until the next time)
        latency = (int) rack.latency();
        fresh = true;                            // (the slot tells it its controls next: what they ask for is then built outright)
    }
    void reset() override { rack.requestReset(); }          // (may be called by the audio thread: the rack forgets at its next block)

    // message / loader thread, with nobody inside process(): every control is told, what they ask for is built, and
    // if that moves the latency the rack is started again, so that the slot (which asks straight after) hears the new one
    void updateParameters (const phantom::ModulationContext& ctx) override
    {
        const std::lock_guard<std::mutex> lock (offAudio);
        tell (ctx.values, ctx.numValues);
        if (rate <= 0.0) return;
        // A rack that has just been prepared, or whose latency has to move, is started again with everything built in
        // place (the audio thread is not in it). Otherwise what changed is built and handed over, to be faded in.
        if (fresh || rack.latencyWanted() != rack.latency()) rack.activate (rate);
        else rack.serviceMainThread();
        fresh = false;
        latency = (int) rack.latency();
    }

    void process (juce::dsp::AudioBlock<float> block, const phantom::ModulationContext& ctx) noexcept override
    {
        const int n = (int) block.getNumSamples();
        if (n <= 0 || block.getNumChannels() == 0) return;
        tell (ctx.values, ctx.numValues);
        float* l = block.getChannelPointer (0);
        float* r = block.getNumChannels() > 1 ? block.getChannelPointer (1) : spare.data();
        if (block.getNumChannels() < 2) { if (n > (int) spare.size()) return; std::memcpy (r, l, sizeof (float) * (size_t) n); }
        rack.process (l, r, (uint32_t) n, ctx.tempo.bpm > 1.0 ? ctx.tempo.bpm : 120.0);
    }

    int getLatencySamples() const override { return latency; }
    bool hasNullSetting() const override { return true; }             // (no effect in any place and WARP CORE off: the sound goes through as it came)
    bool changesLevelOnPurpose() const override { return true; }      // (a compressor and a distortion are among its effects)
    double tailSeconds() const override { return 4.0; }               // (it holds frames, smears and echoes: the slot lets it ring out, then lets it sleep)

    // ---- the engine's side (SpectralRack.h)
    bool set (int index, float value)
    {
        if (index < 0 || index >= spectral::kControls) return false;
        const std::lock_guard<std::mutex> lock (offAudio);
        const auto w = wireOf (index);
        rack.setParam (w.id, (double) value * w.scale);
        return serviceLocked (true);
    }
    bool service()
    {
        const std::lock_guard<std::mutex> lock (offAudio);
        return serviceLocked (false);
    }
    bool spectrum (std::vector<float>& magnitudes, double& sampleRate, bool before)
    {
        const std::lock_guard<std::mutex> lock (offAudio);
        return rate > 0.0 && rack.spectrum (magnitudes, sampleRate, before);
    }

private:
    // builds what the controls ask for and frees what the audio thread has handed back; true: its latency has to be asked again
    bool serviceLocked (bool whetherAskedOrNot)
    {
        if (rate <= 0.0) return false;
        if (rack.takeMainThreadRequest() || whetherAskedOrNot) rack.serviceMainThread();
        return rack.latencyWanted() != rack.latency();
    }
    // Every control whose value is not the one last told goes to the rack. The audio thread does this at each block
    // (and updateParameters, with the audio thread kept out); told[] is theirs alone.
    void tell (const float* values, int count) noexcept
    {
        using namespace spectral;
        count = juce::jmin (count, (int) kControls);
        for (int i = 0; i < count; ++i)
        {
            const float v = values[i];
            if (v == told[(size_t) i]) continue;
            told[(size_t) i] = v;
            const auto w = wireOf (i);
            rack.setParam (w.id, (double) v * w.scale);
            // another effect in this place: the rack has just given it that effect's own starting values, so the
            // sound's values for its knobs are told again (they come next in this same pass)
            if (i >= iPlaces && i < iWarpOn && (i - iPlaces) % kPerPlace == 0)
                for (int k = 0; k < kKnobs; ++k) told[(size_t) (i + 3 + k)] = std::numeric_limits<float>::quiet_NaN();
        }
    }

    Rack rack;
    std::mutex offAudio;                       // everything the rack calls its main thread's work: one caller at a time (never the audio thread)
    std::array<float, spectral::kControls> told {};
    std::vector<float> spare;
    double rate = 0.0;
    int latency = 0;
    bool fresh = true;
};

SpectralRackFx* asRack (phantom::PhantomEffect& fx) { return std::strcmp (fx.key(), "spectral") == 0 ? static_cast<SpectralRackFx*> (&fx) : nullptr; }
} // namespace

// ============================================================================================ the unit's controls
std::vector<phantom::ParamSpec> spectralRackParams()
{
    using P = phantom::ParamSpec;
    using namespace spectral;
    std::vector<P> v;
    v.reserve ((size_t) kControls);
    {
        std::vector<std::string> sizes;
        for (int i = 0; i < p2::kFftSizes; ++i) sizes.push_back (std::to_string (p2::fftSizeAt (i)));
        v.push_back (P::choice ("fft", "WINDOW", sizes, p2::kDefaultFft));
    }
    {
        std::vector<std::string> shapes;
        for (int i = 0; i < p2::kWindows; ++i) shapes.push_back (p2::windowName (i));
        v.push_back (P::choice ("window", "SHAPE", shapes, 0));
    }
    v.push_back (P::integer ("overlap", "OVERLAP", 2, 16, 4));
    {
        std::vector<std::string> reach;
        for (int i = 0; i < p2::kLimits; ++i)
        {
            reach.push_back ("Up to " + std::to_string ((int) std::lround ((double) p2::limitHz (i) / 1000.0)) + " kHz");
        }
        v.push_back (P::choice ("limit", "REACH", reach, p2::kDefaultLimit));
    }
    v.push_back (P::pct ("chain", "CHAIN MIX", 1.0f));
    v.push_back (P::choice ("order", "ORDER", { "Chain, then Warp Core", "Warp Core, then chain" }, 0));
    std::vector<std::string> effects;
    for (const auto& t : p2::types()) effects.push_back (t.index == 0 ? std::string ("NONE") : t.name);
    // The rack arrives with one effect in its first place (as every unit here arrives doing what it is for: in
    // Poltergeist 2 the rack starts empty, on a page of its own). The other places are empty.
    const int first = juce::jmax (0, p2::typeIndex ("TemporalSmear"));
    for (int s = 0; s < kPlaces; ++s)
    {
        const std::string n = std::to_string (s + 1);
        const int effect = s == 0 ? first : 0;
        v.push_back (P::choice ("s" + n + "_fx", "EFFECT " + n, effects, effect));
        v.push_back (P::toggle ("s" + n + "_on", "ON " + n, true));
        v.push_back (P::pct ("s" + n + "_mix", "MIX " + n, 1.0f));
        for (int k = 0; k < kKnobs; ++k)
        {
            auto p = P::pct ("s" + n + "_p" + std::to_string (k + 1), "CONTROL " + n + "." + std::to_string (k + 1), startOf (effect, k));
            p.unit.clear();
            v.push_back (p);
        }
    }
    v.push_back (P::toggle ("w_on", "WARP CORE", false));
    static const char* const ids[kWarp] = { "w_bands", "w_high", "w_scale", "w_poles", "w_pitch", "w_affect", "w_fill", "w_mode", "w_mix" };
    const auto& w = p2::warpParams();
    for (int k = 0; k < kWarp; ++k)
    {
        const auto& d = w[(size_t) k];
        const std::string name = d.name;                  // (upstream's own label, as it writes it)
        if (d.kind == p2::Kind::Bool) v.push_back (P::toggle (ids[k], name, d.def > 0.5f));
        else if (d.kind == p2::Kind::Int) v.push_back (P::integer (ids[k], name, (int) d.min, (int) d.max, (int) d.def));
        else if (d.kind == p2::Kind::Choice)
        {
            std::vector<std::string> c;
            for (const auto& s : d.choices) c.push_back (s);
            v.push_back (P::choice (ids[k], name, c, (int) d.def));
        }
        else v.push_back (P::range (ids[k], name, d.min, d.max, d.def, k == kWarp - 1 ? std::string ("%") : unitOf (d)));
    }
    jassert ((int) v.size() == kControls);
    return v;
}

std::unique_ptr<phantom::PhantomEffect> makeSpectralRack() { return std::make_unique<SpectralRackFx>(); }
bool spectralSet (phantom::PhantomEffect& fx, int index, float value) { auto* r = asRack (fx); return r != nullptr && r->set (index, value); }
bool spectralService (phantom::PhantomEffect& fx) { auto* r = asRack (fx); return r != nullptr && r->service(); }
bool spectralSpectrum (phantom::PhantomEffect& fx, std::vector<float>& m, double& rate, bool before) { auto* r = asRack (fx); return r != nullptr && r->spectrum (m, rate, before); }

// ============================================================================================ for the page
namespace spectral
{
juce::String idOf (int index)
{
    static const std::vector<phantom::ParamSpec> specs = spectralRackParams();
    return index >= 0 && index < (int) specs.size() ? juce::String (specs[(size_t) index].id) : juce::String();
}
int numEffects() { return (int) p2::types().size(); }
juce::String effectName (int effect) { return effect <= 0 ? juce::String ("None") : juce::String (type (effect).name); }
juce::String effectFolder (int effect) { return effect <= 0 ? juce::String() : juce::String (type (effect).category); }
juce::StringArray folders() { juce::StringArray a; for (const auto& c : p2::categories()) a.add (juce::String (c)); return a; }
bool usesWindow (int effect) { const auto d = type (effect).domain; return d == p2::Domain::Spectral || d == p2::Domain::Partial; }
int numKnobs (int effect) { return effect <= 0 ? 0 : juce::jmin ((int) kKnobs, (int) type (effect).params.size()); }
Control knob (int effect, int k)
{
    Control c;
    if (const auto* d = desc (effect, k))
    {
        c.name = juce::String (d->name);
        c.kind = d->kind == p2::Kind::Bool ? cSwitch : (d->kind == p2::Kind::Choice ? cChoice : (d->kind == p2::Kind::Int ? cWhole : cFloat));
        c.min = d->min; c.max = d->max; c.def = d->def; c.log = d->logScale && c.kind == cFloat;
        for (const auto& s : d->choices) c.choices.add (juce::String (s));
    }
    return c;
}
float toKept (int effect, int k, float real) { const auto* d = desc (effect, k); return d != nullptr ? d->toNorm (real) : 0.0f; }
float fromKept (int effect, int k, float kept) { const auto* d = desc (effect, k); return d != nullptr ? d->fromNorm (kept) : 0.0f; }
juce::String written (int effect, int k, float real) { const auto* d = desc (effect, k); return d != nullptr ? juce::String (d->text (real)) : juce::String(); }
void put (std::map<juce::String, float>& values, int place, int effect)
{
    place = juce::jlimit (0, kPlaces - 1, place); effect = juce::jlimit (0, numEffects() - 1, effect);
    values[idOf (iEffect (place))] = (float) effect;
    for (int k = 0; k < kKnobs; ++k) values[idOf (iKnob (place, k))] = startOf (effect, k);
}
bool warpAvailable() { return p2::warpAvailable(); }
int windowSamples (int choice) { return p2::fftSizeAt (choice); }
} // namespace spectral
} // namespace atlas
