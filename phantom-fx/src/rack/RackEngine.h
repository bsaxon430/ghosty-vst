// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX rack engine: 24 slots, a serial main chain plus up to 3 parallel lanes (each with gain, pan, mute), latency-compensated
// summing, the limiter always last, topology changes through a short dry crossfade, the modulator pool and its routing.
// Audio thread: no allocation, no locks. Topology / routing edits happen on the message thread under the processor's callback lock.
#pragma once
#include "EffectSlot.h"
#include "../effects/Catalog.h"
#include "../dsp/Modulators.h"
#include "../spatial/SpatialEngine.h"

namespace phantom
{
constexpr int kLanes = 4;            // lane 0 = the main chain; 1..3 parallel
constexpr int kNumLfos = 4, kNumSteps = 2, kNumEnvs = 2, kNumMacros = 8;
constexpr int kNumModSources = kNumLfos + kNumSteps + kNumEnvs + kNumMacros;   // 16
constexpr int kMaxRoutes = 96;

struct Topology
{
    std::array<int, kNumUnits> order {};   // processing order (unit indices); the limiter is forced last whatever this says
    std::array<int, kNumUnits> lane {};    // lane per unit, 0..3
    Topology() { for (int i = 0; i < kNumUnits; ++i) { order[(size_t) i] = i; lane[(size_t) i] = 0; } }
    bool operator== (const Topology& o) const noexcept { return order == o.order && lane == o.lane; }
};

struct ModRoute { int source = 0; int unit = 0; int param = 0; float depth = 0.0f; bool bipolar = true; };
inline const char* modSourceName (int s)
{
    static const char* n[kNumModSources] { "LFO 1", "LFO 2", "LFO 3", "LFO 4", "STEP 1", "STEP 2", "ENV 1", "ENV 2", "MACRO 1", "MACRO 2", "MACRO 3", "MACRO 4", "MACRO 5", "MACRO 6", "MACRO 7", "MACRO 8" };
    return n[juce::jlimit (0, kNumModSources - 1, s)];
}

class RackEngine
{
public:
    RackEngine()
    {
        for (int u = 0; u < kNumUnits; ++u) slots.push_back (std::make_unique<EffectSlot> (makeEffect (u)));
        for (auto& l : lfos) l.setSync (true, 9);
    }

    EffectSlot& slot (int u) noexcept { return *slots[(size_t) u]; }
    void setUiLevels (bool wanted) noexcept { for (auto& sl : slots) if (sl) sl->uiLevelWanted.store (wanted, std::memory_order_relaxed); }   // the editor's per-unit level meters (off unless a painted look is showing)
    const EffectSlot& slot (int u) const noexcept { return *slots[(size_t) u]; }
    int numSlots() const noexcept { return (int) slots.size(); }

    void prepare (double sampleRate, int maxBlockSize, int numChannels, OversamplingPolicy policy)
    {
        sr = sampleRate; maxBlock = maxBlockSize; channels = numChannels;
        for (auto& s : slots) { s->setPolicy (policy); s->prepare (sampleRate, maxBlockSize, numChannels); }
        for (int k = 0; k < kLanes; ++k) { laneBuf[(size_t) k].setSize (numChannels, maxBlockSize); laneDelay[(size_t) k].prepare (1 << 16, numChannels); laneGain[(size_t) k].prepare (sampleRate, 20.0f, 0.0f, false); lanePan[(size_t) k].prepare (sampleRate, 20.0f, 0.0f, false); laneMute[(size_t) k].prepare (sampleRate, 10.0f, 1.0f, false); }
        dryBuf.setSize (numChannels, maxBlockSize); dryDelay.prepare (1 << 16, numChannels);
        topoFade.prepare (sampleRate, 10.0f, 1.0f, false);
        if (numChannels == 2) spatial.prepare (sampleRate, maxBlockSize);
        for (auto& l : lfos) l.prepare (sampleRate);
        for (auto& s : steps) s.prepare (sampleRate);
        for (auto& e : envs) { e.prepare (sampleRate); e.setTimes (5.0f, 120.0f); }
        applyTopology();
    }
    void reset() noexcept { for (auto& s : slots) s->reset(); for (auto& d : laneDelay) d.reset(); dryDelay.reset(); for (auto& l : lfos) l.reset(); for (auto& s : steps) s.reset(); for (auto& e : envs) e.reset(); }

    // ---- topology (message thread, under the callback lock)
    const Topology& topology() const noexcept { return topo; }
    void setTopology (const Topology& t) { pending = t; pendingSet = true; }   // takes effect through a 10 ms dry dip + swap + 10 ms return
    void setPolicy (OversamplingPolicy p) { for (auto& s : slots) s->setPolicy (p); }
    void applyTopology()                                                   // call after setTopology / policy / latency-changing param (under the lock)
    {
        if (pendingSet) { topo = pending; pendingSet = false; }
        if (maxBlock == 0) return;   // not prepared yet (a host may restore a project before prepareToPlay): prepare() applies it all
        for (auto& s : slots) s->applyPolicy();
        for (int k = 0; k < kLanes; ++k) { laneLat[(size_t) k] = 0; laneUsed[(size_t) k] = false; }
        for (int u = 0; u < kNumUnits; ++u)
        {
            if (u == uLim) continue;
            const int l = juce::jlimit (0, kLanes - 1, topo.lane[(size_t) u]);
            laneUsed[(size_t) l] = true; laneLat[(size_t) l] += slots[(size_t) u]->getLatencySamples();
        }
        laneUsed[0] = true;
        int maxLat = 0; for (int k = 0; k < kLanes; ++k) if (laneUsed[(size_t) k]) maxLat = juce::jmax (maxLat, laneLat[(size_t) k]);
        for (int k = 0; k < kLanes; ++k) laneDelay[(size_t) k].setDelay (laneUsed[(size_t) k] ? maxLat - laneLat[(size_t) k] : 0);
        // SPATIAL: each unit's wet joins at the end of the chain (before the limiter), so it waits for the rest of its lane + the lane's alignment
        const bool wasSpatial = spatialActive;
        spatialActive = spatialRequested.load() && channels == 2 && spatial.ready();
        if (spatialActive)
        {
            std::array<int, kLanes> cum {};
            for (int k = 0; k < kNumUnits; ++k)
            {
                const int u = topo.order[(size_t) k]; if (u == uLim) continue;
                const int l = juce::jlimit (0, kLanes - 1, topo.lane[(size_t) u]);
                cum[(size_t) l] += slots[(size_t) u]->getLatencySamples();
                spatial.setBusDelay (u, maxLat - cum[(size_t) l]);
            }
            if (! wasSpatial) spatial.reset();
        }
        total = maxLat + (spatialActive ? spatial.latency() : 0) + slots[(size_t) uLim]->getLatencySamples();
        dryDelay.setDelay (total);
    }
    int getLatencySamples() const noexcept { return total; }

    // ---- lanes (any thread; plain stores)
    void setLane (int k, float gainDb, float pan, bool mute) noexcept { laneGain[(size_t) k].setTarget (gainDb); lanePan[(size_t) k].setTarget (pan); laneMute[(size_t) k].setTarget (mute ? 0.0f : 1.0f); }

    // ---- modulation (routes edited under the lock; sources' settings are plain stores)
    std::array<ModRoute, kMaxRoutes> routes; int numRoutes = 0;
    std::array<Lfo, kNumLfos> lfos; std::array<StepSequencer, kNumSteps> steps; std::array<EnvelopeFollower, kNumEnvs> envs;
    std::array<float, kNumMacros> macros {};
    std::array<float, kNumModSources> sourceValue {};          // last block's values (UI meters)
    std::array<std::vector<float>, kNumUnits> hostValues;       // what the host says, per slot param (the processor fills these each block)

    // ---- audio
    void process (juce::dsp::AudioBlock<float> block, const TempoInfo& tempo) noexcept
    {
        const int n = (int) block.getNumSamples(), ch = (int) block.getNumChannels();
        // the dry copy for the topology crossfade, delayed by the total latency
        juce::dsp::AudioBlock<float> dry (dryBuf); dry = dry.getSubBlock (0, (size_t) n); dry.copyFrom (block); dryDelay.process (dry);

        modulate (block, tempo);

        // fan out to the parallel lanes
        for (int k = 1; k < kLanes; ++k) if (laneUsed[(size_t) k]) { juce::dsp::AudioBlock<float> lb (laneBuf[(size_t) k]); lb.getSubBlock (0, (size_t) n).copyFrom (block); }
        // main chain in place, lanes in their buffers, in order; the limiter waits
        for (int k = 0; k < kNumUnits; ++k)
        {
            const int u = topo.order[(size_t) k]; if (u == uLim) continue;
            const int l = juce::jlimit (0, kLanes - 1, topo.lane[(size_t) u]);
            juce::dsp::AudioBlock<float> target = block;
            if (l != 0) { juce::dsp::AudioBlock<float> lb (laneBuf[(size_t) l]); target = lb.getSubBlock (0, (size_t) n); }
            const bool sp = spatialActive && spatialEligible (u) && spatial.wants (u);
            if (sp) slots[(size_t) u]->setWetTap (spatial.tapPtr (u, 0), spatial.tapPtr (u, 1));
            slots[(size_t) u]->process (target, tempo);
            if (sp) { slots[(size_t) u]->setWetTap (nullptr, nullptr); spatial.tap (u, target, slots[(size_t) u]->alignedDry (n), n, dbToGain (laneGain[(size_t) l].current) * laneMute[(size_t) l].current); }
        }
        // latency-align and sum
        laneDelay[0].process (block);
        for (int k = 1; k < kLanes; ++k)
        {
            if (! laneUsed[(size_t) k]) { laneGain[(size_t) k].skip (n); lanePan[(size_t) k].skip (n); laneMute[(size_t) k].skip (n); continue; }
            juce::dsp::AudioBlock<float> lb (laneBuf[(size_t) k]); lb = lb.getSubBlock (0, (size_t) n); laneDelay[(size_t) k].process (lb);
            for (int i = 0; i < n; ++i)
            {
                const float g = dbToGain (laneGain[(size_t) k].next()) * laneMute[(size_t) k].next(), p = lanePan[(size_t) k].next();
                float gl = g, gr = g; panGains (p, gl, gr);
                for (int c = 0; c < ch; ++c) block.getChannelPointer ((size_t) c)[i] += lb.getChannelPointer ((size_t) c)[i] * (c == 0 ? gl : gr);
            }
        }
        // main lane gain / pan / mute too (lane 0 has them like the others)
        for (int i = 0; i < n; ++i)
        {
            const float g = dbToGain (laneGain[0].next()) * laneMute[0].next(), p = lanePan[0].next();
            if (g != 1.0f || p != 0.0f)
            {
                float gl = g, gr = g; panGains (p, gl, gr);
                for (int c = 0; c < ch; ++c) block.getChannelPointer ((size_t) c)[i] *= (c == 0 ? gl : gr);
            }
        }
        // SPATIAL: the chain waits one Steam Audio frame and the placed wets join it, before the limiter
        if (spatialActive) spatial.finish (block, n);
        // the limiter, always last
        slots[(size_t) uLim]->process (block, tempo);
        // topology crossfade: a 10 ms dip to the (latency-matched) dry while the chain is re-wired, then back
        if (fadeState != 0 || topoFade.isSmoothing())
            for (int i = 0; i < n; ++i)
            {
                const float f = topoFade.next();
                for (int c = 0; c < ch; ++c) { auto* w = block.getChannelPointer ((size_t) c); const float d = dry.getChannelPointer ((size_t) c)[i]; w[i] = d + (w[i] - d) * f; }
                if (fadeState == 1 && f < 0.002f) { fadeState = 2; swapRequested = true; }
            }
    }
    // message thread: begin a topology change (fade out), finish it when the audio thread signals (fade in)
    // ---- SPATIAL (Phase 6b). The switch takes effect through a topology fade (its latency changes); targets are plain stores.
    SpatialEngine spatial;
    std::atomic<bool> spatialRequested { false };
    bool isSpatialActive() const noexcept { return spatialActive; }
    void requestTopology (const Topology& t) { pending = t; pendingSet = true; fadeState = 1; topoFade.setTarget (0.0f); }
    bool swapDue() const noexcept { return swapRequested; }
    void completeSwap() { swapRequested = false; applyTopology(); fadeState = 0; topoFade.setTarget (1.0f); }

private:
    // equal-power pan; dead centre is exactly unity on both sides
    static inline void panGains (float p, float& l, float& r) noexcept { if (p == 0.0f) return; const float a = (p + 1.0f) * 0.25f * juce::MathConstants<float>::pi; l *= std::cos (a) * 1.41421356f; r *= std::sin (a) * 1.41421356f; }
    void modulate (juce::dsp::AudioBlock<float> block, const TempoInfo& tempo) noexcept
    {
        const int n = (int) block.getNumSamples();
        // sources at block rate (the modules' own smoothers glide between blocks)
        for (int k = 0; k < kNumLfos; ++k) { lfos[(size_t) k].beginBlock (tempo); float v = 0; for (int i = 0; i < n; ++i) v = lfos[(size_t) k].next(); sourceValue[(size_t) k] = v; }
        for (int k = 0; k < kNumSteps; ++k) { steps[(size_t) k].beginBlock (tempo); float v = 0; for (int i = 0; i < n; ++i) v = steps[(size_t) k].next(); sourceValue[(size_t) (kNumLfos + k)] = v * 2.0f - 1.0f; }
        for (int k = 0; k < kNumEnvs; ++k) { auto& e = envs[(size_t) k]; float v = 0; const int ch = (int) block.getNumChannels(); for (int i = 0; i < n; ++i) { float m = 0; for (int c = 0; c < ch; ++c) m = juce::jmax (m, std::abs (block.getChannelPointer ((size_t) c)[i])); v = e.next (m); } sourceValue[(size_t) (kNumLfos + kNumSteps + k)] = juce::jlimit (0.0f, 1.0f, v) * 2.0f - 1.0f; }
        for (int k = 0; k < kNumMacros; ++k) sourceValue[(size_t) (kNumLfos + kNumSteps + kNumEnvs + k)] = macros[(size_t) k] * 2.0f - 1.0f;
        // host values + routed modulation -> slot values
        for (int u = 0; u < kNumUnits; ++u)
        {
            auto& hv = hostValues[(size_t) u]; auto& s = *slots[(size_t) u];
            for (size_t i = 0; i < hv.size(); ++i) s.setValue ((int) i, hv[i]);
        }
        for (int r = 0; r < numRoutes; ++r)
        {
            const auto& m = routes[(size_t) r];
            if (m.unit < 0 || m.unit >= kNumUnits) continue;
            auto& s = *slots[(size_t) m.unit]; const auto& specs = s.params();
            if (m.param < 0 || m.param >= (int) specs.size() || ! specs[(size_t) m.param].isContinuous()) continue;
            const auto& sp = specs[(size_t) m.param];
            float src = sourceValue[(size_t) juce::jlimit (0, kNumModSources - 1, m.source)];
            if (! m.bipolar) src = (src + 1.0f) * 0.5f;
            const float base = hostValues[(size_t) m.unit][(size_t) m.param];
            s.setValue (m.param, juce::jlimit (sp.min, sp.max, base + src * m.depth * sp.modDepth * (sp.max - sp.min)));
        }
    }

    std::vector<std::unique_ptr<EffectSlot>> slots;
    Topology topo, pending; bool pendingSet = false;
    std::array<juce::AudioBuffer<float>, kLanes> laneBuf; std::array<IntegerDelay, kLanes> laneDelay; std::array<int, kLanes> laneLat {}; std::array<bool, kLanes> laneUsed {};
    std::array<ParamSmoother, kLanes> laneGain, lanePan, laneMute;
    juce::AudioBuffer<float> dryBuf; IntegerDelay dryDelay; ParamSmoother topoFade; std::atomic<int> fadeState { 0 }; std::atomic<bool> swapRequested { false };
    double sr = 48000.0; int maxBlock = 0, channels = 2, total = 0;
    bool spatialActive = false;
};
} // namespace phantom
