#pragma once
// PLAY / REPEAT / HOLD model types (value types, saved in project.json). Model.h includes this; the JSON lives in PlayJson.cpp.
//   KeySlot / PlayMap   PLAY: slices on MIDI keys (GHOST-SLICE-STUDY.md 7.4)
//   RepeatOp            GHOST REPEAT: a retrigger span on the output timeline (7.3), expanded by the plan compiler
//   HoldOp              GHOST HOLD: a held warp segment of a region, rendered by a HOLD mode instead of the stretcher (4.x)
#include "SliceCore.h"
#include <array>

namespace gslice
{
// ---------------------------------------------------------------- PLAY
// GHOST CHOP slice modes (GHOST-SLICE-STUDY.md 7.1): the arrangement's regions, N equal parts, hand-placed cuts, or cuts from the
// analysis - TRANSIENT (onsets), SYLLABLE (vocal syllable onsets), NOTE (note boundaries), BEAT (the tempo grid)
enum class SliceMode { Regions = 0, Equal = 1, Manual = 2, Transient = 3, Syllable = 4, Note = 5, Beat = 6 };
constexpr int kSliceModes = 7;

struct KeySlot
{
    int note = 36;                  // MIDI note
    int slice = 0;                  // index into the current slice list
    double pitchSemis = 0;          // -24 .. 24
    bool reverse = false;
    double startMs = 0;             // start offset into the slice
    double attackMs = 0, decayMs = 0, sustain = 1.0, releaseMs = 30;
    double gainDb = 0, pan = 0;     // pan -1 .. 1
    int choke = 0;                  // 0 = none, 1..8
    bool oneShot = true;            // true: plays to the end; false (GATE): note-off releases
    int repeat = 0;                 // retrigger pattern slot: 0 = off, 1 = 1/8, 2 = 1/16, 3 = 1/32, 4 = 1/16T, 5 = 1/64
    bool vowelHold = false;         // phase 16 HOLD VOWEL (GATE keys): holding the key sustains the slice's vowel (SUSTAIN grains)
    bool operator== (const KeySlot& o) const = default;
};

struct PlayMap
{
    SliceMode mode = SliceMode::Regions;
    int equalN = 16;                // EQUAL 2..64 parts of the arrangement
    std::vector<double> manual;     // MANUAL cut points (PF on the output timeline, sorted)
    double sensitivity = 0.5;       // TRANSIENT / SYLLABLE / NOTE: 0 = only the clearest cuts .. 1 = every candidate
    int beatDiv = 8;                // BEAT: cuts every 1/N note (4 / 8 / 16 / 32) from the project tempo and grid phase
    int polyphony = 16;             // 1 .. 32
    int bendRange = 2;              // +-2 .. 24 semitones
    double velToGain = 1.0;         // 0 .. 1
    double velToFilter = 0.0;       // 0 .. 1 (a 2-pole low-pass that opens with velocity)
    std::vector<KeySlot> keys;      // sorted by note, one per note
    bool operator== (const PlayMap& o) const = default;
    const KeySlot* key (int note) const { for (auto& k : keys) if (k.note == note) return &k; return nullptr; }
    KeySlot* key (int note) { for (auto& k : keys) if (k.note == note) return &k; return nullptr; }
};

// ---------------------------------------------------------------- REPEAT
struct RepeatOp
{
    int64 id = 0;
    double start = 0, length = 0;   // PF on the output timeline: the repeat span [start, start + length) (length = the handle)
    int rateDiv = 16;               // 1/4 .. 1/128
    int rateMod = 0;                // 0 straight, 1 triplet, 2 dotted
    bool retrigger = true;          // on: every repeat restarts at `start`; off: the audio runs on, the triggers gate it
    double pitchSemis = 0;          // per repetition
    bool pitchRamp = false;         // false: STEP (n * pitch); true: RAMP (0 .. pitch over the span)
    int reverse = 0;                // 0 none, 1 every 2nd repetition, 2 all
    double fadeInMs = 1, fadeOutMs = 4;
    double swing = 0.5;             // 0.5 .. 0.75
    double ramp = 1.0;              // end-rate multiplier 0.25 .. 4 (1 = constant rate)
    bool ratchetOn = false;
    std::array<float, 5> ratchet { 0, 0, 0, 0, 0 };   // GHOST RATCHET: log2 rate multipliers (-2 .. 2) at 0, 1/4, 1/2, 3/4, 1 of the span
    double decay = 1.0;             // gain multiplier per repetition (0 .. 1)
    double gate = 1.0;              // repetition length as a share of its period (0.1 .. 1)
    bool operator== (const RepeatOp& o) const = default;
    double end() const { return start + length; }
};

// ---------------------------------------------------------------- HOLD
enum class HoldMode { Elastic = 0, Sustain = 1, Texture = 2, Freeze = 3 };

struct HoldOp
{
    int64 id = 0;
    int64 markerA = 0, markerB = 0; // the warp markers around the held (stable) segment: [A, B] is held, the rest of the region plays at 1x
    int64 selA = 0, selB = 0;       // SF: the selection it was made on (head = selA .. A, tail = B .. selB at 1x; drawn as a bracket)
    HoldMode mode = HoldMode::Sustain;
    bool autoStable = true;         // the stable region was auto-picked (false = moved by hand)
    double driftCents = 5;          // SUSTAIN: slow random pitch drift (+-)
    double variation = 1.0;         // SUSTAIN: grain walk width (0.25 .. 2)
    double smear = 0.6;             // TEXTURE: phase randomisation (0.05 .. 1)
    uint32_t seed = 1;
    bool operator== (const HoldOp& o) const = default;
};
} // namespace gslice
