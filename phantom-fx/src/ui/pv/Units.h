// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX painted unit visuals: every unit the designer has painted (DAWN, CEL, NEON), by unit number.
#pragma once
#include "UFatten.h"
#include "UCrystal.h"
#include "UEcho.h"
#include "UBatch1a.h"
#include "UFilter.h"
#include "UBatch1b.h"
#include "UBatch1c.h"
#include "UBatch2a.h"
#include "UBatch2b.h"
#include "UBatch2c.h"
#include "UGate.h"
#include "UGrid.h"
#include "UHaunt.h"
#include "UWarp.h"
#include "UCrackle.h"
#include "URetro.h"
#include "UEq.h"
#include "../../effects/Catalog.h"

namespace phantom::pv
{
inline std::unique_ptr<Scene> makeScene (int unit)
{
    switch (unit)
    {
        case uFatten: return std::make_unique<FattenScene>();
        case uCrystal: return std::make_unique<CrystalScene>();
        case uEcho: return std::make_unique<EchoScene>();
        case uChorus: return std::make_unique<ChorusScene>();
        case uFormant: return std::make_unique<FormantScene>();
        case uFilter: return std::make_unique<FilterScene>();
        case uOtt: return std::make_unique<SqueezeScene>();
        case uLim: return std::make_unique<LimitScene>();
        case uCrush: return std::make_unique<CrushScene>();
        case uTube: return std::make_unique<TubeScene>();
        case uWreck: return std::make_unique<WreckScene>();
        case uBassLab: return std::make_unique<BassScene>();
        case uSpace: return std::make_unique<SpaceScene>();
        case uShimmer: return std::make_unique<ShimmerScene>();
        case uHalls: return std::make_unique<HallsScene>();
        case uSpectra: return std::make_unique<SpectraScene>();
        case uPortal: return std::make_unique<DoorScene>();
        case uGate: return std::make_unique<GateScene>();
        case uGrid: return std::make_unique<GridScene>();
        case uHaunt: return std::make_unique<HauntScene>();
        case uWarp: return std::make_unique<WarpScene>();
        case uCrackle: return std::make_unique<CrackleScene>();
        case uRetro: return std::make_unique<RetroScene>();
        case uEq: return std::make_unique<EqScene>();
        default: return {};
    }
}
inline Look lookFor (const juce::String& style) { return style == "dawn" ? Look::dawn : style == "neon" ? Look::neon : Look::cel; }
} // namespace phantom::pv
