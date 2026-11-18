// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX: what the ported Ghost Box look code (FxUi.h) needs to know about the units, served from the catalog.
#pragma once
#include "../effects/Catalog.h"
#ifndef GPFX_BOX
 #define GPFX_BOX 1
#endif
namespace gpfx
{
// Ghosty Pro's GLASS materials are not part of Phantom FX (glass() is always false here); the calls stay as no-ops so FxUi.h is untouched
namespace progl { template <class... A> inline void screen (A&&...) {} template <class... A> inline void knobFace (A&&...) {} template <class... A> inline void pill (A&&...) {} template <class... A> inline void well (A&&...) {} }
using phantom::kNumUnits; using phantom::gRhythm; using phantom::gColour; using phantom::gSpace; using phantom::gMaster;
using phantom::uGate; using phantom::uGrid; using phantom::uHaunt; using phantom::uWarp; using phantom::uCrackle; using phantom::uWreck; using phantom::uTube; using phantom::uCrush;
using phantom::uRetro; using phantom::uFatten; using phantom::uBassLab; using phantom::uFormant; using phantom::uEq; using phantom::uFilter; using phantom::uChorus; using phantom::uCrystal;
using phantom::uEcho; using phantom::uSpace; using phantom::uShimmer; using phantom::uHalls; using phantom::uSpectra; using phantom::uPortal; using phantom::uOtt; using phantom::uLim;
constexpr int uSplit = -1;   // GHOST SPLIT was dropped (DECISIONS.md); the order tips that named it never match
struct UnitInfo { const char* key; const char* title; const char* shortName; const char* blurb; int group; juce::uint32 accent; int look; bool hasMix; };
inline const UnitInfo& unitInfo (int u)
{
    static UnitInfo k[kNumUnits]; static bool init = false;
    if (! init) { for (int i = 0; i < kNumUnits; ++i) { const auto& d = phantom::unitDef (i); k[i] = { d.key, d.title, d.shortName, d.blurb, d.group, d.accent, d.look, d.hasMix }; } init = true; }
    return k[juce::jlimit (0, kNumUnits - 1, u)];
}
inline const char* groupName (int g) { return phantom::groupName (g); }
inline float clampf (float v, float lo, float hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }
inline float gainToDb (float g) noexcept { return 20.0f * std::log10 (juce::jmax (1.0e-9f, g)); }
}
