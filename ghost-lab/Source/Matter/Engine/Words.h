// Ghost Matter - AGPL-3.0-only (see LICENSE, NOTICE.md).
//
// WORDS: make a sound from words. With the optional "ghostmatter-words" helper (helpers/words: a small script of ours
// around Bark, suno-ai/bark, MIT, code and weights) the words are spoken by one of Bark's own synthetic speaker presets
// (v2/en_speaker_0 .. 9; no voice cloning, no recordings of real people). It runs as a separate program, off the audio
// thread, and its result is kept in the session (MakeSettings::wordsAudio). Without it there are no words: build 1's
// own formant babble is deleted (only real Bark speaks).
#pragma once
#include "Settings.h"

namespace matter
{
juce::File wordsHelper();                                    // empty = not installed
juce::String barkSpeaker (int body);                         // "v2/en_speaker_3"
// runs the helper (blocking: a worker thread); FLAC at kRate in `flacOut`
// `pins`: Bark's own generate_audio options from UNDER THE HOOD (bark.speaker, bark.text_temp, bark.waveform_temp, bark.small), only those you set
bool runWords (const juce::String& text, int speaker, juce::MemoryBlock& flacOut, juce::String& why, const hood::Pins* pins = nullptr);
}
