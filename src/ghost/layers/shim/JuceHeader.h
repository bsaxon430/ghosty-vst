// GHOST BASS b10 (layers): stands in for the JuceHeader.h that Bombaz expects. Only the unity file
// BombazUnity.cpp sees it (it puts "using namespace juce" in the global scope, as their own
// projects do). Our code; the upstream sources are not changed.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#ifndef GB_NO_USING_JUCE
using namespace juce;
#endif
