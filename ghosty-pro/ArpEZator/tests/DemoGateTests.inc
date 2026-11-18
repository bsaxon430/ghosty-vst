// ArpEZator `demogate` (ArpEZatorTests demogate): every way MIDI or audio can leave ArpEZator, through the real editor code,
// LOCKED = refused with the UNLOCK hint and nothing written / dragged; UNLOCKED = it exports. docs/DEMO-EXPORT-GATE.md.
#include "../../Shared/tests/DemoGateHarness.h"

static constexpr int kArpInventory = 11;

struct ArpEditorTestAccess
{
    static arpui::DragPill& lanePill (arpui::ArpEditor& e, int l) { return *e.lanePills[l]; }
    static ghosty::tempomul::Switch& tempo (arpui::ArpEditor& e) { return e.tempoSw; }   // (TEMPO tests)
    static arpui::DragPill& all (arpui::ArpEditor& e) { return *e.dragAll; }
    static arpui::DragPill& audio (arpui::ArpEditor& e) { return *e.dragAudio; }
    static bool rendering (arpui::ArpEditor& e) { return e.audioJob.running; }
    static void stems (arpui::ArpEditor& e, bool on) { e.mixStems.sel = on ? 1 : 0; }
    static void prerender (arpui::ArpEditor& e) { e.prerenderSection (true); }
    static arpui::Btn& lockSel (arpui::ArpEditor& e) { return e.lockSel; }          // (EDITED CHORDS tests)
    static arpui::Seg& voicing (arpui::ArpEditor& e) { return e.voicingSeg; }
    static arpui::Seg& chordRhythm (arpui::ArpEditor& e) { return e.chordRhythm; }
    static arpui::Seg& arpRate (arpui::ArpEditor& e) { return e.rateSeg; }
    static juce::OwnedArray<arpui::Btn>& tools (arpui::ArpEditor& e) { return e.tools; }      // (bug-fix tests: lane tool tooltips)
    static arpui::Seg& chordLen (arpui::ArpEditor& e) { return e.chordLen; }
};


// the demo drop-out on the MIDI OUT: while locked no note-on leaves in the 2 s window every 20 s (18-20 s, the same clock as
// the audio's silence); note-offs still do; unlocked the notes keep coming
static int arpMidiDropout()
{
    struct Res { int onsGap = 0, onsBefore = 0, onsAfter = 0, offsGap = 0; double audioGap0 = -1, audioGap1 = -1; };
    auto run = [] (bool locked) {
        ghosty::License::testForce().store (locked ? 0 : 1);
        ArpProcessor p; p.persistTaste = false;
        p.change ([] (Idea& X) { X.mode = Midnight; X.seed = 4321; applyModeDefaults (X); newIdea (X); for (auto& L : X.lanes) L.mute = false; }, false);
        setP (p, "midiOut", 1);
        const double sr = 48000, bpm = 124; const int blk = 480;
        FakeHead head; head.bpm = bpm; p.setPlayHead (&head); p.setRateAndBufferSizeDetails (sr, blk); p.prepareToPlay (sr, blk);
        Res r; bool inQuiet = false;
        for (juce::int64 pos = 0; pos < (juce::int64) (22.0 * sr); pos += blk)
        {
            head.ppq = pos / sr * bpm / 60.0; head.samples = pos; head.playing = true;
            juce::AudioBuffer<float> b (2, blk); b.clear(); juce::MidiBuffer m; p.processBlock (b, m);
            for (const auto meta : m)
            {
                const double t = (double) (pos + meta.samplePosition) / sr; const auto x = meta.getMessage();
                const bool gap = t >= 18.0 && t < 20.0;
                if (x.isNoteOn()) { if (gap) ++r.onsGap; else if (t >= 16.0 && t < 18.0) ++r.onsBefore; else if (t >= 20.0) ++r.onsAfter; }
                else if (x.isNoteOff() && gap) ++r.offsGap;
            }
            const double t = (double) pos / sr; const bool quiet = b.getMagnitude (0, 0, blk) < 1.0e-6f;
            if (t > 1.0 && quiet && ! inQuiet && r.audioGap0 < 0 && t > 10.0) { r.audioGap0 = t; inQuiet = true; }
            if (inQuiet && ! quiet && r.audioGap1 < 0) { r.audioGap1 = t; inQuiet = false; }
        }
        p.setPlayHead (nullptr);
        return r;
    };
    const auto L = run (true), U = run (false);
    ghosty::License::testForce().store (-1);
    std::printf ("midi dropout locked: %d note-ons in 18-20 s (before %d, after %d, note-offs in the gap %d); audio silent %.2f-%.2f s\n", L.onsGap, L.onsBefore, L.onsAfter, L.offsGap, L.audioGap0, L.audioGap1);
    std::printf ("midi dropout unlocked: %d note-ons in 18-20 s (before %d, after %d)\n", U.onsGap, U.onsBefore, U.onsAfter);
    const bool ok = L.onsGap == 0 && L.onsBefore > 0 && L.onsAfter > 0 && U.onsGap > 0 && std::abs (L.audioGap0 - 18.0) < 0.1 && std::abs (L.audioGap1 - 20.0) < 0.1;
    std::printf ("midi dropout ArpEZator: %s\n", ok ? "PASS (no note-on in the audio's 2 s hole while locked; notes flow when unlocked)" : "FAIL");
    return ok ? 0 : 1;
}

static int arpDemoGate()
{
    ghosty::demogate::Sandbox box;   // HOME / TMPDIR -> a fresh folder before anything touches the disk
    using ghosty::demogate::pump; using A = ArpEditorTestAccess;
    ArpProcessor p; p.persistTaste = false;
    p.change ([] (Idea& X) { X.mode = Midnight; X.seed = 4321; applyModeDefaults (X); newIdea (X); for (auto& L : X.lanes) L.mute = false; }, false);
    p.setRateAndBufferSizeDetails (48000, 256); p.prepareToPlay (48000, 256);
    std::unique_ptr<ArpEditor> ed (dynamic_cast<ArpEditor*> (p.createEditor())); ed->setSize (1240, 820);
    { std::vector<float> x[2] = { smptest::tone (220.0, 1.0, 3), {} }; juce::String m; p.loadSampleAudio (x, 1, 48000.0, "demogate sample", {}, m); }
    p.labGenerate (4);
    pump (200);

    const auto left = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier);
    auto centre = [] (juce::Component& c) { return c.getLocalBounds().toFloat().getCentre(); };
    auto dragPill = [&] (arpui::DragPill& d) {   // a real drag gesture on a DRAG pill (down, move 20 px, up)
        const auto c = centre (d); d.mouseDown (mev (d, c, c, left)); d.mouseDrag (mev (d, c + juce::Point<float> (20, 0), c, left, true)); d.mouseUp (mev (d, c + juce::Point<float> (20, 0), c, {}, true)); };
    auto clickPill = [&] (arpui::DragPill& d) { const auto c = centre (d); d.mouseDown (mev (d, c, c, left)); d.mouseUp (mev (d, c, c, {})); };
    auto waitRender = [&] { pump (20000, [&] { return ! A::rendering (*ed); }); pump (50); };
    auto freshAudio = [&] { p.version.fetch_add (1); };   // a new render key (the cached WAV belongs to the last one)
    int secK = 0;
    auto withSection = [&] (std::function<void()> f) { const int k = (secK++ % 4); p.setSection (k * BAR, (k + 2) * BAR); p.setSectionOn (true); ed->refresh(); f(); p.clearSection(); ed->refresh(); };
    auto dragGrip = [&] (bool audio) {    // the roll's loop grip (MIDI / WAV) dragged out of the plugin
        auto& r = ed->roll; const auto g = r.gripRect (audio).getCentre();
        r.mouseDown (mev (r, g, g, left)); r.mouseDrag (mev (r, g + juce::Point<float> (0, 30), g, left, true)); r.mouseUp (mev (r, g + juce::Point<float> (0, 30), g, {}, true)); };
    auto dragTile = [&] {                 // a Hook Lab tile dragged out = its .mid
        ed->openHookLab (true); ed->hookLab->sync(); auto& h = *ed->hookLab; const auto c = h.tileRect (0).toFloat().getCentre();
        h.mouseDown (mev (h, c, c, left)); h.mouseDrag (mev (h, c + juce::Point<float> (30, 0), c, left, true)); h.mouseUp (mev (h, c + juce::Point<float> (30, 0), c, {}, true)); ed->openHookLab (false); };
    auto setStems = [&] (bool st) { A::stems (*ed, st); };

    std::vector<ghosty::demogate::Path> paths {
        { "arp.pill.midi", ghosty::demogate::Drag, [&] { setStems (false); dragPill (A::lanePill (*ed, LaneMelody)); dragPill (A::all (*ed)); } },
        { "arp.pill.midi-stems", ghosty::demogate::Drag, [&] { setStems (true); dragPill (A::all (*ed)); setStems (false); } },
        { "arp.pill.audio", ghosty::demogate::Drag, [&] { setStems (false); freshAudio(); dragPill (A::audio (*ed)); waitRender(); dragPill (A::audio (*ed)); } },
        { "arp.pill.audio-stems", ghosty::demogate::Drag, [&] { setStems (true); freshAudio(); dragPill (A::audio (*ed)); waitRender(); dragPill (A::audio (*ed)); setStems (false); } },
        { "arp.pill.click-midi", ghosty::demogate::File, [&] { setStems (false); clickPill (A::all (*ed)); } },
        { "arp.pill.click-audio", ghosty::demogate::File, [&] { setStems (false); freshAudio(); clickPill (A::audio (*ed)); waitRender(); } },
        { "arp.section.midi", ghosty::demogate::Drag, [&] { withSection ([&] { dragGrip (false); }); } },
        { "arp.section.audio", ghosty::demogate::Drag, [&] { withSection ([&] { freshAudio(); dragGrip (true); waitRender(); dragGrip (true); }); } },
        { "arp.section.prerender", ghosty::demogate::Auto, [&] { withSection ([&] { freshAudio(); A::prerender (*ed); waitRender(); }); } },
        { "arp.hook.midi-drag", ghosty::demogate::Drag, [&] { dragTile(); } },
        { "arp.sample.region-wav", ghosty::demogate::Drag, [&] { ed->openSample (true); ed->samplePanel->sync(); ed->samplePanel->waveC.startExport(); ed->openSample (false); } },
    };
    int fails = ghosty::demogate::run ("ArpEZator", paths, kArpInventory, box);
    ed.reset();
    fails += arpMidiDropout();
    return fails;
}
