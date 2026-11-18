/*
 * Ghost Bass - a bass instrument built on Surge XT.
 * Copyright 2026 the Ghost Bass authors. Based on Surge XT by the Surge Synth Team.
 * Released under the GNU General Public Licence v3 or later (see LICENSE).
 */
#include "GhostBoardSets.h"
#include "GhostEasyView.h"

#include "SurgeSynthEditor.h"
#include "SurgeSynthProcessor.h"
#include "SurgeGUIEditor.h"
#include "SurgeXTBinary.h"
#include "GhostEngine.h"
#include "GhostPatchTools.h"
#include "terrain/TerrainView.h"
#include "layers/GhostLayers.h"
#include "GhostMidiOut.h"

#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>
#include <deque>
#include <random>
#include <map>
#include <unordered_map>

namespace ghost::ui
{
using R = juce::Rectangle<float>;

// ------------------------------------------------------------------------------------------------
// look (build 12, AKIRA): hand-painted Japanese cyberpunk translated into precise machine controls. Deep midnight-navy panels,
// dirty concrete greys, machined edges with one cut corner, tiny technical labels, Kaneda-red indicators, electric-cyan pointers
// and sodium-amber readouts. The neon is restrained: light only where something is on.
// The member names are the old ones so every page follows the new look: violet is now KANEDA RED, cyan ELECTRIC CYAN,
// amber SODIUM AMBER, slime RADIOACTIVE GREEN, pink PHOSPHOR WHITE (the fourth blend-board corner).
// build 14: two themes. NIGHT is the Akira look above; DAWN is the light one: a lavender-and-peach ink-landscape painting behind
// frosted paper panels, indigo ink lettering, rose / periwinkle / peach / jade for what is lit. Every colour is read at paint
// time from these (so switching repaints everything); themed() maps a NIGHT colour a control stored earlier onto DAWN.
namespace col
{
juce::Colour bg, header, panel, card, cell, line, ink, dim, violet, lilac, cyan, slime, amber, pink;
juce::Colour red, redDeep, steel, concrete, navy, soot;
juce::Colour onLit, titleSwash, titleInk; // lettering on a lit swash; the page-title swash and its lettering
bool light = false;
}
struct ThemeSet
{
    uint32_t v[23];
};
static const ThemeSet kNight{{0xff04070e, 0xff070c18, 0xff0a1222, 0xff0f192d, 0xff15203a, 0xff34435f, 0xffece6d6, 0xff8f96a5, 0xffe5242c,
                              0xffff7a68, 0xff35e0ec, 0xff9cff3d, 0xfff4a340, 0xffd9dde6, 0xffe5242c, 0xff7a0d14, 0xff5b6880, 0xff7d838d,
                              0xff0c1630, 0xff02040a, 0xff02040a, 0xffefe6d2, 0xff02040a}};
static const ThemeSet kDawn{{0xfff1ecf6, 0xffe9e2f3, 0xfff8f5fc, 0xfff0eaf7, 0xffe7e0f2, 0xffb9b1d8, 0xff2c2a5a, 0xff6c6896, 0xffcf4a73,
                             0xffe58aa6, 0xff5a76d8, 0xff3fa384, 0xffe98a5c, 0xff4a4790, 0xffcf4a73, 0xff8a2346, 0xff9a94c0, 0xffa29dc0,
                             0xffdcd4ee, 0xffe2dbef, 0xfffdfbff, 0xff3d3a78, 0xfffdfbff}};
static void setTheme(bool light)
{
    const auto &t = light ? kDawn : kNight;
    juce::Colour *dst[23] = {&col::bg,    &col::header, &col::panel,  &col::card,     &col::cell,  &col::line,       &col::ink,     &col::dim,
                             &col::violet, &col::lilac, &col::cyan,   &col::slime,    &col::amber, &col::pink,       &col::red,     &col::redDeep,
                             &col::steel, &col::concrete, &col::navy, &col::soot,     &col::onLit, &col::titleSwash, &col::titleInk};
    for (int k = 0; k < 23; ++k)
        *dst[k] = juce::Colour(t.v[k]);
    col::light = light;
}
static const bool kThemeInit = (setTheme(false), true);
// a colour a control stored when it was built (a NIGHT accent) shown in the current theme
static juce::Colour themed(juce::Colour c)
{
    if (!col::light)
        return c;
    for (int k = 0; k < 23; ++k)
        if (c.getARGB() == (kNight.v[k] | 0u) && k >= 8 && k <= 14)
            return juce::Colour(kDawn.v[k]);
    return c;
}

static juce::Typeface::Ptr typeface(int w)
{
    static juce::Typeface::Ptr med = juce::Typeface::createSystemTypefaceFor(
        SurgeXTBinary::BarlowCondensedMedium_ttf, SurgeXTBinary::BarlowCondensedMedium_ttfSize);
    static juce::Typeface::Ptr semi = juce::Typeface::createSystemTypefaceFor(
        SurgeXTBinary::BarlowCondensedSemiBold_ttf, SurgeXTBinary::BarlowCondensedSemiBold_ttfSize);
    static juce::Typeface::Ptr bold = juce::Typeface::createSystemTypefaceFor(
        SurgeXTBinary::BarlowCondensedBold_ttf, SurgeXTBinary::BarlowCondensedBold_ttfSize);
    static juce::Typeface::Ptr xbold = juce::Typeface::createSystemTypefaceFor(
        SurgeXTBinary::BarlowCondensedExtraBold_ttf, SurgeXTBinary::BarlowCondensedExtraBold_ttfSize);
    return w >= 3 ? xbold : (w == 2 ? bold : (w == 1 ? semi : med));
}
static juce::Typeface::Ptr monoFace()
{
    static juce::Typeface::Ptr m = juce::Typeface::createSystemTypefaceFor(
        SurgeXTBinary::ShareTechMonoRegular_ttf, SurgeXTBinary::ShareTechMonoRegular_ttfSize);
    return m;
}
// industrial condensed type, a little letter-spaced. Fr is the raw size; F is the "bigger text everywhere" size (build 9 rule, kept)
static juce::Font Fr(float h, int w = 1)
{
    return juce::Font(juce::FontOptions(typeface(w)).withHeight(h).withKerningFactor(0.035f));
}
static juce::Font F(float h, int w = 1) { return Fr(std::max(13.5f, h * 1.32f), w); }
// the tiny technical labels (serials, units, indexes)
static juce::Font Mono(float h) { return juce::Font(juce::FontOptions(monoFace()).withHeight(h).withKerningFactor(0.06f)); }

// text that squeezes (never smaller than 80% sideways) and then ends in "...", on one line, or on as many lines as the box holds
template <typename RT>
static void dt(juce::Graphics &g, const juce::String &t, RT rect, juce::Justification j, bool = true)
{
    R rr = rect.toFloat();
    const float fh = g.getCurrentFont().getHeight();
    const int lines = rr.getHeight() >= fh * 2.3f ? (int)(rr.getHeight() / (fh * 1.25f)) : 1;
    if (lines == 1 && rr.getHeight() < fh * 1.4f)
    {
        const float need = fh * 1.4f;
        if (j.testFlags(juce::Justification::top))
            rr.setHeight(need);
        else if (j.testFlags(juce::Justification::bottom))
            rr = rr.withTop(rr.getBottom() - need);
        else
            rr = rr.withSizeKeepingCentre(rr.getWidth(), need);
    }
    g.drawFittedText(t, rr.toNearestInt(), j, lines, 0.8f);
}

// ---- machined shapes: every "rounded" rectangle of the old look is now a plate with its top-left and bottom-right corners cut ----
static float chamferOf(float rad, float w, float h)
{
    if (rad < 2.5f)
        return 0.f;
    return std::min({2.f + rad * 0.42f, 9.f, w * 0.28f, h * 0.32f});
}
static juce::Path chamferPath(R r, float rad)
{
    juce::Path p;
    const float c = chamferOf(rad, r.getWidth(), r.getHeight());
    if (col::light && c > 0.f)
    {
        p.addRoundedRectangle(r, std::min({rad, 16.f, r.getHeight() * 0.5f}));
        return p;
    }
    if (c <= 0.f)
    {
        p.addRectangle(r);
        return p;
    }
    p.startNewSubPath(r.getX() + c, r.getY());
    p.lineTo(r.getRight(), r.getY());
    p.lineTo(r.getRight(), r.getBottom() - c);
    p.lineTo(r.getRight() - c, r.getBottom());
    p.lineTo(r.getX(), r.getBottom());
    p.lineTo(r.getX(), r.getY() + c);
    p.closeSubPath();
    return p;
}
template <typename T> static void fillChamfer(juce::Graphics &g, juce::Rectangle<T> r, float rad) { g.fillPath(chamferPath(r.toFloat(), rad)); }
static void fillChamfer(juce::Graphics &g, float x, float y, float w, float h, float rad) { fillChamfer(g, R(x, y, w, h), rad); }
template <typename T> static void drawChamfer(juce::Graphics &g, juce::Rectangle<T> r, float rad, float th)
{
    g.strokePath(chamferPath(r.toFloat(), rad), juce::PathStrokeType(th, juce::PathStrokeType::mitered));
}
static void drawChamfer(juce::Graphics &g, float x, float y, float w, float h, float rad, float th) { drawChamfer(g, R(x, y, w, h), rad, th); }

// corner brackets on the two square corners (top-right and bottom-left)
static void brackets(juce::Graphics &g, R r, juce::Colour c, float arm = 14.f, float th = 2.f)
{
    g.setColour(c);
    g.fillRect(R(r.getRight() - arm, r.getY(), arm, th));
    g.fillRect(R(r.getRight() - th, r.getY(), th, arm));
    g.fillRect(R(r.getX(), r.getBottom() - th, arm, th));
    g.fillRect(R(r.getX(), r.getBottom() - arm, th, arm));
}
// a small machined screw head
static void rivet(juce::Graphics &g, juce::Point<float> p, float s = 3.2f)
{
    g.setGradientFill(juce::ColourGradient(col::steel.brighter(0.5f), p.x - s, p.y - s, col::soot, p.x + s, p.y + s, false));
    g.fillEllipse(R(s * 2.f, s * 2.f).withCentre(p));
    g.setColour(col::soot.withAlpha(0.9f));
    g.drawLine(p.x - s * 0.6f, p.y + s * 0.6f, p.x + s * 0.6f, p.y - s * 0.6f, 1.f);
}
// hazard stripes (diagonal) inside a rectangle
static void hazard(juce::Graphics &g, R r, juce::Colour a, juce::Colour b, float step = 7.f)
{
    g.saveState();
    g.reduceClipRegion(r.toNearestInt());
    g.setColour(b);
    g.fillRect(r);
    g.setColour(a);
    for (float x = r.getX() - r.getHeight(); x < r.getRight() + r.getHeight(); x += step * 2.f)
    {
        juce::Path p;
        p.startNewSubPath(x, r.getBottom());
        p.lineTo(x + step, r.getBottom());
        p.lineTo(x + step + r.getHeight(), r.getY());
        p.lineTo(x + r.getHeight(), r.getY());
        p.closeSubPath();
        g.fillPath(p);
    }
    g.restoreState();
}
// a tiny technical label
static void tech(juce::Graphics &g, const juce::String &t, R r, juce::Justification j = juce::Justification::centredLeft,
                 juce::Colour c = col::dim.withAlpha(0.75f), float h = 9.5f)
{
    g.setColour(c);
    g.setFont(Mono(h));
    g.drawText(t, r, j, false);
}

// ---- build 13 (INK): the Akira machine with a painted skin. Brush lettering (Permanent Marker), dry-brush swashes behind
// whatever is lit, watercolor ink blooms in the night behind the panels, paper grain over everything, washes on the keys ----
static juce::Typeface::Ptr brushFace()
{
    static juce::Typeface::Ptr b = juce::Typeface::createSystemTypefaceFor(
        SurgeXTBinary::PermanentMarkerRegular_ttf, SurgeXTBinary::PermanentMarkerRegular_ttfSize);
    return b;
}
static juce::Font Brush(float h) { return juce::Font(juce::FontOptions(brushFace()).withHeight(h).withKerningFactor(0.02f)); }

enum class Art
{
    brush1,
    brush2,
    brush3,
    bloom,
    paper,
    keyWash,
    logo,
    dawn,
    dawnLogo
};
static juce::Image art(Art a)
{
    switch (a)
    {
    case Art::brush1:
        return juce::ImageCache::getFromMemory(SurgeXTBinary::GhostBrush1_png, SurgeXTBinary::GhostBrush1_pngSize);
    case Art::brush2:
        return juce::ImageCache::getFromMemory(SurgeXTBinary::GhostBrush2_png, SurgeXTBinary::GhostBrush2_pngSize);
    case Art::brush3:
        return juce::ImageCache::getFromMemory(SurgeXTBinary::GhostBrush3_png, SurgeXTBinary::GhostBrush3_pngSize);
    case Art::bloom:
        return juce::ImageCache::getFromMemory(SurgeXTBinary::GhostInkBloom_png, SurgeXTBinary::GhostInkBloom_pngSize);
    case Art::paper:
        return juce::ImageCache::getFromMemory(SurgeXTBinary::GhostPaper_png, SurgeXTBinary::GhostPaper_pngSize);
    case Art::keyWash:
        return juce::ImageCache::getFromMemory(SurgeXTBinary::GhostKeyWash_png, SurgeXTBinary::GhostKeyWash_pngSize);
    case Art::logo:
        return juce::ImageCache::getFromMemory(SurgeXTBinary::GhostLogo_png, SurgeXTBinary::GhostLogo_pngSize);
    case Art::dawn:
        return juce::ImageCache::getFromMemory(SurgeXTBinary::GhostShanshui_jpg, SurgeXTBinary::GhostShanshui_jpgSize);
    case Art::dawnLogo:
        return juce::ImageCache::getFromMemory(SurgeXTBinary::GhostLogoDawn_png, SurgeXTBinary::GhostLogoDawn_pngSize);
    }
    return {};
}

// a dry-brush swash of paint in colour c filling r (variant picks one of the three strokes; flip mirrors it)
static void brushSwash(juce::Graphics &g, R r, juce::Colour c, int variant = 0, bool flip = false)
{
    auto img = art(variant == 1 ? Art::brush2 : (variant == 2 ? Art::brush3 : Art::brush1));
    if (!img.isValid())
    {
        g.setColour(c);
        fillChamfer(g, r, 8.f);
        return;
    }
    g.saveState();
    if (flip)
        g.addTransform(juce::AffineTransform::scale(-1.f, 1.f, r.getCentreX(), r.getCentreY()));
    // a soft shadow of the stroke, then the paint
    g.setColour(col::light ? col::ink.withAlpha(0.12f) : col::soot.withAlpha(0.55f));
    g.drawImage(img, r.translated(2.f, 2.5f), juce::RectanglePlacement::stretchToFit, true);
    g.setColour(themed(c));
    g.drawImage(img, r, juce::RectanglePlacement::stretchToFit, true);
    g.restoreState();
}

// a page title: brush letters on a paper-white swash, with a red slash mark after it
static void brushTitle(juce::Graphics &g, const juce::String &t, float x, float y, float h = 30.f, juce::Colour swash = juce::Colour(),
                       juce::Colour ink = juce::Colour())
{
    if (swash.isTransparent())
    {
        swash = col::titleSwash;
        ink = col::titleInk;
    }
    else
    {
        swash = themed(swash);
        ink = col::onLit;
    }
    const auto f = Brush(h * 0.72f);
    juce::GlyphArrangement ga;
    ga.addLineOfText(f, t, 0.f, 0.f);
    const float tw = ga.getBoundingBox(0, -1, true).getWidth();
    brushSwash(g, R(x - 8.f, y - h * 0.12f, tw + 40.f, h * 1.24f), swash, (int)(t.hashCode() & 1));
    g.setColour(ink);
    g.setFont(f);
    g.drawText(t, R(x + 6.f, y - h * 0.08f, tw + 20.f, h), juce::Justification::centredLeft, false);
    g.setColour(col::red);
    g.setFont(Brush(h * 0.6f));
    g.drawText("///", R(x + tw + 38.f, y, 40.f, h), juce::Justification::centredLeft, false);
}

// a hairline steel frame with Kaneda-red brackets (the old "glowing border"; strength sets how lit the brackets are)
static void glowBorder(juce::Graphics &g, R r, float rad, float strength = 1.f, float width = 1.6f)
{
    r = r.reduced(0.5f);
    if (col::light)
    {
        g.setColour(juce::Colours::white.withAlpha(0.95f));
        drawChamfer(g, r, rad, width + 0.5f);
        g.setColour(col::line.withAlpha(0.6f));
        drawChamfer(g, r.expanded(1.f), rad + 1.f, 1.f);
        return;
    }
    g.setColour(col::soot.withAlpha(0.8f));
    drawChamfer(g, r.translated(0.f, 1.f), rad, width + 1.f);
    g.setColour(col::line.withAlpha(0.95f));
    drawChamfer(g, r, rad, std::max(1.f, width * 0.75f));
    brackets(g, r, col::red.withAlpha(juce::jlimit(0.f, 1.f, 0.55f + 0.4f * strength)), std::min(16.f, r.getHeight() * 0.4f), 2.f);
}

// the panel: a deep navy plate, faint scanlines, a steel frame, red / cyan brackets and four screws
static void panelBg(juce::Graphics &g, R r, float rad = 18.f)
{
    r = r.reduced(3.5f);
    if (col::light)
    {
        // DAWN: a frosted paper card over the painting, a soft lavender shadow, a white rim
        g.setColour(col::ink.withAlpha(0.07f));
        fillChamfer(g, r.translated(0.f, 4.f).expanded(1.f), rad);
        g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.80f), r.getX(), r.getY(), col::panel.withAlpha(0.66f), r.getX(),
                                               r.getBottom(), false));
        fillChamfer(g, r, rad);
        g.setColour(juce::Colours::white.withAlpha(0.95f));
        drawChamfer(g, r, rad, 2.f);
        g.setColour(col::line.withAlpha(0.45f));
        drawChamfer(g, r.expanded(1.f), rad + 1.f, 1.f);
        return;
    }
    g.setColour(col::soot.withAlpha(0.7f));
    fillChamfer(g, r.translated(0.f, 3.f), rad);
    g.setGradientFill(juce::ColourGradient(col::panel.brighter(0.10f), r.getX(), r.getY(), col::panel.darker(0.35f), r.getX(), r.getBottom(), false));
    fillChamfer(g, r, rad);
    {
        g.saveState();
        g.reduceClipRegion(chamferPath(r, rad));
        g.setColour(juce::Colours::white.withAlpha(0.018f));
        for (float y = r.getY() + 2.f; y < r.getBottom(); y += 3.f)
            g.fillRect(R(r.getX(), y, r.getWidth(), 1.f));
        // a faint top light, like a sodium lamp above the panel
        g.setGradientFill(juce::ColourGradient(col::amber.withAlpha(0.045f), r.getCentreX(), r.getY(), juce::Colours::transparentBlack, r.getCentreX(),
                                               r.getY() + 90.f, false));
        g.fillRect(r);
        g.restoreState();
    }
    g.setColour(juce::Colours::white.withAlpha(0.06f));
    g.drawLine(r.getX() + chamferOf(rad, r.getWidth(), r.getHeight()) + 2.f, r.getY() + 1.5f, r.getRight() - 2.f, r.getY() + 1.5f, 1.f);
    g.setColour(col::line);
    drawChamfer(g, r, rad, 1.2f);
    brackets(g, r.reduced(0.5f), col::red.withAlpha(0.9f), 16.f, 2.f);
    const float i = 9.f;
    rivet(g, {r.getX() + i + 4.f, r.getY() + i + 4.f}, 2.6f);
    rivet(g, {r.getRight() - i - 4.f, r.getBottom() - i - 4.f}, 2.6f);
}

static juce::ColourGradient accentGrad(R r)
{
    juce::ColourGradient gr(col::red.brighter(0.15f), r.getX(), r.getY(), col::red.darker(0.35f), r.getX(), r.getBottom(), false);
    return gr;
}

// text with a soft light behind it
static void glowText(juce::Graphics &g, const juce::String &t, R r, juce::Justification j, juce::Colour glow, float strength = 0.5f)
{
    g.setColour(glow.withAlpha(0.12f * strength));
    for (int dx = -2; dx <= 2; ++dx)
        for (int dy = -2; dy <= 2; ++dy)
            if (dx || dy)
                dt(g, t, r.translated((float)dx, (float)dy), j);
}

// a distant city at night: block towers, tiny lit windows, a red warning light on the tallest roofs (deterministic, our own drawing)
static void drawSkyline(juce::Graphics &g, R a, unsigned seed, float alpha)
{
    unsigned s = seed;
    auto rnd = [&s]() {
        s = s * 1664525u + 1013904223u;
        return (float)((s >> 8) & 0xffff) / 65535.f;
    };
    float x = a.getX() - 6.f;
    while (x < a.getRight())
    {
        const float bw = 12.f + rnd() * 30.f, bh = a.getHeight() * (0.25f + rnd() * 0.75f);
        R b(x, a.getBottom() - bh, bw, bh);
        g.setColour(col::navy.withAlpha(alpha));
        g.fillRect(b);
        g.setColour(col::steel.withAlpha(alpha * 0.35f));
        g.fillRect(R(b.getX(), b.getY(), b.getWidth(), 1.f));
        if (rnd() < 0.3f) // antenna
            g.fillRect(R(b.getX() + b.getWidth() * (0.3f + rnd() * 0.4f), b.getY() - 8.f - rnd() * 10.f, 1.f, 18.f));
        for (float wy = b.getY() + 4.f; wy < b.getBottom() - 3.f; wy += 4.f)
            for (float wx = b.getX() + 3.f; wx < b.getRight() - 3.f; wx += 4.f)
            {
                const float q = rnd();
                if (q < 0.07f)
                {
                    g.setColour((q < 0.012f ? col::cyan : col::amber).withAlpha(alpha * (0.25f + rnd() * 0.45f)));
                    g.fillRect(R(wx, wy, 1.6f, 1.6f));
                }
            }
        if (bh > a.getHeight() * 0.85f)
        {
            g.setColour(col::red.withAlpha(alpha * 0.9f));
            g.fillEllipse(R(3.f, 3.f).withCentre({b.getCentreX(), b.getY() - 2.f}));
        }
        x += bw + (rnd() < 0.2f ? 4.f + rnd() * 10.f : 0.f);
    }
}

// ---- the FAT GHOST mascot, AKIRA cut: a pale ghost with Kaneda-red visor lids, a soot headband and cyan-lit ear cups ----
static void drawGhost(juce::Graphics &g, R b)
{
    const float cx = b.getCentreX(), s = std::min(b.getWidth(), b.getHeight());
    const float rad = s * 0.41f;
    const float cy = b.getY() + s * 0.45f;
    const float bot = b.getY() + s * 0.92f;
    // the body: a fat dome (JUCE arcs run clockwise from twelve o'clock) with three scallops at the bottom
    juce::Path body;
    body.startNewSubPath(cx - rad, cy);
    body.addCentredArc(cx, cy, rad, rad * 1.02f, 0.f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, false);
    body.lineTo(cx + rad, bot - s * 0.07f);
    const float sw = rad * 2.f / 3.f;
    for (int k = 0; k < 3; ++k)
    {
        const float x1 = cx + rad - sw * (float)k, x0 = x1 - sw;
        body.quadraticTo((x0 + x1) * 0.5f, bot + s * 0.07f, x0, bot - s * 0.07f);
    }
    body.closeSubPath();
    for (int k = 7; k >= 1; --k)
    {
        g.setColour(col::red.withAlpha(0.035f));
        g.strokePath(body, juce::PathStrokeType((float)k * 2.8f, juce::PathStrokeType::curved));
    }
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xfff4efe4), cx - rad * 0.6f, cy - rad * 0.9f, juce::Colour(0xff8a93a6), cx + rad * 0.7f, bot, false));
    g.fillPath(body);
    // shine on the head
    g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.45f), cx - rad * 0.45f, cy - rad * 0.65f, juce::Colours::transparentWhite,
                                           cx - rad * 0.1f, cy - rad * 0.25f, true));
    g.fillEllipse(R(rad * 0.8f, rad * 0.45f).withCentre({cx - rad * 0.42f, cy - rad * 0.62f}));
    g.setColour(col::soot);
    g.strokePath(body, juce::PathStrokeType(2.2f));
    // headphones: the band over the top of the head, two big ear cups with a cyan ring
    juce::Path band;
    band.addCentredArc(cx, cy, rad * 1.07f, rad * 1.09f, 0.f, -1.42f, 1.42f, true);
    g.setColour(col::soot);
    g.strokePath(band, juce::PathStrokeType(s * 0.09f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(col::red);
    g.strokePath(band, juce::PathStrokeType(s * 0.022f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    for (int side = -1; side <= 1; side += 2)
    {
        R cup(s * 0.19f, s * 0.33f);
        cup = cup.withCentre({cx + (float)side * rad * 1.05f, cy + s * 0.01f});
        g.setColour(col::cyan.withAlpha(0.12f));
        fillChamfer(g, cup.expanded(s * 0.035f), s * 0.10f);
        g.setColour(juce::Colour(0xff0b1222));
        fillChamfer(g, cup, s * 0.08f);
        g.setColour(col::cyan);
        drawChamfer(g, cup.reduced(s * 0.018f), s * 0.065f, 1.6f);
        g.setColour(col::red);
        g.fillEllipse(R(s * 0.07f, s * 0.07f).withCentre(cup.getCentre()));
    }
    // angry eyes: big white eyes, a dark pupil, and a heavy lid slanting down toward the middle
    for (int side = -1; side <= 1; side += 2)
    {
        const float sd = (float)side;
        const float ex = cx + sd * rad * 0.42f, ey = cy + rad * 0.14f;
        R eye = R(rad * 0.50f, rad * 0.56f).withCentre({ex, ey});
        g.setColour(juce::Colours::white);
        g.fillEllipse(eye);
        g.setColour(col::soot);
        g.fillEllipse(R(rad * 0.28f, rad * 0.33f).withCentre({ex - sd * rad * 0.05f, ey + rad * 0.07f}));
        g.setColour(juce::Colours::white);
        g.fillEllipse(R(rad * 0.09f, rad * 0.09f).withCentre({ex - sd * rad * 0.10f, ey - rad * 0.01f}));
        // the lid: outer end high, inner end low
        juce::Path lid;
        lid.startNewSubPath(ex + sd * rad * 0.36f, ey - rad * 0.12f);
        lid.lineTo(ex - sd * rad * 0.36f, ey + rad * 0.04f);
        lid.lineTo(ex - sd * rad * 0.36f, ey - rad * 0.50f);
        lid.lineTo(ex + sd * rad * 0.36f, ey - rad * 0.50f);
        lid.closeSubPath();
        g.saveState();
        g.reduceClipRegion(juce::Rectangle<int>((int)(ex - rad * 0.4f), (int)(ey - rad * 0.55f), (int)(rad * 0.8f), (int)(rad * 0.75f)));
        g.setColour(juce::Colour(0xffc81e27));
        g.fillPath(lid);
        g.restoreState();
        g.setColour(col::soot);
        g.drawLine(ex + sd * rad * 0.38f, ey - rad * 0.13f, ex - sd * rad * 0.38f, ey + rad * 0.045f, 3.2f);
    }
    // a small smirk and a thumbs-up hand
    juce::Path mouth;
    mouth.startNewSubPath(cx - rad * 0.22f, cy + rad * 0.58f);
    mouth.quadraticTo(cx + rad * 0.02f, cy + rad * 0.74f, cx + rad * 0.26f, cy + rad * 0.54f);
    g.setColour(col::soot);
    g.strokePath(mouth, juce::PathStrokeType(2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const juce::Point<float> hc(cx + rad * 0.98f, cy + rad * 0.86f);
    g.setColour(juce::Colour(0xffe9e3d5));
    g.fillEllipse(R(rad * 0.40f, rad * 0.34f).withCentre(hc));
    fillChamfer(g, R(rad * 0.13f, rad * 0.26f).withCentre({hc.x + rad * 0.02f, hc.y - rad * 0.26f}), rad * 0.07f);
    g.setColour(col::soot);
    g.drawEllipse(R(rad * 0.40f, rad * 0.34f).withCentre(hc), 1.2f);
}

// ---- the GHOST BASS wordmark, AKIRA cut: heavy condensed letters in Kaneda red with a hard soot shadow, speed lines, a serial ----
static juce::Path wordPath(const juce::String &t, float h, float &width)
{
    juce::GlyphArrangement ga;
    ga.addLineOfText(Brush(h * 0.8f), t, 0.f, 0.f);
    juce::Path p;
    ga.createPath(p);
    p.applyTransform(juce::AffineTransform::shear(-0.06f, 0.f));
    width = p.getBounds().getWidth();
    return p;
}
static void drawWordmark(juce::Graphics &g, float x, float y)
{
    float w1 = 0, w2 = 0;
    auto p1 = wordPath("GHOST", 44.f, w1);
    auto p2 = wordPath("BASS", 44.f, w2);
    p1.applyTransform(juce::AffineTransform::translation(x - p1.getBounds().getX(), y + 36.f - p1.getBounds().getBottom() + 0.f));
    p2.applyTransform(juce::AffineTransform::translation(x + 22.f - p2.getBounds().getX(), y + 68.f - p2.getBounds().getBottom()));
    // speed lines streaking off the left of BASS
    for (int k = 0; k < 4; ++k)
    {
        const float ly = p2.getBounds().getY() + 6.f + (float)k * 8.f, len = 30.f - (float)k * 5.f;
        g.setGradientFill(juce::ColourGradient(col::red.withAlpha(0.f), x + 18.f - len, ly, col::red.withAlpha(0.85f), x + 18.f, ly, false));
        g.fillRect(R(x + 18.f - len, ly, len, k == 1 ? 2.f : 1.f));
    }
    for (auto *p : {&p1, &p2})
    {
        g.setColour(col::soot);
        g.fillPath(*p, juce::AffineTransform::translation(3.f, 3.f));
        g.setColour(col::redDeep);
        g.fillPath(*p, juce::AffineTransform::translation(1.5f, 1.5f));
        const auto b = p->getBounds();
        if (col::light)
            g.setGradientFill(juce::ColourGradient(juce::Colour(0xff4a4790), b.getX(), b.getY(), juce::Colour(0xff23214d), b.getX(), b.getBottom(), false));
        else
            g.setGradientFill(juce::ColourGradient(juce::Colour(0xffff4b4f), b.getX(), b.getY(), juce::Colour(0xffc0141c), b.getX(), b.getBottom(), false));
        g.fillPath(*p);
        // a thin paper highlight across the top of the letters, like light catching painted metal
        g.saveState();
        g.reduceClipRegion(b.withHeight(b.getHeight() * 0.18f).toNearestInt());
        g.setColour(juce::Colours::white.withAlpha(0.22f));
        g.fillPath(*p);
        g.restoreState();
    }
    // the serial plate beside BASS
    const float sx = p2.getBounds().getRight() + 10.f, sy = p2.getBounds().getY() - 12.f;
    g.setColour(col::red);
    g.fillRect(R(sx, sy, 2.f, 30.f));
    tech(g, "GB-11", R(sx + 6.f, sy - 1.f, 80.f, 11.f), juce::Justification::centredLeft, col::ink.withAlpha(0.85f), 10.f);
    tech(g, "BASS UNIT", R(sx + 6.f, sy + 10.f, 80.f, 11.f), juce::Justification::centredLeft, col::dim.withAlpha(0.8f), 9.f);
    tech(g, "NEO-PHX", R(sx + 6.f, sy + 20.f, 80.f, 11.f), juce::Justification::centredLeft, col::cyan.withAlpha(0.7f), 9.f);
}

// plain words for the readout ("Filter 1 Cutoff" -> "filter")
static juce::String shortLabel(const std::string &full)
{
    juce::String s(full);
    static const char *tbl[][2] = {{"Filter 1 Cutoff", "filter"},      {"Filter 2 Cutoff", "filter 2"},
                                   {"Filter 1 Resonance", "resonance"}, {"Filter 2 Resonance", "resonance 2"},
                                   {"Waveshaper Drive", "drive"},       {"FM Depth", "FM"},
                                   {"Amp EG Decay", "decay"},           {"Amp EG Sustain", "sustain"},
                                   {"Amp EG Release", "release"},       {"Amp EG Attack", "attack"},
                                   {"Unison Detune", "detune"},         {"Low Cut", "low cut"}};
    for (auto &t : tbl)
        if (s.contains(t[0]))
            return s.replace(t[0], t[1]).trim();
    return s;
}
// "2681.49 Hz" -> "2.7 kHz", "850.00 Hz" -> "850 Hz", "0.123 s" -> "0.12 s"
static juce::String compactValue(const std::string &v)
{
    juce::String s(v);
    auto t = s.trim();
    int i = 0;
    while (i < t.length() && (juce::CharacterFunctions::isDigit(t[i]) || t[i] == '.' || t[i] == '-' || t[i] == '+'))
        ++i;
    if (i == 0)
        return t;
    double x = t.substring(0, i).getDoubleValue();
    auto unit = t.substring(i).trim();
    if (unit == "Hz" && std::fabs(x) >= 1000.0)
    {
        x /= 1000.0;
        unit = "kHz";
    }
    else if (unit == "ms" && std::fabs(x) >= 1000.0)
    {
        x /= 1000.0;
        unit = "s";
    }
    int dec = std::fabs(x) >= 100 ? 0 : (std::fabs(x) >= 1 ? 1 : 2);
    if (unit == "kHz")
        dec = 1;
    auto num = dec == 0 ? juce::String(juce::roundToInt(x)) : juce::String(x, dec);
    return num + (unit.isEmpty() ? "" : " " + unit);
}

// ------------------------------------------------------------------------------------------------
// a glowing pill button (build 9): glassy when off, filled with its colour (and glowing) when on; GENERATE is violet -> cyan
struct GButton : public juce::Component, public juce::SettableTooltipClient
{
    juce::String text;
    std::function<void()> onClick;
    bool toggled{false}, big{false}, accent{false}, dropdown{false};
    juce::Colour tint{col::violet};
    GButton(juce::String t = {}) : text(std::move(t)) { setMouseCursor(juce::MouseCursor::PointingHandCursor); }
    void paint(juce::Graphics &g) override
    {
        auto r = getLocalBounds().toFloat().reduced(2.5f);
        const float rad = std::min(10.f, r.getHeight() * 0.5f);
        bool over = isMouseOver(), down = isMouseButtonDown();
        const bool lit = accent || toggled;
        juce::Colour txt = col::ink;
        g.setColour(col::soot.withAlpha(0.8f));
        fillChamfer(g, r.translated(0.f, 2.f), rad);
        if (lit)
        {
            // lit: an illuminated plate in its colour (Kaneda red unless tinted), a soft bloom, dark lettering
            // lit (build 13): a dry-brush swash of paint in its colour (Kaneda red unless tinted), dark brush lettering
            const juce::Colour c = (accent ? col::red : themed(tint)).brighter(over ? (down ? 0.25f : 0.12f) : 0.f);
            brushSwash(g, r.expanded(4.f, 3.f), c, accent ? 0 : (int)(text.length() % 3), false);
            txt = col::light ? col::onLit : (accent ? col::ink : col::soot);
        }
        else
        {
            g.setGradientFill(juce::ColourGradient(col::cell.brighter(over ? 0.25f : 0.05f), r.getX(), r.getY(), col::panel.darker(0.25f), r.getX(),
                                                   r.getBottom(), false));
            fillChamfer(g, r, rad);
            g.setColour((over ? themed(tint) : col::line).withAlpha(over ? 0.95f : 0.9f));
            drawChamfer(g, r.reduced(0.5f), rad, over ? 1.5f : 1.1f);
            // the unlit indicator: a small dark LED at the left
            if (r.getWidth() > 70.f)
            {
                g.setColour(over ? themed(tint).withAlpha(0.8f) : col::line);
                g.fillRect(R(r.getX() + 7.f, r.getCentreY() - 3.f, 3.f, 6.f));
            }
        }
        g.setColour(txt);
        if (lit && (big || accent))
            g.setFont(Brush(std::min(r.getHeight() * 0.5f, big ? 21.f : 17.f)));
        else
            g.setFont(F(big ? 13.f : 10.f, big ? 2 : 1));
        auto tb = getLocalBounds().reduced((int)(rad * 0.45f) + 6, 0);
        if (dropdown)
        {
            // a small arrow at the right edge
            const float ax = r.getRight() - 12.f, ay = r.getCentreY();
            juce::Path ar;
            ar.startNewSubPath(ax - 4.f, ay - 2.f);
            ar.lineTo(ax, ay + 2.5f);
            ar.lineTo(ax + 4.f, ay - 2.f);
            g.strokePath(ar, juce::PathStrokeType(1.8f, juce::PathStrokeType::mitered, juce::PathStrokeType::square));
            tb.removeFromRight(10);
        }
        dt(g, text, tb, juce::Justification::centred);
    }
    void mouseUp(const juce::MouseEvent &e) override
    {
        if (e.mouseWasClicked() && onClick)
            onClick();
    }
    void mouseEnter(const juce::MouseEvent &) override { repaint(); }
    void mouseExit(const juce::MouseEvent &) override { repaint(); }
};

// ------------------------------------------------------------------------------------------------
struct EasyView::Impl
{
    SurgeSynthEditor *ed;
    SurgeSynthProcessor &proc;
    SurgeSynthesizer *synth;
    GhostEngine *eng;
    EasyView *view;
    int mode{M_BOARD};
    std::mt19937 rng{std::random_device{}()};
    std::function<void()> requestSave;
    std::function<void()> toggleTheme; // NIGHT <-> DAWN (build 14)
    std::string curPath; // the file the last picked sound came from (this editor only; never saved)

    Impl(SurgeSynthEditor *e, SurgeSynthProcessor &p, EasyView *v)
        : ed(e), proc(p), synth(p.surge.get()), eng(p.surge->ghost.get()), view(v)
    {
    }

    // ---- parameter helpers (UI thread) ----
    void setParam01(Parameter *p, float v01)
    {
        auto id = synth->idForParameter(p);
        auto it = proc.paramsByID.find(id);
        if (it != proc.paramsByID.end())
            it->second->beginChangeGesture();
        synth->setParameter01(id, v01, false, false);
        synth->sendParameterAutomation(id, synth->getParameter01(id));
        if (it != proc.paramsByID.end())
            it->second->endChangeGesture();
        synth->refresh_editor = true;
    }
    void setParamInt(Parameter *p, int v)
    {
        float n = p->val_max.i > p->val_min.i
                      ? (float)(v - p->val_min.i) / (float)(p->val_max.i - p->val_min.i)
                      : 0.f;
        setParam01(p, n);
    }
    void setParamValue(Parameter *p, float v)
    {
        Parameter t = *p;
        t.val.f = v;
        setParam01(p, t.get_value_f01());
    }
    void loadPatchFile(const fs::path &p)
    {
        curPath = path_to_string(p);
        if (auto *sge = ed->getSGE())
            sge->queuePatchFileLoad(path_to_string(p));
    }
    void loadBlob(const ghost::Blob &b, const std::string &name)
    {
        curPath.clear();
        ghost::dna::queueLoad(synth, b, name);
    }
    ghost::Blob current() { return ghost::dna::capture(synth); }

    // build 8 (PURE SURGE): the library is Surge XT's own - its factory patches and its third-party
    // patches, unchanged. Grouped by Surge's own category (the folder: "Basses", "Leads" ...), the factory
    // and every third-party author's patches of a category together; the author is shown with the name.
    struct Preset
    {
        std::string name, category, author, folder;
        fs::path path;
    };
    std::vector<Preset> presets()
    {
        std::vector<Preset> r;
        auto &st = synth->storage;
        for (auto i : st.patchOrdering)
        {
            auto &p = st.patch_list[i];
            if (p.category < 0 || p.category >= (int)st.patch_category.size())
                continue;
            std::string c = st.patch_category[p.category].name, author;
            std::string folder = c;
            std::replace(folder.begin(), folder.end(), '\\', '/');
            const auto sl = c.find_first_of("/\\");
            if (sl != std::string::npos)
            {
                author = c.substr(0, sl);
                c = c.substr(c.find_last_of("/\\") + 1);
            }
            r.push_back({p.name, c, author, folder, p.path});
        }
        return r;
    }
    // the bass folders first, then the rest in alphabetical order
    static bool bassFirst(const std::string &a, const std::string &b)
    {
        auto lower = [](std::string l) {
            std::transform(l.begin(), l.end(), l.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
            return l;
        };
        const std::string la = lower(a), lb = lower(b);
        const bool ba = la.find("bass") != std::string::npos, bb = lb.find("bass") != std::string::npos;
        if (ba != bb)
            return ba;
        return la < lb;
    }

    // ---- the library, indexed once: folders (a tree) and sounds, with a lower-case search text for each ----
    struct Index
    {
        struct Node
        {
            std::string name;
            int parent{-1};
            std::vector<int> folders, items;
            int total{0}; // sounds in this folder and below
        };
        std::vector<Preset> all;
        std::vector<std::string> hay, lname;
        std::vector<Node> nodes;
        std::vector<int> nodeOf;
        std::unordered_map<std::string, int> byPath, byName;
        // build 11b: the picker lists TYPES (BASS, LEAD, ...), each with ONE flat A-Z list of every sound of that type from every folder.
        // Every sound is in exactly one type; what cannot be told goes to OTHER (nothing is lost).
        struct Type
        {
            std::string name;
            std::vector<int> items;
        };
        std::vector<Type> types;
        std::vector<int> typeOf;
        static const std::vector<std::string> &typeNames()
        {
            static const std::vector<std::string> n = {"MY SOUNDS", "BASS", "LEAD", "PAD", "PLUCK", "KEYS", "DRUMS", "FX", "OTHER"};
            return n;
        }
        static int classify(const std::string &folder, const std::string &name)
        {
            // words of a text, lower case, a plural 's' removed ("Basses" -> "bass")
            auto words = [](const std::string &t) {
                std::vector<std::string> w;
                std::string cur;
                auto flush = [&] {
                    if (cur.empty())
                        return;
                    if (cur.size() > 4 && cur.compare(cur.size() - 4, 4, "sses") == 0)
                        cur.pop_back(), cur.pop_back(); // basses -> bass
                    else if (cur.size() > 3 && cur.back() == 's' && cur[cur.size() - 2] != 's')
                        cur.pop_back();
                    w.push_back(cur);
                    cur.clear();
                };
                for (unsigned char ch : t)
                {
                    if (std::isalnum(ch))
                        cur += (char)std::tolower(ch);
                    else
                        flush();
                }
                flush();
                return w;
            };
            static const std::vector<std::pair<int, std::vector<std::string>>> rules = {
                {1, {"bass", "sub", "808", "reese", "wobble", "growl"}},
                {2, {"lead", "solo"}},
                {3, {"pad", "string", "atmosphere", "atmospheric", "ambient", "drone", "choir", "swell", "evolving", "vocal"}},
                {4, {"pluck", "pizz", "harp", "guitar", "banjo", "kalimba"}},
                {5, {"key", "piano", "organ", "epiano", "rhodes", "clav", "bell", "mallet", "vibe", "marimba", "keyboard"}},
                {6, {"drum", "kick", "snare", "hat", "perc", "percussion", "tom", "clap", "rhythmic"}},
                {7, {"fx", "sfx", "effect", "riser", "impact", "noise", "sweep", "transition", "whoosh", "texture", "foley"}}};
            auto firstMatch = [&](const std::vector<std::string> &w) {
                for (auto &r : rules)
                    for (auto &x : w)
                        if (std::find(r.second.begin(), r.second.end(), x) != r.second.end())
                            return r.first;
                return -1;
            };
            const auto fw = words(folder);
            if (!fw.empty() && (fw[0] == "my" || fw[0] == "user" || fw[0] == "favourite" || fw[0] == "favorite" || fw[0] == "favourites"))
                return 0;
            int t = firstMatch(fw);
            if (t < 0)
                t = firstMatch(words(name));
            return t < 0 ? 8 : t;
        }
        void build(std::vector<Preset> ps)
        {
            all = std::move(ps);
            hay.clear();
            lname.clear();
            nodes.assign(1, Node{});
            nodeOf.assign(all.size(), 0);
            byPath.clear();
            byName.clear();
            auto lower = [](std::string l) {
                std::transform(l.begin(), l.end(), l.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
                return l;
            };
            std::map<std::string, int> by;
            for (size_t i = 0; i < all.size(); ++i)
            {
                const auto &p = all[i];
                int cur = 0;
                std::string acc;
                size_t pos = 0;
                while (!p.folder.empty() && pos <= p.folder.size())
                {
                    const size_t sl = p.folder.find('/', pos);
                    const std::string part = p.folder.substr(pos, sl == std::string::npos ? std::string::npos : sl - pos);
                    pos = sl == std::string::npos ? p.folder.size() + 1 : sl + 1;
                    if (part.empty())
                        continue;
                    acc += (acc.empty() ? "" : "/") + part;
                    auto it = by.find(acc);
                    if (it == by.end())
                    {
                        const int id = (int)nodes.size();
                        Node nn;
                        nn.name = part;
                        nn.parent = cur;
                        nodes.push_back(nn);
                        nodes[(size_t)cur].folders.push_back(id);
                        it = by.emplace(acc, id).first;
                    }
                    cur = it->second;
                }
                nodes[(size_t)cur].items.push_back((int)i);
                nodeOf[i] = cur;
                lname.push_back(lower(p.name));
                hay.push_back(lower(p.name + " " + p.author + " " + p.folder));
                byPath.emplace(path_to_string(p.path), (int)i);
                byName.emplace(p.name, (int)i);
            }
            types.clear();
            for (auto &n : typeNames())
                types.push_back({n, {}});
            typeOf.assign(all.size(), 8);
            for (size_t i = 0; i < all.size(); ++i)
            {
                typeOf[i] = classify(all[i].folder, all[i].name);
                types[(size_t)typeOf[i]].items.push_back((int)i);
            }
            for (auto &t : types)
                std::stable_sort(t.items.begin(), t.items.end(), [&](int a, int b) { return lname[(size_t)a] < lname[(size_t)b]; });
            types.erase(std::remove_if(types.begin(), types.end(), [](const Type &t) { return t.items.empty(); }), types.end());
            for (size_t ti = 0; ti < types.size(); ++ti)
                for (int i : types[ti].items)
                    typeOf[(size_t)i] = (int)ti;
            for (size_t n = 0; n < nodes.size(); ++n)
            {
                auto &nd = nodes[n];
                std::stable_sort(nd.items.begin(), nd.items.end(), [&](int a, int b) { return lname[(size_t)a] < lname[(size_t)b]; });
                std::stable_sort(nd.folders.begin(), nd.folders.end(), [&](int a, int b) {
                    return n == 0 ? bassFirst(nodes[(size_t)a].name, nodes[(size_t)b].name)
                                  : lower(nodes[(size_t)a].name) < lower(nodes[(size_t)b].name);
                });
            }
            for (int n = (int)nodes.size() - 1; n >= 0; --n) // (children always come after their parent)
            {
                auto &nd = nodes[(size_t)n];
                nd.total += (int)nd.items.size();
                if (nd.parent >= 0)
                    nodes[(size_t)nd.parent].total += nd.total;
            }
        }
    };
    Index index;
    bool indexed{false};
    size_t indexedCount{0};
    int indexBuilds{0};
    const Index &library()
    {
        auto &st = synth->storage;
        if (!indexed || indexedCount != st.patch_list.size())
        {
            index.build(presets());
            indexed = true;
            indexedCount = st.patch_list.size();
            ++indexBuilds;
        }
        return index;
    }
    void libraryChanged() { indexed = false; }
    // the library entry of the sound that is playing (the file it came from, else the name), or -1
    int currentIndex()
    {
        const auto &L = library();
        const auto &name = synth->storage.getPatch().name;
        if (!curPath.empty())
        {
            auto it = L.byPath.find(curPath);
            if (it != L.byPath.end() && L.all[(size_t)it->second].name == name)
                return it->second;
        }
        auto it = L.byName.find(name);
        return it == L.byName.end() ? -1 : it->second;
    }

    // ---- the sound picker (PresetMenu, on the canvas) ----
    struct PickResult
    {
        enum Kind { PATCH, CURRENT, EMPTY } kind{PATCH};
        std::string path, name;
    };
    struct MenuOpts
    {
        bool current{false}; // list "CURRENT SOUND" first
        bool empty{false};   // list "EMPTY THIS SLOT"
        std::string title;
    };
    std::function<void(R, const MenuOpts &, std::function<void(const PickResult &)>)> openMenu;
    // where a component sits on the canvas
    R canvasRect(juce::Component &c) const { return view->canvas.getLocalArea(&c, c.getLocalBounds()).toFloat(); }
};


// ------------------------------------------------------------------------------------------------
#include "GhostMenuUI.inc"

// the six macros
struct Macros : public juce::Component
{
    EasyView::Impl &I;
    int drag{-1};
    float dragStartV{0};
    juce::Point<float> dragStart;
    Macros(EasyView::Impl &i) : I(i) {}
    R knobRect(int k) const
    {
        float cw = getWidth() / 2.f, ch = (getHeight() - 52) / 3.f;
        return R((k % 2) * cw, 46 + (k / 2) * ch, cw, ch);
    }
    void paint(juce::Graphics &g) override
    {
        panelBg(g, getLocalBounds().toFloat());
        brushTitle(g, "GHOST MACROS", 28.f, 8.f, 28.f, col::red);
        tech(g, "CTRL-06 // LIVE", R((float)getWidth() - 132.f, 16.f, 106.f, 14.f), juce::Justification::centredRight);
        g.setColour(col::line.withAlpha(0.7f));
        g.fillRect(R(28.f, 38.f, (float)getWidth() - 56.f, 1.f));

        for (int k = 0; k < 6; ++k)
        {
            auto r = knobRect(k).reduced(8, 2);
            float v = I.synth->getMacroParameter01(k);
            auto c = juce::Point<float>(r.getCentreX(), r.getY() + r.getHeight() * 0.37f);
            float rad = std::min(r.getWidth() * 0.40f, r.getHeight() * 0.31f);
            float a0 = juce::MathConstants<float>::pi * 1.25f, a1 = juce::MathConstants<float>::pi * 2.75f;
            // the scale: 25 machined ticks, lit red up to the value
            for (int t = 0; t <= 24; ++t)
            {
                const float f = (float)t / 24.f, a = a0 + (a1 - a0) * f - juce::MathConstants<float>::halfPi;
                const bool on = f <= v + 1e-4f && v > 0.001f, major = t % 6 == 0;
                const float r0 = rad + 3.f, r1 = rad + (major ? 11.f : 7.f);
                g.setColour(on ? col::red : col::steel.withAlpha(major ? 0.9f : 0.55f));
                g.drawLine(c.x + std::cos(a) * r0, c.y + std::sin(a) * r0, c.x + std::cos(a) * r1, c.y + std::sin(a) * r1, major ? 2.f : 1.3f);
            }
            juce::Path tr, val;
            tr.addCentredArc(c.x, c.y, rad - 2.f, rad - 2.f, 0, a0, a1, true);
            g.setColour(col::soot);
            g.strokePath(tr, juce::PathStrokeType(4.f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
            if (v > 0.001f)
            {
                val.addCentredArc(c.x, c.y, rad - 2.f, rad - 2.f, 0, a0, a0 + (a1 - a0) * v, true);
                g.setColour(col::red.withAlpha(0.22f));
                g.strokePath(val, juce::PathStrokeType(9.f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
                g.setColour(col::red);
                g.strokePath(val, juce::PathStrokeType(3.f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
            }
            // the knob: a machined steel cap with a knurled rim
            const float kr = rad * 0.72f;
            g.setColour(col::soot.withAlpha(0.8f));
            g.fillEllipse(R(kr * 2 + 4, kr * 2 + 4).withCentre(c.translated(0.f, 2.f)));
            g.setGradientFill(juce::ColourGradient(juce::Colour(col::light ? 0xffffffff : 0xff3a4659), c.x - kr * 0.6f, c.y - kr,
                                                   juce::Colour(col::light ? 0xffcdc5e3 : 0xff070b14), c.x + kr * 0.5f, c.y + kr, false));
            g.fillEllipse(R(kr * 2, kr * 2).withCentre(c));
            g.setColour(col::light ? col::steel.withAlpha(0.35f) : juce::Colours::black.withAlpha(0.45f));
            for (int n = 0; n < 40; ++n)
            {
                const float a = (float)n / 40.f * juce::MathConstants<float>::twoPi;
                g.drawLine(c.x + std::cos(a) * kr * 0.86f, c.y + std::sin(a) * kr * 0.86f, c.x + std::cos(a) * kr, c.y + std::sin(a) * kr, 1.f);
            }
            g.setGradientFill(juce::ColourGradient(juce::Colour(col::light ? 0xfffcfaff : 0xff222c3d), c.x, c.y - kr * 0.7f,
                                                   juce::Colour(col::light ? 0xffe7e1f3 : 0xff0b1019), c.x, c.y + kr * 0.7f, false));
            g.fillEllipse(R(kr * 1.62f, kr * 1.62f).withCentre(c));
            g.setColour(col::steel.withAlpha(0.7f));
            g.drawEllipse(R(kr * 2, kr * 2).withCentre(c), 1.f);
            float ang = a0 + (a1 - a0) * v - juce::MathConstants<float>::halfPi;
            g.setColour(col::cyan.withAlpha(0.25f));
            g.drawLine(c.x + std::cos(ang) * kr * 0.25f, c.y + std::sin(ang) * kr * 0.25f, c.x + std::cos(ang) * kr * 0.8f, c.y + std::sin(ang) * kr * 0.8f, 6.f);
            g.setColour(col::cyan);
            g.drawLine(c.x + std::cos(ang) * kr * 0.25f, c.y + std::sin(ang) * kr * 0.25f, c.x + std::cos(ang) * kr * 0.8f, c.y + std::sin(ang) * kr * 0.8f, 2.4f);
            tech(g, "M-0" + juce::String(k + 1), R(r.getX() + 2.f, r.getY() + 2.f, 40.f, 12.f), juce::Justification::centredLeft, col::dim.withAlpha(0.7f), 9.f);
            // the label: what it does (or MACRO n), and the value as a sodium-amber readout
            juce::String nm = I.synth->storage.getPatch().CustomControllerLabel[k];
            if (nm.trim().isEmpty() || nm.trim() == "-" || nm.startsWithIgnoreCase("macro"))
                nm = juce::StringArray{"SUB", "GRIT", "BITE", "MOTION", "WIDTH", "SPACE"}[k];
            g.setFont(Brush(17.f));
            g.setColour(col::ink);
            dt(g, nm.toUpperCase(), R(r.getX(), c.y + rad + 6, r.getWidth(), 20), juce::Justification::centred);
            tech(g, juce::String((int)std::round(v * 100)).paddedLeft('0', 3) + " %", R(r.getX(), c.y + rad + 26, r.getWidth(), 14),
                 juce::Justification::centred, v > 0.001f ? col::amber : col::dim.withAlpha(0.7f), 12.f);
        }
    }
    void mouseDown(const juce::MouseEvent &e) override
    {
        for (int k = 0; k < 6; ++k)
            if (knobRect(k).contains(e.position))
            {
                drag = k;
                dragStart = e.position;
                dragStartV = I.synth->getMacroParameter01(k);
                I.proc.macrosById[k]->beginChangeGesture();
            }
    }
    void mouseDrag(const juce::MouseEvent &e) override
    {
        if (drag < 0)
            return;
        float v = juce::jlimit(0.f, 1.f, dragStartV + (dragStart.y - e.position.y) / 160.f);
        I.proc.macrosById[drag]->setValueNotifyingHost(v);
        repaint();
    }
    void mouseUp(const juce::MouseEvent &) override
    {
        if (drag >= 0)
            I.proc.macrosById[drag]->endChangeGesture();
        drag = -1;
    }
    void mouseDoubleClick(const juce::MouseEvent &e) override
    {
        for (int k = 0; k < 6; ++k)
            if (knobRect(k).contains(e.position))
                I.proc.macrosById[k]->setValueNotifyingHost(0.f);
    }
};

// ------------------------------------------------------------------------------------------------
// the bass sequencer strip
// the pattern as a .mid file (written on the message thread), for drag-out into FL Studio
static juce::File writePatternMidi(ghost::Sequencer &sq, double bpm, double tf, const juce::String &name)
{
    juce::MidiFile mf = ghost::patternMidiFile(sq, bpm, tf, name);
    auto dir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                   .getChildFile("Ghost Bass")
                   .getChildFile("MIDI");
    dir.createDirectory();
    auto clean = name.retainCharacters("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 -_").trim();
    auto f = dir.getChildFile((clean.isEmpty() ? juce::String("Ghost Bass") : clean) + " - " +
                              ghost::Sequencer::noteName(sq.root.load()) + " " +
                              ghost::Sequencer::scaleName(sq.scale.load()) + ".mid");
    f.deleteFile();
    juce::FileOutputStream os(f);
    if (os.openedOk())
        mf.writeTo(os, 0); // type 0: a single track
    return f;
}

struct MidiDragButton : public GButton
{
    std::function<juce::File()> make;
    bool dragged{false};
    MidiDragButton() : GButton("DRAG MIDI") { setMouseCursor(juce::MouseCursor::DraggingHandCursor); }
    void mouseDown(const juce::MouseEvent &) override { dragged = false; }
    void mouseDrag(const juce::MouseEvent &e) override
    {
        if (dragged || e.getDistanceFromDragStart() < 6 || !make)
            return;
        dragged = true;
        auto f = make();
        if (f.existsAsFile())
            juce::DragAndDropContainer::performExternalDragDropOfFiles({f.getFullPathName()}, false, this);
    }
    void mouseUp(const juce::MouseEvent &e) override
    {
        if (!dragged && e.mouseWasClicked() && make)
        {
            auto f = make();
            if (f.existsAsFile())
                f.revealToUser(); // a click saves it to Documents/Ghost Bass/MIDI and shows it
        }
        repaint();
    }
};

struct SeqStrip : public juce::Component
{
    EasyView::Impl &I;
    GButton playB{"PLAY"}, genB{"NEW PATTERN"}, styleB{"ANY STYLE"}, keyB{"KEY C"}, scaleB{"MINOR"},
        lenB{"16 STEPS"}, grooveB{"STRAIGHT"}, amtB{"GROOVE 50%"}, gateB{"GATE 55%"}, chordB{"CHORD"},
        lockB{"LOCK"}, voiceB{"POLY"}, glideB{"GLIDE OFF"}, prioB{"LAST NOTE"}, arpB{"ARP OFF"},
        midiOutB{"MIDI OUT"};
    // the arp's own controls (shown over the step grid while the arp is on)
    GButton arpRateB{"1/16"}, arpOctB{"1 OCT"}, arpGateB{"GATE 60%"}, arpSwingB{"SWING 0%"}, arpLatchB{"LATCH"};
    MidiDragButton midiB;
    ghost::Arpeggiator &A() { return I.synth->ghost->arp; }
    int style{-1};
    ghost::Sequencer &S() { return I.proc.ghostSeq(); }
    SurgeSceneStorage &scene() { return I.synth->storage.getPatch().scene[0]; }

    SeqStrip(EasyView::Impl &i) : I(i)
    {
        for (auto *b : {&playB, &genB, &styleB, &keyB, &scaleB, &lenB, &grooveB, &amtB, &gateB, &chordB, &lockB,
                        &voiceB, &glideB, &prioB, &arpB, &midiOutB})
            addAndMakeVisible(*b);
        for (auto *b : {&arpRateB, &arpOctB, &arpGateB, &arpSwingB, &arpLatchB})
            addChildComponent(*b);
        addAndMakeVisible(midiB);
        for (auto *b : {&styleB, &keyB, &scaleB, &lenB, &grooveB, &amtB, &gateB, &voiceB, &glideB, &prioB, &arpB})
            b->dropdown = true;
        arpB.tint = col::amber;
        midiOutB.tint = col::cyan;
        arpLatchB.tint = col::amber;
        // ARP: OFF -> UP -> DOWN -> UP-DOWN -> RANDOM -> CHORD -> AS PLAYED -> OFF
        arpB.onClick = [this]() {
            auto &a = A();
            if (!a.enabled.load())
            {
                a.mode.store(0);
                a.enabled.store(true);
            }
            else if (a.mode.load() + 1 >= ghost::Arpeggiator::n_modes)
                a.enabled.store(false);
            else
                a.mode.store(a.mode.load() + 1);
            refresh();
        };
        midiOutB.onClick = [this]() {
            auto &e = *I.synth->ghost;
            e.midiOut.store(!e.midiOut.load());
            refresh();
        };
        arpRateB.onClick = [this]() {
            A().rate.store((A().rate.load() + 1) % ghost::Arpeggiator::n_rates);
            refresh();
        };
        arpOctB.onClick = [this]() {
            A().octaves.store(A().octaves.load() % 4 + 1);
            refresh();
        };
        arpGateB.onClick = [this]() {
            static const float gs[] = {0.25f, 0.45f, 0.6f, 0.8f, 1.0f};
            float g = A().gate.load();
            int k = 0;
            while (k < 5 && gs[k] <= g + 0.01f)
                ++k;
            A().gate.store(gs[k % 5]);
            refresh();
        };
        arpSwingB.onClick = [this]() {
            float s = A().swing.load() + 0.25f;
            A().swing.store(s > 1.01f ? 0.f : s);
            refresh();
        };
        arpLatchB.onClick = [this]() {
            A().latch.store(!A().latch.load());
            if (!A().latch.load())
                A().clear();
            refresh();
        };
        playB.tint = col::slime;
        genB.accent = true;
        chordB.tint = col::pink;
        lockB.tint = col::cyan;
        playB.onClick = [this]() {
            S().playing.store(!S().playing.load());
            if (!S().playing.load())
                S().releaseAllHeld();
            refresh();
        };
        genB.onClick = [this]() {
            S().generate(S().scale.load(), 0.65f, (lastSeed = I.rng()), style);
            refresh();
        };
        styleB.onClick = [this]() {
            style = style + 1 >= ghost::Sequencer::n_styles ? -1 : style + 1;
            S().generate(S().scale.load(), 0.65f, (lastSeed = I.rng()), style);
            refresh();
        };
        keyB.onClick = [this]() {
            int r = S().root.load();
            S().root.store(r + 1 > 47 ? 36 : r + 1); // C2..B2
            refresh();
        };
        scaleB.onClick = [this]() {
            // re-voice the same rhythm in the new scale: regenerate with the same seed
            S().scale.store((S().scale.load() + 1) % ghost::Sequencer::n_scales);
            S().generate(S().scale.load(), 0.65f, lastSeed, style);
            refresh();
        };
        lenB.onClick = [this]() {
            int l = S().length.load() == 16 ? 32 : 16;
            S().length.store(l);
            if (l == 32)
                S().generate(S().scale.load(), 0.65f, (lastSeed = I.rng()), style); // a real two-bar phrase
            refresh();
        };
        grooveB.onClick = [this]() {
            S().groove.store((S().groove.load() + 1) % ghost::Sequencer::n_grooves);
            refresh();
        };
        amtB.onClick = [this]() {
            float a = S().swing.load() + 0.25f;
            S().swing.store(a > 1.01f ? 0.f : a);
            refresh();
        };
        gateB.onClick = [this]() {
            static const float gs[] = {0.25f, 0.4f, 0.55f, 0.7f, 0.9f};
            float g = S().gate.load();
            int k = 0;
            while (k < 5 && gs[k] <= g + 0.01f)
                ++k;
            S().gate.store(gs[k % 5]);
            refresh();
        };
        chordB.onClick = [this]() {
            S().chordMode.store(!S().chordMode.load());
            S().releaseAllHeld();
            if (S().chordMode.load() && !S().playing.load())
                S().playing.store(true);
            refresh();
        };
        lockB.onClick = [this]() {
            auto &e = *I.synth->ghost;
            e.seqLocked.store(!e.seqLocked.load());
            refresh();
        };
        voiceB.onClick = [this]() {
            int pm = scene().polymode.val.i;
            int next = pm == pm_poly ? pm_mono : (pm == pm_mono ? pm_mono_st : pm_poly);
            I.setParamInt(&scene().polymode, next);
            refresh();
        };
        glideB.onClick = [this]() {
            static const float ts[] = {0.f, 0.03f, 0.06f, 0.12f, 0.25f};
            float cur = scene().portamento.val.f; // log2(seconds), -8 = off
            int k = 0;
            for (int j = 0; j < 5; ++j)
                if (ts[j] > 0 && std::fabs(std::log2(ts[j]) - cur) < 0.3f)
                    k = j;
            k = (k + 1) % 5;
            I.setParamValue(&scene().portamento, ts[k] > 0 ? std::log2(ts[k]) : -8.f);
            refresh();
        };
        prioB.onClick = [this]() {
            auto &m = scene().monoVoicePriorityMode;
            m = m == ALWAYS_LATEST ? ALWAYS_LOWEST : (m == ALWAYS_LOWEST ? ALWAYS_HIGHEST : ALWAYS_LATEST);
            I.synth->storage.getPatch().isDirty = true;
            refresh();
        };
        midiB.make = [this]() {
            double bpm = I.synth->time_data.tempo > 1 ? I.synth->time_data.tempo : 120.0;
            return writePatternMidi(S(), bpm, I.synth->ghost->tempoFactor(), juce::String(I.synth->storage.getPatch().name));
        };
        if (!S().get(0).on && !S().get(2).on && !S().get(4).on)
            S().generate(0, 0.6f, 12345, ghost::Sequencer::OFFBEAT);
        refresh();
    }
    uint32_t lastSeed{12345};

    void refresh()
    {
        refreshText();
        repaint();
    }
    void refreshText()
    {
        auto &q = S();
        playB.toggled = q.playing.load();
        playB.text = playB.toggled ? "STOP" : "PLAY";
        styleB.text = style < 0 ? juce::String("ANY STYLE") : juce::String(ghost::Sequencer::styleName(style));
        keyB.text = juce::String("KEY ") + ghost::Sequencer::noteName(q.root.load());
        scaleB.text = ghost::Sequencer::scaleName(q.scale.load());
        lenB.text = juce::String(q.length.load()) + " STEPS";
        grooveB.text = ghost::Sequencer::grooveName(q.groove.load());
        amtB.text = "GROOVE " + juce::String(juce::roundToInt(q.swing.load() * 100)) + "%";
        gateB.text = "GATE " + juce::String(juce::roundToInt(q.gate.load() * 100)) + "%";
        chordB.toggled = q.chordMode.load();
        lockB.toggled = I.synth->ghost->seqLocked.load();
        int pm = scene().polymode.val.i;
        voiceB.text = pm == pm_poly ? "POLY" : (pm == pm_mono ? "MONO" : (pm == pm_mono_st ? "LEGATO" : "MONO+"));
        float pt = scene().portamento.val.f;
        glideB.text = pt <= -7.f ? juce::String("GLIDE OFF")
                                 : "GLIDE " + juce::String(juce::roundToInt(1000.f * std::pow(2.f, pt))) + "ms";
        auto m = scene().monoVoicePriorityMode;
        prioB.text = m == ALWAYS_LOWEST ? "LOW NOTE" : (m == ALWAYS_HIGHEST ? "HIGH NOTE" : "LAST NOTE");
        auto &a = A();
        const bool on = a.enabled.load();
        arpB.toggled = on;
        arpB.text = on ? juce::String("ARP ") + ghost::Arpeggiator::modeName(a.mode.load()) : juce::String("ARP OFF");
        midiOutB.toggled = I.synth->ghost->midiOut.load();
        arpRateB.text = ghost::Arpeggiator::rateName(a.rate.load());
        arpOctB.text = juce::String(a.octaves.load()) + (a.octaves.load() > 1 ? " OCTS" : " OCT");
        arpGateB.text = "GATE " + juce::String(juce::roundToInt(a.gate.load() * 100)) + "%";
        arpSwingB.text = "SWING " + juce::String(juce::roundToInt(a.swing.load() * 100)) + "%";
        arpLatchB.toggled = a.latch.load();
        for (auto *b : {&arpRateB, &arpOctB, &arpGateB, &arpSwingB, &arpLatchB})
            if (b->isVisible() != on)
                b->setVisible(on);
    }

    static constexpr float kGridTop = 12.f, kBtnH = 40.f, kBtnPitch = 46.f;
    R stepsArea() const
    {
        const float top = kGridTop + 3.f * kBtnPitch + 30.f;
        return R(14.f, top, (float)getWidth() - 28.f, (float)getHeight() - top - 14.f);
    }
    int stepAt(juce::Point<float> p) const
    {
        if (I.synth->ghost->arp.enabled.load())
            return -1; // the grid is the arp panel while the arp is on
        auto a = stepsArea();
        int len = I.proc.ghostSeq().length.load();
        if (!a.expanded(0, 2).contains(p))
            return -1;
        return juce::jlimit(0, len - 1, (int)((p.x - a.getX()) / (a.getWidth() / len)));
    }
    bool inGateLane(juce::Point<float> p) const { return p.y > stepsArea().getBottom() - 13; }
    void resized() override
    {
        const int gap = 6, x0 = 14, bh = (int)kBtnH;
        const int bw = (getWidth() - 2 * x0 - 5 * gap) / 6;
        auto place = [&](GButton &b, int col, int row, int span = 1) {
            b.setBounds(x0 + col * (bw + gap), (int)(kGridTop + (float)row * kBtnPitch), span * bw + (span - 1) * gap, bh);
        };
        place(playB, 0, 0);
        place(genB, 1, 0, 2);
        place(styleB, 3, 0);
        place(keyB, 4, 0);
        place(scaleB, 5, 0);
        place(lenB, 0, 1);
        place(grooveB, 1, 1);
        place(amtB, 2, 1);
        place(gateB, 3, 1);
        place(chordB, 4, 1);
        place(lockB, 5, 1);
        place(voiceB, 0, 2);
        place(glideB, 1, 2);
        place(prioB, 2, 2);
        place(midiB, 3, 2);
        place(arpB, 4, 2);
        place(midiOutB, 5, 2);
        auto a = stepsArea();
        const int ab = (int)(a.getWidth() - 16.f - 4.f * gap) / 5;
        int x = (int)a.getX() + 8;
        for (auto *b : {&arpRateB, &arpOctB, &arpGateB, &arpSwingB, &arpLatchB})
        {
            b->setBounds(x, (int)a.getY() + 34, ab, bh);
            x += ab + gap;
        }
    }
    void paint(juce::Graphics &g) override
    {
        refreshText(); // children paint after this, with the current state
        panelBg(g, getLocalBounds().toFloat(), 12.f);
        g.setColour(col::dim);
        g.setFont(F(8.5f, 1));
        auto a = stepsArea();
        auto &q = I.proc.ghostSeq();
        auto &ar = I.synth->ghost->arp;
        if (ar.enabled.load())
        {
            // the ARP panel: its settings (child buttons) and the notes it is cycling through
            g.setColour(col::card);
            fillChamfer(g, a, 6.f);
            g.setColour(col::amber);
            g.setFont(F(10.f, 2));
            dt(g, juce::String("ARPEGGIATOR  ") + ghost::Arpeggiator::modeName(ar.mode.load()) + "  -  " +
                           (ar.heldCount() ? juce::String(ar.heldCount()) + " notes held"
                                           : juce::String("hold notes on your keyboard")) +
                           (ar.latch.load() ? "  (LATCH)" : ""),
                       a.reduced(8, 4).withHeight(24), juce::Justification::left);
            g.setColour(col::dim);
            g.setFont(F(8.5f, 1));
            dt(g, "tempo-synced  |  MIDI OUT sends the notes to your DAW", a.reduced(8, 4).withTrimmedTop(34 + (int)kBtnH + 10).withHeight(18),
                       juce::Justification::left);
            const int ph = ar.playhead.load(), L = std::max(1, ar.heldCount() * ar.octaves.load());
            const float cw = std::min(28.f, (a.getWidth() - 16) / (float)L);
            for (int k = 0; k < L && ar.mode.load() != ghost::Arpeggiator::CHORD; ++k)
            {
                R c(a.getX() + 8 + k * cw, a.getBottom() - 20, cw - 2, 12);
                g.setColour(k == ph ? col::slime : col::line);
                fillChamfer(g, c, 2.f);
            }
            return;
        }
        g.setColour(col::lilac);
        g.setFont(F(8.f, 1));
        dt(g, "BASS SEQUENCER   click: on/off   shift: accent   alt: slide   cmd: x1-4   drag: note   bottom: gate",
                   R(a.getX(), a.getY() - 24.f, a.getWidth(), 20), juce::Justification::left);
        int len = q.length.load();
        int ph = q.playhead.load();
        float w = a.getWidth() / len;
        for (int i = 0; i < len; ++i)
        {
            auto s = q.get(i);
            R c(a.getX() + i * w + 1.f, a.getY(), w - 2.f, a.getHeight());
            g.setColour((i / 4) % 2 ? col::cell : col::card);
            fillChamfer(g, c, 3.f);
            R lane = c.withTrimmedTop(c.getHeight() - 11.f).reduced(1.f, 1.f);
            R body = c.withTrimmedBottom(13.f);
            if (s.on)
            {
                float h = juce::jlimit(0.12f, 1.f, 0.3f + (s.semi + 12 * s.oct) / 30.f);
                R b = body.withTrimmedTop(body.getHeight() * (1 - h)).reduced(1.5f, 1.f);
                const juce::Colour bc = s.accent ? col::pink : col::violet;
                g.setColour(bc.withAlpha(0.22f));
                fillChamfer(g, b.expanded(2.f), 4.f);
                g.setGradientFill(juce::ColourGradient(bc.brighter(0.35f), b.getX(), b.getY(), bc.darker(0.1f), b.getX(), b.getBottom(), false));
                for (int r = 0; r < s.ratchet; ++r)
                {
                    float rw = b.getWidth() / s.ratchet;
                    fillChamfer(g, R(b.getX() + r * rw, b.getY(), rw - (s.ratchet > 1 ? 1.5f : 0.f), b.getHeight()), 2.5f);
                }
                if (s.slide)
                {
                    g.setColour(col::cyan);
                    g.fillRect(R(c.getRight() - 3, b.getY() + 1, 6, 3));
                }
                // gate lane: how long the note holds
                static const float gw[4] = {0.3f, 0.55f, 0.85f, 1.f};
                float gl = s.gate == 1 ? q.gate.load() : gw[juce::jlimit(0, 3, s.gate)];
                g.setColour(s.gate == 3 ? col::cyan : col::lilac.withAlpha(0.8f));
                fillChamfer(g, lane.withWidth(lane.getWidth() * gl), 2.f);
            }
            else
            {
                g.setColour(col::line.withAlpha(0.5f));
                fillChamfer(g, lane.withWidth(3.f), 1.f);
            }
            if (i == ph)
            {
                g.setColour(col::slime.withAlpha(0.85f));
                drawChamfer(g, c, 3.f, 1.5f);
            }
        }
        if (q.chordMode.load())
        {
            g.setColour(col::pink);
            g.setFont(F(9.f, 2));
            dt(g, q.heldCount() ? "CHORD: FOLLOWING YOUR NOTES" : "CHORD: HOLD NOTES TO PLAY",
                       R(a.getX() + 8.f, a.getY() + 4.f, a.getWidth() - 16.f, 18.f), juce::Justification::topRight);
        }
    }
    int dragStep{-1}, dragSemi0{0};
    float dragY0{0};
    bool dragMoved{false};
    void mouseDown(const juce::MouseEvent &e) override
    {
        dragStep = stepAt(e.position);
        dragMoved = false;
        if (dragStep >= 0)
        {
            dragSemi0 = I.proc.ghostSeq().get(dragStep).semi;
            dragY0 = e.position.y;
        }
    }
    void mouseDrag(const juce::MouseEvent &e) override
    {
        if (dragStep < 0 || inGateLane(e.mouseDownPosition))
            return;
        int d = (int)std::lround((dragY0 - e.position.y) / 5.f);
        if (d == 0 && !dragMoved)
            return;
        dragMoved = true;
        auto s = I.proc.ghostSeq().get(dragStep);
        s.on = true;
        s.semi = juce::jlimit(-24, 24, dragSemi0 + d);
        I.proc.ghostSeq().set(dragStep, s);
        repaint();
    }
    void mouseUp(const juce::MouseEvent &e) override
    {
        int i = stepAt(e.position);
        if (i < 0 || dragMoved)
            return;
        auto s = I.proc.ghostSeq().get(i);
        if (inGateLane(e.position))
            s.gate = (s.gate + 1) % 4;
        else if (e.mods.isCommandDown() || e.mods.isCtrlDown())
            s.ratchet = s.ratchet % 4 + 1;
        else if (e.mods.isShiftDown())
            s.accent = !s.accent;
        else if (e.mods.isAltDown() || e.mods.isRightButtonDown())
            s.slide = !s.slide;
        else
            s.on = !s.on;
        I.proc.ghostSeq().set(i, s);
        repaint();
    }
    void mouseWheelMove(const juce::MouseEvent &e, const juce::MouseWheelDetails &w) override
    {
        int i = stepAt(e.position);
        if (i < 0)
            return;
        auto s = I.proc.ghostSeq().get(i);
        s.semi = juce::jlimit(-24, 24, s.semi + (w.deltaY > 0 ? 1 : -1));
        I.proc.ghostSeq().set(i, s);
        repaint();
    }
};

// ------------------------------------------------------------------------------------------------
struct Keys : public juce::MidiKeyboardComponent
{
    void applyColours()
    {
        setColour(whiteNoteColourId, juce::Colour(col::light ? 0xfffcfaff : 0xffdcd6c8));
        setColour(blackNoteColourId, juce::Colour(col::light ? 0xff3d3a78 : 0xff0a0f1c));
        setColour(keySeparatorLineColourId, col::light ? col::line : juce::Colour(0xff6b7280).withAlpha(0.6f));
        setColour(mouseOverKeyOverlayColourId, col::cyan.withAlpha(0.28f));
        setColour(keyDownOverlayColourId, col::red.withAlpha(0.88f)); // the played key lights Kaneda red (DAWN: rose)
        setColour(shadowColourId, juce::Colours::transparentBlack);
        setColour(upDownButtonBackgroundColourId, col::panel);
        setColour(upDownButtonArrowColourId, col::ink);
    }
    Keys(juce::MidiKeyboardState &s) : juce::MidiKeyboardComponent(s, horizontalKeyboard)
    {
        applyColours();
        setLowestVisibleKey(24);
        setKeyWidth(22.f);
        setOctaveForMiddleC(4);
        setWantsKeyboardFocus(false);
    }
    // build 13: pigment washes bleeding across the white keys (one painting spread over the whole keyboard)
    void drawWhiteNote(int note, juce::Graphics &g, juce::Rectangle<float> area, bool isDown, bool isOver, juce::Colour lineColour,
                       juce::Colour textColour) override
    {
        static juce::Image wash = art(Art::keyWash);
        if (wash.isValid() && getWidth() > 0)
        {
            const float sx = area.getX() / (float)getWidth() * (float)wash.getWidth(), sw = area.getWidth() / (float)getWidth() * (float)wash.getWidth();
            g.drawImage(wash, (int)area.getX(), (int)area.getY(), (int)std::ceil(area.getWidth()), (int)area.getHeight(), (int)sx, 0,
                        std::max(1, (int)sw), wash.getHeight());
        }
        juce::MidiKeyboardComponent::drawWhiteNote(note, g, area, isDown, isOver, lineColour, textColour);
    }
};


// ------------------------------------------------------------------------------------------------
struct MorphBar : public juce::Component, public juce::SettableTooltipClient
{
    juce::String name, left, right;
    std::function<float()> get;
    std::function<void(float)> set;
    std::function<juce::String(float)> text;
    std::function<float()> live; // a second marker: where the sound is now (or < 0)
    juce::Colour tint{col::violet};
    MorphBar(juce::String n, juce::String l = {}, juce::String r = {}) : name(std::move(n)), left(std::move(l)), right(std::move(r))
    {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    }
    R track() const { return getLocalBounds().toFloat().withTrimmedTop(18).reduced(2, 2); }
    void paint(juce::Graphics &g) override
    {
        const float v = get ? std::clamp(get(), 0.f, 1.f) : 0.f;
        g.setColour(col::dim);
        g.setFont(F(9.f, 1));
        dt(g, name + (text ? "  " + text(v) : juce::String()), R(0, 0, (float)getWidth(), 14), juce::Justification::left);
        auto t = track();
        g.setColour(col::cell);
        fillChamfer(g, t, t.getHeight() * 0.5f);
        g.setColour(themed(tint).withAlpha(0.85f));
        fillChamfer(g, t.withWidth(std::max(t.getHeight(), t.getWidth() * v)), t.getHeight() * 0.5f);
        if (live)
        {
            const float n = live();
            if (n >= 0.f)
            {
                const float x = t.getX() + t.getWidth() * std::clamp(n, 0.f, 1.f);
                g.setColour(col::ink);
                fillChamfer(g, R(x - 2.f, t.getY() - 3.f, 4.f, t.getHeight() + 6.f), 2.f);
            }
        }
        g.setColour(col::bg);
        g.setFont(F(9.5f, 2));
        if (left.isNotEmpty())
            dt(g, left, t.reduced(10, 0), juce::Justification::centredLeft);
        g.setColour(col::ink.withAlpha(0.8f));
        if (right.isNotEmpty())
            dt(g, right, t.reduced(10, 0), juce::Justification::centredRight);
    }
    void drag(const juce::MouseEvent &e)
    {
        auto t = track();
        if (set)
            set(std::clamp((e.position.x - t.getX()) / std::max(1.f, t.getWidth()), 0.f, 1.f));
        repaint();
    }
    void mouseDown(const juce::MouseEvent &e) override { drag(e); }
    void mouseDrag(const juce::MouseEvent &e) override { drag(e); }
};

// TERRAIN: the landscape that moves the bass's own Surge controls (GhostTerrain.h), drawn and shaped by the shared
// TERRAIN component in Ghost Bass's colours. Across = frequency (120 Hz .. 12 kHz), back = later in
// the note, height = resonance, width = Q, roughness = drive. Dragging changes the sound live; LISTEN
// holds a note so you can hear it with nothing playing.
struct TerrainCard : public juce::Component, private juce::Timer
{
    EasyView::Impl &I;
    ghost::terrain::TerrainEditor ed;
    ghost::terrain::TerrainView view;
    ghost::terrain::SpectrumTap tap;
    uint64_t seenStamp{~0ull};
    int scopeRead{0};
    bool listening{false}, autoListen{false};
    int noMouseTicks{0}, mappedCount{0}, slowTick{0};
    // build 9: what the land is moving right now (the most-moved Surge controls, from the audio thread's own numbers)
    std::vector<std::pair<juce::String, float>> moving;
    MorphBar moveBar{"MOVEMENT", "", ""}, hzBar{"FREE SPEED", "", ""};
    // SHAKE (bounded random offsets on the mapped controls, undoable): it lives with the land it shakes
    GButton shakeB{"SHAKE"}, shakeUndoB{"UNDO"};
    std::vector<std::vector<std::pair<std::string, float>>> shakeUndo;
    std::array<GButton, ghost::kMoveRates> rateB;
    // every mouse-up anywhere on the page, a second chance to end the drag's note (the view's own mouse-up comes first)
    struct Watch : public juce::MouseListener
    {
        TerrainCard &c;
        explicit Watch(TerrainCard &cc) : c(cc) {}
        void mouseUp(const juce::MouseEvent &) override
        {
            juce::Component::SafePointer<TerrainCard> safe(&c);
            juce::Timer::callAfterDelay(40, [safe] {
                if (safe != nullptr)
                    safe->settle();
            });
        }
    } watch{*this};

    void refreshPalette()
    {
        auto p = col::light ? ghost::terrain::Palette::ghostBassLight() : ghost::terrain::Palette::ghostBass();
        p.ground = col::bg;
        p.font = [](float h, bool bold) { return F(h, bold ? 2 : 0); };
        view.setPalette(p);
        view.repaint();
    }
    explicit TerrainCard(EasyView::Impl &i) : I(i), view(ed, i.eng->terrain.getRange())
    {
        refreshPalette();
        view.setTimeLabel("NOTE");
        view.setTooltip("TERRAIN: tap the land for a mountain (or pull the ground up; down carves a valley). Drag a dot or "
                        "its line to move it. The chosen one shows its height tag (drag up / down) and size tag (sideways). "
                        "Every smooth control of the Surge sound sits somewhere on the land (oscillator shapes, both filters, drive, "
                        "envelopes, LFOs, the effects): a mountain here, a valley there, moves a different mix of the sliders, and "
                        "you can watch them move on the ENGINE page. Nothing is added on top. It plays while you pull, and what you "
                        "hear while you pull is what the sound settles to.");
        addAndMakeVisible(view);
        addAndMakeVisible(shakeB);
        addAndMakeVisible(shakeUndoB);
        shakeB.tint = col::amber;
        shakeB.setTooltip("SHAKE: small random moves of the controls on the land (undo brings them back)");
        shakeB.onClick = [this] {
            shakeUndo.push_back(I.eng->getShake());
            if (shakeUndo.size() > 64)
                shakeUndo.erase(shakeUndo.begin());
            I.eng->shake(0.6f, I.rng());
        };
        shakeUndoB.onClick = [this] {
            if (shakeUndo.empty())
            {
                I.eng->clearShake();
                return;
            }
            I.eng->setShake(shakeUndo.back());
            shakeUndo.pop_back();
        };
        ed.setWithoutUndo(I.eng->terrain.get());
        ed.onChange = [this] {
            I.eng->terrain.set(ed.terrain());
            // a drag plays the bass while you pull (C2, by the audio thread), whatever else is going on
            if (ed.inGesture() && !autoListen)
            {
                autoListen = true;
                audition();
            }
        };
        ed.onCommit = [this] {
            I.eng->terrain.set(ed.terrain());
            I.eng->terrain.setPageState(ed.stateToString());
        };
        // EVERY gesture end (also one that changed nothing: a pull that came back flat, a drag back to where it began): the
        // land the engine plays is the last one heard, and the drag's note ends. (build 8's note only ended on a real edit)
        ed.onGestureEnd = [this] {
            I.eng->terrain.set(ed.terrain());
            I.eng->terrain.setPageState(ed.stateToString());
            autoListen = false;
            audition();
        };
        view.onListen = [this] { listen(!listening); };
        view.isListening = [this] { return listening; };
        view.addMouseListener(&watch, true);

        moveBar.tint = col::cyan;
        moveBar.get = [this] { return I.eng->terrain.getMovement(); };
        moveBar.set = [this](float v) { I.eng->terrain.setMovement(v); };
        moveBar.text = [](float v) { return juce::String(juce::roundToInt(v * 100.f)) + "%"; };
        addAndMakeVisible(moveBar);
        hzBar.tint = col::pink;
        hzBar.get = [this] { return std::log(std::max(0.1f, I.eng->terrain.getMoveFreeHz()) / 0.1f) / std::log(80.f); };
        hzBar.set = [this](float v) { I.eng->terrain.setMoveFreeHz(0.1f * std::pow(80.f, v)); };
        hzBar.text = [this](float) { return juce::String(I.eng->terrain.getMoveFreeHz(), 2) + " Hz"; };
        addAndMakeVisible(hzBar);
        for (int k = 0; k < ghost::kMoveRates; ++k)
        {
            rateB[(size_t)k].text = ghost::moveRateName(k);
            rateB[(size_t)k].tint = col::cyan;
            rateB[(size_t)k].setTooltip(k == ghost::kMoveFree ? "FREE: the wandering runs at its own speed (Hz), not the tempo's"
                                                              : "how often the wandering picks new places, in beats: it follows the DAW tempo");
            rateB[(size_t)k].onClick = [this, k] {
                I.eng->terrain.setMoveRate(k);
                refreshRate();
            };
            addAndMakeVisible(rateB[(size_t)k]);
        }
        moveBar.setTooltip("MOVEMENT: random things moving around. 0 = only the land's own shape moves the sound; up = the controls under "
                           "the mountains wander on their own, in time with the tempo (the sound's own controls, within safe ranges)");
        refreshRate();
        startTimerHz(30);
    }
    ~TerrainCard() override
    {
        view.removeMouseListener(&watch);
        stopTimer();
        view.interaction.cancel();
        listening = autoListen = false;
        audition();
    }
    void refreshRate()
    {
        const int r = I.eng->terrain.getMoveRate();
        for (int k = 0; k < ghost::kMoveRates; ++k)
            rateB[(size_t)k].toggled = k == r;
        repaint();
    }
    // the audio thread plays / ends the note from this one flag (SurgeSynthProcessor::ghostSeqTick)
    void audition() { I.eng->audition.want.store(listening || autoListen, std::memory_order_release); }
    void listen(bool on)
    {
        listening = on;
        audition();
        view.repaint();
    }
    // whatever else ended: no press can be going on any more, so no drag note either
    void settle()
    {
        if (ed.inGesture())
            view.interaction.cancel(); // (ends the gesture: onGestureEnd settles the land and the note)
        if (autoListen)
        {
            autoListen = false;
            audition();
        }
    }
    void visibilityChanged() override
    {
        if (!isVisible())
        {
            settle();
            listen(false); // (leaving the page: LISTEN stops too)
        }
    }
    void parentHierarchyChanged() override
    {
        if (getParentComponent() == nullptr)
        {
            settle();
            listen(false);
        }
    }
    void focusLost(FocusChangeType) override { settle(); }
    // the engine is the owner: a patch load (or the DAW) hands the page a new landscape
    void syncFromEngine()
    {
        const auto st = I.eng->terrain.loadStamp();
        if (st == seenStamp)
            return;
        seenStamp = st;
        const auto page = I.eng->terrain.getPageState();
        if (page.empty() || !ed.stateFromString(page))
            ed.setWithoutUndo(I.eng->terrain.get());
        if (ed.terrain() != I.eng->terrain.get())
            ed.setWithoutUndo(I.eng->terrain.get()); // (the page state and the live land disagree: the land wins)
        mappedCount = 0;
        for (auto &c : I.eng->terrainCoverage())
            mappedCount += c.kind != ghost::TK_SKIP;
        refreshRate();
        moveBar.repaint();
        view.settle();
        view.repaint();
    }
    void updateMoving()
    {
        auto &pp = I.synth->storage.getPatch().param_ptr;
        std::array<std::pair<float, int>, 8> top{};
        int n = 0;
        const int cnt = std::min(I.eng->movedCount(), (int)pp.size());
        for (int id = 0; id < cnt; ++id)
        {
            const float a = std::fabs(I.eng->movedAmount(id));
            if (a < 0.004f)
                continue;
            if (n < 8)
                top[(size_t)n++] = {a, id};
            else
            {
                size_t mn = 0;
                for (size_t k = 1; k < 8; ++k)
                    if (top[k].first < top[mn].first)
                        mn = k;
                if (a > top[mn].first)
                    top[mn] = {a, id};
            }
        }
        std::sort(top.begin(), top.begin() + n, [](auto &x, auto &y) { return x.first > y.first; });
        moving.clear();
        for (int k = 0; k < n; ++k)
        {
            auto *q = pp[(size_t)top[(size_t)k].second];
            juce::String nm = q->get_full_name();
            if (q->scene == 1)
                nm = "A " + nm;
            else if (q->scene == 2)
                nm = "B " + nm;
            moving.emplace_back(nm.toUpperCase(), std::min(1.f, top[(size_t)k].first * 4.f));
        }
    }
    void timerCallback() override
    {
        syncFromEngine();
        // safety net: a mouse-up that never arrived (released off the window, focus taken, a touch cancelled)
        if (autoListen || ed.inGesture())
        {
            bool down = juce::ModifierKeys::getCurrentModifiersRealtime().isAnyMouseButtonDown();
            for (auto &ms : juce::Desktop::getInstance().getMouseSources())
                down = down || ms.isDragging();
            noMouseTicks = down ? 0 : noMouseTicks + 1;
            if (noMouseTicks > 6)
            {
                noMouseTicks = 0;
                settle();
            }
        }
        else
            noMouseTicks = 0;
        if (!isShowing())
        {
            settle();
            return;
        }
        auto &T = I.eng->terrain;
        view.setMeters(T.meters());
        view.setDisplayPhase(ed.terrain().evolve > 0.f ? T.phase() : -1.f);
        view.setPlayhead(T.active() && T.depth() < 1.f ? T.depth() : -1.f);
        if (++slowTick % 3 == 0)
        {
            updateMoving();
            moveBar.repaint();
            repaint(stripBounds().toNearestInt());
        }
        if (view.analyzerVisible())
        {
            // the last output samples (the engine's scope) into the analyzer
            const int w = I.eng->scopePos.load(std::memory_order_acquire);
            int n = (w - scopeRead + GhostEngine::kScope) & (GhostEngine::kScope - 1);
            if (n > 0)
            {
                float buf[GhostEngine::kScope];
                for (int k = 0; k < n; ++k)
                    buf[k] = I.eng->scope[(scopeRead + k) & (GhostEngine::kScope - 1)];
                const float *ch[1] = {buf};
                tap.push(ch, 1, n);
                scopeRead = w;
                std::vector<float> db;
                if (tap.read(db, 160, view.range, I.synth->storage.samplerate > 0 ? I.synth->storage.samplerate : 48000.0))
                    view.setSpectrum(std::move(db));
            }
        }
    }
    static constexpr int kStrip = 122; // MOVEMENT row + the most-moved controls
    R stripBounds() const { return getLocalBounds().toFloat().reduced(8).removeFromBottom((float)kStrip); }
    void resized() override
    {
        auto r = getLocalBounds().reduced(8);
        auto strip = r.removeFromBottom(kStrip);
        {
            auto top = r.withHeight(34).withTrimmedRight(6);
            shakeUndoB.setBounds(top.removeFromRight(96).withSizeKeepingCentre(94, 30));
            top.removeFromRight(6);
            shakeB.setBounds(top.removeFromRight(110).withSizeKeepingCentre(108, 30));
        }
        view.setBounds(r.withTrimmedTop(38).withTrimmedBottom(4));
        auto row = strip.removeFromTop(38);
        moveBar.setBounds(row.removeFromLeft(std::max(160, row.getWidth() * 3 / 10)));
        hzBar.setBounds(row.removeFromRight(std::max(120, row.getWidth() / 5)));
        row.removeFromLeft(8);
        row.removeFromRight(8);
        row = row.withTrimmedTop(16);
        const int bw = row.getWidth() / ghost::kMoveRates;
        for (int k = 0; k < ghost::kMoveRates; ++k)
            rateB[(size_t)k].setBounds(row.removeFromLeft(bw).reduced(1, 0));
    }
    void paint(juce::Graphics &g) override
    {
        panelBg(g, getLocalBounds().toFloat());
        brushTitle(g, "TERRAIN", 18.f, 6.f, 24.f);
        g.setFont(F(9.5f, 1));
        g.setColour(col::dim);
        const auto &t = ed.terrain();
        juce::String st = t.empty() ? juce::String("flat land: the bass as it is  -  tap the land for a mountain, or pick SHAPES")
                                    : juce::String::formatted("%d %s on the bass%s%s  -  every smooth Surge control sits on the land: a mountain here, a valley there, moves a different mix",
                                                              t.size(), t.size() == 1 ? "mountain" : "mountains",
                                                              t.evolve > 0.f ? ", drifting" : "", t.frozen ? ", frozen" : "");
        dt(g, st, R(180, 10, (float)getWidth() - 180.f - 240.f, 22), juce::Justification::left);
        g.setColour(view.palette.ground);
        fillChamfer(g, getLocalBounds().toFloat().reduced(8).withTrimmedTop(38).withTrimmedBottom((float)kStrip), 10.f);
        // the movement row's label for the rates, and what is moving
        auto strip = stripBounds();
        auto row = strip.removeFromTop(38);
        g.setColour(col::dim);
        g.setFont(F(9.f, 1));
        row.removeFromLeft(std::max(160.f, row.getWidth() * 0.3f) + 8.f);
        dt(g, "MOVEMENT RATE  (in beats at the DAW tempo, or FREE)", row.withHeight(14.f).withTrimmedRight(std::max(120.f, row.getWidth() / 5.f)), juce::Justification::left);
        strip.removeFromTop(2);
        g.setColour(col::dim);
        g.setFont(F(9.f, 1));
        dt(g, juce::String::formatted("MOVING NOW  -  %d of Surge's own controls are on the land (watch them on the ENGINE page)", mappedCount),
                   strip.removeFromTop(14.f), juce::Justification::left);
        const float cw = strip.getWidth() / 4.f, ch = strip.getHeight() / 2.f;
        for (size_t k = 0; k < moving.size() && k < 8; ++k)
        {
            R c(strip.getX() + cw * (float)(k % 4), strip.getY() + ch * (float)(k / 4), cw, ch);
            c = c.reduced(4, 1);
            g.setColour(col::ink.withAlpha(0.85f));
            g.setFont(F(9.f, 1));
            dt(g, moving[k].first, c.removeFromTop(c.getHeight() * 0.55f), juce::Justification::centredLeft, true);
            g.setColour(col::cell);
            fillChamfer(g, c.withTrimmedTop(1.f).withHeight(5.f), 2.5f);
            g.setColour(col::cyan.withAlpha(0.9f));
            fillChamfer(g, c.withTrimmedTop(1.f).withHeight(5.f).withWidth(std::max(5.f, c.getWidth() * moving[k].second)), 2.5f);
        }
    }
};

// ------------------------------------------------------------------------------------------------
// MORPH (build 7): blend two sounds with Surge's own engine (GhostMorph.h): pick A and B, BLEND,
// PENDULUM (a swing synced to the DAW tempo, or free in Hz) and DRAW YOUR OWN (a curve held and
// played by Surge's MSEG engine; your drawing is fitted with Surge's own freehand fit).
struct MorphPad : public juce::Component
{
    ghost::Morph &M;
    std::vector<std::pair<float, float>> pts; // the drawing in progress (t, v)
    bool drawing{false};
    std::function<void()> onDrawn;
    explicit MorphPad(ghost::Morph &m) : M(m) { setMouseCursor(juce::MouseCursor::CrosshairCursor); }
    R area() const { return getLocalBounds().toFloat().reduced(4); }
    void paint(juce::Graphics &g) override
    {
        auto a = area();
        g.setColour(col::bg);
        fillChamfer(g, a, 10.f);
        g.setColour(col::line.withAlpha(0.5f));
        const int bars = M.drawBars.load();
        for (int k = 1; k < bars * 4; ++k)
        {
            const float x = a.getX() + a.getWidth() * k / (bars * 4.f);
            g.setColour(col::line.withAlpha(k % 4 == 0 ? 0.6f : 0.22f));
            g.drawVerticalLine((int)x, a.getY() + 4, a.getBottom() - 4);
        }
        g.setColour(col::dim);
        g.setFont(F(9.f, 2));
        dt(g, "B", a.reduced(8, 4), juce::Justification::topLeft);
        dt(g, "A", a.reduced(8, 4), juce::Justification::bottomLeft);
        g.setFont(F(8.5f, 1));
        dt(g, "draw your A - B curve here: left to right is the loop", a.reduced(10, 4), juce::Justification::topRight);
        // Surge's MSEG engine draws the curve: what you see is what plays
        const auto ms = M.curve();
        juce::Path p;
        const int N = 160;
        for (int i = 0; i <= N; ++i)
        {
            const float t = i / (float)N, v = ghost::Morph::curveAt(ms, t);
            const float x = a.getX() + a.getWidth() * t, y = a.getBottom() - 6 - (a.getHeight() - 12) * v;
            if (i == 0)
                p.startNewSubPath(x, y);
            else
                p.lineTo(x, y);
        }
        g.setColour(M.drawOn.load() ? col::cyan : col::dim);
        g.strokePath(p, juce::PathStrokeType(2.2f));
        if (drawing && pts.size() > 1)
        {
            juce::Path q;
            for (size_t i = 0; i < pts.size(); ++i)
            {
                const float x = a.getX() + a.getWidth() * pts[i].first, y = a.getBottom() - 6 - (a.getHeight() - 12) * pts[i].second;
                if (i == 0)
                    q.startNewSubPath(x, y);
                else
                    q.lineTo(x, y);
            }
            g.setColour(col::amber);
            g.strokePath(q, juce::PathStrokeType(1.6f));
        }
        if (M.drawOn.load())
        {
            const float x = a.getX() + a.getWidth() * M.phase();
            g.setColour(col::ink.withAlpha(0.7f));
            g.drawVerticalLine((int)x, a.getY() + 2, a.getBottom() - 2);
        }
    }
    void add(const juce::MouseEvent &e)
    {
        auto a = area();
        const float t = std::clamp((e.position.x - a.getX()) / a.getWidth(), 0.f, 1.f);
        const float v = std::clamp((a.getBottom() - 6 - e.position.y) / (a.getHeight() - 12), 0.f, 1.f);
        if (!pts.empty() && t < pts.back().first) // drawing back over itself: the newer stroke wins
            while (!pts.empty() && pts.back().first >= t)
                pts.pop_back();
        pts.emplace_back(t, v);
        repaint();
    }
    void mouseDown(const juce::MouseEvent &e) override
    {
        pts.clear();
        drawing = true;
        add(e);
    }
    void mouseDrag(const juce::MouseEvent &e) override { add(e); }
    void mouseUp(const juce::MouseEvent &) override
    {
        drawing = false;
        if (pts.size() >= 3)
        {
            M.setCurve(ghost::Morph::curveFromDrawing(pts));
            M.drawOn = true;
            if (onDrawn)
                onDrawn();
        }
        pts.clear();
        repaint();
    }
};

struct MorphCard : public juce::Component, private juce::Timer
{
    EasyView::Impl &I;
    // sound A / sound B: a picked file, or the sound that is playing now (kept in the user folder when the morph is made)
    struct Pick
    {
        bool set{false}, current{false};
        std::string path, name;
    } pickA_, pickB_;
    GButton pickA{"THE SOUND PLAYING NOW"}, pickB{"PICK SOUND B"};
    GButton makeB{"MAKE THE MORPH"}, pendB{"PENDULUM"}, freeB{"FREE (Hz)"}, drawB{"DRAW YOUR OWN"}, playB{"PLAY"};
    std::array<GButton, ghost::Morph::kRates> rateB;
    std::array<GButton, ghost::Morph::n_feels> feelB;
    std::array<GButton, ghost::Morph::n_shapes> shapeB;
    std::array<GButton, 4> barsB;
    MorphBar blendBar{"BLEND", "A", "B"}, swingBar{"SWING"}, hzBar{"SPEED"};
    MorphPad pad;
    juce::String status;
    bool playing{false};
    static constexpr int kPlayKey = 33;
    ghost::Morph &M() { return I.eng->morph; }

    explicit MorphCard(EasyView::Impl &i) : I(i), pad(i.eng->morph)
    {
        for (juce::Component *c : std::initializer_list<juce::Component *>{&pickA, &pickB, &makeB, &pendB, &freeB, &drawB, &playB, &blendBar,
                                                                            &swingBar, &hzBar, &pad})
            addAndMakeVisible(c);
        for (int k = 0; k < ghost::Morph::kRates; ++k)
        {
            rateB[k].text = ghost::Morph::rateName(k);
            rateB[k].tint = col::cyan;
            rateB[k].onClick = [this, k] {
                M().pendRate = k;
                M().pendFree = false;
                M().pendOn = true;
                M().drawOn = false;
                sync();
            };
            addAndMakeVisible(rateB[k]);
        }
        for (int k = 0; k < ghost::Morph::n_feels; ++k)
        {
            feelB[k].text = ghost::Morph::feelName(k);
            feelB[k].tint = col::lilac;
            feelB[k].onClick = [this, k] {
                M().pendFeel = k;
                sync();
            };
            addAndMakeVisible(feelB[k]);
        }
        static const char *shapeTip[ghost::Morph::n_shapes] = {"a smooth swing", "a straight line there and back", "a jump from one to the other",
                                                               "somewhere new each half"};
        for (int k = 0; k < ghost::Morph::n_shapes; ++k)
        {
            shapeB[k].text = ghost::Morph::shapeName(k);
            shapeB[k].tint = col::amber;
            shapeB[k].setTooltip(shapeTip[k]);
            shapeB[k].onClick = [this, k] {
                M().pendShape = k;
                M().pendOn = true;
                M().drawOn = false;
                sync();
            };
            addAndMakeVisible(shapeB[k]);
        }
        static const int bars[4] = {1, 2, 4, 8};
        for (int k = 0; k < 4; ++k)
        {
            barsB[k].text = juce::String(bars[k]) + (bars[k] == 1 ? " BAR" : " BARS");
            barsB[k].tint = col::cyan;
            barsB[k].onClick = [this, k] {
                M().drawBars = bars[k];
                sync();
            };
            addAndMakeVisible(barsB[k]);
        }
        makeB.accent = true;
        makeB.onClick = [this] { make(); };
        pendB.tint = col::cyan;
        pendB.onClick = [this] {
            M().pendOn = !M().pendOn.load();
            if (M().pendOn.load())
                M().drawOn = false;
            sync();
        };
        freeB.tint = col::slime;
        freeB.onClick = [this] {
            M().pendFree = !M().pendFree.load();
            M().pendOn = true;
            M().drawOn = false;
            sync();
        };
        drawB.tint = col::cyan;
        drawB.onClick = [this] {
            M().drawOn = !M().drawOn.load();
            if (M().drawOn.load())
                M().pendOn = false;
            sync();
        };
        playB.onClick = [this] { play(!playing); };
        blendBar.get = [this] { return M().blend.load(); };
        blendBar.set = [this](float v) { M().blend = v; };
        blendBar.live = [this] { return M().mode() != ghost::Morph::M_OFF ? M().now() : -1.f; };
        blendBar.text = [](float v) { return juce::String(juce::roundToInt(100 * (1 - v))) + "% A / " + juce::String(juce::roundToInt(100 * v)) + "% B"; };
        swingBar.get = [this] { return M().swing.load(); };
        swingBar.set = [this](float v) { M().swing = v; };
        swingBar.text = [](float v) { return v > 0.98f ? juce::String("all the way from A to B") : juce::String(juce::roundToInt(v * 100)) + "% of the way"; };
        swingBar.tint = col::amber;
        hzBar.get = [this] { return std::log(M().pendHz.load() / 0.05f) / std::log(200.f); };
        hzBar.set = [this](float v) { M().pendHz = 0.05f * std::pow(200.f, v); };
        hzBar.text = [this](float) { return juce::String(M().pendHz.load(), 2) + " Hz"; };
        hzBar.tint = col::slime;
        pad.onDrawn = [this] { sync(); };
        pickA.dropdown = pickB.dropdown = true;
        pickA_.set = pickA_.current = true; // A starts as the sound that is playing now
        pickA.onClick = [this] { choose(pickA, pickA_, "SOUND A"); };
        pickB.onClick = [this] { choose(pickB, pickB_, "SOUND B"); };
        sync();
        startTimerHz(30);
    }
    ~MorphCard() override
    {
        play(false);
    }
    void choose(GButton &b, Pick &pk, const char *title)
    {
        EasyView::Impl::MenuOpts o;
        o.current = true;
        o.title = title;
        I.openMenu(I.canvasRect(b), o, [this, &b, &pk](const EasyView::Impl::PickResult &r) {
            pk.set = true;
            pk.current = r.kind == EasyView::Impl::PickResult::CURRENT;
            pk.path = r.path;
            pk.name = r.name;
            b.text = pk.current ? juce::String("THE SOUND PLAYING NOW") : juce::String(pk.name).toUpperCase();
            b.repaint();
        });
    }
    void play(bool on)
    {
        if (on == playing)
            return;
        playing = on;
        playB.toggled = on;
        if (on)
            I.synth->playNote(0, kPlayKey, 100, 0);
        else
            I.synth->releaseNote(0, kPlayKey, 0);
        playB.repaint();
    }
    void visibilityChanged() override
    {
        if (!isVisible())
            play(false);
    }
    // one pick -> its sound, its file and its name ("the sound playing now" is kept in the user folder)
    bool take(Pick &pk, ghost::Blob &b, std::string &path, std::string &name)
    {
        if (!pk.set)
            return false;
        if (pk.current)
        {
            b = I.current();
            name = I.synth->storage.getPatch().name;
            auto dir = I.synth->storage.userDataPath / "Morph Sounds";
            std::error_code ec;
            fs::create_directories(dir, ec);
            auto f = dir / (name.empty() ? std::string("sound.fxp") : name + ".fxp");
            path = path_to_string(f);
            return ghost::dna::writeFxp(path, b, name);
        }
        path = pk.path;
        name = pk.name;
        return !path.empty() && ghost::dna::readFxp(path, b);
    }
    void make()
    {
        ghost::Blob a, b;
        std::string pa, pb, na, nb;
        if (!take(pickA_, a, pa, na) || !take(pickB_, b, pb, nb))
        {
            status = "pick sound A and sound B first";
            repaint();
            return;
        }
        int mode = 0;
        std::string why;
        auto m = ghost::dna::morphPatch(I.synth, a, b, pa, na, pb, nb, mode, why);
        if (mode == 0 || m.empty())
        {
            status = why;
            repaint();
            return;
        }
        M().blend = 0.f;
        I.loadBlob(m, na + " > " + nb);
        status = why;
        repaint();
    }
    void sync()
    {
        auto &m = M();
        const bool pend = m.pendOn.load(), draw = m.drawOn.load(), fr = m.pendFree.load();
        pendB.toggled = pend;
        drawB.toggled = draw;
        freeB.toggled = pend && fr;
        for (int k = 0; k < ghost::Morph::kRates; ++k)
            rateB[k].toggled = pend && !fr && m.pendRate.load() == k;
        for (int k = 0; k < ghost::Morph::n_feels; ++k)
            feelB[k].toggled = m.pendFeel.load() == k;
        for (int k = 0; k < ghost::Morph::n_shapes; ++k)
            shapeB[k].toggled = pend && m.pendShape.load() == k;
        static const int bars[4] = {1, 2, 4, 8};
        for (int k = 0; k < 4; ++k)
            barsB[k].toggled = m.drawBars.load() == bars[k];
        hzBar.setAlpha(fr ? 1.f : 0.45f);
        repaint();
    }
    void timerCallback() override
    {
        if (isShowing())
        {
            // follow what a loaded sound (or the host) set
            auto &m = M();
            const int st = (m.pendOn.load() ? 1 : 0) | (m.drawOn.load() ? 2 : 0) | (m.pendFree.load() ? 4 : 0) |
                           (m.pendRate.load() << 3) | (m.pendFeel.load() << 8) | (m.pendShape.load() << 10) |
                           (m.drawBars.load() << 12);
            if (st != lastState)
            {
                lastState = st;
                sync();
            }
            blendBar.repaint();
            pad.repaint();
        }
    }
    int lastState{-1};
    void resized() override
    {
        auto b = getLocalBounds().reduced(14);
        b.removeFromTop(30);
        auto row = b.removeFromTop(34);
        pickA.setBounds(row.removeFromLeft(270));
        row.removeFromLeft(8);
        pickB.setBounds(row.removeFromLeft(270));
        row.removeFromLeft(10);
        makeB.setBounds(row);
        b.removeFromTop(24); // status
        auto br = b.removeFromTop(38);
        playB.setBounds(br.removeFromRight(84).withSizeKeepingCentre(80, 32));
        br.removeFromRight(8);
        blendBar.setBounds(br);
        b.removeFromTop(8);
        auto pr = b.removeFromTop(30);
        pendB.setBounds(pr.removeFromLeft(110));
        pr.removeFromLeft(6);
        const int rw = pr.getWidth() / ghost::Morph::kRates;
        for (int k = 0; k < ghost::Morph::kRates; ++k)
            rateB[k].setBounds(pr.removeFromLeft(rw).reduced(1, 0));
        b.removeFromTop(4);
        auto fr = b.removeFromTop(30);
        for (int k = 0; k < ghost::Morph::n_feels; ++k)
            feelB[k].setBounds(fr.removeFromLeft(92).reduced(1, 0));
        fr.removeFromLeft(10);
        const int sw = fr.getWidth() / ghost::Morph::n_shapes;
        for (int k = 0; k < ghost::Morph::n_shapes; ++k)
            shapeB[k].setBounds(fr.removeFromLeft(sw).reduced(1, 0));
        b.removeFromTop(4);
        auto sr = b.removeFromTop(40);
        swingBar.setBounds(sr.removeFromLeft(280));
        sr.removeFromLeft(12);
        freeB.setBounds(sr.removeFromLeft(100).withTrimmedTop(12));
        sr.removeFromLeft(8);
        hzBar.setBounds(sr);
        b.removeFromTop(8);
        auto dr = b.removeFromTop(30);
        drawB.setBounds(dr.removeFromLeft(150));
        dr.removeFromLeft(8);
        for (int k = 0; k < 4; ++k)
            barsB[k].setBounds(dr.removeFromLeft(84).reduced(1, 0));
        b.removeFromTop(4);
        pad.setBounds(b);
    }
    void paint(juce::Graphics &g) override
    {
        panelBg(g, getLocalBounds().toFloat());
        brushTitle(g, "MORPH", 18.f, 6.f, 24.f);
        g.setFont(F(9.5f, 1));
        g.setColour(col::dim);
        auto inf = M().info();
        juce::String sub = inf.mode == ghost::Morph::M_OFF
                               ? juce::String("blend two sounds: pick A and B, then MAKE THE MORPH")
                               : juce::String(inf.nameA).toUpperCase() + "  >  " + juce::String(inf.nameB).toUpperCase() +
                                     (inf.mode == ghost::Morph::M_PARAMS ? "   (true morph: every control glides)"
                                                                          : "   (scene blend: A in scene A, B in scene B)");
        dt(g, sub, R(170, 10, (float)getWidth() - 184, 20), juce::Justification::left);
        if (status.isNotEmpty())
        {
            g.setColour(col::cyan.withAlpha(0.9f));
            g.setFont(F(9.f, 1));
            g.drawFittedText(status, juce::Rectangle<int>(14, 78, getWidth() - 28, 20), juce::Justification::left, 1);
        }
    }
};

// ------------------------------------------------------------------------------------------------
#include "GhostLayersUI.inc"
#include "GhostBoardUI.inc"

// ------------------------------------------------------------------------------------------------
struct EasyHolder
{
    std::unique_ptr<LayersStrip> layers;
    std::unique_ptr<SubPage> sub;
    std::unique_ptr<BlendPage> lockPage;
    std::unique_ptr<BoardPage> board;
    std::unique_ptr<Header> header;
    std::unique_ptr<ModeTabs> tabs;
    std::unique_ptr<TempoBox> tempo;
    std::unique_ptr<TerrainCard> terrain;
    std::unique_ptr<MorphCard> morph;
    std::unique_ptr<Macros> macros;
    std::unique_ptr<SeqStrip> seq;
    std::unique_ptr<Keys> keys;
    std::unique_ptr<PresetMenu> menu;
    juce::String lastName;
    int tick{0};
};
static std::map<EasyView *, EasyHolder> holders; // one per open editor (message thread only); nothing in it outlives its editor

struct KeyRootListener : public juce::MidiKeyboardState::Listener
{
    SurgeSynthProcessor &p;
    KeyRootListener(SurgeSynthProcessor &pp) : p(pp) {}
    void handleNoteOn(juce::MidiKeyboardState *, int, int note, float) override
    {
        if (p.ghostSeq().playing.load())
            p.ghostSeq().root.store(note); // the keyboard transposes the running pattern
    }
    void handleNoteOff(juce::MidiKeyboardState *, int, int, float) override {}
};
static std::map<EasyView *, std::unique_ptr<KeyRootListener>> gbKeyListeners;

EasyView::EasyView(SurgeSynthEditor *ed, SurgeSynthProcessor &proc)
    : impl(std::make_unique<Impl>(ed, proc, this))
{
    // the look (NIGHT / DAWN) is remembered in the user folder
    auto themeFile = [s = impl->synth]() { return juce::File(juce::String(path_to_string(s->storage.userDataPath / "Ghost Bass Theme.txt"))); };
    setTheme(themeFile().loadFileAsString().trim() == "DAWN");
    impl->toggleTheme = [this, themeFile]() {
        setThemeForShot(!col::light);
        auto f = themeFile();
        f.getParentDirectory().createDirectory();
        f.replaceWithText(col::light ? "DAWN" : "NIGHT");
    };
    addAndMakeVisible(canvas);
    auto &h = holders[this];
    h = EasyHolder{}; // (always a fresh holder)
    auto &I = *impl;
    h.header = std::make_unique<Header>(I);
    h.tabs = std::make_unique<ModeTabs>(I);
    h.tempo = std::make_unique<TempoBox>(I);
    h.board = std::make_unique<BoardPage>(I);
    h.terrain = std::make_unique<TerrainCard>(I);
    h.morph = std::make_unique<MorphCard>(I);
    h.macros = std::make_unique<Macros>(I);
    h.seq = std::make_unique<SeqStrip>(I);
    h.keys = std::make_unique<Keys>(proc.midiKeyboardState);
    h.layers = std::make_unique<LayersStrip>(I, *proc.ghostLayers);
    h.sub = std::make_unique<SubPage>(I, *proc.ghostLayers);
    h.lockPage = std::make_unique<BlendPage>(I, *proc.ghostLayers);
    h.menu = std::make_unique<PresetMenu>(I);
    h.layers->onOpen = [this](int k) {
        if (k == 0)
        {
            if (onOpenEngine)
                onOpenEngine();
        }
        else
            setMode(M_SUB);
    };
    gbKeyListeners[this] = std::make_unique<KeyRootListener>(proc);
    proc.midiKeyboardState.addListener(gbKeyListeners[this].get());
    for (juce::Component *c : std::initializer_list<juce::Component *>{
             h.header.get(), h.tabs.get(), h.tempo.get(), h.board.get(), h.terrain.get(), h.morph.get(), h.macros.get(), h.seq.get(),
             h.keys.get(), h.layers.get(), h.sub.get(), h.lockPage.get()})
        canvas.addAndMakeVisible(c);
    canvas.addChildComponent(*h.menu); // on top of everything

    I.openMenu = [this](R anchor, const Impl::MenuOpts &o, std::function<void(const Impl::PickResult &)> cb) {
        holders[this].menu->open(anchor, o, std::move(cb));
    };
    h.header->engB.onClick = [this]() {
        if (onOpenEngine)
            onOpenEngine();
    };
    // the drop-down on the patch name
    h.header->pill.onClick = [this]() {
        auto &hh = holders[this];
        hh.menu->open(impl->canvasRect(hh.header->pill), Impl::MenuOpts{}, [this](const Impl::PickResult &r) {
            if (r.kind == Impl::PickResult::PATCH)
                impl->loadPatchFile(string_to_path(r.path));
        });
    };
    // < > step through the list of the type that the playing sound belongs to (A-Z, wrapping round)
    auto step = [this](int d) {
        auto &L = impl->library();
        if (L.all.empty() || L.types.empty())
            return;
        const int cur = impl->currentIndex();
        const int ty = cur >= 0 ? L.typeOf[(size_t)cur] : 0;
        const auto &items = L.types[(size_t)ty].items;
        int pos = -1;
        for (size_t k = 0; k < items.size(); ++k)
            if (items[k] == cur)
                pos = (int)k;
        const int n = (int)items.size();
        pos = pos < 0 ? (d > 0 ? 0 : n - 1) : (pos + d + n) % n;
        impl->loadPatchFile(L.all[(size_t)items[(size_t)pos]].path);
    };
    h.header->prevB.onClick = [step]() { step(-1); };
    h.header->nextB.onClick = [step]() { step(+1); };
    h.header->saveB.onClick = [this]() {
        auto *aw = new juce::AlertWindow("SAVE", "Name your sound (saved in your Ghost Bass user folder)",
                                         juce::MessageBoxIconType::NoIcon);
        aw->addTextEditor("name", impl->synth->storage.getPatch().name, "NAME");
        aw->addTextEditor("cat", "My Basses", "CATEGORY");
        aw->addButton("SAVE", 1, juce::KeyPress(juce::KeyPress::returnKey));
        aw->addButton("CANCEL", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        juce::Component::SafePointer<EasyView> sp(this);
        aw->enterModalState(true, juce::ModalCallbackFunction::create([sp, aw](int r) {
                                if (!sp || r != 1)
                                    return;
                                auto nm = aw->getTextEditorContents("name").trim();
                                auto ct = aw->getTextEditorContents("cat").trim();
                                if (nm.isEmpty())
                                    return;
                                auto *s = sp->impl->synth;
                                s->storage.getPatch().name = nm.toStdString();
                                s->storage.getPatch().category = ct.toStdString();
                                auto dir = s->storage.userPatchesPath / ct.toStdString();
                                std::error_code ec;
                                fs::create_directories(dir, ec);
                                s->savePatchToPath(dir / (nm.toStdString() + ".fxp"));
                                sp->impl->libraryChanged();
                                sp->repaint();
                            }),
                            true);
    };
    impl->requestSave = h.header->saveB.onClick;
    h.tabs->onMode = [this](int m) { setMode(m); };
    setMode(M_BOARD);
    startTimerHz(12);
}

EasyView::~EasyView()
{
    impl->proc.midiKeyboardState.removeListener(gbKeyListeners[this].get());
    gbKeyListeners.erase(this);
    holders.erase(this);
}

void EasyView::setMode(int m)
{
    impl->mode = juce::jlimit(0, n_modes - 1, m);
    auto &h = holders[this];
    h.board->setVisible(impl->mode == M_BOARD);
    h.terrain->setVisible(impl->mode == M_TERRAIN);
    h.morph->setVisible(impl->mode == M_MORPH);
    h.sub->setVisible(impl->mode == M_SUB);
    h.lockPage->setVisible(impl->mode == M_LOCK);
    h.seq->setVisible(impl->mode == M_SEQ);
    if (impl->mode == M_MORPH)
        h.morph->sync(); // (the page shows what the loaded sound set)
    h.menu->close();
    h.tabs->repaint();
    repaint();
}
int EasyView::mode() const { return impl->mode; }

ghost::terrain::TerrainView &EasyView::terrainView() { return holders[this].terrain->view; }
ghost::terrain::TerrainEditor &EasyView::terrainEditor() { return holders[this].terrain->ed; }
void EasyView::terrainSync()
{
    auto &c = *holders[this].terrain;
    c.syncFromEngine();
    c.view.setMeters(c.I.eng->terrain.meters()); // (as the timer does: the readout follows the land)
}

void EasyView::tickForShot()
{
    auto &h = holders[this];
    h.morph->sync(); // (MORPH follows the engine on its own timer; screenshots have no message loop)
    h.board->timerCallback();
}

// ---- sound picker and blend board hooks for tests / screenshots ----
void EasyView::openMenuForShot(int slot)
{
    auto &h = holders[this];
    if (slot < 0)
        h.header->pill.onClick();
    else
        h.board->pickSlot(juce::jlimit(0, 3, slot));
}
void EasyView::closeMenuForShot() { holders[this].menu->close(); }
bool EasyView::menuOpen() const
{
    auto it = holders.find(const_cast<EasyView *>(this));
    return it != holders.end() && it->second.menu->isOpen();
}
void EasyView::menuSearchForShot(const juce::String &q)
{
    auto &m = *holders[this].menu;
    m.search.setText(q, true);
    m.runSearch();
}
int EasyView::menuResultCountForShot() const
{
    auto it = holders.find(const_cast<EasyView *>(this));
    return it == holders.end() ? 0 : it->second.menu->resultCount();
}
std::vector<std::pair<std::string, int>> EasyView::typeCountsForShot()
{
    std::vector<std::pair<std::string, int>> r;
    for (auto &t : impl->library().types)
        r.emplace_back(t.name, (int)t.items.size());
    return r;
}
bool EasyView::typesPartitionForShot()
{
    auto &L = impl->library();
    std::vector<int> seen(L.all.size(), 0);
    for (size_t t = 0; t < L.types.size(); ++t)
        for (int i : L.types[t].items)
        {
            if (i < 0 || i >= (int)seen.size() || L.typeOf[(size_t)i] != (int)t)
                return false;
            ++seen[(size_t)i];
        }
    for (int c : seen)
        if (c != 1)
            return false;
    return true;
}
std::string EasyView::typeNameForShot(const std::string &folder, const std::string &name)
{
    return Impl::Index::typeNames()[(size_t)Impl::Index::classify(folder, name)];
}
int EasyView::libraryCountForShot() { return (int)impl->library().all.size(); }
double EasyView::indexBuildMsForShot()
{
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    Impl::Index fresh;
    fresh.build(impl->presets());
    return juce::Time::getMillisecondCounterHiRes() - t0;
}
void EasyView::librarySyntheticForShot(int n)
{
    std::vector<Impl::Preset> v;
    static const char *packs[] = {"Basses", "Leads", "Pads", "Keys", "Plucks", "Drones", "FX", "Sequences"};
    for (int i = 0; i < n; ++i)
    {
        std::string folder = packs[i % 8];
        if (i % 3 == 0)
            folder += "/Pack " + std::to_string(i % 37);
        const std::string nm = "Sound " + std::to_string(i) + " " + (i % 5 == 0 ? "wobble" : "glide");
        v.push_back({nm, folder, "", folder, fs::path("/synthetic/" + std::to_string(i) + ".fxp")});
    }
    impl->index.build(std::move(v));
    impl->indexed = true;
    impl->indexedCount = impl->synth->storage.patch_list.size();
}
int EasyView::indexBuildsForShot() const { return impl->indexBuilds; }
bool EasyView::boardSetSlotForShot(int slot, const std::string &path, const std::string &name)
{
    auto &b = *holders[this].board;
    Impl::PickResult r;
    if (path.empty())
        r.kind = Impl::PickResult::CURRENT;
    else
    {
        r.kind = Impl::PickResult::PATCH;
        r.path = path;
        r.name = name;
    }
    b.setSlot(juce::jlimit(0, 3, slot), r);
    return b.status.isEmpty() || !b.status.startsWith("Could not");
}
void EasyView::boardClearForShot() { holders[this].board->clearAll(); }
void EasyView::boardSetPuckForShot(float x, float y) { holders[this].board->setPuck(x, y); }
void EasyView::boardWeightsForShot(float *w4) { holders[this].board->weights(w4); }
float EasyView::boardSpectrumPeakForShot(int src)
{
    auto &b = *holders[this].board;
    b.updateSpectra();
    float m = 0.f;
    for (float v : b.spec[juce::jlimit(0, 4, src)])
        m = std::max(m, v);
    return m;
}
int EasyView::boardSlotsForShot() const
{
    auto it = holders.find(const_cast<EasyView *>(this));
    return it == holders.end() ? 0 : it->second.board->filledCount();
}
std::string EasyView::boardSlotNameForShot(int slot) const
{
    auto it = holders.find(const_cast<EasyView *>(this));
    if (it == holders.end() || slot < 0 || slot > 3)
        return {};
    return it->second.board->slot[slot].name;
}

// ---- the layout test: every visible control (a component without visible children), in canvas coordinates ----
static void gatherControls(juce::Component &parent, juce::Component &canvas, juce::Component *skip,
                           std::vector<EasyView::ControlBounds> &out)
{
    for (auto *ch : parent.getChildren())
    {
        if (!ch->isVisible() || ch == skip)
            continue;
        bool kids = false;
        for (auto *g : ch->getChildren())
            kids = kids || g->isVisible();
        const bool leaf = !kids || dynamic_cast<juce::MidiKeyboardComponent *>(ch) != nullptr;
        if (!leaf)
        {
            gatherControls(*ch, canvas, skip, out);
            continue;
        }
        juce::String nm = juce::String(typeid(*ch).name());
        if (auto *b = dynamic_cast<GButton *>(ch))
            nm += ":" + b->text;
        else if (!ch->getName().isEmpty())
            nm += ":" + ch->getName();
        out.push_back({nm.toStdString(), canvas.getLocalArea(ch->getParentComponent(), ch->getBounds().toFloat())});
    }
}
std::vector<EasyView::ControlBounds> EasyView::controlBounds()
{
    std::vector<ControlBounds> out;
    gatherControls(canvas, canvas, holders[this].menu.get(), out);
    return out;
}
juce::Rectangle<float> EasyView::canvasBoundsInView() const
{
    return canvas.getBounds().toFloat().transformedBy(canvas.getTransform());
}

void EasyView::paint(juce::Graphics &g) { g.fillAll(col::bg); }

// the background (build 12, AKIRA): a midnight city glow behind a machined deck, a faint survey grid, sodium light from above, the
// keyboard in a steel frame with red brackets
void EasyView::Canvas::paint(juce::Graphics &g)
{
    const float w = (float)getWidth(), h = (float)getHeight();
    if (col::light)
    {
        // DAWN: the ink landscape (lavender cliffs, a waterfall, two pines) behind frosted paper panels
        g.fillAll(col::bg);
        auto im = art(Art::dawn);
        if (im.isValid())
            g.drawImage(im, R(0.f, 0.f, w, h), juce::RectanglePlacement::fillDestination);
        g.setColour(juce::Colours::white.withAlpha(0.10f));
        g.fillRect(0.f, 0.f, w, h);
        R kf(10.f, (float)(H - 110), w - 20.f, 102.f);
        g.setColour(juce::Colours::white.withAlpha(0.7f));
        fillChamfer(g, kf, 14.f);
        glowBorder(g, kf, 14.f, 1.f, 1.8f);
        return;
    }
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff0b1324), 0, 0, juce::Colour(0xff020409), 0, h, false));
    g.fillRect(0.f, 0.f, w, h);
    // the survey grid
    g.setColour(col::line.withAlpha(0.10f));
    for (float x = 0.f; x < w; x += 24.f)
        g.fillRect(R(x, 0.f, 1.f, h));
    for (float y = 0.f; y < h; y += 24.f)
        g.fillRect(R(0.f, y, w, 1.f));
    g.setColour(col::line.withAlpha(0.22f));
    for (float x = 0.f; x < w; x += 96.f)
        for (float y = 0.f; y < h; y += 96.f)
        {
            g.fillRect(R(x - 3.f, y, 7.f, 1.f));
            g.fillRect(R(x, y - 3.f, 1.f, 7.f));
        }
    // watercolor ink blooms (red, magenta, violet, a touch of cyan) bleeding through the night behind the panels
    {
        auto bl = art(Art::bloom);
        if (bl.isValid())
        {
            g.setOpacity(0.9f);
            g.drawImage(bl, R(0.f, 0.f, w, h), juce::RectanglePlacement::stretchToFit);
            g.setOpacity(1.f);
        }
    }
    g.setGradientFill(juce::ColourGradient(col::amber.withAlpha(0.05f), w * 0.5f, 0.f, juce::Colours::transparentBlack, w * 0.5f, 260.f, false));
    g.fillRect(0.f, 0.f, w, h);
    g.setGradientFill(juce::ColourGradient(col::cyan.withAlpha(0.05f), w - 60.f, h * 0.35f, juce::Colours::transparentBlack, w - 60.f - 380.f, h * 0.35f, true));
    g.fillRect(0.f, 0.f, w, h);
    // the keyboard's frame: a soot well, a steel rim, red brackets and a hazard strip on the left end
    R kf(10.f, (float)(H - 110), w - 20.f, 102.f);
    g.setColour(col::soot);
    fillChamfer(g, kf, 14.f);
    glowBorder(g, kf, 14.f, 1.1f, 1.8f);
}

void EasyView::Canvas::paintOverChildren(juce::Graphics &g)
{
    static juce::Image paper = art(Art::paper);
    if (!paper.isValid() || col::light)
        return;
    g.setTiledImageFill(paper, 0, 0, 0.55f);
    g.fillAll();
}

// the layout (design canvas 1200 x 760): header | tabs + page | macros | layer strip | keyboard
static constexpr int kHeaderH = 80, kPageTop = 130, kPageBottom = 564, kStripTop = 570, kStripH = 76, kKeysTop = 652;

void EasyView::resized()
{
    canvas.setBounds(0, 0, W, H);
    float s = std::min(getWidth() / (float)W, getHeight() / (float)H);
    float ox = (getWidth() - W * s) * 0.5f, oy = (getHeight() - H * s) * 0.5f;
    canvas.setTransform(juce::AffineTransform::scale(s).translated(ox, oy));
    auto &h = holders[this];
    h.header->setBounds(0, 0, W, kHeaderH);
    h.tabs->setBounds(8, 86, 660, 38);
    h.tempo->setBounds(676, 86, 204, 38);
    for (juce::Component *c : std::initializer_list<juce::Component *>{h.board.get(), h.terrain.get(), h.morph.get(), h.sub.get(),
                                                                        h.lockPage.get(), h.seq.get()})
        c->setBounds(8, kPageTop, 872, kPageBottom - kPageTop);
    h.macros->setBounds(888, 86, 304, kPageBottom - 86);
    h.layers->setBounds(8, kStripTop, W - 16, kStripH);
    h.keys->setBounds(18, kKeysTop, W - 36, H - kKeysTop - 16);
    h.menu->setBounds(0, 0, W, H);
}

bool EasyView::setThemeForShot(bool light)
{
    const bool was = col::light;
    setTheme(light);
    auto &h = holders[this];
    if (h.keys)
        h.keys->applyColours();
    if (h.terrain)
        h.terrain->refreshPalette();
    if (h.menu)
        h.menu->applyColours();
    if (h.header)
        h.header->themeB.text = col::light ? "DARK" : "LIGHT";
    repaint();
    canvas.repaint();
    return was;
}

void EasyView::timerCallback()
{
    auto &h = holders[this];
    h.header->repaint();
    h.tempo->repaint();
    h.macros->repaint();
    h.layers->repaint();
    h.seq->refresh();
    // follow the loaded sound (the picker's own list is indexed once; a changed library is noticed by its size)
    auto nm = impl->synth->storage.getPatch().name;
    if (nm != h.lastName)
    {
        h.lastName = nm;
        h.header->pill.repaint();
    }
}

bool EasyView::isInterestedInFileDrag(const juce::StringArray &files)
{
    for (auto &f : files)
        if (f.endsWithIgnoreCase(".fxp"))
            return true;
    return false;
}

void EasyView::filesDropped(const juce::StringArray &files, int, int)
{
    for (auto &f : files)
    {
        if (f.endsWithIgnoreCase(".fxp"))
        {
            impl->loadPatchFile(string_to_path(f.toStdString()));
            return;
        }
    }
}

} // namespace ghost::ui
