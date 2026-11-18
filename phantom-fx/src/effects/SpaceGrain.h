// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX 16 CRYSTAL: the granular texture engine of Emilie Gillet's eurorack code (MIT; the Surge Synth Team's desktop fork),
// hosted exactly as sst-effects' Nimbus hosts it (GPL-3.0: its own 32 kHz resampling, buffer quality, the engine's position / size /
// pitch / density / texture / spread / feedback / freeze). Our part: the mapping from CRYSTAL's controls, and REVERSE, which the engine
// does not have: reversed, pitched splices read backwards from our own input buffer (two overlapping Hann-windowed readers), blended
// with the engine's forward grains by the REVERSE amount (DECISIONS.md 22). The original names are never shown in the UI.
#pragma once
#include "SpaceCommon.h"
#include "sst/basic-blocks/simd/setup.h"
#include "sst/effects/ConcreteConfig.h"
#include "sst/effects/Nimbus.h"

namespace phantom
{
// the engine host (instantiated in src/space/nimbus.cpp, the only file that sees the engine's own headers)
using NimbusHost = sst::effects::nimbus::Nimbus<sst::effects::core::ConcreteConfig>;
struct NimbusBox
{
    explicit NimbusBox (double sr);
    ~NimbusBox();
    void setParam (int idx, float v) noexcept;
    void process (float* l, float* r) noexcept;   // one 16-sample block in place
    std::unique_ptr<sst::effects::core::ConcreteConfig::GS> gs; std::unique_ptr<NimbusHost> fx;
};

class CrystalFx final : public CatalogEffect
{
public:
    CrystalFx() : CatalogEffect (uCrystal) {}
    bool wetOnly() const override { return true; }
    bool widensOnPurpose() const override { return true; }
    double tailSeconds() const override { return 6.0; }
    double ownSpreadHz() const override { return 200.0; }
    bool zipperEventControl (const std::string& c) const override { return c == "splice" || c == "density" || c == "pitch"; }   // the engine sets size, rate and pitch as each grain starts
protected:
    static constexpr int kB = sst::effects::core::ConcreteConfig::blockSize;
    void prepareEffect (double sampleRate, int, int) override
    {
        pPitch = id ("pitch"); pSplice = id ("splice"); pDelay = id ("delay"); pFb = id ("fb"); pRev = id ("reverse"); pSpread = id ("spread");
        pPos = id ("position"); pDens = id ("density"); pTex = id ("texture"); pFreeze = id ("freeze");
        box = std::make_unique<NimbusBox> (sampleRate);
        ring.setSize (2, (int) (sampleRate * 2.2) + 8); ring.clear(); w = 0; for (auto& r : rd) r = {};
        bpos = 0; for (auto& c : ib) std::fill (std::begin (c), std::end (c), 0.0f); for (auto& c : ob) std::fill (std::begin (c), std::end (c), 0.0f);
    }
    void resetEffect() override { ring.clear(); w = 0; for (auto& r : rd) r = {}; bpos = 0; sinceStart = 1 << 30; turn = 0; }
public:
    void process (juce::dsp::AudioBlock<float> block, const ModulationContext& ctx) noexcept override
    {
        beginBlock (ctx);
        const int n = (int) block.getNumSamples(); auto* L = block.getChannelPointer (0); auto* R = block.getNumChannels() > 1 ? block.getChannelPointer (1) : L;
        const bool frz = on (pFreeze); const int N = ring.getNumSamples();
        for (int i = 0; i < n; ++i)
        {
            const float pitch = p (pPitch).next(), splice = p (pSplice).next(), delMs = p (pDelay).next(), fb = p (pFb).next(), rev = p (pRev).next(), spread = p (pSpread).next();
            const float pos = p (pPos).next(), dens = p (pDens).next(), tex = p (pTex).next();
            // engine: 16-sample blocks (its host's block), one block behind
            ib[0][bpos] = L[i]; ib[1][bpos] = R[i];
            float gl = ob[0][bpos], gr = ob[1][bpos];
            if (++bpos == kB)
            {
                bpos = 0;
                box->setParam (NimbusHost::nmb_pitch, pitch);
                box->setParam (NimbusHost::nmb_size, juce::jlimit (0.0f, 1.0f, std::log2 (juce::jmax (1.0f, splice) / 32.0f) / 4.0f));
                box->setParam (NimbusHost::nmb_position, juce::jlimit (0.0f, 1.0f, delMs / 930.0f + pos));
                box->setParam (NimbusHost::nmb_feedback, fb); box->setParam (NimbusHost::nmb_spread, spread);
                box->setParam (NimbusHost::nmb_density, dens * 2.0f - 1.0f); box->setParam (NimbusHost::nmb_texture, tex * 2.0f - 1.0f);
                box->setParam (NimbusHost::nmb_freeze, frz ? 1.0f : 0.0f);
                for (int k = 0; k < kB; ++k) { ob[0][k] = ib[0][k]; ob[1][k] = ib[1][k]; }
                box->process (ob[0], ob[1]);
            }
            // reverse splices: our ring of the input (held still while frozen), read backwards at the pitch ratio
            if (! frz) { ring.setSample (0, w, L[i]); ring.setSample (1, w, R[i]); if (++w == N) w = 0; }
            float rl = 0.0f, rr = 0.0f;
            if (rev > 0.0f)
            {
                const float len = juce::jlimit (64.0f, (float) N * 0.4f, splice * 0.001f * (float) sr), ratio = std::pow (2.0f, pitch / 12.0f);
                // a new splice every half splice, alternating between the two readers (they overlap by half: Hann windows sum flat)
                if (++sinceStart >= (int) (len * 0.5f))
                {
                    sinceStart = 0; auto& r = rd[(size_t) turn];
                    r.len = len; r.left = len; r.pos = (float) w - delMs * 0.001f * (float) sr - 1.0f;
                    r.panL = 1.0f + spread * 0.35f * (turn == 0 ? 1.0f : -1.0f); turn ^= 1;
                }
                for (auto& r : rd)
                {
                    if (r.left <= 0.0f) continue;
                    const float ph = 1.0f - r.left / r.len, win = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * ph);
                    float x = r.pos; while (x < 0.0f) x += (float) N; while (x >= (float) N) x -= (float) N;
                    const int i0 = juce::jlimit (0, N - 1, (int) x); const float fr = x - (float) i0; const int i1 = i0 + 1 >= N ? 0 : i0 + 1;
                    const float sl = ring.getSample (0, i0) + fr * (ring.getSample (0, i1) - ring.getSample (0, i0));
                    const float sr2 = ring.getSample (1, i0) + fr * (ring.getSample (1, i1) - ring.getSample (1, i0));
                    rl += sl * win * r.panL; rr += sr2 * win * (2.0f - r.panL);
                    r.pos -= ratio; r.left -= 1.0f;
                }
            }
            // forward grains and reversed splices, equal power on REVERSE
            const float a = std::cos (rev * juce::MathConstants<float>::halfPi), b = std::sin (rev * juce::MathConstants<float>::halfPi);
            L[i] = (gl * a + rl * b) * kLevel; R[i] = (gr * a + rr * b) * kLevel;
        }
    }
private:
    static constexpr float kLevel = 1.609f;   // level match on pink noise (DECISIONS.md 24)
    struct Reader { float pos = 0.0f, len = 0.0f, left = 0.0f, panL = 1.0f; };
    std::unique_ptr<NimbusBox> box; juce::AudioBuffer<float> ring; int w = 0; std::array<Reader, 2> rd {}; int sinceStart = 1 << 30, turn = 0;
    alignas (16) float ib[2][kB] {}; alignas (16) float ob[2][kB] {}; int bpos = 0;
    int pPitch = 0, pSplice = 0, pDelay = 0, pFb = 0, pRev = 0, pSpread = 0, pPos = 0, pDens = 0, pTex = 0, pFreeze = 0;
};
} // namespace phantom
