// Ghost Box unit pages (included by FxPanel.cpp inside its anonymous namespace, GPFX_BOX builds only):
// RETRO (six module cards + MAGIC), FATTEN (one huge knob + a puffing ghost), CRACKLE (draggable crossovers,
// per-band curves, speaker box) and HAUNTED HALLS (ghost mist that follows size / decay / freeze).

// ---------------------------------------------------------------- WARP
class WarpPage : public Page
{
public:
    WarpPage (juce::AudioProcessorValueTreeState& s, Rack::Impl& r) : apvts (s), rack (r), lane (r.warpShape),
        modeSeg (s, pid (uWarp, "mode"), accentOf (uWarp)), lenC (s, pid (uWarp, "length"), accentOf (uWarp)), rngC (s, pid (uWarp, "range"), accentOf (uWarp)),
        amount (s, pid (uWarp, "amount"), "AMOUNT", accentOf (uWarp)), formant (s, pid (uWarp, "formant"), "KEEP FORMANTS", accentOf (uWarp))
    {
        const auto acc = accentOf (uWarp);
        lane.accent = acc; lane.ghostLook = unitInfo (uWarp).look;
        lane.rangeSemis = [this] { return bendlane::rangeOf ((int) getParam (apvts, pid (uWarp, "range"))); };
        lane.bars = [this] { return bendlane::barsOf ((int) getParam (apvts, pid (uWarp, "length"))); };
        lane.amount = [this] { return getParam (apvts, pid (uWarp, "amount")); };
        lane.playPos = [this] { return rack.tm.warpPos.load(); };
        lane.onEditStart = [this] { rack.pushUndo(); };
        for (auto* c : std::initializer_list<juce::Component*> { &lane, &modeSeg, &lenC, &rngC, &amount, &formant, &snapBox, &gridBox, &shapes }) addAndMakeVisible (c);
        formant.setTooltip ("PITCH mode: keep the voice / instrument body where it is while the pitch moves");
        for (auto* c : { &snapBox, &gridBox }) { c->setColour (juce::ComboBox::outlineColourId, acc); c->setColour (juce::ComboBox::arrowColourId, acc); }
        snapBox.addItemList (bendlane::snapNames(), 1); gridBox.addItemList (bendlane::gridNames(), 1);
        snapBox.setSelectedItemIndex (lane.snap(), juce::dontSendNotification); gridBox.setSelectedItemIndex (lane.grid(), juce::dontSendNotification);
        snapBox.onChange = [this] { if (snapBox.getSelectedItemIndex() >= 0 && snapBox.getSelectedItemIndex() != lane.snap()) lane.setSnap (snapBox.getSelectedItemIndex()); };
        gridBox.onChange = [this] { if (gridBox.getSelectedItemIndex() >= 0 && gridBox.getSelectedItemIndex() != lane.grid()) lane.setGrid (gridBox.getSelectedItemIndex()); };
        shapes.setButtonText ("SHAPES..."); shapes.setColour (juce::TextButton::buttonOnColourId, acc);
        shapes.setTooltip ("Factory bend shapes (same list as the PRESET menu)");
        shapes.onClick = [this]
        {
            juce::PopupMenu m; m.addSectionHeader ("BEND SHAPES");
            const auto names = bendlane::factoryNames();
            for (int i = 0; i < names.size(); ++i) m.addItem (i + 1, names[i]);
            juce::Component::SafePointer<WarpPage> safe (this);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&shapes), [safe] (int res)
            { if (safe != nullptr && res > 0) { safe->rack.applyUnitPreset (uWarp, res - 1); safe->repaint(); } });
        };
    }
    void resized() override
    {
        auto r = getLocalBounds();
        auto right = r.removeFromRight (230).withTrimmedLeft (10);
        auto row = r.removeFromBottom (34).withTrimmedTop (4);
        lane.setBounds (r.withTrimmedRight (2).withTrimmedBottom (2));
        modeSeg.setBounds (row.removeFromLeft (170)); row.removeFromLeft (8);
        lenC.setBounds (row.removeFromLeft (100).reduced (0, 1)); row.removeFromLeft (6);
        rngC.setBounds (row.removeFromLeft (92).reduced (0, 1)); row.removeFromLeft (6);
        snapBox.setBounds (row.removeFromLeft (118).reduced (0, 1)); row.removeFromLeft (6);
        gridBox.setBounds (row.removeFromLeft (102).reduced (0, 1));
        vis = right.removeFromTop (juce::jmax (110, right.getHeight() * 45 / 100));
        right.removeFromTop (8);
        shapes.setBounds (right.removeFromTop (28)); right.removeFromTop (6);
        formant.setBounds (right.removeFromTop (26)); right.removeFromTop (4);
        info = right.removeFromBottom (16);
        amount.setBounds (right.withSizeKeepingCentre (juce::jmin (right.getWidth(), 110), right.getHeight()));
    }
    void paint (juce::Graphics& g) override
    {
        const auto acc = accentOf (uWarp);
        auto v = vis.toFloat();
        drawScreen (g, v, acc);
        {
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (vis.reduced (2));
            const bool on = getParam (apvts, pid (uWarp, "on")) > 0.5f;
            const float range = bendlane::rangeOf ((int) getParam (apvts, pid (uWarp, "range")));
            const float st = on && rack.tm.warpPos.load() >= 0 ? rack.tm.warpSemis.load() : 0.0f;
            const float k = juce::jlimit (-1.0f, 1.0f, st / range);
            const float cx = v.getCentreX(), cy = v.getCentreY() + 4 - k * v.getHeight() * 0.22f;
            const bool tape = (int) getParam (apvts, pid (uWarp, "mode")) == 1;
            // speed lines: up = streaks above, down = below
            for (int i = 0; i < 7; ++i)
            {
                const float ph = (float) std::fmod (t * (0.6 + std::abs (k) * 2.0) + i / 7.0, 1.0);
                const float x = v.getX() + 10 + (v.getWidth() - 20) * ph, yy = cy + (k >= 0 ? 1.0f : -1.0f) * (v.getHeight() * 0.18f + 6.0f * (float) (i % 3));
                g.setColour (acc.withAlpha ((0.08f + 0.35f * std::abs (k)) * (1.0f - ph)));
                g.fillRoundedRectangle (x, yy, 18.0f + 30.0f * std::abs (k), 2.0f, 1.0f);
            }
            auto L = ghosty::lookFor (unitInfo (uWarp).look); L.slime = acc;
            if (k > 0.5f) L.mouth = 3; else if (k < -0.5f) L.eyes = 3;
            const float stretch = tape ? 1.0f + 0.25f * k : 1.0f;
            juce::Graphics::ScopedSaveState s2 (g);
            g.addTransform (juce::AffineTransform::scale (1.0f / stretch, stretch, cx, cy));
            ghosty::drawGhost (g, L, cx, cy, v.getHeight() * 0.40f, std::abs (k) > 0.9f ? 0.25f : 0.0f, false, on ? 1.0f : 0.5f, k < 0);
        }
        const float st = rack.tm.warpSemis.load();
        drawCaption (g, v.reduced (10, 6).removeFromTop (14), (st > 0.05f ? "+" : "") + juce::String (st, 1) + " st", acc, 11);
        const bool tape = (int) getParam (apvts, pid (uWarp, "mode")) == 1;
        drawCaption (g, info.toFloat(), tape ? "TAPE LAG " + juce::String (juce::roundToInt (rack.tm.warpLagMs.load())) + " ms (0 when the line dips first)"
                                             : "PITCH LATENCY " + juce::String (juce::roundToInt (rack.tm.warpLagMs.load())) + " ms (compensated)", col::dim, 9.0f);
    }
    void tick (double now) override
    {
        t = now; repaint (vis); repaint (info);
        const bool tape = (int) getParam (apvts, pid (uWarp, "mode")) == 1;
        formant.setEnabled (! tape); formant.setAlpha (tape ? 0.4f : 1.0f);
        if (snapBox.getSelectedItemIndex() != lane.snap()) snapBox.setSelectedItemIndex (lane.snap(), juce::dontSendNotification);
        if (gridBox.getSelectedItemIndex() != lane.grid()) gridBox.setSelectedItemIndex (lane.grid(), juce::dontSendNotification);
    }
private:
    juce::AudioProcessorValueTreeState& apvts; Rack::Impl& rack;
    bendlane::Editor lane; Segmented modeSeg; Choice lenC, rngC; Knob amount; Toggle formant;
    juce::ComboBox snapBox, gridBox; juce::TextButton shapes;
    juce::Rectangle<int> vis, info; double t = 0;
};

// ---------------------------------------------------------------- RETRO
class RetroPage : public Page
{
public:
    RetroPage (juce::AudioProcessorValueTreeState& s, Rack::Impl& r) : apvts (s), rack (r)
    {
        const auto acc = accentOf (uRetro);
        magicK = std::make_unique<Knob> (s, pid (uRetro, "magic"), "MAGIC / AGE", acc);
        toneK = std::make_unique<Knob> (s, pid (uRetro, "tone"), "TONE", acc);
        ntoneK = std::make_unique<Knob> (s, pid (uRetro, "ntone"), "NOISE TONE", acc);
        outK = std::make_unique<Knob> (s, pid (uRetro, "out"), "OUTPUT", acc);
        magicK->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 16);
        for (auto* c : { magicK.get(), toneK.get(), ntoneK.get(), outK.get() }) addAndMakeVisible (c);
        static const char* names[6] { "NOISE", "WOBBLE", "DISTORT", "DIGITAL", "SPACE", "MAGNETIC" };
        static const char* onIds[6] { "noise_on", "wobble_on", "dist_on", "digi_on", "space_on", "mag_on" };
        static const char* amtIds[6] { "noise", "wobble", "dist", "digi", "space", "mag" };
        static const char* blurbs[6] { "", "", "", "bits + sample rate", "small character room", "head bump + dropouts" };
        for (int k = 0; k < 6; ++k)
        {
            auto& m = mods[(size_t) k];
            m.name = names[k]; m.blurb = blurbs[k]; m.onId = pid (uRetro, onIds[k]);
            m.on = std::make_unique<Toggle> (s, pid (uRetro, onIds[k]), names[k], acc);
            m.amt = std::make_unique<Knob> (s, pid (uRetro, amtIds[k]), "AMOUNT", acc);
            addAndMakeVisible (*m.on); addAndMakeVisible (*m.amt);
        }
        ntype = std::make_unique<Choice> (s, pid (uRetro, "ntype"), acc);
        follow = std::make_unique<Toggle> (s, pid (uRetro, "nfollow"), "FOLLOW INPUT", acc);
        dtype = std::make_unique<Choice> (s, pid (uRetro, "dtype"), acc);
        flutter = std::make_unique<Knob> (s, pid (uRetro, "flutter"), "FLUTTER", acc);
        flutter->slider.setSliderStyle (juce::Slider::LinearHorizontal);
        flutter->slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        flutter->caption = "FLUT";
        follow->setTooltip ("The noise rises and falls with the input level");
        for (auto* c : std::initializer_list<juce::Component*> { ntype.get(), follow.get(), dtype.get(), flutter.get() }) addAndMakeVisible (c);
    }
    void resized() override
    {
        auto r = getLocalBounds();
        auto right = r.removeFromRight (juce::jlimit (200, 260, r.getWidth() / 5)).withTrimmedLeft (10);
        auto smalls = right.removeFromBottom (92);
        const int w3 = smalls.getWidth() / 3;
        toneK->setBounds (smalls.removeFromLeft (w3)); ntoneK->setBounds (smalls.removeFromLeft (w3)); outK->setBounds (smalls);
        right.removeFromBottom (6);
        magicArea = right;
        auto mk = right.withTrimmedBottom (16);
        magicK->setBounds (mk.withSizeKeepingCentre (juce::jmin (mk.getWidth(), mk.getHeight()), mk.getHeight()));
        cardsArea = r.removeFromBottom (juce::jmin (206, r.getHeight() / 2 + 20));
        r.removeFromBottom (6);
        vis = r;
        const int cw = cardsArea.getWidth() / 6;
        for (int k = 0; k < 6; ++k)
        {
            auto c = juce::Rectangle<int> (cardsArea.getX() + k * cw, cardsArea.getY(), cw, cardsArea.getHeight()).reduced (4, 2);
            mods[(size_t) k].card = c;
            auto in = c.reduced (8, 6);
            mods[(size_t) k].on->setBounds (in.removeFromTop (26));
            in.removeFromTop (2);
            mods[(size_t) k].amt->setBounds (in.removeFromTop (juce::jmin (100, in.getHeight() - 56)));
            in.removeFromTop (4);
            if (k == 0) { ntype->setBounds (in.removeFromTop (24)); in.removeFromTop (4); follow->setBounds (in.removeFromTop (24)); }
            if (k == 1) flutter->setBounds (in.removeFromTop (26));
            if (k == 2) dtype->setBounds (in.removeFromTop (24));
            mods[(size_t) k].extra = in;
        }
    }
    void paint (juce::Graphics& g) override
    {
        const auto acc = accentOf (uRetro);
        // module cards
        for (int k = 0; k < 6; ++k)
        {
            const auto& m = mods[(size_t) k];
            const bool on = getParam (apvts, m.onId) > 0.5f;
            auto c = m.card.toFloat();
            juce::ColourGradient bg (on ? T (juce::Colour (0xff221d16)) : T (juce::Colour (0xff14151b)), 0, c.getY(), T (juce::Colour (0xff0e0f14)), 0, c.getBottom(), false);
            g.setGradientFill (bg); g.fillRoundedRectangle (c, 10);
            g.setColour (acc.withAlpha (on ? 0.5f : 0.12f)); g.drawRoundedRectangle (c.reduced (0.5f), 10, 1.0f);
            if (m.blurb[0] != 0) drawCaption (g, m.extra.toFloat().withHeight (14), m.blurb, col::dim.withAlpha (on ? 1.0f : 0.5f), 9.5f, juce::Justification::centred);
        }
        // MAGIC glow
        auto ma = magicArea.toFloat();
        const float mg = getParam (apvts, pid (uRetro, "magic"));
        juce::ColourGradient glow (acc.withAlpha (0.08f + 0.18f * mg), ma.getCentreX(), ma.getCentreY(), acc.withAlpha (0.0f), ma.getCentreX() + ma.getWidth() * 0.6f, ma.getCentreY(), true);
        g.setGradientFill (glow); g.fillEllipse (ma);
        drawCaption (g, ma.removeFromBottom (13), "moves every module at once", col::dim, 9.0f, juce::Justification::centred);
        paintScreen (g);
    }
    void paintScreen (juce::Graphics& g)
    {
        auto v = vis.toFloat();
        const auto acc = accentOf (uRetro);
        const float wow = rack.tm.retroWow.load(), nz = clampf (rack.tm.retroNoise.load() * 30.0f, 0.0f, 1.0f), drop = rack.tm.retroDrop.load();
        const float crush = rack.tm.retroCrush.load(), lvl = clampf (rack.tm.retroLevel.load() * 1.5f, 0.0f, 1.0f);
        const bool unitOn = getParam (apvts, pid (uRetro, "on")) > 0.5f;
        // old TV / tape-machine screen: sepia glass, vignette
        g.setColour (T (juce::Colour (0xff0a0806))); g.fillRoundedRectangle (v, 16);
        auto scr = v.reduced (10);
        juce::ColourGradient sep (T (juce::Colour (0xff3a2e1f)).withMultipliedBrightness (0.6f + 0.4f * drop), scr.getCentreX(), scr.getCentreY(),
                                  T (juce::Colour (0xff0d0a07)), scr.getX(), scr.getY(), true);
        g.setGradientFill (sep); g.fillRoundedRectangle (scr, 22);
        {
            juce::Graphics::ScopedSaveState ss (g);
            juce::Path clip; clip.addRoundedRectangle (scr, 22); g.reduceClipRegion (clip);
            // the ghost, wobbling with the wow and dimming in dropouts
            const float gx = scr.getCentreX() + wow * 26.0f, gy = scr.getCentreY() + std::sin ((float) t * 1.7f) * 6.0f;
            auto L = ghosty::lookFor (8); L.body = T (juce::Colour (0xffF3E3C3)); L.slime = acc;
            const float gsz = scr.getHeight() * (0.46f + 0.05f * lvl);
            if (crush > 0.02f)
            {   // digital: draw it blocky
                const int bs = 2 + (int) (crush * 9.0f);
                juce::Image img (juce::Image::ARGB, juce::jmax (1, (int) (gsz * 1.4f) / bs), juce::jmax (1, (int) (gsz * 1.4f) / bs), true);
                { juce::Graphics ig (img); ghosty::drawGhost (ig, L, img.getWidth() * 0.5f, img.getHeight() * 0.5f, gsz / (float) bs, 0, false, 0.92f * drop); }
                g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
                g.drawImage (img, juce::Rectangle<float> (gsz * 1.4f, gsz * 1.4f).withCentre ({ gx, gy }));
            }
            else ghosty::drawGhost (g, L, gx, gy, gsz, 0, false, 0.92f * drop);
            // scan lines + rolling bar
            for (float y = scr.getY(); y < scr.getBottom(); y += 3.0f) { g.setColour (juce::Colours::black.withAlpha (0.18f)); g.fillRect (scr.getX(), y, scr.getWidth(), 1.0f); }
            const float bar = scr.getY() + (float) std::fmod (t * 40.0, (double) scr.getHeight() + 60.0) - 30.0f;
            g.setColour (T (juce::Colours::white).withAlpha (0.03f + 0.04f * std::abs (wow))); g.fillRect (scr.getX(), bar, scr.getWidth(), 22.0f);
            // dust, scratches and crackle specks (more with more noise)
            const int specks = unitOn ? 12 + (int) (nz * 140.0f) : 6;
            const uint32_t frame = (uint32_t) (t * 30.0);
            for (int k = 0; k < specks; ++k)
            {
                const float x = hashFloat ((uint32_t) k * 7919u + frame * 31u), y = hashFloat ((uint32_t) k * 104729u + frame * 17u);
                const float a = 0.25f + 0.6f * hashFloat ((uint32_t) k * 13u + frame);
                g.setColour ((k % 3 ? T (juce::Colour (0xffF8EAD0)) : juce::Colours::black).withAlpha (a * (0.4f + 0.6f * nz)));
                const float sz = 1.0f + 2.5f * hashFloat ((uint32_t) k * 3u + frame * 5u);
                g.fillEllipse (scr.getX() + x * scr.getWidth(), scr.getY() + y * scr.getHeight(), sz, sz);
            }
            if (nz > 0.3f && (frame % 7) < 2)
            {
                const float x = scr.getX() + hashFloat (frame * 911u) * scr.getWidth();
                g.setColour (T (juce::Colour (0xffF8EAD0)).withAlpha (0.25f)); g.drawLine (x, scr.getY(), x + 6, scr.getBottom(), 0.8f);
            }
            juce::ColourGradient vig (juce::Colours::transparentBlack, scr.getCentreX(), scr.getCentreY(), juce::Colours::black.withAlpha (0.65f), scr.getX(), scr.getY(), true);
            g.setGradientFill (vig); g.fillRect (scr);
        }
        g.setColour (acc.withAlpha (0.35f)); g.drawRoundedRectangle (scr, 22, 1.5f);
        drawCaption (g, scr.reduced (16, 10).removeFromTop (14), "RETRO", acc.withAlpha (0.9f), 11);
        juce::String live; for (int k = 0; k < 6; ++k) if (getParam (apvts, mods[(size_t) k].onId) > 0.5f) live << (live.isEmpty() ? "" : "  ") << mods[(size_t) k].name;
        drawCaption (g, scr.reduced (16, 10).removeFromBottom (14), live, col::white.withAlpha (0.45f), 9.5f, juce::Justification::centredRight);
    }
    void tick (double now) override { t = now; repaint (vis); }
private:
    struct Mod { const char* name = ""; const char* blurb = ""; juce::String onId; std::unique_ptr<Toggle> on; std::unique_ptr<Knob> amt; juce::Rectangle<int> card, extra; };
    juce::AudioProcessorValueTreeState& apvts; Rack::Impl& rack;
    std::array<Mod, 6> mods; std::unique_ptr<Knob> magicK, toneK, ntoneK, outK, flutter; std::unique_ptr<Choice> ntype, dtype; std::unique_ptr<Toggle> follow;
    juce::Rectangle<int> vis, cardsArea, magicArea; double t = 0;
};

#include "FxFattenPage.inc"   // FATTEN page (shared with Ghosty Pro)

// ---------------------------------------------------------------- CRACKLE
class CrossoverView : public juce::Component
{
public:
    CrossoverView (juce::AudioProcessorValueTreeState& s, Rack::Impl& r) : apvts (s), rack (r) {}
    static float xOf (float hz, float w) { return w * std::log (hz / 20.0f) / std::log (1000.0f); }
    static float hzOf (float x, float w) { return 20.0f * std::pow (1000.0f, clampf (x / w, 0.0f, 1.0f)); }
    double t = 0;
    void paint (juce::Graphics& g) override
    {
        const auto acc = accentOf (uCrackle);
        auto a = getLocalBounds().toFloat();
        drawScreen (g, a, acc);
        const float W = a.getWidth(), H = a.getHeight();
        const int nb = (int) getParam (apvts, pid (uCrackle, "bands")) + 1;
        const float lo = getParam (apvts, pid (uCrackle, "xlo")), hi = std::max (getParam (apvts, pid (uCrackle, "xhi")), lo * 1.5f);
        const float x1 = nb >= 2 ? xOf (lo, W) : W, x2 = nb >= 3 ? xOf (hi, W) : W;
        const float edges[4] { 0.0f, x1, nb >= 3 ? x2 : W, W };
        static const juce::Colour bc[3] { T (juce::Colour (0xffFF6A3D)), T (juce::Colour (0xffFFC35A)), T (juce::Colour (0xffFF4FD8)) };
        static const char* bn[3] { "LOW", "MID", "HIGH" };
        for (int f : { 100, 1000, 10000 })
        {
            const float x = xOf ((float) f, W);
            g.setColour (col::white.withAlpha (0.05f)); g.drawVerticalLine ((int) x, 0, H);
            drawCaption (g, { x + 3, H - 13, 40, 12 }, f >= 1000 ? juce::String (f / 1000) + "k" : juce::String (f), col::dim, 9);
        }
        for (int b = 0; b < nb; ++b)
        {
            const float xa = edges[b], xb = b == nb - 1 ? W : edges[b + 1];
            auto br = juce::Rectangle<float> (xa, 0, xb - xa, H).reduced (2, 2);
            const float lvl = clampf (rack.tm.crackleBand[(size_t) b].load() * 1.5f, 0.0f, 1.0f);
            const float drv = getParam (apvts, pid (uCrackle, ("b" + juce::String (b + 1) + "_drive").toRawUTF8()));
            const int cv = (int) getParam (apvts, pid (uCrackle, ("b" + juce::String (b + 1) + "_curve").toRawUTF8()));
            juce::ColourGradient gr (bc[b].withAlpha (0.10f + 0.30f * lvl), 0, br.getBottom(), bc[b].withAlpha (0.02f), 0, br.getY(), false);
            g.setGradientFill (gr); g.fillRoundedRectangle (br, 8);
            g.setColour (bc[b].withAlpha (0.8f)); g.fillRoundedRectangle (br.getX() + 6, br.getBottom() - 6 - (br.getHeight() - 30) * lvl, 4, (br.getHeight() - 30) * lvl, 2);
            const float gs = juce::jmin (br.getHeight() * 0.5f, br.getWidth() * 0.55f);
            const float shake = lvl * drv * 5.0f;
            auto L = ghosty::lookFor (6 + b * 5); L.slime = bc[b];
            ghosty::drawGhost (g, L, br.getCentreX() + std::sin ((float) t * 37.0f + b) * shake, br.getCentreY() + 4 + std::cos ((float) t * 29.0f + b) * shake,
                               gs, clampf (lvl * drv * 1.2f, 0.0f, 1.0f), false, 0.9f);
            drawCaption (g, br.reduced (8, 6).removeFromTop (13), juce::String (bn[nb == 1 ? 1 : (nb == 2 ? b * 2 : b)]) + "  " + crackleCurveNames()[cv], bc[b], 10);
        }
        for (int k = 0; k < nb - 1; ++k)
        {
            const float x = k == 0 ? x1 : x2;
            g.setColour (col::white.withAlpha (k == hot ? 0.9f : 0.5f)); g.fillRect (x - 1.0f, 0.0f, 2.0f, H);
            glowDot (g, { x, H * 0.5f }, k == hot ? 6.0f : 4.5f, col::white);
            drawCaption (g, { x + 6, 22, 70, 12 }, juce::String ((int) (k == 0 ? lo : hi)) + " Hz", col::white.withAlpha (0.7f), 9.5f);
        }
        if (nb > 1) drawCaption (g, a.reduced (10, 5).removeFromTop (12), "drag the white lines to move the crossovers", col::dim, 9, juce::Justification::centredRight);
    }
    int handleAt (float x) const
    {
        const int nb = (int) getParam (apvts, pid (uCrackle, "bands")) + 1; const float W = (float) getWidth();
        const float lo = getParam (apvts, pid (uCrackle, "xlo")), hi = std::max (getParam (apvts, pid (uCrackle, "xhi")), lo * 1.5f);
        if (nb >= 3 && std::abs (x - xOf (hi, W)) < 9) return 1;
        if (nb >= 2 && std::abs (x - xOf (lo, W)) < 9) return 0;
        return -1;
    }
    void mouseMove (const juce::MouseEvent& e) override { const int h = handleAt (e.position.x); if (h != hot) { hot = h; repaint(); } setMouseCursor (h >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        drag = handleAt (e.position.x);
        if (drag < 0) return;
        if (auto* p = apvts.getParameter (pid (uCrackle, drag == 0 ? "xlo" : "xhi"))) { param = p; p->beginChangeGesture(); }
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drag < 0 || param == nullptr) return;
        param->setValueNotifyingHost (param->convertTo0to1 (hzOf (e.position.x, (float) getWidth())));
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override { if (param) param->endChangeGesture(); param = nullptr; drag = -1; }
private:
    juce::AudioProcessorValueTreeState& apvts; Rack::Impl& rack; int drag = -1, hot = -1; juce::RangedAudioParameter* param = nullptr;
};

class CracklePage : public Page
{
public:
    CracklePage (juce::AudioProcessorValueTreeState& s, Rack::Impl& r) : apvts (s), rack (r), xo (s, r)
    {
        const auto acc = accentOf (uCrackle);
        bandsSeg = std::make_unique<Segmented> (s, pid (uCrackle, "bands"), acc);
        boxC = std::make_unique<Choice> (s, pid (uCrackle, "box"), acc);
        boxMix = std::make_unique<Knob> (s, pid (uCrackle, "boxmix"), "BOX MIX", acc);
        boxMix->slider.setSliderStyle (juce::Slider::LinearHorizontal); boxMix->slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 42, 15);
        boxMix->caption = "MIX";
        autoT = std::make_unique<Toggle> (s, pid (uCrackle, "auto"), "AUTO GAIN", acc);
        autoT->setTooltip ("Keeps the output level close to the input level however hard you drive it");
        static const char* fid[4] { "prehp", "prelp", "posthp", "postlp" };
        static const char* fcap[4] { "PRE LOW CUT", "PRE HIGH CUT", "POST LOW CUT", "POST HIGH CUT" };
        for (int k = 0; k < 4; ++k) filt[(size_t) k] = std::make_unique<Knob> (s, pid (uCrackle, fid[k]), fcap[k], acc);
        outK = std::make_unique<Knob> (s, pid (uCrackle, "out"), "OUTPUT", acc);
        for (int b = 0; b < 3; ++b)
        {
            const juce::String pre = "b" + juce::String (b + 1) + "_";
            auto& bd = bands[(size_t) b];
            bd.curve = std::make_unique<Choice> (s, pid (uCrackle, (pre + "curve").toRawUTF8()), bandCol (b));
            bd.drive = std::make_unique<Knob> (s, pid (uCrackle, (pre + "drive").toRawUTF8()), "DRIVE", bandCol (b));
            bd.mix = std::make_unique<Knob> (s, pid (uCrackle, (pre + "mix").toRawUTF8()), "MIX", bandCol (b));
            bd.level = std::make_unique<Knob> (s, pid (uCrackle, (pre + "level").toRawUTF8()), "LEVEL", bandCol (b));
            for (auto* c : std::initializer_list<juce::Component*> { bd.curve.get(), bd.drive.get(), bd.mix.get(), bd.level.get() }) addAndMakeVisible (c);
        }
        for (auto* c : std::initializer_list<juce::Component*> { bandsSeg.get(), boxC.get(), boxMix.get(), autoT.get(), outK.get(), &xo }) addAndMakeVisible (c);
        for (auto& f : filt) addAndMakeVisible (*f);
    }
    static juce::Colour bandCol (int b) { static const juce::Colour c[3] { T (juce::Colour (0xffFF6A3D)), T (juce::Colour (0xffFFC35A)), T (juce::Colour (0xffFF4FD8)) }; return c[b]; }
    void resized() override
    {
        auto r = getLocalBounds();
        auto right = r.removeFromRight (200).withTrimmedLeft (10);
        outK->setBounds (right.removeFromBottom (92).withSizeKeepingCentre (96, 92));
        std::vector<juce::Component*> items; for (auto& f : filt) items.push_back (f.get());
        gridLayout (right, items, 2, juce::jmin (96, right.getHeight() / 2));
        auto top = r.removeFromTop (32);
        bandsSeg->setBounds (top.removeFromLeft (juce::jmin (300, top.getWidth() / 3)).reduced (0, 1));
        top.removeFromLeft (14);
        boxLabel = top.removeFromLeft (34);
        boxC->setBounds (top.removeFromLeft (170).reduced (0, 4));
        top.removeFromLeft (10);
        autoT->setBounds (top.removeFromRight (96).reduced (0, 3));
        top.removeFromRight (10);
        boxMix->setBounds (top.removeFromLeft (juce::jmin (200, top.getWidth())).reduced (0, 6));
        r.removeFromTop (6);
        auto bottom = r.removeFromBottom (juce::jmin (176, r.getHeight() / 2 + 10));
        r.removeFromBottom (6);
        xo.setBounds (r);
        const int bw = bottom.getWidth() / 3;
        for (int b = 0; b < 3; ++b)
        {
            auto c = juce::Rectangle<int> (bottom.getX() + b * bw, bottom.getY(), bw, bottom.getHeight()).reduced (4, 0);
            auto& bd = bands[(size_t) b]; bd.card = c;
            auto in = c.reduced (8, 6);
            auto head = in.removeFromTop (24);
            head.removeFromLeft (52);
            bd.curve->setBounds (head);
            in.removeFromTop (4);
            auto kn = in.removeFromTop (juce::jmin (96, in.getHeight()));
            const int kw = kn.getWidth() / 3;
            bd.drive->setBounds (kn.removeFromLeft (kw)); bd.mix->setBounds (kn.removeFromLeft (kw)); bd.level->setBounds (kn);
            bd.curveArea = in;
        }
    }
    void paint (juce::Graphics& g) override
    {
        drawCaption (g, boxLabel.toFloat(), "BOX", accentOf (uCrackle), 11);
        const int nb = (int) getParam (apvts, pid (uCrackle, "bands")) + 1;
        static const char* bn[3] { "LOW", "MID", "HIGH" };
        for (int b = 0; b < 3; ++b)
        {
            const auto& bd = bands[(size_t) b];
            const bool act = b < nb;
            auto c = bd.card.toFloat();
            g.setColour (T (juce::Colour (act ? 0xff17141a : 0xff111217))); g.fillRoundedRectangle (c, 10);
            g.setColour (bandCol (b).withAlpha (act ? 0.45f : 0.10f)); g.drawRoundedRectangle (c.reduced (0.5f), 10, 1.0f);
            drawCaption (g, c.reduced (10, 6).withHeight (24), act ? juce::String (bn[nb == 1 ? 1 : (nb == 2 ? b * 2 : b)]) : juce::String ("OFF"), bandCol (b).withAlpha (act ? 1.0f : 0.35f), 11);
            // mini transfer curve of this band's shape at its drive
            auto a = bd.curveArea.toFloat().reduced (4, 2);
            if (a.getHeight() > 10 && act)
            {
                const int cv = (int) getParam (apvts, pid (uCrackle, ("b" + juce::String (b + 1) + "_curve").toRawUTF8()));
                const float drv = getParam (apvts, pid (uCrackle, ("b" + juce::String (b + 1) + "_drive").toRawUTF8()));
                const float gn = crackleUsesGain (cv) ? dbToGain (drv * 40.0f) : 1.0f;
                juce::Path p;
                for (int i = 0; i <= 80; ++i)
                {
                    const float x = -1.0f + 2.0f * (float) i / 80.0f;
                    float y;
                    if (cv == ccBitcrush) { const float q = std::pow (2.0f, 15.0f - 14.0f * drv); y = std::round (x * q) / q; }
                    else if (cv == ccDownsample) y = std::round (x * 6.0f) / 6.0f;
                    else y = crackleStatic (cv, x * gn, drv);
                    const juce::Point<float> pt (a.getX() + a.getWidth() * (float) i / 80.0f, a.getCentreY() - clampf (y, -1.2f, 1.2f) * a.getHeight() * 0.42f);
                    if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
                }
                g.setColour (col::white.withAlpha (0.05f)); g.drawHorizontalLine ((int) a.getCentreY(), a.getX(), a.getRight());
                glowStroke (g, p, bandCol (b).withAlpha (0.85f), 1.5f, 0.6f);
            }
        }
    }
    void tick (double now) override
    {
        t = now; xo.t = now; xo.repaint();
        const int nb = (int) getParam (apvts, pid (uCrackle, "bands")) + 1;
        if (nb != lastNb)
        {
            lastNb = nb;
            for (int b = 0; b < 3; ++b) for (auto* c : std::initializer_list<juce::Component*> { bands[(size_t) b].curve.get(), bands[(size_t) b].drive.get(), bands[(size_t) b].mix.get(), bands[(size_t) b].level.get() })
            { c->setEnabled (b < nb); c->setAlpha (b < nb ? 1.0f : 0.35f); }
            repaint();
        }
        if (++frame % 10 == 0) repaint();   // curves follow drive / curve changes
    }
private:
    struct Band { std::unique_ptr<Choice> curve; std::unique_ptr<Knob> drive, mix, level; juce::Rectangle<int> card, curveArea; };
    juce::AudioProcessorValueTreeState& apvts; Rack::Impl& rack; CrossoverView xo;
    std::unique_ptr<Segmented> bandsSeg; std::unique_ptr<Choice> boxC; std::unique_ptr<Knob> boxMix, outK; std::unique_ptr<Toggle> autoT;
    std::array<std::unique_ptr<Knob>, 4> filt; std::array<Band, 3> bands; juce::Rectangle<int> boxLabel; double t = 0; int lastNb = -1, frame = 0;
};

// ---------------------------------------------------------------- HAUNTED HALLS visual: ghost mist
struct MistState { double frozenT = -1; };
void drawHallsMist (juce::Graphics& g, juce::Rectangle<float> r, double t, Rack::Impl& rk, juce::AudioProcessorValueTreeState& s, MistState& ms)
{
    const auto acc = accentOf (uHalls);
    const float size = getParam (s, pid (uHalls, "size")), decay = getParam (s, pid (uHalls, "decay")), pre = getParam (s, pid (uHalls, "predelay"));
    const int mode = (int) getParam (s, pid (uHalls, "mode"));
    const bool fz = getParam (s, pid (uHalls, "freeze")) > 0.5f;
    const float sh = getParam (s, pid (uHalls, "shimmer")), dmp = getParam (s, pid (uHalls, "damp"));
    const float lvl = clampf (rk.tm.hallsLevel.load() * 3.0f, 0.0f, 1.0f);
    if (fz && ms.frozenT < 0) ms.frozenT = t; else if (! fz) ms.frozenT = -1;
    const double tt = fz ? ms.frozenT + (t - ms.frozenT) * 0.04 : t;
    const juce::Colour tint = fz ? col::ice : (mode == hmDarkTape ? T (juce::Colour (0xffB08A5A)) : (mode == hmGalaxy ? T (juce::Colour (0xffC9A6FF)) : acc));
    const auto ctr = r.getCentre();
    // the room: its walls sit as far out as SIZE
    {
        const float rw = r.getWidth() * (0.28f + 0.2f * size), rh = r.getHeight() * (0.26f + 0.16f * size);
        juce::Path room; room.addRoundedRectangle (ctr.x - rw, ctr.y - rh, rw * 2.0f, rh * 2.0f, 30.0f);
        const float dl[] { 6.0f, 6.0f };
        juce::Path dashed; juce::PathStrokeType (1.2f).createDashedStroke (dashed, room, dl, 2);
        g.setColour (tint.withAlpha (0.18f + 0.2f * lvl)); g.fillPath (dashed);
    }
    // mist puffs: they spread as far as SIZE and live as long as DECAY
    const float life = 0.8f + 1.6f * std::log2 (1.0f + decay);
    const int count = 34;
    for (int k = 0; k < count; ++k)
    {
        const float h1 = hashFloat ((uint32_t) k * 7919u), h2 = hashFloat ((uint32_t) k * 104729u), h3 = hashFloat ((uint32_t) k * 31u);
        const float ph = (float) std::fmod (tt / life + h1, 1.0);
        const float ang = h2 * kTwoPi + (float) tt * 0.05f * (h3 - 0.5f);
        const float spread = (0.12f + ph * (0.35f + 0.6f * size));
        const float px = ctr.x + std::cos (ang) * spread * r.getWidth() * 0.5f, py = ctr.y + std::sin (ang) * spread * r.getHeight() * 0.42f;
        const float rad = r.getHeight() * (0.09f + 0.22f * ph) * (0.7f + 0.6f * size);
        const float a = (1.0f - ph) * std::min (1.0f, ph * 6.0f) * (0.16f + 0.40f * lvl + (fz ? 0.18f : 0.0f));
        const juce::Colour c = (k % 4 == 0 ? col::white : tint).withMultipliedBrightness (1.0f - 0.4f * dmp);
        juce::ColourGradient gr (c.withAlpha (a), px, py, c.withAlpha (0.0f), px + rad, py, true);
        g.setGradientFill (gr); g.fillEllipse (px - rad, py - rad, rad * 2.0f, rad * 2.0f);
    }
    // shimmer sparkles rise
    for (int k = 0; k < (int) (sh * 40.0f); ++k)
    {
        const float h = hashFloat ((uint32_t) k * 977u);
        const float y = (float) std::fmod (h + tt * (0.06 + 0.08 * h), 1.0);
        glowDot (g, { r.getX() + hashFloat ((uint32_t) k * 131u) * r.getWidth(), r.getBottom() - y * r.getHeight() }, 1.2f, col::white.withAlpha (0.25f + 0.5f * lvl * (1.0f - y)));
    }
    // the hall ghost floats in the middle, fading into the mist
    auto L = ghosty::lookFor (2); L.slime = tint; L.body = col::white;
    ghosty::drawGhost (g, L, ctr.x + 18.0f * (float) std::sin (tt * 0.31), ctr.y + 8.0f * (float) std::sin (tt * 0.53), r.getHeight() * (0.26f + 0.12f * size), 0, false, 0.85f + 0.15f * lvl);
    // decay envelope: pre-delay gap, then the tail over DECAY seconds
    auto e = r.removeFromBottom (34).reduced (12, 4);
    juce::Path env; const float span = juce::jmax (1.0f, juce::jmin (decay * 1.15f + pre * 0.001f, 64.0f));
    for (int i = 0; i <= 120; ++i)
    {
        const float ts = span * (float) i / 120.0f, pd = pre * 0.001f;
        float y = ts < pd ? 0.0f : (fz ? 1.0f : std::pow (10.0f, -3.0f * (ts - pd) / std::max (0.1f, decay)));
        if (mode == hmGated) y = ts < pd ? 0.0f : (ts - pd < 0.08f + 0.7f * size ? 0.8f : 0.0f);
        if (mode == hmReverse) y = ts < pd ? 0.0f : clampf ((ts - pd) / (0.15f + 0.65f * size), 0.0f, 1.0f) * (ts - pd < 0.15f + 0.65f * size ? 1.0f : 0.0f);
        const juce::Point<float> pt (e.getX() + e.getWidth() * (float) i / 120.0f, e.getBottom() - y * e.getHeight());
        if (i == 0) env.startNewSubPath (pt); else env.lineTo (pt);
    }
    glowStroke (g, env, tint.withAlpha (0.7f), 1.4f, 0.5f);
    drawCaption (g, e.translated (0, -14).withHeight (12), (fz ? juce::String ("FROZEN") : (decay < 10.0f ? juce::String (decay, 1) : juce::String (juce::roundToInt (decay))) + " s tail"), tint, 9.5f, juce::Justification::centredRight);
    drawCaption (g, r.reduced (10, 6).removeFromTop (14), hallsModeNames()[juce::jlimit (0, kNumHallsModes - 1, mode)], tint, 11);
}


// ---------------------------------------------------------------- GHOST SPLIT
class SplitPage : public Page
{
public:
    SplitPage (juce::AudioProcessorValueTreeState& s, Rack::Impl& r) : rack (r), listen (s, pid (uSplit, "listen"), accentOf (uSplit)), ediv (s, pid (uSplit, "ediv"), accentOf (uSplit))
    {
        const auto acc = accentOf (uSplit), atk = T (juce::Colour (0xffFFD45A)), body = T (juce::Colour (0xff5AB8FF));
        auto add = [&] (juce::OwnedArray<Knob>& arr, const char* id, const char* cap, juce::Colour c) { addAndMakeVisible (arr.add (new Knob (s, pid (uSplit, id), cap, c))); };
        add (global, "sens", "SENSITIVITY", acc); add (global, "xover", "ATTACK LENGTH", acc); add (global, "blend", "BLEND", acc);
        add (global, "attack", "ATTACK", atk); add (global, "sustain", "SUSTAIN", body); add (global, "out", "OUTPUT", acc);
        static const char* ids[] { "gain", "drive", "tilt", "width", "verb", "echo" };
        static const char* caps[] { "GAIN", "SATURATE", "TILT EQ", "WIDTH", "REVERB", "ECHO" };
        for (int k = 0; k < 6; ++k)
        {
            add (aK, (juce::String ("a_") + ids[k]).toRawUTF8(), caps[k], atk);
            add (bK, (juce::String ("b_") + ids[k]).toRawUTF8(), caps[k], body);
        }
        add (ret, "vsize", "VERB SIZE", acc); add (ret, "vdecay", "VERB DECAY", acc); add (ret, "efb", "ECHO FB", acc);
        addAndMakeVisible (listen); addAndMakeVisible (ediv);
    }
    void resized() override
    {
        auto r = getLocalBounds();
        auto bottom = r.removeFromBottom (juce::jmin (150, r.getHeight() * 44 / 100));
        r.removeFromBottom (6);
        auto left = r.removeFromLeft (r.getWidth() * 30 / 100);
        listen.setBounds (left.removeFromBottom (30)); left.removeFromBottom (4);
        vis = left;
        r.removeFromLeft (10);
        auto rr = r.removeFromRight (r.getWidth() * 36 / 100);
        { std::vector<juce::Component*> it; for (auto* k : global) it.push_back (k); gridLayout (r, it, 3, juce::jmin (92, r.getHeight() / 2)); }
        rr.removeFromLeft (8); retBox = rr;
        auto rin = rr.reduced (6, 4); rin.removeFromTop (14);
        auto ediRow = rin.removeFromBottom (28); ediv.setBounds (ediRow.withTrimmedLeft (ediRow.getWidth() / 3).reduced (2, 2)); edivCap = ediRow.withWidth (ediRow.getWidth() / 3);
        { std::vector<juce::Component*> it; for (auto* k : ret) it.push_back (k); gridLayout (rin, it, 3, juce::jmin (88, rin.getHeight())); }
        aBox = bottom.removeFromLeft (bottom.getWidth() / 2).reduced (0, 0).withTrimmedRight (5); bBox = bottom.withTrimmedLeft (5);
        auto lay = [] (juce::Rectangle<int> b, juce::OwnedArray<Knob>& arr)
        { auto in = b.reduced (6, 4); in.removeFromTop (16); std::vector<juce::Component*> it; for (auto* k : arr) it.push_back (k); gridLayout (in, it, 6, juce::jmin (90, in.getHeight())); };
        lay (aBox, aK); lay (bBox, bK);
    }
    void paint (juce::Graphics& g) override
    {
        const auto acc = accentOf (uSplit), atk = T (juce::Colour (0xffFFD45A)), body = T (juce::Colour (0xff5AB8FF));
        auto v = vis.toFloat(); drawScreen (g, v, acc);
        {
            juce::Graphics::ScopedSaveState ss (g); g.reduceClipRegion (vis.reduced (2));
            const auto in = v.reduced (8, 18); const float mid = in.getCentreY();
            const float w = in.getWidth() / 200.0f;
            for (int k = 0; k < 200; ++k)
            {
                const float b = juce::jmin (1.0f, hb.at (k) * 1.6f), a = juce::jmin (1.0f, ha.at (k) * 1.6f), m = hm.at (k);
                const float x = in.getX() + k * w;
                g.setColour (body.withAlpha (0.55f)); g.fillRect (x, mid, juce::jmax (1.0f, w - 0.5f), b * in.getHeight() * 0.48f);
                g.setColour (atk.withAlpha (0.35f + 0.6f * m)); g.fillRect (x, mid - a * in.getHeight() * 0.48f, juce::jmax (1.0f, w - 0.5f), a * in.getHeight() * 0.48f);
            }
            g.setColour (col::white.withAlpha (0.15f)); g.drawHorizontalLine ((int) mid, in.getX(), in.getRight());
            drawCaption (g, { v.getX() + 10, v.getY() + 4, 140, 12 }, "ATTACK", atk, 10);
            drawCaption (g, { v.getX() + 10, v.getBottom() - 16, 140, 12 }, "BODY", body, 10);
            const float m = rack.tm.splitMask.load();
            ghosty::drawGhost (g, ghosty::lookFor (unitInfo (uSplit).look), v.getRight() - 26, v.getY() + 24 - 6 * m, 20, m * 0.6f, false, 0.85f);
        }
        auto boxFrame = [&g] (juce::Rectangle<int> b, juce::Colour c, const juce::String& title)
        {
            auto f = b.toFloat(); g.setColour (c.withAlpha (0.06f)); g.fillRoundedRectangle (f, 9);
            g.setColour (c.withAlpha (0.35f)); g.drawRoundedRectangle (f.reduced (0.5f), 9, 1.0f);
            drawCaption (g, f.reduced (10, 3).removeFromTop (14), title, c, 10.5f);
        };
        boxFrame (aBox, atk, "ATTACK CHAIN  -  the first moments of every hit");
        boxFrame (bBox, body, "BODY CHAIN  -  everything after the hit");
        boxFrame (retBox, acc, "SHARED REVERB / ECHO RETURNS");
        drawCaption (g, edivCap.toFloat().withTrimmedLeft (6), "ECHO TIME", col::dim.brighter (0.25f), 10);
    }
    void tick (double) override
    {
        ha.push (rack.tm.splitAtk.load()); hb.push (rack.tm.splitBody.load()); hm.push (rack.tm.splitMask.load());
        repaint (vis);
    }
private:
    Rack::Impl& rack;
    juce::OwnedArray<Knob> global, aK, bK, ret;
    Segmented listen; Choice ediv;
    juce::Rectangle<int> vis, aBox, bBox, retBox, edivCap;
    History ha, hb, hm;
};

#include "FxBoxUnitPages.inc"

std::unique_ptr<Page> makeBoxPage (int u, juce::AudioProcessorValueTreeState& s, Rack::Impl& rk)
{
    switch (u)
    {
        case uWarp: return std::make_unique<WarpPage> (s, rk);
        case uRetro: return std::make_unique<RetroPage> (s, rk);
        case uFatten: return std::make_unique<FattenPage> (s, rk);
        case uSplit: return std::make_unique<SplitPage> (s, rk);
        case uSpectra: case uBassLab: case uPortal: case uFormant: return makeBox3UnitPage (u, s, rk);
        case uCrackle: return std::make_unique<CracklePage> (s, rk);
        case uHalls:
        {
            auto ms = std::make_shared<MistState>(); auto* sp = &s; auto* rp = &rk;
            return std::make_unique<UnitPage> (s, u, "mode",
                std::vector<KnobSpec> { { "size", "SIZE" }, { "decay", "DECAY" }, { "predelay", "PRE-DELAY" }, { "diffusion", "DIFFUSION" },
                                        { "damp", "DAMPING" }, { "lowcut", "LOW CUT" }, { "mod", "MOD" }, { "width", "WIDTH" },
                                        { "early", "EARLY" }, { "shimmer", "SHIMMER" } },
                std::vector<KnobSpec> { { "freeze", "FREEZE" } }, std::vector<KnobSpec> {},
                [ms, sp, rp] (juce::Graphics& g, juce::Rectangle<float> r, double t) { drawHallsMist (g, r, t, *rp, *sp, *ms); });
        }
        default: return std::make_unique<Page>();
    }
}
