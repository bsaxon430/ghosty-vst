// TEMPO  ½x | 1x | 2x  (Brandon 2026-11-09: Ghost Box gets the tempo switch the other plugins got; Shared/TempoMul.h).
// Every tempo-synced thing in the box (GATE / GRID / HAUNT patterns, synced ECHO / SPLIT echo times, synced LFOs, PUMP, MOTION lanes,
// scenes landing on bars, GHOST SPACE orbits) follows the host tempo times the switch, because the processor scales the host's tempo
// AND song position before the rack sees them.
//   GhostBoxTests tempohash          -> the 1x renders against fixtures/tempo-1x-hashes.txt
//   GhostBoxTests tempohash write    -> (re)write that fixture (ONLY from the build before the switch existed: it is the reference)
//   GhostBoxTests tempo              -> the whole TEMPO section

static juce::File tempoHashFile() { return fixtureDir().getChildFile ("tempo-1x-hashes.txt"); }

// a few tempo-synced setups (each a fresh instance)
static void tempoSetup (Proc& p, const juce::String& what)
{
    if (what == "rhythm")
    {   // the RHYTHM units + a synced echo, nothing else
        p.scene ("dry");
        for (int u : { (int) uGate, (int) uGrid, (int) uHaunt, (int) uEcho }) p.set (pid (u, "on"), 1);
        p.set (pid (uEcho, "sync"), 1);
    }
    else if (what == "motion")
    {   // a scene + a 4-bar MOTION (timeline lanes run on the song position)
        p.scene ("galaxyecho");
        for (auto& m : factoryMotions()) if (m.name == "4-Bar Riser") p.impl().box.applyMotion (m);
    }
    else p.scene (what.toRawUTF8());
}
static const juce::StringArray& tempoSetups() { static juce::StringArray s { "rhythm", "galaxyecho", "tapedrop", "warpflick", "retrofat", "motion" }; return s; }

static juce::uint64 tempoRender (const juce::String& what, double hostBpm, int code, bool setCode, Stereo* keep = nullptr)
{
    rs = 1; const auto sig = mixOf (drums (4.0, hostBpm), pad (4.0), 0.8f);
    Proc p (true); p.head.bpm = hostBpm; p.head.ppq = 0; p.head.playing = true;
    tempoSetup (p, what);
    juce::ignoreUnused (code, setCode);
#ifdef GHOSTBOX_HAS_TEMPOMUL
    if (setCode) p->tempoMul.store (code);
#endif
    p.pump (150);
    p->prepareToPlay (SR, 512); p.head.ppq = 0;
    auto out = run (p, sig);
    const auto h = hashStereo (out);
    if (keep) *keep = std::move (out);
    return h;
}

static std::vector<std::pair<juce::String, juce::uint64>> tempoHashes()
{
    std::vector<std::pair<juce::String, juce::uint64>> out;
    for (auto& w : tempoSetups())
        for (double bpm : { 60.0, 120.0, 140.0, 240.0 })
            out.push_back ({ w + "@" + juce::String ((int) bpm), tempoRender (w, bpm, 1, false) });
    return out;
}

static int tempoHashMode (bool write)
{
    const auto a = tempoHashes(), b = tempoHashes();
    bool det = a.size() == b.size(); for (size_t i = 0; det && i < a.size(); ++i) det = a[i].second == b[i].second;
    if (! det) { std::printf ("tempo renders are not deterministic run to run\n"); return 1; }
    juce::StringArray lines; lines.addLines (tempoHashFile().loadFileAsString()); lines.removeEmptyStrings();
    juce::String s; int bad = 0;
    for (auto& [n, h] : a)
    {
        s << n << " " << juce::String::toHexString ((juce::int64) h) << "\n";
        juce::String want; for (auto& l : lines) if (l.upToFirstOccurrenceOf (" ", false, false) == n) want = l.fromFirstOccurrenceOf (" ", false, false).trim();
        if (! write) { const bool ok = want == juce::String::toHexString ((juce::int64) h); if (! ok) ++bad; std::printf ("  %-20s %s %s\n", n.toRawUTF8(), juce::String::toHexString ((juce::int64) h).toRawUTF8(), ok ? "identical" : "DIFFERENT"); }
    }
    if (write) { tempoHashFile().replaceWithText (s); std::printf ("%s", s.toRawUTF8()); return 0; }
    return bad;
}

#ifdef GHOSTBOX_HAS_TEMPOMUL
// a synced ECHO alone, after 5 s of playing (its time glide has landed): the echo's own delay time in seconds (what it reads from
// its delay line; the tape wobble around it is left out on purpose, so the number is exact)
static double tempoEchoSecs (double hostBpm, int code)
{
    Proc p (false); p.head.bpm = hostBpm; p.head.ppq = 0; p.head.playing = true;
    p.scene ("dry"); p.set ("box_fixlat", 0);
    p.set (pid (uEcho, "on"), 1); p.set (pid (uEcho, "sync"), 1); p.set (pid (uEcho, "fb"), 0.0f); p.set (pid (uEcho, "mix"), 1.0f);
    p->tempoMul.store (code);
    p.pump (150); p->prepareToPlay (SR, 512); p.head.ppq = 0;
    Stereo in; in.resize (S (5.0)); in.l[S (1.0)] = in.r[S (1.0)] = 0.8f;   // (5 s: the echo's time glide has landed)
    run (p, in);
    return (double) p.impl().tm.echoMs.load() * 0.001;
}

static void testTempo()
{
    namespace tm = ghosty::tempomul;
    std::printf ("\n[40] TEMPO 1/2x | 1x | 2x: every tempo-synced effect follows the DAW's tempo x the switch; 1x is exactly as before\n");
    {   // a new instance is at 1x and a 1x project writes nothing
        BoxProcessor p; check (p.tempoMul.load() == tm::Normal, "a new Ghost Box starts at 1x");
        check (! p.stateTree().hasProperty (tm::stateKey), "a 1x project writes no tempo property (old builds read it unchanged)");
    }
#if JUCE_LINUX   // (the reference was written on Linux by the build before TEMPO; other systems' float maths differ, so it is checked here only)
    {   // 1x = bit-identical to the build before the switch (fixture written by that build), with the switch untouched and set to 1x again
        juce::StringArray lines; lines.addLines (tempoHashFile().loadFileAsString()); lines.removeEmptyStrings();
        int bad = 0, n = 0;
        for (auto& w : tempoSetups())
            for (double bpm : { 60.0, 120.0, 140.0, 240.0 })
            {
                const juce::String key = w + "@" + juce::String ((int) bpm);
                juce::String want; for (auto& l : lines) if (l.upToFirstOccurrenceOf (" ", false, false) == key) want = l.fromFirstOccurrenceOf (" ", false, false).trim();
                const auto h0 = juce::String::toHexString ((juce::int64) tempoRender (w, bpm, tm::Normal, false));
                const auto h1 = juce::String::toHexString ((juce::int64) tempoRender (w, bpm, tm::Normal, true));
                ++n; if (want.isEmpty() || h0 != want || h1 != want) { ++bad; std::printf ("    %-18s want %s, untouched %s, set to 1x %s\n", key.toRawUTF8(), want.toRawUTF8(), h0.toRawUTF8(), h1.toRawUTF8()); }
            }
        std::printf ("    1x renders identical to the build before TEMPO: %d of %d (6 setups x 60/120/140/240 bpm)\n", n - bad, n);
        check (bad == 0 && n == 24 && lines.size() >= 24, "1x must be bit-identical to the build before the TEMPO switch");
    }
#else
    std::printf ("    (the 1x reference renders were written on Linux and are checked there)\n");
    for (auto& w : tempoSetups()) check (tempoRender (w, 120.0, tm::Normal, false) == tempoRender (w, 120.0, tm::Normal, true), w + ": setting 1x changes nothing");
#endif
    {   // 2x at the DAW's 120 == 1x at 240, 1/2x at 120 == 1x at 60 (same input), sample for sample, for every setup
        int same = 0, n = 0;
        for (auto& w : tempoSetups())
        {
            auto renderAt = [&w] (double bpm, int code)
            {
                rs = 1; const auto sig = mixOf (drums (4.0, 120.0), pad (4.0), 0.8f);
                Proc p (true); p.head.bpm = bpm; p.head.ppq = 0; p.head.playing = true; tempoSetup (p, w); p->tempoMul.store (code);
                p.pump (150); p->prepareToPlay (SR, 512); p.head.ppq = 0; return hashStereo (run (p, sig));
            };
            const auto d2 = renderAt (120.0, tm::Double), r240 = renderAt (240.0, tm::Normal), h2 = renderAt (120.0, tm::Half), r60 = renderAt (60.0, tm::Normal), one = renderAt (120.0, tm::Normal);
            n += 2; same += (d2 == r240) + (h2 == r60);
            std::printf ("    %-11s 2x@120 %s 1x@240, 1/2x@120 %s 1x@60, and 2x/1/2x differ from 1x: %s\n", w.toRawUTF8(), d2 == r240 ? "==" : "!=", h2 == r60 ? "==" : "!=", d2 != one && h2 != one ? "yes" : (w == "retrofat" ? "no (right: nothing in it is tempo-synced)" : "NO"));
            check (d2 == r240 && h2 == r60, w + ": 2x / 1/2x must be exactly the DAW at double / half tempo");
            if (w == "retrofat") check (d2 == one && h2 == one, w + " (nothing tempo-synced in it): 2x and 1/2x must leave it exactly as it is");
            else check (d2 != one && h2 != one, w + ": 2x and 1/2x must change the sound");
        }
        std::printf ("    %d of %d renders sample-identical to the DAW at the multiplied tempo\n", same, n);
    }
    {   // a synced ECHO's time, measured. The echo glides to a new time and its float glide stops a few samples short of it (by up to
        // ~0.5 ms on long times, exactly as it always did when the DAW's tempo changed), so: the same number as the DAW at 240 / 60, and
        // within that glide's resolution of exactly half / double
        const double e1 = tempoEchoSecs (120.0, tm::Normal), e2 = tempoEchoSecs (120.0, tm::Double), eh = tempoEchoSecs (120.0, tm::Half);
        const double d240 = tempoEchoSecs (240.0, tm::Normal), d60 = tempoEchoSecs (60.0, tm::Normal);
        const double want = echoDivs()[7].beats * 0.5;   // the default 1/8D at 120 bpm
        std::printf ("    synced ECHO (1/8D) at 120 bpm: 1x %.5f s, 2x %.5f s, 1/2x %.5f s (exact: %.5f / %.5f / %.5f); the DAW at 240: %.5f s, at 60: %.5f s\n",
                     e1, e2, eh, want, want / 2, want * 2, d240, d60);
        check (e2 == d240 && eh == d60, "synced echo at 2x / 1/2x must be exactly the DAW at double / half tempo");
        check (std::abs (e1 - want) < 2.0e-6 && std::abs (e2 - want / 2) < 0.0006 && std::abs (eh - want * 2) < 0.0006, "synced echo time must be x1 / x1/2 / x2");
    }
    {   // saved with the project; old / bad values = 1x; A/B, snapshots and scenes never change it
        BoxProcessor a; a.tempoMul.store (tm::Double);
        juce::MemoryBlock mb; a.getStateInformation (mb);
        BoxProcessor b; b.setStateInformation (mb.getData(), (int) mb.getSize());
        check (b.tempoMul.load() == tm::Double, "2x is saved with the project and comes back");
        a.tempoMul.store (tm::Half); a.getStateInformation (mb); BoxProcessor c; c.setStateInformation (mb.getData(), (int) mb.getSize());
        check (c.tempoMul.load() == tm::Half, "1/2x is saved with the project and comes back");
        auto t = a.stateTree(); t.removeProperty (tm::stateKey, nullptr); c.applyStateTree (t);
        check (c.tempoMul.load() == tm::Normal, "a project from before TEMPO opens at 1x");
        t.setProperty (tm::stateKey, 7, nullptr); c.tempoMul.store (tm::Double); c.applyStateTree (t);
        check (c.tempoMul.load() == tm::Normal, "a bad value opens at 1x");
        BoxProcessor d; d.tempoMul.store (tm::Double); d.abToggle(); d.tempoMul.store (tm::Half); d.abToggle();
        check (d.tempoMul.load() == tm::Half, "A/B leaves TEMPO alone");
        d.restoreSound (a.captureSound());
        check (d.tempoMul.load() == tm::Half && ! a.captureSound().hasProperty (tm::stateKey), "a snapshot neither stores nor changes TEMPO");
        d.sceneStore (0); check (! d.sceneBank.slots[0].sound.hasProperty (tm::stateKey), "a scene never stores TEMPO");
    }
    {   // the switch in the window
        BoxProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor()); auto* be = dynamic_cast<BoxEditor*> (ed.get());
        pumpLoop (60);
        auto& sw = be->tempoSwitch();
        check (sw.isVisible() && sw.getWidth() >= 140 && sw.getHeight() >= 26 && ed->getLocalBounds().contains (ed->getLocalArea (&sw, sw.getLocalBounds())), "the TEMPO switch is in the window");
        check (sw.sel == tm::Normal, "the switch shows 1x on a fresh start");
        auto click = [&sw] (int i) { const auto c = sw.segment (i).getCentre(); juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(), c, juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &sw, &sw, juce::Time::getCurrentTime(), c, juce::Time::getCurrentTime(), 1, false); sw.mouseDown (e); };
        click (2); check (p.tempoMul.load() == tm::Double, "clicking 2x sets 2x");
        click (0); check (p.tempoMul.load() == tm::Half, "clicking 1/2x sets 1/2x");
        p.tempoMul.store (tm::Double); pumpLoop (80); check (sw.sel == tm::Double, "the switch follows a loaded project");
        click (1); check (p.tempoMul.load() == tm::Normal, "clicking 1x sets 1x");
    }
}
#endif
