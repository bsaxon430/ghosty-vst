// Phantom FX glue for QDelay: included first by every wrapper in tu/, which then includes one of QDelay's own .cpp files, unchanged,
// inside namespace qdelay. Its class names (Delay, SVF, Distortion, Crusher, LFO, Utils ...) are generic, so they live in their own
// namespace and cannot meet a Phantom FX or another library's class of the same name (a silent one-definition clash otherwise).
// The system and JUCE headers it uses are included here, outside the namespace, so its own #includes of them inside it are no-ops.
#pragma once
#include "JuceHeader.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <deque>
#include <functional>
#include <utility>
#include <vector>
#include <memory>
#include <random>
#undef JucePlugin_Name
#define JucePlugin_Name "QDelay"
#undef JucePlugin_IsSynth
#define JucePlugin_IsSynth 0
#undef JucePlugin_IsMidiEffect
#define JucePlugin_IsMidiEffect 0
#undef JucePlugin_WantsMidiInput
#define JucePlugin_WantsMidiInput 0
#undef JucePlugin_ProducesMidiOutput
#define JucePlugin_ProducesMidiOutput 0
#undef JucePlugin_PreferredChannelConfigurations
#ifndef PROJECT_VERSION
#define PROJECT_VERSION "1.2.3"   // as QDelay's CMakeLists.txt defines it (project(QDelay VERSION 1.2.3))
#endif
namespace qdelay
{
    namespace BinaryData = ::QDelayBinaryData;
    namespace ProjectInfo { const char* const projectName = "QDelay"; const char* const companyName = "Tilr"; const char* const versionString = "1.2.3"; const int versionNumber = 0; }
}
