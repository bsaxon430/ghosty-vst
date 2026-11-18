#pragma once
// GHOST SLICE source audio: the decoded original (never modified), its SHA-256 file fingerprint, the min/max peak pyramid used
// for drawing at any zoom, the working copy at the host rate (a cache) and the onset list (transient lock + FLEX snap targets):
// the spectral-flux + HFC detector of Analysis.h (GHOST-SLICE-STUDY.md 3.2), AUTO material profile, cached as onsets-v1-auto.bin.
#include "SliceCore.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>

namespace gslice
{
// ---------------------------------------------------------------- min/max peak pyramid
// level 0 = 32 frames per bin; each level is 4x coarser (32, 128, 512, 2k, 8k, 32k, ...). Per bin and channel: min, max, mean square.
struct PeakPyramid
{
    static constexpr int kBase = 32, kFactor = 4;
    struct Bin { float mn = 0, mx = 0, ms = 0; };
    int channels = 0; int64 frames = 0;
    std::vector<std::vector<std::vector<Bin>>> levels;   // [level][channel][bin]

    void build (const juce::AudioBuffer<float>& audio);
    static int64 framesPerBin (int level) { int64 f = kBase; for (int i = 0; i < level; ++i) f *= kFactor; return f; }
    int numLevels() const { return (int) levels.size(); }
    // min / max / rms of channel ch over source frames [a, b) (any span; uses the coarsest level that tiles it, raw audio for the edges)
    Bin query (const juce::AudioBuffer<float>& audio, int ch, int64 a, int64 b) const;
    juce::String digest() const;   // a hash of every bin (determinism tests)
};

// ---------------------------------------------------------------- GHOST CAPTURE clip hint (extension point for Ghost Box)
// A captured clip may carry a sidecar next to it: "<file name>.ghostclip.json" (e.g. take.wav.ghostclip.json) or
// "<stem>.ghostclip.json" (take.ghostclip.json):  { "format": "ghostclip", "version": 1, "bpm": 92.5, "bars": 4,
// "beatsPerBar": 4, "offset": 0.0 (seconds to the first downbeat), "key": "F# MIN" }  - every field but format optional; bpm or
// bars is needed (bars alone: bpm = bars x beatsPerBar x 60 / the clip's length). On load the project takes the tempo, grid phase
// and key from it instead of guessing.
struct ClipHint
{
    double bpm = 0;              // 0 = none
    int bars = 0, beatsPerBar = 4;
    double offsetSeconds = 0;
    juce::String key;            // "" = none
    juce::File file;             // the sidecar it came from
    bool valid() const { return bpm >= 20.0 && bpm <= 999.0; }
};

// ---------------------------------------------------------------- the loaded source
struct SourceAudio
{
    juce::File file;
    juce::String fingerprint;            // SHA-256 (hex) of the file's bytes: the identity used by projects, relink and caches
    juce::String formatName;
    double sampleRate = 48000.0;
    int channels = 0;
    int64 frames = 0;
    juce::AudioBuffer<float> audio;      // the decoded original at its own rate, float (read-only after load)
    PeakPyramid peaks;
    std::vector<int64> onsets;           // onsets (SF, sample accurate), sorted by position (AUTO profile; ALIGN uses the analysis' own set)
    std::vector<float> onsetStrength;    // 0..1 per onset
    ClipHint clip;                       // GHOST CAPTURE sidecar (invalid = none)

    double seconds() const { return sampleRate > 0 ? (double) frames / sampleRate : 0.0; }
    juce::String describe() const;       // "48.0 kHz · 2 ch · 3.214 s · WAV"
};

// The working copy: the source resampled to the host rate (or the original buffer itself when the rates match).
struct WorkingAudio
{
    std::shared_ptr<const SourceAudio> src;
    double rate = 48000.0;               // = host rate
    double ratio = 1.0;                  // WF per SF
    juce::AudioBuffer<float> resampled;  // empty when ratio == 1 (then the original is read directly)
    const juce::AudioBuffer<float>& buffer() const { return ratio == 1.0 ? src->audio : resampled; }
    int64 frames() const { return (int64) buffer().getNumSamples(); }
    int channels() const { return buffer().getNumChannels(); }
    // one sample with zero outside the file; channel ch is clamped (mono sources feed both outputs)
    inline float at (int ch, int64 i) const noexcept
    {
        const auto& b = buffer();
        if (i < 0 || i >= (int64) b.getNumSamples() || b.getNumChannels() == 0) return 0.0f;
        return b.getReadPointer (juce::jmin (ch, b.getNumChannels() - 1))[i];
    }
};

namespace source
{
    juce::AudioFormatManager& formats();                 // WAV / AIFF / FLAC / MP3 (+ whatever JUCE registers)
    juce::String wildcard();                             // "*.wav;*.aif;..."
    bool canLoad (const juce::File& f);
    juce::String fingerprintFile (const juce::File& f);  // SHA-256 hex of the bytes ("" when unreadable)
    // decode + fingerprint + peaks (from / into peakCacheDir when given) + onsets. error gets a short user message on failure. Any thread.
    std::shared_ptr<const SourceAudio> load (const juce::File& f, juce::String& error, const juce::File& peakCacheDir = {});
    // from memory (tests / generated audio): fingerprint = SHA-256 of the float data
    std::shared_ptr<const SourceAudio> fromBuffer (juce::AudioBuffer<float> audio, double sampleRate, const juce::String& name);
    std::shared_ptr<const WorkingAudio> makeWorking (std::shared_ptr<const SourceAudio> src, double hostRate);   // r8brain (anti-aliased)
    // r8brain-free-src CDSPResampler24 (MIT): in[0, frames) at fromRate -> out (its size = the output length) at toRate, linear
    // phase, latency removed (output frame k = input time k * fromRate / toRate). Any thread except the audio thread.
    void resampleChannels (const juce::AudioBuffer<float>& in, int frames, double fromRate, double toRate, juce::AudioBuffer<float>& out);
    // the onset list: spectral flux + HFC (analysis::detectOnsets, AUTO material), strength 0..1. cacheDir: read / write
    // cache/analysis/<sha>/onsets-v1-auto.bin when given (fingerprint needed)
    void detectOnsets (const juce::AudioBuffer<float>& audio, double sr, std::vector<int64>& pos, std::vector<float>& strength,
                       const juce::String& fingerprint = {}, const juce::File& cacheDir = {});
    // (the phase 1-6 name; the same API, now the spectral detector)
    inline void detectEnergyOnsets (const juce::AudioBuffer<float>& audio, double sr, std::vector<int64>& pos, std::vector<float>& strength) { detectOnsets (audio, sr, pos, strength); }
    // search folders near `near` (its folder, the parent, the project folder; 2 levels deep) for a file with this fingerprint
    // the GHOST CAPTURE sidecar of an audio file (see ClipHint): false (and why, when a sidecar exists but is unusable) = none
    bool readClipHint (const juce::File& audio, double clipSeconds, ClipHint& out, juce::String* why = nullptr);
    juce::File findByFingerprint (const juce::String& fingerprint, const juce::String& fileName, const juce::Array<juce::File>& near);
}
} // namespace gslice
