// Ghosty Pro MODULATION router (round 3 item 3): see ProMod.h.
#include "ProMod.h"
#include "ProProcessor.h"

namespace gpro
{
namespace mod
{
const juce::StringArray& srcNames()
{
    static juce::StringArray s { "-", "ENV 1 (AMP)", "ENV 2", "ENV 3", "MOD ENV (FILTER)", "LFO 1", "LFO 2", "LFO 3", "LFO 4",
                                 "MACRO 1", "MACRO 2", "MACRO 3", "MACRO 4", "VELOCITY", "NOTE", "AFTERTOUCH", "MOD WHEEL", "PITCH BEND", "RANDOM" };
    return s;
}
const juce::StringArray& srcShortNames()
{
    static juce::StringArray s { "-", "ENV 1", "ENV 2", "ENV 3", "MOD ENV", "LFO 1", "LFO 2", "LFO 3", "LFO 4",
                                 "MACRO 1", "MACRO 2", "MACRO 3", "MACRO 4", "VEL", "NOTE", "AT", "WHEEL", "BEND", "RANDOM" };
    return s;
}
const juce::StringArray& curveNames() { static juce::StringArray s { "Linear", "Exp", "Log", "S", "Custom" }; return s; }
static double divQuarterNotes (int i)
{   // (the same divisions as LFO 1 / 2: lfoDivNames)
    static const double q[] { 16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 0.125, 4.0 / 3.0, 2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0, 1.5, 0.75 };
    return q[juce::jlimit (0, 13, i)];
}
} // namespace mod

using namespace mod;

ModRouter::ModRouter()
{
    for (auto& a : amtT) a.store (0.0f);
    for (auto& a : live) a.store (0.0f);
    for (auto& c : ccVal) c.store (-1);
    for (auto& s : srcLive) s.store (0.0f);
}
ModRouter::~ModRouter()
{
    delete pending.exchange (nullptr); delete trash.exchange (nullptr); delete liveC;
    delete pendingM.exchange (nullptr); delete trashM.exchange (nullptr); delete liveM;
}

void ModRouter::addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& l)
{
    using namespace juce;
    auto pctText = [] (const String& t) { return t.retainCharacters ("0123456789.-+").getFloatValue() / 100.0f; };
    auto pct = [pctText] (const String& id, const String& nm, float def)
    {
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, nm, NormalisableRange<float> (0.0f, 1.0f), def,
                   AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100)) + "%"; })
                                                  .withValueFromStringFunction (pctText));
    };
    // the old LFOs' DEPTH (1 = exactly the sound before)
    for (int k = 1; k <= 2; ++k) l.add (pct ("l" + String (k) + "_depth", "LFO " + String (k) + " Depth", 1.0f));
    for (int k = 3; k <= 4; ++k)
    {
        const String p = "l" + String (k) + "_", N = "LFO " + String (k) + " ";
        NormalisableRange<float> r (0.02f, 30.0f); r.setSkewForCentre (2.0f);
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { p + "rate", 1 }, N + "Rate", r, k == 3 ? 1.0f : 0.25f,
                   AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, v < 1 ? 2 : 1) + " Hz"; })));
        l.add (std::make_unique<AudioParameterBool> (ParameterID { p + "sync", 1 }, N + "Sync", false));
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { p + "div", 1 }, N + "Division", lfoDivNames(), 4));
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { p + "shape", 1 }, N + "Shape", lfoShapeNames(), k == 3 ? 1 : 0));
        l.add (std::make_unique<AudioParameterBool> (ParameterID { p + "trig", 1 }, N + "Retrigger", false));
        NormalisableRange<float> fr (0.0f, 4.0f); fr.setSkewForCentre (0.4f);
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { p + "fade", 1 }, N + "Fade In", fr, 0.0f,
                   AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 1 ? String (roundToInt (v * 1000)) + " ms" : String (v, 2) + " s"; })
                                                  .withValueFromStringFunction ([] (const String& t)
                                                  {
                                                      const float n = t.retainCharacters ("0123456789.-").getFloatValue();
                                                      return t.toLowerCase().contains ("ms") ? n / 1000.0f : (n > 4.0f ? n / 1000.0f : n);
                                                  })));
        l.add (pct (p + "depth", N + "Depth", 1.0f));
    }
}

bool ModRouter::isModParam (const juce::String& id)
{
    return id == "l1_depth" || id == "l2_depth" || id.startsWith ("l3_") || id.startsWith ("l4_");
}

void ModRouter::attach (MacroEngine& m, juce::AudioProcessorValueTreeState& s, std::vector<VoiceTarget> voiceTargets, LfoFn fn)
{
    macros = &m; apvts = &s; vt = std::move (voiceTargets); lfoFn = fn;
    auto P = [&] (const juce::String& id) { auto* raw = s.getRawParameterValue (id); jassert (raw != nullptr); return m.shadow (id, raw, s.getParameter (id)); };
    for (int k = 0; k < NUM_LFO; ++k)
    {
        const juce::String p = "l" + juce::String (k + 1) + "_";
        if (k >= 2) { lfoP[(size_t) k].rate = P (p + "rate"); lfoP[(size_t) k].sync = P (p + "sync"); lfoP[(size_t) k].div = P (p + "div");
                      lfoP[(size_t) k].shape = P (p + "shape"); lfoP[(size_t) k].trig = P (p + "trig"); lfoP[(size_t) k].fade = P (p + "fade"); }
        lfoP[(size_t) k].depth = P (p + "depth");
    }
    off.assign ((size_t) m.slotCount(), 0.0f); touched.assign ((size_t) m.slotCount(), 0);
}

int ModRouter::slotOf (const juce::String& id) const
{
    if (macros == nullptr || id.isEmpty()) return -1;
    if (id.startsWith ("mac") && id.length() == 4) return -1;          // the macros are sources
    for (int i = 0; i < macros->slotCount(); ++i) if (macros->slotId (i) == id) return i;
    return -1;
}
int ModRouter::voiceDstOf (const juce::String& id) const { for (auto& v : vt) if (id == v.param) return v.dst; return 0; }
bool ModRouter::isTarget (const juce::String& id) const { return slotOf (id) >= 0; }
bool ModRouter::isVoiceTarget (const juce::String& id) const { return voiceDstOf (id) > 0 && slotOf (id) >= 0; }

// ------------------------------------------------------------------ the list (message thread)
std::vector<ModConn> ModRouter::list() const { const juce::SpinLock::ScopedLockType sl (lock); return conns; }
int ModRouter::size() const { const juce::SpinLock::ScopedLockType sl (lock); return (int) conns.size(); }
std::vector<int> ModRouter::indicesFor (const juce::String& dst) const
{
    const juce::SpinLock::ScopedLockType sl (lock); std::vector<int> r;
    for (int i = 0; i < (int) conns.size(); ++i) if (conns[(size_t) i].dst == dst) r.push_back (i);
    return r;
}

int ModRouter::add (int src, const juce::String& dst, float amount)
{
    if (src <= none || src >= mod::numSrc || ! isTarget (dst)) return -1;
    int idx = -1;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        for (int i = 0; i < (int) conns.size(); ++i) if (conns[(size_t) i].src == src && conns[(size_t) i].dst == dst) return i;
        if ((int) conns.size() >= MAX_CONN) return -1;
        ModConn c; c.id = nextId++; c.src = src; c.dst = dst; c.amount = juce::jlimit (-1.0f, 1.0f, amount); c.bipolar = srcIsBipolar (src);
        c.slot = allocSlot(); if (c.slot < 0) return -1;
        conns.push_back (c); idx = (int) conns.size() - 1;
    }
    rebuild (false);
    return idx;
}

void ModRouter::setAmount (int index, float amount)
{
    amount = juce::jlimit (-1.0f, 1.0f, std::isfinite (amount) ? amount : 0.0f);
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        if (! juce::isPositiveAndBelow (index, (int) conns.size())) return;
        conns[(size_t) index].amount = amount;
        amtT[(size_t) conns[(size_t) index].slot].store (amount, std::memory_order_relaxed);
    }
    ver.fetch_add (1);
}

template <typename F> static bool editConn (juce::SpinLock& lock, std::vector<ModConn>& conns, int index, F&& f)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    if (! juce::isPositiveAndBelow (index, (int) conns.size())) return false;
    f (conns[(size_t) index]); return true;
}
void ModRouter::setBypass (int index, bool b)  { if (editConn (lock, conns, index, [b] (ModConn& c) { c.bypass = b; })) rebuild (false); }
void ModRouter::setBipolar (int index, bool b) { if (editConn (lock, conns, index, [b] (ModConn& c) { c.bipolar = b; })) rebuild (false); }
void ModRouter::setCurve (int index, int cv)   { if (editConn (lock, conns, index, [cv] (ModConn& c) { c.curve = juce::jlimit (0, (int) numCurves - 1, cv); })) rebuild (false); }
void ModRouter::setAux (int index, int aux)    { if (editConn (lock, conns, index, [aux] (ModConn& c) { c.aux = juce::jlimit (0, (int) mod::numSrc - 1, aux); })) rebuild (false); }
void ModRouter::setSource (int index, int src)
{
    if (src <= none || src >= mod::numSrc) return;
    if (editConn (lock, conns, index, [src] (ModConn& c) { if (srcIsBipolar (c.src) != srcIsBipolar (src)) c.bipolar = srcIsBipolar (src); c.src = src; })) rebuild (false);
}
void ModRouter::setCustom (int index, const std::array<float, 5>& px, const std::array<float, 5>& py)
{
    auto x = px, y = py;
    x[0] = 0.0f; x[4] = 1.0f;
    for (int k = 1; k < 4; ++k) x[(size_t) k] = juce::jlimit (x[(size_t) k - 1], 1.0f, x[(size_t) k]);
    for (auto& v : y) v = juce::jlimit (0.0f, 1.0f, std::isfinite (v) ? v : 0.0f);
    if (editConn (lock, conns, index, [&] (ModConn& c) { c.px = x; c.py = y; c.curve = cCustom; })) rebuild (false);
}
bool ModRouter::move (int index, const juce::String& newDst)
{
    if (! isTarget (newDst)) return false;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        if (! juce::isPositiveAndBelow (index, (int) conns.size())) return false;
        for (int i = 0; i < (int) conns.size(); ++i) if (i != index && conns[(size_t) i].src == conns[(size_t) index].src && conns[(size_t) i].dst == newDst) return false;
        conns[(size_t) index].dst = newDst;
    }
    rebuild (false);
    return true;
}
void ModRouter::remove (int index)
{
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        if (! juce::isPositiveAndBelow (index, (int) conns.size())) return;
        slotUsed[(size_t) conns[(size_t) index].slot] = false;
        conns.erase (conns.begin() + index);
    }
    rebuild (false);
}
void ModRouter::removeAllFor (const juce::String& dst)
{
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        for (auto& c : conns) if (c.dst == dst) slotUsed[(size_t) c.slot] = false;
        conns.erase (std::remove_if (conns.begin(), conns.end(), [&] (const ModConn& c) { return c.dst == dst; }), conns.end());
    }
    rebuild (false);
}
void ModRouter::clear() { { const juce::SpinLock::ScopedLockType sl (lock); conns.clear(); slotUsed.fill (false); } rebuild (true); }
void ModRouter::setList (const std::vector<ModConn>& l, bool snapNow)
{
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        conns.clear(); slotUsed.fill (false);
        for (auto c : l)
        {
            if ((int) conns.size() >= MAX_CONN) break;
            if (c.src <= none || c.src >= mod::numSrc || slotOf (c.dst) < 0) continue;     // (a target this build doesn't have: dropped)
            c.id = nextId++; c.slot = allocSlot(); conns.push_back (c);
        }
    }
    rebuild (snapNow);
}

float ModRouter::liveValue (int index) const noexcept
{
    int slot = -1;
    { const juce::SpinLock::ScopedLockType sl (lock); if (juce::isPositiveAndBelow (index, (int) conns.size())) slot = conns[(size_t) index].slot; }
    return juce::isPositiveAndBelow (slot, MAX_CONN) ? live[(size_t) slot].load (std::memory_order_relaxed) : 0.0f;
}

void ModRouter::rebuild (bool snapNow)
{
    auto* c = new Compiled();
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        c->c.reserve (conns.size());
        for (int i = 0; i < (int) conns.size(); ++i)
        {
            const auto& m = conns[(size_t) i];
            CConn k {};
            k.id = m.id; k.src = m.src; k.aux = m.aux; k.curve = m.curve; k.bipolar = m.bipolar; k.bypass = m.bypass; k.idx = m.slot; k.li = i;
            for (int p = 0; p < 5; ++p) { k.px[p] = m.px[(size_t) p]; k.py[p] = m.py[(size_t) p]; }
            k.slot = slotOf (m.dst); k.vdst = voiceDstOf (m.dst); k.vscale = 1.0f;
            if (k.slot < 0) continue;
            if (k.vdst > 0) { auto* prm = macros->slotParam (k.slot); k.vscale = prm->range.end - prm->range.start; c->anyVoice = true; }
            else c->anyGlobal = true;
            amtT[(size_t) m.slot].store (m.amount, std::memory_order_relaxed);
            c->c.push_back (k);
        }
    }
    collect();
    // (snap: a new sound applies its amounts at once instead of gliding in from the old sound's)
    snapReq.store (snapNow, std::memory_order_relaxed);
    delete pending.exchange (c, std::memory_order_acq_rel);
    collect();
    ver.fetch_add (1);
}

void ModRouter::collect()
{
    delete trash.exchange (nullptr, std::memory_order_acq_rel);
    delete trashM.exchange (nullptr, std::memory_order_acq_rel);
}

// ------------------------------------------------------------------ state
juce::ValueTree ModRouter::toTree() const
{
    const auto l = list();
    if (l.empty()) return {};
    juce::ValueTree t ("MODS");
    for (auto& c : l)
    {
        juce::ValueTree m ("MOD");
        m.setProperty ("src", c.src, nullptr); m.setProperty ("dst", c.dst, nullptr); m.setProperty ("amount", c.amount, nullptr);
        m.setProperty ("bipolar", c.bipolar, nullptr); m.setProperty ("curve", c.curve, nullptr);
        if (c.aux > none) m.setProperty ("aux", c.aux, nullptr);
        if (c.bypass) m.setProperty ("bypass", true, nullptr);
        if (c.curve == cCustom)
        {
            juce::StringArray pts; for (int k = 0; k < 5; ++k) pts.add (juce::String (c.px[(size_t) k], 4) + "," + juce::String (c.py[(size_t) k], 4));
            m.setProperty ("points", pts.joinIntoString (" "), nullptr);
        }
        t.appendChild (m, nullptr);
    }
    return t;
}

void ModRouter::fromTree (const juce::ValueTree& t, bool snapNow)
{
    std::vector<ModConn> l;
    if (t.isValid() && t.hasType ("MODS"))
        for (const auto& m : t)
        {
            if (! m.hasType ("MOD")) continue;
            ModConn c;
            c.src = (int) m.getProperty ("src", 0); c.dst = m.getProperty ("dst", "").toString();
            const float a = (float) m.getProperty ("amount", 0.0f); c.amount = std::isfinite (a) ? juce::jlimit (-1.0f, 1.0f, a) : 0.0f;
            c.bipolar = (bool) m.getProperty ("bipolar", srcIsBipolar (c.src)); c.curve = juce::jlimit (0, (int) numCurves - 1, (int) m.getProperty ("curve", 0));
            c.aux = juce::jlimit (0, (int) mod::numSrc - 1, (int) m.getProperty ("aux", 0)); c.bypass = (bool) m.getProperty ("bypass", false);
            juce::StringArray pts; pts.addTokens (m.getProperty ("points", "").toString(), " ", "");
            if (pts.size() == 5)
                for (int k = 0; k < 5; ++k)
                {
                    const auto xy = juce::StringArray::fromTokens (pts[k], ",", "");
                    if (xy.size() == 2) { c.px[(size_t) k] = juce::jlimit (0.0f, 1.0f, xy[0].getFloatValue()); c.py[(size_t) k] = juce::jlimit (0.0f, 1.0f, xy[1].getFloatValue()); }
                }
            c.px[0] = 0.0f; c.px[4] = 1.0f; for (int k = 1; k < 4; ++k) c.px[(size_t) k] = juce::jlimit (c.px[(size_t) k - 1], 1.0f, c.px[(size_t) k]);
            l.push_back (c);
        }
    setList (l, snapNow);
}

void ModRouter::setMorphTable (int ghosts, std::vector<float> amounts)
{
    MorphT* m = nullptr;
    if (ghosts > 0) { m = new MorphT(); m->ghosts = ghosts; m->n = (int) amounts.size() / ghosts; m->a = std::move (amounts); }
    collect();
    if (m == nullptr) m = new MorphT();          // (ghosts 0 = off)
    delete pendingM.exchange (m, std::memory_order_acq_rel);
    collect();
}

// ------------------------------------------------------------------ MIDI learn
void ModRouter::learn (const juce::String& id) { const juce::SpinLock::ScopedLockType sl (learnLock); learnId = id; lastCc.store (-1); }
void ModRouter::forget (const juce::String& id) { const juce::SpinLock::ScopedLockType sl (learnLock); learned.erase (id); if (learnId == id) learnId = {}; }
int ModRouter::ccFor (const juce::String& id) const { const juce::SpinLock::ScopedLockType sl (learnLock); auto it = learned.find (id); return it != learned.end() ? it->second : -1; }
juce::String ModRouter::learning() const { const juce::SpinLock::ScopedLockType sl (learnLock); return learnId; }
juce::ValueTree ModRouter::learnTree() const
{
    const juce::SpinLock::ScopedLockType sl (learnLock);
    if (learned.empty()) return {};
    juce::ValueTree t ("MIDILEARN");
    for (auto& [id, cc] : learned) { juce::ValueTree c ("CC"); c.setProperty ("param", id, nullptr); c.setProperty ("cc", cc, nullptr); t.appendChild (c, nullptr); }
    return t;
}
void ModRouter::loadLearnTree (const juce::ValueTree& t)
{
    const juce::SpinLock::ScopedLockType sl (learnLock);
    learned.clear();
    if (t.isValid() && t.hasType ("MIDILEARN"))
        for (const auto& c : t) { const int cc = (int) c.getProperty ("cc", -1); if (juce::isPositiveAndBelow (cc, 128)) learned[c.getProperty ("param", "").toString()] = cc; }
}
void ModRouter::scanMidi (const juce::MidiBuffer& m) noexcept
{
    for (const auto meta : m)
    {
        const auto* d = meta.data;
        if (meta.numBytes < 2) continue;
        const int st = d[0] & 0xf0;
        if (st == 0xb0 && meta.numBytes >= 3) { ccVal[(size_t) (d[1] & 127)].store (d[2] & 127, std::memory_order_relaxed); lastCc.store (d[1] & 127, std::memory_order_relaxed); }
        else if (st == 0xe0 && meta.numBytes >= 3) bendNow = juce::jlimit (-1.0f, 1.0f, ((((int) d[2] & 127) << 7 | ((int) d[1] & 127)) - 8192) / 8192.0f);
        else if (st == 0xd0) atNow = (d[1] & 127) / 127.0f;
    }
}
void ModRouter::applyLearned (juce::AudioProcessorValueTreeState& s)
{
    std::vector<std::pair<juce::String, int>> todo;
    {
        const juce::SpinLock::ScopedLockType sl (learnLock);
        if (learnId.isNotEmpty())
        {
            const int cc = lastCc.exchange (-1);
            if (cc >= 0) { for (auto it = learned.begin(); it != learned.end();) { if (it->second == cc) it = learned.erase (it); else ++it; } learned[learnId] = cc; learnId = {}; ver.fetch_add (1); }
        }
        for (auto& [id, cc] : learned) todo.push_back ({ id, cc });
    }
    for (auto& [id, cc] : todo)
    {
        const int v = ccVal[(size_t) cc].exchange (-1, std::memory_order_relaxed);
        if (v < 0) continue;
        if (auto* p = s.getParameter (id)) p->setValueNotifyingHost ((float) v / 127.0f);
    }
}

// ------------------------------------------------------------------ audio thread
void ModRouter::beginBlock (const BlockIn& in) noexcept
{
    if (pending.load (std::memory_order_acquire) != nullptr && trash.load (std::memory_order_acquire) == nullptr)
        if (auto* c = pending.exchange (nullptr, std::memory_order_acq_rel))
        {   // carry each connection's gliding amount over by its id (a structural edit never jumps the amounts that stay)
            std::array<float, MAX_CONN> nb {};
            std::array<int, MAX_CONN> nid {};
            const bool sn = snapReq.exchange (false, std::memory_order_relaxed);
            for (size_t i = 0; i < c->c.size(); ++i)
            {
                nid[i] = c->c[i].id; nb[i] = 0.0f;
                bool found = false;
                if (liveC != nullptr && ! sn) for (size_t j = 0; j < liveC->c.size(); ++j) if (amtId[j] == c->c[i].id) { nb[i] = amtB[j]; found = true; break; }
                if (sn || (! found && snap)) nb[i] = c->c[i].bypass ? 0.0f : amtT[(size_t) c->c[i].idx].load (std::memory_order_relaxed);
            }
            amtB = nb; amtA = nb; amtId = nid;
            trash.store (liveC, std::memory_order_release); liveC = c;
        }
    if (pendingM.load (std::memory_order_acquire) != nullptr && trashM.load (std::memory_order_acquire) == nullptr)
        if (auto* m = pendingM.exchange (nullptr, std::memory_order_acq_rel)) { trashM.store (liveM, std::memory_order_release); liveM = m; }
    sampleRate = in.sr; blockLen = juce::jmax (1, in.n);
    const float a = 1.0f - (float) std::exp (-(double) in.n / (0.010 * in.sr));
    const int N = liveC != nullptr ? (int) liveC->c.size() : 0;
    const bool useMorph = mw != nullptr && liveM != nullptr && liveM->ghosts == mwN && mwN > 0;
    moving = false;
    for (int i = 0; i < N; ++i)
    {
        const auto& k = liveC->c[(size_t) i];
        float t = amtT[(size_t) k.idx].load (std::memory_order_relaxed);
        if (useMorph && k.li < liveM->n) { double acc = 0; for (int g = 0; g < mwN; ++g) acc += (double) mw[g] * liveM->a[(size_t) (g * liveM->n + k.li)]; t = (float) acc; }
        if (k.bypass) t = 0.0f;
        amtA[(size_t) i] = amtB[(size_t) i];
        if (snap) amtB[(size_t) i] = amtA[(size_t) i] = t;
        else { amtB[(size_t) i] += (t - amtB[(size_t) i]) * a; if (std::abs (t - amtB[(size_t) i]) < 1.0e-6f) amtB[(size_t) i] = t; }
        if (amtA[(size_t) i] != amtB[(size_t) i]) moving = true;
    }
    snap = false;

    // LFO 3 / 4 clocks (the global phase: free running, or the host's beat when synced)
    for (int k = 0; k < 2; ++k)
    {
        const auto& P = lfoP[(size_t) k + 2];
        if (P.rate == nullptr) continue;
        shape[k] = (int) P.shape->load(); trig[k] = P.trig->load() > 0.5f; fade[k] = P.fade->load();
        if (P.sync->load() > 0.5f)
        {
            const double qn = divQuarterNotes ((int) P.div->load());
            inc[k] = in.bpm / 60.0 / in.sr / qn;
            if (in.playing) gph[k] = in.ppq / qn;
        }
        else inc[k] = P.rate->load() / in.sr;
        // (gph advances at the end of the block)
    }

    // the global sources: the newest voice's values (envelopes, voice LFOs, velocity, note, random), else the global ones
    if (in.lead != nullptr) gsrc = in.lead->last;
    else
    {
        gsrc.fill (0.0f);
        for (int k = 0; k < 2; ++k) if (lfoFn != nullptr && lfoP[(size_t) k + 2].rate != nullptr) gsrc[(size_t) (lfo3 + k)] = lfoFn (shape[k], gph[k], (juce::uint32) (k + 2) * 977u) * lfoDepth (k + 2);
        gsrc[(size_t) bend] = bendNow; gsrc[(size_t) aftertouch] = atNow;
    }
    for (int k = 0; k < 4; ++k) gsrc[(size_t) (mac1 + k)] = in.macro[k];
    gsrc[(size_t) wheel] = in.wheel;
    if (in.lead == nullptr && gsrcWas != gsrc) moving = moving || (liveC != nullptr && liveC->anyGlobal);
    for (int s = 0; s < mod::numSrc; ++s) srcLive[s].store (gsrc[(size_t) s], std::memory_order_relaxed);

    // global targets: offsets (normalised) into the shadows
    if (macros != nullptr)
    {
        const int S = macros->slotCount();
        if ((int) off.size() != S) { off.assign ((size_t) S, 0.0f); touched.assign ((size_t) S, 0); }    // (first block only: sized before playback in prepare)
        std::fill (off.begin(), off.end(), 0.0f);
        for (int i = 0; i < N; ++i)
        {
            const auto& k = liveC->c[(size_t) i];
            const float x = shapeOf (k.src, gsrc[(size_t) k.src], k.curve, k.bipolar, k.px, k.py, k.aux, gsrc[(size_t) k.aux]);
            live[(size_t) k.idx].store (amtB[(size_t) i] * x, std::memory_order_relaxed);
            if (k.vdst > 0) continue;
            off[(size_t) k.slot] += amtB[(size_t) i] * x;
        }
        macros->applyMod (off.data(), touched.data(), S);
    }
    if (in.lead != nullptr && gsrcWas != gsrc && liveC != nullptr && liveC->anyGlobal) moving = true;
    gsrcWas = gsrc;
    for (int k = 0; k < 2; ++k) { gph[k] += inc[k] * in.n; if (gph[k] > 1.0e6) gph[k] -= 1.0e6; lfoPhaseLive[k].store ((float) (gph[k] - std::floor (gph[k])), std::memory_order_relaxed); }
}

void ModRouter::voiceStart (ModVoice& v, float rnd) const noexcept
{
    v.seed = (juce::uint32) (juce::int32) (rnd * 100000.0f);
    for (int k = 0; k < 2; ++k) v.ph[k] = trig[k] ? 0.0 : gph[k];
    v.t = 0;
}

void ModRouter::voiceStep (ModVoice& v, const ModVoiceIn& in, int pos, int n, float* d) const noexcept
{
    auto& s = v.last;
    s[(size_t) env1] = in.env1; s[(size_t) env2] = in.env2; s[(size_t) env3] = in.env3; s[(size_t) menv] = in.menv;
    s[(size_t) lfo1] = in.lfo1; s[(size_t) lfo2] = in.lfo2;
    for (int k = 0; k < 2; ++k)
    {
        if (lfoFn == nullptr || lfoP[(size_t) k + 2].rate == nullptr) continue;
        const float fd = fade[k] > 0.001f ? juce::jmin (1.0f, (float) v.t / fade[k]) : 1.0f;
        s[(size_t) (lfo3 + k)] = lfoFn (shape[k], v.ph[k], trig[k] ? v.seed + (juce::uint32) (k + 2) * 977u : (juce::uint32) (k + 2) * 977u) * fd * lfoDepth (k + 2);
        v.ph[k] += inc[k] * n; if (v.ph[k] > 1.0e6) v.ph[k] -= 1.0e6;
    }
    v.t += n / sampleRate;
    for (int k = 0; k < 4; ++k) s[(size_t) (mac1 + k)] = gsrc[(size_t) (mac1 + k)];
    s[(size_t) vel] = in.vel; s[(size_t) note] = in.note; s[(size_t) aftertouch] = in.at; s[(size_t) wheel] = in.wheel; s[(size_t) bend] = in.bend; s[(size_t) random] = in.rnd;
    if (liveC == nullptr || ! liveC->anyVoice || d == nullptr) return;
    const float t = juce::jlimit (0.0f, 1.0f, ((float) pos + 0.5f * (float) n) / (float) blockLen);
    const int N = (int) liveC->c.size();
    for (int i = 0; i < N; ++i)
    {
        const auto& k = liveC->c[(size_t) i];
        if (k.vdst <= 0) continue;
        const float am = amtA[(size_t) i] + (amtB[(size_t) i] - amtA[(size_t) i]) * t;
        if (am == 0.0f) continue;
        d[k.vdst] += am * shapeOf (k.src, s[(size_t) k.src], k.curve, k.bipolar, k.px, k.py, k.aux, s[(size_t) k.aux]) * k.vscale;
    }
}
} // namespace gpro

// ================================================================== ProProcessor glue (MOD round 3; kept here so ProProcessor.cpp only has small hooks)
void ProProcessor::modAttach()
{
    using namespace gpro;
    modr.attach (macros, apvts,
                 { { "o1_pos", dPos1 }, { "o2_pos", dPos2 }, { "o1_lvl", dLvl1 }, { "o2_lvl", dLvl2 }, { "o1_det", dDet1 }, { "o2_det", dDet2 },
                   { "xamt", dXmod }, { "sub_lvl", dSub }, { "nz_lvl", dNoise }, { "cutoff", dCutoff }, { "res", dRes }, { "f_drive", dFDrive },
                   { "br_growl", dGrowl }, { "o1_w1a", dW1a }, { "o1_w2a", dW1b }, { "o2_w1a", dW2a }, { "o2_w2a", dW2b },
                   { "o1_eh", dE1h }, { "o1_et", dE1t }, { "o1_em", dE1m }, { "o2_eh", dE2h }, { "o2_et", dE2t }, { "o2_em", dE2m } },   // (+ ENGINES round 3)
                 &ProProcessor::lfoValue);
}

void ProProcessor::modBlock (int n, double bpm, double ppq, bool playing, const juce::MidiBuffer& midiIn)
{
    modr.scanMidi (midiIn);
    gpro::ModRouter::BlockIn b;
    b.n = n; b.sr = sampleRateNow.load(); b.bpm = bpm > 1.0 ? bpm : 120.0; b.ppq = ppq; b.playing = playing;
    const ProVoice* lead = nullptr;
    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* v = static_cast<const ProVoice*> (synth.getVoice (i)); v->isVoiceActive() && (lead == nullptr || v->startedAt > lead->startedAt)) lead = v;
    b.lead = lead != nullptr ? &lead->modVoice() : nullptr;
    b.wheel = ctl.wheel;
    for (int k = 0; k < 4; ++k) b.macro[k] = macro[k]->load();
    float w[gspirit::kMaxGhosts] {};
    int wn = 0;
    if (! isLayer && morphTrim >= 0.0f && morph.enabled.load (std::memory_order_relaxed))
    {
        wn = juce::jmin (gspirit::kMaxGhosts, morph.board.liveN.load (std::memory_order_relaxed));
        for (int k = 0; k < wn; ++k) w[k] = morph.board.liveW[(size_t) k].load (std::memory_order_relaxed);
    }
    modr.setMorphWeights (wn > 0 ? w : nullptr, wn);
    modr.beginBlock (b);
    modr.setMorphWeights (nullptr, 0);
}

void ProProcessor::modMorphRebuild()
{
    const auto l = modr.list();
    const int G = (int) morphBoard.ghosts.size(), N = (int) l.size();
    if (isLayer || G == 0 || N == 0) { modr.setMorphTable (0, {}); return; }
    std::vector<float> a ((size_t) (G * N), 0.0f);
    for (int g = 0; g < G; ++g)
    {
        const auto mods = morphSounds[morphBoard.ghosts[(size_t) g].id].getChildWithName ("MODS");
        for (int i = 0; i < N; ++i)
            for (const auto& m : mods)
                if ((int) m.getProperty ("src", 0) == l[(size_t) i].src && m.getProperty ("dst", "").toString() == l[(size_t) i].dst)
                { a[(size_t) (g * N + i)] = (float) m.getProperty ("amount", 0.0f) * ((bool) m.getProperty ("bypass", false) ? 0.0f : 1.0f); break; }
    }
    modr.setMorphTable (G, std::move (a));
}
