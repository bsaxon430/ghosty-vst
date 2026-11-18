// Ghost Matter - AGPL-3.0-only (see LICENSE, NOTICE.md). Glue to Pertylizer's own Spatial Panner and Reverb (Space.h).
#include "Space.h"
#include <algorithm>
#include <cstdlib>

namespace matter
{
Space::~Space() { freeAll(); }

void Space::freeAll()
{
    for (auto*& p : pan) { if (p) gm_panner_free (p); p = nullptr; }
    if (verb) gm_reverb_free (verb);
    verb = nullptr;
    std::free (mem); mem = nullptr;
}

void Space::prepare (double r, int block)
{
    freeAll();
    sr = r;
    maxBlock = std::max (64, block);
    for (auto*& p : pan) p = gm_panner_new ((float) sr);
    verb = gm_reverb_new ((float) sr);
    mem = (float*) std::calloc ((size_t) maxBlock * 4, sizeof (float));
    for (int i = 0; i < 4; ++i) tmp[i] = mem + (size_t) i * maxBlock;
}

void Space::reset()
{
    if (verb) gm_reverb_reset (verb);
    // the panners have no reset in Pertylizer's interface: a fresh pair (message thread only: prepare / reset)
    for (auto*& p : pan) { if (p) gm_panner_free (p); p = gm_panner_new ((float) sr); }
}

void Space::process (float* L, float* R, int n, const SpaceParams& p, const hood::LivePins* pins) noexcept
{
    if (pan[0] == nullptr) return;
    // the pad -> Pertylizer's position: X across (-1 .. +1), DISTANCE from at your head (0) to its full offset (1);
    // Y stays at Pertylizer's default (0.4, a little in front)
    const float spread = 0.25f * std::clamp (p.width, 0.0f, 1.5f);
    const float x = std::clamp (2.0f * p.x - 1.0f, -1.0f, 1.0f);
    const float dist = std::clamp (0.15f + 0.85f * p.y, 0.0f, 1.0f);
    const float er = std::clamp (p.room, 0.0f, 1.0f);   // Pertylizer's ER level (its default is 0.4)
    for (int k = 0; k < 2; ++k)
    {
        gm_panner_set (pan[k], "x", std::clamp (x + (k == 0 ? -spread : spread), -1.0f, 1.0f));
        gm_panner_set (pan[k], "distance", dist);
        gm_panner_set (pan[k], "er_level", er);
    }
    bool pinnedVerb = false;
    const int pb = hood::liveBase ("panner"), fb = hood::liveBase ("fdn");
    const bool havePins = pins != nullptr && pins->any && pb >= 0 && fb >= 0;
    if (havePins || anyPannerWas)
    {
        // each panner control you moved goes to Pertylizer exactly as set; one you let go of goes back to Pertylizer's own default
        for (int i = 0; i < 9; ++i)
        {
            const bool on = havePins && pins->on[pb + i];
            if (on) for (auto* q : pan) gm_panner_set (q, hood::liveDef (pb + i).id, pins->v[pb + i]);
            else if (pannerWas[i]) for (auto* q : pan) gm_panner_set (q, hood::liveDef (pb + i).id, hood::liveDef (pb + i).upDef);
            pannerWas[i] = on;
        }
        anyPannerWas = false;
        for (bool b : pannerWas) anyPannerWas = anyPannerWas || b;
    }
    const bool tail = p.tail > 0.001f;
    if (tail)
    {
        gm_reverb_set (verb, "room_size", 0.2f + 0.8f * er);
        gm_reverb_set (verb, "decay", std::clamp (p.tail, 0.0f, 1.0f));
        gm_reverb_set (verb, "mix", std::clamp (0.15f + 0.5f * p.tail, 0.0f, 1.0f));
        gm_reverb_set (verb, "width", std::clamp (p.width, 0.0f, 1.0f));
    }
    if (havePins || anyVerbWas)
    {
        for (int i = 0; i < 8; ++i)
        {
            const bool on = havePins && pins->on[fb + i];
            if (on) { gm_reverb_set (verb, hood::liveDef (fb + i).id, pins->v[fb + i]); pinnedVerb = true; }
            else if (verbWas[i]) gm_reverb_set (verb, hood::liveDef (fb + i).id, hood::liveDef (fb + i).upDef);
            verbWas[i] = on;
        }
        anyVerbWas = false;
        for (bool b : verbWas) anyVerbWas = anyVerbWas || b;
    }
    for (int at = 0; at < n; at += maxBlock)
    {
        const int m = std::min (maxBlock, n - at);
        float* l = L + at; float* r = R + at;
        float* inL = tmp[2]; float* inR = tmp[3];
        for (int i = 0; i < m; ++i) { inL[i] = 0.5f * l[i]; inR[i] = 0.5f * r[i]; }
        gm_panner_process (pan[0], inL, tmp[0], tmp[1], m);
        for (int i = 0; i < m; ++i) { l[i] = tmp[0][i]; r[i] = tmp[1][i]; }
        gm_panner_process (pan[1], inR, tmp[0], tmp[1], m);
        for (int i = 0; i < m; ++i) { l[i] += tmp[0][i]; r[i] += tmp[1][i]; }
        if (tail || pinnedVerb) gm_reverb_process (verb, l, r, m);
    }
}
}
