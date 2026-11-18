// SPDX-License-Identifier: AGPL-3.0-or-later
// GHOST ATLAS - how the screen looks: the type, the colours each world lends, and the small drawing helpers.
//
// The screen is a chart of eight painted worlds, so its lettering follows a chart's: place names in an old italic
// (IM Fell English, the Fell types - sea names on charts are italic), headings in its small capitals, and every
// control and number in Atkinson Hyperlegible, chosen because it stays clear at the small sizes a plug-in needs.
// One colour never changes from world to world: the boat's lantern. Whatever is selected or switched on is lit by it.
#pragma once
#include "../core/Common.h"

namespace atlas::ui
{
constexpr int kBaseW = 1320, kBaseH = 720;      // the screen is laid out at this size and scaled as one piece

juce::Font fellItalic (float height);           // world names, the wordmark
juce::Font fellRoman (float height);            // preset names
juce::Font fellCaps (float height);             // headings (true small capitals)
juce::Font sans (float height);                 // controls and values
juce::Font sansBold (float height);

struct Look
{
    juce::Colour paper { 0xfff6eee2 };          // text
    juce::Colour lantern { 0xffffc56b };        // selected / switched on, in every world
    juce::Colour danger { 0xffff6b5e };
    juce::Colour accent { 0xff86ffb8 }, accent2 { 0xffa4aaff }, deep { 0xff14183f };   // lent by the world on screen
    juce::Colour text (float alpha = 1.0f) const { return paper.withAlpha (alpha); }
    juce::Colour ink() const { return deep.darker (0.55f); }      // lettering on a lit (lantern-coloured) surface
};
Look lookFor (int world);
Look& look();                                   // the world on screen now (the editor sets it)

// how names are written on screen: the controls are declared in capitals, the screen writes them as words
juce::String worded (const juce::String& s);    // "LOW PASS 24" -> "Low pass 24"
juce::String named (const juce::String& s);     // "HAUNTED HALLS" -> "Haunted Halls"

// ---- drawing
void drawFitted (juce::Graphics&, const juce::String&, juce::Rectangle<float>, juce::Justification, const juce::Font&, float minScale = 0.72f);
void softShadowText (juce::Graphics&, const juce::String&, juce::Rectangle<float>, juce::Justification, const juce::Font&, juce::Colour text, juce::Colour shadow);
void glow (juce::Graphics&, juce::Point<float> centre, float radius, juce::Colour colour, float strength);   // a soft round light
void heart (juce::Graphics&, juce::Rectangle<float>, bool filled, juce::Colour);
void chevron (juce::Graphics&, juce::Rectangle<float>, int direction, juce::Colour, float thickness = 1.8f);   // 0 left, 1 right, 2 down, 3 up

// ---- what the screen keeps for as long as a window is open
// Its typefaces, the small world pictures and the look of its menus. The editor holds one of these (through a
// juce::SharedResourcePointer, so every open window shares it), which means they are let go while JUCE is still running.
// They must not be statics: on the Mac a typeface made from memory signs itself out of a register inside JUCE when it is
// destroyed, and a static of ours can outlive that register. The process then stops with "mutex lock failed" as it
// exits - inside a host, that is the host crashing when it quits.
struct Kept
{
    Kept();
    ~Kept();
    std::array<juce::Typeface::Ptr, 5> faces;
    std::array<juce::Image, kWorlds + 1> thumbs;
    std::unique_ptr<juce::LookAndFeel> menus;
};

// ---- pictures
juce::Image loadPicture (const char* resourceName);                  // an embedded picture, decoded (not cached)
juce::Image worldThumb (int world);                                  // the small picture for the world grid (cached)
juce::Image blurred (const juce::Image& source, int downscale, int radius, int passes = 3);   // soft copy, same size as source

// menus, scroll bars and text boxes in the screen's own lettering
juce::LookAndFeel& chartLookAndFeel();
} // namespace atlas::ui
