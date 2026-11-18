// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX 15 CHORUS and 17 ECTO ECHO.
// CHORUS: the CHORUS mode stacks voices of the Surge Synth Team's chorus (sst-effects voice-effects/modulation/Chorus.h, GPL-3.0: its own
// sinc-interpolated delay line, LFO and feedback), each voice with its own LFO phase (sst's RNG), rate and delay spread; ENSEMBLE runs
// bucket-brigade lines (chowdsp's BBD model, GPL-3.0) swept by three 120-degree-apart LFOs plus a fast vibrato, the classic string
// ensemble. sst-effects has no bus chorus / ensemble (its effects/Chorus.h is empty at the pinned commit: AUDIT.md, DECISIONS.md 22).
// ECTO ECHO: a delay tree of up to 8 nodes in the way ChowMatrix builds one (each node: delay, feedback, filters, saturation, pan, its
// children taking its output), on the delay-line types ChowMatrix chooses between (chowdsp: Lagrange 5th for TAPE (+ wow) and DIGITAL (clean),
// Lagrange 3rd + soft saturation for ANALOG, the BBD model for BBD; chowdsp's 16-tap sinc was tried for DIGITAL: its read point wanders
// by up to a sample with the fraction, which breaks the tempo lock). ChowMatrix's own DelayProc was not compiled: it is written against
// chowdsp_utils v1 APIs that are gone from the v2 we pin (IIR::Filter, PitchShifter), DECISIONS.md 22.
#pragma once
#include "SpaceCommon.h"
#include "sst/basic-blocks/simd/setup.h"
#include "sst/voice-effects/modulation/Chorus.h"
#include <chowdsp_dsp_utils/chowdsp_dsp_utils.h>
#include <new>

namespace phantom
{
// ---------------------------------------------------------------- the host side of an sst voice effect (parameters, rate, its memory)
struct SstVfxConfig
{
    struct BaseClass { std::array<float, 8> fp {}; std::array<int, 4> ip {}; float sr = 48000.0f; float samplerate = 48000.0f; };   // samplerate: what sst's SimpleLFO reads
    static constexpr int blockSize = 16;
    static void setFloatParam (BaseClass* b, size_t i, float f) { b->fp[i] = f; }
    static float getFloatParam (const BaseClass* b, size_t i) { return b->fp[i]; }
    static void setIntParam (BaseClass* b, size_t i, int v) { b->ip[i] = v; }
    static int getIntParam (const BaseClass* b, size_t i) { return b->ip[i]; }
    static float dbToLinear (const BaseClass*, float f) { return std::pow (10.0f, f * 0.05f); }
    static float equalNoteToPitch (const BaseClass*, float f) { return std::pow (2.0f, f / 12.0f); }
    static float getSampleRate (const BaseClass* b) { return b->sr; }
    static float getSampleRateInv (const BaseClass* b) { return 1.0f / b->sr; }
    static void preReservePool (BaseClass*, size_t) {}
    // the voice asks for its delay-line memory in its init (our prepare); 16-byte aligned for its SSE lines
    static uint8_t* checkoutBlock (BaseClass*, size_t n) { auto* p = static_cast<uint8_t*> (::operator new[] (n, std::align_val_t (16))); std::memset (p, 0, n); return p; }
    static void returnBlock (BaseClass*, uint8_t* p, size_t) { ::operator delete[] (p, std::align_val_t (16)); }
};

// ================================================================ 15 CHORUS
class ChorusFx final : public CatalogEffect
{
public:
    ChorusFx() : CatalogEffect (uChorus) {}
    bool wetOnly() const override { return true; }
    bool widensOnPurpose() const override { return true; }
    double ownSpreadHz() const override { return 200.0; }   // its LFOs move a tone by up to ~1 %: 200 Hz at 20 kHz
    double tailSeconds() const override { return 1.0; }
    // FEEDBACK runs through the voice's soft clipper: it glides over 60 ms, as the drive controls of the nonlinear units do
    std::vector<ParamSpec> parameters() const override { auto ps = CatalogEffect::parameters(); for (auto& q : ps) if (q.id == "fb") q.smoothMs = 60.0f; return ps; }
protected:
    using Voice = sst::voice_effects::modulation::Chorus<SstVfxConfig>;
    void prepareEffect (double sampleRate, int, int) override
    {
        pRate = id ("rate"); pDepth = id ("depth"); pDelay = id ("delay"); pFb = id ("fb"); pWidth = id ("width"); pVoices = id ("voices"); pMode = id ("mode");
        for (int v = 0; v < kMaxVoices; ++v)
        {
            rngs[(size_t) v] = std::make_unique<sst::basic_blocks::dsp::RNG> ((uint32_t) (7717 + v * 131));
            voices[(size_t) v] = std::make_unique<Voice> (sinc, *rngs[(size_t) v]);
            auto& vc = *voices[(size_t) v]; vc.sr = (float) sampleRate; vc.samplerate = (float) sampleRate;
            vc.initVoiceEffectParams(); vc.setIntParam (Voice::ipShape, 0); vc.setIntParam (Voice::ipStereo, 1);
            vc.initVoiceEffect();
        }
        const juce::dsp::ProcessSpec spec { sampleRate, 32, 2 };
        for (auto& b : bbd) { b.prepare (spec); b.setFilterFreq (9000.0f); }
        for (auto& b : bbdIn) b = 0.0f;
        ensPhase = 0.0; vibPhase = 0.0; inPos = 0;
    }
    void resetEffect() override
    {
        for (auto& v : voices) if (v) v->initVoiceEffect();   // re-clears its lines (memory already held: the same size is kept)
        for (auto& b : bbd) b.reset(); inPos = 0;
    }
public:
    void process (juce::dsp::AudioBlock<float> block, const ModulationContext& ctx) noexcept override
    {
        beginBlock (ctx);
        const int n = (int) block.getNumSamples(); auto* L = block.getChannelPointer (0); auto* R = block.getNumChannels() > 1 ? block.getChannelPointer (1) : L;
        const int nv = juce::jlimit (1, kMaxVoices, (int) std::lround (target (pVoices)));
        const bool ensemble = choice (pMode) == 1;
        const float norm = 1.0f / std::sqrt ((float) nv);
        // sst voices run in 16-sample blocks: the input is gathered, a block runs, its output is played out 16 samples later (latency-free for
        // the wet sound's character; the slot's dry path is not delayed: a chorus wet has no "aligned" onset)
        for (int i = 0; i < n; ++i)
        {
            const float rate = p (pRate).next(), depth = p (pDepth).next(), delMs = p (pDelay).next(), fb = p (pFb).next(), w = p (pWidth).next();
            float wl = 0.0f, wr = 0.0f;
            if (! ensemble)
            {
                inBuf[0][inPos] = L[i]; inBuf[1][inPos] = R[i];
                wl = outBuf[0][inPos]; wr = outBuf[1][inPos];
                if (++inPos == SstVfxConfig::blockSize)
                {
                    inPos = 0; for (int c = 0; c < 2; ++c) std::fill (std::begin (outBuf[c]), std::end (outBuf[c]), 0.0f);
                    for (int v = 0; v < nv; ++v)
                    {
                        auto& vc = *voices[(size_t) v]; const float spread = nv > 1 ? (float) v / (float) (nv - 1) - 0.5f : 0.0f;
                        vc.setFloatParam (Voice::fpTime, juce::jlimit (1.0f, 45.0f, delMs * (1.0f + 0.35f * spread)));
                        vc.setFloatParam (Voice::fpRate, std::log2 (juce::jmax (0.01f, rate * (1.0f + 0.27f * spread))));
                        vc.setFloatParam (Voice::fpDepth, juce::jlimit (0.0f, 1.0f, depth * 0.5f));
                        vc.setFloatParam (Voice::fpFeedback, juce::jlimit (0.0f, 0.75f, fb * 0.75f));
                        float tl alignas (16)[SstVfxConfig::blockSize], tr alignas (16)[SstVfxConfig::blockSize];
                        vc.processStereo (inBuf[0], inBuf[1], tl, tr, 0.0f);
                        for (int k = 0; k < SstVfxConfig::blockSize; ++k) { outBuf[0][k] += tl[k] * norm; outBuf[1][k] += tr[k] * norm; }
                    }
                }
            }
            else
            {
                // ensemble: three BBD taps per side, LFOs 120 degrees apart (slow chorus + fast vibrato), right side a quarter-turn on
                ensPhase += rate / sr; if (ensPhase >= 1.0) ensPhase -= 1.0;
                vibPhase += (rate * 9.0f) / sr; if (vibPhase >= 1.0) vibPhase -= 1.0;
                for (int t = 0; t < 3; ++t)
                {
                    for (int c = 0; c < 2; ++c)
                    {
                        const double ph = ensPhase + t / 3.0 + c * 0.25, vp = vibPhase + t / 3.0 + c * 0.25;
                        const float mod = 0.5f + 0.5f * (float) std::sin (ph * juce::MathConstants<double>::twoPi), vib = (float) std::sin (vp * juce::MathConstants<double>::twoPi);
                        const float dms = juce::jlimit (1.0f, 40.0f, delMs * 0.5f + depth * (6.0f * mod + 0.35f * vib));
                        auto& line = bbd[(size_t) t]; line.setDelay (dms * 0.001f * (float) sr);
                        const float x = (c == 0 ? L[i] : R[i]) + bbdIn[(size_t) (t * 2 + c)] * fb * 0.7f;
                        line.pushSample (c, x); const float y = line.popSample (c); bbdIn[(size_t) (t * 2 + c)] = y;
                        (c == 0 ? wl : wr) += y * 0.577f;
                    }
                }
            }
            const float m = 0.5f * (wl + wr), s = 0.5f * (wl - wr) * w;
            L[i] = (m + s) * kLevel; R[i] = (m - s) * kLevel;
        }
    }
private:
    static constexpr int kMaxVoices = 8; static constexpr float kLevel = 0.823f;   // level match on pink noise (DECISIONS.md 24)
    sst::basic_blocks::tables::SurgeSincTableProvider sinc;
    std::array<std::unique_ptr<sst::basic_blocks::dsp::RNG>, kMaxVoices> rngs; std::array<std::unique_ptr<Voice>, kMaxVoices> voices;
    alignas (16) float inBuf[2][SstVfxConfig::blockSize] {}; alignas (16) float outBuf[2][SstVfxConfig::blockSize] {}; int inPos = 0;
    std::array<chowdsp::BBD::BBDDelayWrapper<4096>, 3> bbd; std::array<float, 6> bbdIn {};
    double ensPhase = 0.0, vibPhase = 0.0;
    int pRate = 0, pDepth = 0, pDelay = 0, pFb = 0, pWidth = 0, pVoices = 0, pMode = 0;
};

// ================================================================ 17 ECTO ECHO
class EctoEchoFx final : public CatalogEffect
{
public:
    EctoEchoFx() : CatalogEffect (uEcho) {}
    bool wetOnly() const override { return true; }
    bool widensOnPurpose() const override { return true; }
    bool isRhythmic() const override { return true; }
    double tailSeconds() const override { return 30.0; }
    void tempo (const TempoInfo& t) override { bpm = t.bpm; }
    // the delay time moves pitch while it glides (as on tape); its zipper check is made with the character (saturation / BBD) off
    void zipperIsolate (const std::string& c, std::vector<float>& v) const override { if (c == "time") { v[(size_t) id ("mode")] = 3.0f; v[(size_t) id ("char")] = 0.0f; v[(size_t) id ("fb")] = 0.0f; } if (c == "char") v[(size_t) id ("mode")] = 1.0f; v[(size_t) id ("sync")] = 0.0f; }   // CHARACTER moves the tape wow (pitch): checked in ANALOG, where it is drive only
    double ownSpreadHz() const override { return 150.0; }   // the node saturation's harmonics, as for a nonlinear module
    // FEEDBACK drives the loop's saturation: it glides over 60 ms, as the drive controls of the nonlinear units do
    std::vector<ParamSpec> parameters() const override { auto ps = CatalogEffect::parameters(); for (auto& q : ps) if (q.id == "fb") q.smoothMs = 60.0f; return ps; }
protected:
    static constexpr int kNodes = 8;
    void prepareEffect (double sampleRate, int, int) override
    {
        pMode = id ("mode"); pSync = id ("sync"); pTime = id ("time"); pDiv = id ("div"); pFb = id ("fb"); pPing = id ("ping"); pWidth = id ("width"); pLow = id ("lowcut");
        pHigh = id ("highcut"); pChar = id ("char"); pDuck = id ("duck"); pNodes = id ("nodes"); pSpread = id ("spread"); pSat = id ("sat"); pPan = id ("pan");
        const juce::dsp::ProcessSpec spec { sampleRate, 64, 2 };
        const int maxSamp = (int) (sampleRate * 2.6) + 16;
        for (auto& nd : node)
        {
            nd.tape = std::make_unique<chowdsp::DelayLine<float, chowdsp::DelayLineInterpolationTypes::Lagrange5th>> (maxSamp); nd.tape->prepare (spec);
            nd.analog = std::make_unique<chowdsp::DelayLine<float, chowdsp::DelayLineInterpolationTypes::Lagrange3rd>> (maxSamp); nd.analog->prepare (spec);
            nd.digital = std::make_unique<chowdsp::DelayLine<float, chowdsp::DelayLineInterpolationTypes::Lagrange5th>> (maxSamp); nd.digital->prepare (spec);
            nd.bbd = std::make_unique<chowdsp::BBD::BBDDelayWrapper<16384>>(); nd.bbd->prepare (spec);
            for (auto& f : nd.lp) f.prepare (2); for (auto& f : nd.hp) f.prepare (2);
            nd.fbState = { 0.0f, 0.0f };
        }
        duck.prepare (sampleRate); delaySm.reset (sampleRate, 0.12); delaySm.setCurrentAndTargetValue (-1.0f); wow = 0.0; curDelay = -1.0f;
    }
    void resetEffect() override { for (auto& nd : node) { nd.tape->reset(); nd.analog->reset(); nd.digital->reset(); nd.bbd->reset(); for (auto& f : nd.lp) f.reset(); for (auto& f : nd.hp) f.reset(); nd.fbState = { 0.0f, 0.0f }; } duck.reset(); delaySm.setCurrentAndTargetValue (-1.0f); curDelay = -1.0f; }
public:
    // the synced time, from the host tempo every block (sample-locked: the delay is exactly the division's length at the current tempo)
    double syncedSamples() const noexcept { const auto& dv = divisions(); return dv[(size_t) juce::jlimit (0, (int) dv.size() - 1, choice (pDiv))].beats * 60.0 / juce::jmax (1.0, bpm) * sr; }
    void process (juce::dsp::AudioBlock<float> block, const ModulationContext& ctx) noexcept override
    {
        beginBlock (ctx);
        const int n = (int) block.getNumSamples(); auto* L = block.getChannelPointer (0); auto* R = block.getNumChannels() > 1 ? block.getChannelPointer (1) : L;
        const int mode = choice (pMode), nn = juce::jlimit (1, kNodes, (int) std::lround (target (pNodes)));
        const bool ping = on (pPing), sync = on (pSync);
        if (mode != lastMode) { lastMode = mode; for (auto& nd : node) nd.fbState = { 0.0f, 0.0f }; }
        // a synced time follows the tempo exactly (a tempo change re-times at once: the 120 ms glide is only for the TIME knob)
        if (sync) { const float d = (float) syncedSamples(); delaySm.setCurrentAndTargetValue (d); }
        for (int i = 0; i < n; ++i)
        {
            const float tms = p (pTime).next(), fbk = juce::jmin (0.98f, p (pFb).next() * 1.05f), w = p (pWidth).next(), lc = p (pLow).next(), hc = p (pHigh).next();
            const float chr = p (pChar).next(), dk = p (pDuck).next(), spr = p (pSpread).next(), sat = p (pSat).next(), pan = p (pPan).next();
            // TIME glides like a tape transport: the read point moves at most 0.25 samples per sample (pitch within -25 % .. +25 % while it
            // moves), so a knob turn bends pitch rather than clicking; a synced time follows the tempo at once
            if (! sync) { const float want = tms * 0.001f * (float) sr; if (curDelay < 0.0f) curDelay = want; else curDelay += juce::jlimit (-0.25f, 0.25f, (want - curDelay) * 0.002f); }
            const float base = sync ? delaySm.getCurrentValue() : curDelay;
            if ((i & 31) == 0)
                for (int k = 0; k < nn; ++k)
                {
                    auto& nd = node[(size_t) k];
                    const float hcEff = mode == 0 ? hc * (1.0f - 0.45f * chr) : mode == 2 ? juce::jmin (hc, 9000.0f - 4000.0f * chr) : hc;
                    for (auto& f : nd.lp) f.calcCoefs (juce::jlimit (200.0f, (float) sr * 0.45f, hcEff), (float) sr);
                    for (auto& f : nd.hp) f.calcCoefs (juce::jlimit (10.0f, 5000.0f, lc), (float) sr);
                }
            // tape: a gentle wow on the read position, deeper with CHARACTER
            wow += 0.55 / sr; if (wow >= 1.0) wow -= 1.0;
            const float wowSamp = mode == 0 ? chr * 0.0009f * (float) sr * (float) std::sin (wow * juce::MathConstants<double>::twoPi) : 0.0f;
            const float inL = L[i], inR = R[i];
            duck.push (juce::jmax (std::abs (inL), std::abs (inR)));
            float outL = 0.0f, outR = 0.0f, feedL = inL, feedR = inR;
            for (int k = 0; k < nn; ++k)
            {
                auto& nd = node[(size_t) k];
                // the tree: node 1 takes the input; each next node is a child of the one before (ChowMatrix's chain), its time a spread multiple
                const float mult = 1.0f + spr * (0.5f + 0.37f * k) * (k > 0 ? 1.0f : 0.0f);
                const float d = juce::jlimit (8.0f, (float) (sr * 2.5), base * mult + wowSamp);
                float xl = feedL + nd.fbState[ping ? 1 : 0] * fbk, xr = feedR + nd.fbState[ping ? 0 : 1] * fbk;
                if (ping) { const float mono = 0.5f * (xl + xr); xl = k == 0 ? mono * 1.41f + nd.fbState[1] * fbk : xl; xr = k == 0 ? nd.fbState[0] * fbk : xr; }
                float yl = readNode (nd, mode, 0, xl, d), yr = readNode (nd, mode, 1, xr, d);
                // per node: filters, character / node saturation, then pan
                yl = nd.hp[0].processSample (nd.lp[0].processSample (yl)); yr = nd.hp[1].processSample (nd.lp[1].processSample (yr));
                const float drive = (mode == 1 ? chr * 0.8f : mode == 0 ? chr * 0.4f : 0.0f) + sat;
                if (drive > 0.0f) { const float g = 1.0f + 3.0f * drive; yl = std::tanh (yl * g) / g * (1.0f + 0.3f * drive); yr = std::tanh (yr * g) / g * (1.0f + 0.3f * drive); }
                nd.fbState = { safe (yl), safe (yr) };
                const float pp = nn > 1 ? pan * ((float) k / (float) (nn - 1) * 2.0f - 1.0f) : 0.0f;
                outL += yl * (1.0f - juce::jmax (0.0f, pp)); outR += yr * (1.0f + juce::jmin (0.0f, pp));
                feedL = yl; feedR = yr;
            }
            const float g = duck.gain (dk) / std::sqrt ((float) nn);
            const float m = 0.5f * (outL + outR), s = 0.5f * (outL - outR) * w;
            L[i] = (m + s) * g * kLevel; R[i] = (m - s) * g * kLevel;
        }
    }
private:
    static constexpr float kLevel = 1.86f;   // kLevel: level match on pink noise
    static inline float safe (float x) noexcept { return juce::jlimit (-4.0f, 4.0f, x); }   // the loop can never run away
    struct Node
    {
        std::unique_ptr<chowdsp::DelayLine<float, chowdsp::DelayLineInterpolationTypes::Lagrange5th>> tape;
        std::unique_ptr<chowdsp::DelayLine<float, chowdsp::DelayLineInterpolationTypes::Lagrange3rd>> analog;
        std::unique_ptr<chowdsp::DelayLine<float, chowdsp::DelayLineInterpolationTypes::Lagrange5th>> digital;
        std::unique_ptr<chowdsp::BBD::BBDDelayWrapper<16384>> bbd;
        std::array<chowdsp::FirstOrderLPF<float>, 2> lp; std::array<chowdsp::FirstOrderHPF<float>, 2> hp;
        std::array<float, 2> fbState {};
    };
    static inline float readNode (Node& nd, int mode, int c, float x, float d) noexcept
    {
        switch (mode)
        {
            case 0:  nd.tape->setDelay (d); nd.tape->pushSample (c, x); return nd.tape->popSample (c);
            case 1:  nd.analog->setDelay (d); nd.analog->pushSample (c, x); return nd.analog->popSample (c);
            case 2:  nd.bbd->setDelay (d); nd.bbd->pushSample (c, x); return nd.bbd->popSample (c);
            default: nd.digital->setDelay (d); nd.digital->pushSample (c, x); return nd.digital->popSample (c);
        }
    }
    std::array<Node, kNodes> node; Ducker duck; juce::SmoothedValue<float> delaySm; double wow = 0.0, bpm = 120.0; int lastMode = -1; float curDelay = -1.0f;
    int pMode = 0, pSync = 0, pTime = 0, pDiv = 0, pFb = 0, pPing = 0, pWidth = 0, pLow = 0, pHigh = 0, pChar = 0, pDuck = 0, pNodes = 0, pSpread = 0, pSat = 0, pPan = 0;
};
} // namespace phantom
