// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX shared DSP: the oversampling policy. Modules ask for a factor; the rack's global setting caps it; this applies it.
#pragma once
#include <juce_dsp/juce_dsp.h>
#if defined (__SSE2__) || defined (_M_X64)
 #include <emmintrin.h>
#endif
#include <array>
#include <memory>

namespace phantom
{
enum class OversamplingPolicy { Off = 0, X2 = 1, X4 = 2, X8 = 3, Render = 4 };   // Render = 8x with linear-phase (steep) filters

inline int policyFactor (OversamplingPolicy p) noexcept
{
    switch (p) { case OversamplingPolicy::Off: return 1; case OversamplingPolicy::X2: return 2; case OversamplingPolicy::X4: return 4; case OversamplingPolicy::X8: return 8; default: return 16; }   // Render: up to 16x for the modules that ask for it
}

// One 2x half-band stage: the filters are JUCE's own equiripple half-band designs (FilterDesign::designFIRLowpassHalfBandEquirippleMethod,
// exactly what juce::dsp::Oversampling builds); the convolution is ours. JUCE's loop shifts its whole history every sample and walks the
// taps with a stride of two; here each phase's history is a contiguous window of a doubled ring buffer and only the non-zero taps are
// multiplied, with the arithmetic of JUCE's stage (same taps, same centre-tap path, same latency), several times faster.
class HalfBandStage
{
public:
    void prepare (float tw, float stopDb, int channels, int maxLowBlock)
    {
        auto design = [&] (Phase& ph) {
            auto c = juce::dsp::FilterDesign<float>::designFIRLowpassHalfBandEquirippleMethod (tw, stopDb);
            const float* h = c->getRawCoefficients(); const int N = (int) c->getFilterOrder() + 1;   // N odd, (N - 1) / 2 odd
            ph.order = N - 1; const int nd2 = (N - 1) / 2; ph.L = nd2 + 1; ph.M = (nd2 + 1) / 2; ph.centre = h[nd2]; ph.mc = (nd2 + 1) / 2; ph.D = nd2 / 2 + 1;
            ph.g.resize ((size_t) ph.M); for (int m = 0; m < ph.M; ++m) ph.g[(size_t) m] = h[2 * m];
            ph.ring.assign ((size_t) channels, std::vector<float> ((size_t) (2 * ph.L), 0.0f)); ph.pos.assign ((size_t) channels, 0);
            ph.odd.assign ((size_t) channels, std::vector<float> ((size_t) ph.D, 0.0f)); ph.oddPos.assign ((size_t) channels, 0);
        };
        design (upPh); design (downPh); ch = channels;
        buffer.setSize (channels, 2 * maxLowBlock);
    }
    void reset() noexcept { for (auto* ph : { &upPh, &downPh }) { for (auto& r : ph->ring) std::fill (r.begin(), r.end(), 0.0f); for (auto& r : ph->odd) std::fill (r.begin(), r.end(), 0.0f); std::fill (ph->pos.begin(), ph->pos.end(), 0); std::fill (ph->oddPos.begin(), ph->oddPos.end(), 0); } }
    float latency() const noexcept { return 0.5f * (float) (upPh.order + downPh.order); }   // in samples at this stage's high rate
    // in (n samples per channel) -> buffer (2n)
    void up (const float* const* in, int numCh, int n) noexcept
    {
        for (int c = 0; c < numCh; ++c)
        {
            auto& ring = upPh.ring[(size_t) c]; int p = upPh.pos[(size_t) c]; float* out = buffer.getWritePointer (c); const int L = upPh.L;
            for (int i = 0; i < n; ++i)
            {
                const float x = 2.0f * in[c][i]; ring[(size_t) p] = x; ring[(size_t) (p + L)] = x; const float* w = ring.data() + p + 1; p = p + 1 == L ? 0 : p + 1;
                out[2 * i] = dot (w, upPh); out[2 * i + 1] = w[upPh.mc] * upPh.centre;
            }
            upPh.pos[(size_t) c] = p;
        }
    }
    // buffer (2n) -> out (n)
    void down (float* const* out, int numCh, int n) noexcept
    {
        for (int c = 0; c < numCh; ++c)
        {
            auto& ring = downPh.ring[(size_t) c]; int p = downPh.pos[(size_t) c]; auto& odd = downPh.odd[(size_t) c]; int op = downPh.oddPos[(size_t) c];
            const float* in = buffer.getReadPointer (c); const int L = downPh.L, D = downPh.D;
            for (int i = 0; i < n; ++i)
            {
                const float x = in[2 * i]; ring[(size_t) p] = x; ring[(size_t) (p + L)] = x; const float* w = ring.data() + p + 1; p = p + 1 == L ? 0 : p + 1;
                const float d = odd[(size_t) op]; odd[(size_t) op] = in[2 * i + 1]; op = op + 1 == D ? 0 : op + 1;
                out[c][i] = dot (w, downPh) + d * downPh.centre;
            }
            downPh.pos[(size_t) c] = p; downPh.oddPos[(size_t) c] = op;
        }
    }
    juce::AudioBuffer<float> buffer;   // this stage's high-rate signal
private:
    struct Phase { std::vector<float> g; float centre = 0.5f; int L = 1, M = 0, mc = 0, D = 1, order = 0; std::vector<std::vector<float>> ring, odd; std::vector<int> pos, oddPos; };
    static inline float dot (const float* w, const Phase& ph) noexcept
    {
        const float* g = ph.g.data(); const int M = ph.M, L1 = ph.L - 1; float a0 = 0, a1 = 0, a2 = 0, a3 = 0; int m = 0;
       #if defined (__SSE2__) || defined (_M_X64)
        {   // four taps at a time: the mirrored half of the window is loaded and reversed in the register
            __m128 acc = _mm_setzero_ps();
            for (; m + 4 <= M; m += 4)
            {
                const __m128 fwd = _mm_loadu_ps (w + m), bwd = _mm_loadu_ps (w + L1 - m - 3);
                acc = _mm_add_ps (acc, _mm_mul_ps (_mm_add_ps (fwd, _mm_shuffle_ps (bwd, bwd, _MM_SHUFFLE (0, 1, 2, 3))), _mm_loadu_ps (g + m)));
            }
            float t alignas (16) [4]; _mm_store_ps (t, acc); a0 = t[0]; a1 = t[1]; a2 = t[2]; a3 = t[3];
        }
       #endif
        for (; m + 4 <= M; m += 4) { a0 += (w[m] + w[L1 - m]) * g[m]; a1 += (w[m + 1] + w[L1 - m - 1]) * g[m + 1]; a2 += (w[m + 2] + w[L1 - m - 2]) * g[m + 2]; a3 += (w[m + 3] + w[L1 - m - 3]) * g[m + 3]; }
        for (; m < M; ++m) a0 += (w[m] + w[L1 - m]) * g[m];
        return (a0 + a1) + (a2 + a3);
    }
    Phase upPh, downPh; int ch = 2;
};

// The oversampling policy's engine: two chains of half-band stages (normal and Render), each shared by every factor (a factor of 2^k runs the
// chain's first k stages), so switching the policy never allocates on the audio thread. Linear phase, integer-sample stage latencies; the
// total is a multiple of 1/factor host samples, which the effect slot pads up to a whole host sample.
// Normal: -115 dB stages (first stage pass band to ~21 kHz at 48 kHz); Render: -130 dB with a steeper first stage. JUCE's preset chains
// stop at -60..-90 dB, whose pass-band ripple would leave a -50..-75 dB residual on a round trip (DECISIONS.md 13).
class Oversampler
{
public:
    static constexpr int kMaxStages = 4;   // up to 16x
    void prepare (double sampleRate, int maxBlockSize, int numChannels)
    {
        juce::ignoreUnused (sampleRate); numCh = numChannels;
        for (int s = 0; s < kMaxStages; ++s)
        {
            const int lowBlock = maxBlockSize << s;
            normal[(size_t) s].prepare (s == 0 ? 0.06f : 0.25f, -115.0f, numChannels, lowBlock);
            render[(size_t) s].prepare (s == 0 ? 0.04f : 0.25f, -130.0f, numChannels, lowBlock);
        }
        reset();
    }
    void reset() noexcept { for (auto& st : normal) st.reset(); for (auto& st : render) st.reset(); }
    // factor in {1, 2, 4, 8, 16}; render = the Render chain
    void set (int factor, bool isRender) noexcept { if (factor != current || isRender != linear) { current = factor; linear = isRender; reset(); } }
    int factor() const noexcept { return current; }
    float latencySamples() const noexcept
    {
        float l = 0.0f; const auto& chain = linear ? render : normal;
        for (int s = 0; s < stages(); ++s) l += chain[(size_t) s].latency() / (float) (2 << s);   // a stage's latency counts its own (output) rate
        return l;
    }
    juce::dsp::AudioBlock<float> up (juce::dsp::AudioBlock<float> block) noexcept
    {
        const int k = stages(); if (k == 0) return block;
        auto& chain = linear ? render : normal; int n = (int) block.getNumSamples(); const int c = juce::jmin (numCh, (int) block.getNumChannels());
        const float* ptrs[2] { block.getChannelPointer (0), block.getChannelPointer ((size_t) juce::jmin (1, c - 1)) };
        chain[0].up (ptrs, c, n);
        for (int s = 1; s < k; ++s) { n *= 2; chain[(size_t) s].up (chain[(size_t) s - 1].buffer.getArrayOfReadPointers(), c, n); }
        return juce::dsp::AudioBlock<float> (chain[(size_t) k - 1].buffer).getSubBlock (0, (size_t) n * 2).getSubsetChannelBlock (0, (size_t) c);
    }
    void down (juce::dsp::AudioBlock<float> block) noexcept
    {
        const int k = stages(); if (k == 0) return;
        auto& chain = linear ? render : normal; const int n = (int) block.getNumSamples(); const int c = juce::jmin (numCh, (int) block.getNumChannels());
        for (int s = k - 1; s >= 1; --s) chain[(size_t) s].down (chain[(size_t) s - 1].buffer.getArrayOfWritePointers(), c, n << s);
        float* ptrs[2] { block.getChannelPointer (0), block.getChannelPointer ((size_t) juce::jmin (1, c - 1)) };
        chain[0].down (ptrs, c, n);
    }
private:
    int stages() const noexcept { return current == 2 ? 1 : current == 4 ? 2 : current == 8 ? 3 : current == 16 ? 4 : 0; }
    std::array<HalfBandStage, kMaxStages> normal, render;
    int current = 1, numCh = 2; bool linear = false;
};
} // namespace phantom
