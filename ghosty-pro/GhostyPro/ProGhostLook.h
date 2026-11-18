#pragma once
// GHOST LOOK: Ghosty Pro's 4th look, "GHOST" (the owner's liquid-glass LIGHT): a pale aurora backdrop under translucent frosted glass
// panels, pill glass buttons (lit = violet glass), glossy glass knobs with violet -> teal value arcs, recessed glass value boxes and
// wells, glass display screens, glass keys. Built on Shared/GhostGlass.h (the GHOST look of Ghost Slice).
//
// How it sits in Ghosty Pro: GHOST is the LIGHT theme (pt::Theme::Light, so every `progl::on (theme)` glass hook runs) with this flag on
// and the GHOST palette (pt::ghostPal(), ProTheme.h). The glass LIGHT materials (ProGlass.h: card / screen / knobFace / pill / well) hand
// over to the functions below while the flag is on; BLACK / PURPLE / LIGHT never set it, so they paint exactly as before. The shared
// theme enum is untouched: the look is stored as index 3 in the project's existing look field (older builds read it as BLACK).
//
// Header-only (the FX rack compiles ProGlass.h into Ghost Box too, where the flag is never set). Message thread only (painting).
// PERFORMANCE: no blur. The aurora is rendered once per size (cached image); the panel shadow + glass body, the knob cap and the screen
// body are cached per size + display scale; small parts are a handful of fills.
#include <juce_gui_basics/juce_gui_basics.h>
#include "GhostGlass.h"
#include "GhostArt.h"
#include <map>
#include <tuple>

namespace pghost
{
namespace gg = ghosty::glass;

// ---------------------------------------------------------------- the switch
inline bool& flag() { static bool f = false; return f; }       // set by pt::setLook (ProTheme.h); message thread
inline bool on() { return flag(); }
constexpr int kLook = 3;                                         // the look index (0 BLACK, 1 PURPLE, 2 LIGHT, 3 GHOST)
inline juce::String name() { return "GHOST"; }

// ---------------------------------------------------------------- tokens (text colours are in pt::ghostPal())
inline juce::Colour screenTop()    { return juce::Colour (0xffe9e5f6); }    // the glass display screen (opaque inset under the curves)
inline juce::Colour screenBottom() { return juce::Colour (0xffdcd6ee); }
inline juce::Colour arcFrom()      { return gg::violet(); }                  // knob value arcs: violet -> teal
inline juce::Colour arcTo()        { return juce::Colour (0xff14b8a6); }
inline juce::Colour arcTrack()     { return juce::Colour (0x38463e6c); }
// reference surfaces: the glass as it reads over the aurora at its darkest (text colours are made readable on all of them)
inline std::vector<juce::Colour> glassRefs()
{
    return { juce::Colour (0xffd9c9fb), juce::Colour (0xffd0dcf7), juce::Colour (0xffeee2f2), juce::Colour (0xffd8eee8), juce::Colour (0xffe6e1f4) };
}

// ---------------------------------------------------------------- image cache (per kind + size + scale)
struct Cache
{
    using Key = std::tuple<int, int, int, int, int>;     // kind, w, h, radius*4, scale*100
    std::map<Key, juce::Image> images;
    int builds = 0;
    template <typename Fn> const juce::Image& get (const Key& k, Fn&& make)
    {
        auto it = images.find (k);
        if (it != images.end()) return it->second;
        if (images.size() > 192) images.clear();
        ++builds;
        return images.emplace (k, make()).first->second;
    }
};
inline Cache& cache() { static Cache c; return c; }
inline float scaleOf (juce::Graphics& g) { return juce::jlimit (1.0f, 4.0f, g.getInternalContext().getPhysicalPixelScaleFactor()); }
inline juce::Image newImage (float w, float h, float pad, float s)
{
    return juce::Image (juce::Image::ARGB, juce::jmax (1, (int) std::ceil ((w + 2 * pad) * s)), juce::jmax (1, (int) std::ceil ((h + 2 * pad) * s)), true, juce::SoftwareImageType());
}
inline void blit (juce::Graphics& g, const juce::Image& img, juce::Rectangle<float> r, float pad, float s)
{
    g.drawImageTransformed (img, juce::AffineTransform::scale (1.0f / s).translated (std::round (r.getX()) - pad, std::round (r.getY()) - pad));
}

// ---------------------------------------------------------------- the aurora backdrop (one cached image per size)
inline gg::Backdrop& backdropCache() { static gg::Backdrop b; return b; }
inline void backdrop (juce::Graphics& g, int w, int h) { backdropCache().draw (g, w, h); }
// the part of a w x h aurora that lies under a child at `offset` (its position in the full window)
inline void backdropAt (juce::Graphics& g, int w, int h, juce::Point<int> offset)
{
    juce::Graphics::ScopedSaveState ss (g);
    g.setOrigin (-offset);
    backdropCache().draw (g, w, h);
}

// ---------------------------------------------------------------- a frosted glass panel (cached: shadow + translucent body)
inline void card (juce::Graphics& g, juce::Rectangle<float> r, float radius, juce::Colour accent, bool raised = true)
{
    const float s = scaleOf (g), spread = raised ? 12.0f : 0.0f, pad = raised ? std::ceil (spread * 1.6f) + 2.0f : 1.0f;
    const float w = std::round (r.getWidth()), h = std::round (r.getHeight());
    if (w <= 0 || h <= 0) return;
    const float rad = juce::jmax (radius, raised ? 14.0f : 0.0f);
    const auto& img = cache().get ({ raised ? 1 : 2, (int) w, (int) h, (int) std::round (rad * 4), (int) std::round (s * 100) }, [&]
    {
        auto im = newImage (w, h, pad, s);
        juce::Graphics ig (im); ig.addTransform (juce::AffineTransform::scale (s));
        const juce::Rectangle<float> b (pad, pad, w, h);
        if (raised) gg::panel (ig, b, rad, juce::Colours::white, 0.42f, false, spread);
        else gg::body (ig, b, rad, juce::Colours::white, 0.42f);
        return im;
    });
    blit (g, img, r, pad, s);
    if (! accent.isTransparent() && raised)
    {   // a whisper of the card's colour along the top (its title colour)
        g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.07f), 0, r.getY(), accent.withAlpha (0.0f), 0, r.getY() + 30, false));
        g.fillRoundedRectangle (r.reduced (1.5f), rad);
    }
}

// ---------------------------------------------------------------- a glass display screen (recessed, opaque so the curves read the same everywhere)
inline void screen (juce::Graphics& g, juce::Rectangle<float> r, float radius)
{
    const float s = scaleOf (g), pad = 2.0f, w = std::round (r.getWidth()), h = std::round (r.getHeight());
    if (w <= 0 || h <= 0) return;
    const auto& img = cache().get ({ 3, (int) w, (int) h, (int) std::round (radius * 4), (int) std::round (s * 100) }, [&]
    {
        auto im = newImage (w, h, pad, s);
        juce::Graphics ig (im); ig.addTransform (juce::AffineTransform::scale (s));
        const juce::Rectangle<float> b (pad, pad, w, h);
        juce::Path body; body.addRoundedRectangle (b, radius);
        ig.setGradientFill (juce::ColourGradient (screenTop(), 0, b.getY(), screenBottom(), 0, b.getBottom(), false));
        ig.fillPath (body);
        {
            juce::Graphics::ScopedSaveState ss (ig);
            ig.reduceClipRegion (body);
            // recessed: a soft violet shade under the top edge, a faint sheen across the upper half
            ig.setGradientFill (juce::ColourGradient (gg::shadowTint().withAlpha (0.13f), 0, b.getY(), gg::shadowTint().withAlpha (0.0f), 0, b.getY() + 9, false));
            ig.fillRect (b.withHeight (9));
            juce::Path sheen; sheen.addRoundedRectangle (b.reduced (3).withHeight (b.getHeight() * 0.40f), juce::jmax (0.0f, radius - 3));
            ig.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.30f), 0, b.getY(), juce::Colours::white.withAlpha (0.0f), 0, b.getY() + b.getHeight() * 0.40f, false));
            ig.fillPath (sheen);
        }
        ig.setColour (juce::Colour (0x50463e6c)); ig.strokePath (body, juce::PathStrokeType (1.0f));
        juce::Path lip; lip.addRoundedRectangle (b.expanded (0.6f), radius + 0.6f);     // the bright lip of the glass around the inset
        ig.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.0f), 0, b.getY(), juce::Colours::white.withAlpha (0.95f), 0, b.getBottom(), false));
        ig.strokePath (lip, juce::PathStrokeType (1.2f));
        return im;
    });
    blit (g, img, r, pad, s);
}

// ---------------------------------------------------------------- a glossy glass knob cap (cached per diameter)
inline void knobFace (juce::Graphics& g, float cx, float cy, float radius)
{
    const float s = scaleOf (g), d = std::round (radius * 2.0f), pad = 5.0f;
    if (d <= 0) return;
    const auto& img = cache().get ({ 4, (int) d, (int) d, 0, (int) std::round (s * 100) }, [&]
    {
        auto im = newImage (d, d, pad, s);
        juce::Graphics ig (im); ig.addTransform (juce::AffineTransform::scale (s));
        const juce::Rectangle<float> r (pad, pad, d, d);
        gg::shadow (ig, r, d * 0.5f, juce::jmax (2.0f, d * 0.12f), 1.4f, 4);
        ig.setGradientFill (juce::ColourGradient (juce::Colours::white, r.getCentreX() - d * 0.2f, r.getY() + 1, juce::Colour (0xffdcd4f3), r.getCentreX(), r.getBottom(), false));
        ig.fillEllipse (r);
        {   // a gloss highlight across the upper half
            juce::Graphics::ScopedSaveState ss (ig);
            juce::Path cap; cap.addEllipse (r); ig.reduceClipRegion (cap);
            const auto refl = juce::Rectangle<float> (r.getX() + d * 0.18f, r.getY() + d * 0.05f, d * 0.64f, d * 0.42f);
            ig.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.95f), 0, refl.getY(), juce::Colours::white.withAlpha (0.0f), 0, refl.getBottom(), false));
            ig.fillEllipse (refl);
        }
        ig.setColour (juce::Colour (0xc0ffffff)); ig.drawEllipse (r.reduced (0.7f), 1.3f);
        ig.setColour (gg::fringe()); ig.drawEllipse (r.reduced (d * 0.12f), 0.9f);
        ig.setColour (juce::Colour (0x40463e6c)); ig.drawEllipse (r.expanded (0.4f), 0.8f);
        return im;
    });
    blit (g, img, { cx - d * 0.5f, cy - d * 0.5f, d, d }, pad, s);
}

// the whole knob: glass track, violet -> teal value arc, the glossy cap, an ink pointer (hover = violet)
inline void knob (juce::Graphics& g, float cx, float cy, float rad, float a0, float a1, float ang, bool hover, float arcWidth = 4.0f)
{
    juce::Path track; track.addCentredArc (cx, cy, rad - 3, rad - 3, 0, a0, a1, true);
    g.setColour (arcTrack()); g.strokePath (track, juce::PathStrokeType (arcWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    juce::Path val; val.addCentredArc (cx, cy, rad - 3, rad - 3, 0, a0, ang, true);
    g.setGradientFill (juce::ColourGradient (arcFrom(), cx - rad, cy, arcTo(), cx + rad, cy, false));
    g.strokePath (val, juce::PathStrokeType (arcWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    knobFace (g, cx, cy, rad * 0.64f);
    juce::Path ptr; ptr.startNewSubPath (cx + std::sin (ang) * rad * 0.18f, cy - std::cos (ang) * rad * 0.18f); ptr.lineTo (cx + std::sin (ang) * rad * 0.52f, cy - std::cos (ang) * rad * 0.52f);
    g.setColour (hover ? gg::violetDeep() : juce::Colour (0xff6d4ae0)); g.strokePath (ptr, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// ---------------------------------------------------------------- pills, wells, value boxes
// a glass pill in r (no inset: the callers lay buttons out tightly); lit = violet glass (white text reads on it)
inline void pill (juce::Graphics& g, juce::Rectangle<float> r, float radius, bool lit, bool hover, bool down = false, bool enabled = true, juce::Colour litTint = gg::violetDeep())
{
    if (down) r = r.translated (0, 0.6f);
    const float rad = juce::jmin (radius, r.getHeight() * 0.5f, r.getWidth() * 0.5f);
    gg::shadow (g, r, rad, down ? 1.5f : 2.4f, enabled ? 1.2f : 0.6f, 3);
    if (lit) gg::body (g, r, rad, litTint, hover ? 0.95f : 0.91f, 0.3f);
    else     gg::body (g, r, rad, juce::Colours::white, hover ? 0.62f : 0.50f);
    if (! enabled) { g.setColour (juce::Colours::white.withAlpha (0.28f)); g.fillRoundedRectangle (r, rad); }
}
// a recessed glass well (combo box, text entry, preset display); outline = hover tint (transparent = none)
inline void well (juce::Graphics& g, juce::Rectangle<float> r, float radius, juce::Colour outline = juce::Colours::transparentBlack)
{
    radius = juce::jmin (radius, r.getHeight() * 0.5f);
    g.setColour (juce::Colour (0xd8f4f1fc)); g.fillRoundedRectangle (r, radius);
    gg::recessed (g, r, radius);
    if (! outline.isTransparent()) { g.setColour (outline.withAlpha (0.55f)); g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f); }
}
// a knob's value box: a recessed glass capsule
inline void valueBox (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (juce::Colour (0xc8f4f1fc)); g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    gg::recessed (g, r);
}

// ---------------------------------------------------------------- ghost characters stay legible on the glass
// an ink outline round a ghost drawn with ghosty::drawGhost (same transform): the background parade, the FLOW finger ghost
inline void outlineGhost (juce::Graphics& g, float cx, float cy, float size, float alpha, bool flip = false, float px = 1.1f)
{
    const float s = size / 118.0f;
    const auto t = juce::AffineTransform::translation (-130.0f, -106.0f).scaled (flip ? -s : s, s).translated (cx, cy);
    juce::Path p (ghosty::cuteBody()); p.applyTransform (t);
    g.setColour (gg::ink().withAlpha (juce::jlimit (0.0f, 1.0f, alpha)));
    g.strokePath (p, juce::PathStrokeType (px));
}

// (tests) skip the GHOST look's painted text (card titles, tab names) to read the pixels under it
inline bool& hideTextForTests() { static bool h = false; return h; }

// ---------------------------------------------------------------- Ghosty Pro only (ProGhostLook.cpp; never called in Ghost Box)
// a TextButton's background (ProLnF): white pill glass, lit = violet glass (accentRole 1 / 2 = lit in OSC 1 pink / OSC 2 blue glass)
void button (juce::Graphics&, juce::Rectangle<float> r, bool on, bool over, bool down, bool enabled, int accentRole, juce::Colour pink, juce::Colour blue);
// a linear slider: glass track, violet -> teal fill from `zeroProp` (0..1 of the track) to pos, a glossy thumb
void linearSlider (juce::Graphics&, juce::Rectangle<float> r, float pos, float zeroProp, bool enabled);
// a slider's value box (a Label inside a Slider): recessed glass + the label's text; false = not a value box (paint it as usual)
bool valueBoxLabel (juce::Graphics&, juce::Label&);
// a component that fills itself with the window colour: in GHOST it shows the aurora under it instead (false = not GHOST: fill as usual)
bool backdropUnder (juce::Graphics&, juce::Component&);
// the look button in GHOST (and LIGHT's, which names GHOST): a pill, a swatch of the next look, its name
void lookButton (juce::Graphics&, juce::Rectangle<float> r, int nextLook, bool over, bool down);
// STUDIO's on-screen keys in GHOST: frosted white keys, glossy indigo glass black keys
void whiteKey (juce::Graphics&, juce::Rectangle<float> area, bool down, bool over);
void blackKey (juce::Graphics&, juce::Rectangle<float> area, bool down, bool over);
// the shared BYPASS button, drawn as a glass pill in GHOST (amber glass when bypassed); every other look: the shared painting unchanged
void bypassPill (juce::Graphics&, juce::Rectangle<float> r, bool on, bool over, bool down);
} // namespace pghost
