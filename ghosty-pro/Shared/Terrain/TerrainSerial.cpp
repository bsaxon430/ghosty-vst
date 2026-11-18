#include "TerrainSerial.h"

namespace ghost::terrain
{

juce::var toVar (const Terrain& t)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("v", (int) t.version);   // 1: the first TERRAIN (its own noise), 2: the islands
    juce::Array<juce::var> ps;
    for (auto& p : t)
    {
        auto* q = new juce::DynamicObject();
        q->setProperty ("x", p.x); q->setProperty ("z", p.z); q->setProperty ("h", p.height);
        q->setProperty ("w", p.width); q->setProperty ("d", p.depth); q->setProperty ("r", p.rough);
        ps.add (juce::var (q));
    }
    o->setProperty ("peaks", ps);
    o->setProperty ("grit", t.grit);
    o->setProperty ("evolve", t.evolve);
    o->setProperty ("phase", t.phase);
    o->setProperty ("frozen", t.frozen);
    o->setProperty ("freezeZ", t.freezeZ);
    o->setProperty ("seed", (juce::int64) t.seed);
    if (t.labelCount() > 0)
    {
        juce::Array<juce::var> ls;
        for (auto& l : t.labels)
        {
            if (l.level == 0) continue;
            auto* q = new juce::DynamicObject();
            q->setProperty ("x", l.x); q->setProperty ("z", l.z); q->setProperty ("level", (int) l.level);
            q->setProperty ("text", juce::String::fromUTF8 (l.text));
            ls.add (juce::var (q));
        }
        o->setProperty ("labels", ls);
    }
    return juce::var (o);
}

static float num (const juce::var& o, const char* k, float def)
{
    const auto v = o.getProperty (k, juce::var());
    return v.isVoid() ? def : (float) (double) v;
}

bool fromVar (const juce::var& v, Terrain& out)
{
    if (! v.isObject()) return false;
    Terrain t;
    if (auto* arr = v.getProperty ("peaks", juce::var()).getArray())
        for (auto& q : *arr)
        {
            if (! q.isObject()) continue;
            Peak p;
            p.x = num (q, "x", p.x); p.z = num (q, "z", p.z); p.height = num (q, "h", p.height);
            p.width = num (q, "w", p.width); p.depth = num (q, "d", p.depth); p.rough = num (q, "r", p.rough);
            t.add (p);
        }
    t.grit = num (v, "grit", 0.0f);
    t.evolve = num (v, "evolve", 0.0f);
    t.phase = num (v, "phase", 0.0f);
    t.frozen = (bool) v.getProperty ("frozen", false);
    t.freezeZ = num (v, "freezeZ", 0.5f);
    t.seed = (uint32_t) (juce::int64) v.getProperty ("seed", (juce::int64) 1);
    // a landscape saved before the islands (v 1, or no v) keeps its own land and so its sound
    t.version = (uint8_t) juce::jlimit (1, (int) Terrain::kVersion, (int) v.getProperty ("v", 1));
    if (auto* arr = v.getProperty ("labels", juce::var()).getArray())
        for (auto& q : *arr)
        {
            if (! q.isObject()) continue;
            RingLabel l;
            l.x = num (q, "x", 0.5f); l.z = num (q, "z", 0.5f);
            l.level = (int8_t) juce::jlimit (-25, 25, (int) q.getProperty ("level", 0));
            q.getProperty ("text", "").toString().copyToUTF8 (l.text, sizeof (l.text));
            if (l.level != 0) t.addLabel (l);
        }
    t.clampAll();
    out = t;
    return true;
}

juce::String toString (const Terrain& t) { return juce::JSON::toString (toVar (t), true); }

bool fromString (const juce::String& s, Terrain& out)
{
    juce::var v;
    if (juce::JSON::parse (s, v).failed()) return false;
    return fromVar (v, out);
}

juce::ValueTree toValueTree (const Terrain& t, const juce::Identifier& type)
{
    juce::ValueTree v (type);
    v.setProperty ("json", toString (t), nullptr);
    return v;
}

bool fromValueTree (const juce::ValueTree& v, Terrain& out)
{
    return v.isValid() && fromString (v.getProperty ("json").toString(), out);
}
}
