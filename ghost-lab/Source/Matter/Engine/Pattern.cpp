// Ghost Matter - AGPL-3.0-only (see LICENSE, NOTICE.md). Glue to Pertylizer's own Euclidean and Turing Machine (Pattern.h).
#include "Pattern.h"
#include <algorithm>
#include <cmath>

namespace matter
{
Pattern::Pattern() { prepare (48000.0); }
Pattern::~Pattern() { if (euclid) gm_euclid_free (euclid); if (turing) gm_turing_free (turing); }

void Pattern::prepare (double sr)
{
    if (euclid) gm_euclid_free (euclid);
    if (turing) gm_turing_free (turing);
    euclid = gm_euclid_new ((float) sr);
    turing = gm_turing_new ((float) sr);
    gm_turing_set (turing, "scale", 3.0f);   // Pertylizer's Pentatonic
    k = r = -1; pos = -1; lastMut = lastRange = -1; lastSwing = 0.0f; lastScale = 3.0f; lastLength = 16.0f;
    set (16, 5, 0);
}

void Pattern::set (int steps, int hits, int rotate) noexcept
{
    steps = std::clamp (steps, 1, 32); hits = std::clamp (hits, 0, steps); rotate = std::clamp (rotate, 0, 31);
    if (steps == n && hits == k && rotate == r) return;
    n = steps; k = hits; r = rotate;
    gm_euclid_set (euclid, "steps", (float) n);
    gm_euclid_set (euclid, "pulses", (float) k);
    gm_euclid_set (euclid, "rotation", (float) r);
}

void Pattern::setTuring (float mutate, float range) noexcept
{
    if (mutate != lastMut) { gm_turing_set (turing, "mutation", std::clamp (mutate, 0.0f, 1.0f)); lastMut = mutate; }
    if (range != lastRange) { gm_turing_set (turing, "range", std::clamp (range, 0.0f, 1.0f)); lastRange = range; }
}

void Pattern::applyPins (const hood::LivePins& pn) noexcept
{
    const int eb = hood::liveBase ("euclid"), tb = hood::liveBase ("turing");
    const float swing = pn.pick (eb + 3, 0.0f), scale = pn.pick (tb + 2, 3.0f), length = pn.pick (tb + 3, 16.0f);   // Pertylizer's defaults; Ghost Matter's scale is Pentatonic
    if (swing != lastSwing) { gm_euclid_set (euclid, "swing", swing); lastSwing = swing; }
    if (scale != lastScale) { gm_turing_set (turing, "scale", scale); lastScale = scale; }
    if (length != lastLength) { gm_turing_set (turing, "length", length); lastLength = length; }
}

bool Pattern::tick (int& semis) noexcept
{
    // a rising edge then low: each module advances one step
    const float clock[2] = { 1.0f, 0.0f };
    float gate[2], acc[2], pitch[2], tg[2];
    gm_euclid_process (euclid, clock, gate, acc, 2);
    gm_turing_process (turing, clock, pitch, tg, 2);
    pos = (pos + 1) % n;
    semis = (int) std::lround (pitch[0] * 12.0f);
    return gate[0] > 0.5f;
}
}
