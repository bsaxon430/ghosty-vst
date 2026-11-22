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

// ---- GHOST POSTER: the signature shape and the scale a control is drawn at
juce::Path cutRect (juce::Rectangle<float> r, float cut)
{
    cut = juce::jlimit (0.0f, juce::jmin (r.getWidth(), r.getHeight()) * 0.5f, cut);
    juce::Path p;
    p.startNewSubPath (r.getX(), r.getY());
    p.lineTo (r.getRight() - cut, r.getY());
    p.lineTo (r.getRight(), r.getY() + cut);
    p.lineTo (r.getRight(), r.getBottom());
    p.lineTo (r.getX(), r.getBottom());
    p.closeSubPath();
    return p;
}

float scaleOf (const juce::Component& c)
{
    const auto& v = c.getProperties()["k"];
    return v.isVoid() ? 1.0f : juce::jlimit (0.6f, 2.5f, (float) (double) v);
}

juce::Label* LookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = juce::LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (font (juce::jmax (11.5f, 13.0f * scaleOf (s)), 1));
    return l;
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    const auto accent = s.findColour (juce::Slider::rotarySliderFillColourId) == juce::Colours::transparentBlack ? green : s.findColour (juce::Slider::rotarySliderFillColourId);
    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3);
    const float d = juce::jmin (r.getWidth(), r.getHeight());
    r = r.withSizeKeepingCentre (d, d);
    const float th = juce::jmax (5.0f, d * 0.105f);   // (poster: a chunky ring, one flat colour)
    const auto c = r.getCentre();
    const float rad = d * 0.5f - th * 0.5f - 1.0f;
    const float ang = a0 + pos * (a1 - a0);
    juce::Path track; track.addCentredArc (c.x, c.y, rad, rad, 0, a0, a1, true);
    g.setColour (line);
    g.strokePath (track, juce::PathStrokeType (th, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
    juce::Path val; val.addCentredArc (c.x, c.y, rad, rad, 0, a0, ang, true);
    g.setColour (purple);
    g.strokePath (val, juce::PathStrokeType (th, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
    if (isLive (s))
    {
        // TERRAIN is moving this control: a glowing second indicator follows the value in use; the handle stays where you put it
        const float lp = juce::jlimit (0.0f, 1.0f, (float) (double) s.getProperties()["live"]);
        const float la = a0 + lp * (a1 - a0);
        const float lo = juce::jmin (ang, la), hi = juce::jmax (ang, la);
        if (hi - lo > 0.004f)
        {
            juce::Path tether; tether.addCentredArc (c.x, c.y, rad, rad, 0, lo, hi, true);
            g.setColour (live.withAlpha (0.85f));
            g.strokePath (tether, juce::PathStrokeType (th * 0.45f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
        }
        const auto dot = c.getPointOnCircumference (rad, la);
        for (int k = 2; k >= 1; --k) { g.setColour (live.withAlpha (0.16f * (float) (3 - k))); const float rr = th * (0.7f + 0.5f * (float) k); g.fillEllipse (dot.x - rr, dot.y - rr, rr * 2, rr * 2); }
        g.setColour (live);
        g.fillEllipse (dot.x - th * 0.6f, dot.y - th * 0.6f, th * 1.2f, th * 1.2f);
    }
    // the cap: a dark disc with a thick tick that reaches the ring
    const float capR = rad - th * 1.1f;
    g.setColour (panelHi);
    g.fillEllipse (c.x - capR, c.y - capR, capR * 2, capR * 2);
    const auto tip = c.getPointOnCircumference (capR * 0.92f, ang), base = c.getPointOnCircumference (capR * 0.3f, ang);
    g.setColour (s.isMouseOverOrDragging() ? accent : ink);
    g.drawLine ({ base, tip }, juce::jmax (3.0f, th * 0.5f));
}

void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle st, juce::Slider& s)
{
    if (st != juce::Slider::LinearHorizontal && st != juce::Slider::LinearBar) { juce::LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, st, s); return; }
    const float k = scaleOf (s);
    const float cy = std::floor ((float) y + (float) h * 0.5f);
    const float x0 = (float) x + 5, x1 = (float) (x + w) - 5;
    const float th = std::floor (juce::jlimit (4.0f, 12.0f, 6.0f * k));   // the track: a flat bar, square ends
    g.setColour (line);
    g.fillRect (x0, cy - th * 0.5f, x1 - x0, th);
    const float px = juce::jlimit (x0, x1, pos);
    g.setColour (purple);
    g.fillRect (x0, cy - th * 0.5f, juce::jmax (0.0f, px - x0), th);
    if (isLive (s))
    {
        // TERRAIN is moving this control: a glowing marker follows the value in use, tied to the handle (which stays at your setting)
        const float sp = (float) s.valueToProportionOfLength (s.getValue()), lp = juce::jlimit (0.0f, 1.0f, (float) (double) s.getProperties()["live"]);
        const float lx = juce::jlimit (x0, x1, px + (lp - sp) * (float) w);
        g.setColour (live);
        g.fillRect (juce::jmin (px, lx), cy - 1.5f, std::abs (lx - px), 3.0f);
        for (int j = 2; j >= 1; --j) { g.setColour (live.withAlpha (0.16f * (float) (3 - j))); const float rr = (5.0f + 3.0f * (float) j) * k; g.fillEllipse (lx - rr, cy - rr, rr * 2, rr * 2); }
        g.setColour (live);
        g.fillEllipse (lx - 5.5f * k, cy - 5.5f * k, 11.0f * k, 11.0f * k);
        g.setColour (ground);
        g.fillEllipse (lx - 2.0f * k, cy - 2.0f * k, 4.0f * k, 4.0f * k);
    }
    // the handle: the signature block (top-right corner cut)
    const float tw = std::floor (juce::jlimit (8.0f, 18.0f, 10.0f * k)), thh = std::floor (juce::jlimit (14.0f, (float) h, 18.0f * k));
    g.setColour (s.isMouseOverOrDragging() ? green : ink);
    g.fillPath (cutRect ({ px - tw * 0.5f, cy - thh * 0.5f, tw, thh }, tw * 0.45f));
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool primary = b.getProperties()["primary"];
    const bool on = b.getToggleState();
    const auto shape = cutRect (r, juce::jmin (10.0f, r.getHeight() * 0.32f));
    if (primary)
    {
        g.setColour (down ? green.darker (0.2f) : over ? green.brighter (0.15f) : green);
        g.fillPath (shape);
        return;
    }
    // on = a solid purple block with dark words (the page you are on, DOTS / HEAT, the tab); otherwise a dark block with an edge
    g.setColour (on ? (over ? purple.brighter (0.12f) : purple) : down ? band : over ? panelHi : panel);
    g.fillPath (shape);
    if (! on) { g.setColour (over ? inkFaint : line.brighter (0.25f)); g.strokePath (shape, juce::PathStrokeType (1.0f)); }
    if (! b.isEnabled()) { g.setColour (ground.withAlpha (0.5f)); g.fillPath (shape); }
}

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton& b, int h)
{
    const bool primary = b.getProperties()["primary"];
    const float k = scaleOf (b);
    return spaced (juce::jmax (11.0f, juce::jmin ((primary ? 19.0f : 13.0f) * k, (float) h * 0.42f)), primary ? 2 : 1, 0.14f);
}

void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    const bool primary = b.getProperties()["primary"];
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (primary || b.getToggleState() ? ground : b.isEnabled() ? ink : inkFaint);
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (6, 2), juce::Justification::centred, 1, 0.8f);
}

void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat();
    const float k = scaleOf (b);
    const float h = std::floor (juce::jmin (18.0f * k, r.getHeight() - 2));
    auto sw = juce::Rectangle<float> (std::floor (r.getRight() - h * 1.9f - 1), std::floor (r.getCentreY() - h * 0.5f), std::floor (h * 1.9f), h);
    const bool on = b.getToggleState();
    // a chunky block switch: green = on (the block sits right), dark = off
    // (off = a bright knob on a dark track; flat grey all over = switched off for a reason, which its row says)
    const bool can = b.isEnabled();
    g.setColour (! can ? line : on ? green : over ? inkFaint : band.brighter (0.25f));
    g.fillPath (cutRect (sw, h * 0.34f));
    const float kn = h - 6;
    g.setColour (! can ? inkFaint : on ? ground : ink);
    g.fillRect (on ? sw.getRight() - kn - 3 : sw.getX() + 3, sw.getY() + 3, kn, kn);
    if (b.getButtonText().isNotEmpty())
    {
        g.setColour (b.isEnabled() ? ink : inkFaint);
        g.setFont (spaced (juce::jmax (11.0f, 12.5f * k), 1, 0.1f));
        g.drawFittedText (b.getButtonText(), r.withRight (sw.getX() - 6).toNearestInt(), juce::Justification::centredLeft, 1);
    }
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& c)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    const auto shape = cutRect (r, juce::jmin (9.0f, r.getHeight() * 0.32f));
    g.setColour (c.isMouseOver (true) ? panelHi : well);
    g.fillPath (shape);
    g.setColour (c.hasKeyboardFocus (true) ? purple : line.brighter (0.25f));
    g.strokePath (shape, juce::PathStrokeType (1.0f));
    if (isLive (c))
    {
        // TERRAIN is moving this choice: a bright edge and the choice in use, on the right
        g.setColour (live);
        g.strokePath (shape, juce::PathStrokeType (1.5f));
        g.setFont (spaced (juce::jmin (12.0f, (float) h * 0.45f), 1, 0.04f));
        g.drawFittedText (">  " + c.getProperties()["liveText"].toString(), juce::Rectangle<int> (w / 2, 0, w / 2 - 24, h), juce::Justification::centredRight, 1, 0.75f);
    }
    // the arrow: a solid purple wedge
    juce::Path arrow;
    const float a = juce::jlimit (4.0f, 8.0f, (float) h * 0.17f);
    const float ax = (float) w - 10.0f - a, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - a, ay - a * 0.6f, ax + a, ay - a * 0.6f, ax, ay + a * 0.7f);
    g.setColour (c.isEnabled() ? purple : inkFaint);
    g.fillPath (arrow);
}

juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox& c) { return spaced (juce::jmax (11.0f, juce::jmin (13.0f * scaleOf (c), (float) c.getHeight() * 0.5f)), 1, 0.06f); }
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
    const auto shape = cutRect (r.reduced (0.5f), 14.0f);
    g.setColour (hi ? panelHi : panel);
    g.fillPath (shape);
    g.setColour (hi ? purple : line);
    g.strokePath (shape, juce::PathStrokeType (hi ? 1.5f : 1.0f));
}

// A numbered section (GHOST POSTER): the card, a header band in a lighter tone, the number in a solid block, the name big.
void zone (juce::Graphics& g, juce::Rectangle<int> ri, const juce::String& number, const juce::String& name, int headH, bool hot, bool lower, juce::Colour accent)
{
    const auto r = ri.toFloat().reduced (0.5f);
    const float hh = (float) headH, cut = juce::jmin (16.0f, hh * 0.4f);
    g.setColour (panel);
    g.fillRect (r.withTrimmedTop (hh - 1.0f));
    g.setColour (band);
    g.fillPath (cutRect (r.withHeight (hh), cut));
    float x = r.getX();
    if (number.isNotEmpty())
    {
        const float bw = std::floor (hh * 1.05f);
        g.setColour (accent);
        g.fillRect (r.getX(), r.getY(), bw, hh);
        g.setColour (ground);
        g.setFont (font (juce::jmax (10.5f, hh * 0.52f), 2));
        g.drawText (number, juce::Rectangle<float> (r.getX(), r.getY(), bw, hh).toNearestInt(), juce::Justification::centred, false);
        x += bw;
    }
    g.setColour (ink);
    if (lower) g.setFont (font (juce::jlimit (12.0f, 28.0f, hh * 0.52f), 2));
    else       g.setFont (spaced (juce::jlimit (11.0f, 22.0f, hh * 0.40f), 2, 0.14f));
    g.drawFittedText (name, juce::Rectangle<float> (x + hh * 0.3f, r.getY(), r.getRight() - x - hh * 0.3f - cut, hh).toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
    g.setColour (hot ? purple : line);
    g.strokePath (cutRect (r, cut), juce::PathStrokeType (hot ? 1.5f : 1.0f));
}

void title (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> r, juce::Colour c, float size)
{
    g.setColour (c);
    g.setFont (spaced (size, 1, 0.16f));
    g.drawText (text, r, juce::Justification::centredLeft, true);
}
}
