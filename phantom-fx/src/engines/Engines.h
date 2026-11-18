// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX: the ENGINE menus (DECISIONS.md 85).
#pragma once
#include "../effects/Catalog.h"
#include <memory>
#include <string>
#include <vector>

namespace phantom::engines
{
std::vector<std::string> engineLabels (int unit);   // empty: the unit has only its own engine (no ENGINE menu)
// the unit's module with its ENGINE menu around it (or the module itself when it has none); defaultEntry: the menu entry it starts on
std::unique_ptr<PhantomEffect> withEngines (int unit, std::unique_ptr<PhantomEffect> own, int defaultEntry = 0);
// the level match of one menu entry against the unit's PHANTOM engine (EngineGains.inc, from the tests' calibration run)
float levelMatch (int unit, const std::string& label);
} // namespace phantom::engines
