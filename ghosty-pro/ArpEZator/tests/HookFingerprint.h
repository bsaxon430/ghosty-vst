#pragma once
// HOOK LAB determinism: FNV-1a over the hooks GENERATE makes for the old moods (4 / 8 bars, a few styles / candies / seeds).
// The old moods' hooks must stay exactly as shipped (the TRAP / HIP HOP work routes around them).
#include "../HookLab.h"
#include "VarietyTests.h"

namespace arptest
{
inline uint64_t hookFingerprint (int mood)
{
    uint64_t h = 1469598103934665603ull;
    auto mix = [&] (int64_t x) { for (int b = 0; b < 8; ++b) { h ^= (uint64_t) ((x >> (8 * b)) & 0xff); h *= 1099511628211ull; } };
    for (int s = 1; s <= 3; ++s)
    {
        arp::Idea I; I.mode = mood; I.seed = (uint32_t) (s * 211); arp::applyModeDefaults (I); arp::newIdea (I);
        for (int v = 0; v < 3; ++v)
        {
            auto hp = arp::hookParams (I, v == 2 ? 8 : 4, (uint32_t) (77 + s * 13 + v));
            hp.count = 4; hp.style = v == 1 ? 2 : 0; hp.candy = v == 1 ? 2 : 0;
            for (auto& hk : arp::generateHooks (I, hp)) { for (auto& n : hk.notes) { mix (n.t); mix (n.d); mix (n.p); mix ((int64_t) std::lround (n.v * 10000)); mix (n.l.type); } for (char c : hk.label) mix (c); mix ((int64_t) std::lround (hk.score * 1e6)); }
        }
    }
    return h;
}
} // namespace arptest

namespace arptest
{
// TRAP (every shipped flavour fixed, at three DARKNESS values, the PUSH FURTHER path, HOOK LAB with the mood's styles, a borrowed
// TRAP lane) + HIP HOP (AUTO and fixed, the same paths): measured before EMO was added; these must never move
inline uint64_t urbanFingerprint (int mood)
{
    struct Ver { int v; Ver() : v (arp::newIdeaMelVer()) { arp::setNewIdeaMelVer (0); } ~Ver() { arp::setNewIdeaMelVer (v); } } asShipped;   // older sessions: the shipped melody writer
    uint64_t h = 1469598103934665603ull;
    auto mix = [&] (uint64_t x) { h = (h ^ x) * 1099511628211ull; };
    auto hooks = [&] (const arp::Idea& I, int style) {
        auto hp = arp::hookParams (I, 4, 991); hp.count = 3; hp.style = style;
        for (auto& hk : arp::generateHooks (I, hp)) for (auto& n : hk.notes) { mix ((uint64_t) n.t); mix ((uint64_t) n.d); mix ((uint64_t) n.p); mix ((uint64_t) n.l.type); } };
    const int first = mood == arp::Trap ? 0 : -1;
    for (int f = first; f < 5; ++f)
        for (int dk : { -1, 0, 100 })
        {
            std::vector<arp::RecentIdea> rec;
            for (uint32_t s = 1; s <= 6; ++s)
            {
                arp::Idea I; I.mode = mood; I.seed = s * 101 + (uint32_t) (f + 1) * 13; I.flavour = f; I.darkness = dk; arp::applyModeDefaults (I); arp::newIdea (I);
                mix (ideaFingerprint (I));
                arp::Idea F = I; arp::newIdeaFurther (F, rec); mix (ideaFingerprint (F)); mix (F.seed);
                rec.push_back (arp::recentOf (F)); if (rec.size() > 3) rec.erase (rec.begin());
                if (s == 1 && dk == -1) for (int st : { (int) arp::StyleAuto, (int) arp::StyleDark, (int) arp::StyleTriumphant, (int) arp::StyleHypnoLoop, (int) arp::StyleBoomBap, (int) arp::StyleSoulful, (int) arp::StyleLaidBack }) hooks (I, st);
            }
        }
    for (uint32_t s = 1; s <= 4; ++s)
    {   // a MIDNIGHT idea whose melody / arp / bass borrow this mood (the seeded flavour)
        arp::Idea L; L.mode = arp::Midnight; L.seed = s * 77; arp::applyModeDefaults (L); arp::newIdea (L);
        for (int l = arp::LaneArp; l < arp::kLanes; ++l) L.laneMood[(size_t) l] = mood;
        for (int l : { arp::LaneMelody, arp::LaneArp, arp::LaneBass }) arp::composeLane (L, l, true);
        mix (ideaFingerprint (L));
    }
    return h;
}
} // namespace arptest
