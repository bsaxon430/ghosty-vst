#include "ArpEditor.h"
#include "UrbanMoods.h"

using namespace arp;
namespace arpui
{
// the lane tools' tooltips (the CHORDS lane relabels SIMPLER / BUSIER / NEW and sets its own; any other lane gets these back)
static const char* const kToolTip[] { "Vary a little (one change, everywhere it repeats)", "Vary more (a few changes + one rhythm change)", "New rhythm, same notes", "New notes, same rhythm", "Fewer notes", "More notes",
                                      "Octave down", "Octave up", "A brand-new take for this lane: every press gives a different one (locked bars / notes stay)" };
static const char* const kNewChordsTip = "NEW CHORDS: a new chord progression (the other lanes keep their rhythm)";

static constexpr int kW = 1240, kH = 820;
static juce::Font F (float h, bool b = true) { return ghosty::font (h, b); }

// ================================================================= widgets
void Btn::paint (juce::Graphics& g)
{
    const auto& P = th.p; auto r = getLocalBounds().toFloat().reduced (1.0f);
    const Colour c = tint ? tint() : P.a1;
    const bool over = isMouseOver() && isEnabled();
    const float rad = big ? 12.0f : 8.0f;
    if (P.glass)
    {   // GHOST: a glass pill; on = the tint as glass
        namespace gg = ghosty::glass;
        auto pr = r.reduced (0.5f, 0.0f).withTrimmedBottom (1.5f); const float pd = juce::jmin (pr.getHeight() * 0.5f, big ? 16.0f : 12.0f);
        glassui::sprites().draw (g, pr, pd, on ? (over ? glassui::PillOnOver : glassui::PillOn) : (over ? glassui::PillOffOver : glassui::PillOff), on ? c.withAlpha (1.0f) : Colours::white);
        if (! on)
        {
            g.setColour (c.withAlpha (isEnabled() ? (over ? 0.8f : 0.3f) : 0.15f));
            if (dashed) { juce::Path pp; pp.addRoundedRectangle (pr, pd); juce::Path d; const float dl[] { 4, 3 }; juce::PathStrokeType (1.2f).createDashedStroke (d, pp, dl, 2); g.fillPath (d); }
            else g.drawRoundedRectangle (pr.reduced (0.5f), pd, 1.0f);
        }
        if (! isEnabled()) { g.setColour (Colours::white.withAlpha (0.28f)); g.fillRoundedRectangle (pr, pd); }
    }
    else if (on) { g.setColour (c.withAlpha (0.25f)); g.fillRoundedRectangle (r.expanded (1.5f), rad + 1); g.setColour (c); g.fillRoundedRectangle (r, rad); }
    else
    {
        g.setColour (P.card2.withAlpha (isEnabled() ? 1.0f : 0.5f)); g.fillRoundedRectangle (r, rad);
        g.setColour (c.withAlpha (isEnabled() ? (over ? 0.95f : 0.55f) : 0.2f));
        if (dashed) { juce::Path p; p.addRoundedRectangle (r, rad); juce::Path d; const float dl[] { 4, 3 }; juce::PathStrokeType (1.2f).createDashedStroke (d, p, dl, 2); g.fillPath (d); }
        else g.drawRoundedRectangle (r, rad, 1.2f);
    }
    const Colour txt = on ? P.textOn (P.card.overlaidWith (c)) : (isEnabled() ? P.ink : P.dim.withAlpha (P.glass ? 0.75f : 0.5f));
    g.setColour (txt); g.setFont (F (big ? 17.0f : juce::jmin (12.0f, (float) getHeight() * 0.42f)));
    auto tb = getLocalBounds().reduced (4, 1);
    if (icon > 0)
    {   // 1 undo / 2 redo (curved arrows), 3 save (disk), 4 load (folder)
        const float ih = juce::jmin (18.0f, (float) getHeight() * 0.55f), ix = (float) tb.getX() + 6, iy = (float) getHeight() / 2 - ih / 2;
        juce::Path p;
        if (icon <= 2)
        {
            const bool redo = icon == 2; const float cx = ix + ih / 2, cy = iy + ih * 0.58f, rr = ih * 0.38f;
            p.addCentredArc (cx, cy, rr, rr, 0, redo ? -juce::MathConstants<float>::pi * 0.75f : juce::MathConstants<float>::pi * 0.75f, redo ? juce::MathConstants<float>::pi * 0.5f : -juce::MathConstants<float>::pi * 0.5f, true);
            g.strokePath (p, stroke (2.0f));
            const float hx = redo ? cx + rr * 0.72f : cx - rr * 0.72f, hy0 = cy - rr * 0.72f; juce::Path a;
            a.addTriangle (hx - (redo ? -4.5f : 4.5f), hy0 - 1.0f, hx + (redo ? -3.0f : 3.0f), hy0 - 5.5f, hx + (redo ? -1.0f : 1.0f), hy0 + 4.5f); g.fillPath (a);
        }
        else if (icon >= 5)
        {   // speaker (5 playing: waves, 6 muted: a cross)
            const float s = ih * 0.8f, y0 = (float) getHeight() / 2; juce::Path sp;
            sp.startNewSubPath (ix, y0 - s * 0.18f); sp.lineTo (ix + s * 0.25f, y0 - s * 0.18f); sp.lineTo (ix + s * 0.55f, y0 - s * 0.45f); sp.lineTo (ix + s * 0.55f, y0 + s * 0.45f); sp.lineTo (ix + s * 0.25f, y0 + s * 0.18f); sp.lineTo (ix, y0 + s * 0.18f); sp.closeSubPath();
            g.fillPath (sp);
            if (icon == 5) { juce::Path w; w.addCentredArc (ix + s * 0.55f, y0, s * 0.3f, s * 0.3f, 0, 0.6f, 2.5f, true); w.addCentredArc (ix + s * 0.55f, y0, s * 0.55f, s * 0.55f, 0, 0.6f, 2.5f, true); g.strokePath (w, stroke (1.3f)); }
            else { g.drawLine (ix + s * 0.75f, y0 - s * 0.25f, ix + s * 1.15f, y0 + s * 0.25f, 1.6f); g.drawLine (ix + s * 0.75f, y0 + s * 0.25f, ix + s * 1.15f, y0 - s * 0.25f, 1.6f); }
            tb.removeFromLeft ((int) ih + 6); g.drawFittedText (text, tb, juce::Justification::centred, 1, 0.7f); return;
        }
        else if (icon == 3) { p.addRoundedRectangle (ix, iy, ih, ih, 2.5f); g.strokePath (p, stroke (1.7f)); g.fillRect (ix + ih * 0.25f, iy + 1.5f, ih * 0.5f, ih * 0.3f); g.fillRect (ix + ih * 0.22f, iy + ih * 0.6f, ih * 0.56f, ih * 0.28f); }
        else { p.startNewSubPath (ix, iy + ih * 0.2f); p.lineTo (ix + ih * 0.4f, iy + ih * 0.2f); p.lineTo (ix + ih * 0.5f, iy + ih * 0.32f); p.lineTo (ix + ih, iy + ih * 0.32f); p.lineTo (ix + ih, iy + ih * 0.92f); p.lineTo (ix, iy + ih * 0.92f); p.closeSubPath(); g.strokePath (p, stroke (1.7f)); }
        tb.removeFromLeft ((int) ih + 6);
    }
    g.drawFittedText (text, tb, juce::Justification::centred, 2, 0.8f);
}
void Btn::mouseUp (const juce::MouseEvent& e)
{
    if (! isEnabled() || ! getLocalBounds().contains (e.getPosition())) return;
    if (e.mods.isPopupMenu()) { if (onRight) onRight(); return; }
    if (toggle) on = ! on;
    repaint();
    if (onClick) onClick();
}
void Seg::paint (juce::Graphics& g)
{
    const auto& P = th.p; const int n = juce::jmax (1, items.size()); const float w = (float) getWidth() / (float) n;
    const Colour c = tint ? tint() : P.a1;
    if (P.glass) glassui::sprites().draw (g, getLocalBounds().toFloat().reduced (0.5f), juce::jmin (11.0f, (float) getHeight() * 0.5f), glassui::Well);   // GHOST: a glass well, the pick = tinted glass
    for (int i = 0; i < n; ++i)
    {
        auto r = juce::Rectangle<float> ((float) i * w, 0, w, (float) getHeight()).reduced (1.5f, 1.0f);
        if (P.glass) { if (i == sel) glassui::sprites().draw (g, r.reduced (0.5f, 0.5f), juce::jmin (10.0f, r.getHeight() * 0.5f), glassui::Sel, c.withAlpha (1.0f));
                       else if (i > 0 && i - 1 != sel) { g.setColour (P.line.withAlpha (0.6f)); g.fillRect (r.getX() - 1.5f, r.getY() + 4, 1.0f, r.getHeight() - 8); } }
        else if (i == sel) { g.setColour (c); g.fillRoundedRectangle (r, 7); }
        else { g.setColour (P.card2.withAlpha (isEnabled() ? 1.0f : 0.5f)); g.fillRoundedRectangle (r, 7); g.setColour (c.withAlpha (isEnabled() ? 0.45f : 0.2f)); g.drawRoundedRectangle (r, 7, 1.0f); }
        g.setColour (i == sel ? P.textOn (P.card.overlaidWith (c)) : (isEnabled() ? P.ink : P.dim));
        g.setFont (F (fontH)); g.drawFittedText (items[i], r.toNearestInt().reduced (2, 0), juce::Justification::centred, 1, 0.7f);
    }
}
void Seg::mouseUp (const juce::MouseEvent& e)
{
    if (! isEnabled() || ! getLocalBounds().contains (e.getPosition())) return;
    const int i = juce::jlimit (0, items.size() - 1, (int) (e.position.x / ((float) getWidth() / (float) juce::jmax (1, items.size()))));
    sel = i; repaint(); if (onChange) onChange (i);
}
void Stepper::paint (juce::Graphics& g)
{
    const auto& P = th.p; auto r = getLocalBounds().toFloat().reduced (1.5f, 1.0f);
    const Colour c = tint ? tint() : P.a1; const bool on = value != reset;
    Colour fill = P.card2;   // (opaque; the tint as strong as the label still reads on it)
    if (on) for (float a = 0.28f; a > 0.0f; a -= 0.04f) { fill = P.card2.overlaidWith (c.withAlpha (a)); if (ghosty::themes::contrast (P.textOn (fill), fill) >= 4.6) break; }
    if (P.glass) { const float pd = juce::jmin (11.0f, r.getHeight() * 0.5f); glassui::sprites().draw (g, r, pd, on ? glassui::StepOn : glassui::PillOff, on ? fill.withAlpha (1.0f) : Colours::white);
                   g.setColour (c.withAlpha (on ? 0.9f : 0.35f)); g.drawRoundedRectangle (r.reduced (0.5f), pd, 1.0f); }
    else { g.setColour (fill); g.fillRoundedRectangle (r, 7); g.setColour (c.withAlpha (on ? 0.9f : 0.45f)); g.drawRoundedRectangle (r, 7, 1.0f); }
    g.setColour (on ? P.textOn (fill) : P.ink); g.setFont (F (12.0f));
    g.drawText ("-", r.removeFromLeft (16), juce::Justification::centred); g.drawText ("+", r.removeFromRight (16), juce::Justification::centred);
    g.setFont (F (10.5f)); g.drawFittedText (label + " " + (fmt ? fmt (value) : juce::String (value)), r.toNearestInt(), juce::Justification::centred, 1, 0.7f);
}
void Stepper::mouseUp (const juce::MouseEvent& e)
{
    if (! isEnabled() || ! getLocalBounds().contains (e.getPosition()) || e.getNumberOfClicks() > 1) return;
    if (e.mods.isPopupMenu()) { if (onRight) onRight(); return; }
    if (e.position.x < 22) set (value - 1); else if (e.position.x > (float) getWidth() - 22) set (value + 1);
}
void ArpLnF::refresh()
{
    const auto& P = th.p;
    // every stock JUCE widget from the theme tokens first (no LookAndFeel_V4 white-on-dark defaults), then ours
    setColourScheme ({ P.bg1, P.card2, P.card, P.line, P.ink, P.a1, P.textOn (P.a1), P.a1, P.ink });
    setColour (juce::PopupMenu::backgroundColourId, P.card); setColour (juce::PopupMenu::textColourId, P.ink);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, P.a1); setColour (juce::PopupMenu::highlightedTextColourId, P.textOn (P.a1)); setColour (juce::PopupMenu::headerTextColourId, P.dim);
    setColour (juce::ComboBox::backgroundColourId, P.card2); setColour (juce::ComboBox::textColourId, P.ink); setColour (juce::ComboBox::outlineColourId, P.a1.withAlpha (0.5f)); setColour (juce::ComboBox::arrowColourId, P.a1);
    setColour (juce::TextEditor::backgroundColourId, P.card2); setColour (juce::TextEditor::textColourId, P.ink); setColour (juce::TextEditor::outlineColourId, P.a1.withAlpha (0.5f));
    setColour (juce::TextEditor::focusedOutlineColourId, P.a1); setColour (juce::CaretComponent::caretColourId, P.ink); setColour (juce::Label::textColourId, P.dim);
    setColour (juce::Slider::textBoxTextColourId, P.ink); setColour (juce::Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (juce::AlertWindow::backgroundColourId, P.card); setColour (juce::AlertWindow::textColourId, P.ink);
    setColour (juce::TextButton::buttonColourId, P.card2); setColour (juce::TextButton::textColourOffId, P.ink);
    setColour (juce::BubbleComponent::backgroundColourId, P.card); setColour (juce::BubbleComponent::outlineColourId, P.a1);
    setColour (juce::TooltipWindow::backgroundColourId, P.card); setColour (juce::TooltipWindow::textColourId, P.ink); setColour (juce::TooltipWindow::outlineColourId, P.a1);
    if (P.glass) { setColour (juce::PopupMenu::highlightedBackgroundColourId, ghosty::glass::violetDeep()); setColour (juce::PopupMenu::highlightedTextColourId, Colours::white); }
}
// GHOST: glass cards for menus + tooltips (every other look: the LookAndFeel_V4 painting, unchanged)
void ArpLnF::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    if (! th.p.glass) { LookAndFeel_V4::drawPopupMenuBackground (g, w, h); return; }
    g.fillAll (th.p.card); ghosty::glass::card (g, juce::Rectangle<float> (0, 0, (float) w, (float) h), 6.0f);
}
void ArpLnF::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    if (! th.p.glass) { LookAndFeel_V4::drawTooltip (g, text, w, h); return; }
    const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h); g.fillAll (th.p.card); ghosty::glass::card (g, r, 6.0f);
    juce::AttributedString a; a.setJustification (juce::Justification::centred); a.append (text, juce::Font (juce::FontOptions (13.0f, juce::Font::bold)), th.p.ink);
    juce::TextLayout tl; tl.createLayoutWithBalancedLineLengths (a, 400.0f); tl.draw (g, r);
}
void ArpLnF::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    const auto& P = th.p;
    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3);
    const float rad = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f, cx = r.getCentreX(), cy = r.getCentreY();
    if (P.glass)
    {   // GHOST: a glossy pearl knob in a glass ring, the value as a violet -> teal arc
        namespace gg = ghosty::glass; const float ang = a0 + pos * (a1 - a0);
        const auto dome = juce::Rectangle<float> (cx - rad * 0.66f, cy - rad * 0.66f, rad * 1.32f, rad * 1.32f);
        glassui::sprites().draw (g, dome, dome.getWidth() * 0.5f, glassui::Dome);
        juce::Path tr; tr.addCentredArc (cx, cy, rad - 3, rad - 3, 0, a0, a1, true); g.setColour (Colour (0x30443a7a)); g.strokePath (tr, stroke (4.0f));
        if (ang - a0 > 0.01f) { juce::Path v; v.addCentredArc (cx, cy, rad - 3, rad - 3, 0, a0, ang, true); g.setGradientFill (juce::ColourGradient (gg::violet(), cx - rad, cy + rad, gg::teal(), cx + rad, cy - rad, false)); g.strokePath (v, stroke (4.0f)); }
        juce::Path ptr; ptr.startNewSubPath (cx + std::sin (ang) * rad * 0.18f, cy - std::cos (ang) * rad * 0.18f); ptr.lineTo (cx + std::sin (ang) * rad * 0.55f, cy - std::cos (ang) * rad * 0.55f);
        g.setColour (s.isEnabled() ? gg::violetDeep() : P.dim); g.strokePath (ptr, stroke (2.2f));
        return;
    }
    juce::Path tr; tr.addCentredArc (cx, cy, rad - 3, rad - 3, 0, a0, a1, true);
    g.setColour (P.line); g.strokePath (tr, stroke (4.0f));
    const float ang = a0 + pos * (a1 - a0);
    juce::Path v; v.addCentredArc (cx, cy, rad - 3, rad - 3, 0, a0, ang, true);
    g.setGradientFill (juce::ColourGradient (P.a2, cx - rad, cy, P.a1, cx + rad, cy, false)); g.strokePath (v, stroke (4.0f));
    g.setColour (P.card2); g.fillEllipse (cx - rad * 0.6f, cy - rad * 0.6f, rad * 1.2f, rad * 1.2f);
    juce::Path ptr; ptr.startNewSubPath (cx, cy); ptr.lineTo (cx + std::sin (ang) * rad * 0.55f, cy - std::cos (ang) * rad * 0.55f);
    g.setColour (s.isMouseOverOrDragging() ? P.a1 : P.ink); g.strokePath (ptr, stroke (2.2f));
}
void ArpLnF::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider& s)
{
    const auto& P = th.p;
    const float cy = (float) y + (float) h * 0.5f, x0 = (float) x + 4, x1 = (float) (x + w) - 4;
    const Colour c = s.findColour (juce::Slider::trackColourId).getAlpha() > 0 && s.findColour (juce::Slider::trackColourId) != Colour (0xff4a9ad4) ? s.findColour (juce::Slider::trackColourId) : P.a1;
    if (P.glass)
    {   // GHOST: a glass track, the violet -> teal fill, a glossy round thumb
        ghosty::glass::track (g, juce::Rectangle<float> (x0, cy - 3.5f, x1 - x0, 7.0f), x0, juce::jmax (x0, pos), s.isEnabled());
        ghosty::glass::thumb (g, { pos, cy }, 16.0f); return;
    }
    g.setColour (P.line.withAlpha (s.isEnabled() ? 1.0f : 0.4f)); g.fillRoundedRectangle (x0, cy - 3, x1 - x0, 6, 3);
    if (s.isEnabled()) { g.setColour (c); g.fillRoundedRectangle (x0, cy - 3, juce::jmax (0.0f, pos - x0), 6, 3); }
    g.setColour (P.ink.withAlpha (s.isEnabled() ? 1.0f : 0.35f)); g.fillEllipse (pos - 8, cy - 8, 16, 16);
    g.setColour (s.isEnabled() ? c : P.line); g.drawEllipse (pos - 8, cy - 8, 16, 16, 3.0f);
}
Knob::Knob (ThemeRef& t, juce::AudioProcessorValueTreeState& st, const juce::String& id, juce::String lab) : th (t), label (std::move (lab))
{
    addAndMakeVisible (s); s.setPopupDisplayEnabled (true, true, nullptr);
    att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (st, id, s);   // (the attachment shows the parameter's own units)
    if (auto* p = st.getParameter (id)) s.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
    s.onDragStart = [this] { if (onGesture) onGesture(); };
}
void Knob::paint (juce::Graphics& g) { g.setColour (th.p.dim); g.setFont (F (9.5f)); g.drawFittedText (label, getLocalBounds().removeFromBottom (14), juce::Justification::centred, 1, 0.7f); }

// ================================================================= lanes + drag
juce::String ArpEditor::laneName (int l) { static const char* n[] { "CHORDS", "ARP", "MELODY", "BASS" }; return n[juce::jlimit (0, 3, l)]; }
Colour ArpEditor::laneColour (int l) const { const auto& P = th.p; return l == LaneChords ? P.a3 : l == LaneArp ? P.a2 : l == LaneMelody ? P.a1 : P.gold; }

LaneCard::LaneCard (ArpEditor& e, int l) : ed (e), lane (l), mute (e.th, "ON"), drag (e.th, "")
{
    addAndMakeVisible (mute);
    mute.tint = [this] { return ed.proc.idea().lanes[lane].mute ? ed.th.p.bad : ed.laneColour (lane); };
    mute.onClick = [this] { const bool m = ed.proc.idea().lanes[lane].mute; ed.proc.change ([this] (Idea& X) { X.lanes[lane].mute = ! X.lanes[lane].mute; }, true, m ? "unmute" : "mute"); };
    mute.setTooltip ("PLAYING / MUTED: muted lanes are left out of the sound and the MIDI out (your edits stay)");
    addAndMakeVisible (vol); vol.setPopupDisplayEnabled (true, true, nullptr); vol.setDoubleClickReturnValue (true, 0.0);
    volAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (e.proc.apvts, laneVolId (lane), vol);
    vol.onValueChange = [this] { repaint(); };
    vol.onDragStart = [this] { ed.proc.pushUndoPoint (ArpEditor::laneName (lane).toLowerCase() + " volume"); };
    vol.setTooltip ("Volume of this lane (preview sound; also the MIDI velocity, see MORE). Double-click = 0 dB");
    if (lane == LaneChords)
    {
        simpleSeg = std::make_unique<Seg> (e.th, juce::StringArray { "SIMPLE", "RICH" }); addAndMakeVisible (*simpleSeg);
        simpleSeg->tint = [this] { return ed.laneColour (lane); }; simpleSeg->fontH = 11.0f;
        simpleSeg->onChange = [this] (int i) { ed.proc.change ([i] (Idea& X) { setSimple (X, i == 0); }); ed.toast (i == 0 ? "SIMPLE chords: plain major / minor triads in classic loops" : "RICH chords: 7ths, 9ths, sus, slash basses (the full generator)"); };
        simpleSeg->setTooltip ("SIMPLE: classic big-room / progressive triads (Am F C G). RICH: colourful jazzy chords (7ths, 9ths, sus, slash basses).");
    }
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}
void LaneCard::resized()
{
    auto r = getLocalBounds().reduced (8, 6);
    auto top = r.removeFromTop (22);
    mute.setBounds (top.removeFromRight (74));
    if (simpleSeg != nullptr) simpleSeg->setBounds (getWidth() - 132, 34, 120, 22);
    vol.setBounds (getWidth() - 132 + 26, getHeight() - 24, 120 - 26 - 40, 16);
}
void LaneCard::paint (juce::Graphics& g)
{
    const auto& P = ed.th.p; const Colour c = ed.laneColour (lane); const bool sel = ed.lane() == lane;
    auto r = getLocalBounds().toFloat().reduced (1.5f);
    if (P.glass) { g.setOpacity (1.0f); ed.panels.draw (g, r.reduced (2.0f), 12.0f, 0.42f, 0.0f); g.setColour (sel ? c : P.line.withAlpha (0.7f)); g.drawRoundedRectangle (r.reduced (2.0f), 12, sel ? 2.4f : 1.0f); }   // GHOST: a cached glass card
    else {
    g.setColour (P.card); g.fillRoundedRectangle (r, 12);
    g.setColour (sel ? c : P.line); g.drawRoundedRectangle (r, 12, sel ? 2.4f : 1.2f); }
    g.setColour (c); g.fillRoundedRectangle (r.getX() + 1, r.getY() + 10, 4, r.getHeight() - 20, 2);
    const auto& I = ed.proc.idea();
    const bool m = I.lanes[lane].mute;
    mute.text = m ? "MUTED" : "PLAYING"; mute.on = true; mute.icon = m ? 6 : 5;
    if (simpleSeg != nullptr) simpleSeg->sel = I.simple ? 0 : 1;
    g.setColour (sel ? c : P.ink); g.setFont (F (15.0f)); g.drawText (ArpEditor::laneName (lane), 14, 6, 120, 22, juce::Justification::centredLeft);
    juce::String sub;
    if (lane == LaneChords) { juce::StringArray nm; for (size_t i = 0; i < I.chords.size() && i < 4; ++i) nm.add (ed.dispChord (I.chords[i].name)); sub = nm.joinIntoString ("  "); }
    const auto laneMd = [&I] (int l) { return I.laneMood[(size_t) l] >= 0 ? I.laneMood[(size_t) l] : I.mode; };
    const auto emoLane = [&I] (int l) { return isEmo (I) && (I.laneMood[(size_t) l] < 0 || I.laneMood[(size_t) l] == I.mode); };   // (EMO: its own guitar bed + 808 patterns)
    const auto plain = [] (const char* n) { const juce::String s (n); return s.fromFirstOccurrenceOf (" ", false, false); };          // "GA1 rolling 8ths" -> "rolling 8ths"
    if (lane == LaneChords) {}
    else if (lane == LaneArp && emoLane (LaneArp)) sub = "guitar: " + plain (emoArpArchName (emoArchFor (I))) + (I.lanes[lane].edited ? " (edited)" : "");
    else if (lane == LaneArp && urbanMood (laneMd (LaneArp))) sub = juce::String ("rolls in the rests") + (I.lanes[lane].edited ? " (edited)" : "");
    else if (lane == LaneArp) sub = juce::String (I.fig.len) + "-step figure" + (I.lanes[lane].edited ? " (edited)" : "");
    else if (lane == LaneMelody) sub = I.lanes[lane].notes.empty() ? "empty: HARVEST from the arp" : juce::String ((int) I.lanes[lane].notes.size()) + " notes" + (urbanMood (I.mode) ? juce::String ("  ") + flavourName (I.mode, I.flavourUsed) : juce::String());
    else if (lane == LaneBass && emoLane (LaneBass) && I.urbanBassArch >= 0) sub = "808: " + plain (urbanBassArchName (Trap, I.urbanBassArch, kEmo));
    else if (urbanMood (laneMd (LaneBass)) && I.urbanBassArch >= 0) sub = juce::String (laneMd (LaneBass) == Trap ? "808: " : "sub: ") + urbanBassArchName (laneMd (LaneBass), I.urbanBassArch);
    else sub = "roots, steady rhythm";
    g.setColour (P.dim); g.setFont (F (10.0f, false)); g.drawText (sub, 14, 26, getWidth() - (simpleSeg != nullptr ? 150 : 20), 14, juce::Justification::centredLeft, true);
    // mini preview
    auto pr = juce::Rectangle<float> (14, 44, (float) getWidth() - 28, (float) getHeight() - 52);
    if (P.glass) glassui::sprites().draw (g, pr, 8.0f, glassui::Well); else { g.setColour (P.roll.withAlpha (0.85f)); g.fillRoundedRectangle (pr, 6); }
    std::vector<MidiNote> notes;
    if (lane == LaneChords) notes = chordLaneNotes (I); else for (auto& n : I.lanes[lane].notes) notes.push_back ({ n.t, n.d, n.p, n.v });
    if (! notes.empty())
    {
        int lo = 127, hi = 0; for (auto& n : notes) { lo = juce::jmin (lo, n.p); hi = juce::jmax (hi, n.p); }
        const float rg = (float) juce::jmax (6, hi - lo), L = (float) juce::jmax (1, I.total);
        g.setColour (c.withAlpha (m ? 0.3f : 0.9f));
        for (auto& n : notes) g.fillRect (pr.getX() + 3 + (float) n.t / L * (pr.getWidth() - 6), pr.getBottom() - 4 - (float) (n.p - lo) / rg * (pr.getHeight() - 8), juce::jmax (1.5f, (float) n.d / L * (pr.getWidth() - 6)), 2.0f);
    }
    {   // the volume fader's pill
        auto vr = juce::Rectangle<float> ((float) getWidth() - 134, (float) getHeight() - 27, 124, 22);
        if (P.glass) { g.setColour (P.card); g.fillRoundedRectangle (vr, 11.0f); glassui::sprites().draw (g, vr, 11.0f, glassui::Well); } else { g.setColour (P.card); g.fillRoundedRectangle (vr, 8); } g.setColour (c.withAlpha (0.45f)); g.drawRoundedRectangle (vr, P.glass ? 11.0f : 8.0f, 1.0f);
        g.setColour (c); g.setFont (F (9.0f)); g.drawText ("VOL", vr.withWidth (26).toNearestInt(), juce::Justification::centred);
        const float db = (float) vol.getValue(); g.setColour (P.ink); g.drawText (db <= -59.9f ? juce::String ("-inf") : (db > 0.05f ? "+" : "") + juce::String (db, 1), vr.removeFromRight (40).toNearestInt(), juce::Justification::centred);
    }
}
void LaneCard::mouseUp (const juce::MouseEvent& e) { if (getLocalBounds().contains (e.getPosition())) ed.selectLane (lane); }

void DragPill::paint (juce::Graphics& g)
{
    const auto& P = ed.th.p; auto r = getLocalBounds().toFloat().reduced (2);
    const Colour c1 = mask == (1 << LaneChords) ? P.a3 : mask == (1 << LaneArp) ? P.a2 : mask == (1 << LaneMelody) ? P.a1 : mask == (1 << LaneBass) ? P.gold : P.a1;
    const Colour c2 = huge ? P.a2 : (P.dark ? c1.brighter (0.25f) : c1.darker (0.15f));   // (the label must read on both ends)
    if (P.glass) { if (isMouseOver()) { g.setColour (c1.withAlpha (0.3f)); g.fillRoundedRectangle (r.expanded (2), r.getHeight() * 0.5f + 2); }
                   glassui::sprites().draw (g, r.withTrimmedBottom (1.5f), r.getHeight() * 0.5f, glassui::PillOn, c1.withAlpha (1.0f)); }   // GHOST: glossy tinted glass
    else {
    g.setColour (c1.withAlpha (isMouseOver() ? 0.45f : 0.25f)); g.fillRoundedRectangle (r.expanded (2), r.getHeight() * 0.5f + 2);
    g.setGradientFill (juce::ColourGradient (c1, r.getX(), r.getY(), c2, r.getRight(), r.getBottom(), false)); g.fillRoundedRectangle (r, r.getHeight() * 0.5f); }
    const Colour txt = P.textOn (c1);
    // a little "MIDI file + arrow" icon
    const float ih = r.getHeight() * 0.56f, ix = r.getX() + r.getHeight() * 0.38f, iy = r.getCentreY() - ih / 2;
    if (audio)
    {   // a waveform icon + the render status
        juce::Path w; for (int k = 0; k < 7; ++k) { const float hh = ih * (0.25f + 0.75f * std::abs (std::sin ((float) k * 1.3f))); w.addRoundedRectangle (ix + (float) k * ih * 0.2f, r.getCentreY() - hh / 2, ih * 0.11f, hh, 1.0f); }
        g.setColour (txt); g.fillPath (w);
        g.setFont (F (huge ? 15.0f : 12.5f)); g.drawFittedText (ed.renderStatus.isNotEmpty() && huge ? ed.renderStatus : text, r.withTrimmedLeft (ih * 1.75f).toNearestInt(), juce::Justification::centred, 1, 0.7f);
        return;
    }
    juce::Path doc; doc.addRoundedRectangle (ix, iy, ih * 0.72f, ih, 2.0f); g.setColour (txt); g.strokePath (doc, stroke (1.6f));
    for (int k = 0; k < 3; ++k) g.fillRect (ix + ih * 0.14f, iy + ih * (0.28f + 0.2f * (float) k), ih * 0.44f, 1.6f);
    juce::Path ar; const float ax = ix + ih * 0.95f, ay = r.getCentreY(); ar.startNewSubPath (ax, ay); ar.lineTo (ax + ih * 0.5f, ay); ar.startNewSubPath (ax + ih * 0.32f, ay - ih * 0.2f); ar.lineTo (ax + ih * 0.5f, ay); ar.lineTo (ax + ih * 0.32f, ay + ih * 0.2f);
    g.strokePath (ar, stroke (1.8f));
    g.setFont (F (huge ? 16.0f : 12.5f)); g.drawFittedText (text, r.withTrimmedLeft (ih * 1.75f).toNearestInt(), juce::Justification::centred, 1, 0.7f);
}
void DragPill::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 6) return;
    dragging = true;
    const bool wav = audio || e.mods.isAltDown();
    const bool st = mask == 15 && ed.stems();
    if (! ed.proc.exportAllowed (this, wav ? (st ? "arp.pill.audio-stems" : "arp.pill.audio") : (st ? "arp.pill.midi-stems" : "arp.pill.midi"))) { ed.toast ("Drag-out is part of the full version (UNLOCK)"); return; }
    ed.startDragOut (mask, wav, st, this, true);
}
void DragPill::mouseUp (const juce::MouseEvent& e)
{
    if (dragging) { dragging = false; return; }
    if (! getLocalBounds().contains (e.getPosition())) return;
    const bool wavClick = audio || e.mods.isAltDown();
    if (! ed.proc.exportAllowed (this, wavClick ? "arp.pill.click-audio" : "arp.pill.click-midi")) { ed.toast ("Drag-out is part of the full version (UNLOCK)"); return; }
    if (wavClick) { ed.startDragOut (mask, true, mask == 15 && ed.stems(), nullptr, true); return; }   // a click renders ahead, then drag it
    const auto f = ed.proc.writeMidiFile (mask, ed.proc.oneTrackDrag);
    ed.toast ("Drag this handle into your DAW (hold Alt on a lane = AUDIO). Saved a copy: " + f.getFileName());
}

// ================================================================= pattern editor (ARP figure)
juce::Rectangle<float> PatternEd::box (int i, int len) const
{
    const float w = juce::jmin (46.0f, ((float) getWidth() - 60) / (float) juce::jmax (1, len));
    return { 2.0f + (float) i * w, 2.0f, w - 4, (float) getHeight() - 4 };
}
void PatternEd::paint (juce::Graphics& g)
{
    const auto& P = ed.th.p; const auto& I = ed.proc.idea(); const auto& f = I.fig; const Colour c = ed.laneColour (LaneArp);
    if (urbanMood (I.laneMood[(size_t) LaneArp] >= 0 ? I.laneMood[(size_t) LaneArp] : I.mode))
    {   // TRAP / HIP HOP arps are not figures: they answer in the melody's rests (edit the notes in the roll)
        g.setColour (P.card2); g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 7);
        g.setColour (P.dim); g.setFont (F (11.0f, false));
        const bool emo = isEmo (I) && I.laneMood[(size_t) LaneArp] < 0;
        g.drawFittedText (emo ? "the guitar bed: a fingerpicked loop (one chord per felt bar) under the melody: VARY / NEW reshape it, the roll edits notes"
                              : "rolls + arpeggios in the melody's rests: VARY / NEW reshape them, the roll edits notes", getLocalBounds().reduced (10, 4), juce::Justification::centredLeft, 2, 0.8f);
        return;
    }
    const int N = ladderSize (arpParams (I));
    for (int i = 0; i < f.len; ++i)
    {
        const auto& s = f.s[(size_t) i]; auto r = box (i, f.len);
        if (P.glass) glassui::sprites().draw (g, r, 8.0f, glassui::Cell); else { g.setColour (P.card2); g.fillRoundedRectangle (r, 7); }   // GHOST: glass step cells
        if (! s.rest)
        {
            const float lv = s.pedal ? 1.0f : juce::jlimit (0.0f, 1.0f, (float) s.idx / (float) (N + 1));
            auto fill = r.reduced (3); fill = fill.withTop (fill.getBottom() - fill.getHeight() * (0.18f + 0.82f * lv));
            g.setColour ((s.pedal ? P.gold : c).withAlpha (0.35f + (i == 0 ? 0.2f : 0.0f))); g.fillRoundedRectangle (fill, 5);
        }
        g.setColour (i == 0 ? c : P.line); g.drawRoundedRectangle (r, 7, i == 0 ? 1.8f : 1.0f);
        const juce::String num = s.rest ? juce::String ("-") : s.pedal ? juce::String ("P") : juce::String (s.idx);
        juce::String marks; if (s.oct > 0) marks << "+8"; if (s.oct < 0) marks << "-8"; if (s.tie) marks << " ~"; if (s.acc || i == 0) marks << " >";
        {   // the labels sit on small opaque chips (readable over the level fill in every theme)
            const auto nr = r.withTrimmedBottom (10); const float nw = ghosty::textWidth (F (15.0f), num) + 8;
            g.setColour (P.card2); g.fillRoundedRectangle (juce::Rectangle<float> (nw, 18).withCentre (nr.getCentre()), 4);
            if (marks.trim().isNotEmpty()) { const auto mr = r.withTrimmedTop (r.getHeight() - 14); const float mw = ghosty::textWidth (F (9.0f), marks.trim()) + 6;
                                             g.fillRoundedRectangle (juce::Rectangle<float> (mw, 12).withCentre (mr.getCentre()), 3); }
        }
        g.setColour (P.ink); g.setFont (F (15.0f));
        g.drawText (num, r.withTrimmedBottom (10), juce::Justification::centred);
        g.setFont (F (9.0f)); g.setColour (P.dim);
        g.drawText (marks.trim(), r.withTrimmedTop (r.getHeight() - 14), juce::Justification::centred);
    }
    // - / + length
    auto last = box (f.len, f.len);
    g.setColour (P.dim); g.setFont (F (11.0f));
    g.drawText ("-", juce::Rectangle<float> (last.getX() + 2, 4, 20, (float) getHeight() / 2 - 4), juce::Justification::centred);
    g.drawText ("+", juce::Rectangle<float> (last.getX() + 2, (float) getHeight() / 2, 20, (float) getHeight() / 2 - 4), juce::Justification::centred);
    g.setColour (P.line); g.drawRoundedRectangle (juce::Rectangle<float> (last.getX() + 2, 3, 20, (float) getHeight() - 6), 5, 1.0f);
}
void PatternEd::mouseUp (const juce::MouseEvent& e)
{
    const auto& I0 = ed.proc.idea();
    if (urbanMood (I0.laneMood[(size_t) LaneArp] >= 0 ? I0.laneMood[(size_t) LaneArp] : I0.mode)) return; const int len = I0.fig.len, N = ladderSize (arpParams (I0));
    auto apply = [this] (std::function<void (Figure&)> fn) {
        ed.proc.change ([fn] (Idea& X) {
            fn (X.fig);
            X.figEvents.erase (std::remove_if (X.figEvents.begin(), X.figEvents.end(), [] (const FigEvent& fe) { return fe.hasFig; }), X.figEvents.end());   // the bar-5 variant followed the old figure
            rerealizeArp (X);
            X.lanes[LaneArp].edited = false;
        });
    };
    auto last = box (len, len);
    if (e.position.x >= last.getX() && e.position.x <= last.getX() + 24)
    {
        const bool plus = e.position.y > (float) getHeight() / 2;
        apply ([plus] (Figure& f) { if (plus && f.len < 8) { f.s[(size_t) f.len] = f.s[(size_t) f.len - 1]; f.s[(size_t) f.len].acc = false; ++f.len; } else if (! plus && f.len > 3) --f.len; });
        return;
    }
    for (int i = 0; i < len; ++i)
        if (box (i, len).contains (e.position))
        {
            if (e.mods.isPopupMenu())
            {
                juce::PopupMenu m; const auto& s = I0.fig.s[(size_t) i];
                m.addSectionHeader ("Step " + juce::String (i + 1));
                m.addItem (1, "Pedal note (stays put)", true, s.pedal); m.addItem (2, "Rest", true, s.rest); m.addItem (3, "Accent", true, s.acc); m.addItem (4, "Tie (legato)", true, s.tie);
                m.addItem (5, "Octave up", true, s.oct > 0); m.addItem (6, "Octave down", true, s.oct < 0); m.addItem (7, "Plain ladder note");
                m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [apply, i, N] (int r) {
                    if (r <= 0) return;
                    apply ([r, i, N] (Figure& f) { auto& st = f.s[(size_t) i];
                        if (r == 1) { st.pedal = ! st.pedal; st.rest = false; if (st.pedal) st.idx = N; }
                        else if (r == 2) st.rest = ! st.rest; else if (r == 3) st.acc = ! st.acc; else if (r == 4) st.tie = ! st.tie;
                        else if (r == 5) st.oct = st.oct > 0 ? 0 : 1; else if (r == 6) st.oct = st.oct < 0 ? 0 : -1;
                        else { st.pedal = st.rest = st.tie = false; st.oct = 0; } });
                });
                return;
            }
            const bool up = e.position.y < box (i, len).getCentreY();
            apply ([i, up, N] (Figure& f) { auto& st = f.s[(size_t) i]; if (st.rest) { st.rest = false; return; } if (st.pedal) { st.pedal = false; st.idx = N; }
                st.idx = juce::jlimit (1, N + 2, st.idx + (up ? 1 : -1)); });
            return;
        }
}
void PatternEd::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const auto& I0 = ed.proc.idea(); if (urbanMood (I0.laneMood[(size_t) LaneArp] >= 0 ? I0.laneMood[(size_t) LaneArp] : I0.mode)) return; const int len = I0.fig.len, N = ladderSize (arpParams (I0));
    for (int i = 0; i < len; ++i)
        if (box (i, len).contains (e.position))
        {
            const int d = w.deltaY > 0 ? 1 : -1;
            ed.proc.change ([i, d, N] (Idea& X) { auto& st = X.fig.s[(size_t) i]; st.pedal = false; st.rest = false; st.idx = juce::jlimit (1, N + 2, st.idx + d);
                rerealizeArp (X); });
            return;
        }
}

// ================================================================= chord chips
int ChordChips::slotAt (float x) const
{
    const auto& I = ed.proc.idea(); const auto& pr = chordProg (I); const int n = (int) pr.size(); if (n == 0) return -1;
    const int loop = juce::jmax (1, I.H.loopTicks); int t = 0;
    for (int i = 0; i < n; ++i) { const int len = (pr[(size_t) i].len ? pr[(size_t) i].len : 4) * PPQ * juce::jmax (1, I.chordBars); const float x0 = (float) t / (float) loop * (float) getWidth(), x1 = (float) (t + len) / (float) loop * (float) getWidth(); if (x >= x0 && x < x1) return i; t += len; }
    return n - 1;
}
void ChordChips::paint (juce::Graphics& g)
{
    const auto& P = ed.th.p; const auto& I = ed.proc.idea(); const int loop = juce::jmax (1, I.H.loopTicks);
    const double pt = ed.proc.playTick.load();
    for (size_t i = 0; i < I.chords.size(); ++i)
    {
        const auto& c = I.chords[i];
        auto r = juce::Rectangle<float> ((float) c.t / (float) loop * (float) getWidth(), 0, (float) (c.tEnd - c.t) / (float) loop * (float) getWidth(), (float) getHeight()).reduced (3, 2);
        const bool now = pt >= 0 && std::fmod (pt, (double) loop) >= c.t && std::fmod (pt, (double) loop) < c.tEnd;
        if (P.glass) glassui::sprites().draw (g, r, 10.0f, now ? glassui::ChipNow : glassui::Chip); else { g.setColour (P.card2); g.fillRoundedRectangle (r, 9); }
        g.setColour (now ? P.a3 : P.a3.withAlpha (0.45f)); g.drawRoundedRectangle (r, P.glass ? 10.0f : 9.0f, now ? 2.4f : 1.2f);
        const juce::String nm = ed.dispChord (i < I.H.chords.size() ? I.H.chords[i].name : c.name);
        g.setColour (c.chrom ? P.pink : P.ink); g.setFont (F (15.0f)); g.drawFittedText (nm, r.withTrimmedBottom (r.getHeight() * 0.42f).toNearestInt(), juce::Justification::centredBottom, 1, 0.6f);
        g.setColour (P.dim); g.setFont (F (10.0f, false)); g.drawFittedText (juce::String (c.roman) + "  " + juce::String (i < chordProg (I).size() ? altNames()[(size_t) (chordProg (I)[i].alt % 8)] : std::string()), r.withTrimmedTop (r.getHeight() * 0.6f).toNearestInt(), juce::Justification::centredTop, 1, 0.6f);
    }
}
void ChordChips::mouseUp (const juce::MouseEvent& e)
{
    const int i = slotAt (e.position.x); if (i < 0) return;
    if (e.mods.isPopupMenu()) { ed.roll.chordMenu (i); return; }
    ed.proc.change ([i] (Idea& X) { nextAlt (X, i); });
}

// ================================================================= the piano roll
// the notes the roll shows / edits: a lane's notes; CHORDS: the chord lane (EDITED CHORDS: the stored notes + the octave, index-aligned)
static std::vector<Note> rollNotes (const Idea& I, int ln) { return ln == LaneChords ? chordDisplayNotes (I) : I.lanes[ln].notes; }
static int rollShift (const Idea& I, int ln) { return ln == LaneChords ? 12 * I.chordOctUsed : 0; }   // shown pitch - stored pitch
static void editLane (Idea& X, int ln) { if (ln == LaneChords) materializeChords (X); }              // CHORDS: the first edit makes the chords notes
static int laneSpb (const Idea& I, int ln) { return ln == LaneArp ? I.arpSpb : ln == LaneMelody ? I.melSpb : ln == LaneBass ? (I.bassRate == 1 ? 16 : 8) : 16; }
static float segDist (juce::Point<float> p, juce::Point<float> a, juce::Point<float> b)
{
    const auto ab = b - a; const float L2 = ab.x * ab.x + ab.y * ab.y;
    const float u = L2 > 0 ? juce::jlimit (0.0f, 1.0f, ((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / L2) : 0.0f;
    return p.getDistanceFrom (a + ab * u);
}

int Roll::total() const { return juce::jmax (BAR, ed.proc.idea().total); }
void Roll::zoomAround (float x, double factor)
{
    const double frac = juce::jlimit (0.0, 1.0, (double) (x - kx) / juce::jmax (1.0, (double) getWidth() - kx));
    view = zoomAt (view, frac, factor, total()); repaint();
}
void Roll::audition (int pitch)
{
    if (audPitch == pitch) return;
    if (audPitch >= 0) ed.proc.audition (audPitch, false);
    audPitch = pitch;
    if (pitch >= 0) ed.proc.audition (pitch, true);
}

void Roll::fitRows (const std::vector<Note>& notes)
{
    if (frozen || pitchManual) return;
    int l = 127, h = 0; for (auto& n : notes) { l = juce::jmin (l, n.p); h = juce::jmax (h, n.p); }
    if (l > h) { l = 60; h = 79; }
    l -= 3; h += 3; while (h - l < 22) { --l; ++h; }
    lo = juce::jmax (0, l); hi = juce::jmin (127, h);
}
std::vector<juce::Point<float>> Roll::linkPath (const std::vector<Note>& n, int i) const
{
    const auto& a = n[(size_t) i]; const auto& b = n[(size_t) i + 1];
    const float ya = Y (a.p) + rh() / 2, yb = Y (b.p) + rh() / 2, ax1 = X (a.t + a.d), bx0 = X (b.t);
    const float amid = (X (a.t) + juce::jmax (ax1, X (a.t) + 3)) / 2, bmid = (bx0 + juce::jmax (X (b.t + b.d), bx0 + 3)) / 2, xs = juce::jmax (amid, juce::jmin (ax1, bx0 - 6));
    if (a.l.type == 1)
    {
        const float gx = juce::jlimit (X (a.t) + 1, juce::jmax (X (a.t) + 1, bx0 - 1), X (a.t + a.l.w * (b.t - a.t)));
        std::vector<juce::Point<float>> p { { juce::jmin (xs, gx), ya }, { gx, ya } };
        for (int k = 1; k <= 8; ++k) { const float u = k / 8.0f, s = u * u * (3 - 2 * u); p.push_back ({ gx + (bx0 - gx) * u, ya + (yb - ya) * s }); }
        return p;
    }
    if (a.l.type == 2) return { { amid, ya }, { bmid, yb } };
    const float xm = (xs + bx0) / 2;
    return { { xs, ya }, { xm, ya }, { xm, yb }, { bx0, yb } };
}
juce::Point<float> Roll::linkHandle (const std::vector<Note>& n, int i) const
{
    const auto& a = n[(size_t) i]; const auto& b = n[(size_t) i + 1]; const float ya = Y (a.p) + rh() / 2, yb = Y (b.p) + rh() / 2;
    juce::Point<float> h;
    if (a.l.type == 1) h = { juce::jlimit (X (a.t) + 1, juce::jmax (X (a.t) + 1, X (b.t) - 1), X (a.t + a.l.w * (b.t - a.t))), ya };
    else
    {
        const float amid = (X (a.t) + juce::jmax (X (a.t + a.d), X (a.t) + 3)) / 2, bmid = (X (b.t) + juce::jmax (X (b.t + b.d), X (b.t) + 3)) / 2;
        h = { (amid + bmid) / 2, (ya + yb) / 2 };
    }
    // never under a note: lift it above the line
    for (const Note* m : { &a, &b })
    {
        const juce::Rectangle<float> r (X (m->t) - 2, Y (m->p) - 2, juce::jmax (8.0f, X (m->t + m->d) - X (m->t)) + 4, rh() + 4);
        if (r.expanded (7).contains (h)) { h.y = juce::jmin (Y (a.p), Y (b.p)) - 11; break; }
    }
    return h;
}
bool Roll::hitHandle (const std::vector<Note>& n, int i, float x, float y) const
{
    if (i < 0 || i + 1 >= (int) n.size() || ! linkHasAmount (n[(size_t) i].l)) return false;
    return linkHandle (n, i).getDistanceFrom ({ x, y }) <= 11.0f;
}
int Roll::hitLink (const std::vector<Note>& n, float x, float y) const
{
    int best = -1; float bd = 9.5f;
    for (int i = 0; i + 1 < (int) n.size(); ++i)
    {
        if (X (n[(size_t) i + 1].t) < kx - 4 || X (n[(size_t) i].t) > (float) getWidth() + 4) continue;
        const auto p = linkPath (n, i); float d = 1e9f;
        for (size_t k = 1; k < p.size(); ++k) d = juce::jmin (d, segDist ({ x, y }, p[k - 1], p[k]));
        if (linkHasAmount (n[(size_t) i].l)) d = juce::jmin (d, linkHandle (n, i).getDistanceFrom ({ x, y }));
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}
int Roll::hitNote (const std::vector<Note>& n, float x, float y) const
{
    for (int i = (int) n.size() - 1; i >= 0; --i)
    {
        const auto& m = n[(size_t) i]; const float nx = X (m.t), nw = juce::jmax (8.0f, X (m.t + m.d) - nx), ny = Y (m.p);
        if (x >= nx - 1 && x <= nx + nw && y >= ny - 1 && y <= ny + rh() + 1) return i;
    }
    return -1;
}
void Roll::paint (juce::Graphics& g)
{
    const auto& P = ed.th.p; const int ln = ed.lane();
    std::vector<Note> notes, ghost; std::vector<Resolved> chords; std::vector<bool> locks; int total = BAR * 8, spb = 16, loop = BAR * 8;
    std::vector<HChord> hch;
    {
        const juce::ScopedLock sl (ed.proc.ideaLock); const auto& I = ed.proc.idea();
        total = juce::jmax (BAR, I.total); loop = juce::jmax (BAR, I.H.loopTicks); hch = I.H.chords; chords = I.chords; locks = I.lanes[ln].barLocks;
        spb = ln == LaneArp ? I.arpSpb : ln == LaneMelody ? I.melSpb : ln == LaneBass ? (I.bassRate == 1 ? 16 : 8) : 16;
        laneMutedNow = I.lanes[ln].mute;
        notes = rollNotes (I, ln);
        if (ln == LaneMelody) ghost = I.lanes[LaneArp].notes; else if (ln == LaneArp) ghost = I.lanes[LaneMelody].notes;
    }
    view = clampView (view, total);
    if (sel.size() != notes.size()) sel.assign (notes.size(), false);
    fitRows (notes);
    const float W = (float) getWidth(), H = (float) getHeight(); const double vStart = view.start, vEnd = view.start + view.len;
    g.setColour (P.roll); g.fillRoundedRectangle (getLocalBounds().toFloat(), 10);
    juce::Graphics::ScopedSaveState ss (g);
    juce::Path clip; clip.addRoundedRectangle (getLocalBounds().toFloat(), 10); g.reduceClipRegion (clip);
    const int dsh = ed.dispShift (ln);   // TRANSPOSE: the keyboard + rows are labelled with the pitch you hear (the notes stay where they are)
    for (int p = lo; p <= hi; ++p) { const int q = mod12 (p + dsh); const bool blk = (q == 1 || q == 3 || q == 6 || q == 8 || q == 10); g.setColour (blk ? P.rowB : P.rowA); g.fillRect (kx, Y (p), W - kx, rh()); }
    // chord-tone rows
    g.setColour (P.chordRow);
    for (int base = 0; base < total; base += loop)
        for (auto& c : hch)
        {
            const int t0 = base + c.t, t1 = base + c.tEnd; if (t1 <= vStart || t0 >= vEnd) continue;
            const float x0 = juce::jmax (kx, X (t0)), x1 = juce::jmin (W, X (t1));
            for (int p = lo; p <= hi; ++p) if (std::find (c.pcs.begin(), c.pcs.end(), p % 12) != c.pcs.end()) g.fillRect (x0, Y (p), x1 - x0, rh());
        }
    const int b0v = (int) std::floor (vStart / BAR), b1v = (int) std::ceil (vEnd / BAR);
    // locked bars: gold tint + hatching
    for (int b = b0v; b < b1v; ++b) if (b >= 0 && b < (int) locks.size() && locks[(size_t) b])
    {
        const float x0 = X (b * BAR), x1 = X ((b + 1) * BAR);
        g.setColour (P.gold.withAlpha (0.13f)); g.fillRect (x0, hy, x1 - x0, H - hy);
        g.setColour (P.gold.withAlpha (0.10f)); for (float d = -H; d < x1 - x0; d += 14) g.drawLine (x0 + d, H, x0 + d + (H - hy), hy, 1.0f);
        g.setColour (P.gold.withAlpha (0.7f)); g.fillRect (x0, hy, x1 - x0, 2.0f);
    }
    // bar selection
    if (selBar0 >= 0) { const int a = juce::jmin (selBar0, selBar1), b = juce::jmax (selBar0, selBar1); g.setColour (P.cyan.withAlpha (0.12f)); g.fillRect (X (a * BAR), hy, X ((b + 1) * BAR) - X (a * BAR), H - hy); }
    // grid
    const int st = BAR / juce::jmax (1, spb);
    const double pxPerStep = (W - kx) / (view.len / st);
    for (int t = (int) (vStart / st) * st; t <= vEnd; t += st)
    {
        const bool bar = t % BAR == 0, beat = t % PPQ == 0;
        if (! beat && pxPerStep < 5) continue;
        g.setColour (bar ? P.gridB : beat ? P.gridB.withAlpha (0.35f) : P.grid);
        g.fillRect (std::round (X (t)), hy, bar ? 1.5f : 1.0f, H - hy);
    }
    // header
    g.setColour (P.card2); g.fillRect (0.0f, 0.0f, W, hy);
    g.setFont (F (10.5f));
    for (int base = 0; base < total; base += loop)
        for (size_t i = 0; i < hch.size(); ++i)
        {
            const int t0 = base + hch[i].t; if (base + hch[i].tEnd <= vStart || t0 >= vEnd) continue;
            g.setColour (i < chords.size() && chords[i].chrom ? P.pink : P.a3);
            g.drawText (ed.dispChord (hch[i].name), juce::Rectangle<float> (juce::jmax (kx, X (t0)) + 4, 1, 140, hy1 - 1), juce::Justification::centredLeft, true);
        }
    for (int b = b0v; b < b1v; ++b)
    {
        const float x = X (b * BAR); const bool lk = b >= 0 && b < (int) locks.size() && locks[(size_t) b];
        juce::Path pad; const float lx = x + 4, ly = hy1 + 2;   // padlock: click it to lock / unlock the bar
        pad.addRoundedRectangle (lx, ly + 5, 10, 8, 1.5f); juce::Path sh; sh.addCentredArc (lx + 5, ly + 5, 3.4f, 3.6f, 0, -juce::MathConstants<float>::halfPi, lk ? juce::MathConstants<float>::halfPi : juce::MathConstants<float>::halfPi * 0.2f, true);
        g.setColour (lk ? P.gold : P.dim.withAlpha (0.6f)); if (lk) g.fillPath (pad); else g.strokePath (pad, stroke (1.0f)); g.strokePath (sh, stroke (1.3f));
        g.setColour (lk ? P.gold : P.dim); g.drawText (juce::String (b + 1) + (lk ? " LOCKED" : ""), juce::Rectangle<float> (x + 18, hy1, 80, hy2 - hy1), juce::Justification::centredLeft);
        g.setColour (P.gridB); g.fillRect (x, 0.0f, 1.0f, hy);
    }
    {   // which bars are on screen
        const juce::String vis = "BARS " + juce::String ((int) std::floor (vStart / BAR) + 1) + "-" + juce::String (juce::jmax ((int) std::floor (vStart / BAR) + 1, (int) std::ceil (vEnd / BAR - 0.01))) + " OF " + juce::String (total / BAR);
        g.setFont (F (9.5f)); const float w = ghosty::textWidth (F (9.5f), vis) + 14;
        auto r = juce::Rectangle<float> (W - w - 6, hy1 + 2, w, hy2 - hy1 - 4); g.setColour (P.roll.withAlpha (0.85f)); g.fillRoundedRectangle (r, 4); g.setColour (P.dim); g.drawText (vis, r, juce::Justification::centred);
    }
    {   // the TIMELINE strip: its own band, a hint while it is empty
        g.setColour (P.roll.interpolatedWith (P.card2, 0.5f)); g.fillRect (kx, hy2, W - kx, hy - hy2);
        g.setColour (P.line.withAlpha (0.6f)); g.fillRect (kx, hy2, W - kx, 1.0f);
        if (! ed.proc.section().valid()) { g.setColour (P.dim); g.setFont (F (9.0f, false)); g.drawText ("right-click: play from here   |   right-drag: loop   |   drag the loop's MIDI / WAV grip out to export", juce::Rectangle<float> (kx + 8, hy2, W - kx - 16, hy - hy2), juce::Justification::centredLeft, true); }
    }
    g.setColour (P.line); g.fillRect (0.0f, hy - 1, W, 1.0f);
    // ghost notes of the other melodic lane
    g.setColour (P.dim.withAlpha (0.22f));
    for (auto& n : ghost) { if (n.t + n.d < vStart || n.t > vEnd || n.p < lo || n.p > hi) continue; g.fillRoundedRectangle (X (n.t), Y (n.p) + rh() * 0.3f, juce::jmax (2.0f, X (n.t + n.d) - X (n.t) - 1), rh() * 0.4f, 2); }
    // lines between notes (always drawn; brighter on hover, bold when selected)
    const Colour laneC = ed.laneColour (ln);
    denseNow = pxPerStep < 9;
    if (ln != LaneChords)
        for (int i = 0; i + 1 < (int) notes.size(); ++i)
        {
            const auto& a = notes[(size_t) i]; const auto& b = notes[(size_t) i + 1]; if (b.t < vStart - BAR || a.t > vEnd) continue;
            const bool s = i == selLink, hv = i == hoverLink; const auto pts = linkPath (notes, i);
            juce::Path pth; pth.startNewSubPath (pts[0]); for (size_t k = 1; k < pts.size(); ++k) pth.lineTo (pts[k]);
            const Colour c = a.l.type == 1 ? P.pink : a.l.type == 2 ? P.gold : P.cyan;
            if (s || hv) { g.setColour (c.withAlpha (0.22f)); g.strokePath (pth, juce::PathStrokeType (9.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); }
            if (a.l.type == 2)
            {
                const float ya = pts[0].y, yb = pts[1].y, amid = pts[0].x, bmid = pts[1].x, k = (bmid - amid) * 0.5f, t1 = (2 + a.l.f * rh() * 1.6f) / 2;
                juce::Path sh; sh.startNewSubPath (amid, ya - 1.2f); sh.cubicTo (amid + k, ya - 1.2f, bmid - k, yb - t1, bmid, yb - t1); sh.lineTo (bmid, yb + t1); sh.cubicTo (bmid - k, yb + t1, amid + k, ya + 1.2f, amid, ya + 1.2f); sh.closeSubPath();
                g.setColour (c.withAlpha (s ? 0.95f : hv ? 0.85f : 0.65f)); g.fillPath (sh);
            }
            else { g.setColour (c.withAlpha (s ? 1.0f : hv ? 0.9f : a.l.type == 1 ? 0.75f : 0.38f)); g.strokePath (pth, juce::PathStrokeType (s ? 2.6f : hv ? 2.0f : a.l.type == 1 ? 2.0f : 1.2f)); }
        }
    // notes
    for (size_t i = 0; i < notes.size(); ++i)
    {
        const auto& n = notes[i]; if (n.t + n.d < vStart || n.t > vEnd) continue;
        const Link* pl = i > 0 ? &notes[i - 1].l : nullptr; const float fat = pl && pl->type == 2 ? pl->f : 0;
        const float x = X (n.t), w = juce::jmax (3.0f, X (n.t + n.d) - x - 1), hh = juce::jmin (rh() * 1.8f, rh() * 0.78f * (1 + 0.9f * fat)), y = Y (n.p) + rh() / 2 - hh / 2;
        const float vel = ln == LaneChords ? 0.8f : playVel (notes, i);
        g.setColour (laneC.withAlpha ((0.45f + 0.55f * vel) * (laneMutedNow ? 0.45f : 1.0f))); g.fillRoundedRectangle (x, y, w, hh, juce::jmin (4.0f, hh / 2));
        if (i < sel.size() && sel[i]) { g.setColour (P.ink); g.drawRoundedRectangle (x - 1, y - 1, w + 2, hh + 2, 4, 1.6f); }
        if (n.lock) { g.setColour (P.gold); g.fillEllipse (x + 1, y - 3, 5, 5); }
    }
    // handles: ABOVE the notes (GLIDE dot: where the glide starts, drag left / right; FATTEN ball: drag up / down)
    if (ln != LaneChords)
        for (int i = 0; i + 1 < (int) notes.size(); ++i)
        {
            const auto& a = notes[(size_t) i]; if (! linkHasAmount (a.l) || notes[(size_t) i + 1].t < vStart - BAR || a.t > vEnd) continue;
            const bool s = i == selLink, hv = i == hoverLink; const auto h = linkHandle (notes, i); const Colour c = a.l.type == 1 ? P.pink : P.gold;
            if (! s && ! hv) { g.setColour (c.withAlpha (0.9f)); g.fillEllipse (h.x - 3, h.y - 3, 6, 6); continue; }
            const float r = (s && hoverHandle) || (dr.kind == 2 && dr.idx == i) ? 8.0f : s ? 6.5f : 5.0f;
            g.setColour (c.withAlpha (0.28f)); g.fillEllipse (h.x - r - 5, h.y - r - 5, 2 * (r + 5), 2 * (r + 5));
            g.setColour (c); g.fillEllipse (h.x - r, h.y - r, 2 * r, 2 * r);
            g.setColour (P.ink); g.drawEllipse (h.x - r, h.y - r, 2 * r, 2 * r, 2.0f);
            if (s)
            {
                const juce::String lab = a.l.type == 1 ? "glide " + juce::String (juce::roundToInt (a.l.w * 100)) + "%" + (glideHintShown ? "" : "   < drag >")
                                                        : "fat " + juce::String (juce::roundToInt (a.l.f * 100)) + "%" + (glideHintShown ? "" : "   drag up / down");
                g.setFont (F (10.5f)); const float tw = ghosty::textWidth (F (10.5f), lab) + 12;
                auto lr = juce::Rectangle<float> (h.x + r + 6, h.y - 9, tw, 18); if (lr.getRight() > W - 4) lr.setX (h.x - r - 6 - tw);
                g.setColour (P.card); g.fillRoundedRectangle (lr, 5); g.setColour (c); g.drawRoundedRectangle (lr, 5, 1.0f);
                g.setColour (P.ink); g.drawText (lab, lr, juce::Justification::centred);
            }
        }
    // rubber band
    if (dr.kind == 3 && dr.moved) { g.setColour (P.cyan.withAlpha (0.15f)); g.fillRect (dr.band); g.setColour (P.cyan); g.drawRect (dr.band, 1.0f); }
    {   // LOOP SECTION: the brace in the TIMELINE strip (+ handles, its length, the MIDI / WAV drag-out grips) and a tint over the roll
        const auto sc = ed.proc.section();
        if (sc.valid() && sc.end > vStart && sc.start < vEnd)
        {
            const float x0 = juce::jmax (kx, X (sc.start)), x1 = juce::jmin (W, X (sc.end));
            const Colour c = sc.on ? P.cyan : P.dim;
            g.setColour (c.withAlpha (sc.on ? 0.07f : 0.04f)); g.fillRect (x0, hy, x1 - x0, H - hy);
            auto br = juce::Rectangle<float> (x0, hy2 + 1, x1 - x0, hy - hy2 - 2);
            g.setColour (c.withAlpha (sc.on ? 0.9f : 0.45f)); g.fillRoundedRectangle (br, 4);
            g.setColour (c); for (float hx : { X (sc.start), X (sc.end) }) if (hx >= kx && hx <= W) g.fillRect (hx - 2.0f, hy2, 4.0f, H - hy2);
            // the grips: drag them out of the plugin = the section as MIDI / WAV
            for (int k = 0; k < 2; ++k)
            {
                const auto gr = gripRect (k == 1); if (gr.isEmpty()) continue;
                g.setColour (P.card); g.fillRoundedRectangle (gr, 3); g.setColour (P.ink); g.drawRoundedRectangle (gr, 3, 1.0f);
                g.setFont (F (8.5f)); g.drawText (k == 0 ? "MIDI" : (ed.sectionWavRendering() || (ed.proc.section().valid() && ! ed.sectionWavReady() && ed.proc.exportUnlocked()) ? juce::String ("WAV..") : juce::String ("WAV")), gr, juce::Justification::centred);
            }
            const int len = sc.end - sc.start;
            auto bb = [] (int t) { return juce::String (t / BAR + 1) + "." + juce::String ((t % BAR) / PPQ + 1); };
            const juce::String range = bb (sc.start) + "-" + bb (sc.end - 1) + " (" + juce::String (len / BAR) + "." + juce::String ((len % BAR) / PPQ) + ")";
            const juce::String lab = sc.on ? "LOOP ON " + range + " - click LOOP (or press L) to play everything" : "loop off " + range + " (brace kept)";
            g.setFont (F (9.5f));
            const auto gm = gripRect (false);
            auto at = br.reduced (8, 0); if (! gm.isEmpty() && gm.getX() > at.getX() && gm.getX() < at.getRight()) at.setRight (gm.getX() - 4);
            g.setColour (sc.on ? P.textOn (c) : P.ink); g.drawText (lab, at, juce::Justification::centredLeft, true);
        }
    }
    // the PLAY cursor (SPACE starts here): a thin bright line + its bar.beat on the ruler
    {
        const int pc = ed.proc.playCursor.load();
        if (pc >= 0 && pc >= vStart && pc < vEnd)
        {
            const float cx = X (pc); const Colour cc (0xff3ef0c8);
            g.setColour (cc.withAlpha (0.9f)); g.fillRect (cx - 0.75f, hy1, 1.5f, hy2 - hy1); g.fillRect (cx - 0.75f, hy, 1.5f, H - hy);   // (not across the strip's loop label)
            g.fillRect (cx - 3.0f, hy - 3.0f, 6.0f, 3.0f);
            const juce::String lab = juce::String (pc / BAR + 1) + "." + juce::String ((pc % BAR) / PPQ + 1) + ((pc % PPQ) ? "." + juce::String ((pc % PPQ) / (PPQ / 4) + 1) : juce::String());
            auto lr = juce::Rectangle<float> (cx, hy1 + 1, 34, hy2 - hy1 - 2);   // (the bar-number row: never over the loop's label)
            g.setColour (cc); g.fillRoundedRectangle (lr, 3); g.setColour (P.textOn (cc)); g.setFont (F (9.5f)); g.drawText (lab, lr, juce::Justification::centred);
        }
    }
    // playhead
    const double pt = ed.proc.playTick.load();
    if (pt >= 0 && pt >= vStart && pt < vEnd) { g.setColour (P.ink.withAlpha (0.75f)); g.fillRect (X (pt) - 1, hy, 2.0f, H - hy); }
    // a muted lane: say so, with an UNMUTE button (edits are still kept)
    bannerUnmute = {};
    if (laneMutedNow)
    {
        auto br = juce::Rectangle<float> (kx + 8, hy + 6, W - kx - 16, 30);
        const Colour bannerC = ghosty::themes::readableOn (P.bad, P.ink, 4.6);   // red, as dark / light as the text needs
        g.setColour (bannerC); g.fillRoundedRectangle (br, 8);
        bannerUnmute = br.removeFromRight (100).reduced (4);
        g.setColour (P.ink); g.setFont (F (13.0f));
        g.drawText (ArpEditor::laneName (ln) + " is muted, you won't hear these edits.", br.reduced (12, 0), juce::Justification::centredLeft);
        g.setColour (P.card); g.fillRoundedRectangle (bannerUnmute, 6); g.setColour (P.readable (P.bad)); g.setFont (F (12.0f)); g.drawText ("UNMUTE", bannerUnmute, juce::Justification::centred);
    }
    // keys (click = hear it)
    for (int p = lo; p <= hi; ++p)
    {
        const int q = p + dsh, qc = mod12 (q);
        const bool blk = (qc == 1 || qc == 3 || qc == 6 || qc == 8 || qc == 10);
        g.setColour (p == audPitch ? laneC : blk ? P.keyB : P.keyW); g.fillRect (0.0f, Y (p), kx - 2, rh() - 0.5f);
        if (qc == 0 && rh() >= 7) { g.setColour (ghosty::themes::readableOn (P.dim, P.keyW, 4.5)); g.setFont (F (juce::jmin (9.0f, rh()))); g.drawText ("C" + juce::String (q / 12 - 1), juce::Rectangle<float> (2, Y (p), kx - 4, rh()), juce::Justification::centredLeft); }
    }
    g.setColour (P.card2); g.fillRect (0.0f, 0.0f, kx, hy);
    if (! energyStrip().isEmpty())
    {   // the ENERGY lane (GROW / WAVE / JOURNEY or drawn): level, brightness, space and the CC74 / CC11 out. Drag to draw, double-click = the shape's own curve
        const auto& I = ed.proc.idea(); const auto es = energyStrip();
        g.setColour (P.card2.withAlpha (0.75f)); g.fillRect (es);
        const int bars = juce::jmax (1, I.total / BAR);
        for (int b = 0; b < bars; ++b)
        {
            const float x0 = X (b * BAR), x1 = X ((b + 1) * BAR); if (x1 < kx || x0 > (float) getWidth()) continue;
            const double e = drawingE && (size_t) b < drawE.size() ? drawE[(size_t) b] : arpEnergyAt (I, b * BAR);
            const float h = (es.getHeight() - 4) * (float) e;
            g.setColour (P.gold.withAlpha (I.energy.empty() && ! drawingE ? 0.45f : 0.8f)); g.fillRect (juce::jmax (kx, x0) + 1, es.getBottom() - 2 - h, juce::jmax (1.0f, x1 - juce::jmax (kx, x0) - 2), h);
        }
        g.setColour (P.ink.withAlpha (0.85f)); g.setFont (F (9.0f));
        g.drawText (juce::String ("ENERGY ") + (I.energy.empty() ? juce::String (evolveModeName (I.evolveMode)) + "  (drag to draw)" : juce::String ("DRAWN  (double-click = ") + evolveModeName (I.evolveMode) + ")"),
                    juce::Rectangle<float> (kx + 4, es.getY() - 12, 260, 11), juce::Justification::centredLeft);
    }
}

void Roll::unmuteLane()
{
    const int ln = ed.lane();
    ed.proc.change ([ln] (Idea& X) { X.lanes[ln].mute = false; }, true, "unmute " + ArpEditor::laneName (ln).toLowerCase());
    ed.toast (ArpEditor::laneName (ln) + " unmuted", 1400); repaint();
}

void Roll::mouseMove (const juce::MouseEvent& e)
{
    const int ln = ed.lane(); int hl = -1; bool hh = false;
    if (ln != LaneChords && e.position.y > hy && e.position.x > kx)
    {
        const auto& n = ed.proc.idea().lanes[ln].notes;
        if (hitHandle (n, selLink, e.position.x, e.position.y)) { hl = selLink; hh = true; }
        else if (hitNote (n, e.position.x, e.position.y) < 0) hl = hitLink (n, e.position.x, e.position.y);
    }
    if (hl != hoverLink || hh != hoverHandle)
    {
        hoverLink = hl; hoverHandle = hh;
        setTooltip (hh ? "drag: " + juce::String (ed.proc.idea().lanes[ln].notes[(size_t) hl].l.type == 1 ? "where the GLIDE starts (left / right)" : "how much FATTER (up / down)")
                       : hl >= 0 ? "click: select this line, then GLIDE / FATTEN / STEP below (right-click = menu)" : "");
        setMouseCursor (hh ? (ed.proc.idea().lanes[ln].notes[(size_t) hl].l.type == 1 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::UpDownResizeCursor)
                           : hl >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}
void Roll::mouseExit (const juce::MouseEvent&) { if (hoverLink >= 0) { hoverLink = -1; hoverHandle = false; repaint(); } }

void Roll::setCursorAt (float x, float y)
{   // the clicked note's start, else the click snapped down to the roll's grid
    const int ln = ed.lane(); const auto& I = ed.proc.idea();
    if (y >= hy) { const auto ns = rollNotes (I, ln); const int ni = hitNote (ns, x, y); if (ni >= 0) { ed.proc.setPlayCursor (ns[(size_t) ni].t); repaint(); return; } }
    const int st = juce::jmax (1, BAR / juce::jmax (1, laneSpb (I, ln)));
    const int t = juce::jmax (0, (int) std::floor (T (x)));
    ed.proc.setPlayCursor ((t / st) * st); repaint();
}
juce::Rectangle<float> Roll::energyStrip() const
{
    if (ed.lane() != LaneArp || ! arpEvolving (ed.proc.idea())) return {};
    return { kx, (float) getHeight() - 30.0f, (float) getWidth() - kx, 28.0f };
}
bool Roll::energyDraw (const juce::MouseEvent& e)
{   // paint the bar under the mouse (and every bar the drag crossed) to the mouse height
    const auto es = energyStrip(); if (es.isEmpty()) return false;
    const auto& I = ed.proc.idea(); const int bars = juce::jmax (1, I.total / BAR);
    if (! drawingE) { drawingE = true; drawE.clear(); for (int b = 0; b < bars; ++b) drawE.push_back ((float) arpEnergyAt (I, b * BAR)); }
    const float v = juce::jlimit (0.0f, 1.0f, (es.getBottom() - 2 - e.position.y) / (es.getHeight() - 4));
    const int b0 = juce::jlimit (0, bars - 1, (int) (T (e.mouseDownPosition.x) / BAR)), b1 = juce::jlimit (0, bars - 1, (int) (T (e.position.x) / BAR));
    if (e.mouseWasDraggedSinceMouseDown()) { for (int b = juce::jmin (b0, b1); b <= juce::jmax (b0, b1); ++b) if (b == b1) drawE[(size_t) b] = v; }
    else drawE[(size_t) b1] = v;
    repaint();
    return true;
}
void Roll::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    if (energyStrip().contains (e.position) && e.getNumberOfClicks() < 2) { energyDraw (e); return; }
    const int ln = ed.lane(); const float x = e.position.x, y = e.position.y;
    dr = Drag(); dr.x0 = x; dr.y0 = y; dr.vs0 = view.start; dr.lo0 = lo;
    const auto& I = ed.proc.idea();
    if (! bannerUnmute.isEmpty() && bannerUnmute.contains (e.position)) { unmuteLane(); return; }
    if (e.mods.isMiddleButtonDown() || (e.mods.isAltDown() && e.mods.isLeftButtonDown())) { dr.kind = 6; frozen = true; return; }   // hand tool
    if (I.lanes[ln].mute && y > hy && x > kx) ed.toast ("This lane is muted: you won't hear it (UNMUTE in the red bar)", 2000);
    if (y < hy && x < kx) { ed.proc.setPlayCursor (-1); repaint(); return; }   // the ruler's corner: the PLAY cursor back to the beginning
    if (inStrip (e.position))
    {   // the TIMELINE strip: right-click = play from here, right-drag = loop; left only moves / resizes a loop that is there; the grips drag out
        if (gripRect (false).contains (e.position)) { dr.kind = 9; return; }
        if (gripRect (true).contains (e.position)) { dr.kind = 10; return; }
        const auto sc = ed.proc.section(); secT0 = snapSec (T (x), e.mods); secS0 = sc.start; secE0 = sc.end;
        int mode = 0; const double tt = T (x);
        if (sc.valid()) { if (std::abs (x - X (sc.start)) < 7) mode = 2; else if (std::abs (x - X (sc.end)) < 7) mode = 3; else if (tt >= sc.start && tt < sc.end) mode = 4; }
        if (e.mods.isPopupMenu()) { dr.kind = 8; secMode = mode != 0 ? mode : 1; return; }
        if (mode != 0) { dr.kind = 7; secMode = mode; }
        return;
    }
    if (y < hy)
    {
        if (x < kx) return;
        const int t = (int) T (x);
        if (y < hy1)
        {   // chord names: tap = next version, right-click = list
            const int loop = juce::jmax (1, I.H.loopTicks), tt = t % loop; int slot = -1;
            for (size_t i = 0; i < I.chords.size(); ++i) if (tt >= I.chords[i].t && tt < I.chords[i].tEnd) slot = (int) i;
            if (slot < 0) return;
            if (e.mods.isPopupMenu()) chordMenu (slot);
            else ed.proc.change ([slot] (Idea& X) { nextAlt (X, slot); }, true, "chord");
            return;
        }
        if (e.mods.isPopupMenu()) return;   // right-click on the bar row does nothing (it can never hit a padlock)
        const int b = t / BAR;
        if (x - X (b * BAR) < 18)
        {
            const bool wasLocked = b < (int) I.lanes[ln].barLocks.size() && I.lanes[ln].barLocks[(size_t) b];
            ed.proc.change ([ln, b] (Idea& X) { auto& L = X.lanes[ln].barLocks; if (b < (int) L.size()) L[(size_t) b] = ! L[(size_t) b]; }, true, wasLocked ? "unlock bar" : "lock bar");
            ed.toast ("Bar " + juce::String (b + 1) + (wasLocked ? " unlocked" : " locked: tweaks leave it alone (click the padlock again to unlock)")); return;
        }
        if (e.mods.isShiftDown() && selBar0 >= 0) selBar1 = b; else if (selBar0 == b && selBar1 == b) selBar0 = selBar1 = -1; else selBar0 = selBar1 = b;
        std::fill (sel.begin(), sel.end(), false); ed.refresh(); repaint(); return;
    }
    if (x < kx) { dr.kind = 5; audition (P (y)); repaint(); return; }   // the keyboard: hear the key
    const auto notes = rollNotes (I, ln);
    frozen = true;
    if (e.mods.isPopupMenu())
    {
        const int ni = hitNote (notes, x, y);
        const int li = ni < 0 && ln != LaneChords ? hitLink (notes, x, y) : -1;   // (CHORDS: no lines between notes)
        if (li >= 0)
        {
            selLink = li; ed.refresh(); repaint(); juce::PopupMenu m; const auto cur = notes[(size_t) li].l.type;
            m.addSectionHeader ("Line between two notes"); m.addItem (1, "GLIDE (slide into the next note)", true, cur == 1); m.addItem (2, "Glide early + long"); m.addItem (3, "Glide late + quick");
            m.addItem (4, "FATTEN (next note louder)", true, cur == 2); m.addItem (5, "STEP (abrupt change)", true, cur == 0); m.addSeparator(); m.addItem (6, "Set ALL lines like this one"); m.addItem (7, "Reset ALL lines to STEP");
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, li, ln] (int r) {
                if (r <= 0) return;
                static const char* lab[] { "", "glide", "glide early", "glide late", "fatten", "step", "all lines", "reset lines" };
                ed.proc.change ([r, li, ln] (Idea& X) { auto& ns = X.lanes[ln].notes; if (li + 1 >= (int) ns.size()) return; auto& l = ns[(size_t) li].l;
                    if (r == 1) l.type = 1; else if (r == 2) { l.type = 1; l.w = 0.1f; } else if (r == 3) { l.type = 1; l.w = 0.85f; } else if (r == 4) { l.type = 2; l.f = juce::jmax (0.5f, l.f); } else if (r == 5) l.type = 0;
                    else if (r == 6) { const Link c = l; for (auto& n : ns) n.l = c; } else { for (auto& n : ns) n.l = Link(); }
                    X.lanes[ln].edited = true; }, true, lab[juce::jlimit (0, 7, r)]); });
            return;
        }
        if (ni >= 0) noteMenu (ni); else rollMenu();
        return;
    }
    if (ln != LaneChords && hitHandle (notes, selLink, x, y)) { dr.kind = 2; dr.idx = selLink; dr.l0 = notes[(size_t) selLink].l; repaint(); return; }   // drag the amount
    const int ni = hitNote (notes, x, y);
    if (ni >= 0)
    {
        dr.kind = 1; dr.idx = ni; dr.t0 = notes[(size_t) ni].t; dr.p0 = notes[(size_t) ni].p;
        if (! e.mods.isShiftDown()) sel.assign (notes.size(), false);
        if (ni < (int) sel.size()) sel[(size_t) ni] = e.mods.isShiftDown() ? ! sel[(size_t) ni] : true;
        selLink = -1; audition (notes[(size_t) ni].p); ed.refresh(); repaint(); return;
    }
    const int li = ln != LaneChords ? hitLink (notes, x, y) : -1;
    if (li >= 0) { selLink = li; dr.kind = 0; ed.refresh(); repaint(); return; }   // click a line = select it (the type stays)
    dr.kind = (e.mods.isShiftDown() || e.mods.isCommandDown()) ? 3 : 4;
}

bool Roll::inPadlock (juce::Point<float> p) const
{
    if (p.y < hy1 || p.y >= hy2 || p.x < kx) return false;
    const int b = (int) std::floor (T (p.x) / BAR);
    return p.x - X (b * BAR) < 18;
}
juce::Rectangle<float> Roll::gripRect (bool audio) const
{
    const auto sc = ed.proc.section(); if (! sc.valid()) return {};
    const float W = (float) getWidth(), x0 = juce::jmax (kx, X (sc.start)), x1 = juce::jmin (W, X (sc.end));
    if (x1 <= kx || x0 >= W) return {};
    const float gw = 36, gh = hy - hy2 - 6;
    float right = x1 - 4;
    if (x1 - x0 < 2 * gw + 70) right = juce::jmin (W - 2, x1 + 4 + 2 * gw + 4);   // a short loop: the grips sit just after it
    const float gx = right - (audio ? gw : 2 * gw + 4);
    return { gx, hy2 + 3, gw, gh };
}
int Roll::snapSec (double t, const juce::ModifierKeys& m) const
{
    const auto& I = ed.proc.idea();
    const int g = m.isAltDown() ? BAR : m.isShiftDown() ? juce::jmax (1, BAR / juce::jmax (1, laneSpb (I, ed.lane()))) : PPQ;
    return juce::jlimit (0, juce::jmax (BAR, I.total), (int) std::lround (t / g) * g);
}
void Roll::mouseDrag (const juce::MouseEvent& e)
{
    if (dr.kind == 9 || dr.kind == 10)
    {   // a loop grip dragged out of the plugin
        if (dr.moved || e.getDistanceFromDragStart() < 6) return;
        dr.moved = true; ed.startSectionDrag (dr.kind == 10, this); return;
    }
    if (dr.kind == 7 || dr.kind == 8)
    {
        if (e.getDistanceFromDragStart() < 6) return;   // a click (or a trackpad wobble) never makes a loop
        dr.moved = true;
        const int t = snapSec (T (e.position.x), e.mods), g = e.mods.isAltDown() ? BAR : e.mods.isShiftDown() ? juce::jmax (1, BAR / juce::jmax (1, laneSpb (ed.proc.idea(), ed.lane()))) : PPQ;
        int s = secS0, en = secE0;
        if (secMode == 1) { s = juce::jmin (secT0, t); en = juce::jmax (secT0, t); if (en - s < g) en = s + g; }
        else if (secMode == 2) s = juce::jmin (t, secE0 - g);
        else if (secMode == 3) en = juce::jmax (t, secS0 + g);
        else { const int d = t - secT0; const int L = juce::jmax (BAR, ed.proc.idea().total); const int dd = juce::jlimit (-secS0, L - secE0, d); s = secS0 + dd; en = secE0 + dd; }
        const bool was = ed.proc.section().valid();
        ed.proc.setSection (s, en);
        if (secMode == 1 && ! was) ed.proc.setSectionOn (true);
        if (secMode == 1 && ! ed.proc.section().on) ed.proc.setSectionOn (true);
        selBar0 = selBar1 = -1; ed.refresh(); repaint();
        return;
    }
    if (drawingE) { energyDraw (e); return; }
    const int ln = ed.lane(); const float x = e.position.x, y = e.position.y;
    const auto& I = ed.proc.idea();
    if (dr.kind == 5) { if (y >= hy) audition (juce::jlimit (0, 127, P (y))); repaint(); return; }   // glissando over the keys
    if (! dr.moved && e.getDistanceFromDragStart() < 5) return;
    if (! dr.moved) { dr.moved = true; }
    if (dr.kind == 1)
    {
        const int st = BAR / juce::jmax (1, laneSpb (I, ln));
        const int t = juce::jlimit (0, I.total - st, (int) std::lround ((dr.t0 + (T (x) - T (dr.x0))) / st) * st);
        int p = juce::jlimit (24, 108, dr.p0 + (int) std::lround (-(y - dr.y0) / rh()));
        if (ed.snapKey) p = snapToKey (p, t, I.H, -(int) (y - dr.y0));   // (CHORDS too: the key + the chord, as MELODY)
        const auto n = rollNotes (I, ln); if (dr.idx < 0 || dr.idx >= (int) n.size()) return;
        if (n[(size_t) dr.idx].t == t && n[(size_t) dr.idx].p == p) return;
        if (n[(size_t) dr.idx].p != p) audition (p);
        const bool first = ! dr.undo; dr.undo = true;
        int newIdx = dr.idx;
        ed.proc.change ([&] (Idea& X) {
            editLane (X, ln);
            auto& ns = X.lanes[ln].notes; Note m = ns[(size_t) dr.idx]; m.t = t; m.p = p - rollShift (X, ln);
            ns.erase (ns.begin() + dr.idx);
            size_t k = 0; while (k < ns.size() && (ns[k].t < m.t || (ns[k].t == m.t && ns[k].p < m.p))) ++k;
            ns.insert (ns.begin() + (long) k, m); newIdx = (int) k; X.lanes[ln].edited = true; }, first, "moved note");
        dr.idx = newIdx; sel.assign (I.lanes[ln].notes.size(), false); if (newIdx < (int) sel.size()) sel[(size_t) newIdx] = true;
    }
    else if (dr.kind == 2)
    {   // the handle: only the current type's amount changes
        const auto& n = I.lanes[ln].notes; if (dr.idx + 1 >= (int) n.size()) return;
        const bool first = ! dr.undo; dr.undo = true; glideHintShown = true;
        const int i = dr.idx; const Link l0 = dr.l0; const double tx = T (x); const float dy = y - dr.y0;
        const int ta = n[(size_t) i].t, tb = n[(size_t) i + 1].t;
        const Link nl = l0.type == 1 ? glideDrag (l0, tx, ta, tb) : fattenDrag (l0, l0.f, dy);
        ed.proc.change ([=] (Idea& X) { auto& ns = X.lanes[ln].notes; if (i + 1 >= (int) ns.size()) return; ns[(size_t) i].l = nl; X.lanes[ln].edited = true; }, first,
                        nl.type == 1 ? "glide " + juce::String (juce::roundToInt (nl.w * 100)) + "%" : "fatten " + juce::String (juce::roundToInt (nl.f * 100)) + "%");
    }
    else if (dr.kind == 3) { dr.band = juce::Rectangle<float> (juce::Point<float> (dr.x0, dr.y0), e.position); repaint(); }
    else if (dr.kind == 4 || dr.kind == 6)
    {   // hand tool: drag the view (time left / right, pitch up / down)
        view = panBy ({ dr.vs0, view.len }, -(x - dr.x0) / juce::jmax (1.0f, (float) getWidth() - kx) * view.len, total());
        const int rows = (int) std::lround ((y - dr.y0) / rh());
        if (rows != 0 || pitchManual) { const int span = hi - lo; int nl = juce::jlimit (0, 127 - span, dr.lo0 + rows); lo = nl; hi = nl + span; pitchManual = true; }
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        repaint();
    }
}

void Roll::mouseUp (const juce::MouseEvent& e)
{
    if (dr.kind == 8 && ! dr.moved)
    {   // right-click in the strip: play from here
        dr = Drag(); setCursorAt (e.position.x, hy2 + 1.0f);
        const int pc = ed.proc.playCursor.load();
        ed.toast ("PLAY from " + juce::String (pc / BAR + 1) + "." + juce::String ((pc % BAR) / PPQ + 1) + " (SPACE starts here; HOME = back to the start)", 1800);
        return;
    }
    if (dr.kind >= 7 && dr.kind <= 10) { dr = Drag(); repaint(); return; }
    if (drawingE)
    {
        drawingE = false; const auto curve = drawE;
        ed.proc.change ([curve] (Idea& X) { setArpEnergy (X, curve); }, true, "ENERGY");
        ed.toast ("ENERGY drawn: the arp's level, brightness and space (+ CC74 / CC11 out) follow it. Double-click the strip = back to the shape's curve");
        return;
    }
    const int ln = ed.lane(); const auto& I = ed.proc.idea();
    const Drag d = dr; dr = Drag(); frozen = false;
    audition (-1);
    if (d.kind == 4 || d.kind == 6) setMouseCursor (juce::MouseCursor::NormalCursor);
    if (e.mods.isPopupMenu() || d.kind == 5 || d.kind == 6) { repaint(); return; }
    if (d.moved)
    {
        if (d.kind == 1)
        {   // a moved note replaces whatever else sat on its step
            ed.proc.change ([ln, idx = d.idx] (Idea& X) { auto& ns = X.lanes[ln].notes; if (idx < 0 || idx >= (int) ns.size()) return; const Note m = ns[(size_t) idx];
                std::vector<Note> o; for (size_t k = 0; k < ns.size(); ++k) if ((int) k == idx || ns[k].t != m.t || (ln == LaneChords && ns[k].p != m.p)) o.push_back (ns[k]); ns = o; }, false);   // (CHORDS: only the same pitch)
        }
        if (d.kind == 3)
        {
            const auto ns = rollNotes (I, ln); sel.assign (ns.size(), false); int cnt = 0;
            for (size_t k = 0; k < ns.size(); ++k) { const auto r = juce::Rectangle<float> (X (ns[k].t), Y (ns[k].p), juce::jmax (3.0f, X (ns[k].t + ns[k].d) - X (ns[k].t)), rh()); if (r.intersects (d.band)) { sel[k] = true; ++cnt; } }
            selBar0 = selBar1 = -1;
            if (cnt) ed.toast ("Lasso: " + juce::String (cnt) + " notes selected: tweaks work on them only (ARP: HARVEST sends them to MELODY)");
        }
        ed.refresh(); repaint(); return;
    }
    if (d.kind == 1 || d.kind == 2) { ed.refresh(); repaint(); return; }   // a click on a note selects it (double-click deletes)
    if (d.kind == 4 && d.x0 > kx && d.y0 > hy)
    {
        const int spb = laneSpb (I, ln), st = BAR / juce::jmax (1, spb);
        const int t = (int) std::floor (T (d.x0) / st) * st;
        int p = P (d.y0); if (ed.snapKey) p = snapToKey (p, t, I.H, 1);
        if (t < 0 || t >= I.total) return;
        const double gate = ln == LaneArp ? I.arpGate : ln == LaneBass ? 0.6 : 0.8;
        ed.proc.change ([=] (Idea& X) { editLane (X, ln); const int q = p - rollShift (X, ln);
            auto& ns = X.lanes[ln].notes; std::vector<Note> o; for (auto& n : ns) if (n.t != t || (ln == LaneChords && n.p != q)) o.push_back (n);   // (CHORDS: a chord note joins the others)
            Note n; n.t = t; n.p = q; n.d = ln == LaneChords ? PPQ : juce::jmax (6, (int) std::lround (st * gate)); n.v = ln == LaneChords ? 0.62f : 0.78f; n.acc = ln != LaneChords && t % PPQ == 0;
            o.push_back (n); sortNotes (o); ns = o; X.lanes[ln].edited = true; X.lanes[ln].mute = false; }, true, "added note");
        ed.proc.audition (p, true); juce::Timer::callAfterDelay (180, [pr = &ed.proc, p] { pr->audition (p, false); });
        clearSel();
    }
    else if (d.kind == 4) { clearSel(); repaint(); }
}

void Roll::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.position.y < hy && e.position.y >= hy2)
    {   // double-click the brace: gone
        const auto sc = ed.proc.section(); const double t = T (e.position.x);
        if (sc.valid() && t >= sc.start && t < sc.end) { ed.proc.clearSection(); ed.toast ("LOOP SECTION cleared", 1400); ed.refresh(); repaint(); return; }
    }
    if (energyStrip().contains (e.position)) { drawingE = false; ed.proc.change ([] (Idea& X) { setArpEnergy (X, {}); }, true, "ENERGY RESET"); return; }
    const int ln = ed.lane(); if (e.position.y < hy || e.position.x < kx) return;
    const auto ns0 = rollNotes (ed.proc.idea(), ln); const int ni = hitNote (ns0, e.position.x, e.position.y); if (ni < 0) return;
    if (ns0[(size_t) ni].lock) { ed.toast ("That note is locked (right-click > Unlock)"); return; }
    ed.proc.change ([ln, ni] (Idea& X) { editLane (X, ln); auto& ns = X.lanes[ln].notes; if (ni < (int) ns.size()) ns.erase (ns.begin() + ni); X.lanes[ln].edited = true; }, true, "deleted note");
    clearSel();
}

void Roll::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCommandDown() || e.mods.isCtrlDown()) { zoomAround (e.position.x, std::pow (2.0, -w.deltaY * 2.5)); return; }   // Cmd / Ctrl + wheel = zoom
    float dx = w.deltaX, dy = w.deltaY;
    if (e.mods.isShiftDown() && dx == 0) { dx = dy; dy = 0; }
    if (dx != 0) view = panBy (view, -dx * view.len * 0.6, total());
    if (dy != 0)
    {   // up / down: move through the pitches
        const int span = hi - lo, rows = (int) std::lround (dy * 18.0f); const int step = rows != 0 ? rows : (dy > 0 ? 1 : -1);
        lo = juce::jlimit (0, 127 - span, lo + step); hi = lo + span; pitchManual = true;
    }
    repaint();
}
void Roll::mouseMagnify (const juce::MouseEvent& e, float scale) { if (scale > 0) zoomAround (e.position.x, 1.0 / scale); }   // trackpad pinch

void Roll::transposeSelected (int d)
{
    const int ln = ed.lane();
    const auto s = sel; if (std::find (s.begin(), s.end(), true) == s.end()) return;
    ed.proc.change ([ln, s, d] (Idea& X) { editLane (X, ln); const int sh = rollShift (X, ln); auto& ns = X.lanes[ln].notes;
                        for (size_t i = 0; i < ns.size() && i < s.size(); ++i) if (s[i]) ns[i].p = ln == LaneChords ? foldChordPitch (ns[i].p + sh + d) - sh : foldPitch (ns[i].p + d, ln);   // (CHORDS: kept in 24..108)
                        X.lanes[ln].edited = true; },
                    true, "transpose selected " + juce::String (d > 0 ? "+" : "") + juce::String (d));
    repaint();
}
bool Roll::keyPressed (const juce::KeyPress& k)
{
    const int ln = ed.lane();
    if (k == juce::KeyPress::escapeKey && ed.proc.section().valid()) { ed.proc.clearSection(); ed.toast ("LOOP SECTION cleared", 1400); ed.refresh(); repaint(); return true; }
    if ((k.getKeyCode() == juce::KeyPress::upKey || k.getKeyCode() == juce::KeyPress::downKey) && ! k.getModifiers().isCommandDown() && ! k.getModifiers().isAltDown())
    {   // the selected notes +-1 semitone (SHIFT = an octave); nothing selected = nothing happens
        if (std::find (sel.begin(), sel.end(), true) == sel.end()) return false;
        transposeSelected ((k.getKeyCode() == juce::KeyPress::upKey ? 1 : -1) * (k.getModifiers().isShiftDown() ? 12 : 1));
        return true;
    }
    if ((k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) && std::find (sel.begin(), sel.end(), true) != sel.end())
    {
        const auto s = sel;
        ed.proc.change ([ln, s] (Idea& X) { editLane (X, ln); auto& ns = X.lanes[ln].notes; std::vector<Note> o; for (size_t i = 0; i < ns.size(); ++i) if (i >= s.size() || ! s[i] || ns[i].lock) o.push_back (ns[i]); ns = o; X.lanes[ln].edited = true; }, true, "deleted notes");
        clearSel(); return true;
    }
    return false;
}

void Roll::noteMenu (int idx)
{
    const int ln = ed.lane(); const auto all = rollNotes (ed.proc.idea(), ln); if (idx < 0 || idx >= (int) all.size()) return; const auto n = all[(size_t) idx];
    juce::PopupMenu m; m.addSectionHeader ("Note");
    m.addItem (1, n.lock ? "Unlock (tweaks may change it)" : "Lock (tweaks keep it)"); m.addItem (2, "Delete"); m.addItem (3, "Louder"); m.addItem (4, "Softer");
    m.addItem (5, "Longer"); m.addItem (6, "Shorter"); m.addItem (7, "Octave up"); m.addItem (8, "Octave down");
    m.addSeparator(); m.addItem (9, "UNLOCK ALL (bars + notes of this lane)");
    const bool anySel = std::find (sel.begin(), sel.end(), true) != sel.end();
    if (anySel) { juce::PopupMenu t; t.addItem (20, "+1 semitone  (UP)"); t.addItem (21, "-1 semitone  (DOWN)"); t.addItem (22, "+1 octave  (SHIFT+UP)"); t.addItem (23, "-1 octave  (SHIFT+DOWN)"); m.addSubMenu ("Transpose selected", t); }
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, idx] (int r) { noteAction (idx, r); });
}
void Roll::noteAction (int idx, int r)
{
    const int ln = ed.lane();
    if (r <= 0) return;
    if (r >= 20 && r <= 23) { static const int d[] { 1, -1, 12, -12 }; transposeSelected (d[r - 20]); return; }
    static const char* lab[] { "", "lock note", "deleted note", "louder", "softer", "longer", "shorter", "octave up", "octave down", "unlock all" };
    ed.proc.change ([r, ln, idx] (Idea& X) { if (r == 9) { unlockAll (X, ln); return; } editLane (X, ln); const int sh = rollShift (X, ln); auto& ns = X.lanes[ln].notes; if (idx >= (int) ns.size()) return; auto& q = ns[(size_t) idx];
        if (r == 1) q.lock = ! q.lock; else if (r == 2) ns.erase (ns.begin() + idx); else if (r == 3) q.v = juce::jlimit (0.1f, 1.0f, q.v + 0.15f); else if (r == 4) q.v = juce::jlimit (0.1f, 1.0f, q.v - 0.15f);
        else if (r == 5) q.d = (int) std::lround (q.d * 1.5); else if (r == 6) q.d = juce::jmax (6, (int) std::lround (q.d / 1.5)); else if (r == 7) q.p = juce::jmin (108, q.p + sh + 12) - sh; else if (r == 8) q.p = juce::jmax (24, q.p + sh - 12) - sh;
        if (r != 1) X.lanes[ln].edited = true; }, true, lab[juce::jlimit (0, 9, r)]);
    clearSel();
}

void Roll::rollMenu()
{
    const int ln = ed.lane();
    juce::PopupMenu m; m.addSectionHeader ("Piano roll");
    m.addItem (1, "UNLOCK ALL (bars + notes of this lane)");
    m.addItem (2, "Zoom: fit everything"); m.addItem (3, "Zoom: 4 bars"); m.addItem (4, "Pitch: fit the notes");
    if (std::find (sel.begin(), sel.end(), true) != sel.end())
    { juce::PopupMenu t; t.addItem (20, "+1 semitone  (UP)"); t.addItem (21, "-1 semitone  (DOWN)"); t.addItem (22, "+1 octave  (SHIFT+UP)"); t.addItem (23, "-1 octave  (SHIFT+DOWN)"); m.addSubMenu ("Transpose selected", t); }
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, ln] (int r) {
        if (r >= 20 && r <= 23) { static const int d[] { 1, -1, 12, -12 }; transposeSelected (d[r - 20]); return; }
        if (r == 1) { ed.proc.change ([ln] (Idea& X) { unlockAll (X, ln); }, true, "unlock all"); ed.toast ("Everything in this lane unlocked"); }
        else if (r == 2) setView ({ 0, (double) total() });
        else if (r == 3) setView ({ view.start, 4.0 * BAR });
        else if (r == 4) { pitchManual = false; repaint(); }
    });
}

void Roll::chordMenu (int slot)
{
    const auto& I = ed.proc.idea(); if (slot < 0 || slot >= (int) chordProg (I).size()) return;
    const auto& s0 = chordProg (I)[(size_t) slot];
    const int nAlt = altCount (I, s0);
    juce::PopupMenu m; m.addSectionHeader ("Chord " + juce::String (slot + 1) + (I.simple ? "  (SIMPLE)" : ""));
    for (int k = 0; k < nAlt; ++k)
    {
        const int a = altAt (I, s0, k);
        ProgState ps = progState (I); ps.prog[(size_t) slot].alt = a;
        const auto rc = resolveProg (ps);
        const juce::String nm = s0.fixed ? juce::String (std::vector<const char*> { "as heard", "plain triad", "no slash bass" }[(size_t) a]) : juce::String (altNames()[(size_t) a]);
        m.addItem (100 + k, juce::String (rc[(size_t) slot].name) + "   (" + nm + ")", true, k == altIndex (I, s0));
    }
    m.addSeparator();
    for (size_t j = 0; j < I.chords.size(); ++j) if ((int) j != slot) m.addItem (200 + (int) j, "= chord " + juce::String ((int) j + 1) + " (" + juce::String (I.chords[j].name) + ")");
    m.addItem (300, "Set ALL slots to this chord");
    m.showMenuAsync (juce::PopupMenu::Options(), [this, slot] (int r) {
        if (r <= 0) return;
        ed.proc.change ([r, slot] (Idea& X) {
            auto& pr = chordProg (X);
            if (r >= 300) { const Slot src = pr[(size_t) slot]; for (auto& p : pr) { const int len = p.len; p = src; p.len = len; } }
            else if (r >= 200) { const int len = pr[(size_t) slot].len; pr[(size_t) slot] = pr[(size_t) (r - 200)]; pr[(size_t) slot].len = len; }
            else pr[(size_t) slot].alt = altAt (X, pr[(size_t) slot], r - 100);
            rebuildChords (X); refitLanesToChords (X); });
    });
}

// ================================================================= MORE panel
static const char* EXAMPLES[] { "hypnotic sparse bell in A minor", "euphoric 124 bpm", "melodic minor pluck", "dreamy lush 9ths", "dark rolling acid", "epic octave up", "triplet tense", "busy wild glide" };

MorePanel::MorePanel (ArpEditor& e)
    : ed (e),
      bars (e.th, { "2 BARS", "4 BARS", "8 BARS", "16" }), resync (e.th, { "AUTO", "1", "2", "4", "8", "OFF" }), evolveLen (e.th, { "16 BARS", "32 BARS" }),
      phrase (e.th, { "AUTO", "4", "8", "16" }), melRate (e.th, { "1/8", "1/16" }), reg (e.th, { "LOW", "MID", "HIGH" }), keySeg (e.th, {}),
      hold (e.th, "HOLD PEDAL"), pushB (e.th, "PUSH FURTHER"), listenB (e.th, "LISTEN (sidechain)"), listenFile (e.th, "LISTEN TO A FILE"), savePre (e.th, "SAVE IDEA"), loadPre (e.th, "LOAD IDEA"),
      starters (e.th, "STARTERS"), midiOut (e.th, "MIDI OUT"), ch1B (e.th, "MIDI OUT: ALL CH 1"), velB (e.th, "VOL SCALES VELOCITY"), bendB (e.th, "GLIDES AS PITCH BEND"), bendSeg (e.th, { "BEND +-2", "+-12", "+-24" }), oneTrack (e.th, "ONE-TRACK DRAG"), follow (e.th, "FOLLOW MY KEYS"), close (e.th, "CLOSE"), genWords (e.th, "GENERATE")
{
    auto& P = ed.proc;
    addAndMakeVisible (words); words.setTextToShowWhenEmpty ("extra words for the generator, e.g. 128 bpm in F# minor", ed.th.p.dim); words.setFont (F (14.0f, false));
    words.onReturnKey = [this] { ed.proc.change ([w = words.getText().toStdString()] (Idea& X) { X.words = w; }, false); ed.proc.generateAsync (ed.proc.idea().mode, true); };
    addAndMakeVisible (genWords); genWords.tint = [this] { return ed.th.p.a1; }; genWords.onClick = words.onReturnKey;
    for (auto* ex : EXAMPLES)
    {
        auto* b = chips.add (new Btn (ed.th, ex)); addAndMakeVisible (b); b->tint = [this] { return ed.th.p.a3; };
        b->onClick = [this, s = juce::String (ex)] { words.setText (s, false); ed.proc.change ([w = s.toStdString()] (Idea& X) { X.words = w; }, false); ed.proc.generateAsync (ed.proc.idea().mode, true); };
    }
    auto setupS = [this] (juce::Slider& s, double lo, double hi) { addAndMakeVisible (s); s.setSliderStyle (juce::Slider::LinearHorizontal); s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 18); s.setRange (lo, hi, 0.01); };
    setupS (master, 0, 1); setupS (groove, 0, 1); setupS (colour, 0, 1); setupS (gate, 0.1, 1); setupS (stutterS, 0, 1);
    master.onDragEnd = [this] { const double v = master.getValue(); ed.proc.change ([v] (Idea& X) { X.master = v; for (int l = LaneArp; l <= LaneMelody; ++l) composeLane (X, l, true); }); };
    groove.onValueChange = [this] { const double v = groove.getValue(); ed.proc.change ([v] (Idea& X) { X.groove = v; }, false); };
    colour.onDragEnd = [this] { const double v = colour.getValue(); ed.proc.change ([v] (Idea& X) { X.g.colour = v; rebuildChords (X); refitLanesToChords (X); }); };
    gate.onDragEnd = [this] { const double v = gate.getValue(); ed.proc.change ([v] (Idea& X) { X.arpGate = v; if (! X.lanes[LaneArp].edited) rerealizeArp (X); }); };
    stutterS.onDragEnd = [this] { const double v = stutterS.getValue(); ed.proc.change ([v] (Idea& X) { X.stutter = v; composeLane (X, LaneMelody, true); }); };
    for (auto* s : { &bars, &resync, &evolveLen, &phrase, &melRate, &reg }) { addAndMakeVisible (*s); s->tint = [this] { return ed.th.p.a2; }; }
    bars.onChange = [this] (int i) { const int nb = i == 0 ? 2 : i == 1 ? 4 : i == 2 ? 8 : 16; ed.proc.change ([nb] (Idea& X) {
        auto fit = [nb] (std::vector<Slot>& pr) {
            if (pr.empty()) return;
            std::vector<Slot> out; int beats = 0; size_t k = 0; const auto cur = pr;
            while (beats < nb * 4) { Slot s = cur[k % cur.size()]; s.len = juce::jmin (s.len ? s.len : 4, nb * 4 - beats); out.push_back (s); beats += s.len; ++k; }
            pr = out; };
        fit (X.g.prog); fit (X.simpleProg); rebuildChords (X); for (int l = LaneArp; l < kLanes; ++l) composeLane (X, l, false); }); };
    resync.onChange = [this] (int i) { const int v = i == 0 ? 0 : i == 5 ? -1 : std::vector<int> { 0, 1, 2, 4, 8 }[(size_t) i]; ed.proc.change ([v] (Idea& X) { X.resync = v; if (! X.lanes[LaneArp].edited) rerealizeArp (X); }); };
    evolveLen.onChange = [this] (int i) { ed.proc.change ([i] (Idea& X) { X.evolveBars = i ? 32 : 16; if (X.evolve) { rebuildChords (X); for (int l = LaneArp; l < kLanes; ++l) composeLane (X, l, true); } }); };
    phrase.onChange = [this] (int i) { const int v = std::vector<int> { 0, 4, 8, 16 }[(size_t) i]; ed.proc.change ([v] (Idea& X) { X.phraseBars = v; rebuildChords (X); for (int l = LaneArp; l < kLanes; ++l) composeLane (X, l, true); }); };
    melRate.onChange = [this] (int i) { ed.proc.change ([i] (Idea& X) { X.melSpb = i ? 16 : 8; composeLane (X, LaneMelody, true); }); };
    reg.onChange = [this] (int i) { ed.proc.change ([i] (Idea& X) { X.reg = i - 1; for (int l = LaneArp; l <= LaneMelody; ++l) composeLane (X, l, true); }); };
    for (auto* b : { &hold, &listenB, &listenFile, &savePre, &loadPre, &starters, &midiOut, &oneTrack, &follow, &close }) { addAndMakeVisible (*b); b->tint = [this] { return ed.th.p.a2; }; }
    addAndMakeVisible (pushB); pushB.tint = [this] { return ed.th.p.a2; }; pushB.toggle = true;
    pushB.setTooltip ("PUSH FURTHER: each GENERATE moves away from your last 3 ideas (another melody shape, rhythm, scale / bass pattern). Always on in TRAP and HIP HOP");
    pushB.onClick = [this] { ed.proc.pushFurther = pushB.on; };
    hold.toggle = true; hold.onClick = [this] { const bool v = hold.on; ed.proc.change ([v] (Idea& X) { X.holdPedal = v; if (! X.lanes[LaneArp].edited) rerealizeArp (X); }); };
    listenB.onClick = [this] { if (ed.proc.listenState == 1) ed.proc.stopListening(); else ed.proc.startListening(); };
    listenFile.onClick = [this] {
        auto fc = std::make_shared<juce::FileChooser> ("Listen to an audio file", juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3;*.m4a");
        fc->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, fc] (const juce::FileChooser& c) { if (c.getResult().existsAsFile()) ed.proc.listenToFile (c.getResult()); }); };
    savePre.onClick = [this] { setVisible (false); ed.openSave(); };
    loadPre.onClick = [this] { setVisible (false); ed.openBrowser(); };
    starters.onClick = [this] {
        const auto names = ed.proc.starterNames(); juce::PopupMenu m;
        for (int i = 0; i < names.size(); ++i) m.addItem (i + 1, names[i]);
        if (names.isEmpty()) m.addItem (-1, "(no starters in this build)", false);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&starters), [this] (int r) { if (r > 0) { ed.proc.loadStarter (r - 1); ed.selectLane (ed.proc.idea().mode == Midnight ? LaneArp : LaneMelody); ed.toast ("Starter idea loaded: now VARY it, lock what you like, drag it out"); } }); };
    midiOut.toggle = true; midiAtt = nullptr;
    midiOut.onClick = [this] { if (auto* p = ed.proc.apvts.getParameter ("midiOut")) p->setValueNotifyingHost (midiOut.on ? 1.0f : 0.0f); };
    oneTrack.toggle = true; oneTrack.onClick = [this] { ed.proc.oneTrackDrag = oneTrack.on; };
    follow.toggle = true; follow.onClick = [this] { if (auto* p = ed.proc.apvts.getParameter ("follow")) p->setValueNotifyingHost (follow.on ? 1.0f : 0.0f); };
    addAndMakeVisible (bendSeg); bendSeg.tint = [this] { return ed.th.p.a2; }; bendSeg.fontH = 10.5f;
    bendSeg.onChange = [this] (int i) { ed.proc.pushUndoPoint ("bend range"); if (auto* p = ed.proc.apvts.getParameter ("bendRange")) p->setValueNotifyingHost (p->convertTo0to1 ((float) i)); ed.proc.change ([] (Idea&) {}, false); };
    bendSeg.setTooltip ("Pitch-bend range the glides are written for (set the same range on your synth; RPN 0 is sent too)");
    for (auto* b : { &ch1B, &velB, &bendB }) { addAndMakeVisible (*b); b->toggle = true; b->tint = [this] { return ed.th.p.a2; }; }
    ch1B.onClick = [this] { if (auto* p = ed.proc.apvts.getParameter ("midiCh1")) p->setValueNotifyingHost (ch1B.on ? 1.0f : 0.0f); };
    ch1B.setTooltip ("Live MIDI out: every lane on channel 1 (on), or ARP 1 / MELODY 2 / CHORDS 3 / BASS 4 (off). Dragged .mid files are always channel 1.");
    bendB.onClick = [this] { ed.proc.pushUndoPoint ("glide bends"); if (auto* p = ed.proc.apvts.getParameter ("glideBend")) p->setValueNotifyingHost (bendB.on ? 1.0f : 0.0f); ed.proc.change ([] (Idea&) {}, false); };
    bendB.setTooltip ("MELODY / BASS glides also write pitch bend (plus overlapping legato notes for portamento). FL Studio: dragging a .mid onto the piano roll keeps notes + velocity only; "
                      "bends and CC11 arrive when you import the .mid as a MIDI file (File > Import > MIDI file). FL slide notes can't be written in MIDI: for the exact sound use DRAG AUDIO.");
    velB.onClick = [this] { if (auto* p = ed.proc.apvts.getParameter ("velScale")) p->setValueNotifyingHost (velB.on ? 1.0f : 0.0f); };
    velB.setTooltip ("The lane VOL faders also scale the MIDI velocity (so the balance carries over to Ghosty Pro)");
    close.onClick = [this] { setVisible (false); };
    {   // LANES: mood / variation + FX per lane (all neutral by default)
        static const int order[4] { LaneChords, LaneArp, LaneMelody, LaneBass };
        static const char* fxTip[4] { "DRIVE: warm saturation on this lane only", "TONE: left = darker, right = thinner (centre = off)", "ECHO: send this lane into the dotted 1/8 delay", "SPACE: send this lane into the reverb" };
        for (int r = 0; r < 4; ++r)
        {
            const int ln = order[r];
            auto* sg = laneMoodS.add (new Seg (ed.th, { "SAME", "SUNRISE", "DUSK", "MIDNIGHT", "TRAP", "HIP HOP" })); addAndMakeVisible (sg);
            sg->tint = [this, ln] { return ed.laneColour (ln); };
            auto* vb = laneVarB.add (new Btn (ed.th, "NEW TAKE")); addAndMakeVisible (vb); vb->tint = [this, ln] { return ed.laneColour (ln); };
            const bool moodable = ln == LaneArp || ln == LaneMelody;
            sg->setEnabled (moodable); vb->setEnabled (moodable);
            sg->setTooltip (moodable ? "LANE MOOD: this lane composes in its own mood (SAME = the idea's mood); the other lanes keep theirs" : "LANE MOOD: the chords and the bass follow the harmony (ARP and MELODY can pick their own mood)");
            vb->setTooltip (moodable ? "NEW TAKE: a new variation of this lane only (UNDO to go back)" : "NEW TAKE: for ARP and MELODY");
            sg->onChange = [this, ln] (int i) { ed.proc.setLaneMood (ln, i - 1); ed.refresh(); };
            vb->onClick = [this, ln] { ed.proc.newLaneVariation (ln); ed.toast (ArpEditor::laneName (ln) + ": a new take (UNDO to go back)", 1600); ed.refresh(); };
            for (int k = 0; k < 4; ++k)
            {
                auto* s = laneFxS.add (new juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox)); addAndMakeVisible (s);
                s->setRange (k == 1 ? -1.0 : 0.0, 1.0, 0.01); s->setDoubleClickReturnValue (true, 0.0); s->setPopupDisplayEnabled (true, true, this);
                s->setTooltip (ArpEditor::laneName (ln) + " " + fxTip[k] + " (double-click = off)");
                s->setColour (juce::Slider::trackColourId, ed.laneColour (ln));
                s->onValueChange = [this, s, ln, k] { ed.proc.setLaneFx (ln, k, (float) s->getValue()); };
            }
        }
    }
    addAndMakeVisible (darkS); darkS.setSliderStyle (juce::Slider::LinearHorizontal); darkS.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    darkS.setRange (0, 100, 1); darkS.setPopupDisplayEnabled (true, true, this); darkS.setDoubleClickReturnValue (true, 70);
    darkS.setTooltip ("DARKNESS (TRAP / HIP HOP): up = phrygian / harmonic minor, b2 + b5 notes, lower and sparser melodies, longer 808s, slower slides, darker chords + sound; 0 = a brighter minor");
    auto applyDark = [this] { const int v = (int) darkS.getValue(); const auto& I0 = ed.proc.idea(); if (! urbanMood (I0.mode) || v == darknessOf (I0, I0.mode)) return;
        ed.proc.change ([v] (Idea& X) { X.darkness = v; newIdea (X); }); ed.proc.applyDarknessSound(); ed.refresh(); };
    darkS.onDragEnd = applyDark; darkS.onValueChange = [this, applyDark] { if (! darkS.isMouseButtonDown()) applyDark(); };
    addAndMakeVisible (flavourBox);
    flavourBox.setTooltip ("FLAVOUR (TRAP / HIP HOP): AUTO picks one per idea; a flavour sets tempo, scales, progressions, the 808 / bass patterns and the melody rhythms");
    flavourBox.onChange = [this] { const int f = flavourBox.getSelectedId() - 2; const auto& I0 = ed.proc.idea(); if (! urbanMood (I0.mode) || f == I0.flavour) return;
        ed.proc.change ([f] (Idea& X) { X.flavour = f; newIdea (X); }); ed.proc.applyFlavourLanes (false); ed.refresh(); };
    addAndMakeVisible (key); addAndMakeVisible (scale);
    for (int k = 0; k < 12; ++k) key.addItem (juce::String (noteName (k, k, "major")) + " / " + juce::String (noteName (k, k, "minor")) + "m", k + 1);
    for (auto& s : scaleNames()) scale.addItem (juce::String (scaleLabel (s)) + " (" + juce::String (scaleFeel (s)) + ")", scale.getNumItems() + 1);
    key.onChange = [this] { const int k = key.getSelectedId() - 1; if (k < 0 || k == ed.proc.idea().g.key) return;
        ed.proc.change ([k] (Idea& X) { int d = mod12 (k - X.g.key); if (d > 6) d -= 12; X.g.key = k; for (auto& L : X.lanes) for (auto& n : L.notes) n.p = juce::jlimit (0, 127, n.p + d); rebuildChords (X); }); };
    scale.onChange = [this] { const int i = scale.getSelectedId() - 1; if (i < 0 || scaleNames()[(size_t) i] == ed.proc.idea().g.scale) return;
        ed.proc.change ([i] (Idea& X) { X.g.scale = scaleNames()[(size_t) i]; rebuildChords (X); refitLanesToChords (X); }); };
    vol = std::make_unique<Knob> (ed.th, P.apvts, "volume", "VOLUME"); verb = std::make_unique<Knob> (ed.th, P.apvts, "reverb", "REVERB");
    addAndMakeVisible (*vol); addAndMakeVisible (*verb);
}
void MorePanel::sync()
{
    const auto& I = ed.proc.idea();
    {   static const int order[4] { LaneChords, LaneArp, LaneMelody, LaneBass };
        for (int r = 0; r < 4 && r < laneMoodS.size(); ++r)
        {
            const int ln = order[r]; laneMoodS[r]->sel = I.laneMood[(size_t) ln] + 1; laneMoodS[r]->repaint();
            for (int k = 0; k < 4; ++k) { auto* s = laneFxS[r * 4 + k]; s->setValue (ed.proc.getLaneFx (ln, k), juce::dontSendNotification); s->setColour (juce::Slider::trackColourId, ed.laneColour (ln)); }
        }
    }
    if (! words.hasKeyboardFocus (true)) words.setText (juce::String (I.words), false);
    master.setValue (I.master, juce::dontSendNotification); groove.setValue (I.groove, juce::dontSendNotification); colour.setValue (I.g.colour, juce::dontSendNotification);
    gate.setValue (I.arpGate, juce::dontSendNotification); stutterS.setValue (I.stutter, juce::dontSendNotification);
    colour.setEnabled (! I.simple); colour.setAlpha (I.simple ? 0.4f : 1.0f);   // SIMPLE chords ignore CHORD COLOUR
    const int nb = (int) std::lround ((double) progTicks (chordProg (I)) / BAR); bars.sel = nb <= 2 ? 0 : nb <= 4 ? 1 : nb <= 8 ? 2 : 3;
    resync.sel = I.resync < 0 ? 5 : I.resync == 0 ? 0 : I.resync == 1 ? 1 : I.resync == 2 ? 2 : I.resync == 4 ? 3 : 4;
    evolveLen.sel = I.evolveBars >= 32 ? 1 : 0; phrase.sel = I.phraseBars == 4 ? 1 : I.phraseBars == 8 ? 2 : I.phraseBars == 16 ? 3 : 0; melRate.sel = I.melSpb == 8 ? 0 : 1; reg.sel = juce::jlimit (0, 2, I.reg + 1);
    bendB.on = ed.proc.apvts.getRawParameterValue ("glideBend")->load() > 0.5f; bendSeg.sel = juce::jlimit (0, 2, (int) ed.proc.apvts.getRawParameterValue ("bendRange")->load());
    ch1B.on = ed.proc.apvts.getRawParameterValue ("midiCh1")->load() > 0.5f; velB.on = ed.proc.apvts.getRawParameterValue ("velScale")->load() > 0.5f;
    hold.on = I.holdPedal; pushB.on = ed.proc.pushFurther || urbanMood (I.mode); pushB.setEnabled (! urbanMood (I.mode)); midiOut.on = ed.proc.apvts.getRawParameterValue ("midiOut")->load() > 0.5f; follow.on = ed.proc.apvts.getRawParameterValue ("follow")->load() > 0.5f; oneTrack.on = ed.proc.oneTrackDrag;
    {   // FLAVOUR: the list of the idea's mood (TRAP / HIP HOP), off for the other moods
        flavourBox.clear (juce::dontSendNotification);
        if (urbanMood (I.mode)) { flavourBox.addItem ("AUTO (" + juce::String (flavourName (I.mode, I.flavourUsed)) + ")", 1); for (int f = 0; f < flavourCount (I.mode); ++f) flavourBox.addItem (flavourName (I.mode, f), f + 2); }
        else flavourBox.addItem ("TRAP / HIP HOP only", 1);
        flavourBox.setSelectedId (urbanMood (I.mode) ? I.flavour + 2 : 1, juce::dontSendNotification); flavourBox.setEnabled (urbanMood (I.mode));
        darkS.setDoubleClickReturnValue (true, defaultDarkness (urbanMood (I.mode) ? I.mode : Trap));
        darkS.setValue (urbanMood (I.mode) ? darknessOf (I, I.mode) : 70, juce::dontSendNotification); darkS.setEnabled (urbanMood (I.mode));
    }
    key.setSelectedId (I.g.key + 1, juce::dontSendNotification);
    for (size_t i = 0; i < scaleNames().size(); ++i) if (scaleNames()[i] == I.g.scale) scale.setSelectedId ((int) i + 1, juce::dontSendNotification);
    listenB.text = ed.proc.listenState == 1 ? "STOP LISTENING" : "LISTEN (sidechain)";
    repaint();
}
void MorePanel::paint (juce::Graphics& g)
{
    const auto& P = ed.th.p; auto r = getLocalBounds().toFloat();
    fillCard (g, r, 14, P); g.setColour (P.a1); g.drawRoundedRectangle (r.reduced (1), 14, 1.6f);
    g.setColour (P.ink); g.setFont (F (15.0f)); g.drawText ("MORE", 16, 10, 200, 22, juce::Justification::centredLeft);
    g.setColour (P.dim); g.setFont (F (9.5f, false)); g.drawText ("ArpEZator v" + ArpEditor::versionString(), getLocalBounds().reduced (16, 8), juce::Justification::bottomRight);
    g.setColour (P.dim); g.setFont (F (10.5f));
    auto cap = [&] (const juce::String& s, juce::Rectangle<int> b) { g.drawText (s, b.translated (0, -15).withHeight (14), juce::Justification::centredLeft); };
    cap ("IDEA WORDS (feed the chord generator; same words + seed = same chords as the web version)", words.getBounds());
    cap ("MASTER  simple  <->  wild", master.getBounds()); cap ("GROOVE  0 = perfectly even", groove.getBounds()); cap (ed.proc.idea().simple ? "CHORD COLOUR  (RICH chords only)" : "CHORD COLOUR  plain <-> lush", colour.getBounds());
    cap ("ARP GATE", gate.getBounds()); cap ("STUTTER (SUNRISE bounces)", stutterS.getBounds()); cap ("CHORD LOOP", bars.getBounds()); cap ("ARP RE-SYNC (bars)", resync.getBounds());
    cap ("EVOLVE LENGTH", evolveLen.getBounds()); cap ("MELODY PHRASE (bars)", phrase.getBounds()); cap ("MELODY GRID", melRate.getBounds()); cap ("REGISTER", reg.getBounds());
    cap ("KEY", key.getBounds()); cap ("SCALE", scale.getBounds()); cap ("FLAVOUR (TRAP / HIP HOP)", flavourBox.getBounds()); cap ("DARKNESS " + juce::String ((int) darkS.getValue()), darkS.getBounds()); cap ("LISTEN: hear a song, write a NEW idea in its tempo / key / chords", listenB.getBounds().getUnion (listenFile.getBounds()));
    cap ("IDEAS + STARTERS", savePre.getBounds().getUnion (starters.getBounds())); cap ("MIDI", midiOut.getBounds().getUnion (follow.getBounds()));
    if (! laneBox.isEmpty() && laneFxS.size() >= 4)
    {   // LANES: the caption, the lane names, the FX column titles
        g.drawText ("LANES: own mood / take (ARP, MELODY) + own FX", laneBox.getX(), laneBox.getY(), laneBox.getWidth(), 14, juce::Justification::centredLeft);
        static const int order[4] { LaneChords, LaneArp, LaneMelody, LaneBass };
        static const char* fxN[4] { "DRIVE", "TONE", "ECHO", "SPACE" };
        g.setFont (F (8.5f));
        for (int k = 0; k < 4; ++k) { const auto kb = laneFxS[k]->getBounds(); g.setColour (P.dim); g.drawText (fxN[k], kb.getX() - 6, laneBox.getY(), kb.getWidth() + 12, 14, juce::Justification::centred); }
        g.setFont (F (10.5f));
        for (int r = 0; r < 4 && r < laneMoodS.size(); ++r) { g.setColour (ed.laneColour (order[r])); g.drawText (ArpEditor::laneName (order[r]), laneBox.getX(), laneMoodS[r]->getY(), 62, laneMoodS[r]->getHeight(), juce::Justification::centredLeft); }
    }
    if (ed.proc.listenState.load() != 0) { g.setColour (ed.proc.listenState == 4 ? P.pink : P.cyan); g.setFont (F (11.0f, false)); g.drawFittedText (ed.proc.listenMessage, listenB.getBounds().translated (0, 34).withWidth (420).withHeight (30), juce::Justification::topLeft, 2); }
}
void MorePanel::resized()
{
    auto r = getLocalBounds().reduced (16); r.removeFromTop (30);
    close.setBounds (getWidth() - 96, 10, 80, 26);
    auto row = r.removeFromTop (54).withTrimmedTop (16); genWords.setBounds (row.removeFromRight (110)); row.removeFromRight (8); words.setBounds (row);
    auto ch = r.removeFromTop (34); const int cw = ch.getWidth() / juce::jmax (1, chips.size());
    for (auto* c : chips) c->setBounds (ch.removeFromLeft (cw).reduced (2, 3));
    r.removeFromTop (8);
    auto cols = r; const int colW = cols.getWidth() / 3;
    auto c1 = cols.removeFromLeft (colW).reduced (6, 0), c2 = cols.removeFromLeft (colW).reduced (6, 0), c3 = cols.reduced (6, 0);
    auto place = [] (juce::Rectangle<int>& col, juce::Component& c, int h) { col.removeFromTop (18); c.setBounds (col.removeFromTop (h)); col.removeFromTop (6); };
    place (c1, master, 28); place (c1, groove, 28); place (c1, colour, 28); place (c1, gate, 28); place (c1, stutterS, 28);
    place (c2, bars, 28); place (c2, resync, 28); place (c2, evolveLen, 28); place (c2, phrase, 28); place (c2, melRate, 28); place (c2, reg, 28);
    {   // LANES (new): under the first two columns
        const int top = juce::jmax (c1.getY(), c2.getY()) + 10;
        laneBox = juce::Rectangle<int> (c1.getX(), top, c2.getRight() - c1.getX(), getHeight() - 30 - top);
        auto b = laneBox.withTrimmedTop (18); const int rh = juce::jmin (40, b.getHeight() / 4);
        for (int r = 0; r < laneMoodS.size(); ++r)
        {
            auto row = b.removeFromTop (rh); row.removeFromLeft (64);
            laneMoodS[r]->setBounds (row.removeFromLeft (292).reduced (0, 7)); row.removeFromLeft (5);
            laneVarB[r]->setBounds (row.removeFromLeft (72).reduced (0, 7)); row.removeFromLeft (4);
            const int kw = juce::jmax (26, row.getWidth() / 4);
            for (int k = 0; k < 4; ++k) laneFxS[r * 4 + k]->setBounds (row.removeFromLeft (kw).reduced (4, 1));
        }
    }
    { c3.removeFromTop (18); auto kr = c3.removeFromTop (28); key.setBounds (kr.removeFromLeft (kr.getWidth() / 2 - 4)); kr.removeFromLeft (8); scale.setBounds (kr); c3.removeFromTop (6); }
    { c3.removeFromTop (18); auto lr = c3.removeFromTop (30); listenB.setBounds (lr.removeFromLeft (lr.getWidth() / 2 - 4)); lr.removeFromLeft (8); listenFile.setBounds (lr); c3.removeFromTop (40); }
    { c3.removeFromTop (18); auto pr = c3.removeFromTop (30); const int w = pr.getWidth() / 3; savePre.setBounds (pr.removeFromLeft (w).reduced (2, 0)); loadPre.setBounds (pr.removeFromLeft (w).reduced (2, 0)); starters.setBounds (pr.reduced (2, 0)); c3.removeFromTop (6); }
    { auto hr = c3.removeFromTop (30); hold.setBounds (hr.removeFromLeft (150)); hr.removeFromLeft (8); pushB.setBounds (hr.removeFromLeft (juce::jmin (150, hr.getWidth()))); c3.removeFromTop (6); }
    { c3.removeFromTop (18); auto mr = c3.removeFromTop (30); const int w = mr.getWidth() / 3; midiOut.setBounds (mr.removeFromLeft (w).reduced (2, 0)); oneTrack.setBounds (mr.removeFromLeft (w).reduced (2, 0)); follow.setBounds (mr.reduced (2, 0)); c3.removeFromTop (4); }
    { auto mr = c3.removeFromTop (30); const int w = mr.getWidth() / 2; ch1B.setBounds (mr.removeFromLeft (w).reduced (2, 0)); velB.setBounds (mr.reduced (2, 0)); c3.removeFromTop (4); }
    { auto mr = c3.removeFromTop (30); bendB.setBounds (mr.removeFromLeft (mr.getWidth() / 2).reduced (2, 0)); bendSeg.setBounds (mr.reduced (2, 2)); c3.removeFromTop (6); }
    auto kr = c3.removeFromTop (70); vol->setBounds (kr.removeFromLeft (70)); verb->setBounds (kr.removeFromLeft (70));
    c3.removeFromTop (22); { auto fr = c3.removeFromTop (28); flavourBox.setBounds (fr.removeFromLeft (juce::jmin (170, fr.getWidth() / 2 + 20))); fr.removeFromLeft (10); darkS.setBounds (fr); }
}

// ================================================================= first-run hints
struct HintLayer : juce::Component
{
    ArpEditor& ed; explicit HintLayer (ArpEditor& e) : ed (e) {}
    void paint (juce::Graphics& g) override
    {
        const auto& P = ed.th.p;
        g.setColour (P.shadow.withAlpha (0.55f)); g.fillAll();
        auto bubble = [&] (juce::Rectangle<int> target, const juce::String& num, const juce::String& txt, bool below)
        {
            g.setColour (P.a1); g.drawRoundedRectangle (target.toFloat().expanded (4), 12, 3.0f);
            const auto b = bubbleRect (target, below);
            g.setColour (P.card); g.fillRoundedRectangle (b, 14); g.setColour (P.a1); g.drawRoundedRectangle (b, 14, 2.0f);
            g.setColour (P.a1); g.fillEllipse (b.getX() + 10, b.getCentreY() - 17, 34, 34);
            g.setColour (P.textOn (P.a1)); g.setFont (F (20.0f)); g.drawText (num, juce::Rectangle<float> (b.getX() + 10, b.getCentreY() - 17, 34, 34), juce::Justification::centred);
            g.setColour (P.ink); g.setFont (F (15.0f)); g.drawFittedText (txt, b.withTrimmedLeft (52).reduced (4).toNearestInt(), juce::Justification::centredLeft, 2);
        };
        bubble (ed.modeArea, "1", "pick a mood", true);
        bubble (ed.genArea, "2", "GENERATE", true);
        bubble (ed.dragAllArea, "3", "drag the MIDI into your DAW", true);
        { const auto r = startRect();
          g.setColour (P.card); g.fillRoundedRectangle (r, 8); g.setColour (P.ink); g.setFont (F (12.0f)); g.drawText ("click anywhere to start", r, juce::Justification::centred); }
    }
    juce::Rectangle<float> bubbleRect (juce::Rectangle<int> target, bool below) const
    {
        const auto c = target.getCentre(); auto b = juce::Rectangle<float> (220, 62).withCentre ({ (float) c.x, below ? (float) target.getBottom() + 52 : (float) target.getY() - 52 });
        return b.withX (juce::jlimit (8.0f, (float) getWidth() - 228.0f, b.getX()));
    }
    juce::Rectangle<float> startRect() const { return juce::Rectangle<float> (180, 26).withCentre (getLocalBounds().removeFromBottom (40).getCentre().toFloat()); }
    std::vector<juce::Rectangle<int>> focus() const   // what the hints show (the UI behind the scrim is dimmed on purpose)
    { return { bubbleRect (ed.modeArea, true).toNearestInt(), bubbleRect (ed.genArea, true).toNearestInt(), bubbleRect (ed.dragAllArea, true).toNearestInt(), startRect().toNearestInt() }; }
    void mouseUp (const juce::MouseEvent&) override { ed.proc.hintsSeen = true; setVisible (false); }
};

// ================================================================= the editor
ArpEditor::ArpEditor (ArpProcessor& p)
    : AudioProcessorEditor (p), proc (p),
      modeSeg (th, { "SUNRISE", "DUSK", "MIDNIGHT" }), modeSeg2 (th, { "TRAP", "HIP HOP" }), gen (th, "GENERATE"), seedPrev (th, "<"), seedNext (th, ">"), undoB (th, "UNDO"), redoB (th, "REDO"), saveB (th, "SAVE"), loadB (th, "LOAD"), moreB (th, "MORE"), themeB (th, "LIGHT"),
      play (th, "PLAY"), bpmB (th, "124 BPM"), tap (th, "TAP"), kickB (th, "KICK"), followB (th, "FOLLOW"), evolveB (th, "EVOLVE"),
      layerPluck (th, "+ PLUCK"), layerOct (th, "+ OCTAVE"), stabsB (th, "STABS"), soundB (th, "SOUND"), harvestB (th, "HARVEST > MELODY"),
      cycleSeg (th, { "AUTO", "3", "4", "5", "6", "7", "8" }), rangeSeg (th, { "TIGHT", "MID", "WIDE" }), rateSeg (th, { "1/8", "1/16", "1/16T", "1/32" }),
      formSeg (th, { "AAAB", "ABAB", "ABAC", "AAAA" }), staggerSeg (th, { "IN PHASE", "+2 BEATS", "+1 BAR" }), melRangeSeg (th, { "TIGHT", "MID", "WIDE" }),
      chordLen (th, { "1 BAR", "2 BARS" }), chordRhythm (th, { "SUSTAIN", "OFFBEAT", "SYNCOPATED" }), voicingSeg (th, { "SMOOTH", "POWER", "WIDE", "WEB" }), bassRate (th, { "1/8", "1/16", "OFFBEAT" }),
      lkGlide (th, "GLIDE"), lkFat (th, "FATTEN"), lkStep (th, "STEP"), lkAll (th, "SET ALL"), snapB (th, "SNAP KEY"), zoomIn (th, "+"), zoomOut (th, "-"), followRollB (th, "FOLLOW"),
      lockSel (th, "LOCK SEL"), clearSelB (th, "CLEAR SEL"), mixStems (th, { "MIX", "STEMS" })
{
    seenChordRewrite = proc.chordRewriteSeq; th.mode = proc.idea().mode; th.p = palette (th.mode, proc.themeId(), proc.ghostLook()); fromP = toP = th.p;
    lnf.refresh(); setLookAndFeel (&lnf);
    addAndMakeVisible (content); content.setInterceptsMouseClicks (false, true);
    auto add = [this] (juce::Component& c) { content.addAndMakeVisible (c); };
    // header
    add (modeSeg); modeSeg.fontH = 14.0f; modeSeg.sel = th.mode < 3 ? th.mode : -1; modeSeg.tint = [this] { return th.p.a1; };
    modeSeg.onChange = [this] (int m) { setMode (m); };
    add (modeSeg2); modeSeg2.fontH = 12.5f; modeSeg2.sel = th.mode >= 3 ? th.mode - 3 : -1; modeSeg2.tint = [this] { return th.p.a1; };   // TRAP / HIP HOP (appended moods)
    modeSeg2.onChange = [this] (int m) { setMode (Trap + m); };
    modeSeg2.setTooltip ("TRAP: half-time minor motifs, flute, 808 slides and rolls. HIP HOP: swung loops, dusty keys, round sub");
    add (gen); gen.big = true; gen.tint = [this] { return th.p.a1; }; gen.on = true; gen.onClick = [this] { proc.generateAsync (proc.idea().mode, true); roll.clearSel(); };   // composed on the worker; the new idea arrives a moment later
    gen.setTooltip ("A new idea in this mood: chords + arp + melody");
    add (seedPrev); add (seedNext); seedPrev.onClick = [this] { proc.stepHistoryAsync (-1); }; seedNext.onClick = [this] { proc.stepHistoryAsync (1); };
    add (seedLbl); seedLbl.setEditable (true); seedLbl.setJustificationType (juce::Justification::centred); seedLbl.setFont (F (13.0f));
    seedLbl.onTextChange = [this] { const auto v = seedLbl.getText().getLargeIntValue(); if (v > 0) proc.recallSeed ((uint32_t) v); };
    seedLbl.setTooltip ("SEED: click to type a seed (same mood + seed = same idea)");
    add (undoB); undoB.icon = 1; undoB.onClick = [this] { doUndo(); }; undoB.setTooltip ("Undo the last change (Cmd/Ctrl+Z)");
    add (redoB); redoB.icon = 2; redoB.onClick = [this] { doRedo(); }; redoB.setTooltip ("Redo (Cmd/Ctrl+Shift+Z or Ctrl+Y)");
    for (auto* b : { &undoB, &redoB }) b->tint = [this] { return th.p.a1; };
    add (saveB); saveB.icon = 3; saveB.tint = [this] { return th.p.green; }; saveB.onClick = [this] { openSave(); }; saveB.setTooltip ("Save this idea (lanes, chords, sound) to Documents/Ghosty/ArpEZator");
    add (loadB); loadB.icon = 4; loadB.tint = [this] { return th.p.green; }; loadB.onClick = [this] { openBrowser(); }; loadB.setTooltip ("Your saved ideas + the STARTERS");
    add (moreB); moreB.onClick = [this] { more->sync(); more->setVisible (! more->isVisible()); more->toFront (false); };
    bypassB = std::make_unique<ghosty::BypassButton> (*proc.bypassParam); add (*bypassB);
    tips = std::make_unique<juce::TooltipWindow> (this, 650);   // the one tooltip window (theme colours from the look-and-feel)
    // TRANSPOSE: the whole thing up / down (live, automatable), BAKE it into the notes, TO KEY = the smallest move to the song's key
    for (juce::Component* c : std::initializer_list<juce::Component*> { &trS, &bakeB, &keyB }) add (*c);
    trS.fmt = [] (int v) { return (v > 0 ? "+" : "") + juce::String (v) + " st"; }; trS.tint = [this] { return th.p.gold; };
    trS.setTooltip ("TRANSPOSE every lane (and the MIDI out, the drag-outs, the Hook Lab) up / down in semitones. Live: GENERATE, the moods and the Hook Lab still write in the "
                    "original key and the offset is added on the way out. Click - / +, wheel, double-click = 0, right-click = type it. TO KEY picks the smallest move to your song's key; BAKE writes it into the notes");
    trS.onChange = [this] (int v) { proc.setTranspose (v); const int f = proc.foldedCount(); toast ("TRANSPOSE " + trS.fmt (v) + (v != 0 ? "  (" + dispKey() + ")" : juce::String()) + (f > 0 ? "  - folded " + juce::String (f) + " notes an octave to stay in range" : juce::String()), 2200); repaint(); roll.repaint(); };
    trS.onRight = [this] {
        auto* w = new juce::AlertWindow ("Transpose", "Semitones (-24 .. +24):", juce::MessageBoxIconType::NoIcon, this);
        w->addTextEditor ("t", juce::String (proc.transposeNow())); w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey)); w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<ArpEditor> sp (this);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([sp, w] (int r) { if (sp && r == 1) sp->trS.set (juce::jlimit (-24, 24, w->getTextEditorContents ("t").getIntValue())); }), true); };
    bakeB.tint = [this] { return th.p.gold; }; bakeB.setTooltip ("BAKE: write the TRANSPOSE into the notes and chords (one UNDO step) and set it back to 0, to keep editing at the new pitch");
    bakeB.onClick = [this] { if (proc.transposeNow() == 0) { toast ("Nothing to bake: TRANSPOSE is 0"); return; } const auto k = dispKey(); proc.bakeTransposeNow(); roll.clearSel(); toast ("BAKED: the idea is now in " + k + " (UNDO to go back)"); refresh(); };
    keyB.tint = [this] { return th.p.gold; }; keyB.setTooltip ("TO KEY: pick your song's key (the major / minor of the idea stays); the plugin moves by the smallest step. Also: which lanes follow the TRANSPOSE");
    keyB.onClick = [this] {
        juce::PopupMenu m; const auto& I = proc.idea(); const bool minor = juce::String (keyName (I.H.key, I.H.scaleName)).endsWith ("m");
        m.addSectionHeader ("TO KEY (from " + juce::String (keyName (I.H.key, I.H.scaleName)) + ")");
        for (int r = 0; r < 12; ++r)
        {
            const int s = keyShiftTo (I.H.key, r);
            m.addItem (1 + r, juce::String (keyName (I.H.key, I.H.scaleName)) + " -> " + juce::String (keyName (r, I.H.scaleName)) + "  (" + (s > 0 ? "+" : "") + juce::String (s) + ")", true, s == proc.transposeNow());
        }
        (void) minor;
        m.addSeparator(); m.addSectionHeader ("Lanes that follow TRANSPOSE");
        static const char* nm[] { "CHORDS", "ARP", "MELODY", "BASS" };
        for (int l = 0; l < kLanes; ++l) m.addItem (100 + l, nm[l], true, (proc.transposeMask.load() >> l) & 1);
        juce::Component::SafePointer<ArpEditor> sp (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&keyB), [sp] (int r) {
            if (sp == nullptr || r <= 0) return;
            if (r >= 100) { sp->proc.transposeMask = sp->proc.transposeMask.load() ^ (1 << (r - 100)); sp->proc.setTranspose (sp->proc.transposeNow()); sp->refresh(); return; }
            const auto from = juce::String (keyName (sp->proc.idea().H.key, sp->proc.idea().H.scaleName));
            const int s = sp->proc.transposeToKey (r - 1); sp->trS.value = s; sp->trS.repaint();
            sp->toast (from + " -> " + sp->dispKey() + "  (" + (s > 0 ? "+" : "") + juce::String (s) + ")"); sp->refresh(); }); };
    add (playSeg); playSeg.fontH = 9.5f;
    playSeg.setTooltip ("PLAY: ALWAYS = plays with the transport (as before). ON NOTES = plays only while a MIDI note is held (any key), so muting the channel in your DAW "
                        "(FL's mute light) stops it and the next note starts it again in time. With FOLLOW KEYS on, the same held keys also set the chord. BYPASS wins");
    playSeg.onChange = [this] (int i) { if (auto* q = proc.apvts.getParameter ("playMode")) q->setValueNotifyingHost (q->convertTo0to1 ((float) i));
                                        toast (i ? "PLAY: ON NOTES - hold a note (draw one long note per pattern in your DAW) and it plays; no note = silent" : "PLAY: ALWAYS - plays with the transport"); };
    add (themeB); themeB.onClick = [this] { proc.theme = ArpProcessor::nextLook (proc.theme); fromP = th.p; toP = palette (th.mode, proc.themeId(), proc.ghostLook()); fadeStart = t; refresh(); };
    themeB.setTooltip ("The look: BLACK -> PURPLE -> LIGHT -> GHOST (liquid glass) (the button names the next one)");
    unlock = std::make_unique<ghosty::UnlockButton> (proc.license); add (*unlock); unlock->onChange = [this] { repaint(); };
    // transport
    syncB = std::make_unique<ghosty::SyncButton> (proc.sync); add (*syncB);
    syncB->onModeChanged = [this] { const bool own = proc.syncOwn(); proc.setSyncOwn (own); syncTransportUi(); layoutContent(); refresh();
                                    toast (own ? "SYNC: OWN - ArpEZator runs by itself: its own tempo (BPM / TAP), its own PLAY and SPACE (from the cursor). The DAW's transport is ignored"
                                               : "SYNC: DAW - the DAW's play button, tempo and position drive it (SPACE is the DAW's)", 3600); };
    add (play); play.tint = [this] { return th.p.green; };
    play.onClick = [this] {
        if (proc.syncOwn()) proc.sync.toggleOwn ((double) proc.playStartTick() / PPQ);
        else { proc.internalPlay = ! proc.internalPlay.load(); proc.userToggledPlay(); }
        refresh(); };
    play.setTooltip ("Play on its own (when the host is stopped). With the host playing it follows the host.");
    add (bpmB); bpmB.tint = [this] { return th.p.cyan; };
    bpmB.onClick = [this] {
        auto* w = new juce::AlertWindow ("Tempo", "BPM (used when the host is stopped; MIDI drags use the host tempo when there is one):", juce::MessageBoxIconType::NoIcon, this);
        w->addTextEditor ("b", juce::String (proc.idea().g.bpm)); w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey)); w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<ArpEditor> sp (this);
        if (proc.syncOwn()) w->getTextEditor ("b")->setText (juce::String (proc.sync.getOwnBpm(), 1));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([sp, w] (int r) {
            if (! sp || r != 1) return;
            if (sp->proc.syncOwn()) sp->proc.setOwnBpm (juce::jlimit (20.0, 400.0, w->getTextEditorContents ("b").getDoubleValue()));   // OWN: the plugin's own tempo
            else { const int b = juce::jlimit (40, 240, w->getTextEditorContents ("b").getIntValue()); sp->proc.change ([b] (Idea& X) { X.g.bpm = b; }, false); }
            sp->refresh(); }), true); };
    add (tap); tap.tint = [this] { return th.p.cyan; }; tap.onClick = [this] { proc.tapTempo(); };
    add (kickB); kickB.toggle = true; kickB.tint = [this] { return th.p.gold; }; kickB.onClick = [this] { proc.pushUndoPoint ("kick"); if (auto* q = proc.apvts.getParameter ("kick")) q->setValueNotifyingHost (kickB.on ? 1.0f : 0.0f); layoutContent(); };
    kickB.setTooltip ("A little kick, just for tempo + fun (never in the MIDI)");
    add (followB); followB.toggle = true; followB.tint = [this] { return th.p.pink; }; followB.onClick = [this] { if (auto* q = proc.apvts.getParameter ("follow")) q->setValueNotifyingHost (followB.on ? 1.0f : 0.0f); };
    followB.setTooltip ("FOLLOW MY KEYS: hold a chord on your MIDI keyboard and the arp / melody re-voice onto it");
    add (evolveB); evolveB.toggle = true; evolveB.tint = [this] { return th.p.a3; };
    evolveB.onClick = [this] { const bool v = evolveB.on; proc.change ([v] (Idea& X) { X.evolve = v; rebuildChords (X); for (int l = LaneArp; l < kLanes; ++l) composeLane (X, l, true); }); toast (v ? "EVOLVE: one small change every 2-4 bars, baked into a long take (drag it out)" : "EVOLVE off"); };
    dragAll = std::make_unique<DragPill> (*this, 15, "DRAG MIDI"); dragAll->huge = true; add (*dragAll);
    dragAll->setTooltip ("Drag the MIDI into your DAW: MIX = one .mid with every lane, STEMS = one .mid per lane");
    dragAudio = std::make_unique<DragPill> (*this, 15, "DRAG AUDIO"); dragAudio->huge = true; dragAudio->audio = true; add (*dragAudio);
    dragAudio->setTooltip ("Drag the preview sound as WAV (24-bit): MIX = the whole thing, STEMS = one WAV per lane (+ kick). Rendered in the background first.");
    add (mixStems); mixStems.tint = [this] { return th.p.a2; }; mixStems.fontH = 10.5f; mixStems.onChange = [this] (int) { renderStatus.clear(); repaint(); };
    mixStems.setTooltip ("MIX: one file with everything. STEMS: one file per lane, all the same length");
    add (kickVol); kickVol.setPopupDisplayEnabled (true, true, nullptr); kickVol.setDoubleClickReturnValue (true, 0.0);
    kickVolAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, laneVolId (4), kickVol);
    kickVol.onDragStart = [this] { proc.pushUndoPoint ("kick volume"); }; kickVol.setTooltip ("KICK volume");
    // lanes
    for (int l = 0; l < kLanes; ++l)
    {
        auto* c = lanes.add (new LaneCard (*this, l)); add (*c);
        static const char* dragTxt[] { "DRAG CHORDS", "DRAG ARP", "DRAG MELODY", "DRAG BASS" };
        auto* dp = lanePills.add (new DragPill (*this, 1 << l, dragTxt[l])); c->addAndMakeVisible (dp); c->drag.setVisible (false);
        dp->setName ("drag"); dp->setTooltip ("Drag this lane into your DAW as a .mid (at the host tempo)");
    }
    // sound
    add (presetBox);
    presetBox.onChange = [this] {
        if (fillingPresets) return;
        const int id = presetBox.getSelectedId(); if (id <= 0) return;
        const int choice = proc.linkAll ? id : id - 1;   // LINK ALL: 1..n = the shared sound; per lane: 0 LINKED, 1..n, last = YOUR SAMPLE
        if (choice == arpsyn::kPresets + 1 && ! proc.hasSample()) { openSample (true); toast ("YOUR SAMPLE: drop a sample on the panel (or LOAD)"); }
        proc.setLaneChoice (proc.linkAll ? -1 : soundLane(), choice);
        refresh(); };
    for (juce::Component* c : std::initializer_list<juce::Component*> { &soundTabs, &linkB, &sampleB }) add (*c);
    soundTabs.fontH = 9.5f; soundTabs.tint = [this] { return laneColour (soundLane()); };
    soundTabs.setTooltip ("SOUND FOR: the lane the sound menu edits (it follows the lane you are editing until you pick one). LINK ALL on = one sound for everything");
    soundTabs.onChange = [this] (int i) { soundTab = i; if (proc.linkAll) { proc.linkAll = false; toast ("LINK ALL off: each lane can have its own sound now"); } refresh(); };
    linkB.toggle = true; linkB.tint = [this] { return th.p.a2; };
    linkB.setTooltip ("LINK ALL: one sound for every lane (as before). Off: CHORDS, ARP, MELODY and BASS each get their own sound (or YOUR SAMPLE)");
    linkB.onClick = [this] { proc.linkAll = linkB.on; if (linkB.on) proc.setLaneChoice (-1, 1 + juce::jlimit (0, (int) arpsyn::kPresets, (int) proc.apvts.getRawParameterValue ("preset")->load())); refresh(); };
    sampleB.tint = [this] { return th.p.cyan; };
    sampleB.setTooltip ("YOUR SAMPLE: load / drop a sample (WAV, AIFF, FLAC, MP3) and play it from any lane; trim, loop, reverse, root, ADSR");
    sampleB.onClick = [this] { openSample (samplePanel == nullptr || ! samplePanel->isVisible()); };
    for (auto* b : { &layerPluck, &layerOct, &stabsB, &soundB }) { add (*b); b->toggle = true; b->tint = [this] { return th.p.a1; }; }
    layerPluck.onClick = [this] { proc.pushUndoPoint ("+ pluck"); if (auto* q = proc.apvts.getParameter ("layerPluck")) q->setValueNotifyingHost (layerPluck.on ? 1.0f : 0.0f); };
    layerOct.onClick = [this] { proc.pushUndoPoint ("+ octave"); if (auto* q = proc.apvts.getParameter ("layerOct")) q->setValueNotifyingHost (layerOct.on ? 1.0f : 0.0f); };
    stabsB.onClick = [this] { proc.pushUndoPoint ("stabs"); if (auto* q = proc.apvts.getParameter ("stabs")) q->setValueNotifyingHost (stabsB.on ? 1.0f : 0.0f); };
    soundB.onClick = [this] { if (auto* q = proc.apvts.getParameter ("sound")) q->setValueNotifyingHost (soundB.on ? 1.0f : 0.0f); };
    soundB.setTooltip ("The preview sound. Turn it off when ArpEZator drives another synth through MIDI out.");
    stabsB.setTooltip ("Chords as wide supersaw STABS (else a soft pad)");
    for (auto [id, lab] : { std::pair<const char*, const char*> { "cutoff", "CUTOFF" }, { "lfoDepth", "LFO DEPTH" }, { "lfoRate", "LFO RATE" }, { "delay", "DELAY" }, { "pump", "PUMP" }, { "reverb", "REVERB" } })
    { auto* k = macros.add (new Knob (th, proc.apvts, id, lab)); add (*k); k->onGesture = [this, l = juce::String (lab).toLowerCase()] { proc.pushUndoPoint (l); }; }
    struct PresetUndo : juce::MouseListener { ArpProcessor& p; explicit PresetUndo (ArpProcessor& q) : p (q) {} void mouseDown (const juce::MouseEvent&) override { p.pushUndoPoint ("sound preset"); } };
    presetUndo = std::make_unique<PresetUndo> (proc); presetBox.addMouseListener (presetUndo.get(), true);
    // editor area
    static const char* toolIds[] { "varyLittle", "varyMore", "newRhythm", "newNotes", "simpler", "busier", "octDown", "octUp", "new" };
    static const char* toolTxt[] { "VARY .", "VARY ..", "NEW RHYTHM", "NEW NOTES", "SIMPLER", "BUSIER", "OCT -", "OCT +", "NEW" };
    for (int i = 0; i < 9; ++i)
    {
        auto* b = tools.add (new Btn (th, toolTxt[i])); add (*b); b->setTooltip (kToolTip[i]);
        b->tint = [this] { return laneColour (lane()); };
        b->onClick = [this, id = juce::String (toolIds[i])] { tool (id); };
    }
    add (harvestB); harvestB.tint = [this] { return th.p.a1; }; harvestB.onClick = [this] {
        const bool anySel = std::find (roll.sel.begin(), roll.sel.end(), true) != roll.sel.end();
        if (! anySel && roll.selBar0 < 0) { toast ("Select arp bars (click bar numbers, shift = more) or notes (shift-drag), then HARVEST"); return; }
        proc.harvest (anySel ? roll.sel : std::vector<bool>(), juce::jmin (roll.selBar0, roll.selBar1), juce::jmax (roll.selBar0, roll.selBar1));
        roll.clearSel(); toast ("Harvested into MELODY: tweak it there, then drag it out"); };
    harvestB.setTooltip ("Copy the selected arp bars / notes into the MELODY lane");
    for (auto* s : { &cycleSeg, &rangeSeg, &rateSeg, &formSeg, &staggerSeg, &melRangeSeg, &chordLen, &chordRhythm, &voicingSeg, &bassRate }) { add (*s); s->tint = [this] { return laneColour (lane()); }; }
    cycleSeg.onChange = [this] (int i) { const int c = i == 0 ? 0 : i + 2; proc.change ([c] (Idea& X) {
        X.cycle = c;
        if (c >= 3 && X.fig.len != c) { auto& f = X.fig; while (f.len < c) { f.s[(size_t) f.len] = f.s[(size_t) ((f.len - 1) % juce::jmax (1, f.len))]; f.s[(size_t) f.len].acc = false; ++f.len; } f.len = c; }
        rerealizeArp (X); }); };
    cycleSeg.setTooltip ("Figure length: odd lengths drift against the 16-step bar (polymeter) and re-sync at phrase starts");
    rangeSeg.onChange = [this] (int i) { proc.change ([i] (Idea& X) { X.arpRange = i + 1; rerealizeArp (X); }); };
    rangeSeg.setTooltip ("How far the chord-tone ladder spreads: 1, 2 or 3 octaves");
    rateSeg.onChange = [this] (int i) { const int spb = std::vector<int> { 8, 16, 24, 32 }[(size_t) i]; proc.change ([spb] (Idea& X) { X.arpSpb = spb; rerealizeArp (X); }); };
    formSeg.onChange = [this] (int i) { proc.change ([i] (Idea& X) { X.form = i; composeLane (X, LaneMelody, true); }); };
    staggerSeg.onChange = [this] (int i) { const int s = i == 0 ? 0 : i == 1 ? 2 * PPQ : BAR; proc.change ([s] (Idea& X) { X.stagger = s; composeLane (X, LaneMelody, true); }); };
    staggerSeg.setTooltip ("STAGGER: start the melody phrase late against the chord loop, so the loop keeps pulling forward");
    melRangeSeg.onChange = [this] (int i) { proc.change ([i] (Idea& X) { X.melRange = i + 1; composeLane (X, LaneMelody, true); }); };
    chordLen.onChange = [this] (int i) { proc.change ([i] (Idea& X) { X.chordBars = i + 1; rebuildChords (X); for (int l = LaneArp; l < kLanes; ++l) composeLane (X, l, true); }); };
    voicingSeg.fontH = 10.0f; chordRhythm.fontH = 10.5f;
    chordRhythm.onChange = [this] (int i) { proc.change ([i] (Idea& X) { X.chordRhythm = i; rebuildChords (X); }); };   // (rebuild: EDITED CHORDS learn the rhythm changed)
    voicingSeg.onChange = [this] (int i) { proc.change ([i] (Idea& X) { X.voicing = i; rebuildChords (X); refitLanesToChords (X); }); };
    for (auto* b : { &chordOctDn, &chordOctUp }) { add (*b); b->tint = [this] { return laneColour (lane()); }; }
    chordOctDn.setTooltip ("CHORDS OCTAVE DOWN: every chord an octave lower (the ARP, MELODY and BASS stay as they are)");
    chordOctUp.setTooltip ("CHORDS OCTAVE UP: every chord an octave higher (the ARP, MELODY and BASS stay as they are)");
    chordOctDn.onClick = [this] { chordOctave (-1); }; chordOctUp.onClick = [this] { chordOctave (1); };
    add (chordOctLbl); chordOctLbl.setFont (F (11.0f)); chordOctLbl.setJustificationType (juce::Justification::centred); chordOctLbl.setText ("0", juce::dontSendNotification);
    chordOctLbl.setTooltip ("CHORDS OCTAVE: how many octaves the chord lane is moved (0 = where the voicing puts it)");
    bassRate.onChange = [this] (int i) { proc.change ([i] (Idea& X) { X.bassRate = i; composeLane (X, LaneBass, true); X.lanes[LaneBass].mute = false; }); };
    add (stutter); stutter.setSliderStyle (juce::Slider::LinearHorizontal); stutter.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0); stutter.setRange (0, 1, 0.01);
    stutter.onDragEnd = [this] { const double v = stutter.getValue(); proc.change ([v] (Idea& X) { X.stutter = v; composeLane (X, LaneMelody, true); }); };
    add (stutterLbl); stutterLbl.setText ("STUTTER", juce::dontSendNotification); stutterLbl.setFont (F (10.5f)); stutterLbl.setJustificationType (juce::Justification::centredRight);
    add (pattern); add (chips); add (roll);
    for (auto* b : { &lkGlide, &lkFat, &lkStep, &lkAll }) add (*b);
    lkGlide.tint = [this] { return th.p.pink; }; lkFat.tint = [this] { return th.p.gold; }; lkStep.tint = [this] { return th.p.cyan; }; lkAll.tint = [this] { return th.p.a3; };
    lkGlide.onClick = [this] { linkSet (1); }; lkFat.onClick = [this] { linkSet (2); }; lkStep.onClick = [this] { linkSet (0); };
    lkAll.onClick = [this] { const int ln = lane(), li = roll.selLink; if (li < 0) return; proc.change ([ln, li] (Idea& X) { auto& ns = X.lanes[ln].notes; if (li + 1 >= (int) ns.size()) return; const Link c = ns[(size_t) li].l; for (auto& n : ns) n.l = c; X.lanes[ln].edited = true; }); toast ("All lines set like this one"); };
    add (lkAmt); lkAmt.setSliderStyle (juce::Slider::LinearHorizontal); lkAmt.setTextBoxStyle (juce::Slider::TextBoxLeft, true, 74, 20); lkAmt.setRange (0, 1, 0.01);
    lkAmt.textFromValueFunction = [this] (double v) { const int ln = lane(), li = roll.selLink; const auto& ns = proc.idea().lanes[ln].notes;
        const bool fat = li >= 0 && li + 1 < (int) ns.size() && ns[(size_t) li].l.type == 2; return (fat ? "FAT " : "GLIDE ") + juce::String (juce::roundToInt (v * 100)) + "%"; };
    lkAmt.setTooltip ("GLIDE: where the slide starts (pink). FATTEN: how much louder / fatter the next note is (gold)");
    lkAmt.onDragStart = [this] { const int ln = lane(), li = roll.selLink; if (li < 0) return; const auto& ns = proc.idea().lanes[ln].notes; if (li + 1 < (int) ns.size()) proc.pushUndoPoint (ns[(size_t) li].l.type == 1 ? "glide amount" : "fatten amount"); };
    lkAmt.onValueChange = [this] { const int ln = lane(), li = roll.selLink; if (li < 0) return; const float v = (float) lkAmt.getValue();
        proc.change ([ln, li, v] (Idea& X) { auto& ns = X.lanes[ln].notes; if (li + 1 >= (int) ns.size()) return; auto& l = ns[(size_t) li].l; if (l.type == 0) return; if (l.type == 2) l.f = v; else l.w = juce::jmin (0.97f, v); X.lanes[ln].edited = true; }, false); };
    add (lkInfo); lkInfo.setFont (F (10.5f));
    for (auto* b : { &snapB, &zoomIn, &zoomOut, &followRollB, &lockSel, &clearSelB }) { add (*b); b->tint = [this] { return th.p.a2; }; }
    snapB.toggle = true; snapB.on = true; snapB.onClick = [this] { snapKey = snapB.on; };
    snapB.setTooltip ("SNAP KEY: notes you add or drag land only on notes of the key / the chord");
    followRollB.toggle = true; followRollB.on = true; followRollB.onClick = [this] { followPlay = followRollB.on; };
    add (loopB); loopB.toggle = true; loopB.tint = [this] { return th.p.cyan; };
    loopB.setTooltip ("LOOP SECTION (L): play only the loop in the timeline strip, over and over. Right-drag in the strip (two-finger drag on a trackpad) to make one, drag its ends to resize, the middle to move; double-click / ESC clears it; its MIDI / WAV grips drag it out");
    loopB.onClick = [this] { toggleLoopSection(); };
    add (dragSecSeg); dragSecSeg.fontH = 8.5f; dragSecSeg.tint = [this] { return th.p.cyan; };
    dragSecSeg.setTooltip ("DRAG: WHOLE = the whole idea, SECTION = just the looped section (starting at 0); MIDI, AUDIO and STEMS");
    dragSecSeg.onChange = [this] (int i) { proc.drawSection = i; renderStatus.clear(); repaint(); };
    zoomIn.onClick = [this] { roll.zoomAround (roll.kx + (roll.getWidth() - roll.kx) * 0.5f, 0.5); };
    zoomOut.onClick = [this] { roll.zoomAround (roll.kx + (roll.getWidth() - roll.kx) * 0.5f, 2.0); };
    zoomIn.setTooltip ("Zoom in (or pinch / Cmd+wheel over the roll)"); zoomOut.setTooltip ("Zoom out");
    lockSel.onClick = [this] { const int ln = lane(); const auto s = roll.sel; const int b0 = juce::jmin (roll.selBar0, roll.selBar1), b1 = juce::jmax (roll.selBar0, roll.selBar1);
        const bool anySel = std::find (s.begin(), s.end(), true) != s.end();
        if (! anySel && b0 < 0) { toast ("Select notes (shift-drag) or bars (click bar numbers) first"); return; }
        bool nowLocked = false;
        const bool wasLocked = selectionLocked (proc.idea(), ln, s, b0, b1);
        proc.change ([ln, s, b0, b1, anySel, &nowLocked] (Idea& X) { if (anySel) editLane (X, ln); nowLocked = toggleLocks (X, ln, s, b0, b1); }, true, wasLocked ? "unlock" : "lock");
        toast (nowLocked ? "Locked: VARY and friends leave it alone (UNLOCK SEL undoes it)" : "Unlocked"); refresh(); };
    clearSelB.onClick = [this] { roll.clearSel(); roll.repaint(); };
    // ---- HOOK LAB + its lane controls
    add (hookLabB); hookLabB.big = true; hookLabB.tint = [this] { return th.p.gold; }; hookLabB.onClick = [this] { openHookLab (hookLab == nullptr || ! hookLab->isVisible()); };
    hookLabB.setTooltip ("HOOK LAB: 16 hooks over these chords as tiles; click to hear, star the keepers, FIND THE MAGIC, COMBINE, MAKE IT A SONG");
    auto reMel = [] (Idea& X) { if (! X.lanes[LaneMelody].edited) composeLane (X, LaneMelody, true); };
    add (styleSeg); styleSeg.setTooltip ("COMPOSITION STYLE (independent of the mood): how the melody behaves");
    styleSeg.onChange = [this, reMel] (int b) { const int i = styleAt (proc.idea().mode, b); proc.change ([i, reMel] (Idea& X) { X.style = i; reMel (X); }, true, "STYLE"); toast (juce::String ("STYLE: ") + styleName (i)); };
    add (candySeg); candySeg.setTooltip ("EAR CANDY: rare special moments (grace notes, octave answers, run-ups, ratchets...), a small budget per 4 / 8 bars");
    candySeg.onChange = [this, reMel] (int i) { proc.change ([i, reMel] (Idea& X) { X.candy = i; reMel (X); }, true, "EAR CANDY"); };
    add (evolveModeSeg); evolveModeSeg.setTooltip ("EVOLVE shape: SUBTLE (one small change), GROW (more and more), WAVE (breathes), JOURNEY (travels away and comes home). The strongest motif stays");
    evolveModeSeg.onChange = [this] (int i) {   // OFF, SUBTLE (the classic EVOLVE), GROW, WAVE, JOURNEY; lanes the user edited are left alone
        juce::StringArray redone, left;
        proc.change ([i, &redone, &left] (Idea& X) {
            X.evolve = i > 0; if (i > 0) X.evolveMode = i - 1;
            rebuildChords (X);
            static const char* nm[] { "CHORDS", "ARP", "MELODY", "BASS" };
            for (int l = LaneArp; l < kLanes; ++l) { if (X.lanes[l].edited) left.add (nm[l]); else { composeLane (X, l, true); redone.add (nm[l]); } }
        }, true, i == 0 ? juce::String ("EVOLVE OFF") : juce::String ("EVOLVE ") + evolveModeName (i - 1));
        toast ((i == 0 ? juce::String ("EVOLVE off") : juce::String ("EVOLVE ") + evolveModeName (i - 1) + (i > 1 ? ": the strongest motif stays" : ""))
               + (left.isEmpty() ? juce::String() : " (" + left.joinIntoString (" + ") + " kept as you edited)")); };
    for (auto* sg : { &dirSeg, &contourSeg, &accentSeg }) add (*sg);
    dirSeg.setTooltip ("ARP DIRECTION: the order the figure's notes play in (the same notes)");
    contourSeg.setTooltip ("ARP CONTOUR: the arp's shape across the whole chord loop");
    accentSeg.setTooltip ("ARP ACCENT: which steps lean in, and how the emphasis develops");
    dirSeg.onChange = [this] (int i) { proc.change ([i] (Idea& X) { X.arpDir = i; rerealizeArp (X); }, true, "ARP DIRECTION"); };
    contourSeg.onChange = [this] (int i) { proc.change ([i] (Idea& X) { X.arpContour = i; rerealizeArp (X); }, true, "ARP CONTOUR"); };
    accentSeg.onChange = [this] (int i) { proc.change ([i] (Idea& X) { X.arpAccent = i; rerealizeArp (X); }, true, "ARP ACCENT"); };
    // IDM: the arp's METER (accent groups + the euclid cycle, against the 4/4 bars) and a EUCLIDEAN rhythm with ROTATE
    for (juce::Component* c : std::initializer_list<juce::Component*> { &arpMeterSeg, &arpEucS, &arpRotS }) add (*c);
    arpMeterSeg.tint = [this] { return th.p.cyan; }; arpEucS.tint = [this] { return th.p.cyan; }; arpRotS.tint = [this] { return th.p.cyan; };
    arpMeterSeg.setTooltip ("ARP METER: accents in 3/4, 5/4, 7/8 or 9/8 groups (7/8 = 2+2+3) that phase against the 4/4 bars; EUCLID uses the same cycle");
    arpEucS.setTooltip ("ARP EUCLID: keep only this many evenly spread steps of the cycle (the figure's order stays). OFF = every step");
    arpRotS.setTooltip ("ROTATE the euclidean rhythm by steps");
    arpEucS.fmt = [this] (int v) { return v == 0 ? juce::String ("OFF") : juce::String (v) + "/" + juce::String (juce::jmax (2, meterSteps (proc.idea().arpMeter) * proc.idea().arpSpb / 16)); };
    arpMeterSeg.onChange = [this] (int i) { proc.change ([i] (Idea& X) { X.arpMeter = i; rerealizeArp (X); }, true, "ARP METER"); toast (juce::String ("ARP METER ") + meterName (i)); };
    arpEucS.onChange = [this] (int v) { proc.change ([v] (Idea& X) { X.arpEuclid = v; rerealizeArp (X); }, true, "ARP EUCLID"); };
    arpRotS.onChange = [this] (int v) { proc.change ([v] (Idea& X) { X.arpEuclidRot = v; rerealizeArp (X); }, true, "ARP ROTATE"); };
    add (align);
    for (auto* sl : { &tensionS, &simS }) { add (*sl); sl->setSliderStyle (juce::Slider::LinearHorizontal); sl->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0); }
    tensionS.setRange (-0.1, 1.0, 0.05); tensionS.setDoubleClickReturnValue (true, -0.1);
    tensionS.setTooltip ("MELODIC TENSION: left = stable chord tones, right = passing tones, suspensions, 9ths / 7ths, resolved chromatic notes. Far left (double-click) = AUTO");
    tensionCommit.fn = [this, reMel] { if (tensionS.isMouseButtonDown()) { tensionCommit.startTimer (150); return; }
                                        const double v = tensionS.getValue() < 0 ? -1.0 : tensionS.getValue(); if (std::abs (v - proc.idea().tension) < 1e-9) return;
                                        proc.change ([v, reMel] (Idea& X) { X.tension = v; reMel (X); }, true, "TENSION"); };
    tensionS.onValueChange = [this] { const double v = tensionS.getValue(); tensionLbl.setText (v < 0 ? "TENSION AUTO" : "TENSION " + juce::String (juce::roundToInt (v * 100)) + "%", juce::dontSendNotification); tensionCommit.startTimer (150); };   // drag, wheel or keys: commits 150 ms after the last move
    for (auto* sl : { &tensionS, &simS }) { sl->setPopupDisplayEnabled (true, true, this); sl->textFromValueFunction = [] (double v) { return v < 0 ? juce::String ("AUTO") : juce::String (juce::roundToInt (v * 100)) + "%"; }; }
    simS.setRange (0, 1, 0.05); simS.setValue (0.6, juce::dontSendNotification);
    simS.setTooltip ("SIMILARITY of the RESPONSE: right = the answer copies the call's rhythm and shape, left = a fresh answer");
    simS.onValueChange = [this] { simLbl.setText ("SIMILAR " + juce::String (juce::roundToInt (simS.getValue() * 100)) + "%", juce::dontSendNotification); };
    for (auto* lb : { &tensionLbl, &simLbl, &evolveLbl }) { add (*lb); lb->setFont (F (10.5f)); lb->setJustificationType (juce::Justification::centredRight); }
    tensionLbl.setText ("TENSION AUTO", juce::dontSendNotification); evolveLbl.setText ("EVOLVE", juce::dontSendNotification); simLbl.setText ("SIMILAR 60%", juce::dontSendNotification);
    add (respondB); respondB.tint = [this] { return th.p.a1; }; respondB.setTooltip ("GENERATE RESPONSE: the second half of the phrase answers the first (call and response, fitted to the chords)");
    respondB.onClick = [this] { proc.respond (simS.getValue()); toast ("RESPONSE: the second half answers the first (SIMILAR " + juce::String (juce::roundToInt (simS.getValue() * 100)) + "%)"); };
    add (motifB); motifB.toggle = true; motifB.tint = [this] { return th.p.gold; };
    motifB.setTooltip ("LOCK MOTIF: the selected melody notes (or the strongest motif) come back in every phrase, through EVOLVE and regenerating");
    motifB.onClick = [this] { proc.lockMotif (roll.sel); toast (proc.idea().motif.empty() ? "Motif unlocked" : "MOTIF LOCKED: it comes back in every phrase"); };
    hookLab = std::make_unique<HookLabPanel> (*this); content.addChildComponent (*hookLab);
    samplePanel = std::make_unique<SamplePanel> (*this); content.addChildComponent (*samplePanel);
    more = std::make_unique<MorePanel> (*this); content.addChildComponent (*more);
    auto* hl = new HintLayer (*this); content.addChildComponent (hl); hl->setName ("hints");
    if (! proc.hintsSeen) hl->setVisible (true);
    typeIn = std::make_unique<ghosty::TypeIn> (*this);
    applySliderConvention (content);
    sendLookAndFeelChange();   // children built after setLookAndFeel (slider text boxes, text fields) take the theme colours too
    proc.asyncUi = true;
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) kW / (double) kH);
    setResizeLimits (kW * 6 / 10, kH * 6 / 10, kW * 2, kH * 2);
    setSize (kW, kH);
    refresh();
    startTimerHz (30);
}

ArpEditor::~ArpEditor()
{
    proc.asyncUi = false;
    std::function<void (juce::Component&)> unhook = [&] (juce::Component& r) { for (auto* c : r.getChildren()) { if (dynamic_cast<juce::Slider*> (c) != nullptr) c->removeMouseListener (&wheelStep); unhook (*c); } };
    unhook (content);
    stopTimer();
    presetBox.removeMouseListener (presetUndo.get());
    for (int i = content.getNumChildComponents(); --i >= 0;) if (content.getChildComponent (i)->getName() == "hints") delete content.getChildComponent (i);
    setLookAndFeel (nullptr);
}

void ArpEditor::selectLane (int l) { proc.selLane = juce::jlimit (0, kLanes - 1, l); roll.clearSel(); roll.pitchManual = false; refresh(); layoutContent(); repaint(); }
void ArpEditor::doUndo() { const auto l = proc.undo(); roll.clearSel(); toast (l.isEmpty() ? juce::String ("Nothing to undo") : "Undo: " + l, 1000); }
void ArpEditor::doRedo() { const auto l = proc.redo(); roll.clearSel(); toast (l.isEmpty() ? juce::String ("Nothing to redo") : "Redo: " + l, 1000); }
void ArpEditor::tool (const juce::String& id)
{
    const int ln = lane();
    const bool anySel = std::find (roll.sel.begin(), roll.sel.end(), true) != roll.sel.end();
    std::vector<bool> s = roll.sel;
    if (! anySel && roll.selBar0 >= 0)
    {   // a bar selection works like selecting its notes
        const auto& ns = proc.idea().lanes[ln].notes; s.assign (ns.size(), false);
        const int b0 = juce::jmin (roll.selBar0, roll.selBar1), b1 = juce::jmax (roll.selBar0, roll.selBar1);
        for (size_t i = 0; i < ns.size(); ++i) { const int b = ns[i].t / BAR; s[i] = b >= b0 && b <= b1; }
    }
    if (ln == LaneChords && id == "new") { proc.laneTool (ln, "newChords", nullptr); toast ("New chords (the other lanes keep their rhythm)"); }
    else proc.laneTool (ln, id, &s);
    roll.clearSel();
}
void ArpEditor::chordOctave (int d)
{   // the step starts from what is applied; a step past +-2 or past MIDI 24..108 stops (as far as fits) with a toast
    const auto& I = proc.idea(); const int want = I.chordOctUsed + d;
    const int fit = want < -2 || want > 2 ? I.chordOctUsed : chordOctFit (I, want);
    const juce::String limit = d > 0 ? "CHORDS: highest octave" : "CHORDS: lowest octave";
    if (fit == I.chordOctUsed) { toast (limit, 1600); refresh(); return; }
    proc.change ([fit] (Idea& X) { setChordOct (X, fit); }, true, d > 0 ? "chords octave up" : "chords octave down");
    if (fit != want) toast (limit, 1600);
    else toast ("CHORDS " + juce::String (fit == 0 ? "back at their own octave" : (fit > 0 ? "+" : "") + juce::String (fit) + (std::abs (fit) == 1 ? " octave" : " octaves")) + " (ARP, MELODY and BASS stay)", 1400);
    refresh();
}
void ArpEditor::linkSet (int type)
{
    const int ln = lane(), li = roll.selLink; if (li < 0) { toast ("Click a line between two notes first"); return; }
    proc.change ([ln, li, type] (Idea& X) { auto& ns = X.lanes[ln].notes; if (li + 1 >= (int) ns.size()) return; auto& l = ns[(size_t) li].l; l.type = type; if (type == 2) l.f = juce::jmax (0.5f, l.f); X.lanes[ln].edited = true; });
}
void ArpEditor::setMode (int m)
{
    if (m == proc.idea().mode) return;
    fromP = th.p; toP = palette (m, proc.themeId(), proc.ghostLook()); fadeStart = t; th.mode = m;
    const bool had = proc.modeHasIdea (m);
    proc.switchMode (m);   // each mode keeps its own idea (only GENERATE replaces it)
    selectLane (m == Sunrise || urbanMood (m) ? LaneMelody : LaneArp);
    if (had) { toast (juce::String (modeName (m)) + ": back to your idea", 1400); return; }
    toast (juce::String (modeName (m)) + ": " + (m == Sunrise ? "feel-good hooks over big stabs" : m == Dusk ? "bittersweet, major + minor" : m == Trap ? "half-time minor motifs, a breathy flute, 808 slides and rolls"
                                                 : m == HipHop ? "swung loops that repeat and vary, dusty keys, a round sub" : "dark, hypnotic, evolving arps"));
}
void ArpEditor::startDrag (int mask, juce::Component* src)
{   // (every caller asked ghosty::exportAllowed; the writer and the drag re-check silently)
    if (! proc.exportUnlocked()) return;
    const auto f = proc.writeMidiFile (mask, proc.oneTrackDrag);
    if (f.existsAsFile()) ghosty::exportgate::dragFiles (proc.license, { f.getFullPathName() }, src);
}

// ================================================================= SAVE prompt + LOAD browser (drawn in the plugin)
namespace
{
struct Veil : juce::Component
{
    ArpEditor& ed; explicit Veil (ArpEditor& e) : ed (e) {}
    juce::Rectangle<int> cardR;
    void paintCard (juce::Graphics& g, const juce::String& title)
    {
        const auto& P = ed.th.p;
        g.fillAll (P.shadow.withAlpha (0.45f));
        fillCard (g, cardR.toFloat(), 16, P); g.setColour (P.a1); g.drawRoundedRectangle (cardR.toFloat().reduced (1), 16, 1.6f);
        g.setColour (P.ink); g.setFont (F (17.0f)); g.drawText (title, cardR.getX() + 20, cardR.getY() + 14, cardR.getWidth() - 40, 24, juce::Justification::centredLeft);
    }
    void mouseUp (const juce::MouseEvent& e) override { if (! cardR.contains (e.getPosition())) ed.closeOverlay(); }
};

struct SavePrompt : Veil
{
    juce::TextEditor name; Btn save, cancel; juce::String note;
    explicit SavePrompt (ArpEditor& e) : Veil (e), save (e.th, "SAVE"), cancel (e.th, "CANCEL")
    {
        addAndMakeVisible (name); addAndMakeVisible (save); addAndMakeVisible (cancel);
        name.setFont (F (18.0f, false)); name.setText (juce::String (modeName (ed.proc.idea().mode)).toLowerCase() + " idea " + juce::String (ed.proc.idea().seedUsed % 1000)); name.setSelectAllWhenFocused (true);
        name.setColour (juce::TextEditor::textColourId, ed.th.p.ink); name.setColour (juce::TextEditor::backgroundColourId, ed.th.p.card2); name.applyColourToAllText (ed.th.p.ink);
        name.onReturnKey = [this] { doSave(); }; name.onEscapeKey = [this] { ed.closeOverlay(); };
        save.icon = 3; save.on = true; save.tint = [this] { return ed.th.p.green; }; save.onClick = [this] { doSave(); };
        cancel.onClick = [this] { ed.closeOverlay(); };
    }
    void doSave()
    {
        const auto n = name.getText().trim(); if (n.isEmpty()) return;
        const auto dir = ArpProcessor::presetDir(); const auto want = dir.getChildFile (n.retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 #-").trim() + ".arpz");
        const bool existed = want.existsAsFile();
        if (ed.proc.savePreset (n)) ed.toast (existed ? "Saved as a new copy (\"" + n + "\" was taken, nothing overwritten)" : "Saved \"" + n + "\" (LOAD brings it back)");
        ed.closeOverlay();
    }
    void visibilityChanged() override { if (isShowing()) name.grabKeyboardFocus(); }
    void resized() override
    {
        cardR = juce::Rectangle<int> (460, 190).withCentre (getLocalBounds().getCentre());
        auto r = cardR.reduced (20); r.removeFromTop (36);
        name.setBounds (r.removeFromTop (40)); r.removeFromTop (12);
        auto b = r.removeFromTop (40); cancel.setBounds (b.removeFromRight (110)); b.removeFromRight (8); save.setBounds (b.removeFromRight (130));
    }
    void paint (juce::Graphics& g) override
    {
        paintCard (g, "SAVE THIS IDEA");
        g.setColour (ed.th.p.dim); g.setFont (F (10.5f, false));
        g.drawText ("Documents/Ghosty/ArpEZator  (never overwrites: a taken name gets (2))", cardR.getX() + 20, cardR.getBottom() - 28, cardR.getWidth() - 40, 16, juce::Justification::centredLeft);
    }
};

struct Browser : Veil, juce::ListBoxModel
{
    struct Item { juce::String name; juce::File file; int starter = -1; };
    juce::TextEditor search; juce::ListBox list { "ideas", this }; Btn load, cancel, del;
    std::vector<Item> all, shown; juce::String before; bool previewed = false; int armDelete = -1;
    explicit Browser (ArpEditor& e) : Veil (e), load (e.th, "LOAD"), cancel (e.th, "CANCEL"), del (e.th, "DELETE")
    {
        before = ed.proc.sessionString();
        addAndMakeVisible (search); addAndMakeVisible (list); addAndMakeVisible (load); addAndMakeVisible (cancel); addAndMakeVisible (del);
        search.setFont (F (15.0f, false)); search.setTextToShowWhenEmpty ("search", ed.th.p.dim); search.onTextChange = [this] { filter(); };
        search.onEscapeKey = [this] { revert(); };
        list.setRowHeight (30); list.setColour (juce::ListBox::backgroundColourId, ed.th.p.card2);
        load.icon = 4; load.on = true; load.tint = [this] { return ed.th.p.green; }; load.onClick = [this] { if (! previewed) preview (list.getSelectedRow()); ed.closeOverlay(); };
        cancel.onClick = [this] { revert(); };
        del.tint = [this] { return ed.th.p.pink; }; del.onClick = [this] { remove(); };
        rescan();
    }
    void rescan()
    {
        all.clear();
        auto files = ArpProcessor::presetDir().findChildFiles (juce::File::findFiles, false, "*.arpz"); files.sort();
        for (auto& f : files) all.push_back ({ f.getFileNameWithoutExtension(), f, -1 });
        const auto st = ed.proc.starterNames(); for (int i = 0; i < st.size(); ++i) all.push_back ({ "STARTER  " + st[i], {}, i });
        filter();
    }
    void filter()
    {
        shown.clear(); const auto q = search.getText().trim();
        for (auto& it : all) if (q.isEmpty() || it.name.containsIgnoreCase (q)) shown.push_back (it);
        list.updateContent(); list.repaint(); armDelete = -1; del.text = "DELETE";
    }
    void preview (int row)
    {
        if (row < 0 || row >= (int) shown.size()) return;
        const auto& it = shown[(size_t) row];
        if (it.starter >= 0) ed.proc.loadStarter (it.starter); else ed.proc.loadPreset (it.file);
        previewed = true; ed.selectLane (ed.proc.idea().mode == Midnight ? LaneArp : LaneMelody);
        if (! ed.proc.internalPlay.load() && ! ed.proc.hostRunning.load()) ed.proc.internalPlay = true;   // hear it
    }
    void revert() { if (previewed) ed.proc.restoreSession (before); ed.closeOverlay(); }
    void remove()
    {
        const int row = list.getSelectedRow(); if (row < 0 || row >= (int) shown.size() || shown[(size_t) row].starter >= 0) { ed.toast ("Pick one of YOUR ideas to delete (starters stay)"); return; }
        if (armDelete != row) { armDelete = row; del.text = "SURE? DELETE"; del.on = true; del.repaint(); return; }
        shown[(size_t) row].file.moveToTrash(); ed.toast ("Deleted \"" + shown[(size_t) row].name + "\""); del.on = false; rescan();
    }
    int getNumRows() override { return (int) shown.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        if (row < 0 || row >= (int) shown.size()) return;
        const auto& P = ed.th.p; const auto& it = shown[(size_t) row];
        if (selected) { g.setColour (P.a1.withAlpha (0.35f)); g.fillRoundedRectangle (2, 1, (float) w - 4, (float) h - 2, 6); }
        g.setColour (it.starter >= 0 ? P.dim : P.ink); g.setFont (F (13.5f, it.starter < 0)); g.drawText (it.name, 12, 0, w - 24, h, juce::Justification::centredLeft, true);
    }
    void selectedRowsChanged (int row) override { armDelete = -1; del.text = "DELETE"; del.on = false; del.repaint(); preview (row); }
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override { preview (row); ed.closeOverlay(); }
    void resized() override
    {
        cardR = juce::Rectangle<int> (640, 560).withCentre (getLocalBounds().getCentre());
        auto r = cardR.reduced (20); r.removeFromTop (36);
        search.setBounds (r.removeFromTop (34)); r.removeFromTop (10);
        auto b = r.removeFromBottom (40); r.removeFromBottom (10);
        list.setBounds (r);
        del.setBounds (b.removeFromLeft (130)); cancel.setBounds (b.removeFromRight (110)); b.removeFromRight (8); load.setBounds (b.removeFromRight (130));
    }
    void paint (juce::Graphics& g) override { paintCard (g, "LOAD AN IDEA  (click = hear it, LOAD = keep it, CANCEL = back)"); }
};
} // namespace

void ArpEditor::closeOverlay() { if (overlay != nullptr) { auto* o = overlay.release(); juce::MessageManager::callAsync ([o] { delete o; }); o->setVisible (false); } refresh(); }
void ArpEditor::openSave() { closeOverlay(); overlay = std::make_unique<SavePrompt> (*this); content.addAndMakeVisible (*overlay); overlay->setBounds (0, 0, kW, kH); overlay->toFront (true); }
void ArpEditor::openBrowser() { closeOverlay(); overlay = std::make_unique<Browser> (*this); content.addAndMakeVisible (*overlay); overlay->setBounds (0, 0, kW, kH); overlay->toFront (true); }

// ================================================================= drag-out: MIDI / AUDIO, mix / stems
juce::String ArpEditor::audioKey (int mask, bool st) const
{
    double rate = proc.getSampleRate(); if (rate < 8000) rate = 48000;
    return juce::String (proc.version.load()) + "|" + juce::String (mask) + "|" + (st ? "S" : "M") + "|" + juce::String (proc.hostOrIdeaBpm()) + "|" + juce::String (rate) + "|" + juce::String (proc.drawMode()) + (proc.drawMode() == 1 && proc.section().valid() ? "@" + juce::String (proc.section().start) + "-" + juce::String (proc.section().end) : juce::String()) + "|"
         + juce::String (juce::String (proc.apvts.copyState().toXmlString()).hashCode64());
}
void ArpEditor::startDragOut (int mask, bool audio, bool st, juce::Component* src, bool gated)
{   // gated: the caller already asked ghosty::exportAllowed with its own path id (else this asks for a plain lane drag)
    if (! gated && ! proc.exportAllowed (src != nullptr ? src : (juce::Component*) this, audio ? (st ? "arp.pill.audio-stems" : "arp.pill.audio") : (st ? "arp.pill.midi-stems" : "arp.pill.midi")))
        { toast ("Drag-out is part of the full version (UNLOCK)"); return; }
    if (! proc.exportUnlocked()) return;   // back-stop
    if (! audio)
    {
        if (! st) { startDrag (mask, src); return; }
        juce::StringArray files;   // STEMS: one .mid per lane, dragged together
        for (int l = 0; l < kLanes; ++l) if ((mask & (1 << l)) && ! proc.idea().lanes[l].mute && (l == LaneChords || ! proc.idea().lanes[l].notes.empty())) files.add (proc.writeMidiFile (1 << l, true).getFullPathName());
        files.removeEmptyStrings();
        if (! files.isEmpty() && src != nullptr) ghosty::exportgate::dragFiles (proc.license, files, src);
        return;
    }
    const auto key = audioKey (mask, st);
    if (key == audioJob.key && ! audioJob.running && ! audioJob.files.isEmpty())
    {
        bool ok = true; for (auto& f : audioJob.files) ok &= juce::File (f).existsAsFile();
        if (ok) { if (src != nullptr) ghosty::exportgate::dragFiles (proc.license, audioJob.files, src); return; }
    }
    audioDragSrc = src;
    if (audioJob.running && key == audioJob.key) return;   // already on it
    // snapshot on the message thread; render on a background thread
    double rate = proc.getSampleRate(); if (rate < 8000) rate = 48000;
    const double bpm = proc.hostOrIdeaBpm();
    auto job = std::make_shared<ArpProcessor::RenderJob> (proc.renderJob (rate, bpm));
    const bool kickOn = proc.apvts.getRawParameterValue ("kick")->load() > 0.5f;
    std::vector<std::pair<int, juce::String>> parts;   // laneMask, file label
    if (! st) parts.push_back ({ mask == 15 ? -1 : mask, mask == 15 ? juce::String ("mix") : ArpEditor::laneName (juce::roundToInt (std::log2 ((double) mask))).toLowerCase() });
    else
    {
        for (int l = 0; l < kLanes; ++l) if (! job->idea.lanes[l].mute && (l == LaneChords || ! job->idea.lanes[l].notes.empty())) parts.push_back ({ 1 << l, ArpEditor::laneName (l).toLowerCase() });
        if (kickOn) parts.push_back ({ 16, "kick" });
    }
    const auto dir = ArpProcessor::presetDir().getChildFile ("Audio"); dir.createDirectory();
    const juce::String base = "ArpEZator " + juce::String (keyName (job->idea.g.key, job->idea.g.scale)) + " " + juce::String (juce::roundToInt (bpm)) + "bpm";
    audioJob = {}; audioJob.key = key; audioJob.running = true; audioJob.progress = std::make_shared<std::atomic<float>> (0.0f);
    for (auto& p : parts) audioJob.files.add (dir.getChildFile (base + " " + p.second + ".wav").getNonexistentSibling (true).getFullPathName());
    auto prog = audioJob.progress; const auto files = audioJob.files;
    renderStatus = "RENDERING 0%";
    juce::Thread::launch ([job, parts, files, prog] {
        std::vector<juce::AudioBuffer<float>> bufs;
        for (size_t i = 0; i < parts.size(); ++i)
        {
            std::atomic<float> sub { 0.0f };
            bufs.push_back (ArpProcessor::renderOffline (*job, parts[i].first, &sub));
            prog->store ((float) (i + 1) / (float) parts.size() * 0.98f);
        }
        float peak = 0; for (auto& b : bufs) for (int c = 0; c < b.getNumChannels(); ++c) peak = juce::jmax (peak, b.getMagnitude (c, 0, b.getNumSamples()));
        const float gain = peak > 1.0e-6f ? 0.501f / peak : 1.0f;   // -6 dBFS peak: headroom for the mix (stems share one gain)
        for (size_t i = 0; i < bufs.size(); ++i) { bufs[i].applyGain (gain); ArpProcessor::writeWav (juce::File (files[(int) i]), bufs[i], job->rate); }
        prog->store (2.0f);
    });
}

// the slider convention (every GhostBlock plugin): right-click = type a value (TypeIn), double-click = the default, hover = the value
// in real units, a bar jumps to the click and scrubs, SHIFT = fine, the wheel = one step; one undo step per gesture where undo exists
void ArpEditor::WheelStep::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    auto* s = dynamic_cast<juce::Slider*> (e.eventComponent);
    if (s == nullptr || ! s->isEnabled() || std::abs (w.deltaY) < 1.0e-4f) return;
    const double step = s->getInterval() > 0 ? s->getInterval() : (s->getMaximum() - s->getMinimum()) / 100.0;
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (s != last.getComponent() || now - lastT > 500.0) { if (s->onDragStart) s->onDragStart(); }   // one undo step per wheel burst
    last = s; lastT = now;
    s->setValue (juce::jlimit (s->getMinimum(), s->getMaximum(), s->getValue() + (w.deltaY > 0 ? step : -step) * (w.isReversed ? -1.0 : 1.0)), juce::sendNotificationSync);
    if (s->onDragEnd) s->onDragEnd();
}
void ArpEditor::applySliderConvention (juce::Component& root)
{
    for (auto* c : root.getChildren())
    {
        if (auto* s = dynamic_cast<juce::Slider*> (c))
        {
            if (! s->isDoubleClickReturnEnabled()) s->setDoubleClickReturnValue (true, s->getValue());
            s->setPopupDisplayEnabled (true, true, nullptr, 1600);
            s->setVelocityModeParameters (0.18, 1, 0.0, true, juce::ModifierKeys::shiftModifier);   // SHIFT: fine
            s->setSliderSnapsToMousePosition (true);
            if (! (bool) s->getProperties().getWithDefault ("wheelStep", false)) { s->setScrollWheelEnabled (false); s->addMouseListener (&wheelStep, false); s->getProperties().set ("wheelStep", true); }
        }
        applySliderConvention (*c);
    }
}
void ArpEditor::startSectionDrag (bool audio, juce::Component* src)
{
    if (! proc.exportAllowed (src != nullptr ? src : (juce::Component*) this, audio ? "arp.section.audio" : "arp.section.midi")) { toast ("Drag-out is part of the full version (UNLOCK)"); return; }
    proc.drawOverride = proc.section().valid() ? 1 : 0;
    startDragOut (15, audio, false, src, true);
    proc.drawOverride = -1;
    if (audio && audioJob.running) { audioJob.silent = false; toast ("Rendering the loop as WAV (a moment): keep holding, or drag the WAV grip again when it says WAV", 3600); }
}
void ArpEditor::toggleLoopSection()
{
    auto sc = proc.section();
    if (! sc.valid())
    {   // no brace yet: the bars on screen (or the selected bars) become the section
        const int b0 = roll.selBar0 >= 0 ? juce::jmin (roll.selBar0, roll.selBar1) : (int) (roll.view.start / BAR), b1 = roll.selBar0 >= 0 ? juce::jmax (roll.selBar0, roll.selBar1) + 1 : b0 + juce::jmax (1, roll.viewBars());
        proc.setSection (b0 * BAR, b1 * BAR); proc.setSectionOn (true);
    }
    else proc.setSectionOn (! sc.on);
    const auto n = proc.section();
    toast (n.on ? "LOOP SECTION " + juce::String (n.start / BAR + 1) + "." + juce::String ((n.start % BAR) / PPQ + 1) + " - " + juce::String ((n.end - 1) / BAR + 1) + "." + juce::String (((n.end - 1) % BAR) / PPQ + 1) + ": only this part plays, over and over (press L again to play everything)" : juce::String ("LOOP SECTION off: the whole song plays (the brace stays)"), 2400);
    refresh(); roll.repaint();
}
std::vector<juce::Rectangle<int>> ArpEditor::modalFocus()
{   // a modal layer is up: only its own surfaces are meant to be read (editor coordinates)
    std::vector<juce::Rectangle<int>> out;
    if (auto* v = dynamic_cast<Veil*> (overlay.get()); v != nullptr && v->isVisible()) out.push_back (getLocalArea (v, v->cardR));
    for (auto* ch : content.getChildren()) if (auto* h = dynamic_cast<HintLayer*> (ch); h != nullptr && h->isVisible()) for (auto r : h->focus()) out.push_back (getLocalArea (h, r));
    return out;
}
Btn& ArpEditor::themeButton() { return themeB; }
juce::Component* ArpEditor::bypassButton() { return bypassB.get(); }
juce::Component* ArpEditor::syncButton() { return syncB.get(); }
juce::String ArpEditor::sectionWavKey() const
{
    if (! proc.section().valid()) return {};
    proc.drawOverride = 1; const auto k = audioKey (15, false); proc.drawOverride = -1; return k;
}
bool ArpEditor::sectionWavReady() const
{
    const auto k = sectionWavKey(); if (k.isEmpty() || k != audioJob.key || audioJob.running || audioJob.files.isEmpty()) return false;
    for (auto& f : audioJob.files) if (! juce::File (f).existsAsFile()) return false;
    return true;
}
bool ArpEditor::sectionWavRendering() const { const auto k = sectionWavKey(); return k.isNotEmpty() && audioJob.running && k == audioJob.key; }
void ArpEditor::prerenderSection (bool now)
{   // the loop's WAV, rendered ahead to disk so the WAV grip drags at once: never in the demo (no hint: it runs from the timer)
    if (! proc.section().valid() || ! proc.exportAllowed (nullptr, "arp.section.prerender")) return;
    const auto k = sectionWavKey(); if (k.isEmpty() || k == prerenderKey || audioJob.running) return;
    const double tNow = juce::Time::getMillisecondCounterHiRes();
    if (k != prerenderSeenKey) { prerenderSeenKey = k; prerenderSeen = tNow; if (! now) return; }
    if (! now && tNow - prerenderSeen < 600.0) return;   // wait until the loop stops changing
    if (proc.ideaBusy.load() || roll.dr.kind == 7 || roll.dr.kind == 8) return;   // (not while generating / dragging the brace)
    prerenderKey = k;
    const auto src = audioDragSrc; const auto status = renderStatus;
    proc.drawOverride = 1; startDragOut (15, true, false, nullptr, true); proc.drawOverride = -1;
    audioDragSrc = src;
    if (audioJob.running && audioJob.key == k) { audioJob.silent = true; renderStatus = status; }
}
void ArpEditor::applyThemeNow (int mode)
{
    th.mode = mode; th.p = palette (mode, proc.themeId(), proc.ghostLook()); fromP = toP = th.p; fadeStart = -1; lnf.refresh(); sendLookAndFeelChange(); refresh(); repaint();
}
juce::String ArpEditor::versionString()
{
   #ifdef JucePlugin_VersionString
    return JucePlugin_VersionString;
   #else
    return "3.0.0";   // (the test app has no plug-in version macro)
   #endif
}
int ArpEditor::soundLane() const { return soundTab >= 0 ? soundTab : juce::jlimit (0, 3, lane()); }
void ArpEditor::fillPresetBox()
{   // LINK ALL: the shared sound (+ YOUR SAMPLE); per lane: LINKED + the factory sounds + YOUR SAMPLE
    fillingPresets = true;
    presetBox.clear (juce::dontSendNotification);
    const int shared = (int) proc.apvts.getRawParameterValue ("preset")->load();
    const juce::String sharedName = shared >= arpsyn::kPresets ? juce::String ("YOUR SAMPLE") : juce::String (arpsyn::presetName (shared));
    if (proc.linkAll)
    {
        for (int i = 0; i < arpsyn::kPresets; ++i) presetBox.addItem (arpsyn::presetName (i), i + 1);
        presetBox.addItem ("YOUR SAMPLE" + juce::String (proc.hasSample() ? "  (" + proc.sampleInfo.name + ")" : juce::String()), arpsyn::kPresets + 1);
        presetBox.setSelectedId (shared + 1, juce::dontSendNotification);
    }
    else
    {
        const int ln = soundLane();
        static const char* classic[] { "LINKED (classic pad)", "LINKED", "LINKED", "LINKED (classic bass)" };
        presetBox.addItem (juce::String (classic[ln]) + (ln == 1 || ln == 2 ? "  = " + sharedName : juce::String()), 1);
        for (int i = 0; i < arpsyn::kPresets; ++i) presetBox.addItem (arpsyn::presetName (i), i + 2);
        presetBox.addItem ("YOUR SAMPLE" + juce::String (proc.hasSample() ? "  (" + proc.sampleInfo.name + ")" : juce::String()), arpsyn::kPresets + 2);
        presetBox.setSelectedId (proc.laneChoice (ln) + 1, juce::dontSendNotification);
    }
    fillingPresets = false;
}
juce::String ArpEditor::tempoText() const
{
    if (proc.syncOwn()) return juce::String (juce::roundToInt (proc.sync.getOwnBpm())) + " BPM";
    if (proc.hostPresent.load()) { const double hb = proc.hostBpm.load(); return (hb > 1 ? juce::String (juce::roundToInt (hb)) : juce::String (proc.idea().g.bpm)) + " DAW"; }
    return juce::String (proc.idea().g.bpm) + " BPM";
}
void ArpEditor::syncTransportUi()
{   // SYNC: DAW with a DAW: its tempo read-only, no plugin PLAY; OWN (or no DAW at all): the plugin's own PLAY, BPM, TAP
    const bool daw = ! proc.syncOwn() && proc.hostPresent.load();
    play.setVisible (! daw); bpmB.setEnabled (! daw); tap.setEnabled (! daw);
    bpmB.setTooltip (daw ? "Tempo from the DAW (SYNC: DAW). Switch to SYNC: OWN for the plugin's own tempo" : proc.syncOwn() ? "The plugin's own tempo (SYNC: OWN): click to type it, or TAP" : "Tempo (no DAW): click to type it, or TAP");
    if (syncB != nullptr) { syncB->setTheme (proc.themeId()); syncB->repaint(); }
}
int ArpEditor::dispShift (int lane) const { const int t = proc.transposeNow(); return t != 0 && ((proc.transposeMask.load() >> lane) & 1) ? t : 0; }
juce::String ArpEditor::dispChord (const std::string& name) const
{
    const int t = dispShift (LaneChords); const auto& H = proc.idea().H;
    return t == 0 ? juce::String (name) : juce::String (transposeChordName (name, t, mod12 (H.key + t), H.scaleName));
}
juce::String ArpEditor::dispKey() const { const auto& H = proc.idea().H; return keyName (mod12 (H.key + proc.transposeNow()), H.scaleName); }
void ArpEditor::refresh()
{
    const auto& I = proc.idea();
    if (bypassB != nullptr) bypassB->setTheme (proc.themeId());
    linkB.on = proc.linkAll; soundTabs.sel = soundLane(); soundTabs.setAlpha (1.0f);   // (LINK ALL shows on its own button: no see-through, low-contrast tabs) soundTabs.repaint(); linkB.repaint();
    fillPresetBox(); sampleB.on = samplePanel != nullptr && samplePanel->isVisible();
    if (samplePanel != nullptr && samplePanel->isVisible()) samplePanel->sync();
    playSeg.sel = proc.apvts.getRawParameterValue ("playMode")->load() > 0.5f ? 1 : 0; playSeg.repaint();
    trS.value = proc.transposeNow(); trS.repaint(); bakeB.setEnabled (proc.transposeNow() != 0);
    { const auto sc = proc.section(); loopB.on = sc.on && sc.valid(); loopB.text = loopB.on ? "LOOP ON" : "LOOP"; loopB.setEnabled (true); loopB.repaint(); dragSecSeg.sel = proc.drawSection.load(); dragSecSeg.setEnabled (sc.valid()); dragSecSeg.repaint(); }
    if (I.mode != th.mode) { fromP = th.p; toP = palette (I.mode, proc.themeId(), proc.ghostLook()); fadeStart = t; }
    modeSeg.sel = I.mode < 3 ? I.mode : -1; modeSeg2.sel = I.mode >= 3 ? I.mode - 3 : -1; modeSeg2.repaint(); th.mode = I.mode;
    seedLbl.setText ("SEED " + juce::String (I.seedUsed), juce::dontSendNotification);
    const double hb = proc.hostBpm.load();
    (void) hb; bpmB.text = tempoText();
    play.on = proc.syncOwn() ? proc.sync.isOwnPlaying() : proc.internalPlay.load(); play.text = play.on ? "STOP" : "PLAY";
    syncTransportUi();
    undoB.setEnabled (proc.canUndo()); redoB.setEnabled (proc.canRedo());
    themeB.text = ArpProcessor::lookName (ArpProcessor::nextLook (proc.theme));
    kickB.on = proc.apvts.getRawParameterValue ("kick")->load() > 0.5f; followB.on = proc.apvts.getRawParameterValue ("follow")->load() > 0.5f; evolveB.on = I.evolve;
    layerPluck.on = proc.apvts.getRawParameterValue ("layerPluck")->load() > 0.5f; layerOct.on = proc.apvts.getRawParameterValue ("layerOct")->load() > 0.5f;
    stabsB.on = proc.apvts.getRawParameterValue ("stabs")->load() > 0.5f; soundB.on = proc.apvts.getRawParameterValue ("sound")->load() > 0.5f;
    if (lane() == LaneChords && tools.size() > 5) { tools[4]->text = I.simple ? "CALMER" : "SIMPLER"; tools[5]->text = I.simple ? "PUNCHIER" : "RICHER"; }   // SIMPLE: rhythm only
    cycleSeg.sel = I.cycle == 0 ? 0 : I.cycle - 2; rangeSeg.sel = I.arpRange - 1; rateSeg.sel = I.arpSpb == 8 ? 0 : I.arpSpb == 24 ? 2 : I.arpSpb == 32 ? 3 : 1;
    formSeg.sel = I.form; staggerSeg.sel = I.stagger == 0 ? 0 : I.stagger >= BAR ? 2 : 1; melRangeSeg.sel = I.melRange - 1;
    chordLen.sel = I.chordBars - 1; chordRhythm.sel = I.chordRhythm; voicingSeg.sel = I.voicing; bassRate.sel = I.bassRate;
    if (proc.chordRewriteSeq != seenChordRewrite)
    {   // EDITED CHORDS rebuilt (VOICING, SIMPLE / RICH, NEW CHORDS, CHORD LENGTH, RHYTHM, tap a chord, GENERATE): say what happened to the edits
        seenChordRewrite = proc.chordRewriteSeq;
        toast (proc.chordRewriteKind == 2 ? "CHORDS rebuilt - kept locked notes" : "CHORDS rebuilt - your edited notes were replaced", 3200);
    }
    chordOctLbl.setText (I.chordOctUsed == 0 ? juce::String ("0") : (I.chordOctUsed > 0 ? "+" : "") + juce::String (I.chordOctUsed), juce::dontSendNotification);
    stutter.setValue (I.stutter, juce::dontSendNotification);
    {   // TRAP / HIP HOP: three more style buttons after the classic six
        juce::StringArray it; for (int i = 0; i < styleCountFor (I.mode); ++i) it.add (styleName (styleAt (I.mode, i)));
        if (it != styleSeg.items) { styleSeg.items = it; styleSeg.repaint(); layoutContent(); }
    }
    styleSeg.sel = styleButton (I.mode, I.style); candySeg.sel = I.candy; evolveModeSeg.sel = I.evolve ? I.evolveMode + 1 : 0; dirSeg.sel = I.arpDir; contourSeg.sel = I.arpContour; accentSeg.sel = I.arpAccent;
    arpMeterSeg.sel = I.arpMeter; arpEucS.hi = juce::jmax (2, meterSteps (I.arpMeter) * I.arpSpb / 16); arpRotS.hi = arpEucS.hi - 1;
    arpEucS.value = juce::jmin (I.arpEuclid, arpEucS.hi); arpRotS.value = I.arpEuclidRot; arpRotS.setEnabled (I.arpEuclid > 0); arpEucS.repaint(); arpRotS.repaint();
    if (! tensionS.isMouseButtonDown()) { tensionS.setValue (I.tension < 0 ? -0.1 : I.tension, juce::dontSendNotification); tensionS.onValueChange(); }
    motifB.on = ! I.motif.empty(); motifB.text = motifB.on ? "MOTIF LOCKED" : "LOCK MOTIF";
    hookLabB.on = hookLab != nullptr && hookLab->isVisible();
    if (hookLab != nullptr && hookLab->isVisible()) hookLab->sync();
    const int ln = lane();
    const bool sel = roll.selLink >= 0 && roll.selLink + 1 < (int) I.lanes[ln].notes.size() && ln != LaneChords;
    const Link l = sel ? I.lanes[ln].notes[(size_t) roll.selLink].l : Link();
    for (auto* b : { &lkGlide, &lkFat, &lkStep, &lkAll }) b->setEnabled (sel);
    lkGlide.on = sel && l.type == 1; lkFat.on = sel && l.type == 2; lkStep.on = sel && l.type == 0;
    lkAmt.setEnabled (sel && l.type != 0);   // STEP has no amount
    lkAmt.setAlpha (sel && l.type != 0 ? 1.0f : 0.35f);
    if (sel) { lkAmt.setValue (l.type == 2 ? l.f : l.type == 1 ? l.w : 0, juce::dontSendNotification); lkAmt.updateText(); }
    lkAmt.setColour (juce::Slider::trackColourId, ! sel || l.type == 0 ? th.p.dim : l.type == 2 ? th.p.gold : th.p.pink);
    lkAmt.setColour (juce::Slider::thumbColourId, ! sel || l.type == 0 ? th.p.dim : l.type == 2 ? th.p.gold : th.p.pink);
    lkInfo.setText (! sel ? "click the line between two notes" : l.type == 1 ? "GLIDE starts at " + juce::String (juce::roundToInt (l.w * 100)) + "% (drag the pink dot)" : l.type == 2 ? "FATTEN " + juce::String (juce::roundToInt (l.f * 100)) + "% (drag the gold ball)" : "STEP: no amount (pick GLIDE / FATTEN)", juce::dontSendNotification);
    {
        const int b0 = juce::jmin (roll.selBar0, roll.selBar1), b1 = juce::jmax (roll.selBar0, roll.selBar1);
        const bool locked = selectionLocked (I, ln, roll.sel, b0, b1);
        lockSel.text = locked ? "UNLOCK SEL" : "LOCK SEL"; lockSel.on = locked; lockSel.tint = [this, locked] { return locked ? th.p.gold : th.p.a2; };
    }
    // the chord mood drives the ghost
    const auto m = analyzeMood (I.chords, I.g.scale, I.g.bpm, I.master, I.g.preset, I.g.rate);
    faceTarget = faceFor (m.mood); moodWord = juce::String (m.mood).toUpperCase().toStdString();
    if (more != nullptr && more->isVisible()) more->sync();
    for (auto* c : lanes) c->repaint();
    layoutContent();
    repaint();
}

void ArpEditor::timerCallback()
{
    t += 1.0 / 30.0;
    if (fadeStart >= 0) { const float k = (float) juce::jlimit (0.0, 1.0, (t - fadeStart) / 0.3); th.p = lerpP (fromP, toP, k); lnf.refresh(); if (k >= 1) { fadeStart = -1; sendLookAndFeelChange(); } content.repaint(); }
    const int v = proc.version.load();
    if (v != lastVersion) { lastVersion = v; refresh(); }
    face = lerpFace (face, faceTarget, 1.0f - std::exp (-(1.0f / 30.0f) * 4.0f));
    // follow the playhead in the roll
    const double pt = proc.playTick.load();
    if (followPlay && pt >= 0 && roll.dr.kind == 0 && (pt < roll.view.start || pt >= roll.view.start + roll.view.len)) roll.setView ({ std::floor (pt / roll.view.len) * roll.view.len, roll.view.len });
    // keyboard shortcuts work while the mouse is over the plugin (focus follows the mouse; never taken from a text box)
    if (isMouseOver (true) && ! hasKeyboardFocus (true) && isShowing()) grabKeyboardFocus();
    prerenderSection();
    // the audio render: progress on the pills, then the drag
    if (audioJob.running && audioJob.progress != nullptr)
    {
        const float pr = audioJob.progress->load();
        if (pr >= 2.0f && audioJob.silent) { audioJob.running = false; audioJob.silent = false; roll.repaint(); }   // (the loop's WAV, rendered ahead: no toast)
        else if (audioJob.silent) { roll.repaint(); }
        else if (pr >= 2.0f) { audioJob.running = false; renderStatus = "AUDIO READY: DRAG"; if (audioDragSrc != nullptr && juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown()) { ghosty::exportgate::dragFiles (proc.license, audioJob.files, audioDragSrc.getComponent()); } else toast ("Audio ready: drag DRAG AUDIO into your DAW", 2000); }
        else renderStatus = "RENDERING " + juce::String (juce::roundToInt (pr * 100)) + "%";
        if (dragAudio != nullptr) dragAudio->repaint();
        for (auto* c : lanes) c->repaint();
    }
    static int slow = 0;
    { const juce::String g = proc.ideaBusy.load() ? "GENERATING..." : "GENERATE"; if (gen.text != g) { gen.text = g; gen.repaint(); } }
    {   // why only the start plays: a LOOP SECTION on, or the DAW looping a pattern shorter than the idea
        const bool playingNow = proc.playTick.load() >= 0;
        const auto sc = proc.section(); const double now = juce::Time::getMillisecondCounterHiRes();
        if (playingNow && ! wasPlayingUi && sc.on && sc.valid() && now - toldLoopAt > 30000.0)
        { toldLoopAt = now; toast ("Looping bars " + juce::String (sc.start / BAR + 1) + "-" + juce::String ((sc.end - 1) / BAR + 1) + " - press L (or click LOOP) to play the whole song", 4200); }
        wasPlayingUi = playingNow;
        const int span = proc.hostLoopTicks.load();
        if (! toldShortDawLoop && proc.hostLoopCount.load() >= 2 && span > 0 && span < proc.idea().total - PPQ && ! proc.syncOwn())
        {
            toldShortDawLoop = true;
            toast ("Your DAW keeps looping a " + juce::String (span / BAR) + "." + juce::String ((span % BAR) / PPQ) + "-bar pattern, so only the start of the idea plays (" + juce::String (proc.idea().total / BAR)
                   + " bars). Make the DAW pattern / loop " + juce::String (proc.idea().total / BAR) + " bars long, or switch SYNC to OWN.", 9000);
        }
    }
    if (++slow % 6 == 0) { const auto txt = tempoText(); if (bpmB.text != txt) { bpmB.text = txt; bpmB.repaint(); }
        const bool pon = proc.syncOwn() ? proc.sync.isOwnPlaying() : proc.internalPlay.load(); if (pon != play.on) { play.on = pon; play.text = pon ? "STOP" : "PLAY"; play.repaint(); }
        if (play.isVisible() == (! proc.syncOwn() && proc.hostPresent.load())) syncTransportUi();
        if (more->isVisible()) more->sync(); }
    roll.repaint(); chips.repaint();
    if (hookLab != nullptr && hookLab->isVisible() && (proc.lab.playing >= 0 || proc.lab.busy)) hookLab->repaint();
    repaint (header.getUnion (juce::Rectangle<int> (0, 0, 80, 80)).transformedBy (content.getTransform()));
    if (toastUntil > 0 && juce::Time::getMillisecondCounterHiRes() > toastUntil) { toastUntil = 0; repaint(); }
}

void ArpEditor::resized()
{
    const float s = (float) getWidth() / (float) kW;
    content.setBounds (0, 0, kW, kH);
    content.setTransform (juce::AffineTransform::scale (s));
    layoutContent();
}

void ArpEditor::layoutContent()
{
    const int ln = lane();
    header = { 0, 0, kW, 70 };
    auto h = header.reduced (12, 9);
    h.removeFromLeft (62 + 8 + 180);   // logo + title
    modeArea = h.removeFromLeft (262);
    {   // the three moods on top, TRAP / HIP HOP under them (same width, same order)
        auto ma = modeArea.reduced (0, 1);
        modeSeg.setBounds (ma.removeFromTop (27)); ma.removeFromTop (1); modeSeg2.setBounds (ma);
    }
    h.removeFromLeft (10);
    genArea = h.removeFromLeft (140); gen.setBounds (genArea);
    h.removeFromLeft (10);
    auto sr = h.removeFromLeft (116).reduced (0, 10); seedPrev.setBounds (sr.removeFromLeft (26)); seedNext.setBounds (sr.removeFromRight (26)); seedLbl.setBounds (sr);
    {   // BYPASS on top, PLAY: ALWAYS | ON NOTES under it
        auto col = h.removeFromRight (136); h.removeFromRight (6);
        bypassB->setBounds (col.removeFromTop (26).withSizeKeepingCentre (84, 26).withX (col.getRight() - 84));
        col.removeFromTop (2); playSeg.setBounds (col.removeFromTop (22));
    }
    unlock->setBounds (h.removeFromRight (96).reduced (0, 12));
    h.removeFromRight (6); themeB.setBounds (h.removeFromRight (60).reduced (0, 12));
    h.removeFromRight (6); moreB.setBounds (h.removeFromRight (64).reduced (0, 12));
    ghostArea = h.removeFromRight (juce::jmin (64, h.getWidth()));
    // transport
    transport = { 12, 74, kW - 24, 38 };
    auto tr = transport;
    syncB->setBounds (tr.removeFromLeft (90).withSizeKeepingCentre (90, 26)); tr.removeFromLeft (6);
    play.setBounds (tr.removeFromLeft (60)); tr.removeFromLeft (6); bpmB.setBounds (tr.removeFromLeft (72)); tr.removeFromLeft (6); tap.setBounds (tr.removeFromLeft (44)); tr.removeFromLeft (6);
    saveB.setBounds (tr.removeFromLeft (66)); tr.removeFromLeft (4); loadB.setBounds (tr.removeFromLeft (66)); tr.removeFromLeft (8);
    kickB.setBounds (tr.removeFromLeft (52)); tr.removeFromLeft (4);
    const bool kickOn = proc.apvts.getRawParameterValue ("kick")->load() > 0.5f;
    kickVol.setVisible (kickOn); if (kickOn) { kickVol.setBounds (tr.removeFromLeft (52).reduced (0, 10)); tr.removeFromLeft (4); }
    followB.setBounds (tr.removeFromLeft (70)); tr.removeFromLeft (6); evolveB.setBounds (tr.removeFromLeft (64)); tr.removeFromLeft (8);
    dragAllArea = tr.removeFromRight (360); tr.removeFromRight (8);
    {   // TRANSPOSE - +3 st +  BAKE  TO KEY
        auto t = tr; const int w = t.getWidth();
        const int kw = juce::jmin (58, w / 5), bw = juce::jmin (48, w / 5);
        keyB.setBounds (t.removeFromRight (kw).reduced (0, 3)); t.removeFromRight (4); bakeB.setBounds (t.removeFromRight (bw).reduced (0, 3)); t.removeFromRight (4);
        trS.setBounds (t.reduced (0, 3));
    }
    { auto d = dragAllArea; auto ms = d.removeFromRight (82); mixStems.setBounds (ms.removeFromTop (ms.getHeight() / 2).reduced (0, 1)); dragSecSeg.setBounds (ms.reduced (0, 1)); d.removeFromRight (6); dragAudio->setBounds (d.removeFromRight (d.getWidth() / 2)); dragAll->setBounds (d.withTrimmedRight (4)); }
    // lanes
    laneArea = { 12, 118, 292, 4 * 94 };
    for (int l = 0; l < kLanes; ++l)
    {
        auto r = juce::Rectangle<int> (12, 118 + l * 94, 292, 90); lanes[l]->setBounds (r);
        if (auto* dp = lanes[l]->findChildWithID ("")) (void) dp;
        for (auto* ch : lanes[l]->getChildren()) if (ch->getName() == "drag") ch->setBounds (r.getWidth() - 212, 5, 128, 26);
    }
    // sound card
    soundCard = { 12, 498, 292, 310 };
    auto sc = soundCard.reduced (12);
    { auto top = sc.removeFromTop (20); linkB.setBounds (top.removeFromRight (78).reduced (0, 1)); }
    sc.removeFromTop (4); soundTabs.setBounds (sc.removeFromTop (22)); sc.removeFromTop (6);
    { auto r = sc.removeFromTop (28); sampleB.setBounds (r.removeFromRight (70)); r.removeFromRight (6); presetBox.setBounds (r); }
    sc.removeFromTop (6);
    { auto r = sc.removeFromTop (28); const int w = r.getWidth() / 4; layerPluck.setBounds (r.removeFromLeft (w).reduced (2, 0)); layerOct.setBounds (r.removeFromLeft (w).reduced (2, 0)); stabsB.setBounds (r.removeFromLeft (w).reduced (2, 0)); soundB.setBounds (r.reduced (2, 0)); }
    sc.removeFromTop (6);
    { for (int row = 0; row < 2; ++row) { auto r = sc.removeFromTop (68); const int w = r.getWidth() / 3; for (int k = 0; k < 3; ++k) if (row * 3 + k < macros.size()) macros[row * 3 + k]->setBounds (r.removeFromLeft (w).reduced (12, 0)); } }
    // editor card
    editorCard = { 314, 118, kW - 326, 690 };
    auto er = editorCard.reduced (12);
    hookLabB.setBounds (er.getRight() - 124, editorCard.getY() + 6, 124, 26);   // HOOK LAB: top right of the lane editor, always there
    er.removeFromTop (26);   // lane title
    toolsRow = er.removeFromTop (34);
    {
        auto r = toolsRow; const bool chords = ln == LaneChords; const bool isArp = ln == LaneArp;
        redoB.setBounds (r.removeFromRight (84)); r.removeFromRight (4); undoB.setBounds (r.removeFromRight (90)); r.removeFromRight (10);
        const int nT = chords ? 6 : 9;
        if (isArp) { harvestB.setBounds (r.removeFromRight (150)); r.removeFromRight (8); }
        harvestB.setVisible (isArp);
        const int w = r.getWidth() / nT;
        for (int i = 0; i < 9; ++i)
        {
            auto* b = tools[i];
            const bool show = ! chords || i == 0 || i == 1 || i == 4 || i == 5 || i == 8;
            b->setVisible (show); if (! show) continue;
            b->setBounds (r.removeFromLeft (chords && i == 8 ? w * 2 : w).reduced (2, 0));
        }
        if (chords) { const bool smp = proc.idea().simple; tools[4]->text = smp ? "CALMER" : "SIMPLER"; tools[5]->text = smp ? "PUNCHIER" : "RICHER";
                     tools[4]->setTooltip (smp ? "SIMPLE chords: a calmer chord rhythm (stabs -> sustain)" : "Plainer chords (less colour)");
                     tools[5]->setTooltip (smp ? "SIMPLE chords: a busier chord rhythm (sustain -> stabs)" : "Richer chords (more colour)"); tools[8]->text = "NEW CHORDS"; tools[8]->setTooltip (kNewChordsTip); }
        else { tools[4]->text = "SIMPLER"; tools[5]->text = "BUSIER"; tools[8]->text = "NEW"; for (int i : { 4, 5, 8 }) tools[i]->setTooltip (kToolTip[i]); }   // every lane change: this lane's own wording
    }
    er.removeFromTop (8);
    ctlRow = er.removeFromTop (ln == LaneArp || ln == LaneMelody ? 120 : 58);
    for (auto* c : std::initializer_list<juce::Component*> { &pattern, &cycleSeg, &rangeSeg, &rateSeg, &formSeg, &staggerSeg, &melRangeSeg, &stutter, &stutterLbl, &chips, &chordLen, &chordRhythm, &voicingSeg, &bassRate,
                                                             &chordOctDn, &chordOctUp, &chordOctLbl,
                                                             &styleSeg, &candySeg, &evolveModeSeg, &dirSeg, &contourSeg, &accentSeg, &tensionS, &tensionLbl, &simS, &simLbl, &respondB, &motifB, &align, &evolveLbl,
                                                             &arpMeterSeg, &arpEucS, &arpRotS }) c->setVisible (false);
    if (ln == LaneArp || ln == LaneMelody)
    {   // the HOOK LAB rows under the lane's own controls
        auto x = ctlRow.removeFromBottom (60); ctlRow.removeFromBottom (2);
        auto r1 = x.removeFromTop (30).reduced (0, 2), r2 = x.reduced (0, 2);
        auto show = [] (juce::Component& c, juce::Rectangle<int> b) { c.setBounds (b); c.setVisible (true); };
        if (ln == LaneArp)
        {
            show (dirSeg, r1.removeFromLeft (r1.getWidth() * 58 / 100)); r1.removeFromLeft (8); show (contourSeg, r1);
            show (accentSeg, r2.removeFromLeft (r2.getWidth() * 40 / 100)); r2.removeFromLeft (8);
            show (arpEucS, r2.removeFromLeft (118)); r2.removeFromLeft (4); show (arpRotS, r2.removeFromLeft (104)); r2.removeFromLeft (8); show (align, r2);
        }
        else
        {
            show (styleSeg, r1.removeFromLeft (r1.getWidth() * (styleSeg.items.size() > kClassicStyles + 3 ? 71 : styleSeg.items.size() > kClassicStyles ? 64 : 56) / 100)); r1.removeFromLeft (8); show (candySeg, r1);   // (TRAP: ten styles, HIP HOP: nine)
            show (tensionLbl, r2.removeFromLeft (94)); r2.removeFromLeft (4); show (tensionS, r2.removeFromLeft (100)); r2.removeFromLeft (10);
            show (respondB, r2.removeFromLeft (90)); r2.removeFromLeft (2); show (simLbl, r2.removeFromLeft (78)); show (simS, r2.removeFromLeft (84)); r2.removeFromLeft (10);
            show (motifB, r2.removeFromLeft (112)); r2.removeFromLeft (8); show (evolveLbl, r2.removeFromLeft (50)); show (evolveModeSeg, r2);
        }
    }
    {
        auto r = ctlRow;
        auto twoRow = [] (juce::Rectangle<int> b, juce::Component& top, juce::Component& bot) { top.setBounds (b.removeFromTop (b.getHeight() / 2).reduced (0, 2)); bot.setBounds (b.reduced (0, 2)); top.setVisible (true); bot.setVisible (true); };
        if (ln == LaneArp)
        {
            pattern.setVisible (true); pattern.setBounds (r.removeFromLeft (8 * 46 + 30)); r.removeFromLeft (14);
            auto a = r.removeFromLeft (r.getWidth() / 2); twoRow (a, cycleSeg, rangeSeg); r.removeFromLeft (10);
            rateSeg.setVisible (true); rateSeg.setBounds (r.removeFromTop (29).reduced (0, 2));
            arpMeterSeg.setVisible (true); arpMeterSeg.setBounds (r.removeFromTop (29).reduced (0, 2));
        }
        else if (ln == LaneMelody)
        {
            auto a = r.removeFromLeft (r.getWidth() / 2); twoRow (a, formSeg, staggerSeg); r.removeFromLeft (12);
            melRangeSeg.setVisible (true); melRangeSeg.setBounds (r.removeFromTop (29).reduced (0, 2));
            stutterLbl.setVisible (true); stutter.setVisible (true); stutterLbl.setBounds (r.removeFromLeft (70)); stutter.setBounds (r);
        }
        else if (ln == LaneChords)
        {
            chips.setVisible (true); chips.setBounds (r.removeFromLeft (r.getWidth() * 55 / 100)); r.removeFromLeft (12);
            auto top = r.removeFromTop (29), oct = top.removeFromRight (150); top.removeFromRight (10);   // OCT - [n] OCT + at the right of the CHORD LENGTH row
            chordLen.setVisible (true); chordLen.setBounds (top.reduced (0, 2));
            for (auto* c : std::initializer_list<juce::Component*> { &chordOctDn, &chordOctUp, &chordOctLbl }) c->setVisible (true);
            chordOctDn.setBounds (oct.removeFromLeft (58).reduced (0, 2)); chordOctUp.setBounds (oct.removeFromRight (58).reduced (0, 2)); chordOctLbl.setBounds (oct);
            auto b = r; chordRhythm.setVisible (true); voicingSeg.setVisible (true); chordRhythm.setBounds (b.removeFromLeft (b.getWidth() / 2).reduced (0, 2)); b.removeFromLeft (6); voicingSeg.setBounds (b.reduced (0, 2));
        }
        else { bassRate.setVisible (true); bassRate.setBounds (r.removeFromLeft (300).removeFromTop (29).reduced (0, 2)); }
    }
    er.removeFromTop (8);
    linkRow = er.removeFromBottom (32);
    er.removeFromBottom (6);
    roll.setBounds (er);
    {
        auto r = linkRow; const bool editable = ln != LaneChords;   // (CHORDS: notes edit too, but no lines between them: LOCK SEL / CLEAR SEL only)
        for (auto* c : std::initializer_list<juce::Component*> { &lkGlide, &lkFat, &lkStep, &lkAll }) c->setVisible (editable);
        lockSel.setVisible (true); clearSelB.setVisible (true);
        zoomIn.setBounds (r.removeFromRight (30)); r.removeFromRight (4); zoomOut.setBounds (r.removeFromRight (30)); r.removeFromRight (6);
        followRollB.setBounds (r.removeFromRight (74)); r.removeFromRight (6); snapB.setBounds (r.removeFromRight (90)); r.removeFromRight (6);
        loopB.setBounds (r.removeFromRight (64)); r.removeFromRight (6);
        clearSelB.setBounds (r.removeFromRight (86)); r.removeFromRight (4); lockSel.setBounds (r.removeFromRight (82)); r.removeFromRight (10);
        // one slot shows the hint, or the amount slider while a GLIDE / FATTEN line is selected
        const auto& ns = proc.idea().lanes[ln].notes; const int li = roll.selLink;
        const bool amt = editable && li >= 0 && li + 1 < (int) ns.size() && linkHasAmount (ns[(size_t) li].l);
        auto slot = r.removeFromLeft (juce::jmax (150, r.getWidth() - (60 + 66 + 52 + 64 + 16)));
        lkInfo.setBounds (slot); lkAmt.setBounds (slot.reduced (0, 4)); lkInfo.setVisible (editable && ! amt); lkAmt.setVisible (amt); r.removeFromLeft (6);
        lkGlide.setBounds (r.removeFromLeft (60)); r.removeFromLeft (4); lkFat.setBounds (r.removeFromLeft (66)); r.removeFromLeft (4); lkStep.setBounds (r.removeFromLeft (52)); r.removeFromLeft (4);
        lkAll.setBounds (r.removeFromLeft (64));
    }
    more->setBounds (editorCard.reduced (2).withTrimmedTop (-2));
    hookLab->setBounds (editorCard.reduced (2).withTrimmedTop (-2));
    samplePanel->setBounds (editorCard.reduced (2).withTrimmedTop (-2));
    for (auto* ch : content.getChildren()) if (ch->getName() == "hints") ch->setBounds (0, 0, kW, kH);
}

void ArpEditor::paint (juce::Graphics& g)
{
    g.addTransform (content.getTransform());
    const auto& P = th.p; const int mode = th.mode;
    if (P.glass) paintGlassSurfaces (g); else drawBackdrop (g, { 0, 0, (float) kW, (float) kH }, P, mode, t, 1.0f);
    // header: logo + title + mood ghost
    drawProfessor (g, { 12, 4, 62, 62 }, t, mode);
    g.setColour (P.dim); g.setFont (F (10.0f)); g.drawText ("PROFESSOR BERNSTEIN'S", 84, 10, 200, 14, juce::Justification::centredLeft);
    {
        const juce::String title = "ArpEZator"; auto tr = juce::Rectangle<float> (82, 22, 200, 40);
        g.setFont (F (32.0f));
        juce::GlyphArrangement ga; ga.addLineOfText (F (32.0f), title, tr.getX(), tr.getBottom() - 8); juce::Path tp; ga.createPath (tp);
        g.setGradientFill (juce::ColourGradient (P.a1, tr.getX(), 0, P.a2, tr.getX() + 170, 0, false)); g.fillPath (tp);
    }
    {   // mood ghost (next to the seed)
        auto mg = ghostArea.toFloat().withSizeKeepingCentre (54, 54).withY (4);
        drawMoodGhost (g, mg, face, t, proc.listenState == 1 || proc.listenState == 2 ? 1.0f : 0.0f);
        g.setColour (P.dim); g.setFont (F (8.5f)); g.drawText (moodWord, mg.withY (58).withHeight (10).toNearestInt(), juce::Justification::centred);
    }
    // cards
    auto card = [&] (juce::Rectangle<int> r, Colour edge) {
        if (P.glass) { g.setColour (edge.withAlpha (0.5f)); g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 16, 1.2f); return; }   // (GHOST: the glass card is in paintGlassSurfaces)
        g.setColour (P.card); g.fillRoundedRectangle (r.toFloat(), 14); g.setColour (edge.withAlpha (0.6f)); g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 14, 1.3f); };
    card (soundCard, P.line);
    g.setColour (P.dim); g.setFont (F (12.0f)); g.drawText ("PREVIEW SOUND", soundCard.getX() + 14, soundCard.getY() + 12, 200, 16, juce::Justification::centredLeft);
    g.setColour (P.dim); g.setFont (F (9.5f, false));
    g.drawFittedText (proc.apvts.getRawParameterValue ("midiCh1")->load() > 0.5f ? "MIDI out: every lane on channel 1 (MORE: split ARP 1 / MELODY 2 / CHORDS 3 / BASS 4)" : "MIDI out: ARP ch 1, MELODY ch 2, CHORDS ch 3, BASS ch 4", soundCard.withTrimmedTop (soundCard.getHeight() - 26).reduced (14, 2), juce::Justification::centredLeft, 2);
    const int ln = lane(); const Colour lc = laneColour (ln);
    card (editorCard, lc);
    g.setColour (lc); g.setFont (F (16.0f)); g.drawText (laneName (ln), editorCard.getX() + 14, editorCard.getY() + 8, 160, 22, juce::Justification::centredLeft);
    g.setColour (P.dim); g.setFont (F (10.5f, false));
    const auto& I = proc.idea();
    juce::String info = ln == LaneArp ? "figure steps: click top half up / bottom half down, right-click pedal / rest / accent / tie. Roll: click a key to hear it, drag empty = move, pinch = zoom, click the line between notes = GLIDE / FATTEN / STEP"
                      : ln == LaneMelody ? "click empty = add, click a note = hear + select, double-click = delete, drag empty = move around, pinch / Cmd+wheel = zoom, click the line between two notes = GLIDE / FATTEN / STEP"
                      : ln == LaneChords ? "click a chord = next version, right-click = list; " + dispKey() + " " + juce::String (scaleLabel (I.g.scale)).toLowerCase()
                      : urbanMood (I.mode) ? juce::String (I.mode == Trap ? "808: kick-locked hits, pushes, slides between roots (the line between two notes = GLIDE), rolls" : "round sub on the kick positions, dotted pushes, slides")
                      : "the chord roots, a steady rhythm (optional lane)";
    g.drawFittedText (info, editorCard.getX() + 110, editorCard.getY() + 8, editorCard.getWidth() - 124 - 132, 20, juce::Justification::centredLeft, 1, 0.75f);
    // license line
    if (ghosty::License::required() && ! proc.license.isUnlocked()) { g.setColour (P.gold); g.setFont (F (10.0f)); g.drawText ("DEMO: drops out every 20 s, MIDI drag off", kW - 312, 56, 300, 12, juce::Justification::centredRight); }
}

void ArpEditor::paintGlassSurfaces (juce::Graphics& g)
{
    namespace gg = ghosty::glass;
    g.setOpacity (1.0f); backdrop.draw (g, kW, kH);   // (a blit takes the current colour's alpha)
    panels.draw (g, { 8.0f, 1.0f, 70.0f, 68.0f }, 20.0f, 0.46f, 6.0f);   // the professor's glass plate (cached)
    glassui::sprites().draw (g, ghostArea.toFloat().withSizeKeepingCentre (52, 52).withY (5), 26.0f, glassui::Disc, Colour (0xff6f5cc6));   // the mood ghost's violet glass bubble (a white ghost reads on it)
    for (auto r : { soundCard, editorCard }) { g.setOpacity (1.0f); panels.draw (g, r.toFloat(), 16.0f, 0.36f, 12.0f); }
}

void ArpEditor::paintOverChildren (juce::Graphics& g)
{
    {   // the lane-control captions: over the controls, on their own opaque chip (always readable)
        juce::Graphics::ScopedSaveState ss (g); g.addTransform (content.getTransform());
        const auto& P = th.p; const int ln = lane() + ((hookLab != nullptr && hookLab->isVisible()) || (samplePanel != nullptr && samplePanel->isVisible()) || (more != nullptr && more->isVisible()) || overlay != nullptr || ! modalFocus().empty() ? 100 : 0); g.setFont (F (9.5f));
        auto lbl = [&] (const juce::String& txt, int x, int y, int w, int h, juce::Justification) {
            const float tw = ghosty::textWidth (F (9.5f), txt) + 6.0f; auto r = juce::Rectangle<float> ((float) x - 3, (float) y, juce::jmin ((float) w + 6, tw), (float) h);
            g.setColour (P.card); g.fillRoundedRectangle (r, 3); g.setColour (P.dim); g.drawText (txt, r.reduced (3, 0), juce::Justification::centredLeft, false); };
    if (ln == LaneArp) { lbl ("CYCLE / RANGE", cycleSeg.getX(), ctlRow.getY() - 12, 160, 12, juce::Justification::centredLeft); lbl ("RATE", rateSeg.getX(), ctlRow.getY() - 12, 60, 12, juce::Justification::centredLeft); }
    if (ln == LaneMelody) { lbl ("FORM / STAGGER", formSeg.getX(), ctlRow.getY() - 12, 160, 12, juce::Justification::centredLeft); lbl ("RANGE", melRangeSeg.getX(), ctlRow.getY() - 12, 60, 12, juce::Justification::centredLeft); }
    if (ln == LaneChords) { lbl ("CHORD LENGTH / RHYTHM / VOICING", chordLen.getX(), ctlRow.getY() - 12, 260, 12, juce::Justification::centredLeft); lbl ("OCTAVE", chordOctDn.getX(), ctlRow.getY() - 12, 60, 12, juce::Justification::centredLeft); }
    }
    if (toastUntil <= 0) return;
    g.addTransform (content.getTransform());
    const auto& P = th.p;
    g.setFont (F (13.0f));
    const float w = juce::jmin (900.0f, ghosty::textWidth (F (13.0f), toastText) + 40);
    auto r = juce::Rectangle<float> (w, 38).withCentre ({ kW / 2.0f, kH - 40.0f });
    fillCard (g, r, 10, P); g.setColour (P.a1); g.drawRoundedRectangle (r, 10, 1.4f);
    g.setColour (P.ink); g.drawFittedText (toastText, r.toNearestInt().reduced (10, 2), juce::Justification::centred, 2);
}

bool ArpEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files) if (f.endsWithIgnoreCase (".arpz") || f.endsWithIgnoreCase (".wav") || f.endsWithIgnoreCase (".aif") || f.endsWithIgnoreCase (".aiff") || f.endsWithIgnoreCase (".flac") || f.endsWithIgnoreCase (".ogg") || f.endsWithIgnoreCase (".mp3") || f.endsWithIgnoreCase (".m4a")) return true;
    return false;
}
void ArpEditor::filesDropped (const juce::StringArray& files, int x, int y)
{
    if (files.isEmpty()) return;
    const juce::File f (files[0]);
    const auto pt = content.getLocalPoint (this, juce::Point<int> (x, y));
    const bool onSound = soundCard.contains (pt) || (samplePanel != nullptr && samplePanel->isVisible() && samplePanel->getBounds().contains (pt));
    if (f.hasFileExtension ("arpz")) { proc.loadPreset (f); toast ("Loaded " + f.getFileNameWithoutExtension()); }
    else if (onSound) loadSampleFile (f);   // on PREVIEW SOUND / the sample panel: YOUR SAMPLE
    else { proc.listenToFile (f); toast ("Listening to " + f.getFileName() + "..."); }
}
bool ArpEditor::keyPressed (const juce::KeyPress& k)
{
    if (hookLab != nullptr && hookLab->isVisible() && hookLab->keyPressed (k)) return true;   // the lab's keys first while it is open
    if (k == juce::KeyPress::spaceKey) { if (! proc.spacePlay()) return false; refresh(); return true; }   // from the PLAY cursor (the host keeps its space while it plays)
    if (k == juce::KeyPress::homeKey)
    {
        const auto sc = proc.section();
        if (sc.on && sc.valid()) { proc.setPlayCursor (sc.start); roll.repaint(); toast ("PLAY cursor: the section start", 1400); return true; }
        proc.setPlayCursor (-1); roll.repaint(); toast ("PLAY cursor: back to the beginning", 1400); return true;
    }
    if (! k.getModifiers().isAnyModifierKeyDown() && juce::CharacterFunctions::toUpperCase (k.getTextCharacter()) == 'L') { toggleLoopSection(); return true; }
    const auto m = k.getModifiers(); const int kc = juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) k.getKeyCode());
    if ((m.isCommandDown() || m.isCtrlDown()) && kc == 'Z') { if (m.isShiftDown()) doRedo(); else doUndo(); return true; }
    if ((m.isCommandDown() || m.isCtrlDown()) && kc == 'Y') { doRedo(); return true; }
    if ((m.isCommandDown() || m.isCtrlDown()) && kc == 'S') { openSave(); return true; }
    return false;
}
void ArpEditor::mouseUp (const juce::MouseEvent&) {}
} // namespace arpui
