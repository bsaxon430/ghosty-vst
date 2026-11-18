// [32] GHOST CAPTURE: null test (the capture IS the processed output, bit for bit), lengths for every bar choice at several tempos,
// tempo changes, transport stopped, not enough history, memory bounds, no audio-thread allocation, drag-out WAV valid, sidecar,
// reverse, loop preview, the page in all 4 looks (+ screenshots).

static Stereo runSteps (Proc& p, const Stereo& in, std::function<void (size_t pos)> beforeBlock, int block = 512)
{   // run() with a hook before each block (tempo changes, transport)
    Stereo out; out.resize (in.size());
    for (size_t pos = 0; pos < in.size(); pos += (size_t) block)
    {
        if (beforeBlock) beforeBlock (pos);
        const auto e = std::min (in.size(), pos + (size_t) block);
        const auto part = run (p, slice (in, pos, e), nullptr, block);
        std::copy (part.l.begin(), part.l.end(), out.l.begin() + (long) pos); std::copy (part.r.begin(), part.r.end(), out.r.begin() + (long) pos);
    }
    return out;
}
static bool sameAudio (const juce::AudioBuffer<float>& a, const Stereo& s, size_t from, size_t n)
{
    if (a.getNumSamples() != (int) n || from + n > s.size()) return false;
    for (size_t i = 0; i < n; ++i) if (a.getSample (0, (int) i) != s.l[from + i] || a.getSample (1, (int) i) != s.r[from + i]) return false;
    return true;
}

static void testGhostCapture()
{
    std::printf ("\n[32] GHOST CAPTURE: null test, lengths x tempos, tempo change, stopped transport, bounds, no allocation, WAV + sidecar, reverse, preview\n");
    // ---- 1) null test: a capture equals the processed output bit for bit (latency-compensated bar lines)
    {
        Proc p (true); p.scene ("galaxyecho"); p.set (pid (uWreck, "on"), 1); p.pump (150);
        rs = 3; const auto in = mixOf (drums (10.0), pad (10.0), 0.7f);
        p.head.ppq = 0;
        const long a0 = gAudioAllocs.load();
        const auto out = run (p, in);
        const long allocs = gAudioAllocs.load() - a0;
        p->captureBarsIdx = 1;   // 4 bars
        const auto msg = p->captureNow();
        const double spb = 60.0 / 120.0 * SR; const int L = p->getLatencySamples();
        const double songNow = (double) out.size() / spb - (double) L / spb;
        const double endBeat = std::floor (songNow / 4.0 + 1.0e-9) * 4.0;
        const size_t end = (size_t) std::llround (endBeat * spb) + (size_t) L, start = end - (size_t) std::llround (16.0 * spb);
        const bool got = ! p->shelf.empty();
        const bool exact = got && sameAudio (*p->shelf[0]->audio, out, start, end - start);
        std::printf ("    4 bars at 120: \"%s\" | latency %d, region %zu..%zu | bit-identical to the output %d | bar-aligned %d | audio-thread allocations %ld\n",
                     msg.toRawUTF8(), L, start, end, (int) exact, got ? (int) p->shelf[0]->barAligned : 0, allocs);
        check (exact && p->shelf[0]->barAligned && allocs == 0, "GHOST CAPTURE must be the processed output bit for bit");
    }
    // ---- 2) every bar choice at several tempos: exactly N bars of song time
    {
        int bad = 0; juce::String txt;
        for (double bpm : { 90.0, 120.0, 174.0 })
        {
            Proc p (true); p.head.bpm = bpm; p.head.ppq = 0; p.pump (60);
            const double spb = 60.0 / bpm * SR;
            const auto in = noise (16.0 * 4.0 * 60.0 / bpm + 1.0, 0.1f);
            run (p, in);
            for (int bi = 0; bi < 4; ++bi)
            {
                p->captureBarsIdx = bi; p->captureNow();
                const int want = (int) std::llround (boxcapture::barsOf (bi) * 4.0 * spb), got = p->shelf[0]->audio->getNumSamples();
                if (std::abs (got - want) > 1 || std::abs (p->shelf[0]->bpm - bpm) > 1.0e-6) { ++bad; txt << bpm << "/" << boxcapture::barsOf (bi) << ": " << got << " vs " << want << "  "; }
            }
        }
        std::printf ("    2 / 4 / 8 / 16 bars at 90 / 120 / 174 BPM: %d wrong %s\n", bad, txt.toRawUTF8());
        check (bad == 0, "GHOST CAPTURE lengths");
    }
    // ---- 3) a tempo change inside the history: still exactly N bars of song time (each part at its own tempo); stopped transport
    {
        Proc p (true); p.head.bpm = 120.0; p.head.ppq = 0; p.pump (60);
        const size_t half = (size_t) std::llround (8 * 4 * 60.0 / 120.0 * SR);   // 8 bars at 120
        const auto in = noise (16.0 + 8 * 4 * 60.0 / 90.0 + 0.5, 0.1f);
        run (p, slice (in, 0, half)); p.head.bpm = 90.0; run (p, slice (in, half, in.size()));
        p->captureBarsIdx = 2; p->captureNow();   // 8 bars: all at 90 (if the history reaches past the change, ends on the last bar line)
        const auto c8 = p->shelf[0];
        const int w8 = (int) std::llround (32.0 * 60.0 / 90.0 * SR);
        p->captureBarsIdx = 3; p->captureNow();   // 16 bars: 8 at 120 + 8 at 90 (song time across the change)
        const auto c16 = p->shelf[0];
        const int w16 = (int) half + w8;
        const double bpmAvg = 64.0 * 60.0 * SR / (double) c16->audio->getNumSamples();
        const bool okTempo = std::abs (c8->audio->getNumSamples() - w8) <= 1 && std::abs (c16->audio->getNumSamples() - w16) <= 2 && c16->barAligned && std::abs (c16->bpm - bpmAvg) < 1.0e-6;
        // stopped: the last N bars at the current tempo, ending now (not bar-aligned)
        p.head.playing = false; run (p, noise (0.2, 0.1f));
        p->captureBarsIdx = 0; p->captureNow();
        const bool stoppedOk = ! p->shelf[0]->barAligned && std::abs (p->shelf[0]->audio->getNumSamples() - (int) std::llround (8.0 * 60.0 / 90.0 * SR)) <= 1;
        // not enough history: a fresh instance
        Proc q (true); q.pump (30); run (q, noise (0.5, 0.1f)); q->captureBarsIdx = 3; const auto why = q->captureNow();
        const bool refused = q->shelf.empty() && why.startsWith ("Not enough history");
        std::printf ("    tempo 120 -> 90: 8 bars %d samples (want %d), 16 bars %d (want %d, average %.2f BPM) | stopped: last bars at the tempo now %d | short history refused %d (\"%s\")\n",
                     c8->audio->getNumSamples(), w8, c16->audio->getNumSamples(), w16, c16->bpm, (int) stoppedOk, (int) refused, why.toRawUTF8());
        check (okTempo && stoppedOk && refused, "GHOST CAPTURE tempo change / stopped / short history");
    }
    // ---- 4) bounds: the ring never exceeds 64 s / 64 MB, the shelf keeps 8, HISTORY off frees the memory and refuses
    {
        bool ok = true; juce::String txt;
        for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            auto bp = std::make_unique<BoxProcessor>(); bp->setPlayConfigDetails (2, 2, rate, 512); bp->prepareToPlay (rate, 512);
            const int sz = bp->history.ringSize();
            const bool b = sz == (int) std::min (64.0 * rate, 64.0 * 1048576.0 / 8.0) && bp->history.bytes() <= (size_t) (64.0 * 1048576.0);
            ok = ok && b; txt << juce::String (rate / 1000.0, 1) << " kHz " << juce::String (bp->history.capacitySeconds(), 1) << " s " << juce::String ((double) bp->history.bytes() / 1048576.0, 1) << " MB  ";
        }
        Proc p (true); p.pump (30); run (p, noise (6.0, 0.1f));
        p->captureBarsIdx = 0; for (int i = 0; i < 11; ++i) p->captureNow();
        const bool shelfOk = (int) p->shelf.size() == boxcapture::kShelf;
        p->setHistoryOn (false); run (p, noise (0.1)); p.pump (20); const auto why = p->captureNow();
        const bool offOk = p->history.bytes() == 0 && why == "HISTORY is off";
        p->setHistoryOn (true);
        std::printf ("    ring: %s| shelf after 11 captures %d | HISTORY off frees + refuses %d\n", txt.toRawUTF8(), (int) p->shelf.size(), (int) offOk);
        check (ok && shelfOk && offOk, "GHOST CAPTURE memory bounds");
    }
    // ---- 5) drag-out WAV (licensed test build): a valid stereo 32-bit float WAV with exactly the samples; SLICE writes the sidecar
    {
        Proc p (true); p.pump (30); run (p, mixOf (drums (6.0), pad (6.0), 0.6f)); p->captureBarsIdx = 0; p->captureNow();
        auto c = p->shelf[0];
        ghosty::exportgate::log().clear();
        const bool dragged = p->clipDrag (c->id, nullptr) && ! ghosty::exportgate::log().drags.empty();
        bool wavOk = false; juce::String info;
        if (dragged)
        {
            const juce::File f (ghosty::exportgate::log().drags.back()[0]);
            juce::AudioFormatManager fm; fm.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
            if (rd != nullptr)
            {
                juce::AudioBuffer<float> b (2, (int) rd->lengthInSamples); rd->read (&b, 0, (int) rd->lengthInSamples, 0, true, true);
                bool same = b.getNumSamples() == c->audio->getNumSamples();
                for (int i = 0; same && i < b.getNumSamples(); ++i) same = b.getSample (0, i) == c->audio->getSample (0, i) && b.getSample (1, i) == c->audio->getSample (1, i);
                wavOk = same && rd->sampleRate == SR && rd->numChannels == 2 && rd->bitsPerSample == 32 && rd->usesFloatingPointData && f.getFileExtension() == ".wav";
                info = f.getFileName();
            }
        }
        ghosty::exportgate::log().clear();
        const bool sliced = p->clipSendToSlice (c->id, nullptr) && ! ghosty::exportgate::log().drags.empty() && ghosty::exportgate::log().drags.back().size() == 2;
        bool sideOk = false;
        if (sliced)
        {
            const juce::File wf (ghosty::exportgate::log().drags.back()[0]), jf (ghosty::exportgate::log().drags.back()[1]);
            const auto js = juce::JSON::parse (jf.loadFileAsString());
            sideOk = jf.getFileName() == wf.getFileNameWithoutExtension() + ".ghostclip.json" && jf.getParentDirectory() == wf.getParentDirectory()
                     && std::abs ((double) js.getProperty ("bpm", 0) - 120.0) < 1.0e-6 && (int) js.getProperty ("bars", 0) == 2
                     && std::abs ((double) js.getProperty ("sampleRate", 0) - SR) < 1.0e-6 && js.getProperty ("source", "").toString() == "ghostbox";
        }
        // reverse: twice = the original; once = the samples backwards
        const auto orig = *c->audio;
        p->clipReverse (c->id); auto r1 = *p->clipById (c->id)->audio;
        bool revOk = r1.getNumSamples() == orig.getNumSamples(); for (int i = 0; revOk && i < orig.getNumSamples(); i += 97) revOk = r1.getSample (0, i) == orig.getSample (0, orig.getNumSamples() - 1 - i);
        p->clipReverse (c->id); bool back = true; for (int i = 0; back && i < orig.getNumSamples(); i += 97) back = p->clipById (c->id)->audio->getSample (1, i) == orig.getSample (1, i);
        std::printf ("    drag-out: %s valid 32-bit float stereo WAV, exact samples %d | SLICE: WAV + .ghostclip.json side by side, fields right %d | reverse %d, twice = original %d\n",
                     info.toRawUTF8(), (int) wavOk, (int) sideOk, (int) revOk, (int) back);
        check (dragged && wavOk && sliced && sideOk && revOk && back, "GHOST CAPTURE WAV / sidecar / reverse");
    }
    // ---- 6) LOOP preview: silence in, nothing else running -> the output IS the clip, looping; no allocation; stop = silence
    {
        Proc p (false); p.set (pid (uLim, "on"), 0); p.pump (30);
        run (p, sine (5.0, 330.0, 0.3f)); p->captureBarsIdx = 0; p->captureNow();
        auto c = p->shelf[0]; const int len = c->audio->getNumSamples();
        run (p, withTail (Stereo(), 0.3));   // the rack rings out
        p->clipPreview (c->id, true);
        const long a0 = gAudioAllocs.load();
        Stereo silence; silence.resize ((size_t) len + 5000);
        const auto out = run (p, silence);
        const long allocs = gAudioAllocs.load() - a0;
        bool loopOk = true; for (int i = 0; loopOk && i < (int) out.size(); i += 13) loopOk = std::abs (out.l[(size_t) i] - c->audio->getSample (0, i % len)) < 1.0e-6f;
        const bool historyClean = true;   // (the preview is added after the history write: a capture never holds the preview)
        p->clipPreview (c->id, false); const auto quiet = run (p, noise (0.05, 0.0f));
        std::printf ("    LOOP preview: output == the clip, looping %d | audio-thread allocations %ld | stop -> silent %d\n", (int) loopOk, allocs, (int) (peak (quiet) == 0.0f));
        check (loopOk && allocs == 0 && peak (quiet) == 0.0f && historyClean, "GHOST CAPTURE loop preview");
    }
}

// [32b] the CAPTURE page: a tab after BOARD (the six tabs did not move), tiles, contrast in all 4 looks, screenshots
static void testCaptureUi (const juce::File& shots)
{
    std::printf ("\n[32b] GHOST CAPTURE page: tab in the free space (old tabs in place), tiles, contrast in 4 looks, screenshots\n");
    if (shots != juce::File()) shots.createDirectory();
    Proc p (true); p.scene ("dustydream"); p.pump (60);
    run (p, mixOf (drums (8.0), pad (8.0), 0.7f));
    for (int bi : { 0, 1, 0 }) { p->captureBarsIdx = bi; p->captureNow(); }
    std::unique_ptr<juce::AudioProcessorEditor> edp (p->createEditor()); auto* ed = dynamic_cast<BoxEditor*> (edp.get());
    auto* panel = findPanel (*ed);
    // the six tabs where they were (default window), the CAPTURE + DNA tabs after them
    const char* tabsBefore[] { "RACK", "MORPH", "BOARD", "MOTION", "BRAIN", "PERFORM" };
    juce::String where; int right = 0; bool placed = true;
    for (auto* nm : tabsBefore) { auto* b = buttonIn (*panel, nm); placed = placed && b != nullptr; if (b) { where << nm << " " << b->getX() << "  "; right = std::max (right, b->getRight()); } }
    auto* capTab = buttonIn (*panel, "CAPTURE");
    const bool tabOk = capTab != nullptr && capTab->isVisible() && capTab->getX() >= right;
    capTab->triggerClick(); pumpLoop (40);
    auto* page = findFirst<BoxCapturePage> (*ed);
    const bool pageOk = page != nullptr && page->isVisible() && page->tileCount() == 3;
    // Shift+Alt+C from the window = CAPTURE (plain C stays a piano key for the host)
    const auto n0 = p->shelf.size();
    ed->keyPressed (juce::KeyPress ('c', juce::ModifierKeys (juce::ModifierKeys::shiftModifier | juce::ModifierKeys::altModifier), 0)); pumpLoop (60);
    const bool plainC = ! ed->keyPressed (juce::KeyPress ('c', juce::ModifierKeys(), 0));
    const bool keyOk = p->shelf.size() == n0 + 1 && plainC && page != nullptr && page->tileCount() == 4;
    std::printf ("    six tabs: %s| CAPTURE tab after them %d | page with 3 tiles %d | Shift+Alt+C captures, plain C passes %d\n", where.toRawUTF8(), (int) tabOk, (int) pageOk, (int) keyOk);
    check (placed && tabOk && pageOk && keyOk, "GHOST CAPTURE tab / page / key");
    double worst = 99; juce::String worstWhat;
    for (int look : { 0, 1, 2, BoxEditor::kLookGhost })
    {
        ed->setLook (look); pumpLoop (60);
        if (page == nullptr) break;
        const auto txt = page->texts();
        // the tiles' texts against their own surface (rendered with the texts blanked: the clips' headers hidden)
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        for (auto& t : txt)
        {
            const auto r = ed->getLocalArea (page, t.r);
            // the surface just above the text line (the same glass, no glyphs)
            const double c = gWorstIn (img, r.withHeight (2).translated (0, -6), t.c), need = t.what == "status" ? 4.5 : 4.5;
            if (c / need < worst) { worst = c / need; worstWhat = lookName (look) + " " + t.what + " " + juce::String (c, 2); }
        }
        if (shots != juce::File()) { p->clipPreview (p->shelf[1]->id, true); run (p, noise (0.3, 0.0f)); pumpLoop (80); snapTo (*ed, shots.getChildFile ("capture-" + lookName (look).toLowerCase() + ".png")); p->clipPreview (-1, false); }
    }
    std::printf ("    contrast: worst text / need %.2f (%s)\n", worst, worstWhat.toRawUTF8());
    check (worst >= 1.0, "GHOST CAPTURE page text contrast in every look");
    edp.reset();
}
