// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX: ported from Ghost Box's BendLaneEditor.h (GhostBlock's own code).
#pragma once
// Bend lane curve editor (shared by Ghosty Pro's BEND page and Ghost Box's WARP page).
// Click empty space = add a point, drag a point = move it, right-click / double-click a point = delete it.
// Each segment has a small diamond in its middle: drag it up/down = curve tension, double-click it = smooth <-> step,
// right-click a segment = menu. Hold ALT (or CMD) while dragging to move freely without the snap / grid.
#include <juce_gui_basics/juce_gui_basics.h>
#include "../rhythm/BendLane.h"
#include "GhostArt.h"
#include "FxTheme.h"

namespace phantom::bendlane {
// Phantom FX: the editor reads and writes its curve through these two functions (WARP's lane, GHOST GATE's drawn pump)
struct ShapeModel
{
    std::function<Shape()> getter; std::function<void (const Shape&)> setter; uint32_t ver = 2;
    Shape get() const { return getter ? getter() : defaultShape(); }
    void set (Shape s) { s.sanitise(); if (setter) setter (s); ver += 2; }
    uint32_t version() const noexcept { return ver; }
};

class Editor : public juce::Component, private juce::Timer
{
public:
    explicit Editor (ShapeModel& s) : model (s) { edit = model.get(); seenVer = model.version(); startTimerHz (30); }
    ~Editor() override { stopTimer(); }

    std::function<float()> rangeSemis = [] { return 12.0f; };   // current range (y = +-1 means +-range)
    std::function<double()> bars = [] { return 1.0; };          // lane length
    std::function<float()> playPos;                               // 0..1 lane position, < 0 = hide
    std::function<float()> amount = [] { return 1.0f; };        // AMOUNT knob (draws the effective curve faintly)
    std::function<void()> onEditStart, onEdited;                  // undo hook / anything that wants to know
    std::function<juce::String (float)> valueText;                // optional: label y (-1..1) yourself instead of semitones
    juce::Colour accent { ghosty::col::slime };
    int ghostLook = 7;

    void setSnap (int s) { edit = model.get(); edit.snap = juce::jlimit (0, 3, s); commit(); }
    void setGrid (int g) { edit = model.get(); edit.grid = juce::jlimit (0, 3, g); commit(); }
    int snap() const { return edit.snap; }
    int grid() const { return edit.grid; }
    void load (const Shape& s) { if (onEditStart) onEditStart(); edit = s; commit(); }
    const Shape& shape() const { return edit; }

    void paint (juce::Graphics& g) override
    {
        using ghosty::font; using ghosty::lookFor; using ghosty::drawGhost; namespace col = fxtheme::col; using fxtheme::T;
        const juce::Colour acc = T (accent);   // the stored acc, themed
        auto b = getLocalBounds().toFloat();
        juce::ColourGradient bg (T (juce::Colour (0xff07080c)), 0, b.getY(), T (juce::Colour (0xff0d0f17)), 0, b.getBottom(), false);
        g.setGradientFill (bg); g.fillRoundedRectangle (b, 10);
        g.setColour (acc.withAlpha (0.14f)); g.drawRoundedRectangle (b.reduced (0.5f), 10, 1.0f);
        const auto a = area();
        const float rng = rangeSemis();
        // vertical grid: grid lines, beats, bars
        const double nb = bars(); const int perBeat = gridPerBeat (edit.grid);
        const int beats = juce::jmax (2, (int) std::lround (nb * 4.0));
        int cells = beats * perBeat; int stepEvery = 1;
        while (cells / stepEvery > 128) stepEvery *= 2;
        for (int k = 0; k <= cells; k += stepEvery)
        {
            const float x = a.getX() + a.getWidth() * (float) k / (float) cells;
            const bool bar = (k % (perBeat * 4)) == 0, beat = (k % perBeat) == 0;
            g.setColour (col::white.withAlpha (bar ? 0.20f : beat ? 0.09f : 0.035f));
            g.fillRect (x - 0.5f, a.getY(), bar ? 1.4f : 1.0f, a.getHeight());
            if (bar && k < cells)
            {
                g.setColour (col::dim); g.setFont (font (9.0f));
                g.drawText (juce::String (k / (perBeat * 4) + 1), juce::Rectangle<float> (x + 3, a.getBottom() + 1, 30, 12), juce::Justification::centredLeft);
            }
        }
        if (nb < 1.0) { g.setColour (col::dim); g.setFont (font (9.0f)); g.drawText ("1/2 BAR", juce::Rectangle<float> (a.getX() + 3, a.getBottom() + 1, 60, 12), juce::Justification::centredLeft); }
        // horizontal: semitone landmarks
        std::vector<float> marks;
        if (valueText) marks = { -rng, -0.5f * rng, 0, 0.5f * rng, rng };
        else if (rng <= 2.0f) marks = { -2, -1, 0, 1, 2 };
        else if (rng <= 7.0f) marks = { -7, -5, 0, 5, 7 };
        else if (rng <= 12.0f) marks = { -12, -7, 0, 7, 12 };
        else marks = { -24, -12, -7, 0, 7, 12, 24 };
        for (float m : marks)
        {
            const float y = yOf (m / rng);
            const bool zero = m == 0, oct = std::fmod (std::abs (m), 12.0f) == 0;
            g.setColour (col::white.withAlpha (zero ? 0.22f : oct ? 0.12f : 0.06f));
            g.fillRect (a.getX(), y - 0.5f, a.getWidth(), zero ? 1.4f : 1.0f);
            g.setColour (zero ? col::white.withAlpha (0.7f) : col::dim); g.setFont (font (9.0f));
            g.drawText (valueText ? valueText (m / rng) : (m > 0 ? "+" : "") + juce::String ((int) m), juce::Rectangle<float> (b.getX() + 2, y - 6, a.getX() - b.getX() - 6, 12), juce::Justification::centredRight);
        }
        // the curve (effective = times AMOUNT, faint) and the drawn one
        const float amt = juce::jlimit (0.0f, 1.0f, amount());
        auto path = curvePath (1.0f), eff = curvePath (amt);
        {
            juce::Path fill (path); fill.lineTo (a.getRight(), yOf (0)); fill.lineTo (a.getX(), yOf (0)); fill.closeSubPath();
            juce::ColourGradient fg (acc.withAlpha (0.20f), 0, a.getY(), acc.withAlpha (0.04f), 0, yOf (0), false);
            g.setGradientFill (fg); g.fillPath (fill);
        }
        if (amt < 0.995f) { g.setColour (col::white.withAlpha (0.25f)); g.strokePath (eff, juce::PathStrokeType (1.2f)); }
        g.setColour (acc.withAlpha (0.10f)); g.strokePath (path, juce::PathStrokeType (8.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (acc.withAlpha (0.22f)); g.strokePath (path, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (acc); g.strokePath (path, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        // segment handles
        if (isMouseOverOrDragging() || dragMode == DragMode::tension)
            for (int i = 0; i < edit.n; ++i)
            {
                const auto h = handlePos (i);
                const bool hot = i == hoverHandle || (dragMode == DragMode::tension && i == dragIndex);
                juce::Path d; d.addRectangle (-3.5f, -3.5f, 7.0f, 7.0f); d.applyTransform (juce::AffineTransform::rotation (0.785f).translated (h));
                g.setColour (edit.p[(size_t) i].step ? col::gold.withAlpha (hot ? 1.0f : 0.6f) : col::white.withAlpha (hot ? 0.9f : 0.35f));
                g.fillPath (d);
            }
        // points
        for (int i = 0; i < edit.n; ++i)
        {
            const auto q = pointPos (i);
            const bool hot = i == hoverPoint || (dragMode == DragMode::point && i == dragIndex);
            g.setColour (acc.withAlpha (0.25f)); g.fillEllipse (q.x - 8, q.y - 8, 16, 16);
            g.setColour (hot ? col::white : acc.brighter (0.3f)); g.fillEllipse (q.x - 4.5f, q.y - 4.5f, 9, 9);
            g.setColour (T (juce::Colour (0xff07080c))); g.fillEllipse (q.x - 1.8f, q.y - 1.8f, 3.6f, 3.6f);
        }
        // value read-out while dragging a point
        if (dragMode == DragMode::point && juce::isPositiveAndBelow (dragIndex, edit.n))
        {
            const auto q = pointPos (dragIndex);
            const float st = edit.p[(size_t) dragIndex].y * rng;
            const juce::String txt = valueText ? valueText (edit.p[(size_t) dragIndex].y) : (st > 0.05f ? "+" : "") + juce::String (st, std::abs (st - std::round (st)) < 0.05f ? 0 : 1) + " st";
            g.setColour (col::white); g.setFont (font (11.0f));
            g.drawText (txt, juce::Rectangle<float> (q.x - 40, q.y - 24, 80, 14), juce::Justification::centred);
        }
        // playhead + a little ghost riding the curve
        const float pp = playPos ? playPos() : -1.0f;
        if (pp >= 0.0f)
        {
            const float x = a.getX() + a.getWidth() * juce::jlimit (0.0f, 1.0f, pp);
            const float v = edit.valueAt (pp) * amt;
            g.setColour (col::white.withAlpha (0.35f)); g.fillRect (x - 0.75f, a.getY(), 1.5f, a.getHeight());
            const float y = yOf (v);
            juce::ColourGradient gl (acc.withAlpha (0.6f), x, y, acc.withAlpha (0.0f), x + 14, y, true);
            g.setGradientFill (gl); g.fillEllipse (x - 14, y - 14, 28, 28);
            auto L = lookFor (ghostLook); L.slime = acc;
            drawGhost (g, L, x, y - 16, 22, 0.0f, false, 0.95f);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int hp = hitPoint (e.position), hh = hp < 0 ? hitHandle (e.position) : -1;
        if (hp != hoverPoint || hh != hoverHandle) { hoverPoint = hp; hoverHandle = hh; repaint(); }
        setMouseCursor (hp >= 0 ? juce::MouseCursor::DraggingHandCursor : hh >= 0 ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::CrosshairCursor);
    }
    void mouseExit (const juce::MouseEvent&) override { hoverPoint = hoverHandle = -1; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        edit = model.get();
        dragMode = DragMode::none; dragIndex = -1;
        const int hp = hitPoint (e.position), hh = hp < 0 ? hitHandle (e.position) : -1;
        if (e.mods.isPopupMenu())
        {
            if (hp >= 0) { removePoint (hp); return; }
            const int seg = hh >= 0 ? hh : edit.segmentAt (phaseOf (e.position.x));
            showSegmentMenu (seg);
            return;
        }
        if (hp >= 0)
        {
            if (e.getNumberOfClicks() >= 2) { removePoint (hp); return; }
            begin(); dragMode = DragMode::point; dragIndex = hp; return;
        }
        if (hh >= 0)
        {
            if (e.getNumberOfClicks() >= 2) { begin(); edit.p[(size_t) hh].step = ! edit.p[(size_t) hh].step; commit(); return; }
            begin(); dragMode = DragMode::tension; dragIndex = hh; tensionStart = edit.p[(size_t) hh].tension; return;
        }
        if (! area().contains (e.position) || edit.n >= kMaxPoints) return;
        begin();
        float x = phaseOf (e.position.x), y = valOf (e.position.y);
        if (! freeMove (e)) { x = snapX (x); y = snapY (y); }
        for (int i = 0; i < edit.n; ++i)   // a point already sits on this grid line: move that one instead of stacking a second
            if (std::abs (edit.p[(size_t) i].x - x) < 1.0e-6f) { edit.p[(size_t) i].y = y; dragMode = DragMode::point; dragIndex = i; commit(); return; }
        const int seg = edit.segmentAt (x);
        const bool step = seg >= 0 && edit.p[(size_t) seg].step;
        edit.add (x, y, 0.0f, step);
        edit.sanitise();
        dragIndex = -1;
        for (int i = 0; i < edit.n; ++i) if (std::abs (edit.p[(size_t) i].x - x) < 1.0e-6f && std::abs (edit.p[(size_t) i].y - y) < 1.0e-6f) dragIndex = i;
        dragMode = DragMode::point;
        commit();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragMode == DragMode::point && juce::isPositiveAndBelow (dragIndex, edit.n))
        {
            float x = phaseOf (e.position.x), y = valOf (e.position.y);
            if (! freeMove (e)) { x = snapX (x); y = snapY (y); }
            const float lo = dragIndex > 0 ? edit.p[(size_t) dragIndex - 1].x + 0.002f : 0.0f;
            const float hi = dragIndex + 1 < edit.n ? edit.p[(size_t) dragIndex + 1].x - 0.002f : 0.9999f;
            auto& q = edit.p[(size_t) dragIndex];
            q.x = juce::jlimit (lo, juce::jmax (lo, hi), x); q.y = juce::jlimit (-1.0f, 1.0f, y);
            commit();
        }
        else if (dragMode == DragMode::tension && juce::isPositiveAndBelow (dragIndex, edit.n))
        {
            edit.p[(size_t) dragIndex].tension = juce::jlimit (-1.0f, 1.0f, tensionStart - (float) e.getDistanceFromDragStartY() / 90.0f);
            if (edit.p[(size_t) dragIndex].step) edit.p[(size_t) dragIndex].step = false;
            commit();
        }
    }
    void mouseUp (const juce::MouseEvent&) override { dragMode = DragMode::none; dragIndex = -1; repaint(); }

private:
    ShapeModel& model;
    Shape edit;
    uint32_t seenVer = 0;
    enum class DragMode { none, point, tension } dragMode = DragMode::none;
    int dragIndex = -1, hoverPoint = -1, hoverHandle = -1; float tensionStart = 0;
    float lastPos = -2;

public:
    juce::Rectangle<float> laneArea() const { return area(); }   // (where the lane is drawn: WARP's painted DAWN picture lies under it)
private:
    // the value gutter grows when the lane labels its own values ("+100%", "+2.0 vowel") instead of semitones
    float gutter() const { return valueText ? 58.0f : 36.0f; }
    juce::Rectangle<float> area() const { auto b = getLocalBounds().toFloat(); return { b.getX() + gutter(), b.getY() + 30, b.getWidth() - gutter() - 10, b.getHeight() - 48 }; }
    float yOf (float v) const { const auto a = area(); return a.getCentreY() - juce::jlimit (-1.0f, 1.0f, v) * a.getHeight() * 0.5f; }
    float xOf (float ph) const { const auto a = area(); return a.getX() + ph * a.getWidth(); }
    float phaseOf (float px) const { const auto a = area(); return juce::jlimit (0.0f, 0.9999f, (px - a.getX()) / a.getWidth()); }
    float valOf (float py) const { const auto a = area(); return juce::jlimit (-1.0f, 1.0f, (a.getCentreY() - py) / (a.getHeight() * 0.5f)); }
    juce::Point<float> pointPosOf (float x, float y) const { return { xOf (x), yOf (y) }; }
    juce::Point<float> pointPos (int i) const { return pointPosOf (edit.p[(size_t) i].x, edit.p[(size_t) i].y); }
    static bool freeMove (const juce::MouseEvent& e) { return e.mods.isAltDown() || e.mods.isCommandDown(); }
    float snapX (float x) const
    {
        const double cells = std::max (1.0, bars() * 4.0 * gridPerBeat (edit.grid));
        return juce::jlimit (0.0f, 0.9999f, (float) (std::round (x * cells) / cells));
    }
    float snapY (float y) const { const float r = rangeSemis(); return juce::jlimit (-1.0f, 1.0f, snapSemis (y * r, edit.snap) / r); }
    juce::Point<float> handlePos (int i) const
    {
        const auto& a = edit.p[(size_t) i];
        const double bx = i + 1 < edit.n ? edit.p[(size_t) i + 1].x : edit.p[0].x + 1.0;
        double mid = 0.5 * (a.x + bx); if (mid >= 1.0) mid -= 1.0;
        return { xOf ((float) mid), yOf (edit.valueAt (mid)) };
    }
    int hitPoint (juce::Point<float> p) const
    {
        int best = -1; float bd = 9.0f;
        for (int i = 0; i < edit.n; ++i) { const float d = pointPos (i).getDistanceFrom (p); if (d < bd) { bd = d; best = i; } }
        return best;
    }
    int hitHandle (juce::Point<float> p) const
    {
        if (edit.n < 2) return -1;
        for (int i = 0; i < edit.n; ++i) if (handlePos (i).getDistanceFrom (p) < 7.0f) return i;
        return -1;
    }
    juce::Path curvePath (float scale) const
    {
        juce::Path p; const auto a = area();
        constexpr int N = 480;
        for (int k = 0; k <= N; ++k)
        {
            const double ph = k == N ? 0.99999 : (double) k / N;
            const float x = a.getX() + a.getWidth() * (float) k / N, y = yOf (edit.valueAt (ph) * scale);
            if (k == 0) p.startNewSubPath (x, y);
            else
            {
                // steps: draw the jump as a vertical edge
                const double prev = (double) (k - 1) / N;
                const int sPrev = edit.segmentAt (prev), sNow = edit.segmentAt (ph);
                if (sPrev != sNow && sPrev >= 0 && edit.p[(size_t) sPrev].step)
                {
                    const float jx = xOf (edit.p[(size_t) sNow].x);
                    p.lineTo (jx, yOf (edit.p[(size_t) sPrev].y * scale)); p.lineTo (jx, yOf (edit.valueAt (edit.p[(size_t) sNow].x) * scale));
                }
                p.lineTo (x, y);
            }
        }
        return p;
    }
    void begin() { if (onEditStart) onEditStart(); }
    void commit() { model.set (edit); edit = model.get(); seenVer = model.version(); if (onEdited) onEdited(); repaint(); }
    void removePoint (int i)
    {
        if (edit.n <= 1 || ! juce::isPositiveAndBelow (i, edit.n)) return;
        begin();
        for (int k = i; k < edit.n - 1; ++k) edit.p[(size_t) k] = edit.p[(size_t) k + 1];
        --edit.n; hoverPoint = -1; commit();
    }
    void showSegmentMenu (int seg)
    {
        if (! juce::isPositiveAndBelow (seg, edit.n)) return;
        juce::PopupMenu m;
        m.addSectionHeader ("SEGMENT");
        m.addItem (1, "Smooth (curve)", true, ! edit.p[(size_t) seg].step);
        m.addItem (2, "Step (jump)", true, edit.p[(size_t) seg].step);
        m.addItem (3, "Straight line (reset tension)");
        m.addSeparator();
        m.addItem (4, "Flatten all to 0");
        juce::Component::SafePointer<Editor> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe, seg] (int r)
        {
            if (safe == nullptr || r == 0) return;
            auto& ed = *safe; ed.edit = ed.model.get(); if (! juce::isPositiveAndBelow (seg, ed.edit.n)) return;
            ed.begin();
            auto& q = ed.edit.p[(size_t) seg];
            if (r == 1) q.step = false; else if (r == 2) q.step = true; else if (r == 3) { q.step = false; q.tension = 0; }
            else if (r == 4) { const int sn = ed.edit.snap, gr = ed.edit.grid; ed.edit = {}; ed.edit.snap = sn; ed.edit.grid = gr; ed.edit.add (0, 0); }
            ed.commit();
        });
    }
    void timerCallback() override
    {
        if (model.version() != seenVer && dragMode == DragMode::none) { edit = model.get(); seenVer = model.version(); repaint(); }
        const float pp = playPos ? playPos() : -1.0f;
        if (std::abs (pp - lastPos) > 1.0e-4f) { lastPos = pp; repaint(); }
    }
};

} // namespace phantom::bendlane
