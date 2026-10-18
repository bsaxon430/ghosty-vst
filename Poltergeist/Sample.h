#pragma once
// Sample Ghost data: one mono sample (any length up to 60 s) plus a wavetable made from it.
// Built on the message thread when a file is dropped/loaded, then handed to the audio thread as a
// shared_ptr (the audio thread never allocates or frees it).
#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>
#include <vector>

namespace gs {

struct SampleData {
    std::vector<float> data;          // mono, normalised to peak 0.9
    double rate = 44100;
    juce::String name;
    // wavetable: kFrames single-cycle frames of kFrameLen samples cut evenly from the sample
    static constexpr int kFrames = 64, kFrameLen = 2048;
    std::vector<float> frames;

    int size() const { return (int) data.size(); }
    inline float at(double pos) const {   // linear interpolation, clamped
        const int n = size(); if (n < 2) return 0.0f;
        if (pos <= 0) return data[0];
        if (pos >= n - 1) return data[(size_t) n - 1];
        const int i = (int) pos; const float f = (float) (pos - i);
        return data[(size_t) i] + (data[(size_t) i + 1] - data[(size_t) i]) * f;
    }
    inline float frame(int f, float phase) const {   // phase 0..1 inside frame f
        const float x = phase * kFrameLen; const int i = (int) x; const float t = x - (float) i;
        const float* F = frames.data() + (size_t) f * kFrameLen;
        return F[i & (kFrameLen - 1)] + (F[(i + 1) & (kFrameLen - 1)] - F[i & (kFrameLen - 1)]) * t;
    }

    void buildFrames() {
        frames.assign((size_t) kFrames * kFrameLen, 0.0f);
        const int n = size(); if (n < 64) return;
        for (int f = 0; f < kFrames; ++f) {
            // each frame: kFrameLen samples taken from an evenly spaced window, made loopable
            const double start = (double) f / kFrames * juce::jmax(1, n - kFrameLen);
            const double step = juce::jmin(1.0, (double) n / kFrameLen);
            float* F = frames.data() + (size_t) f * kFrameLen;
            double mean = 0;
            for (int i = 0; i < kFrameLen; ++i) { F[i] = at(start + i * step); mean += F[i]; }
            mean /= kFrameLen;
            float peak = 1e-6f;
            for (int i = 0; i < kFrameLen; ++i) { F[i] -= (float) mean; peak = juce::jmax(peak, std::abs(F[i])); }
            // crossfade the ends so the cycle loops without a click, then normalise
            const int x = kFrameLen / 16;
            for (int i = 0; i < x; ++i) { const float w = (float) i / x; F[i] = F[i] * w + F[kFrameLen - x + i] * (1 - w); }
            for (int i = 0; i < kFrameLen; ++i) F[i] /= peak;
        }
    }

    static std::shared_ptr<SampleData> fromBuffer(const juce::AudioBuffer<float>& b, double rate, const juce::String& name) {
        auto s = std::make_shared<SampleData>();
        const int n = juce::jmin(b.getNumSamples(), (int) (rate * 60.0));
        s->data.assign((size_t) n, 0.0f);
        for (int c = 0; c < b.getNumChannels(); ++c) {
            const float* x = b.getReadPointer(c);
            for (int i = 0; i < n; ++i) s->data[(size_t) i] += x[i] / (float) b.getNumChannels();
        }
        // trim silence at both ends
        int a = 0, z = n - 1;
        while (a < z && std::abs(s->data[(size_t) a]) < 0.0015f) ++a;
        while (z > a && std::abs(s->data[(size_t) z]) < 0.0015f) --z;
        if (z - a > 32) s->data = std::vector<float>(s->data.begin() + a, s->data.begin() + z + 1);
        float peak = 1e-6f; for (float v : s->data) peak = juce::jmax(peak, std::abs(v));
        for (float& v : s->data) v *= 0.9f / peak;
        s->rate = rate; s->name = name;
        s->buildFrames();
        return s;
    }

    static std::shared_ptr<SampleData> fromFile(const juce::File& f, juce::String& err) {
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(f));
        if (!r) { err = "That file isn't an audio file Poltergeist can read (try WAV, AIFF, MP3 or FLAC)."; return nullptr; }
        const int n = (int) juce::jmin<juce::int64>(r->lengthInSamples, (juce::int64) (r->sampleRate * 60.0));
        juce::AudioBuffer<float> b((int) juce::jmin(2u, r->numChannels), n);
        r->read(&b, 0, n, 0, true, true);
        return fromBuffer(b, r->sampleRate, f.getFileNameWithoutExtension());
    }

    // compact form for the plugin state: 16-bit mono, max 30 s
    juce::String toBase64() const {
        const int n = juce::jmin(size(), (int) (rate * 30.0));
        juce::MemoryBlock mb((size_t) n * 2);
        auto* p = static_cast<int16_t*>(mb.getData());
        for (int i = 0; i < n; ++i) p[i] = (int16_t) juce::jlimit(-32767.0f, 32767.0f, data[(size_t) i] * 32767.0f);
        return mb.toBase64Encoding();
    }
    static std::shared_ptr<SampleData> fromBase64(const juce::String& b64, double rate, const juce::String& name) {
        juce::MemoryBlock mb; if (!mb.fromBase64Encoding(b64) || mb.getSize() < 4) return nullptr;
        auto s = std::make_shared<SampleData>();
        const int n = (int) (mb.getSize() / 2); auto* p = static_cast<const int16_t*>(mb.getData());
        s->data.resize((size_t) n); for (int i = 0; i < n; ++i) s->data[(size_t) i] = p[i] / 32767.0f;
        s->rate = rate; s->name = name; s->buildFrames();
        return s;
    }
};

} // namespace gs
