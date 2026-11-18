// Ghost Matter - AGPL-3.0-only (see LICENSE, NOTICE.md).
//
// STRETCH: PaulXStretch (essej/paulxstretch @ b6175e9, GPL-3; Nasca Octavian Paul, Xenakios, Jesse Chappell), its whole
// processing chain, unmodified: its StretchAudioSource (which feeds the ProcessedStretch stretchers, runs its spectral
// modules, resamples, sets the volume and clips) set up exactly as its plug-in's offline render sets it up
// (PaulstretchpluginAudioProcessor::offlineRender: the plug-in's default parameters, window type 1, onset detection 0,
// main volume (its default -3 dB; VOLUME sets it), FFT size 2^(7 + 10 x size) through its optimizebufsize, its spectral-module order and switches).
// Our controls set the plug-in's own parameters: AMOUNT = "Stretch amount", SIZE = "FFT size" (default 0.7 as in the
// plug-in), PITCH = "Pitch shift", UP / DOWN = the "Ratios" module's 1 octave up / down levels, SPREAD = "Frequency spread".
// Nothing of ours touches its output (build 1 matched the loudness, put a tanh on it and faded it: WHY.md).
// Worker thread (allocates).
#pragma once
#include "Settings.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>

namespace matter
{
bool stretchIsOff (const StretchSettings& s);
// `cancel` is polled; a cancelled render returns what it has so far
juce::AudioBuffer<float> stretchSound (const juce::AudioBuffer<float>& in, const StretchSettings& s, const std::atomic<bool>* cancel = nullptr);
int stretchFftSize (float size);     // the plug-in's FFT size for its "FFT size" parameter (0..1)
// the length PaulXStretch itself gives this stretch (its getOutputDurationSecondsForRange), in samples, capped
int stretchLength (int inSamples, const StretchSettings& s);
}
