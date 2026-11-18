// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
//
// brain.cpp includes <spiralcore/audio.h> (the PortAudio device wrapper) but uses nothing from
// it. In the plug-in the host (or JUCE's standalone wrapper) owns the audio device, so this
// empty header stands in for it. The original stays in samplebrain/brain/src/spiralcore/.
#pragma once
#include "spiralcore/types.h"
