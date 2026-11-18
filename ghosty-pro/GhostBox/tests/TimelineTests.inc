// GHOST MOTION timeline tests (GhostyPro/fx/FxBoxTimeline.h, BoxMod): lanes on the right song positions, AUDIO-AWARE rules on synthetic
// drum loops, exact loop restarts, click-free smoothing, state round trip, old sessions unchanged, no audio-thread allocation.
//   GhostBoxTests timeline        -> these checks
//   GhostBoxTests timelineui [dir] -> the MOTION page's TIMELINE view (contrast in 4 looks, tooltips, tile drop, screenshots)

// a synthetic drum loop with hits at known places: { beat, level (peak), kind 0 kick 1 snare 2 hat }
struct TlTestHit { double beat; float level; int kind; };
static Stereo tlDrumLoop (const std::vector<TlTestHit>& hits, double loopBeats, int loops, double bpm = 120.0)
{
    const double spb = 60.0 / bpm * SR;
    Stereo s; s.resize ((size_t) std::ceil (loopBeats * loops * spb));
    for (int L = 0; L < loops; ++L)
        for (auto& h : hits)
        {
            const size_t at = (size_t) std::llround ((h.beat + L * loopBeats) * spb);
            const size_t len = (size_t) (SR * (h.kind == 2 ? 0.05 : 0.18));
            uint32_t seed = 77u + (uint32_t) at;
            for (size_t i = 0; i < len && at + i < s.size(); ++i)
            {
                const double t = (double) i / SR; float x;
                if (h.kind == 0) x = (float) (std::sin (2.0 * juce::MathConstants<double>::pi * (50.0 * t + 80.0 * (1.0 - std::exp (-t * 30.0)) / 30.0)) * std::exp (-t * 14.0));
                else { seed = seed * 1664525u + 1013904223u; const float nz = (float) (seed >> 8) / 8388608.0f - 1.0f; x = (float) ((h.kind == 1 ? 0.6 * nz + 0.4 * std::sin (2.0 * juce::MathConstants<double>::pi * 190.0 * t) : nz) * std::exp (-t * (h.kind == 1 ? 18.0 : 70.0))); }
                const float fade = (float) std::min (1.0, (double) (len - i) / (SR * 0.005));   // a 5 ms release (no click at the end)
                s.l[at + i] += h.level * x * fade; s.r[at + i] += h.level * x * fade;
            }
        }
    return s;
}

static void testGhostMotionTimeline()
{
    std::printf ("\n[34] GHOST MOTION timeline: lane positions, AUDIO-AWARE rules, loop restart, smoothing, state, old sessions, allocations\n");
    // ---- the pure lane function: events with edges, wrap-around, steps
    {
        std::vector<TlEvent> ev { { 1.0, 0.5, 1.0f, false }, { 3.9, 0.2, -0.5f, false } };
        const double edge = 0.008;
        const float mid = tlEventsAt (ev.data(), 2, 1.25, 4.0, edge), before = tlEventsAt (ev.data(), 2, 0.999, 4.0, edge), after = tlEventsAt (ev.data(), 2, 1.5001, 4.0, edge);
        const float wrap = tlEventsAt (ev.data(), 2, 0.05, 4.0, edge), edgeHalf = tlEventsAt (ev.data(), 2, 1.004, 4.0, edge);
        std::array<float, kTlMaxSteps> st {}; st[0] = 1.0f; st[3] = -0.5f;
        const bool steps = tlStepsAt (st.data(), 8, 0.01) == 1.0f && tlStepsAt (st.data(), 8, 3.5 / 8.0) == -0.5f && tlStepsAt (st.data(), 8, 0.999) == 0.0f;
        std::printf ("    lane function: inside %.3f, before %.3f, after %.3f, wrapped event at the loop start %.3f, half way up the edge %.3f, steps %d\n", mid, before, after, wrap, edgeHalf, (int) steps);
        check (std::abs (mid - 1.0f) < 1e-6f && before == 0.0f && after == 0.0f && std::abs (wrap + 0.5f) < 1e-6f && std::abs (edgeHalf - 0.5f) < 0.02f && steps, "timeline lane function (events, edges, wrap, steps)");
    }
    // ---- lanes land on the right song positions (the shadow value the unit reads, sampled every 16 samples)
    {
        Proc p (false); p.set (pid (uEcho, "on"), 1); p.set (pid (uEcho, "mix"), 0.2f); p.set (pid (uWreck, "on"), 1); p.pump (60);
        auto& box = p.impl().box;
        TimelineDef d; d.bars = 1;
        TlLane L; L.id = pid (uEcho, "mix"); L.depth = 0.5f; L.shape = flatShape(); L.events.push_back ({ 1.0, 0.5, 1.0f, false }); L.events.push_back ({ 2.75, 0.25, -0.6f, false });
        d.lanes.push_back (L); box.tlLoad (d);
        const double spb = 60.0 / p.head.bpm * SR;
        juce::AudioBuffer<float> b (2, 16); juce::MidiBuffer m; b.clear();
        std::vector<std::pair<double, float>> trace;
        for (int i = 0; i < (int) (2.0 * 4.0 * spb / 16.0); ++i)
        {
            b.clear(); { AudioScope as; p->processBlock (b, m); }
            p.head.ppq += 16.0 / spb;
            trace.push_back ({ p.head.ppq, box.effective (pid (uEcho, "mix")) });
        }
        const int up = box.upLat[uEcho];
        const double expUp = (1.0 + 4.0) + up / spb, expDown = (2.75 + 4.0) + up / spb;   // the second loop: the smoother is settled
        double tUp = -1, tDown = -1; const float base = 0.2f;
        for (size_t i = 1; i < trace.size(); ++i)
        {
            if (trace[i].first > 4.5 && tUp < 0 && trace[i].second > base + 0.25f) tUp = trace[i].first;
            if (trace[i].first > 6.5 && tDown < 0 && trace[i].second < base - 0.15f) tDown = trace[i].first;
        }
        const double eUpMs = (tUp - expUp) * 60.0 / p.head.bpm * 1000.0, eDownMs = (tDown - expDown) * 60.0 / p.head.bpm * 1000.0;
        float plateau = 0; for (auto& [q, v] : trace) if (q > 5.1 && q < 5.4) plateau = std::max (plateau, v);
        std::printf ("    event lane on ECHO mix (WRECKER before it: %d samples of latency): up at %.4f beats (expected %.4f, %+.2f ms), down at %.4f (expected %.4f, %+.2f ms), plateau %.3f (base 0.2 + 0.5)\n",
                     up, tUp, expUp, eUpMs, tDown, expDown, eDownMs, plateau);
        check (tUp > 0 && eUpMs >= 0.0 && eUpMs < 8.0 && tDown > 0 && eDownMs >= 0.0 && eDownMs < 8.0 && std::abs (plateau - 0.7f) < 0.01f, "timeline events land on their song positions (latency-aligned)");
        // a CURVE lane: a ramp -1 .. +1 over the loop on FILTER cutoff (normalised), sampled half way
        TimelineDef d2; d2.bars = 2;
        TlLane C; C.id = pid (uFilter, "cutoff"); C.depth = 0.3f; C.shape = bendlane::Shape(); C.shape.snap = 0; C.shape.add (0.0f, -1.0f); C.shape.add (0.999f, 1.0f); C.shape.sanitise();
        d2.lanes.push_back (C); box.tlLoad (d2); p.set (pid (uFilter, "on"), 1);
        auto* prm = p->apvts.getParameter (pid (uFilter, "cutoff")); const float base01 = prm->getValue();
        float atQuarter = 0, atThreeQ = 0;
        p.head.ppq = 0;
        for (int i = 0; i < (int) (8.0 * spb / 16.0); ++i)
        {
            b.clear(); { AudioScope as; p->processBlock (b, m); }
            p.head.ppq += 16.0 / spb;
            const float v01 = prm->convertTo0to1 (box.effective (pid (uFilter, "cutoff"))) - base01;
            if (std::abs (p.head.ppq - 2.0) < 8.0 / spb) atQuarter = v01;
            if (std::abs (p.head.ppq - 6.0) < 8.0 / spb) atThreeQ = v01;
        }
        std::printf ("    curve lane (2-bar ramp, depth 30 %%): offset at 1/4 of the loop %.3f (expected -0.150), at 3/4 %.3f (expected +0.150)\n", atQuarter, atThreeQ);
        check (std::abs (atQuarter + 0.15f) < 0.01f && std::abs (atThreeQ - 0.15f) < 0.01f, "timeline curve lane follows the loop");
    }
    // ---- AUDIO-AWARE: onsets of synthetic drum loops, and the rules
    {
        const std::vector<TlTestHit> hits { { 0.0, 0.8f, 0 }, { 0.5, 0.1f, 2 }, { 1.0, 0.6f, 1 }, { 1.5, 0.1f, 2 }, { 2.0, 0.8f, 0 }, { 2.5, 0.1f, 2 }, { 2.75, 0.7f, 0 }, { 3.0, 0.6f, 1 }, { 3.5, 0.1f, 2 },
                                            { 4.0, 0.8f, 0 }, { 5.0, 0.6f, 1 }, { 5.5, 0.1f, 2 }, { 6.0, 0.8f, 0 }, { 7.0, 0.6f, 1 }, { 7.75, 0.12f, 2 } };
        Proc p (false); p.pump (40);
        auto& box = p.impl().box;
        TimelineDef d; d.bars = 2; d.lanes.push_back ({}); d.lanes[0].id = pid (uEcho, "mix"); d.lanes[0].shape = flatShape(); box.tlLoad (d);
        box.tlListen = true;
        const auto in = slice (tlDrumLoop (hits, 8.0, 3), 0, S (8.5));   // 2 loops + a bit: the analysis takes the last complete one
        juce::ignoreUnused (run (p, in, nullptr, 128));
        const auto a = box.tlDetectNow();
        juce::String found; int matched = 0; double worstMs = 0;
        for (auto& h : a.hits) found << juce::String (h.beat, 3) << "(" << juce::String (h.levelDb, 0) << ") ";
        for (auto& e : hits)
        {
            double best = 1e9; for (auto& h : a.hits) best = std::min (best, std::abs (h.beat - e.beat));
            const double ms = best * 500.0;
            if (ms < 3.0) { ++matched; worstMs = std::max (worstMs, ms); }
        }
        std::printf ("    detection (2-bar loop, 15 hits incl. quiet hats): ok %d, %d hits found, %d / 15 within 3 ms (worst %.2f ms)\n      %s\n", (int) a.ok, (int) a.hits.size(), matched, worstMs, found.toRawUTF8());
        check (a.ok && a.hits.size() == hits.size() && matched == (int) hits.size(), "AUDIO-AWARE: every hit of the synthetic loop found, none extra, within 3 ms");
        auto beatsOf = [&a] (const std::vector<int>& ix) { std::vector<double> v; for (int i : ix) v.push_back (std::round (a.hits[(size_t) i].beat * 4.0) / 4.0); return v; };
        auto same = [] (const std::vector<double>& x, std::vector<double> y) { return x == y; };
        const auto e2 = beatsOf (tlPickHits (a.hits, trEvery2nd, 4.0)), last = beatsOf (tlPickHits (a.hits, trLastOfBar, 4.0)), loud = beatsOf (tlPickHits (a.hits, trLoud, 4.0)), quiet = beatsOf (tlPickHits (a.hits, trQuiet, 4.0));
        const bool ok2 = same (e2, { 0.5, 1.5, 2.5, 3.0, 4.0, 5.5, 7.0 }), okLast = same (last, { 3.5, 7.75 }), okLoud = same (loud, { 0.0, 1.0, 2.0, 2.75, 3.0, 4.0, 5.0, 6.0, 7.0 }), okQuiet = same (quiet, { 0.5, 1.5, 2.5, 3.5, 5.5, 7.75 });
        std::printf ("    rules: EVERY 2ND HIT %d, LAST HIT OF BAR %d, LOUD HITS %d, QUIET HITS %d\n", (int) ok2, (int) okLast, (int) okLoud, (int) okQuiet);
        check (ok2 && okLast && okLoud && okQuiet, "AUDIO-AWARE rules pick the right hits");
        box.timeline.eventLen = 1; box.timeline.eventAmt = 0.8f;
        box.timeline.lanes[0].events.push_back ({ 1.25, 0.25, -1.0f, false });   // a manual event stays
        const int placed = box.tlPlaceOnHits (0, a, trLastOfBar);
        const int again = box.tlPlaceOnHits (0, a, trLastOfBar);                  // re-applying replaces the earlier auto events
        const auto& evs = box.timeline.lanes[0].events;
        bool manualKept = false; for (auto& e : evs) manualKept = manualKept || (! e.autoPlaced && std::abs (e.pos - 1.25) < 1e-9);
        std::printf ("    placed %d events (again: %d, lane holds %d incl. the manual one %d)\n", placed, again, (int) evs.size(), (int) manualKept);
        check (placed == 2 && again == 2 && evs.size() == 3 && manualKept, "events placed on the picked hits; manual events kept; no stacking");
        // a busier loop: the harness drum groove (hats on every 1/8, kicks, snares) over a pad: every 1/8 is a hit
        {
            Proc g (false); g.pump (20); auto& gb = g.impl().box; TimelineDef gd; gd.bars = 1; gb.tlLoad (gd); gb.tlListen = true;
            juce::ignoreUnused (run (g, mixOf (drums (4.5), pad (4.5), 0.6f), nullptr, 256));
            const auto ga = gb.tlDetectNow();
            int ok8 = 0, extra = 0;
            for (int k = 0; k < 8; ++k) { bool f = false; for (auto& h : ga.hits) f = f || std::abs (h.beat - 0.5 * k) * 500.0 < 3.0; ok8 += f ? 1 : 0; }
            // (the harness groove cuts every sound at its 1/16 step's end: the kick's cut-offs at 0.25 / 2.25 / 2.75, the snare's at
            // 1.25 / 3.25, are real clicks in that signal and may count as hits; anything else is extra)
            for (auto& h : ga.hits) { const double q = std::round (h.beat * 4.0) / 4.0; const bool onGrid = std::abs (h.beat - q) * 500.0 < 3.0;
                                      const bool allowed = std::fmod (q, 0.5) == 0.0 || q == 0.25 || q == 2.25 || q == 2.75 || q == 1.25 || q == 3.25; if (! onGrid || ! allowed) ++extra; }
            juce::String gs; for (auto& h : ga.hits) gs << juce::String (h.beat, 3) << " ";
            std::printf ("    drum groove over a pad: %d / 8 eighth notes found within 3 ms, %d extra\n      %s\n", ok8, extra, gs.toRawUTF8());
            check (ga.ok && ok8 == 8 && extra == 0, "AUDIO-AWARE detection on a groove over a pad");
        }
        // async (worker thread) gives the same
        bool got = false; TlAnalysis as2; box.tlDetectAsync ([&] (TlAnalysis r) { as2 = r; got = true; });
        for (int i = 0; i < 400 && ! got; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
        check (got && as2.hits.size() == a.hits.size(), "AUDIO-AWARE detection on the worker thread");
        // not enough audio yet
        Proc q (false); q.p->rack.impl->box.tlListen = true; juce::ignoreUnused (run (q, sine (0.5, 200.0)));
        const auto none = q.impl().box.tlDetectNow();
        std::printf ("    without one full loop: ok %d (\"%s\")\n", (int) none.ok, none.why.toRawUTF8());
        check (! none.ok && none.why.isNotEmpty(), "detection refuses without a full loop");
    }
    // ---- loop restart exact: every loop restart starts a chunk on its own sample (odd host blocks), the value snaps back
    {
        Proc p (false); p.set (pid (uEcho, "on"), 1); p.set (pid (uEcho, "mix"), 0.5f); p.pump (40);
        auto& box = p.impl().box;
        TimelineDef d; d.bars = 1; TlLane L; L.id = pid (uEcho, "mix"); L.depth = 0.4f; L.shape = bendlane::Shape(); L.shape.snap = 0; L.shape.add (0.0f, -1.0f); L.shape.add (0.9999f, 1.0f); L.shape.sanitise();
        d.lanes.push_back (L); box.tlLoad (d);
        const double spb = 60.0 / p.head.bpm * SR;
        juce::AudioBuffer<float> b (2, 37); juce::MidiBuffer m;
        std::vector<std::pair<double, float>> tr; int restarts = 0; double worstOff = 0; float lastPos = 0;
        for (int i = 0; i < (int) (3.2 * 4.0 * spb / 37.0); ++i)
        {
            b.clear(); { AudioScope as; p->processBlock (b, m); }
            p.head.ppq += 37.0 / spb;
            const float pos = box.tlPos.load();
            if (pos < lastPos - 0.5f) ++restarts;
            lastPos = pos;
            tr.push_back ({ p.head.ppq, box.effective (pid (uEcho, "mix")) });
        }
        juce::ignoreUnused (worstOff);
        // the chunk that starts on the restart: tlChunkLimit ends the previous chunk exactly there
        const double ppqBefore = 4.0 - 0.3 / spb;   // 0.3 samples before the boundary
        const int lim = [&] { box.beginBlock(); const int r = box.tlChunkLimit (ppqBefore - 20.0 / spb, spb, 4.0, 64); box.endBlock(); return r; }();
        float before = 0, after10 = 0;
        for (auto& [q, v] : tr) { if (q < 8.0 - 30.0 / spb && q > 8.0 - 70.0 / spb) before = v; if (q > 8.0 + 0.015 * 2.0 && after10 == 0.0f) after10 = v; }
        std::printf ("    restarts seen %d (3.2 loops), chunk limit before the restart %d samples (expected 21: the first sample at or after the restart), value just before the restart %.3f (0.5 + 0.4), 15 ms after %.3f (0.5 - 0.4)\n", restarts, lim, before, after10);
        check (restarts == 3 && lim == 21 && std::abs (before - 0.9f) < 0.01f && std::abs (after10 - 0.1f) < 0.02f, "timeline loop restart exact (chunk on the restart sample, snap back)");
    }
    // ---- smoothing click-free: a full-depth step event on WRECKER output level over a steady sine
    {
        Proc p (false); p.set (pid (uWreck, "on"), 1); p.set (pid (uWreck, "drive"), 0.05f); p.set (pid (uWreck, "auto"), 0.0f); p.pump (40);
        auto& box = p.impl().box;
        TimelineDef d; d.bars = 1; TlLane L; L.id = pid (uWreck, "out"); L.depth = -0.6f; L.shape = flatShape(); L.events.push_back ({ 1.0, 1.0, 1.0f, false }); L.events.push_back ({ 2.5, 0.125, 1.0f, false });
        d.lanes.push_back (L); box.tlLoad (d);
        const auto in = sine (6.0, 220.0, 0.3f);
        const long a0 = gAudioAllocs.load();
        const auto out = run (p, in, nullptr, 256);
        const long allocs = gAudioAllocs.load() - a0;
        double where = 0; const float cs = clickScore (slice (out, S (0.5), out.size()), &where);
        // the same jump as a hard host automation step (no timeline), for reference
        Proc h (false); h.set (pid (uWreck, "on"), 1); h.set (pid (uWreck, "drive"), 0.05f); h.set (pid (uWreck, "auto"), 0.0f); h.pump (40);
        auto* op = h->apvts.getParameter (pid (uWreck, "out")); const float o0 = op->getValue();
        Stereo hout; hout.resize (in.size());
        { auto a = run (h, slice (in, 0, S (2.5))); h.set (pid (uWreck, "out"), op->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, o0 - 0.6f))); auto b2 = run (h, slice (in, S (2.5), in.size())); hout = a; append (hout, b2); }
        const float hcs = clickScore (slice (hout, S (0.5), hout.size()));
        float depthDb = rmsDb (out, S (2.6), S (2.9)) - rmsDb (out, S (1.5), S (1.9));
        std::printf ("    step events at full speed: click score %.1f at %.2f s (a hard host step of the same jump: %.1f) | level drop %.1f dB | audio allocations %ld\n", cs, where + 0.5, hcs, depthDb, allocs);
        check (cs < 4.0f && depthDb < -6.0f && allocs == 0 && finite (out), "timeline smoothing click-free, no audio-thread allocation");
    }
    // ---- state round trip + old sessions
    {
        Proc p (false); p.set (pid (uEcho, "on"), 1); p.pump (40);
        auto& box = p.impl().box;
        TimelineDef d; d.bars = 3; d.rule = trQuiet; d.eventLen = 2; d.eventAmt = -0.4f;
        TlLane A; A.id = pid (uEcho, "mix"); A.depth = -0.7f; A.mode = 1; A.stepsIdx = 2; for (int i = 0; i < 32; ++i) A.steps[(size_t) i] = (i % 3) * 0.3f - 0.3f; A.shape = flatShape(); A.events.push_back ({ 0.5, 0.25, 0.9f, true });
        TlLane B; B.id = pid (uEcho, "fb"); B.on = false; B.shape = bendlane::Shape(); B.shape.snap = 0; B.shape.add (0.0f, 0.2f); B.shape.add (0.5f, -0.4f, 0.3f, true); B.shape.sanitise(); B.events.push_back ({ 9.5, 1.0, -0.2f, false });
        d.lanes = { A, B }; box.tlLoad (d);
        juce::MemoryBlock mb; p->getStateInformation (mb);
        Proc q (false); q->setStateInformation (mb.getData(), (int) mb.getSize()); q.pump (40);
        const auto x = box.tlCurrent(), y = q.impl().box.tlCurrent();
        bool same = x.bars == y.bars && x.rule == y.rule && x.eventLen == y.eventLen && std::abs (x.eventAmt - y.eventAmt) < 1e-6f && x.lanes.size() == y.lanes.size();
        for (size_t k = 0; same && k < x.lanes.size(); ++k)
        {
            const auto& a = x.lanes[k]; const auto& b = y.lanes[k];
            same = a.id == b.id && a.on == b.on && std::abs (a.depth - b.depth) < 1e-6f && a.mode == b.mode && a.stepsIdx == b.stepsIdx && a.shape.sameAs (b.shape) && a.events.size() == b.events.size();
            for (int i = 0; same && i < kTlMaxSteps; ++i) same = std::abs (a.steps[(size_t) i] - b.steps[(size_t) i]) < 1e-4f;
            for (size_t i = 0; same && i < a.events.size(); ++i) same = std::abs (a.events[i].pos - b.events[i].pos) < 1e-6 && std::abs (a.events[i].len - b.events[i].len) < 1e-6 && std::abs (a.events[i].amt - b.events[i].amt) < 1e-4f && a.events[i].autoPlaced == b.events[i].autoPlaced;
        }
        p.head.ppq = q.head.ppq = 0;
        const auto in = mixOf (drums (4.0), sine (4.0, 330.0, 0.2f));
        const auto o1 = run (p, in), o2 = run (q, in);
        const float md = maxDiff (o1, o2);
        // a default session saves no TIMELINE child; an older session (none) loads an empty timeline
        Proc r (false); const auto st = r->stateTree();
        bool hasChild = false; for (auto c : st.getChildWithName ("FXRACK").getChild (0).getChildWithName ("BOX3")) hasChild = hasChild || c.hasType ("TIMELINE");
        auto old = st.createCopy(); Proc s (false); s.impl().box.tlLoad (d); s->applyStateTree (old);
        std::printf ("    state round trip: timeline equal %d, renders equal (max diff %.2e) | default session saves no TIMELINE %d, an older session clears it %d\n", (int) same, md, (int) ! hasChild, (int) s.impl().box.timeline.lanes.empty());
        check (same && md == 0.0f && ! hasChild && s.impl().box.timeline.lanes.empty(), "timeline state round trip / old sessions unchanged");
        // undo brings an edit back
        p.impl().pushUndo(); box.tlRemoveLane (0); const bool removed = box.timeline.lanes.size() == 1; p.impl().undo();
        check (removed && box.timeline.lanes.size() == 2 && box.timeline.lanes[0].id == pid (uEcho, "mix"), "timeline edits are undoable");
        // discrete settings and unknown ids are refused; the lane count is bounded
        const bool refused = box.tlAddLane (pid (uEcho, "mode")) < 0 && box.tlAddLane ("fx_nothing") < 0 && box.tlAddLane ("box_mix") < 0;
        int added = 0; for (auto* id : { "fx_echo_width", "fx_echo_char", "fx_echo_duck", "fx_space_size", "fx_space_mix", "fx_space_decay", "fx_halls_mix", "fx_halls_size", "fx_wreck_drive", "fx_tube_drive", "fx_crush_crush", "fx_chorus_depth" }) if (box.tlAddLane (id) >= 0) ++added;
        std::printf ("    switches / unknown / box_ ids refused %d | lanes bounded: %d of 12 more added (max %d)\n", (int) refused, added, kTlLanes);
        check (refused && (int) box.timeline.lanes.size() == kTlLanes, "timeline lane rules (continuous fx_ settings only, at most 12)");
    }
}

// ---------------------------------------------------------------- the TIMELINE view inside the MOTION tab
static std::vector<juce::Component*> findAll (juce::Component& root, std::function<bool (juce::Component*)> pred)
{
    std::vector<juce::Component*> v; std::function<void (juce::Component&)> f = [&] (juce::Component& c) { for (auto* k : c.getChildren()) { if (pred (k)) v.push_back (k); f (*k); } }; f (root); return v;
}
static void testTimelineUi (const juce::File& shots)
{
    std::printf ("\n[34b] GHOST MOTION timeline view: inside MOTION, nothing else moved, tooltips, tile drop, contrast in 4 looks, screenshots\n");
    if (shots != juce::File()) shots.createDirectory();
    Proc p (true); p.scene ("galaxyecho"); p.set (pid (uWreck, "on"), 1); p.pump (60);
    auto& box = p.impl().box;
    TimelineDef d; d.bars = 2;
    TlLane A; A.id = pid (uEcho, "mix"); A.depth = 0.6f; A.shape = flatShape();
    TlLane B; B.id = pid (uFilter, "cutoff"); B.depth = -0.4f; B.shape = bendlane::Shape(); B.shape.snap = 0; B.shape.add (0.0f, 0.0f, 0.4f); B.shape.add (0.5f, 0.8f, -0.3f); B.shape.add (0.75f, 0.2f); B.shape.sanitise();
    TlLane C; C.id = pid (uWreck, "drive"); C.depth = 0.5f; C.mode = 1; C.stepsIdx = 1; for (int i = 0; i < 16; ++i) C.steps[(size_t) i] = (i % 4 == 0) ? 0.9f : (i % 2 ? -0.3f : 0.2f); C.shape = flatShape();
    d.lanes = { A, B, C }; box.tlLoad (d);
    box.tlListen = true;
    const std::vector<TlTestHit> hits { { 0.0, 0.8f, 0 }, { 0.5, 0.1f, 2 }, { 1.0, 0.6f, 1 }, { 1.5, 0.1f, 2 }, { 2.0, 0.8f, 0 }, { 2.75, 0.7f, 0 }, { 3.0, 0.6f, 1 }, { 3.5, 0.1f, 2 },
                                        { 4.0, 0.8f, 0 }, { 5.0, 0.6f, 1 }, { 5.5, 0.1f, 2 }, { 6.0, 0.8f, 0 }, { 7.0, 0.6f, 1 }, { 7.5, 0.12f, 2 } };
    juce::ignoreUnused (run (p, slice (tlDrumLoop (hits, 8.0, 3), 0, S (8.5))));
    box.lastHits = box.tlDetectNow();
    box.tlPlaceOnHits (0, box.lastHits, trLastOfBar); box.timeline.eventLen = 0; box.tlPlaceOnHits (2, box.lastHits, trQuiet);
    std::unique_ptr<juce::AudioProcessorEditor> edp (p->createEditor()); auto* ed = dynamic_cast<BoxEditor*> (edp.get());
    auto* panel = findPanel (*ed);
    if (auto* mt = buttonIn (*panel, "MOTION")) mt->triggerClick();
    pumpLoop (60);
    auto* tlB = buttonIn (*panel, "GHOST MOTION TIMELINE");
    auto boundsOf = [&] (const char* t) { auto* b = buttonIn (*panel, t); return b != nullptr ? b->getScreenBounds() : juce::Rectangle<int>(); };
    const auto mo0 = boundsOf ("MOTIONS..."), sv0 = boundsOf ("SAVE MOTION..."), cl0 = boundsOf ("CLEAR ALL"), rs0 = boundsOf ("RESET ON START"), vol0 = boundsOf ("VOLUME");
    if (tlB) tlB->triggerClick();
    pumpLoop (80);
    auto* view = findFirst<gpfx::TimelineUiApi> (*panel);
    auto* volB = buttonIn (*panel, "VOLUME");
    const bool stays = boundsOf ("MOTIONS...") == mo0 && boundsOf ("SAVE MOTION...") == sv0 && boundsOf ("CLEAR ALL") == cl0 && boundsOf ("RESET ON START") == rs0 && ! mo0.isEmpty();
    const bool swapped = view != nullptr && volB != nullptr && ! volB->isVisible();
    std::printf ("    TIMELINE switch in MOTION %d | the view replaces the lane editor %d | MOTIONS / SAVE / CLEAR / RESET ON START unmoved %d\n", (int) (tlB != nullptr), (int) swapped, (int) stays);
    check (tlB != nullptr && swapped && stays, "GHOST MOTION timeline lives inside MOTION and moves nothing else");
    // every control in the view has a tooltip
    int noTip = 0, controls = 0; juce::String missing;
    if (auto* vc = dynamic_cast<juce::Component*> (view))
        for (auto* c : findAll (*vc, [] (juce::Component* k) { return dynamic_cast<juce::Button*> (k) != nullptr || dynamic_cast<juce::ComboBox*> (k) != nullptr || dynamic_cast<juce::Slider*> (k) != nullptr; }))
        {
            ++controls;
            auto* tc = dynamic_cast<juce::SettableTooltipClient*> (c);
            if (tc == nullptr || tc->getTooltip().isEmpty()) { ++noTip; if (auto* b = dynamic_cast<juce::Button*> (c)) missing << b->getButtonText() << " "; else missing << "(" << c->getName() << ") "; }
        }
    std::printf ("    controls %d, without a tooltip %d %s\n", controls, noTip, missing.toRawUTF8());
    check (controls > 10 && noTip == 0, "every timeline control has a tooltip");
    // drag the CHORUS tile from the strip into the loop editor: a CHORUS mix lane with an event where it landed
    bool dropped = false; double dropBeat = -1;
    if (view != nullptr)
    {
        auto& edc = view->editorComponent();
        juce::Component* strip = panel->getComponentAt (juce::Point<int> (60, 60));
        const auto order = p.impl().getOrder(); int k = 0; for (int i = 0; i < kNumUnits; ++i) if (order[(size_t) i] == uChorus) k = i;
        if (strip != nullptr)
        {
            const int rows = strip->getWidth() < 1300 ? 2 : 1, perRow = (kNumUnits + rows - 1) / rows;
            const float w = (float) strip->getWidth() / (float) perRow, rh = ((float) strip->getHeight() - 14.0f) / (float) rows;
            const juce::Point<float> from ((float) (k % perRow) * w + w * 0.5f, 14.0f + (float) (k / perRow) * rh + rh * 0.5f);
            const auto target = edc.getLocalBounds().toFloat().getCentre() + juce::Point<float> (edc.getWidth() * 0.125f, 0);   // 5/8 of the loop
            const auto to = strip->getLocalPoint (&edc, target);
            const int before = (int) box.timeline.lanes.size();
            auto ev = [&] (juce::Point<float> pos, bool down) { auto src = juce::Desktop::getInstance().getMainMouseSource();
                return juce::MouseEvent (src, pos, down ? juce::ModifierKeys::leftButtonModifier : juce::ModifierKeys(), 1.0f, 0, 0, 0, 0, strip, strip, juce::Time::getCurrentTime(), from, juce::Time::getCurrentTime(), 1, false); };
            strip->mouseDown (ev (from, true)); strip->mouseDrag (ev (from + juce::Point<float> (20, 30), true)); strip->mouseDrag (ev (to, true)); strip->mouseUp (ev (to, false));
            pumpLoop (30);
            if ((int) box.timeline.lanes.size() == before + 1 && box.timeline.lanes.back().id == pid (uChorus, "mix") && ! box.timeline.lanes.back().events.empty())
            { dropped = true; dropBeat = box.timeline.lanes.back().events[0].pos; }
        }
    }
    std::printf ("    CHORUS tile dropped into the loop: lane + event %d at beat %.2f (expected about 5.0 of 8)\n", (int) dropped, dropBeat);
    check (dropped && std::abs (dropBeat - 5.0) < 0.3, "drag an effect tile onto the timeline");
    view = findFirst<gpfx::TimelineUiApi> (*panel);   // (a click on a tile would have left MOTION: the view must still be there)
    check (view != nullptr, "the timeline view survives a tile drag");
    if (view) view->select (2);
    double worst = 99; juce::String worstWhat;
    for (int look : { 0, 1, 2, BoxEditor::kLookGhost })
    {
        ed->setLook (look); pumpLoop (60);
        auto* vc = dynamic_cast<juce::Component*> (view); if (vc == nullptr) break;
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        for (auto& t : view->uiTexts())
        {
            const auto r = ed->getLocalArea (vc, t.r);
            const double c = gWorstIn (img, r.withHeight (2).translated (0, -7), t.c);
            if (c / 4.5 < worst) { worst = c / 4.5; worstWhat = lookName (look) + " " + t.what + " " + juce::String (c, 2); }
        }
        if (shots != juce::File()) snapTo (*ed, shots.getChildFile ("timeline-" + lookName (look).toLowerCase() + ".png"));
    }
    std::printf ("    contrast: worst text / need %.2f (%s)\n", worst, worstWhat.toRawUTF8());
    check (worst >= 1.0, "GHOST MOTION timeline text contrast in every look");
    // switching back restores the classic lanes exactly
    ed->setLook (0); pumpLoop (20);
    if (auto* b = buttonIn (*panel, "GHOST MOTION TIMELINE")) b->triggerClick();
    pumpLoop (40);
    check (findFirst<gpfx::TimelineUiApi> (*panel) == nullptr && boundsOf ("VOLUME") == vol0, "TIMELINE off: the 5 lanes are back where they were");
    edp.reset();
}
