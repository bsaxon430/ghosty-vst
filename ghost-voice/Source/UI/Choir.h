// Ghost Voice - AGPL-3.0-only (see LICENSE, NOTICE.md).
//
// THE GHOST CHOIR: the look of Ghost Voice. A cathedral of spectral singers: stained-glass light, rows of hooded ghost voices in
// choir stalls (each chop / key is a singer who lights up and opens its mouth when it sounds), organ-pipe meters, candle smoke and
// hymn-book type (Crimson Pro + Gloock, both SIL OFL 1.1, embedded: see ChoirFonts.h, defined in Editor.cpp).
//
// DARK = "Vespers": night stone, gold leaf, glass glowing from candle light. LIGHT = "Matins": limestone and parchment, sunlight
// through the glass, iron-gall ink. Everything here is drawing only (message thread). Static art is cached in Images by the
// callers (choir::Cache), so a repaint is a blit plus what moves.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "GhostThemes.h"
#include <cmath>
#include <functional>
#include <map>

namespace gv::choir
{
// ================================================================================================ fonts (Editor.cpp)
enum Style { regular = 0, bold = 1, italic = 2 };
juce::Font serif (float h, int style = regular);   // Crimson Pro: every word of the UI
juce::Font display (float h);                      // Gloock: titles, tabs, big buttons
inline float width (const juce::Font& f, const juce::String& s)
{
    juce::GlyphArrangement ga; ga.addLineOfText (f, s, 0.0f, 0.0f);
    return ga.getBoundingBox (0, -1, true).getWidth();
}
// the largest size <= h at which s fits w (never below lo). Remembered per (style, text, w, h): paint calls it every frame.
inline float fit (int style, const juce::String& s, float w, float h, float lo = 8.0f)
{
    static std::map<juce::String, float> memo;
    const auto key = juce::String (style) + "|" + juce::String (juce::roundToInt (w)) + "|" + juce::String (h, 1) + "|" + s;
    if (auto it = memo.find (key); it != memo.end()) return it->second;
    float x = h;
    while (x > lo && width (style < 0 ? display (x) : serif (x, style), s) > w) x -= 0.5f;
    if (memo.size() > 4000) memo.clear();
    memo[key] = x;
    return x;
}
inline juce::Font fitted (int style, const juce::String& s, float w, float h, float lo = 8.0f)
{ const float x = fit (style, s, w, h, lo); return style < 0 ? display (x) : serif (x, style); }
constexpr int disp = -1;   // fit()'s style for Gloock

// ================================================================================================ the palette
struct Pal
{
    bool dark = true;
    juce::Colour bg0, bg1, panel, panelHi, card, cell, edge, edgeDim, ink, dim, faint, gold, goldHi, onGold;
    juce::Colour glass[8];   // ruby, sapphire, emerald, amber, amethyst, rose, teal, citrine
    juce::Colour ghost, ghostInk, glow, play, select, good, warn, bad, waveBg, waveInk, keyWhite, keyBlack, shadow, lead;
    juce::Colour voiceA() const { return glass[1]; }
    juce::Colour voiceB() const { return glass[0]; }
    juce::Colour voiceC() const { return glass[4]; }
    juce::Colour chop (int i) const { static const int order[] { 0, 1, 3, 2, 4, 6, 5, 7 }; return glass[order[((i % 8) + 8) % 8]]; }
};

inline const Pal& pal (bool dark)
{
    static const Pal D = []
    {
        Pal p; p.dark = true; using C = juce::Colour;
        p.bg0 = C (0xff0a0911); p.bg1 = C (0xff17121f); p.panel = C (0xff16121e); p.panelHi = C (0xff1f1a2a); p.card = C (0xff241e31); p.cell = C (0xff2c2539);
        p.edge = C (0xffb8954a); p.edgeDim = C (0x70b8954a); p.ink = C (0xfff2eadb); p.dim = C (0xffb9ae98); p.faint = C (0xff7f7667);
        p.gold = C (0xffd9b45a); p.goldHi = C (0xfff6dd95); p.onGold = C (0xff1b1309);
        const juce::uint32 g[] { 0xffd83a55, 0xff4a7ef0, 0xff34b87c, 0xffeaa83a, 0xffa463ea, 0xffe86aa2, 0xff38b8c8, 0xffd9d25a };
        for (int i = 0; i < 8; ++i) p.glass[i] = C (g[i]);
        p.ghost = C (0xffe9f1ff); p.ghostInk = C (0xff17131f); p.glow = C (0xffb0dcff); p.play = C (0xffffd27a); p.select = C (0xffbf98ff);
        p.good = C (0xff74d9a0); p.warn = C (0xffecb54e); p.bad = C (0xffff7070); p.waveBg = C (0xff0d0b15); p.waveInk = C (0xffdfe8ff);
        p.keyWhite = C (0xffe8dfca); p.keyBlack = C (0xff1a1521); p.shadow = C (0x99000000); p.lead = C (0xff050408);
        return p;
    }();
    static const Pal L = []
    {
        Pal p; p.dark = false; using C = juce::Colour;
        p.bg0 = C (0xfff1eadb); p.bg1 = C (0xffe3d8c2); p.panel = C (0xfff8f3e8); p.panelHi = C (0xfffcf8f0); p.card = C (0xfffffbf3); p.cell = C (0xffece3d1);
        p.edge = C (0xff9a7226); p.edgeDim = C (0x809a7226); p.ink = C (0xff2a2017); p.dim = C (0xff5c4f40); p.faint = C (0xff8e8170);
        p.gold = C (0xffa67b22); p.goldHi = C (0xffc89a3c); p.onGold = C (0xfffffbf3);
        const juce::uint32 g[] { 0xffb22442, 0xff2453bc, 0xff1b8a5a, 0xffbf7a12, 0xff763bc2, 0xffc03e7e, 0xff1a8898, 0xff98920e };
        for (int i = 0; i < 8; ++i) p.glass[i] = C (g[i]);
        p.ghost = C (0xffffffff); p.ghostInk = C (0xff3a3046); p.glow = C (0xff5f9fe0); p.play = C (0xffc27800); p.select = C (0xff7438c0);
        p.good = C (0xff1f8a50); p.warn = C (0xffa86c00); p.bad = C (0xffc0302a); p.waveBg = C (0xfff4eee1); p.waveInk = C (0xff2c2440);
        p.keyWhite = C (0xfffffbf3); p.keyBlack = C (0xff2a2017); p.shadow = C (0x40503a20); p.lead = C (0xff3a3026);
        return p;
    }();
    return dark ? D : L;
}

// the GhostBlock theme tokens built from ours (the shared Spirit Board and the host-facing pal() use these)
inline const ghosty::themes::Palette& themePal (bool dark)
{
    auto make = [] (bool d)
    {
        const auto& c = pal (d);
        ghosty::themes::Palette p = ghosty::themes::get (d ? ghosty::themes::Theme::Dark : ghosty::themes::Theme::Light);
        p.isLight = ! d; p.window = c.bg0; p.windowTop = c.bg1; p.windowBottom = c.bg0; p.panel = c.panel; p.card = c.card; p.cell = c.cell; p.line = c.edgeDim.withAlpha (1.0f);
        p.ink = c.ink; p.dim = c.dim; p.onAccent = c.onGold; p.overlay = d ? juce::Colours::white : juce::Colours::black;
        p.accent1 = c.glass[5]; p.accent2 = c.glass[1]; p.accent3 = c.glass[2]; p.violet = c.glass[4]; p.good = c.good; p.warn = c.warn; p.bad = c.bad;
        p.keyWhite = c.keyWhite; p.keyBlack = c.keyBlack; p.waveBg = c.waveBg; p.waveLo = c.glass[1]; p.waveHi = c.glass[4]; p.shadow = c.shadow;
        return p;
    };
    static const auto D = make (true), L = make (false);
    return dark ? D : L;
}

// ================================================================================================ a cached image of static art
// (fix-blur) Caches are rendered at the PHYSICAL pixel scale of the context they are drawn into (2 on a Retina Mac, the host's
// plug-in zoom included), not at 1x logical size: a 1x image blown up to 2x is what made the text look soft. The scale is part
// of the cache key, so moving the window to another display (or changing the host zoom) rebuilds it.
inline float pixelScale (juce::Graphics& g)
{
    const float s = g.getInternalContext().getPhysicalPixelScaleFactor();
    return std::isfinite (s) ? juce::jlimit (1.0f, 4.0f, s) : 1.0f;
}
// draws an image made at `scale` x its logical size at logical (x, y); a pure blit when the device scale matches
inline void drawScaled (juce::Graphics& g, const juce::Image& img, float scale, int x, int y)
{
    g.setOpacity (1.0f);   // (drawImage uses the current colour's alpha)
    if (scale == 1.0f) { g.drawImageAt (img, x, y); return; }
    g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
    g.drawImageTransformed (img, juce::AffineTransform::scale (1.0f / scale).translated ((float) x, (float) y));
}
struct Cache
{
    juce::Image img; juce::String key; float scale = 0.0f;
    // draws the cached image at (x, y); rebuilds it with `draw` when the key, the size or the display scale changed
    template <typename F> void draw (juce::Graphics& g, const juce::String& k, juce::Rectangle<int> r, F&& paintIt, bool opaque = false)
    {
        if (r.isEmpty()) return;
        const float s = pixelScale (g);
        const int pw = juce::roundToInt ((float) r.getWidth() * s), ph = juce::roundToInt ((float) r.getHeight() * s);
        if (k != key || s != scale || ! img.isValid() || img.getWidth() != pw || img.getHeight() != ph)
        {
            key = k; scale = s;
            img = juce::Image (opaque ? juce::Image::RGB : juce::Image::ARGB, pw, ph, true);
            juce::Graphics ig (img);
            if (s != 1.0f) ig.addTransform (juce::AffineTransform::scale (s));
            paintIt (ig, r.withZeroOrigin());
        }
        drawScaled (g, img, s, r.getX(), r.getY());
    }
    void clear() { key = {}; img = {}; scale = 0.0f; }
};

// ================================================================================================ drawing pieces
inline float hash01 (int a, int b = 0) { juce::uint32 h = (juce::uint32) (a * 73856093) ^ (juce::uint32) (b * 19349663); h ^= h >> 13; h *= 0x5bd1e995; h ^= h >> 15; return (float) (h & 0xffff) / 65535.0f; }

// a pointed (lancet) arch: straight sides from the bottom up to `spring` (0..1 of the height), then two arcs to the point
inline juce::Path lancet (juce::Rectangle<float> r, float spring = 0.42f)
{
    juce::Path p;
    const float sy = r.getY() + r.getHeight() * spring;
    p.startNewSubPath (r.getX(), r.getBottom());
    p.lineTo (r.getX(), sy);
    p.quadraticTo (r.getX(), r.getY() + (sy - r.getY()) * 0.25f, r.getCentreX(), r.getY());
    p.quadraticTo (r.getRight(), r.getY() + (sy - r.getY()) * 0.25f, r.getRight(), sy);
    p.lineTo (r.getRight(), r.getBottom());
    p.closeSubPath();
    return p;
}

// a hymn-book panel: a page with a gilded double rule and small corner fleurons
inline void page (juce::Graphics& g, const Pal& P, juce::Rectangle<float> r, juce::Colour glow = {})
{
    g.setColour (P.shadow); g.fillRoundedRectangle (r.translated (0, 3), 6.0f);
    juce::ColourGradient gr (P.panelHi, r.getX(), r.getY(), P.panel, r.getX(), r.getBottom(), false);
    g.setGradientFill (gr); g.fillRoundedRectangle (r, 6.0f);
    if (! glow.isTransparent()) { g.setColour (glow.withAlpha (0.10f)); g.fillRoundedRectangle (r, 6.0f); }
    g.setColour (glow.isTransparent() ? P.edgeDim : glow.withAlpha (0.9f)); g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    g.setColour (P.edgeDim.withMultipliedAlpha (0.55f)); g.drawRoundedRectangle (r.reduced (4.0f), 3.5f, 0.8f);
    g.setColour (P.gold);
    for (auto c : { r.reduced (4).getTopLeft(), r.reduced (4).getTopRight(), r.reduced (4).getBottomLeft(), r.reduced (4).getBottomRight() })
    {   // a tiny diamond fleuron at each corner
        juce::Path d; d.startNewSubPath (c.x, c.y - 3); d.lineTo (c.x + 3, c.y); d.lineTo (c.x, c.y + 3); d.lineTo (c.x - 3, c.y); d.closeSubPath(); g.fillPath (d);
    }
}

// a section title in a panel: small caps in Gloock with a gilded rule and a diamond after it
inline void title (juce::Graphics& g, const Pal& P, juce::Rectangle<float> r, const juce::String& t, const juce::String& numeral = {})
{
    float x = r.getX();
    if (numeral.isNotEmpty())
    {
        g.setColour (P.gold); g.setFont (display (r.getHeight() * 0.8f));
        g.drawText (numeral, juce::Rectangle<float> (x, r.getY(), 40, r.getHeight()), juce::Justification::centredLeft, false);
        x += width (display (r.getHeight() * 0.8f), numeral) + 8;
    }
    const auto f = display (r.getHeight() * 0.8f);
    g.setColour (P.ink); g.setFont (f);
    g.drawText (t, juce::Rectangle<float> (x, r.getY(), r.getRight() - x, r.getHeight()), juce::Justification::centredLeft, false);
    const float tx = x + width (f, t) + 10;
    if (tx + 20 < r.getRight())
    {
        const float y = r.getCentreY() + 1;
        g.setColour (P.edgeDim); g.fillRect (tx, y, r.getRight() - tx - 8, 1.0f);
        juce::Path d; d.startNewSubPath (r.getRight() - 4, y - 3.5f); d.lineTo (r.getRight(), y + 0.5f); d.lineTo (r.getRight() - 4, y + 4.5f); d.lineTo (r.getRight() - 8, y + 0.5f); d.closeSubPath();
        g.setColour (P.gold); g.fillPath (d);
    }
}

// a candle (stick + flame), lit or not; `flick` 0..1 bends the flame
inline void candle (juce::Graphics& g, const Pal& P, juce::Rectangle<float> r, bool lit, float flick = 0.5f, float alpha = 1.0f)
{
    const float w = r.getWidth(), stickW = w * 0.42f, stickH = r.getHeight() * 0.52f;
    auto stick = juce::Rectangle<float> (r.getCentreX() - stickW * 0.5f, r.getBottom() - stickH, stickW, stickH);
    juce::ColourGradient sg (P.keyWhite.withMultipliedAlpha (alpha), stick.getX(), 0, P.keyWhite.darker (0.35f).withMultipliedAlpha (alpha), stick.getRight(), 0, false);
    g.setGradientFill (sg); g.fillRoundedRectangle (stick, 1.5f);
    g.setColour (P.ghostInk.withMultipliedAlpha (0.8f * alpha)); g.fillRect (r.getCentreX() - 0.6f, stick.getY() - 3, 1.2f, 3.0f);   // the wick
    if (lit)
    {
        const float fh = r.getHeight() - stickH - 3, fx = r.getCentreX() + (flick - 0.5f) * w * 0.25f, fy = stick.getY() - 3;
        g.setColour (P.play.withAlpha (0.22f * alpha)); g.fillEllipse (r.getCentreX() - w * 0.8f, fy - fh * 0.9f, w * 1.6f, fh * 1.5f);
        juce::Path f; f.startNewSubPath (r.getCentreX() - w * 0.22f, fy);
        f.quadraticTo (r.getCentreX() - w * 0.30f, fy - fh * 0.45f, fx, fy - fh);
        f.quadraticTo (r.getCentreX() + w * 0.30f, fy - fh * 0.45f, r.getCentreX() + w * 0.22f, fy); f.closeSubPath();
        juce::ColourGradient fg (juce::Colour (0xfffff6d8).withMultipliedAlpha (alpha), r.getCentreX(), fy, P.play.withMultipliedAlpha (alpha), fx, fy - fh, false);
        g.setGradientFill (fg); g.fillPath (f);
    }
    else
    {   // a thread of smoke from the wick
        juce::Path s; const float y0 = stick.getY() - 3;
        s.startNewSubPath (r.getCentreX(), y0);
        s.cubicTo (r.getCentreX() + w * 0.3f, y0 - 4, r.getCentreX() - w * 0.3f, y0 - 8, r.getCentreX() + w * 0.1f, r.getY());
        g.setColour (P.faint.withAlpha (0.55f * alpha)); g.strokePath (s, juce::PathStrokeType (1.0f));
    }
}

// a hooded ghost singer in a box (w : h about 1 : 1.4). open: 0 = mouth shut .. 1 = singing "O". lit: 0..1 glow and halo
inline void singer (juce::Graphics& g, const Pal& P, juce::Rectangle<float> r, juce::Colour robe, float open, float lit, float alpha = 1.0f, bool book = true)
{
    const float x = r.getX(), y = r.getY(), w = r.getWidth(), h = r.getHeight();
    auto at = [&] (float u, float v) { return juce::Point<float> (x + u * w, y + v * h); };
    if (lit > 0.01f)
    {   // the light round a singing voice, and its halo
        g.setColour (P.glow.withAlpha (0.20f * lit * alpha)); g.fillEllipse (x - w * 0.25f, y - h * 0.05f, w * 1.5f, h * 0.9f);
        g.setColour (P.play.withAlpha (0.9f * lit * alpha));
        g.drawEllipse (x + w * 0.24f, y - h * 0.035f, w * 0.52f, h * 0.07f, juce::jmax (1.0f, w * 0.05f));
    }
    juce::Path body;
    body.startNewSubPath (at (0.5f, 0.03f));
    body.cubicTo (at (0.80f, 0.06f), at (0.90f, 0.30f), at (0.86f, 0.50f));
    body.cubicTo (at (0.96f, 0.64f), at (1.0f, 0.84f), at (0.98f, 0.99f));
    for (int k = 0; k < 5; ++k)
    {   // the ghostly hem
        const float u0 = 0.98f - 0.192f * (float) k;
        body.quadraticTo (at (u0 - 0.096f, 0.90f), at (u0 - 0.192f, 0.99f));
    }
    body.cubicTo (at (0.0f, 0.84f), at (0.04f, 0.64f), at (0.14f, 0.50f));
    body.cubicTo (at (0.10f, 0.30f), at (0.20f, 0.06f), at (0.5f, 0.03f));
    body.closeSubPath();
    const auto top = robe.interpolatedWith (P.ghost, P.dark ? 0.18f : 0.10f), bottom = robe.withMultipliedAlpha (0.25f);
    juce::ColourGradient rg (top.withMultipliedAlpha (alpha), x, y, bottom.withMultipliedAlpha (alpha), x, y + h, false);
    rg.addColour (0.55, robe.withMultipliedAlpha (alpha));
    g.setGradientFill (rg); g.fillPath (body);
    g.setColour ((P.dark ? robe.brighter (0.5f) : P.ghostInk).withAlpha (0.55f * alpha)); g.strokePath (body, juce::PathStrokeType (juce::jmax (0.7f, w * 0.03f)));
    // the face inside the hood
    const auto face = juce::Rectangle<float> (x + w * 0.29f, y + h * 0.17f, w * 0.42f, h * 0.29f);
    g.setColour (P.ghostInk.withAlpha (0.55f * alpha)); g.fillEllipse (face.expanded (w * 0.04f, h * 0.02f));
    g.setColour (P.ghost.interpolatedWith (P.glow, 0.25f * lit).withMultipliedAlpha (alpha)); g.fillEllipse (face);
    g.setColour (P.ghostInk.withMultipliedAlpha (alpha));
    const float ew = w * 0.075f, eh = h * 0.05f + h * 0.01f * (1.0f - open);
    g.fillEllipse (face.getX() + face.getWidth() * 0.27f - ew * 0.5f, face.getY() + face.getHeight() * 0.36f - eh * 0.5f, ew, eh);
    g.fillEllipse (face.getX() + face.getWidth() * 0.73f - ew * 0.5f, face.getY() + face.getHeight() * 0.36f - eh * 0.5f, ew, eh);
    const float mw = w * (0.10f + 0.03f * open), mh = juce::jmax (1.0f, h * (0.018f + 0.10f * open));
    g.fillEllipse (face.getCentreX() - mw * 0.5f, face.getY() + face.getHeight() * 0.70f - mh * 0.5f, mw, mh);
    if (book && w >= 18)
    {   // a little hymnal held at the chest
        auto b = juce::Rectangle<float> (x + w * 0.33f, y + h * 0.58f, w * 0.34f, h * 0.10f);
        g.setColour (P.gold.withMultipliedAlpha (alpha)); g.fillRect (b);
        g.setColour (P.ghostInk.withAlpha (0.6f * alpha)); g.fillRect (b.getCentreX() - 0.5f, b.getY(), 1.0f, b.getHeight());
    }
}

// an organ pipe meter: a pipe whose body fills from the foot up with `v` (0..1) of glass light; mouth near the foot
inline void pipe (juce::Graphics& g, const Pal& P, juce::Rectangle<float> r, float v, juce::Colour c, bool active = true)
{
    const float a = active ? 1.0f : 0.4f;
    auto foot = r.removeFromBottom (r.getHeight() * 0.14f);
    juce::Path cone; cone.startNewSubPath (foot.getX(), foot.getY()); cone.lineTo (foot.getRight(), foot.getY()); cone.lineTo (foot.getCentreX() + 2, foot.getBottom()); cone.lineTo (foot.getCentreX() - 2, foot.getBottom()); cone.closeSubPath();
    g.setColour (P.edge.withMultipliedAlpha (a)); g.fillPath (cone);
    juce::ColourGradient mg (P.cell.brighter (0.15f), r.getX(), 0, P.cell.darker (0.3f), r.getRight(), 0, false);
    g.setGradientFill (mg); g.fillRect (r);
    const float fv = juce::jlimit (0.0f, 1.0f, v);
    if (fv > 0)
    {
        auto f = r.withTop (r.getBottom() - r.getHeight() * fv);
        juce::ColourGradient cg (c.brighter (0.4f).withMultipliedAlpha (a), f.getX(), 0, c.darker (0.2f).withMultipliedAlpha (a), f.getRight(), 0, false);
        g.setGradientFill (cg); g.fillRect (f);
    }
    // the mouth and the gold bands
    auto mouth = juce::Rectangle<float> (r.getX() + r.getWidth() * 0.25f, r.getBottom() - r.getHeight() * 0.16f, r.getWidth() * 0.5f, r.getHeight() * 0.06f);
    g.setColour (P.lead.withAlpha (0.8f * a)); g.fillEllipse (mouth);
    g.setColour (P.edge.withMultipliedAlpha (a)); g.drawRect (r, 1.0f); g.fillRect (r.getX(), r.getY(), r.getWidth(), 2.0f);
}

// the nave: stone ground with ashlar joints and shafts of coloured light falling from high windows (cache it: callers do)
inline void nave (juce::Graphics& g, const Pal& P, juce::Rectangle<int> ri, juce::Point<int> origin = {})
{
    const auto r = ri.toFloat();
    const float ox = (float) origin.x, oy = (float) origin.y;   // where this piece sits in the window (so the pieces join)
    juce::ColourGradient bg (P.dark ? P.bg1 : P.bg0, 0, -oy, P.dark ? P.bg0 : P.bg1, 0, 900.0f - oy, false);
    g.setGradientFill (bg); g.fillRect (r);
    g.setColour ((P.dark ? juce::Colours::white : P.lead).withAlpha (P.dark ? 0.022f : 0.045f));
    for (int row = (int) std::floor (oy / 36.0f); row * 36.0f - oy < r.getBottom(); ++row)
    {
        const float y = row * 36.0f - oy;
        g.fillRect (r.getX(), y, r.getWidth(), 1.0f);
        for (float x = (row % 2 ? -40.0f : 0.0f) - std::fmod (ox, 80.0f); x < r.getRight(); x += 80.0f) g.fillRect (x, y, 1.0f, 36.0f);
    }
    const juce::Colour shafts[] { P.glass[1], P.glass[4], P.glass[3], P.glass[0] };
    for (int k = 0; k < 4; ++k)
    {   // the shafts of light: wide soft quads from the top, leaning right
        const float x0 = 120.0f + 300.0f * (float) k - ox, w0 = 70.0f + 20.0f * (float) (k % 2);
        juce::Path q; q.startNewSubPath (x0, -oy); q.lineTo (x0 + w0, -oy); q.lineTo (x0 + w0 + 520.0f, 900.0f - oy); q.lineTo (x0 + 380.0f, 900.0f - oy); q.closeSubPath();
        juce::ColourGradient sg (shafts[k].withAlpha (P.dark ? 0.075f : 0.085f), x0, -oy, shafts[k].withAlpha (0.0f), x0 + 300.0f, 760.0f - oy, false);
        g.setGradientFill (sg); g.fillPath (q);
    }
}

// a diamond (lozenge) ornament
inline void diamond (juce::Graphics& g, juce::Point<float> c, float s, juce::Colour col)
{
    juce::Path d; d.startNewSubPath (c.x, c.y - s); d.lineTo (c.x + s, c.y); d.lineTo (c.x, c.y + s); d.lineTo (c.x - s, c.y); d.closeSubPath();
    g.setColour (col); g.fillPath (d);
}

// (polish 1) greyed words stay readable: at least 3:1 against their ground in both looks, yet plainly dimmer than live words
inline float greyA (const Pal& P) { return P.dark ? 0.48f : 0.56f; }
// (fix-blur) greyed / dimmed TEXT as a solid colour, never alpha-blended: `c` mixed toward the panel ground by (1 - a), then pushed
// back toward the ink just enough to keep 3:1 against that ground. Alpha-blended glyph edges on cream read as fuzzy on a Retina screen
inline float luminance (juce::Colour c)
{
    auto ch = [] (float v) { return v <= 0.03928f ? v / 12.92f : std::pow ((v + 0.055f) / 1.055f, 2.4f); };
    return 0.2126f * ch (c.getFloatRed()) + 0.7152f * ch (c.getFloatGreen()) + 0.0722f * ch (c.getFloatBlue());
}
inline float contrast (juce::Colour a, juce::Colour b)
{ const float x = luminance (a), y = luminance (b); return (juce::jmax (x, y) + 0.05f) / (juce::jmin (x, y) + 0.05f); }
inline juce::Colour greyInk (const Pal& P, juce::Colour c, float a, juce::Colour ground)
{
    if (a >= 0.999f) return c;
    c = c.withAlpha (1.0f);
    for (float t = juce::jlimit (0.0f, 1.0f, a); t < 1.0f; t += 0.04f)
    {
        const auto m = ground.interpolatedWith (c, t);
        if (contrast (m, ground) >= 3.0f) return m;
    }
    juce::ignoreUnused (P);
    return c;
}
inline juce::Colour greyInk (const Pal& P, juce::Colour c, float a) { return greyInk (P, c, a, P.panel); }

// (polish 1) a path shown to the user never shows a developer's folder (/home, /tmp, ...): the home folder becomes ~, anything else
// outside it is shortened to its last part ("…/voice-a.wav")
inline juce::String friendlyPath (const juce::String& full)
{
    if (full.isEmpty()) return full;
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName();
    const bool dev = full.startsWith ("/tmp") || full.startsWith ("/home") || full.startsWith ("/var") || full.startsWith ("/private") || full.startsWith ("/root");
    if (! dev && home.length() > 1 && full.startsWith (home + "/")) return "~" + full.substring (home.length());
    if (full.startsWith ("/") || (full.length() > 2 && full[1] == ':'))
        return juce::String::fromUTF8 ("\xe2\x80\xa6/") + juce::File (full).getFileName();
    return full;
}

// ================================================================================================ the look (JUCE LookAndFeel)
class Look : public juce::LookAndFeel_V4
{
public:
    const Pal* P = &pal (true);
    void setPal (const Pal& p)
    {
        P = &p;
        setColourScheme ({ p.bg0, p.panel, p.card, p.edgeDim, p.ink, p.glass[4], p.onGold, p.glass[4], p.ink });
        setColour (juce::TextButton::textColourOffId, p.ink); setColour (juce::TextButton::textColourOnId, p.ink);
        setColour (juce::Label::textColourId, p.ink);
        setColour (juce::ComboBox::textColourId, p.ink); setColour (juce::ComboBox::backgroundColourId, p.cell); setColour (juce::ComboBox::outlineColourId, p.edgeDim);
        setColour (juce::PopupMenu::backgroundColourId, p.card); setColour (juce::PopupMenu::textColourId, p.ink);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, p.glass[4].withAlpha (0.85f)); setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        setColour (juce::TooltipWindow::backgroundColourId, p.card); setColour (juce::TooltipWindow::textColourId, p.ink); setColour (juce::TooltipWindow::outlineColourId, p.edge);
        setColour (juce::TextEditor::backgroundColourId, p.waveBg); setColour (juce::TextEditor::textColourId, p.ink);
        setColour (juce::TextEditor::outlineColourId, p.edgeDim); setColour (juce::TextEditor::focusedOutlineColourId, p.gold);
        setColour (juce::TextEditor::highlightColourId, p.glass[4].withAlpha (0.35f)); setColour (juce::CaretComponent::caretColourId, p.gold);
        setColour (juce::AlertWindow::backgroundColourId, p.card); setColour (juce::AlertWindow::textColourId, p.ink); setColour (juce::AlertWindow::outlineColourId, p.edge);
        setColour (juce::ScrollBar::thumbColourId, p.edge.withAlpha (0.6f));
        setColour (juce::ProgressBar::foregroundColourId, p.play); setColour (juce::ProgressBar::backgroundColourId, p.cell);
        setColour (juce::ResizableWindow::backgroundColourId, p.card);
    }
    // ---- buttons: brass-edged plaques; ON = lit glass; greyed = faded, no edge glow
    juce::Font getTextButtonFont (juce::TextButton& b, int h) override
    {   // (polish 1) a row of buttons can share one size ("fontH"), so no button in it looks smaller than its neighbours
        if (b.getProperties().contains ("fontH")) return serif ((float) b.getProperties()["fontH"], bold);
        return serif (juce::jmin (14.5f, (float) h * 0.50f), bold);
    }
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool en = b.isEnabled(), on = b.getToggleState();
        const float a = en ? 1.0f : 0.38f;
        if (on)
        {
            juce::ColourGradient gr (P->glass[4].withMultipliedAlpha (a), r.getX(), r.getY(), P->glass[1].withMultipliedAlpha (a), r.getRight(), r.getBottom(), false);
            g.setGradientFill (gr); g.fillRoundedRectangle (r, 4.0f);
            g.setColour (juce::Colours::white.withAlpha (0.18f * a)); g.fillRoundedRectangle (r.withHeight (r.getHeight() * 0.45f).reduced (2, 1), 3.0f);
        }
        else if (! en)
        {   // (polish 1) greyed: no plaque at all, only a faint dashed outline, so it reads as "not now" at a glance in both looks
            juce::Path o; o.addRoundedRectangle (r, 4.0f); juce::Path d; const float dash[] { 3.0f, 3.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (d, o, dash, 2);
            g.setColour (P->faint.withAlpha (P->dark ? 0.55f : 0.65f)); g.fillPath (d);
            return;
        }
        else
        {   // flat (cheap) with a thin lit edge at the top
            g.setColour ((down ? P->cell : P->card).withMultipliedAlpha (a)); g.fillRoundedRectangle (r, 4.0f);
            g.setColour ((P->dark ? juce::Colours::white : juce::Colours::white).withAlpha ((P->dark ? 0.05f : 0.5f) * a)); g.fillRect (r.getX() + 4, r.getY() + 1.5f, r.getWidth() - 8, 1.0f);
        }
        g.setColour ((en ? (over ? P->goldHi : P->edge) : P->faint).withMultipliedAlpha (en ? 1.0f : 0.5f));
        g.drawRoundedRectangle (r, 4.0f, over && en ? 1.4f : 1.0f);
    }
    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        const bool en = b.isEnabled(), on = b.getToggleState();
        auto f = getTextButtonFont (b, b.getHeight());
        const auto r = b.getLocalBounds().reduced (5, 1).toFloat();
        const float h = b.getProperties().contains ("fontH") ? f.getHeight() : fit (bold, b.getButtonText(), r.getWidth(), f.getHeight(), 9.0f);
        g.setFont (serif (h, bold));
        g.setColour ((on ? juce::Colours::white : P->ink).withMultipliedAlpha (en ? 1.0f : greyA (*P)));
        g.drawText (b.getButtonText(), r, juce::Justification::centred, false);
    }
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool) override
    {   // the small stop tabs (REV, 1SHOT, FMNT C): a lit candle when on
        const bool en = b.isEnabled(), on = b.getToggleState();
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        g.setColour ((on ? P->glass[3].withAlpha (0.22f) : P->cell).withMultipliedAlpha (en ? 1.0f : 0.4f)); g.fillRoundedRectangle (r, 4.0f);
        g.setColour ((en ? (over ? P->goldHi : P->edge) : P->faint).withMultipliedAlpha (en ? 1.0f : 0.5f)); g.drawRoundedRectangle (r, 4.0f, 1.0f);
        candle (g, *P, juce::Rectangle<float> (r.getX() + 6, r.getY() + 3, 9, r.getHeight() - 6), on && en, 0.5f, en ? 1.0f : 0.4f);
        const auto t = r.withTrimmedLeft (20).withTrimmedRight (4);
        const auto words = b.getProperties().contains ("onOff") ? juce::String (on ? "ON" : "OFF") : b.getButtonText();
        g.setFont (fitted (bold, words, t.getWidth(), 13.5f, 9.0f));
        g.setColour (greyInk (*P, (on && en ? (P->dark ? P->play : P->ink) : P->ink), en ? 1.0f : greyA (*P)));
        g.drawText (words, t, juce::Justification::centred, false);
    }
    // ---- combo boxes: a plaque with a gold chevron
    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& c) override
    {
        auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
        const bool en = c.isEnabled(); const float a = en ? 1.0f : 0.38f;
        juce::ColourGradient gr (P->card.withMultipliedAlpha (a), 0, 0, P->cell.withMultipliedAlpha (a), 0, (float) h, false);
        g.setGradientFill (gr); g.fillRoundedRectangle (r, 4.0f);
        g.setColour ((en ? P->edge : P->faint).withMultipliedAlpha (en ? 1.0f : 0.5f)); g.drawRoundedRectangle (r, 4.0f, 1.0f);
        juce::Path ar; const float ax = (float) w - 12, ay = (float) h * 0.5f;
        ar.startNewSubPath (ax - 4, ay - 2); ar.lineTo (ax, ay + 2.5f); ar.lineTo (ax + 4, ay - 2);
        g.setColour (P->gold.withMultipliedAlpha (a)); g.strokePath (ar, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    juce::Font getComboBoxFont (juce::ComboBox& c) override { return serif (juce::jmin (14.5f, (float) c.getHeight() * 0.56f), bold); }
    void positionComboBoxText (juce::ComboBox& c, juce::Label& l) override
    {
        l.setBounds (6, 0, c.getWidth() - 22, c.getHeight());
        l.setFont (getComboBoxFont (c)); l.setMinimumHorizontalScale (0.75f);
        l.setColour (juce::Label::textColourId, greyInk (*P, P->ink, c.isEnabled() ? 1.0f : greyA (*P)));
        l.setJustificationType (juce::Justification::centred);
    }
    juce::Font getPopupMenuFont() override { return serif (15.0f, bold); }
    // ---- linear sliders: a brass rail filled with glass light, a gold lozenge for the thumb
    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider& s) override
    {
        const bool en = s.isEnabled(); const float a = en ? 1.0f : 0.35f;
        const float cy = (float) y + (float) h * 0.5f;
        auto tr = juce::Rectangle<float> ((float) x, cy - 3.0f, (float) w, 6.0f);
        g.setColour (P->cell.withMultipliedAlpha (a)); g.fillRoundedRectangle (tr, 3.0f);
        const auto& pr = s.getProperties();
        const auto c0 = pr.contains ("c0") ? juce::Colour ((juce::uint32) (juce::int64) pr["c0"]) : P->glass[1];
        const auto c1 = pr.contains ("c1") ? juce::Colour ((juce::uint32) (juce::int64) pr["c1"]) : P->glass[4];
        const bool full = pr.contains ("full");
        juce::ColourGradient gr (c0.withMultipliedAlpha (a), tr.getX(), cy, c1.withMultipliedAlpha (a), tr.getRight(), cy, false);
        if (en) { g.setGradientFill (gr); g.fillRoundedRectangle (full ? tr : tr.withRight (juce::jmax (tr.getX() + 6, pos)), 3.0f); }   // (polish 2) greyed: no fill
        g.setColour ((en ? P->edge : P->faint).withMultipliedAlpha (a)); g.drawRoundedRectangle (tr, 3.0f, 0.8f);
        const float r = 8.0f;
        juce::Path d; d.startNewSubPath (pos, cy - r); d.lineTo (pos + r * 0.75f, cy); d.lineTo (pos, cy + r); d.lineTo (pos - r * 0.75f, cy); d.closeSubPath();
        g.setColour (P->shadow); g.fillPath (d, juce::AffineTransform::translation (0, 1.5f));
        juce::ColourGradient tg (P->goldHi.withMultipliedAlpha (a), pos, cy - r, P->gold.darker (0.3f).withMultipliedAlpha (a), pos, cy + r, false);
        g.setGradientFill (tg); g.fillPath (d);
        g.setColour (P->onGold.withAlpha (0.5f * a)); g.strokePath (d, juce::PathStrokeType (0.8f));
    }
    // ---- rotary: an organ stop knob (an ivory face in a gilded ring), the value as a ring of candle light
    std::map<juce::String, juce::Image> knobFaces;   // the static part of each knob size / look / state (track + ivory face)
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) override
    {
        const bool en = s.isEnabled(); const float al = en ? 1.0f : 0.35f;
        const float rad = juce::jmin (w, h) * 0.5f - 2.0f, cx = (float) x + (float) w * 0.5f, cy = (float) y + (float) h * 0.5f;
        const float fr = rad - 6.5f;
        const int side = (int) std::ceil (rad * 2 + 6);
        const float ps = pixelScale (g);   // (fix-blur) the face at the display's pixel scale
        const auto key = juce::String (ps, 2) + "@" + juce::String (side) + "|" + juce::String ((int) P->dark) + "|" + juce::String ((int) en) + "|" + juce::String (a0, 2) + juce::String (a1, 2);
        auto& img = knobFaces[key];
        if (! img.isValid())
        {
            const int ph = juce::roundToInt ((float) side * ps);
            img = juce::Image (juce::Image::ARGB, ph, ph, true);
            juce::Graphics ig (img);
            if (ps != 1.0f) ig.addTransform (juce::AffineTransform::scale ((float) ph / (float) side));
            const float c = (float) side * 0.5f;
            juce::Path track; track.addCentredArc (c, c, rad - 2, rad - 2, 0, a0, a1, true);
            // (polish 1) LIGHT: a darker track and an ink rim, so the knob never melts into the cream page
            ig.setColour ((P->dark ? P->cell : P->faint.withAlpha (0.55f)).withMultipliedAlpha (al)); ig.strokePath (track, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            ig.setColour (P->shadow); ig.fillEllipse (c - fr, c - fr + 1.5f, fr * 2, fr * 2);
            ig.setColour (P->edge.withMultipliedAlpha (al)); ig.fillEllipse (c - fr, c - fr, fr * 2, fr * 2);
            if (! P->dark) { ig.setColour (P->lead.withAlpha (0.75f * al)); ig.drawEllipse (c - fr, c - fr, fr * 2, fr * 2, 1.0f); }
            juce::ColourGradient fg (P->keyWhite.withMultipliedAlpha (al), c - fr, c - fr, P->keyWhite.darker (0.25f).withMultipliedAlpha (al), c + fr, c + fr, false);
            ig.setGradientFill (fg); ig.fillEllipse (c - fr + 1.6f, c - fr + 1.6f, fr * 2 - 3.2f, fr * 2 - 3.2f);
            if (knobFaces.size() > 200) knobFaces.clear();
        }
        drawScaled (g, img, (float) img.getWidth() / (float) side, (int) std::round (cx - (float) side * 0.5f), (int) std::round (cy - (float) side * 0.5f));
        const bool bip = s.getMinimum() < 0 && s.getMaximum() > 0;
        const float mid = a0 + (a1 - a0) * (float) ((0 - s.getMinimum()) / (s.getMaximum() - s.getMinimum()));
        const float ang = a0 + pos * (a1 - a0);
        juce::Path val; val.addCentredArc (cx, cy, rad - 2, rad - 2, 0, bip ? juce::jmin (mid, ang) : a0, bip ? juce::jmax (mid, ang) : ang, true);
        // (polish 2) greyed knobs: the face at 35 %, and no lit arc
        if (en) { g.setColour (P->play); g.strokePath (val, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); }
        g.setColour (juce::Colour (0xff2a2017).withMultipliedAlpha (al));
        g.drawLine (cx + std::sin (ang) * (fr - 2.5f), cy - std::cos (ang) * (fr - 2.5f), cx + std::sin (ang) * fr * 0.35f, cy - std::cos (ang) * fr * 0.35f, 2.0f);
    }
    void drawProgressBar (juce::Graphics& g, juce::ProgressBar&, int w, int h, double p, const juce::String&) override
    {
        auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
        g.setColour (P->cell); g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        if (p > 0) { juce::ColourGradient gr (P->gold, 0, 0, P->play, (float) w, 0, false); g.setGradientFill (gr); g.fillRoundedRectangle (r.withWidth (r.getWidth() * (float) juce::jlimit (0.0, 1.0, p)), r.getHeight() * 0.5f); }
        g.setColour (P->edgeDim); g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 0.8f);
    }
    void drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vert, int tp, int ts, bool over, bool) override
    {
        g.setColour (P->cell.withAlpha (0.6f)); g.fillRoundedRectangle (juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3), 3.0f);
        auto t = vert ? juce::Rectangle<float> ((float) x, (float) tp, (float) w, (float) ts) : juce::Rectangle<float> ((float) tp, (float) y, (float) ts, (float) h);
        g.setColour (over ? P->goldHi : P->edge); g.fillRoundedRectangle (t.reduced (3), 3.0f);
    }
    void fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor& t) override
    { g.setColour (t.findColour (juce::TextEditor::backgroundColourId)); g.fillRoundedRectangle (juce::Rectangle<float> (0, 0, (float) w, (float) h), 4.0f); }
    void drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& t) override
    { g.setColour (t.hasKeyboardFocus (true) ? P->gold : P->edgeDim); g.drawRoundedRectangle (juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f), 4.0f, 1.0f); }
    juce::Font getAlertWindowMessageFont() override { return serif (16.0f); }
    juce::Font getAlertWindowTitleFont() override { return display (20.0f); }
    juce::Font getLabelFont (juce::Label& l) override
    {   // (polish 1) a picker's words are the same size and weight as the buttons round it
        if (auto* cb = dynamic_cast<juce::ComboBox*> (l.getParentComponent())) return getComboBoxFont (*cb);
        return serif ((float) juce::jmin (16, l.getHeight()) * 0.8f);
    }
    juce::Rectangle<int> getTooltipBounds (const juce::String& tip, juce::Point<int> pos, juce::Rectangle<int> parent) override
    {
        juce::AttributedString s; s.append (tip, serif (15.0f), P->ink); juce::TextLayout tl; tl.createLayoutWithBalancedLineLengths (s, 420.0f);
        const int w = (int) tl.getWidth() + 18, h = (int) tl.getHeight() + 12;
        return juce::Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 24, pos.y > parent.getCentreY() ? pos.y - (h + 6) : pos.y + 6, w, h).constrainedWithin (parent);
    }
    void drawTooltip (juce::Graphics& g, const juce::String& tip, int w, int h) override
    {
        auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h);
        g.setColour (P->card); g.fillRoundedRectangle (r, 4.0f); g.setColour (P->edge); g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
        juce::AttributedString s; s.append (tip, serif (15.0f), P->ink); s.setJustification (juce::Justification::centredLeft);
        juce::TextLayout tl; tl.createLayoutWithBalancedLineLengths (s, 420.0f); tl.draw (g, r.reduced (9, 6));
    }
};

// ================================================================================================ a candle switch (on = lit)
// The switch every page uses: a candle in a small niche and its words. A click (or Space / Return) toggles it. Greyed = faded and
// not clickable. Same calls as the old switch: label, onChange, setToggleState (state, notify), getToggleState.
class Candle : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit Candle (juce::String l = {}) : label (std::move (l)) { setWantsKeyboardFocus (true); setMouseCursor (juce::MouseCursor::PointingHandCursor); setTitle (label); }
    std::function<void (bool)> onChange;
    juce::String label;
    const Pal* P = &pal (true);
    bool getToggleState() const { return state; }
    void setToggleState (bool s, bool notify) { if (s == state) return; state = s; if (notify && onChange) onChange (state); repaint(); }
    void paint (juce::Graphics& g) override
    {
        const bool en = isEnabled(); const float a = en ? 1.0f : 0.38f;
        auto r = getLocalBounds().toFloat();
        auto niche = r.removeFromLeft (r.getHeight() * 0.8f + 6).reduced (1, 1);
        g.setColour ((state ? P->play.withAlpha (P->dark ? 0.16f : 0.22f) : P->cell).withMultipliedAlpha (a)); g.fillPath (lancet (niche, 0.45f));
        g.setColour ((en ? (hot ? P->goldHi : P->edge) : P->faint).withMultipliedAlpha (a)); g.strokePath (lancet (niche, 0.45f), juce::PathStrokeType (1.0f));
        candle (g, *P, niche.reduced (niche.getWidth() * 0.28f, 3).withTrimmedTop (2), state, 0.5f, a);
        if (hasKeyboardFocus (false)) { g.setColour (P->gold.withAlpha (0.7f)); g.drawRect (getLocalBounds().toFloat(), 1.0f); }
        r.removeFromLeft (8);
        const float fh = fit (bold, label, r.getWidth(), juce::jmin (15.0f, r.getHeight() * 0.62f), 9.0f);
        g.setColour (greyInk (*P, P->ink, en ? 1.0f : greyA (*P))); g.setFont (serif (fh, bold));
        g.drawText (label, r, juce::Justification::centredLeft, false);
    }
    void mouseEnter (const juce::MouseEvent&) override { hot = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hot = false; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override { if (isEnabled() && ! e.mods.isPopupMenu() && getLocalBounds().contains (e.getPosition())) setToggleState (! state, true); }
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::spaceKey || k == juce::KeyPress::returnKey) { if (isEnabled()) setToggleState (! state, true); return true; }
        return false;
    }
    void enablementChanged() override { repaint(); }
private:
    bool state = false, hot = false;
};
}
