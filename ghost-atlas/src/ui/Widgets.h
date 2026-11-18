// SPDX-License-Identifier: AGPL-3.0-or-later
// GHOST ATLAS - the controls. Each one is told how to read its value, how to write it, and when it does nothing
// (a control that does nothing in the current setting is drawn greyed out and does not respond).
#pragma once
#include "Theme.h"

namespace atlas::ui
{
// ============================================================================================ what every control shares
class Control : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<float()> get;                    // its value, in its own units
    std::function<void (float)> set;
    std::function<void()> begin, end;              // a drag starts / ends (for the host's automation)
    std::function<juce::String (float)> text;      // how the value is written
    std::function<bool()> usable;                  // false: it does nothing right now
    juce::String label;
    float lo = 0.0f, hi = 1.0f, def = 0.0f;
    bool log = false, bipolar = false, whole = false;
    juce::StringArray choices;

    float value() const { return get ? get() : def; }
    // greyed out when it does nothing right now; it can still be moved, so a setting can be made ready beforehand
    bool live() const { return isEnabled() && (! usable || usable()); }
    float toNorm (float v) const
    {
        v = juce::jlimit (lo, hi, v);
        if (log && lo > 0.0f) return std::log (v / lo) / std::log (hi / lo);
        return hi > lo ? (v - lo) / (hi - lo) : 0.0f;
    }
    float fromNorm (float n) const
    {
        n = juce::jlimit (0.0f, 1.0f, n);
        float v = (log && lo > 0.0f) ? lo * std::pow (hi / lo, n) : lo + (hi - lo) * n;
        return whole ? std::round (v) : v;
    }
    void change (float v)
    {
        v = juce::jlimit (lo, hi, whole ? std::round (v) : v);
        if (set) set (v);
        repaint();
    }
    void changeOnce (float v) { if (begin) begin(); change (v); if (end) end(); }
    juce::String shown() const
    {
        const float v = value();
        if (text) return text (v);
        if (! choices.isEmpty()) return choices[juce::jlimit (0, choices.size() - 1, (int) std::lround (v))];
        return whole ? juce::String ((int) std::lround (v)) : juce::String (v, 2);
    }
};

// ============================================================================================ knob
class Knob final : public Control
{
public:
    float valueSize = 13.0f, labelSize = 14.0f;
    bool valueInside = true;

    void paint (juce::Graphics& g) override
    {
        const auto& l = look();
        const bool on = live();
        const float a = on ? 1.0f : 0.3f;
        auto b = getLocalBounds().toFloat();
        const float labelH = label.isNotEmpty() ? labelSize + 5.0f : 0.0f;
        auto dial = b.withTrimmedBottom (labelH);
        const float d = juce::jmin (dial.getWidth(), dial.getHeight()) - 4.0f, r = d * 0.5f;
        const float cx = dial.getCentreX(), cy = dial.getCentreY();
        const float a0 = -2.42f, a1 = 2.42f, n = toNorm (value()), an = a0 + (a1 - a0) * n;
        const float thick = juce::jmax (2.6f, d * 0.062f);

        g.setColour (l.deep.darker (0.6f).withAlpha (0.42f * a));
        g.fillEllipse (cx - r + thick * 1.7f, cy - r + thick * 1.7f, d - thick * 3.4f, d - thick * 3.4f);

        juce::Path track; track.addCentredArc (cx, cy, r - thick * 0.5f, r - thick * 0.5f, 0.0f, a0, a1, true);
        g.setColour (l.text ((hover && on ? 0.26f : 0.16f) * a));
        g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const float from = bipolar ? 0.0f : a0;
        if (std::abs (an - from) > 0.01f)
        {
            juce::Path arc; arc.addCentredArc (cx, cy, r - thick * 0.5f, r - thick * 0.5f, 0.0f, juce::jmin (from, an), juce::jmax (from, an), true);
            g.setColour (l.accent.withAlpha (0.95f * a));
            g.strokePath (arc, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        const juce::Point<float> tip (cx + std::sin (an) * (r - thick * 0.5f), cy - std::cos (an) * (r - thick * 0.5f));
        if (on) glow (g, tip, thick * 2.6f, l.lantern, dragging ? 0.75f : 0.4f);
        g.setColour ((on ? l.lantern : l.text (0.5f)).withAlpha (a));
        g.fillEllipse (tip.x - thick * 0.78f, tip.y - thick * 0.78f, thick * 1.56f, thick * 1.56f);

        if (valueInside && d >= 40.0f)
        {
            g.setColour (l.text (0.95f * a));
            drawFitted (g, shown(), { cx - r + thick * 2.0f, cy - valueSize * 0.6f, d - thick * 4.0f, valueSize * 1.2f }, juce::Justification::centred, sans (valueSize), 0.7f);
        }
        if (label.isNotEmpty())
        {
            g.setColour (l.text (0.78f * a));
            drawFitted (g, label, b.removeFromBottom (labelH), juce::Justification::centred, sans (labelSize), 0.7f);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! isEnabled() || e.mods.isPopupMenu()) return;
        dragging = true; startNorm = toNorm (value());
        if (begin) begin();
        repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging) return;
        const float span = e.mods.isShiftDown() ? 1100.0f : 190.0f;
        change (fromNorm (startNorm + (float) (-e.getDistanceFromDragStartY() + e.getDistanceFromDragStartX() * 0.35f) / span));
    }
    void mouseUp (const juce::MouseEvent&) override { if (dragging) { dragging = false; if (end) end(); repaint(); } }
    void mouseDoubleClick (const juce::MouseEvent&) override { if (isEnabled()) changeOnce (def); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        if (! isEnabled() || std::abs (w.deltaY) < 1.0e-4f) return;
        const float step = whole ? 1.0f / juce::jmax (1.0f, hi - lo) : 0.02f;
        changeOnce (fromNorm (toNorm (value()) + (w.deltaY > 0 ? step : -step)));
    }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }

private:
    bool dragging = false, hover = false;
    float startNorm = 0.0f;
};

// ============================================================================================ bar (a level in a row)
class Bar final : public Control
{
public:
    bool showValue = true;
    void paint (juce::Graphics& g) override
    {
        const auto& l = look();
        const bool on = live();
        const float a = on ? 1.0f : 0.3f;
        auto b = getLocalBounds().toFloat();
        if (label.isNotEmpty()) { g.setColour (l.text (0.78f * a)); drawFitted (g, label, b.removeFromLeft (labelWidth).withTrimmedRight (8.0f), juce::Justification::centredLeft, sans (14.0f)); }
        if (showValue) { g.setColour (l.text (0.92f * a)); drawFitted (g, shown(), b.removeFromRight (valueWidth), juce::Justification::centredRight, sans (13.5f)); b.removeFromRight (6.0f); }
        track = b.withSizeKeepingCentre (b.getWidth(), 6.0f);
        g.setColour (l.text ((hover && on ? 0.24f : 0.15f) * a));
        g.fillRoundedRectangle (track, 3.0f);
        const float n = toNorm (value());
        const float x0 = bipolar ? track.getCentreX() : track.getX(), x1 = track.getX() + track.getWidth() * n;
        g.setColour (l.accent.withAlpha (0.95f * a));
        g.fillRoundedRectangle (juce::Rectangle<float> (juce::jmin (x0, x1), track.getY(), std::abs (x1 - x0), track.getHeight()), 3.0f);
        if (on) glow (g, { x1, track.getCentreY() }, 11.0f, l.lantern, 0.4f);
        g.setColour ((on ? l.lantern : l.text (0.5f)).withAlpha (a));
        g.fillEllipse (x1 - 5.0f, track.getCentreY() - 5.0f, 10.0f, 10.0f);
    }
    void mouseDown (const juce::MouseEvent& e) override { if (! isEnabled() || e.mods.isPopupMenu()) return; dragging = true; if (begin) begin(); mouseDrag (e); }
    void mouseDrag (const juce::MouseEvent& e) override { if (dragging && track.getWidth() > 1.0f) change (fromNorm (((float) e.x - track.getX()) / track.getWidth())); }
    void mouseUp (const juce::MouseEvent&) override { if (dragging) { dragging = false; if (end) end(); } }
    void mouseDoubleClick (const juce::MouseEvent&) override { if (isEnabled()) changeOnce (def); }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    float labelWidth = 70.0f, valueWidth = 58.0f;
private:
    juce::Rectangle<float> track;
    bool dragging = false, hover = false;
};

// ============================================================================================ switch
class Switch final : public Control
{
public:
    void paint (juce::Graphics& g) override
    {
        const auto& l = look();
        const bool on = live(), set = value() > 0.5f;
        const float a = on ? 1.0f : 0.3f;
        auto b = getLocalBounds().toFloat();
        auto pill = b.removeFromLeft (34.0f).withSizeKeepingCentre (32.0f, 17.0f);
        g.setColour (set ? l.lantern.withAlpha (0.9f * a) : l.text (0.16f * a));
        g.fillRoundedRectangle (pill, 8.5f);
        const float kx = set ? pill.getRight() - 15.0f : pill.getX() + 2.0f;
        g.setColour (set ? l.ink().withAlpha (a) : l.text (0.75f * a));
        g.fillEllipse (kx, pill.getY() + 2.0f, 13.0f, 13.0f);
        if (label.isNotEmpty()) { g.setColour (l.text ((set ? 0.95f : 0.72f) * a)); drawFitted (g, label, b.withTrimmedLeft (8.0f), juce::Justification::centredLeft, sans (14.0f)); }
    }
    void mouseDown (const juce::MouseEvent&) override { if (isEnabled()) changeOnce (value() > 0.5f ? 0.0f : 1.0f); }
};

// ============================================================================================ choice (a menu)
class Choice final : public Control
{
public:
    juce::Component* menuParent = nullptr;       // the scaled canvas, so the menu is the same size as the rest
    std::function<void (juce::PopupMenu&)> fill;  // the page builds the menu itself (sections, extra entries) ...
    std::function<void (int)> picked;             // ... and is told which entry was chosen
    std::function<juce::String()> caption;        // what the box shows, when it is not simply the chosen entry
    void paint (juce::Graphics& g) override
    {
        const auto& l = look();
        const bool on = live();
        const float a = on ? 1.0f : 0.3f;
        auto b = getLocalBounds().toFloat();
        if (label.isNotEmpty()) { g.setColour (l.text (0.78f * a)); drawFitted (g, label, b.removeFromLeft (labelWidth), juce::Justification::centredLeft, sans (14.0f)); }
        auto box = b.reduced (0.5f, 1.0f);
        g.setColour (l.text ((hover && on ? 0.16f : 0.09f) * a)); g.fillRoundedRectangle (box, 7.0f);
        g.setColour (l.text (0.16f * a)); g.drawRoundedRectangle (box, 7.0f, 1.0f);
        g.setColour (l.text (0.95f * a));
        drawFitted (g, caption ? caption() : shown(), box.withTrimmedLeft (10.0f).withTrimmedRight (22.0f), juce::Justification::centredLeft, sans (14.5f));
        chevron (g, box.removeFromRight (22.0f), 2, l.text (0.6f * a), 1.5f);
    }
    void mouseDown (const juce::MouseEvent&) override
    {
        if (! isEnabled() || (choices.isEmpty() && ! fill)) return;
        juce::PopupMenu m;
        m.setLookAndFeel (&chartLookAndFeel());
        if (fill) fill (m);
        else { const int now = (int) std::lround (value()); for (int i = 0; i < choices.size(); ++i) m.addItem (i + 1, choices[i], true, i == now); }
        auto opts = juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (juce::jmax (120, getWidth() - (int) labelWidth)).withMaximumNumColumns (2);
        if (menuParent != nullptr) opts = opts.withParentComponent (menuParent);
        m.showMenuAsync (opts, [safe = juce::Component::SafePointer<Choice> (this)] (int r) {
            if (safe == nullptr || r <= 0) return;
            if (safe->picked) { safe->picked (r); safe->repaint(); } else safe->changeOnce ((float) (r - 1));
        });
    }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    float labelWidth = 0.0f;
private:
    bool hover = false;
};

// ============================================================================================ segments (a few choices side by side)
class Segments final : public Control
{
public:
    void paint (juce::Graphics& g) override
    {
        const auto& l = look();
        const bool on = live();
        const float a = on ? 1.0f : 0.3f;
        const auto b = getLocalBounds().toFloat().reduced (0.5f, 1.0f);
        g.setColour (l.text (0.08f * a)); g.fillRoundedRectangle (b, 8.0f);
        const int n = juce::jmax (1, choices.size()), now = (int) std::lround (value());
        const float w = b.getWidth() / (float) n;
        for (int i = 0; i < n; ++i)
        {
            const auto cell = juce::Rectangle<float> (b.getX() + w * (float) i, b.getY(), w, b.getHeight()).reduced (2.0f);
            if (i == now) { g.setColour (l.lantern.withAlpha (0.92f * a)); g.fillRoundedRectangle (cell, 6.0f); }
            g.setColour (i == now ? l.ink().withAlpha (a) : l.text ((i == hoverIndex && on ? 0.95f : 0.7f) * a));
            drawFitted (g, choices[i], cell.reduced (4.0f, 0.0f), juce::Justification::centred, i == now ? sansBold (13.5f) : sans (13.5f), 0.68f);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override { if (isEnabled() && ! choices.isEmpty()) changeOnce ((float) juce::jlimit (0, choices.size() - 1, e.x * choices.size() / juce::jmax (1, getWidth()))); }
    void mouseMove (const juce::MouseEvent& e) override { const int h = e.x * juce::jmax (1, choices.size()) / juce::jmax (1, getWidth()); if (h != hoverIndex) { hoverIndex = h; repaint(); } }
    void mouseExit (const juce::MouseEvent&) override { hoverIndex = -1; repaint(); }
private:
    int hoverIndex = -1;
};

// ============================================================================================ buttons
class TextBtn final : public juce::Component, public juce::SettableTooltipClient
{
public:
    enum Style { plain, lit, quiet, glass };     // glass: sits straight on the picture, so it brings its own shade
    juce::String text;
    Style style = plain;
    bool selected = false;
    std::function<void()> onClick;
    std::function<bool()> usable;
    float size = 14.5f;
    bool caps = false;                           // small capitals (the Fell face)

    bool live() const { return isEnabled() && (! usable || usable()); }
    void paint (juce::Graphics& g) override
    {
        const auto& l = look();
        const bool on = live();
        const float a = on ? 1.0f : 0.3f;
        const auto b = getLocalBounds().toFloat().reduced (0.5f, 1.0f);
        const float radius = juce::jmin (b.getHeight() * 0.5f, 9.0f);
        juce::Colour ink = l.text (0.9f * a);
        if (style == lit)
        {
            g.setColour (l.lantern.withAlpha ((down ? 0.8f : 1.0f) * a)); g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
            if (hover && on) { g.setColour (juce::Colours::white.withAlpha (0.18f)); g.fillRoundedRectangle (b, b.getHeight() * 0.5f); }
            ink = l.ink().withAlpha (a);
        }
        else if (style == plain)
        {
            g.setColour (selected ? l.lantern.withAlpha (0.2f * a) : l.text ((hover && on ? 0.17f : 0.09f) * a)); g.fillRoundedRectangle (b, radius);
            g.setColour (selected ? l.lantern.withAlpha (0.7f * a) : l.text (0.17f * a)); g.drawRoundedRectangle (b, radius, 1.0f);
            if (selected) ink = l.lantern.withAlpha (a);
        }
        else if (style == glass)
        {
            g.setColour (l.deep.darker (0.7f).withAlpha ((hover && on ? 0.76f : 0.58f) * a)); g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
            g.setColour (l.text (0.22f * a)); g.drawRoundedRectangle (b, b.getHeight() * 0.5f, 1.0f);
        }
        else
        {
            ink = selected ? l.lantern.withAlpha (a) : l.text ((hover && on ? 1.0f : 0.72f) * a);
            if (selected) { g.setColour (l.lantern.withAlpha (a)); g.fillRoundedRectangle (b.getCentreX() - 9.0f, b.getBottom() - 3.0f, 18.0f, 2.0f, 1.0f); }
        }
        g.setColour (ink);
        drawFitted (g, text, b.reduced (6.0f, 0.0f), juce::Justification::centred, caps ? fellCaps (size) : (style == lit ? sansBold (size) : sans (size)), 0.7f);
    }
    void mouseDown (const juce::MouseEvent&) override { if (live()) { down = true; repaint(); } }
    void mouseUp (const juce::MouseEvent& e) override { const bool was = down; down = false; repaint(); if (was && live() && contains (e.getPosition()) && onClick) onClick(); }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
private:
    bool hover = false, down = false;
};

// a small picture button (an arrow, a heart, a play triangle): the page draws it
class IconBtn final : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<void (juce::Graphics&, juce::Rectangle<float>, bool hover)> draw;
    std::function<void()> onClick;
    std::function<bool()> usable;
    bool live() const { return isEnabled() && (! usable || usable()); }
    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        if (hover && live()) { g.setColour (look().text (0.1f)); g.fillEllipse (b.withSizeKeepingCentre (juce::jmin (b.getWidth(), b.getHeight()), juce::jmin (b.getWidth(), b.getHeight()))); }
        g.setOpacity (live() ? 1.0f : 0.3f);
        if (draw) draw (g, b, hover && live());
    }
    void mouseUp (const juce::MouseEvent& e) override { if (live() && contains (e.getPosition()) && onClick) onClick(); }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
private:
    bool hover = false;
};

// ============================================================================================ a list of rows the page paints
class RowList final : public juce::ListBox, private juce::ListBoxModel
{
public:
    std::function<int()> count;
    std::function<void (int row, juce::Graphics&, juce::Rectangle<float>, bool selected, bool hover)> paintRow;
    std::function<void (int row, const juce::MouseEvent&)> clicked;
    std::function<void (int row)> doubleClicked, chosen;

    RowList()
    {
        setModel (this);
        setLookAndFeel (&chartLookAndFeel());
        setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        setRowHeight (30);
        setMouseMoveSelectsRows (false);
        setWantsKeyboardFocus (false);          // (the keyboard stays with the host: see TextBox)
    }
    ~RowList() override { setLookAndFeel (nullptr); setModel (nullptr); }
    int getNumRows() override { return count ? count() : 0; }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        if (paintRow) paintRow (row, g, juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h), selected, false);
    }
    void listBoxItemClicked (int row, const juce::MouseEvent& e) override { if (clicked) clicked (row, e); }
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override { if (doubleClicked) doubleClicked (row); }
    void selectedRowsChanged (int row) override { if (chosen && row >= 0) chosen (row); }
    void returnKeyPressed (int row) override { if (doubleClicked) doubleClicked (row); }
};

// ============================================================================================ a text box (search, names)
// The keyboard belongs to the host: its typing keys play notes. A window hands the keyboard to the first thing in it
// that asks for it, on any click anywhere and whenever the window comes forward, so a text box does not ask for it at
// all until it is clicked into. It gives it back on Return, on Escape, and on a click anywhere else.
class TextBox final : public juce::Component
{
public:
    juce::TextEditor editor;
    juce::String hint;
    std::function<void (const juce::String&)> onChange;
    TextBox()
    {
        editor.setLookAndFeel (&chartLookAndFeel());
        editor.setFont (sans (15.5f));
        editor.setIndents (0, 0);
        editor.setBorder ({});
        editor.setJustification (juce::Justification::centredLeft);
        editor.setWantsKeyboardFocus (false);
        editor.onTextChange = [this] { if (onChange) onChange (editor.getText()); repaint(); };
        editor.onFocusLost = [this] { letGo(); };
        editor.onEscapeKey = [this] { letGo(); };
        editor.onReturnKey = [this] { letGo(); };
        addAndMakeVisible (editor);
        juce::Desktop::getInstance().addGlobalMouseListener (this);
    }
    ~TextBox() override { juce::Desktop::getInstance().removeGlobalMouseListener (this); editor.setLookAndFeel (nullptr); }
    void setHint (const juce::String& h) { hint = h; editor.setTextToShowWhenEmpty (h, look().text (0.45f)); }
    void resized() override { editor.setBounds (getLocalBounds().reduced (12, 4)); }
    void paint (juce::Graphics& g) override
    {
        const auto& l = look();
        const auto b = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (l.text (0.08f)); g.fillRoundedRectangle (b, 8.0f);
        g.setColour (editor.hasKeyboardFocus (true) ? l.lantern.withAlpha (0.7f) : l.text (0.16f)); g.drawRoundedRectangle (b, 8.0f, 1.0f);
    }
    // every press of the mouse, anywhere (this box listens to them all): in the box it takes the keyboard, elsewhere it gives it back
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.eventComponent == this || isParentOf (e.eventComponent)) take(); else letGo();
    }
    void take()
    {
        editor.setWantsKeyboardFocus (true);
        if (! editor.hasKeyboardFocus (true)) editor.grabKeyboardFocus();
        repaint();
    }
    void letGo()
    {
        if (! editor.getWantsKeyboardFocus()) return;
        editor.setWantsKeyboardFocus (false);
        if (editor.hasKeyboardFocus (true)) editor.giveAwayKeyboardFocus();
        repaint();
    }
    bool typing() const { return editor.getWantsKeyboardFocus(); }
};

// ============================================================================================ the keyboard
// Keys of pale glass. A key that is down is lit like a lantern and throws its light upwards (into the strip of room kept
// above the keys); the light lingers a moment after the key comes up. Keys no layer plays are dimmed.
class Keys final : public juce::Component
{
public:
    std::function<void (int note, float velocity, bool on)> onNote;
    std::function<bool (int note)> held, playable;
    int first = 24, last = 96;                   // C1 .. C7
    float room = 18.0f;                          // above the keys, for their light

    // where a note's key is, left to right, in a keyboard this wide (the picture of the sound above it uses the same)
    static float centreOf (int note, int first, int last, float width)
    {
        int whites = 0, before = 0;
        for (int n = first; n <= last; ++n) if (! isBlack (n)) { ++whites; if (n < note) ++before; }
        const float w = width / (float) juce::jmax (1, whites);
        return isBlack (note) ? w * (float) before : w * ((float) before + 0.5f);
    }

    // called as the screen ticks: lets the light of released keys fade. True when the picture changed.
    bool step()
    {
        bool moved = false;
        for (int n = first; n <= last; ++n)
        {
            const bool down = (held && held (n)) || n == mouseNote;
            auto& v = lit[(size_t) n];
            const float to = down ? 1.0f : (v > 0.02f ? v * 0.78f : 0.0f);
            if (std::abs (to - v) > 0.004f) { v = to; moved = true; }
        }
        if (moved) repaint();
        return moved;
    }

    void paint (juce::Graphics& g) override
    {
        const auto& l = look();
        layout();
        const float top = room, h = (float) getHeight() - room;
        // the light of the keys that are down, behind everything
        for (int n = first; n <= last; ++n)
        {
            const float v = lit[(size_t) n];
            if (v <= 0.01f) continue;
            const auto r = keyRect (n);
            const float cx = r.getCentreX(), w = r.getWidth();
            juce::ColourGradient beam (l.lantern.withAlpha (0.62f * v), cx, top, l.lantern.withAlpha (0.0f), cx, 0.0f, false);
            g.setGradientFill (beam);
            g.fillRect (juce::Rectangle<float> (cx - w * 0.5f, 0.0f, w, top));
            glow (g, { cx, top + 1.0f }, 26.0f + 10.0f * v, l.lantern, 0.5f * v);
        }
        // white keys
        for (int n = first; n <= last; ++n)
            if (! isBlack (n))
            {
                const auto r = keyRect (n).reduced (0.7f, 0.0f);
                const bool can = ! playable || playable (n);
                const float v = lit[(size_t) n], a = can ? 1.0f : 0.3f;
                juce::Path key; key.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), 3.2f, 3.2f, false, false, true, true);
                juce::ColourGradient body (l.paper.withAlpha (0.52f * a), 0.0f, top, l.paper.withAlpha (0.66f * a), 0.0f, top + h, false);
                body.addColour (0.72, l.paper.withAlpha (0.78f * a));
                g.setGradientFill (body); g.fillPath (key);
                if (v > 0.01f)
                {
                    juce::ColourGradient hot (l.lantern.withAlpha (0.9f * v), 0.0f, top, l.lantern.interpolatedWith (juce::Colours::white, 0.55f).withAlpha (v), 0.0f, top + h, false);
                    g.setGradientFill (hot); g.fillPath (key);
                }
                g.setColour (juce::Colours::white.withAlpha (0.22f * a)); g.fillRect (juce::Rectangle<float> (r.getX(), top, 1.0f, h - 4.0f));          // the lit edge
                g.setColour (l.ink().withAlpha (0.2f * a)); g.fillRect (juce::Rectangle<float> (r.getX() + 1.5f, top + h - 3.2f, r.getWidth() - 3.0f, 1.6f));   // the lip
                if (n % 12 == 0)
                {
                    g.setColour (l.ink().withAlpha (can ? 0.78f : 0.42f)); g.setFont (sans (10.5f));
                    g.drawText ("C" + juce::String (n / 12 - 1), r.withTop (r.getBottom() - 17.0f).withHeight (13.0f), juce::Justification::centred, false);
                }
            }
        // the shade the black keys and the rail throw on the white ones
        {
            juce::ColourGradient shade (l.ink().withAlpha (0.34f), 0.0f, top, l.ink().withAlpha (0.0f), 0.0f, top + 9.0f, false);
            g.setGradientFill (shade); g.fillRect (juce::Rectangle<float> (0.0f, top, (float) getWidth(), 9.0f));
        }
        // black keys
        for (int n = first; n <= last; ++n)
            if (isBlack (n))
            {
                const auto r = keyRect (n);
                const bool can = ! playable || playable (n);
                const float v = lit[(size_t) n];
                juce::Path key; key.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), 2.6f, 2.6f, false, false, true, true);
                g.setColour (juce::Colours::black.withAlpha (can ? 0.3f : 0.12f));
                g.fillRoundedRectangle (r.translated (0.0f, 2.2f).expanded (0.8f, 0.0f), 2.6f);                                                       // its shadow
                const auto dark = l.deep.darker (0.9f);
                juce::ColourGradient body (dark.withAlpha (can ? 0.97f : 0.62f), 0.0f, r.getY(), dark.brighter (0.22f).withAlpha (can ? 0.97f : 0.62f), 0.0f, r.getBottom(), false);
                g.setGradientFill (body); g.fillPath (key);
                const auto face = juce::Rectangle<float> (r.getX() + 1.6f, r.getY(), r.getWidth() - 3.2f, r.getHeight() - 5.0f);
                juce::ColourGradient sheen (juce::Colours::white.withAlpha (can ? 0.13f : 0.05f), 0.0f, face.getY(), juce::Colours::white.withAlpha (0.015f), 0.0f, face.getBottom(), false);
                g.setGradientFill (sheen); g.fillRoundedRectangle (face, 1.6f);
                if (v > 0.01f)
                {
                    juce::ColourGradient hot (l.lantern.withAlpha (0.85f * v), 0.0f, r.getY(), l.lantern.interpolatedWith (juce::Colours::white, 0.45f).withAlpha (v), 0.0f, r.getBottom(), false);
                    g.setGradientFill (hot); g.fillPath (key);
                }
            }
        // the rail the keys hang from, in the world's own colour
        g.setColour (l.ink().withAlpha (0.55f)); g.fillRect (juce::Rectangle<float> (0.0f, top - 2.0f, (float) getWidth(), 3.0f));
        g.setColour (l.accent.withAlpha (0.55f)); g.fillRect (juce::Rectangle<float> (0.0f, top - 2.0f, (float) getWidth(), 1.0f));
    }
    void mouseDown (const juce::MouseEvent& e) override { press (noteAt (e.position), e.position); }
    void mouseDrag (const juce::MouseEvent& e) override { const int n = noteAt (e.position); if (n != mouseNote) press (n, e.position); }
    void mouseUp (const juce::MouseEvent&) override { press (-1, {}); }
    bool hitTest (int, int y) override { return (float) y >= room - 2.0f; }

private:
    static bool isBlack (int n) { const int k = n % 12; return k == 1 || k == 3 || k == 6 || k == 8 || k == 10; }
    void layout() { int whites = 0; for (int n = first; n <= last; ++n) if (! isBlack (n)) ++whites; whiteW = (float) getWidth() / (float) juce::jmax (1, whites); }
    juce::Rectangle<float> keyRect (int n) const
    {
        const float h = (float) getHeight() - room;
        int whites = 0; for (int k = first; k < n; ++k) if (! isBlack (k)) ++whites;
        if (! isBlack (n)) return { whiteW * (float) whites, room, whiteW, h };
        return { whiteW * (float) whites - whiteW * 0.3f, room, whiteW * 0.6f, h * 0.61f };
    }
    int noteAt (juce::Point<float> p) const
    {
        for (int n = first; n <= last; ++n) if (isBlack (n) && keyRect (n).contains (p)) return n;
        for (int n = first; n <= last; ++n) if (! isBlack (n) && keyRect (n).contains (p)) return n;
        return -1;
    }
    void press (int n, juce::Point<float> p)
    {
        if (mouseNote >= 0 && onNote) onNote (mouseNote, 0.0f, false);
        mouseNote = n;
        if (n >= 0 && onNote) onNote (n, juce::jlimit (0.25f, 1.0f, 0.3f + 0.7f * (p.y - room) / juce::jmax (1.0f, (float) getHeight() - room)), true);
        step();
        repaint();
    }
    float whiteW = 20.0f;
    int mouseNote = -1;
    std::array<float, 128> lit {};
};

// ============================================================================================ output meter
class Meter final : public juce::Component
{
public:
    void setLevels (float left, float right)
    {
        auto follow = [] (float& shown, float in) { const float v = juce::jlimit (0.0f, 1.0f, (gainToDb (juce::jmax (1.0e-6f, in)) + 54.0f) / 54.0f); shown = v > shown ? v : shown + (v - shown) * 0.22f; };
        const float a = l, b = r;
        follow (l, left); follow (r, right);
        if (std::abs (a - l) + std::abs (b - r) > 0.002f) repaint();
    }
    void paint (juce::Graphics& g) override
    {
        const auto& lk = look();
        auto b = getLocalBounds().toFloat();
        const float w = (b.getWidth() - 3.0f) * 0.5f;
        for (int c = 0; c < 2; ++c)
        {
            const auto bar = juce::Rectangle<float> (b.getX() + (w + 3.0f) * (float) c, b.getY(), w, b.getHeight());
            g.setColour (lk.text (0.12f)); g.fillRoundedRectangle (bar, 2.0f);
            const float v = c == 0 ? l : r;
            if (v > 0.003f)
            {
                g.setColour (v > 0.985f ? lk.danger : lk.accent.withAlpha (0.92f));
                g.fillRoundedRectangle (bar.withTop (bar.getBottom() - bar.getHeight() * v), 2.0f);
            }
        }
    }
private:
    float l = 0.0f, r = 0.0f;
};
} // namespace atlas::ui
