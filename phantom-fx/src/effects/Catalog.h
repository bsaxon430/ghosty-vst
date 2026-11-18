// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX: the 24 effects — identity (key, title, group, accent, ghost look) and every control, declared once.
// Keys, titles and parameter ids are the Ghost Box ones (AUDIT.md) so names and automation stay stable for good.
#pragma once
#include "../core/PhantomEffect.h"
#include <memory>

namespace phantom
{
enum Unit { uGate, uGrid, uHaunt, uWarp, uCrackle, uWreck, uTube, uCrush, uRetro, uFatten, uBassLab, uFormant, uEq, uFilter,
            uChorus, uCrystal, uEcho, uSpace, uShimmer, uHalls, uSpectra, uPortal, uOtt, uLim, kNumUnits };
enum Group { gRhythm, gColour, gSpace, gMaster };

struct UnitDef
{
    const char* key; const char* title; const char* shortName; const char* blurb; int group; juce::uint32 accent; int look; bool hasMix; float defaultMix;
};
const UnitDef& unitDef (int u);
const char* groupName (int g);
std::vector<ParamSpec> unitParams (int u, bool withEngineMenu = true);   // the controls of unit u (AUDIT.md); the last one is its ENGINE menu when it has more engines (DECISIONS.md 85)
std::unique_ptr<PhantomEffect> makeEffect (int u);  // the current module for unit u, with its ENGINE menu (placeholder until its phase rebuilds it)
std::unique_ptr<PhantomEffect> makeEffectOn (int u, int engineEntry);   // the same, starting on one ENGINE menu entry (the tests)

// A pass-through module carrying a unit's identity and controls, so the rack, UI, presets and automation are complete before the engine lands.
class PlaceholderEffect final : public PhantomEffect
{
public:
    explicit PlaceholderEffect (int unit) : u (unit) {}
    const char* key() const override { return unitDef (u).key; }
    const char* title() const override { return unitDef (u).title; }
    std::vector<ParamSpec> parameters() const override { return unitParams (u); }
    void prepare (double, int, int) override {}
    void reset() override {}
    void process (juce::dsp::AudioBlock<float>, const ModulationContext&) noexcept override {}
    int getLatencySamples() const override { return 0; }
    bool changesLevelOnPurpose() const override { return u == uOtt || u == uLim || u == uCrush; }
    bool isRhythmic() const override { return u == uGate || u == uGrid || u == uHaunt || u == uFilter || u == uEcho; }
    bool isNonlinear() const override { return (u >= uCrackle && u <= uBassLab) || u == uOtt || u == uLim; }
private:
    int u;
};
} // namespace phantom
