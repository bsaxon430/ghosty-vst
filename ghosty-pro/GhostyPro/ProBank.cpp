#include "ProBank.h"
#include <set>
#include "ProProcessor.h"

namespace gpro
{
namespace
{
// ------------------------------------------------------------------ templates (expanded where a spec says @name)
struct Tpl { const char* name; const char* spec; };
const Tpl templates[] = {
    { "pluck",  "attack=.001 decay=.38 sustain=0 release=.35 curve=.8 f_type=lp24 cutoff=.36 res=.22 fenv=.55 f_a=.001 f_d=.32 f_s=0 f_r=.3 vel_f=.15" },
    { "keys",   "attack=.002 decay=1.6 sustain=.25 release=.55 curve=.75 cutoff=.62 fenv=.3 f_a=.001 f_d=.8 f_s=.2 f_r=.5 vel_f=.25 vel_amp=.7" },
    { "pad",    "attack=.9 decay=2 sustain=.85 release=2.4 curve=.5 o1_uni=6 o1_det=.32 o1_spr=.85 cutoff=.62 verb=.5" },
    { "bass",   "mono=1 poly=1 attack=.002 decay=.5 sustain=.8 release=.09 curve=.6 f_type=lp24 cutoff=.42 vel_amp=.35" },
    { "lead",   "mono=1 legato=1 glide=.05 attack=.004 decay=.4 sustain=.85 release=.25 curve=.6 echo=.22 verb=.2" },
    { "ssaw",   "o1_tbl=Saw o1_uni=8 o1_det=.42 o1_spr=.9 o1_bln=.8 attack=.004 release=.45" },
    { "brass",  "attack=.06 decay=.6 sustain=.75 release=.3 f_type=lp24 cutoff=.42 res=.12 fenv=.45 f_a=.07 f_d=.5 f_s=.45 f_r=.3 vel_f=.25" },
    { "bell",   "attack=.001 decay=2.6 sustain=0 release=2.4 curve=.85 vel_amp=.65 verb=.35" },
    { "arp",    "arp=1 arpRate=1/16 arpOct=2 arpGate=.5" },
    { "vib",    "l1_rate=5.2 l1_fade=.6 l1_trig=1 mod=lfo1>fine:.12" },
};

const juce::String& expandTemplates (const juce::String& spec, juce::String& out)
{
    juce::StringArray toks; toks.addTokens (spec, " \t\n", "");
    for (auto& t : toks)
    {
        if (t.isEmpty()) continue;
        if (t.startsWithChar ('@'))
        {
            for (auto& tp : templates) if (t.substring (1) == tp.name) { out << tp.spec << " "; break; }
        }
        else out << t << " ";
    }
    return out;
}

juce::String norm (const juce::String& s) { return s.removeCharacters (" _-").toLowerCase(); }

int findChoice (const juce::StringArray& items, const juce::String& v)
{
    const auto n = norm (v);
    for (int i = 0; i < items.size(); ++i) if (norm (items[i]) == n) return i;
    // also accept "comb+" / "comb-" etc. where removing '-' changed things
    for (int i = 0; i < items.size(); ++i) if (items[i].removeCharacters (" ").toLowerCase() == v.removeCharacters (" ").toLowerCase()) return i;
    return -1;
}

int srcAlias (const juce::String& s)
{
    static const std::pair<const char*, int> a[] { { "lfo1", srcLfo1 }, { "lfo2", srcLfo2 }, { "env2", srcEnv2 }, { "env3", srcEnv3 }, { "fenv", srcFEnv },
        { "amp", srcAmpEnv }, { "vel", srcVel }, { "wheel", srcWheel }, { "at", srcAT }, { "rnd", srcRandom }, { "note", srcNote },
        { "mac1", srcMac1 }, { "mac2", srcMac2 }, { "mac3", srcMac3 }, { "mac4", srcMac4 } };
    for (auto& p : a) if (s == p.first) return p.second;
    return findChoice (modSourceNames(), s);
}
int dstAlias (const juce::String& s)
{
    static const std::pair<const char*, int> a[] { { "pitch", dPitch }, { "fine", dFine }, { "pitch1", dPitch1 }, { "pitch2", dPitch2 }, { "pos1", dPos1 }, { "pos2", dPos2 },
        { "lvl1", dLvl1 }, { "lvl2", dLvl2 }, { "det1", dDet1 }, { "det2", dDet2 }, { "spread", dSpread }, { "xmod", dXmod }, { "sub", dSub }, { "noise", dNoise },
        { "cut", dCutoff }, { "res", dRes }, { "drive", dFDrive }, { "vol", dVolume }, { "pan", dPan }, { "rate1", dLfo1Rate }, { "rate2", dLfo2Rate },
        { "growl", dGrowl }, { "char", dCharacter }, { "tape", dTape }, { "lvly2", dLy2Lvl }, { "lvly3", dLy3Lvl }, { "smpstart", dSmpStart } };
    for (auto& p : a) if (s == p.first) return p.second;
    return findChoice (modDestNames(), s);
}
} // namespace

namespace
{
void setToken (ProProcessor& p, const juce::String& key, const juce::String& val)
{
    auto* prm = p.apvts.getParameter (key);
    if (prm == nullptr) { if (std::getenv ("GP_SPEC_CHECK")) fprintf (stderr, "unknown key %s\n", key.toRawUTF8()); return; }
    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (prm))
    {
        int idx = val.containsOnly ("0123456789") ? val.getIntValue() : findChoice (c->choices, val);
        if (idx < 0) { if (std::getenv ("GP_SPEC_CHECK")) fprintf (stderr, "unknown choice %s=%s\n", key.toRawUTF8(), val.toRawUTF8()); return; }
        p.setParam (key, (float) idx);
    }
    else if (dynamic_cast<juce::AudioParameterBool*> (prm) != nullptr)
        p.setParam (key, (val == "1" || val == "on" || val == "true") ? 1.0f : 0.0f);
    else
        p.setParam (key, val.getFloatValue());
}
}

juce::String expandSpec (const juce::String& spec) { juce::String out; expandTemplates (spec, out); return out.trim(); }

void applyParams (ProProcessor& p, const juce::String& spec)
{
    juce::StringArray toks; toks.addTokens (spec, " ", "");
    for (auto& t : toks) { const int eq = t.indexOfChar ('='); if (eq > 0) setToken (p, t.substring (0, eq), t.substring (eq + 1)); }
}

void applySpec (ProProcessor& p, const juce::String& rawSpec)
{
    juce::String spec; expandTemplates (rawSpec, spec);
    juce::StringArray toks; toks.addTokens (spec, " ", "");
    int slot = 0;
    MacroMap macroMap;                       // mc=template / mc1=NAME~ / mc1:param=amount^curve (ProMacros.h)
    for (auto& t : toks)
    {
        const int eq = t.indexOfChar ('=');
        if (eq <= 0) continue;
        const auto key = t.substring (0, eq), val = t.substring (eq + 1);
        if (key == "ly2" || key == "ly3")      // STACK layer: another factory sound by name (underscores for spaces)
        {
            if (! p.setLayerSound (key.getTrailingIntValue() - 1, val.replaceCharacter ('_', ' ')) && std::getenv ("GP_SPEC_CHECK")) fprintf (stderr, "unknown layer sound %s\n", val.toRawUTF8());
            continue;
        }
        if (key == "mod")
        {
            // mod=src>dst:amount  -> next free matrix slot
            const int gt = val.indexOfChar ('>'), co = val.indexOfChar (':');
            if (gt <= 0 || co <= gt || slot >= NUM_SLOTS) { if (std::getenv ("GP_SPEC_CHECK")) fprintf (stderr, "bad mod %s\n", val.toRawUTF8()); continue; }
            const int s = srcAlias (val.substring (0, gt)), d = dstAlias (val.substring (gt + 1, co));
            if ((s <= 0 || d <= 0) && std::getenv ("GP_SPEC_CHECK")) fprintf (stderr, "bad mod route %s\n", val.toRawUTF8());
            ++slot;
            p.setParam ("m" + juce::String (slot) + "_src", (float) juce::jmax (0, s));
            p.setParam ("m" + juce::String (slot) + "_dst", (float) juce::jmax (0, d));
            p.setParam ("m" + juce::String (slot) + "_amt", val.substring (co + 1).getFloatValue());
            continue;
        }
        if (key.startsWith ("mc") && macroMap.parseToken (key, val)) continue;
        setToken (p, key, val);
    }
    p.macros.setMap (macroMap);
    p.macros.syncIdle();
}

// ------------------------------------------------------------------ the bank
namespace
{
struct Raw { const char* cat; const char* name; const char* scene; const char* spec; };

#if GHOSTY_FACTORY_DATA
const Raw raw[] = {
#include "ProBankData.inc"
#include "ProLeadsPresets.inc"
#include "ProV2Showcase.inc"     // SOUND ENGINE V2: sounds only the new engine can make (category "V2 Showcase", after LEADS: program numbers stay)
};
#elif GHOSTY_GHOST_SET
const Raw raw[] = {
#include "ProGhostSet.inc"
};
#else
const Raw raw[] = { { "", "", "", "" } };   // FACTORY PRESETS OFF, no layer compat: the old data is not compiled in (the one empty row is skipped)
#endif

// The 81 sounds of the free Ghosty, carried over as "Ghosty Classics" (converted to the Pro engine).
struct Classic { const char* cat; const char* name; int A, B; float morph; int uni; float width, sub, a, d, s, r, cut, res, fenv, echo, verb, drive, chorus, phaser, crush, haunt; };
#if GHOSTY_FACTORY_DATA
const Classic classics[] = {
#include "ProClassics.inc"
};
#else
const Classic classics[] = { { "", "", 0, 0, 0.0f, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } };
#endif

juce::String classicSpec (const Classic& c)
{
    auto f = [] (float v) { return juce::String (v, 4); };
    const float det = c.width <= 0 ? 0.0f : std::pow (0.5f * c.width, 1.0f / 1.6f);
    const float cut = juce::jlimit (0.0f, 1.0f, (std::log2 (50.0f / 30.0f) + 8.5f * c.cut) / 9.3f);
    juce::String s;
    s << "o1_tbl=0 waveA=" << c.A << " waveB=" << c.B << " o1_pos=" << f (c.morph) << " o1_uni=" << c.uni << " o1_det=" << f (det)
      << " o1_spr=" << f (juce::jmin (1.0f, c.width * 1.6f)) << " o1_bln=1 sub_lvl=" << f (c.sub * 0.75f)
      << " attack=" << f (juce::jmax (0.001f, c.a)) << " decay=" << f (juce::jmax (0.01f, c.d)) << " sustain=" << f (c.s) << " release=" << f (juce::jmax (0.01f, c.r))
      << " curve=0 vel_amp=.65 f_type=lp12 cutoff=" << f (cut) << " res=" << f (c.res * 9.0f / 11.0f) << " fenv=" << f (c.fenv * 5.0f / 7.0f)
      << " f_a=" << f (c.a * 0.8f + 0.001f) << " f_d=" << f (juce::jmax (0.01f, c.d)) << " f_s=0 f_r=" << f (juce::jmax (0.01f, c.r))
      << " echo=" << f (c.echo) << " verb=" << f (c.verb) << " drive=" << f (c.drive) << " chorus=" << f (c.chorus) << " phaser=" << f (c.phaser)
      << " crush=" << f (c.crush) << " haunt=" << f (c.haunt);
    return s;
}

// the few classics that need a Ghost FX scene (the free Ghosty had none): peak-limited plucks get the transparent limiter
juce::String classicScene (const juce::String& name) { return name == "Hyper Pluck" ? "cleanlimit" : ""; }

struct Trim { const char* name; float db; };
const Trim trims[] = {
    { "", 0.0f },
#if GHOSTY_FACTORY_DATA
#include "ProTrims.inc"
#include "ProLeadsTrims.inc"
#elif GHOSTY_GHOST_SET && __has_include ("ProGhostSetTrims.inc")
#include "ProGhostSetTrims.inc"
#endif
};
} // namespace

const juce::StringArray& bankCategories()
{
    // the TRAP categories come after the classics so every older sound keeps its program number
#if GHOSTY_GHOST_SET
    static juce::StringArray g { "Bass", "Leads", "Keys", "Plucks", "Pads", "Bells", "Rhythm & Arps", "FX & Atmos" };
    return g;
#endif
    static juce::StringArray c { "Supersaws", "Leads", "Plucks", "Keys", "Pads", "Bass", "Brass & Strings", "Bells & Mallets", "Arps & Sequences",
                                 "FX & Atmos", "Ghost Specials", "Ghosty Classics",
                                 "Trap Brass", "Trap Bass", "Trap Keys & Bells", "Trap Leads & Flutes", "Trap Pads & Choirs", "Trap Stacks & FX",
                                 "Leads Hypnotic", "Leads Euphoric", "Leads Aggressive",    // LEADS after TRAP: program numbers stay
                                 "V2 Showcase" };                                           // SOUND ENGINE V2 (2026-10-25): last, program numbers stay
    return c;
}

const std::vector<FactoryPreset>& factoryLibrary()
{
    static const std::vector<FactoryPreset> bank = []
    {
        std::vector<FactoryPreset> b;
        if (! GHOSTY_FACTORY_DATA && ! GHOSTY_GHOST_SET) return b;
        for (auto& cat : bankCategories())
        {
            for (auto& r : raw) if (cat == r.cat) b.push_back ({ r.cat, r.name, r.scene, r.spec });
            if (cat == "Ghosty Classics" && GHOSTY_FACTORY_DATA)
                for (auto& c : classics) b.push_back ({ "Ghosty Classics", c.name, classicScene (c.name), classicSpec (c) });
        }
        return b;
    }();
    return bank;
}

const std::vector<FactoryPreset>& factoryBank()
{
#if GHOSTY_FACTORY_PRESETS
    return factoryLibrary();
#else
    static const std::vector<FactoryPreset> none;   // FACTORY PRESETS OFF: no factory sound presets (a new hand-made set comes later)
    return none;
#endif
}

// LEGACY (SOUND ENGINE V2): factory sounds that stay loadable by name / program number (older projects, host program lists) but are
// hidden from BROWSE unless SHOW LEGACY is on (TAGS panel)
bool isLegacy (const juce::String& name)
{
    static const std::set<juce::String> s = []
    {
        static const char* const list[] {
#if GHOSTY_FACTORY_DATA
#include "ProV2Legacy.inc"
#endif
            nullptr };
        std::set<juce::String> x; for (auto* const* n = list; *n != nullptr; ++n) x.insert (*n);
        return x;
    }();
    return s.count (name) > 0;
}
const juce::String& legacyCategory() { static const juce::String c ("Legacy"); return c; }

float presetTrimDb (const juce::String& name)
{
    static const std::map<juce::String, float> m = []
    {
        std::map<juce::String, float> x;
        for (auto& t : trims) if (t.name[0] != 0) x[t.name] = t.db;
        return x;
    }();
    auto it = m.find (name);
    return it != m.end() ? it->second : 0.0f;
}
// ------------------------------------------------------------------ PRESET BROWSER tags
const juce::StringArray& derivedTagNames()
{
    static const juce::StringArray t { "unison", "fm", "sync", "warp", "mono", "glide", "arp", "reverb", "layered", "clean" };
    return t;
}

juce::StringArray categoryTags (const juce::String& cat)
{
    juce::StringArray w; w.addTokens (cat.toLowerCase(), " &/", "");
    w.removeEmptyStrings(); w.removeString ("ghost"); w.removeString ("ghosty");
    return w;
}

juce::StringArray tagsFromValues (const std::map<juce::String, juce::String>& v, const juce::String& scene)
{
    auto str = [&v] (const char* k) { auto it = v.find (k); return it != v.end() ? it->second : juce::String(); };
    auto num = [&] (const char* k, float def) { const auto s = str (k); return s.isEmpty() ? def : s.getFloatValue(); };
    auto on = [&] (const char* k, bool def) { const auto s = str (k).toLowerCase(); return s.isEmpty() ? def : (s == "on" || s == "true" || s.getFloatValue() > 0.5f); };
    auto choiceIs = [&] (const char* k, const juce::StringArray& names, const juce::String& want)   // a choice given by name or by index
    {
        const auto s = str (k); if (s.isEmpty()) return false;
        const auto n = s.containsOnly ("0123456789.") ? names[s.getIntValue()] : s;
        return norm (n).startsWith (norm (want));
    };
    auto choiceOff = [&] (const char* k, const juce::StringArray& names)
    {
        const auto s = str (k); if (s.isEmpty()) return true;
        const auto n = s.containsOnly ("0123456789.") ? names[s.getIntValue()] : s;
        return n.isEmpty() || norm (n) == "off";
    };
    const bool o1 = on ("o1_on", true), o2 = on ("o2_on", false);
    juce::StringArray t;
    const int uni = juce::jmax (o1 ? (int) num ("o1_uni", 1) : 1, o2 ? (int) num ("o2_uni", 1) : 1);
    if (uni > 1) t.add ("unison");
    bool fm = choiceIs ("xmod", xmodNames(), "fm"), warp = false, sync = choiceIs ("xmod", xmodNames(), "sync");
    for (const char* k : { "o1_tbl", "o2_tbl" }) { const auto s = str (k); if (s.isNotEmpty() && ((s.containsOnly ("0123456789") ? tableNames()[s.getIntValue()] : s).toLowerCase().removeCharacters (" ").startsWith ("fm"))) fm = true; }
    for (const char* k : { "o1_w1", "o1_w2", "o2_w1", "o2_w2" })
        if (! choiceOff (k, warpModeNames()))
        {
            warp = true;
            if (choiceIs (k, warpModeNames(), "fm")) fm = true;
            if (choiceIs (k, warpModeNames(), "sync")) sync = true;
        }
    if (fm) t.add ("fm");
    if (sync) t.add ("sync");
    if (warp) t.add ("warp");
    if (on ("mono", false)) t.add ("mono");
    if (num ("glide", 0.0f) > 0.0005f) t.add ("glide");
    if (on ("arp", false)) t.add ("arp");
    if (num ("verb", 0.0f) >= 0.3f || scene == "space" || scene == "shimmer") t.add ("reverb");
    if (str ("ly2").isNotEmpty() || str ("ly3").isNotEmpty()) t.add ("layered");
    const bool dirty = num ("drive", 0) > 0.001f || num ("f_drive", 0) > 0.001f || num ("crush", 0) > 0.001f || num ("ch_amt", 0) > 0.001f || on ("tp_on", false)
                       || scene == "wreck" || scene == "wreckclean" || scene == "lofi" || scene == "tape" || scene == "crush" || scene == "freak";
    if (! dirty) t.add ("clean");
    return t;
}

const juce::StringArray& factoryTags (int program)
{
    static const std::vector<juce::StringArray> all = []
    {
        std::vector<juce::StringArray> out;
        for (auto& fp : factoryBank())
        {
            juce::String spec; expandTemplates (fp.spec, spec);
            juce::StringArray toks; toks.addTokens (spec, " ", "");
            std::map<juce::String, juce::String> v;
            for (auto& tk : toks) { const int eq = tk.indexOfChar ('='); if (eq > 0) v[tk.substring (0, eq)] = tk.substring (eq + 1); }   // the last value wins (as applySpec)
            auto t = tagsFromValues (v, fp.scene);
            for (auto& c : categoryTags (fp.cat)) t.addIfNotAlreadyThere (c);
            out.push_back (t);
        }
        return out;
    }();
    static const juce::StringArray none;
    return juce::isPositiveAndBelow (program, (int) all.size()) ? all[(size_t) program] : none;
}

juce::StringArray userPresetTags (const juce::File& file)
{
    static std::map<juce::String, std::pair<juce::int64, juce::StringArray>> cache;    // message thread only
    const auto key = file.getFullPathName(); const auto when = file.getLastModificationTime().toMilliseconds();
    auto it = cache.find (key);
    if (it != cache.end() && it->second.first == when) return it->second.second;
    juce::StringArray t;
    if (auto xml = juce::parseXML (file))
    {
        std::map<juce::String, juce::String> v;
        for (auto* c : xml->getChildWithTagNameIterator ("PARAM")) v[c->getStringAttribute ("id")] = c->getStringAttribute ("value");
        if (xml->getStringAttribute ("ly2_sound").isNotEmpty()) v["ly2"] = "1";
        if (xml->getStringAttribute ("ly3_sound").isNotEmpty()) v["ly3"] = "1";
        t = tagsFromValues (v, xml->getStringAttribute ("scene"));
    }
    cache[key] = { when, t };
    return t;
}
} // namespace gpro
