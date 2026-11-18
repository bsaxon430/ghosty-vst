// LAYOUT 2 (round 3): CLASSIC stays pixel-identical; STUDIO shows everything; the choice is a per-user setting.
// Included from TrapTests.cpp. The CLASSIC shots are taken on a fresh editor (system Random seeded, no message loop run, so no
// animation has moved yet) and hashed; GhostyPro/tests/ClassicShots.hash holds the hashes of the build BEFORE the STUDIO layout.
namespace layouttest
{
// the tests never read or write the user's real settings file (the LAYOUT choice): a scratch one that says CLASSIC, so the older
// tests (written for CLASSIC) see what they were written for; the STUDIO tests switch explicitly. Windows open at 100 % (no fit).
static juce::File testSettingsFile() { return juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ghostypro-test-" + juce::String ((int) getpid()) + ".settings"); }
static void writeTestLayout (const juce::File& f, const char* layout)
{
    f.replaceWithText (juce::String ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<PROPERTIES>\n  <VALUE name=\"layout\" val=\"") + layout + "\"/>\n</PROPERTIES>\n");
}
static const bool settingsRedirected = []
{
    const auto f = testSettingsFile();
    writeTestLayout (f, "classic");
    setenv ("GHOSTY_SETTINGS_FILE", f.getFullPathName().toRawUTF8(), 1);
    setenv ("GHOSTY_NO_FIT", "1", 1);
    return true;
}();
static const char* const themeNames[] { "black", "purple", "light" };
static const char* const tabNames[] { "synth", "mod", "fx", "browse", "bend", "trap", "morph" };

static juce::String imageHash (const juce::Image& img)
{
    juce::uint64 h = 0xcbf29ce484222325ull;      // FNV-1a over every pixel's ARGB
    for (int y = 0; y < img.getHeight(); ++y)
        for (int x = 0; x < img.getWidth(); ++x)
        {
            const auto c = img.getPixelAt (x, y).getARGB();
            for (int k = 0; k < 4; ++k) { h ^= (c >> (8 * k)) & 0xffu; h *= 0x100000001b3ull; }
        }
    return juce::String::toHexString ((juce::int64) h) + "-" + juce::String (img.getWidth()) + "x" + juce::String (img.getHeight());
}

static void writePng (const juce::Image& img, const juce::File& f)
{
    f.getParentDirectory().createDirectory(); f.deleteFile();
    if (auto os = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream())) juce::PNGImageFormat().writeImageToStream (img, *os);
}

// one CLASSIC shot: a fresh processor + editor, the theme and tab set, painted once
static juce::Image classicShot (int theme, int tab)
{
    seedSystemRandom (0x6a7);
    auto p = makeProc (progOf ("Trap Starter Stack"));
    seedSystemRandom (0x6a8);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());     // (the test settings say CLASSIC)
    auto* pe = dynamic_cast<ProEditor*> (ed.get());
    pe->setSize (ProEditor::W, ProEditor::H);
    pe->setTheme (theme);
    pe->showTab (tab);
    return pe->createComponentSnapshot (pe->getLocalBounds(), true, 1.0f);
}

static juce::File refFile() { return juce::File (__FILE__).getSiblingFile ("ClassicShots.hash"); }

// writes the hash list (run on the build BEFORE a change to make the reference) and, with a directory, the PNGs
static int classicRef (const juce::File& dir)
{
    juce::String out;
    for (int t = 0; t < 3; ++t)
        for (int tab = 0; tab < 7; ++tab)
        {
            const auto img = classicShot (t, tab);
            const juce::String name = juce::String (themeNames[t]) + "-" + tabNames[tab];
            out << name << " " << imageHash (img) << "\n";
            if (dir != juce::File()) writePng (img, dir.getChildFile ("classic-" + name + ".png"));
        }
    refFile().replaceWithText (out);
    printf ("%s", out.toRawUTF8());
    printf ("written %s\n", refFile().getFullPathName().toRawUTF8());
    return 0;
}

// CLASSIC renders pixel-identical to the reference (every tab, every look); a mismatch writes the shot and a diff mask
static void classicIdentity (const juce::File& dir)
{
    section ("LAYOUT: CLASSIC renders pixel-identical to the build before the STUDIO layout (every tab x BLACK / PURPLE / LIGHT)");
    juce::StringPairArray want;
    for (auto& line : juce::StringArray::fromLines (refFile().loadFileAsString()))
        if (line.trim().isNotEmpty()) want.set (line.upToFirstOccurrenceOf (" ", false, false), line.fromFirstOccurrenceOf (" ", false, false).trim());
    if (want.size() == 0) { CHECK (false, "no reference hashes in %s (run 'ProTests classicref' on the build before)", refFile().getFullPathName().toRawUTF8()); return; }
    {   // the shot itself is repeatable (else the comparison means nothing)
        const auto a = classicShot (0, 0), b = classicShot (0, 0);
        CHECK (imageHash (a) == imageHash (b), "a CLASSIC shot is repeatable (two fresh editors paint the same pixels)");
    }
    int same = 0, n = 0; juce::StringArray bad;
    for (int t = 0; t < 3; ++t)
        for (int tab = 0; tab < 7; ++tab)
        {
            const auto img = classicShot (t, tab);
            const juce::String name = juce::String (themeNames[t]) + "-" + tabNames[tab];
            ++n;
            if (imageHash (img) == want[name]) { ++same; continue; }
            bad.add (name);
            if (dir != juce::File()) writePng (img, dir.getChildFile ("classic-now-" + name + ".png"));
        }
    CHECK (same == n, "CLASSIC shots identical to the reference: %d of %d%s%s", same, n, bad.isEmpty() ? "" : "; different: ", bad.joinIntoString (", ").toRawUTF8());
}
} // namespace layouttest

namespace layouttest
{
using ghosty::GhostKnob;
namespace layout2 = gpro::layout2;
template <typename T> static std::vector<T*> findAll (juce::Component& root)
{
    std::vector<T*> v;
    std::function<void (juce::Component&)> walk = [&] (juce::Component& c) { for (auto* k : c.getChildren()) { if (auto* t = dynamic_cast<T*> (k)) v.push_back (t); walk (*k); } };
    walk (root);
    return v;
}
static bool showing (juce::Component* c, juce::Component* root) { for (; c != nullptr && c != root; c = c->getParentComponent()) if (! c->isVisible()) return false; return c == root; }
static GhostKnob* knobFor (juce::Component& root, const juce::String& id)
{
    for (auto* k : findAll<GhostKnob> (root)) if (showing (k, &root) && k->slider.getProperties()["paramId"].toString() == id) return k;
    return nullptr;
}
static juce::Component* byTooltip (juce::Component& root, const juce::String& start)
{
    for (auto* c : findAll<juce::Component> (root))
        if (auto* t = dynamic_cast<juce::TooltipClient*> (c); t != nullptr && showing (c, &root) && t->getTooltip().startsWith (start)) return c;
    return nullptr;
}
static juce::TextButton* buttonNamed (juce::Component& root, const juce::String& text)
{
    for (auto* b : findAll<juce::TextButton> (root)) if (showing (b, &root) && b->getButtonText() == text) return b;
    return nullptr;
}
static void mouse (juce::Component* c, juce::Point<float> from, juce::Point<float> to, int clicks = 1, juce::ModifierKeys mods = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier))
{
    auto src = juce::Desktop::getInstance().getMainMouseSource(); const auto now = juce::Time::getCurrentTime();
    juce::MouseEvent down (src, from, mods, 1.0f, 0, 0, 0, 0, c, c, now, from, now, clicks, false);
    juce::MouseEvent drag (src, to, mods, 1.0f, 0, 0, 0, 0, c, c, now, from, now, clicks, true);
    juce::MouseEvent up (src, to, juce::ModifierKeys(), 1.0f, 0, 0, 0, 0, c, c, now, from, now, clicks, from != to);
    c->mouseDown (down); if (from != to) c->mouseDrag (drag); c->mouseUp (up);
}
static void shot (juce::Component& c, const juce::File& f) { writePng (c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f), f); }

// STUDIO: switch, size, everything reachable, the drag-and-drop modulation hooks, the draggable displays, HOLD, readability, shots
static int studioTests (const juce::File& dir)
{
    section ("LAYOUT 2: CLASSIC by default, STUDIO per user (settings file), STUDIO shows everything, mod tiles + rings, draggable curves, HOLD, contrast, shots");
    dir.createDirectory();
    using namespace gpro;
    {   // default + the per-user setting (never in a project or a sound)
        auto p = makeProc (progOf ("Trap Starter Stack"));
        const auto sf = layout2::settingsFile(); sf.deleteFile();          // a fresh install: no settings file
        std::unique_ptr<juce::AudioProcessorEditor> e0 (p->createEditor()); auto* pe0 = dynamic_cast<ProEditor*> (e0.get());
        const bool freshStudio = pe0->layout() == 1;
        layout2::saveLayout (0);
        std::unique_ptr<juce::AudioProcessorEditor> e1 (p->createEditor()); auto* pe1 = dynamic_cast<ProEditor*> (e1.get());
        const bool defClassic = freshStudio && pe1->layout() == 0 && pe1->getWidth() == ProEditor::W;
        layout2::saveLayout (1);
        std::unique_ptr<juce::AudioProcessorEditor> e2 (p->createEditor()); auto* pe2 = dynamic_cast<ProEditor*> (e2.get());
        const bool opensStudio = pe2->layout() == 1 && pe2->getWidth() > ProEditor::W;
        juce::MemoryBlock mb; p->getStateInformation (mb);
        const bool notInState = ! juce::String::fromUTF8 ((const char*) mb.getData(), (int) mb.getSize()).containsIgnoreCase ("studio");
        layout2::saveLayout (0);
        std::unique_ptr<juce::AudioProcessorEditor> e3 (p->createEditor()); auto* pe3 = dynamic_cast<ProEditor*> (e3.get());
        CHECK (defClassic && opensStudio && pe3->layout() == 0 && notInState, "a fresh install opens STUDIO (%d); CLASSIC still selectable (%dx%d); STUDIO again (%dx%d); the project state does not carry it; settings file %s",
               (int) freshStudio, pe1->getWidth(), pe1->getHeight(), pe2->getWidth(), pe2->getHeight(), layout2::settingsFile().getFullPathName().toRawUTF8());
        // the size it opens at: a 1440 x 900 Mac screen (menu bar 25 px; with / without the Dock) and bigger screens
        const float zMac = ProEditor::fitZoomFor ({ 0, 25, 1440, 875 }, 1420, 940), zDock = ProEditor::fitZoomFor ({ 0, 25, 1440, 805 }, 1420, 940),
                    zHd = ProEditor::fitZoomFor ({ 0, 0, 1920, 1040 }, 1420, 940), zTiny = ProEditor::fitZoomFor ({ 0, 0, 1024, 600 }, 1420, 940);
        CHECK (std::abs (zMac - 0.8f) < 1e-4f && zDock <= 0.75f && zHd >= 0.95f && std::abs (zTiny - 0.6f) < 1e-4f && 940 * zMac + 120 <= 875,
               "STUDIO opens to fit: 1440x900 Mac %.0f %% (%dx%d), with the Dock %.0f %%, 1920x1080 %.0f %%, a 1024x600 screen %.0f %% (the smallest zoom)",
               zMac * 100, (int) (1420 * zMac), (int) (940 * zMac), zDock * 100, zHd * 100, zTiny * 100);
    }
    {   // CLASSIC after a trip to STUDIO paints exactly like a fresh CLASSIC window
        const auto fresh = classicShot (0, 0);
        seedSystemRandom (0x6a7);
        auto p = makeProc (progOf ("Trap Starter Stack"));
        seedSystemRandom (0x6a8);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor()); auto* pe = dynamic_cast<ProEditor*> (ed.get());
        pe->setSize (ProEditor::W, ProEditor::H); pe->setTheme (0); pe->showTab (0);
        pe->setLayout (1); const int sw = pe->getWidth();
        pe->setLayout (0);
        const auto back = pe->createComponentSnapshot (pe->getLocalBounds(), true, 1.0f);
        CHECK (imageHash (back) == imageHash (fresh) && pe->getWidth() == ProEditor::W, "CLASSIC -> STUDIO (%d wide) -> CLASSIC: identical pixels to a fresh CLASSIC window", sw);
        if (imageHash (back) != imageHash (fresh)) writePng (back, dir.getChildFile ("classic-after-studio.png"));
    }
    auto p = makeProc (progOf ("Trap Starter Stack"));
    std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
    auto* pe = dynamic_cast<ProEditor*> (ed.get());
    pe->setLayout (1); pump (100);
    auto& root = *pe->contentComponent();
    const int zoomW = pe->getWidth();
    {   // every knob of CLASSIC's SYNTH page + the MOD page's LFO / ENV / macro knobs is on the STUDIO front page
        pe->setLayout (0); pe->showTab (0); pump (20);
        juce::StringArray classicIds;
        for (auto* k : findAll<GhostKnob> (root)) if (showing (k, &root)) classicIds.addIfNotAlreadyThere (k->slider.getProperties()["paramId"].toString());
        pe->showTab (1); pump (20);
        for (auto* k : findAll<GhostKnob> (root)) if (showing (k, &root)) classicIds.addIfNotAlreadyThere (k->slider.getProperties()["paramId"].toString());
        pe->setLayout (1); pe->showTab (0); pump (50);
        juce::StringArray missing;
        for (auto& id : classicIds)
        {
            bool found = false;
            for (auto* k : findAll<GhostKnob> (root)) found |= k->slider.getProperties()["paramId"].toString() == id && k->getParentComponent() != nullptr && showing (k->getParentComponent(), &root);
            if (! found) missing.add (id);
        }
        CHECK (missing.isEmpty() && classicIds.size() > 60, "%d knobs of CLASSIC's SYNTH + MOD pages are on the STUDIO front page; missing: %s", classicIds.size(), missing.isEmpty() ? "none" : missing.joinIntoString (" ").toRawUTF8());
        juce::StringArray needButtons { "MORE", "ON", "DRAW A", "DRAW B", "DICE", "WAV", "MONO", "LEGATO", "SYNC", "RETRIG", "BOARD", "FX RACK", "MOD MATRIX", "FLOW", "HOLD", "EDITOR", "STUDIO", "SAVE", "LOAD", "INIT", "ARP" }, absent;
        for (auto& t : needButtons) if (buttonNamed (root, t) == nullptr) absent.add (t);
        const bool morphBtn = buttonNamed (root, "MORPH OFF") != nullptr || buttonNamed (root, "MORPH ON") != nullptr;
        int more = 0; for (auto* b : findAll<juce::TextButton> (root)) more += showing (b, &root) && b->getButtonText() == "MORE";
        CHECK (absent.isEmpty() && morphBtn && more == 6, "front-page buttons present (%d MORE pop-downs: OSC 1, OSC 2, NOISE, FILTER, REVERB, QUALITY); missing: %s",
               more, absent.isEmpty() ? "none" : absent.joinIntoString (" ").toRawUTF8());
        // every tab still opens its page in STUDIO
        juce::StringArray tabsOk;
        for (int tab : { 1, 4, 5, 6, 2, 3, 0 }) { pe->showTab (tab); pump (40); tabsOk.add (juce::String (tab)); }
        CHECK (tabsOk.size() == 7, "every tab opens in STUDIO (MOD, BEND, TRAP, MORPH, FX, BROWSE, back to the front page)");
    }
    {   // MODULATION in STUDIO = ProModUI's (one ring system): its tiles in STUDIO's cards drag onto STUDIO's knobs, rings on them
        auto* ui = pe->modUi();
        auto tileFor = [&] (int src) -> juce::Component*
        { juce::Component* best = nullptr; for (auto* c : findAll<juce::Component> (root)) if (showing (c, &root) && (int) c->getProperties().getWithDefault ("modTile", -1) == src) best = c; return best; };
        auto* lfoTile = tileFor (gpro::mod::lfo1);
        auto* cut = knobFor (root, "cutoff");
        int studioTiles = 0; for (auto* c : findAll<juce::Component> (root)) studioTiles += showing (c, &root) && c->getProperties().contains ("modTile");
        CHECK (ui != nullptr && lfoTile != nullptr && cut != nullptr && studioTiles >= 10, "ProModUI tiles on the STUDIO front page: %d (AMP, FILTER, ENV 2, ENV 3, LFO 1, LFO 2, M1..M4); CUTOFF knob found", studioTiles);
        if (ui != nullptr && lfoTile != nullptr && cut != nullptr)
        {
            p->modr.clear(); pump (50);
            auto centre = [] (juce::Component& c) { return c.localPointToGlobal (c.getLocalBounds().getCentre()); };
            ui->beginDrag (gpro::mod::lfo1, -1, centre (*lfoTile));
            ui->dragMoveTo (centre (cut->slider));
            const bool aimed = ui->drag().target.getComponent() == &cut->slider;
            ui->dragEnd (centre (cut->slider)); pump (100);
            const auto l = p->modr.list();
            gpro::ModUi::KnobGeom kg; const bool geom = ui->knobGeom (cut->slider, kg);
            const float mid = 0.5f * (kg.a0 + kg.a1), rr = gpro::ModUi::ringRadius (kg.rad, 0);
            const auto hit = ui->ringAt ({ kg.c.x + std::sin (mid) * rr, kg.c.y - std::cos (mid) * rr });
            shot (*pe, dir.getChildFile ("studio-black-ring-lfo1-cutoff.png"));
            CHECK (aimed && l.size() == 1 && l[0].src == gpro::mod::lfo1 && l[0].dst == "cutoff" && geom && hit.slider == &cut->slider && ui->sliderFor ("cutoff") == &cut->slider && ui->knobVisible (cut->slider),
                   "drag LFO 1 (STUDIO tile) onto STUDIO's CUTOFF: aimed %d, %d route(s), its ring is hit on the STUDIO knob (%d), the knob menu / rings use the STUDIO knob", (int) aimed, (int) l.size(), (int) (hit.slider == &cut->slider));
            p->modr.clear(); pump (50);
            // the ENVELOPES tiles also pick the envelope shown
            auto* fTile = tileFor (gpro::mod::menv);
            auto* ec = byTooltip (root, "Drag the handles");
            bool shows = false;
            if (fTile != nullptr && fTile->getParentComponent() != nullptr)
            {
                auto src = juce::Desktop::getInstance().getMainMouseSource(); const auto now = juce::Time::getCurrentTime(); const juce::Point<float> c0 (10.0f, 8.0f);
                juce::MouseEvent up (src, c0, juce::ModifierKeys(), 1.0f, 0, 0, 0, 0, fTile, fTile, now, c0, now, 1, false);
                fTile->getParentComponent()->mouseUp (up); pump (30);
                shows = knobFor (root, "f_a") != nullptr && knobFor (root, "attack") == nullptr && ec != nullptr;
                fTile->getParentComponent()->mouseUp (juce::MouseEvent (src, c0, juce::ModifierKeys(), 1.0f, 0, 0, 0, 0, tileFor (gpro::mod::env1), tileFor (gpro::mod::env1), now, c0, now, 1, false)); pump (30);
            }
            CHECK (shows && knobFor (root, "attack") != nullptr, "click the FILTER tile: the envelope display + knobs show the FILTER envelope; AMP back");
        }
    }
    {   // the draggable displays: filter curve, envelope handles, LFO
        auto* fc = byTooltip (root, "FILTER: drag");
        auto* ec = byTooltip (root, "Drag the handles");
        auto* lv = byTooltip (root, "LFO: drag");
        CHECK (fc != nullptr && ec != nullptr && lv != nullptr, "filter curve, envelope curve and LFO display found");
        if (fc != nullptr)
        {
            p->setParam ("cutoff", 0.5f); p->setParam ("res", 0.2f); pump (20);
            mouse (fc, { 100.0f, 60.0f }, { 140.0f, 30.0f }); pump (20);
            const float cut = p->apvts.getRawParameterValue ("cutoff")->load(), res = p->apvts.getRawParameterValue ("res")->load();
            CHECK (cut > 0.52f && res > 0.25f, "filter curve: drag right + up -> CUTOFF %.3f (was 0.5), RES %.3f (was 0.2)", cut, res);
        }
        if (ec != nullptr)
        {
            p->setParam ("attack", 0.01f); pump (20);
            auto* attP = p->apvts.getParameter ("attack");
            auto a = ec->getLocalBounds().toFloat().reduced (12.0f, 8.0f).withTrimmedTop (14.0f);
            const float seg = (a.getWidth() - a.getWidth() * 0.16f) / 3.0f;
            const juce::Point<float> h (a.getX() + attP->getValue() * seg, a.getY());
            const float before = attP->getValue();
            mouse (ec, h, h + juce::Point<float> (30.0f, 0.0f)); pump (20);
            CHECK (attP->getValue() > before + 0.1f, "envelope curve: the attack handle dragged 30 px right -> ATTACK %s (knob travel %.2f -> %.2f)", attP->getCurrentValueAsText().toRawUTF8(), before, attP->getValue());
            auto* curveP = p->apvts.getParameter ("curve"); const float c0 = curveP->getValue();
            const float xd = h.x + 30.0f + p->apvts.getParameter ("decay")->getValue() * seg * 0.5f;
            mouse (ec, { xd, a.getCentreY() }, { xd, a.getCentreY() + 20.0f }); pump (20);
            CHECK (curveP->getValue() > c0 + 0.05f, "envelope curve: the decay slope dragged down -> CURVE %.2f -> %.2f", c0, curveP->getValue());
        }
        if (lv != nullptr)
        {
            auto* rate = p->apvts.getParameter ("l1_rate"); const float r0 = rate->getValue();
            mouse (lv, { 50.0f, 40.0f }, { 50.0f, 10.0f }); pump (20);
            CHECK (rate->getValue() > r0 + 0.1f, "LFO display: drag up -> LFO 1 RATE %s", rate->getCurrentValueAsText().toRawUTF8());
        }
    }
    {   // the WAVE PALETTE: a click picks the table for that oscillator; AMBIENT is a per-user setting (default on)
        auto* pal = byTooltip (root, "WAVE PALETTE: click = OSC 1");
        CHECK (pal != nullptr, "WAVE PALETTE under OSC 1 found");
        if (pal != nullptr)
        {
            const int cols = 12, nTiles = gpro::eng::tableChoiceNames().size() + 1, rows = (nTiles + cols - 1) / cols;
            const float w = (float) pal->getWidth() / (float) cols, h = (float) pal->getHeight() / (float) rows;
            const juce::Point<float> tile ((float) (gpro::tPulse % cols) * w + w * 0.5f, (float) (gpro::tPulse / cols) * h + h * 0.5f);
            mouse (pal, tile, tile); pump (30);
            const int t1 = (int) p->apvts.getRawParameterValue ("o1_tbl")->load();
            const int diceI = nTiles - 1;
            const juce::Point<float> dice ((float) (diceI % cols) * w + w * 0.5f, (float) (diceI / cols) * h + h * 0.5f);
            mouse (pal, dice, dice); pump (30);
            const int t2 = (int) p->apvts.getRawParameterValue ("o1_tbl")->load();
            const juce::Point<float> engTile ((float) ((gpro::tableNames().size() + gpro::eng::eString) % cols) * w + w * 0.5f, (float) ((gpro::tableNames().size() + gpro::eng::eString) / cols) * h + h * 0.5f);
            mouse (pal, engTile, engTile); pump (30);
            const int t3 = (int) p->apvts.getRawParameterValue ("o1_tbl")->load();
            CHECK (t1 == gpro::tPulse && t2 != gpro::tPulse && t2 != gpro::tSample && gpro::eng::engineOf (t3) == gpro::eng::eString, "palette: click the PULSE tile -> OSC 1 plays %s; the dice -> %s; the STRING engine tile -> %s",
                   gpro::eng::tableChoiceNames()[t1].toRawUTF8(), gpro::eng::tableChoiceNames()[t2].toRawUTF8(), gpro::eng::tableChoiceNames()[t3].toRawUTF8());
            shot (*pe, dir.getChildFile ("studio-black-engine-string.png"));
            p->setParam ("o1_tbl", (float) gpro::tSaw); pump (20);
            juce::MouseEvent mv (juce::Desktop::getInstance().getMainMouseSource(), tile, juce::ModifierKeys(), 1.0f, 0, 0, 0, 0, pal, pal, juce::Time::getCurrentTime(), tile, juce::Time::getCurrentTime(), 0, false);
            pal->mouseMove (mv); pump (30);
            shot (*pe, dir.getChildFile ("studio-black-palette-hover.png"));
            pal->mouseExit (mv);
        }
        const bool def = layout2::ambientOn();
        layout2::saveAmbient (false); const bool off = ! layout2::ambientOn(); layout2::saveAmbient (true);
        CHECK (def && off && layout2::ambientOn(), "AMBIENT rain: on by default, the choice is saved per user");
    }
    {   // XY pad extras (STUDIO): OSC 1 / OSC 2 / BOTH for the MORPH ghost, AUTO-MOVE, click a ghost = its knobs light up
        auto* both = buttonNamed (root, "BOTH"); auto* osc2 = buttonNamed (root, "OSC 2"); auto* autoB = buttonNamed (root, "AUTO");
        ghosty::XYGhostPad* xy = nullptr; for (auto* x : findAll<ghosty::XYGhostPad> (root)) if (showing (x, &root)) xy = x;
        CHECK (both != nullptr && osc2 != nullptr && autoB != nullptr && xy != nullptr, "XY pad with OSC 1 / OSC 2 / BOTH and AUTO found");
        if (both != nullptr && autoB != nullptr && xy != nullptr)
        {
            both->setToggleState (true, juce::sendNotificationSync); pump (60);
            p->setParam ("o1_pos", 0.7f); pump (200);
            const float o2 = p->apvts.getRawParameterValue ("o2_pos")->load();
            for (auto* x : findAll<ghosty::XYGhostPad> (root)) if (showing (x, &root)) xy = x;
            auto* b1 = buttonNamed (root, "OSC 1"); b1->setToggleState (true, juce::sendNotificationSync); pump (60);
            p->setParam ("o1_pos", 0.2f); pump (200);
            const float o2b = p->apvts.getRawParameterValue ("o2_pos")->load();
            CHECK (std::abs (o2 - 0.7f) < 1e-3f && std::abs (o2b - 0.7f) < 1e-3f, "BOTH: OSC 2 POS follows OSC 1 POS (%.2f); back on OSC 1 it stays (%.2f)", o2, o2b);
            const float c0 = p->apvts.getRawParameterValue ("cutoff")->load();
            autoB->setToggleState (true, juce::sendNotificationSync); pump (900);
            const float c1 = p->apvts.getRawParameterValue ("cutoff")->load();
            autoB->setToggleState (false, juce::sendNotificationSync); pump (100);
            const float c2 = p->apvts.getRawParameterValue ("cutoff")->load(); pump (300);
            CHECK (std::abs (c1 - c0) > 1e-3f && std::abs (c1 - c0) < 0.08f && std::abs (p->apvts.getRawParameterValue ("cutoff")->load() - c2) < 1e-6f,
                   "AUTO-MOVE: the TONE ghost drifts (cutoff %.3f -> %.3f, within 0.07 of where it was), stops when off", c0, c1);
            for (auto* x : findAll<ghosty::XYGhostPad> (root)) if (showing (x, &root)) xy = x;
            auto* cutP = p->apvts.getParameter ("cutoff"); auto* resP = p->apvts.getParameter ("res");
            const juce::Point<float> gp (30.0f + cutP->getValue() * ((float) xy->getWidth() - 60.0f), 30.0f + (1.0f - resP->getValue()) * ((float) xy->getHeight() - 60.0f));
            auto src = juce::Desktop::getInstance().getMainMouseSource(); const auto now = juce::Time::getCurrentTime();
            juce::MouseEvent up (src, gp, juce::ModifierKeys(), 1.0f, 0, 0, 0, 0, xy, xy, now, gp, now, 1, false);
            xy->getParentComponent()->mouseUp (up); pump (50);
            const int lit = (int) xy->getParentComponent()->getProperties().getWithDefault ("flashing", 0);
            CHECK (lit == 2, "click the TONE ghost: its two knobs (CUTOFF, RES) light up (%d lit)", lit);
            shot (*pe, dir.getChildFile ("studio-black-ghost-click.png"));
        }
    }
    {   // the keys on the play card play the engine's keyboard (HOLD: built separately)
        juce::MidiKeyboardComponent* kb = nullptr; for (auto* k : findAll<juce::MidiKeyboardComponent> (root)) if (showing (k, &root)) kb = k;
        CHECK (kb != nullptr && kb->getWidth() > 900, "KEYS on the play card (%d px wide), FLOW strip one click away", kb != nullptr ? kb->getWidth() : 0);
    }
    {   // readable + clickable in every look; screenshots of every state
        static const char* tabName[] { "front", "mod", "fx", "browse", "bend", "trap", "morph" };
        int bad = 0, clickBad = 0, checked = 0, clicks = 0;
        render (*p, { { 0.0, 48, 0.9f, true }, { 0.0, 60, 0.9f, true } }, 0.3);
        pump (3800);      // (the toast of the drop test fades first: it would cover what it overlaps)
        for (int t = 0; t < 3; ++t)
        {
            pe->setTheme (t); pump (60);
            for (int tab : { 0, 1, 2, 3, 4, 5, 6 })
            {
                pe->showTab (tab); pump (tab == 0 ? 300 : 120);
                const juce::String nm = juce::String ("studio-") + themeNames[t] + "-" + tabName[tab];
                shot (*pe, dir.getChildFile (nm + ".png"));
                if (tab != 0) continue;
                auto issues = contrastAudit (*pe, root, checked);
                auto cb = clickAudit (*pe, root, clicks);
                int b = 0; for (auto& i : issues) if (i.enabled || i.ratio < 3.0) { ++b; if (b <= 12) printf ("        %s text %.2f:1 %s %s\n", themeNames[t], i.ratio, i.enabled ? "" : "(greyed)", i.what.toRawUTF8()); }
                for (int k = 0; k < juce::jmin (12, cb.size()); ++k) printf ("        %s click %s\n", themeNames[t], cb[k].toRawUTF8());
                bad += b; clickBad += cb.size();
            }
        }
        pe->setTheme (0); pe->showTab (0);
        CHECK (bad == 0, "STUDIO front page text contrast in BLACK / PURPLE / LIGHT: %d controls checked, %d below 4.5:1 (greyed 3:1)", checked, bad);
        CHECK (clickBad == 0, "STUDIO front page: %d controls, a click at each one's centre reaches it (%d do not)", clicks, clickBad);
        // the MORE pop-downs, LIGHT
        pe->setTheme (2); pump (50);
        shot (*pe, dir.getChildFile ("studio-light-front.png"));
        pe->setTheme (0);
    }
    {   // every control on the STUDIO front page says what it does (tooltip); the displays take keyboard focus
        pe->setLayout (1); pe->showTab (0); pump (50);
        juce::StringArray noTip; int controls = 0;
        for (auto* c : findAll<juce::Component> (root))
        {
            if (! showing (c, &root)) continue;
            const bool ctl = dynamic_cast<juce::Button*> (c) || dynamic_cast<juce::ComboBox*> (c) || dynamic_cast<juce::Slider*> (c);
            if (! ctl || dynamic_cast<juce::ComboBox*> (c->getParentComponent()) || dynamic_cast<juce::Slider*> (c->getParentComponent())) continue;
            ++controls;
            auto* tc = dynamic_cast<juce::TooltipClient*> (c);
            if (tc == nullptr || tc->getTooltip().isEmpty()) noTip.add ((dynamic_cast<juce::Button*> (c) ? dynamic_cast<juce::Button*> (c)->getButtonText() : c->getName()) + "@" + c->getBounds().getPosition().toString());
        }
        int focusable = 0; for (auto* c : findAll<juce::Component> (root)) focusable += showing (c, &root) && c->getWantsKeyboardFocus() && (c->getTitle() == "Filter curve" || c->getTitle() == "LFO display");
        CHECK (noTip.size() <= controls / 10 && focusable == 3, "STUDIO front page: %d controls, %d without a tooltip%s%s; filter curve + 2 LFO displays take keyboard focus (%d)",
               controls, noTip.size(), noTip.isEmpty() ? "" : ": ", noTip.joinIntoString (" ").substring (0, 300).toRawUTF8(), focusable);
    }
    {   // any window size: one uniform zoom (nothing re-flows, so nothing can overlap), limits 60 .. 160 %
        juce::String got; bool ok = true;
        for (float z : { 0.6f, 0.8f, 1.0f, 1.6f })
        {
            pe->setSize (juce::roundToInt (1420 * z), juce::roundToInt (940 * z)); pump (10);
            const float sc = pe->contentComponent()->getTransform().mat00;
            ok &= std::abs (sc - (float) pe->getWidth() / 1420.0f) < 1e-3f && std::abs (pe->contentComponent()->getTransform().mat11 - sc) < 1e-6f;
            got << juce::String (z * 100, 0) << "% -> " << pe->getWidth() << "x" << pe->getHeight() << "  ";
        }
        auto* cons = pe->getConstrainer();
        ok &= cons != nullptr && cons->getMinimumWidth() == 1420 * 6 / 10 && cons->getMaximumWidth() == 1420 * 8 / 5;
        CHECK (ok, "STUDIO scales as one piece at every size: %s; limits %d .. %d wide, aspect fixed", got.toRawUTF8(), cons ? cons->getMinimumWidth() : 0, cons ? cons->getMaximumWidth() : 0);
        pe->setSize (1420, 940);
    }
    {   // zoom: the window keeps its zoom when the layout changes; limits follow
        pe->setLayout (0); pe->setSize (ProEditor::W * 3 / 4, ProEditor::H * 3 / 4); pump (20);
        pe->setLayout (1); pump (20);
        const float zoom = (float) pe->getWidth() / (float) pe->contentComponent()->getWidth();
        CHECK (std::abs (zoom - 0.75f) < 0.01f && zoomW > ProEditor::W, "zoom kept across the switch: CLASSIC at 75 %% -> STUDIO at %.0f %% (%dx%d)", zoom * 100, pe->getWidth(), pe->getHeight());
    }
    return 0;
}
} // namespace layouttest
