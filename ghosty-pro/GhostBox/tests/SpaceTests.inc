// GHOST SPACE tests (GhostyPro/fx/FxGhostSpace.h SpaceStage, just before the limiter): the null (off = bit-identical), the distance
// laws (farther = quieter and darker), the ORBIT period, path timing on the grid, the BLACK HOLE tail (finite, bounded), 5,000 random
// positions / paths (no NaN), the mono fold-down, click-free moves, state round trip, no audio-thread allocation, CPU against the plain rack.
//   GhostBoxTests space          -> these checks
//   GhostBoxTests spaceui [dir]  -> the G-SPACE page (entry points, the field, tooltips, contrast in 4 looks, screenshots)

static float balanceDb (const Stereo& s, size_t a, size_t b)
{   // R minus L level (dB)
    double l = 1.0e-20, r = 1.0e-20; for (size_t i = a; i < b && i < s.size(); ++i) { l += (double) s.l[i] * s.l[i]; r += (double) s.r[i] * s.r[i]; }
    return (float) (10.0 * std::log10 (r / l));
}
static float monoFoldDb (const Stereo& s, size_t a, size_t b)
{   // the level of (L + R) / 2 against the stereo power sqrt ((L^2 + R^2) / 2): 0 dB = no loss in mono, -3 dB = a hard-panned sound
    double m = 1.0e-20, p = 1.0e-20;
    for (size_t i = a; i < b && i < s.size(); ++i) { const double x = 0.5 * ((double) s.l[i] + s.r[i]); m += x * x; p += 0.5 * ((double) s.l[i] * s.l[i] + (double) s.r[i] * s.r[i]); }
    return (float) (10.0 * std::log10 (m / p));
}
static void spaceSetup (Proc& p, float x, float y, int path = spStill)
{
    p.set ("fx_gspace_on", 1); p.set ("fx_gspace_x", x); p.set ("fx_gspace_y", y); p.set ("fx_gspace_path", (float) path); p.pump (20);
}
static Stereo fadedIn (Stereo s, double secs = 0.1) { const size_t n = S (secs); for (size_t i = 0; i < n && i < s.size(); ++i) { const float g = 0.5f - 0.5f * std::cos (3.14159265f * (float) i / (float) n); s.l[i] *= g; s.r[i] *= g; } return s; }
static Stereo monoNoise (double secs, float amp = 0.25f) { Stereo s; s.resize (S (secs)); for (size_t i = 0; i < s.size(); ++i) s.l[i] = s.r[i] = rnd() * amp; return s; }

static void testGhostSpace()
{
    std::printf ("\n[37] GHOST SPACE: null, distance laws, ORBIT period, grid timing, BLACK HOLE tail, fuzz, mono, clicks, state, allocations, CPU\n");
    // ---- null: never on = untouched; on, then off: once settled, bit-identical to a rack that never had it
    {
        const auto in = mixOf (drums (4.0), pad (4.0), 0.5f);
        Proc a (true), b (true); a.scene ("galaxyecho"); b.scene ("galaxyecho"); a.pump (40); b.pump (40);
        const auto oa0 = run (a, slice (in, 0, S (1.0)));
        spaceSetup (b, 0.8f, 0.2f, spOrbit);
        const auto ob0 = run (b, slice (in, 0, S (1.0)));
        b.set ("fx_gspace_on", 0); b.pump (20);
        const auto oa1 = run (a, slice (in, S (1.0), in.size())), ob1 = run (b, slice (in, S (1.0), in.size()));
        const float onDiff = maxDiff (oa0, ob0, S (0.2));
        float offDiff = 0; for (size_t i = S (0.1); i < oa1.size(); ++i) offDiff = std::max ({ offDiff, std::abs (oa1.l[i] - ob1.l[i]), std::abs (oa1.r[i] - ob1.r[i]) });
        Proc c (true); c.scene ("galaxyecho"); c.pump (40);
        Proc d (true); d.scene ("galaxyecho"); d.pump (40);
        const auto oc = run (c, in), od = run (d, in);
        std::printf ("    on changes the sound (max diff %.3f) | off again, after the 20 ms fade: max diff %.3g | two plain racks: %.3g\n", onDiff, offDiff, maxDiff (oc, od));
        check (onDiff > 0.01f && offDiff == 0.0f && maxDiff (oc, od) == 0.0f && ! b.impl().space.active(), "GHOST SPACE off = bit-identical to the plain rack");
    }
    // ---- distance laws: farther = quieter and darker (STILL, front axis and a diagonal), the laws themselves monotone
    {
        bool lawsOk = true;
        for (int k = 1; k <= 100; ++k)
        {
            const float r0 = (float) (k - 1) / 100.0f, r1 = (float) k / 100.0f;
            lawsOk = lawsOk && spaceDirectGain (r1) < spaceDirectGain (r0) && spaceAirHz (r1, 0.5f) < spaceAirHz (r0, 0.5f) && spacePreDelayMs (r1, 0.5f) < spacePreDelayMs (r0, 0.5f)
                         && spaceVerbGain (r1, 0.5f) / spaceDirectGain (r1) > spaceVerbGain (r0, 0.5f) / spaceDirectGain (r0);
        }
        const auto in = noise (2.0, 0.2f);
        juce::String line; bool mono = lawsOk; float lastDb = 99, lastHz = 1.0e9f;
        for (int dir = 0; dir < 2; ++dir)
        {
            lastDb = 99; lastHz = 1.0e9f;
            for (int k = 0; k <= 6; ++k)
            {
                const float r = (float) k / 6.0f * 0.98f;
                const float x = dir == 0 ? 0.5f : 0.5f + r * 0.5f * 0.7071f, y = dir == 0 ? 0.5f - r * 0.5f : 0.5f - r * 0.5f * 0.7071f;
                Proc p (false); spaceSetup (p, x, y);
                const auto out = run (p, in);
                const float db = rmsDb (out, S (0.8), S (2.0)); const double hz = centroidHz (spectrumDb (out, S (0.8), S (0.8) + 32768), 100.0, 16000.0);
                if (dir == 0) line << juce::String (r, 2) << ": " << juce::String (db, 1) << " dB " << juce::String ((int) hz) << " Hz  ";
                mono = mono && db < lastDb && hz < lastHz; lastDb = db; lastHz = (float) hz;
            }
        }
        std::printf ("    laws monotone %d | front axis distance: %s\n", (int) lawsOk, line.toRawUTF8());
        check (mono, "GHOST SPACE: farther = quieter and darker (front axis and diagonal), laws monotone");
    }
    // ---- ORBIT: the pan turns once per RATE (1 BAR at 120 BPM = 2.000 s), measured from the output's L / R balance
    {
        Proc p (false); p.set ("fx_gspace_rate", 1); p.set ("fx_gspace_depth", 0.9f); spaceSetup (p, 0.9f, 0.5f, spOrbit);
        const auto out = run (p, monoNoise (9.0, 0.2f));
        const double win = 0.01; std::vector<double> bal;
        for (size_t a = S (1.0); a + S (win) <= out.size(); a += S (win)) bal.push_back (balanceDb (out, a, a + S (win)));
        double mean = 0; for (double v : bal) mean += v; mean /= (double) bal.size(); for (auto& v : bal) v -= mean;
        int best = 0; double bestC = -1;
        for (int lag = 100; lag <= 300; ++lag) { double c = 0, e0 = 0, e1 = 0; for (size_t i = 0; i + (size_t) lag < bal.size(); ++i) { c += bal[i] * bal[i + (size_t) lag]; e0 += bal[i] * bal[i]; e1 += bal[i + (size_t) lag] * bal[i + (size_t) lag]; } c /= std::sqrt (e0 * e1) + 1.0e-12; if (c > bestC) { bestC = c; best = lag; } }
        // refine with a parabola through the peak
        auto corr = [&] (int lag) { double c = 0, e0 = 0, e1 = 0; for (size_t i = 0; i + (size_t) lag < bal.size(); ++i) { c += bal[i] * bal[i + (size_t) lag]; e0 += bal[i] * bal[i]; e1 += bal[i + (size_t) lag] * bal[i + (size_t) lag]; } return c / (std::sqrt (e0 * e1) + 1.0e-12); };
        const double cm = corr (best - 1), c0 = corr (best), cp = corr (best + 1), off = 0.5 * (cm - cp) / (cm - 2 * c0 + cp);
        const double period = (best + off) * win;
        float swing = 0; for (double v : bal) swing = std::max (swing, (float) std::abs (v));
        std::printf ("    ORBIT 1 BAR at 120 BPM: measured period %.3f s (2.000), balance swing +-%.1f dB, correlation %.2f\n", period, swing, bestC);
        check (std::abs (period - 2.0) < 0.02 && swing > 4.0f, "GHOST SPACE ORBIT period correct");
    }
    // ---- path timing on the grid (the pure movement the audio thread plays)
    {
        SpaceMove m; m.x = 0.8f; m.y = 0.3f; m.depth = 0.7f; m.periodBeats = 4.0;
        bool ok = true; float worst = 0;
        auto dist = [] (SpacePos a, SpacePos b) { return std::hypot (a.x - b.x, a.y - b.y); };
        for (int path : { spOrbit, spFall, spGhost, spHole })
        {
            m.path = path;
            for (double t : { 0.0, 1.0, 2.5, 3.75 }) { const float d = dist (spacePositionAt (m, t), spacePositionAt (m, t + 4.0 * 7)); worst = std::max (worst, d); }
        }
        m.path = spOrbit;   // a quarter turn per beat around the listener, starting at the dot's angle (radius = DEPTH / 2)
        const auto o0 = spacePositionAt (m, 0.0), o2 = spacePositionAt (m, 2.0);
        const float a0 = std::atan2 (0.3f - 0.5f, 0.8f - 0.5f);
        ok = ok && std::abs (o0.x - (0.5f + 0.35f * std::cos (a0))) < 1.0e-5f && std::abs (o0.y - (0.5f + 0.35f * std::sin (a0))) < 1.0e-5f;
        ok = ok && std::abs (o2.x - (0.5f - 0.35f * std::cos (a0))) < 1.0e-5f && std::abs (o2.y - (0.5f - 0.35f * std::sin (a0))) < 1.0e-5f;
        m.path = spFall;   // farthest exactly half way, home at the bar line
        float far = 0; double farAt = 0; for (int k = 0; k < 64; ++k) { const auto q = spacePositionAt (m, k / 16.0); if (q.fall > far) { far = q.fall; farAt = k / 16.0; } }
        ok = ok && farAt == 2.0 && spacePositionAt (m, 0.0).fall == 0.0f && dist (spacePositionAt (m, 4.0), SpacePos { 0.8f, 0.3f }) < 1.0e-6f;
        m.path = spHole;   // frozen from exactly half way to 0.85
        ok = ok && ! spacePositionAt (m, 1.999).freeze && spacePositionAt (m, 2.0).freeze && spacePositionAt (m, 3.39).freeze && ! spacePositionAt (m, 3.4).freeze;
        m.path = spGhost; m.trailBeats = 1.0;   // the copy is where the source was one beat before
        const auto g = spacePositionAt (m, 3.0), s2 = spacePositionAt (m, 2.0);
        ok = ok && std::abs (g.gx - s2.x) < 1.0e-5f && std::abs (g.gy - s2.y) < 1.0e-5f;
        // DRAWN: the path the board's DRAWN ORBIT model plays, 2 bars, host-synced
        ghosty::spirit::OrbitPath dp; dp.bars = 2; for (int k = 0; k < 8; ++k) dp.pts.push_back ({ 0.5f + 0.3f * std::cos (k * 0.785f), 0.5f + 0.3f * std::sin (k * 0.785f), k / 8.0f }); dp.normalise();
        SpacePathTables tb; tb.publish (dp); const int tk = tb.acquire();
        m.path = spDrawn; m.drawn = tb.t[(size_t) tk].data(); m.drawnN = SpacePathTables::kN; m.drawnBeats = tb.beats[(size_t) tk];
        float dw = 0; for (int k = 0; k < 8; ++k) { const auto q = spacePositionAt (m, 8.0 * k / 8.0 + 16.0); const auto w = dp.at (k / 8.0); dw = std::max (dw, std::hypot (q.x - w.x, q.y - w.y)); }
        std::printf ("    one period later every path is where it was (worst %.2g) | ORBIT start / half turn exact %d | FALL farthest at beat %.3f | DRAWN on its points (worst %.2g)\n",
                     worst, (int) ok, farAt, dw);
        check (ok && worst < 1.0e-5f && dw < 0.002f && m.drawnBeats == 8.0, "GHOST SPACE path timing exact on the grid");
        // audio: FALL's quietest moment lands half way through the bar (within a 1/32 note + the 8 ms smoothing)
        Proc p (false); p.set ("fx_gspace_rate", 1); p.set ("fx_gspace_depth", 1.0f); p.set ("fx_gspace_verb", 0.0f); spaceSetup (p, 0.5f, 0.45f, spFall);
        const auto out = run (p, monoNoise (6.5, 0.2f));
        const auto env = envelope (out, 0.005, S (2.0), S (4.0));   // bar 2 (2 .. 4 s)
        // the dip's centre: half way between where the level falls through the middle (between loudest and quietest) and rises back
        float mx = -999, mn = 999; for (float v : env) { mx = std::max (mx, v); mn = std::min (mn, v); }
        const float mid = 0.5f * (mx + mn); int down = -1, up = -1;
        for (int k = 1; k < (int) env.size(); ++k) { if (down < 0 && env[(size_t) k - 1] >= mid && env[(size_t) k] < mid) down = k; if (down >= 0 && env[(size_t) k - 1] < mid && env[(size_t) k] >= mid) up = k; }
        const double lat = p->getLatencySamples() / SR;   // (FIXED LATENCY pads the output: the host shifts it back)
        const double at = 2.0 + 0.5 * (down + up) * 0.005 - lat;   // seconds; expected 3.0 (half way through bar 2 at 120 BPM)
        std::printf ("    FALL (1 BAR): the dip is centred at %.3f s (3.000 expected), %.1f dB deep\n", at, mx - mn);
        check (std::abs (at - 3.0) < 0.0625 + 0.02, "GHOST SPACE FALL lands on the grid");
    }
    // ---- BLACK HOLE: bounded while frozen, the tail ends (silence after the input stops)
    {
        Proc p (false); p.set ("fx_gspace_rate", 1); spaceSetup (p, 0.7f, 0.3f, spHole);
        Stereo in = pad (1.5); in = withTail (in, 24.0);
        const auto out = run (p, in);
        const float pin = peak (in), pout = peak (out);
        float cyc[12] {}; for (int k = 0; k < 12; ++k) cyc[k] = peak (out, S (2.0 + 2.0 * k), S (4.0 + 2.0 * k));
        bool falling = true; for (int k = 1; k < 12; ++k) falling = falling && cyc[k] <= cyc[k - 1] * 1.02f + 1.0e-7f;
        const float endDb = rmsDb (out, S (23.0), S (25.5));
        std::printf ("    BLACK HOLE: input peak %.3f, output peak %.3f, cycle peaks after the input %.3g %.3g %.3g %.3g ... (never rising %d), last 2.5 s %.1f dBFS\n",
                     pin, pout, cyc[0], cyc[1], cyc[2], cyc[3], (int) falling, endDb);
        check (finite (out) && pout < 2.0f * pin && falling && endDb < -90.0f, "GHOST SPACE BLACK HOLE tail finite and bounded");
    }
    // ---- fuzz: 5,000 random positions / paths / settings (one per 64-sample block), extreme corners, drawn paths published meanwhile
    {
        Proc p (false); p.set ("fx_gspace_on", 1); p.pump (20);
        uint32_t seed = 77; auto rr = [&seed] { seed = seed * 1664525u + 1013904223u; return (float) (seed >> 8) / 16777216.0f; };
        const auto in = mixOf (drums (8.0), noise (8.0, 0.1f));
        Stereo out; out.resize (S (6.7)); juce::AudioBuffer<float> buf (2, 64); juce::MidiBuffer midi; bool fin = true; float pk = 0;
        const long a0 = gAudioAllocs.load();
        for (int k = 0; k < 5000; ++k)
        {
            auto pick = [&] (float v) { const float e = rr(); return e < 0.1f ? 0.0f : e < 0.2f ? 1.0f : v; };
            for (auto* id : { "fx_gspace_x", "fx_gspace_y" }) if (auto* q = p->apvts.getParameter (id)) q->setValueNotifyingHost (pick (rr()));
            if (k % 7 == 0) if (auto* q = p->apvts.getParameter ("fx_gspace_path")) q->setValueNotifyingHost (rr());
            if (k % 11 == 0) for (auto* id : { "fx_gspace_rate", "fx_gspace_depth", "fx_gspace_room", "fx_gspace_air", "fx_gspace_verb", "fx_gspace_ghost", "fx_gspace_trail" }) if (auto* q = p->apvts.getParameter (id)) q->setValueNotifyingHost (pick (rr()));
            if (k % 400 == 0) { ghosty::spirit::OrbitPath dp; dp.bars = 1 << (k / 400 % 4); for (int j = 0; j < 40; ++j) dp.pts.push_back ({ rr(), rr(), j / 40.0f }); dp.normalise(); p.impl().setSpacePath (dp); }
            if (k % 1000 == 999) p.head.ppq = rr() * 1000.0;   // transport jumps
            const size_t pos = (size_t) (k % 5000) * 64 % (in.size() - 64);
            std::copy (in.l.begin() + (long) pos, in.l.begin() + (long) pos + 64, buf.getWritePointer (0)); std::copy (in.r.begin() + (long) pos, in.r.begin() + (long) pos + 64, buf.getWritePointer (1));
            { AudioScope as; p->processBlock (buf, midi); }
            p.head.ppq += 64.0 / (60.0 / p.head.bpm * SR);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 64; ++i) { const float v = buf.getSample (c, i); fin = fin && std::isfinite (v); pk = std::max (pk, std::abs (v)); }
        }
        const long allocs = gAudioAllocs.load() - a0;
        std::printf ("    5,000 random positions / paths / settings + 13 drawn paths + 5 transport jumps: finite %d, peak %.2f, audio-thread allocations %ld\n", (int) fin, pk, allocs);
        check (fin && pk < 8.0f && allocs == 0, "GHOST SPACE fuzz: no NaN, bounded, no allocation");
    }
    // ---- mono fold-down: (L + R) / 2 never loses more than 3 dB against the stereo power (every path, 40 places, a mono and a stereo source)
    {
        float worst = 99; juce::String where;
        uint32_t seed = 5; auto rr = [&seed] { seed = seed * 1664525u + 1013904223u; return (float) (seed >> 8) / 16777216.0f; };
        const auto srcs = { monoNoise (2.0, 0.2f), pad (2.0) };
        int si = 0;
        for (auto& src : srcs)
        {
            for (int path = 0; path < 5; ++path)
                for (int k = 0; k < 4; ++k)
                {
                    Proc p (false); const float x = rr(), y = rr(); spaceSetup (p, x, y, path);
                    const auto out = run (p, src);
                    const float m = monoFoldDb (out, S (0.5), S (2.0)) - monoFoldDb (src, S (0.5), S (2.0));   // (against how the dry source itself folds)
                    if (m < worst) { worst = m; where = juce::String (si == 0 ? "mono noise" : "pad") + " " + spacePathNames()[path] + " at " + juce::String (x, 2) + ", " + juce::String (y, 2); }
                }
            ++si;
        }
        // the dry source as a reference (the stereo pad itself)
        const float ref = monoFoldDb (pad (2.0), S (0.5), S (2.0));
        std::printf ("    mono fold-down loss against the dry source's own: worst %.2f dB (%s); (the dry pad itself folds at %.2f dB, mono noise at 0)\n", worst, where.toRawUTF8(), ref);
        check (worst > -3.0f, "GHOST SPACE mono-compatible (mono fold-down within 3 dB)");
    }
    // ---- click-free: the dot jumps across the field, the path changes, ON switches, under a steady sine. With the reverb and the
    //      reflections off the score is absolute (< 4); with them on, a pure sine beats through the reflections by itself, so the live
    //      score is held against the worst static score of the same states
    for (const float vb : { 0.0f, 0.5f })
    {
        const auto in = fadedIn (sine (6.0, 220.0, 0.3f));   // (a hard start would echo through the reflections as clicks of its own)
        Proc p (false); p.set ("fx_gspace_verb", vb); spaceSetup (p, 0.3f, 0.3f);
        const auto out = runLive (p, in, { { 1.0, [&] { p.set ("fx_gspace_x", 0.95f); p.set ("fx_gspace_y", 0.9f); } }, { 2.0, [&] { p.set ("fx_gspace_path", (float) spOrbit); } },
                                          { 2.7, [&] { p.set ("fx_gspace_path", (float) spHole); } }, { 3.5, [&] { p.set ("fx_gspace_path", (float) spGhost); } },
                                          { 4.3, [&] { p.set ("fx_gspace_on", 0); } }, { 5.0, [&] { p.set ("fx_gspace_on", 1); } } });
        double where = 0; const float cs = clickScore (slice (out, S (0.3), out.size()), &where);
        float base = 0;
        if (vb > 0.0f)
            for (auto st : { std::make_tuple (0.3f, 0.3f, (int) spStill), std::make_tuple (0.95f, 0.9f, (int) spStill), std::make_tuple (0.95f, 0.9f, (int) spOrbit),
                             std::make_tuple (0.95f, 0.9f, (int) spHole), std::make_tuple (0.95f, 0.9f, (int) spGhost) })
            {
                Proc h (false); h.set ("fx_gspace_verb", vb); spaceSetup (h, std::get<0> (st), std::get<1> (st), std::get<2> (st));
                base = std::max (base, clickScore (slice (run (h, in), S (0.3), in.size())));
            }
        const float lim = std::max (4.0f, 1.25f * base);
        std::printf ("    jumps / path changes / on-off live (REVERB %d %%): click score %.1f at %.2f s (limit %.1f%s)\n", (int) (vb * 100), cs, where + 0.3, lim,
                     vb > 0.0f ? (", the same states held still: " + juce::String (base, 1)).toRawUTF8() : "");
        check (cs < lim && finite (out), "GHOST SPACE moves are click-free");
    }
    // ---- state round trip (settings + the drawn path) and the same sound after loading
    {
        Proc a (false); a.scene ("galaxyecho"); spaceSetup (a, 0.2f, 0.7f, spDrawn); a.set ("fx_gspace_room", 0.8f); a.set ("fx_gspace_ghost", 7.0f);
        ghosty::spirit::OrbitPath dp; dp.bars = 4; for (int k = 0; k < 16; ++k) dp.pts.push_back ({ 0.5f + 0.4f * std::sin (k * 0.4f), 0.5f + 0.3f * std::cos (k * 0.9f), k / 16.0f }); dp.normalise();
        a.impl().setSpacePath (dp);
        juce::MemoryBlock mb; a->getStateInformation (mb);
        Proc b (false); b->setStateInformation (mb.getData(), (int) mb.getSize()); b.pump (40);
        const bool same = b.impl().space.drawnPath.pts.size() == dp.pts.size() && b.impl().space.drawnPath.bars == 4 && std::abs (b.get ("fx_gspace_room") - 0.8f) < 1.0e-4f && b.get ("fx_gspace_path") == (float) spDrawn;
        a.head.ppq = b.head.ppq = 0; a->reset(); b->reset(); a->rack.reset(); b->rack.reset();
        const auto in = pad (3.0);
        const auto oa = run (a, in), ob = run (b, in);
        // and a session without it loads with no path
        Proc c (false); c.impl().setSpacePath (dp); { Proc fresh (false); juce::MemoryBlock m2; fresh->getStateInformation (m2); c->setStateInformation (m2.getData(), (int) m2.getSize()); }
        std::printf ("    state round trip: settings + drawn path %d, rendered the same (max diff %.3g), an older session clears the path %d\n", (int) same, maxDiff (oa, ob, S (0.5)), (int) c.impl().space.drawnPath.empty());
        check (same && maxDiff (oa, ob, S (0.5)) < 1.0e-4f && c.impl().space.drawnPath.empty(), "GHOST SPACE state round trip");
    }
    // ---- CPU: the plain rack against the rack + GHOST SPACE (each path), 10 s of pad + drums
    {
        const auto in = mixOf (pad (10.0), drums (10.0), 0.5f);
        auto cost = [&] (int path) {
            Proc p (true); p.scene ("galaxyecho"); if (path >= 0) spaceSetup (p, 0.75f, 0.3f, path); p.pump (20);
            juce::ignoreUnused (run (p, slice (in, 0, S (1.0))));
            const double r0 = renderSecs; juce::ignoreUnused (run (p, in)); return (renderSecs - r0) / 10.0 * 100.0; };
        const double base = cost (-1);
        juce::String line; double worst = 0;
        for (int path = 0; path < 6; ++path) { const double c = cost (path); worst = std::max (worst, c - base); line << spacePathNames()[path] << " +" << juce::String (c - base, 2) << "  "; }
        std::printf ("    CPU (one core, %% of real time): plain rack (galaxyecho) %.2f %% | with GHOST SPACE: %s\n", base, line.toRawUTF8());
        check (worst < 6.0, "GHOST SPACE is cheap (< 6 % of one core over the plain rack)");
    }
}

// ---------------------------------------------------------------- the PRINT and G-SPACE pages (shared checks: entry points, nothing moved,
// tooltips, contrast in 4 looks, screenshots)
template <typename PageT>
static PageT* openHostPage (BoxEditor& ed, const juce::String& name, int menuItem, bool& scOk, int& moved, bool& wideTab, bool& viaMenu, juce::String& scInfo)
{
    auto* panel = findPanel (ed);
    std::map<juce::String, juce::Rectangle<int>> before;
    for (auto* c : findAll (ed, [] (juce::Component* k) { return dynamic_cast<juce::Button*> (k) != nullptr && k->isVisible(); }))
        before[static_cast<juce::Button*> (c)->getButtonText() + "@" + juce::String (c->getScreenX())] = c->getScreenBounds();
    juce::TextButton* sc = nullptr; int visibleShortcuts = 0;
    for (auto* c : findAll (*panel, [] (juce::Component* k) { auto* b = dynamic_cast<juce::TextButton*> (k); return b != nullptr && b->getProperties().contains ("extraPage"); }))
    { auto* b = static_cast<juce::TextButton*> (c); if (b->getButtonText() == name) sc = b; if (b->isVisible() && b->getHeight() >= 16) ++visibleShortcuts; }
    scOk = sc != nullptr && sc->isVisible() && visibleShortcuts == 4;
    scInfo = sc != nullptr ? sc->getBounds().toString() + ", visible shortcuts " + juce::String (visibleShortcuts) : juce::String ("missing");
    if (sc) sc->triggerClick();
    pumpLoop (60);
    auto* page = findFirst<PageT> (ed);
    moved = 0;
    for (auto* c : findAll (ed, [] (juce::Component* k) { return dynamic_cast<juce::Button*> (k) != nullptr && k->isVisible(); }))
    {
        const auto key = static_cast<juce::Button*> (c)->getButtonText() + "@" + juce::String (c->getScreenX());
        if (auto it = before.find (key); it != before.end() && it->second != c->getScreenBounds()) ++moved;
    }
    ed.setSize (1900, 1200 + BoxEditor::kSceneBar); pumpLoop (30);
    auto* tab = findFirst<juce::TextButton> (*panel, [&] (juce::TextButton& b) { return b.getButtonText() == name && ! b.getProperties().contains ("extraPage"); });
    wideTab = tab != nullptr && tab->isVisible();
    ed.setSize (960, 720 + BoxEditor::kSceneBar); pumpLoop (20);
    int small = 0; for (auto* c : findAll (*panel, [] (juce::Component* k) { auto* b = dynamic_cast<juce::TextButton*> (k); return b != nullptr && b->getProperties().contains ("extraPage") && b->isVisible(); })) { juce::ignoreUnused (c); ++small; }
    scOk = scOk && small == 4;
    ed.sceneBar().moreMenuResult (7); pumpLoop (30);
    ed.sceneBar().moreMenuResult (menuItem); pumpLoop (40);
    viaMenu = findFirst<PageT> (ed) != nullptr;
    ed.setSize (1180, 780 + BoxEditor::kSceneBar); pumpLoop (30);
    juce::ignoreUnused (page);
    return findFirst<PageT> (ed);
}
template <typename PageT>
static void pageTipsAndLooks (BoxEditor& ed, PageT* page, const juce::File& shots, const juce::String& shotName, const juce::String& what, int minControls)
{
    int noTip = 0, controls = 0; juce::String missing;
    if (page)
        for (auto* c : findAll (*page, [] (juce::Component* k) { return (dynamic_cast<juce::Button*> (k) != nullptr || dynamic_cast<juce::ComboBox*> (k) != nullptr || dynamic_cast<juce::Slider*> (k) != nullptr) && k->isVisible(); }))
        {
            ++controls; auto* tc = dynamic_cast<juce::SettableTooltipClient*> (c);
            if (tc == nullptr || tc->getTooltip().isEmpty()) { ++noTip; if (auto* b = dynamic_cast<juce::Button*> (c)) missing << b->getButtonText() << " "; else missing << "(" << c->getName() << ") "; }
        }
    std::printf ("    controls %d, without a tooltip %d %s\n", controls, noTip, missing.toRawUTF8());
    check (controls >= minControls && noTip == 0, "every " + what + " control has a tooltip");
    double worst = 99; juce::String worstWhat;
    for (int look : { 0, 1, 2, BoxEditor::kLookGhost })
    {
        ed.setLook (look); pumpLoop (60);
        if (page == nullptr) break;
        auto img = ed.createComponentSnapshot (ed.getLocalBounds(), true, 1.0f);
        for (auto& t : page->texts())
        {
            const auto r = ed.getLocalArea (page, t.r);
            const double c = gWorstIn (img, r.withHeight (2).translated (0, -7), t.c);
            if (c / 4.5 < worst) { worst = c / 4.5; worstWhat = lookName (look) + " " + t.what + " " + juce::String (c, 2); }
        }
        if (shots != juce::File()) snapTo (ed, shots.getChildFile (shotName + "-" + lookName (look).toLowerCase() + ".png"));
    }
    std::printf ("    contrast: worst text / need %.2f (%s)\n", worst, worstWhat.toRawUTF8());
    check (worst >= 1.0, what + " page text contrast in every look");
    ed.setLook (0); pumpLoop (30);
}

static void testSpaceUi (const juce::File& shots)
{
    std::printf ("\n[37b] GHOST SPACE page: strip shortcut + tab + menu, the field (drag, draw), tooltips, nothing moved, contrast in 4 looks, screenshots\n");
    if (shots != juce::File()) shots.createDirectory();
    Proc p (true); p.scene ("galaxyecho"); p.set ("fx_gspace_path", (float) spGhost); p.set ("fx_gspace_on", 1); p.set ("fx_gspace_x", 0.78f); p.set ("fx_gspace_y", 0.3f); p.pump (40);
    std::unique_ptr<juce::AudioProcessorEditor> edp (p->createEditor()); auto* ed = dynamic_cast<BoxEditor*> (edp.get());
    ed->setSize (1180, 780 + BoxEditor::kSceneBar); pumpLoop (40);
    bool scOk = false, wideTab = false, viaMenu = false; int moved = 0; juce::String scInfo;
    auto* page = openHostPage<BoxSpacePage> (*ed, "G-SPACE", 11, scOk, moved, wideTab, viaMenu, scInfo);
    std::printf ("    strip shortcut %d (%s) | buttons that moved %d | 1900 px tab %d | 960 px: shortcuts + the ... menu %d\n", (int) scOk, scInfo.toRawUTF8(), moved, (int) wideTab, (int) viaMenu);
    check (scOk && page != nullptr && moved == 0 && wideTab && viaMenu, "GHOST SPACE page reachable (shortcut, tab, menu), nothing moved");
    bool dragOk = false, drawOk = false;
    if (page)
    {   // drag the dot to the right front; draw a loop (DRAW on)
        auto& f = page->fieldView();
        const auto r = f.getLocalBounds().toFloat(); const float s = std::min (r.getWidth(), r.getHeight()) - 8.0f; const auto c = r.getCentre();
        auto at = [&] (float x, float y) { return juce::Point<float> (c.x - s * 0.5f + x * s, c.y - s * 0.5f + y * s); };
        f.mouseDown (mouseAt (f, at (0.78f, 0.3f), true)); f.mouseDrag (mouseAt (f, at (0.2f, 0.25f), true)); f.mouseUp (mouseAt (f, at (0.2f, 0.25f), false));
        dragOk = std::abs (p.get ("fx_gspace_x") - 0.2f) < 0.02f && std::abs (p.get ("fx_gspace_y") - 0.25f) < 0.02f;
        std::vector<juce::Point<float>> pts; std::vector<double> secs;
        for (int k = 0; k < 40; ++k) { pts.push_back ({ 0.5f + 0.35f * std::cos (k * 0.157f), 0.5f + 0.25f * std::sin (k * 0.157f) }); secs.push_back (k * 0.02); }
        page->drawStroke (pts, secs); pumpLoop (20);
        drawOk = ! p.impl().space.drawnPath.empty() && juce::roundToInt (p.get ("fx_gspace_path")) == spDrawn && p.impl().space.drawnPath.bars == 2;
        p.set ("fx_gspace_path", (float) spGhost); p.set ("fx_gspace_x", 0.78f); p.set ("fx_gspace_y", 0.3f); p.pump (20);
    }
    std::printf ("    field: drag moves the dot %d | a drawn loop becomes PATH DRAWN (2 bars) %d\n", (int) dragOk, (int) drawOk);
    check (dragOk && drawOk, "GHOST SPACE field drag + draw");
    // audio running so the live dot and the ghost copy show in the shots
    LiveAudio la (p); pumpLoop (400);
    pageTipsAndLooks (*ed, page, shots, "space", "GHOST SPACE", 10);
    edp.reset();
}

static void testPrintUi (const juce::File& shots)
{
    std::printf ("\n[38b] GHOSTPRINT page: strip shortcut + tab + menu, drop a file, APPLY, tooltips, nothing moved, contrast in 4 looks, screenshots\n");
    if (shots != juce::File()) shots.createDirectory();
    Proc p (true); p.scene ("galaxyecho"); p.pump (40);
    std::unique_ptr<juce::AudioProcessorEditor> edp (p->createEditor()); auto* ed = dynamic_cast<BoxEditor*> (edp.get());
    ed->setSize (1180, 780 + BoxEditor::kSceneBar); pumpLoop (40);
    bool scOk = false, wideTab = false, viaMenu = false; int moved = 0; juce::String scInfo;
    auto* page = openHostPage<BoxPrintPage> (*ed, "PRINT", 10, scOk, moved, wideTab, viaMenu, scInfo);
    std::printf ("    strip shortcut %d (%s) | buttons that moved %d | 1900 px tab %d | 960 px: shortcuts + the ... menu %d\n", (int) scOk, scInfo.toRawUTF8(), moved, (int) wideTab, (int) viaMenu);
    check (scOk && page != nullptr && moved == 0 && wideTab && viaMenu, "GHOSTPRINT page reachable (shortcut, tab, menu), nothing moved");
    if (shots != juce::File() && page) { ed->setLook (0); pumpLoop (40); snapTo (*ed, shots.getChildFile ("print-empty-black.png")); }
    bool dropOk = false, applyOk = false, badOk = false;
    if (page)
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("gbprintui"); dir.deleteRecursively(); dir.createDirectory();
        dir.getChildFile ("broken.wav").replaceWithText ("not audio");
        page->filesDropped ({ dir.getChildFile ("broken.wav").getFullPathName() }, 10, 10); p->printWait(); pumpLoop (30);
        badOk = ! p->printResult.ok && p->printMessage.contains ("Could not read");
        writeWav (dir.getChildFile ("gate.wav"), mixOf (gateRef (120.0, 4), sweepRef (120.0, 4, nullptr), 0.5f));
        page->filesDropped ({ dir.getChildFile ("gate.wav").getFullPathName() }, 10, 10); p->printWait(); pumpLoop (250);
        dropOk = p->printResult.ok && p->printResult.loopBars == 2;
        page->applyButton().triggerClick(); pumpLoop (30);
        applyOk = p.impl().box.timeline.lanes.size() >= 2;
        dir.deleteRecursively();
    }
    std::printf ("    a broken file gives a message %d | a dropped WAV is read (2-bar loop) %d | APPLY writes lanes %d (%d)\n", (int) badOk, (int) dropOk, (int) applyOk, (int) p.impl().box.timeline.lanes.size());
    check (badOk && dropOk && applyOk, "GHOSTPRINT page: drop + APPLY");
    pageTipsAndLooks (*ed, page, shots, "print", "GHOSTPRINT", 12);
    edp.reset();
}
