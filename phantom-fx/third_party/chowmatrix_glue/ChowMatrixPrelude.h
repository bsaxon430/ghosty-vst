// Phantom FX glue for ChowMatrix (DECISIONS.md 94): included first by every wrapper in tu/, which then includes one .cpp of ChowMatrix,
// of the chowdsp_utils version it was built on (c34df55), of Foleys GUI Magic (3f0188f) or of polylogarithm, inside namespace chowmatrix.
// Phantom FX already uses a newer chowdsp_utils whose classes have the same names in the same namespace chowdsp; inside namespace
// chowmatrix the old ones are chowmatrix::chowdsp::..., so the two versions cannot meet (a silent one-definition clash otherwise).
// The system and JUCE headers they use are included here, outside the namespace, so their own #includes of them are no-ops.
#pragma once
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "ChowMatrixBinaryData.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <complex>
#include <deque>
#include <functional>
#include <future>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <random>
#include <unordered_map>
#include <utility>
#include <vector>
#include <type_traits>
#include <iostream>
#include <sstream>
#include <string>
#include <stack>
#include <cfloat>
#include <set>
#include <list>
#include <optional>
// as ChowMatrix's own CMake files set them (modules/CMakeLists.txt, CMakeLists.txt), except: no OpenGL context and no auto-updater
// (that one is a GUI feature that checks the internet for new versions)
#undef JucePlugin_Name
#define JucePlugin_Name "ChowMatrix"
#undef JucePlugin_Manufacturer
#define JucePlugin_Manufacturer "chowdsp"
#undef JucePlugin_VersionString
#define JucePlugin_VersionString "1.3.0"
#undef JucePlugin_ManufacturerWebsite
#undef JucePlugin_IsSynth
#define JucePlugin_IsSynth 0
#undef JucePlugin_IsMidiEffect
#define JucePlugin_IsMidiEffect 0
#undef JucePlugin_WantsMidiInput
#define JucePlugin_WantsMidiInput 0
#undef JucePlugin_ProducesMidiOutput
#define JucePlugin_ProducesMidiOutput 0
#undef JucePlugin_PreferredChannelConfigurations
#define FOLEYS_SHOW_GUI_EDITOR_PALLETTE 0
#define FOLEYS_ENABLE_BINARY_DATA 1
#define FOLEYS_ENABLE_OPEN_GL_CONTEXT 0
#define CHOWDSP_AUTO_UPDATE 0
