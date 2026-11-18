#pragma once
// Exports (the demo gate guards every caller: SliceEditor asks ghosty::exportAllowed with "slice.drag.wav" / "slice.drag.midi" /
// "slice.take.save" before it writes; the processor re-checks silently with ghosty::exportUnlocked before it writes).
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

namespace gslice::exporter
{
    // a WAV of the audio (24-bit integer or 32-bit float)
    bool writeWav (const juce::AudioBuffer<float>& audio, double sampleRate, const juce::File& f, int bits, juce::String& error);
    bool writeMidi (const juce::MidiFile& m, const juce::File& f, juce::String& error);
}
