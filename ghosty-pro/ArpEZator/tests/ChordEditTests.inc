// ArpEZatorTests: EDITED CHORDS (the CHORDS roll edits notes like MELODY: drag with SNAP KEY, move on the grid, add / delete, keys,
// the note menu, locks; rebuilds replace the edits but keep locked notes; persistence, MIDI, undo). --chordedit <shot dir> runs it alone.
namespace chordedit {
using chordoct::sameNotes; using chordoct::others;
static bool onKey (const Idea& I, int p, int t) { return std::find (I.H.keyPcs.begin(), I.H.keyPcs.end(), mod12 (p)) != I.H.keyPcs.end() || I.H.chordTone (p, t); }
static juce::Point<float> at (arpui::Roll& r, const Note& n) { return { juce::jmin (r.X (n.t) + 3.0f, r.X (n.t + n.d) - 1.0f), r.Y (n.p) + r.rh() * 0.5f }; }
} // namespace chordedit

void chordEditChecks (Counter& C, const juce::String& shotDir)
{
    using namespace chordedit; using A = ArpEditorTestAccess;
    std::printf ("== EDITED CHORDS\n");
    const juce::ModifierKeys left (juce::ModifierKeys::leftButtonModifier), none;
    ArpProcessor p; p.persistTaste = false; p.hintsSeen = true;
    std::unique_ptr<ArpEditor> ed (dynamic_cast<ArpEditor*> (p.createEditor())); ed->setSize (1240, 820);
    auto& roll = ed->roll;
    auto prep = [&] (int mode, uint32_t seed) {
        p.change ([mode, seed] (Idea& X) { X.mode = mode; X.seed = seed; applyModeDefaults (X); newIdea (X); for (auto& L : X.lanes) L.mute = false; }, false);
        ed->selectLane (LaneChords); ed->refresh(); roll.clearSel(); roll.pitchManual = false; roll.frozen = false; roll.setView ({ 0, 4.0 * BAR }); roll.fitRows (chordDisplayNotes (p.idea())); };
    auto drag = [&] (juce::Point<float> a, juce::Point<float> b, juce::ModifierKeys m = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier)) {
        roll.mouseDown (mev (roll, a, a, m)); roll.mouseDrag (mev (roll, a + (b - a) * 0.5f, a, m, true)); roll.mouseDrag (mev (roll, b, a, m, true)); roll.mouseUp (mev (roll, b, a, juce::ModifierKeys(), true)); };
    auto click = [&] (juce::Point<float> a, juce::ModifierKeys m = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier)) { roll.mouseDown (mev (roll, a, a, m)); roll.mouseUp (mev (roll, a, a, juce::ModifierKeys())); };
    auto selIdx = [&] { for (size_t i = 0; i < roll.sel.size(); ++i) if (roll.sel[i]) return (int) i; return -1; };
    {   // drag a chord note up / down with SNAP KEY in every mood (several seeds = several keys / scales): always on the key (or the chord),
        // a chord tone may land on a plain scale tone (not forced to chord tones); time moves land on the grid; the other lanes never change
        int moves = 0, onScale = 0, scaleOnly = 0, grid = 0, otherOk = 0, moved = 0; std::set<std::string> keys;
        for (int m = 0; m < kMoods; ++m)
            for (uint32_t s : { 3u, 41u, 77u, 150u, 999u, 4321u, 8080u, 12345u })
            {
                prep (m, s); const Idea before = p.idea(); keys.insert (std::to_string (before.H.key) + " " + before.H.scaleName);
                auto ns = chordDisplayNotes (p.idea()); if (ns.empty()) continue;
                const int i = (int) ((s * 7) % (uint32_t) juce::jmin ((int) ns.size(), 8)); const Note n0 = ns[(size_t) i];
                const int rows = (int) (s % 5) + 1, dir = (s % 2) ? 1 : -1; const float dx = (s % 3) == 0 ? 37.0f : 0.0f;
                const auto a = at (roll, n0); drag (a, a + juce::Point<float> (dx, -dir * rows * roll.rh()));
                const int k = selIdx(); ns = chordDisplayNotes (p.idea()); ++moves;
                if (k < 0 || k >= (int) ns.size()) { std::printf ("    no selection after the drag (mood %d seed %u)\n", m, s); continue; }
                const Note n1 = ns[(size_t) k];
                if (n1.p != n0.p || n1.t != n0.t) ++moved;
                if (onKey (p.idea(), n1.p, n1.t)) ++onScale; else std::printf ("    off the key: mood %d seed %u pitch %d\n", m, s, n1.p);
                if (! p.idea().H.chordTone (n1.p, n1.t)) ++scaleOnly;
                if (n1.t % (BAR / 16) == 0) ++grid;
                if (others (before, p.idea()) && chordsEdited (p.idea())) ++otherOk;
            }
        std::printf ("  %d drags (%zu keys / scales): %d moved, %d on the key, %d on a plain scale tone, %d on the grid, %d other lanes identical\n", moves, keys.size(), moved, onScale, scaleOnly, grid, otherOk);
        C.ok (moves == 8 * kMoods && moved == moves && onScale == moves && scaleOnly > 0, "EDITED CHORDS: dragged chord notes snap to the key / scale as MELODY (every mood, " + std::to_string (keys.size()) + " keys / scales; not forced to chord tones)");
        C.ok (grid == moves, "EDITED CHORDS: a note moved in time lands on the grid");
        C.ok (otherOk == moves, "EDITED CHORDS: ARP / MELODY / BASS bit-identical after chord edits");
    }
    prep (Dusk, 51);
    const Idea base = p.idea(); const auto s0 = p.sessionString();
    std::vector<juce::String> states { s0 };
    {   // add (click empty space), delete (double-click, DELETE key), transpose keys, note menu, locks
        auto ns = chordDisplayNotes (p.idea()); const size_t n0 = ns.size();
        // an empty spot: a row two above the highest note of the first chord, a beat in
        int top = 0; for (auto& n : ns) if (n.t < PPQ) top = std::max (top, n.p);
        const auto spot = juce::Point<float> (roll.X (PPQ + 4), roll.Y (top + 2) + roll.rh() * 0.5f);
        click (spot); states.push_back (p.sessionString());
        ns = chordDisplayNotes (p.idea()); bool added = ns.size() == n0 + 1; int addP = -1;
        for (auto& n : ns) if (n.t == PPQ && n.p >= top + 1) addP = n.p;
        added &= addP > 0 && onKey (p.idea(), addP, PPQ) && chordsEdited (p.idea());
        C.ok (added && others (base, p.idea()), "EDITED CHORDS: a click on empty space adds a chord note (on the grid, snapped to the key), the other chord notes stay");
        // double-click deletes
        int ai = -1; for (size_t i = 0; i < ns.size(); ++i) if (ns[i].t == PPQ && ns[i].p == addP) ai = (int) i;
        roll.mouseDoubleClick (mev (roll, at (roll, ns[(size_t) ai]), at (roll, ns[(size_t) ai]), left)); states.push_back (p.sessionString());
        ns = chordDisplayNotes (p.idea()); const bool dbl = ns.size() == n0;
        // select + UP / SHIFT+UP / DOWN; DELETE removes the selection
        const Note t0 = ns[2]; click (at (roll, t0)); const int k = selIdx();
        roll.keyPressed (juce::KeyPress (juce::KeyPress::upKey)); states.push_back (p.sessionString());
        const int up1 = chordDisplayNotes (p.idea())[(size_t) k].p;
        roll.keyPressed (juce::KeyPress (juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier, 0)); states.push_back (p.sessionString());
        const int up13 = chordDisplayNotes (p.idea())[(size_t) k].p;
        roll.keyPressed (juce::KeyPress (juce::KeyPress::downKey, juce::ModifierKeys::shiftModifier, 0)); roll.keyPressed (juce::KeyPress (juce::KeyPress::downKey)); states.push_back (p.sessionString());
        const int back = chordDisplayNotes (p.idea())[(size_t) k].p;
        C.ok (dbl, "EDITED CHORDS: double-click deletes a chord note");
        C.ok (k == 2 && up1 == t0.p + 1 && up13 == t0.p + 13 && back == t0.p, "EDITED CHORDS: UP / DOWN = +-1 semitone, SHIFT = an octave on the selected chord notes");
        roll.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey)); states.push_back (p.sessionString());
        C.ok (chordDisplayNotes (p.idea()).size() == n0 - 1, "EDITED CHORDS: DELETE removes the selected chord notes");
        // the note menu: louder / softer / longer / shorter / octave up / down / lock / unlock all
        ns = chordDisplayNotes (p.idea()); const Note m0 = ns[1];
        roll.noteAction (1, 3); const bool louder = std::abs (chordDisplayNotes (p.idea())[1].v - juce::jlimit (0.1f, 1.0f, m0.v + 0.15f)) < 1e-6f;
        roll.noteAction (1, 4); const bool softer = std::abs (chordDisplayNotes (p.idea())[1].v - m0.v) < 1e-5f;
        roll.noteAction (1, 5); const bool longer = chordDisplayNotes (p.idea())[1].d == (int) std::lround (m0.d * 1.5);
        roll.noteAction (1, 6); const int dShort = chordDisplayNotes (p.idea())[1].d;
        roll.noteAction (1, 7); const bool octUp = chordDisplayNotes (p.idea())[1].p == juce::jmin (108, m0.p + 12);
        roll.noteAction (1, 8); const bool octDn = chordDisplayNotes (p.idea())[1].p == m0.p;
        roll.noteAction (1, 1); const bool locked = chordDisplayNotes (p.idea())[1].lock;
        roll.noteAction (1, 9); const bool unl = ! chordDisplayNotes (p.idea())[1].lock;
        states.push_back (p.sessionString());
        std::printf ("  menu: louder %d softer %d longer %d shorter %d (%d) oct up %d down %d lock %d unlock all %d\n", (int) louder, (int) softer, (int) longer, (int) (dShort < (int) std::lround (m0.d * 1.5)), dShort, (int) octUp, (int) octDn, (int) locked, (int) unl);
        C.ok (louder && softer && longer && dShort < (int) std::lround (m0.d * 1.5) && octUp && octDn, "EDITED CHORDS: note menu LOUDER / SOFTER / LONGER / SHORTER / OCTAVE UP / DOWN");
        // LOCK SEL toggles the selected chord notes
        click (at (roll, chordDisplayNotes (p.idea())[0])); A::lockSel (*ed).onClick(); const bool l1 = chordDisplayNotes (p.idea())[0].lock;
        click (at (roll, chordDisplayNotes (p.idea())[0])); A::lockSel (*ed).onClick(); const bool l2 = ! chordDisplayNotes (p.idea())[0].lock;
        C.ok (locked && unl && l1 && l2 && A::lockSel (*ed).isVisible(), "EDITED CHORDS: LOCK (menu / LOCK SEL) and UNLOCK ALL on chord notes");
        C.ok (others (base, p.idea()), "EDITED CHORDS: still ARP / MELODY / BASS bit-identical after add / delete / keys / menu / locks");
        states.push_back (p.sessionString());
    }
    {   // undo every step back to the start (byte-identical), redo back to the end
        const auto end = p.sessionString(); bool ok = true; int n = 0;
        while (p.sessionString() != s0 && p.canUndo() && n < 60) { p.undo(); ++n; }
        ok &= p.sessionString() == s0 && ! chordsEdited (p.idea());
        int r = 0; while (p.canRedo() && r < 60) { p.redo(); ++r; }
        ok &= p.sessionString() == end && r == n;
        C.ok (ok, "EDITED CHORDS: undo walks back to the untouched chords (byte-identical), redo to the last edit (" + std::to_string (n) + " steps)");
    }
    {   // what keeps the edits: other lanes' tools / rate / GENERATE of a lane, OCT +- (on top of the edits), a longer idea (EVOLVE)
        const auto keep = p.idea().lanes[LaneChords].notes;
        int kept = 0, n = 0; auto same = [&] (const char* what) { ++n; const bool ok = chordsEdited (p.idea()) && sameNotes (p.idea().lanes[LaneChords].notes, keep); kept += ok; if (! ok) std::printf ("    edits lost: %s\n", what); };
        p.laneTool (LaneMelody, "varyLittle", nullptr); same ("melody vary");
        p.laneTool (LaneArp, "new", nullptr); same ("arp new");
        p.laneTool (LaneBass, "new", nullptr); same ("bass new");
        A::arpRate (*ed).onChange (2); same ("arp rate");
        const auto c0 = chordLaneNotes (p.idea());
        ed->chordOctUp.onClick(); same ("oct +");
        const bool octTop = chordoct::shifted (c0, chordLaneNotes (p.idea()), 12);
        ed->chordOctDn.onClick(); same ("oct -");
        C.ok (kept == n && octTop, "EDITED CHORDS kept by lane tools (VARY / NEW on ARP, MELODY, BASS), ARP RATE; OCT + moves the edited notes +12 (" + std::to_string (kept) + "/" + std::to_string (n) + ")");
        const int t0 = p.idea().total;
        p.change ([] (Idea& X) { X.evolve = true; X.evolveBars = 16; rebuildChords (X); });
        bool longer = chordsEdited (p.idea()) && p.idea().total > t0;
        for (auto& x : keep) { bool found = false; for (auto& y : p.idea().lanes[LaneChords].notes) if (y.t == x.t + t0 && y.p == x.p) found = true; longer &= found || x.t + t0 >= p.idea().total; }
        p.undo();
        C.ok (longer, "EDITED CHORDS: a longer idea (EVOLVE 16 bars) repeats the edited chords");
    }
    {   // what rebuilds the chords: the edits are replaced (toast), locked notes kept where possible (toast)
        const auto edits = p.idea().lanes[LaneChords].notes;
        ed->refresh(); ed->toast ("", -1.0e15);
        A::voicing (*ed).onChange ((p.idea().voicing + 1) % 4); ed->refresh();
        const bool replaced = ! chordsEdited (p.idea()) && ed->toastShown() == "CHORDS rebuilt - your edited notes were replaced";
        p.undo(); ed->refresh(); ed->toast ("", -1.0e15);
        const bool back = sameNotes (p.idea().lanes[LaneChords].notes, edits);
        roll.noteAction (0, 1); const Note lk = p.idea().lanes[LaneChords].notes[0];
        A::voicing (*ed).onChange ((p.idea().voicing + 2) % 4); ed->refresh();   // (NEW CHORDS can pick the same loop again)
        bool keptLock = chordsEdited (p.idea()); bool has = false; for (auto& x : p.idea().lanes[LaneChords].notes) if (x.t == lk.t && x.p == lk.p && x.lock) has = true;
        Idea fresh = p.idea(); fresh.lanes[LaneChords].notes.clear(); fresh.lanes[LaneChords].edited = false; const auto gen = chordLaneNotes (fresh);
        int fromNew = 0; for (auto& x : p.idea().lanes[LaneChords].notes) if (! x.lock) for (auto& g : gen) if (g.t == x.t && g.p == x.p) { ++fromNew; break; }
        keptLock &= has && fromNew > 0 && ed->toastShown() == "CHORDS rebuilt - kept locked notes";
        std::printf ("  rebuilds: VOICING replaced %d, undo back %d, VOICING kept the locked note %d (%d new chord notes)\n", (int) replaced, (int) back, (int) keptLock, fromNew);
        C.ok (replaced && back, "EDITED CHORDS: VOICING rebuilds the chords: edits replaced + the toast; undo brings them back");
        C.ok (keptLock, "EDITED CHORDS: a rebuild (VOICING) keeps the locked chord note, the new chords fill the rest + the toast");
        // the others that rewrite: SIMPLE / RICH, CHORD LENGTH, RHYTHM, tap a chord, GENERATE (no locks: replaced)
        int rw = 0, n = 0;
        auto rewrites = [&] (const char* what, std::function<void()> fn) { p.change ([] (Idea& X) { unlockAll (X, LaneChords); materializeChords (X); X.lanes[LaneChords].notes[0].p += 1; }); ed->refresh(); ed->toast ("", -1.0e15);
            fn(); ed->refresh(); ++n; const bool ok = ! chordsEdited (p.idea()) && ed->toastShown().contains ("replaced"); rw += ok; if (! ok) std::printf ("    not rebuilt: %s\n", what); };
        rewrites ("rich", [&] { p.change ([] (Idea& X) { setSimple (X, ! X.simple); }); });
        rewrites ("chord length", [&] { A::chordLen (*ed).onChange (p.idea().chordBars == 1 ? 1 : 0); });
        rewrites ("rhythm", [&] { A::chordRhythm (*ed).onChange ((p.idea().chordRhythm + 1) % 3); });
        rewrites ("tap a chord", [&] { p.change ([] (Idea& X) { nextAlt (X, 1); }); });
        p.change ([] (Idea& X) { setSimple (X, false); });
        rewrites ("new chords (RICH)", [&] { const auto sig = p.idea().chordSig; for (int k = 0; k < 8 && p.idea().chordSig == sig; ++k) p.laneTool (LaneChords, "newChords", nullptr); });   // (SIMPLE: a mood may have one classic loop)
        rewrites ("generate", [&] { p.generate (p.idea().mode, true); });
        C.ok (rw == n, "EDITED CHORDS replaced (with the toast) by SIMPLE/RICH, CHORD LENGTH, RHYTHM, tap a chord, NEW CHORDS, GENERATE (" + std::to_string (rw) + "/" + std::to_string (n) + ")");
    }
    {   // persistence: plugin state + SAVE / LOAD files; MIDI drag-out + live MIDI carry the edits; old (unedited) sessions unchanged
        prep (Midnight, 2024);
        const auto unedited = juce::JSON::toString (ArpProcessor::ideaToVar (p.idea()), true); const auto gen0 = chordLaneNotes (p.idea());
        Idea re; ArpProcessor::ideaFromVar (ArpProcessor::ideaToVar (p.idea()), re);
        const bool oldSame = juce::JSON::toString (ArpProcessor::ideaToVar (re), true) == unedited && ! chordsEdited (re) && chordoct::shifted (gen0, chordLaneNotes (re), 0);
        auto ns = chordDisplayNotes (p.idea()); const auto a = at (roll, ns[1]); drag (a, a + juce::Point<float> (0, -3 * roll.rh()));
        ed->chordOctUp.onClick();
        const auto c1 = chordLaneNotes (p.idea());
        juce::MemoryBlock mb; p.getStateInformation (mb); ArpProcessor q; q.persistTaste = false; q.setStateInformation (mb.getData(), (int) mb.getSize());
        const bool rt = chordsEdited (q.idea()) && juce::JSON::toString (ArpProcessor::ideaToVar (q.idea()), true) == juce::JSON::toString (ArpProcessor::ideaToVar (p.idea()), true) && chordoct::shifted (c1, chordLaneNotes (q.idea()), 0);
        C.ok (oldSame && unedited.contains ("\"edited\": false"), "EDITED CHORDS: an unedited idea saves / loads exactly as before (no new keys, the generated chords)");
        C.ok (rt, "EDITED CHORDS: edits (+ the octave) saved + restored in the plugin state, identical");
        std::vector<int> want; for (auto& n : c1) want.push_back (n.p);
        const auto file = chordoct::midiPitches (ideaMidi (p.idea(), 1 << LaneChords, false, 124));
        std::vector<int> w2 = want; { std::vector<std::pair<int, int>> tp; for (auto& n : c1) tp.push_back ({ n.t, n.p }); std::sort (tp.begin(), tp.end()); w2.clear(); for (auto& x : tp) w2.push_back (x.second); }
        bool dif = false; for (size_t i = 0; i < c1.size() && i < gen0.size(); ++i) dif |= c1[i].p != gen0[i].p + 12;
        C.ok (file == w2 && dif, "EDITED CHORDS: the MIDI drag-out has the edited chord notes");
        setP (p, "midiCh1", 0); setP (p, "kick", 0);
        std::vector<MidiEv> ev; int byp = 0; RenderOpts o; o.bars = 2; render (p, o, ev, byp); setP (p, "midiCh1", 1);
        std::multiset<int> live, exp; for (auto& e : ev) if (e.on && e.ch == 3) live.insert (e.pitch);
        for (auto& n : c1) if (n.t < 2 * BAR) exp.insert (n.p);
        bool sub = ! exp.empty(); for (int x : exp) sub &= live.count (x) >= exp.count (x);
        C.ok (sub, "EDITED CHORDS: live MIDI out (= what the preview plays) has the edited chord notes");
    }
    {   // screenshots: an edited chord lane with a selection in BLACK, PURPLE, LIGHT, GHOST; readable
        prep (Dusk, 51);
        auto ns = chordDisplayNotes (p.idea()); const auto a = at (roll, ns[2]); drag (a, a + juce::Point<float> (24, -2 * roll.rh()));
        roll.noteAction (0, 1);
        const juce::File dir (shotDir); if (shotDir.isNotEmpty()) dir.createDirectory();
        const juce::String looks[] { "dark", "purple", "light", "ghost" }; int runs = 0, bad = 0;
        for (auto& lk : looks)
        {
            p.theme = lk; ed.reset (dynamic_cast<ArpEditor*> (p.createEditor())); ed->setSize (1240, 820); ed->applyThemeNow (p.idea().mode); ed->selectLane (LaneChords); ed->refresh(); ed->toast ("", -1.0e15);
            auto& r2 = ed->roll; r2.setView ({ 0, 4.0 * BAR }); r2.fitRows (chordDisplayNotes (p.idea()));
            r2.sel.assign (chordDisplayNotes (p.idea()).size(), false); for (size_t i = 0; i < r2.sel.size(); ++i) r2.sel[i] = chordDisplayNotes (p.idea())[i].t < BAR && i % 2 == 0;
            ed->refresh();
            const auto R = probeContrast (*ed, "chordedit/" + lk, nullptr, {}, { ed->editorCardArea() }); runs += R.runs; bad += R.bad;
            if (R.bad) std::printf ("    %s: %d low-contrast runs, worst %s\n", lk.toRawUTF8(), R.bad, R.worstAt.toRawUTF8());
            if (shotDir.isNotEmpty()) { auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f); juce::File f = dir.getChildFile ("chordedit-" + lk + ".png"); f.deleteFile(); juce::FileOutputStream os (f); juce::PNGImageFormat png; png.writeImageToStream (img, os); }
        }
        C.ok (runs > 120 && bad == 0, "EDITED CHORDS: the edited CHORDS lane (selection, LOCK SEL / CLEAR SEL) in BLACK, PURPLE, LIGHT, GHOST: every text run >= 4.5:1");
        p.theme = "dark";
    }
}
