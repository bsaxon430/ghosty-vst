#pragma once
// Ghost Slice look: the three house themes (Shared/GhostThemes.h: BLACK / PURPLE / LIGHT) with the editor's own roles on top:
// graphite surfaces (BLACK), purple waveform, cyan markers, violet for pitch, amber timing warnings. Every role colour is made
// readable on the surface it is drawn on (>= 3:1 for marks, >= 4.5:1 for text); SliceTests checks it in all three themes.
// GHOST (Slice-only 4th look): palette (Theme::Light, true) = liquid glass on the LIGHT base (Shared/GhostGlass.h); the LnF draws glass only when P.glass.
#include <juce_gui_basics/juce_gui_basics.h>
#include "GhostThemes.h"
#include "GhostGlass.h"

namespace gslice::ui
{
using ghosty::themes::Theme;
struct Palette
{
    Theme theme = Theme::Dark; bool light = false;
    bool glass = false;              // the GHOST look (Slice-only): liquid glass on the LIGHT base (theme = Light, light = true)
    juce::Colour window, header, panel, card, cell, line, ink, dim, onAccent;
    juce::Colour waveBg, waveBgAlt, wave, waveCore, waveOther;   // waveOther = a region under another one / not selected focus
    juce::Colour marker, markerDim, pitch, warn, bad, good;
    juce::Colour slime;              // the neon-green slime accent (the mascot's cleaver; GHOST CHOP cuts, the selected slice)
    juce::Colour draw;               // GHOST PITCH: the drawn / edited pitch curve (a deeper violet than the note blobs)
    juce::Colour f0;                 // the detected f0 curve (F0 CURVE): ink on the house looks, pink on GHOST
    juce::Colour select, playhead, cursor, grid, gridBar, region, regionLine, regionSel, header2;
    juce::Colour chordBg, chordInk, chordLine, chordNc;   // the CHORDS lane (phase 17): opaque chips, text >= 4.5:1 on them
};

inline Palette palette (Theme t, bool glass = false)
{
    using C = juce::Colour; using ghosty::themes::readableOn;
    const auto& h = ghosty::themes::get (t);
    Palette p; p.theme = t; p.light = h.isLight;
    switch (t)
    {
        case Theme::Dark:   // graphite
            p.window = C (0xff1a1b20); p.header = C (0xff141519); p.panel = C (0xff212228); p.card = C (0xff2a2b32); p.cell = C (0xff2c2d35);
            p.line = C (0xff40414c); p.ink = C (0xffeeebf5); p.dim = C (0xffa6a2b5); p.onAccent = C (0xff15161a);
            p.waveBg = C (0xff111215); p.waveBgAlt = C (0xff16171c);
            p.wave = C (0xffa77bff); p.marker = C (0xff22e6ff); p.pitch = C (0xffc4a6ff); p.warn = C (0xffffb020);
            break;
        case Theme::Purple:
            p.window = h.window; p.header = h.windowBottom; p.panel = h.panel; p.card = h.card; p.cell = h.cell; p.line = h.line;
            p.ink = h.ink; p.dim = h.dim; p.onAccent = h.onAccent;
            p.waveBg = C (0xff241a38); p.waveBgAlt = C (0xff2a1f41);
            p.wave = C (0xffc9a8ff); p.marker = C (0xff22e6ff); p.pitch = C (0xffe0ccff); p.warn = C (0xffffc04d);
            break;
        case Theme::Light:
            p.window = h.window; p.header = C (0xffc9c3d4); p.panel = h.panel; p.card = h.card; p.cell = h.cell; p.line = h.line;
            p.ink = h.ink; p.dim = h.dim; p.onAccent = h.onAccent;
            p.waveBg = C (0xffe2dde9); p.waveBgAlt = C (0xffd9d3e2);
            p.wave = C (0xff5a2fd0); p.marker = C (0xff006b80); p.pitch = C (0xff7a3fe0); p.warn = C (0xff8a5200);
            if (glass)
            {   // GHOST: the flat tokens = the frosted glass as it reads over the aurora (contrast maths + any flat fill)
                namespace gg = ghosty::glass;
                p.glass = true;
                p.window = C (0xffebe6f8); p.header = C (0xfff1eefa); p.panel = C (0xfff0edf9); p.card = C (0xfff7f5fd); p.cell = C (0xffe6e1f4);
                p.line = C (0xffb8afd8); p.ink = gg::ink(); p.dim = gg::ink2(); p.onAccent = C (0xffffffff);
                p.waveBg = C (0xffe9e5f5); p.waveBgAlt = C (0xffe1dcf0);
                p.wave = C (0xff4f2bc4); p.marker = C (0xff075a52); p.pitch = C (0xff6d3fe0); p.warn = C (0xff744400);
            }
            break;
    }
    p.wave = readableOn (p.wave, p.waveBg, 3.0); p.marker = readableOn (p.marker, p.waveBg, 3.0);
    p.pitch = readableOn (p.pitch, p.waveBg, 3.0); p.warn = readableOn (p.warn, p.waveBg, 3.0);
    p.slime = readableOn (readableOn (p.light ? C (0xff0a7f2c) : t == Theme::Purple ? C (0xff5dff8a) : C (0xff3dff6e), p.waveBg, 3.6), p.window, 3.3);
    p.draw = readableOn (p.light ? C (0xff5b12c8) : t == Theme::Purple ? C (0xffff9cf2) : C (0xffd77bff), p.waveBg, 3.0);
    p.bad = readableOn (h.bad, p.waveBg, 3.0); p.good = readableOn (h.good, p.panel, 3.0);
    p.waveCore = p.light ? p.wave.darker (0.35f) : p.wave.brighter (0.45f);
    p.waveOther = readableOn (p.wave.withSaturation (p.wave.getSaturation() * 0.35f), p.waveBg, 2.0);
    p.markerDim = p.marker.withAlpha (0.45f);
    p.select = p.pitch.withAlpha (p.light ? 0.22f : 0.18f);
    p.playhead = p.light ? C (0xff1b1830) : C (0xfff4f0ff);
    p.cursor = p.marker;
    p.grid = p.ink.withAlpha (p.light ? 0.12f : 0.08f); p.gridBar = p.ink.withAlpha (p.light ? 0.26f : 0.20f);
    p.region = p.light ? C (0xffd6d0e0) : p.waveBgAlt; p.regionLine = p.line;
    p.regionSel = readableOn (p.wave, p.waveBg, 3.0);
    p.header2 = p.cell;
    p.f0 = p.glass ? readableOn (ghosty::glass::pink(), p.waveBg, 3.0) : p.ink;
    if (p.glass) { p.select = ghosty::glass::violet().withAlpha (0.16f); p.region = C (0xffe4dff3); p.playhead = ghosty::glass::ink(); }
    // CHORDS lane chips: the card tinted with the ghost violet (GHOST: white glass with the deep violet ink), N.C. chips plainer
    p.chordBg = p.glass ? C (0xfff8f6fe) : p.card.interpolatedWith (p.pitch, p.light ? 0.10f : 0.16f);
    p.chordInk = readableOn (p.glass ? ghosty::glass::violetDeep() : p.light ? p.pitch.darker (0.4f) : p.pitch.brighter (0.25f), p.chordBg, 4.8);
    p.chordLine = readableOn (p.glass ? ghosty::glass::violet() : p.pitch, p.chordBg, 3.0);
    p.chordNc = readableOn (p.dim, p.chordBg, 4.6);
    return p;
}

// house look-and-feel tuned to the palette (buttons, sliders, combos, menus, labels, text boxes, tooltips)
class LnF : public juce::LookAndFeel_V4
{
public:
    Palette P;
    void set (const Palette& p)
    {
        P = p; using L = juce::LookAndFeel_V4;
        setColour (juce::ResizableWindow::backgroundColourId, P.window);
        setColour (juce::TextButton::buttonColourId, P.cell); setColour (juce::TextButton::buttonOnColourId, P.wave);
        setColour (juce::TextButton::textColourOffId, P.ink); setColour (juce::TextButton::textColourOnId, P.onAccent);
        setColour (juce::ComboBox::backgroundColourId, P.cell); setColour (juce::ComboBox::outlineColourId, P.line);
        setColour (juce::ComboBox::textColourId, P.ink); setColour (juce::ComboBox::arrowColourId, P.marker);
        setColour (juce::PopupMenu::backgroundColourId, P.card); setColour (juce::PopupMenu::textColourId, P.ink);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, P.wave); setColour (juce::PopupMenu::highlightedTextColourId, P.onAccent);
        setColour (juce::Label::textColourId, P.ink);
        setColour (juce::Slider::textBoxTextColourId, P.ink); setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, P.cell); setColour (juce::Slider::trackColourId, P.wave);
        setColour (juce::Slider::backgroundColourId, P.cell); setColour (juce::Slider::thumbColourId, P.marker);
        setColour (juce::TextEditor::backgroundColourId, P.card); setColour (juce::TextEditor::textColourId, P.ink);
        setColour (juce::TextEditor::outlineColourId, P.line); setColour (juce::TextEditor::focusedOutlineColourId, P.marker);
        setColour (juce::TooltipWindow::backgroundColourId, P.card); setColour (juce::TooltipWindow::textColourId, P.ink); setColour (juce::TooltipWindow::outlineColourId, P.line);
        setColour (juce::AlertWindow::backgroundColourId, P.card); setColour (juce::AlertWindow::textColourId, P.ink); setColour (juce::AlertWindow::outlineColourId, P.line);
        setColour (juce::ListBox::backgroundColourId, P.panel); setColour (juce::DirectoryContentsDisplayComponent::textColourId, P.ink);
        setColour (juce::FileTreeComponent::backgroundColourId, P.panel);
        if (P.glass)
        {   // GHOST: the popup highlight is the violet glass (white text), the slider value boxes are recessed glass (drawLabel)
            setColour (juce::PopupMenu::highlightedBackgroundColourId, ghosty::glass::violetDeep()); setColour (juce::PopupMenu::highlightedTextColourId, P.onAccent);
            setColour (juce::ComboBox::arrowColourId, ghosty::glass::violetDeep());
            setColour (juce::Slider::thumbColourId, ghosty::glass::violet());
        }
        juce::ignoreUnused (sizeof (L));
    }
    static juce::Font uiFont (float h, bool bold = true) { return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain)); }
    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return uiFont (juce::jmin (12.5f, (float) h * 0.48f)); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return uiFont (12.5f); }
    juce::Font getLabelFont (juce::Label& l) override { return l.getFont(); }
    juce::Font getPopupMenuFont() override { return uiFont (14.0f, false); }
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        if (P.glass) { ghosty::glass::pill (g, b.getLocalBounds().toFloat(), b.getToggleState(), over, down, b.isEnabled()); return; }
        auto r = b.getLocalBounds().toFloat().reduced (1.0f);
        const bool on = b.getToggleState();
        g.setColour (on ? P.wave : (over ? P.cell.brighter (P.light ? 0.04f : 0.12f) : P.cell));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (on ? P.wave.brighter (0.2f) : (down ? P.marker : P.line));
        g.drawRoundedRectangle (r, 6.0f, down ? 1.6f : 1.0f);
        if (! b.isEnabled()) { g.setColour (P.window.withAlpha (0.35f)); g.fillRoundedRectangle (r, 6.0f); }
    }
    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        g.setFont (getTextButtonFont (b, b.getHeight()));
        if (P.glass)
        {
            auto c = b.getToggleState() ? P.onAccent : P.ink;
            if (! b.isEnabled()) c = glassDisabledInk();
            g.setColour (c);
            g.drawFittedText (b.getButtonText(), ghosty::glass::pillBody (b.getLocalBounds().toFloat()).toNearestInt().reduced (4, 0), juce::Justification::centred, 1, 0.85f);
            return;
        }
        auto c = b.getToggleState() ? P.onAccent : P.ink;
        if (! b.isEnabled()) c = ghosty::themes::readableOn (P.dim, P.cell, 3.0);
        g.setColour (c);
        g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (4, 1), juce::Justification::centred, 1, 0.85f);
    }
    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider& s) override
    {
        if (P.glass)
        {
            const double mid = s.getMinimum() < 0 && s.getMaximum() > 0 ? s.valueToProportionOfLength (0.0) : 0.0;
            const float d = juce::jlimit (10.0f, 18.0f, (float) h - 4.0f);
            auto tr = juce::Rectangle<float> ((float) x, (float) y + (float) h * 0.5f - 3.5f, (float) w, 7.0f);
            ghosty::glass::track (g, tr, (float) x + (float) mid * (float) w, pos, s.isEnabled());
            ghosty::glass::thumb (g, { juce::jlimit ((float) x + d * 0.5f - 2.0f, (float) (x + w) - d * 0.5f + 2.0f, pos), (float) y + (float) h * 0.5f }, d);
            return;
        }
        auto r = juce::Rectangle<float> ((float) x, (float) y + (float) h * 0.5f - 3.0f, (float) w, 6.0f);
        g.setColour (P.cell); g.fillRoundedRectangle (r, 3.0f);
        g.setColour (P.line); g.drawRoundedRectangle (r, 3.0f, 1.0f);
        const double mid = s.getMinimum() < 0 && s.getMaximum() > 0 ? s.valueToProportionOfLength (0.0) : 0.0;
        const float x0 = (float) x + (float) mid * (float) w;
        g.setColour (s.isEnabled() ? P.wave : P.dim); g.fillRect (juce::Rectangle<float>::leftTopRightBottom (std::min (x0, pos), r.getY() + 1, std::max (x0, pos), r.getBottom() - 1));
        g.setColour (P.marker); g.fillRoundedRectangle (pos - 4.0f, (float) y + (float) h * 0.5f - 8.0f, 8.0f, 16.0f, 3.0f);
    }
    // ---- GHOST only (every other look takes the LookAndFeel_V4 path unchanged)
    juce::Colour glassDisabledInk() const { return ghosty::themes::readableOn (P.dim.withAlpha (1.0f), juce::Colour (0xffe9e5f4), 3.0); }
    void drawComboBox (juce::Graphics& g, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox& c) override
    {
        if (! P.glass) { juce::LookAndFeel_V4::drawComboBox (g, w, h, down, bx, by, bw, bh, c); return; }
        const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h);
        ghosty::glass::pill (g, r, false, c.isMouseOver (true), down, c.isEnabled());
        const auto body = ghosty::glass::pillBody (r);
        juce::Path a; const float ax = body.getRight() - 14.0f, ay = body.getCentreY();
        a.startNewSubPath (ax - 4.0f, ay - 2.0f); a.lineTo (ax, ay + 2.5f); a.lineTo (ax + 4.0f, ay - 2.0f);
        g.setColour (c.isEnabled() ? ghosty::glass::violetDeep() : glassDisabledInk());
        g.strokePath (a, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    void positionComboBoxText (juce::ComboBox& c, juce::Label& l) override
    {
        if (! P.glass) { juce::LookAndFeel_V4::positionComboBoxText (c, l); return; }
        l.setBounds (8, 0, c.getWidth() - 26, c.getHeight() - 2);
        l.setFont (getComboBoxFont (c));
    }
    void drawLabel (juce::Graphics& g, juce::Label& l) override
    {
        if (! P.glass || dynamic_cast<juce::Slider*> (l.getParentComponent()) == nullptr) { juce::LookAndFeel_V4::drawLabel (g, l); return; }
        // a slider's value box: recessed glass
        const auto r = l.getLocalBounds().toFloat().reduced (1.0f, 2.0f);
        ghosty::glass::recessed (g, r);
        if (! l.isBeingEdited())
        {
            g.setColour (l.isEnabled() ? P.ink : glassDisabledInk()); g.setFont (uiFont (12.0f));
            g.drawFittedText (l.getText(), r.toNearestInt().reduced (4, 0), juce::Justification::centred, 1, 0.8f);
        }
    }
    void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override
    {
        if (! P.glass) { juce::LookAndFeel_V4::drawPopupMenuBackground (g, w, h); return; }
        g.fillAll (P.card);
        ghosty::glass::card (g, juce::Rectangle<float> (0, 0, (float) w, (float) h), 6.0f);
    }
    void drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h) override
    {
        if (! P.glass) { juce::LookAndFeel_V4::drawTooltip (g, text, w, h); return; }
        const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h);
        g.fillAll (P.card);
        ghosty::glass::card (g, r, 6.0f);
        juce::AttributedString s; s.setJustification (juce::Justification::centred);
        s.append (text, juce::Font (juce::FontOptions (13.0f, juce::Font::bold)), P.ink);
        juce::TextLayout tl; tl.createLayoutWithBalancedLineLengths (s, 400.0f);
        tl.draw (g, r);
    }
};
} // namespace gslice::ui
