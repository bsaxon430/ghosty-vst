// ArpEZatorTests: the manual writers' bug list (2026-10-27). --bugfix runs it alone.
//   NEW on ARP / MELODY / BASS gives a different take on every press (a deterministic sequence of takes), MP3 import,
//   SIMPLER / BUSIER tooltips follow the lane.
void newTakeChecks (Counter& C)
{
    using chordoct::sameNotes;
    std::printf ("== NEW: a different take on every press\n");
    int presses = 0, empties = 0, differ = 0, bassPresses = 0, bassDiffer = 0, undoOk = 0, undos = 0; bool determ = true;
    for (int m = 0; m < kMoods; ++m)
        for (uint32_t s : { 7u, 99u, 2024u })
        {
            ArpProcessor a, b; a.persistTaste = false; b.persistTaste = false;
            for (auto* p : { &a, &b }) p->change ([m, s] (Idea& X) { X.mode = m; X.seed = s; applyModeDefaults (X); newIdea (X); }, false);
            for (int lane : { (int) LaneArp, (int) LaneMelody, (int) LaneBass })
                for (int k = 0; k < 5; ++k)
                {
                    const auto prev = a.idea().lanes[lane].notes;
                    a.laneTool (lane, "new", nullptr); b.laneTool (lane, "new", nullptr); ++presses;
                    const bool emptyLane = prev.empty() && a.idea().lanes[lane].notes.empty();   // (a TRAP arp answers in the melody's rests: none = no arp, nothing to vary)
                    if (emptyLane) { --presses; ++empties; continue; }
                    const bool d = ! sameNotes (prev, a.idea().lanes[lane].notes); differ += d;
                    if (! d) std::printf ("    same take: mood %d seed %u lane %d press %d (%d notes, var %d, locks %d)\n", m, s, lane, k + 1, (int) prev.size(), a.idea().laneVar[(size_t) lane], (int) std::count_if (prev.begin(), prev.end(), [] (const Note& n) { return n.lock; }));
                    if (lane == LaneBass) { ++bassPresses; bassDiffer += d; }
                    determ &= sameNotes (a.idea().lanes[lane].notes, b.idea().lanes[lane].notes) && a.idea().laneVar == b.idea().laneVar;
                    if (k == 4 && ! prev.empty()) { ++undos; a.undo(); b.undo(); undoOk += sameNotes (a.idea().lanes[lane].notes, prev) && sameNotes (b.idea().lanes[lane].notes, prev); a.redo(); b.redo(); }
                }
        }
    C.ok (differ == presses, "NEW on ARP / MELODY / BASS: every press gives a different take (" + std::to_string (differ) + "/" + std::to_string (presses) + ", every mood, 3 seeds, 5 presses per lane; " + std::to_string (empties) + " presses on an empty TRAP arp skipped)");
    C.ok (bassDiffer == bassPresses, "NEW on an unedited BASS lane changes it every time (" + std::to_string (bassDiffer) + "/" + std::to_string (bassPresses) + ")");
    C.ok (determ, "NEW: the takes are a deterministic sequence (two instances from the same idea get the same takes)");
    C.ok (undoOk == undos, "NEW: one press is one undo step");
    {   // the take is saved: a reloaded session has the same notes and keeps going from the same place in the sequence
        ArpProcessor a; a.persistTaste = false; a.change ([] (Idea& X) { X.mode = Sunrise; X.seed = 55; applyModeDefaults (X); newIdea (X); }, false);
        a.laneTool (LaneBass, "new", nullptr); a.laneTool (LaneBass, "new", nullptr);
        juce::MemoryBlock st; a.getStateInformation (st); ArpProcessor r; r.persistTaste = false; r.setStateInformation (st.getData(), (int) st.getSize());
        const bool same0 = sameNotes (a.idea().lanes[LaneBass].notes, r.idea().lanes[LaneBass].notes);
        a.laneTool (LaneBass, "new", nullptr); r.laneTool (LaneBass, "new", nullptr);
        C.ok (same0 && sameNotes (a.idea().lanes[LaneBass].notes, r.idea().lanes[LaneBass].notes), "NEW: the take is saved with the session, and the next press after reopening gives the same next take");
    }
}

void mp3Checks (Counter& C)
{   // MP3 import uses JUCE's own decoder (JUCE_USE_MP3AUDIOFORMAT=1), not the computer's: a sample and a LISTEN file load from MP3
    std::printf ("== MP3 import\n");
    juce::AudioFormatManager fm; fm.registerBasicFormats(); bool mp3Format = false;
    for (int i = 0; i < fm.getNumKnownFormats(); ++i) mp3Format |= fm.getKnownFormat (i)->getFormatName().containsIgnoreCase ("MP3");
    C.ok (mp3Format, "MP3: the built-in MP3 reader is registered (not left to the computer's decoders)");
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("arpMp3Test"); dir.createDirectory();
    const auto wav = dir.getChildFile ("tone.wav"), mp3 = dir.getChildFile ("tone.mp3");
    {   // 2 s of A3 (220 Hz) with a few harmonics
        juce::AudioBuffer<float> b (1, 88200); for (int i = 0; i < b.getNumSamples(); ++i) { double v = 0; for (int h = 1; h <= 4; ++h) v += std::sin (2 * juce::MathConstants<double>::pi * 220.0 * h * i / 44100.0) / h; b.setSample (0, i, (float) (0.3 * v)); }
        wav.deleteFile(); juce::WavAudioFormat wf; std::unique_ptr<juce::OutputStream> os (wav.createOutputStream());
        if (auto* w = wf.createWriterFor (os.get(), 44100, 1, 16, {}, 0)) { os.release(); std::unique_ptr<juce::AudioFormatWriter> wr (w); wr->writeFromAudioSampleBuffer (b, 0, b.getNumSamples()); }
    }
    const bool made = std::system (("ffmpeg -y -loglevel error -i " + wav.getFullPathName().quoted() + " -b:a 160k " + mp3.getFullPathName().quoted() + " 2>/dev/null").toRawUTF8()) == 0 && mp3.existsAsFile();
    if (! made) { std::printf ("    (ffmpeg not available: MP3 load test skipped)\n"); dir.deleteRecursively(); return; }
    ArpProcessor p; p.persistTaste = false; juce::String msg;
    const bool loaded = p.loadSample (mp3, msg);
    juce::AudioBuffer<float> got; double rate = 0; const bool dec = p.decodeSampleBuffer (got, rate);
    C.ok (loaded && dec && got.getNumSamples() > 80000 && got.getMagnitude (0, 0, got.getNumSamples()) > 0.1f, "MP3: a 2 s MP3 loads as YOUR SAMPLE (" + std::to_string (got.getNumSamples()) + " samples) " + msg.toStdString());
    dir.deleteRecursively();
}

void laneTooltipChecks (Counter& C)
{   // SIMPLER / BUSIER / NEW: each lane shows its own tooltips every time the lane changes (CHORDS relabels them; the others get theirs back)
    using A = ArpEditorTestAccess;
    std::printf ("== lane tool tooltips\n");
    ArpProcessor p; p.persistTaste = false; p.hintsSeen = true;
    std::unique_ptr<ArpEditor> ed (dynamic_cast<ArpEditor*> (p.createEditor())); ed->setSize (1240, 820);
    auto& T = A::tools (*ed); int ok = 0, n = 0;
    auto plain = [&] { return T[4]->getTooltip() == "Fewer notes" && T[5]->getTooltip() == "More notes" && T[8]->getTooltip().startsWith ("A brand-new take for this lane"); };
    for (int round = 0; round < 2; ++round)
        for (int l : { (int) LaneChords, (int) LaneArp, (int) LaneChords, (int) LaneMelody, (int) LaneChords, (int) LaneBass })
        {
            ed->selectLane (l); ++n;
            if (l == LaneChords) ok += T[4]->getTooltip() != "Fewer notes" && T[5]->getTooltip() != "More notes" && T[8]->getTooltip().startsWith ("NEW CHORDS");
            else { const bool g = plain(); ok += g; if (! g) std::printf ("    lane %d: '%s' / '%s' / '%s'\n", l, T[4]->getTooltip().toRawUTF8(), T[5]->getTooltip().toRawUTF8(), T[8]->getTooltip().toRawUTF8()); }
        }
    C.ok (ok == n, "SIMPLER / BUSIER / NEW tooltips follow the lane: CHORDS wording on CHORDS only, ARP / MELODY / BASS get theirs back (" + std::to_string (ok) + "/" + std::to_string (n) + " lane changes)");
    p.change ([] (Idea& X) { X.simple = true; }, false); ed->selectLane (LaneChords); const bool smp = T[4]->getTooltip().startsWith ("SIMPLE chords"); ed->selectLane (LaneMelody);
    C.ok (smp && plain(), "SIMPLE chords wording stays on CHORDS (MELODY shows 'Fewer notes' / 'More notes')");
}

void bugfixChecks (Counter& C)
{
    newTakeChecks (C);
    mp3Checks (C);
    laneTooltipChecks (C);
}
