// GhostSliceTests: phase 10 PLAY (included by SliceTests.cpp; uses its helpers)

static juce::AudioBuffer<float> constant (double sr, double secs, float v)
{
    juce::AudioBuffer<float> b (2, (int) (sr * secs));
    for (int c = 0; c < 2; ++c) for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (c, i, v);
    return b;
}
// blocks with MIDI (the transport stopped): the raw plugin output (ceiling latency included)
static juce::AudioBuffer<float> runMidi (SliceProcessor& p, int frames, int block, const std::function<void (int64, juce::MidiBuffer&)>& feed)
{
    juce::AudioBuffer<float> all (2, frames + block); all.clear();
    juce::AudioBuffer<float> b (2, block);
    for (int done = 0; done < frames; done += block)
    {
        juce::MidiBuffer m; if (feed) feed (done, m);
        tlAudio = true; p.processBlock (b, m); tlAudio = false;
        for (int c = 0; c < 2; ++c) all.copyFrom (c, done, b, c, 0, block);
    }
    juce::AudioBuffer<float> out (2, frames);
    for (int c = 0; c < 2; ++c) out.copyFrom (c, 0, all, c, 0, frames);
    return out;
}
static Project withPlay (const Project& p, const std::function<void (PlayMap&)>& f) { Project q = p; f (q.play); return q; }

static void testPlay()
{
    section ("[14] PLAY: slices on keys, voices, stealing, latency, mapping, export gate");
    // ---- slices + TURN INTO INSTRUMENT
    Rig r (drums (48000, 4.0));
    auto q = ops::splitRange (r->project(), 48000, 96000); q = ops::splitAt (*q, q->regions.back().id, 144000);
    r->apply (*q, "cuts");
    const auto sl = r->currentSlices();
    check (sl.size() == r->project().regions.size() && sl.size() == 4, "REGIONS: one slice per region in output order (" + juce::String ((int) sl.size()) + ")");
    r->apply (*ops::turnIntoInstrument (r->project(), (int) sl.size()), "instrument");
    const auto& km = r->project().play.keys;
    bool mapOk = km.size() == 4; for (int i = 0; i < (int) km.size(); ++i) mapOk = mapOk && km[(size_t) i].note == 36 + i && km[(size_t) i].slice == i;
    check (mapOk, "TURN INTO INSTRUMENT: slice i on note 36 + i (C1 upward)");
    {
        Project e = withPlay (r->project(), [] (PlayMap& m) { m.mode = SliceMode::Equal; m.equalN = 8; });
        auto ep = compile (e, r->getSource().get(), CompileOptions {});
        const auto es = play::slices (e, ep);
        bool even = es.size() == 8; for (auto& s : es) even = even && std::abs ((double) s.length() - ep.length / 8.0) <= 1.0;
        Project mn = withPlay (r->project(), [] (PlayMap& m) { m.mode = SliceMode::Manual; m.manual = { 30000.0, 70000.0, 150000.0 }; });
        const auto ms = play::slices (mn, ep);
        check (even && ms.size() == 4 && ms[1].dst0 == 30000 && ms[2].dst0 == 70000, "EQUAL 8 = 8 even parts; MANUAL cuts at their places (" + juce::String ((int) ms.size()) + " slices)");
    }
    check (r->buildBankNow() && r->getBank()->audio.size() == 4 && r->getBank()->audio[1].getNumSamples() == 48000, "the bank pre-renders every slice (zero-latency heads)");
    // ---- mapping round trip: JSON + MIDI
    {
        Project m = withPlay (r->project(), [] (PlayMap& pm)
        {
            pm.polyphony = 7; pm.bendRange = 12; pm.velToGain = 0.4; pm.velToFilter = 0.3; pm.mode = SliceMode::Manual; pm.manual = { 1234.5, 99999.25 };
            auto& k = pm.keys[1]; k.pitchSemis = -3; k.reverse = true; k.startMs = 12.5; k.attackMs = 3; k.decayMs = 40; k.sustain = 0.6; k.releaseMs = 120;
            k.gainDb = -2.5; k.pan = 0.25; k.choke = 2; k.oneShot = false; k.repeat = 3;
        });
        Project back; juce::String err;
        const bool js = json::fromString (json::toString (m), back, err) && back.play == m.play && back == m;
        const auto mf = play::mappingMidi (r->project().play, sl, r->project().bpm, 48000.0);
        const auto notes = play::notesOf (mf);
        bool midiOk = notes.size() == sl.size();
        const double tpf = 960.0 * r->project().bpm / 60.0 / 48000.0;
        for (size_t i = 0; i < notes.size() && midiOk; ++i)
        {
            const auto* k = r->project().play.key ((int) notes[i][0]);
            midiOk = k != nullptr && k->slice == (int) i && std::abs (notes[i][1] - std::round ((double) sl[i].dst0 * tpf)) < 0.5;
        }
        check (js, "mapping round trip: project.json keeps every key setting");
        check (midiOk, "mapping round trip: the MIDI map plays each slice's key at its place (" + juce::String ((int) notes.size()) + " notes)");
    }
    // ---- latency: a note at sample 100 sounds at 100 (+ the ceiling's fixed latency), bit-close to the bank
    {
        Project z = withPlay (r->project(), [] (PlayMap& m) { m.velToGain = 0; for (auto& k : m.keys) { k.attackMs = 0; k.releaseMs = 20; } });
        r->apply (z, "keys"); r->buildBankNow();
        { juce::AudioBuffer<float> w (2, 512); juce::MidiBuffer m; r->processBlock (w, m); }
        gAudioAllocs = 0;
        const int lat = r->ceilingLatency();
        const auto out = runMidi (*r.p, 24000, 256, [] (int64 done, juce::MidiBuffer& m) { if (done == 0) m.addEvent (juce::MidiMessage::noteOn (1, 37, (juce::uint8) 64), 100); });
        const auto& b = r->getBank()->audio[1];
        float err = 0, pre = 0;
        for (int i = 0; i < 100 + lat; ++i) pre = std::max (pre, std::abs (out.getSample (0, i)));
        for (int i = 0; i < 20000; ++i) err = std::max (err, std::abs (out.getSample (0, 100 + lat + i) - b.getSample (0, i)));
        check (pre < 1e-7f && err < 1e-5f, "latency 0 beyond the ceiling (" + juce::String (lat) + " smp): note-on at 100 = the slice from sample 100 + " + juce::String (lat) + " (error " + juce::String (err, 8) + ")");
        check (gAudioAllocs.load() == 0, "no allocation on the audio thread while notes play (" + juce::String (gAudioAllocs.load()) + ")");
    }
    // ---- voices: one-shot vs gate, note off, retrigger of the same key, choke
    {
        Project g = withPlay (r->project(), [] (PlayMap& m) { m.keys[0].oneShot = false; m.keys[0].releaseMs = 10; m.keys[2].choke = 1; m.keys[3].choke = 1; });
        r->apply (g, "keys");
        auto& E = r->getPlayEngine();
        runMidi (*r.p, 2048, 256, [] (int64 d, juce::MidiBuffer& m) { if (d == 0) { m.addEvent (juce::MidiMessage::noteOn (1, 36, 1.0f), 0); m.addEvent (juce::MidiMessage::noteOn (1, 37, 1.0f), 10); } });
        const int two = E.activeVoices.load();
        runMidi (*r.p, 2048, 256, [] (int64 d, juce::MidiBuffer& m) { if (d == 0) { m.addEvent (juce::MidiMessage::noteOff (1, 36), 0); m.addEvent (juce::MidiMessage::noteOff (1, 37), 0); } });
        const int afterOff = E.activeVoices.load();
        check (two == 2 && afterOff == 1, "note on = a voice; note off releases a GATE key (10 ms) and leaves a ONE-SHOT key playing (" + juce::String (two) + " -> " + juce::String (afterOff) + ")");
        runMidi (*r.p, 1024, 256, [] (int64 d, juce::MidiBuffer& m) { if (d == 0) m.addEvent (juce::MidiMessage::noteOn (1, 37, 1.0f), 0); });
        const int same = E.activeVoices.load();
        runMidi (*r.p, 1024, 256, [] (int64 d, juce::MidiBuffer& m) { if (d == 0) { m.addEvent (juce::MidiMessage::noteOn (1, 38, 1.0f), 0); m.addEvent (juce::MidiMessage::noteOn (1, 39, 1.0f), 300); } });
        const bool choked = E.isSounding (39) && ! E.isSounding (38);
        check (same == 1 && choked, "the same key again restarts it (old voice faded, " + juce::String (same) + " voice); a CHOKE group cuts its other key");
        runMidi (*r.p, 512, 256, [] (int64 d, juce::MidiBuffer& m) { if (d == 0) m.addEvent (juce::MidiMessage::allSoundOff (1), 0); });
        runMidi (*r.p, 2048, 256, {});
        check (E.activeVoices.load() == 0 && E.fadingVoices.load() == 0, "ALL SOUND OFF: every voice fades out");
    }
    // ---- polyphony 16 + click-free stealing
    {
        Rig d (constant (48000, 2.0, 0.5f));
        d->apply (withPlay (d->project(), [] (PlayMap& m) { m.mode = SliceMode::Equal; m.equalN = 2; m.velToGain = 0; m.polyphony = 16;
                                                             for (int i = 0; i < 20; ++i) { KeySlot k; k.note = 40 + i; k.slice = 0; k.gainDb = -30; k.attackMs = 0; m.keys.push_back (k); } }), "poly");
        d->buildBankNow();
        auto run = [&] (double fadeMs)
        {
            d->getPlayEngine().voicePool().stealFadeMs = fadeMs;
            d->getPlayEngine().voicePool().allNotesOff (true); runMidi (*d.p, 4096, 256, {});
            return runMidi (*d.p, 12000, 256, [] (int64 done, juce::MidiBuffer& m)
            {
                const int k = (int) (done / 256);
                if (k < 17 && done % 256 == 0) m.addEvent (juce::MidiMessage::noteOn (1, 40 + k, 1.0f), 128);   // 17 notes: the 17th steals
            });
        };
        const int steals0 = d->getPlayEngine().voicePool().steals;
        const auto smooth = run (3.0);
        const int active = d->getPlayEngine().activeVoices.load(), stolen = d->getPlayEngine().voicePool().steals - steals0;
        const auto hard = run (0.0);
        const int s17 = 16 * 256 + 128 + d->ceilingLatency();
        const float stepSmooth = maxStep (smooth, s17 - 64, s17 + 400), stepHard = maxStep (hard, s17 - 64, s17 + 400), stepNote = maxStep (smooth, 1, s17 - 200);
        check (active == 16 && stolen == 1, "polyphony 16: the 17th note steals one voice (" + juce::String (active) + " sounding, " + juce::String (stolen) + " stolen)");
        check (stepSmooth <= stepNote * 1.6f && stepHard > stepSmooth * 5.0f,
               "stealing is click-free: max step " + juce::String (stepSmooth, 5) + " (a plain note-on " + juce::String (stepNote, 5) + "; a hard cut would be " + juce::String (stepHard, 5) + ")");
        d->getPlayEngine().voicePool().stealFadeMs = 3.0;
    }
    // ---- pitch bend (+-12) and velocity -> gain
    {
        Rig s (sine (48000, 3.0, 200.0, 0.3));
        s->apply (withPlay (s->project(), [] (PlayMap& m) { m.bendRange = 12; m.velToGain = 1.0; KeySlot k; k.note = 60; k.slice = 0; k.attackMs = 0; m.keys = { k }; }), "bend");
        s->buildBankNow();
        const auto plain = runMidi (*s.p, 24000, 256, [] (int64 d, juce::MidiBuffer& m) { if (d == 0) m.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0); });
        runMidi (*s.p, 4096, 256, [] (int64 d, juce::MidiBuffer& m) { if (d == 0) m.addEvent (juce::MidiMessage::allSoundOff (1), 0); });
        const auto bent = runMidi (*s.p, 24000, 256, [] (int64 d, juce::MidiBuffer& m) { if (d == 0) { m.addEvent (juce::MidiMessage::pitchWheel (1, 16383), 0); m.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0); } });
        runMidi (*s.p, 4096, 256, [] (int64 d, juce::MidiBuffer& m) { if (d == 0) { m.addEvent (juce::MidiMessage::allSoundOff (1), 0); m.addEvent (juce::MidiMessage::pitchWheel (1, 8192), 0); } });
        const auto soft = runMidi (*s.p, 24000, 256, [] (int64 d, juce::MidiBuffer& m) { if (d == 0) m.addEvent (juce::MidiMessage::noteOn (1, 60, 0.5f), 0); });
        const int z0 = zeroCrossings (plain, 4000, 20000), z1 = zeroCrossings (bent, 4000, 20000);
        float p0 = 0, p1 = 0; for (int i = 4000; i < 20000; ++i) { p0 = std::max (p0, std::abs (plain.getSample (0, i))); p1 = std::max (p1, std::abs (soft.getSample (0, i))); }
        check (std::abs ((double) z1 / std::max (1, z0) - 2.0) < 0.03, "pitch bend +12 (range 12) doubles the pitch (" + juce::String (z0) + " -> " + juce::String (z1) + " zero crossings)");
        check (std::abs (p1 / p0 - 0.25f) < 0.01f, "velocity -> gain: velocity 0.5 = 1/4 the level (" + juce::String (p1 / p0, 3) + ")");
    }
    // ---- the export back-stops (the gate itself: GhostSliceTests demogate)
    {
        ghosty::License::testForce() = 0; juce::String err;
        const auto f1 = tmpDir().getChildFile ("locked-map.mid"), f2 = tmpDir().getChildFile ("locked-slice.wav");
        const bool none = ! r->exportPlayMidi (f1, err) && ! r->writeSliceWav (0, f2, err) && ! f1.exists() && ! f2.exists();
        ghosty::License::testForce() = -1;
        const auto f3 = tmpDir().getChildFile ("map.mid"), f4 = tmpDir().getChildFile ("slice.wav");
        check (none && r->exportPlayMidi (f3, err) && r->writeSliceWav (1, f4, err) && f3.existsAsFile() && f4.existsAsFile(),
               "locked: the processor writes no MIDI map / slice WAV (back-stop); unlocked it does");
    }
}

static void testPlayUi (const juce::File& shots)
{
    section ("[15] PLAY UI: layout in 3 themes, TURN INTO INSTRUMENT, pads, keyboard, screenshots");
    using namespace ghosty::themes;
    Rig rig (drums (48000, 6.0));
    auto q = ops::splitRange (rig->project(), 48000, 96000); q = ops::splitRange (*q, 150000, 200000); q = ops::splitAt (*q, q->regions.back().id, 240000);
    rig->apply (*q, "cuts"); rig->buildBankNow();
    std::unique_ptr<SliceEditor> ed (static_cast<SliceEditor*> (rig->createEditor()));
    ed->setVisible (true);
    auto& pp = ed->playPanel;
    for (auto size : { juce::Point<int> (1160, 700), juce::Point<int> (1600, 1000) })
    {
        ed->setSize (size.x, size.y); ed->resized(); ed->setWorkspace (gslice::ui::Workspace::Play); pump (20);
        int out = 0, overlap = 0, tight = 0;
        auto vis = pp.controls(); vis.erase (std::remove_if (vis.begin(), vis.end(), [] (juce::Component* c) { return ! c->isVisible(); }), vis.end());
        for (auto* c : vis) if (! pp.getLocalBounds().contains (c->getBounds())) { ++out; std::printf ("    outside: %s\n", c->getComponentID().toRawUTF8()); }
        for (size_t i = 0; i < vis.size(); ++i) for (size_t j = i + 1; j < vis.size(); ++j) if (vis[i]->getBounds().intersects (vis[j]->getBounds())) { ++overlap; std::printf ("    overlap: %s / %s\n", vis[i]->getComponentID().toRawUTF8(), vis[j]->getComponentID().toRawUTF8()); }
        for (auto* c : vis) if (auto* b = dynamic_cast<juce::TextButton*> (c)) { const auto f = ed->lnf.getTextButtonFont (*b, b->getHeight()); if (ghosty::textWidth (f, b->getButtonText()) > (float) b->getWidth() - 6) { ++tight; std::printf ("    text too wide: %s\n", b->getButtonText().toRawUTF8()); } }
        // the editor's own controls in PLAY (header + tool row 1)
        std::vector<juce::Component*> ev;
        for (auto* c : ed->getChildren()) if (c->isVisible() && c != &pp && dynamic_cast<ghosty::exportgate::Hint*> (c) == nullptr && dynamic_cast<juce::TooltipWindow*> (c) == nullptr && dynamic_cast<juce::ResizableCornerComponent*> (c) == nullptr) ev.push_back (c);
        for (auto* c : ev) if (c->getBounds().intersects (pp.getBounds())) { ++overlap; std::printf ("    over the panel: %s\n", c->getComponentID().toRawUTF8()); }
        check (out == 0 && overlap == 0 && tight == 0 && pp.isVisible() && ! ed->view.isVisible(),
               juce::String (size.x) + "x" + juce::String (size.y) + " PLAY: " + juce::String ((int) vis.size()) + " controls inside, no overlaps, every label fits");
    }
    ed->setSize (1280, 780); ed->resized(); ed->setWorkspace (gslice::ui::Workspace::Play); pump (20);
    pp.instrBtn.triggerClick(); pump (10);
    check (rig->project().play.keys.size() == rig->currentSlices().size() && rig->project().play.keys.size() == 6, "TURN INTO INSTRUMENT (button): " + juce::String ((int) rig->project().play.keys.size()) + " keys");
    // a pad click plays its key (through the audition queue), the pad / key light up
    const auto pr = pp.pads.padRect (2).getCentre();
    const int before = rig->getPlayEngine().notesPlayed.load();
    pp.pads.mouseDown (mouseAt (pp.pads, pr));
    { juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer m; rig->processBlock (b, m); }
    const bool lit = rig->getPlayEngine().isSounding (38);
    pp.pads.mouseUp (mouseAt (pp.pads, pr, {}));
    check (rig->getPlayEngine().notesPlayed.load() == before + 1 && lit && pp.selectedSlice() == 2, "click a pad = its key plays (slice 3 on D1) and the pad is selected");
    pp.pitchF.s.setValue (5.0, juce::sendNotificationSync);
    check (rig->project().play.key (38) != nullptr && rig->project().play.key (38)->pitchSemis == 5.0, "the key panel edits the selected pad's key (PITCH +5)");
    pp.oneShotBtn.triggerClick(); pump (10); pp.repeatBox.setSelectedId (3, juce::sendNotificationSync);
    check (! rig->project().play.key (38)->oneShot && rig->project().play.key (38)->repeat == 2, "GATE and REPEAT 1/16 on the key");
    const auto kr = pp.keys.keyRect (36).getCentre();
    pp.keys.mouseDown (mouseAt (pp.keys, kr)); { juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer m; rig->processBlock (b, m); } pp.keys.mouseUp (mouseAt (pp.keys, kr, {}));
    check (pp.selectedSlice() == 0 && rig->getPlayEngine().notesPlayed.load() == before + 2, "click a key on the keyboard strip = it plays and selects its pad");
    pp.setMode (SliceMode::Manual); pump (5);
    const auto nCuts = rig->project().play.manual.size();
    const auto sx = pp.strip.getWidth() * 0.93f;
    pp.strip.mouseDown (mouseAt (pp.strip, { sx, 20 })); pp.strip.mouseUp (mouseAt (pp.strip, { sx, 20 }, {}));
    check (rig->project().play.mode == SliceMode::Manual && rig->project().play.manual.size() == nCuts + 1, "MANUAL: the cuts start from the regions (" + juce::String ((int) nCuts) + "), a click on the strip adds one");
    rig->undo(); rig->undo(); rig->buildBankNow(); pp.refresh(); pump (10);
    for (auto t : { Theme::Dark, Theme::Purple, Theme::Light })
    {
        ed->setTheme (t); pp.selectPad (2); pump (20);
        snap (*ed, shots.getChildFile ("slice-" + displayName (t).toLowerCase() + "-play.png"));
    }
    check (shots.getChildFile ("slice-light-play.png").existsAsFile(), "PLAY screenshots written (3 themes)");
    ed.reset(); pump (20);
}
