// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX rack: one slot = one effect module plus everything the interface promises around it:
// parameter feed (host value + modulation), the oversampling policy, click-free bypass with latency kept,
// dry/wet with a latency-matched dry path, and the per-effect output trim. Audio thread: no allocation, no locks.
#pragma once
#include <atomic>
#include "../core/PhantomEffect.h"
#include "../dsp/Oversampler.h"
#include "../dsp/DelayLine.h"
#include "../dsp/Smoothing.h"
#include <memory>

namespace phantom
{
class EffectSlot
{
public:
    explicit EffectSlot (std::unique_ptr<PhantomEffect> fx) : effect (std::move (fx)) { specs = effect->parameters(); values.assign (specs.size(), 0.0f); for (size_t i = 0; i < specs.size(); ++i) values[i] = specs[i].def; }

    PhantomEffect& fx() noexcept { return *effect; }
    const std::vector<ParamSpec>& params() const noexcept { return specs; }

    void prepare (double sampleRate, int maxBlockSize, int numChannels, int maxLatencySamples = 1 << 16)
    {
        sr = sampleRate; maxBlock = maxBlockSize; channels = numChannels;
        os.prepare (sampleRate, maxBlockSize, numChannels);
        effect->prepare (sampleRate * (double) requestedFactor(), maxBlockSize * requestedFactor(), numChannels);
        dryDelay.prepare (maxLatencySamples, numChannels); padDelay.prepare (64, numChannels);
        dryBuf.setSize (numChannels, maxBlockSize);
        mixSmoother.prepare (sampleRate, 20.0f, mixTarget, false);
        trimSmoother.prepare (sampleRate, 20.0f, trimTarget, false);
        onFade.prepare (sampleRate, 10.0f, enabledTarget ? 1.0f : 0.0f, false);   // 10 ms on/off crossfade (click-free bypass)
        applyPolicy();
        reset();
    }
    void reset() noexcept { effect->reset(); os.reset(); dryDelay.reset(); padDelay.reset(); dryBuf.clear(); primed = false; sleeping = false; warmLeft = 0; tailQuiet = 0; tailAsleep = false; tailSecs = effect->tailSeconds(); }
    bool isTailAsleep() const noexcept { return tailAsleep; }

    // ---- what the rack / host sets (message or audio thread; plain stores)
    void setValue (int i, float v) noexcept { if (i >= 0 && i < (int) values.size()) values[(size_t) i] = v; }
    void setEnabled (bool on) noexcept { enabledTarget = on; onFade.setTarget (on ? 1.0f : 0.0f); }
    void setMix (float m) noexcept { mixTarget = m; mixSmoother.setTarget (m); }
    void setTrimDb (float dB) noexcept { trimTarget = dB; trimSmoother.setTarget (dB); }
    void setPolicy (OversamplingPolicy p) noexcept { policy = p; }   // takes effect at the next prepare / topology change
    bool isEnabled() const noexcept { return enabledTarget; }
    // the latency-aligned dry of the last processed block (SPATIAL takes the unit's wet as output minus this)
    juce::dsp::AudioBlock<float> alignedDry (int n) noexcept { juce::dsp::AudioBlock<float> d (dryBuf); return d.getSubBlock (0, (size_t) n); }
    // SPATIAL: while set, the next process() also writes the unit's wet part (trim x effect x mix x on) here (stereo only); else nullptr
    void setWetTap (float* l, float* r) noexcept { tapL = l; tapR = r; }

    // the policy is applied here (prepare or a topology change on the message thread), never mid-block
    void applyPolicy()
    {
        const int f = effect->isNonlinear() ? juce::jmin (policyFactor (policy), juce::jmax (1, effect->oversamplingFactor())) : 1;
        os.set (f, policy == OversamplingPolicy::Render);
        if (f != currentFactor) { currentFactor = f; effect->prepare (sr * f, maxBlock * f, channels); effect->reset(); }
        { ModulationContext c; c.values = values.data(); c.numValues = (int) values.size(); c.sampleRate = sr * currentFactor; c.oversamplingFactor = currentFactor; effect->updateParameters (c); }
        // the module's latency (at the processing rate) plus the oversampler's (a multiple of 1/factor host samples: its later half-band
        // stages run at the higher rates) is padded at the processing rate up to a whole number of host samples, so the reported latency
        // is exact to the sample whatever the factor and the module
        const int procLat = juce::jmax (0, effect->getLatencySamples()) + (int) std::lround (os.latencySamples() * (float) currentFactor);
        const int hostLat = (procLat + currentFactor - 1) / currentFactor;             // whole host samples
        padDelay.setDelay (hostLat * currentFactor - procLat);
        latency = hostLat;
        dryDelay.setDelay (latency);
    }
    int getLatencySamples() const noexcept { return latency; }

    // ---- audio
    // the unit's level meter for the editor (the painted looks' ghosts bounce to it). Measured only while an editor asks for it
    // (uiLevelWanted): with no painted editor open this is one relaxed load per block and the audio path is exactly as before.
    std::atomic<float> uiLevel { 0.0f }; std::atomic<bool> uiLevelWanted { false };
    // the same for the unit's visual on its page (0.1.11): the last block's peak, written only while that visual shows
    std::atomic<float> visLevel { 0.0f }; std::atomic<bool> visWanted { false };
    void process (juce::dsp::AudioBlock<float> block, const TempoInfo& tempo) noexcept
    {
        processInner (block, tempo);
        if (uiLevelWanted.load (std::memory_order_relaxed)) uiLevel.store (juce::jmax (uiLevel.load (std::memory_order_relaxed), peakOfBlock (block)), std::memory_order_relaxed);
        if (visWanted.load (std::memory_order_relaxed)) visLevel.store (peakOfBlock (block), std::memory_order_relaxed);
    }
    void processInner (juce::dsp::AudioBlock<float> block, const TempoInfo& tempo) noexcept
    {
        const int n = (int) block.getNumSamples(), ch = (int) block.getNumChannels();
        if (tapL != nullptr) { juce::FloatVectorOperations::clear (tapL, n); juce::FloatVectorOperations::clear (tapR, n); }
        if (! primed) { onFade.snap (enabledTarget ? 1.0f : 0.0f); mixSmoother.snap (mixTarget); trimSmoother.snap (trimTarget); primed = true; }   // a fresh instance starts where the host put it, no fade-in from the wrong state
        // keep the dry copy, delayed by the effect's latency so dry and wet line up
        juce::dsp::AudioBlock<float> dry (dryBuf); dry = dry.getSubBlock (0, (size_t) n);
        dry.copyFrom (block);
        dryDelay.process (dry);

        ModulationContext ctx;
        ctx.values = values.data(); ctx.numValues = (int) values.size(); ctx.tempo = tempo;
        ctx.bypassed = ! enabledTarget && ! onFade.isSmoothing();
        ctx.oversamplingFactor = currentFactor; ctx.sampleRate = sr * currentFactor;
        effect->tempo (tempo);

        // switched off and faded out: the effect sleeps (no processing at all) and the slot passes the latency-matched dry copy.
        // On waking it starts from a clean state and fades in over 10 ms, as any switch-on does.
        if (ctx.bypassed && onFade.current <= 0.0f)
        {
            block.copyFrom (dry); sleeping = true; return;
        }
        bool warming = false;
        if (sleeping) { effect->reset(); os.reset(); padDelay.reset(); sleeping = false; warming = true; warmLeft = effect->warmupSamples(); }
        else if (warmLeft > 0) warming = true;

        // tail sleep (effects with a tail: reverbs, echoes, spectral): once the input has been silent and the effect's own output has
        // fallen below -120 dBFS for half a second, the effect stops computing and its wet output is silence, until the input moves again.
        // A frozen / held effect never gets there (its output stays loud), so it keeps running.
        const bool hasTail = tailSecs > 0.0;
        if (hasTail)
        {
            const float inPeak = dry.getNumChannels() > 0 ? peakOfBlock (block) : 0.0f;
            if (inPeak > kSilence) { tailQuiet = 0; tailAsleep = false; }
            if (tailAsleep) { block.clear(); goto mixOut; }
        }
        if (currentFactor > 1) { auto up = os.up (block); effect->process (up, ctx); padDelay.process (up); os.down (block); }
        else { effect->process (block, ctx); padDelay.process (block); }
        if (hasTail && ! warming)
        {
            if (peakOfBlock (block) < kSilence && peakOfBlock (dry) < kSilence) { tailQuiet += n; if (tailQuiet > (int) (sr * 0.5)) tailAsleep = true; }
            else tailQuiet = 0;
        }

        // a waking effect runs one block on the real signal before it is heard (its filters settle), then fades in
        if (warming) { warmLeft = juce::jmax (0, warmLeft - n); block.copyFrom (dry); return; }
    mixOut:
        // wet = trim * effect; out = dry + (wet - dry) * mix * on
        if (tapL != nullptr && ch == 2)
        {   // the same arithmetic, plus the wet part on its own for SPATIAL
            for (int i = 0; i < n; ++i)
            {
                const float on = onFade.next(), mix = mixSmoother.next() * on, tdb = trimSmoother.next();
                if (tdb != lastTrimDb) { lastTrimDb = tdb; trimGain = dbToGain (tdb); }
                const float g = trimGain;
                auto* wl = block.getChannelPointer (0); auto* wr = block.getChannelPointer (1);
                const float dl = dry.getChannelPointer (0)[i], dr = dry.getChannelPointer (1)[i];
                tapL[i] = wl[i] * g * mix; tapR[i] = wr[i] * g * mix;
                wl[i] = dl + (wl[i] * g - dl) * mix; wr[i] = dr + (wr[i] * g - dr) * mix;
            }
            return;
        }
        for (int i = 0; i < n; ++i)
        {
            const float on = onFade.next(), mix = mixSmoother.next() * on, tdb = trimSmoother.next();
            if (tdb != lastTrimDb) { lastTrimDb = tdb; trimGain = dbToGain (tdb); }
            const float g = trimGain;
            for (int c = 0; c < ch; ++c)
            {
                auto* w = block.getChannelPointer ((size_t) c); const float d = dry.getChannelPointer ((size_t) c)[i];
                w[i] = d + (w[i] * g - d) * mix;
            }
        }
    }

private:
    int requestedFactor() const noexcept { return effect->isNonlinear() ? juce::jmin (policyFactor (policy), juce::jmax (1, effect->oversamplingFactor())) : 1; }

    std::unique_ptr<PhantomEffect> effect;
    std::vector<ParamSpec> specs; std::vector<float> values;
    Oversampler os; OversamplingPolicy policy = OversamplingPolicy::X4; int currentFactor = 1;
    IntegerDelay dryDelay, padDelay; juce::AudioBuffer<float> dryBuf;
    ParamSmoother mixSmoother, trimSmoother, onFade;
    float mixTarget = 1.0f, trimTarget = 0.0f, lastTrimDb = 0.0f, trimGain = 1.0f; bool enabledTarget = false, primed = false, sleeping = false;
    double sr = 48000.0; int maxBlock = 0, channels = 2, latency = 0;
    static constexpr float kSilence = 1.0e-6f;   // -120 dBFS
    int warmLeft = 0; float* tapL = nullptr; float* tapR = nullptr;
    double tailSecs = 0.0; int tailQuiet = 0; bool tailAsleep = false;
    static float peakOfBlock (const juce::dsp::AudioBlock<float>& b) noexcept
    { float pk = 0.0f; for (size_t c = 0; c < b.getNumChannels(); ++c) { auto r = juce::FloatVectorOperations::findMinAndMax (b.getChannelPointer (c), (int) b.getNumSamples()); pk = juce::jmax (pk, std::abs (r.getStart()), std::abs (r.getEnd())); } return pk; }
};
} // namespace phantom
