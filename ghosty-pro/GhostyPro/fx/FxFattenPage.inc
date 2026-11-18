// FATTEN page: one huge knob + a puffing ghost, MODE + quick settings, INPUT, PUNCH, LOW CLEAN, glue + clip meter.
// Included by FxBoxPages.inc (Ghost Box) and by FxPanel.cpp (Ghosty Pro).
// ---------------------------------------------------------------- FATTEN
class FattenPage : public Page
{
public:
    FattenPage (juce::AudioProcessorValueTreeState& s, Rack::Impl& r) : apvts (s), rack (r)
    {
        const auto acc = accentOf (uFatten);
        big = std::make_unique<Knob> (s, pid (uFatten, "fat"), "", acc);
        big->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 18);
        color = std::make_unique<Knob> (s, pid (uFatten, "color"), "COLOR", acc);
        crunch = std::make_unique<Knob> (s, pid (uFatten, "crunch"), "CRUNCH", acc);
        input = std::make_unique<Knob> (s, pid (uFatten, "drive"), "INPUT", acc);
        mix = std::make_unique<Knob> (s, pid (uFatten, "mix"), "MIX", acc);
        out = std::make_unique<Knob> (s, pid (uFatten, "out"), "OUTPUT", acc);
        for (auto* c : { big.get(), color.get(), crunch.get(), input.get(), mix.get(), out.get() }) addAndMakeVisible (c);
        big->slider.setTooltip ("FAT: 0-30 % glue and weight, 30-70 % warm drive, 70-100 % heavy clip. Turn it and it gets fatter");
        color->slider.setTooltip ("Tone: left darker, right brighter (more upper harmonics)");
        crunch->slider.setTooltip ("Rougher, crunchier drive; above 30 % the loudest peaks fold back for grit");
        input->slider.setTooltip ("Pushes more level into the fattener: more drive, more squash. The output level is kept in check for you");
        mix->slider.setTooltip ("Blend the fattened sound with the dry sound");
        out->slider.setTooltip ("Output level of FATTEN");
        modeC = std::make_unique<Choice> (s, pid (uFatten, "mode"), acc);
        modeC->setTooltip ("What FAT does. CLASSIC: the original. SUB: an octave-down sub layer. BODY: a 140 Hz thump. SMASH: heavy parallel squash. "
                           "SATURATE: warm tape / tube drive. STACK: wide doubled copies. TRANSIENT: softer attack, longer tail. SAUSAGE: all of it, the maximum fat");
        addAndMakeVisible (*modeC);
        punchT = std::make_unique<Toggle> (s, pid (uFatten, "punch"), "PUNCH", acc);
        punchT->setTooltip ("Keeps the hit of drums: the attack of each hit passes through clean, the fat goes on the body");
        lowT = std::make_unique<Toggle> (s, pid (uFatten, "lowclean"), "LOW CLEAN", acc);
        lowT->setTooltip ("The deep sub (below about 90 Hz) skips the drive and stays clean and mono: no muddy bass");
        addAndMakeVisible (*punchT); addAndMakeVisible (*lowT);
        // quick settings: mode, fat, crunch, input, punch, low clean, colour
        struct Q { const char* name; const char* tip; float v[7]; };
        static const Q qs[] { { "LIGHT",    "A touch of weight and glue (CLASSIC, FAT 30 %)",                 { (float) fmClassic, 0.3f, 0.05f, 0.0f, 0, 0, 0.0f } },
                              { "THICK",    "Chunky body that keeps the punch (BODY, FAT 55 %, PUNCH)",         { (float) fmBody, 0.55f, 0.15f, 0.0f, 1, 0, 0.0f } },
                              { "SAUSAGE",  "Everything at once: loud, dense, maximum fat (SAUSAGE, FAT 85 %)", { (float) fmSausage, 0.85f, 0.35f, 3.0f, 0, 0, 0.0f } },
                              { "SUB BOMB", "Deep octave sub, clean low end, punchy hits (SUB, FAT 80 %)",      { (float) fmSub, 0.8f, 0.1f, 0.0f, 1, 1, -0.3f } },
                              { "CRUNCH",   "Driven and gritty (SATURATE, FAT 75 %, CRUNCH 70 %)",             { (float) fmSaturate, 0.75f, 0.7f, 6.0f, 0, 0, 0.2f } },
                              { "GLUE",     "Glued and even, still natural (SMASH, FAT 40 %)",                  { (float) fmSmash, 0.4f, 0.0f, 0.0f, 0, 0, 0.0f } } };
        for (auto& q : qs)
        {
            auto b = std::make_unique<juce::TextButton> (q.name);
            b->setTooltip (q.tip);
            b->setColour (juce::TextButton::buttonOnColourId, acc);
            const Q qq = q;
            b->onClick = [this, qq]
            {
                static const char* ids[] { "mode", "fat", "crunch", "drive", "punch", "lowclean", "color" };
                for (int k = 0; k < 7; ++k)
                    if (auto* p = apvts.getParameter (pid (uFatten, ids[k]))) { p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (qq.v[k])); p->endChangeGesture(); }
            };
            addAndMakeVisible (*b); quick.push_back (std::move (b));
        }
    }
    void resized() override
    {
        auto r = getLocalBounds();
        auto right = r.removeFromRight (230).withTrimmedLeft (10);
        meter = right.removeFromBottom (44);
        right.removeFromBottom (4);
        auto tg = right.removeFromBottom (24);
        punchT->setBounds (tg.removeFromLeft (tg.getWidth() / 2).reduced (2, 0)); lowT->setBounds (tg.reduced (2, 0));
        right.removeFromBottom (6);
        std::vector<juce::Component*> items { color.get(), crunch.get(), input.get(), mix.get(), out.get() };
        gridLayout (right, items, 3, juce::jmin (96, right.getHeight() / 2));
        // the strip on top: MODE + quick settings, then the mode's description line
        auto top = r.removeFromTop (26);
        modeC->setBounds (top.removeFromLeft (150).reduced (0, 1)); top.removeFromLeft (8);
        const int bw = juce::jmax (40, juce::jmin (84, (top.getWidth() - 10) / (int) quick.size()));
        for (auto& b : quick) { b->setBounds (top.removeFromLeft (bw).reduced (2, 1)); }
        blurb = r.removeFromTop (18).withTrimmedTop (3);
        r.removeFromTop (4);
        auto mid = r.removeFromRight (juce::jmin (330, r.getWidth() / 2));
        bigArea = mid;
        const int ks = juce::jmin (mid.getWidth() - 20, mid.getHeight() - 26);
        big->setBounds (mid.withSizeKeepingCentre (ks, ks + 18).translated (0, 6));
        vis = r.withTrimmedRight (10);
    }
    void paint (juce::Graphics& g) override
    {
        const auto acc = accentOf (uFatten);
        // big knob halo + caption
        auto ba = bigArea.toFloat();
        const float f = getParam (apvts, pid (uFatten, "fat"));
        juce::ColourGradient glow (acc.withAlpha (0.10f + 0.25f * f), ba.getCentreX(), ba.getCentreY(), acc.withAlpha (0.0f), ba.getCentreX() + ba.getWidth() * 0.55f, ba.getCentreY(), true);
        g.setGradientFill (glow); g.fillEllipse (ba.reduced (4));
        drawCaption (g, ba.removeFromTop (18), "TURN IT  -  IT GETS FATTER", acc, 12.0f, juce::Justification::centred);
        // mode description
        {
            const int md = juce::jlimit (0, (int) kNumFattenModes - 1, (int) std::lround (getParam (apvts, pid (uFatten, "mode"))));
            drawCaption (g, blurb.toFloat(), fattenModeNames()[md] + ":  " + fattenModeBlurb (md), col::dim.brighter (0.25f), 10.5f);
        }
        // gain reduction meter + the clip LED (green: clean, amber: driven, red: the ceiling is catching peaks)
        auto m = meter.toFloat();
        const float gr = grShown;
        auto cap = m.removeFromTop (14);
        drawCaption (g, cap, "GLUE  " + juce::String (gr, 1) + " dB", col::dim, 9.5f);
        {
            const float h = heatShown;
            const juce::Colour led = h >= 0.8f ? T (juce::Colour (0xffFF4D5E)) : h >= 0.5f ? T (juce::Colour (col::gold)) : T (juce::Colour (col::slime));
            auto lr = cap.removeFromRight (64);
            drawCaption (g, lr.withTrimmedRight (16), h >= 0.8f ? "CLIP" : "LEVEL", col::dim, 9.5f, juce::Justification::centredRight);
            glowDot (g, { lr.getRight() - 6.0f, lr.getCentreY() }, 3.5f, led.withAlpha (h > 0.02f ? 1.0f : 0.35f));
        }
        auto bars = m.reduced (0, 4);
        auto grBar = bars.removeFromTop (bars.getHeight() * 0.5f - 1.0f), heatBar = bars.withTrimmedTop (2.0f);
        g.setColour (T (juce::Colour (col::card2))); g.fillRoundedRectangle (grBar, 3); g.fillRoundedRectangle (heatBar, 3);
        g.setColour (acc.withAlpha (0.8f)); g.fillRoundedRectangle (grBar.withWidth (grBar.getWidth() * clampf (-gr / 18.0f, 0.0f, 1.0f)), 3);
        {
            const float db = gainToDb (heatShown + 1.0e-6f), w = clampf ((db + 30.0f) / 30.0f, 0.0f, 1.0f);
            const juce::Colour hc = heatShown >= 0.8f ? T (juce::Colour (0xffFF4D5E)) : heatShown >= 0.5f ? T (juce::Colour (col::gold)) : T (juce::Colour (col::slime));
            g.setColour (hc.withAlpha (0.85f)); g.fillRoundedRectangle (heatBar.withWidth (heatBar.getWidth() * w), 3);
        }
        paintGhost (g);
    }
    void paintGhost (juce::Graphics& g)
    {
        const auto acc = accentOf (uFatten);
        auto v = vis.toFloat();
        drawScreen (g, v, acc);
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (vis.reduced (2));
        const float f = rack.tm.fatAmount.load(), lvl = clampf (rack.tm.fatLevel.load() * 1.4f, 0.0f, 1.0f);
        const float cr = getParam (apvts, pid (uFatten, "crunch")), colr = getParam (apvts, pid (uFatten, "color"));
        const float cx = v.getCentreX(), cy = v.getCentreY() + 6;
        // puff rings that grow with FAT
        for (int k = 0; k < 6; ++k)
        {
            const float ph = (float) std::fmod (t * (0.35 + 0.5 * f) + k / 6.0, 1.0);
            const float rad = v.getHeight() * (0.18f + ph * (0.25f + 0.4f * f));
            g.setColour (acc.withAlpha ((1.0f - ph) * (0.05f + 0.22f * f)));
            g.drawEllipse (cx - rad * 1.2f, cy - rad, rad * 2.4f, rad * 2.0f, 2.0f + 4.0f * f);
        }
        // the ghost puffs up: wider + rounder with FAT, breathes with the level
        const float breathe = 1.0f + 0.04f * lvl * (float) std::sin (t * 9.0);
        const float sx = (1.0f + 0.65f * f) * breathe, sy = (1.0f + 0.25f * f) * breathe;
        auto L = ghosty::lookFor (4); L.slime = acc; L.mouth = f > 0.6f ? 3 : 1; L.eyes = f > 0.85f ? 3 : L.eyes;
        if (colr < -0.3f) L.body = T (juce::Colour (0xffEDE3FF)); else if (colr > 0.3f) L.body = T (juce::Colour (0xffFFF6E0));
        const float base = v.getHeight() * 0.42f;
        {
            juce::Graphics::ScopedSaveState s2 (g);
            g.addTransform (juce::AffineTransform::scale (sx, sy, cx, cy));
            ghosty::drawGhost (g, L, cx, cy, base, cr * lvl * 0.6f, false, 1.0f);
        }
        // rosy cheeks appear as it gets fat
        if (f > 0.25f)
        {
            const float a = clampf ((f - 0.25f) * 1.6f, 0.0f, 0.7f);
            const float ox = base * 0.42f * sx, oy = base * 0.10f;
            for (float sgn : { -1.0f, 1.0f })
            {
                juce::ColourGradient ck (col::pink.withAlpha (a), cx + sgn * ox * 0.55f, cy + oy, col::pink.withAlpha (0.0f), cx + sgn * ox * 0.55f + base * 0.16f, cy + oy, true);
                g.setGradientFill (ck); g.fillEllipse (cx + sgn * ox * 0.55f - base * 0.16f, cy + oy - base * 0.1f, base * 0.32f, base * 0.2f);
            }
        }
        // crunch sparks
        for (int k = 0; k < (int) (cr * 18.0f); ++k)
        {
            const float a = hashFloat ((uint32_t) k * 977u + (uint32_t) (t * 12.0)) * kTwoPi, d = base * (0.8f + 0.6f * hashFloat ((uint32_t) k * 31u + (uint32_t) (t * 12.0))) * sx;
            glowDot (g, { cx + std::cos (a) * d, cy + std::sin (a) * d * 0.7f }, 1.5f, col::gold.withAlpha (0.4f + 0.5f * lvl));
        }
        drawCaption (g, v.reduced (12, 8).removeFromTop (14), "FAT  " + juce::String (juce::roundToInt (f * 100.0f)) + "%", acc, 11);
    }
    void tick (double now) override
    {
        t = now; grShown = std::min (rack.tm.fatGr.exchange (0.0f), grShown * 0.85f);
        heatShown = std::max (rack.tm.fatHeat.exchange (0.0f), heatShown * 0.88f);
        const int md = (int) std::lround (getParam (apvts, pid (uFatten, "mode")));
        if (md != shownMode) { shownMode = md; repaint (blurb); }
        repaint (vis); repaint (meter);
    }
private:
    juce::AudioProcessorValueTreeState& apvts; Rack::Impl& rack;
    std::unique_ptr<Knob> big, color, crunch, input, mix, out; std::unique_ptr<Choice> modeC; std::unique_ptr<Toggle> punchT, lowT;
    std::vector<std::unique_ptr<juce::TextButton>> quick;
    juce::Rectangle<int> vis, bigArea, meter, blurb; double t = 0; float grShown = 0, heatShown = 0; int shownMode = -1;
};

