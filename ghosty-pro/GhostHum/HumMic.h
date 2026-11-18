#pragma once
// GHOST HUM - the Mac's own microphone (build 3). A DAW only feeds Ghost Hum audio when it sits on the microphone's mixer track; loaded
// like an instrument (FL Studio's channel rack) it gets no input at all. So when the host gives it nothing usable, Ghost Hum reads the
// default input device itself: an input-only juce::AudioDeviceManager (macOS lets several apps read the mic at once), opened lazily on
// the message thread on the first record tap that needs it, closed when the plugin goes away or after a while unused.
//
// Threads: open() / close() on the message thread only. The device calls `sink` on its own audio thread with the raw input channels:
// the sink must do no allocation and take no locks (HumProcessor::micCallback writes into a buffer sized here, at open time).
#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include <functional>
#include <memory>

namespace hum {

struct MicInput : private juce::AudioIODeviceCallback {
    std::function<void(const float* const* in, int nCh, int n)> sink;   // (device thread)
    std::atomic<bool> running { false }; std::atomic<double> rate { 0 };
    juce::String error;                                                  // why the last open() failed, in a few words

    ~MicInput() override { close(); }
    bool isOpen() const { return running.load(); }
    // opens the default input device (no output); true when it runs. The sample rate is whatever the device runs at (rate).
    bool open() {
        if (running.load()) return true;
        error.clear(); dm = std::make_unique<juce::AudioDeviceManager>();
        const auto err = dm->initialiseWithDefaultDevices(2, 0);
        if (err.isNotEmpty() || dm->getCurrentAudioDevice() == nullptr) { error = err.isNotEmpty() ? err : juce::String("no input device"); dm.reset(); return false; }
        if (dm->getCurrentAudioDevice()->getActiveInputChannels().countNumberOfSetBits() == 0) { error = "the input device has no channels"; close(); return false; }
        dm->addAudioCallback(this);   // (audioDeviceAboutToStart runs inside this call: `running` and `rate` are set when it returns)
        if (!running.load()) { error = "the input device did not start"; close(); return false; }
        return true;
    }
    void close() { if (dm) { dm->removeAudioCallback(this); dm->closeAudioDevice(); dm.reset(); } running.store(false); }
    juce::String deviceName() const { return dm && dm->getCurrentAudioDevice() ? dm->getCurrentAudioDevice()->getName() : juce::String(); }

private:
    std::unique_ptr<juce::AudioDeviceManager> dm;
    void audioDeviceAboutToStart(juce::AudioIODevice* d) override { rate.store(d != nullptr ? d->getCurrentSampleRate() : 0.0); running.store(d != nullptr); }
    void audioDeviceStopped() override { running.store(false); }
    void audioDeviceIOCallbackWithContext(const float* const* in, int nIn, float* const* out, int nOut, int n, const juce::AudioIODeviceCallbackContext&) override {
        for (int c = 0; c < nOut; ++c) if (out[c] != nullptr) juce::FloatVectorOperations::clear(out[c], n);   // (no output asked for; just in case)
        if (sink && nIn > 0) sink(in, nIn, n);
    }
};

} // namespace hum
