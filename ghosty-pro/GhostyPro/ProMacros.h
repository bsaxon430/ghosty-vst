#pragma once
// Ghosty Pro MACROS: four named knobs per preset, each riding several parameters with its own amount and curve.
//
// The four knobs already exist (parameters mac1..mac4, "Macro 1..4", host-automatable, mod-matrix sources). What was
// missing: a per-preset NAME for each knob and a map "macro -> many parameters" that doesn't eat the eight matrix slots
// and can reach anything the matrix can't (reverb / echo / chorus sends, filter env amount, CHARACTER, layer levels,
// matrix amounts = LFO depth, LFO rates, envelope times ...).
//
// How it works (no change to the parameter list, no change to any saved state that has no macros):
//   * every float parameter the processor reads through a pointer gets a SHADOW atomic. ProProcessor's constructor
//     asks  macros.shadow (id, raw, param)  and keeps the returned pointer; for non-float parameters it is the raw one.
//   * at the top of processBlock  macros.update (n, sr)  copies raw -> shadow for every float, adding the smoothed
//     macro offsets (one-pole, 50 ms, so a knob move or an automation jump never zips). With no macro map, or every
//     macro target at 0, shadow == raw: the sound is the same as before.
//   * a target is  amount * m^curve  (unipolar macro, m = knob 0..1) or  amount * sign(x)|x|^curve, x = 2m-1
//     (bipolar macro: the preset's own value sits at knob 0.5). Clamped to the parameter's range.
//   * a STACK layer gets the parent's macro values (externals) whenever the parent preset has a macro map, so a
//     layer that is itself a macro preset answers to the same four knobs. Parents without a map leave the layers alone
//     (old TRAP stacks unchanged).
//   * saved per preset: a MACROS child in the state tree (names, bipolar flags, targets). Factory presets write it in
//     the spec:   mc1=BRIGHTNESS~   (name; trailing ~ = bipolar)      mc1:cutoff=.25        (target, amount)
//                 mc4:verb=.35^1.5  (amount ^ curve)                  mc=lead               (a built-in template, see macroTemplates)
//     Repeating a target for the same parameter replaces it, so a preset can start from a template and adjust.
//
// Nothing here allocates or frees on the audio thread: the compiled map is handed over through a pointer swap (pending -> live),
// the previous one goes to 'trash' and is freed on the message thread (setMap / collect). The audio thread only swaps while
// 'trash' is empty, so it never has to drop or free anything.
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>
#include <cmath>

namespace gpro
{
constexpr int NUM_MACROS = 4;

struct MacroTarget { juce::String param; float amount = 0.0f; float curve = 1.0f; };
struct MacroDef { juce::String name; bool bipolar = false; std::vector<MacroTarget> targets; };

struct MacroMap
{
    std::array<MacroDef, NUM_MACROS> macro;

    bool empty() const
    {
        for (auto& m : macro) if (m.name.isNotEmpty() || ! m.targets.empty()) return false;
        return true;
    }

    void setTarget (int k, const juce::String& param, float amount, float curve)
    {
        if (! juce::isPositiveAndBelow (k, NUM_MACROS)) return;
        for (auto& t : macro[(size_t) k].targets) if (t.param == param) { t.amount = amount; t.curve = curve; return; }
        macro[(size_t) k].targets.push_back ({ param, amount, curve });
    }

    // One spec token (key=value) that belongs to the macros: "mc1=NAME[~]", "mc1:param=amount[^curve]", "mc=template".
    // Returns false when the key isn't a macro key (the caller then treats it as an ordinary parameter).
    bool parseToken (const juce::String& key, const juce::String& val);

    juce::ValueTree toTree() const
    {
        juce::ValueTree t ("MACROS");
        for (int k = 0; k < NUM_MACROS; ++k)
        {
            const auto& m = macro[(size_t) k];
            if (m.name.isEmpty() && m.targets.empty()) continue;
            juce::ValueTree mt ("MACRO");
            mt.setProperty ("index", k, nullptr); mt.setProperty ("name", m.name, nullptr); mt.setProperty ("bipolar", m.bipolar, nullptr);
            for (auto& g : m.targets)
            {
                juce::ValueTree gt ("TARGET");
                gt.setProperty ("param", g.param, nullptr); gt.setProperty ("amount", g.amount, nullptr); gt.setProperty ("curve", g.curve, nullptr);
                mt.appendChild (gt, nullptr);
            }
            t.appendChild (mt, nullptr);
        }
        return t;
    }

    static MacroMap fromTree (const juce::ValueTree& t)
    {
        MacroMap m;
        if (! t.isValid() || ! t.hasType ("MACROS")) return m;
        for (const auto& mt : t)
        {
            const int k = (int) mt.getProperty ("index", -1);
            if (! juce::isPositiveAndBelow (k, NUM_MACROS)) continue;
            auto& d = m.macro[(size_t) k];
            d.name = mt.getProperty ("name", "").toString().substring (0, 16); d.bipolar = (bool) mt.getProperty ("bipolar", false);
            for (const auto& gt : mt)
            {
                const float amt = (float) gt.getProperty ("amount", 0.0f), cv = (float) gt.getProperty ("curve", 1.0f);
                if (std::isfinite (amt) && std::isfinite (cv)) m.setTarget (k, gt.getProperty ("param", "").toString(), juce::jlimit (-100.0f, 100.0f, amt), juce::jlimit (0.2f, 5.0f, cv));
            }
        }
        return m;
    }
};

// Built-in macro templates for the factory presets ("mc=lead" etc.). Every lead preset in the LEADS pack starts from one of
// these and may adjust single targets after it. Matrix slot conventions used by the templates: slot 1 = LFO 2 > cutoff
// (the MOVEMENT filter sweep, amount 0 until the knob moves), slot 2 = LFO 2 > osc 1 position (wavetable drift).
struct MacroTemplate { const char* name; const char* spec; };
inline const std::vector<MacroTemplate>& macroTemplates()
{
    static const std::vector<MacroTemplate> t {
        // a mono / poly lead: BRIGHTNESS = filter + octave layer, WIDTH = unison spread/detune + chorus, MOVEMENT = LFO depth/rate + echo,
        // SIZE = everything grows: brighter, wider, more verb/echo, more sub, longer release
        { "lead",  "mc1=BRIGHTNESS~ mc1:cutoff=.22 mc1:fenv=.08 mc1:o2_lvl=.15 "
                   "mc2=WIDTH~ mc2:o1_spr=.9 mc2:o2_spr=.9 mc2:o1_det=.14 mc2:o2_det=.14 mc2:chorus=.3 "
                   "mc3=MOVEMENT mc3:m1_amt=.3 mc3:m2_amt=.25 mc3:l2_rate=.8^1.6 mc3:echo=.22 "
                   "mc4=SIZE mc4:cutoff=.22^1.4 mc4:o1_det=.14 mc4:o1_spr=.3 mc4:o2_det=.1 mc4:verb=.55 mc4:echo=.22 mc4:sub_lvl=.08 mc4:release=.5 mc4:chorus=.12" },
        // a pluck: the filter envelope is the sound, so BRIGHTNESS and SIZE lean on fenv / decay instead of a long release
        { "pluck", "mc1=BRIGHTNESS~ mc1:cutoff=.2 mc1:fenv=.14 mc1:o2_lvl=.15 "
                   "mc2=WIDTH~ mc2:o1_spr=.9 mc2:o2_spr=.9 mc2:o1_det=.14 mc2:o2_det=.1 mc2:chorus=.3 "
                   "mc3=MOVEMENT mc3:m1_amt=.28 mc3:m2_amt=.2 mc3:l2_rate=.8^1.6 mc3:echo=.28 "
                   "mc4=SIZE mc4:cutoff=.2^1.4 mc4:fenv=.1 mc4:f_d=.18 mc4:o1_det=.12 mc4:o1_spr=.3 mc4:verb=.6 mc4:echo=.26 mc4:sub_lvl=.08 mc4:release=.3 mc4:chorus=.12" },
        // a STACK: SIZE also brings the two layers up (their levels start low in the preset) so the stack blooms
        { "stack", "mc1=BRIGHTNESS~ mc1:cutoff=.22 mc1:fenv=.06 mc1:o2_lvl=.12 "
                   "mc2=WIDTH~ mc2:o1_spr=.9 mc2:o2_spr=.9 mc2:o1_det=.14 mc2:o2_det=.1 mc2:chorus=.3 "
                   "mc3=MOVEMENT mc3:m1_amt=.28 mc3:m2_amt=.2 mc3:l2_rate=.8^1.6 mc3:echo=.2 "
                   "mc4=SIZE mc4:cutoff=.22^1.4 mc4:o1_det=.12 mc4:o1_spr=.3 mc4:verb=.55 mc4:echo=.2 mc4:ly2_lvl=.5 mc4:ly3_lvl=.5 mc4:sub_lvl=.08 mc4:release=.5 mc4:chorus=.1" },
        // aggressive: BRIGHTNESS also opens the filter drive, SIZE pours on CHARACTER and drive as it grows
        { "aggr",  "mc1=BRIGHTNESS~ mc1:cutoff=.24 mc1:res=.06 mc1:o2_lvl=.12 "
                   "mc2=WIDTH~ mc2:o1_spr=.9 mc2:o2_spr=.9 mc2:o1_det=.16 mc2:o2_det=.12 mc2:chorus=.3 "
                   "mc3=MOVEMENT mc3:m1_amt=.35 mc3:m2_amt=.3 mc3:l2_rate=.8^1.6 mc3:echo=.18 "
                   "mc4=SIZE mc4:cutoff=.22^1.4 mc4:o1_det=.14 mc4:o1_spr=.3 mc4:ch_amt=.3 mc4:verb=.55 mc4:echo=.2 mc4:sub_lvl=.08 mc4:release=.4" },
    };
    return t;
}

inline bool MacroMap::parseToken (const juce::String& key, const juce::String& val)
{
    if (! key.startsWith ("mc")) return false;
    if (key == "mc")                                     // mc=template
    {
        for (auto& t : macroTemplates())
            if (val == t.name)
            {
                juce::StringArray toks; toks.addTokens (juce::String (t.spec), " ", "");
                for (auto& tk : toks) { const int eq = tk.indexOfChar ('='); if (eq > 0) parseToken (tk.substring (0, eq), tk.substring (eq + 1)); }
                return true;
            }
        return true;                                     // unknown template: swallowed (the spec checker reports it via GP_SPEC_CHECK below)
    }
    const int colon = key.indexOfChar (':');
    const int k = key.substring (2, colon > 0 ? colon : key.length()).getIntValue() - 1;
    if (! juce::isPositiveAndBelow (k, NUM_MACROS)) return key.length() <= 4;   // "mc9=..." is ours but invalid; anything longer isn't ours
    auto& m = macro[(size_t) k];
    if (colon < 0)                                       // mc1=NAME or mc1=NAME~
    {
        m.bipolar = val.endsWithChar ('~');
        m.name = val.trimCharactersAtEnd ("~").replaceCharacter ('_', ' ').toUpperCase().substring (0, 16);
        return true;
    }
    const int caret = val.indexOfChar ('^');
    const float amount = (caret >= 0 ? val.substring (0, caret) : val).getFloatValue();
    const float curve = caret >= 0 ? juce::jlimit (0.2f, 5.0f, val.substring (caret + 1).getFloatValue()) : 1.0f;
    setTarget (k, key.substring (colon + 1), amount, curve);
    return true;
}

// ------------------------------------------------------------------ the engine (one per ProProcessor)
class MacroEngine
{
public:
    MacroEngine() { for (auto& e : external) e.store (0.0f); }
    ~MacroEngine() { delete pending.exchange (nullptr); delete trash.exchange (nullptr); delete live; }

    // Constructor time (before any processing). Float parameters get a shadow, everything else passes through.
    std::atomic<float>* shadow (const juce::String& id, std::atomic<float>* raw, juce::RangedAudioParameter* prm)
    {
        auto* fp = dynamic_cast<juce::AudioParameterFloat*> (prm);
        if (fp == nullptr || raw == nullptr) return raw;
        auto s = std::make_unique<Slot>();
        s->id = id; s->raw = raw; s->prm = fp; s->lo = fp->range.start; s->hi = fp->range.end; s->out.store (raw->load());
        // slew limit (units per second) on top of the smoothing: a matrix amount moves a modulation DEPTH that the voices only
        // read once per block, so it gets the gentlest limit (LFO > cutoff jumps would click); everything else may cross its range in 1/2 s
        const bool matrixAmount = id.length() == 6 && id.startsWithChar ('m') && id.endsWith ("_amt");
        s->slew = matrixAmount ? 0.8f : (s->hi - s->lo) * 2.0f;
        if (id.startsWith ("mac") && id.length() == 4) macroSlot[(size_t) juce::jlimit (0, NUM_MACROS - 1, id.getTrailingIntValue() - 1)] = (int) slots.size();
        slots.push_back (std::move (s));
        return &slots.back()->out;
    }

    // Message thread: the preset's map (empty = none). Also the names the editor shows.
    void setMap (const MacroMap& m)
    {
        { const juce::SpinLock::ScopedLockType sl (mapLock); map = m; }
        auto* c = new Compiled();
        c->hasMap = ! m.empty();
        for (int k = 0; k < NUM_MACROS; ++k)
        {
            c->bipolar[(size_t) k] = m.macro[(size_t) k].bipolar;
            for (auto& t : m.macro[(size_t) k].targets)
                for (int i = 0; i < (int) slots.size(); ++i)
                    if (slots[(size_t) i]->id == t.param) { c->t.push_back ({ i, k, t.amount, t.curve }); break; }
        }
        collect();                            // the map the audio thread let go of last time
        delete pending.exchange (c, std::memory_order_acq_rel);   // the audio thread picks it up at its next block; an unclaimed older one is freed here
        collect();                            // (if the audio thread swapped in between, what it let go of is freed now)
    }
    // Message thread, after a sound / state was loaded: every parameter no macro of the current map drives reads its stored value
    // right away (the audio thread does the same at its next block; until then readers of the parameter pointers - the editor,
    // a state round trip - would see the value from before the load). Targets of the map keep their effective value.
    void syncIdle()
    {
        const auto m = getMap();
        for (auto& s : slots)
        {
            bool targeted = false;
            for (auto& d : m.macro) for (auto& t : d.targets) targeted |= t.param == s->id;
            if (! targeted) s->out.store (s->raw->load(), std::memory_order_relaxed);
        }
    }
    // Message thread (also fine from a timer): frees the map the audio thread has let go of. Whatever sits in 'trash' is never read again.
    void collect() { delete trash.exchange (nullptr, std::memory_order_acq_rel); }
    MacroMap getMap() const { const juce::SpinLock::ScopedLockType sl (mapLock); return map; }
    juce::String name (int k) const { const juce::SpinLock::ScopedLockType sl (mapLock); return juce::isPositiveAndBelow (k, NUM_MACROS) ? map.macro[(size_t) k].name : juce::String(); }
    bool hasMap() const { const juce::SpinLock::ScopedLockType sl (mapLock); return ! map.empty(); }
    bool bipolar (int k) const { const juce::SpinLock::ScopedLockType sl (mapLock); return juce::isPositiveAndBelow (k, NUM_MACROS) && map.macro[(size_t) k].bipolar; }

    // Parent -> layer: the parent's four knob values ride the layer too (on = parent has a map).
    void setExternal (const float v[NUM_MACROS], bool on) { for (int k = 0; k < NUM_MACROS; ++k) external[(size_t) k].store (v[k], std::memory_order_relaxed); useExternal.store (on, std::memory_order_relaxed); }
    void macroValues (float out[NUM_MACROS]) const { for (int k = 0; k < NUM_MACROS; ++k) out[k] = value[(size_t) k]; }
    bool mapLive() const noexcept { return live != nullptr && live->hasMap; }     // audio thread

    // Audio thread, top of processBlock: shadows = raw + smoothed macro offsets.
    void update (int numSamples, double sr) noexcept
    {
        // Same hand-over as gpro::Handoff: the audio thread only swaps while 'trash' is empty, puts the map it lets go of there and
        // never deletes anything; the message thread frees 'trash' (setMap / collect). Nothing is ever lost, nothing freed here.
        if (pending.load (std::memory_order_acquire) != nullptr && trash.load (std::memory_order_acquire) == nullptr)
            if (auto* c = pending.exchange (nullptr, std::memory_order_acq_rel)) { trash.store (live, std::memory_order_release); live = c; snapNow = true; }
        const bool ext = useExternal.load (std::memory_order_relaxed);
        for (int k = 0; k < NUM_MACROS; ++k)
        {
            const int si = macroSlot[(size_t) k];
            float v = si >= 0 ? slots[(size_t) si]->raw->load() : 0.0f;
            if (ext) v = external[(size_t) k].load (std::memory_order_relaxed);
            value[(size_t) k] = juce::jlimit (0.0f, 1.0f, v);
        }
        for (auto& s : slots) s->target = 0.0f;
        if (live != nullptr && live->hasMap)
            for (auto& t : live->t)
            {
                float m = value[(size_t) t.macro], x;
                if (live->bipolar[(size_t) t.macro]) { m = m * 2.0f - 1.0f; x = (m < 0 ? -1.0f : 1.0f) * std::pow (std::abs (m), t.curve); }
                else x = std::pow (m, t.curve);
                slots[(size_t) t.slot]->target += t.amount * x;
            }
        // two one-pole stages of 25 ms (critically damped): a knob that steps once per host block glides with no kink at the
        // block edges (a single one-pole changes direction abruptly at every step: the zipper you hear on a ramp)
        const float a = 1.0f - (float) std::exp (-(double) numSamples / (0.025 * sr));
        for (int k = 0; k < NUM_MACROS; ++k) if (macroSlot[(size_t) k] >= 0) slots[(size_t) macroSlot[(size_t) k]]->target = 0.0f;
        bool still = true;
        for (size_t si = 0; si < slots.size(); ++si)
        {
            auto& s = slots[si];
            const float maxStep = s->slew * (float) numSamples / (float) sr;
            if (snapNow) s->sm = s->sm1 = s->target;       // a new sound (program / preset / state): its offsets apply at once, an old sound plays its own values
            else { s->sm1 += (s->target - s->sm1) * a; s->sm += juce::jlimit (-maxStep, maxStep, (s->sm1 - s->sm) * a); }
            if (std::abs (s->sm) < 1.0e-6f && std::abs (s->sm1) < 1.0e-6f) s->sm = s->sm1 = 0.0f;
            if (std::abs (s->target - s->sm) > 2.0e-4f * (s->hi - s->lo) || std::abs (s->target - s->sm1) > 2.0e-4f * (s->hi - s->lo)) still = false;
            float v = (ovUse != nullptr && ovUse[si] != 0) ? ovVal[si] : s->raw->load();      // MORPH BOARD: the blended value replaces the stored one
            if (s->sm != 0.0f) v = juce::jlimit (s->lo, s->hi, v + s->sm);
            s->base = v;                                   // MOD (round 3): what the router's offsets start from
            s->out.store (v, std::memory_order_relaxed);
        }
        if (ext) for (int k = 0; k < NUM_MACROS; ++k) if (macroSlot[(size_t) k] >= 0) slots[(size_t) macroSlot[(size_t) k]]->out.store (value[(size_t) k], std::memory_order_relaxed);
        settled = still; snapNow = false;
    }

    // Audio thread, before update(): true while a macro-driven value is still gliding to its target, or a knob (or the parent's,
    // for a layer) has moved since the last update. The processor then renders the host block in short sub-blocks, so every
    // parameter a macro drives moves in small steps instead of once per host block (no zipper at block edges). Sounds without a
    // macro map never ask for it (they render exactly as before).
    bool wantsFineSteps() const noexcept
    {
        if (live == nullptr || ! live->hasMap) return false;
        if (! settled) return true;
        const bool ext = useExternal.load (std::memory_order_relaxed);
        for (int k = 0; k < NUM_MACROS; ++k)
        {
            const int si = macroSlot[(size_t) k];
            const float v = juce::jlimit (0.0f, 1.0f, ext ? external[(size_t) k].load (std::memory_order_relaxed) : (si >= 0 ? slots[(size_t) si]->raw->load() : 0.0f));
            if (std::abs (v - value[(size_t) k]) > 1.0e-6f) return true;
        }
        return false;
    }

    // MOD (round 3): the modulation router's global offsets (normalised travel), on top of what update() wrote this block (audio thread)
    void applyMod (const float* off, uint8_t* touched, int count) noexcept
    {
        const int S = juce::jmin (count, (int) slots.size());
        for (int si = 0; si < S; ++si)
        {
            if (off[si] == 0.0f && touched[si] == 0) continue;
            auto& s = *slots[(size_t) si];
            float v = s.base;
            if (off[si] != 0.0f) v = s.prm->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, s.prm->convertTo0to1 (v) + off[si]));
            s.out.store (v, std::memory_order_relaxed);
            touched[si] = off[si] != 0.0f ? 1 : 0;
        }
    }

    // MORPH BOARD (audio thread, before update): per-slot values that replace the stored parameter values (nullptr = none)
    void setOverride (const float* vals, const uint8_t* use) noexcept { ovVal = vals; ovUse = use; }
    int slotCount() const noexcept { return (int) slots.size(); }
    const juce::String& slotId (int i) const { return slots[(size_t) i]->id; }
    juce::AudioParameterFloat* slotParam (int i) const noexcept { return slots[(size_t) i]->prm; }

    // For tests / UI: what a parameter is worth right now with the macros applied.
    float effective (const juce::String& id) const { for (auto& s : slots) if (s->id == id) return s->out.load(); return 0.0f; }

private:
    const float* ovVal = nullptr; const uint8_t* ovUse = nullptr;
    struct Slot { juce::String id; juce::AudioParameterFloat* prm = nullptr; std::atomic<float>* raw = nullptr; std::atomic<float> out { 0 }; float lo = 0, hi = 1, sm = 0, sm1 = 0, target = 0, slew = 1, base = 0; };   // (base: MOD round 3)
    struct Compiled { struct T { int slot, macro; float amount, curve; }; std::vector<T> t; std::array<bool, NUM_MACROS> bipolar {}; bool hasMap = false; };
    std::vector<std::unique_ptr<Slot>> slots;
    std::array<int, NUM_MACROS> macroSlot { -1, -1, -1, -1 };
    std::array<float, NUM_MACROS> value {};
    std::array<std::atomic<float>, NUM_MACROS> external;
    std::atomic<bool> useExternal { false };
    bool settled = true, snapNow = false;                           // audio thread: every macro-driven value has reached its target
    Compiled* live = nullptr;                      // audio thread owns
    std::atomic<Compiled*> pending { nullptr }, trash { nullptr };
    mutable juce::SpinLock mapLock; MacroMap map;  // message-thread copy
};
} // namespace gpro
