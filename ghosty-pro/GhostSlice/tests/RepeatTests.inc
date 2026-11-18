// GhostSliceTests: phase 11 GHOST REPEAT (included by SliceTests.cpp)

static juce::File rendersDir()
{
    if (const char* d = std::getenv ("GSLICE_RENDERS_DIR")) { juce::File f (d); f.createDirectory(); return f; }
    return {};
}
static void saveRender (const juce::AudioBuffer<float>& b, double sr, const juce::String& name)
{
    const auto d = rendersDir(); if (d == juce::File()) return;
    juce::WavAudioFormat wav; writeAudio (b, sr, d.getChildFile (name), wav, 24);
}

static void testRepeat()
{
    section ("[16] GHOST REPEAT: trigger maths, plan expansion, bit-exact renders, clicks, LIVE, UI");
    using namespace ghosty::slicevoice;
    // ---- the maths
    {
        std::vector<double> t; retrig::constantTimes (retrig::periodSeconds (120, 16, 0), 1.0, t);
        double e = 0; for (size_t n = 0; n < t.size(); ++n) e = std::max (e, std::abs (t[n] - 0.125 * (double) n));
        const bool periods = std::abs (retrig::periodSeconds (120, 16, 1) - 0.125 * 2 / 3) < 1e-12 && std::abs (retrig::periodSeconds (120, 16, 2) - 0.1875) < 1e-12
                          && std::abs (retrig::periodSeconds (120, 128, 0) - 0.015625) < 1e-12 && std::abs (retrig::periodSeconds (120, 4, 0) - 0.5) < 1e-12;
        check (t.size() == 8 && e < 1e-12 && periods, "constant: t_n = n P exactly; 1/16 = 125 ms, 1/16T = 83.3 ms, 1/16. = 187.5 ms, 1/128 = 15.6 ms at 120 BPM");
        // RAMP: the closed form against a numeric solve of Phi(t) = n
        std::vector<double> rt; const double P0 = 0.1, k = 3.0, D = 2.0; retrig::rampTimes (P0, k, D, rt);
        double re = 0;
        for (size_t n = 0; n < rt.size(); ++n)
        {
            const double f0 = 1.0 / P0; auto Phi = [&] (double x) { return f0 * D * (std::pow (k, x / D) - 1.0) / std::log (k); };
            double lo = 0, hi = D; for (int it = 0; it < 200; ++it) { const double m = 0.5 * (lo + hi); (Phi (m) < (double) n ? lo : hi) = m; }
            re = std::max (re, std::abs (rt[n] - 0.5 * (lo + hi)));
        }
        bool accel = true; for (size_t n = 2; n < rt.size(); ++n) accel = accel && (rt[n] - rt[n - 1]) < (rt[n - 1] - rt[n - 2]);
        check (re < 1e-9 && accel && rt.size() > 20, "RAMP: t_n = D ln(1 + n ln k / (f0 D)) / ln k (error " + juce::String (re, 12) + " s), intervals shrink (" + juce::String ((int) rt.size()) + " triggers)");
        // RATCHET: monotonic curves = monotonic rates
        const float up[5] { -1.0f, -0.5f, 0.0f, 0.75f, 1.5f }, down[5] { 1.5f, 0.5f, 0.0f, -0.5f, -1.0f }, flat[5] { 0, 0, 0, 0, 0 };
        std::vector<double> tu, td, tf, tc; retrig::curveTimes (0.1, up, 5, 3.0, tu); retrig::curveTimes (0.1, down, 5, 3.0, td); retrig::curveTimes (0.1, flat, 5, 3.0, tf);
        retrig::constantTimes (0.1, 3.0, tc);
        bool incr = tu.size() > 3, decr = td.size() > 3;
        for (size_t n = 2; n < tu.size(); ++n) incr = incr && (tu[n] - tu[n - 1]) < (tu[n - 1] - tu[n - 2]) + 1e-9;
        for (size_t n = 2; n < td.size(); ++n) decr = decr && (td[n] - td[n - 1]) > (td[n - 1] - td[n - 2]) - 1e-9;
        double fe = tf.size() == tc.size() ? 0.0 : 1.0; for (size_t n = 0; n < std::min (tf.size(), tc.size()); ++n) fe = std::max (fe, std::abs (tf[n] - tc[n]));
        check (incr && decr && fe < 1e-9, "GHOST RATCHET: a rising curve = steadily faster repeats, a falling one = slower; flat = the constant grid (" + juce::String ((int) tu.size()) + " / " + juce::String ((int) td.size()) + " triggers)");
        RepeatOp sw; sw.length = 48000; sw.swing = 0.62; const auto st = repeat::triggers (sw, 120, 48000);
        bool swOk = st.size() == 8; for (auto& g : st) swOk = swOk && std::abs (g.t - (0.125 * g.n + ((g.n & 1) ? 0.24 * 0.125 : 0.0))) < 1e-12;
        double a = 0.004, rr = 0.004; retrig::fitFades (0.002, a, rr);
        check (swOk && a + rr + 0.001 <= 0.002 + 1e-12 && a >= 0.00025, "SWING 62%: odd repeats late by 0.24 P; fades fit a 2 ms period (" + juce::String (a * 1000, 2) + " + " + juce::String (rr * 1000, 2) + " ms)");
    }
    // ---- the plan
    Rig r (sine (48000, 3.0, 200.0, 0.4));
    const double start = 24061;   // (not on a zero crossing)
    int64 id = 0; auto q = ops::addRepeat (r->project(), start, start + 24000, &id); r->apply (*q, "repeat");
    {
        const auto& pl = r->currentPlan();
        std::vector<const Piece*> reps; for (auto& pc : pl.pieces) if (pc.repeatIndex >= 0) reps.push_back (&pc);
        bool pos = reps.size() == 4; std::set<uint64> keys;
        for (size_t n = 0; n < reps.size() && pos; ++n)
        {
            pos = reps[n]->dst0 == (int64) std::llround (start) + (int64) std::llround (0.125 * (double) n * 48000) && reps[n]->length() == 6000 && reps[n]->u0 == (int64) std::llround (start);
            keys.insert (reps[n]->studioKey);
        }
        bool cut = true; for (auto& pc : pl.pieces) if (pc.repeatIndex < 0 && pc.dst1 > (int64) start && pc.dst0 < (int64) start + 24000) cut = false;
        check (pos && cut, "1/16 over 0.5 s = 4 repetitions at start + n P, each P long, reading the span's start; the arrangement under it is cut out");
        check (keys.size() == 1, "equal repetitions share one cache key (one STUDIO render for all of them)");
        RepeatOp x = *r->project().findRepeat (id); x.retrigger = false; Project g = *ops::setRepeat (r->project(), x);
        const auto gp = compile (g, r->getSource().get(), CompileOptions {});
        bool gate = true; int n = 0; for (auto& pc : gp.pieces) if (pc.repeatIndex >= 0) { gate = gate && pc.u0 == pc.dst0; ++n; }
        check (gate && n == 4, "RETRIG off: each repetition gates the audio under it (the source runs on)");
        CompileOptions o100; o100.bpm = 100;
        const auto hp = compile (r->project(), r->getSource().get(), o100);
        std::vector<int64> d0; for (auto& pc : hp.pieces) if (pc.repeatIndex >= 0) d0.push_back (pc.dst0);
        check (d0.size() == 4 && d0[1] - d0[0] == (int64) std::llround (0.15 * 48000), "host sync: at the DAW's 100 BPM the 1/16 period is 150 ms");
    }
    // ---- bit-exact renders, clicks, LIVE
    RepeatOp full = *r->project().findRepeat (id);
    full.rateDiv = 32; full.pitchSemis = 2; full.reverse = 1; full.decay = 0.85; full.swing = 0.58; full.ratchetOn = true; full.ratchet = { -1.0f, 0.0f, 1.0f, 1.5f, 0.5f };
    r->apply (*ops::setRepeat (r->project(), full), "repeat params");
    {
        const auto a = r.render(); const auto f1 = r->currentPlan().fingerprint;
        Rig r2 (sine (48000, 3.0, 200.0, 0.4)); r2->apply (r->project(), "same");
        const auto b = r2.render();
        check (bitEqual (a, b) && r2->currentPlan().fingerprint == f1, "bit-exact: the same project renders the same samples (pitch steps, reverse, swing, ratchet)");
        saveRender (a, 48000, "repeat-ratchet-sine.wav");
    }
    {   // fades vs a hard cut, on a 1x retrigger
        RepeatOp x = *r->project().findRepeat (id); x = RepeatOp(); x.id = id; x.start = start; x.length = 24000; x.rateDiv = 32;
        r->apply (*ops::setRepeat (r->project(), x), "plain");
        const auto smooth = r.render();
        x.fadeInMs = 0; x.fadeOutMs = 0; r->apply (*ops::setRepeat (r->project(), x), "hard");
        const auto hard = r.render();
        const float bound = 0.4f * (float) (2.0 * juce::MathConstants<double>::pi * 200.0 / 48000.0 + juce::MathConstants<double>::pi / (2.0 * 48.0)) * 1.15f;
        const int a0 = (int) start - 100, a1 = (int) start + 24100;
        check (maxStep (smooth, a0, a1) <= bound && maxStep (hard, a0, a1) > 4.0f * maxStep (smooth, a0, a1),
               "no clicks: each repetition fades in / out (max step " + juce::String (maxStep (smooth, a0, a1), 4) + " <= " + juce::String (bound, 4) + "; without fades " + juce::String (maxStep (hard, a0, a1), 4) + ")");
        r->apply (*ops::setRepeat (r->project(), full), "back");
    }
    {   // LIVE: plays at once, the same as the render (non-pitched repetitions are DIRECT)
        RepeatOp x = full; x.pitchSemis = 0; r->apply (*ops::setRepeat (r->project(), x), "unpitched");
        const auto live = playFor (*r.p, 0, 72000, 256);
        const auto ref = render::plan (r->currentPlan(), *r->getWorking(), nullptr, false);
        float e = 0; for (int i = 0; i < 72000; ++i) e = std::max (e, std::abs (live.getSample (0, i) - ref.getSample (0, i)));
        check (e < 1e-5f, "audible LIVE at once: the playing plugin = the render (" + juce::String (e, 8) + ")");
        r->apply (*ops::setRepeat (r->project(), full), "back");
        const auto pl = playFor (*r.p, 0, 72000, 256);
        if (std::getenv ("GSLICE_DEBUG")) for (auto& pc : r->currentPlan().pieces) std::printf ("    piece %lld..%lld u0 %lld rep %d stretch %d pitch %.1f rev %d gain %.3f\n", (long long) pc.dst0, (long long) pc.dst1, (long long) pc.u0, pc.repeatIndex, (int) pc.stretch, pc.pitchOffset, (int) pc.reverse, pc.gain);
        float pk = 0; for (int i = (int) start; i < (int) start + 24000; ++i) pk = std::max (pk, std::abs (pl.getSample (0, i)));
        check (pk > 0.1f, "pitched repetitions play LIVE through the stretcher (peak " + juce::String (pk, 2) + ")");
    }
    {   // nondestructive + undo + JSON
        const auto f = r->currentPlan().fingerprint;
        r->apply (*ops::removeRepeat (r->project(), id), "remove");
        const bool gone = r->project().repeats.empty();
        Rig plain (sine (48000, 3.0, 200.0, 0.4));
        const bool orig = r->currentPlan().fingerprint == plain->currentPlan().fingerprint;
        r->undo();
        Project back; juce::String err;
        check (gone && orig && r->currentPlan().fingerprint == f && json::fromString (json::toString (r->project()), back, err) && back == r->project(),
               "nondestructive: REMOVE gives the plain arrangement back, undo restores the REPEAT; project.json keeps every parameter");
    }
}

static void testRepeatUi (const juce::File& shots)
{
    section ("[17] REPEAT UI: REPEAT button -> strip, handle drag, layout, screenshots");
    using namespace ghosty::themes;
    Rig rig (drums (48000, 4.0));
    std::unique_ptr<SliceEditor> ed (static_cast<SliceEditor*> (rig->createEditor()));
    ed->setVisible (true); ed->setSize (1280, 780); ed->resized(); ed->setWorkspace (gslice::ui::Workspace::Edit); pump (20);
    auto& v = ed->view; v.zoomToFit(); pump (10);
    v.sel.a = 48000; v.sel.b = 72000;
    ed->repeatBtn.triggerClick(); pump (20);
    const auto* rp = rig->project().repeats.empty() ? nullptr : &rig->project().repeats.front();
    check (rp != nullptr && rp->start == 48000 && rp->length == 24000 && ed->repeatStrip.isVisible() && ed->repeatStrip.repeatId() == rp->id,
           "REPEAT on a selection: a span over it, the REPEAT strip opens");
    ed->repeatStrip.rateBox.setSelectedId (4, juce::sendNotificationSync);
    ed->repeatStrip.retrigBtn.triggerClick(); pump (10);
    check (rig->project().repeats.front().rateDiv == 32 && ! rig->project().repeats.front().retrigger, "the strip edits it (RATE 1/32, RETRIG off)");
    // drag the length handle (on the overlay over the waveform)
    auto& ov = ed->overlay;
    const auto h = ov.repeatHandle (rig->project().repeats.front()).getCentre(), h2 = h.translated (80, 0);
    const bool hit = ov.hitTest ((int) h.x, (int) h.y) && ! ov.hitTest ((int) h.x - 60, (int) h.y + 40);
    ov.mouseDown (mouseAt (ov, h)); ov.mouseDrag (mouseAt (ov, h2, juce::ModifierKeys::leftButtonModifier, 1, h)); ov.mouseUp (mouseAt (ov, h2, {}, 1, h));
    const double len = rig->project().repeats.front().length;
    check (hit && len > 24000 && std::abs (std::fmod (48000 + len, rig->project().gridStep())) < 1.0 && rig->getHistory().undoLabel() == "Repeat length",
           "drag the handle = the repetition length (snapped to the grid: " + juce::String (len, 0) + "), one undo step; elsewhere the waveform keeps the mouse");
    for (auto size : { juce::Point<int> (1160, 700), juce::Point<int> (1600, 1000) })
    {
        ed->setSize (size.x, size.y); ed->resized(); pump (20);
        int out = 0, overlap = 0, tight = 0;
        auto vis = ed->repeatStrip.controls();
        for (auto* c : vis) if (! ed->repeatStrip.getLocalBounds().contains (c->getBounds())) { ++out; std::printf ("    outside: %s\n", c->getComponentID().toRawUTF8()); }
        for (size_t i = 0; i < vis.size(); ++i) for (size_t j = i + 1; j < vis.size(); ++j) if (vis[i]->getBounds().intersects (vis[j]->getBounds())) { ++overlap; std::printf ("    overlap: %s / %s\n", vis[i]->getComponentID().toRawUTF8(), vis[j]->getComponentID().toRawUTF8()); }
        for (auto* c : vis) if (auto* b = dynamic_cast<juce::TextButton*> (c)) { const auto f = ed->lnf.getTextButtonFont (*b, b->getHeight()); if (ghosty::textWidth (f, b->getButtonText()) > (float) b->getWidth() - 6) { ++tight; std::printf ("    text too wide: %s\n", b->getButtonText().toRawUTF8()); } }
        std::vector<juce::Component*> ev;
        for (auto* c : ed->getChildren()) if (c->isVisible() && dynamic_cast<ghosty::exportgate::Hint*> (c) == nullptr && dynamic_cast<juce::TooltipWindow*> (c) == nullptr && dynamic_cast<juce::ResizableCornerComponent*> (c) == nullptr) ev.push_back (c);
        for (size_t i = 0; i < ev.size(); ++i) for (size_t j = i + 1; j < ev.size(); ++j) if (ev[i]->getBounds().intersects (ev[j]->getBounds())) { ++overlap; std::printf ("    overlap: %s / %s\n", ev[i]->getComponentID().toRawUTF8(), ev[j]->getComponentID().toRawUTF8()); }
        for (auto* c : ev) if (auto* b = dynamic_cast<juce::TextButton*> (c)) { const auto f = ed->lnf.getTextButtonFont (*b, b->getHeight()); if (ghosty::textWidth (f, b->getButtonText()) > (float) b->getWidth() - 6) { ++tight; std::printf ("    text too wide: %s\n", b->getButtonText().toRawUTF8()); } }
        check (out == 0 && overlap == 0 && tight == 0, juce::String (size.x) + "x" + juce::String (size.y) + " EDIT + REPEAT strip: everything inside, no overlaps, every label fits");
    }
    ed->setSize (1280, 780); ed->resized();
    RepeatOp x = rig->project().repeats.front(); x.ratchetOn = true; x.ratchet = { -1.0f, -0.25f, 0.5f, 1.25f, 1.75f }; x.reverse = 1; x.pitchSemis = -1;
    rig->apply (*ops::setRepeat (rig->project(), x), "ratchet"); ed->repeatStrip.refresh();
    v.viewStart = 30000; v.hfPerPx = 60; v.repaint(); pump (20);
    for (auto t : { Theme::Dark, Theme::Purple, Theme::Light })
    {
        ed->setTheme (t); pump (20);
        snap (*ed, shots.getChildFile ("slice-" + displayName (t).toLowerCase() + "-repeat.png"));
    }
    saveRender (rig.render(), 48000, "repeat-ratchet-drums.wav");
    check (shots.getChildFile ("slice-light-repeat.png").existsAsFile(), "REPEAT screenshots written (3 themes)");
    ed.reset(); pump (20);
}
