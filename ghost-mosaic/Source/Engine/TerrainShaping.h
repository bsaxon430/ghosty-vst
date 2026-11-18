// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
//
// TERRAIN -> matching. How the landscape on the TERRAIN page steers the engine (agreed with
// Brandon; the landscape itself is GhostBlock's shared TERRAIN component, ghosty-pro/Shared/Terrain):
//
//  LEFT -> RIGHT = FREQUENCY (log, 30 Hz .. 16 kHz). Seen from the FRONT, the land's skyline is a
//    weighting curve for MATCHING: where it stands up, that frequency band counts more when the
//    engine picks the closest library piece (raise the lows = match on the bass / kick; the highs =
//    the hats); a valley makes a band count less. A full mountain (height 1) counts as if
//    that band were 18 dB louder, a full valley 18 dB quieter. The distance sums squared POWER
//    differences (the 4th power of level), so that is a weight of 8^4 = 4096 (1/4096); a weaker
//    weighting is lost under the loudest band. The weights are scaled so their average is 1 (the
//    distance keeps its size, so NOVELTY and WAIT UNTIL CLOSE behave as before).
//    Where: Samplebrain's distance (block.cpp, block::_compare) sums squared differences of 100
//    FFT bands (band n = n..n+1 hundredths of half the sample rate: linear, ~220 Hz each at
//    44.1 kHz) blended with 12 MFCCs. The weights go on the FFT bands (engine patch 2). MFCCs are
//    a cosine transform of the whole mel spectrum, so no coefficient belongs to one frequency:
//    they are left as they are. So the weighting acts on the TONE side of TONE / TIMBRE (all of
//    it at TONE, none at TIMBRE). Each band takes the land over its own frequency span (its
//    highest point, or its deepest valley), so band 0 (0..220 Hz) answers to everything up to
//    220 Hz: the engine cannot tell a kick from a bass line any finer than that.
//
//  FRONT -> BACK = TIME ACROSS THE TARGET (front = its start). Seen from the SIDE, the skyline
//    says how wild the matching gets as the target plays: tall or rough land at a moment raises
//    NOVELTY and BOREDOM there (towards the manual's "both all the way up: semi-random"); a valley
//    lowers NOVELTY (stick closely to the target). Flat land leaves the knobs as they are.
//
//  Decoupling: a ridge that runs the whole way front to back (a frequency that always counts) is
//    the same at every moment, so it is taken out of the time curve; a ridge right across all
//    frequencies (a moment that gets wild) is the same at every frequency, so it is taken out of
//    the weights (each by subtracting the land's median along that axis).
//
//  The piece sizes stay normal controls (changing them rebuilds the library, which can't follow a
//    drag: spires stutter with REPEAT instead); the search style stays a normal control too.
//
//  The whole landscape also moves the back end, live (Brandon: "move around all the toggles"):
//    spires (tall, very thin)        -> STUTTER: REPEAT +1..+3 and SMOOTH down (short stuttering repeats)
//    plateaus (wide, raised)         -> SMOOTH up: long smooth runs of the same sound
//    mountains merging into each other -> BLEND: ORIGINAL MIX up (neighbouring sounds blur together)
//    where the land stands across    -> BRIGHTNESS: raised lows pick darker pieces, raised highs brighter ones
//                                       (each piece's spectral centroid; engine patch 3 scales its distance)
//    how many mountains              -> how many of your sounds are in play (1 = about half, 4+ = all),
//                                       picked by brightness: the sounds whose pitch sits under a mountain
//  Build 12 (Brandon: "we are using theirs and moving it around through terrain"): TERRAIN only moves Samplebrain's
//  own settings (the matching, NOVELTY / BOREDOM, SMOOTH, REPEAT, ORIGINAL MIX, which sounds are on) and adds no
//  processing of ours to the sound. The STEREO SPREAD of builds 9-11 (the right channel crossfaded towards a second
//  search) was a widener of ours and is gone: with STEREO off both channels are the left search, as upstream.
//  All of it is worked out from LandFeatures (below), a small summary of the landscape: the one place
//  that reads the shared component's model, so a new version of the component only changes features().
//
// A flat landscape gives weightsOn = timeOn = macrosOn = maskOn = false: the engine is untouched (bit-identical).
#pragma once
#include "Shaping.h"
#include "TerrainModel.h"
#include "TerrainMapping.h"
#include <juce_core/juce_core.h>
#include <vector>

namespace mosaic
{
ghost::terrain::Range terrainRange();                     // 30 Hz .. 16 kHz, as the page draws it

// a small summary of the landscape (from the component's public model)
struct LandFeatures
{
    struct Mountain { float x, z, height, width, depth, rough; };
    std::vector<Mountain> mountains;       // the raised ones (height > 0.05)
    int valleys = 0, spires = 0, plateaus = 0, merges = 0;
    float spireAmount = 0, plateauAmount = 0, mergeAmount = 0;   // 0..1
    float tallest = 0, tallestX = 0.5f, jagged = 0, mass = 0;
};
LandFeatures features (const ghost::terrain::Terrain& t);

// the landscape as the engine uses it (message thread; ~2 ms). soundX: each library sound's brightness as an x
// position (0..1 across, from its pieces' pitch); empty = no sound picking.
Shaping makeShaping (const ghost::terrain::Terrain& t, double sampleRate, const std::vector<float>& soundX = {});

// what the shape is pushing, strongest first ("MATCH ON LOWS", 0.8), for the page's bars
struct Push { juce::String name; float amount; };
std::vector<Push> pushes (const Shaping& s);

// DRAG: how a mountain is being pulled, from two moments of the land (the page calls it every frame while you
// drag). burst 0..1 = how fast; side -1..1 = left / right (moving it or widening it); lift -1..1 = down / up.
// The engine turns them into a burst of wildness + stutter (fast flicks), a brightness sweep (sideways),
// and up = push wilder / down = pull back to the target, all dying away ~0.5 s after you stop. Never saved.
struct DragFeel { float burst = 0, side = 0, lift = 0; };
DragFeel dragFeel (const ghost::terrain::Terrain& before, const ghost::terrain::Terrain& now, int peak, float seconds);

// the SHAPES menu: ready-made landscapes that each sound clearly different
struct ShapePreset { const char* name; const char* blurb; ghost::terrain::Terrain (*make)(); };
const std::vector<ShapePreset>& shapePresets();

// the engine band that holds a frequency, and a band's frequency span
int bandOfHz (float hz, double sampleRate);
void bandSpan (int band, double sampleRate, float& loHz, float& hiHz);

// a short readout of what the landscape changes ("MATCH: lows x3.1 ... | WILD: 40% of the way in")
struct ShapingSummary
{
    bool weights = false, time = false;
    float tilt = 0;                          // < 0 darker pieces preferred, > 0 brighter
    float topHz = 0, topWeight = 1;          // the band that counts most
    float lowHz = 0, lowWeight = 1;          // the band that counts least
    float wildPos = 0, wildMax = 0;          // where along the target it gets wildest, and how wild
    float calmPos = 0, calmMin = 0;          // where it sticks closest
};
ShapingSummary summarise (const Shaping& s, double sampleRate);
juce::String describe (const ShapingSummary& s);
}
