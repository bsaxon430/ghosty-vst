#pragma once
// GHOST - GRAPH: Ghost Graph's song as notes, played through GHOST's lanes and Ghosty Beats' drum sounds.
//
// Ghost Graph runs inside GHOST (its own window on the GRAPH page). GHOST reads its song from its saved state and splits it
// into parts exactly as Ghost Graph's own DRAG does (graph/GraphStems.h, Ghost Graph's code, unchanged):
//   Bass -> BASS, Pad / Chords -> CHORDS, Arp -> ARP, Lead -> MELODY (a second lead -> ARP when ARP has nothing)
//   Kick / Snare / Clap / Rim / Hi-hat / Open hat / Tom / Shaker -> the Ghosty Beats track holding that kind of drum
// Ghost Graph's own sound is not heard in GHOST: its notes play your sounds.
#include <juce_core/juce_core.h>
#include <array>
#include <memory>
#include <vector>

namespace ghost
{
struct GraphEvent
{
    double beat = 0;     // in the song (0 .. beats)
    int target = 0;      // 0..3 a lane (CHORDS, ARP, MELODY, BASS), 4 the drums
    int note = 60, velocity = 100;
    bool on = true;
};

struct GraphSong
{
    double beats = 0;                          // the length (it loops)
    std::vector<GraphEvent> events;            // sorted by beat
    std::array<juce::String, 4> laneParts;     // what each lane plays ("Lead · riff, Lead")
    juce::String drums;                        // the drum parts it found
    juce::String error;
};

// the drum kinds GHOST maps (Ghost Graph's roles)
enum DrumKind { DKick = 0, DSnare, DClap, DRim, DHat, DOpenHat, DTom, DShaker, kDrumKinds };
using DrumMap = std::array<int, kDrumKinds>;   // the MIDI note that plays each kind in Ghosty Beats (36 + its track), -1 = none

// Ghosty Beats' kit (from its saved state): which track holds which kind of drum
DrumMap drumMapFromBeatsState (const juce::MemoryBlock& beatsHostState, juce::String* kitText = nullptr);
// Ghost Graph's song (from its saved state, as JUCE's VST3 host saves it) as GHOST's events
std::unique_ptr<GraphSong> graphSongFromState (const juce::MemoryBlock& graphHostState, const DrumMap& drums);
// a hosted plug-in's own state out of JUCE's VST3 host wrapper
juce::MemoryBlock componentStateOf (const juce::MemoryBlock& hostState);
} // namespace ghost
