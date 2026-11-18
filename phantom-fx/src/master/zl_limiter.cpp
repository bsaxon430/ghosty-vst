// SPDX-License-Identifier: AGPL-3.0-or-later
// GHOST LIMITER challenger engine: ZL Limiter's own DSP (AGPL-3.0, source/dsp/limiter, header-only on Google Highway, static dispatch),
// compiled unchanged. As ZL Limiter's own Controller does: one limiter per oversampling stage count (1x..32x), every one kept in step
// with the settings, the selected one processes in chunks of at most 512 oversampled samples. Gain before it is the unit's own GAIN.
#include "Engines.h"
#include "dsp/limiter/limiter.hpp"
#include <tuple>
#include <array>
#include <algorithm>

namespace phantom
{
namespace
{
class ZlLimiter final : public LimiterEngine
{
public:
    void prepare (double sampleRate, int maxBlock) override
    {
        sr = sampleRate; juce_ignore (maxBlock);
        std::apply ([&] (auto&... l) { (l.prepare (sr, kChunk, 2), ...); }, lims);
        applyAll(); reset();
    }
    void reset() override { active = want; std::apply ([] (auto&... l) { (l.reset(), ...); }, lims); }
    void set (const LimiterSettings& s) noexcept override
    {
        const bool changed = s.ceilingDb != cur.ceilingDb || s.releaseMs != cur.releaseMs || s.lookaheadMs != cur.lookaheadMs || s.adaptive != cur.adaptive || s.truePeak != cur.truePeak;
        cur = s; want = std::clamp (s.oversampling, 0, 5);
        if (changed) applyAll();
        if (want != active) { active = want; withActive ([] (auto& l) { l.reset(); }); }   // ZL's selectOversampling: the new one starts clean
    }
    void process (float* l, float* r, int n) noexcept override
    {
        for (int off = 0; off < n;)
        {
            const int m = std::min (n - off, kChunk);
            std::array<float*, 2> p { l + off, r + off };
            withActive ([&] (auto& lim) { lim.process (std::span<float*> (p.data(), 2), (size_t) m); });
            off += m;
        }
    }
    int latencyFor (int os) const override
    {
        int lat = 0; int k = 0;
        std::apply ([&] (const auto&... l) { ((lat = (k++ == os ? (int) l.getLatencySamples() : lat)), ...); }, lims);
        return lat;
    }

private:
    static void juce_ignore (int) {}
    // ZL's own chunk size (Controller::kMaximumOversampledBlockSize) at the base rate
    static constexpr int kChunk = 512;
    template <typename F> void withActive (F&& f) noexcept
    {
        int k = 0; std::apply ([&] (auto&... l) { ((k++ == active ? f (l) : void()), ...); }, lims);
    }
    void applyAll() noexcept
    {
        std::apply ([&] (auto&... l) { (apply (l), ...); }, lims);
    }
    template <typename L> void apply (L& l) noexcept
    {
        l.setOutputCeilingDecibels (cur.ceilingDb);
        l.setTruePeakEnabled (cur.truePeak);
        l.setLookaheadMilliseconds (cur.lookaheadMs);
        l.setAttackMilliseconds (100.0f);                                        // ZL Limiter's default attack
        l.setReleaseMilliseconds (std::max (25.0f, cur.releaseMs));              // ZL's release range starts at 25 ms
        l.setChannelDeltaDecibels (1.5f);                                        // ZL's default channel link
        l.setRecoveryMilliseconds (cur.adaptive ? 50.0f : 3.5f);                 // ZL's default recovery; ADAPTIVE off = its fastest
    }
    std::tuple<zldsp::limiter::Limiter<float, 0>, zldsp::limiter::Limiter<float, 1>, zldsp::limiter::Limiter<float, 2>,
               zldsp::limiter::Limiter<float, 3>, zldsp::limiter::Limiter<float, 4>, zldsp::limiter::Limiter<float, 5>> lims;
    LimiterSettings cur; double sr = 48000.0; int active = 2, want = 2;
};
} // namespace

std::unique_ptr<LimiterEngine> makeZlLimiter() { return std::make_unique<ZlLimiter>(); }
} // namespace phantom
