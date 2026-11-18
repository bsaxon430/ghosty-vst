// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX: LYNX SHARE's six reverbs (Ka1-S/LYNX-SHARE @ d141370, src/core/LynxVerbs.{h,cpp}, AGPL-3.0, vendored unchanged in
// third_party/lynx) as extra engines of HAUNTED HALLS (VINTAGE, ROOMS, PLATES), SPECTRAL SPACE (NEBULA, HORIZON) and SHIMMER (HALO).
// The LYNX engine and its front-ends do all the sound; this file only turns the effect's own controls (seconds, ms, Hz, %) into the
// front-end's 0..1 controls with LYNX's own laws (epoch::decaySec, foil::decaySec, ...), so each of our controls does on each engine what
// its name says. Our own defaults land on the front-end's own defaults wherever the two have the same control.
#pragma once
#include "AltEngine.h"
#include "core/LynxVerbs.h"
#include "core/Effects.h"
#include "../effects/SpaceCommon.h"

namespace phantom::engines
{
class LynxVerbEngine final : public AltEngine
{
public:
    enum Kind { Epoch, Atrium, Foil, Nebula, Halo, Horizon };
    static int voiceCount (Kind k) noexcept { switch (k) { case Epoch: return lynx::verbs::epoch::modeCount; case Atrium: return lynx::verbs::atrium::typeCount; case Foil: return lynx::verbs::foil::typeCount; case Nebula: return lynx::verbs::nebula::modeCount; case Halo: return 7; case Horizon: return lynx::verbs::horizon::reverbModeCount; } return 1; }
    static std::string voiceName (Kind k, int i)
    {
        static const char* halo[7] { "Wide", "Mono", "Small stereo", "Medium stereo", "Dense", "Big stereo", "Bright" };
        switch (k) { case Epoch: return lynx::verbs::epoch::modeName (i); case Atrium: return lynx::verbs::atrium::typeName (i); case Foil: return lynx::verbs::foil::typeName (i);
                     case Nebula: return lynx::verbs::nebula::modeName (i); case Halo: return halo[juce::jlimit (0, 6, i)]; case Horizon: return lynx::verbs::horizon::reverbModeName (i); }
        return {};
    }
    // defaults: our controls' declared defaults (the unit's ParamSpecs), so "around our default" maps line up per unit
    LynxVerbEngine (Kind k, const std::vector<ParamSpec>& specs) : kind (k)
    {
        for (auto& s : specs) defs[s.id] = s.def;
        for (auto& f : fx) f = lynx::verbs::make (lynxKind());
        const auto& inf = lynx::verbs::info (lynxKind()); base.fill (0.0f);
        for (size_t i = 0; i < inf.params.size(); ++i) base[i] = inf.params[i].defaultValue;
        base[0] = 1.0f;   // MIX 100 %: our slot's MIX blends the dry (the effect is wet-only)
    }
    void prepare (double sampleRate, int) override
    {
        sr = sampleRate; for (auto& f : fx) { f->prepare (sampleRate); f->reset(); }
        pre.prepare (sampleRate, 0.52); glide.cur = -1.0f;
        // glide times per control: the modulation depth moves the tank's taps (a fast change is a pitch bend), so it glides slowest;
        // the decay, diffusion and tone controls retune the tank's filters and gains
        for (int k = 0; k < kCtl; ++k) { const double sec = k == cMod ? 0.6 : (k == cDecay || k == cDiff || k == cDamp || k == cTone || k == cBlend || k == cEarly || k == cShim) ? 0.3 : 0.05;
                                          smA[(size_t) k] = 1.0f - (float) std::exp (-(double) kSub / (sampleRate * sec)); }
        reset();
    }
    void reset() noexcept override
    {
        for (auto& f : fx) f->reset(); pre.reset(); glide.cur = -1.0f; inGain = 1.0f; wPrimed = false; primed = false;
        cur = 0; next = -1; xf = 1.0f; inFade = 1.0f; settle = 0; builtPrimed = false; pendingS = { -1.0f, -1.0f, -1.0f, -1.0f };
    }
    void process (float* L, float* R, int n, const Controls& c, int voice, const TempoInfo& t) noexcept override
    {
        // our controls, smoothed here (50 to 600 ms, see prepare) and handed to the engine every 32 samples: LYNX glides inside a block, a fast host sweep
        // would still step from block to block
        float tgt[kCtl]; readControls (c, tgt);
        if (! primed) { std::copy (tgt, tgt + kCtl, sm); primed = true; }
        // the room's shape (its line lengths: SIZE, or DECAY on Halo; how far its taps move: MODULATION; its all-passes: DIFFUSION, BLEND)
        // is re-formed once those controls have stopped moving for 100 ms, in a second engine that the first crossfades to over 150 ms
        // (as HAUNTED HALLS does with its own engine): sweeping them never bends the tank's pitch
        const Shape want = shapeOf (tgt);
        if (! (want == pendingS)) { pendingS = want; settle = 0; } else settle += n;
        if (! builtPrimed) { built[0] = built[1] = want; builtPrimed = true; }
        if (next < 0 && ! (want == built[(size_t) cur]) && settle >= (int) (0.1 * sr))
        {   next = 1 - cur; built[(size_t) next] = want; fx[(size_t) next]->reset(); xf = 0.0f; inFade = 0.0f; }
        const bool frz = tgt[cFreeze] >= 0.5f; const float gStep = 1.0f / (0.02f * (float) sr);
        const double bpm = t.bpm > 0.0 ? t.bpm : 120.0;
        for (int i0 = 0; i0 < n; i0 += kSub)
        {
            const int m = juce::jmin (kSub, n - i0); float* l = L + i0; float* r = R + i0;
            for (int k = 0; k < kCtl; ++k) sm[k] += smA[(size_t) k] * (tgt[k] - sm[k]);
            // PRE-DELAY: ours (a slow tape-like glide, as on the PHANTOM engines); the engine's own pre-delay stays at 0
            for (int i = 0; i < m; ++i)
            {
                const float pd = glide (juce::jlimit (0.0f, 500.0f, tgt[cPre]) * 0.001f * (float) sr);
                inGain = frz ? juce::jmax (0.0f, inGain - gStep) : juce::jmin (1.0f, inGain + gStep);   // FREEZE: the input fades out, the tail holds
                pre.write (0, l[i]); pre.write (1, r[i]); l[i] = pre.read (0, pd + 1.0f) * inGain; r[i] = pre.read (1, pd + 1.0f) * inGain;
            }
            if (next >= 0)
            {
                for (int i = 0; i < m; ++i)
                {   // the incoming room hears the input fade in over 20 ms (a tone switched on mid-wave would click inside it)
                    inFade = juce::jmin (1.0f, inFade + gStep); const float g = 0.5f - 0.5f * std::cos (inFade * juce::MathConstants<float>::pi);
                    nl[(size_t) i] = l[i] * g; nr[(size_t) i] = r[i] * g;
                }
                auto pn = base; configure (pn, sm, built[(size_t) next], frz, voice); fx[(size_t) next]->process (nl.data(), nr.data(), m, pn, bpm);
            }
            auto pc = base; configure (pc, sm, built[(size_t) cur], frz, voice); fx[(size_t) cur]->process (l, r, m, pc, bpm);
            if (next >= 0)
            {
                const float step = 1.0f / (0.15f * (float) sr);
                for (int i = 0; i < m; ++i)
                {
                    xf = juce::jmin (1.0f, xf + step); const float a = std::cos (xf * juce::MathConstants<float>::halfPi), b = std::sin (xf * juce::MathConstants<float>::halfPi);
                    l[i] = l[i] * a + nl[(size_t) i] * b; r[i] = r[i] * a + nr[(size_t) i] * b;
                }
                if (xf >= 1.0f) { cur = next; next = -1; }
            }
        }
        // WIDTH: ours, mid / side on the engine's output, so WIDTH 0 is exactly mono (as on the PHANTOM engines)
        // (ROOMS: its late tank answers a wide stereo source with much side energy, so its full width is 80 % side: the mono sum of a
        // stereo mix keeps its share, Phantom's stereo-integrity bar)
        const float wt = clamp01 (tgt[cWidth]) * (kind == Atrium ? 0.8f : 1.0f); if (! wPrimed) { w = wt; wPrimed = true; }
        const float ws = (wt - w) / (float) juce::jmax (1, n);
        for (int i = 0; i < n; ++i) { w += ws; const float mm = 0.5f * (L[i] + R[i]), sd = 0.5f * (L[i] - R[i]) * w; L[i] = mm + sd; R[i] = mm - sd; }
    }
private:
    lynx::EffectKind lynxKind() const noexcept { return (lynx::EffectKind) (int) kind; }
    float def (const char* id, float fb) const { auto it = defs.find (id); return it == defs.end() ? fb : it->second; }
    enum Ctl { cSize, cDecay, cPre, cDiff, cDamp, cLow, cMod, cEarly, cTone, cBlend, cShim, cWidth, cFreeze, cInterval, kCtl };
    static constexpr int kSub = 32;
    void readControls (const Controls& c, float* v) const noexcept
    {
        v[cSize] = c.get ("size", def ("size", 0.6f)); v[cDecay] = c.get ("decay", def ("decay", 3.0f)); v[cPre] = c.get ("predelay", 0.0f);
        v[cDiff] = c.get ("diffusion", def ("diffusion", 0.7f)); v[cDamp] = c.get ("damp", def ("damp", 0.4f)); v[cLow] = c.get ("lowcut", 0.0f);
        v[cMod] = c.get ("mod", def ("mod", 0.4f)); v[cEarly] = c.get ("early", def ("early", 0.5f)); v[cTone] = c.get ("tone", 0.0f);
        v[cBlend] = c.get ("blend", 0.6f); v[cShim] = c.get ("shimmer", def ("shimmer", 0.5f)); v[cWidth] = c.get ("width", 1.0f);
        v[cFreeze] = (c.get ("freeze", 0.0f) >= 0.5f || c.get ("hold", 0.0f) >= 0.5f) ? 1.0f : 0.0f; v[cInterval] = c.get ("interval", 0.0f);
    }
    struct Shape { float size = 0.0f, mod = 0.0f, diff = 0.0f, blend = 0.0f; bool operator== (const Shape& o) const noexcept { return size == o.size && mod == o.mod && diff == o.diff && blend == o.blend; } };
    static float q (float v) noexcept { return std::round (v * 40.0f) / 40.0f; }
    Shape shapeOf (const float* v) const noexcept
    {
        Shape sh; sh.size = q (kind == Halo ? (v[cFreeze] >= 0.5f ? 1.0f : clamp01 ((float) ((v[cDecay] - 1.5) / 12.5))) : clamp01 (v[cSize]));
        sh.mod = q (v[cMod]); sh.diff = q (v[cDiff]); sh.blend = q (v[cBlend]); return sh;
    }
    // DAMPING and TONE together: -1 (open, brighter than the engine's default) .. 0 (its default) .. +1 (dark)
    float darkness (const float* v) const noexcept
    {
        float d = 0.0f;
        if (defs.count ("damp")) d += around (v[cDamp], 0.0f, def ("damp", 0.4f), 1.0f, -1.0f, 0.0f, 1.0f);
        if (defs.count ("tone")) d -= v[cTone];
        return juce::jlimit (-1.0f, 1.0f, d);
    }
    // a 0..1 high cut whose engine default is dflt: darker moves it down to 35 % of the default, brighter up to fully open
    static float cut (float dflt, float dark) noexcept { return dark < 0.0f ? dflt + (1.0f - dflt) * -dark : dflt * (1.0f - 0.65f * dark); }
    // our controls (v, smoothed) -> the front-end's 0..1 controls; st = the structure this engine instance was built with
    void configure (lynx::EffectParams& p, const float* v, const Shape& st, bool frz, int voice) const noexcept
    {
        using namespace lynx::verbs;
        const float size = st.size, sizeDef = def ("size", 0.6f), mod = st.mod, modDef = def ("mod", 0.4f);
        const double decay = v[cDecay], lowHz = v[cLow];
        const float diff = st.diff, diffDef = def ("diffusion", 0.7f), early = v[cEarly], earlyDef = def ("early", 0.5f), dark = darkness (v);
        const int nv = voiceCount (kind); const float vsel = nv > 1 ? (float) juce::jlimit (0, nv - 1, voice) / (float) (nv - 1) : 0.0f;
        switch (kind)
        {
            case Epoch:
                p[1] = 0.0f; p[2] = inverse01 ([] (float x) { return epoch::decaySec (x); }, frz ? 70.0 : decay);
                p[3] = around (size, 0.0f, sizeDef, 1.0f, 0.3f, 1.0f, 1.0f); p[9] = around (diff, 0.0f, diffDef, 1.0f, 0.3f, 1.0f, 1.0f);
                p[12] = around (mod, 0.0f, modDef, 1.0f, 0.0f, base[12], 1.0f); p[13] = cut (base[13], dark); p[14] = clamp01 ((float) ((lowHz - 10.0) / 1490.0)); p[16] = vsel; break;
            case Atrium:
                p[1] = 0.0f; p[2] = atrium::decayValue (frz ? 100.0 : decay); p[3] = cut (base[3], dark);
                p[4] = around (early, 0.0f, earlyDef, 1.0f, 0.9f, base[4], 0.1f); p[5] = around (size, 0.0f, sizeDef, 1.0f, 0.1f, base[5], 1.0f);
                p[8] = around (mod, 0.0f, modDef, 1.0f, 0.0f, base[8], 1.0f); p[18] = around (diff, 0.0f, diffDef, 1.0f, 0.3f, base[18], 1.0f);
                p[19] = vsel; p[21] = clamp01 ((float) (lowHz / 1000.0)); break;
            case Foil:
                p[1] = 0.0f; p[2] = inverse01 ([] (float x) { return foil::decaySec (x); }, decay);
                p[3] = around (size, 0.0f, sizeDef, 1.0f, 0.15f, base[3], 1.0f); p[6] = around (mod, 0.0f, modDef, 1.0f, 0.0f, 0.0f, 0.6f);
                p[10] = clamp01 (0.5f - 0.35f * dark); p[11] = vsel; p[12] = frz ? 1.0f : 0.0f;
                p[13] = lowHz <= 20.0 ? 0.0f : clamp01 ((float) (std::log (lowHz / 20.0) / std::log (50.0))); break;
            case Nebula:
            {   // SIZE is the network's delay, DECAY its feedback (LYNX's own RT law, RT = 6 D / -log10 F), BLEND its density
                p[3] = around (size, 0.0f, sizeDef, 1.0f, 0.15f, base[3], 0.9f);
                const double meanSec = nebula::delayMs (p[3]) / 1000.0;
                p[5] = frz ? 1.0f : clamp01 ((float) std::pow (10.0, -6.0 * meanSec / juce::jmax (0.05, decay)));
                p[6] = juce::jlimit (0.03f, 1.0f, st.blend); p[9] = cut (base[9], dark); p[11] = around (mod, 0.0f, modDef, 1.0f, 0.0f, base[11], 1.0f); p[12] = vsel; break;
            }
            case Halo:
            {   // DECAY is Halo's size (its RT follows 1.5 + 12.5 x Size s), SHIMMER its shifted feedback, SHIFT its interval
                static const float modeValue[7] { 0.0f, 0.125f, 0.25f, 0.375f, 0.5f, 0.625f, 1.0f };
                const int iv = (int) std::lround (v[cInterval]);
                p[1] = iv == 1 ? 19.0f / 24.0f : iv == 3 ? 0.0f : 1.0f; p[10] = iv == 2 ? 0.25f : 0.0f;   // +12 / +7 / +12 dual / -12
                p[2] = frz ? 1.0f : around (v[cShim], 0.0f, def ("shimmer", 0.5f), 1.0f, 0.0f, 0.5f, 0.95f);
                p[4] = st.size; p[6] = cut (base[6], dark);
                p[8] = around (mod, 0.0f, modDef, 1.0f, 0.0f, base[8], 1.0f); p[9] = modeValue[juce::jlimit (0, 6, voice)]; break;
            }
            case Horizon:
                p[4] = 0.0f; p[9] = 0.0f;   // echo level 0 at its shortest time: our own pre-delay goes first
                p[11] = inverse01 ([] (float x) { return horizon::decaySec (x); }, decay); p[12] = around (size, 0.0f, sizeDef, 1.0f, 0.1f, base[12], 1.0f);
                p[18] = around (mod, 0.0f, modDef, 1.0f, 0.0f, base[18], 1.0f); p[20] = cut (base[20], dark); p[22] = vsel; break;
        }
    }
    Kind kind; std::array<std::unique_ptr<lynx::Effect>, 2> fx; lynx::EffectParams base {}; std::map<std::string, float> defs;
    double sr = 48000.0; float inGain = 1.0f, w = 1.0f, xf = 1.0f, inFade = 1.0f, pendingDummy = 0.0f; bool wPrimed = false, primed = false, builtPrimed = false;
    float sm[kCtl] {}; std::array<float, kCtl> smA {}; std::array<Shape, 2> built {}; int cur = 0, next = -1, settle = 0; Shape pendingS { -1.0f, -1.0f, -1.0f, -1.0f };
    StereoDelay pre; PreGlide glide; std::array<float, kSub> nl {}, nr {};
};
} // namespace phantom::engines
