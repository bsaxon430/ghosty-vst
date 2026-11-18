// GhostSliceTests: GHOST CHOP slice modes (TRANSIENT / SYLLABLE / NOTE / BEAT + EQUAL / MANUAL / REGIONS), SENSITIVITY,
// SPLIT @ SLICES. Included after PitchEditTests.inc (shares voice / fourNotes / stereoOf). Groups: chop, chopui

static std::unique_ptr<Rig> analysedRig (juce::AudioBuffer<float> audio, const char* material)
{
    auto r = std::make_unique<Rig> (std::move (audio));
    { Project q = (*r)->project(); q.material = material; (*r)->apply (q, "material"); }
    (*r)->waitAnalysis (30000);
    return r;
}
static int sliceCount (Rig& rig, SliceMode m, double sens = 0.5, int div = 8)
{
    Project q = rig->project(); q.play.mode = m; q.play.sensitivity = sens; q.play.beatDiv = div; rig->apply (q, "mode");
    return (int) rig->currentSlices().size();
}

static void testChop()
{
    section ("[21] GHOST CHOP slice modes: TRANSIENT / SYLLABLE / NOTE / BEAT, SENSITIVITY, SPLIT @ SLICES");
    {   // TRANSIENT on 8th-note hits (120 BPM, 2 s = 8 hits) and BEAT on the same tempo
        auto rig = analysedRig (drums (48000, 2.0), "DRUMS");
        const int t = sliceCount (*rig, SliceMode::Transient);
        check (t == 8, "TRANSIENT: 8 hits -> " + juce::String (t) + " slices");
        const int b8 = sliceCount (*rig, SliceMode::Beat, 0.5, 8), b16 = sliceCount (*rig, SliceMode::Beat, 0.5, 16), b4 = sliceCount (*rig, SliceMode::Beat, 0.5, 4);
        check (b4 == 4 && b8 == 8 && b16 == 16, "BEAT at 120 BPM over 2 s: 1/4 = " + juce::String (b4) + ", 1/8 = " + juce::String (b8) + ", 1/16 = " + juce::String (b16) + " slices");
        // the slice points sit on the hits (within 3 ms)
        sliceCount (*rig, SliceMode::Transient);
        const auto sl = (*rig)->currentSlices(); double worst = 0;
        for (size_t i = 1; i < sl.size(); ++i) { const double hit = std::round ((double) sl[i].dst0 / 12000.0) * 12000.0; worst = std::max (worst, std::abs ((double) sl[i].dst0 - hit)); }
        check (worst <= 0.003 * 48000, "TRANSIENT cuts sit on the attacks (worst " + juce::String (worst / 48.0, 2) + " ms)");
        // SPLIT @ SLICES round trip: regions at the cuts, the render is unchanged (1x cuts are seamless), REGIONS mode gives the same slices
        const auto before = rig->render();
        std::vector<double> cuts; for (size_t i = 1; i < sl.size(); ++i) cuts.push_back ((*rig)->hfToPf ((double) sl[i].dst0));
        int made = 0;
        auto q = ops::splitAtSlices ((*rig)->project(), cuts, &made);
        bool ok = q.has_value() && made == (int) cuts.size() && (int) q->regions.size() == (int) sl.size() && q->play.mode == SliceMode::Regions;
        if (ok)
        {
            (*rig)->apply (*q, "split");
            const auto rs = (*rig)->currentSlices();
            bool same = rs.size() == sl.size();
            for (size_t i = 0; same && i < rs.size(); ++i) same = std::abs (rs[i].dst0 - sl[i].dst0) <= 1;
            ok = same && bitEqual (rig->render(), before);
            (*rig)->undo();
            ok = ok && (*rig)->project().regions.size() == 1 && (*rig)->project().play.mode == SliceMode::Transient;
        }
        check (ok, "SPLIT @ SLICES: " + juce::String (made + 1) + " regions at the cuts, the same render, REGIONS = the same slices, one undo step");
    }
    {   // SENSITIVITY: monotonic on clicks of random level
        Truth tr; auto rig = analysedRig (stereoOf (clickTrain (48000, 4.0, 5, tr)), "DRUMS");
        std::vector<int> n; for (double s : { 0.0, 0.2, 0.4, 0.6, 0.8, 1.0 }) n.push_back (sliceCount (*rig, SliceMode::Transient, s));
        bool mono = true; for (size_t i = 1; i < n.size(); ++i) mono = mono && n[i] >= n[i - 1];
        juce::String ns; for (int x : n) ns << x << " ";
        check (mono && n.back() > n.front(), "SENSITIVITY 0 .. 100 %: slice counts never fall (" + ns.trim() + ")");
    }
    {   // SYLLABLE on a spoken-like line: one cut per syllable; then 'words' become movable blocks
        Truth tr; auto rig = analysedRig (stereoOf (vocalLike (48000, 6, 3, tr)), "VOCAL");
        const int s = sliceCount (*rig, SliceMode::Syllable);
        int inside = 0; const int64 L = (*rig)->currentPlan().length;
        for (auto p : tr.pos) if (p > (int64) (0.12 * 48000) && p < L - (int64) (0.12 * 48000)) ++inside;
        check (std::abs (s - (inside + 1)) <= 1, "SYLLABLE: 6 syllables -> " + juce::String (s) + " slices (expected " + juce::String (inside + 1) + ")");
        const auto sl = (*rig)->currentSlices();
        std::vector<double> cuts; for (size_t i = 1; i < sl.size(); ++i) cuts.push_back ((*rig)->hfToPf ((double) sl[i].dst0));
        auto q = ops::splitAtSlices ((*rig)->project(), cuts);
        bool moved = false;
        if (q)
        {
            (*rig)->apply (*q, "split");
            const auto firstId = (*rig)->project().regions[1].id;
            auto r2 = ops::rearrange ((*rig)->project(), 1, (int) (*rig)->project().regions.size() - 1);
            if (r2) { (*rig)->apply (*r2, "rearrange"); std::vector<const Region*> rs; for (auto& r : (*rig)->project().regions) rs.push_back (&r);
                      std::sort (rs.begin(), rs.end(), [] (auto* a, auto* b) { return a->dstStart < b->dstStart; }); moved = rs.back()->id == firstId; }
        }
        check (moved, "the syllables are blocks: SPLIT @ SLICES then drag the first word to the end (rearranged)");
    }
    {   // NOTE: note starts + note ends before silence
        auto rig = analysedRig (stereoOf (voice (48000, fourNotes())), "MELODIC");
        const int s = sliceCount (*rig, SliceMode::Note);
        check (s == 7, "NOTE: C D | F E (a breath after D) -> " + juce::String (s) + " slices (lead-in, 4 notes, the breath, the tail)");
        const int lo = sliceCount (*rig, SliceMode::Note, 0.0), hi = sliceCount (*rig, SliceMode::Note, 1.0);
        check (lo <= s && s <= hi, "NOTE sensitivity monotonic (" + juce::String (lo) + " / " + juce::String (s) + " / " + juce::String (hi) + ")");
    }
    {   // EQUAL / MANUAL still work, and the new fields round trip
        auto rig = analysedRig (drums (48000, 2.0), "DRUMS");
        Project q = (*rig)->project(); q.play.mode = SliceMode::Equal; q.play.equalN = 5; (*rig)->apply (q, "eq");
        const int e = (int) (*rig)->currentSlices().size();
        q.play.mode = SliceMode::Manual; q.play.manual = { 30000.0, 60000.0 }; (*rig)->apply (q, "man");
        const int m = (int) (*rig)->currentSlices().size();
        Project p = (*rig)->project(); p.play.mode = SliceMode::Syllable; p.play.sensitivity = 0.8; p.play.beatDiv = 16;
        Project back; juce::String err; const bool rt = json::fromString (json::toString (p), back, err) && back == p;
        check (e == 5 && m == 3 && rt, "EQUAL 5 = 5 slices, MANUAL 2 cuts = 3 slices; mode / SENS / DIV round trip through JSON");
    }
}

static void testChopUi (const juce::File& shots)
{
    section ("[21b] CHOP in EDIT + PLAY: mode box, the dashed cut preview, SPLIT @ SLICES, screenshots");
    using namespace ghosty::themes;
    auto rig = analysedRig (drums (48000, 2.0), "DRUMS");
    std::unique_ptr<SliceEditor> ed (static_cast<SliceEditor*> ((*rig)->createEditor()));
    ed->setVisible (true); ed->setSize (1280, 780); ed->resized(); ed->setWorkspace (gslice::ui::Workspace::Edit); ed->view.zoomToFit(); pump (20);
    ed->chopBox.setSelectedId ((int) SliceMode::Transient + 1, juce::sendNotificationSync); pump (10);
    check ((*rig)->project().play.mode == SliceMode::Transient && ed->view.chopPreview.size() == 7 && ed->chopSensF.isVisible() && ed->splitSlicesBtn.isEnabled(),
           "EDIT: CHOP TRANSIENT shows 7 dashed cuts, SENS appears, SPLIT @ SLICES is on");
    for (auto t : { Theme::Dark, Theme::Purple, Theme::Light })
    {
        ed->setTheme (t); ed->refresh(); pump (10);
        snap (*ed, shots.getChildFile ("slice-" + displayName (t).toLowerCase() + "-chop-transient.png"));
    }
    ed->setTheme (Theme::Dark);
    ed->chopBox.setSelectedId ((int) SliceMode::Beat + 1, juce::sendNotificationSync); pump (10);
    check (ed->chopDivBox.isVisible() && ! ed->chopSensF.isVisible() && ed->view.chopPreview.size() == 7, "EDIT: CHOP BEAT shows the DIV box (1/8: 7 cuts)");
    snap (*ed, shots.getChildFile ("slice-black-chop-beat.png"));
    ed->chopBox.setSelectedId ((int) SliceMode::Transient + 1, juce::sendNotificationSync); pump (10);
    ed->splitSlicesBtn.triggerClick(); pump (20);
    check ((*rig)->project().regions.size() == 8 && ed->lane.blocks().size() == 8, "SPLIT @ SLICES: 8 regions = 8 blocks in the lane strip");
    snap (*ed, shots.getChildFile ("slice-black-chop-split.png"));
    (*rig)->undo();
    ed->setWorkspace (gslice::ui::Workspace::Play); pump (20);
    auto& pp = ed->playPanel;
    check (pp.chopBox.isVisible() && pp.sensF.isVisible() && pp.chopBox.getSelectedId() == (int) SliceMode::Transient + 1, "PLAY: the same CHOP mode + SENS");
    snap (*ed, shots.getChildFile ("slice-black-play-chop.png"));
    ed.reset(); pump (20);
}
