// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX 20 HAUNTED HALLS: Dragonfly Reverb's engines (early reflections + Hall / Room / Plate late reverbs on freeverb3), through
// src/space/HallsEngine.h. Eleven modes: ROOM, HALL, PLATE, CHAMBER, CATHEDRAL, CLOUD and GALAXY are the engines at different sizes and
// settings; REVERSE (the tail played back in reversed swells), GATED (the 80s gate on the tail, held open by the input), NONLINEAR (dense
// early reflections cut off by the gate) and DARK TAPE (a dark plate with a slow tape wobble and soft saturation) add our own stage after
// the engine. SHIMMER feeds an octave-up copy (Signalsmith Stretch) back in; FREEZE holds the space.
// Size and mode re-size the engine's delay lines (freeverb3 allocates for that), so they are rebuilt on this unit's worker
// thread into a spare engine set, and the audio thread crossfades to it (80 ms). Decay, diffusion, damping, low cut, width and early send
// are live on the audio thread.
#pragma once
#include "SpaceCommon.h"
#include "../space/HallsEngine.h"
#include "signalsmith-stretch/signalsmith-stretch.h"
#include <atomic>

namespace phantom
{
class HauntedHallsFx final : public CatalogEffect
{
public:
    HauntedHallsFx() : CatalogEffect (uHalls) {}
    ~HauntedHallsFx() override { worker.reset(); for (auto& s : sets) if (s.eng) halls::destroy (s.eng); }
    bool wetOnly() const override { return true; }
    bool widensOnPurpose() const override { return true; }
    double tailSeconds() const override { return 62.0; }
    double ownSpreadHz() const override { return 150.0; }
    void zipperIsolate (const std::string& c, std::vector<float>& v) const override { if (c != "mod") v[(size_t) id ("mod")] = 0.0f; if (c != "shimmer") v[(size_t) id ("shimmer")] = 0.0f; }
    struct Spec { int mode = 1; float size = 0.6f, mod = 0.4f; bool operator== (const Spec& o) const { return mode == o.mode && size == o.size && mod == o.mod; } };
protected:
    void prepareEffect (double sampleRate, int maxBlockSize, int) override
    {
        worker.reset();
        pMode = id ("mode"); pSize = id ("size"); pDecay = id ("decay"); pPre = id ("predelay"); pDiff = id ("diffusion"); pDamp = id ("damp"); pLow = id ("lowcut");
        pMod = id ("mod"); pWidth = id ("width"); pEarly = id ("early"); pShim = id ("shimmer"); pFreeze = id ("freeze");
        for (auto& s : sets) { if (! s.eng) s.eng = halls::create(); s.state.store (kFree); }
        const int mb = juce::jmax (maxBlockSize, 1024); scratch.setSize (8, mb + 2048 + 512); scratch.clear();
        pre.prepare (sampleRate, 0.52); wobble.prepare (sampleRate, 0.05); gateEnv = 0.0f; gateOpen = 0.0f; holdLeft = 0;
        rev.setSize (2, (int) (sampleRate * 4.2) + 8); rev.clear(); revW = 0; for (auto& r : rr) r = {}; sinceStart = 1 << 30; turn = 0;
        stretch.presetCheaper (2, (float) sampleRate, true); stretch.setTransposeSemitones (12.0f); shimFb.setSize (2, mb); shimFb.clear(); shimOut.setSize (2, mb); shimOut.clear();
        wobPhase = 0.0; wPrimed = false; wSm.prepare (sampleRate, 20.0f, target (pWidth), false);
        Spec s0 = specNow(); halls::configure (sets[0].eng, sampleRate, s0.mode, s0.size, s0.mod); sets[0].built = s0; sets[0].last.fill (-1.0f); sets[0].state.store (kLive); cur = 0; next = -1; xf = 1.0f;
        requested = s0; reqGen.store (0); worker = std::make_unique<Worker> (*this);
    }
    // message thread (before playback / on a topology change): a different mode / size / modulation is built here at once, into the live set
    void updateParameters (const ModulationContext& ctx) override
    {
        EffectBase::updateParameters (ctx);
        const Spec want = specNow();
        if (! (want == sets[(size_t) cur].built) && next < 0) { halls::configure (sets[(size_t) cur].eng, sr, want.mode, want.size, want.mod); sets[(size_t) cur].built = want; sets[(size_t) cur].last.fill (-1.0f); requested = want; }
    }
    void resetEffect() override
    {
        for (auto& s : sets) { const int st = s.state.load(); if (st == kLive || st == kIncoming) halls::mute (s.eng); }
        wPrimed = false; pre.reset(); glidePre.cur = -1.0f; wobble.reset(); rev.clear(); revW = 0; for (auto& r : rr) r = {}; shimFb.clear(); gateEnv = 0.0f; gateOpen = 0.0f; stretch.reset();
    }
public:
    void process (juce::dsp::AudioBlock<float> block, const ModulationContext& ctx) noexcept override
    {
        beginBlock (ctx);
        const int n = (int) block.getNumSamples(); auto* L = block.getChannelPointer (0); auto* R = block.getNumChannels() > 1 ? block.getChannelPointer (1) : L;
        // SIZE re-forms the hall once it has stopped moving for 100 ms (each change re-sizes the engine on the worker): a sweep does not
        // rebuild it over and over
        Spec want = specNow();
        if (! (want == pendingSpec)) { pendingSpec = want; settleSamples = 0; } else settleSamples += n;
        if (! (want == requested) && (settleSamples >= (int) (0.1 * sr) || want.mode != requested.mode)) { requested = want; reqGen.fetch_add (1); }
        if (next < 0) for (int k = 0; k < kSets; ++k) if (sets[(size_t) k].state.load() == kReady) { sets[(size_t) k].state.store (kIncoming); next = k; xf = 0.0f; inFade = 0.0f; break; }
        const bool frz = on (pFreeze); const int mode = sets[(size_t) cur].built.mode;
        auto liveFor = [&] (ESet& s, const std::array<float, 7>& lv) { if (s.last != lv) { s.last = lv; halls::live (s.eng, lv[0], lv[1], lv[2], lv[3], lv[4], lv[5], lv[6]); } };
        if (! wPrimed) { wSm.snap (target (pWidth)); wPrimed = true; } wSm.setTarget (target (pWidth));
        float* inL = scratch.getWritePointer (0); float* inR = scratch.getWritePointer (1);
        // shimmer: an octave-up copy of the last wet block, fed back (only computed while SHIMMER is up)
        const bool shim = target (pShim) > 0.0f || p (pShim).isSmoothing();
        if (shim) { float* si[2] { shimFb.getWritePointer (0), shimFb.getWritePointer (1) }; float* so[2] { shimOut.getWritePointer (0), shimOut.getWritePointer (1) }; stretch.process (si, n, so, n); }
        for (int i = 0; i < n; ++i)
        {
            const float pd = glidePre (p (pPre).next() * 0.001f * (float) sr), sh = p (pShim).next() * 0.5f;
            pre.write (0, L[i]); pre.write (1, R[i]);
            const float g = frz ? 0.0f : 1.0f;
            inL[i] = pre.read (0, pd + 1.0f) * g + (shim ? shimOut.getSample (0, i) * sh : 0.0f);
            inR[i] = pre.read (1, pd + 1.0f) * g + (shim ? shimOut.getSample (1, i) * sh : 0.0f);
        }
        float* oL = scratch.getWritePointer (2); float* oR = scratch.getWritePointer (3); float* nL = scratch.getWritePointer (4); float* nR = scratch.getWritePointer (5);
        // freeverb3 takes its live controls as plain values: while they glide, the engine runs in 16-sample steps with fresh values
        for (int i0 = 0; i0 < n; )
        {
            bool moving = false; for (int k : { pDecay, pDiff, pDamp, pLow, pEarly, pMod }) if (smoothers[(size_t) k].isSmoothing()) moving = true;
            const int len = juce::jmin (moving ? 4 : 256, n - i0);
            for (int k : { pDecay, pDiff, pDamp, pLow, pEarly, pMod }) p (k).skip (len);
            const float decay = frz ? 1.0e4f : p (pDecay).current, damp = frz ? 0.0f : p (pDamp).current;
            const std::array<float, 7> lv { decay, p (pDiff).current, damp, p (pLow).current, 1.0f, 0.1f + 0.4f * p (pEarly).current, p (pMod).current };
            liveFor (sets[(size_t) cur], lv); runSet (sets[(size_t) cur], inL + i0, inR + i0, oL + i0, oR + i0, len);
            if (next >= 0)
            {   // the incoming set's input fades in over 20 ms (a tone switched on mid-wave would click inside the new engine)
                float* fL = scratch.getWritePointer (6) + 2048; float* fR = scratch.getWritePointer (7) + 2048;
                for (int i = 0; i < len; ++i) { inFade = juce::jmin (1.0f, inFade + 1.0f / (0.02f * (float) sr)); const float g = 0.5f - 0.5f * std::cos (inFade * juce::MathConstants<float>::pi); fL[i] = inL[i0 + i] * g; fR[i] = inR[i0 + i] * g; }
                liveFor (sets[(size_t) next], lv); runSet (sets[(size_t) next], fL, fR, nL + i0, nR + i0, len);
            }
            i0 += len;
        }
        if (next >= 0)
        {
            const float step = 1.0f / (float) (0.15 * sr);
            for (int i = 0; i < n; ++i) { xf = juce::jmin (1.0f, xf + step); const float a = std::cos (xf * juce::MathConstants<float>::halfPi), b = std::sin (xf * juce::MathConstants<float>::halfPi); oL[i] = oL[i] * a + nL[i] * b; oR[i] = oR[i] * a + nR[i] * b; }
            if (xf >= 1.0f) { sets[(size_t) cur].state.store (kFree); cur = next; next = -1; sets[(size_t) cur].state.store (kLive); }
        }
        // the modes' own stage after the engine
        post (mode, L, R, oL, oR, n, frz);
        if (shim) for (int i = 0; i < n; ++i) { shimFb.setSample (0, i, oL[i]); shimFb.setSample (1, i, oR[i]); }
        // width: ours, on the engine's stereo output (mid / side), so WIDTH 0 is exactly mono
        for (int i = 0; i < n; ++i) { const float w = wSm.next(), m = 0.5f * (oL[i] + oR[i]), sd = 0.5f * (oL[i] - oR[i]) * w; L[i] = (m + sd) * kLevel; R[i] = (m - sd) * kLevel; }
    }
private:
    static constexpr int kSets = 3, kFree = 0, kBuilding = 1, kReady = 2, kIncoming = 3, kLive = 4;
    static constexpr float kLevel = 0.651f;   // level match on pink noise
    struct ESet { halls::Set* eng = nullptr; std::atomic<int> state { 0 }; Spec built; std::array<float, 7> last {}; };
    Spec specNow() const noexcept { Spec s; s.mode = choice (pMode); s.size = std::round (target (pSize) * 20.0f) / 20.0f; s.mod = 0.4f; /* MODULATION is live (Dragonfly's own Modulation control: spin / LFO factors); wander is fixed */ return s; }
    void runSet (ESet& s, const float* inL, const float* inR, float* oL, float* oR, int n) noexcept
    {
        float* eL = scratch.getWritePointer (6); float* eR = scratch.getWritePointer (7);
        halls::process (s.eng, inL, inR, eL, eR, oL, oR, n);
        const float early = p (pEarly).current, lateG = s.built.mode == halls::Nonlinear ? 0.0f : 1.0f, earlyG = s.built.mode == halls::Nonlinear ? 1.6f : early * 0.9f;
        for (int i = 0; i < n; ++i) { oL[i] = oL[i] * lateG + eL[i] * earlyG; oR[i] = oR[i] * lateG + eR[i] * earlyG; }
    }
    void post (int mode, const float* inL, const float* inR, float* oL, float* oR, int n, bool frz) noexcept
    {
        if (mode == halls::Gated || mode == halls::Nonlinear)
        {   // the gate: open while the input plays, held for a time set by DECAY (up to 1.5 s), closed in 40 ms
            const float hold = juce::jlimit (0.08f, 1.5f, p (pDecay).current * 0.25f), rel = 1.0f - std::exp (-1.0f / (0.04f * (float) sr)), att = 1.0f - std::exp (-1.0f / (0.002f * (float) sr));
            for (int i = 0; i < n; ++i)
            {
                const float pk = juce::jmax (std::abs (inL[i]), std::abs (inR[i]));
                if (pk > 0.01f || frz) holdLeft = (int) (hold * (float) sr);
                const float tgt = holdLeft > 0 ? 1.0f : 0.0f; if (holdLeft > 0) --holdLeft;
                gateOpen += (tgt > gateOpen ? att : rel) * (tgt - gateOpen);
                oL[i] *= gateOpen; oR[i] *= gateOpen;
            }
        }
        else if (mode == halls::Reverse)
        {   // reversed swells: the tail is recorded and played backwards in overlapping chunks (length from DECAY, 0.25..2 s)
            const int N = rev.getNumSamples(); const float len = juce::jlimit (0.25f, 2.0f, p (pDecay).current * 0.5f) * (float) sr;
            for (int i = 0; i < n; ++i)
            {
                rev.setSample (0, revW, oL[i]); rev.setSample (1, revW, oR[i]); if (++revW == N) revW = 0;
                if (++sinceStart >= (int) (len * 0.5f)) { sinceStart = 0; auto& r = rr[(size_t) turn]; turn ^= 1; r.len = len; r.left = len; r.pos = (float) revW - 1.0f; }
                float yl = 0.0f, yr = 0.0f;
                for (auto& r : rr)
                {
                    if (r.left <= 0.0f) continue;
                    const float ph = 1.0f - r.left / r.len, w = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * ph) * 1.0f;
                    const float env = ph * ph;   // a swell: quiet at the start of each reversed chunk, full before its end
                    int x = (int) r.pos; while (x < 0) x += N; while (x >= N) x -= N;
                    yl += rev.getSample (0, x) * w * env * 1.7f; yr += rev.getSample (1, x) * w * env * 1.7f; r.pos -= 1.0f; r.left -= 1.0f;
                }
                oL[i] = yl; oR[i] = yr;
            }
        }
        else if (mode == halls::DarkTape)
        {   // slow tape wobble (a moving read on a short line) and soft saturation
            for (int i = 0; i < n; ++i)
            {
                wobPhase += 0.6 / sr; if (wobPhase >= 1.0) wobPhase -= 1.0;
                const float d = (0.006f + 0.0015f * (float) std::sin (wobPhase * juce::MathConstants<double>::twoPi)) * (float) sr;
                wobble.write (0, oL[i]); wobble.write (1, oR[i]);
                oL[i] = std::tanh (wobble.read (0, d) * 1.4f) / 1.4f; oR[i] = std::tanh (wobble.read (1, d) * 1.4f) / 1.4f;
            }
        }
    }
    struct Worker : juce::Thread
    {
        explicit Worker (HauntedHallsFx& o) : juce::Thread ("Phantom FX HAUNTED HALLS builder"), owner (o) { startThread (juce::Thread::Priority::low); }
        ~Worker() override { stopThread (4000); }
        void run() override
        {
            int doneGen = owner.reqGen.load();
            while (! threadShouldExit())
            {
                wait (15);
                const int g = owner.reqGen.load(); if (g == doneGen) continue;
                int k = -1; for (int i = 0; i < kSets; ++i) { int ex = kFree; if (owner.sets[(size_t) i].state.compare_exchange_strong (ex, kBuilding)) { k = i; break; } }
                if (k < 0) continue;
                const Spec sp = owner.requested; doneGen = g;
                halls::configure (owner.sets[(size_t) k].eng, owner.sr, sp.mode, sp.size, sp.mod);
                owner.sets[(size_t) k].built = sp; owner.sets[(size_t) k].last.fill (-1.0f); owner.sets[(size_t) k].state.store (kReady);
                while (! threadShouldExit() && owner.sets[(size_t) k].state.load() == kReady) wait (5);
            }
        }
        HauntedHallsFx& owner;
    };
    struct RevReader { float pos = 0.0f, len = 1.0f, left = 0.0f; };
    std::array<ESet, kSets> sets; int cur = 0, next = -1; float xf = 1.0f;
    Spec requested, pendingSpec; int settleSamples = 0; std::atomic<int> reqGen { 0 }; std::unique_ptr<Worker> worker;
    juce::AudioBuffer<float> scratch, rev, shimFb, shimOut; StereoDelay pre, wobble; PreGlide glidePre; signalsmith::stretch::SignalsmithStretch<float> stretch;
    std::array<RevReader, 2> rr {}; int revW = 0, sinceStart = 1 << 30, turn = 0, holdLeft = 0; float gateEnv = 0.0f, gateOpen = 0.0f; double wobPhase = 0.0; ParamSmoother wSm; bool wPrimed = false; float inFade = 1.0f;
    int pMode = 0, pSize = 0, pDecay = 0, pPre = 0, pDiff = 0, pDamp = 0, pLow = 0, pMod = 0, pWidth = 0, pEarly = 0, pShim = 0, pFreeze = 0;
};
} // namespace phantom
