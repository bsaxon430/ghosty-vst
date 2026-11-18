#include "AudioTools.h"
#include <juce_dsp/juce_dsp.h>
#include <cmath>

namespace glab
{
juce::AudioFormatManager& formats()
{
    static std::unique_ptr<juce::AudioFormatManager> m = [] { auto f = std::make_unique<juce::AudioFormatManager>(); f->registerBasicFormats(); return f; }();
    return *m;
}

bool canLoad (const juce::File& f)
{
    return f.existsAsFile() && formats().findFormatForFileExtension (f.getFileExtension()) != nullptr;
}

std::shared_ptr<Audio> loadAudio (const juce::File& f, juce::String& error, bool* trimmed)
{
    std::unique_ptr<juce::AudioFormatReader> r (formats().createReaderFor (f));
    if (r == nullptr) { error = "Ghost Lab can't open that file. Try a WAV, AIFF, FLAC or MP3."; return {}; }
    if (r->sampleRate < 8000 || r->sampleRate > 384000 || r->lengthInSamples <= 0) { error = "That file has no sound Ghost Lab can use."; return {}; }
    auto maxLen = (juce::int64) (kMaxInputSeconds * r->sampleRate);
    const auto len = (int) juce::jmin (r->lengthInSamples, maxLen);
    if (trimmed != nullptr) *trimmed = r->lengthInSamples > maxLen;
    if (len < (int) (0.05 * r->sampleRate)) { error = "That sound is too short (Ghost Lab needs at least a twentieth of a second)."; return {}; }
    const int inCh = (int) juce::jmax (1u, r->numChannels);
    juce::AudioBuffer<float> tmp (inCh, len);
    r->read (&tmp, 0, len, 0, true, true);
    auto a = std::make_shared<Audio>();
    a->sampleRate = r->sampleRate;
    const int outCh = juce::jmin (2, inCh);
    a->buf.setSize (outCh, len);
    a->buf.clear();
    for (int c = 0; c < inCh; ++c)
        a->buf.addFrom (c % outCh, 0, tmp, c, 0, len, inCh > 2 ? 2.0f / (float) inCh : 1.0f);
    sanitize (*a);
    if (isEffectivelyMono (*a) && outCh == 2) { auto m = mono (*a); a->buf = m.buf; }
    return a;
}

bool writeWav (const Audio& a, const juce::File& f, int bits, juce::String& error)
{
    f.deleteFile();
    f.getParentDirectory().createDirectory();
    std::unique_ptr<juce::FileOutputStream> os (f.createOutputStream());
    if (os == nullptr) { error = "Couldn't write " + f.getFileName(); return false; }
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), a.sampleRate, (unsigned) a.channels(), bits, {}, 0));
    if (w == nullptr) { error = "Couldn't write " + f.getFileName(); return false; }
    os.release();
    if (! w->writeFromAudioSampleBuffer (a.buf, 0, a.length())) { error = "Couldn't write " + f.getFileName(); return false; }
    return true;
}

std::shared_ptr<Audio> readWav (const juce::File& f, juce::String& error)
{
    std::unique_ptr<juce::AudioFormatReader> r (formats().createReaderFor (f));
    if (r == nullptr || r->lengthInSamples <= 0) { error = "The sound engine gave back no sound."; return {}; }
    const auto maxLen = (juce::int64) (kMaxOutputSeconds * r->sampleRate);
    const int len = (int) juce::jmin (r->lengthInSamples, maxLen);
    auto a = std::make_shared<Audio>();
    a->sampleRate = r->sampleRate;
    a->buf.setSize ((int) juce::jlimit (1u, 2u, r->numChannels), len);
    r->read (&a->buf, 0, len, 0, true, true);
    sanitize (*a);
    return a;
}

Audio channel (const Audio& a, int ch)
{
    Audio o; o.sampleRate = a.sampleRate;
    o.buf.setSize (1, a.length());
    o.buf.copyFrom (0, 0, a.buf, juce::jlimit (0, a.channels() - 1, ch), 0, a.length());
    return o;
}

Audio mono (const Audio& a)
{
    Audio o; o.sampleRate = a.sampleRate;
    o.buf.setSize (1, a.length());
    o.buf.clear();
    for (int c = 0; c < a.channels(); ++c) o.buf.addFrom (0, 0, a.buf, c, 0, a.length(), 1.0f / (float) a.channels());
    return o;
}

bool isEffectivelyMono (const Audio& a)
{
    if (a.channels() < 2) return true;
    double diff = 0, sum = 0;
    auto* l = a.buf.getReadPointer (0); auto* r = a.buf.getReadPointer (1);
    for (int i = 0; i < a.length(); ++i) { diff += std::abs (l[i] - r[i]); sum += std::abs (l[i]) + std::abs (r[i]); }
    return diff <= 1e-4 * (sum + 1e-9);
}

Audio resampled (const Audio& a, double newRate)
{
    if (std::abs (a.sampleRate - newRate) < 0.5) return a;
    const double ratio = a.sampleRate / newRate;
    const int outLen = (int) std::floor (a.length() / ratio);
    Audio o; o.sampleRate = newRate;
    o.buf.setSize (a.channels(), juce::jmax (1, outLen));
    o.buf.clear();
    for (int c = 0; c < a.channels(); ++c)
    {
        juce::WindowedSincInterpolator interp;
        // pad the input so the interpolator never reads past the end
        std::vector<float> in ((size_t) a.length() + 256, 0.0f);
        std::copy (a.buf.getReadPointer (c), a.buf.getReadPointer (c) + a.length(), in.begin());
        interp.process (ratio, in.data(), o.buf.getWritePointer (c), outLen);
    }
    return o;
}

float rms (const Audio& a)
{
    double s = 0; juce::int64 n = 0;
    for (int c = 0; c < a.channels(); ++c)
    {
        auto* p = a.buf.getReadPointer (c);
        for (int i = 0; i < a.length(); ++i) s += (double) p[i] * p[i];
        n += a.length();
    }
    return n > 0 ? (float) std::sqrt (s / (double) n) : 0.0f;
}

float peak (const Audio& a)
{
    float m = 0;
    for (int c = 0; c < a.channels(); ++c) m = juce::jmax (m, a.buf.getMagnitude (c, 0, a.length()));
    return m;
}

// the modified Bessel function I0 (Kaiser window); Apple's libc++ has no std::cyl_bessel_i
static double besselI0 (double x)
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 40; ++k) { term *= (x / (2.0 * k)) * (x / (2.0 * k)); sum += term; if (term < 1e-12 * sum) break; }
    return sum;
}

// 4x oversampling with a 48-tap Kaiser-windowed sinc per phase: the inter-sample peak estimate BS.1770-4 asks for
float truePeakDb (const Audio& a)
{
    constexpr int kOver = 4, kHalf = 12;
    static const auto taps = [] {
        std::vector<std::array<float, 2 * kHalf>> t (kOver);
        const double beta = 7.0, i0b = besselI0 (beta);
        for (int p = 0; p < kOver; ++p)
            for (int k = 0; k < 2 * kHalf; ++k)
            {
                const double x = (k - kHalf + 1) - (double) p / kOver;   // distance to the input sample
                const double sinc = std::abs (x) < 1e-9 ? 1.0 : std::sin (juce::MathConstants<double>::pi * x) / (juce::MathConstants<double>::pi * x);
                const double w = besselI0 (beta * std::sqrt (juce::jmax (0.0, 1.0 - (x / (kHalf + 0.5)) * (x / (kHalf + 0.5))))) / i0b;
                t[(size_t) p][(size_t) k] = (float) (sinc * w);
            }
        return t;
    }();
    float m = 0;
    for (int c = 0; c < a.channels(); ++c)
    {
        auto* s = a.buf.getReadPointer (c);
        const int n = a.length();
        const float samplePeak = a.buf.getMagnitude (c, 0, n);
        m = juce::jmax (m, samplePeak);
        // an inter-sample peak can only beat the file's peak between samples that are already close to it
        const float near = samplePeak * 0.35f;
        for (int i = 0; i < n; ++i)
        {
            if (std::abs (s[i]) < near && (i + 1 >= n || std::abs (s[i + 1]) < near)) continue;
            for (int p = 1; p < kOver; ++p)
            {
                float acc = 0;
                for (int k = 0; k < 2 * kHalf; ++k)
                {
                    const int j = i + k - kHalf + 1;
                    if (j >= 0 && j < n) acc += s[j] * taps[(size_t) p][(size_t) k];
                }
                m = juce::jmax (m, std::abs (acc));
            }
        }
    }
    return juce::Decibels::gainToDecibels (m, -200.0f);
}

void sanitize (Audio& a)
{
    for (int c = 0; c < a.channels(); ++c)
    {
        auto* p = a.buf.getWritePointer (c);
        for (int i = 0; i < a.length(); ++i) if (! std::isfinite (p[i])) p[i] = 0.0f;
    }
}

void finish (Audio& a, float referenceRms)
{
    sanitize (a);
    const int n = a.length();
    if (n == 0) return;
    // DC: a 10 Hz one-pole high-pass, run forwards then backwards (no phase shift, no step at the start)
    const float r = 1.0f - (float) (juce::MathConstants<double>::twoPi * 10.0 / a.sampleRate);
    for (int c = 0; c < a.channels(); ++c)
    {
        auto* p = a.buf.getWritePointer (c);
        for (int pass = 0; pass < 2; ++pass)
        {
            float x1 = pass == 0 ? p[0] : p[n - 1], y1 = 0;
            for (int k = 0; k < n; ++k)
            {
                const int i = pass == 0 ? k : n - 1 - k;
                const float x = p[i], y = x - x1 + r * y1;
                x1 = x; y1 = y; p[i] = y;
            }
        }
    }
    // edges: raised-cosine fades so a result never starts or stops with a click
    const int fade = juce::jmin (n / 4, (int) (0.006 * a.sampleRate));
    for (int c = 0; c < a.channels(); ++c)
    {
        auto* p = a.buf.getWritePointer (c);
        for (int i = 0; i < fade; ++i)
        {
            const float g = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * (float) i / (float) fade);
            p[i] *= g; p[n - 1 - i] *= g;
        }
        p[0] = 0.0f; p[n - 1] = 0.0f;
    }
    // loudness close to the source (fair A/B), then the true-peak ceiling
    const float now = rms (a);
    if (now > 1e-7f && referenceRms > 1e-7f)
        a.buf.applyGain (juce::jlimit (0.0f, 4.0f, referenceRms / now));
    for (int tries = 0; tries < 4; ++tries)
    {
        const float tp = truePeakDb (a);
        if (tp <= kTruePeakTargetDb) break;
        a.buf.applyGain (juce::Decibels::decibelsToGain (kTruePeakTargetDb - tp - 0.02f));
    }
}

float edgeJump (const Audio& a, int n)
{
    float m = 0;
    for (int c = 0; c < a.channels(); ++c)
    {
        auto* p = a.buf.getReadPointer (c);
        const int len = a.length();
        m = juce::jmax (m, std::abs (p[0]), std::abs (p[len - 1]));
        for (int i = 1; i < juce::jmin (n, len); ++i)
        {
            m = juce::jmax (m, std::abs (p[i] - p[i - 1]));
            m = juce::jmax (m, std::abs (p[len - 1 - i] - p[len - i]));
        }
    }
    return m;
}

Profile makeProfile (const Audio& a)
{
    Profile pr;
    constexpr int C = Profile::kColumns, B = Profile::kBands, W = Profile::kWaveBins;
    pr.energy.assign (C, 0); pr.centroid.assign (C, 0); pr.flux.assign (C, 0); pr.bands.assign ((size_t) C * B, 0);
    pr.waveMin.assign (W, 0); pr.waveMax.assign (W, 0);
    const int n = a.length();
    if (n == 0) return pr;
    auto m = mono (a);
    auto* s = m.buf.getReadPointer (0);
    for (int b = 0; b < W; ++b)
    {
        const int i0 = (int) ((juce::int64) b * n / W), i1 = juce::jmax (i0 + 1, (int) ((juce::int64) (b + 1) * n / W));
        float lo = 0, hi = 0;
        for (int i = i0; i < i1 && i < n; ++i) { lo = juce::jmin (lo, s[i]); hi = juce::jmax (hi, s[i]); }
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
            work[(size_t) i] = (j >= 0 && j < n) ? s[j] * w : 0.0f;
        }
        fft.performFrequencyOnlyForwardTransform (work.data());
        double e = 0, num = 0, fl = 0;
        for (int k = 1; k < N / 2; ++k)
        {
            const float mag = work[(size_t) k];
            e += mag * mag; num += (double) mag * k;
            fl += juce::jmax (0.0f, mag - prev[(size_t) k]);
            prev[(size_t) k] = mag;
            // bands on a log scale from ~40 Hz to Nyquist
            const double frac = std::log ((double) k) / std::log ((double) N / 2);
            const int band = juce::jlimit (0, B - 1, (int) (frac * B));
            pr.bands[(size_t) (col * B + band)] += mag;
        }
        pr.energy[(size_t) col] = (float) std::sqrt (e);
        double magSum = 0; for (int k = 1; k < N / 2; ++k) magSum += work[(size_t) k];
        pr.centroid[(size_t) col] = magSum > 1e-9 ? (float) juce::jlimit (0.0, 1.0, (num / magSum) / (N / 2) * 4.0) : 0.0f;
        pr.flux[(size_t) col] = (float) fl;
        maxE = juce::jmax (maxE, pr.energy[(size_t) col]); maxF = juce::jmax (maxF, pr.flux[(size_t) col]);
    }
    float maxB = 1e-9f;
    for (auto v : pr.bands) maxB = juce::jmax (maxB, v);
    for (auto& v : pr.bands) v = std::sqrt (v / maxB);
    for (int col = 0; col < C; ++col)
    {
        pr.energy[(size_t) col] = std::sqrt (pr.energy[(size_t) col] / maxE);
        pr.flux[(size_t) col] /= maxF;
    }
    return pr;
}
}
