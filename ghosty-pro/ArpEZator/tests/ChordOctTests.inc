// ArpEZatorTests: CHORDS OCT - / OCT + (the whole chord lane an octave down / up; ARP / MELODY / BASS untouched). --chordoct <shot dir> runs it alone.
namespace chordoct {
static bool sameNotes (const std::vector<Note>& a, const std::vector<Note>& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        const auto& x = a[i]; const auto& y = b[i];
        if (x.t != y.t || x.d != y.d || x.p != y.p || x.v != y.v || x.lock != y.lock || x.acc != y.acc || x.l.type != y.l.type || x.l.w != y.l.w || x.l.f != y.l.f) return false;
    }
    return true;
}
static bool others (const Idea& a, const Idea& b) { for (int l = LaneArp; l < kLanes; ++l) if (! sameNotes (a.lanes[l].notes, b.lanes[l].notes)) return false; return true; }
static bool shifted (const std::vector<MidiNote>& base, const std::vector<MidiNote>& now, int semis)
{
    if (base.size() != now.size() || base.empty()) return false;
    for (size_t i = 0; i < base.size(); ++i) if (now[i].t != base[i].t || now[i].d != base[i].d || now[i].v != base[i].v || now[i].p != base[i].p + semis) return false;
    return true;
}
static std::vector<MidiNote> unshifted (Idea I) { I.chordOct = 0; rebuildChords (I); return chordLaneNotes (I); }   // what the chords are at octave 0
static std::vector<int> midiPitches (const std::vector<uint8_t>& bytes)
{
    std::vector<std::pair<double, int>> v; juce::MemoryInputStream ms (bytes.data(), bytes.size(), false); juce::MidiFile mf; if (! mf.readFrom (ms)) return {};
    for (int t = 0; t < mf.getNumTracks(); ++t) for (auto* e : *mf.getTrack (t)) if (e->message.isNoteOn()) v.push_back ({ e->message.getTimeStamp(), e->message.getNoteNumber() });
    std::sort (v.begin(), v.end()); std::vector<int> out; for (auto& x : v) out.push_back (x.second); return out;
}
} // namespace chordoct

void chordOctChecks (Counter& C, const juce::String& shotDir)
{
    using namespace chordoct;
    std::printf ("== CHORDS OCT - / OCT +\n");
    {   // every mood, several seeds: +1 / -1 move every chord note by exactly 12; the other lanes, names, bass notes bit-identical
        int ideas = 0, good = 0, other = 0, names = 0;
        for (int m = 0; m < kMoods; ++m)
            for (uint32_t s : { 11u, 222u, 3333u })
            {
                Idea I; I.mode = m; I.seed = s; applyModeDefaults (I); newIdea (I); ++ideas;
                const Idea base = I; const auto c0 = chordLaneNotes (I);
                for (int d : { 1, -1 })
                {
                    Idea X = base; const bool full = setChordOct (X, d);
                    if (full && X.chordOct == d && X.chordOctUsed == d && shifted (c0, chordLaneNotes (X), 12 * d)) ++good;
                    if (others (base, X)) ++other;
                    bool nm = X.H.chords.size() == base.H.chords.size();
                    for (size_t k = 0; nm && k < X.H.chords.size(); ++k) nm = X.H.chords[k].name == base.H.chords[k].name && X.H.chords[k].bassNote == base.H.chords[k].bassNote && X.H.chords[k].pcs == base.H.chords[k].pcs;
                    if (nm && X.chords.size() == base.chords.size()) ++names;
                }
            }
        std::printf ("  %d ideas x (+1, -1): %d shifted exactly, %d other lanes identical, %d names / bass / tones identical\n", ideas, good, other, names);
        C.ok (good == 2 * ideas, "CHORDS OCT: +1 / -1 move every chord note by exactly 12 (time, length, velocity kept) in every mood");
        C.ok (other == 2 * ideas && names == 2 * ideas, "CHORDS OCT: ARP / MELODY / BASS notes bit-identical; chord names, bass notes, chord tones unchanged");
    }
    {   // the other lanes never read the chord voicings: composing them on a +2 harmony gives the same notes
        Idea I; I.mode = Sunrise; I.seed = 404; applyModeDefaults (I); newIdea (I);
        Idea X = I; setChordOct (X, 2); for (int l = LaneArp; l < kLanes; ++l) composeLane (X, l, false); refitLanesToChords (X);
        Idea Y = I; for (int l = LaneArp; l < kLanes; ++l) composeLane (Y, l, false); refitLanesToChords (Y);
        C.ok (others (X, Y), "CHORDS OCT: composing / re-fitting ARP, MELODY, BASS on the moved chords = the same notes (they follow the progression, not the chord lane's pitches)");
    }
    {   // limits: +-2, MIDI 24..108 (as far as fits, then stops)
        Idea I; I.mode = Dusk; I.seed = 77; applyModeDefaults (I); newIdea (I);
        Idea X = I; const bool a = setChordOct (X, 2); const bool b = ! setChordOct (X, 3) && X.chordOct == 2;
        int hi = 0, lo = 999; for (auto& n : chordLaneNotes (X)) { hi = std::max (hi, n.p); lo = std::min (lo, n.p); }
        Idea Y = I; Y.keyShift = 24; rebuildChords (Y); const bool c = ! setChordOct (Y, 2); int hiY = 0; for (auto& n : chordLaneNotes (Y)) hiY = std::max (hiY, n.p);
        C.ok (a && b && hi <= 108 && lo >= 24 && c && Y.chordOct < 2 && hiY <= 108, "CHORDS OCT: clamped at +-2 and inside MIDI 24..108 (stops where it fits)");
    }
    ArpProcessor p; p.persistTaste = false; p.hintsSeen = true;
    p.change ([] (Idea& X) { X.mode = Midnight; X.seed = 515; applyModeDefaults (X); newIdea (X); for (auto& L : X.lanes) L.mute = false; }, false);
    std::unique_ptr<ArpEditor> ed (dynamic_cast<ArpEditor*> (p.createEditor())); ed->setSize (1240, 820); ed->selectLane (LaneChords); ed->refresh();
    {   // the buttons: OCT + three times = +2 then a toast; OCT - five times = -2 then a toast; readout; undo / redo
        const Idea base = p.idea(); const auto c0 = chordLaneNotes (base); const auto s0 = p.sessionString();
        ed->chordOctUp.onClick(); const bool one = shifted (c0, chordLaneNotes (p.idea()), 12) && others (base, p.idea()) && ed->chordOctLbl.getText() == "+1";
        const auto s1 = p.sessionString();
        p.undo(); const bool undone = p.sessionString() == s0 && p.idea().chordOct == 0 && shifted (c0, chordLaneNotes (p.idea()), 0);
        p.redo(); const bool redone = p.sessionString() == s1 && p.idea().chordOct == 1;
        ed->chordOctUp.onClick(); ed->chordOctUp.onClick(); const bool top = p.idea().chordOct == 2 && ed->toastShown().contains ("highest octave") && ed->chordOctLbl.getText() == "+2";
        for (int k = 0; k < 5; ++k) ed->chordOctDn.onClick();
        const bool bottom = p.idea().chordOct == -2 && ed->toastShown().contains ("lowest octave") && shifted (c0, chordLaneNotes (p.idea()), -24) && others (base, p.idea()) && ed->chordOctLbl.getText() == "-2";
        std::printf ("  buttons: +1 %d, undo %d, redo %d, +2 limit %d, -2 limit %d ('%s')\n", (int) one, (int) undone, (int) redone, (int) top, (int) bottom, ed->toastShown().toRawUTF8());
        C.ok (one && ed->chordOctUp.isVisible() && ed->chordOctDn.isVisible() && ed->chordOctLbl.isVisible(), "CHORDS OCT +: every chord note +12, the other lanes identical, readout '+1'");
        C.ok (undone && redone, "CHORDS OCT: one undo step (undo = byte-identical session, redo = back)");
        C.ok (top && bottom, "CHORDS OCT: stops at +2 / -2 with a toast ('CHORDS: highest / lowest octave')");
        C.ok (ed->chordOctDn.getTooltip() == "CHORDS OCTAVE DOWN: every chord an octave lower (the ARP, MELODY and BASS stay as they are)"
              && ed->chordOctUp.getTooltip() == "CHORDS OCTAVE UP: every chord an octave higher (the ARP, MELODY and BASS stay as they are)", "CHORDS OCT: tooltips");
        bool hidden = true; for (int l = LaneArp; l < kLanes; ++l) { ed->selectLane (l); hidden &= ! ed->chordOctUp.isVisible() && ! ed->chordOctDn.isVisible() && ! ed->chordOctLbl.isVisible(); }
        ed->selectLane (LaneChords);
        C.ok (hidden, "CHORDS OCT: only on the CHORDS lane");
    }
    {   // everything that rebuilds the chords keeps the octave (VOICING, SIMPLE / RICH, NEW CHORDS, CHORD LENGTH, tap a chord, BAKE, GENERATE same mood); a new mood resets it (as VOICING)
        p.change ([] (Idea& X) { setChordOct (X, 1); }, false);
        int kept = 0, n = 0;
        auto check = [&] (const char* what, std::function<void()> fn) { fn(); ++n; const bool ok = p.idea().chordOct == 1 && shifted (unshifted (p.idea()), chordLaneNotes (p.idea()), 12); kept += ok; if (! ok) std::printf ("    lost the octave: %s\n", what); };
        check ("voicing", [&] { p.change ([] (Idea& X) { X.voicing = 2; rebuildChords (X); refitLanesToChords (X); }); });
        check ("rich", [&] { p.change ([] (Idea& X) { setSimple (X, ! X.simple); }); });
        check ("new chords", [&] { p.laneTool (LaneChords, "newChords", nullptr); });
        check ("chord length", [&] { p.change ([] (Idea& X) { X.chordBars = X.chordBars == 1 ? 2 : 1; rebuildChords (X); for (int l = LaneArp; l < kLanes; ++l) composeLane (X, l, true); }); });
        check ("tap a chord", [&] { p.change ([] (Idea& X) { nextAlt (X, 0); }); });
        check ("bake", [&] { p.change ([] (Idea& X) { bakeTranspose (X, 3, 15); }); });
        check ("generate", [&] { p.generate (p.idea().mode, true); });
        check ("melody tool", [&] { p.laneTool (LaneMelody, "varyLittle", nullptr); });
        C.ok (kept == n, "CHORDS OCT kept by VOICING, SIMPLE/RICH, NEW CHORDS, CHORD LENGTH, tap a chord, BAKE, GENERATE, lane tools (" + std::to_string (kept) + "/" + std::to_string (n) + ")");
        p.generate (Sunrise, true);
        C.ok (p.idea().chordOct == 0, "CHORDS OCT: a new mood starts at 0 (as VOICING: the mood's defaults)");
    }
    {   // state: round trip; an old session (no key) = 0 and renders as before; the key is written only when used
        p.change ([] (Idea& X) { X.mode = Dusk; X.seed = 4040; applyModeDefaults (X); newIdea (X); for (auto& L : X.lanes) L.mute = false; }, false);
        const auto c0 = chordLaneNotes (p.idea()); const auto json0 = juce::JSON::toString (ArpProcessor::ideaToVar (p.idea()), true);
        p.change ([] (Idea& X) { setChordOct (X, -1); }, false);
        juce::MemoryBlock mb; p.getStateInformation (mb); ArpProcessor q; q.persistTaste = false; q.setStateInformation (mb.getData(), (int) mb.getSize());
        const bool rt = q.idea().chordOct == -1 && juce::JSON::toString (ArpProcessor::ideaToVar (q.idea()), true) == juce::JSON::toString (ArpProcessor::ideaToVar (p.idea()), true) && shifted (c0, chordLaneNotes (q.idea()), -12);
        auto v = ArpProcessor::ideaToVar (p.idea()); const bool hasKey = v.hasProperty ("chordOct");
        if (auto* o = v.getDynamicObject()) o->removeProperty ("chordOct");
        Idea old; ArpProcessor::ideaFromVar (v, old);
        const bool oldOk = old.chordOct == 0 && shifted (c0, chordLaneNotes (old), 0) && others (p.idea(), old);
        const bool noKey = ! json0.contains ("chordOct");
        C.ok (rt, "CHORDS OCT: saved + restored (plugin state), the chords come back moved");
        C.ok (hasKey && noKey && oldOk, "CHORDS OCT: written only when used (older sessions byte-identical); a session without it loads at 0 with the same chords");
        // MIDI drag-out (stems + one track) and the live MIDI out carry the moved chords
        p.change ([] (Idea& X) { setChordOct (X, 0); }, false); const auto m0 = midiPitches (ideaMidi (p.idea(), 1 << LaneChords, false, 124));
        setP (p, "midiCh1", 0); setP (p, "kick", 0);
        auto live = [&] { std::vector<MidiEv> ev; int byp = 0; RenderOpts o; o.bars = 2; render (p, o, ev, byp); std::vector<int> ps; for (auto& e : ev) if (e.on && e.ch == 3) ps.push_back (e.pitch); return ps; };
        const auto l0 = live();
        p.change ([] (Idea& X) { setChordOct (X, 1); }, false); const auto m1 = midiPitches (ideaMidi (p.idea(), 1 << LaneChords, false, 124));
        const auto l1 = live();
        bool mid = ! m0.empty() && m0.size() == m1.size(); for (size_t i = 0; mid && i < m0.size(); ++i) mid = m1[i] == m0[i] + 12;
        bool liv = ! l0.empty() && l0.size() == l1.size(); for (size_t i = 0; liv && i < l0.size(); ++i) liv = l1[i] == l0[i] + 12;
        std::vector<int> want; for (auto& n : chordLaneNotes (p.idea())) if (n.t < 2 * BAR) want.push_back (n.p);
        const bool match = l1.size() >= want.size() && ! want.empty() && std::equal (want.begin(), want.end(), l1.begin());   // (the render runs a beat past bar 2)
        setP (p, "midiCh1", 1);
        std::printf ("  MIDI: drag-out %zu chord notes moved %d, live %zu chord notes moved %d, = chord lane %d\n", m1.size(), (int) mid, l1.size(), (int) liv, (int) match);
        C.ok (mid, "CHORDS OCT: the MIDI drag-out (CHORDS) has every chord note +12");
        C.ok (liv && match, "CHORDS OCT: live MIDI out = the moved chord lane (what the preview plays)");
    }
    {   // the four looks: the row with the buttons, readable (>= 4.5:1) and screenshots
        const juce::String looks[] { "dark", "purple", "light", "ghost" }; int bad = 0, runs = 0;
        const juce::File dir (shotDir); if (shotDir.isNotEmpty()) dir.createDirectory();
        p.change ([] (Idea& X) { X.mode = Dusk; X.seed = 51; applyModeDefaults (X); newIdea (X); for (auto& L : X.lanes) L.mute = false; setChordOct (X, 1); }, false);
        for (auto& lk : looks)
        {
            p.theme = lk; ed.reset (dynamic_cast<ArpEditor*> (p.createEditor())); ed->setSize (1240, 820); ed->applyThemeNow (p.idea().mode); ed->selectLane (LaneChords); ed->refresh(); ed->toast ("", -1.0e15);
            const auto R = probeContrast (*ed, "chordoct/" + lk, nullptr, {}, { ed->editorCardArea() }); runs += R.runs; bad += R.bad;   // (the lane editor card: where the buttons are)
            if (R.bad) std::printf ("    %s: %d low-contrast runs, worst %s\n", lk.toRawUTF8(), R.bad, R.worstAt.toRawUTF8());
            if (shotDir.isNotEmpty()) { auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f); juce::File f = dir.getChildFile ("chordoct-" + lk + ".png"); f.deleteFile(); juce::FileOutputStream os (f); juce::PNGImageFormat png; png.writeImageToStream (img, os); }
        }
        C.ok (runs > 120 && bad == 0, "CHORDS OCT: the CHORDS lane with OCT - / + in BLACK, PURPLE, LIGHT, GHOST: every text run >= 4.5:1");
        p.theme = "dark";
    }
}
