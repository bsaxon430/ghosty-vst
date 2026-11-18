// Ghost Voice - AGPL-3.0-only (see LICENSE, NOTICE.md).
// The look: GhostBlock themes (BLACK / PURPLE / LIGHT) drawn the way Brandon's mockup draws them: deep panels with a thin
// violet edge, glowing gradient sliders, arc knobs, rounded buttons.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "GhostThemes.h"
#include "GhostArt.h"
#include "SplashArt.h"

namespace gv
{
namespace col
{
inline juce::Colour voiceA (const ghosty::themes::Palette& p) { return p.isLight ? juce::Colour (0xff0e86b8) : juce::Colour (0xff9fb8f0); }
inline juce::Colour voiceB (const ghosty::themes::Palette& p) { return p.isLight ? juce::Colour (0xff7a3fd6) : juce::Colour (0xffb3a2f0); }
inline juce::Colour voiceC (const ghosty::themes::Palette& p) { return p.isLight ? juce::Colour (0xffb5179e) : juce::Colour (0xffe7a7b6); }
inline juce::Colour edge (const ghosty::themes::Palette& p) { return p.isLight ? p.line : juce::Colour (0x40b48cff); }
}

inline juce::Font gfont (float h, bool bold = true) { return ghosty::font (h, bold); }

// build 9: every panel is SPLASH's gooey melting one with a sketched outline (SplashArt.h)
inline void panel (juce::Graphics& g, const ghosty::themes::Palette& P, juce::Rectangle<float> r, float rad = 12.0f, juce::Colour glow = {})
{
    splash::gooPanel (g, P, r, rad, glow);
}

class Look : public juce::LookAndFeel_V4
{
public:
    const ghosty::themes::Palette* P = &ghosty::themes::get (ghosty::themes::Theme::Purple);
    void setPalette (const ghosty::themes::Palette& p)
    {
        P = &p;
        setColourScheme ({ p.window, p.panel, p.card, p.line, p.ink, p.violet, p.onAccent, p.violet, p.ink });
        setColour (juce::TextButton::buttonColourId, p.cell);
        setColour (juce::TextButton::buttonOnColourId, p.violet);
        setColour (juce::TextButton::textColourOffId, p.ink);
        setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        setColour (juce::Label::textColourId, p.ink);
        setColour (juce::Slider::textBoxTextColourId, p.ink);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::ComboBox::backgroundColourId, p.cell);
        setColour (juce::ComboBox::textColourId, p.ink);
        setColour (juce::ComboBox::outlineColourId, col::edge (p));
        setColour (juce::ComboBox::arrowColourId, p.ink);
        setColour (juce::PopupMenu::backgroundColourId, p.card);
        setColour (juce::PopupMenu::textColourId, p.ink);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, p.violet);
        setColour (juce::ProgressBar::foregroundColourId, col::voiceC (p));
        setColour (juce::ProgressBar::backgroundColourId, p.cell);
        setColour (juce::TooltipWindow::backgroundColourId, p.card);
        setColour (juce::TooltipWindow::textColourId, p.ink);
        setColour (juce::TooltipWindow::outlineColourId, col::edge (p));
        setColour (juce::TextEditor::backgroundColourId, p.cell);
        setColour (juce::TextEditor::textColourId, p.ink);
        setColour (juce::TextEditor::outlineColourId, col::edge (p));
        setColour (juce::AlertWindow::backgroundColourId, p.card);
        setColour (juce::AlertWindow::textColourId, p.ink);
    }
    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return gfont (juce::jmin (15.0f, (float) h * 0.42f)); }
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {   // SPLASH: a soft gooey pill; ON = a wash of two paints; the outline sketched in a few thin passes
        // HAND-DRAWN (build 10): paper + pale hatching, or a painted wash when ON, with an ink line round it
        const auto r = b.getLocalBounds().toFloat();
        splash::handButton (g, *P, r, b.getToggleState(), b.isEnabled(), over || down, splash::seedOf (r) + (juce::int64) b.getButtonText().hashCode());
    }
    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {   // readable: dark ink, white on the painted ON pill, faded when it does nothing
        g.setFont (getTextButtonFont (b, b.getHeight()));
        g.setColour ((b.getToggleState() ? juce::Colours::white : P->ink).withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.45f));
        g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (6, 2), juce::Justification::centred, 1, 0.8f);
    }
    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& c) override
    {
        auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (1.5f);
        const auto seed = splash::seedOf (r) + c.getComponentID().hashCode() + c.getX() * 13 + c.getY();
        const auto& body = splash::handBox (r, juce::jmin (r.getHeight() * 0.5f, 12.0f), seed);
        g.setColour (P->card.withMultipliedAlpha (c.isEnabled() ? 1.0f : 0.45f)); g.fillPath (body);
        if (c.isEnabled()) splash::inkLine (g, body, seed + 1, P->ink, 1.1f, 0.6f);
        else { g.setColour (P->line.withAlpha (0.6f)); g.strokePath (body, juce::PathStrokeType (0.9f)); }
        juce::Path arrow; const float ax = (float) w - 16, ay = (float) h * 0.5f;
        arrow.startNewSubPath (ax - 4.5f, ay - 2.5f); arrow.lineTo (ax, ay + 2.5f); arrow.lineTo (ax + 4.5f, ay - 2.5f);
        g.setColour (P->ink.withMultipliedAlpha (c.isEnabled() ? 1.0f : 0.4f));
        g.strokePath (splash::wobble (arrow, seed + 2, 0.4f, 2.0f), juce::PathStrokeType (1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    juce::Font getComboBoxFont (juce::ComboBox& c) override { return gfont (juce::jmin (13.0f, (float) c.getHeight() * 0.5f)); }
    void positionComboBoxText (juce::ComboBox& c, juce::Label& l) override
    { l.setBounds (10, 1, c.getWidth() - 30, c.getHeight() - 2); l.setFont (getComboBoxFont (c)); }
    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider& s) override
    {   // SPLASH: a soft track, the value filled with a paint wash, a white goo blob for the thumb with a sketched rim
        const float cy = (float) y + (float) h * 0.5f;
        const float a = s.isEnabled() ? 1.0f : 0.35f;
        auto tr = juce::Rectangle<float> ((float) x, cy - 4.0f, (float) w, 8.0f);
        const auto seed = (juce::int64) (s.getX() * 31 + s.getY() * 7 + w);
        const auto& track = splash::handBox (tr, 4.0f, seed, 0.6f);
        g.setColour (P->card.withMultipliedAlpha (a)); g.fillPath (track);
        const auto c0 = s.getProperties().contains ("c0") ? juce::Colour ((juce::uint32) (juce::int64) s.getProperties()["c0"]) : splash::art().wave0;
        const auto c1 = s.getProperties().contains ("c1") ? juce::Colour ((juce::uint32) (juce::int64) s.getProperties()["c1"]) : splash::art().wave2;
        const bool full = s.getProperties().contains ("full");   // BALANCE: the whole track is the A..B gradient
        {   // the value: a painted wash with pen hatching, inside the track
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion ((full ? tr : tr.withRight (pos)).expanded (0, 2).toNearestInt());
            juce::ColourGradient gr (c0.withMultipliedAlpha (a), tr.getX(), cy, c1.withMultipliedAlpha (a), tr.getRight(), cy, false);
            gr.addColour (0.5, splash::art().wave1.withMultipliedAlpha (a));
            g.setGradientFill (gr); g.fillPath (track);
            splash::hatch (g, track, seed, splash::art().onHatch.withAlpha (0.22f * a), 3.5f);
        }
        if (s.isEnabled()) splash::inkLine (g, track, seed + 1, P->ink, 1.0f, 0.5f);
        else { g.setColour (P->line.withAlpha (0.5f)); g.strokePath (track, juce::PathStrokeType (0.8f)); }
        const float r = 8.5f;
        juce::Path blob; blob.addEllipse (pos - r, cy - r, 2 * r, 2 * r);
        const auto hb = splash::wobble (blob, seed + (juce::int64) pos, 0.7f, 3.0f);
        g.setColour (P->shadow); g.fillPath (hb, juce::AffineTransform::translation (0, 2));
        g.setColour (juce::Colours::white.withMultipliedAlpha (a)); g.fillPath (hb);
        if (s.isEnabled()) splash::inkLine (g, blob, seed + 5, P->ink, 1.2f, 0.6f);
    }
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) override
    {   // SPLASH: a milky knob with a sketched rim, the value as a painted arc
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
        const float rad = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f, cx = r.getCentreX(), cy = r.getCentreY();
        const float a = s.isEnabled() ? 1.0f : 0.4f;
        const auto seed = (juce::int64) (cx * 13 + cy * 7);
        juce::Path track; track.addCentredArc (cx, cy, rad - 3, rad - 3, 0, a0, a1, true);
        g.setColour (P->cell.withMultipliedAlpha (a));
        g.strokePath (splash::wobble (track, seed, 0.5f, 3.0f), juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const bool bipolar = s.getMinimum() < 0 && s.getMaximum() > 0;
        const float mid = a0 + (a1 - a0) * (float) ((0 - s.getMinimum()) / (s.getMaximum() - s.getMinimum()));
        const float ang = a0 + pos * (a1 - a0);
        juce::Path val; val.addCentredArc (cx, cy, rad - 3, rad - 3, 0, bipolar ? juce::jmin (mid, ang) : a0, bipolar ? juce::jmax (mid, ang) : ang, true);
        juce::ColourGradient gr (splash::art().wave0.withMultipliedAlpha (a), cx - rad, cy + rad, splash::art().wave2.withMultipliedAlpha (a), cx + rad, cy - rad, false);
        g.setGradientFill (gr);
        g.strokePath (splash::wobble (val, seed + 1, 0.6f, 3.0f), juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        juce::Path face; face.addEllipse (cx - rad + 8, cy - rad + 8, 2 * rad - 16, 2 * rad - 16);
        const auto hf = splash::wobble (face, seed + 2, 0.7f, 3.0f);
        g.setColour (P->shadow); g.fillPath (hf, juce::AffineTransform::translation (0, 2));
        g.setColour (P->card.withMultipliedAlpha (a)); g.fillPath (hf);
        if (s.isEnabled()) splash::inkLine (g, face, seed + 3, P->ink, 1.1f, 0.6f);
        juce::Path tick; tick.startNewSubPath (0, -(rad - 10)); tick.lineTo (0, -(rad - 10) + rad * 0.42f);
        g.setColour (P->ink.withMultipliedAlpha (a));
        g.strokePath (tick, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), juce::AffineTransform::rotation (ang).translated (cx, cy));
    }
    juce::Label* createSliderTextBox (juce::Slider& s) override
    {
        auto* l = juce::LookAndFeel_V4::createSliderTextBox (s);
        l->setFont (gfont (12.0f, false)); l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        return l;
    }
};
}
