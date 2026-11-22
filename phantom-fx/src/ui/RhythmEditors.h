// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX: the drawable editors of the Rhythm units, shown above their knobs on the unit page (included by PluginEditor.cpp).
//   GHOST GATE  pattern picker, drawn gate curve (GATE-12 point types, tension), 16-step time lane, drawn pump (DRAWN pump mode)
//   GHOST GRID  pattern picker, 12 painted rows x 16 / 32 steps
//   HAUNT EDIT  16 pads (hold = play; click a pad's name line to edit its type / size / motion)
//   WARP        the bend lane (snap, grid, factory shapes)
// Every edit goes to the unit (lock-free hand-over) and into the project's state (PhantomProcessor::setUnitData).
#pragma once
#include "BendLaneEditor.h"
#include "../rhythm/RhythmGate.h"
#include "../rhythm/RhythmGrid.h"
#include "../rhythm/RhythmHaunt.h"
#include "../rhythm/RhythmWarp.h"

namespace phantom
{
// a row of small pill buttons (pattern / brush pickers)
struct PillRow : public juce::Component
{
    juce::StringArray items; int selected = 0; juce::Colour accent; std::function<void (int)> onPick; std::function<void (int)> onRightClick;
    void paint (juce::Graphics& g) override
    {
        const float w = (float) getWidth() / juce::jmax (1, items.size());
        for (int i = 0; i < items.size(); ++i)
        {
            auto r = juce::Rectangle<float> (i * w, 0, w, (float) getHeight()).reduced (2, 1);
            g.setColour (i == selected ? accent.withAlpha (0.85f) : T (juce::Colour (0xff141722))); g.fillRoundedRectangle (r, 5);
            g.setColour (i == selected ? accent : col::white.withAlpha (0.12f)); g.drawRoundedRectangle (r, 5, 1);
            g.setColour (i == selected ? T (juce::Colour (0xff07080c)) : col::white.withAlpha (0.75f)); g.setFont (font (juce::jmin (12.0f, r.getHeight() * 0.5f)));
            g.drawFittedText (items[i], r.toNearestInt().reduced (2, 0), juce::Justification::centred, 1, 0.7f);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = juce::jlimit (0, items.size() - 1, (int) (e.position.x / ((float) getWidth() / juce::jmax (1, items.size()))));
        if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick (i); return; }
        selected = i; repaint(); if (onPick) onPick (i);
    }
};

// ================================================================ GHOST GATE
class GateCurve : public juce::Component
{
public:
    GateCurve (PhantomProcessor& p) : proc (p) {}
    GhostGateFx* fx() { return dynamic_cast<GhostGateFx*> (&proc.rack.slot (uGate).fx()); }
    int pat = 0; gatedata::Pattern data; std::array<float, gatedata::kTab + 1> tab {}; int hover = -1;
    void load (int p) { pat = p; if (auto* f = fx()) data = f->pattern (p); rebake(); repaint(); }
    void commit() { rebake(); if (auto* f = fx()) { f->setPattern (pat, data); proc.setUnitData (uGate, f->getData()); } repaint(); if (onEdit) onEdit(); }
    std::function<void()> onEdit;
    juce::Rectangle<float> area() const { return getLocalBounds().toFloat().reduced (10, 12); }
    juce::Point<float> toScreen (float x, float y) const { auto a = area(); return { a.getX() + x * a.getWidth(), a.getBottom() - y * a.getHeight() }; }
    juce::Point<float> fromScreen (juce::Point<float> p) const { auto a = area(); return { juce::jlimit (0.0f, 1.0f, (p.x - a.getX()) / a.getWidth()), juce::jlimit (0.0f, 1.0f, (a.getBottom() - p.y) / a.getHeight()) }; }
    float valueAt (float x) const { const float t = juce::jlimit (0.0f, 1.0f, x) * gatedata::kTab; const int i = juce::jmin (gatedata::kTab - 1, (int) t); return tab[(size_t) i] + (t - i) * (tab[(size_t) i + 1] - tab[(size_t) i]); }
    void paint (juce::Graphics& g) override
    {
        const auto acc = accentOf (uGate); drawScreen (g, getLocalBounds().toFloat(), acc); auto a = area();
        for (int s = 0; s < gatedata::kSteps; ++s)
            if (data.steps[(size_t) s] != rhythm::tmNormal)
            { g.setColour (T (juce::Colour (rhythm::timeModeColour (data.steps[(size_t) s]))).withAlpha (0.10f)); g.fillRect (a.getX() + a.getWidth() * s / gatedata::kSteps, a.getY(), a.getWidth() / gatedata::kSteps, a.getHeight()); }
        for (int s = 0; s <= 32; ++s) { g.setColour (col::white.withAlpha (s % 8 == 0 ? 0.16f : s % 2 == 0 ? 0.07f : 0.03f)); g.drawVerticalLine ((int) (a.getX() + a.getWidth() * s / 32.0f), a.getY(), a.getBottom()); }
        juce::Path curve, fill; const int N = juce::jmax (64, (int) a.getWidth());
        for (int i = 0; i <= N; ++i)
        {
            const float x = (float) i / N; const auto p = toScreen (x, valueAt (x));
            if (i == 0) { curve.startNewSubPath (p); fill.startNewSubPath (a.getX(), a.getBottom()); } else curve.lineTo (p);
            fill.lineTo (p);
        }
        fill.lineTo (a.getRight(), a.getBottom()); fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (acc.withAlpha (0.35f), 0, a.getY(), col::ecto.withAlpha (0.05f), 0, a.getBottom(), false)); g.fillPath (fill);
        glowStroke (g, curve, acc, 2.0f);
        for (size_t i = 0; i + 1 < data.pts.size(); ++i)
        {
            const float mx = 0.5f * (data.pts[i].x + data.pts[i + 1].x); const auto m = toScreen (mx, valueAt (mx));
            juce::Path d; d.addRectangle (-3.5f, -3.5f, 7.0f, 7.0f); d.applyTransform (juce::AffineTransform::rotation (0.785f).translated (m));
            g.setColour (col::ecto.withAlpha (0.8f)); g.strokePath (d, juce::PathStrokeType (1.2f));
        }
        for (size_t i = 0; i < data.pts.size(); ++i) { const auto p = toScreen (data.pts[i].x, data.pts[i].level); glowDot (g, p, (int) i == hover ? 5.5f : 4.0f, (int) i == hover ? col::white.get() : acc); }
        if (auto* f = fx()) if (f->uiPattern.load() == pat)
        {
            const float pos = f->uiPos.load();
            if (pos >= 0.0f) { const float x = a.getX() + pos * a.getWidth(); g.setColour (col::white.withAlpha (0.35f)); g.drawVerticalLine ((int) x, a.getY(), a.getBottom()); }
        }
        drawCaption (g, a.removeFromTop (14).reduced (4, 0), data.name, col::white.withAlpha (0.5f), 10, juce::Justification::centredRight);
        drawCaption (g, getLocalBounds().toFloat().reduced (14, 2).removeFromBottom (12), "click: add  -  drag: move  -  double-click: delete  -  drag a diamond: curve  -  right-click a point: segment shape", col::dim, 9.0f);
    }
    int hitPoint (juce::Point<float> p) const { for (size_t i = 0; i < data.pts.size(); ++i) if (toScreen (data.pts[i].x, data.pts[i].level).getDistanceFrom (p) < 8.0f) return (int) i; return -1; }
    int hitHandle (juce::Point<float> p) const { for (size_t i = 0; i + 1 < data.pts.size(); ++i) { const float mx = 0.5f * (data.pts[i].x + data.pts[i + 1].x); if (toScreen (mx, valueAt (mx)).getDistanceFrom (p) < 7.0f) return (int) i; } return -1; }
    float snapX (float x) const { return std::round (x * 32.0f) / 32.0f; }
    void mouseMove (const juce::MouseEvent& e) override { const int h = hitPoint (e.position); if (h != hover) { hover = h; repaint(); } }
    void mouseDown (const juce::MouseEvent& e) override
    {
        drag = hitPoint (e.position); tension = -1;
        if (e.mods.isPopupMenu() && drag >= 0)
        {
            juce::PopupMenu m; for (int t = 0; t <= 8; ++t) m.addItem (t + 1, gatedata::pointTypeName (t), true, data.pts[(size_t) drag].type == t);
            const int idx = drag; juce::Component::SafePointer<GateCurve> safe (this);
            m.showMenuAsync (juce::PopupMenu::Options(), [safe, idx] (int r) { if (safe == nullptr || r <= 0) return; safe->data.pts[(size_t) idx].type = r - 1; safe->commit(); });
            drag = -1; return;
        }
        if (e.getNumberOfClicks() >= 2 && drag > 0 && drag < (int) data.pts.size() - 1) { data.pts.erase (data.pts.begin() + drag); drag = -1; commit(); return; }
        if (drag >= 0) return;
        tension = hitHandle (e.position); if (tension >= 0) { startT = data.pts[(size_t) tension].tension; return; }
        auto q = fromScreen (e.position); q.x = e.mods.isAltDown() ? q.x : snapX (q.x);
        size_t i = 1; while (i < data.pts.size() && data.pts[i].x < q.x) ++i; if (i >= data.pts.size()) i = data.pts.size() - 1;
        data.pts.insert (data.pts.begin() + (long) i, { juce::jlimit (data.pts[i - 1].x + 0.001f, data.pts[i].x - 0.001f, q.x), q.y, 0, 1 });
        drag = (int) i; commit();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (tension >= 0) { data.pts[(size_t) tension].tension = juce::jlimit (-1.0f, 1.0f, startT + (float) e.getDistanceFromDragStartY() / 80.0f); commit(); return; }
        if (drag < 0) return;
        auto q = fromScreen (e.position); auto& pt = data.pts[(size_t) drag]; pt.level = q.y;
        if (drag > 0 && drag < (int) data.pts.size() - 1) pt.x = juce::jlimit (data.pts[(size_t) drag - 1].x + 0.001f, data.pts[(size_t) drag + 1].x - 0.001f, e.mods.isAltDown() ? q.x : snapX (q.x));
        commit();
    }
private:
    void rebake() { gatedata::bake (data, tab.data()); }
    PhantomProcessor& proc; int drag = -1, tension = -1; float startT = 0;
};

class GateTimeLane : public juce::Component
{
public:
    explicit GateTimeLane (GateCurve& c) : curve (c) {}
    void paint (juce::Graphics& g) override
    {
        const float w = (float) getWidth() / gatedata::kSteps;
        for (int s = 0; s < gatedata::kSteps; ++s)
        {
            auto r = juce::Rectangle<float> (s * w, 0, w, (float) getHeight()).reduced (1.5f); const int m = curve.data.steps[(size_t) s]; const auto c = T (juce::Colour (rhythm::timeModeColour (m)));
            g.setColour (m == rhythm::tmNormal ? T (juce::Colour (0xff141722)) : c.withAlpha (0.55f)); g.fillRoundedRectangle (r, 4);
            g.setColour (m == rhythm::tmNormal ? col::white.withAlpha (s % 4 == 0 ? 0.18f : 0.08f) : c); g.drawRoundedRectangle (r, 4, 1.0f);
            g.setColour (m == rhythm::tmNormal ? col::dim.withAlpha (0.5f) : col::white.get()); g.setFont (font (10.0f)); g.drawText (rhythm::timeModeName (m), r, juce::Justification::centred);
        }
    }
    // click: next time effect on that step; right-click: previous; drag: paint the clicked one along
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int s = stepAt (e.position.x); const int m = curve.data.steps[(size_t) s];
        paintMode = (m + (e.mods.isPopupMenu() ? rhythm::kNumTimeModes - 1 : 1)) % rhythm::kNumTimeModes; apply (s);
    }
    void mouseDrag (const juce::MouseEvent& e) override { apply (stepAt (e.position.x)); }
private:
    int stepAt (float x) const { return juce::jlimit (0, gatedata::kSteps - 1, (int) (x / ((float) getWidth() / gatedata::kSteps))); }
    void apply (int s) { if (curve.data.steps[(size_t) s] == paintMode) return; curve.data.steps[(size_t) s] = (uint8_t) paintMode; curve.commit(); repaint(); }
    GateCurve& curve; int paintMode = 0;
};

class GatePanel : public juce::Component, private juce::Timer
{
public:
    GatePanel (PhantomProcessor& p) : proc (p), curve (p), lane (curve), pump (pumpModel)
    {
        for (int i = 1; i <= gatedata::kPatterns; ++i) pats.items.add (juce::String (i));
        pats.accent = accentOf (uGate);
        pats.onPick = [this] (int i) { if (auto* rp = proc.apvts.getParameter (PhantomProcessor::pid (uGate, "pattern"))) rp->setValueNotifyingHost (rp->convertTo0to1 ((float) (i + 1))); curve.load (i); lane.repaint(); };
        curve.onEdit = [this] { lane.repaint(); };
        pumpModel.getter = [this] { auto* f = dynamic_cast<GhostGateFx*> (&proc.rack.slot (uGate).fx()); return f ? f->pump() : bendlane::pumpDefault(); };
        pumpModel.setter = [this] (const bendlane::Shape& s) { if (auto* f = dynamic_cast<GhostGateFx*> (&proc.rack.slot (uGate).fx())) { f->setPump (s); proc.setUnitData (uGate, f->getData()); } };
        pump.accent = accentOf (uGate); pump.rangeSemis = [] { return 1.0f; }; pump.valueText = [] (float v) { return juce::String (juce::roundToInt (v * 100)) + "%"; };
        pump.bars = [this] { static const double b[] { 0.125, 0.25, 0.5, 1.0 }; return b[juce::jlimit (0, 3, (int) proc.apvts.getRawParameterValue (PhantomProcessor::pid (uGate, "plen"))->load())]; };
        addAndMakeVisible (pats); addAndMakeVisible (curve); addAndMakeVisible (lane); addChildComponent (pump);
        sync(); startTimerHz (30);
    }
    void sync() { const int p = juce::jlimit (1, 16, (int) proc.apvts.getRawParameterValue (PhantomProcessor::pid (uGate, "pattern"))->load()) - 1; pats.selected = p; pats.repaint(); curve.load (p); lane.repaint(); }
    void resized() override
    {
        auto r = getLocalBounds(); pats.setBounds (r.removeFromTop (24)); r.removeFromTop (4);
        const bool drawn = (int) proc.apvts.getRawParameterValue (PhantomProcessor::pid (uGate, "pmode"))->load() == GhostGateFx::pmDrawn;
        pump.setVisible (drawn); if (drawn) { pump.setBounds (r.removeFromRight (r.getWidth() / 3)); r.removeFromRight (6); }
        lane.setBounds (r.removeFromBottom (26)); r.removeFromBottom (4); curve.setBounds (r);
    }
private:
    int seenGen = -1;
    void timerCallback() override
    {
        if (const int gen = proc.drawnGeneration.load(); gen != seenGen) { seenGen = gen; sync(); }   // a scene / A-B / preset replaced the drawn data: show it
        const int p = juce::jlimit (1, 16, (int) proc.apvts.getRawParameterValue (PhantomProcessor::pid (uGate, "pattern"))->load()) - 1;
        if (p != pats.selected) sync();
        const bool drawn = (int) proc.apvts.getRawParameterValue (PhantomProcessor::pid (uGate, "pmode"))->load() == GhostGateFx::pmDrawn;
        if (drawn != pump.isVisible()) resized();
        curve.repaint();
    }
    PhantomProcessor& proc; PillRow pats; GateCurve curve; GateTimeLane lane; bendlane::ShapeModel pumpModel; bendlane::Editor pump;
};

// ================================================================ GHOST GRID
class GridPanel : public juce::Component, private juce::Timer
{
public:
    GridPanel (PhantomProcessor& p) : proc (p)
    {
        for (int i = 0; i < griddata::kPatterns; ++i) pats.items.add (griddata::factory (i).name);
        pats.accent = accentOf (uGrid);
        pats.onPick = [this] (int i) { if (auto* rp = proc.apvts.getParameter (PhantomProcessor::pid (uGrid, "pattern"))) rp->setValueNotifyingHost (rp->convertTo0to1 ((float) (i + 1))); load (i); };
        pats.onRightClick = [this] (int i) { if (auto* f = fx()) { f->setPattern (i, griddata::factory (i)); proc.setUnitData (uGrid, f->getData()); load (pat); } };
        addAndMakeVisible (pats); load (current()); startTimerHz (30);
        // each row's knob sits at the end of its row; STEPS and SEED next to the patterns
        stepsPick.items = { "16 STEPS", "32 STEPS" }; stepsPick.accent = accentOf (uGrid);
        stepsPick.onPick = [this] (int i) { if (auto* rp = proc.apvts.getParameter (PhantomProcessor::pid (uGrid, "steps"))) rp->setValueNotifyingHost ((float) i); repaint(); };
        addAndMakeVisible (stepsPick);
        for (int r = 0; r < griddata::kRows; ++r) addRowSlider (griddata::row (r).key, T (juce::Colour (griddata::row (r).colour)));
        addRowSlider ("seed", accentOf (uGrid));
    }
    ~GridPanel() override { atts.clear(); }
    GhostGridFx* fx() { return dynamic_cast<GhostGridFx*> (&proc.rack.slot (uGrid).fx()); }
    int current() const { return juce::jlimit (1, 8, (int) proc.apvts.getRawParameterValue (PhantomProcessor::pid (uGrid, "pattern"))->load()) - 1; }
    int steps() const { return (int) proc.apvts.getRawParameterValue (PhantomProcessor::pid (uGrid, "steps"))->load() == 1 ? 32 : 16; }
    void load (int p) { pat = p; pats.selected = p; if (auto* f = fx()) data = f->pattern (p); repaint(); pats.repaint(); }
    void resized() override
    {
        auto r = getLocalBounds(); auto top = r.removeFromTop (26);
        auto seedR = top.removeFromRight (150); stepsPick.setBounds (top.removeFromRight (170)); top.removeFromRight (6); pats.setBounds (top);
        sliders.getLast()->setBounds (seedR.withTrimmedLeft (44).reduced (0, 4));
        r.removeFromTop (4); r.removeFromBottom (14); cells = r;
        for (int k = 0; k < griddata::kRows; ++k) { auto c = cellRect (k, 0); sliders[k]->setBounds (juce::Rectangle<int> (cells.getRight() - kKnobW + 46, (int) c.getY(), kKnobW - 50, (int) c.getHeight()).reduced (0, 1)); }
    }
    juce::Rectangle<float> cellRect (int row, int s) const
    {
        auto c = cells.toFloat(); const float labelW = 74.0f; c.removeFromLeft (labelW); c.removeFromRight ((float) kKnobW);
        const float w = c.getWidth() / steps(), h = c.getHeight() / griddata::kRows; return { c.getX() + s * w, c.getY() + row * h, w, h };
    }
    void paint (juce::Graphics& g) override
    {
        drawScreen (g, cells.toFloat().expanded (2), accentOf (uGrid), 6);
        const int n = steps(); float pos = -1.0f; if (auto* f = fx()) if (f->uiPattern.load() == pat) pos = f->uiPos.load();
        const int play = pos >= 0.0f ? juce::jlimit (0, n - 1, (int) (pos * n)) : -1;
        for (int row = 0; row < griddata::kRows; ++row)
        {
            const auto c = T (juce::Colour (griddata::row (row).colour)); auto lr = cellRect (row, 0); lr.setX ((float) cells.getX() + 2); lr.setWidth (70);
            g.setColour (c); g.setFont (font (10.0f)); g.drawText (griddata::row (row).name, lr, juce::Justification::centredLeft);
            for (int s = 0; s < n; ++s)
            {
                auto r = cellRect (row, s).reduced (1.5f); const bool on = ((data.rows[(size_t) row] >> s) & 1u) != 0, beat = s % (n / 4) == 0;
                if (on) { g.setGradientFill (juce::ColourGradient (c.brighter (s == play ? 0.6f : 0.1f), 0, r.getY(), c.darker (0.5f), 0, r.getBottom(), false)); g.fillRoundedRectangle (r, 3); }
                else { g.setColour (s == play ? T (juce::Colour (0xff262a3a)) : beat ? T (juce::Colour (0xff171a26)) : T (juce::Colour (0xff12141d))); g.fillRoundedRectangle (r, 3); }
            }
        }
        drawCaption (g, juce::Rectangle<float> ((float) cells.getX(), (float) getHeight() - 12, (float) cells.getWidth(), 12), "paint steps with the mouse  -  right-click a pattern name: back to its factory paint  -  each row's control is at its end", col::dim, 9.0f);
        g.setColour (col::dim.get()); g.setFont (font (9.5f)); g.drawText ("SEED", sliders.getLast()->getBounds().withX (sliders.getLast()->getX() - 40).withWidth (36), juce::Justification::centredRight);
        for (int k = 0; k < griddata::kRows; ++k) { auto b = sliders[k]->getBounds(); g.setColour (T (juce::Colour (griddata::row (k).colour)).withAlpha (0.8f)); g.setFont (font (9.0f)); g.drawText (griddata::row (k).knob, b.withX (b.getX() - 46).withWidth (44), juce::Justification::centredRight); }
    }
    void mouseDown (const juce::MouseEvent& e) override { int row, s; if (! hit (e.position, row, s)) return; paintValue = ! ((data.rows[(size_t) row] >> s) & 1u); apply (row, s); }
    void mouseDrag (const juce::MouseEvent& e) override { int row, s; if (hit (e.position, row, s)) apply (row, s); }
private:
    bool hit (juce::Point<float> p, int& row, int& s) const
    { for (row = 0; row < griddata::kRows; ++row) for (s = 0; s < steps(); ++s) if (cellRect (row, s).contains (p)) return true; return false; }
    void apply (int row, int s)
    {
        const uint32_t bit = 1u << s; auto& m = data.rows[(size_t) row]; const uint32_t nm = paintValue ? (m | bit) : (m & ~bit);
        if (nm == m) return; m = nm;
        if (auto* f = fx()) { f->setPattern (pat, data); proc.setUnitData (uGrid, f->getData()); }
        repaint();
    }
    int seenGen = -1;
    void timerCallback() override
    {
        if (const int gen = proc.drawnGeneration.load(); gen != seenGen) { seenGen = gen; load (pat); }   // a scene / A-B / preset replaced the drawn data: show it
        if (current() != pat) load (current());
        const int st = steps() == 32 ? 1 : 0; if (st != stepsPick.selected) { stepsPick.selected = st; stepsPick.repaint(); repaint(); }
        repaint (cells);
    }
    void addRowSlider (const char* key, juce::Colour c)
    {
        auto* sl = sliders.add (new juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::NoTextBox));
        sl->setColour (juce::Slider::trackColourId, c.withAlpha (0.6f)); sl->setColour (juce::Slider::backgroundColourId, T (juce::Colour (0xff12141d)));
        sl->setPopupDisplayEnabled (true, true, nullptr);
        addAndMakeVisible (sl); atts.add (new juce::AudioProcessorValueTreeState::SliderAttachment (proc.apvts, PhantomProcessor::pid (uGrid, key), *sl));
    }
    static constexpr int kKnobW = 150;
    PillRow stepsPick; juce::OwnedArray<juce::Slider> sliders; juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> atts;
    PhantomProcessor& proc; PillRow pats; griddata::Pattern data; int pat = 0; bool paintValue = true; juce::Rectangle<int> cells;
};

// ================================================================ HAUNT EDIT
class HauntPanel : public juce::Component, private juce::Timer
{
public:
    HauntPanel (PhantomProcessor& p) : proc (p)
    {
        type.addItemList (hauntdata::typeNames(), 1); size.addItemList (hauntdata::sizeNames(), 1);
        type.onChange = [this] { edit ([this] (hauntdata::Pad& d) { d.type = type.getSelectedId() - 1; }); };
        size.onChange = [this] { edit ([this] (hauntdata::Pad& d) { d.size = size.getSelectedId() - 1; }); };
        motion.setSliderStyle (juce::Slider::LinearHorizontal); motion.setRange (0.0, 1.0); motion.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        motion.onValueChange = [this] { edit ([this] (hauntdata::Pad& d) { d.motion = (float) motion.getValue(); }); };
        for (auto* c : std::initializer_list<juce::Component*> { &type, &size, &motion }) addAndMakeVisible (c);
        select (0); startTimerHz (30);
    }
    HauntEditFx* fx() { return dynamic_cast<HauntEditFx*> (&proc.rack.slot (uHaunt).fx()); }
    void select (int i) { selected = i; if (auto* f = fx()) { f->uiSel.store (i, std::memory_order_relaxed); const auto d = f->pad (i); loading = true; type.setSelectedId (d.type + 1, juce::dontSendNotification); size.setSelectedId (d.size + 1, juce::dontSendNotification); motion.setValue (d.motion, juce::dontSendNotification); loading = false; } repaint(); }
    void resized() override
    {
        auto r = getLocalBounds(); auto side = r.removeFromRight (190); r.removeFromRight (8); pads = r;
        side.removeFromTop (18); type.setBounds (side.removeFromTop (28)); side.removeFromTop (22); size.setBounds (side.removeFromTop (28)); side.removeFromTop (22); motion.setBounds (side.removeFromTop (24));
        info = side.withTrimmedTop (8);
    }
    juce::Rectangle<float> padRect (int i) const { const float w = pads.getWidth() / 4.0f, h = pads.getHeight() / 4.0f; return { pads.getX() + (i % 4) * w, pads.getY() + (i / 4) * h, w, h }; }
    void paint (juce::Graphics& g) override
    {
        auto* f = fx(); const int held = f ? f->uiHeld.load() : -1; const float env = f ? f->uiEnv.load() : 0.0f, prog = f ? f->uiProgress.load() : 0.0f;
        const bool fol = following();
        if (fol) g.beginTransparencyLayer (0.32f);   // FOLLOW GHOST BREAK: the pads rest (greyed, as every control that does nothing)
        for (int i = 0; i < hauntdata::kPads; ++i)
        {
            auto r = padRect (i).reduced (4); const auto d = f ? f->pad (i) : hauntdata::factory (i); const auto c = T (juce::Colour (hauntdata::typeColour (d.type))); const bool down = held == i;
            if (down) { g.setColour (c.withAlpha (0.25f + 0.25f * env)); g.fillRoundedRectangle (r.expanded (3), 12); }
            g.setGradientFill (juce::ColourGradient (down ? c.withAlpha (0.9f) : c.withAlpha (0.18f), 0, r.getY(), down ? c.darker (0.6f) : T (juce::Colour (0xff12101c)), 0, r.getBottom(), false)); g.fillRoundedRectangle (r, 10);
            g.setColour (c.withAlpha (i == selected ? 1.0f : 0.5f)); g.drawRoundedRectangle (r, 10, i == selected ? 2.0f : 1.0f);
            if (down) { juce::Path arc; arc.addCentredArc (r.getRight() - 14, r.getY() + 14, 7, 7, 0, 0, 6.2832f * prog, true); g.setColour (col::white.get()); g.strokePath (arc, juce::PathStrokeType (2.0f)); }
            g.setColour (down ? col::white.get() : c.brighter (0.4f)); g.setFont (font (juce::jmin (14.0f, r.getHeight() * 0.22f))); g.drawText (hauntdata::typeNames()[d.type], r.withTrimmedBottom (r.getHeight() * 0.3f), juce::Justification::centred);
            g.setColour (col::dim.get()); g.setFont (font (10.0f)); g.drawText (hauntdata::sizeNames()[d.size], r.withTrimmedTop (r.getHeight() * 0.55f), juce::Justification::centred);
            g.setColour (col::white.withAlpha (0.25f)); g.setFont (font (9.0f)); g.drawText (juce::String (i + 1), r.reduced (7, 5), juce::Justification::topLeft);
        }
        if (fol) g.endTransparencyLayer();
        const auto acc = accentOf (uHaunt);
        if (fol)
        {   // what is being followed, over the resting pads
            const float sh = f ? f->uiShift.load() : -1.0f; const int bay = (int) std::lround (proc.apvts.getRawParameterValue (PhantomProcessor::pid (uHaunt, "fbay"))->load());
            const juce::String bayName = juce::String ("BAY ") + juce::String::charToString ((juce::juce_wchar) ('A' + juce::jlimit (0, 3, bay)));
            const juce::String line = sh < 0.0f ? "FOLLOWING GHOST BREAK " + bayName + "\nno Ghost Break playing in this project yet"
                                                : "FOLLOWING GHOST BREAK " + bayName + "\nNUDGE moves the sound " + juce::String (sh, 3) + " beats earlier";
            g.setColour (acc); g.setFont (font (15.0f)); g.drawFittedText (line, pads.reduced (12), juce::Justification::centred, 3);
        }
        g.setColour (acc); g.setFont (font (10.0f));
        g.drawText ("PAD " + juce::String (selected + 1) + " TYPE", type.getBounds().translated (0, -16).withHeight (14), juce::Justification::centredLeft);
        g.drawText ("SIZE", size.getBounds().translated (0, -16).withHeight (14), juce::Justification::centredLeft);
        g.drawText ("MOTION", motion.getBounds().translated (0, -16).withHeight (14), juce::Justification::centredLeft);
        g.setColour (col::dim.get()); g.setFont (font (9.5f));
        g.drawFittedText ("hold a pad to play it, and slide across the pads to move between them (LATCH: click on / off). MIDI notes 48-63 play pads 1-16. Right-click: edit a pad without playing it.", info, juce::Justification::topLeft, 6, 0.9f);
    }
    int padAt (juce::Point<float> p) const { for (int i = 0; i < hauntdata::kPads; ++i) if (padRect (i).contains (p)) return i; return -1; }
    bool following() const { auto* a = proc.apvts.getRawParameterValue (PhantomProcessor::pid (uHaunt, "follow")); return a != nullptr && a->load() >= 0.5f; }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (following()) return;   // the pads rest while FOLLOW GHOST BREAK is on
        const int i = padAt (e.position); if (i < 0) return; select (i);
        if (e.mods.isPopupMenu()) return;
        if (auto* f = fx())
        {
            const bool latch = proc.apvts.getRawParameterValue (PhantomProcessor::pid (uHaunt, "latch"))->load() >= 0.5f;
            if (latch) f->uiPad.store (f->uiPad.load() == i ? -1 : i); else f->uiPad.store (i);
        }
    }
    // sliding a held click across the pads moves the stutter from pad to pad, like rolling over a pad controller (Brandon 2026-11-12):
    // the pad under the mouse plays (LATCH: becomes the latched one); off the pads, the last one keeps playing until the button is let go
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() || following()) return;
        const int i = padAt (e.position); if (i < 0 || i == selected) return;
        select (i); if (auto* f = fx()) f->uiPad.store (i);
    }
    void mouseUp (const juce::MouseEvent&) override
    { const bool latch = proc.apvts.getRawParameterValue (PhantomProcessor::pid (uHaunt, "latch"))->load() >= 0.5f; if (! latch) if (auto* f = fx()) f->uiPad.store (-1); }
private:
    template <typename Fn> void edit (Fn fn) { if (loading) return; if (auto* f = fx()) { auto d = f->pad (selected); fn (d); f->setPad (selected, d); proc.setUnitData (uHaunt, f->getData()); } repaint(); }
    void timerCallback() override
    {
        const bool fol = following();
        for (auto* c : std::initializer_list<juce::Component*> { &type, &size, &motion }) if ((c->getAlpha() < 0.99f) != fol) c->setAlpha (fol ? 0.32f : 1.0f);
        if (fol != wasFol) { wasFol = fol; repaint(); } else repaint (pads);
    }
    bool wasFol = false;
    PhantomProcessor& proc; juce::ComboBox type, size; juce::Slider motion; int selected = 0; bool loading = false; juce::Rectangle<int> pads, info;
};

// ================================================================ WARP
class WarpPanel : public juce::Component
{
public:
    WarpPanel (PhantomProcessor& p) : proc (p), lane (model)
    {
        model.getter = [this] { auto* f = fx(); return f ? f->lane() : bendlane::defaultShape(); };
        model.setter = [this] (const bendlane::Shape& s) { if (auto* f = fx()) { f->setLane (s); proc.setUnitData (uWarp, f->getData()); } };
        lane.accent = accentOf (uWarp);
        lane.rangeSemis = [this] { return bendlane::rangeOf ((int) proc.apvts.getRawParameterValue (PhantomProcessor::pid (uWarp, "range"))->load()); };
        lane.bars = [this] { return bendlane::barsOf ((int) proc.apvts.getRawParameterValue (PhantomProcessor::pid (uWarp, "length"))->load()); };
        lane.amount = [this] { return proc.apvts.getRawParameterValue (PhantomProcessor::pid (uWarp, "amount"))->load(); };
        lane.playPos = [this] { auto* f = fx(); return f ? f->uiPos.load() : -1.0f; };
        snap.items = bendlane::snapNames(); grid.items = bendlane::gridNames(); snap.accent = grid.accent = shapes.accent = accentOf (uWarp);
        for (auto& f : bendlane::factory()) shapes.items.add (juce::String (f.name).toUpperCase());
        snap.selected = model.get().snap; grid.selected = model.get().grid;
        snap.onPick = [this] (int i) { lane.setSnap (i); }; grid.onPick = [this] (int i) { lane.setGrid (i); };
        shapes.onPick = [this] (int i)
        {
            const auto& fct = bendlane::factory()[(size_t) i]; auto s = fct.shape; s.snap = model.get().snap; s.grid = model.get().grid; model.set (s); lane.repaint();
            for (auto [id, v] : { std::pair<const char*, int> { "length", fct.lenIndex }, { "range", fct.rangeIndex } })
                if (auto* rp = proc.apvts.getParameter (PhantomProcessor::pid (uWarp, id))) rp->setValueNotifyingHost (rp->convertTo0to1 ((float) v));
        };
        for (auto* c : std::initializer_list<juce::Component*> { &lane, &snap, &grid, &shapes }) addAndMakeVisible (c);
    }
    WarpFx* fx() { return dynamic_cast<WarpFx*> (&proc.rack.slot (uWarp).fx()); }
    void resized() override
    {
        auto r = getLocalBounds(); auto top = r.removeFromTop (24); snap.setBounds (top.removeFromLeft (top.getWidth() / 2).reduced (0, 0)); top.removeFromLeft (6); grid.setBounds (top);
        r.removeFromTop (4); shapes.setBounds (r.removeFromBottom (24)); r.removeFromBottom (4); lane.setBounds (r);
    }
private:
    PhantomProcessor& proc; bendlane::ShapeModel model; bendlane::Editor lane; PillRow snap, grid, shapes;
};

inline std::unique_ptr<juce::Component> makeRhythmPanel (PhantomProcessor& p, int unit)
{
    switch (unit)
    {
        case uGate: return std::make_unique<GatePanel> (p);
        case uGrid: return std::make_unique<GridPanel> (p);
        case uHaunt: return std::make_unique<HauntPanel> (p);
        case uWarp: return std::make_unique<WarpPanel> (p);
        default: return {};
    }
}
} // namespace phantom
