// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Mutate.h"
#include "Dsp.h"
#include <set>

namespace atlas
{
using namespace measure;

namespace
{
constexpr int kCoreLo = 36, kCoreHi = 84;        // the keys a sound is mostly played on
constexpr float kBandsPerOctave = 2.776f;        // 24 bands over 40 Hz .. 16 kHz

struct Profile { std::array<double, 24> p {}; double total = 0.0; };

// an instrument's energy by band at a level and a transposition (from what the analysis heard)
Profile profileOf (const Instrument& i, float gainDb, int semitones)
{
    Profile out;
    const int shift = (int) std::lround ((float) semitones / 12.0f * kBandsPerOctave);
    const double level = std::pow (10.0, ((double) i.f.loudnessDb + (double) gainDb) / 10.0);
    for (int b = 0; b < 24; ++b)
    {
        const int from = b - shift;
        if (from < 0 || from >= 24) continue;
        out.p[(size_t) b] = level * std::pow (10.0, (double) i.f.bands[(size_t) from] / 10.0);
        out.total += out.p[(size_t) b];
    }
    return out;
}
void add (Profile& a, const Profile& b) { for (size_t k = 0; k < 24; ++k) a.p[k] += b.p[k]; a.total += b.total; }
// how much of their shape two sounds share (1 = the same bands, 0 = nothing in common)
float shared (const Profile& a, const Profile& b)
{
    if (a.total <= 0.0 || b.total <= 0.0) return 0.0f;
    double s = 0.0; for (size_t k = 0; k < 24; ++k) s += std::min (a.p[k] / a.total, b.p[k] / b.total);
    return (float) s;
}
// how much of a sound lies where the bed is thin (bands the bed fills to less than a tenth of its strongest)
float room (const Profile& c, const Profile& bed)
{
    if (c.total <= 0.0) return 0.0f;
    if (bed.total <= 0.0) return 0.5f;
    double top = 0.0; for (double v : bed.p) top = std::max (top, v);
    double s = 0.0; for (size_t k = 0; k < 24; ++k) if (bed.p[k] < 0.1 * top) s += c.p[k] / c.total;
    return (float) s;
}
float brightnessOf (const Profile& p)
{
    if (p.total <= 0.0) return 0.0f;
    double hi = 0.0; for (size_t k = 16; k < 24; ++k) hi += p.p[k];       // from about 2 kHz up
    return clamp01 ((float) ((10.0 * std::log10 ((hi + 1.0e-12) / (p.total - hi + 1.0e-12)) + 35.0) / 35.0));
}
float coverage (const Instrument& i, int semitones)
{
    int n = 0; for (int k = kCoreLo; k <= kCoreHi; ++k) if (i.plays (k + semitones)) ++n;
    return (float) n / (float) (kCoreHi - kCoreLo + 1);
}
float partLevel (const juce::String& role)
{
    if (role == "support") return -5.0f;
    if (role == "transient") return -4.0f;
    if (role == "atmosphere") return -9.0f;
    if (role == "shimmer") return -11.0f;
    return 0.0f;
}
float levelFor (const Instrument& i) { return i.f.valid ? juce::jlimit (-18.0f, 9.0f, -19.0f - i.f.loudnessDb) : 0.0f; }
// an instrument's measured tuning is acted on only when it is a pitched sound whose own notes agree with each other
// (a tremolo or a chorused ensemble reads a little differently note by note: moving it on such a reading would detune it)
bool tuningKnown (const Instrument& i) { return i.f.valid && i.f.harmonicity >= 0.6f && i.f.pitchSpread <= 8.0f; }

// loop seams and other clicks while a sound is held: one sample-to-sample step standing far above the usual
float seamsOf (const std::vector<float>& mono)
{
    if (mono.size() < 4096) return 0.0f;
    std::vector<float> d; d.reserve (mono.size());
    for (size_t i = 1; i < mono.size(); ++i) d.push_back (std::abs (mono[i] - mono[i - 1]));
    std::vector<float> local;
    for (size_t i = 0; i + 512 <= d.size(); i += 512) { double s = 0; for (size_t k = 0; k < 512; ++k) s += (double) d[i + k] * d[i + k]; local.push_back ((float) std::sqrt (s / 512.0)); }
    if (local.size() < 5) return 0.0f;
    std::sort (local.begin(), local.end());
    const float med = juce::jmax (1.0e-7f, local[local.size() / 2]);
    float worst = 0; for (float v : d) worst = juce::jmax (worst, v);
    return clamp01 ((worst / med - 9.0f) / 20.0f);
}


const char* const moodWords[] = { "Amber", "Ember", "Drowned", "Lantern", "Emerald", "Tidal", "Snowbound", "Northern", "Hollow", "Silver", "Distant", "Sunken", "Paper", "Velvet", "Glass", "Faded" };
// the second word of a name MUTATE gives a sound that has sailed somewhere new
juce::String nounFor (const juce::String& category)
{
    static const std::pair<const char*, const char*> nouns[] = {
        { "Pianos", "Piano" }, { "Keys", "Keys" }, { "Bells", "Bells" }, { "Analog Synths", "Circuit" }, { "Digital Synths", "Signal" }, { "Leads", "Line" }, { "Plucks", "Pluck" },
        { "Pads", "Pad" }, { "Cinematic", "Score" }, { "Ambient", "Field" }, { "Choirs", "Choir" }, { "Vocal Pads", "Voices" }, { "Vocal Chops", "Chant" }, { "Vocal Textures", "Breath" },
        { "Strings", "Strings" }, { "Experimental", "Machine" }, { "Granular", "Dust" }, { "Soundscapes", "Weather" }, { "Hybrid Instruments", "Hybrid" } };
    for (const auto& n : nouns) if (category == n.first) return n.second;
    return "Sound";
}
} // namespace

// ============================================================================================ what it works from
struct Mutator::Impl
{
    Impl (const Library& l, Engine::Resolver r) : lib (l), resolver (std::move (r)) {}
    const Library& lib;
    Engine::Resolver resolver;
    mutable Timbre::Index timbre;
    mutable std::set<juce::String> asked;
    bool blind = false; juce::Random blindRng;       // (the experiment's control: see chooseBlind)

    bool find (const juce::String& id, Instrument& out) const { return id.isNotEmpty() && lib.findLoose (id, out); }

    // every instrument's likeness to one, as a place in the queue: 0 = most like it, 1 = least
    std::map<juce::String, float> likenessTo (const Instrument& a, const std::vector<Instrument>& all) const
    {
        auto ensure = [this] (const juce::String& id) { if (asked.insert (id).second) timbre.add (id); return timbre.has (id); };
        std::vector<std::pair<double, juce::String>> d;
        const bool aHas = ensure (a.id);
        for (const auto& c : all)
        {
            if (c.id == a.id || ! c.f.valid) continue;
            double dist = (aHas && ensure (c.id)) ? timbre.distance (a.id, c.id) : -1.0;
            if (dist < 0.0)
            {   // no blocks for one of them: fall back on the stored band picture
                double s = 0; for (size_t k = 0; k < 24; ++k) { const double x = (double) a.f.bands[k] - c.f.bands[k]; s += x * x; }
                dist = 1.0e12 * std::sqrt (s / 24.0);
            }
            d.emplace_back (dist, c.id);
        }
        std::sort (d.begin(), d.end());
        std::map<juce::String, float> out;
        for (size_t i = 0; i < d.size(); ++i) out[d[i].second] = d.size() > 1 ? (float) i / (float) (d.size() - 1) : 0.0f;
        return out;
    }

    // how well an instrument would do one part beside what is already there (0 = not at all)
    Pick fitFor (const Instrument& c, const juce::String& role, const Profile& bed, const Instrument* main) const
    {
        Pick out; out.inst = c; out.role = role;
        const auto& f = c.f;
        const bool pitched = f.harmonicity >= 0.6f;
        const bool clean = f.loopClick <= 0.3f;
        if (role == "support")
        {
            if (! pitched || f.pitchSpread > 14.0f || ! clean || coverage (c, 0) < 0.6f) return out;
            const bool mainHolds = main == nullptr || main->f.sustain >= 0.35f;
            float env = f.sustain >= 0.3f ? 1.0f : (mainHolds ? 0.2f : 0.5f);       // under a pad, a body that holds; under a pluck either, a held one better
            if (main != nullptr && mainHolds && f.sustain >= 0.3f) env *= 1.0f - 0.5f * clamp01 (std::abs (std::log2 ((f.attackMs + 20.0f) / (main->f.attackMs + 20.0f))) / 5.0f);
            // under a sound that strikes and fades (a piano, a bell) the body should be soft: slow to speak and not bright
            if (main != nullptr && ! mainHolds && f.sustain >= 0.3f) env *= 0.55f + 0.45f * clamp01 (f.attackMs / 400.0f) * (1.0f - clamp01 ((f.brightness - 0.45f) / 0.4f));
            const auto prof = profileOf (c, 0.0f, 0);
            const float sh = shared (prof, bed);
            const float blend = 1.0f - clamp01 (std::abs (sh - 0.55f) / 0.55f);     // some common ground, not the same ground
            out.fit = 0.4f * env + 0.3f * blend + 0.15f * room (prof, bed) + 0.1f * clamp01 (f.width * 3.0f) + 0.05f;
            out.why = "it shares " + juce::String ((int) std::lround (sh * 100.0f)) + " % of the range already filled, so it blends without doubling";
        }
        else if (role == "transient")
        {
            if (f.sustain >= 0.25f || f.attackMs > 80.0f || ! pitched || coverage (c, 0) < 0.5f) return out;
            const auto prof = profileOf (c, 0.0f, 0);
            out.fit = 0.4f * clamp01 (f.transient) + 0.3f * (1.0f - shared (prof, bed)) + 0.2f * clamp01 ((brightnessOf (prof) - brightnessOf (bed) + 0.3f) / 0.6f) + 0.1f * clamp01 (f.velocityDb / 16.0f);
            out.why = (f.attackMs < 3.0f ? juce::String ("it speaks at once") : "it speaks in " + juce::String ((int) f.attackMs) + " ms") + " and is gone before the main sound has opened";
        }
        else if (role == "atmosphere")
        {
            const bool airy = f.attackMs >= 120.0f || f.movement >= 0.45f || c.category == "Soundscapes" || c.category == "Ambient" || c.category == "Vocal Textures" || c.category == "Vocal Pads" || c.category == "Pads";
            if (! airy || f.sustain < 0.25f || ! clean || coverage (c, 0) < 0.5f) return out;
            if (! pitched && c.category != "Vocal Textures" && c.category != "Soundscapes") return out;
            const auto prof = profileOf (c, 0.0f, 0);
            out.fit = 0.35f * clamp01 (f.movement * 1.2f) + 0.25f * (1.0f - 0.8f * shared (prof, bed)) + 0.2f * clamp01 (f.width * 3.0f) + 0.2f * clamp01 (f.attackMs / 1500.0f);
            out.why = (f.attackMs >= 400.0f ? "it takes " + juce::String (f.attackMs / 1000.0f, 1) + " s to arrive" : juce::String ("it keeps changing while held")) + " and sits where the others leave room";
        }
        else if (role == "shimmer")
        {
            const int semis = coverage (c, 12) >= 0.6f ? 12 : 0;
            if (! pitched || f.pitchSpread > 14.0f || ! clean || (semis == 0 && f.brightness < 0.5f) || coverage (c, semis) < 0.5f) return out;
            const auto prof = profileOf (c, 0.0f, semis);
            if (brightnessOf (prof) < 0.35f) return out;            // light on top has to be bright where it is played, whatever lies below
            out.fit = 0.4f * (1.0f - shared (prof, bed)) + 0.3f * clamp01 (brightnessOf (prof) + 0.1f) + 0.2f * room (prof, bed) + 0.1f * clamp01 (f.velocityDb / 16.0f);
            out.why = semis == 12 ? "played an octave up, above where the other layers lie" : "bright on its own, above where the other layers lie";
        }
        else if (role == "primary")
        {
            if (f.harmonicity < 0.8f || coverage (c, 0) < 0.75f || f.loopClick > 0.25f || f.pitchSpread > 12.0f || c.category == "Vocal Chops" || c.category == "Vocal Textures") return out;
            out.fit = 0.5f + 0.2f * clamp01 (f.velocityDb / 20.0f) + 0.15f * coverage (c, 0) + 0.15f * (main != nullptr && c.category != main->category ? 1.0f : 0.0f);
            out.why = "it can carry a sound on its own across the keyboard";
        }
        return out;
    }
};

Mutator::Mutator (const Library& library, Engine::Resolver resolver) : impl (new Impl (library, std::move (resolver))) {}
Mutator::~Mutator() = default;
void Mutator::chooseBlind (bool on, juce::int64 seed) { impl->blind = on; impl->blindRng.setSeed (seed); }

// ============================================================================================ reading a sound
juce::String Mutator::partOf (const Instrument& i)
{
    if (! i.f.valid) return "support";
    if (i.f.sustain < 0.2f && i.f.attackMs < 80.0f) return "transient";
    if (i.f.brightness > 0.62f && i.f.low < 0.04f && i.f.sustain < 0.5f) return "shimmer";
    if (i.f.attackMs >= 400.0f || i.f.movement >= 0.6f || i.f.harmonicity < 0.5f) return "atmosphere";
    return "support";
}

// the layer that carries the sound: the one named primary, else the loudest by measured loudness and its own level
int Mutator::mainLayer (const Preset& p) const
{
    int best = -1; float bestLevel = -1.0e9f;
    for (int l = 0; l < kLayers; ++l)
    {
        const auto& ls = p.layers[(size_t) l];
        if (! ls.used()) continue;
        if (ls.role == "primary") return l;
        Instrument inst;
        const float level = (impl->find (ls.instrument, inst) && inst.f.valid ? inst.f.loudnessDb : -24.0f) + ls.p[lpGain] - (ls.p[lpMute] > 0.5f ? 100.0f : 0.0f);
        if (level > bestLevel) { bestLevel = level; best = l; }
    }
    return best;
}

std::vector<Mutator::Pick> Mutator::candidates (const Preset& bedPreset, const juce::String& role, const Instrument* like, MutateMode mode, const juce::StringArray& avoid) const
{
    const auto all = impl->lib.all();
    Profile bed;
    std::set<juce::String> used;
    Instrument main; bool haveMain = false;
    const int mainIndex = mainLayer (bedPreset);
    for (int l = 0; l < kLayers; ++l)
    {
        const auto& ls = bedPreset.layers[(size_t) l];
        Instrument inst;
        if (! ls.used() || ! impl->find (ls.instrument, inst)) continue;
        used.insert (inst.id);
        if (inst.f.valid) add (bed, profileOf (inst, ls.p[lpGain], (int) ls.p[lpCoarse]));
        if (l == mainIndex) { main = inst; haveMain = true; }
    }
    if (impl->blind)
    {   // the experiment's control: any instrument at all, in no order
        std::vector<Pick> any;
        for (const auto& c : all)
        {
            if (! c.f.valid || c.hidden || used.count (c.id) > 0 || avoid.contains (c.id) || (like != nullptr && c.id == like->id)) continue;
            Pick pk; pk.inst = c; pk.role = role; pk.fit = 1.0f; pk.why = "drawn blind";
            any.push_back (std::move (pk));
        }
        for (int i = (int) any.size() - 1; i > 0; --i) std::swap (any[(size_t) i], any[(size_t) impl->blindRng.nextInt (i + 1)]);
        return any;
    }
    std::map<juce::String, float> queue;
    if (like != nullptr) queue = impl->likenessTo (*like, all);
    float lo = 0.0f, hi = 1.0f;
    if (like != nullptr) { if (mode == MutateMode::Subtle) hi = 0.22f; else if (mode == MutateMode::Evolve) { lo = 0.1f; hi = 0.6f; } else lo = 0.4f; }

    std::vector<Pick> out;
    for (int widen = 0; widen < 2 && out.size() < 3; ++widen)
    {
        out.clear();
        for (const auto& c : all)
        {
            if (! c.f.valid || c.hidden || used.count (c.id) > 0 || avoid.contains (c.id)) continue;
            if (like != nullptr && c.id == like->id) continue;
            const float place = like != nullptr && queue.count (c.id) > 0 ? queue[c.id] : 0.5f;
            if (widen == 0 && like != nullptr && (place < lo || place > hi)) continue;
            auto pick = impl->fitFor (c, role, bed, haveMain ? &main : nullptr);
            if (pick.fit <= 0.0f) continue;
            pick.likeness = 1.0f - place;
            out.push_back (std::move (pick));
        }
    }
    std::stable_sort (out.begin(), out.end(), [] (const Pick& a, const Pick& b) { return a.fit > b.fit; });
    return out;
}

std::vector<Instrument> Mutator::byLikeness (const Instrument& like, float lo, float hi) const
{
    const auto all = impl->lib.all();
    const auto queue = impl->likenessTo (like, all);
    std::vector<std::pair<float, Instrument>> in;
    for (const auto& c : all)
    {
        const auto it = queue.find (c.id);
        if (it == queue.end() || c.hidden || ! c.f.valid || it->second < lo || it->second > hi) continue;
        in.emplace_back (it->second, c);
    }
    std::stable_sort (in.begin(), in.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<Instrument> out;
    for (auto& i : in) out.push_back (std::move (i.second));
    return out;
}

// ============================================================================================ setting a layer up for its part
void Mutator::place (Preset& p, int layer, const Instrument& inst, const juce::String& role, juce::Random& rng) const
{
    auto& l = p.layers[(size_t) layer];
    l = LayerState();
    l.instrument = inst.id; l.role = role;
    l.p[lpOn] = 1.0f;
    const auto& f = inst.f;
    Instrument main; bool haveMain = false;
    const int mi = mainLayer (p);
    if (mi >= 0 && mi != layer) haveMain = impl->find (p.layers[(size_t) mi].instrument, main);

    l.p[lpGain] = juce::jlimit (-40.0f, 9.0f, std::round ((levelFor (inst) + partLevel (role)) * 2.0f) * 0.5f);
    l.p[lpSendC] = 0.3f;
    // tuning: every pitched layer agrees with the main one (as the main one is set now)
    if (haveMain && tuningKnown (inst) && tuningKnown (main))
    {
        const float want = main.f.tuneCents + p.layers[(size_t) mi].p[lpFine] - f.tuneCents;
        if (std::abs (want) >= 3.0f) l.p[lpFine] = juce::jlimit (-60.0f, 60.0f, std::round (want));
    }
    // its release reaches as far as the main sound's, so no layer stops short
    if (haveMain && role != "transient" && f.releaseMs < 0.5f * main.f.releaseMs) l.p[lpRelease] = juce::jlimit (0.0f, 0.8f, std::sqrt ((main.f.releaseMs - f.releaseMs) / 10000.0f));
    // its range is what it can play
    l.p[lpKeyLo] = (float) juce::jlimit (0, 127, inst.loKey); l.p[lpKeyHi] = (float) juce::jlimit (0, 127, inst.hiKey);

    if (role == "support")
    {
        if (f.low > 0.25f) { l.p[lpFilterType] = 3.0f; l.p[lpCutoff] = 140.0f; }
        else if (f.brightness > 0.8f) { l.p[lpFilterType] = 1.0f; l.p[lpCutoff] = 6500.0f; }
        l.p[lpWidth] = 1.2f; l.p[lpPan] = rng.nextBool() ? 0.15f : -0.15f;
        l.p[lpSendC] = 0.35f;
        if (f.width < 0.05f) l.p[lpSendA] = 0.25f;                          // a mono source is given a little width by the first send
        if (haveMain && main.f.attackMs >= 300.0f && f.attackMs < 60.0f) l.p[lpAttack] = 0.2f;
    }
    else if (role == "transient")
    {
        if (f.low > 0.3f) { l.p[lpFilterType] = 3.0f; l.p[lpCutoff] = 110.0f; }
        l.p[lpSendC] = 0.22f; l.p[lpSendB] = 0.2f;
    }
    else if (role == "atmosphere")
    {
        l.p[lpFilterType] = 3.0f; l.p[lpCutoff] = f.harmonicity < 0.5f ? 320.0f : 190.0f;
        l.p[lpAttack] = juce::jmax (0.36f, l.p[lpAttack]);
        l.p[lpRelease] = juce::jmax (0.35f, l.p[lpRelease]);
        l.p[lpWidth] = 1.5f; l.p[lpSendC] = 0.65f;
        l.p[lpVelSens] = 0.4f;
        l.p[lpLfoShape] = 4.0f; l.p[lpLfoRate] = 0.12f + 0.1f * rng.nextFloat(); l.p[lpLfoPan] = 0.35f; l.p[lpLfoAmp] = 0.15f;
        if (f.harmonicity < 0.5f) l.p[lpGain] -= 4.0f;                       // a texture without pitch stays further back
    }
    else if (role == "shimmer")
    {
        const bool up = coverage (inst, 12) >= 0.6f;
        if (up) { l.p[lpCoarse] = 12.0f; l.p[lpKeyLo] = (float) juce::jlimit (0, 127, inst.loKey - 12); l.p[lpKeyHi] = (float) juce::jlimit (0, 127, inst.hiKey - 12); }
        l.p[lpFilterType] = 3.0f; l.p[lpCutoff] = 900.0f;
        l.p[lpAttack] = f.sustain >= 0.3f ? 0.25f : 0.0f;
        l.p[lpWidth] = 1.3f; l.p[lpSendC] = 0.6f; l.p[lpSendB] = 0.2f;
        // it comes in with a harder touch: the sound brightens as it is played harder
        l.p[lpVelLo] = 40.0f; l.velFadeIn = 45.0f;
    }
}

// aims the four macros at what this sound actually has
void Mutator::autoMacros (Preset& p) const
{
    const int mi = mainLayer (p);
    int atmos = -1, shim = -1;
    for (int l = 0; l < kLayers; ++l) { if (! p.layers[(size_t) l].used()) continue; if (p.layers[(size_t) l].role == "atmosphere") atmos = l; if (p.layers[(size_t) l].role == "shimmer") shim = l; }
    auto aim = [] (int layer, const char* param) { return "L" + juce::String (layer + 1) + "." + param; };
    for (auto& m : p.macros) { m.targets.clear(); m.value = 0.0f; }
    p.macros[0].name = "TONE"; p.macros[1].name = "MOTION"; p.macros[2].name = "AIR"; p.macros[3].name = "SPACE";
    if (mi >= 0)
    {
        auto& main = p.layers[(size_t) mi];
        if (main.p[lpFilterType] < 0.5f) { main.p[lpFilterType] = 1.0f; main.p[lpCutoff] = 20000.0f; }
        if (main.p[lpFilterType] < 2.5f) p.macros[0].targets.push_back ({ aim (mi, "cutoff"), -0.5f });       // TONE closes the main sound's filter
        if (shim >= 0) p.macros[0].targets.push_back ({ aim (shim, "gain"), -0.12f });
        const int mover = atmos >= 0 ? atmos : mi;                                                           // MOTION: the slow layer if there is one
        p.macros[1].targets.push_back ({ aim (mover, "lfoAmp"), 0.45f });
        p.macros[1].targets.push_back ({ aim (mover, "lfoPan"), 0.5f });
        if (mover == mi && main.p[lpLfoRate] > 2.0f) main.p[lpLfoRate] = 0.5f;
        if (shim >= 0) p.macros[2].targets.push_back ({ aim (shim, "gain"), 0.11f });                         // AIR: more of the light on top
        else { p.macros[2].targets.push_back ({ aim (mi, "width"), 0.25f }); p.macros[2].targets.push_back ({ aim (mi, "sendA"), 0.35f }); }
        if (atmos >= 0) p.macros[2].targets.push_back ({ aim (atmos, "gain"), 0.07f });
    }
    p.macros[3].targets.push_back ({ "R3", 0.6f });                                                           // SPACE: more of the room and the echo
    p.macros[3].targets.push_back ({ "R2", 0.3f });
}

// ============================================================================================ listening
Heard Mutator::listen (const Preset& original, int soloLayer) const
{
    Heard h;
    Preset p = original;
    // the true peak, before the limiter and the squeeze: the master level is set in front of them, by what goes into them.
    // Whatever else stands on the master (a rack of SPECTRAL FX, an equaliser) is part of the sound, and is heard.
    for (auto& m : p.master) if (m.unit == "lim" || m.unit == "ott") m.on = false;
    if (soloLayer >= 0) for (int l = 0; l < kLayers; ++l) if (l != soloLayer) p.layers[(size_t) l].p[lpOn] = 0.0f;
    const int mi = soloLayer >= 0 ? soloLayer : mainLayer (p);
    if (mi < 0) return h;
    // notes the main layer can actually play
    Instrument main; const bool known = impl->find (p.layers[(size_t) mi].instrument, main);
    const auto& ml = p.layers[(size_t) mi];
    auto playable = [&] (int want) {
        for (int d = 0; d < 48; ++d)
            for (int k : { want - d, want + d })
                if (k >= (int) ml.p[lpKeyLo] && k <= (int) ml.p[lpKeyHi] && (! known || main.plays (k + (int) ml.p[lpCoarse]))) return k;
        return want;
    };
    const int mid = playable (60), low = playable (mid - 12), high = playable (mid + 12);
    h.midNote = mid;
    std::vector<Note> notes { { mid, 0.8f, 0.0, 2.0 }, { low, 0.8f, 3.0, 1.5 }, { high, 0.8f, 5.5, 1.5 }, { mid, 0.35f, 8.0, 1.0 }, { mid, 1.0f, 10.0, 1.0 } };
    int chord[5] = { low, playable (low + 7), mid, playable (mid + 4), playable (mid + 7) };
    for (int i = 0; i < 5; ++i) notes.push_back ({ chord[i], 0.75f, 12.0 + 0.01 * i, 3.0 });
    const double fs = 44100.0;
    h.audio = Engine::bounce (p, impl->resolver, notes, 17.5, fs, true);
    auto at = [fs] (double t) { return (int) (t * fs); };
    const auto& a = h.audio;
    for (int c = 0; c < a.getNumChannels(); ++c) { const float* x = a.getReadPointer (c); for (int i = 0; i < a.getNumSamples(); ++i) if (! std::isfinite (x[i])) return h; }
    h.peakDb = gainToDb (juce::jmax (1.0e-6f, peakOf (a)));
    auto loudest = [&] (double t0, double t1) { float best = -120.0f; for (int s = at (t0); s + at (0.4) <= at (t1); s += at (0.1)) best = juce::jmax (best, loudnessDb (a, fs, s, at (0.4))); return best; };
    h.onsetDb = loudest (12.0, 15.0);
    h.loudnessDb = loudnessDb (a, fs, at (12.0), at (3.0));       // the whole held chord: its start and its body together
    const auto chordMono = monoOf (a, at (12.3), at (2.5));
    const auto sp = spectrumOf (chordMono.data(), (int) chordMono.size(), fs, 12);
    h.lowShare = (float) shareBetween (sp, 0.0f, 150.0f);
    for (size_t b = 0; b < 24; ++b) h.bands[b] = (float) sp.band[b];
    h.monoLossDb = monoLossDb (a, at (12.3), at (2.5));
    h.ringDb = ringOf (sp);
    { const auto m = monoOf (a, at (0.03), at (1.95)); h.tuneCents = tuningOf (m.data(), (int) m.size(), fs, midiToHz ((float) mid), h.tuneStrength); }
    { const float l = loudest (3.0, 4.6), m = loudest (0.0, 2.0), hi = loudest (5.5, 7.1); h.registerSpreadDb = juce::jmax (l, m, hi) - juce::jmin (l, m, hi); }
    h.touchDb = gainToDb (juce::jmax (1.0e-6f, rmsOf (a, at (10.03), at (0.9)))) - gainToDb (juce::jmax (1.0e-6f, rmsOf (a, at (8.03), at (0.9))));
    h.seams = seamsOf (monoOf (a, at (12.8), at (2.0)));
    h.holds = h.onsetDb - h.loudnessDb < 5.0f;                   // (the three seconds average within 5 dB of the loudest moment)
    h.ok = h.loudnessDb > -70.0f;
    return h;
}

// a ring is a narrow band the layering has added: it is measured against what the main sound has on its own
// (a pure tone's partials stand alone above their neighbours, and that is the sound, not a fault)
// A struck or plucked sound is made of partials that stand alone and ring - that is what a bell is - so it is given more room.
// And a ring is something the layering made: a sound is not blamed for a ring one of its instruments has on its own
// (singing glasses and bowed bars are single pure tones by nature). `own`: the sharpest of the instruments' own rings.
static float ringLimit (float own, bool holds) { return holds ? juce::jmax (11.0f, own + 2.0f) : juce::jmax (15.0f, own + 6.0f); }

juce::StringArray Mutator::checkNames()
{
    return { "It sounds", "Nothing clips", "The low end is not piling up", "The main sound is not buried", "No harsh ring", "In tune", "Holds up in mono", "No loop seams", "Even across the keys" };
}

// the sharpest ring any of the sound's instruments has on its own (measured when each was listened to for the library),
// or the main sound's as heard just now if that is sharper
float Mutator::ringAllowed (float own, bool holds) { return ringLimit (own, holds); }
float Mutator::ownRing (const Preset& p, const Heard* mainAlone) const
{
    float own = mainAlone != nullptr && mainAlone->ok ? mainAlone->ringDb : -100.0f;
    for (const auto& l : p.layers) { Instrument inst; if (l.used() && impl->find (l.instrument, inst) && inst.f.valid) own = juce::jmax (own, inst.f.ring); }
    return own;
}

std::vector<QualityCheck> Mutator::judge (const Heard& mix, const Heard* mainAlone, const Preset& p) const
{
    std::vector<QualityCheck> c;
    auto db = [] (float v) { return juce::String (v, 1) + " dB"; };
    c.push_back ({ "It sounds", mix.ok, mix.ok ? "a chord reads " + db (mix.loudnessDb) : juce::String ("nothing came out") });
    c.push_back ({ "Nothing clips", mix.peakDb <= -0.5f, "the loudest moment peaks at " + db (mix.peakDb) });
    const float lowLimit = juce::jmax (0.5f, (mainAlone != nullptr ? mainAlone->lowShare : 0.3f) + 0.2f);
    c.push_back ({ "The low end is not piling up", mix.lowShare <= lowLimit, juce::String ((int) std::lround (mix.lowShare * 100.0f)) + " % of a chord lies under 150 Hz" });
    if (mainAlone != nullptr && mainAlone->ok)
    {
        const float behind = mix.loudnessDb - mainAlone->loudnessDb;
        c.push_back ({ "The main sound is not buried", behind <= 7.0f, "the other layers add " + db (juce::jmax (0.0f, behind)) + " to it" });
    }
    const float own = ownRing (p, mainAlone);
    // (kept short: the panel shows it on one small line)
    c.push_back ({ "No harsh ring", mix.ringDb <= ringLimit (own, mix.holds), own > 9.0f ? "sharpest band " + db (mix.ringDb) + " above its neighbours (" + db (own) + " alone)"
                                                                                          : "the sharpest band stands " + db (mix.ringDb) + " above its neighbours" });
    const bool pitched = mix.tuneStrength >= 0.35f;        // (a weak reading - a tremolo, a noise with a hint of pitch - is not a tuning)
    // (a plucked or struck note starts a little off and settles: it is given more room than a held one)
    c.push_back ({ "In tune", ! pitched || std::abs (mix.tuneCents) <= (mix.holds ? 12.0f : 20.0f), pitched ? juce::String (mix.tuneCents > 0 ? "+" : "") + juce::String (mix.tuneCents, 1) + " cents from concert pitch" : juce::String ("no single pitch to measure") });
    c.push_back ({ "Holds up in mono", mix.monoLossDb <= 3.0f, "folding it to mono costs " + db (mix.monoLossDb) });
    // (only a held sound can show a loop's seam: in one that is struck and fades, echoes of its own attack would be mistaken for one)
    c.push_back ({ "No loop seams", ! mix.holds || mix.seams <= 0.3f, ! mix.holds ? juce::String ("nothing is held long enough to loop") : (mix.seams <= 0.05f ? juce::String ("clean") : "a seam reads " + juce::String (mix.seams, 2)) });
    c.push_back ({ "Even across the keys", mix.registerSpreadDb <= 13.0f, "low, middle and high notes lie within " + db (mix.registerSpreadDb) });
    return c;
}

// listens, puts right what can be put right, and listens again
bool Mutator::refine (Preset& p, juce::StringArray& notes, Heard& mix, std::vector<QualityCheck>& checks) const
{
    const int mi = mainLayer (p);
    if (mi < 0) return false;
    Heard aloneThen, alone;
    bool haveAlone = false;
    float masterThen = 0.0f;
    auto addNote = [&notes] (const juce::String& s) { if (! notes.contains (s)) notes.add (s); };
    for (int pass = 0; pass < 4; ++pass)
    {
        mix = listen (p);
        if (! mix.ok) { checks = judge (mix, nullptr, p); return false; }
        if (! haveAlone) { aloneThen = listen (p, mi); masterThen = p.masterDb; haveAlone = true; }
        alone = aloneThen;                                    // (the master level may have moved since: it moves the main sound alone just as much)
        alone.loudnessDb += p.masterDb - masterThen; alone.onsetDb += p.masterDb - masterThen; alone.peakDb += p.masterDb - masterThen;
        checks = judge (mix, &alone, p);
        bool allPassed = true; for (const auto& c : checks) allPassed = allPassed && c.passed;
        if (allPassed && mix.peakDb > -9.0f) return true;
        if (pass == 3) break;

        bool changed = false;
        const float behind = mix.loudnessDb - alone.loudnessDb;
        if (behind > 7.0f)
        {
            const float down = juce::jmin (9.0f, behind - 4.5f);
            for (int l = 0; l < kLayers; ++l) if (l != mi && p.layers[(size_t) l].used()) p.layers[(size_t) l].p[lpGain] = juce::jmax (-48.0f, p.layers[(size_t) l].p[lpGain] - down);
            addNote ("Brought the other layers down so the main sound stays in front");
            changed = true;
        }
        const float lowLimit = juce::jmax (0.5f, alone.lowShare + 0.2f);
        if (mix.lowShare > lowLimit)
        {
            for (int l = 0; l < kLayers; ++l)
            {
                auto& ls = p.layers[(size_t) l];
                if (l == mi || ! ls.used()) continue;
                if (ls.p[lpFilterType] > 2.5f && ls.p[lpFilterType] < 3.5f) ls.p[lpCutoff] = juce::jmin (900.0f, ls.p[lpCutoff] * 1.6f);
                else if (ls.p[lpFilterType] < 0.5f) { ls.p[lpFilterType] = 3.0f; ls.p[lpCutoff] = 170.0f; }
                else ls.p[lpGain] -= 2.0f;
            }
            addNote ("Cut the low end of the other layers so it does not pile up under the main sound");
            changed = true;
        }
        if (mix.monoLossDb > 3.0f)
        {
            for (int l = 0; l < kLayers; ++l) { auto& ls = p.layers[(size_t) l]; if (ls.used()) { ls.p[lpWidth] = juce::jmax (0.5f, ls.p[lpWidth] * 0.65f); ls.p[lpSendA] *= 0.5f; } }
            addNote ("Narrowed the layers so the sound holds up in mono");
            changed = true;
        }
        if (mix.ringDb > ringLimit (ownRing (p, &alone), mix.holds))
        {
            int brightest = -1; float top = -1.0f;
            for (int l = 0; l < kLayers; ++l)
            {
                Instrument inst;
                if (l == mi || ! p.layers[(size_t) l].used() || ! impl->find (p.layers[(size_t) l].instrument, inst)) continue;
                if (inst.f.brightness > top) { top = inst.f.brightness; brightest = l; }
            }
            if (brightest >= 0) { p.layers[(size_t) brightest].p[lpGain] -= 3.0f; addNote ("Turned the brightest layer down to take a ring out"); changed = true; }
        }
        // level: the loudest moment sits a little under full scale, neither clipping nor far below
        if (mix.peakDb > -0.5f || mix.peakDb < -9.0f)
        {
            const float move = juce::jlimit (-18.0f, 12.0f, -3.0f - mix.peakDb);
            p.masterDb = juce::jlimit (-30.0f, 12.0f, p.masterDb + move);
            if (mix.peakDb > -0.5f) addNote ("Turned the whole sound down so nothing clips");
            changed = true;
        }
        if (! changed) break;
    }
    for (const auto& c : checks) if (! c.passed) return false;
    return true;
}

// ============================================================================================ a hand-written sound made ready
bool Mutator::polish (Preset& p, juce::StringArray& notes, std::vector<QualityCheck>& checks, Heard& heard, juce::String& missing, float targetDb, const juce::StringArray& setAside) const
{
    const int mi = mainLayer (p);
    if (mi < 0) { missing = "no layer"; return false; }
    for (int l = 0; l < kLayers; ++l)
    {
        auto& ls = p.layers[(size_t) l];
        if (! ls.used()) continue;
        Instrument inst;
        if (! impl->find (ls.instrument, inst)) { missing = ls.instrument; return false; }
        if (ls.role.isEmpty()) ls.role = l == mi ? juce::String ("primary") : partOf (inst);
        ls.p[lpGain] = juce::jlimit (-40.0f, 9.0f, std::round ((levelFor (inst) + partLevel (ls.role) + ls.p[lpGain]) * 2.0f) * 0.5f);
        if (tuningKnown (inst) && std::abs (inst.f.tuneCents) >= 3.0f && std::abs (inst.f.tuneCents) <= 60.0f)
            ls.p[lpFine] = juce::jlimit (-100.0f, 100.0f, ls.p[lpFine] - std::round (inst.f.tuneCents));
    }
    bool aimed = false; for (const auto& m : p.macros) aimed = aimed || ! m.targets.empty();
    if (! aimed) autoMacros (p);
    if (! refine (p, notes, heard, checks))
    {
        if (! heard.ok) return false;
        for (const auto& c : checks) if (! c.passed && ! setAside.contains (c.name)) return false;
    }
    // one loudness for all, short of clipping. A sound whose attack stands very tall over its body (a struck bell) would stay
    // far quieter than the rest: when the master ends in its limiter, such a sound may lean up to 4 dB on it.
    bool limited = false;
    for (const auto& m : p.master) limited = limited || (m.unit == "lim" && m.on);
    const float lean = limited ? juce::jlimit (0.0f, 4.0f, (heard.peakDb - heard.onsetDb) - 15.0f) : 0.0f;
    const float move = juce::jlimit (-24.0f, 24.0f, juce::jmin (targetDb - heard.onsetDb, -1.5f + lean - heard.peakDb));
    p.masterDb = juce::jlimit (-30.0f, 12.0f, std::round ((p.masterDb + move) * 10.0f) * 0.1f);
    if (lean > 0.5f && heard.peakDb + move > -1.0f) notes.add ("its attack leans " + juce::String (heard.peakDb + move + 1.5f, 1) + " dB on the master limiter");
    heard.onsetDb += move; heard.loudnessDb += move; heard.peakDb += move;
    return true;
}

// ============================================================================================ the baseline MUTATE is measured against
Preset Mutator::randomLayering (const Preset& original, int layers, juce::Random& rng) const
{
    Preset p = original;
    const int mi = mainLayer (p);
    const auto all = impl->lib.all();
    std::vector<Instrument> pool;
    for (const auto& i : all) if (i.f.valid && ! i.hidden) pool.push_back (i);
    int added = 0;
    for (int l = 0; l < kLayers && added < layers - 1; ++l)
    {
        if (l == mi) continue;
        p.layers[(size_t) l] = LayerState();
        if (pool.empty()) continue;
        const auto& inst = pool[(size_t) rng.nextInt ((int) pool.size())];
        auto& ls = p.layers[(size_t) l];
        ls.instrument = inst.id; ls.p[lpOn] = 1.0f; ls.p[lpGain] = levelFor (inst); ls.p[lpSendC] = 0.3f;     // level matched, nothing else
        ++added;
    }
    for (int l = 0; l < kLayers; ++l) if (l != mi && p.layers[(size_t) l].instrument.isEmpty()) p.layers[(size_t) l] = LayerState();
    p.name = original.name + " (random layers)";
    return p;
}

// ============================================================================================ the whole thing
Mutation Mutator::mutate (const Preset& original, MutateMode mode, juce::uint32 seed, const Progress& progress)
{
    Mutation out;
    juce::Random rng ((juce::int64) seed * 2654435761LL + 17);
    auto say = [&progress] (float f, const juce::String& s) { return progress ? progress (f, s) : true; };
    const int mi = mainLayer (original);
    Instrument main;
    if (mi < 0 || ! impl->find (original.layers[(size_t) mi].instrument, main)) { out.failure = "This sound has no layer from the library to start from."; return out; }
    if (impl->lib.size() < 4) { out.failure = "The library needs a few more instruments before MUTATE has anything to choose from."; return out; }

    if (! say (0.05f, "Listening to the sound as it is")) return out;
    const Heard before = listen (original);
    out.originalLoudnessDb = before.loudnessDb;

    // among the best few, by fit (the better the fit the likelier); a wider, flatter draw when a whole new main sound is wanted
    auto choose = [&rng] (const std::vector<Pick>& picks, int among = 4, double sharpness = 3.0) -> const Pick* {
        if (picks.empty()) return nullptr;
        const int n = juce::jmin (among, (int) picks.size());
        double total = 0; for (int i = 0; i < n; ++i) total += std::pow ((double) picks[(size_t) i].fit, sharpness);
        double r = rng.nextDouble() * total;
        for (int i = 0; i < n; ++i) { r -= std::pow ((double) picks[(size_t) i].fit, sharpness); if (r <= 0) return &picks[(size_t) i]; }
        return &picks.front();
    };
    juce::StringArray avoid;                                   // WILD leaves the original's instruments behind
    if (mode == MutateMode::Wild) for (const auto& l : original.layers) { Instrument i; if (l.used() && impl->find (l.instrument, i)) avoid.add (i.id); }
    auto freeLayer = [] (const Preset& p) { for (int l = 0; l < kLayers; ++l) if (! p.layers[(size_t) l].used()) return l; return -1; };
    auto describe = [] (const juce::String& role) {
        if (role == "support") return juce::String ("as a second body");
        if (role == "transient") return juce::String ("to give each note an edge");
        if (role == "atmosphere") return juce::String ("as slow air behind");
        if (role == "shimmer") return juce::String ("as light on top");
        return juce::String ("as the main sound");
    };

    const int attempts = 4;
    for (int attempt = 0; attempt < attempts; ++attempt)
    {
        ++out.tried;
        if (! say (0.12f + 0.8f * (float) attempt / (float) attempts, attempt == 0 ? "Choosing layers that fit" : "That one did not hold up: trying another")) return out;
        Preset cand = original;
        juce::StringArray changes;
        auto addPart = [&] (const juce::String& role, const Instrument* like) {
            const int slot = freeLayer (cand);
            if (slot < 0) return false;
            const auto picks = candidates (cand, role, like, mode, avoid);
            const Pick* pk = choose (picks);
            if (pk == nullptr) return false;
            place (cand, slot, pk->inst, role, rng);
            juce::String line = "Added " + pk->inst.name + " " + describe (role) + ": " + pk->why;
            if (std::abs (cand.layers[(size_t) slot].p[lpFine]) >= 3.0f) line << "; tuned " << (cand.layers[(size_t) slot].p[lpFine] > 0 ? "+" : "") << (int) cand.layers[(size_t) slot].p[lpFine] << " cents to agree";
            changes.add (line);
            return true;
        };
        const bool mainHolds = main.f.sustain >= 0.35f;

        if (mode == MutateMode::Subtle)
        {
            // one change: a close relative in place of a layer, or one quiet layer more; and small moves of what is there
            std::vector<int> others;
            for (int l = 0; l < kLayers; ++l) if (l != mi && cand.layers[(size_t) l].used()) others.push_back (l);
            bool done = false;
            if (! others.empty() && (cand.layerCount() >= 4 || rng.nextFloat() < 0.6f))
            {
                const int l = others[(size_t) rng.nextInt ((int) others.size())];
                Instrument old;
                if (impl->find (cand.layers[(size_t) l].instrument, old))
                {
                    const auto role = cand.layers[(size_t) l].role.isNotEmpty() ? cand.layers[(size_t) l].role : partOf (old);
                    const auto keep = cand.layers[(size_t) l];
                    cand.layers[(size_t) l] = LayerState();
                    const auto picks = candidates (cand, role, &old, mode);
                    if (const Pick* pk = choose (picks))
                    {
                        place (cand, l, pk->inst, role, rng);
                        cand.layers[(size_t) l].p[lpGain] = juce::jlimit (-40.0f, 9.0f, keep.p[lpGain] + (levelFor (pk->inst) - levelFor (old)));      // it takes the old one's place at the old one's loudness
                        changes.add ("Swapped " + old.name + " for " + pk->inst.name + ", a close relative that does the same job");
                        done = true;
                    }
                    else cand.layers[(size_t) l] = keep;
                }
            }
            if (! done)
            {
                juce::StringArray missing { "atmosphere", "shimmer", "support", "transient" };
                for (int l = 0; l < kLayers; ++l) if (cand.layers[(size_t) l].used()) missing.removeString (cand.layers[(size_t) l].role);
                if (! mainHolds) missing.removeString ("transient");
                for (const auto& role : missing) if (addPart (role, &main)) { done = true; break; }
            }
            for (int l = 0; l < kLayers; ++l)
            {
                auto& ls = cand.layers[(size_t) l];
                if (! ls.used() || l == mi) continue;
                ls.p[lpSendC] = clamp01 (ls.p[lpSendC] + (rng.nextFloat() - 0.5f) * 0.14f);
                ls.p[lpWidth] = juce::jlimit (0.6f, 1.7f, ls.p[lpWidth] + (rng.nextFloat() - 0.5f) * 0.2f);
                if (ls.p[lpFilterType] > 0.5f) ls.p[lpCutoff] = juce::jlimit (60.0f, 18000.0f, ls.p[lpCutoff] * std::pow (2.0f, (rng.nextFloat() - 0.5f) * 0.4f));
            }
            if (! done) { ++out.rejected; continue; }
            changes.insert (0, "Kept " + main.name + " as the main sound");
            cand.name = original.name + " II";
        }
        else if (mode == MutateMode::Evolve)
        {
            // the main sound stays; everything around it is chosen again
            for (int l = 0; l < kLayers; ++l) if (l != mi) cand.layers[(size_t) l] = LayerState();
            cand.layers[(size_t) mi].role = "primary";
            changes.add ("Kept " + main.name + " as the main sound and rebuilt everything around it");
            // a main sound that sits off its key is brought onto it (the layers chosen next agree with it as it is then)
            {
                auto& ml = cand.layers[(size_t) mi];
                const float off = main.f.tuneCents + ml.p[lpFine];
                if (tuningKnown (main) && std::abs (off) >= 5.0f && std::abs (main.f.tuneCents) <= 60.0f)
                {
                    ml.p[lpFine] = juce::jlimit (-60.0f, 60.0f, -std::round (main.f.tuneCents));
                    changes.add ("Brought " + main.name + " onto its key (it sat " + juce::String ((int) std::lround (std::abs (off))) + " cents " + (off > 0 ? "sharp" : "flat") + ")");
                }
            }
            juce::StringArray parts = mainHolds ? juce::StringArray { "atmosphere", "shimmer", "transient", "support" } : juce::StringArray { "support", "atmosphere", "shimmer" };
            const int want = 2 + (rng.nextFloat() < 0.45f ? 1 : 0);
            // shuffle a little, keeping the first choice likely
            if (rng.nextFloat() < 0.5f && parts.size() > 2) parts.move (2, 0);
            if (rng.nextFloat() < 0.35f && parts.size() > 1) parts.move (1, 0);
            int added = 0;
            for (const auto& role : parts) { if (added >= want) break; if (addPart (role, &main)) ++added; }
            if (added == 0) { ++out.rejected; continue; }
            cand.name = original.name + ", evolved";
        }
        else
        {
            // a new main sound from far away, and new layers for it
            Preset empty = Preset::blank();
            empty.sends = original.sends; empty.returns = original.returns; empty.master = original.master;
            cand = empty;
            const auto mains = candidates (cand, "primary", &main, mode, avoid);
            const Pick* pk = choose (mains, 12, 1.0);
            if (pk == nullptr) { ++out.rejected; continue; }
            place (cand, 0, pk->inst, "primary", rng);
            const Instrument newMain = pk->inst;
            // a new sound starts at concert pitch: the main layer is brought there, and the others then agree with it
            if (tuningKnown (newMain) && std::abs (newMain.f.tuneCents) >= 3.0f) cand.layers[0].p[lpFine] = juce::jlimit (-60.0f, 60.0f, -std::round (newMain.f.tuneCents));
            changes.add ("Sailed far from " + main.name + ": " + newMain.name + " carries the sound now");
            const bool holds = newMain.f.sustain >= 0.35f;
            juce::StringArray parts = holds ? juce::StringArray { "atmosphere", "shimmer", "transient" } : juce::StringArray { "support", "atmosphere", "shimmer" };
            if (rng.nextFloat() < 0.4f) parts.move (1, 0);
            const int want = 2 + (rng.nextFloat() < 0.5f ? 1 : 0);
            int added = 0;
            for (const auto& role : parts) { if (added >= want) break; if (addPart (role, &newMain)) ++added; }
            // one bolder touch on the slow layer: a voice has its vowels moved; anything else may be turned backwards
            for (int l = 1; l < kLayers; ++l)
            {
                auto& ls = cand.layers[(size_t) l];
                Instrument inst;
                if (! ls.used() || ls.role != "atmosphere" || ! impl->find (ls.instrument, inst)) continue;
                const bool voice = inst.category == "Choirs" || inst.category == "Vocal Pads" || inst.category == "Vocal Textures";
                if (voice && rng.nextFloat() < 0.6f)
                {
                    const float vowels = (rng.nextBool() ? 1.0f : -1.0f) * (float) (3 + rng.nextInt (3));
                    ls.inserts[0] = fxDefault ("shift"); ls.inserts[0].values["formant"] = vowels;
                    changes.add ("Moved the vowels of " + inst.name + " " + (vowels > 0 ? "up " : "down ") + juce::String ((int) std::abs (vowels)) + " semitones without moving its pitch");
                }
                else if (rng.nextFloat() < 0.45f)
                {
                    ls.reverse = true; ls.loopMode = 1; ls.loopStart = 0.2f; ls.loopEnd = 0.9f; ls.loopFade = 0.3f;
                    changes.add ("Turned " + inst.name + " backwards and looped it, so it swells instead of fading");
                }
                break;
            }
            cand.category = newMain.category;
            cand.world.clear();
            cand.tags = newMain.tags;
            cand.name = juce::String (moodWords[rng.nextInt ((int) std::size (moodWords))]) + " " + nounFor (newMain.category);
            cand.description = "Grown by MUTATE from " + original.name;
        }
        cand.author = "MUTATE"; cand.factory = false; cand.file.clear();
        if (mode != MutateMode::Subtle) autoMacros (cand);

        if (! say (0.2f + 0.8f * ((float) attempt + 0.5f) / (float) attempts, "Playing it and listening")) return out;
        Heard heard;
        std::vector<QualityCheck> checks;
        juce::StringArray notes;
        if (! refine (cand, notes, heard, checks))
        {
            ++out.rejected;
            out.checks = checks;                           // (kept in case every attempt fails: the last reason is shown)
            continue;
        }
        // as loud as the original, so the two can be compared fairly (never past the point where it would clip)
        const float want = before.loudnessDb - heard.loudnessDb;
        const float move = juce::jlimit (-30.0f, juce::jmax (0.0f, -1.0f - heard.peakDb), want);
        if (before.ok) cand.masterDb = juce::jlimit (-30.0f, 12.0f, cand.masterDb + move);
        out.loudnessDb = heard.loudnessDb + (before.ok ? move : 0.0f);
        out.originalTrimDb = before.ok ? juce::jmin (0.0f, move - want) : 0.0f;
        changes.addArray (notes);
        out.ok = true; out.preset = cand; out.changes = changes; out.checks = checks;
        say (1.0f, "Done");
        return out;
    }
    juce::String why;
    for (const auto& c : out.checks) if (! c.passed) why << (why.isEmpty() ? "" : "; ") << c.name.toLowerCase() << " (" << c.found << ")";
    out.failure = "Nothing that was tried held up" + (why.isNotEmpty() ? ": the last one failed on " + why : juce::String (".")) + " Press MUTATE again for other choices.";
    return out;
}
} // namespace atlas
