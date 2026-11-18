// Ghost Box 3 unit pages: GHOST SPECTRA, BASS LAB, PORTAL, GHOST FORMANT (included by FxBoxPages.inc, GPFX_BOX only).

// GHOST SPECTRA display: the live 64-band spectrum, and a paintable FREEZE MASK on top (drag across the bands to freeze
// just those bands; right-drag clears; double-click clears everything)
class SpectraView : public juce::Component, private juce::Timer
{
public:
    SpectraView (juce::AudioProcessorValueTreeState& s, Rack::Impl& r) : apvts (s), rack (r) { startTimerHz (30); }
    ~SpectraView() override { stopTimer(); }
    void paint (juce::Graphics& g) override
    {
        const auto acc = accentOf (uSpectra);
        auto rr = getLocalBounds().toFloat(); drawScreen (g, rr, acc);
        const bool frozen = getParam (apvts, pid (uSpectra, "freeze")) > 0.5f;
        const float trans = rack.tm.spectraTrans.load();
        auto a = area();
        const float bw = a.getWidth() / (float) kSpectraBands;
        for (int b = 0; b < kSpectraBands; ++b)
        {
            const float m = rack.tm.spectrum[(size_t) b].load(), mk = rack.spectraMask.get (b);
            const float h = juce::jlimit (0.0f, 1.0f, (gainToDb (m) + 72.0f) / 72.0f) * a.getHeight();
            auto bar = juce::Rectangle<float> (a.getX() + b * bw + 1, a.getBottom() - h, bw - 2, h);
            const bool held = frozen || mk > 0.001f;
            juce::ColourGradient gr (acc.withAlpha (0.85f), 0, bar.getY(), (held ? T (juce::Colour (0xffB8F4FF)) : acc).withAlpha (0.15f), 0, a.getBottom(), false);
            g.setGradientFill (gr); g.fillRoundedRectangle (bar, 1.5f);
            if (held) { g.setColour (col::white.withAlpha (0.35f)); g.fillRect (bar.getX(), bar.getY(), bar.getWidth(), 1.5f); }
            if (mk > 0.001f)
            {   // the painted mask: an ice-blue column, its height = how frozen the band is
                auto col_ = juce::Rectangle<float> (a.getX() + b * bw + 1, a.getY() + (1.0f - mk) * a.getHeight(), bw - 2, mk * a.getHeight());
                g.setColour (T (juce::Colour (0xffB8F4FF)).withAlpha (0.18f)); g.fillRect (col_);
                g.setColour (T (juce::Colour (0xffB8F4FF)).withAlpha (0.9f)); g.fillRect (col_.getX(), col_.getY(), col_.getWidth(), 2.0f);
            }
        }
        ghosty::drawGhost (g, ghosty::lookFor (unitInfo (uSpectra).look), a.getRight() - 30, a.getY() + 26 + 3 * (float) std::sin (t * 1.7), 24, 0.6f * (1.0f - trans), false, 0.35f + 0.65f * trans);
        if (frozen) drawCaption (g, { a.getX(), a.getY(), 120, 14 }, "FROZEN", T (juce::Colour (0xffB8F4FF)), 11);
        else if (rack.spectraMask.any()) drawCaption (g, { a.getX(), a.getY(), 220, 14 }, "FREEZE MASK  (right-drag clears)", T (juce::Colour (0xffB8F4FF)), 10);
        else drawCaption (g, { a.getX(), a.getY(), 260, 14 }, "drag across bands to paint a freeze mask", col::dim, 9.5f);
        drawCaption (g, { a.getX(), a.getBottom() + 1, 60, 12 }, "30 Hz", col::dim, 8.5f);
        drawCaption (g, { a.getRight() - 60, a.getBottom() + 1, 60, 12 }, "20 kHz", col::dim, 8.5f, juce::Justification::centredRight);
        const int fs = static_cast<SpectraUnit*> (rack.units[uSpectra].get())->frameSize();
        drawCaption (g, { a.getCentreX() - 140, a.getBottom() + 1, 280, 12 }, "latency " + juce::String (fs * 1000.0 / rack.sr, 1) + " ms (reported to the host)", col::dim, 8.5f, juce::Justification::centred);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.getNumberOfClicks() >= 2) { rack.pushUndo(); rack.spectraMask.clear(); repaint(); return; }
        rack.pushUndo(); lastBand = -1; paintAt (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override { paintAt (e); }
private:
    void paintAt (const juce::MouseEvent& e)
    {
        const auto a = area(); if (a.getWidth() <= 0) return;
        const int b = juce::jlimit (0, kSpectraBands - 1, (int) ((e.position.x - a.getX()) / a.getWidth() * kSpectraBands));
        const float v = e.mods.isPopupMenu() ? 0.0f : juce::jlimit (0.0f, 1.0f, (a.getBottom() - e.position.y) / a.getHeight());
        const int from = lastBand < 0 ? b : lastBand;
        for (int k = std::min (from, b); k <= std::max (from, b); ++k) rack.spectraMask.set (k, v);   // fill the gap between drag events
        lastBand = b; repaint();
    }
    juce::Rectangle<float> area() const { return getLocalBounds().toFloat().reduced (12, 16); }
    void timerCallback() override { t += 1.0 / 30.0; repaint(); }
    juce::AudioProcessorValueTreeState& apvts; Rack::Impl& rack; double t = 0; int lastBand = -1;
};

class SpectraPage : public Page
{
public:
    SpectraPage (juce::AudioProcessorValueTreeState& s, Rack::Impl& r) : view (s, r), syncC (s, pid (uSpectra, "fsync"), accentOf (uSpectra)), divC (s, pid (uSpectra, "mdiv"), accentOf (uSpectra)),
        freezeT (s, pid (uSpectra, "freeze"), "FREEZE", accentOf (uSpectra)), hqT (s, pid (uSpectra, "hq"), "HQ BOUNCE", accentOf (uSpectra))
    {
        const auto acc = accentOf (uSpectra);
        static const char* ids[] { "smear", "scatter", "shift", "ghost", "motion", "tilt", "thresh" };
        static const char* caps[] { "SMEAR", "SCATTER", "SHIFT", "GHOST", "MOTION", "TILT", "THRESHOLD" };
        for (int k = 0; k < 7; ++k) addAndMakeVisible (kn.add (new Knob (s, pid (uSpectra, ids[k]), caps[k], acc)));
        kn[6]->slider.setTooltip ("Spectral gate: bins this far under the loudest bin of the frame are dropped before resynthesis (-90 = off)");
        hqT.setTooltip ("Off (default): a bounce is exactly what you heard. On: bounces use a denser overlap (8 instead of 4) for a smoother resynthesis; the time constants are hop-normalised, but SCATTER's randomness differs slightly");
        syncC.setTooltip ("While FREEZE is on, re-capture the spectrum on every division");
        for (auto* c : std::initializer_list<juce::Component*> { &view, &syncC, &divC, &freezeT, &hqT }) addAndMakeVisible (c);
    }
    void resized() override
    {
        auto r = getLocalBounds();
        auto right = r.removeFromRight (3 * 86 + 10).withTrimmedLeft (10);
        auto bottom = right.removeFromBottom (34);
        freezeT.setBounds (bottom.removeFromLeft (bottom.getWidth() / 2).reduced (2, 2)); hqT.setBounds (bottom.reduced (2, 2));
        auto row = right.removeFromBottom (30).withTrimmedTop (4);
        syncCap = row.removeFromLeft (84); syncC.setBounds (row.removeFromLeft (80).reduced (0, 1)); row.removeFromLeft (8);
        divCap = row.removeFromLeft (50); divC.setBounds (row.reduced (0, 1));
        std::vector<juce::Component*> it; for (auto* k : kn) it.push_back (k);
        gridLayout (right, it, 3, juce::jmin (96, right.getHeight() / 3));
        view.setBounds (r);
    }
    void paint (juce::Graphics& g) override
    {
        drawCaption (g, syncCap.toFloat(), "FREEZE SYNC", col::dim.brighter (0.25f), 10);
        drawCaption (g, divCap.toFloat(), "MOTION", col::dim.brighter (0.25f), 10);
    }
    void tick (double) override {}
private:
    SpectraView view; Choice syncC, divC; Toggle freezeT, hqT; juce::OwnedArray<Knob> kn; juce::Rectangle<int> syncCap, divCap;
};

void drawBassVis (juce::Graphics& g, juce::Rectangle<float> r, double t, Rack::Impl& rk, juce::AudioProcessorValueTreeState& s)
{
    const auto acc = accentOf (uBassLab);
    auto a = r.reduced (14, 18);
    auto xOf = [&a] (float hz) { return a.getX() + a.getWidth() * (std::log (hz / 20.0f) / std::log (1000.0f)); };
    const float lo = getParam (s, pid (uBassLab, "xlo")), hi = getParam (s, pid (uBassLab, "xhi"));
    const juce::Colour bc[3] { T (juce::Colour (0xffFF7A3D)), T (juce::Colour (0xffFFD45A)), T (juce::Colour (0xff45E6FF)) };
    const float xs[4] { a.getX(), xOf (lo), xOf (hi), a.getRight() };
    const char* names[3] { "LOW  (mono + sub)", "MID  (growl)", "HIGH  (edge)" };
    for (int k = 0; k < 3; ++k)
    {
        const float lv = juce::jlimit (0.0f, 1.0f, rk.tm.bassBand[(size_t) k].load() * 1.4f);
        auto band = juce::Rectangle<float> (xs[k], a.getY(), xs[k + 1] - xs[k], a.getHeight());
        g.setColour (bc[k].withAlpha (0.08f)); g.fillRect (band);
        auto lvl = band.withTrimmedTop (band.getHeight() * (1.0f - lv)).reduced (6, 0);
        juce::ColourGradient gr (bc[k].withAlpha (0.7f), 0, lvl.getY(), bc[k].withAlpha (0.1f), 0, lvl.getBottom(), false);
        g.setGradientFill (gr); g.fillRoundedRectangle (lvl, 3);
        drawCaption (g, band.reduced (6, 4).removeFromTop (12), names[k], bc[k], 9.5f);
    }
    for (int k = 1; k < 3; ++k)
    {
        g.setColour (col::white.withAlpha (0.6f)); g.fillRect (xs[k] - 0.75f, a.getY(), 1.5f, a.getHeight());
        drawCaption (g, { xs[k] - 40, a.getBottom() + 2, 80, 12 }, juce::String (juce::roundToInt (k == 1 ? lo : hi)) + " Hz", col::white.withAlpha (0.8f), 9, juce::Justification::centred);
    }
    // the measured fundamental and, with TRANSLATE, the harmonics it adds above it (what a phone speaker can play)
    const float tr = getParam (s, pid (uBassLab, "translate")), f0 = rk.tm.bassF0.load();
    if (f0 > 20.0f)
    {
        g.setColour (col::white.withAlpha (0.75f)); g.fillRect (xOf (f0) - 1.0f, a.getBottom() - a.getHeight() * 0.7f, 2.0f, a.getHeight() * 0.7f);
        drawCaption (g, { xOf (f0) - 40, a.getBottom() - a.getHeight() * 0.7f - 14, 80, 12 }, juce::String (juce::roundToInt (f0)) + " Hz", col::white, 9, juce::Justification::centred);
        if (tr > 0.01f)
            for (int h = 2; h <= 5; ++h)
            {
                const float x = xOf (f0 * (float) h), hgt = a.getHeight() * 0.55f * tr / (float) (h - 1);
                g.setColour (acc.withAlpha (0.8f)); g.fillRect (x - 1.0f, a.getBottom() - hgt, 2.0f, hgt);
            }
    }
    // GROWL centre (moves with GROWL MOVE)
    if (getParam (s, pid (uBassLab, "growl")) > 0.01f)
    {
        const float gx = xOf (rk.tm.bassGrowlHz.load());
        g.setColour (bc[1].withAlpha (0.9f)); g.fillRect (gx - 1.0f, a.getY() + 16, 2.0f, a.getHeight() * 0.35f);
        drawCaption (g, { gx - 40, a.getY() + 16 + a.getHeight() * 0.35f, 80, 12 }, "growl " + juce::String (juce::roundToInt (rk.tm.bassGrowlHz.load())), bc[1], 8.5f, juce::Justification::centred);
    }
    ghosty::drawGhost (g, ghosty::lookFor (unitInfo (uBassLab).look), a.getX() + 26, a.getY() + 34 + 2 * (float) std::sin (t * 2.0), 22, 0, false, 0.9f);
}

void drawPortalVis (juce::Graphics& g, juce::Rectangle<float> r, double t, Rack::Impl& rk, juce::AudioProcessorValueTreeState& s)
{
    const auto acc = accentOf (uPortal);
    const int sh = (int) getParam (s, pid (uPortal, "shape")), mv = (int) getParam (s, pid (uPortal, "move"));
    const float sz = getParam (s, pid (uPortal, "size")), lvl = juce::jlimit (0.0f, 1.0f, rk.tm.portalLevel.load() * 3.0f);
    const auto c = r.getCentre(); const float R0 = juce::jmin (r.getWidth(), r.getHeight()) * (0.18f + 0.25f * sz);
    const double spin = mv == 2 ? t * 0.8 : mv == 1 ? std::sin (t * 0.5) * 0.4 : 0.0;
    const float breathe = mv == 3 ? 0.85f + 0.15f * (float) std::sin (t * 0.9) : 1.0f;
    const int sides = sh == 0 ? 6 : sh == 4 ? 8 : 0;
    for (int ring = 5; ring >= 0; --ring)
    {
        const float rr = R0 * breathe * (0.35f + 0.13f * ring);
        juce::Path p;
        if (sides > 0) { for (int k = 0; k <= sides; ++k) { const double a = spin + ring * 0.2 + k * 2.0 * juce::MathConstants<double>::pi / sides; const auto pt = c + juce::Point<float> ((float) std::cos (a), (float) std::sin (a)) * rr; if (k == 0) p.startNewSubPath (pt); else p.lineTo (pt); } }
        else
        {
            for (int k = 0; k <= 64; ++k)
            {
                const double a = spin * (ring % 2 ? -1 : 1) + k * 2.0 * juce::MathConstants<double>::pi / 64;
                const float wob = sh == 1 ? 1.0f + 0.06f * (float) std::sin (a * 5 + t * 1.3 + ring) : sh == 3 ? 1.0f + 0.03f * (float) std::sin (a * 13 + t) : 1.0f;
                const auto pt = c + juce::Point<float> ((float) std::cos (a), (float) std::sin (a)) * rr * wob;
                if (k == 0) p.startNewSubPath (pt); else p.lineTo (pt);
            }
        }
        glowStroke (g, p, acc.withAlpha ((sh == 2 && ring < 2 ? 0.15f : 0.25f) + 0.12f * ring + 0.3f * lvl), 1.2f, 0.6f);
    }
    ghosty::drawGhost (g, ghosty::lookFor (unitInfo (uPortal).look), c.x, c.y, R0 * 0.32f, 0.2f * lvl, false, 0.4f + 0.6f * lvl, std::sin (t * 0.3) < 0);
    if (getParam (s, pid (uPortal, "freeze")) > 0.5f) drawCaption (g, r.reduced (10, 6).removeFromTop (14), "FROZEN", T (juce::Colour (0xffB8F4FF)), 11);
}

class FormantPage : public Page
{
public:
    FormantPage (juce::AudioProcessorValueTreeState& s, Rack::Impl& r) : apvts (s), rack (r), lane (r.formantShape),
        moveSeg (s, pid (uFormant, "move"), accentOf (uFormant)), divC (s, pid (uFormant, "mdiv"), accentOf (uFormant)), lenC (s, pid (uFormant, "mlen"), accentOf (uFormant)), shapeC (s, pid (uFormant, "mshape"), accentOf (uFormant))
    {
        const auto acc = accentOf (uFormant);
        static const char* ids[] { "vowel", "mouth", "reso", "shift", "drive", "mdepth", "body" };
        static const char* caps[] { "VOWEL", "MOUTH SIZE", "RESONANCE", "FORMANT SHIFT", "DRIVE", "MOVE DEPTH", "BODY" };
        for (int k = 0; k < 7; ++k) addAndMakeVisible (kn.add (new Knob (s, pid (uFormant, ids[k]), caps[k], acc)));
        lane.accent = acc; lane.ghostLook = unitInfo (uFormant).look;
        lane.rangeSemis = [] { return 1.0f; };
        lane.bars = [this] { return bendlane::barsOf ((int) getParam (apvts, pid (uFormant, "mlen"))); };
        lane.amount = [this] { return getParam (apvts, pid (uFormant, "mdepth")); };
        lane.onEditStart = [this] { rack.pushUndo(); };
        lane.valueText = [] (float v) { static const char* vw[] { "U", "O", "I", "E", "A" }; juce::ignoreUnused (vw); return (v > 0 ? "+" : "") + juce::String (v * 2.0f, 1) + " vowel"; };
        shapeC.setTooltip ("LFO shape: sine, triangle, saw, square, sample-and-hold (new vowel every division) or ENV (the input level opens the mouth)");
        for (auto* c : std::initializer_list<juce::Component*> { &lane, &moveSeg, &divC, &lenC, &shapeC }) addAndMakeVisible (c);
        moveSeg.onChange = [this] (int) { updateMode(); }; updateMode();
    }
    void updateMode()
    {   // the drawn path only matters in DRAWN mode: dim it (and the division / shape controls) otherwise
        const int mv = (int) getParam (apvts, pid (uFormant, "move"));
        lane.setAlpha (mv == 2 ? 1.0f : fxtheme::kDisabledAlpha); lane.setEnabled (mv == 2); lenC.setEnabled (mv == 2);
        divC.setEnabled (mv == 1); shapeC.setEnabled (mv == 1);
    }
    void resized() override
    {
        auto r = getLocalBounds();
        auto right = r.removeFromRight (4 * 84 + 10).withTrimmedLeft (10);
        auto top = r.removeFromTop (r.getHeight() * 45 / 100); vis = top; r.removeFromTop (6);
        auto row = r.removeFromBottom (32).withTrimmedTop (4);
        moveSeg.setBounds (row.removeFromLeft (210)); row.removeFromLeft (6);
        divCap = row.removeFromLeft (40); divC.setBounds (row.removeFromLeft (80).reduced (0, 1)); row.removeFromLeft (6);
        shapeC.setBounds (row.removeFromLeft (104).reduced (0, 1)); row.removeFromLeft (6);
        lenCap = row.removeFromLeft (60); lenC.setBounds (row.removeFromLeft (92).reduced (0, 1));
        lane.setBounds (r);
        std::vector<juce::Component*> it; for (auto* k : kn) it.push_back (k);
        gridLayout (right, it, 4, juce::jmin (96, right.getHeight() / 2));
    }
    void paint (juce::Graphics& g) override
    {
        const auto acc = accentOf (uFormant);
        auto v = vis.toFloat(); drawScreen (g, v, acc);
        juce::Graphics::ScopedSaveState ss (g); g.reduceClipRegion (vis.reduced (2));
        // a ghost whose mouth makes the vowel being heard
        const float vw = rack.tm.formantVowel.load(), lv = juce::jlimit (0.0f, 1.0f, rack.tm.formantLevel.load() * 2.5f);
        static const float open[5] { 1.0f, 0.55f, 0.25f, 0.8f, 0.45f }, wide[5] { 0.8f, 1.0f, 1.1f, 0.6f, 0.35f };
        const int a = juce::jlimit (0, 3, (int) vw); const float f = vw - (float) a;
        const float op = open[a] + f * (open[a + 1] - open[a]), wd = wide[a] + f * (wide[a + 1] - wide[a]);
        const auto c = v.getCentre().translated (-v.getWidth() * 0.18f, 0);
        const float gs = v.getHeight() * 0.36f;
        ghosty::drawGhost (g, ghosty::lookFor (unitInfo (uFormant).look), c.x, c.y, gs, 0, false, 1.0f);
        const float mw = gs * 0.42f * wd, mh = gs * 0.08f + gs * 0.30f * op * (0.4f + 0.6f * lv);
        g.setColour (T (juce::Colour (0xff0b0c10))); g.fillEllipse (c.x - mw * 0.5f, c.y + gs * 0.18f, mw, mh);
        g.setColour (acc.withAlpha (0.6f)); g.drawEllipse (c.x - mw * 0.5f, c.y + gs * 0.18f, mw, mh, 1.2f);
        // vowel strip
        auto strip = juce::Rectangle<float> (v.getCentreX() + 10, v.getCentreY() - 14, v.getWidth() * 0.4f, 28);
        static const char* names[] { "A", "E", "I", "O", "U" };
        for (int k = 0; k < 5; ++k)
        {
            const float x = strip.getX() + strip.getWidth() * k / 4.0f;
            const bool near = std::abs (vw - (float) k) < 0.5f;
            drawCaption (g, { x - 12, strip.getY(), 24, 28 }, names[k], near ? acc : col::dim, near ? 18.0f : 13.0f, juce::Justification::centred);
        }
        glowDot (g, { strip.getX() + strip.getWidth() * vw / 4.0f, strip.getBottom() + 8 }, 4.0f, acc);
        drawCaption (g, divCap.toFloat(), "SYNC", col::dim.brighter (0.25f), 10);
        drawCaption (g, lenCap.toFloat(), "LENGTH", col::dim.brighter (0.25f), 10);
    }
    void tick (double) override { repaint (vis); }
private:
    juce::AudioProcessorValueTreeState& apvts; Rack::Impl& rack; bendlane::Editor lane;
    Segmented moveSeg; Choice divC, lenC, shapeC; juce::OwnedArray<Knob> kn; juce::Rectangle<int> vis, divCap, lenCap;
};

std::unique_ptr<Page> makeBox3UnitPage (int u, juce::AudioProcessorValueTreeState& s, Rack::Impl& rk)
{
    auto* sp = &s; auto* rp = &rk;
    switch (u)
    {
        case uSpectra: return std::make_unique<SpectraPage> (s, rk);
        case uBassLab:
            return std::make_unique<UnitPage> (s, u, nullptr,
                std::vector<KnobSpec> { { "xlo", "LOW SPLIT" }, { "xhi", "HIGH SPLIT" }, { "lowwidth", "LOW WIDTH" }, { "sub", "SUB" },
                                        { "growl", "GROWL" }, { "gfreq", "GROWL FREQ" }, { "gmove", "GROWL MOVE" }, { "edge", "EDGE" },
                                        { "translate", "TRANSLATE" }, { "low", "LOW" }, { "mid", "MID" }, { "high", "HIGH" }, { "out", "OUTPUT" } },
                std::vector<KnobSpec> { { "auto", "GAIN COMP" } }, std::vector<KnobSpec> { { "gdiv", "GROWL SYNC" } },
                [sp, rp] (juce::Graphics& g, juce::Rectangle<float> r, double t) { drawBassVis (g, r, t, *rp, *sp); });
        case uPortal:
            return std::make_unique<UnitPage> (s, u, "shape",
                std::vector<KnobSpec> { { "size", "SIZE" }, { "decay", "DECAY" }, { "early", "EARLY" }, { "density", "DENSITY" }, { "transform", "TRANSFORM" }, { "predelay", "PRE-DELAY" }, { "tone", "TONE" } },
                std::vector<KnobSpec> { { "freeze", "FREEZE" } }, std::vector<KnobSpec> { { "move", "MOVEMENT" } },
                [sp, rp] (juce::Graphics& g, juce::Rectangle<float> r, double t) { drawPortalVis (g, r, t, *rp, *sp); });
        case uFormant: return std::make_unique<FormantPage> (s, rk);
        default: return nullptr;
    }
}
