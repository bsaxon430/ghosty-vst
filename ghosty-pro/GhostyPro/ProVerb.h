#pragma once
// GHOST VERB (2026-10-24): the synth reverb for Ghosty Pro, round 2. Original DSP (no third-party code).
// Chosen by measurement against Airwindows Galactic / Verbity / MatrixVerb / Chamber (MIT) on impulse responses: at the same T60 the
// tail is as smooth (worst spectral peak 11-12 dB vs 10-12 dB), the early field denser (crest 15-19 dB vs 18-27 dB), no floor, 0.7 % CPU;
// and it has what the synth needs that those lack: pre-delay, LO / HI CUT on the wet, T60 in seconds independent of SIZE, in-loop
// LF damping, M/S width, ducking. Measurement harness: the lead's scratchpad verbharness/ (not shipped).
// pre-delay -> wet LO CUT / HI CUT -> 4 input all-pass diffusers -> 8-line feedback delay network (Householder mixing,
// mutually prime lengths scaled by SIZE, per-line T60 gain from DECAY, in-loop one-pole HF damping + LF damping, slow
// modulation of four lines so the tail never rings) -> M/S WIDTH -> equal-power wet / dry, optional ducking of the wet by the dry.
// Audio-thread rule: prepare() allocates, everything else is allocation- and lock-free. No JUCE needed (plain C++).
#include <cmath>
#include <vector>
#include <array>
#include <algorithm>

namespace ghosty
{
class GhostVerb
{
public:
    struct Params
    {
        float mix = 0.2f;         // equal-power wet (0 .. 1); 0.2 = the default "about 20 %"
        float size = 0.6f;        // 0 .. 1: delay scale 0.4 .. 1.6
        float decay = 1.5f;       // T60 seconds (0.1 .. 30)
        float predelayMs = 10.0f; // 0 .. 250
        float loCut = 150.0f;     // Hz on the wet (20 .. 1000)
        float hiCut = 9000.0f;    // Hz on the wet (1 k .. 20 k)
        float damp = 0.4f;        // 0 .. 1: in-loop HF damping (how fast the top decays)
        float lowDamp = 0.3f;     // 0 .. 1: in-loop LF damping (how fast the bottom decays: keeps the tail out of the mud)
        float mod = 0.5f;         // 0 .. 1: delay-line modulation depth
        float width = 1.0f;       // 0 mono .. 1 full .. 1.3 extra wide (M/S)
        float duck = 0.0f;        // 0 .. 1: the dry ducks the wet (attack 5 ms, release 250 ms)
        float diffusion = 0.7f;   // 0 .. 1: input all-pass gain
    };
    static constexpr int N = 16, AP = 4;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        const double scale = sr / 48000.0;
        // base lengths (samples at 48 k), mutually prime, 23 .. 113 ms, spread like a hall's early modes
        static const int base[N] { 1103, 1301, 1489, 1693, 1907, 2131, 2371, 2621, 2887, 3163, 3449, 3761, 4093, 4423, 4787, 5413 };
        static const int apBase[AP] { 142, 379, 677, 1013 };
        const int cap = (int) (base[N - 1] * scale * 1.7 + 64);          // one capacity for every line (they share the write index)
        for (int i = 0; i < N; ++i)
        {
            lenBase[i] = (double) base[i] * scale;
            lines[i].assign ((size_t) cap, 0.0f);
            lfoPhase[i] = (double) i / N;
        }
        for (int i = 0; i < AP; ++i) for (int c = 0; c < 2; ++c) ap[c][i].assign ((size_t) (int) (apBase[i] * scale * 1.1 + 8), 0.0f);
        pre[0].assign ((size_t) (0.26 * sr) + 8, 0.0f); pre[1].assign ((size_t) (0.26 * sr) + 8, 0.0f);
        lineW = 0; preW = 0; apW = 0;
        duckA = 1.0f - (float) std::exp (-1.0 / (0.005 * sr)); duckR = 1.0f - (float) std::exp (-1.0 / (0.25 * sr));
        smA = 1.0f - (float) std::exp (-1.0 / (0.03 * sr));
        reset();
    }
    void reset()
    {
        for (auto& l : lines) std::fill (l.begin(), l.end(), 0.0f);
        for (auto& c : ap) for (auto& a : c) std::fill (a.begin(), a.end(), 0.0f);
        for (auto& p : pre) std::fill (p.begin(), p.end(), 0.0f);
        for (auto& v : dampZ) v = 0; for (auto& v : lowZ) v = 0;
        for (auto& v : inLp) v = 0; for (auto& v : inHp) v = 0; for (auto& v : inHp2) v = 0;
        duckEnv = 0; wetSm = -1; sizeSm = -1; preSm = -1; widthSm = -1;
    }

    // L / R in place; n samples; wet is added to the dry by the mix law (dry scaled by cos, wet by sin)
    void process (float* L, float* R, int n, const Params& p) noexcept
    {
        // per-block coefficients
        const double sizeT = 0.4 + 1.2 * std::min (1.0f, std::max (0.0f, p.size));
        if (sizeSm < 0) sizeSm = sizeT;
        const float preT = (float) std::min ((double) pre[0].size() - 4, std::max (0.0, p.predelayMs * 0.001 * sr));
        if (preSm < 0) preSm = preT;
        const float widthT = std::max (0.0f, p.width);
        if (widthSm < 0) widthSm = widthT;
        const float lpA = (float) std::exp (-2.0 * 3.14159265358979323846 * std::min (0.45 * sr, (double) std::max (1000.0f, p.hiCut)) / sr);
        const float hpA = (float) std::exp (-2.0 * 3.14159265358979323846 * std::max (20.0, (double) p.loCut) / sr);
        // in-loop damping: HF one-pole from DAMP (20 kHz .. 1.5 kHz), LF one-pole from LOWDAMP (0 .. 250 Hz)
        const float dampHz = 20000.0f * std::pow (0.075f, std::min (1.0f, std::max (0.0f, p.damp)));
        const float da = (float) std::exp (-2.0 * 3.14159265358979323846 * std::min (0.45 * sr, (double) dampHz) / sr);
        const float lowHz = 250.0f * std::min (1.0f, std::max (0.0f, p.lowDamp));
        const float la = lowHz > 1.0f ? (float) std::exp (-2.0 * 3.14159265358979323846 * lowHz / sr) : 0.0f;
        const float apG = 0.35f + 0.4f * std::min (1.0f, std::max (0.0f, p.diffusion));
        const double t60 = std::max (0.1f, p.decay);
        const float modDepth = (float) (std::min (1.0f, std::max (0.0f, p.mod)) * 0.0004 * sr);   // up to ~0.4 ms of swing
        const double modHz = 0.11;
        const float mixT = std::min (1.0f, std::max (0.0f, p.mix));
        if (wetSm < 0) wetSm = mixT;
        const float duckAmt = std::min (1.0f, std::max (0.0f, p.duck));
        double lenNow[N], lenStep[N];
        for (int i = 0; i < N; ++i)
        {
            const double len = lenBase[i] * sizeSm;
            g[i] = (float) std::pow (10.0, -3.0 * (len / sr) / (t60 * 1.18));     // x1.18: the in-loop damping and the interpolation take a little each pass (measured)
            lfoInc[i] = modHz * (0.55 + 0.06 * i) / sr;
            // the modulated length: one sine per line per BLOCK, ramped linearly across the block (no per-sample trig)
            const double depth = modDepth * (0.5 + 0.5 * (i & 3) / 3.0);
            const double l0 = len + depth * std::sin (lfoPhase[i] * 6.283185307179586);
            lfoPhase[i] += lfoInc[i] * n; if (lfoPhase[i] >= 1.0) lfoPhase[i] -= 1.0;
            const double l1 = len + depth * std::sin (lfoPhase[i] * 6.283185307179586);
            lenNow[i] = l0; lenStep[i] = (l1 - l0) / std::max (1, n);
        }
        const int lineMask = -1; (void) lineMask;
        for (int s = 0; s < n; ++s)
        {
            sizeSm += (sizeT - sizeSm) * 0.00002;      // SIZE glides slowly (a jump would be a pitch sweep; the lengths above follow per block)
            wetSm += (mixT - wetSm) * smA; preSm += (preT - preSm) * smA; widthSm += (widthT - widthSm) * smA;   // 30 ms glides (no zipper)
            const float preSamps = preSm, widthS = widthSm;
            float inL = L[s], inR = R[s];
            const float dryL = inL, dryR = inR;
            // pre-delay (whole samples + linear)
            pre[0][(size_t) preW] = inL; pre[1][(size_t) preW] = inR;
            {
                const int plen = (int) pre[0].size();
                const float rp = (float) preW - preSamps; int ri = (int) std::floor (rp); const float fr = (float) ((float) rp - (float) ri);
                while (ri < 0) ri += plen;
                const int r0 = ri % plen, r1 = (ri + 1) % plen;
                inL = pre[0][(size_t) r0] + (pre[0][(size_t) r1] - pre[0][(size_t) r0]) * fr;
                inR = pre[1][(size_t) r0] + (pre[1][(size_t) r1] - pre[1][(size_t) r0]) * fr;
            }
            preW = (preW + 1) % (int) pre[0].size();
            // wet LO CUT (2nd order: two one-poles) and HI CUT (one-pole) on the input to the tank
            for (int c = 0; c < 2; ++c)
            {
                float& x = c == 0 ? inL : inR;
                inHp[c] = hpA * inHp[c] + (1.0f - hpA) * x; x -= inHp[c];
                inHp2[c] = hpA * inHp2[c] + (1.0f - hpA) * x; x -= inHp2[c];
                inLp[c] = lpA * inLp[c] + (1.0f - lpA) * x; x = inLp[c];
            }
            // input diffusion: 4 all-passes in series per channel
            for (int c = 0; c < 2; ++c)
            {
                float& x = c == 0 ? inL : inR;
                for (int i = 0; i < AP; ++i)
                {
                    auto& buf = ap[c][i]; const int len = (int) buf.size() - 8;
                    const int w = apW % len;
                    const float d = buf[(size_t) w];
                    const float y = -apG * x + d;
                    buf[(size_t) w] = x + apG * y;
                    x = y;
                }
            }
            ++apW;
            // read the 8 lines (four of them modulated), damp, feed back through a Householder reflection
            float v[N];
            for (int i = 0; i < N; ++i)
            {
                auto& buf = lines[i]; const int cap = (int) buf.size();
                lenNow[i] += lenStep[i];
                const double rp = (double) lineW - lenNow[i]; int ri = (int) std::floor (rp); const float fr = (float) (rp - ri);
                ri %= cap; if (ri < 0) ri += cap;
                const int r1 = ri + 1 == cap ? 0 : ri + 1;
                float x = buf[(size_t) ri] + (buf[(size_t) r1] - buf[(size_t) ri]) * fr;
                // in-loop damping: HF one-pole, LF shelf-ish (subtract a one-pole low pass, scaled)
                dampZ[i] = da * dampZ[i] + (1.0f - da) * x; x = dampZ[i];
                if (la > 0.0f) { lowZ[i] = la * lowZ[i] + (1.0f - la) * x; x -= 0.5f * lowZ[i]; }
                v[i] = x * g[i];
            }
            // outputs: odd / even lines to L / R (decorrelated), alternating signs so no line dominates
            float wetL = 0, wetR = 0;
            for (int i = 0; i < N; ++i) { const float sg = (i & 2) ? -1.0f : 1.0f; if (i & 1) wetR += v[i] * sg; else wetL += v[i] * sg; }
            // feedback through a 16-point Hadamard (fast butterfly, scaled 1/4: lossless)
            float u[N]; for (int i = 0; i < N; ++i) u[i] = v[i];
            for (int len = 1; len < N; len <<= 1)
                for (int j = 0; j < N; j += len << 1)
                    for (int k = j; k < j + len; ++k) { const float a = u[k], b = u[k + len]; u[k] = a + b; u[k + len] = a - b; }
            for (int i = 0; i < N; ++i)
            {
                const float in = (i & 1) ? inR : inL;
                lines[i][(size_t) lineW] = u[i] * 0.25f + in * ((i & 3) == 0 || (i & 3) == 3 ? 0.35f : -0.35f);
            }
            lineW = lineW + 1 == (int) lines[0].size() ? 0 : lineW + 1;       // (all lines share one capacity rule: the longest)
            wetL *= 0.35f; wetR *= 0.35f;
            // M/S width
            { const float m = 0.5f * (wetL + wetR), sd = 0.5f * (wetL - wetR) * widthS; wetL = m + sd; wetR = m - sd; }
            // ducking by the dry
            if (duckAmt > 0.0f)
            {
                const float lvl = std::max (std::abs (dryL), std::abs (dryR));
                duckEnv += (lvl > duckEnv ? duckA : duckR) * (lvl - duckEnv);
                const float gd = 1.0f - duckAmt * std::min (1.0f, duckEnv * 2.0f);
                wetL *= gd; wetR *= gd;
            }
            // equal-power mix
            const float a = wetSm * 1.5707963f, gw = std::sin (a), gdry = std::cos (a);
            L[s] = dryL * gdry + wetL * gw; R[s] = dryR * gdry + wetR * gw;
        }
    }

private:
    double sr = 48000; double lenBase[N] {}; float g[N] {};
    std::vector<float> lines[N]; int lineW = 0;
    std::vector<float> ap[2][AP]; int apW = 0;
    std::vector<float> pre[2]; int preW = 0;
    double lfoPhase[N] {}, lfoInc[N] {};
    float dampZ[N] {}, lowZ[N] {}, inLp[2] {}, inHp[2] {}, inHp2[2] {};
    float duckEnv = 0, duckA = 0.01f, duckR = 0.001f, smA = 0.001f, wetSm = -1, preSm = -1, widthSm = -1; double sizeSm = -1;
};
} // namespace ghosty
