#pragma once
// WARP + SPECTRAL modes (round 3 item 2, 2026-10-24). Original DSP; the behaviour list is Serum's manual (behaviour only),
// the maths are ours. Audio-thread rule: nothing here allocates; the spectral part runs on the message thread (table builds).
//
//   WARP      per oscillator two slots in series, mode + amount (0 = exactly off: the slot is skipped). Phase warps bend the read
//             position of the band-limited table (the mip is chosen for the steepest slope they can make, so the read itself never
//             leaves its band; kinks and steps still add partials - Quantize / Sync get a BLEP, the rest rely on QUALITY); output warps
//             shape or multiply the read sample (Tube, Zero-Square, AM, RM); FM / PD read a source (the other oscillator, the sub,
//             the noise) at the voice rate, FM through zero.
//   SPECTRAL  one slot per oscillator: the frame's spectrum is reshaped before the mips are built (so the result stays band-limited by
//             construction); the amount is a knob (the tables rebuild on change with the usual 8 ms cross-fade), not an audio-rate target.
#include <cmath>
#include <vector>
#include <complex>
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

namespace gpro
{
enum WarpMode { wOff = 0, wSync, wBendUp, wBendDown, wSqueeze, wAsym, wMirror, wQuantize, wFormant, wPulseWidth,
                wFmOsc, wFmSub, wFmNoise, wPdOsc, wPdSub, wPdNoise, wAmOsc, wAmSub, wAmNoise, wRmOsc, wRmSub, wRmNoise,
                wTube, wZeroSquare, numWarpModes };
inline const juce::StringArray& warpModeNames()
{
    static juce::StringArray s { "Off", "Sync", "Bend +", "Bend -", "Squeeze", "Asym", "Mirror", "Quantize", "Formant", "Pulse Width",
                                 "FM Osc", "FM Sub", "FM Noise", "PD Osc", "PD Sub", "PD Noise", "AM Osc", "AM Sub", "AM Noise", "RM Osc", "RM Sub", "RM Noise",
                                 "Tube", "Zero-Square" };
    return s;
}
inline bool warpIsPhase (int m) noexcept { return (m >= wBendUp && m <= wPulseWidth) || (m >= wPdOsc && m <= wPdNoise); }
inline bool warpIsFm (int m) noexcept { return m >= wFmOsc && m <= wFmNoise; }
inline bool warpIsOut (int m) noexcept { return (m >= wAmOsc && m <= wRmNoise) || m == wTube || m == wZeroSquare; }
inline int  warpSource (int m) noexcept { return m >= wFmOsc && m <= wRmNoise ? (m - wFmOsc) % 3 : -1; }   // 0 other osc, 1 sub, 2 noise

// the warped read position, p in [0, 1); a > 0. Quantize also reports its step index (for the BLEP at step changes)
inline double warpPhase (int m, double p, float a, int* qStep = nullptr) noexcept
{
    switch (m)
    {
        // Bend / Asym are sinusoidal phase bends: smooth across the cycle's wrap (no kink there), so the read adds no partials of its own
        case wBendUp:   { const double v = p - 0.95 * a * std::sin (6.283185307179586 * p) / 6.283185307179586; return v - std::floor (v); }   // the start stretched, the end squeezed
        case wBendDown: { const double v = p + 0.95 * a * std::sin (6.283185307179586 * p) / 6.283185307179586; return v - std::floor (v); }
        case wSqueeze:  { const double x = 2.0 * p - 1.0; return 0.5 + 0.5 * (x < 0 ? -std::pow (-x, 1.0 + 3.0 * a) : std::pow (x, 1.0 + 3.0 * a)); }   // the middle slowed, the ends rushed
        case wAsym:     { const double v = p + 0.15 * a * std::sin (12.566370614359172 * p); return v - std::floor (v); }                    // alternate halves squeezed / stretched
        case wMirror:   { const double mir = p < 0.5 ? 2.0 * p : 2.0 - 2.0 * p; return p + a * (mir - p); }   // the second half plays backwards
        case wQuantize:
        {
            const int n = (int) std::lround (4.0 + (1.0 - a) * 60.0);                             // 64 steps .. 4 steps
            const int k = (int) (p * n); if (qStep) *qStep = k;
            return (double) k / (double) n;
        }
        case wFormant:
        {   // the cycle squeezed into 1 / s of its time, the rest holds the end; the corner is rounded over 3 % of the cycle (a raised cosine
            // between slope s and 0) so it adds far less than a sharp one
            const double s = 1.0 + 3.0 * a, edge = 1.0 / s, soft = 0.03;
            if (p <= edge - soft) return p * s;
            if (p >= edge + soft) return 1.0;
            const double x = (p - (edge - soft)) / (2.0 * soft);                                 // 0 .. 1 across the rounding
            return std::min (1.0, (edge - soft) * s + s * 2.0 * soft * (x - 0.5 * x * x));          // slope eases linearly from s to 0 (meets 1 at the end)
        }
        case wPulseWidth:
        {
            const double w = 0.5 * (1.0 - 0.9 * a);                                               // the first half of the wave squeezed into w of the cycle
            return p < w ? 0.5 * p / w : 0.5 + 0.5 * (p - w) / (1.0 - w);
        }
        default: return p;
    }
}
// the steepest |d warped / d p| a mode can make at this amount: the mip is chosen for f x this, so the table read stays in its band
inline float warpSlope (int m, float a) noexcept
{
    switch (m)
    {
        case wBendUp: case wBendDown: return 1.0f + 0.95f * a;
        case wSqueeze: case wFormant: return 1.0f + 3.0f * a;
        case wAsym: return 1.0f + 0.15f * 12.57f * a;
        case wMirror: return 1.0f + a;
        case wPulseWidth: return 1.0f / (1.0f - 0.9f * a);
        case wSync: return 1.0f + 5.0f * a;
        case wFmOsc: case wFmSub: case wFmNoise: return 1.0f + 2.0f * a;
        case wPdOsc: case wPdSub: case wPdNoise: return 1.0f + 2.0f * a;
        default: return 1.0f;
    }
}
// output warps: v = the read sample, src = the source for AM / RM (-1 .. 1)
inline float warpOut (int m, float v, float a, float src) noexcept
{
    switch (m)
    {
        case wAmOsc: case wAmSub: case wAmNoise: return v * (1.0f + a * src);                     // amplitude modulation (sidebands, carrier kept)
        case wRmOsc: case wRmSub: case wRmNoise: return v * ((1.0f - a) + a * src);               // ring modulation (carrier gone at 100 %)
        case wTube:
        {   // soft, slightly asymmetric saturation (even harmonics), level-normalised; blended in by the amount
            const float g = 1.0f + 6.0f * a, bias = 0.3f * a;
            const float s = (std::tanh (v * g + bias) - std::tanh (bias)) / std::tanh (g);
            return v + a * (s - v);
        }
        case wZeroSquare: return v + a * (0.9f * std::tanh (v * 12.0f) - v);                       // pushed towards a square of its own sign
        default: return v;
    }
}

// ---------------------------------------------------------------- SPECTRAL modes (message thread)
enum SpecMode { sOff = 0, sSmear, sHarmScale, sInharm, sDisperse, sSkew, sRandomAmps, sLowPass, sHighPass, sShepard, numSpecModes };
inline const juce::StringArray& specModeNames()
{
    static juce::StringArray s { "Off", "Smear", "Harmonic Scale", "Inharmonic", "Phase Disperse", "Skew", "Random Amps", "Low Pass", "High Pass", "Shepard" };
    return s;
}
// reshapes one single-cycle frame (any power-of-two length 256 .. 2048) in the spectral domain; a > 0; the output keeps the input's RMS
inline std::vector<float> spectralProcess (const std::vector<float>& src, int mode, float a)
{
    const int N = (int) src.size(); int order = 0; while ((1 << order) < N) ++order;
    juce::dsp::FFT fft (order);
    using C = std::complex<float>;
    std::vector<C> in ((size_t) N), spec ((size_t) N), out ((size_t) N), res ((size_t) N);
    double rmsIn = 0;
    for (int i = 0; i < N; ++i) { in[(size_t) i] = C (src[(size_t) i], 0.0f); rmsIn += (double) src[(size_t) i] * src[(size_t) i]; }
    fft.perform (in.data(), spec.data(), false);
    const int H = N / 2 - 1;                                                       // partials 1 .. H
    std::vector<float> mag ((size_t) H + 1, 0.0f), ph ((size_t) H + 1, 0.0f), mag2 ((size_t) H + 1, 0.0f), ph2 ((size_t) H + 1, 0.0f);
    for (int k = 1; k <= H; ++k) { mag[(size_t) k] = std::abs (spec[(size_t) k]); ph[(size_t) k] = std::arg (spec[(size_t) k]); }
    auto hash01 = [] (juce::uint32 x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return (float) (x & 0xffffff) / 16777215.0f; };
    switch (mode)
    {
        case sSmear:
        {   // magnitudes blurred across neighbouring partials (width grows with the amount), phases kept
            const int w = (int) std::lround (a * a * 24.0f) + 1;
            for (int k = 1; k <= H; ++k)
            {
                double acc = 0, wsum = 0;
                for (int j = -w; j <= w; ++j) { const int q = k + j; if (q < 1 || q > H) continue; const double g = 1.0 - std::abs ((double) j) / (w + 1); acc += g * mag[(size_t) q]; wsum += g; }
                mag2[(size_t) k] = (float) (acc / std::max (1e-9, wsum)); ph2[(size_t) k] = ph[(size_t) k];
            }
            break;
        }
        case sHarmScale:
        {   // partial k moves to k x (1 + 1.5 a): the spectrum stretched up (what falls above the frame's top is gone)
            const double s = 1.0 + 1.5 * a;
            for (int k = 1; k <= H; ++k) { const int q = (int) std::lround (k * s); if (q >= 1 && q <= H) { mag2[(size_t) q] += mag[(size_t) k]; ph2[(size_t) q] = ph[(size_t) k]; } }
            break;
        }
        case sInharm:
        {   // partial k moves to k + a k^2 / 64: the top drifts sharp like a stiff string
            for (int k = 1; k <= H; ++k) { const int q = (int) std::lround (k + a * k * k / 64.0); if (q >= 1 && q <= H) { mag2[(size_t) q] += mag[(size_t) k]; ph2[(size_t) q] = ph[(size_t) k]; } }
            break;
        }
        case sDisperse:
        {   // all-pass dispersion: the phase of partial k turned by a pi k^2 / 48 (the shape smears, the spectrum stays)
            for (int k = 1; k <= H; ++k) { mag2[(size_t) k] = mag[(size_t) k]; ph2[(size_t) k] = ph[(size_t) k] + (float) (a * juce::MathConstants<double>::pi * k * k / 48.0); }
            break;
        }
        case sSkew:
        {   // the balance tilted up: partial k gains (k / 32)^(2 a) (above 32 it keeps growing: a brighter, thinner wave)
            for (int k = 1; k <= H; ++k) { mag2[(size_t) k] = mag[(size_t) k] * (float) std::pow (k / 32.0, 2.0 * a); ph2[(size_t) k] = ph[(size_t) k]; }
            break;
        }
        case sRandomAmps:
        {   // every partial scaled by a fixed random factor (seeded by its index: the same every time, on every machine)
            for (int k = 1; k <= H; ++k) { mag2[(size_t) k] = mag[(size_t) k] * (1.0f + a * (hash01 ((juce::uint32) k * 2654435761U + 17u) * 2.0f - 1.0f) * 0.95f); ph2[(size_t) k] = ph[(size_t) k]; }
            break;
        }
        case sLowPass:
        {   // a spectral low pass: partials above kc = H (1 - a)^2 fade over an octave
            const double kc = std::max (1.0, H * std::pow (1.0 - a, 2.0));
            for (int k = 1; k <= H; ++k) { const double r = k / kc; const float g = r <= 1.0 ? 1.0f : (float) std::max (0.0, 1.0 - (r - 1.0)); mag2[(size_t) k] = mag[(size_t) k] * g; ph2[(size_t) k] = ph[(size_t) k]; }
            break;
        }
        case sHighPass:
        {   // a spectral high pass: partials below kc = 1 + H a^2 fade over an octave below it
            const double kc = 1.0 + H * a * a;
            for (int k = 1; k <= H; ++k) { const double r = kc / k; const float g = r <= 1.0 ? 1.0f : (float) std::max (0.0, 1.0 - (r - 1.0)); mag2[(size_t) k] = mag[(size_t) k] * g; ph2[(size_t) k] = ph[(size_t) k]; }
            break;
        }
        case sShepard:
        {   // octave copies (x1/4 .. x4) under a bell over the octaves, blended in by the amount: the endless-staircase tone
            for (int k = 1; k <= H; ++k) { mag2[(size_t) k] = mag[(size_t) k]; ph2[(size_t) k] = ph[(size_t) k]; }
            for (int oct = -2; oct <= 2; ++oct)
            {
                if (oct == 0) continue;
                const float w = a * std::exp (-0.5f * (float) (oct * oct) / 1.2f);
                for (int k = 1; k <= H; ++k)
                {
                    const int q = oct > 0 ? k << oct : k >> -oct;
                    if (q < 1 || q > H || (oct < 0 && (k % (1 << -oct)) != 0)) continue;
                    mag2[(size_t) q] += w * mag[(size_t) k];
                }
            }
            break;
        }
        default: for (int k = 1; k <= H; ++k) { mag2[(size_t) k] = mag[(size_t) k]; ph2[(size_t) k] = ph[(size_t) k]; } break;
    }
    for (int k = 1; k <= H; ++k) { out[(size_t) k] = std::polar (mag2[(size_t) k], ph2[(size_t) k]); out[(size_t) (N - k)] = std::conj (out[(size_t) k]); }
    fft.perform (out.data(), res.data(), true);
    std::vector<float> dst ((size_t) N);
    double rmsOut = 0;
    for (int i = 0; i < N; ++i) { dst[(size_t) i] = res[(size_t) i].real(); rmsOut += (double) dst[(size_t) i] * dst[(size_t) i]; }
    const float g = rmsOut > 1e-12 ? (float) std::sqrt (rmsIn / rmsOut) : 1.0f;
    float pk = 0; for (auto& v : dst) { v *= g; pk = std::max (pk, std::abs (v)); }
    if (pk > 1.0f) for (auto& v : dst) v /= pk;                                      // (never hotter than full scale: the shape, not the level, is the point)
    return dst;
}
inline std::vector<std::vector<float>> spectralProcessFrames (const std::vector<std::vector<float>>& frames, int mode, float a)
{
    std::vector<std::vector<float>> out; out.reserve (frames.size());
    for (auto& f : frames) out.push_back (spectralProcess (f, mode, a));
    return out;
}
} // namespace gpro
