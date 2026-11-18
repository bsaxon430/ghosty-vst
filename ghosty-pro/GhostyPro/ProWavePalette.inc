// LAYOUT 2 (round 3): the WAVE PALETTE - one small tile per oscillator table, drawn as its own waveform, the selected one lit, a
// dice tile for a random one. Click = this oscillator plays that table; hover = the oscillator display previews it. In STUDIO it sits
// under each oscillator's display; in CLASSIC it is part of the OSC MORE pop-down (nothing in the CLASSIC window itself moves).
// Included by ProEditor.cpp after MorePanel, before the pages. The built-in tables' thumbnails are made once per process.

class WavePalette : public juce::Component, public juce::TooltipClient, private juce::Timer
{
public:
    WavePalette (ProProcessor& p, int oscIndex) : proc (p), osc (oscIndex) { setMouseCursor (juce::MouseCursor::PointingHandCursor); startTimerHz (8); setComponentID ("wavePalette" + juce::String (oscIndex + 1)); }
    std::function<void (int table)> onHover;        // the table under the mouse (-1 = none): the display previews it
    std::function<void (const juce::String&)> onMessage;
    int columns = 12;        // 20 tables + 13 ENGINES + the dice = three rows

    static int numTiles() { return gpro::eng::tableChoiceNames().size() + 1; }     // every table, every ENGINE, + the dice
    juce::String tblId() const { return osc == 0 ? "o1_tbl" : "o2_tbl"; }

    // a table's first frame as a 256-point cycle (built-in tables: made once, then cached); GHOST A/B = frame A as drawn now;
    // SAMPLE = what the oscillator plays when it plays the sample (else nothing to draw: false)
    static bool thumbFor (ProProcessor& p, int osc, int table, ghosty::Frame& out)
    {
        if (table == tGhost) { out = p.frameA; return true; }
        if (table == tSample || table == tUser)     // (USER = the WAVETABLE EDITOR's table: drawn when this oscillator plays it)
        {
            if ((int) paramValue (p, osc == 0 ? "o1_tbl" : "o2_tbl") != table) return false;
            if (auto t = p.uiTable (osc); t != nullptr && ! t->preview.empty()) { out = t->preview.front(); return true; }
            return false;
        }
        static std::map<int, ghosty::Frame> cache;
        auto it = cache.find (table);
        if (it == cache.end())
        {
            const auto frames = makeBuiltIn (table);
            ghosty::Frame f {};
            if (! frames.empty()) f = toPreview (frames.front());
            it = cache.emplace (table, f).first;
        }
        out = it->second;
        return true;
    }

    juce::Rectangle<float> tileRect (int i) const
    {
        const int rows = (numTiles() + columns - 1) / columns;
        const float w = (float) getWidth() / (float) columns, h = (float) getHeight() / (float) rows;
        return { (float) (i % columns) * w, (float) (i / columns) * h, w, h };
    }
    int tileAt (juce::Point<float> p) const { for (int i = 0; i < numTiles(); ++i) if (tileRect (i).contains (p)) return i; return -1; }

    void paint (juce::Graphics& g) override
    {
        pt::ScopedDark screen;      // tiles are little displays: dark in every look
        const int sel = (int) paramValue (proc, tblId());
        const int dice = numTiles() - 1;
        for (int i = 0; i < numTiles(); ++i)
        {
            const auto r = tileRect (i).reduced (1.5f);
            const bool on = i == sel, over = i == hover;
            g.setColour (C().deep); g.fillRoundedRectangle (r, 5.0f);
            const auto acc = on ? C().slime : C().gold;
            if (on) { g.setColour (C().slime.withAlpha (0.16f)); g.fillRoundedRectangle (r, 5.0f); }
            g.setColour (on ? C().slime : (over ? C().gold : C().white.withAlpha (0.12f))); g.drawRoundedRectangle (r, 5.0f, on ? 1.6f : 1.0f);
            const auto a = r.reduced (4.0f, 3.5f);
            if (i == dice)
            {   // the dice: five pips
                const float s = juce::jmin (a.getWidth(), a.getHeight()) * 0.8f, cx = a.getCentreX(), cy = a.getCentreY();
                g.setColour (C().pink); g.drawRoundedRectangle (cx - s * 0.5f, cy - s * 0.5f, s, s, 2.5f, 1.2f);
                for (auto [dx, dy] : { std::pair<float, float> { -0.25f, -0.25f }, { 0.25f, -0.25f }, { 0.0f, 0.0f }, { -0.25f, 0.25f }, { 0.25f, 0.25f } })
                    g.fillEllipse (cx + dx * s - 1.4f, cy + dy * s - 1.4f, 2.8f, 2.8f);
                continue;
            }
            if (const int e = gpro::eng::engineOf (i); e >= 0)
            {   // an ENGINE: no single wave to draw - its name, in the engine colour
                g.setColour (on ? C().slime : C().ice); g.setFont (font (7.5f));
                g.drawFittedText (gpro::eng::engineNames()[e].toUpperCase(), a.toNearestInt(), juce::Justification::centred, 2, 0.7f);
                continue;
            }
            ghosty::Frame f;
            if (! thumbFor (proc, osc, i, f))
            {
                g.setColour (acc.withAlpha (0.8f)); g.setFont (font (8.5f));
                g.drawText (i == tSample ? "WAV" : i == tUser ? "USER" : "?", a, juce::Justification::centred);
                continue;
            }
            float mx = 1.0e-6f; for (auto v : f) mx = juce::jmax (mx, std::abs (v));
            juce::Path p;
            for (int k = 0; k < ghosty::FRAME; k += 4)
            {
                const float x = a.getX() + (float) k / (float) (ghosty::FRAME - 4) * a.getWidth(), y = a.getCentreY() - f[(size_t) k] / mx * a.getHeight() * 0.46f;
                if (k == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            if (on) { g.setColour (acc.withAlpha (0.3f)); g.strokePath (p, juce::PathStrokeType (3.5f)); }
            g.setColour (acc); g.strokePath (p, juce::PathStrokeType (on ? 1.6f : 1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }
    juce::String getTooltip() override
    {
        if (hover < 0) return "WAVE PALETTE: click = OSC " + juce::String (osc + 1) + " plays that table, hover = preview it, the dice = a random one";
        if (hover == numTiles() - 1) return "A random table for OSC " + juce::String (osc + 1);
        return "OSC " + juce::String (osc + 1) + ": " + gpro::eng::tableChoiceNames()[hover] + (gpro::eng::engineOf (hover) >= 0 ? "  (ENGINE: its settings in MORE)" : "");
    }
    void mouseMove (const juce::MouseEvent& e) override { setHover (tileAt (e.position)); }
    void mouseExit (const juce::MouseEvent&) override { setHover (-1); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = tileAt (e.position);
        if (i < 0) return;
        int t = i;
        if (i == numTiles() - 1)
        {   // the dice: any table but the sample (unless one is loaded) and not the one playing now
            const int now = (int) paramValue (proc, tblId());
            do t = juce::Random::getSystemRandom().nextInt (gpro::eng::tableChoiceNames().size());
            while (t == now || t == tUser || (t == tSample && proc.sampleName().isEmpty()));     // (USER: only by choice)
        }
        if (t == tSample && proc.sampleName().isEmpty()) { if (onMessage) onMessage ("No sample yet: drop a WAV on the oscillator display (or WAV / IMPORT WAV)"); return; }
        if (gpro::eng::engineOf (t) >= 0 || gpro::eng::engineOf ((int) paramValue (proc, tblId())) >= 0)   // ENGINES DIARY: engine clicks, on disk before the audio thread sees them
            gpro::eng::diary::note ("palette click: OSC " + juce::String (osc + 1) + " " + gpro::eng::tableChoiceNames()[(int) paramValue (proc, tblId())] + " -> " + gpro::eng::tableChoiceNames()[t]
                                    + " (quality " + juce::String ((int) paramValue (proc, "quality")) + ", " + juce::String (proc.getSampleRate(), 0) + " Hz)");
        if (auto* prm = proc.apvts.getParameter (tblId())) { prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 ((float) t)); prm->endChangeGesture(); }
        if (onMessage) onMessage ("OSC " + juce::String (osc + 1) + ": " + gpro::eng::tableChoiceNames()[t]);
        repaint();
    }

private:
    ProProcessor& proc;
    const int osc;
    int hover = -1, lastSel = -1;
    void setHover (int h) { if (h == hover) return; hover = h; repaint(); if (onHover) onHover (h >= 0 && h < tableNames().size() ? h : -1); }   // (engines: no preview)
    void timerCallback() override
    {
        const int sel = (int) paramValue (proc, tblId());
        if (sel != lastSel || sel == tGhost || sel == tSample) { lastSel = sel; repaint(); }
    }
};

// CLASSIC: the OSC MORE pop-down with the palette under its settings (the window itself is unchanged)
struct MoreWithPalette : public juce::Component
{
    std::unique_ptr<MorePanel> more; WavePalette palette;
    MoreWithPalette (ProProcessor& p, int osc, const juce::String& title, const std::vector<MorePanel::Item>& items, juce::LookAndFeel* lf)
        : more (std::make_unique<MorePanel> (p.apvts, title, items, lf)), palette (p, osc)
    {
        if (lf != nullptr) setLookAndFeel (lf);
        addAndMakeVisible (*more); addAndMakeVisible (palette);
        setSize (juce::jmax (more->getWidth(), 352), more->getHeight() + 22 + 66);
    }
    ~MoreWithPalette() override { setLookAndFeel (nullptr); }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (C().card);
        if (progl::on (C().theme)) progl::card (g, getLocalBounds().toFloat(), 0, juce::Colours::transparentBlack, false);    // GLASS (round 3): frosted pop-down
        g.setColour (C().ecto); g.setFont (font (10.5f));
        g.drawText ("WAVE PALETTE  (click = play it)", juce::Rectangle<int> (10, more->getBottom() + 2, getWidth() - 20, 18), juce::Justification::centredLeft);
    }
    void resized() override
    {
        more->setBounds (0, 0, getWidth(), more->getHeight());
        palette.setBounds (8, more->getBottom() + 22, getWidth() - 16, 60);
    }
    static void show (juce::Component* from, ProProcessor& p, int osc, const juce::String& title, const std::vector<MorePanel::Item>& items)
    {
        auto panel = std::make_unique<MoreWithPalette> (p, osc, title, items, &from->getLookAndFeel());
        if (const char* shot = std::getenv ("GHOSTY_MORESHOT"))
        {   // tests (no desktop under Xvfb): snapshot the panel instead of opening the call-out
            panel->resized();
            auto img = panel->createComponentSnapshot (panel->getLocalBounds(), true, 1.0f);
            juce::PNGImageFormat png; juce::File f (shot); f.deleteFile();
            if (auto os = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream())) png.writeImageToStream (img, *os);
            return;
        }
        juce::CallOutBox::launchAsynchronously (std::move (panel), from->getScreenBounds(), nullptr);
    }
};

// the OSC MORE pop-down's items (CLASSIC and STUDIO): unison, two warps, spectral, then the ENGINE row (HARMONICS / TIMBRE / MORPH,
// named for the engine the oscillator plays) and, for TEXTURE / RESONATOR, their SOURCE and a row of their own
static std::vector<MorePanel::Item> oscMoreItems (ProProcessor& proc, const juce::String& pfx)
{
    std::vector<MorePanel::Item> items {
      { pfx + "dmode", "DETUNE MODE", MorePanel::Item::combo }, { pfx + "stack", "STACK", MorePanel::Item::combo }, { "uni_law", "UNISON LAW", MorePanel::Item::combo }, { "", "", MorePanel::Item::gap },
      { pfx + "w1", "WARP 1", MorePanel::Item::combo }, { pfx + "w1a", "AMOUNT 1", MorePanel::Item::knob }, { pfx + "w2", "WARP 2", MorePanel::Item::combo }, { pfx + "w2a", "AMOUNT 2", MorePanel::Item::knob },
      { pfx + "spec", "SPECTRAL", MorePanel::Item::combo }, { pfx + "speca", "AMOUNT", MorePanel::Item::knob } };
    const int e = gpro::eng::engineOf ((int) paramValue (proc, pfx + "tbl"));
    items.push_back ({ "", "", MorePanel::Item::gap }); items.push_back ({ "", "", MorePanel::Item::gap });
    for (int k = 0; k < 3; ++k) items.push_back ({ pfx + (k == 0 ? "eh" : k == 1 ? "et" : "em"), gpro::eng::controlCaption (e, k), MorePanel::Item::knob });
    items.push_back (gpro::eng::usesInput (e) ? MorePanel::Item { pfx + "esrc", "SOURCE", MorePanel::Item::combo } : MorePanel::Item { "", "", MorePanel::Item::gap });
    if (e == gpro::eng::eTexture)
        for (auto& it : std::initializer_list<MorePanel::Item> { { pfx + "tsize", "SIZE", MorePanel::Item::knob }, { pfx + "tpitch", "PITCH", MorePanel::Item::knob },
                                                                 { pfx + "tfb", "FEEDBACK", MorePanel::Item::knob }, { pfx + "tfrz", "FREEZE", MorePanel::Item::toggle } }) items.push_back (it);
    if (e == gpro::eng::eResonator)
        for (auto& it : std::initializer_list<MorePanel::Item> { { pfx + "rpos", "POSITION", MorePanel::Item::knob }, { pfx + "rmodel", "MODEL", MorePanel::Item::combo } }) items.push_back (it);
    return items;
}
static juce::String oscMoreTitle (ProProcessor& proc, const juce::String& pfx)
{
    const int e = gpro::eng::engineOf ((int) paramValue (proc, pfx + "tbl"));
    return e >= 0 ? "UNISON  /  WARP  /  SPECTRAL  /  ENGINE: " + gpro::eng::engineNames()[e] : "UNISON  /  WARP  /  SPECTRAL  /  ENGINE";
}
