#pragma once
// SPIRIT EQ: filter design maths shared by the audio engine and the editor.
// Zero-latency bands use "matched" second-order sections: poles by impulse invariance of the analog prototype and the
// numerator fitted so the digital magnitude equals the analog one at DC, at Nyquist and at a third frequency
// (Vicanek, "Matched Second Order Digital Filters", 2016). This removes the bilinear "cramping" of bells and shelves near
// Nyquist, giving the analog-like ("natural") response with zero latency.
#include "FxCore.h"
#include "FxEq.h"

namespace gpfx {
namespace eq {

constexpr int kMaxSections = 8;
constexpr double kPiD = 3.14159265358979323846;

enum Placement { plStereo, plLeft, plRight, plMid, plSide, kNumPlacements };

inline int slopeOrder (int slopeIdx) { static const int o[] { 1, 2, 3, 4, 6, 8, 12, 16 }; return o[juce::jlimit (0, 7, slopeIdx)]; }
inline bool isGainType (int t) { return t == btBell || t == btLowShelf || t == btHighShelf || t == btTiltShelf || t == btFlatTilt; }
inline bool usesSlope (int t) { return t == btLowCut || t == btHighCut || t == btBandPass; }

struct Sections
{
    std::array<BiquadCoefs, kMaxSections> s {}; int n = 0; float scale = 1.0f;
    double mag (double f, double sr) const
    {
        double m = std::abs ((double) scale);
        for (int i = 0; i < n; ++i) m *= biquadMag (s[(size_t) i], f, sr);
        return m;
    }
};

struct BandSpec { int type = btBell, slope = 1; double freq = 1000, gain = 0, q = 1; };

namespace matched {

// H(s) = (n2 s^2 + n1 s + n0) / (s^2 + d1 s + d0), s in radians per sample.
struct Analog2 { double n0 = 0, n1 = 0, n2 = 0, d0 = 1, d1 = 1; };
inline double mag2 (const Analog2& a, double w)
{
    const double re = a.n0 - a.n2 * w * w, im = a.n1 * w, dre = a.d0 - w * w, dim = a.d1 * w;
    return (re * re + im * im) / std::max (1e-300, dre * dre + dim * dim);
}
// Impulse-invariant pole pair for an analog resonance at w0 (rad/sample) with quality Q.
inline void poles (double w0, double Q, double& a1, double& a2)
{
    w0 = juce::jlimit (1e-7, 0.985 * kPiD, w0);
    const double zeta = 1.0 / (2.0 * std::max (0.01, Q));
    const double e = std::exp (-zeta * w0);
    if (zeta <= 1.0) a1 = -2.0 * e * std::cos (w0 * std::sqrt (1.0 - zeta * zeta));
    else             a1 = -2.0 * e * std::cosh (w0 * std::sqrt (zeta * zeta - 1.0));
    a2 = std::exp (-2.0 * zeta * w0);
}
inline BiquadCoefs fit (const Analog2& a, double a1, double a2, double wm)
{
    wm = juce::jlimit (1e-4, 0.97 * kPiD, wm);
    const double A0 = (1 + a1 + a2) * (1 + a1 + a2), A1 = (1 - a1 + a2) * (1 - a1 + a2), A2 = -4.0 * a2;
    const double p1 = std::sin (wm * 0.5) * std::sin (wm * 0.5), p0 = 1.0 - p1, p2 = 4.0 * p0 * p1;
    const double B0 = A0 * mag2 (a, 0.0), B1 = A1 * mag2 (a, kPiD);
    const double Ad = A0 * p0 + A1 * p1 + A2 * p2;
    const double B2 = (mag2 (a, wm) * Ad - B0 * p0 - B1 * p1) / p2;
    const double s0 = std::sqrt (std::max (0.0, B0)), s1 = std::sqrt (std::max (0.0, B1));
    const double W = 0.5 * (s0 + s1);
    const double disc = std::max (0.0, W * W + B2);
    const double b0 = 0.5 * (W + std::sqrt (disc));
    const double b1 = 0.5 * (s0 - s1);
    const double b2 = b0 > 1e-30 ? -B2 / (4.0 * b0) : 0.0;
    return { (float) b0, (float) b1, (float) b2, (float) a1, (float) a2 };
}
inline BiquadCoefs section (const Analog2& a, double poleW, double poleQ, double wm)
{
    double a1, a2; poles (poleW, poleQ, a1, a2); return fit (a, a1, a2, wm);
}
// First order: H(s) = (n1 s + n0) / (s + p).
inline BiquadCoefs first (double n1, double n0, double p)
{
    const double pz = std::exp (-juce::jlimit (1e-7, 0.985 * kPiD, p));
    const double h0 = std::abs (n0 / p);
    const double hpi = std::sqrt ((n1 * n1 * kPiD * kPiD + n0 * n0) / (kPiD * kPiD + p * p));
    const double sum = (1.0 - pz) * h0, dif = (1.0 + pz) * hpi;   // b0 + b1, b0 - b1
    return { (float) (0.5 * (sum + dif)), (float) (0.5 * (sum - dif)), 0.0f, (float) -pz, 0.0f };
}

// Inverse of a (minimum-phase) section: swaps numerator and denominator.
inline BiquadCoefs invert (const BiquadCoefs& c)
{
    const double b0 = std::max (1e-12, (double) c.b0);
    return { (float) (1.0 / b0), (float) (c.a1 / b0), (float) (c.a2 / b0), (float) (c.b1 / b0), (float) (c.b2 / b0) };
}
inline BiquadCoefs scaled (BiquadCoefs c, double g) { c.b0 = (float) (c.b0 * g); c.b1 = (float) (c.b1 * g); c.b2 = (float) (c.b2 * g); return c; }

// Boosts are designed directly (the poles are the sharp, well-conditioned part); cuts are the exact inverse of the
// matching boost, as in Vicanek's paper, so their sharp zeros are matched as precisely as the boost's poles.
inline BiquadCoefs bellBoost (double w0, double Q, double gainDb)
{
    const double A = std::pow (10.0, gainDb / 40.0);
    Analog2 a; a.n2 = 1; a.n1 = A * w0 / Q; a.n0 = w0 * w0; a.d1 = w0 / (A * Q); a.d0 = w0 * w0;
    return section (a, w0, A * Q, juce::jmin (w0, 0.6 * kPiD));
}
inline BiquadCoefs bell (double w0, double Q, double gainDb)
{
    if (std::abs (gainDb) < 1e-4) return {};
    return gainDb > 0 ? bellBoost (w0, Q, gainDb) : invert (bellBoost (w0, Q, -gainDb));
}
inline BiquadCoefs lowShelfBoost (double w0, double Q, double gainDb)
{
    const double A = std::pow (10.0, gainDb / 40.0), sA = std::sqrt (A);
    Analog2 a; a.n2 = 1; a.n1 = sA * w0 / Q; a.n0 = A * w0 * w0; a.d1 = w0 / (Q * sA); a.d0 = w0 * w0 / A;
    return section (a, w0 / sA, Q, juce::jmin (w0, 0.75 * kPiD));
}
inline BiquadCoefs lowShelf (double w0, double Q, double gainDb)
{
    if (std::abs (gainDb) < 1e-4) return {};
    return gainDb > 0 ? lowShelfBoost (w0, Q, gainDb) : invert (lowShelfBoost (w0, Q, -gainDb));
}
// High shelf(g) == 10^(g/20) * low shelf(-g) exactly (same transition), so it reuses the low-shelf designs.
inline BiquadCoefs highShelf (double w0, double Q, double gainDb)
{
    if (std::abs (gainDb) < 1e-4) return {};
    const double G = std::pow (10.0, gainDb / 20.0);
    return scaled (gainDb > 0 ? invert (lowShelfBoost (w0, Q, gainDb)) : lowShelfBoost (w0, Q, -gainDb), G);
}
inline BiquadCoefs lowpass (double w0, double Q)
{
    Analog2 a; a.n0 = w0 * w0; a.d1 = w0 / Q; a.d0 = w0 * w0;
    return section (a, w0, Q, juce::jmin (w0, 0.8 * kPiD));
}
inline BiquadCoefs highpass (double w0, double Q)
{
    Analog2 a; a.n2 = 1; a.d1 = w0 / Q; a.d0 = w0 * w0;
    return section (a, w0, Q, juce::jmin (w0, 0.8 * kPiD));
}
inline BiquadCoefs bandpass (double w0, double Q)
{
    Analog2 a; a.n1 = w0 / Q; a.d1 = w0 / Q; a.d0 = w0 * w0;
    return section (a, w0, Q, juce::jmin (w0, 0.9 * kPiD));
}
// Notch: zeros exactly on the unit circle at w0, unity at DC and Nyquist; the bandwidth is set so the lower -3 dB edge
// lands exactly where the analog notch has it (the upper edge may lie beyond Nyquist and cannot be matched).
inline BiquadCoefs notch (double w0, double Q)
{
    w0 = juce::jlimit (1e-5, 0.995 * kPiD, w0);
    const double fl = w0 * (std::sqrt (1.0 + 4.0 * Q * Q) - 1.0) / (2.0 * Q);   // analog lower -3 dB edge (rad/sample)
    const double O0 = std::tan (w0 * 0.5), Ol = std::tan (juce::jlimit (1e-6, w0 * 0.999, fl) * 0.5);
    const double Oh = O0 * O0 / Ol;
    const double Qd = O0 / std::max (1e-9, Oh - Ol);
    const double alpha = std::sin (w0) / (2.0 * Qd), cw = std::cos (w0), a0 = 1.0 + alpha;
    return { (float) (1.0 / a0), (float) (-2.0 * cw / a0), (float) (1.0 / a0), (float) (-2.0 * cw / a0), (float) ((1.0 - alpha) / a0) };
}
} // namespace matched

// Full band design (all sections) at sample rate sr. gainDb is the effective gain (static * scale + dynamic).
inline void designBand (const BandSpec& b, double sr, Sections& out)
{
    out.n = 0; out.scale = 1.0f;
    const double f = juce::jlimit (5.0, 40000.0, b.freq), w0 = 2.0 * kPiD * f / sr;
    const double q = juce::jlimit (0.025, 40.0, b.q), g = juce::jlimit (-36.0, 36.0, b.gain);
    auto add = [&] (const BiquadCoefs& c) { if (out.n < kMaxSections) out.s[(size_t) out.n++] = c; };
    switch (b.type)
    {
        case btBell:      add (matched::bell (w0, q, g)); break;
        case btLowShelf:  add (matched::lowShelf (w0, juce::jlimit (0.1, 4.0, q), g)); break;
        case btHighShelf: add (matched::highShelf (w0, juce::jlimit (0.1, 4.0, q), g)); break;
        case btTiltShelf: add (matched::highShelf (w0, juce::jlimit (0.1, 4.0, q), g)); out.scale = (float) std::pow (10.0, -g / 40.0); break;
        case btNotch:     add (matched::notch (w0, q)); break;
        case btBandPass:
        {
            const int k = juce::jlimit (1, 4, slopeOrder (b.slope) / 2);
            for (int i = 0; i < k; ++i) add (matched::bandpass (w0, q * (k > 1 ? 0.6 + 0.2 * k : 1.0)));
            break;
        }
        case btLowCut: case btHighCut:
        {
            const int order = slopeOrder (b.slope);
            const bool lp = b.type == btHighCut;
            const double res = q / 0.70710678;
            if (order % 2 == 1) add (lp ? matched::first (0.0, w0, w0) : matched::first (1.0, 0.0, w0));
            const int pairs = order / 2;
            for (int k = 0; k < pairs; ++k)
            {
                double Qk = order % 2 == 1 ? 1.0 / (2.0 * std::cos (kPiD * (k + 1.0) / order))
                                           : 1.0 / (2.0 * std::cos (kPiD * (2.0 * k + 1.0) / (2.0 * order)));
                if (k == pairs - 1) Qk *= res;   // the last section has the highest Q: the resonance goes there
                add (lp ? matched::lowpass (w0, juce::jlimit (0.2, 40.0, Qk)) : matched::highpass (w0, juce::jlimit (0.2, 40.0, Qk)));
            }
            break;
        }
        case btFlatTilt:
        {
            // whole-spectrum tilt: g dB across ~10 octaves (20 Hz..20 kHz), pivoting at f, built from 1st-order shelves
            const double slope = g / 10.0;                // dB per octave
            const double centres[] { 40, 160, 640, 2560, 10240 };
            for (double fc : centres)
            {
                const double G = std::pow (10.0, (2.0 * slope) / 20.0), g2 = std::sqrt (G);
                const double wc = 2.0 * kPiD * fc / sr;
                add (matched::first (g2, g2 * wc / g2, wc * g2));
            }
            const double m = out.mag (f, sr);
            out.scale = (float) (1.0 / std::max (1e-6, m));
            break;
        }
        default: break;
    }
}

// Analog reference magnitude (dB) of a band, used by the self-test to verify the matched designs.
inline double analogRefDb (const BandSpec& b, double f, double sr)
{
    const double w = 2.0 * kPiD * f / sr, w0 = 2.0 * kPiD * b.freq / sr, q = b.q, g = b.gain;
    matched::Analog2 a;
    const double A = std::pow (10.0, g / 40.0), sA = std::sqrt (A);
    switch (b.type)
    {
        case btBell: a.n2 = 1; a.n1 = A * w0 / q; a.n0 = w0 * w0; a.d1 = w0 / (A * q); a.d0 = w0 * w0; break;
        case btLowShelf: a.n2 = 1; a.n1 = sA * w0 / q; a.n0 = A * w0 * w0; a.d1 = w0 / (q * sA); a.d0 = w0 * w0 / A; break;
        case btHighShelf: a.n2 = A * A; a.n1 = A * sA * w0 / q; a.n0 = A * w0 * w0; a.d1 = sA * w0 / q; a.d0 = A * w0 * w0; break;
        case btNotch: a.n2 = 1; a.n0 = w0 * w0; a.d1 = w0 / q; a.d0 = w0 * w0; break;
        case btBandPass: a.n1 = w0 / q; a.d1 = w0 / q; a.d0 = w0 * w0; break;
        default: return 0;
    }
    return 10.0 * std::log10 (std::max (1e-30, matched::mag2 (a, w)));
}

// Spectral-region weight (0..1) of a band at frequency f, used by spectral dynamics.
inline float regionWeight (int type, double f, double f0, double q)
{
    const double r = f / std::max (1.0, f0);
    switch (type)
    {
        case btLowShelf: case btHighCut: { const double x = r * r; return (float) (1.0 / (1.0 + x * x)); }
        case btHighShelf: case btLowCut: { const double x = 1.0 / std::max (1e-9, r * r); return (float) (1.0 / (1.0 + x * x)); }
        case btTiltShelf: case btFlatTilt: return 1.0f;
        default: { const double x = std::max (0.3, q) * (r - 1.0 / std::max (1e-9, r)); return (float) (1.0 / (1.0 + x * x)); }
    }
}

inline juce::String noteName (double f)
{
    if (f <= 0) return {};
    static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const double midi = 69.0 + 12.0 * std::log2 (f / 440.0);
    const int m = (int) std::lround (midi);
    const int cents = (int) std::lround ((midi - m) * 100.0);
    juce::String s = juce::String (names[((m % 12) + 12) % 12]) + juce::String (m / 12 - 1);
    if (cents != 0) s << (cents > 0 ? " +" : " ") << cents << "c";
    return s;
}
inline juce::String freqText (double f)
{
    return f >= 1000.0 ? juce::String (f / 1000.0, f >= 10000.0 ? 1 : 2) + " kHz" : juce::String (f, f < 100.0 ? 1 : 0) + " Hz";
}

} // namespace eq
} // namespace gpfx
