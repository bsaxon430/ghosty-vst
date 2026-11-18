#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "GhostArt.h"
#include <deque>

namespace ghosty
{
// ---------------------------------------------------------------- look and feel
class GhostLnF : public juce::LookAndFeel_V4
{
public:
    GhostLnF()
    {
        setColour (juce::ResizableWindow::backgroundColourId, col::bg);
        setColour (juce::TextButton::buttonColourId, col::card2);
        setColour (juce::TextButton::buttonOnColourId, col::slime);
        setColour (juce::TextButton::textColourOffId, col::white);
        setColour (juce::TextButton::textColourOnId, col::bg);
        setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff06070a));
        setColour (juce::ComboBox::outlineColourId, col::slime.withAlpha (0.35f));
        setColour (juce::ComboBox::textColourId, col::white);
        setColour (juce::ComboBox::arrowColourId, col::slime);
        setColour (juce::PopupMenu::backgroundColourId, col::card);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, col::ecto);
        setColour (juce::PopupMenu::textColourId, col::white);
        setColour (juce::Slider::textBoxTextColourId, col::slime);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Label::textColourId, col::dim);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) override
    {
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4);
        const float rad = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f, cx = r.getCentreX(), cy = r.getCentreY();
        juce::Path track; track.addCentredArc (cx, cy, rad - 3, rad - 3, 0, a0, a1, true);
        g.setColour (juce::Colours::white.withAlpha (0.08f)); g.strokePath (track, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const float ang = a0 + pos * (a1 - a0);
        juce::Path val; val.addCentredArc (cx, cy, rad - 3, rad - 3, 0, a0, ang, true);
        juce::ColourGradient gr (col::ecto, cx - rad, cy, col::slime, cx + rad, cy, false);
        g.setGradientFill (gr); g.strokePath (val, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (juce::Colour (0xff1a1d28)); g.fillEllipse (cx - rad * 0.62f, cy - rad * 0.62f, rad * 1.24f, rad * 1.24f);
        juce::Path ptr; ptr.startNewSubPath (cx, cy); ptr.lineTo (cx + std::sin (ang) * rad * 0.55f, cy - std::cos (ang) * rad * 0.55f);
        g.setColour (s.isMouseOverOrDragging() ? col::slime : col::white); g.strokePath (ptr, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (1);
        const bool on = b.getToggleState();
        if (on) { g.setColour (col::slime.withAlpha (0.25f)); g.fillRoundedRectangle (r.expanded (2), 10); }
        juce::ColourGradient gr (on ? col::slime : juce::Colour (0xff22263a), 0, r.getY(), on ? juce::Colour (0xff25c96d) : juce::Colour (0xff14161f), 0, r.getBottom(), false);
        g.setGradientFill (gr); g.fillRoundedRectangle (r, 8);
        g.setColour ((on ? col::slime : col::ecto).withAlpha (over || down ? 0.7f : 0.3f)); g.drawRoundedRectangle (r, 8, 1.0f);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return font (juce::jmin (13.0f, (float) h * 0.45f)); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return font (14.0f); }
    juce::Font getLabelFont (juce::Label&) override { return font (10.0f); }
    juce::Font getPopupMenuFont() override { return font (14.0f, false); }
};

// ---------------------------------------------------------------- knob with caption
struct GhostKnob : public juce::Component
{
    juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Label  label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;

    GhostKnob (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& caption)
    {
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 16);
        addAndMakeVisible (slider); addAndMakeVisible (label);
        label.setText (caption, juce::dontSendNotification); label.setJustificationType (juce::Justification::centred);
        att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, id, slider);
    }
    void resized() override { auto r = getLocalBounds(); label.setBounds (r.removeFromTop (14)); slider.setBounds (r); }
};

// ---------------------------------------------------------------- button that is "on" only while held
struct HoldButton : public juce::Component
{
    juce::String text; juce::Colour colour { col::ecto };
    std::function<void (bool)> onHold;
    bool down = false;
    HoldButton (juce::String t, juce::Colour c) : text (std::move (t)), colour (c) {}
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2);
        if (down) { g.setColour (colour.withAlpha (0.35f)); g.fillRoundedRectangle (r.expanded (2), 12); }
        juce::ColourGradient gr (down ? colour : colour.withAlpha (0.18f), 0, r.getY(), down ? colour.darker (0.4f) : juce::Colour (0xff150f22), 0, r.getBottom(), false);
        g.setGradientFill (gr); g.fillRoundedRectangle (r, 10);
        g.setColour (colour.withAlpha (0.6f)); g.drawRoundedRectangle (r, 10, 1.2f);
        g.setColour (down ? col::white : colour.brighter (0.4f)); g.setFont (font (15.0f)); g.drawText (text, r, juce::Justification::centred);
    }
    void mouseDown (const juce::MouseEvent&) override { down = true; if (onHold) onHold (true); repaint(); }
    void mouseUp (const juce::MouseEvent&) override { down = false; if (onHold) onHold (false); repaint(); }
};

// ---------------------------------------------------------------- XY pad of draggable glitching ghosts
class XYGhostPad : public juce::Component, private juce::Timer
{
public:
    struct Ghost
    {
        juce::String name; juce::Colour colour;
        juce::RangedAudioParameter* px = nullptr; juce::RangedAudioParameter* py = nullptr;
        GhostLook look; float glitch = 0, speed = 0; bool held = false; bool flip = false;
        std::deque<juce::Point<float>> trail;
        // where the ghost looks (screen space, length 0..1; 0 = its resting look) and its drag glitch 0..1, from its motion: a short
        // smoothed velocity of its position (a mouse drag, or automation / MORPH / macros moving its parameters - gentler then).
        // Only custom ghost art reads them (XYGhostPad::ghostPainter); the classic look ignores them.
        juce::Point<float> gaze, vel, lastPos, heldGaze; bool hasLast = false; float dragGlitch = 0;
    };

    std::function<float()> levelSource;     // 0..1 output level, makes ghosts breathe

    // Opt-in custom ghost art (Ghosty Pro round 3, GhostyPro/ProGhostArt.h). Unset = the classic look, exactly as before.
    // Called twice per ghost and paint: glowPass = true (all glows first, under every ghost), then false (the ghost itself; its label
    // is still drawn by the pad). Return false to fall back to the classic look for that ghost.
    struct GhostPaint
    {
        const Ghost& ghost; int index; juce::Point<float> centre; float size, glitch, level, time; bool glowPass; juce::Colour backdrop;
    };
    std::function<bool (juce::Graphics&, const GhostPaint&)> ghostPainter;
    bool painterDrawsNames = false;          // the painter writes each ghost's name itself: the pad skips its floating name label

    void addGhost (const juce::String& name, juce::Colour c, juce::RangedAudioParameter* x, juce::RangedAudioParameter* y, int lookIndex)
    {
        Ghost gh; gh.name = name; gh.colour = c; gh.px = x; gh.py = y; gh.look = lookFor (lookIndex);
        ghosts.push_back (std::move (gh));
    }

    XYGhostPad() { startTimerHz (40); }

    // GLASS (round 3): an optional theme-aware backdrop. Off by default (backdrop unset, or it returns nullptr): the original black pad,
    // pixel-identical, in every plugin. Ghosty Pro sets it (GhostyPro/ProGlass.cpp): PURPLE = a deep-purple screen with a light violet grid,
    // LIGHT = a pale frosted glass screen with a soft grey-violet grid, its ghosts outlined and their colours deepened so they still read.
    struct Backdrop
    {
        juce::Colour top, bottom;                 // vertical gradient of the screen (opaque)
        juce::Colour grid; float gridAlpha = 0.1f;
        juce::Colour rim, edge;                   // fine outer rim; a bright inner top edge (transparent = none)
        juce::Colour ghostOutline;                // drawn round each ghost (transparent = none)
        float glowAlpha = 0.42f, trailAlpha = 0.4f, rainAlpha = 1.0f;
        std::function<juce::Colour (juce::Colour)> ink;      // a ghost's colour -> its trail / glow / rain colour on this screen
        std::function<juce::Colour (juce::Colour)> label;    // a ghost's colour -> its name / readout text colour (>= 4.5:1 on the screen)
    };
    std::function<const Backdrop* ()> backdrop;

    void paint (juce::Graphics& g) override
    {
        if (const Backdrop* bd = backdrop ? backdrop() : nullptr) { paintThemed (g, *bd); return; }
        auto r = getLocalBounds().toFloat();
        g.setColour (padBg); g.fillRoundedRectangle (r, 12);
        g.setColour (col::ecto.withAlpha (0.08f));
        for (int i = 0; i <= 10; ++i)
        {
            const float x = m + (float) i / 10.0f * (r.getWidth() - 2 * m), y = m + (float) i / 10.0f * (r.getHeight() - 2 * m);
            g.drawLine (x, m, x, r.getHeight() - m); g.drawLine (m, y, r.getWidth() - m, y);
        }
        rain.paint (g);
        const float lvl = levelSource ? levelSource() : 0.0f;
        for (auto& gh : ghosts)
        {
            auto p = pos (gh);
            for (size_t k = 1; k < gh.trail.size(); ++k)
            {
                const float a = (float) k / (float) gh.trail.size();
                g.setColour (gh.colour.withAlpha (a * 0.4f)); g.drawLine ({ gh.trail[k - 1], gh.trail[k] }, a * 8.0f);
            }
            if (ghostPainter && ghostPainter (g, { gh, (int) (&gh - &ghosts[0]), bobbed (gh, p), 30.0f + (gh.held ? 10.0f : 0.0f) + lvl * 8.0f, gh.glitch, lvl, (float) phase, true, padBg }))
                continue;
            const float R = 26.0f + (gh.held ? 8.0f : 0.0f) + lvl * 12.0f;
            juce::ColourGradient glow (gh.colour.withAlpha (0.42f), p.x, p.y, gh.colour.withAlpha (0.0f), p.x + R * 2.2f, p.y, true);
            g.setGradientFill (glow); g.fillEllipse (p.x - R * 2.2f, p.y - R * 2.2f, R * 4.4f, R * 4.4f);
        }
        for (auto& gh : ghosts)
        {
            auto p = pos (gh);
            if (! (ghostPainter && ghostPainter (g, { gh, (int) (&gh - &ghosts[0]), bobbed (gh, p), 30.0f + (gh.held ? 10.0f : 0.0f) + lvl * 8.0f, gh.glitch, lvl, (float) phase, false, padBg })))
            {
                const bool locked = gh.held && juce::Random::getSystemRandom().nextFloat() > 0.15f;
                drawGhost (g, gh.look, p.x, p.y + std::sin ((float) phase * 2.2f + (float) (&gh - &ghosts[0])) * 3.0f,
                           30.0f + (gh.held ? 10.0f : 0.0f) + lvl * 8.0f, gh.glitch, locked, 1.0f, gh.flip);
            }
            if (painterDrawsNames && ghostPainter) continue;
            g.setColour (gh.colour); g.setFont (font (10.0f));
            g.drawText (gh.name, juce::Rectangle<float> (p.x - 40, p.y < m + 56 ? p.y + 40 : p.y - 52, 80, 14), juce::Justification::centred);
        }
        if (readout.isNotEmpty())
        {
            g.setColour (readoutColour); g.setFont (font (11.0f));
            g.drawText (readout, r.reduced (10, 6).removeFromTop (16), juce::Justification::centredRight);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        Ghost* best = nullptr; float bd = 1e9f;
        for (auto& gh : ghosts) { const float d = pos (gh).getDistanceFrom (e.position); if (d < bd) { bd = d; best = &gh; } }
        if (best == nullptr) return;
        if (e.getNumberOfClicks() >= 2 && bd < 40) { best->look = lookFor (juce::Random::getSystemRandom().nextInt (35)); best->glitch = 1.0f; }
        active = best; best->held = true;
        best->px->beginChangeGesture(); best->py->beginChangeGesture();
        if (bd > 40) move (*best, e.position);
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (active) move (*active, e.position); }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (! active) return;
        active->px->endChangeGesture(); active->py->endChangeGesture(); active->held = false; active = nullptr;
    }

    // GLASS (round 3): the same pad on a themed backdrop (the layout, sizes and motion are the original's)
    void paintThemed (juce::Graphics& g, const Backdrop& bd)
    {
        auto r = getLocalBounds().toFloat();
        auto inkOf = [&] (juce::Colour c) { return bd.ink ? bd.ink (c) : c; };
        auto labelOf = [&] (juce::Colour c) { return bd.label ? bd.label (c) : c; };
        g.setGradientFill (juce::ColourGradient (bd.top, 0, 0, bd.bottom, 0, r.getHeight(), false)); g.fillRoundedRectangle (r, 12);
        if (! bd.edge.isTransparent())
        {
            g.setGradientFill (juce::ColourGradient (bd.edge, 0, 0, bd.edge.withAlpha (0.0f), 0, 18, false));
            g.drawRoundedRectangle (r.reduced (1.0f), 11, 1.0f);
        }
        if (! bd.rim.isTransparent()) { g.setColour (bd.rim); g.drawRoundedRectangle (r.reduced (0.5f), 12, 1.0f); }
        g.setColour (bd.grid.withAlpha (bd.gridAlpha));
        for (int i = 0; i <= 10; ++i)
        {
            const float x = m + (float) i / 10.0f * (r.getWidth() - 2 * m), y = m + (float) i / 10.0f * (r.getHeight() - 2 * m);
            g.drawLine (x, m, x, r.getHeight() - m); g.drawLine (m, y, r.getWidth() - m, y);
        }
        for (auto& d : rain.drops)
        {
            const auto c = inkOf (d.c);
            juce::ColourGradient gr (c.withAlpha (0.0f), d.x, d.y - d.len, c.withAlpha (0.8f * bd.rainAlpha), d.x, d.y, false);
            g.setGradientFill (gr); g.fillRoundedRectangle (d.x - d.w * 0.5f, d.y - d.len, d.w, d.len, d.w * 0.5f);
            g.setColour (c.withAlpha (0.95f * bd.rainAlpha)); g.fillEllipse (d.x - d.w, d.y - d.w, d.w * 2, d.w * 2);
        }
        const float lvl = levelSource ? levelSource() : 0.0f;
        for (auto& gh : ghosts)
        {
            auto p = pos (gh);
            const auto c = inkOf (gh.colour);
            for (size_t k = 1; k < gh.trail.size(); ++k)
            {
                const float a = (float) k / (float) gh.trail.size();
                g.setColour (c.withAlpha (a * bd.trailAlpha)); g.drawLine ({ gh.trail[k - 1], gh.trail[k] }, a * 8.0f);
            }
            // custom ghost art (opt-in): it adapts to the backdrop colour under the ghost (light / dark)
            if (ghostPainter && ghostPainter (g, { gh, (int) (&gh - &ghosts[0]), bobbed (gh, p), 30.0f + (gh.held ? 10.0f : 0.0f) + lvl * 8.0f, gh.glitch, lvl, (float) phase, true, backdropAt (bd, p.y) }))
                continue;
            const float R = 26.0f + (gh.held ? 8.0f : 0.0f) + lvl * 12.0f;
            juce::ColourGradient glow (c.withAlpha (bd.glowAlpha), p.x, p.y, c.withAlpha (0.0f), p.x + R * 2.2f, p.y, true);
            g.setGradientFill (glow); g.fillEllipse (p.x - R * 2.2f, p.y - R * 2.2f, R * 4.4f, R * 4.4f);
        }
        for (auto& gh : ghosts)
        {
            auto p = pos (gh);
            const bool locked = gh.held && juce::Random::getSystemRandom().nextFloat() > 0.15f;
            const float cy = p.y + std::sin ((float) phase * 2.2f + (float) (&gh - &ghosts[0])) * 3.0f;
            const float size = 30.0f + (gh.held ? 10.0f : 0.0f) + lvl * 8.0f;
            if (! (ghostPainter && ghostPainter (g, { gh, (int) (&gh - &ghosts[0]), bobbed (gh, p), size, gh.glitch, lvl, (float) phase, false, backdropAt (bd, p.y) })))
            {
                if (! bd.ghostOutline.isTransparent()) paintGhostOutline (g, gh, p.x, cy, size, locked, bd.ghostOutline);
                drawGhost (g, gh.look, p.x, cy, size, gh.glitch, locked, 1.0f, gh.flip);
            }
            if (painterDrawsNames && ghostPainter) continue;
            g.setColour (labelOf (gh.colour)); g.setFont (font (10.0f));
            g.drawText (gh.name, juce::Rectangle<float> (p.x - 40, p.y < m + 56 ? p.y + 40 : p.y - 52, 80, 14), juce::Justification::centred);
        }
        if (readout.isNotEmpty())
        {
            g.setColour (labelOf (readoutColour)); g.setFont (font (11.0f));
            g.drawText (readout, r.reduced (10, 6).removeFromTop (16), juce::Justification::centredRight);
        }
    }
    // the backdrop colour at a height (the custom ghost art picks its light / dark look from it)
    juce::Colour backdropAt (const Backdrop& bd, float y) const { return bd.top.interpolatedWith (bd.bottom, juce::jlimit (0.0f, 1.0f, y / (float) juce::jmax (1, getHeight()))); }
    // the ghost's silhouette (body + drips, the same transform as drawGhost) stroked in the outline colour, under the ghost
    static void paintGhostOutline (juce::Graphics& g, const Ghost& gh, float cx, float cy, float size, bool locked, juce::Colour c)
    {
        const float s = size / 118.0f;
        auto t = juce::AffineTransform::translation (-130.0f, -106.0f).scaled (gh.flip ? -s : s, s).translated (cx, cy);   // = drawGhost's
        juce::Path p (locked ? snipeBody() : cuteBody()); p.addPath (locked ? snipeDrips() : cuteDrips()); p.applyTransform (t);
        g.setColour (c); g.strokePath (p, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    std::vector<juce::String> ghostNames() const { std::vector<juce::String> v; for (auto& gh : ghosts) v.push_back (gh.name); return v; }
    std::vector<juce::Colour> ghostColours() const { std::vector<juce::Colour> v; for (auto& gh : ghosts) v.push_back (gh.colour); return v; }
    std::vector<GhostLook> ghostLooks() const { std::vector<GhostLook> v; for (auto& gh : ghosts) v.push_back (gh.look); return v; }
    // test hooks
    void stepForTest() { timerCallback(); }
    int numGhosts() const { return (int) ghosts.size(); }
    juce::Point<float> gazeOf (int i) const { return ghosts[(size_t) i].gaze; }
    float dragGlitchOf (int i) const { return ghosts[(size_t) i].dragGlitch; }
    juce::Point<float> ghostPos (int i) const { return pos (ghosts[(size_t) i]); }

private:
    std::vector<Ghost> ghosts;
    Ghost* active = nullptr;
    NeonRain rain;
    double phase = 0;
    const float m = 30.0f;
    const juce::Colour padBg { 0xff06070a };
    juce::Point<float> bobbed (const Ghost& gh, juce::Point<float> p) const { return { p.x, p.y + std::sin ((float) phase * 2.2f + (float) (&gh - &ghosts[0])) * 3.0f }; }
    juce::String readout; juce::Colour readoutColour; int readoutTimer = 0;

    juce::Point<float> pos (const Ghost& gh) const
    {
        return { m + gh.px->getValue() * ((float) getWidth() - 2 * m), m + (1.0f - gh.py->getValue()) * ((float) getHeight() - 2 * m) };
    }
    void move (Ghost& gh, juce::Point<float> p)
    {
        auto old = pos (gh);
        gh.px->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, (p.x - m) / ((float) getWidth() - 2 * m)));
        gh.py->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, 1.0f - (p.y - m) / ((float) getHeight() - 2 * m)));
        auto now = pos (gh);
        gh.speed = juce::jmin (1.0f, gh.speed * 0.6f + now.getDistanceFrom (old) / 20.0f);
        if (std::abs (now.x - old.x) > 0.5f) gh.flip = now.x < old.x;
        readout = gh.px->getName (12) + " " + gh.px->getCurrentValueAsText() + " / " + gh.py->getName (12) + " " + gh.py->getCurrentValueAsText();
        readoutColour = gh.colour; readoutTimer = 60;
    }
    // the look direction (like the keyboard's GHOST CURSOR) and the drag glitch, from the ghost's motion on screen
    void updateGaze (Ghost& gh)
    {
        const auto p = pos (gh);
        if (! gh.hasLast || getWidth() != lastW || getHeight() != lastH) { gh.lastPos = p; gh.hasLast = true; gh.vel = {}; }
        const auto d = p - gh.lastPos; gh.lastPos = p;
        gh.vel = gh.vel * 0.55f + d * 0.45f;                                       // a short smoothed velocity (px per frame)
        const float sp = gh.vel.getDistanceFromOrigin();
        juce::Point<float> want;
        if (sp > (gh.held ? 2.0f : 0.35f))
        {
            want = gh.vel / sp * (juce::jmin (1.0f, sp / 5.0f) * (gh.held ? 1.0f : 0.55f));   // automation / MORPH / macros: gentler
            if (gh.held) gh.heldGaze = want;
        }
        else if (gh.held) want = gh.heldGaze;                                      // held (nearly) still: keeps looking the last way it went
        gh.gaze += (want - gh.gaze) * 0.25f;                                       // eases (back to rest within ~0.35 s)
        if (want.isOrigin() && gh.gaze.getDistanceFromOrigin() < 0.02f) gh.gaze = {};
        if (! gh.held) gh.heldGaze = {};
        // the drag glitch: follows the drag speed, decays ~0.3 s after the drag stops
        gh.dragGlitch = juce::jmax (gh.dragGlitch * 0.72f, gh.held ? juce::jmin (1.0f, sp / 12.0f) : 0.0f);   // full at ~480 px/s
        if (gh.dragGlitch < 0.02f) gh.dragGlitch = 0.0f;
    }
    int lastW = 0, lastH = 0;
    void timerCallback() override
    {
        phase += 1.0 / 40.0;
        bool anyHeld = false;
        for (auto& gh : ghosts)
        {
            gh.trail.push_back (pos (gh)); if (gh.trail.size() > 16) gh.trail.pop_front();
            gh.speed *= 0.9f;
            if (! gh.held && juce::Random::getSystemRandom().nextFloat() < 0.004f) gh.glitch = 0.8f;
            gh.glitch = juce::jmax (gh.glitch * 0.9f, gh.held ? 0.35f + gh.speed * 0.65f : 0.0f);
            anyHeld |= gh.held;
            updateGaze (gh);
        }
        lastW = getWidth(); lastH = getHeight();
        if (anyHeld) rain.spawn (getLocalBounds().toFloat().reduced (m, 0), 2);
        rain.step ((float) getHeight());
        if (readoutTimer > 0 && --readoutTimer == 0) readout.clear();
        repaint();
    }
};
} // namespace ghosty
