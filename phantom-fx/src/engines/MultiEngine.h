// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX: an effect with an ENGINE menu (DECISIONS.md 85). Entry 0 is the effect's own module ("PHANTOM"); the others are alternative
// engines (AltEngine) with one of their voices each. Only the chosen engine runs (the CPU limit holds: one engine per effect at a time);
// a change of engine crossfades over 30 ms, the old one fading out on a copy of the same input, and an engine that comes back starts
// from a cleared state. Everything the rack asks of the effect (wet-only, oversampling, the test hooks) is the PHANTOM module's.
#pragma once
#include "AltEngine.h"
#include <atomic>

namespace phantom
{
class MultiEngineFx final : public PhantomEffect
{
public:
    // engineParam: the id of the ENGINE choice among the effect's parameters (the last one by convention)
    MultiEngineFx (std::unique_ptr<PhantomEffect> own, std::vector<std::unique_ptr<engines::AltEngine>> alts, std::vector<engines::Entry> menu, int defaultEntry = 0)
        : base (std::move (own)), engines (std::move (alts)), entries (std::move (menu)), defEntry (defaultEntry)
    {
        const auto ps = base->parameters();
        for (size_t i = 0; i < ps.size(); ++i) index[ps[i].id] = (int) i;
        engineIdx = index.count ("engine") ? index["engine"] : -1;
    }
    const char* key() const override { return base->key(); }
    const char* title() const override { return base->title(); }
    std::vector<ParamSpec> parameters() const override
    {
        auto ps = base->parameters(); if (engineIdx >= 0) ps[(size_t) engineIdx].def = (float) defEntry; return ps;   // (a test build may start on another entry)
    }
    void prepare (double sampleRate, int maxBlockSize, int numChannels) override
    {
        base->prepare (sampleRate, maxBlockSize, numChannels); sr = sampleRate;
        for (auto& e : engines) e->prepare (sampleRate, maxBlockSize);
        const int mb = juce::jmax (maxBlockSize, 64); fadeBuf.setSize (2, mb); inBuf.setSize (2, mb);
        cur = defEntry; prev = -1; xf = 1.0f; lastGain = gainOf (cur);
    }
    void reset() override { base->reset(); for (auto& e : engines) e->reset(); prev = -1; xf = 1.0f; }
    void process (juce::dsp::AudioBlock<float> block, const ModulationContext& ctx) noexcept override
    {
        const int n = (int) block.getNumSamples(); auto* L = block.getChannelPointer (0); auto* R = block.getNumChannels() > 1 ? block.getChannelPointer (1) : L;
        const int want = entryOf (ctx);
        if (want != cur)
        {   // a new engine: the one playing fades out from here (if a fade is already running, the oldest is dropped)
            prev = cur; cur = want; xf = 0.0f; wake (cur);
            // two engines with different latencies would overlap out of time (a tone can sum up to 3 dB louder): then the old one fades
            // out before the new one fades in (a short dip, no doubling); with the same latency, the usual equal-power crossfade
            serial = latencyOf (prev) != latencyOf (cur);
        }
        if (prev >= 0)
        {
            for (int c = 0; c < 2; ++c) inBuf.copyFrom (c, 0, c == 0 ? L : R, n);
            run (prev, fadeBuf.getWritePointer (0), fadeBuf.getWritePointer (1), inBuf, block, ctx, n, true);
        }
        run (cur, L, R, inBuf, block, ctx, n, false);
        if (prev >= 0)
        {
            const float step = 1.0f / (0.03f * (float) sr); const float* oL = fadeBuf.getReadPointer (0); const float* oR = fadeBuf.getReadPointer (1);
            for (int i = 0; i < n; ++i)
            {
                xf = juce::jmin (1.0f, xf + step);
                float a, b;
                if (serial) { const float o = juce::jmax (0.0f, 1.0f - 2.0f * xf), q = juce::jmax (0.0f, 2.0f * xf - 1.0f); a = std::sin (o * juce::MathConstants<float>::halfPi); b = std::sin (q * juce::MathConstants<float>::halfPi); }
                else { a = std::cos (xf * juce::MathConstants<float>::halfPi); b = std::sin (xf * juce::MathConstants<float>::halfPi); }
                L[i] = L[i] * b + oL[i] * a; if (R != L) R[i] = R[i] * b + oR[i] * a;
            }
            if (xf >= 1.0f) prev = -1;
        }
    }
    int latencyOf (int e) const noexcept { const auto& en = entries[(size_t) juce::jlimit (0, (int) entries.size() - 1, e)]; return en.engine < 0 ? base->getLatencySamples() : engines[(size_t) en.engine]->latencySamples (en.voice); }
    int getLatencySamples() const override { const auto& e = entries[(size_t) juce::jlimit (0, (int) entries.size() - 1, uiEntry.load())]; return e.engine < 0 ? base->getLatencySamples() : engines[(size_t) e.engine]->latencySamples (e.voice); }
    void updateParameters (const ModulationContext& ctx) override
    {
        base->updateParameters (ctx); const int e = entryOf (ctx); uiEntry.store (e);
        const auto& en = entries[(size_t) e]; if (en.engine >= 0) { engines::Controls c { ctx.values, ctx.numValues, &index }; engines[(size_t) en.engine]->update (c, en.voice); }
    }

    StereoMode stereoMode() const override { return base->stereoMode(); }
    // an engine that does not want the rack's oversampling (a spectral one: its STFT is made for the host rate) turns it off for its entries
    bool isNonlinear() const override
    {
        const auto& e = entries[(size_t) juce::jlimit (0, (int) entries.size() - 1, uiEntry.load())];
        return base->isNonlinear() && (e.engine < 0 || engines[(size_t) e.engine]->wantsOversampling());
    }
    std::string byDesign (const std::string& check) const override
    {
        const auto& e = entries[(size_t) juce::jlimit (0, (int) entries.size() - 1, uiEntry.load())];
        return e.engine < 0 ? base->byDesign (check) : engines[(size_t) e.engine]->byDesign (check, e.voice);
    }
    int oversamplingFactor() const override { return base->oversamplingFactor(); }
    bool isGenerator() const override { return base->isGenerator(); }
    bool hasNullSetting() const override { return base->hasNullSetting(); }
    void neutralise (std::vector<float>& v) const override { base->neutralise (v); }
    void zipperIsolate (const std::string& c, std::vector<float>& v) const override { base->zipperIsolate (c, v); }
    void aliasIsolate (std::vector<float>& v) const override { base->aliasIsolate (v); }
    bool changesLevelOnPurpose() const override { return base->changesLevelOnPurpose(); }
    bool isRhythmic() const override { return base->isRhythmic(); }
    bool wetOnly() const override { return base->wetOnly(); }
    bool neutralAtMixZero() const override { return base->neutralAtMixZero(); }
    bool widensOnPurpose() const override { return base->widensOnPurpose(); }
    double ownSpreadHz() const override { return base->ownSpreadHz(); }
    bool zipperEventControl (const std::string& c) const override
    {   // the chosen engine's own answer when it has one (QDELAY's TIME glides its delay like tape, by design: DECISIONS.md 93)
        const auto& en = entries[(size_t) juce::jlimit (0, (int) entries.size() - 1, uiEntry.load())];
        return en.engine >= 0 ? engines[(size_t) en.engine]->zipperEventControl (c, en.voice) : base->zipperEventControl (c);
    }
    double tailSeconds() const override { return juce::jmax (base->tailSeconds(), altTail); }
    void setAltTail (double s) noexcept { altTail = s; }
    int warmupSamples() const override { return base->warmupSamples(); }
    float nullTestGain() const override { return base->nullTestGain(); }
    void tempo (const TempoInfo& t) override { base->tempo (t); }
    AnalyserTap* analyserTap() override { return base->analyserTap(); }
    const std::atomic<float>* reductionMeter() const override { return base->reductionMeter(); }
    // the project's non-parameter state: the PHANTOM module's own and an extra engine's (a drawn curve), side by side when both have one
    bool hasData() const override { return base->hasData() || dataEngine() >= 0; }
    juce::var getData() const override
    {
        const int k = dataEngine(); if (k < 0) return base->getData(); if (! base->hasData()) return engines[(size_t) k]->getData();
        auto* o = new juce::DynamicObject(); o->setProperty ("base", base->getData()); o->setProperty ("engine", engines[(size_t) k]->getData()); return juce::var (o);
    }
    void setData (const juce::var& d) override
    {
        const int k = dataEngine(); if (k < 0) { base->setData (d); return; }
        if (! base->hasData()) { engines[(size_t) k]->setData (d); return; }
        base->setData (d.isVoid() ? d : d["base"]); engines[(size_t) k]->setData (d.isVoid() ? d : d["engine"]);
    }
    engines::AltEngine* engine (int k) noexcept { return k >= 0 && k < (int) engines.size() ? engines[(size_t) k].get() : nullptr; }

    PhantomEffect& own() noexcept { return *base; }
    const std::vector<engines::Entry>& menu() const noexcept { return entries; }
private:
    int dataEngine() const { for (size_t k = 0; k < engines.size(); ++k) if (engines[k]->hasData()) return (int) k; return -1; }
    int entryOf (const ModulationContext& ctx) const noexcept { return engineIdx < 0 ? 0 : juce::jlimit (0, (int) entries.size() - 1, (int) std::lround (ctx.value (engineIdx))); }
    float gainOf (int e) const noexcept { return entries[(size_t) e].gain; }
    void wake (int e) noexcept
    {
        const auto& en = entries[(size_t) e];
        if (en.engine >= 0) { bool busy = false; for (int k : { prev }) if (k >= 0 && entries[(size_t) k].engine == en.engine) busy = true; if (! busy) engines[(size_t) en.engine]->reset(); }
        else if (prev < 0 || entries[(size_t) prev].engine >= 0) base->reset();
    }
    // runs entry e on L / R (when 'copy', on the saved input copied into L / R first)
    void run (int e, float* L, float* R, juce::AudioBuffer<float>& in, juce::dsp::AudioBlock<float> block, const ModulationContext& ctx, int n, bool copy) noexcept
    {
        const auto& en = entries[(size_t) e];
        if (copy) { std::copy (in.getReadPointer (0), in.getReadPointer (0) + n, L); std::copy (in.getReadPointer (1), in.getReadPointer (1) + n, R); }
        if (en.engine < 0)
        {
            if (copy) { float* ch[2] { L, R }; juce::dsp::AudioBlock<float> b (ch, 2, (size_t) n); base->process (b, ctx); }
            else base->process (block, ctx);
        }
        else
        {
            engines::Controls c { ctx.values, ctx.numValues, &index }; auto& eng = *engines[(size_t) en.engine];
            if (eng.appliesLevel()) eng.setLevel (en.gain);
            eng.process (L, R, n, c, en.voice, ctx.tempo);
            const float g = en.gain; if (g != 1.0f && ! eng.appliesLevel()) for (int i = 0; i < n; ++i) { L[i] *= g; R[i] *= g; }
        }
    }
    std::unique_ptr<PhantomEffect> base; std::vector<std::unique_ptr<engines::AltEngine>> engines; std::vector<engines::Entry> entries; int defEntry = 0;
    std::map<std::string, int> index; int engineIdx = -1; double altTail = 0.0;
    double sr = 48000.0; int cur = 0, prev = -1; float xf = 1.0f, lastGain = 1.0f; std::atomic<int> uiEntry { 0 };
    juce::AudioBuffer<float> fadeBuf, inBuf; bool serial = false;
};
} // namespace phantom
