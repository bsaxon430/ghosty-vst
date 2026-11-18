// Ghost Voice - AGPL-3.0-only (see LICENSE, NOTICE.md).
//
// TERRAIN in Ghost Voice. Brandon's rule for the open-source products: TERRAIN never adds a sound of ours; it only MOVES the
// engine's own controls. Here the engine is Seed-VC, so the land moves Seed-VC's settings and the blend (each move is heard
// on the next RENDER):
//   the tallest mountain        -> FOLLOW (Seed-VC's inference_cfg_rate: how hard it holds to the voice), up to +0.3
//   how much land there is      -> QUALITY (diffusion steps), up to +30
//   the land's lean left/right  -> the blend leans to voice A / voice B, up to 25 %
//   the deepest valley          -> PITCH down, up to an octave (whole semitones: Seed-VC's semi_tone_shift)
//   spires                      -> PITCH up, up to an octave
//   jagged land                 -> SINGER DETAIL on (Seed-VC's own reference prompt) past half way
//   movement / EVOLVE           -> MOTION depth (the puck's orbit), up to +0.25
// NATURAL / CLASSIC (build 2): the same land moves THEIR own controls instead:
//   the tallest mountain        -> RESONANCE (the engine's resonance), up to +0.6
//   how much land there is      -> WARMTH (the engine's warmth), up to +0.5
//   jagged land                 -> GRIT (the engine's grit), up to +0.4
//   lean, valleys / spires, movement: as above (the blend, the engine's own PITCH transpose, MOTION)
// BEST OF ALL: both at once (SEED-VC's parts get Seed-VC's moves, the NATURAL parts NATURAL's).
// Pure functions (any thread, no allocation).
#pragma once
#include "TerrainFeatures.h"
#include "VoiceEngine.h"
#include "DspEngine.h"
#include <array>

namespace gv
{
struct VoiceLand
{
    float follow = 0, lean = 0, motionDepth = 0;
    int steps = 0, semitones = 0;
    bool detail = false;
    float resonance = 0, warmth = 0, grit = 0;   // the NATURAL / CLASSIC moves
    std::array<ghost::terrain::Meter, 4> meters {};
    int meterCount = 0;
    bool any = false;
};
// engine picks the meter names (what the land moves on the engine in use)
VoiceLand mapVoiceLand (const ghost::terrain::Terrain& t, float motion = 0.0f, float amount = 1.0f, Engine engine = Engine::seedvc);
EngineSettings applyLand (const EngineSettings& base, const VoiceLand& land);
DspSettings applyLand (const DspSettings& base, const VoiceLand& land);
}
