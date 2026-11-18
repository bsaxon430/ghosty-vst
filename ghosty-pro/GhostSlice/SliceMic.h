#pragma once
// GHOST SLICE - the Mac's own microphone for RECORD. A DAW only feeds an instrument audio when it is routed to it (FL Studio's
// channel rack gives an instrument no input at all), so with RECORD on INPUT AUTO / MAC MIC Ghost Slice also reads the default input
// device itself: an input-only juce::AudioDeviceManager (macOS lets several apps read the mic at once), opened on the message
// thread when recording starts, closed when it stops. (The same approach as Ghost Hum build 3, written again here.)
// Threads: open() / close() on the message thread only. `sink` runs on the device's audio thread: no allocation, no locks.
#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include <functional>
#include <memory>

namespace gslice
{
struct MicInput : private juce::AudioIODeviceCallback
{
    std::function<void (const float* const* in, int nCh, int n)> sink;   // (device thread)
    std::atomic<bool> running { false }; std::atomic<double> rate { 0 };
    juce::String error;

    ~MicInput() override { close(); }
    bool isOpen() const { return running.load(); }
    bool open()
    {
        if (running.load()) return true;
        error.clear(); dm = std::make_unique<juce::AudioDeviceManager>();
        const auto err = dm->initialiseWithDefaultDevices (2, 0);
        if (err.isNotEmpty() || dm->getCurrentAudioDevice() == nullptr) { error = err.isNotEmpty() ? err : juce::String ("no input device"); dm.reset(); return false; }
        if (dm->getCurrentAudioDevice()->getActiveInputChannels().countNumberOfSetBits() == 0) { error = "the input device has no channels"; close(); return false; }
        dm->addAudioCallback (this);
        if (! running.load()) { error = "the input device did not start"; close(); return false; }
        return true;
    }
    void close() { if (dm) { dm->removeAudioCallback (this); dm->closeAudioDevice(); dm.reset(); } running.store (false); }

private:
    std::unique_ptr<juce::AudioDeviceManager> dm;
    void audioDeviceAboutToStart (juce::AudioIODevice* d) override { rate.store (d != nullptr ? d->getCurrentSampleRate() : 0.0); running.store (d != nullptr); }
    void audioDeviceStopped() override { running.store (false); }
    void audioDeviceIOCallbackWithContext (const float* const* in, int nIn, float* const* out, int nOut, int n, const juce::AudioIODeviceCallbackContext&) override
    {
        for (int c = 0; c < nOut; ++c) if (out[c] != nullptr) juce::FloatVectorOperations::clear (out[c], n);
        if (sink && nIn > 0) sink (in, nIn, n);
    }
};
} // namespace gslice
