#pragma once
// Mr. Mickens 2: what each bay plays on each 16th. Pure functions of (DNA, settings, seed, step), so the same bar
// always plays the same way (drag-outs match, snapshots morph cleanly) and they run on the audio thread
// (no allocation, no locks).
//   MODES   PRESERVE keeps the break's order; REWIRE swaps hits for other hits of the same kind (the skeleton stays,
//           the sounds move); MUTATE rewrites the skeleton itself.
//   REMIX   0..100 stacks transformation LAYERS, each with its own range: 1 accents + ghost dynamics (0+),
//           2 same-kind swaps (20+), 3 ratchets on hats / ghosts (40+), 4 reverses + pitch moves + swing (60+),
//           5 phrase edits: beat repeats, half-time bars, tape stops (80+).
//   GROOVE LOCK  what must survive (kick/snare placement, hit positions, swing, accents, ghost relationships) and how
//           strictly (LOCK 0..100; at 100 every ticked item is kept exactly).
//   FREEZE DNA  keep one break's groove (its DNA) and play it with another break's sounds.
//   FILLS   CLASSIC / JUNGLE / ROLLING / GLITCH / CHAOS over the last beats before a bar line; the downbeat after a
//           fill always lands on a kick (RESOLVE).
//   INTERLOCK  bay roles (main / ghost / texture / fills) and how the other bays answer the main one.
#include "BreakDNA.h"

namespace mk {

enum LockBits : uint8_t { kLockKS = 1, kLockPos = 2, kLockSwing = 4, kLockAcc = 8, kLockGhost = 16 };
enum DnaMode { kPreserve = 0, kRewire = 1, kMutate = 2 };
enum Role { kFree = 0, kMain = 1, kGhostRole = 2, kTexture = 3, kFillsRole = 4 };
enum Interlock { kIlOff = 0, kComplement = 1, kCallResp = 2, kUnison = 3, kCross = 4, kCollide = 5 };
enum FillStyle { kClassic = 0, kJungle = 1, kRolling = 2, kGlitch = 3, kChaos = 4 };

struct RemixParams { int mode = kPreserve; float remix = 0, lock = 1; uint8_t lockMask = 0x1f; bool freeze = false; uint32_t seed = 0; };

struct StepPlan {
    int slice = -1;            // -1 = rest (the bay goes quiet)
    float gain = 1, semis = 0, skip = 0, delay = 0, len = 1;   // skip: start this far into the slice; delay: in steps
    int ratchet = 1; bool reverse = false; uint8_t cls = 0;     // cls: the DNA of what plays
    bool fill = false, resolve = false, identity = true;        // identity: exactly what the classic engine plays
};

// STEP LOCKS: the SEQUENCE strip lets you keep one 16th exactly as it plays now ("lock this step"); a locked step
// wins over REMIX and INTERLOCK (fills still take over). 32 steps = the 2-bar break; step s uses lock s % 32.
struct StepLock { bool on = false; int slice = 0, ratchet = 1; bool reverse = false; float semis = 0, gain = 1, len = 1; uint8_t cls = 0; };
struct StepLocks { std::array<StepLock, 32> s {}; int count = 0; };
inline StepLock lockFrom(const StepPlan& p) { StepLock L; L.on = true; L.slice = p.slice; L.ratchet = p.ratchet; L.reverse = p.reverse; L.semis = p.semis; L.gain = p.gain; L.len = p.len; L.cls = p.cls; return L; }
inline StepPlan applyLock(const StepLock& L, StepPlan p) { if (!L.on) return p; p.slice = L.slice; p.ratchet = L.ratchet; p.reverse = L.reverse; p.semis = L.semis; p.gain = L.gain; p.len = L.len; p.cls = L.cls; p.delay = 0; p.skip = 0; p.identity = false; return p; }

inline uint32_t mix32(uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }
inline float h01(uint32_t seed, long step, uint32_t salt) { return (float) (mix32(seed * 0x9e3779b9u ^ mix32((uint32_t) step * 2654435761u + salt * 0x85ebca6bu)) & 0xffffffu) / 16777216.0f; }

// pick a slice of the live break whose kick/snare/ghost bits equal `want` (hats/cymbals matched when possible)
inline int pickLike(const BreakDNA& d, uint8_t want, int fallback, float r01) {
    const uint8_t core = (uint8_t) (want & (kK | kS | kG)); int cand[kMaxSteps]; int nc = 0, nexact = 0; int exact[kMaxSteps];
    for (int i = 0; i < d.steps; ++i) { const uint8_t c = d.cls[(size_t) i];
        if ((c & (kK | kS | kG)) != core) continue;
        if (core == 0 && (c & (kH | kC)) == 0 && (want & (kH | kC))) continue;   // wanted a hat / cymbal: not a silent slice
        if (want == 0 && c != 0) continue;                                        // wanted silence: only silent slices (a rest never grows a hit)
        cand[nc++] = i; if (c == want) exact[nexact++] = i; }
    if (nexact > 0 && r01 < 0.7f) return exact[juce::jmin(nexact - 1, (int) (r01 / 0.7f * nexact))];
    if (nc > 0) return cand[juce::jmin(nc - 1, (int) (r01 * nc))];
    return fallback;
}

inline float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }

// g = the groove DNA (the frozen one, or the live break's), d = the live break's DNA, pos = the slice the classic engine picked
inline StepPlan planStep(const BreakDNA* g, const BreakDNA* d, const RemixParams& P, int pos, long s) {
    StepPlan p; p.slice = pos;
    if (!d || d->steps <= 0 || (!g && !P.freeze)) return p;
    if (!g) g = d;
    const bool frozen = P.freeze && g != d;
    if (P.remix <= 0.0001f && !frozen) { p.cls = d->cls[(size_t) (pos % d->steps)]; return p; }
    p.identity = false;
    const float r = clampf(P.remix / 100.0f, 0, 1);
    const int n = d->steps, gp = ((pos % g->steps) + g->steps) % g->steps;
    const int bar = (int) ((s / 16) & 3), st = (int) (s & 15);
    const uint32_t seed = P.seed + 0x51ed27u;
    auto H = [&](uint32_t salt) { return h01(seed, s & 63, salt); };   // the remix repeats every 4 bars
    auto locked = [&](uint8_t bit) { return (P.lockMask & bit) && H(100u + bit) < P.lock; };
    const bool lKS = locked(kLockKS), lPos = locked(kLockPos), lSw = locked(kLockSwing), lAcc = locked(kLockAcc), lGh = locked(kLockGhost);
    const uint8_t gc = g->cls[(size_t) gp];
    uint8_t tc = gc;                                    // the target DNA for this step
    // ---- MUTATE: rewrite the skeleton (only what isn't locked)
    if (P.mode == kMutate) {
        if ((tc & kK) && !lKS && H(1) < r * 0.45f) tc = (uint8_t) ((tc & ~kK) | kH);
        else if (!(tc & (kK | kS)) && !lKS && !lPos && (st % 2 == 0 ? H(2) < r * 0.3f : H(2) < r * 0.12f)) tc = (uint8_t) ((tc & ~kG) | kK);
        if ((tc & kS) && !lKS && st % 8 != 4 && H(3) < r * 0.5f) tc = (uint8_t) ((tc & ~kS) | kG);
        if ((tc & kG) && !lGh && !lKS && H(4) < r * 0.3f) tc = (uint8_t) ((tc & ~kG) | kS);
        if (tc == 0 && !lPos && H(5) < r * 0.35f) tc = H(6) < 0.5f ? kG : kH;
    }
    // ---- which slice plays
    const uint8_t liveC = d->cls[(size_t) (pos % n)];
    float subP = 0;
    if (P.mode == kPreserve) subP = r > 0.2f ? (r - 0.2f) * 0.6f : 0;
    else if (P.mode == kRewire) subP = r;
    else subP = 0.3f + 0.7f * r;
    if ((tc & kG) && lGh) subP *= 0.3f;
    const bool must = tc != liveC && ((tc ^ liveC) & (kK | kS | kG));
    int sl = pos % n;
    if (must || H(7) < subP) sl = pickLike(*d, tc, pos % n, H(8));
    // REWIRE 50+: borrow whole beats from elsewhere in the break when their kick/snare layout matches
    if (P.mode == kRewire && r > 0.5f && !lPos && H(9) < (r - 0.5f) * 0.8f) {
        const int beatOff = 4 * (1 + (int) (H(10) * 7)); const int alt = (pos + beatOff) % n;
        if (!lKS || ((d->cls[(size_t) alt] & (kK | kS)) == (tc & (kK | kS)))) sl = alt;
    }
    // honour the kick/snare lock: never let a swap change what hits where
    if (lKS && ((d->cls[(size_t) sl] & (kK | kS)) != (tc & (kK | kS)))) sl = ((liveC & (kK | kS)) == (tc & (kK | kS))) ? pos % n : sl;
    p.slice = sl; p.cls = d->cls[(size_t) sl];
    const bool ks = (p.cls & (kK | kS)) != 0;
    // ---- layer 1: accents + ghost dynamics
    if (lAcc && sl != pos % n) p.gain = clampf(g->accent[(size_t) gp] / juce::jmax(0.05f, d->accent[(size_t) sl]), 0.5f, 2.0f);
    else if (!lAcc) { float e = st % 8 == 4 ? 0.3f : (st % 4 == 0 ? 0.15f : (st & 1 ? -0.3f : -0.1f)); if (ks) e = juce::jmax(e, 0.1f); p.gain *= 1 + r * e; }
    if (p.cls & kG) { if (lGh) { if (sl != pos % n) p.gain = clampf(g->accent[(size_t) gp] / juce::jmax(0.05f, d->accent[(size_t) sl]), 0.4f, 1.5f); }
                      else p.gain *= 0.6f + 0.7f * H(11) * r + (1 - r) * 0.4f; }
    // micro-timing: locked swing puts the swapped hit's onset where the groove's onset was
    if (lSw && (sl != pos % n || frozen)) { const float diff = g->offset[(size_t) gp] - d->offset[(size_t) sl]; if (diff > 0) p.delay = diff; else p.skip = -diff; }
    // ---- layer 3: ratchets on hats / ghosts
    if (r > 0.4f && !ks && (p.cls & (kH | kG | kC)) && !(lGh && (p.cls & kG)) && H(12) < (r - 0.4f) * 0.9f) p.ratchet = (r > 0.7f && H(13) < 0.35f) ? (H(14) < 0.5f ? 3 : 4) : 2;
    // ---- layer 4: reverses, pitch moves, new swing
    if (r > 0.6f) {
        const float l4 = (r - 0.6f) / 0.4f;
        if (!ks && !lPos && H(15) < l4 * 0.35f) p.reverse = true;
        if (!(ks && lKS) && H(16) < l4 * 0.3f) { static const float semis[] = { -12, -5, 7, 12, -7, 5 }; p.semis = semis[(int) (H(17) * 6) % 6]; }
        if (!lSw && (st & 1) && !(ks && lKS)) p.delay += l4 * 0.22f;
    }
    // ---- layer 5: phrase edits in the last bar(s) of the 4-bar phrase
    if (r > 0.8f) {
        const float l5 = (r - 0.8f) / 0.2f;
        if (P.mode == kPreserve) { if (bar == 3 && st >= 12 && !ks && H(18) < 0.4f + 0.6f * l5) { p.ratchet = 4; p.semis += (st - 11) * 2.0f * l5; } }
        else if (P.mode == kRewire) { if (bar == 3 && st >= 12 && !lKS) { const int src = (pos - st + 12 + n) % n; p.slice = src; p.cls = d->cls[(size_t) src]; p.ratchet = st >= 14 ? 2 : 1; } }
        else {
            if (bar == 2 && !lKS && l5 > 0.3f) { const int src = ((pos - st) + st / 2 + n) % n; p.slice = src; p.cls = d->cls[(size_t) src]; p.len = 1.6f; p.ratchet = 1; p.reverse = false; }   // half-time bar
            if (bar == 3 && st >= 12 && !(ks && lKS)) { p.semis -= (st - 11) * 3.0f * l5; p.len = 1.3f; }                                                                         // tape stop
        }
    }
    return p;
}

// ---- GENERATE FILL: step k of a fill `len` steps long (it ends exactly on the next downbeat)
inline StepPlan fillStep(const BreakDNA* d, int style, float intensity, int k, int len, long s, uint32_t seed, int pos) {
    StepPlan p; p.fill = true; p.identity = false; if (!d || d->steps <= 0) { p.slice = pos; return p; }
    const float I = clampf(intensity, 0, 1), u = len > 1 ? k / (float) (len - 1) : 1.0f;
    auto H = [&](uint32_t salt) { return h01(seed + 0x6f11u, s, salt); };
    auto pick = [&](int g, float r01) { return d->nCls[g] > 0 ? (int) d->byCls[g][(size_t) juce::jmin(d->nCls[g] - 1, (int) (r01 * d->nCls[g]))] : pos % d->steps; };
    const int snare = d->nCls[1] > 0 ? 1 : (d->nCls[2] > 0 ? 2 : 5), ghost = d->nCls[2] > 0 ? 2 : snare, kick = d->nCls[0] > 0 ? 0 : snare;
    switch (style) {
        case kClassic:
            p.slice = k == 0 && d->nCls[0] > 0 ? pick(kick, H(1)) : pick((k & 1) && I < 0.7f ? ghost : snare, H(2));
            p.gain = 0.65f + 0.45f * u; if (k == len - 1 && I > 0.45f) p.ratchet = 2; break;
        case kJungle:
            p.slice = (k % 3 == 2) ? pick(ghost, H(3)) : pick(snare, H(4)); if (k == len - 3 && len > 3) p.slice = pick(kick, H(5));
            p.ratchet = (k % 3 == 2 || (I > 0.6f && k % 2)) ? 2 : 1; p.semis = k * 0.5f * I; p.gain = 0.7f + 0.35f * u; break;
        case kRolling:
            p.slice = pick(snare, H(6)); p.ratchet = juce::jlimit(1, 4, 1 + (int) (u * (1.0f + 3.0f * I))); p.gain = 0.55f + 0.55f * u; p.semis = 5.0f * I * u; break;
        case kGlitch: {
            p.slice = (int) (H(7) * d->steps) % d->steps; static const int rt[] = { 1, 2, 4, 2 }; p.ratchet = rt[(int) (H(8) * 4) & 3];
            p.reverse = H(9) < 0.4f * I + 0.1f; static const float sm[] = { 0, 12, -12, 7, 0, -5 }; p.semis = sm[(int) (H(10) * 6) % 6] * I; p.len = 0.3f + 0.5f * H(11); break; }
        default: {   // CHAOS
            p.slice = (int) (H(12) * d->steps) % d->steps; p.ratchet = 1 + (int) (H(13) * (1 + 3 * I)) % 4; p.reverse = H(14) < 0.5f * I;
            p.semis = (H(15) - 0.5f) * 24.0f * I; p.len = 0.2f + 0.8f * H(16); p.gain = 0.6f + 0.6f * H(17); break; }
    }
    p.cls = d->cls[(size_t) p.slice];
    return p;
}

// the downbeat after a fill: whatever the bay would play, made sure to carry a kick
inline StepPlan resolveStep(const BreakDNA* d, StepPlan normal) {
    normal.resolve = true; normal.ratchet = 1; normal.reverse = false; normal.delay = 0; normal.skip = 0;
    if (d && d->steps > 0 && !(normal.cls & kK) && d->nCls[0] > 0) { normal.slice = d->byCls[0][0]; normal.cls = d->cls[(size_t) normal.slice]; normal.identity = false; normal.semis = 0; }
    if (normal.slice < 0 && d && d->steps > 0) { normal.slice = d->nCls[0] > 0 ? d->byCls[0][0] : 0; normal.cls = d->cls[(size_t) normal.slice]; normal.identity = false; }
    return normal;
}

// ---- INTERLOCK: a non-main bay's plan, given what the main bay plays on the same step
inline StepPlan interlockStep(int mode, int role, StepPlan own, const StepPlan& main, const BreakDNA* d, const BreakDNA* md, long s, bool inFill) {
    if (mode == kIlOff || role == kFree || role == kMain || !d || d->steps <= 0) return own;
    if (role == kFillsRole && !inFill) { own.slice = -1; own.identity = false; return own; }
    if (inFill) return own;
    own.identity = false;
    const bool mainHit = main.slice >= 0 && (main.cls & (kK | kS)); const int st = (int) (s & 15), bar = (int) (s / 16);
    auto rest = [&] { own.slice = -1; return own; };
    auto like = [&](uint8_t want, float salt) { own.slice = pickLike(*d, want, own.slice < 0 ? 0 : own.slice, h01(0x1234u, s, (uint32_t) (salt * 100))); own.cls = d->cls[(size_t) own.slice]; };
    switch (mode) {
        case kComplement: if (mainHit) return rest(); break;
        case kCallResp: if (bar % 2 == 0 && role != kGhostRole) return rest(); break;
        case kUnison: if (main.slice < 0) return rest(); own.slice = (int) ((long) main.slice * d->steps / juce::jmax(1, md ? md->steps : d->steps)) % d->steps; own.cls = d->cls[(size_t) own.slice];
                      own.ratchet = main.ratchet; own.reverse = main.reverse; own.semis = main.semis; own.delay = main.delay; break;
        case kCross: { const long c = ((s % 48) + 48) % 48; if (c % 3 != (role == kGhostRole ? 1 : 0)) return rest(); own.slice = (int) ((c / 3) * 2 % d->steps); own.cls = d->cls[(size_t) own.slice]; own.len = 1.5f; break; }
        case kCollide: if (!mainHit) return role == kGhostRole ? own : rest(); like((uint8_t) (main.cls & (kK | kS)), 1); break;
        default: break;
    }
    // the roles colour what's left
    if (role == kGhostRole) { if (own.cls & (kK | kS)) like(kG, 2); own.gain *= 0.55f; }
    else if (role == kTexture) { if (st & 1) return rest(); if (own.cls & (kK | kS)) like(kH, 3); own.len = juce::jmax(own.len, 2.0f); own.gain *= 0.7f; }
    return own;
}
// CALL & RESPONSE thins the MAIN bay in the answering bars
inline StepPlan mainUnderInterlock(int mode, StepPlan p, long s) {
    if (mode == kCallResp && ((s / 16) % 2 == 1) && !(p.cls & (kK | kS))) { p.slice = -1; p.identity = false; }
    return p;
}

} // namespace mk
