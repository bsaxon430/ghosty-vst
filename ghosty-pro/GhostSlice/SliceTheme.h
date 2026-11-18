#pragma once
// Ghost Slice look: the three house themes (Shared/GhostThemes.h: BLACK / PURPLE / LIGHT) with the editor's own roles on top:
// graphite surfaces (BLACK), purple waveform, cyan markers, violet for pitch, amber timing warnings. Every role colour is made
// readable on the surface it is drawn on (>= 3:1 for marks, >= 4.5:1 for text); SliceTests checks it in all three themes.
// GHOST (Slice-only 4th look): palette (Theme::Light, true) = liquid glass on the LIGHT base (Shared/GhostGlass.h); the LnF draws glass only when P.glass.
#include <juce_gui_basics/juce_gui_basics.h>
#include "GhostThemes.h"
#include "GhostGlass.h"
#include "GhostSliceFonts.h"

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
        case Theme::Purple:   // THE look (owner 2026-11-02, "just this color"): deep navy / indigo, violet -> cyan, lilac glass lines
            p.window = C (0xff0b0a1f); p.header = C (0xff121031); p.panel = C (0xff17153a); p.card = C (0xff1e1b48); p.cell = C (0xff221f4f);
            p.line = C (0xff4a4385); p.ink = C (0xfff5f3ff); p.dim = C (0xffa9a3cc); p.onAccent = C (0xff0b0a1f);
            p.waveBg = C (0xff0e0c26); p.waveBgAlt = C (0xff141232);
            p.wave = C (0xffa855f7); p.marker = C (0xff22d3ee); p.pitch = C (0xffc084fc); p.warn = C (0xfffbbf24);
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

// ---------------------------------------------------------------- the window's skin (the 2026-11-02 one-screen design)
// One layout for the four looks; only these colours change. PURPLE is the reference picture's palette.
struct Skin
{
    int look = 1; bool light = false, glass = false;
    juce::Colour bg, glow1, glow2, glow3;               // the window + its soft vignette glows (bottom-left, top-left, right)
    juce::Colour panelTop, panelBot, panelLine, panelHi;// glass panels
    juce::Colour a, a2, b, b2;                          // violet, lilac, cyan, sky
    juce::Colour ink, dim, faint;                       // text, captions, quiet marks
    juce::Colour tile, tileLine, selTop, selBot, selLine;
    juce::Colour well;                                  // recessed areas (the waveform, the overview, the scale box)
    juce::Colour knobFace, knobFace2, knobRing;
    juce::Colour keyW1, keyW2, keyB1, keyB2, keyLine;
    juce::Colour genA, genM, genB, genInk;              // the GENERATE pill (left, middle, right, text)
    juce::Colour good, bad, star;
    juce::Colour at (float t) const { return t < 0.5f ? a.interpolatedWith (a2.interpolatedWith (b2, 0.5f), t * 2.0f) : a2.interpolatedWith (b2, 0.5f).interpolatedWith (b, (t - 0.5f) * 2.0f); }
};

inline Skin skinFor (int look)
{
    using C = juce::Colour; Skin k; k.look = look;
    switch (look)
    {
        case 0:   // BLACK: graphite glass, the same neon
            k.bg = C (0xff0b0b0e); k.glow1 = C (0x55302250); k.glow2 = C (0x33262230); k.glow3 = C (0x3010303a);
            k.panelTop = C (0xcc1d1d24); k.panelBot = C (0xdd131317); k.panelLine = C (0x26ffffff); k.panelHi = C (0x0cffffff);
            k.a = C (0xffa77bff); k.a2 = C (0xffc4a6ff); k.b = C (0xff22e6ff); k.b2 = C (0xff4cc9ff);
            k.ink = C (0xffeeebf5); k.dim = C (0xffa6a2b5); k.faint = C (0xff6b6878);
            k.tile = C (0xcc222229); k.tileLine = C (0x1fffffff); k.selTop = C (0xff3d2f6a); k.selBot = C (0xff261f45); k.selLine = C (0xffb594ff);
            k.well = C (0x990a0a0c); k.knobFace = C (0xff26262c); k.knobFace2 = C (0xff111114); k.knobRing = C (0xff2c2c34);
            k.keyW1 = C (0xffd3d1da); k.keyW2 = C (0xfff0eff4); k.keyB1 = C (0xff0d0d10); k.keyB2 = C (0xff2a2a31); k.keyLine = C (0xff6a6878);
            k.genA = C (0xffc79bff); k.genM = C (0xffa9b4ff); k.genB = C (0xff5fe9ff); k.genInk = C (0xff121018);
            break;
        case 2:   // LIGHT: lilac paper, deep violet -> teal
            k.light = true;
            k.bg = C (0xffece8f3); k.glow1 = C (0x80d6c4ff); k.glow2 = C (0x60f3e6ff); k.glow3 = C (0x60c4eaf5);
            k.panelTop = C (0xccffffff); k.panelBot = C (0xb8f6f3fb); k.panelLine = C (0x667c6bb8); k.panelHi = C (0x80ffffff);
            k.a = C (0xff7c3aed); k.a2 = C (0xff9d5cf5); k.b = C (0xff0891b2); k.b2 = C (0xff0284c7);
            k.ink = C (0xff1e1838); k.dim = C (0xff544c70); k.faint = C (0xff9a94b2);
            k.tile = C (0xd9ffffff); k.tileLine = C (0x4d7c6bb8); k.selTop = C (0xffece2ff); k.selBot = C (0xffdccafd); k.selLine = C (0xff7c3aed);
            k.well = C (0xffe7e2f0); k.knobFace = C (0xffffffff); k.knobFace2 = C (0xffe2dcee); k.knobRing = C (0xffd5cde6);
            k.keyW1 = C (0xffffffff); k.keyW2 = C (0xfff2eff8); k.keyB1 = C (0xff2a2540); k.keyB2 = C (0xff4a4466); k.keyLine = C (0xffb3abc9);
            k.genA = C (0xff8b45f0); k.genM = C (0xff6a5cf0); k.genB = C (0xff0e9fc2); k.genInk = C (0xffffffff);
            break;
        case 3:   // GHOST: liquid glass on the aurora
            k.light = true; k.glass = true;
            k.bg = ghosty::glass::auroraTop(); k.glow1 = C (0x00000000); k.glow2 = C (0x00000000); k.glow3 = C (0x00000000);
            k.panelTop = C (0x9effffff); k.panelBot = C (0x70ffffff); k.panelLine = C (0xccffffff); k.panelHi = C (0xb0ffffff);
            k.a = C (0xff7c4df0); k.a2 = C (0xff9b7bf7); k.b = C (0xff0fb39b); k.b2 = C (0xff3aa6e8);
            k.ink = ghosty::glass::ink(); k.dim = ghosty::glass::ink2(); k.faint = C (0xff8f88b0);
            k.tile = C (0x8cffffff); k.tileLine = C (0xccffffff); k.selTop = C (0xf2f1ebff); k.selBot = C (0xe6e2d8ff); k.selLine = ghosty::glass::violet();
            k.well = C (0x80ffffff); k.knobFace = C (0xffffffff); k.knobFace2 = C (0xffe6e1f4); k.knobRing = C (0xb0ffffff);
            k.keyW1 = C (0xffffffff); k.keyW2 = C (0xfff4f1fb); k.keyB1 = C (0xff2a2250); k.keyB2 = C (0xff4a4280); k.keyLine = C (0xffb8afd8);
            k.genA = C (0xff8b5cf6); k.genM = C (0xff6d6cf2); k.genB = C (0xff14b8a6); k.genInk = C (0xffffffff);
            break;
        default:  // PURPLE: the reference picture
            k.bg = C (0xff0b0a1f); k.glow1 = C (0x8c4c1d95); k.glow2 = C (0x663b1470); k.glow3 = C (0x5a1e3a8a);
            k.panelTop = C (0x8c241e52); k.panelBot = C (0x99120f30); k.panelLine = C (0x5c9d8cff); k.panelHi = C (0x14ffffff);
            k.a = C (0xffa855f7); k.a2 = C (0xffc084fc); k.b = C (0xff22d3ee); k.b2 = C (0xff38bdf8);
            k.ink = C (0xfff5f3ff); k.dim = C (0xffa9a3cc); k.faint = C (0xff6f6996);
            k.tile = C (0xb31f1b45); k.tileLine = C (0x667c6fc0); k.selTop = C (0xe67c55e8); k.selBot = C (0xd0482c9e); k.selLine = C (0xffd8b4fe);
            k.well = C (0x73080716); k.knobFace = C (0xff221e44); k.knobFace2 = C (0xff0c0a1e); k.knobRing = C (0xff2c2756);
            k.keyW1 = C (0xffc6bfdc); k.keyW2 = C (0xfff3f0fb); k.keyB1 = C (0xff12102a); k.keyB2 = C (0xff2c2848); k.keyLine = C (0xff8d86ab);
            k.genA = C (0xffef78f6); k.genM = C (0xffa99dff); k.genB = C (0xff4fe0f6); k.genInk = C (0xff17124c);
            break;
    }
    k.good = k.light ? C (0xff0a8a4a) : C (0xff34d399); k.bad = k.light ? C (0xffd61f45) : C (0xffff5a6e); k.star = k.light ? C (0xffd99100) : C (0xfffbbf24);
    return k;
}

// the window's font: Poppins (SIL OFL 1.1, GhostSlice/fonts), embedded
enum class Weight { Medium, SemiBold, Bold };
inline juce::Typeface::Ptr poppins (Weight w)
{
    static juce::Typeface::Ptr m = juce::Typeface::createSystemTypefaceFor (GhostSliceFonts::PoppinsMedium_ttf, (size_t) GhostSliceFonts::PoppinsMedium_ttfSize);
    static juce::Typeface::Ptr s = juce::Typeface::createSystemTypefaceFor (GhostSliceFonts::PoppinsSemiBold_ttf, (size_t) GhostSliceFonts::PoppinsSemiBold_ttfSize);
    static juce::Typeface::Ptr b = juce::Typeface::createSystemTypefaceFor (GhostSliceFonts::PoppinsBold_ttf, (size_t) GhostSliceFonts::PoppinsBold_ttfSize);
    return w == Weight::Medium ? m : w == Weight::Bold ? b : s;
}
// a font by its CAP height in pixels (Poppins: cap height 0.7 em, JUCE height 1.4 em), tracking = extra kerning (0.1 = 10 %)
inline juce::Font capFont (float capPx, Weight w = Weight::SemiBold, float tracking = 0.0f)
{
    return juce::Font (juce::FontOptions (poppins (w)).withHeight (capPx * 2.0f).withKerningFactor (tracking));
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
    static juce::Font uiFont (float h, bool bold = true) { return juce::Font (juce::FontOptions (poppins (bold ? Weight::SemiBold : Weight::Medium)).withHeight (h * 1.12f)); }
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
