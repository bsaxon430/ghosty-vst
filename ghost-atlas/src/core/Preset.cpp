// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Preset.h"
#include "GhostAtlasArt.h"

namespace atlas
{
juce::var FxState::toVar() const
{
    auto o = newObject();
    o->setProperty ("unit", unit); o->setProperty ("on", on); o->setProperty ("mix", mix);
    auto v = newObject();
    for (const auto& kv : values) v->setProperty (kv.first, kv.second);
    o->setProperty ("values", v.get());
    if (! data.isVoid()) o->setProperty ("data", data);
    return o.get();
}
FxState FxState::fromVar (const juce::var& v)
{
    FxState f;
    if (! v.isObject()) return f;
    f.unit = prop (v, "unit").toString(); f.on = (bool) prop (v, "on", false); f.mix = (float) (double) prop (v, "mix", 1.0);
    if (auto* d = prop (v, "values").getDynamicObject())
        for (const auto& nv : d->getProperties()) f.values[nv.name.toString()] = (float) (double) nv.value;
    f.data = prop (v, "data");
    return f;
}

juce::var LayerState::toVar() const
{
    auto o = newObject();
    o->setProperty ("instrument", instrument);
    if (label.isNotEmpty()) o->setProperty ("label", label);
    if (role.isNotEmpty()) o->setProperty ("role", role);
    auto pv = newObject();
    for (int i = 0; i < kNumLayerParams; ++i)
        if (std::abs (p[(size_t) i] - layerParam (i).def) > 1.0e-6f || i == lpOn) pv->setProperty (layerParam (i).id, p[(size_t) i]);
    o->setProperty ("params", pv.get());
    auto sh = newObject();
    sh->setProperty ("startRange", startRange); sh->setProperty ("loop", loopMode); sh->setProperty ("loopStart", loopStart); sh->setProperty ("loopEnd", loopEnd);
    sh->setProperty ("loopFade", loopFade); sh->setProperty ("reverse", reverse); sh->setProperty ("voices", voices);
    if (keyFadeIn > 0) sh->setProperty ("keyFadeIn", keyFadeIn);
    if (keyFadeOut > 0) sh->setProperty ("keyFadeOut", keyFadeOut);
    if (velFadeIn > 0) sh->setProperty ("velFadeIn", velFadeIn);
    if (velFadeOut > 0) sh->setProperty ("velFadeOut", velFadeOut);
    o->setProperty ("shape", sh.get());
    juce::Array<juce::var> ins; for (const auto& f : inserts) ins.add (f.toVar());
    o->setProperty ("inserts", ins);
    return o.get();
}
LayerState LayerState::fromVar (const juce::var& v)
{
    LayerState l;
    if (! v.isObject()) return l;
    l.instrument = prop (v, "instrument").toString(); l.label = prop (v, "label").toString(); l.role = prop (v, "role").toString();
    if (auto* d = prop (v, "params").getDynamicObject())
        for (const auto& nv : d->getProperties())
        {
            const int i = layerParamIndex (nv.name.toString());
            if (i >= 0) l.p[(size_t) i] = juce::jlimit (layerParam (i).min, layerParam (i).max, (float) (double) nv.value);
        }
    const auto sh = prop (v, "shape");
    l.startRange = juce::jlimit (0.0f, 30.0f, (float) (double) prop (sh, "startRange", 0.0)); l.loopMode = juce::jlimit (0, 2, (int) prop (sh, "loop", 0));
    l.loopStart = clamp01 ((float) (double) prop (sh, "loopStart", 0.35)); l.loopEnd = clamp01 ((float) (double) prop (sh, "loopEnd", 0.94));
    l.loopFade = juce::jlimit (0.001f, 1.0f, (float) (double) prop (sh, "loopFade", 0.2)); l.reverse = (bool) prop (sh, "reverse", false);
    l.voices = juce::jlimit (4, 128, (int) prop (sh, "voices", 32));
    l.keyFadeIn = juce::jlimit (0.0f, 48.0f, (float) (double) prop (sh, "keyFadeIn", 0.0)); l.keyFadeOut = juce::jlimit (0.0f, 48.0f, (float) (double) prop (sh, "keyFadeOut", 0.0));
    l.velFadeIn = juce::jlimit (0.0f, 100.0f, (float) (double) prop (sh, "velFadeIn", 0.0)); l.velFadeOut = juce::jlimit (0.0f, 100.0f, (float) (double) prop (sh, "velFadeOut", 0.0));
    if (auto* a = prop (v, "inserts").getArray()) for (int i = 0; i < kInserts && i < a->size(); ++i) l.inserts[(size_t) i] = FxState::fromVar (a->getReference (i));
    return l;
}

Preset Preset::blank()
{
    Preset p;
    p.sends[0].unit = "chorus"; p.sends[0].on = true;
    p.sends[1].unit = "echo";   p.sends[1].on = true;
    p.sends[2].unit = "halls";  p.sends[2].on = true;
    p.master[0].unit = "eq";  p.master[0].on = false;
    p.master[1].unit = "ott"; p.master[1].on = false; p.master[1].mix = 0.35f;
    p.master[2].unit = "lim"; p.master[2].on = true;
    static const char* names[kMacros] = { "TONE", "MOTION", "AIR", "SPACE" };
    for (int i = 0; i < kMacros; ++i) p.macros[(size_t) i].name = names[i];
    return p;
}

juce::var Preset::toVar() const
{
    auto o = newObject();
    o->setProperty ("format", "ghost-atlas-preset"); o->setProperty ("version", 1);
    o->setProperty ("name", name); o->setProperty ("author", author); o->setProperty ("category", category); if (world.isNotEmpty()) o->setProperty ("world", world);
    o->setProperty ("description", description); o->setProperty ("tags", fromStrings (tags));
    juce::Array<juce::var> la; for (const auto& l : layers) la.add (l.toVar());
    o->setProperty ("layers", la);
    juce::Array<juce::var> sa; for (const auto& s : sends) sa.add (s.toVar());
    o->setProperty ("sends", sa);
    juce::Array<juce::var> ra; for (float r : returns) ra.add (r);
    o->setProperty ("returns", ra);
    juce::Array<juce::var> ma; for (const auto& m : master) ma.add (m.toVar());
    o->setProperty ("masterFx", ma);
    o->setProperty ("masterDb", masterDb);
    juce::Array<juce::var> mc;
    for (const auto& m : macros)
    {
        auto mo = newObject(); mo->setProperty ("name", m.name); mo->setProperty ("value", m.value);
        juce::Array<juce::var> ta;
        for (const auto& t : m.targets) { auto to = newObject(); to->setProperty ("param", t.param); to->setProperty ("amount", t.amount); ta.add (to.get()); }
        mo->setProperty ("targets", ta); mc.add (mo.get());
    }
    o->setProperty ("macros", mc);
    return o.get();
}

Preset Preset::fromVar (const juce::var& v)
{
    Preset p = blank();
    if (! v.isObject()) return p;
    p.name = prop (v, "name", "Untitled").toString(); p.author = prop (v, "author").toString(); p.category = prop (v, "category", "Pads").toString();
    p.world = prop (v, "world", "").toString();
    p.description = prop (v, "description").toString(); p.tags = toStrings (prop (v, "tags"));
    if (auto* a = prop (v, "layers").getArray()) for (int i = 0; i < kLayers && i < a->size(); ++i) p.layers[(size_t) i] = LayerState::fromVar (a->getReference (i));
    if (auto* a = prop (v, "sends").getArray()) for (int i = 0; i < kSends && i < a->size(); ++i) p.sends[(size_t) i] = FxState::fromVar (a->getReference (i));
    if (auto* a = prop (v, "returns").getArray()) for (int i = 0; i < kSends && i < a->size(); ++i) p.returns[(size_t) i] = juce::jlimit (0.0f, 2.0f, (float) (double) a->getReference (i));
    if (auto* a = prop (v, "masterFx").getArray()) for (int i = 0; i < kMasterSlots && i < a->size(); ++i) p.master[(size_t) i] = FxState::fromVar (a->getReference (i));
    p.masterDb = juce::jlimit (-60.0f, 12.0f, (float) (double) prop (v, "masterDb", 0.0));
    if (auto* a = prop (v, "macros").getArray())
        for (int i = 0; i < kMacros && i < a->size(); ++i)
        {
            const auto& m = a->getReference (i);
            p.macros[(size_t) i].name = prop (m, "name", p.macros[(size_t) i].name).toString();
            p.macros[(size_t) i].value = clamp01 ((float) (double) prop (m, "value", 0.0));
            p.macros[(size_t) i].targets.clear();
            if (auto* ta = prop (m, "targets").getArray())
                for (const auto& t : *ta) p.macros[(size_t) i].targets.push_back ({ prop (t, "param").toString(), juce::jlimit (-1.0f, 1.0f, (float) (double) prop (t, "amount", 0.0)) });
        }
    return p;
}

// ============================================================================================ the bank
juce::var PresetBank::factoryData()
{
    static const juce::var data = [] {
        int size = 0;
        if (const char* d = GhostAtlasArt::getNamedResource ("factory_json", size)) return parseJson (juce::String::fromUTF8 (d, size));
        return juce::var();
    }();
    return data;
}

static PresetInfo infoOf (const juce::var& v, const juce::String& file, bool factory)
{
    PresetInfo i;
    i.name = prop (v, "name", "Untitled").toString(); i.category = prop (v, "category", "Pads").toString(); i.world = prop (v, "world").toString();
    if (i.world.isEmpty()) i.world = world (worldOfCategory (i.category)).key;
    i.author = prop (v, "author").toString(); i.description = prop (v, "description").toString(); i.tags = toStrings (prop (v, "tags"));
    i.file = file; i.factory = factory;
    if (auto* a = prop (v, "layers").getArray())
        for (const auto& l : *a)
        {
            const auto id = prop (l, "instrument").toString();
            if (id.isNotEmpty() && (double) prop (prop (l, "params"), "on", 0.0) > 0.5) { i.instruments.add (id); ++i.layers; }
        }
    return i;
}

void PresetBank::loadSettings()
{
    const auto s = readJson (settingsFile());
    favourites = toStrings (prop (s, "favouritePresets")); recents = toStrings (prop (s, "recentPresets"));
}
void PresetBank::saveSettings() const
{
    auto s = readJson (settingsFile());
    juce::DynamicObject::Ptr o = s.getDynamicObject() != nullptr ? s.getDynamicObject() : new juce::DynamicObject();
    o->setProperty ("favouritePresets", fromStrings (favourites)); o->setProperty ("recentPresets", fromStrings (recents));
    writeJson (settingsFile(), o.get());
}

void PresetBank::refresh()
{
    loadSettings();
    items.clear();
    if (auto* a = factoryData().getArray())
        for (int i = 0; i < a->size(); ++i) items.push_back (infoOf (a->getReference (i), "factory:" + prop (a->getReference (i), "name").toString(), true));
    juce::Array<juce::File> files;
    userPresetDir().findChildFiles (files, juce::File::findFiles, true, "*.atlas");
    files.sort();
    for (const auto& f : files)
    {
        const auto v = readJson (f);
        if (v.isObject()) items.push_back (infoOf (v, f.getFullPathName(), false));
    }
    for (auto& i : items) i.favourite = favourites.contains (i.file);
}

bool PresetBank::load (const juce::String& file, Preset& out) const
{
    juce::var v;
    if (file.startsWith ("factory:")) { if (auto* a = factoryData().getArray()) for (const auto& e : *a) if (prop (e, "name").toString() == file.substring (8)) { v = e; break; } }
    else v = readJson (juce::File (file));
    if (! v.isObject()) return false;
    out = Preset::fromVar (v);
    out.factory = file.startsWith ("factory:"); out.file = file;
    return true;
}
bool PresetBank::load (const PresetInfo& i, Preset& out) const { return load (i.file, out); }

juce::File PresetBank::saveUser (const Preset& p)
{
    const auto dir = userPresetDir().getChildFile (safeName (p.category));
    dir.createDirectory();
    const auto f = dir.getChildFile (safeName (p.name) + ".atlas");
    if (! writeJson (f, p.toVar())) return {};
    refresh();
    return f;
}
bool PresetBank::removeUser (const PresetInfo& i)
{
    if (i.factory) return false;
    const juce::File f (i.file);
    const bool ok = f.existsAsFile() && f.moveToTrash();
    refresh();
    return ok;
}
void PresetBank::setFavourite (const juce::String& file, bool on)
{
    if (on) favourites.addIfNotAlreadyThere (file); else favourites.removeString (file);
    for (auto& i : items) if (i.file == file) i.favourite = on;
    saveSettings();
}
void PresetBank::touchRecent (const juce::String& file)
{
    recents.removeString (file); recents.insert (0, file);
    while (recents.size() > 24) recents.remove (recents.size() - 1);
    saveSettings();
}
} // namespace atlas
