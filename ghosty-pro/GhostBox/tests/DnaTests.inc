// [33] GHOST DNA: determinism by seed, locks, ranges, the safety limiter, gain match (offline renders), BREED parents, EVOLVE /
// audition / KEEP / UNDO, state round trip, and the fuzz: 10,000 mutations through the processor (finite, peak <= 0 dBFS).

static juce::ValueTree soundOf (Proc& p) { return p->captureSound(); }
static float outputLoudness (const juce::ValueTree& sound, const Stereo& sig, double skipSecs = 1.0)
{   // the whole plugin, offline: the sound loaded as a reopened project, the signal through it, K-weighted loudness of the output
    Proc q (true); q.pump (20);
    auto t = sound.createCopy();
    juce::MemoryBlock mb; { auto xml = t.createXml(); q->copyXmlToBinary (*xml, mb); }
    q->setStateInformation (mb.getData(), (int) mb.getSize()); q.pump (120); q->prepareToPlay (SR, 512); q.head.ppq = 0;
    const auto out = run (q, sig);
    gpfx::Loudness lu; lu.prepare (SR);
    lu.process (out.l.data() + S (skipSecs), out.r.data() + S (skipSecs), (int) (out.size() - S (skipSecs)));
    return lu.lufs;
}
static void applyFast (Proc& p, const boxdna::Genome& g, const juce::ValueTree& t)
{   // the fuzz's quick load: every rack setting straight into the host parameters, the order through the rack
    for (auto& q : g.all())
    {
        const float v01 = q.p->convertTo0to1 (boxdna::Genome::get (t, q));
        if (std::abs (q.p->getValue() - v01) > 1.0e-7f) q.p->setValueNotifyingHost (v01);
    }
    const auto keys = boxdna::Genome::orderOf (t);
    std::array<int, kNumUnits> o; o.fill (-1); int k = 0;
    for (auto& key : keys) for (int u = 0; u < kNumUnits; ++u) if (key == unitInfo (u).key && k < kNumUnits) o[(size_t) k++] = u;
    p.impl().setOrder (o);
}

static void testGhostDna()
{
    std::printf ("\n[33] GHOST DNA: seed determinism, locks, ranges, safety limiter, gain match, BREED, EVOLVE / audition / KEEP / UNDO, state, fuzz\n");
    Proc p (true); p.scene ("galaxyecho"); p.set (pid (uWreck, "on"), 1); p.set (pid (uWreck, "drive"), 0.35f); p.pump (60);
    auto& g = p->genome();
    const auto base = soundOf (p);
    // ---- 1) determinism (also on another instance), different seeds differ
    {
        boxdna::Locks none;
        const auto a = g.mutate (base, 4242, 0.6f, none), b = g.mutate (base, 4242, 0.6f, none), c = g.mutate (base, 4243, 0.6f, none);
        Proc q (true); q.pump (20); boxdna::Genome g2 (*q.p);
        const auto d = g2.mutate (base, 4242, 0.6f, none);
        p.scene ("retrofat"); p.pump (40); const auto other = soundOf (p); p.scene ("galaxyecho"); p.set (pid (uWreck, "on"), 1); p.set (pid (uWreck, "drive"), 0.35f); p.pump (40);
        const auto b1 = g.breed (base, other, 77, 0.5f, none), b2 = g.breed (base, other, 77, 0.5f, none);
        int n = 0; g.diffText (base, a, &n);
        std::printf ("    MUTATE seed 4242 twice identical %d, on a second instance identical %d, seed 4243 differs %d (%d settings moved) | BREED seed 77 twice identical %d\n",
                     (int) a.isEquivalentTo (b), (int) a.isEquivalentTo (d), (int) ! a.isEquivalentTo (c), n, (int) b1.isEquivalentTo (b2));
        check (a.isEquivalentTo (b) && a.isEquivalentTo (d) && ! a.isEquivalentTo (c) && n > 0 && b1.isEquivalentTo (b2), "GHOST DNA must be deterministic for a seed");
        // ---- 2) locks + 3) ranges + the safety limiter, over 300 mutations at random strengths
        boxdna::Locks L; L.units.insert (uEcho); L.params.insert (pid (uWreck, "drive")); L.params.insert (pid (uHalls, "size"));
        int lockBroken = 0, outOfRange = 0, illegal = 0, limOff = 0, fbHigh = 0;
        auto cur = base;
        for (int i = 0; i < 300; ++i)
        {
            const auto r = g.mutate (i % 30 == 0 ? base : cur, 9000 + i, (float) ((i * 37) % 100) / 100.0f, L);
            for (auto& q : g.all())
            {
                const float v = boxdna::Genome::get (r, q), was = boxdna::Genome::get (base, q);
                if ((L.unitLocked (q.unit) || L.paramLocked (q.id)) && v != was) ++lockBroken;
                const auto rg = q.p->getNormalisableRange();
                if (v < rg.start - 1.0e-6f || v > rg.end + 1.0e-6f || ! std::isfinite (v)) ++outOfRange;
                if (std::abs (q.p->convertFrom0to1 (q.p->convertTo0to1 (v)) - v) > 1.0e-5f * std::max (1.0f, std::abs (v))) ++illegal;
                if (q.isOn && q.unit == uLim && v < 0.5f) ++limOff;
                if (q.feedback && v != boxdna::Genome::get (i % 30 == 0 ? base : cur, q) && q.p->convertTo0to1 (v) > 0.9001f) ++fbHigh;
            }
            cur = r;
        }
        std::printf ("    300 mutations with ECHO + 2 settings locked: locked values changed %d | out of range %d, illegal steps %d | limiter off %d | feedback above 90 %% %d\n",
                     lockBroken, outOfRange, illegal, limOff, fbHigh);
        check (lockBroken == 0, "GHOST DNA must respect locks");
        check (outOfRange == 0 && illegal == 0 && limOff == 0 && fbHigh == 0, "GHOST DNA ranges / safety limiter");
        // BREED: every unit's on / off comes from A or B
        int notParent = 0;
        for (int s = 1; s <= 20; ++s)
        {
            const auto r = g.breed (base, other, s, (float) s / 20.0f, none);
            for (int u = 0; u < kNumUnits; ++u)
            {
                const bool on = boxdna::Genome::unitOn (r, g.all(), u);
                if (u != uLim && on != boxdna::Genome::unitOn (base, g.all(), u) && on != boxdna::Genome::unitOn (other, g.all(), u)) ++notParent;
            }
        }
        std::printf ("    BREED x20: units from neither parent %d\n", notParent);
        check (notParent == 0, "GHOST DNA BREED takes every unit from A or B");
    }
    // ---- 4) gain match, measured through the WHOLE plugin offline (a reopened project, the signal through processBlock): on the
    // reference mix the match is made on (drums + bass + pad), within 1.5 LU; on other material (the tests' drums + pad) gates / stutters
    // that the variations switch on interact with the program, so that is reported and only checked for sanity (6 LU)
    {
        Stereo ref; { const auto& D = boxdna::testSignal (SR); ref.resize ((size_t) D.getNumSamples()); for (int i = 0; i < D.getNumSamples(); ++i) { ref.l[(size_t) i] = D.getSample (0, i); ref.r[(size_t) i] = D.getSample (1, i); } }
        rs = 11; const auto other = mixOf (drums (6.0), pad (6.0), 0.8f);
        float worst = 0, worstOther = 0, louderOther = 0; juce::String txt; int n = 0;
        for (auto* sc : { "galaxyecho", "crackedhall", "retrofat", "dub" })
        {   // the product's own path: EVOLVE (4 variations, each loudness-matched on the worker) at strength 70 %
            Proc q (true); q.scene (sc); q.pump (60);
            const auto orig = soundOf (q); const float lRef = outputLoudness (orig, ref, 0.5), lOther = outputLoudness (orig, other, 1.0);
            q->dnaSeed = 500; q->dnaStrength = 0.7f; q->dnaEvolve(); q->dnaWait();
            for (int k = 0; k < q->dnaCount; ++k)
            {
                const auto& v = q->dnaVariations[(size_t) k];
                const float d = outputLoudness (v, ref, 0.5) - lRef, dOther = outputLoudness (v, other, 1.0) - lOther;
                worst = std::max (worst, std::abs (d)); worstOther = std::max (worstOther, std::abs (dOther)); louderOther = std::max (louderOther, dOther); ++n;
                txt << sc << "/" << k << " " << juce::String (d, 2) << " (" << juce::String (dOther, 1) << ")  ";
            }
        }
        std::printf ("    gain match over %d EVOLVE variations, whole plugin offline: reference mix worst %.2f LU | other material worst %.1f LU (loudest +%.1f)\n      %s\n",
                     n, worst, worstOther, louderOther, txt.toRawUTF8());
        check (worst <= 1.5f, "GHOST DNA variations gain-matched within 1.5 LU");
        check (worstOther <= 6.0f, "GHOST DNA gain match sane on other material");
    }
    // ---- 5) EVOLVE / audition / KEEP / UNDO (through the processor), the seed advances, state round trip
    {
        Proc q (true); q.scene ("dustydream"); q.pump (60);
        const auto orig = soundOf (q);
        q->dnaSeed = 100; q->dnaStrength = 0.5f;
        q->dnaEvolve(); const bool done = q->dnaWait();
        bool distinct = q->dnaCount == 4; for (int a = 0; a < 4 && distinct; ++a) for (int b = a + 1; b < 4; ++b) distinct = distinct && ! q->dnaVariations[(size_t) a].isEquivalentTo (q->dnaVariations[(size_t) b]);
        const bool seeds = (q->dnaSeeds[0] - 100) % 7919 == 0 && (q->dnaSeeds[3] - 103) % 7919 == 0 && q->dnaSeed == 104;   // (a dud rolls again at seed + 7919 k)
        const bool sameAsMutate = q->dnaVariations[2].isValid() && [&] { boxdna::Locks none; auto m = q->genome().mutate (orig, q->dnaSeeds[2], 0.5f, none);
            for (auto& prm : q->genome().all()) if (prm.id != "fx_lim_gain" && std::abs (boxdna::Genome::get (m, prm) - boxdna::Genome::get (q->dnaVariations[2], prm)) > 1.0e-6f) return false; return true; }();
        q->dnaAudition (2); q.pump (80); run (q, noise (0.05)); q.pump (40);
        int off = 0; for (auto& prm : q->genome().all()) if (std::abs (q.get (prm.id) - boxdna::Genome::get (q->dnaVariations[2], prm)) > 1.0e-5f * std::max (1.0f, std::abs (q.get (prm.id)))) ++off;
        q->dnaUndo(); q.pump (80); run (q, noise (0.05)); q.pump (40);
        int back = 0; for (auto& prm : q->genome().all()) if (std::abs (q.get (prm.id) - boxdna::Genome::get (orig, prm)) > 1.0e-5f * std::max (1.0f, std::abs (q.get (prm.id)))) ++back;
        q->dnaMutate(); q->dnaWait(); q.pump (80);
        const bool mutHeard = q->dnaAuditioning == 0 && q->dnaOriginalValid();
        q->dnaKeep(); const bool kept = ! q->dnaOriginalValid() && q->dnaCount == 0;
        // state: seed, strength, blend, locks
        q->dnaLocks.units.insert (uHalls); q->dnaLocks.params.insert (pid (uEcho, "time")); q->dnaBlend = 0.25f;
        juce::MemoryBlock mb; q->getStateInformation (mb); Proc r (true); r->setStateInformation (mb.getData(), (int) mb.getSize());
        const bool st = r->dnaSeed == q->dnaSeed && std::abs (r->dnaStrength - q->dnaStrength) < 1.0e-6f && std::abs (r->dnaBlend - 0.25f) < 1.0e-6f && r->dnaLocks == q->dnaLocks;
        std::printf ("    EVOLVE: done %d, 4 distinct %d, seeds 100..103 then 104 %d, = MUTATE at that seed %d | audition: %d settings off | UNDO TO ORIGINAL: %d off | MUTATE heard %d | KEEP %d | state %d\n",
                     (int) done, (int) distinct, (int) seeds, (int) sameAsMutate, off, back, (int) mutHeard, (int) kept, (int) st);
        check (done && distinct && seeds && sameAsMutate && off == 0 && back == 0 && mutHeard && kept && st, "GHOST DNA evolve / audition / undo / keep / state");
    }
    // ---- 6) the fuzz: 10,000 mutations through the processor (cumulative walks, restarted every 40), a drum + pad signal
    {
        Proc f (true); f.scene ("galaxyecho"); f.pump (60);
        auto& gf = f->genome(); const auto start = soundOf (f);
        rs = 5; const auto sig = mixOf (drums (4.0), pad (4.0), 1.0f);   // (loud on purpose)
        juce::Random rr (99); auto cur = start; int bad = 0; float worstPk = 0; size_t pos = 0;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        const long a0 = gAudioAllocs.load();
        for (int i = 0; i < 10000; ++i)
        {
            boxdna::Locks none;
            cur = gf.mutate (i % 40 == 0 ? start : cur, 20000 + i, 0.2f + 0.8f * rr.nextFloat(), none);
            applyFast (f, gf, cur);
            Stereo part; part.resize (256);
            for (size_t k = 0; k < 256; ++k) { part.l[k] = sig.l[(pos + k) % sig.size()]; part.r[k] = sig.r[(pos + k) % sig.size()]; }
            pos += 256;
            const auto out = run (f, part, nullptr, 256);
            const float pk = peak (out);
            if (! finite (out) || pk > 1.0f) ++bad;
            worstPk = std::max (worstPk, finite (out) ? pk : 99.0f);
        }
        const long allocs = gAudioAllocs.load() - a0;
        std::printf ("    fuzz: 10000 mutations, %d blocks non-finite or above 0 dBFS, worst peak %.3f, %.1f s, audio-thread allocations %ld\n", bad, worstPk, (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0, allocs);
        check (bad == 0 && worstPk <= 1.0f, "GHOST DNA fuzz: finite, peak below 0 dBFS after the safety limiter");
    }
}

// [33b] the DNA page: tab after CAPTURE, chips for the units that are on, variation tiles, contrast in 4 looks, screenshots
static void testDnaUi (const juce::File& shots)
{
    std::printf ("\n[33b] GHOST DNA page: tab, chips, tiles, locks by click, contrast in 4 looks, screenshots\n");
    if (shots != juce::File()) shots.createDirectory();
    Proc p (true); p.scene ("galaxyecho"); p.set (pid (uWreck, "on"), 1); p.pump (60);
    p->dnaSeed = 2026; p->dnaEvolve(); p->dnaWait(); p.pump (40);
    std::unique_ptr<juce::AudioProcessorEditor> edp (p->createEditor()); auto* ed = dynamic_cast<BoxEditor*> (edp.get());
    auto* panel = findPanel (*ed);
    auto* capTab = buttonIn (*panel, "CAPTURE"); auto* dnaTab = buttonIn (*panel, "DNA");
    const bool tabOk = dnaTab != nullptr && dnaTab->isVisible() && capTab != nullptr && dnaTab->getX() >= capTab->getRight();
    // at the smallest window the tab may not fit: the scene bar's ... menu opens the page too
    ed->setSize (960, 720 + BoxEditor::kSceneBar); pumpLoop (20);
    const bool narrowTab = dnaTab != nullptr && dnaTab->isVisible();
    ed->sceneBar().moreMenuResult (7); pumpLoop (40);
    const bool viaMenu = findFirst<BoxDnaPage> (*ed) != nullptr;
    ed->setSize (1180, 780 + BoxEditor::kSceneBar); pumpLoop (20);
    std::printf ("    at 960 px: DNA tab %s; the ... menu opens the DNA page %d\n", narrowTab ? "visible" : "hidden", (int) viaMenu);
    check (viaMenu, "GHOST DNA reachable from the scene bar menu");
    if (dnaTab) dnaTab->triggerClick();
    pumpLoop (60);
    auto* page = findFirst<BoxDnaPage> (*ed);
    const bool chipsOk = page != nullptr && page->chip (uWreck) != nullptr && page->chip (uEcho) != nullptr && page->chip (uLim) == nullptr;
    if (page) page->chipClick (uWreck);
    const bool locked = p->dnaLocks.unitLocked (uWreck);
    if (page) page->chipClick (uWreck);
    p->dnaAudition (1); pumpLoop (80);
    std::printf ("    DNA tab after CAPTURE %d | chips for the units that are on (not the limiter) %d | a chip click locks + unlocks %d\n", (int) tabOk, (int) chipsOk, (int) (locked && ! p->dnaLocks.unitLocked (uWreck)));
    check (tabOk && chipsOk && locked && ! p->dnaLocks.unitLocked (uWreck), "GHOST DNA tab / chips / locks");
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
        if (shots != juce::File()) snapTo (*ed, shots.getChildFile ("dna-" + lookName (look).toLowerCase() + ".png"));
    }
    std::printf ("    contrast: worst text / need %.2f (%s)\n", worst, worstWhat.toRawUTF8());
    check (worst >= 1.0, "GHOST DNA page text contrast in every look");
    edp.reset();
}
