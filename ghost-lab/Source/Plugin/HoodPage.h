#pragma once
// UNDER THE HOOD (build 9: in plain words). Every engine process Ghost Lab ships, organised the way the friendliest of the
// engine's other front ends do it: by WHAT IT DOES TO THE SOUND (time, pitch, tone, texture, space + level, blending two
// sounds, data tools), each version of a process its own entry with our own name and a one-line description (Soundshaper's
// one-word names and process tables; SoundThread's categories, titles and descriptions). Pick one on the left; the right
// shows what it does, its versions as buttons, and every control big, with our name first and the engine's own name and
// words small under it. RUN runs the real program with exactly those values; the ENGINE ROOM shows the engine's own
// information text, the analysis settings and the exact command (what will run / what ran).
#include "Theme.h"
#include "../Core/Hood.h"
#include "../Core/Engine.h"
#include <set>

namespace glab
{
using namespace theme;

// the page's own skin: big rounded buttons, big tick boxes, thick sliders with a glowing thumb
struct HoodLook : LabLook
{
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& base, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (1.0f);
        auto c = base;
        if (! b.isEnabled()) c = c.withMultipliedAlpha (0.4f);
        if (b.getToggleState()) c = b.findColour (juce::TextButton::buttonOnColourId);
        if (over) c = c.brighter (0.12f);
        if (down) c = c.darker (0.15f);
        const float rad = juce::jmin (10.0f, r.getHeight() * 0.35f);
        g.setGradientFill (juce::ColourGradient (c.brighter (0.08f), 0, r.getY(), c.darker (0.12f), 0, r.getBottom(), false));
        g.fillRoundedRectangle (r, rad);
        g.setColour (juce::Colours::white.withAlpha (over ? 0.28f : 0.14f)); g.drawRoundedRectangle (r, rad, 1.0f);
    }
    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return font (juce::jmin (14.5f, (float) h * 0.42f), 1, 0.08f); }
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool) override
    {
        auto r = b.getLocalBounds().toFloat();
        const float bs = juce::jmin (18.0f, r.getHeight() - 4.0f);
        auto box = juce::Rectangle<float> (bs, bs).withPosition (r.getX() + 2, r.getCentreY() - bs * 0.5f);
        const bool on = b.getToggleState();
        const auto tick = b.findColour (juce::ToggleButton::tickColourId);
        g.setColour (on ? tick.withAlpha (0.9f) : night0); g.fillRoundedRectangle (box, 4.0f);
        g.setColour (on ? tick : text.withAlpha (over ? 0.8f : 0.45f)); g.drawRoundedRectangle (box, 4.0f, 1.3f);
        if (on)
        {
            juce::Path p;
            p.startNewSubPath (box.getX() + bs * 0.24f, box.getCentreY());
            p.lineTo (box.getX() + bs * 0.43f, box.getBottom() - bs * 0.26f);
            p.lineTo (box.getRight() - bs * 0.22f, box.getY() + bs * 0.26f);
            g.setColour (juce::Colour (0xff0e1408)); g.strokePath (p, juce::PathStrokeType (bs * 0.13f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour (b.isEnabled() ? text.withAlpha (on ? 1.0f : 0.85f) : dim);
        g.setFont (font (juce::jmin (13.0f, r.getHeight() * 0.55f), 1, 0.06f));
        g.drawFittedText (b.getButtonText(), r.withTrimmedLeft (bs + 9).toNearestInt(), juce::Justification::centredLeft, 1, 0.75f);
    }
    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider& s) override
    {
        const float th = 8.0f, cy = (float) y + (float) h * 0.5f;
        auto track = juce::Rectangle<float> ((float) x, cy - th * 0.5f, (float) w, th);
        g.setColour (s.findColour (juce::Slider::backgroundColourId)); g.fillRoundedRectangle (track, th * 0.5f);
        g.setColour (juce::Colours::white.withAlpha (0.12f)); g.drawRoundedRectangle (track, th * 0.5f, 1.0f);
        const auto c = s.findColour (juce::Slider::trackColourId);
        auto filled = track.withRight (juce::jmax (track.getX() + th, pos));
        g.setGradientFill (juce::ColourGradient (c.darker (0.3f), track.getX(), cy, c.brighter (0.2f), filled.getRight(), cy, false));
        g.fillRoundedRectangle (filled, th * 0.5f);
        auto thumb = juce::Rectangle<float> (20, 20).withCentre ({ pos, cy });
        juce::Path tp; tp.addEllipse (thumb);
        art::glow (g, tp, c.withAlpha (0.8f), 9);
        g.setColour (s.findColour (juce::Slider::thumbColourId)); g.fillPath (tp);
        g.setColour (c); g.drawEllipse (thumb.reduced (2.5f), 2.5f);
    }
};

class HoodPage : public juce::Component, private juce::ListBoxModel
{
public:
    std::function<void (const Step&)> onRun;      // the editor runs it on the engine
    std::function<void()> onUndo;

    static constexpr int kRowH = 76, kCheckH = 48, kTextH = 74, kNameW = 300;
    juce::Colour accent { purple };                // the chosen process's category colour
    bool compact = false;                          // phone-narrow: each control stacks (name on top)

    explicit HoodPage (Engine& e) : engine (e)
    {
        setLookAndFeel (&look);
        search.setTextToShowWhenEmpty ("Search: stretch, freeze, grain, reverse...", dim.brighter (0.2f));
        styleEditor (search, false);
        search.setIndents (34, 0);
        search.onTextChange = [this] { rebuildRows(); };
        addAndMakeVisible (search);
        // the category tabs: ALL, then what each process does to the sound
        addTab ("all", "ALL", text);
        for (auto& c : hood::categories()) addTab (c.id, c.title, c.colour);
        list.setModel (this);
        list.setRowHeight (54);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        list.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
        list.getViewport()->setScrollBarThickness (7);
        addAndMakeVisible (list);

        addAndMakeVisible (viewport);
        viewport.setViewedComponent (&form, false);
        viewport.setScrollBarThickness (8);
        viewport.setScrollBarsShown (true, false);

        form.addChildComponent (modeBox);   // (kept for the old API; the versions are buttons now)
        modeBox.onChange = [this] { if (! building) { rememberValues(); const int id = modeBox.getSelectedId(); setMode (id >= 1000 ? id - 1000 : 0); } };
        for (auto* t : { &pointsEd, &overlapEd }) { styleEditor (*t, false); form.addChildComponent (t); t->setJustification (juce::Justification::centred); t->onTextChange = [this] { updateCommand(); }; }
        pointsEd.setText ("1024", false); overlapEd.setText ("3", false);
        styleEditor (usageView, true); usageView.setFont (font (12.0f)); form.addChildComponent (usageView);
        styleEditor (commandView, true); commandView.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.5f, 0)));
        form.addChildComponent (commandView);
        for (auto* b : { &runBtn, &undoBtn, &defaultsBtn, &infoBtn })
        {
            addAndMakeVisible (*b);
            b->setColour (juce::TextButton::buttonColourId, night2);
            b->setColour (juce::TextButton::textColourOffId, text);
            b->setColour (juce::TextButton::textColourOnId, text);
        }
        runBtn.setButtonText ("RUN IT"); undoBtn.setButtonText ("UNDO"); defaultsBtn.setButtonText ("RESET ALL"); infoBtn.setButtonText ("ENGINE ROOM");
        runBtn.setColour (juce::TextButton::buttonColourId, green);
        runBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff0e1408));
        runBtn.setTooltip ("Run the engine on your sound with these settings (UNDO takes it back)");
        defaultsBtn.setTooltip ("Every control back to the engine's own default");
        infoBtn.setTooltip ("The engine's own words about this process, the analysis settings, and the exact command");
        runBtn.onClick = [this] { run(); };
        undoBtn.onClick = [this] { if (onUndo) onUndo(); };
        defaultsBtn.onClick = [this] { setDefaults(); };
        infoBtn.setClickingTogglesState (true);
        infoBtn.setColour (juce::TextButton::buttonOnColourId, purple.withAlpha (0.45f));
        infoBtn.onClick = [this] { showInfo = infoBtn.getToggleState(); layoutForm (formWidth()); };
        form.addChildComponent (moreBtn);
        moreBtn.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        moreBtn.setColour (juce::TextButton::textColourOffId, purple.brighter (0.4f));
        moreBtn.onClick = [this] { aboutOpen = ! aboutOpen; moreBtn.setButtonText (aboutOpen ? "SHOW LESS" : "READ MORE"); layoutForm (formWidth()); };
        rebuildRows();
        selectProcess ("stretch", "time");
    }
    ~HoodPage() override { list.setModel (nullptr); setLookAndFeel (nullptr); }

    // ---- what the tests and the editor use
    void selectProcess (const juce::String& prog, const juce::String& name)
    {
        proc = hood::find (prog, name);
        if (proc == nullptr) return;
        values.clear(); extraFiles.clear();
        aboutOpen = false; moreBtn.setButtonText ("READ MORE");
        building = true;
        modeBox.clear (juce::dontSendNotification);
        const auto modes = hood::modesOf (*proc);
        if (modes.empty()) { modeBox.addItem ("(this process has no modes)", 1); modeBox.setSelectedId (1, juce::dontSendNotification); mode = 0; }
        else
        {
            for (int m : modes) modeBox.addItem ("MODE " + juce::String (m), 1000 + m);
            mode = modes.front();
            modeBox.setSelectedId (1000 + mode, juce::dontSendNotification);
        }
        building = false;
        buildVersions();
        buildControls();
        usageView.setText (hood::shownText (proc->usage), false);
        usageView.moveCaretToTop (false);
        selectRowForCurrent();
        resized();
        updateCommand();
        viewport.setViewPosition (0, 0);
    }
    void setMode (int m)
    {
        if (proc == nullptr) return;
        rememberValues();
        mode = m;
        building = true;
        modeBox.setSelectedId (m == 0 ? 1 : 1000 + m, juce::dontSendNotification);
        building = false;
        buildVersions();
        buildControls();
        selectRowForCurrent();
        updateCommand();
    }
    void setValue (const juce::String& name, const juce::String& v)
    {
        values[name] = v;
        for (auto& c : controls) if (c->key() == name) c->set (v);
        updateCommand();
    }
    void setDefaults()
    {
        if (dialog == nullptr) return;
        for (auto& kv : hood::dialogDefaults (*dialog)) setValue (kv.first, kv.second);
    }
    void setAnalysis (const juce::String& points, const juce::String& overlap) { pointsEd.setText (points, false); overlapEd.setText (overlap, false); refreshDialog(); updateCommand(); }
    void setSearch (const juce::String& t) { search.setText (t, false); rebuildRows(); }
    void setCategory (const juce::String& id) { cat = id; for (auto* t : tabs) t->setToggleState (t->getComponentID() == id, juce::dontSendNotification); rebuildRows(); }
    void setShowInformation (bool on) { infoBtn.setToggleState (on, juce::dontSendNotification); showInfo = on; layoutForm (formWidth()); }
    int visibleProcessCount() const
    {
        std::set<const hood::Process*> seen;
        for (auto& r : rows) if (r.entry != nullptr) seen.insert (r.entry->proc);
        return (int) seen.size();
    }
    bool listShows (const juce::String& prog, const juce::String& name) const
    {
        for (auto& r : rows) if (r.entry != nullptr && r.entry->prog == prog && r.entry->name == name) return true;
        return false;
    }
    int paramFieldCount() const { return (int) controls.size(); }
    const hood::Process* current() const { return proc; }
    int currentMode() const { return mode; }
    const hood::Dialog* currentDialog() const { return dialog.get(); }
    bool dialogIsLive() const { return live; }
    juce::String valueOf (const juce::String& key) const { auto it = values.find (key); return it != values.end() ? it->second : juce::String(); }
    // the sound this page's ranges were asked for (what the engine's front end would send about the file)
    hood::SoundInfo soundInfo() const
    {
        hood::SoundInfo s;
        if (auto a = engine.processedAudio())
        {
            s.sampleRate = a->sampleRate; s.samples = a->length();
            const bool spectral = proc != nullptr && hood::Request { proc->prog, proc->name }.spectral();
            s.channels = spectral || (proc != nullptr && proc->prog == "pvoc") ? 1 : a->channels();
        }
        s.points = juce::jmax (2, pointsEd.getText().getIntValue() > 0 ? pointsEd.getText().getIntValue() : 1024);
        s.overlap = juce::jmax (1, overlapEd.getText().getIntValue() > 0 ? overlapEd.getText().getIntValue() : 3);
        return s;
    }
    // the sound changed (a drop, a run, UNDO): the ranges that depend on it are asked again (once per sound)
    void soundChanged()
    {
        juce::String key;
        if (auto a = engine.processedAudio()) key = juce::String::toHexString ((juce::pointer_sized_int) a.get()) + ":" + juce::String (a->length()) + ":" + juce::String (a->sampleRate) + ":" + juce::String (a->channels());
        if (key == soundKey && (live || key.isEmpty())) return;
        soundKey = key;
        refreshDialog();
        form.repaint();
    }

    juce::File dataFolder() const { return engine.sessionFolder().getChildFile ("data"); }
    void setExtraFiles (const juce::StringArray& files) { extraFiles = files; if (sounds != nullptr) sounds->rebuild(); updateCommand(); }
    void makeDataNow (const juce::String& key, AudioPtr from)   // tests: MAKE FROM SOUND on this thread
    {
        for (auto& c : controls) if (c->key() == key && c->dataKind.isNotEmpty()) c->makeNow (from);
    }
    hood::Request request()
    {
        rememberValues();
        hood::Request r;
        if (proc == nullptr) return r;
        r.prog = proc->prog; r.name = proc->name; r.mode = mode;
        r.values = values;
        r.dataDir = dataFolder().getFullPathName();
        if (const auto* f = hood::formFor (*proc, mode)) if (f->multi) r.extraFiles = extraFiles;
        r.points = pointsEd.getText().trim().isEmpty() ? "1024" : pointsEd.getText().trim();
        r.overlap = overlapEd.getText().trim().isEmpty() ? "3" : overlapEd.getText().trim();
        return r;
    }
    void run()
    {
        if (proc == nullptr || ! onRun) return;
        Step s; s.op = Op::Hood; s.seed = 1; s.data = request().toData();
        onRun (s);
    }
    // after a run: the commands that really ran (the engine's own words under them). A failure opens the ENGINE ROOM.
    void showRan (const juce::String& commands, bool ok)
    {
        ran = commands; ranOk = ok;
        commandView.setText (hood::shownText ((ok ? "RAN:\n" : "DID NOT WORK:\n") + commands), false);
        commandView.moveCaretToTop (false);
        if (! ok && ! showInfo) setShowInformation (true);   // a failure opens the ENGINE ROOM: the engine's own words say why
    }
    juce::String commandText() const { return commandView.getText(); }
    juce::String usageText() const { return usageView.getText(); }
    juce::String searchHint() const { return search.getTextToShowWhenEmpty(); }
    juce::Component& formComponent() { return form; }

    void resized() override
    {
        auto r = getLocalBounds();
        // left: search, the categories (two columns), the list. Phone-narrow: the list on top, the page under it.
        const bool stacked = r.getWidth() < 760;
        auto left = stacked ? r.removeFromTop (juce::jmin (360, r.getHeight() * 45 / 100)) : r.removeFromLeft (juce::jlimit (250, 340, r.getWidth() * 26 / 100));
        listR = left;
        auto l = left.reduced (8, 8);
        search.setBounds (l.removeFromTop (34));
        l.removeFromTop (8);
        const int n = tabs.size(), cols = 2, rowsN = (n + cols - 1) / cols, th = 30, tg = 6;
        auto grid = l.removeFromTop (rowsN * (th + tg));
        for (int i = 0; i < n; ++i)
        {
            const int c = i % cols, rr = i / cols;
            const int cw = (grid.getWidth() - tg) / cols;
            tabs[i]->setBounds (grid.getX() + c * (cw + tg), grid.getY() + rr * (th + tg), cw, th);
        }
        l.removeFromTop (4);
        list.setBounds (l);
        if (stacked) r.removeFromTop (12); else r.removeFromLeft (12);
        pageR = r;
        // the page's head (not scrolling): what it is on the left, RUN IT and friends on the right
        auto head = r.reduced (14, 10).removeFromTop (stacked ? 170 : 84);
        headR = head;
        auto btns = stacked ? head.removeFromBottom (82) : head.removeFromRight (juce::jmin (330, head.getWidth() / 2));
        auto rowA = btns.removeFromTop (44);
        runBtn.setBounds (rowA.removeFromLeft (rowA.getWidth() * 60 / 100));
        rowA.removeFromLeft (8);
        undoBtn.setBounds (rowA);
        btns.removeFromTop (8);
        auto rowB = btns.removeFromTop (30);
        defaultsBtn.setBounds (rowB.removeFromLeft ((rowB.getWidth() - 8) / 2));
        rowB.removeFromLeft (8);
        infoBtn.setBounds (rowB);
        titleR = stacked ? head.withTrimmedBottom (4) : head.withTrimmedRight (14);
        r.removeFromTop (stacked ? 186 : 100);
        viewport.setBounds (r.reduced (6, 0).withTrimmedBottom (6));
        layoutForm (formWidth());
    }

    void paint (juce::Graphics& g) override
    {
        // the list's panel and the page's panel
        art::glassPanel (g, listR.toFloat(), purple, 12.0f, 0.5f);
        art::glassPanel (g, pageR.toFloat(), accent, 12.0f, 0.55f);
        if (proc == nullptr) return;
        const auto* e = entry;
        const auto* c = e != nullptr ? hood::category (e->cat) : nullptr;
        auto h = titleR.toFloat();
        // the category chip and the engine's own name; our name, big; the one line
        float x = h.getX();
        if (c != nullptr)
        {
            g.setFont (font (11.0f, 2, 0.12f));
            const float cw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), c->title) + 26;
            auto chip = juce::Rectangle<float> (x, h.getY() + 2, cw, 20);
            g.setColour (accent.withAlpha (0.2f)); g.fillRoundedRectangle (chip, 10.0f);
            g.setColour (accent); g.drawRoundedRectangle (chip, 10.0f, 1.0f);
            g.fillEllipse (chip.getX() + 8, chip.getCentreY() - 3, 6, 6);
            g.setColour (theme::text); g.drawText (c->title, chip.withTrimmedLeft (18), juce::Justification::centredLeft);
            x = chip.getRight() + 10;
        }
        g.setColour (dim.brighter (0.4f)); g.setFont (font (11.0f, 0, 0.04f));
        g.drawFittedText ("ENGINE:  " + proc->id() + (mode > 0 ? "  mode " + juce::String (mode) : juce::String()),
                          juce::Rectangle<float> (x, h.getY() + 2, h.getRight() - x, 20).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
        g.setColour (theme::text); g.setFont (font (26.0f, 2, 0.04f));
        g.drawFittedText (e != nullptr ? e->title.toUpperCase() : proc->id(), h.withTrimmedTop (24).withHeight (36).toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
        if (e != nullptr)
        {
            g.setColour (accent.brighter (0.35f)); g.setFont (font (13.5f, 1));
            g.drawFittedText (e->shortText, h.withTrimmedTop (60).withHeight (22).toNearestInt(), juce::Justification::centredLeft, 2, 0.8f);
        }
        g.setColour (accent.withAlpha (0.25f));
        g.fillRect ((float) pageR.getX() + 14, (float) headR.getBottom() + 8, (float) pageR.getWidth() - 28, 1.0f);
    }
    void paintOverChildren (juce::Graphics& g) override
    {
        // the search icon sits inside the box (drawn over it)
        icon::search (g, juce::Rectangle<float> (15, 15).withCentre ({ (float) search.getX() + 18, (float) search.getBounds().getCentreY() }), sub.brighter (0.4f));
    }

private:
    struct Row { const hood::Category* header = nullptr; const hood::Entry* entry = nullptr; };
    int formWidth() const { return juce::jmax (320, viewport.getWidth() - 12); }

    void styleEditor (juce::TextEditor& t, bool multi)
    {
        t.setMultiLine (multi, multi);
        t.setReadOnly (multi);
        t.setColour (juce::TextEditor::backgroundColourId, night0.withAlpha (0.85f));
        t.setColour (juce::TextEditor::textColourId, text);
        t.setColour (juce::TextEditor::outlineColourId, juce::Colours::white.withAlpha (0.16f));
        t.setColour (juce::TextEditor::focusedOutlineColourId, purple);
        t.setColour (juce::CaretComponent::caretColourId, green);
        t.setFont (font (14.0f));
        t.setScrollbarsShown (true);
    }

    // the category tabs
    struct Tab : juce::Button
    {
        juce::Colour colour;
        Tab (const juce::String& id, const juce::String& title, juce::Colour c) : juce::Button (title), colour (c) { setComponentID (id); setClickingTogglesState (false); }
        void paintButton (juce::Graphics& g, bool over, bool) override
        {
            auto r = getLocalBounds().toFloat().reduced (1.0f);
            const bool on = getToggleState();
            g.setColour (night0.withAlpha (0.7f)); g.fillRoundedRectangle (r, 10.0f);
            if (on) { juce::Path p; p.addRoundedRectangle (r, 10.0f); art::glow (g, p, colour.withAlpha (0.5f), 10); g.setColour (colour.withAlpha (0.22f)); g.fillRoundedRectangle (r, 10.0f); }
            else if (over) { g.setColour (colour.withAlpha (0.1f)); g.fillRoundedRectangle (r, 10.0f); }
            g.setColour (colour.withAlpha (on ? 1.0f : 0.5f)); g.drawRoundedRectangle (r, 10.0f, on ? 1.6f : 1.0f);
            g.setColour (colour); g.fillEllipse (r.getX() + 12, r.getCentreY() - 4, 8, 8);
            g.setColour (on ? theme::text : theme::text.withAlpha (0.85f)); g.setFont (font (juce::jmin (13.0f, r.getHeight() * 0.44f), 2, 0.06f));
            g.drawFittedText (getButtonText(), r.withTrimmedLeft (26).withTrimmedRight (6).toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
        }
    };
    void addTab (const juce::String& id, const juce::String& title, juce::Colour c)
    {
        auto* t = tabs.add (new Tab (id, title, c));
        t->setToggleState (id == cat, juce::dontSendNotification);
        t->onClick = [this, id] { setCategory (id); };
        addAndMakeVisible (t);
    }

    // ---- the versions of the chosen process (its modes, by our names) as buttons
    struct Version : juce::Button
    {
        juce::Colour colour; int mode;
        Version (const juce::String& t, int m, juce::Colour c) : juce::Button (t), colour (c), mode (m) {}
        void paintButton (juce::Graphics& g, bool over, bool) override
        {
            auto r = getLocalBounds().toFloat().reduced (1.0f);
            const bool on = getToggleState();
            const float rad = r.getHeight() * 0.5f;
            g.setColour (on ? colour.withAlpha (0.28f) : night0.withAlpha (0.7f)); g.fillRoundedRectangle (r, rad);
            g.setColour (colour.withAlpha (on ? 1.0f : (over ? 0.8f : 0.45f))); g.drawRoundedRectangle (r, rad, on ? 1.6f : 1.0f);
            g.setColour (on ? theme::text : theme::text.withAlpha (0.85f)); g.setFont (font (12.5f, 1, 0.04f));
            g.drawFittedText (getButtonText(), r.reduced (12, 0).toNearestInt(), juce::Justification::centred, 1, 0.75f);
        }
    };
    void buildVersions()
    {
        versions.clear();
        if (proc == nullptr) return;
        const auto vs = hood::versionsOf (*proc);
        if (vs.size() < 2) return;
        for (auto* v : vs)
        {
            const auto* c = hood::category (v->cat);
            auto* b = versions.add (new Version (v->title.toUpperCase(), v->mode, c != nullptr ? c->colour : purple));
            b->setToggleState (v->mode == mode, juce::dontSendNotification);
            const int m = v->mode;
            b->onClick = [this, m] { if (m != mode) setMode (m); };
            b->setTooltip (v->shortText);
            form.addAndMakeVisible (b);
        }
    }

    // ------------------------------------------------------------------ one control of the ORIGINAL page
    struct Control : juce::Component, juce::FileDragAndDropTarget
    {
        HoodPage& page;
        hood::DialogItem item;                 // the engine's own definition
        const hood::Param* fallback = nullptr; // a usage word with no cdparams control (the input files of combine make): a plain field
        juce::Label name, plain, loLab, hiLab, rangeLab;
        juce::Slider slider;
        juce::TextEditor ed;
        juce::ToggleButton check, brk, full, on, choiceA, choiceB;
        juce::TextButton def, makeBtn, pickBtn;   // data-file fields: MAKE FROM SOUND (the engine's own extractor) and PICK (a file)
        bool pitchwise = true, updating = false, hasSub = false, busyMaking = false;
        juce::String dataKind;                 // "pitch" / "formant" / "envelope" when the engine can make this file from a sound

        bool isFileField() const { return fallback != nullptr || item.textual(); }
        bool isInterestedInFileDrag (const juce::StringArray&) override { return isFileField(); }
        void filesDropped (const juce::StringArray& files, int, int) override { if (isFileField() && ! files.isEmpty()) { ed.setText (files[0], false); changed(); } }

        Control (HoodPage& p, const hood::DialogItem& it, const hood::Param* fb) : page (p), item (it), fallback (fb)
        {
            const bool isCheck = item.kind == "CHECKBUTTON";
            // our name, big; the engine's own name and its plain words small under it
            name.setText (isCheck ? juce::String() : (fb != nullptr ? hood::friendlyControl (fb->name.toUpperCase()) : hood::friendlyControl (item.name)), juce::dontSendNotification);
            name.setFont (font (15.5f, 1)); name.setColour (juce::Label::textColourId, text);
            name.setMinimumHorizontalScale (0.7f);
            plain.setFont (font (11.0f)); plain.setColour (juce::Label::textColourId, sub.brighter (0.25f)); plain.setMinimumHorizontalScale (1.0f);
            plain.setJustificationType (juce::Justification::topLeft);
            addAndMakeVisible (name); addAndMakeVisible (plain);
            for (auto* l : { &loLab, &hiLab }) { l->setFont (font (10.5f)); l->setColour (juce::Label::textColourId, sub); addChildComponent (*l); }
            rangeLab.setFont (font (11.0f)); rangeLab.setColour (juce::Label::textColourId, sub); addChildComponent (rangeLab);
            loLab.setJustificationType (juce::Justification::centredLeft); hiLab.setJustificationType (juce::Justification::centredRight);

            slider.setSliderStyle (juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
            slider.setColour (juce::Slider::trackColourId, page.accent);
            slider.setColour (juce::Slider::backgroundColourId, night0);
            slider.setColour (juce::Slider::thumbColourId, text);
            slider.onValueChange = [this] { if (! updating) { updating = true; ed.setText (hood::formatValue (slider.getValue(), item.integer()), false); updating = false; changed(); } };
            addChildComponent (slider);

            page.styleEditor (ed, false);
            ed.setJustification (juce::Justification::centred);
            ed.onTextChange = [this] { if (! updating) { updating = true; syncSliderFromText(); updating = false; changed(); } };
            ed.onReturnKey = [this] { ed.giveAwayKeyboardFocus(); };
            addChildComponent (ed);

            for (auto* t : { &check, &brk, &full, &on, &choiceA, &choiceB })
            {
                t->setColour (juce::ToggleButton::tickColourId, green);
                t->setColour (juce::ToggleButton::textColourId, text);
                addChildComponent (*t);
            }
            check.setButtonText (hood::friendlyControl (item.name)); check.onClick = [this] { changed(); };
            brk.setButtonText ("OVER TIME"); brk.setTooltip ("Make this change over time: type time / value pairs (0 1.5  2 0.7) or give a file");
            brk.onClick = [this] { layoutSelf(); if (! brk.getToggleState()) { syncSliderFromText(); } changed(); };
            full.setButtonText ("WIDER RANGE"); full.setTooltip ("The slider shows the usual range; WIDER RANGE opens it to everything the engine accepts");
            full.onClick = [this] { applyRange(); };
            on.setButtonText ("USE"); on.setTooltip ("An optional setting: ticked, this value is used; unticked, the engine does its own default thing");
            on.onClick = [this] { changed(); };
            choiceA.setButtonText (hood::friendlyControl (item.name2).toUpperCase()); choiceB.setButtonText (hood::friendlyControl (item.name3).toUpperCase());
            choiceA.setRadioGroupId (77); choiceB.setRadioGroupId (77);
            choiceA.onClick = [this] { pitchwise = true; applyRange(); changed(); };
            choiceB.onClick = [this] { pitchwise = false; applyRange(); changed(); };
            def.setButtonText ("RESET"); def.setTooltip ("Back to the engine's own default for this control");
            def.setColour (juce::TextButton::buttonColourId, night2); def.setColour (juce::TextButton::textColourOffId, sub.brighter (0.3f));
            def.onClick = [this] { setToDefault(); };
            addChildComponent (def);
            dataKind = hood::dataKindFor (fb != nullptr ? fb->name : item.param);
            makeBtn.setButtonText ("MAKE FROM SOUND"); makeBtn.setColour (juce::TextButton::buttonColourId, green.withAlpha (0.25f));
            makeBtn.setTooltip ("Runs the engine's own program that makes this data from a sound (" + dataKind + ") and fills this in");
            makeBtn.onClick = [this] { makeMenu(); };
            pickBtn.setButtonText ("PICK"); pickBtn.setTooltip ("A file made earlier in this session, or any file on disk");
            pickBtn.onClick = [this] { pickMenu(); };
            addChildComponent (makeBtn); addChildComponent (pickBtn);
            if (isFileField()) { pickBtn.setVisible (true); makeBtn.setVisible (dataKind.isNotEmpty()); }

            if (fallback != nullptr)
            {
                ed.setJustification (juce::Justification::centredLeft);
                ed.setTextToShowWhenEmpty (fallback->kind == "datafile" ? "type the numbers (see ENGINE ROOM), drop a file here, or MAKE / PICK one" : "needed by the engine", dim);
                ed.setVisible (true);
                return;
            }
            if (isCheck) { check.setVisible (true); return; }
            if (item.textual())
            {
                ed.setJustification (juce::Justification::centredLeft);
                ed.setTextToShowWhenEmpty (item.kind == "FILENAME" ? "type the data, one line per entry (see ENGINE ROOM), or drop a file here" : "the text the ENGINE ROOM describes", dim);
                ed.setVisible (true);
                if (item.kind == "FILENAME" && item.ranges > 0)
                    rangeLab.setText ("values " + hood::formatValue (item.lo, false) + " to " + hood::formatValue (item.hi, false)
                                      + (item.ranges > 1 ? " and " + hood::formatValue (item.lo2, false) + " to " + hood::formatValue (item.hi2, false) : juce::String()), juce::dontSendNotification);
                rangeLab.setVisible (item.ranges > 0);
                return;
            }
            ed.setVisible (true);
            def.setVisible (true);
            if (item.kind == "SWITCHED") { choiceA.setVisible (true); choiceB.setVisible (true); choiceA.setToggleState (true, juce::dontSendNotification); }
            if (item.slider()) { slider.setVisible (true); loLab.setVisible (true); hiLab.setVisible (true); }
            else rangeLab.setVisible (true);
            if (item.varying()) brk.setVisible (true);
            hasSub = item.slider() && item.kind != "SWITCHED" && (item.sublo > item.lo + 1e-9 || item.subhi < item.hi - 1e-9);
            full.setVisible (hasSub);
            if (item.optional && item.flag.isNotEmpty() && item.kind != "SWITCHED") on.setVisible (true);
            applyRange();
        }

        juce::String key() const { return fallback != nullptr ? fallback->name : item.param; }

        // MAKE FROM SOUND: which sound; then the engine's extractor runs off the message thread and the path lands here
        void makeMenu()
        {
            juce::PopupMenu m;
            m.addSectionHeader ("MAKE " + dataKind.toUpperCase() + " DATA FROM");
            m.addItem (1, "the current sound", page.engine.hasSpecimen());
            m.addItem (2, "SPECIMEN 002", page.engine.hasPartner());
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (makeBtn), [this] (int r)
            {
                if (r == 1) makeFrom (page.engine.processedAudio());
                else if (r == 2) makeFrom (page.engine.partnerAudio());
            });
        }
        void makeFrom (AudioPtr a)
        {
            if (a == nullptr || busyMaking || page.engine.cdp == nullptr) return;
            busyMaking = true;
            makeBtn.setButtonText ("MAKING..."); makeBtn.setEnabled (false);
            const auto kind = dataKind, points = page.pointsEd.getText().trim(), overlap = page.overlapEd.getText().trim();
            const auto dataDir = page.dataFolder();
            const Cdp* cdp = page.engine.cdp.get();
            juce::Component::SafePointer<Control> self (this);
            juce::Thread::launch ([self, a, kind, points, overlap, dataDir, cdp]
            {
                auto made = hood::makeData (*cdp, *a, kind, points.isEmpty() ? "1024" : points, overlap.isEmpty() ? "3" : overlap, dataDir);
                juce::MessageManager::callAsync ([self, made]
                {
                    if (self == nullptr) return;
                    self->busyMaking = false;
                    self->makeBtn.setButtonText ("MAKE FROM SOUND"); self->makeBtn.setEnabled (true);
                    if (made.error.isNotEmpty()) { self->page.showRan (made.commands.joinIntoString ("\n") + "\n" + made.error, false); return; }
                    self->ed.setText (made.file.getFullPathName(), false);
                    self->changed();
                    self->page.showRan ("MADE " + self->dataKind + " data: " + made.file.getFullPathName() + "\n" + made.commands.joinIntoString ("\n"), true);
                });
            });
        }
        void makeNow (AudioPtr a)   // the same, on this thread (tests)
        {
            if (a == nullptr || page.engine.cdp == nullptr) return;
            auto made = hood::makeData (*page.engine.cdp, *a, dataKind, page.pointsEd.getText().trim(), page.overlapEd.getText().trim(), page.dataFolder());
            if (made.error.isEmpty()) { ed.setText (made.file.getFullPathName(), false); changed(); }
            page.showRan ((made.error.isEmpty() ? "MADE " + dataKind + " data: " + made.file.getFullPathName() : made.error) + "\n" + made.commands.joinIntoString ("\n"), made.error.isEmpty());
        }
        void pickMenu()
        {
            juce::PopupMenu m;
            auto files = page.dataFolder().findChildFiles (juce::File::findFiles, false);
            files.sort();
            int id = 100;
            if (! files.isEmpty()) m.addSectionHeader ("MADE IN THIS SESSION");
            for (auto& f : files) m.addItem (id++, f.getFileName());
            m.addSeparator();
            m.addItem (1, "Browse for a file...");
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (pickBtn), [this, files] (int r)
            {
                if (r >= 100 && r - 100 < files.size()) { ed.setText (files[r - 100].getFullPathName(), false); changed(); }
                else if (r == 1)
                {
                    chooser = std::make_unique<juce::FileChooser> ("Pick a file for " + (fallback != nullptr ? fallback->name : item.name), page.dataFolder());
                    juce::Component::SafePointer<Control> self (this);
                    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [self] (const juce::FileChooser& fc)
                    {
                        if (self == nullptr || fc.getResult() == juce::File()) return;
                        self->ed.setText (fc.getResult().getFullPathName(), false); self->changed();
                    });
                }
            });
        }
        std::unique_ptr<juce::FileChooser> chooser;
        int height() const
        {
            if (item.kind == "CHECKBUTTON") return kCheckH;
            if (page.compact)   // phone-narrow: the name on top, the controls under it
                return (fallback != nullptr || item.textual()) ? 160 : (item.kind == "SWITCHED" ? 176 : 148);
            if (fallback != nullptr || item.textual()) return kTextH;
            return kRowH;
        }

        // the slider's range: the engine's starting sub-range, or everything it accepts
        void applyRange()
        {
            double lo = item.sublo, hi = item.subhi;
            if (item.kind == "SWITCHED") { lo = pitchwise ? item.lo : item.lo2; hi = pitchwise ? item.hi : item.hi2; }
            else if (full.getToggleState() || ! hasSub) { lo = item.lo; hi = item.hi; }
            if (hi <= lo) hi = lo + 1.0;
            const bool isInt = item.integer();
            if (item.kind == "POWTWO")
            {
                const double klo = std::round (std::log2 (juce::jmax (1.0, lo))), khi = std::round (std::log2 (juce::jmax (2.0, hi)));
                juce::NormalisableRange<double> r (lo, hi,
                    [klo, khi] (double, double, double n) { return std::pow (2.0, std::round (klo + n * (khi - klo))); },
                    [klo, khi] (double, double, double v) { return juce::jlimit (0.0, 1.0, (std::log2 (juce::jmax (1.0, v)) - klo) / juce::jmax (1.0, khi - klo)); },
                    [] (double, double, double v) { return std::pow (2.0, std::round (std::log2 (juce::jmax (1.0, v)))); });
                slider.setNormalisableRange (r);
            }
            else if (item.logarithmic() && lo > 0)
            {
                const double a = std::log (lo), b = std::log (hi);
                juce::NormalisableRange<double> r (lo, hi,
                    [a, b, isInt] (double, double, double n) { const double v = std::exp (a + n * (b - a)); return isInt ? std::round (v) : v; },
                    [a, b] (double, double, double v) { return juce::jlimit (0.0, 1.0, (std::log (juce::jmax (1e-12, v)) - a) / (b - a)); },
                    [isInt] (double, double, double v) { return isInt ? std::round (v) : v; });
                slider.setNormalisableRange (r);
            }
            else slider.setNormalisableRange ({ lo, hi, isInt ? 1.0 : 0.0 });
            loLab.setText (hood::formatValue (lo, false), juce::dontSendNotification);
            hiLab.setText (hood::formatValue (hi, false), juce::dontSendNotification);
            rangeLab.setText ("range " + hood::formatValue (lo, false) + " to " + hood::formatValue (hi, false), juce::dontSendNotification);
            updating = true; syncSliderFromText(); updating = false;
        }
        void syncSliderFromText()
        {
            if (! slider.isVisible()) return;
            const auto t = ed.getText().trim();
            if (t.isEmpty() || t.containsAnyOf (" \t")) return;
            const double v = t.getDoubleValue();
            // a typed value outside the starting range opens the slider to the full range (the engine accepts it)
            if (hasSub && ! full.getToggleState() && (v < slider.getMinimum() - 1e-9 || v > slider.getMaximum() + 1e-9) && v >= item.lo && v <= item.hi)
            { full.setToggleState (true, juce::dontSendNotification); applyRange(); return; }
            slider.setValue (juce::jlimit (slider.getMinimum(), slider.getMaximum(), v), juce::dontSendNotification);
        }
        void setToDefault()
        {
            if (item.kind == "SWITCHED") { pitchwise = true; choiceA.setToggleState (true, juce::dontSendNotification); applyRange(); }
            brk.setToggleState (false, juce::dontSendNotification);
            if (on.isVisible()) on.setToggleState (false, juce::dontSendNotification);
            layoutSelf();
            updating = true;
            ed.setText (hood::formatValue (item.dflt, item.integer()), false);
            syncSliderFromText();
            updating = false;
            changed();
        }

        // ---- the value as the command gets it
        void set (const juce::String& v)
        {
            updating = true;
            if (item.kind == "CHECKBUTTON") check.setToggleState (v.trim() == "1", juce::dontSendNotification);
            else if (item.kind == "SWITCHED")
            {
                pitchwise = ! v.trim().startsWith ("-f");
                choiceA.setToggleState (pitchwise, juce::dontSendNotification); choiceB.setToggleState (! pitchwise, juce::dontSendNotification);
                applyRange();
                ed.setText (v.trim().substring (2), false);
                syncSliderFromText();
            }
            else
            {
                const bool varyingText = item.varying() && brk.isVisible() && (v.containsAnyOf (" \t\r\n") || juce::File::isAbsolutePath (v.trim()));
                if (brk.getToggleState() != varyingText) { brk.setToggleState (varyingText, juce::dontSendNotification); layoutSelf(); }
                // an optional flag left empty: not sent; the box shows the engine's own default so the number is there to send
                if (on.isVisible()) { on.setToggleState (v.trim().isNotEmpty(), juce::dontSendNotification); ed.setText (v.trim().isNotEmpty() ? v : hood::formatValue (item.dflt, item.integer()), false); }
                else ed.setText (v, false);
                syncSliderFromText();
            }
            updating = false;
        }
        juce::String get() const
        {
            if (item.kind == "CHECKBUTTON") return check.getToggleState() ? "1" : "0";
            if (item.kind == "SWITCHED") return (pitchwise ? "-p" : "-f") + ed.getText().trim();
            if (on.isVisible() && ! on.getToggleState()) return {};
            return ed.getText();
        }
        void changed() { page.rememberValues(); page.updateCommand(); }

        void resized() override { layoutSelf(); }
        void layoutSelf()
        {
            auto r = getLocalBounds().withTrimmedLeft (16).withTrimmedRight (10);
            if (item.kind == "CHECKBUTTON")
            {
                const int cw = juce::jmin (r.getWidth() / 2, 48 + (int) juce::GlyphArrangement::getStringWidth (font (14.0f, 1), check.getButtonText()));
                check.setBounds (r.removeFromLeft (cw).reduced (0, 8));
                plain.setBounds (r.reduced (8, 12));
                return;
            }
            if (page.compact) { layoutCompact (r); return; }
            const int nameW = juce::jmin (kNameW, r.getWidth() * 2 / 5);
            auto nameCol = r.removeFromLeft (nameW);
            name.setBounds (nameCol.getX(), r.getY() + 8, nameCol.getWidth(), 24);
            plain.setBounds (nameCol.getX(), r.getY() + 32, nameCol.getWidth(), juce::jmax (14, getHeight() - 38));
            r.removeFromLeft (14);
            if (fallback != nullptr || item.textual())
            {
                auto row = r.withHeight (34).withY (r.getY() + 10);
                if (pickBtn.isVisible()) { pickBtn.setBounds (row.removeFromRight (70)); row.removeFromRight (8); }
                if (makeBtn.isVisible()) { makeBtn.setBounds (row.removeFromRight (160)); row.removeFromRight (8); }
                ed.setBounds (row);
                rangeLab.setBounds (r.withTop (row.getBottom() + 4).withHeight (18));
                return;
            }
            // right side: the value, RESET, and the extras this control has (USE, CHANGES OVER TIME, WIDER RANGE)
            auto right = r.removeFromRight (juce::jmin (330, r.getWidth() / 2));
            auto row = right.withHeight (34).withY (r.getY() + 8);
            ed.setBounds (row.removeFromLeft (100));
            row.removeFromLeft (8);
            def.setBounds (row.removeFromLeft (72).reduced (0, 3));
            row.removeFromLeft (10);
            if (on.isVisible()) { on.setBounds (row.removeFromLeft (70)); row.removeFromLeft (4); }
            auto under = right.withTop (row.getBottom() + 4).withHeight (22);
            const bool varyingOn = brk.isVisible() && brk.getToggleState();
            full.setVisible (hasSub && ! varyingOn);
            if (brk.isVisible() && ! varyingOn) { brk.setBounds (under.removeFromLeft (124)); under.removeFromLeft (8); }
            if (full.isVisible()) full.setBounds (under.removeFromLeft (juce::jmin (150, under.getWidth())));
            r.removeFromRight (16);
            if (item.kind == "SWITCHED")
            {
                auto ch = r.removeFromLeft (juce::jmin (190, r.getWidth() / 2));
                choiceA.setBounds (ch.withHeight (24).withY (r.getY() + 8));
                choiceB.setBounds (ch.withHeight (24).withY (r.getY() + 36));
                r.removeFromLeft (8);
            }
            if (varyingOn)
            {
                // the time / value pairs replace the slider: type them, or the path of a file
                slider.setVisible (false); loLab.setVisible (false); hiLab.setVisible (false);
                ed.setBounds (r.withHeight (34).withY (r.getY() + 8).withRight (right.getX() - 12));
                ed.setJustification (juce::Justification::centredLeft);
                def.setBounds (right.withHeight (28).withY (r.getY() + 11).removeFromLeft (72));
                brk.setBounds (right.withHeight (26).withY (r.getY() + 12).withX (right.getX() + 82).withWidth (juce::jmin (124, right.getWidth() - 82)));
                rangeLab.setText ("time value pairs (0 1.5  2 0.7), or a file path", juce::dontSendNotification); rangeLab.setVisible (true);
                rangeLab.setBounds (r.withTop (r.getY() + 46).withHeight (18));
                return;
            }
            ed.setJustification (juce::Justification::centred);
            if (item.slider())
            {
                slider.setVisible (true); loLab.setVisible (true); hiLab.setVisible (true); rangeLab.setVisible (false);
                slider.setBounds (r.withHeight (32).withY (r.getY() + 9));
                loLab.setBounds (r.withTop (r.getY() + 42).withHeight (18).removeFromLeft (r.getWidth() / 2));
                hiLab.setBounds (r.withTop (r.getY() + 42).withHeight (18).removeFromRight (r.getWidth() / 2));
            }
            else
            {
                rangeLab.setText ("range " + hood::formatValue (slider.getMinimum(), false) + " to " + hood::formatValue (slider.getMaximum(), false), juce::dontSendNotification);
                rangeLab.setVisible (true);
                rangeLab.setBounds (r.withHeight (34).withY (r.getY() + 8));
            }
        }
        void layoutCompact (juce::Rectangle<int> r)
        {
            name.setBounds (r.getX(), 6, r.getWidth(), 22);
            plain.setBounds (r.getX(), 30, r.getWidth(), 28);
            if (fallback != nullptr || item.textual())
            {
                ed.setBounds (r.getX(), 62, r.getWidth(), 34);
                auto row = juce::Rectangle<int> (r.getX(), 100, r.getWidth(), 32);
                if (makeBtn.isVisible()) { makeBtn.setBounds (row.removeFromLeft (160)); row.removeFromLeft (8); }
                if (pickBtn.isVisible()) pickBtn.setBounds (row.removeFromLeft (70));
                rangeLab.setBounds (r.getX(), 136, r.getWidth(), 18);
                return;
            }
            const bool varyingOn = brk.isVisible() && brk.getToggleState();
            auto row = juce::Rectangle<int> (r.getX(), 62, r.getWidth(), 34);
            def.setBounds (row.removeFromRight (64).reduced (0, 3)); row.removeFromRight (8);
            if (varyingOn)
            {
                slider.setVisible (false); loLab.setVisible (false); hiLab.setVisible (false);
                ed.setBounds (row); ed.setJustification (juce::Justification::centredLeft);
                rangeLab.setText ("time value pairs (0 1.5  2 0.7), or a file path", juce::dontSendNotification); rangeLab.setVisible (true);
                rangeLab.setBounds (r.getX(), 98, r.getWidth(), 16);
            }
            else
            {
                ed.setBounds (row.removeFromRight (84)); row.removeFromRight (10);
                ed.setJustification (juce::Justification::centred);
                if (item.slider())
                {
                    slider.setVisible (true); loLab.setVisible (true); hiLab.setVisible (true); rangeLab.setVisible (false);
                    slider.setBounds (row);
                    loLab.setBounds (row.getX(), 98, row.getWidth() / 2, 16); hiLab.setBounds (row.getX() + row.getWidth() / 2, 98, row.getWidth() - row.getWidth() / 2, 16);
                }
                else
                {
                    rangeLab.setText ("range " + hood::formatValue (slider.getMinimum(), false) + " to " + hood::formatValue (slider.getMaximum(), false), juce::dontSendNotification);
                    rangeLab.setVisible (true); rangeLab.setBounds (row);
                }
            }
            int y = 116;
            if (item.kind == "SWITCHED")
            {
                choiceA.setBounds (r.getX(), y, r.getWidth() / 2 - 4, 24); choiceB.setBounds (r.getX() + r.getWidth() / 2 + 4, y, r.getWidth() / 2 - 4, 24);
                y += 28;
            }
            auto tg = juce::Rectangle<int> (r.getX(), y, r.getWidth(), 24);
            full.setVisible (hasSub && ! varyingOn);
            if (on.isVisible()) { on.setBounds (tg.removeFromLeft (70)); tg.removeFromLeft (8); }
            if (brk.isVisible()) { brk.setBounds (tg.removeFromLeft (124)); tg.removeFromLeft (8); }
            if (full.isVisible()) full.setBounds (tg.removeFromLeft (juce::jmin (150, tg.getWidth())));
        }
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (0, 3);
            g.setColour (night0.withAlpha (0.55f)); g.fillRoundedRectangle (r, 10.0f);
            g.setColour (page.accent.withAlpha (0.18f)); g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
            g.setColour (page.accent.withAlpha (0.8f)); g.fillRoundedRectangle (r.getX() + 3, r.getY() + 10, 3.0f, r.getHeight() - 20, 1.5f);
            if (item.extra)
            {
                g.setColour (dim); g.setFont (font (9.5f));
                g.drawText ("accepted by the engine; not in its usage text", getLocalBounds().removeFromBottom (14).removeFromRight (300).withTrimmedRight (10), juce::Justification::centredRight);
            }
        }
    };

    // ---- the extra sounds of a process that takes several (modify sausage, combine interleave / max, loudness 7 / 8)
    struct SoundsControl : juce::Component, juce::FileDragAndDropTarget
    {
        HoodPage& page;
        juce::Label title, hint;
        juce::TextButton addCurrent, addPartner, addEarlier, addFile;
        juce::OwnedArray<juce::TextButton> removes;
        std::unique_ptr<juce::FileChooser> chooser;
        explicit SoundsControl (HoodPage& p) : page (p)
        {
            title.setText ("MORE SOUNDS", juce::dontSendNotification); title.setFont (font (15.0f, 1)); title.setColour (juce::Label::textColourId, text);
            hint.setFont (font (11.0f)); hint.setColour (juce::Label::textColourId, sub.brighter (0.25f));
            hint.setText ("this one uses several sounds: your sound is the first; add the others here (or drop files on this box)", juce::dontSendNotification);
            addAndMakeVisible (title); addAndMakeVisible (hint);
            for (auto* b : { &addCurrent, &addPartner, &addEarlier, &addFile }) { b->setColour (juce::TextButton::buttonColourId, panelHi); b->setColour (juce::TextButton::textColourOffId, text); addAndMakeVisible (*b); }
            addCurrent.setButtonText ("+ CURRENT SOUND"); addPartner.setButtonText ("+ SPECIMEN 002"); addEarlier.setButtonText ("+ EARLIER RESULT"); addFile.setButtonText ("+ FILE");
            addCurrent.onClick = [this] { addAudio (page.engine.processedAudio(), "current"); };
            addPartner.onClick = [this] { addAudio (page.engine.partnerAudio(), "specimen-002"); };
            addEarlier.onClick = [this]
            {
                juce::PopupMenu m;
                for (int i = 0; i < page.engine.historySize(); ++i) m.addItem (i + 1, juce::String (i) + "  " + page.engine.snapshot (i).label);
                m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (addEarlier), [this] (int r) { if (r > 0) addAudio (page.engine.snapshot (r - 1).audio, "result-" + juce::String (r - 1)); });
            };
            addFile.onClick = [this]
            {
                chooser = std::make_unique<juce::FileChooser> ("Add sounds", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
                juce::Component::SafePointer<SoundsControl> self (this);
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                                      [self] (const juce::FileChooser& fc) { if (self != nullptr) for (auto& f : fc.getResults()) self->addPath (f.getFullPathName()); });
            };
        }
        bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
        void filesDropped (const juce::StringArray& files, int, int) override { for (auto& f : files) if (canLoad (juce::File (f))) addPath (f); }
        void addAudio (AudioPtr a, const juce::String& stem)
        {
            if (a == nullptr) return;
            auto f = hood::writeSoundFile (*a, page.dataFolder(), stem);
            if (f != juce::File()) addPath (f.getFullPathName());
        }
        void addPath (const juce::String& p) { page.extraFiles.add (p); rebuild(); page.updateCommand(); }
        void rebuild()
        {
            removes.clear();
            for (int i = 0; i < page.extraFiles.size(); ++i)
            {
                auto* b = removes.add (new juce::TextButton ("x"));
                b->setColour (juce::TextButton::buttonColourId, panelHi); b->setColour (juce::TextButton::textColourOffId, red);
                b->onClick = [this, i] { page.extraFiles.remove (i); rebuild(); page.updateCommand(); };
                addAndMakeVisible (*b);
            }
            page.layoutForm (page.formWidth());
            resized(); repaint();
        }
        int height() const { return 84 + 28 * page.extraFiles.size(); }
        void resized() override
        {
            auto r = getLocalBounds().reduced (14, 6);
            title.setBounds (r.removeFromTop (24));
            hint.setBounds (r.removeFromTop (18));
            auto row = r.removeFromTop (28);
            addCurrent.setBounds (row.removeFromLeft (150)); row.removeFromLeft (6);
            addPartner.setBounds (row.removeFromLeft (140)); row.removeFromLeft (6);
            addEarlier.setBounds (row.removeFromLeft (150)); row.removeFromLeft (6);
            addFile.setBounds (row.removeFromLeft (80));
            for (int i = 0; i < removes.size(); ++i) removes[i]->setBounds (r.getX(), r.getY() + 4 + i * 28, 22, 22);
        }
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (0, 3);
            g.setColour (night0.withAlpha (0.55f)); g.fillRoundedRectangle (r, 10.0f);
            g.setColour (page.accent.withAlpha (0.25f)); g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
            g.setColour (text.withAlpha (0.85f)); g.setFont (font (12.5f));
            for (int i = 0; i < page.extraFiles.size(); ++i)
                g.drawText (juce::String (i + 2) + ".  " + juce::File (page.extraFiles[i]).getFileName(), 44, 86 + i * 28, getWidth() - 56, 22, juce::Justification::centredLeft);
        }
    };


    // ---- the list: every process + version by our names, grouped by what it does
    void rebuildRows()
    {
        rows.clear();
        const auto q = search.getText().trim().toLowerCase();
        auto hit = [&q] (const hood::Entry& e)
        {
            if (q.isEmpty()) return true;
            if (e.title.toLowerCase().contains (q) || e.shortText.toLowerCase().contains (q) || e.about.toLowerCase().contains (q)) return true;
            if (e.proc == nullptr) return false;
            if (e.proc->id().toLowerCase().contains (q) || e.proc->title.toLowerCase().contains (q)) return true;
            if (const auto* f = hood::formFor (*e.proc, e.mode))
                for (auto& pr : f->params) if (pr.name.toLowerCase().contains (q)) return true;
            if (const auto* d = hood::dialogFor (e.prog, e.name, e.mode))
                for (auto& it : d->items) if (it.name.toLowerCase().contains (q) || hood::friendlyControl (it.name).toLowerCase().contains (q)) return true;
            return false;
        };
        for (auto& c : hood::categories())
        {
            if (cat != "all" && cat != c.id) continue;
            std::vector<const hood::Entry*> mine;
            for (auto& e : hood::entries()) if (e.cat == c.id && hit (e)) mine.push_back (&e);
            if (mine.empty()) continue;
            if (cat == "all" || q.isNotEmpty()) rows.push_back ({ &c, nullptr });
            for (auto* e : mine) rows.push_back ({ nullptr, e });
        }
        list.updateContent();
        selectRowForCurrent();
        list.repaint();
    }
    void selectRowForCurrent()
    {
        if (proc == nullptr) return;
        for (int i = 0; i < (int) rows.size(); ++i)
            if (rows[(size_t) i].entry != nullptr && rows[(size_t) i].entry->proc == proc && rows[(size_t) i].entry->mode == mode)
            { list.selectRow (i, false, true); return; }
        list.deselectAllRows();
    }
    int getNumRows() override { return (int) rows.size(); }
    void paintListBoxItem (int i, juce::Graphics& g, int w, int h, bool sel) override
    {
        if (i < 0 || i >= (int) rows.size()) return;
        auto& r = rows[(size_t) i];
        if (r.header != nullptr)
        {
            g.setColour (r.header->colour); g.fillEllipse (8.0f, (float) h * 0.5f - 12.0f, 9.0f, 9.0f);
            g.setFont (font (14.0f, 2, 0.12f));
            g.drawText (r.header->title, 24, 6, w - 28, h / 2 - 2, juce::Justification::bottomLeft);
            g.setColour (sub); g.setFont (font (11.0f));
            g.drawText (r.header->blurb, 24, h / 2 + 2, w - 28, h / 2 - 6, juce::Justification::topLeft);
            g.setColour (r.header->colour.withAlpha (0.3f)); g.fillRect (8, h - 2, w - 16, 1);
            return;
        }
        const auto* c = hood::category (r.entry->cat);
        const auto col = c != nullptr ? c->colour : purple;
        auto box = juce::Rectangle<float> (2, 3, (float) w - 4, (float) h - 6);
        if (sel)
        {
            g.setColour (col.withAlpha (0.2f)); g.fillRoundedRectangle (box, 8.0f);
            g.setColour (col.withAlpha (0.9f)); g.drawRoundedRectangle (box, 8.0f, 1.3f);
        }
        g.setColour (col.withAlpha (sel ? 1.0f : 0.7f)); g.fillRoundedRectangle (box.getX() + 6, box.getY() + 9, 3.0f, box.getHeight() - 18, 1.5f);
        g.setColour (text); g.setFont (font (14.5f, 1));
        auto t = r.entry->title;
        if (r.entry->inputs >= 2 || r.entry->multi) t << "  +";
        g.drawFittedText (t, 18, 6, w - 26, 22, juce::Justification::centredLeft, 1, 0.8f);
        g.setColour (sub.brighter (0.2f)); g.setFont (font (11.0f));
        g.drawFittedText (r.entry->shortText, 18, 27, w - 26, 18, juce::Justification::centredLeft, 1, 0.8f);
    }
    void listBoxItemClicked (int i, const juce::MouseEvent&) override
    {
        if (i < 0 || i >= (int) rows.size() || rows[(size_t) i].entry == nullptr) return;
        auto* e = rows[(size_t) i].entry;
        if (proc == nullptr || e->prog != proc->prog || e->name != proc->name) selectProcess (e->prog, e->name);
        if (e->mode != mode) setMode (e->mode);
    }

    // ---- the page
    void rememberValues()
    {
        for (auto& c : controls) values[c->key()] = c->get();
    }
    // the engine's own definition for this process + mode, asked for THIS sound when the engine's cdparams can run
    void refreshDialog()
    {
        if (proc == nullptr) return;
        const auto* baked = hood::dialogFor (proc->prog, proc->name, mode);
        if (baked == nullptr) { dialog.reset(); live = false; return; }
        std::unique_ptr<hood::Dialog> d;
        if (engine.cdp != nullptr && engine.hasSpecimen()) d = hood::liveDialog (*engine.cdp, *baked, soundInfo());
        live = d != nullptr;
        if (d == nullptr) d = std::make_unique<hood::Dialog> (*baked);
        dialog = std::move (d);
        // the ranges on the controls follow
        for (auto& c : controls)
            for (auto& it : dialog->items)
                if (c->fallback == nullptr && it.param == c->item.param && it.name == c->item.name) { c->item = it; c->applyRange(); }
        form.repaint();
    }
    void buildControls()
    {
        controls.clear();
        dialog.reset();
        if (proc == nullptr) return;
        entry = hood::entryFor (proc->prog, proc->name, mode);
        if (const auto* c = entry != nullptr ? hood::category (entry->cat) : nullptr) accent = c->colour;
        const auto* f = hood::formFor (*proc, mode);
        if (f == nullptr) return;
        refreshDialog();
        std::map<juce::String, juce::String> start;
        if (dialog != nullptr) start = hood::dialogDefaults (*dialog);
        for (auto& kv : hood::startValues (*f)) if (start.find (kv.first) == start.end()) start[kv.first] = kv.second;
        std::set<juce::String> covered;
        if (dialog != nullptr)
            for (auto& it : dialog->items)
            {
                if (it.param.isEmpty()) continue;
                auto c = std::make_unique<Control> (*this, it, nullptr);
                c->plain.setText (it.name + "   " + hood::plainWords (*proc, *f, it), juce::dontSendNotification);
                covered.insert (it.param);
                form.addAndMakeVisible (*c);
                controls.push_back (std::move (c));
            }
        // a usage word the engine's dialog has no control for (the input data files of combine make): a plain field
        for (auto& p : f->params)
        {
            if (covered.count (p.name) > 0) continue;
            hood::DialogItem it; it.kind = "FILENAME"; it.name = p.name.toUpperCase(); it.param = p.name; it.flag = p.flag; it.optional = p.optional;
            if (p.kind == "switch") { it.kind = "CHECKBUTTON"; it.name = p.flag; }
            auto c = std::make_unique<Control> (*this, it, p.kind == "switch" ? nullptr : &p);
            juce::String h = hood::shownText (p.help).replace ("\n", " ");
            const auto dk = hood::dataKindFor (p.name);
            if (dk.isNotEmpty()) h = dk + " data: MAKE FROM SOUND makes it from a sound, or PICK a file.  " + h;
            else if (p.kind == "datafile") h = "a data file: type its numbers, or the path of a file.  " + h;
            c->plain.setText (h.substring (0, 120), juce::dontSendNotification);
            form.addAndMakeVisible (*c);
            controls.push_back (std::move (c));
        }
        for (auto& c : controls)
        {
            auto it = values.find (c->key());
            if (it != values.end()) c->set (it->second);
            else { auto s = start.find (c->key()); c->set (s != start.end() ? s->second : juce::String()); }
        }
        sounds.reset();
        if (f->multi) { sounds = std::make_unique<SoundsControl> (*this); form.addAndMakeVisible (*sounds); sounds->rebuild(); }
        rememberValues();
        layoutForm (formWidth());
        repaint();
    }
    // the words at the top of the page: how tall they are at this width
    juce::TextLayout aboutLayout (int w) const
    {
        juce::AttributedString a;
        a.append (entry != nullptr ? entry->about : juce::String(), font (12.5f), theme::text.withAlpha (0.8f));
        a.setLineSpacing (2.0f);
        juce::TextLayout tl; tl.createLayout (a, (float) w);
        return tl;
    }
    void layoutForm (int w)
    {
        if (w <= 0) return;
        compact = w < 560;
        int y = 6;
        const bool hasAbout = entry != nullptr && entry->about.isNotEmpty();
        aboutH = hasAbout ? juce::jmin (aboutOpen ? 400 : 54, (int) std::ceil (aboutLayout (w - 16).getHeight()) + 4) : 0;
        aboutR = hasAbout ? juce::Rectangle<int> (8, y, w - 16, aboutH) : juce::Rectangle<int>();
        moreBtn.setVisible (hasAbout && (aboutOpen || aboutLayout (w - 16).getHeight() > 56.0f));
        if (hasAbout) { y += aboutH + 2; if (moreBtn.isVisible()) { moreBtn.setBounds (4, y, 130, 28); y += 30; } y += 8; }
        // the versions: buttons in rows, the word VERSIONS first
        versionsR = {};
        if (! versions.isEmpty())
        {
            versionsR = { 8, y, 90, 30 };
            int x = 98;
            for (auto* b : versions)
            {
                const int bw = juce::jlimit (100, 260, (int) juce::GlyphArrangement::getStringWidth (font (12.5f, 1, 0.04f), b->getButtonText()) + 32);
                if (x + bw > w) { x = 98; y += 36; }
                b->setBounds (x, y, bw, 30);
                x += bw + 8;
            }
            y += 42;
        }
        // needs a second sound?
        needsR = {};
        if (entry != nullptr && (entry->inputs >= 2 || entry->multi))
        {
            needsR = { 0, y, w, 26 };
            y += 30;
        }
        settingsR = { 0, y, w, 24 };
        y += 26;
        controlsTop = y;
        if (sounds != nullptr) { sounds->setBounds (0, y, w, sounds->height()); y += sounds->height() + 4; }
        for (auto& c : controls)
        {
            const int h = c->height();
            c->setBounds (0, y, w, h);
            y += h + 4;
        }
        if (controls.empty() && sounds == nullptr) y += 40;   // room for the "no settings" line
        y += 8;
        // the ENGINE ROOM: the analysis, the engine's own information, the exact command
        engineR = {};
        const bool spectral = proc != nullptr && hood::Request { proc->prog, proc->name }.spectral();
        pointsEd.setVisible (showInfo && spectral); overlapEd.setVisible (showInfo && spectral);
        usageView.setVisible (showInfo); commandView.setVisible (showInfo);
        analysisRow = {};
        if (showInfo)
        {
            const int top = y;
            y += 30;
            if (spectral)
            {
                analysisRow = { 0, y, w, 38 };
                pointsEd.setBounds (200, y + 2, 90, 34); overlapEd.setBounds (400, y + 2, 70, 34);
                y += 46;
            }
            usageView.setBounds (8, y, w - 16, 170);
            y += 178;
            commandView.setBounds (8, y, w - 16, 96);
            y += 104;
            engineR = { 0, top, w, y - top };
            y += 8;
        }
        form.setSize (w, y);
        form.repaint();
    }
    void updateCommand()
    {
        if (proc == nullptr) return;
        auto r = request();
        const auto* f = hood::formFor (*proc, mode);
        if (f == nullptr) return;
        juce::StringArray words, ins;
        for (int i = 0; i < f->inputs; ++i) ins.add (i == 0 ? "infile" : "infile" + juce::String (i + 1));
        if (f->multi) for (int i = 0; i < r.extraFiles.size(); ++i) ins.add ("infile" + juce::String (f->inputs + i + 1));
        juce::String err;
        const bool ok = hood::buildWords (*proc, *f, r, ins, f->out == "none" ? juce::String() : (f->out == "text" ? "outtextfile" : f->out == "data" ? "outfile" + f->outExt : "outfile"),
                                          [] (const juce::String&) { return juce::String ("datafile.txt"); }, words, err);
        juce::String t = "WILL RUN:\n";
        if (hood::Request { r.prog, r.name }.spectral()) t << "pvoc anal 1 infile infile.ana -c" << r.points << " -o" << r.overlap << "\n";
        t << (ok ? words.joinIntoString (" ") : err);
        if (hood::Request { r.prog, r.name }.spectral() && f->out == "sound") t << "\npvoc synth outfile.ana outfile.wav";
        commandView.setText (t, false);
    }

    struct FormComp : juce::Component
    {
        HoodPage& owner;
        explicit FormComp (HoodPage& o) : owner (o) {}
        void paint (juce::Graphics& g) override
        {
            if (owner.proc == nullptr) return;
            const auto* e = owner.entry;
            if (! owner.aboutR.isEmpty())
            {
                juce::Graphics::ScopedSaveState ss (g);
                g.reduceClipRegion (owner.aboutR);
                owner.aboutLayout (owner.aboutR.getWidth()).draw (g, owner.aboutR.toFloat());
            }
            if (! owner.versionsR.isEmpty())
            {
                g.setColour (sub.brighter (0.2f)); g.setFont (font (11.5f, 2, 0.14f));
                g.drawText ("VERSIONS", owner.versionsR, juce::Justification::centredLeft);
            }
            if (! owner.needsR.isEmpty())
            {
                const bool have = owner.engine.hasPartner();
                g.setColour ((have ? green : yellow).withAlpha (0.14f)); g.fillRoundedRectangle (owner.needsR.toFloat(), 8.0f);
                g.setColour (have ? green : yellow); g.setFont (font (12.5f, 1));
                const bool multi = e != nullptr && e->multi && e->inputs < 2;
                g.drawFittedText (multi ? "Uses several sounds: add them in MORE SOUNDS below."
                                        : (have ? "Uses two sounds: your sound and SPECIMEN 002." : "Uses two sounds: load a + SECOND SOUND first (top left)."),
                                  owner.needsR.reduced (12, 0), juce::Justification::centredLeft, 1, 0.8f);
            }
            g.setColour (sub.brighter (0.2f)); g.setFont (font (11.5f, 2, 0.14f));
            g.drawText (owner.controls.empty() && owner.sounds == nullptr ? juce::String ("SETTINGS") : "SETTINGS  (" + juce::String ((int) owner.controls.size()) + ")",
                        owner.settingsR, juce::Justification::centredLeft);
            if (owner.dialog != nullptr && ! owner.live)
            {
                g.setColour (dim.brighter (0.3f)); g.setFont (font (11.0f));
                g.drawText ("ranges shown for a 10 s sound: drop a sound for its own", owner.settingsR, juce::Justification::centredRight);
            }
            if (owner.controls.empty() && owner.sounds == nullptr)
            {
                g.setColour (text.withAlpha (0.8f)); g.setFont (font (14.0f));
                g.drawText ("Nothing to set: RUN IT runs it as it is.", 0, owner.controlsTop + 6, getWidth(), 28, juce::Justification::centredLeft);
            }
            if (! owner.engineR.isEmpty())
            {
                auto er = owner.engineR.toFloat();
                g.setColour (night0.withAlpha (0.6f)); g.fillRoundedRectangle (er, 10.0f);
                g.setColour (purple.withAlpha (0.4f)); g.drawRoundedRectangle (er.reduced (0.5f), 10.0f, 1.0f);
                g.setColour (purple.brighter (0.35f)); g.setFont (font (12.5f, 2, 0.14f));
                g.drawText ("ENGINE ROOM", er.reduced (12, 0).withHeight (28), juce::Justification::centredLeft);
                g.setColour (sub); g.setFont (font (11.0f));
                g.drawText ("the engine's own words, its analysis settings, and the exact command", er.reduced (12, 0).withHeight (28), juce::Justification::centredRight);
                if (! owner.analysisRow.isEmpty())
                {
                    g.setFont (font (12.0f, 1, 0.08f)); g.setColour (text.withAlpha (0.85f));
                    g.drawText ("ANALYSIS SIZE", 12, owner.analysisRow.getY(), 180, 38, juce::Justification::centredLeft);
                    g.drawText ("OVERLAP", 306, owner.analysisRow.getY(), 90, 38, juce::Justification::centredLeft);
                    g.setFont (font (10.5f)); g.setColour (sub);
                    g.drawFittedText ("1024 / 3 are the engine's defaults. Bigger = finer pitch detail, smoother time detail.",
                                      484, owner.analysisRow.getY(), owner.analysisRow.getWidth() - 492, 38, juce::Justification::centredLeft, 2, 0.85f);
                }
            }
        }
    };

    Engine& engine;
    HoodLook look;
    const hood::Process* proc = nullptr;
    const hood::Entry* entry = nullptr;
    std::unique_ptr<hood::Dialog> dialog;
    int mode = 0, controlsTop = 0, aboutH = 0;
    bool building = false, ranOk = false, live = false, showInfo = false, aboutOpen = false;
    juce::TextButton moreBtn { "READ MORE" };
    juce::String ran, soundKey, cat { "all" };
    std::map<juce::String, juce::String> values;
    std::vector<Row> rows;
    juce::TextEditor search, usageView, commandView, pointsEd, overlapEd;
    juce::ListBox list;
    juce::Viewport viewport;
    FormComp form { *this };
    juce::ComboBox modeBox;
    juce::TextButton runBtn, undoBtn, defaultsBtn, infoBtn;
    juce::OwnedArray<Tab> tabs;
    juce::OwnedArray<Version> versions;
    std::vector<std::unique_ptr<Control>> controls;
    std::unique_ptr<SoundsControl> sounds;
    juce::StringArray extraFiles;
    juce::Rectangle<int> aboutR, versionsR, needsR, settingsR, analysisRow, engineR, listR, pageR, headR, titleR;
};
}
