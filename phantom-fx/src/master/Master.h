// SPDX-License-Identifier: AGPL-3.0-or-later
// 23 GHOST SQUEEZE and 24 GHOST LIMITER (Phase 6, Master). Engines: src/master/Engines.h; choices: DECISIONS.md 35-41.
#pragma once
#include "../effects/ColourSst.h"
#include "Engines.h"

// Which GHOST LIMITER engine the plugin uses (the brief's challenger procedure: the other stays in the repo, built, behind this flag).
// 0 = LSP (the brief's baseline), 1 = ZL Limiter (the challenger). Set by CMake's PHANTOM_LIMITER_ENGINE.
#ifndef PHANTOM_LIMITER_ZL
 #define PHANTOM_LIMITER_ZL 1
#endif

namespace phantom
{
// ================================================================ 23 GHOST SQUEEZE: vitOTTx's 3-band OTT (Vital's MultibandCompressor)
class GhostSqueezeFx final : public CatalogEffect
{
public:
    GhostSqueezeFx() : CatalogEffect (uOtt) {}
    bool isNonlinear() const override { return true; }
    int oversamplingFactor() const override { return 2; }
    bool changesLevelOnPurpose() const override { return true; }
    bool neutralAtMixZero() const override { return true; }   // DEPTH (the slot's mix) at 0 % is its 'does nothing' setting
    // the aliasing check reads it at its slowest TIME: at fast times its envelope ripple makes low products (30-120 Hz) that do not change
    // with the oversampling factor (they are there with oversampling off too), so they are its sound, not folding (DECISIONS.md 36)
    void aliasIsolate (std::vector<float>& v) const override { v[(size_t) id ("time")] = 1.0f; }
protected:
    void prepareEffect (double sampleRate, int, int) override
    {
        iTime = id ("time"); iUp = id ("up"); iDown = id ("down"); iIn = id ("in"); iOut = id ("out");
        iLo = id ("lowgain"); iMid = id ("midgain"); iHi = id ("highgain"); iXlo = id ("xlo"); iXhi = id ("xhi");
        box.prepare (sampleRate);
    }
    void resetEffect() override { box.reset(); }
    void process (juce::dsp::AudioBlock<float> block, const ModulationContext& ctx) noexcept override
    {
        beginBlock (ctx);
        const int n = (int) block.getNumSamples(), nch = (int) block.getNumChannels();
        float* L = block.getChannelPointer (0); float* R = nch > 1 ? block.getChannelPointer (1) : L;
        for (int off = 0; off < n; off += kChunk)
        {
            const int m = std::min (kChunk, n - off);
            SqueezeSettings s;
            s.attack = s.release = p (iTime).skip (m);
            s.upward = 2.0f * p (iUp).skip (m); s.downward = 2.0f * p (iDown).skip (m);   // 50 % = vitOTTx's own setting
            s.lowGainDb = 16.3f + p (iLo).skip (m); s.midGainDb = 11.7f + p (iMid).skip (m); s.highGainDb = 16.3f + p (iHi).skip (m);
            s.lowHz = p (iXlo).skip (m); s.highHz = p (iXhi).skip (m);
            box.set (s);
            for (int i = off; i < off + m; ++i) { const float g = juce::Decibels::decibelsToGain (p (iIn).next()); L[i] *= g; if (nch > 1) R[i] *= g; }
            if (nch > 1) box.process (L + off, R + off, m); else { std::copy (L + off, L + off + m, mono.begin()); box.process (L + off, mono.data(), m); }
            for (int i = off; i < off + m; ++i) { const float g = juce::Decibels::decibelsToGain (p (iOut).next()); L[i] *= g; if (nch > 1) R[i] *= g; }
        }
    }
private:
    static constexpr int kChunk = 32;
    SqueezeBox box; std::array<float, kChunk> mono {};
    int iTime = 0, iUp = 1, iDown = 2, iIn = 3, iOut = 4, iLo = 5, iMid = 6, iHi = 7, iXlo = 8, iXhi = 9;
};

// ================================================================ 24 GHOST LIMITER: true-peak lookahead limiter, its own oversampling
class GhostLimiterFx final : public CatalogEffect
{
public:
    // engine: -1 = the build's choice (PHANTOM_LIMITER_ZL), 0 = LSP, 1 = ZL (the challenger tests build both)
    explicit GhostLimiterFx (int engine = -1) : CatalogEffect (uLim)
    {
        const int e = engine < 0 ? PHANTOM_LIMITER_ZL : engine;
        eng = e == 1 ? makeZlLimiter() : makeLspLimiter(); zl = e == 1;
    }
    // the ceiling glides over 60 ms, as the drive controls of the other nonlinear units do (a moving ceiling is a moving gain)
    std::vector<ParamSpec> parameters() const override { auto ps = CatalogEffect::parameters(); for (auto& q : ps) if (q.id == "ceiling") q.smoothMs = 60.0f; return ps; }
    bool isNonlinear() const override { return true; }
    int oversamplingFactor() const override { return 1; }     // it oversamples itself (QUALITY), so the rack never does
    bool changesLevelOnPurpose() const override { return true; }
    void neutralise (std::vector<float>& v) const override
    { v[(size_t) id ("ceiling")] = 0.0f; v[(size_t) id ("gain")] = 0.0f; v[(size_t) id ("truepeak")] = 0.0f; v[(size_t) id ("quality")] = 0.0f; }
    int getLatencySamples() const override { return prepared ? eng->latencyFor (os.load()) : 0; }
    // after waking, its delay lines and oversampling filters start empty: the slot plays the dry until they are full, then fades in
    int warmupSamples() const override { return getLatencySamples() + 64; }
    // its 'does nothing' case is a signal under the ceiling: the null test plays the noise 12 dB down
    float nullTestGain() const override { return 0.25f; }
    void updateParameters (const ModulationContext& ctx) override { CatalogEffect::updateParameters (ctx); os.store (choice (iOs)); }
    const char* engineName() const { return zl ? "ZL" : "LSP"; }
    std::atomic<float> uiReduction { 0.0f };   // dB of gain reduction in the last block (meter)
    const std::atomic<float>* reductionMeter() const override { return &uiReduction; }
protected:
    void prepareEffect (double sampleRate, int maxBlockSize, int) override
    {
        iCeil = id ("ceiling"); iGain = id ("gain"); iRel = id ("release"); iAdapt = id ("adaptive"); iLook = id ("lookahead"); iTp = id ("truepeak"); iOs = id ("quality");
        os.store (choice (iOs));   // the latency asked before the first block is the one the first block plays with
        eng->prepare (sampleRate, maxBlockSize); prepared = true; mono.assign ((size_t) juce::jmax (1, maxBlockSize), 0.0f);
        pushSettings (target (iCeil));
    }
    void resetEffect() override { eng->reset(); }
    void process (juce::dsp::AudioBlock<float> block, const ModulationContext& ctx) noexcept override
    {
        beginBlock (ctx);
        const int n = (int) block.getNumSamples(), nch = (int) block.getNumChannels();
        float* L = block.getChannelPointer (0); float* R = nch > 1 ? block.getChannelPointer (1) : mono.data();
        if (nch == 1) std::copy (L, L + n, mono.begin());
        float inPk = 0.0f;
        for (int i = 0; i < n; ++i) { const float g = juce::Decibels::decibelsToGain (p (iGain).next()); L[i] *= g; R[i] *= g; inPk = std::max (inPk, std::max (std::abs (L[i]), std::abs (R[i]))); }
        // the ceiling glides (it is set every 32 samples from its smoother: no stepped gain when it is moved or automated)
        for (int off = 0; off < n; off += kChunk)
        {
            const int m = std::min (kChunk, n - off);
            pushSettings (p (iCeil).skip (m));
            eng->process (L + off, R + off, m);
        }
        float outPk = 0.0f; for (int i = 0; i < n; ++i) outPk = std::max (outPk, std::max (std::abs (L[i]), std::abs (R[i])));
        uiReduction.store (inPk > 1.0e-6f ? juce::jmax (0.0f, juce::Decibels::gainToDecibels (inPk) - juce::Decibels::gainToDecibels (outPk + 1.0e-9f)) : 0.0f, std::memory_order_relaxed);
    }
private:
    static constexpr int kChunk = 16;
    void pushSettings (float ceilingDb) noexcept
    {
        LimiterSettings s; s.ceilingDb = ceilingDb; s.releaseMs = target (iRel); s.lookaheadMs = target (iLook);
        s.adaptive = on (iAdapt); s.truePeak = on (iTp); s.oversampling = choice (iOs); os.store (s.oversampling);
        eng->set (s);
    }
    std::unique_ptr<LimiterEngine> eng; bool zl = false, prepared = false; std::vector<float> mono; std::atomic<int> os { 1 };
    int iCeil = 0, iGain = 1, iRel = 2, iAdapt = 3, iLook = 4, iTp = 5, iOs = 6;
};
} // namespace phantom
