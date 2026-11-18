#pragma once
// TONE EQ (Ghosty Pro, FX RACK round 3, 2026-10-25). Original DSP (no third-party code).
//   3 parametric bells (FREQ, GAIN +-18 dB, Q) and a TILT (+-12 dB around 1 kHz: highs up, lows down by half each), OUTPUT.
//   Zero-delay-feedback state-variable bells (TPT form): every control glides per sample, coefficients move every sample, no
//   zipper; at 0 dB a band passes the signal untouched. A light, quick EQ (also usable inside a SPLITTER band); SPIRIT EQ stays
//   the precision tool.
#include "FxProCommon.h"

#if ! GPFX_BOX
namespace gpfx
{
class Eq3Unit : public UnitDsp
{
public:
    Telemetry* tm = nullptr;
    void attach (juce::AudioProcessorValueTreeState& s) override
    {
        static const char* b[] { "b1", "b2", "b3" };
        for (int k = 0; k < 3; ++k)
        {
            const juce::String p = b[k];
            freq[(size_t) k].bind (s, pid (uEq3, (p + "_freq").toRawUTF8()), defFreq (k)); gain[(size_t) k].bind (s, pid (uEq3, (p + "_gain").toRawUTF8()), 0.0f);
            q[(size_t) k].bind (s, pid (uEq3, (p + "_q").toRawUTF8()), 0.9f);
        }
        tilt.bind (s, pid (uEq3, "tilt"), 0.0f); out.bind (s, pid (uEq3, "out"), 0.0f);
    }
    static float defFreq (int k) { static const float f[] { 120.0f, 1000.0f, 6000.0f }; return f[k]; }
    void prepare (double sr_, int) override
    {
        sr = sr_;
        for (auto& e : fSm) e.setTime (sr, 25.0f);
        for (auto& e : gSm) e.setTime (sr, 25.0f);
        for (auto& e : qSm) e.setTime (sr, 25.0f);
        tiltSm.setTime (sr, 25.0f); outSm.setTime (sr, 20.0f);
        tiltG = (float) std::tan (juce::MathConstants<double>::pi * 1000.0 / sr);
        reset();
    }
    void reset() override
    {
        for (auto& b : st) for (auto& c : b) c = {};
        tz[0] = tz[1] = 0;
        for (int k = 0; k < 3; ++k) { fSm[(size_t) k].snap (std::log2 (fNow (k))); gSm[(size_t) k].snap (gNow (k)); qSm[(size_t) k].snap (qNow (k)); }
        tiltSm.snap (juce::jlimit (-12.0f, 12.0f, tilt())); outSm.snap (dbToGain (out()));
    }
    void process (float* L, float* R, int n, const BlockCtx&) override
    {
        for (int k = 0; k < 3; ++k) { fSm[(size_t) k].target = std::log2 (fNow (k)); gSm[(size_t) k].target = gNow (k); qSm[(size_t) k].target = qNow (k); }
        tiltSm.target = juce::jlimit (-12.0f, 12.0f, tilt()); outSm.target = dbToGain (juce::jlimit (-24.0f, 12.0f, out()));
        for (int i = 0; i < n; ++i)
        {
            float x[2] { L[i], R[i] };
            for (int k = 0; k < 3; ++k)
            {
                const float f = std::exp2 (fSm[(size_t) k].next()), gdb = gSm[(size_t) k].next(), qq = qSm[(size_t) k].next();
                if (std::abs (gdb) < 1.0e-4f && gSm[(size_t) k].target == 0.0f) continue;   // (a band at 0 dB passes untouched)
                const float A = std::pow (10.0f, gdb / 40.0f), g = std::tan (kPi * juce::jlimit (10.0f, (float) sr * 0.45f, f) / (float) sr), kk = 1.0f / (qq * A);
                const float a1 = 1.0f / (1.0f + g * (g + kk)), a2 = g * a1, a3 = g * a2, m1 = kk * (A * A - 1.0f);
                for (int c = 0; c < 2; ++c)
                {
                    auto& s = st[(size_t) k][(size_t) c];
                    const float v3 = x[c] - s.ic2, v1 = a1 * s.ic1 + a2 * v3, v2 = s.ic2 + a2 * s.ic1 + a3 * v3;
                    s.ic1 = 2 * v1 - s.ic1; s.ic2 = 2 * v2 - s.ic2;
                    x[c] = x[c] + m1 * v1;
                }
            }
            const float t = tiltSm.next();
            if (t != 0.0f || tiltSm.target != 0.0f)
            {   // tilt: one-pole split at 1 kHz, lows by -t/2 dB, highs by +t/2 dB
                const float gl = dbToGain (-0.5f * t), gh = dbToGain (0.5f * t);
                for (int c = 0; c < 2; ++c)
                {
                    const float v = (x[c] - tz[c]) * tiltG / (1.0f + tiltG), lo = v + tz[c]; tz[c] = lo + v;
                    x[c] = lo * gl + (x[c] - lo) * gh;
                }
            }
            else for (int c = 0; c < 2; ++c) { const float v = (x[c] - tz[c]) * tiltG / (1.0f + tiltG); tz[c] = v + tz[c] + v; }   // (keeps the state current: no step when TILT moves off 0)
            const float o = outSm.next();
            L[i] = x[0] * o; R[i] = x[1] * o;
        }
    }
    // magnitude (dB) at frequency f for the editor and the tests (the analog prototype the TPT bells match)
    static double responseDb (double f, const float fr[3], const float gd[3], const float qs[3], float tiltDb, double sRate = 48000.0)
    {   // (the bilinear map of the TPT filters: w = tan (pi f / sr) / tan (pi fc / sr), so the curve is the digital one)
        auto warp = [sRate] (double ff, double fc) { const double ny = sRate * 0.4999; return std::tan (juce::MathConstants<double>::pi * std::min (ff, ny) / sRate) / std::tan (juce::MathConstants<double>::pi * std::min (fc, sRate * 0.45) / sRate); };
        double db = 0;
        for (int k = 0; k < 3; ++k)
        {
            if (gd[k] == 0.0f) continue;
            const double A = std::pow (10.0, gd[k] / 40.0), w = warp (f, fr[k]), kk = 1.0 / (qs[k] * A);
            // H(s) = (s^2 + s k A^2 + 1) / (s^2 + s k + 1) at s = j w (bandwidth set by k, gain A^2 at the centre)
            const std::complex<double> s (0.0, w), num = s * s + s * kk * A * A + 1.0, den = s * s + s * kk + 1.0;
            db += 20.0 * std::log10 (std::abs (num / den));
        }
        if (tiltDb != 0.0f)
        {
            const double w = warp (f, 1000.0); const std::complex<double> s (0.0, w);
            const auto lo = 1.0 / (s + 1.0), hi = s / (s + 1.0);
            db += 20.0 * std::log10 (std::abs (lo * std::pow (10.0, -0.25 * tiltDb / 10.0) + hi * std::pow (10.0, 0.25 * tiltDb / 10.0)));
        }
        return db;
    }
private:
    float fNow (int k) const { return juce::jlimit (20.0f, 20000.0f, freq[(size_t) k]()); }
    float gNow (int k) const { return juce::jlimit (-18.0f, 18.0f, gain[(size_t) k]()); }
    float qNow (int k) const { return juce::jlimit (0.2f, 12.0f, q[(size_t) k]()); }
    struct S { float ic1 = 0, ic2 = 0; };
    std::array<P, 3> freq, gain, q; P tilt, out;
    double sr = 48000; float tiltG = 0.07f; float tz[2] {};
    std::array<std::array<S, 2>, 3> st {};
    std::array<ExpSmooth, 3> fSm, gSm, qSm; ExpSmooth tiltSm, outSm;
};

inline void addEq3Parameters (juce::AudioProcessorValueTreeState::ParameterLayout& l)
{
    auto T = [] (const juce::String& s) { return juce::String ("FX ") + unitInfo (uEq3).title + " " + s; };
    static const char* nm[] { "Low", "Mid", "High" };
    for (int k = 0; k < 3; ++k)
    {
        const juce::String p = "b" + juce::String (k + 1);
        addFloat (l, pid (uEq3, (p + "_freq").toRawUTF8()), T (juce::String (nm[k]) + " Freq"), skewRange (20.0f, 20000.0f, 1000.0f), Eq3Unit::defFreq (k), "Hz", 0);
        addFloat (l, pid (uEq3, (p + "_gain").toRawUTF8()), T (juce::String (nm[k]) + " Gain"), juce::NormalisableRange<float> (-18.0f, 18.0f), 0.0f, "dB", 1);
        addFloat (l, pid (uEq3, (p + "_q").toRawUTF8()), T (juce::String (nm[k]) + " Q"), skewRange (0.2f, 12.0f, 1.0f), 0.9f, "", 2);
    }
    addFloat (l, pid (uEq3, "tilt"), T ("Tilt"), juce::NormalisableRange<float> (-12.0f, 12.0f), 0.0f, "dB", 1);
    addFloat (l, pid (uEq3, "out"), T ("Output"), juce::NormalisableRange<float> (-24.0f, 12.0f), 0.0f, "dB", 1);
}
} // namespace gpfx
#endif // ! GPFX_BOX
