// DEMO EXPORT GATE (docs/DEMO-EXPORT-GATE.md, Shared/ExportGate.h): `ProTests demogate`
// Every way audio leaves Ghosty Pro is tried LOCKED (refused, hint, nothing written) and UNLOCKED (it exports), through the real
// WAVETABLE EDITOR. The paths table below IS the inventory (ExportGateLint checks it against the gate ids in the sources).
#include "../../Shared/tests/DemoGateHarness.h"

namespace demogatetest
{
static constexpr int kProInventory = 3;

// .ghostywt files new or changed since `before` (the harness counts audio / MIDI types; a .ghostywt is PCM too)
static int ghostywtChanged (const ghosty::demogate::Sandbox& box, const std::map<juce::String, juce::int64>& before)
{
    int n = 0;
    for (const auto& [path, v] : box.snapshot()) if (path.endsWith (".ghostywt")) { auto it = before.find (path); if (it == before.end() || it->second != v) ++n; }
    return n;
}

static int run()
{
    ghosty::demogate::Sandbox box;                       // FIRST: HOME / TMPDIR / XDG_* -> a fresh folder
    auto p = makeProc (-1); p->initPatch(); settle (*p);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());   // the real editor (its UNLOCK button, theme, look)
    ed->setVisible (true); pump (50);
    ProLnF lnf; WavetableEditor w (*p, 0); w.setLookAndFeel (&lnf); w.setSize (WavetableEditor::W, WavetableEditor::H); w.setVisible (true); pump (50);
    w.op ("gentable"); pump (250);
    int extra = 0; int n = 0;
    auto clearBank = [] { for (auto& f : gpro::wt::listUserTables()) f.deleteFile(); };   // (SAVE overwrites: start from an empty bank so a write shows)
    auto saveAsRound = [&] { w.testPress ("saveas"); pump (30); w.testAnswerSaveAs ("demogate table " + juce::String (++n)); pump (60); };
    std::vector<ghosty::demogate::Path> paths {
        { "pro.wt.export-wav", ghosty::demogate::File, [&] { w.testPress ("export"); pump (30); } },
        { "pro.wt.save", ghosty::demogate::File, [&] { clearBank(); w.testPress ("save"); pump (30); } },
        { "pro.wt.save-as", ghosty::demogate::File, [&] { saveAsRound(); } },
    };
    int fails = ghosty::demogate::run ("Ghosty Pro", paths, kProInventory, box);
    // .ghostywt (float32 PCM in base64) is gated like a WAV: check it by name too
    {
        ghosty::License::testForce().store (0);
        clearBank(); auto before = box.snapshot();
        w.testPress ("save"); pump (30); saveAsRound();
        const int lockedNew = ghostywtChanged (box, before);
        ghosty::License::testForce().store (1);
        clearBank(); before = box.snapshot();
        w.testPress ("save"); pump (30); saveAsRound();
        const int unlockedNew = ghostywtChanged (box, before);
        ghosty::License::testForce().store (-1);
        std::printf ("  .ghostywt (SAVE + SAVE AS): locked wrote %d, unlocked wrote %d\n", lockedNew, unlockedNew);
        if (lockedNew != 0) { ++extra; std::printf ("  FAIL a .ghostywt was written in demo mode\n"); }
        if (unlockedNew < 2) { ++extra; std::printf ("  FAIL SAVE / SAVE AS wrote no .ghostywt when unlocked\n"); }
    }
    w.setLookAndFeel (nullptr);
    ed.reset(); pump (20);
    std::printf ("\n%d failures\n", fails + extra);
    return fails + extra;
}
// LOCKED SAVES (`ProTests savegate`): presets / snapshots / boards / the project stay allowed in the demo, but a WAVETABLE EDITOR
// table the plugin made (generated, drawn, seeded from a built-in table) is left out of them while locked; a table imported
// from the user's own sample stays. Unlocked: both are saved (with made="1" on the plugin-made one).
static int savegate()
{
    ghosty::demogate::Sandbox box;
    auto p = makeProc (-1); p->initPatch(); settle (*p);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditorIfNeeded()); ed->setVisible (true); pump (50);   // (as a host does: the active editor shows the hint)
    ProLnF lnf; WavetableEditor w (*p, 0); w.setLookAndFeel (&lnf); w.setSize (WavetableEditor::W, WavetableEditor::H); pump (20);
    int fails = 0;
    auto check = [&] (bool ok, const juce::String& what) { std::printf ("  %s %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8()); if (! ok) ++fails; };
    // a user sample on disk (the user's own file): two cycles of a bright tone
    const auto wav = box.root.getChildFile ("my sample.wav");
    {
        juce::AudioBuffer<float> b (1, 48000); for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (0, i, 0.5f * (float) (std::sin (i * 0.0571) + 0.3 * std::sin (i * 0.1713)));
        juce::WavAudioFormat fmt; std::unique_ptr<juce::FileOutputStream> os (wav.createOutputStream());
        std::unique_ptr<juce::AudioFormatWriter> wr (fmt.createWriterFor (os.get(), 48000, 1, 24, {}, 0)); os.release(); wr->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());   // NOT-EXPORT: the test's own input file
    }
    auto userTables = [] (const juce::ValueTree& t) { int n = 0, made = 0; std::function<void (const juce::ValueTree&)> f = [&] (const juce::ValueTree& v) {
        for (auto c : v) { if (c.hasType ("USERTABLE") && c.getProperty ("data").toString().length() > 100) { ++n; if ((bool) c.getProperty ("made", false)) ++made; } f (c); } }; f (t); return std::pair<int, int> { n, made }; };
    auto fromXmlFile = [] (const juce::File& f) { auto x = juce::parseXML (f); return x != nullptr ? juce::ValueTree::fromXml (*x) : juce::ValueTree(); };
    auto stateTree = [&] { juce::MemoryBlock mb; p->getStateInformation (mb); auto x = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize()); return x != nullptr ? juce::ValueTree::fromXml (*x) : juce::ValueTree(); };
    int round = 0;
    auto saves = [&] (const char* label, bool made, bool locked) {
        ghosty::License::testForce().store (locked ? 0 : 1);
        while (! p->morphBoard.ghosts.empty()) p->morphRemove (0);   // one ghost (this round's sound) on the board
        ghosty::exportgate::log().clear();
        const auto nm = juce::String ("savegate ") + juce::String (++round);
        const bool presetOk = p->saveUserPreset (nm);
        const auto preset = userTables (fromXmlFile (ProProcessor::userPresetDir().getChildFile (nm + ".ghostypro")));
        const int si = p->snapshots->take (nm); const auto snap = si >= 0 ? userTables (fromXmlFile (p->snapshots->info (si).file)) : std::pair<int, int> { -1, -1 };
        p->morphAddGhost (0.3f, 0.3f, p->captureSound(), nm); const bool boardOk = p->saveBoard (nm);
        const auto board = userTables (fromXmlFile (ProProcessor::boardDir().getChildFile (nm + ".ghostyboard")));
        const auto state = userTables (stateTree());
        const bool keep = ! (locked && made);
        const int want = keep ? 1 : 0;
        const bool clickLogged = ghosty::exportgate::Log::has (ghosty::exportgate::log().stripped, "save:pro.user-wavetable");
        const bool stateLogged = ghosty::exportgate::Log::has (ghosty::exportgate::log().stripped, "save:pro.user-wavetable.state");
        const bool hinted = ghosty::exportgate::Log::has (ghosty::exportgate::log().hinted, "save:pro.user-wavetable");
        std::printf ("  %-26s preset %d/%d  snapshot %d/%d  board %d/%d  project %d/%d  (tables/made)\n", label, preset.first, preset.second, snap.first, snap.second, board.first, board.second, state.first, state.second);
        check (presetOk && boardOk && si >= 0, juce::String (label) + ": the preset, snapshot and board saves still work");
        check (preset.first == want && snap.first == want && board.first == want && state.first >= want && (keep || state.first == 0),
               juce::String (label) + (keep ? ": the table is saved" : ": the plugin-made table is left out of every save"));
        check (! keep || preset.second == (made ? 1 : 0), juce::String (label) + ": provenance saved (made=\"1\" only on a plugin-made table)");
        check (keep ? (! clickLogged && ! stateLogged) : (clickLogged && stateLogged && hinted), juce::String (label) + (keep ? ": nothing left out, no hint" : ": the click saves show the UNLOCK hint (the project save is silent)"));
        check (p->userTable().valid(), juce::String (label) + ": the live sound keeps its table");
        ghosty::License::testForce().store (-1);
    };
    // 1) a plugin-made table (GENERATE TABLE in the editor)
    w.op ("gentable"); p->setUserTable (w.document());
    check (w.document().made, "GENERATE TABLE marks the table plugin-made");
    saves ("made, LOCKED", true, true);
    saves ("made, UNLOCKED", true, false);
    // the made flag survives a state round trip, and drawing into an imported table makes it plugin-made
    // 2) a table imported from the user's own sample
    check (w.importFile (wav, 0) && ! w.document().made, "IMPORT from the user's sample: not plugin-made");
    p->setUserTable (w.document());
    saves ("imported, LOCKED", false, true);
    saves ("imported, UNLOCKED", false, false);
    { auto d = w.document(); w.drawPoint (0.2f, 0.5f, true); w.drawPoint (0.3f, -0.5f, false); w.endDraw(); check (w.document().made && ! d.made, "drawing into an imported table makes it plugin-made"); }
    { w.seedFromOsc(); check (w.document().made, "START FROM the oscillator's built-in table: plugin-made"); }
    {   // .ghostywt keeps the provenance; older files (no flag) load as imported
        w.op ("gentable"); const auto f = box.root.getChildFile ("t.ghostywt"); gpro::wt::save (w.document(), f); gpro::wt::Doc d; check (gpro::wt::load (f, d) && d.made, ".ghostywt keeps made=\"1\"");
        w.importFile (wav, 0); gpro::wt::save (w.document(), f); gpro::wt::Doc e; check (gpro::wt::load (f, e) && ! e.made, ".ghostywt of an imported table loads as imported");
    }
    w.setLookAndFeel (nullptr);
    ed.reset(); pump (20);
    std::printf ("\nsavegate: %d failures\n", fails);
    return fails;
}
} // namespace demogatetest
