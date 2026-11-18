// Poltergeist `savegate` (PoltergeistTests savegate): while LOCKED (demo) every save of Poltergeist's own formats - the project
// (getStateInformation), user sounds, snapshots, MORPH boards - leaves out the audio Poltergeist MADE (RESAMPLE, PROCESS LAB
// results, Sample Lab renders) and keeps the user's own imported samples; a click save that left something out shows the UNLOCK
// hint. UNLOCKED, everything is saved (made audio carries made="1"). Shared/ExportGate.h keepRenderedAudio / renderedAudioLeftOut.

static int saveGateTests() {
    ghosty::demogate::Sandbox box;
    int fails = 0;
    auto check = [&](bool ok, const char* what) { std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++fails; };
    auto& L = ghosty::exportgate::log();
    auto xmlOf = [](const juce::MemoryBlock& mb) { return juce::AudioProcessor::getXmlFromBinary(mb.getData(), (int) mb.getSize()); };
    auto stateXml = [&](PoltergeistProcessor& p) { juce::MemoryBlock mb; p.getStateInformation(mb); return xmlOf(mb); };
    auto pcmLen = [](const juce::XmlElement* e, const char* attr = "data") { return e ? e->getStringAttribute(attr).length() : 0; };
    auto srcById = [](const juce::XmlElement& st, int id) -> const juce::XmlElement* {
        if (auto* lab = st.getChildByName("LAB")) for (auto* c : lab->getChildWithTagNameIterator("SRC")) if (c->getIntAttribute("id") == id) return c;
        return nullptr; };
    auto plabById = [](const juce::XmlElement& st, int id) -> const juce::XmlElement* {
        for (auto* c : st.getChildWithTagNameIterator("PLABSRC")) if (c->getIntAttribute("id") == id) return c;
        return nullptr; };

    // the user's own file (imported: always saved)
    const auto wavFile = box.root.getChildFile("home/Music/my sample.wav");
    {
        juce::AudioBuffer<float> b(1, 44100); for (int i = 0; i < b.getNumSamples(); ++i) b.setSample(0, i, 0.5f * (float) std::sin(i * 0.05));
        juce::WavAudioFormat wav; std::unique_ptr<juce::OutputStream> os(wavFile.createOutputStream());
        if (auto* w = wav.createWriterFor(os.get(), 44100, 1, 16, {}, 0)) { os.release(); std::unique_ptr<juce::AudioFormatWriter> wr(w); wr->writeFromAudioSampleBuffer(b, 0, b.getNumSamples()); }   // NOT-EXPORT: the test's own input file
    }

    {   // a project with only imported audio: locked and unlocked saves are the same bytes, with no made="1" anywhere
        PoltergeistProcessor p; p.setRateAndBufferSizeDetails(44100, 512); p.prepareToPlay(44100, 512);
        juce::String err; p.loadSampleFile(wavFile, err);
        ghosty::License::testForce().store(0); juce::MemoryBlock a; p.getStateInformation(a); const int leftA = p.leftOut;
        ghosty::License::testForce().store(1); juce::MemoryBlock b; p.getStateInformation(b);
        check(a == b && leftA == 0 && ! b.toString().contains("made=\"1\""), "imported only: the locked save = the unlocked save (byte for byte), nothing left out");
    }

    PoltergeistProcessor p; p.setRateAndBufferSizeDetails(44100, 512); p.prepareToPlay(44100, 512);
    std::unique_ptr<PoltergeistEditor> ed(dynamic_cast<PoltergeistEditor*>(p.createEditor())); ed->setSize(1040, 744);
    juce::String err;
    const int imported = p.labAddFile(wavFile, err);                    // a Lab recording from the user's file
    const int processed = p.labAddFile(wavFile, err);                   // ... and one PROCESS LAB will APPLY to
    {   // PROCESS LAB APPLY (the panel's Host::setBuffer -> replaceLabSource) and BOUNCE->NEW (addLabSourceFrom)
        plab::Audio a; a.rate = 48000; a.ch.push_back(std::vector<float>(48000)); for (int i = 0; i < 48000; ++i) a.ch[0][(size_t) i] = 0.3f * (float) std::sin(i * 0.02);
        p.replaceLabSource(processed, a);
    }
    p.labMapKey(60, p.lab.section(imported, 0.1, 0.5, "my key"));      // a key from the user's file (kept)
    p.labMapKey(62, p.lab.section(processed, 0.1, 0.5, "made key"));    // a key cut from the processed recording (made)
    {   // RESAMPLE (an old project's resampled Sample Ghost; the button left with the SAMPLE page, the processor path stays): play a note, then resample
        juce::MidiBuffer m; m.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 110), 0);
        for (int k = 0; k < 120; ++k) { juce::AudioBuffer<float> b(2, 512); b.clear(); p.processBlock(b, m); m.clear(); }
        p.resampleRecent(4.0);
    }
    const bool resampled = p.rawSample && p.rawSample->made;
    check(resampled, "RESAMPLE made the Sample Ghost (made = true)");
    check(p.lab.sources.count(processed) && p.lab.sources[processed]->made && ! p.lab.sources[imported]->made, "PROCESS LAB result is made, the imported recording is not");
    p.labRenderNow(); if (p.labRendered) p.chopLoad(std::make_shared<gs::SampleData>(*p.labRendered), false);   // VOCAL CHOP of a Sample Lab render (made)
    p.morphAddGhost(0.3f, 0.3f, "current");                              // a MORPH ghost holding the resampled sound

    // ---- LOCKED: the project
    ghosty::License::testForce().store(0); L.clear();
    {
        auto x = stateXml(p);
        check(x && x->getChildByName("SAMPLE") == nullptr, "locked project: no PCM for the RESAMPLE result (no SAMPLE block)");
        check(x && srcById(*x, processed) == nullptr && plabById(*x, processed) == nullptr, "locked project: no PCM for the PROCESS LAB result (Lab SRC + PLABSRC left out)");
        check(x && pcmLen(srcById(*x, imported)) > 1000 && pcmLen(plabById(*x, imported), "d0") > 1000, "locked project: the imported recording is still embedded (Lab + PROCESS LAB source)");
        int keys = 0, madeKeys = 0; if (x) if (auto* k = x->getChildByName("KEYS")) for (auto* e : k->getChildIterator()) { ++keys; if (e->getIntAttribute("note") == 62) ++madeKeys; }
        check(keys == 1 && madeKeys == 0, "locked project: the key cut from the user's file stays, the key cut from the processed one is left out");
        bool ghostPcm = false; if (x) if (auto* mo = x->getChildByName("MORPH")) for (auto* g : mo->getChildWithTagNameIterator("GSOUND")) ghostPcm |= g->hasAttribute("sdata");
        check(! ghostPcm, "locked project: the MORPH ghost keeps its settings, not the resampled audio");
        auto* ch = x ? x->getChildByName("CHOP") : nullptr;
        check(ch != nullptr && ! ch->hasAttribute("data") && ! ch->hasAttribute("audio"), "locked project: VOCAL CHOP of a Lab render keeps its lines, not the audio");
        check(p.leftOut >= 4 && L.stripped.empty() && L.hinted.empty(), "locked project (host save): counted, no hint (no click)");
    }
    // ---- LOCKED: click saves (user sound, snapshot, board) -> left out + the hint
    {
        L.clear(); p.saveUserSound("demo sound");
        juce::MemoryBlock mb; p.userDir().getChildFile("demo sound.ghostpatch").loadFileAsData(mb); auto x = xmlOf(mb);
        check(x && x->getChildByName("SAMPLE") == nullptr && srcById(*x, processed) == nullptr && pcmLen(srcById(*x, imported)) > 1000, "locked SAVE (user sound): no RESAMPLE / PROCESS LAB PCM, the imported one kept");
        check(ghosty::exportgate::Log::has(L.stripped, "save:polt.user-sound") && ghosty::exportgate::Log::has(L.hinted, "save:polt.user-sound"), "locked SAVE (user sound): the UNLOCK hint says why");
        L.clear(); auto snap = p.captureSound();
        check(! snap.getChildWithName("SAMPLE").isValid() && ghosty::exportgate::Log::has(L.hinted, "save:polt.snapshot"), "locked SNAPSHOT: no RESAMPLE PCM + the hint");
        L.clear(); p.saveBoard("demo board");
        auto bx = juce::XmlDocument::parse(p.boardsDir().getChildFile("demo board.ghostboard")); bool pcm = false;
        if (bx) for (auto* g : bx->getChildWithTagNameIterator("GSOUND")) pcm |= g->hasAttribute("sdata");
        check(bx != nullptr && ! pcm && ghosty::exportgate::Log::has(L.hinted, "save:polt.board"), "locked board save: no resampled PCM in the .ghostboard + the hint");
    }
    // ---- UNLOCKED: everything is saved, tagged
    ghosty::License::testForce().store(1); L.clear();
    {
        auto x = stateXml(p); auto* smp = x ? x->getChildByName("SAMPLE") : nullptr;
        check(smp != nullptr && pcmLen(smp) > 1000 && smp->getBoolAttribute("made"), "unlocked project: the RESAMPLE result is embedded (made=\"1\")");
        check(x && pcmLen(srcById(*x, processed)) > 1000 && srcById(*x, processed)->getBoolAttribute("made") && pcmLen(plabById(*x, processed), "d0") > 1000, "unlocked project: the PROCESS LAB result is embedded");
        check(x && pcmLen(srcById(*x, imported)) > 1000 && ! srcById(*x, imported)->getBoolAttribute("made"), "unlocked project: the imported recording is embedded, untagged");
        check(p.leftOut == 0, "unlocked project: nothing left out");
        check(x && pcmLen(x->getChildByName("CHOP")) > 1000, "unlocked project: the VOCAL CHOP audio is embedded");
        L.clear(); p.saveUserSound("full sound"); p.saveBoard("full board");
        juce::MemoryBlock mb; p.userDir().getChildFile("full sound.ghostpatch").loadFileAsData(mb); auto ux = xmlOf(mb);
        auto bx = juce::XmlDocument::parse(p.boardsDir().getChildFile("full board.ghostboard")); bool pcm = false;
        if (bx) for (auto* g : bx->getChildWithTagNameIterator("GSOUND")) pcm |= g->getStringAttribute("sdata").length() > 1000;
        check(ux && pcmLen(ux->getChildByName("SAMPLE")) > 1000 && pcm && L.stripped.empty() && L.hinted.empty(), "unlocked SAVE + board: the resampled audio is in both files, no hint");
        // round trip: a made sample comes back made (so a later demo save still leaves it out)
        juce::MemoryBlock st; p.getStateInformation(st);
        PoltergeistProcessor q; q.setRateAndBufferSizeDetails(44100, 512); q.restoreState(st.getData(), (int) st.getSize());
        check(q.rawSample && q.rawSample->made && q.lab.sources.count(processed) && q.lab.sources[processed]->made && q.lab.sources.count(imported) && ! q.lab.sources[imported]->made, "reload: made / imported provenance survives the project");
    }
    ghosty::License::testForce().store(-1);
    ed.reset();
    std::printf("savegate Poltergeist: %s (%d failure%s)\n", fails == 0 ? "PASS" : "FAIL", fails, fails == 1 ? "" : "s");
    return fails;
}
