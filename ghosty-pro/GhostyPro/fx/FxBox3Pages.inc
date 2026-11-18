// Ghost Box 3 pages (included by FxPanel.cpp inside its anonymous namespace, GPFX_BOX builds only):
// MORPH, MOTION, CHAIN BRAIN, PERFORM and the MACRO strip.

constexpr fxtheme::C kMorphA { 0xff45E6FF }, kMorphB { 0xffFF4FD8 }, kMotionC { 0xffC6FF4F }, kBrainC { 0xffFFC35A }, kMacroC { 0xffA06BFF };

void styleButton (juce::TextButton& b, juce::Colour c) { b.setColour (juce::TextButton::buttonOnColourId, c); b.setColour (juce::TextButton::textColourOffId, col::white); }

// Picks a factory or user scene (the menu used by MORPH's "A from scene" / "B from scene").
void sceneMenu (juce::Component* target, Rack::Impl& rack, std::function<void (std::function<void()>)> withLoad)
{
    juce::PopupMenu m; m.addSectionHeader ("LOAD A SCENE, THEN CAPTURE IT");
    const auto& fs = factoryScenes();
    for (int i = 0; i < (int) fs.size(); ++i) m.addItem (100 + i, fs[(size_t) i].display);
    auto files = listXml (sceneDir());
    if (! files.isEmpty()) { m.addSectionHeader ("Your scenes"); for (int i = 0; i < files.size(); ++i) m.addItem (1000 + i, files[i].getFileNameWithoutExtension()); }
    juce::Component::SafePointer<juce::Component> safe (target);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [safe, &rack, files, withLoad] (int r)
    {
        if (safe == nullptr || r <= 0) return;
        const auto& fs2 = factoryScenes();
        if (r >= 100 && r < 100 + (int) fs2.size()) { const juce::String key = fs2[(size_t) (r - 100)].key; withLoad ([&rack, key] { rack.owner->applyScene (*rack.apvts, key); }); }
        else if (r >= 1000 && r - 1000 < files.size()) { const auto f = files[r - 1000]; withLoad ([&rack, f] { rack.loadUserScene (f); }); }
    });
}

// ---------------------------------------------------------------- BROWSER: scenes + the selected unit's presets, typed search, favourites
juce::File favouritesFile() { return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Ghosty").getChildFile ("Ghost Box").getChildFile ("favourites.txt"); }
class Browser : public juce::Component, private juce::TextEditor::Listener, private juce::KeyListener
{
public:
    Browser (juce::AudioProcessorValueTreeState& s, Rack::Impl& r) : apvts (s), rack (r)
    {
        filter.setTextToShowWhenEmpty ("type to search scenes and presets...", col::dim);
        filter.setColour (juce::TextEditor::outlineColourId, col::slime.withAlpha (0.5f)); filter.setColour (juce::TextEditor::focusedOutlineColourId, col::slime);
        filter.addListener (this); filter.addKeyListener (this); addAndMakeVisible (filter);
        filter.setTooltip ("Type to filter. UP / DOWN: step through the list and hear each one. ENTER: keep it and close. ESC: close. Click the star: favourite.");
        favOnly.setButtonText ("FAVOURITES"); favOnly.setClickingTogglesState (true); styleButton (favOnly, col::gold); favOnly.onClick = [this] { rebuild(); }; addAndMakeVisible (favOnly);
        close.setButtonText ("CLOSE"); close.onClick = [this] { if (onClose) onClose(); }; addAndMakeVisible (close);
        view.setViewedComponent (&list, false); view.setScrollBarsShown (true, false); addAndMakeVisible (view);
        list.owner = this;
        loadFavs();
    }
    std::function<void()> onClose, onLoaded; std::function<int()> selectedUnit;
    void open() { sel = -1; rebuild(); filter.grabKeyboardFocus(); }
    ~Browser() override { filter.removeKeyListener (this); }
    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        auto top = r.removeFromTop (30);
        close.setBounds (top.removeFromRight (80).reduced (2, 2)); top.removeFromRight (6);
        favOnly.setBounds (top.removeFromRight (110).reduced (2, 2)); top.removeFromRight (8);
        filter.setBounds (top.reduced (0, 2));
        r.removeFromTop (8); view.setBounds (r); list.setSize (r.getWidth() - 12, juce::jmax (r.getHeight(), (int) rows.size() * 24 + 8));
    }
    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (T (juce::Colour (0xfa0e1017))); g.fillRoundedRectangle (b, 12);
        g.setColour (col::slime.withAlpha (0.35f)); g.drawRoundedRectangle (b.reduced (0.5f), 12, 1.2f);
    }
    void textEditorTextChanged (juce::TextEditor&) override { sel = -1; rebuild(); }
    void textEditorReturnKeyPressed (juce::TextEditor&) override
    {
        if (rows.empty()) return;
        if (! juce::isPositiveAndBelow (sel, (int) rows.size())) load (rows[0]);   // nothing auditioned yet: the first match
        if (onClose) onClose();
    }
    bool keyPressed (const juce::KeyPress& k, juce::Component*) override
    {   // UP / DOWN audition: each step loads (click-free, undoable), so a pro can flip through sounds without the mouse
        const int dir = k == juce::KeyPress::downKey ? 1 : k == juce::KeyPress::upKey ? -1 : 0;
        if (dir == 0 || rows.empty()) return false;
        sel = juce::jlimit (0, (int) rows.size() - 1, sel < 0 ? (dir > 0 ? 0 : (int) rows.size() - 1) : sel + dir);
        load (rows[(size_t) sel]);
        view.setViewPosition (0, juce::jlimit (0, juce::jmax (0, list.getHeight() - view.getHeight()), sel * 24 - view.getHeight() / 2));
        list.repaint();
        return true;
    }
    int selectedRow() const { return sel; }
    void textEditorEscapeKeyPressed (juce::TextEditor&) override { if (onClose) onClose(); }

    struct Row { int kind = 0; juce::String key, display, section; int index = -1; juce::File file; };   // kind: 0 scene, 1 user scene, 2 unit preset, 3 user unit preset
    // true when this row is what is loaded right now (scene name / the selected unit's preset)
    bool isCurrent (const Row& r) const
    {
        const auto clean = [] (juce::String t) { return t.upToFirstOccurrenceOf (" (edited)", false, false).trim(); };
        if (r.kind <= 1) return clean (rack.sceneLabel()).equalsIgnoreCase (r.display);
        return clean (rack.unitLabel (selectedUnit ? selectedUnit() : 0)).equalsIgnoreCase (r.display);
    }
    juce::String favKey (const Row& r) const { return juce::String (r.kind) + ":" + ((r.kind == 2 || r.kind == 3) ? juce::String (unitInfo (selectedUnit ? selectedUnit() : 0).key) + ":" : juce::String()) + r.key; }
    bool isFav (const Row& r) const { return favs.contains (favKey (r)); }
    void toggleFav (const Row& r) { const auto k = favKey (r); if (favs.contains (k)) favs.removeString (k); else favs.add (k); saveFavs(); rebuild(); }
    void rebuild()
    {
        rows.clear();
        const auto f = filter.getText().trim(); const bool fo = favOnly.getToggleState();
        auto add = [&] (Row r) { if ((f.isEmpty() || r.display.containsIgnoreCase (f) || r.section.containsIgnoreCase (f)) && (! fo || isFav (r))) rows.push_back (r); };
        if (rack.snapshots != nullptr)   // SNAPSHOTS first (newest first): what you saved with SNAPSHOT / Shift+Alt+S
            for (int i = 0; i < rack.snapshots->size(); ++i) add ({ 5, rack.snapshots->info (i).file.getFileName(), rack.snapshots->info (i).name, "SNAPSHOT", i, rack.snapshots->info (i).file });
        for (auto& sc : factoryScenes()) add ({ 0, sc.key, sc.display, "SCENE", -1, {} });
        for (auto& file : listXml (sceneDir())) add ({ 1, file.getFileNameWithoutExtension(), file.getFileNameWithoutExtension(), "YOUR SCENE", -1, file });
        const int u = selectedUnit ? selectedUnit() : 0; const auto& ps = unitPresets (u);
        for (int i = 0; i < (int) ps.size(); ++i) add ({ 2, ps[(size_t) i].name, ps[(size_t) i].name, juce::String (unitInfo (u).title) + " PRESET", i, {} });
        for (auto& file : listXml (unitPresetDir (u))) add ({ 3, file.getFileNameWithoutExtension(), file.getFileNameWithoutExtension(), juce::String (unitInfo (u).title) + " YOUR PRESET", -1, file });
        for (auto& file : listXml (boardDir())) add ({ 4, file.getFileNameWithoutExtension(), file.getFileNameWithoutExtension(), "SPIRIT BOARD", -1, file });
        // favourites first
        std::stable_sort (rows.begin(), rows.end(), [this] (const Row& a, const Row& b) { return isFav (a) && ! isFav (b); });
        resized(); list.repaint();
    }
    void load (const Row& r)
    {
        const int u = selectedUnit ? selectedUnit() : 0;
        if (r.kind == 0) rack.owner->applyScene (*rack.apvts, r.key);
        else if (r.kind == 1) rack.loadUserScene (r.file);
        else if (r.kind == 2) rack.applyUnitPreset (u, r.index);
        else if (r.kind == 4) rack.loadBoardFile (r.file);
        else if (r.kind == 5) { if (rack.snapshots != nullptr) { const int i = rack.snapshots->indexOfFile (r.file); if (i >= 0) rack.snapshots->load (i); } }
        else rack.loadUserPreset (u, r.file);
        if (onLoaded) onLoaded();
    }
private:
    struct List : juce::Component
    {
        Browser* owner = nullptr; int hover = -1;
        void paint (juce::Graphics& g) override
        {
            juce::String lastSection;
            for (int i = 0; i < (int) owner->rows.size(); ++i)
            {
                const auto& r = owner->rows[(size_t) i]; auto rr = juce::Rectangle<float> (0, (float) i * 24, (float) getWidth(), 24);
                if (i == owner->selectedRow()) { g.setColour (col::slime.withAlpha (0.14f)); g.fillRoundedRectangle (rr.reduced (2, 1), 5); }
                else if (i == hover) { g.setColour (col::white.withAlpha (0.06f)); g.fillRoundedRectangle (rr.reduced (2, 1), 5); }
                if (owner->isCurrent (r)) { g.setColour (col::slime); g.fillRoundedRectangle (rr.getX() + 2, rr.getY() + 5, 3, rr.getHeight() - 10, 1.5f); }
                {   // the favourite star: always visible (outline), gold when set
                    const bool fav = owner->isFav (r);
                    juce::Path star; star.addStar (rr.withX (8).withWidth (20).getCentre(), 5, 3.0f, 7.0f, -juce::MathConstants<float>::halfPi);
                    if (fav) { g.setColour (col::gold); g.fillPath (star); }
                    else { g.setColour (col::dim.withAlpha (i == hover ? 0.9f : 0.45f)); g.strokePath (star, juce::PathStrokeType (1.0f)); }
                }
                g.setColour (r.kind == 0 || r.kind == 1 ? col::slime : accentOf (owner->selectedUnit ? owner->selectedUnit() : 0)); g.setFont (font (12.0f));
                g.drawText (r.display, rr.withTrimmedLeft (32).withTrimmedRight (180).toNearestInt(), juce::Justification::centredLeft, true);
                drawCaption (g, rr.withTrimmedLeft (rr.getWidth() - 176).withTrimmedRight (8), r.section, col::dim, 9.5f, juce::Justification::centredRight);
            }
            if (owner->rows.empty()) drawCaption (g, { 12, 8, 400, 16 }, "nothing matches - try another word, or switch FAVOURITES off", col::dim, 11);
        }
        void mouseMove (const juce::MouseEvent& e) override { const int i = e.y / 24; if (i != hover) { hover = i; repaint(); } }
        void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
        void mouseDown (const juce::MouseEvent& e) override
        {
            const int i = e.y / 24; if (! juce::isPositiveAndBelow (i, (int) owner->rows.size())) return;
            if (e.x < 30) owner->toggleFav (owner->rows[(size_t) i]); else { owner->sel = i; owner->load (owner->rows[(size_t) i]); }
        }
    };
    void loadFavs() { favs.clear(); const auto f = favouritesFile(); if (f.existsAsFile()) favs.addLines (f.loadFileAsString()); favs.removeEmptyStrings(); }
    void saveFavs() { auto f = favouritesFile(); f.getParentDirectory().createDirectory(); f.replaceWithText (favs.joinIntoString ("\n")); }
    juce::AudioProcessorValueTreeState& apvts; Rack::Impl& rack;
    juce::TextEditor filter; juce::TextButton favOnly, close; juce::Viewport view; List list; std::vector<Row> rows; juce::StringArray favs; int sel = -1;
};

// ---------------------------------------------------------------- MORPH
class MorphPage : public Page
{
public:
    MorphPage (juce::AudioProcessorValueTreeState& s, Rack::Impl& r) : apvts (s), rack (r), morph (s, "box_morph", "MORPH", kMorphB)
    {
        morph.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
        addAndMakeVisible (morph);
        for (auto* b : { &setA, &setB, &swap, &clear, &fromA, &fromB, &onBtn }) addAndMakeVisible (b);
        setA.setButtonText ("SET A"); setB.setButtonText ("SET B"); swap.setButtonText ("SWAP A/B"); clear.setButtonText ("CLEAR");
        fromA.setButtonText ("A FROM SCENE..."); fromB.setButtonText ("B FROM SCENE..."); onBtn.setButtonText ("MORPH ON"); onBtn.setClickingTogglesState (true);
        styleButton (setA, kMorphA); styleButton (setB, kMorphB); styleButton (onBtn, kMorphB);
        setA.setTooltip ("Capture the whole rack as it sounds now as state A");
        setB.setTooltip ("Capture the whole rack as it sounds now as state B (MORPH switches on)");
        swap.setTooltip ("Swap A and B"); clear.setTooltip ("Forget A and B (UNDO brings them back)");
        fromA.setTooltip ("Load a scene and capture it as A"); fromB.setTooltip ("Load a scene and capture it as B");
        setA.onClick = [this] { rack.morphSet (0); changed(); };
        setB.onClick = [this] { rack.morphSet (1); changed(); };
        swap.onClick = [this] { rack.morphSwap(); changed(); };
        clear.onClick = [this] { rack.morphClear(); changed(); };
        onBtn.onClick = [this] { rack.morphEnable (onBtn.getToggleState()); changed(); };
        fromA.onClick = [this] { sceneMenu (&fromA, rack, [this] (std::function<void()> load) { load(); rack.morphSet (0); changed(); }); };
        fromB.onClick = [this] { sceneMenu (&fromB, rack, [this] (std::function<void()> load) { load(); rack.morphSet (1); changed(); }); };
        curve.addItemList (morphCurveNames(), 1); curve.setColour (juce::ComboBox::outlineColourId, kMorphB); curve.setColour (juce::ComboBox::arrowColourId, kMorphB);
        curve.onChange = [this] { if (curve.getSelectedItemIndex() >= 0 && curve.getSelectedItemIndex() != rack.box.getMorphCurve()) { rack.pushUndo(); rack.box.setMorphCurve (curve.getSelectedItemIndex()); } };
        curve.setTooltip ("How the knob travels from A to B");
        addAndMakeVisible (curve);
        changed();
    }
    std::function<void()> onChanged;
    void changed()
    {
        onBtn.setToggleState (rack.box.isMorphOn(), juce::dontSendNotification);
        onBtn.setEnabled (rack.box.hasState (0) && rack.box.hasState (1));
        swap.setEnabled (rack.box.hasState (0) || rack.box.hasState (1));
        curve.setSelectedItemIndex (rack.box.getMorphCurve(), juce::dontSendNotification);
        buildList();
        if (onChanged) onChanged();
        repaint();
    }
    void resized() override
    {
        auto r = getLocalBounds();
        auto left = r.removeFromLeft (300);
        auto row1 = left.removeFromTop (30); setA.setBounds (row1.removeFromLeft (row1.getWidth() / 2).reduced (3, 1)); setB.setBounds (row1.reduced (3, 1));
        left.removeFromTop (4);
        auto row2 = left.removeFromTop (28); fromA.setBounds (row2.removeFromLeft (row2.getWidth() / 2).reduced (3, 1)); fromB.setBounds (row2.reduced (3, 1));
        auto bottom = left.removeFromBottom (28);
        swap.setBounds (bottom.removeFromLeft (bottom.getWidth() / 3).reduced (3, 1)); clear.setBounds (bottom.removeFromLeft (bottom.getWidth() / 2).reduced (3, 1)); onBtn.setBounds (bottom.reduced (3, 1));
        left.removeFromBottom (4);
        auto cr = left.removeFromBottom (26); curveCap = cr.removeFromLeft (70); curve.setBounds (cr.reduced (3, 1));
        morph.setBounds (left.reduced (30, 6));
        vis = r.withTrimmedLeft (12);
    }
    void paint (juce::Graphics& g) override
    {
        drawCaption (g, curveCap.toFloat().withTrimmedLeft (6), "CURVE", col::dim.brighter (0.25f), 10);
        auto v = vis.toFloat(); drawScreen (g, v, kMorphB);
        juce::Graphics::ScopedSaveState ss (g); g.reduceClipRegion (vis.reduced (2));
        const float m = getParam (apvts, "box_morph");
        // the morph track: ghost A, ghost B and where the knob is
        auto track = v.reduced (24, 0).withHeight (64).withY (v.getY() + 14);
        const float y = track.getCentreY();
        juce::ColourGradient gr (kMorphA, track.getX(), y, kMorphB, track.getRight(), y, false);
        g.setGradientFill (gr); g.fillRoundedRectangle (track.getX(), y - 2, track.getWidth(), 4, 2);
        const bool hA = rack.box.hasState (0), hB = rack.box.hasState (1);
        ghosty::drawGhost (g, ghosty::lookFor (3), track.getX() + 10, y - 2, 22, 0, false, hA ? 1.0f : 0.3f);
        ghosty::drawGhost (g, ghosty::lookFor (11), track.getRight() - 10, y - 2, 22, 0, false, hB ? 1.0f : 0.3f);
        drawCaption (g, { track.getX() - 4, y + 18, 60, 14 }, hA ? "A" : "A  (empty)", kMorphA, 11);
        drawCaption (g, { track.getRight() - 90, y + 18, 94, 14 }, hB ? "B" : "(empty)  B", kMorphB, 11, juce::Justification::centredRight);
        const float x = track.getX() + 30 + (track.getWidth() - 60) * m;
        glowDot (g, { x, y }, 6.0f, kMorphA.interpolatedWith (kMorphB, m));
        drawCaption (g, { x - 40, y - 26, 80, 14 }, juce::String (juce::roundToInt (m * 100)) + " %", col::white, 11, juce::Justification::centred);
        drawCaption (g, { v.getX() + 14, track.getBottom() + 8, v.getWidth() - 28, 14 }, rack.box.morphNote() + (rack.box.isMorphOn() ? "" : (hA && hB ? "  -  MORPH is OFF" : "")), col::white.withAlpha (0.8f), 11);
        // what moves: the biggest differences, each with its own A..B bar and the live position
        auto list = v.reduced (14, 0).withTrimmedTop (track.getBottom() - v.getY() + 30).withTrimmedBottom (8);
        const int rows = juce::jmin ((int) items.size(), juce::jmax (1, (int) (list.getHeight() / 17.0f)) * 2);
        const float cw = list.getWidth() / 2.0f;
        for (int i = 0; i < rows; ++i)
        {
            const auto& it = items[(size_t) i];
            const int col_ = i % 2, row = i / 2;
            auto rr = juce::Rectangle<float> (list.getX() + col_ * cw, list.getY() + row * 17.0f, cw - 12, 15);
            drawCaption (g, rr.withWidth (cw * 0.48f), it.label, col::dim.brighter (0.3f), 9.5f);
            auto bar = rr.withTrimmedLeft (cw * 0.5f).reduced (0, 5);
            g.setColour (col::white.withAlpha (0.08f)); g.fillRoundedRectangle (bar, 2);
            const float now = it.param != nullptr ? it.param->convertTo0to1 (rack.box.effective (it.id)) : 0.0f;
            const float ax = bar.getX() + bar.getWidth() * it.a01, bx = bar.getX() + bar.getWidth() * it.b01;
            g.setColour (kMorphA.interpolatedWith (kMorphB, 0.5f).withAlpha (0.35f)); g.fillRect (juce::jmin (ax, bx), bar.getY(), std::abs (bx - ax) + 1, bar.getHeight());
            g.setColour (kMorphA); g.fillEllipse (ax - 2.5f, bar.getCentreY() - 2.5f, 5, 5);
            g.setColour (kMorphB); g.fillEllipse (bx - 2.5f, bar.getCentreY() - 2.5f, 5, 5);
            g.setColour (col::white); g.fillRect (bar.getX() + bar.getWidth() * now - 1, bar.getY() - 3, 2.0f, bar.getHeight() + 6);
        }
        if (items.empty())
        {   // wrapped to the width (it used to run off the right edge at 1180 px)
            g.setColour (col::dim.brighter (0.2f)); g.setFont (font (11.0f));
            g.drawFittedText ("1. Set up the rack   2. SET A   3. change anything (units on/off, knobs)   4. SET B   5. turn MORPH.\n"
                              "Every setting glides between A and B; switches flip half way; delay times and pitch jump with a short crossfade.",
                              list.withHeight (90).toNearestInt(), juce::Justification::topLeft, 6, 1.0f);
        }
    }
    void tick (double) override { repaint (vis); }
private:
    struct Item { juce::String id, label; float a01, b01; juce::RangedAudioParameter* param; };
    void buildList()
    {
        items.clear();
        if (! rack.box.hasState (0) || ! rack.box.hasState (1)) return;
        const auto& A = rack.box.state (0).vals; const auto& B = rack.box.state (1).vals;
        for (auto& [id, a] : A)
        {
            auto it = B.find (id); if (it == B.end() || it->second == a) continue;
            auto* p = apvts.getParameter (id); if (p == nullptr) continue;
            items.push_back ({ id, p->getName (40).replace ("FX ", ""), p->convertTo0to1 (a), p->convertTo0to1 (it->second), p });
        }
        std::sort (items.begin(), items.end(), [] (const Item& x, const Item& y) { return std::abs (x.a01 - x.b01) > std::abs (y.a01 - y.b01); });
    }
    juce::AudioProcessorValueTreeState& apvts; Rack::Impl& rack;
    Knob morph; juce::TextButton setA, setB, swap, clear, fromA, fromB, onBtn; juce::ComboBox curve;
    juce::Rectangle<int> vis, curveCap; std::vector<Item> items;
};

// ---------------------------------------------------------------- MOTION
juce::String laneTargetText (int lane, bool units)
{
    switch (lane)
    {
        case lnVolume: return "output volume (built in, before the limiter)";
        case lnFilter: return units ? "PHANTOM FILTER cutoff" : "built-in sweep filter (down = low-pass, up = high-pass)";
        case lnPitch:  return units ? "WARP amount" : "built-in pitch shift (+-12 st)";
        case lnSpace:  return units ? "mix of the space units that are on" : "built-in reverb send (no space unit on)";
        default:       return units ? "drive / crush / crackle of the units that are on" : "built-in saturation (no drive unit on)";
    }
}
// STEPS mode: one bar per step, drag to draw values (-1..1), right-click a step = tie (hold the previous step), double-click = 0
class StepEditor : public juce::Component, private juce::Timer
{
public:
    StepEditor (Rack::Impl& r, int laneIndex) : rack (r), lane (laneIndex) { startTimerHz (30); }
    ~StepEditor() override { stopTimer(); }
    juce::Colour accent { col::slime }; std::function<void()> onEditStart; std::function<float()> playPos; std::function<juce::String (float)> valueText;
    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        juce::ColourGradient bg (T (juce::Colour (0xff07080c)), 0, b.getY(), T (juce::Colour (0xff0d0f17)), 0, b.getBottom(), false);
        g.setGradientFill (bg); g.fillRoundedRectangle (b, 10);
        g.setColour (accent.withAlpha (0.14f)); g.drawRoundedRectangle (b.reduced (0.5f), 10, 1.0f);
        const auto st = rack.box.getSteps (lane); const int n = st.count; const auto a = area();
        const float w = a.getWidth() / (float) n, mid = a.getCentreY();
        g.setColour (col::white.withAlpha (0.22f)); g.fillRect (a.getX(), mid - 0.5f, a.getWidth(), 1.0f);
        for (int i = 0; i < n; ++i)
        {
            const float x = a.getX() + i * w; const bool tie = (st.ties >> i) & 1u;
            if (i % 4 == 0) { g.setColour (col::white.withAlpha (i % 16 == 0 ? 0.18f : 0.08f)); g.fillRect (x - 0.5f, a.getY(), 1.0f, a.getHeight()); }
            const float v = st.valueAt ((i + 0.5) / n);
            auto bar = juce::Rectangle<float> (x + 1.5f, juce::jmin (mid, mid - v * a.getHeight() * 0.5f), juce::jmax (1.0f, w - 3.0f), std::abs (v) * a.getHeight() * 0.5f);
            g.setColour (accent.withAlpha (tie ? 0.25f : (i == hover ? 0.95f : 0.7f))); g.fillRoundedRectangle (bar, 2.0f);
            if (tie) { g.setColour (accent.withAlpha (0.8f)); g.fillRect (x + 1.5f, mid - v * a.getHeight() * 0.5f - 1.0f, w - 3.0f, 2.0f); }
            if (std::abs (v) < 0.001f && ! tie) { g.setColour (accent.withAlpha (0.35f)); g.fillRect (x + 1.5f, mid - 1.0f, w - 3.0f, 2.0f); }
        }
        g.setColour (col::dim); g.setFont (font (9.0f));
        g.drawText ("STEPS  " + juce::String (n) + "   drag = value, right-click = tie, double-click = 0", juce::Rectangle<float> (a.getX(), b.getBottom() - 15, a.getWidth(), 12), juce::Justification::centredLeft);
        for (float m : { 1.0f, 0.0f, -1.0f })
        {
            const float y = mid - m * a.getHeight() * 0.5f;
            g.drawText (valueText ? valueText (m) : (m > 0 ? "+" : "") + juce::String ((int) (m * 100)) + "%", juce::Rectangle<float> (b.getX() + 2, y - 6, a.getX() - b.getX() - 6, 12), juce::Justification::centredRight);
        }
        const float pp = playPos ? playPos() : -1.0f;
        if (pp >= 0.0f) { const float x = a.getX() + a.getWidth() * juce::jlimit (0.0f, 1.0f, pp); g.setColour (col::white.withAlpha (0.45f)); g.fillRect (x - 0.75f, a.getY(), 1.5f, a.getHeight()); }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = stepAt (e.position.x); if (i < 0) return;
        if (onEditStart) onEditStart();
        const auto st = rack.box.getSteps (lane);
        if (e.mods.isPopupMenu()) { rack.box.setTie (lane, i, ! ((st.ties >> i) & 1u)); repaint(); return; }
        if (e.getNumberOfClicks() >= 2) { rack.box.setStep (lane, i, 0.0f); rack.box.setTie (lane, i, false); repaint(); return; }
        apply (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) apply (e); }
    void mouseMove (const juce::MouseEvent& e) override { const int i = stepAt (e.position.x); if (i != hover) { hover = i; repaint(); } }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
private:
    void apply (const juce::MouseEvent& e)
    {
        const int i = stepAt (e.position.x); if (i < 0) return;
        const auto a = area(); float v = juce::jlimit (-1.0f, 1.0f, (a.getCentreY() - e.position.y) / (a.getHeight() * 0.5f));
        if (! e.mods.isAltDown()) v = std::round (v * 12.0f) / 12.0f;   // 1/12 grid (a semitone on the pitch lane); ALT = free
        rack.box.setStep (lane, i, v); rack.box.setTie (lane, i, false); repaint();
    }
    juce::Rectangle<float> area() const { auto b = getLocalBounds().toFloat(); return { b.getX() + 44, b.getY() + 10, b.getWidth() - 54, b.getHeight() - 30 }; }
    int stepAt (float px) const { const auto a = area(); const int n = rack.box.getSteps (lane).count; if (px < a.getX() || px > a.getRight()) return -1; return juce::jlimit (0, n - 1, (int) ((px - a.getX()) / a.getWidth() * n)); }
    void timerCallback() override { const float pp = playPos ? playPos() : -1.0f; if (std::abs (pp - lastPos) > 1.0e-4f) { lastPos = pp; repaint(); } }
    Rack::Impl& rack; int lane, hover = -1; float lastPos = -2;
};

class MotionPage : public Page
{
public:
    MotionPage (juce::AudioProcessorValueTreeState& s, Rack::Impl& r) : apvts (s), rack (r), resetT (s, "box_mo_reset", "RESET ON START", kMotionC)
    {
        resetT.setTooltip ("On: every lane counts from the moment the transport starts. Off: lanes follow the song position (bar 1 = lane start)");
        addAndMakeVisible (resetT);
        for (int k = 0; k < kLanes; ++k)
        {
            auto* se = stepEds.add (new StepEditor (rack, k));
            se->accent = laneColour (k); se->playPos = [this, k] { return rack.box.lanePos[(size_t) k].load(); }; se->onEditStart = [this] { rack.pushUndo(); };
            se->valueText = [k] (float v) { if (k == lnPitch) { const float st = v * 12.0f; return (st > 0.05f ? "+" : "") + juce::String (st, 0) + " st"; } const int pc = juce::roundToInt (v * 100); return (pc > 0 ? "+" : "") + juce::String (pc) + "%"; };
            addChildComponent (se);
            auto* e = editors.add (new bendlane::Editor (rack.box.lanes[(size_t) k]));
            e->accent = laneColour (k); e->ghostLook = 15 + k;
            e->rangeSemis = [] { return 1.0f; };
            e->bars = [this, k] { return motionBeats ((int) getParam (apvts, lp (k, "len")), (int) getParam (apvts, lp (k, "feel"))) / 4.0; };
            e->amount = [this, k] { return getParam (apvts, lp (k, "depth")); };
            e->playPos = [this, k] { return rack.box.lanePos[(size_t) k].load(); };
            e->onEditStart = [this] { rack.pushUndo(); };
            e->valueText = [k] (float v) { const int pc = juce::roundToInt (v * 100); return (pc > 0 ? "+" : "") + juce::String (pc) + (k == lnPitch ? "" : "%"); };
            if (k == lnPitch) e->valueText = [] (float v) { const float st = v * 12.0f; return (st > 0.05f ? "+" : "") + juce::String (st, std::abs (st - std::round (st)) < 0.05f ? 0 : 1) + " st"; };
            addChildComponent (e);
            auto* b = tabs.add (new juce::TextButton (laneName (k)));
            b->setClickingTogglesState (false); styleButton (*b, laneColour (k));
            b->onClick = [this, k] { show (k); };
            addAndMakeVisible (b);
        }
        dna.setButtonText ("MOTIONS..."); save.setButtonText ("SAVE MOTION..."); clearAll.setButtonText ("CLEAR ALL");
        styleButton (dna, kMotionC); styleButton (save, kMotionC);
        dna.setTooltip ("Factory motions and your saved MOTION DNA files");
        save.setTooltip ("Save all 5 lanes as a MOTION DNA file (Documents/Ghosty/Ghost Box/Motion)");
        dna.onClick = [this] { dnaMenu(); };
        save.onClick = [this]
        {
            askInPanel (*getParentComponent(), "Save motion", "Name for this motion:", true, rack.box.motionName, "Save", [this] (juce::String n)
            {
                auto m = rack.box.currentMotion(); m.name = n.trim();
                if (m.name.isNotEmpty() && saveMotionFile (m, motionDir().getChildFile (juce::File::createLegalFileName (m.name) + ".xml"))) rack.box.motionName = m.name;
                repaint();
            });
        };
        clearAll.onClick = [this] { rack.pushUndo(); rack.box.resetMotion(); repaint(); };
        for (auto* c : std::initializer_list<juce::Component*> { &dna, &save, &clearAll }) addAndMakeVisible (c);
        // GHOST MOTION timeline: one loop for the whole rack, any setting as a lane, AUDIO-AWARE events (FxBoxTimelinePage.inc)
        timelineB.setButtonText ("GHOST MOTION TIMELINE"); timelineB.setClickingTogglesState (false); styleButton (timelineB, kMotionC);
        timelineB.setTooltip ("GHOST MOTION timeline: one loop (1 - 8 bars) for the whole rack. Lanes = any setting, events placed by hand or on the hits of your audio. The 5 lanes above keep running");
        timelineB.onClick = [this] { setTimeline (! rack.box.tlView); };
        addAndMakeVisible (timelineB);
        show (0);
        if (rack.box.tlView) setTimeline (true);
    }
    void setTimeline (bool want)
    {
        rack.box.tlView = want; timelineB.setToggleState (want, juce::dontSendNotification);
        if (want && timeline == nullptr) { timeline = std::make_unique<TimelineView> (apvts, rack); addAndMakeVisible (*timeline); fxtheme::retint (*timeline); }
        if (! want) timeline.reset();
        for (auto* c : tabs) c->setVisible (! want);
        for (auto* c : std::initializer_list<juce::Component*> { on.get(), depth.get(), phase.get(), len.get(), feel.get(), mode.get() }) if (c) c->setVisible (! want);
        if (want) { for (auto* e : editors) e->setVisible (false); for (auto* e : stepEds) e->setVisible (false); if (stepsC) stepsC->setVisible (false); }
        else showEditor();
        resized(); repaint();
    }
    bool dropUnit (int u, juce::Point<int> screenPos) { return timeline != nullptr && timeline->dropUnit (u, screenPos); }
    TimelineView* timelineView() { return timeline.get(); }
    void show (int k)
    {
        sel = k;
        for (int i = 0; i < kLanes; ++i) tabs[i]->setToggleState (i == k, juce::dontSendNotification);
        on = std::make_unique<Toggle> (apvts, lp (k, "on"), "LANE ON", laneColour (k));
        depth = std::make_unique<Knob> (apvts, lp (k, "depth"), "DEPTH", laneColour (k));
        phase = std::make_unique<Knob> (apvts, lp (k, "phase"), "PHASE", laneColour (k));
        len = std::make_unique<Choice> (apvts, lp (k, "len"), laneColour (k));
        feel = std::make_unique<Choice> (apvts, lp (k, "feel"), laneColour (k));
        mode = std::make_unique<Segmented> (apvts, lp (k, "mode"), laneColour (k));
        stepsC = std::make_unique<Choice> (apvts, lp (k, "steps"), laneColour (k));
        mode->onChange = [this] (int) { showEditor(); };
        phase->slider.setTooltip ("Shifts the lane against the song: 50 % starts half way through the lane");
        for (auto* c : std::initializer_list<juce::Component*> { on.get(), depth.get(), phase.get(), len.get(), feel.get(), mode.get(), stepsC.get() }) addAndMakeVisible (c);
        showEditor(); resized(); repaint();
        if (timeline != nullptr) setTimeline (true);
    }
    void showEditor()
    {
        const bool steps = getParam (apvts, lp (sel, "mode")) > 0.5f, tl = timeline != nullptr;
        for (int i = 0; i < kLanes; ++i) { editors[i]->setVisible (! tl && i == sel && ! steps); stepEds[i]->setVisible (! tl && i == sel && steps); }
        if (stepsC) stepsC->setVisible (! tl && steps);
    }
    void resized() override
    {
        auto r = getLocalBounds();
        auto right = r.removeFromRight (230).withTrimmedLeft (10);
        if (timeline != nullptr) timeline->setBounds (r);   // (the timeline takes the lanes' place: tabs, editor, controls)
        auto tabRow = r.removeFromTop (30);
        const int tw = tabRow.getWidth() / kLanes;
        for (int i = 0; i < kLanes; ++i) tabs[i]->setBounds (tabRow.removeFromLeft (tw).reduced (3, 2));
        r.removeFromTop (4);
        auto ctl = r.removeFromBottom (34).withTrimmedTop (4);
        if (on) on->setBounds (ctl.removeFromLeft (92).reduced (2, 1));
        ctl.removeFromLeft (6); if (mode) mode->setBounds (ctl.removeFromLeft (130).reduced (2, 2));
        ctl.removeFromLeft (6); if (stepsC) stepsC->setBounds (ctl.removeFromLeft (64).reduced (2, 1));
        ctl.removeFromLeft (6); lenCap = ctl.removeFromLeft (50);
        if (len) len->setBounds (ctl.removeFromLeft (96).reduced (2, 1));
        ctl.removeFromLeft (6); feelCap = ctl.removeFromLeft (34);
        if (feel) feel->setBounds (ctl.removeFromLeft (96).reduced (2, 1));
        target = ctl.withTrimmedLeft (10);
        for (auto* e : editors) e->setBounds (r);
        for (auto* e : stepEds) e->setBounds (r);
        dna.setBounds (right.removeFromTop (30).reduced (0, 2)); right.removeFromTop (4);
        auto rr = right.removeFromTop (30); save.setBounds (rr.removeFromLeft (rr.getWidth() * 3 / 5).reduced (0, 2)); clearAll.setBounds (rr.withTrimmedLeft (6).reduced (0, 2));
        right.removeFromTop (4); resetT.setBounds (right.removeFromTop (26).reduced (0, 1));
        right.removeFromTop (6);
        auto knobs = right.removeFromBottom (100);
        if (depth) depth->setBounds (knobs.removeFromLeft (knobs.getWidth() / 2).withSizeKeepingCentre (96, 100));
        if (phase) phase->setBounds (knobs.withSizeKeepingCentre (96, 100));
        timelineB.setBounds (right.removeFromTop (30).reduced (0, 2)); right.removeFromTop (4);   // (GHOST MOTION timeline: the overview gave up its top 34 px)
        overview = right.withTrimmedBottom (6);
    }
    void paint (juce::Graphics& g) override
    {
        const bool units = rack.box.laneTarget[(size_t) sel].load() != 0;
        if (timeline == nullptr)
        {
            drawCaption (g, lenCap.toFloat(), "LENGTH", col::dim.brighter (0.25f), 10);
            drawCaption (g, feelCap.toFloat(), "FEEL", col::dim.brighter (0.25f), 10);
            drawCaption (g, target.toFloat(), juce::String::fromUTF8 ("\xe2\x86\x92 ") + laneTargetText (sel, units), laneColour (sel), 10.5f);
        }
        // all five lanes at a glance
        auto o = overview.toFloat(); drawScreen (g, o, kMotionC);
        drawCaption (g, o.reduced (10, 4).removeFromTop (14), rack.box.motionName.isNotEmpty() ? "MOTION: " + rack.box.motionName : "MOTION: your own", col::white.withAlpha (0.85f), 10.5f);
        const float h = (o.getHeight() - 26) / kLanes;
        for (int k = 0; k < kLanes; ++k)
        {
            auto lr = juce::Rectangle<float> (o.getX() + 8, o.getY() + 22 + k * h, o.getWidth() - 16, h - 4);
            const bool lon = getParam (apvts, lp (k, "on")) > 0.5f;
            const auto c = laneColour (k).withAlpha (lon ? 1.0f : 0.3f);
            drawCaption (g, lr.withWidth (60), laneName (k), c, 9);
            auto cv = lr.withTrimmedLeft (62);
            g.setColour (col::white.withAlpha (0.06f)); g.fillRoundedRectangle (cv, 3);
            const auto sh = rack.box.lanes[(size_t) k].get(); const float dpt = getParam (apvts, lp (k, "depth"));
            const bool stepsMode = getParam (apvts, lp (k, "mode")) > 0.5f; const auto st = rack.box.getSteps (k);
            juce::Path p;
            for (int i = 0; i <= 60; ++i) { const double ph = i / 60.0 * 0.9999; const float x = cv.getX() + cv.getWidth() * i / 60.0f, y = cv.getCentreY() - (stepsMode ? st.valueAt (ph) : sh.valueAt (ph)) * dpt * cv.getHeight() * 0.45f; if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y); }
            g.setColour (c); g.strokePath (p, juce::PathStrokeType (1.2f));
            const float pp = rack.box.lanePos[(size_t) k].load();
            if (lon && pp >= 0) { g.setColour (col::white.withAlpha (0.8f)); g.fillRect (cv.getX() + cv.getWidth() * pp, cv.getY(), 1.5f, cv.getHeight()); }
        }
    }
    void tick (double) override { repaint (overview); repaint (target); }
private:
    static juce::String lp (int k, const char* s) { return "box_mo_" + juce::String (laneKey (k)) + "_" + s; }
    static juce::Colour laneColour (int k) { static const juce::uint32 c[] { 0xff3CFF8F, 0xffA06BFF, 0xffC6FF4F, 0xff45E6FF, 0xffFF6A3D }; return T (juce::Colour (c[juce::jlimit (0, 4, k)])); }
    void dnaMenu()
    {
        juce::PopupMenu m; m.addSectionHeader ("FACTORY MOTIONS");
        const auto& f = factoryMotions();
        for (int i = 0; i < (int) f.size(); ++i) m.addItem (100 + i, f[(size_t) i].name);
        files = listXml (motionDir());
        if (! files.isEmpty()) { m.addSectionHeader ("YOUR MOTION DNA"); for (int i = 0; i < files.size(); ++i) m.addItem (1000 + i, files[i].getFileNameWithoutExtension()); }
        juce::Component::SafePointer<MotionPage> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&dna), [safe] (int r)
        {
            if (safe == nullptr || r <= 0) return;
            auto& me = *safe; const auto& f2 = factoryMotions(); MotionDef d;
            if (r >= 100 && r < 100 + (int) f2.size()) d = f2[(size_t) (r - 100)];
            else if (r >= 1000 && r - 1000 < me.files.size()) { if (! loadMotionFile (me.files[r - 1000], d)) return; }
            else return;
            me.rack.pushUndo(); me.rack.box.applyMotion (d); me.show (me.sel);
        });
    }
    juce::AudioProcessorValueTreeState& apvts; Rack::Impl& rack;
    juce::OwnedArray<bendlane::Editor> editors; juce::OwnedArray<StepEditor> stepEds; juce::OwnedArray<juce::TextButton> tabs;
    std::unique_ptr<Toggle> on; std::unique_ptr<Knob> depth, phase; std::unique_ptr<Choice> len, feel, stepsC; std::unique_ptr<Segmented> mode; Toggle resetT;
    juce::TextButton dna, save, clearAll; juce::Array<juce::File> files;
    juce::Rectangle<int> lenCap, feelCap, target, overview; int sel = 0;
    juce::TextButton timelineB; std::unique_ptr<TimelineView> timeline;   // GHOST MOTION timeline
};

// ---------------------------------------------------------------- CHAIN BRAIN
// The audition (source / goal / seed / which chain / loudness match) lives in Rack::Impl: leaving this tab keeps it running,
// only APPLY, ORIGINAL or UNDO end it.
class BrainPage : public Page
{
public:
    BrainPage (juce::AudioProcessorValueTreeState& s, Rack::Impl& r) : apvts (s), rack (r), source (s, "", kBrainC), goal (s, "", kBrainC)
    {
        source.items = brain::sourceNames(); goal.items = brain::goalNames(); goal.columns = 4;
        source.value = rack.brainSource; goal.value = rack.brainGoal;
        source.onChange = [this] (int v) { rack.brainSource = v; generate(); }; goal.onChange = [this] (int v) { rack.brainGoal = v; generate(); };
        for (auto* c : std::initializer_list<juce::Component*> { &source, &goal, &newSeed, &original, &apply, &match, &seedBox }) addAndMakeVisible (c);
        newSeed.setButtonText ("NEW IDEAS"); original.setButtonText ("ORIGINAL"); apply.setButtonText ("APPLY"); match.setButtonText ("MATCH LOUDNESS");
        styleButton (newSeed, kBrainC); styleButton (apply, kBrainC); styleButton (match, kBrainC); styleButton (original, kBrainC);
        match.setClickingTogglesState (true); match.setToggleState (rack.brainMatch, juce::dontSendNotification);
        match.setTooltip ("While auditioning, every chain plays at the loudness of your dry sound (short-term LUFS) so louder never fools you");
        newSeed.setTooltip ("Three new chains for the same source and goal (type a seed number to bring a set back)");
        original.setTooltip ("Back to the rack you had before auditioning");
        apply.setTooltip ("Keep the chain you hear; its loudness match becomes make-up gain (limiter GAIN while the limiter is on, otherwise output gain)");
        seedBox.setTooltip ("SEED: the same number always gives the same three chains. Type one and press enter");
        seedBox.setInputRestrictions (6, "0123456789"); seedBox.setJustification (juce::Justification::centred);
        seedBox.setColour (juce::TextEditor::outlineColourId, kBrainC.withAlpha (0.5f)); seedBox.setColour (juce::TextEditor::focusedOutlineColourId, kBrainC);
        seedBox.setText (juce::String (rack.brainSeed), juce::dontSendNotification);
        seedBox.onReturnKey = [this] { rack.brainSeed = (uint32_t) seedBox.getText().getIntValue(); generate(); };
        seedBox.onFocusLost = [this] { if ((uint32_t) seedBox.getText().getIntValue() != rack.brainSeed) { rack.brainSeed = (uint32_t) seedBox.getText().getIntValue(); generate(); } };
        newSeed.onClick = [this] { rack.brainSeed = hash32 (rack.brainSeed + 0x9e3779b9u) % 100000u; seedBox.setText (juce::String (rack.brainSeed), juce::dontSendNotification); generate(); };
        original.onClick = [this] { if (rack.brainAudition >= 0) { brain::endAudition (rack, false); rack.undo(); } refresh(); };
        apply.onClick = [this] { brain::endAudition (rack, true); refresh(); };
        match.onClick = [this] { rack.brainMatch = match.getToggleState(); if (rack.brainAudition >= 0) rack.box.matchOn = rack.brainMatch; };
        for (int k = 0; k < 3; ++k)
        {
            auto* bt = aud.add (new juce::TextButton (juce::String ("AUDITION ") + (char) ('A' + k)));
            styleButton (*bt, kBrainC); bt->setClickingTogglesState (false);
            bt->onClick = [this, k] { audition (k); };
            addAndMakeVisible (bt);
        }
        chains = brain::generate (rack.brainSource, rack.brainGoal, rack.brainSeed);
        refresh();
    }
    void generate()
    {
        chains = brain::generate (rack.brainSource, rack.brainGoal, rack.brainSeed);
        if (rack.brainAudition >= 0) audition (rack.brainAudition);   // the auditioned slot follows the new set
        refresh();
    }
    void audition (int k) { brain::audition (rack, k); refresh(); }
    void refresh()
    {
        for (int i = 0; i < 3; ++i) aud[i]->setToggleState (i == rack.brainAudition, juce::dontSendNotification);
        apply.setEnabled (rack.brainAudition >= 0); original.setEnabled (rack.brainAudition >= 0);
        repaint();
    }
    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop (108);
        srcCap = top.removeFromTop (14); source.setBounds (top.removeFromTop (32));
        top.removeFromTop (4); goalCap = top.removeFromTop (14); goal.setBounds (top);
        r.removeFromTop (8);
        auto bottom = r.removeFromBottom (32);
        newSeed.setBounds (bottom.removeFromLeft (120).reduced (2, 2)); seedCap = bottom.removeFromLeft (44); seedBox.setBounds (bottom.removeFromLeft (70).reduced (0, 4));
        apply.setBounds (bottom.removeFromRight (110).reduced (2, 2)); original.setBounds (bottom.removeFromRight (110).reduced (2, 2));
        match.setBounds (bottom.removeFromRight (150).reduced (2, 2)); meter = bottom.reduced (10, 0);
        r.removeFromBottom (6);
        const int w = r.getWidth() / 3;
        for (int k = 0; k < 3; ++k)
        {
            auto c = r.removeFromLeft (w).reduced (5, 0); cards[(size_t) k] = c;
            aud[k]->setBounds (c.removeFromBottom (30).reduced (10, 2));
        }
    }
    // a setting as the user reads it: the parameter's own text, caption without the unit title
    static juce::String settingText (juce::AudioProcessorValueTreeState& s, int u, const juce::String& key, float v)
    {
        auto* p = s.getParameter (pid (u, key.toRawUTF8())); if (p == nullptr) return {};
        auto name = p->getName (40).fromLastOccurrenceOf (unitInfo (u).title, false, false).trim().toLowerCase();
        return name + " " + p->getText (p->convertTo0to1 (v), 16);
    }
    void paint (juce::Graphics& g) override
    {
        drawCaption (g, srcCap.toFloat(), "SOURCE  -  what goes into the box", col::dim.brighter (0.3f), 10);
        drawCaption (g, goalCap.toFloat(), "GOAL  -  what you want it to become", col::dim.brighter (0.3f), 10);
        drawCaption (g, seedCap.toFloat().withTrimmedLeft (8), "SEED", col::dim.brighter (0.3f), 11);
        const auto& bx = rack.box;
        for (int k = 0; k < 3; ++k)
        {
            auto c = cards[(size_t) k].toFloat(); const bool on = rack.brainAudition == k;
            g.setColour (kBrainC.withAlpha (on ? 0.14f : 0.05f)); g.fillRoundedRectangle (c, 10);
            g.setColour (kBrainC.withAlpha (on ? 0.9f : 0.3f)); g.drawRoundedRectangle (c.reduced (0.5f), 10, on ? 1.6f : 1.0f);
            const auto& ch = chains[(size_t) k];
            auto in = c.reduced (12, 8); in.removeFromBottom (34);   // the AUDITION button sits there
            drawCaption (g, in.removeFromTop (26), ch.name + "   " + ch.recipe, col::white, 16);
            in.removeFromTop (2);
            // the chain as flowing tiles
            auto tilesR = in.removeFromTop (44); const int n = (int) ch.units.size();
            const float tw = juce::jmin (90.0f, (tilesR.getWidth() - (n - 1) * 14.0f) / juce::jmax (1, n));
            for (int i = 0; i < n; ++i)
            {
                auto t = juce::Rectangle<float> (tilesR.getX() + i * (tw + 14), tilesR.getY(), tw, 40);
                const auto acc = accentOf (ch.units[(size_t) i].unit);
                g.setColour (acc.withAlpha (0.15f)); g.fillRoundedRectangle (t, 7); g.setColour (acc.withAlpha (0.7f)); g.drawRoundedRectangle (t, 7, 1.0f);
                drawCaption (g, t, unitInfo (ch.units[(size_t) i].unit).shortName, col::white, 11, juce::Justification::centred);
                drawCaption (g, t.translated (0, 27).withHeight (12), juce::String (juce::roundToInt (ch.units[(size_t) i].mix * 100)) + "% mix", col::dim, 8.5f, juce::Justification::centred);
                if (i + 1 < n) { juce::Path a; const float x = t.getRight() + 5, y = t.getCentreY(); a.startNewSubPath (x, y - 4); a.lineTo (x + 4, y); a.lineTo (x, y + 4); g.setColour (col::white.withAlpha (0.7f)); g.strokePath (a, juce::PathStrokeType (1.4f)); }
            }
            in.removeFromTop (8);
            // why this chain
            g.setColour (kBrainC.withAlpha (0.9f)); g.setFont (font (10.5f));
            g.drawFittedText (ch.why, in.removeFromTop (30).toNearestInt(), juce::Justification::topLeft, 2, 0.9f);
            in.removeFromTop (6);
            // the settings, as the parameters display them (units without settings just show their mix above)
            juce::StringArray lines;
            for (auto& u : ch.units)
            {
                juce::StringArray vs;
                for (auto& [key, v] : u.vals) { if (vs.size() >= 3) break; const auto t = settingText (apvts, u.unit, key, v); if (t.isNotEmpty()) vs.add (t); }
                if (! vs.isEmpty()) lines.add (juce::String (unitInfo (u.unit).shortName) + ":  " + vs.joinIntoString (",  "));
            }
            g.setColour (col::dim.brighter (0.2f)); g.setFont (font (9.5f));
            g.drawFittedText (lines.joinIntoString ("\n"), in.removeFromTop (juce::jmin (62.0f, in.getHeight() - 60)).toNearestInt(), juce::Justification::topLeft, 5, 0.9f);
            // live level for the chain you hear: dry vs chain LUFS and the trim the match applies
            if (on && bx.matchOn.load())
            {
                auto mr = in.removeFromBottom (44).reduced (0, 4);
                const float li = bx.lufsIn.load(), lo = bx.lufsOut.load(), tr = bx.trimDb.load();
                auto bar = [&g, &mr] (float lufs, const juce::String& label, juce::Colour col_, float y)
                {
                    auto row = juce::Rectangle<float> (mr.getX(), y, mr.getWidth(), 14);
                    drawCaption (g, row.removeFromLeft (46), label, col_, 9);
                    g.setColour (col::white.withAlpha (0.08f)); g.fillRoundedRectangle (row.reduced (0, 4), 2);
                    const float f = clampf ((lufs + 40.0f) / 40.0f, 0.0f, 1.0f);
                    g.setColour (col_.withAlpha (0.85f)); g.fillRoundedRectangle (row.reduced (0, 4).withWidth (row.getWidth() * f), 2);
                    drawCaption (g, row, juce::String (lufs, 1) + " LUFS", col::white, 8.5f, juce::Justification::centredRight);
                };
                bar (li, "DRY", col::ice, mr.getY()); bar (lo, "CHAIN", kBrainC, mr.getY() + 16);
                drawCaption (g, { mr.getX(), mr.getY() + 32, mr.getWidth(), 12 }, juce::String::formatted ("match trim %+.1f dB", tr), col::dim, 8.5f);
            }
        }
        juce::String mt = rack.brainAudition >= 0 ? "auditioning " + chains[(size_t) rack.brainAudition].name + "  (stays on while you look at other pages; APPLY keeps it, ORIGINAL / UNDO drop it)" : "pick AUDITION A / B / C";
        drawCaption (g, meter.toFloat(), mt, kBrainC.withAlpha (0.9f), 10.5f);
    }
    void tick (double) override { repaint (meter); for (int k = 0; k < 3; ++k) if (rack.brainAudition == k) repaint (cards[(size_t) k]); }
    std::array<brain::Chain, 3> chains;
private:
    juce::AudioProcessorValueTreeState& apvts; Rack::Impl& rack;
    Segmented source, goal; juce::TextButton newSeed, original, apply, match; juce::TextEditor seedBox; juce::OwnedArray<juce::TextButton> aud;
    std::array<juce::Rectangle<int>, 3> cards; juce::Rectangle<int> srcCap, goalCap, seedCap, meter;
};

// ---------------------------------------------------------------- PERFORM: big macro knobs + MORPH + lane switches
class PerformPage : public Page
{
public:
    PerformPage (juce::AudioProcessorValueTreeState& s, Rack::Impl& r) : rack (r), morph (s, "box_morph", "MORPH", kMorphB)
    {
        for (int k = 0; k < kMacros; ++k)
        {
            auto* kn = knobs.add (new Knob (s, "box_macro" + juce::String (k + 1), rack.box.macroNames[(size_t) k], macroColour (k)));
            kn->getProperties().set ("gpfxMacro", k); kn->slider.getProperties().set ("gpfxMacro", k);
            kn->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 16);
            addAndMakeVisible (kn);
        }
        morph.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 16);
        addAndMakeVisible (morph);
        for (int k = 0; k < kLanes; ++k) addAndMakeVisible (lanes.add (new Toggle (s, "box_mo_" + juce::String (laneKey (k)) + "_on", laneName (k), kMotionC)));
    }
    static juce::Colour macroColour (int k) { static const juce::uint32 c[] { 0xff9C8BFF, 0xffFF4FD8, 0xff45E6FF, 0xffFFC35A, 0xff3CFF8F, 0xff7FD8FF, 0xffFF6A3D, 0xffE6B8FF }; return T (juce::Colour (c[k & 7])); }
    void resized() override
    {
        auto r = getLocalBounds();
        auto laneRow = r.removeFromBottom (34);
        laneCap = laneRow.removeFromLeft (110);
        const int lw = laneRow.getWidth() / kLanes; for (auto* t : lanes) t->setBounds (laneRow.removeFromLeft (lw).reduced (4, 2));
        auto right = r.removeFromRight (juce::jmin (260, r.getWidth() / 4));
        morph.setBounds (right.reduced (10, 10));
        const int cw = r.getWidth() / 4, ch = r.getHeight() / 2;
        for (int k = 0; k < kMacros; ++k) knobs[k]->setBounds (juce::Rectangle<int> (r.getX() + (k % 4) * cw, r.getY() + (k / 4) * ch, cw, ch).reduced (8, 4));
    }
    void paint (juce::Graphics& g) override { drawCaption (g, laneCap.toFloat(), "MOTION LANES", kMotionC, 11); }
    void tick (double) override { for (int k = 0; k < kMacros; ++k) if (knobs[k]->caption != rack.box.macroNames[(size_t) k]) { knobs[k]->caption = rack.box.macroNames[(size_t) k]; knobs[k]->repaint(); } }
private:
    Rack::Impl& rack; juce::OwnedArray<Knob> knobs; Knob morph; juce::OwnedArray<Toggle> lanes; juce::Rectangle<int> laneCap;
};

// ---------------------------------------------------------------- SPIRIT BOARD: blend up to 12 scenes by moving the puck between ghosts
// SPIRIT BOARD diary: a few breadcrumbs for the first BOARD open of a session + every failure, flushed line by line, so a crash in a
// host leaves its last step on disk. Mac: ~/Library/Logs/Ghost Box/ghostbox-board.log; elsewhere <app data>/Ghost Box/ghostbox-board.log
namespace boardlog
{
inline juce::File file()
{
#if JUCE_MAC
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Logs/Ghost Box/ghostbox-board.log");
#else
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Ghost Box/ghostbox-board.log");
#endif
}
inline void write (const juce::String& s) noexcept
{
    try
    {
        auto f = file(); f.getParentDirectory().createDirectory();
        if (f.getSize() > 512 * 1024) f.deleteFile();
        juce::FileOutputStream o (f);   // (appends)
        if (! o.openedOk()) return;
#ifdef JucePlugin_VersionString
        const char* ver = JucePlugin_VersionString;
#else
        const char* ver = "dev";
#endif
        o << juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S") << "  v" << ver << " (" << __DATE__ << ")  " << s << "\n";
        o.flush();
    }
    catch (...) {}
}
inline void crumb (const juce::String& s) noexcept
{   // only the first 40 per session (the first BOARD open)
    static std::atomic<int> left { 40 };
    if (left.fetch_sub (1) > 0) write ("board: " + s);
}
inline juce::String context (Rack::Impl& r, juce::Rectangle<int> bounds)
{
    juce::String s;
    s << "bounds " << bounds.toString() << "  scale " << juce::Desktop::getInstance().getGlobalScaleFactor();
    if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()) s << "  display scale " << d->scale;
    s << "  ghosts " << (int) r.box.board.ghosts.size() << "  board on " << (int) r.box.isBoardOn();
    if (r.layerStatus) s << "  layer '" << r.layerStatus() << "'";
    return s;
}
} // namespace boardlog

// ---------------------------------------------------------------- DRAWN ORBIT: the SPEED curve (5 points; a pop-down from the PATH strip)
// x = time through the loop, y = how far along the path: steep = the puck moves fast there, flat = slow. The ends stay at 0 and 1.
class SpeedCurveEditor : public juce::Component
{
public:
    std::array<float, 5> v { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
    std::function<void()> onEditStart;
    std::function<void (const std::array<float, 5>&)> onChange;
    explicit SpeedCurveEditor (fxtheme::C accent) : acc (accent)
    {
        resetB.setButtonText ("STRAIGHT"); resetB.setTooltip ("Even speed (the timing as drawn / recorded)");
        styleButton (resetB, acc); addAndMakeVisible (resetB);
        resetB.onClick = [this] { if (onEditStart) onEditStart(); v = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }; if (onChange) onChange (v); repaint(); };
        setSize (280, 190);
        getProperties().set ("gbSpeedCurve", true);
    }
    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (14.0f, 10.0f).withTrimmedTop (16.0f).withTrimmedBottom (32.0f); }
    juce::Point<float> knot (int k) const { const auto r = plot(); return { r.getX() + r.getWidth() * (float) k / 4.0f, r.getBottom() - r.getHeight() * v[(size_t) k] }; }
    void resized() override { resetB.setBounds (getLocalBounds().removeFromBottom (30).reduced (14, 3).removeFromRight (96)); }
    void paint (juce::Graphics& g) override
    {
        const auto& P = fxtheme::pal();
        g.fillAll (P.panel);
        const auto r = plot();
        drawCaption (g, { 14.0f, 6.0f, (float) getWidth() - 28.0f, 14.0f }, "SPEED: steep = fast, flat = slow (drag the 3 points)", col::white, 10.0f);
        g.setColour (P.waveBg); g.fillRoundedRectangle (r, 6.0f);
        g.setColour (P.line); g.drawRoundedRectangle (r, 6.0f, 1.0f);
        g.setColour (P.overlay.withAlpha (P.isLight ? 0.12f : 0.08f));
        for (int k = 1; k < 4; ++k) { const float x = r.getX() + r.getWidth() * (float) k / 4.0f, y = r.getY() + r.getHeight() * (float) k / 4.0f; g.drawVerticalLine ((int) x, r.getY(), r.getBottom()); g.drawHorizontalLine ((int) y, r.getX(), r.getRight()); }
        g.setColour (col::dim.withAlpha (0.6f)); g.drawLine ({ r.getBottomLeft(), r.getTopRight() }, 1.0f);
        ghosty::spirit::OrbitPath tmp; tmp.setSpeed (v);
        juce::Path c; for (int i = 0; i <= 80; ++i) { const float u = (float) i / 80.0f; const juce::Point<float> q (r.getX() + r.getWidth() * u, r.getBottom() - r.getHeight() * tmp.speedMap (u)); if (i == 0) c.startNewSubPath (q); else c.lineTo (q); }
        const auto ac = ghosty::themes::readableOn (acc.get(), P.waveBg, 3.0);
        g.setColour (ac.withAlpha (0.25f)); g.strokePath (c, juce::PathStrokeType (5.0f));
        g.setColour (ac); g.strokePath (c, juce::PathStrokeType (2.0f));
        for (int k = 0; k < 5; ++k) { const auto q = knot (k); const bool inner = k > 0 && k < 4; g.setColour (inner ? ac : col::dim.get()); g.fillEllipse (q.x - 5, q.y - 5, 10, 10); if (inner) { g.setColour (P.onAccent); g.drawEllipse (q.x - 5, q.y - 5, 10, 10, 1.2f); } }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        drag = -1; float best = 16.0f;
        for (int k = 1; k < 4; ++k) { const float d = knot (k).getDistanceFrom (e.position); if (d < best) { best = d; drag = k; } }
        if (drag < 0) { const auto r = plot(); const int k = juce::jlimit (1, 3, juce::roundToInt ((e.position.x - r.getX()) / r.getWidth() * 4.0f)); drag = k; }
        if (onEditStart) onEditStart();
        mouseDrag (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drag < 1 || drag > 3) return;
        const auto r = plot();
        auto nv = v; nv[(size_t) drag] = juce::jlimit (0.0f, 1.0f, (r.getBottom() - e.position.y) / juce::jmax (1.0f, r.getHeight()));
        ghosty::spirit::OrbitPath tmp; tmp.setSpeed (nv);
        for (int k = drag + 1; k < 4; ++k) tmp.speed[(size_t) k] = juce::jmax (tmp.speed[(size_t) k], nv[(size_t) drag]);   // pushing up carries the later points along
        tmp.setSpeed (tmp.speed);
        v = tmp.speed; if (onChange) onChange (v); repaint();
    }
    void mouseUp (const juce::MouseEvent&) override { drag = -1; }
    juce::TextButton resetB;
private:
    fxtheme::C acc; int drag = -1;
};

// ---------------------------------------------------------------- DRAWN ORBIT: the PATH strip (a pop-down over the bottom of the board)
// row 1: DRAW, REC, LOOP length, SPEED..., PHASE, status   row 2: SMOOTH, REVERSE, RESAMPLE..., CLEAR, SIZE
class PathStrip : public juce::Component
{
public:
    explicit PathStrip (fxtheme::C accent) : acc (accent)
    {
        getProperties().set ("gbPathUi", true);
        for (auto* b : { &drawB, &recB, &speedB, &smoothB, &revB, &resB, &clearB }) { addAndMakeVisible (b); styleButton (*b, acc); }
        drawB.setButtonText ("DRAW"); drawB.setClickingTogglesState (true);
        drawB.setTooltip ("DRAW: drag a loop on the board. Draw slowly where the puck should linger, quickly where it should rush; it plays back over the LOOP length, closed back to the start");
        recB.setButtonText ("REC"); recB.setClickingTogglesState (true);
        recB.setTooltip ("REC: while the song plays, move the puck by hand (drag anywhere on the board): one LOOP length of movement is recorded with its timing, then it plays back. Click again to stop early");
        speedB.setButtonText ("SPEED..."); speedB.setTooltip ("SPEED: a curve that speeds up / slows down the path through the loop");
        smoothB.setButtonText ("SMOOTH"); smoothB.setTooltip ("Round off the corners and the jitter of the path (the timing stays)");
        revB.setButtonText ("REVERSE"); revB.setClickingTogglesState (true); revB.setTooltip ("Play the path backwards");
        resB.setButtonText ("RESAMPLE..."); resB.setTooltip ("Rebuild the path from a number of points, evenly spaced in time (fewer = simpler, smoother)");
        clearB.setButtonText ("CLEAR"); clearB.setTooltip ("Clear this path (UNDO brings it back)");
        addAndMakeVisible (loopBox); loopBox.addItemList ({ "1 BAR", "2 BARS", "4 BARS", "8 BARS" }, 1);
        loopBox.setTooltip ("LOOP: how long one pass of the path takes, in bars (synced to the song)");
        loopBox.setColour (juce::ComboBox::outlineColourId, acc); loopBox.setColour (juce::ComboBox::arrowColourId, acc);
        for (auto* sl : { &phaseS, &sizeS })
        {
            addAndMakeVisible (sl); sl->setSliderStyle (juce::Slider::LinearHorizontal); sl->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            sl->setColour (juce::Slider::rotarySliderFillColourId, acc); sl->setColour (juce::Slider::thumbColourId, acc);
        }
        phaseS.setRange (0.0, 1.0); phaseS.setDoubleClickReturnValue (true, 0.0); phaseS.setTooltip ("PHASE: where in the loop the path starts (slide the whole movement earlier / later)");
        sizeS.setRange (0.1, 2.0); sizeS.setSkewFactorFromMidPoint (1.0); sizeS.setDoubleClickReturnValue (true, 1.0); sizeS.setTooltip ("SIZE: shrink / grow the path around its middle");
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (6, 5);
        auto r1 = r.removeFromTop (26); r.removeFromTop (5); auto r2 = r.removeFromTop (26);
        auto take = [] (juce::Rectangle<int>& row, int w) { auto x = row.removeFromLeft (w); row.removeFromLeft (5); return x; };
        drawB.setBounds (take (r1, 62)); recB.setBounds (take (r1, 62)); loopBox.setBounds (take (r1, 96)); speedB.setBounds (take (r1, 84));
        phaseCap = take (r1, 44); phaseS.setBounds (take (r1, juce::jmin (140, juce::jmax (60, r1.getWidth() - 60)))); statusR = r1;
        smoothB.setBounds (take (r2, 70)); revB.setBounds (take (r2, 80)); resB.setBounds (take (r2, 96)); clearB.setBounds (take (r2, 62));
        sizeCap = take (r2, 44); sizeS.setBounds (take (r2, juce::jmin (140, juce::jmax (60, r2.getWidth() - 60)))); status2R = r2;
    }
    void paint (juce::Graphics& g) override
    {
        const auto& P = fxtheme::pal();
        const auto b = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (P.panel.withAlpha (0.97f)); g.fillRoundedRectangle (b, 8.0f);
        g.setColour (ghosty::themes::readableOn (acc.get(), P.panel, 3.0).withAlpha (0.8f)); g.drawRoundedRectangle (b, 8.0f, 1.2f);
        drawCaption (g, phaseCap.toFloat(), "PHASE", col::dim, 9.5f); drawCaption (g, sizeCap.toFloat(), "SIZE", col::dim, 9.5f);
        drawCaption (g, statusR.toFloat(), status, status.startsWith ("REC") ? col::pink.get() : col::white.get(), 10.0f);
        drawCaption (g, status2R.toFloat(), status2, col::dim, 9.5f);
    }
    void setStatus (const juce::String& a, const juce::String& b) { if (a != status || b != status2) { status = a; status2 = b; repaint(); } }
    juce::TextButton drawB, recB, speedB, smoothB, revB, resB, clearB;
    juce::ComboBox loopBox;
    FineSlider phaseS { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox }, sizeS { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Rectangle<int> phaseCap, sizeCap, statusR, status2R;
    juce::String status, status2;
private:
    fxtheme::C acc;
};

class BoardPage : public Page, private juce::Timer, private juce::AsyncUpdater, private juce::AudioProcessorValueTreeState::Listener
{
public:
    BoardPage (juce::AudioProcessorValueTreeState& s, Rack::Impl& r)
        : apvts (s), rack (r), view (r.box.board), orbit (s, "box_orbit", kBoardC), rate (s, "box_orbit_rate", kBoardC), depth (s, "box_orbit_depth", "ORBIT DEPTH", kBoardC),
          blendC (s, "box_board_blend", kBoardC), lawC (s, "box_board_law", kBoardC), srcC (s, "box_board_wsrc", kBoardC), kC (s, "box_board_k", kBoardC)
    {
        boardlog::crumb ("ctor start");
        view.trail = [] (const char* w) { boardlog::crumb (w); };
        view.onPaintError = [this] (const juce::String& e) { failed ("board view paint", e); };
        // LAYER mode strip (above the board): SMOOTH blends the ghosts' SETTINGS in one rack; LAYER runs every weighted ghost's own rack at once
        layerStrip = s.getParameter ("box_board_blend") != nullptr;
        for (auto* c : { &blendC, &lawC, &srcC, &kC }) { addChildComponent (c); c->setVisible (layerStrip); }
        blendC.setTooltip ("BLEND: SMOOTH = one rack glides between the ghosts' settings (as before). LAYER = every ghost with weight runs its OWN rack on the same sound at the same time, mixed at its weight: different effects play together");
        lawC.setTooltip ("WEIGHT LAW (LAYER): POWER keeps the loudness even while you move (equal power), LINEAR mixes the outputs at exactly the weights");
        srcC.setTooltip ("WEIGHTS (LAYER): DISTANCE = by the puck, EVEN = all ghosts equal, CUSTOM = your own % per ghost (right-click a ghost: Set weight %...)");
        kC.setTooltip ("LAYERS: how many ghosts' racks may run at once (the strongest ones; more = more CPU)");
        view.onSetPercent = [this] (int g) { askPercent (g); };
        addAndMakeVisible (view);
        view.palette = [] () -> const ghosty::themes::Palette& { return fxtheme::pal(); };
        view.onAddGhost = [this] (float x, float y) { if (rack.boardAdd (x, y) >= 0 && rack.box.board.ghosts.size() >= 2 && ! rack.box.isBoardOn()) rack.boardEnable (true); changed(); };
        view.requestScene = [this] (int g) { setFromMenu (g); };
        view.onRemoveGhost = [this] (int g) { rack.boardRemove (g); changed(); };
        view.onGhostMoved = [this] (int, float, float) { rack.boardEdited(); };
        view.onRenamed = [this] (int) { rack.boardEdited(); };
        view.onEditStart = [this] { rack.pushUndo(); };
        view.onPuckMoved = [this] (float x, float y) { setParam (apvts, "box_board_x", x); setParam (apvts, "box_board_y", y); };
        view.livePuck = [this] { return livePuckNow(); };
        view.liveWeights = [this] { std::vector<float> w; if (! rack.box.isBoardOn() && ! (rack.layerRunning && rack.layerRunning())) return w; const auto a = rack.box.boardWeightsNow(); w.assign (a.begin(), a.begin() + (long) rack.box.board.ghosts.size()); return w; };
        for (auto* b : { &onBtn, &snapB, &randB, &circB, &clearB, &saveB, &loadB }) { addAndMakeVisible (b); styleButton (*b, kBoardC); }
        onBtn.setButtonText ("BOARD ON"); onBtn.setClickingTogglesState (true);
        onBtn.setTooltip ("The board drives the rack: the sound is the blend of every ghost's scene, weighted by how close each ghost is to the glowing puck (MORPH lets go)");
        onBtn.onClick = [this] { rack.boardEnable (onBtn.getToggleState()); changed(); };
        snapB.setButtonText ("SNAP"); snapB.setClickingTogglesState (true); snapB.setTooltip ("Let go of the puck and it jumps onto the nearest ghost");
        snapB.onClick = [this] { rack.pushUndo(); rack.box.board.snapToGhost = snapB.getToggleState(); rack.boardEdited(); };
        randB.setButtonText ("RANDOM"); randB.setTooltip ("Throw the ghosts to random places on the board");
        randB.onClick = [this] { rack.pushUndo(); rack.box.board.randomise ((uint32_t) juce::Time::getMillisecondCounter()); rack.boardEdited(); changed(); };
        circB.setButtonText ("CIRCLE"); circB.setTooltip ("Arrange the ghosts in a circle (the puck in the middle hears all of them)");
        circB.onClick = [this] { rack.pushUndo(); rack.box.board.arrangeInCircle(); rack.boardEdited(); changed(); };
        clearB.setButtonText ("CLEAR"); clearB.setTooltip ("Remove every ghost (UNDO brings them back)");
        clearB.onClick = [this] { rack.pushUndo(); rack.box.board.ghosts.clear(); rack.box.boardSnaps.clear(); ++rack.box.boardContentVersion; rack.box.setBoardOn (false); rack.boardEdited(); changed(); };
        saveB.setButtonText ("SAVE BOARD..."); saveB.setTooltip ("Save this board (ghosts, their scenes, positions, settings) as a file; it shows up in BROWSE");
        saveB.onClick = [this] { askName(); };
        loadB.setButtonText ("LOAD BOARD..."); loadB.setTooltip ("Load a saved board");
        loadB.onClick = [this] { loadMenu(); };
        addAndMakeVisible (modeBox); modeBox.addItemList ({ "DISTANCE", "SOFT (GAUSSIAN)", "NEAREST 3" }, 1);
        modeBox.setTooltip ("How the ghosts share the sound: DISTANCE = each by its closeness, SOFT = smooth wide blends, NEAREST 3 = only the 3 closest ghosts count");
        modeBox.onChange = [this] { rack.pushUndo(); rack.box.board.weights.mode = (ghosty::spirit::WeightMode) (modeBox.getSelectedItemIndex()); rack.boardEdited(); };
        addAndMakeVisible (sharp); sharp.setRange (0.0, 1.0); sharp.setSliderStyle (juce::Slider::LinearHorizontal); sharp.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        sharp.setTooltip ("SHARPNESS: low = wide, creamy blends; high = each ghost owns its own island");
        sharp.setColour (juce::Slider::rotarySliderFillColourId, kBoardC);
        sharp.onDragStart = [this] { rack.pushUndo(); };
        sharp.onValueChange = [this] { auto& w = rack.box.board.weights; const float k = (float) sharp.getValue(); w.power = 0.75f + 6.0f * k; w.radius = 0.5f - 0.44f * k; rack.boardEdited(); };
        orbit.setTooltip ("ORBIT: the puck moves by itself around where you put it (circle, figure 8 or a random walk), in time with the song; CUSTOM 1..8 = your own drawn or recorded path (PATH)");
        rate.setTooltip ("How long one orbit takes"); depth.slider.setTooltip ("How far the orbit travels");
        addAndMakeVisible (orbit); addAndMakeVisible (rate); addAndMakeVisible (depth);
        changed();
        // live: the page follows the audio's puck on its own timer, and any ORBIT / RATE / DEPTH / X / Y change (UI, automation, host)
        // shows at once. (It used to follow only the panel's tick, which runs while the host reports the window as showing: in some hosts
        // that stayed false until the window was closed and opened again, and the puck stood still.)
        for (auto* id : kLiveIds) apvts.addParameterListener (id, this);
        setupPathUi();
        startTimerHz (30);
        boardlog::crumb ("ctor end");
    }
    ~BoardPage() override
    {
        stopTimer(); cancelPendingUpdate();
        if (rack.box.board.pathHold) { rack.box.board.pathHold = false; rack.box.boardShared.publish (rack.box.board); }   // (a REC cut short by closing the page)
        orbitAtt.reset();
        for (auto* id : kLiveIds) apvts.removeParameterListener (id, this);
    }
    // where the puck is: the audio's (with the orbit) while the board is on and the audio runs; while the host has the audio
    // suspended, the same maths here (X / Y + orbit at the last host time), so a change shows even then; board off = X / Y
    juce::Point<float> livePuckNow() const
    {
        const juce::Point<float> base (getParam (apvts, "box_board_x"), getParam (apvts, "box_board_y"));
        if (! rack.box.isBoardOn()) return base;
        auto& sh = rack.box.boardShared;
        if (sh.audioFresh()) return { sh.livePx.load(), sh.livePy.load() };
        ghosty::spirit::OrbitParams op;
        op.mode = (ghosty::spirit::Orbit) juce::jlimit (0, 3, juce::roundToInt (getParam (apvts, "box_orbit"))); op.sync = true;
        op.rate = (float) (1.0 / boardOrbitBeats (juce::roundToInt (getParam (apvts, "box_orbit_rate")))); op.depth = juce::jlimit (0.0f, 1.0f, getParam (apvts, "box_orbit_depth")) * 0.5f;
        juce::Point<float> pk;
        if (op.mode == ghosty::spirit::Orbit::Off && sh.pathPuck (sh.beatsNow(), pk)) return pk;   // a DRAWN ORBIT
        return ghosty::spirit::clampToBoard (juce::Point<float> (juce::jlimit (0.0f, 1.0f, base.x), juce::jlimit (0.0f, 1.0f, base.y)) + ghosty::spirit::orbitOffset (op, sh.beatsNow()));
    }
    int liveTicks = 0;   // (tests) how often the page followed the live puck
    void changed()
    {
        onBtn.setToggleState (rack.box.isBoardOn(), juce::dontSendNotification);
        snapB.setToggleState (rack.box.board.snapToGhost, juce::dontSendNotification);
        modeBox.setSelectedItemIndex ((int) rack.box.board.weights.mode, juce::dontSendNotification);
        sharp.setValue (juce::jlimit (0.0, 1.0, (rack.box.board.weights.power - 0.75) / 6.0), juce::dontSendNotification);
        if (orbitAtt) syncPathUi();
        view.refresh();
    }
    void resized() override
    {
        boardlog::crumb ("resized " + getLocalBounds().toString());
        try { layoutPage(); }
        catch (const std::exception& e) { failed ("resized", e.what()); }
        catch (...) { failed ("resized", "unknown exception"); }
    }
    void layoutPage()
    {
        auto r = getLocalBounds().reduced (10, 6);
        auto side = r.removeFromRight (200); r.removeFromRight (10);
        if (layerStrip)
        {   // BLEND | LAW | WEIGHTS | LAYERS, then the LAYER readout
            auto st = r.removeFromTop (26); r.removeFromTop (6);
            blendC.setBounds (st.removeFromLeft (110).reduced (1, 0)); st.removeFromLeft (6);
            lawC.setBounds (st.removeFromLeft (96).reduced (1, 0)); st.removeFromLeft (6);
            srcC.setBounds (st.removeFromLeft (110).reduced (1, 0)); st.removeFromLeft (6);
            kC.setBounds (st.removeFromLeft (60).reduced (1, 0)); st.removeFromLeft (8);
            layerInfo = st;
            pathB.setBounds (layerInfo.removeFromRight (72).reduced (1, 0)); layerInfo.removeFromRight (6);   // DRAWN ORBIT: PATH (end of the strip)
        }
        view.setBounds (r);
        if (! layerStrip) pathB.setBounds (r.getRight() - 14 - 80, r.getY() + 18, 72, 24);
        {   // the PATH strip: over the bottom of the board (two rows, above the board's hint line)
            const int sh = 67;
            strip.setBounds (r.getX() + 14, juce::jmax (r.getY() + 14, r.getBottom() - 30 - sh), juce::jmax (0, r.getWidth() - 28), sh);
        }
        auto row = [&side] (int h) { auto x = side.removeFromTop (h); side.removeFromTop (6); return x; };
        onBtn.setBounds (row (30));
        modeBox.setBounds (row (26)); sharpCap = row (14); sharp.setBounds (row (20));
        orbit.setBounds (row (26)); rate.setBounds (row (26)); depth.setBounds (row (64));
        auto two = [&row] (juce::Component& a, juce::Component& b) { auto x = row (28); a.setBounds (x.removeFromLeft (x.getWidth() / 2).reduced (2, 0)); b.setBounds (x.reduced (2, 0)); };
        two (snapB, clearB); two (randB, circB); saveB.setBounds (row (28)); loadB.setBounds (row (28));
    }
    void paint (juce::Graphics& g) override
    {
        boardlog::crumb ("page paint");
        try
        {
            drawCaption (g, sharpCap.toFloat(), "SHARPNESS", col::dim, 9.5f);
            if (layerStrip)
            {
                const auto txt = rack.layerStatus ? rack.layerStatus() : juce::String();
                drawCaption (g, layerInfo.toFloat(), txt.isNotEmpty() ? txt : juce::String ("SMOOTH: one rack glides between the ghosts"), txt.isNotEmpty() ? col::white : col::dim, 10.0f);
            }
        }
        catch (const std::exception& e) { failed ("paint", e.what()); }
        catch (...) { failed ("paint", "unknown exception"); }
        if (failText.isNotEmpty())
        {   // something went wrong: say so on the page (the log file has the details) instead of taking the host down
            g.setColour (col::pink); g.setFont (font (12.0f));
            g.drawFittedText ("SPIRIT BOARD problem: " + failText + "\nDetails were saved to " + boardlog::file().getFullPathName(), getLocalBounds().reduced (12).removeFromTop (60), juce::Justification::centredLeft, 3);
        }
    }
    void tick (double) override { liveTick(); }   // (the panel's tick; the page's own timer does the same)
    void liveTick()
    {   // the puck follows automation / the orbit; the board view repaints when it moved
        ++liveTicks;
        if (! view.isMouseButtonDown()) { rack.box.board.puckX = getParam (apvts, "box_board_x"); rack.box.board.puckY = getParam (apvts, "box_board_y"); }
        if (onBtn.getToggleState() != rack.box.isBoardOn()) changed();
        if (rec.armed && rec.feed (rack.box.boardShared.beatsNow(), rack.box.boardShared.livePlaying.load(), getParam (apvts, "box_board_x"), getParam (apvts, "box_board_y"))) finishRec();
        syncPathUi();
        view.syncLive();
        if (rec.armed || drawing || customActive()) view.repaint();   // (the path, its marker, the REC trail)
        if (layerStrip) repaint (layerInfo);
    }
    // ---------------------------------------------------------------- DRAWN ORBIT (the PATH strip, CUSTOM 1..8 in ORBIT)
    static constexpr int kCustomId = 101;
    int curPath() const { return juce::jlimit (0, ghosty::spirit::kMaxPaths - 1, rack.box.board.orbit.custom); }
    ghosty::spirit::OrbitPath& path() { return rack.box.board.paths[(size_t) curPath()]; }
    bool customActive() const { return rack.box.board.orbit.mode == ghosty::spirit::Orbit::Custom && juce::roundToInt (getParam (apvts, "box_orbit")) == 0; }
    void publishPath() { rack.box.boardShared.publish (rack.box.board); view.repaint(); }
    void selectCustom (int k)
    {
        auto& b = rack.box.board;
        if (! (b.orbit.mode == ghosty::spirit::Orbit::Custom && b.orbit.custom == k)) rack.pushUndo();
        b.orbit.mode = ghosty::spirit::Orbit::Custom; b.orbit.custom = juce::jlimit (0, ghosty::spirit::kMaxPaths - 1, k);
        if (juce::roundToInt (getParam (apvts, "box_orbit")) != 0 && orbitAtt) orbitAtt->setValueAsCompleteGesture (0.0f);
        publishPath(); showStrip (true); refreshOrbitBox(); syncPathUi();
    }
    void selectBuiltIn (int m)
    {
        stopRec(); stopDraw();
        auto& b = rack.box.board;
        if (b.orbit.mode == ghosty::spirit::Orbit::Custom) { rack.pushUndo(); b.orbit.mode = ghosty::spirit::Orbit::Off; publishPath(); }
        if (orbitAtt) orbitAtt->setValueAsCompleteGesture ((float) juce::jlimit (0, 3, m));
        refreshOrbitBox(); syncPathUi();
    }
    void ensureCustom()
    {   // DRAW / REC with a built-in orbit: the first empty CUSTOM path (or the one last used)
        if (customActive()) return;
        int k = curPath();
        if (! path().empty()) for (int i = 0; i < ghosty::spirit::kMaxPaths; ++i) if (rack.box.board.paths[(size_t) i].empty()) { k = i; break; }
        selectCustom (k);
    }
    void showStrip (bool on) { pathB.setToggleState (on, juce::dontSendNotification); strip.setVisible (on); if (! on) { stopDraw(); } view.repaint(); }
    void refreshOrbitBox()
    {
        const auto& b = rack.box.board;
        for (int k = 0; k < ghosty::spirit::kMaxPaths; ++k)
        {
            const juce::String t = "CUSTOM " + juce::String (k + 1) + (b.paths[(size_t) k].empty() ? "  (empty)" : "");
            if (orbit.getItemText (orbit.indexOfItemId (kCustomId + k)) != t) orbit.changeItemText (kCustomId + k, t);
        }
        const int m = juce::jlimit (0, 3, juce::roundToInt (getParam (apvts, "box_orbit")));
        const int id = m == 0 && b.orbit.mode == ghosty::spirit::Orbit::Custom ? kCustomId + curPath() : m + 1;
        if (orbit.getSelectedId() != id) orbit.setSelectedId (id, juce::dontSendNotification);
    }
    void orbitPicked()
    {
        const int id = orbit.getSelectedId();
        if (id >= kCustomId) selectCustom (id - kCustomId);
        else if (id >= 1) selectBuiltIn (id - 1);
    }
    void startRec()
    {
        stopDraw(); ensureCustom();
        rack.pushUndo();
        rec.arm (path().loopBeats()); rack.box.board.pathHold = true; publishPath();
        strip.recB.setToggleState (true, juce::dontSendNotification); syncPathUi();
    }
    void stopRec()
    {
        if (! rec.armed && ! rack.box.board.pathHold) { strip.recB.setToggleState (false, juce::dontSendNotification); return; }
        rec.cancel(); rack.box.board.pathHold = false; publishPath();
        strip.recB.setToggleState (false, juce::dontSendNotification); syncPathUi();
    }
    void finishRec()
    {   // one loop captured: it plays from now on, exactly as performed (PHASE 0, even SPEED, SIZE 1)
        auto np = rec.result (path().bars);
        rec.cancel(); rack.box.board.pathHold = false;
        if (! np.empty()) path() = np;
        publishPath(); strip.recB.setToggleState (false, juce::dontSendNotification); refreshOrbitBox(); syncPathUi();
    }
    void stopDraw() { drawing = false; stroke.clear(); strokeT.clear(); strip.drawB.setToggleState (false, juce::dontSendNotification); }
    void finishDraw()
    {
        auto np = ghosty::spirit::OrbitPath::fromStroke (stroke, strokeT, path().bars);
        stopDraw();
        if (np.empty()) { syncPathUi(); return; }
        rack.pushUndo(); path() = np; publishPath(); refreshOrbitBox(); syncPathUi();
    }
    // the board's mouse while DRAW / REC is on (the ghosts and the puck are not dragged meanwhile)
    bool boardMouse (const juce::MouseEvent& e, int phase)
    {
        if (phase == 3) return drawing || rec.armed;
        const auto bp = view.toBoard (e.position);
        if (drawing)
        {
            const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
            if (phase == 0) { stroke.clear(); strokeT.clear(); }
            if (phase == 0 || stroke.empty() || bp.getDistanceFrom (stroke.back()) > 0.002f || phase == 2) { stroke.push_back (bp); strokeT.push_back (now); }
            if (phase == 2) finishDraw();
            view.repaint(); return true;
        }
        if (rec.armed)
        {   // REC: the hand moves the puck anywhere on the board
            setParam (apvts, "box_board_x", bp.x); setParam (apvts, "box_board_y", bp.y); rack.box.board.puckX = bp.x; rack.box.board.puckY = bp.y;
            if (rec.feed (rack.box.boardShared.beatsNow(), rack.box.boardShared.livePlaying.load(), bp.x, bp.y)) finishRec();
            view.repaint(); return true;
        }
        return false;
    }
    void syncPathUi()
    {   // the strip follows the board (undo, a loaded board, automation); cheap when nothing changed
        refreshOrbitBox();
        if (! strip.isVisible() && ! rec.armed) return;
        auto& pth = path();
        const int bi = pth.bars >= 8 ? 3 : pth.bars >= 4 ? 2 : pth.bars >= 2 ? 1 : 0;
        if (strip.loopBox.getSelectedItemIndex() != bi) strip.loopBox.setSelectedItemIndex (bi, juce::dontSendNotification);
        if (strip.revB.getToggleState() != pth.reverse) strip.revB.setToggleState (pth.reverse, juce::dontSendNotification);
        if (strip.drawB.getToggleState() != drawing) strip.drawB.setToggleState (drawing, juce::dontSendNotification);
        if (strip.recB.getToggleState() != rec.armed) strip.recB.setToggleState (rec.armed, juce::dontSendNotification);
        if (! strip.phaseS.isMouseButtonDown() && std::abs (strip.phaseS.getValue() - pth.phase) > 1.0e-6) strip.phaseS.setValue (pth.phase, juce::dontSendNotification);
        if (! strip.sizeS.isMouseButtonDown() && std::abs (strip.sizeS.getValue() - pth.scale) > 1.0e-6) strip.sizeS.setValue (pth.scale, juce::dontSendNotification);
        for (auto* b : { &strip.speedB, &strip.smoothB, &strip.revB, &strip.resB, &strip.clearB }) b->setEnabled (! pth.empty() && ! rec.armed);
        juce::String a, b2;
        const juce::String name = "CUSTOM " + juce::String (curPath() + 1);
        if (rec.armed) a = rec.running ? "REC " + juce::String (juce::roundToInt (rec.progress() * 100.0)) + " %  -  move the puck" : "REC: play the song to start";
        else if (drawing) a = "DRAW: drag a loop on the board";
        else if (pth.empty()) a = name + ": empty  -  DRAW or REC";
        else a = name + (customActive() ? ": playing" : ": pick it in ORBIT to play") + "  -  " + juce::String ((int) pth.pts.size()) + " points";
        b2 = pth.empty() ? juce::String ("the path loops over LOOP, in time with the song") : juce::String ("dots = even steps in time: close dots = slow");
        strip.setStatus (a, b2);
    }
    void setupPathUi()
    {
        // ORBIT gains CUSTOM 1..8 (the parameter keeps its 4 choices: its own attachment; CUSTOM = ORBIT OFF + the board's path)
        orbit.att.reset();
        orbit.addSeparator();
        for (int k = 0; k < ghosty::spirit::kMaxPaths; ++k) orbit.addItem ("CUSTOM " + juce::String (k + 1), kCustomId + k);
        if (auto* op = apvts.getParameter ("box_orbit")) orbitAtt = std::make_unique<juce::ParameterAttachment> (*op, [this] (float) { refreshOrbitBox(); syncPathUi(); }, nullptr);
        orbit.onChange = [this] { orbitPicked(); };
        refreshOrbitBox();
        pathB.setButtonText ("PATH"); pathB.setClickingTogglesState (true); styleButton (pathB, kBoardC);
        pathB.setTooltip ("DRAWN ORBIT: draw or record your own looping path for the puck (CUSTOM 1..8 in ORBIT)");
        pathB.getProperties().set ("gbPathUi", true);
        pathB.onClick = [this] { showStrip (pathB.getToggleState()); syncPathUi(); };
        addAndMakeVisible (pathB);
        addChildComponent (strip);
        auto& st = strip;
        st.drawB.onClick = [this] { if (strip.drawB.getToggleState()) { stopRec(); ensureCustom(); drawing = true; stroke.clear(); strokeT.clear(); } else stopDraw(); syncPathUi(); view.repaint(); };
        st.recB.onClick = [this] { if (strip.recB.getToggleState()) startRec(); else stopRec(); };
        st.loopBox.onChange = [this]
        {
            static const int bars[] { 1, 2, 4, 8 };
            const int nb = bars[juce::jlimit (0, 3, strip.loopBox.getSelectedItemIndex())];
            if (path().bars == nb) return;
            rack.pushUndo(); path().bars = nb; if (rec.armed) rec.arm (path().loopBeats()); publishPath(); syncPathUi();
        };
        st.speedB.onClick = [this] { openSpeed(); };
        st.smoothB.onClick = [this] { rack.pushUndo(); path().smooth(); publishPath(); syncPathUi(); };
        st.revB.onClick = [this] { rack.pushUndo(); path().reverse = strip.revB.getToggleState(); publishPath(); syncPathUi(); };
        st.clearB.onClick = [this] { rack.pushUndo(); const int bars = path().bars; path().clear(); path().bars = bars; publishPath(); refreshOrbitBox(); syncPathUi(); };
        st.resB.onClick = [this]
        {
            juce::PopupMenu m; m.addSectionHeader ("RESAMPLE TO");
            for (int n : { 8, 16, 32, 64, 128 }) m.addItem (n, juce::String (n) + " points");
            juce::Component::SafePointer<BoardPage> sp (this);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&strip.resB), [sp] (int n) { if (sp == nullptr || n < 3) return; sp->rack.pushUndo(); sp->path().resample (n); sp->publishPath(); sp->syncPathUi(); });
        };
        st.phaseS.onDragStart = [this] { rack.pushUndo(); };
        st.phaseS.onValueChange = [this] { path().phase = (float) strip.phaseS.getValue(); publishPath(); };
        st.sizeS.onDragStart = [this] { rack.pushUndo(); };
        st.sizeS.onValueChange = [this] { path().scale = (float) strip.sizeS.getValue(); publishPath(); };
        view.paintPath = [this] (juce::Graphics& g) { paintPathOn (g); };
        view.onBoardMouse = [this] (const juce::MouseEvent& e, int phase) { return boardMouse (e, phase); };
        syncPathUi();   // (the strip starts closed: PATH, DRAW / REC or picking CUSTOM n opens it)
    }
    void openSpeed()
    {
        if (path().empty()) return;
        auto ed = std::make_unique<SpeedCurveEditor> (kBoardC);
        ed->v = path().speed;
        juce::Component::SafePointer<BoardPage> sp (this);
        ed->onEditStart = [sp] { if (sp != nullptr) sp->rack.pushUndo(); };
        ed->onChange = [sp] (const std::array<float, 5>& v) { if (sp != nullptr) { sp->path().setSpeed (v); sp->publishPath(); } };
        fxtheme::retint (*ed);
        auto* top = getTopLevelComponent();   // (inside the plugin window: no extra desktop window in the host)
        juce::CallOutBox::launchAsynchronously (std::move (ed), top->getLocalArea (&strip.speedB, strip.speedB.getLocalBounds()), top);
    }
    // the path on the board: a glowing dotted line (the dots are even steps in time: close = slow), direction ticks, the playing spot
    void paintPathOn (juce::Graphics& g)
    {
        const auto& P = fxtheme::pal();
        const auto ac = ghosty::themes::readableOn (kBoardC.get(), P.waveBg, 3.0);
        if (drawing && stroke.size() > 1)
        {
            juce::Path sp; for (size_t i = 0; i < stroke.size(); ++i) { const auto q = view.toScreen (stroke[i].x, stroke[i].y); if (i == 0) sp.startNewSubPath (q); else sp.lineTo (q); }
            g.setColour (ac.withAlpha (0.25f)); g.strokePath (sp, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (ac); g.strokePath (sp, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        if (rec.armed && ! rec.pts.empty())
        {   // the REC trail so far (in recording order)
            const auto rc = ghosty::themes::readableOn (col::pink.get(), P.waveBg, 3.0);
            const size_t step = juce::jmax ((size_t) 1, rec.pts.size() / 200);
            for (size_t i = 0; i < rec.pts.size(); i += step) { const auto q = view.toScreen (rec.pts[i].x, rec.pts[i].y); g.setColour (rc.withAlpha (0.9f)); g.fillEllipse (q.x - 2.5f, q.y - 2.5f, 5.0f, 5.0f); }
        }
        const auto& b = rack.box.board;
        if (drawing || ! (strip.isVisible() || b.orbit.mode == ghosty::spirit::Orbit::Custom)) return;
        const auto& pth = b.paths[(size_t) curPath()];
        if (pth.empty()) return;
        const float alpha = rec.armed || ! customActive() ? 0.45f : 1.0f;
        const int N = 192;
        juce::Path line;
        for (int k = 0; k <= N; ++k) { const auto q = pth.shapeAt ((double) k / N); const auto sq = view.toScreen (q.x, q.y); if (k == 0) line.startNewSubPath (sq); else line.lineTo (sq); }
        g.setColour (ac.withAlpha (0.10f * alpha)); g.strokePath (line, juce::PathStrokeType (9.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (ac.withAlpha (0.20f * alpha)); g.strokePath (line, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const int D = 96;
        g.setColour (ac.withAlpha (0.95f * alpha));
        for (int k = 0; k < D; ++k) { const auto q = pth.at ((double) k / D); const auto sq = view.toScreen (q.x, q.y); g.fillEllipse (sq.x - 1.8f, sq.y - 1.8f, 3.6f, 3.6f); }
        for (int k = 0; k < 8; ++k)
        {   // direction ticks: a small arrow head every 1/8 of the loop, pointing the way the puck goes
            const double ph = ((double) k + 0.5) / 8.0;
            const auto q0 = pth.at (ph - 0.004), q1 = pth.at (ph + 0.004);
            const auto s0 = view.toScreen (q0.x, q0.y), s1 = view.toScreen (q1.x, q1.y);
            auto d = s1 - s0; const float len = d.getDistanceFromOrigin(); if (len < 0.5f) continue;
            d /= len; const juce::Point<float> nrm (-d.y, d.x); const auto tip = (s0 + s1) * 0.5f + d * 4.0f;
            juce::Path a; a.addTriangle (tip, tip - d * 8.0f + nrm * 4.5f, tip - d * 8.0f - nrm * 4.5f);
            g.setColour (ac.withAlpha (alpha)); g.fillPath (a);
        }
        if (customActive() && ! rec.armed)
        {   // where the song is on the path (the puck rides it)
            const auto q = pth.atBeats (rack.box.boardShared.beatsNow()); const auto sq = view.toScreen (q.x, q.y);
            g.setColour (ac.withAlpha (0.8f)); g.drawEllipse (sq.x - 13.0f, sq.y - 13.0f, 26.0f, 26.0f, 1.5f);
        }
    }
    juce::String failText;   // the first failure on this page (shown on the page; tests read it)
    void failed (const juce::String& where, const juce::String& what)
    {
        boardlog::write ("board: " + where + " threw: " + what + "  " + boardlog::context (rack, getBounds()));
        if (failText.isEmpty()) { failText = where + ": " + what; repaint(); }
    }
private:
    static constexpr const char* kLiveIds[] { "box_orbit", "box_orbit_rate", "box_orbit_depth", "box_board_x", "box_board_y" };
    void timerCallback() override { liveTick(); }
    void handleAsyncUpdate() override { liveTick(); }
    void parameterChanged (const juce::String&, float) override { triggerAsyncUpdate(); }   // (any thread)
    void askPercent (int g)
    {   // CUSTOM weight of one ghost (LAYER, WEIGHTS = CUSTOM); the weights are normalised
        if (! juce::isPositiveAndBelow (g, (int) rack.box.board.ghosts.size())) return;
        const auto id = rack.box.board.ghosts[(size_t) g].id;
        const float cur = rack.box.boardPct.count (id) ? rack.box.boardPct[id] : 100.0f;
        auto* w = new juce::AlertWindow ("Weight of ghost " + rack.box.board.ghosts[(size_t) g].label, "Percent (0 - 100; the weights are normalised):", juce::MessageBoxIconType::NoIcon, this);
        w->addTextEditor ("pct", juce::String (cur, 1)); w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey)); w->addButton ("Cancel", 0);
        juce::Component::SafePointer<BoardPage> sp (this);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([sp, w, id] (int res)
        {
            if (res != 1 || sp == nullptr) return;
            sp->rack.pushUndo();
            sp->rack.box.boardPct[id] = juce::jlimit (0.0f, 100.0f, w->getTextEditorContents ("pct").getFloatValue());
            sp->rack.boardEdited();
            if (sp->apvts.getParameter ("box_board_wsrc") != nullptr) setParam (sp->apvts, "box_board_wsrc", 2.0f);   // CUSTOM
        }), true);
    }
    void setFromMenu (int g)
    {   // what the ghost holds: what you hear now, a SNAPSHOT, a factory scene or one of your scenes
        juce::PopupMenu m; m.addSectionHeader ("GHOST " + rack.box.board.ghosts[(size_t) g].label + " HOLDS...");
        m.addItem (1, "What you hear now");
        if (rack.snapshots != nullptr && ! rack.snapshots->empty())
        { juce::PopupMenu sm; for (int i = 0; i < rack.snapshots->size(); ++i) sm.addItem (5000 + i, rack.snapshots->info (i).name); m.addSubMenu ("SNAPSHOTS", sm); }
        juce::PopupMenu fm; const auto& fs = factoryScenes(); for (int i = 0; i < (int) fs.size(); ++i) fm.addItem (100 + i, fs[(size_t) i].display); m.addSubMenu ("Factory scenes", fm);
        const auto files = listXml (sceneDir());
        if (! files.isEmpty()) { juce::PopupMenu um; for (int i = 0; i < files.size(); ++i) um.addItem (1000 + i, files[i].getFileNameWithoutExtension()); m.addSubMenu ("Your scenes", um); }
        juce::Component::SafePointer<BoardPage> sp (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&view).withMousePosition(), [sp, g, files] (int r)
        {
            if (sp == nullptr || r <= 0) return;
            auto& rk = sp->rack;
            std::function<void()> load;
            if (r == 1) load = [] {};
            else if (r >= 5000 && rk.snapshots != nullptr) { const int i = r - 5000; load = [&rk, i] { if (i < rk.snapshots->size()) rk.snapshots->load (i); }; }
            else if (r >= 1000 && r - 1000 < files.size()) { const auto f = files[r - 1000]; load = [&rk, f] { rk.loadUserScene (f); }; }
            else if (r >= 100 && r - 100 < (int) factoryScenes().size()) { const juce::String key = factoryScenes()[(size_t) (r - 100)].key; load = [&rk, key] { rk.owner->applyScene (*rk.apvts, key); }; }
            if (load) { rk.boardSetFrom (g, load); sp->changed(); }
        });
    }
    void askName()
    {
        auto* w = new juce::AlertWindow ("Save SPIRIT BOARD", "Name:", juce::MessageBoxIconType::NoIcon, this);
        w->addTextEditor ("name", "My Board"); w->addButton ("SAVE", 1, juce::KeyPress (juce::KeyPress::returnKey)); w->addButton ("Cancel", 0);
        juce::Component::SafePointer<BoardPage> sp (this);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([sp, w] (int res) { if (res == 1 && sp != nullptr) sp->rack.saveBoardFile (w->getTextEditorContents ("name")); }), true);
    }
    void loadMenu()
    {
        juce::PopupMenu m; m.addSectionHeader ("SPIRIT BOARDS");
        const auto files = listXml (boardDir());
        if (files.isEmpty()) m.addItem (-1, "no saved boards yet: SAVE BOARD... first", false);
        for (int i = 0; i < files.size(); ++i) m.addItem (100 + i, files[i].getFileNameWithoutExtension());
        juce::Component::SafePointer<BoardPage> sp (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&loadB), [sp, files] (int r) { if (sp != nullptr && r >= 100 && r - 100 < files.size()) { sp->rack.loadBoardFile (files[r - 100]); sp->changed(); } });
    }
    static constexpr fxtheme::C kBoardC { 0xffff2bd6 };
    juce::AudioProcessorValueTreeState& apvts; Rack::Impl& rack;
    ghosty::spirit::BoardView view;
    juce::TextButton onBtn, snapB, randB, circB, clearB, saveB, loadB;
    juce::ComboBox modeBox; FineSlider sharp { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox }; juce::Rectangle<int> sharpCap;
    Choice orbit, rate; Knob depth;
    Choice blendC, lawC, srcC, kC; bool layerStrip = false; juce::Rectangle<int> layerInfo;
    // DRAWN ORBIT
    std::unique_ptr<juce::ParameterAttachment> orbitAtt;
    juce::TextButton pathB; PathStrip strip { kBoardC };
    ghosty::spirit::PathRecorder rec;
    bool drawing = false; std::vector<juce::Point<float>> stroke; std::vector<double> strokeT;
public:
    PathStrip& pathStrip() { return strip; }   // (tests)
    juce::TextButton& pathButton() { return pathB; }
    bool isDrawing() const { return drawing; }
    const ghosty::spirit::PathRecorder& recorder() const { return rec; }
};

// ---------------------------------------------------------------- MACRO strip (bottom of the Box panel)
class MacroStrip : public juce::Component
{
public:
    MacroStrip (juce::AudioProcessorValueTreeState& s, Rack::Impl& r) : rack (r), par (s, "fx_par_on", "PARALLEL", T (juce::Colour (0xffFF4FD8))), blend (s, "fx_par_blend", "BLEND A|B", T (juce::Colour (0xffFF4FD8)))
    {
        par.setTooltip ("PARALLEL LANES: right-click a unit tile to put it in lane B; both lanes run from the input and are summed (latency-aligned)");
        blend.slider.setSliderStyle (juce::Slider::LinearHorizontal); blend.slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        addAndMakeVisible (par); addAndMakeVisible (blend);
        for (int k = 0; k < kMacros; ++k)
        {
            auto* kn = knobs.add (new Knob (s, "box_macro" + juce::String (k + 1), rack.box.macroNames[(size_t) k], PerformPage::macroColour (k)));
            kn->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            kn->slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            kn->getProperties().set ("gpfxMacro", k); kn->slider.getProperties().set ("gpfxMacro", k);
            kn->slider.setTooltip ("Right-click: what this macro moves, depth, rename. Right-click any knob in the rack to assign it here.");
            addAndMakeVisible (kn);
        }
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 3); r.removeFromLeft (70);
        auto pr = r.removeFromRight (190); pr.removeFromLeft (10);
        par.setBounds (pr.removeFromTop (pr.getHeight() / 2).reduced (2, 3)); blend.setBounds (pr.reduced (0, 2));
        blendCap = blend.getBounds();
        const int w = r.getWidth() / kMacros;
        for (auto* k : knobs) k->setBounds (r.removeFromLeft (w).reduced (6, 0));
    }
    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        if (ui::ghost()) {}   // GHOST: the glass bar is in the window's cached static layer (Panel::paintGhostSurfaces)
        else
        {
        g.setColour (T (juce::Colour (0xff11131a))); g.fillRoundedRectangle (b, 8);
        g.setColour (kMacroC.withAlpha (0.2f)); g.drawRoundedRectangle (b.reduced (0.5f), 8, 1.0f);
        }
        drawCaption (g, b.withWidth (76).withTrimmedLeft (10), "MACROS", kMacroC, 12);
        for (int k = 0; k < kMacros; ++k)
        {   // how many settings each macro moves right now (assignments whose unit is on)
            const int n = rack.box.liveAssignCount (k, rack.onP);
            auto kb = knobs[k]->getBounds().toFloat();
            drawCaption (g, { kb.getRight() - 22, kb.getY(), 22, 12 }, juce::String (n), col::dim, 8.5f, juce::Justification::centredRight);
        }
    }
    void refresh() { for (int k = 0; k < kMacros; ++k) if (knobs[k]->caption != rack.box.macroNames[(size_t) k]) { knobs[k]->caption = rack.box.macroNames[(size_t) k]; knobs[k]->repaint(); } repaint(); }
private:
    Rack::Impl& rack; juce::OwnedArray<Knob> knobs; Toggle par; Knob blend; juce::Rectangle<int> blendCap;
};

// right-click menus: on a rack knob (assign to a macro) and on a macro (its assignments)
void macroMenuForParam (juce::Component& host, Rack::Impl& rack, const juce::String& id, std::function<void()> after, juce::Slider* slider = nullptr)
{
    if (! id.startsWith ("fx_")) return;
    auto* prm = rack.apvts->getParameter (id); if (prm == nullptr) return;
    juce::PopupMenu m; m.addSectionHeader (prm->getName (40).replace ("FX ", ""));
    juce::Component::SafePointer<juce::Slider> sp (slider);
    if (slider != nullptr) addSliderItems (m, true);   // "Type value..." first
    if (rack.box.isDiscrete (id))
    {   // switches and choices would flip with a jump somewhere along the macro: MORPH handles those (ducked), macros do not
        m.addItem (-1, "switches and choices can't ride a macro (use MORPH for those)", false);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&host).withMousePosition(), [] (int) {});
        return;
    }
    juce::PopupMenu as;
    for (int k = 0; k < kMacros; ++k)
    {
        bool has = false; for (auto& a : rack.box.assigns[(size_t) k]) has = has || a.id == id;
        as.addItem (100 + k, juce::String (k + 1) + "  " + rack.box.macroNames[(size_t) k], true, has);
    }
    m.addSubMenu ("Assign to macro...", as);
    for (int k = 0; k < kMacros; ++k) for (auto& a : rack.box.assigns[(size_t) k]) if (a.id == id) m.addItem (200 + k, "Remove from " + rack.box.macroNames[(size_t) k]);
    {   // GHOST MOTION timeline: this setting as a lane (MOTION tab, TIMELINE)
        bool has = false; for (auto& L : rack.box.timeline.lanes) has = has || L.id == id;
        m.addItem (300, has ? "In the GHOST MOTION timeline" : "Add to GHOST MOTION timeline", ! has && (int) rack.box.timeline.lanes.size() < kTlLanes, has);
    }
    if (slider != nullptr) addSliderItems (m, false);   // "Reset to default" last
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&host).withMousePosition(), [&rack, id, after, sp] (int r)
    {
        if (handleSliderItem (r, sp.getComponent())) return;
        if (r >= 100 && r < 100 + kMacros) { rack.pushUndo(); rack.box.assign (r - 100, id, 0.5f); }
        else if (r >= 200 && r < 200 + kMacros) { rack.pushUndo(); rack.box.unassign (r - 200, id); }
        else if (r == 300) { rack.pushUndo(); rack.box.tlAddLane (id); }
        if (after) after();
    });
}
void macroMenu (juce::Component& host, juce::Component& panel, Rack::Impl& rack, int k, std::function<void()> after)
{
    juce::PopupMenu m; m.addSectionHeader ("MACRO " + juce::String (k + 1) + "  " + rack.box.macroNames[(size_t) k]);
    const auto list = rack.box.assigns[(size_t) k];
    if (list.empty()) m.addItem (-1, "nothing assigned yet: right-click any knob in the rack", false);
    static const float depths[] { 1.0f, 0.5f, 0.25f, -0.25f, -0.5f, -1.0f };
    for (int i = 0; i < (int) list.size() && i < 40; ++i)
    {
        auto* p = rack.apvts->getParameter (list[(size_t) i].id);
        juce::PopupMenu sub;
        for (int d = 0; d < 6; ++d) sub.addItem (1000 + i * 20 + d, juce::String ("depth ") + (depths[d] > 0 ? "+" : "") + juce::String (juce::roundToInt (depths[d] * 100)) + "%", true, std::abs (list[(size_t) i].depth - depths[d]) < 0.01f);
        sub.addSeparator();
        for (int c = 0; c < 4; ++c) sub.addItem (1000 + i * 20 + 10 + c, "curve " + macroCurveNames()[c].toLowerCase(), true, list[(size_t) i].curve == c);
        sub.addSeparator(); sub.addItem (1000 + i * 20 + 19, "remove");
        m.addSubMenu ((p ? p->getName (40).replace ("FX ", "") : list[(size_t) i].id) + "   " + (list[(size_t) i].depth > 0 ? "+" : "") + juce::String (juce::roundToInt (list[(size_t) i].depth * 100)) + "%", sub);
    }
    m.addSeparator();
    m.addItem (1, "Rename...");
    m.addItem (2, "Clear this macro");
    m.addItem (3, "All macros back to the factory set");
    juce::Component::SafePointer<juce::Component> sp (&panel);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&host).withMousePosition(), [&rack, k, list, after, sp] (int r)
    {
        if (r <= 0) return;
        if (r == 1 && sp != nullptr)
        {
            askInPanel (*sp, "Rename macro", "Name for macro " + juce::String (k + 1) + ":", true, rack.box.macroNames[(size_t) k], "Rename",
                        [&rack, k, after] (juce::String n) { if (n.trim().isNotEmpty()) { rack.pushUndo(); rack.box.macroNames[(size_t) k] = n.trim().toUpperCase().substring (0, 12); rack.box.rebuild(); } if (after) after(); });
            return;
        }
        rack.pushUndo();
        if (r == 2) { rack.box.assigns[(size_t) k].clear(); rack.box.rebuild(); }
        else if (r == 3) { rack.box.setDefaultMacros(); rack.box.rebuild(); }
        else if (r >= 1000)
        {
            const int i = (r - 1000) / 20, sub = (r - 1000) % 20;
            if (i < (int) list.size())
            {
                auto a = list[(size_t) i];
                if (sub < 6) rack.box.assign (k, a.id, depths[sub], a.curve);
                else if (sub >= 10 && sub < 14) rack.box.assign (k, a.id, a.depth, sub - 10);
                else if (sub == 19) rack.box.unassign (k, a.id);
            }
        }
        if (after) after();
    });
}
