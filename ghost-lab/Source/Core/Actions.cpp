#include "Actions.h"
#include "Rooms.h"
#include "TerrainDsp.h"
#include "TerrainSerial.h"
#include "LabTerrainMap.h"
#include "Hood.h"
#include "Spectre.h"
#include <cmath>

namespace glab
{
// ------------------------------------------------------------------------------------------------ the catalogue
static const Control kDetail { "detail", "Detail (analysis size)", 256, 4096, 1024, "", true };

static std::vector<OpInfo> makeCatalogue()
{
    std::vector<OpInfo> v;
    v.push_back ({ Op::Pull, "pull", "PULL", "Stretch sound", "pvoc anal > stretch time > pvoc synth",
                   { { "amount", "How long", 1.1f, 8.0f, 2.5f, "x", false },
                     { "from", "Stretch from", 0.0f, 1.0f, 0.0f, "", true },
                     { "to", "Stretch to", 0.0f, 1.0f, 1.0f, "", true }, kDetail } });
    v.push_back ({ Op::Twist, "twist", "TWIST", "Change character", "pvoc anal > stretch spectrum > strange shift > pvoc synth",
                   { { "bend", "Bend", -1.0f, 1.0f, 0.6f, "", false },
                     { "above", "Starts above", 50.0f, 2000.0f, 180.0f, "Hz", true }, kDetail } });
    v.push_back ({ Op::Freeze, "freeze", "FREEZE", "Hold a moment", "pvoc anal > focus hold > pvoc synth",
                   { { "where", "Where", 0.0f, 1.0f, 0.35f, "", false },
                     { "hold", "Hold for", 0.3f, 10.0f, 2.0f, "s", false }, kDetail } });
    v.push_back ({ Op::Smear, "smear", "SMEAR", "Blur frequencies", "pvoc anal > blur blur > blur chorus > pvoc synth",
                   { { "amount", "Blur", 0.0f, 1.0f, 0.6f, "", false },
                     { "shimmer", "Shimmer", 0.0f, 1.0f, 0.25f, "", true }, kDetail } });
    v.push_back ({ Op::Shatter, "shatter", "SHATTER", "Fragment audio", "modify radical 2 (shred)",
                   { { "amount", "Pieces", 0.0f, 1.0f, 0.6f, "", false },
                     { "scatter", "Scatter", 0.0f, 3.0f, 1.0f, "", true } } });
    v.push_back ({ Op::Mutate, "mutate", "MUTATE", "Evolve sound", "pvoc anal > 2-3 random spectral processes > pvoc synth",
                   { { "wild", "Wildness", 0.0f, 1.0f, 0.5f, "", false }, kDetail } });
    v.push_back ({ Op::GrainCloud, "grain", "GRAIN CLOUD", "Scatter and disperse", "modify brassage 6",
                   { { "density", "Density", 0.0f, 1.0f, 0.6f, "", false },
                     { "size", "Grain size", 10.0f, 300.0f, 70.0f, "ms", false },
                     { "scatter", "Scatter", 0.0f, 1.0f, 0.7f, "", true },
                     { "stretch", "Stretch", 0.5f, 4.0f, 1.5f, "x", true },
                     { "pitch", "Pitch", -12.0f, 12.0f, 0.0f, "st", true } } });
    v.push_back ({ Op::SpectralMelt, "melt", "SPECTRAL MELT", "Dissolve frequency structure", "pvoc anal > focus accu > blur blur > pvoc synth",
                   { { "amount", "Melt", 0.0f, 1.0f, 0.6f, "", false },
                     { "drip", "Drip", -1.0f, 1.0f, -0.5f, "", true }, kDetail } });
    v.push_back ({ Op::TimeWarp, "warp", "TIME WARP", "Bend and stretch time", "modify radical 1 (reverse) > pvoc anal > stretch time > pvoc synth",
                   { { "bend", "Bend", 0.0f, 1.0f, 0.6f, "", false },
                     { "backwards", "Backwards", 0.0f, 1.0f, 1.0f, "", false }, kDetail } });
    v.push_back ({ Op::VoiceFusion, "fusion", "VOICE FUSION", "Blend and morph voices", "pvoc anal (both) > stretch time (fit) > combine cross > pvoc synth",
                   { { "amount", "Blend", 0.0f, 1.0f, 0.75f, "", false }, kDetail } });
    v.push_back ({ Op::Pinch, "pinch", "PINCH", "Squeeze the spectrum", "pvoc anal > stretch spectrum > pvoc synth",
                   { { "amount", "Squeeze", -1.0f, 1.0f, 0.6f, "", false }, kDetail } });
    v.push_back ({ Op::Breed, "breed", "BREED", "Two specimens make a new one", "pvoc anal (both) > combine cross (both ways) > morph morph > pvoc synth",
                   { { "lean", "Leans toward", 0.0f, 1.0f, 0.5f, "", false }, kDetail } });
    v.push_back ({ Op::Hood, "hood", "UNDER THE HOOD", "Any engine process, all its own settings", "the engine program you picked, exactly as typed", {} });
    v.push_back ({ Op::Terrain, "terrain", "TERRAIN", "The landscape shapes the spectrum", "Ghost Lab's own spectral sculpt + resonator bank (no engine programs)",
                   { { "strength", "How much", 0.0f, 1.0f, 1.0f, "", false } } });
        // the rooms (Rooms.h). The controls are plain words; each one sets the engine's own control of that kind.
    v.push_back ({ Op::Grow, "grow", "GROW", "A sound out of nothing", "the physical-model, voice and noise engines, at their own defaults",
        { { "source", "What kind", 0, 2, 0, "", false }, { "body", "Which one", 0, 11, 6, "", false },
          { "loud", "Loud", 0, 1, 0.5f, "", false }, { "bright", "Bright", 0, 1, 0.5f, "", false }, { "rough", "Rough", 0, 1, 0.2f, "", false },
          { "shape", "Shape", 0, 1, 0.5f, "", false }, { "seconds", "How long", 0.5f, 12, 4, "s", false }, { "note", "Note", 24, 96, 48, "", false } } });
    v.push_back ({ Op::XStretch, "xstretch", "STRETCH", "Slow it into a wash", "the extreme-stretch engine, its own offline render",
        { { "amount", "How far", 1, 64, 12, "x", false }, { "size", "Smooth", 0, 1, 0.7f, "", false }, { "spread", "Smear", 0, 1, 0, "", false },
          { "up", "Octave above", 0, 1, 0, "", false }, { "down", "Octave below", 0, 1, 0, "", false }, { "pitch", "Pitch", -12, 12, 0, "st", false } } });
    v.push_back ({ Op::Reborn, "reborn", "REBORN", "Played back by a neural instrument", "the neural instrument engine, its own processor",
        { { "instrument", "Instrument", -1, 31, -1, "", false }, { "mix", "How much", 0, 1, 1, "", false }, { "shift", "Move the tune", -24, 24, 0, "st", false },
          { "tone", "Tone", 0, 1, 1, "", false }, { "breath", "Breath", 0, 1, 1, "", false },
          { "chaos", "Chaos", 0, 1, 0, "", false }, { "hop", "Swap instruments", 0, 1, 0, "", false }, { "refeed", "Feed it back", 0, 1, 0, "", false } } });
    v.push_back ({ Op::Space, "space", "SPACE", "Place it around your head", "the binaural panner and reverb engine",
        { { "x", "Left / right", 0, 1, 0.5f, "", false }, { "y", "Near / far", 0, 1, 0.3f, "", false }, { "room", "Room", 0, 1, 0.3f, "", false },
          { "tail", "Tail", 0, 1, 0.25f, "", false }, { "width", "Width", 0, 1.5f, 1, "", false } } });
    {
        // SPECTRE (build 17): the controls are the engine's own (Spectre.h), under its own keys
        std::vector<Control> cs;
        for (int i = 0; i < spectre::kParams; ++i)
        {
            const auto& p = spectre::param (i);
            cs.push_back ({ p.key, p.name, p.lo, p.hi, p.def, p.unit, false });
        }
        v.push_back ({ Op::Spectre, "spectre", "SPECTRE", "Rebuilt from up to 1,024 sine waves", "our own real-time resynthesis engine", cs });
    }
return v;
}

static const std::vector<OpInfo>& catalogue() { static auto c = makeCatalogue(); return c; }

const OpInfo& info (Op op) { return catalogue()[(size_t) juce::jlimit (0, (int) Op::Count - 1, (int) op)]; }

Op opFromId (const juce::String& id)
{
    for (auto& o : catalogue()) if (o.id == id) return o.op;
    return Op::Count;
}

float Step::get (const juce::String& id) const
{
    for (auto& c : info (op).controls)
        if (c.id == id)
        {
            auto it = values.find (id);
            return juce::jlimit (c.min, c.max, it != values.end() ? it->second : c.def);
        }
    jassertfalse;
    return 0.0f;
}

juce::var Step::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("op", info (op).id);
    o->setProperty ("seed", (int) seed);
    auto* vals = new juce::DynamicObject();
    for (auto& kv : values) vals->setProperty (kv.first, kv.second);
    o->setProperty ("values", juce::var (vals));
    if (data.isNotEmpty()) o->setProperty ("data", data);
    return juce::var (o);
}

bool Step::fromVar (const juce::var& v, Step& out)
{
    const auto op = opFromId (v["op"].toString());
    if (op == Op::Count) return false;
    out = {};
    out.op = op;
    out.seed = (juce::uint32) (int) v["seed"];
    if (auto* vals = v["values"].getDynamicObject())
        for (auto& p : vals->getProperties()) out.values[p.name.toString()] = (float) p.value;
    out.data = v["data"].toString();
    return true;
}

juce::String Step::describe() const
{
    if (op == Op::Hood) { hood::Request r; if (hood::Request::fromData (data, r)) return r.describe(); }
    if (rooms::isRoomOp (op)) return rooms::describe (*this);
    const auto& i = info (op);
    if (i.controls.empty()) return i.title;
    const auto& c = i.controls.front();
    const float v = get (c.id);
    if (c.max <= 1.0f && c.min >= -1.0f) return i.title + " " + juce::String (juce::roundToInt (v * 100)) + "%";
    return i.title + " " + juce::String (v, 1) + c.unit;
}

// ------------------------------------------------------------------------------------------------ running recipes
namespace
{
juce::String num (double v) { return juce::String (v, 4); }

int snapPoints (float v)
{
    int p = 256;
    while (p < 4096 && p * 1.41f < v) p *= 2;
    return p;
}

struct Job
{
    const Cdp& cdp;
    juce::File dir;
    const std::atomic<bool>* cancel;
    juce::StringArray& commands;
    juce::String error;
    int counter = 0;

    juce::File name (const juce::String& ext) { return dir.getChildFile ("f" + juce::String (++counter) + ext); }
    bool failed() const { return error.isNotEmpty(); }

    // args: program words, with files given by their names inside the job folder
    juce::File run (juce::StringArray args, const juce::File& out)
    {
        if (failed()) return {};
        commands.add (args.joinIntoString (" "));
        auto r = cdp.run (args, dir, out, cancel, 600000);
        if (! r.ok) { error = r.error; return {}; }
        return out;
    }
    juce::File wav (const Audio& a)
    {
        auto f = name (".wav");
        if (! writeWav (a, f, 32, error)) return {};
        return f;
    }
    juce::File text (const juce::String& content)
    {
        auto f = name (".txt");
        f.replaceWithText (content, false, false, "\n");
        return f;
    }
    juce::File anal (const juce::File& w, int points)
    {
        auto o = name (".ana");
        return run ({ "pvoc", "anal", "1", w.getFileName(), o.getFileName(), "-c" + juce::String (points) }, o);
    }
    juce::File synth (const juce::File& ana)
    {
        auto o = name (".wav");
        return run ({ "pvoc", "synth", ana.getFileName(), o.getFileName() }, o);
    }
    // one spectral program: ana in -> ana out
    juce::File spec (juce::StringArray words, const juce::File& in, juce::StringArray params)
    {
        auto o = name (".ana");
        words.add (in.getFileName()); words.add (o.getFileName()); words.addArray (params);
        return run (words, o);
    }
    // two spectral inputs: ana in1, ana in2 -> ana out (combine cross takes "in1 in2 out [params]")
    juce::File spec2 (juce::StringArray words, const juce::File& in1, const juce::File& in2, juce::StringArray params)
    {
        auto o = name (".ana");
        words.add (in1.getFileName()); words.add (in2.getFileName()); words.add (o.getFileName()); words.addArray (params);
        return run (words, o);
    }
    juce::File sound (juce::StringArray words, const juce::File& in, juce::StringArray params)
    {
        auto o = name (".wav");
        words.add (in.getFileName()); words.add (o.getFileName()); words.addArray (params);
        return run (words, o);
    }
};

// a time-varying stretch for PULL: 1 outside [from, to], `amount` inside, with short ramps
juce::String pullCurve (double dur, double from, double to, double amount)
{
    if (from <= 0.002 && to >= 0.998) return {};
    from = juce::jlimit (0.0, 1.0, from) * dur; to = juce::jlimit (0.0, 1.0, to) * dur;
    if (to < from) std::swap (from, to);
    const double ramp = juce::jmin (0.04, (to - from) * 0.2 + 0.001);
    struct P { double t, v; };
    std::vector<P> pts { { 0.0, 1.0 }, { from - ramp, 1.0 }, { from, amount }, { to, amount }, { to + ramp, 1.0 }, { dur + 1.0, 1.0 } };
    juce::String s; double last = -1;
    for (auto& p : pts)
    {
        const double t = juce::jmax (p.t, 0.0);
        if (t <= last + 1e-4) continue;
        s << num (t) << " " << num (p.v) << "\n";
        last = t;
    }
    return s;
}

// one mono channel of one step; returns the result WAV
juce::File renderMono (const Step& s, const Audio& in, const Audio* partner, Job& j, int ch)
{
    const double dur = in.seconds();
    const int pts = snapPoints (s.op == Op::Shatter || s.op == Op::GrainCloud ? 1024.0f : s.get ("detail"));
    juce::Random rng ((juce::int64) s.seed * 7919 + ch * 104729);
    auto w = j.wav (in);
    if (j.failed()) return {};

    switch (s.op)
    {
        case Op::Pull:
        {
            auto a = j.anal (w, pts);
            const auto curve = pullCurve (dur, s.get ("from"), s.get ("to"), s.get ("amount"));
            const auto arg = curve.isEmpty() ? num (s.get ("amount")) : j.text (curve).getFileName();
            return j.synth (j.spec ({ "stretch", "time", "1" }, a, { arg }));
        }
        case Op::Twist:
        {
            const float bend = s.get ("bend");
            auto a = j.anal (w, pts);
            auto b = j.spec ({ "stretch", "spectrum", "1" }, a, { num (s.get ("above")), num (std::pow (2.0, bend * 0.9)), "1" });
            const double shift = bend * 70.0;
            if (std::abs (shift) > 1.0) b = j.spec ({ "strange", "shift", "1" }, b, { num (shift) });
            return j.synth (b);
        }
        case Op::Pinch:
        {
            auto a = j.anal (w, pts);
            return j.synth (j.spec ({ "stretch", "spectrum", "1" }, a, { "120", num (std::pow (2.0, -s.get ("amount") * 1.2)), "1" }));
        }
        case Op::Freeze:
        {
            auto a = j.anal (w, pts);
            const double t = juce::jlimit (0.02, juce::jmax (0.03, dur - 0.05), s.get ("where") * dur);
            auto data = j.text (num (t) + " " + num (s.get ("hold")) + "\n");
            return j.synth (j.spec ({ "focus", "hold" }, a, { data.getFileName() }));
        }
        case Op::Smear:
        {
            const float amt = s.get ("amount");
            auto a = j.anal (w, pts);
            auto b = j.spec ({ "blur", "blur" }, a, { juce::String (2 + juce::roundToInt (std::pow (amt, 1.5f) * 80.0f)) });
            const float sh = s.get ("shimmer");
            if (sh > 0.01f) b = j.spec ({ "blur", "chorus", "1" }, b, { num (1.0 + sh * 20.0) });
            return j.synth (b);
        }
        case Op::Shatter:
        {
            const float amt = s.get ("amount");
            double chunk = juce::jmin (0.30 - 0.26 * amt, dur / 3.0);
            const int repeats = 1 + juce::roundToInt (amt * 3.0f);
            const double maxScatter = std::floor (dur / chunk);
            const double scatter = juce::jmin ((double) s.get ("scatter"), maxScatter);
            return j.sound ({ "modify", "radical", "2" }, w, { juce::String (repeats), num (chunk), "-s" + num (scatter) });
        }
        case Op::Mutate:
        {
            const float wild = s.get ("wild");
            juce::Random pick ((juce::int64) s.seed * 31337);   // the same seed picks the same processes on both channels
            std::vector<int> pool { 0, 1, 2, 3, 4, 5 };
            for (int i = (int) pool.size() - 1; i > 0; --i) std::swap (pool[(size_t) i], pool[(size_t) pick.nextInt (i + 1)]);
            const int count = wild > 0.7f ? 3 : 2;
            auto b = j.anal (w, pts);
            for (int k = 0; k < count && ! j.failed(); ++k)
            {
                const float r = pick.nextFloat();
                switch (pool[(size_t) k])
                {
                    case 0: b = j.spec ({ "blur", "chorus", "5" }, b, { num (1.0 + wild * 12.0 * (0.5 + r)), num (1.0 + wild * 1.2 * (0.5 + r)) }); break;
                    case 1: b = j.spec ({ "blur", "scatter" }, b, { juce::String (juce::jmax (4, (int) (pts / 2 * (0.3f - 0.24f * wild)))) }); break;
                    case 2: b = j.spec ({ "strange", "waver", "1" }, b, { num (0.5 + wild * 5.0 * r), num (1.2 + wild * 1.6), "150" }); break;
                    case 3: b = j.spec ({ "strange", "shift", "1" }, b, { num ((r < 0.5f ? -1.0 : 1.0) * (40.0 + wild * 220.0)) }); break;
                    case 4: b = j.spec ({ "focus", "accu" }, b, { "-d" + num (0.08 + 0.35 * wild), "-g" + num ((r < 0.5f ? -1.0 : 1.0) * wild * 0.6) }); break;
                    default: b = j.spec ({ "stretch", "spectrum", "1" }, b, { "200", num (0.55 + r * 1.3), "1" }); break;
                }
            }
            return j.synth (b);
        }
        case Op::GrainCloud:
        {
            const double size = s.get ("size");
            const double splice = juce::jmin (5.0, size / 4.0);
            const double space = in.channels() > 1 ? 0.5 : (ch == 0 ? 0.5 : 0.5);
            return j.sound ({ "modify", "brassage", "6" }, w,
                            { num (1.0 / s.get ("stretch")), num (0.6 + s.get ("density") * 5.0), num (size), num (s.get ("pitch")),
                              "1", num (space), num (splice), num (splice), "-j" + num (s.get ("scatter")) });
        }
        case Op::SpectralMelt:
        {
            const float amt = s.get ("amount");
            auto a = j.anal (w, pts);
            auto b = j.spec ({ "focus", "accu" }, a, { "-d" + num (0.05 + 0.45 * amt), "-g" + num (s.get ("drip") * (0.15 + 0.6 * amt)) });
            b = j.spec ({ "blur", "blur" }, b, { juce::String (3 + juce::roundToInt (amt * 20.0f)) });
            return j.synth (b);
        }
        case Op::TimeWarp:
        {
            auto src = s.get ("backwards") >= 0.5f ? j.sound ({ "modify", "radical", "1" }, w, {}) : w;
            auto a = j.anal (src, pts);
            const double bend = s.get ("bend");
            auto curve = j.text ("0 " + num (1.0 / (1.0 + bend)) + "\n" + num (dur * 0.5) + " 1\n" + num (dur + 0.5) + " " + num (1.0 + bend * 3.0) + "\n");
            return j.synth (j.spec ({ "stretch", "time", "1" }, a, { curve.getFileName() }));
        }
        case Op::VoiceFusion:
        {
            auto a = j.anal (w, pts);
            juce::File pa;
            if (partner != nullptr)
            {
                pa = j.anal (j.wav (*partner), pts);
                const double ratio = juce::jlimit (0.25, 4.0, dur / juce::jmax (0.05, partner->seconds()));
                if (std::abs (ratio - 1.0) > 0.01) pa = j.spec ({ "stretch", "time", "1" }, pa, { num (ratio) });
            }
            else
            {
                // no second specimen: fuse with a ghost of itself (a fifth down, reversed)
                auto low = j.sound ({ "modify", "speed", "2" }, w, { "-7" });
                auto rev = j.sound ({ "modify", "radical", "1" }, low, {});
                pa = j.anal (rev, pts);
                pa = j.spec ({ "stretch", "time", "1" }, pa, { num (std::pow (2.0, -7.0 / 12.0)) });
            }
            return j.synth (j.spec2 ({ "combine", "cross" }, a, pa, { "-i" + num (s.get ("amount")) }));
        }
        case Op::Breed:
        {
            jassert (partner != nullptr);
            auto a = j.anal (w, pts);
            auto b = j.anal (j.wav (*partner), pts);
            const double ratio = juce::jlimit (0.25, 4.0, dur / juce::jmax (0.05, partner->seconds()));
            if (std::abs (ratio - 1.0) > 0.01) b = j.spec ({ "stretch", "time", "1" }, b, { num (ratio) });
            // a's movement with b's colour, and b's movement with a's colour, then one grows into the other
            auto x = j.spec2 ({ "combine", "cross" }, a, b, { "-i1" });
            auto y = j.spec2 ({ "combine", "cross" }, b, a, { "-i1" });
            const double e = std::pow (4.0, (0.5 - s.get ("lean")) * 2.0);
            auto o = j.name (".ana");
            j.run ({ "morph", "morph", "2", x.getFileName(), y.getFileName(), o.getFileName(), "0", num (dur), "0", num (dur), num (e), num (e) }, o);
            return j.failed() ? juce::File() : j.synth (o);
        }
        case Op::Terrain: case Op::Hood: case Op::Grow: case Op::XStretch: case Op::Reborn: case Op::Space: case Op::Spectre: case Op::Count: break;
    }
    j.error = "Unknown action.";
    return {};
}
}

RenderResult renderTerrain (const Step& s, const Audio& in, const Audio* partner, const Cdp& cdp, const std::atomic<bool>* cancel)
{
    // TERRAIN: our own spectral sculpt follows the landscape across the sound (x = frequency, depth = time), then the
    // resonator bank adds the ringing and the grit. Deterministic: the same landscape always gives the same sound.
    RenderResult res;
    ghost::terrain::Terrain t;
    if (! ghost::terrain::fromString (s.data, t)) { res.error = "That TERRAIN step has no landscape."; return res; }
    auto out = std::make_shared<Audio>();
    out->sampleRate = in.sampleRate;
    out->buf.setSize (in.channels(), in.length());
    std::vector<const float*> ip; std::vector<float*> op;
    for (int c = 0; c < in.channels(); ++c) { ip.push_back (in.buf.getReadPointer (c)); op.push_back (out->buf.getWritePointer (c)); }
    ghost::terrain::SculptOptions o;
    o.strength = s.get ("strength");
    ghost::terrain::sculpt (t, ip.data(), op.data(), in.channels(), in.length(), in.sampleRate, o);
    res.commands.add ("terrain sculpt (own) strength " + juce::String (o.strength, 2) + " peaks " + juce::String (t.size()));
    sanitize (*out);
    if (rms (*out) < 1e-6f) { res.error = "That landscape turned this sound into silence. Raise a mountain."; return res; }
    finish (*out, rms (in));
    res.audio = out;
    // build 4: the land drives the lab's own processes too (LabTerrainMap.h): FREEZE, SMEAR, GRAIN CLOUD, SHATTER, TWIST, BREED.
    // A landscape saved before (v 1: the first TERRAIN) keeps its sculpt-only sound.
    if (t.version >= 2 && cdp.available())
        for (const auto& st : mapLabLand (t, 0.0f, partner != nullptr).steps)
        {
            if (cancel != nullptr && cancel->load()) break;
            auto r = render (st, *res.audio, partner, cdp, cancel);
            for (auto& c : r.commands) res.commands.add (c);
            if (r.audio != nullptr) res.audio = r.audio;
            else res.commands.add ("(skipped " + info (st.op).title + ": " + r.error + ")");
        }
    return res;
}

RenderResult render (const Step& s, const Audio& inRaw, const Audio* partnerRaw, const Cdp& cdp, const std::atomic<bool>* cancel)
{
    RenderResult res;
    if (inRaw.length() < (int) (0.05 * inRaw.sampleRate)) { res.error = "That sound is too short."; return res; }
    if (s.op == Op::Hood) return hood::renderHood (s, inRaw, partnerRaw, cdp, cancel);
    if (s.op == Op::Terrain) return renderTerrain (s, inRaw, partnerRaw, cdp, cancel);
    if (s.op == Op::Grow) { res.error = "GROW makes a new sound: use the GROW room."; return res; }
    if (rooms::isRoomOp (s.op)) return rooms::render (s, inRaw, cancel);
    if (! cdp.available()) { res.error = "Ghost Lab's sound engine is missing. Reinstall Ghost Lab."; return res; }
    if (s.op == Op::Breed && partnerRaw == nullptr) { res.error = "Breeding needs two specimens: drop a second sound first."; return res; }

    // partner at the same rate, mono (spectral crossing is per channel; one partner voice drives both sides)
    std::unique_ptr<Audio> partner;
    if (partnerRaw != nullptr) partner = std::make_unique<Audio> (resampled (mono (*partnerRaw), inRaw.sampleRate));

    const bool monoWork = s.op == Op::Breed || isEffectivelyMono (inRaw);
    const int workCh = monoWork ? 1 : inRaw.channels();
    auto dir = cdp.newJobFolder();
    Job j { cdp, dir, cancel, res.commands, {}, 0 };
    std::vector<std::shared_ptr<Audio>> parts;
    for (int c = 0; c < workCh && ! j.failed(); ++c)
    {
        const auto in = monoWork ? mono (inRaw) : channel (inRaw, c);
        auto out = renderMono (s, in, partner.get(), j, c);
        if (j.failed()) break;
        juce::String err;
        auto a = readWav (out, err);
        if (a == nullptr) { j.error = err; break; }
        parts.push_back (a);
    }
    dir.deleteRecursively();
    if (j.failed()) { res.error = j.error; return res; }

    auto out = std::make_shared<Audio>();
    out->sampleRate = parts.front()->sampleRate;
    if (workCh == 1)
        out->buf = parts.front()->buf;   // a mono source may come back stereo (GRAIN CLOUD): keep its width
    else
    {
        int len = INT_MAX;
        for (auto& p : parts) len = juce::jmin (len, p->length());
        out->buf.setSize (workCh, len);
        for (int c = 0; c < workCh; ++c)
            out->buf.copyFrom (c, 0, parts[(size_t) c]->buf, juce::jmin (c, parts[(size_t) c]->channels() - 1), 0, len);
    }
    if (std::abs (out->sampleRate - inRaw.sampleRate) > 0.5) *out = resampled (*out, inRaw.sampleRate);
    if (out->length() < 16 || rms (*out) < 1e-6f) { res.error = "That turned this sound into silence. Try a gentler setting."; return res; }
    finish (*out, rms (inRaw));
    res.audio = out;
    return res;
}
}
