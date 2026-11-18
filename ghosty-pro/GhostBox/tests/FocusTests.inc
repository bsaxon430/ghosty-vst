// GHOST FOCUS tests (GhostBox/BoxFocus.h + BoxProcessor focus*): every chain builds from existing presets, deterministic, safe (finite,
// peak limited) and gain-matched within 1.5 LU through the offline renderer; the audition / AMOUNT / KEEP / BACK flow.
//   GhostBoxTests focus          -> these checks
//   GhostBoxTests focusui [dir]  -> the FOCUS page (entry points, tooltips, contrast in 4 looks, screenshots)

static void testGhostFocus()
{
    std::printf ("\n[36] GHOST FOCUS: 8 intentions x 4 chains from factory presets, deterministic, safe, gain-matched, audition / AMOUNT / KEEP / BACK\n");
    // ---- the recipe book: every step names a real unit + factory preset
    int missing = 0; juce::String miss;
    for (int i = 0; i < boxfocus::kIntents; ++i)
        for (int k = 0; k < boxfocus::kChains; ++k)
            for (auto& st : boxfocus::chain (i, k).steps)
            {
                bool found = st.preset == nullptr || *st.preset == 0;
                for (auto& pr : unitPresets (st.unit)) found = found || pr.name == st.preset;
                if (! found || st.unit == uLim) { ++missing; miss << boxfocus::chain (i, k).name << ":" << st.preset << " "; }
            }
    std::printf ("    32 chains, steps naming a missing preset: %d %s\n", missing, miss.toRawUTF8());
    check (missing == 0, "every GHOST FOCUS chain is built from existing units and factory presets");
    // ---- deterministic: the same chain twice = the same sound
    {
        Proc p (false);
        const auto a = p->focusChainSound (2, 1, p->captureSound()), b = p->focusChainSound (2, 1, p->captureSound());
        check (a.isEquivalentTo (b), "GHOST FOCUS chains are deterministic");
    }
    // ---- every chain offline: gain-matched against the dry sound on the reference mix (within 1.5 LU), then finite + peak limited on hot test signals
    {
        Proc p (true);
        const auto dry = p->captureSound();
        const float lDry = BoxProcessor::loudnessOf (dry, SR);
        const Stereo sigs[] { [] { auto d = drums (3.0); for (auto& x : d.l) x *= 2.0f; for (auto& x : d.r) x *= 2.0f; return d; }(),
                              mixOf (bassline (3.0), drums (3.0), 0.5f), mixOf (pad (3.0), pluck (3.0), 0.8f), impulse (3.0, 0.9f, 0.1) };
        float worstLu = 0, worstPeak = 0, rawLo = 99, rawHi = -99; bool allFinite = true; juce::String worstName, peakName, loName, hiName;
        for (int i = 0; i < boxfocus::kIntents; ++i)
            for (int k = 0; k < boxfocus::kChains; ++k)
            {
                const auto raw = p->focusChainSound (i, k, dry);
                const float lRaw = BoxProcessor::loudnessOf (raw, SR);
                if (lRaw - lDry < rawLo) { rawLo = lRaw - lDry; loName = boxfocus::chain (i, k).name; }
                if (lRaw - lDry > rawHi) { rawHi = lRaw - lDry; hiName = boxfocus::chain (i, k).name; }
                float trim = 0, lEnd = 0;
                const auto m = BoxProcessor::gainMatched (dry, lDry, raw, SR, &trim, &lEnd);
                if (std::abs (lEnd - lDry) > std::abs (worstLu)) { worstLu = lEnd - lDry; worstName = boxfocus::chain (i, k).name; }
                Proc q (true); q->applyStateTree (m.createCopy()); q.pump (5);
                for (auto& s : sigs)
                {
                    q.head.ppq = 0;
                    const auto out = run (q, withTail (s, 1.0));
                    allFinite = allFinite && finite (out);
                    const float pk = peak (out);
                    if (pk > worstPeak) { worstPeak = pk; peakName = boxfocus::chain (i, k).name; }
                }
            }
        std::printf ("    raw chains vs dry on the reference mix: %+.1f (%s) .. %+.1f (%s) LU | after the match: worst %+.2f LU (%s) | finite %d | worst peak %.3f (%s; ceiling -0.3 dB = 0.966)\n",
                     rawLo, loName.toRawUTF8(), rawHi, hiName.toRawUTF8(), worstLu, worstName.toRawUTF8(), (int) allFinite, worstPeak, peakName.toRawUTF8());
        check (std::abs (worstLu) <= 1.5f && allFinite && worstPeak <= 0.97f && rawLo > -9.0f && rawHi < 9.0f, "GHOST FOCUS chains: gain-matched within 1.5 LU, finite, peak limited");
    }
    // ---- the flow on the processor: pick, audition, AMOUNT, KEEP, BACK
    {
        Proc p (true); p.scene ("galaxyecho"); p.pump (40);
        const auto before = p->captureSound();
        p->focusPick (0); const bool done = p->focusWait();
        const int n = p->focusCount();
        p->focusAudition (2); p.pump (20);
        const auto& c = boxfocus::chain (0, 2);
        bool unitsOn = true; for (auto& st : c.steps) unitsOn = unitsOn && p.get (pid (st.unit, "on")) > 0.5f;
        const bool echoOff = p.get (pid (uEcho, "on")) < 0.5f;   // a starting chain replaces what was there
        const float mixFull = p.get (pid (uCrush, "mix"));
        p->focusSetAmount (0.5f); p.pump (5);
        const float mixHalf = p.get (pid (uCrush, "mix"));
        p->focusBack(); p.pump (20);
        const auto back = p->captureSound();
        bool same = true; juce::String diffs;
        for (auto c2 : before) if (c2.hasType ("PARAM")) { const auto o = back.getChildWithProperty ("id", c2.getProperty ("id")); const float va = (float) c2.getProperty ("value"); const bool eq = o.isValid() && std::abs ((float) o.getProperty ("value") - va) <= 1.0e-5f * std::max (1.0f, std::abs (va)); if (! eq) diffs << c2.getProperty ("id").toString() << " "; same = same && eq; }
        if (! same) std::printf ("    (differs: %s)\n", diffs.substring (0, 300).toRawUTF8());
        p->focusPick (7); p->focusWait(); p->focusAudition (0); p->focusKeep();
        const bool kept = ! p->focusOriginalValid() && p.get (pid (uSplit, "on")) > 0.5f;
        std::printf ("    pick FAT: done %d, %d chains | audition 3: its units on %d, the old chain gone %d | AMOUNT 50 %%: CRUSH mix %.2f -> %.2f | BACK: exactly the sound before %d | KEEP stays %d\n",
                     (int) done, n, (int) unitsOn, (int) echoOff, mixFull, mixHalf, (int) same, (int) kept);
        check (done && n == 4 && unitsOn && echoOff && std::abs (mixHalf - 0.5f * mixFull) < 0.01f && same && kept, "GHOST FOCUS pick / audition / AMOUNT / BACK / KEEP");
    }
}

static void testFocusUi (const juce::File& shots)
{
    std::printf ("\n[36b] GHOST FOCUS page: strip shortcut + tab + menu, tiles, tooltips, nothing moved, contrast in 4 looks, screenshots\n");
    if (shots != juce::File()) shots.createDirectory();
    Proc p (true); p.scene ("galaxyecho"); p.pump (40);
    juce::ignoreUnused (run (p, drums (0.5))); p.pump();
    p->focusPick (3); p->focusWait(); p->focusAudition (1); p.pump (20);
    juce::ignoreUnused (run (p, drums (0.5))); p.pump();
    std::unique_ptr<juce::AudioProcessorEditor> edp (p->createEditor()); auto* ed = dynamic_cast<BoxEditor*> (edp.get());
    auto* panel = findPanel (*ed);
    std::map<juce::String, juce::Rectangle<int>> before;
    for (auto* c : findAll (*ed, [] (juce::Component* k) { return dynamic_cast<juce::Button*> (k) != nullptr && k->isVisible(); }))
        before[static_cast<juce::Button*> (c)->getButtonText() + "@" + juce::String (c->getScreenX())] = c->getScreenBounds();
    juce::TextButton* sc = nullptr; juce::TextButton* scSplit = nullptr;
    for (auto* c : findAll (*panel, [] (juce::Component* k) { auto* b = dynamic_cast<juce::TextButton*> (k); return b != nullptr && b->getProperties().contains ("extraPage"); }))
    { auto* b = static_cast<juce::TextButton*> (c); if (b->getButtonText() == "FOCUS") sc = b; if (b->getButtonText() == "SPLITS") scSplit = b; }
    const bool scOk = sc != nullptr && sc->isVisible() && scSplit != nullptr && sc->getY() > scSplit->getBottom();
    if (sc) sc->triggerClick();
    pumpLoop (60);
    auto* page = findFirst<BoxFocusPage> (*ed);
    int moved = 0;
    for (auto* c : findAll (*ed, [] (juce::Component* k) { return dynamic_cast<juce::Button*> (k) != nullptr && k->isVisible(); }))
    {
        const auto key = static_cast<juce::Button*> (c)->getButtonText() + "@" + juce::String (c->getScreenX());
        if (auto it = before.find (key); it != before.end() && it->second != c->getScreenBounds()) ++moved;
    }
    std::printf ("    strip shortcut under SPLITS %d | page open %d | buttons that moved %d\n", (int) scOk, (int) (page != nullptr), moved);
    check (scOk && page != nullptr && moved == 0, "GHOST FOCUS page: shortcut, nothing moved");
    ed->setSize (1900, 1200 + BoxEditor::kSceneBar); pumpLoop (30);
    auto* tab = findFirst<juce::TextButton> (*panel, [] (juce::TextButton& b) { return b.getButtonText() == "FOCUS" && ! b.getProperties().contains ("extraPage"); });
    const bool wideTab = tab != nullptr && tab->isVisible();
    ed->setSize (960, 720 + BoxEditor::kSceneBar); pumpLoop (20);
    ed->sceneBar().moreMenuResult (7); pumpLoop (30);
    ed->sceneBar().moreMenuResult (9); pumpLoop (40);
    const bool viaMenu = findFirst<BoxFocusPage> (*ed) != nullptr;
    ed->setSize (1180, 780 + BoxEditor::kSceneBar); pumpLoop (30);
    std::printf ("    1900 px: tab visible %d | 960 px: the ... menu opens it %d\n", (int) wideTab, (int) viaMenu);
    check (wideTab && viaMenu, "GHOST FOCUS reachable at every width");
    page = findFirst<BoxFocusPage> (*ed);
    int noTip = 0, controls = 0; juce::String missing;
    if (page)
        for (auto* c : findAll (*page, [] (juce::Component* k) { return dynamic_cast<juce::Button*> (k) != nullptr || dynamic_cast<juce::ComboBox*> (k) != nullptr || dynamic_cast<juce::Slider*> (k) != nullptr; }))
        {
            ++controls; auto* tc = dynamic_cast<juce::SettableTooltipClient*> (c);
            if (tc == nullptr || tc->getTooltip().isEmpty()) { ++noTip; if (auto* b = dynamic_cast<juce::Button*> (c)) missing << b->getButtonText() << " "; }
        }
    std::printf ("    controls %d, without a tooltip %d %s\n", controls, noTip, missing.toRawUTF8());
    check (controls >= 11 && noTip == 0, "every GHOST FOCUS control has a tooltip");
    if (page) page->tileClick (2);
    pumpLoop (40);
    double worst = 99; juce::String worstWhat;
    for (int look : { 0, 1, 2, BoxEditor::kLookGhost })
    {
        ed->setLook (look); pumpLoop (60);
        if (page == nullptr) break;
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        for (auto& t : page->texts())
        {
            const auto r = ed->getLocalArea (page, t.r);
            const double c = gWorstIn (img, r.withHeight (2).translated (0, -7), t.c);
            if (c / 4.5 < worst) { worst = c / 4.5; worstWhat = lookName (look) + " " + t.what + " " + juce::String (c, 2); }
        }
        if (shots != juce::File()) snapTo (*ed, shots.getChildFile ("focus-" + lookName (look).toLowerCase() + ".png"));
    }
    std::printf ("    contrast: worst text / need %.2f (%s)\n", worst, worstWhat.toRawUTF8());
    check (worst >= 1.0, "GHOST FOCUS page text contrast in every look");
    edp.reset();
}
