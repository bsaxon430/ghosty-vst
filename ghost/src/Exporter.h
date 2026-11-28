#pragma once
// GHOST - SEND TO FL: the song as files FL takes in.
//
//   MIDI/GHOST song.mid        every part as its own track (CHORDS, ARP, MELODY, BASS, one per drum): dropped on FL's
//                              Channel Rack, FL makes a channel per part with its notes in a pattern
//   MIDI/<part>.mid            each part alone
//   Stems/<part>.wav           each part exactly as heard (its sound, TONE, FX, level), plus the whole mix
//   Sounds/<lane>.vital|.sxsnp|.atlas   each lane's sound as its synth's own preset (your edits included)
//   Sounds/<lane> FX.ghostfx   the lane's FX, for the GHOST FX plug-in
//   Sounds/<drum>.wav          each drum's sound, one hit (drop it on that drum's channel)
//
// GHOST plays the song through its own parts again, offline (the live sound pauses for those seconds), a little at a time
// on the message thread so the window keeps answering. ARPEZ: ArpEZator's notes are recorded from bar 1; Ghosty Beats is
// played one track at a time (its SOLO) and the hits are found in the sound. GRAPH: the notes come straight from the song.
#include "GhostProcessor.h"

namespace ghost
{
class Exporter : private juce::Timer
{
public:
    explicit Exporter (GhostProcessor& p);
    ~Exporter() override;

    bool start();                       // false when it can't (already running, nothing open)
    bool running() const { return isRunning; }
    float progress() const { return total > 0 ? (float) doneJobs / (float) total : 0.0f; }
    juce::String status;                // what it's doing / what went wrong
    juce::File folder;                  // the last export

    struct Part { juce::String name; juce::File midi, stem, sound; };
    std::vector<Part> parts;            // the result, in order
    juce::File songMidi, mixStem;
    std::function<void()> onChange;     // (the window)

    // checks: run the whole thing at once
    bool runBlocking();

private:
    void timerCallback() override;
    bool step();                        // a bounded piece of work; false when everything is done
    void finish (bool ok);

    struct NoteEv { double beat; int note, vel; bool on; };
    struct Hit { double beat; int vel; int semis; };
    struct Render;
    GhostProcessor& proc;
    bool isRunning = false;
    int total = 0, doneJobs = 0;
    std::vector<std::function<bool()>> jobs;   // each returns true when finished (called again until then)
    std::unique_ptr<Render> current;
    double bpm = 120.0, beats = 16.0, rate = 48000.0;
    int block = 512;
    int src = 0;
    std::array<std::vector<NoteEv>, kLanes> laneNotes;
    struct DrumTrack { int track; juce::String sound; std::vector<Hit> hits; juce::AudioBuffer<float> stem, shot; bool used = false; };
    std::vector<DrumTrack> drums;
    std::array<juce::AudioBuffer<float>, kLanes> laneStems;
    std::array<bool, kLanes> laneHasStem {};
    juce::MemoryBlock beatsOriginal;           // Ghosty Beats' state before its tracks were played one by one
    bool beatsTouched = false;
    size_t drumAt = 0, shotAt = 0;
    juce::uint32 startedAt = 0;
};
} // namespace ghost
