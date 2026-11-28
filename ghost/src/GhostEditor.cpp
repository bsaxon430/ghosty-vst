#include "GhostEditor.h"

namespace ghost
{
static constexpr int kW = 1330, kH = 770;

// ================================================================== a hosted editor inside the screen
void HostedView::show (juce::AudioPluginInstance* p, const juce::String& missing)
{
    if (p == showing && (ed != nullptr || p == nullptr) && message == missing) return;
    clear();
    showing = p;
    message = missing;
    if (p != nullptr)
    {
        ed.reset (p->createEditorIfNeeded());
        if (ed != nullptr)
        {
            addAndMakeVisible (*ed);
            ed->addComponentListener (this);
            place();
        }
        else message = "this plug-in has no window";
    }
    repaint();
}
void HostedView::clear()
{
    if (ed != nullptr) { ed->removeComponentListener (this); removeChildComponent (ed.get()); ed.reset(); }
    showing = nullptr;
}
void HostedView::place()
{
    if (ed == nullptr) return;
    placing = true;
    const auto area = getLocalBounds();
    if (ed->isResizable()) ed->setBounds (area);
    // (a plug-in that keeps its own shape or size: centred; a bigger one is clipped by the screen)
    ed->setTopLeftPosition (area.getCentreX() - ed->getWidth() / 2, juce::jmax (0, area.getCentreY() - ed->getHeight() / 2));
    placing = false;
}
void HostedView::resized() { place(); }
void HostedView::paint (juce::Graphics& g)
{
    g.fillAll (look::screenBg);
    if (ed == nullptr && message.isNotEmpty())
    {
        look::glowText (g, message.toUpperCase(), getLocalBounds().reduced (40).withTrimmedBottom (40), look::mono (20.0f), juce::Justification::centred);
        look::glowText (g, "GHOST opens your installed plug-ins from ~/Library/Audio/Plug-Ins/VST3", getLocalBounds().reduced (40).withTrimmedTop (60),
                        look::mono (12.0f), juce::Justification::centred, look::amber.withAlpha (0.6f));
    }
}

// ================================================================== a synth's own window
SynthWindow::SynthWindow (const juce::String& title, juce::AudioPluginInstance& p, std::function<void()> closed)
    : juce::DocumentWindow (title, juce::Colour (0xff1d1a16), juce::DocumentWindow::closeButton), onClosed (std::move (closed))
{
    setUsingNativeTitleBar (true);
    if (auto* e = p.createEditorIfNeeded())
    {
        setContentOwned (e, true);
        setResizable (e->isResizable(), false);
    }
    centreWithSize (getWidth(), getHeight());
    setAlwaysOnTop (true);
    setVisible (true);
    toFront (true);
}
SynthWindow::~SynthWindow() { clearContentComponent(); }

// ================================================================== GHOST
GhostEditor::GhostEditor (GhostProcessor& p) : juce::AudioProcessorEditor (p), proc (p), drums (p, *this)
{
    setLookAndFeel (&lnf);
    for (auto* b : { &arpBtn, &beatsBtn, &graphBtn, &playBtn, &sendBtn }) addAndMakeVisible (b);
    sendBtn.onClick = [this] { showSend(); };
    sendBtn.setTooltip ("The song as FL files: notes, sounds, stems");
    arpBtn.onClick = [this] { showPage (PageArp); };
    arpBtn.setTooltip ("ArpEZator: write the song (chords, arp, melody, bass)");
    beatsBtn.onClick = [this] { showPage (PageBeats); };
    beatsBtn.setTooltip ("Ghosty Beats: QUICK page, sequencer, drum sounds");
    graphBtn.onClick = [this] { showPage (PageGraph); };
    graphBtn.setTooltip ("Ghost Graph: its songs and GENERATE, played through your sounds and Ghosty Beats' drums");
    playBtn.onClick = [this] { proc.togglePlay(); };
    playBtn.setTooltip ("Play / stop when FL is stopped (ArpEZator and Ghosty Beats play together). When FL plays, GHOST follows FL");
    addAndMakeVisible (master);
    aMaster = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, "master", master.s);
    master.text = [] (double v) { return v <= -59.9 ? juce::String ("OFF") : juce::String (v, 1) + " dB"; };
    addAndMakeVisible (screen);
    for (int l = 0; l < kLanes; ++l) addAndMakeVisible (laneMods.add (new LaneModule (p, *this, l)));
    addAndMakeVisible (drums);
    proc.onLanePluginClosing = [this] (int lane) { closeSynthEditor (lane); };
    page = proc.source.load() == GhostProcessor::FromGraph ? PageGraph : PageArp;
    setSize (kW, kH);
    startTimerHz (24);
    timerCallback();
}

GhostEditor::~GhostEditor()
{
    stopTimer();
    proc.onLanePluginClosing = nullptr;
    for (int l = 0; l < kLanes; ++l) closeSynthEditor (l);
    browser.reset(); fxPanel.reset();
    screen.clear();
    setLookAndFeel (nullptr);
}

void GhostEditor::closeOverlays()
{
    browser.reset(); fxPanel.reset(); sendPanel.reset();
    screen.setVisible (true);
}

void GhostEditor::showPage (Page pg)
{
    closeOverlays();
    page = pg;
    if (page == PageArp) proc.setSource (GhostProcessor::FromArpez);
    if (page == PageGraph) proc.setSource (GhostProcessor::FromGraph);
    proc.graphVisible = page == PageGraph;
    if (page == PageGraph) screen.show (proc.graph.get(), proc.graph ? juce::String() : (proc.graphError.isNotEmpty() ? proc.graphError : juce::String ("opening Ghost Graph...")));
    else if (page == PageArp) screen.show (proc.arp.get(), proc.arp ? juce::String() : (proc.arpError.isNotEmpty() ? proc.arpError : juce::String ("opening ArpEZator...")));
    else if (page == PageBeats) screen.show (proc.beats.get(), proc.beats ? juce::String() : (proc.beatsError.isNotEmpty() ? proc.beatsError : juce::String ("opening Ghosty Beats...")));
    arpBtn.setToggleState (page == PageArp, juce::dontSendNotification);
    beatsBtn.setToggleState (page == PageBeats, juce::dontSendNotification);
    graphBtn.setToggleState (page == PageGraph, juce::dontSendNotification);
    refreshAll();
    repaint();
}

void GhostEditor::showBrowser (int lane)
{
    closeOverlays();
    browser = std::make_unique<Browser> (proc, lane);
    browser->onClose = [this] { juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<GhostEditor> (this)] { if (sp) { sp->closeOverlays(); sp->refreshAll(); } }); };
    screen.setVisible (false);
    addAndMakeVisible (*browser);
    browser->setBounds (screenArea);
}

void GhostEditor::showSend()
{
    closeOverlays();
    sendPanel = std::make_unique<SendPanel> (proc, exporter);
    sendPanel->onClose = [this] { juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<GhostEditor> (this)] { if (sp && ! sp->exporter.running()) { sp->closeOverlays(); sp->refreshAll(); } }); };
    screen.setVisible (false);
    addAndMakeVisible (*sendPanel);
    sendPanel->setBounds (screenArea);
}

void GhostEditor::showFx (int lane)
{
    closeOverlays();
    fxPanel = std::make_unique<FxPanel> (proc.lanes[(size_t) lane].fx, "FX  -  " + juce::String (laneName (lane)), [this, lane] { proc.fxChanged (lane); });
    fxPanel->onClose = [this] { juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<GhostEditor> (this)] { if (sp) { sp->closeOverlays(); sp->refreshAll(); } }); };
    screen.setVisible (false);
    addAndMakeVisible (*fxPanel);
    fxPanel->setBounds (screenArea);
}

void GhostEditor::openSynthEditor (int lane)
{
    auto* p = proc.laneSynth (lane);
    if (p == nullptr) return;
    if (synthWins[(size_t) lane] != nullptr && winPlugin[(size_t) lane] == p) { synthWins[(size_t) lane]->toFront (true); return; }
    closeSynthEditor (lane);
    auto& L = proc.lanes[(size_t) lane];
    const auto title = juce::String ("GHOST  -  ") + laneName (lane) + "  -  " + engineName (L.engine) + "  -  " + L.soundName;
    synthWins[(size_t) lane] = std::make_unique<SynthWindow> (title, *p, [this, lane]
    {
        juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<GhostEditor> (this), lane] { if (sp) sp->closeSynthEditor (lane); });
    });
    winPlugin[(size_t) lane] = p;
}

void GhostEditor::closeSynthEditor (int lane)
{
    synthWins[(size_t) lane].reset();
    winPlugin[(size_t) lane] = nullptr;
}

void GhostEditor::refreshAll()
{
    for (auto* m : laneMods) m->refresh();
    drums.repaint();
}

void GhostEditor::timerCallback()
{
    if (! partsShown && (proc.arp || proc.arpError.isNotEmpty()))
    {
        partsShown = true;
        showPage (page);
    }
    if (proc.uiVersion.load() != seenVersion)
    {
        seenVersion = proc.uiVersion.load();
        refreshAll();
        if (browser == nullptr && fxPanel == nullptr && sendPanel == nullptr) showPage (page);
        // a lane whose synth changed: its window belongs to the old synth
        for (int l = 0; l < kLanes; ++l) if (synthWins[(size_t) l] != nullptr && winPlugin[(size_t) l] != proc.laneSynth (l)) closeSynthEditor (l);
    }
    for (auto* m : laneMods) m->tickMeters();
    drums.tickMeters();
    const float l = proc.masterMeterL.load(), r = proc.masterMeterR.load();
    proc.masterMeterL = l * 0.8f; proc.masterMeterR = r * 0.8f;
    for (auto& L : proc.lanes) L.meter = L.meter.load() * 0.8f;
    proc.beatsMeter = proc.beatsMeter.load() * 0.8f;
    const float nl = vuL * 0.75f + l * 0.25f, nr = vuR * 0.75f + r * 0.25f;
    const bool playing = proc.dawPlaying.load() || proc.ownPlay.load();
    playBtn.setToggleState (proc.ownPlay.load(), juce::dontSendNotification);
    playBtn.setButtonText (proc.ownPlay.load() ? "STOP" : "PLAY");
    if (proc.opening != shownOpening) { shownOpening = proc.opening; repaint (topBar); }
    if (std::abs (nl - vuL) > 0.002f || std::abs (nr - vuR) > 0.002f || playing != lastPlaying || juce::Time::getMillisecondCounter() > statusUntil)
    {
        vuL = nl; vuR = nr; lastPlaying = playing;
        repaint (topBar);
    }
}

void GhostEditor::renderBackground()
{
    // everything that doesn't move, drawn once into a picture (the faceplate's grain is thousands of lines)
    bg = juce::Image (juce::Image::RGB, juce::jmax (1, getWidth()), juce::jmax (1, getHeight()), true);
    juce::Graphics g (bg);
    g.fillAll (juce::Colour (0xff100d0a));
    auto all = getLocalBounds().toFloat();
    look::plate (g, all.reduced (2.0f), 6.0f);
    // rack ears
    for (float x : { 2.0f, all.getRight() - 26.0f })
    {
        juce::Rectangle<float> ear (x, 2.0f, 24.0f, all.getHeight() - 4.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffa9a49a), ear.getX(), 0, juce::Colour (0xffcfcbc3), ear.getRight(), 0, false));
        g.fillRect (ear);
        g.setColour (juce::Colours::black.withAlpha (0.25f)); g.drawVerticalLine ((int) (x < 10 ? ear.getRight() : ear.getX()), ear.getY(), ear.getBottom());
        for (float y : { 30.0f, all.getCentreY(), all.getBottom() - 30.0f }) look::screw (g, { ear.getCentreX(), y }, 6.0f);
    }
    // the name
    look::engraved (g, "GHOST", { topBar.getX() + 6, topBar.getY() + 4, 220, 40 }, look::wordmark (34.0f), juce::Justification::left);
    look::engraved (g, "GHOSTBLOCK  -  MODEL GH-1  SONG MACHINE", { topBar.getX() + 8, topBar.getY() + 42, 320, 14 }, look::label (9.5f), juce::Justification::left, look::inkSoft);
    look::recess (g, screenArea.toFloat().expanded (6.0f), juce::Colour (0xff050403), 6.0f);
}

void GhostEditor::paint (juce::Graphics& g)
{
    if (bg.isNull() || bg.getWidth() != getWidth() || bg.getHeight() != getHeight()) renderBackground();
    g.drawImageAt (bg, 0, 0);
    // the display: what's playing, the tempo, the bar
    auto disp = juce::Rectangle<int> (playBtn.getRight() + 12, topBar.getY() + 8, 300, 44);
    look::recess (g, disp.toFloat());
    const bool dawPlay = proc.dawPlaying.load(), own = proc.ownPlay.load();
    const double ppq = proc.shownPpq.load();
    const int bar = (int) std::floor (juce::jmax (0.0, ppq) / 4.0) + 1, beat = (int) std::floor (std::fmod (juce::jmax (0.0, ppq), 4.0)) + 1;
    if (proc.opening.isNotEmpty())
    {
        look::glowText (g, "OPENING " + proc.opening + "...", disp.reduced (10, 4).removeFromTop (20), look::mono (16.0f), juce::Justification::left);
        return;
    }
    look::glowText (g, juce::String (proc.shownBpm.load(), 1) + " BPM   SONG: " + (proc.source.load() == GhostProcessor::FromGraph ? "GRAPH" : "ARPEZ"),
                    disp.reduced (10, 4).removeFromTop (20), look::mono (16.0f), juce::Justification::left);
    look::glowText (g, (dawPlay ? "FL PLAYING" : own ? "GHOST PLAYING" : "STOPPED") + juce::String ("   ") + (dawPlay || own ? juce::String (bar) + "." + juce::String (beat) : juce::String ("-.-")),
                    disp.reduced (10, 4).removeFromBottom (16), look::mono (11.0f), juce::Justification::left, look::amber.withAlpha (0.75f));
    const bool showStatus = juce::Time::getMillisecondCounter() < statusUntil && status.isNotEmpty();
    look::glowText (g, showStatus ? status.toUpperCase() : juce::String(), disp.reduced (10, 4).withTrimmedLeft (130), look::mono (11.0f), juce::Justification::topRight, look::amber.withAlpha (0.85f));
    // the VU meter (analog needle)
    auto vu = juce::Rectangle<float> ((float) master.getX() - 150.0f, (float) topBar.getY() + 4.0f, 140.0f, 52.0f);
    g.setColour (juce::Colour (0xff221e19)); g.fillRoundedRectangle (vu, 4.0f);
    auto face = vu.reduced (5.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffff4cf), face.getCentreX(), face.getBottom() + 10, juce::Colour (0xffc9a75a), face.getCentreX(), face.getY() - 30, true));
    g.fillRoundedRectangle (face, 2.0f);
    g.setColour (juce::Colour (0xff3a2a10)); g.setFont (look::label (8.0f));
    g.drawText ("-20      -7      0    +3", face.toNearestInt().reduced (6, 2), juce::Justification::centredTop);
    g.drawText ("VU", face.toNearestInt().reduced (4, 2), juce::Justification::bottomLeft);
    g.saveState();
    g.reduceClipRegion (face.toNearestInt());
    for (int k = 0; k < 2; ++k)
    {
        const float v = k == 0 ? vuL : vuR;
        const float db = juce::Decibels::gainToDecibels (v, -40.0f);
        const float t = juce::jlimit (0.0f, 1.0f, (db + 20.0f) / 23.0f);
        const float a = -0.9f + 1.8f * t;
        const juce::Point<float> base (face.getCentreX() + (k == 0 ? -3.0f : 3.0f), face.getBottom() + 14.0f);
        g.setColour (k == 0 ? juce::Colour (0xff1a1410) : juce::Colour (0xffb3261e));
        g.drawLine (base.x, base.y, base.x + 50.0f * std::sin (a), base.y - 50.0f * std::cos (a), 1.4f);
    }
    g.restoreState();
    }

void GhostEditor::resized()
{
    auto r = getLocalBounds().reduced (32, 10);
    topBar = r.removeFromTop (60);
    r.removeFromTop (12);
    rack = r.removeFromRight (276);
    r.removeFromRight (16);
    screenArea = r.withTrimmedBottom (2).reduced (4);
    {
        auto t = topBar;
        t.removeFromLeft (240);
        auto pages = t.removeFromLeft (270).withSizeKeepingCentre (270, 34);
        arpBtn.setBounds (pages.removeFromLeft (90).reduced (3, 0));
        beatsBtn.setBounds (pages.removeFromLeft (90).reduced (3, 0));
        graphBtn.setBounds (pages.removeFromLeft (90).reduced (3, 0));
        t.removeFromLeft (14);
        playBtn.setBounds (t.removeFromLeft (80).withSizeKeepingCentre (80, 34));
        master.setBounds (t.removeFromRight (60));
        t.removeFromRight (160);
        sendBtn.setBounds (t.removeFromRight (110).withSizeKeepingCentre (110, 34));
    }
    screen.setBounds (screenArea);
    bg = {};
    if (browser) browser->setBounds (screenArea);
    if (fxPanel) fxPanel->setBounds (screenArea);
    if (sendPanel) sendPanel->setBounds (screenArea);
    auto rk = rack;
    rk.removeFromTop (6);
    const int laneH = 146;
    for (auto* m : laneMods) { m->setBounds (rk.removeFromTop (laneH)); rk.removeFromTop (6); }
    drums.setBounds (rk.removeFromTop (juce::jmin (64, rk.getHeight())));
}
} // namespace ghost
