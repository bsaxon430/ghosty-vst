// CHAIN BRAIN v1: rule tables (original), seeded variation (GPFX_BOX builds only).
#include "FxBrain.h"
#if GPFX_BOX
#include <map>

namespace gpfx {
namespace brain {

namespace {
struct Range { const char* key; float lo, hi; };
struct Step { int unit; float mixLo, mixHi; std::vector<Range> p; };
struct Recipe { const char* name; std::vector<Step> steps; const char* why = ""; };

const std::vector<Recipe>& recipes (int goal)
{
    static const std::array<std::vector<Recipe>, 8> all {
        std::vector<Recipe> {   // AGGRESSIVE
            { "Crackle Rage", { { uCrackle, 1, 1, { { "bands", 1, 1 }, { "b1_curve", ccFuzz, ccFuzz }, { "b1_drive", 0.5f, 0.7f }, { "b2_curve", ccCracked, ccCracked }, { "b2_drive", 0.4f, 0.7f } } },
                                { uOtt, 0.3f, 0.5f, {} } }, "two bands of fuzz and cracked speaker, then squeezed: nasty and in your face" },
            { "Pentode Smash", { { uWreck, 1, 1, { { "style", wsPentode, wsPentode }, { "drive", 0.55f, 0.8f }, { "punish", 1, 1 } } },
                                 { uCrush, 0.45f, 0.65f, { { "crush", 0.5f, 0.7f }, { "dark", 0.15f, 0.3f } } } }, "pushed pentode saturation with PUNISH, a grit compressor behind it" },
            { "Fat Filter Grind", { { uFatten, 1, 1, { { "fat", 0.6f, 0.9f }, { "crunch", 0.5f, 0.8f } } },
                                    { uFilter, 1, 1, { { "type", ftLP, ftLP }, { "cutoff", 2200, 5000 }, { "reso", 0.3f, 0.5f }, { "drive", 0.35f, 0.5f } } },
                                    { uOtt, 0.2f, 0.3f, {} } }, "fattened, then a driven resonant low-pass grinds the top off" },
            { "Diode Burn", { { uTube, 1, 1, { { "drive", 0.6f, 0.8f }, { "char", 1, 1 } } },
                              { uCrackle, 0.6f, 0.8f, { { "b1_curve", ccDiode, ccDiode }, { "b1_drive", 0.45f, 0.6f } } } }, "tube mic character into a diode-clipping band: hot and buzzy" } },
        std::vector<Recipe> {   // HUGE
            { "Fat Hall", { { uFatten, 1, 1, { { "fat", 0.45f, 0.6f } } },
                            { uHalls, 0.22f, 0.32f, { { "mode", hmHall, hmHall }, { "size", 0.75f, 0.9f }, { "decay", 3.5f, 6.0f } } },
                            { uOtt, 0.15f, 0.25f, {} } }, "weight first, then a big hall and a little glue" },
            { "Wide Space", { { uChorus, 0.25f, 0.35f, { { "depth", 0.35f, 0.5f }, { "width", 1, 1 } } },
                              { uSpace, 0.25f, 0.35f, { { "size", 0.75f, 0.9f }, { "blend", 0.6f, 0.8f }, { "decay", 4.0f, 6.0f } } } }, "chorus widens it, a long blended ambience carries it" },
            { "Clean Hit Huge Tail", { { uSplit, 1, 1, { { "sens", 0.6f, 0.7f }, { "attack", 0.15f, 0.3f }, { "b_verb", 0.8f, 1.0f }, { "vsize", 0.85f, 0.95f }, { "vdecay", 5.0f, 7.0f } } },
                                       { uFatten, 1, 1, { { "fat", 0.3f, 0.45f } } } }, "SPLIT keeps every attack dry while the body gets a huge reverb" },
            { "Shimmer Echo", { { uEcho, 0.15f, 0.22f, { { "div", 7, 7 }, { "fb", 0.3f, 0.4f } } },
                                { uShimmer, 0.22f, 0.32f, { { "size", 0.8f, 0.9f }, { "decay", 6.0f, 9.0f } } } }, "echoes feeding an octave shimmer: a choir behind the sound" } },
        std::vector<Recipe> {   // CINEMATIC
            { "Galaxy Veil", { { uFilter, 1, 1, { { "type", ftLP, ftLP }, { "cutoff", 3000, 6000 }, { "reso", 0.15f, 0.25f } } },
                               { uChorus, 0.15f, 0.25f, {} },
                               { uHalls, 0.3f, 0.4f, { { "mode", hmGalaxy, hmGalaxy }, { "size", 0.85f, 0.95f }, { "decay", 8.0f, 12.0f } } } }, "softened top, chorus, then an endless galaxy hall" },
            { "Crystal Choir", { { uCrystal, 0.25f, 0.35f, { { "pitch", 12, 12 }, { "reverse", 0.5f, 0.7f }, { "fb", 0.4f, 0.55f } } },
                                 { uShimmer, 0.25f, 0.35f, { { "decay", 6.0f, 9.0f } } } }, "octave-up reversed splices into a shimmer: choir-like glass" },
            { "Tape Cloud", { { uTube, 1, 1, { { "drive", 0.25f, 0.35f } } },
                              { uEcho, 0.2f, 0.28f, { { "mode", emTape, emTape }, { "div", 10, 10 }, { "fb", 0.45f, 0.55f } } },
                              { uHalls, 0.25f, 0.35f, { { "mode", hmCloud, hmCloud }, { "size", 0.8f, 0.9f }, { "decay", 5.0f, 7.0f } } } }, "warm tube, tape echoes, a cloud reverb: film score depth" },
            { "Wide Score", { { uRetro, 1, 1, { { "noise_on", 0, 0 }, { "wobble", 0.15f, 0.25f }, { "dist", 0.1f, 0.2f } } },
                              { uSpace, 0.3f, 0.4f, { { "size", 0.85f, 0.95f }, { "blend", 0.75f, 0.9f }, { "decay", 6.0f, 9.0f } } } }, "gentle wobble and dirt under a wide blended space" } },
        std::vector<Recipe> {   // NOSTALGIC
            { "Dusty Vinyl", { { uRetro, 1, 1, { { "noise", 0.3f, 0.4f }, { "ntype", rnVinyl, rnVinyl }, { "wobble", 0.25f, 0.35f }, { "dist", 0.25f, 0.35f }, { "tone", -0.3f, -0.2f } } },
                               { uChorus, 0.2f, 0.3f, { { "rate", 0.2f, 0.3f } } } }, "vinyl noise, wobble and dirt with a soft chorus" },
            { "Old Cassette", { { uRetro, 1, 1, { { "noise", 0.3f, 0.4f }, { "ntype", rnHiss, rnHiss }, { "wobble", 0.4f, 0.5f }, { "flutter", 0.4f, 0.55f }, { "mag_on", 1, 1 }, { "mag", 0.35f, 0.45f } } },
                                { uHalls, 0.15f, 0.22f, { { "mode", hmRoom, hmRoom }, { "size", 0.3f, 0.4f } } } }, "hiss, heavy wow / flutter and tape magnetism, a small room" },
            { "Tube Tape Echo", { { uTube, 1, 1, { { "drive", 0.4f, 0.5f }, { "char", 1, 1 } } },
                                  { uEcho, 0.14f, 0.2f, { { "mode", emTape, emTape }, { "div", 10, 10 }, { "fb", 0.3f, 0.4f } } },
                                  { uChorus, 0.15f, 0.25f, { { "rate", 0.2f, 0.3f }, { "depth", 0.25f, 0.35f } } } }, "mic-channel tube, a tape echo, slow chorus: 1970s warmth" },
            { "Warm Memory", { { uRetro, 1, 1, { { "noise_on", 0, 0 }, { "wobble", 0.2f, 0.3f }, { "dist", 0.35f, 0.45f }, { "dtype", 1, 1 }, { "space_on", 1, 1 }, { "space", 0.25f, 0.35f } } },
                               { uFilter, 1, 1, { { "type", ftLP, ftLP }, { "cutoff", 6000, 9000 }, { "reso", 0.05f, 0.15f } } } }, "wobble and tube dirt with a soft space, rolled-off top" } },
        std::vector<Recipe> {   // HIT HARDER
            { "Split Punch", { { uSplit, 1, 1, { { "sens", 0.55f, 0.65f }, { "xover", 30, 45 }, { "attack", 0.4f, 0.6f }, { "sustain", -0.25f, -0.1f } } },
                               { uOtt, 0.25f, 0.4f, {} } }, "SPLIT lifts the attacks and tames the body, a touch of squeeze" },
            { "Crush Fat", { { uCrush, 0.5f, 0.7f, { { "crush", 0.5f, 0.7f }, { "dark", 0.15f, 0.25f }, { "release", 80, 150 } } },
                             { uFatten, 1, 1, { { "fat", 0.4f, 0.6f } } } }, "grit compression then weight: dense and forward" },
            { "Driven Snap", { { uSplit, 1, 1, { { "attack", 0.3f, 0.4f }, { "a_drive", 0.35f, 0.5f } } },
                               { uWreck, 1, 1, { { "style", wsTape, wsTape }, { "drive", 0.3f, 0.45f } } } }, "driven attacks out of SPLIT, then tape saturation" },
            { "Fat Squeeze", { { uFatten, 1, 1, { { "fat", 0.55f, 0.7f }, { "crunch", 0.25f, 0.35f } } },
                           { uOtt, 0.3f, 0.4f, { { "up", 0.6f, 0.75f }, { "down", 0.6f, 0.75f } } } }, "fattened and squeezed up and down: loud and even" } },
        std::vector<Recipe> {   // MOVE
            { "Filter Wobble", { { uFilter, 1, 1, { { "type", ftLP, ftLP }, { "cutoff", 800, 1600 }, { "reso", 0.45f, 0.6f }, { "lfoamt", 0.4f, 0.6f }, { "lfosync", 1, 1 }, { "lfodiv", 6, 6 } } },
                                 { uChorus, 0.25f, 0.35f, {} } }, "tempo-synced resonant low-pass wobble, chorus behind it" },
            { "Gate Echo", { { uGate, 1, 1, { { "pattern", 2, 6 } } },
                             { uEcho, 0.15f, 0.25f, { { "div", 7, 7 }, { "fb", 0.3f, 0.45f } } } }, "a rhythmic gate pattern feeding synced echoes" },
            { "Chorus Ping", { { uChorus, 0.35f, 0.45f, { { "depth", 0.5f, 0.7f }, { "rate", 0.6f, 1.0f } } },
                               { uEcho, 0.2f, 0.3f, { { "mode", emDigital, emDigital }, { "ping", 1, 1 }, { "div", 6, 6 }, { "fb", 0.35f, 0.45f } } } }, "deep chorus into ping-pong digital echoes: movement left to right" },
            { "Step Filter", { { uFilter, 1, 1, { { "type", ftLP, ftLP }, { "cutoff", 700, 1400 }, { "reso", 0.5f, 0.65f }, { "lfoamt", 0.45f, 0.6f }, { "lfoshape", lsStep, lsStep }, { "lfodiv", 6, 6 } } },
                               { uWreck, 1, 1, { { "drive", 0.2f, 0.35f } } } }, "stepped filter LFO with drive: a sequenced sound from a static one" } },
        std::vector<Recipe> {   // DISAPPEAR INTO SPACE
            { "Into The Galaxy", { { uHalls, 0.55f, 0.7f, { { "mode", hmGalaxy, hmGalaxy }, { "size", 0.9f, 1.0f }, { "decay", 12.0f, 20.0f } } },
                                   { uShimmer, 0.25f, 0.35f, {} } }, "the galaxy hall at near-endless decay with shimmer on top" },
            { "Endless Space", { { uEcho, 0.3f, 0.4f, { { "div", 10, 10 }, { "fb", 0.55f, 0.65f } } },
                                 { uSpace, 0.55f, 0.65f, { { "size", 0.9f, 1.0f }, { "blend", 0.85f, 0.95f }, { "decay", 8.0f, 10.0f } } } }, "long dotted echoes into a huge blended space" },
            { "Reverse Mist", { { uFilter, 1, 1, { { "type", ftLP, ftLP }, { "cutoff", 2000, 3000 }, { "reso", 0.1f, 0.2f } } },
                                { uCrystal, 0.4f, 0.5f, { { "reverse", 0.7f, 0.9f }, { "fb", 0.5f, 0.65f } } },
                                { uHalls, 0.45f, 0.55f, { { "mode", hmCloud, hmCloud }, { "decay", 8.0f, 12.0f } } } }, "darkened, reversed splices feeding a cloud reverb" },
            { "Tail Only", { { uSplit, 1, 1, { { "a_verb", 0.2f, 0.35f }, { "b_verb", 0.9f, 1.0f }, { "sustain", 0.2f, 0.35f }, { "vsize", 0.95f, 1.0f }, { "vdecay", 8.0f, 11.0f } } },
                             { uShimmer, 0.25f, 0.35f, {} } }, "SPLIT sends only the body to an enormous reverb, attacks stay close" } },
        std::vector<Recipe> {   // LO-FI
            { "Decimated Tube", { { uWreck, 0.55f, 0.7f, { { "style", wsDecimate, wsDecimate }, { "drive", 0.35f, 0.5f } } },
                                  { uTube, 1, 1, { { "drive", 0.35f, 0.45f }, { "char", 1, 1 } } },
                                  { uFilter, 1, 1, { { "type", ftLP, ftLP }, { "cutoff", 3500, 6000 }, { "reso", 0.1f, 0.2f } } } }, "sample-rate decimation, tube grit and a low-pass: old sampler" },
            { "Beat Tape", { { uRetro, 1, 1, { { "noise", 0.25f, 0.35f }, { "ntype", rnDust, rnDust }, { "digi_on", 1, 1 }, { "digi", 0.4f, 0.5f }, { "dist", 0.35f, 0.45f }, { "dtype", 1, 1 }, { "space_on", 1, 1 }, { "space", 0.2f, 0.3f } } } }, "dust, bit reduction, dirt and a small space: tape loop feel" },
            { "Phone Line", { { uCrackle, 1, 1, { { "b1_curve", ccDiode, ccDiode }, { "b1_drive", 0.45f, 0.55f }, { "box", cbPhone, cbPhone } } },
                              { uRetro, 1, 1, { { "noise", 0.25f, 0.35f }, { "ntype", rnRadio, rnRadio }, { "wobble", 0.1f, 0.2f } } } }, "diode clipping through a telephone box, radio noise on top" },
            { "Bit Memories", { { uRetro, 1, 1, { { "noise", 0.15f, 0.25f }, { "ntype", rnWhite, rnWhite }, { "digi_on", 1, 1 }, { "digi", 0.6f, 0.75f } } },
                                { uChorus, 0.2f, 0.3f, {} } }, "white noise and heavy bit reduction with a chorus" } },
    };
    return all[(size_t) juce::jlimit (0, 7, goal)];
}

bool isDriveKey (const juce::String& k) { return k == "drive" || k == "crush" || k == "fat" || k == "crunch" || k.endsWith ("_drive") || k == "dist" || k == "digi"; }
} // namespace

juce::String Chain::describe() const
{
    juce::StringArray s; for (auto& u : units) s.add (unitInfo (u.unit).shortName);
    return s.joinIntoString ("  >  ");
}

std::array<Chain, 3> generate (int source, int goal, uint32_t seed)
{
    static const float spaceMul[] { 0.45f, 0.75f, 1.0f, 1.0f, 1.15f, 0.55f };   // bass .. full mix
    static const float driveMul[] { 1.0f, 1.0f, 0.8f, 1.0f, 0.8f, 0.6f };
    source = juce::jlimit (0, 5, source); goal = juce::jlimit (0, 7, goal);
    juce::Random rnd ((juce::int64) hash32 (seed * 2654435761u + (uint32_t) source * 97u + (uint32_t) goal * 7919u));
    const auto& pool = recipes (goal);
    std::vector<int> idx; for (int i = 0; i < (int) pool.size(); ++i) idx.push_back (i);
    for (int i = (int) idx.size() - 1; i > 0; --i) std::swap (idx[(size_t) i], idx[(size_t) rnd.nextInt (i + 1)]);
    std::array<Chain, 3> out;
    for (int c = 0; c < 3; ++c)
    {
        const auto& r = pool[(size_t) idx[(size_t) c % idx.size()]];
        Chain ch; ch.name = juce::String::charToString ((juce::juce_wchar) ('A' + c)); ch.recipe = r.name; ch.why = r.why;
        for (auto& st : r.steps)
        {
            Setting s; s.unit = st.unit;
            const bool spaceU = unitInfo (st.unit).group == gSpace || st.unit == uHalls;
            s.mix = st.mixLo + (st.mixHi - st.mixLo) * rnd.nextFloat();
            if (spaceU && st.mixHi < 1.0f) s.mix = clampf (s.mix * spaceMul[source], 0.05f, 1.0f);
            for (auto& p : st.p)
            {
                float v = p.lo + (p.hi - p.lo) * rnd.nextFloat();
                if (p.lo == p.hi) v = p.lo;
                else if (std::abs (p.hi - p.lo) >= 1.0f && std::abs (p.lo - std::round (p.lo)) < 1.0e-6f && std::abs (p.hi - std::round (p.hi)) < 1.0e-6f && p.hi < 64) v = std::round (v);   // integer choices
                const juce::String key (p.key);
                if (isDriveKey (key) && v > 0 && v <= 1) v = clampf (v * driveMul[source], 0.0f, 1.0f);
                if (st.unit == uSplit && (key == "a_verb" || key == "b_verb")) v = clampf (v * spaceMul[source], 0.0f, 1.0f);
                s.vals.push_back ({ key, v });
            }
            ch.units.push_back (s);
        }
        // bass and full mixes: keep the low end out of any long reverb (high-pass the space units through their own tone where possible)
        if (source == 0 || source == 5)
            for (auto& s : ch.units) if (s.unit == uHalls) s.vals.push_back ({ "lowcut", source == 0 ? 220.0f : 150.0f });
        out[(size_t) c] = ch;
    }
    return out;
}

void apply (Rack::Impl& d, const Chain& c)
{
    if (d.apvts == nullptr) return;
    auto& s = *d.apvts;
    std::map<juce::RangedAudioParameter*, float> tgt;
    for (auto* p : d.allParams)
        if (! p->getParameterID().startsWith ("fx_lim_")) tgt[p] = p->getDefaultValue();
    auto set = [&tgt, &s] (const juce::String& id, float v) { if (auto* p = s.getParameter (id)) tgt[p] = p->convertTo0to1 (v); };
    for (auto& u : c.units)
    {
        set (pid (u.unit, "on"), 1.0f);
        if (unitInfo (u.unit).hasMix) set (pid (u.unit, "mix"), u.mix);
        for (auto& [k, v] : u.vals) set (pid (u.unit, k.toRawUTF8()), v);
    }
    std::array<int, kNumUnits> order; order.fill (-1); int k = 0;
    for (auto& u : c.units) order[(size_t) k++] = u.unit;
    std::array<bool, kNumUnits> all; all.fill (true);
    d.duckAround (all, [&] { d.box.setMorphOn (false); for (auto& [p, v] : tgt) d.setNorm (p, v); });   // MORPH lets go inside the duck, never before it
    d.setOrder (order);
    {
        const juce::ScopedLock sl (d.presetLock);
        d.sceneName = "Brain " + c.name + ": " + c.recipe; d.sceneEdited = false;
        for (int u = 0; u < kNumUnits; ++u) { d.unitPreset[(size_t) u] = {}; d.unitEdited[(size_t) u] = false; }
    }
    d.baselinePending.store (true);
}

void audition (Rack::Impl& d, int which)
{
    if (d.brainAudition < 0) d.pushUndo();   // one undo step for the whole audition
    d.brainAudition = juce::jlimit (0, 2, which);
    const auto chains = generate (d.brainSource, d.brainGoal, d.brainSeed);
    apply (d, chains[(size_t) d.brainAudition]);
    d.box.matchOn = d.brainMatch;
}

void endAudition (Rack::Impl& d, bool foldTrim)
{
    if (d.brainAudition < 0 && ! d.box.matchOn.load()) return;
    if (foldTrim && d.box.matchOn.load() && d.apvts != nullptr)
    {   // keep the matched loudness: make-up gain goes into the limiter's GAIN (pre-ceiling) while it is on, the rest into the box output
        float tr = d.box.trimDb.load();
        auto bump = [&d] (const juce::String& id, float lo, float hi, float& amount)
        {
            auto* p = d.apvts->getParameter (id); if (p == nullptr || std::abs (amount) < 0.05f) return;
            const float now = p->convertFrom0to1 (p->getValue()), to = juce::jlimit (lo, hi, now + amount);
            ++d.internalEdit; p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (to)); p->endChangeGesture(); --d.internalEdit;
            amount -= to - now;
        };
        if (tr > 0 && d.onP[uLim].b()) bump (pid (uLim, "gain"), 0.0f, 18.0f, tr);
        bump ("box_out", -24.0f, 24.0f, tr);
    }
    d.box.matchOn = false;
    d.brainAudition = -1;
}

} // namespace brain
} // namespace gpfx
#endif
