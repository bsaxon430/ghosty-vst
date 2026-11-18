// Ghost Voice - AGPL-3.0-only (see LICENSE, NOTICE.md).
//
// LIQUID TOGGLE: a pill switch whose single droplet travels on a spring and stretches along its path (scaleX up, scaleY = 1 /
// scaleX, so its area stays). Re-created natively from the Ghost Voice liquid toggle (a React adaptation of Bencho's liquid
// toggle, MIT, https://bencho.dev/licence; credited in NOTICE.md): the same 92 x 46 pill / 36 px droplet proportions, its
// spring (stiffness 170 - (50 - speed) * 1.1, damping 21.5, mass 0.9), its velocity -> stretch rule (1 + min(0.4, |v| / 600)
// * stretch), a slight swell on hover, drag with the pointer, a press without moving = a click, Space / Return toggle.
// No framer-motion, no web view: a JUCE timer integrates the spring.
#pragma once
#include "SplashArt.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <functional>

namespace gv
{
class LiquidToggle : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit LiquidToggle (juce::String labelText = {}) : label (std::move (labelText))
    {
        setWantsKeyboardFocus (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTitle (label);
    }
    std::function<void (bool)> onChange;
    juce::String label;
    juce::Colour on { 0xff9159ed }, off { 0xff272a40 }, ink { 0xfff5f1ff }, text { 0xfff5f1ff }, edge { 0x3dc1a0ff };
    float stretch = 36.0f, speed = 50.0f;
    static constexpr float kTrackW = 46.0f, kTrackH = 23.0f, kThumb = 18.0f;   // the 92 x 46 / 36 pill at half size

    bool getToggleState() const { return state; }
    void setToggleState (bool s, bool notify)
    {
        if (s == state) return;
        state = s;
        if (! isShowing()) snap();                       // not on screen: no animation to watch
        else if (! held) startTimerHz (120);
        if (notify && onChange) onChange (state);
        repaint();
    }
    void snap() { x = target(); v = 0; repaint(); }   // no animation (state loaded)
    float thumbX() const { return x; }

    void paint (juce::Graphics& g) override
    {
        const auto tr = track();
        const float mix = juce::jlimit (0.0f, 1.0f, (x - shutX()) / (openX() - shutX()));
        // HAND-DRAWN (build 10): the track inked by hand, painted as it turns on
        const auto seed = (juce::int64) (getX() * 31 + getY() * 7) + label.hashCode();
        const auto& box = gv::splash::handBox (tr, tr.getHeight() * 0.5f, seed, 0.7f);
        g.setColour (off.interpolatedWith (on, mix).withMultipliedAlpha (isEnabled() ? 1.0f : 0.4f)); g.fillPath (box);
        if (mix > 0.05f) gv::splash::hatch (g, box, seed, gv::splash::art().onHatch.withAlpha (0.2f * mix), 4.0f);
        if (isEnabled()) gv::splash::inkLine (g, box, seed + 1, text, 1.1f, 0.6f);
        else { g.setColour (edge); g.strokePath (box, juce::PathStrokeType (0.9f)); }
        // the droplet: stretched along its travel, squashed across it
        const float l = 1.0f + juce::jmin (0.4f, std::abs (vEased) / 600.0f) * juce::jlimit (0.0f, 100.0f, stretch) / 100.0f;
        const float sw = swell;
        const float w = kThumb * l * sw, h = kThumb * sw / l;
        const float cx = tr.getX() + x + kThumb * 0.5f, cy = tr.getCentreY();
        g.setColour (juce::Colours::black.withAlpha (0.25f)); g.fillEllipse (cx - w * 0.5f, cy - h * 0.5f + 1.5f, w, h);
        g.setColour (ink.withMultipliedAlpha (isEnabled() ? 1.0f : 0.6f)); g.fillEllipse (cx - w * 0.5f, cy - h * 0.5f, w, h);
        if (isEnabled()) { juce::Path dot; dot.addEllipse (cx - w * 0.5f, cy - h * 0.5f, w, h); g.setColour (text.withAlpha (0.75f)); g.strokePath (gv::splash::wobble (dot, 77, 0.5f, 2.5f), juce::PathStrokeType (1.0f)); }
        if (hasKeyboardFocus (false)) { g.setColour (ink.withAlpha (0.7f)); g.drawRoundedRectangle (tr.expanded (3.0f), tr.getHeight() * 0.5f + 3.0f, 1.5f); }
        if (label.isNotEmpty())
        {
            g.setColour (text.withMultipliedAlpha (isEnabled() ? 1.0f : 0.5f)); g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText (label, getLocalBounds().toFloat().withTrimmedLeft (kTrackW + 12.0f), juce::Justification::centredLeft, true);
        }
    }

    void mouseEnter (const juce::MouseEvent&) override { hot = true; startTimerHz (120); }
    void mouseExit (const juce::MouseEvent&) override { hot = false; startTimerHz (120); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! isEnabled() || e.mods.isPopupMenu()) return;
        held = true; moved = false; grab = std::nan (""); downX = e.position.x;
        if (isShowing()) grabKeyboardFocus();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! held) return;
        const float at = e.position.x - track().getX();
        if (std::isnan (grab)) grab = at - x;            // the grab offset is set at the first MOVE: a click never jumps
        if (std::abs (e.position.x - downX) > 3.0f) moved = true;
        const float nx = juce::jlimit (shutX(), openX(), at - grab);
        v = (nx - x) * 60.0f; x = nx;
        if (moved) { const bool s = x > midX(); if (s != state) { state = s; if (onChange) onChange (state); } }
        startTimerHz (120); repaint();
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (! held) return;
        held = false;
        if (! moved) setToggleState (! state, true);    // a press without moving is a click
        else setToggleState (x > midX(), true);
        startTimerHz (120);
    }
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::spaceKey || k == juce::KeyPress::returnKey) { if (isEnabled()) setToggleState (! state, true); return true; }
        return false;
    }
    void resized() override { if (! isTimerRunning()) snap(); }

private:
    juce::Rectangle<float> track() const { return { 2.0f, (float) getHeight() * 0.5f - kTrackH * 0.5f, kTrackW, kTrackH }; }
    float pad() const { return (kTrackH - kThumb) * 0.5f; }
    float shutX() const { return pad(); }
    float openX() const { return kTrackW - kThumb - pad(); }
    float midX() const { return 0.5f * (shutX() + openX()); }
    float target() const { return state ? openX() : shutX(); }
    void timerCallback() override
    {
        const float dt = 1.0f / 120.0f;
        if (! held)
        {   // the settle spring (track units are half the original's, so its forces are too: the motion looks the same)
            const float k = 170.0f - (50.0f - juce::jlimit (0.0f, 100.0f, speed)) * 1.1f, c = 21.5f, m = 0.9f;
            const float a = (-k * (x - target()) - c * v) / m;
            v += a * dt; x += v * dt;
        }
        // the eased velocity and the hover swell (stiffness 380, damping 34, mass 0.6)
        auto spring = [dt] (float& p, float& pv, float to) { const float a = (-380.0f * (p - to) - 34.0f * pv) / 0.6f; pv += a * dt; p += pv * dt; };
        spring (vEased, vEasedV, v * 2.0f);   // x2: back to the original's pixel scale for the stretch rule
        spring (swell, swellV, hot && isEnabled() ? 1.035f : 1.0f);
        repaint();
        if (! held && std::abs (x - target()) < 0.01f && std::abs (v) < 0.05f && std::abs (vEased) < 0.5f && std::abs (swell - (hot ? 1.035f : 1.0f)) < 0.001f)
        { x = target(); v = 0; stopTimer(); }
    }
    bool state = false, held = false, moved = false, hot = false;
    float x = 2.5f, v = 0, vEased = 0, vEasedV = 0, swell = 1, swellV = 0, grab = 0, downX = 0;
};
}
