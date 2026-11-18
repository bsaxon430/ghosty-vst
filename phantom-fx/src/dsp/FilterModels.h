// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX: PHANTOM FILTER's extra models (0.1.16), each the original project's own DSP, run through PHANTOM FILTER's controls
// (cutoff, resonance, slope, spread, routing, drive, LFO, envelope) exactly as its Surge models are:
//   SAMPLER LP / HP / BP / NOTCH  Airwindows ZLowpass2 / ZHighpass2 / ZBandpass2 / ZNotch2 (MIT): Airwindows' take on the classic
//                                 hardware sampler's Z-plane-style filters, clipping stages included (the grit of a chopped break)
//   ACID LP                       Airwindows ZAcidLowpass (MIT): the same family's squelchy lowpass; RESONANCE drives its OVER
//   WERNER LP 24 / LP 12 / BP / HP chowdsp WernerFilter (GPL-3.0): Werner & McClellan's generalised SVF (DAFx 2020)
//   RESONATOR LP / BP / HP / NOTCH chowdsp ARPFilter (GPL-3.0): a model of a classic 1970s multimode filter / resonator, with its
//                                 input "limit" mode (on while SELF-OSCILLATION SAFE is on)
// Coefficients are set once per chunk (<= 32 samples, as the Surge models); the Airwindows filters glide inside each chunk themselves.
// Audio thread: no allocation (every model is built in prepare()).
#pragma once
#include <juce_dsp/juce_dsp.h>
#include <chowdsp_filters/chowdsp_filters.h>
#include "../aw/Airwindows.h"
#include <array>
#include <cmath>
#include <memory>

namespace phantom
{
class FilterModels
{
public:
    enum Model { zLP, zHP, zBP, zNotch, zAcid, wLP4, wLP2, wBP, wHP, rLP, rBP, rHP, rNotch, kNumModels };
    static const char* name (int m)
    {
        static const char* n[kNumModels] { "SAMPLER LP", "SAMPLER HP", "SAMPLER BP", "SAMPLER NOTCH", "ACID LP", "WERNER LP 24", "WERNER LP 12", "WERNER BP", "WERNER HP",
                                           "RESONATOR LP", "RESONATOR BP", "RESONATOR HP", "RESONATOR NOTCH" };
        return n[juce::jlimit (0, kNumModels - 1, m)];
    }
    // the level match: the passband at unity. The SAMPLER models' own output trim grows with the cutoff (by design), so theirs follows
    // the cutoff: their passband level measured at 300 Hz, 1 kHz and 4 kHz (GhostBreakTests filters calibrate, 2x rate), interpolated
    // over log frequency. WERNER LP / BP lose some passband at low cutoffs (resonance 0): the same correction. The rest are unity.
    static float makeup (int m, float cutHz)
    {
        if (m <= zNotch || (m >= wLP4 && m <= wBP))
        {
            static const float pb[7][3] { { 6.23f, 5.78f, 7.92f }, { 1.52f, 3.76f, 6.69f }, { 6.65f, 7.12f, 10.81f }, { 4.80f, 5.69f, 9.40f },
                                          { 0.0f, 0.0f, 0.0f }, { -5.97f, -3.07f, -0.98f }, { -3.81f, -1.81f, -0.53f } };
            static const float wbp[3] { -4.98f, -3.14f, -1.74f };
            static const float at[3] { 8.2288f, 9.9658f, 11.9658f };   // log2 of 300, 1000, 4000
            const float x = std::log2 (juce::jmax (20.0f, cutHz)); const float* q = m == wBP ? wbp : m <= zNotch ? pb[m] : pb[m - wLP4 + 5];
            const float dB = x <= at[0] ? q[0] : x >= at[2] ? q[2] : x < at[1] ? q[0] + (q[1] - q[0]) * (x - at[0]) / (at[1] - at[0]) : q[1] + (q[2] - q[1]) * (x - at[1]) / (at[2] - at[1]);
            return juce::Decibels::decibelsToGain (-dB);
        }
        return 1.0f;
    }

    void prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate; juce::ignoreUnused (maxBlock);
        z[0] = aw::makeZLowpass2(); z[1] = aw::makeZHighpass2(); z[2] = aw::makeZBandpass2(); z[3] = aw::makeZNotch2(); z[4] = aw::makeZAcidLowpass();
        for (auto& p : z) p->setSampleRate (sampleRate);
        const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) juce::jmax (64, maxBlock), 2 };
        werner.prepare (spec); arp.prepare (spec);
        model = -1; reset();
    }
    void reset() noexcept { werner.reset(); arp.reset(); zFresh = true; }
    void setModel (int m) noexcept
    {
        m = juce::jlimit (0, kNumModels - 1, m);
        if (m != model) { model = m; werner.reset(); arp.reset(); zFresh = true; lastGain = -1.0f; }
    }
    // one chunk (n <= 32), stereo in place. cutHz: the cutoff; res 0..1; steep: the 24 dB slope; limit: the resonator's limit mode
    void process (float* L, float* R, int n, float cutHz, float res, bool steep, bool limit) noexcept
    {
        if (model < 0 || n <= 0) return;
        const float fc = juce::jlimit (20.0f, (float) sr * 0.45f, cutHz); res = juce::jlimit (0.0f, 1.0f, res);
        if (model <= zNotch)
        {
            auto& p = *z[(size_t) model];
            // FREQ: each plugin's own law, f = B^e x scale Hz plus a floor x the sample rate (ZLowpass2: B^3 18930 + 0.00162,
            // ZHighpass2: B^4 9500 + 0.00076, ZBandpass2: B^4 14300 + 0.00079, ZNotch2: B^3 4700 + 0.0009963), inverted
            static const double law[4][3] { { 3.0, 18930.0, 0.00162 }, { 4.0, 9500.0, 0.00076 }, { 4.0, 14300.0, 0.00079 }, { 3.0, 4700.0, 0.0009963 } };
            const auto& q = law[model];
            const float B = (float) std::pow (juce::jmax (0.0, ((double) fc - q[2] * sr) / q[1]), 1.0 / q[0]);
            p.setParameter (0, 0.1f); p.setParameter (1, juce::jlimit (0.0f, 1.0f, B)); p.setParameter (2, 1.0f);
            p.setParameter (3, steep ? 0.7071f : 0.5f);   // POLES: one stage (12 dB) or two (24 dB)
            if (zFresh) { p.process (L, R, 0); zFresh = false; }
            p.process (L, R, n);
        }
        else if (model == zAcid)
        {
            auto& p = *z[4];
            // CUTOFF: the inverse of its own law (cutoff = A^3 / s / (2 / s^(0.5 - (s - 1) 0.0375)), s = rate / 44100), the cutoff being
            // the part of the rate its smoothing steps through (its corner sits near cutoff x rate / 2)
            const double s = sr / 44100.0, want = juce::jlimit (0.0001, 1.0, 2.0 * fc / sr);
            const double A = std::cbrt (want * s * (2.0 / std::pow (s, 0.5 - (s - 1.0) * 0.0375)));
            p.setParameter (0, (float) juce::jlimit (0.0, 1.0, A)); p.setParameter (1, res * 0.9f); p.setParameter (2, 1.0f);
            p.setParameter (3, 0.0f); p.setParameter (4, 0.1f); p.setParameter (5, 1.0f);
            p.process (L, R, n);
        }
        else if (model <= wHP)
        {
            werner.calcCoeffs (fc, 0.25f * (1.0f - res) + 0.05f, res);
            for (int i = 0; i < n; ++i)
            {
                switch (model)
                {
                    case wLP4: L[i] = werner.processSample<chowdsp::WernerFilterType::Lowpass4> (0, L[i]); R[i] = werner.processSample<chowdsp::WernerFilterType::Lowpass4> (1, R[i]); break;
                    case wLP2: L[i] = werner.processSample<chowdsp::WernerFilterType::Lowpass2> (0, L[i]); R[i] = werner.processSample<chowdsp::WernerFilterType::Lowpass2> (1, R[i]); break;
                    case wBP:  L[i] = werner.processSample<chowdsp::WernerFilterType::Bandpass2> (0, L[i]); R[i] = werner.processSample<chowdsp::WernerFilterType::Bandpass2> (1, R[i]); break;
                    default:   L[i] = werner.processSample<chowdsp::WernerFilterType::Highpass2> (0, L[i]); R[i] = werner.processSample<chowdsp::WernerFilterType::Highpass2> (1, R[i]); break;
                }
            }
        }
        else
        {
            arp.setCutoffFrequency (fc); arp.setQValue (0.5f * std::pow (36.0f, res)); arp.setLimitMode (limit && model == rBP);   // the limit mode holds the band-pass peak at unity; on the others it would lift the passband
            using T = chowdsp::ARPFilterType;
            for (int i = 0; i < n; ++i)
            {
                switch (model)
                {
                    case rLP: L[i] = arp.processSample<T::Lowpass> (L[i], 0, 0); R[i] = arp.processSample<T::Lowpass> (R[i], 0, 1); break;
                    case rBP: L[i] = arp.processSample<T::Bandpass> (L[i], 0, 0); R[i] = arp.processSample<T::Bandpass> (R[i], 0, 1); break;
                    case rHP: L[i] = arp.processSample<T::Highpass> (L[i], 0, 0); R[i] = arp.processSample<T::Highpass> (R[i], 0, 1); break;
                    default:  L[i] = arp.processSample<T::Notch> (L[i], 0, 0); R[i] = arp.processSample<T::Notch> (R[i], 0, 1); break;
                }
            }
            arp.snapToZero();
        }
        // the Airwindows filters keep their own anti-denormal noise and dither alive in silence (~1e-7 after their output trim): below
        // -120 dBFS their output is taken as silence, so digital silence in stays digital silence out (and the latency check sees no
        // output before the impulse)
        if (model <= zAcid) for (int i = 0; i < n; ++i) { if (std::abs (L[i]) < 1.0e-6f) L[i] = 0.0f; if (std::abs (R[i]) < 1.0e-6f) R[i] = 0.0f; }
        const float g = makeup (model, fc); if (lastGain < 0.0f) lastGain = g;
        if (g != 1.0f || lastGain != 1.0f) { const float step = (g - lastGain) / (float) n; for (int i = 0; i < n; ++i) { const float k = lastGain + step * (float) (i + 1); L[i] *= k; R[i] *= k; } }
        lastGain = g;
    }
private:
    double sr = 48000.0; int model = -1; bool zFresh = true; float lastGain = -1.0f;
    std::array<std::unique_ptr<aw::Plugin>, 5> z;
    chowdsp::WernerFilter werner; chowdsp::ARPFilter<float> arp;
};
} // namespace phantom
