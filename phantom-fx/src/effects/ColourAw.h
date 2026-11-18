// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX Colour effects built on Airwindows (MIT, compiled unchanged; see src/aw): TUBE GLOW, SOUL CRUSH, FATTEN, BASS LAB.
// Our code here is only the plumbing around the engines: parameter mapping, crossovers, mixing and level matching.
#pragma once
#include "ColourSst.h"
#include "../aw/Airwindows.h"
#include <chowdsp_compressor/chowdsp_compressor.h>

namespace phantom
{
// An Airwindows engine run in short sub-blocks so its parameters follow the rack's smoothed values (Airwindows reads its
// parameters once per call). `amount` crossfades the engine in from the untouched signal, so at 0 the module nulls.
struct AwStage
{
    static constexpr int kSub = 32;
    std::unique_ptr<aw::Plugin> fx; std::vector<float> dl, dr;
    // optional guard: chowdsp's ADAA tanh in front of the engine, so the Airwindows code (which clips hard at its own limits, without
    // anti-aliasing) is never driven into those limits; it costs one sample (at the processing rate) on every path, engine on or off
    // the guard's output is then held to the audio band (two Butterworth sections at 20 kHz, run at the oversampled rate) so a hard-driven
    // guard's ultrasonic edges never reach the engine's own nonlinearity, where they would fold back as aliasing
    bool guarded = false; float ceiling = 1.0f; Shaper guard; float gpL = 0, gpR = 0; std::array<Svf, 2> band;
    void prepare (std::unique_ptr<aw::Plugin> p, double sr, int maxBlock, bool withGuard = false, float guardCeiling = 1.0f)
    {
        fx = std::move (p); fx->setSampleRate (sr); dl.assign ((size_t) maxBlock + kSub, 0.0f); dr.assign ((size_t) maxBlock + kSub, 0.0f);
        guarded = withGuard; ceiling = guardCeiling; if (guarded) { guard.prepare (2); guard.setKind (Shaper::Kind::Tanh); }
        const float top = (float) juce::jmin (20000.0, sr * 0.45);
        band[0].prepare (sr); band[1].prepare (sr); band[0].set (sst::filters::CytomicSVF::Mode::Lowpass, top, 0.2346f); band[1].set (sst::filters::CytomicSVF::Mode::Lowpass, top, 0.6173f);
    }
    void reset() { guard.reset(); gpL = gpR = 0.0f; band[0].reset(); band[1].reset(); }
    static constexpr int latency (bool withGuard) { return withGuard ? Shaper::latencySamples : 0; }
    // process [i0, i0 + len) of L / R in place; a0 -> a1 is the engine amount across the sub-block
    inline void run (float* L, float* R, int i0, int len, float a0, float a1) noexcept
    {
        if (guarded)
        {
            for (int i = 0; i < len; ++i)
            {
                float l = L[i0 + i], r = R[i0 + i]; dl[(size_t) i] = gpL; dr[(size_t) i] = gpR; gpL = l; gpR = r;   // the untouched path, one sample late
                if (a0 > 0.0f || a1 > 0.0f) { guard.process (l, r, 1.0f / ceiling, 0.0f); l *= ceiling; r *= ceiling; band[0].process (l, r); band[1].process (l, r); } else guard.pass (l, r);
                L[i0 + i] = l; R[i0 + i] = r;
            }
            if (a0 <= 0.0f && a1 <= 0.0f) { std::copy (dl.data(), dl.data() + len, L + i0); std::copy (dr.data(), dr.data() + len, R + i0); return; }
        }
        else
        {
            if (a0 <= 0.0f && a1 <= 0.0f) return;   // out of the path (its state simply rests)
            std::copy (L + i0, L + i0 + len, dl.data()); std::copy (R + i0, R + i0 + len, dr.data());
        }
        fx->process (L + i0, R + i0, len);
        if (a0 >= 1.0f && a1 >= 1.0f) return;
        for (int i = 0; i < len; ++i) { const float a = a0 + (a1 - a0) * (float) (i + 1) / (float) len; L[i0 + i] = dl[(size_t) i] + (L[i0 + i] - dl[(size_t) i]) * a; R[i0 + i] = dr[(size_t) i] + (R[i0 + i] - dr[(size_t) i]) * a; }
    }
};
inline float fadeIn (float v, float width = 0.05f) noexcept { return juce::jlimit (0.0f, 1.0f, v / width); }
// the engines come in over the first quarter of their main knob, slowly enough that the crossfade itself never sounds
constexpr float kFade = 0.25f;

// ================================================================ 7 TUBE GLOW: Airwindows Tube2 (+ Console7Channel for MIC), bias, bass / treble, air shelf
class TubeGlowFx final : public CatalogEffect
{
public:
    TubeGlowFx() : CatalogEffect (uTube) {}
    bool isNonlinear() const override { return true; }
    int oversamplingFactor() const override { return 16; }
    void neutralise (std::vector<float>& v) const override { v[(size_t) id ("drive")] = 0.0f; v[(size_t) id ("bias")] = 0.0f; v[(size_t) id ("air")] = 0.0f; }
    void prepareEffect (double sampleRate, int maxBlock, int) override
    {
        tube.prepare (aw::makeTube2(), sampleRate, maxBlock, true, 0.97f); console.prepare (aw::makeConsole7Channel(), sampleRate, maxBlock); airF.prepare (sampleRate);
        lo.prepare (sampleRate); hi.prepare (sampleRate); dc.prepare (sampleRate); ag.prepare (sampleRate);
        pDrive = id ("drive"); pChar = id ("char"); pBass = id ("bass"); pTreble = id ("treble"); pBias = id ("bias"); pAir = id ("air"); pOut = id ("out");
        prevDrive = target (pDrive); prevAir = target (pAir); prevMic = choice (pChar) == 1 ? 1.0f : 0.0f;
    }
    void resetEffect() override { lo.reset(); hi.reset(); airF.reset(); dc.reset(); tube.reset(); ag.reset(); }
    int getLatencySamples() const override { return AwStage::latency (true); }
    void process (juce::dsp::AudioBlock<float> block, const ModulationContext& ctx) noexcept override
    {
        beginBlock (ctx);
        const int n = (int) block.getNumSamples(), ch = (int) block.getNumChannels();
        auto* L = block.getChannelPointer (0); auto* R = ch > 1 ? block.getChannelPointer (1) : L;
        using M = sst::filters::CytomicSVF::Mode;
        const float micTarget = choice (pChar) == 1 ? 1.0f : 0.0f;
        for (int i0 = 0; i0 < n; i0 += AwStage::kSub)
        {
            const int len = juce::jmin (AwStage::kSub, n - i0);
            const float drive = p (pDrive).skip (len), airV = p (pAir).skip (len), bias = p (pBias).skip (len);
            const float bass = p (pBass).skip (len), treble = p (pTreble).skip (len), out = p (pOut).skipGain (len);
            const float mic = prevMic + juce::jlimit (-0.02f, 0.02f, micTarget - prevMic);   // LINE <-> MIC glides over ~50 sub-blocks
            // bias: a DC offset into the tube (asymmetry, even harmonics), removed again after it
            const float off = bias * 0.25f * fadeIn (drive);
            for (int i = i0; i < i0 + len; ++i) { agIn[0] = agIn[1]; agIn[1] = 0.5f * (L[i] + R[i]); ag.feedIn (L[i], R[i]); L[i] += off; R[i] += off; }
            // DRIVE = how hard the tube is hit: our pre-gain (1x .. 2.5x) into the guard and Tube2 (Input unpadded, Tube full: its smoothest, lowest-order curve).
            // Tube2's own Tube knob picks a whole-number curve order, so sweeping it would step; the level into it glides instead.
            tube.fx->setParameter (0, 1.0f); tube.fx->setParameter (1, 1.0f);
            { const float g0 = 1.0f + kHit * prevDrive, g1 = 1.0f + kHit * drive; for (int i = i0; i < i0 + len; ++i) { const float g = g0 + (g1 - g0) * (float) (i - i0 + 1) / (float) len; L[i] *= g; R[i] *= g; preG[i - i0] = g; } }
            tube.run (L, R, i0, len, fadeIn (prevDrive, kFade), fadeIn (drive, kFade));
            for (int i = i0; i < i0 + len; ++i) { L[i] /= preG[i - i0]; R[i] /= preG[i - i0]; }
            for (int i = i0; i < i0 + len; ++i) { L[i] -= off; R[i] -= off; float l = L[i], r = R[i]; if (std::abs (bias) > 1.0e-4f || dcActive) { dc.process (l, r); L[i] = l; R[i] = r; } }
            dcActive = std::abs (bias) > 1.0e-4f;
            console.fx->setParameter (0, 0.772f);                     // the console channel at its unity fader
            console.run (L, R, i0, len, prevMic * fadeIn (prevDrive, kFade), mic * fadeIn (drive, kFade));
            if (std::abs (bass) > 1.0e-4f || std::abs (treble) > 1.0e-4f || p (pBass).isSmoothing() || p (pTreble).isSmoothing())
            {
                lo.set (M::LowShelf, 120.0f, 0.3f, dbToGain (bass)); hi.set (M::HighShelf, 5000.0f, 0.3f, dbToGain (treble));
                for (int i = i0; i < i0 + len; ++i) { float l = L[i], r = R[i]; lo.process (l, r); hi.process (l, r); L[i] = l; R[i] = r; }
            }
            // AIR: a broad high shelf at 12 kHz, up to +9 dB (Airwindows Air3 measures 'air' in samples and blows up at oversampled rates:
            // DECISIONS.md 15). At 0 the shelf's gain is exactly 1 and it is an identity.
            if (airV > 1.0e-5f || p (pAir).isSmoothing())
            {
                airF.set (M::HighShelf, 12000.0f, 0.25f, dbToGain (9.0f * airV));
                for (int i = i0; i < i0 + len; ++i) { float l = L[i], r = R[i]; airF.process (l, r); L[i] = l; R[i] = r; }
            }
            for (int i = i0; i < i0 + len; ++i)
            {
                // Tube2 "peaks at 0 dB exactly", which lifts quiet material: a slow RMS match against the input, faded in with DRIVE
                const float a = fadeIn (prevDrive, kFade) + (fadeIn (drive, kFade) - fadeIn (prevDrive, kFade)) * (float) (i - i0 + 1) / (float) len;
                const float g = ag.gain (L[i], R[i]); const float gm = 1.0f + (g - 1.0f) * a;
                L[i] *= gm * out; R[i] *= gm * out;
            }
            prevDrive = drive; prevAir = airV; prevMic = mic;
        }
        juce::ignoreUnused (ch);
    }
private:
    AwStage tube, console; Svf lo, hi, airF; DcBlock dc; AutoGain ag; float agIn[2] { 0, 0 }, preG[AwStage::kSub] {}; bool dcActive = false; static constexpr float kHit = 1.5f; float prevDrive = 0, prevAir = 0, prevMic = 0;
    int pDrive = 0, pChar = 0, pBass = 0, pTreble = 0, pBias = 0, pAir = 0, pOut = 0;
};

// ================================================================ 8 SOUL CRUSH: chowdsp's compressor (feed-forward gain computer; peak or program-dependent
// RMS detection), the shared anti-aliased stage for grit, darkness, parallel blend. (Airwindows Pressure5 was tried first: at full
// CRUSH it shapes the waveform itself, which folds back as aliasing near -38 dBFS, and its time constants count samples, so it cannot
// be oversampled: DECISIONS.md 15.)
class SoulCrushFx final : public CatalogEffect
{
public:
    SoulCrushFx() : CatalogEffect (uCrush) {}
    bool isNonlinear() const override { return true; }
    int oversamplingFactor() const override { return 4; }
    bool changesLevelOnPurpose() const override { return true; }   // a compressor
    int getLatencySamples() const override { return Shaper::latencySamples; }
    void neutralise (std::vector<float>& v) const override { v[(size_t) id ("crush")] = 0.0f; v[(size_t) id ("dark")] = 0.0f; }
    void prepareEffect (double sampleRate, int maxBlock, int ch) override
    {
        comp.prepare ({ sampleRate, (juce::uint32) maxBlock, 2 }); key.setSize (2, maxBlock); dry.setSize (2, maxBlock); lp.prepare (sampleRate);
        grit.prepare (ch); grit.setKind (Shaper::Kind::Tanh);
        pCrush = id ("crush"); pDark = id ("dark"); pRel = id ("release"); pProg = id ("prog"); pBlend = id ("blend"); pOut = id ("out");
    }
    void resetEffect() override { lp.reset(); grit.reset(); comp.levelDetector.reset(); for (auto& d : dpad) d = 0.0f; }
    void process (juce::dsp::AudioBlock<float> block, const ModulationContext& ctx) noexcept override
    {
        beginBlock (ctx);
        const int n = (int) block.getNumSamples(), ch = (int) block.getNumChannels();
        auto* L = block.getChannelPointer (0); auto* R = ch > 1 ? block.getChannelPointer (1) : L;
        using M = sst::filters::CytomicSVF::Mode;
        for (int i0 = 0; i0 < n; i0 += kSub)
        {
            const int len = juce::jmin (kSub, n - i0);
            const float crush = p (pCrush).skip (len), dark = p (pDark).skip (len), rel = p (pRel).skip (len);
            // CRUSH: threshold from 0 to -36 dB, ratio 1:1 to 20:1, fast attack; make-up gain brings the squashed level back up
            comp.levelDetector.setMode (on (pProg) ? 1 : 0);   // PROGRAM: RMS detection, whose release follows the material
            comp.params.attackMs = 0.5f + (1.0f - crush) * 9.5f; comp.params.releaseMs = rel; comp.params.thresholdDB = -36.0f * crush;
            comp.params.ratio = 1.0f + 19.0f * crush * crush; comp.params.kneeDB = 6.0f; comp.params.autoMakeup = false;
            const float makeup = dbToGain (18.0f * crush * (1.0f - 1.0f / comp.params.ratio));
            for (int i = 0; i < len; ++i) { key.setSample (0, i, L[i0 + i]); key.setSample (1, i, R[i0 + i]); dry.setSample (0, i0 + i, L[i0 + i]); dry.setSample (1, i0 + i, R[i0 + i]); }
            float* chans[2] { L + i0, R + i0 }; const float* keys[2] { key.getReadPointer (0), key.getReadPointer (1) };
            comp.processBlock (chowdsp::BufferView<float> (chans, 2, len), chowdsp::BufferView<const float> (keys, 2, len));
            for (int i = i0; i < i0 + len; ++i)
            {
                const float a = fadeIn (crush);   // engine amount (CRUSH 0 = untouched)
                float l = L[i] * makeup, r = R[i] * makeup;
                // grit: the squashed signal driven into the shared stage, harder with CRUSH (unity small-signal gain)
                const float g = 1.0f + 3.0f * crush;
                if (crush > 0.0005f) { grit.process (l, r, g, 0.0f); l /= g; r /= g; } else grit.pass (l, r);
                const float dl = dpad[0], dr = dpad[1]; dpad[0] = dry.getSample (0, i); dpad[1] = dry.getSample (1, i);   // dry, aligned with the stage
                L[i] = dl + (l - dl) * a; R[i] = dr + (r - dr) * a;
            }
            if (dark > 1.0e-4f || p (pDark).isSmoothing())
            {
                lp.set (M::Lowpass, 20000.0f * std::pow (0.075f, dark), 0.15f);   // 20 kHz down to 1.5 kHz
                for (int i = i0; i < i0 + len; ++i) { float l = L[i], r = R[i]; lp.process (l, r); const float a = fadeIn (dark, 0.02f); L[i] += (l - L[i]) * a; R[i] += (r - R[i]) * a; }
            }
        }
        // parallel blend against the untouched input, then the output trim
        for (int i = 0; i < n; ++i)
        {
            const float b = p (pBlend).next(), out = p (pOut).nextGain();
            const float dl = bpad[0], dr = bpad[1]; bpad[0] = dry.getSample (0, i); bpad[1] = dry.getSample (1, i);
            L[i] = (dl + (L[i] - dl) * b) * out; R[i] = (dr + (R[i] - dr) * b) * out;
        }
    }
private:
    static constexpr int kSub = 32;
    using Detector = chowdsp::compressor::CompressorLevelDetector<float, types_list::TypesList<chowdsp::compressor::PeakDetector, chowdsp::compressor::RMSDetector>>;
    using Computer = chowdsp::compressor::GainComputer<float, chowdsp::compressor::FeedForwardCompGainComputer<float>>;
    chowdsp::compressor::MonoCompressor<float, Detector, Computer> comp; juce::AudioBuffer<float> key, dry; Svf lp; Shaper grit; float dpad[2] { 0, 0 }, bpad[2] { 0, 0 };
    int pCrush = 0, pDark = 0, pRel = 0, pProg = 0, pBlend = 0, pOut = 0;
};

// ================================================================ 10 FATTEN: Airwindows Density (+ FathomFive sub, DrumSlam, Tube2 by mode), colour tilt,
// crunch, input drive, punch, low clean; a slow RMS match keeps "fatter" from just meaning "louder"
class FattenFx final : public CatalogEffect
{
public:
    FattenFx() : CatalogEffect (uFatten) {}
    bool isNonlinear() const override { return true; }
    int oversamplingFactor() const override { return 16; }
    int getLatencySamples() const override { return AwStage::latency (true) + Shaper::latencySamples; }   // the guard and CRUNCH
    void neutralise (std::vector<float>& v) const override { v[(size_t) id ("fat")] = 0.0f; v[(size_t) id ("crunch")] = 0.0f; v[(size_t) id ("color")] = 0.0f; v[(size_t) id ("drive")] = 0.0f; }
    void prepareEffect (double sampleRate, int maxBlock, int ch) override
    {
        dens.prepare (aw::makeDensity(), sampleRate, maxBlock, true, 1.0f); dens2.prepare (aw::makeDensity(), sampleRate, maxBlock); sub.prepare (aw::makeFathomFive(), sampleRate, maxBlock);
        slam.prepare (aw::makeDrumSlam(), sampleRate, maxBlock); tube.prepare (aw::makeTube2(), sampleRate, maxBlock);
        crunch.prepare (ch); crunch.setKind (Shaper::Kind::Tanh); tiltLo.prepare (sampleRate); tiltHi.prepare (sampleRate); body.prepare (sampleRate);
        split.prepare (sampleRate); ag.prepare (sampleRate); punchEnvF.prepare (sampleRate); punchEnvS.prepare (sampleRate); punchEnvF.setTimes (0.5f, 30.0f); punchEnvS.setTimes (20.0f, 200.0f);
        lows.setSize (2, maxBlock); dry.setSize (2, maxBlock);
        pFat = id ("fat"); pMode = id ("mode"); pColor = id ("color"); pCrunch = id ("crunch"); pDrive = id ("drive"); pPunch = id ("punch"); pLowClean = id ("lowclean"); pOut = id ("out");
        prevFat = target (pFat);
    }
    void resetEffect() override { crunch.reset(); tiltLo.reset(); tiltHi.reset(); body.reset(); split.reset(); ag.reset(); punchEnvF.reset(); punchEnvS.reset(); dens.reset(); for (auto& c : lowPad) c[0] = c[1] = 0.0f; }
    void process (juce::dsp::AudioBlock<float> block, const ModulationContext& ctx) noexcept override
    {
        beginBlock (ctx);
        const int n = (int) block.getNumSamples(), ch = (int) block.getNumChannels();
        auto* L = block.getChannelPointer (0); auto* R = ch > 1 ? block.getChannelPointer (1) : L;
        using M = sst::filters::CytomicSVF::Mode;
        const int mode = choice (pMode); const bool punch = on (pPunch) || mode == 6, lowClean = on (pLowClean);
        // LOW CLEAN: the lows under 120 Hz go round the engines untouched (complementary split, so the sum is exact)
        split.set (M::Lowpass, 120.0f, 0.3f);
        for (int i = 0; i < n; ++i)
        {
            float l = L[i], r = R[i]; dry.setSample (0, i, l); dry.setSample (1, i, r);
            if (lowClean) { split.process (l, r); lows.setSample (0, i, l); lows.setSample (1, i, r); L[i] -= l; R[i] -= r; } else { lows.setSample (0, i, 0.0f); lows.setSample (1, i, 0.0f); }
        }
        for (int i0 = 0; i0 < n; i0 += AwStage::kSub)
        {
            const int len = juce::jmin (AwStage::kSub, n - i0);
            const float fat = p (pFat).skip (len), color = p (pColor).skip (len), cr = p (pCrunch).skip (len), inDb = p (pDrive).skip (len), out = p (pOut).skipGain (len);
            const float a0 = fadeIn (prevFat, kFade), a1 = fadeIn (fat, kFade);
            // input drive into the engines, taken back off after them (DRIVE changes how hard they work, not the level)
            const float ig = dbToGain (inDb);
            for (int i = i0; i < i0 + len; ++i) { ag.feedIn (dry.getSample (0, i), dry.getSample (1, i)); L[i] *= ig; R[i] *= ig; }
            // Density: Airwindows' own scale is -1..+4 (A * 5 - 1). FAT walks it from 0 (clean) to 1 (full): Density's blend between its
            // passes wraps at every whole number, so a sweep across 1 would jump; STACK and SAUSAGE go further by running a second pass
            dens.fx->setParameter (0, 0.2f + fat * 0.2f); dens.fx->setParameter (1, 0.0f); dens.fx->setParameter (2, 1.0f); dens.fx->setParameter (3, 1.0f);
            dens.run (L, R, i0, len, mode != 3 ? a0 : 0.0f, mode != 3 ? a1 : 0.0f);   // (always called: its guard keeps the path's one-sample pad)
            if (mode == 5 || mode == 7) { dens2.fx->setParameter (0, 0.2f + fat * 0.2f); dens2.fx->setParameter (1, 0.0f); dens2.fx->setParameter (2, 1.0f); dens2.fx->setParameter (3, 1.0f); dens2.run (L, R, i0, len, a0, a1); }
            if (mode == 1) { sub.fx->setParameter (0, 1.0f); sub.fx->setParameter (1, 0.5f); sub.fx->setParameter (2, 0.5f); sub.fx->setParameter (3, 0.5f + 0.25f * fat); sub.run (L, R, i0, len, a0, a1); }
            if (mode == 3 || mode == 7) { slam.fx->setParameter (0, fat); slam.fx->setParameter (1, 1.0f); slam.fx->setParameter (2, 1.0f); slam.run (L, R, i0, len, a0, a1); }
            if (mode == 4) { tube.fx->setParameter (0, 1.0f); tube.fx->setParameter (1, 0.2f + 0.8f * fat); tube.run (L, R, i0, len, a0, a1); }
            for (int i = i0; i < i0 + len; ++i)
            {
                float l = L[i] / ig, r = R[i] / ig;
                // CRUNCH: the shared anti-aliased stage, unity small-signal gain
                const float g = 1.0f + cr * 5.0f; float cl = l, crr = r;
                if (cr > 0.0005f) { crunch.process (cl, crr, g, 0.0f); cl /= g; crr /= g; } else crunch.pass (cl, crr);
                l = cl; r = crr;
                // PUNCH: fast envelope over slow envelope lifts the attacks
                if (punch) { const float m = juce::jmax (std::abs (l), std::abs (r)); const float f = punchEnvF.next (m), s = punchEnvS.next (m); const float k = juce::jlimit (1.0f, 2.0f, (f + 1.0e-6f) / (s + 1.0e-6f)); const float pg = 1.0f + (k - 1.0f) * fat * 0.8f; l *= pg; r *= pg; }
                L[i] = l; R[i] = r;
            }
            // COLOR: tilt, + warms (low shelf up, highs down), - brightens; BODY mode adds a 200 Hz bump
            if (std::abs (color) > 1.0e-4f || p (pColor).isSmoothing() || mode == 2)
            {
                tiltLo.set (M::LowShelf, 250.0f, 0.3f, dbToGain (color * 4.0f)); tiltHi.set (M::HighShelf, 3000.0f, 0.3f, dbToGain (-color * 4.0f));
                body.set (M::Bell, 200.0f, 0.4f, dbToGain (mode == 2 ? fat * 6.0f : 0.0f));
                for (int i = i0; i < i0 + len; ++i) { float l = L[i], r = R[i]; tiltLo.process (l, r); tiltHi.process (l, r); body.process (l, r); L[i] = l; R[i] = r; }
            }
            for (int i = i0; i < i0 + len; ++i)
            {
                // the slow RMS match against the input, faded in with the effect so FAT 0 is untouched
                const float a = a0 + (a1 - a0) * (float) (i - i0 + 1) / (float) len;
                const float ga = ag.gain (L[i], R[i]); const float gm = 1.0f + (ga - 1.0f) * a;
                L[i] = (L[i] * gm + delayedLow (0, i)) * out; R[i] = (R[i] * gm + delayedLow (1, i)) * out;
            }
            prevFat = fat;
        }
    }
private:
    // the clean lows line up with the engine path's one-sample stage pad
    float delayedLow (int c, int i) noexcept { const float v = lows.getSample (c, i); const float o = lowPad[c][1]; lowPad[c][1] = lowPad[c][0]; lowPad[c][0] = v; return o; }
    AwStage dens, dens2, sub, slam, tube; Shaper crunch; Svf tiltLo, tiltHi, body, split; AutoGain ag; EnvelopeFollower punchEnvF, punchEnvS;
    juce::AudioBuffer<float> lows, dry; float lowPad[2][2] {}; float prevFat = 0;
    int pFat = 0, pMode = 0, pColor = 0, pCrunch = 0, pDrive = 0, pPunch = 0, pLowClean = 0, pOut = 0;
};

// ================================================================ 11 BASS LAB: perfect-reconstruction 3-band split, mono-below-X, Airwindows BassKit sub,
// low-band-only saturation, growl, edge, translate
class BassLabFx final : public CatalogEffect
{
public:
    BassLabFx() : CatalogEffect (uBassLab) {}
    bool isNonlinear() const override { return true; }
    int oversamplingFactor() const override { return 16; }
    int getLatencySamples() const override { return Shaper::latencySamples; }
    void neutralise (std::vector<float>& v) const override { for (auto* k : { "sub", "growl", "edge", "translate", "lowsat", "gmove" }) v[(size_t) id (k)] = 0.0f; v[(size_t) id ("lowwidth")] = 1.0f; }
    void prepareEffect (double sampleRate, int maxBlock, int ch) override
    {
        kit.prepare (aw::makeBassKit(), sampleRate, maxBlock);
        for (auto& f : lp1) f.prepare (sampleRate); for (auto& f : lp2) f.prepare (sampleRate); growlF.prepare (sampleRate); transHp.prepare (sampleRate);
        lowSat.prepare (ch); lowSat.setKind (Shaper::Kind::Tanh); edgeSat.prepare (ch); edgeSat.setKind (Shaper::Kind::Soft5); trans.prepare (ch); trans.setKind (Shaper::Kind::Rect); growlSat.prepare (ch); growlSat.setKind (Shaper::Kind::Tanh);
        lfo.prepare (sampleRate); ag.prepare (sampleRate); lowBuf.setSize (2, maxBlock);
        pXlo = id ("xlo"); pXhi = id ("xhi"); pWidth = id ("lowwidth"); pSub = id ("sub"); pGrowl = id ("growl"); pGf = id ("gfreq"); pGmove = id ("gmove"); pGdiv = id ("gdiv");
        pEdge = id ("edge"); pTrans = id ("translate"); pLow = id ("low"); pMid = id ("mid"); pHigh = id ("high"); pLowSat = id ("lowsat"); pAuto = id ("auto"); pOut = id ("out");
        prevSub = target (pSub);
    }
    void resetEffect() override { for (auto& f : lp1) f.reset(); for (auto& f : lp2) f.reset(); growlF.reset(); transHp.reset(); lowSat.reset(); edgeSat.reset(); trans.reset(); growlSat.reset(); mp[0] = mp[1] = 0.0f; act = 0.0f; lfo.reset(); ag.reset(); }
    void process (juce::dsp::AudioBlock<float> block, const ModulationContext& ctx) noexcept override
    {
        beginBlock (ctx);
        const int n = (int) block.getNumSamples(), ch = (int) block.getNumChannels();
        auto* L = block.getChannelPointer (0); auto* R = ch > 1 ? block.getChannelPointer (1) : L;
        using M = sst::filters::CytomicSVF::Mode;
        lfo.setShape (LfoShape::Sine); lfo.setSync (true, choice (pGdiv)); lfo.beginBlock (ctx.tempo);
        const bool autoGain = on (pAuto);
        for (int i0 = 0; i0 < n; i0 += AwStage::kSub)
        {
            const int len = juce::jmin (AwStage::kSub, n - i0);
            const float xlo = p (pXlo).skip (len), xhi = p (pXhi).skip (len), gf = p (pGf).skip (len), gmove = p (pGmove).skip (len), subV = p (pSub).skip (len);
            // the split: two cascaded Butterworth sections (24 dB/oct) for each low-pass; each upper band is the remainder, so the bands always sum back
            // to the input exactly (the null test holds to the bit) and the low band is steep where the bass work happens
            lp1[0].set (M::Lowpass, xlo, 0.2929f); lp1[1].set (M::Lowpass, xlo, 0.2929f);
            lp2[0].set (M::Lowpass, xhi, 0.2929f); lp2[1].set (M::Lowpass, xhi, 0.2929f);
            const float lv = lfo.next(); for (int k = 1; k < len; ++k) lfo.next();
            growlF.set (M::Bandpass, juce::jlimit (100.0f, 4000.0f, gf * std::pow (2.0f, gmove * lv)), 0.85f);
            transHp.set (M::Highpass, xlo * 1.5f, 0.2f);
            for (int i = i0; i < i0 + len; ++i)
            {
                float x[2] { L[i], R[i] }, lo[2] { L[i], R[i] };
                lp1[0].process (lo[0], lo[1]); lp1[1].process (lo[0], lo[1]);
                float rest[2] { x[0] - lo[0], x[1] - lo[1] }, mid[2] { rest[0], rest[1] };
                lp2[0].process (mid[0], mid[1]); lp2[1].process (mid[0], mid[1]);
                float hi[2] { rest[0] - mid[0], rest[1] - mid[1] };
                // ---- low band: mono below X, saturation only here
                const float w = p (pWidth).next(); const float m = 0.5f * (lo[0] + lo[1]), s = 0.5f * (lo[0] - lo[1]) * w; lo[0] = m + s; lo[1] = m - s;
                const float ls = p (pLowSat).next();
                float tl = lo[0], tr = lo[1];
                if (ls > 0.0005f) { const float g = 1.0f + ls * 8.0f; lowSat.process (tl, tr, g, 0.0f); tl /= g; tr /= g; } else lowSat.pass (tl, tr);
                // TRANSLATE: the low band's harmonics (full-wave rectified = octave up), high-passed, added to the mids for small speakers
                const float tv = p (pTrans).next(); float hl = lo[0], hr = lo[1];
                if (tv > 0.0005f) { trans.process (hl, hr, 1.0f, 0.0f); transHp.process (hl, hr); } else { trans.pass (hl, hr); hl = hr = 0.0f; }
                lowBuf.setSample (0, i, tl); lowBuf.setSample (1, i, tr);
                // ---- mid band: growl (a moving resonance driven into the shared stage), translate
                const float gv = p (pGrowl).next(); float gl = mid[0], gr = mid[1];
                growlF.process (gl, gr);   // (runs always, so its state is warm when GROWL comes up)
                if (gv > 0.0005f) { const float g = 1.0f + gv * 10.0f; growlSat.process (gl, gr, g, 0.0f); gl /= g; gr /= g; } else { growlSat.pass (gl, gr); gl = gr = 0.0f; }
                float ml = mid[0], mr = mid[1]; midPad2 (ml, mr);
                ml += gl * gv + hl * tv * 2.0f; mr += gr * gv + hr * tv * 2.0f;
                // ---- high band: edge (soft saturation)
                const float ev = p (pEdge).next(); float el = hi[0], er = hi[1];
                if (ev > 0.0005f) { const float g = 1.0f + ev * 6.0f; edgeSat.process (el, er, g, 0.0f); el /= g; er /= g; } else edgeSat.pass (el, er);
                const float gLo = p (pLow).nextGain(), gMid = p (pMid).nextGain(), gHi = p (pHigh).nextGain();
                L[i] = tl * gLo + ml * gMid + el * gHi; R[i] = tr * gLo + mr * gMid + er * gHi;
                if (autoGain) ag.feedIn (x[0], x[1]);
            }
            // SUB: Airwindows BassKit's sub-octave synth (its SubOut; BassOut and its own drive left neutral), faded in with the knob
            kit.fx->setParameter (0, 0.5f); kit.fx->setParameter (1, 0.5f); kit.fx->setParameter (2, 0.5f); kit.fx->setParameter (3, 0.5f + 0.5f * subV);
            kit.run (L, R, i0, len, fadeIn (prevSub), fadeIn (subV));
            // GAIN COMPENSATION: the slow RMS match, applied only as far as something is actually changing the sound (so a neutral
            // Bass Lab is an exact pass-through)
            const float activity = fadeIn (juce::jmax (juce::jmax (subV, target (pGrowl), target (pEdge)), juce::jmax (target (pTrans), target (pLowSat), 1.0f - target (pWidth)),
                                           juce::jmax (std::abs (target (pLow)), std::abs (target (pMid)), std::abs (target (pHigh))) / 6.0f), 0.05f);
            for (int i = i0; i < i0 + len; ++i)
            {
                const float out = p (pOut).nextGain();
                act += 0.001f * (activity - act);
                float ga = 1.0f; if (autoGain) { ga = ag.gain (L[i], R[i]); ga = 1.0f + (ga - 1.0f) * act; }
                L[i] *= ga * out; R[i] *= ga * out;
            }
            prevSub = subV;
        }
    }
private:
    // every band leaves through exactly one stage-sized pad so the bands stay aligned whatever is switched on
    void midPad2 (float& l, float& r) noexcept { const float tl = mp[0], tr = mp[1]; mp[0] = l; mp[1] = r; l = tl; r = tr; }
    AwStage kit; std::array<Svf, 2> lp1, lp2; Svf growlF, transHp; Shaper lowSat, edgeSat, trans, growlSat; Lfo lfo; AutoGain ag; juce::AudioBuffer<float> lowBuf;
    float mp[2] { 0, 0 }, prevSub = 0, act = 0;
    int pXlo = 0, pXhi = 0, pWidth = 0, pSub = 0, pGrowl = 0, pGf = 0, pGmove = 0, pGdiv = 0, pEdge = 0, pTrans = 0, pLow = 0, pMid = 0, pHigh = 0, pLowSat = 0, pAuto = 0, pOut = 0;
};
} // namespace phantom
