// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
#include "Look.h"
#include "GhostMosaicFonts.h"

namespace mosaic::look
{
static juce::Typeface::Ptr face (int weight)
{
    static juce::Typeface::Ptr f[3] = {
        juce::Typeface::createSystemTypefaceFor (GhostMosaicFonts::PoppinsMedium_ttf, GhostMosaicFonts::PoppinsMedium_ttfSize),
        juce::Typeface::createSystemTypefaceFor (GhostMosaicFonts::PoppinsSemiBold_ttf, GhostMosaicFonts::PoppinsSemiBold_ttfSize),
        juce::Typeface::createSystemTypefaceFor (GhostMosaicFonts::PoppinsBold_ttf, GhostMosaicFonts::PoppinsBold_ttfSize) };
    return f[juce::jlimit (0, 2, weight)];
}

juce::Font font (float height, int weight)
{
    return juce::Font (juce::FontOptions (face (weight)).withHeight (height));
}

juce::Font spaced (float height, int weight, float tracking)
{
    return font (height, weight).withExtraKerningFactor (tracking);
}

LookAndFeel::LookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, ground);
    setColour (juce::Label::textColourId, ink);
    setColour (juce::TextButton::buttonColourId, panel);
    setColour (juce::TextButton::buttonOnColourId, purple.withAlpha (0.25f));
    setColour (juce::TextButton::textColourOffId, ink);
    setColour (juce::TextButton::textColourOnId, ink);
    setColour (juce::ComboBox::backgroundColourId, panel);
    setColour (juce::ComboBox::outlineColourId, line);
    setColour (juce::ComboBox::textColourId, ink);
    setColour (juce::ComboBox::arrowColourId, inkDim);
    setColour (juce::PopupMenu::backgroundColourId, panelHi);
    setColour (juce::PopupMenu::textColourId, ink);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, purple.withAlpha (0.35f));
    setColour (juce::PopupMenu::highlightedTextColourId, ink);
    setColour (juce::TextEditor::backgroundColourId, ground);
    setColour (juce::TextEditor::outlineColourId, line);
    setColour (juce::TextEditor::focusedOutlineColourId, purple);
    setColour (juce::TextEditor::textColourId, ink);
    setColour (juce::TextEditor::highlightColourId, purple.withAlpha (0.4f));
    setColour (juce::CaretComponent::caretColourId, green);
    setColour (juce::Slider::textBoxTextColourId, ink);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::ScrollBar::thumbColourId, line);
    setColour (juce::TooltipWindow::backgroundColourId, panelHi);
    setColour (juce::TooltipWindow::textColourId, ink);
    setColour (juce::AlertWindow::backgroundColourId, panelHi);
    setColour (juce::AlertWindow::textColourId, ink);
    setColour (juce::DocumentWindow::backgroundColourId, ground);
    setDefaultSansSerifTypeface (face (0));
}

juce::Typeface::Ptr LookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName()) return face (f.isBold() ? 2 : 0);
    return juce::LookAndFeel_V4::getTypefaceForFont (f);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    const auto accent = s.findColour (juce::Slider::rotarySliderFillColourId) == juce::Colours::transparentBlack ? green : s.findColour (juce::Slider::rotarySliderFillColourId);
    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4);
    const float d = juce::jmin (r.getWidth(), r.getHeight());
    r = r.withSizeKeepingCentre (d, d);
    const float th = juce::jmax (4.0f, d * 0.075f);
    const auto c = r.getCentre();
    const float rad = d * 0.5f - th;
    const float ang = a0 + pos * (a1 - a0);
    juce::Path track; track.addCentredArc (c.x, c.y, rad, rad, 0, a0, a1, true);
    g.setColour (line);
    g.strokePath (track, juce::PathStrokeType (th, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    juce::Path val; val.addCentredArc (c.x, c.y, rad, rad, 0, a0, ang, true);
    juce::ColourGradient grad (purple, c.x - rad, c.y + rad, accent, c.x + rad, c.y - rad, false);
    g.setGradientFill (grad);
    g.strokePath (val, juce::PathStrokeType (th, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (isLive (s))
    {
        // TERRAIN is moving this control: a glowing second indicator follows the value in use; the handle stays where you put it
        const float lp = juce::jlimit (0.0f, 1.0f, (float) (double) s.getProperties()["live"]);
        const float la = a0 + lp * (a1 - a0);
        const float lo = juce::jmin (ang, la), hi = juce::jmax (ang, la);
        if (hi - lo > 0.004f)
        {
            juce::Path tether; tether.addCentredArc (c.x, c.y, rad, rad, 0, lo, hi, true);
            g.setColour (live.withAlpha (0.28f));
            g.strokePath (tether, juce::PathStrokeType (th * 1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (live.withAlpha (0.85f));
            g.strokePath (tether, juce::PathStrokeType (th * 0.45f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
        }
        juce::Path ring; ring.addCentredArc (c.x, c.y, rad + th * 0.95f, rad + th * 0.95f, 0, a0, la, true);
        g.setColour (live.withAlpha (0.9f));
        g.strokePath (ring, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const auto dot = c.getPointOnCircumference (rad, la);
        for (int k = 3; k >= 1; --k) { g.setColour (live.withAlpha (0.12f * (float) (4 - k))); const float rr = th * (0.7f + 0.5f * (float) k); g.fillEllipse (dot.x - rr, dot.y - rr, rr * 2, rr * 2); }
        g.setColour (live);
        g.fillEllipse (dot.x - th * 0.55f, dot.y - th * 0.55f, th * 1.1f, th * 1.1f);
    }
    // the cap: a dark disc with a light tick
    const float capR = rad - th * 1.4f;
    g.setColour (panelHi);
    g.fillEllipse (c.x - capR, c.y - capR, capR * 2, capR * 2);
    g.setColour (line.brighter (0.2f));
    g.drawEllipse (c.x - capR, c.y - capR, capR * 2, capR * 2, 1.0f);
    const auto tip = c.getPointOnCircumference (capR * 0.82f, ang), base = c.getPointOnCircumference (capR * 0.35f, ang);
    g.setColour (s.isMouseOverOrDragging() ? accent : ink);
    g.drawLine ({ base, tip }, juce::jmax (2.0f, th * 0.45f));
}

void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle st, juce::Slider& s)
{
    if (st != juce::Slider::LinearHorizontal && st != juce::Slider::LinearBar) { juce::LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, st, s); return; }
    const float cy = (float) y + (float) h * 0.5f;
    const float x0 = (float) x + 4, x1 = (float) (x + w) - 4;
    g.setColour (line);
    g.fillRoundedRectangle (x0, cy - 2.5f, x1 - x0, 5.0f, 2.5f);
    const float px = juce::jlimit (x0, x1, pos);
    juce::ColourGradient grad (purple, x0, cy, green, x1, cy, false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (x0, cy - 2.5f, juce::jmax (0.0f, px - x0), 5.0f, 2.5f);
    if (isLive (s))
    {
        // TERRAIN is moving this control: a glowing marker follows the value in use, tied to the handle (which stays at your setting)
        const float sp = (float) s.valueToProportionOfLength (s.getValue()), lp = juce::jlimit (0.0f, 1.0f, (float) (double) s.getProperties()["live"]);
        const float lx = juce::jlimit (x0, x1, px + (lp - sp) * (float) w);
        g.setColour (live.withAlpha (0.35f));
        g.fillRoundedRectangle (juce::jmin (px, lx), cy - 3.5f, std::abs (lx - px), 7.0f, 3.5f);
        g.setColour (live.withAlpha (0.9f));
        g.fillRoundedRectangle (juce::jmin (px, lx), cy - 1.5f, std::abs (lx - px), 3.0f, 1.5f);
        for (int k = 3; k >= 1; --k) { g.setColour (live.withAlpha (0.12f * (float) (4 - k))); const float rr = 5.0f + 3.0f * (float) k; g.fillEllipse (lx - rr, cy - rr, rr * 2, rr * 2); }
        g.setColour (live);
        g.fillEllipse (lx - 5.5f, cy - 5.5f, 11.0f, 11.0f);
        g.setColour (ground);
        g.fillEllipse (lx - 2.0f, cy - 2.0f, 4.0f, 4.0f);
    }
    g.setColour (s.isMouseOverOrDragging() ? green : ink);
    g.fillEllipse (px - 6.5f, cy - 6.5f, 13.0f, 13.0f);
    g.setColour (ground);
    g.fillEllipse (px - 2.5f, cy - 2.5f, 5.0f, 5.0f);
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool primary = b.getProperties()["primary"];
    const bool on = b.getToggleState();
    const float cr = juce::jmin (10.0f, r.getHeight() * 0.3f);
    if (primary)
    {
        g.setColour (down ? green.darker (0.2f) : over ? green.brighter (0.15f) : green);
        g.fillRoundedRectangle (r, cr);
        return;
    }
    g.setColour (on ? purple.withAlpha (0.22f) : over ? panelHi : panel);
    g.fillRoundedRectangle (r, cr);
    g.setColour (on ? purple : over ? line.brighter (0.4f) : line);
    g.drawRoundedRectangle (r, cr, 1.0f);
    if (! b.isEnabled()) { g.setColour (ground.withAlpha (0.5f)); g.fillRoundedRectangle (r, cr); }
}

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton& b, int h)
{
    const bool primary = b.getProperties()["primary"];
    return spaced (juce::jmin (primary ? 17.0f : 13.0f, (float) h * 0.42f), 1, 0.14f);
}

void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    const bool primary = b.getProperties()["primary"];
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (primary ? ground : b.isEnabled() ? ink : inkFaint);
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (6, 2), juce::Justification::centred, 1, 0.8f);
}

void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat();
    const float h = juce::jmin (18.0f, r.getHeight() - 4);
    auto sw = juce::Rectangle<float> (r.getRight() - h * 1.8f - 2, r.getCentreY() - h * 0.5f, h * 1.8f, h);
    const bool on = b.getToggleState();
    g.setColour (on ? green.withAlpha (0.9f) : line);
    g.fillRoundedRectangle (sw, h * 0.5f);
    const float k = h - 4;
    g.setColour (on ? ground : over ? ink : inkDim);
    g.fillEllipse (on ? sw.getRight() - k - 2 : sw.getX() + 2, sw.getY() + 2, k, k);
    if (b.getButtonText().isNotEmpty())
    {
        g.setColour (b.isEnabled() ? ink : inkFaint);
        g.setFont (spaced (12.5f, 1, 0.1f));
        g.drawFittedText (b.getButtonText(), r.withRight (sw.getX() - 6).toNearestInt(), juce::Justification::centredLeft, 1);
    }
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& c)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (c.isMouseOver (true) ? panelHi : panel);
    g.fillRoundedRectangle (r, 8);
    g.setColour (c.hasKeyboardFocus (true) ? purple : line);
    g.drawRoundedRectangle (r, 8, 1);
    if (isLive (c))
    {
        // TERRAIN is moving this choice: a glowing edge and the choice in use, on the right
        for (int k = 3; k >= 1; --k) { g.setColour (live.withAlpha (0.10f * (float) (4 - k))); g.drawRoundedRectangle (r.expanded ((float) k * 0.8f), 8.0f + (float) k * 0.8f, 1.5f); }
        g.setColour (live);
        g.drawRoundedRectangle (r, 8, 1.5f);
        g.setFont (spaced (juce::jmin (12.0f, (float) h * 0.45f), 1, 0.04f));
        g.drawFittedText (">  " + c.getProperties()["liveText"].toString(), juce::Rectangle<int> (w / 2, 0, w / 2 - 24, h), juce::Justification::centredRight, 1, 0.75f);
    }
    juce::Path arrow;
    const float ax = (float) w - 16, ay = (float) h * 0.5f;
    arrow.startNewSubPath (ax - 4, ay - 2); arrow.lineTo (ax, ay + 2); arrow.lineTo (ax + 4, ay - 2);
    g.setColour (inkDim);
    g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox& c) { return spaced (juce::jmin (13.0f, (float) c.getHeight() * 0.5f), 1, 0.06f); }
juce::Font LookAndFeel::getPopupMenuFont() { return font (14.0f, 0); }
juce::Font LookAndFeel::getLabelFont (juce::Label& l) { return l.getFont(); }

void LookAndFeel::positionComboBoxText (juce::ComboBox& c, juce::Label& l)
{
    l.setBounds (8, 1, isLive (c) ? (c.getWidth() - 28) / 2 : c.getWidth() - 28, c.getHeight() - 2);
    l.setFont (getComboBoxFont (c));
}

void LookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    g.setColour (panelHi);
    g.fillRoundedRectangle (0, 0, (float) w, (float) h, 6);
    g.setColour (purple.withAlpha (0.6f));
    g.drawRoundedRectangle (0.5f, 0.5f, (float) w - 1, (float) h - 1, 6, 1);
    g.setColour (ink);
    g.setFont (font (13.5f, 0));
    g.drawFittedText (text, juce::Rectangle<int> (w, h).reduced (10, 6), juce::Justification::centredLeft, 4);
}

juce::Rectangle<int> LookAndFeel::getTooltipBounds (const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> area)
{
    const int w = juce::jmin (340, (int) juce::GlyphArrangement::getStringWidth (font (13.5f, 0), text) + 24);
    const int lines = 1 + (int) (juce::GlyphArrangement::getStringWidth (font (13.5f, 0), text) / 316.0f);
    const int h = 14 + 18 * juce::jmin (4, lines);
    return juce::Rectangle<int> (pos.x > area.getCentreX() ? pos.x - (w + 12) : pos.x + 18, pos.y > area.getCentreY() ? pos.y - (h + 8) : pos.y + 18, w, h).constrainedWithin (area);
}

void LookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int ts, int tsz, bool over, bool)
{
    auto r = vertical ? juce::Rectangle<int> (x + w / 2 - 3, ts, 6, tsz) : juce::Rectangle<int> (ts, y + h / 2 - 3, tsz, 6);
    g.setColour (over ? purple.withAlpha (0.7f) : line.brighter (0.2f));
    g.fillRoundedRectangle (r.toFloat(), 3);
}

void card (juce::Graphics& g, juce::Rectangle<float> r, bool hi)
{
    g.setColour (hi ? panelHi : panel);
    g.fillRoundedRectangle (r, 12);
    g.setColour (hi ? purple.withAlpha (0.8f) : line);
    g.drawRoundedRectangle (r.reduced (0.5f), 12, hi ? 1.5f : 1.0f);
}

void title (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> r, juce::Colour c, float size)
{
    g.setColour (c);
    g.setFont (spaced (size, 1, 0.16f));
    g.drawText (text, r, juce::Justification::centredLeft, true);
}
}
