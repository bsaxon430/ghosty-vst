// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX: CORRUPTOR and WARPCORE (DECISIONS.md 95). Spectral Corruptor's effects (Dirty-Octopus, MPL-2.0) and WarpCore's spectral
// inversion (ManasWolrd, GPL-3), the same upstream code and the same hosting (SpectralFx) as Poltergeist 2's FX page, spread over the
// Phantom FX effects they fit: each Spectral Corruptor effect is a voice of a CORRUPTOR engine in the effect it belongs with, and reads
// that effect's own knobs (SpectralEngines.cpp lists which knob drives which of its parameters).
#pragma once
#include "AltEngine.h"
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace phantom::engines
{
// the spectral engines of one unit: name ("CORRUPTOR", "WARPCORE") and voice names, in menu order
struct SpectralEngineInfo { std::string name; std::vector<std::string> voices; };
std::vector<SpectralEngineInfo> spectralEngines (int unit);
std::unique_ptr<AltEngine> makeSpectralEngine (int unit, int engineIndex);
// the unit's controls a menu entry ("CORRUPTOR · BIN SHUFFLE") reads; nullptr = not a spectral entry
const std::set<std::string>* spectralReads (int unit, const std::string& label);
} // namespace phantom::engines
