// [31] GHOST SCENES: exact recall, CUT on the grid, MORPH monotone + click-free, SPILL tails, hold / return (MIDI + window), host
// automation of box_scene, state round trip, old sessions, undo, no audio-thread allocation.

static Stereo runWithMidi (Proc& pr, const Stereo& in, const std::vector<std::pair<size_t, juce::MidiMessage>>& events, int block = 512)
{   // like run(), plus MIDI events at sample positions
    auto& p = *pr.p;
    Stereo out; out.resize (in.size());
    juce::AudioBuffer<float> buf (2, block); juce::MidiBuffer midi;
    for (size_t pos = 0; pos < in.size(); pos += (size_t) block)
    {
        const int n = (int) std::min<size_t> ((size_t) block, in.size() - pos);
        buf.setSize (2, n, false, false, true);
        std::copy (in.l.begin() + (long) pos, in.l.begin() + (long) pos + n, buf.getWritePointer (0));
        std::copy (in.r.begin() + (long) pos, in.r.begin() + (long) pos + n, buf.getWritePointer (1));
        midi.clear();
        for (auto& [at, m] : events) if (at >= pos && at < pos + (size_t) n) midi.addEvent (m, (int) (at - pos));
        { AudioScope as; p.processBlock (buf, midi); }
        pr.head.ppq += (double) n / (60.0 / pr.head.bpm * SR);
        std::copy (buf.getReadPointer (0), buf.getReadPointer (0) + n, out.l.begin() + (long) pos);
        std::copy (buf.getReadPointer (1), buf.getReadPointer (1) + n, out.r.begin() + (long) pos);
    }
    return out;
}
static void sceneSet (Proc& p, int mode, int quant, int morphLen = 4)
{
    p->sceneBank.set.mode = mode; p->sceneBank.set.quant = quant; p->sceneBank.set.morphLen = morphLen; p->sceneSettingsChanged();
}
// every scene-able parameter's real value now vs what slot k stored (count of mismatches; first one named)
static int sceneMismatch (Proc& p, int k, juce::String* first = nullptr)
{
    const auto& snd = p->sceneBank.slots[(size_t) k].sound; int bad = 0;
    for (auto* prm : p->getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            const auto id = rp->getParameterID();
            if (BoxProcessor::notInScene (id) || ! (id.startsWith ("fx_") || id.startsWith ("box_"))) continue;
            const auto c = snd.getChildWithProperty ("id", id); if (! c.isValid()) continue;
            const float want = rp->convertFrom0to1 (rp->convertTo0to1 ((float) c.getProperty ("value"))), now = p.get (id);
            if (std::abs (want - now) > 1.0e-6f * std::max (1.0f, std::abs (want))) { if (first != nullptr && first->isEmpty()) *first = id; ++bad; }
        }
    return bad;
}
static int shadowMismatch (Proc& p)
{   // what the units read == the host values (the scene layer has let go)
    auto& box = p.impl().box; int bad = 0;
    for (int i = 0; i < box.numSlots(); ++i) if (std::abs (box.effective (box.slotId (i)) - p.get (box.slotId (i))) > 1.0e-6f) ++bad;
    return bad;
}

static void testGhostScenes()
{
    std::printf ("\n[31] GHOST SCENES: exact recall, CUT on the grid, MORPH monotone + click-free, SPILL tails, hold / return, box_scene automation, state, undo, no allocation\n");
    const double spb = 60.0 / 120.0 * SR;
    // ---- 1) store + recall: the parameters equal the stored scene, the shadows let go, box_scene follows
    {
        Proc p (true); p.pump();
        p.scene ("galaxyecho"); p.set ("box_mix", 0.8f); p.pump (30); p->sceneStore (1);
        p.scene ("dustydream"); p.set ("box_mix", 0.6f); p.pump (30); p->sceneStore (2);
        p.scene ("dry"); p.set ("box_mix", 1.0f); p.pump (30);
        sceneSet (p, gpfx::smCut, 0);
        int badAll = 0; juce::String first; bool shadowsOk = true, activeOk = true, paramOk = true;
        for (int k : { 1, 2, 1 })
        {
            p->sceneTrigger (k); run (p, noise (0.05)); p.pump (60); run (p, noise (0.1)); p.pump (30);
            badAll += sceneMismatch (p, k, &first);
            shadowsOk = shadowsOk && shadowMismatch (p) == 0 && ! p.impl().box.scenes.on();
            activeOk = activeOk && p->sceneEngine().active.load() == k;
            paramOk = paramOk && std::lround (p.get ("box_scene")) == k + 1;
        }
        const bool empty = p->sceneBank.slots[0].filled() == false;
        p->sceneTrigger (0); run (p, noise (0.05)); p.pump (40);   // an empty slot does nothing
        const bool emptyNoop = p->sceneEngine().active.load() == 1;
        std::printf ("    recall 3x: %d settings off%s | units read the host values again %d | active %d | box_scene follows %d | empty slot ignored %d\n",
                     badAll, first.isNotEmpty() ? (" (" + first + ")").toRawUTF8() : "", (int) shadowsOk, (int) activeOk, (int) paramOk, (int) (empty && emptyNoop));
        check (badAll == 0 && shadowsOk && activeOk && paramOk && empty && emptyNoop, "GHOST SCENES recall must equal the stored scene");
    }
    // ---- 2) CUT lands on the grid: sample-accurate (no difference before the grid sample, a difference right on it)
    {
        auto make = [] (Proc& q)
        {
            q.set ("box_fixlat", 0); q.set (pid (uWreck, "on"), 1); q.set (pid (uWreck, "drive"), 0.15f); q.pump (30); q->sceneStore (0);
            q.set (pid (uWreck, "drive"), 0.9f); q.pump (30); q->sceneStore (1);
            q.set (pid (uWreck, "drive"), 0.15f); q.pump (30);
        };
        const auto sig = sine (2.4, 220.0, 0.4f);
        Proc ref (false); make (ref); sceneSet (ref, gpfx::smCut, 3);
        Proc q (false); make (q); sceneSet (q, gpfx::smCut, 3);
        ref.pump (30); q.pump (30);
        const int lat = q->rack.latencySamples();
        // trigger at beat 0.3: lands on beat 1 (sample 24000)
        Stereo a = run (ref, slice (sig, 0, S (0.15))), b = run (q, slice (sig, 0, S (0.15)));
        q->sceneTrigger (1);
        append (a, run (ref, slice (sig, S (0.15), sig.size()))); append (b, run (q, slice (sig, S (0.15), sig.size())));
        long firstDiff = -1; float before = 0;
        for (size_t i = 0; i < a.size() && firstDiff < 0; ++i) if (std::abs (a.l[i] - b.l[i]) > 1.0e-5f || std::abs (a.r[i] - b.r[i]) > 1.0e-5f) firstDiff = (long) i;
        for (size_t i = 0; i < (size_t) spb && i < a.size(); ++i) before = std::max ({ before, std::abs (a.l[i] - b.l[i]), std::abs (a.r[i] - b.r[i]) });
        std::printf ("      (largest difference before the grid sample: %.2e)\n", before);
        const double landAt = q->sceneEngine().lastLandPpq.load();
        // (the split chunk at the grid changes float rounding by a few 1e-6 before it; the new drive reaches the output through the
        // unit's oversampling filter, centred on its latency, which the host's delay compensation lines up with the grid)
        const bool onGrid = std::abs (landAt - 1.0) * spb < 0.5 && before < 1.0e-4f && firstDiff >= (long) std::lround (spb) && firstDiff <= (long) std::lround (spb) + lat + (long) S (0.003);   // (+ the 10 ms de-click glide's first steps)
        // BAR quantise (4/4 without a host bar position: from ppq 0): a trigger at beat 1.2 lands on beat 4
        Proc r (false); make (r); sceneSet (r, gpfx::smCut, 4); r.pump (30);
        run (r, slice (sig, 0, S (0.6))); r->sceneTrigger (1); run (r, slice (sig, 0, S (1.9)));
        const double barAt = r->sceneEngine().lastLandPpq.load();
        // transport stopped: lands at once (the next chunk), whatever the quantise
        Proc s (false); make (s); sceneSet (s, gpfx::smCut, 4); s.head.playing = false; s.pump (30);
        run (s, slice (sig, 0, S (0.1))); s->sceneTrigger (1); run (s, slice (sig, 0, 512)); const bool stoppedNow = s->sceneEngine().active.load() == 1;
        std::printf ("    CUT, BEAT: landed at ppq %.6f, first changed sample %ld (grid %ld + latency %d) | BAR: landed at ppq %.6f | stopped transport: at once %d\n",
                     landAt, firstDiff, (long) std::lround (spb), lat, barAt, (int) stoppedNow);
        check (onGrid, "GHOST SCENES CUT must land exactly on the beat");
        check (std::abs (barAt - 4.0) * spb < 0.5 && stoppedNow, "GHOST SCENES BAR quantise / stopped transport");
    }
    // ---- 3) MORPH: every glide monotone, units fading in through their mix, click-free
    {
        auto make = [] (Proc& q)
        {
            q.set (pid (uFilter, "on"), 1); q.set (pid (uFilter, "cutoff"), 300.0f); q.set (pid (uFilter, "lfoamt"), 0.0f); q.set (pid (uHalls, "on"), 0); q.pump (30); q->sceneStore (0);
            q.set (pid (uFilter, "cutoff"), 8000.0f); q.set (pid (uHalls, "on"), 1); q.set (pid (uHalls, "mix"), 0.4f); q.pump (30); q->sceneStore (1);
            q->sceneTrigger (0); run (q, noise (0.02)); q.pump (60); run (q, noise (0.02)); q.pump (30);
        };
        Proc q (true); make (q); sceneSet (q, gpfx::smMorph, 3, 4);   // 1 BAR = 2 s at 120
        const auto sig = mixOf (sine (6.0, 220.0, 0.3f), sine (6.0, 1650.0, 0.1f));   // a steady signal: every edge is the morph's
        Stereo out = run (q, slice (sig, 0, S (0.5)));
        q->sceneTrigger (1);
        std::vector<float> cut, mix; std::vector<int> landed; Stereo morph;
        for (size_t pos = S (0.5); pos < S (3.2); pos += 512)
        {
            auto part = run (q, slice (sig, pos, std::min (sig.size(), pos + 512))); append (out, part);
            cut.push_back (q.impl().box.effective (pid (uFilter, "cutoff"))); mix.push_back (q.impl().box.effective (pid (uHalls, "mix")));
            landed.push_back (q->sceneEngine().active.load() == 1 ? 1 : 0);
            q.pump (1);
        }
        if (gOnly.contains ("dbg")) for (size_t i = 0; i < mix.size(); ++i) std::printf ("      %zu cut %.1f mix %.4f\n", i, cut[i], mix[i]);
        bool mono = true, monoMix = true; for (size_t i = 1; i < cut.size(); ++i) { mono = mono && cut[i] >= cut[i - 1] - 1.0e-3f; if (landed[i - 1]) monoMix = monoMix && mix[i] >= mix[i - 1] - 1.0e-6f; }   // (from the landing: HALLS was off before, its mix value meant nothing)
        const bool ends = std::abs (cut.front() - 300.0f) < 30.0f && std::abs (cut.back() - 8000.0f) < 1.0f && std::abs (mix.back() - 0.4f) < 1.0e-4f;
        // the largest sample step during the morph vs. the same pad through scene A and scene B held still
        auto maxStep = [] (const Stereo& s, size_t a, size_t b) { float m = 0; for (size_t i = std::max<size_t> (a, 1); i < b && i < s.size(); ++i) m = std::max ({ m, std::abs (s.l[i] - s.l[i - 1]), std::abs (s.r[i] - s.r[i - 1]) }); return m; };
        Proc ra (true); make (ra); const auto oa = run (ra, sig);
        Proc rb (true); make (rb); rb->sceneTrigger (1); run (rb, noise (0.02)); rb.pump (60); const auto ob = run (rb, sig);
        const float stepMorph = maxStep (out, S (0.5), S (3.0)), stepRef = std::max (maxStep (oa, S (0.5), S (3.0)), maxStep (ob, S (0.5), S (3.0)));
        double at = 0; const float cs = clickScore (slice (out, S (0.3), S (3.1)), &at);
        std::printf ("      (held scenes' click scores %.1f / %.1f)\n", clickScore (slice (oa, S (0.3), S (3.1))), clickScore (slice (ob, S (0.3), S (3.1))));
        std::printf ("    MORPH 1 bar: cutoff %.0f -> %.0f Hz monotone %d, HALLS mix fades in monotone %d | max sample step %.4f (held scenes %.4f) | click score %.1f at %.2f s\n",
                     cut.front(), cut.back(), (int) mono, (int) monoMix, stepMorph, stepRef, cs, at);
        check (mono && monoMix && ends, "GHOST SCENES MORPH must glide monotonically to the scene");
        check (stepMorph <= stepRef * 1.25f + 1.0e-4f && cs < 6.0f && finite (out), "GHOST SCENES MORPH must be click-free");
        // the same jump as a CUT (filter 300 -> 8000 Hz, HALLS on): de-clicked too
        Proc qc (true); make (qc); sceneSet (qc, gpfx::smCut, 3);
        Stereo oc = run (qc, slice (sig, 0, S (0.5))); qc->sceneTrigger (1);
        for (size_t pos = S (0.5); pos < S (3.2); pos += 512) { append (oc, run (qc, slice (sig, pos, std::min (sig.size(), pos + 512)))); qc.pump (1); }
        double atc = 0; const float csc = clickScore (slice (oc, S (0.3), S (3.1)), &atc);
        // the same change made by hand at the same moment (HALLS on + the cutoff set): what the units themselves do
        Proc qh (true); make (qh);
        Stereo oh = run (qh, slice (sig, 0, S (0.5)));
        for (size_t pos = S (0.5); pos < S (3.2); pos += 512)
        {
            if (pos >= S (0.5) + 512 * 46 && pos < S (0.5) + 512 * 47) { qh.set (pid (uFilter, "cutoff"), 8000.0f); qh.set (pid (uHalls, "on"), 1); qh.set (pid (uHalls, "mix"), 0.4f); }
            append (oh, run (qh, slice (sig, pos, std::min (sig.size(), pos + 512)))); qh.pump (1);
        }
        double ath = 0; const float csh = clickScore (slice (oh, S (0.3), S (3.1)), &ath);
        std::printf ("    CUT of the same jump: click score %.1f at %.2f s (the same change by hand: %.1f at %.2f s)\n", csc, atc, csh, ath);
        if (gOnly.contains ("dbg"))
        {
            const size_t c0 = S (0.3 + atc) - S (0.02);
            for (size_t w = 0; w < 40; ++w) { double e = 0; float pk = 0; for (size_t i = c0 + w * S (0.001); i < c0 + (w + 1) * S (0.001); ++i) { const double d = oc.l[i] - 2.0 * oc.l[i - 1] + oc.l[i - 2]; e += d * d; pk = std::max (pk, std::abs (oc.l[i])); } std::printf ("      t %.3f e %.3g pk %.3f\n", (double) (c0 + w * S (0.001)) / SR, e, pk); }
            std::printf ("      landed at ppq %.3f (render t %.3f)\n", qc->sceneEngine().lastLandPpq.load(), qc->sceneEngine().lastLandPpq.load() * 0.5 - 0.04);
        }
        // and a CUT of the filter alone (a pure settings jump)
        auto makeF = [] (Proc& q) { q.set (pid (uFilter, "on"), 1); q.set (pid (uFilter, "cutoff"), 300.0f); q.set (pid (uFilter, "lfoamt"), 0.0f); q.pump (30); q->sceneStore (0);
                                    q.set (pid (uFilter, "cutoff"), 8000.0f); q.pump (30); q->sceneStore (1); q->sceneTrigger (0); run (q, noise (0.02)); q.pump (60); run (q, noise (0.02)); q.pump (30); };
        Proc qf (true); makeF (qf); sceneSet (qf, gpfx::smCut, 3);
        Stereo of = run (qf, slice (sig, 0, S (0.5))); qf->sceneTrigger (1);
        for (size_t pos = S (0.5); pos < S (3.2); pos += 512) { append (of, run (qf, slice (sig, pos, std::min (sig.size(), pos + 512)))); qf.pump (1); }
        double atf = 0; const float csf = clickScore (slice (of, S (0.3), S (3.1)), &atf);
        Proc qfh (true); makeF (qfh);
        Stereo ofh = run (qfh, slice (sig, 0, S (0.5)));
        for (size_t pos = S (0.5); pos < S (3.2); pos += 512)
        {
            if (pos >= S (0.5) + 512 * 46 && pos < S (0.5) + 512 * 47) qfh.set (pid (uFilter, "cutoff"), 8000.0f);
            append (ofh, run (qfh, slice (sig, pos, std::min (sig.size(), pos + 512)))); qfh.pump (1);
        }
        double atfh = 0; const float csfh = clickScore (slice (ofh, S (0.3), S (3.1)), &atfh);
        std::printf ("    CUT of the filter alone (300 -> 8000 Hz): click score %.1f at %.2f s (by hand %.1f at %.2f s)\n", csf, atf, csfh, atfh);
        check (csf <= std::max (6.0f, csfh * 1.1f) && finite (of) && finite (oc) && csc <= std::max (6.0f, csh * 1.1f), "GHOST SCENES CUT must be click-free (never worse than the same change by hand)");
    }
    // ---- 4) SPILL: the old scene's reverb tail keeps ringing after the switch (CUT cuts it), then decays and ends
    {
        auto make = [] (Proc& q, int mode)
        {
            q.set (pid (uHalls, "on"), 1); q.set (pid (uHalls, "mix"), 0.5f); q.set (pid (uHalls, "decay"), 0.85f); q.pump (30); q->sceneStore (0);
            q.set (pid (uHalls, "on"), 0); q.pump (30); q->sceneStore (1);
            q->sceneTrigger (0); run (q, noise (0.02)); q.pump (60); run (q, noise (0.02)); q.pump (30);
            sceneSet (q, mode, 0);
        };
        Stereo burst = noise (0.6, 0.3); burst = withTail (burst, 12.0);
        Proc sp (true); make (sp, gpfx::smSpill);
        Proc ct (true); make (ct, gpfx::smCut);
        Stereo os = run (sp, slice (burst, 0, S (0.6))), oc = run (ct, slice (burst, 0, S (0.6)));
        sp->sceneTrigger (1); ct->sceneTrigger (1);
        const long a0 = gAudioAllocs.load();
        std::vector<float> spillSeen;
        for (size_t pos = S (0.6); pos < burst.size(); pos += 4800)
        {
            const auto e = std::min (burst.size(), pos + 4800);
            append (os, run (sp, slice (burst, pos, e))); append (oc, run (ct, slice (burst, pos, e)));
            sp.pump (1); ct.pump (1);
            spillSeen.push_back ((float) sp->sceneEngine().spillCount.load());
        }
        const long allocs = gAudioAllocs.load() - a0;
        const float eSp = rmsDb (os, S (0.7), S (1.0)), eCt = rmsDb (oc, S (0.7), S (1.0)), eLate = rmsDb (os, S (3.0), S (3.3));
        const bool rang = spillSeen.front() >= 1.0f;
        int endedAt = -1; for (size_t i = 0; i < spillSeen.size(); ++i) if (spillSeen[i] == 0.0f) { endedAt = (int) i; break; }
        const bool unitOff = ! sp.impl().active[(size_t) uHalls] || sp.impl().onSm[(size_t) uHalls].cur <= 0.0f;
        double at = 0; const float cs = clickScore (slice (os, S (0.3), S (2.0)), &at);
        std::printf ("    SPILL: tail 0.1-0.4 s after the switch %.1f dB (CUT %.1f dB), 2.4 s later %.1f dB | ringing %d, ended after %.1f s, HALLS idle %d | click score %.1f | audio allocations %ld\n",
                     eSp, eCt, eLate, (int) rang, endedAt < 0 ? -1.0 : endedAt * 0.1, (int) unitOff, cs, allocs);
        check (rang && eSp > eCt + 20.0f && eLate < eSp - 6.0f && endedAt > 0 && endedAt <= 112 && unitOff, "GHOST SCENES SPILL tail must continue, then decay and end");
        check (cs < 6.0f && finite (os) && allocs == 0, "GHOST SCENES SPILL click-free, no allocation");
    }
    // ---- 5) hold / return: a MIDI key holds a scene, the release goes back to exactly what played before; the same from the window
    {
        Proc p (true); p.pump();
        p.scene ("galaxyecho"); p.pump (30); p->sceneStore (3);
        p.scene ("dustydream"); p.set (pid (uFilter, "on"), 1); p.set (pid (uFilter, "cutoff"), 1234.0f); p.pump (30);
        std::map<juce::String, float> before; for (auto* prm : p->getParameters()) if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm)) if (! BoxProcessor::notInScene (rp->getParameterID())) before[rp->getParameterID()] = p.get (rp->getParameterID());
        sceneSet (p, gpfx::smCut, 0);
        const int base = p->sceneBank.set.midiBase;
        const long a0 = gAudioAllocs.load();
        runWithMidi (p, noise (0.1), { { 100, juce::MidiMessage::noteOn (1, base + 3, (juce::uint8) 100) } }); p.pump (60); run (p, noise (0.05)); p.pump (30);
        const bool heldOk = sceneMismatch (p, 3) == 0 && p->sceneEngine().active.load() == 3;
        runWithMidi (p, noise (0.1), { { 300, juce::MidiMessage::noteOff (1, base + 3) } }); p.pump (60); run (p, noise (0.05)); p.pump (30);
        const long allocs = gAudioAllocs.load() - a0;
        int off = 0; juce::String firstOff;
        for (auto& [id, v] : before) if (std::abs (p.get (id) - v) > 1.0e-5f * std::max (1.0f, std::abs (v))) { ++off; if (firstOff.isEmpty()) firstOff = id; }
        const bool backOk = off == 0 && p->sceneEngine().active.load() == -1 && shadowMismatch (p) == 0;
        // the window: a long press holds, a quick click stays
        p->scenePress (3); run (p, noise (0.05)); p.pump (60); p->sceneRelease (3); run (p, noise (0.05)); p.pump (60); run (p, noise (0.05)); p.pump (30);
        const bool pressBack = p->sceneEngine().active.load() == -1 && std::abs (p.get (pid (uFilter, "cutoff")) - 1234.0f) < 0.01f;
        p->scenePress (3); run (p, noise (0.02)); p->sceneClickUp (3); run (p, noise (0.05)); p.pump (60); run (p, noise (0.05)); p.pump (30);
        const bool clickStays = p->sceneEngine().active.load() == 3 && sceneMismatch (p, 3) == 0;
        // a key outside the 8 scene keys, and MIDI switched off: nothing
        runWithMidi (p, noise (0.05), { { 10, juce::MidiMessage::noteOn (1, base + 9, (juce::uint8) 100) } }); p.pump (40);
        p->sceneBank.set.midiOn = false; p->sceneSettingsChanged(); runWithMidi (p, noise (0.05), { { 10, juce::MidiMessage::noteOn (1, base, (juce::uint8) 100) } }); p.pump (40);
        const bool ignored = p->sceneEngine().active.load() == 3;
        std::printf ("    MIDI hold: scene on %d, release -> back: %d settings off%s, units on the host values %d | window press/release back %d, quick click stays %d | other keys / MIDI off ignored %d | audio allocations %ld\n",
                     (int) heldOk, off, firstOff.isNotEmpty() ? (" (" + firstOff + ")").toRawUTF8() : "", (int) backOk, (int) pressBack, (int) clickStays, (int) ignored, allocs);
        check (heldOk && backOk && pressBack && clickStays && ignored && allocs == 0, "GHOST SCENES hold / return");
    }
    // ---- 6) box_scene host automation triggers; the mirror never re-triggers; a restored session does not trigger; undo
    {
        Proc p (true); p.pump();
        p.scene ("galaxyecho"); p.pump (30); p->sceneStore (0); p.scene ("tape"); p.pump (30); p->sceneStore (5); p.scene ("dry"); p.pump (30);
        sceneSet (p, gpfx::smCut, 0); run (p, noise (0.02));   // (the first block reads box_scene as it is)
        const int landsBefore = p->sceneEngine().landSeq.load();
        p.set ("box_scene", 6.0f); run (p, noise (0.05)); p.pump (60); run (p, noise (0.1)); p.pump (60); run (p, noise (0.1));
        const bool autoOk = p->sceneEngine().active.load() == 5 && sceneMismatch (p, 5) == 0 && p->sceneEngine().landSeq.load() == landsBefore + 1;
        // undo: back to the sound before the scene
        const float tapeVal = p.get (pid (uTube, "on"));
        p.impl().undo(); p.pump (60); run (p, noise (0.05)); p.pump (30);
        const bool undone = p.get (pid (uTube, "on")) != tapeVal || sceneMismatch (p, 5) > 0;
        // state: names, settings and sounds come back; box_scene restores without a landing
        p->sceneRename (5, "TAPE IT"); p->sceneBank.set.mode = gpfx::smSpill; p->sceneBank.set.midiBase = 48; p->sceneSettingsChanged();
        juce::MemoryBlock mb; p->getStateInformation (mb);
        Proc r (true); r->setStateInformation (mb.getData(), (int) mb.getSize()); r.pump (30); run (r, noise (0.1)); r.pump (30);
        bool same = true; for (int k = 0; k < boxscenes::kSlots; ++k) same = same && r->sceneBank.slots[(size_t) k] == p->sceneBank.slots[(size_t) k];
        same = same && r->sceneBank.set == p->sceneBank.set && r->sceneBank.slots[5].name == "TAPE IT";
        const bool noLand = r->sceneEngine().landSeq.load() == 0 && r->sceneEngine().active.load() == p->sceneEngine().active.load();
        // an old session: 8 empty slots with the default names, default settings
        Proc o (true); juce::MemoryBlock ob; { auto xml = juce::XmlDocument::parse (fixtureDir().getChildFile ("ghostbox2_retro_gate.xml")); if (xml) o->copyXmlToBinary (*xml, ob); }
        o->setStateInformation (ob.getData(), (int) ob.getSize());
        const bool oldEmpty = ! o->sceneBank.anyFilled() && o->sceneBank.slots[7].name == "DROP" && o->sceneBank.set == boxscenes::Settings() && o.get ("box_scene") == 0.0f && std::abs (o.get ("box_scene_morph") - 1.0f) < 1.0e-6f;
        // slot-edit undo
        p->sceneClear (0); const bool cleared = ! p->sceneBank.slots[0].filled(); p->sceneUndo(); const bool back = p->sceneBank.slots[0].filled();
        // snapshots never carry the scene slots or the scene switch
        const auto snap = p->captureSound(); const bool snapClean = ! snap.getChildWithName ("GHOSTSCENES").isValid() && ! snap.getChildWithProperty ("id", "box_scene").isValid();
        std::printf ("    box_scene automation -> scene 6 (one landing) %d | undo %d | state round trip %d, restored without a landing %d | old session: empty + defaults %d | slot undo %d | snapshots clean %d\n",
                     (int) autoOk, (int) undone, (int) same, (int) noLand, (int) oldEmpty, (int) (cleared && back), (int) snapClean);
        check (autoOk && undone && same && noLand && oldEmpty && cleared && back && snapClean, "GHOST SCENES automation / state / undo");
    }
    // ---- 7) SCENE MORPH parameter: 50 % = half way between the sound before and the scene; starters fill empty slots only
    {
        Proc p (true); p.pump();
        p.set (pid (uFilter, "on"), 1); p.set (pid (uFilter, "cutoff"), 400.0f); p.pump (30); p->sceneStore (0);
        p.set (pid (uFilter, "cutoff"), 6400.0f); p.pump (30); p->sceneStore (1);
        sceneSet (p, gpfx::smCut, 0);
        p->sceneTrigger (0); run (p, noise (0.05)); p.pump (60); run (p, noise (0.05));
        p.set ("box_scene_morph", 0.5f); p->sceneTrigger (1); run (p, noise (0.05)); p.pump (60); run (p, noise (0.1));
        const float half = p.impl().box.effective (pid (uFilter, "cutoff"));   // log glide: sqrt (400 * 6400) = 1600
        p.set ("box_scene_morph", 1.0f); run (p, noise (0.1)); p.pump (30); run (p, noise (0.05));
        const bool full = std::abs (p.impl().box.effective (pid (uFilter, "cutoff")) - 6400.0f) < 0.5f && ! p.impl().box.scenes.on();
        const int filled = p->sceneFillStarters();
        bool startersOk = filled == 6 && p->sceneBank.slots[0] == p->sceneBank.slots[0];
        int starterBad = 0; for (int k = 2; k < 8; ++k) { p->sceneTrigger (k); run (p, pad (0.3)); p.pump (60); auto o = run (p, pad (0.6)); p.pump (30); if (! finite (o) || peak (o) > 1.05f || rmsDb (o) < -60.0f) ++starterBad; }
        std::printf ("    SCENE MORPH 50 %%: cutoff %.0f Hz (log half way 1600) -> 100 %%: at the scene %d | starters filled %d empty slots, %d misbehaving\n", half, (int) full, filled, starterBad);
        check (std::abs (half - 1600.0f) < 20.0f && full, "GHOST SCENES scene morph parameter");
        check (startersOk && starterBad == 0, "GHOST SCENES starters");
    }
}

static juce::String lookName (int l) { return BoxEditor::lookName (l); }
// [31b] GHOST SCENES bar: below the rack (the rack keeps its old bounds), mouse (store / click / hold / menus), text contrast on the
// rendered pixels in BLACK / PURPLE / LIGHT / GHOST, screenshots of each look to <shots>.
static void testSceneUi (const juce::File& shots)
{
    std::printf ("\n[31b] GHOST SCENES bar: placement (no existing control moved), mouse, menus, contrast in all 4 looks, screenshots\n");
    if (shots != juce::File()) shots.createDirectory();
    Proc p (true); p.pump();
    p.scene ("galaxyecho"); p.pump (30); p->sceneStore (0); p.scene ("retrofat"); p.pump (30); p->sceneStore (1); p.scene ("dustydream"); p.pump (30); p->sceneStore (4);
    p->sceneBank.set.mode = gpfx::smMorph; p->sceneBank.set.morphLen = 6; p->sceneSettingsChanged();
    std::unique_ptr<juce::AudioProcessorEditor> edp (p->createEditor());
    auto* ed = dynamic_cast<BoxEditor*> (edp.get());
    auto* panel = findPanel (*ed);
    auto& bar = ed->sceneBar();
    // placement (Ghost Box 3.2: the window opens at the fitting size kOpenW x kOpenH, Brandon 2026-11-06): the rack panel fills the space
    // between the header and the bar, the bar is at the bottom
    const int W = BoxEditor::kOpenW, H = BoxEditor::kOpenH;
    const bool sizeOk = ed->getWidth() == W && ed->getHeight() == H;
    const bool panelOk = panel != nullptr && panel->getBounds() == juce::Rectangle<int> (0, 62, W, H - 62 - BoxEditor::kSceneBar);
    const bool barOk = bar.getBounds() == juce::Rectangle<int> (0, H - BoxEditor::kSceneBar, W, BoxEditor::kSceneBar);
    // the old smallest window still gives the rack its old minimum
    ed->setSize (960, 720 + BoxEditor::kSceneBar); const bool minOk = panel != nullptr && panel->getHeight() == 720 - 62;
    ed->setSize (W, H);
    std::printf ("    window %dx%d (opens at the fitting size, bar %d) %d | rack panel between header and bar %d | bar below it %d | min size keeps the old rack minimum %d\n",
                 ed->getWidth(), ed->getHeight(), BoxEditor::kSceneBar, (int) sizeOk, (int) panelOk, (int) barOk, (int) minOk);
    check (sizeOk && panelOk && barOk && minOk, "GHOST SCENES bar placement: no existing control may move");
    // mouse: an empty slot stores, a filled one triggers (click), a long press holds and returns
    sceneSet (p, gpfx::smCut, 0);
    auto click = [&] (int i, int holdMs, bool right = false)
    {
        auto* s = bar.slotComponent (i); const auto c = s->getLocalBounds().getCentre().toFloat();
        const auto mods = right ? juce::ModifierKeys::rightButtonModifier : juce::ModifierKeys::leftButtonModifier;
        juce::MouseEvent down (juce::Desktop::getInstance().getMainMouseSource(), c, mods, 1.0f, 0, 0, 0, 0, s, s, juce::Time::getCurrentTime(), c, juce::Time::getCurrentTime(), 1, false);
        s->mouseDown (down);
        if (holdMs > 0) { run (p, noise (0.02)); pumpLoop (holdMs); run (p, noise (0.02)); pumpLoop (40); }
        juce::MouseEvent up (juce::Desktop::getInstance().getMainMouseSource(), c, juce::ModifierKeys(), 1.0f, 0, 0, 0, 0, s, s, juce::Time::getCurrentTime(), c, juce::Time::getCurrentTime(), 1, false);
        s->mouseUp (up);
        run (p, noise (0.02)); pumpLoop (60); run (p, noise (0.02)); pumpLoop (30);
    };
    p.scene ("tape"); p.pump (30);
    click (6, 0); const bool stored = p->sceneBank.slots[6].filled();
    click (1, 0); const bool clicked = p->sceneEngine().active.load() == 1 && sceneMismatch (p, 1) == 0;
    click (4, 450); const bool heldBack = p->sceneEngine().active.load() == 1 && sceneMismatch (p, 1) == 0;
    // menus: rename through the result handler, copy, clear, undo; the ... menu: LATCH, base note
    bar.slotMenuResult (4, 100 + 7); const bool copied = p->sceneBank.slots[7] .filled();
    bar.slotMenuResult (7, 3); const bool clr = ! p->sceneBank.slots[7].filled(); bar.slotMenuResult (7, 4); const bool und = p->sceneBank.slots[7].filled();
    bar.moreMenuResult (3); bar.moreMenuResult (1000 + 48); const bool latch = p->sceneBank.set.midiLatch && p->sceneBank.set.midiBase == 48;
    bar.moreMenuResult (2); bar.moreMenuResult (1000 + 36);
    bar.modeButton (gpfx::smSpill).triggerClick(); pumpLoop (20); const bool modeBtn = p->sceneBank.set.mode == gpfx::smSpill && bar.modeButton (gpfx::smSpill).getToggleState();
    std::printf ("    mouse: empty slot stores %d, click switches %d, long press holds + returns %d | menus: copy %d clear %d undo %d, LATCH + keys from C2 %d | mode buttons %d\n",
                 (int) stored, (int) clicked, (int) heldBack, (int) copied, (int) clr, (int) und, (int) latch, (int) modeBtn);
    check (stored && clicked && heldBack && copied && clr && und && latch && modeBtn, "GHOST SCENES bar mouse / menus");
    // contrast in all 4 looks: the bar rendered with the slot names blanked, every text colour against the pixels under it
    p->sceneBank.set.mode = gpfx::smMorph; p->sceneSettingsChanged();
    p->sceneTrigger (4); run (p, noise (0.02)); pumpLoop (60); run (p, noise (0.3)); pumpLoop (30);   // scene 5 active and morphing (2 bars)
    double worstAll = 99; juce::String worstWhat;
    for (int look : { 0, 1, 2, BoxEditor::kLookGhost })
    {
        ed->setLook (look); pumpLoop (40); bar.refresh();
        std::array<juce::String, boxscenes::kSlots> names; for (int i = 0; i < boxscenes::kSlots; ++i) names[(size_t) i] = p->sceneBank.slots[(size_t) i].name;
        const auto txt = bar.texts();
        for (auto& s : p->sceneBank.slots) s.name = " ";
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        for (int i = 0; i < boxscenes::kSlots; ++i) p->sceneBank.slots[(size_t) i].name = names[(size_t) i];
        for (auto& t : txt)
        {
            if (t.what == "label")
            {   // the label is painted by the bar: its colour against the bar surface just under the text
                const auto under = juce::Rectangle<int> (t.r.getX(), bar.getHeight() - 8, t.r.getWidth(), 2) + bar.getPosition();
                const double c = gWorstIn (img, under, t.c);
                if (c / 4.5 < worstAll) { worstAll = c / 4.5; worstWhat = lookName (look) + " label " + juce::String (c, 2) + ":1"; }
                continue;
            }
            const bool empty = ! p->sceneBank.slots[(size_t) (t.what.getTrailingIntValue() - 1)].filled();
            const double c = gWorstIn (img, t.r + bar.getPosition(), t.c), need = empty ? 3.0 : 4.5;
            if (c / need < worstAll) { worstAll = c / need; worstWhat = lookName (look) + " " + t.what + " " + juce::String (c, 2) + ":1 (need " + juce::String (need, 1) + ")"; }
        }
        if (shots != juce::File())
        {
            p->sceneBank.set.quant = 5; p->sceneSettingsChanged();
            p->sceneEngine().post (0, 1); run (p, noise (0.02));   // (scene 2 pending on the next 2-bar line: the blink shows in the shot)
            p->sceneBank.set.quant = 0; p->sceneSettingsChanged();
            snapTo (*ed, shots.getChildFile ("scenes-" + lookName (look).toLowerCase() + ".png"));
            run (p, noise (0.6)); pumpLoop (60); p->sceneTrigger (4); run (p, noise (0.02)); pumpLoop (60); run (p, noise (0.3));
        }
    }
    std::printf ("    contrast: worst slot text / need = %.2f (%s)\n", worstAll, worstWhat.toRawUTF8());
    check (worstAll >= 1.0, "GHOST SCENES bar text contrast in every look");
    edp.reset();
}
