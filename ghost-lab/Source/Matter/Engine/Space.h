// Ghost Matter - AGPL-3.0-only (see LICENSE, NOTICE.md).
//
// SPACE: Pertylizer's own Spatial Panner (binaural ITD / ILD / head shadow + image-source early reflections) and its own
// FDN Reverb, run from its Rust crate (Pertylizer.h). Our glue only: the pad and the ROOM / TAIL / WIDTH controls set
// Pertylizer's own parameters. A stereo matter is two sources: the left channel goes to one panner, the right to a second
// one beside it (WIDTH spreads them), each at half level so a centred mono sound comes out at its own level.
// No allocation after prepare().
#pragma once
#include "Pertylizer.h"
#include "HoodDefs.h"

namespace matter
{
struct SpaceParams
{
    float x = 0.5f, y = 0.5f;     // the pad: x 0 left .. 1 right, y 0 near .. 1 far
    float room = 0.3f;            // 0..1 the panner's early reflections (and the reverb's room size)
    float tail = 0.25f;           // 0..1 the reverb (0 = no reverb)
    float width = 1.0f;           // 0..1.5 (TERRAIN can push it)
};

class Space
{
public:
    Space() = default;
    ~Space();
    Space (const Space&) = delete;
    Space& operator= (const Space&) = delete;
    void prepare (double sr, int maxBlock);
    void reset();
    // pins: the Spatial Panner and Reverb controls you moved UNDER THE HOOD (exactly as set, on top of the pad and the knobs)
    void process (float* L, float* R, int n, const SpaceParams& p, const hood::LivePins* pins = nullptr) noexcept;
private:
    GmPanner* pan[2] { nullptr, nullptr };
    GmReverb* verb = nullptr;
    double sr = 48000;
    bool pannerWas[16] {}, verbWas[16] {}, anyPannerWas = false, anyVerbWas = false;
    float* tmp[4] {};             // scratch for the two panners' outputs and the halved inputs
    int maxBlock = 0;
    float* mem = nullptr;
    void freeAll();
};
}
