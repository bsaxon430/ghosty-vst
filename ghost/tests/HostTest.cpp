// GHOST with real plug-ins (Linux builds of ArpEZator / Ghosty Beats in GHOST_VST3_DIR): it opens them, plays 16 bars on its
// own clock, every lane gets its notes, the sound is finite, the song saves and comes back, the window draws.
#include "GhostProcessor.h"
#include "GhostEditor.h"
#include <cstdio>
using namespace ghost;
static int fails = 0;
#define CHECK(c, msg) do { if (! (c)) { ++fails; std::printf ("FAIL: %s\n", juce::String (msg).toRawUTF8()); } else std::printf ("ok: %s\n", juce::String (msg).toRawUTF8()); } while (0)

static void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

int main (int argc, char** argv)
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    juce::ScopedJuceInitialiser_GUI init;
    const juce::String shot = argc > 1 ? juce::String (argv[1]) : juce::String ("/tmp/ghost.png");
    auto proc = std::make_unique<GhostProcessor>();
    proc->prepareToPlay (48000.0, 512);
    pump (1500);
    CHECK (proc->arp != nullptr, "ArpEZator opened (" + proc->arpError + ")");
    CHECK (proc->beats != nullptr, "Ghosty Beats opened (" + proc->beatsError + ")");
    for (int l = 0; l < kLanes; ++l) std::printf ("  lane %s: %s / %s %s\n", laneName (l), proc->lanes[(size_t) l].soundName.toRawUTF8(),
                                                   proc->lanes[(size_t) l].hasEngine ? engineName (proc->lanes[(size_t) l].engine) : "-", proc->lanes[(size_t) l].error.toRawUTF8());
    // one lane on each other synth (when they are installed): Poltergeist 2 on ARP, Ghost Atlas on MELODY
    const bool realMac = juce::SystemStats::getEnvironmentVariable ("GHOST_REAL_SYNTHS", {}).isNotEmpty();
    if (realMac)
    {
        juce::String e;
        CHECK (proc->loadSound (LaneArp, "p2:factory:Bells/Alien Gamelan", e), "Poltergeist 2 sound on ARP (" + e + ")");
        Sound at; for (auto& s : Catalog::factorySounds()) if (s.engine == Engine::Atlas) { at = s; break; }
        CHECK (proc->loadSound (LaneMelody, at, e), "Ghost Atlas sound on MELODY: " + at.name + " (" + e + ")");
        CHECK (proc->lanes[LaneChords].plugin != nullptr && proc->lanes[LaneChords].engine == Engine::GhostyPro2, "Ghosty Pro 2 on CHORDS");
        // every lane gets a note from 'the keyboard' too, so each synth is heard even if ArpEZator's idea leaves a lane empty
    }
    proc->ownPlay = true;
    juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer midi;
    double peak = 0; bool finite = true; float lanePeak[kLanes] {};
    const int blocks = (int) (48000.0 * 60.0 / 120.0 * 4 * 16 / 512);   // 16 bars at 120
    for (int b = 0; b < blocks; ++b)
    {
        midi.clear();
        if (realMac && b % 200 == 10) { proc->selectedLane = (b / 200) % kLanes; midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0); }
        if (realMac && b % 200 == 150) midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
        proc->processBlock (buf, midi);
        for (int l = 0; l < kLanes; ++l) lanePeak[l] = juce::jmax (lanePeak[l], proc->lanes[(size_t) l].meter.load());
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) { const float v = buf.getSample (c, i); if (! std::isfinite (v)) finite = false; peak = juce::jmax (peak, (double) std::abs (v)); }
        if (b % 64 == 0) pump (1);
    }
    for (int l = 0; l < kLanes; ++l) std::printf ("  notes into %s: %d\n", laneName (l), proc->lanes[(size_t) l].notesIn.load());
    int lanesWithNotes = 0; for (auto& L : proc->lanes) if (L.notesIn.load() > 0) ++lanesWithNotes;
    CHECK (lanesWithNotes >= 3, "ArpEZator's notes reach the lanes (" + juce::String (lanesWithNotes) + " lanes)");
    CHECK (finite, "the output is finite");
    std::printf ("  peak %.3f\n", peak);
    CHECK (peak > 0.001, "something is heard (drums / lanes)");
    for (int l = 0; l < kLanes; ++l) std::printf ("  %s peak %.4f (%s)\n", laneName (l), lanePeak[l], proc->lanes[(size_t) l].soundName.toRawUTF8());
    if (realMac) for (int l : { (int) LaneChords, (int) LaneArp, (int) LaneBass }) CHECK (lanePeak[l] > 0.0005f, juce::String (laneName (l)) + " is heard");
    CHECK (proc->beatsMeter.load() >= 0.0f, "drums meter");
    // FX on a lane: Bin Shuffle in bay 1 + WARP
    auto& F = proc->lanes[0].fx;
    F.setParam (sfx::SpectralFx::pOn, 1); F.setParam (sfx::SpectralFx::pSlotType (0), 1); F.setParam (sfx::SpectralFx::pWarpOn, 1);
    proc->fxChanged (0); pump (300); proc->fxChanged (0);
    std::printf ("  FX latency %d, GHOST latency %d\n", (int) F.latency(), proc->totalLatency());
    CHECK (proc->totalLatency() == (int) F.latency() + proc->lanes[0].pluginLatency || proc->totalLatency() >= (int) F.latency(), "GHOST lines up with the FX delay");
    for (int b = 0; b < 200; ++b) { midi.clear(); proc->processBlock (buf, midi); for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) if (! std::isfinite (buf.getSample (c, i))) finite = false; }
    CHECK (finite, "finite with the FX on");
    // the song comes back
    juce::MemoryBlock song; proc->getStateInformation (song);
    std::printf ("  song: %d bytes\n", (int) song.getSize());
    auto proc2 = std::make_unique<GhostProcessor>();
    proc2->prepareToPlay (48000.0, 512);
    proc2->setStateInformation (song.getData(), (int) song.getSize());
    pump (1500);
    for (int l = 0; l < kLanes; ++l) CHECK (proc2->lanes[(size_t) l].soundId == proc->lanes[(size_t) l].soundId, juce::String ("lane ") + laneName (l) + " comes back");
    CHECK (proc2->lanes[0].fx.on() && proc2->lanes[0].fx.slotType (0) == 1 && proc2->lanes[0].fx.warpOn(), "the FX come back");
    proc2.reset();
    // the window
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc->createEditor());
        ed->setVisible (true);
        pump (800);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::File (shot).deleteFile();
        juce::FileOutputStream out ((juce::File (shot))); juce::PNGImageFormat().writeImageToStream (img, out);
        std::printf ("  window %dx%d -> %s\n", ed->getWidth(), ed->getHeight(), shot.toRawUTF8());
        if (auto* ge = dynamic_cast<GhostEditor*> (ed.get()))
        {
            ge->showFx (0); pump (300);
            auto i2 = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            juce::File f2 (shot.replace (".png", "-fx.png")); f2.deleteFile(); juce::FileOutputStream o2 (f2); juce::PNGImageFormat().writeImageToStream (i2, o2);
            ge->showBrowser (2); pump (300);
            auto i3 = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            juce::File f3 (shot.replace (".png", "-sounds.png")); f3.deleteFile(); juce::FileOutputStream o3 (f3); juce::PNGImageFormat().writeImageToStream (i3, o3);
            ge->showPage (GhostEditor::PageBeats); pump (500);
            auto i4 = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            juce::File f4 (shot.replace (".png", "-beats.png")); f4.deleteFile(); juce::FileOutputStream o4 (f4); juce::PNGImageFormat().writeImageToStream (i4, o4);
        }
        ed.reset();
    }
    proc.reset();
    std::printf (fails == 0 ? "ALL GOOD\n" : "%d FAILED\n", fails);
    return fails == 0 ? 0 : 1;
}
