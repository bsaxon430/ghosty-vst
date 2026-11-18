#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>
#include <vector>

namespace glab
{
// one sound: the samples and their rate. Snapshots share these read-only (std::shared_ptr<const Audio>).
struct Audio
{
    juce::AudioBuffer<float> buf;
    double sampleRate = 44100.0;
    int channels() const { return buf.getNumChannels(); }
    int length() const { return buf.getNumSamples(); }
    double seconds() const { return sampleRate > 0 ? length() / sampleRate : 0.0; }
};
using AudioPtr = std::shared_ptr<const Audio>;

constexpr double kMaxInputSeconds = 600.0;    // longer files are trimmed (10 minutes)
constexpr double kMaxOutputSeconds = 720.0;   // no result grows past 12 minutes
constexpr float kTruePeakCeilingDb = -1.0f;   // every result: true peak <= -1 dBTP
constexpr float kTruePeakTargetDb = -1.2f;    // (a small margin under the ceiling)

juce::AudioFormatManager& formats();
bool canLoad (const juce::File& f);
// loads any WAV / AIFF / FLAC / MP3 / OGG; stereo at most (more channels are folded to stereo); trimmed to kMaxInputSeconds
std::shared_ptr<Audio> loadAudio (const juce::File& f, juce::String& error, bool* trimmed = nullptr);
bool writeWav (const Audio& a, const juce::File& f, int bits, juce::String& error);   // bits 16 / 24 / 32 (float)
std::shared_ptr<Audio> readWav (const juce::File& f, juce::String& error);            // a CDP result

// one channel of a sound (for CDP, whose spectral programs are mono)
Audio channel (const Audio& a, int ch);
Audio mono (const Audio& a);
bool isEffectivelyMono (const Audio& a);
Audio resampled (const Audio& a, double newRate);

float rms (const Audio& a);
float peak (const Audio& a);
float truePeakDb (const Audio& a);          // 4x oversampled (BS.1770-style) estimate
void sanitize (Audio& a);                    // NaN / inf -> 0
// the finishing stage every result goes through: DC removed, 6 ms fades at both edges, loudness matched to the source
// (at most +12 dB), then the true peak held at or under -1 dBTP
void finish (Audio& a, float referenceRms);
// largest jump between neighbouring samples in the first / last n samples (the click check)
float edgeJump (const Audio& a, int n);

// what the living body is drawn from: per column (time) energy, brightness and roughness of the spectrum, plus the
// waveform overview (min / max per bin)
struct Profile
{
    std::vector<float> energy, centroid, flux, bands;   // bands: columns * kBands, 0..1
    std::vector<float> waveMin, waveMax;
    static constexpr int kColumns = 128, kBands = 12, kWaveBins = 600;
};
Profile makeProfile (const Audio& a);
}
