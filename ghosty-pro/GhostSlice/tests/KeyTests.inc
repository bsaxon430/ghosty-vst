// GhostSliceTests: GHOST KEY modes (CHROMATIC / SCALE SNAP / MELODY PRESERVE / MANUAL) for MATCH PROJECT KEY. Group: keymodes
#include "../AnalysisAlign.h"

static void testKeyModes (const juce::File& shots)
{
    section ("[22] GHOST KEY modes: SCALE SNAP / MELODY PRESERVE / MANUAL / CHROMATIC, the KEY picker");
    {   // SCALE SNAP: C4 D#4 F#4 A4 B4 into C MAJ (D# and F# are out of the scale)
        const std::vector<MelNote> line { { 60, 0.45, 0, 0, true }, { 63, 0.45, 0, 0, true }, { 66, 0.45, 0, 0, true }, { 69, 0.45, 0, 0, true }, { 71, 0.5 } };
        auto rig = analysedRig (stereoOf (voice (48000, line)), "MELODIC");
        auto a = (*rig)->analysis();
        Project q = (*rig)->project(); q.projectKey = "C MAJ"; q.keyMode = (int) keyops::Mode::ScaleSnap; q.pitchSemis = 2; (*rig)->apply (q, "key");
        juce::String why; int snapped = 0;
        auto m = keyops::matchKey ((*rig)->project(), a.get(), &why, nullptr, &snapped);
        bool ok = m.has_value();
        juce::String got;
        pitchedit::Scale cmaj; cmaj.chromatic = false; cmaj.tonic = 0;
        if (ok)
        {
            (*rig)->apply (*m, "snap");
            for (auto& n : pitchedit::notesOf ((*rig)->project(), a.get())) { ok = ok && pitchedit::inScale ((int) std::lround (n.target), cmaj) && std::abs (n.target - std::round (n.target)) < 1e-9; got << juce::String (n.target, 0) << " "; }
            ok = ok && (*rig)->project().pitchSemis == 0;
            const auto t = trackOf (rig->render());
            double worst = 0;
            for (auto& n : pitchedit::notesOf ((*rig)->project(), a.get())) worst = std::max (worst, std::abs (pitchedit::centreOf (t, n.s0, n.s1) - n.target * 100.0));
            ok = ok && worst <= 15.0;
            got << "(rendered within " << juce::String (worst, 1) << " ct)";
        }
        check (ok && snapped == 5, "SCALE SNAP into C MAJ: every note on the scale: " + got + " " + why);
    }
    {   // MELODY PRESERVE vs CHROMATIC: A minor material into C major
        const std::vector<MelNote> line { { 69, 0.4, 0, 0, true }, { 72, 0.4, 0, 0, true }, { 76, 0.4, 0, 0, true }, { 74, 0.4, 0, 0, true }, { 71, 0.5 } };
        auto rig = analysedRig (stereoOf (voice (48000, line)), "MELODIC");
        auto a = (*rig)->analysis();
        Project q = (*rig)->project(); q.key = "A MIN"; q.projectKey = "C MAJ"; q.keyMode = (int) keyops::Mode::Chromatic; (*rig)->apply (q, "keys");
        juce::String why; int sh = 99;
        auto c = keyops::matchKey ((*rig)->project(), a.get(), &why, &sh);
        const bool chromaticNoop = ! c.has_value() && sh == 0;
        q = (*rig)->project(); q.keyMode = (int) keyops::Mode::MelodyPreserve; (*rig)->apply (q, "mode");
        const auto before = rig->render();
        auto m = keyops::matchKey ((*rig)->project(), a.get(), &why, &sh);
        bool ok = m.has_value() && sh == 3 && m->pitchSemis == 3 && m->regions[0].pitch == (*rig)->project().regions[0].pitch;
        double worst = 0;
        if (ok)
        {
            (*rig)->apply (*m, "melody");
            const auto t0 = trackOf (before), t1 = trackOf (rig->render());
            const auto ns = pitchedit::notesOf ((*rig)->project(), a.get());
            for (size_t i = 1; i < ns.size(); ++i)
            {
                const double i0 = pitchedit::centreOf (t0, ns[i].s0, ns[i].s1) - pitchedit::centreOf (t0, ns[0].s0, ns[0].s1);
                const double i1 = pitchedit::centreOf (t1, ns[i].s0, ns[i].s1) - pitchedit::centreOf (t1, ns[0].s0, ns[0].s1);
                worst = std::max (worst, std::abs (i1 - i0));
            }
            ok = ns.size() == 5 && worst <= 5.0;
        }
        check (chromaticNoop && ok, "MELODY PRESERVE A MIN -> C MAJ: +3 st tonic to tonic, every interval kept (rendered within " + juce::String (worst, 2)
               + " ct; the model shifts all notes by one global step); CHROMATIC sees the relative key and leaves it");
        q = (*rig)->project(); q.keyMode = (int) keyops::Mode::Manual; (*rig)->apply (q, "manual");
        auto mm = keyops::matchKey ((*rig)->project(), a.get(), &why);
        check (! mm.has_value() && why.contains ("MANUAL"), "MANUAL: MATCH KEY does nothing automatic (" + why + ")");
        // the editor: KEY MODE box, MATCH KEY through it, the readout shows the target
        std::unique_ptr<SliceEditor> ed (static_cast<SliceEditor*> ((*rig)->createEditor()));
        ed->setVisible (true); ed->setSize (1280, 780); ed->resized(); ed->setWorkspace (gslice::ui::Workspace::Pitch); ed->view.zoomToFit(); pump (20);
        check (! ed->matchKeyBtn.isEnabled() && ed->keyModeBox.getSelectedId() == 4, "the KEY MODE box shows MANUAL and MATCH KEY is off");
        ed->keyModeBox.setSelectedId (2, juce::sendNotificationSync); pump (5);
        const bool snapOk = (*rig)->project().keyMode == 1 && ed->matchProjectKey();
        int inScale = 0, total = 0; pitchedit::Scale cmaj; cmaj.chromatic = false;
        for (auto& n : pitchedit::notesOf ((*rig)->project(), a.get())) { ++total; inScale += pitchedit::inScale ((int) std::lround (n.target), cmaj) ? 1 : 0; }
        check (snapOk && inScale == total && ed->keySub().contains ("C MAJ"), "PITCH tab: KEY MODE SCALE SNAP + MATCH KEY = every note in C MAJ; the KEY readout says '" + ed->keySub() + "'");
        for (auto t : { ghosty::themes::Theme::Dark, ghosty::themes::Theme::Purple, ghosty::themes::Theme::Light })
        {
            ed->setTheme (t); ed->refresh(); pump (10);
            snap (*ed, shots.getChildFile ("slice-" + ghosty::themes::displayName (t).toLowerCase() + "-key-scalesnap.png"));
        }
        (*rig)->undo(); ed->keyModeBox.setSelectedId (3, juce::sendNotificationSync); ed->matchProjectKey(); ed->setTheme (ghosty::themes::Theme::Dark); ed->refresh(); pump (10);
        snap (*ed, shots.getChildFile ("slice-black-key-melody.png"));
        ed.reset(); pump (10);
    }
    {
        Project p = makeProject ({ "abc", "", "", "a.wav", 48000, 96000, 2 }); p.keyMode = 2;
        Project back; juce::String err;
        check (json::fromString (json::toString (p), back, err) && back.keyMode == 2, "KEY MODE round trips through JSON");
    }
}
