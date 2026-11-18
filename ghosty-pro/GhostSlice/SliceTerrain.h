// GHOST SLICE build 12 (v1.7.0): TERRAIN, the take shaped by a landscape (ghosty-pro/Shared/Terrain, our own code).
//
// The landscape is the shared one: x (across) = frequency on a log scale from 40 Hz to 16 kHz, depth = time across the take (0 = its
// start, 1 = its end, so a mountain further back is heard later and one that lasts the whole depth is there throughout), height =
// boost / resonance (a mountain: up to +18 dB, a tall narrow one rings) or cut (a valley), width = Q, roughness = grit.
//
// shapeTake() shapes every buffer the take plays: its preview (depth = time across the take), each key's sample and each chop
// (depth = time across that note), with the shared offline sculpt (a spectral filter that follows the landscape's slice at each
// moment, then the resonator bank for the ringing and the grit). Each buffer is level-matched to what it was (A/B compares the shape,
// not the loudness) and its peak held at the dry one's. A flat landscape is never applied: the take stays bit for bit what it was,
// so every session saved before build 12 reopens identical.
#pragma once
#include "Generate.h"
#include "TerrainMapping.h"

namespace gslice::land
{
ghost::terrain::Range range();                                   // 40 Hz .. 16 kHz, +-18 dB
bool flat (const ghost::terrain::Terrain& t);                    // nothing to apply
// the take, shaped (the dry one is left alone). Two steps, so what you hear follows the drag at once:
//   shapeTake  (withKeys = false)  the preview and the chops: what plays, drags and exports (one or two hundred ms)
//   shapeKeys                      then every key's sample (the zones, the syllable sets), from a take shapeTake made; the
//                                  keys play the dry samples until it lands (a second or two, in parallel)
std::shared_ptr<const gen::Take> shapeTake (const gen::Take& dry, const ghost::terrain::Terrain& t, bool withKeys = true);
std::shared_ptr<const gen::Take> shapeKeys (const gen::Take& shaped, const gen::Take& dry, const ghost::terrain::Terrain& t);
void shapeBuffer (juce::AudioBuffer<float>& buf, double rate, const ghost::terrain::Terrain& t);     // one buffer, in place, level-matched
}
