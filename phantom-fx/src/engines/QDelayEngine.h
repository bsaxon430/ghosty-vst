// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX: QDELAY, ECTO ECHO's engine running tiagolr's QDelay (GPL-3, third_party/qdelay, unchanged) (DECISIONS.md 93).
// Only the factory is visible here: QDelay's own headers live inside namespace qdelay (third_party/qdelay_glue) and stay in
// QDelayEngine.cpp, so its generic class names never reach the rest of the plugin.
#pragma once
#include "AltEngine.h"
#include <memory>
#include <string>

namespace phantom::engines
{
int qdelayVoices();
std::string qdelayVoiceName (int voice);
std::unique_ptr<AltEngine> makeQDelayEngine();
}
