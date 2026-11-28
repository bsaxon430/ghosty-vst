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

// ================================================================== FX
FxPanel::FxPanel (GhostProcessor& p, int l) : proc (p), lane (l)
{
    for (auto* c : std::initializer_list<juce::Component*> { &close, &onBtn, &warpOn, &order, &fftBtn, &mix, &chainMix }) addAndMakeVisible (c);
    close.onClick = [this] { if (onClose) onClose(); };
    onBtn.onClick = [this] { set (sfx::SpectralFx::pOn, fx().on() ? 0.0 : 1.0); sync(); };
    onBtn.setTooltip ("The whole FX rack of this lane on / off (off = the sound exactly as the synth makes it)");
    warpOn.onClick = [this] { set (sfx::SpectralFx::pWarpOn, fx().warpOn() ? 0.0 : 1.0); sync(); };
    warpOn.setTooltip ("WARP: WarpCore's spectral inversion");
    order.onClick = [this] { set (sfx::SpectralFx::pOrder, fx().order() == 0 ? 1.0 : 0.0); sync(); };
    order.setTooltip ("Which comes first: the 4 bays or WARP");
    fftBtn.onClick = [this]
    {
        juce::PopupMenu m;
        for (int i = 0; i < sfx::kFftSizes; ++i) m.addItem (i + 1, "FFT " + juce::String (sfx::fftSizeAt (i)) + (i == sfx::kDefaultFft ? "  (normal)" : ""), true, fx().fftIndex() == i);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (fftBtn), [this] (int r) { if (r > 0) { set (sfx::SpectralFx::pFft, r - 1); sync(); } });
    };
    fftBtn.setTooltip ("The window the spectral bays work on: bigger = smoother and later, smaller = grittier and quicker");
    mix.s.setRange (0.0, 100.0, 0.1); mix.text = [] (double v) { return juce::String ((int) std::lround (v)) + " %"; };
    mix.s.onValueChange = [this] { if (! syncing) turn (sfx::SpectralFx::pMix, mix.s.getValue()); };
    chainMix.s.setRange (0.0, 100.0, 0.1); chainMix.text = mix.text;
    chainMix.s.onValueChange = [this] { if (! syncing) turn (sfx::SpectralFx::pChainMix, chainMix.s.getValue()); };
    for (int b = 0; b < sfx::kSlots; ++b)
    {
        auto& B = bays[(size_t) b];
        addAndMakeVisible (B.effect); addAndMakeVisible (B.on); addAndMakeVisible (B.mix);
        B.effect.onClick = [this, b] { pickEffect (b); };
        B.effect.setTooltip ("Pick this bay's effect (Spectral Corruptor's 38, in its eight folders)");
        B.on.onClick = [this, b] { set (sfx::SpectralFx::pSlotOn (b), fx().slotOn (b) ? 0.0 : 1.0); sync(); };
        B.mix.s.setRange (0.0, 100.0, 0.1); B.mix.text = mix.text;
        B.mix.s.onValueChange = [this, b] { if (! syncing) turn (sfx::SpectralFx::pSlotMix (b), bays[(size_t) b].mix.s.getValue()); };
    }
    const auto& wp = sfx::warpParams();
    for (int k = 0; k < (int) wp.size(); ++k)
    {
        auto* kn = warpKnobs.add (new Knob (juce::String (wp[(size_t) k].name).toUpperCase()));
        addAndMakeVisible (kn);
        const auto d = wp[(size_t) k];
        kn->s.setRange (d.min, d.max, d.stepped() ? 1.0 : 0.0);
        if (d.logScale && d.min > 0) kn->s.setSkewFactorFromMidPoint (std::sqrt (d.min * d.max));
        kn->text = [d] (double v) { return juce::String (d.text ((float) v)); };
        kn->s.onValueChange = [this, k, kn] { if (! syncing) turn (sfx::SpectralFx::pWarpParam (k), kn->s.getValue()); };
    }
    rebuild();
    sync();
    startTimerHz (10);
}
FxPanel::~FxPanel() { stopTimer(); }

void FxPanel::pickEffect (int bay)
{
    juce::PopupMenu m;
    m.addItem (1000, "EMPTY", true, fx().slotType (bay) == 0);
    const auto& types = sfx::types();
    for (auto& cat : sfx::categories())
    {
        juce::PopupMenu sub;
        for (auto& t : types) if (t.index > 0 && t.category == cat) sub.addItem (t.index + 1, t.name, true, fx().slotType (bay) == t.index);
        m.addSubMenu (juce::String (cat), sub);
    }
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (bays[(size_t) bay].effect), [this, bay] (int r)
    {
        if (r <= 0) return;
        const int type = r == 1000 ? 0 : r - 1;
        set (sfx::SpectralFx::pSlotType (bay), type);
        if (type > 0 && ! fx().on()) set (sfx::SpectralFx::pOn, 1.0);    // picking an effect turns the rack on
        if (type > 0 && fx().slotMix (bay) < 1.0f) set (sfx::SpectralFx::pSlotMix (bay), 100.0);
        rebuild(); sync();
    });
}

void FxPanel::rebuild()
{
    const auto& types = sfx::types();
    for (int b = 0; b < sfx::kSlots; ++b)
    {
        auto& B = bays[(size_t) b];
        const int t = juce::jlimit (0, (int) types.size() - 1, fx().slotType (b));
        if (t == shownType[(size_t) b]) continue;
        shownType[(size_t) b] = t;
        B.knobs.clear(); B.choices.clear();
        const auto& d = types[(size_t) t];
        B.effect.setButtonText (t == 0 ? juce::String ("EMPTY") : juce::String (d.name).toUpperCase());
        const int count = juce::jmin (8, (int) d.params.size());   // (a bay shows the effect's first 8 controls, the ones the host can automate)
        for (int k = 0; k < count; ++k)
        {
            const auto pd = d.params[(size_t) k];
            auto* kn = B.knobs.add (new Knob (juce::String (pd.name).toUpperCase().substring (0, 12)));
            addAndMakeVisible (kn);
            kn->s.setRange (pd.min, pd.max, pd.stepped() ? juce::jmax (1.0f, pd.step) : 0.0);
            if (pd.logScale && pd.min > 0) kn->s.setSkewFactorFromMidPoint (std::sqrt (pd.min * pd.max));
            kn->text = [pd] (double v) { return juce::String (pd.text ((float) v)); };
            kn->s.onValueChange = [this, b, k, kn, pd]
            {
                if (syncing) return;
                turn (sfx::SpectralFx::pSlotParam (b, k), pd.toNorm ((float) kn->s.getValue()));
            };
        }
    }
    resized();
}

void FxPanel::sync()
{
    syncing = true;
    auto& F = fx();
    onBtn.setToggleState (F.on(), juce::dontSendNotification);
    warpOn.setToggleState (F.warpOn(), juce::dontSendNotification);
    warpOn.setEnabled (sfx::warpAvailable());
    order.setButtonText (F.order() == 0 ? "BAYS > WARP" : "WARP > BAYS");
    fftBtn.setButtonText ("FFT " + juce::String (sfx::fftSizeAt (F.fftIndex())));
    if (! mix.s.isMouseButtonDown()) mix.s.setValue (F.mix(), juce::dontSendNotification);
    if (! chainMix.s.isMouseButtonDown()) chainMix.s.setValue (F.chainMix(), juce::dontSendNotification);
    bool anySpectral = false;
    for (int b = 0; b < sfx::kSlots; ++b)
    {
        auto& B = bays[(size_t) b];
        const int t = F.slotType (b);
        const bool has = t > 0;
        if (has && sfx::types()[(size_t) t].domain != sfx::Domain::Time) anySpectral = true;
        B.on.setToggleState (F.slotOn (b), juce::dontSendNotification);
        B.on.setEnabled (has);            // (greyed when the bay is empty: nothing to switch)
        B.mix.setEnabled (has);
        if (! B.mix.s.isMouseButtonDown()) B.mix.s.setValue (F.slotMix (b), juce::dontSendNotification);
        for (int k = 0; k < B.knobs.size(); ++k)
        {
            if (! B.knobs[k]->s.isMouseButtonDown()) B.knobs[k]->s.setValue (F.slotValue (b, k), juce::dontSendNotification);
            B.knobs[k]->setEnabled (F.slotOn (b));
        }
    }
    fftBtn.setEnabled (anySpectral);      // (the window only matters to spectral effects)
    chainMix.setEnabled (anySpectral || F.slotType (0) || F.slotType (1) || F.slotType (2) || F.slotType (3));
    for (int k = 0; k < warpKnobs.size(); ++k)
    {
        if (! warpKnobs[k]->s.isMouseButtonDown()) warpKnobs[k]->s.setValue (F.warpValue (k), juce::dontSendNotification);
        warpKnobs[k]->setEnabled (F.warpOn());
    }
    syncing = false;
    lastChange = F.changeCount();
    repaint();
}

void FxPanel::timerCallback()
{
    if (fx().changeCount() == lastChange) return;   // (only changes from elsewhere: automation, a loaded song)
    bool typeChanged = false;
    for (int b = 0; b < sfx::kSlots; ++b) typeChanged = typeChanged || fx().slotType (b) != shownType[(size_t) b];
    if (typeChanged) rebuild();
    sync();
}

void FxPanel::paint (juce::Graphics& g)
{
    look::plate (g, getLocalBounds().toFloat(), 6.0f);
    look::engraved (g, "FX  -  " + juce::String (laneName (lane)), { 18, 10, 400, 26 }, look::wordmark (20.0f), juce::Justification::left);
    look::engraved (g, "POLTERGEIST 2 SPECTRAL RACK:  4 BAYS  +  WARP", { 18, 34, 500, 16 }, look::label (11.0f), juce::Justification::left, look::inkSoft);
    const int bw = (getWidth() - 36 - 3 * 10) / 4;
    for (int b = 0; b < sfx::kSlots; ++b)
    {
        juce::Rectangle<float> r ((float) (18 + b * (bw + 10)), 66.0f, (float) bw, 330.0f);
        g.setColour (juce::Colours::black.withAlpha (0.10f)); g.fillRoundedRectangle (r, 6.0f);
        g.setColour (juce::Colours::black.withAlpha (0.25f)); g.drawRoundedRectangle (r, 6.0f, 1.0f);
        look::engraved (g, "BAY " + juce::String (b + 1), r.toNearestInt().removeFromTop (22).reduced (8, 0), look::label (12.0f), juce::Justification::left);
        look::led (g, { r.getRight() - 12.0f, r.getY() + 11.0f }, fx().on() && fx().slotOn (b) && fx().slotType (b) > 0);
    }
    juce::Rectangle<float> w (18.0f, 408.0f, (float) getWidth() - 36.0f, (float) getHeight() - 420.0f);
    g.setColour (juce::Colours::black.withAlpha (0.10f)); g.fillRoundedRectangle (w, 6.0f);
    g.setColour (juce::Colours::black.withAlpha (0.25f)); g.drawRoundedRectangle (w, 6.0f, 1.0f);
    look::engraved (g, "WARP", w.toNearestInt().removeFromTop (24).reduced (10, 0), look::label (13.0f), juce::Justification::left);
    if (! sfx::warpAvailable()) look::engraved (g, "(not on this computer's processor)", w.toNearestInt().removeFromTop (24).reduced (70, 0), look::label (11.0f), juce::Justification::left, look::inkSoft);
}

void FxPanel::resized()
{
    auto top = getLocalBounds().reduced (18, 12).removeFromTop (44);
    close.setBounds (top.removeFromRight (80).withHeight (28));
    top.removeFromRight (10);
    onBtn.setBounds (top.removeFromRight (80).withHeight (28));
    top.removeFromRight (10);
    mix.setBounds (top.removeFromRight (50).withHeight (50));
    top.removeFromRight (6);
    chainMix.setBounds (top.removeFromRight (56).withHeight (50));
    top.removeFromRight (10);
    order.setBounds (top.removeFromRight (110).withHeight (28));
    top.removeFromRight (8);
    fftBtn.setBounds (top.removeFromRight (90).withHeight (28));
    const int bw = (getWidth() - 36 - 3 * 10) / 4;
    for (int b = 0; b < sfx::kSlots; ++b)
    {
        auto& B = bays[(size_t) b];
        juce::Rectangle<int> r (18 + b * (bw + 10), 66, bw, 330);
        r = r.reduced (8); r.removeFromTop (18);
        B.effect.setBounds (r.removeFromTop (30));
        r.removeFromTop (6);
        auto row = r.removeFromTop (56);
        B.on.setBounds (row.removeFromLeft (row.getWidth() / 2).reduced (6, 14));
        B.mix.setBounds (row);
        r.removeFromTop (6);
        const int kw = r.getWidth() / 4, kh = 58;
        for (int k = 0; k < B.knobs.size(); ++k)
            B.knobs[k]->setBounds (r.getX() + (k % 4) * kw, r.getY() + (k / 4) * (kh + 4), kw, kh);
    }
    auto w = juce::Rectangle<int> (18, 408, getWidth() - 36, getHeight() - 420).reduced (10, 6);
    auto wt = w.removeFromTop (22);
    warpOn.setBounds (wt.removeFromRight (100));
    w.removeFromTop (4);
    const int kw = juce::jmin (90, w.getWidth() / juce::jmax (1, warpKnobs.size()));
    for (int k = 0; k < warpKnobs.size(); ++k) warpKnobs[k]->setBounds (w.getX() + k * kw, w.getY(), kw, juce::jmin (70, w.getHeight()));
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
