// Ghost Matter - the header JUCE generates for a plug-in (juce_generate_juce_header), written out for DDSP-VST's own sources
// (third_party/ddsp-vst/src, unmodified), as its DDSP Effect target would get it: JucePlugin_IsSynth 0 (the effect: it
// listens to the audio it is fed), its BinaryData (icons, the pitch model, the 11 instrument models) and its ProjectInfo.
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "BinaryData.h"
#include <math.h>   // its sources call isnan unqualified (the C library name), as Apple's headers allow

#undef JucePlugin_IsSynth
#define JucePlugin_IsSynth 0
#undef JucePlugin_WantsMidiInput
#define JucePlugin_WantsMidiInput 0
#undef JucePlugin_IsMidiEffect
#define JucePlugin_IsMidiEffect 0
#undef JucePlugin_ProducesMidiOutput
#define JucePlugin_ProducesMidiOutput 0
#undef JucePlugin_Name
#define JucePlugin_Name "DDSP Effect"

namespace ProjectInfo
{
    const char* const projectName = "DDSP";
    const char* const companyName = "Magenta";
    const char* const versionString = "1.1.0";
    const int versionNumber = 0x10100;
}
