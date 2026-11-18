#include "PlayJson.h"
#include <cstdlib>
#include <cstdio>

namespace gslice::playjson
{
namespace
{
juce::var num (double v)
{
    if (std::isfinite (v) && v == std::floor (v) && std::abs (v) < 9.0e15) return juce::var ((juce::int64) v);
    char b[40]; std::snprintf (b, sizeof (b), "%.17g", v); return juce::var (juce::String (b));
}
double dbl (const juce::var& v, double def)
{
    if (v.isString()) { const auto s = v.toString(); return s.isEmpty() ? def : std::strtod (s.toRawUTF8(), nullptr); }
    if (v.isVoid() || v.isUndefined()) return def;
    const double d = (double) v; return std::isfinite (d) ? d : def;
}
int64 i64 (const juce::var& v, int64 def = 0) { if (v.isVoid() || v.isUndefined()) return def; if (v.isString()) return v.toString().getLargeIntValue(); return (int64) (juce::int64) v; }
int in (const juce::var& v, int def, int lo, int hi) { return v.isVoid() || v.isUndefined() ? def : juce::jlimit (lo, hi, (int) v); }
bool bl (const juce::var& v, bool def) { return v.isVoid() || v.isUndefined() ? def : (bool) v; }
}

juce::var holdsToVar (const std::vector<HoldOp>& hs)
{
    juce::Array<juce::var> a;
    for (auto& h : hs)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", (juce::int64) h.id); o->setProperty ("markerA", (juce::int64) h.markerA); o->setProperty ("markerB", (juce::int64) h.markerB);
        o->setProperty ("selA", (juce::int64) h.selA); o->setProperty ("selB", (juce::int64) h.selB);
        o->setProperty ("mode", (int) h.mode); o->setProperty ("autoStable", h.autoStable);
        o->setProperty ("driftCents", num (h.driftCents)); o->setProperty ("variation", num (h.variation)); o->setProperty ("smear", num (h.smear));
        o->setProperty ("seed", (juce::int64) h.seed);
        a.add (juce::var (o));
    }
    return a;
}
std::vector<HoldOp> holdsFromVar (const juce::var& v)
{
    std::vector<HoldOp> out;
    if (auto* arr = v.getArray())
        for (auto& x : *arr)
        {
            HoldOp h; h.id = i64 (x["id"]); h.markerA = i64 (x["markerA"]); h.markerB = i64 (x["markerB"]); h.selA = i64 (x["selA"]); h.selB = i64 (x["selB"]);
            h.mode = (HoldMode) in (x["mode"], 1, 0, 3); h.autoStable = bl (x["autoStable"], true);
            h.driftCents = dbl (x["driftCents"], 5); h.variation = dbl (x["variation"], 1); h.smear = dbl (x["smear"], 0.6);
            h.seed = (uint32_t) i64 (x["seed"], 1);
            out.push_back (h);
        }
    return out;
}

juce::var repeatsToVar (const std::vector<RepeatOp>& rs)
{
    juce::Array<juce::var> a;
    for (auto& r : rs)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", (juce::int64) r.id); o->setProperty ("start", num (r.start)); o->setProperty ("length", num (r.length));
        o->setProperty ("rateDiv", r.rateDiv); o->setProperty ("rateMod", r.rateMod); o->setProperty ("retrigger", r.retrigger);
        o->setProperty ("pitchSemis", num (r.pitchSemis)); o->setProperty ("pitchRamp", r.pitchRamp); o->setProperty ("reverse", r.reverse);
        o->setProperty ("fadeInMs", num (r.fadeInMs)); o->setProperty ("fadeOutMs", num (r.fadeOutMs)); o->setProperty ("swing", num (r.swing));
        o->setProperty ("ramp", num (r.ramp)); o->setProperty ("ratchetOn", r.ratchetOn);
        juce::Array<juce::var> c; for (float f : r.ratchet) c.add (num ((double) f)); o->setProperty ("ratchet", c);
        o->setProperty ("decay", num (r.decay)); o->setProperty ("gate", num (r.gate));
        a.add (juce::var (o));
    }
    return a;
}
std::vector<RepeatOp> repeatsFromVar (const juce::var& v)
{
    std::vector<RepeatOp> out;
    if (auto* arr = v.getArray())
        for (auto& x : *arr)
        {
            RepeatOp r; r.id = i64 (x["id"]); r.start = std::max (0.0, dbl (x["start"], 0)); r.length = std::max (0.0, dbl (x["length"], 0));
            r.rateDiv = in (x["rateDiv"], 16, 1, 128); r.rateMod = in (x["rateMod"], 0, 0, 2); r.retrigger = bl (x["retrigger"], true);
            r.pitchSemis = dbl (x["pitchSemis"], 0); r.pitchRamp = bl (x["pitchRamp"], false); r.reverse = in (x["reverse"], 0, 0, 2);
            r.fadeInMs = dbl (x["fadeInMs"], 1); r.fadeOutMs = dbl (x["fadeOutMs"], 4); r.swing = dbl (x["swing"], 0.5);
            r.ramp = dbl (x["ramp"], 1); r.ratchetOn = bl (x["ratchetOn"], false);
            if (auto* c = x["ratchet"].getArray()) for (int i = 0; i < 5 && i < c->size(); ++i) r.ratchet[(size_t) i] = (float) dbl ((*c)[i], 0);
            r.decay = dbl (x["decay"], 1); r.gate = dbl (x["gate"], 1);
            if (r.length > 0) out.push_back (r);
        }
    return out;
}

juce::var playToVar (const PlayMap& m)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("mode", (int) m.mode); o->setProperty ("equalN", m.equalN);
    if (m.sensitivity != 0.5) o->setProperty ("sensitivity", num (m.sensitivity));
    if (m.beatDiv != 8) o->setProperty ("beatDiv", m.beatDiv);
    juce::Array<juce::var> mc; for (double d : m.manual) mc.add (num (d)); o->setProperty ("manual", mc);
    o->setProperty ("polyphony", m.polyphony); o->setProperty ("bendRange", m.bendRange);
    o->setProperty ("velToGain", num (m.velToGain)); o->setProperty ("velToFilter", num (m.velToFilter));
    juce::Array<juce::var> ks;
    for (auto& k : m.keys)
    {
        auto* x = new juce::DynamicObject();
        x->setProperty ("note", k.note); x->setProperty ("slice", k.slice); x->setProperty ("pitchSemis", num (k.pitchSemis)); x->setProperty ("reverse", k.reverse);
        x->setProperty ("startMs", num (k.startMs)); x->setProperty ("attackMs", num (k.attackMs)); x->setProperty ("decayMs", num (k.decayMs));
        x->setProperty ("sustain", num (k.sustain)); x->setProperty ("releaseMs", num (k.releaseMs)); x->setProperty ("gainDb", num (k.gainDb));
        x->setProperty ("pan", num (k.pan)); x->setProperty ("choke", k.choke); x->setProperty ("oneShot", k.oneShot); x->setProperty ("repeat", k.repeat);
        if (k.vowelHold) x->setProperty ("vowelHold", true);   // (omitted when off: older projects stay byte-identical)
        ks.add (juce::var (x));
    }
    o->setProperty ("keys", ks);
    return juce::var (o);
}
PlayMap playFromVar (const juce::var& v)
{
    PlayMap m;
    if (! v.isObject()) return m;
    m.mode = (SliceMode) in (v["mode"], 0, 0, kSliceModes - 1); m.equalN = in (v["equalN"], 16, 2, 64);
    m.sensitivity = juce::jlimit (0.0, 1.0, dbl (v["sensitivity"], 0.5));
    { const int d = in (v["beatDiv"], 8, 1, 64); m.beatDiv = d <= 4 ? 4 : d <= 8 ? 8 : d <= 16 ? 16 : 32; }
    if (auto* mc = v["manual"].getArray()) for (auto& d : *mc) m.manual.push_back (dbl (d, 0));
    std::sort (m.manual.begin(), m.manual.end());
    m.polyphony = in (v["polyphony"], 16, 1, 32); m.bendRange = in (v["bendRange"], 2, 2, 24);
    m.velToGain = juce::jlimit (0.0, 1.0, dbl (v["velToGain"], 1)); m.velToFilter = juce::jlimit (0.0, 1.0, dbl (v["velToFilter"], 0));
    if (auto* ks = v["keys"].getArray())
        for (auto& x : *ks)
        {
            KeySlot k; k.note = in (x["note"], 36, 0, 127); k.slice = in (x["slice"], 0, 0, 4095); k.pitchSemis = dbl (x["pitchSemis"], 0); k.reverse = bl (x["reverse"], false);
            k.startMs = dbl (x["startMs"], 0); k.attackMs = dbl (x["attackMs"], 0); k.decayMs = dbl (x["decayMs"], 0); k.sustain = dbl (x["sustain"], 1);
            k.releaseMs = dbl (x["releaseMs"], 30); k.gainDb = dbl (x["gainDb"], 0); k.pan = dbl (x["pan"], 0); k.choke = in (x["choke"], 0, 0, 8);
            k.oneShot = bl (x["oneShot"], true); k.repeat = in (x["repeat"], 0, 0, 5); k.vowelHold = bl (x["vowelHold"], false);
            if (m.key (k.note) == nullptr) m.keys.push_back (k);
        }
    std::sort (m.keys.begin(), m.keys.end(), [] (const KeySlot& a, const KeySlot& b) { return a.note < b.note; });
    return m;
}
} // namespace gslice::playjson
