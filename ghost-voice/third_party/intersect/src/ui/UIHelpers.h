#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

namespace UIHelpers
{

// Computes a new parameter value from a vertical drag gesture.
// startVal    : value at drag start
// deltaY      : upward pixels (positive = increase)
// minVal/maxVal: parameter range
// coarse      : true when Shift is held (5-unit snap; default: 1-unit)
// Sensitivity : full parameter range covered in 200 px of drag
inline float computeDragValue (float startVal, float deltaY,
                                float minVal, float maxVal, bool coarse)
{
    float sensitivity = (maxVal - minVal) / 200.0f;
    float newVal = startVal + deltaY * sensitivity;
    float snap = coarse ? 5.0f : 1.0f;
    newVal = std::round (newVal / snap) * snap;
    return juce::jlimit (minVal, maxVal, newVal);
}

// Computes a zoom multiplier from a vertical drag delta.
// Each pixel of downward drag multiplies zoom by 1.01, giving
// approximately 70 px to double or halve the zoom level.
inline float computeZoomFactor (float deltaY)
{
    return std::pow (1.01f, deltaY);
}

} // namespace UIHelpers

namespace UILayout
{

// Waveform vertical scale factor per channel.
// At 0.48, each channel's peak reaches 48% of component height,
// leaving a small visible gap between the two channels at 0 dBFS.
static constexpr float waveformVerticalScale = 0.48f;

} // namespace UILayout
