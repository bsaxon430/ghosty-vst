// GHOST LOOK: Ghosty Pro's 4th look (liquid glass on the LIGHT base; GhostyPro/ProGhostLook.h). Included from TrapTests.cpp.
//   ProTests ghostref <hashfile>        the old looks' shots (BLACK / PURPLE / LIGHT x 7 tabs, CLASSIC + STUDIO) hashed; run on the build
//                                       BEFORE the GHOST look to make the reference (the look button's own rect is masked: its label names
//                                       the NEXT look, which is GHOST after LIGHT now - the one intended difference)
//   ProTests ghostlook [shots] [hash]   cycle / persistence / old sessions / old looks identical (vs the reference + after visiting GHOST) /
//                                       pixel contrast / tooltips / aurora + panel caches / paint time / GHOST screenshots
namespace ghostlooktest
{
static const char* const lookNames[] { "black", "purple", "light", "ghost" };
static const char* const tabNames[] { "synth", "mod", "fx", "browse", "bend", "trap", "morph" };

static juce::Component* findNamed (juce::Component& root, const juce::String& name)
{
    for (auto* k : root.getChildren())
    {
        if (k->getName() == name && k->isVisible()) return k;
        if (auto* f = findNamed (*k, name)) return f;
    }
    return nullptr;
}
// the editor's look button rect (masked in the identity hashes)
static juce::Rectangle<int> lookButtonRect (juce::Component& ed)
{
    auto* b = findNamed (ed, "THEME");
    return b != nullptr ? ed.getLocalArea (b->getParentComponent(), b->getBounds()) : juce::Rectangle<int>();
}
static juce::Image masked (juce::Image img, juce::Rectangle<int> r)
{
    if (! r.isEmpty()) { juce::Graphics g (img); g.setColour (juce::Colours::magenta); g.fillRect (r); }
    return img;
}

// one shot of a fresh editor (system Random seeded, no message loop run): look, layout (0 CLASSIC, 1 STUDIO), tab
static juce::Image lookShot (int look, int layout, int tab, int visitFirst = -1)
{
    seedSystemRandom (0x6a7);
    auto p = makeProc (progOf ("Trap Starter Stack"));
    seedSystemRandom (0x6a8);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
    auto* pe = dynamic_cast<ProEditor*> (ed.get());
    pe->setSize (ProEditor::W, ProEditor::H);
    if (layout == 1) pe->setLayout (1);
    pe->showTab (tab);
    if (visitFirst >= 0) { pe->setTheme (visitFirst); (void) pe->createComponentSnapshot (pe->getLocalBounds(), true, 1.0f); }
    pe->setTheme (look);
    // (the header logo next to the PRO badge is new in both headers: masked like the look button, it is not part of what this test pins)
    return masked (masked (pe->createComponentSnapshot (pe->getLocalBounds(), true, 1.0f), lookButtonRect (*pe)), { 210, 2, 38, 62 });
}
static juce::String shotName (int look, int layout, int tab) { return juce::String (lookNames[look]) + "-" + (layout == 1 ? "studio" : "classic") + "-" + tabNames[tab]; }

static int ghostRef (const juce::File& hashFile, const juce::File& pngDir)
{
    juce::String out;
    for (int layout = 0; layout < 2; ++layout)
        for (int look = 0; look < 3; ++look)
            for (int tab = 0; tab < 7; ++tab)
            {
                const auto img = lookShot (look, layout, tab);
                out << shotName (look, layout, tab) << " " << layouttest::imageHash (img) << "\n";
                if (pngDir != juce::File()) layouttest::writePng (img, pngDir.getChildFile ("ref-" + shotName (look, layout, tab) + ".png"));
            }
    hashFile.getParentDirectory().createDirectory();
    hashFile.replaceWithText (out);
    printf ("%swritten %s\n", out.toRawUTF8(), hashFile.getFullPathName().toRawUTF8());
    return 0;
}

// GHOST screenshots: SYNTH, MOD, BEND, TRAP, MORPH, FX, BROWSE (CLASSIC) and the STUDIO layout, at the default size
static int ghostShots (const juce::File& dir)
{
    dir.createDirectory();
    auto p = makeProc (progOf ("Trap Starter Stack"));
    auto wav = makeTestSample(); juce::String msg; p->importTrapSample (wav, msg);
    p->setParam ("ly3_src", (float) gpro::lsSample); settle (*p);
    render (*p, { { 0.0, 48, 0.9f, true }, { 0.0, 60, 0.9f, true }, { 0.0, 67, 0.9f, true } }, 0.4);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
    auto* pe = dynamic_cast<ProEditor*> (ed.get());
    pe->setSize (ProEditor::W, ProEditor::H); ed->setVisible (true);
    const int look = std::getenv ("GP_SHOTLOOK") != nullptr ? juce::String (std::getenv ("GP_SHOTLOOK")).getIntValue() : 3;   // (debug: another look)
    pe->setTheme (look); pump (100);
    int n = 0;
    const int order[] { 0, 1, 4, 5, 6, 2, 3 };
    for (int tab : order)
    {
        pe->showTab (tab); pump (300);
        layouttest::writePng (pe->createComponentSnapshot (pe->getLocalBounds(), true, 1.0f), dir.getChildFile (juce::String ("pro-ghost-") + tabNames[tab] + ".png")); ++n;
    }
    pe->setLayout (1); pe->showTab (0); pump (400);
    layouttest::writePng (pe->createComponentSnapshot (pe->getLocalBounds(), true, 1.0f), dir.getChildFile ("pro-ghost-studio.png")); ++n;
    pe->setLayout (0); pe->setTheme (0);
    return n;
}

// ---------------------------------------------------------------- pixel contrast helpers
// the worst contrast of `text` (alpha composited over each pixel) against every pixel of `area` in img
static double worstContrastIn (const juce::Image& img, juce::Rectangle<int> area, juce::Colour text)
{
    area = area.getIntersection (img.getBounds());
    double worst = 99.0;
    const juce::Image::BitmapData bd (img, juce::Image::BitmapData::readOnly);
    for (int y = area.getY(); y < area.getBottom(); ++y)
        for (int x = area.getX(); x < area.getRight(); ++x)
        {
            const auto px = bd.getPixelColour (x, y).withAlpha (1.0f);
            worst = std::min (worst, ghosty::themes::contrast (px.overlaidWith (text), px));
        }
    return worst;
}
// the box a line of text of width tw and font f takes when drawn with justification j inside r
static juce::Rectangle<int> textBox (juce::Rectangle<int> r, const juce::Font& f, const juce::String& text, juce::Justification j)
{
    const int tw = juce::jmin (r.getWidth(), (int) std::ceil (ghosty::textWidth (f, text))), th = juce::jmin (r.getHeight(), (int) std::ceil (f.getAscent()));
    auto b = juce::Rectangle<int> (tw, th).withCentre (r.getCentre());
    if (j.testFlags (juce::Justification::left)) b.setX (r.getX());
    if (j.testFlags (juce::Justification::right)) b.setX (r.getRight() - tw);
    return b;
}
static bool showingIn (juce::Component* c, juce::Component* root) { for (; c != nullptr && c != root; c = c->getParentComponent()) if (! c->isVisible()) return false; return c == root; }
template <typename T> static std::vector<T*> allOf (juce::Component& root)
{
    std::vector<T*> v;
    std::function<void (juce::Component&)> walk = [&] (juce::Component& c) { for (auto* k : c.getChildren()) { if (auto* t = dynamic_cast<T*> (k)) v.push_back (t); walk (*k); } };
    walk (root);
    return v;
}
struct Worst
{
    double v = 99; juce::String what; int n = 0;
    void add (double c, const juce::String& w) { ++n; if (c < v) { v = c; what = w; } }
};
// every showing Label (captions, value boxes, combo texts, readouts) and every ProLnF TextButton of the editor: the text blanked, the pixels
// under it snapshotted, the worst contrast of its colour there. Enabled text >= 4.5:1; disabled (greyed on purpose) >= 3:1
static void componentContrast (ProEditor& pe, Worst& text, Worst& greyed, const juce::String& where)
{
    auto snapArea = [&] (juce::Rectangle<int> r) { return pe.createComponentSnapshot (r, true, 1.0f); };
    for (auto* l : allOf<juce::Label> (pe))
    {
        if (! showingIn (l, &pe) || l->getText().trim().isEmpty() || l->isBeingEdited()) continue;
        const auto R = pe.getLocalArea (l->getParentComponent(), l->getBounds()).getIntersection (pe.getLocalBounds());
        if (R.getWidth() < 6 || R.getHeight() < 6) continue;
        auto col = l->findColour (juce::Label::textColourId);
        if (col.getAlpha() < 8) continue;
        if (! l->isEnabled()) col = col.withMultipliedAlpha (0.8f);
        const bool valueBox = dynamic_cast<juce::Slider*> (l->getParentComponent()) != nullptr;
        const auto f = l->getLookAndFeel().getLabelFont (*l);
        const auto local = valueBox ? l->getLocalBounds().reduced (4, 1) : l->getLookAndFeel().getLabelBorderSize (*l).subtractedFrom (l->getLocalBounds());
        auto tb = textBox (local, f, l->getText(), valueBox ? juce::Justification::centred : l->getJustificationType());
        tb = pe.getLocalArea (l, tb);
        const auto keep = l->getText();
        l->setText ({}, juce::dontSendNotification);
        const auto img = snapArea (R);
        l->setText (keep, juce::dontSendNotification);
        const double c = worstContrastIn (img, tb.translated (-R.getX(), -R.getY()), col);
        (l->isEnabled() ? text : greyed).add (c, where + " label '" + keep + "' #" + col.toDisplayString (false));
    }
    for (auto* b : allOf<juce::TextButton> (pe))
    {
        if (! showingIn (b, &pe) || b->getButtonText().trim().isEmpty() || dynamic_cast<ProLnF*> (&b->getLookAndFeel()) == nullptr) continue;
        const auto R = pe.getLocalArea (b->getParentComponent(), b->getBounds()).getIntersection (pe.getLocalBounds());
        if (R.getWidth() < 8 || R.getHeight() < 8) continue;
        auto col = b->findColour (b->getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId);
        if (! b->isEnabled()) col = col.withMultipliedAlpha (0.8f);
        const auto f = b->getLookAndFeel().getTextButtonFont (*b, b->getHeight());
        auto tb = pe.getLocalArea (b, textBox (b->getLocalBounds().reduced (4, 2), f, b->getButtonText(), juce::Justification::centred));
        const auto keep = b->getButtonText();
        b->setButtonText ({});
        const auto img = snapArea (R);
        b->setButtonText (keep);
        const double c = worstContrastIn (img, tb.translated (-R.getX(), -R.getY()), col);
        (b->isEnabled() ? text : greyed).add (c, where + " button '" + keep + "' #" + col.toDisplayString (false));
    }
}

static int ghostLookTests (const juce::File& shots, const juce::File& hashFile)
{
    namespace th = ghosty::themes;
    section ("GHOST LOOK: the 4th look (liquid glass on LIGHT) - cycle, persistence, old sessions, old looks identical, pixel contrast, tooltips, caches, paint time, shots");
    const auto& G = pt::ghostPal();
    const auto& S = pt::ghostScreenPal();
    {   // tokens: text >= 4.5:1 on the glass at its darkest over the aurora and on the flat tokens; screen palette on the glass screens
        double body = 99, acc = 99; juce::String worst;
        auto refs = pghost::glassRefs(); for (auto c : { G.bg, G.card, G.card2, G.deep }) refs.push_back (c);
        for (auto bgc : refs)
        {
            body = std::min ({ body, contrastOf (G.white, bgc), contrastOf (G.dim, bgc) });
            for (auto a : { G.slime, G.ecto, G.pink, G.ice, G.gold, G.red, G.blue, G.violet })
                if (contrastOf (a, bgc) < acc) { acc = contrastOf (a, bgc); worst = a.toDisplayString (false) + " on #" + bgc.toDisplayString (false); }
        }
        double scr = 99;
        for (auto bgc : { pghost::screenTop(), pghost::screenBottom() })
            for (auto a : { S.white, S.dim, S.slime, S.ecto, S.pink, S.ice, S.gold, S.red, S.blue, S.violet }) scr = std::min (scr, contrastOf (a, bgc));
        const double lit = contrastOf (G.onAccent, ghosty::glass::violetDeep());
        CHECK (body >= 4.5 && acc >= 4.5 && scr >= 4.5 && lit >= 4.5 && G.theme == th::Theme::Light,
               "GHOST palette: ink / secondary %.2f:1, accents as text %.2f:1 (lowest %s), screen text + curves %.2f:1, white on violet glass %.2f:1", body, acc, worst.toRawUTF8(), scr, lit);
        CHECK (pt::paletteFor (th::Theme::Light).white != G.white && ! pghost::on(), "LIGHT keeps its own palette (GHOST is a separate instance; the shared theme enum is untouched)");
        CHECK (th::fromString ("ghost") == th::Theme::Dark && th::fromInt (3) == th::Theme::Dark && pt::lookFromInt (3) == 3 && pt::lookFromInt (4) == 0 && pt::lookFromInt (-1) == 0,
               "an older build reads 'ghost' / 3 as BLACK; this build: 3 = GHOST, anything unknown = BLACK");
    }

    // ---- the cycle: BLACK -> PURPLE -> LIGHT -> GHOST -> BLACK
    {
        auto q = makeProc (0);
        std::unique_ptr<juce::AudioProcessorEditor> e2 (q->createEditor()); e2->setSize (ProEditor::W, ProEditor::H); pump (50);
        auto* pe2 = dynamic_cast<ProEditor*> (e2.get());
        auto* themeBtn = dynamic_cast<juce::Button*> (findNamed (*e2, "THEME"));
        juce::String seq;
        if (themeBtn != nullptr) for (int k = 0; k < 4; ++k) { themeBtn->triggerClick(); pump (30); seq << lookNames[pe2->themeIndex()] << "(" << q->uiTheme.load() << ") "; }
        CHECK (seq.trim() == "purple(1) light(2) ghost(3) black(0)", "the look button cycles BLACK -> PURPLE -> LIGHT -> GHOST -> BLACK and marks the project: %s", seq.toRawUTF8());
        const auto tip = themeBtn != nullptr ? themeBtn->getTooltip() : juce::String();
        CHECK (tip.contains ("BLACK -> PURPLE -> LIGHT -> GHOST") && tip.contains ("next"), "the look button's tooltip names the four looks: \"%s\"", tip.toRawUTF8());
        pe2->setTheme (3); pe2->createComponentSnapshot (pe2->getLocalBounds(), true, 1.0f);
        CHECK (pghost::on() && pt::pal().theme == th::Theme::Light && pe2->isLightTheme() && pt::pal().white == G.white, "GHOST = the LIGHT theme + the glass + the GHOST palette while it paints");
        const auto& L = pe2->getLookAndFeel();
        const double tipC = contrastOf (L.findColour (juce::TooltipWindow::textColourId), L.findColour (juce::TooltipWindow::backgroundColourId));
        CHECK (tipC >= 4.5, "GHOST tooltips: text %.2f:1 on their card", tipC);
        // two windows: the one painting sets the look
        auto q2 = makeProc (0);
        std::unique_ptr<juce::AudioProcessorEditor> e3 (q2->createEditor()); e3->setSize (ProEditor::W, ProEditor::H);
        auto* pe3 = dynamic_cast<ProEditor*> (e3.get()); pe3->setTheme (0);
        pe3->createComponentSnapshot ({ 0, 0, 10, 10 }, true, 1.0f); const int a = pt::currentLook();
        pe2->createComponentSnapshot ({ 0, 0, 10, 10 }, true, 1.0f); const int b = pt::currentLook();
        CHECK (a == 0 && b == 3, "two windows (BLACK + GHOST): each paints in its own look (%d, %d)", a, b);
        pe2->setTheme (0); pe3->setTheme (0);
    }

    // ---- persistence + old sessions
    {
        auto q = makeProc (0);
        auto stateWith = [&] (int t) { q->uiTheme.store (t); juce::MemoryBlock mb; q->getStateInformation (mb); return mb; };
        int ok = 0;
        for (int t = 0; t < 4; ++t) { auto mb = stateWith (t); auto r = makeProc (0); r->uiTheme.store (t == 0 ? 2 : 0); r->setStateInformation (mb.getData(), (int) mb.getSize()); ok += r->uiTheme.load() == t; }
        auto mb = stateWith (3);
        auto xml = Peek::getXmlFromBinary (mb.getData(), (int) mb.getSize());
        const auto saved = xml->getStringAttribute ("theme_name");
        juce::String olds; int oldOk = 0;
        const std::pair<const char*, int> cases[] { { "black", 0 }, { "dark", 0 }, { "purple", 1 }, { "light", 2 }, { "2", 2 }, { "zzz", 0 }, { "7", 0 }, { "GHOST", 3 } };
        for (auto& [name, want] : cases)
        {
            xml->setAttribute ("theme_name", name);
            juce::MemoryBlock m2; Peek::copyXmlToBinary (*xml, m2);
            auto r = makeProc (0); r->uiTheme.store (1); r->setStateInformation (m2.getData(), (int) m2.getSize());
            oldOk += r->uiTheme.load() == want; olds << name << "->" << r->uiTheme.load() << " ";
        }
        xml->removeAttribute ("theme_name"); xml->setAttribute ("uiTheme", "light");
        juce::MemoryBlock legacyLight; Peek::copyXmlToBinary (*xml, legacyLight);
        xml->removeAttribute ("uiTheme");
        juce::MemoryBlock legacyNone; Peek::copyXmlToBinary (*xml, legacyNone);
        auto r1 = makeProc (0); r1->setStateInformation (legacyLight.getData(), (int) legacyLight.getSize());
        auto r2 = makeProc (0); r2->uiTheme.store (3); r2->setStateInformation (legacyNone.getData(), (int) legacyNone.getSize());
        CHECK (ok == 4 && saved == "ghost" && oldOk == 8 && r1->uiTheme.load() == 2 && r2->uiTheme.load() == 0,
               "round trip keeps all four looks (%d of 4; GHOST saved as theme_name=\"%s\"); old sessions load unchanged: %s; legacy LIGHT / none -> %d / %d",
               ok, saved.toRawUTF8(), olds.trim().toRawUTF8(), r1->uiTheme.load(), r2->uiTheme.load());
        q->uiTheme.store (3); q->saveUserPreset ("_ghostlook_test");
        const auto f = ProProcessor::userPresetDir().getChildFile ("_ghostlook_test.ghostypro");
        auto u = makeProc (0); u->uiTheme.store (3); u->loadUserPreset (f); for (int k : { 0, 7 }) u->setCurrentProgram (k); u->initPatch();
        CHECK (! f.loadFileAsString().contains ("theme_name") && u->uiTheme.load() == 3, "sounds never carry the look; sounds / programs / INIT keep GHOST");
        f.deleteFile();
        auto w = makeProc (0); w->uiTheme.store (3);
        std::unique_ptr<juce::AudioProcessorEditor> e4 (w->createEditor());
        CHECK (dynamic_cast<ProEditor*> (e4.get())->themeIndex() == 3, "a project saved in GHOST opens in GHOST");
        e4.reset();
        pt::setTheme (th::Theme::Dark);
    }

    // ---- the old looks stay pixel-identical: vs the reference made on the build before (look button masked), and after visiting GHOST
    {
        juce::StringPairArray want;
        for (auto& line : juce::StringArray::fromLines (hashFile.loadFileAsString()))
            if (line.trim().isNotEmpty()) want.set (line.upToFirstOccurrenceOf (" ", false, false), line.fromFirstOccurrenceOf (" ", false, false).trim());
        if (want.size() == 0) CHECK (false, "no reference hashes in %s (run 'ProTests ghostref' on the build before)", hashFile.getFullPathName().toRawUTF8());
        else
        {
            int same = 0, n = 0; juce::StringArray bad;
            for (int layout = 0; layout < 2; ++layout)
                for (int look = 0; look < 3; ++look)
                    for (int tab = 0; tab < 7; ++tab)
                    {
                        const auto img = lookShot (look, layout, tab);
                        ++n;
                        if (layouttest::imageHash (img) == want[shotName (look, layout, tab)]) ++same;
                        else { bad.add (shotName (look, layout, tab)); layouttest::writePng (img, shots.getChildFile ("diff-now-" + shotName (look, layout, tab) + ".png")); }
                    }
            CHECK (same == n, "BLACK / PURPLE / LIGHT, CLASSIC + STUDIO, every tab: %d of %d shots identical to the build before (the look button masked: LIGHT's names GHOST now)%s%s",
                   same, n, bad.isEmpty() ? "" : "; different: ", bad.joinIntoString (", ").toRawUTF8());
        }
        int same = 0, n = 0;
        for (int layout = 0; layout < 2; ++layout)
            for (int look = 0; look < 3; ++look)
                for (int tab : { 0, 2, 3 })
                { ++n; same += layouttest::imageHash (lookShot (look, layout, tab, 3)) == layouttest::imageHash (lookShot (look, layout, tab, look == 0 ? 1 : 0)); }
        // (a fresh window that painted once in another old look is the control: any first paint moves the animations' random state)
        CHECK (same == n, "after visiting GHOST, BLACK / PURPLE / LIGHT paint the same pixels as after visiting another old look (%d of %d)", same, n);
        pt::setTheme (th::Theme::Dark);
    }

    // ---- GHOST pixel contrast (rendered): every label / value box / combo text / button on every tab (CLASSIC + STUDIO), tab names,
    //      card titles in every accent on glass panels over the aurora, the readouts painted straight on the aurora
    auto p = makeProc (progOf ("Trap Starter Stack"));
    std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
    auto* pe = dynamic_cast<ProEditor*> (ed.get());
    pe->setSize (ProEditor::W, ProEditor::H); ed->setVisible (true);
    pe->setTheme (3); pump (100);
    {
        Worst text, greyed, tabs;
        for (int layout = 0; layout < 2; ++layout)
        {
            pe->setLayout (layout);
            for (int tab : layout == 0 ? std::vector<int> { 0, 1, 2, 3, 4, 5, 6 } : std::vector<int> { 0, 1, 2, 3 })
            {
                pe->showTab (tab); pump (150);
                const juce::String where = juce::String (layout ? "studio-" : "") + tabNames[tab];
                componentContrast (*pe, text, greyed, where);
                pghost::hideTextForTests() = true;
                const auto img = pe->createComponentSnapshot ({ 0, 0, 7 * 115 + 16, 112 }, true, 1.0f);
                pghost::hideTextForTests() = false;
                static const char* names[] { "SYNTH", "MOD", "BEND", "TRAP", "MORPH", "FX", "BROWSE" };
                static const int ids[] { 0, 1, 4, 5, 6, 2, 3 };
                for (int i = 0; i < 7; ++i)
                {
                    const bool open = ids[i] == tab;
                    const auto r = juce::Rectangle<int> (16 + i * 115, 76, 110, 32).withTrimmedLeft (open ? 22 : 0);
                    tabs.add (worstContrastIn (img, textBox (r, ghosty::font (12.5f), names[i], juce::Justification::centred), open ? G.onAccent : G.white), where + " tab " + names[i]);
                }
            }
        }
        pe->setLayout (0);
        CHECK (text.v >= 4.5 && text.n > 300, "GHOST text on the rendered glass: %d labels / value boxes / combo texts / buttons, worst %.2f:1 (%s)", text.n, text.v, text.what.toRawUTF8());
        CHECK (greyed.n == 0 || greyed.v >= 3.0, "GHOST greyed (disabled) text: %d items, worst %.2f:1 (%s)", greyed.n, greyed.v, greyed.what.toRawUTF8());
        CHECK (tabs.v >= 4.5, "GHOST tab names (white on the open violet tab, ink on glass): worst %.2f:1 (%s)", tabs.v, tabs.what.toRawUTF8());
    }
    {   // card titles: a glass panel over the aurora at many places of the STUDIO / CLASSIC windows, each accent as the title colour
        Worst titles;
        for (auto size : { juce::Point<int> (1140, 866), juce::Point<int> (1420, 940) })
        {
            juce::Image img (juce::Image::RGB, size.x, size.y, true);
            juce::Graphics g (img);
            pghost::backdrop (g, size.x, size.y);
            for (int cy = 112; cy < size.y - 120; cy += 118)
                for (int cx = 16; cx < size.x - 300; cx += 236)
                    pghost::card (g, juce::Rectangle<float> ((float) cx, (float) cy, 260.0f, 104.0f), 12, G.slime);
            for (int cy = 112; cy < size.y - 120; cy += 118)
                for (int cx = 16; cx < size.x - 300; cx += 236)
                {
                    const auto band = juce::Rectangle<int> (cx, cy, 260, 104).reduced (12, 0).removeFromTop (22).translated (0, 2);
                    for (auto a : { G.slime, G.ecto, G.pink, G.ice, G.gold, G.red, G.violet, G.white, G.dim })
                        titles.add (worstContrastIn (img, band.withWidth (120).withSizeKeepingCentre (120, 10), a), "#" + a.toDisplayString (false) + " at " + juce::String (cx) + "," + juce::String (cy));
                }
            // the readouts painted on the aurora itself: VOICES / UNI, the version, the MORPH ON note
            juce::Image bare (juce::Image::RGB, size.x, size.y, true);
            { juce::Graphics g2 (bare); pghost::backdrop (g2, size.x, size.y); }
            titles.add (worstContrastIn (bare, { size.x - 16 - 300 + 80, 86, 140, 10 }, G.dim), "VOICES readout (dim on the aurora)");
            titles.add (worstContrastIn (bare, { 165, 52, 40, 8 }, G.dim), "version (dim on the aurora)");
            titles.add (worstContrastIn (bare, { 16 + 7 * 115 + 6, 86, 160, 10 }, G.pink), "MORPH ON note (pink on the aurora)");
        }
        CHECK (titles.v >= 4.5, "GHOST card titles (every accent) on glass panels over the aurora + readouts on the aurora: %d probes, worst %.2f:1 (%s)", titles.n, titles.v, titles.what.toRawUTF8());
    }

    // ---- ghost characters stay legible: the pad uses LIGHT's outlined glass backdrop; the FLOW cursor ghost gets an ink outline
    {
        const auto* bd = progl::padBackdrop (pt::pal().theme);
        CHECK (pghost::on() && bd != nullptr && bd == progl::padBackdrop (th::Theme::Light) && ! bd->ghostOutline.isTransparent(),
               "GHOST pad: the pale glass backdrop of the glass LIGHT (ghosts outlined, colours deepened: tested in 'glass')");
        // the pad ghosts (round-3 art, rimmed on light backdrops) paint exactly as on the glass LIGHT pad, whose legibility 'glass' + 'padghosts' measure
        if (auto* pad = allOf<ghosty::XYGhostPad> (*ed).empty() ? nullptr : allOf<ghosty::XYGhostPad> (*ed).front())
        {
            pe->showTab (0); pump (50);
            const auto a = pad->createComponentSnapshot (pad->getLocalBounds(), true, 1.0f);
            pe->setTheme (2); const auto b = pad->createComponentSnapshot (pad->getLocalBounds(), true, 1.0f); pe->setTheme (3);
            int d = 0; for (int y = 0; y < a.getHeight(); ++y) for (int x = 0; x < a.getWidth(); ++x) d += a.getPixelAt (x, y) != b.getPixelAt (x, y);
            layouttest::writePng (a, shots.getChildFile ("pro-ghost-pad.png"));
            CHECK (d == 0, "GHOST pad ghosts paint the same pixels as on the glass LIGHT pad (%d differ): outlined / rimmed, legible on the glass", d);
        }
        auto* cur = dynamic_cast<keysx::GhostCursor*> (findNamed (*ed, "GHOST CURSOR"));
        int inkG = 0, inkL = 0;
        if (cur != nullptr)
        {
            auto count = [&]
            {
                cur->hoverForTest ({ 60.0f, 30.0f }); for (int k = 0; k < 40; ++k) cur->stepForTest();
                const auto c = cur->ghostPos();
                auto img = cur->createComponentSnapshot (cur->getLocalBounds(), true, 1.0f);
                int n = 0;
                for (int y = (int) c.y - 14; y < (int) c.y + 10; ++y) for (int x = (int) c.x - 12; x < (int) c.x + 12; ++x)
                    if (img.getBounds().contains (x, y)) { const auto px = img.getPixelAt (x, y); n += px.getAlpha() > 100 && th::relLum (px) < 0.06 && px.getBlue() > px.getGreen(); }
                return n;
            };
            inkG = count(); pe->setTheme (2); pump (20); inkL = count(); pe->setTheme (3); pump (20);
            cur->leaveForTest();
        }
        CHECK (cur != nullptr && inkG > inkL + 8, "the FLOW cursor ghost is outlined in GHOST (%d ink-outline pixels vs %d in LIGHT)", inkG, inkL);
    }

    // ---- caches: the aurora once per size, the panels once per size; no rebuild on a repaint
    {
        auto& bk = pghost::backdropCache(); auto& pc = pghost::cache();
        pe->showTab (0); pump (50);
        pe->createComponentSnapshot (pe->getLocalBounds(), true, 1.0f);
        const int b0 = bk.builds, p0 = pc.builds;
        for (int k = 0; k < 3; ++k) pe->createComponentSnapshot (pe->getLocalBounds(), true, 1.0f);
        const int b1 = bk.builds, p1 = pc.builds;
        pe->setSize (ProEditor::W * 6 / 5, ProEditor::H * 6 / 5); pump (20);
        pe->createComponentSnapshot (pe->getLocalBounds(), true, 1.0f); pe->createComponentSnapshot (pe->getLocalBounds(), true, 1.0f);
        const int b2 = bk.builds;
        pe->setSize (ProEditor::W, ProEditor::H); pump (20);
        CHECK (b1 == b0 && p1 == p0 && b2 - b1 <= 1, "aurora + glass panels cached: 3 repaints rebuilt %d aurora / %d panel images; a 120 %% resize rebuilt the aurora %d time(s)", b1 - b0, p1 - p0, b2 - b1);
    }

    // ---- paint time: a full repaint in GHOST vs LIGHT (SYNTH, CLASSIC and STUDIO) within 1.5x
    {
        auto timeIt = [&] (int look, int layout)
        {
            pe->setLayout (layout); pe->setTheme (look); pe->showTab (0); pump (80);
            for (int k = 0; k < 3; ++k) pe->createComponentSnapshot (pe->getLocalBounds(), true, 1.0f);
            double best = 1e9;
            for (int rep = 0; rep < 3; ++rep)
            {
                const auto t0 = juce::Time::getMillisecondCounterHiRes();
                for (int k = 0; k < 8; ++k) pe->createComponentSnapshot (pe->getLocalBounds(), true, 1.0f);
                best = std::min (best, (juce::Time::getMillisecondCounterHiRes() - t0) / 8.0);
            }
            return best;
        };
        const double lc = timeIt (2, 0), gc = timeIt (3, 0), ls = timeIt (2, 1), gs = timeIt (3, 1);
        pe->setLayout (0);
        CHECK (gc <= lc * 1.5 && gs <= ls * 1.5, "full repaint: CLASSIC SYNTH LIGHT %.1f ms / GHOST %.1f ms (%.2fx), STUDIO LIGHT %.1f ms / GHOST %.1f ms (%.2fx); limit 1.5x",
               lc, gc, gc / lc, ls, gs, gs / ls);
    }
    ed.reset();
    pt::setTheme (th::Theme::Dark);

    // ---- screenshots
    const int n = ghostShots (shots);
    CHECK (n == 8, "GHOST shots (SYNTH, MOD, BEND, TRAP, MORPH, FX, BROWSE + STUDIO): %d written to %s", n, shots.getFullPathName().toRawUTF8());
    pt::setTheme (th::Theme::Dark);
    return 0;
}
} // namespace ghostlooktest
