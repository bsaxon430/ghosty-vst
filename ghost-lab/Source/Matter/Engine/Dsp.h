// Ghost Matter - AGPL-3.0-only (see LICENSE, NOTICE.md).
// Small offline DSP helpers shared by the render steps (worker thread; these allocate).
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>

namespace matter::dsp
{
void removeDc (juce::AudioBuffer<float>& b);
void fadeEdges (juce::AudioBuffer<float>& b, int fadeIn, int fadeOut);
juce::AudioBuffer<float> resampleTo (const juce::AudioBuffer<float>& in, double fromRate, double toRate);
float peak (const juce::AudioBuffer<float>& b);
// 4x oversampled peak (true peak estimate), linear
float truePeak (const juce::AudioBuffer<float>& b);
}
