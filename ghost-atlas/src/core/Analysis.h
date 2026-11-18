// SPDX-License-Identifier: AGPL-3.0-or-later
// GHOST ATLAS - listening to an instrument: it is played through the real engine (offline), and what comes out is
// measured. The numbers are what the browser's tags, the level matching and MUTATE work from.
#pragma once
#include "Library.h"
#include "SfzLayer.h"

namespace atlas
{
struct Note { int note = 60; float velocity = 0.8f; double start = 0.0, length = 1.0; };

// plays notes through one instrument, offline (every sample fully loaded first), and returns stereo audio
juce::AudioBuffer<float> renderInstrument (const juce::File& sfz, const SfzShape& shape, const std::vector<Note>& notes,
                                           double seconds, double sampleRate = 44100.0, SfzInfo* info = nullptr);

struct AnalysisResult
{
    bool ok = false;
    juce::String error;
    Features features;
    std::vector<float> preview;
    juce::StringArray unsupported;
    int regions = 0;
};

// keys: the keys that play something (SfzScan::keys); the probes only use those, so an instrument with gaps is still heard
AnalysisResult analyseInstrument (const juce::File& sfz, const juce::String& instrumentId, const std::vector<int>& keys);
} // namespace atlas
