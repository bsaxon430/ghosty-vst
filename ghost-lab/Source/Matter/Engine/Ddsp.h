// Ghost Matter - AGPL-3.0-only (see LICENSE, NOTICE.md).
//
// NEURAL: Magenta's DDSP-VST (magenta/ddsp-vst @ f2996e9, Apache-2.0; third_party/ddsp-vst, unmodified). Its own effect
// processor (DDSPAudioProcessor: CREPE-style pitch + loudness tracking on TensorFlow Lite, the instrument model, its
// harmonic + noise synthesizers, its output gain and reverb) runs offline on the sound, block by block, exactly as a host
// renders the plug-in offline (non-realtime: inference on the calling thread). Its 11 instruments are built in; a model of
// your own (.tflite, from Magenta's training colab) goes in Documents/Magenta/DDSP/Models, the folder DDSP-VST itself reads.
//
// CHAOS, HOP and REFEED are ours, on top: they only move DDSP's own controls (its Pitch Shift, Input Pitch, Input Gain),
// switch its model (its own loadModel), or feed its output back in. At 0 the model plays as DDSP-VST plays it.
// Worker thread (allocates). One processor serves the whole process, one render at a time (it holds its models in memory).
#pragma once
#include "Settings.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>

namespace matter::neural
{
bool available();                          // false when this build has no DDSP in it (GHOSTMATTER_DDSP=OFF)
juce::StringArray modelNames();            // its model library's list: the 11 built in, then your own
juce::File userModelsFolder();             // Documents/Magenta/DDSP/Models (DDSP-VST's own folder)
int passesFor (float refeed);              // REFEED 0..1 -> 1..4 passes through the model
int holdBlocksFor (float wobble);          // CHAOS: how many model frames (20 ms) each random jump is held
int hopBlocksFor (float hop);              // HOP: model frames between model switches (0 = never)

// one render: `in` (kRate, 1 or 2 ch) through DDSP with the model `m.ddspModel`, its pinned controls (ddsp.*) and CHAOS /
// HOP / REFEED. `out` is 2 ch, the length of `in`, lined up with it (its 64 ms latency taken off). False + `why` on failure.
bool render (const juce::AudioBuffer<float>& in, const MorphSettings& m, juce::AudioBuffer<float>& out, juce::String& why,
             const std::atomic<bool>* cancel = nullptr, std::atomic<float>* progress = nullptr);
}
