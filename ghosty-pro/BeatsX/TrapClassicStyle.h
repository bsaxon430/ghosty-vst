#pragma once
// Ghosty Beats: the TRAP CLASSIC style (GENERATE TRAP CLASSIC and the TRAP CLASSIC kit's starter beats). Original archetypes only:
// the early-2010s southern "orchestral trap" feel, 140-160 BPM read in half time (~70-80):
//   HATS:   the signature: 8ths or 16ths with ROLLS from a hat-roll vocabulary (1/32, 1/16 triplet, accelerating, pitch-rising,
//           1/64 bursts), every roll with a velocity ramp; open hats on off-beats.
//   808:    a booming long 808 on the kick positions (held to the next note), now and then sliding into its note.
//   SNARE:  snare + clap layered on 3 of every bar (16ths 8, 24, 40, 56); the 4-bar phrase ends with a snare roll into the drop.
//   CRASH on the phrase start; BRASS HIT stabs and TIMP TOM rolls (the orchestral slots); toms / snare stand in when a kit has none.
// One generation = one 4-bar pattern on the 1/16 grid; ten pattern families rotate on every press.
// Included from GenreKits.h (after BeatModel.h): include BeatModel.h, never this header directly.
#include "BeatModel.h"
#include <set>

namespace gb::trapc {

constexpr int kStyle = 4;   // GENERATE style index: 0 INTRICATE, 1 MACHINE GUN, 2 DRILL, 3 REGGAETON, 4 TRAP CLASSIC
inline uint32_t mix(uint32_t x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x; }
inline int tempoFor(int seed) { return 140 + (int) (mix((uint32_t) seed * 2654435761u + 4049u) % 21u); }   // 140..160 (felt 70-80)

enum Kind : uint8_t { KNone = 0, KKick, KBass, KSnare, KClap, KHat, KOpen, KCrash, KBrass, KTimp, KTom, KPerc };
inline uint8_t kindOf(const juce::String& name, uint8_t role) {
    const auto n = name.toLowerCase();
    if (n.contains("brass") || n.contains("stab") || n.contains("orch")) return KBrass;
    if (n.contains("timp")) return KTimp;
    switch (role) {
        case RoleKick: return KKick;
        case Role808: return KBass;
        case RoleSnare: return (n.contains("clap") || n.contains("snap")) ? (uint8_t) KClap : (uint8_t) KSnare;
        case RoleHat: return KHat;
        case RoleOpenHat: return KOpen;
        case RolePerc: return n.contains("tom") ? (uint8_t) KTom : (uint8_t) KPerc;
        default: break;
    }
    if (n.contains("crash") || n.contains("cymbal")) return KCrash;
    return KNone;
}
inline std::array<uint8_t, kTracks> kindsFromRoles(const std::array<uint8_t, kTracks>& roles) { std::array<uint8_t, kTracks> k {}; for (int t = 0; t < kTracks; ++t) k[(size_t) t] = kindOf({}, roles[(size_t) t]); return k; }

// ---------------- the hat-roll vocabulary (one step, or an 8th when `two`: the next step is left empty for it)
enum Roll : uint8_t { R32 = 0, R32x4, RTrip, RAccel, RPitch, R64, kRolls };
inline const char* rollName(int r) { static const char* n[] { "1/32", "1/32 x4", "1/16 triplet", "accelerating", "pitch-rising", "1/64" }; return n[juce::jlimit(0, (int) kRolls - 1, r)]; }
inline bool rollTwoSteps(int r) { return r == R32x4 || r == RTrip || r == RAccel || r == RPitch; }
inline void applyRoll(Step& st, int r) {
    switch (r) {
        case R32:    st.roll = 2; st.rate = 8;  st.velRamp = 35; break;
        case R32x4:  st.roll = 4; st.rate = 8;  st.velRamp = 45; break;
        case RTrip:  st.roll = 3; st.rate = 6;  st.velRamp = 30; break;
        case RAccel: st.roll = 6; st.rate = 8;  st.accel = 60; st.velRamp = 50; break;
        case RPitch: st.roll = 4; st.rate = 8;  st.pitchRamp = 7; st.velRamp = 40; break;
        default:     st.roll = 4; st.rate = 16; st.velRamp = 55; break;   // R64
    }
}

// ---------------- the pattern families (2-bar kick cells: bars 1 / 3 play `kickA`, bars 2 / 4 `kickB`; the 808 follows the kick)
struct Family {
    const char* name;
    const char* kickA; const char* kickB;   // never on 16th 8 (the snare)
    int hats;                                // 0 8ths, 1 16ths
    std::array<int, 2> rollSpots;            // 16ths in each bar where rolls may go (two-step rolls take the next step too)
    int rollsPerBar;                         // 1 or 2
    int openAt;                              // the open-hat off-beat (16th), -1 none
    std::array<int, 3> bassDeg;              // 808 degrees cycling over its notes (semitones from the root)
    float slide;                             // chance a later 808 note slides into its note
    int brass;                               // 0 phrase start only, 1 + bar 3, 2 + stabs in bar 2
    int timp;                                // 0 under the snare roll, 1 a roll at the end of bar 2 too
    bool ghost;                              // ghost snares before the 3
};
inline const std::array<Family, 10>& families() {
    static const std::array<Family, 10> f { {
        { "TC1 classic bounce",    "x.....x...x.....", "x.....x..x...x..", 0, { 6, 14 }, 2, 10, { 0, 0, 7 },  0.25f, 0, 0, false },
        { "TC2 double tap",        "xx........x.....", "x.x.......x...x.", 1, { 3, 11 }, 1, 14, { 0, 0, 0 },  0.2f,  1, 0, true },
        { "TC3 late push",         "x......x..x.....", "x......x...x..x.", 0, { 4, 12 }, 2, 6,  { 0, 3, 0 },  0.3f,  2, 1, false },
        { "TC4 half-time boom",    "x.........x.....", "x......x..x.....", 1, { 6, 13 }, 2, 10, { 0, 0, -5 }, 0.15f, 1, 1, false },
        { "TC5 skip",              "x..x......x.....", "x..x.....x.x....", 0, { 14, 6 }, 1, 2,  { 0, 0, 5 },  0.25f, 0, 0, true },
        { "TC6 triplet kick",      "x..x..x.........", "x..x..x...x.....", 1, { 11, 14 }, 2, 10, { 0, 7, 0 }, 0.2f,  2, 0, false },
        { "TC7 rolling bass",      "x.....x..x....x.", "x.....x..x.x....", 0, { 2, 12 }, 2, 14, { 0, 0, 12 }, 0.3f,  1, 1, false },
        { "TC8 orchestral stomp",  "x.........x.....", "x..x......x...x.", 1, { 6, 14 }, 1, 2,  { 0, -2, 0 }, 0.2f,  2, 1, true },
        { "TC9 stutter",           "x.x.......x.x...", "x.....x..x......", 0, { 4, 13 }, 2, 6,  { 0, 0, 3 },  0.25f, 0, 1, false },
        { "TC10 minimal",          "x.....x.........", "x......x...x....", 1, { 6, 12 }, 2, 14, { 0, 7, 0 },  0.15f, 1, 0, true },
    } };
    return f;
}
// what one generation rotates (consecutive seeds always change the family and the roll order)
inline int family(int seed) { return (int) (((int64_t) seed * 3 % 10 + 10) % 10); }
inline int rollOrder(int seed) { return (int) (((int64_t) seed * 5 % kRolls + kRolls) % kRolls); }
inline int snareRoll(int seed) { return ((seed % 3) + 3) % 3; }   // the phrase-end snare roll: 0 16ths, 1 32nds (one beat), 2 triplets
constexpr int kBars = 4, kLen = kBars * 16;
inline bool kickAt(const Family& F, int s) { const int bar = s / 16, k = s % 16; if (bar == kBars - 1 && k >= 12) return false;   // (the last beat: the snare roll into the drop)
    return k != 8 && ((bar & 1) ? F.kickB : F.kickA)[k] == 'x'; }   // (never under the snare on 3)

// ---------------- one TRAP CLASSIC generation (locked tracks are left alone; the caller restores locked steps)
inline void generate(Pattern& p, const std::array<uint8_t, kTracks>& roles, const std::array<bool, kTracks>& lockedTrack, int seed, const std::array<uint8_t, kTracks>* kindsIn = nullptr) {
    const auto kinds = kindsIn ? *kindsIn : kindsFromRoles(roles);
    juce::Random R((juce::int64) seed * 92821 + 3141);
    const auto& F = families()[(size_t) family(seed)]; const int ro = rollOrder(seed), sr = snareRoll(seed);
    p.length = kLen; p.res = 1; p.evolveStyle = 0; p.evolve = 0.0f; p.fillEvery = 0; p.seed = seed;   // (the fills are written: no engine fills on top)
    static const int roots[] { -2, -1, 0, 1, 2 };   // F, F#, G, G#, A on a G 808
    const int root = roots[R.nextInt(5)];
    bool hasTimp = false, hasTom = false; for (auto k : kinds) { hasTimp = hasTimp || k == KTimp; hasTom = hasTom || k == KTom; }
    // the 808 line: one note per kick step, degrees cycling; some later notes slide in (a long note into a fifth / octave / back down)
    struct N { int s, pitch; bool slide; }; std::vector<N> bass; int di = 0;
    for (int s = 0; s < kLen; ++s) if (kickAt(F, s)) { N n { s, root + F.bassDeg[(size_t) (di++ % 3)], false };
        if (!bass.empty() && s - bass.back().s >= 2 && R.nextFloat() < F.slide) { n.slide = true; if (n.pitch == bass.back().pitch) { static const int I[] { 7, 12, -5 }; n.pitch = juce::jlimit(-14, 12, n.pitch + I[R.nextInt(3)]); } }
        bass.push_back(n); }
    bool done[16] {};
    for (int t = 0; t < kTracks; ++t) {
        if (lockedTrack[(size_t) t]) continue;
        const int kind = kinds[(size_t) t]; if (kind == KNone || done[kind]) continue;
        done[kind] = true; auto& row = p.steps[(size_t) t]; for (auto& st : row) st = Step(); p.trackLen[(size_t) t] = 0;
        auto hit = [&](int s, int v) -> Step& { auto& st = row[(size_t) (((s % kLen) + kLen) % kLen)]; st = Step(); st.vel = (uint8_t) juce::jlimit(1, 127, v); return st; };
        auto jit = [&](int v, int r) { return v + R.nextInt(2 * r + 1) - r; };
        switch (kind) {
            case KKick: for (int s = 0; s < kLen; ++s) if (kickAt(F, s)) hit(s, s % 16 == 0 ? 110 : jit(100, 4)); break;
            case KBass:
                for (size_t i = 0; i < bass.size(); ++i) { const auto& n = bass[i]; auto& st = hit(n.s, n.s == 0 ? 94 : jit(98, 4)); st.pitch = (int8_t) n.pitch;
                    st.flags = (uint8_t) (n.slide ? 16 : 32); if (n.slide) st.glideT = (uint8_t) (std::abs(n.pitch - bass[i - 1].pitch) >= 12 ? 18 : 11);
                    const int next = i + 1 < bass.size() ? bass[i + 1].s : kLen; st.hold = (uint8_t) juce::jlimit(1, kMaxSteps, next - n.s + (i + 1 < bass.size() && bass[i + 1].slide ? 1 : 0)); }
                break;
            case KSnare:
                for (int b = 0; b < kBars; ++b) { hit(b * 16 + 8, jit(114, 4)); if (F.ghost && b < kBars - 1 && R.nextFloat() < 0.6f) hit(b * 16 + (R.nextBool() ? 5 : 15), jit(46, 6)); }
                {   const int o = (kBars - 1) * 16;   // the snare roll into the drop
                    if (sr == 0) for (int s = 12; s < 16; ++s) hit(o + s, 48 + (s - 12) * 10);
                    else if (sr == 1) { auto& st = hit(o + 12, 76); st.roll = 8; st.rate = 8; st.velRamp = 70; }
                    else { for (int s : { 12, 14 }) { auto& st = hit(o + s, s == 12 ? 62 : 78); st.roll = 3; st.rate = 6; st.velRamp = 40; } } }
                break;
            case KClap: for (int b = 0; b < kBars; ++b) hit(b * 16 + 8, jit(98, 4)); break;   // layered on the 3
            case KHat: {
                for (int s = 0; s < kLen; ++s) { const int k = s % 16; if (F.hats == 0 && (k & 1)) continue;
                    if (s / 16 == kBars - 1 && k >= 12) continue;   // the drop's last beat belongs to the snare roll
                    const int v = k == 8 ? 58 : (k == 0 ? 60 : (k % 4 == 0 ? 92 : (k % 2 == 0 ? 76 : 56))); hit(s, jit(v, 4)); }   // (softer under the kick / snare)
                int used = 0;
                for (int b = 0; b < kBars; ++b) for (int j = 0; j < F.rollsPerBar; ++j) {
                    int s = F.rollSpots[(size_t) j]; const int r = (ro + used) % kRolls; const bool two = rollTwoSteps(r);
                    if (b == kBars - 1 && s >= 11) s = 6;   // (not into the snare roll)
                    if (s == 8 || (two && s + 1 == 8) || (two && s == 15)) s = two ? 6 : 7;   // (never on / into the snare)
                    auto& st = hit(b * 16 + s, jit(84, 4)); applyRoll(st, r); if (two) row[(size_t) (b * 16 + s + 1)] = Step(); ++used; }
                break; }
            case KOpen: if (F.openAt >= 0) for (int b = 0; b < kBars; ++b) if ((b & 1) && !(b == kBars - 1 && F.openAt >= 12)) hit(b * 16 + F.openAt, jit(78, 4)); break;
            case KCrash: hit(0, 70); break;   // the phrase start
            case KBrass: hit(0, 74); if (F.brass >= 1) hit(32, 96); if (F.brass == 2) { int n = 0; for (int s : { 22, 26, 20, 28, 18 }) if (n < 2 && !kickAt(F, s)) { hit(s, jit(n ? 84 : 88, 4)); ++n; } } break;   // (the stabs in the kick's holes)
            case KTimp: case KTom: {
                if (kind == KTom && hasTimp) break;   // (the timp plays the orchestral fills)
                const int o = (kBars - 1) * 16; for (int s = 12; s < 16; ++s) hit(o + s, 56 + (s - 12) * 6);   // under the snare roll, building
                if (F.timp == 1) for (int s : { 30, 28, 26, 22 }) if (!kickAt(F, s) && !kickAt(F, s + 1)) { auto& st = hit(s, 80); st.roll = 4; st.rate = 8; st.velRamp = 50; row[(size_t) (s + 1)] = Step(); break; }   // (a roll in the kick's holes)
                break; }
            case KPerc: if (R.nextFloat() < 0.6f) hit(16 + (R.nextBool() ? 4 : 12), jit(70, 6)); break;
            default: break;
        }
    }
    juce::ignoreUnused(hasTom);
}

// (tests) every 808 event (attack or slide) sits on a kick step and every kick step has one
inline bool bassOnKicks(const Pattern& p, int tKick, int tBass) {
    for (int s = 0; s < p.length; ++s) { const bool k = p.steps[(size_t) tKick][(size_t) s].vel > 0, b = p.steps[(size_t) tBass][(size_t) s].vel > 0; if (k != b) return false; }
    return true;
}
inline int hatRolls(const Pattern& p, int tHat, std::set<int>* kinds = nullptr) {   // roll steps on the hat track (and which vocabulary entries)
    int n = 0; for (int s = 0; s < p.length; ++s) { const auto& st = p.steps[(size_t) tHat][(size_t) s]; if (!st.vel || st.roll < 2) continue; ++n;
        if (kinds) { int r = R64; if (st.rate == 8 && st.roll == 2) r = R32; else if (st.rate == 8 && st.accel) r = RAccel; else if (st.rate == 8 && st.pitchRamp) r = RPitch; else if (st.rate == 8) r = R32x4; else if (st.rate == 6) r = RTrip; kinds->insert(r); } }
    return n;
}

// the TRAP CLASSIC kit's mix (16 tracks, see GenreKits.h)
inline const std::array<float, kTracks>& kitVols() {
    static const std::array<float, kTracks> v { 0.8f, 0.6f, 0.42f, 0.4f, 0.14f, 0.22f, 0.4f, 0.4f, 0.2f, 0.3f, 0.3f, 0.3f, 0.3f, 0.3f, 0.3f, 0.3f };
    return v;
}
inline std::array<uint8_t, kTracks> kindsOf(const Model& m, const std::function<juce::String(const juce::String&)>& categoryOf) {
    std::array<uint8_t, kTracks> k {};
    for (int t = 0; t < kTracks; ++t) { const auto& nm = m.tracks[(size_t) t].sound; k[(size_t) t] = kindOf(nm, categoryOf ? roleOf(categoryOf(nm), nm) : roleOf({}, nm)); }
    return k;
}
// the TRAP CLASSIC kit's starter beats: A = the classic bounce (seed 10), B = the orchestral stomp (seed 9); both 4 bars
inline void starter(Model& m, const std::function<juce::String(const juce::String&)>& categoryOf) {
    std::array<uint8_t, kTracks> roles {}; std::array<bool, kTracks> none {};
    for (int t = 0; t < kTracks; ++t) { const auto& nm = m.tracks[(size_t) t].sound; roles[(size_t) t] = categoryOf ? roleOf(categoryOf(nm), nm) : roleOf({}, nm); }
    const auto kinds = kindsOf(m, categoryOf);
    generate(m.patterns[0], roles, none, 10, &kinds); generate(m.patterns[1], roles, none, 9, &kinds);
    m.song = { { 0, 0, m.patterns[0].bars() }, { 1, m.patterns[0].bars(), m.patterns[1].bars() } };
}

} // namespace gb::trapc
