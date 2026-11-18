// Ghost Matter - AGPL-3.0-only (see LICENSE, NOTICE.md).
#include "Dsp.h"
#include <cmath>

namespace matter::dsp
{
void removeDc (juce::AudioBuffer<float>& b)
{
    for (int c = 0; c < b.getNumChannels(); ++c)
    {
        auto* x = b.getWritePointer (c);
        float x1 = 0, y1 = 0;
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float y = x[i] - x1 + 0.9995f * y1;
            x1 = x[i]; y1 = y; x[i] = y;
        }
    }
}




void fadeEdges (juce::AudioBuffer<float>& b, int fadeIn, int fadeOut)
{
    const int n = b.getNumSamples();
    fadeIn = juce::jmin (fadeIn, n / 2); fadeOut = juce::jmin (fadeOut, n / 2);
    for (int c = 0; c < b.getNumChannels(); ++c)
    {
        auto* x = b.getWritePointer (c);
        for (int i = 0; i < fadeIn; ++i) x[i] *= 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * (float) i / (float) fadeIn);
        for (int i = 0; i < fadeOut; ++i) x[n - 1 - i] *= 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * (float) i / (float) fadeOut);
    }
}

juce::AudioBuffer<float> resampleTo (const juce::AudioBuffer<float>& in, double fromRate, double toRate)
{
    if (std::abs (fromRate - toRate) < 0.5 || in.getNumSamples() == 0) return in;
    const double ratio = fromRate / toRate;
    const int outN = (int) std::floor (in.getNumSamples() / ratio);
    juce::AudioBuffer<float> out (in.getNumChannels(), outN);
    for (int c = 0; c < in.getNumChannels(); ++c)
    {
        juce::WindowedSincInterpolator interp;
        // feed a little silence at the end so the interpolator flushes
        std::vector<float> src ((size_t) in.getNumSamples() + 256, 0.0f);
        std::copy (in.getReadPointer (c), in.getReadPointer (c) + in.getNumSamples(), src.begin());
        interp.process (ratio, src.data(), out.getWritePointer (c), outN, (int) src.size(), 0);
    }
    return out;
}

float peak (const juce::AudioBuffer<float>& b)
{
    float p = 0;
    for (int c = 0; c < b.getNumChannels(); ++c) p = juce::jmax (p, b.getMagnitude (c, 0, b.getNumSamples()));
    return p;
}

float truePeak (const juce::AudioBuffer<float>& b)
{
    // 8x oversampled with a 64-tap Hann-windowed sinc per phase (a reference meter: slow, accurate; for tests and checks)
    constexpr int P = 8, H = 32;
    static const std::vector<std::vector<float>> taps = []
    {
        std::vector<std::vector<float>> t ((size_t) P, std::vector<float> (2 * H + 1));
        for (int p = 0; p < P; ++p)
            for (int k = -H; k <= H; ++k)
            {
                const double d = k - (double) p / P, x = juce::MathConstants<double>::pi * d;
                const double sn = std::abs (x) < 1e-12 ? 1.0 : std::sin (x) / x;
                t[(size_t) p][(size_t) (k + H)] = (float) (sn * (0.5 + 0.5 * std::cos (juce::MathConstants<double>::pi * d / (H + 1))));
            }
        return t;
    }();
    float pk = peak (b);
    for (int c = 0; c < b.getNumChannels(); ++c)
    {
        const float* x = b.getReadPointer (c);
        const int n = b.getNumSamples();
        for (int i = 0; i < n; ++i)
        {
            if (std::abs (x[i]) < pk * 0.5f) continue;   // only loud places can hold the true peak
            for (int j = juce::jmax (0, i - 1); j <= juce::jmin (n - 1, i); ++j)
                for (int p = 1; p < P; ++p)
                {
                    float y = 0;
                    const auto& t = taps[(size_t) p];
                    for (int k = -H; k <= H; ++k) { const int q = j + k; if (q >= 0 && q < n) y += x[q] * t[(size_t) (k + H)]; }
                    pk = juce::jmax (pk, std::abs (y));
                }
        }
    }
    return pk;
}
}
