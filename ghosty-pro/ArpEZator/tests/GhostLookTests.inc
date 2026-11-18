// ArpEZatorTests: the GHOST look (liquid glass on the LIGHT base, ArpEZator-only 4th look). --ghost <shot dir> runs it alone.
//   saved as theme_name "ghost" (old sessions unchanged, unknown -> BLACK); BLACK / PURPLE / LIGHT render pixel-identically before and after GHOST;
//   GHOST keeps the layout + every tooltip; every text run on every page >= 4.5:1 measured on the rendered glass (the house probe); the cached
//   aurora / cards; full-repaint time GHOST vs LIGHT; the professor + the mood ghost on glass; screenshots of every page.
namespace ghosttest {
static bool same (const juce::Image& a, const juce::Image& b)
{
    if (a.getWidth() != b.getWidth() || a.getHeight() != b.getHeight()) return false;
    const juce::Image::BitmapData A (a, juce::Image::BitmapData::readOnly), B (b, juce::Image::BitmapData::readOnly);
    for (int y = 0; y < a.getHeight(); ++y) for (int x = 0; x < a.getWidth(); ++x) if (A.getPixelColour (x, y) != B.getPixelColour (x, y)) return false;
    return true;
}
static void collect (juce::Component& c, std::vector<juce::Component*>& out) { for (auto* ch : c.getChildren()) { out.push_back (ch); collect (*ch, out); } }
static void png (const juce::Image& img, const juce::File& f) { f.deleteFile(); juce::FileOutputStream o (f); if (o.openedOk()) { juce::PNGImageFormat p; p.writeImageToStream (img, o); } }
} // namespace ghosttest

void ghostChecks (Counter& C, const juce::String& shotDir)
{
    using namespace ghosty::themes; using namespace ghosttest;
    std::printf ("== GHOST look (ArpEZator)\n");
    {   // the saved look: "ghost" round-trips; the old values unchanged; unknown -> BLACK
        ArpProcessor p; p.persistTaste = false; p.theme = "ghost"; juce::MemoryBlock mb; p.getStateInformation (mb);
        ArpProcessor q; q.persistTaste = false; q.setStateInformation (mb.getData(), (int) mb.getSize());
        auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize()); auto root = juce::ValueTree::fromXml (*xml);
        auto named = [&] (const juce::String& n) { auto r = root.createCopy(); r.setProperty ("theme_name", n, nullptr); return ArpProcessor::themeFromState (r); };
        const bool ok = q.theme == "ghost" && q.ghostLook() && q.themeId() == Theme::Light && root.getProperty ("theme_name").toString() == "ghost" && root.getProperty ("theme").toString() == "light"
                     && named ("dark") == "dark" && named ("purple") == "purple" && named ("light") == "light" && named ("bogus") == "dark" && named ("GHOST") == "ghost";
        C.ok (ok, "GHOST: saved as theme_name \"ghost\" (the old key says light), restored; dark / purple / light load unchanged, unknown -> BLACK");
        ArpProcessor d; d.persistTaste = false; d.theme = "dark"; C.ok (! d.ghostLook() && ArpProcessor::nextLook ("light") == "ghost" && ArpProcessor::nextLook ("ghost") == "dark" && ArpProcessor::lookName ("ghost") == "GHOST",
                                                                       "GHOST: the look after LIGHT, then back to BLACK");
    }
    ArpProcessor p; p.persistTaste = false; p.theme = "dark"; p.hintsSeen = true;
    p.change ([] (Idea& X) { X.mode = Dusk; X.seed = 51; applyModeDefaults (X); newIdea (X); for (auto& L : X.lanes) L.mute = false; }, false);
    std::unique_ptr<ArpEditor> ed (dynamic_cast<ArpEditor*> (p.createEditor())); ed->setSize (1240, 820); ed->refresh();
    auto setLook = [&] (const juce::String& look) { p.theme = look; ed->applyThemeNow (p.idea().mode); };
    {   // tokens
        setLook ("ghost"); const auto& P = ed->th.p; double text = 99, acc = 99;
        for (auto bg : { P.bg1, P.panel, P.card, P.card2, P.roll }) for (auto fg : { P.ink, P.dim }) text = std::min (text, contrast (fg, bg));
        for (auto a : { P.a1, P.a2, P.a3, P.gold, P.pink, P.cyan, P.green }) acc = std::min (acc, contrast (a, juce::Colour (0xffd0c4da)));
        C.ok (P.glass && ! P.dark && P.theme == Theme::Light && text >= 4.5 && acc >= 4.5 && ! glassLight(),
              ("GHOST tokens: text " + juce::String (text, 2) + ":1, accents on the darkest glass " + juce::String (acc, 2) + ":1 (>= 4.5); the shared themes untouched").toStdString());
        setLook ("light"); C.ok (! ed->th.p.glass && ed->th.p.card == get (Theme::Light).card, "LIGHT stays the shared LIGHT (GHOST is ArpEZator's own palette)");
    }
    // ---- the old looks render the same before and after GHOST; GHOST keeps the LIGHT layout + tooltips
    std::vector<juce::Image> before, after; const juce::String looks[] { "dark", "purple", "light" };
    auto shotsOf = [&] (std::vector<juce::Image>& v) { for (int l = 0; l < 4; ++l) { ed->selectLane (l); ed->refresh(); v.push_back (ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f)); } };
    auto layout = [&] { std::vector<juce::Component*> v; collect (*ed, v); std::vector<juce::Rectangle<int>> r; juce::StringArray tips;
        for (auto* c : v) { r.push_back (c->isVisible() ? c->getBounds() : juce::Rectangle<int> (-1, -1, 0, 0)); if (auto* tc = dynamic_cast<juce::SettableTooltipClient*> (c)) tips.add (tc->getTooltip()); }
        return std::make_pair (r, tips); };
    for (auto& lk : looks) { setLook (lk); shotsOf (before); }
    const auto layLight = layout();
    setLook ("ghost"); std::vector<juce::Image> ghostShots; shotsOf (ghostShots); const auto layGhost = layout();
    for (auto& lk : looks) { setLook (lk); shotsOf (after); }
    { int eq = 0; for (size_t i = 0; i < before.size(); ++i) { if (same (before[i], after[i])) ++eq; else std::printf ("    differs: look %d lane %d\n", (int) i / 4, (int) i % 4); }
      C.ok (eq == (int) before.size(), ("GHOST: BLACK / PURPLE / LIGHT x 4 lanes pixel-identical before and after GHOST (" + juce::String (eq) + "/" + juce::String ((int) before.size()) + ")").toStdString());
      int differ = 0; for (int i = 0; i < 4; ++i) differ += same (ghostShots[(size_t) i], before[(size_t) (8 + i)]) ? 0 : 1;
      int nt = 0; for (auto& s : layGhost.second) nt += s.isNotEmpty() ? 1 : 0;
      C.ok (layLight.first == layGhost.first && layLight.second == layGhost.second && differ == 4 && nt >= 40,
            ("GHOST keeps the LIGHT layout and every tooltip (" + juce::String (nt) + " tooltips) and paints its own look (" + juce::String (differ) + "/4 lanes differ from LIGHT)").toStdString()); }
    // ---- every text run on every page >= 4.5:1 (large >= 3:1) on the rendered glass: the house probe, as the other looks are measured
    {   // (a window opened in GHOST, as the other looks are measured: placeholder texts take the palette at construction)
        setLook ("dark"); ed.reset(); p.theme = "ghost"; ed.reset (dynamic_cast<ArpEditor*> (p.createEditor())); ed->setSize (1240, 820); ed->refresh(); setLook ("ghost");
        int runs = 0, bad = 0, pages = 0; double worst = 99; juce::String worstAt;
        const juce::File dir (shotDir);
        auto page = [&] (const juce::String& name, std::function<void()> setup) {
            if (setup) setup();
            ed->refresh(); ed->toast ("", -1.0e15);
            const auto R = probeContrast (*ed, "ghost/" + name, nullptr, {}, ed->modalFocus());
            runs += R.runs; bad += R.bad; if (R.worst < worst) { worst = R.worst; worstAt = R.worstAt; } ++pages;
            if (shotDir.isNotEmpty()) png (ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f), dir.getChildFile ("arp-ghost-" + name + ".png"));
        };
        for (int m = 0; m < kMoods; ++m)
        {
            const juce::String mn = m == 0 ? "sunrise" : m == 1 ? "dusk" : m == 2 ? "midnight" : m == Trap ? "trap" : "hiphop";
            p.change ([m] (Idea& X) { X.mode = (decltype (X.mode)) m; applyModeDefaults (X); newIdea (X); for (auto& L : X.lanes) L.mute = false; }, false);
            ed->applyThemeNow (m);
            for (int l = 0; l < 4; ++l) page ("main-" + mn + "-" + ArpEditor::laneName (l).toLowerCase(), [&] { ed->selectLane (l); });
        }
        p.change ([] (Idea& X) { X.mode = Dusk; applyModeDefaults (X); newIdea (X); for (auto& L : X.lanes) L.mute = false; }, false); ed->applyThemeNow (Dusk);
        page ("loop-section", [&] { ed->selectLane (LaneMelody); p.setSection (BAR, 3 * BAR); p.setSectionOn (true); p.setPlayCursor (BAR + PPQ); });
        page ("muted-lane", [&] { p.clearSection(); p.setPlayCursor (-1); p.change ([] (Idea& X) { X.lanes[LaneBass].mute = true; }, false); ed->selectLane (LaneBass); });
        p.change ([] (Idea& X) { X.lanes[LaneBass].mute = false; }, false); ed->selectLane (LaneMelody);
        { ed->toast ("Looping bars 2-3 - press L (or click LOOP) to play the whole song", 1.0e9); const auto R = probeContrast (*ed, "ghost/toast"); runs += R.runs; bad += R.bad; if (R.worst < worst) { worst = R.worst; worstAt = R.worstAt; } ++pages;
          if (shotDir.isNotEmpty()) png (ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f), dir.getChildFile ("arp-ghost-toast.png")); }
        page ("hooklab", [&] { p.labGenerate (4); ed->openHookLab (true); ed->hookLab->sync(); });
        page ("sample", [&] { ed->openHookLab (false); std::vector<float> x[2] = { smptest::tone (220.0, 2.0, 3), {} }; juce::String msg; p.loadSampleAudio (x, 1, 48000.0, "sample", {}, msg);
                              auto q = p.sampleParams(); q.start = 0.05f; q.end = 0.9f; q.mode = 1; q.loopStart = 0.35f; q.loopEnd = 0.6f; p.setSampleParams (q); ed->openSample (true); ed->samplePanel->sync(); });
        page ("sample-slices", [&] { p.sliceSample (0.9f); ed->samplePanel->sync(); ed->samplePanel->resized(); });
        { auto q = p.sampleParams(); q.mode = 1; p.setSampleParams (q); ed->samplePanel->sync(); ed->samplePanel->resized(); }
        page ("more", [&] { ed->openSample (false); ed->more->setVisible (true); ed->more->toFront (false); ed->more->sync(); });
        page ("save", [&] { ed->more->setVisible (false); ed->openSave(); });
        page ("load", [&] { ed->openBrowser(); });
        auto hints = [&] (bool v) { std::function<void (juce::Component&)> w = [&] (juce::Component& k) { for (auto* ch : k.getChildren()) { if (ch->getName() == "hints") ch->setVisible (v); else w (*ch); } }; w (*ed); };
        page ("hints", [&] { ed->closeOverlay(); hints (true); });
        hints (false);
        auto& lnf = ed->getLookAndFeel();
        auto popup = [&] (const juce::String& name, int w, int h, std::function<void (juce::Graphics&)> fn) {
            juce::Component dummy; const auto R = probeContrast (dummy, "ghost/" + name, fn, { 0, 0, w, h });
            runs += R.runs; bad += R.bad; if (R.worst < worst) { worst = R.worst; worstAt = R.worstAt; } ++pages;
            if (shotDir.isNotEmpty()) { juce::Image img (juce::Image::ARGB, w, h, true); { juce::Graphics g (img); fn (g); } png (img, dir.getChildFile ("arp-ghost-" + name + ".png")); }
        };
        popup ("menu", 260, 150, [&] (juce::Graphics& g) {
            lnf.drawPopupMenuBackground (g, 260, 150);
            lnf.drawPopupMenuSectionHeader (g, { 0, 4, 260, 24 }, "Line between two notes");
            lnf.drawPopupMenuItem (g, { 0, 30, 260, 24 }, false, true, false, true, false, "GLIDE (slide into the next note)", {}, nullptr, nullptr);
            lnf.drawPopupMenuItem (g, { 0, 56, 260, 24 }, false, true, true, false, false, "Glide early + long", {}, nullptr, nullptr);
            lnf.drawPopupMenuItem (g, { 0, 82, 260, 24 }, false, true, false, false, true, "FATTEN (next note louder)", {}, nullptr, nullptr); });
        popup ("tooltip", 300, 60, [&] (juce::Graphics& g) { lnf.drawTooltip (g, "LOOP SECTION (L): play only the loop", 300, 60); });
        std::printf ("contrast ghost: %d pages, %d text runs, %d below the bar%s%s\n", pages, runs, bad, worstAt.isEmpty() ? "" : "; worst: ", worstAt.toRawUTF8());
        C.ok (runs > 400 && bad == 0, ("GHOST: every text run on " + juce::String (pages) + " pages / overlays / pop-ups >= 4.5:1 (large >= 3:1), measured on the rendered glass").toStdString());
        ed->closeOverlay();
    }
    // ---- caches: a repaint rebuilds nothing; a resize rebuilds the aurora once (it is drawn in design coordinates, scaled)
    {
        setLook ("ghost"); ed->selectLane (LaneMelody); (void) ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        const int b0 = ed->backdrop.builds, p0 = ed->panels.builds;
        for (int i = 0; i < 3; ++i) (void) ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        const int b1 = ed->backdrop.builds, p1 = ed->panels.builds;
        ed->setSize (1550, 1025); (void) ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f); const int b2 = ed->backdrop.builds; ed->setSize (1240, 820);
        C.ok (b1 == b0 && p1 == p0 && b2 == b0 + 1, ("GHOST paints from cached images: aurora " + juce::String (b0) + " -> " + juce::String (b1) + ", cards " + juce::String (p0) + " -> " + juce::String (p1) + " on repaint; a resize rebuilds the aurora once (" + juce::String (b2) + ")").toStdString());
    }
    // ---- full-repaint time GHOST vs LIGHT (warm caches), every lane + the HOOK LAB + MORE
    {
        auto timeIt = [&] (const juce::String& look) { setLook (look); double tot = 0;
            auto one = [&] { (void) ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f); (void) ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
                             const double t0 = juce::Time::getMillisecondCounterHiRes(); for (int i = 0; i < 10; ++i) (void) ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f); return (juce::Time::getMillisecondCounterHiRes() - t0) / 10.0; };
            for (int l = 0; l < 4; ++l) { ed->selectLane (l); ed->refresh(); tot += one(); }
            ed->openHookLab (true); tot += one(); ed->openHookLab (false); ed->more->setVisible (true); tot += one(); ed->more->setVisible (false);
            return tot; };
        double tl = 0, tg = 0; for (int rep = 0; rep < 2; ++rep) { tl = timeIt ("light"); tg = timeIt ("ghost"); }
        std::printf ("  full repaint (1240 x 820, 4 lanes + HOOK LAB + MORE): LIGHT %.1f ms, GHOST %.1f ms (x%.2f)\n", tl, tg, tg / tl);
        C.ok (tg <= tl * 1.5, ("GHOST full repaint within 1.5x LIGHT (x" + juce::String (tg / tl, 2) + ")").toStdString());
    }
    // ---- the professor + the mood ghost read on glass: their pixels differ clearly from the glass under them
    {
        setLook ("ghost"); ed->selectLane (LaneMelody); ed->refresh();
        const auto full = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::Image surf (juce::Image::ARGB, ed->getWidth(), ed->getHeight(), true); { juce::Graphics g (surf); g.addTransform (ed->contentComp().getTransform()); ed->paintGlassSurfaces (g); }
        auto legible = [&] (juce::Rectangle<int> r) { int n = 0, hi = 0; const juce::Image::BitmapData A (full, juce::Image::BitmapData::readOnly), B (surf, juce::Image::BitmapData::readOnly);
            r = r.getIntersection (full.getBounds()); for (int y = r.getY(); y < r.getBottom(); ++y) for (int x = r.getX(); x < r.getRight(); ++x) { ++n; if (contrast (A.getPixelColour (x, y).withAlpha (1.0f), B.getPixelColour (x, y).withAlpha (1.0f)) >= 3.0) ++hi; }
            return n ? (double) hi / n : 0.0; };
        const double prof = legible ({ 12, 4, 62, 62 }), ghost = legible (ed->moodGhostArea().withSizeKeepingCentre (54, 54).withY (4));
        C.ok (prof > 0.3 && ghost > 0.2, ("GHOST: Professor Bernstein (" + juce::String (100 * prof, 0) + "% of his box >= 3:1 against the glass) and the mood ghost (" + juce::String (100 * ghost, 0) + "%) stand out").toStdString());
    }
    setLook ("dark"); ed.reset();
}
