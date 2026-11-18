// SPDX-License-Identifier: AGPL-3.0-or-later
// GHOST LIMITER baseline engine: LSP dsp-units (LGPL-3.0) Limiter + Oversampler + Delay, compiled unchanged with lsp-common-lib and
// lsp-dsp-lib (LGPL-3.0). Wired as LSP's own limiter plugin wires them: each channel is oversampled (Lanczos, 24-bit quality), the
// side chain is the louder channel (stereo link), the Limiter plans a gain curve with lookahead, the oversampled audio is delayed by the
// lookahead and multiplied by it, then downsampled. Fixed latency: the audio is always delayed by the largest lookahead (10 ms) and the
// side chain by the difference, so turning LOOKAHEAD never moves the reported latency. LSP's oversampler stops at 8x (16x / 32x = 8x).
#include "Engines.h"
#include <lsp-plug.in/dsp/dsp.h>
#include <lsp-plug.in/dsp-units/dynamics/Limiter.h>
#include <lsp-plug.in/dsp-units/util/Oversampler.h>
#include <lsp-plug.in/dsp-units/util/Delay.h>
#include <vector>
#include <cmath>
#include <algorithm>

namespace phantom
{
namespace
{
namespace du = lsp::dspu;
class LspLimiter final : public LimiterEngine
{
public:
    LspLimiter()
    {
        lsp::dsp::init();
        lim.construct(); for (auto& o : os) o.construct(); for (auto& d : dl) d.construct(); scDelay.construct();
    }
    ~LspLimiter() override { lim.destroy(); for (auto& o : os) o.destroy(); for (auto& d : dl) d.destroy(); scDelay.destroy(); }
    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        const size_t maxSr = (size_t) std::lround (sr * 8.0);
        lim.init (maxSr, kMaxLookMs);
        maxLook = (size_t) std::ceil (kMaxLookMs * 0.001 * (double) maxSr) + 4;
        for (auto& o : os) { o.init(); o.set_sample_rate ((size_t) std::lround (sr)); o.set_filtering (false); }
        for (auto& d : dl) d.init (maxLook + kChunk * 8 + 16);
        scDelay.init (maxLook + kChunk * 8 + 16);
        for (auto& b : up) b.assign ((size_t) kChunk * 8, 0.0f);
        sc.assign ((size_t) kChunk * 8, 0.0f); gain.assign ((size_t) kChunk * 8, 0.0f);
        // the oversampler's own latency (host samples) + the fixed lookahead delay at the oversampled rate, in host samples
        for (int idx = 0; idx < 4; ++idx)
        {
            du::Oversampler probe; probe.construct(); probe.init(); probe.set_sample_rate ((size_t) std::lround (sr)); probe.set_mode (modeFor (idx)); probe.update_settings();
            lat[(size_t) idx] = (int) probe.latency() + (int) (lookSamples (1 << idx) / (size_t) (1 << idx)); probe.destroy();
        }
        configure(); reset();
    }
    void reset() override
    {
        if (factorIndex != wantIndex) { factorIndex = wantIndex; configure(); }
        for (auto& d : dl) d.clear();
        scDelay.clear();
    }
    void set (const LimiterSettings& s) noexcept override
    {
        wantIndex = std::min (std::clamp (s.oversampling, 0, 5), 3);
        if (wantIndex != factorIndex) { cur = s; reset(); return; }   // a new oversampling factor starts clean (as ZL's own controller does)
        if (s.ceilingDb != cur.ceilingDb || s.releaseMs != cur.releaseMs || s.lookaheadMs != cur.lookaheadMs || s.adaptive != cur.adaptive || s.truePeak != cur.truePeak)
        { cur = s; settings(); }
    }
    void process (float* l, float* r, int n) noexcept override
    {
        const int f = 1 << factorIndex;
        float* ch[2] { l, r };
        for (int off = 0; off < n;)
        {
            const int m = std::min (n - off, kChunk), M = m * f;
            for (int c = 0; c < 2; ++c) os[(size_t) c].upsample (up[(size_t) c].data(), ch[c] + off, (size_t) m);
            for (int i = 0; i < M; ++i) sc[(size_t) i] = std::max (std::abs (up[0][(size_t) i]), std::abs (up[1][(size_t) i]));
            scDelay.process (sc.data(), sc.data(), (size_t) M);
            lim.process (gain.data(), sc.data(), (size_t) M);
            for (int c = 0; c < 2; ++c)
            {
                dl[(size_t) c].process (up[(size_t) c].data(), up[(size_t) c].data(), (size_t) M);
                lsp::dsp::mul2 (up[(size_t) c].data(), gain.data(), (size_t) M);
                os[(size_t) c].downsample (ch[c] + off, up[(size_t) c].data(), (size_t) m);
            }
            off += m;
        }
    }
    int latencyFor (int oversampling) const override
    {
        return lat[(size_t) std::min (std::clamp (oversampling, 0, 5), 3)];
    }

private:
    static constexpr float kMaxLookMs = 10.0f; static constexpr int kChunk = 256;
    static du::over_mode_t modeFor (int idx) { return idx == 0 ? du::OM_NONE : idx == 1 ? du::OM_LANCZOS_2X24BIT : idx == 2 ? du::OM_LANCZOS_4X24BIT : du::OM_LANCZOS_8X24BIT; }
    // the fixed audio delay at the oversampled rate: the largest lookahead, rounded up to a whole host sample
    size_t lookSamples (int f) const { const size_t s = (size_t) std::ceil (kMaxLookMs * 0.001 * sr * f); return ((s + (size_t) f - 1) / (size_t) f) * (size_t) f; }
    void configure() noexcept
    {
        const int f = 1 << factorIndex;
        for (auto& o : os) { o.set_mode (modeFor (factorIndex)); o.update_settings(); }
        lim.set_sample_rate ((size_t) std::lround (sr * f));
        for (auto& d : dl) d.set_delay (lookSamples (f));
        settings();
    }
    void settings() noexcept
    {
        const int f = 1 << factorIndex;
        lim.set_mode (du::LM_HERM_THIN);   // LSP's limiter plugin default
        lim.set_threshold (std::pow (10.0f, cur.ceilingDb / 20.0f), false);
        lim.set_lookahead (std::clamp (cur.lookaheadMs, 0.1f, kMaxLookMs));
        lim.set_attack (std::clamp (cur.lookaheadMs, 0.1f, kMaxLookMs));      // LSP plugin: attack follows the lookahead
        lim.set_release (cur.releaseMs);
        lim.set_alr (false);   // LSP has no adaptive release; its ALR (automatic level regulation) is a different tool, so ADAPTIVE is not used by this engine
        lim.update_settings();
        const size_t look = lim.get_latency(), total = lookSamples (f);
        scDelay.set_delay (total > look ? total - look : 0);
    }
    du::Limiter lim; du::Oversampler os[2]; du::Delay dl[2], scDelay;
    std::vector<float> up[2], sc, gain;
    int lat[4] {}; LimiterSettings cur; double sr = 48000.0; size_t maxLook = 0; int factorIndex = 2, wantIndex = 2;
};
} // namespace

std::unique_ptr<LimiterEngine> makeLspLimiter() { return std::make_unique<LspLimiter>(); }
} // namespace phantom
