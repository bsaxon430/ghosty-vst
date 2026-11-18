// Phantom FX glue for QDelay (third_party/qdelay, unchanged): the JuceHeader.h its files include. QDelay's own Projucer/CMake header
// pulls in the JUCE modules, its BinaryData and `using namespace juce`; this does the same, with its resources under their own name
// (QDelayBinaryData, aliased to BinaryData inside namespace qdelay by QDelayPrelude.h) so nothing clashes with Phantom FX's.
#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "QDelayBinaryData.h"
using namespace juce;
