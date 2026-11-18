// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
//
// Shaping: what the TERRAIN page does to the matching, as plain numbers the audio thread can
// copy (no heap, no pointers). Made from a landscape by makeShaping() (TerrainShaping.h) on the
// message thread; the engine smooths it on the audio thread and hands it to the renderers.
//
//   weights[100]   one weight per FFT band of the engine (band n = n..n+1 hundredths of half the
//                  sample rate, as fft.cpp's calculate_bins cuts them). A band with weight 4
//                  counts four times as much when the closest library piece is picked.
//                  weightsOn = false: no weighting at all (upstream's distance, to the bit).
//   wild[kSteps]   -1..1 along the target (0 = its start, 1 = its end): how wild the matching
//                  gets there. > 0 raises NOVELTY and BOREDOM towards "ignore the target"
//                  (the manual: both all the way up = semi-random), < 0 lowers NOVELTY towards
//                  0 (stick closely to the target). timeOn = false: the knobs are left alone.
#pragma once
#include <cstdint>
#include <cmath>

namespace mosaic
{
struct Shaping
{
    static constexpr int kBands = 100, kSteps = 128;
    bool weightsOn = false, timeOn = false;
    float weights[kBands];
    float wild[kSteps];
    // the whole-landscape moves (0..1 each, 0 = the knob as set; macrosOn = false: all 0)
    //   smooth   plateaus -> SMOOTH (stickyness) up towards 0.9: long smooth runs of pieces
    //   stutter  spires   -> REPEAT +1..+3 and SMOOTH down: short stuttering repeats
    //   blend    merging mountains -> ORIGINAL MIX up to +0.4: neighbouring sounds blur together
    // (build 12: every move is one of Samplebrain's own settings. The STEREO SPREAD of builds 9-11 is gone: it
    // crossfaded the right channel towards a second search, a widener of ours. STEREO stays the original switch.)
    bool macrosOn = false;
    float smooth = 0, stutter = 0, blend = 0;
    // how many library sounds are in play (the peak count), and which (the ones whose brightness sits
    // under a mountain); maskOn = false: every sound you switched on plays
    static constexpr int kMaxSounds = 1024;
    bool maskOn = false;
    int inPlay = 0, ofSounds = 0;
    uint32_t mask[kMaxSounds / 32];
    // BRIGHTNESS (build 10): where the land stands across, as a preference for darker (< 0) or brighter (> 0)
    // pieces, -1..1. The engine scales each piece's distance by exp (-kTilt * tilt * (2 * brightness - 1)), its
    // brightness being its spectral centroid's rank in the library (0 = darkest). 0 = off.
    float tilt = 0;
    static constexpr float kTilt = 3.4f;        // a full tilt makes the darkest piece look 30x closer than the brightest
    static float tiltFactor (float tilt, float brightness) { return tilt == 0.0f ? 1.0f : std::exp (-kTilt * tilt * (2.0f * brightness - 1.0f)); }
    // SEARCH MOVES (build 15): the engine's own TONE / TIMBRE, SHAPE / PITCH ONLY and SEARCH STYLE, moved by the land
    //   tone    the land weights frequencies  -> TONE / TIMBRE leans to TONE (the weights only act on the TONE side)
    //   timbre  rough / gritty land           -> TONE / TIMBRE leans to TIMBRE
    //   pitch   plateaus and merging mountains -> SHAPE / PITCH ONLY up (loudness wobble stops mattering in the long runs)
    //   net     wide plateaus                  -> SEARCH STYLE Closest -> Network (stays near the last piece: smooth runs)
    // 0..1 each, 0 = the knob as set; searchOn = false: all 0 and the knobs are left alone
    bool searchOn = false;
    float tone = 0, timbre = 0, pitch = 0, net = 0;
    // build 15, every live slider: the land's global character (energy = how much land there is, valley = deepest valley, tall =
    // tallest mountain, 0..1) and what stands in three frequency zones (lows / mids / highs, -1..1) at each moment of the target:
    // "the shape under the playhead". zonesOn = false: zones are 0.
    float energy = 0, valley = 0, tall = 0;
    bool zonesOn = false;
    float zl[kSteps], zm[kSteps], zh[kSteps];
    float zoneAt (const float* z, float pos) const
    {
        const float f = (pos < 0 ? 0 : (pos > 1 ? 1 : pos)) * (float) (kSteps - 1);
        const int i = (int) f;
        if (i >= kSteps - 1) return z[kSteps - 1];
        return z[i] + (z[i + 1] - z[i]) * (f - (float) i);
    }
    // DRAG (build 10, the engine fills these in live while you drag; never saved): a wildness added everywhere
    float motionWild = 0;
    Shaping() { for (auto& w : weights) w = 1.0f; for (auto& w : wild) w = 0.0f; for (auto& w : zl) w = 0.0f; for (auto& w : zm) w = 0.0f; for (auto& w : zh) w = 0.0f; for (auto& m : mask) m = 0xffffffffu; }

    bool active() const { return weightsOn || timeOn || macrosOn || searchOn || zonesOn || tilt != 0.0f || motionWild != 0.0f; }
    bool timeShaped() const { return timeOn || motionWild != 0.0f; }
    float wildNow (float pos) const { const float w = wildAt (pos) + motionWild; return w < -1 ? -1 : (w > 1 ? 1 : w); }
    bool allows (int sound) const { return ! maskOn || sound < 0 || sound >= kMaxSounds || (mask[sound >> 5] >> (sound & 31)) & 1u; }

    // how wild the matching is at a point of the target (0..1), linear between the steps
    float wildAt (float pos) const
    {
        if (! timeOn) return 0.0f;
        const float f = (pos < 0 ? 0 : (pos > 1 ? 1 : pos)) * (float) (kSteps - 1);
        const int i = (int) f;
        if (i >= kSteps - 1) return wild[kSteps - 1];
        const float t = f - (float) i;
        return wild[i] + (wild[i + 1] - wild[i]) * t;
    }

    // the search settings at a wildness w (-1..1) from the knobs' own values
    static float novelty (float base, float w)
    {
        if (w > 0) return base + (kMaxNovelty - base > 0 ? (kMaxNovelty - base) * w : 0.0f);
        return base * (1.0f + w);
    }
    static float boredom (float base, float w)
    {
        if (w > 0 && base < kMaxBoredom) return base + (kMaxBoredom - base) * w;
        return base;
    }
    static constexpr float kMaxNovelty = 0.9f, kMaxBoredom = 0.99f;
};
}
