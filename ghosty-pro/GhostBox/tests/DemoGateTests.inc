// Ghost Box `demogate` (GhostBoxTests demogate). Ghost Box is an effect on the DAW's own track; its presets, scenes, boards, snapshots
// and motion are its own XML formats. Since GHOST CAPTURE it has 3 ways to get audio out as a file, all through the CAPTURE page's
// real UI code: a tile dragged into the DAW (box.capture.drag), SAVE AS... (box.capture.save) and SLICE (box.capture.slice: the WAV +
// its .ghostclip.json, both dragged). The inventory is 3 paths (docs/DEMO-EXPORT-GATE.md); the lint (ExportGateLint) fails if a GhostBox /
// GhostyPro/fx source ever adds an ungated drag / save dialog / writer. The demo also has the drop-out (2 s of silence every 20 s while
// locked), checked here on a real render.
#include "../../Shared/tests/DemoGateHarness.h"

static constexpr int kBoxInventory = 3;

static int boxDemoGate()
{
    ghosty::demogate::Sandbox box;
    int fails = 0;
    {   // the editor opens locked and unlocked; nothing is written or dragged by just using it
        for (int lock : { 0, 1 })
        {
            ghosty::License::testForce().store (lock);
            BoxProcessor p; p.setRateAndBufferSizeDetails (48000, 512); p.prepareToPlay (48000, 512);
            const auto before = box.snapshot(); ghosty::exportgate::log().clear();
            std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor()); ed->setSize (1180, 780); ghosty::demogate::pump (300);
            ed.reset();
            const bool clean = box.exportsSince (before).isEmpty() && ghosty::exportgate::log().drags.empty() && ghosty::exportgate::log().saves.empty();
            std::printf ("box editor %s: %s\n", lock ? "unlocked" : "locked", clean ? "no audio / MIDI file, no drag" : "WROTE OR DRAGGED SOMETHING");
            if (! clean) ++fails;
        }
        ghosty::License::testForce().store (-1);
    }
    // GHOST CAPTURE: a real processor that played 4 s, one capture on the shelf, the editor on the CAPTURE page
    ghosty::License::testForce().store (1);
    BoxProcessor p; p.setPlayConfigDetails (2, 2, 48000, 512); p.prepareToPlay (48000, 512);
    {
        juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer m;
        for (int k = 0; k < 400; ++k)
        {
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) b.setSample (c, i, 0.25f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * (double) (k * 512 + i) / 48000.0));
            p.processBlock (b, m);
        }
    }
    p.captureBarsIdx = 0; p.captureNow();
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor()); ghosty::demogate::pump (100);
    juce::Component* panel = nullptr; for (auto* c : ed->getChildren()) if (dynamic_cast<gpfx::Panel*> (c) != nullptr) panel = c;
    if (panel != nullptr) { panel->getProperties().set ("mode", 6); panel->resized(); ghosty::demogate::pump (60); }
    BoxCapturePage* page = nullptr; std::function<void (juce::Component&)> find = [&] (juce::Component& c) { for (auto* k : c.getChildren()) { if (auto* pg = dynamic_cast<BoxCapturePage*> (k)) page = pg; find (*k); } };
    find (*ed);
    const bool ready = page != nullptr && page->tileCount() == 1;
    std::printf ("box CAPTURE page with one clip: %s\n", ready ? "ready" : "MISSING");
    if (! ready) ++fails;
    std::vector<ghosty::demogate::Path> paths {
        { "box.capture.drag", ghosty::demogate::Drag, [&] { if (page) page->tileDragOut (0); } },
        { "box.capture.save", ghosty::demogate::File, [&] { if (page) page->tileSaveAs (0); } },
        { "box.capture.slice", ghosty::demogate::Drag, [&] { if (page) page->tileSlice (0); } },
    };
    fails += ghosty::demogate::run ("Ghost Box", paths, kBoxInventory, box);
    ed.reset();
    fails += ghosty::demogate::checkDropout ("Ghost Box", [] { return std::unique_ptr<juce::AudioProcessor> (new BoxProcessor()); },
        [] (juce::AudioBuffer<float>& b, juce::MidiBuffer&, juce::int64 pos) {
            for (int c = 0; c < b.getNumChannels(); ++c) for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (c, i, 0.25f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * (double) (pos + i) / 48000.0));
        });
    return fails;
}
