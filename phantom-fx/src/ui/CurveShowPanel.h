// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX: SHOW CURVE (DECISIONS.md 90). On WRECKER, CRACKLE and TUBE GLOW with a CURVES set picked, a floating panel shows that
// set's curves in the DRAW editor's art: every curve of the set faint, the one DRIVE has reached bright (DRIVE 10 .. 100 % walks
// through them, blending neighbours; below 10 % it fades in from the straight line = clean), with where DRIVE stands. It floats over
// the whole plugin window, inside it (so it can never fall behind the plugin): drag its title bar to move it, the corner to resize it,
// the X to close it. A view only: it changes nothing and is not saved.
#pragma once
#include "../plugin/PluginProcessor.h"
#include "../engines/CurveEngine.h"

namespace phantom
{
class CurveShowPanel : public juce::Component, private juce::Timer
{
public:
    static constexpr int kTitle = 26;
    CurveShowPanel (PhantomProcessor& p, int unit, int set, juce::Colour accent) : proc (p), u (unit), acc (accent)
    {
        setName ("SHOW CURVE"); setSet (set);
        driveParam = proc.apvts.getRawParameterValue (PhantomProcessor::pid (u, u == uCrackle ? "b1_drive" : "drive"));
        closeBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff141726)); closeBtn.setColour (juce::TextButton::textColourOffId, acc);
        closeBtn.setTooltip ("Close the curve view"); closeBtn.onClick = [this] { open = false; setVisible (false); if (onClose) onClose(); };
        addAndMakeVisible (closeBtn);
        limits.setMinimumSize (240, 200); limits.setMinimumOnscreenAmounts (0xffffff, 0xffffff, 0xffffff, 0xffffff);   // wholly inside the plugin window
        grip = std::make_unique<juce::ResizableCornerComponent> (this, &limits); addAndMakeVisible (*grip);
        startTimerHz (20);
    }
    std::function<void()> onClose;
    // the page that opened it: while that page is not on screen (another tab, another effect) the panel hides, and comes back with it
    void followPage (juce::Component* page) { owner = page; }
    bool isOpen() const noexcept { return open; }
    void setSet (int set)
    {
        const auto& lib = engines::curveLibrary();
        cs = set >= 0 && set < (int) lib.size() ? &lib[(size_t) set] : nullptr; repaint();
    }
    // place it over the plugin window: on first opening, centred in the right part of it
    void placeIn (juce::Component& top, juce::Rectangle<int> remembered)
    {
        if (getParentComponent() != &top) top.addChildComponent (*this);
        auto r = remembered.isEmpty() ? juce::Rectangle<int> (0, 0, juce::jmin (420, top.getWidth() - 40), juce::jmin (380, top.getHeight() - 40)).withCentre ({ top.getWidth() * 2 / 3, top.getHeight() / 2 }) : remembered;
        limits.setSizeLimits (240, 200, juce::jmax (240, top.getWidth()), juce::jmax (200, top.getHeight()));
        limits.setBoundsForComponent (this, r, false, false, false, false);
        open = true; setVisible (ownerUp()); toFront (false);
    }
    void close() { open = false; setVisible (false); }
    void resized() override
    {
        auto r = getLocalBounds(); auto title = r.removeFromTop (kTitle); closeBtn.setBounds (title.removeFromRight (kTitle).reduced (3));
        grip->setBounds (getWidth() - 16, getHeight() - 16, 16, 16);
        auto body = r.reduced (10).withTrimmedBottom (18).toFloat(); const float side = juce::jmin (body.getWidth(), body.getHeight());
        plot = body.withSizeKeepingCentre (juce::jmin (body.getWidth(), side * 1.25f), side);
    }
    void paint (juce::Graphics& g) override
    {
        const auto all = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff0b0d16)); g.fillRoundedRectangle (all, 8.0f);
        g.setColour (acc.withAlpha (0.55f)); g.drawRoundedRectangle (all.reduced (0.5f), 8.0f, 1.2f);
        g.setColour (acc.withAlpha (0.12f)); g.fillRoundedRectangle (all.withHeight ((float) kTitle), 8.0f);
        g.setColour (juce::Colours::white.withAlpha (0.85f)); g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        g.drawText ("CURVE  -  " + juce::String (cs != nullptr ? cs->name : std::string ("(none)")), getLocalBounds().removeFromTop (kTitle).reduced (10, 0).withTrimmedRight (kTitle),
                    juce::Justification::centredLeft, true);
        // the DRAW editor's grid, frame and the straight line (clean)
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        for (int k = 1; k < 8; ++k) { const float x = plot.getX() + plot.getWidth() * k / 8.0f, y = plot.getY() + plot.getHeight() * k / 8.0f; g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom()); g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight()); }
        g.setColour (juce::Colours::white.withAlpha (0.25f)); g.drawRect (plot, 1.0f);
        g.drawLine (juce::Line<float> (toScreen (-1.0f, -1.0f), toScreen (1.0f, 1.0f)), 1.0f);
        if (cs == nullptr || cs->lanes < 1) return;
        g.saveState(); g.reduceClipRegion (plot.toNearestInt());
        for (int l = 0; l < cs->lanes; ++l) { g.setColour (acc.withAlpha (0.16f)); g.strokePath (pathOf ([&] (float x) { return at (l, x); }), juce::PathStrokeType (1.2f)); }
        const float d = drive; const double pos = juce::jlimit (0.0, 1.0, (d - 0.1) / 0.9) * (cs->lanes - 1); const int a = juce::jmin ((int) pos, cs->lanes - 1), c = juce::jmin (a + 1, cs->lanes - 1); const float fr = (float) (pos - a);
        const float w = juce::jlimit (0.0f, 1.0f, d / 0.1f);
        auto now = pathOf ([&] (float x) { const float y = at (a, x) + fr * (at (c, x) - at (a, x)); return x * (1.0f - w) + y * w; });
        g.setColour (acc.withAlpha (0.25f)); g.strokePath (now, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (acc); g.strokePath (now, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.restoreState();
        g.setColour (juce::Colours::white.withAlpha (0.6f)); g.setFont (juce::FontOptions (11.0f));
        const juce::String where = d < 0.1f ? "fading in from clean" : cs->lanes == 1 ? "the set's one curve" : "curve " + juce::String (pos + 1.0, 1) + " of " + juce::String (cs->lanes);
        g.drawText ("DRIVE " + juce::String (juce::roundToInt (d * 100.0f)) + " %  -  " + where, juce::Rectangle<float> (plot.getX(), plot.getBottom() + 2.0f, plot.getWidth(), 16.0f), juce::Justification::centredLeft);
        g.drawText ("in", plot.withTop (plot.getBottom() - 14).translated (0, 13), juce::Justification::centredRight); g.drawText ("out", plot.withHeight (14).translated (4, 0), juce::Justification::topLeft);
    }
    void mouseDown (const juce::MouseEvent& e) override { if (e.y < kTitle) dragger.startDraggingComponent (this, e); toFront (false); }
    void mouseDrag (const juce::MouseEvent& e) override { if (e.mouseDownPosition.y < kTitle) dragger.dragComponent (this, e, &limits); }
    float shownDrive() const noexcept { return drive; }
private:
    void timerCallback() override
    {
        if (open) { const bool want = ownerUp(); if (want != isVisible()) { setVisible (want); if (want) toFront (false); } }
        const float d = driveParam != nullptr ? driveParam->load() : 0.3f; if (std::abs (d - drive) > 0.0005f) { drive = d; repaint(); }
    }
    // the page and every component above it shown (isShowing() also asks for a window on screen, which an offscreen render has not)
    bool ownerUp() const { for (auto* c = owner.getComponent(); c != nullptr && c != getParentComponent(); c = c->getParentComponent()) if (! c->isVisible()) return false; return true; }   // (up to the window it floats in)
    float at (int lane, float x) const noexcept
    {   // the stored curve (input -1..+1 over its points), straight from the set
        const auto& c = cs->curve[(size_t) lane]; const float p = juce::jlimit (0.0f, (float) (cs->points - 1), (x + 1.0f) * 0.5f * (float) (cs->points - 1));
        const int k = juce::jmin ((int) p, cs->points - 2); return juce::jlimit (-1.5f, 1.5f, c[(size_t) k] + (c[(size_t) k + 1] - c[(size_t) k]) * (p - (float) k));
    }
    template <typename F> juce::Path pathOf (F&& f) const
    {
        juce::Path p; const int n = juce::jlimit (120, 600, (int) plot.getWidth());
        for (int i = 0; i <= n; ++i) { const float x = -1.0f + 2.0f * i / (float) n; const auto s = toScreen (x, f (x)); if (i == 0) p.startNewSubPath (s); else p.lineTo (s); }
        return p;
    }
    juce::Point<float> toScreen (float x, float y) const noexcept { return { plot.getX() + (x + 1.0f) * 0.5f * plot.getWidth(), plot.getCentreY() - y * 0.5f * plot.getHeight() / 1.0f }; }
    PhantomProcessor& proc; int u; juce::Colour acc; const engines::CurveSet* cs = nullptr; std::atomic<float>* driveParam = nullptr; float drive = -1.0f;
    juce::Rectangle<float> plot; juce::TextButton closeBtn { "X" }; juce::ComponentBoundsConstrainer limits; std::unique_ptr<juce::ResizableCornerComponent> grip; juce::ComponentDragger dragger;
    juce::Component::SafePointer<juce::Component> owner; bool open = false;
};
} // namespace phantom
