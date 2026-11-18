#pragma once
// Ghosty Beats: the REGGAETON style (GENERATE REGGAETON and the LATIN kit's starter beats). Original archetypes only.
// The dembow: a 2-bar cell at 88-100 BPM built on the 3-3-2 "boom-ch-boom-chick":
//   KICK:   beats 1 and 3 of every bar (steps 0 and 8), plus each family's own extras (four on the floor, pickups).
//   SNARE:  the dembow figure in every bar: the "a of 1" and the "and of 2", the "a of 3" and the "and of 4" (16ths 3, 6, 11, 14),
//           on the snare or (the rim family) the rimshot, with ghosts / doubles / claps per family.
//   HATS:   straight 8ths or 16ths (light swing on the off-16ths, 0-6 %); SHAKER 16ths; CLAVE 3-2 or 2-3; congas in a tumbao,
//           bongos in a martillo, cowbell / bell accents, TIMBALE fills at the end of every 4 or 8 bars.
// One generation = one 4-bar (fills every 4 bars) or 8-bar (fills every 8 bars, a pickup at bar 4) pattern on the 1/16 grid.
// Which track plays what comes from the track's SOUND (kindOf): the LATIN kit names, else whatever a kit holds (rim, perc, toms).
// Included from GenreKits.h (after BeatModel.h): include BeatModel.h, never this header directly.
#include "BeatModel.h"

namespace gb::latin {

constexpr int kStyle = 3;   // GENERATE style index: 0 INTRICATE, 1 MACHINE GUN, 2 DRILL, 3 REGGAETON
inline uint32_t mix(uint32_t x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x; }
inline int tempoFor(int seed) { return 88 + (int) (mix((uint32_t) seed * 2246822519u + 991u) % 13u); }   // 88..100

// what each track plays (from its sound's name + role)
enum Kind : uint8_t { KNone = 0, KKick, KBass, KSnare, KRim, KClap, KHat, KOpenHat, KShaker, KClave, KCongaHi, KCongaLo, KBongoHi, KBongoLo,
                      KTimbHi, KTimbLo, KTimbRim, KBell, KPerc, KTom, KCymbal };
inline uint8_t kindOf(const juce::String& name, uint8_t role) {
    const auto n = name.toLowerCase();
    const bool low = n.endsWith(" lo") || n.contains(" low") || n.contains("tumba") || n.contains(" lo ");
    if (n.contains("timbal")) return n.contains("rim") ? (uint8_t) KTimbRim : (low ? (uint8_t) KTimbLo : (uint8_t) KTimbHi);
    if (n.contains("conga")) return low ? (uint8_t) KCongaLo : (uint8_t) KCongaHi;
    if (n.contains("bongo")) return low ? (uint8_t) KBongoLo : (uint8_t) KBongoHi;
    if (n.contains("shaker") || n.contains("maraca") || n.contains("tambourine")) return KShaker;
    if (n.contains("clave") || n.contains("wood")) return KClave;
    if (n.contains("cowbell") || n.contains("bell") || n.contains("agogo")) return KBell;
    switch (role) {
        case RoleKick: return KKick;
        case Role808: return KBass;
        case RoleSnare: return n.contains("rim") ? (uint8_t) KRim : ((n.contains("clap") || n.contains("snap")) ? (uint8_t) KClap : (uint8_t) KSnare);
        case RoleHat: return KHat;
        case RoleOpenHat: return KOpenHat;
        case RolePerc: return n.contains("tom") ? (uint8_t) KTom : (n.contains("rim") ? (uint8_t) KRim : (uint8_t) KPerc);
        default: break;
    }
    if (n.contains("crash") || n.contains("ride") || n.contains("cymbal")) return KCymbal;
    return KNone;
}

// ---------------- the pattern families (2-bar cells; "x" hit, "o" soft; the snare figure is always laid down too)
struct Family {
    const char* name;
    const char* kick1; const char* kick2;      // 16 steps each (every one has steps 0 and 8)
    const char* extra1; const char* extra2;    // the family's extra snare / rim hits (ghosts, doubles, pickups)
    bool rimLead;      // the rimshot plays the figure, the snare only backs the "and of 2" / "and of 4"
    int hats;          // 0 straight 8ths, 1 16ths, 2 light (quarters + soft 8ths)
    int clap;          // 0 none, 1 layered on the "chick" (6, 14), 2 bar 2's last "chick" only
    int congas;        // 0 none, 1 tumbao, 2 busy tumbao
    bool bongos, bell, cascara;
    int fillBars;      // 4 or 8
};
inline const std::array<Family, 10>& families() {
    static const std::array<Family, 10> f { {
        { "DEM1 dembow classic", "x...x...x...x...", "x...x...x...x...", "................", "................", false, 0, 0, 1, false, false, false, 4 },
        { "DEM2 perreo",         "x...x...x...x...", "x...x...x...x...", "................", "..............o.", false, 1, 1, 0, true,  true,  false, 8 },
        { "DEM3 bounce",         "x.......x......x", "x......xx.......", "...............o", "................", false, 0, 2, 1, true,  false, false, 4 },
        { "DEM4 slowed",         "x.......x.......", "x.......x.......", "................", ".......o........", false, 2, 0, 1, false, false, false, 8 },
        { "DEM5 rim dembow",     "x...x...x...x...", "x...x...x...x...", "................", "................", true,  0, 0, 0, true,  false, false, 4 },
        { "DEM6 double tap",     "x.......x...x...", "x.......x...x...", ".......o.......o", "...............o", false, 1, 1, 1, false, false, false, 4 },
        { "DEM7 tumbao ride",    "x...x...x...x...", "x...x...x...x...", "................", "................", false, 0, 0, 2, false, true,  false, 8 },
        { "DEM8 timbal street",  "x.......x.......", "x.......x.....x.", "................", "................", false, 2, 2, 0, true,  false, true,  4 },
        { "DEM9 pickup kick",    "x...x...x...x..x", "x...x...x..x.x..", "................", "..o.............", false, 1, 0, 1, false, true,  false, 4 },
        { "DEM10 sparse verse",  "x.......x.......", "x.......x.......", "................", "................", true,  2, 0, 1, false, false, false, 8 },
    } };
    return f;
}
// what one generation rotates (consecutive seeds always change the family; clave side and conga cell turn too)
inline int family(int seed) { return (int) (((int64_t) seed * 3 % 10 + 10) % 10); }
inline int claveSide(int seed) { return (int) ((mix((uint32_t) seed + 17u) >> 3) & 1u); }   // 0 = 3-2, 1 = 2-3
inline int congaCell(int seed) { return ((seed % 3) + 3) % 3; }
constexpr int kFigure[4] { 3, 6, 11, 14 };   // the dembow figure in every bar
inline bool isFigure(int s16) { for (int f : kFigure) if (f == s16) return true; return false; }

// (tests) the dembow figure on the snare-like tracks (snare / rim / clap) in bar b: all four figure steps sound
inline bool dembowInBar(const Pattern& p, const std::array<uint8_t, kTracks>& kinds, int bar) {
    for (int f : kFigure) { bool on = false; for (int t = 0; t < kTracks; ++t) if (kinds[(size_t) t] == KSnare || kinds[(size_t) t] == KRim || kinds[(size_t) t] == KClap) { const auto& st = p.steps[(size_t) t][(size_t) (bar * 16 + f)]; if (st.vel >= 60) on = true; } if (!on) return false; }
    return true;
}

// generic kinds when only roles are known (a kit's tracks in order: the first perc tracks become shaker, clave, conga, bongo ...)
inline std::array<uint8_t, kTracks> kindsFromRoles(const std::array<uint8_t, kTracks>& roles) {
    std::array<uint8_t, kTracks> k {}; int perc = 0; static const uint8_t order[] { KShaker, KClave, KCongaHi, KCongaLo, KBongoHi, KTimbHi, KTimbLo, KBell };
    for (int t = 0; t < kTracks; ++t) { const uint8_t r = roles[(size_t) t];
        k[(size_t) t] = r == RolePerc ? (perc < 8 ? order[perc++] : (uint8_t) KPerc) : kindOf({}, r); }
    return k;
}

// ---------------- one REGGAETON generation (locked tracks are left alone; the caller restores locked steps)
inline void generate(Pattern& p, const std::array<uint8_t, kTracks>& roles, const std::array<bool, kTracks>& lockedTrack, int seed, const std::array<uint8_t, kTracks>* kindsIn = nullptr) {
    const auto kinds = kindsIn ? *kindsIn : kindsFromRoles(roles);
    juce::Random R((juce::int64) seed * 40503 + 7777);
    const int fi = family(seed); const auto& F = families()[(size_t) fi];
    const int bars = F.fillBars, L = bars * 16;
    p.length = L; p.res = 1; p.evolveStyle = 0; p.evolve = 0.0f; p.fillEvery = 0; p.seed = seed;   // (fills are written: no engine fills on top)
    const int clave = claveSide(seed), cell = congaCell(seed);
    const int swing = R.nextInt(7);   // hats + shaker only, 0..6 % of a step on the off-16ths
    static const int degrees[] { 0, -4, 3, -2 };   // i - VI - III - VII (minor), one chord per bar
    const int keyRoot = R.nextInt(5) - 2;
    // what is in the kit: fills go on the timbales, else the toms, else the snare
    bool hasTimb = false, hasTom = false, hasCongas = false, hasBongos = false, hasRim = false;
    for (int t = 0; t < kTracks; ++t) { const auto k = kinds[(size_t) t]; hasTimb = hasTimb || k == KTimbHi || k == KTimbLo; hasTom = hasTom || k == KTom; hasRim = hasRim || (k == KRim && !lockedTrack[(size_t) t]);
                                        hasCongas = hasCongas || k == KCongaHi || k == KCongaLo; hasBongos = hasBongos || k == KBongoHi || k == KBongoLo; }
    const bool useCongas = F.congas > 0 || !hasBongos, useBongos = F.bongos || !hasCongas;   // (a kit with one of the two always gets it)
    const bool rimLead = F.rimLead && hasRim;   // (no free rim track: the snare plays the whole figure)
    auto fillBar = [&](int bar) { return bar == bars - 1; };                  // the big fill: the last bar
    auto pickupBar = [&](int bar) { return bars == 8 && bar == 3; };          // 8-bar patterns: a short pickup at the end of bar 4
    std::array<bool, 32> done {};
    for (int t = 0; t < kTracks; ++t) {
        if (lockedTrack[(size_t) t]) continue;
        const int kind = kinds[(size_t) t]; if (kind == KNone) continue;
        auto& row = p.steps[(size_t) t];
        if (done[(size_t) kind]) { if (kind != KPerc) continue; }   // one track per part (a second track of a part keeps its hits; every plain perc track gets accents)
        for (auto& st : row) st = Step(); p.trackLen[(size_t) t] = 0; done[(size_t) kind] = true;
        auto hit = [&](int s, int v) -> Step& { auto& st = row[(size_t) (((s % L) + L) % L)]; st = Step(); st.vel = (uint8_t) juce::jlimit(1, 127, v); return st; };
        auto jit = [&](int v, int r) { return v + R.nextInt(2 * r + 1) - r; };
        // gain staging inside the groove: a hand / metal hit that lands with the kick or the dembow snare sits back (it is masked there
        // anyway, and the transients would otherwise stack into one peak the bus limiter has to catch)
        auto soft = [&](int bar, int s16, int v) { const char* kk = (bar & 1) ? F.kick2 : F.kick1; return kk[s16] == 'x' ? v * 62 / 100 : (isFigure(s16) ? v * 78 / 100 : v); };
        for (int bar = 0; bar < bars; ++bar) {
            const int o = bar * 16; const bool second = (bar & 1) != 0; const bool fill = fillBar(bar), pick = pickupBar(bar);
            const char* kick = second ? F.kick2 : F.kick1; const char* extra = second ? F.extra2 : F.extra1;
            switch (kind) {
                case KKick:
                    for (int s = 0; s < 16; ++s) if (kick[s] == 'x') hit(o + s, s == 0 ? 112 : (s == 8 ? 106 : (s % 4 == 0 ? 98 : 84)));
                    if (bar % 4 == 3 && !fill && R.nextFloat() < 0.4f) hit(o + 15, 80);   // a pickup into the next 4 bars
                    break;
                case KBass: {   // the low end under the kick: the chord root on 1 and 3 (+ a push on the "a of 2" now and then), one note at a time
                    const int pitch = keyRoot + degrees[bar % 4];
                    for (int s : { 0, 8 }) { auto& st = hit(o + s, s == 0 ? 92 : 88); st.pitch = (int8_t) pitch; st.flags = 32; st.hold = 6; }
                    if (R.nextFloat() < 0.35f) { auto& st = hit(o + 7, 78); st.pitch = (int8_t) pitch; st.flags = 32; st.hold = 1; }
                    break; }
                case KSnare:
                    for (int f : kFigure) if (!rimLead || f == 6 || f == 14) hit(o + f, (f == 6 || f == 14) ? jit(rimLead ? 66 : 112, 4) : jit(98, 4));
                    for (int s = 0; s < 16; ++s) if (extra[s] == 'o' && !(fill && s >= 12)) hit(o + s, jit(52, 6));
                    if (!hasTimb && !hasTom && fill) for (int s : { 12, 13, 15 }) hit(o + s, 70 + (s - 12) * 12);   // no timbales or toms: the snare fills
                    break;
                case KRim:
                    if (rimLead) { for (int f : kFigure) hit(o + f, (f == 6 || f == 14) ? jit(98, 4) : jit(86, 4)); }
                    else { if (second && R.nextFloat() < 0.6f) hit(o + 2, jit(56, 5)); if (R.nextFloat() < 0.4f) hit(o + 10, jit(52, 5)); }   // ghost rims between the figure
                    break;
                case KClap:
                    if (F.clap == 1) for (int s : { 6, 14 }) hit(o + s, jit(68, 4));   // (layered on the snare: the two sum)
                    else if (F.clap == 2 && second) hit(o + 14, jit(72, 4));
                    break;
                case KHat:
                    for (int s = 0; s < 16; ++s) {
                        if (fill && s >= 12 && hasTimb) continue;   // the hats step aside for the timbale fill
                        int v = 0;
                        if (F.hats == 1) v = s % 4 == 0 ? 92 : (s % 2 == 0 ? 74 : 48);
                        else if (F.hats == 0) v = s % 2 ? 0 : (s % 4 == 0 ? 92 : 70);
                        else v = s % 4 == 0 ? 84 : (s % 2 == 0 ? 50 : 0);
                        if (!v) continue;
                        auto& st = hit(o + s, soft(bar, s, jit(v, 5))); if (s & 1) st.nudge = (int8_t) swing;
                    }
                    break;
                case KOpenHat: if (second && R.nextFloat() < 0.5f && !fill) hit(o + 10, 70); break;
                case KShaker:
                    for (int s = 0; s < 16; ++s) { auto& st = hit(o + s, soft(bar, s, jit(s % 4 == 2 ? 80 : (s % 2 == 0 ? 62 : 46), 4))); if (s & 1) st.nudge = (int8_t) swing; }   // (accents on the off-beat 8ths)
                    break;
                case KClave: {   // son clave across the 2-bar cell: 3-2 = 0, 6, 12 | 20, 24 ; 2-3 = 4, 8 | 16, 22, 28
                    static const std::array<int, 5> s32 { 0, 6, 12, 20, 24 }, t32 { 4, 8, 16, 22, 28 };
                    for (int s : (clave == 0 ? s32 : t32)) if ((s >= 16) == second) hit(o - (second ? 16 : 0) + s, soft(bar, s % 16, jit(86, 4)));
                    break; }
                case KCongaHi: if (useCongas) {   // tumbao on the high drum: heel / tip ghosts, the slap on 2 and 4
                    static const char* c[] { "o.o.x.o...o.....", "o.o.x.o.o.o.x...", "....x.......x..." };
                    const char* pat = F.congas == 2 ? c[1] : c[cell == 2 ? 2 : 0];
                    for (int s = 0; s < 16; ++s) { if (fill && s >= 8 && hasTimb) break; if (pat[s] == 'x') hit(o + s, soft(bar, s, jit(100, 5))); else if (pat[s] == 'o') hit(o + s, soft(bar, s, jit(44, 5))); }
                } break;
                case KCongaLo: if (useCongas) {   // the open tones on the low drum: "4" and "and of 4" (cell 1: + a push on the "a of 2")
                    if (!(fill && hasTimb)) { hit(o + 12, soft(bar, 12, jit(92, 4))); hit(o + 14, soft(bar, 14, jit(84, 4))); if (cell == 1 || F.congas == 2) hit(o + 7, soft(bar, 7, jit(70, 4))); }
                    else hit(o + 6, soft(bar, 6, jit(80, 4)));
                } break;
                case KBongoHi: if (useBongos) {   // martillo: every 8th on the high drum, accent on the beats
                    for (int s = 0; s < 16; s += 2) { if (s == 6 || s == 14) continue; if (fill && s >= 8 && hasTimb) break; hit(o + s, soft(bar, s, jit(s % 4 == 0 ? 82 : 52, 4))); }
                } break;
                case KBongoLo: if (useBongos) { if (!(fill && hasTimb)) { hit(o + 6, soft(bar, 6, jit(78, 4))); hit(o + 14, soft(bar, 14, jit(88, 4))); } } break;
                case KTimbHi: case KTimbLo: {   // the fills: a 16th run over the last half bar (alternating hi / lo, building), the pickup at bar 4
                    const bool hi = kind == KTimbHi;
                    if (fill) { const int start = (F.fillBars == 8 || R.nextBool()) ? 8 : 12;
                        for (int s = start; s < 16; ++s) { const bool mine = ((s - start) % 2 == 0) == hi || (s >= 14 && hi); if (mine) hit(o + s, soft(bar, s, juce::jmin(112, 70 + (s - start) * 6 + R.nextInt(6)))); } }
                    else if (pick) { if (hi) hit(o + 14, soft(bar, 14, 96)); else hit(o + 15, 100); }
                    else if (!hi && R.nextFloat() < 0.2f) hit(o + 15, 60);   // now and then a soft low-timbale pickup
                    break; }
                case KTimbRim: {   // cascara on the shell (the timbal family), else a few rim accents
                    if (F.cascara) { static const char* cas[] { "x.x.xx.x.x.xx.x.", "x.xx.x.x.xx.x.x." }; for (int s = 0; s < 16; ++s) { if (fill && s >= 8) break; if (cas[second ? 1 : 0][s] == 'x') hit(o + s, soft(bar, s, jit(s % 4 == 0 ? 84 : 62, 4))); } }
                    else if (!fill && second && R.nextFloat() < 0.5f) hit(o + 15, jit(70, 4));
                    break; }
                case KBell:
                    if (F.bell) { for (int s = 2; s < 16; s += 4) hit(o + s, jit(s == 14 ? 70 : 60, 4)); }   // the bell on the off-beat 8ths
                    else if (bar % 2 == 0) hit(o, soft(bar, 0, jit(84, 4)));                                // a bell accent on the cycle's 1
                    break;
                case KTom: if (!hasTimb && fill) for (int s = 12; s < 16; ++s) hit(o + s, 76 + (s - 12) * 10); break;
                case KCymbal: if (bar == 0) hit(0, 66); break;
                case KPerc: if (R.nextFloat() < 0.5f) hit(o + (second ? 7 : 15), jit(60, 6)); break;
                default: break;
            }
        }
    }
}

// the LATIN kit's mix: kick + snare carry it, the hand percussion around them (16 tracks, see GenreKits.h)
inline const std::array<float, kTracks>& kitVols() {
    static const std::array<float, kTracks> v { 0.8f, 0.85f, 0.55f, 0.48f, 0.42f, 0.32f, 0.36f, 0.5f, 0.5f, 0.42f, 0.42f, 0.5f, 0.5f, 0.38f, 0.36f, 0.45f };
    return v;
}
inline std::array<uint8_t, kTracks> kindsOf(const Model& m, const std::function<juce::String(const juce::String&)>& categoryOf) {
    std::array<uint8_t, kTracks> k {};
    for (int t = 0; t < kTracks; ++t) { const auto& nm = m.tracks[(size_t) t].sound; k[(size_t) t] = kindOf(nm, categoryOf ? roleOf(categoryOf(nm), nm) : roleOf({}, nm)); }
    return k;
}
// the LATIN kit's starter beats: A = the classic dembow (seed 10), B = the bounce (seed 4); both 4 bars
inline void starter(Model& m, const std::function<juce::String(const juce::String&)>& categoryOf) {
    std::array<uint8_t, kTracks> roles {}; std::array<bool, kTracks> none {};
    for (int t = 0; t < kTracks; ++t) { const auto& nm = m.tracks[(size_t) t].sound; roles[(size_t) t] = categoryOf ? roleOf(categoryOf(nm), nm) : roleOf({}, nm); }
    const auto kinds = kindsOf(m, categoryOf);
    generate(m.patterns[0], roles, none, 10, &kinds); generate(m.patterns[1], roles, none, 4, &kinds);
    m.song = { { 0, 0, m.patterns[0].bars() }, { 1, m.patterns[0].bars(), m.patterns[1].bars() } };
}

} // namespace gb::latin
