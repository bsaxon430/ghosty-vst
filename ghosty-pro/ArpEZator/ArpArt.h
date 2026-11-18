#pragma once
// ArpEZator drawings + themes. Ported from the web app's gfx.js (canvas paths -> juce::Path): Professor Bernstein in
// his hammock (SUNRISE: sun glow + sunglasses, DUSK: sunset, MIDNIGHT: stars + glowing eyes; TRAP holds a double foam cup, HIP HOP a 40 in a paper bag) and the mood ghost whose
// face follows the chords. Colours: the shared GhostBlock themes (BLACK / PURPLE / LIGHT) + each mood's lane accents; the editor crossfades.
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <map>
#include "GhostThemes.h"
#include "GhostGlass.h"

namespace arpui
{
using juce::Colour;
namespace Colours = juce::Colours;
struct Palette
{   // every colour comes from the shared GhostBlock themes (Shared/GhostThemes.h): BLACK / PURPLE / LIGHT.
    // The three moods keep their own lane accents (they carry meaning), made readable on the theme's surfaces.
    Colour bg1, bg2, bg3, card, card2, line, ink, dim, a1, a2, a3, gold, pink, cyan, green, roll, rowA, rowB, grid, gridB, keyW, keyB, chordRow;
    Colour panel, onAccent, shadow, bad, overlay;
    bool dark = true;
    int face = 0;         // the picture FACES (ArpFaces.h): 1 HOLO, 2 NEON RAIN, 3 MASK, 4 DREAM; cards are frosted over the face's picture
    bool glass = false;   // the GHOST look (ArpEZator-only 4th look): liquid glass on the LIGHT base (theme = Light); the widgets draw glass when set
    ghosty::themes::Theme theme = ghosty::themes::Theme::Dark;
    // the label colour on a filled accent / lane colour: the theme's onAccent, else whichever text token reads best
    Colour textOn (Colour fill) const
    {
        Colour best = onAccent; double bc = ghosty::themes::contrast (onAccent, fill);
        for (auto c : { ink, bg1, card }) { const double k = ghosty::themes::contrast (c, fill); if (k > bc + 0.01) { bc = k; best = c; } }
        return best;
    }
    Colour readable (Colour c, double minRatio = 4.5) const   // c, darkened / brightened until it reads on every surface (text on panels + cards + cells)
    {
        for (int k = 0; k < 3; ++k) for (auto bg : { card, card2, panel, roll, bg1 }) c = ghosty::themes::readableOn (c, bg, minRatio);
        return c;
    }
};
// an opaque card (overlays, panels, toasts): the theme's card colour; GHOST = a frosted glass card
inline void fillCard (juce::Graphics& g, juce::Rectangle<float> r, float rad, const Palette& P)
{
    if (! P.glass) { g.setColour (P.card); g.fillRoundedRectangle (r, rad); return; }
    ghosty::glass::card (g, r, rad);
}
// GHOST: the glass shapes the widgets paint many times (pills, wells, segments, knob domes, cells) rendered ONCE per (kind, tint, size, pixel
// scale) into an image and blitted afterwards (the timer repaints lanes, chips and the roll; a full repaint stays near LIGHT's cost). Message thread only.
namespace glassui
{
namespace gg = ghosty::glass;
enum Kind { PillOff = 0, PillOffOver, PillOn, PillOnOver, Well, Sel, Dome, Cell, Chip, ChipNow, StepOn, Disc };
inline void paintKind (juce::Graphics& g, int kind, Colour tint, juce::Rectangle<float> r, float rad)
{
    switch (kind)
    {
        case PillOff: case PillOffOver: gg::shadow (g, r, rad, 2.4f, 1.2f, 3); gg::body (g, r, rad, Colours::white, kind == PillOffOver ? 0.54f : 0.42f); break;
        case PillOn: case PillOnOver:   gg::shadow (g, r, rad, 2.4f, 1.2f, 3); gg::body (g, r, rad, tint, kind == PillOnOver ? 0.98f : 0.95f, 0.3f); break;
        case StepOn:                    gg::shadow (g, r, rad, 2.4f, 1.0f, 3); gg::body (g, r, rad, tint, 0.92f, 0.5f); break;
        case Well:  gg::recessed (g, r, rad); break;
        case Disc:  gg::shadow (g, r, rad, 4.0f, 1.0f, 4); gg::body (g, r, rad, tint, 0.86f, 0.6f); break;
        case Sel:   gg::body (g, r, rad, tint, 0.95f, 0.3f); break;
        case Cell:  gg::body (g, r, rad, Colours::white, 0.40f, 0.8f); break;
        case Chip: case ChipNow: gg::shadow (g, r, rad, 3.0f, 1.0f, 3); gg::body (g, r, rad, Colours::white, kind == ChipNow ? 0.62f : 0.46f); break;
        case Dome:
        {
            const auto dome = r;
            gg::shadow (g, dome, dome.getWidth() * 0.5f, juce::jmax (2.0f, dome.getWidth() * 0.12f), 1.0f, 4);
            g.setGradientFill (juce::ColourGradient (Colours::white, dome.getX() + dome.getWidth() * 0.3f, dome.getY(), Colour (0xffd9d0f3), dome.getCentreX(), dome.getBottom(), false)); g.fillEllipse (dome);
            g.setGradientFill (juce::ColourGradient (Colours::white.withAlpha (0.85f), dome.getCentreX(), dome.getY() + 1, Colour (0x00ffffff), dome.getCentreX(), dome.getCentreY(), false));
            g.fillEllipse (dome.reduced (dome.getWidth() * 0.12f, 0).withHeight (dome.getHeight() * 0.5f).translated (0, 1.5f));
            g.setColour (Colour (0xb0ffffff)); g.drawEllipse (dome.reduced (0.6f), 1.2f);
            break;
        }
        default: break;
    }
}
struct Sprites
{
    int builds = 0;
    void draw (juce::Graphics& g, juce::Rectangle<float> r, float rad, int kind, Colour tint = Colours::white)
    {
        if (r.getWidth() < 1.0f || r.getHeight() < 1.0f) return;
        const float scale = juce::jlimit (1.0f, 4.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        const int m = 7, qw = juce::roundToInt (r.getWidth() * 4), qh = juce::roundToInt (r.getHeight() * 4);
        const Key k { kind, (juce::int64) tint.getARGB(), qw, qh, juce::roundToInt (rad * 4), juce::roundToInt (scale * 100) };
        auto it = map.find (k);
        if (it == map.end())
        {
            if (map.size() > 600) map.clear();
            const float w = (float) qw / 4.0f, h = (float) qh / 4.0f;
            juce::Image img (juce::Image::ARGB, juce::jmax (1, (int) std::ceil ((w + 2 * m) * scale)), juce::jmax (1, (int) std::ceil ((h + 2 * m) * scale)), true, juce::SoftwareImageType());
            { juce::Graphics ig (img); ig.addTransform (juce::AffineTransform::scale (scale)); paintKind (ig, kind, tint, { (float) m, (float) m, w, h }, juce::jmin (rad, w * 0.5f, h * 0.5f)); }
            it = map.emplace (k, img).first; ++builds;
        }
        g.setOpacity (1.0f);   // (an image blit takes the current colour's alpha)
        g.drawImageTransformed (it->second, juce::AffineTransform::scale (1.0f / scale).translated (std::round (r.getX()) - (float) m, std::round (r.getY()) - (float) m));   // (whole pixels: the fast blit)
    }
private:
    using Key = std::tuple<int, juce::int64, int, int, int, int>;
    std::map<Key, juce::Image> map;
};
inline Sprites& sprites() { static Sprites s; return s; }
} // namespace glassui

inline Colour lerpC (Colour a, Colour b, float t) { return a.interpolatedWith (b, juce::jlimit (0.0f, 1.0f, t)); }
inline Palette lerpP (const Palette& a, const Palette& b, float t)
{
    Palette o;
    o.bg1 = lerpC (a.bg1, b.bg1, t); o.bg2 = lerpC (a.bg2, b.bg2, t); o.bg3 = lerpC (a.bg3, b.bg3, t); o.card = lerpC (a.card, b.card, t); o.card2 = lerpC (a.card2, b.card2, t);
    o.line = lerpC (a.line, b.line, t); o.ink = lerpC (a.ink, b.ink, t); o.dim = lerpC (a.dim, b.dim, t); o.a1 = lerpC (a.a1, b.a1, t); o.a2 = lerpC (a.a2, b.a2, t); o.a3 = lerpC (a.a3, b.a3, t);
    o.gold = lerpC (a.gold, b.gold, t); o.pink = lerpC (a.pink, b.pink, t); o.cyan = lerpC (a.cyan, b.cyan, t); o.green = lerpC (a.green, b.green, t);
    o.roll = lerpC (a.roll, b.roll, t); o.rowA = lerpC (a.rowA, b.rowA, t); o.rowB = lerpC (a.rowB, b.rowB, t); o.grid = lerpC (a.grid, b.grid, t); o.gridB = lerpC (a.gridB, b.gridB, t);
    o.keyW = lerpC (a.keyW, b.keyW, t); o.keyB = lerpC (a.keyB, b.keyB, t); o.chordRow = lerpC (a.chordRow, b.chordRow, t);
    o.panel = lerpC (a.panel, b.panel, t); o.onAccent = lerpC (a.onAccent, b.onAccent, t); o.shadow = lerpC (a.shadow, b.shadow, t); o.bad = lerpC (a.bad, b.bad, t); o.overlay = lerpC (a.overlay, b.overlay, t);
    o.dark = t < 0.5f ? a.dark : b.dark; o.theme = t < 0.5f ? a.theme : b.theme; o.glass = t < 0.5f ? a.glass : b.glass; o.face = t < 0.5f ? a.face : b.face;
    return o;
}
// glass = GHOST (only with theme Light): the flat tokens = the frosted glass as it reads over the aurora (contrast maths + flat fills); the
// accents are made readable on the darkest glass too (a card over the pink aurora orb)
// the FACES' surfaces (what a frosted card / the roll reads as over its picture: the contrast maths use these flat tokens)
inline void faceSurfaces (Palette& p, int face)
{
    auto set = [&p] (juce::uint32 bg1, juce::uint32 bg2, juce::uint32 bg3, juce::uint32 panel, juce::uint32 card, juce::uint32 card2, juce::uint32 line,
                     juce::uint32 ink, juce::uint32 dim, juce::uint32 roll, juce::uint32 keyW, juce::uint32 keyB)
    { p.bg1 = Colour (bg1); p.bg2 = Colour (bg2); p.bg3 = Colour (bg3); p.panel = Colour (panel); p.card = Colour (card); p.card2 = Colour (card2); p.line = Colour (line);
      p.ink = Colour (ink); p.dim = Colour (dim); p.roll = Colour (roll); p.keyW = Colour (keyW); p.keyB = Colour (keyB); };
    switch (face)
    {   // HOLO: deep plum glass, lavender lines (the raincoat's pearl / lilac / mint / pink foil lives on the edges)
        case 1: set (0xff140d22, 0xff1d1430, 0xff0e0918, 0xff241a38, 0xff241a3a, 0xff352a52, 0xff9d8be0, 0xfff7f3ff, 0xffcdc2ea, 0xff17112a, 0xffefeafb, 0xff2c2244); break;
        // NEON RAIN: wet asphalt blue-black, cyan rain lines
        case 2: set (0xff070b12, 0xff0b111b, 0xff05080d, 0xff0f1824, 0xff0e1622, 0xff1a2738, 0xff3f8aa6, 0xffecf7ff, 0xffa9c2d4, 0xff0a1019, 0xffe6f1f8, 0xff17222f); break;
        // MASK: glossy black ink, cream paper text, mustard lines
        case 3: set (0xff0e0c0c, 0xff171414, 0xff0a0909, 0xff1a1716, 0xff181515, 0xff2a2523, 0xffd6a43a, 0xfffcf4e2, 0xffdccdab, 0xff131111, 0xfff6eedd, 0xff221d1b); break;
        // DREAM: cream watercolour paper, wine ink, blush lines
        case 4: set (0xfffbf4ee, 0xfffdf7f1, 0xfff4e4dd, 0xfff7eee7, 0xfffaf2ec, 0xffefe1d9, 0xffd9b3b0, 0xff3a2230, 0xff7a5664, 0xfffbf5f0, 0xfffffcf9, 0xff4a2f3c); break;
        // KITSUNE: shrine-night black with a red haze, red neon lines, ice-white text
        case 5: set (0xff0a0607, 0xff120a0b, 0xff060405, 0xff160c0d, 0xff150b0c, 0xff261315, 0xffc8323a, 0xfffff2f0, 0xffd9b9bb, 0xff0e0708, 0xfff4ecec, 0xff241416); break;
        default: return;
    }
    p.face = face; p.glass = false;
}
inline Palette palette (int mode, ghosty::themes::Theme theme, bool glass = false, int face = 0)
{
    glass = glass && theme == ghosty::themes::Theme::Light;
    const auto& T = ghosty::themes::get (theme);
    Palette p; p.theme = theme; p.dark = ! T.isLight; p.glass = glass;
    p.bg1 = T.window; p.bg2 = T.windowTop; p.bg3 = T.windowBottom; p.panel = T.panel; p.card = T.card; p.card2 = T.cell; p.line = T.line;
    p.ink = T.ink; p.dim = T.dim; p.onAccent = T.onAccent; p.shadow = T.shadow; p.bad = ghosty::themes::readableOn (T.bad, T.ink, 4.6);   /* red under ink text (MUTED) */ p.overlay = T.overlay;
    p.roll = T.waveBg; p.keyW = T.keyWhite; p.keyB = T.keyBlack;
    if (glass)
    {
        namespace gg = ghosty::glass;
        p.bg1 = Colour (0xffebe6f8); p.bg2 = gg::auroraTop(); p.bg3 = gg::auroraBottom(); p.panel = Colour (0xfff0edf9); p.card = Colour (0xfff7f5fd); p.card2 = Colour (0xffe6e1f4);
        p.line = Colour (0xffb8afd8); p.ink = gg::ink(); p.dim = gg::ink2(); p.onAccent = Colour (0xffffffff); p.overlay = gg::ink();
        p.roll = Colour (0xffe9e5f5); p.keyW = Colour (0xfff7f5fd); p.keyB = Colour (0xff3a3260);
        p.bad = ghosty::themes::readableOn (ghosty::themes::readableOn (T.bad, Colour (0xffd0c4da), 4.6), p.ink, 4.6);
    }
    if (face > 0) { glass = false; p.glass = false; faceSurfaces (p, face); }
    // the moods' lane accents (melody / arp / chords): SUNRISE warm gold + sky, DUSK orange + pink + violet, MIDNIGHT neon violet + cyan + green
    // TRAP: icy blue + crimson + silver-lilac; HIP HOP: dusty amber + teal + plum
    static const juce::uint32 neon[5][3] { { 0xffffc35a, 0xff7ec8ff, 0xffff8a65 }, { 0xffff8a5c, 0xffff5ca8, 0xffb06bff }, { 0xffa46bff, 0xff3de3ff, 0xff39ff9a },
                                           { 0xff6fd3ff, 0xffff4d6d, 0xffc7b8ff }, { 0xffe8b05c, 0xff4fd1c5, 0xffc77dff } };
    static const juce::uint32 deep[5][3] { { 0xffa86a00, 0xff1f6fb8, 0xffc24a24 }, { 0xffc0501e, 0xffb0236f, 0xff6a34d0 }, { 0xff6234e0, 0xff096678, 0xffad2d7b },
                                           { 0xff11638f, 0xffb3123a, 0xff5b47b8 }, { 0xff8a5a00, 0xff0d7068, 0xff8a34b8 } };   // (MIDNIGHT LIGHT chords: magenta, apart from the teal arp)
    const int m = juce::jlimit (0, 4, mode);
    auto rd = [&] (Colour c) { return p.readable (glass ? ghosty::themes::readableOn (c, Colour (0xffd0c4da), 4.6) : c); };
    auto acc = [&] (int k) { return rd (Colour (T.isLight ? deep[m][k] : neon[m][k])); };   // >= 4.5:1: they are text too (lane + chord names)
    p.a1 = acc (0); p.a2 = acc (1); p.a3 = acc (2);
    p.gold = rd (T.warn); p.pink = rd (T.accent1); p.cyan = rd (m == 1 ? T.violet : T.accent2); p.green = rd (T.good);
    if (face == 5)
    {   // KITSUNE: the lantern red, the fox's ice white-blue, a warm lamp amber
        p.a1 = rd (Colour (0xffff4a4a)); p.a2 = rd (Colour (0xffa8e4ff)); p.a3 = rd (Colour (0xffff8a6a));
        p.gold = rd (Colour (0xffffb35c)); p.pink = rd (Colour (0xffff5a6e)); p.cyan = rd (Colour (0xffa8e4ff));
    }
    if (face == 1)
    {   // HOLO: the raincoat's own foil on every lane in every mood (melody lilac, arp mint, chords pink; the sheen's sky blue + peach)
        p.a1 = rd (Colour (0xffcbb9ff)); p.a2 = rd (Colour (0xff8ff2d9)); p.a3 = rd (Colour (0xffff9fd6));
        p.gold = rd (Colour (0xffffd59a)); p.pink = rd (Colour (0xffff9fd6)); p.cyan = rd (Colour (0xff8fd8ff));
    }
    p.rowA = T.overlay.withAlpha (T.isLight ? 0.0f : 0.03f); p.rowB = T.overlay.withAlpha (T.isLight ? 0.06f : 0.05f);
    p.grid = T.overlay.withAlpha (T.isLight ? 0.08f : 0.07f); p.gridB = T.overlay.withAlpha (T.isLight ? 0.2f : 0.16f);
    p.chordRow = p.a1.withAlpha (0.10f);
    return p;
}

// ---------------------------------------------------------------- path helpers (canvas angles: 0 = 3 o'clock)
inline juce::Path ell (float x, float y, float rx, float ry, float rot = 0)
{
    juce::Path p; p.addEllipse (x - std::abs (rx), y - std::abs (ry), 2 * std::abs (rx), 2 * std::abs (ry));
    if (std::abs (rot) > 1.0e-6f) p.applyTransform (juce::AffineTransform::rotation (rot, x, y));
    return p;
}
inline void arcTo (juce::Path& p, float cx, float cy, float r, float a0, float a1, bool newSub)
{
    p.addCentredArc (cx, cy, r, r, 0, a0 + juce::MathConstants<float>::halfPi, a1 + juce::MathConstants<float>::halfPi, newSub);
}
inline juce::PathStrokeType stroke (float w) { return juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded); }
inline Colour rgba (int r, int g, int b, float a) { return Colour ((juce::uint8) r, (juce::uint8) g, (juce::uint8) b, juce::jlimit (0.0f, 1.0f, a)); }
inline float seeded (int i) { juce::uint32 s = (juce::uint32) i * 2654435761u + 0x6D2B79F5u; s ^= s >> 15; s *= 0x2c1b3c6du; s ^= s >> 12; return (float) (s & 0xffffff) / 16777216.0f; }

// ---------------------------------------------------------------- the professor's held prop in TRAP / HIP HOP (generic, no text or logos)
// TRAP: a stacked double foam cup (ribbed rims, a straw, purple at the top, two ice cubes, a slosh). HIP HOP: a 40 in a plain kraft
// paper bag (neck + cap out of the crumpled top, a gentle tilt). Drawn in the prop's own units (origin = the hand, the flask's frame).
// Every path is built ONCE (outlines pre-stroked) and painted through transforms, so an animated frame allocates no paths of its own.
// A light rim outside the ink outline keeps each prop >= 3:1 against the night sky and the arm behind it.
struct PropPaths
{
    juce::Path cupOuter, cupInner, cupLipLow, cupLipTop, cupRing, cupMouth, cupRibs, straw, ice, cupLine, cupHalo, cupShade, cupGlint;
    juce::Path bag, neck, ring, cap, capTeeth, glint, creases, folds, bagLine, bagHalo;
};
inline int& propPathBuilds() { static int n = 0; return n; }   // tests: the props' paths are built once, never per frame
inline const PropPaths& propPaths()
{
    static const PropPaths P = []
    {
        ++propPathBuilds();
        PropPaths q;
        auto outline = [] (const juce::Path& src, float w) { juce::Path o; stroke (w).createStrokedPath (o, src); return o; };
        // ---- the double cup: the outer (lower) cup, the inner cup's band above its rim, two rolled lips, the open top
        q.cupOuter.startNewSubPath (-6.4f, -10.6f); q.cupOuter.lineTo (6.4f, -10.6f); q.cupOuter.lineTo (4.6f, 2.2f);
        q.cupOuter.quadraticTo (0, 3.6f, -4.6f, 2.2f); q.cupOuter.closeSubPath();
        q.cupInner.startNewSubPath (-6.8f, -14.0f); q.cupInner.lineTo (6.8f, -14.0f); q.cupInner.lineTo (6.2f, -10.4f); q.cupInner.lineTo (-6.2f, -10.4f); q.cupInner.closeSubPath();
        auto lip = [] (float y, float hw, float sag, float h) { juce::Path l; l.startNewSubPath (-hw, y); l.quadraticTo (0, y + 2 * sag, hw, y);
                                                                l.lineTo (hw - 0.2f, y + h); l.quadraticTo (0, y + h + 2 * sag, -hw + 0.2f, y + h); l.closeSubPath(); return l; };
        q.cupLipLow = lip (-11.3f, 6.9f, 0.85f, 1.5f);
        q.cupLipTop = lip (-14.0f, 7.0f, 1.15f, 1.5f);
        q.cupRing.addEllipse (-7.0f, -16.3f, 14.0f, 4.6f);
        q.cupMouth.addEllipse (-5.8f, -15.6f, 11.6f, 3.2f);
        for (float u = 0.08f; u < 0.93f; u += 0.084f)
            for (auto L : { std::pair<float, float> (-14.0f, 1.15f), std::pair<float, float> (-11.3f, 0.85f) })
            {   // the rims' ribs: short ticks down the rolled lips (a quadratic: x linear in u, y sags by 2 u (1 - u) * 2 * sag)
                const float hw = L.first < -12 ? 7.0f : 6.9f, x = -hw + 2 * hw * u, y = L.first + 4 * u * (1 - u) * L.second;
                q.cupRibs.addRectangle (x - 0.14f, y + 0.25f, 0.28f, 1.0f);
            }
        q.cupShade.startNewSubPath (2.6f, -10.0f); q.cupShade.lineTo (5.6f, -10.0f); q.cupShade.lineTo (4.2f, 1.8f); q.cupShade.lineTo (2.0f, 2.6f); q.cupShade.closeSubPath();
        q.cupGlint.startNewSubPath (-5.3f, -9.6f); q.cupGlint.lineTo (-4.3f, -9.6f); q.cupGlint.lineTo (-3.3f, 0.8f); q.cupGlint.lineTo (-4.0f, 1.0f); q.cupGlint.closeSubPath();
        q.straw.addRoundedRectangle (-0.8f, -11.0f, 1.6f, 11.0f, 0.6f);
        q.straw.applyTransform (juce::AffineTransform::rotation (0.32f).translated (1.6f, -14.2f));
        q.ice.addRoundedRectangle (-0.8f, -0.7f, 1.6f, 1.4f, 0.4f);
        { juce::Path all; all.addPath (q.cupOuter); all.addPath (q.cupInner); all.addPath (q.cupLipLow); all.addPath (q.cupLipTop); all.addPath (q.cupRing); all.addPath (q.straw);
          q.cupHalo = outline (all, 2.6f); }
        { juce::Path ink; ink.addPath (q.cupOuter); ink.addPath (q.cupInner); ink.addPath (q.cupLipLow); ink.addPath (q.cupLipTop); ink.addPath (q.cupRing); ink.addPath (q.cupMouth); ink.addPath (q.straw);
          q.cupLine = outline (ink, 0.9f); }
        // ---- the 40 in a paper bag: the neck + lip ring + crown cap out of the bag's crumpled top
        q.neck.addRoundedRectangle (-1.8f, -17.0f, 3.6f, 11.5f, 0.9f);
        q.ring.addRoundedRectangle (-2.15f, -17.4f, 4.3f, 1.2f, 0.5f);
        q.cap.addRoundedRectangle (-2.4f, -19.6f, 4.8f, 2.3f, 0.6f);
        for (int i = 0; i < 5; ++i) q.capTeeth.addRectangle (-1.85f + (float) i * 0.9f, -18.2f, 0.32f, 0.95f);
        q.glint.addRoundedRectangle (-1.15f, -15.8f, 0.6f, 8.0f, 0.3f);
        q.bag.startNewSubPath (-6.6f, 2.6f); q.bag.quadraticTo (-7.5f, -1.6f, -5.2f, -5.0f);
        const float top[][2] { { -5.7f, -6.5f }, { -3.6f, -5.7f }, { -2.7f, -7.9f }, { -1.4f, -6.6f }, { 0.0f, -8.5f }, { 1.3f, -6.6f }, { 2.7f, -8.0f }, { 3.5f, -5.8f }, { 5.7f, -6.6f }, { 5.2f, -5.0f } };
        for (auto& v : top) q.bag.lineTo (v[0], v[1]);
        q.bag.quadraticTo (7.5f, -1.6f, 6.6f, 2.6f); q.bag.quadraticTo (0, 3.5f, -6.6f, 2.6f); q.bag.closeSubPath();
        { juce::Path c; c.startNewSubPath (-2.7f, -7.3f); c.quadraticTo (-3.6f, -4.0f, -3.3f, -0.8f); c.startNewSubPath (2.7f, -7.4f); c.quadraticTo (3.3f, -3.5f, 3.0f, 0.6f);
          c.startNewSubPath (0.0f, -8.0f); c.lineTo (-0.5f, -4.6f); c.startNewSubPath (-6.0f, -2.4f); c.quadraticTo (-3.5f, -1.4f, -1.2f, -2.0f); c.startNewSubPath (1.6f, -0.6f); c.lineTo (4.6f, -1.6f);
          q.creases = outline (c, 0.45f); }
        { juce::Path f; f.startNewSubPath (-4.6f, -4.6f); f.quadraticTo (-5.6f, -1.6f, -5.0f, 1.4f); f.startNewSubPath (-1.6f, -5.6f); f.lineTo (-2.0f, -3.4f); f.startNewSubPath (0.6f, -2.8f); f.lineTo (2.0f, -3.4f);
          q.folds = outline (f, 0.55f); }
        {   // a tall 40: the bag stretched up, the neck + cap raised to match
            const auto up = juce::AffineTransform::translation (0, -3.0f), tall = juce::AffineTransform::scale (1.0f, 1.25f, 0, 3.0f);
            for (auto* pp : { &q.neck, &q.ring, &q.cap, &q.capTeeth, &q.glint }) pp->applyTransform (up);
            for (auto* pp : { &q.bag, &q.creases, &q.folds }) pp->applyTransform (tall);
        }
        { juce::Path all; all.addPath (q.bag); all.addPath (q.neck); all.addPath (q.ring); all.addPath (q.cap); q.bagHalo = outline (all, 2.6f); }
        { juce::Path ink; ink.addPath (q.bag); ink.addPath (q.neck); ink.addPath (q.ring); ink.addPath (q.cap); q.bagLine = outline (ink, 0.9f); }
        return q;
    }();
    return P;
}
// paints the prop for mode TRAP (3) or HIP HOP (4) with the hand at (hx, hy) in the professor's 100x100 space; swirl = the arm's sway
inline void drawUrbanProp (juce::Graphics& g, int mode, float tf, float hx, float hy, float swirl)
{
    const auto& Q = propPaths();
    const Colour inkC (0xff1d1238), rim (0xfff1e9ff);
    if (mode == 3)
    {
        const auto T = juce::AffineTransform::rotation (swirl * 0.45f).translated (hx, hy);
        g.setColour (rim); g.fillPath (Q.cupHalo, T);
        g.setGradientFill (juce::ColourGradient (Colour (0xffffffff), hx - 6, hy, Colour (0xffd8d0ec), hx + 6, hy, false));
        g.fillPath (Q.cupOuter, T); g.fillPath (Q.cupInner, T);
        g.setColour (rgba (120, 100, 170, 0.16f)); g.fillPath (Q.cupShade, T);
        g.setColour (Colour (0xccffffff)); g.fillPath (Q.cupGlint, T);
        g.setColour (Colour (0xfffbf8ff)); g.fillPath (Q.cupLipLow, T); g.fillPath (Q.cupLipTop, T); g.fillPath (Q.cupRing, T);
        g.setColour (rgba (150, 135, 190, 0.75f)); g.fillPath (Q.cupRibs, T);
        {   // the purple top: the surface sloshes against the sway, two ice cubes bob
            juce::Graphics::ScopedSaveState s (g); g.reduceClipRegion (Q.cupMouth, T);
            g.setGradientFill (juce::ColourGradient (Colour (0xffb06bff), hx, hy - 15.6f, Colour (0xff5a1aa8), hx, hy - 12.4f, false)); g.fillPath (Q.cupMouth, T);
            const float slosh = std::sin (tf * 3.1f) * 2.0f - swirl * 4.0f;
            g.setColour (rgba (225, 180, 255, 0.85f)); g.fillPath (Q.ice, juce::AffineTransform::scale (1.6f, 0.38f).translated (slosh, -14.5f).followedBy (T));
            for (int i = 0; i < 2; ++i)
            {
                const float bob = std::sin (tf * 2.2f + (float) i * 2.4f) * 0.35f;
                g.setColour (rgba (232, 246, 255, 0.95f)); g.fillPath (Q.ice, juce::AffineTransform::rotation (0.5f * (float) i - 0.2f + bob * 0.4f).translated (-2.6f + (float) i * 4.4f + slosh * 0.25f, -14.0f + bob).followedBy (T));
            }
        }
        g.setColour (Colour (0xffff4f8b)); g.fillPath (Q.straw, T);
        g.setColour (inkC); g.fillPath (Q.cupLine, T);
    }
    else
    {
        const auto T = juce::AffineTransform::rotation (-0.10f + std::sin (tf * 1.7f) * 0.09f).translated (hx, hy);
        g.setColour (rim); g.fillPath (Q.bagHalo, T);
        g.setGradientFill (juce::ColourGradient (Colour (0xff8a5420), hx - 2, hy, Colour (0xff4e2a0c), hx + 2, hy, false)); g.fillPath (Q.neck, T);
        g.setColour (Colour (0xff6a3e14)); g.fillPath (Q.ring, T);
        g.setColour (rgba (255, 236, 200, 0.55f)); g.fillPath (Q.glint, T);
        g.setGradientFill (juce::ColourGradient (Colour (0xfff2d27a), hx, hy - 22.6f, Colour (0xffc0902e), hx, hy - 20.3f, false)); g.fillPath (Q.cap, T);
        g.setColour (rgba (120, 80, 20, 0.7f)); g.fillPath (Q.capTeeth, T);
        g.setGradientFill (juce::ColourGradient (Colour (0xffdcb47e), hx - 6, hy, Colour (0xffa9773f), hx + 6, hy, false)); g.fillPath (Q.bag, T);
        g.setColour (rgba (255, 236, 205, 0.55f)); g.fillPath (Q.folds, T);
        g.setColour (rgba (110, 68, 28, 0.6f)); g.fillPath (Q.creases, T);
        g.setColour (inkC); g.fillPath (Q.bagLine, T);
    }
}

// ---------------------------------------------------------------- Professor Bernstein (logo), a 100x100 drawing
inline void drawProfessor (juce::Graphics& g, juce::Rectangle<float> r, double t, int mode, float modeBlend = 1.0f, bool withProp = true)   // withProp: tests only
{
    juce::Graphics::ScopedSaveState ss (g);
    const float s = r.getWidth() / 100.0f;
    g.addTransform (juce::AffineTransform::scale (s).translated (r.getX(), r.getY()));
    juce::Path clip; clip.addRoundedRectangle (0, 0, 100, 100, 18); g.reduceClipRegion (clip);
    const float tf = (float) t;
    // sky
    if (mode == 0)
    {
        g.setGradientFill (juce::ColourGradient (Colour (0xff7ec8ff), 0, 0, Colour (0xffffb36b), 0, 100, false)); g.fillRect (0, 0, 100, 100);
        g.setGradientFill (juce::ColourGradient (Colour (0xccfff1a8), 70, 22, Colour (0x00ffc35a), 70 + 38, 22, true)); g.fillRect (0, 0, 100, 100);
        g.setColour (Colour (0xffffe27a)); g.fillEllipse (60, 12, 20, 20);
        for (int i = 0; i < 10; ++i) { const float a = tf * 0.4f + (float) i * 0.628f; juce::Path ray; ray.startNewSubPath (70 + std::cos (a) * 13, 22 + std::sin (a) * 13); ray.lineTo (70 + std::cos (a) * 18, 22 + std::sin (a) * 18); g.setColour (Colour (0xaaffe27a)); g.strokePath (ray, stroke (1.4f)); }
    }
    else if (mode == 1)
    {
        juce::ColourGradient gr (Colour (0xff3a1450), 0, 0, Colour (0xffff8a5c), 0, 100, false); gr.addColour (0.55, Colour (0xffd0408a)); g.setGradientFill (gr); g.fillRect (0, 0, 100, 100);
        g.setColour (Colour (0xffffc35a).withAlpha (0.85f)); g.fillEllipse (58, 58, 26, 26);
        g.setGradientFill (juce::ColourGradient (Colour (0x66ffd2a0), 71, 71, Colour (0x00ffd2a0), 71 + 34, 71, true)); g.fillRect (0, 0, 100, 100);
    }
    else
    {
        g.setGradientFill (juce::ColourGradient (Colour (0xff2a1650), 0, 0, Colour (0xff0d0820), 0, 100, false)); g.fillRect (0, 0, 100, 100);
        g.setGradientFill (juce::ColourGradient (rgba (57, 255, 154, 0.28f), 66, 26, rgba (57, 255, 154, 0), 66 + 40, 26, true)); g.fillRect (0, 0, 100, 100);
        for (int i = 0; i < 14; ++i)
        {
            const float x = seeded (i * 2 + 1) * 100, y = seeded (i * 2 + 2) * 45, tw = 0.5f + 0.5f * std::sin (tf * 2.3f + (float) i * 1.9f);
            g.setColour (rgba (230, 220, 255, 0.2f + 0.6f * tw)); g.fillEllipse (x - (0.5f + 0.7f * tw), y - (0.5f + 0.7f * tw), 2 * (0.5f + 0.7f * tw), 2 * (0.5f + 0.7f * tw));
        }
    }
    (void) modeBlend;
    // ground + posts
    g.setColour (rgba (155, 92, 255, 0.25f)); g.fillPath (ell (50, 99, 60, 9));
    for (float x : { 9.0f, 91.0f })
    {
        g.setGradientFill (juce::ColourGradient (Colour (0xff6b3fc4), x - 3, 0, Colour (0xff3b2178), x + 3, 0, false));
        g.fillRoundedRectangle (x - 2.6f, 24, 5.2f, 80, 2.6f);
        g.setColour (Colour (0xff39ff9a)); g.fillEllipse (x - 3.4f, 24 - 3.4f, 6.8f, 6.8f);
        g.setColour (Colour (0x99ffffff)); g.fillEllipse (x - 1 - 1.1f, 23 - 1.1f, 2.2f, 2.2f);
    }
    const float sway = std::sin (tf * 1.25f) * 0.05f;
    juce::Graphics::ScopedSaveState s2 (g);
    g.addTransform (juce::AffineTransform::rotation (sway, 50, 26));
    {
        juce::Path ropes;
        const float pts[4][4] { { 9, 27, 19, 52 }, { 9, 27, 22, 54 }, { 91, 27, 81, 52 }, { 91, 27, 78, 54 } };
        for (auto& q : pts) { ropes.startNewSubPath (q[0], q[1]); ropes.lineTo (q[2], q[3]); }
        g.setColour (Colour (0xffd9c7ff)); g.strokePath (ropes, stroke (0.9f));
    }
    const Colour inkC (0xff1d1238);
    g.setColour (Colour (0xffeafff4)); g.fillPath (ell (24, 41, 6, 5, -0.5f)); g.setColour (inkC); g.strokePath (ell (24, 41, 6, 5, -0.5f), stroke (1.2f));
    {   // head
        juce::Graphics::ScopedSaveState s3 (g);
        g.addTransform (juce::AffineTransform::rotation (-0.22f).translated (37, 45));
        juce::ColourGradient hg (Colour (0xffffffff), -4, -5, Colour (0xffd9ccff), 15 - 4, -5, true);
        g.setGradientFill (hg); g.fillPath (ell (0, 0, 13, 12.5f)); g.setColour (inkC); g.strokePath (ell (0, 0, 13, 12.5f), stroke (1.2f));
        const float tufts[7][3] { { -12, -6, 3.6f }, { -13.5f, -1, 3 }, { -11, -10, 3 }, { 11, -9, 3.2f }, { 13, -4, 3 }, { -6, -12.5f, 2.6f }, { 2, -13.4f, 2.4f } };
        for (auto& q : tufts) { auto e = ell (q[0] + std::sin (tf * 3 + q[0]) * 0.25f, q[1], q[2], q[2] * 0.85f); g.setColour (Colours::white); g.fillPath (e); g.setColour (inkC); g.strokePath (e, stroke (0.9f)); }
        g.setGradientFill (hg); g.fillPath (ell (0, 0.5f, 12.2f, 11.6f));
        g.setColour (rgba (255, 120, 190, 0.55f)); g.fillPath (ell (-7.5f, 4.5f, 2.8f, 1.8f)); g.fillPath (ell (7.5f, 4.5f, 2.8f, 1.8f));
        if (mode == 0)
        {   // sunglasses
            for (float ex : { -4.8f, 4.8f }) { g.setColour (Colour (0xff1d1238)); g.fillRoundedRectangle (ex - 4.4f, -3.4f, 8.8f, 6.0f, 2.6f); g.setColour (Colour (0x88ffffff)); g.drawLine (ex - 2.6f, -2.2f, ex - 0.6f, -2.2f, 0.7f); }
            g.setColour (inkC); g.drawLine (-0.6f, -1.0f, 0.6f, -1.0f, 1.0f);
        }
        else
        {
            if (mode == 2)
            {   // glowing eyes
                const float gl = 0.6f + 0.4f * std::sin (tf * 3.0f);
                for (float ex : { -4.8f, 4.8f })
                {
                    g.setGradientFill (juce::ColourGradient (rgba (61, 227, 255, 0.85f * gl), ex, 0.2f, rgba (61, 227, 255, 0), ex + 4.5f, 0.2f, true)); g.fillEllipse (ex - 4.5f, 0.2f - 4.5f, 9, 9);
                    g.setColour (Colour (0xffcfffff)); g.fillEllipse (ex - 1.3f, 0.2f - 1.3f, 2.6f, 2.6f);
                }
            }
            else
            {   // happy closed eyes
                juce::Path e; for (float ex : { -4.8f, 4.8f }) arcTo (e, ex, 0.6f, 2.2f, juce::MathConstants<float>::pi * 1.1f, juce::MathConstants<float>::pi * 1.9f, true);
                g.setColour (inkC); g.strokePath (e, stroke (1.3f));
            }
            for (float ex : { -4.8f, 4.8f }) { g.setColour (rgba (160, 220, 255, 0.18f)); g.fillEllipse (ex - 4.2f, -0.2f - 4.2f, 8.4f, 8.4f); g.setColour (Colour (0xff3d2a6e)); g.drawEllipse (ex - 4.2f, -0.2f - 4.2f, 8.4f, 8.4f, 0.9f); }
            juce::Path br; br.startNewSubPath (-0.8f, -0.6f); br.quadraticTo (0, -1.6f, 0.8f, -0.6f); g.strokePath (br, stroke (0.9f));
        }
        juce::Path sm; arcTo (sm, 0, 5.2f, 2.6f, 0.2f, juce::MathConstants<float>::pi - 0.2f, true);
        g.setColour (inkC); g.strokePath (sm, stroke (1.1f));
    }
    // the raised arm with the vial
    const float swirl = std::sin (tf * 2.6f) * 0.22f, hx = 61 + std::sin (tf * 2.6f) * 1.2f, hy = 37 + std::cos (tf * 2.6f) * 0.8f;
    { juce::Path arm; arm.startNewSubPath (48, 58); arm.quadraticTo (57, 52, hx, hy + 3); g.setColour (inkC); g.strokePath (arm, stroke (8.4f)); g.setColour (Colour (0xffeafff4)); g.strokePath (arm, stroke (6.2f)); }
    if (mode == 3 || mode == 4) { if (withProp) drawUrbanProp (g, mode, tf, hx, hy, swirl); }   // TRAP: the double cup, HIP HOP: the 40 in a bag
    else
    {
        juce::Graphics::ScopedSaveState s4 (g);
        g.addTransform (juce::AffineTransform::rotation (swirl).translated (hx, hy));
        const float glow = 0.55f + 0.25f * std::sin (tf * 4);
        g.setGradientFill (juce::ColourGradient (rgba (57, 255, 154, 0.45f * glow), 0, -6, rgba (57, 255, 154, 0), 16, -6, true)); g.fillRect (-18.0f, -24.0f, 36.0f, 36.0f);
        juce::Path flask; flask.startNewSubPath (-1.8f, -16); flask.lineTo (-1.8f, -10); flask.lineTo (-7, -1); flask.quadraticTo (-8, 1.5f, -5, 1.5f); flask.lineTo (5, 1.5f); flask.quadraticTo (8, 1.5f, 7, -1); flask.lineTo (1.8f, -10); flask.lineTo (1.8f, -16); flask.closeSubPath();
        g.setColour (rgba (220, 240, 255, 0.25f)); g.fillPath (flask);
        {
            juce::Graphics::ScopedSaveState s5 (g); g.reduceClipRegion (flask);
            const float lvl = -6.5f, tilt = -swirl * 6; juce::Path liq; liq.startNewSubPath (-9, lvl - tilt);
            for (int x = -9; x <= 9; ++x) liq.lineTo ((float) x, lvl + std::sin ((float) x * 0.9f + tf * 7) * 0.5f - tilt * (float) x / 9);
            liq.lineTo (9, 3); liq.lineTo (-9, 3); liq.closeSubPath();
            g.setGradientFill (juce::ColourGradient (Colour (0xff7dffbf), 0, -8, Colour (0xff14c76a), 0, 2, false)); g.fillPath (liq);
            for (int i = 0; i < 4; ++i) { const float ph = std::fmod (tf * 1.6f + (float) i * 0.27f, 1.0f); g.setColour (Colour (0xb3ffffff)); g.fillEllipse (-3 + (float) i * 2 + std::sin (tf * 5 + (float) i) * 0.6f - 0.55f, 1 - ph * 7 - 0.55f, 1.1f, 1.1f); }
        }
        g.setColour (inkC); g.strokePath (flask, stroke (0.9f));
        g.setColour (Colour (0x8cffffff)); g.fillRect (-4.6f, -3.5f, 1.0f, 3.0f);
        g.setColour (inkC); g.drawLine (-2.8f, -16, 2.8f, -16, 1.0f);
    }
    g.setColour (Colours::white); g.fillPath (ell (hx, hy + 1.2f, 3, 2.6f)); g.setColour (inkC); g.strokePath (ell (hx, hy + 1.2f, 3, 2.6f), stroke (0.9f));
    for (int i = 0; i < (mode == 3 || mode == 4 ? 0 : 6); ++i)   // the flask's bubbles
    {
        const float ph = std::fmod (tf * 0.55f + (float) i / 6, 1.0f), x = hx + std::sin (swirl) * 16 + std::sin (tf * 2 + (float) i * 2.1f) * (2 + ph * 3), y = hy - 17 - ph * 26, rr = 0.8f + ph * 1.9f;
        g.setColour (rgba (57, 255, 154, 0.22f * (1 - ph))); g.fillEllipse (x - rr, y - rr, 2 * rr, 2 * rr);
        g.setColour (rgba (125, 255, 191, 0.9f * (1 - ph))); g.drawEllipse (x - rr, y - rr, 2 * rr, 2 * rr, 0.7f);
    }
    // hammock bed
    juce::Path bed; bed.startNewSubPath (18, 51); bed.quadraticTo (50, 70, 82, 51); bed.quadraticTo (50, 90, 18, 51); bed.closeSubPath();
    {
        juce::Graphics::ScopedSaveState s6 (g); g.reduceClipRegion (bed);
        const Colour stA = mode == 0 ? Colour (0xffffc35a) : mode == 1 ? Colour (0xffff8a5c) : Colour (0xff39ff9a);
        const Colour stB = mode == 0 ? Colour (0xff7ec8ff) : mode == 1 ? Colour (0xffb06bff) : Colour (0xff9b5cff);
        for (int i = -12; i < 20; ++i) { juce::Path st; const float x = (float) i * 6; st.startNewSubPath (x, 40); st.lineTo (x + 6, 40); st.lineTo (x + 18, 95); st.lineTo (x + 12, 95); st.closeSubPath(); g.setColour (i % 2 ? stA : stB); g.fillPath (st); }
        g.setColour (rgba (13, 8, 32, 0.28f)); g.fillRect (0.0f, 66.0f, 100.0f, 30.0f);
    }
    g.setColour (inkC); g.strokePath (bed, stroke (1.3f));
    { juce::Path hl; hl.startNewSubPath (19, 51.5f); hl.quadraticTo (50, 70, 81, 51.5f); g.setColour (Colour (0x8cffffff)); g.strokePath (hl, stroke (1)); }
    { juce::Path col; col.startNewSubPath (28, 54); col.lineTo (36, 52); col.lineTo (40, 57.5f); col.lineTo (45, 52); col.lineTo (51, 54.5f); col.quadraticTo (40, 61, 28, 54); col.closeSubPath();
      g.setColour (Colour (0xffeafff4)); g.fillPath (col); g.setColour (inkC); g.strokePath (col, stroke (1)); }
    { juce::Path bow; bow.startNewSubPath (37.6f, 55); bow.lineTo (40, 57.8f); bow.lineTo (42.4f, 55); bow.lineTo (40, 54.2f); bow.closeSubPath(); g.setColour (mode == 0 ? Colour (0xffff7a59) : Colour (0xff9b5cff)); g.fillPath (bow); }
    const float wig = std::sin (tf * 2) * 1.2f;
    juce::Path tail; tail.startNewSubPath (70, 56); tail.quadraticTo (80, 50, 85, 54); tail.quadraticTo (89, 58 + wig, 92, 57 + wig); tail.quadraticTo (90, 62 + wig, 86, 61); tail.quadraticTo (84, 64, 80, 61.5f); tail.quadraticTo (76, 62, 73, 60); tail.closeSubPath();
    g.setColour (Colours::white); g.fillPath (tail); g.setColour (inkC); g.strokePath (tail, stroke (1.1f));
}

// ---------------------------------------------------------------- the mood ghost
struct Face { float eye, smileEye, brow, mouth, open, wave, blush, tear, sweat, spark, dream; float hue[3]; };
inline Face faceFor (const std::string& mood)
{
    if (mood == "dreamy") return { 0.35f, 0.2f, -0.25f, 0.45f, 0, 0, 0.7f, 0, 0, 0.2f, 1, { 180, 140, 255 } };
    if (mood == "sad")    return { 0.75f, 0, -1, -0.85f, 0.05f, 0, 0.1f, 1, 0, 0, 0, { 95, 168, 255 } };
    if (mood == "tense")  return { 1.3f, 0, -0.85f, -0.15f, 0.2f, 1, 0, 0, 1, 0, 0, { 255, 96, 96 } };
    if (mood == "epic")   return { 0.95f, 0, 1, 0.9f, 0.9f, 0, 0.35f, 0, 0, 1, 0, { 57, 255, 154 } };
    return { 1, 1, 0, 1, 0.45f, 0, 1, 0, 0, 0.4f, 0, { 255, 214, 74 } };
}
inline Face lerpFace (const Face& a, const Face& b, float k)
{
    auto L = [k] (float x, float y) { return x + (y - x) * k; };
    Face o { L (a.eye, b.eye), L (a.smileEye, b.smileEye), L (a.brow, b.brow), L (a.mouth, b.mouth), L (a.open, b.open), L (a.wave, b.wave), L (a.blush, b.blush), L (a.tear, b.tear), L (a.sweat, b.sweat), L (a.spark, b.spark), L (a.dream, b.dream), { L (a.hue[0], b.hue[0]), L (a.hue[1], b.hue[1]), L (a.hue[2], b.hue[2]) } };
    return o;
}
inline void drawMoodGhost (juce::Graphics& g, juce::Rectangle<float> r, const Face& F, double t, float ear = 0)
{
    juce::Graphics::ScopedSaveState ss (g);
    const float s = r.getWidth() / 100.0f, tf = (float) t;
    g.addTransform (juce::AffineTransform::scale (s).translated (r.getX(), r.getY()));
    const int hr = (int) F.hue[0], hgc = (int) F.hue[1], hb = (int) F.hue[2];
    const float bob = std::sin (tf * 2.1f) * 2.5f * (1 - F.sweat * 0.5f) + std::sin (tf * 23) * F.sweat * 0.5f;
    g.setGradientFill (juce::ColourGradient (rgba (hr, hgc, hb, 0.35f), 50, 48, rgba (hr, hgc, hb, 0), 100, 48, true)); g.fillRect (0, 0, 100, 100);
    g.addTransform (juce::AffineTransform::translation (0, bob));
    const Colour inkC (0xff1d1238);
    juce::Path body; body.startNewSubPath (20, 46); body.cubicTo (20, 12, 80, 12, 80, 46); body.lineTo (80, 80);
    for (int i = 0; i <= 6; ++i) { const float x = 80 - (float) i * 10, y = 82 + (i % 2 ? -5.0f : 2.0f) + std::sin (tf * 4 + (float) i) * 1.6f; body.quadraticTo (x + 5, y + 4, x, y); }
    body.closeSubPath();
    g.setGradientFill (juce::ColourGradient (Colours::white, 30, 20, Colour ((juce::uint8) juce::jmin (255, 200 + hr / 5), (juce::uint8) juce::jmin (255, 200 + hgc / 5), (juce::uint8) juce::jmin (255, 215 + (int) (hb * 0.15f))), 70, 90, false));
    g.fillPath (body); g.setColour (inkC); g.strokePath (body, stroke (2.2f));
    if (F.blush > 0.02f) { g.setColour (rgba (255, 120, 180, 0.55f * F.blush)); g.fillPath (ell (30, 55, 6, 3.5f)); g.fillPath (ell (70, 55, 6, 3.5f)); }
    for (float ex : { 37.0f, 63.0f })
    {
        const float h = 8.5f * F.eye, ey = 44;
        if (F.smileEye < 0.98f)
        {
            const float al = 1 - F.smileEye;
            g.setColour (inkC.withAlpha (al)); g.fillPath (ell (ex, ey, 5 * (F.eye > 1 ? 1.1f : 1.0f), juce::jmax (0.8f, h)));
            g.setColour (Colours::white.withAlpha (al)); g.fillPath (ell (ex + 1.6f, ey - h * 0.35f, 1.6f, juce::jmax (0.4f, 1.8f * juce::jmin (1.0f, F.eye))));
            if (F.spark > 0.3f) { g.setColour (rgba (255, 255, 255, F.spark * al)); g.fillEllipse (ex - 1.6f - 0.9f, ey + h * 0.3f - 0.9f, 1.8f, 1.8f); }
            if (F.dream > 0.3f) { g.setColour (rgba (29, 18, 56, F.dream * al)); g.fillRect (ex - 6, ey - 10, 12.0f, juce::jmax (0.0f, 10 - h * 0.6f)); g.setColour (inkC.withAlpha (al)); g.drawLine (ex - 5.5f, ey - h * 0.55f, ex + 5.5f, ey - h * 0.55f, 2); }
        }
        if (F.smileEye > 0.02f) { juce::Path a; arcTo (a, ex, ey + 2.5f, 5, juce::MathConstants<float>::pi * 1.15f, juce::MathConstants<float>::pi * 1.85f, true); g.setColour (inkC.withAlpha (F.smileEye)); g.strokePath (a, stroke (2.6f)); }
        const float inner = ex < 50 ? 1.0f : -1.0f, dy = F.brow * 3.2f;
        g.setColour (inkC); g.drawLine (ex - 5 * inner, 34 - dy * 0.2f - (F.eye - 1) * 3, ex + 5 * inner, 34 + dy - (F.eye - 1) * 3, 2.2f);
    }
    const float my = 60;
    if (F.wave > 0.5f)
    {
        juce::Path w; for (int i = 0; i <= 12; ++i) { const float x = 41 + (float) i * 1.5f, y = my + 1 + std::sin ((float) i * 1.6f + tf * 6) * 1.6f * F.wave; if (i) w.lineTo (x, y); else w.startNewSubPath (x, y); }
        g.setColour (inkC); g.strokePath (w, stroke (2.2f));
    }
    else
    {
        const float w = 9 + F.open * 2, cu = F.mouth * 7;
        if (F.open > 0.15f)
        {
            juce::Path m; m.startNewSubPath (50 - w, my); m.quadraticTo (50, my + cu * 0.35f, 50 + w, my); m.quadraticTo (50, my + cu + F.open * 9 * (cu < 0 ? -1.0f : 1.0f), 50 - w, my); m.closeSubPath();
            g.setColour (Colour (0xff5a1630)); g.fillPath (m); g.setColour (inkC); g.strokePath (m, stroke (2.2f));
            juce::Graphics::ScopedSaveState s2 (g); g.reduceClipRegion (m); g.setColour (Colour (0xffff7aa8)); g.fillPath (ell (50, my + cu * 0.5f + F.open * 6, 5, 3.5f));
        }
        else { juce::Path m; m.startNewSubPath (50 - w * 0.8f, my); m.quadraticTo (50, my + cu * 1.2f, 50 + w * 0.8f, my); g.setColour (inkC); g.strokePath (m, stroke (2.2f)); }
    }
    if (F.tear > 0.05f)
    {
        const float ph = std::fmod (tf * 0.6f, 1.0f), x = 33, y = 50 + ph * 18; juce::Path d; d.startNewSubPath (x, y - 4); d.quadraticTo (x + 3.2f, y + 1, x, y + 2.6f); d.quadraticTo (x - 3.2f, y + 1, x, y - 4);
        g.setColour (rgba (110, 190, 255, F.tear * (1 - ph * 0.6f))); g.fillPath (d);
    }
    if (F.sweat > 0.05f)
    {
        const float x = 76, y = 30 + std::sin (tf * 3) * 1.5f; juce::Path d; d.startNewSubPath (x, y - 5); d.quadraticTo (x + 4, y + 1, x, y + 3.5f); d.quadraticTo (x - 4, y + 1, x, y - 5);
        g.setColour (rgba (140, 210, 255, F.sweat)); g.fillPath (d); g.setColour (inkC); g.strokePath (d, stroke (1));
    }
    if (F.spark > 0.05f)
        for (int i = 0; i < 4; ++i)
        {
            const float a = tf * 1.5f + (float) i * juce::MathConstants<float>::halfPi, x = 50 + std::cos (a) * 44, y = 42 + std::sin (a) * 34, z = (2 + std::sin (tf * 5 + (float) i) * 1.2f) * F.spark;
            juce::Path st; for (int k = 0; k < 8; ++k) { const float rr = k % 2 ? z * 0.35f : z, an = (float) k * juce::MathConstants<float>::pi / 4; if (k) st.lineTo (x + std::cos (an) * rr, y + std::sin (an) * rr); else st.startNewSubPath (x + std::cos (an) * rr, y + std::sin (an) * rr); }
            st.closeSubPath(); g.setColour (rgba (hr, hgc, hb, F.spark)); g.fillPath (st);
        }
    if (F.dream > 0.05f)
    {
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        for (int i = 0; i < 2; ++i) { const float ph = std::fmod (tf * 0.35f + (float) i * 0.5f, 1.0f); g.setColour (rgba (200, 180, 255, F.dream * (1 - ph))); g.drawText ("z", juce::Rectangle<float> (78 + ph * 10, 18 - ph * 18 + (float) i * 2, 10, 12), juce::Justification::centred); }
    }
    if (ear > 0.02f)
    {
        const float a = juce::jmin (1.0f, ear);
        juce::Path arm; arm.startNewSubPath (76, 62); arm.quadraticTo (90, 60, 86, 44); arm.lineTo (80, 46); arm.quadraticTo (82, 56, 76, 56); arm.closeSubPath();
        juce::Path hand; hand.startNewSubPath (80, 47); hand.quadraticTo (84, 30, 92, 36); hand.quadraticTo (90, 44, 86, 46); hand.closeSubPath();
        for (auto* p : { &arm, &hand }) { g.setColour (Colours::white.withAlpha (a)); g.fillPath (*p); g.setColour (inkC.withAlpha (a)); g.strokePath (*p, stroke (2.2f)); }
        for (int i = 0; i < 3; ++i) { const float ph = std::fmod (tf * 0.9f + (float) i / 3, 1.0f), rr = 16 - ph * 12; juce::Path w; arcTo (w, 90, 40, rr, -0.6f, 0.6f, true); g.setColour (rgba (61, 227, 255, 0.9f * a * ph)); g.strokePath (w, stroke (1.8f)); }
    }
}

// a few twinkling stars / a sun glow for the window background
inline void drawBackdrop (juce::Graphics& g, juce::Rectangle<float> r, const Palette& P, int mode, double t, float modeAlpha)
{
    juce::ColourGradient gr (P.bg2, r.getX(), r.getY(), P.bg3, r.getX(), r.getBottom(), false);   // the theme's window (opaque; no full-window glow layers)
    g.setGradientFill (gr); g.fillRect (r);
    if (mode == 2)
        for (int i = 0; i < 70; ++i)
        {
            const float x = r.getX() + seeded (i * 3 + 7) * r.getWidth(), y = r.getY() + seeded (i * 3 + 8) * r.getHeight(), tw = 0.5f + 0.5f * std::sin ((float) t * (1.2f + seeded (i) * 2) + (float) i);
            g.setColour (P.ink.withAlpha ((P.dark ? 0.12f + 0.35f * tw : 0.05f + 0.1f * tw) * modeAlpha)); const float rr = 0.6f + 0.9f * seeded (i * 5);
            g.fillEllipse (x - rr, y - rr, 2 * rr, 2 * rr);
        }
    // (SUNRISE / DUSK: no full-window glow layers any more - the shared themes paint flat, opaque surfaces)
}
} // namespace arpui
