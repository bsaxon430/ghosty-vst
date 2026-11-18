// Ghosty Beats `demogate` (BeatsTests demogate): every way audio / MIDI leaves the plugin (docs/DEMO-EXPORT-GATE.md),
// triggered through the real editor. LOCKED: refused, hint, no drag / dialog / file / clipboard. UNLOCKED: it exports.
#include "../../Shared/tests/DemoGateHarness.h"

static constexpr int kBeatsInventory = 12;

static int demoGateTest()
{
    ghosty::demogate::Sandbox box;   // first: HOME / TMPDIR / XDG_* -> a fresh folder
    BeatsXProcessor p; p.applyKit(0, true);
    // a melody with notes (MELODY MIDI / XM / IT have something to write)
    p.melody.on = true; p.melody.inst[0].sound = p.model.tracks[0].sound; p.melody.inst[0].route = 0;
    for (int r = 0; r < 16; r += 4) { auto& cl = p.melody.pat(0).cells[0][(size_t) r]; cl.note = 49 + r; cl.inst = 1; }
    p.model.tracks[0].slices = { 0, 2000, 4000, 6000 };
    p.publish();
    std::unique_ptr<juce::AudioProcessorEditor> e(p.createEditor()); auto* be = dynamic_cast<BeatsXEditor*>(e.get());
    if (be == nullptr) { std::printf("demogate: no editor\n"); return 1; }
    be->showPageForTest(0); be->showSlicesForTest(false);
    auto ev = [](juce::Component* c, juce::Point<float> at, float dy) {
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at.translated(0, dy), juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                c, c, juce::Time::getCurrentTime(), at, juce::Time::getCurrentTime(), 1, dy != 0); };
    auto dragOn = [&](juce::Component* c, juce::Point<float> at) { if (! c) return; c->mouseDown(ev(c, at, 0)); c->mouseDrag(ev(c, at, 30)); c->mouseUp(ev(c, at, 30)); };
    auto dragPart = [&](const char* name) { auto* c = be->partForTest(name); if (c) dragOn(c, c->getLocalBounds().toFloat().getCentre()); };
    auto stems = [&](bool on) { if (auto* b = dynamic_cast<juce::Button*>(be->partForTest("stems"))) b->setToggleState(on, juce::dontSendNotification); };
    auto trackHandle = [&](bool midi) { auto* tl = dynamic_cast<bx::TrackList*>(be->partForTest("tl0")); if (! tl) return; be->showPageForTest(0); dragOn(tl, midi ? tl->midiHandle(0).getCentre() : tl->wavHandle(0).getCentre()); };
    auto grip = [&] { be->showPageForTest(0); be->showSlicesForTest(false); auto* w = be->regionForTest(); if (w) dragOn(w, w->gripRect().getCentre()); };
    auto slice = [&] { be->showPageForTest(0); be->showSlicesForTest(true); auto* sv = be->slicesForTest(); if (! sv) return; sv->setSize(240, 80); sv->performDrag = nullptr; dragOn(sv, { 150.0f, 40.0f }); };
    auto soundGrip = [&] { be->showPageForTest(1); auto* sl = dynamic_cast<bx::SoundList*>(be->partForTest("sounds")); if (! sl || sl->items.empty()) return; if (sl->getWidth() < 300) sl->setSize(495, 200); sl->performDrag = nullptr; dragOn(sl, sl->grip(0).getCentre().toFloat()); be->showPageForTest(0); };
    std::vector<ghosty::demogate::Path> paths {
        { "beats.drag-midi", ghosty::demogate::Drag, [&] { dragPart("midiOut"); } },
        { "beats.drag-audio", ghosty::demogate::Drag, [&] { stems(false); dragPart("audioOut"); } },
        { "beats.drag-stems", ghosty::demogate::Drag, [&] { stems(true); dragPart("audioOut"); stems(false); } },
        { "beats.drag-melody-midi", ghosty::demogate::Drag, [&] { dragPart("melOut"); } },
        { "beats.drag-beat-melody-midi", ghosty::demogate::Drag, [&] { dragPart("melBoth"); } },
        { "beats.track-midi-drag", ghosty::demogate::Drag, [&] { trackHandle(true); } },
        { "beats.track-wav-drag", ghosty::demogate::Drag, [&] { trackHandle(false); } },
        { "beats.region-wav-drag", ghosty::demogate::Drag, [&] { grip(); } },
        { "beats.slice-wav-drag", ghosty::demogate::Drag, [&] { slice(); } },
        { "beats.sound-wav-drag", ghosty::demogate::Drag, [&] { soundGrip(); } },
        { "beats.export-xm", ghosty::demogate::File, [&] { be->exportModuleForTest(false); } },
        { "beats.export-it", ghosty::demogate::File, [&] { be->exportModuleForTest(true); } },
    };
    const int fails = ghosty::demogate::run("Ghosty Beats", paths, kBeatsInventory, box);
    // SAVE BEAT is the plugin's own project format: it works in the demo too
    ghosty::License::testForce().store(0); const bool saved = p.saveBeat("demogate beat"); ghosty::License::testForce().store(-1);
    std::printf("  SAVE BEAT (.ghostbeat, own format) in the demo: %s\n", saved ? "saved (allowed)" : "REFUSED");
    e.reset();
    return fails + (saved ? 0 : 1);
}
