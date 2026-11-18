#pragma once
// TERRAIN -> sound. Turns a landscape into numbers a DSP engine uses:
//   bands (one per mountain / valley: frequency, gain, Q, drive)  for resonator / EQ banks (realtime or offline)
//   a gain curve (dB at any list of frequencies)                    for spectral filters (STFT bins)
// Pure functions on a copy of the terrain, no allocation: call them on any thread (the audio thread uses the fixed-array
// bandsAt()).
#include "TerrainModel.h"
#include <vector>

namespace ghost::terrain
{
struct Range
{
    float minHz = 30.0f, maxHz = 16000.0f;   // x = 0 .. 1, log scale
    float maxDb = 18.0f;                     // height 1 = +maxDb, -1 = -maxDb
    float minQ = 0.4f, maxQ = 40.0f;         // the widest and the narrowest mountain as filter Q
};

float xToHz (float x, const Range& r = {});
float hzToX (float hz, const Range& r = {});

// a peak's width (x units, 1 sd) as a filter Q: the narrowest mountain (kMinWidth) is maxQ, the widest (kMaxWidth) minQ,
// straight on a log-log line between them
float qFromWidth (float width, const Range& r = {});
float widthFromQ (float q, const Range& r = {});

struct Band
{
    float hz = 1000.0f;
    float gainDb = 0.0f;   // + boost / resonance, - cut
    float q = 1.0f;
    float drive = 0.0f;    // 0..1 harmonic grit for this band
    float height = 0.0f;   // -1..1 (the peak as it is at this moment)
};

// one band per peak at depth z (EVOLVE / FREEZE applied). Returns the number of bands written (<= kMaxPeaks).
int bandsAt (const Terrain& t, float z, Band* out, const Range& r = {});
void bandsAt (const Terrain& t, float z, std::vector<Band>& out, const Range& r = {});   // the same, resizing `out`

// the landscape's slice at depth z as gain in dB at each frequency in hz[0..n) (the full surface, grit included)
void gainCurveDb (const Terrain& t, float z, const float* hz, int n, float* outDb, const Range& r = {});

// short readout text for a peak: "1.24 kHz  +9.3 dB  Q 6.1"
juce::String describeBand (const Band& b);
juce::String describeHz (float hz);   // "820 Hz" / "1.2 kHz"
}
