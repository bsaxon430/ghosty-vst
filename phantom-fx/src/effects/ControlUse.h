// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX: which controls do nothing at a unit's current settings (Brandon, 2026-11-12: "always grey it out on anything that runs into
// this"). The editors (Phantom FX's pages, Ghost Break's FX panel) grey those controls out and say why; they stay movable, so a setting
// can be made ready before the mode that uses it is picked. Rules come from what the code reads (DECISIONS.md 89):
//   - an ENGINE other than PHANTOM reads only some of the unit's controls (each engine's own list, from its source)
//   - a mode, type or section switch leaves some controls out (PHANTOM FILTER's SAMPLER types have no resonance, ...)
// Only switches, menus and stepped counts decide; a knob at zero never greys out another knob.
#pragma once
#include <functional>
#include <string>
#include <vector>

namespace phantom
{
// value (id): the unit's current value of one of its controls, in its own units (a menu or a switch: its index / 0 or 1).
// Returns an empty string when the control is in use, else a short reason for its tooltip.
using ControlValue = std::function<float (const std::string& id)>;
std::string controlIdleReason (int unit, const std::string& id, const ControlValue& value);
// every control id the rules name for a unit (the tests check each one exists)
std::vector<std::string> controlRuleIds (int unit);
} // namespace phantom
