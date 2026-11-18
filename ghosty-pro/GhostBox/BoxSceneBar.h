#pragma once
// GHOST SCENES bar: a strip along the bottom of the Ghost Box window (added BELOW the rack: the window grew by its height, so no existing
// control moved). [GHOST SCENES] [1 CLEAN] .. [8 DROP] [CUT|MORPH|SPILL] [quantise] [morph time] [MIDI C1] [...]
//
//   click a filled slot      the scene (it stays)              long press (> 350 ms)  the scene while held, back on release
//   click an empty slot      stores what you hear there        right-click            store / rename / clear / copy / undo / starters
//   a slot shows its number, its name and its MIDI key; the active scene is lit, a pending one (waiting for the grid) blinks, a running
//   MORPH draws its progress, a ringing SPILL tail shows as a soft ring.
// Themed in BLACK / PURPLE / LIGHT through the rack's look-and-feel (gpfx::ui::LnF) and the fx colour tokens; GHOST paints the bar as
// liquid glass (Shared/GhostGlass.h) with deep indigo ink.
#include "BoxProcessor.h"
#include "FxUi.h"
#include "GhostGlass.h"

class BoxSceneBar : public juce::Component, private juce::Timer
{
public:
    static constexpr int kHeight = 44;
    std::function<void (const juce::String&)> onMessage;   // toasts in the window header

    explicit BoxSceneBar (BoxProcessor& p) : proc (p)
    {
        for (int i = 0; i < boxscenes::kSlots; ++i) { auto* b = slots.add (new Slot (*this, i)); addAndMakeVisible (b); }
        const juce::StringArray modeTips {
            "CUT: the scene switches on the grid (quantise), settings glide 10 ms so nothing clicks",
            "MORPH: every setting glides from what you hear to the scene over the MORPH TIME; units only one side uses fade through their mix",
            "SPILL: switches like CUT, but echoes and reverbs the new scene turns off keep ringing out their tails" };
        for (int m = 0; m < 3; ++m)
        {
            auto* b = modes.add (new juce::TextButton (gpfx::sceneModeNames()[m]));
            b->setClickingTogglesState (false); b->setColour (juce::TextButton::buttonOnColourId, gpfx::ui::col::ecto);
            b->setTooltip (modeTips[m]);
            b->onClick = [this, m] { proc.sceneBank.set.mode = m; proc.sceneSettingsChanged(); refresh(); };
            addAndMakeVisible (b);
        }
        quant.addItemList (gpfx::sceneQuantNames(), 1); quant.setTooltip ("QUANTISE: scene changes land on the next 1/16, 1/8, beat, bar or 2 bars of the song (OFF: at once; transport stopped: at once)");
        quant.onChange = [this] { proc.sceneBank.set.quant = juce::jmax (0, quant.getSelectedItemIndex()); proc.sceneSettingsChanged(); };
        morph.addItemList (gpfx::sceneMorphNames(), 1); morph.setTooltip ("MORPH TIME: how long a MORPH takes");
        morph.onChange = [this] { proc.sceneBank.set.morphLen = juce::jmax (0, morph.getSelectedItemIndex()); proc.sceneSettingsChanged(); };
        midi.setClickingTogglesState (false); midi.setColour (juce::TextButton::buttonOnColourId, gpfx::ui::col::ice);
        midi.onClick = [this] { proc.sceneBank.set.midiOn = ! proc.sceneBank.set.midiOn; proc.sceneSettingsChanged(); refresh(); message (proc.sceneBank.set.midiOn ? "MIDI keys trigger the scenes" : "MIDI keys off"); };
        more.setButtonText (juce::String::fromUTF8 ("\xe2\x80\xa6")); more.setTooltip ("GHOST SCENES: MIDI keys, hold or latch, starter scenes, undo");
        more.onClick = [this] { showMoreMenu(); };
        for (auto* c : std::initializer_list<juce::Component*> { &quant, &morph, &midi, &more }) addAndMakeVisible (c);
        proc.onScenesChanged = [sp = juce::Component::SafePointer<BoxSceneBar> (this)] { if (sp) sp->refresh(); };
        refresh();
        startTimerHz (20);
    }
    ~BoxSceneBar() override { proc.onScenesChanged = nullptr; stopTimer(); }

    void refresh()
    {
        const auto& st = proc.sceneBank.set;
        for (int m = 0; m < 3; ++m) modes[m]->setToggleState (st.mode == m, juce::dontSendNotification);
        quant.setSelectedItemIndex (st.quant, juce::dontSendNotification);
        morph.setSelectedItemIndex (st.morphLen, juce::dontSendNotification);
        morph.setEnabled (st.mode == gpfx::smMorph);
        midi.setToggleState (st.midiOn, juce::dontSendNotification);
        midi.setButtonText ("MIDI " + boxscenes::noteName (st.midiBase));
        midi.setTooltip (st.midiOn ? "MIDI keys " + boxscenes::noteName (st.midiBase) + " - " + boxscenes::noteName (st.midiBase + 7) + " trigger scenes 1 - 8 ("
                                     + (st.midiLatch ? "LATCH: a key switches" : "HOLD: the scene while the key is down") + "). Click: MIDI off. The key range is in the ... menu."
                                   : "MIDI keys are off. Click to turn them on.");
        syncLit();
        for (auto* s : slots) s->repaint();
        repaint();
    }

    // the bar's glass in GHOST (the window composites it into its cached static layer)
    void paintGhostSurface (juce::Graphics& g, juce::Rectangle<float> r) { ghosty::glass::body (g, r.reduced (1.0f), 12.0f, juce::Colours::white, 0.40f); }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (8.0f, 3.0f);
        if (! gpfx::ui::ghost())
        {
            g.setColour (gpfx::ui::T (juce::Colour (0xff11131a))); g.fillRoundedRectangle (b, 8);
            g.setColour (gpfx::ui::col::ecto.withAlpha (0.22f)); g.drawRoundedRectangle (b.reduced (0.5f), 8, 1.0f);
        }
        gpfx::ui::drawCaption (g, label.toFloat(), "GHOST SCENES", gpfx::ui::ghost() ? ghosty::glass::violetDeep() : gpfx::ui::col::ecto.get(), 11.5f);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (14, 6);
        label = r.removeFromLeft (96);
        auto ctl = [&r] (int w) { return r.removeFromRight (w).withSizeKeepingCentre (w, juce::jmin (26, r.getHeight())); };
        more.setBounds (ctl (26)); r.removeFromRight (6);
        midi.setBounds (ctl (62)); r.removeFromRight (6);
        morph.setBounds (ctl (74)); r.removeFromRight (4);
        quant.setBounds (ctl (62)); r.removeFromRight (6);
        for (int m = 3; --m >= 0;) { modes[m]->setBounds (ctl (58)); r.removeFromRight (m > 0 ? 2 : 10); }   // (wide enough for MORPH in the GHOST pill)
        const int w = (r.getWidth() + 4) / boxscenes::kSlots;
        for (int i = 0; i < boxscenes::kSlots; ++i) slots[i]->setBounds (r.removeFromLeft (w - 4)), r.removeFromLeft (4);
    }

    // tests / screenshots
    juce::Component* slotComponent (int i) { return slots[i]; }
    juce::TextButton& modeButton (int m) { return *modes[m]; }
    juce::ComboBox& quantBox() { return quant; }
    juce::PopupMenu slotMenuFor (int i) const { return slotMenu (i); }
    void slotMenuResult (int i, int r) { slotMenuPicked (i, r); }
    juce::PopupMenu moreMenuFor() const { return moreMenu(); }
    void moreMenuResult (int r) { morePicked (r); }
    struct Text { juce::Rectangle<int> r; juce::Colour c; juce::String what; };
    std::vector<Text> texts();   // where the bar draws its texts and in which colour (contrast tests)

private:
    struct Slot : juce::Button   // (painted through the rack's look-and-feel; the press / release logic is its own, not the Button state machine)
    {
        Slot (BoxSceneBar& o, int k) : juce::Button ("scene " + juce::String (k + 1)), bar (o), i (k) { setColour (juce::TextButton::buttonOnColourId, gpfx::ui::col::ecto); setTooltip (tip()); }
        BoxSceneBar& bar; int i; double downAt = 0; bool pressed = false;
        juce::String tip() const { return "Click: this scene (it stays). Hold: the scene while held, back on release. Empty: a click stores what you hear. Right-click: store, rename, clear, copy"; }
        void paintButton (juce::Graphics& g, bool over, bool down) override;
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu()) { bar.showSlotMenu (i); return; }
            auto& p = bar.proc;
            if (! p.sceneBank.slots[(size_t) i].filled()) { p.sceneStore (i); bar.message ("Stored what you hear in scene " + juce::String (i + 1) + " " + p.sceneBank.slots[(size_t) i].name); return; }
            p.scenePress (i); pressed = true; downAt = juce::Time::getMillisecondCounterHiRes();
        }
        void mouseUp (const juce::MouseEvent&) override
        {
            if (! pressed) return;
            pressed = false;
            if (juce::Time::getMillisecondCounterHiRes() - downAt < 350.0) bar.proc.sceneClickUp (i); else bar.proc.sceneRelease (i);
        }
    };
    BoxProcessor& proc;
    juce::OwnedArray<Slot> slots; juce::OwnedArray<juce::TextButton> modes;
    juce::ComboBox quant, morph; juce::TextButton midi, more;
    juce::Rectangle<int> label;
    double blink = 0;
    void message (const juce::String& m) { if (onMessage) onMessage (m); }
    void timerCallback() override
    {
        blink += 0.05;
        auto& e = proc.sceneEngine();
        const int a = e.active.load(), pd = e.pending.load(), sc = e.spillCount.load(); const float pr = e.progress.load();
        if (a != seenActive || pd != seenPending || pd >= 0 || pr < 1.0f || sc != seenSpill || std::abs (pr - seenProgress) > 1.0e-6f)
        { seenActive = a; seenPending = pd; seenSpill = sc; seenProgress = pr; syncLit(); for (auto* s : slots) s->repaint(); }
    }
    void syncLit() { for (int i = 0; i < slots.size(); ++i) slots[i]->setToggleState (proc.sceneBank.slots[(size_t) i].filled() && proc.sceneEngine().active.load() == i, juce::dontSendNotification); }
    int seenActive = -2, seenPending = -2, seenSpill = -1; float seenProgress = -1.0f;

    juce::PopupMenu slotMenu (int i) const
    {
        const auto& s = proc.sceneBank.slots[(size_t) i];
        juce::PopupMenu m; m.addSectionHeader ("SCENE " + juce::String (i + 1) + "  " + s.name);
        m.addItem (1, s.filled() ? "Store what you hear here (replace)" : "Store what you hear here");
        m.addItem (2, "Rename...");
        m.addItem (3, "Clear", s.filled());
        juce::PopupMenu cp; for (int k = 0; k < boxscenes::kSlots; ++k) if (k != i) cp.addItem (100 + k, juce::String (k + 1) + "  " + proc.sceneBank.slots[(size_t) k].name, s.filled());
        m.addSubMenu ("Copy to", cp, s.filled());
        m.addSeparator();
        m.addItem (4, "Undo the last scene edit", proc.sceneBank.canUndo());
        return m;
    }
    void slotMenuPicked (int i, int r)
    {
        auto& p = proc;
        if (r == 1) { p.sceneStore (i); message ("Stored what you hear in scene " + juce::String (i + 1) + " " + p.sceneBank.slots[(size_t) i].name); }
        else if (r == 2) askRename (i);
        else if (r == 3) { p.sceneClear (i); message ("Scene " + juce::String (i + 1) + " cleared (undo in the menu)"); }
        else if (r == 4) { if (p.sceneUndo()) message ("Scene edit undone"); }
        else if (r >= 100 && r < 100 + boxscenes::kSlots) { p.sceneCopy (i, r - 100); message ("Scene " + juce::String (i + 1) + " copied to " + juce::String (r - 99)); }
        refresh();
    }
    void showSlotMenu (int i)
    {
        slotMenu (i).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (slots[i]), [sp = juce::Component::SafePointer<BoxSceneBar> (this), i] (int r) { if (sp && r != 0) sp->slotMenuPicked (i, r); });
    }
    juce::PopupMenu moreMenu() const
    {
        const auto& st = proc.sceneBank.set;
        juce::PopupMenu m; m.addSectionHeader ("GHOST SCENES");
        m.addItem (1, "MIDI keys trigger scenes", true, st.midiOn);
        m.addItem (2, "MIDI: HOLD (the scene while the key is down)", st.midiOn, ! st.midiLatch);
        m.addItem (3, "MIDI: LATCH (a key switches)", st.midiOn, st.midiLatch);
        juce::PopupMenu base;
        for (int n : { 24, 36, 48, 60, 72, 84 }) base.addItem (1000 + n, boxscenes::noteName (n) + " - " + boxscenes::noteName (n + 7) + "  (MIDI " + juce::String (n) + ")", true, st.midiBase == n);
        m.addSubMenu ("MIDI keys from", base, st.midiOn);
        m.addSeparator();
        m.addItem (4, "Fill the empty slots with starter scenes");
        m.addItem (5, "Undo the last scene edit", proc.sceneBank.canUndo());
        m.addSeparator();
        m.addItem (6, "Open GHOST CAPTURE (the CAPTURE tab)");   // (also reachable when a narrow window hides the tab)
        m.addItem (7, "Open GHOST DNA (the DNA tab)");
        m.addItem (8, "Open GHOST SPLIT (the SPLITS tab)");
        m.addItem (9, "Open GHOST FOCUS (the FOCUS tab)");
        m.addItem (10, "Open GHOSTPRINT (the PRINT tab)");
        m.addItem (11, "Open GHOST SPACE (the G-SPACE tab)");
        return m;
    }
    void morePicked (int r)
    {
        auto& st = proc.sceneBank.set;
        if (r == 1) st.midiOn = ! st.midiOn;
        else if (r == 2) st.midiLatch = false;
        else if (r == 3) st.midiLatch = true;
        else if (r >= 1000 && r < 1128) st.midiBase = r - 1000;
        else if (r == 4) { const int n = proc.sceneFillStarters(); message (n > 0 ? juce::String (n) + " starter scenes added (undo in the menu)" : "No empty slot left"); }
        else if (r == 5) { if (proc.sceneUndo()) message ("Scene edit undone"); }
        else if (r >= 6 && r <= 11) { openTab (r); return; }   // (menu item 6 .. 11 = panel modes 6 .. 11: CAPTURE, DNA, SPLITS, FOCUS, PRINT, G-SPACE)
        proc.sceneSettingsChanged(); refresh();
    }
    void openTab (int mode)
    {   // the rack panel's own page switch (modes 6 .. 11 = the host pages CAPTURE / DNA / SPLITS / FOCUS / PRINT / G-SPACE)
        if (auto* parent = getParentComponent())
            for (auto* c : parent->getChildren())
                if (dynamic_cast<gpfx::Panel*> (c) != nullptr) { c->getProperties().set ("mode", mode); c->resized(); }
    }
    void showMoreMenu() { moreMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&more), [sp = juce::Component::SafePointer<BoxSceneBar> (this)] (int r) { if (sp && r != 0) sp->morePicked (r); }); }
    void askRename (int i)
    {
        auto* w = new juce::AlertWindow ("Rename scene " + juce::String (i + 1), "New name:", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("n", proc.sceneBank.slots[(size_t) i].name); w->addButton ("RENAME", 1, juce::KeyPress (juce::KeyPress::returnKey)); w->addButton ("Cancel", 0);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([sp = juce::Component::SafePointer<BoxSceneBar> (this), w, i] (int res) {
            if (sp && res == 1) { sp->proc.sceneRename (i, w->getTextEditorContents ("n")); sp->refresh(); } }), true);
    }
};

inline void BoxSceneBar::Slot::paintButton (juce::Graphics& g, bool over, bool)
{
    namespace gg = ghosty::glass; using namespace gpfx::ui;
    auto& p = bar.proc; auto& e = p.sceneEngine();
    const auto& s = p.sceneBank.slots[(size_t) i];
    const bool filled = s.filled(), active = filled && e.active.load() == i, pending = e.pending.load() == i;
    const bool ghostLook = ghost();
    if (getToggleState() != active) setToggleState (active, juce::dontSendNotification);   // (lit = the active scene, also between the bar's timer ticks)
    auto r = getLocalBounds().toFloat();
    if (filled) getLookAndFeel().drawButtonBackground (g, *this, findColour (juce::TextButton::buttonColourId), over, false);
    else
    {   // empty: a dashed outline (click = store)
        auto rr = r.reduced (1.5f);
        juce::Path outline; outline.addRoundedRectangle (rr, 7.0f);
        juce::Path dashed; const float dash[] { 4.0f, 3.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dash, 2);
        const auto lineC = ghostLook ? gg::ink2().withAlpha (over ? 0.9f : 0.6f) : col::dim.withAlpha (over ? 0.95f : 0.7f);
        g.setColour (lineC); g.fillPath (dashed);
    }
    if (pending)
    {   // waiting for the grid: a blinking ring
        const float a = 0.45f + 0.45f * (float) std::sin (bar.blink * 12.0);
        g.setColour ((ghostLook ? gg::violetDeep() : col::pink.get()).withAlpha (a)); g.drawRoundedRectangle (r.reduced (1.0f), 7.0f, 2.0f);
    }
    if (active && e.spillCount.load() > 0)
    {   // a SPILL tail rings: a soft outer glow
        g.setColour ((ghostLook ? gg::teal() : col::ice.get()).withAlpha (0.55f)); g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.2f);
    }
    // texts: number, name, MIDI key
    const auto ink = ghostLook ? (active ? juce::Colours::white : gg::ink()) : active ? gpfx::ui::LnF::litColours (findColour (juce::TextButton::buttonOnColourId)).second : col::white.get();
    const auto ink2 = ghostLook ? (active ? juce::Colours::white.withAlpha (0.85f) : gg::ink2()) : active ? ink.withAlpha (0.8f) : col::dim.get();
    // two lines: the number + the MIDI key (small), the name under them
    auto tr = (ghostLook && filled ? gg::pillBody (r) : r).reduced (6.0f, 1.0f);
    auto top = tr.removeFromTop (juce::jmin (11.0f, tr.getHeight() * 0.38f));
    g.setColour (ink2); g.setFont (font (8.5f)); g.drawText (juce::String (i + 1), top, juce::Justification::bottomLeft, false);
    if (p.sceneBank.set.midiOn) g.drawText (boxscenes::noteName (p.sceneBank.set.midiBase + i), top, juce::Justification::bottomRight, false);
    g.setColour (filled ? ink : ink2); g.setFont (font (juce::jmin (12.0f, tr.getHeight() * 0.75f)));
    g.drawFittedText (s.name, tr.toNearestInt(), juce::Justification::centred, 1, 0.8f);
    if (active && e.progress.load() < 1.0f)
    {   // MORPH progress along the bottom edge
        const auto pb = r.reduced (6.0f, 0.0f).removeFromBottom (3.0f).translated (0.0f, -2.0f);
        g.setColour (ink.withAlpha (0.25f)); g.fillRoundedRectangle (pb, 1.5f);
        g.setColour (ink.withAlpha (0.9f)); g.fillRoundedRectangle (pb.withWidth (pb.getWidth() * e.progress.load()), 1.5f);
    }
}

inline std::vector<BoxSceneBar::Text> BoxSceneBar::texts()
{
    using namespace gpfx::ui; namespace gg = ghosty::glass;
    std::vector<Text> v;
    v.push_back ({ label.withWidth ((int) ghosty::textWidth (font (11.5f), "GHOST SCENES")).reduced (0, 6), ghost() ? gg::violetDeep() : col::ecto.get(), "label" });
    for (int i = 0; i < boxscenes::kSlots; ++i)
    {
        auto* s = slots[i]; const auto& sl = proc.sceneBank.slots[(size_t) i];
        const bool active = sl.filled() && proc.sceneEngine().active.load() == i;
        const auto ink = ghost() ? (active ? juce::Colours::white : gg::ink()) : active ? LnF::litColours (s->findColour (juce::TextButton::buttonOnColourId)).second : col::white.get();
        auto inner = s->getBounds().toFloat().reduced (6.0f, 1.0f); inner.removeFromTop (juce::jmin (11.0f, inner.getHeight() * 0.38f));
        const int tw = juce::jmin ((int) inner.getWidth() - 2, (int) ghosty::textWidth (font (12.0f), sl.name));
        const auto r = inner.toNearestInt().withSizeKeepingCentre (tw, 9);
        v.push_back ({ r, sl.filled() ? ink : (ghost() ? gg::ink2() : col::dim.get()), "slot " + juce::String (i + 1) });
    }
    return v;
}
