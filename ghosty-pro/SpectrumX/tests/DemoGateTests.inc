// Ghosty Spectrum `demogate` (SpectrumLabTests demogate): every way audio or MIDI can leave the plugin
// (docs/DEMO-EXPORT-GATE.md), triggered through the real editor / pages with the licence forced LOCKED, then UNLOCKED.
// Shared/tests/DemoGateHarness.h checks: locked = the gate refused that id, the UNLOCK hint showed, no drag, no save dialog,
// no audio / MIDI file anywhere in the sandbox, clipboard unchanged; unlocked = it really exports.
#include "../../Shared/tests/DemoGateHarness.h"

static constexpr int kSpecInventory = 26;   // = the Ghosty Spectrum table in docs/DEMO-EXPORT-GATE.md

static int demoGateTests() {
    ghosty::demogate::Sandbox box;   // FIRST: HOME / TMPDIR / Documents / Music -> a fresh folder
    using ghosty::demogate::pump;
    auto& G = ghosty::exportgate::log();
    auto refused = [&](const char* id) { return ghosty::exportgate::Log::has(G.refused, id); };
    auto unlocked = [] { return ghosty::License::testForce().load() == 1; };

    ghosty::License::testForce().store(1);   // the set-up runs licensed
    SpecProcessor p; p.guideSeen = 0x7fffffff; p.prepareToPlay(48000, 512); auto& L = p.lab;
    { juce::AudioBuffer<float> b(2, 512); juce::MidiBuffer m; p.processBlock(b, m); }
    addLoop(p);
    // the Lab: a sound (also the PROCESS page's sound)
    { const auto x = testSignal(2.0); analyse(x, L.canvas); L.sourceAudio = x; L.sourceName = "gate"; const int v0 = L.bankVersion.load(); L.edited(); L.requestPreview();
      waitFor([&] { return L.bankVersion.load() > v0 && !L.busy.load(); }, 20000); }
    // ARRANGE: a tone with notes (MIX / STEMS / MIDI), a loop region
    { juce::AudioBuffer<float> tone(2, 48000 * 4); for (int i = 0; i < tone.getNumSamples(); ++i) { const float v = 0.25f * (float) std::sin(2 * juce::MathConstants<double>::pi * 220 * i / 48000.0); tone.setSample(0, i, v); tone.setSample(1, i, v); }
      std::vector<arr::Note> notes { { 60, 1.0f, 0.0, 0.5 }, { 64, 1.0f, 1.0, 0.5 }, { 67, 1.0f, 2.0, 0.5 } };
      p.arrange.receive(arr::makePayload(tone, 48000, "gate tone", notes, {}, 120), 0, 0); p.arrange.waitUntilUpToDate(20000);
      p.arrange.arrangement.loopStart = 0; p.arrange.arrangement.loopEnd = 4; p.arrange.arrangement.loopOn = false; p.arrange.changed(); p.arrange.waitUntilUpToDate(20000); }
    // ARCS: a factory page (loop off: its loop WAV is only pre-rendered inside the paths that test it)
    p.arcsPage.edit() = arcs::factory::pages()[1].make(); p.arcsPage.edit().loopOn = false; p.arcsPage.commit();
#if GHOSTY_VOICE
    { juce::String err; p.voice.setAudio(0, synthVoice(150.0, 1.5), 48000, "ah", false, &err); p.voice.waitIdle(60000); p.voice.renderNow(err); }
#endif
    std::unique_ptr<juce::AudioProcessorEditor> edp(p.createEditor()); auto* ed = dynamic_cast<SpecEditor*>(edp.get()); ed->setVisible(true); pump(150);
    auto click = [&](const char* id) { if (auto* b = dynamic_cast<juce::Button*>(findRec(ed, id))) b->triggerClick(); pump(150); };
    for (auto* t : { "tab_arrange", "tab_arcs", "tab_process", "tab_voice", "tab_lab", "tab_loop" }) click(t);
    click("labButton"); if (auto* panel = dynamic_cast<LabPanel*>(findRec(ed, "spectralLab"))) panel->showPage(0); pump(100);
    auto* arrPg = dynamic_cast<arr::ArrangePage*>(findRec(ed, "arrangePage")); auto* arcsPg = dynamic_cast<arcs::ArcsPage*>(findRec(ed, "arcsPage"));
    auto* procPg = dynamic_cast<LabProcessPage*>(findRec(ed, "processPage"));
#if GHOSTY_VOICE
    auto* voiceTab = dynamic_cast<SpecVoiceTab*>(findRec(ed, "voiceTab"));
    if (!voiceTab) { std::printf("demogate: voice pages missing\n"); return 1; }
#endif
    if (!arrPg || !arcsPg || !procPg) { std::printf("demogate: pages missing\n"); return 1; }
    // the background MIX for the current arrangement, ready (licensed) before the locked runs start
    waitFor([&] { return p.arrange.lastRender && p.arrange.lastRender->revision == p.arrange.audioHashNow(p.arrange.arrangement.loopOn) && !p.arrange.rendering(); }, 30000);
    pump(200);

    auto comp = [&](const char* id) -> juce::Component& { auto* c = findRec(ed, id); jassert(c != nullptr); static juce::Component none; return c ? *c : none; };
    auto dragC = [&](juce::Component& c) { drag(c, { 10, 8 }, { 110, 8 }); };
    // background work started in an UNLOCKED run finishes before the next LOCKED run starts
    auto settle = [&] { if (!unlocked()) return; waitFor([&] { return !p.arrange.rendering() && !p.arcsPage.rendering() && !L.busy.load(); }, 60000);
#if GHOSTY_VOICE
        p.voice.waitIdle(60000);
#endif
        pump(150); };
    // drag until it really drags (renders that are not ready yet ask again); locked = one refused try
    auto untilDrag = [&](const char* id, std::function<void()> fn, int ms) { const double t0 = juce::Time::getMillisecondCounterHiRes();
        for (;;) { fn(); if (!G.drags.empty() || refused(id) || juce::Time::getMillisecondCounterHiRes() - t0 > ms) break; pump(250); } settle(); };
    auto findBtn = [&](juce::Component& root, const juce::String& text) -> juce::Button* { juce::Button* r = nullptr;
        std::function<void(juce::Component&)> f = [&](juce::Component& c) { if (auto* b = dynamic_cast<juce::TextButton*>(&c); b && b->getButtonText() == text) r = b; for (auto* ch : c.getChildren()) if (!r) f(*ch); }; f(root); return r; };
    auto clickUp = [&](juce::Component& c) { c.mouseDown(mev(c, { 10, 8 })); c.mouseUp(mev(c, { 10, 8 })); };
    int arcsLoopTick = 0;
    auto arcsLoop = [&](bool on) { auto& P = p.arcsPage.edit(); P.loopOn = on; P.loopA = 0.0f; P.loopB = 0.25f + 0.01f * (float) (++arcsLoopTick % 20); p.arcsPage.commit("demogate loop"); };

    std::vector<ghosty::demogate::Path> paths {
        { "spec.pattern.wav-drag", ghosty::demogate::Drag, [&] { dragC(comp("patternWavOut")); settle(); } },
        { "spec.pattern.stems-drag", ghosty::demogate::Drag, [&] { dragC(comp("patternStemsOut")); settle(); } },
        { "spec.lab.wav-drag", ghosty::demogate::Drag, [&] { untilDrag("spec.lab.wav-drag", [&] { dragC(comp("labWavOut")); }, 30000); } },
        { "spec.lab.region-drag", ghosty::demogate::Drag, [&] { auto* w = dynamic_cast<ghosty::WaveRegion*>(findRec(ed, "labWave")); untilDrag("spec.lab.region-drag", [&] { if (w) w->startExport(); }, 30000); } },
        { "spec.lab.send-beats", ghosty::demogate::File, [&] { if (auto* b = dynamic_cast<juce::Button*>(findRec(ed, "labToBeats"))) b->triggerClick(); pump(100); waitFor([&] { return refused("spec.lab.send-beats") || (L.progress.load() < 0 && !L.busy.load() && LabHost::sharedSoundsDir().getNumberOfChildFiles(juce::File::findFiles, "*.wav") > 0); }, 30000); settle(); } },
        { "spec.plab.drag", ghosty::demogate::Drag, [&] { if (auto* b = findBtn(procPg->panel(), "DRAG")) dragC(*b); settle(); } },
        { "spec.arcs.midi-drag", ghosty::demogate::Drag, [&] { dragC(comp("arcsMidiDrag")); settle(); } },
        { "spec.arcs.midi-save", ghosty::demogate::File, [&] { clickUp(comp("arcsMidiDrag")); settle(); } },
        { "spec.arcs.audio-render", ghosty::demogate::File, [&] { p.arcsPage.edit(); p.arcsPage.commit("demogate"); clickUp(comp("arcsAudioDrag")); pump(100); waitFor([&] { return !p.arcsPage.rendering(); }, 60000); settle(); } },
        { "spec.arcs.audio-drag", ghosty::demogate::Drag, [&] { if (unlocked() && !p.arcsPage.renderReady()) { clickUp(comp("arcsAudioDrag")); waitFor([&] { return !p.arcsPage.rendering(); }, 60000); } dragC(comp("arcsAudioDrag")); settle(); } },
        { "spec.arcs.loop-wav-drag", ghosty::demogate::Drag, [&] { arcsLoop(true); untilDrag("spec.arcs.loop-wav-drag", [&] { arcsPg->dragLoopOut(false); }, 30000); arcsLoop(false); settle(); } },
        { "spec.arcs.loop-midi-drag", ghosty::demogate::Drag, [&] { arcsLoop(true); arcsPg->dragLoopOut(true); arcsLoop(false); settle(); } },
        { "spec.arcs.loop-prerender", ghosty::demogate::Auto, [&] { arcsLoop(true); waitFor([&] { return refused("spec.arcs.loop-prerender") || arcsPg->loopWavReady(); }, 30000); arcsLoop(false); settle(); } },
        { "spec.arrange.mix-drag", ghosty::demogate::Drag, [&] { untilDrag("spec.arrange.mix-drag", [&] { dragC(comp("arrangeMixChip")); }, 30000); } },
        { "spec.arrange.stems-drag", ghosty::demogate::Drag, [&] { dragC(comp("arrangeStemsChip")); if (unlocked() && G.drags.empty()) { settle(); dragC(comp("arrangeStemsChip")); } settle(); } },
        { "spec.arrange.midi-drag", ghosty::demogate::Drag, [&] { dragC(comp("arrangeMidiChip")); settle(); } },
        { "spec.arrange.loop-wav-drag", ghosty::demogate::Drag, [&] { arrPg->dragLoop(false); if (unlocked() && G.drags.empty()) { settle(); arrPg->dragLoop(false); } settle(); } },
        { "spec.arrange.loop-midi-drag", ghosty::demogate::Drag, [&] { arrPg->dragLoop(true); settle(); } },
        { "spec.arrange.render", ghosty::demogate::File, [&] { if (auto* b = findBtn(*arrPg, p.arrange.arrangement.loopOn ? "RENDER LOOP" : "RENDER")) b->triggerClick(); pump(100); settle(); } },
        { "spec.arrange.send-beats", ghosty::demogate::File, [&] { if (auto* b = findBtn(*arrPg, "SEND TO BEATS")) b->triggerClick(); pump(100); waitFor([&] { return refused("spec.arrange.send-beats") || !arr::Session::listShared().isEmpty(); }, 30000); pump(500); settle(); } },
        { "spec.arrange.auto-mix", ghosty::demogate::Auto, [&] { static int k = 0; p.arrange.arrangement.tracks[0].clips[0].gainDb = -0.1f * (float) (++k); p.arrange.changed(); p.arrange.waitUntilUpToDate(20000);   // a new edit = a new MIX to pre-render
              waitFor([&] { return refused("spec.arrange.auto-mix") || (p.arrange.lastRender && p.arrange.lastRender->revision == p.arrange.audioHashNow(p.arrange.arrangement.loopOn) && !p.arrange.rendering()); }, 30000); settle(); } },
#if GHOSTY_VOICE
        { "spec.voice.wav-drag", ghosty::demogate::Drag, [&] { dragC(comp("voiceWavOut")); settle(); } },
        { "spec.voice.render-set", ghosty::demogate::File, [&] { voiceTab->page().exportForTest(2); pump(100); settle(); } },
        { "spec.voice.render-set-shared", ghosty::demogate::File, [&] { voiceTab->page().exportForTest(3); pump(100); settle(); } },
        { "spec.voice.set-drag", ghosty::demogate::Drag, [&] { voiceTab->page().refreshNow(); dragC(comp("voiceSetOut")); settle(); } },
        { "spec.voice.region-drag", ghosty::demogate::Drag, [&] { if (auto* w = dynamic_cast<ghosty::WaveRegion*>(findRec(ed, "voiceSourceWave"))) w->startExport(); settle(); } },
#endif
    };
    ghosty::License::testForce().store(-1);
    const int f = ghosty::demogate::run("Ghosty Spectrum", paths, kSpecInventory, box);
    edp.reset(); pump(100);
    return f;
}

// `savegate` (also run at the end of `demogate`): while LOCKED the project state keeps the user's own imported sounds but
// not the audio the plugin made (an ARRANGE bounce, the LAB's PROCESS result, the canvas rendered into a morph slot).
// The saved state is decoded and checked; UNLOCKED, everything is there.
static int saveGateTests() {
    ghosty::demogate::Sandbox box;
    int fails = 0; auto check = [&](bool ok, const char* what) { std::printf("  %s  %s\n", ok ? "PASS" : "FAIL", what); if (!ok) ++fails; };
    auto& G = ghosty::exportgate::log();
    ghosty::License::testForce().store(1);
    SpecProcessor p; p.guideSeen = 0x7fffffff; p.prepareToPlay(48000, 512); auto& L = p.lab;
    { juce::AudioBuffer<float> b(2, 512); juce::MidiBuffer m; p.processBlock(b, m); }
    // the user's own files: a noise WAV (ARRANGE import + LAB morph B) and a second one that ARRANGE bounces in place
    auto writeWav = [&](const juce::String& name, float hz) {
        auto f = box.root.getChildFile("home/Music").getChildFile(name); juce::AudioBuffer<float> b(1, 48000);
        for (int i = 0; i < b.getNumSamples(); ++i) b.setSample(0, i, 0.3f * (float) std::sin(2 * juce::MathConstants<double>::pi * hz * i / 48000.0));
        juce::WavAudioFormat wav; std::unique_ptr<juce::OutputStream> os(f.createOutputStream()); if (auto* w = wav.createWriterFor(os.get(), 48000, 1, 24, {}, 0)) { os.release(); std::unique_ptr<juce::AudioFormatWriter> W(w); W->writeFromAudioSampleBuffer(b, 0, b.getNumSamples()); }   // NOT-EXPORT: test fixture
        return f; };
    const auto mine = writeWav("mine.wav", 220), other = writeWav("other.wav", 330);
    const auto idMine = p.arrange.receive(arr::Session::loadSound(mine, "File"), 0, 0);
    const auto idOther = p.arrange.receive(arr::Session::loadSound(other, "File"), 1, 0);
    const auto idBounce = p.arrange.bounce({ idOther }, true);   // BOUNCE IN PLACE: a plugin-made sound
    p.arrange.waitUntilUpToDate(20000);
    juce::String err; L.importAudio(mine, err); L.setProcessed(testSignal(1.0), "processed", false);   // a PROCESS result is the Lab's sound now
    L.loadMorphSound(1, mine, err);                                                                     // B: the user's file
    bool aDone = false; L.useCanvasAs(0, [&] { aDone = true; }); waitFor([&] { return aDone; }, 30000); // A: the canvas, rendered
    waitFor([&] { return !L.busy.load(); }, 20000);
    check(idMine && idBounce, "set-up: an imported ARRANGE clip + a bounced one, a processed LAB sound, morph A rendered / B imported");

    struct Saved { juce::ValueTree lab, sounds; int clips = 0; bool ok = false; };
    auto save = [&](int lock) {
        ghosty::License::testForce().store(lock); G.clear();
        juce::MemoryBlock mb; p.getStateInformation(mb); Saved s;
        if (auto xml = juce::AudioProcessor::getXmlFromBinary(mb.getData(), (int) mb.getSize())) s.lab = juce::ValueTree::fromXml(*xml).getChildWithName("SPECTRALLAB");
        const char tag[4] = { 'G', 'A', 'R', 'R' }; const auto* d = (const char*) mb.getData();
        for (size_t i = 0; i + 8 <= mb.getSize(); ++i) if (std::memcmp(d + i, tag, 4) == 0) {
            juce::int32 n = 0; std::memcpy(&n, d + i + 4, 4); if (n <= 0 || i + 8 + (size_t) n > mb.getSize()) continue;
            auto t = juce::ValueTree::readFromData(d + i + 8, (size_t) n); s.sounds = t.getChildWithName("sounds");
            arr::Arrangement a; s.ok = arr::loadState(d + i + 8, (size_t) n, a); for (auto& tr : a.tracks) s.clips += (int) tr.clips.size(); break; }
        return s; };
    auto soundBy = [](const juce::ValueTree& sounds, const juce::String& page) { for (auto c : sounds) if (c["page"].toString() == page) return c; return juce::ValueTree(); };
    auto flacOk = [](const juce::ValueTree& s) { juce::AudioBuffer<float> a; double sr = 0; auto* fl = s["flac"].getBinaryData(); return fl && arr::decodeFlac(*fl, a, sr, (int) s.getProperty("exp", 0)) && a.getNumSamples() > 1000; };

    const auto Lk = save(0);
    const auto bLk = soundBy(Lk.sounds, "Bounce"), fLk = soundBy(Lk.sounds, "File");
    std::printf("locked save: bounce sound flac=%d stripped=%d, imported flac=%d, clips on reload %d; lab source=%d a=%d b=%d; stripped log %d\n",
                bLk.hasProperty("flac") ? 1 : 0, (bool) bLk["stripped"] ? 1 : 0, flacOk(fLk) ? 1 : 0, Lk.clips, Lk.lab.hasProperty("source") ? 1 : 0, Lk.lab.hasProperty("audioA") ? 1 : 0, Lk.lab.hasProperty("audioB") ? 1 : 0, (int) G.stripped.size());
    check(bLk.isValid() && !bLk.hasProperty("flac") && !bLk.hasProperty("srcState") && !bLk.hasProperty("notes") && (bool) bLk["stripped"], "LOCKED: the ARRANGE bounce is saved without its audio (no FLAC, no re-render state)");
    check(flacOk(fLk) && Lk.ok && Lk.clips == 1, "LOCKED: the imported ARRANGE clip keeps its audio (decodes); only it comes back on load");
    check(!Lk.lab.hasProperty("source") && (bool) Lk.lab["sourceStripped"] && !Lk.lab.hasProperty("audioA") && Lk.lab.hasProperty("audioB"), "LOCKED: the LAB's PROCESS result + rendered morph A left out, the imported morph B kept");
    check(ghosty::exportgate::Log::has(G.stripped, "save:spec.arrange-clips") && ghosty::exportgate::Log::has(G.stripped, "save:spec.lab-results"), "LOCKED: the save reports what it left out");
    const auto Un = save(1);
    const auto bUn = soundBy(Un.sounds, "Bounce"), fUn = soundBy(Un.sounds, "File");
    check(flacOk(bUn) && (bool) bUn["made"] && flacOk(fUn) && !(bool) fUn["made"] && Un.clips == 2, "UNLOCKED: both ARRANGE sounds saved with audio, provenance 'made' on the bounce only");
    check(Un.lab.hasProperty("source") && (bool) Un.lab["sourceMade"] && Un.lab.hasProperty("audioA") && Un.lab.hasProperty("audioB") && G.stripped.empty(), "UNLOCKED: the LAB saves everything, nothing reported stripped");
    {   // old states (no "made") load as imported: a licensed user's project is untouched
        auto t = arr::toTree(p.arrange.arrangement); auto ss = t.getChildWithName("sounds"); for (auto c : ss) c.removeProperty("made", nullptr);
        arr::Arrangement a; arr::fromTree(t, a); bool anyMade = false; for (auto& tr : a.tracks) for (auto& c : tr.clips) anyMade |= c.source && c.source->rendered;
        ghosty::License::testForce().store(0); int n = 0; auto t2 = arr::toTree(a, false, &n);
        check(!anyMade && n == 0, "back-compat: a state without provenance loads as imported (nothing stripped from it)");
    }
    ghosty::License::testForce().store(-1);
    std::printf("savegate Ghosty Spectrum: %s (%d failure%s)\n", fails == 0 ? "PASS" : "FAIL", fails, fails == 1 ? "" : "s");
    return fails;
}
