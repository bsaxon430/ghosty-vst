// Ghost Matter - AGPL-3.0-only (see LICENSE, NOTICE.md). UNDER THE HOOD logic (Hood.h).
#include "Hood.h"
#include "MakeStage.h"
#include <cmath>
#include <cstring>

namespace matter::hood
{
namespace
{
struct Link { const char* key; float (*get) (const RenderSettings&); void (*set) (RenderSettings&, float); };
const Link kLinks[] = {
    { "pxs.mainvolume0",     [] (const RenderSettings& r) { return r.stretch.volume; }, [] (RenderSettings& r, float v) { r.stretch.volume = v; } },
    { "pxs.stretchamount0",  [] (const RenderSettings& r) { return r.stretch.amount; }, [] (RenderSettings& r, float v) { r.stretch.amount = v; } },
    { "pxs.fftsize0",        [] (const RenderSettings& r) { return r.stretch.size; },   [] (RenderSettings& r, float v) { r.stretch.size = v; } },
    { "pxs.pitchshift0",     [] (const RenderSettings& r) { return r.stretch.pitch; },  [] (RenderSettings& r, float v) { r.stretch.pitch = v; } },
    { "pxs.spread0",         [] (const RenderSettings& r) { return r.stretch.spread; }, [] (RenderSettings& r, float v) { r.stretch.spread = v; } },
    { "pxs.octavemixm1_0",   [] (const RenderSettings& r) { return r.stretch.down; },   [] (RenderSettings& r, float v) { r.stretch.down = v; } },
    { "pxs.octavemix1_0",    [] (const RenderSettings& r) { return r.stretch.up; },     [] (RenderSettings& r, float v) { r.stretch.up = v; } },
    { "cdp.combine.interp",  [] (const RenderSettings& r) { return r.morph.amount; },   [] (RenderSettings& r, float v) { r.morph.amount = v; } },
};
const Link* linkOf (const std::string& key)
{
    for (auto& l : kLinks) if (key == l.key) return &l;
    return nullptr;
}

Pins& pinsOf (RenderSettings& r, const std::string& e)
{
    if (e == "pm" || e == "cb" || e == "ds" || e == "bark") return r.make.pins;
    if (e == "pxs") return r.stretch.pins;
    if (e == "cdp" || e == "ddsp") return r.morph.pins;
    return r.livePins;
}
const Pins& pinsOf (const RenderSettings& r, const std::string& e) { return pinsOf (const_cast<RenderSettings&> (r), e); }

fluesplug::Plugin pluginOf (const std::string& e) { return e == "pm" ? fluesplug::Plugin::pmSynth : e == "cb" ? fluesplug::Plugin::chatterbox : fluesplug::Plugin::disyn; }

// pins that equal the page's own default are dropped (the stretch must stay bypassed with everything at default)
bool dropsAtDefault (const std::string& key)
{
    return key.rfind ("pxs.", 0) == 0 && key != "pxs.enab_specmodule4" && key != "pxs.enab_specmodule5";
}
}

bool isRenderEngine (const char* e)
{
    for (auto* n : { "pxs", "pm", "cb", "ds", "cdp", "ddsp", "bark" }) if (std::strcmp (e, n) == 0) return true;
    return false;
}

bool isLinked (int ti) { return linkOf (keyOf (def (ti))) != nullptr; }

float value (const RenderSettings& r, int ti, const LiveView* lv)
{
    const auto& d = def (ti);
    const std::string key = keyOf (d), e = d.engine;
    if (auto* l = linkOf (key)) return l->get (r);
    const auto& pins = pinsOf (r, e);
    if (auto it = pins.find (key); it != pins.end()) return it->second;
    const std::string id = d.id;
    if (e == "pm" || e == "cb" || e == "ds")
    {
        auto ports = fluesPorts (r.make, pluginOf (e), false);
        if (auto it = ports.find (id); it != ports.end()) return it->second;
    }
    if (e == "pxs")
    {
        if (id == "enab_specmodule4") return (r.stretch.up > 0.001f || r.stretch.down > 0.001f) ? 1.0f : 0.0f;
        if (id == "enab_specmodule5") return r.stretch.spread > 0.001f ? 1.0f : 0.0f;
    }
    if (e == "cdp" && (id == "morph.ae" || id == "morph.fe"))
    {
        const double dur = lv != nullptr ? lv->matterSeconds : 4.0;
        return r.morph.detail > 0.001f ? (float) (dur / juce::jlimit (0.05, 1.0, (double) r.morph.detail)) : d.matDef;
    }
    if (e == "bark" && id == "speaker") return r.make.source == Source::words ? (float) r.make.body : 0.0f;
    if (lv != nullptr)
    {
        if (e == "panner")
        {
            if (id == "x") return juce::jlimit (-1.0f, 1.0f, 2.0f * lv->spaceX - 1.0f);
            if (id == "distance") return juce::jlimit (0.0f, 1.0f, 0.15f + 0.85f * lv->spaceY);
            if (id == "er_level") return juce::jlimit (0.0f, 1.0f, lv->room);
        }
        if (e == "fdn")
        {
            if (id == "room_size") return 0.2f + 0.8f * juce::jlimit (0.0f, 1.0f, lv->room);
            if (id == "decay") return juce::jlimit (0.0f, 1.0f, lv->tail);
            if (id == "mix") return juce::jlimit (0.0f, 1.0f, 0.15f + 0.5f * lv->tail);
            if (id == "width") return juce::jlimit (0.0f, 1.0f, lv->width);
        }
        if (e == "euclid")
        {
            if (id == "steps") return (float) lv->steps;
            if (id == "pulses") return (float) juce::jmin (lv->hits, lv->steps);
            if (id == "rotation") return (float) lv->rotate;
        }
        if (e == "turing")
        {
            if (id == "mutation") return lv->mutate;
            if (id == "range") return juce::jlimit (0.0f, 1.0f, lv->range / 2.0f);
        }
        if (e == "box2d")
        {
            if (id == "gravity") return (0.5f + 5.5f * lv->gravity) * 10.0f;
            if (id == "restitution") return juce::jlimit (0.05f, 0.97f, lv->springy);
        }
    }
    return d.matDef;
}

bool isSet (const RenderSettings& r, int ti)
{
    const auto& d = def (ti);
    const std::string key = keyOf (d);
    if (auto* l = linkOf (key)) return std::abs (l->get (r) - d.matDef) > 1e-5f;
    return pinsOf (r, d.engine).count (key) > 0;
}

void set (RenderSettings& r, int ti, float v)
{
    const auto& d = def (ti);
    const std::string key = keyOf (d);
    v = juce::jlimit (d.lo, d.hi, v);
    if (d.kind == 'i' || d.kind == 'c' || d.kind == 'b') v = std::round (v);
    if (auto* l = linkOf (key)) { l->set (r, v); return; }
    auto& pins = pinsOf (r, d.engine);
    if (dropsAtDefault (key) && std::abs (v - d.matDef) < 1e-6f) pins.erase (key);
    else pins[key] = v;
}

void clear (RenderSettings& r, int ti)
{
    const auto& d = def (ti);
    const std::string key = keyOf (d);
    if (auto* l = linkOf (key)) { l->set (r, d.matDef); return; }
    pinsOf (r, d.engine).erase (key);
}

void clearAll (RenderSettings& r)
{
    r.make.pins.clear(); r.stretch.pins.clear(); r.morph.pins.clear(); r.livePins.clear();
    r.stretch.freeFilter = {};
}

int numSet (const RenderSettings& r)
{
    int n = 0;
    for (int i = 0; i < count(); ++i) if (isSet (r, i)) ++n;
    return n;
}

LivePins livePins (const RenderSettings& r)
{
    LivePins lp;
    for (auto* pp : { &r.make.pins, &r.livePins })
        for (auto& kv : *pp)
        {
            const int ti = indexOf (kv.first);
            const int s = ti < 0 ? -1 : liveSlot (ti);
            if (s >= 0) { lp.v[s] = kv.second; lp.on[s] = true; lp.any = true; }
        }
    return lp;
}

float pxs (const StretchSettings& s, const char* id)
{
    const std::string key = std::string ("pxs.") + id;
    if (! std::strcmp (id, "mainvolume0")) return s.volume;
    if (! std::strcmp (id, "stretchamount0")) return s.amount;
    if (! std::strcmp (id, "fftsize0")) return s.size;
    if (! std::strcmp (id, "pitchshift0")) return s.pitch;
    if (! std::strcmp (id, "spread0")) return s.spread;
    if (! std::strcmp (id, "octavemixm1_0")) return s.down;
    if (! std::strcmp (id, "octavemix1_0")) return s.up;
    if (auto it = s.pins.find (key); it != s.pins.end()) return it->second;
    const int ti = indexOf (key);
    return ti >= 0 ? def (ti).upDef : 0.0f;
}

bool anyPxsPin (const StretchSettings& s) { return ! s.pins.empty(); }
}
