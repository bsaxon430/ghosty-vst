// Ghost Matter - AGPL-3.0-only (see LICENSE, NOTICE.md).
#include "Renderer.h"
#include "MakeStage.h"
#include "StretchStage.h"
#include "Morph.h"
#include "Dsp.h"
#include <juce_dsp/juce_dsp.h>

namespace matter
{
Profile makeProfile (const juce::AudioBuffer<float>& a)
{
    // after Ghost Lab's makeProfile (GhostBlock's own code)
    Profile pr;
    constexpr int C = Profile::kColumns, B = Profile::kBands, W = Profile::kWaveBins;
    pr.energy.assign (C, 0); pr.centroid.assign (C, 0); pr.flux.assign (C, 0); pr.bands.assign ((size_t) C * B, 0);
    pr.waveMin.assign (W, 0); pr.waveMax.assign (W, 0);
    const int n = a.getNumSamples();
    if (n == 0 || a.getNumChannels() == 0) return pr;
    std::vector<float> s ((size_t) n);
    for (int i = 0; i < n; ++i) { float v = 0; for (int c = 0; c < a.getNumChannels(); ++c) v += a.getSample (c, i); s[(size_t) i] = v / (float) a.getNumChannels(); }
    for (int b = 0; b < W; ++b)
    {
        const int i0 = (int) ((juce::int64) b * n / W), i1 = juce::jmax (i0 + 1, (int) ((juce::int64) (b + 1) * n / W));
        float lo = 0, hi = 0;
        for (int i = i0; i < i1 && i < n; ++i) { lo = juce::jmin (lo, s[(size_t) i]); hi = juce::jmax (hi, s[(size_t) i]); }
        pr.waveMin[(size_t) b] = lo; pr.waveMax[(size_t) b] = hi;
    }
    constexpr int order = 10, N = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> work (2 * N), prev (N / 2, 0.0f);
    float maxE = 1e-9f, maxF = 1e-9f;
    for (int col = 0; col < C; ++col)
    {
        const int centre = (int) ((juce::int64) (2 * col + 1) * n / (2 * C));
        std::fill (work.begin(), work.end(), 0.0f);
        for (int i = 0; i < N; ++i)
        {
            const int j = centre - N / 2 + i;
            const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N);
            work[(size_t) i] = (j >= 0 && j < n) ? s[(size_t) j] * w : 0.0f;
        }
        fft.performFrequencyOnlyForwardTransform (work.data());
        double e = 0, num = 0, fl = 0, magSum = 0;
        for (int k = 1; k < N / 2; ++k)
        {
            const float mag = work[(size_t) k];
            e += mag * mag; num += (double) mag * k; magSum += mag;
            fl += juce::jmax (0.0f, mag - prev[(size_t) k]);
            prev[(size_t) k] = mag;
            const double frac = std::log ((double) k) / std::log ((double) N / 2);
            const int band = juce::jlimit (0, B - 1, (int) (frac * B));
            pr.bands[(size_t) (col * B + band)] += mag;
        }
        pr.energy[(size_t) col] = (float) std::sqrt (e);
        pr.centroid[(size_t) col] = magSum > 1e-9 ? (float) juce::jlimit (0.0, 1.0, (num / magSum) / (N / 2) * 4.0) : 0.0f;
        pr.flux[(size_t) col] = (float) fl;
        maxE = juce::jmax (maxE, pr.energy[(size_t) col]); maxF = juce::jmax (maxF, pr.flux[(size_t) col]);
    }
    float maxB = 1e-9f;
    for (auto v : pr.bands) maxB = juce::jmax (maxB, v);
    for (auto& v : pr.bands) v = std::sqrt (v / maxB);
    for (int col = 0; col < C; ++col) { pr.energy[(size_t) col] = std::sqrt (pr.energy[(size_t) col] / maxE); pr.flux[(size_t) col] /= maxF; }
    return pr;
}

int makeLoopable (juce::AudioBuffer<float>& b, int fade)
{
    // the loop is cross-faded as it plays (Live.cpp: the last `fade` samples fade into the start); here only its length
    return juce::jmin (fade, b.getNumSamples() / 3) < 16 ? 0 : juce::jmin (fade, b.getNumSamples() / 3);
}

Renderer::Renderer() : juce::Thread ("Ghost Matter render") { startThread (juce::Thread::Priority::low); }

Renderer::~Renderer()
{
    cancel = true;
    signalThreadShouldExit();
    notify();
    stopThread (20000);
}

void Renderer::request (const RenderSettings& s)
{
    {
        const juce::SpinLock::ScopedLockType l (lock);
        pending = s;
        pendingFlag = true;
    }
    cancel = true;
    notify();
}

std::shared_ptr<const Matter> Renderer::latest() const
{
    const juce::SpinLock::ScopedLockType l (lock);
    return result;
}

void Renderer::run()
{
    while (! threadShouldExit())
    {
        if (! pendingFlag.load()) { wait (200); continue; }
        RenderSettings s;
        {
            const juce::SpinLock::ScopedLockType l (lock);
            s = pending;
            pendingFlag = false;
        }
        cancel = false;
        working = true;
        auto m = renderCached (s);
        working = false;
        if (m != nullptr && ! cancel.load())
        {
            m->serial = doneSerial.load() + 1;
            {
                const juce::SpinLock::ScopedLockType l (lock);
                result = m;
            }
            doneSerial = m->serial;
        }
    }
}

static void finish (Matter& m, juce::AudioBuffer<float>&& a)
{
    m.audio = std::move (a);   // as the engines made it (build 1 put a tanh ceiling here: WHY.md)
    if (m.audio.getNumSamples() == 0) { m.audio.setSize (2, 64); m.audio.clear(); }
    m.profile = makeProfile (m.audio);
    m.loopFrom = makeLoopable (m.audio, juce::jmin ((int) (0.25 * kRate), m.audio.getNumSamples() / 4));
}

std::shared_ptr<Matter> Renderer::renderCached (const RenderSettings& s)
{
    auto& c = cache;
    stage = 0;
    const bool makeOk = c.valid && c.make == s.make;
    if (! makeOk)
    {
        c.made = makeSound (s.make);
        c.madeProfile = makeProfile (c.made);
        c.make = s.make;
        c.valid = true;
    }
    if (cancel.load()) { c.valid = c.valid && makeOk; return nullptr; }
    stage = 1;
    const bool stretchOk = makeOk && c.stretch == s.stretch && c.stretched.getNumSamples() > 0;
    if (! stretchOk)
    {
        c.stretched = stretchSound (c.made, s.stretch, &cancel);
        c.stretch = s.stretch;
    }
    if (cancel.load()) { c.stretched.setSize (0, 0); return nullptr; }
    stage = 2;
    auto m = std::make_shared<Matter>();
    m->settings = s;
    m->made = c.madeProfile;
    finish (*m, morphSound (c.stretched, s.morph, &m->note, &cancel, &neural));
    stage = 3;
    return m;
}

std::shared_ptr<Matter> Renderer::renderNow (const RenderSettings& s)
{
    auto m = std::make_shared<Matter>();
    m->settings = s;
    auto made = makeSound (s.make);
    m->made = makeProfile (made);
    auto st = stretchSound (made, s.stretch);
    finish (*m, morphSound (st, s.morph, &m->note, nullptr));
    return m;
}
}
