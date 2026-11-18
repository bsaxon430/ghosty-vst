#pragma once
// GLASS (round 3): Ghosty Pro's "liquid glass" LIGHT look: the materials. Header-only, message thread only (painting).
//
// Everything here paints ONLY while Ghosty Pro's LIGHT look is on with the glass opt-in (progl::on()); BLACK and PURPLE never call it
// (every hook is `if (progl::on (theme)) ... else <the original code>`), and the other plugins never set the opt-in (Shared/GhostThemes.h).
// The FX rack (GhostyPro/fx, also compiled into Ghost Box) includes this header too: in Ghost Box glassLight() is false, so nothing changes.
//
//   card (g, r, radius, accent)   a raised frosted panel: drop shadow, translucent-looking frosted fill (opaque pixels: a vertical gradient
//                                 over the panel colour), a 1 px bright top edge, a soft inner shadow at the bottom, a faint specular
//                                 sweep, the accent's title tint and a fine rim
//   screen (g, r, radius)         a light frosted display, a slightly darker inset than the panels, so glowing curves (deepened) still read
//   knobFace (g, cx, cy, r)       a glassy knob cap: rim highlight, a soft reflection, a contact shadow
//   pill (g, r, radius, lit, accent, hover)   a glass pill button / toggle; lit = filled in the accent colour
//
// Cheap to paint: no real-time blur. The shadow + body of a card, the knob cap and the screen body are rendered once per size (and
// per display scale) into cached images; a new size = a new entry (= invalidated on resize); the cache is cleared when it grows past 128.
#include <juce_gui_basics/juce_gui_basics.h>
#include "GhostThemes.h"
#include "GhostUI.h"
#include "ProGhostLook.h"   // GHOST LOOK: while the 4th look is on, the materials below hand over to its glass (pghost::)
#include <map>
#include <tuple>

namespace progl
{
namespace th = ghosty::themes;
inline bool on (th::Theme t) { return t == th::Theme::Light && th::glassLight(); }

// the tones of the glass look (derived from the glass LIGHT palette)
struct Tones
{
    juce::Colour bodyTop { 0xfff8f9fb }, bodyBottom { 0xffeaedf2 };   // frosted panel gradient (opaque)
    juce::Colour rim { 0xffc3c9d4 };                                  // fine outer rim
    juce::Colour highlight { 0xffffffff };                            // top edge / specular (used with alpha)
    juce::Colour innerShade { 0xff283046 };                           // inner bottom shadow / drop shadow (used with alpha)
    juce::Colour screenTop { 0xffe2e7ef }, screenBottom { 0xffd2d9e3 }, screenRim { 0xffa9b1c0 };
    juce::Colour knobTop { 0xfffbfcfd }, knobBottom { 0xffdde1e8 };
};
inline const Tones& tones() { static const Tones t; return t; }

// ---------------------------------------------------------------- image cache (per size + scale)
struct Cache
{
    using Key = std::tuple<int, int, int, int, int>;    // kind, w, h, radius*4, scale*100
    std::map<Key, juce::Image> images;
    template <typename Fn> const juce::Image& get (const Key& k, Fn&& make)
    {
        auto it = images.find (k);
        if (it != images.end()) return it->second;
        if (images.size() > 128) images.clear();
        return images.emplace (k, make()).first->second;
    }
};
inline Cache& cache() { static Cache c; return c; }

inline float scaleOf (juce::Graphics& g) { return juce::jlimit (0.5f, 4.0f, g.getInternalContext().getPhysicalPixelScaleFactor()); }

// draws a cached image made at scale s with `pad` logical px around rect r
inline void blit (juce::Graphics& g, const juce::Image& img, juce::Rectangle<float> r, float pad, float s)
{
    g.drawImageTransformed (img, juce::AffineTransform::scale (1.0f / s).translated (r.getX() - pad, r.getY() - pad));
}

// ---------------------------------------------------------------- a raised frosted card
inline juce::Image makeCardImage (float w, float h, float radius, float s, float pad, bool raised)
{
    const auto& T = tones();
    juce::Image img (juce::Image::ARGB, juce::jmax (1, (int) std::ceil ((w + 2 * pad) * s)), juce::jmax (1, (int) std::ceil ((h + 2 * pad) * s)), true);
    juce::Graphics g (img);
    g.addTransform (juce::AffineTransform::scale (s));
    const juce::Rectangle<float> r (pad, pad, w, h);
    juce::Path body; body.addRoundedRectangle (r, radius);
    if (raised)
    {   // soft drop shadow (blurred once, here)
        juce::DropShadow (T.innerShade.withAlpha (0.16f), 12, { 0, 4 }).drawForPath (g, body);
        juce::DropShadow (T.innerShade.withAlpha (0.10f), 3, { 0, 1 }).drawForPath (g, body);
    }
    // frosted fill: opaque vertical gradient
    g.setGradientFill (juce::ColourGradient (T.bodyTop, 0, r.getY(), T.bodyBottom, 0, r.getBottom(), false));
    g.fillPath (body);
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (body);
        // faint specular sweep: a diagonal band across the upper left
        juce::Path sweep;
        sweep.startNewSubPath (r.getX(), r.getY());
        sweep.lineTo (r.getX() + juce::jmin (r.getWidth() * 0.62f, r.getHeight() * 1.6f), r.getY());
        sweep.lineTo (r.getX() + juce::jmin (r.getWidth() * 0.30f, r.getHeight() * 0.8f), r.getY() + r.getHeight() * 0.62f);
        sweep.lineTo (r.getX(), r.getY() + r.getHeight() * 0.70f);
        sweep.closeSubPath();
        juce::ColourGradient sg (T.highlight.withAlpha (0.42f), r.getX(), r.getY(), T.highlight.withAlpha (0.0f), r.getX() + r.getWidth() * 0.34f, r.getY() + r.getHeight() * 0.42f, false);
        g.setGradientFill (sg); g.fillPath (sweep);
        // soft inner shadow at the bottom
        const float sh = juce::jmin (16.0f, r.getHeight() * 0.25f);
        g.setGradientFill (juce::ColourGradient (T.innerShade.withAlpha (0.0f), 0, r.getBottom() - sh, T.innerShade.withAlpha (0.07f), 0, r.getBottom(), false));
        g.fillRect (r.withTop (r.getBottom() - sh));
    }
    // 1 px bright top edge (fading down the sides)
    juce::Path edge; edge.addRoundedRectangle (r.reduced (1.0f), juce::jmax (0.0f, radius - 1.0f));
    g.setGradientFill (juce::ColourGradient (T.highlight.withAlpha (0.95f), 0, r.getY(), T.highlight.withAlpha (0.0f), 0, r.getY() + juce::jmax (radius * 1.6f, 12.0f), false));
    g.strokePath (edge, juce::PathStrokeType (1.0f));
    // fine rim
    g.setColour (T.rim.withAlpha (0.85f)); g.strokePath (body, juce::PathStrokeType (1.0f));
    return img;
}

inline void card (juce::Graphics& g, juce::Rectangle<float> r, float radius, juce::Colour accent, bool raised = true)
{
    if (pghost::on()) { pghost::card (g, r, radius, accent, raised); return; }     // GHOST LOOK
    const float s = scaleOf (g), pad = raised ? 18.0f : 2.0f;
    const auto& img = cache().get ({ raised ? 1 : 2, (int) std::round (r.getWidth()), (int) std::round (r.getHeight()), (int) std::round (radius * 4), (int) std::round (s * 100) },
                                   [&] { return makeCardImage (std::round (r.getWidth()), std::round (r.getHeight()), radius, s, pad, raised); });
    blit (g, img, r, pad, s);
    if (! accent.isTransparent())
    {   // the accent's title tint (as the original card) and a lit rim along the top
        g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.08f), 0, r.getY(), accent.withAlpha (0.0f), 0, r.getY() + 34, false));
        g.fillRoundedRectangle (r, radius);
        g.setColour (accent.withAlpha (0.22f)); g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
    }
}

// ---------------------------------------------------------------- a light frosted display screen (inset)
inline juce::Image makeScreenImage (float w, float h, float radius, float s)
{
    const auto& T = tones();
    const float pad = 2.0f;
    juce::Image img (juce::Image::ARGB, juce::jmax (1, (int) std::ceil ((w + 2 * pad) * s)), juce::jmax (1, (int) std::ceil ((h + 2 * pad) * s)), true);
    juce::Graphics g (img);
    g.addTransform (juce::AffineTransform::scale (s));
    const juce::Rectangle<float> r (pad, pad, w, h);
    juce::Path body; body.addRoundedRectangle (r, radius);
    g.setGradientFill (juce::ColourGradient (T.screenTop, 0, r.getY(), T.screenBottom, 0, r.getBottom(), false));
    g.fillPath (body);
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (body);
        // inset: a soft shade under the top edge (the screen sits below the panel), a faint sheen across the upper half
        g.setGradientFill (juce::ColourGradient (T.innerShade.withAlpha (0.07f), 0, r.getY(), T.innerShade.withAlpha (0.0f), 0, r.getY() + 8, false));
        g.fillRect (r.withHeight (8));
        juce::Path sheen; sheen.addRoundedRectangle (r.reduced (3).withHeight (r.getHeight() * 0.42f), juce::jmax (0.0f, radius - 3));
        g.setGradientFill (juce::ColourGradient (T.highlight.withAlpha (0.22f), 0, r.getY(), T.highlight.withAlpha (0.0f), 0, r.getY() + r.getHeight() * 0.42f, false));
        g.fillPath (sheen);
    }
    g.setColour (T.screenRim.withAlpha (0.9f)); g.strokePath (body, juce::PathStrokeType (1.0f));
    juce::Path lip; lip.addRoundedRectangle (r.expanded (0.5f), radius + 0.5f);      // the bright lip of the panel around the inset
    g.setGradientFill (juce::ColourGradient (T.highlight.withAlpha (0.0f), 0, r.getY(), T.highlight.withAlpha (0.9f), 0, r.getBottom(), false));
    g.strokePath (lip, juce::PathStrokeType (1.0f));
    return img;
}
inline void screen (juce::Graphics& g, juce::Rectangle<float> r, float radius)
{
    if (pghost::on()) { pghost::screen (g, r, radius); return; }     // GHOST LOOK
    const float s = scaleOf (g);
    const auto& img = cache().get ({ 3, (int) std::round (r.getWidth()), (int) std::round (r.getHeight()), (int) std::round (radius * 4), (int) std::round (s * 100) },
                                   [&] { return makeScreenImage (std::round (r.getWidth()), std::round (r.getHeight()), radius, s); });
    blit (g, img, r, 2.0f, s);
}

// ---------------------------------------------------------------- a glassy knob cap
inline juce::Image makeKnobImage (float d, float s)
{
    const auto& T = tones();
    const float pad = 4.0f;
    juce::Image img (juce::Image::ARGB, juce::jmax (1, (int) std::ceil ((d + 2 * pad) * s)), juce::jmax (1, (int) std::ceil ((d + 2 * pad) * s)), true);
    juce::Graphics g (img);
    g.addTransform (juce::AffineTransform::scale (s));
    const juce::Rectangle<float> r (pad, pad, d, d);
    juce::Path cap; cap.addEllipse (r);
    juce::DropShadow (T.innerShade.withAlpha (0.22f), 4, { 0, 2 }).drawForPath (g, cap);       // contact shadow
    g.setGradientFill (juce::ColourGradient (T.knobTop, 0, r.getY(), T.knobBottom, 0, r.getBottom(), false));
    g.fillPath (cap);
    {   // soft reflection: an ellipse of light across the upper half
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (cap);
        auto refl = juce::Rectangle<float> (r.getX() + d * 0.16f, r.getY() + d * 0.06f, d * 0.68f, d * 0.46f);
        g.setGradientFill (juce::ColourGradient (T.highlight.withAlpha (0.85f), 0, refl.getY(), T.highlight.withAlpha (0.0f), 0, refl.getBottom(), false));
        g.fillEllipse (refl);
        g.setGradientFill (juce::ColourGradient (T.innerShade.withAlpha (0.0f), 0, r.getCentreY(), T.innerShade.withAlpha (0.10f), 0, r.getBottom(), false));
        g.fillEllipse (r);
    }
    // glassy rim: bright at the top, darker at the bottom
    g.setGradientFill (juce::ColourGradient (T.highlight, 0, r.getY(), T.rim.darker (0.15f), 0, r.getBottom(), false));
    g.strokePath (cap, juce::PathStrokeType (1.2f));
    g.setColour (T.rim.withAlpha (0.55f)); g.drawEllipse (r.expanded (0.6f), 0.8f);
    return img;
}
inline void knobFace (juce::Graphics& g, float cx, float cy, float radius)
{
    if (pghost::on()) { pghost::knobFace (g, cx, cy, radius); return; }     // GHOST LOOK
    const float s = scaleOf (g), d = std::round (radius * 2.0f);
    const auto& img = cache().get ({ 4, (int) d, (int) d, 0, (int) std::round (s * 100) }, [&] { return makeKnobImage (d, s); });
    blit (g, img, { cx - d * 0.5f, cy - d * 0.5f, d, d }, 4.0f, s);
}

// ---------------------------------------------------------------- a glass pill (button / toggle)
inline void pill (juce::Graphics& g, juce::Rectangle<float> r, float radius, bool lit, juce::Colour accent, bool hover)
{
    if (pghost::on()) { pghost::pill (g, r, radius, lit, hover); return; }     // GHOST LOOK (lit = violet glass)
    const auto& T = tones();
    if (lit)
    {
        g.setColour (accent.withAlpha (0.20f)); g.fillRoundedRectangle (r.expanded (1.5f).translated (0, 1), radius + 1.5f);   // soft lit glow
        g.setGradientFill (juce::ColourGradient (accent, 0, r.getY(), accent.darker (0.22f), 0, r.getBottom(), false));
        g.fillRoundedRectangle (r, radius);
        // glass gloss over the colour: the upper half lifted
        g.setGradientFill (juce::ColourGradient (T.highlight.withAlpha (0.10f), 0, r.getY(), T.highlight.withAlpha (0.0f), 0, r.getCentreY(), false));
        g.fillRoundedRectangle (r.reduced (1.0f).withHeight (r.getHeight() * 0.5f), juce::jmax (0.0f, radius - 1.0f));
        g.setColour (accent.darker (0.35f)); g.drawRoundedRectangle (r, radius, 1.0f);
        return;
    }
    g.setColour (T.innerShade.withAlpha (0.10f)); g.fillRoundedRectangle (r.translated (0, 1.0f), radius);            // contact shadow
    g.setGradientFill (juce::ColourGradient (T.knobTop, 0, r.getY(), T.knobBottom, 0, r.getBottom(), false));
    g.fillRoundedRectangle (r, radius);
    g.setGradientFill (juce::ColourGradient (T.highlight.withAlpha (0.9f), 0, r.getY(), T.highlight.withAlpha (0.0f), 0, r.getY() + juce::jmin (10.0f, r.getHeight() * 0.5f), false));
    g.drawRoundedRectangle (r.reduced (1.0f), juce::jmax (0.0f, radius - 1.0f), 1.0f);
    g.setColour (hover ? accent : T.rim); g.drawRoundedRectangle (r, radius, 1.0f);
}

// a glass inset field (combo box / text entry): a slightly recessed frosted well
inline void well (juce::Graphics& g, juce::Rectangle<float> r, float radius, juce::Colour fill, juce::Colour outline)
{
    if (pghost::on()) { juce::ignoreUnused (fill, outline); pghost::well (g, r, radius); return; }     // GHOST LOOK
    const auto& T = tones();
    g.setColour (fill); g.fillRoundedRectangle (r, radius);
    juce::Graphics::ScopedSaveState ss (g);
    juce::Path p; p.addRoundedRectangle (r, radius); g.reduceClipRegion (p);
    g.setGradientFill (juce::ColourGradient (T.innerShade.withAlpha (0.06f), 0, r.getY(), T.innerShade.withAlpha (0.0f), 0, r.getY() + 5, false));
    g.fillRect (r.withHeight (5));
    g.setColour (outline); g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
}
// ---------------------------------------------------------------- Ghosty Pro only (ProGlass.cpp; not built into Ghost Box)
// the XY ghost pad follows the theme: BLACK = the original black pad (no backdrop), PURPLE = deep purple + light violet grid, LIGHT = pale glass
void themePad (ghosty::XYGhostPad& pad);
// the pad's backdrop for a theme (nullptr for BLACK) - also read by the contrast tests
const ghosty::XYGhostPad::Backdrop* padBackdrop (th::Theme t);
} // namespace progl
