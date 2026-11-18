// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX: AnClark's re-creations of two Kjaerhus Audio "Classic" freeware plugins (GPL-3.0, vendored unchanged in
// third_party/kjaerhus, hosted through a small DPF shim): Classic Reverb RE-04 (AnClark/ClassicReverb-RE04 @ main, 2026) as the
// CLASSIC engine of HAUNTED HALLS, with its six factory presets as voices; Classic Master Limiter RE-01 (AnClark/ClassicMasterLimiter-RE01)
// as the CLASSIC engine of GHOST LIMITER. PRIVATE BUILD ONLY (their READMEs call them reverse-engineered rewrites: DECISIONS.md 85).
#pragma once
#include "AltEngine.h"
#include "../effects/SpaceCommon.h"
#define PHANTOM_DPF_HOST 1
#include "../../third_party/kjaerhus/DistrhoPlugin.hpp"

namespace DISTRHO { Plugin* createPlugin_RE04(); Plugin* createPlugin_RE01(); }

namespace phantom::engines
{
// ---------------------------------------------------------------- HAUNTED HALLS: CLASSIC (Classic Reverb RE-04)
class ClassicReverbEngine final : public AltEngine
{
public:
    // its factory presets (PresetManager.cpp kFactoryPresets): room m2, damping %, pre-delay ms, hi damp %, lo-cut Hz, early dB
    struct Preset { const char* name; float room, damping, pre, hiDamp, loCut, early; };
    static const Preset& preset (int i)
    {
        static const Preset k[6] { { "Grand Hall", 640.0f, 32.0f, 0.0f, 36.0f, 80.0f, 1.60f }, { "Small Room", 10.0f, 55.0f, 0.0f, 50.0f, 120.0f, -6.0f },
                                   { "Cathedral", 500.0f, 15.0f, 20.0f, 20.0f, 60.0f, 0.0f }, { "Bright Chamber", 50.0f, 20.0f, 5.0f, 10.0f, 200.0f, -3.0f },
                                   { "Warm Plate", 80.0f, 60.0f, 0.0f, 70.0f, 100.0f, -8.0f }, { "Long Ambience", 200.0f, 25.0f, 10.0f, 30.0f, 70.0f, -2.0f } };
        return k[juce::jlimit (0, 5, i)];
    }
    static constexpr int kVoices = 6;
    // RE-04's wet is 4 .. 11 dB hotter than its input and ends in a soft clip (+5 dBFS ceiling): it is fed 12 dB down and brought back
    // after, so the reverb itself never reaches the clip (the reverb is linear up to it)
    static constexpr float kHeadroom = 0.25f;
    explicit ClassicReverbEngine (const std::vector<ParamSpec>& specs) { for (auto& s : specs) defs[s.id] = s.def; for (auto& r : rv) r.reset (DISTRHO::createPlugin_RE04()); }
    void prepare (double sampleRate, int maxBlock) override
    {
        sr = sampleRate; for (auto& r : rv) r->phantomSetSampleRate (sampleRate);
        const int mb = juce::jmax (maxBlock, 64); for (auto& b : tmp) b.assign ((size_t) mb, 0.0f);
        pre.prepare (sampleRate, 0.52); reset();
    }
    void reset() noexcept override { for (auto& r : rv) { r->phantomSetSampleRate (sr); } pre.reset(); glide.cur = -1.0f; cur = 0; next = -1; xf = 1.0f; settle = 0; builtRoom = { -1.0f, -1.0f }; pending = -1.0f; inGain = 1.0f; primed = false; for (auto& l : sent) l.fill (-1.0e9f); }
    void process (float* L, float* R, int n, const Controls& c, int voice, const TempoInfo&) noexcept override
    {
        const auto& ps = preset (voice);
        // SIZE: the room (m2), around the preset's own (x1/16 .. x4); it re-forms after it settles (100 ms), crossfaded over 150 ms
        const float size = c.get ("size", 0.6f), sizeDef = def ("size", 0.6f);
        const float room = juce::jlimit (0.625f, 640.0f, ps.room * std::pow (2.0f, size <= sizeDef ? (size - sizeDef) / sizeDef * 4.0f : (size - sizeDef) / (1.0f - sizeDef) * 2.0f));
        const float want = std::round (std::log2 (room) * 8.0f) / 8.0f;
        if (want != pending || voice != lastVoice) { pending = want; settle = 0; lastVoice = voice; } else settle += n;
        if (builtRoom[(size_t) cur] < 0.0f) builtRoom[(size_t) cur] = want;
        if (next < 0 && want != builtRoom[(size_t) cur] && settle >= (int) (0.1 * sr)) { next = 1 - cur; builtRoom[(size_t) next] = want; rv[(size_t) next]->phantomSetSampleRate (sr); sent[(size_t) next].fill (-1.0e9f); xf = 0.0f; }
        // DECAY: its DAMPING (the comb feedback loss): longer decays damp less, around the preset's own at our default
        const float decay = c.get ("decay", 3.0f), decDef = def ("decay", 3.0f);
        const float damping = juce::jlimit (0.0f, 100.0f, ps.damping - 18.0f * std::log2 (juce::jmax (0.05f, decay) / decDef));
        const float hiDamp = juce::jlimit (0.0f, 100.0f, around (c.get ("damp", 0.4f), 0.0f, def ("damp", 0.4f), 1.0f, 0.0f, ps.hiDamp, 100.0f));
        const float loCut = juce::jlimit (20.0f, 1000.0f, juce::jmax (ps.loCut, c.get ("lowcut", 20.0f)));
        const float early = around (c.get ("early", 0.5f), 0.0f, def ("early", 0.5f), 1.0f, -40.0f, ps.early, 6.0f);
        const bool frz = c.get ("freeze", 0.0f) >= 0.5f; const float gStep = 1.0f / (0.02f * (float) sr);
        for (int i = 0; i < n; ++i)
        {   // our own pre-delay (the preset's own pre-delay stays inside the engine) and FREEZE's input fade
            const float pd = glide (juce::jlimit (0.0f, 500.0f, c.get ("predelay", 0.0f)) * 0.001f * (float) sr);
            inGain = frz ? juce::jmax (0.0f, inGain - gStep) : juce::jmin (1.0f, inGain + gStep);
            pre.write (0, L[i]); pre.write (1, R[i]); L[i] = pre.read (0, pd + 1.0f) * inGain * kHeadroom; R[i] = pre.read (1, pd + 1.0f) * inGain * kHeadroom;
        }
        // its controls glide (300 ms) and reach it every 32 samples (each change recomputes its coefficients: only when one moved)
        const float tgt[4] { frz ? 0.0f : damping, hiDamp, loCut, early };
        if (! primed) { for (int k = 0; k < 4; ++k) sm[k] = tgt[k]; primed = true; }
        const float a = 1.0f - std::exp (-32.0f / (0.3f * (float) sr));
        auto run = [&] (int k, int i0, int m, float* oL, float* oR)
        {
            auto& p = *rv[(size_t) k]; auto& last = sent[(size_t) k];
            const float v[8] { std::pow (2.0f, builtRoom[(size_t) k]), sm[0], ps.pre, sm[1], sm[2], sm[3], 100.0f, 0.0f };
            for (int q = 0; q < 8; ++q) if (v[q] != last[(size_t) q]) { p.phantomSet ((uint32_t) q, v[q]); last[(size_t) q] = v[q]; }
            const float* in[2] { L + i0, R + i0 }; float* out[2] { oL + i0, oR + i0 }; p.phantomRun (in, out, (uint32_t) m);
        };
        for (int i0 = 0; i0 < n; i0 += 32)
        {
            const int m = juce::jmin (32, n - i0); for (int k = 0; k < 4; ++k) sm[k] += a * (tgt[k] - sm[k]);
            run (cur, i0, m, tmp[0].data(), tmp[1].data()); if (next >= 0) run (next, i0, m, tmp[2].data(), tmp[3].data());
        }
        if (next >= 0)
        { const float step = 1.0f / (0.15f * (float) sr);
            for (int i = 0; i < n; ++i) { xf = juce::jmin (1.0f, xf + step); const float a = std::cos (xf * juce::MathConstants<float>::halfPi), b = std::sin (xf * juce::MathConstants<float>::halfPi); tmp[0][(size_t) i] = tmp[0][(size_t) i] * a + tmp[2][(size_t) i] * b; tmp[1][(size_t) i] = tmp[1][(size_t) i] * a + tmp[3][(size_t) i] * b; }
            if (xf >= 1.0f) { cur = next; next = -1; }
        }
        const float w = clamp01 (c.get ("width", 1.0f));
        for (int i = 0; i < n; ++i) { const float m = 0.5f * (tmp[0][(size_t) i] + tmp[1][(size_t) i]) / kHeadroom, sd = 0.5f * (tmp[0][(size_t) i] - tmp[1][(size_t) i]) * w / kHeadroom; L[i] = m + sd; R[i] = m - sd; }
    }
private:
    float def (const char* id, float fb) const { auto it = defs.find (id); return it == defs.end() ? fb : it->second; }
    std::array<std::unique_ptr<DISTRHO::Plugin>, 2> rv; std::map<std::string, float> defs; std::array<std::vector<float>, 4> tmp;
    double sr = 48000.0; int cur = 0, next = -1, settle = 0, lastVoice = -1; float xf = 1.0f, pending = -1.0f, inGain = 1.0f; std::array<float, 2> builtRoom { -1.0f, -1.0f }; float sm[4] {}; bool primed = false; std::array<std::array<float, 8>, 2> sent {};
    StereoDelay pre; PreGlide glide;
};

// ---------------------------------------------------------------- GHOST LIMITER: CLASSIC (Classic Master Limiter RE-01)
// Its one control is the threshold, -20..0 dB, with the output raised to 0.977 of full scale (the input pushed into it). Here GAIN pushes
// the input, CEILING is the threshold, and the output is brought back so the ceiling is where the limiter stops (unity below it).
class ClassicLimiterEngine final : public AltEngine
{
public:
    ClassicLimiterEngine() { lim.reset (DISTRHO::createPlugin_RE01()); }
    void prepare (double sampleRate, int maxBlock) override { sr = sampleRate; lim->phantomSetSampleRate (sampleRate); const int mb = juce::jmax (maxBlock, 64); for (auto& b : tmp) b.assign ((size_t) mb, 0.0f); reset(); }
    void reset() noexcept override { lim->phantomSetSampleRate (sr); primed = false; lastCeil = 1.0e9f; }
    int latencySamples (int) const noexcept override { return (int) lim->phantomLatency(); }
    void process (float* L, float* R, int n, const Controls& c, int, const TempoInfo&) noexcept override
    {
        // CEILING glides 30 ms, GAIN 150 ms (per sample: a fast push into a limiter is heard as its gain reduction jumping); the threshold
        // is handed to RE-01 every 32 samples, only when it moved
        const float ceilT = juce::jlimit (-20.0f, 0.0f, c.get ("ceiling", -0.3f)), gainT = c.get ("gain", 0.0f);
        if (! primed) { ceilSm = ceilT; gainSm = gainT; primed = true; }
        const float a = 1.0f - std::exp (-32.0f / (0.03f * (float) sr)), ag = 1.0f - std::exp (-1.0f / (0.15f * (float) sr));
        for (int i0 = 0; i0 < n; i0 += 32)
        {
            const int m = juce::jmin (32, n - i0); ceilSm += a * (ceilT - ceilSm);
            if (ceilSm != lastCeil) { lim->phantomSet (0, ceilSm); lastCeil = ceilSm; }
            const float back = juce::Decibels::decibelsToGain (ceilSm) / 0.977f;   // RE-01's output gain is 0.977 / threshold
            for (int i = 0; i < m; ++i) { gainSm += ag * (gainT - gainSm); const float g = juce::Decibels::decibelsToGain (gainSm); tmp[0][(size_t) i] = L[i0 + i] * g; tmp[1][(size_t) i] = R[i0 + i] * g; }
            const float* in[2] { tmp[0].data(), tmp[1].data() }; float* out[2] { L + i0, R + i0 }; lim->phantomRun (in, out, (uint32_t) m);
            for (int i = 0; i < m; ++i) { L[i0 + i] *= back; R[i0 + i] *= back; }
        }
    }
private:
    std::unique_ptr<DISTRHO::Plugin> lim; std::array<std::vector<float>, 2> tmp; double sr = 48000.0; float ceilSm = 0.0f, gainSm = 0.0f, lastCeil = 1.0e9f; bool primed = false;
};
} // namespace phantom::engines
