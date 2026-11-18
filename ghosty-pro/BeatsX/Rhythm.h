#pragma once
// Ghosty Beats rhythm tools. All of them edit the same Pattern / Step model the grid and tracker show, all are
// seeded (same input + same seed = same result) and the caller pushes an undo step first.
//   RHYTHM DNA: a feature vector per pattern (density per track / beat position, syncopation, velocities, swing,
//               retrigger placement, pitch movement, silence, accent map).
//   KEEP x% IDENTITY: constraint search that keeps the strongest x% of hits (strong beats / accents) and places new ones
//               where the DNA says hits belong, picking the candidate closest to the source DNA.
//   BLEND:      combine the STRUCTURE of two patterns beat by beat (not a crossfade), closest to the mixed DNA.
//   BREED:      12-20 variations; next generations lean toward what the starred favourites share.
//   GENERATE:   a whole pattern in the INTRICATE or MACHINE GUN style for the kit on the tracks.
//   BREAK CHOP: transient slicing, slice roles (kick / snare / hat), extracted rhythm, REARRANGE.
#include "BeatEngine.h"
#include "RhythmDNA.h"

namespace gb::rhythm {

inline int hitCount(const Pattern& p) { int n = 0; for (int t = 0; t < kTracks; ++t) for (int s = 0; s < p.lenOf(t); ++s) if (p.steps[(size_t) t][(size_t) s].vel) ++n; return n; }
inline float overlap(const Pattern& a, const Pattern& src) {   // share of src hits that a also has
    int both = 0, n = 0; for (int t = 0; t < kTracks; ++t) for (int s = 0; s < src.lenOf(t); ++s) if (src.steps[(size_t) t][(size_t) s].vel) { ++n; if (a.steps[(size_t) t][(size_t) s].vel) ++both; }
    return n ? (float) both / n : 1.0f;
}

// hits that can't change: locked steps, locked tracks
inline bool fixedHit(const Pattern& p, const std::array<bool, kTracks>& lockedTrack, int t, int s) { return lockedTrack[(size_t) t] || p.steps[(size_t) t][(size_t) s].locked(); }

// weighted pick of an empty step on track t
inline int pickEmpty(const Pattern& c, int t, const std::array<float, kPos>& w, juce::Random& R, const Pattern* avoid = nullptr) {
    const int L = c.lenOf(t);
    for (int pass = 0; pass < (avoid ? 1 : 2); ++pass) {   // with `avoid`: only steps the source didn't have (NEW hits)
        auto ok = [&](int s) { const auto& st = c.steps[(size_t) t][(size_t) s]; return !st.vel && !st.locked() && (pass == 1 || !avoid || !avoid->steps[(size_t) t][(size_t) s].vel); };
        float tot = 0; for (int s = 0; s < L; ++s) if (ok(s)) tot += 0.08f + w[(size_t) posOf(c, s)];
        if (tot <= 0) continue; float x = R.nextFloat() * tot;
        for (int s = 0; s < L; ++s) if (ok(s)) { x -= 0.08f + w[(size_t) posOf(c, s)]; if (x <= 0) return s; }
    }
    return -1;
}

// ---------------- KEEP x% IDENTITY, NEW HITS
inline Pattern keepIdentity(const Pattern& src, const std::array<uint8_t, kTracks>& roles, const std::array<bool, kTracks>& lockedTrack, float keep, int seed, int candidates = 48) {
    struct Hit { int t, s; float imp; };
    std::vector<Hit> hits;
    for (int t = 0; t < kTracks; ++t) for (int s = 0; s < src.lenOf(t); ++s) { const auto& st = src.steps[(size_t) t][(size_t) s]; if (!st.vel) continue;
        const int pos = posOf(src, s); hits.push_back({ t, s, st.vel / 127.0f * (0.5f + metric(pos)) * roleWeight(roles[(size_t) t]) + (st.vel >= 110 ? 0.3f : 0.0f) + (fixedHit(src, lockedTrack, t, s) ? 100.0f : 0.0f) }); }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.imp > b.imp; });
    const DNA target = extract(src, roles);
    const int keepN = juce::jlimit(0, (int) hits.size(), (int) std::lround(keep * (float) hits.size()));
    Pattern best = src; float bestScore = 1e9f;
    for (int c = 0; c < candidates; ++c) {
        juce::Random R((juce::int64) seed * 7919 + c * 104729 + 17);
        Pattern cand = src; for (int t = 0; t < kTracks; ++t) if (!lockedTrack[(size_t) t]) for (int s = 0; s < kMaxSteps; ++s) if (!src.steps[(size_t) t][(size_t) s].locked()) cand.steps[(size_t) t][(size_t) s] = Step();
        std::array<int, kTracks> toAdd {};
        for (int i = 0; i < (int) hits.size(); ++i) {
            const auto& h = hits[(size_t) i];
            // anchors (strong-beat accents on kick / snare) survive whenever we keep at least 30%
            const bool anchor = keep >= 0.3f && metric(posOf(src, h.s)) >= 0.6f && src.steps[(size_t) h.t][(size_t) h.s].vel >= 90 && (roles[(size_t) h.t] == RoleKick || roles[(size_t) h.t] == RoleSnare);
            // a little seeded wobble on the boundary so candidates differ
            const bool kept = i < keepN + (c % 3 == 0 ? 0 : (R.nextFloat() < 0.5f ? 1 : -1)) || anchor || h.imp >= 100.0f;
            if (kept) cand.steps[(size_t) h.t][(size_t) h.s] = src.steps[(size_t) h.t][(size_t) h.s]; else ++toAdd[(size_t) h.t];
        }
        for (int t = 0; t < kTracks; ++t) {
            if (lockedTrack[(size_t) t]) continue;
            std::vector<const Step*> pool; for (int s = 0; s < src.lenOf(t); ++s) if (src.steps[(size_t) t][(size_t) s].vel) pool.push_back(&src.steps[(size_t) t][(size_t) s]);
            for (int k = 0; k < toAdd[(size_t) t] && !pool.empty(); ++k) {
                const int s = pickEmpty(cand, t, target.trackPos[(size_t) t], R, &src); if (s < 0) break;
                Step st = *pool[(size_t) R.nextInt((int) pool.size())]; st.flags &= (uint8_t) ~2u;
                if (st.vel >= 110 && metric(posOf(cand, s)) < 0.4f) st.vel = (uint8_t) (60 + R.nextInt(40));   // don't invent accents on weak spots
                cand.steps[(size_t) t][(size_t) s] = st;
            }
        }
        const float ov = overlap(cand, src);
        const float score = distance(extract(cand, roles), target) + 6.0f * std::abs(ov - keep) + (keep < 0.999f && ov > 0.999f ? 5.0f : 0.0f);
        if (score < bestScore) { bestScore = score; best = cand; }
    }
    return best;
}

// ---------------- BLEND: structure of A and B, beat by beat per track
inline Pattern blend(const Pattern& a, const Pattern& b, const std::array<uint8_t, kTracks>& roles, const std::array<bool, kTracks>& lockedTrack, float w, int seed, int candidates = 48) {
    const DNA target = lerp(extract(a, roles), extract(b, roles), w);
    const Pattern& dom = w < 0.5f ? a : b;
    Pattern best = a; float bestScore = 1e9f;
    for (int c = 0; c < candidates; ++c) {
        juce::Random R((juce::int64) seed * 31337 + c * 7919 + 3);
        Pattern cand = a;
        for (int t = 0; t < kTracks; ++t) {
            if (lockedTrack[(size_t) t]) continue;
            const int L = a.lenOf(t);
            for (int s0 = 0; s0 < L; s0 += 4) {   // one beat of one track at a time comes from A or B
                const bool fromB = R.nextFloat() < w;
                for (int s = s0; s < juce::jmin(L, s0 + 4); ++s) {
                    if (a.steps[(size_t) t][(size_t) s].locked()) continue;
                    const auto& srcP = fromB ? b : a; const int sl = s % juce::jmax(1, srcP.lenOf(t));
                    cand.steps[(size_t) t][(size_t) s] = srcP.steps[(size_t) t][(size_t) sl];
                }
            }
            for (int s = 0; s < L; ++s) {   // anchors from the dominant side
                const auto& d = dom.steps[(size_t) t][(size_t) (s % juce::jmax(1, dom.lenOf(t)))];
                if ((roles[(size_t) t] == RoleKick || roles[(size_t) t] == RoleSnare) && metric(posOf(a, s)) >= 0.6f && d.vel >= 90) cand.steps[(size_t) t][(size_t) s] = d;
            }
        }
        const float score = distance(extract(cand, roles), target);
        if (score < bestScore) { bestScore = score; best = cand; }
    }
    best.evolve = dom.evolve; best.evolveStyle = dom.evolveStyle;
    return best;
}

// ---------------- MUTATE (used by BREED)
inline Pattern mutate(const Pattern& p, const std::array<uint8_t, kTracks>& roles, const std::array<bool, kTracks>& lockedTrack, const DNA& toward, float amount, juce::Random& R) {
    Pattern c = p;
    for (int t = 0; t < kTracks; ++t) {
        if (lockedTrack[(size_t) t]) continue;
        const int L = c.lenOf(t); std::vector<const Step*> pool;
        for (int s = 0; s < L; ++s) if (p.steps[(size_t) t][(size_t) s].vel) pool.push_back(&p.steps[(size_t) t][(size_t) s]);
        for (int s = 0; s < L; ++s) {
            auto& st = c.steps[(size_t) t][(size_t) s]; if (st.locked()) continue;
            const int pos = posOf(c, s); const bool anchor = st.vel >= 90 && metric(pos) >= 0.6f && (roles[(size_t) t] == RoleKick || roles[(size_t) t] == RoleSnare);
            if (st.vel && !anchor && R.nextFloat() < amount * 0.3f * (1.2f - toward.trackPos[(size_t) t][(size_t) pos])) st = Step();
            else if (!st.vel && !pool.empty() && R.nextFloat() < amount * 0.6f * toward.trackPos[(size_t) t][(size_t) pos]) { st = *pool[(size_t) R.nextInt((int) pool.size())]; st.flags &= (uint8_t) ~2u; }
            else if (st.vel && R.nextFloat() < amount * 0.25f) st.vel = (uint8_t) juce::jlimit(1, 127, st.vel + R.nextInt(41) - 20);
            if (st.vel && R.nextFloat() < amount * 0.08f * (1.0f + toward.retrig * 4)) { st.roll = (uint8_t) (2 + R.nextInt(3)); if (R.nextBool()) st.rate = 16; }
        }
    }
    return c;
}

// ---------------- BREED: generation 0 = variations of the current pattern; later generations lean toward the starred ones
inline std::vector<Pattern> breed(const Pattern& current, const std::vector<Pattern>& starred, const std::array<uint8_t, kTracks>& roles, const std::array<bool, kTracks>& lockedTrack, int count, int generation, int seed) {
    std::vector<Pattern> pool;
    juce::Random R((juce::int64) seed * 92821 + generation * 7);
    if (starred.empty()) {
        for (int i = 0; i < count * 2; ++i) {
            const float keep = 0.45f + 0.5f * (float) ((i * 37) % 100) / 100.0f;
            Pattern c = keepIdentity(current, roles, lockedTrack, keep, seed + i * 13 + generation * 1000, 16);
            if (i % 3 == 1) c = mutate(c, roles, lockedTrack, extract(current, roles), 0.3f, R);
            pool.push_back(c);
        }
    } else {
        std::vector<DNA> ds; for (auto& s : starred) ds.push_back(extract(s, roles));
        DNA mean = ds[0]; for (size_t i = 1; i < ds.size(); ++i) mean = lerp(mean, ds[i], 1.0f / (float) (i + 1));
        // consensus: hits most starred patterns share
        Pattern cons = starred[0]; for (auto& tr : cons.steps) for (auto& st : tr) st = Step();
        for (int t = 0; t < kTracks; ++t) for (int s = 0; s < kMaxSteps; ++s) { int n = 0; const Step* any = nullptr; for (auto& p : starred) if (p.steps[(size_t) t][(size_t) s].vel) { ++n; any = &p.steps[(size_t) t][(size_t) s]; }
            if (any && n * 2 >= (int) starred.size()) cons.steps[(size_t) t][(size_t) s] = *any; }
        for (int i = 0; i < count * 3; ++i) {
            const auto& pa = starred[(size_t) R.nextInt((int) starred.size())]; const auto& pb = starred[(size_t) R.nextInt((int) starred.size())];
            Pattern c = pa;
            for (int t = 0; t < kTracks; ++t) if (!lockedTrack[(size_t) t] && R.nextFloat() < 0.35f) c.steps[(size_t) t] = pb.steps[(size_t) t];   // crossover per track
            for (int t = 0; t < kTracks; ++t) for (int s = 0; s < kMaxSteps; ++s) if (cons.steps[(size_t) t][(size_t) s].vel && R.nextFloat() < 0.85f) c.steps[(size_t) t][(size_t) s] = cons.steps[(size_t) t][(size_t) s];
            pool.push_back(mutate(c, roles, lockedTrack, mean, 0.35f, R));
        }
        std::stable_sort(pool.begin(), pool.end(), [&](const Pattern& x, const Pattern& y) { return distance(extract(x, roles), mean) < distance(extract(y, roles), mean); });
    }
    std::vector<Pattern> out;   // keep them different from each other
    for (auto& c : pool) { bool dup = false; for (auto& o : out) if (hamming(c, o) < 2) dup = true; if (!dup) out.push_back(c); if ((int) out.size() >= count) break; }
    for (size_t i = 0; (int) out.size() < count && i < pool.size(); ++i) out.push_back(pool[i]);
    return out;
}

// ---------------- GENERATE a whole pattern: 0 INTRICATE (melodic, swung, ghost-rich), 1 MACHINE GUN (dense retriggers, ramps),
//                  2 DRILL (DrillStyle.h: displaced bar-2 snare, 3-3-2 hats, sliding 808), 3 REGGAETON (LatinStyle.h: the dembow, 4 / 8 bars),
//                  4 TRAP CLASSIC (TrapClassicStyle.h: rolling hats, the 808 on the kicks, snare + clap on 3, 4 bars),
//                  100 + n / 300 + n GENERATE <STYLE> / VARY through the profile n of the style engine (StyleGen.h; sty::styleFor(kit) picks it)
inline void generate16(Pattern& p, const std::array<uint8_t, kTracks>& roles, const std::array<bool, kTracks>& lockedTrack, int style, int seed, int grit = 0, const std::array<uint8_t, kTracks>* kinds = nullptr);
// GENERATE keeps the pattern's grid: the 2-bar 1/16 recipe is laid onto the pattern's own resolution (1/32 = 64 steps, 1/64 = one bar, 1/8 merges)
// grit: DRILL GRIT (0 = installer #27); kinds: what each track plays for REGGAETON (latin::kindOf) / TRAP CLASSIC (trapc::kindOf); null = from the roles
inline void generate(Pattern& p, const std::array<uint8_t, kTracks>& roles, const std::array<bool, kTracks>& lockedTrack, int style, int seed, int grit = 0, const std::array<uint8_t, kTracks>* kinds = nullptr) {
    if (p.res == 1) { generate16(p, roles, lockedTrack, style, seed, grit, kinds); return; }
    Pattern g = p; g.res = 1; for (auto& row : g.steps) for (auto& st : row) st = Step(); g.trackLen.fill(0);
    generate16(g, roles, lockedTrack, style, seed, grit, kinds);
    const double mult = 0.25 / stepBeats(p.res); const Pattern orig = p;
    p.evolveStyle = g.evolveStyle; p.evolve = g.evolve; p.fillEvery = g.fillEvery; p.seed = g.seed; p.length = juce::jlimit(1, kMaxSteps, (int) std::lround(g.length * mult));
    for (int t = 0; t < kTracks; ++t) {
        if (lockedTrack[(size_t) t]) continue;
        auto& row = p.steps[(size_t) t]; for (auto& st : row) if (!st.locked()) st = Step();
        const int gl = g.lenOf(t); p.trackLen[(size_t) t] = g.trackLen[(size_t) t] ? (uint8_t) juce::jlimit(1, kMaxSteps, (int) std::lround(gl * mult)) : (uint8_t) 0;
        const int L = p.lenOf(t);
        for (int s = 0; s < gl; ++s) { const auto& st = g.steps[(size_t) t][(size_t) s]; if (!st.vel) continue; const int d = (int) std::lround(s * mult); if (d < 0 || d >= L) continue;
            auto& dst = row[(size_t) d]; if (dst.locked()) continue; if (!dst.vel || dst.vel < st.vel) { const auto fl = dst.flags; dst = st; dst.flags = (uint8_t) (fl | (st.flags & ~2));
                if (st.hold) dst.hold = (uint8_t) juce::jlimit(1, kMaxSteps, (int) std::lround(st.hold * mult)); } }   // (a HOLD is in steps: it scales with the grid)
    }
    for (int t = 0; t < kTracks; ++t) for (int s = 0; s < kMaxSteps; ++s) if (orig.steps[(size_t) t][(size_t) s].locked()) p.steps[(size_t) t][(size_t) s] = orig.steps[(size_t) t][(size_t) s];
}
inline void generate16(Pattern& p, const std::array<uint8_t, kTracks>& roles, const std::array<bool, kTracks>& lockedTrack, int style, int seed, int grit, const std::array<uint8_t, kTracks>* kinds) {
    juce::Random R((juce::int64) seed * 48271 + style * 11);
    const Pattern orig = p;   // locked steps come back afterwards
    struct Restore { Pattern& p; const Pattern& o; ~Restore() { for (int t = 0; t < kTracks; ++t) for (int s = 0; s < kMaxSteps; ++s) if (o.steps[(size_t) t][(size_t) s].locked()) p.steps[(size_t) t][(size_t) s] = o.steps[(size_t) t][(size_t) s]; } } restore { p, orig };
    if (style == drill::kStyle) { drill::generate(p, roles, lockedTrack, seed, grit); return; }
    if (style == latin::kStyle) { latin::generate(p, roles, lockedTrack, seed, kinds); return; }
    if (style == trapc::kStyle) { trapc::generate(p, roles, lockedTrack, seed, kinds); return; }   // (its own recipe; the two older styles below are untouched)
    if (sty::isStyle(style)) { sty::generate(p, roles, lockedTrack, style, seed, kinds); return; }   // the STYLE ENGINE (StyleGen.h): GENERATE <STYLE> / VARY for every other kit
    const int L = 32; p.length = L; p.res = 1; p.evolveStyle = style; p.evolve = style ? 0.45f : 0.3f; p.fillEvery = style ? 2 : 4; p.seed = seed;
    bool kickDone = false, snareDone = false, hatDone = false; int percs = 0;
    for (int t = 0; t < kTracks; ++t) {
        if (lockedTrack[(size_t) t]) continue;
        auto& row = p.steps[(size_t) t]; for (auto& st : row) st = Step(); p.trackLen[(size_t) t] = 0;
        const int role = roles[(size_t) t];
        auto hit = [&](int s, int v) -> Step& { auto& st = row[(size_t) (s % L)]; st = Step(); st.vel = (uint8_t) juce::jlimit(1, 127, v); return st; };
        if (role == RoleKick && !kickDone) {
            kickDone = true;
            for (int bar = 0; bar < 2; ++bar) { const int o = bar * 16; hit(o, 120);
                if (style == 0) { if (R.nextFloat() < 0.7f) hit(o + 10, 100); if (R.nextFloat() < 0.5f) hit(o + 7, 85); if (R.nextFloat() < 0.3f) hit(o + 13 + R.nextInt(2), 80); }
                else { hit(o + 8, 110); for (int k = 0; k < 3; ++k) { auto& st = hit(o + 2 + R.nextInt(13), 90); if (R.nextFloat() < 0.3f) { st.roll = 2; st.rate = 16; } } } }
        } else if (role == RoleSnare && !snareDone) {
            snareDone = true;
            for (int bar = 0; bar < 2; ++bar) { const int o = bar * 16; hit(o + 4, 118); hit(o + 12, 120);
                for (int s = 0; s < 16; ++s) if (!row[(size_t) (o + s)].vel && (s & 1) && R.nextFloat() < (style == 0 ? 0.35f : 0.2f)) { auto& st = hit(o + s, 22 + R.nextInt(26)); if (style == 0) st.nudge = (int8_t) (R.nextInt(13) - 4); }
                if (style == 1) { const int s = o + 13 + R.nextInt(3); auto& st = hit(s, 100); st.roll = (uint8_t) (R.nextBool() ? 8 : 6); st.rate = (uint8_t) (R.nextBool() ? 32 : 16); st.pitchRamp = (int8_t) (R.nextBool() ? 12 : -7); st.velRamp = 60; st.accel = (int8_t) (R.nextInt(80)); } }
        } else if (role == RoleHat && !hatDone) {
            hatDone = true;
            for (int s = 0; s < L; ++s) {
                const bool eighth = (s & 1) == 0;
                if (style == 0) { if (eighth || R.nextFloat() < 0.6f) { auto& st = hit(s, eighth ? 92 + R.nextInt(20) : 38 + R.nextInt(25)); if (R.nextFloat() < 0.08f) { st.roll = 2; st.velRamp = -30; } } }
                else { auto& st = hit(s, eighth ? 100 : 70); if (R.nextFloat() < 0.3f) { st.roll = (uint8_t) (2 + R.nextInt(3)); st.rate = (uint8_t) (R.nextBool() ? 16 : 32); st.pitchRamp = (int8_t) (R.nextInt(9) - 4); st.cut = 60; } }
            }
            if (style == 0) { p.trackLen[(size_t) t] = 0; }
        } else if (role == RolePerc && percs < 2) {
            ++percs; const int len = style == 0 ? (percs == 1 ? 12 : 10) : 0;   // polymeter perc in INTRICATE
            const int n = style == 0 ? 3 : 5;
            for (int k = 0; k < n; ++k) { auto& st = hit(R.nextInt(len ? len : L), 60 + R.nextInt(50)); st.pitch = (int8_t) (style == 0 ? (R.nextInt(5) - 2) * 2 : R.nextInt(13) - 6);
                if (style == 1 && R.nextFloat() < 0.5f) { st.roll = 4; st.rate = 32; st.flags |= (uint8_t) (R.nextBool() ? 1 : 0); st.destroy = 40; } }
            p.trackLen[(size_t) t] = (uint8_t) len;
        } else if (role == RoleBreak) {
            for (int s = 0; s < L; s += 2) { auto& st = hit(s, 90 + R.nextInt(30)); st.slice = (uint8_t) (1 + (s / 2) % 16); if (style == 1 && R.nextFloat() < 0.3f) { st.roll = 4; st.rate = 16; } }
        }
    }
}

// ---------------- BREAK CHOP
// transient slicing: 3-band (low / mid / high) energy in 2.5 ms frames, onset strength = summed dB rise over 5 ms,
// local-maximum picking with a dB threshold set by the sensitivity, min gap 30 ms, then each onset is placed on the
// first sample that rises clearly above what was ringing before. Returns slice starts (first = 0).
inline std::vector<int> chop(const Sound& snd, float sensitivity, int minSlices = 8, int maxSlices = 64) {
    std::vector<float> mix; if (snd.stereo()) { mix.resize(snd.data.size()); for (size_t i = 0; i < mix.size(); ++i) mix[i] = 0.5f * (snd.data[i] + snd.right[i]); }
    const auto& x = snd.stereo() ? mix : snd.data; const int n = (int) x.size(); const double sr = snd.rate;
    const int hop = juce::jmax(16, (int) (sr * 0.0025)), frames = n / hop;
    if (frames < 8) return { 0 };
    std::array<std::vector<float>, 3> L; for (auto& v : L) v.assign((size_t) frames, -120.0f);
    const float a1 = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * 200.0f / (float) sr), a2 = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * 2500.0f / (float) sr);
    // peak envelopes: instant attack, release longer than each band's waveform period (so a ringing tail reads as smooth)
    constexpr float RL = 0.06f, RM = 0.03f, RH = 0.015f, TB = 9.0f, TS = 8.0f; constexpr int LB = 3;   // tuned on synthetic breaks (tests)
    const float rel[3] { std::exp(-1.0f / (RL * (float) sr)), std::exp(-1.0f / (RM * (float) sr)), std::exp(-1.0f / (RH * (float) sr)) };
    float l1 = 0, l2 = 0, peak = 1e-9f, env[3] {};
    for (int f = 0; f < frames; ++f) {
        for (int k = 0; k < hop; ++k) { const float v = x[(size_t) (f * hop + k)]; l1 += a1 * (v - l1); l2 += a2 * (v - l2); const float band[3] { std::abs(l1), std::abs(l2 - l1), std::abs(v - l2) };
            for (int b = 0; b < 3; ++b) env[b] = juce::jmax(band[b], env[b] * rel[b]); peak = juce::jmax(peak, std::abs(v)); }
        for (int b = 0; b < 3; ++b) L[(size_t) b][(size_t) f] = 20.0f * std::log10(env[b] + 1e-6f);
    }
    const float floorDb = 20.0f * std::log10(peak) - 45.0f;
    std::vector<float> flux((size_t) frames, 0.0f);
    for (int f = 2; f < frames; ++f) { float r = 0; for (int b = 0; b < 3; ++b) { const float now = L[(size_t) b][(size_t) f]; if (now < floorDb) continue; r += juce::jmax(0.0f, now - juce::jmax(L[(size_t) b][(size_t) juce::jmax(0, f - LB)], floorDb)); } flux[(size_t) f] = r; }
    const int gap = (int) std::ceil(0.03 * sr / hop);
    auto pick = [&](float thrDb) {
        std::vector<int> on;
        for (int f = 2; f < frames - 1; ++f) {
            if (flux[(size_t) f] < thrDb) continue;
            bool isMax = true; for (int k = juce::jmax(2, f - 4); k <= juce::jmin(frames - 1, f + 4); ++k) if (flux[(size_t) k] > flux[(size_t) f] || (flux[(size_t) k] == flux[(size_t) f] && k < f)) { isMax = false; break; }
            if (!isMax) continue;
            float lvl = -200; for (int b = 0; b < 3; ++b) lvl = juce::jmax(lvl, L[(size_t) b][(size_t) f]); if (lvl < floorDb + 6) continue;
            if (!on.empty() && f - on.back() < gap) { if (flux[(size_t) f] > flux[(size_t) on.back()]) on.back() = f; continue; }
            on.push_back(f);
        }
        return on;
    };
    float thr = juce::jlimit(1.0f, 30.0f, TB - sensitivity * TS);
    auto on = pick(thr);
    for (int it = 0; it < 10 && (int) on.size() < minSlices - 1 && thr > 1.6f; ++it) { thr *= 0.75f; on = pick(thr); }
    if ((int) on.size() > maxSlices - 1) {   // keep the strongest
        std::vector<int> idx = on; std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) { return flux[(size_t) a] > flux[(size_t) b]; });
        idx.resize((size_t) maxSlices - 1); std::sort(idx.begin(), idx.end()); on = idx;
    }
    std::vector<int> out { 0 };
    for (int f : on) {
        // the rise happened between frames f-2 and f: find the first sample clearly above what was ringing before
        const int w0 = juce::jmax(0, (f - LB) * hop), w1 = juce::jmin(n, (f + 2) * hop);
        float pre = 0; for (int k = juce::jmax(0, w0 - (int) (sr * 0.003)); k < w0; ++k) pre = juce::jmax(pre, std::abs(x[(size_t) k]));
        float post = 0; for (int k = w0; k < w1; ++k) post = juce::jmax(post, std::abs(x[(size_t) k]));
        int s0 = w0; const float lim = pre + 0.25f * juce::jmax(0.0f, post - pre); while (s0 < w1 - 1 && std::abs(x[(size_t) s0]) <= lim) ++s0;
        const int back = juce::jmax(w0 - hop, s0 - (int) (sr * 0.001)); while (s0 > back && (x[(size_t) s0] > 0) == (x[(size_t) (s0 - 1)] > 0)) --s0;   // to the zero crossing
        if (s0 - out.back() > (int) (sr * 0.02)) out.push_back(s0);
    }
    return out;
}
// what each slice sounds like: kick (low end), snare (mid noise), hat (bright), other
inline std::vector<uint8_t> sliceRoles(const Sound& snd, const std::vector<int>& sl) {
    std::vector<uint8_t> r;
    for (size_t i = 0; i < sl.size(); ++i) {
        const int a = sl[i], z = juce::jmin((int) snd.data.size(), i + 1 < sl.size() ? sl[i + 1] : (int) snd.data.size(), a + (int) (snd.rate * 0.08));
        double lo = 0, all = 0, hi = 0; float l = 0, prev = 0;
        for (int k = a; k < z; ++k) { const float v = snd.data[(size_t) k]; l += 0.02f * (v - l); lo += l * l; all += v * v; const float d = v - prev; prev = v; hi += d * d; }
        const double lr = lo / (all + 1e-9), hr = hi / (all + 1e-9);
        r.push_back(lr > 0.25 ? RoleKick : (hr > 1.2 ? RoleHat : (all > 0 ? RoleSnare : RoleOther)));
    }
    return r;
}
// slices -> steps on track t (step i plays the slice that starts there); beats = loop length in beats
inline void extractRhythm(Pattern& p, int t, const Sound& snd, const std::vector<int>& sl, double beats) {
    auto& row = p.steps[(size_t) t]; for (auto& st : row) st = Step();
    const double sb = stepBeats(p.res); const int steps = juce::jlimit(1, kMaxSteps, (int) std::lround(beats / sb));
    p.trackLen[(size_t) t] = (uint8_t) (steps == p.length ? 0 : steps);   // the break keeps its own length (polymeter), the other tracks keep theirs
    const double n = (double) snd.data.size();
    for (size_t i = 0; i < sl.size() && i < 64; ++i) {
        const int s = (int) std::lround(sl[i] / n * beats / sb); if (s < 0 || s >= steps) continue;
        float pk = 0; for (int k = sl[i]; k < juce::jmin((int) n, sl[i] + (int) (snd.rate * 0.03)); ++k) pk = juce::jmax(pk, std::abs(snd.data[(size_t) k]));
        auto& st = row[(size_t) s]; if (st.vel) continue;
        st.vel = (uint8_t) juce::jlimit(30, 127, (int) std::lround(40 + pk / 0.89f * 87)); st.slice = (uint8_t) (i + 1);
        st.nudge = (int8_t) juce::jlimit(-50, 50, (int) std::lround((sl[i] / n * beats / sb - s) * 100));   // keep the break's own feel
        st.decay = 100;
    }
}
// REARRANGE: a new slice order that keeps the groove (kick-like slices on strong beats, snare-like on backbeats)
inline void rearrange(Pattern& p, int t, const std::vector<uint8_t>& roles, int seed) {
    if (roles.empty()) return;
    juce::Random R((juce::int64) seed * 6151 + t);
    std::vector<int> kick, snare, other; for (int i = 0; i < (int) roles.size(); ++i) (roles[(size_t) i] == RoleKick ? kick : (roles[(size_t) i] == RoleSnare ? snare : other)).push_back(i + 1);
    std::vector<int> all; for (int i = 1; i <= (int) roles.size(); ++i) all.push_back(i);
    auto from = [&](const std::vector<int>& v) { return v.empty() ? all[(size_t) R.nextInt((int) all.size())] : v[(size_t) R.nextInt((int) v.size())]; };
    for (int s = 0; s < p.lenOf(t); ++s) {
        auto& st = p.steps[(size_t) t][(size_t) s]; if (!st.vel || st.locked()) continue;
        const int pos = posOf(p, s);
        st.slice = (uint8_t) (pos == 0 || pos == 8 ? from(kick) : (pos == 4 || pos == 12 ? from(snare) : (R.nextFloat() < 0.6f ? from(other.empty() ? all : other) : from(all))));
        st.nudge = 0;
    }
}

} // namespace gb::rhythm
