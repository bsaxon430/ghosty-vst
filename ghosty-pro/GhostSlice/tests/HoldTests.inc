// GhostSliceTests: phase 12 GHOST HOLD (included by SliceTests.cpp)

// a vowel-like synthetic: an "s" (noise), a vowel (220 Hz, 12 formant-shaped harmonics, 5.5 Hz +-20 ct vibrato, a quiet
// high noise band), a "t" (a noise click), silence
static juce::AudioBuffer<float> vowel (double sr)
{
    const int N = (int) (sr * 1.0);
    juce::AudioBuffer<float> b (2, N); b.clear();
    std::mt19937 rng (11); std::uniform_real_distribution<float> U (-1.0f, 1.0f);
    float st[6] {}; double ph = 0;
    for (int i = 0; i < N; ++i)
    {
        const double t = i / sr;
        float d = U (rng); for (auto& s : st) { const float o = d - s; s = d; d = o; }   // 6th difference: a high band
        const float hi = d / 400.0f;
        auto ss = [] (double x) { x = juce::jlimit (0.0, 1.0, x); return x * x * (3 - 2 * x); };
        const double env = ss ((t - 0.08) / 0.02) * (1.0 - ss ((t - 0.66) / 0.02));
        const double f = 220.0 * std::exp2 (20.0 * std::sin (2.0 * juce::MathConstants<double>::pi * 5.5 * t) / 1200.0);
        ph += 2.0 * juce::MathConstants<double>::pi * f / sr;
        double s = 0;
        for (int h = 1; h <= 12; ++h) { const double fh = h * 220.0; s += (std::exp (-std::pow ((fh - 700.0) / 300.0, 2)) + 0.6 * std::exp (-std::pow ((fh - 1200.0) / 400.0, 2)) + 0.08) * std::sin (h * ph); }
        double v = 0.12 * s * env + 0.5 * hi * env;
        if (t < 0.08) v += 4.0 * hi * ss (t / 0.01) * (1.0 - ss ((t - 0.07) / 0.01));
        if (t >= 0.68 && t < 0.76) v += 3.0 * hi * std::exp (-(t - 0.68) / 0.01);
        for (int c = 0; c < 2; ++c) b.setSample (c, i, (float) v);
    }
    return b;
}
static juce::AudioBuffer<float> hp6 (const juce::AudioBuffer<float>& x, int a, int b)
{
    juce::AudioBuffer<float> y (1, juce::jmax (1, b - a)); y.clear();
    float st[6] {};
    for (int i = a; i < b; ++i) { float d = x.getSample (0, i); for (auto& s : st) { const float o = d - s; s = d; d = o; } y.setSample (0, i - a, d); }
    return y;
}
static double autocorrAt (const juce::AudioBuffer<float>& y, int lag, int skip = 64)
{
    double best = -1;
    for (int L = lag - 48; L <= lag + 48; ++L)
    {
        if (L <= 0 || L + skip >= y.getNumSamples()) continue;
        double xy = 0, xx = 0, yy = 0;
        for (int n = skip; n + L < y.getNumSamples(); ++n) { const double p = y.getSample (0, n), q = y.getSample (0, n + L); xy += p * q; xx += p * p; yy += q * q; }
        if (xx > 0 && yy > 0) best = std::max (best, xy / std::sqrt (xx * yy));
    }
    return best;
}
struct PitchStats { double maxJump = 0, median = 0; int frames = 0; };
static PitchStats pitchStats (const juce::AudioBuffer<float>& b, int a, int e)
{
    PitchStats st;
    auto src = source::fromBuffer (b, 48000, "x.wav");
    const auto raw = hold::pitchTrack (*src, a, e, 240);
    auto f = raw;   // a 3-point median (single-frame tracker outliers are not the audio's pitch)
    for (size_t i = 1; i + 1 < raw.size(); ++i) { double x[3] { raw[i - 1], raw[i], raw[i + 1] }; std::sort (x, x + 3); f[i] = x[0] <= 0 ? raw[i] : x[1]; }
    std::vector<double> v; double prev = 0;
    int k = 0, at = -1;
    for (double x : f) { if (x > 0) { v.push_back (x); if (prev > 0 && std::abs (1200.0 * std::log2 (x / prev)) > st.maxJump) { st.maxJump = std::abs (1200.0 * std::log2 (x / prev)); at = k; } } prev = x; ++k; }
    if (std::getenv ("GSLICE_DEBUG") && at >= 0)
    {
        std::printf ("    max jump at frame %d (sample %d):", at, a + at * 240);
        for (int j = std::max (0, at - 6); j < std::min ((int) f.size(), at + 6); ++j) std::printf (" %.1f", f[(size_t) j] > 0 ? 1200.0 * std::log2 (f[(size_t) j] / 220.0) : -999.0);
        std::printf ("\n");
    }
    st.frames = (int) v.size();
    if (! v.empty()) { std::sort (v.begin(), v.end()); st.median = v[v.size() / 2]; }
    return st;
}

static void testHold()
{
    section ("[18] GHOST HOLD: stable region, four modes, pitch continuity, no loop, head / tail exact, determinism, clicks");
    const auto src = vowel (48000);
    Rig base (vowel (48000));
    const int64 selA = 2400, selB = 34560;   // 50 .. 720 ms: the end of the "s", the vowel, the start of the "t"
    const auto st = hold::pickStable (*base->getSource(), selA, selB);
    check (st.a >= (int64) (0.10 * 48000) && st.b <= (int64) (0.66 * 48000) && st.b - st.a >= (int64) (0.08 * 48000) && ! st.unstable,
           "the auto-picked stable region is inside the vowel (" + juce::String (st.a / 48.0, 0) + " .. " + juce::String (st.b / 48.0, 0) + " ms, score " + juce::String (st.score, 2) + ")");
    const double total = 4.0 * (double) (selB - selA);
    const double held = total - (double) (st.a - selA) - (double) (selB - st.b);
    const int64 tailStart = st.a + (int64) std::llround (held), shift = tailStart - st.b;
    const float vowelStep = maxStep (src, (int) st.a, (int) st.b);
    static const char* names[] { "ELASTIC", "SUSTAIN", "TEXTURE", "FREEZE" };
    for (int mi = 0; mi < 4; ++mi)
    {
        const auto mode = (HoldMode) mi;
        Rig r (vowel (48000));
        int64 id = 0; juce::String why;
        auto q = ops::addHold (r->project(), r.r0().id, selA, selB, st.a, st.b, held, mode, &id, &why);
        if (! q) { check (false, juce::String ("addHold ") + names[mi] + ": " + why); continue; }
        r->apply (*q, "hold");
        const auto out = r.render();
        const juce::String nm (names[mi]);
        bool head = out.getNumSamples() == src.getNumSamples() + (int) shift, tail = head;
        for (int c = 0; c < 2 && head; ++c) for (int i = 0; i < (int) st.a; ++i) if (out.getSample (c, i) != src.getSample (c, i)) { head = false; break; }
        for (int c = 0; c < 2 && tail; ++c) for (int i = (int) st.b; i < src.getNumSamples(); ++i) if (out.getSample (c, i + (int) shift) != src.getSample (c, i)) { tail = false; break; }
        check (head && tail, nm + ": held 4x; the head (consonant) and the tail are bit-identical to the source (1x)");
        Rig r2 (vowel (48000)); r2->apply (*q, "hold");
        check (bitEqual (out, r2.render()), nm + ": deterministic (a fresh processor renders the same samples)");
        const int j0 = (int) st.a - 240, j1 = (int) tailStart + 240;
        const float hs = maxStep (out, j0, j1);
        const float joinIn = maxStep (out, (int) st.a - 144, (int) st.a + 144), joinOut = maxStep (out, (int) tailStart - 144, (int) tailStart + 144);
        check (hs <= vowelStep * (mi >= 2 ? 2.0f : 1.5f) && std::max (joinIn, joinOut) <= vowelStep * 1.5f,
               nm + ": no clicks (max step " + juce::String (hs, 4) + ", joins " + juce::String (joinIn, 4) + " / " + juce::String (joinOut, 4) + "; the vowel itself " + juce::String (vowelStep, 4) + ")");
        saveRender (out, 48000, "hold-" + nm.toLowerCase() + "-4x.wav");
        if (mode == HoldMode::Sustain || mode == HoldMode::Elastic)
        {
            const auto ps = pitchStats (out, (int) st.a - 1440, (int) tailStart + 1440);
            const auto pv = pitchStats (src, (int) st.a, (int) st.b);
            if (std::getenv ("GSLICE_DEBUG")) std::printf ("    source max jump %.1f ct\n", pv.maxJump);
            const double medErr = std::abs (1200.0 * std::log2 (ps.median / pv.median));
            if (mode == HoldMode::Elastic)   // (reported; the join drift is measured against the source in [34] elastic)
                std::printf ("    ELASTIC pitch: max jump %.1f ct between 5 ms frames, median within %.1f ct\n", ps.maxJump, medErr);
            else check (ps.maxJump < 10.0 && medErr < 10.0 && ps.frames > 100,
                   nm + ": pitch continuity: no jump over 10 ct between 5 ms frames across the hold and its joins (max " + juce::String (ps.maxJump, 1) + " ct), median within " + juce::String (medErr, 1) + " ct");
        }
        if (mode == HoldMode::Sustain)
        {
            const int L = (int) (st.b - st.a);
            const auto y = hp6 (out, (int) st.a + 2400, (int) tailStart - 2400);
            juce::AudioBuffer<float> loop (1, (int) held); for (int i = 0; i < (int) held; ++i) loop.setSample (0, i, src.getSample (0, (int) st.a + i % L));
            const auto yl = hp6 (loop, 2400, (int) held - 2400);
            const double ac = autocorrAt (y, L), acLoop = autocorrAt (yl, L);
            check (ac < 0.3 && acLoop > 0.8, "SUSTAIN: no periodic loop: autocorrelation at the stable length " + juce::String (L / 48.0, 0) + " ms = " + juce::String (ac, 3) + " (a plain loop: " + juce::String (acLoop, 3) + ")");
            gAudioAllocs = 0;
            const auto live = playFor (*r.p, 0, out.getNumSamples(), 256);
            float e = 0; int ei = 0; for (int i = 0; i < out.getNumSamples(); ++i) { const float d = std::abs (live.getSample (0, i) - out.getSample (0, i)); if (d > e) { e = d; ei = i; } }
            if (std::getenv ("GSLICE_DEBUG")) { std::printf ("    max err at %d (hold %lld .. %lld)\n", ei, (long long) st.a, (long long) tailStart); for (auto& pc : r->currentPlan().pieces) std::printf ("    piece %lld..%lld u0 %lld hold %d stretch %d\n", (long long) pc.dst0, (long long) pc.dst1, (long long) pc.u0, pc.holdMode, (int) pc.stretch); }
            check (e < 1e-5f && gAudioAllocs.load() == 0, "SUSTAIN: LIVE plays exactly the STUDIO samples (" + juce::String (e, 8) + "), no allocation on the audio thread");
        }
        if (mode == HoldMode::Texture)
        {
            r->setStudioMode (true); r->getPlayer().switches = 0;
            bool waited = false;
            const auto sw = playFor (*r.p, 0, out.getNumSamples(), 256, [&] (int64 done) { if (! waited && done >= st.a + 4800) { r->waitStudio (30000); waited = true; } });
            float post = 0; for (int i = (int) tailStart - 9600; i < (int) tailStart - 2400; ++i) post = std::max (post, std::abs (sw.getSample (0, i) - out.getSample (0, i)));
            check (r->getPlayer().switches.load() >= 1 && post < 1e-5f && maxStep (sw, j0, j1) <= vowelStep * 2.0f,
                   "TEXTURE: LIVE previews with SUSTAIN, the STUDIO render takes over in the background, click-free (" + juce::String (r->getPlayer().switches.load()) + " switch, then = the render)");
        }
    }
    // ops: the held length (right edge), the stable handles, remove = back to the original, refusals, JSON
    {
        Rig r (vowel (48000));
        const auto orig = r.r0();
        int64 id = 0;
        r->apply (*ops::addHold (r->project(), orig.id, selA, selB, st.a, st.b, held, HoldMode::Sustain, &id), "hold");
        const double len1 = r.r0().length();
        r->apply (*ops::setHoldLength (r->project(), orig.id, id, held * 1.5), "longer");
        const double len2 = r.r0().length();
        auto sq = ops::setHoldStable (r->project(), orig.id, id, st.a + 960, st.b - 960);
        const bool stOk = sq.has_value() && ! sq->find (orig.id)->holds.front().autoStable;
        Project back; juce::String err;
        const bool js = json::fromString (json::toString (r->project()), back, err) && back == r->project();
        r->apply (*ops::removeHold (r->project(), orig.id, id), "remove");
        check (std::abs ((len2 - len1) - held * 0.5) < 1.0 && stOk && js && r.r0().warp == orig.warp && r.r0().holds.empty(),
               "the right edge sets the held length; the stable handles move it (manual); project.json keeps it; REMOVE = the original region");
        Rig rv (vowel (48000)); rv->apply (*ops::setReverse (rv->project(), rv.r0().id, true), "rev");
        juce::String why; const bool refused = ! ops::addHold (rv->project(), rv.r0().id, selA, selB, st.a, st.b, held, HoldMode::Sustain, nullptr, &why).has_value();
        check (refused && why.isNotEmpty(), "HOLD on a reversed region is refused (" + why + ")");
    }
    saveRender (src, 48000, "hold-source.wav");
}

static void testHoldUi (const juce::File& shots)
{
    section ("[19] HOLD UI: HOLD button, handles, strip, screenshots");
    using namespace ghosty::themes;
    Rig rig (vowel (48000));
    std::unique_ptr<SliceEditor> ed (static_cast<SliceEditor*> (rig->createEditor()));
    ed->setVisible (true); ed->setSize (1280, 780); ed->resized(); ed->setWorkspace (gslice::ui::Workspace::Edit); pump (20);
    auto& v = ed->view; v.zoomToFit(); pump (10);
    v.sel.a = 2400; v.sel.b = 34560;
    ed->holdBtn.triggerClick(); pump (20);
    const auto& rr = rig->project().regions.front();
    check (rr.holds.size() == 1 && ed->holdStrip.isVisible() && ed->holdStrip.holdId() == rr.holds.front().id && rr.length() > 2.5 * (34560 - 2400),
           "HOLD on a selection: the stable part is found and held (3x first), the HOLD strip opens");
    auto& ov = ed->overlay;
    v.zoomToFit(); pump (10);
    const auto& h0 = rig->project().regions.front().holds.front();
    const auto e = ov.holdHandle (rig->project().regions.front(), h0, 3).getCentre(), e2 = e.translated (60, 0);
    const double l0 = rig->project().regions.front().length();
    ov.mouseDown (mouseAt (ov, e)); ov.mouseDrag (mouseAt (ov, e2, juce::ModifierKeys::leftButtonModifier, 1, e)); ov.mouseUp (mouseAt (ov, e2, {}, 1, e));
    const double l1 = rig->project().regions.front().length();
    check (l1 > l0 + 1000 && rig->getHistory().undoLabel() == "Hold length", "drag the right edge = a longer hold (" + juce::String ((l1 - l0) / 48.0, 0) + " ms more), one undo step");
    const auto a = ov.holdHandle (rig->project().regions.front(), rig->project().regions.front().holds.front(), 1).getCentre(), a2 = a.translated (8, 0);
    ov.mouseDown (mouseAt (ov, a)); ov.mouseDrag (mouseAt (ov, a2, juce::ModifierKeys::leftButtonModifier, 1, a)); ov.mouseUp (mouseAt (ov, a2, {}, 1, a));
    check (! rig->project().regions.front().holds.front().autoStable, "drag a stable-region handle = a manual override");
    ed->holdStrip.autoBtn.triggerClick(); pump (10);
    check (rig->project().regions.front().holds.front().autoStable, "AUTO picks the stable region again");
    ed->holdStrip.modeBox.setSelectedId (3, juce::sendNotificationSync);
    check (rig->project().regions.front().holds.front().mode == HoldMode::Texture, "MODE: TEXTURE");
    ed->holdStrip.modeBox.setSelectedId (2, juce::sendNotificationSync);
    for (auto size : { juce::Point<int> (1160, 700), juce::Point<int> (1600, 1000) })
    {
        ed->setSize (size.x, size.y); ed->resized(); pump (20);
        int out = 0, overlap = 0, tight = 0;
        auto vis = ed->holdStrip.controls();
        for (auto* c : vis) if (! ed->holdStrip.getLocalBounds().contains (c->getBounds())) { ++out; std::printf ("    outside: %s\n", c->getComponentID().toRawUTF8()); }
        for (size_t i = 0; i < vis.size(); ++i) for (size_t j = i + 1; j < vis.size(); ++j) if (vis[i]->getBounds().intersects (vis[j]->getBounds())) { ++overlap; std::printf ("    overlap: %s / %s\n", vis[i]->getComponentID().toRawUTF8(), vis[j]->getComponentID().toRawUTF8()); }
        for (auto* c : vis) if (auto* b = dynamic_cast<juce::TextButton*> (c)) { const auto f = ed->lnf.getTextButtonFont (*b, b->getHeight()); if (ghosty::textWidth (f, b->getButtonText()) > (float) b->getWidth() - 6) ++tight; }
        std::vector<juce::Component*> ev;
        for (auto* c : ed->getChildren()) if (c->isVisible() && dynamic_cast<ghosty::exportgate::Hint*> (c) == nullptr && dynamic_cast<juce::TooltipWindow*> (c) == nullptr && dynamic_cast<juce::ResizableCornerComponent*> (c) == nullptr) ev.push_back (c);
        for (size_t i = 0; i < ev.size(); ++i) for (size_t j = i + 1; j < ev.size(); ++j) if (ev[i]->getBounds().intersects (ev[j]->getBounds())) { ++overlap; std::printf ("    overlap: %s / %s\n", ev[i]->getComponentID().toRawUTF8(), ev[j]->getComponentID().toRawUTF8()); }
        check (out == 0 && overlap == 0 && tight == 0, juce::String (size.x) + "x" + juce::String (size.y) + " EDIT + HOLD strip: everything inside, no overlaps, every label fits");
    }
    ed->setSize (1280, 780); ed->resized(); v.zoomToFit(); pump (20);
    for (auto t : { Theme::Dark, Theme::Purple, Theme::Light })
    {
        ed->setTheme (t); pump (20);
        snap (*ed, shots.getChildFile ("slice-" + displayName (t).toLowerCase() + "-hold.png"));
    }
    check (shots.getChildFile ("slice-light-hold.png").existsAsFile(), "HOLD screenshots written (3 themes)");
    ed.reset(); pump (20);
}
