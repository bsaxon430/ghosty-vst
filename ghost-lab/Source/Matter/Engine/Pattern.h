// Ghost Matter - AGPL-3.0-only (see LICENSE, NOTICE.md).
//
// PATTERN: the held keys played as a rhythm by Pertylizer's own Euclidean module and its own Turing Machine (shift-register
// melody, its own pentatonic scale), run from its Rust crate (Pertylizer.h). Our glue: we clock both modules once per
// step from the host's beat (a gate pulse on their clock inputs) and read back the Euclidean gate (a hit or not) and the
// Turing pitch (octaves, which we turn into semitones). No allocation.
#pragma once
#include "Pertylizer.h"
#include "HoodDefs.h"

namespace matter
{
class Pattern
{
public:
    Pattern();
    ~Pattern();
    Pattern (const Pattern&) = delete;
    Pattern& operator= (const Pattern&) = delete;
    void prepare (double sr);
    void set (int steps, int hits, int rotate) noexcept;   // Pertylizer's steps / pulses / rotation
    void setTuring (float mutate, float range) noexcept;   // Pertylizer's mutation / range (0..1)
    // UNDER THE HOOD: the Euclidean swing and the Turing Machine scale and length, when you set them (and back to Pertylizer's own defaults when you let go)
    void applyPins (const hood::LivePins& pins) noexcept;
    // one step of the clock: true on a hit; `semis` is the Turing machine's note above the key
    bool tick (int& semis) noexcept;
    int steps() const noexcept { return n; }
    int step() const noexcept { return pos; }
private:
    GmEuclid* euclid = nullptr;
    GmTuring* turing = nullptr;
    int n = 16, k = -1, r = -1, pos = -1;
    float lastMut = -1, lastRange = -1;
    float lastSwing = 0.0f, lastScale = 3.0f, lastLength = 16.0f;
};
}
