// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX shared DSP: interpolated delay lines (Signalsmith DSP, MIT) and a 3-band Linkwitz-Riley crossover (chowdsp_filters, GPL-3).
#pragma once
#include <juce_dsp/juce_dsp.h>
#include "signalsmith-dsp/delay.h"
#include <chowdsp_filters/chowdsp_filters.h>
#include <array>

namespace phantom
{
// Stereo fractional delay, cubic interpolation, fixed capacity set in prepare() (no allocation after that).
class StereoDelay
{
public:
    void prepare (double sampleRate, double maxSeconds)
    {
        sr = sampleRate;
        const int cap = (int) std::ceil (sampleRate * maxSeconds) + 8;
        for (auto& d : lines) d.resize (cap);
        capacity = cap;
    }
    void reset() noexcept { for (auto& d : lines) d.reset(); }
    inline float read (int ch, float delaySamples) const noexcept
    {
        return lines[(size_t) ch].read (juce::jlimit (1.0f, (float) (capacity - 2), delaySamples));
    }
    inline void write (int ch, float v) noexcept { lines[(size_t) ch].write (v); }
    int maxDelaySamples() const noexcept { return capacity - 2; }
private:
    std::array<signalsmith::delay::Delay<float, signalsmith::delay::InterpolatorCubic>, 2> lines;
    double sr = 48000.0; int capacity = 0;
};

// Whole-sample latency-matching delay for a dry path (set once per topology change; integer, no interpolation)
class IntegerDelay
{
public:
    void prepare (int maxSamples, int numChannels)
    {
        buf.setSize (numChannels, juce::jmax (1, maxSamples + 1)); buf.clear(); w = 0; len = 0;
    }
    void reset() noexcept { buf.clear(); w = 0; }
    // room for at least maxSamples (message thread, no block running: it re-allocates and clears when it must grow). A delay set past
    // the room would be cut short; FOLLOW GHOST BREAK's look-ahead can be seconds long, past the 1 << 16 most paths start with
    void ensure (int maxSamples) { if (buf.getNumSamples() < maxSamples + 1) { const int d = len; prepare (maxSamples, juce::jmax (1, buf.getNumChannels())); len = d; } }
    void setDelay (int samples) noexcept { len = juce::jlimit (0, buf.getNumSamples() - 1, samples); }
    int delay() const noexcept { return len; }
    void process (juce::dsp::AudioBlock<float> block) noexcept
    {
        if (len == 0) return;
        const int N = buf.getNumSamples(), n = (int) block.getNumSamples();
        for (int ch = 0; ch < (int) block.getNumChannels() && ch < buf.getNumChannels(); ++ch)
        {
            auto* d = buf.getWritePointer (ch); auto* x = block.getChannelPointer ((size_t) ch);
            int wi = w;
            for (int i = 0; i < n; ++i)
            {
                int ri = wi - len; if (ri < 0) ri += N;
                const float out = d[ri]; d[wi] = x[i]; x[i] = out;
                if (++wi == N) wi = 0;
            }
        }
        w = (w + n) % N;
    }
private:
    juce::AudioBuffer<float> buf; int w = 0, len = 0;
};

// 3-band crossover: two 4th-order Linkwitz-Riley splits (phase-coherent recombination). Bands sum back to flat.
class Crossover3
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        lo.prepare (spec); hi.prepare (spec); comp.prepare (spec);
        const int ch = (int) spec.numChannels, n = (int) spec.maximumBlockSize;
        lowBuf.setSize (ch, n); midBuf.setSize (ch, n); highBuf.setSize (ch, n); tmp.setSize (ch, n);
        reset();
    }
    void reset() noexcept { lo.reset(); hi.reset(); comp.reset(); }
    void setFrequencies (float fLow, float fHigh) noexcept { lo.setCrossoverFrequency (fLow); hi.setCrossoverFrequency (juce::jmax (fLow * 1.01f, fHigh)); comp.setCrossoverFrequency (juce::jmax (fLow * 1.01f, fHigh)); }

    // splits `in` into low / mid / high (each the same size as in)
    void split (const juce::dsp::AudioBlock<const float>& in) noexcept
    {
        const int ch = (int) in.getNumChannels(), n = (int) in.getNumSamples();
        for (int c = 0; c < ch; ++c)
        {
            auto* x = in.getChannelPointer ((size_t) c);
            auto* L = lowBuf.getWritePointer (c); auto* M = midBuf.getWritePointer (c); auto* H = highBuf.getWritePointer (c);
            for (int i = 0; i < n; ++i)
            {
                auto a = lo.processSample (c, x[i]);        // low | rest
                auto b = hi.processSample (c, a.second);    // mid | high
                auto lc = comp.processSample (c, a.first);  // the low band through the high crossover's all-pass (its two halves summed),
                L[i] = lc.first + lc.second; M[i] = b.first; H[i] = b.second;   // so low + mid + high sums to one flat all-pass
            }
        }
    }
    juce::AudioBuffer<float> lowBuf, midBuf, highBuf;
private:
    chowdsp::LinkwitzRileyFilter<float, 4> lo, hi, comp;
    juce::AudioBuffer<float> tmp;
};
} // namespace phantom
