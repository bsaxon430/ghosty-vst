#include "LanePanels.h"
#include "GhostEditor.h"
#include "HostedPlugin.h"

namespace ghost
{
static juce::String dbText (double v) { return v <= -59.9 ? juce::String ("OFF") : juce::String (v, 1) + " dB"; }

// ================================================================== a lane
LaneModule::LaneModule (GhostProcessor& p, GhostEditor& ed, int l) : proc (p), editor (ed), lane (l)
{
    for (auto* c : std::initializer_list<juce::Component*> { &display, &prev, &next, &dice, &fxBtn, &editBtn, &keysBtn, &muteBtn, &level, &tone, &fxMix })
        addAndMakeVisible (c);
    addChildComponent (unmuteBtn);
    unmuteBtn.onClick = [this] { proc.unmuteArpLane (lane); editor.setStatus (laneName (lane) + juce::String (": playing again in ArpEZator")); refresh(); };
    unmuteBtn.setTooltip ("ArpEZator has this lane muted (its mood does that). Click: ArpEZator plays it again");
    const juce::String n = juce::String (laneName (l)).toLowerCase();
    aLevel = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, n + "_level", level.s);
    aTone = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, n + "_tone", tone.s);
    aMute = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, n + "_mute", muteBtn);
    muteBtn.setClickingTogglesState (true);
    level.text = dbText;
    tone.text = [] (double v) { return std::abs (v) < 0.01 ? juce::String ("OPEN") : v < 0 ? "DARKER " + juce::String ((int) std::lround (-v * 100)) : "THINNER " + juce::String ((int) std::lround (v * 100)); };
    tone.s.setDoubleClickReturnValue (true, 0.0);
    fxMix.s.setRange (0.0, 100.0, 0.1);
    fxMix.text = [] (double v) { return juce::String ((int) std::lround (v)) + " %"; };
    fxMix.s.onValueChange = [this] { if (fxMix.s.isMouseButtonDown() || fxMix.s.hasKeyboardFocus (false)) { proc.lanes[(size_t) lane].fx.setParam (sfx::SpectralFx::pMix, fxMix.s.getValue()); } fxMix.repaint(); };
    display.onClick = [this] { editor.showBrowser (lane); };
    display.setTooltip ("Click: every sound of Ghosty Pro 2, Poltergeist 2 and Ghost Atlas");
    prev.onClick = [this] { editor.setStatus (laneName (lane) + juce::String (": ") + proc.stepSound (lane, -1)); refresh(); };
    next.onClick = [this] { editor.setStatus (laneName (lane) + juce::String (": ") + proc.stepSound (lane, +1)); refresh(); };
    dice.onClick = [this] { editor.setStatus (laneName (lane) + juce::String (": ") + proc.nextGood (lane)); refresh(); };
    prev.setTooltip ("The sound before (sounds that suit " + juce::String (laneName (l)) + ")");
    next.setTooltip ("The next sound (sounds that suit " + juce::String (laneName (l)) + ")");
    dice.setTooltip ("A random sound that suits " + juce::String (laneName (l)) + ", from any of the three synths");
    fxBtn.onClick = [this] { editor.showFx (lane); };
    fxBtn.setTooltip ("Poltergeist 2's FX on this lane: 4 bays + WARP");
    editBtn.onClick = [this] { editor.openSynthEditor (lane); };
    editBtn.setTooltip ("Open this sound in its own synth (the synth's own window)");
    keysBtn.onClick = [this] { proc.selectedLane = lane; editor.refreshAll(); };
    keysBtn.setTooltip ("Your MIDI keyboard plays this lane");
    muteBtn.setTooltip ("Mute this lane");
    refresh();
}

void LaneModule::refresh()
{
    auto& L = proc.lanes[(size_t) lane];
    display.error = L.error.isNotEmpty();
    display.top = (L.hasEngine ? juce::String (engineName (L.engine)) : juce::String ("NO SYNTH")) + (L.soundCategory.isNotEmpty() ? "  /  " + L.soundCategory.toUpperCase() : juce::String());
    display.main = L.error.isNotEmpty() ? L.error.toUpperCase() : L.soundName.isNotEmpty() ? L.soundName : juce::String ("PICK A SOUND");
    display.repaint();
    keysBtn.setToggleState (proc.selectedLane.load() == lane, juce::dontSendNotification);
    // where the notes come from
    if (proc.source.load() == GhostProcessor::FromGraph)
    {
        const auto& part = proc.graphShown.laneParts[(size_t) lane];
        feed = proc.graph == nullptr ? juce::String ("GHOST GRAPH ISN'T OPEN") : part.isNotEmpty() ? "GRAPH: " + part.toUpperCase() : juce::String ("GRAPH: NOTHING FOR THIS LANE");
        feedWarn = proc.graph == nullptr || part.isEmpty();
        unmuteBtn.setVisible (false);
    }
    else
    {
        const bool muted = proc.arpMuted[(size_t) lane];
        const bool none = proc.arp != nullptr && proc.arpNoteCount[(size_t) lane] == 0 && lane != LaneChords;
        feed = proc.arp == nullptr ? juce::String ("ARPEZATOR ISN'T OPEN") : muted ? juce::String ("MUTED IN ARPEZ") : none ? juce::String ("ARPEZ: NO NOTES") : juce::String ("ARPEZ");
        feedWarn = proc.arp == nullptr || muted || none;
        unmuteBtn.setVisible (muted && proc.arp != nullptr);
    }
    keysBtn.setVisible (! unmuteBtn.isVisible());
    editBtn.setEnabled (L.plugin != nullptr && L.plugin->hasEditor());
    const bool fxOn = L.fx.on();
    fxBtn.setToggleState (fxOn, juce::dontSendNotification);
    fxMix.setEnabled (fxOn);   // (greyed: the FX are off, so the mix does nothing)
    if (! fxMix.s.isMouseButtonDown()) fxMix.s.setValue (L.fx.mix(), juce::dontSendNotification);
    repaint();
}

void LaneModule::tickMeters()
{
    auto& L = proc.lanes[(size_t) lane];
    const bool a = L.activity.exchange (false);
    const float m = L.meter.load();
    if (a != activity || std::abs (m - meter) > 0.01f) { activity = a; meter = m; repaint (getLocalBounds().removeFromTop (24)); }
}

void LaneModule::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.12f)); g.fillRoundedRectangle (r, 5.0f);
    g.setColour (juce::Colours::white.withAlpha (0.5f)); g.drawRoundedRectangle (r.reduced (0.5f).translated (0, 1), 5.0f, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.25f)); g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
    look::led (g, { 12.0f, 13.0f }, activity);
    look::engraved (g, laneName (lane), { 22, 4, 70, 18 }, look::label (14.0f), juce::Justification::left);
    // where its notes come from (red when nothing comes)
    look::engraved (g, feed, { 152, 6, getWidth() - 152 - 82, 14 }, look::label (9.0f), juce::Justification::left, feedWarn ? look::red : look::inkSoft);
    // the lane's level, as a row of small lights
    const int lights = (int) std::round (juce::jlimit (0.0f, 1.0f, std::sqrt (meter)) * 8.0f);
    for (int i = 0; i < 8; ++i)
    {
        g.setColour (i < lights ? (i >= 7 ? look::red : look::amber) : look::amberDim.withAlpha (0.4f));
        g.fillRoundedRectangle (88.0f + i * 7.0f, 9.0f, 5.0f, 8.0f, 1.0f);
    }
}

void LaneModule::resized()
{
    auto r = getLocalBounds().reduced (6, 4);
    auto top = r.removeFromTop (20);
    muteBtn.setBounds (top.removeFromRight (26));
    top.removeFromRight (4);
    keysBtn.setBounds (top.removeFromRight (44));
    unmuteBtn.setBounds (keysBtn.getBounds().withLeft (keysBtn.getX() - 14));
    r.removeFromTop (3);
    auto row = r.removeFromTop (36);
    prev.setBounds (row.removeFromLeft (22));
    next.setBounds (row.removeFromRight (22));
    display.setBounds (row.reduced (3, 0));
    r.removeFromTop (3);
    auto knobs = r.removeFromTop (54);
    const int kw = 56;
    level.setBounds (knobs.removeFromLeft (kw));
    tone.setBounds (knobs.removeFromLeft (kw));
    fxMix.setBounds (knobs.removeFromLeft (kw));
    dice.setBounds (knobs.reduced (2, 10));
    r.removeFromTop (2);
    auto btns = r.removeFromTop (20);
    fxBtn.setBounds (btns.removeFromLeft (btns.getWidth() / 2).reduced (2, 0));
    editBtn.setBounds (btns.reduced (2, 0));
}

// ================================================================== the drums
DrumsModule::DrumsModule (GhostProcessor& p, GhostEditor& ed) : proc (p), editor (ed)
{
    addAndMakeVisible (level); addAndMakeVisible (open); addAndMakeVisible (muteBtn);
    aLevel = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, "beats_level", level.s);
    aMute = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, "beats_mute", muteBtn);
    muteBtn.setClickingTogglesState (true);
    level.text = dbText;
    open.onClick = [this] { editor.showPage (GhostEditor::PageBeats); };
    open.setTooltip ("Ghosty Beats: the QUICK page, the sequencer and every drum sound");
}
void DrumsModule::tickMeters() { const float m = proc.beatsMeter.load(); if (std::abs (m - meter) > 0.01f) { meter = m; repaint(); } }
void DrumsModule::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.12f)); g.fillRoundedRectangle (r, 5.0f);
    g.setColour (juce::Colours::black.withAlpha (0.25f)); g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
    look::engraved (g, "DRUMS", { 10, 6, 80, 18 }, look::label (14.0f), juce::Justification::left);
    look::engraved (g, proc.beatsError.isNotEmpty() ? proc.beatsError.toUpperCase() : juce::String ("GHOSTY BEATS"), { 10, 24, 130, 14 }, look::label (10.0f),
                    juce::Justification::left, proc.beatsError.isNotEmpty() ? look::red : look::inkSoft);
    const int lights = (int) std::round (juce::jlimit (0.0f, 1.0f, std::sqrt (meter)) * 8.0f);
    for (int i = 0; i < 8; ++i)
    {
        g.setColour (i < lights ? (i >= 7 ? look::red : look::amber) : look::amberDim.withAlpha (0.4f));
        g.fillRoundedRectangle (10.0f + i * 7.0f, 44.0f, 5.0f, 8.0f, 1.0f);
    }
}
void DrumsModule::resized()
{
    auto r = getLocalBounds().reduced (6, 4);
    r.removeFromLeft (128);
    level.setBounds (r.removeFromLeft (54));
    auto b = r.reduced (2, 6);
    muteBtn.setBounds (b.removeFromRight (26));
    b.removeFromRight (4);
    open.setBounds (b);
}

// ================================================================== the browser
Browser::Browser (GhostProcessor& p, int l) : lane (l), proc (p)
{
    addAndMakeVisible (search); addAndMakeVisible (close); addAndMakeVisible (fitOnly); addAndMakeVisible (rescan); addAndMakeVisible (list);
    search.setFont (look::mono (15.0f));
    search.setTextToShowWhenEmpty ("SEARCH  (name, folder or synth)", look::amber.withAlpha (0.4f));
    search.onTextChange = [this] { filter(); };
    search.onReturnKey = [this] { if (! shown.empty()) load (juce::jmax (0, list.getSelectedRow())); };
    close.onClick = [this] { if (onClose) onClose(); };
    fitOnly.setClickingTogglesState (true);
    fitOnly.setToggleState (true, juce::dontSendNotification);
    fitOnly.onClick = [this] { filter(); };
    fitOnly.setTooltip ("Only the sounds that suit this lane (off: every sound)");
    rescan.onClick = [this] { Catalog::get().rescanAsync(); status = "looking for your sounds..."; repaint(); };
    rescan.setTooltip ("Look for your own sounds and packs again");
    const char* names[] { "ALL", "GHOSTY PRO 2", "POLTERGEIST 2", "GHOST ATLAS" };
    for (int i = 0; i <= kEngines; ++i)
    {
        auto& b = engineBtn[(size_t) i];
        b.setButtonText (names[i]);
        addAndMakeVisible (b);
        b.onClick = [this, i] { engine = i - 1; filter(); };
        if (i > 0 && Hosted::findBundle (engineBundle ((Engine) (i - 1))) == juce::File())
        {
            b.setEnabled (false);   // (greyed: that synth isn't installed on this computer)
            b.setTooltip (juce::String (engineName ((Engine) (i - 1))) + " isn't installed");
        }
    }
    list.setRowHeight (24);
    list.setColour (juce::ListBox::backgroundColourId, look::screenBg);
    filter();
    startTimerHz (4);
}

void Browser::timerCallback()
{
    if (Catalog::get().version() != catVersion) filter();
    if (! Catalog::get().scanning() && status.startsWith ("looking")) { status = {}; repaint(); }
}

void Browser::filter()
{
    catVersion = Catalog::get().version();
    all = Catalog::get().snapshot();
    const auto roles = GhostProcessor::rolesFor (lane);
    const auto words = juce::StringArray::fromTokens (search.getText().toLowerCase(), " ", "");
    shown.clear();
    for (auto& s : all)
    {
        if (engine >= 0 && (int) s.engine != engine) continue;
        if (fitOnly.getToggleState() && search.isEmpty() && std::find (roles.begin(), roles.end(), s.role) == roles.end()) continue;
        if (! words.isEmpty())
        {
            const auto hay = (s.name + " " + s.category + " " + s.group + " " + engineName (s.engine) + " " + roleName (s.role)).toLowerCase();
            bool ok = true; for (auto& w : words) if (w.isNotEmpty() && ! hay.contains (w)) { ok = false; break; }
            if (! ok) continue;
        }
        shown.push_back (s);
    }
    for (int i = 0; i <= kEngines; ++i) engineBtn[(size_t) i].setToggleState (engine == i - 1, juce::dontSendNotification);
    list.updateContent();
    int sel = -1;
    for (int i = 0; i < (int) shown.size(); ++i) if (shown[(size_t) i].id == proc.lanes[(size_t) lane].soundId) { sel = i; break; }
    if (sel >= 0) { list.selectRow (sel, true, true); }
    repaint();
}

void Browser::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) shown.size()) return;
    const auto& s = shown[(size_t) row];
    const bool current = s.id == proc.lanes[(size_t) lane].soundId;
    if (selected) { g.setColour (look::amber.withAlpha (0.18f)); g.fillRect (0, 0, w, h); }
    const auto c = current ? juce::Colour (0xffffd08a) : look::amber;
    g.setFont (look::mono (14.0f)); g.setColour (c);
    g.drawText ((current ? "> " : "  ") + s.name, 6, 0, w / 2 - 10, h, juce::Justification::centredLeft, true);
    g.setFont (look::mono (11.0f)); g.setColour (c.withAlpha (0.6f));
    g.drawText (s.category.toUpperCase(), w / 2, 0, w / 4 - 6, h, juce::Justification::centredLeft, true);
    g.drawText (juce::String (engineShort (s.engine)) + (s.factory ? juce::String() : "  " + s.group.toUpperCase()), w * 3 / 4, 0, w / 4 - 8, h, juce::Justification::centredLeft, true);
}

void Browser::load (int row)
{
    if (row < 0 || row >= (int) shown.size() || loading) return;
    loading = true;
    juce::String err;
    status = proc.loadSound (lane, shown[(size_t) row], err) ? juce::String() : err;
    loading = false;
    list.repaint();
    repaint();
}
void Browser::listBoxItemClicked (int row, const juce::MouseEvent&) { load (row); }
void Browser::selectedRowsChanged (int row) { if (row >= 0 && (list.hasKeyboardFocus (true))) load (row); }   // (up / down keys: hear each one)
void Browser::listBoxItemDoubleClicked (int row, const juce::MouseEvent&) { load (row); if (onClose) onClose(); }

void Browser::paint (juce::Graphics& g)
{
    look::plate (g, getLocalBounds().toFloat(), 6.0f);
    look::engraved (g, "SOUNDS  -  " + juce::String (laneName (lane)), { 18, 10, 400, 26 }, look::wordmark (20.0f), juce::Justification::left);
    juce::String line = juce::String ((int) shown.size()) + " SOUNDS";
    if (Catalog::get().scanning()) line << "   (still looking for your own sounds...)";
    if (status.isNotEmpty() && ! status.startsWith ("looking")) line << "   " << status.toUpperCase();
    look::engraved (g, line, { 18, 34, 700, 16 }, look::label (11.0f), juce::Justification::left, status.isNotEmpty() && ! status.startsWith ("looking") ? look::red : look::inkSoft);
    look::recess (g, list.getBounds().toFloat().expanded (4.0f));
    look::engraved (g, "CLICK = HEAR IT ON THE LANE    DOUBLE-CLICK = KEEP IT AND CLOSE    UP / DOWN = NEXT ONE", { 18, getHeight() - 22, getWidth() - 36, 16 }, look::label (10.0f), juce::Justification::centred, look::inkSoft);
}

void Browser::resized()
{
    auto r = getLocalBounds().reduced (18, 12);
    auto top = r.removeFromTop (44);
    close.setBounds (top.removeFromRight (80).withHeight (28));
    top.removeFromRight (10);
    rescan.setBounds (top.removeFromRight (100).withHeight (28));
    r.removeFromTop (8);
    auto row = r.removeFromTop (30);
    search.setBounds (row.removeFromLeft (300));
    row.removeFromLeft (10);
    fitOnly.setBounds (row.removeFromRight (120));
    row.removeFromRight (10);
    const int bw = row.getWidth() / (kEngines + 1);
    for (int i = 0; i <= kEngines; ++i) engineBtn[(size_t) i].setBounds (row.removeFromLeft (bw).reduced (2, 0));
    r.removeFromTop (12);
    r.removeFromBottom (24);
    list.setBounds (r.reduced (4));
}
} // namespace ghost

namespace ghost
{
SendPanel::SendPanel (GhostProcessor& p, Exporter& x) : proc (p), ex (x)
{
    for (auto* c : std::initializer_list<juce::Component*> { &make, &close, &finder, &view }) addAndMakeVisible (c);
    view.setViewedComponent (&rows, false);
    view.setScrollBarsShown (true, false);
    make.onClick = [this] { if (! ex.start()) repaint(); };
    make.setTooltip ("Plays the song through every part once more (a few seconds, the sound pauses) and writes the FL files");
    close.onClick = [this] { if (onClose) onClose(); };
    finder.onClick = [this] { if (ex.folder.isDirectory()) ex.folder.revealToUser(); };
    ex.onChange = [sp = juce::Component::SafePointer<SendPanel> (this)] { if (sp) { sp->rebuild(); sp->repaint(); } };
    rebuild();
    startTimerHz (10);
}
SendPanel::~SendPanel() { ex.onChange = nullptr; }

void SendPanel::rebuild()
{
    make.setEnabled (! ex.running());
    finder.setEnabled (ex.folder.isDirectory() && ! ex.running());
    const int n = ex.running() ? -2 : (int) ex.parts.size();
    if (n == shownParts) return;
    shownParts = n;
    rowParts.clear(); all.reset(); mix.reset();
    if (! ex.running() && ex.songMidi.existsAsFile())
    {
        all = std::make_unique<DragTile> (ex.songMidi, "ALL PARTS  (MIDI)  -  DRAG ONTO FL'S CHANNEL RACK", true);
        addAndMakeVisible (*all);
        mix = std::make_unique<DragTile> (ex.mixStem, "WHOLE MIX (WAV)");
        addAndMakeVisible (*mix);
        for (auto& part : ex.parts)
        {
            auto* row = rowParts.add (new juce::Component());
            struct Label : juce::Component { juce::String t; void paint (juce::Graphics& g) override { look::glowText (g, t, getLocalBounds(), look::mono (13.0f), juce::Justification::centredLeft); } };
            auto* lab = new Label(); lab->t = part.name; row->addAndMakeVisible (lab);
            auto* m = new DragTile (part.midi, "NOTES (MIDI)"); row->addAndMakeVisible (m);
            auto* s = new DragTile (part.stem, "AS HEARD (WAV)"); row->addAndMakeVisible (s);
            const auto ext = part.sound.getFileExtension().toUpperCase().trimCharactersAtStart (".");
            auto* so = new DragTile (part.sound, ext == "WAV" ? juce::String ("DRUM SOUND (WAV)") : "SOUND (" + ext + ")"); row->addAndMakeVisible (so);
            rows.addAndMakeVisible (row);
        }
    }
    resized();
}

void SendPanel::paint (juce::Graphics& g)
{
    look::plate (g, getLocalBounds().toFloat(), 6.0f);
    look::engraved (g, "SEND TO FL", { 18, 10, 400, 26 }, look::wordmark (20.0f), juce::Justification::left);
    look::engraved (g, "THE SONG (" + juce::String (proc.source.load() == GhostProcessor::FromGraph ? "GRAPH" : "ARPEZ") + ") AS FILES: EVERY PART'S NOTES, ITS SOUND AS HEARD, ITS SYNTH PRESET",
                    { 18, 34, getWidth() - 36, 16 }, look::label (11.0f), juce::Justification::left, look::inkSoft);
    auto bar = juce::Rectangle<float> (18.0f, 62.0f, (float) getWidth() - 36.0f, 26.0f);
    look::recess (g, bar);
    if (ex.running())
    {
        g.setColour (look::amber.withAlpha (0.35f)); g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * ex.progress()).reduced (2.0f), 3.0f);
        look::glowText (g, "MAKING IT...  " + ex.status.toUpperCase(), bar.toNearestInt().reduced (10, 0), look::mono (12.0f), juce::Justification::centredLeft);
    }
    else
        look::glowText (g, ex.folder.isDirectory() ? ("READY: " + ex.folder.getFileName() + "   (" + ex.status + ")").toUpperCase() : (ex.status.isNotEmpty() ? ex.status.toUpperCase() : juce::String ("PRESS MAKE IT")),
                        bar.toNearestInt().reduced (10, 0), look::mono (12.0f), juce::Justification::centredLeft);
    if (! ex.running() && ! ex.songMidi.existsAsFile())
    {
        auto t = getLocalBounds().reduced (30, 0).withTrimmedTop (130);
        look::engraved (g, "1.  MAKE IT  (the sound pauses for a few seconds while GHOST plays every part once more)", t.removeFromTop (24), look::label (13.0f), juce::Justification::left);
        look::engraved (g, "2.  Drag ALL PARTS onto FL's Channel Rack: a channel per part, the notes in a pattern", t.removeFromTop (24), look::label (13.0f), juce::Justification::left);
        look::engraved (g, "3.  A synth channel: swap it for its synth, open its SOUND file.  A drum: drop its DRUM SOUND on it", t.removeFromTop (24), look::label (13.0f), juce::Justification::left);
        look::engraved (g, "4.  Or drag AS HEARD (WAV): the part exactly as GHOST plays it, FX and all", t.removeFromTop (24), look::label (13.0f), juce::Justification::left);
    }
}

void SendPanel::resized()
{
    auto top = getLocalBounds().reduced (18, 12).removeFromTop (44);
    close.setBounds (top.removeFromRight (80).withHeight (28));
    top.removeFromRight (10);
    finder.setBounds (top.removeFromRight (130).withHeight (28));
    top.removeFromRight (10);
    make.setBounds (top.removeFromRight (110).withHeight (28));
    auto r = getLocalBounds().reduced (18, 0).withTrimmedTop (100).withTrimmedBottom (14);
    if (all) { auto a = r.removeFromTop (40); mix->setBounds (a.removeFromRight (170)); a.removeFromRight (8); all->setBounds (a); r.removeFromTop (10); }
    view.setBounds (r);
    const int rowH = 30, w = juce::jmax (100, r.getWidth() - 14);
    rows.setSize (w, rowParts.size() * rowH);
    for (int i = 0; i < rowParts.size(); ++i)
    {
        auto* row = rowParts[i];
        row->setBounds (0, i * rowH, w, rowH - 4);
        auto rr = row->getLocalBounds();
        auto kids = row->getChildren();
        if (kids.size() == 4)
        {
            kids[3]->setBounds (rr.removeFromRight (140)); rr.removeFromRight (6);
            kids[2]->setBounds (rr.removeFromRight (130)); rr.removeFromRight (6);
            kids[1]->setBounds (rr.removeFromRight (110)); rr.removeFromRight (10);
            kids[0]->setBounds (rr);
        }
    }
}
} // namespace ghost
