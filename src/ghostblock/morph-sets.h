/*
 * Poltergeist 2 (GhostBlock) - MORPH SETS: ready-made boards for the blend board (owner, 2026-11-16: "morph presets
 * that blend sounds well").
 *
 * Each set fills all four corners with factory sounds (unchanged Six Sines patches) that blend well when the puck
 * mixes them: the same kind of envelope (pads with pads, keys with keys, basses with basses), the same register
 * (a corner's OCTAVE corrects a sound that sits an octave off), and each corner's VOLUME set so the four corners
 * play at the same loudness (measured offline: one note, each patch rendered by the real engine, then the volume
 * that brings it to the set's level). Corner A becomes THIS SOUND (loaded into the main engine, so the GHOSTS
 * pages edit it); B, C and D are their own engines. The set also sets ORBIT (shape, rate, size), puts the puck in
 * the middle and turns MORPH on. Picked from the corner ledger's MORPH SETS tab.
 */
#pragma once
#include <array>
#include <vector>

namespace ghostblock::p2
{
struct MorphSetCorner
{
    const char *cat, *file; // a factory patch (folder, file)
    int octave;             // the corner's OCTAVE (-3 .. +3)
    float volDb;            // the corner's VOLUME
};
struct MorphSet
{
    const char *name, *hint;
    int orbit, rate; // Morph::Orbit, an index into Morph::rateBars
    float size;
    std::array<MorphSetCorner, 4> corners; // A top-left, B top-right, C bottom-left, D bottom-right
};
inline const std::vector<MorphSet> &morphSets()
{
    static const std::vector<MorphSet> v = {
    {"GLASS CATHEDRAL", "4 slow glass pads", 5, 5, 0.60f,
      {{{"Strings", "Bowing 1.sxsnp", 0, 1.9f},
       {"Strings", "Bowed Glass.sxsnp", 0, 1.1f},
       {"Harmony", "Subtle Spaces 2.sxsnp", 0, -3.7f},
       {"Strings", "Slow Primes.sxsnp", 0, 6.0f}}}},
    {"NEBULA DRIFT", "dark pads, circling", 1, 4, 0.70f,
      {{{"Pads", "OST Pad.sxsnp", 0, 2.9f},
       {"Pads", "Holding Pattern.sxsnp", 0, 6.0f},
       {"Harmony", "Harmony 3.sxsnp", 0, 0.1f},
       {"Harmony", "Harmony 6.sxsnp", 0, 4.6f}}}},
    {"HAUNTED KEYS", "pianos + EPs", 0, 3, 0.60f,
      {{{"Keys", "EP 3.sxsnp", 0, 3.1f},
       {"Keys", "EP 4.sxsnp", 0, 5.5f},
       {"Keys", "Pianobells.sxsnp", 0, -10.4f},
       {"Keys", "Surge DX Piano Port.sxsnp", 0, 6.0f}}}},
    {"BELL TOWER", "bells, one by one", 3, 3, 0.90f,
      {{{"Bells", "ED2-11 Iso-2 Bells.sxsnp", 0, 2.5f},
       {"Bells", "Five Primes Bell.sxsnp", 0, 5.5f},
       {"Bells", "Iso-Bells 6.sxsnp", 0, 6.0f},
       {"Bells", "Detuned Metal.sxsnp", 0, 1.4f}}}},
    {"MALLET GARDEN", "marimbas + bells", 0, 3, 0.60f,
      {{{"Mallets", "Bubblegum Marimba.sxsnp", 0, 1.2f},
       {"Keys", "Nostalgic Bell Synth.sxsnp", 1, 6.0f},
       {"Mallets", "CPS Bells.sxsnp", 1, -0.1f},
       {"Bells", "Orin.sxsnp", 1, -5.4f}}}},
    {"SUB STACK", "4 deep basses", 0, 3, 0.60f,
      {{{"Bass", "Bass 1.sxsnp", 0, -4.1f},
       {"Bass", "Bass 2.sxsnp", 0, -7.3f},
       {"Bass", "Neon New York.sxsnp", 0, -5.6f},
       {"Bass", "Butterbean.sxsnp", 0, -5.2f}}}},
    {"GROWL MACHINE", "talking growl bass", 2, 2, 0.80f,
      {{{"Bass", "Growler.sxsnp", 0, -0.9f},
       {"Bass", "Scream Queen.sxsnp", 0, -1.9f},
       {"Bass", "Polite Discourse.sxsnp", 0, -0.4f},
       {"Bass", "I Am Sweepy.sxsnp", 0, -0.6f}}}},
    {"FM BASS LAB", "tight FM basses", 4, 1, 0.70f,
      {{{"Bass", "Classic 4op Bass.sxsnp", 0, -0.7f},
       {"Bass", "Reso FM Bass.sxsnp", 0, 2.9f},
       {"Bass", "Dirty Bass Sync.sxsnp", 0, -5.7f},
       {"Bass", "Sub Bass.sxsnp", 0, -2.1f}}}},
    {"SYNC LEADS", "sync + PWM leads", 1, 3, 0.60f,
      {{{"Leads", "Rounded PWM.sxsnp", 0, -2.3f},
       {"Leads", "Rounded Sync.sxsnp", 0, 1.1f},
       {"Leads", "Rounded Sync 2.sxsnp", -1, -1.9f},
       {"Leads", "Cars Sync.sxsnp", 0, -4.9f}}}},
    {"CZ PHASE PLAY", "CZ keys, each bar", 1, 2, 0.50f,
      {{{"Keys", "Phase Map CZ Double - Tukey.sxsnp", 0, -1.1f},
       {"Keys", "Phase Map CZ Saw - Sin x cubed.sxsnp", 0, 2.0f},
       {"Keys", "Phase Map CZ Sine to Saw - TX 7.sxsnp", 0, 0.4f},
       {"Keys", "Phase Map CZ Sine to Square - Spiky TX 4.sxsnp", 0, -4.5f}}}},
    {"RESONANT SWEEPS", "drifting resonance", 5, 4, 0.70f,
      {{{"Keys", "Resonant Sweep - Saw - TX 3.sxsnp", 0, -0.2f},
       {"Keys", "Resonant Sweep - Half Trap - Spiky TX 2.sxsnp", 0, -2.8f},
       {"Keys", "Resonant Sweep - Tri - Blackman Harris.sxsnp", 0, -4.7f},
       {"Keys", "Resonant Sweep - Full Trap - Hann.sxsnp", 0, -9.5f}}}},
    {"ORGAN LOFT", "organs, swinging", 4, 4, 0.80f,
      {{{"Keys", "Organ.sxsnp", 0, 1.9f},
       {"Keys", "EP to Organ.sxsnp", 0, -3.2f},
       {"Keys", "Moving Glass Organ.sxsnp", 0, 5.4f},
       {"Harmony", "Harmony 5.sxsnp", 0, 2.0f}}}}
    };
    return v;
}
} // namespace ghostblock::p2
