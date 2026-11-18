#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "GhostLabFonts.h"

namespace glab::theme
{
const juce::Colour bg { 0xff121314 }, panel { 0xff17181a }, panelHi { 0xff1c1d20 }, border { 0xff2a2b2e }, borderHi { 0xff3a3b40 };
const juce::Colour purple { 0xffa66bff }, green { 0xffa6f23a }, text { 0xfff2f2f3 }, sub { 0xff9a9ca2 }, dim { 0xff5e6066 };
const juce::Colour ice { 0xffcfe9ff }, red { 0xffff6b6b };
// the lab at night (build 9): the backdrop, the glass, and the card colours (each card / category its own neon)
const juce::Colour night0 { 0xff0b0918 }, night1 { 0xff141029 }, night2 { 0xff1c1636 }, tubeInside { 0xff120e24 };
const juce::Colour cyan { 0xff4fd8ff }, pink { 0xffff5fd2 }, yellow { 0xffffd84a }, mint { 0xff5cf2a0 }, blue { 0xff6b8cff };

inline juce::Typeface::Ptr face (int weight)   // 0 medium, 1 semibold, 2 bold
{
    static juce::Typeface::Ptr f[3] = {
        juce::Typeface::createSystemTypefaceFor (GhostLabFonts::PoppinsMedium_ttf, GhostLabFonts::PoppinsMedium_ttfSize),
        juce::Typeface::createSystemTypefaceFor (GhostLabFonts::PoppinsSemiBold_ttf, GhostLabFonts::PoppinsSemiBold_ttfSize),
        juce::Typeface::createSystemTypefaceFor (GhostLabFonts::PoppinsBold_ttf, GhostLabFonts::PoppinsBold_ttfSize) };
    return f[juce::jlimit (0, 2, weight)];
}
inline juce::Font font (float size, int weight = 0, float kerning = 0.0f)
{
    return juce::Font (juce::FontOptions (face (weight)).withHeight (size * (size < 30.0f ? 1.2f : 1.08f))).withExtraKerningFactor (kerning);
}

// ---- our colours on JUCE's own parts (menus, lists, combo boxes, scroll bars, text boxes, dialogs): never JUCE's defaults.
// Held by each open editor through a SharedResourcePointer (a skin, not state: nothing about the sound or the session lives here).
struct LabLook : juce::LookAndFeel_V4
{
    LabLook() : juce::LookAndFeel_V4 (juce::LookAndFeel_V4::ColourScheme (
        bg, panel, panelHi, border, text, purple.withAlpha (0.55f), text, panelHi, text))
    {
        setColour (juce::PopupMenu::backgroundColourId, panel);
        setColour (juce::PopupMenu::textColourId, text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, purple.withAlpha (0.35f));
        setColour (juce::PopupMenu::highlightedTextColourId, text);
        setColour (juce::PopupMenu::headerTextColourId, sub);
        setColour (juce::ComboBox::backgroundColourId, bg);
        setColour (juce::ComboBox::outlineColourId, border);
        setColour (juce::ComboBox::textColourId, text);
        setColour (juce::ComboBox::arrowColourId, green);
        setColour (juce::ComboBox::focusedOutlineColourId, purple);
        setColour (juce::TextButton::buttonColourId, panelHi);
        setColour (juce::TextButton::buttonOnColourId, purple.withAlpha (0.55f));
        setColour (juce::TextButton::textColourOffId, text);
        setColour (juce::TextButton::textColourOnId, text);
        setColour (juce::ToggleButton::tickColourId, green);
        setColour (juce::ToggleButton::tickDisabledColourId, dim);
        setColour (juce::ToggleButton::textColourId, text);
        setColour (juce::TextEditor::backgroundColourId, bg);
        setColour (juce::TextEditor::textColourId, text);
        setColour (juce::TextEditor::outlineColourId, border);
        setColour (juce::TextEditor::focusedOutlineColourId, purple);
        setColour (juce::TextEditor::highlightColourId, purple.withAlpha (0.35f));
        setColour (juce::CaretComponent::caretColourId, green);
        setColour (juce::ListBox::backgroundColourId, panel);
        setColour (juce::ListBox::outlineColourId, border);
        setColour (juce::ScrollBar::thumbColourId, borderHi);
        setColour (juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);
        setColour (juce::AlertWindow::backgroundColourId, panel);
        setColour (juce::AlertWindow::textColourId, text);
        setColour (juce::AlertWindow::outlineColourId, purple.withAlpha (0.6f));
        setColour (juce::Slider::trackColourId, green);
        setColour (juce::Slider::backgroundColourId, border);
        setColour (juce::Slider::thumbColourId, text);
        setColour (juce::Slider::textBoxTextColourId, sub);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Label::textColourId, text);
        setColour (juce::TooltipWindow::backgroundColourId, panelHi);
        setColour (juce::TooltipWindow::textColourId, text);
        setColour (juce::TooltipWindow::outlineColourId, border);
        setColour (juce::ResizableWindow::backgroundColourId, bg);
        setColour (juce::DocumentWindow::textColourId, text);
    }
    juce::Font getPopupMenuFont() override { return font (13.0f); }
    juce::Font getAlertWindowMessageFont() override { return font (12.5f); }
    juce::Font getAlertWindowTitleFont() override { return font (16.0f, 2, 0.06f); }
    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return font (juce::jmin (14.0f, (float) h * 0.45f), 1, 0.08f); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return font (12.5f); }
};


// ---- the lab at night (build 9): drawn, no images (the scene, the glass specimen tube, flasks, the ghost scientist)
namespace art
{
using G = juce::Graphics;
using R = juce::Rectangle<float>;
using P = juce::Point<float>;

inline float sq (float x) { return x * x; }
inline void glow (G& g, const juce::Path& p, juce::Colour c, int radius)
{
    juce::DropShadow (c, radius, {}).drawForPath (g, p);
}

// Brandon's own Ghost Lab banner (the title, the ghost, the whole lab): the top of every page
inline juce::Image bannerImage() { return juce::ImageCache::getFromMemory (GhostLabFonts::lab_banner_png, GhostLabFonts::lab_banner_pngSize); }
// the banner's top `srcRows` rows across `area`, scaled to its height; a window wider than that gets the banner's own
// edges mirrored out to the sides (the room carries on), and the bottom fades into the page
inline void banner (G& g, R area, float srcRows)
{
    auto im = bannerImage();
    if (! im.isValid() || area.isEmpty()) return;
    const int rows = juce::jlimit (1, im.getHeight(), juce::roundToInt (srcRows));
    auto top = im.getClippedImage ({ 0, 0, im.getWidth(), rows });
    const float k = area.getHeight() / (float) rows, bw = (float) im.getWidth() * k;
    const float x0 = area.getCentreX() - bw * 0.5f;
    g.setOpacity (1.0f);
    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (area.toNearestInt());
    if (x0 > area.getX())
    {
        g.drawImageTransformed (top, juce::AffineTransform::scale (-k, k).translated (x0, area.getY()));
        g.drawImageTransformed (top, juce::AffineTransform::scale (-k, k).translated (x0 + 2.0f * bw, area.getY()));
        for (int side = 0; side < 2; ++side)
        {
            const float ex = side == 0 ? x0 : x0 + bw;
            g.setGradientFill (juce::ColourGradient (night0.withAlpha (0.0f), ex, 0, night0.withAlpha (0.55f), side == 0 ? area.getX() : area.getRight(), 0, false));
            g.fillRect (side == 0 ? area.withRight (ex) : area.withLeft (ex));
        }
    }
    g.drawImageTransformed (top, juce::AffineTransform::scale (k).translated (x0, area.getY()));
    // the bottom fades into the page
    g.setGradientFill (juce::ColourGradient (night0.withAlpha (0.0f), 0, area.getBottom() - 40, night0.withAlpha (0.92f), 0, area.getBottom(), false));
    g.fillRect (area.withTop (area.getBottom() - 40));
}

// a round-bottomed flask standing on `base` (its bottom centre), `s` tall
inline void flask (G& g, P base, float s, juce::Colour liquid, float level = 0.55f, bool conical = false)
{
    juce::Path glass;
    const float neckW = s * 0.24f, bodyR = s * 0.36f;
    if (conical)
    {
        glass.startNewSubPath (base.x - neckW * 0.5f, base.y - s);
        glass.lineTo (base.x - neckW * 0.5f, base.y - s * 0.62f);
        glass.lineTo (base.x - s * 0.42f, base.y - s * 0.04f);
        glass.quadraticTo (base.x - s * 0.44f, base.y, base.x - s * 0.36f, base.y);
        glass.lineTo (base.x + s * 0.36f, base.y);
        glass.quadraticTo (base.x + s * 0.44f, base.y, base.x + s * 0.42f, base.y - s * 0.04f);
        glass.lineTo (base.x + neckW * 0.5f, base.y - s * 0.62f);
        glass.lineTo (base.x + neckW * 0.5f, base.y - s);
        glass.closeSubPath();
    }
    else
    {
        glass.addRectangle (base.x - neckW * 0.5f, base.y - s, neckW, s * 0.4f);
        glass.addEllipse (base.x - bodyR, base.y - bodyR * 2.0f, bodyR * 2.0f, bodyR * 2.0f);
    }
    // liquid: the glass below the level line, glowing
    {
        const float top = base.y - s * level * (conical ? 0.62f : 0.72f);
        juce::Path below; below.addRectangle (base.x - s, top, s * 2.0f, s * 2.0f);
        // the liquid: the glass drawn through a clip below the level line
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (below);
        glow (g, glass, liquid.withAlpha (0.55f), (int) (s * 0.35f));
        g.setGradientFill (juce::ColourGradient (liquid.brighter (0.5f), base.x, top, liquid.darker (0.4f), base.x, base.y, false));
        g.fillPath (glass);
        // bubbles
        juce::Random rnd ((juce::int64) (base.x * 13 + base.y));
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        for (int i = 0; i < 6; ++i)
        {
            const float bs = s * (0.025f + 0.04f * rnd.nextFloat());
            g.drawEllipse (base.x - s * 0.25f + s * 0.5f * rnd.nextFloat(), top + (base.y - top) * rnd.nextFloat() * 0.8f, bs, bs, 0.8f);
        }
    }
    g.setColour (juce::Colours::white.withAlpha (0.05f)); g.fillPath (glass);
    g.setColour (juce::Colours::white.withAlpha (0.38f)); g.strokePath (glass, juce::PathStrokeType (juce::jmax (1.0f, s * 0.018f)));
    // a highlight on the glass
    g.setColour (juce::Colours::white.withAlpha (0.25f));
    g.drawLine (base.x - s * 0.22f, base.y - s * 0.5f, base.x - s * 0.27f, base.y - s * 0.28f, juce::jmax (1.0f, s * 0.03f));
    // the rim
    g.setColour (juce::Colours::white.withAlpha (0.45f));
    g.fillRoundedRectangle (base.x - neckW * 0.65f, base.y - s - s * 0.03f, neckW * 1.3f, s * 0.05f, s * 0.02f);
}

// a test tube in a rack slot
inline void testTube (G& g, R r, juce::Colour liquid, float level)
{
    juce::Path t; t.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), r.getWidth() * 0.5f, r.getWidth() * 0.5f, false, false, true, true);
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (r.withTrimmedTop (r.getHeight() * (1.0f - level)).toNearestInt());
        glow (g, t, liquid.withAlpha (0.5f), (int) r.getWidth());
        g.setColour (liquid); g.fillPath (t);
    }
    g.setColour (juce::Colours::white.withAlpha (0.35f)); g.strokePath (t, juce::PathStrokeType (1.0f));
}

// a metal pipe along a path, with joints at its corners
inline void pipe (G& g, const juce::Path& path, float thick, juce::Colour tint)
{
    g.setColour (juce::Colour (0xff07060d)); g.strokePath (path, juce::PathStrokeType (thick + 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (tint.darker (0.6f).withAlpha (0.95f)); g.strokePath (path, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (tint.withAlpha (0.55f)); g.strokePath (path, juce::PathStrokeType (thick * 0.35f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// the round window behind the ghost: night sky, a pink moon, the city
inline void moonWindow (G& g, P c, float rad)
{
    juce::Path disc; disc.addEllipse (c.x - rad, c.y - rad, rad * 2, rad * 2);
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (disc);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a1d5c), c.x, c.y - rad, juce::Colour (0xff7b3b8f), c.x, c.y + rad, false));
        g.fillPath (disc);
        // the moon
        const P m (c.x + rad * 0.32f, c.y - rad * 0.3f);
        juce::Path moon; moon.addEllipse (m.x - rad * 0.24f, m.y - rad * 0.24f, rad * 0.48f, rad * 0.48f);
        glow (g, moon, pink.withAlpha (0.8f), (int) (rad * 0.4f));
        g.setColour (juce::Colour (0xffffb3e6)); g.fillPath (moon);
        // stars
        juce::Random rnd (3);
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        for (int i = 0; i < 18; ++i) g.fillEllipse (c.x - rad + rnd.nextFloat() * rad * 2, c.y - rad + rnd.nextFloat() * rad * 0.9f, 1.4f, 1.4f);
        // the city
        g.setColour (juce::Colour (0xff120b26));
        float x = c.x - rad;
        juce::Random cr (9);
        while (x < c.x + rad)
        {
            const float bw = rad * (0.12f + cr.nextFloat() * 0.16f), bh = rad * (0.25f + cr.nextFloat() * 0.45f);
            g.fillRect (x, c.y + rad - bh, bw, bh + 1);
            g.setColour (yellow.withAlpha (0.55f));
            for (int k = 0; k < 4; ++k) if (cr.nextFloat() < 0.6f) g.fillRect (x + bw * 0.25f + (k % 2) * bw * 0.35f, c.y + rad - bh + bh * 0.2f + (float) (k / 2) * bh * 0.25f, 1.6f, 1.6f);
            g.setColour (juce::Colour (0xff120b26));
            x += bw + 1.0f;
        }
    }
    // the frame and its bars
    g.setColour (juce::Colour (0xff2b2440)); g.drawEllipse (c.x - rad, c.y - rad, rad * 2, rad * 2, rad * 0.09f);
    g.setColour (juce::Colour (0xff4a3f6b).withAlpha (0.9f)); g.drawEllipse (c.x - rad * 1.03f, c.y - rad * 1.03f, rad * 2.06f, rad * 2.06f, 1.2f);
    g.setColour (juce::Colour (0xff2b2440));
    g.drawLine (c.x, c.y - rad, c.x, c.y + rad, rad * 0.05f);
    g.drawLine (c.x - rad, c.y + rad * 0.15f, c.x + rad, c.y + rad * 0.15f, rad * 0.05f);
}

// our ghost scientist: a sheet ghost with big dark eyes, holding up a bubbling green flask. `r` is its whole box.
inline void ghost (G& g, R r, float phase = 0.0f)
{
    const float w = r.getWidth(), h = r.getHeight(), x0 = r.getX(), y0 = r.getY() + std::sin (phase) * h * 0.02f;
    const float bw = w * 0.66f, cx = x0 + w * 0.4f, left = cx - bw * 0.5f, right = cx + bw * 0.5f, hem = y0 + h * 0.97f;
    juce::Path body;
    body.startNewSubPath (left, y0 + bw * 0.52f);
    body.cubicTo (left, y0 - bw * 0.16f, right, y0 - bw * 0.16f, right, y0 + bw * 0.52f);
    body.cubicTo (right, y0 + h * 0.7f, right + w * 0.04f, hem - h * 0.1f, right + w * 0.07f, hem - h * 0.02f);
    // the hem: four soft scallops, right to left
    const float hl = left - w * 0.07f, hr = right + w * 0.07f;
    for (int k = 0; k < 4; ++k)
    {
        const float xa = hr - (hr - hl) * (float) k / 4.0f, xb = hr - (hr - hl) * (float) (k + 1) / 4.0f;
        body.quadraticTo ((xa + xb) * 0.5f, hem + h * (k % 2 == 0 ? 0.05f : 0.02f) - h * 0.1f, xb, hem - h * (k == 3 ? 0.02f : 0.04f));
    }
    body.cubicTo (left - w * 0.04f, hem - h * 0.1f, left, y0 + h * 0.7f, left, y0 + bw * 0.52f);
    body.closeSubPath();
    glow (g, body, purple.withAlpha (0.55f), (int) (w * 0.22f));
    g.setGradientFill (juce::ColourGradient (juce::Colours::white, cx, y0, juce::Colour (0xffd9cdf5), cx, hem, false));
    g.fillPath (body);
    // the far arm, holding a beaker against the body
    juce::Path arm2;
    arm2.startNewSubPath (left + bw * 0.08f, y0 + h * 0.5f);
    arm2.quadraticTo (left + bw * 0.25f, y0 + h * 0.66f, left + bw * 0.48f, y0 + h * 0.64f);
    {
        // the beaker (pink) in that hand
        juce::Path bk; bk.addRoundedRectangle (left + bw * 0.38f, y0 + h * 0.56f, bw * 0.22f, h * 0.2f, 2.0f);
        g.setColour (juce::Colours::white.withAlpha (0.5f)); g.fillPath (bk);
        juce::Path lq; lq.addRoundedRectangle (left + bw * 0.38f, y0 + h * 0.66f, bw * 0.22f, h * 0.1f, 2.0f);
        glow (g, lq, pink.withAlpha (0.7f), 8);
        g.setColour (pink); g.fillPath (lq);
        g.setColour (juce::Colour (0xff6a5a8c)); g.strokePath (bk, juce::PathStrokeType (1.0f));
    }
    g.setColour (juce::Colour (0xffc9bce8)); g.strokePath (arm2, juce::PathStrokeType (w * 0.07f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (juce::Colours::white); g.strokePath (arm2, juce::PathStrokeType (w * 0.05f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // the near arm, held out with the green flask
    const P hand (x0 + w * 0.86f, y0 + h * 0.46f);
    juce::Path arm;
    arm.startNewSubPath (right - bw * 0.04f, y0 + h * 0.5f);
    arm.quadraticTo (right + w * 0.1f, y0 + h * 0.56f, hand.x - w * 0.02f, hand.y + h * 0.04f);
    flask (g, { hand.x + w * 0.02f, hand.y + h * 0.08f }, h * 0.36f, green, 0.6f);
    g.setColour (juce::Colour (0xffc9bce8)); g.strokePath (arm, juce::PathStrokeType (w * 0.08f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (juce::Colours::white); g.strokePath (arm, juce::PathStrokeType (w * 0.06f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // the eyes: big dark ovals with a glint
    const float ew = bw * 0.15f, eh = bw * 0.24f, ey = y0 + h * 0.2f;
    for (float ex : { cx - bw * 0.2f, cx + bw * 0.08f })
    {
        g.setColour (juce::Colour (0xff120d1f)); g.fillEllipse (ex, ey, ew, eh);
        g.setColour (juce::Colours::white.withAlpha (0.85f)); g.fillEllipse (ex + ew * 0.2f, ey + eh * 0.14f, ew * 0.3f, ew * 0.3f);
    }
    // a blush of purple in the folds
    g.setColour (purple.withAlpha (0.18f));
    g.drawLine (cx - bw * 0.05f, hem - h * 0.28f, cx - bw * 0.1f, hem - h * 0.06f, 1.5f);
    g.drawLine (cx + bw * 0.22f, hem - h * 0.3f, cx + bw * 0.26f, hem - h * 0.06f, 1.5f);
}

// an atom: the logo mark beside the title
inline void atom (G& g, R r, juce::Colour c)
{
    const auto ctr = r.getCentre();
    for (int k = 0; k < 3; ++k)
    {
        juce::Path e; e.addEllipse (-r.getWidth() * 0.5f, -r.getHeight() * 0.18f, r.getWidth(), r.getHeight() * 0.36f);
        e.applyTransform (juce::AffineTransform::rotation ((float) k * juce::MathConstants<float>::pi / 3.0f).translated (ctr));
        glow (g, e, c.withAlpha (0.6f), 6);
        g.setColour (c); g.strokePath (e, juce::PathStrokeType (r.getWidth() * 0.05f));
    }
    g.setColour (c.brighter (0.4f)); g.fillEllipse (juce::Rectangle<float> (r.getWidth() * 0.16f, r.getWidth() * 0.16f).withCentre (ctr));
}

// the title, neon: slanted, a white-pink-purple gradient, a glow
inline void neonTitle (G& g, const juce::String& t, R r, float size)
{
    juce::GlyphArrangement ga;
    ga.addLineOfText (font (size, 2, 0.0f), t, 0, 0);
    juce::Path p; ga.createPath (p);
    const auto b = p.getBounds();
    p.applyTransform (juce::AffineTransform::translation (-b.getX(), -b.getY()).followedBy (juce::AffineTransform::shear (-0.24f, 0.0f))
                          .translated (r.getX() + b.getHeight() * 0.16f, r.getCentreY() - b.getHeight() * 0.5f));
    glow (g, p, purple.withAlpha (1.0f), (int) (size * 0.45f));
    glow (g, p, pink.withAlpha (0.8f), (int) (size * 0.14f));
    const auto pb = p.getBounds();
    g.setGradientFill (juce::ColourGradient (juce::Colours::white, pb.getX(), pb.getY(), juce::Colour (0xffd59bff), pb.getRight(), pb.getBottom(), false));
    g.fillPath (p);
}

// ------------------------------------------------------------------------------------------------ the specimen tube
// (build 11) After the tube on Brandon's banner: a straight glass cylinder of dark, glowing liquid held between two
// chrome end caps (ribs, a bolted flange, a lit porthole), standing on feet, plumbed into the lab. `r` is the whole
// tube's box, caps included. It is drawn in three layers:
//   tubeBack     behind the living sound, once into the page's scene: the light it throws, the liquid, the caps
//   tubeLiquid   only the liquid (the living sound keeps its own copy and lays its light over that)
//   tubeFront    over the living sound: the glass itself (walls, rim light, reflections), bubbles and glints, the scale
struct Tube
{
    R box, glass, liquid;    // all of it; the glass cylinder between the caps; the liquid inside (where the living sound is)
    float capW = 0, s = 1;   // one cap's width; the drawing scale (1 = a tube 160 px tall)
};
inline Tube tube (R r)
{
    Tube t; t.box = r;
    t.s = juce::jlimit (0.7f, 1.6f, r.getHeight() / 160.0f);
    t.capW = juce::jlimit (38.0f, 86.0f, r.getHeight() * 0.36f);
    t.glass = r.reduced (t.capW, 3.0f * t.s);
    t.liquid = t.glass.reduced (0.0f, 3.5f * t.s);
    return t;
}
inline R tubeGlass (R r) { return tube (r).liquid; }

// the living sound's colours along its length: purple where the sound starts, through magenta, blue and cyan, to acid green
inline juce::ColourGradient lengthGradient (float x0, float x1, float alpha = 1.0f)
{
    juce::ColourGradient lg (purple.withAlpha (alpha), x0, 0, green.withAlpha (alpha), x1, 0, false);
    lg.addColour (0.28, pink.withAlpha (alpha)); lg.addColour (0.5, blue.withAlpha (alpha)); lg.addColour (0.68, cyan.withAlpha (alpha));
    return lg;
}

// a glint: a four-pointed star with a soft halo
inline void sparkle (G& g, P c, float size, juce::Colour col, float alpha = 1.0f)
{
    juce::Path p;
    const float k = size * 0.16f;
    p.startNewSubPath (c.x, c.y - size); p.quadraticTo (c.x + k, c.y - k, c.x + size, c.y); p.quadraticTo (c.x + k, c.y + k, c.x, c.y + size);
    p.quadraticTo (c.x - k, c.y + k, c.x - size, c.y); p.quadraticTo (c.x - k, c.y - k, c.x, c.y - size); p.closeSubPath();
    glow (g, p, col.withAlpha (0.75f * alpha), juce::jmax (2, juce::roundToInt (size * 1.1f)));
    g.setColour (juce::Colours::white.interpolatedWith (col, 0.25f).withAlpha (alpha)); g.fillPath (p);
}

// a bubble: a thin bright rim (brightest low on the right, where the light comes through), a faint body, a glint top left
inline void bubble (G& g, P c, float rad, juce::Colour tint, float alpha = 1.0f)
{
    const R e (c.x - rad, c.y - rad, rad * 2.0f, rad * 2.0f);
    juce::ColourGradient body (tint.withAlpha (0.02f * alpha), c.x - rad * 0.3f, c.y - rad * 0.3f, tint.withAlpha (0.20f * alpha), c.x + rad * 0.9f, c.y + rad * 0.9f, true);
    g.setGradientFill (body); g.fillEllipse (e);
    g.setColour (juce::Colours::white.interpolatedWith (tint, 0.45f).withAlpha (0.55f * alpha));
    g.drawEllipse (e, juce::jmax (0.8f, rad * 0.13f));
    juce::Path arc; arc.addCentredArc (c.x, c.y, rad * 0.68f, rad * 0.68f, 0.0f, 1.75f, 3.25f, true);
    g.setColour (juce::Colours::white.withAlpha (0.42f * alpha));
    g.strokePath (arc, juce::PathStrokeType (juce::jmax (0.7f, rad * 0.16f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (juce::Colours::white.withAlpha (0.9f * alpha));
    g.fillEllipse (c.x - rad * 0.58f, c.y - rad * 0.62f, rad * 0.42f, rad * 0.3f);
}

// dark liquid with depth: deep indigo, pools of the living sound's own colours (purple where the sound starts, green
// where it ends), slow far-off currents, dust catching the light
inline void liquid (G& g, R a, float s, int seed)
{
    juce::Graphics::ScopedSaveState ss (g);
    { juce::Path clip; clip.addRectangle (a); g.reduceClipRegion (clip); }
    juce::ColourGradient base (juce::Colour (0xff0c0824), 0, a.getY(), juce::Colour (0xff090617), 0, a.getBottom(), false);
    base.addColour (0.40, juce::Colour (0xff1a1242)); base.addColour (0.72, juce::Colour (0xff130d33));
    g.setGradientFill (base); g.fillRect (a);
    struct Pool { float x, y, rad, alpha; juce::Colour c; };
    for (auto p : { Pool { 0.08f, 0.50f, 0.36f, 0.34f, purple }, Pool { 0.30f, 0.66f, 0.24f, 0.11f, pink }, Pool { 0.52f, 0.40f, 0.30f, 0.15f, blue },
                    Pool { 0.72f, 0.64f, 0.24f, 0.10f, cyan }, Pool { 0.94f, 0.50f, 0.34f, 0.20f, green } })
    {
        const P c (a.getX() + a.getWidth() * p.x, a.getY() + a.getHeight() * p.y);
        const float rad = juce::jmax (a.getHeight(), a.getWidth() * p.rad);
        juce::ColourGradient pg (p.c.withAlpha (p.alpha), c.x, c.y, p.c.withAlpha (0.0f), c.x + rad, c.y, true);
        pg.addColour (0.45, p.c.withAlpha (p.alpha * 0.38f));
        g.setFillType (juce::FillType (pg).transformed (juce::AffineTransform::scale (1.0f, juce::jlimit (0.35f, 1.0f, a.getHeight() * 0.95f / rad), c.x, c.y)));
        g.fillRect (a);
    }
    // far-off currents: wide, soft, slow
    for (int k = 0; k < 4; ++k)
    {
        juce::Path w;
        for (int i = 0; i <= 72; ++i)
        {
            const float u = (float) i / 72.0f;
            const P pt (a.getX() + u * a.getWidth(), a.getCentreY() + a.getHeight() * ((0.16f + 0.05f * (float) k) * std::sin (u * (4.2f + 1.3f * (float) k) + (float) k * 1.9f + (float) seed)
                                                                                         + ((float) k - 1.5f) * 0.13f));
            if (i == 0) w.startNewSubPath (pt); else w.lineTo (pt);
        }
        g.setGradientFill (lengthGradient (a.getX(), a.getRight(), 0.075f - 0.012f * (float) k));
        g.strokePath (w, juce::PathStrokeType (a.getHeight() * (0.16f - 0.03f * (float) k), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    // dust in the liquid
    juce::Random rnd (seed);
    const int n = juce::jlimit (30, 220, (int) (a.getWidth() * a.getHeight() / 900.0f));
    for (int i = 0; i < n; ++i)
    {
        const float u = rnd.nextFloat(), v = rnd.nextFloat(), sz = (0.5f + 1.0f * rnd.nextFloat() * rnd.nextFloat()) * s;
        const auto c = purple.interpolatedWith (green, u).interpolatedWith (juce::Colours::white, 0.35f + 0.5f * rnd.nextFloat());
        g.setColour (c.withAlpha (0.10f + 0.42f * rnd.nextFloat() * rnd.nextFloat()));
        g.fillEllipse (a.getX() + u * a.getWidth(), a.getY() + v * a.getHeight(), sz, sz);
    }
    // the liquid darkens toward the glass above and below (it is a cylinder)
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.42f), 0, a.getY(), juce::Colours::transparentBlack, 0, a.getY() + a.getHeight() * 0.22f, false));
    g.fillRect (a.withHeight (a.getHeight() * 0.22f));
    g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, 0, a.getBottom() - a.getHeight() * 0.2f, juce::Colours::black.withAlpha (0.38f), 0, a.getBottom(), false));
    g.fillRect (a.withTop (a.getBottom() - a.getHeight() * 0.2f));
}
inline void tubeLiquid (G& g, R r) { const auto t = tube (r); liquid (g, t.glass, t.s, 31); }

// polished metal lit by the lab: a bright band high up, the dark horizon, the liquid's colour bounced back from below
inline void chrome (G& g, R b, float corner, juce::Colour bounce, float gain = 1.0f)
{
    auto c = [gain] (juce::uint32 argb) { return juce::Colour (argb).withMultipliedBrightness (gain); };
    juce::ColourGradient m (c (0xff1c1832), b.getX(), b.getY(), c (0xff0f0c1e), b.getX(), b.getBottom(), false);
    m.addColour (0.05, c (0xff584f82)); m.addColour (0.14, c (0xffc6bfea)); m.addColour (0.20, c (0xfff6f2ff)); m.addColour (0.28, c (0xffa79dd4));
    m.addColour (0.43, c (0xff5f548b)); m.addColour (0.51, c (0xff211b3a)); m.addColour (0.60, c (0xff372d62));
    m.addColour (0.80, c (0xff7462b0).interpolatedWith (bounce, 0.42f)); m.addColour (0.93, c (0xff3a2f63).interpolatedWith (bounce, 0.3f));
    g.setGradientFill (m); g.fillRoundedRectangle (b, corner);
    if (b.getHeight() > 30.0f && b.getWidth() > 5.0f)
    {
        // the hard edges of a polished surface: a fine bright line where the light sits, a dark one at the horizon
        g.setColour (juce::Colours::white.withAlpha (0.7f * juce::jmin (1.0f, gain))); g.fillRect (b.getX() + 1.5f, b.getY() + b.getHeight() * 0.195f, b.getWidth() - 3.0f, 1.0f);
        g.setColour (juce::Colour (0xff0b0918).withAlpha (0.75f)); g.fillRect (b.getX() + 1.5f, b.getY() + b.getHeight() * 0.515f, b.getWidth() - 3.0f, 1.2f);
        g.setColour (juce::Colours::white.withAlpha (0.2f)); g.fillRect (b.getX() + 1.2f, b.getY() + corner, 0.9f, b.getHeight() - 2.0f * corner);
    }
    g.setColour (juce::Colour (0xff06050c)); g.drawRoundedRectangle (b, corner, 1.1f);
}

// one end cap, drawn as the left one (the right one is this, mirrored): from the lab's pipe to the glass
inline void tubeCap (G& g, const Tube& t, juce::Colour lamp)
{
    const float s = t.s, x0 = t.box.getX(), w = t.capW, cy = t.box.getCentreY(), H = t.box.getHeight();
    auto piece = [&] (float f0, float f1, float h) { return R (x0 + w * f0, cy - h * 0.5f, w * (f1 - f0), h); };
    auto shade = [&g] (R b, float corner, float a0, float a1)   // rounder: darker toward the far end
    {
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (a0), b.getX(), 0, juce::Colours::black.withAlpha (a1), b.getRight(), 0, false));
        g.fillRoundedRectangle (b, corner);
    };
    // the pipe into the lab and its coupling
    {
        const R pipe (x0 - 40.0f, cy - H * 0.105f, 40.0f + w * 0.1f, H * 0.21f);
        juce::ColourGradient pg (juce::Colour (0xff3b2f66), 0, pipe.getY(), juce::Colour (0xff120d24), 0, pipe.getBottom(), false);
        pg.addColour (0.22, juce::Colour (0xffb9a6ee)); pg.addColour (0.5, juce::Colour (0xff5a478f)); pg.addColour (0.85, lamp.darker (0.75f));
        g.setGradientFill (pg); g.fillRect (pipe);
        g.setColour (juce::Colour (0xff06050c)); g.drawRect (pipe, 1.0f);
        chrome (g, R (x0 - 7.0f * s, cy - H * 0.16f, 9.0f * s, H * 0.32f), 2.0f * s, lamp);
    }
    // the domed end, the drum, the collar the glass sits in
    const auto dome = piece (0.0f, 0.34f, H * 0.68f);
    {
        juce::Path d; d.addRoundedRectangle (dome.getX(), dome.getY(), dome.getWidth(), dome.getHeight(), H * 0.2f, H * 0.2f, true, false, true, false);
        juce::Graphics::ScopedSaveState ss (g); g.reduceClipRegion (d);
        chrome (g, dome.expanded (2.0f, 0.0f), 0.0f, lamp, 0.82f);
        shade (dome, 0.0f, 0.5f, 0.0f);
    }
    const auto drum = piece (0.2f, 0.72f, H * 0.9f);
    chrome (g, drum, 3.0f * s, lamp);
    shade (drum, 3.0f * s, 0.26f, 0.0f);
    const auto collar = piece (0.84f, 1.0f, t.glass.getHeight() + 7.0f * s);
    chrome (g, collar.withRight (t.glass.getX() + 0.5f), 2.0f * s, lamp, 0.9f);
    // two raised ribs round the drum
    for (float f : { 0.275f, 0.615f })
    {
        const auto rib = R (x0 + w * f, drum.getY() - 2.5f * s, juce::jmax (3.0f, w * 0.062f), drum.getHeight() + 5.0f * s);
        chrome (g, rib, 1.5f * s, lamp, 1.12f);
        g.setColour (juce::Colours::black.withAlpha (0.45f)); g.fillRect (rib.getRight(), drum.getY() + 2.0f, 1.6f * s, drum.getHeight() - 4.0f);
    }
    // lit slots, high and low
    for (float fy : { -0.315f, 0.315f })
    {
        const auto slot = R (w * 0.16f, juce::jmax (2.5f, H * 0.032f)).withCentre ({ x0 + w * 0.465f, cy + H * fy });
        juce::Path sp; sp.addRoundedRectangle (slot, slot.getHeight() * 0.45f);
        g.setColour (juce::Colour (0xff06050c)); g.fillRoundedRectangle (slot.expanded (1.2f), slot.getHeight() * 0.6f);
        glow (g, sp, lamp.withAlpha (0.9f), juce::jmax (3, juce::roundToInt (5.0f * s)));
        g.setColour (juce::Colours::white.interpolatedWith (lamp, 0.35f)); g.fillPath (sp);
    }
    // the porthole: a bezel, dark glass, the lamp behind it
    {
        const P c (x0 + w * 0.465f, cy);
        const float R0 = juce::jmin (w * 0.21f, H * 0.1f);
        juce::ColourGradient bz (juce::Colour (0xfff2edff), c.x - R0, c.y - R0, juce::Colour (0xff1d1836), c.x + R0, c.y + R0, false);
        bz.addColour (0.45, juce::Colour (0xff7d72ab));
        g.setGradientFill (bz); g.fillEllipse (R (R0 * 2, R0 * 2).withCentre (c));
        g.setColour (juce::Colour (0xff06050c)); g.drawEllipse (R (R0 * 2, R0 * 2).withCentre (c), 1.1f);
        g.fillEllipse (R (R0 * 1.52f, R0 * 1.52f).withCentre (c));
        juce::Path lens; lens.addEllipse (R (R0 * 1.24f, R0 * 1.24f).withCentre (c));
        glow (g, lens, lamp.withAlpha (0.95f), juce::jmax (4, juce::roundToInt (9.0f * s)));
        juce::ColourGradient lg (juce::Colours::white.interpolatedWith (lamp, 0.3f), c.x - R0 * 0.2f, c.y - R0 * 0.25f, lamp.darker (0.55f), c.x + R0 * 0.7f, c.y + R0 * 0.7f, true);
        lg.addColour (0.45, lamp);
        g.setGradientFill (lg); g.fillPath (lens);
        g.setColour (juce::Colours::white.withAlpha (0.85f)); g.fillEllipse (c.x - R0 * 0.42f, c.y - R0 * 0.46f, R0 * 0.36f, R0 * 0.24f);
    }
    // the bolted flange (the widest ring), then the feet it stands on and the lug on top
    const auto flange = piece (0.68f, 0.88f, H + 10.0f * s);
    {
        const auto foot = R (flange.getCentreX() - 10.0f * s, flange.getBottom() - 3.0f * s, 20.0f * s, 9.0f * s);
        chrome (g, foot, 2.0f * s, lamp, 0.7f);
        chrome (g, R (foot.getX() - 7.0f * s, foot.getBottom() - 1.5f * s, foot.getWidth() + 14.0f * s, 4.5f * s), 1.5f * s, lamp, 0.9f);
        chrome (g, R (flange.getCentreX() - 6.5f * s, flange.getY() - 5.0f * s, 13.0f * s, 8.0f * s), 2.0f * s, lamp, 0.85f);
    }
    chrome (g, flange, 2.5f * s, lamp, 1.06f);
    const int bolts = juce::jlimit (4, 7, (int) (flange.getHeight() / (26.0f * s)));
    for (int k = 0; k < bolts; ++k)
    {
        const P c (flange.getCentreX(), flange.getY() + flange.getHeight() * ((float) k + 0.5f) / (float) bolts);
        const float br = juce::jmin (flange.getWidth() * 0.3f, 3.2f * s);
        g.setColour (juce::Colour (0xff06050c)); g.fillEllipse (R (br * 2.5f, br * 2.5f).withCentre (c));
        juce::ColourGradient bg (juce::Colour (0xffe9e3ff), c.x - br, c.y - br, juce::Colour (0xff2a2348), c.x + br, c.y + br, false);
        g.setGradientFill (bg); g.fillEllipse (R (br * 2, br * 2).withCentre (c));
    }
    // the liquid's light on the metal next to the glass
    g.setGradientFill (juce::ColourGradient (lamp.withAlpha (0.0f), x0 + w * 0.6f, 0, lamp.withAlpha (0.34f), t.glass.getX(), 0, false));
    g.fillRect (R (x0 + w * 0.6f, t.glass.getY(), w * 0.4f, t.glass.getHeight()));
}

inline void tubeBack (G& g, R r)
{
    const auto t = tube (r);
    const auto gl = t.glass;
    // the light the liquid throws around the tube: purple, blue, green along its length
    {
        const juce::Colour cs[3] = { purple, blue, green };
        const float w3 = gl.getWidth() / 3.0f;
        for (int k = 0; k < 3; ++k)
        {
            juce::Path p; p.addRoundedRectangle (gl.getX() + w3 * (float) k + w3 * 0.08f, gl.getY() + 8.0f, w3 * 0.84f, gl.getHeight() - 16.0f, 14.0f);
            glow (g, p, cs[k].withAlpha (k == 1 ? 0.3f : 0.45f), juce::roundToInt (30.0f * t.s));
        }
    }
    tubeLiquid (g, r);
    tubeCap (g, t, purple);
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.addTransform (juce::AffineTransform::scale (-1.0f, 1.0f, r.getCentreX(), 0.0f));
        tubeCap (g, t, green);
    }
}

inline void tubeFront (G& g, R r)
{
    const auto t = tube (r);
    const auto gl = t.glass, lq = t.liquid;
    const float s = t.s, W = gl.getWidth(), H = gl.getHeight();
    // where the glass goes into the collars: shadow, and the lamp colour of that end soaking into the liquid
    for (int side = 0; side < 2; ++side)
    {
        const float x = side == 0 ? gl.getX() : gl.getRight(), d = side == 0 ? 1.0f : -1.0f;
        const auto lamp = side == 0 ? purple : green;
        const auto endR = side == 0 ? gl.withWidth (H * 0.75f) : gl.withLeft (gl.getRight() - H * 0.75f);
        g.setGradientFill (juce::ColourGradient (lamp.withAlpha (0.26f), x, 0, lamp.withAlpha (0.0f), x + d * H * 0.75f, 0, false));
        g.fillRect (endR);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff05040b).withAlpha (0.78f), x, 0, juce::Colour (0xff05040b).withAlpha (0.0f), x + d * H * 0.2f, 0, false));
        g.fillRect (side == 0 ? gl.withWidth (H * 0.2f) : gl.withLeft (gl.getRight() - H * 0.2f));
        // the end of the cylinder seen through the glass: a curved gleam
        juce::Path arc; arc.addCentredArc (x + d * H * 0.2f, gl.getCentreY(), H * 0.085f, lq.getHeight() * 0.47f, 0.0f, side == 0 ? 0.25f : -0.25f, side == 0 ? 2.9f : -2.9f, true);
        g.setColour (juce::Colours::white.interpolatedWith (lamp, 0.4f).withAlpha (0.20f));
        g.strokePath (arc, juce::PathStrokeType (1.4f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        // the collar's lip over the glass
        chrome (g, R (side == 0 ? x - 1.5f : x - 2.5f * s, gl.getY() - 2.0f * s, 4.0f * s, H + 4.0f * s), 1.5f * s, lamp, 1.1f);
    }
    // the glass walls, top and bottom: a little milky, lit along their outside edge in the living sound's colours
    for (auto wall : { gl.withBottom (lq.getY()), gl.withTop (lq.getBottom()) })
    {
        g.setColour (juce::Colours::white.withAlpha (0.07f)); g.fillRect (wall);
    }
    g.setColour (juce::Colours::white.withAlpha (0.16f));
    g.fillRect (gl.getX(), lq.getY() - 0.5f, W, 1.0f); g.fillRect (gl.getX(), lq.getBottom() - 0.5f, W, 1.0f);
    {
        juce::Path rim; rim.addRectangle (gl.getX(), gl.getY() - 0.8f, W, 1.6f); rim.addRectangle (gl.getX(), gl.getBottom() - 0.8f, W, 1.6f);
        g.setGradientFill (lengthGradient (gl.getX(), gl.getRight(), 0.5f));
        g.fillRect (gl.getX(), gl.getY() - 3.5f * s, W, 3.5f * s); g.fillRect (gl.getX(), gl.getBottom(), W, 3.5f * s);
        g.setGradientFill (lengthGradient (gl.getX(), gl.getRight()));
        g.fillPath (rim);
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.fillRect (gl.getX() + H * 0.2f, gl.getY() - 0.4f, W - H * 0.4f, 0.8f);
    }
    // reflections of the lab in the glass: a soft band of light high up (in pieces, fading out at their ends) under a
    // fine hard line; a fainter one low down, with the bench's light coming up through the liquid
    auto streak = [&g] (float x0, float x1, float y, float h, float alpha)
    {
        // thin rows of light, strongest at the top; each fades to nothing at both ends
        const int rows = juce::jmax (4, juce::roundToInt (h * 2.0f));
        for (int k = 0; k < rows; ++k)
        {
            const float a = alpha * sq ((float) (rows - k) / (float) rows);
            juce::ColourGradient lg (juce::Colours::white.withAlpha (0.0f), x0, 0, juce::Colours::white.withAlpha (0.0f), x1, 0, false);
            lg.addColour (0.08, juce::Colours::white.withAlpha (a)); lg.addColour (0.9, juce::Colours::white.withAlpha (a * 0.8f));
            g.setGradientFill (lg); g.fillRect (x0, y + (float) k * h / (float) rows, x1 - x0, h / (float) rows);
        }
    };
    {
        const float y = lq.getY() + H * 0.03f, bh = H * 0.13f;
        for (auto seg : { std::pair<float, float> { 0.05f, 0.56f }, std::pair<float, float> { 0.6f, 0.79f }, std::pair<float, float> { 0.82f, 0.87f } })
        {
            const float x0 = gl.getX() + W * seg.first, x1 = gl.getX() + W * seg.second;
            streak (x0, x1, y, bh, 0.3f);
            juce::ColourGradient hard (juce::Colours::white.withAlpha (0.0f), x0, 0, juce::Colours::white.withAlpha (0.0f), x1, 0, false);
            hard.addColour (0.12, juce::Colours::white.withAlpha (0.62f)); hard.addColour (0.85, juce::Colours::white.withAlpha (0.4f));
            g.setGradientFill (hard); g.fillRect (x0, y, x1 - x0, juce::jmax (1.0f, 1.1f * s));
        }
        g.setGradientFill (juce::ColourGradient (cyan.withAlpha (0.0f), 0, lq.getBottom() - H * 0.3f, cyan.withAlpha (0.10f), 0, lq.getBottom(), false));
        g.fillRect (lq.withTop (lq.getBottom() - H * 0.3f));
        for (auto seg : { std::pair<float, float> { 0.1f, 0.6f }, std::pair<float, float> { 0.64f, 0.8f } })
        {
            const float x0 = gl.getX() + W * seg.first, x1 = gl.getX() + W * seg.second;
            juce::ColourGradient low (juce::Colours::white.withAlpha (0.0f), x0, 0, juce::Colours::white.withAlpha (0.0f), x1, 0, false);
            low.addColour (0.15, juce::Colours::white.withAlpha (0.2f)); low.addColour (0.8, juce::Colours::white.withAlpha (0.12f));
            g.setGradientFill (low); g.fillRect (x0, lq.getBottom() - H * 0.085f, x1 - x0, juce::jmax (1.0f, 1.2f * s));
        }
    }
    // the scale etched along the bottom of the glass (the sound runs left to right)
    {
        const float x0 = gl.getX() + W * 0.035f, x1 = gl.getRight() - W * 0.035f;
        for (int k = 0; k <= 50; ++k)
        {
            const bool major = k % 5 == 0;
            g.setColour (juce::Colours::white.withAlpha (major ? 0.34f : 0.17f));
            g.fillRect (x0 + (x1 - x0) * (float) k / 50.0f - 0.5f, lq.getBottom() - (major ? 7.0f : 3.5f) * s, 1.0f, (major ? 7.0f : 3.5f) * s);
        }
    }
    // bubbles clinging to the glass and glints in it: at the ends and along the edges, clear of the middle
    juce::Random rnd (21);
    const int nb = juce::jlimit (9, 18, (int) (W / 60.0f));
    for (int i = 0; i < nb; ++i)
    {
        const bool atEnd = i % 3 != 2;
        float u = atEnd ? 0.03f + 0.17f * rnd.nextFloat() : 0.2f + 0.6f * rnd.nextFloat();
        if (atEnd && (i & 1)) u = 1.0f - u;
        const float v = atEnd ? 0.14f + 0.72f * rnd.nextFloat() : ((i & 1) ? 0.12f + 0.08f * rnd.nextFloat() : 0.8f + 0.08f * rnd.nextFloat());
        const float rad = (atEnd ? 2.2f + 5.5f * rnd.nextFloat() * rnd.nextFloat() : 1.6f + 2.2f * rnd.nextFloat()) * s;
        bubble (g, { gl.getX() + u * W, lq.getY() + v * lq.getHeight() }, rad, purple.interpolatedWith (green, u), 0.8f);
    }
    for (int i = 0; i < 12; ++i)
    {
        float u = 0.04f + 0.24f * rnd.nextFloat(); if (i & 1) u = 1.0f - u;
        const float v = 0.12f + 0.76f * rnd.nextFloat();
        const P c (gl.getX() + u * W, lq.getY() + v * lq.getHeight());
        const auto col = purple.interpolatedWith (green, u).interpolatedWith (i % 3 == 0 ? pink : cyan, 0.3f);
        if (i < 6) sparkle (g, c, (2.6f + 3.2f * rnd.nextFloat()) * s, col, 0.9f);
        else
        {
            // pixel dust: tiny lit squares
            const float q = (1.6f + 1.6f * rnd.nextFloat()) * s;
            juce::Path sq; sq.addRectangle (R (q, q).withCentre (c));
            glow (g, sq, col.withAlpha (0.7f), juce::jmax (2, juce::roundToInt (3.0f * s)));
            g.setColour (col.brighter (0.5f).withAlpha (0.85f)); g.fillPath (sq);
        }
    }
}

// the same liquid behind a rounded window (the phone-width page has no room for the long tube)
inline void tankLiquid (G& g, R r)
{
    juce::Graphics::ScopedSaveState ss (g);
    juce::Path p; p.addRoundedRectangle (r.reduced (0.5f), 14.0f); g.reduceClipRegion (p);
    liquid (g, r, 1.0f, 57);
}
inline void tankFront (G& g, R r)
{
    const auto in = r.reduced (1.0f);
    juce::Path rim; rim.addRoundedRectangle (in, 14.0f);
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (rim);
        for (auto seg : { std::pair<float, float> { 0.08f, 0.6f }, std::pair<float, float> { 0.64f, 0.8f } })
        {
            const float x0 = in.getX() + in.getWidth() * seg.first, x1 = in.getX() + in.getWidth() * seg.second;
            for (int k = 0; k < 12; ++k)
            {
                juce::ColourGradient lg (juce::Colours::white.withAlpha (0.0f), x0, 0, juce::Colours::white.withAlpha (0.0f), x1, 0, false);
                const float a = (k == 0 ? 0.6f : 0.15f * sq ((float) (12 - k) / 12.0f));
                lg.addColour (0.1, juce::Colours::white.withAlpha (a)); lg.addColour (0.88, juce::Colours::white.withAlpha (a * 0.75f));
                g.setGradientFill (lg); g.fillRect (x0, in.getY() + 8.0f + (float) k * 1.2f, x1 - x0, 1.5f);
            }
        }
        g.setGradientFill (juce::ColourGradient (cyan.withAlpha (0.0f), 0, in.getBottom() - in.getHeight() * 0.25f, cyan.withAlpha (0.09f), 0, in.getBottom(), false));
        g.fillRect (in.withTop (in.getBottom() - in.getHeight() * 0.25f));
        for (int k = 0; k <= 40; ++k)
        {
            const bool major = k % 5 == 0;
            g.setColour (juce::Colours::white.withAlpha (major ? 0.32f : 0.16f));
            g.fillRect (in.getX() + in.getWidth() * (0.06f + 0.88f * (float) k / 40.0f) - 0.5f, in.getBottom() - (major ? 8.0f : 4.5f), 1.0f, major ? 7.0f : 3.5f);
        }
        juce::Random rnd (9);
        for (int i = 0; i < 9; ++i)
        {
            const float u = (i & 1) ? 0.04f + 0.14f * rnd.nextFloat() : 0.82f + 0.14f * rnd.nextFloat(), v = 0.12f + 0.74f * rnd.nextFloat();
            const P c (in.getX() + u * in.getWidth(), in.getY() + v * in.getHeight());
            if (i < 6) bubble (g, c, 2.0f + 5.0f * rnd.nextFloat() * rnd.nextFloat(), purple.interpolatedWith (green, u), 0.8f);
            else sparkle (g, c, 2.6f + 2.4f * rnd.nextFloat(), purple.interpolatedWith (green, u), 0.9f);
        }
    }
    g.setGradientFill (lengthGradient (in.getX(), in.getRight()));
    g.strokePath (rim, juce::PathStrokeType (1.5f));
}

// a tinted glass panel (cards, the waveform strip, the experiment table)
inline void glassPanel (G& g, R r, juce::Colour tint, float corner, float strength = 1.0f, bool lit = false, bool grid = false)
{
    g.setGradientFill (juce::ColourGradient (tint.withAlpha (0.13f * strength), r.getX(), r.getY(), tint.withAlpha (0.03f * strength), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, corner);
    g.setColour (juce::Colour (0xff0b0918).withAlpha (0.62f)); g.fillRoundedRectangle (r.reduced (1.0f), corner);
    if (grid)
    {
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip; clip.addRoundedRectangle (r.reduced (2.0f), corner); g.reduceClipRegion (clip);
        g.setColour (tint.withAlpha (0.07f));
        for (float x = r.getX() + 12; x < r.getRight(); x += 14.0f) g.fillRect (x, r.getY(), 1.0f, r.getHeight());
        for (float y = r.getY() + 12; y < r.getBottom(); y += 14.0f) g.fillRect (r.getX(), y, r.getWidth(), 1.0f);
    }
    g.setGradientFill (juce::ColourGradient (tint.withAlpha (0.12f * strength), r.getX(), r.getY(), tint.withAlpha (0.02f * strength), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r.reduced (1.0f), corner);
    if (lit) { juce::Path p; p.addRoundedRectangle (r, corner); glow (g, p, tint.withAlpha (0.35f), 14); }
    g.setColour (tint.withAlpha (lit ? 0.95f : 0.5f * strength)); g.drawRoundedRectangle (r.reduced (0.5f), corner, lit ? 1.6f : 1.1f);
}


// a shelf of glowing glassware (under the title): a metal plank, flasks, a test-tube rack, beakers, a small burner glow
inline void shelf (G& g, R r)
{
    const float y = r.getBottom() - 3;
    // the plank and its brackets
    g.setColour (juce::Colour (0xff07060d)); g.fillRoundedRectangle (r.getX(), y - 1, r.getWidth(), 6.0f, 2.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6a5f8c), 0, y, juce::Colour (0xff2a2442), 0, y + 5, false));
    g.fillRoundedRectangle (r.getX(), y, r.getWidth(), 4.0f, 2.0f);
    for (float f : { 0.08f, 0.92f }) { g.setColour (juce::Colour (0xff3a3358)); g.fillRect (r.getX() + r.getWidth() * f - 2, y + 3, 4.0f, 8.0f); }
    // what stands on it, evenly across
    const juce::Colour cs[] = { purple, pink, cyan, green, yellow, purple, mint, pink };
    const float h = r.getHeight() - 4;
    float x = r.getX() + 14;
    int k = 0;
    while (x < r.getRight() - 20)
    {
        const auto c = cs[k % 8];
        switch (k % 5)
        {
            case 0: flask (g, { x + h * 0.4f, y }, h, c, 0.55f, false); x += h * 0.95f; break;
            case 1: for (int t = 0; t < 3; ++t) testTube (g, { x + (float) t * h * 0.28f, y - h * 0.9f, h * 0.18f, h * 0.9f }, cs[(k + t) % 8], 0.5f + 0.15f * (float) t);
                    g.setColour (juce::Colour (0xff3a3358)); g.fillRect (x - 3, y - h * 0.45f, h * 0.84f + 4, 3.0f); x += h * 1.0f; break;
            case 2: flask (g, { x + h * 0.42f, y }, h * 0.95f, c, 0.6f, true); x += h * 0.95f; break;
            case 3:
            {
                auto b = juce::Rectangle<float> (x, y - h * 0.62f, h * 0.55f, h * 0.62f);
                juce::Path bk; bk.addRoundedRectangle (b, 2.0f);
                { juce::Graphics::ScopedSaveState ss (g); g.reduceClipRegion (b.withTrimmedTop (b.getHeight() * 0.45f).toNearestInt()); glow (g, bk, c.withAlpha (0.6f), 8); g.setColour (c); g.fillPath (bk); }
                g.setColour (juce::Colours::white.withAlpha (0.4f)); g.strokePath (bk, juce::PathStrokeType (1.0f));
                x += h * 0.8f; break;
            }
            default: flask (g, { x + h * 0.3f, y }, h * 0.7f, c, 0.5f, false); x += h * 0.75f; break;
        }
        x += 10.0f; ++k;
    }
}

// more glassware filling a strip of the header (when the window is wider than the art)
inline void glasswareCluster (G& g, R r)
{
    shelf (g, r.withTop (r.getBottom() - 70).withHeight (64).reduced (6, 0));
}

// the whole room behind everything (drawn once per size into an image): the night gradient, brick, pipes, shelves, glowing
// glassware at the edges, the floor glow. `keepClear` is where the panels go (kept dark and quiet).
inline void labRoom (G& g, R r, R keepClear, P windowCentre, float windowRad)
{
    g.setGradientFill (juce::ColourGradient (night1, r.getCentreX(), r.getY(), night0, r.getCentreX(), r.getBottom(), false));
    g.fillRect (r);
    // a purple haze top-left, a green haze bottom-right
    g.setGradientFill (juce::ColourGradient (purple.withAlpha (0.16f), r.getX(), r.getY(), purple.withAlpha (0.0f), r.getX() + r.getWidth() * 0.45f, r.getY() + r.getHeight() * 0.5f, true));
    g.fillRect (r);
    g.setGradientFill (juce::ColourGradient (green.withAlpha (0.08f), r.getRight(), r.getBottom(), green.withAlpha (0.0f), r.getRight() - r.getWidth() * 0.4f, r.getBottom() - r.getHeight() * 0.5f, true));
    g.fillRect (r);
    // brick on the back wall (faint)
    g.setColour (juce::Colours::white.withAlpha (0.018f));
    const float bh = 18.0f, bwid = 46.0f;
    for (int row = 0; row * bh < r.getHeight(); ++row)
    {
        const float y = r.getY() + (float) row * bh;
        g.fillRect (r.getX(), y, r.getWidth(), 1.0f);
        for (float x = r.getX() + ((row % 2) ? bwid * 0.5f : 0.0f); x < r.getRight(); x += bwid) g.fillRect (x, y, 1.0f, bh);
    }
    // the window behind the ghost
    if (windowRad > 0) moonWindow (g, windowCentre, windowRad);
    // glassware at the bottom corners (outside the panels)
    const float floorY = r.getBottom();
    // the floor: a glow that the footer continues
    g.setGradientFill (juce::ColourGradient (purple.withAlpha (0.10f), r.getCentreX(), floorY, purple.withAlpha (0.0f), r.getCentreX(), floorY - 120, false));
    g.fillRect (r.withTop (floorY - 120));
    // keep the panel area calm
    g.setColour (night0.withAlpha (0.35f));
    g.fillRoundedRectangle (keepClear, 14.0f);
}
}

// ---- icons (drawn, no images): each fills `r`
namespace icon
{
using G = juce::Graphics;
using R = juce::Rectangle<float>;
using P = juce::Point<float>;

inline void arrowHead (G& g, juce::Point<float> tip, float angle, float size, float thick)
{
    juce::Path p;
    p.startNewSubPath (tip.translated (-size * std::cos (angle - 0.6f), -size * std::sin (angle - 0.6f)));
    p.lineTo (tip);
    p.lineTo (tip.translated (-size * std::cos (angle + 0.6f), -size * std::sin (angle + 0.6f)));
    g.strokePath (p, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

inline void pull (G& g, R r, juce::Colour c)
{
    g.setColour (c);
    const float y = r.getCentreY(), t = r.getWidth() * 0.06f;
    g.drawLine (r.getX() + 2, y, r.getRight() - 2, y, t);
    arrowHead (g, { r.getRight() - 1, y }, 0.0f, r.getWidth() * 0.2f, t);
    arrowHead (g, { r.getX() + 1, y }, juce::MathConstants<float>::pi, r.getWidth() * 0.2f, t);
}

inline void spiral (G& g, R r, juce::Colour c)
{
    g.setColour (c);
    const auto ctr = r.getCentre(); const float R0 = r.getWidth() * 0.48f, t = r.getWidth() * 0.06f;
    for (int arm = 0; arm < 3; ++arm)
    {
        juce::Path p;
        const float a0 = arm * juce::MathConstants<float>::twoPi / 3.0f;
        for (int i = 0; i <= 40; ++i)
        {
            const float u = (float) i / 40.0f, a = a0 + u * 3.2f, rad = R0 * (0.25f + 0.75f * u);
            auto pt = ctr + juce::Point<float> (std::cos (a) * rad, std::sin (a) * rad);
            if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
        }
        g.strokePath (p, juce::PathStrokeType (t * (arm == 0 ? 1.2f : 0.8f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

inline void snowflake (G& g, R r, juce::Colour c)
{
    g.setColour (c);
    const auto ctr = r.getCentre(); const float L = r.getWidth() * 0.48f, t = r.getWidth() * 0.065f;
    for (int k = 0; k < 6; ++k)
    {
        const float a = k * juce::MathConstants<float>::pi / 3.0f - juce::MathConstants<float>::halfPi;
        auto dir = juce::Point<float> (std::cos (a), std::sin (a));
        g.drawLine ({ ctr, ctr + dir * L }, t);
        for (float f : { 0.45f, 0.72f })
        {
            auto base = ctr + dir * (L * f);
            const float b = L * (f > 0.5f ? 0.22f : 0.3f);
            for (float s : { -1.0f, 1.0f })
            {
                const float aa = a + s * 0.75f;
                g.drawLine ({ base, base + juce::Point<float> (std::cos (aa), std::sin (aa)) * b }, t * 0.85f);
            }
        }
    }
}

inline void smear (G& g, R r, juce::Colour c)
{
    for (int i = 0; i < 9; ++i)
    {
        const float k = (float) i / 8.0f;
        auto e = r.withSizeKeepingCentre (r.getWidth() * (1.05f - 0.55f * k), r.getHeight() * (0.42f - 0.3f * k)).translated (r.getWidth() * 0.05f * std::sin (k * 6.0f), 0);
        g.setColour (c.withAlpha (0.08f + 0.06f * k));
        g.fillEllipse (e);
    }
    g.setColour (c.withAlpha (0.7f));
    g.fillEllipse (r.withSizeKeepingCentre (r.getWidth() * 0.5f, r.getHeight() * 0.09f));
}

inline void burst (G& g, R r, juce::Colour c)
{
    g.setColour (c);
    const auto ctr = r.getCentre(); const float L = r.getWidth() * 0.5f, t = r.getWidth() * 0.05f;
    juce::Random rnd (7);
    for (int k = 0; k < 14; ++k)
    {
        const float a = k * juce::MathConstants<float>::twoPi / 14.0f + rnd.nextFloat() * 0.2f;
        const float r0 = L * (0.25f + 0.15f * rnd.nextFloat()), r1 = L * (0.6f + 0.35f * rnd.nextFloat());
        auto d = juce::Point<float> (std::cos (a), std::sin (a));
        g.drawLine ({ ctr + d * r0, ctr + d * r1 }, t);
        if (k % 2 == 0) g.fillEllipse (juce::Rectangle<float> (t * 1.8f, t * 1.8f).withCentre (ctr + d * (r1 + t * 2.2f)));
    }
    g.fillEllipse (juce::Rectangle<float> (t * 2.0f, t * 2.0f).withCentre (ctr));
}

inline void nodes (G& g, R r, juce::Colour c)
{
    g.setColour (c);
    const auto ctr = r.getCentre(); const float L = r.getWidth() * 0.42f, t = r.getWidth() * 0.05f;
    for (int k = 0; k < 8; ++k)
    {
        const float a = k * juce::MathConstants<float>::twoPi / 8.0f;
        auto d = juce::Point<float> (std::cos (a), std::sin (a));
        g.drawLine ({ ctr + d * (L * 0.25f), ctr + d * L }, t);
        g.fillEllipse (juce::Rectangle<float> (t * 3.2f, t * 3.2f).withCentre (ctr + d * L));
    }
    g.fillEllipse (juce::Rectangle<float> (t * 4.2f, t * 4.2f).withCentre (ctr));
}

inline void grains (G& g, R r, juce::Colour c)
{
    juce::Random rnd (11);
    for (int i = 0; i < 70; ++i)
    {
        const float a = rnd.nextFloat() * juce::MathConstants<float>::twoPi, d = std::sqrt (rnd.nextFloat());
        auto p = r.getCentre() + juce::Point<float> (std::cos (a) * d * r.getWidth() * 0.5f, std::sin (a) * d * r.getHeight() * 0.32f);
        const float s = 0.8f + rnd.nextFloat() * 1.8f;
        g.setColour (c.withAlpha (0.35f + 0.65f * (1.0f - d)));
        g.fillEllipse (juce::Rectangle<float> (s, s).withCentre (p));
    }
}

inline void wave (G& g, R r, juce::Colour c)
{
    g.setColour (c);
    for (int k = 0; k < 2; ++k)
    {
        juce::Path p;
        for (int i = 0; i <= 40; ++i)
        {
            const float u = (float) i / 40.0f;
            auto pt = juce::Point<float> (r.getX() + u * r.getWidth(), r.getCentreY() + (k == 0 ? 1.0f : -0.6f) * std::sin (u * 6.6f + k) * r.getHeight() * 0.22f + (k == 0 ? 0 : r.getHeight() * 0.12f));
            if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
        }
        g.strokePath (p, juce::PathStrokeType (r.getWidth() * (k == 0 ? 0.06f : 0.035f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

inline void warp (G& g, R r, juce::Colour c)
{
    g.setColour (c);
    juce::Path p;
    const float t = r.getWidth() * 0.065f;
    p.startNewSubPath (r.getX() + r.getWidth() * 0.85f, r.getY() + r.getHeight() * 0.18f);
    p.cubicTo (r.getX() + r.getWidth() * 0.35f, r.getY() + r.getHeight() * 0.05f, r.getX() - r.getWidth() * 0.05f, r.getY() + r.getHeight() * 0.65f,
               r.getX() + r.getWidth() * 0.4f, r.getY() + r.getHeight() * 0.78f);
    p.quadraticTo (r.getX() + r.getWidth() * 0.65f, r.getY() + r.getHeight() * 0.84f, r.getX() + r.getWidth() * 0.9f, r.getY() + r.getHeight() * 0.55f);
    g.strokePath (p, juce::PathStrokeType (t, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    arrowHead (g, { r.getX() + r.getWidth() * 0.9f, r.getY() + r.getHeight() * 0.55f }, -0.85f, r.getWidth() * 0.22f, t);
}

inline void rings (G& g, R r, juce::Colour c)
{
    g.setColour (c);
    const float rad = r.getWidth() * 0.26f, t = r.getWidth() * 0.055f;
    for (int k = 0; k < 3; ++k)
    {
        const float a = k * juce::MathConstants<float>::twoPi / 3.0f - juce::MathConstants<float>::halfPi;
        auto ctr = r.getCentre() + juce::Point<float> (std::cos (a), std::sin (a)) * (rad * 0.62f);
        g.drawEllipse (juce::Rectangle<float> (rad * 2, rad * 2).withCentre (ctr), t);
    }
}

inline void undo (G& g, R r, juce::Colour c)
{
    g.setColour (c);
    juce::Path p; const float t = r.getWidth() * 0.09f;
    p.startNewSubPath (r.getX() + r.getWidth() * 0.15f, r.getY() + r.getHeight() * 0.35f);
    p.lineTo (r.getX() + r.getWidth() * 0.62f, r.getY() + r.getHeight() * 0.35f);
    p.quadraticTo (r.getRight() - r.getWidth() * 0.05f, r.getY() + r.getHeight() * 0.35f, r.getRight() - r.getWidth() * 0.05f, r.getY() + r.getHeight() * 0.62f);
    p.quadraticTo (r.getRight() - r.getWidth() * 0.05f, r.getBottom() - r.getHeight() * 0.1f, r.getX() + r.getWidth() * 0.55f, r.getBottom() - r.getHeight() * 0.1f);
    p.lineTo (r.getX() + r.getWidth() * 0.3f, r.getBottom() - r.getHeight() * 0.1f);
    g.strokePath (p, juce::PathStrokeType (t, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    arrowHead (g, { r.getX() + r.getWidth() * 0.12f, r.getY() + r.getHeight() * 0.35f }, juce::MathConstants<float>::pi, r.getWidth() * 0.25f, t);
}

inline void save (G& g, R r, juce::Colour c)
{
    g.setColour (c);
    const float t = r.getWidth() * 0.085f;
    juce::Path tray;
    tray.startNewSubPath (r.getX() + t, r.getY() + r.getHeight() * 0.55f);
    tray.lineTo (r.getX() + t, r.getBottom() - t);
    tray.lineTo (r.getRight() - t, r.getBottom() - t);
    tray.lineTo (r.getRight() - t, r.getY() + r.getHeight() * 0.55f);
    g.strokePath (tray, juce::PathStrokeType (t, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
    g.drawLine (r.getCentreX(), r.getY() + t, r.getCentreX(), r.getY() + r.getHeight() * 0.68f, t);
    arrowHead (g, { r.getCentreX(), r.getY() + r.getHeight() * 0.7f }, juce::MathConstants<float>::halfPi, r.getWidth() * 0.3f, t);
}

inline void folder (G& g, R r, juce::Colour c)
{
    g.setColour (c);
    juce::Path p; const float t = r.getWidth() * 0.08f;
    p.startNewSubPath (r.getX() + t, r.getY() + r.getHeight() * 0.25f);
    p.lineTo (r.getX() + r.getWidth() * 0.4f, r.getY() + r.getHeight() * 0.25f);
    p.lineTo (r.getX() + r.getWidth() * 0.5f, r.getY() + r.getHeight() * 0.36f);
    p.lineTo (r.getRight() - t, r.getY() + r.getHeight() * 0.36f);
    p.lineTo (r.getRight() - t, r.getBottom() - r.getHeight() * 0.15f);
    p.lineTo (r.getX() + t, r.getBottom() - r.getHeight() * 0.15f);
    p.closeSubPath();
    g.strokePath (p, juce::PathStrokeType (t, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}
inline void layers (G& g, R r, juce::Colour c)
{
    g.setColour (c);
    const float t = r.getWidth() * 0.075f;
    for (int k = 0; k < 3; ++k)
    {
        const float y = r.getY() + r.getHeight() * (0.3f + 0.2f * (float) k);
        juce::Path p;
        p.startNewSubPath (r.getX() + t, y); p.lineTo (r.getCentreX(), y - r.getHeight() * 0.16f); p.lineTo (r.getRight() - t, y); p.lineTo (r.getCentreX(), y + r.getHeight() * 0.16f); p.closeSubPath();
        g.strokePath (p, juce::PathStrokeType (t, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
    }
}
inline void mountain (G& g, R r, juce::Colour c)
{
    g.setColour (c);
    const float t = r.getWidth() * 0.08f;
    juce::Path p;
    p.startNewSubPath (r.getX() + t, r.getBottom() - r.getHeight() * 0.2f);
    p.lineTo (r.getX() + r.getWidth() * 0.4f, r.getY() + r.getHeight() * 0.25f);
    p.lineTo (r.getX() + r.getWidth() * 0.58f, r.getY() + r.getHeight() * 0.5f);
    p.lineTo (r.getX() + r.getWidth() * 0.7f, r.getY() + r.getHeight() * 0.38f);
    p.lineTo (r.getRight() - t, r.getBottom() - r.getHeight() * 0.2f);
    p.closeSubPath();
    g.strokePath (p, juce::PathStrokeType (t, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
}
inline void gearOpen (G& g, R r, juce::Colour c)   // UNDER THE HOOD: a spanner + gear
{
    g.setColour (c);
    const auto ctr = r.getCentre(); const float R1 = r.getWidth() * 0.32f, t = r.getWidth() * 0.08f;
    g.drawEllipse (juce::Rectangle<float> (R1 * 1.1f, R1 * 1.1f).withCentre (ctr), t);
    for (int k = 0; k < 8; ++k)
    {
        const float a = (float) k * juce::MathConstants<float>::twoPi / 8.0f;
        const P d (std::cos (a), std::sin (a));
        g.drawLine ({ ctr + d * (R1 * 0.7f), ctr + d * (R1 * 1.25f) }, t * 1.3f);
    }
}
inline void deepDown (G& g, R r, juce::Colour c)    // DEEP: an arrow into a tray
{
    g.setColour (c);
    const float t = r.getWidth() * 0.08f;
    g.drawLine (r.getCentreX(), r.getY() + t, r.getCentreX(), r.getY() + r.getHeight() * 0.62f, t);
    arrowHead (g, { r.getCentreX(), r.getY() + r.getHeight() * 0.64f }, juce::MathConstants<float>::halfPi, r.getWidth() * 0.28f, t);
    g.drawLine (r.getX() + t, r.getBottom() - t * 1.5f, r.getRight() - t, r.getBottom() - t * 1.5f, t);
}
inline void dna (G& g, R r, juce::Colour c)
{
    g.setColour (c);
    const float t = r.getWidth() * 0.06f;
    for (int s = 0; s < 2; ++s)
    {
        juce::Path p;
        for (int i = 0; i <= 30; ++i)
        {
            const float u = (float) i / 30.0f;
            const P pt (r.getX() + u * r.getWidth(), r.getCentreY() + std::sin (u * 6.283f + (float) s * 3.1416f) * r.getHeight() * 0.32f);
            if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
        }
        g.strokePath (p, juce::PathStrokeType (t, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    for (int i = 1; i < 6; ++i)
    {
        const float u = (float) i / 6.0f, a = std::sin (u * 6.283f) * r.getHeight() * 0.32f;
        g.drawLine (r.getX() + u * r.getWidth(), r.getCentreY() - a, r.getX() + u * r.getWidth(), r.getCentreY() + a, t * 0.7f);
    }
}
inline void chevronRight (G& g, R r, juce::Colour c)
{
    g.setColour (c);
    juce::Path p; const float t = r.getWidth() * 0.14f;
    p.startNewSubPath (r.getX() + r.getWidth() * 0.35f, r.getY() + t);
    p.lineTo (r.getRight() - r.getWidth() * 0.3f, r.getCentreY());
    p.lineTo (r.getX() + r.getWidth() * 0.35f, r.getBottom() - t);
    g.strokePath (p, juce::PathStrokeType (t, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
}
inline void play (G& g, R r, juce::Colour c)
{
    g.setColour (c);
    juce::Path p; p.addTriangle (r.getX() + r.getWidth() * 0.22f, r.getY() + r.getHeight() * 0.12f, r.getX() + r.getWidth() * 0.22f, r.getBottom() - r.getHeight() * 0.12f, r.getRight() - r.getWidth() * 0.1f, r.getCentreY());
    g.fillPath (p);
}
inline void search (G& g, R r, juce::Colour c)
{
    g.setColour (c);
    const float t = r.getWidth() * 0.1f;
    g.drawEllipse (r.getX() + t, r.getY() + t, r.getWidth() * 0.58f, r.getHeight() * 0.58f, t);
    g.drawLine (r.getX() + r.getWidth() * 0.6f, r.getY() + r.getHeight() * 0.6f, r.getRight() - t, r.getBottom() - t, t * 1.2f);
}

inline void upRight (G& g, R r, juce::Colour c)
{
    g.setColour (c);
    const float t = r.getWidth() * 0.11f;
    g.drawLine (r.getX() + t, r.getBottom() - t, r.getRight() - t, r.getY() + t, t);
    juce::Path p;
    p.startNewSubPath (r.getX() + r.getWidth() * 0.35f, r.getY() + t);
    p.lineTo (r.getRight() - t, r.getY() + t);
    p.lineTo (r.getRight() - t, r.getY() + r.getHeight() * 0.65f);
    g.strokePath (p, juce::PathStrokeType (t, juce::PathStrokeType::mitered, juce::PathStrokeType::square));
}
}
}
