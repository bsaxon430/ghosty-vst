// LAYOUT 2 (round 3): the STUDIO window - the Ghosty Pro UI (owner 2026-10-25; CLASSIC is a fallback until tested in FL).
// REMOVING CLASSIC later is one cut: in ProEditor.cpp drop SynthPage (STUDIO's front page replaces it; nothing here uses it but
// hiding it), the CLASSIC branch of ProContent::resized / paint / paintOverChildren (the "LAYOUT 2" lines pick STUDIO first),
// ProContent's CLASSIC header / play-row positions, ProContent::setLayout's CLASSIC case and the LAYOUT menu items; build the
// ProStudio in ProContent's constructor. STUDIO places every shared widget itself (layoutContent) - no CLASSIC position is read. Included by ProEditor.cpp right after ProContent (ProStudio is ProContent's friend: it
// places ProContent's own header, tabs, pages and play row at STUDIO size and puts its front page where the SYNTH page was).
// CLASSIC never runs any of this: ProContent creates the ProStudio the first time STUDIO is chosen.

namespace studio2
{
ProStudio::ProStudio (ProContent& content, ProProcessor& p) : body (p), keys (p), c (content), proc (p)
{
    setOpaque (true);
    addAndMakeVisible (body); addChildComponent (keys);
    for (auto* b : { &keysBtn, &layoutBtn }) addAndMakeVisible (b);
    body.onMessage = [this] (const juce::String& m) { c.toast = m; c.toastTime = 3.0f; };
    body.onTab = [this] (int t) { c.showTab (t); };
    body.onMorphToggle = [this]
    {   // the MORPH page's own ON button (its rules: needs a ghost on the board, commits the blend when it goes off)
        for (auto* ch : c.morphPage.getChildren())
            if (auto* b = dynamic_cast<juce::TextButton*> (ch); b != nullptr && b->getButtonText() == "MORPH ON") { b->triggerClick(); return; }
    };
    body.onSnapStep = [this] (int d) { c.snapBar.step (d); };
    keys.attach (c.flow.latch, c.flow.octave);
    // tooltips on the shared header / play-row controls (CLASSIC shows them too: text only, nothing on screen changes)
    if (gpro::factoryPresetsBuilt()) { c.prevBtn.setTooltip ("Previous sound"); c.nextBtn.setTooltip ("Next sound"); }   // (FACTORY PRESETS OFF: the header sets them: your sounds / greyed)
    c.saveBtn.setTooltip ("Save this sound (every knob, both drawn waves, a sample, the FX)"); c.loadBtn.setTooltip ("Load one of your sounds");
    c.initBtn.setTooltip ("INIT: a plain starting sound");
    c.keyBox.setTooltip ("PLAY: the key"); c.scaleBox.setTooltip ("PLAY: the scale (FLOW strip + chords follow it)"); c.chordBox.setTooltip ("PLAY: one key plays this chord");
    c.arpBtn.setTooltip ("ARP on / off"); c.rateBox.setTooltip ("ARP rate"); c.modeBox.setTooltip ("ARP order"); c.octBox.setTooltip ("ARP range");
    c.gateKnob->slider.setTooltip ("ARP GATE: how long each arp note is held");
    body.onSnapList = [this] { c.snapBar.listBtn.triggerClick(); };
    body.snapName = [this]
    {
        auto& b = *proc.snapshots; const int i = b.current();
        return i >= 0 ? "SNAPSHOT  " + b.info (i).name : (b.empty() ? juce::String ("no snapshots yet") : "SNAPSHOTS (" + juce::String (b.size()) + ")");
    };
    keysBtn.setTooltip ("FLOW: play with the FLOW strip (slide across the scale); off = the KEYS");
    keysBtn.setClickingTogglesState (true);
    keysBtn.onClick = [this] { flowMode = keysBtn.getToggleState(); layoutContent(); };
    ambient = L2::ambientOn();
    layoutBtn.setTooltip ("LAYOUT: CLASSIC (the tabs as before) or STUDIO (everything on one page). Saved for you, not in projects or sounds");
    layoutBtn.onClick = [this] { showLayoutMenu (&layoutBtn); };
    startTimerHz (24);
}

ProStudio::~ProStudio() { stopTimer(); L2::wantTrace (&proc, false); }

void ProStudio::setOn (bool s)
{
    on = s;
    setVisible (s);
    if (s) { c.setSize (W, H); toBack(); }
    else
    {
        c.flow.setVisible (true);
        c.setSize (ProContent::W, ProContent::H);
    }
    c.resized();
    c.showTab (c.tabs.current);
}

void ProStudio::layoutContent()
{
    setBounds (0, 0, W, H); toBack();
    // header: the left group where CLASSIC has it, the look / bypass group at the right edge, LAYOUT next to the look button
    c.prevBtn.setBounds (244, 18, 34, 34); c.display.setBounds (282, 18, 206, 34); c.nextBtn.setBounds (492, 18, 34, 34);
    c.saveBtn.setBounds (534, 18, 60, 34); c.loadBtn.setBounds (598, 18, 60, 34); c.initBtn.setBounds (662, 18, 54, 34);
    c.snapBar.setBounds (722, 20, 108, 30);
    c.unlock.setBounds (836, 18, 112, 34);
    if (c.unlock.isVisible()) c.cleanStrip.setBounds (722, 55, 226, 18); else c.cleanStrip.setBounds (838, 17, 112, 36);
    c.cleanCard.setBounds (W - 16 - CleanCard::CW, 58, CleanCard::CW, CleanCard::CH);
    c.themeBtn.setBounds (W - 16 - 84 - 8 - 76, 22, 76, 26);
    c.bypassBtn.setBounds (W - 16 - 84, 22, 84, 26);
    layoutBtn.setBounds (c.themeBtn.getX() - 8 - 92, 22, 92, 26);
    c.tabs.setBounds (16, 76, 7 * TabBar::tabStep, 32);
    c.voicesTip.setBounds (W - 16 - 300 + 60, 78, 160, 28);
    // the pages: the front page in the SYNTH page's place, the other pages over the same area
    bodyRect = { 16, TY, W - 32, BODY_H };
    for (auto* pg : std::initializer_list<juce::Component*> { &c.synthPage, &c.modPage, &c.fxPanel, &c.browsePage, &c.bendPage, &c.trapPage, &c.morphPage }) pg->setBounds (bodyRect);
    body.setBounds (bodyRect);
    // play card: PLAY / ARP row, then the KEYS or the FLOW strip
    const int y = TY + BODY_H + 10;
    playCard = { 16, y, W - 32, H - 10 - y };
    const int ry = y + 10;
    c.keyBox.setBounds (66, ry, 62, 28); c.scaleBox.setBounds (134, ry, 160, 28); c.chordBox.setBounds (300, ry, 140, 28);
    c.arpBtn.setBounds (460, ry, 72, 28); c.rateBox.setBounds (538, ry, 86, 28); c.modeBox.setBounds (630, ry, 116, 28); c.octBox.setBounds (752, ry, 110, 28);
    c.holdBtn.setBounds (c.octBox.getRight() + 8, ry, 60, 28); c.octCtl.setBounds (c.holdBtn.getRight() + 6, ry, 82, 28);   // HOLD + OCT (as CLASSIC): keys + FLOW
    keysBtn.setBounds (c.octCtl.getRight() + 14, ry, 70, 28);
    keysBtn.setToggleState (flowMode, juce::dontSendNotification);
    c.gateKnob->setBounds (playCard.getRight() - 96, y + 6, 88, playCard.getHeight() - 12);
    const juce::Rectangle<int> playArea (26, ry + 36, c.gateKnob->getX() - 10 - 26, playCard.getBottom() - 10 - (ry + 36));
    keys.setBounds (playArea); c.flow.setBounds (playArea);
    keys.setVisible (on && ! flowMode); c.flow.setVisible (! on || flowMode);
    covered.clear();
    for (auto r : body.cardRects()) covered.push_back (r.translated (bodyRect.getX(), bodyRect.getY()).reduced (2));
    covered.push_back (playCard.reduced (2));
}

void ProStudio::tabShown (int tab)
{
    if (! on) return;
    c.synthPage.setVisible (false);
    body.setVisible (tab == 0);
}

juce::Rectangle<float> ProStudio::toastRect() const { return { (float) W / 2 - 220, (float) (TY + BODY_H - 44), 440, 32 }; }

void ProStudio::resized() {}

void ProStudio::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    paintHouseWindow (g, b);       // (the same window / play card / card / knob painting as CLASSIC: a restyle reaches both)
    const float lvl = proc.level.load();
    {   // the background ghost parade, off the header and the play card
        juce::Graphics::ScopedSaveState ss (g);
        g.excludeClipRegion (juce::Rectangle<int> (0, 0, W, TY));
        g.excludeClipRegion (playCard);
        for (auto& q : c.bg)
        {
            const float rr = q.r * juce::jmin (b.getWidth(), b.getHeight());
            const float x = b.getCentreX() + std::cos (q.a) * rr * 1.3f, y = b.getHeight() * 0.5f + std::sin (q.a) * rr * 0.8f;
            drawGhost (g, q.look, x, y, q.sz, c.wordGlitch * 0.4f, false, q.al + lvl * 0.25f, std::cos (q.a + 1.57f) < 0);
            if (pghost::on()) pghost::outlineGhost (g, x, y, q.sz, 0.35f + lvl * 0.25f, std::cos (q.a + 1.57f) < 0);    // GHOST LOOK: legible on the aurora
        }
    }
    if (ambientRaining()) paintRain (g);
    if (C().dark)
    {
        juce::ColourGradient pg (C().ecto.withAlpha (0.18f + lvl * 0.35f), 164, 35, C().ecto.withAlpha (0.0f), 214, 35, true);
        g.setGradientFill (pg); g.fillEllipse (114, 5, 100, 60);
    }
    wordmark (g, { 20, 14, 230, 42 }, "PRO", c.wordGlitch);
    ghosty::prologo::drawMascot (g, { 214.0f, 5.0f, 31.0f, 58.0f }, C().bg, 0.6f);   // GHOSTY PRO logo (brand): angry ghost, additive (STUDIO header)
    g.setColour (C().dim); g.setFont (font (9.0f, false));
    g.drawText ("v" + ProEditor::versionString(), juce::Rectangle<int> (150, 50, 70, 12), juce::Justification::centred);
    if (proc.morph.enabled.load())
    {
        g.setColour (C().pink); g.setFont (font (9.5f));
        g.drawText ("MORPH ON - knobs follow the board", juce::Rectangle<int> (16 + 7 * TabBar::tabStep + 6, 78, 170, 28), juce::Justification::centredLeft);
    }
    g.setColour (C().dim); g.setFont (font (10.0f));
    int voicesAll = 0; for (int k = 0; k < NUM_LAYERS; ++k) voicesAll += proc.layerVoices[k].load();
    int uniPlaying = 0, uniMax = 0;
    for (int k = 0; k < NUM_LAYERS; ++k) { uniPlaying += proc.uniLayerLive[k].load(); uniMax += proc.uniLayerMax[k].load(); }
    g.drawText ("VOICES " + juce::String (voicesAll) + "   " + juce::String (uniPlaying) + " / " + juce::String (uniMax) + " UNI", juce::Rectangle<int> (W - 16 - 300, 78, 220, 28), juce::Justification::centredRight);
    auto meter = juce::Rectangle<float> ((float) W - 16 - 70, 89, 70, 6);
    g.setColour (C().white.withAlpha (0.08f)); g.fillRoundedRectangle (meter, 3);
    g.setColour (C().slime.interpolatedWith (C().pink, juce::jmin (1.0f, lvl))); g.fillRoundedRectangle (meter.withWidth (meter.getWidth() * juce::jmin (1.0f, lvl * 1.4f)), 3);
    // play card
    auto pc = playCard.toFloat();
    paintPlayCard (g, pc);
    g.setColour (C().dim); g.setFont (font (10.0f));
    g.drawText ("PLAY", juce::Rectangle<float> (pc.getX() + 12, (float) c.keyBox.getY(), 44, (float) c.keyBox.getHeight()), juce::Justification::centredLeft);
    auto led = juce::Rectangle<float> ((float) c.arpBtn.getRight() - 12, (float) c.arpBtn.getY() + 4, 6, 6);
    g.setColour (C().slime.withAlpha (0.15f + c.arpFlash * 0.85f)); g.fillEllipse (led);
    if (! proc.license.isUnlocked() && License::required())
    {
        g.setColour (C().gold.withAlpha (0.85f)); g.setFont (font (10.0f));
        g.drawText ("DEMO - sound drops out every 20 s", juce::Rectangle<int> (W - 16 - 330, 78, 200, 28), juce::Justification::centredLeft);
    }
}

void ProStudio::timerCallback()
{
    if (! on) return;
    repaint (0, 0, W, TY);
    repaint (playCard);
    // AMBIENT: off by itself when the window is hidden, or when the ticks come late for ~2 s (the computer is busy)
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (lastTickMs > 0 && isShowing()) slowTicks = now - lastTickMs > 110.0 ? slowTicks + 1 : 0;
    lastTickMs = now;
    if (slowTicks > 48 && ! ambientTooSlow) { ambientTooSlow = true; repaint(); }
    if (ambientRaining() && isShowing()) stepRain();
}

void ProStudio::setAmbient (bool r)
{
    ambient = r; ambientTooSlow = false; slowTicks = 0;
    if (! r) for (auto& d : rain) d.y = -1000;
    repaint();
}

void ProStudio::stepRain()
{
    for (auto& d : rain)
    {
        const auto before = juce::Rectangle<float> (d.x - 3, d.y - d.len - 2, 6, d.len + 5).getSmallestIntegerContainer();
        if (d.y > (float) H + 30 || d.y < -900)
        {   // a new drop above the window (some start lower so the rain never comes in a wave)
            d.x = rnd.nextFloat() * (float) W; d.y = d.y < -900 ? rnd.nextFloat() * (float) H : -rnd.nextFloat() * 120.0f;
            d.vy = 1.2f + rnd.nextFloat() * 2.2f; d.len = 10.0f + rnd.nextFloat() * 18.0f; d.a = 0.30f + rnd.nextFloat() * 0.3f; d.col = rnd.nextInt (3);
        }
        else d.y += d.vy;
        const auto after = juce::Rectangle<float> (d.x - 3, d.y - d.len - 2, 6, d.len + 5).getSmallestIntegerContainer();
        const auto dirty = before.getUnion (after);
        bool hidden = false; for (auto& r : covered) hidden |= r.contains (dirty);
        if (! hidden) repaint (dirty);
    }
}

void ProStudio::paintRain (juce::Graphics& g)
{
    const juce::Colour cols[] { C().slime, C().pink, C().ice };
    for (auto& d : rain)
    {
        if (d.y < -900) continue;
        const auto c0 = cols[d.col];
        for (int k = 0; k < 4; ++k)      // the trail: four fading segments (rectangles: no allocation)
        {
            const float seg = d.len / 4.0f;
            g.setColour (c0.withAlpha (d.a * (0.15f + 0.22f * (float) k)));
            g.fillRect (d.x - 0.7f, d.y - d.len + seg * (float) k, 1.4f, seg);
        }
        g.setColour (c0.withAlpha (d.a * 0.35f)); g.fillEllipse (d.x - 3.0f, d.y - 3.0f, 6.0f, 6.0f);
        g.setColour (c0.withAlpha (juce::jmin (1.0f, d.a * 1.6f))); g.fillEllipse (d.x - 1.6f, d.y - 1.6f, 3.2f, 3.2f);
    }
}

void ProStudio::showLayoutMenu (juce::Component* target) { c.layout2Menu (target); }
} // namespace studio2

// ---- ProContent's LAYOUT 2 members
void ProContent::setLayout (int l)
{
    if (l == layout()) return;
    if (l == gpro::layout2::studio && studio == nullptr)
    {
        // build it in the BLACK palette, like every other page (colours stored at construction are BLACK colours, themed at paint)
        const auto was = pt::currentLook();     // (GHOST LOOK: the look index, GHOST included)
        pt::setTheme (pt::Theme::Dark);
        studio = std::make_unique<ProStudio> (*this, proc);
        pt::setLook (was);
        addChildComponent (*studio);
        studio->toBack();
        sliderConvention (*studio);
        retint (*studio, nullptr);
        if (pghost::on()) studio->sendLookAndFeelChange();     // GHOST LOOK: the knobs' value boxes take the look's ink (built before joining: white text)
        morphKnobsBuilt = false; morphKnobs.clear();      // MORPH ON: the STUDIO knobs follow the board too
        if (modUi != nullptr) { studio->body.attachMod (modUi.get()); modUi->knobsChanged(); }    // MOD: tiles, rings, knob menus on STUDIO's knobs too
    }
    if (studio != nullptr) studio->setOn (l == gpro::layout2::studio);
}

void ProContent::layout2Menu (juce::Component* target)
{
    juce::PopupMenu m;
    m.addSectionHeader ("LOOK");
    for (int t = 0; t < 3; ++t) m.addItem (1 + t, ghosty::themes::displayName (ghosty::themes::fromInt (t)), true, look() == t);
    m.addItem (4, pghost::name(), true, look() == pghost::kLook);      // GHOST LOOK
    m.addSectionHeader ("LAYOUT  (saved for you - not in projects or sounds)");
    m.addItem (11, "CLASSIC  -  the tabs, as before", true, layout() == gpro::layout2::classic);
    m.addItem (12, "STUDIO  -  both oscillators, filter, envelopes, LFOs and FX on one page", true, layout() == gpro::layout2::studio);
    m.addItem (21, "AMBIENT  -  the gentle neon rain behind STUDIO's panels", true, gpro::layout2::ambientOn());
    juce::Component::SafePointer<ProContent> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [safe] (int r)
    {
        if (safe == nullptr || r == 0) return;
        if (r >= 1 && r <= 4) { safe->setLook (r - 1); return; }     // (GHOST LOOK: 4 = GHOST)
        if (r == 21)
        {
            const bool now = ! gpro::layout2::ambientOn();
            gpro::layout2::saveAmbient (now);
            if (safe->studio != nullptr) safe->studio->setAmbient (now);
            return;
        }
        if (r == 11 || r == 12)
        {
            gpro::layout2::saveLayout (r - 11);
            if (auto* ed = safe->findParentComponentOfClass<ProEditor>()) ed->setLayout (r - 11);
        }
    });
}
