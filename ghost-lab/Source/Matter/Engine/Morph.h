// Ghost Matter - AGPL-3.0-only (see LICENSE, NOTICE.md).
//
// MORPH: take on the colour of another sound.
//   CDP8 (ComposersDesktop/CDP8, LGPL-2.1+), its own programs run unmodified (Cdp.h): pvoc anal, then "combine cross"
//   (our spectral amplitudes replaced by the other sound's, to degree AMOUNT) and "morph morph" (GLIDE: the spectrum
//   glides into the other sound's, amplitudes and frequencies), then pvoc synth. Build 1's own spectral morph is deleted.
//   The other sounds (VOICE, METAL, AIR, GLASS, THROAT, WOOD) are made by the Flues plug-ins (no samples), or YOUR SOUND.
//   NEURAL: Magenta's DDSP-VST (magenta/ddsp-vst, Apache-2.0), its own processor, run offline on the sound (Ddsp.h): it
//   hears the pitch and loudness and plays them back as the instrument its model was trained on. CHAOS / HOP / REFEED are
//   ours, on top: they only move DDSP's own controls and what it is fed.
// Worker thread (allocates).
#pragma once
#include "Settings.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <atomic>
#include <vector>

namespace matter
{
bool morphIsOff (const MorphSettings& m);
juce::AudioBuffer<float> timbreSound (Timbre t, const MorphSettings& m);     // the other sound (2 ch, kRate)
juce::AudioBuffer<float> spectralMorph (const juce::AudioBuffer<float>& in, const juce::AudioBuffer<float>& other, float amount, float detail,
                                        juce::String* why = nullptr, const std::atomic<bool>* cancel = nullptr, const hood::Pins* pins = nullptr);
juce::AudioBuffer<float> morphSound (const juce::AudioBuffer<float>& in, const MorphSettings& m, juce::String* note = nullptr,
                                     const std::atomic<bool>* cancel = nullptr,
                                     std::atomic<float>* neuralProgress = nullptr);   // 0..1 while DDSP runs, -1 when it is not running

}
