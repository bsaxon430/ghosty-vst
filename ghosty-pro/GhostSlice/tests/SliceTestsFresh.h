// GHOST SLICE (2026-11-07): the size the window opens at, and a fresh start every time. Included from SliceTests.cpp.
//   size        the editor opens at about 1200 x 760 (never wider than 1240, never taller than 780), keeps resizing, and at every size the
//               whole picture sits inside the window (one scaled canvas, so nothing can overlap or fall off the edge)
//   freshstate  two instances in one process: changing the first leaves the second at factory defaults; destroy + recreate = defaults
#pragma once

namespace freshstart
{
static juce::String stateOf (juce::AudioProcessor& p) { juce::MemoryBlock mb; p.getStateInformation (mb); return juce::String::fromUTF8 ((const char*) mb.getData(), (int) mb.getSize()); }

static void sizeTest (const juce::File& dir)
{
    section ("size: opens at the fitting size (about 1200 x 760, at most 1240 x 780), resizable, nothing off the edge");
    ghosty::License::testForce().store (1);
    SliceProcessor p; p.prepareToPlay (48000, 512);
    std::unique_ptr<SliceEditor> ed (static_cast<SliceEditor*> (p.createEditor())); ed->setVisible (true); pump (40);
    const int w = ed->getWidth(), h = ed->getHeight();
    check (w <= 1240 && h <= 780 && w >= 1000 && h >= 720, "opens at " + juce::String (w) + " x " + juce::String (h) + " (fits FL's window on a 1440 x 900 MacBook Pro)");
    check (ed->isResizable(), "the window can be resized");
    savePng (shot (*ed), dir.getChildFile ("ui-open-size.png"));
    auto inside = [&] (int W, int H)
    {
        ed->setSize (W, H); pump (10);
        const auto r = ed->canvas.getBoundsInParent().toFloat();   // (the canvas after its scale transform)
        const auto win = ed->getLocalBounds().toFloat().expanded (0.5f);
        return win.contains (r) && r.getWidth() > 0 && r.getHeight() > 0;
    };
    bool all = true; juce::String bad;
    for (auto s : { juce::Point<int> (1140, 760), juce::Point<int> (1200, 760), juce::Point<int> (1240, 780), juce::Point<int> (840, 560), juce::Point<int> (1536, 1024), juce::Point<int> (2304, 1536) })
        if (! inside (s.x, s.y)) { all = false; bad << s.x << "x" << s.y << " "; }
    check (all, "at every size the whole picture is inside the window " + bad);
    // the controls on the canvas do not overlap each other (the main ones, at the design's own size; the scale is uniform)
    ed->setSize (SliceEditor::kDefaultW, SliceEditor::kDefaultH); pump (10);
    juce::Array<juce::Component*> main { &ed->generateBtn, &ed->varyBtn, &ed->playBtn, &ed->dragWav, &ed->dragMidi, &ed->dragChords, &ed->saveBtn, &ed->keys,
                                         &ed->stage, &ed->typeTiles, &ed->charTiles, &ed->styleBtn, &ed->lengthBtn, &ed->strip, &ed->prevBtn, &ed->nextBtn, &ed->keyBtn, &ed->tempoBtn, &ed->tempoSw,
                                         &ed->gearBtn, &ed->musicalSl, &ed->toneKnob, &ed->typeKnob, &ed->spaceKnob };
    int clashes = 0; juce::String which;
    for (int i = 0; i < main.size(); ++i)
        for (int j = i + 1; j < main.size(); ++j)
            if (main[i]->isVisible() && main[j]->isVisible() && main[i]->getBounds().intersects (main[j]->getBounds())) { ++clashes; which << main[i]->getName() << "/" << main[j]->getName() << " "; }
    check (clashes == 0, "the main controls do not overlap (" + juce::String (clashes) + ") " + which);
    // a second editor (a new project) opens at the fitting size again, whatever the first was set to
    ed->setSize (1536, 1024); pump (10); ed.reset(); pump (10);
    std::unique_ptr<SliceEditor> e2 (static_cast<SliceEditor*> (p.createEditor())); e2->setVisible (true); pump (20);
    check (e2->getWidth() == SliceEditor::kDefaultW && e2->getHeight() == SliceEditor::kDefaultH, "reopened: the fitting size again (not the last size)");
    e2.reset(); pump (10);
    ghosty::License::testForce().store (-1);
}

static void freshTest()
{
    section ("fresh start: a new Ghost Slice is always brand new (nothing carries over from another instance or project)");
    ghosty::License::testForce().store (1);
    juce::String factory;
    { SliceProcessor f; f.prepareToPlay (48000, 512); factory = stateOf (f); }
    {
        SliceProcessor a, b; a.prepareToPlay (48000, 512); b.prepareToPlay (48000, 512);
        const auto bStart = stateOf (b);
        auto file = wavOf (sung(), "sung-fresh.wav");
        juce::String err; check (a.loadAudio (file, err), "load a sound into the first");
        a.type = Type::Chops; a.generate(); a.waitIdle (60000);
        a.setTone (-0.5f); a.setSpace (0.9f); a.keyLock = true; a.uiTheme.store (3); a.clear = ! a.clear;
        check (stateOf (a) != factory && a.takes().size() >= 1, "the first has a sound, a take and new settings");
        check (stateOf (b) == bStart && stateOf (b) == factory && b.takes().empty() && b.getPrepared() == nullptr, "the second, open at the same time, is still at factory defaults");
        SliceProcessor c; c.prepareToPlay (48000, 512);
        check (stateOf (c) == factory && c.takes().empty(), "a third made after the change starts at factory defaults (no sound, no takes)");
    }
    {   // destroy and recreate
        SliceProcessor d; d.prepareToPlay (44100, 256);
        check (stateOf (d) == factory && d.takes().empty() && d.getPrepared() == nullptr && ! d.keyLock, "destroyed and made again: factory defaults");
        std::unique_ptr<SliceEditor> ed (static_cast<SliceEditor*> (d.createEditor())); ed->setVisible (true); pump (20);
        check (ed->getWidth() == SliceEditor::kDefaultW && ed->typeTiles.sel == 0 && ed->headline() == "DROP A VOCAL OR ANY SOUND", "its window opens fresh: AUTO, no sound, the fitting size");
        ed.reset(); pump (10);
    }
    ghosty::License::testForce().store (-1);
}

// 2026-11-07: TEMPO ½x | 1x | 2x
static void tempoTest (const juce::File& dir)
{
    section ("TEMPO 1/2x | 1x | 2x: takes at the multiple, DRAG MIDI lands as heard at the session's tempo, DRAG WAV as heard, saved with the session");
    ghosty::License::testForce().store (1);
    auto notesOf = [] (const juce::MidiFile& mf, double& tempo) { std::vector<double> t; tempo = 0; const double tpq = mf.getTimeFormat() > 0 ? mf.getTimeFormat() : 960;
        for (int k = 0; k < mf.getNumTracks(); ++k) for (auto* e : *mf.getTrack (k)) { if (e->message.isNoteOn()) t.push_back (e->message.getTimeStamp() / tpq); if (e->message.isTempoMetaEvent()) tempo = 60.0 / e->message.getTempoSecondsPerQuarterNote(); }
        std::sort (t.begin(), t.end()); return t; };
    SliceProcessor p; p.prepareToPlay (48000, 512);
    auto file = wavOf (sung(), "sung-tempo.wav"); juce::String err; check (p.loadAudio (file, err), "load a sound");
    p.type = Type::Instrument;
    for (int code : { 2, 0 })
    {
        const double f = ghosty::tempomul::factor (code);
        p.setTempoMul (code); p.generate(); p.waitIdle (60000);
        auto t = p.currentTake(); if (t == nullptr) { check (false, "a take"); continue; }
        check (std::abs (t->bpm - p.tempo() * f) < 1e-6, "TEMPO " + ghosty::tempomul::multName (code) + ": the take is made at " + f2 (t->bpm, 1) + " BPM (the session at " + f2 (p.tempo(), 1) + ")");
        const auto mf = [&] { juce::String e; auto fm = p.writeTakeMidi (tmpDir(), e); juce::MidiFile m; juce::FileInputStream in (fm); m.readFrom (in); return std::make_pair (m, fm); }();
        double tTake = 0, tFile = 0; const auto nTake = notesOf (t->midi, tTake), nFile = notesOf (mf.first, tFile);
        double err2 = nTake.size() == nFile.size() && ! nTake.empty() ? 0.0 : 9.0; for (size_t i = 0; err2 < 9 && i < nTake.size(); ++i) err2 = std::max (err2, std::abs (nFile[i] - nTake[i] / f));
        std::printf ("    %s: %zu notes; the take's tempo %.1f, the file's %.1f; timing error %.4f beats  (%s)\n", ghosty::tempomul::multName (code).toRawUTF8(), nTake.size(), tTake, tFile, err2, mf.second.getFileName().toRawUTF8());
        check (err2 < 0.004 && std::abs (tFile - p.tempo()) < 0.01, "DRAG MIDI at the session's " + f2 (p.tempo(), 0) + " BPM, every note where it is heard");
        check (mf.second.getFileName().contains (ghosty::tempomul::bpmText (t->bpm) + "bpm"), "the file name says the BPM heard");
        juce::String e2; const auto w = p.writeTakeWav (tmpDir(), e2); juce::WavAudioFormat wf; std::unique_ptr<juce::AudioFormatReader> r (wf.createReaderFor (new juce::FileInputStream (w), true));
        const double wavSec = r != nullptr ? (double) r->lengthInSamples / r->sampleRate : 0.0, musicSec = t->recipe.bars * 4.0 * 60.0 / t->bpm;
        std::printf ("    %s: DRAG WAV %.2f s, %d bars at %.0f BPM = %.2f s\n", ghosty::tempomul::multName (code).toRawUTF8(), wavSec, t->recipe.bars, t->bpm, musicSec);
        check (r != nullptr && wavSec >= musicSec - 0.05 && wavSec - musicSec < 4.0 && w.getFileName().contains ("bpm"), "DRAG WAV is the take as heard: its bars at " + f2 (t->bpm, 0) + " BPM (" + w.getFileName() + ")");
    }
    {   // the take on screen re-renders at the new tempo
        p.setTempoMul (1); p.waitIdle (60000); auto t = p.currentTake();
        check (t != nullptr && std::abs (t->bpm - p.tempo()) < 1e-6, "switching back to 1x re-renders the take on screen at " + f2 (p.tempo(), 0) + " BPM");
    }
    {   // saved with the session; a new one starts at 1x
        p.setTempoMul (2); juce::MemoryBlock mb; p.getStateInformation (mb);
        SliceProcessor q; check (q.tempoMul == 1, "a new Ghost Slice starts at 1x"); q.setStateInformation (mb.getData(), (int) mb.getSize()); check (q.tempoMul == 2, "the session reopens at 2x");
    }
    {   // the switch: in the header next to TEMPO, nothing under it
        std::unique_ptr<SliceEditor> ed (static_cast<SliceEditor*> (p.createEditor())); ed->setVisible (true); pump (30);
        const auto b = ed->tempoSw.getBounds(); int clash = 0;
        for (auto* c : ed->canvas.getChildren()) if (c != &ed->tempoSw && c->isVisible() && c->getBounds().intersects (b) && c->getWidth() < 900 && c != &ed->toastView) ++clash;
        check (ed->tempoSw.isVisible() && clash == 0 && ! b.intersects (ed->tempoBtn.getBounds()) && ed->tempoSw.sel == p.tempoMul, "the TEMPO switch is in the header with nothing under it, showing the session's");
        savePng (shot (*ed), dir.getChildFile ("ui-tempo-2x.png"));
        ed.reset(); pump (10);
    }
    ghosty::License::testForce().store (-1);
}
} // namespace freshstart
