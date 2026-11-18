// FX RACK (round 3) pages, Ghosty Pro only: DISTORTION (curve view + X-SHAPER editor). Included by FxPanel.cpp.

struct TipSegmented : public Segmented, public juce::SettableTooltipClient { using Segmented::Segmented; };

// ---------------------------------------------------------------- X-SHAPER: draw the two curves
class XShaperEditor : public juce::Component, public juce::SettableTooltipClient
{
public:
    XShaperEditor (Rack::Impl& r) : rack (r) { setTooltip ("Draw the curve: drag across the points. A = the sound at DRIVE 0, B = at full DRIVE (in between they morph)"); }
    int editing = 0;   // 0 = A, 1 = B
    juce::Rectangle<float> plot() const { auto b = getLocalBounds().toFloat().reduced (12, 10).withTrimmedTop (12).withTrimmedBottom (12); const float s = juce::jmin (b.getWidth(), b.getHeight()); return b.withSizeKeepingCentre (s * 1.25f > b.getWidth() ? b.getWidth() : s * 1.25f, s); }
    juce::Point<float> toScreen (double x, double y) const { auto p = plot(); return { p.getX() + (float) ((x + 1.0) * 0.5) * p.getWidth(), p.getCentreY() - (float) y * p.getHeight() * 0.5f }; }
    void paint (juce::Graphics& g) override
    {
        const auto acc = accentOf (uDist);
        const auto c = rack.distCurves.get();
        auto p = plot();
        g.setColour (col::white.withAlpha (0.06f));
        for (int k = 0; k <= 4; ++k) { g.drawVerticalLine ((int) (p.getX() + p.getWidth() * (float) k / 4.0f), p.getY(), p.getBottom()); g.drawHorizontalLine ((int) (p.getY() + p.getHeight() * (float) k / 4.0f), p.getX(), p.getRight()); }
        g.setColour (col::white.withAlpha (0.12f)); g.drawLine (p.getX(), p.getBottom(), p.getRight(), p.getY(), 1.0f);   // the straight line (no change)
        DistTables t; t.make (c);
        const float drv = rack.tm.distDrive.load();
        const double m = juce::jlimit (0.0, 1.0, (double) drv / 36.0);
        for (int which = 0; which < 3; ++which)
        {   // B, A, then the morph you hear
            juce::Path path;
            for (int i = 0; i <= 160; ++i)
            {
                const double x = -1.0 + 2.0 * i / 160.0;
                const double y = which == 0 ? DistTables::f (t.TB, x) : which == 1 ? DistTables::f (t.TA, x) : (1.0 - m) * DistTables::f (t.TA, x) + m * DistTables::f (t.TB, x);
                const auto q = toScreen (x, y);
                if (i == 0) path.startNewSubPath (q); else path.lineTo (q);
            }
            const bool sel = (which == 0 && editing == 1) || (which == 1 && editing == 0);
            const auto cc = which == 2 ? col::white : which == 0 ? col::ice : acc;
            if (which == 2) glowStroke (g, path, cc.withAlpha (0.85f), 1.6f, 0.6f);
            else glowStroke (g, path, cc.withAlpha (sel ? 0.95f : 0.35f), sel ? 2.2f : 1.2f, sel ? 1.0f : 0.0f);
        }
        const auto& pts = editing == 0 ? c.a : c.b;
        for (int k = 0; k < DistCurves::kPts; ++k)
        {
            if (c.symmetric && k <= DistCurves::kPts / 2) continue;
            const double x = -1.0 + 2.0 * k / (DistCurves::kPts - 1);
            glowDot (g, toScreen (x, pts[(size_t) k]), k == hover ? 4.5f : 3.0f, editing == 0 ? acc : col::ice);
        }
        drawCaption (g, getLocalBounds().toFloat().reduced (12, 6).removeFromTop (14), juce::String ("EDITING ") + (editing == 0 ? "A (DRIVE 0)" : "B (FULL DRIVE)") + "   -   white = what you hear now",
                     col::dim.brighter (0.2f), 10.0f);
        drawCaption (g, getLocalBounds().toFloat().reduced (12, 6).removeFromBottom (14), c.symmetric ? "SYMMETRIC: draw the right half, the left mirrors it" : "ASYMMETRIC: both halves free (even harmonics)", col::dim, 9.5f);
    }
    int pointAt (float sx) const
    {
        auto p = plot(); const double x = juce::jlimit (-1.0, 1.0, (double) ((sx - p.getX()) / p.getWidth()) * 2.0 - 1.0);
        return juce::jlimit (0, DistCurves::kPts - 1, (int) std::lround ((x + 1.0) * 0.5 * (DistCurves::kPts - 1)));
    }
    void mouseMove (const juce::MouseEvent& e) override { const int h = pointAt (e.position.x); if (h != hover) { hover = h; repaint(); } }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override { rack.pushUndo(); lastK = -1; paintAt (e.position); }
    void mouseDrag (const juce::MouseEvent& e) override { paintAt (e.position); }
    void mouseDoubleClick (const juce::MouseEvent&) override
    {   // double-click: back to this curve's factory shape
        auto c = rack.distCurves.get(); const auto d = DistCurves::defaults();
        (editing == 0 ? c.a : c.b) = editing == 0 ? d.a : d.b; rack.distCurves.set (c); rack.dataVersion.fetch_add (1); repaint();
    }
private:
    void paintAt (juce::Point<float> pos)
    {
        auto c = rack.distCurves.get();
        auto& pts = editing == 0 ? c.a : c.b;
        constexpr int mid = DistCurves::kPts / 2;
        int k = pointAt (pos.x);
        auto p = plot(); float y = clampf ((p.getCentreY() - pos.y) / (p.getHeight() * 0.5f), -1.0f, 1.0f);
        if (c.symmetric && k <= mid) { if (k == mid) return; k = 2 * mid - k; y = -y; }   // the left half draws its mirror
        // drawing fast across several points fills the ones in between
        if (lastK < 0 || lastK == k) pts[(size_t) k] = y;
        else
            for (int j = juce::jmin (lastK, k); j <= juce::jmax (lastK, k); ++j)
                pts[(size_t) j] = clampf (lastY + (float) (j - lastK) / (float) (k - lastK) * (y - lastY), -1.0f, 1.0f);
        lastK = k; lastY = y;
        rack.distCurves.set (c); rack.dataVersion.fetch_add (1); repaint();
    }
    Rack::Impl& rack; int hover = -1, lastK = -1; float lastY = 0;
};

// ---------------------------------------------------------------- DISTORTION
class DistPage : public Page
{
public:
    DistPage (juce::AudioProcessorValueTreeState& s, Rack::Impl& r) : apvts (s), rack (r), mode (s, pid (uDist, "mode"), accentOf (uDist)),
        fpos (s, pid (uDist, "fpos"), accentOf (uDist)), xs (r)
    {
        const auto acc = accentOf (uDist);
        mode.columns = 5;
        addAndMakeVisible (mode); addAndMakeVisible (fpos); addChildComponent (xs);
        struct K { const char* id; const char* cap; const char* tip; };
        static const K ks[] { { "drive", "DRIVE", "How hard the signal hits the curve (0 .. 36 dB). AUTO GAIN keeps the level in check: it gets louder only half as fast as it gets denser. X-SHAPER: morphs curve A into B" },
                              { "level", "LEVEL", "Output level after the auto gain" },
                              { "ftype", "TYPE", "The filter's type, morphing LOW PASS -> BAND PASS -> HIGH PASS" },
                              { "freq", "FREQ", "The filter's frequency" },
                              { "q", "Q", "The filter's resonance" },
                              { "fkey", "KEY", "Key tracking: at 100 % the filter follows the last played note one to one (C4 = FREQ)" } };
        for (auto& k : ks) { auto* kn = knobs.add (new Knob (s, pid (uDist, k.id), k.cap, acc)); kn->slider.setTooltip (k.tip); addAndMakeVisible (kn); }
        mode.setTooltip ("The curve. Every one runs 4x oversampled with anti-aliasing (DOWNSAMPLE is a deliberate sample-and-hold)");
        fpos.setTooltip ("The filter: OFF, PRE (shapes what goes into the curve: the distortion follows it) or POST (tames what comes out)");
        for (auto* b : { &editA, &editB, &symB }) { addChildComponent (*b); b->setColour (juce::TextButton::buttonOnColourId, b == &editB ? col::ice : acc); }
        editA.setClickingTogglesState (false); editB.setClickingTogglesState (false);
        editA.onClick = [this] { xs.editing = 0; refreshX(); }; editB.onClick = [this] { xs.editing = 1; refreshX(); };
        editA.setTooltip ("Draw curve A (what DRIVE 0 sounds like)"); editB.setTooltip ("Draw curve B (what full DRIVE sounds like)");
        symB.setClickingTogglesState (true); symB.setTooltip ("SYMMETRIC: one half drawn, the other mirrored (odd harmonics). Off: both halves free (asymmetric, even harmonics too)");
        symB.onClick = [this] { rack.pushUndo(); auto c = rack.distCurves.get(); c.symmetric = symB.getToggleState(); rack.distCurves.set (c); rack.dataVersion.fetch_add (1); xs.repaint(); };
        mode.onChange = [this] (int) { refreshX(); };
        refreshX();
    }
    void refreshX()
    {
        const bool x = (int) std::lround (getParam (apvts, pid (uDist, "mode"))) == dmXShaper;
        xs.setVisible (x); editA.setVisible (x); editB.setVisible (x); symB.setVisible (x);
        editA.setToggleState (xs.editing == 0, juce::dontSendNotification); editB.setToggleState (xs.editing == 1, juce::dontSendNotification);
        symB.setToggleState (rack.distCurves.get().symmetric, juce::dontSendNotification);
        knobs[0]->caption = x ? "A -> B" : "DRIVE"; knobs[0]->repaint();
        xs.repaint(); repaint();
    }
    void resized() override
    {
        auto r = getLocalBounds();
        mode.setBounds (r.removeFromTop (52)); r.removeFromTop (2);
        blurb = r.removeFromTop (16);
        r.removeFromTop (4);
        auto right = r.removeFromRight (3 * 88 + 10).withTrimmedLeft (10);
        auto top = right.removeFromTop (juce::jmin (100, right.getHeight() / 3));
        gridLayout (top, { knobs[0], knobs[1] }, 2, top.getHeight());
        right.removeFromTop (6);
        fcap = right.removeFromTop (14);
        fpos.setBounds (right.removeFromTop (28)); right.removeFromTop (4);
        gridLayout (right, { knobs[2], knobs[3], knobs[4], knobs[5] }, 2, juce::jmin (90, right.getHeight() / 2));
        vis = r;
        auto bar = vis.removeFromBottom (28).reduced (4, 2);
        editA.setBounds (bar.removeFromLeft (70)); bar.removeFromLeft (4); editB.setBounds (bar.removeFromLeft (70)); bar.removeFromLeft (8);
        symB.setBounds (bar.removeFromLeft (100));
        xs.setBounds (vis);
    }
    void paint (juce::Graphics& g) override
    {
        const auto acc = accentOf (uDist);
        const int md = juce::jlimit (0, (int) kNumDistModes - 1, (int) std::lround (getParam (apvts, pid (uDist, "mode"))));
        drawCaption (g, blurb.toFloat(), distModeNames()[md] + ":  " + distModeBlurb (md), col::dim.brighter (0.25f), 10.5f);
        drawCaption (g, fcap.toFloat(), "FILTER", col::dim.brighter (0.25f), 10.0f);
        drawScreen (g, vis.toFloat(), acc);
        if (md == dmXShaper) return;   // the editor draws itself
        auto p = vis.toFloat().reduced (16, 14);
        const float s = juce::jmin (p.getWidth(), p.getHeight()); p = p.withSizeKeepingCentre (juce::jmin (p.getWidth(), s * 1.4f), s);
        auto toS = [&] (double x, double y) { return juce::Point<float> (p.getX() + (float) ((x + 1.0) * 0.5) * p.getWidth(), p.getCentreY() - (float) juce::jlimit (-1.1, 1.1, y) * p.getHeight() * 0.5f); };
        g.setColour (col::white.withAlpha (0.06f));
        for (int k = 0; k <= 4; ++k) { g.drawVerticalLine ((int) (p.getX() + p.getWidth() * (float) k / 4.0f), p.getY(), p.getBottom()); g.drawHorizontalLine ((int) (p.getY() + p.getHeight() * (float) k / 4.0f), p.getX(), p.getRight()); }
        const float drv = getParam (apvts, pid (uDist, "drive"));
        juce::Path path;
        for (int i = 0; i <= 200; ++i)
        {
            const double x = -1.0 + 2.0 * i / 200.0;
            double y = DistUnit::curveAt (md, x, drv, nullptr);
            if (md == dmDownsample) { const double st = 2.0 / (2.0 + 30.0 * drv / 36.0); y = std::round (x / st) * st; }   // (a step picture of the rate reduction)
            const auto q = toS (x, y); if (i == 0) path.startNewSubPath (q); else path.lineTo (q);
        }
        glowStroke (g, path, acc, 2.0f);
        // the input level: a ghost riding the curve
        const float in = clampf (rack.tm.distIn.load() * dbToGain (0.0f), 0.0f, 1.0f);
        const auto q = toS (in, DistUnit::curveAt (md, in, drv, nullptr));
        ghosty::drawGhost (g, ghosty::lookFor (unitInfo (uDist).look), q.x, q.y - 12.0f, 18.0f + 10.0f * in, md == dmDownsample ? 0.4f : 0.0f, false, 0.5f + 0.5f * in);
        drawCaption (g, vis.toFloat().reduced (12, 6).removeFromTop (14), juce::String ("DRIVE ") + juce::String (drv, 1) + " dB   -   in " + juce::String (gainToDb (rack.tm.distIn.load() + 1e-6f), 1)
                     + " dB, out " + juce::String (gainToDb (rack.tm.distOut.load() + 1e-6f), 1) + " dB", col::dim.brighter (0.2f), 10.0f);
    }
    void tick (double) override { repaint (vis); if (xs.isVisible()) xs.repaint(); }
private:
    juce::AudioProcessorValueTreeState& apvts; Rack::Impl& rack;
    TipSegmented mode, fpos; XShaperEditor xs;
    juce::OwnedArray<Knob> knobs; juce::TextButton editA { "DRAW A" }, editB { "DRAW B" }, symB { "SYMMETRIC" };
    juce::Rectangle<int> vis, blurb, fcap;
};

// ---------------------------------------------------------------- COMPRESSOR
class CompPage : public Page
{
public:
    CompPage (juce::AudioProcessorValueTreeState& s, Rack::Impl& r) : apvts (s), rack (r), mode (s, pid (uComp, "mode"), accentOf (uComp)), keyC (s, pid (uComp, "key"), accentOf (uComp))
    {
        const auto acc = accentOf (uComp);
        addAndMakeVisible (mode); addAndMakeVisible (keyC);
        mode.setTooltip ("SINGLE: one compressor. MULTIBAND: three bands (low / mid / high), each compressed down and / or up on its own");
        keyC.setTooltip ("What the compressor listens to. UNIT INPUT: the sound arriving at it. RACK INPUT: the dry synth before any effect, so echoes and reverbs after it duck to your playing, not to their own tails");
        struct K { const char* id; const char* cap; const char* tip; };
        static const K ks[] { { "thresh", "THRESH", "Level above which the sound is turned down (and, in MULTIBAND with UP, below which quiet parts are lifted). In LIMIT: the ceiling" },
                              { "ratio", "RATIO", "How hard: 2 : 1 gentle, 8 : 1 strong; all the way up = LIMIT, a true-peak limiter that never lets a peak past THRESH" },
                              { "attack", "ATTACK", "How fast it clamps down (LIMIT: the look-ahead window, 0 .. 10 ms)" },
                              { "release", "RELEASE", "How fast it lets go" },
                              { "makeup", "MAKEUP", "Gain after the compression (LIMIT: the drive into the ceiling), 0 .. 36 dB" },
                              { "knee", "KNEE", "How softly it starts around the threshold" } };
        for (auto& k : ks) { auto* kn = knobs.add (new Knob (s, pid (uComp, k.id), k.cap, acc)); kn->slider.setTooltip (k.tip); addAndMakeVisible (kn); }
        static const char* bandCap[] { "LOW", "MID", "HIGH" };
        for (int b = 0; b < 3; ++b)
            for (auto* id : { "thr", "down", "up", "gain" })
            {
                const juce::String pidS = pid (uComp, (juce::String ("b") + juce::String (b + 1) + "_" + id).toRawUTF8());
                auto* kn = bandKnobs.add (new Knob (s, pidS, juce::String (id) == "thr" ? "THR" : juce::String (id) == "gain" ? "LEVEL" : juce::String (id).toUpperCase(), b == 0 ? col::pink : b == 1 ? acc : col::ice));
                kn->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 14);
                kn->slider.setTooltip (juce::String (bandCap[b]) + (juce::String (id) == "thr" ? " band: threshold offset from THRESH" : juce::String (id) == "down" ? " band: how much it is turned down above the threshold"
                                       : juce::String (id) == "up" ? " band: how much quiet parts are lifted towards the threshold (upward compression, at most 18 dB)" : " band: level"));
                addChildComponent (kn);
            }
        for (auto* id : { "xlo", "xhi" })
        {
            auto* kn = xKnobs.add (new Knob (s, pid (uComp, id), juce::String (id) == "xlo" ? "LOW X" : "HIGH X", acc));
            kn->slider.setTooltip (juce::String (id) == "xlo" ? "Crossover between the LOW and MID bands" : "Crossover between the MID and HIGH bands");
            addChildComponent (kn);
        }
        mode.onChange = [this] (int) { refresh(); };
        refresh();
    }
    void refresh()
    {
        const bool multi = (int) std::lround (getParam (apvts, pid (uComp, "mode"))) == 1;
        for (auto* k : bandKnobs) k->setVisible (multi);
        for (auto* k : xKnobs) k->setVisible (multi);
        resized(); repaint();
    }
    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop (32);
        mode.setBounds (top.removeFromLeft (260).reduced (0, 1)); top.removeFromLeft (12);
        keyCap = top.removeFromLeft (40); keyC.setBounds (top.removeFromLeft (160).reduced (0, 3));
        r.removeFromTop (6);
        const bool multi = bandKnobs[0]->isVisible();
        if (multi)
        {
            auto bands = r.removeFromRight (juce::jmin (420, r.getWidth() / 2)).withTrimmedLeft (8);
            bandArea = bands;
            auto xr = bands.removeFromBottom (juce::jmin (84, bands.getHeight() / 3));
            gridLayout (xr.withTrimmedLeft (bands.getWidth() / 4).withTrimmedRight (bands.getWidth() / 4), { xKnobs[0], xKnobs[1] }, 2, xr.getHeight());
            bands.removeFromTop (16);
            const int cw = bands.getWidth() / 3;
            for (int b = 0; b < 3; ++b)
            {
                auto col = bands.removeFromLeft (cw).reduced (3, 0);
                std::vector<juce::Component*> items; for (int k = 0; k < 4; ++k) items.push_back (bandKnobs[b * 4 + k]);
                gridLayout (col, items, 2, juce::jmin (78, col.getHeight() / 2));
            }
        }
        else bandArea = {};
        auto kn = r.removeFromRight (3 * 84 + 6).withTrimmedLeft (6);
        gridLayout (kn, { knobs[0], knobs[1], knobs[2], knobs[3], knobs[4], knobs[5] }, 3, juce::jmin (96, kn.getHeight() / 2));
        vis = r;
    }
    void paint (juce::Graphics& g) override
    {
        const auto acc = accentOf (uComp);
        drawCaption (g, keyCap.toFloat(), "KEY", col::dim.brighter (0.25f), 10.0f);
        if (! bandArea.isEmpty())
        {
            static const char* bandCap[] { "LOW", "MID", "HIGH" };
            const int cw = bandArea.getWidth() / 3;
            for (int b = 0; b < 3; ++b)
            {
                auto cr = juce::Rectangle<float> ((float) (bandArea.getX() + b * cw), (float) bandArea.getY(), (float) cw, 14.0f);
                const float gr = rack.tm.compBandGr[(size_t) b].load();
                drawCaption (g, cr.reduced (6, 0), juce::String (bandCap[b]) + "  " + (gr >= 0 ? "+" : "") + juce::String (gr, 1) + " dB", b == 0 ? col::pink : b == 1 ? acc : col::ice, 10.0f);
            }
        }
        drawScreen (g, vis.toFloat(), acc);
        auto v = vis.toFloat().reduced (12, 10);
        auto meter = v.removeFromRight (v.getWidth() > 260 ? 54.0f : 30.0f); v.removeFromRight (10);
        // the static curve: input dB (x) -> output dB (y), -60 .. 0
        const float thr = getParam (apvts, pid (uComp, "thresh")), rat = getParam (apvts, pid (uComp, "ratio")), kn = getParam (apvts, pid (uComp, "knee")), mk = getParam (apvts, pid (uComp, "makeup"));
        const bool lim = rat >= kCompRatioMax - 0.01f;
        const float s = juce::jmin (v.getWidth(), v.getHeight()); auto p = v.withSizeKeepingCentre (s, s);
        auto toS = [&] (float xi, float yo) { return juce::Point<float> (p.getX() + (xi + 60.0f) / 60.0f * p.getWidth(), p.getBottom() - (clampf (yo, -60.0f, 6.0f) + 60.0f) / 60.0f * p.getHeight()); };
        g.setColour (col::white.withAlpha (0.06f));
        for (int k = 0; k <= 6; ++k) { const float f = (float) k / 6.0f; g.drawVerticalLine ((int) (p.getX() + p.getWidth() * f), p.getY(), p.getBottom()); g.drawHorizontalLine ((int) (p.getY() + p.getHeight() * f), p.getX(), p.getRight()); }
        g.setColour (col::white.withAlpha (0.12f)); g.drawLine (p.getX(), p.getBottom(), p.getRight(), p.getY(), 1.0f);
        juce::Path c;
        for (int i = 0; i <= 120; ++i)
        {
            const float xi = -60.0f + 60.0f * (float) i / 120.0f;
            const float yo = lim ? std::min (xi + mk, thr) : xi + CompUnit::curve (xi, thr, kn, rat) + mk;
            const auto q = toS (xi, yo); if (i == 0) c.startNewSubPath (q); else c.lineTo (q);
        }
        glowStroke (g, c, acc, 2.0f);
        g.setColour (acc.withAlpha (0.35f)); g.drawHorizontalLine ((int) toS (0, thr).y, p.getX(), p.getRight());
        drawCaption (g, { p.getX() + 6, p.getY() + 4, p.getWidth() - 12, 14 }, lim ? juce::String ("LIMIT  ceiling ") + juce::String (thr, 1) + " dB" : "THRESH " + juce::String (thr, 1) + " dB  -  " + juce::String (rat, rat < 10 ? 1 : 0) + " : 1",
                     col::dim.brighter (0.2f), 10.0f);
        // gain-reduction meter (single / limit) with a falling hold
        const float gr = rack.tm.compGr.load();
        grShown = std::min (gr, grShown * 0.9f);
        if (glass()) progl::well (g, meter, 4, T (juce::Colour (0xff12141c)), col::dim.withAlpha (0.35f));    // GLASS (round 3)
        else { g.setColour (T (juce::Colour (0xff12141c))); g.fillRoundedRectangle (meter, 4); }
        const float h = meter.getHeight() * clampf (-grShown / 24.0f, 0.0f, 1.0f);
        juce::ColourGradient gg (acc.brighter (0.3f), 0, meter.getY(), acc.darker (0.4f), 0, meter.getY() + h, false);
        g.setGradientFill (gg); g.fillRoundedRectangle (meter.withHeight (h), 4);
        for (int db : { 3, 6, 12, 18 }) { const float y = meter.getY() + meter.getHeight() * (float) db / 24.0f; g.setColour (col::white.withAlpha (0.15f)); g.drawHorizontalLine ((int) y, meter.getX(), meter.getRight()); }
        drawCaption (g, meter.withY (meter.getBottom() - 14).withHeight (14), juce::String (grShown, 1), col::white.withAlpha (0.8f), 9.0f, juce::Justification::centred);
        drawCaption (g, meter.withHeight (14).translated (0, -1), "GR", col::dim, 9.0f, juce::Justification::centred);
    }
    void tick (double) override { repaint (vis); if (! bandArea.isEmpty()) repaint (bandArea.withHeight (16)); }
private:
    juce::AudioProcessorValueTreeState& apvts; Rack::Impl& rack;
    TipSegmented mode; Choice keyC;
    juce::OwnedArray<Knob> knobs, bandKnobs, xKnobs; juce::Rectangle<int> vis, bandArea, keyCap; float grShown = 0;
};

// ---------------------------------------------------------------- SPLITTER
class BandsPage : public Page
{
public:
    BandsPage (juce::AudioProcessorValueTreeState& s, Rack::Impl& r) : apvts (s), rack (r), mode (s, pid (uBands, "mode"), accentOf (uBands))
    {
        const auto acc = accentOf (uBands);
        addAndMakeVisible (mode);
        mode.setTooltip ("L / H: low and high. L / M / H: three bands. M / S: the middle (mono) and the sides (the stereo part) of the sound");
        for (auto* id : { "xlo", "xhi" })
        {
            auto* kn = xKnobs.add (new Knob (s, pid (uBands, id), juce::String (id) == "xlo" ? "LOW X" : "HIGH X", acc));
            kn->slider.setTooltip (juce::String (id) == "xlo" ? "Where LOW ends and the next band starts" : "Where MID ends and HIGH starts (L / M / H)");
            addAndMakeVisible (kn);
        }
        for (int b = 0; b < kBands; ++b)
        {
            auto* g = gains.add (new Knob (s, pid (uBands, ("b" + juce::String (b + 1) + "_gain").toRawUTF8()), "LEVEL", bandColour (b)));
            g->slider.setTooltip ("This band's level"); addAndMakeVisible (g);
            for (int k = 0; k < kSlots; ++k)
            {
                auto* c = slots.add (new juce::ComboBox());
                c->addItem ("-", 1);
                for (size_t i = 0; i < bandSlotUnits().size(); ++i) c->addItem (unitInfo (bandSlotUnits()[i]).title, (int) i + 2);
                c->setColour (juce::ComboBox::outlineColourId, bandColour (b)); c->setColour (juce::ComboBox::arrowColourId, bandColour (b));
                c->setTooltip ("A unit for this band (in this order). It uses that unit's own settings: open its page from the strip to tweak it");
                c->onChange = [this, b, k, c]
                {
                    if (syncing) return;
                    const int id = c->getSelectedId(); const int u = id >= 2 ? bandSlotUnits()[(size_t) (id - 2)] : -1;
                    if (u != rack.bandSlot (b, k)) { rack.pushUndo(); rack.setBandSlot (b, k, u); }
                };
                addAndMakeVisible (c);
                auto* a = amts.add (new Knob (s, pid (uBands, ("s" + juce::String (b + 1) + juce::String (k + 1) + "_amt").toRawUTF8()), "", bandColour (b)));
                a->slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
                a->slider.setTooltip ("How much of this slot's unit you hear (its own dry / wet)");
                addAndMakeVisible (a);
            }
        }
        mode.onChange = [this] (int) { resized(); repaint(); };
        sync();
    }
    juce::Colour bandColour (int b) const { return b == 0 ? col::pink : b == 1 ? accentOf (uBands) : col::ice; }
    int bandCount() const { return (int) std::lround (getParam (apvts, pid (uBands, "mode"))) == 1 ? 3 : 2; }
    juce::String bandName (int b) const
    {
        const int m = (int) std::lround (getParam (apvts, pid (uBands, "mode")));
        if (m == 2) return b == 0 ? "MID" : "SIDE";
        if (m == 0) return b == 0 ? "LOW" : "HIGH";
        return b == 0 ? "LOW" : b == 1 ? "MID" : "HIGH";
    }
    void sync()
    {   // the slot menus follow the rack (undo, a loaded state)
        syncing = true;
        for (int b = 0; b < kBands; ++b)
            for (int k = 0; k < kSlots; ++k)
            {
                const int u = rack.bandSlot (b, k); int id = 1;
                for (size_t i = 0; i < bandSlotUnits().size(); ++i) if (bandSlotUnits()[i] == u) id = (int) i + 2;
                if (slots[b * kSlots + k]->getSelectedId() != id) slots[b * kSlots + k]->setSelectedId (id, juce::dontSendNotification);
            }
        syncing = false;
    }
    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop (34);
        mode.setBounds (top.removeFromLeft (330).reduced (0, 1));
        r.removeFromTop (4);
        hint = r.removeFromBottom (16);
        auto left = r.removeFromLeft (juce::jmin (300, r.getWidth() / 4));
        auto kr = left.removeFromBottom (juce::jmin (92, left.getHeight() / 2));
        const bool three = bandCount() == 3;
        xKnobs[1]->setVisible (three);
        if (three) gridLayout (kr, { xKnobs[0], xKnobs[1] }, 2, kr.getHeight()); else xKnobs[0]->setBounds (kr.withSizeKeepingCentre (kr.getWidth() / 2, kr.getHeight()));
        vis = left.reduced (0, 4);
        r.removeFromLeft (10);
        cols = r;
        const int nb = bandCount(), cw = r.getWidth() / 3;
        for (int b = 0; b < kBands; ++b)
        {
            const bool show = b < nb;
            auto c = r.removeFromLeft (cw).reduced (6, 0);
            gains[b]->setVisible (show);
            for (int k = 0; k < kSlots; ++k) { slots[b * kSlots + k]->setVisible (show); amts[b * kSlots + k]->setVisible (show); }
            if (! show) continue;
            c.removeFromTop (22);
            auto g = c.removeFromBottom (juce::jmin (86, c.getHeight() / 3));
            gains[b]->setBounds (g.withSizeKeepingCentre (juce::jmin (90, g.getWidth()), g.getHeight()));
            const int rh = juce::jmin (44, c.getHeight() / kSlots);
            for (int k = 0; k < kSlots; ++k)
            {
                auto row = c.removeFromTop (rh).reduced (0, 4);
                amts[b * kSlots + k]->setBounds (row.removeFromRight (row.getHeight() + 4));
                row.removeFromRight (4);
                slots[b * kSlots + k]->setBounds (row.withSizeKeepingCentre (row.getWidth(), juce::jmin (26, row.getHeight())));
            }
        }
    }
    void paint (juce::Graphics& g) override
    {
        const auto acc = accentOf (uBands);
        drawCaption (g, hint.toFloat(), "each slot is that unit with its own page's settings (open it from the strip); the bands are re-aligned and summed back", col::dim, 10.0f);
        // the split: a band picture with each band's level
        drawScreen (g, vis.toFloat(), acc);
        auto v = vis.toFloat().reduced (10, 8);
        const int nb = bandCount(); const int m = (int) std::lround (getParam (apvts, pid (uBands, "mode")));
        const float w = v.getWidth() / (float) nb;
        for (int b = 0; b < nb; ++b)
        {
            auto br = juce::Rectangle<float> (v.getX() + w * (float) b, v.getY(), w, v.getHeight()).reduced (3, 0);
            const float lvl = clampf ((gainToDb (rack.tm.bandsLevel[(size_t) b].load() + 1e-6f) + 48.0f) / 48.0f, 0.0f, 1.0f);
            g.setColour (bandColour (b).withAlpha (0.12f)); g.fillRoundedRectangle (br, 6);
            auto f = br.withTrimmedTop (br.getHeight() * (1.0f - lvl));
            juce::ColourGradient gg (bandColour (b).withAlpha (0.8f), 0, f.getY(), bandColour (b).withAlpha (0.25f), 0, br.getBottom(), false);
            g.setGradientFill (gg); g.fillRoundedRectangle (f, 6);
            int used = 0; for (int k = 0; k < kSlots; ++k) used += rack.bandSlot (b, k) >= 0 ? 1 : 0;
            ghosty::drawGhost (g, ghosty::lookFor (26 + b), br.getCentreX(), juce::jmax (br.getY() + 30.0f, f.getY() - 12.0f), 14.0f + 3.0f * (float) used, 0, false, 0.5f + 0.5f * lvl);
            drawCaption (g, br.withHeight (14).translated (0, 2), bandName (b), col::white.withAlpha (0.85f), 9.5f, juce::Justification::centred);
        }
        drawCaption (g, v.withHeight (12).translated (0, v.getHeight() - 12), m == 2 ? "M / S" : (juce::String (juce::roundToInt (getParam (apvts, pid (uBands, "xlo")))) + " Hz" + (nb == 3 ? "  /  " + juce::String (getParam (apvts, pid (uBands, "xhi")) / 1000.0f, 1) + " kHz" : juce::String())),
                     col::dim, 9.0f, juce::Justification::centred);
        // the band columns' titles
        const int cw = cols.getWidth() / 3;
        for (int b = 0; b < nb; ++b)
        {
            auto t = juce::Rectangle<float> ((float) (cols.getX() + b * cw + 6), (float) cols.getY(), (float) cw - 12.0f, 18.0f);
            g.setColour (bandColour (b).withAlpha (0.18f)); g.fillRoundedRectangle (t, 5);
            drawCaption (g, t.reduced (8, 0), bandName (b) + "  -  up to 3 units", bandColour (b), 10.5f);
        }
    }
    void tick (double) override { repaint (vis); if (++n % 10 == 0) sync(); }
private:
    juce::AudioProcessorValueTreeState& apvts; Rack::Impl& rack; TipSegmented mode;
    juce::OwnedArray<Knob> xKnobs, gains, amts; juce::OwnedArray<juce::ComboBox> slots;
    juce::Rectangle<int> vis, cols, hint; bool syncing = false; int n = 0;
};

// ---------------------------------------------------------------- MORE: extra controls in a pop-down over an existing page
// (the page itself stays exactly as it was: strictly additive UI). Used by the CHORUS upgrade.
class MorePage : public Page
{
public:
    struct Pop : juce::Component
    {
        int unit = 0;
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            if (glass()) progl::card (g, r, 10, juce::Colours::transparentBlack, false);    // GLASS (round 3): frosted pop-down (Ghosty Pro LIGHT)
            else { g.setColour (T (juce::Colour (0xff11131a))); g.fillRoundedRectangle (r, 10); }
            g.setColour (accentOf (unit).withAlpha (0.6f)); g.drawRoundedRectangle (r.reduced (0.5f), 10, 1.2f);
        }
    };
    MorePage (juce::AudioProcessorValueTreeState& s, int unit, std::unique_ptr<Page> innerPage, const char* segId, std::vector<KnobSpec> knobs, std::vector<juce::String> tips)
        : inner (std::move (innerPage))
    {
        const auto acc = accentOf (unit);
        addAndMakeVisible (*inner);
        more.setButtonText ("MORE"); more.setClickingTogglesState (true); more.setColour (juce::TextButton::buttonOnColourId, acc);
        more.setTooltip ("More controls for this effect");
        more.onClick = [this] { pop.setVisible (more.getToggleState()); if (pop.isVisible()) pop.toFront (false); };
        addAndMakeVisible (more);
        pop.unit = unit; addChildComponent (pop);
        if (segId != nullptr) { seg = std::make_unique<TipSegmented> (s, pid (unit, segId), acc); seg->columns = 1; pop.addAndMakeVisible (*seg); if (! tips.empty()) seg->setTooltip (tips[0]); }
        for (size_t i = 0; i < knobs.size(); ++i)
        {
            auto* k = kn.add (new Knob (s, pid (unit, knobs[i].id), knobs[i].caption, acc));
            if (i + 1 < tips.size()) k->slider.setTooltip (tips[i + 1]);
            pop.addAndMakeVisible (k);
        }
    }
    void resized() override
    {
        inner->setBounds (getLocalBounds());
        more.setBounds (8, 8, 64, 22);
        const int w = juce::jmin (getWidth() - 16, 120 + 96 * kn.size()), h = 124;
        pop.setBounds (8, 34, w, h);
        auto r = pop.getLocalBounds().reduced (10, 8);
        if (seg) seg->setBounds (r.removeFromLeft (110).withSizeKeepingCentre (110, 56));
        r.removeFromLeft (10);
        std::vector<juce::Component*> items; for (auto* k : kn) items.push_back (k);
        gridLayout (r, items, juce::jmax (1, kn.size()), r.getHeight());
    }
    void tick (double t) override { inner->tick (t); }
private:
    std::unique_ptr<Page> inner; juce::TextButton more; Pop pop; std::unique_ptr<TipSegmented> seg; juce::OwnedArray<Knob> kn;
};
inline std::unique_ptr<Page> withChorusMore (juce::AudioProcessorValueTreeState& s, std::unique_ptr<Page> inner)
{
    return std::make_unique<MorePage> (s, uChorus, std::move (inner), "taps",
        std::vector<KnobSpec> { { "delay2", "DELAY 2" }, { "lpf", "WET LP" }, { "hpf", "WET HP" } },
        std::vector<juce::String> { "3 VOICES: the classic chorus. 4 TAPS: two taps at DELAY and two at DELAY 2, a quarter cycle apart: wider, richer",
                                    "The second delay time (4 TAPS)", "Low-pass on the chorus only (the dry stays as it is)", "High-pass on the chorus only: keeps the lows clean and centred" });
}

// ---------------------------------------------------------------- TONE EQ
inline std::unique_ptr<Page> makeEq3Page (juce::AudioProcessorValueTreeState& s, Rack::Impl&)
{
    auto* sp = &s;
    auto page = std::make_unique<UnitPage> (s, uEq3, nullptr,
        std::vector<KnobSpec> { { "b1_freq", "LOW F" }, { "b1_gain", "LOW" }, { "b1_q", "LOW Q" }, { "tilt", "TILT" },
                                { "b2_freq", "MID F" }, { "b2_gain", "MID" }, { "b2_q", "MID Q" }, { "out", "OUTPUT" },
                                { "b3_freq", "HIGH F" }, { "b3_gain", "HIGH" }, { "b3_q", "HIGH Q" } },
        std::vector<KnobSpec> {}, std::vector<KnobSpec> {},
        [sp] (juce::Graphics& g, juce::Rectangle<float> r, double)
        {   // the response curve, -18 .. +18 dB, 20 Hz .. 20 kHz, with a ghost on each band
            float fr[3], gd[3], qs[3];
            for (int k = 0; k < 3; ++k)
            {
                const juce::String p = "b" + juce::String (k + 1);
                fr[k] = getParam (*sp, pid (uEq3, (p + "_freq").toRawUTF8())); gd[k] = getParam (*sp, pid (uEq3, (p + "_gain").toRawUTF8())); qs[k] = getParam (*sp, pid (uEq3, (p + "_q").toRawUTF8()));
            }
            const float tl = getParam (*sp, pid (uEq3, "tilt"));
            auto pr = r.reduced (10, 16);
            auto xOf = [&] (double f) { return pr.getX() + (float) (std::log10 (f / 20.0) / 3.0) * pr.getWidth(); };
            auto yOf = [&] (double db) { return pr.getCentreY() - (float) juce::jlimit (-18.0, 18.0, db) / 18.0f * pr.getHeight() * 0.5f; };
            g.setColour (col::white.withAlpha (0.06f));
            for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 }) g.drawVerticalLine ((int) xOf (f), pr.getY(), pr.getBottom());
            for (double db : { -12.0, -6.0, 0.0, 6.0, 12.0 }) g.drawHorizontalLine ((int) yOf (db), pr.getX(), pr.getRight());
            juce::Path c;
            for (int i = 0; i <= 200; ++i)
            {
                const double f = 20.0 * std::pow (1000.0, i / 200.0);
                const auto pt = juce::Point<float> (xOf (f), yOf (Eq3Unit::responseDb (f, fr, gd, qs, tl)));
                if (i == 0) c.startNewSubPath (pt); else c.lineTo (pt);
            }
            glowStroke (g, c, accentOf (uEq3), 2.0f);
            const juce::Colour cs[3] { col::pink, accentOf (uEq3), col::ice };
            for (int k = 0; k < 3; ++k)
            {
                const float x = xOf (fr[k]), y = yOf (Eq3Unit::responseDb (fr[k], fr, gd, qs, tl));
                auto L = ghosty::lookFor (28 + k); L.slime = cs[k];
                ghosty::drawGhost (g, L, x, y - 14.0f, 22.0f, 0, false, 0.9f);
            }
            drawCaption (g, r.reduced (10, 4).removeFromTop (14), "TILT " + juce::String (tl, 1) + " dB  -  low " + juce::String (gd[0], 1) + "  mid " + juce::String (gd[1], 1) + "  high " + juce::String (gd[2], 1) + " dB", col::dim.brighter (0.2f), 10.0f);
        });
    for (auto* c : page->getChildren())
        if (auto* k = dynamic_cast<Knob*> (c))
        {
            const auto cap = k->caption;
            k->slider.setTooltip (cap == "TILT" ? "Tilts the whole sound around 1 kHz: right = brighter (highs up, lows down), left = darker"
                                : cap == "OUTPUT" ? "Output level of TONE EQ"
                                : cap.endsWith (" F") ? "Where this band sits" : cap.endsWith (" Q") ? "How narrow this band is (higher = narrower)"
                                : "Boost or cut this band (0 = untouched)");
        }
    return page;
}
