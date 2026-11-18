// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX 18 SPECTRAL SPACE and 19 SHIMMER on the CloudSeed reverb core (Ghost Note Audio, CloudSeedCore, MIT; third_party/cloudseed).
// CloudSeed's own ReverbChannel runs each side (its input mix, pre-filters, early diffuser and late delay-line network with its in-loop EQ);
// we only move its parameters. SHIMMER adds Signalsmith Stretch (MIT) as the pitch shifter in the reverb's feedback.
// Ours (glue / features CloudSeed has not got): the smooth pre-delay (CloudSeed's own pre-delay jumps in whole samples), ducking,
// output width, freeze / infinite hold, the level match and the parameter mapping.
#pragma once
#include "SpaceCommon.h"
#include "../space/CloudSeedShim.h"
#include "DSP/ReverbChannel.h"
#ifdef PHANTOM_CLOUDSEED_DEFINED_BUFFER_SIZE
#undef BUFFER_SIZE   // chowdsp uses the name as a template parameter
#endif
#include "signalsmith-stretch/signalsmith-stretch.h"

namespace phantom
{
// ---------------------------------------------------------------- CloudSeed: two channels exactly as its ReverbController runs them
class CloudVerb
{
public:
    void prepare (double sampleRate)
    {
        sr = (int) std::lround (sampleRate);
        // CloudSeed's delays take their LFO start phases from std::rand(): seeded here so every instance (and every test run) is the same
        if (! L) { std::srand (20241); L = std::make_unique<Cloudseed::ReverbChannel> (sr, Cloudseed::ChannelLR::Left); R = std::make_unique<Cloudseed::ReverbChannel> (sr, Cloudseed::ChannelLR::Right); }
        L->SetSamplerate (sr); R->SetSamplerate (sr);
        for (auto& v : cur) v = -1.0;
        clear();
    }
    void clear() { if (L) { L->ClearBuffers(); R->ClearBuffers(); } }
    // a CloudSeed parameter in its own 0..1 scale (ScaleParam turns it into the engine's units, as its controller does)
    void set (int id, double v01) noexcept
    {
        if (cur[(size_t) id] == v01) return;
        cur[(size_t) id] = v01; inputMix = id == Cloudseed::Parameter::InputMix ? (float) Cloudseed::ScaleParam (v01, id) : inputMix;
        const double s = Cloudseed::ScaleParam (v01, id); L->SetParameter (id, s); R->SetParameter (id, s);
    }
    // an engine-unit value directly (used only for the infinite hold: a decay beyond the 60 s CloudSeed's knob stops at)
    void setScaled (int id, double scaled) noexcept { cur[(size_t) id] = -2.0; L->SetParameter (id, scaled); R->SetParameter (id, scaled); }
    // stereo in place, n <= 1024 (CloudSeed's BUFFER_SIZE)
    void process (float* l, float* r, int n) noexcept
    {
        const float cm = inputMix * 0.5f, cmi = 1.0f - cm;
        for (int i = 0; i < n; ++i) { const float a = l[i], b = r[i]; inL[i] = a * cmi + b * cm; inR[i] = b * cmi + a * cm; }
        L->Process (inL, l, n); R->Process (inR, r, n);
    }
private:
    std::unique_ptr<Cloudseed::ReverbChannel> L, R; int sr = 48000; float inputMix = 0.0f;
    std::array<double, Cloudseed::Parameter::COUNT> cur {};
    float inL[1024] {}, inR[1024] {};
};

// CloudSeed's knob responses, inverted (so our controls can be in seconds / ms / Hz)
namespace cs
{
    using namespace Cloudseed;
    inline double inv3dec (double r) { return std::log10 (1.0 + juce::jmax (0.0, r) * 999.0) / 3.0; }   // Resp3dec(x) = (10^3x - 1) / 999
    inline double inv2dec (double r) { return std::log10 (1.0 + juce::jmax (0.0, r) * 99.0) / 2.0; }
    inline double inv4oct (double r) { return std::log2 (1.0 + juce::jmax (0.0, r) * 15.0) / 4.0; }      // Resp4oct(x) = (2^4x - 1) / 15
    inline double decay01 (double seconds) { return juce::jlimit (0.0, 1.0, inv3dec ((seconds - 0.05) / 59.95)); }
    inline double lineSize01 (double ms) { return juce::jlimit (0.0, 1.0, inv2dec ((ms - 20.0) / 980.0)); }
    inline double cutoff01 (double hz) { return juce::jlimit (0.0, 1.0, inv4oct ((hz - 400.0) / 19600.0)); }
    inline double lowcut01 (double hz) { return juce::jlimit (0.0, 1.0, inv4oct ((hz - 20.0) / 980.0)); }
    inline double gainDb01 (double dB) { return dB <= -30.0 ? 0.0 : juce::jlimit (0.0, 1.0, (dB + 30.0) / 30.0); }   // Dry/Early/Late out
    inline double eqGain01 (double dB) { return juce::jlimit (0.0, 1.0, (dB + 20.0) / 20.0); }
    inline double mod01 (double ms) { return juce::jlimit (0.0, 1.0, ms / 2.5); }
    inline double rate01 (double hz) { return juce::jlimit (0.0, 1.0, inv2dec (hz / 5.0)); }

    // the fixed part of our CloudSeed setting (from its Dark Plate program, with fewer late lines: CPU, see DECISIONS.md 21)
    inline void base (CloudVerb& v, int lines, int lateDiffuse, int earlyDiffuse)
    {
        v.set (Parameter::Interpolation, 1.0); v.set (Parameter::LowCutEnabled, 1.0); v.set (Parameter::HighCutEnabled, 0.0);
        v.set (Parameter::InputMix, 0.2347); v.set (Parameter::DryOut, 0.0); v.set (Parameter::TapEnabled, 0.0); v.set (Parameter::TapPredelay, 0.0);
        v.set (Parameter::EarlyDiffuseEnabled, 1.0); v.set (Parameter::EarlyDiffuseCount, (earlyDiffuse - 1) / 11.999); v.set (Parameter::EarlyDiffuseFeedback, 0.7707);
        v.set (Parameter::LateMode, 1.0); v.set (Parameter::LateLineCount, (lines - 1) / 11.999 + 1.0e-4);
        v.set (Parameter::LateDiffuseEnabled, 1.0); v.set (Parameter::LateDiffuseCount, (lateDiffuse - 1) / 7.999 + 1.0e-4); v.set (Parameter::LateDiffuseFeedback, 0.8507);
        v.set (Parameter::LateDiffuseDelay, 0.24); v.set (Parameter::EqLowShelfEnabled, 1.0); v.set (Parameter::EqHighShelfEnabled, 1.0); v.set (Parameter::EqLowpassEnabled, 1.0);
        v.set (Parameter::EqLowFreq, 0.388); v.set (Parameter::EqHighFreq, 0.5134); v.set (Parameter::EqCrossSeed, 0.0);
        v.set (Parameter::SeedTap, 0.334); v.set (Parameter::SeedDiffusion, 0.185); v.set (Parameter::SeedDelay, 0.2181); v.set (Parameter::SeedPostDiffusion, 0.3653);
    }
}

// ================================================================ 18 SPECTRAL SPACE
class SpectralSpaceFx final : public CatalogEffect
{
public:
    SpectralSpaceFx() : CatalogEffect (uSpace) {}
    bool wetOnly() const override { return true; }
    bool widensOnPurpose() const override { return true; }
    double tailSeconds() const override { return 14.0; }
    double ownSpreadHz() const override { return 250.0; }   // GLIMMER at full moves the tail by ~1 % per pass, ~250 Hz at its 6 kHz images
    // CloudSeed's delay modulation refreshes every 8 samples, which leaves faint images of a tone 6 kHz away; any control other than the
    // modulation ones is zipper-checked with that modulation off, so the check measures the control's own stepping
    void zipperIsolate (const std::string& c, std::vector<float>& v) const override { if (c != "mod") v[(size_t) id ("mod")] = 0.0f; if (c != "glimmer") v[(size_t) id ("glimmer")] = 0.0f; }
protected:
    void prepareEffect (double sampleRate, int, int) override
    {
        verb.prepare (sampleRate); cs::base (verb, kLines, kLateDiffuse, kEarlyDiffuse);
        pre.prepare (sampleRate, 0.26); duck.prepare (sampleRate); appliedSize = -1.0;
        pSize = id ("size"); pBlend = id ("blend"); pDecay = id ("decay"); pTone = id ("tone"); pMod = id ("mod"); pGlim = id ("glimmer");
        pWidth = id ("width"); pPre = id ("predelay"); pDuck = id ("duck"); pDamp = id ("damp");
        applied = false;
    }
    void resetEffect() override { verb.clear(); pre.reset(); glidePre.cur = -1.0f; duck.reset(); applied = false; }
public:
    void process (juce::dsp::AudioBlock<float> block, const ModulationContext& ctx) noexcept override
    {
        beginBlock (ctx);
        const int n = (int) block.getNumSamples(); auto* L = block.getChannelPointer (0); auto* R = block.getNumChannels() > 1 ? block.getChannelPointer (1) : L;
        for (int i0 = 0; i0 < n; )
        {
            // CloudSeed's gains / filters / delay lengths change per call: while a control glides, calls are 8 samples (CloudSeed's own
            // modulation update rate); settled, 64
            const bool moving = anyMoving();
            const int len = juce::jmin (moving ? 8 : 64, n - i0);
            for (auto* s : { &p (pBlend), &p (pDecay), &p (pTone), &p (pMod), &p (pGlim), &p (pDamp) }) s->skip (len);
            if (moving || ! applied) applyEngine();
            float* l = L + i0; float* r = R + i0;
            // our smooth pre-delay + ducking detector on the input
            for (int i = 0; i < len; ++i)
            {
                const float pd = glidePre (p (pPre).next() * 0.001f * (float) sr);
                duck.push (juce::jmax (std::abs (l[i]), std::abs (r[i])));
                pre.write (0, l[i]); pre.write (1, r[i]);
                l[i] = pre.read (0, pd + 1.0f); r[i] = pre.read (1, pd + 1.0f);
            }
            verb.process (l, r, len);
            for (int i = 0; i < len; ++i)
            {
                const float w = p (pWidth).next(), d = duck.gain (p (pDuck).next());
                const float m = 0.5f * (l[i] + r[i]), s = 0.5f * (l[i] - r[i]) * w;
                l[i] = (m + s) * d * kLevel; r[i] = (m - s) * d * kLevel;
            }
            i0 += len;
        }
    }
private:
    static constexpr int kLines = 4, kLateDiffuse = 3, kEarlyDiffuse = 6;   // CPU: 4 late lines pass the tail checks (8: same results, 1.6x the CPU)
    static constexpr float kLevel = 1.218f;   // level match (gain-staging measurement, DECISIONS.md 21)
    bool anyMoving() const noexcept
    {
        for (int k : { pBlend, pDecay, pTone, pMod, pGlim, pDamp }) if (smoothers[(size_t) k].isSmoothing()) return true;
        return target (pSize) != appliedSize;
    }
    void applyEngine() noexcept
    {
        using namespace Cloudseed;
        const double blend = p (pBlend).current, decay = p (pDecay).current, tone = p (pTone).current, mod = p (pMod).current, glim = p (pGlim).current, damp = p (pDamp).current;
        if (sizeSettled (target (pSize))) { const double sz = appliedSize; verb.set (Parameter::LateLineSize, cs::lineSize01 (30.0 + sz * sz * 400.0)); verb.set (Parameter::EarlyDiffuseDelay, juce::jlimit (0.0, 1.0, (15.0 + sz * 70.0 - 10.0) / 90.0)); }
        verb.set (Parameter::LateLineDecay, cs::decay01 (decay));
        // blend: early cloud (CloudSeed's early diffuser) against the late tail, equal power
        const double eg = std::cos (blend * juce::MathConstants<double>::halfPi), lg = std::sin (blend * juce::MathConstants<double>::halfPi);
        verb.set (Parameter::EarlyOut, cs::gainDb01 (20.0 * std::log10 (juce::jmax (1.0e-4, eg))));
        verb.set (Parameter::LateOut, cs::gainDb01 (20.0 * std::log10 (juce::jmax (1.0e-4, lg))));
        // tone: below 0 the in-loop high shelf cuts (darker), above 0 the low shelf cuts (lighter); damping = the in-loop low-pass
        verb.set (Parameter::EqHighGain, cs::eqGain01 (tone < 0.0 ? tone * 14.0 : 0.0));
        verb.set (Parameter::EqLowGain, cs::eqGain01 (tone > 0.0 ? -tone * 14.0 : 0.0));
        verb.set (Parameter::EqCutoff, cs::cutoff01 (20000.0 * std::pow (0.1, damp)));
        verb.set (Parameter::LowCut, cs::lowcut01 (60.0));
        verb.set (Parameter::LateLineModAmount, cs::mod01 (mod * 0.7));
        verb.set (Parameter::LateLineModRate, cs::rate01 (0.25 + mod * 0.9));
        verb.set (Parameter::EarlyDiffuseModAmount, cs::mod01 (mod * 0.36));
        verb.set (Parameter::EarlyDiffuseModRate, cs::rate01 (0.3 + mod * 0.6));
        // glimmer: CloudSeed's late-diffuser modulation, faster and deeper (the shimmering movement inside the tail)
        verb.set (Parameter::LateDiffuseModAmount, cs::mod01 (glim * 0.45));
        verb.set (Parameter::LateDiffuseModRate, cs::rate01 (0.4 + glim * 1.6));
        applied = true;
    }
    // SIZE re-forms the room once the knob has stopped for 100 ms: CloudSeed's delay lines jump to their new lengths (its own design);
    // jumping on every glide step would click (DECISIONS.md 21). Not a modulation target.
    double appliedSize = -1.0, pendingSize = -1.0; int settleCount = 0;
    bool sizeSettled (double want) noexcept
    {
        if (appliedSize < 0.0) { appliedSize = pendingSize = want; return true; }
        if (want != pendingSize) { pendingSize = want; settleCount = 0; return false; }
        if (want == appliedSize) return false;
        if (++settleCount * 8 < (int) (0.1 * sr)) return false;
        appliedSize = want; return true;
    }
    CloudVerb verb; StereoDelay pre; PreGlide glidePre; Ducker duck; bool applied = false;
    int pSize = 0, pBlend = 0, pDecay = 0, pTone = 0, pMod = 0, pGlim = 0, pWidth = 0, pPre = 0, pDuck = 0, pDamp = 0;
};

// ================================================================ 19 SHIMMER: CloudSeed with Signalsmith Stretch shifting pitch in its feedback
class ShimmerFx final : public CatalogEffect
{
public:
    ShimmerFx() : CatalogEffect (uShimmer) {}
    // MODULATION moves the delay lines' modulation depth and rate: it glides over 60 ms
    std::vector<ParamSpec> parameters() const override { auto ps = CatalogEffect::parameters(); for (auto& q : ps) if (q.id == "mod") q.smoothMs = 60.0f; return ps; }
    double ownSpreadHz() const override { return 150.0; }
    // the octave loop turns a test tone into its octaves, and CloudSeed's modulation images; every other control is checked without them
    void zipperIsolate (const std::string& c, std::vector<float>& v) const override { if (c != "shimmer") v[(size_t) id ("shimmer")] = 0.0f; if (c != "mod") v[(size_t) id ("mod")] = 0.0f; }
    bool wetOnly() const override { return true; }
    bool widensOnPurpose() const override { return true; }
    double tailSeconds() const override { return 40.0; }
protected:
    void prepareEffect (double sampleRate, int, int) override
    {
        verb.prepare (sampleRate); cs::base (verb, kLines, kLateDiffuse, kEarlyDiffuse);
        pre.prepare (sampleRate, 0.26); appliedSize = -1.0;
        stretch.presetCheaper (2, (float) sampleRate, true);
        fb.setSize (2, 1024); fb.clear(); shifted.setSize (2, 1024); shifted.clear();
        pSize = id ("size"); pDecay = id ("decay"); pDamp = id ("damp"); pShim = id ("shimmer"); pPre = id ("predelay"); pMod = id ("mod");
        pWidth = id ("width"); pFreeze = id ("freeze"); pInt = id ("interval"); pHold = id ("hold");
        applied = false; lastInterval = -1; frozen = -1;
        fbHp.prepare ({ sampleRate, 1024, 2 }); fbHp.setCutoffFrequency (120.0f); fbHp.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    }
    void resetEffect() override { verb.clear(); pre.reset(); glidePre.cur = -1.0f; stretch.reset(); fb.clear(); shifted.clear(); fbHp.reset(); applied = false; frozen = -1; }
public:
    void process (juce::dsp::AudioBlock<float> block, const ModulationContext& ctx) noexcept override
    {
        beginBlock (ctx);
        const int n = (int) block.getNumSamples(); auto* L = block.getChannelPointer (0); auto* R = block.getNumChannels() > 1 ? block.getChannelPointer (1) : L;
        const int iv = choice (pInt);
        if (iv != lastInterval) { lastInterval = iv; static const float st[] { 12.0f, 7.0f, 19.0f, -12.0f }; stretch.setTransposeSemitones (st[juce::jlimit (0, 3, iv)]); }
        const bool hold = on (pFreeze) || on (pHold);
        if ((int) hold != frozen) { frozen = (int) hold; applied = false; }
        for (int i0 = 0; i0 < n; )
        {
            const bool moving = anyMoving();
            const int len = juce::jmin (moving ? 8 : 64, n - i0);
            for (auto* s : { &p (pDecay), &p (pDamp), &p (pMod) }) s->skip (len);
            if (moving || ! applied) applyEngine (hold);
            float* l = L + i0; float* r = R + i0;
            // the pitch-shifted copy of the reverb's previous output goes back into its input (the shimmer loop)
            {
                float* in[2] { fb.getWritePointer (0), fb.getWritePointer (1) }; float* out[2] { shifted.getWritePointer (0), shifted.getWritePointer (1) };
                stretch.process (in, len, out, len);
                juce::dsp::AudioBlock<float> sb (shifted); auto sub = sb.getSubBlock (0, (size_t) len); fbHp.process (juce::dsp::ProcessContextReplacing<float> (sub));
            }
            for (int i = 0; i < len; ++i)
            {
                const float pd = glidePre (p (pPre).next() * 0.001f * (float) sr), sh = p (pShim).next() * kShimmerLoop, inGain = hold ? 0.0f : 1.0f;
                pre.write (0, l[i]); pre.write (1, r[i]);
                l[i] = pre.read (0, pd + 1.0f) * inGain + softLimit (shifted.getSample (0, i) * sh);
                r[i] = pre.read (1, pd + 1.0f) * inGain + softLimit (shifted.getSample (1, i) * sh);
            }
            verb.process (l, r, len);
            for (int i = 0; i < len; ++i) { fb.setSample (0, i, l[i]); fb.setSample (1, i, r[i]); }
            for (int i = 0; i < len; ++i)
            {
                const float w = p (pWidth).next(), m = 0.5f * (l[i] + r[i]), s = 0.5f * (l[i] - r[i]) * w;
                l[i] = (m + s) * kLevel; r[i] = (m - s) * kLevel;
            }
            i0 += len;
        }
    }
private:
    static constexpr int kLines = 4, kLateDiffuse = 4, kEarlyDiffuse = 6;
    static constexpr float kLevel = 1.317f, kShimmerLoop = 0.55f;   // loop gain at 100 % shimmer: below 1, so the shimmer always dies away unless frozen
    static inline float softLimit (float x) noexcept { return x / (1.0f + std::abs (x)); }
    bool anyMoving() const noexcept { for (int k : { pDecay, pDamp, pMod }) if (smoothers[(size_t) k].isSmoothing()) return true; return target (pSize) != appliedSize; }
    void applyEngine (bool hold) noexcept
    {
        using namespace Cloudseed;
        const double decay = p (pDecay).current, damp = p (pDamp).current, mod = p (pMod).current;
        if (sizeSettled (target (pSize))) { const double sz = appliedSize; verb.set (Parameter::LateLineSize, cs::lineSize01 (40.0 + sz * sz * 500.0)); verb.set (Parameter::EarlyDiffuseDelay, juce::jlimit (0.0, 1.0, (20.0 + sz * 70.0 - 10.0) / 90.0)); }
        if (hold) { verb.setScaled (Parameter::LateLineDecay, 1.0e7); verb.set (Parameter::EqLowpassEnabled, 0.0); verb.set (Parameter::EqHighGain, 1.0); verb.set (Parameter::EqLowGain, 1.0); }
        else
        {
            verb.set (Parameter::LateLineDecay, cs::decay01 (decay)); verb.set (Parameter::EqLowpassEnabled, 1.0);
            verb.set (Parameter::EqHighGain, cs::eqGain01 (-3.0)); verb.set (Parameter::EqLowGain, cs::eqGain01 (-2.0));
            verb.set (Parameter::EqCutoff, cs::cutoff01 (20000.0 * std::pow (0.12, damp)));
        }
        verb.set (Parameter::EarlyOut, cs::gainDb01 (-9.0)); verb.set (Parameter::LateOut, cs::gainDb01 (0.0));
        verb.set (Parameter::LowCut, cs::lowcut01 (80.0));
        verb.set (Parameter::LateLineModAmount, cs::mod01 (mod * 0.5)); verb.set (Parameter::LateLineModRate, cs::rate01 (0.2 + mod * 0.6));
        verb.set (Parameter::EarlyDiffuseModAmount, cs::mod01 (mod * 0.4)); verb.set (Parameter::EarlyDiffuseModRate, cs::rate01 (0.3 + mod * 0.5));
        verb.set (Parameter::LateDiffuseModAmount, cs::mod01 (mod * 0.65)); verb.set (Parameter::LateDiffuseModRate, cs::rate01 (0.3 + mod * 1.0));
        applied = true;
    }
    // SIZE re-forms the room once the knob has stopped for 100 ms: CloudSeed's delay lines jump to their new lengths (its own design);
    // jumping on every glide step would click (DECISIONS.md 21). Not a modulation target.
    double appliedSize = -1.0, pendingSize = -1.0; int settleCount = 0;
    bool sizeSettled (double want) noexcept
    {
        if (appliedSize < 0.0) { appliedSize = pendingSize = want; return true; }
        if (want != pendingSize) { pendingSize = want; settleCount = 0; return false; }
        if (want == appliedSize) return false;
        if (++settleCount * 8 < (int) (0.1 * sr)) return false;
        appliedSize = want; return true;
    }
    CloudVerb verb; StereoDelay pre; PreGlide glidePre; signalsmith::stretch::SignalsmithStretch<float> stretch;
    juce::AudioBuffer<float> fb, shifted; juce::dsp::StateVariableTPTFilter<float> fbHp; bool applied = false; int lastInterval = -1, frozen = -1;
    int pSize = 0, pDecay = 0, pDamp = 0, pShim = 0, pPre = 0, pMod = 0, pWidth = 0, pFreeze = 0, pInt = 0, pHold = 0;
};
} // namespace phantom
