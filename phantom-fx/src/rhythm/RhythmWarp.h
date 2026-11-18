// SPDX-License-Identifier: AGPL-3.0-or-later
// 4 WARP: a drawn, tempo-synced bend lane (Ghost Box's bend lane model). PITCH: Signalsmith Stretch (MIT) transposes and keeps time
// (KEEP FORMANTS = its own formant compensation), latency = the stretcher's input + output latency, reported exactly. TAPE: varispeed,
// a read head on TIME-12's buffer whose delay depends only on the lane phase (no drift, never reads ahead of the input), no latency.
// A flat lane (or AMOUNT 0) is an exact, latency-aligned pass-through; the engine fades in (10 ms) when the lane bends.
// Phase 6: Rubber Band (GPL-2.0-or-later, R3 engine, real time) is the challenger behind the same interface (brief: challenger procedure);
// PHANTOM_WARP_RUBBERBAND picks the engine the plugin uses, the tests build both. Switching on: the slot plays the aligned dry while the
// stretcher fills (warmupSamples), then fades the unit in, so there is no silent gap (DECISIONS.md 85).
#pragma once
#include "RhythmCore.h"
#include "BendLane.h"
#include "../effects/ColourSst.h"
#include "signalsmith-stretch/signalsmith-stretch.h"
#include "../master/Engines.h"
#ifndef PHANTOM_WARP_RUBBERBAND
 #define PHANTOM_WARP_RUBBERBAND 0
#endif

namespace phantom
{
class WarpFx final : public CatalogEffect
{
public:
    struct Lane { bendlane::Shape shape; float maxAbs = 1.0f; };
    // engine: -1 = the build's choice, 0 = Signalsmith Stretch, 1 = Rubber Band
    explicit WarpFx (int engine = -1) : CatalogEffect (uWarp) { rbEngine = (engine < 0 ? PHANTOM_WARP_RUBBERBAND : engine) == 1; model = bendlane::defaultShape(); Lane l; l.shape = model; l.maxAbs = maxAbsOf (model); published.publishAll (l); }
    bool isRhythmic() const override { return true; }
    int getLatencySamples() const override { return tapeMode ? 0 : lat; }
    int warmupSamples() const override { return tapeMode ? 0 : 2 * lat + (int) (0.012 * sr); }
    const char* engineName() const { return rbEngine ? "RUBBER BAND" : "SIGNALSMITH"; }
    void neutralise (std::vector<float>& v) const override { if (iAmount >= 0 && iAmount < (int) v.size()) v[(size_t) iAmount] = 0.0f; }
    void updateParameters (const ModulationContext& ctx) override { CatalogEffect::updateParameters (ctx); tapeMode = choice (iMode) == 1; }

    bool hasData() const override { return true; }
    juce::var getData() const override
    { auto* o = new juce::DynamicObject(); o->setProperty ("lane", model.toString()); o->setProperty ("snap", model.snap); o->setProperty ("grid", model.grid); return juce::var (o); }
    void setData (const juce::var& v) override
    {
        model = v["lane"].isString() ? bendlane::Shape::fromString (v["lane"].toString(), v.hasProperty ("snap") ? (int) v["snap"] : 1, v.hasProperty ("grid") ? (int) v["grid"] : 2) : bendlane::defaultShape();
        push();
    }
    const bendlane::Shape& lane() const { return model; }
    void setLane (const bendlane::Shape& s) { model = s; model.sanitise(); push(); }
    std::atomic<float> uiPos { -1.0f }, uiSemis { 0.0f };
    double debugPhase = -1.0;   // test hook: the lane phase heard at the last sample of the last block

protected:
    void prepareEffect (double sampleRate, int, int) override
    {
        iMode = id ("mode"); iAmount = id ("amount"); iLen = id ("length"); iRange = id ("range"); iFormant = id ("formant");
        // shorter blocks than Signalsmith's default preset (as Ghost Box's WARP): the transpose can move every 12.5 ms, latency ~50 ms
        st.configure (2, (int) std::lround (sampleRate * 0.05), (int) std::lround (sampleRate * 0.0125), false);
        lat = st.inputLatency() + st.outputLatency();
        if (rbEngine) { rb.prepare (sampleRate, kChunk); lat = rb.latency(); }
        tape.prepare ((int) (sampleRate * kTapeSeconds) + 64);
        for (auto& b : outBuf) b.assign ((size_t) kMaxBlock, 0.0f);
        resetEffect();
    }
    void resetEffect() override { written = 0; st.reset(); if (rbEngine) rb.reset(); tape.clear(); tableSeen = false; tableScale = -1.0f; wet = 0.0f; tapeD = -1.0; lastMode = -1; }
    void process (juce::dsp::AudioBlock<float> block, const ModulationContext& ctx) noexcept override
    {
        using namespace rhythm;
        beginBlock (ctx);
        if (published.fetch()) tableSeen = false;
        const auto& ln = published.read();
        const int n = (int) block.getNumSamples(), nch = juce::jmin (2, (int) block.getNumChannels());
        float* L = block.getChannelPointer (0); float* R = nch > 1 ? block.getChannelPointer (1) : L;
        HostClock clk; clk.set (ctx.tempo);
        const int md = tapeMode ? 1 : 0;
        if (md != lastMode) { st.reset(); if (rbEngine) rb.reset(); wet = 0.0f; tapeD = -1.0; lastMode = md; }
        const double lenBeats = bendlane::barsOf (choice (iLen)) * ctx.tempo.beatsPerBar();
        const float range = bendlane::rangeOf (choice (iRange));
        const float wf = 1.0f - (float) std::exp (-1.0 / (0.010 * sr * 0.5));
        double phase = 0; float semis = 0;
        if (md == 0)
        {
            st.setFormantFactor (1.0f, on (iFormant));
            const double inLat = (double) (rbEngine ? rb.inputLatency() : st.inputLatency());
            for (int off = 0; off < n; off += kChunk)
            {
                const int m = std::min (kChunk, n - off);
                const float scale = range * juce::jlimit (0.0f, 1.0f, p (iAmount).skip (m));
                const bool bends = scale * ln.maxAbs > 1.0e-4f;
                phase = positiveMod ((clk.ppqAt (off) - inLat / clk.spb) / lenBeats, 1.0);
                semis = ln.shape.valueAt (phase) * scale;
                float* in[2] { L + off, R + off }; float* out[2] { outBuf[0].data() + off, outBuf[1].data() + off };
                if (rbEngine) { rb.setPitch (std::exp2 (semis / 12.0), on (iFormant)); rb.process (in, out, m); }
                else { st.setTransposeFactor (std::exp2 (semis / 12.0f)); st.process (in, m, out, m); }
                for (int i = off; i < off + m; ++i)
                {
                    tape.write (L[i], R[i]); ++written;
                    // after a reset (switching on) nothing is heard until the buffer covers the latency, then a 5 ms fade in: no start-up transient
                    const float gIn = (float) juce::jlimit (0.0, 1.0, (double) (written - 2 * lat) / (0.010 * sr));   // the stretcher settles over its first two blocks
                    wet += wf * ((bends ? 1.0f : 0.0f) - wet); if (wet < 1.0e-6f) wet = 0.0f; if (wet > 0.999999f) wet = 1.0f;
                    const float dl = tape.read (0, (double) lat), dr = tape.read (1, (double) lat);   // the dry, aligned to the engine
                    L[i] = gIn * (dl + wet * (outBuf[0][(size_t) i] - dl)); if (nch > 1) R[i] = gIn * (dr + wet * (outBuf[1][(size_t) i] - dr));
                }
            }
            phase = positiveMod ((clk.ppqAt (n) - (double) lat / clk.spb) / lenBeats, 1.0);
            semis = ln.shape.valueAt (phase) * range * p (iAmount).current;
        }
        else
        {
            const float scale = range * juce::jlimit (0.0f, 1.0f, p (iAmount).skip (n));
            if (! tableSeen || std::abs (scale - tableScale) > 1.0e-5f) { table.build (ln.shape, scale); tableSeen = true; tableScale = scale; }
            const double cyc = lenBeats * clk.spb, maxD = tape.size() - 8.0, headRoom = std::min (maxD, (double) table.maxO * cyc);
            for (int i = 0; i < n; ++i)
            {
                tape.write (L[i], R[i]); ++written;
                phase = positiveMod (clk.ppqAt (i) / lenBeats, 1.0);
                const double D = juce::jlimit (0.0, maxD, headRoom - (double) table.at (phase) * cyc);
                // the head glides to a new delay at most 1 sample per sample (a tempo or length change bends, never clicks)
                tapeD = tapeD < 0 ? D : tapeD + juce::jlimit (-1.0, 1.0, D - tapeD);
                const float a = tape.read (0, tapeD), b = tape.read (1, tapeD);
                const float gIn = (float) juce::jlimit (0.0, 1.0, ((double) written - tapeD) / (0.005 * sr));   // never the cleared buffer
                L[i] = tapeD < 1.0e-3 ? L[i] : a * gIn; if (nch > 1) R[i] = tapeD < 1.0e-3 ? R[i] : b * gIn;
            }
            semis = ln.shape.valueAt (phase) * scale;
        }
        debugPhase = phase;
        uiPos.store ((float) phase, std::memory_order_relaxed); uiSemis.store (semis, std::memory_order_relaxed);
    }

private:
    static float maxAbsOf (const bendlane::Shape& s) { float m = 0; for (int k = 0; k <= 512; ++k) m = std::max (m, std::abs (s.valueAt (k / 512.0))); return m; }
    void push() { auto& e = published.edit(); e.shape = model; e.maxAbs = maxAbsOf (model); published.publish(); }
    static constexpr double kTapeSeconds = 12.0; static constexpr int kChunk = 32;
    bendlane::Shape model;   // message thread
    rhythm::Published<Lane> published;
    signalsmith::stretch::SignalsmithStretch<float> st { 0x6a057 };
    RubberBandBox rb; bool rbEngine = false;
    rhythm::TapeBuffer tape; std::array<std::vector<float>, 2> outBuf;
    bendlane::TapeTable table; bool tableSeen = false; float tableScale = -1.0f, wet = 0.0f; double tapeD = -1.0; long long written = 0;
    int lat = 0, lastMode = -1, iMode = 0, iAmount = 1, iLen = 2, iRange = 3, iFormant = 4; std::atomic<bool> tapeMode { false };
};
} // namespace phantom
