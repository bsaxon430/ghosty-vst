// Ghost Matter - AGPL-3.0-only (see LICENSE, NOTICE.md).
#include "MakeStage.h"
#include "Dsp.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

#include "FluesLv2.h"

namespace matter
{
namespace
{
// our controls -> a plug-in port: at the control's default the port sits exactly at the plug-in's own .ttl default
float around (float control, float controlDefault, float portDefault, float span, float lo = 0.0f, float hi = 1.0f)
{
    return juce::jlimit (lo, hi, portDefault + (control - controlDefault) * span);
}

// Flues' own vowel presets (FormantBankModule::setVowel, Hz) as Chatterbox plug-in ports: its plug-in maps a port p to
// lo * (hi / lo)^p for F1 200..1000, F2 500..3000, F3 1500..4000, F4 2500..4500 Hz, so p = log (f / lo) / log (hi / lo)
float formantPort (float hz, float lo, float hi) { return juce::jlimit (0.0f, 1.0f, std::log (hz / lo) / std::log (hi / lo)); }

void setVowel (std::map<std::string, float>& p, char v)
{
    // the values in Flues' FormantBankModule::setVowel ('a' 'e' 'i' 'o' 'u')
    float f[4] {};
    switch (v)
    {
        case 'a': { float t[4] = { 730, 1090, 2440, 3200 }; std::copy (t, t + 4, f); break; }
        case 'e': { float t[4] = { 530, 1840, 2480, 3500 }; std::copy (t, t + 4, f); break; }
        case 'i': { float t[4] = { 270, 2290, 3010, 3500 }; std::copy (t, t + 4, f); break; }
        case 'o': { float t[4] = { 570, 840, 2410, 3200 }; std::copy (t, t + 4, f); break; }
        case 'u': { float t[4] = { 300, 870, 2240, 3200 }; std::copy (t, t + 4, f); break; }
        default: return;   // NEUTRAL: the plug-in's own default formants
    }
    p["f1"] = formantPort (f[0], 200, 1000); p["f2"] = formantPort (f[1], 500, 3000);
    p["f3"] = formantPort (f[2], 1500, 4000); p["f4"] = formantPort (f[3], 2500, 4500);
}

// NOISE: our pick of Disyn's 19 algorithms, in bodyNames(noise) order
const int kNoiseAlgo[] = { 0, 2, 1, 3, 4, 5, 6, 9, 10, 12, 16, 18 };
}

std::map<std::string, float> fluesPorts (const MakeSettings& s, fluesplug::Plugin plug, bool withPins)
{
    // the controls' defaults (MakeSettings): loud 0.5, bright 0.5, rough 0.2, shape 0.5 -> every port at its .ttl default
    auto p = fluesplug::defaults (plug);
    if (plug == fluesplug::Plugin::pmSynth && s.source == Source::physical)
    {
        p["interfaceType"] = (float) juce::jlimit (0, 11, s.body);
        p["interfaceIntensity"] = around (s.shape, 0.5f, 0.5f, 1.0f);
        p["filterFrequency"] = around (s.bright, 0.5f, 0.5663233348f, 0.8f);
        p["noiseLevel"] = around (s.rough, 0.2f, 0.15f, 0.6f);
        p["dcLevel"] = juce::jlimit (0.0f, 1.0f, s.loud);             // LOUD = its own DC (breath) level; 0.5 = .ttl default
    }
    else if (plug == fluesplug::Plugin::chatterbox && s.source == Source::voice)
    {
        static const char vowels[] = { 'a', 'i', 'u', 'o', 'e', 0, 0, 0 };
        const int b = juce::jlimit (0, 7, s.body);
        setVowel (p, vowels[b]);
        if (b == 6) p["nasal"] = 1.0f;                                  // HUM
        if (b == 7) { p["voiced"] = 0.0f; p["aspirated"] = 1.0f; }     // WHISPER
        p["stress"] = around (s.shape, 0.5f, 0.3f, 1.0f);
        p["noise_level"] = around (s.rough, 0.2f, 0.2f, 0.8f);
        p["fry"] = s.rough > 0.75f ? 1.0f : 0.0f;
        p["shout"] = s.bright > 0.8f ? 1.0f : 0.0f;
        p["master_gain"] = juce::jlimit (0.0f, 1.0f, 1.6f * s.loud);   // LOUD 0.5 = its .ttl default 0.8
    }
    else if (plug == fluesplug::Plugin::disyn && s.source == Source::noise)
    {
        p["algorithmType"] = (float) kNoiseAlgo[juce::jlimit (0, 11, s.body)];
        p["param1"] = around (s.shape, 0.5f, 0.55f, 1.0f);
        p["param2"] = around (s.rough, 0.2f, 0.5f, 1.0f);
        p["param3"] = around (s.bright, 0.5f, 0.0f, 2.0f);
        p["masterGain"] = juce::jlimit (0.0f, 1.0f, 1.6f * s.loud);    // LOUD 0.5 = its .ttl default 0.8
    }
    if (withPins)
    {
        // UNDER THE HOOD: the ports you moved, exactly as you set them (the engine's own units)
        const char* pre = plug == fluesplug::Plugin::pmSynth ? "pm." : plug == fluesplug::Plugin::chatterbox ? "cb." : "ds.";
        for (auto& kv : s.pins)
            if (kv.first.rfind (pre, 0) == 0)
            {
                const auto port = kv.first.substr (3);
                if (p.count (port)) p[port] = kv.second;
            }
    }
    return p;
}

juce::AudioBuffer<float> makeSoundRaw (const MakeSettings& s)
{
    const double secs = juce::jlimit (0.5f, (float) kMaxSourceSeconds, s.seconds);
    fluesplug::Note note { juce::jlimit (0, 127, s.note), 110, 0.75 };
    if (s.source == Source::physical) return fluesplug::render (fluesplug::Plugin::pmSynth, fluesPorts (s, fluesplug::Plugin::pmSynth), note, secs, kRate);
    if (s.source == Source::voice) return fluesplug::render (fluesplug::Plugin::chatterbox, fluesPorts (s, fluesplug::Plugin::chatterbox), note, secs, kRate);
    if (s.source == Source::noise) return fluesplug::render (fluesplug::Plugin::disyn, fluesPorts (s, fluesplug::Plugin::disyn), note, secs, kRate);
    return {};
}

float targetCentroidHz (float bright) { return 350.0f * std::pow (2.0f, juce::jlimit (0.0f, 1.0f, bright) * 4.6f); }

float rmsDb (const juce::AudioBuffer<float>& b)
{
    double s = 0; juce::int64 n = 0;
    for (int c = 0; c < b.getNumChannels(); ++c)
    {
        auto* x = b.getReadPointer (c);
        for (int i = 0; i < b.getNumSamples(); ++i) { s += (double) x[i] * x[i]; ++n; }
    }
    return n == 0 ? -120.0f : (float) juce::Decibels::gainToDecibels (std::sqrt (s / (double) n), -120.0);
}

float spectralCentroidHz (const juce::AudioBuffer<float>& b)
{
    constexpr int order = 11, N = 1 << order;
    const int n = b.getNumSamples();
    if (n < 64) return 0.0f;
    juce::dsp::FFT fft (order);
    std::vector<float> work (2 * N);
    double num = 0, den = 0;
    const int hop = juce::jmax (N, n / 24);
    for (int start = 0; start < juce::jmax (1, n - N / 2); start += hop)
    {
        std::fill (work.begin(), work.end(), 0.0f);
        for (int i = 0; i < N; ++i)
        {
            const int j = start + i;
            if (j >= n) break;
            float v = 0;
            for (int c = 0; c < b.getNumChannels(); ++c) v += b.getSample (c, j);
            work[(size_t) i] = v * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N));
        }
        fft.performFrequencyOnlyForwardTransform (work.data());
        for (int k = 1; k < N / 2; ++k)
        {
            const double m = work[(size_t) k];
            num += m * k; den += m;
        }
    }
    return den <= 0 ? 0.0f : (float) (num / den * kRate / N);
}

juce::AudioBuffer<float> makeSound (const MakeSettings& s)
{
    // the engine's own sound, or the dropped / Bark audio, as it is: no level change, no EQ, no distortion of ours
    if (s.source == Source::own) return decodeAudio (s.ownAudio, kMaxSourceSeconds);
    if (s.source == Source::words) return decodeAudio (s.wordsAudio, kMaxSourceSeconds);   // only from the Bark helper
    auto out = makeSoundRaw (s);
    // glue: the render stops while a body may still ring, so its last 30 ms fade (no click at the buffer's end)
    dsp::fadeEdges (out, 0, (int) (0.03 * kRate));
    return out;
}

juce::AudioBuffer<float> decodeAudio (const juce::MemoryBlock& bytes, double maxSeconds)
{
    juce::AudioBuffer<float> out;
    if (bytes.getSize() == 0) return out;
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (std::make_unique<juce::MemoryInputStream> (bytes, false)));
    if (r == nullptr || r->lengthInSamples <= 0 || r->sampleRate <= 0) return out;
    const auto len = (int) std::min<juce::int64> (r->lengthInSamples, (juce::int64) (maxSeconds * r->sampleRate));
    juce::AudioBuffer<float> raw ((int) juce::jlimit (1u, 2u, r->numChannels), len);
    r->read (&raw, 0, len, 0, true, raw.getNumChannels() > 1);
    out = dsp::resampleTo (raw, r->sampleRate, kRate);
    if (out.getNumChannels() == 1) { juce::AudioBuffer<float> st (2, out.getNumSamples()); st.copyFrom (0, 0, out, 0, 0, out.getNumSamples()); st.copyFrom (1, 0, out, 0, 0, out.getNumSamples()); out = std::move (st); }
    return out;
}

juce::MemoryBlock encodeFlac (const juce::AudioBuffer<float>& b)
{
    juce::MemoryBlock mb;
    juce::FlacAudioFormat flac;
    auto* os = new juce::MemoryOutputStream (mb, false);
    std::unique_ptr<juce::AudioFormatWriter> w (flac.createWriterFor (os, kRate, (unsigned int) b.getNumChannels(), 24, {}, 0));
    if (w == nullptr) { delete os; return {}; }
    w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    w.reset();
    return mb;
}

bool importAudioFile (const juce::File& f, double maxSeconds, juce::MemoryBlock& flacOut, juce::String& name)
{
    juce::MemoryBlock raw;
    if (! f.loadFileAsData (raw)) return false;
    auto b = decodeAudio (raw, maxSeconds);
    if (b.getNumSamples() < 256) return false;
    flacOut = encodeFlac (b);
    name = f.getFileNameWithoutExtension();
    return flacOut.getSize() > 0;
}
}
