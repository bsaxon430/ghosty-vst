// SPDX-License-Identifier: AGPL-3.0-or-later
// GHOST BREAK: the two random buttons. Pure functions (same seed, same answer), message thread only.
//   RANDOM START  a new start position for every unlocked bay, on that bay's NUDGE grid.
//   RANDOM COMBO  a new 2-bar arrangement across the unlocked bays (and unlocked drum lanes) inside the RULES.
//                 It never touches a sound setting, a locked bay, a locked step or a locked drum lane.
#pragma once
#include "Engine.h"
#include <random>

namespace gbk
{
struct Rules
{
    float density = 0.85f;   // how many 16ths start a slice (the rest hold the slice before, or go quiet)
    float chaos = 0.35f;     // how often a slice comes from somewhere else in the break
    float ratchet = 0.15f;   // stutter intensity: 1/32 and 1/64 repeats
    float reverse = 0.08f;   // reversed slices
    float trade = 0.5f;      // how much the bays take turns instead of all playing at once
    int sliceLen = 3;        // 0 = 1/4, 1 = 1/8, 2 = 1/16, 3 = MIXED
    bool newBreaks = false;  // also pick a new break for every unlocked bay
};
struct Locks { bool bay[kBays] {}; bool drum[kDrums] { true, true, true, true, true, true }; };

// RANDOM START: the new NUDGE value of each bay (-1 = leave it)
inline std::array<int, kBays> randomStart (const Pattern& P, const std::array<BayP, kBays>& B, const Locks& L, uint32_t seed)
{
    std::mt19937 rng (seed * 2654435761u + 17u); std::array<int, kBays> out; out.fill (-1);
    for (int b = 0; b < kBays; ++b)
    {
        const auto& br = P.brk[(size_t) b]; if (L.bay[b] || ! br) { (void) rng(); continue; }
        const int positions = juce::jlimit (1, 32, br->slices * 4 / gridUnit64 (B[(size_t) b].grid));
        int v = (int) (rng() % (uint32_t) positions); if (positions > 1 && v == B[(size_t) b].nudge) v = (v + 1) % positions;
        out[(size_t) b] = v;
    }
    return out;
}

namespace gen
{
inline float u01 (std::mt19937& r) { return (float) (r() & 0xffffffu) / 16777216.0f; }
// the kind of hit a built-in break has on a slice (0 kick, 1 snare, 2 other); files have no map: the position in the beat stands in
inline int kindOf (const mk::Break& br, int slice)
{
    if ((int) br.truth.size() == br.slices) { const uint8_t t = br.truth[(size_t) slice]; return (t & 1) ? 0 : (t & 2) ? 1 : 2; }
    return slice % 8 == 0 ? 0 : slice % 8 == 4 ? 1 : 2;
}
inline void drums (Pattern& P, const Locks& L, const Rules& R, std::mt19937& rng)
{
    static const char* kicks[] { "x.........x.....x.........x.....", "x.........x.......x.......x.....", "x.....x...x.....x.....x.x.......", "x.x.......x.....x.........xx....", "x.........x..x..x.....x...x....." };
    static const char* snares[] { "....x.......x.......x.......x...", "....x.......x.......x.......x.x.", "....x..x....x.......x..x....x...", "....x.......x..x....x.......x..x" };
    auto lane = [&] (int d, const char* s, int vel) { for (int i = 0; i < kSteps; ++i) P.drum[(size_t) d][(size_t) i] = s[i] == 'x' ? (uint8_t) vel : (uint8_t) 0; };
    if (! L.drum[dKick]) lane (dKick, kicks[rng() % 5u], 118);
    if (! L.drum[dSnare]) lane (dSnare, snares[rng() % 4u], 115);
    if (! L.drum[dPerc1]) { const bool on = u01 (rng) < 0.5f; for (int i = 0; i < kSteps; ++i) P.drum[dPerc1][(size_t) i] = (on && i % 8 == 4) ? (uint8_t) 96 : (i % 8 == 7 && u01 (rng) < 0.25f * R.chaos + 0.1f) ? (uint8_t) 52 : (uint8_t) 0; }   // backbeat layer + ghost notes
    if (! L.drum[dHat]) { const int every = R.density > 0.75f && u01 (rng) < 0.5f ? 1 : 2; for (int i = 0; i < kSteps; ++i) P.drum[dHat][(size_t) i] = i % every == 0 && u01 (rng) < 0.55f + 0.45f * R.density ? (uint8_t) (i % 4 == 2 ? 104 : 72) : (uint8_t) 0; }
    if (! L.drum[dOpenHat]) { const int at = (rng() % 2u) ? 6 : 14; for (int i = 0; i < kSteps; ++i) P.drum[dOpenHat][(size_t) i] = i % 16 == at && u01 (rng) < 0.8f ? (uint8_t) 90 : (uint8_t) 0; }
    if (! L.drum[dPerc2]) for (int i = 0; i < kSteps; ++i) P.drum[dPerc2][(size_t) i] = (i % 2 == 1) && u01 (rng) < 0.12f + 0.2f * R.chaos ? (uint8_t) 84 : (uint8_t) 0;
}
} // namespace gen

// RANDOM COMBO. `library` = the breaks NEW BREAKS may pick from; `active[b]` = the bay is not muted
inline Pattern randomCombo (const Pattern& in, const std::array<BayP, kBays>& B, const Rules& R, const Locks& L, const std::vector<mk::BreakPtr>& library, uint32_t seed)
{
    using namespace gen;
    Pattern P = in; std::mt19937 rng (seed * 747796405u + 2891336453u);
    // who plays on which beat: one bay leads each beat; with TRADE up, the others step back while it does
    bool plays[kBays][8]; int nActive = 0, act[kBays];
    for (int b = 0; b < kBays; ++b) if (P.brk[(size_t) b] && ! B[(size_t) b].mute) act[nActive++] = b;
    for (int beat = 0; beat < 8; ++beat)
    {
        const int lead = nActive > 0 ? act[rng() % (uint32_t) nActive] : 0;
        for (int b = 0; b < kBays; ++b) plays[b][beat] = b == lead || nActive <= 1 || u01 (rng) >= R.trade;
    }
    for (int b = 0; b < kBays; ++b)
    {
        std::mt19937 r (rng());   // every bay draws from its own stream, so a locked bay never shifts the others
        if (L.bay[b] || ! P.brk[(size_t) b]) continue;
        if (R.newBreaks && ! library.empty()) P.brk[(size_t) b] = library[r() % (uint32_t) library.size()];
        const mk::Break& br = *P.brk[(size_t) b]; const int ns = juce::jmax (1, br.slices);
        const int gridSteps = juce::jmax (1, gridUnit64 (B[(size_t) b].grid) / 4);   // allowed slice starts (a 1/4 grid = every 4th slice)
        auto snap = [&] (int s) { return ((s / gridSteps) * gridSteps % ns + ns) % ns; };
        const int unit = R.sliceLen == 0 ? 4 : R.sliceLen == 1 ? 2 : 1;
        int cursor = 0; bool sounding = false;
        for (int s = 0; s < kSteps; ++s)
        {
            Step e; const int beat = s / 4; const bool beatStart = s % 4 == 0;
            const bool locked = (P.stepLock[(size_t) b] >> s) & 1u;
            // draw the same numbers whether or not the step is locked (a lock changes that step only)
            const float dDens = u01 (r), dChaos = u01 (r), dKind = u01 (r), dRat = u01 (r), dRatN = u01 (r), dRev = u01 (r), dSync = u01 (r), dVel = u01 (r); const uint32_t pick = r();
            if (! plays[b][beat]) { e.kind = kRest; sounding = false; }
            else
            {
                const bool onUnit = s % unit == 0;
                const bool start = onUnit && (dDens < R.density || (beatStart && ! sounding));
                if (! start) e.kind = sounding ? kTie : kRest;
                else
                {
                    if (beatStart && dSync >= R.chaos) cursor = s % ns;          // the bar pulls the break back into place
                    int slice = cursor % ns;
                    if (dChaos < R.chaos)
                    {
                        // a slice from somewhere else: most of the time one with the same kind of hit, so kicks stay kicks
                        const int want = kindOf (br, slice); int found = -1;
                        if (dKind < 0.7f) for (int k = 0; k < ns; ++k) { const int c = (int) ((pick + (uint32_t) k * 7u) % (uint32_t) ns); if (c != slice && kindOf (br, c) == want) { found = c; break; } }
                        slice = found >= 0 ? found : (int) (pick % (uint32_t) ns);
                    }
                    slice = snap (slice); e.slice = (int16_t) slice; cursor = slice;
                    e.kind = kPlay; sounding = true;
                    const float late = (s % 4 == 3 || s % 8 >= 6) ? 2.0f : 0.5f;
                    if (dRat < R.ratchet * late) e.rat = dRatN < 0.7f ? 2 : dRatN < 0.8f ? 3 : 4;
                    e.rev = dRev < R.reverse;
                    e.vel = (uint8_t) (beatStart ? 118 : 96 + (int) (dVel * 22.0f));
                }
                cursor = (cursor + 1) % ns;
            }
            if (! locked) P.seq[(size_t) b][(size_t) s] = e;
            else sounding = P.seq[(size_t) b][(size_t) s].kind != kRest;
        }
    }
    drums (P, L, R, rng);
    return P;
}
} // namespace gbk
