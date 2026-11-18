// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX: CHOWMATRIX, ECTO ECHO's engine running Chowdhury DSP's ChowMatrix (BSD-3, third_party/chowmatrix, unchanged)
// (DECISIONS.md 94). Only the factory is visible here: ChowMatrix and the old chowdsp_utils it is built on live inside namespace
// chowmatrix (third_party/chowmatrix_glue) and stay in ChowMatrixEngine.cpp, so they never meet the chowdsp_utils Phantom FX uses.
#pragma once
#include "AltEngine.h"
#include <memory>
#include <string>

namespace phantom::engines
{
int chowMatrixVoices();
std::string chowMatrixVoiceName (int voice);
std::unique_ptr<AltEngine> makeChowMatrixEngine();
}
