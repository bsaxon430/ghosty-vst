// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Editor.h"
#include "../ui/Pages.h"

namespace atlas
{
using namespace ui;

// ============================================================================================ the screen at its own size
struct Editor::Impl final : public juce::Component, private juce::Timer, private juce::ChangeListener
{
    // the lettering at the top: the wordmark, the sound's name, one line under it
    struct Masthead final : public juce::Component
    {
        Impl& owner;
        explicit Masthead (Impl& o) : owner (o) { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics& g) override
        {
            const auto& l = look();
            g.setColour (l.text (0.96f));
            g.setFont (fellItalic (30.0f));
            g.drawText ("Ghost Atlas", juce::Rectangle<float> (22.0f, 9.0f, 170.0f, 38.0f), juce::Justification::centredLeft, false);
            g.setColour (l.text (0.98f));
            drawFitted (g, owner.name, { 606.0f, 5.0f, 404.0f, 34.0f }, juce::Justification::centred, fellRoman (29.0f), 0.7f);
            g.setColour (owner.lineLit ? l.lantern : l.text (0.72f));
            drawFitted (g, owner.line, { 586.0f, 36.0f, 444.0f, 18.0f }, juce::Justification::centred, sans (13.5f), 0.75f);
        }
    };

    explicit Impl (Processor& p) : proc (p), shell (p), masthead (*this)
    {
        setSize (kBaseW, kBaseH);
        shell.canvas = this;
        shell.goTo = [this] (int pg) { goTo (pg); };
        shell.showWorld = [this] (int w) { setWorld (w); };
        shell.soundChanged = [this] { refreshAll(); };
        shell.say = [this] (const juce::String& s) { said = s; saidUntil = juce::Time::getMillisecondCounter() + 4200u; };
        shell.pickInstrument = [this] (int layer) { open (makeInstrumentPicker (shell, layer)); };
        shell.savePreset = [this] { open (makeSaveDialog (shell)); };
        onMutate = [this] { open (makeMutatePanel (shell)); };
        stage.onDrawBegins = [this] (bool mostOfIt, float pixelsPerUnit) { drawBegan (mostOfIt, pixelsPerUnit); };

        addAndMakeVisible (stage);
        pages[pagePlay] = makePlayPage (shell);
        pages[pageLayers] = makeLayersPage (shell);
        pages[pageEffects] = makeEffectsPage (shell);
        pages[pageLibrary] = makeLibraryPage (shell);
        for (auto& pg : pages) { addChildComponent (*pg); pg->setBounds (0, 0, kBaseW, kBaseH); }
        addAndMakeVisible (masthead);
        masthead.setBounds (0, 0, kBaseW, 60);

        static const char* tabNames[kNumPages] = { "Play", "Layers", "Effects", "Library" };
        for (int i = 0; i < kNumPages; ++i)
        {
            auto& t = tabs[(size_t) i];
            t.text = tabNames[i]; t.style = TextBtn::quiet; t.caps = true; t.size = 19.0f;
            t.onClick = [this, i] { goTo (i); };
            addAndMakeVisible (t);
            t.setBounds (206 + i * 86, 12, 80, 34);
        }
        prev.draw = [] (juce::Graphics& g, juce::Rectangle<float> r, bool h) { chevron (g, r, 0, look().text (h ? 1.0f : 0.8f), 2.0f); };
        next.draw = [] (juce::Graphics& g, juce::Rectangle<float> r, bool h) { chevron (g, r, 1, look().text (h ? 1.0f : 0.8f), 2.0f); };
        prev.onClick = [this] { step (-1); };
        next.onClick = [this] { step (1); };
        prev.setTooltip ("The sound before"); next.setTooltip ("The next sound");
        fav.draw = [this] (juce::Graphics& g, juce::Rectangle<float> r, bool h) { heart (g, r.withSizeKeepingCentre (16.0f, 16.0f), favourite, favourite ? look().lantern : look().text (h ? 1.0f : 0.75f)); };
        fav.onClick = [this] {
            const auto f = proc.presetFile();
            if (f.isEmpty()) { shell.say ("Save this sound first, then it can be a favourite"); return; }
            { const juce::ScopedLock sl (proc.bankLock()); proc.bank().setFavourite (f, ! favourite); }
            refreshAll();
        };
        fav.setTooltip ("Keep this sound among your favourites");
        play.draw = [] (juce::Graphics& g, juce::Rectangle<float> r, bool h) {
            juce::Path p; const float s = 6.5f, cx = r.getCentreX() + 1.0f, cy = r.getCentreY();
            p.addTriangle (cx - s * 0.8f, cy - s, cx - s * 0.8f, cy + s, cx + s, cy);
            g.setColour (look().text (h ? 1.0f : 0.8f)); g.fillPath (p);
        };
        play.onClick = [this] { proc.engine().audition (auditionKind, 60); auditionKind = (auditionKind + 1) % 3; };
        play.setTooltip ("Play a few notes of this sound");
        for (auto* b : { &prev, &next, &fav, &play }) addAndMakeVisible (*b);
        prev.setBounds (566, 10, 32, 32); next.setBounds (1018, 10, 32, 32); fav.setBounds (1052, 10, 32, 32); play.setBounds (1086, 10, 32, 32);

        save.text = "Save"; save.style = TextBtn::plain;
        save.onClick = [this] { if (shell.savePreset) shell.savePreset(); };
        addAndMakeVisible (save); save.setBounds (1128, 13, 62, 30);
        mutate.text = "Mutate"; mutate.style = TextBtn::lit; mutate.caps = true; mutate.size = 20.0f;
        mutate.onClick = [this] { if (onMutate) onMutate(); };
        addAndMakeVisible (mutate); mutate.setBounds (1200, 10, 104, 36);

        // how much the world moves (kept with the window's other settings); one small button on the Play page steps through it
        motion.style = TextBtn::glass; motion.size = 13.0f;
        motion.setTooltip ("How much the world moves. Click to change it.");
        motion.onClick = [this] {
            int m = stage.motion();
            do { m = (m + 2) % 3; } while (m == Stage::motionFilm && ! stage.filmAvailable());        // film, alive, still, and round again
            setMotion (m);
        };
        addChildComponent (motion); motion.setBounds (1234, 70, 70, 24);
        setMotion ((int) prop (proc.uiState, "motion", (int) Stage::motionFilm));

        tips.setLookAndFeel (&chartLookAndFeel());
        proc.addChangeListener (this);
        const int startPage = juce::jlimit (0, kNumPages - 1, (int) prop (proc.uiState, "page", 0));
        refreshAll();
        goTo (startPage);
        startTimerHz (30);
    }
    ~Impl() override
    {
        stopTimer();
        proc.removeChangeListener (this);
        tips.setLookAndFeel (nullptr);
    }

    // ---- pages and worlds
    Page& page() { return *pages[(size_t) current]; }
    void goTo (int pg)
    {
        pg = juce::jlimit (0, kNumPages - 1, pg);
        for (int i = 0; i < kNumPages; ++i) { pages[(size_t) i]->setVisible (i == pg); tabs[(size_t) i].selected = i == pg; tabs[(size_t) i].repaint(); }
        current = pg;
        motion.setVisible (pg == pagePlay);
        page().refresh();
        setWorld (soundWorld);
        auto o = proc.uiState.getDynamicObject() != nullptr ? juce::DynamicObject::Ptr (proc.uiState.getDynamicObject()) : newObject();
        o->setProperty ("page", pg); proc.uiState = o.get();
    }
    void setMotion (int mode)
    {
        mode = juce::jlimit (0, 2, mode);
        auto o = proc.uiState.getDynamicObject() != nullptr ? juce::DynamicObject::Ptr (proc.uiState.getDynamicObject()) : newObject();
        o->setProperty ("motion", mode); proc.uiState = o.get();
        if (mode == Stage::motionFilm && ! stage.filmAvailable()) mode = Stage::motionAlive;      // (asked for, not there yet: the next best)
        stage.setMotion (mode);
        static const char* names[] = { "Still", "Alive", "Film" };
        motion.text = names[mode]; motion.repaint();
    }
    void setWorld (int w)
    {
        shownWorld = juce::jlimit (0, kWorlds, w);
        const int onStage = page().worldOverride() >= 0 ? page().worldOverride() : shownWorld;
        look() = lookFor (onStage);
        stage.setScene (onStage, page().secondView(), page().panes(), page().open(), page().showsTitle());
        repaint();
    }
    void refreshAll()
    {
        const auto p = proc.capture();
        name = p.name;
        soundWorld = p.worldIndex();
        about = juce::String (p.category) + " in " + world (soundWorld).name + (p.author.isNotEmpty() ? ", by " + p.author : juce::String());
        { const juce::ScopedLock sl (proc.bankLock()); favourite = proc.presetFile().isNotEmpty() && proc.bank().isFavourite (proc.presetFile()); }
        for (auto& pg : pages) pg->refresh();
        if (overlay == nullptr || true) setWorld (soundWorld);
        masthead.repaint(); fav.repaint();
    }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refreshAll(); }

    void step (int by)
    {
        std::vector<PresetInfo> all;
        { const juce::ScopedLock sl (proc.bankLock()); all = proc.bank().list(); }
        if (all.empty()) return;
        int at = -1;
        for (size_t i = 0; i < all.size(); ++i) if (all[i].file == proc.presetFile()) at = (int) i;
        at = at < 0 ? 0 : (at + by + (int) all.size()) % (int) all.size();
        proc.loadPresetFile (all[(size_t) at].file);
    }

    // ---- overlays
    void open (std::unique_ptr<Overlay> o)
    {
        if (o == nullptr) return;
        overlay = std::move (o);
        overlay->onClose = [this] { juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<Impl> (this)] { if (safe != nullptr) { safe->overlay.reset(); safe->refreshAll(); } }); };
        addAndMakeVisible (*overlay);
        overlay->setBounds (0, 0, kBaseW, kBaseH);
        overlay->toFront (true);
    }

    // ---- how long the window takes to draw: told to the film, and written down every twenty seconds while the window is
    //      open (Reports/screen.txt), so that a slow screen can be looked into from what the plug-in itself measured
    void drawBegan (bool mostOfIt, float pixelsPerUnit)
    {
        drawStarted = mostOfIt ? juce::Time::getMillisecondCounterHiRes() : 0.0;
        pixels = { juce::roundToInt ((float) kBaseW * pixelsPerUnit), juce::roundToInt ((float) kBaseH * pixelsPerUnit) };
    }
    void drawEnded()
    {
        if (drawStarted <= 0.0) return;
        const float ms = (float) (juce::Time::getMillisecondCounterHiRes() - drawStarted);
        drawStarted = 0.0;
        stage.drawTook (ms);
        ++draws; drawSum += ms; drawWorst = juce::jmax (drawWorst, ms);
    }
    void writeScreenReport (juce::uint32 now)
    {
        const double span = juce::jmax (1.0, (double) (now - reportAt) * 0.001);
        const int made = stage.player().made();
        static const char* moves[3] = { "still", "alive", "film" };
        juce::String t;
        t << "Ghost Atlas " << GHOSTATLAS_VERSION << " - how the screen is doing (written by the plug-in while its window is open)\n"
          << "window: " << pixels.x << " x " << pixels.y << " pixels (" << points.x << " x " << points.y << " points)\n"
          << "the world: " << moves[juce::jlimit (0, 2, stage.motion())] << (stage.filmShowing() ? " (the film is showing)" : "") << "\n"
          << "whole-window draws: " << draws << " in the last " << juce::roundToInt (span) << " s, " << juce::String (drawSum / (float) juce::jmax (1, draws), 1) << " ms on average, "
          << juce::String (drawWorst, 1) << " ms the slowest\n"
          << "film: " << juce::String ((double) (made - madeAtReport) / span, 1) << " pictures a second, the last made in " << juce::String (stage.player().lastMakeMs(), 1) << " ms\n"
          << "written: " << juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S") << "\n";
        reportsDir().createDirectory();
        reportsDir().getChildFile ("screen.txt").replaceWithText (t);
        draws = 0; drawSum = 0.0f; drawWorst = 0.0f; madeAtReport = made;
    }

    void timerCallback() override
    {
        auto& e = proc.engine();
        page().tick();
        const auto now = juce::Time::getMillisecondCounter();
        if (reportAt == 0) reportAt = now;
        else if (now - reportAt >= 20000u) { if (draws > 0) writeScreenReport (now); reportAt = now; }
        juce::String want = about; bool lit = false;
        if (e.loading()) { want = "Loading the sound..."; lit = true; }
        else if (said.isNotEmpty() && now < saidUntil) { want = said; lit = true; }
        if (want != line || lit != lineLit) { line = want; lineLit = lit; masthead.repaint(); }
    }

    Processor& proc;
    Shell shell;
    Stage stage;
    std::array<std::unique_ptr<Page>, kNumPages> pages;
    Masthead masthead;
    std::array<TextBtn, kNumPages> tabs;
    IconBtn prev, next, fav, play;
    TextBtn save, mutate, motion;
    std::unique_ptr<Overlay> overlay;
    juce::TooltipWindow tips { this, 650 };
    std::function<void()> onMutate;
    juce::String name, about, line, said;
    juce::uint32 saidUntil = 0;
    int current = 0, soundWorld = 7, shownWorld = 7, auditionKind = 0;
    bool favourite = false, lineLit = false;
    double drawStarted = 0.0;                        // when the window began to be drawn (0: only a small part of it is)
    juce::Point<int> pixels, points;
    int draws = 0, madeAtReport = 0; float drawSum = 0.0f, drawWorst = 0.0f;
    juce::uint32 reportAt = 0;
};

// ============================================================================================ the window
Editor::Editor (Processor& p) : juce::AudioProcessorEditor (p), impl (new Impl (p))
{
    addAndMakeVisible (*impl);
    setResizable (true, true);
    if (auto* c = getConstrainer()) { c->setFixedAspectRatio ((double) kBaseW / (double) kBaseH); c->setSizeLimits (880, 480, 2640, 1440); }
    const int w = juce::jlimit (880, 2640, (int) prop (p.uiState, "width", kBaseW));
    setSize (w, (int) std::lround ((double) w * kBaseH / kBaseW));
}
Editor::~Editor() = default;

void Editor::paint (juce::Graphics& g) { g.fillAll (juce::Colours::black); }
// (the stage is drawn first and says when the drawing of the window began; this is called when all of it has been drawn)
void Editor::paintOverChildren (juce::Graphics&) { impl->points = { getWidth(), getHeight() }; impl->drawEnded(); }

void Editor::resized()
{
    const float s = juce::jmin ((float) getWidth() / (float) kBaseW, (float) getHeight() / (float) kBaseH);
    impl->setTransform (juce::AffineTransform::scale (s));
    impl->setTopLeftPosition (0, 0);
    impl->stage.setBounds (0, 0, kBaseW, kBaseH);
    impl->stage.scaleChanged();
    auto o = impl->proc.uiState.getDynamicObject() != nullptr ? juce::DynamicObject::Ptr (impl->proc.uiState.getDynamicObject()) : newObject();
    o->setProperty ("width", getWidth()); impl->proc.uiState = o.get();
}

void Editor::showPage (int page) { impl->goTo (page); }
void Editor::showOverlay (const juce::String& which)
{
    if (which == "picker") impl->open (makeInstrumentPicker (impl->shell, impl->shell.layer));
    else if (which == "save") impl->open (makeSaveDialog (impl->shell));
    else if (which == "mutate" && impl->onMutate) impl->onMutate();
    else impl->page().showFor (which);            // a named view of the page that is up
}
void Editor::setMotion (int mode) { impl->setMotion (mode); }
bool Editor::filmShowing() const { return impl->stage.filmShowing(); }
float Editor::filmMakeMs() const { return impl->stage.player().lastMakeMs(); }
int Editor::filmPictures() const { return impl->stage.player().made(); }
void Editor::settle()
{
    impl->stage.setOffscreen (true);              // (whoever calls this is drawing the screen into a picture: it must still move)
    // (an overlay at work - MUTATE - is waited for, so that a picture of the screen shows what it found)
    for (int i = 0; i < 2400 && impl->overlay != nullptr && impl->overlay->busy(); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    impl->stage.waitUntilSettled();
}
} // namespace atlas
