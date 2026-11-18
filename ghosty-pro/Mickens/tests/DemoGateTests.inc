// MickensTests demogate: every way audio or MIDI can leave Mr. Mickens (docs/DEMO-EXPORT-GATE.md), triggered through the real
// editor (mouse down on the drag control -> the worker render -> the drag), LOCKED then UNLOCKED (Shared/tests/DemoGateHarness.h).
#include "tests/DemoGateHarness.h"

static constexpr int kMickInventory = 5;

static int demogateTest() {
    ghosty::demogate::Sandbox box;   // FIRST: HOME / TMPDIR -> a fresh folder (renders land in TMPDIR/MrMickens)
    MickensProcessor p; waitReady(p);
    std::unique_ptr<juce::AudioProcessorEditor> edp(p.createEditor()); auto* ed = edp.get(); ed->setVisible(true); pump(300);
    { juce::AudioBuffer<float> buf(2, 512); juce::MidiBuffer m; p.processBlock(buf, m); }
    p.secA = 2; p.secB = 4;   // a LOOP SECTION, so the ruler's MIDI / WAV grips show
    if (auto* r = findId(ed, "barRuler")) { r->resized(); r->repaint(); }
    pump(100);
    auto& L = ghosty::exportgate::log();
    // the drag control's gesture: mouse down (the gate, then the render on a worker thread), then drags until the files are ready
    auto dragOut = [&](const char* id) {
        auto* c = findId(ed, id); if (c == nullptr) { std::printf("  (no component %s)\n", id); return; }
        c->setVisible(true);
        c->mouseDown(mouseAt(*c, 4));
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        while (L.drags.empty() && juce::Time::getMillisecondCounterHiRes() - t0 < 30000) {
            c->mouseUp(mouseAt(*c, 4)); c->mouseDrag(mouseAt(*c, 20, {}, 1, { 4, c->getHeight() * 0.5f })); pump(20);
            if (L.drags.empty() && ! ghosty::exportUnlocked(p.license) && juce::Time::getMillisecondCounterHiRes() - t0 > 400) break;   // locked: nothing to wait for
        }
        c->mouseUp(mouseAt(*c, 4)); pump(30);
    };
    std::vector<ghosty::demogate::Path> paths {
        { "mick.mix.wav", ghosty::demogate::Drag, [&] { dragOut("dragWav"); } },
        { "mick.stems.wav", ghosty::demogate::Drag, [&] { dragOut("dragStems"); } },
        { "mick.bay.midi", ghosty::demogate::Drag, [&] { dragOut("bayMidi0"); } },
        { "mick.section.midi", ghosty::demogate::Drag, [&] { dragOut("sectionMidi"); } },
        { "mick.section.wav", ghosty::demogate::Drag, [&] { dragOut("sectionWav"); } },
    };
    const int fails = ghosty::demogate::run("Mr. Mickens", paths, kMickInventory, box);
    edp.reset(); pump(50);
    return fails;
}
