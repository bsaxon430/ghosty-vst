// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX SPATIAL: the thin glue to Steam Audio (Apache-2.0, vendored unchanged in third_party/steamaudio). Steam Audio's own
// classes do all the sound: its HRTF database (the built-in CIPIC set), BinauralEffect (HRTF convolution, bilinear interpolation,
// per-frame crossfade when the direction moves), PanningEffect (stereo speakers), and the DirectEffect's distance model
// (DistanceAttenuationModel + AirAbsorptionModel through its EQEffect and GainEffect, wired as DirectEffect::apply wires them).
// This header has no Steam Audio includes, so the rest of the plug-in never sees its names.
#pragma once
#include <memory>

namespace phantom::spatial
{
class Renderer
{
public:
    Renderer();
    ~Renderer();
    // message thread: builds the HRTF database and every voice's effects (allocates); frameSize is fixed from here on.
    // Steam Audio's built-in HRTF covers 44.1 and 48 kHz; at other rates ready() stays false and SPATIAL is not available.
    void prepare (int sampleRate, int frameSize, int numVoices);
    bool ready() const noexcept;
    void resetVoice (int v) noexcept;
    // audio thread, one frame of voice v. in = the voice's stereo wet (frameSize samples each). (x, y, z) = unit direction in the
    // listener's frame (x right, y up, -z ahead); distM in metres. The left and right of the wet are two sources either side of the
    // direction. The result is ADDED to outL / outR. speakers = stereo-speaker mode (the binaural render's mid, panned for speakers).
    void render (int v, const float* inL, const float* inR, float x, float y, float z, float distM, bool speakers, float* outL, float* outR) noexcept;
    static constexpr float kSpreadDegrees = 22.5f;   // each side of the tile's direction
private:
    void build (int sampleRate, int frameSize, int numVoices);
    struct Impl; std::unique_ptr<Impl> impl;
};
} // namespace phantom::spatial
