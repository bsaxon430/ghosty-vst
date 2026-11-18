// GHOST SPLIT tests (GhostyPro/fx/FxRoute.h + Rack::Impl::runRoute): identity in every mode (empty branches null to the input), the
// 808 test (sub clean while the top is distorted), latency alignment between branches + what the host is told, click-free switching,
// state round trip, old sessions, no audio-thread allocation.
//   GhostBoxTests split          -> these checks
//   GhostBoxTests splitui [dir]  -> the SPLITS page (entry points, tooltips, contrast in 4 looks, screenshots)

static Stereo delayed (const Stereo& s, int d) { Stereo o; o.resize (s.size()); for (size_t i = (size_t) std::max (0, d); i < s.size(); ++i) { o.l[i] = s.l[i - (size_t) d]; o.r[i] = s.r[i - (size_t) d]; } return o; }
static float nullDb (const Stereo& out, const Stereo& in, int lat, size_t from)
{   // RMS of (out - input delayed by lat) relative to the input, from 'from' on
    return diffDb (out, in, from, lat);
}

static void testGhostSplit()
{
    std::printf ("\n[35] GHOST SPLIT: identity per mode, 808 sub clean, latency alignment, switching, state, old sessions, allocations\n");
    const auto in = mixOf (mixOf (drums (2.5), bassline (2.5), 0.8f), sine (2.5, 330.0, 0.15f, 331.0));
    // ---- identity: every mode with units assigned to every branch but switched off: the output nulls to the input
    {
        struct M { int mode, bands; const char* name; };
        const M modes[] { { rmFreq, 0, "FREQUENCY 2" }, { rmFreq, 1, "FREQUENCY 3" }, { rmTransient, 0, "TRANSIENT / BODY" }, { rmMidSide, 0, "MID / SIDE" }, { rmLevel, 0, "LEVEL" }, { rmParallel, 0, "PARALLEL" } };
        float worst = -999; juce::String line;
        for (auto& m : modes)
        {
            Proc p (false); p.set ("box_fixlat", 0);
            p.set ("fx_route_mode", (float) m.mode); p.set ("fx_route_bands", (float) m.bands); p.set ("fx_route_on", 1);
            p.impl().setRoute (uTube, 1); p.impl().setRoute (uChorus, 2); p.impl().setRoute (uEcho, 3);
            p.pump();
            const auto out = run (p, in);
            p.pump();
            const int lat = p->rack.latencySamples();
            const float db = nullDb (out, in, lat, S (0.4));
            worst = std::max (worst, db); line << m.name << " " << juce::String (db, 1) << " dB  ";
            check (p.impl().routeActiveNow, juce::String ("GHOST SPLIT active in ") + m.name);
        }
        std::printf ("    empty branches vs the input (delayed by the reported latency): %s\n", line.toRawUTF8());
        check (worst < -90.0f, "GHOST SPLIT identity: empty branches null to the input in every mode (< -90 dB)");
    }
    // ---- the 808 test: FREQUENCY 2 bands at 120 Hz, a hard drive in HIGH only: the sub stays as it was
    {
        Stereo b808 = mixOf (bassline (4.0), drums (4.0), 0.25f);
        auto subDb = [] (const Stereo& s, size_t a, size_t b) { const auto sp = spectrumDb (s, a, b, 14); return bandDb (sp, 25.0, 70.0, 14); };
        auto topDb = [] (const Stereo& s, size_t a, size_t b) { const auto sp = spectrumDb (s, a, b, 14); return bandDb (sp, 1000.0, 8000.0, 14); };
        Proc p (false); p.set ("box_fixlat", 0);
        p.set (pid (uWreck, "on"), 1); p.set (pid (uWreck, "style"), 5); p.set (pid (uWreck, "drive"), 0.9f); p.set (pid (uWreck, "auto"), 0.0f);
        p.set ("fx_route_mode", (float) rmFreq); p.set ("fx_route_bands", 0); p.set ("fx_route_xlo", 120.0f); p.set ("fx_route_on", 1);
        p.impl().setRoute (uWreck, 2);
        p.pump();
        const auto out = run (p, b808); p.pump(); const int lat = p->rack.latencySamples();
        const auto ref = delayed (b808, lat);
        const float dSub = subDb (out, S (1.0), S (4.0)) - subDb (ref, S (1.0), S (4.0)), dTop = topDb (out, S (1.0), S (4.0)) - topDb (ref, S (1.0), S (4.0));
        // the same drive on the whole signal (no split), for comparison
        Proc q (false); q.set ("box_fixlat", 0); q.set (pid (uWreck, "on"), 1); q.set (pid (uWreck, "style"), 5); q.set (pid (uWreck, "drive"), 0.9f); q.set (pid (uWreck, "auto"), 0.0f); q.pump();
        const auto outQ = run (q, b808); q.pump(); const auto refQ = delayed (b808, q->rack.latencySamples());
        const float dSubQ = subDb (outQ, S (1.0), S (4.0)) - subDb (refQ, S (1.0), S (4.0));
        std::printf ("    808 + drums, WRECKER (FUZZ, drive 90 %%) in HIGH only: sub (25-70 Hz) %+.2f dB, top (1-8 kHz) %+.1f dB | the same drive on everything: sub %+.2f dB\n", dSub, dTop, dSubQ);
        check (std::abs (dSub) < 0.5f && dTop > 3.0f && finite (out), "808: the sub band stays within 0.5 dB while the upper band is distorted");
    }
    // ---- latency alignment: PARALLEL, SPECTRA (latent) at mix 0 in A, B empty: aligned = a null; the host is told the slowest branch
    {
        Proc p (false); p.set ("box_fixlat", 0); p.pump(); const int base = p->rack.latencySamples();
        p.set (pid (uSpectra, "on"), 1); p.set (pid (uSpectra, "mix"), 0.0f);
        p.set (pid (uWarp, "on"), 1); p.set (pid (uWarp, "mix"), 0.0f);
        p.set ("fx_route_mode", (float) rmParallel); p.set ("fx_route_blend", 0.5f); p.set ("fx_route_on", 1);
        p.impl().setRoute (uSpectra, 1); p.impl().setRoute (uWarp, 2);
        p.pump(); run (p, slice (in, 0, S (0.3))); p.pump();
        const int lat = p->rack.latencySamples(), lS = p.impl().units[uSpectra]->latency(), lW = p.impl().units[uWarp]->latency();
        const auto out = run (p, in);
        const float db = nullDb (out, in, lat, S (0.3));
        // the same two units in series (no split): the latencies add up
        Proc q (false); q.set ("box_fixlat", 0); q.set (pid (uSpectra, "on"), 1); q.set (pid (uSpectra, "mix"), 0.0f); q.set (pid (uWarp, "on"), 1); q.set (pid (uWarp, "mix"), 0.0f); q.pump(); run (q, slice (in, 0, S (0.3))); q.pump();
        const int latQ = q->rack.latencySamples();
        std::printf ("    PARALLEL: A = SPECTRA (%d samples), B = WARP (%d samples), both at mix 0: host latency %d (base %d + the slower branch; in series it would be %d) | null %.1f dB\n", lS, lW, lat, base, latQ, db);
        check (lat == base + std::max (lS, lW) && latQ == base + lS + lW && db < -90.0f, "GHOST SPLIT aligns its branches and reports the slowest one");
        p.set ("box_fixlat", 1); p.pump();
        check (p->getLatencySamples() >= lat, "FIXED LATENCY still covers the split");
    }
    // ---- switching while audio plays: on / off, a unit into a branch, a mode change: click-free fades, no allocation
    {
        Proc p (false); p.set (pid (uWreck, "on"), 1); p.set (pid (uWreck, "drive"), 0.6f); p.set (pid (uEcho, "on"), 1); p.pump();
        const auto sig = mixOf (pad (6.0), sine (6.0, 220.0, 0.2f));
        const long a0 = gAudioAllocs.load();
        const auto out = runLive (p, sig, {
            { 0.8, [&] { p.impl().setRoute (uWreck, 2); p.set ("fx_route_on", 1); } },
            { 1.8, [&] { p.set ("fx_route_mode", (float) rmMidSide); } },
            { 2.8, [&] { p.impl().setRoute (uEcho, 1); } },
            { 3.8, [&] { p.set ("fx_route_mode", (float) rmLevel); } },
            { 4.8, [&] { p.set ("fx_route_on", 0); } } });
        const long allocs = gAudioAllocs.load() - a0;
        double where = 0; const float cs = clickScore (slice (out, S (0.3), out.size()), &where);
        std::printf ("    live switching (on, MID / SIDE, ECHO into a branch, LEVEL, off): click score %.1f at %.2f s, finite %d, audio allocations %ld\n", cs, where + 0.3, (int) finite (out), allocs);
        check (finite (out) && allocs == 0 && cs < 40.0f, "GHOST SPLIT switches click-free (the rack's fade) without audio-thread allocation");
    }
    // ---- state round trip + old sessions + undo
    {
        Proc p (false); p.set (pid (uWreck, "on"), 1); p.set (pid (uWreck, "drive"), 0.7f); p.set (pid (uChorus, "on"), 1);
        p.set ("fx_route_mode", (float) rmFreq); p.set ("fx_route_bands", 1); p.set ("fx_route_xlo", 180.0f); p.set ("fx_route_xhi", 4200.0f); p.set ("fx_route_g2", -3.0f); p.set ("fx_route_on", 1);
        p.impl().setRoute (uWreck, 2); p.impl().setRoute (uChorus, 3); p.pump();
        juce::MemoryBlock mb; p->getStateInformation (mb);
        Proc q (false); q->setStateInformation (mb.getData(), (int) mb.getSize()); q.pump();
        const bool same = q.impl().routeBranchOf (uWreck) == 2 && q.impl().routeBranchOf (uChorus) == 3 && std::abs (q.get ("fx_route_xhi") - 4200.0f) < 0.5f && std::abs (q.get ("fx_route_g2") + 3.0f) < 1e-3f && q.get ("fx_route_on") > 0.5f;
        p.head.ppq = q.head.ppq = 0; p->rack.snapOnNextBlock();   // (p starts like a restored session: no switch fade on its first block)
        const auto o1 = run (p, in), o2 = run (q, in);
        const float md = maxDiff (o1, o2);
        Proc r (false); const auto st = r->stateTree();
        const bool noProp = ! st.getChildWithName ("FXRACK").getChild (0).hasProperty ("route");
        Proc s (false); s.impl().setRoute (uTube, 1); s->applyStateTree (st.createCopy());
        p.impl().pushUndo(); p.impl().setRoute (uWreck, 0); p.impl().undo();
        std::printf ("    state round trip: settings + branches %d, renders equal (max diff %.2e) | default session saves no route %d, an older session clears it %d | undo %d\n",
                     (int) same, md, (int) noProp, (int) (s.impl().routeBranchOf (uTube) == 0), (int) (p.impl().routeBranchOf (uWreck) == 2));
        check (same && md == 0.0f && noProp && s.impl().routeBranchOf (uTube) == 0 && p.impl().routeBranchOf (uWreck) == 2, "GHOST SPLIT state round trip / old sessions / undo");
    }
}

static void testSplitUi (const juce::File& shots)
{
    std::printf ("\n[35b] GHOST SPLIT page: strip shortcut + tab + menu, chips, tooltips, nothing moved, contrast in 4 looks, screenshots\n");
    if (shots != juce::File()) shots.createDirectory();
    Proc p (true); p.scene ("galaxyecho"); p.set (pid (uWreck, "on"), 1); p.set (pid (uFatten, "on"), 1); p.pump (60);
    p.set ("fx_route_mode", (float) rmFreq); p.set ("fx_route_bands", 1); p.set ("fx_route_on", 1);
    p.impl().setRoute (uWreck, 2); p.impl().setRoute (uFatten, 2); p.impl().setRoute (uEcho, 3); p.pump();
    juce::ignoreUnused (run (p, mixOf (drums (0.6), bassline (0.6)))); p.pump();   // (the audio side picks the settings up: tiles, badges, latency)
    std::unique_ptr<juce::AudioProcessorEditor> edp (p->createEditor()); auto* ed = dynamic_cast<BoxEditor*> (edp.get());
    auto* panel = findPanel (*ed);
    // every button of the window before: nothing may move
    std::map<juce::String, juce::Rectangle<int>> before;
    for (auto* c : findAll (*ed, [] (juce::Component* k) { return dynamic_cast<juce::Button*> (k) != nullptr && k->isVisible(); }))
        before[static_cast<juce::Button*> (c)->getButtonText() + "@" + juce::String (c->getScreenX())] = c->getScreenBounds();
    // the shortcut sits in the strip's free cell (after the limiter tile)
    juce::TextButton* sc = nullptr;
    for (auto* c : findAll (*panel, [] (juce::Component* k) { auto* b = dynamic_cast<juce::TextButton*> (k); return b != nullptr && b->getButtonText() == "SPLITS" && b->getProperties().contains ("extraPage"); })) sc = static_cast<juce::TextButton*> (c);
    auto* tab = findFirst<juce::TextButton> (*panel, [] (juce::TextButton& b) { return b.getButtonText() == "SPLITS" && ! b.getProperties().contains ("extraPage"); });
    const bool scOk = sc != nullptr && sc->isVisible() && sc->getWidth() >= 38;   // (half the free cell since PRINT / G-SPACE joined it)
    std::printf ("    1180 px: strip shortcut %d (%s), tab visible %d\n", (int) scOk, sc ? sc->getBounds().toString().toRawUTF8() : "-", (int) (tab != nullptr && tab->isVisible()));
    if (sc) sc->triggerClick();
    pumpLoop (60);
    auto* page = findFirst<BoxSplitPage> (*ed);
    const bool chipsOk = page != nullptr && page->chipsIn (1) == 2 && page->chipsIn (2) == 1 && page->chipsIn (0) == 0;
    // nothing else moved (the buttons that were there before)
    int moved = 0; juce::String mv;
    for (auto* c : findAll (*ed, [] (juce::Component* k) { return dynamic_cast<juce::Button*> (k) != nullptr && k->isVisible(); }))
    {
        const auto key = static_cast<juce::Button*> (c)->getButtonText() + "@" + juce::String (c->getScreenX());
        if (auto it = before.find (key); it != before.end() && it->second != c->getScreenBounds()) { ++moved; mv << key << " "; }
    }
    std::printf ("    page open %d | chips per branch 0 / 2 / 1 %d | buttons that moved %d %s\n", (int) (page != nullptr), (int) chipsOk, moved, mv.toRawUTF8());
    check (scOk && page != nullptr && chipsOk && moved == 0, "GHOST SPLIT page: shortcut, chips, nothing moved");
    // wide window: the tab fits; narrow: the scene bar's menu opens it
    ed->setSize (1900, 1200 + BoxEditor::kSceneBar); pumpLoop (30);
    const bool wideTab = tab != nullptr && tab->isVisible();
    ed->setSize (960, 720 + BoxEditor::kSceneBar); pumpLoop (20);
    ed->sceneBar().moreMenuResult (7); pumpLoop (30);
    ed->sceneBar().moreMenuResult (8); pumpLoop (40);
    const bool viaMenu = findFirst<BoxSplitPage> (*ed) != nullptr;
    ed->setSize (1180, 780 + BoxEditor::kSceneBar); pumpLoop (30);
    std::printf ("    1900 px: tab visible %d | 960 px: the ... menu opens it %d\n", (int) wideTab, (int) viaMenu);
    check (wideTab && viaMenu, "GHOST SPLIT reachable at every width");
    page = findFirst<BoxSplitPage> (*ed);
    int noTip = 0, controls = 0; juce::String missing;
    if (page)
        for (auto* c : findAll (*page, [] (juce::Component* k) { return (dynamic_cast<juce::Button*> (k) != nullptr || dynamic_cast<juce::ComboBox*> (k) != nullptr || dynamic_cast<juce::Slider*> (k) != nullptr) && k->isVisible(); }))
        {
            ++controls; auto* tc = dynamic_cast<juce::SettableTooltipClient*> (c);
            if (tc == nullptr || tc->getTooltip().isEmpty()) { ++noTip; if (auto* b = dynamic_cast<juce::Button*> (c)) missing << b->getButtonText() << " "; else missing << "(" << c->getName() << ") "; }
        }
    std::printf ("    controls %d, without a tooltip %d %s\n", controls, noTip, missing.toRawUTF8());
    check (controls > 8 && noTip == 0, "every GHOST SPLIT control has a tooltip");
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
        if (shots != juce::File()) snapTo (*ed, shots.getChildFile ("split-" + lookName (look).toLowerCase() + ".png"));
    }
    std::printf ("    contrast: worst text / need %.2f (%s)\n", worst, worstWhat.toRawUTF8());
    check (worst >= 1.0, "GHOST SPLIT page text contrast in every look");
    edp.reset();
}
