#pragma once
// TERRAIN colours: the topographic STYLE (thin rings, handles, tags, a tiny toolbar) is shared; the colours come from the
// plug-in that hosts the page. Nothing in the view is hard-coded: it reads this struct.
//   ground      the near-black ground
//   ring        contour rings when nothing is selected (and of the other mountains, dimmer: ringDim)
//   accent      the selected mountain's rings at its base, blending to accentTop at its summit
//   handle      the vertical handle lines, their dots, the value / width tags, the toolbar icons
//   onHandle    text drawn on a handle-coloured tag
//   valley      rings below the ground
//   button      the toolbar's dark rounded buttons (buttonHi when hovered / on)
//   text / dim  readouts and hints
#include <juce_graphics/juce_graphics.h>
#include <functional>

namespace ghost::terrain
{
struct Palette
{
    // the reference's "night" colourway, measured off its bundle: bg #0F0C07, line #DCD8D2, accent #FFCD28
    juce::Colour ground { 0xff0f0c07 };
    juce::Colour ring { 0xffdcd8d2 }, ringDim { 0x80dcd8d2 };
    juce::Colour accent { 0xffffcd28 }, accentTop { 0xffffcd28 };
    juce::Colour handle { 0xffffcd28 }, handleDim { 0x80ffcd28 }, onHandle { 0xff0f0c07 };
    juce::Colour valley { 0x80b8b0a0 };
    juce::Colour button { 0xff1c1813 }, buttonHi { 0xff2b261e };
    juce::Colour text { 0xffdcd8d2 }, dim { 0xff7a7467 };
    std::function<juce::Font (float height, bool bold)> font;   // empty = the system sans

    // the reference look (topographically.vercel.app): warm charcoal, cream rings, one amber
    static Palette amber() { return {}; }

    // Ghost Lab: charcoal, the selected mountain electric purple blending to acid green, handles / tags in acid green
    static Palette ghostLab()
    {
        Palette p;
        p.ground = juce::Colour (0xff121314);
        p.ring = juce::Colour (0xffd8d8dc); p.ringDim = juce::Colour (0x80c8c8d0);
        p.accent = juce::Colour (0xffa66bff); p.accentTop = juce::Colour (0xffa6f23a);
        p.handle = juce::Colour (0xffa6f23a); p.handleDim = juce::Colour (0x80a66bff); p.onHandle = juce::Colour (0xff111311);
        p.valley = juce::Colour (0x80a66bff);
        p.button = juce::Colour (0xff1c1d20); p.buttonHi = juce::Colour (0xff2a2b2e);
        p.text = juce::Colour (0xfff2f2f3); p.dim = juce::Colour (0xff9a9ca2);
        return p;
    }

    // Ghost Slice: its deep indigo, the selected mountain violet blending to pink, handles / tags in pink
    static Palette ghostSlice()
    {
        Palette p;
        p.ground = juce::Colour (0xff0b0a1f);
        p.ring = juce::Colour (0xffc9c3e6); p.ringDim = juce::Colour (0x80c9c3e6);
        p.accent = juce::Colour (0xffa855f7); p.accentTop = juce::Colour (0xffff9cf2);
        p.handle = juce::Colour (0xffff9cf2); p.handleDim = juce::Colour (0x80a855f7); p.onHandle = juce::Colour (0xff0b0a1f);
        p.valley = juce::Colour (0x8022d3ee);
        p.button = juce::Colour (0xff17153a); p.buttonHi = juce::Colour (0xff221f4f);
        p.text = juce::Colour (0xfff5f3ff); p.dim = juce::Colour (0xffa9a3cc);
        return p;
    }

    // Ghost Bass: the GhostBlock indigo, the selected mountain cyan blending to slime green, handles / tags in cyan
    static Palette ghostBass()
    {
        Palette p;
        p.ground = juce::Colour (0xff0b0a1f);
        p.ring = juce::Colour (0xffc9c3e6); p.ringDim = juce::Colour (0x80c9c3e6);
        p.accent = juce::Colour (0xff22d3ee); p.accentTop = juce::Colour (0xff5dff8a);
        p.handle = juce::Colour (0xff22d3ee); p.handleDim = juce::Colour (0x8022d3ee); p.onHandle = juce::Colour (0xff0b0a1f);
        p.valley = juce::Colour (0x80a855f7);
        p.button = juce::Colour (0xff17153a); p.buttonHi = juce::Colour (0xff221f4f);
        p.text = juce::Colour (0xfff5f3ff); p.dim = juce::Colour (0xffa9a3cc);
        return p;
    }
};
}
