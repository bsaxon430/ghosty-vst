// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX: what ChowTapeModel's sources expect from their plugin's generated JuceHeader.h (JUCE + chowdsp, `using namespace juce`),
// so the upstream files in third_party/chowtape compile unchanged. Only their DSP classes are used; the plot hooks are no-ops.
#pragma once
#include <juce_dsp/juce_dsp.h>
#include <chowdsp_simd/chowdsp_simd.h>
#include <chowdsp_math/chowdsp_math.h>
#include <chowdsp_filters/chowdsp_filters.h>
#include <chowdsp_sources/chowdsp_sources.h>
using namespace juce;
namespace foleys { struct MagicPlotSource { void pushSamples (const juce::AudioBuffer<float>&) {} }; }
