// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX SPATIAL page (Phase 6b, SPATIAL.md): a sphere around the listener's head, seen from the TOP (front / back / left / right)
// and from the SIDE (above / below). Every switched-on unit is a tile with its own ghost (the strip's tile art). Drag a tile to place
// that unit's wet sound; the further from the head, the further away it sounds. Tiles sitting on the head (the dock below the views)
// are not placed: their sound is exactly as it is in the rack. Recorded movements show as faint trails; the tile rides its path on
// playback. Included by PluginEditor.cpp (uses its drawUnitTile, accentOf, col, font).
#pragma once

namespace phantom
{
class SpatialPage : public juce::Component, private juce::Timer
{
public:
    explicit SpatialPage (PhantomProcessor& p) : proc (p), onT (p.apvts, "fx_spatial_on", "SPATIAL ON", col::ice.get()), outC (p.apvts, "fx_spatial_out", col::ice.get())
    {
        setName ("SPATIAL PAGE"); addAndMakeVisible (onT); addAndMakeVisible (outC); trig.fill (-100);
        for (int m = 0; m < 4; ++m)
        {
            auto* b = modeBtns.add (new juce::TextButton (spatialModeName (m))); b->setClickingTogglesState (true); b->setRadioGroupId (4711);
            b->onClick = [this, m] { proc.spatialAuto.mode = m; }; addAndMakeVisible (b);
        }
        armBtn.setClickingTogglesState (true); armBtn.onClick = [this] { if (sel >= 0) proc.spatialAuto.armed[(size_t) sel] = armBtn.getToggleState(); repaint(); };
        armAll.onClick = [this] { bool any = false; for (int u : shown) any |= ! proc.spatialAuto.armed[(size_t) u].load(); for (int u : shown) proc.spatialAuto.armed[(size_t) u] = any; refreshControls(); repaint(); };
        centreBtn.onClick = [this] { if (sel >= 0) setPlace (sel, 0.0f, 0.0f, 0.0f, true); };
        clearTile.onClick = [this] { if (sel >= 0) proc.spatialAuto.clear (sel); };
        clearAll.onClick = [this] { proc.spatialAuto.clearAll(); };
        clearSect.onClick = [this] { const double bpb = proc.lastTempo().beatsPerBar(); const double a = (fromBar.getValue() - 1) * bpb, b = toBar.getValue() * bpb; proc.spatialAuto.clearSection (sectAll.getToggleState() ? -1 : sel, a, b); };
        sectAll.setClickingTogglesState (true);
        for (auto* s : { &fromBar, &toBar }) { s->setSliderStyle (juce::Slider::IncDecButtons); s->setRange (1, 999, 1); s->setTextBoxStyle (juce::Slider::TextBoxLeft, false, 44, 22); addAndMakeVisible (s); }
        fromBar.setValue (1); toBar.setValue (4);
        for (int q = 0; q < 4; ++q) quantBox.addItem (spatialQuantName (q), q + 1);
        for (int l = 0; l < 4; ++l) loopBox.addItem (spatialLoopName (l), l + 1);
        quantBox.onChange = [this] { if (sel >= 0) proc.spatialAuto.quant[(size_t) sel] = quantBox.getSelectedId() - 1; };
        loopBox.onChange = [this] { if (sel >= 0) proc.spatialAuto.loop[(size_t) sel] = loopBox.getSelectedId() - 1; };
        for (juce::Component* c : std::initializer_list<juce::Component*> { &armBtn, &armAll, &centreBtn, &clearTile, &clearAll, &clearSect, &sectAll, &quantBox, &loopBox }) addAndMakeVisible (c);
        startTimerHz (30);
    }
    ~SpatialPage() override { endDrag(); }

    // tests / screenshots
    void selectUnit (int u) { sel = u; refreshControls(); repaint(); }
    juce::Rectangle<float> topView() const { return topR; }
    juce::Rectangle<float> sideView() const { return sideR; }

    // ---- PAINTED (LAYOUT.md "SPATIAL page"): the look's two view rings (labels, head idol, distance rings and the dark veil are
    // in the art), the control tablet, the tray; the tiles are the look's small tags with each unit's ghost
    std::function<void (juce::Rectangle<int>)> onDirty;
    void animTick (int tick)
    {
        for (int u : shown)
        {
            const float lv = proc.rack.slot (u).uiLevel.exchange (0.0f);
            if (lv > 0.02f && tick - trig[(size_t) u] > 10 && (lv > slow[(size_t) u] * 1.4f || tick - trig[(size_t) u] > 24)) trig[(size_t) u] = tick;
            slow[(size_t) u] += (lv - slow[(size_t) u]) * 0.3f;
            const int pose = painted::poseAfter (tick - trig[(size_t) u], tick, u);
            if (pose == poses[(size_t) u]) continue; poses[(size_t) u] = pose;
            auto mark = [this] (juce::Rectangle<float> r) { const auto ri = r.expanded (10.0f).toNearestInt(); repaint (ri); if (onDirty) onDirty (ri); };
            if (placed (u)) { mark (tileRect (u, true)); mark (tileRect (u, false)); } else mark (dockTile (u));
        }
    }
    void resizedPainted (painted::Look& L)
    {
        const painted::Map m (*this); const float R = L.num ("spatial.ringR", 180);
        topR = m.sq (236, 356, R * 2.0f, R * 2.0f); sideR = m.sq (656, 356, R * 2.0f, R * 2.0f);
        tabletR = m.f (L.rect ("spatial.tablet", { 882, 130, 304, 388 })); dockR = m.f (L.rect ("spatial.tray", { 20, 588, 848, 110 })); hintR = m.r (32, 595, 824, 14);
        onT.setBounds (m.r (892, 140, 138, 26)); outC.setBounds (m.r (1037, 140, 140, 26)); readR = m.f (892, 174, 285, 84);
        autoLabel = m.r (892, 263, 285, 14); for (int i = 0; i < modeBtns.size(); ++i) modeBtns[i]->setBounds (m.r (892.0f + 72.0f * (float) i, 280, 67, 26));
        armBtn.setBounds (m.r (892, 312, 92, 26)); armAll.setBounds (m.r (988.5f, 312, 92, 26)); centreBtn.setBounds (m.r (1085, 312, 92, 26));
        qlLabel = m.r (892, 342, 285, 14); quantBox.setBounds (m.r (892, 358, 139, 25)); loopBox.setBounds (m.r (1038, 358, 139, 25));
        clearTile.setBounds (m.r (892, 389, 139, 26)); clearAll.setBounds (m.r (1038, 389, 139, 26));
        sectLabel = m.r (892, 418, 285, 14); fromBar.setBounds (m.r (892, 434, 139, 25)); toBar.setBounds (m.r (1038, 434, 139, 25));
        sectAll.setBounds (m.r (892, 465, 139, 26)); clearSect.setBounds (m.r (1038, 465, 139, 26)); playR = m.r (892, 495, 285, 16);
        auto prop = [this] (const char* k, juce::Rectangle<float> r) { getProperties().set (k, r.toNearestInt().toString()); };
        prop ("topView", topR); prop ("sideView", sideR); prop ("dock", dockR); prop ("readout", readR);
    }
    void paintPainted (juce::Graphics& g, painted::Look& L)
    {
        const painted::Map m (*this); const float s = m.s(); const auto ink = fxtheme::pal().ink;
        drawViewPainted (g, L, topR, true); drawViewPainted (g, L, sideR, false);
        L.paperText (g, "TOP VIEW", m.f (18, 143, 80, 18), 9.6f * s, false, "tag"); L.paperText (g, "SIDE VIEW", m.f (438, 143, 80, 18), 9.6f * s, false, "tag");
        // the tray: tiles that sit on the head (not placed)
        const auto tray = L.str ("slabs.tray"); L.panel (g, *this, tray, dockR); const bool on = proc.apvts.getRawParameterValue ("fx_spatial_on")->load() >= 0.5f, ready = proc.rack.spatial.ready() || proc.getSampleRate() <= 0;
        const juce::String hint = ! ready ? "SPATIAL works at 44.1, 48, 88.2, 96, 176.4 and 192 kHz. This project runs at " + juce::String (proc.getSampleRate() / 1000.0, 1) + " kHz, so it stays off."
                                 : ! on ? juce::String ("SPATIAL is off: you hear the rack as it is. Switch it on to hear the tiles.")
                                 : shown.empty() ? juce::String ("switch units on in the rack to see their tiles here") : juce::String ("ON THE HEAD (not placed: they sound exactly as in the rack)  -  drag a tile onto a view to place it");
        L.text (g, hint, hintR.toFloat(), juce::Justification::centredLeft, 8.8f * s, ! ready || ! on ? juce::Colour (0xfff7c245) : L.inkOn (tray));
        for (int u : shown) if (! placed (u)) drawPaintedTile (g, L, dockTile (u), u, true, u == sel, poses[(size_t) u], true);
        // the tablet with the selected tile's card
        L.panel (g, *this, L.str ("slabs.tablet"), tabletR); L.nineBody (g, L.str ("slabs.card"), readR);
        auto tx = readR.reduced (10.0f * s, 6.0f * s); const auto dk = L.inkOn (L.str ("slabs.card")), vk = L.colour ("spatial.valueInk", dk);
        L.text (g, sel >= 0 ? juce::String (unitDef (sel).title) : juce::String ("NO TILE SELECTED"), tx.removeFromTop (19.0f * s), juce::Justification::centredLeft, 13.0f * s, L.colour ("text.title", dk), true);
        if (sel >= 0)
        {
            const auto& v = proc.spatialView[(size_t) sel]; const float az = v.az.load(), el = v.el.load(), d = v.dist.load();
            auto line = [&] (const juce::String& k, const juce::String& val) { auto row = tx.removeFromTop (15.0f * s); L.text (g, k, row, juce::Justification::centredLeft, 9.4f * s, dk.withAlpha (0.85f)); L.text (g, val, row, juce::Justification::centredRight, 10.0f * s, vk); };
            line ("ANGLE", d <= 0.0f ? "-" : juce::String (juce::roundToInt (az)) + juce::String::fromUTF8 ("\xc2\xb0") + (az > 1 ? "  right" : az < -1 ? "  left" : "  ahead"));
            line ("ELEVATION", d <= 0.0f ? "-" : juce::String (juce::roundToInt (el)) + juce::String::fromUTF8 ("\xc2\xb0") + (el > 1 ? "  above" : el < -1 ? "  below" : ""));
            line ("DISTANCE", d <= 0.0f ? "on the head" : juce::String (d, 2) + " m");
            L.text (g, v.recording.load() ? "RECORDING" : v.driven.load() ? "PLAYING ITS PATH" : proc.spatialAuto.hasPath (sel) ? "HAS A PATH" : "NO PATH YET", tx.removeFromTop (13.0f * s), juce::Justification::centredLeft, 8.6f * s, v.recording.load() ? juce::Colour (0xffb01f7c) : dk.withAlpha (0.8f));
        }
        L.text (g, "AUTOMATION (memory)", autoLabel.toFloat(), juce::Justification::centredLeft, 8.8f * s, ink);
        L.text (g, "SELECTED TILE: QUANTISE / LOOP", qlLabel.toFloat(), juce::Justification::centredLeft, 8.8f * s, ink);
        L.text (g, "CLEAR SECTION: FROM BAR / TO BAR", sectLabel.toFloat(), juce::Justification::centredLeft, 8.8f * s, ink);
        const double ppq = proc.spatialPpq.load(), bpb = juce::jmax (1.0, proc.lastTempo().beatsPerBar());
        L.text (g, juce::String (proc.spatialPlaying.load() ? "PLAYING  " : "STOPPED  ") + "bar " + juce::String ((int) std::floor (ppq / bpb) + 1) + "." + juce::String ((int) std::floor (std::fmod (juce::jmax (0.0, ppq), bpb)) + 1), playR.toFloat(), juce::Justification::centredLeft, 9.4f * s, ink);
    }
    void drawViewPainted (juce::Graphics& g, painted::Look& L, juce::Rectangle<float> r, bool top)
    {
        const float s = painted::Map (*this).s(); const auto c = r.getCentre();
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        L.draw (g, top ? "spatial/ring-top" : "spatial/ring-side", juce::Rectangle<float> (440.0f * s, 440.0f * s).withCentre (c));
        if (const auto li = L.str ("spatial.listener.img"); li.isNotEmpty()) L.drawAt (g, li, c, s, L.el ("spatial.listener.c", 0, 23), L.el ("spatial.listener.c", 1, 25));   // the listener in the middle (looks whose rings do not have it in the art)
        for (int u : shown)
        {   // recorded paths, under the tiles, in each unit's colour
            const auto& tr = trails[(size_t) u]; if (tr.size() < 2) continue;
            juce::Path p; bool started = false;
            for (auto& pt : tr) { float ux, uy, uz; toVec (pt.az, pt.el, pt.dist, ux, uy, uz); const auto q = project (r, top, ux, uy, uz); if (! started || pt.segStart) { p.startNewSubPath (q); started = true; } else p.lineTo (q); }
            const auto a = painted::Look::accentColour (juce::Colour (unitDef (u).accent));
            g.setColour (a.withAlpha (u == sel ? 0.95f : 0.55f)); g.strokePath (p, juce::PathStrokeType ((u == sel ? 2.6f : 1.8f) * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        std::vector<int> order; for (int u : shown) if (placed (u)) order.push_back (u);
        std::sort (order.begin(), order.end(), [this, top] (int a, int b) { float ax, ay, az, bx, by, bz; toVec (proc.spatialView[(size_t) a].az.load(), proc.spatialView[(size_t) a].el.load(), proc.spatialView[(size_t) a].dist.load(), ax, ay, az); toVec (proc.spatialView[(size_t) b].az.load(), proc.spatialView[(size_t) b].el.load(), proc.spatialView[(size_t) b].dist.load(), bx, by, bz); return top ? ay < by : ax < bx; });
        for (int u : order)
        {
            const auto tr = tileRect (u, top); const auto a = painted::Look::accentColour (juce::Colour (unitDef (u).accent));
            g.setColour (a.withAlpha (0.85f)); g.drawLine (c.x, c.y, tr.getCentreX(), tr.getCentreY(), 1.8f * s);
        }
        for (int u : order)
        {
            const auto tr = tileRect (u, top);
            drawPaintedTile (g, L, tr, u, true, u == sel, poses[(size_t) u], true);
            if (proc.spatialView[(size_t) u].recording.load()) { g.setColour (L.ink()); g.fillEllipse (tr.getRight() - 9.0f, tr.getY() - 2.0f, 10.0f, 10.0f); g.setColour (juce::Colour (0xffed6ec7)); g.fillEllipse (tr.getRight() - 7.5f, tr.getY() - 0.5f, 7.0f, 7.0f); }
            else if (proc.spatialAuto.armed[(size_t) u].load()) { g.setColour (L.ink()); g.drawEllipse (tr.getRight() - 9.0f, tr.getY() - 2.0f, 9.0f, 9.0f, 2.6f); g.setColour (juce::Colour (0xffed6ec7)); g.drawEllipse (tr.getRight() - 9.0f, tr.getY() - 2.0f, 9.0f, 9.0f, 1.4f); }
        }
    }
    void paint (juce::Graphics& g) override
    {
        if (auto* L = painted::active()) { paintPainted (g, *L); return; }
        drawView (g, topR, true); drawView (g, sideR, false);
        // dock: tiles that sit on the head (not placed)
        g.setColour (col::white.withAlpha (0.05f)); g.fillRoundedRectangle (dockR, 8);
        g.setColour (ghost() ? ghosty::glass::ink2() : col::dim.get()); g.setFont (font (9.0f));
        g.drawText (shown.empty() ? "switch units on in the rack to see their tiles here" : "ON THE HEAD (not placed: they sound exactly as in the rack)  -  drag a tile onto a view to place it", dockR.reduced (8, 2).withHeight (12), juce::Justification::centredLeft, false);
        for (int u : shown) if (! placed (u)) drawUnitTile (g, dockTile (u), u, true, u == sel, t, glass);
        // the selected tile's readout (as a binaural panner shows it)
        auto rr = readR;
        drawScreen (g, rr, sel >= 0 ? accentOf (sel) : col::ice.get(), 8.0f);
        auto tx = rr.reduced (10, 6);
        g.setColour (col::white.get()); g.setFont (font (13.0f));
        g.drawText (sel >= 0 ? juce::String (unitDef (sel).title) : juce::String ("NO TILE SELECTED"), tx.removeFromTop (18), juce::Justification::centredLeft, false);
        if (sel >= 0)
        {
            const auto& v = proc.spatialView[(size_t) sel];
            const float az = v.az.load(), el = v.el.load(), d = v.dist.load();
            g.setFont (font (11.0f));
            auto line = [&] (const juce::String& k, const juce::String& val) { auto row = tx.removeFromTop (16); g.setColour (col::dim.get()); g.drawText (k, row, juce::Justification::centredLeft, false); g.setColour (col::white.get()); g.drawText (val, row, juce::Justification::centredRight, false); };
            line ("ANGLE", d <= 0.0f ? "-" : juce::String (juce::roundToInt (az)) + juce::String::fromUTF8 ("\xc2\xb0") + (az > 1 ? "  right" : az < -1 ? "  left" : "  ahead"));
            line ("ELEVATION", d <= 0.0f ? "-" : juce::String (juce::roundToInt (el)) + juce::String::fromUTF8 ("\xc2\xb0") + (el > 1 ? "  above" : el < -1 ? "  below" : ""));
            line ("DISTANCE", d <= 0.0f ? "on the head" : juce::String (d, 2) + " m");
            g.setColour (v.recording.load() ? col::pink.get() : v.driven.load() ? col::slime.get() : col::dim.get()); g.setFont (font (9.5f));
            g.drawText (v.recording.load() ? "RECORDING" : v.driven.load() ? "PLAYING ITS PATH" : proc.spatialAuto.hasPath (sel) ? "HAS A PATH" : "NO PATH YET", tx.removeFromTop (14), juce::Justification::centredLeft, false);
        }
        // section labels
        g.setColour (ghost() ? ghosty::glass::ink2() : col::dim.get()); g.setFont (font (9.0f));
        g.drawText ("AUTOMATION (memory)", autoLabel, juce::Justification::centredLeft, false);
        g.drawText ("SELECTED TILE: QUANTISE / LOOP", qlLabel, juce::Justification::centredLeft, false);
        g.drawText ("CLEAR SECTION: FROM BAR / TO BAR", sectLabel, juce::Justification::centredLeft, false);
        const double ppq = proc.spatialPpq.load(), bpb = juce::jmax (1.0, proc.lastTempo().beatsPerBar());
        g.setColour (proc.spatialPlaying.load() ? col::slime.get() : col::dim.get()); g.setFont (font (10.0f));
        g.drawText (juce::String (proc.spatialPlaying.load() ? "PLAYING  " : "STOPPED  ") + "bar " + juce::String ((int) std::floor (ppq / bpb) + 1) + "." + juce::String ((int) std::floor (std::fmod (juce::jmax (0.0, ppq), bpb)) + 1), playR, juce::Justification::centredLeft, false);
        if (! proc.rack.spatial.ready() && proc.getSampleRate() > 0)
        { g.setColour (col::gold.get()); g.setFont (font (10.0f)); g.drawText ("SPATIAL works at 44.1, 48, 88.2, 96, 176.4 and 192 kHz. This project runs at " + juce::String (proc.getSampleRate() / 1000.0, 1) + " kHz, so it stays off.", hintR, juce::Justification::centredLeft, false); }
        else if (proc.apvts.getRawParameterValue ("fx_spatial_on")->load() < 0.5f)
        { g.setColour (col::gold.get()); g.setFont (font (10.0f)); g.drawText ("SPATIAL is off: you hear the rack as it is. Switch it on to hear the tiles.", hintR, juce::Justification::centredLeft, false); }
    }

    void resized() override
    {
        if (auto* L = painted::active()) { resizedPainted (*L); return; }
        auto r = getLocalBounds().reduced (4);
        auto right = r.removeFromRight (juce::jlimit (250, 300, r.getWidth() / 4)); r.removeFromRight (8);
        dockR = r.removeFromBottom (juce::jlimit (60, 76, r.getHeight() / 6)).toFloat(); r.removeFromBottom (6);
        hintR = r.removeFromBottom (14);
        const float v = (float) juce::jmin ((r.getWidth() - 10) / 2, r.getHeight());
        topR = juce::Rectangle<float> ((float) r.getX(), (float) r.getY(), v, v);
        sideR = juce::Rectangle<float> ((float) r.getX() + v + 10.0f, (float) r.getY(), v, v);
        { const float y = topR.getBottom() + 22.0f; dockR = juce::Rectangle<float> (dockR.getX(), y, dockR.getWidth(), juce::jmin (dockR.getBottom() + 0.0f, y + 84.0f) - y); hintR = juce::Rectangle<int> (r.getX(), (int) topR.getBottom() + 4, r.getWidth(), 14); }
        auto row = [&] (int h) { auto x = right.removeFromTop (h); right.removeFromTop (4); return x; };
        { auto a = row (26); onT.setBounds (a.removeFromLeft (a.getWidth() / 2 - 2)); a.removeFromLeft (4); outC.setBounds (a); }
        readR = row (92).toFloat();
        autoLabel = row (12);
        { auto a = row (26); const int w = a.getWidth() / 4; for (auto* b : modeBtns) b->setBounds (a.removeFromLeft (w).reduced (1, 0)); }
        { auto a = row (26); const int w = a.getWidth() / 3; armBtn.setBounds (a.removeFromLeft (w).reduced (1, 0)); armAll.setBounds (a.removeFromLeft (w).reduced (1, 0)); centreBtn.setBounds (a.reduced (1, 0)); }
        qlLabel = row (12);
        { auto a = row (26); quantBox.setBounds (a.removeFromLeft (a.getWidth() / 2 - 2)); a.removeFromLeft (4); loopBox.setBounds (a); }
        { auto a = row (26); clearTile.setBounds (a.removeFromLeft (a.getWidth() / 2 - 2)); a.removeFromLeft (4); clearAll.setBounds (a); }
        sectLabel = row (12);
        { auto a = row (26); fromBar.setBounds (a.removeFromLeft (a.getWidth() / 2 - 2)); a.removeFromLeft (4); toBar.setBounds (a); }
        { auto a = row (26); sectAll.setBounds (a.removeFromLeft (a.getWidth() / 2 - 2)); a.removeFromLeft (4); clearSect.setBounds (a); }
        playR = row (16);
        auto prop = [this] (const char* k, juce::Rectangle<float> r) { getProperties().set (k, r.toNearestInt().toString()); };   // for the layout check
        prop ("topView", topR); prop ("sideView", sideR); prop ("dock", dockR); prop ("readout", readR);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int u = tileAt (e.position); if (u < 0) return;
        sel = u; refreshControls();
        if (e.getNumberOfClicks() > 1) { setPlace (u, 0.0f, 0.0f, 0.0f, true); return; }
        beginDrag (u); repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drag < 0) return;
        const auto p = e.position;
        float ux, uy, uz; uiVec (drag, ux, uy, uz);   // ux right, uy up, uz ahead; |v| = sqrt (dist / max)
        toDrawn (ux, uy, uz);   // (painted looks: the views' own ring spacing; no change on the drawn looks)
        if (topR.expanded (20).contains (p)) { const float R = topR.getWidth() * 0.5f * sphere(); ux = (p.x - topR.getCentreX()) / R; uz = (topR.getCentreY() - p.y) / R; if (! placed (drag)) uy = 0.0f; }
        else if (sideR.expanded (20).contains (p)) { const float R = sideR.getWidth() * 0.5f * sphere(); uz = (p.x - sideR.getCentreX()) / R; uy = (sideR.getCentreY() - p.y) / R; if (! placed (drag)) ux = 0.0f; }
        else if (dockR.contains (p)) { ux = uy = uz = 0.0f; }
        else return;
        fromDrawn (ux, uy, uz);
        setFromVec (drag, ux, uy, uz);
    }
    void mouseUp (const juce::MouseEvent&) override { endDrag(); repaint(); }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const int u = tileAt (e.position); if (u < 0 || ! placed (u)) return;
        float ux, uy, uz; uiVec (u, ux, uy, uz); const float rho = std::sqrt (ux * ux + uy * uy + uz * uz);
        const float nr = juce::jlimit (0.05f, 1.0f, rho + w.deltaY * 0.15f), k = nr / juce::jmax (1.0e-4f, rho);
        beginDrag (u); setFromVec (u, ux * k, uy * k, uz * k); endDrag();
    }

private:
    static constexpr float kSphere = 0.86f;   // the sphere's radius inside its view
    static float sphere() { return painted::active() != nullptr ? 1.0f : kSphere; }   // (a painted look's view square is the 20 m ring itself)
    // PAINTED: the art's distance rings sit at 30 / 80 / 126 / 180 px (1 / 5 / 10 / 20 m); the tiles follow those rings exactly, so
    // a tile on the "5 m" ring is at 5 m. (The drawn looks use the plain square-root spacing; both are only how the view is drawn.)
    static float ringMap (float rho, bool toArt)
    {
        static const float a[5] { 0.0f, 0.223607f, 0.5f, 0.707107f, 1.0f }, b[5] { 0.0f, 30.0f / 180.0f, 80.0f / 180.0f, 126.0f / 180.0f, 1.0f };
        const float* x = toArt ? a : b; const float* y = toArt ? b : a; rho = juce::jlimit (0.0f, 1.0f, rho);
        for (int i = 0; i < 4; ++i) if (rho <= x[i + 1]) return y[i] + (y[i + 1] - y[i]) * (rho - x[i]) / (x[i + 1] - x[i]);
        return 1.0f;
    }
    static void toDrawn (float& ux, float& uy, float& uz) { if (painted::active() == nullptr) return; const float rho = std::sqrt (ux * ux + uy * uy + uz * uz); if (rho < 1.0e-5f) return; const float k = ringMap (rho, true) / rho; ux *= k; uy *= k; uz *= k; }
    static void fromDrawn (float& ux, float& uy, float& uz) { if (painted::active() == nullptr) return; const float rho = std::sqrt (ux * ux + uy * uy + uz * uz); if (rho < 1.0e-5f) return; const float k = ringMap (juce::jmin (1.0f, rho), false) / rho; ux *= k; uy *= k; uz *= k; }
    juce::uint64 signature() const
    {
        juce::uint64 sig = (juce::uint64) (sel + 2) * 131u + (juce::uint64) (drag + 2) * 17u + (juce::uint64) proc.spatialAuto.mode.load() + (proc.spatialPlaying.load() ? 64u : 0u) + (proc.apvts.getRawParameterValue ("fx_spatial_on")->load() >= 0.5f ? 128u : 0u) + (juce::uint64) (pathVersion + 3) * 7919u;
        const double bpb = juce::jmax (1.0, proc.lastTempo().beatsPerBar()); sig = sig * 1099511628211ull + (juce::uint64) (juce::int64) std::floor (proc.spatialPpq.load() / bpb * 4.0);
        for (int u : shown) { const auto& v = proc.spatialView[(size_t) u]; float ux, uy, uz; if (drag == u) uiVec (u, ux, uy, uz); else toVec (v.az.load(), v.el.load(), v.dist.load(), ux, uy, uz);
            sig = sig * 1099511628211ull + (juce::uint64) (juce::int64) (u * 4 + (v.recording.load() ? 1 : 0) + (v.driven.load() ? 2 : 0) + (proc.spatialAuto.armed[(size_t) u].load() ? 4096 : 0)) + (juce::uint64) (juce::int64) (ux * 700.0f) * 31u + (juce::uint64) (juce::int64) (uy * 700.0f) * 1013u + (juce::uint64) (juce::int64) (uz * 700.0f) * 7u; }
        return sig;
    }
    void timerCallback() override
    {
        t += 1.0 / 30.0;
        std::vector<int> now; for (int u = 0; u < kNumUnits; ++u) if (spatialEligible (u) && proc.apvts.getRawParameterValue (PhantomProcessor::pid (u, "on"))->load() >= 0.5f) now.push_back (u);
        if (now != shown) { shown = now; if (sel >= 0 && std::find (shown.begin(), shown.end(), sel) == shown.end()) sel = shown.empty() ? -1 : shown.front(); if (sel < 0 && ! shown.empty()) sel = shown.front(); refreshControls(); }
        if (const int v = proc.spatialAuto.version.load(); v != pathVersion) { pathVersion = v; for (int u = 0; u < kNumUnits; ++u) trails[(size_t) u] = spatialEligible (u) ? proc.spatialAuto.copyPath (u) : std::vector<SpPoint> {}; }
        const int m = proc.spatialAuto.mode.load(); if (! modeBtns[m]->getToggleState()) modeBtns[m]->setToggleState (true, juce::dontSendNotification);
        if (painted::active() != nullptr) { if (! isShowing() && ! getProperties().contains ("pfxAlways")) return; const auto sig = signature(); if (sig == lastSig) return; lastSig = sig; }   // PAINTED: repaint only when something moved
        repaint();
    }
    void refreshControls()
    {
        const bool has = sel >= 0;
        armBtn.setEnabled (has); centreBtn.setEnabled (has); clearTile.setEnabled (has); quantBox.setEnabled (has); loopBox.setEnabled (has);
        armBtn.setToggleState (has && proc.spatialAuto.armed[(size_t) sel].load(), juce::dontSendNotification);
        quantBox.setSelectedId (has ? proc.spatialAuto.quant[(size_t) sel].load() + 1 : 1, juce::dontSendNotification);
        loopBox.setSelectedId (has ? proc.spatialAuto.loop[(size_t) sel].load() + 1 : 1, juce::dontSendNotification);
    }
    bool placed (int u) const { return proc.spatialView[(size_t) u].dist.load() > 0.0f || (drag == u && dragPlaced); }
    // UI space: the place as a vector inside the unit sphere (radius = sqrt of distance / max: near positions get more room)
    static void toVec (float az, float el, float d, float& ux, float& uy, float& uz)
    {
        const float rho = std::sqrt (juce::jlimit (0.0f, 1.0f, d / kSpatialMaxDistance)), a = juce::degreesToRadians (az), e = juce::degreesToRadians (el);
        ux = rho * std::sin (a) * std::cos (e); uy = rho * std::sin (e); uz = rho * std::cos (a) * std::cos (e);
    }
    void uiVec (int u, float& ux, float& uy, float& uz) const
    {
        auto* pa = proc.apvts.getRawParameterValue (PhantomProcessor::spid (u, "azimuth")); auto* pe = proc.apvts.getRawParameterValue (PhantomProcessor::spid (u, "elevation")); auto* pd = proc.apvts.getRawParameterValue (PhantomProcessor::spid (u, "distance"));
        toVec (pa->load(), pe->load(), pd->load(), ux, uy, uz);
    }
    void setFromVec (int u, float ux, float uy, float uz)
    {
        float rho = std::sqrt (ux * ux + uy * uy + uz * uz);
        if (rho > 1.0f) { ux /= rho; uy /= rho; uz /= rho; rho = 1.0f; }
        if (rho < 0.04f) { setPlace (u, 0.0f, 0.0f, 0.0f, false); dragPlaced = false; return; }
        const float az = juce::radiansToDegrees (std::atan2 (ux, uz)), el = juce::radiansToDegrees (std::asin (juce::jlimit (-1.0f, 1.0f, uy / rho)));
        dragPlaced = true; setPlace (u, az, el, rho * rho * kSpatialMaxDistance, false);
    }
    void setPlace (int u, float az, float el, float d, bool gesture)
    {
        const char* ids[] { "azimuth", "elevation", "distance" }; const float vals[] { az, el, d };
        for (int i = 0; i < 3; ++i) if (auto* p = proc.apvts.getParameter (PhantomProcessor::spid (u, ids[i])))
        { if (gesture) p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (vals[i])); if (gesture) p->endChangeGesture(); }
        repaint();
    }
    void beginDrag (int u)
    {
        endDrag(); drag = u; dragPlaced = proc.spatialView[(size_t) u].dist.load() > 0.0f; proc.spatialAuto.touching[(size_t) u] = true;
        for (auto id : { "azimuth", "elevation", "distance" }) if (auto* p = proc.apvts.getParameter (PhantomProcessor::spid (u, id))) p->beginChangeGesture();
    }
    void endDrag()
    {
        if (drag < 0) return;
        for (auto id : { "azimuth", "elevation", "distance" }) if (auto* p = proc.apvts.getParameter (PhantomProcessor::spid (drag, id))) p->endChangeGesture();
        proc.spatialAuto.touching[(size_t) drag] = false; drag = -1;
    }
    // where a tile is drawn in a view (top: x right, y ahead; side: x ahead, y up)
    juce::Point<float> project (const juce::Rectangle<float>& r, bool top, float ux, float uy, float uz) const
    {
        const float R = r.getWidth() * 0.5f * sphere(); toDrawn (ux, uy, uz);
        return top ? juce::Point<float> (r.getCentreX() + ux * R, r.getCentreY() - uz * R) : juce::Point<float> (r.getCentreX() + uz * R, r.getCentreY() - uy * R);
    }
    float tileSize() const { if (painted::active() != nullptr) return 46.0f * painted::Map (*this).s(); return juce::jlimit (34.0f, 52.0f, topR.getWidth() * 0.11f); }
    juce::Rectangle<float> tileRect (int u, bool top) const
    {
        const auto& v = proc.spatialView[(size_t) u]; float ux, uy, uz; toVec (v.az.load(), v.el.load(), v.dist.load(), ux, uy, uz);
        if (drag == u) uiVec (u, ux, uy, uz);
        const float s = tileSize(); return juce::Rectangle<float> (s, s * 1.08f).withCentre (project (top ? topR : sideR, top, ux, uy, uz));
    }
    juce::Rectangle<float> dockTile (int u) const
    {
        int k = 0; for (int x : shown) { if (x == u) break; if (! placed (x)) ++k; }
        if (painted::active() != nullptr) { const painted::Map m (*this); const auto c = dockR.getTopLeft() + juce::Point<float> ((48.0f + 78.0f * (float) k) * m.sx, dockR.getHeight() * 0.59f); return juce::Rectangle<float> (58.0f * m.s(), 60.0f * m.s()).withCentre (c); }
        const float h = dockR.getHeight() - 18.0f, w = h * 0.95f;
        return { dockR.getX() + 8.0f + k * (w + 6.0f), dockR.getY() + 15.0f, w, h };
    }
    int tileAt (juce::Point<float> p) const
    {
        for (auto it = shown.rbegin(); it != shown.rend(); ++it)
        {
            const int u = *it;
            if (placed (u)) { if (tileRect (u, true).contains (p) || tileRect (u, false).contains (p)) return u; }
            else if (dockTile (u).contains (p)) return u;
        }
        return -1;
    }
    void drawView (juce::Graphics& g, juce::Rectangle<float> r, bool top)
    {
        drawScreen (g, r, col::ice.get(), 10.0f);
        const auto c = r.getCentre(); const float R = r.getWidth() * 0.5f * kSphere;
        // distance rings: 1, 5, 10, 20 m
        for (float m : { 1.0f, 5.0f, 10.0f, 20.0f })
        {
            const float rr = R * std::sqrt (m / kSpatialMaxDistance);
            g.setColour (col::ice.withAlpha (m == 20.0f ? 0.45f : 0.16f)); g.drawEllipse (c.x - rr, c.y - rr, rr * 2, rr * 2, m == 20.0f ? 1.4f : 1.0f);
            g.setColour (col::dim.withAlpha (0.7f)); g.setFont (font (8.0f)); g.drawText (juce::String ((int) m) + " m", juce::Rectangle<float> (c.x + rr * 0.71f + 2, c.y + rr * 0.71f - 2, 34, 10), juce::Justification::centredLeft, false);
        }
        g.setColour (col::white.withAlpha (0.08f)); g.drawLine (c.x - R, c.y, c.x + R, c.y); g.drawLine (c.x, c.y - R, c.x, c.y + R);
        g.setColour (ghost() ? ghosty::glass::ink() : col::dim.get()); g.setFont (font (9.5f));
        if (top) { g.drawText ("FRONT", juce::Rectangle<float> (c.x - 40, r.getY() + 3, 80, 12), juce::Justification::centred); g.drawText ("BACK", juce::Rectangle<float> (c.x - 40, r.getBottom() - 15, 80, 12), juce::Justification::centred);
                   g.drawText ("L", juce::Rectangle<float> (r.getX() + 4, c.y - 6, 14, 12), juce::Justification::centred); g.drawText ("R", juce::Rectangle<float> (r.getRight() - 18, c.y - 6, 14, 12), juce::Justification::centred); }
        else { g.drawText ("ABOVE", juce::Rectangle<float> (c.x - 40, r.getY() + 3, 80, 12), juce::Justification::centred); g.drawText ("BELOW", juce::Rectangle<float> (c.x - 40, r.getBottom() - 15, 80, 12), juce::Justification::centred);
               g.drawText ("BACK", juce::Rectangle<float> (r.getX() + 4, c.y - 6, 40, 12), juce::Justification::centredLeft); g.drawText ("FRONT", juce::Rectangle<float> (r.getRight() - 44, c.y - 6, 40, 12), juce::Justification::centredRight); }
        g.setColour (col::white.get()); g.setFont (font (11.0f)); g.drawText (top ? "TOP VIEW" : "SIDE VIEW", r.reduced (10, 6).withHeight (14), juce::Justification::centredLeft, false);
        // the head: from above (nose ahead, ears left / right) or from the side (nose to the right)
        const float h = R * 0.13f; g.setColour (col::white.withAlpha (0.85f));
        juce::Path head; head.addEllipse (c.x - h, c.y - h, h * 2, h * 2);
        if (top) { head.addTriangle (c.x - h * 0.3f, c.y - h * 0.95f, c.x + h * 0.3f, c.y - h * 0.95f, c.x, c.y - h * 1.35f); head.addEllipse (c.x - h * 1.18f, c.y - h * 0.3f, h * 0.3f, h * 0.6f); head.addEllipse (c.x + h * 0.88f, c.y - h * 0.3f, h * 0.3f, h * 0.6f); }
        else { head.addTriangle (c.x + h * 0.95f, c.y - h * 0.3f, c.x + h * 0.95f, c.y + h * 0.2f, c.x + h * 1.35f, c.y); }
        g.strokePath (head, juce::PathStrokeType (1.6f)); g.setColour (col::ice.withAlpha (0.18f)); g.fillPath (head);
        // trails (recorded paths), faint, in each tile's colour
        for (int u : shown)
        {
            const auto& tr = trails[(size_t) u]; if (tr.size() < 2) continue;
            juce::Path p; bool started = false;
            for (auto& pt : tr) { float ux, uy, uz; toVec (pt.az, pt.el, pt.dist, ux, uy, uz); const auto q = project (r, top, ux, uy, uz); if (! started || pt.segStart) { p.startNewSubPath (q); started = true; } else p.lineTo (q); }
            g.setColour (accentOf (u).withAlpha (u == sel ? 0.55f : 0.25f)); g.strokePath (p, juce::PathStrokeType (u == sel ? 1.8f : 1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        // the placed tiles, furthest first
        std::vector<int> order; for (int u : shown) if (placed (u)) order.push_back (u);
        std::sort (order.begin(), order.end(), [this, top] (int a, int b) { float ax, ay, az, bx, by, bz; toVec (proc.spatialView[(size_t) a].az.load(), proc.spatialView[(size_t) a].el.load(), proc.spatialView[(size_t) a].dist.load(), ax, ay, az); toVec (proc.spatialView[(size_t) b].az.load(), proc.spatialView[(size_t) b].el.load(), proc.spatialView[(size_t) b].dist.load(), bx, by, bz); return top ? ay < by : ax < bx; });
        for (int u : order)
        {
            const auto tr = tileRect (u, top);
            g.setColour (accentOf (u).withAlpha (0.35f)); g.drawLine (c.x, c.y, tr.getCentreX(), tr.getCentreY(), 1.0f);
            drawUnitTile (g, tr, u, true, u == sel, t, glass);
            if (proc.spatialView[(size_t) u].recording.load()) glowDot (g, { tr.getRight() - 4, tr.getY() + 4 }, 3.5f, col::pink.get());
            else if (proc.spatialAuto.armed[(size_t) u].load()) { g.setColour (col::pink.withAlpha (0.8f)); g.drawEllipse (tr.getRight() - 8, tr.getY(), 8, 8, 1.4f); }
        }
    }

    PhantomProcessor& proc;
    Toggle onT; Choice outC;
    juce::OwnedArray<juce::TextButton> modeBtns;
    juce::TextButton armBtn { "ARM" }, armAll { "ARM ALL" }, centreBtn { "CENTRE" }, clearTile { "CLEAR TILE" }, clearAll { "CLEAR ALL" }, clearSect { "CLEAR" }, sectAll { "ALL TILES" };
    juce::Slider fromBar, toBar; juce::ComboBox quantBox, loopBox;
    juce::Rectangle<float> topR, sideR, dockR, readR, tabletR; std::array<int, kNumUnits> poses {}, trig {}; std::array<float, kNumUnits> slow {}; juce::uint64 lastSig = 0; juce::Rectangle<int> autoLabel, qlLabel, sectLabel, playR, hintR;
    std::vector<int> shown; int sel = -1, drag = -1, pathVersion = -1; bool dragPlaced = false; double t = 0;
    std::array<std::vector<SpPoint>, kNumUnits> trails;
    ghosty::glass::PanelCache glass;
};
} // namespace phantom
