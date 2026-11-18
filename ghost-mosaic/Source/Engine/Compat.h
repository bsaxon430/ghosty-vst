// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
// See LICENSE and NOTICE.md in this folder.
//
// The glue that lets the unmodified Samplebrain engine run inside a plug-in:
//  - its sound loading (libsndfile calls) is served by JUCE's readers, resampled to the
//    plug-in's sample rate, or from sounds held in memory (sessions, built-in demo kits);
//  - its status messages (liblo calls) go to whoever is listening on the calling thread.
#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <functional>
#include <mutex>

namespace mosaic::compat
{
// Sounds in memory: brain::load_sound ("mem:<key>") reads these instead of a file.
// The buffer is stored at the rate it is given (rate), and resampled like a file on read.
void putMemorySound (const juce::String& key, const juce::AudioBuffer<float>& audio, double rate);
void removeMemorySound (const juce::String& key);
bool hasMemorySound (const juce::String& key);

// The sample rate the engine works at, for loads on this thread (files are resampled to it).
// 0 = no resampling (exactly as upstream Samplebrain).
void setThreadEngineRate (double rate);

// Read any supported file (or mem: key) as interleaved float frames at the thread's engine rate.
struct Audio { int channels = 0; int64_t frames = 0; int rate = 0; std::vector<float> interleaved; };
bool readAudio (const juce::String& pathOrKey, Audio& out, juce::String* error = nullptr);

// Status messages from the engine on this thread ("/report" text, "/sound-item" name+colour).
using StatusSink = std::function<void (const juce::String& path, const juce::StringArray& args)>;
void setThreadStatusSink (StatusSink* sink);

// Every thread that builds blocks must call this once (FFTW's planner made thread safe).
void initFftThreads();

juce::AudioFormatManager& formats();
}
