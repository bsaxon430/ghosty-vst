// Ghost Matter - AGPL-3.0-only (see LICENSE, NOTICE.md).
//
// The render: MAKE -> STRETCH -> MORPH on a background thread, each step's result kept so that changing a later
// step does not redo the earlier ones. The result ("the matter") is an immutable object the audio thread plays.
#pragma once
#include "Settings.h"
#include "Morph.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <atomic>
#include <memory>

namespace matter
{
// what the pictures on the pages are drawn from (the same idea as Ghost Lab's Profile)
struct Profile
{
    static constexpr int kColumns = 128, kBands = 12, kWaveBins = 600;
    std::vector<float> energy, centroid, flux, bands;   // bands: columns * kBands, 0..1
    std::vector<float> waveMin, waveMax;
};
Profile makeProfile (const juce::AudioBuffer<float>& a);

struct Matter
{
    juce::AudioBuffer<float> audio;   // kRate, 2 ch
    int loopFrom = 0;                 // a looped key cross-fades its last loopFrom samples into the start (0 = no loop)
    int length() const { return audio.getNumSamples(); }
    Profile made, profile;            // the MAKE sound and the final matter
    juce::String note;                // e.g. "NEURAL OFF: ..." for the MORPH page
    RenderSettings settings;
    juce::uint64 serial = 0;
};

// how long the loop's cross-fade is for `b`
int makeLoopable (juce::AudioBuffer<float>& b, int fadeSamples);

class Renderer : private juce::Thread
{
public:
    Renderer();
    ~Renderer() override;
    void request (const RenderSettings& s);                  // any thread; the newest request wins
    std::shared_ptr<const Matter> latest() const;            // message thread
    bool busy() const { return working.load() || pendingFlag.load(); }
    float progress() const                                    // 0..1 through the steps (the neural model's own progress inside MORPH)
    {
        const float n = neural.load();
        return (stage.load() + (stage.load() == 2 && n > 0.0f ? n : 0.0f)) / 3.0f;
    }
    float neuralProgress() const { return neural.load(); }   // 0..1 while DDSP runs, -1 otherwise
    juce::uint64 serial() const { return doneSerial.load(); }
    // renders synchronously on the calling thread (tests, offline export)
    static std::shared_ptr<Matter> renderNow (const RenderSettings& s);

private:
    void run() override;
    mutable juce::SpinLock lock;
    RenderSettings pending;
    std::atomic<bool> pendingFlag { false }, working { false }, cancel { false };
    std::atomic<int> stage { 0 };
    std::atomic<float> neural { -1.0f };
    std::atomic<juce::uint64> doneSerial { 0 };
    std::shared_ptr<const Matter> result;
    // the cache: each step's input settings and output
    struct Cache
    {
        bool valid = false;
        MakeSettings make; StretchSettings stretch; MorphSettings morph;
        juce::AudioBuffer<float> made, stretched;
        Profile madeProfile;
    } cache;
    std::shared_ptr<Matter> renderCached (const RenderSettings& s);
};
}
