#include "RepeatStrip.h"

namespace gslice::ui
{
static const int kRates[] { 4, 8, 16, 32, 64, 128 };

// ---------------------------------------------------------------- ratchet curve
juce::Point<float> RatchetCurve::pointPos (int i) const
{
    const auto r = getLocalBounds().toFloat().reduced (8.0f, 5.0f);
    return { r.getX() + r.getWidth() * (float) i / 4.0f, r.getCentreY() - r.getHeight() * 0.5f * pts[(size_t) i] / 2.0f };
}
void RatchetCurve::paint (juce::Graphics& g)
{
    auto R = getLocalBounds().toFloat();
    g.setColour (P.waveBg); g.fillRoundedRectangle (R, 5.0f);
    g.setColour (P.line); g.drawRoundedRectangle (R.reduced (0.5f), 5.0f, 1.0f);
    g.setColour (P.grid); g.fillRect (R.getX() + 4, R.getCentreY(), R.getWidth() - 8, 1.0f);
    juce::Path p; for (int i = 0; i < 5; ++i) { const auto q = pointPos (i); if (i == 0) p.startNewSubPath (q); else p.lineTo (q); }
    g.setColour (active ? P.marker : P.dim); g.strokePath (p, juce::PathStrokeType (1.8f));
    for (int i = 0; i < 5; ++i) { const auto q = pointPos (i); g.fillEllipse (q.x - 3.5f, q.y - 3.5f, 7.0f, 7.0f); }
}
void RatchetCurve::mouseDown (const juce::MouseEvent& e)
{
    dragging = -1; float best = 1e9f;
    for (int i = 0; i < 5; ++i) { const float d = std::abs (pointPos (i).x - e.position.x); if (d < best) { best = d; dragging = i; } }
    if (onDragStart) onDragStart();
    mouseDrag (e);
}
void RatchetCurve::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0) return;
    const auto r = getLocalBounds().toFloat().reduced (8.0f, 5.0f);
    const float v = juce::jlimit (-2.0f, 2.0f, (r.getCentreY() - e.position.y) / (r.getHeight() * 0.5f) * 2.0f);
    const float snapped = std::round (v * 4.0f) / 4.0f;
    if (snapped == pts[(size_t) dragging]) return;
    pts[(size_t) dragging] = snapped; repaint();
    if (onChange) onChange();
}
void RatchetCurve::mouseUp (const juce::MouseEvent&) { dragging = -1; if (onDragEnd) onDragEnd(); }
void RatchetCurve::mouseDoubleClick (const juce::MouseEvent&) { pts.fill (0.0f); repaint(); if (onChange) onChange(); }

// ---------------------------------------------------------------- the strip
RepeatStrip::RepeatStrip (SliceProcessor& p) : proc (p)
{
    auto add = [this] (juce::Component& c, const juce::String& id, const juce::String& tip)
    {
        c.setComponentID (id); addAndMakeVisible (c);
        if (auto* s = dynamic_cast<juce::SettableTooltipClient*> (&c); s != nullptr && tip.isNotEmpty()) s->setTooltip (tip);
    };
    add (rateBox, "repeat.rate", "RATE: the repeat period (follows the DAW's tempo when SYNC: DAW plays, else the project's BPM)");
    add (modBox, "repeat.mod", "Straight, triplet (2/3) or dotted (3/2)");
    add (revBox, "repeat.reverse", "REVERSE every 2nd repetition, or all of them");
    add (retrigBtn, "repeat.retrig", "RETRIG on: every repetition restarts at the span's start (stutter). Off: the audio runs on, the repeats gate it");
    add (rampModeBtn, "repeat.pitchmode", "PITCH per repetition: STEP (n x PITCH) or RAMP (0 .. PITCH over the span)");
    add (ratchetBtn, "repeat.ratchet", "GHOST RATCHET: the drawn curve sets the rate over the span (overrides RAMP)");
    add (removeBtn, "repeat.remove", "Remove this REPEAT (the audio under it comes back)");
    add (closeBtn, "repeat.close", "Close the REPEAT strip");
    add (curve, "repeat.curve", "GHOST RATCHET curve: drag the points (up = faster, down = slower), double-click = flat");
    for (int i = 0; i < 6; ++i) rateBox.addItem ("1/" + juce::String (kRates[i]), i + 1);
    modBox.addItemList ({ "STRAIGHT", "TRIPLET", "DOTTED" }, 1);
    revBox.addItemList ({ "REV OFF", "REV ALT", "REV ALL" }, 1);
    rateBox.onChange = [this] { if (! refreshing) change ([this] (RepeatOp& r) { r.rateDiv = kRates[juce::jlimit (0, 5, rateBox.getSelectedId() - 1)]; }, "Repeat rate"); };
    modBox.onChange = [this] { if (! refreshing) change ([this] (RepeatOp& r) { r.rateMod = modBox.getSelectedId() - 1; }, "Repeat rate"); };
    revBox.onChange = [this] { if (! refreshing) change ([this] (RepeatOp& r) { r.reverse = revBox.getSelectedId() - 1; }, "Repeat reverse"); };
    retrigBtn.onClick = [this] { change ([] (RepeatOp& r) { r.retrigger = ! r.retrigger; }, "Repeat retrigger"); };
    rampModeBtn.onClick = [this] { change ([] (RepeatOp& r) { r.pitchRamp = ! r.pitchRamp; }, "Repeat pitch mode"); };
    ratchetBtn.onClick = [this] { change ([] (RepeatOp& r) { r.ratchetOn = ! r.ratchetOn; }, "Ratchet"); };
    removeBtn.onClick = [this]
    {
        if (auto q = ops::removeRepeat (proc.project(), rid)) proc.apply (*q, "Remove repeat");
        rid = 0; if (onClose) onClose();
    };
    closeBtn.onClick = [this] { rid = 0; if (onClose) onClose(); };
    curve.onDragStart = [this] { proc.beginGesture(); }; curve.onDragEnd = [this] { proc.endGesture(); };
    curve.onChange = [this] { change ([this] (RepeatOp& r) { r.ratchet = curve.pts; r.ratchetOn = true; }, "Ratchet curve"); };
    for (auto* f : fields())
    {
        addAndMakeVisible (*f); f->capW = 62;
        f->s.onDragStart = [this] { proc.beginGesture(); }; f->s.onDragEnd = [this] { proc.endGesture(); };
        f->s.onValueChange = [this, f]
        {
            if (refreshing) return;
            const double v = f->s.getValue();
            change ([this, f, v] (RepeatOp& r)
            {
                if (f == &pitchF) r.pitchSemis = v; else if (f == &fadeInF) r.fadeInMs = v; else if (f == &fadeOutF) r.fadeOutMs = v;
                else if (f == &swingF) r.swing = v / 100.0; else if (f == &rampF) r.ramp = v; else if (f == &decayF) r.decay = v / 100.0; else r.gate = v / 100.0;
            }, "Repeat " + f->cap.getText().toLowerCase());
        };
    }
    rampF.s.setSkewFactorFromMidPoint (1.0);
    setComponentID ("repeatstrip");
}

std::vector<juce::Component*> RepeatStrip::controls()
{
    std::vector<juce::Component*> v { &rateBox, &modBox, &revBox, &retrigBtn, &rampModeBtn, &ratchetBtn, &removeBtn, &closeBtn, &curve };
    for (auto* f : fields()) v.push_back (f);
    return v;
}

void RepeatStrip::setPalette (const Palette& p) { P = p; curve.P = p; for (auto* f : fields()) f->cap.setColour (juce::Label::textColourId, P.dim); repaint(); }
void RepeatStrip::show (int64 id) { rid = id; refresh(); }

void RepeatStrip::change (const std::function<void (RepeatOp&)>& f, const juce::String& label)
{
    const auto* r = proc.project().findRepeat (rid); if (r == nullptr) return;
    RepeatOp x = *r; f (x);
    if (auto q = ops::setRepeat (proc.project(), x)) proc.apply (*q, label);
    refresh();
}

void RepeatStrip::refresh()
{
    const auto* r = proc.project().findRepeat (rid);
    if (r == nullptr) return;
    refreshing = true;
    for (int i = 0; i < 6; ++i) if (kRates[i] == r->rateDiv) rateBox.setSelectedId (i + 1, juce::dontSendNotification);
    modBox.setSelectedId (r->rateMod + 1, juce::dontSendNotification); revBox.setSelectedId (r->reverse + 1, juce::dontSendNotification);
    retrigBtn.setToggleState (r->retrigger, juce::dontSendNotification);
    rampModeBtn.setButtonText (r->pitchRamp ? "RAMP" : "STEP"); rampModeBtn.setToggleState (r->pitchRamp, juce::dontSendNotification);
    ratchetBtn.setToggleState (r->ratchetOn, juce::dontSendNotification);
    pitchF.s.setValue (r->pitchSemis, juce::dontSendNotification); fadeInF.s.setValue (r->fadeInMs, juce::dontSendNotification); fadeOutF.s.setValue (r->fadeOutMs, juce::dontSendNotification);
    swingF.s.setValue (r->swing * 100.0, juce::dontSendNotification); rampF.s.setValue (r->ramp, juce::dontSendNotification);
    decayF.s.setValue (r->decay * 100.0, juce::dontSendNotification); gateF.s.setValue (r->gate * 100.0, juce::dontSendNotification);
    rampF.setEnabled (! r->ratchetOn);
    curve.pts = r->ratchet; curve.active = r->ratchetOn; curve.repaint();
    refreshing = false;
    repaint();
}

void RepeatStrip::resized()
{
    auto r = getLocalBounds().reduced (10, 6);
    auto row1 = r.removeFromTop (30); r.removeFromTop (6); auto row2 = r.removeFromTop (30);
    titleArea = row1.removeFromLeft (150); row1.removeFromLeft (6);
    auto place = [] (juce::Rectangle<int>& row, juce::Component& c, int w, int gap = 6) { c.setBounds (row.removeFromLeft (w)); row.removeFromLeft (gap); };
    closeBtn.setBounds (row1.removeFromRight (30)); row1.removeFromRight (6);
    removeBtn.setBounds (row1.removeFromRight (84)); row1.removeFromRight (12);
    place (row1, rateBox, 84); place (row1, modBox, 112, 10); place (row1, retrigBtn, 74, 10);
    place (row1, pitchF, 196); place (row1, rampModeBtn, 64, 10); place (row1, revBox, 108);
    // row 2: six fields share the width left of the ratchet
    auto rr = row2.removeFromRight (std::min (300, row2.getWidth() / 3));
    ratchetBtn.setBounds (rr.removeFromLeft (84)); rr.removeFromLeft (6); curve.setBounds (rr);
    row2.removeFromRight (10);
    const int w = (row2.getWidth() - 5 * 6) / 6;
    for (auto* f : { &fadeInF, &fadeOutF, &swingF, &rampF, &decayF, &gateF }) place (row2, *f, w);
}

void RepeatStrip::paint (juce::Graphics& g)
{
    if (! P.glass)   // (GHOST: the editor paints the glass panel under the strip)
    {
        g.fillAll (P.panel);
        g.setColour (P.line); g.fillRect (0, 0, getWidth(), 1);
    }
    const auto* r = proc.project().findRepeat (rid);
    g.setFont (LnF::uiFont (13.0f)); g.setColour (P.marker);
    auto t = titleArea;
    g.drawText ("GHOST REPEAT", t.removeFromTop (16), juce::Justification::centredLeft);
    if (r != nullptr)
    {
        const auto tr = repeat::triggers (*r, proc.repeatBpm(), proc.project().sampleRate);
        g.setFont (LnF::uiFont (10.5f, false)); g.setColour (P.dim);
        g.drawText (juce::String ((int) tr.size()) + " repeats  " + juce::String (juce::roundToInt (r->length / proc.project().sampleRate * 1000.0)) + " ms  @" + juce::String (proc.repeatBpm(), 1),
                    t, juce::Justification::centredLeft);
    }
}

// ---------------------------------------------------------------- the overlay (REPEAT spans + HOLD brackets)
juce::Rectangle<float> EditOverlay::repeatHandle (const RepeatOp& r) const
{
    const float x = view.xOf (proc.pfToHf (r.end())); const auto w = view.waveArea().toFloat();
    return { x - 5.0f, w.getCentreY() - 16.0f, 10.0f, 32.0f };
}
juce::Rectangle<float> EditOverlay::repeatTab (const RepeatOp& r) const
{
    const float x = view.xOf (proc.pfToHf (r.start)); const auto w = view.waveArea().toFloat();
    return { x + 2.0f, w.getY() + 2.0f, 92.0f, 15.0f };
}
juce::Rectangle<float> EditOverlay::holdHandle (const Region& r, const HoldOp& h, int what) const
{
    const auto sp = ops::holdSpan (proc.project(), r, h); if (! sp.ok) return {};
    const auto w = view.waveArea().toFloat();
    const float xa = view.xOf (proc.pfToHf (sp.a)), xb = view.xOf (proc.pfToHf (sp.b)), xs = view.xOf (proc.pfToHf (sp.selA));
    switch (what)
    {
        case 1: return { xa - 6.0f, w.getBottom() - 22.0f, 12.0f, 14.0f };
        case 2: return { xb - 6.0f, w.getBottom() - 22.0f, 12.0f, 14.0f };
        case 3: return { xb - 5.0f, w.getCentreY() - 18.0f, 10.0f, 36.0f };
        case 4: return { xs + 2.0f, w.getY() + 20.0f, 100.0f, 15.0f };
        default: return {};
    }
}
EditOverlay::HoldHit EditOverlay::holdAt (juce::Point<float> p) const
{
    for (auto& r : proc.project().regions)
        for (auto& h : r.holds)
            for (int what : { 3, 1, 2, 4 })
                if (holdHandle (r, h, what).expanded (2.0f).contains (p)) return { r.id, h.id, what };
    return {};
}

bool EditOverlay::hitTest (int x, int y)
{
    if (view.workspace() != Workspace::Edit) return false;
    const juce::Point<float> p ((float) x, (float) y);
    for (auto& r : proc.project().repeats) if (repeatHandle (r).expanded (2.0f).contains (p) || repeatTab (r).contains (p)) return true;
    return holdAt (p).hold != 0;
}

void EditOverlay::paint (juce::Graphics& g)
{
    if (view.workspace() != Workspace::Edit || proc.getSource() == nullptr) return;
    const auto& p = proc.project();
    const auto w = view.waveArea().toFloat();
    g.reduceClipRegion (w.toNearestInt());
    for (auto& r : p.repeats)
    {
        const float x0 = view.xOf (proc.pfToHf (r.start)), x1 = view.xOf (proc.pfToHf (r.end()));
        if (x1 < w.getX() || x0 > w.getRight()) continue;
        const bool sel = r.id == selRepeat;
        g.setColour (P.marker.withAlpha (sel ? 0.16f : 0.09f)); g.fillRect (x0, w.getY(), x1 - x0, w.getHeight());
        g.setColour (P.marker.withAlpha (0.8f)); g.fillRect (x0, w.getY(), 1.5f, w.getHeight());
        // the triggers
        const auto tr = repeat::triggers (r, proc.repeatBpm(), p.sampleRate);
        for (auto& t : tr)
        {
            const float x = view.xOf (proc.pfToHf (r.start + t.t * p.sampleRate));
            g.setColour (t.reverse ? P.pitch : P.marker); g.fillRect (x - 0.5f, w.getBottom() - 14.0f, 1.5f, 12.0f);
            const float xe = view.xOf (proc.pfToHf (r.start + (t.t + t.len) * p.sampleRate));
            g.setColour (P.marker.withAlpha (0.35f)); g.fillRect (x, w.getBottom() - 4.0f, std::max (1.0f, xe - x - 1.0f), 2.0f);
        }
        auto tab = repeatTab (r);
        g.setColour (sel ? P.marker : P.card); g.fillRoundedRectangle (tab, 4.0f);
        g.setColour (sel ? P.onAccent : P.marker); g.setFont (LnF::uiFont (10.5f));
        g.drawText ("REPEAT 1/" + juce::String (r.rateDiv) + (r.rateMod == 1 ? "T" : r.rateMod == 2 ? "." : ""), tab.toNearestInt(), juce::Justification::centred);
        const auto hd = repeatHandle (r);
        g.setColour (P.marker); g.fillRoundedRectangle (hd, 3.0f);
        g.setColour (P.onAccent); for (int k = -1; k <= 1; ++k) g.fillRect (hd.getCentreX() - 0.5f + (float) k * 2.5f, hd.getCentreY() - 6.0f, 1.0f, 12.0f);
    }
    static const char* modeNames[] { "ELASTIC", "SUSTAIN", "TEXTURE", "FREEZE" };
    for (auto& r : p.regions)
        for (auto& h : r.holds)
        {
            const auto sp = ops::holdSpan (p, r, h); if (! sp.ok) continue;
            const float xs = view.xOf (proc.pfToHf (sp.selA)), xa = view.xOf (proc.pfToHf (sp.a)), xb = view.xOf (proc.pfToHf (sp.b)), xe = view.xOf (proc.pfToHf (sp.selB));
            const bool sel = h.id == selHold;
            // the selection bracket (head / tail at 1x) and the held segment
            g.setColour (P.pitch.withAlpha (0.85f));
            g.fillRect (xs, w.getY() + 38.0f, xe - xs, 1.5f); g.fillRect (xs, w.getY() + 34.0f, 1.5f, 9.0f); g.fillRect (xe - 1.5f, w.getY() + 34.0f, 1.5f, 9.0f);
            g.setColour (P.marker.withAlpha (sel ? 0.20f : 0.12f)); g.fillRect (xa, w.getY() + 40.0f, xb - xa, w.getHeight() - 40.0f);
            g.setColour (P.marker); g.fillRect (xa, w.getY() + 40.0f, 1.5f, w.getHeight() - 40.0f); g.fillRect (xb - 1.5f, w.getY() + 40.0f, 1.5f, w.getHeight() - 40.0f);
            auto tab = holdHandle (r, h, 4);
            g.setColour (sel ? P.marker : P.card); g.fillRoundedRectangle (tab, 4.0f);
            g.setColour (sel ? P.onAccent : P.marker); g.setFont (LnF::uiFont (10.5f));
            g.drawText (juce::String ("HOLD ") + modeNames[juce::jlimit (0, 3, (int) h.mode)] + (h.autoStable ? "" : " *"), tab.toNearestInt(), juce::Justification::centred);
            for (int what : { 1, 2 })
            {   // the stable region's manual handles (triangles at the foot)
                const auto hr = holdHandle (r, h, what);
                juce::Path tri; tri.addTriangle (hr.getCentreX(), hr.getY(), hr.getX(), hr.getBottom(), hr.getRight(), hr.getBottom());
                g.setColour (P.marker); g.fillPath (tri);
            }
            const auto hd = holdHandle (r, h, 3);
            g.setColour (P.marker); g.fillRoundedRectangle (hd, 3.0f);
            g.setColour (P.onAccent); for (int k = -1; k <= 1; ++k) g.fillRect (hd.getCentreX() - 0.5f + (float) k * 2.5f, hd.getCentreY() - 7.0f, 1.0f, 14.0f);
        }
}

void EditOverlay::mouseMove (const juce::MouseEvent& e)
{
    const auto p = e.position;
    bool edge = false;
    for (auto& r : proc.project().repeats) if (repeatHandle (r).expanded (2.0f).contains (p)) edge = true;
    const auto hh = holdAt (p); if (hh.what >= 1 && hh.what <= 3) edge = true;
    setMouseCursor (edge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::PointingHandCursor);
}

void EditOverlay::mouseDown (const juce::MouseEvent& e)
{
    drag = Drag::None; moved = false; downHf = view.hfAt (e.position.x);
    for (auto& r : proc.project().repeats)
    {
        bool hit = false;
        if (repeatHandle (r).expanded (2.0f).contains (e.position)) { drag = Drag::RepeatLen; dragRepeat = r.id; start0 = r.start; len0 = r.length; hit = true; proc.beginGesture(); }
        else if (repeatTab (r).contains (e.position)) hit = true;
        if (hit) { selRepeat = r.id; if (onSelectRepeat) onSelectRepeat (r.id); repaint(); return; }
    }
    const auto hh = holdAt (e.position);
    if (hh.hold != 0)
    {
        selHold = hh.hold; selHoldRegion = hh.region; dragRegion = hh.region; dragHold = hh.hold;
        if (const auto* r = proc.project().find (hh.region))
            for (auto& h : r->holds)
                if (h.id == hh.hold)
                {
                    const auto sp = ops::holdSpan (proc.project(), *r, h);
                    len0 = sp.b - sp.a;
                    for (auto& wm : r->warp) { if (wm.id == h.markerA) start0 = (double) wm.s; if (wm.id == h.markerB) end0 = (double) wm.s; }
                }
        drag = hh.what == 1 ? Drag::HoldA : hh.what == 2 ? Drag::HoldB : hh.what == 3 ? Drag::HoldLen : Drag::None;
        if (drag != Drag::None) proc.beginGesture();
        if (onSelectHold) onSelectHold (hh.region, hh.hold);
        repaint();
    }
}

void EditOverlay::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == Drag::None || (! moved && e.getDistanceFromDragStart() < 3)) return;
    moved = true;
    const double hf = view.hfAt (e.position.x);
    const auto& p = proc.project();
    const bool alt = e.mods.isAltDown();
    if (drag == Drag::RepeatLen)
    {
        const auto* r = p.findRepeat (dragRepeat); if (r == nullptr) return;
        RepeatOp x = *r;
        const double endHf = view.snapHf (proc.pfToHf (start0 + len0) + (hf - downHf), false, alt);
        x.length = std::max (p.sampleRate * 0.01, proc.hfToPf (endHf) - x.start);
        if (auto q = ops::setRepeat (p, x)) proc.apply (*q, "Repeat length");
    }
    else
    {
        const auto* r = p.find (dragRegion); if (r == nullptr) return;
        const HoldOp* h = nullptr; for (auto& x : r->holds) if (x.id == dragHold) h = &x;
        if (h == nullptr) return;
        juce::String why; std::optional<Project> q;
        if (drag == Drag::HoldLen) q = ops::setHoldLength (p, r->id, h->id, len0 + proc.hfToPf (hf) - proc.hfToPf (downHf), &why);
        else
        {
            int64 a = 0, b = 0; for (auto& wm : r->warp) { if (wm.id == h->markerA) a = wm.s; if (wm.id == h->markerB) b = wm.s; }
            const int64 d = (int64) std::llround ((hf - downHf) * p.source.sampleRate / proc.hostRate());
            if (drag == Drag::HoldA) q = ops::setHoldStable (p, r->id, h->id, (int64) start0 + d, b, &why);
            else q = ops::setHoldStable (p, r->id, h->id, a, (int64) end0 + d, &why);
        }
        if (q) proc.apply (*q, drag == Drag::HoldLen ? "Hold length" : "Hold stable region");
        else if (why.isNotEmpty() && toast) toast (why);
    }
    view.repaint();
}

void EditOverlay::mouseUp (const juce::MouseEvent&)
{
    if (drag != Drag::None) proc.endGesture();
    drag = Drag::None;
}
} // namespace gslice::ui
