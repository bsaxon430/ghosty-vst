// Ghost Mosaic tests: the screen (UI test, screenshots) and the demo renders. Included by MosaicTests.cpp.

static void frames (Editor& ed, Processor& p, int n, bool audio)
{
    juce::AudioBuffer<float> io (2, 735);
    juce::MidiBuffer midi;
    for (int i = 0; i < n; ++i)
    {
        if (audio)
        {
            if (p.isEffect())
            {
                static auto voice = demo::vowelPhrase (44100, 6);
                static int pos = 0;
                for (int c = 0; c < 2; ++c) io.copyFrom (c, 0, voice, juce::jmin (c, voice.getNumChannels() - 1), pos, 735);
                pos = (pos + 735) % (voice.getNumSamples() - 800);
            }
            else io.clear();
            p.processBlock (io, midi);
        }
        ed.frame();
        pump (16);
    }
}

static juce::Button* btn (Editor& ed, const juce::String& id) { return dynamic_cast<juce::Button*> (ed.findById (id)); }

// build 16 (Brandon 2026-11-06 15:44): a new instance is ALWAYS brand new. Nothing carries over from another instance.
static void testFreshStart()
{
    const double sr = 44100;
    auto atDefaults = [] (Processor& q, juce::String& why)
    {
        for (auto& info : paramTable())
        {
            if (! q.isEffect() && (juce::String (info.id) == "live_input" || juce::String (info.id) == "dry")) continue;
            if (std::abs (q.getParam (info.id) - info.def) > 1e-4f * (info.max - info.min) + 1e-6f) { why << info.id << " "; }
        }
        if (! q.engine().sounds().empty()) why << "sounds ";
        if (q.abIsB()) why << "B ";
        if (q.undo().canUndo()) why << "undo ";
        return why.isEmpty();
    };
    juce::String why1, why2, why3;
    {
        Processor a; a.prepareToPlay (sr, 735);
        a.loadKit ("drums"); waitIdle (a);
        setP (a, "novelty", 0.8f); setP (a, "boredom", 0.2f); setP (a, "volume", 0.7f); setP (a, "limiter", 1); setP (a, "src_block", 2400);
        a.switchAB(); setP (a, "novelty", 0.1f);
        Processor b; b.prepareToPlay (sr, 735);
        Editor eb (b); pump (20);
        check (atDefaults (b, why1) && ! eb.hoodShown(), "a second instance made while the first is changed starts at factory defaults (no sounds, first page, no A/B or undo)", why1);
        check (a.engine().sounds().size() == demo::kit ("drums", sr).size() && std::abs (a.getParam ("novelty") - 0.1f) < 0.011f, "...and the first instance keeps its own state");
    }
    {
        Processor c; c.prepareToPlay (sr, 735);
        check ((int) c.getParam ("tgt_block") == 7237 && std::abs (c.getParam ("tgt_overlap") - 0.81f) < 0.0051f && (int) c.getParam ("tgt_window") == 5
                   && (int) c.getParam ("src_block") == 5304 && std::abs (c.getParam ("src_overlap") - 0.82f) < 0.0051f && (int) c.getParam ("src_window") == 5,
               "a fresh instance starts with target 7237 / 0.81 / Hamming and library 5304 / 0.82 / Hamming",
               juce::String (c.getParam ("tgt_block")) + " " + juce::String (c.getParam ("tgt_overlap")) + " " + juce::String (c.getParam ("tgt_window")) + " | "
                   + juce::String (c.getParam ("src_block")) + " " + juce::String (c.getParam ("src_overlap")) + " " + juce::String (c.getParam ("src_window")));
        check (atDefaults (c, why2), "destroy and recreate: the new instance is at factory defaults", why2);
    }
    {
        Processor d; d.prepareToPlay (sr, 735);
        Editor ed (d); pump (20);
        check (ed.getWidth() <= 1240 && ed.getHeight() <= 780 && ed.getWidth() >= 1100 && ed.getHeight() >= 700, "the editor opens at the fitting size (about 1200 x 760, never above 1240 x 780)",
               juce::String (ed.getWidth()) + " x " + juce::String (ed.getHeight()));
        ed.showPage (true); pump (10);
        ed.showPage (false); pump (10);
        Processor e; e.prepareToPlay (sr, 735);
        Editor ee (e); pump (10);
        check (ee.getWidth() == ed.getWidth() && ee.getHeight() == ed.getHeight() && ! ee.hoodShown(), "a new editor always opens at the same default size, on the first page", juce::String (ee.getWidth()) + " x " + juce::String (ee.getHeight()));
    }
}

static void testUi()
{
    const double sr = 44100;
    Processor p;
    p.prepareToPlay (sr, 735);
    setP (p, "auto_build", 0);
    Editor ed (p);
    pump (50);
    // every setting has a control on UNDER THE HOOD
    int missing = 0; juce::StringArray miss;
    for (auto& info : paramTable())
    {
        if (! p.isEffect() && (juce::String (info.id) == "live_input" || juce::String (info.id) == "dry")) continue;
        if (! ed.hood().hasControlFor (info.id)) { ++missing; miss.add (info.id); }
    }
    check (missing == 0, "UNDER THE HOOD has a control for every setting", juce::String (ed.hood().numControls()) + " controls" + (miss.isEmpty() ? juce::String() : ", missing " + miss.joinIntoString (",")));
    const char* actions[] = { "addSounds", "addFolder", "allOn", "allOff", "clearLibrary", "generateLibrary", "loadTarget", "generateTarget", "restart",
                              "openLibrary", "saveLibrary", "openSession", "saveSession", "record", "stopRecord" };
    int nb = 0; for (auto* a : actions) nb += ed.hood().button (a) != nullptr ? 1 : 0;
    check (nb == 15, "UNDER THE HOOD has every button of the original (sounds, target, files, recording)", juce::String (nb) + " of 15");
    // build 16: the original's window in our colours: its own names, its columns, its bottom row
    {
        ed.setSize (1200, 760); ed.showPage (true); pump (20);
        auto& h = ed.hood();
        const char* want[][2] = { { "ratio", "fft / mfcc" }, { "n_ratio", "freq & dynamics / freq only" }, { "fft_start", "Start" }, { "fft_end", "End" }, { "novelty", "novelty" },
            { "boredom", "boredom" }, { "stickyness", "stickyness" }, { "stretch", "search stretch" }, { "algo", "algorithm" }, { "synapses", "num synpases" },
            { "slide_error", "synaptic slide error" }, { "tgt_block", "block size" }, { "tgt_overlap", "block overlap" }, { "tgt_window", "window shape" },
            { "autotune", "autotune" }, { "n_mix", "normalised" }, { "target_mix", "brain / target" }, { "stereo", "stereo mode" },
            { "src_block", "block size" }, { "src_overlap", "block overlap" }, { "src_window", "window shape" } };
        int ok = 0; juce::String bad;
        for (auto& w : want) { if (h.originalLabel (w[0]) == w[1] && h.control (w[0]) != nullptr) ++ok; else bad << w[0] << " "; }
        check (ok == 21, "UNDER THE HOOD uses the original's own names for every control", juce::String (ok) + " of 21 " + bad);
        // the three columns in the original's order, left to right, and each column top to bottom
        auto x = [&] (const char* id) { return h.control (id)->getScreenBounds().getCentre(); };
        const bool cols = x ("ratio").x < x ("tgt_block").x && x ("tgt_block").x < x ("src_block").x;
        const bool c1 = x ("ratio").y < x ("n_ratio").y && x ("n_ratio").y < x ("fft_start").y && x ("fft_start").y < x ("novelty").y && x ("novelty").y < x ("boredom").y
                     && x ("boredom").y < x ("stickyness").y && x ("stickyness").y < x ("stretch").y && x ("stretch").y < x ("algo").y && x ("algo").y < x ("synapses").y && x ("synapses").y < x ("slide_error").y;
        const bool c2 = x ("tgt_block").y < x ("tgt_overlap").y && x ("tgt_overlap").y < x ("tgt_window").y && x ("tgt_window").y < x ("autotune").y && x ("autotune").y < x ("n_mix").y
                     && x ("n_mix").y < x ("target_mix").y && x ("target_mix").y < x ("stereo").y;
        const bool c3 = x ("src_block").y < x ("src_overlap").y && x ("src_overlap").y < x ("src_window").y;
        check (cols && c1 && c2 && c3, "UNDER THE HOOD: three columns (brain tweaks, target sound + mix, brain contents), same order as the original");
        check (x ("fft_start").x < x ("fft_end").x, "UNDER THE HOOD: fft subsection Start then End on one line");
        auto* sp = h.button ("play"); auto* sb = h.button ("pause"); auto* sr = h.button ("record"); auto* ss = h.button ("stopRecord");
        check (sp && sb && sr && ss && sp->getX() < sb->getX() && sb->getX() < sr->getX() && sr->getX() < ss->getX() && h.control ("volume")
                   && h.button ("openSession") && h.button ("saveSession") && h.button ("openSession")->getX() > ss->getX(),
               "UNDER THE HOOD: bottom row play, pause, record, stop, volume dial, load session, save session");
        check (h.button ("allOn") && h.button ("allOff") && h.button ("generateTarget") && h.button ("loadTarget") && h.button ("generateLibrary") && h.button ("openLibrary") && h.button ("saveLibrary"),
               "UNDER THE HOOD: all, none, load target, (re)generate blocks, (re)generate brain, load brain, save brain");
        // fits at the default window size: the original part ends inside the visible body; only the added strip scrolls
        check (h.originalPartBottom() <= ed.getHeight() - 110 - 94, "UNDER THE HOOD: the original part fits at the default size without scrolling", juce::String (h.originalPartBottom()) + " of " + juce::String (ed.getHeight() - 204));
        // one source of truth: the dial here IS the volume setting (and the VOLUME knob on the main screen)
        if (auto* v = dynamic_cast<juce::Slider*> (h.control ("volume")))
        {
            v->setValue (0.42, juce::sendNotificationSync); pump (20);
            const bool a = std::abs (p.getParam ("volume") - 0.42f) < 0.011f;
            p.setParam ("volume", 0.2f); pump (20);
            check (a && std::abs (v->getValue() - 0.2) < 0.011, "UNDER THE HOOD: the volume dial and the volume setting are the same control");
        }
        // play / pause / stop reach the engine
        sp->triggerClick(); pump (20);
        const bool played = p.engine().isPlaying();
        sb->triggerClick(); pump (20);
        const bool paused = ! p.engine().isPlaying();
        check (played && paused, "UNDER THE HOOD: play and pause buttons drive the engine");
        // the net tab is still there as a second tab
        if (auto* t = ed.findById ("hoodTabNet")) { static_cast<juce::Button*> (t)->triggerClick(); pump (10); check (h.netTabShown(), "UNDER THE HOOD: the net tab (OSC) is there as a second tab"); static_cast<juce::Button*> (ed.findById ("hoodTabSearch"))->triggerClick(); pump (10); }
        else check (false, "UNDER THE HOOD: the net tab exists");
        ed.showPage (false); pump (10);
    }
    check (ed.numMacros() >= 8 && btn (ed, "generate") && btn (ed, "mutate") && btn (ed, "undo") && btn (ed, "redo") && btn (ed, "ab") && ed.findById ("dragWav") && ed.findById ("presets"),
           "main screen: macros, GENERATE, MUTATE, UNDO, REDO, A/B, DRAG WAV, presets", juce::String (ed.numMacros()) + " macros");

    // GENERATE with nothing: a plain-words hint, no build
    btn (ed, "generate")->triggerClick(); pump (30);
    check (! p.engine().busy() && p.engine().status().contains ("SOURCE SOUNDS"), "GENERATE with no sounds says what to do", p.engine().status());

    p.loadKit ("drums");
    if (! p.isEffect()) p.loadDemoTarget ("vowels");
    pump (30); waitIdle (p);
    btn (ed, "generate")->triggerClick(); pump (30);
    waitIdle (p);
    for (int i = 0; i < 30; ++i) { ed.frame(); pump (5); }
    const auto pic = p.engine().libraryPicture();
    check (pic != nullptr && pic->numBlocks > 0 && ed.swarm().numDots() > 0 && ed.swarm().numDots() <= pic->numBlocks,
           "GENERATE builds the library and the live view shows its pieces", juce::String (pic ? pic->numBlocks : 0) + " pieces, " + juce::String (ed.swarm().numDots()) + " dots");
    check (ed.sources().list().getListBoxModel()->getNumRows() == (int) p.engine().sounds().size() && p.engine().sounds().size() == demo::kit ("drums", sr).size(),
           "SOURCE SOUNDS lists the sounds", juce::String ((int) p.engine().sounds().size()));

    // playing lights the dots
    if (! p.isEffect()) p.engine().play();
    int lit = 0;
    {
        juce::AudioBuffer<float> io (2, 735); juce::MidiBuffer midi;
        auto voice = demo::vowelPhrase (sr, 6);
        Engine::Event ev[512];
        for (int i = 0; i < 120; ++i)
        {
            if (p.isEffect()) for (int c = 0; c < 2; ++c) io.copyFrom (c, 0, voice, 0, (i * 735) % (voice.getNumSamples() - 800), 735); else io.clear();
            p.processBlock (io, midi);
            if (auto* lt = p.engine().liveTarget()) lt->waitIdle (100);
            if (i == 60) lit = p.engine().popEvents (ev, 512);
        }
        ed.frame();
    }
    check (lit > 0 && ! ed.swarm().settled(), "the dots flash as the engine plays their pieces", juce::String (lit) + " pieces played in 0.5 s");

    // MUTATE: changes settings, fires the shockwave, UNDO takes it back
    pump (100);
    std::vector<float> before; for (auto& info : paramTable()) before.push_back (p.getParam (info.id));
    const int shapeBefore = ed.swarm().shapeIndex();
    btn (ed, "mutate")->triggerClick(); pump (100);
    for (int i = 0; i < 3; ++i) { ed.frame(); pump (5); }
    int changed = 0; { size_t k = 0; for (auto& info : paramTable()) changed += std::abs (p.getParam (info.id) - before[k++]) > 1e-5f * (info.max - info.min) + 1e-6f ? 1 : 0; }
    check (changed > 0 && ed.swarm().shapeIndex() != shapeBefore, "MUTATE changes the search settings and the dots regather into the next shape", juce::String (changed) + " settings changed");
    for (int i = 0; i < 12; ++i) ed.frame();
    btn (ed, "undo")->triggerClick(); pump (100);
    int back = 0; juce::String backIds; { size_t k = 0; for (auto& info : paramTable()) { if (std::abs (p.getParam (info.id) - before[k]) > 1e-5f * (info.max - info.min) + 1e-6f) { ++back; backIds << info.id << " " << before[k] << "->" << p.getParam (info.id) << "; "; } ++k; } }
    check (back == 0, "UNDO takes MUTATE back", juce::String (back) + " settings still different " + backIds);
    for (int i = 0; i < 12; ++i) ed.frame();   // (the buttons light up on the screen's clock)
    btn (ed, "redo")->triggerClick(); pump (100);
    int again = 0; { size_t k = 0; for (auto& info : paramTable()) again += std::abs (p.getParam (info.id) - before[k++]) > 1e-5f * (info.max - info.min) + 1e-6f ? 1 : 0; }
    check (again == changed, "REDO puts it back", juce::String (again));

    // A / B
    p.setParam ("novelty", 0.2f); pump (30);
    btn (ed, "ab")->triggerClick(); pump (30);
    p.setParam ("novelty", 0.7f); pump (30);
    btn (ed, "ab")->triggerClick(); pump (30);
    const float a1 = p.getParam ("novelty");
    btn (ed, "ab")->triggerClick(); pump (30);
    const float b1 = p.getParam ("novelty");
    check (std::abs (a1 - 0.2f) < 0.006f && std::abs (b1 - 0.7f) < 0.006f && p.engine().sounds().size() == demo::kit ("drums", sr).size(),
           "A / B switches between two sets of settings and keeps the sounds", "A " + juce::String (a1, 2) + ", B " + juce::String (b1, 2));

    // the macro PIECE SIZE sets both piece sizes
    if (auto* k = dynamic_cast<juce::Slider*> (ed.findById ("macro:PIECE SIZE")))
    {
        k->setValue (2400, juce::sendNotificationSync); pump (30);
        check ((int) p.getParam ("src_block") == 2400 && (int) p.getParam ("tgt_block") == 2400, "PIECE SIZE macro sets the library's and the target's piece size");
    }
    else check (false, "PIECE SIZE macro exists");

    // a preset loads its sounds and builds
    p.applyPreset (2); pump (50); waitIdle (p); pump (50);
    check (! p.engine().sounds().empty() && p.engine().libraryPicture() && p.engine().libraryPicture()->numBlocks > 0, "a preset with built-in sounds loads and builds them",
           juce::String ((int) p.engine().sounds().size()) + " sounds");

    // DRAG WAV makes a WAV to drag out (the effect: a recording of the track, rebuilt)
    if (p.isEffect())
    {
        btn (ed, "tgtRec")->triggerClick(); pump (20);
        juce::AudioBuffer<float> io (2, 735); juce::MidiBuffer midi;
        auto voice = demo::vowelPhrase (sr, 3);
        for (int i = 0; i * 735 + 735 < voice.getNumSamples(); ++i)
        {
            for (int c = 0; c < 2; ++c) io.copyFrom (c, 0, voice, 0, i * 735, 735);
            p.processBlock (io, midi);
            if (auto* lt = p.engine().liveTarget()) lt->waitIdle (100);
        }
        btn (ed, "tgtRec")->triggerClick(); pump (50);
    }
    else ed.dragWav().render();
    for (int i = 0; i < 6000 && ! ed.dragWav().fresh(); ++i) pump (10);
    const auto wav = ed.dragWav().file();
    check (ed.dragWav().fresh() && wav.getSize() > 20000, "DRAG WAV renders a WAV to drag into the DAW", wav.getFileName() + ", " + juce::String (wav.getSize()) + " bytes");
    wav.deleteFile();

    // the TERRAIN page: a SHAPES preset shapes the land and the readout shows what it does; ON off = the knobs as set
    {
        ed.setPage (1); pump (20);
        auto& tp = ed.terrainPage();
        tp.shapes().setSelectedItemIndex (0, juce::sendNotificationSync);   // GHOST
        for (int i = 0; i < 10; ++i) { ed.frame(); pump (5); }
        const bool shaped = ! p.terrain.terrain().empty() && p.engine().shaping().active() && ! tp.shownPushes().empty();
        tp.onSwitch().triggerClick(); pump (20);
        const bool off = ! p.engine().shaping().active() && ! p.terrainOn();
        tp.onSwitch().triggerClick(); pump (20);
        p.terrain.undo(); pump (20);
        check (shaped && off && p.terrainOn() && p.terrain.terrain().empty() && btn (ed, "tabTerrain") != nullptr,
               "TERRAIN page: a SHAPES preset shapes the land and the readout shows it; ON switches it off; UNDO takes the shape back",
               juce::String ((int) tp.shownPushes().size()) + " moves shown" + (tp.shownPushes().empty() ? juce::String() : ", top: " + tp.shownPushes().front().name));
        ed.setPage (0);
    }

    // the pages and views paint
    ed.showPage (true); pump (20);
    auto i1 = ed.createComponentSnapshot (ed.getLocalBounds());
    ed.showPage (false);
    btn (ed, "viewHeat")->triggerClick(); pump (20);
    for (int i = 0; i < 5; ++i) { ed.frame(); pump (5); }
    auto i2 = ed.createComponentSnapshot (ed.getLocalBounds());
    check (i1.isValid() && i2.isValid() && ed.swarm().heatView(), "UNDER THE HOOD and the HEAT view draw");
    // no UI words that must not be there
    juce::StringArray words;
    std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
    {
        // (UNDER THE HOOD prints the original window's own words, 'brain' included, on purpose: Brandon 2026-11-06 15:28)
        if (auto* b = dynamic_cast<juce::Button*> (&c)) words.add ((c.findParentComponentOfClass<HoodPage>() != nullptr ? juce::String() : b->getButtonText()) + " " + b->getTooltip());
        if (auto* s = dynamic_cast<juce::SettableTooltipClient*> (&c)) words.add (s->getTooltip());
        for (auto* ch : c.getChildren()) walk (*ch);
    };
    walk (ed);
    for (auto& info : paramTable()) { words.add (info.label); words.add (info.help); }
    for (auto& pr : factoryPresets()) { words.add (pr.name); words.add (pr.blurb); }
    for (auto& info : paramTable()) words.add (shownOriginal (info));
    words.add (p.engine().status());
    const auto clean = words.joinIntoString (" ").toLowerCase();
    check (! clean.contains ("samplebrain") && ! clean.contains ("then try this") && ! clean.contains ("gitlab"), "no upstream project names in any label or tooltip on screen (only About / Credits names them)");
    check (creditsText().contains ("GNU General Public License") && creditsText().contains ("gitlab.com/then-try-this/samplebrain") && creditsText().contains ("github.com/bsaxon430"), "About / Credits names the project, its licence and the source links");
    const int at = clean.indexOf ("brain");
    check (at < 0 && ! clean.contains (juce::String::fromUTF8 ("\xf0\x9f\xa7\xa0")), "no 'brain' anywhere in the words on screen", at < 0 ? juce::String() : "found in: ..." + clean.substring (juce::jmax (0, at - 60), at + 30) + "...");
}

static juce::File shot (Editor& ed, const juce::File& dir, const juce::String& name)
{
    auto img = ed.createComponentSnapshot (ed.getLocalBounds(), true, 1.0f);
    auto f = dir.getChildFile (name + ".png");
    f.deleteFile();
    juce::FileOutputStream os (f);
    juce::PNGImageFormat png;
    png.writeImageToStream (img, os);
    std::printf ("SHOT %s\n", f.getFullPathName().toRawUTF8());
    return f;
}

static int runShots (const juce::File& dir)
{
    dir.createDirectory();
    const juce::String tag = GHOSTMOSAIC_FX ? "mosaic-fx" : "mosaic";
    {
        Processor p; p.prepareToPlay (44100, 735);
        Editor ed (p);
        frames (ed, p, 70, false);
        shot (ed, dir, tag + "-empty");
    }
    {
        // build 14: a new instance opens ready to play; a long song shows its build with the time left
        Processor::preloadNewInstances = true;
        Processor p; p.prepareToPlay (44100, 735);
        Processor::preloadNewInstances = false;
        Editor ed (p);
        waitIdle (p);
        frames (ed, p, 40, false);
        shot (ed, dir, tag + "-new-instance");
        auto song = hostscen::melody (44100, 60);
        auto f = dir.getChildFile ("long-song.wav");
        hostscen::writeWav (dir, "long-song", song, 44100);
        p.engine().clearSounds();
        p.engine().addFiles (juce::StringArray (f.getFullPathName()));
        for (int i = 0; i < 400 && ! (p.engine().buildInfo().phase == 2 && p.engine().progress() > 0.3f); ++i) frames (ed, p, 1, false);
        p.generate();
        frames (ed, p, 6, false);
        shot (ed, dir, tag + "-building-long-song");
        waitIdle (p);
        f.deleteFile();
    }
    Processor p; p.prepareToPlay (44100, 735);
    Editor ed (p);
    if (GHOSTMOSAIC_FX) { p.loadKit ("drums"); p.loadKit ("tones"); }
    else p.applyPreset (1);                         // Voice From Drums
    pump (50); waitIdle (p);
    if (GHOSTMOSAIC_FX) { p.engine().generateLibrary(); waitIdle (p); }
    frames (ed, p, 90, false);
    p.engine().play();
    frames (ed, p, 150, true);
    shot (ed, dir, tag + "-playing");
    p.mutate (77);
    frames (ed, p, 9, true);
    shot (ed, dir, tag + "-mutate");
    frames (ed, p, 120, true);
    btn (ed, "viewHeat")->triggerClick();
    frames (ed, p, 180, true);
    shot (ed, dir, tag + "-heat");
    btn (ed, "viewDots")->triggerClick();
    ed.showPage (true);
    ed.setSize (1200, 760);
    frames (ed, p, 5, false);
    shot (ed, dir, tag + "-under-the-hood-default");
    ed.setSize (1180, 1400);
    frames (ed, p, 5, false);
    shot (ed, dir, tag + "-under-the-hood");
    ed.showPage (false);
    ed.setSize (1000, 700);
    frames (ed, p, 60, true);
    shot (ed, dir, tag + "-small-window");

    // ---- TERRAIN (build 9): flat, with peaks (playing), while dragging a peak's dot
    ed.setSize (1200, 760);
    ed.setPage (1);
    p.terrain.set (ghost::terrain::flatTerrain());
    frames (ed, p, 40, true);
    shot (ed, dir, tag + "-terrain-flat");
    {
        using namespace ghost::terrain;
        Terrain t;
        t.add ({ hzToX (90, terrainRange()), 0.5f, 0.85f, 0.05f, kMaxDepth, 0.0f });      // the lows count more, all the way through
        t.add ({ hzToX (7000, terrainRange()), 0.3f, 0.45f, 0.06f, 0.5f, 0.0f });        // the hats a little, early on
        t.add ({ 0.5f, 0.72f, 0.7f, 0.3f, 0.06f, 0.3f });                                // late in the target it gets wild
        t.add ({ 0.62f, 0.2f, -0.4f, 0.07f, 0.09f, 0.0f });                              // a valley early on: stick close
        p.terrain.set (t);
        p.terrain.select (2);
        auto& v = ed.terrainPage().view();
        v.settle();
        frames (ed, p, 60, true);
        shot (ed, dir, tag + "-terrain-peaks");
        // drag the selected mountain's dot up (the value tag shows while dragging)
        const auto dot = v.handleDot (0);
        v.interaction.mouseDown (dot, {});
        for (int i = 1; i <= 6; ++i) { v.interaction.mouseDrag (dot.translated (0.0f, -6.0f * (float) i), {}); frames (ed, p, 3, true); }
        frames (ed, p, 10, true);
        shot (ed, dir, tag + "-terrain-dragging");
        v.interaction.mouseUp (dot.translated (0.0f, -36.0f), {});
        frames (ed, p, 20, true);
        p.terrain.set (shapePresets().front().make());   // the GHOST shape
        v.settle();
        frames (ed, p, 40, true);
        shot (ed, dir, tag + "-terrain-shape-ghost");
        // build 15: MOVING NOW. A lively shape with MOVEMENT up; frames over time on the TERRAIN page and UNDER THE HOOD, markers moving
        setP (p, "move_amount", 1.0f); setP (p, "move_rate", 0);
        setP (p, "ratio", 0.5f); setP (p, "novelty", 0.3f); setP (p, "stickyness", 0.3f); setP (p, "stretch", 2); setP (p, "synapses", 50);
        setP (p, "n_ratio", 0.3f); setP (p, "target_mix", 0.3f); setP (p, "boredom", 0.5f);
        p.terrain.set (shapePresets()[1].make());   // SPIRE FOREST
        v.settle();
        for (int k = 0; k < 4; ++k) { frames (ed, p, 45, true); shot (ed, dir, tag + "-moving-terrain-" + juce::String (k)); }
        ed.showPage (true);
        ed.setSize (1180, 1400);
        for (int k = 0; k < 3; ++k) { frames (ed, p, 45, true); shot (ed, dir, tag + "-moving-under-the-hood-" + juce::String (k)); }
        ed.showPage (false);
        ed.setSize (1200, 760);
        ed.setPage (0);
        frames (ed, p, 45, true);
        shot (ed, dir, tag + "-moving-play");
        ed.setPage (1);
    }
    ed.setPage (0);

    // ---- the ghost in the live view: first open (a fresh editor), and a strip of the swarm sweeping into it
    {
        Processor gp; gp.prepareToPlay (44100, 735);
        if (GHOSTMOSAIC_FX) { gp.loadKit ("drums"); gp.loadKit ("tones"); } else gp.applyPreset (1);
        pump (50); waitIdle (gp);
        if (GHOSTMOSAIC_FX) { gp.engine().generateLibrary(); waitIdle (gp); }
        Editor ge (gp);
        frames (ge, gp, 150, false);
        auto& sw = ge.swarm();
        auto save = [&] (const juce::Image& img, const juce::String& name)
        {
            auto f = dir.getChildFile (name + ".png"); f.deleteFile();
            juce::FileOutputStream os (f); juce::PNGImageFormat png; png.writeImageToStream (img, os);
            std::printf ("SHOT %s\n", f.getFullPathName().toRawUTF8());
        };
        save (sw.createComponentSnapshot (sw.getLocalBounds(), true, 2.0f), tag + "-ghost");
        // round the cycle to the flower, then the shockwave that gathers into the ghost
        for (int i = 0; i < 3; ++i) { sw.shockwave(); frames (ge, gp, 90, false); }
        sw.shockwave();
        const int cells = 8;
        const auto cell = sw.getLocalBounds();
        juce::Image strip (juce::Image::ARGB, cell.getWidth() * cells / 2, cell.getHeight() / 2 * 1, true);
        {
            juce::Graphics g (strip);
            g.fillAll (look::ground);
            for (int k = 0; k < cells; ++k)
            {
                auto img = sw.createComponentSnapshot (cell, true, 0.5f);
                g.drawImageAt (img, k * cell.getWidth() / 2, 0);
                frames (ge, gp, k < 4 ? 5 : 10, false);
            }
        }
        save (strip, tag + "-ghost-sweep-strip");
    }
    return 0;
}

// ---------------------------------------------------------------- demos (our own synthesised sounds only)
static int runDemos (const juce::File& dir)
{
    dir.createDirectory();
    const double sr = 44100;
#if GHOSTMOSAIC_FX
    // the effect: a sung line through the effect, rebuilt live from synth drums and tones
    struct D { const char* name; const char* kits[2]; float dry; };
    const D ds[] = { { "fx-voice-through-drums", { "drums", nullptr }, 0.0f }, { "fx-voice-through-tones-with-dry", { "tones", "noise" }, 0.35f } };
    for (auto& d : ds)
    {
        Processor p; setP (p, "auto_build", 0);
        setP (p, "src_window", 6); setP (p, "tgt_window", 6); setP (p, "src_overlap", 0.75f); setP (p, "tgt_overlap", 0.75f); setP (p, "src_block", 1500); setP (p, "tgt_block", 1500);
        setP (p, "dry", d.dry); setP (p, "volume", 0.25f); setP (p, "ratio", 0.7f);
        p.prepareToPlay (sr, 512);
        for (auto* k : d.kits) if (k) p.loadKit (k);
        p.engine().generateLibrary(); waitIdle (p);
        auto voice = demo::vowelPhrase (sr, 6);
        juce::AudioBuffer<float> all (2, voice.getNumSamples() * 2);
        juce::AudioBuffer<float> io (2, 512); juce::MidiBuffer midi;
        for (int pos = 0; pos + 512 <= all.getNumSamples(); pos += 512)
        {
            for (int c = 0; c < 2; ++c) io.copyFrom (c, 0, voice, juce::jmin (c, voice.getNumChannels() - 1), pos % (voice.getNumSamples() - 512), 512);
            p.processBlock (io, midi);
            if (auto* lt = p.engine().liveTarget()) lt->waitIdle (200);
            for (int c = 0; c < 2; ++c) all.copyFrom (c, pos, io, c, 0, 512);
        }
        auto f = dir.getChildFile (juce::String (d.name) + ".wav");
        f.deleteFile();
        if (auto os = f.createOutputStream())
        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, 2, 24, {}, 0));
            if (w) { os.release(); w->writeFromAudioSampleBuffer (all, 0, all.getNumSamples()); }
        }
        std::printf ("DEMO %s\n", f.getFullPathName().toRawUTF8());
    }
    // and the input, for comparison
    {
        auto voice = demo::vowelPhrase (sr, 6);
        auto f = dir.getChildFile ("fx-input-sung-vowels.wav"); f.deleteFile();
        if (auto os = f.createOutputStream()) { juce::WavAudioFormat wav; std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, voice.getNumChannels(), 24, {}, 0)); if (w) { os.release(); w->writeFromAudioSampleBuffer (voice, 0, voice.getNumSamples()); } }
    }
#else
    struct D { const char* name; int preset; const char* kits[2]; const char* target; };
    const D ds[] = {
        { "voice-from-drums", 1, { nullptr, nullptr }, nullptr },
        { "drums-from-tones", 2, { nullptr, nullptr }, nullptr },
        { "piano-in-the-air", 3, { nullptr, nullptr }, nullptr },
        { "glitch-shuffle", 4, { "tones", "drums" }, "beat" },
        { "long-smears", 5, { "noise", "tones" }, "vowels" },
        { "network-walk", 6, { "drums", "noise" }, "piano" },
        { "pitch-follow", 9, { "tones", nullptr }, "vowels" },
    };
    for (auto& d : ds)
    {
        if (std::getenv ("MOSAIC_LIVE_DEMO_ONLY") != nullptr) break;
        Processor p; p.prepareToPlay (sr, 512);
        setP (p, "auto_build", 0);
        for (auto* k : d.kits) if (k) p.loadKit (k);
        if (d.target) p.loadDemoTarget (d.target);
        pump (20);
        p.applyPreset (d.preset);
        setP (p, "volume", 0.3f);
        pump (50); waitIdle (p);
        auto f = dir.getChildFile (juce::String (d.name) + ".wav");
        bool ok = false, done = false;
        std::srand (11);
        p.engine().renderToFile (f, 40, [&] (bool r) { ok = r; done = true; });
        for (int i = 0; i < 60000 && ! done; ++i) pump (10);
        std::printf ("DEMO %s %s\n", f.getFullPathName().toRawUTF8(), ok ? "ok" : "FAILED");
    }
    // TERRAIN (build 9): the same target and library through four landscapes
    if (std::getenv ("MOSAIC_LIVE_DEMO_ONLY") == nullptr)
    {
        using namespace ghost::terrain;
        auto ridge = [] (float hz) { Terrain t; t.add ({ hzToX (hz, terrainRange()), 0.5f, 1.0f, 0.06f, kMaxDepth, 0.0f }); return t; };
        Terrain timeR; timeR.add ({ 0.5f, 0.5f, 1.0f, kMaxWidth, 0.12f, 0.4f });
        struct L { const char* name; Terrain t; };
        const L lands[] = { { "terrain-flat", flatTerrain() }, { "terrain-lows-weighted", ridge (100) }, { "terrain-highs-weighted", ridge (7000) }, { "terrain-time-ridge", timeR } };
        Processor p; p.prepareToPlay (sr, 512);
        setP (p, "auto_build", 0);
        p.loadKit ("drums"); p.loadKit ("tones"); p.loadDemoTarget ("vowels");
        pump (20);
        setP (p, "ratio", 0.2f); setP (p, "src_block", 1500); setP (p, "tgt_block", 1500); setP (p, "src_window", 6); setP (p, "tgt_window", 6); setP (p, "src_overlap", 0.75f); setP (p, "tgt_overlap", 0.75f);
        setP (p, "novelty", 0.05f); setP (p, "volume", 0.3f);
        pump (50); waitIdle (p);
        std::vector<L> all (std::begin (lands), std::end (lands));
        for (auto& sp : shapePresets())
        {
            const juce::String n = juce::String ("shape-") + juce::String (sp.name).toLowerCase().replace (" ", "-");
            static std::list<std::string> names; names.push_back (n.toStdString());
            all.push_back ({ names.back().c_str(), sp.make() });
        }
        for (auto& l : all)
        {
            p.terrain.set (l.t); pump (5);
            auto f = dir.getChildFile (juce::String (l.name) + ".wav");
            bool ok = false, done = false;
            std::srand (11);
            p.engine().renderToFile (f, 40, [&] (bool r) { ok = r; done = true; });
            for (int i = 0; i < 60000 && ! done; ++i) pump (10);
            std::printf ("DEMO %s %s\n", f.getFullPathName().toRawUTF8(), ok ? "ok" : "FAILED");
            // how dark / bright it came out: the mean spectral centroid over 4096-sample frames (the loud ones)
            if (auto rd = std::unique_ptr<juce::AudioFormatReader> (juce::WavAudioFormat().createReaderFor (f.createInputStream().release(), true)))
            {
                juce::AudioBuffer<float> a (1, (int) rd->lengthInSamples); rd->read (&a, 0, a.getNumSamples(), 0, true, false);
                double s = 0, wsum = 0;
                for (int pos = 0; pos + 4096 <= a.getNumSamples(); pos += 2048)
                {
                    const float rms = a.getRMSLevel (0, pos, 4096);
                    if (rms < 1e-3f) continue;
                    s += rms * spectralCentroidHz (a.getReadPointer (0, pos), 4096, sr); wsum += rms;
                }
                std::printf ("CENTROID %s %.0f Hz\n", l.name, wsum > 0 ? s / wsum : 0.0);
            }
        }
    }
    // TERRAIN, live: building a wild shape step by step while it plays (the realtime engine, as you hear it dragging)
    {
        using namespace ghost::terrain;
        Processor p; p.prepareToPlay (sr, 512);
        setP (p, "auto_build", 0);
        p.loadKit ("drums"); p.loadKit ("tones"); p.loadDemoTarget ("vowels");
        pump (20);
        setP (p, "ratio", 0.2f); setP (p, "src_block", 1500); setP (p, "tgt_block", 1500); setP (p, "src_window", 6); setP (p, "tgt_window", 6); setP (p, "src_overlap", 0.75f); setP (p, "tgt_overlap", 0.75f);
        setP (p, "novelty", 0.05f); setP (p, "volume", 0.3f);
        pump (50); waitIdle (p);
        p.engine().generateLibrary(); p.engine().generateTarget(); waitIdle (p);
        p.terrain.set (flatTerrain()); pump (5);
        const int stepSec = 5, steps = 8;
        juce::AudioBuffer<float> all (2, (int) sr * stepSec * steps);
        juce::AudioBuffer<float> io (2, 512); juce::MidiBuffer midi;
        p.engine().play();
        Terrain t;
        for (int pos = 0, step = -1; pos + 512 <= all.getNumSamples(); pos += 512)
        {
            const int st = pos / (int) (sr * stepSec);
            const float inStep = (float) (pos - st * (int) (sr * stepSec)) / (float) (sr * stepSec);
            if (st != step) { step = st; if (step == 1) t.add ({ hzToX (90, terrainRange()), 0.5f, 0.0f, 0.06f, kMaxDepth, 0 }); }
            // step 0 flat; 1 a low mountain rises; 2 a spire at the hats; 3 a ridge across the middle of the target;
            // 4 more spires; 5 two mountains slide together and merge; 6 roughen; 7 scatter (RANDOMIZE)
            if (step == 1) t[0].height = juce::jmin (1.0f, inStep * 1.6f);
            if (step == 2 && t.size() < 2) t.add ({ hzToX (7000, terrainRange()), 0.4f, 0.8f, 0.016f, 0.3f, 0 });
            if (step == 3 && t.size() < 3) t.add ({ 0.5f, 0.55f, 0.0f, kMaxWidth, 0.08f, 0.2f });
            if (step == 3) t[2].height = juce::jmin (1.0f, inStep * 1.5f);
            if (step == 4 && t.size() < 5) { t.add ({ 0.62f, 0.25f, 0.7f, 0.015f, 0.08f, 0 }); t.add ({ 0.75f, 0.8f, 0.7f, 0.015f, 0.08f, 0 }); }
            if (step == 5 && t.size() < 6) t.add ({ 0.20f, 0.5f, 0.8f, 0.07f, 0.5f, 0 });
            if (step == 5) t[5].x = 0.20f + (hzToX (90, terrainRange()) + 0.03f - 0.20f) * inStep;
            if (step == 6) t.grit = 0.4f * inStep;
            if (step == 7 && inStep < 0.02f) t = randomTerrain (9);
            p.terrain.setWithoutUndo (t);
            io.clear(); p.processBlock (io, midi);
            for (int c = 0; c < 2; ++c) all.copyFrom (c, pos, io, c, 0, 512);
        }
        auto f = dir.getChildFile ("terrain-build-a-wild-shape-live.wav"); f.deleteFile();
        if (auto os = f.createOutputStream()) { juce::WavAudioFormat wav; std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, 2, 24, {}, 0)); if (w) { os.release(); w->writeFromAudioSampleBuffer (all, 0, all.getNumSamples()); } }
        std::printf ("DEMO %s ok\n", f.getFullPathName().toRawUTF8());
    }
    // TERRAIN, live (build 10): a few mountains, then flicks: fast up-right pulls burst wild, slow ones barely, down pulls calm
    {
        using namespace ghost::terrain;
        Processor p; p.prepareToPlay (sr, 512);
        setP (p, "auto_build", 0);
        p.loadKit ("drums"); p.loadKit ("tones"); p.loadDemoTarget ("vowels");
        pump (20);
        setP (p, "ratio", 0.3f); setP (p, "src_block", 1500); setP (p, "tgt_block", 1500); setP (p, "src_window", 6); setP (p, "tgt_window", 6); setP (p, "src_overlap", 0.75f); setP (p, "tgt_overlap", 0.75f);
        setP (p, "novelty", 0.05f); setP (p, "volume", 0.3f);
        pump (50); waitIdle (p);
        p.engine().generateLibrary(); p.engine().generateTarget(); waitIdle (p);
        Terrain t; t.add ({ hzToX (120, terrainRange()), 0.5f, 0.6f, 0.06f, kMaxDepth, 0 });
        p.terrain.set (t); pump (5);
        juce::AudioBuffer<float> all (2, (int) sr * 30);
        juce::AudioBuffer<float> io (2, 512); juce::MidiBuffer midi;
        p.engine().play();
        struct Flick { double at, len; float burst, side, lift; };
        const Flick fl[] = { { 4, 0.25, 1.0f, 0.8f, 0.9f }, { 7, 0.25, 1.0f, -0.8f, 0.9f }, { 10, 0.6, 0.3f, 0.2f, 0.2f }, { 13, 0.3, 0.6f, 0.0f, -1.0f },
                             { 16, 0.15, 1.0f, 1.0f, 1.0f }, { 16.6, 0.15, 1.0f, -1.0f, 1.0f }, { 17.2, 0.15, 1.0f, 1.0f, 1.0f }, { 21, 1.5, 0.7f, -0.5f, 0.6f } };
        for (int pos = 0; pos + 512 <= all.getNumSamples(); pos += 512)
        {
            const double ts = pos / sr;
            float b = 0, sd = 0, lf = 0;
            for (auto& f : fl) if (ts >= f.at && ts < f.at + f.len) { b = f.burst; sd = f.side; lf = f.lift; }
            p.engine().setDrag (b, sd, lf);
            io.clear(); p.processBlock (io, midi);
            for (int c = 0; c < 2; ++c) all.copyFrom (c, pos, io, c, 0, 512);
        }
        auto f = dir.getChildFile ("terrain-drag-flicks-live.wav"); f.deleteFile();
        if (auto os = f.createOutputStream()) { juce::WavAudioFormat wav; std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, 2, 24, {}, 0)); if (w) { os.release(); w->writeFromAudioSampleBuffer (all, 0, all.getNumSamples()); } }
        std::printf ("DEMO %s ok\n", f.getFullPathName().toRawUTF8());
    }
    // the built-in targets themselves, for comparison
    const char* tg[] = { "beat", "vowels", "piano" };
    for (auto* t : tg)
    {
        auto b = juce::String (t) == "beat" ? demo::beatLoop (sr) : juce::String (t) == "vowels" ? demo::vowelPhrase (sr) : demo::pianoLine (sr);
        auto f = dir.getChildFile (juce::String ("target-") + t + ".wav"); f.deleteFile();
        if (auto os = f.createOutputStream()) { juce::WavAudioFormat wav; std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, b.getNumChannels(), 24, {}, 0)); if (w) { os.release(); w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples()); } }
    }
#endif
    return 0;
}
