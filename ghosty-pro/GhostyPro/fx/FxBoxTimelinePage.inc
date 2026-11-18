// GHOST MOTION timeline view (included by FxPanel.cpp inside its anonymous namespace, before FxBox3Pages.inc; GPFX_BOX builds only).
// It lives INSIDE the existing MOTION tab: the TIMELINE switch in the MOTION page's right column swaps the 5 classic lanes' editor for
// this view (the classic lanes keep running). One loop of 1 .. 8 bars for the whole rack:
//   row 1: LOOP length, + LANE (any continuous rack setting), DETECT HITS (AUDIO-AWARE), the RULE, the event LENGTH, PLACE ON HITS
//   row 2: a chip per lane (click = edit it, right-click = remove)
//   row 3: the edited lane: ON, BASE (CURVE / STEPS), STEPS count, DEPTH, EDIT (EVENTS / BASE)
//   the editor: the loop's bars and beats, the hits found in the input (ticks on top), the base, the events
// Drag an effect tile from the strip onto the editor: a lane for that unit's MIX with an event where you dropped it.

constexpr fxtheme::C kTimelineC { 0xffC6FF4F };

class TimelineView : public juce::Component, public TimelineUiApi, private juce::Timer
{
public:
    TimelineView (juce::AudioProcessorValueTreeState&, Rack::Impl& r) : rack (r)
    {
        auto btn = [this] (juce::TextButton& b, const char* text, const char* tip) { b.setButtonText (text); b.setTooltip (tip); b.setColour (juce::TextButton::buttonOnColourId, kTimelineC); b.setColour (juce::TextButton::textColourOffId, col::white); addAndMakeVisible (b); };
        btn (addLane, "+ LANE", "Add a lane: pick any rack setting (switches and choices can't be moved smoothly, so they are not listed). Or drag an effect tile here");
        btn (detect, "DETECT HITS", "AUDIO-AWARE: find the hits in the last full loop of Ghost Box's input (play the loop once with the transport running)");
        btn (place, "PLACE ON HITS", "Put an event on every hit the RULE picks, in the selected lane (replaces the events placed this way before; your own events stay)");
        btn (onB, "ON", "This lane on / off (off: it glides back to the setting's own value)");
        btn (removeB, "REMOVE", "Remove the selected lane");
        btn (baseCurve, "CURVE", "Base of this lane: a drawn curve over the loop");
        btn (baseSteps, "STEPS", "Base of this lane: steps over the loop");
        btn (editEvents, "EVENTS", "Edit the events: drag on an empty place = a new event (its height = its amount), drag an event = move it, drag its right end = length, double-click or right-click = delete");
        btn (editBase, "BASE", "Edit the base (the curve or the steps) of this lane");
        for (auto* b : { &onB, &baseCurve, &baseSteps, &editEvents, &editBase }) b->setClickingTogglesState (false);
        addLane.onClick = [this] { laneMenu(); };
        detect.onClick = [this] { runDetect(); };
        place.onClick = [this] { placeOnHits(); };
        onB.onClick = [this] { if (auto* L = lane()) { rack.pushUndo(); L->on = ! L->on; changed(); } };
        removeB.onClick = [this] { if (lane() != nullptr) { rack.pushUndo(); rack.box.tlRemoveLane (sel); sel = juce::jmax (0, sel - 1); rebuildChips(); changed(); } };
        baseCurve.onClick = [this] { if (auto* L = lane()) { rack.pushUndo(); L->mode = 0; editMode = 1; changed(); } };
        baseSteps.onClick = [this] { if (auto* L = lane()) { rack.pushUndo(); L->mode = 1; editMode = 1; changed(); } };
        editEvents.onClick = [this] { editMode = 0; refresh(); };
        editBase.onClick = [this] { editMode = 1; refresh(); };
        auto combo = [this] (juce::ComboBox& c, const juce::StringArray& items, const char* tip) { c.addItemList (items, 1); c.setTooltip (tip); c.setColour (juce::ComboBox::outlineColourId, kTimelineC); c.setColour (juce::ComboBox::arrowColourId, kTimelineC); addAndMakeVisible (c); };
        combo (bars, tlBarsNames(), "LOOP: the timeline's length (1 - 8 bars, in time with the song)");
        combo (rule, tlRuleNames(), "RULE: which of the hits get an event");
        combo (evLen, tlEventLenNames(), "Length of a new event");
        combo (steps, tlStepsNames(), "How many steps the loop has (STEPS base)");
        bars.onChange = [this] { const int b = bars.getSelectedItemIndex() + 1; if (b >= 1 && b != rack.box.timeline.bars) { rack.pushUndo(); rack.box.timeline.bars = b; changed(); } };
        rule.onChange = [this] { rack.box.timeline.rule = juce::jmax (0, rule.getSelectedItemIndex()); };
        evLen.onChange = [this] { rack.box.timeline.eventLen = juce::jmax (0, evLen.getSelectedItemIndex()); };
        steps.onChange = [this] { if (auto* L = lane()) { const int i = juce::jmax (0, steps.getSelectedItemIndex()); if (i != L->stepsIdx) { rack.pushUndo(); L->stepsIdx = i; changed(); } } };
        depth.setSliderStyle (juce::Slider::LinearHorizontal); depth.setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, 16); depth.setRange (-100.0, 100.0, 1.0); depth.setTextValueSuffix (" %");
        depth.setColour (juce::Slider::rotarySliderFillColourId, kTimelineC); depth.setTooltip ("DEPTH: how far the lane moves the setting (negative = the other way)");
        depth.setDoubleClickReturnValue (true, 50.0);
        depth.onDragStart = [this] { rack.pushUndo(); };
        depth.onValueChange = [this] { if (auto* L = lane()) { L->depth = (float) depth.getValue() / 100.0f; rack.box.tlChanged(); } };
        addAndMakeVisible (depth);
        editor = std::make_unique<Editor> (*this); addAndMakeVisible (*editor);
        curveEd = std::make_unique<bendlane::Editor> (rack.box.tlShapes[0]); addChildComponent (*curveEd);
        rack.box.tlListen = true;
        rebuildChips(); refresh();
        startTimerHz (30);
    }
    ~TimelineView() override { stopTimer(); rack.box.tlListen = false; }

    void paint (juce::Graphics& g) override
    {
        const auto ink2 = ghost() ? ghosty::glass::ink2() : col::dim.brighter (0.25f);
        drawCaption (g, loopCap.toFloat(), "LOOP", ink2, 10.0f);
        drawCaption (g, ruleCap.toFloat(), "RULE", ink2, 10.0f);
        drawCaption (g, depthCap.toFloat(), "DEPTH", ink2, 10.0f);
        drawCaption (g, editCap.toFloat(), "EDIT", ink2, 10.0f);
        if (chips.isEmpty()) drawCaption (g, chipsR.toFloat(), "No lanes yet: + LANE, right-click a knob (Add to GHOST MOTION timeline), or drag an effect tile into the timeline", ink2, 10.5f);
        drawCaption (g, msgR.toFloat(), message, ghost() ? ghosty::glass::violetDeep() : kTimelineC.get(), 10.5f, juce::Justification::centredRight);
    }
    void resized() override
    {
        auto r = getLocalBounds();
        auto row1 = r.removeFromTop (28);
        loopCap = row1.removeFromLeft (34); bars.setBounds (row1.removeFromLeft (86).reduced (0, 2)); row1.removeFromLeft (6);
        addLane.setBounds (row1.removeFromLeft (76).reduced (0, 1)); row1.removeFromLeft (14);
        detect.setBounds (row1.removeFromLeft (104).reduced (0, 1)); row1.removeFromLeft (8);
        ruleCap = row1.removeFromLeft (32); rule.setBounds (row1.removeFromLeft (132).reduced (0, 2)); row1.removeFromLeft (4);
        evLen.setBounds (row1.removeFromLeft (64).reduced (0, 2)); row1.removeFromLeft (4);
        place.setBounds (row1.removeFromLeft (116).reduced (0, 1)); row1.removeFromLeft (8);
        msgR = row1;
        r.removeFromTop (4);
        chipsR = r.removeFromTop (24);
        r.removeFromTop (4);
        auto row3 = r.removeFromTop (26);
        onB.setBounds (row3.removeFromLeft (44).reduced (0, 1)); row3.removeFromLeft (8);
        baseCurve.setBounds (row3.removeFromLeft (62).reduced (0, 1)); baseSteps.setBounds (row3.removeFromLeft (62).reduced (0, 1)); row3.removeFromLeft (4);
        steps.setBounds (row3.removeFromLeft (56).reduced (0, 2)); row3.removeFromLeft (10);
        depthCap = row3.removeFromLeft (40); depth.setBounds (row3.removeFromLeft (170)); row3.removeFromLeft (10);
        editCap = row3.removeFromLeft (30); editEvents.setBounds (row3.removeFromLeft (70).reduced (0, 1)); editBase.setBounds (row3.removeFromLeft (56).reduced (0, 1)); row3.removeFromLeft (10);
        removeB.setBounds (row3.removeFromRight (76).reduced (0, 1));
        r.removeFromTop (6);
        editor->setBounds (r); curveEd->setBounds (r.withTrimmedTop (12));
        layoutChips();
    }

    // ---- drop an effect tile (the strip calls this through the panel): a lane for its MIX, an event where it landed
    bool dropUnit (int u, juce::Point<int> screenPos) override
    {
        if (u < 0 || u >= kNumUnits || u == uLim) return false;
        const auto local = getLocalPoint (nullptr, screenPos);
        if (! getLocalBounds().contains (local)) return false;
        juce::String id = unitInfo (u).hasMix ? pid (u, "mix") : juce::String();
        if (id.isEmpty() || ! rack.box.tlAllowed (id))
            for (auto* p : rack.unitParams[(size_t) u]) if (rack.box.tlAllowed (p->getParameterID()) && ! p->getParameterID().endsWith ("_on")) { id = p->getParameterID(); break; }
        if (id.isEmpty()) return false;
        rack.pushUndo();
        const int k = rack.box.tlAddLane (id);
        if (k < 0) { message = "The timeline is full (12 lanes)"; repaint(); return false; }
        sel = k; editMode = 0;
        const auto ep = editor->getLocalPoint (this, local);
        if (editor->getLocalBounds().contains (ep))
        {
            auto& L = rack.box.timeline.lanes[(size_t) k];
            TlEvent e; e.pos = editor->snapBeat (editor->beatAt ((float) ep.x)); e.len = tlEventLenBeats (rack.box.timeline.eventLen); e.amt = rack.box.timeline.eventAmt;
            if ((int) L.events.size() < kTlEvents) L.events.push_back (e);
        }
        if (! unitOn (u)) message = juce::String (unitInfo (u).title) + " is off: switch it on to hear the lane";
        rebuildChips(); changed();
        return true;
    }
    // tests
    int selected() const override { return sel; }
    void select (int k) override { sel = juce::jlimit (0, juce::jmax (0, (int) rack.box.timeline.lanes.size() - 1), k); refresh(); }
    juce::Component& editorComponent() override { return *editor; }
    std::vector<Text> uiTexts() const override
    {
        std::vector<Text> v; const auto ink2 = ghost() ? ghosty::glass::ink2() : col::dim.brighter (0.25f);
        v.push_back ({ loopCap.withSizeKeepingCentre (loopCap.getWidth(), 9), ink2, "LOOP" });
        v.push_back ({ depthCap.withSizeKeepingCentre (depthCap.getWidth(), 9), ink2, "DEPTH" });
        v.push_back ({ editor->getBounds().removeFromTop (12).withWidth (40).translated (6, 0), editor->inkBars(), "bar numbers" });
        return v;
    }
    void runDetect()
    {
        message = "listening: finding the hits..."; repaint();
        juce::Component::SafePointer<TimelineView> sp (this);
        rack.box.tlDetectAsync ([sp] (TlAnalysis a)
        {
            if (sp == nullptr) return;
            sp->message = a.ok ? juce::String ((int) a.hits.size()) + " hits in the last loop" : a.why;
            sp->editor->repaint(); sp->repaint();
        });
    }
    void placeOnHits()
    {
        if (lane() == nullptr) { message = "Add a lane first"; repaint(); return; }
        if (! rack.box.lastHits.ok) { message = "DETECT HITS first"; repaint(); return; }
        rack.pushUndo();
        const int n = rack.box.tlPlaceOnHits (sel, rack.box.lastHits, rack.box.timeline.rule);
        message = juce::String (n) + " events on " + tlRuleNames()[rack.box.timeline.rule].toLowerCase();
        editMode = 0; refresh();
    }

private:
    // ---------------------------------------------------------------- the loop editor
    struct Editor : juce::Component, juce::SettableTooltipClient
    {
        explicit Editor (TimelineView& o) : v (o) { setTooltip ("The loop: hits found in the input are the ticks on top. EVENTS: drag on an empty place = new event, drag = move, right end = length, double-click / right-click = delete"); }
        TimelineView& v;
        double loopBeats() const { return juce::jlimit (1, 8, v.rack.box.timeline.bars) * v.rack.box.tlBpb.load(); }
        juce::Rectangle<float> area() const { auto b = getLocalBounds().toFloat(); return { b.getX() + 8, b.getY() + 16, b.getWidth() - 16, b.getHeight() - 24 }; }
        double beatAt (float x) const { const auto a = area(); return juce::jlimit (0.0, loopBeats(), (double) (x - a.getX()) / a.getWidth() * loopBeats()); }
        float xOf (double beat) const { const auto a = area(); return a.getX() + (float) (beat / loopBeats()) * a.getWidth(); }
        float yOf (float val) const { const auto a = area(); return a.getCentreY() - val * a.getHeight() * 0.45f; }
        double snapBeat (double b) const { return juce::jlimit (0.0, loopBeats(), std::round (b * 4.0) / 4.0); }   // 1/16 grid
        juce::Colour inkBars() const { return ghost() ? ghosty::glass::ink() : col::white.withAlpha (0.85f); }
        void paint (juce::Graphics& g) override
        {
            auto b = getLocalBounds().toFloat(); drawScreen (g, b, kTimelineC);
            const auto a = area(); const double lb = loopBeats(); const double bpb = v.rack.box.tlBpb.load();
            const auto acc = kTimelineC.get();
            // grid: 1/16 faint, beats, bars with numbers
            const int sixteenths = (int) std::lround (lb * 4.0);
            for (int k = 0; k <= sixteenths; ++k)
            {
                const float x = a.getX() + a.getWidth() * (float) k / (float) juce::jmax (1, sixteenths);
                const bool bar = std::fmod (k / 4.0, bpb) < 1.0e-6, beat = k % 4 == 0;
                g.setColour ((ghost() ? ghosty::glass::ink() : col::white.get()).withAlpha (bar ? 0.35f : beat ? 0.16f : 0.06f));
                g.fillRect (x, a.getY(), bar ? 1.4f : 1.0f, a.getHeight());
                if (bar && k < sixteenths) { g.setColour (inkBars()); g.setFont (font (9.5f)); g.drawText (juce::String ((int) std::lround (k / 4.0 / bpb) + 1), juce::Rectangle<float> (x + 3, b.getY() + 2, 30, 12), juce::Justification::centredLeft, false); }
            }
            g.setColour ((ghost() ? ghosty::glass::ink() : col::white.get()).withAlpha (0.18f)); g.fillRect (a.getX(), a.getCentreY(), a.getWidth(), 1.0f);
            // hits found in the input (ticks on top; taller = louder)
            const auto& hits = v.rack.box.lastHits;
            if (hits.ok)
            {
                float lo = 0, hi = -100; for (auto& h : hits.hits) { lo = std::min (lo, h.levelDb); hi = std::max (hi, h.levelDb); }
                for (auto& h : hits.hits)
                {
                    if (h.beat > lb) continue;
                    const float k = hi - lo > 1.0f ? (h.levelDb - lo) / (hi - lo) : 1.0f;
                    g.setColour (T (juce::Colour (0xffFF4FD8)).withAlpha (0.9f)); g.fillRect (xOf (h.beat) - 1.0f, a.getY() - 2.0f, 2.0f, 5.0f + 9.0f * k);
                }
            }
            auto* L = v.lane(); if (L == nullptr) return;
            // base (faint when editing the events)
            juce::Path p; const auto sh = v.rack.box.tlShapes[(size_t) v.sel].get();
            for (int i = 0; i <= 200; ++i)
            {
                const double ph = i / 200.0 * 0.99999; const float x = a.getX() + a.getWidth() * (float) i / 200.0f;
                const float val = L->mode == 1 ? tlStepsAt (L->steps.data(), tlStepCount (L->stepsIdx), ph) : sh.valueAt (ph);
                if (i == 0) p.startNewSubPath (x, yOf (val)); else p.lineTo (x, yOf (val));
            }
            g.setColour (acc.withAlpha (v.editMode == 1 ? 0.95f : 0.35f)); g.strokePath (p, juce::PathStrokeType (v.editMode == 1 ? 2.0f : 1.2f));
            // events
            for (int i = 0; i < (int) L->events.size(); ++i)
            {
                const auto& e = L->events[(size_t) i];
                auto drawPart = [&] (double from, double len)
                {
                    const float x0 = xOf (from), x1 = xOf (std::min (lb, from + len)), y = yOf (e.amt), y0 = yOf (0.0f);
                    auto rr = juce::Rectangle<float>::leftTopRightBottom (x0, std::min (y, y0), std::max (x0 + 3.0f, x1), std::max (y, y0));
                    g.setColour ((e.autoPlaced ? T (juce::Colour (0xffFF4FD8)) : acc).withAlpha (i == drag ? 0.75f : 0.45f)); g.fillRoundedRectangle (rr, 3.0f);
                    g.setColour ((e.autoPlaced ? T (juce::Colour (0xffFF4FD8)) : acc)); g.drawRoundedRectangle (rr, 3.0f, 1.0f);
                };
                drawPart (e.pos, e.len);
                if (e.pos + e.len > lb) drawPart (0.0, e.pos + e.len - lb);   // (wraps over the loop end)
            }
            // play head + the lane value now
            const float pos = v.rack.box.tlPos.load();
            if (pos >= 0.0f) { g.setColour (inkBars()); g.fillRect (a.getX() + a.getWidth() * pos, a.getY(), 1.5f, a.getHeight()); }
        }
        int eventAt (juce::Point<float> pt, bool& atEnd) const
        {
            auto* L = v.lane(); atEnd = false; if (L == nullptr) return -1;
            for (int i = (int) L->events.size(); --i >= 0;)
            {
                const auto& e = L->events[(size_t) i];
                const float x0 = xOf (e.pos), x1 = xOf (std::min (loopBeats(), e.pos + e.len)), y = yOf (e.amt), y0 = yOf (0.0f);
                if (pt.x >= x0 - 2 && pt.x <= std::max (x0 + 6.0f, x1) + 3 && pt.y >= std::min (y, y0) - 6 && pt.y <= std::max (y, y0) + 6) { atEnd = pt.x > std::max (x0 + 6.0f, x1) - 6; return i; }
            }
            return -1;
        }
        float valueAt (float y) const { const auto a = area(); float val = juce::jlimit (-1.0f, 1.0f, (a.getCentreY() - y) / (a.getHeight() * 0.45f)); return std::round (val * 12.0f) / 12.0f; }
        void mouseDown (const juce::MouseEvent& e) override
        {
            auto* L = v.lane(); if (L == nullptr) return;
            if (v.editMode == 1 && L->mode == 1) { v.rack.pushUndo(); setStep (e.position); return; }
            if (v.editMode != 0) return;
            bool atEnd = false; const int i = eventAt (e.position, atEnd);
            if (i >= 0 && (e.mods.isPopupMenu() || e.getNumberOfClicks() >= 2)) { v.rack.pushUndo(); L->events.erase (L->events.begin() + i); drag = -1; v.changed(); return; }
            if (e.mods.isPopupMenu()) return;
            v.rack.pushUndo();
            if (i >= 0) { drag = i; dragEnd = atEnd; grabBeat = beatAt (e.position.x) - L->events[(size_t) i].pos; }
            else if ((int) L->events.size() < kTlEvents)
            {
                TlEvent ev; ev.pos = snapBeat (beatAt (e.position.x)); ev.len = tlEventLenBeats (v.rack.box.timeline.eventLen); ev.amt = valueAt (e.position.y);
                if (std::abs (ev.amt) < 1.0e-3f) ev.amt = v.rack.box.timeline.eventAmt;
                L->events.push_back (ev); drag = (int) L->events.size() - 1; dragEnd = true; grabBeat = 0; creating = true;
            }
            v.changed();
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            auto* L = v.lane(); if (L == nullptr) return;
            if (v.editMode == 1 && L->mode == 1) { setStep (e.position); return; }
            if (drag < 0 || drag >= (int) L->events.size()) return;
            auto& ev = L->events[(size_t) drag];
            if (dragEnd) ev.len = juce::jlimit (0.0625, loopBeats(), std::max (0.0625, snapBeat (beatAt (e.position.x)) - ev.pos));
            else { ev.pos = juce::jlimit (0.0, loopBeats() - 0.0625, snapBeat (beatAt (e.position.x) - grabBeat)); ev.amt = valueAt (e.position.y); if (std::abs (ev.amt) < 1.0e-3f) ev.amt = 1.0f / 12.0f; }
            if (creating && ! dragEnd) {}
            ev.autoPlaced = false;   // an edited event is yours
            v.rack.box.tlChanged(); repaint();
        }
        void mouseUp (const juce::MouseEvent&) override
        {
            if (auto* L = v.lane(); L != nullptr && drag >= 0) std::sort (L->events.begin(), L->events.end(), [] (const TlEvent& x, const TlEvent& y) { return x.pos < y.pos; });
            drag = -1; creating = false; v.changed();
        }
        void setStep (juce::Point<float> pt)
        {
            auto* L = v.lane(); if (L == nullptr) return;
            const auto a = area(); const int n = tlStepCount (L->stepsIdx);
            const int i = juce::jlimit (0, n - 1, (int) ((pt.x - a.getX()) / a.getWidth() * (float) n));
            L->steps[(size_t) i] = valueAt (pt.y); v.rack.box.tlChanged(); repaint();
        }
        int drag = -1; bool dragEnd = false, creating = false; double grabBeat = 0;
    };
    struct Chip : juce::Button
    {
        Chip (TimelineView& o, int k) : juce::Button (o.rack.box.tlLaneName (k)), view (o), lane (k) { setTooltip ("Click: edit this lane. Right-click: remove it"); }
        TimelineView& view; int lane;
        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            const bool on = view.sel == lane;
            setToggleState (on, juce::dontSendNotification); setColour (juce::TextButton::buttonOnColourId, kTimelineC);
            getLookAndFeel().drawButtonBackground (g, *this, findColour (juce::TextButton::buttonColourId), over, down);
            const bool laneOn = juce::isPositiveAndBelow (lane, (int) view.rack.box.timeline.lanes.size()) && view.rack.box.timeline.lanes[(size_t) lane].on;
            const auto ink = ghost() ? (on ? juce::Colours::white : ghosty::glass::ink()) : on ? gpfx::ui::LnF::litColours (kTimelineC.get()).second : col::white.get();
            g.setColour (ink.withAlpha (laneOn ? 1.0f : 0.55f)); g.setFont (font (10.5f));
            g.drawFittedText (getButtonText(), getLocalBounds().reduced (6, 0), juce::Justification::centred, 1, 0.8f);
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu()) { view.rack.pushUndo(); view.rack.box.tlRemoveLane (lane); view.sel = juce::jmax (0, juce::jmin (view.sel, (int) view.rack.box.timeline.lanes.size() - 1)); view.rebuildChips(); view.changed(); return; }
            if (! e.mouseWasDraggedSinceMouseDown()) { view.sel = lane; view.refresh(); }
        }
    };

    TlLane* lane() const { auto& ls = rack.box.timeline.lanes; return juce::isPositiveAndBelow (sel, (int) ls.size()) ? &ls[(size_t) sel] : nullptr; }
    bool unitOn (int u) const { return rack.onP[(size_t) u].b(); }
    void changed() { rack.box.tlChanged(); refresh(); }
    void refresh()
    {
        const int nL = (int) rack.box.timeline.lanes.size();
        if (nL != chips.size()) rebuildChips();
        sel = juce::jlimit (0, juce::jmax (0, nL - 1), sel);
        auto* L = lane();
        bars.setSelectedItemIndex (juce::jlimit (1, 8, rack.box.timeline.bars) - 1, juce::dontSendNotification);
        rule.setSelectedItemIndex (rack.box.timeline.rule, juce::dontSendNotification);
        evLen.setSelectedItemIndex (rack.box.timeline.eventLen, juce::dontSendNotification);
        for (auto* c : std::initializer_list<juce::Component*> { &onB, &removeB, &baseCurve, &baseSteps, &steps, &depth, &editEvents, &editBase, &place }) c->setEnabled (L != nullptr);
        onB.setToggleState (L != nullptr && L->on, juce::dontSendNotification);
        baseCurve.setToggleState (L != nullptr && L->mode == 0, juce::dontSendNotification); baseSteps.setToggleState (L != nullptr && L->mode == 1, juce::dontSendNotification);
        editEvents.setToggleState (editMode == 0, juce::dontSendNotification); editBase.setToggleState (editMode == 1, juce::dontSendNotification);
        steps.setVisible (L != nullptr && L->mode == 1);
        if (L != nullptr) { steps.setSelectedItemIndex (L->stepsIdx, juce::dontSendNotification); depth.setValue (juce::roundToInt (L->depth * 100.0f), juce::dontSendNotification); }
        // the curve editor (bendlane) shows only while editing a CURVE base
        const bool curve = L != nullptr && editMode == 1 && L->mode == 0;
        if (curve && curveFor != sel)
        {
            curveEd = std::make_unique<bendlane::Editor> (rack.box.tlShapes[(size_t) sel]);
            curveEd->accent = kTimelineC.get(); curveEd->ghostLook = 15;
            curveEd->rangeSemis = [] { return 1.0f; };
            curveEd->bars = [this] { return juce::jlimit (1, 8, rack.box.timeline.bars) * rack.box.tlBpb.load() / 4.0; };
            curveEd->playPos = [this] { return rack.box.tlPos.load(); };
            curveEd->onEditStart = [this] { rack.pushUndo(); };
            curveEd->valueText = [] (float val) { const int pc = juce::roundToInt (val * 100); return (pc > 0 ? "+" : "") + juce::String (pc) + "%"; };
            addChildComponent (*curveEd); curveFor = sel; fxtheme::retint (*curveEd);
            resized();
        }
        curveEd->setVisible (curve);
        for (auto* c : chips) c->repaint();
        editor->repaint(); repaint();
    }
    void rebuildChips()
    {
        chips.clear();
        for (int k = 0; k < (int) rack.box.timeline.lanes.size(); ++k) { auto* c = chips.add (new Chip (*this, k)); addAndMakeVisible (c); }
        fxtheme::retint (*this);
        layoutChips(); curveFor = -1;
    }
    void layoutChips()
    {
        auto r = chipsR;
        for (auto* c : chips)
        {
            const int w = juce::jmin (180, (int) ghosty::textWidth (font (10.5f), c->getButtonText()) + 18);
            c->setVisible (r.getWidth() >= w);
            if (r.getWidth() >= w) { c->setBounds (r.removeFromLeft (w)); r.removeFromLeft (4); }
        }
    }
    void laneMenu()
    {   // every continuous fx_ setting, by unit (the units that are on first)
        juce::PopupMenu m; m.addSectionHeader ("ADD A LANE: which setting moves?");
        ids.clear();
        auto addUnit = [this] (juce::PopupMenu& into, int u)
        {
            juce::PopupMenu sub;
            for (auto* p : rack.unitParams[(size_t) u])
            {
                const auto id = p->getParameterID();
                if (! rack.box.tlAllowed (id)) continue;
                ids.add (id); sub.addItem (ids.size(), p->getName (60).replace ("FX ", "").replace (unitInfo (u).title, "").trim());
            }
            if (sub.getNumItems() > 0) into.addSubMenu (juce::String (unitInfo (u).title) + (unitOn (u) ? "" : "  (off)"), sub);
        };
        const auto order = rack.getOrder();
        for (int u : order) if (unitOn (u) && u != uLim) addUnit (m, u);
        juce::PopupMenu offUnits; for (int u : order) if (! unitOn (u) && u != uLim) addUnit (offUnits, u);
        if (offUnits.getNumItems() > 0) m.addSubMenu ("Units that are off", offUnits);
        juce::Component::SafePointer<TimelineView> sp (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addLane), [sp] (int r)
        {
            if (sp == nullptr || r <= 0 || r > sp->ids.size()) return;
            sp->rack.pushUndo();
            const int k = sp->rack.box.tlAddLane (sp->ids[r - 1]);
            if (k < 0) sp->message = "The timeline is full (12 lanes)";
            else sp->sel = k;
            sp->rebuildChips(); sp->changed();
        });
    }
    void timerCallback() override
    {
        if ((int) rack.box.timeline.lanes.size() != chips.size() || rack.box.timeline.bars - 1 != bars.getSelectedItemIndex()) refresh();   // (undo / a load / the knob menu changed it)
        editor->repaint();
    }

    Rack::Impl& rack;
    juce::TextButton addLane, detect, place, onB, removeB, baseCurve, baseSteps, editEvents, editBase;
    juce::ComboBox bars, rule, evLen, steps; juce::Slider depth;
    std::unique_ptr<Editor> editor; std::unique_ptr<bendlane::Editor> curveEd; int curveFor = -1;
    juce::OwnedArray<Chip> chips; juce::StringArray ids;
    juce::Rectangle<int> loopCap, ruleCap, depthCap, editCap, chipsR, msgR;
    juce::String message; int sel = 0, editMode = 0;   // editMode 0 EVENTS, 1 BASE
};
