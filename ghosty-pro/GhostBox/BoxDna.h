#pragma once
// GHOST DNA (Ghost Box): BREED two sounds, MUTATE one (seeded, bounded, with per-unit / per-setting LOCKs), EVOLVE 4 variations to
// audition, UNDO back to the original. Every result is a normal sound (the SNAPSHOT format: BoxProcessor::captureSound), loaded through
// the click-free live path and editable like any other chain. Message thread only; the loudness match renders offline on a private
// rack (boxlayers::LayerHost) on a worker thread.
//
//   deterministic: the same inputs + seed + strength + locks give the same result (juce::Random (seed), a fixed walk over the units
//                  and settings in parameter order); the seed is shown and saved with the session
//   bounded:       every value stays inside its parameter's range (mutations move the normalised value and clamp it, choices pick a
//                  legal step); feedback-type settings (id contains "fb" / "feedback" / "regen") stay below 90 % of their range;
//                  the GHOST LIMITER is never mutated and is always ON in a result (the safety limiter: a variation never clips)
//   gain-matched:  the loudness difference (K-weighted, BS.1770 style) between the original and the result, measured on a fixed 4 s
//                  drum + bass + pad signal through a private rack: louder = the OUTPUT (box_out, after the limiter) turned down by it;
//                  quieter = the limiter's input GAIN raised (up to +12 dB, so a boost never clips), measured again, any overshoot taken
//                  off the OUTPUT (never louder). A result still more than 1 LU off is rolled again with the next seeds
//                  (seed + 7919 k, up to 6, the closest kept; deterministic; the seed used is shown)
//   what moves:    MUTATE moves the settings of units that are ON (and, at higher strength, may switch a unit on / off and swap two
//                  neighbours in the chain); BREED picks every unit from A or B (seeded, by BLEND) and, where both have the unit on,
//                  glides its continuous settings between them; the rest of the sound (patterns, curves, MORPH / MOTION / MACROS, MIX,
//                  IN / OUT) comes from the original / A
#include <juce_audio_processors/juce_audio_processors.h>
#include "FxCore.h"
#include <set>
#include <vector>

namespace boxdna {

struct Locks
{
    std::set<int> units;                 // unit numbers (gpfx::Unit)
    std::set<juce::String> params;       // parameter ids
    bool unitLocked (int u) const { return units.count (u) > 0; }
    bool paramLocked (const juce::String& id) const { return params.count (id) > 0; }
    juce::String unitsText() const { juce::StringArray a; for (int u : units) a.add (gpfx::unitInfo (u).key); return a.joinIntoString (","); }
    juce::String paramsText() const { juce::StringArray a; for (auto& p : params) a.add (p); return a.joinIntoString (","); }
    void fromText (const juce::String& u, const juce::String& p)
    {
        units.clear(); params.clear();
        for (auto& k : juce::StringArray::fromTokens (u, ",", "")) for (int x = 0; x < gpfx::kNumUnits; ++x) if (k == gpfx::unitInfo (x).key) units.insert (x);
        for (auto& k : juce::StringArray::fromTokens (p, ",", "")) if (k.startsWith ("fx_")) params.insert (k);
    }
    bool operator== (const Locks& o) const { return units == o.units && params == o.params; }
};

// one rack parameter as DNA sees it
struct Param { juce::String id; int unit = -1; bool discrete = false, isOn = false, isMix = false, feedback = false; juce::RangedAudioParameter* p = nullptr; int steps = 0; };

class Genome
{
public:
    // the processor's fx_ parameters (parameter order = the walk order: deterministic)
    explicit Genome (juce::AudioProcessor& proc)
    {
        for (auto* ap : proc.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (ap))
            {
                const auto id = rp->getParameterID();
                if (! id.startsWith ("fx_")) continue;
                Param q; q.id = id; q.p = rp; q.discrete = dynamic_cast<juce::AudioParameterFloat*> (rp) == nullptr;
                for (int u = 0; u < gpfx::kNumUnits; ++u) if (id.startsWith (juce::String ("fx_") + gpfx::unitInfo (u).key + "_")) { q.unit = u; break; }
                q.isOn = q.unit >= 0 && id == gpfx::pid (q.unit, "on"); q.isMix = q.unit >= 0 && id == gpfx::pid (q.unit, "mix");
                q.feedback = id.contains ("_fb") || id.containsIgnoreCase ("feedback") || id.containsIgnoreCase ("regen");
                q.steps = q.discrete ? juce::jmax (2, rp->getNumSteps()) : 0;
                params.push_back (q);
            }
    }
    const std::vector<Param>& all() const { return params; }

    // ---- values in a sound tree (real values in PARAM children; missing = the default)
    static float get (const juce::ValueTree& t, const Param& q)
    {
        const auto c = t.getChildWithProperty ("id", q.id);
        return c.isValid() ? (float) c.getProperty ("value") : q.p->convertFrom0to1 (q.p->getDefaultValue());
    }
    static void set (juce::ValueTree& t, const Param& q, float v)
    {
        v = q.p->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, q.p->convertTo0to1 (v)));   // inside the range, on a legal value
        auto c = t.getChildWithProperty ("id", q.id);
        if (! c.isValid()) { c = juce::ValueTree ("PARAM"); c.setProperty ("id", q.id, nullptr); t.appendChild (c, nullptr); }
        c.setProperty ("value", v, nullptr);
    }
    static bool unitOn (const juce::ValueTree& t, const std::vector<Param>& ps, int u)
    {
        for (auto& q : ps) if (q.isOn && q.unit == u) return get (t, q) > 0.5f;
        return false;
    }
    // the chain order of a sound (keys) and back
    static juce::StringArray orderOf (const juce::ValueTree& t)
    {
        const auto fx = t.getChildWithName ("FXRACK"); const auto rk = fx.isValid() && fx.getNumChildren() > 0 ? fx.getChild (0) : juce::ValueTree();
        return juce::StringArray::fromTokens (rk.getProperty ("order").toString(), ",", "");
    }
    static void setOrder (juce::ValueTree& t, const juce::StringArray& o)
    {
        auto fx = t.getChildWithName ("FXRACK"); if (! fx.isValid() || fx.getNumChildren() == 0) return;
        fx.getChild (0).setProperty ("order", o.joinIntoString (","), nullptr);
    }

    // ---- MUTATE
    juce::ValueTree mutate (const juce::ValueTree& base, juce::int64 seed, float strength, const Locks& locks) const
    {
        auto t = base.createCopy();
        juce::Random rng (seed);
        strength = juce::jlimit (0.0f, 1.0f, strength);
        // units switching on / off (never the limiter, never a locked unit)
        std::array<bool, gpfx::kNumUnits> on {}, turnedOn {};
        for (int u = 0; u < gpfx::kNumUnits; ++u) on[(size_t) u] = unitOn (t, params, u);
        for (auto& q : params)
        {
            if (! q.isOn || q.unit == gpfx::uLim || locks.unitLocked (q.unit) || locks.paramLocked (q.id)) { if (q.isOn) rng.nextFloat(); continue; }
            const float r = rng.nextFloat();
            if (r < strength * 0.12f) { on[(size_t) q.unit] = ! on[(size_t) q.unit]; turnedOn[(size_t) q.unit] = on[(size_t) q.unit]; set (t, q, on[(size_t) q.unit] ? 1.0f : 0.0f); }
        }
        // the settings of units that are on (a unit just switched on gets a fresh roll of its settings)
        for (auto& q : params)
        {
            const float r1 = rng.nextFloat(), r2 = rng.nextFloat(), r3 = rng.nextFloat();   // (always drawn: the walk stays the same whatever is locked)
            if (q.isOn || q.unit < 0 || q.unit == gpfx::uLim || ! on[(size_t) q.unit] || locks.unitLocked (q.unit) || locks.paramLocked (q.id)) continue;
            const float moveProb = turnedOn[(size_t) q.unit] ? 0.8f : 0.15f + 0.6f * strength;
            if (r1 > moveProb) continue;
            const float v01 = q.p->convertTo0to1 (get (t, q));
            float n01;
            if (q.discrete)
            {
                if (r2 > 0.25f + 0.5f * strength) continue;   // choices change less often
                n01 = (float) juce::jlimit (0, q.steps - 1, (int) (r3 * (float) q.steps)) / (float) juce::jmax (1, q.steps - 1);
            }
            else n01 = v01 + (r2 * 2.0f - 1.0f) * (0.08f + 0.32f * strength) * (turnedOn[(size_t) q.unit] ? 1.6f : 1.0f);
            if (q.feedback) n01 = juce::jmin (n01, 0.9f);
            set (t, q, q.p->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, n01)));
        }
        // the chain: at higher strength two neighbouring units that are on may swap (never the limiter, never a locked unit)
        auto order = orderOf (t);
        const float rs = rng.nextFloat(), ri = rng.nextFloat();
        if (order.size() > 2 && rs < strength * 0.25f)
        {
            std::vector<int> live;
            for (int i = 0; i + 1 < order.size(); ++i)
            {
                int ua = -1, ub = -1; for (int u = 0; u < gpfx::kNumUnits; ++u) { if (order[i] == gpfx::unitInfo (u).key) ua = u; if (order[i + 1] == gpfx::unitInfo (u).key) ub = u; }
                if (ua >= 0 && ub >= 0 && ua != gpfx::uLim && ub != gpfx::uLim && on[(size_t) ua] && on[(size_t) ub] && ! locks.unitLocked (ua) && ! locks.unitLocked (ub)) live.push_back (i);
            }
            if (! live.empty()) { const int i = live[(size_t) juce::jlimit (0, (int) live.size() - 1, (int) (ri * (float) live.size()))]; order.getReference (i).swapWith (order.getReference (i + 1)); setOrder (t, order); }
        }
        safety (t);
        return t;
    }

    // ---- BREED: blend = how much of B (0 = all A, 1 = all B)
    juce::ValueTree breed (const juce::ValueTree& a, const juce::ValueTree& b, juce::int64 seed, float blend, const Locks& locks) const
    {
        auto t = a.createCopy();
        juce::Random rng (seed);
        blend = juce::jlimit (0.0f, 1.0f, blend);
        std::array<int, gpfx::kNumUnits> from {};   // 0 = A, 1 = B
        for (int u = 0; u < gpfx::kNumUnits; ++u) { const float r = rng.nextFloat(); from[(size_t) u] = (u != gpfx::uLim && ! locks.unitLocked (u) && r < blend) ? 1 : 0; }
        std::array<bool, gpfx::kNumUnits> onA {}, onB {};
        for (int u = 0; u < gpfx::kNumUnits; ++u) { onA[(size_t) u] = unitOn (a, params, u); onB[(size_t) u] = unitOn (b, params, u); }
        for (auto& q : params)
        {
            const float r = rng.nextFloat();
            if (q.unit < 0 || q.unit == gpfx::uLim || locks.unitLocked (q.unit) || locks.paramLocked (q.id)) continue;
            const auto& src = from[(size_t) q.unit] == 1 ? b : a;
            const bool both = onA[(size_t) q.unit] && onB[(size_t) q.unit];
            if (both && ! q.discrete && ! q.isOn)
            {   // a unit both parents use: its continuous settings glide between them (normalised space), around BLEND
                const float x = juce::jlimit (0.0f, 1.0f, blend + (r - 0.5f) * 0.5f);
                const float va = q.p->convertTo0to1 (get (a, q)), vb = q.p->convertTo0to1 (get (b, q));
                set (t, q, q.p->convertFrom0to1 (va + (vb - va) * x));
            }
            else set (t, q, get (src, q));
        }
        if (rng.nextFloat() < blend) setOrder (t, orderOf (b));
        safety (t);
        return t;
    }

    // a result always has the safety limiter on (its settings are the original's)
    void safety (juce::ValueTree& t) const
    {
        for (auto& q : params) if (q.isOn && q.unit == gpfx::uLim) set (t, q, 1.0f);
    }
    // how many rack settings differ (and in which units) between two sounds
    juce::String diffText (const juce::ValueTree& a, const juce::ValueTree& b, int* count = nullptr) const
    {
        int n = 0; std::set<int> us;
        for (auto& q : params) if (std::abs (get (a, q) - get (b, q)) > 1.0e-6f) { ++n; if (q.unit >= 0) us.insert (q.unit); }
        if (count) *count = n;
        juce::StringArray names; for (int u : us) names.add (gpfx::unitInfo (u).shortName);
        if (orderOf (a) != orderOf (b)) names.add ("ORDER");
        return juce::String (n) + " changes" + (names.isEmpty() ? juce::String() : ": " + names.joinIntoString (" "));
    }

private:
    std::vector<Param> params;
};

} // namespace boxdna

// ---- the loudness match: a fixed, deterministic 3 s drum + pad signal (120 BPM) through a private rack
namespace boxdna {
inline const juce::AudioBuffer<float>& testSignal (double sr)
{   // 4 s at 120 BPM: kick / snare / hats, an 808-style bass line and a soft chord pad (a small mix, deterministic)
    static juce::AudioBuffer<float> sig; static double made = 0;
    static juce::CriticalSection lock; const juce::ScopedLock sl (lock);
    if (std::abs (made - sr) < 1.0e-9 && sig.getNumSamples() > 0) return sig;
    const int n = (int) (4.0 * sr); sig.setSize (2, n); const double spb = 0.5 * sr, twoPi = juce::MathConstants<double>::twoPi;
    uint32_t r = 12345u; auto rnd = [&r] { r = r * 1664525u + 1013904223u; return (float) (r >> 8) / 8388608.0f - 1.0f; };
    double ph[8] {}, bph = 0; float lpL = 0, lpR = 0;
    static const double chords[2][4] { { 130.8, 196.0, 261.6, 329.6 }, { 110.0, 164.8, 220.0, 277.2 } };
    static const double bassNotes[8] { 41.2, 41.2, 49.0, 41.2, 55.0, 41.2, 36.7, 41.2 };
    for (int i = 0; i < n; ++i)
    {
        const double beat = i / spb, b16 = beat * 4.0, f16 = (b16 - std::floor (b16)) * spb / 4.0 / sr; const int step = (int) b16 % 16;
        float k = 0, s = 0, h = 0, pL = 0, pR = 0;
        if (step == 0 || step == 8 || step == 10) k = (float) (std::sin (twoPi * (45.0 * f16 + 90.0 * (1.0 - std::exp (-f16 * 35.0)) / 35.0)) * std::exp (-f16 * 7.0)) * 0.55f;
        if (step == 4 || step == 12) s = (float) ((rnd() * 0.7 + 0.35 * std::sin (twoPi * 190.0 * f16)) * std::exp (-f16 * 16.0)) * 0.4f;
        if (step % 2 == 0) h = rnd() * (float) std::exp (-f16 * (step % 4 == 2 ? 30.0 : 70.0)) * 0.14f;
        const double e8 = beat * 2.0, t8 = (e8 - std::floor (e8)) * spb * 0.5 / sr; const int s8 = (int) e8 % 8;
        bph += bassNotes[s8] * (1.0 + 0.6 * std::exp (-t8 * 40.0)) / sr; bph -= std::floor (bph);
        const float bass = (float) (std::sin (twoPi * bph) * std::min (1.0, t8 / 0.004) * std::exp (-t8 * 2.5)) * 0.45f;
        const int c = (int) (beat / 4.0) % 2;
        for (int v = 0; v < 4; ++v) for (int d = 0; d < 2; ++d)
        {
            double& p = ph[v * 2 + d]; p += chords[c][v] * (d ? 1.005 : 0.995) / sr; p -= std::floor (p);
            const float x = (float) (2.0 * p - 1.0) * 0.05f; if (d) pR += x; else pL += x;
        }
        lpL += 0.12f * (pL - lpL); lpR += 0.12f * (pR - lpR);
        sig.setSample (0, i, k + s + h + bass + lpL); sig.setSample (1, i, k + s * 0.95f + h * 0.8f + bass + lpR);
    }
    made = sr; return sig;
}
} // namespace boxdna
