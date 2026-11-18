// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
//
// Built-in sounds, all synthesised here from scratch (no recordings, no sample packs), so
// the presets and demos can ship freely (our own work, dedicated to the public domain: CC0).
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>

namespace mosaic::demo
{
struct Sound { juce::String name; juce::AudioBuffer<float> audio; };

std::vector<Sound> kit (const juce::String& which, double rate);   // "drums", "tones", "noise", "vowels"
juce::StringArray kitNames();                                      // plain names for the UI
juce::String kitLabel (const juce::String& which);

// targets
juce::AudioBuffer<float> beatLoop (double rate, int bars = 2, double bpm = 100);
juce::AudioBuffer<float> vowelPhrase (double rate, double seconds = 6);   // a sung "ah-ee-oh-oo" line (formant synth)
juce::AudioBuffer<float> pianoLine (double rate, double seconds = 8);     // a plucked, piano-like melody (additive synth)
}
