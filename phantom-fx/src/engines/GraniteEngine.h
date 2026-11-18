// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX: Granite (Nate Houk / Epsilon Records, epsilon-records/granite @ bed64f0, MIT; plugins/Granite/GraniteEngine.hpp and its two
// common/ headers vendored unchanged in third_party/granite) as the GRANITE engine of GHOST LIMITER, its eight styles as voices
// (DECISIONS.md 87). GHOST LIMITER's own controls drive it: GAIN, CEILING, RELEASE, LOOKAHEAD (attack = the look-ahead), TRUE PEAK
// (Granite's true-peak limiting stage) and OVERSAMPLING (Granite's own 1x .. 32x, the same six steps). ADAPTIVE RELEASE is left to the
// style (Granite's styles carry their own release behaviour). Its dither, sidechain, DC filter, monitoring and meters stay off / unused.
#pragma once
#include "AltEngine.h"
#include "GraniteEngine.hpp"
#include <atomic>

namespace phantom::engines
{
class GraniteLimEngine final : public AltEngine
{
public:
    static constexpr int kVoices = granite::kNumStyles;
    static std::string voiceName (int i)
    {
        static const char* n[kVoices] { "Clear", "Punchy", "Dynamic", "Balanced", "Aggressive", "Modern", "Bus", "Safe" };
        return n[juce::jlimit (0, kVoices - 1, i)];
    }
    GraniteLimEngine() { for (auto& f : fx) f = std::make_unique<granite::Engine>(); }
    void prepare (double sampleRate, int maxBlock) override
    {
        sr = sampleRate;
        // the latency at each OVERSAMPLING step is Granite's at its longest look-ahead (10 ms) with the true-peak stage on: a shorter
        // look-ahead, or TRUE PEAK off, is padded up to it, so the latency moves only with OVERSAMPLING (as the PHANTOM engine's does)
        for (int k = 0; k < granite::kNumOversampling; ++k)
        {
            granite::Engine e; e.p[granite::kOversampling] = (float) k; e.p[granite::kLookahead] = 10.0f; e.p[granite::kTruePeakLimit] = 1.0f;
            e.setSampleRate ((float) sampleRate); maxLat[(size_t) k] = (int) e.latency();
        }
        const int most = *std::max_element (maxLat.begin(), maxLat.end()) + 8;
        for (auto& f : fx) { quiet (*f); f->setSampleRate ((float) sampleRate); }
        for (auto& pd : pad) for (auto& ch : pd.buf) ch.assign ((size_t) most + 1, 0.0f);
        const int mb = juce::jmax (maxBlock, 64); for (auto& b : tmp) b.assign ((size_t) mb, 0.0f);
        reset();
    }
    void reset() noexcept override
    {
        primed = false; cur = 0; next = -1; xf = 1.0f; settle = 0; built[0] = built[1] = Shape {}; pending = Shape {};
        for (auto& pd : pad) { for (auto& ch : pd.buf) std::fill (ch.begin(), ch.end(), 0.0f); pd.pos = 0; pd.delay = 0; }
    }
    void update (const Controls& c, int) override { osNow.store (osStep (c), std::memory_order_relaxed); }
    int latencySamples (int) const noexcept override { return maxLat[(size_t) juce::jlimit (0, granite::kNumOversampling - 1, osNow.load (std::memory_order_relaxed))]; }
    void process (float* L, float* R, int n, const Controls& c, int voice, const TempoInfo&) noexcept override
    {
        // GAIN glides per sample over 150 ms (as APEX's), CEILING over 30 ms, handed to Granite every 32 samples (it smooths them again,
        // 20 ms). LOOKAHEAD, TRUE PEAK and OVERSAMPLING re-form the limiter: a second Granite takes the new shape once it has settled
        // (200 ms) and the two crossfade over 50 ms, each padded to the shared latency
        const Shape want { osStep (c), std::round (juce::jlimit (0.5f, 10.0f, c.get ("lookahead", 2.0f)) * 10.0f) / 10.0f, c.get ("truepeak", 1.0f) >= 0.5f };
        osNow.store (want.os, std::memory_order_relaxed);
        const float gT = c.get ("gain", 0.0f), cT = c.get ("ceiling", -0.3f), rel = c.get ("release", 80.0f);
        if (! primed) { gSm = gT; cSm = cT; primed = true; }
        if (! (want == pending)) { pending = want; settle = 0; } else settle += n;
        if (! built[(size_t) cur].valid) { shape (cur, want); fx[(size_t) cur]->reset(); }
        if (next < 0 && ! (want == built[(size_t) cur]) && settle >= (int) (0.2 * sr)) { next = 1 - cur; shape (next, want); fx[(size_t) next]->reset(); clearPad (next); xf = 0.0f; }
        const float ag = 1.0f - std::exp (-1.0f / (0.15f * (float) sr)), ac = 1.0f - std::exp (-32.0f / (0.03f * (float) sr));
        const int style = juce::jlimit (0, kVoices - 1, voice);
        for (auto& f : fx) { f->p[granite::kStyle] = (float) style; f->p[granite::kRelease] = juce::jlimit (1.0f, 2000.0f, rel); }
        for (int i0 = 0; i0 < n; i0 += 32)
        {
            const int m = juce::jmin (32, n - i0); cSm += ac * (cT - cSm);
            for (int i = i0; i < i0 + m; ++i) { gSm += ag * (gT - gSm); const float g = juce::Decibels::decibelsToGain (gSm); L[i] *= g; R[i] *= g; }
            for (auto& f : fx) f->p[granite::kCeiling] = juce::jlimit (-30.0f, 0.0f, cSm);
            if (next >= 0)
            {
                std::copy (L + i0, L + i0 + m, tmp[0].begin()); std::copy (R + i0, R + i0 + m, tmp[1].begin());
                run (next, tmp[0].data(), tmp[1].data(), m);
            }
            run (cur, L + i0, R + i0, m);
            if (next >= 0)
            {
                const float step = 1.0f / (0.05f * (float) sr);
                for (int i = 0; i < m; ++i) { xf = juce::jmin (1.0f, xf + step); const float a = std::cos (xf * juce::MathConstants<float>::halfPi), b = std::sin (xf * juce::MathConstants<float>::halfPi); L[i0 + i] = L[i0 + i] * a + tmp[0][(size_t) i] * b; R[i0 + i] = R[i0 + i] * a + tmp[1][(size_t) i] * b; }
                if (xf >= 1.0f) { cur = next; next = -1; }
            }
        }
    }
private:
    struct Shape { int os = 1; float look = 2.0f; bool tp = true; bool valid = false; bool operator== (const Shape& o) const noexcept { return os == o.os && look == o.look && tp == o.tp; } };
    struct Pad { std::array<std::vector<float>, 2> buf; int pos = 0, delay = 0; };
    static int osStep (const Controls& c) noexcept { return juce::jlimit (0, granite::kNumOversampling - 1, (int) std::lround (c.get ("quality", 1.0f))); }
    // everything of Granite's that is not a limiter: off (and the attack follows the look-ahead: the full window, as a brickwall)
    static void quiet (granite::Engine& e) noexcept
    {
        using namespace granite;
        e.p[kDither] = 0; e.p[kTruePeak] = 0; e.p[kSidechain] = 0; e.p[kDcFilter] = 0; e.p[kUnityGain] = 0; e.p[kAudition] = 0; e.p[kGain] = 0;
        e.p[kLoudTarget] = 0; e.p[kLinkTransient] = 0.75f; e.p[kLinkRelease] = 1.0f;
    }
    void shape (int k, Shape s) noexcept
    {
        auto& e = *fx[(size_t) k]; quiet (e);
        e.p[granite::kOversampling] = (float) s.os; e.p[granite::kLookahead] = s.look; e.p[granite::kAttack] = juce::jlimit (0.1f, 10.0f, s.look);
        e.p[granite::kTruePeakLimit] = s.tp ? 1.0f : 0.0f; e.p[granite::kCeiling] = juce::jlimit (-30.0f, 0.0f, cSm);
        s.valid = true; built[(size_t) k] = s;
    }
    void clearPad (int k) noexcept { auto& pd = pad[(size_t) k]; for (auto& ch : pd.buf) std::fill (ch.begin(), ch.end(), 0.0f); pd.pos = 0; }
    // one Granite on L / R in place, then its pad up to the shared latency of its OVERSAMPLING step
    void run (int k, float* L, float* R, int m) noexcept
    {
        auto& e = *fx[(size_t) k]; e.process (L, R, L, R, (uint32_t) m);
        auto& pd = pad[(size_t) k]; pd.delay = juce::jmax (0, maxLat[(size_t) built[(size_t) k].os] - (int) e.latency());
        if (pd.delay == 0) return;
        const int len = (int) pd.buf[0].size(); float* io[2] { L, R };
        for (int i = 0; i < m; ++i)
        {
            int rd = pd.pos - pd.delay; if (rd < 0) rd += len;
            for (int ch = 0; ch < 2; ++ch) { auto& b = pd.buf[(size_t) ch]; b[(size_t) pd.pos] = io[ch][i]; io[ch][i] = b[(size_t) rd]; }
            if (++pd.pos >= len) pd.pos = 0;
        }
    }
    std::array<std::unique_ptr<granite::Engine>, 2> fx; std::array<Pad, 2> pad; std::array<std::vector<float>, 2> tmp; std::array<int, granite::kNumOversampling> maxLat {};
    std::array<Shape, 2> built; Shape pending; std::atomic<int> osNow { 1 }; double sr = 48000.0;
    float gSm = 0.0f, cSm = -0.3f, xf = 1.0f; int cur = 0, next = -1, settle = 0; bool primed = false;
};
} // namespace phantom::engines
