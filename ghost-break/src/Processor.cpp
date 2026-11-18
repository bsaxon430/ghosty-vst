// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Processor.h"
#include "Editor.h"
#include <map>
#include <set>

namespace gbk
{
std::shared_ptr<const SharedLibrary> sharedLibrary()
{
    static const std::shared_ptr<const SharedLibrary> lib = [] { auto l = std::make_shared<SharedLibrary>(); l->breaks.build (48000.0); l->drums.build (48000.0); return l; }();
    return lib;
}
juce::String slotPrefix (int s)
{
    if (s < kBays * 2) return juce::String (bayPrefix (s / 2)) + ((s & 1) ? "filter_" : "crunch_");
    if (isCrush (s)) return juce::String (bayPrefix (slotBay (s))) + "crush_";
    switch (s) { case sStutter: return "stut_"; case sStutFilter: return "sfilt_"; case sStutGate: return "sgate_"; case sChorus: return "chorus_"; case sReverb: return "reverb_"; default: return "limit_"; }
}
juce::String slotTitle (int s)
{
    if (s < kBays * 2) return "BAY " + juce::String::charToString ((juce::juce_wchar) ('A' + s / 2)) + ((s & 1) ? " FILTERS" : " CLIP");
    if (isCrush (s)) return "BAY " + juce::String::charToString ((juce::juce_wchar) ('A' + slotBay (s))) + " CRUSH";
    switch (s) { case sStutter: return "STUTTER"; case sStutFilter: return "STUTTER FILTER"; case sStutGate: return "STUTTER GATE"; case sChorus: return "CHORUS"; case sReverb: return "REVERB"; default: return "LIMITER"; }
}
bool slotParamHidden (int s, const std::string& id) { return s == sStutter && (id == "pad" || id == "latch" || id == "ptype" || id == "psize"); }
bool slotHasOn (int s) { return s != sStutter && s != sChorus && s != sReverb; }
bool slotHasMix (int s) { return ! (s < kBays * 2 && (s & 1) == 0) && s != sStutter && s != sChorus && s != sReverb && phantom::unitDef (slotUnit (s)).hasMix; }

namespace
{
    using APF = juce::AudioParameterFloat; using APB = juce::AudioParameterBool; using APC = juce::AudioParameterChoice; using API = juce::AudioParameterInt;
    juce::ParameterID pid (const juce::String& id) { return { id, 1 }; }
    juce::AudioParameterFloatAttributes pct() { return juce::AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }).withValueFromStringFunction ([] (const juce::String& t) { return (float) (t.getDoubleValue() / 100.0); }); }
    juce::AudioParameterFloatAttributes unit (const juce::String& u, int dec) { return juce::AudioParameterFloatAttributes().withLabel (u).withStringFromValueFunction ([u, dec] (float v, int) { return juce::String (v, dec) + (u.isEmpty() ? juce::String() : " " + u); }); }
    std::unique_ptr<APF> pc (const juce::String& id, const juce::String& name, float def) { return std::make_unique<APF> (pid (id), name, juce::NormalisableRange<float> (0.0f, 1.0f), def, pct()); }

    // the same mapping Phantom FX uses for its own units (a unit's control looks and automates the same in both plugins)
    std::unique_ptr<juce::RangedAudioParameter> makeParam (const juce::String& id, const juce::String& name, phantom::ParamSpec s)
    {
        using T = phantom::ParamSpec::Type;
        switch (s.type)
        {
            case T::Choice: { juce::StringArray items; for (auto& c : s.choices) items.add (c); return std::make_unique<APC> (pid (id), name, items, (int) s.def); }
            case T::Bool: return std::make_unique<APB> (pid (id), name, s.def >= 0.5f);
            case T::Int: return std::make_unique<API> (pid (id), name, (int) s.min, (int) s.max, (int) s.def);
            case T::Float: break;
        }
        juce::NormalisableRange<float> r (s.min, s.max); if (s.skewCentre > s.min && s.skewCentre < s.max) r.setSkewForCentre (s.skewCentre);
        const juce::String u = s.unit;
        auto attrs = juce::AudioParameterFloatAttributes().withLabel (u)
            .withStringFromValueFunction ([u] (float v, int) {
                if (u == "%") return juce::String (juce::roundToInt (v * 100.0f)) + " %";
                if (u == "Hz" && v >= 1000.0f) return juce::String (v / 1000.0f, 2) + " kHz";
                return juce::String (v, 2) + (u.isEmpty() ? juce::String() : " " + u); })
            .withValueFromStringFunction ([u] (const juce::String& t) {
                auto s2 = t.trim().toLowerCase(); double v = s2.getDoubleValue();
                if (u == "%") v /= 100.0;
                if (u == "Hz" && s2.containsChar ('k')) v *= 1000.0;
                return (float) v; });
        return std::make_unique<APF> (pid (id), name, r, s.def, attrs);
    }
    // Ghost Break's starting points for the Phantom FX units (a break wants a different default than a full mix)
    void tune (int slot, phantom::ParamSpec& p)
    {
        auto set = [&] (const char* id, float v) { if (p.id == id) p.def = juce::jlimit (p.min, p.max, v); };
        if (slot < kBays * 2 && (slot & 1)) { set ("routing", 2); set ("cutoff", 2500); set ("reso", 0.35f); }
        if (slot == sStutFilter) { set ("type", 1); set ("cutoff", 1500); set ("envamt", 0.6f); set ("reso", 0.5f); }
        if (slot == sReverb)
        {   // Brandon's reverb (2026-11-13): ROOMS · Deep Space, size 76 %, decay 1.2 s, pre-delay 20 ms, diffusion 38 %, damping 40 %,
            // low cut 80 Hz, modulation 40 %, width 100 %, early 50 %, shimmer 0, freeze off
            set ("mode", 2); set ("size", 0.76f); set ("decay", 1.2f); set ("predelay", 20.0f); set ("diffusion", 0.38f); set ("damp", 0.4f);
            set ("lowcut", 80.0f); set ("mod", 0.4f); set ("width", 1.0f); set ("early", 0.5f); set ("shimmer", 0.0f); set ("freeze", 0.0f);
            if (p.id == "engine") for (size_t i = 0; i < p.choices.size(); ++i) if (juce::String::fromUTF8 (p.choices[i].c_str()).endsWith ("Deep Space")) { p.def = (float) i; break; }
        }
        if (slot == sChorus) set ("depth", 0.4f);
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout Processor::layout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<APB> (pid ("play"), "Play", false));
    l.add (std::make_unique<APF> (pid ("bpm"), "Tempo", juce::NormalisableRange<float> (60.0f, 220.0f, 0.5f), 172.0f, unit ("bpm", 1)));
    l.add (pc ("swing", "Swing", 0.0f)); l.add (pc ("breaks", "Breaks Level", 0.8f)); l.add (pc ("drums", "Drums Level", 0.8f)); l.add (pc ("volume", "Volume", 0.8f));
    // the split sidechain (Duck.h): global settings, then each band's envelope and which drums drive it
    l.add (std::make_unique<APC> (pid ("duck_mode"), "Duck Mode", juce::StringArray { "PUNCH", "SMART" }, 0));
    l.add (std::make_unique<APF> (pid ("duck_xlow"), "Duck Low / Mid Split", juce::NormalisableRange<float> (40.0f, 1000.0f, 0.0f, 0.4f), 160.0f, unit ("Hz", 0)));
    l.add (std::make_unique<APF> (pid ("duck_xhigh"), "Duck Mid / High Split", juce::NormalisableRange<float> (1000.0f, 16000.0f, 0.0f, 0.4f), 5000.0f, unit ("Hz", 0)));
    l.add (std::make_unique<APC> (pid ("duck_slope"), "Duck Split Slope", juce::StringArray { "12 dB", "24 dB", "48 dB" }, 1));
    l.add (std::make_unique<APC> (pid ("duck_look"), "Duck Lookahead", juce::StringArray { "OFF", "1 ms", "2 ms", "5 ms", "10 ms" }, 0));
    l.add (pc ("duck_mix", "Duck Mix", 1.0f));
    l.add (std::make_unique<APF> (pid ("duck_thr"), "Duck Smart Threshold", juce::NormalisableRange<float> (-60.0f, 0.0f), -30.0f, unit ("dB", 1)));
    for (int k = 0; k < kBands; ++k)
    {
        const juce::String p = bandPrefix (k), n = "Duck " + juce::String (bandName (k)).toLowerCase().substring (0, 1).toUpperCase() + juce::String (bandName (k)).toLowerCase().substring (1) + " ";
        l.add (std::make_unique<APB> (pid (p + "on"), n + "On", true));
        l.add (std::make_unique<APF> (pid (p + "atk"), n + "Attack", juce::NormalisableRange<float> (0.1f, 50.0f, 0.0f, 0.4f), 1.5f, unit ("ms", 1)));
        l.add (std::make_unique<APF> (pid (p + "hold"), n + "Hold", juce::NormalisableRange<float> (0.0f, 200.0f, 0.0f, 0.5f), 0.0f, unit ("ms", 0)));
        l.add (std::make_unique<APF> (pid (p + "rel"), n + "Release", juce::NormalisableRange<float> (10.0f, 1000.0f, 0.0f, 0.4f), k == 0 ? 160.0f : k == 1 ? 120.0f : 70.0f, unit ("ms", 0)));
        l.add (pc (p + "shape", n + "Release Shape", 0.5f));
        for (int d = 0; d < kDrums; ++d) l.add (std::make_unique<APB> (pid (p + "from_" + juce::String (drumPrefix (d)).dropLastCharacters (1)), n + "From Lane " + juce::String (d + 1), defaultRoute (k, d)));
    }
    l.add (std::make_unique<APB> (pid ("stutter"), "Stutter", false));
    { juce::StringArray t { "STUTTER", "BUILD", "GATE", "GLITCH", "REVERSE", "TAPE STOP", "LP SWEEP", "HP SWEEP", "CRUSH", "PITCH DROP" }; l.add (std::make_unique<APC> (pid ("stut_type"), "Stutter Type", t, 0)); }
    { juce::StringArray t { "1/64", "1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4T", "1/4", "1/2", "1 BAR", "2 BARS" }; l.add (std::make_unique<APC> (pid ("stut_rate"), "Stutter Rate", t, 3)); }
    l.add (std::make_unique<APC> (pid ("tempo_mul"), "Tempo", juce::StringArray { "1/2x", "1x", "2x" }, 1));
    l.add (std::make_unique<APC> (pid ("stut_target"), "Stutter Target", juce::StringArray { "BREAKS", "DRUMS", "BOTH" }, 0));
    l.add (std::make_unique<APB> (pid ("stut_link"), "Stutter Filter + Gate Only While Stuttering", true));
    l.add (std::make_unique<APB> (pid ("drums_out"), "Drums To Own Output", false));
    l.add (std::make_unique<APB> (pid ("choke"), "Closed Hat Chokes Open Hat", true));
    l.add (pc ("rule_density", "Rule Density", 0.85f)); l.add (pc ("rule_chaos", "Rule Chaos", 0.35f)); l.add (pc ("rule_ratchet", "Rule Stutter Hits", 0.15f));
    l.add (pc ("rule_reverse", "Rule Reverse", 0.08f)); l.add (pc ("rule_trade", "Rule Trade", 0.5f));
    l.add (std::make_unique<APC> (pid ("rule_slice"), "Rule Slice Length", juce::StringArray { "1/4", "1/8", "1/16", "MIXED" }, 3));
    l.add (std::make_unique<APB> (pid ("rule_newbreaks"), "Rule New Breaks", true));
    for (int b = 0; b < kBays; ++b)
    {
        const juce::String p = bayPrefix (b), n = "Bay " + juce::String::charToString ((juce::juce_wchar) ('A' + b)) + " ";
        l.add (std::make_unique<API> (pid (p + "nudge"), n + "Nudge", 0, 31, 0));
        l.add (std::make_unique<APC> (pid (p + "grid"), n + "Nudge Grid", juce::StringArray { "1/4", "1/8", "1/16", "1/32", "1/64" }, 2));
        l.add (std::make_unique<APF> (pid (p + "pitch"), n + "Pitch", juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f, unit ("st", 2)));
        l.add (pc (p + "level", n + "Level", 0.75f));
        l.add (std::make_unique<APF> (pid (p + "pan"), n + "Pan", juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f, unit ("", 2)));
        l.add (pc (p + "decay", n + "Slice Decay", 1.0f));
        l.add (std::make_unique<APB> (pid (p + "mute"), n + "Mute", b != 0));
        l.add (pc (p + "duck_low", n + "Kick Ducks Lows", b == 0 ? 0.6f : 0.0f)); l.add (pc (p + "duck_mid", n + "Snare Ducks Mids", b == 0 ? 0.35f : 0.0f)); l.add (pc (p + "duck_high", n + "Hats Duck Highs", 0.0f));
        l.add (pc (p + "send_cho", n + "Chorus Send", 0.0f)); l.add (pc (p + "send_rev", n + "Reverb Send", 0.0f));
        l.add (std::make_unique<APB> (pid (p + "out"), n + "To Own Output", false));
        l.add (std::make_unique<APB> (pid (p + "lock"), n + "Lock", false));
        l.add (std::make_unique<APB> (pid (p + "filter_drums"), n + "Filter The Drums Too", false));   // FILTER +DRUMS: the drums go through this bay's filter as well
    }
    for (int d = 0; d < kDrums; ++d)
    {
        const juce::String p = drumPrefix (d), n = "Lane " + juce::String (d + 1) + " ";
        l.add (pc (p + "level", n + "Level", 0.75f));
        l.add (std::make_unique<APF> (pid (p + "pan"), n + "Pan", juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f, unit ("", 2)));
        l.add (std::make_unique<APF> (pid (p + "tune"), n + "Tune", juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f, unit ("st", 2)));
        l.add (std::make_unique<APB> (pid (p + "mute"), n + "Mute", false));
        l.add (std::make_unique<APB> (pid (p + "lock"), n + "Lock", false));   // a locked lane keeps its hits when RANDOM DRUMS makes a new beat
    }
    // every control of every hosted Phantom FX unit
    for (int s = 0; s < kNumSlots; ++s)
    {
        const juce::String p = slotPrefix (s); juce::String t = slotTitle (s).toLowerCase(); t = t.substring (0, 1).toUpperCase() + t.substring (1) + " ";
        if (slotHasOn (s)) l.add (std::make_unique<APB> (pid (p + "on"), t + "On", s == sLimiter));
        if (slotHasMix (s)) l.add (pc (p + "mix", t + "Mix", 1.0f));
        for (auto spec : makeSlotEffect (s)->parameters())
        {
            if (slotParamHidden (s, spec.id)) continue;
            tune (s, spec); l.add (makeParam (p + juce::String (spec.id), t + juce::String (spec.name), spec));
        }
    }
    return l;
}

Processor::Processor()
    : juce::AudioProcessor (BusesProperties().withOutput ("Main", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Bay A", juce::AudioChannelSet::stereo(), false).withOutput ("Bay B", juce::AudioChannelSet::stereo(), false)
                                .withOutput ("Bay C", juce::AudioChannelSet::stereo(), false).withOutput ("Bay D", juce::AudioChannelSet::stereo(), false)
                                .withOutput ("Drums", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "GHOSTBREAK", layout())
{
   #ifdef GHOSTBREAK_VERSION
    build = GHOSTBREAK_VERSION;
   #endif
    lib = sharedLibrary();
    for (int s = 0; s < kNumSlots; ++s)
    {
        const juce::String p = slotPrefix (s);
        if (slotHasOn (s)) sp[(size_t) s].on = apvts.getRawParameterValue (p + "on");
        if (slotHasMix (s)) sp[(size_t) s].mix = apvts.getRawParameterValue (p + "mix");
        for (auto& spec : engine.slot (s).params()) sp[(size_t) s].v.push_back (slotParamHidden (s, spec.id) ? nullptr : apvts.getRawParameterValue (p + juce::String (spec.id)));
    }
    for (int b = 0; b < kBays; ++b)
    {
        const juce::String p = bayPrefix (b); auto g = [&] (const char* id) { return apvts.getRawParameterValue (p + id); };
        bayRaw[b] = { g ("nudge"), g ("grid"), g ("pitch"), g ("level"), g ("pan"), g ("decay"), g ("mute"), { g ("duck_low"), g ("duck_mid"), g ("duck_high") }, g ("send_cho"), g ("send_rev"), g ("out") };
    }
    for (int d = 0; d < kDrums; ++d) { const juce::String p = drumPrefix (d); drumRaw[d] = { apvts.getRawParameterValue (p + "level"), apvts.getRawParameterValue (p + "pan"), apvts.getRawParameterValue (p + "tune"), apvts.getRawParameterValue (p + "mute") }; }
    gPlay = apvts.getRawParameterValue ("play"); gBpm = apvts.getRawParameterValue ("bpm"); gSwing = apvts.getRawParameterValue ("swing"); gBreaks = apvts.getRawParameterValue ("breaks");
    gDrums = apvts.getRawParameterValue ("drums"); gVolume = apvts.getRawParameterValue ("volume"); gStutter = apvts.getRawParameterValue ("stutter");
    gStutLink = apvts.getRawParameterValue ("stut_link"); gDrumsOut = apvts.getRawParameterValue ("drums_out");
    for (auto id : { "duck_mode", "duck_xlow", "duck_xhigh", "duck_slope", "duck_look", "duck_mix", "duck_thr" }) duckG.push_back (apvts.getRawParameterValue (id));
    for (int k = 0; k < kBands; ++k)
    {
        const juce::String p = bandPrefix (k); auto& r = duckB[k];
        r.on = apvts.getRawParameterValue (p + "on"); r.atk = apvts.getRawParameterValue (p + "atk"); r.hold = apvts.getRawParameterValue (p + "hold"); r.rel = apvts.getRawParameterValue (p + "rel"); r.shape = apvts.getRawParameterValue (p + "shape");
        for (int d = 0; d < kDrums; ++d) r.from[d] = apvts.getRawParameterValue (p + "from_" + juce::String (drumPrefix (d)).dropLastCharacters (1));
    }
    rescanUserBreaks(); rescanUserDrums();
    defaultPattern(); publish(); pushStutterPad();
    startTimerHz (10);
}
Processor::~Processor() { stopTimer(); engine.setPattern (nullptr); }

void Processor::defaultPattern()
{
    cur = Pattern(); const auto& br = lib->breaks.breaks();
    auto byName = [&] (const char* nm) { for (auto& b : br) if (b->name == nm) return b; return br.empty() ? mk::BreakPtr() : br.front(); };
    cur.brk = { byName ("Jungle Roller - Original"), byName ("Rolling - Ghosts"), byName ("Haunt - Original"), byName ("Two Step - Dirty") };
    {   // with the real break pack there: the four bays open on real breaks, the first one of the first four packs (folders)
        std::vector<const BreakEntry*> firsts; juce::StringArray seen;
        // Brandon's opening four (2026-11-14): AMEN, APACHE, COLD SWEAT, FLOW (the first file whose name starts so)
        static const char* const want[] { "KAB1_137_AMENBREAK_CUT_0", "KAPB1_115_APACHEBREAK_00", "KCSB1_112.5_COLDSWEATBREA", "KFB1_159_FLOWBREAK_ORIGIN" };
        for (auto* w : want)
            for (auto& c : catalog) if (c.kind == classicBreak && c.name.startsWithIgnoreCase (w)) { firsts.push_back (&c); seen.add (c.section); break; }
        if (firsts.size() != (size_t) kBays) { firsts.clear(); seen.clear(); }
        for (auto& c : catalog) if (c.kind == classicBreak && ! seen.contains (c.section)) { seen.add (c.section); firsts.push_back (&c); }
        for (auto& c : catalog) if (firsts.size() < (size_t) kBays && c.kind == classicBreak && std::find (firsts.begin(), firsts.end(), &c) == firsts.end()) firsts.push_back (&c);
        for (int b = 0; b < kBays && b < (int) firsts.size(); ++b) { juce::String err; if (auto x = loadFile (firsts[(size_t) b]->file, err)) cur.brk[(size_t) b] = x; }
    }
    for (int b = 0; b < kBays; ++b) cur.straight (b);
    static const char* want[kDrums] { "Punch Kick", "Tight Snare", "Hat", "", "Clap", "" };
    for (int d = 0; d < kDrums; ++d) { auto s = soundsFor (d); gb::SoundPtr pick = s.empty() ? nullptr : s.front(); for (auto& x : s) if (x->name == want[d]) pick = x; cur.snd[(size_t) d] = pick; }
    // the MICKENS drums (Music/GhostBlock/Ghost Break/Drums) are the starting kit when they are there: the first kick, snare and closed hat
    for (int d : { (int) dKick, (int) dSnare, (int) dHat }) for (auto& e : drumCat) if (e.source == "MICKENS" && e.tab == d) { cur.snd[(size_t) d] = e.s; break; }
    const char* k = "x.........x.....x.........x.....", *sn = "....x.......x.......x.......x...";
    for (int i = 0; i < kSteps; ++i) { cur.drum[dKick][(size_t) i] = k[i] == 'x' ? 118 : 0; cur.drum[dSnare][(size_t) i] = sn[i] == 'x' ? 115 : 0; cur.drum[dHat][(size_t) i] = i % 2 == 0 ? (i % 4 == 2 ? 100 : 70) : 0; }
}
std::vector<gb::SoundPtr> Processor::soundsFor (int d) const
{
    std::vector<gb::SoundPtr> v; const juce::String own = drumCategory (d);
    for (auto& s : userDrums) if (s->category == own) v.push_back (s);
    for (auto& s : userDrums) if (s->category != own) v.push_back (s);
    for (auto& s : lib->drums.sounds()) if (s->category == own) v.push_back (s);
    for (auto& s : lib->drums.sounds()) if (s->category != own) v.push_back (s);
    return v;
}
juce::String Processor::laneName (int d) const { d = juce::jlimit (0, kDrums - 1, d); return cur.laneName[(size_t) d].isNotEmpty() ? cur.laneName[(size_t) d] : juce::String (drumName (d)); }
void Processor::setLaneName (int d, const juce::String& n)
{
    if (d < 0 || d >= kDrums) return;
    juce::String t = n.trim().toUpperCase().substring (0, 16); if (t == drumName (d)) t = {};
    if (t != cur.laneName[(size_t) d]) edit ([&] (Pattern& p) { p.laneName[(size_t) d] = t; });
}
juce::File Processor::drumsFolder() const { return breaksFolder().getParentDirectory().getChildFile ("Drums"); }
namespace
{
    // which lane a file belongs to, from its folder names first, then its own name
    int laneFor (const juce::File& f, const juce::File& root)
    {
        // categories: 0 Kick, 1 Snare, 2 Hat, 3 Open Hat, 4 Clap, 5 Perc (drumCategory's order)
        auto pick = [] (juce::String t) -> int {
            t = t.toLowerCase();
            if (t.contains ("open") || t.contains ("ohat") || t.contains ("ophat") || t.contains ("ride") || t.contains ("crash") || t.contains ("cymbal")) return 3;
            if (t.contains ("kick") || t.contains ("kik") || t.contains ("bd")) return 0;
            if (t.contains ("clap") || t.contains ("clp")) return 4;
            if (t.contains ("snare") || t.contains ("snr") || t.contains ("rim")) return 1;
            if (t.contains ("hat") || t.contains ("hh") || t.contains ("hihat")) return 2;
            if (t.contains ("perc") || t.contains ("shaker") || t.contains ("tom") || t.contains ("conga") || t.contains ("bongo") || t.contains ("cowbell") || t.contains ("tamb")) return 5;
            return -1; };
        for (auto p = f.getParentDirectory(); p != root && p.isAChildOf (root); p = p.getParentDirectory()) if (const int l = pick (p.getFileName()); l >= 0) return l;
        const int l = pick (f.getFileNameWithoutExtension()); return l >= 0 ? l : 5;
    }
    // every user drum sound, decoded once for every instance (a later rescan adds new files)
    struct DrumCache { juce::CriticalSection lock; std::map<juce::String, gb::SoundPtr> byPath; };
    DrumCache& drumCache() { static DrumCache c; return c; }
}
// one folder of one-shots -> catalogue entries of one source. Kinds come from folder names, then file names; a hat ringing longer than
// a quarter second counts as an open hat. `kitFromFirstFolder`: the first folder level names the kit (Classic Break Kits/Amen/Kicks/...)
void Processor::scanDrumFolder (const juce::File& dir, const juce::String& source, bool kitFromFirstFolder, std::set<juce::String>& names)
{
    if (! dir.isDirectory()) return;
    auto files = dir.findChildFiles (juce::File::findFiles, true, "*.wav;*.aif;*.aiff;*.flac;*.mp3");
    std::sort (files.begin(), files.end(), [] (const juce::File& x, const juce::File& y) { return x.getFullPathName().compareNatural (y.getFullPathName()) < 0; });
    juce::AudioFormatManager fm; fm.registerBasicFormats(); auto& cache = drumCache(); const juce::ScopedLock sl (cache.lock);
    for (int i = 0; i < files.size() && i < 3000; ++i)
    {
        const auto& f = files.getReference (i); if (f.getFileName().startsWithChar ('.')) continue;
        gb::SoundPtr snd; auto it = cache.byPath.find (f.getFullPathName());
        if (it != cache.byPath.end()) snd = it->second;
        else
        {
            std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f)); if (! r || r->sampleRate < 8000 || r->lengthInSamples < 16) continue;
            const int n = (int) std::min<juce::int64> (r->lengthInSamples, (juce::int64) (r->sampleRate * 4.0));
            juce::AudioBuffer<float> b ((int) juce::jlimit (1u, 2u, r->numChannels), n); r->read (&b, 0, n, 0, true, true);
            auto s2 = std::make_shared<gb::Sound>(); s2->rate = r->sampleRate; s2->data.assign ((size_t) n, 0.0f);
            for (int c = 0; c < b.getNumChannels(); ++c) for (int k = 0; k < n; ++k) s2->data[(size_t) k] += b.getSample (c, k) / (float) b.getNumChannels();
            float pk = 1.0e-6f; for (float x : s2->data) pk = juce::jmax (pk, std::abs (x)); for (float& x : s2->data) x *= 0.89f / pk;
            for (int k = 0; k < 64 && k < n; ++k) s2->data[(size_t) (n - 1 - k)] *= (float) k / 64.0f;   // click-free end
            juce::String cat = drumCategory (laneFor (f, dir));
            if (cat == "Hat")
            {   // open or closed: how long it rings above -40 dB
                int last = 0; for (int k = 0; k < n; ++k) if (std::abs (s2->data[(size_t) k]) > 0.0089f) last = k;
                if (last > (int) (0.25 * r->sampleRate)) cat = "Open Hat";
            }
            s2->category = cat; s2->name = f.getFileNameWithoutExtension();
            snd = s2; cache.byPath[f.getFullPathName()] = s2;
        }
        // names are how a project remembers its sounds: keep them unique
        if (names.count (snd->name) || lib->drums.byName (snd->name))
        {
            auto c = std::make_shared<gb::Sound> (*snd); c->name = snd->name + " (" + f.getParentDirectory().getFileName() + ")";
            if (names.count (c->name)) continue;
            snd = c; cache.byPath[f.getFullPathName()] = c;
        }
        names.insert (snd->name); userDrums.push_back (snd);
        juce::String kit; if (kitFromFirstFolder) { const auto rel = f.getRelativePathFrom (dir).replaceCharacter ('\\', '/'); if (rel.containsChar ('/')) kit = rel.upToFirstOccurrenceOf ("/", false, false); }
        drumCat.push_back ({ snd, source, kit, kindTab (snd->category) });
    }
}
void Processor::rescanUserDrums()
{
    userDrums.clear(); drumCat.clear(); std::set<juce::String> names;
    scanDrumFolder (drumsFolder(), "MICKENS", false, names);
    // (the classic break kits are no longer offered: just your Mickens drums and the built-in ones)
    for (auto& x : lib->drums.sounds()) drumCat.push_back ({ x, "BUILT-IN", {}, kindTab (x->category) });
}
juce::File Processor::kitsFolder() const { return breaksFolder().getParentDirectory().getChildFile ("Classic Break Kits"); }
void Processor::setDrumSound (int d, const gb::SoundPtr& s)
{
    if (d < 0 || d >= kDrums || s == nullptr) return;
    edit ([&] (Pattern& p) { p.snd[(size_t) d] = s; });
}
std::vector<mk::BreakPtr> Processor::allBreaks() const { const juce::ScopedLock sl (userLock); auto v = lib->breaks.breaks(); v.insert (v.end(), userBreaks.begin(), userBreaks.end()); return v; }
juce::File Processor::breaksFolder() const { return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("GhostBlock").getChildFile ("Ghost Break").getChildFile ("Breaks"); }
juce::File Processor::yourBreaksFolder() const { return breaksFolder().getParentDirectory().getChildFile ("Your Breaks"); }
// lists the classic breaks (every audio file under the Breaks folder), the built-in ones, then yours (the Your Breaks folder and the files
// added by hand). Names only: a file is decoded when a bay picks it.
void Processor::rescanUserBreaks()
{
    if (! catalog.empty()) rescanUserDrums();   // (the constructor scans the drums itself, after the breaks)
    catalog.clear(); const auto& br = lib->breaks.breaks();
    auto scan = [this] (const juce::File& dir, int kind, const juce::String& rootSection)
    {
        if (! dir.isDirectory()) return;
        auto files = dir.findChildFiles (juce::File::findFiles, true, "*.wav;*.aif;*.aiff;*.flac;*.mp3");
        std::sort (files.begin(), files.end(), [] (const juce::File& x, const juce::File& y) { return x.getFullPathName().compareNatural (y.getFullPathName()) < 0; });
        for (int i = 0; i < files.size() && i < 4000; ++i)
        {
            const auto& f = files.getReference (i); if (f.getFileName().startsWithChar ('.')) continue;
            const juce::String rel = f.getRelativePathFrom (dir).replaceCharacter ('\\', '/');
            const juce::String sec = rel.containsChar ('/') ? rel.upToFirstOccurrenceOf ("/", false, false) : rootSection;
            catalog.push_back ({ f.getFileNameWithoutExtension(), sec, -1, f, kind });
        }
    };
    scan (breaksFolder(), classicBreak, "Classic breaks");
    // the home-made re-creations only stand in when there are no real breaks at all (a machine without the pack): the real records are the point
    if (catalog.empty()) for (int i = 0; i < (int) br.size(); ++i) catalog.push_back ({ br[(size_t) i]->name, br[(size_t) i]->family, i, juce::File(), builtInBreak });
    scan (yourBreaksFolder(), yourBreak, "Your breaks");
    const juce::ScopedLock sl (userLock);
    for (auto& u : userBreaks) { bool listed = false; for (auto& c : catalog) if (c.libIndex < 0 && c.file.getFullPathName() == u->family) listed = true; if (! listed) catalog.push_back ({ u->name, "Your breaks", -1, juce::File (u->family), yourBreak }); }
}
void Processor::resetBay (int bay)
{
    if (bay < 0 || bay >= kBays) return;
    edit ([bay] (Pattern& p) { p.brk[(size_t) bay] = nullptr; p.straight (bay); p.stepLock[(size_t) bay] = 0; });
}
void Processor::resetBays()
{
    edit ([] (Pattern& p) { for (int b = 0; b < kBays; ++b) { p.brk[(size_t) b] = nullptr; p.straight (b); p.stepLock[(size_t) b] = 0; } });
}
float Processor::uiScale() const { return juce::jlimit (0.6f, 2.5f, (float) (double) apvts.state.getProperty ("ui_scale", 1.0)); }
void Processor::setUiScale (float s) { apvts.state.setProperty ("ui_scale", (double) juce::jlimit (0.6f, 2.5f, s), nullptr); }
// decodes a file (kept while a bay uses it). The tempo comes from the file name when it carries one ("..._174_..."), else from its length.
mk::BreakPtr Processor::loadFile (const juce::File& f, juce::String& err)
{
    const juce::ScopedLock sl (userLock);
    for (auto& u : userBreaks) if (u->family == f.getFullPathName()) return u;
    auto b = mk::BreakLibrary::fromFile (f, hostBpmNow.load(), err); if (! b) return nullptr;
    auto c = std::make_shared<mk::Break> (*b); c->family = f.getFullPathName();
    for (auto& t : juce::StringArray::fromTokens (f.getFileNameWithoutExtension(), "_- ", ""))
    {
        if (! t.containsOnly ("0123456789") || t.length() < 2 || t.length() > 3) continue;
        const int bpm = t.getIntValue(); if (bpm < 60 || bpm > 220) continue;
        const double secs = (double) c->data.size() / c->rate; const int sl = (int) std::lround (secs * bpm / 60.0 * 4.0);
        if (sl >= 4) { c->bpm = bpm; c->slices = sl; c->tempoFound = true; }
        break;
    }
    if (userBreaks.size() > 48) userBreaks.erase (std::remove_if (userBreaks.begin(), userBreaks.end(), [] (const mk::BreakPtr& x) { return x.use_count() == 1; }), userBreaks.end());
    userBreaks.push_back (c); return c;
}
int Processor::bayBreakIndex (int bay) const
{
    const auto& br = cur.brk[(size_t) juce::jlimit (0, kBays - 1, bay)]; if (! br) return -1;
    const auto& lb = lib->breaks.breaks();
    for (int i = 0; i < (int) catalog.size(); ++i)
    {
        const auto& c = catalog[(size_t) i];
        if (c.libIndex >= 0 ? (c.libIndex < (int) lb.size() && lb[(size_t) c.libIndex] == br) : (br->user && c.file.getFullPathName() == br->family)) return i;
    }
    return -1;
}
juce::String Processor::bayBreakName (int bay) const { const auto& br = cur.brk[(size_t) juce::jlimit (0, kBays - 1, bay)]; return br ? br->name : juce::String ("(no break)"); }
void Processor::setBayBreak (int bay, int index)
{
    if (bay < 0 || bay >= kBays || index < 0 || index >= (int) catalog.size()) return;
    const auto c = catalog[(size_t) index]; mk::BreakPtr br; juce::String err;
    if (c.libIndex >= 0) { const auto& lb = lib->breaks.breaks(); if (c.libIndex < (int) lb.size()) br = lb[(size_t) c.libIndex]; } else br = loadFile (c.file, err);
    if (! br) return;
    edit ([&] (Pattern& p) { p.brk[(size_t) bay] = br; const int ns = juce::jmax (1, br->slices); for (auto& e : p.seq[(size_t) bay]) e.slice = (int16_t) (e.slice % ns); });
}
// a break added by hand (ADD / drag and drop) from anywhere else is copied into Your Breaks, so it is in YOUR BREAKS next time too
// (the original is never moved; nothing there is overwritten). If the copy cannot be made, the original is used where it is.
juce::File Processor::importBreak (const juce::File& f) const
{
    if (! f.existsAsFile() || f.isAChildOf (breaksFolder()) || f.isAChildOf (yourBreaksFolder())) return f;
    const auto dir = yourBreaksFolder(); auto dest = dir.getChildFile (f.getFileName());
    if (dest.existsAsFile() && dest.getSize() != f.getSize()) dest = dest.getNonexistentSibling (false);
    return dir.createDirectory() && (dest.existsAsFile() || f.copyFileTo (dest)) ? dest : f;
}
bool Processor::addUserBreak (const juce::File& f, int bay, juce::String& err)
{
    if (bay < 0 || bay >= kBays) return false;
    auto br = loadFile (f, err); if (! br) return false;
    bool listed = false; for (auto& c : catalog) if (c.libIndex < 0 && c.file == f) listed = true;
    if (! listed) catalog.push_back ({ br->name, "Your breaks", -1, f, yourBreak });
    edit ([&] (Pattern& p) { p.brk[(size_t) bay] = br; const int ns = juce::jmax (1, br->slices); for (auto& e : p.seq[(size_t) bay]) e.slice = (int16_t) (e.slice % ns); });
    return true;
}
void Processor::setDrumSound (int d, int index)
{
    const auto s = soundsFor (d); if (d < 0 || d >= kDrums || index < 0 || index >= (int) s.size()) return;
    edit ([&] (Pattern& p) { p.snd[(size_t) d] = s[(size_t) index]; });
}

juce::var Processor::slotData (int s) const { return const_cast<Engine&> (engine).slot (s).fx().getData(); }
void Processor::setSlotData (int s, const juce::var& v) { if (s >= 0 && s < kNumSlots && s != sStutter) { engine.slot (s).fx().setData (v); sendChangeMessage(); } }

void Processor::publish()
{
    auto next = std::make_shared<const Pattern> (cur);
    if (live) retired.push_back ({ live, engine.blockCount.load() });
    live = next; engine.setPattern (live.get());
}
void Processor::edit (const std::function<void (Pattern&)>& fn, bool undoable)
{
    const juce::ScopedLock sl (editLock);
    if (! randomEditing) { fromRandom = false; awayValid = false; }   // a hand edit: this is "mine" now
    if (undoable) { UndoState u; u.p = cur; for (int b = 0; b < kBays; ++b) u.nudge[b] = (int) std::lround (bayRaw[b].nudge->load()); undoStack.push_back (std::move (u)); if (undoStack.size() > 32) undoStack.erase (undoStack.begin()); }
    fn (cur); publish(); sendChangeMessage();
}
void Processor::undo()
{
    const juce::ScopedLock sl (editLock);
    if (undoStack.empty()) return;
    auto u = std::move (undoStack.back()); undoStack.pop_back(); cur = u.p;
    for (int b = 0; b < kBays; ++b) if (auto* p = apvts.getParameter (juce::String (bayPrefix (b)) + "nudge")) p->setValueNotifyingHost (p->convertTo0to1 ((float) u.nudge[b]));
    publish(); sendChangeMessage();
}
Rules Processor::rules() const
{
    Rules r; r.density = raw ("rule_density"); r.chaos = raw ("rule_chaos"); r.ratchet = raw ("rule_ratchet"); r.reverse = raw ("rule_reverse"); r.trade = raw ("rule_trade");
    r.sliceLen = (int) std::lround (raw ("rule_slice")); r.newBreaks = raw ("rule_newbreaks") > 0.5f; return r;
}
Locks Processor::locks() const
{
    Locks l; for (int b = 0; b < kBays; ++b) l.bay[b] = raw (juce::String (bayPrefix (b)) + "lock") > 0.5f; for (int d = 0; d < kDrums; ++d) l.drum[d] = raw (juce::String (drumPrefix (d)) + "lock") > 0.5f; return l;
}
std::array<BayP, kBays> Processor::bayParams() const
{
    std::array<BayP, kBays> B;
    for (int b = 0; b < kBays; ++b)
    {
        auto& q = B[(size_t) b]; const auto& r = bayRaw[b];
        q.nudge = (int) std::lround (r.nudge->load()); q.grid = (int) std::lround (r.grid->load()); q.pitch = r.pitch->load(); q.level = r.level->load(); q.pan = r.pan->load(); q.decay = r.decay->load();
        q.mute = r.mute->load() > 0.5f; for (int e = 0; e < 3; ++e) q.duck[e] = r.duck[e]->load(); q.sendChorus = r.cho->load(); q.sendReverb = r.rev->load(); q.ownOut = r.out->load() > 0.5f;
    }
    return B;
}
DuckP Processor::duckParams() const
{
    DuckP d; d.smart = duckG[0]->load() > 0.5f; d.xLow = duckG[1]->load(); d.xHigh = duckG[2]->load(); d.slope = (int) std::lround (duckG[3]->load()); d.mix = duckG[5]->load(); d.thrDb = duckG[6]->load();
    static const float look[] { 0, 1, 2, 5, 10 }; d.lookMs = look[juce::jlimit (0, 4, (int) std::lround (duckG[4]->load()))];
    for (int k = 0; k < kBands; ++k)
    {
        auto& b = d.band[(size_t) k]; const auto& r = duckB[k];
        b.on = r.on->load() > 0.5f; b.attackMs = r.atk->load(); b.holdMs = r.hold->load(); b.releaseMs = r.rel->load(); b.shape = r.shape->load();
        b.route = 0; for (int l = 0; l < kDrums; ++l) if (r.from[l]->load() > 0.5f) b.route |= (uint8_t) (1u << l);
    }
    return d;
}
void Processor::beginRandom()
{   // the first RANDOM after a hand-made beat keeps that beat as "mine"
    const juce::ScopedLock sl (editLock);
    awayValid = false;   // a new RANDOM: BACK TO THE BREAK starts over from it
    if (! fromRandom) { home.p = cur; for (int b = 0; b < kBays; ++b) home.nudge[b] = (int) std::lround (bayRaw[b].nudge->load()); homeValid = true; }
    fromRandom = true;
}
Processor::UndoState Processor::snapshot() const
{
    UndoState u; u.p = cur; for (int b = 0; b < kBays; ++b) u.nudge[b] = (int) std::lround (bayRaw[b].nudge->load()); return u;
}
void Processor::restore (const UndoState& u)
{
    randomEditing = true; edit ([&u] (Pattern& p) { p = u.p; }); randomEditing = false;
    for (int b = 0; b < kBays; ++b) if (auto* p = apvts.getParameter (juce::String (bayPrefix (b)) + "nudge")) p->setValueNotifyingHost (p->convertTo0to1 ((float) u.nudge[b]));
}
void Processor::backAway()
{   // BACK TO THE BREAK: the random beat you just left with BACK TO MY BEAT (and the button flips back)
    if (! canGoAway()) return;
    const auto a = away; restore (a); fromRandom = true; awayValid = false;
}
void Processor::backHome()
{
    if (! homeValid) return;
    if (fromRandom) { away = snapshot(); awayValid = true; }   // keep the random beat for BACK TO THE BREAK
    const auto h = home; randomEditing = true;
    edit ([&h] (Pattern& p) { p = h.p; });
    randomEditing = false; fromRandom = false;
    for (int b = 0; b < kBays; ++b) if (auto* p = apvts.getParameter (juce::String (bayPrefix (b)) + "nudge")) p->setValueNotifyingHost (p->convertTo0to1 ((float) h.nudge[b]));
}
void Processor::randomStart()
{
    beginRandom(); randomEditing = true;
    const auto v = gbk::randomStart (cur, bayParams(), locks(), (uint32_t) juce::Random::getSystemRandom().nextInt());
    edit ([] (Pattern&) {});   // an undo point (the pattern itself stays)
    for (int b = 0; b < kBays; ++b) if (v[(size_t) b] >= 0) if (auto* p = apvts.getParameter (juce::String (bayPrefix (b)) + "nudge")) p->setValueNotifyingHost (p->convertTo0to1 ((float) v[(size_t) b]));
    randomEditing = false;
}
void Processor::randomCombo()
{
    beginRandom(); randomEditing = true;
    struct Done { bool& f; ~Done() { f = false; } } done { randomEditing };
    const auto B = bayParams(); const auto R = rules(); auto L = locks(); const auto sd = (uint32_t) juce::Random::getSystemRandom().nextInt();
    for (auto& x : L.drum) x = true;   // RANDOM COMBO is the breaks; the drums have their own RANDOM DRUMS
    std::vector<mk::BreakPtr> all;   // NEW BREAKS draws from the real breaks (the CLASSIC BREAKS packs): a handful of candidates, decoded here
    std::vector<const BreakEntry*> pool; for (auto& c : catalog) if (c.kind == classicBreak) pool.push_back (&c);
    if (pool.empty()) for (auto& c : catalog) pool.push_back (&c);
    if (R.newBreaks && ! pool.empty())
        for (int k = 0; k < 6; ++k)
        {
            const auto& c = *pool[(size_t) juce::Random::getSystemRandom().nextInt ((int) pool.size())]; juce::String err;
            if (c.libIndex >= 0) all.push_back (lib->breaks.breaks()[(size_t) c.libIndex]); else if (auto br = loadFile (c.file, err)) all.push_back (br);
        }
    edit ([&] (Pattern& p) { p = gbk::randomCombo (p, B, R, L, all, sd); });
}
// RANDOM DRUMS: a new drum & bass beat on the six lanes (a locked lane keeps its hits); the breaks are left as they are
void Processor::randomDrums()
{
    beginRandom(); randomEditing = true;
    struct Done { bool& f; ~Done() { f = false; } } done { randomEditing };
    const auto B = bayParams(); auto R = rules(); auto L = locks(); R.newBreaks = false;
    for (auto& x : L.bay) x = true;
    const auto sd = (uint32_t) juce::Random::getSystemRandom().nextInt();
    edit ([&] (Pattern& p) { const Pattern n = gbk::randomCombo (p, B, R, L, {}, sd); p.drum = n.drum; });
}
void Processor::pushStutterPad()
{
    const int type = (int) std::lround (raw ("stut_type")), rate = (int) std::lround (raw ("stut_rate"));
    juce::Array<juce::var> pads; pads.add (juce::Array<juce::var> { type, rate, 0.5f });
    auto* o = new juce::DynamicObject(); o->setProperty ("pads", pads);
    engine.slot (sStutter).fx().setData (juce::var (o));
    lastStut[0] = type; lastStut[1] = rate;
}

void Processor::prepareToPlay (double rate, int block)
{
    // under the callback lock: a host may call this from any thread while the message thread's timer re-aligns latency
    { const juce::ScopedLock sl (getCallbackLock());
      engine.prepare (rate, block); engine.setLookaheadMs (duckParams().lookMs);
      for (int s = 0; s < kNumSlots; ++s) for (size_t i = 0; i < sp[(size_t) s].v.size(); ++i) if (auto* a = sp[(size_t) s].v[i]) engine.slot (s).setValue ((int) i, a->load()); for (int k = 0; k < 3; ++k) for (size_t i = 0; i < sp[(size_t) (sStutter + k)].v.size(); ++i) if (auto* a = sp[(size_t) (sStutter + k)].v[i]) engine.drumSlot (k).setValue ((int) i, a->load()); for (int b = 0; b < kBays; ++b) for (size_t i = 0; i < sp[(size_t) filterSlot (b)].v.size(); ++i) if (auto* a = sp[(size_t) filterSlot (b)].v[i]) engine.drumFilter (b).setValue ((int) i, a->load());
      engine.refreshLatency(); engine.setPattern (live.get()); prepared.store (true); }
    setLatencySamples (engine.getLatencySamples());
}
bool Processor::isBusesLayoutSupported (const BusesLayout& l) const
{
    if (l.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    for (int i = 1; i < l.outputBuses.size(); ++i) if (! l.outputBuses[i].isDisabled() && l.outputBuses[i] != juce::AudioChannelSet::stereo()) return false;
    return l.inputBuses.isEmpty() || l.getMainInputChannelSet().isDisabled();
}

void Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    const int n = buffer.getNumSamples();
    if (! prepared.load()) { buffer.clear(); midi.clear(); return; }
    auto main = getBusBuffer (buffer, false, 0);
    if (main.getNumChannels() < 2 || n <= 0) { buffer.clear(); return; }
    float* aux[kAux * 2] {};
    for (int k = 0; k < kAux; ++k)
    {
        if (getBusCount (false) <= k + 1) continue;
        auto ab = getBusBuffer (buffer, false, k + 1);
        if (ab.getNumChannels() >= 2) { aux[k * 2] = ab.getWritePointer (0); aux[k * 2 + 1] = ab.getWritePointer (1); } else ab.clear();
    }
    std::array<BayP, kBays> B; std::array<DrumP, kDrums> D; Globals G; gather (B, D, G);
    if (G.play && ! wasPlay) engine.restart();
    wasPlay = G.play;
    feedSlots (engine, G.stutter || engine.uiStutter.load());
    double bpm = 0, ppq = 0; bool playing = false;
    if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition()) { playing = pos->getIsPlaying(); if (auto b = pos->getBpm()) bpm = *b; if (auto q = pos->getPpqPosition()) ppq = *q; }
    if (bpm < 20.0) playing = false;
    hostPlayingNow.store (playing); hostBpmNow.store (bpm);
    engine.process (main.getWritePointer (0), main.getWritePointer (1), aux, n, B, D, G, bpm, ppq, playing, midi);
    for (int c = 2; c < main.getNumChannels(); ++c) main.clear (c, 0, n);
    midi.clear();
}

void Processor::gather (std::array<BayP, kBays>& B, std::array<DrumP, kDrums>& D, Globals& G) const
{
    B = bayParams();
    for (int d = 0; d < kDrums; ++d) { D[(size_t) d].level = drumRaw[d].level->load(); D[(size_t) d].pan = drumRaw[d].pan->load(); D[(size_t) d].tune = drumRaw[d].tune->load(); D[(size_t) d].mute = drumRaw[d].mute->load() > 0.5f; }
    G.play = gPlay->load() > 0.5f; G.bpm = gBpm->load(); G.swing = gSwing->load(); G.breaks = gBreaks->load() * 1.25f; G.drums = gDrums->load() * 1.25f; G.volume = gVolume->load();
    G.duck = duckParams(); G.stutter = gStutter->load() > 0.5f; G.drumsOwnOut = gDrumsOut->load() > 0.5f; G.choke = raw ("choke") > 0.5f; G.stutterPad = 1;
}
double Processor::tempoMul() const { const int t = (int) std::lround (raw ("tempo_mul")); return t == 0 ? 0.5 : t == 2 ? 2.0 : 1.0; }
double Processor::playBpm() const { const double h = hostBpmNow.load(); return (hostPlayingNow.load() && h >= 20.0 ? h : (double) gBpm->load()) * tempoMul(); }

void Processor::feedSlots (Engine& e, bool stutNow) const
{
    const bool link = gStutLink->load() > 0.5f;
    e.tempoMul.store (tempoMul());
    for (int s = 0; s < kNumSlots; ++s)
    {
        auto& sl = e.slot (s); const auto& q = sp[(size_t) s];
        for (size_t i = 0; i < q.v.size(); ++i) if (q.v[i] != nullptr) sl.setValue ((int) i, q.v[i]->load());
        bool on = q.on == nullptr || q.on->load() > 0.5f;
        if ((s == sStutFilter || s == sStutGate) && link && ! stutNow) on = false;
        const int tgt = (int) std::lround (raw ("stut_target"));   // 0 the breaks, 1 the drums, 2 both
        if (s == sStutter || s == sStutFilter || s == sStutGate)
        {   // the drums' own copy of the stutter chain, set the same
            auto& dl = e.drumSlot (s - sStutter);
            for (size_t i = 0; i < q.v.size(); ++i) if (q.v[i] != nullptr) dl.setValue ((int) i, q.v[i]->load());
            dl.setEnabled (on && tgt != 0); dl.setMix (q.mix != nullptr ? q.mix->load() : 1.0f);
            if (s != sStutter && tgt == 1) on = false;
        }
        if (s < kBays * 2 && (s & 1) == 1)
        {   // FILTER +DRUMS: the drums' copy of this bay's filter, set the same
            auto& df = e.drumFilter (s / 2);
            for (size_t i = 0; i < q.v.size(); ++i) if (q.v[i] != nullptr) df.setValue ((int) i, q.v[i]->load());
            df.setEnabled (on && raw (juce::String (bayPrefix (s / 2)) + "filter_drums") > 0.5f); df.setMix (q.mix != nullptr ? q.mix->load() : 1.0f);
        }
        sl.setEnabled (on); sl.setMix (q.mix != nullptr ? q.mix->load() : 1.0f);
    }
    e.stutTarget.store ((int) std::lround (raw ("stut_target")));
}

juce::File Processor::renderWav (int only)
{
    PatternPtr keep; { const juce::ScopedLock sl (editLock); keep = live; }
    if (keep == nullptr) return {};
    const double rate = getSampleRate() > 8000.0 ? getSampleRate() : 48000.0;
    const double h = hostBpmNow.load(), bpm = hostPlayingNow.load() && h >= 20.0 ? h : (double) gBpm->load(), mul = tempoMul();
    auto e = std::make_unique<Engine>(); constexpr int kBlock = 512;
    e->prepare (rate, kBlock); e->setLookaheadMs (duckParams().lookMs);
    for (int s = 0; s < kNumSlots; ++s) for (size_t i = 0; i < sp[(size_t) s].v.size(); ++i) if (auto* a = sp[(size_t) s].v[i]) e->slot (s).setValue ((int) i, a->load());
    for (int k = 0; k < 3; ++k) for (size_t i = 0; i < sp[(size_t) (sStutter + k)].v.size(); ++i) if (auto* a = sp[(size_t) (sStutter + k)].v[i]) e->drumSlot (k).setValue ((int) i, a->load());
    for (int b = 0; b < kBays; ++b) for (size_t i = 0; i < sp[(size_t) filterSlot (b)].v.size(); ++i) if (auto* a = sp[(size_t) filterSlot (b)].v[i]) e->drumFilter (b).setValue ((int) i, a->load());
    feedSlots (*e, false); e->refreshLatency(); e->setPattern (keep.get());
    std::array<BayP, kBays> B; std::array<DrumP, kDrums> D; Globals G; gather (B, D, G);
    G.stutter = false; G.drumsOwnOut = false;
    if (only >= 0 && only < kBays) { for (int b = 0; b < kBays; ++b) if (b != only) B[(size_t) b].mute = true; B[(size_t) only].mute = false; G.drums = 0.0f; }
    // one pass of the 32 steps (8 of its beats = 8 / TEMPO of the host's), rendered twice: the second pass carries the first one's tails
    const double spb0 = rate * 60.0 / bpm; const int loop = (int) std::lround ((double) kSteps / 4.0 / mul * spb0), L = e->getLatencySamples();
    const int total = 2 * loop + L;
    juce::AudioBuffer<float> all (2, total + kBlock); all.clear(); juce::MidiBuffer none;
    for (int at = 0; at < total; at += kBlock)
    {
        const int n = juce::jmin (kBlock, total - at);
        e->process (all.getWritePointer (0, at), all.getWritePointer (1, at), nullptr, n, B, D, G, bpm, (double) at / spb0, true, none);
    }
    e->setPattern (nullptr);
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Ghost Break Drags"); dir.createDirectory();
    const juce::String bpmTxt = juce::String (bpm * mul, std::abs (bpm * mul - std::round (bpm * mul)) < 0.01 ? 0 : 1);
    const juce::String what = only >= 0 ? juce::String ("Bay ") + juce::String::charToString ((juce::juce_wchar) ('A' + only)) + " " + (keep->brk[(size_t) only] != nullptr ? keep->brk[(size_t) only]->name : juce::String()) : juce::String ("Beat");
    auto f = dir.getNonexistentChildFile ("Ghost Break " + juce::File::createLegalFileName (what.trim()) + " " + bpmTxt + "bpm", ".wav", false);
    juce::WavAudioFormat wav;
    if (auto os = f.createOutputStream())
        if (auto w = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (os.get(), rate, 2, 24, {}, 0)))
        {
            os.release(); w->writeFromAudioSampleBuffer (all, L + loop, loop);
            return f;
        }
    return {};
}

void Processor::timerCallback()
{
    // a stepped control of a hosted unit moved: its latency may have (limiter lookahead, gate lookahead, engine menus); re-align
    float h = 0; int k = 1;
    for (int s = 0; s < kNumSlots; ++s) { const auto& specs = engine.slot (s).params(); for (size_t i = 0; i < specs.size(); ++i, ++k) if (! specs[i].isContinuous() && sp[(size_t) s].v[i] != nullptr) h += sp[(size_t) s].v[i]->load() * (float) (k % 97 + 1); }
    h += duckG[4]->load() * 7919.0f;   // LOOKAHEAD moves the latency too
    if (h != lastStepped && prepared.load())
    {
        lastStepped = h;
        { const juce::ScopedLock sl (getCallbackLock());
          if (! prepared.load()) return;
          engine.setLookaheadMs (duckParams().lookMs);
          for (int s = 0; s < kNumSlots; ++s) for (size_t i = 0; i < sp[(size_t) s].v.size(); ++i) if (auto* a = sp[(size_t) s].v[i]) engine.slot (s).setValue ((int) i, a->load()); for (int k = 0; k < 3; ++k) for (size_t i = 0; i < sp[(size_t) (sStutter + k)].v.size(); ++i) if (auto* a = sp[(size_t) (sStutter + k)].v[i]) engine.drumSlot (k).setValue ((int) i, a->load()); for (int b = 0; b < kBays; ++b) for (size_t i = 0; i < sp[(size_t) filterSlot (b)].v.size(); ++i) if (auto* a = sp[(size_t) filterSlot (b)].v[i]) engine.drumFilter (b).setValue ((int) i, a->load());
          engine.refreshLatency(); }
        if (engine.getLatencySamples() != getLatencySamples()) setLatencySamples (engine.getLatencySamples());
    }
    if ((int) std::lround (raw ("stut_type")) != lastStut[0] || (int) std::lround (raw ("stut_rate")) != lastStut[1]) pushStutterPad();
    const uint64_t now = engine.blockCount.load();
    retired.erase (std::remove_if (retired.begin(), retired.end(), [now] (const Retired& r) { return now >= r.stamp + 2; }), retired.end());
}

// ---------------------------------------------------------------- state
void Processor::writePattern (juce::ValueTree& t) const
{
    for (int b = 0; b < kBays; ++b)
    {
        juce::ValueTree c ("BAY"); const auto& br = cur.brk[(size_t) b];
        if (! br) c.setProperty ("empty", true, nullptr);
        c.setProperty ("name", br ? br->name : juce::String(), nullptr); c.setProperty ("file", br && br->user ? br->family : juce::String(), nullptr);
        if (br && ! br->user) { const auto& all = lib->breaks.breaks(); for (size_t i = 0; i < all.size(); ++i) if (all[i] == br) c.setProperty ("lib", (int) i, nullptr); }   // which one exactly (two can share a name)
        juce::String s; for (auto& e : cur.seq[(size_t) b]) s << (int) e.slice << "." << (int) e.kind << "." << (int) e.rat << "." << (int) e.vel << "." << (int) e.pitch << "." << (e.rev ? 1 : 0) << ",";
        c.setProperty ("seq", s, nullptr); c.setProperty ("lock", (juce::int64) cur.stepLock[(size_t) b], nullptr); t.appendChild (c, nullptr);
    }
    for (int d = 0; d < kDrums; ++d)
    {
        juce::ValueTree c ("DRUM"); c.setProperty ("sound", cur.snd[(size_t) d] ? cur.snd[(size_t) d]->name : juce::String(), nullptr); c.setProperty ("name", cur.laneName[(size_t) d], nullptr);
        juce::String s; for (auto v : cur.drum[(size_t) d]) s << (int) v << ","; c.setProperty ("steps", s, nullptr); t.appendChild (c, nullptr);
    }
}
void Processor::readPattern (const juce::ValueTree& t)
{
    defaultPattern(); int b = 0, d = 0; const auto all = lib->breaks.breaks();
    for (auto c : t)
    {
        if (c.hasType ("BAY") && b < kBays)
        {
            const juce::String name = c["name"].toString(), file = c["file"].toString(); mk::BreakPtr br;
            if (file.isNotEmpty() && juce::File::isAbsolutePath (file) && juce::File (file).existsAsFile()) { juce::String err; br = loadFile (juce::File (file), err); }
            const int li = c.hasProperty ("lib") ? (int) c["lib"] : -1;
            if (! br && li >= 0 && li < (int) all.size() && all[(size_t) li]->name == name) br = all[(size_t) li];
            if (! br) for (auto& x : all) if (x->name == name && ! x->user) { br = x; break; }
            if (br) cur.brk[(size_t) b] = br; else if ((bool) c.getProperty ("empty", false)) cur.brk[(size_t) b] = nullptr;   // a bay that was reset stays empty
            cur.straight (b);
            const int ns = cur.brk[(size_t) b] ? juce::jmax (1, cur.brk[(size_t) b]->slices) : 32;
            auto toks = juce::StringArray::fromTokens (c["seq"].toString(), ",", "");
            for (int s = 0; s < kSteps && s < toks.size(); ++s)
            {
                auto f = juce::StringArray::fromTokens (toks[s], ".", ""); if (f.size() < 6) continue;
                Step e; e.slice = (int16_t) juce::jlimit (0, ns - 1, f[0].getIntValue()); e.kind = (uint8_t) juce::jlimit (0, 2, f[1].getIntValue()); e.rat = (uint8_t) juce::jlimit (1, 4, f[2].getIntValue());
                e.vel = (uint8_t) juce::jlimit (1, 127, f[3].getIntValue()); e.pitch = (int8_t) juce::jlimit (-24, 24, f[4].getIntValue()); e.rev = f[5].getIntValue() != 0;
                cur.seq[(size_t) b][(size_t) s] = e;
            }
            cur.stepLock[(size_t) b] = (uint32_t) (juce::int64) c["lock"]; ++b;
        }
        else if (c.hasType ("DRUM") && d < kDrums)
        {
            const juce::String name = c["sound"].toString(); for (auto& s : soundsFor (d)) if (s->name == name) cur.snd[(size_t) d] = s; cur.laneName[(size_t) d] = c["name"].toString().substring (0, 16);
            auto toks = juce::StringArray::fromTokens (c["steps"].toString(), ",", ""); cur.drum[(size_t) d].fill (0);
            for (int s = 0; s < kSteps && s < toks.size(); ++s) cur.drum[(size_t) d][(size_t) s] = (uint8_t) juce::jlimit (0, 127, toks[s].getIntValue());
            ++d;
        }
    }
}
void Processor::getStateInformation (juce::MemoryBlock& dest)
{
    auto st = apvts.copyState(); st.removeChild (st.getChildWithName ("PATTERN"), nullptr);
    juce::ValueTree pt ("PATTERN"); writePattern (pt); st.appendChild (pt, nullptr);
    st.removeChild (st.getChildWithName ("SLOTDATA"), nullptr);
    juce::ValueTree sd ("SLOTDATA");
    for (int s = 0; s < kNumSlots; ++s) if (s != sStutter && engine.slot (s).fx().hasData()) { juce::ValueTree c ("SLOT"); c.setProperty ("id", slotPrefix (s), nullptr); c.setProperty ("data", juce::JSON::toString (engine.slot (s).fx().getData(), true), nullptr); sd.appendChild (c, nullptr); }
    st.appendChild (sd, nullptr);
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, dest);
}
void Processor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto st = juce::ValueTree::fromXml (*xml); const auto pt = st.getChildWithName ("PATTERN"); const auto sd = st.getChildWithName ("SLOTDATA");
            for (auto c : sd) for (int s = 0; s < kNumSlots; ++s) if (s != sStutter && c["id"].toString() == slotPrefix (s)) engine.slot (s).fx().setData (juce::JSON::parse (c["data"].toString()));
            apvts.replaceState (st);
            if (pt.isValid()) readPattern (pt); else defaultPattern();
            undoStack.clear(); publish(); pushStutterPad(); lastStepped = -1.0f; sendChangeMessage();
        }
}
juce::AudioProcessorEditor* Processor::createEditor() { return new Editor (*this); }
} // namespace gbk

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new gbk::Processor(); }
