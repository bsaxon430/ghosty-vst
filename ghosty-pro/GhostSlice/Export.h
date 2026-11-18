#pragma once
// Exports (the demo gate guards every caller: SliceEditor asks ghosty::exportAllowed with "slice.export.wav" / "slice.export.midi"
// before the save dialog; the processor re-checks silently with ghosty::exportUnlocked before it writes).
#include "RenderPlan.h"

namespace gslice::exporter
{
    // a WAV of the audio (24-bit integer or 32-bit float)
    bool writeWav (const juce::AudioBuffer<float>& audio, double sampleRate, const juce::File& f, int bits, juce::String& error);
    // MIDI of the slice positions: one note per piece in output order (C1 = 36 upward, like the PLAY page will map them),
    // at the project tempo, 960 PPQ; note length = the piece's length
    juce::MidiFile slicesMidi (const Plan& plan, double bpm);
    bool writeMidi (const juce::MidiFile& m, const juce::File& f, juce::String& error);
}
