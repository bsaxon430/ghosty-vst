// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Processor.h"
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new atlas::Processor(); }
