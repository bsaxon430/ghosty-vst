// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX: the DRAW engine's curve editor (WRECKER, CRACKLE, TUBE GLOW; DECISIONS.md 86). It shows the transfer curve (input left to
// right, output bottom to top, the straight line = clean) and its points: drag a point to bend the curve, double-click to add one,
// right-click a point to take it away (the two ends stay, at the left and right edges); RESET goes back to the soft start curve,
// MIRROR makes the left half the upside-down copy of the right (a symmetric curve: odd harmonics only). Every edit goes to the unit and
// into the project's state through PhantomProcessor::setUnitData, as the drawn rhythm editors do (undo, A/B and unit presets carry it).
#pragma once
#include "../plugin/PluginProcessor.h"
#include "../engines/CurveEngine.h"
#include "FxUi.h"

namespace phantom
{
class CurveDrawEditor : public juce::Component, public juce::SettableTooltipClient
{
public:
    CurveDrawEditor (PhantomProcessor& p, int unit, juce::Colour accent) : proc (p), u (unit), acc (accent)
    {
        for (auto* b : { &resetBtn, &mirrorBtn }) { addAndMakeVisible (*b); b->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff141726)); b->setColour (juce::TextButton::textColourOffId, acc); }
        resetBtn.setTooltip ("Back to the soft start curve"); mirrorBtn.setTooltip ("Make the left half an upside-down copy of the right (symmetric: odd harmonics only)");
        resetBtn.onClick = [this] { pts = engines::CurveEngine::factoryPoints(); commit(); };
        mirrorBtn.onClick = [this]
        {
            std::vector<juce::Point<float>> m; for (auto& q : pts) if (q.x > 0.0f) m.push_back (q);
            std::vector<juce::Point<float>> out; for (auto it = m.rbegin(); it != m.rend(); ++it) out.push_back ({ -it->x, -it->y });
            out.push_back ({ 0.0f, 0.0f }); for (auto& q : m) out.push_back (q); if (out.size() >= 2) { out.front().x = -1.0f; out.back().x = 1.0f; pts = out; commit(); }
        };
        pts = engines::CurveEngine::varToPoints (proc.getUnitData (u));
        setTooltip ("Draw the curve: drag a point, double-click to add one, right-click to remove one");
    }
    void resized() override
    {
        auto r = getLocalBounds(); auto bar = r.removeFromBottom (26); resetBtn.setBounds (bar.removeFromRight (80).reduced (2)); mirrorBtn.setBounds (bar.removeFromRight (80).reduced (2));
        plot = r.reduced (8).toFloat(); const float side = juce::jmin (plot.getWidth(), plot.getHeight()); plot = plot.withSizeKeepingCentre (side * 1.25f > plot.getWidth() ? plot.getWidth() : side * 1.25f, side);
    }
    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colour (0xe0090b12)); g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        for (int k = 1; k < 8; ++k) { const float x = plot.getX() + plot.getWidth() * k / 8.0f, y = plot.getY() + plot.getHeight() * k / 8.0f; g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom()); g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight()); }
        g.setColour (juce::Colours::white.withAlpha (0.25f)); g.drawRect (plot, 1.0f);
        g.drawLine (juce::Line<float> (toScreen ({ -1.0f, -1.0f }), toScreen ({ 1.0f, 1.0f })), 1.0f);   // clean
        juce::Path c; for (int i = 0; i <= 240; ++i) { const float x = -1.0f + 2.0f * i / 240.0f; const auto s = toScreen ({ x, juce::jlimit (-1.5f, 1.5f, engines::CurveEngine::evalCurve (pts, x)) }); if (i == 0) c.startNewSubPath (s); else c.lineTo (s); }
        g.saveState(); g.reduceClipRegion (plot.toNearestInt());
        g.setColour (acc.withAlpha (0.25f)); g.strokePath (c, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (acc); g.strokePath (c, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.restoreState();
        for (size_t i = 0; i < pts.size(); ++i) { const auto s = toScreen (pts[i]); g.setColour ((int) i == drag ? juce::Colours::white : acc); g.fillEllipse (s.x - 5.0f, s.y - 5.0f, 10.0f, 10.0f); g.setColour (juce::Colours::black.withAlpha (0.6f)); g.drawEllipse (s.x - 5.0f, s.y - 5.0f, 10.0f, 10.0f, 1.0f); }
        g.setColour (juce::Colours::white.withAlpha (0.55f)); g.setFont (juce::FontOptions (11.0f));
        g.drawText ("DRAW  -  drag a point, double-click to add, right-click to remove", getLocalBounds().removeFromBottom (26).reduced (10, 0), juce::Justification::centredLeft);
        g.drawText ("in", plot.withTop (plot.getBottom() - 14).translated (0, 13), juce::Justification::centredRight); g.drawText ("out", plot.withHeight (14).translated (4, 0), juce::Justification::topLeft);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        drag = hit (e.position);
        if (e.mods.isPopupMenu() && drag > 0 && drag < (int) pts.size() - 1) { pts.erase (pts.begin() + drag); drag = -1; commit(); return; }
        repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drag < 0) return; auto q = fromScreen (e.position);
        if (drag == 0) q.x = -1.0f; else if (drag == (int) pts.size() - 1) q.x = 1.0f;
        else q.x = juce::jlimit (pts[(size_t) drag - 1].x + 0.01f, pts[(size_t) drag + 1].x - 0.01f, q.x);   // points keep their order
        q.y = juce::jlimit (-1.5f, 1.5f, q.y); pts[(size_t) drag] = q; commit();
    }
    void mouseUp (const juce::MouseEvent&) override { drag = -1; repaint(); }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (hit (e.position) >= 0 || ! plot.contains (e.position)) return;
        auto q = fromScreen (e.position); q.x = juce::jlimit (-0.99f, 0.99f, q.x);
        pts.push_back (q); std::sort (pts.begin(), pts.end(), [] (auto& a, auto& b) { return a.x < b.x; }); commit();
    }
    std::function<void()> onEdit;
private:
    juce::Point<float> toScreen (juce::Point<float> v) const { return { plot.getX() + (v.x + 1.0f) * 0.5f * plot.getWidth(), plot.getBottom() - (v.y + 1.0f) / 2.0f * plot.getHeight() }; }
    juce::Point<float> fromScreen (juce::Point<float> s) const { return { (s.x - plot.getX()) / plot.getWidth() * 2.0f - 1.0f, (plot.getBottom() - s.y) / plot.getHeight() * 2.0f - 1.0f }; }
    int hit (juce::Point<float> s) const { for (size_t i = 0; i < pts.size(); ++i) if (toScreen (pts[i]).getDistanceFrom (s) < 9.0f) return (int) i; return -1; }
    void commit() { proc.setUnitData (u, engines::CurveEngine::pointsToVar (pts)); pts = engines::CurveEngine::varToPoints (proc.getUnitData (u)); repaint(); if (onEdit) onEdit(); }
    PhantomProcessor& proc; int u; juce::Colour acc; std::vector<juce::Point<float>> pts; int drag = -1; juce::Rectangle<float> plot;
    juce::TextButton resetBtn { "RESET" }, mirrorBtn { "MIRROR" };
};
} // namespace phantom
