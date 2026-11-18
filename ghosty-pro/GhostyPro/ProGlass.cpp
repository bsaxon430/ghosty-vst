// GLASS (round 3): Ghosty Pro's theme-aware XY pad backdrop and the glass LIGHT opt-in (see ProGlass.h).
#include "ProGlass.h"
#include "ProTheme.h"

namespace progl
{
namespace
{
// Ghosty Pro paints its LIGHT look with the glass palette (also set in pt::Palette::make; this covers anything read before the first palette)
const bool kGlassOptIn = (ghosty::themes::optInGlassLight (true), true);

// a colour made readable on both ends of a vertical gradient
juce::Colour readableOnBoth (juce::Colour c, juce::Colour a, juce::Colour b, double ratio)
{
    for (int k = 0; k < 3; ++k) c = th::readableOn (th::readableOn (c, a, ratio), b, ratio);
    return c;
}

ghosty::XYGhostPad::Backdrop makePurple()
{
    ghosty::XYGhostPad::Backdrop b;
    const auto& h = th::get (th::Theme::Purple);
    b.top = juce::Colour (0xff372860); b.bottom = juce::Colour (0xff261a3f);          // the deep-purple panel family (window #3a2a5a .. lane #2e2046)
    b.grid = h.violet; b.gridAlpha = 0.16f;                                          // lighter violet grid
    b.rim = h.line.withAlpha (0.55f); b.edge = juce::Colours::white.withAlpha (0.10f);
    b.ghostOutline = juce::Colours::transparentBlack;
    b.glowAlpha = 0.42f; b.trailAlpha = 0.45f; b.rainAlpha = 1.0f;
    const auto top = b.top, bot = b.bottom;
    b.ink = [] (juce::Colour c) { return pt::remap (c, pt::darkPal(), pt::paletteFor (pt::Theme::Purple)); };
    b.label = [top, bot] (juce::Colour c) { return readableOnBoth (pt::remap (c, pt::darkPal(), pt::paletteFor (pt::Theme::Purple)), top, bot, 4.5); };
    return b;
}

ghosty::XYGhostPad::Backdrop makeLight()
{
    ghosty::XYGhostPad::Backdrop b;
    b.top = juce::Colour (0xffeef1f6); b.bottom = juce::Colour (0xffdbe1ea);         // a pale frosted glass screen
    b.grid = juce::Colour (0xff6f6a92); b.gridAlpha = 0.22f;                         // soft grey-violet grid
    b.rim = juce::Colour (0xffa9b1c0); b.edge = juce::Colours::white.withAlpha (0.95f);
    b.ghostOutline = juce::Colour (0xff2a3142).withAlpha (0.9f);                       // white ghosts keep an edge on the pale glass
    b.glowAlpha = 0.38f; b.trailAlpha = 0.7f; b.rainAlpha = 0.85f;
    const auto top = b.top, bot = b.bottom;
    // trails / glow / rain: the theme's role colour, deepened and saturated until it reads (>= 3:1) on the glass
    b.ink = [top, bot] (juce::Colour c)
    {
        auto t = pt::remap (c, pt::darkPal(), pt::paletteFor (pt::Theme::Light));
        if (t == c) t = juce::Colour::fromHSV (c.getHue(), juce::jmax (c.getSaturation(), 0.6f), juce::jmin (c.getBrightness(), 0.75f), c.getFloatAlpha());   // a neon not in the palette
        return readableOnBoth (t, top, bot, 3.0);
    };
    b.label = [top, bot] (juce::Colour c) { return readableOnBoth (pt::remap (c, pt::darkPal(), pt::paletteFor (pt::Theme::Light)), top, bot, 4.5); };
    return b;
}
} // namespace

const ghosty::XYGhostPad::Backdrop* padBackdrop (th::Theme t)
{
    static const auto purple = makePurple(), light = makeLight();
    if (t == th::Theme::Purple) return &purple;
    if (t == th::Theme::Light) return &light;
    return nullptr;     // BLACK: the original pad
}

void themePad (ghosty::XYGhostPad& pad)
{
    juce::ignoreUnused (kGlassOptIn);
    pad.backdrop = [] { return padBackdrop (pt::pal().theme); };
}
} // namespace progl
