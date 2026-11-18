// Poltergeist `demogate` (PoltergeistTests demogate): every way audio or MIDI can leave Poltergeist (docs/DEMO-EXPORT-GATE.md).
// Since the sample pages left (owner 2026-10-26: SAMPLE, SAMPLE LAB, PROCESS LAB and VOCAL CHOP moved to Ghost Slice) there is
// none: the inventory is 0 paths. The check: an editor with a sample, an old project's Sample Lab / CHOP data and the ADD SAMPLE
// waveform in use writes and drags nothing, locked or unlocked (the slot's waveform has no WAV grip, no gate is wired).
// Shared harness: Shared/tests/DemoGateHarness.h.
#include "../../Shared/tests/DemoGateHarness.h"
#include "WaveRegion.h"

static constexpr int kPoltInventory = 0;

static std::shared_ptr<gs::SampleData> demogateTone(double secs) {
    auto s = std::make_shared<gs::SampleData>(); s->rate = 44100; s->name = "tone";
    s->data.resize((size_t) (secs * s->rate));
    for (size_t i = 0; i < s->data.size(); ++i) s->data[i] = 0.4f * (float) std::sin(2 * juce::MathConstants<double>::pi * 220.0 * (double) i / s->rate) * (((i / 11025) % 2) ? 1.0f : 0.3f);
    s->buildFrames();
    return s;
}
static juce::MouseEvent dgEv(juce::Component& c, juce::Point<float> pos, juce::Point<float> down, juce::ModifierKeys m, bool dragged) {
    auto src = juce::Desktop::getInstance().getMainMouseSource(); const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent(src, pos, m, 1.0f, 0, 0, 0, 0, &c, &c, now, down, now, 1, dragged);
}
// press, drag 40 px, release (the gesture every drag handle starts on)
static void dgDrag(juce::Component* c, juce::Point<float> at, juce::ModifierKeys m = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier)) {
    if (c == nullptr) { std::printf("  (missing component)\n"); return; }
    const auto to = at.translated(40, 0);
    c->mouseDown(dgEv(*c, at, at, m, false)); c->mouseDrag(dgEv(*c, to, at, m, true)); c->mouseUp(dgEv(*c, to, at, m, true));
}

static int demogateTests() {
    ghosty::demogate::Sandbox box;   // FIRST: HOME / TMPDIR / XDG -> a fresh folder (every temp WAV / MIDI would land in it)
    int fails = 0;
    for (int lock : { 0, 1 }) {
        ghosty::License::testForce().store(lock);
        PoltergeistProcessor p; p.setRateAndBufferSizeDetails(44100, 512); p.prepareToPlay(44100, 512);
        p.addSample(demogateTone(2.0));                                            // ADD SAMPLE
        const int sid = p.lab.addSource(demogateTone(2.0)); p.lab.bpm = 120;      // an old project's Sample Lab pieces + CHOP data
        p.lab.addClip(sid, 0.0, 0.5, 0.0); p.lab.addClip(sid, 0.5, 1.25, 1.0); p.labRenderNow();
        p.chopLoad(demogateTone(2.0), false); p.chopRenderNow();
        const auto before = box.snapshot(); ghosty::exportgate::log().clear();
        std::unique_ptr<PoltergeistEditor> ed(dynamic_cast<PoltergeistEditor*>(p.createEditor())); ed->setSize(1040, 744);
        ed->selectGhost(7); ed->testRefresh();
        auto* w = ed->testSampleWave();
        { juce::Image im(juce::Image::ARGB, 1040, 744, true); juce::Graphics g(im); ed->paintEntireComponent(g, false); }   // (syncs the peaks)
        const bool noGrip = w != nullptr && w->hasWave() && w->gripRect().isEmpty();
        if (w) { auto r = w->region(); r.loopOn = false; w->setRegion(r); const auto wa = w->waveArea(); dgDrag(w, { wa.getRight() - 30, wa.getY() + 10 }); dgDrag(w, wa.getCentre()); }   // where a WAV grip would be + the middle
        for (int pg = 0; pg < 10; ++pg) ed->testShow(pg, 0, 0);
        ghosty::demogate::pump(200);
        ed.reset();
        const bool clean = box.exportsSince(before).isEmpty() && ghosty::exportgate::log().drags.empty() && ghosty::exportgate::log().saves.empty();
        std::printf("Poltergeist editor %s: %s, ADD SAMPLE waveform without a WAV grip: %s\n", lock ? "unlocked" : "locked", clean ? "no audio / MIDI file, no drag" : "WROTE OR DRAGGED SOMETHING", noGrip ? "yes" : "NO");
        if (!clean || !noGrip) ++fails;
    }
    ghosty::License::testForce().store(-1);
    std::vector<ghosty::demogate::Path> paths;   // none: see above
    fails += ghosty::demogate::run("Poltergeist", paths, kPoltInventory, box);
    return fails;
}
