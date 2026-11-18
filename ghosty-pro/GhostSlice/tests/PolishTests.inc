// GhostSliceTests: first-release polish - layout at both window sizes in every workspace, tooltips on every control, contrast
// in the three looks (LIGHT included), the ? shortcut overlay, the mascot, the STUDIO progress bar, the offline bounce waiting
// for STUDIO renders. Groups: polish (engine), polishui (editor + screenshots)
#include <thread>

static void collectControls (juce::Component& c, std::vector<juce::Component*>& out)
{
    for (auto* ch : c.getChildren())
    {
        if (! ch->isVisible()) continue;
        if (dynamic_cast<juce::Button*> (ch) != nullptr || dynamic_cast<juce::Slider*> (ch) != nullptr || dynamic_cast<juce::ComboBox*> (ch) != nullptr) out.push_back (ch);
        else if (dynamic_cast<juce::TooltipWindow*> (ch) == nullptr) collectControls (*ch, out);
    }
}

static void testPolish()
{
    section ("[23] polish: the offline bounce waits for STUDIO renders, STUDIO progress");
    // a stretched + pitched arrangement, STUDIO on: an offline (non-realtime) render must play the STUDIO renders from the first block
    Rig rig (pad (48000, 2.0));
    rig->waitAnalysis (30000);   // (the analysis' median f0 is the formant base of a pitched plan: let it land before the bounce)
    auto q = ops::splitAt (rig->project(), rig.r0().id, 48000); q = ops::stretchTo (*q, q->regions[1].id, q->regions[1].length() * 1.5);
    q->pitchSemis = 3; rig->apply (*q, "edit");
    rig->getHistory().reset (rig->project());
    rig->setStudioMode (true);
    rig->studioCache().clear();
    rig->apply ([&] { Project x = rig->project(); x.formantSemis = 0.5; return x; }(), "fresh");   // new keys: nothing rendered yet
    const auto& plan = rig->currentPlan();
    const int lat = rig->ceilingLatency();
    const int total = (int) plan.length;
    juce::AudioBuffer<float> out (2, total + lat + 512); out.clear();
    rig->setNonRealtime (true);
    rig->setCursor (0); rig->play (true);
    std::atomic<bool> done { false };
    std::thread th ([&]
    {
        juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer m;
        for (int pos = 0; pos < total + lat; pos += 512)
        {
            rig->processBlock (b, m);
            for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, b, c, 0, juce::jmin (512, out.getNumSamples() - pos));
        }
        done.store (true);
    });
    float pmin = 1.0f;
    while (! done.load()) { pump (5); pmin = std::min (pmin, rig->studioProgress()); }
    th.join();
    rig->play (false); rig->setNonRealtime (false);
    const auto ref = rig->renderArrangement();   // the STUDIO render (with the ceiling)
    float err = 0; for (int c = 0; c < 2; ++c) for (int i = 0; i < total; ++i) err = std::max (err, std::abs (out.getSample (c, i + lat) - ref.getSample (c, i)));
    check (rig->offlineWaits.load() > 0 && rig->offlineTimeouts.load() == 0 && err < 1.0e-4f,
           "offline bounce: the blocks waited for the STUDIO renders (" + juce::String (rig->offlineWaits.load()) + " waits) and the bounce equals the STUDIO render (max error " + juce::String (err, 7) + ")");
    check (pmin < 1.0f && rig->studioProgress() == 1.0f, "STUDIO progress went from " + juce::String (pmin, 2) + " to 1.0 while the renders landed");
    {   // a timeout falls back to LIVE (no endless wait) and the realtime path never waits
        Rig r2 (pad (48000, 20.0));
        auto q2 = ops::stretchTo (r2->project(), r2.r0().id, r2.r0().length() * 1.3); r2->apply (*q2, "s");
        r2->setStudioMode (true); r2->offlineWaitMs = 30;
        r2->setNonRealtime (true); r2->setCursor (0); r2->play (true);
        juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer m;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < 20; ++i) r2->processBlock (b, m);   // (no message pump: the STUDIO jobs are never requested)
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        r2->setNonRealtime (false);
        const int waitsBefore = r2->offlineWaits.load();
        for (int i = 0; i < 20; ++i) r2->processBlock (b, m);
        check (r2->offlineTimeouts.load() == 1 && ms < 1500 && r2->offlineWaits.load() == waitsBefore, "offline: a render that never comes times out once (" + juce::String (ms, 0) + " ms for 20 blocks), then plays LIVE; realtime never waits");
    }
}

static void testPolishUi (const juce::File& shots)
{
    section ("[23b] polish UI: both window sizes in every workspace, tooltips, contrast, ? overlay, mascot, header shots");
    using namespace ghosty::themes;
    for (auto t : { Theme::Dark, Theme::Purple, Theme::Light })
    {
        const auto P = gslice::ui::palette (t);
        double worst = 99;
        for (auto c : { P.slime, P.draw, P.pitch, P.marker, P.wave }) worst = std::min (worst, contrast (c, P.waveBg));
        const double sel = contrast (P.slime, P.waveBg.overlaidWith (P.slime.withAlpha (P.light ? 0.16f : 0.13f)));
        const double txt = std::min ({ contrast (P.ink, P.window), contrast (P.dim, P.panel), contrast (P.slime, P.window.withAlpha (1.0f)) });
        check (worst >= 3.0 && sel >= 2.5 && txt >= 3.0, displayName (t) + ": slime / draw / pitch / marker / wave >= 3:1 on the waveform (worst " + juce::String (worst, 2)
               + "), the selected-slice edge " + juce::String (sel, 2) + ", text and the help headers >= 3:1 (" + juce::String (txt, 2) + ")");
    }
    auto rig = analysedRig (stereoOf (voice (48000, fourNotes())), "MELODIC");
    std::unique_ptr<SliceEditor> ed (static_cast<SliceEditor*> ((*rig)->createEditor()));
    ed->setVisible (true);
    static const char* wsName[4] { "EDIT", "FLEX", "PITCH", "PLAY" };
    for (auto size : { juce::Point<int> (1160, 700), juce::Point<int> (1600, 1000) })
    {
        ed->setSize (size.x, size.y); ed->resized(); pump (10);
        for (int ws = 0; ws < 4; ++ws)
        {
            ed->setWorkspace ((gslice::ui::Workspace) ws); pump (5);
            int out = 0, overlap = 0, tight = 0, noTip = 0;
            std::vector<juce::Component*> vis;
            for (auto* c : ed->getChildren()) if (c->isVisible() && c != &ed->view && c != &ed->lane && c != &ed->playPanel && c != &ed->help && dynamic_cast<ghosty::exportgate::Hint*> (c) == nullptr
                                                  && dynamic_cast<juce::TooltipWindow*> (c) == nullptr && dynamic_cast<juce::ResizableCornerComponent*> (c) == nullptr) vis.push_back (c);
            for (auto* c : vis) if (! ed->getLocalBounds().contains (c->getBounds())) { ++out; std::printf ("    outside: %s\n", c->getComponentID().toRawUTF8()); }
            for (size_t i = 0; i < vis.size(); ++i) for (size_t j = i + 1; j < vis.size(); ++j) if (vis[i]->getBounds().intersects (vis[j]->getBounds())) { ++overlap; std::printf ("    overlap: %s / %s\n", vis[i]->getComponentID().toRawUTF8(), vis[j]->getComponentID().toRawUTF8()); }
            for (auto* c : vis) if (auto* b = dynamic_cast<juce::TextButton*> (c)) { const auto f = ed->lnf.getTextButtonFont (*b, b->getHeight()); if (ghosty::textWidth (f, b->getButtonText()) > (float) b->getWidth() - 6) { ++tight; std::printf ("    text too wide: %s\n", b->getButtonText().toRawUTF8()); } }
            std::vector<juce::Component*> ctl; collectControls (*ed, ctl);
            for (auto* c : ctl)
                if (auto* tc = dynamic_cast<juce::SettableTooltipClient*> (c); tc != nullptr && tc->getTooltip().isEmpty())
                {   // a slider inside a captioned field may carry the tip on the field's slider; the pads / keys / strip are not controls
                    ++noTip; std::printf ("    no tooltip: %s (%s)\n", c->getComponentID().toRawUTF8(), c->getParentComponent() != nullptr ? c->getParentComponent()->getComponentID().toRawUTF8() : "");
                }
            check (out == 0 && overlap == 0 && tight == 0 && noTip == 0, juce::String (size.x) + "x" + juce::String (size.y) + " " + wsName[ws] + ": " + juce::String ((int) vis.size()) + " controls inside, no overlaps, labels fit, "
                   + juce::String ((int) ctl.size()) + " controls all with tooltips");
        }
    }
    ed->setSize (1280, 780); ed->resized(); ed->setWorkspace (gslice::ui::Workspace::Edit); pump (10);
    ed->keyPressed (juce::KeyPress ('/', juce::ModifierKeys::shiftModifier, '?'));
    const bool shown = ed->help.isVisible();
    for (auto t : { Theme::Dark, Theme::Purple, Theme::Light }) { ed->setTheme (t); pump (5); snap (*ed, shots.getChildFile ("slice-" + displayName (t).toLowerCase() + "-help.png")); }
    ed->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
    check (shown && ! ed->help.isVisible() && HelpOverlay::lines().size() > 30, "? shows the shortcut list (" + juce::String (HelpOverlay::lines().size()) + " lines), Esc closes it");
    for (auto t : { Theme::Dark, Theme::Purple, Theme::Light })
    {   // the header in each look
        ed->setTheme (t); ed->refresh(); pump (5);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds().withHeight (SliceEditor::kHeader + SliceEditor::kInfo), true, 2.0f);
        const auto f = shots.getChildFile ("slice-" + displayName (t).toLowerCase() + "-header.png"); f.deleteFile();
        juce::FileOutputStream os (f); juce::PNGImageFormat png; png.writeImageToStream (img, os);
    }
    {   // the mascot sheet: 120 / 64 / 32 / 24 px on each look's window and header
        juce::Image sheet (juce::Image::ARGB, 3 * 300, 220, true);
        juce::Graphics g (sheet);
        int col = 0;
        for (auto t : { Theme::Dark, Theme::Purple, Theme::Light })
        {
            const auto P = gslice::ui::palette (t);
            const auto r = juce::Rectangle<float> ((float) col * 300.0f, 0, 300, 220);
            g.setColour (P.window); g.fillRect (r);
            g.setColour (P.header); g.fillRect (r.withTrimmedTop (160));
            gslice::ui::drawSliceGhost (g, gslice::ui::mascotColours (P), r.getX() + 80, 80, 120);
            gslice::ui::drawSliceGhost (g, gslice::ui::mascotColours (P), r.getX() + 210, 70, 64);
            gslice::ui::drawSliceGhost (g, gslice::ui::mascotColours (P), r.getX() + 60, 190, 32);
            gslice::ui::drawSliceGhost (g, gslice::ui::mascotColours (P), r.getX() + 120, 190, 24);
            ++col;
        }
        const auto f = shots.getChildFile ("slice-mascot.png"); f.deleteFile();
        juce::FileOutputStream os (f); juce::PNGImageFormat png; png.writeImageToStream (sheet.rescaled (sheet.getWidth() * 2, sheet.getHeight() * 2), os);
    }
    {
        SliceProcessor empty; empty.setPlayConfigDetails (0, 2, 48000, 512); empty.prepareToPlay (48000, 512);
        std::unique_ptr<juce::AudioProcessorEditor> ee (empty.createEditor()); ee->setVisible (true); pump (20);
        snap (*ee, shots.getChildFile ("slice-black-empty.png"));
    }
    check (shots.getChildFile ("slice-light-header.png").existsAsFile() && shots.getChildFile ("slice-mascot.png").existsAsFile(), "header shots in the three looks + the mascot sheet written");
    ed.reset(); pump (10);
}

// GhostSliceTests renders DIR: listening renders of the note edits (before / after), STUDIO quality
static void writeRenders (const juce::File& dir)
{
    section ("renders: GHOST PITCH before / after (STUDIO)");
    dir.createDirectory();
    auto save = [&] (const juce::AudioBuffer<float>& b, const juce::String& name) { juce::String err; const bool ok = exporter::writeWav (b, 48000, dir.getChildFile (name), 24, err); check (ok, name + " " + err); };
    {
        auto rig = analysedRig (stereoOf (voice (48000, fourNotes())), "MELODIC");
        save (rig->render(), "notes-1-before-C4-D4-F4-E4.wav");
        auto n = notesNow (*rig->p);
        (*rig)->apply (*ops::shiftNotes ((*rig)->project(), n[1].regionId, { n[1], n[2] }, 200, true), "e");
        save (rig->render(), "notes-2-after-C4-E4-G4-E4.wav");
        n = notesNow (*rig->p);
        (*rig)->apply (*ops::editNotes ((*rig)->project(), n[1].regionId, { n[2] }, [] (PitchEdit& e) { e.glide = true; e.transitionMs = 180; }), "g");
        save (rig->render(), "notes-3-after-glide-180ms-into-G4.wav");
        (*rig)->apply (*ops::editNotes ((*rig)->project(), n[1].regionId, { n[3] }, [] (PitchEdit& e) { e.formantCents = 400; }), "f");
        save (rig->render(), "notes-4-after-E4-formant-plus4st.wav");
    }
    {
        auto rig = analysedRig (stereoOf (voice (48000, { { 57, 2.4, 0.5, 30 } })), "MELODIC");
        save (rig->render(), "drift-1-before.wav");
        auto n = notesNow (*rig->p);
        (*rig)->apply (*ops::editNotes ((*rig)->project(), n[0].regionId, { n[0] }, [] (PitchEdit& e) { e.drift = 0; }), "d");
        save (rig->render(), "drift-2-after-DRIFT-0.wav");
    }
    {
        auto rig = analysedRig (stereoOf (voice (48000, { { 64, 1.8, 5.0, 40 } })), "MELODIC");
        save (rig->render(), "vibrato-1-before.wav");
        auto n = notesNow (*rig->p);
        (*rig)->apply (*ops::editNotes ((*rig)->project(), n[0].regionId, { n[0] }, [] (PitchEdit& e) { e.vibrato = 0; }), "v0");
        save (rig->render(), "vibrato-2-after-x0.wav");
        n = notesNow (*rig->p);
        (*rig)->apply (*ops::editNotes ((*rig)->project(), n[0].regionId, { n[0] }, [] (PitchEdit& e) { e.vibrato = 2; }), "v2");
        save (rig->render(), "vibrato-3-after-x2.wav");
    }
    {
        auto rig = analysedRig (stereoOf (voice (48000, { { 60, 1.6 } })), "MELODIC");
        save (rig->render(), "draw-1-before.wav");
        auto n = notesNow (*rig->p);
        const int64 a0 = n[0].s0 + 9600, a1 = n[0].s1 - 9600;
        (*rig)->apply (*ops::addDraw ((*rig)->project(), n[0].regionId, { { a0, 0.0 }, { a1, 1200.0 } }, false), "draw");
        save (rig->render(), "draw-2-after-ramp-plus12st.wav");
    }
    {
        const std::vector<MelNote> line { { 60, 0.45, 0, 0, true }, { 63, 0.45, 0, 0, true }, { 66, 0.45, 0, 0, true }, { 69, 0.45, 0, 0, true }, { 71, 0.5 } };
        auto rig = analysedRig (stereoOf (voice (48000, line)), "MELODIC");
        save (rig->render(), "key-1-before-C-Eb-Gb-A-B.wav");
        Project q = (*rig)->project(); q.projectKey = "C MAJ"; q.keyMode = 1; (*rig)->apply (q, "k");
        (*rig)->apply (*keyops::matchKey ((*rig)->project(), (*rig)->analysis().get()), "snap");
        save (rig->render(), "key-2-after-SCALE-SNAP-C-MAJ.wav");
    }
}
