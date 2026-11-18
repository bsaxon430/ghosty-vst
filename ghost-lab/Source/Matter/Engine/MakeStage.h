// Ghost Matter - AGPL-3.0-only (see LICENSE, NOTICE.md).
//
// MAKE: the first sound, from Flues (danja/flues @ 2eae800, MIT, Danny Ayers), run through Flues' OWN LV2 plug-in code
// (FluesLv2.h) with its own .ttl defaults:
//   PHYSICAL  the PM-Synth plug-in (12 interface models, pluck .. plasma)
//   VOICE     the Chatterbox plug-in, with Flues' own vowel presets
//   NOISE     the Disyn plug-in (distortion synthesis)
//   WORDS     only from the Bark helper (helpers/words: real Bark); nothing of ours stands in for it
//   YOUR SOUND  the dropped audio, as it is
// LOUD / BRIGHT / ROUGH / SHAPE move the plug-ins' own controls (at their defaults every control sits at the .ttl
// default). Nothing of ours touches the sound (build 1 had a ring modulator, a tanh, a tilt EQ, an upward compressor and
// loudness matching here: see WHY.md).
#pragma once
#include "Settings.h"
#include "FluesLv2.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace matter
{
// renders at kRate, 2 channels. Worker thread (allocates).
juce::AudioBuffer<float> makeSound (const MakeSettings& s);
// the Flues plug-in's ports for these settings: our controls' mapping (when `p` is the source in use) or the .ttl defaults, and then
// the ports you moved in UNDER THE HOOD (withPins)
std::map<std::string, float> fluesPorts (const MakeSettings& s, fluesplug::Plugin p, bool withPins = true);
// the engine's own output alone (no level, no feel processing)
juce::AudioBuffer<float> makeSoundRaw (const MakeSettings& s);

// decodes FLAC / WAV / AIFF / MP3 bytes to 2 channels at kRate, at most maxSeconds (empty on failure)
juce::AudioBuffer<float> decodeAudio (const juce::MemoryBlock& bytes, double maxSeconds);
// a file -> FLAC bytes at kRate (stereo, <= maxSeconds). The name for display in `name`.
bool importAudioFile (const juce::File& f, double maxSeconds, juce::MemoryBlock& flacOut, juce::String& name);
juce::MemoryBlock encodeFlac (const juce::AudioBuffer<float>& b);

// descriptors (tests, readouts)
float spectralCentroidHz (const juce::AudioBuffer<float>& b);
float rmsDb (const juce::AudioBuffer<float>& b);
float targetCentroidHz (float bright);       // what BRIGHT asks for
}
