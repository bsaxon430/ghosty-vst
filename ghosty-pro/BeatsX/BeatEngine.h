#pragma once
// Ghosty Beats engine. Real-time safe: no allocation, no locks, no string lookups in process().
// Timing is in beats (host ppq when the DAW plays, an internal clock otherwise). Each pattern has its own grid
// (1/8 ... 1/8T) and every track its own length (polymeter). collectEvents() is the ONE place that turns
// patterns into timed hits: playback, offline renders and the MIDI export all use it, so they always agree.
#include "Script.h"
#include "VintageCharacter.h"
#include <juce_dsp/juce_dsp.h>
#include <climits>

namespace gb {

struct EngineParams {
    float swing = 0, human = 0, glitch = 0, filter = 0.5f, res = 0.2f, echo = 0, verb = 0, crush = 0, drive = 0, glue = 0, volume = 0.8f;
    bool songMode = false; int pattern = 0; float bpm = 140;
    int stutter = 0; bool tape = false;             // performance (held buttons)
    bool padRepeat = false; int repeatDiv = 2; bool recording = false;
    int groove = 0; float clip = 0, room = 0;       // groove template, bus clipper + room
    // MACROS (automatable, all neutral by default = the pattern plays exactly as written)
    float chaos = 0, density = 0.5f, mutation = 0, fracture = 0, gravity = 0.5f, memory = 0.5f, tension = 0, resolve = 0;
    float ceiling = 0;                              // output ceiling in dB (0 = the old 0.95 limiter top; the plugin default is -1 dB)
    int limiter = 0;                                // 0 CLEAN (look-ahead peak limiter at the ceiling), 1 SOFT (the v1 tanh stage), 2 OFF (VOLUME only)
    bool bypassMaster = false;                      // stems: track chains + sidechain only, no bus processing (so stems sum to the dry mix)
    // per-track host automation (offsets on top of the track panel; all neutral by default)
    float tVol[kTracks] {}, tPan[kTracks] {}, tPitch[kTracks] {}, tTone[kTracks] {}, tDuck[kTracks] {};   // dB, -1..1, semitones, -0.5..0.5, 0..1
    float tDecay[kTracks] { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };                              // multiplier
    float tChar[kTracks] {};                                                                              // CHARACTER amount offset -1..1
};

inline const juce::StringArray& grooveNames() { static juce::StringArray g { "Straight", "Swing 54%", "Swing 58%", "Swing 62%", "Swing 66%", "Triplet Shuffle", "House Push" }; return g; }
// timing offset (in steps) a groove template gives local step s
inline double grooveOffset(int g, long s) {
    const bool odd = (s & 1) != 0;
    switch (g) {
        case 1: return odd ? 0.08 : 0.0;  case 2: return odd ? 0.16 : 0.0;  case 3: return odd ? 0.24 : 0.0;  case 4: return odd ? 0.32 : 0.0;
        case 5: return odd ? 1.0 / 3.0 : 0.0;
        case 6: return odd ? 0.06 : ((s % 4) == 2 ? -0.06 : 0.0);   // house push: off-beat 8ths a hair early, 16ths a hair late
        default: return 0.0;
    }
}
inline float hash01(long a, long b) { uint32_t x = (uint32_t) (a * 73856093L) ^ (uint32_t) (b * 19349663L) ^ 0x9e3779b9u; x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return (x & 0xffffff) / 16777216.0f; }

struct EvInfo { float pitch = 0, decay = 1, offset = 0, rateMul = 1; double gate = 0; uint8_t slice = 0, variant = 0; bool reverse = false; int crush = 1; int8_t pan = 0; uint8_t filt = 0;   // variant 1 freeze, 2..8 band reverse
                float slideMs = 0; bool mono = false; float releaseMs = 0; };   // step SLIDE (glide time, 0 = none), MONO (cut the ringing note), HOLD release (0 = the default cut fade)

// The rhythm brain. One step of one track -> what plays. Everything is seeded (pattern seed, bar, step, track)
// so the same project + settings + seed always gives the same notes and the same audio.
struct EventCtx {
    const Snapshot& S; const EngineParams& P; const Pattern& p;
    double sb = 0.25, origin = 0, bps = 2; bool trip = false, evolving = false, macros = false; long originSteps = 0, convSteps = 0;
    EventCtx(const Snapshot& s, const EngineParams& pp, const Pattern& pat, double org, double bpm) : S(s), P(pp), p(pat) {
        sb = stepBeats(p.res); trip = isTriplet(p.res); origin = org; bps = juce::jmax(20.0, bpm) / 60.0;
        originSteps = (long) std::llround(origin / sb);
        macros = P.chaos > 0.001f || std::abs(P.density - 0.5f) > 0.001f || P.mutation > 0.001f || P.fracture > 0.001f || std::abs(P.gravity - 0.5f) > 0.001f
              || std::abs(P.memory - 0.5f) > 0.001f || P.tension > 0.001f || P.resolve > 0.001f;
        for (auto& m : S.motifs) if (m.active()) hasMotif = true;
        evolving = p.evolve > 0.001f || p.fillEvery >= 2 || macros || hasMotif;
        convSteps = S.converge > 0 ? (long) std::llround(S.converge * 4.0 / sb) : 0;
        for (auto& L : S.locks) if (L.active()) hasLocks = true;
        if (S.nProgs > 0) for (int t = 0; t < kTracks; ++t) { int n = 0; for (int k = 0; k < p.lenOf(t); ++k) if (p.steps[(size_t) t][(size_t) k].vel) ++n; density[t] = (float) n / (float) p.lenOf(t); }
    }
    bool hasMotif = false, hasLocks = false; float density[kTracks] {};
    // MOTIF LOCK: the step a motif puts at this bar position (nullptr = none)
    const Step* motifAt(int t, long s) const {
        if (!hasMotif) return nullptr;
        const double q = std::fmod(s * sb, 4.0) / 0.25; const int pos = (int) std::lround(q); if (std::abs(q - pos) > 1e-6) return nullptr;
        for (auto& m : S.motifs) if (m.active() && m.track == t && pos >= m.start && pos < m.start + m.len) return &m.steps[(size_t) (pos - m.start)];
        return nullptr;
    }
    bool phraseLocked(int t, long s) const { if (!hasLocks) return false; const long bar = barOf(s); for (auto& L : S.locks) if (L.covers(bar, t)) return true; return false; }
    long barOf(long s) const { return (long) std::floor((origin + s * sb) / 4.0 + 1e-9); }
    const Step& raw(int t, long s) const { const long local = convSteps > 0 ? s % convSteps : s; return p.steps[(size_t) t][(size_t) (local % p.lenOf(t))]; }
    float H(long s, int t, long bar, int k) const { return hash01(bar * 7919L + p.seed * 104729L + k * 31L, s * 131L + t * 17L + k); }

    // EVOLVE + macros on one step (no rules): seeded bar-by-bar variation; anchors (kick / snare on the beat) stay put
    Step evolved(int t, long s) const {
        if (const Step* m = motifAt(t, s)) return *m;
        const Step& in = raw(t, s);
        if (!evolving || in.locked() || S.tracks[(size_t) t].lock || phraseLocked(t, s)) return in;
        const int role = S.role[(size_t) t]; const long bar = barOf(s);
        const double pos = std::fmod(s * sb, 4.0); const bool onBeat = std::abs(pos - std::round(pos)) < 1e-6;
        const bool fillBar = p.fillEvery >= 2 && ((bar + 1) % p.fillEvery) == 0, fillZone = fillBar && pos >= 3.0 - 1e-9;
        const float phrase = S.arc[(size_t) (((bar % 16) + 16) % 16)] / 100.0f;
        const float mem = juce::jlimit(0.0f, 2.0f, (1.0f - P.memory) * 2.0f);   // 0.5 -> x1, 1 -> nothing changes, 0 -> x2
        const float A = juce::jlimit(0.0f, 1.0f, (p.evolve * (0.4f + 1.2f * phrase) + P.mutation) * (1.0f - 0.8f * P.resolve));
        const float T = P.tension * (0.5f + phrase);
        const bool mg = p.evolveStyle == 1;
        auto h = [&](int k) { return H(s, t, bar, k); };
        Step st = in;
        const bool anchor = in.vel >= 80 && onBeat && (role == RoleKick || role == RoleSnare);
        if (anchor && !(A >= 0.8f && P.memory < 0.5f)) { if (P.resolve > 0.001f) st.vel = (uint8_t) juce::jlimit(1, 127, (int) (st.vel * (1.0f + 0.25f * P.resolve))); return st; }
        if (st.vel) {
            const float drop = (role == RoleHat ? A * 0.12f : (role == RoleKick ? A * 0.2f : A * 0.28f)) * mem + (P.density < 0.5f ? (0.5f - P.density) * 1.6f : 0.0f);
            if (h(1) < drop) st.vel = 0;
            else {
                st.vel = (uint8_t) juce::jlimit(1, 127, (int) std::lround(st.vel * (1.0 + (h(2) - 0.5) * (A * 0.5 + P.chaos * 0.8) * mem)));
                const float rt = (mg ? A * 0.35f : 0.0f) + T * 0.3f + P.chaos * 0.25f;
                if (h(3) < rt && st.roll == 1) { static const int R[] { 2, 3, 4, 6, 8 }; st.roll = (uint8_t) R[juce::jmin(4, (int) (h(4) * 5))]; st.rate = (uint8_t) (h(5) < 0.55f ? 16 : 8);
                    st.pitchRamp = (int8_t) std::lround((h(6) - (T > 0.01f ? 0.2 : 0.5)) * 24 * juce::jmax(A, T)); st.velRamp = (int8_t) (h(7) < 0.5f ? 60 : -60); if (P.chaos > 0.3f) st.accel = (int8_t) std::lround((h(8) - 0.4) * 120); }
                else if (!mg && role != RoleKick && h(3) < A * 0.18f) { st.roll = (uint8_t) (h(4) < 0.6f ? 2 : 3); st.velRamp = -30; }
                if (!in.timeLocked()) st.nudge = (int8_t) juce::jlimit(-50, 50, st.nudge + (int) std::lround((h(9) - 0.5) * (mg ? 0 : A * 16 * mem) + (h(10) - 0.5) * P.chaos * 80));
                if (P.resolve > 0.001f && h(11) < P.resolve) { st.roll = 1; st.rate = 0; st.pitchRamp = 0; }
            }
        } else {
            float add = 0;
            switch (role) { case RoleSnare: add = A * (mg ? 0.22f : 0.32f); break; case RoleHat: add = A * (mg ? 0.4f : 0.28f); break;
                            case RolePerc: add = A * 0.2f; break; case RoleKick: add = onBeat ? 0.0f : A * 0.1f; break; case RoleBreak: add = A * 0.2f; break; default: break; }
            if (role == RoleSnare || role == RoleHat || role == RolePerc || role == RoleBreak) add += (P.density > 0.5f ? (P.density - 0.5f) * 0.9f : 0.0f) + T * 0.25f;
            if (h(12) < add) {
                st = Step(); st.vel = (uint8_t) (role == RoleSnare ? 26 + h(13) * 30 : 38 + h(13) * 50);
                if (role == RoleBreak) { // a slice that already plays on this track
                    for (int k = 1; k <= p.lenOf(t); ++k) { const auto& o = p.steps[(size_t) t][(size_t) ((s + k * 3) % p.lenOf(t))]; if (o.vel) { st.slice = o.slice; break; } }
                }
                if ((mg || T > 0.3f) && h(14) < 0.5f) { st.roll = (uint8_t) (h(15) < 0.5f ? 4 : 8); st.rate = (uint8_t) (h(16) < 0.6f ? 16 : 32); st.pitchRamp = (int8_t) std::lround((h(17) - 0.3) * 18); }
                else if (!mg && h(14) < 0.15f) st.roll = 2;
            }
        }
        if (fillZone) {
            const double u = pos - 3.0;   // 0..1 across the last beat
            if (role == RoleSnare || role == RolePerc) {
                if (st.vel == 0 && h(18) < 0.45f + A * 0.4f) { st = Step(); st.vel = (uint8_t) juce::jlimit(30, 127, (int) (55 + u * 70)); }
                if (st.vel) { if (mg) { st.roll = (uint8_t) (h(19) < 0.5f ? 4 : 8); st.rate = (uint8_t) (h(20) < 0.5f ? 16 : 32); st.pitchRamp = 12; st.velRamp = 50; } else if (h(19) < 0.35f) st.roll = 2; }
            }
            if (role == RoleHat && h(21) < 0.5f) st.vel = 0;
        }
        return st;
    }
    // evolved + probability: did this track play here (before the conditional rules)
    Step played(int t, long s) const {
        if (s < 0) return Step();
        Step st = evolved(t, s);
        if (st.vel && st.prob < 100 && hash01(originSteps + s, t * 7 + 1) * 100.0f >= st.prob) st.vel = 0;
        return st;
    }
    // + conditional rules
    Step decide(int t, long s) const {
        Step st = played(t, s);
        const auto& tr = S.tracks[(size_t) t];
        for (int k = 0; k < 4; ++k) {
            const auto& r = tr.rules[(size_t) k]; if (r.type == RuleNone) continue;
            const long bar = barOf(s); const float x = hash01(originSteps + s, t * 13 + k * 101 + 7), pct = r.pct / 100.0f; const int n = juce::jmax(1, (int) r.n);
            switch (r.type) {
                case RuleGhostAfter: if (!st.vel && r.src != t && played(r.src, s - 1).vel && x < pct) { st = Step(); st.vel = 35; } break;
                case RuleRetrigEveryN: if (st.vel && ((bar + 1) % n) == 0 && x < pct && st.roll == 1) { st.roll = (uint8_t) (2 + (int) (x * 3 / juce::jmax(0.01f, pct))); st.rate = 16; st.velRamp = -40; } break;
                case RuleFillSilence: if (!st.vel) { bool quiet = true; for (int b = 1; b <= n && quiet; ++b) if (played(t, s - b).vel) quiet = false; if (quiet && s >= n && x < pct) { st = Step(); st.vel = 70; } } break;
                case RuleQuieterAfterLoud: if (st.vel) for (int b = 1; b <= 4; ++b) { const auto q = played(t, s - b); if (q.vel) { if (q.vel >= 100) st.vel = (uint8_t) juce::jmax(1, (int) (st.vel * (1.0f - pct))); break; } } break;
                case RuleDropEveryN: if (st.vel && ((bar + 1) % n) == 0 && x < pct) st.vel = 0; break;
                case RuleBarOfN: if (st.vel && (((bar % n) + n) % n) != juce::jlimit(0, n - 1, (int) r.src)) st.vel = 0; break;
                case RuleSkipIfTrack: if (st.vel && r.src != t && played(r.src, s).vel && x < juce::jmax(pct, 0.0001f)) st.vel = 0; break;
                case RuleAccentEveryN: if (st.vel && (s % n) == 0) st.vel = (uint8_t) juce::jlimit(1, 127, (int) (st.vel * (1.0f + pct))); break;
                default: break;
            }
        }
        // SCRIPTS (locked steps / tracks / phrases are left alone)
        if (S.nProgs > 0 && !st.locked() && !tr.lock && !phraseLocked(t, s) && !motifAt(t, s)) {
            script::Env e; e.seed = (uint32_t) p.seed; e.sAbs = originSteps + s; e.track = t;
            const double q = std::fmod(s * sb, 4.0);
            e.var[script::VBar] = (double) barOf(s); e.var[script::VStep] = (double) (s % p.lenOf(t)); e.var[script::VPos] = std::floor(q / 0.25 + 1e-9); e.var[script::VBeat] = std::floor(q + 1e-9);
            e.var[script::VTrack] = t + 1; e.var[script::VRole] = S.role[(size_t) t]; e.var[script::VPrev] = played(t, s - 1).vel ? 1 : 0; e.var[script::VNext] = played(t, s + 1).vel ? 1 : 0;
            e.var[script::VDensity] = density[t]; e.var[script::VLen] = p.lenOf(t); e.var[script::VSeed] = p.seed;
            for (int k = 0; k < S.nProgs; ++k) if (S.progs[(size_t) k]) { e.scriptIdx = k; script::run(*S.progs[(size_t) k], e, st); }
        }
        return st;
    }
};

// the arrangement's length in bars (where its last block ends; at least 1)
inline long songLength(const Snapshot& S) { long e = 1; for (int i = 0; i < S.songCount; ++i) e = juce::jmax(e, (long) (S.song[(size_t) i].startBar + S.song[(size_t) i].bars)); return e; }
// emit(track, beat, velocity 0..~1.4, EvInfo) for every hit with beat in [b0, b1). Playback, offline renders,
// MIDI export and the tests all come through here.
template <class F>
inline void collectEvents(const Snapshot& S, const EngineParams& P, double bpm, double b0, double b1, F&& emit) {
    long shiftBars = 0;   // SONG LOOP: which pass of the arrangement this region belongs to (in bars)
    auto region = [&](int pat, double origin, long nSteps) {
        const auto& p = S.patterns[(size_t) juce::jlimit(0, kPatterns - 1, pat)];
        const EventCtx C(S, P, p, origin, bpm);
        const double sb = C.sb, bps = C.bps;
        long sLo = juce::jmax(0L, (long) std::floor((b0 - origin) / sb) - 3), sHi = (long) std::ceil((b1 - origin + 0.25) / sb) + 1;
        sLo = juce::jmax(0L, sLo - (long) std::ceil(1.0 / sb));   // retriggers / fragments can run up to a beat past their step
        if (nSteps != LONG_MAX) sHi = juce::jmin(sHi, nSteps - 1);
        bool anyRules = S.nProgs > 0; for (auto& tr : S.tracks) for (auto& r : tr.rules) if (r.type) anyRules = true;
        for (long s = sLo; s <= sHi; ++s) {
            if (nSteps != LONG_MAX) {   // song: the first block that covers this bar wins (as before)
                const long bar = C.barOf(s) - shiftBars; bool mine = false;
                for (int i = 0; i < S.songCount; ++i) { const auto& b = S.song[(size_t) i]; if (bar >= b.startBar && bar < b.startBar + b.bars) { mine = std::abs((b.startBar + shiftBars) * 4.0 - origin) < 1e-9 && b.pattern == pat; break; } }
                if (!mine) continue;
            }
            const long sAbs = C.originSteps + s;
            for (int t = 0; t < kTracks; ++t) {
                if (!C.evolving && !anyRules && !C.raw(t, s).vel) continue;
                const Step st = anyRules ? C.decide(t, s) : C.played(t, s);
                if (!st.vel) continue;
                const auto& tr = S.tracks[(size_t) t];
                const bool free = !st.locked() && !tr.lock;
                int roll = juce::jlimit(1, 16, (int) st.roll);
                float vel = st.vel / 127.0f;
                if (P.glitch > 0.01f) { const float g = hash01(sAbs, t * 7 + 3); if (g < P.glitch * 0.12f) continue; if (g > 1 - P.glitch * 0.18f) roll = juce::jmax(roll, 2 + (int) (hash01(sAbs, t * 7 + 4) * 3)); }
                // SWING + GROOVE live on the 16th-note grid whatever the pattern's resolution: a 1/32 or 1/64 pattern swings its 16ths exactly
                // like a 1/16 pattern (on a 1/16 grid this is the v1 arithmetic, bit for bit), and the steps in between are warped proportionally.
                double swing = 0;
                if (!C.trip) {
                    const double q = s * sb, six = std::floor(q / 0.25 + 1e-9), fr = q / 0.25 - six; const long si = (long) six;
                    if (fr < 1e-6) swing = ((si & 1) ? (grooveOffset(P.groove, si) + P.swing * 0.5 + tr.swing * 0.5) : grooveOffset(P.groove, si)) * 0.25;
                    else { const double g0 = grooveOffset(P.groove, si), g1 = grooveOffset(P.groove, si + 1), tri = (si & 1) ? 1.0 - fr : fr;
                           swing = ((g0 + (g1 - g0) * fr) + (P.swing * 0.5 + tr.swing * 0.5) * tri) * 0.25; }
                }
                double base = origin + s * sb + swing + st.nudge / 100.0 * sb;
                if (P.human > 0.01f) { base += (hash01(sAbs, t * 7 + 5) - 0.3) * P.human * 0.06; vel *= 1.0f - hash01(sAbs, t * 7 + 6) * P.human * 0.35f; }
                if (tr.human > 0.001f) { base += (hash01(sAbs, t * 7 + 9) - 0.5) * tr.human * 0.02 * bps; vel *= 1.0f - hash01(sAbs, t * 7 + 10) * tr.human * 0.3f; }
                if (tr.accent > 0.001f) vel *= st.vel >= 120 ? 1.0f + tr.accent * 0.4f : 1.0f - tr.accent * 0.35f;
                if (free && !st.timeLocked() && std::abs(P.gravity - 0.5f) > 0.001f) {   // GRAVITY: pull hits toward (or push away from) the beat
                    const double q = std::round(base), d = q - base;
                    if (std::abs(d) > 1e-6 && std::abs(d) < 0.5) base += P.gravity > 0.5f ? d * (P.gravity - 0.5f) * 1.6 : -d * (0.5f - P.gravity) * 0.8;
                }
                EvInfo info; info.pitch = st.pitch + st.cents / 100.0f; info.decay = juce::jlimit(0.05f, 1.0f, st.decay / 100.0f);
                info.offset = juce::jlimit(0, 99, (int) st.offset) / 100.0f; info.slice = st.slice; info.reverse = st.reversed(); info.pan = st.pan; info.filt = st.filt;
                if (st.slide()) info.slideMs = st.slideMs();
                info.mono = st.mono();
                if (st.hold > 0 && !st.cut) { info.gate = st.hold * sb; info.releaseMs = 120.0f; }   // HOLD: the note's length (808 notes), released over 120 ms
                if (st.freeze) { info.variant = 1; info.gate = st.freeze * 0.01 * bps; }                    // SPECTRAL FREEZE, held for freeze x 10 ms
                else if (st.frev & 7) info.variant = (uint8_t) (1 + (st.frev & 7));                          // FREQUENCY-DEPENDENT REVERSE
                // per-hit DESTRUCTION (seeded): fragment into N pieces with an accelerating rate, pitch envelope, reverse, rate-reduce, stretch
                const float dChance = juce::jlimit(0.0f, 1.0f, st.destroy / 100.0f + (free ? P.fracture * 0.6f : 0.0f));
                if (dChance > 0.0f && hash01(sAbs, t * 7 + 20) < dChance) {
                    const auto hh = [&](int k) { return hash01(sAbs * 3 + k, t * 7 + 21 + k); };
                    const float mode = hh(12);
                    if (mode < 0.18f) { EvInfo e = info; e.variant = 1; e.gate = (0.15 + hh(13) * 0.5) * bps; if (base >= b0 && base < b1) emit(t, base, vel, e); continue; }   // freeze
                    if (mode < 0.36f) { EvInfo e = info; e.variant = (uint8_t) (2 + (int) (hh(14) * 6.99f)); if (base >= b0 && base < b1) emit(t, base, vel, e); continue; }      // band reverse
                    const int N = 2 + (int) (hh(1) * 7.0f); const double acc = std::exp2((hh(2) - 0.35) * 1.6); const float pEnv = (float) std::lround((hh(3) - 0.5) * 24);
                    double gap = sb * (0.35 + hh(4) * 0.5) / N * 2.0, tb = base;
                    const int crush = hh(5) < 0.35f ? 2 + (int) (hh(6) * 8) : 1; const float stretch = hh(7) < 0.25f ? 0.55f + hh(8) * 0.3f : 1.0f;
                    for (int k = 0; k < N; ++k) {
                        EvInfo e = info; const float u = (float) k / (float) (N - 1);
                        e.offset = juce::jlimit(0.0f, 0.95f, info.offset + (float) k / (float) N * (hh(9) < 0.5f ? 1.0f : 0.25f));
                        e.pitch += pEnv * u; e.reverse = info.reverse != (hash01(sAbs * 5 + k, t + 77) < 0.3f); e.gate = gap * 0.9; e.crush = crush; e.rateMul = stretch;
                        if (tb >= b0 && tb < b1) emit(t, tb, vel * (1.0f - 0.4f * u), e);
                        tb += gap; gap = juce::jmax(sb / 64.0, gap / acc);
                    }
                    continue;
                }
                const double rb = st.rate ? retrigBeats(st.rate) : sb / roll;
                if (st.cut) info.gate = juce::jlimit(1, 100, (int) st.cut) / 100.0 * (roll > 1 ? rb : sb);
                if (st.flam) { const double fb = base - st.flam / 1000.0 * bps; if (fb >= b0 && fb < b1) emit(t, fb, vel * 0.55f, info); }
                const double k = st.accel ? std::exp2(-st.accel / 100.0) : 1.0;   // each gap = previous * k (k < 1 = speeding up)
                double tb = base, gap = rb;
                for (int r = 0; r < roll; ++r) {
                    if (r > 0) { tb = st.rate || st.accel ? tb + gap : base + r * sb / roll; if (st.accel) gap *= k; }
                    if (tb < b0 || tb >= b1) continue;
                    float m;
                    if (st.velRamp != 0) { const float u = roll > 1 ? (float) r / (float) (roll - 1) : 1.0f; m = st.velRamp > 0 ? 1.0f - st.velRamp / 100.0f * (1.0f - u) : 1.0f + st.velRamp / 100.0f * u; }
                    else m = (st.rate == 0 && roll > 1) ? 0.7f + 0.3f * r / (roll - 1) : 1.0f;
                    EvInfo e = info; if (st.pitchRamp != 0 && roll > 1) e.pitch += st.pitchRamp * (float) r / (float) (roll - 1);
                    emit(t, tb, vel * juce::jmax(0.02f, m), e);
                }
            }
        }
    };
    if (P.songMode && S.songCount > 0) {
        // SONG LOOP (Brandon 2026-11-21): the arrangement starts over when it ends (A B C D A B C D ...), for as long as the DAW plays
        const long len = songLength(S); const long k0 = juce::jmax(0L, (long) std::floor((b0 - 1.0) / (len * 4.0))), k1 = (long) std::floor((b1 + 1.0) / (len * 4.0));
        for (long k = k0; k <= k1; ++k) {
            shiftBars = k * len;
            for (int i = 0; i < S.songCount; ++i) {
                const auto& b = S.song[(size_t) i]; const double o = (b.startBar + shiftBars) * 4.0, e = o + b.bars * 4.0;
                if (e + 1.0 < b0 || o - 1.0 > b1) continue;
                bool dup = false; for (int j = 0; j < i; ++j) if (S.song[(size_t) j].startBar == b.startBar && S.song[(size_t) j].pattern == b.pattern) dup = true;
                if (dup) continue;
                const auto& p = S.patterns[(size_t) juce::jlimit(0, kPatterns - 1, b.pattern)];
                region(b.pattern, o, (long) std::floor(b.bars * 4.0 / stepBeats(p.res) + 1e-9));
            }
        }
    } else region(P.pattern, 0.0, LONG_MAX);
}

// where playback is: which pattern and the beat its grid started at (false = silence in song mode)
inline bool locate(const Snapshot& S, const EngineParams& P, double beat, int& pat, double& origin) {
    if (beat < 0) return false;
    if (P.songMode && S.songCount > 0) {
        const long len = songLength(S), bar0 = (long) std::floor(beat / 4.0), pass = bar0 / len, bar = bar0 % len;   // (the song loops)
        for (int i = 0; i < S.songCount; ++i) { const auto& b = S.song[(size_t) i]; if (bar >= b.startBar && bar < b.startBar + b.bars) { pat = juce::jlimit(0, kPatterns - 1, b.pattern); origin = (b.startBar + pass * len) * 4.0; return true; } }
        return false;
    }
    pat = juce::jlimit(0, kPatterns - 1, P.pattern); origin = 0; return true;
}

// LINEAR DRUMMING (Linear.h) and the MELODIC TRACKER (MelodyTracker.h) include this header themselves, so the engine reaches
// them through a hook table that BeatEngineHooks.h binds in every translation unit that has both headers complete (the plugin,
// its tests). Unbound (e.g. the LINEAR / MELODY test harness, which only renders MIDI through the engine) = collectEvents, no melody.
namespace linear { struct Cache; } namespace mel { struct Player; }
struct EngineHooks {
    using Emit = void (*)(void* ctx, int track, double beat, float vel, const EvInfo& e);
    std::shared_ptr<void> (*makeCache)() = nullptr;
    void (*collect)(void* cache, const Snapshot& S, const EngineParams& P, double bpm, double b0, double b1, Emit emit, void* ctx) = nullptr;
    std::shared_ptr<void> (*makePlayer)(double sr) = nullptr;
    void (*playerProcess)(void* player, const Snapshot& S, const EngineParams& P, double b0, double spb, int n, float* const* lr, bool running, int* uiRow) = nullptr;
    void (*playerReset)(void* player) = nullptr;
    int (*playerTempo)(void* player) = nullptr;   // a tracker F20..FF tempo command that just played (0 = none); taken once
    void (*playerAudition)(void* player, int inst, int note, float vel) = nullptr;
    static EngineHooks& get() { static EngineHooks h; return h; }
};

struct PadEvent { int track = 0; float vel = 1; };
struct RecEvent { int pattern = 0, track = 0, step = 0; uint8_t vel = 100; };

// VELOCITY -> LEVEL. v1 projects keep the old 12 dB law; new ones get a curve with a -40 dB floor, scaled by the track's VELOCITY amount.
inline float velGain(float vel, float velAmt, int ver) {
    if (ver < 4) return 0.25f + 0.75f * vel;
    const float c = std::pow(juce::jmax(0.0f, vel), 1.5f);
    return juce::jmax(0.01f, (1.0f - velAmt) + velAmt * c);
}

// Windowed-sinc sample reader (Kaiser, 12 taps per side at unity rate; the kernel stretches with the rate so pitched-up and
// down-sampled playback is low-passed below the new Nyquist). Integer positions at rate 1 read the sample exactly.
struct Sinc {
    static constexpr int HALF = 12, PH = 64, N = HALF * PH + 2;
    static const float* table() {
        static const std::array<float, N> t = [] { std::array<float, N> a {}; auto bessel = [](double x) { double s = 1, term = 1; for (int k = 1; k < 40; ++k) { term *= (x / (2.0 * k)) * (x / (2.0 * k)); s += term; } return s; };
            const double beta = 9.0, den = bessel(beta);
            for (int i = 0; i < N; ++i) { const double x = (double) i / PH, u = juce::jmin(1.0, x / HALF); const double w = bessel(beta * std::sqrt(juce::jmax(0.0, 1.0 - u * u))) / den;
                a[(size_t) i] = (float) (i == 0 ? 1.0 : std::sin(juce::MathConstants<double>::pi * x) / (juce::MathConstants<double>::pi * x) * w); }
            a[N - 1] = 0; return a; }();
        return t.data();
    }
    static float read(const float* d, int len, double p, float stretch) {
        const float* tbl = table();
        const int c = (int) std::floor(p); const float fr = (float) (p - c);
        if (stretch <= 1.0001f && fr < 1e-7f) return c >= 0 && c < len ? d[c] : 0.0f;
        const int span = (int) std::ceil(HALF * stretch); const float scale = (float) PH / stretch;
        float acc = 0, wsum = 0;
        for (int k = -span + 1; k <= span; ++k) {
            const float ax = std::abs(((float) k - fr) * scale); if (ax >= (float) (HALF * PH)) continue;
            const int ti = (int) ax; const float tf = ax - (float) ti; const float w = tbl[ti] + (tbl[ti + 1] - tbl[ti]) * tf;
            const int idx = c + k; acc += w * (idx >= 0 && idx < len ? d[idx] : 0.0f); wsum += w;
        }
        return wsum > 1e-6f ? acc / wsum : 0.0f;
    }
};

// A small feedback-delay-network reverb (8 lines, Householder feedback, damping, pre-delay, slow modulation). Stereo in / out.
struct FdnReverb {
    static constexpr int kLines = 8;
    std::vector<float> line[kLines]; int wpos[kLines] {}; int lens[kLines] {}; float lp[kLines] {}; float fb[kLines] {};
    std::vector<float> preL, preR; int prePos = 0, preLen = 1; double sr = 48000, lfo = 0;
    void prepare(double rate) {
        sr = rate; static const double base[kLines] { 1123, 1361, 1559, 1823, 2029, 2311, 2591, 2887 };
        for (int i = 0; i < kLines; ++i) { lens[i] = (int) (base[i] * sr / 48000.0); line[i].assign((size_t) (lens[i] + 16), 0.0f); wpos[i] = 0; lp[i] = 0; }
        preL.assign((size_t) (sr * 0.06) + 8, 0.0f); preR = preL; prePos = 0; preLen = 1;
    }
    void clear() { for (auto& l : line) std::fill(l.begin(), l.end(), 0.0f); for (auto& v : lp) v = 0; std::fill(preL.begin(), preL.end(), 0.0f); std::fill(preR.begin(), preR.end(), 0.0f); }
    void set(float rt60, float damp, float preMs) {
        for (int i = 0; i < kLines; ++i) fb[i] = (float) std::pow(10.0, -3.0 * lens[i] / (juce::jmax(0.05f, rt60) * sr));
        damping = damp; preLen = juce::jlimit(1, (int) preL.size() - 2, (int) (preMs * 0.001 * sr));
    }
    float damping = 0.5f;
    void process(float* L, float* R, int n, float wet) {
        const int PN = (int) preL.size();
        for (int i = 0; i < n; ++i) {
            lfo += 0.31 / sr; if (lfo > 1) lfo -= 1;
            const int rp = (prePos - preLen + PN) % PN; const float inL = preL[(size_t) rp], inR = preR[(size_t) rp]; preL[(size_t) prePos] = L[i]; preR[(size_t) prePos] = R[i]; prePos = (prePos + 1) % PN;
            float out[kLines]; float sum = 0;
            for (int k = 0; k < kLines; ++k) {   // read with a slow +-2 sample wobble (breaks up metallic modes)
                const double m = 2.0 * std::sin(juce::MathConstants<double>::twoPi * (lfo + k * 0.125)); const int LN = (int) line[k].size();
                double rpos = wpos[k] - lens[k] + m; while (rpos < 0) rpos += LN; const int a = (int) rpos; const float t = (float) (rpos - a);
                const float y = line[k][(size_t) a] + (line[k][(size_t) ((a + 1) % LN)] - line[k][(size_t) a]) * t; out[k] = y; sum += y;
            }
            const float hh = sum * (2.0f / kLines); float oL = 0, oR = 0;
            for (int k = 0; k < kLines; ++k) {
                float v = out[k] - hh;                                   // Householder reflection
                lp[k] += (v - lp[k]) * (1.0f - damping * 0.85f); v = lp[k] * fb[k];
                const float in = (k & 1) ? inR : inL;
                line[k][(size_t) wpos[k]] = v + in * ((k & 2) ? -0.25f : 0.25f); wpos[k] = (wpos[k] + 1) % (int) line[k].size();
                if (k & 1) oR += out[k]; else oL += out[k];
            }
            L[i] += oL * wet; R[i] += oR * wet;
        }
    }
};

// A look-ahead peak limiter: 1.5 ms look-ahead, instant gain computer on the window maximum, 60 ms release, hard safety clip at the ceiling.
struct Limiter {
    std::vector<float> dL, dR; int pos = 0, la = 64; float env = 1; double sr = 48000;
    void prepare(double rate) { sr = rate; la = juce::jmax(8, (int) (0.0015 * sr)); dL.assign((size_t) la + 1, 0.0f); dR = dL; pos = 0; env = 1; }
    void clear() { std::fill(dL.begin(), dL.end(), 0.0f); std::fill(dR.begin(), dR.end(), 0.0f); env = 1; }
    void process(float* L, float* R, int n, float top) {
        const int N = (int) dL.size(); const float att = 1.0f - std::exp(-1.0f / (0.3f * (float) la)), rel = 1.0f - std::exp(-1.0f / (0.06f * (float) sr));
        for (int i = 0; i < n; ++i) {
            dL[(size_t) pos] = L[i]; dR[(size_t) pos] = R[i];
            float pk = 0; for (int k = 0; k < N; ++k) pk = juce::jmax(pk, std::abs(dL[(size_t) k]), std::abs(dR[(size_t) k]));   // the window = everything not yet output
            const float g = pk > top ? top / pk : 1.0f;
            env += (g - env) * (g < env ? att : rel);
            const int rp = (pos + 1) % N; L[i] = juce::jlimit(-top, top, dL[(size_t) rp] * env); R[i] = juce::jlimit(-top, top, dR[(size_t) rp] * env); pos = rp;
        }
    }
};

class BeatEngine {
public:
    static constexpr int kVoices = 128, kPreview = kTracks;   // track slot kTracks = the sound browser's preview
    static constexpr int kMaxHits = 4096;

    void prepare(double rate, int maxBlock) {
        sr = rate; fadeDef = juce::jmax(32, (int) (0.0046 * sr)); hits.assign((size_t) kMaxHits, Hit()); nHits = 0;
        trackBuf.setSize((kTracks + 1) * 2, juce::jmax(64, maxBlock)); melBuf.setSize(kTracks * 2, juce::jmax(64, maxBlock)); scBuf.assign((size_t) juce::jmax(64, maxBlock), 0.0f);
        for (auto& v : voices) v = Voice();
        juce::dsp::ProcessSpec spec { rate, (juce::uint32) juce::jmax(64, maxBlock), 2 };
        reverb.prepare(spec); roomVerb.prepare(spec); fdn.prepare(rate); fdnRoom.prepare(rate); limiter.prepare(rate);
        echoL.assign((size_t) (rate * 2.5), 0.0f); echoR = echoL; ePos = 0;
        ring.assign((size_t) (rate * 4.0), 0.0f); ring2 = ring; rPos = 0;
        stutBuf.assign((size_t) (rate * 3.2), 0.0f); stutBuf2 = stutBuf; stut = Stut();
        for (auto& f : trackFilt) f = SVF();
        for (auto& f : fx) f = TrackFx();
        for (auto& c : charFx) c.prepare(rate, juce::jmax(64, maxBlock));
        { auto& H = EngineHooks::get(); linCache = H.makeCache ? H.makeCache() : nullptr; melPlayer = H.makePlayer ? H.makePlayer(rate) : nullptr; uiMelRow.store(-1); }
        clock = 0; baseBeat = 0; sinceBase = 0; baseSpb = 0; lastStepUi = -1; scEnv = scHold = 0;
        for (auto& m : meter) m = 0; for (auto& a : trackPeak) a.store(0.0f); for (auto& a : masterPeak) a.store(0.0f);
        Sinc::table();
    }
    void setSnapshot(const Snapshot* s) { snap.store(s); }
    const Snapshot* snapshotInUse() const { return seen.load(); }
    // BYPASS engaged: stop everything that sounds or would sound (voices, queued hits, the melody player, every tail / delay / filter state).
    // No allocation; the caller fades the block out, so the cut itself never clicks.
    void panic() {
        for (auto& v : voices) v = Voice(); nHits = 0; heldPads.store(0);
        if (melPlayer && EngineHooks::get().playerReset) EngineHooks::get().playerReset(melPlayer.get());
        std::fill(echoL.begin(), echoL.end(), 0.0f); std::fill(echoR.begin(), echoR.end(), 0.0f); eLp[0] = eLp[1] = 0;
        std::fill(ring.begin(), ring.end(), 0.0f); std::fill(ring2.begin(), ring2.end(), 0.0f); tapeWas = false;
        stut = Stut(); glueEnv = 0; crushHold[0] = crushHold[1] = 0; scEnv = scHold = 0;
        reverb.reset(); roomVerb.reset(); fdn.clear(); fdnRoom.clear(); limiter.clear();
        for (auto& f : trackFilt) f = SVF(); djFilt = SVF(); for (auto& f : fx) f = TrackFx(); for (auto& c : charFx) c.reset();
        for (auto& m : meter) m = 0; for (auto& a : trackPeak) a.store(0.0f); for (auto& a : masterPeak) a.store(0.0f);
        trackBuf.clear(); melBuf.clear();
    }
    // BYPASS silent: nothing renders, but the internal clock and the UI position keep moving so un-bypass lands in time
    void advanceSilently(int n, const EngineParams& P, double hostBpm, double hostPpq, bool hostPlaying) {
        const double bpm = hostPlaying && hostBpm > 0 ? hostBpm : P.bpm; const double spb = sr * 60.0 / juce::jmax(20.0, bpm);
        if (hostPlaying) { clock = hostPpq; baseBeat = hostPpq + n / spb; sinceBase = 0; baseSpb = spb; }
        else if (std::abs(spb - baseSpb) > 1e-9) { baseBeat = clock; sinceBase = 0; baseSpb = spb; }
        const bool running = hostPlaying || internalPlay; const Snapshot* S = snap.load(); seen.store(S);
        if (running) { const double b0 = hostPlaying ? hostPpq : baseBeat + (double) sinceBase / spb; int pat = 0; double org = 0;
            if (S && locate(*S, P, b0, pat, org)) { const auto& pp = S->patterns[(size_t) pat]; uiStep.store((int) ((long) std::floor((b0 - org) / stepBeats(pp.res) + 1e-9) % pp.length)); uiPattern.store(pat); uiOrigin.store(org); uiBeat.store(b0); }
            uiBar.store((int) std::floor(b0 / 4.0)); if (!hostPlaying) { sinceBase += n; clock = baseBeat + (double) sinceBase / spb; } }
        else { uiStep.store(-1); uiBeat.store(-1.0); }
        uiMelRow.store(-1); int a, b, c, d; const int k = padFifo.getNumReady(); padFifo.prepareToRead(k, a, b, c, d); padFifo.finishedRead(b + d);   // pads tapped while bypassed are dropped
    }
    int activeVoices() const { int k = 0; for (const auto& v : voices) if (v.data) ++k; return k; }   // (tests / diagnostics)
    int lookahead() const { return limiter.la; }                     // the CLEAN limiter's delay in samples (reported to the host as latency; renders trim it)
    static int latencyFor(const EngineParams& P, int la) { return P.limiter == 0 && !P.bypassMaster ? la : 0; }   // (stems bypass the bus, so they carry no look-ahead delay)

    // ui feedback
    std::atomic<int> uiStep { -1 }, uiPattern { 0 }, uiBar { 0 };
    std::atomic<double> uiBeat { -1.0 }, uiOrigin { 0.0 };
    std::atomic<uint32_t> trig[kTracks] {};
    std::atomic<uint32_t> heldPads { 0 };
    std::atomic<float> trackPeak[kTracks] {}, masterPeak[2] {};   // TRACK LEVELS: peak meters (after the track chain / after the limiter), ~0.35 s decay, 0 when muted
    std::atomic<int> uiMelRow { -1 }, uiMelPat { 0 };             // the melodic tracker row / pattern under the playhead (-1 = not playing)
    // MELODY: audition a note on an instrument (message thread; lock-free, never recorded) + MIDI keyboard into the tracker
    void melodyAudition(int inst, int note, float vel = 0.85f) { if (melPlayer && EngineHooks::get().playerAudition) EngineHooks::get().playerAudition(melPlayer.get(), inst, note, vel); }
    std::atomic<bool> melodyMidi { false }; std::atomic<int> melodyInst { 0 };   // tracker MIDI REC armed: MIDI notes play / record the melody instead of the drum pads
    struct MelNote { int note = 60; uint8_t vel = 100; double beat = -1; bool on = true; };
    juce::AbstractFifo melFifo { 256 }; MelNote melNotes[256];
    bool internalPlay = false;
    std::atomic<int> tempoReq { 0 };   // the last tracker tempo command (F20..FF) played; the processor applies it in SYNC: OWN
    bool patternGate = true;   // audio thread: false = the pattern / song / melody do not run (PLAY: ON NOTES with no note held); pads + preview still play
    bool legacyTiming = false;   // test only: the old engine's clock accumulation + truncation (to prove everything else is bit-identical)
    std::atomic<const Sound*> previewReq { nullptr };   // the browser asks to hear a sound (the library keeps it alive)
    std::atomic<bool> previewStop { false };

    // pad events in (UI -> audio), recorded steps out (audio -> UI)
    juce::AbstractFifo padFifo { 256 }; PadEvent padBuf[256];
    juce::AbstractFifo recFifo { 512 }; RecEvent recBuf[512];
    void pushPad(int track, float vel) { int a, b, c, d; padFifo.prepareToWrite(1, a, b, c, d); if (b > 0) { padBuf[a] = { track, vel }; padFifo.finishedWrite(1); } }

    // aux: optional per-track outputs (2 * kTracks channel pointers, null = not wanted): the track after its chain, before the bus
    void process(juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, const EngineParams& P, double hostBpm, double hostPpq, bool hostPlaying, float* const* aux = nullptr) {
        const int n = out.getNumSamples();
        out.clear();
        if (n > trackBuf.getNumSamples()) { trackBuf.setSize((kTracks + 1) * 2, n, false, false, true); melBuf.setSize(kTracks * 2, n, false, false, true); scBuf.resize((size_t) n); }   // host broke its promise
        const Snapshot* S = snap.load(); seen.store(S);
        const double bpm = hostPlaying && hostBpm > 0 ? hostBpm : P.bpm;   // a host that reports no tempo gets the plugin's own
        const double spb = sr * 60.0 / juce::jmax(20.0, bpm);
        const bool clockRuns = hostPlaying || internalPlay, running = clockRuns && patternGate;   // (PLAY: ON NOTES closes patternGate between notes)
        // internal clock: beats = base + samples / spb, so where a hit lands never depends on how the host slices buffers
        if (hostPlaying) { clock = hostPpq; baseBeat = hostPpq + n / spb; sinceBase = 0; baseSpb = spb; }
        else if (std::abs(spb - baseSpb) > 1e-9 || legacyTiming) { baseBeat = clock; sinceBase = 0; baseSpb = spb; }
        double b0 = hostPlaying ? hostPpq : baseBeat + (double) sinceBase / spb;
        const int ver = S ? S->ver : kStateVersion;

        nHits = 0;
        // live pads (UI) and MIDI notes (36..51 = tracks 1..16, the usual 4x4 pad layout): play now, and record into the pattern while running
        auto record = [&](int track, float vel, int sampleOffset) {
            if (!(P.recording && running && S)) return;
            int pat = 0; double org = 0; const double at = b0 + sampleOffset / spb;
            if (locate(*S, P, at, pat, org)) {
                const auto& pp = S->patterns[(size_t) pat]; const long st = (long) std::llround((at - org) / stepBeats(pp.res));
                int w1, w2, w3, w4; recFifo.prepareToWrite(1, w1, w2, w3, w4);
                if (w2 > 0) { recBuf[w1] = { pat, track, (int) (st % pp.lenOf(track)), (uint8_t) juce::jlimit(1, 127, (int) (vel * 127)) }; recFifo.finishedWrite(1); }
            }
        };
        const bool melMidi = melodyMidi.load(std::memory_order_relaxed);
        for (const auto md : midi) { const auto m = md.getMessage();
            if (melMidi) {   // MELODY MIDI REC: the keyboard plays the selected instrument and writes into the tracker (the drum pads stay quiet)
                if (!(m.isNoteOn() || m.isNoteOff())) continue;
                melodyAudition(melodyInst.load(), m.isNoteOn() ? m.getNoteNumber() : -1, m.getFloatVelocity());
                int w1, w2, w3, w4; melFifo.prepareToWrite(1, w1, w2, w3, w4);
                if (w2 > 0) { melNotes[w1] = { m.getNoteNumber(), (uint8_t) juce::jlimit(1, 127, (int) (m.getFloatVelocity() * 127)), (hostPlaying || internalPlay) ? b0 + md.samplePosition / spb : -1.0, m.isNoteOn() }; melFifo.finishedWrite(1); }
                continue;
            }
            if (m.isNoteOn()) { const int t = m.getNoteNumber() - 36; if (juce::isPositiveAndBelow(t, kTracks)) { addHit(t, m.getFloatVelocity(), md.samplePosition); record(t, m.getFloatVelocity(), md.samplePosition); } } }
        { int a, b, c, d; const int k = padFifo.getNumReady(); padFifo.prepareToRead(k, a, b, c, d);
          auto take = [&](int idx) { const auto& e = padBuf[idx]; addHit(e.track, e.vel, 0); record(e.track, e.vel, 0); };
          for (int i = 0; i < b; ++i) take(a + i); for (int i = 0; i < d; ++i) take(c + i); padFifo.finishedRead(b + d); }

        const double b1 = b0 + n / spb;
        if (running && S) {
            auto emitHit = [&](int t, double tb, float vel, const EvInfo& e) { addHit(t, vel, legacyTiming ? (int) ((tb - b0) * spb) : juce::jlimit(0, n - 1, (int) std::floor((tb - b0) * spb + 1e-6)), e, (int) (e.gate * spb)); };
            auto& H = EngineHooks::get();
            if (H.collect && linCache) H.collect(linCache.get(), *S, P, bpm, b0, b1, [](void* c, int t, double tb, float v, const EvInfo& e) { (*static_cast<decltype(emitHit)*>(c))(t, tb, v, e); }, &emitHit);   // LINEAR DRUMMING
            else collectEvents(*S, P, bpm, b0, b1, emitHit);
            // note repeat on held pads
            if (P.padRepeat && heldPads.load()) {
                static const double D[] = { 0.5, 0.25, 0.125, 1.0 / 6.0 };   // 1/8, 1/16, 1/32, 1/16T
                const double q = D[juce::jlimit(0, 3, P.repeatDiv)];
                for (long k = (long) std::ceil(b0 / q); k * q < b1; ++k) {
                    const int off = juce::jlimit(0, n - 1, (int) std::floor((k * q - b0) * spb + 1e-6)); const uint32_t mask = heldPads.load();
                    for (int t = 0; t < kTracks; ++t) if (mask & (1u << t)) addHit(t, 0.85f, off);
                }
            }
            int pat = 0; double org = 0;
            if (locate(*S, P, b0, pat, org)) { const auto& pp = S->patterns[(size_t) pat]; uiStep.store((int) ((long) std::floor((b0 - org) / stepBeats(pp.res) + 1e-9) % pp.length)); uiPattern.store(pat); uiOrigin.store(org); uiBeat.store(b0); }
            else { uiStep.store(-1); uiBeat.store(-1.0); }
            uiBar.store((int) std::floor(b0 / 4.0));
            if (!hostPlaying) { sinceBase += n; clock = baseBeat + (double) sinceBase / spb; }
        } else { uiStep.store(-1); uiBeat.store(-1.0); if (clockRuns && !hostPlaying) { sinceBase += n; clock = baseBeat + (double) sinceBase / spb; } }   // gated: the internal clock keeps time

        // the sound browser's preview
        if (previewStop.exchange(false)) for (auto& v : voices) if (v.data && v.track == kPreview) v.fade = juce::jmin(v.fade < 0 ? fadeDef : v.fade, fadeDef);
        if (auto* ps = previewReq.exchange(nullptr)) if (!ps->data.empty()) {
            for (auto& v : voices) if (v.data && v.track == kPreview && v.fade < 0) { v.fade = fadeDef; v.fadeLen = fadeDef; }
            Voice& v = alloc(); v = Voice(); v.data = ps->data.data(); v.dataR = ps->stereo() ? ps->right.data() : nullptr; v.len = (int) ps->data.size(); v.srcRatio = ps->rate / sr; v.rate = v.srcRatio; v.stretch = (float) juce::jmax(1.0, v.rate); v.track = kPreview; v.gain = 0.8f * ps->trim; v.age = ++ageCounter; v.fadeLen = fadeDef;
        }

        // sort the few hits by time, then render sample by sample
        for (int i = 1; i < nHits; ++i) { const Hit x = hits[(size_t) i]; int j = i - 1; while (j >= 0 && hits[(size_t) j].offset > x.offset) { hits[(size_t) (j + 1)] = hits[(size_t) j]; --j; } hits[(size_t) (j + 1)] = x; }   // stable, no allocation
        trackBuf.clear(0, n);
        const int scT = S ? S->scTrack : -1;
        const float scAtt = 1.0f - std::exp(-1.0f / (0.0015f * (float) sr)), scRel = std::exp(-1.0f / (0.11f * (float) sr));
        int h = 0;
        for (int i = 0; i < n; ++i) {
            while (h < nHits && hits[(size_t) h].offset <= i) { if (hits[(size_t) h].track == scT) scHold = 1.0f; startVoice(S, P, hits[(size_t) h], ver); ++h; }
            scEnv += (scHold - scEnv) * scAtt; scHold *= scRel; scBuf[(size_t) i] = scEnv;
            for (auto& v : voices) {
                if (!v.data) continue;
                if (v.wait > 0) { --v.wait; continue; }
                if (v.gate >= 0 && --v.gate < 0 && v.fade < 0) v.fade = v.fadeLen;   // tracker CUT / freeze hold end
                if (v.loopB > v.loopA) { if (v.pos >= v.loopB) v.pos -= v.loopB - v.loopA; if (--v.loopLeft <= 0 && v.fade < 0) { v.fade = v.fadeLen; } }   // SAMPLE REGION loop (8 s safety stop)
                if (v.pos >= v.len - 1) { v.data = nullptr; continue; }
                const double rp = v.reverse ? v.len - 1 - v.pos : v.pos;
                float x = Sinc::read(v.data, v.len, rp, v.stretch), xr = v.dataR ? Sinc::read(v.dataR, v.len, rp, v.stretch) : x;
                if (v.loopXf > 1.0 && v.pos > v.loopB - v.loopXf) {   // REGION loop wrap: fade into the loop start's run-up (equal power, so no click and no dip)
                    const double u = (v.pos - (v.loopB - v.loopXf)) / v.loopXf, p2 = v.pos - (v.loopB - v.loopA), rp2 = v.reverse ? v.len - 1 - p2 : p2;
                    const float gOut = (float) std::cos(u * juce::MathConstants<double>::halfPi), gIn = (float) std::sin(u * juce::MathConstants<double>::halfPi);
                    const float x2 = Sinc::read(v.data, v.len, rp2, v.stretch), xr2 = v.dataR ? Sinc::read(v.dataR, v.len, rp2, v.stretch) : x2;
                    x = x * gOut + x2 * gIn; xr = xr * gOut + xr2 * gIn;
                }
                if (v.crush > 1) { if (v.crushN++ % v.crush == 0) { v.hold = x; v.holdR = xr; } x = v.hold; xr = v.holdR; }   // DESTRUCTION: rate reduce
                if (v.fType) { x = v.filt(0, x); xr = v.dataR ? v.filt(1, xr) : x; }
                // pitch: glide + pitch envelope move the rate in the semitone domain (updated every 16 samples)
                if ((v.penvCur != 0.0f || v.semis != v.semisTarget) && (++v.pitchTick & 15) == 0) {
                    v.penvCur *= v.penvK; if (std::abs(v.penvCur) < 0.005f) v.penvCur = 0;
                    if (v.semis != v.semisTarget) { v.semis += v.glideStep; if ((v.glideStep >= 0 && v.semis >= v.semisTarget) || (v.glideStep < 0 && v.semis <= v.semisTarget)) v.semis = v.semisTarget; }   // GLIDE: straight line in semitones, arrives in the glide time
                    v.rate = v.srcRatio * std::exp2((v.semis + v.penvCur) / 12.0); v.stretch = (float) juce::jmax(1.0, v.rate);
                }
                v.pos += v.rate;
                v.env *= v.envK;
                float g = v.env * v.gain;
                if (v.attG < 1.0f) { v.attG = juce::jmin(1.0f, v.attG + v.attInc); g *= v.attG; }
                if (v.endFade > 0) { const double rem = v.len - 1 - v.pos; if (rem < v.endFade) g *= (float) juce::jmax(0.0, rem) / (float) v.endFade; }
                if (v.fade >= 0) { g *= juce::jmin(1.0f, v.fade / (float) v.fadeLen); if (--v.fade < 0) { v.data = nullptr; continue; } }
                trackBuf.addSample(v.track * 2, i, x * g * v.panL);
                trackBuf.addSample(v.track * 2 + 1, i, xr * g * v.panR);
            }
        }
        // MELODIC TRACKER: its channels play into their route tracks (then the track chain, mute / solo, meters, aux outs apply)
        if (S && S->melody && melPlayer && EngineHooks::get().playerProcess) {   // into its own buffer, then onto the route tracks at their VOLUME (fader + automation)
            melBuf.clear(0, n); float* lr[kTracks * 2]; for (int c = 0; c < kTracks * 2; ++c) lr[c] = melBuf.getWritePointer(c);
            int row = -1; EngineHooks::get().playerProcess(melPlayer.get(), *S, P, b0, spb, n, lr, running, &row); uiMelRow.store(row < 0 ? -1 : (row & 255)); if (row >= 0) uiMelPat.store(row >> 8);
            if (EngineHooks::get().playerTempo) if (const int bt = EngineHooks::get().playerTempo(melPlayer.get()); bt > 0) tempoReq.store(bt);
            for (int t = 0; t < kTracks; ++t) { const float g = S->tracks[(size_t) t].vol * juce::Decibels::decibelsToGain(P.tVol[t]); if (g <= 0) continue;
                trackBuf.addFrom(t * 2, 0, melBuf, t * 2, 0, n, g); trackBuf.addFrom(t * 2 + 1, 0, melBuf, t * 2 + 1, 0, n, g); }
        } else uiMelRow.store(-1);
        // per-track tone (one knob: low-pass <- 0.5 -> high-pass) + drive, then the producer chain, then mix
        float* L = out.getWritePointer(0); float* R = out.getWritePointer(1);
        for (int t = 0; t < kTracks; ++t) {
            if (!S) break;
            const auto& tr = S->tracks[(size_t) t];
            const bool audible = !tr.mute && (!S->anySolo || tr.solo);
            float* tl = trackBuf.getWritePointer(t * 2); float* trr = trackBuf.getWritePointer(t * 2 + 1);
            if (tr.character > 0) {   // CHARACTER: the track as played by an old sampler (off = this block is never touched: bit-exact)
                auto& c = charFx[t]; c.setMode(tr.character); c.setAmount(juce::jlimit(0.0f, 1.0f, tr.charAmount + P.tChar[t])); c.setDrive(tr.charDrive);
                float* io[2] { tl, trr }; c.process(io, 2, n);
            }
            const float tone = juce::jlimit(0.0f, 1.0f, tr.tone + P.tTone[t]); auto& F = trackFilt[t];
            const bool filt = std::abs(tone - 0.5f) > 0.02f;
            if (filt) F.setup(tone < 0.5f ? 150.0f * std::pow(130.0f, tone * 2.0f) : 30.0f * std::pow(130.0f, (tone - 0.5f) * 2.0f), 0.25f, (float) sr);
            const float dg = 1.0f + tr.drive * tr.drive * 20.0f, dc = 1.0f / (1.0f + tr.drive * 1.6f);
            for (int i = 0; i < n; ++i) {
                float a = tl[i], b = trr[i];
                if (filt) { a = F.tick(0, a, tone < 0.5f ? 0 : 2); b = F.tick(1, b, tone < 0.5f ? 0 : 2); }
                if (tr.drive > 0.01f) { a = std::tanh(a * dg) * dc; b = std::tanh(b * dg) * dc; }
                tl[i] = a; trr[i] = b;
            }
            producerChain(t, tr, juce::jlimit(0.0f, 1.0f, tr.duck + P.tDuck[t]), tl, trr, n);
            float pk = 0;
            if (audible) for (int i = 0; i < n; ++i) { L[i] += tl[i]; R[i] += trr[i]; pk = juce::jmax(pk, std::abs(tl[i]), std::abs(trr[i])); }
            if (aux && aux[t * 2] && aux[t * 2 + 1]) { if (audible) { std::memcpy(aux[t * 2], tl, (size_t) n * sizeof(float)); std::memcpy(aux[t * 2 + 1], trr, (size_t) n * sizeof(float)); } else { std::memset(aux[t * 2], 0, (size_t) n * sizeof(float)); std::memset(aux[t * 2 + 1], 0, (size_t) n * sizeof(float)); } }
            meter[t] = juce::jmax(pk, meter[t] * meterDecay(n)); if (meter[t] < 1e-4f) meter[t] = 0; trackPeak[t].store(meter[t]);
        }
        { const float* pl = trackBuf.getReadPointer(kPreview * 2); const float* pr = trackBuf.getReadPointer(kPreview * 2 + 1);
          for (int i = 0; i < n; ++i) { L[i] += pl[i]; R[i] += pr[i]; } }
        if (P.bypassMaster) { const float g = P.volume * 1.25f; for (int i = 0; i < n; ++i) { L[i] *= g; R[i] *= g; } }   // stems: the dry mix level, nothing else
        else { masterBeat0 = b0; masterRunning = running; master(out, P, spb, ver); }
        { float pl = 0, pr = 0; for (int i = 0; i < n; ++i) { pl = juce::jmax(pl, std::abs(L[i])); pr = juce::jmax(pr, std::abs(R[i])); }
          meter[kTracks] = juce::jmax(pl, meter[kTracks] * meterDecay(n)); meter[kTracks + 1] = juce::jmax(pr, meter[kTracks + 1] * meterDecay(n));
          for (int c = 0; c < 2; ++c) { if (meter[kTracks + c] < 1e-4f) meter[kTracks + c] = 0; masterPeak[c].store(meter[kTracks + c]); } }
    }

private:
    struct Voice {
        const float* data = nullptr; const float* dataR = nullptr; int len = 0; double pos = 0, rate = 1, srcRatio = 1; float stretch = 1;
        float gain = 1, env = 1, envK = 1, panL = 0.7f, panR = 0.7f; int track = 0, fade = -1, fadeLen = 220, wait = 0, gate = -1, crush = 1, crushN = 0; float hold = 0, holdR = 0; bool reverse = false, main = false; uint32_t age = 0;
        float attG = 1, attInc = 1, penvCur = 0, penvK = 1, semis = 0, semisTarget = 0, layerSemis = 0, glideStep = 0; int pitchTick = 0, endFade = 0;
        double loopA = 0, loopB = 0; int loopLeft = 0;   // SAMPLE REGION loop (positions in playback order; loopB > loopA = looping; loopLeft = samples until the 8 s safety stop)
        double loopXf = 0;                               // equal-power crossfade length at the wrap (positions)
        // per-step filter (12 dB state-variable, one per channel)
        int fType = 0; float fa1 = 0, fa2 = 0, fa3 = 0, fk = 1, s1[2] {}, s2[2] {};
        float filt(int c, float x) { const float v3 = x - s2[c], v1 = fa1 * s1[c] + fa2 * v3, v2 = s2[c] + fa2 * s1[c] + fa3 * v3; s1[c] = 2 * v1 - s1[c]; s2[c] = 2 * v2 - s2[c]; return fType == 1 ? v2 : x - fk * v1 - v2; }
    };
    struct Hit { int track = 0; float vel = 1; int offset = 0; EvInfo e; int gate = 0; };
    struct SVF {
        float s1[2] {}, s2[2] {}, a1 = 0, a2 = 0, a3 = 0, k = 1;
        void setup(float f, float q, float rate) { const float g = std::tan(juce::MathConstants<float>::pi * juce::jlimit(20.0f, rate * 0.45f, f) / rate); k = 2.0f - 1.9f * q; a1 = 1 / (1 + g * (g + k)); a2 = g * a1; a3 = g * a2; }
        float tick(int c, float x, int type) { const float v3 = x - s2[c], v1 = a1 * s1[c] + a2 * v3, v2 = s2[c] + a2 * s1[c] + a3 * v3; s1[c] = 2 * v1 - s1[c]; s2[c] = 2 * v2 - s2[c]; return type == 0 ? v2 : (type == 1 ? v1 : x - k * v1 - v2); }
    };
    struct TrackFx { SVF lc, hc; float tilt[2] {}, ef = 0, es = 0, sf = 0, ss = 0, cenv = 0; };

    double sr = 44100, clock = 0, baseBeat = 0, baseSpb = 0; juce::int64 sinceBase = 0; int fadeDef = 220;   // ~4.6 ms fade when a voice is cut (steal / choke), scaled with the rate
    std::atomic<const Snapshot*> snap { nullptr }, seen { nullptr };
    Voice voices[kVoices]; uint32_t ageCounter = 0;
    std::vector<Hit> hits; int nHits = 0;   // sized in prepare() (kMaxHits), never resized in process()
    juce::AudioBuffer<float> trackBuf, melBuf; std::vector<float> scBuf; float scEnv = 0, scHold = 0;
    SVF trackFilt[kTracks], djFilt; TrackFx fx[kTracks]; vc::Character charFx[kTracks];
    std::shared_ptr<void> linCache, melPlayer;   // linear::Cache / mel::Player (made by the hooks in prepare; type-erased so this header needs neither type)
    float djCut = 0.5f, glueEnv = 0, crushHold[2] {}; int crushCount = 0;
    juce::dsp::Reverb reverb, roomVerb; FdnReverb fdn, fdnRoom; Limiter limiter;
    std::vector<float> echoL, echoR; int ePos = 0; float eLp[2] {};
    std::vector<float> ring, ring2; int rPos = 0;
    std::vector<float> stutBuf, stutBuf2;   // STUTTER: everything after the press (up to one beat) is kept here and replayed
    struct Stut { bool on = false; int div = 0, pending = 0; double switchAt = 0; int pos = 0, rec = 0; int xfPos = -1, xfDiv = 0, xf = 0; int outFade = 0; } stut;
    double masterBeat0 = 0; bool masterRunning = false;
public:
    std::atomic<int> uiStutter { 0 };   // (UI / tests) the division the stutter plays now (0 = off)
    std::atomic<long> stutterSwitches { 0 }; std::atomic<double> stutterSwitchBeat { -1.0 };
private:
    double tapePos = 0; float tapeRate = 1; bool tapeWas = false;
    int lastStepUi = -1;
    float meter[kTracks + 2] {};
    float meterDecay(int n) const { return std::exp(-(float) n / (0.35f * (float) sr)); }

    void addHit(int track, float vel, int offset, const EvInfo& e = {}, int gate = 0) { if (nHits < (int) hits.size()) hits[(size_t) nHits++] = { track, vel, juce::jmax(0, offset), e, gate }; }

    Voice& alloc() {
        Voice* pick = nullptr; uint32_t oldest = UINT32_MAX;
        for (auto& v : voices) { if (!v.data) { pick = &v; break; } if (v.age < oldest) { oldest = v.age; pick = &v; } }
        if (pick->data) pick->fade = juce::jmin(pick->fade < 0 ? fadeDef : pick->fade, 32);   // steal with a quick duck
        return *pick;
    }

    void startVoice(const Snapshot* S, const EngineParams& P, const Hit& h, int ver) {
        if (!S) return;
        const auto* snd = S->sound[(size_t) h.track]; if (!snd || snd->data.empty()) return;
        const auto& tr = S->tracks[(size_t) h.track]; const int t = h.track;
        trig[t].fetch_add(1);
        if (tr.choke > 0) for (auto& v : voices) if (v.data && v.track != t && v.track < kTracks && S->tracks[(size_t) v.track].choke == tr.choke && v.fade < 0) { v.fade = fadeDef; v.fadeLen = fadeDef; }
        const float pan = juce::jlimit(-1.0f, 1.0f, tr.pan + P.tPan[t] + h.e.pan / 50.0f);
        const float panL = std::cos((pan + 1) * juce::MathConstants<float>::pi * 0.25f), panR = std::sin((pan + 1) * juce::MathConstants<float>::pi * 0.25f);
        const float decay = juce::jlimit(0.05f, 1.0f, tr.decay * P.tDecay[t]) * h.e.decay;
        const float trackPitch = tr.pitch + P.tPitch[t] + h.e.pitch;
        const float g = tr.vol * juce::Decibels::decibelsToGain(P.tVol[t]) * velGain(h.vel, tr.velAmt, ver);
        // STEP SLIDE: this hit glides the note still ringing on this track (no retrigger: no new click, envelope or pitch envelope); its HOLD
        // becomes the note's new length. Nothing ringing = an ordinary hit.
        if (h.e.slideMs > 0.5f && h.e.variant == 0 && h.e.slice == 0) {
            bool any = false; const float ticks = juce::jmax(1.0f, h.e.slideMs * 0.001f * (float) sr / 16.0f);
            for (auto& v : voices) if (v.data && v.track == t && v.fade < 0 && v.env * v.gain > 0.02f) { v.semisTarget = v.layerSemis + trackPitch; v.glideStep = (v.semisTarget - v.semis) / ticks; v.gate = h.gate > 0 ? h.gate : -1; any = true; }
            if (any) return;
        }
        // STEP MONO: the note still ringing on this track ends (a quick 12 ms fade) as this one starts: one 808 note at a time
        if (h.e.mono) { const int mf = juce::jmax(16, (int) (0.012 * sr)); for (auto& v : voices) if (v.data && v.track == t && v.fade < 0) { v.fade = mf; v.fadeLen = mf; } }
        // LEGATO / GLIDE: a hit while this track still rings slides the running voices to the new pitch instead of retriggering
        if (tr.glide > 0.5f && h.e.variant == 0 && h.e.slice == 0) {
            bool any = false; const float ticks = juce::jmax(1.0f, tr.glide * 0.001f * (float) sr / 16.0f);
            for (auto& v : voices) if (v.data && v.track == t && v.fade < 0 && v.env * v.gain > 0.02f) { v.semisTarget = v.layerSemis + trackPitch; v.glideStep = (v.semisTarget - v.semis) / ticks; any = true; }
            if (any) return;
        }
        auto play = [&](const Sound* sd, float gain, float layerSemis, int wait, bool main) {
            int a = 0, z = (int) sd->data.size(); const bool sliced = main && h.e.slice > 0 && h.e.variant != 1 && h.e.slice <= (int) tr.slices.size();
            if (sliced) { a = juce::jlimit(0, z - 1, tr.slices[(size_t) h.e.slice - 1]); if (h.e.slice < (int) tr.slices.size()) z = juce::jlimit(a + 1, z, tr.slices[(size_t) h.e.slice]); }
            const bool region = main && !sliced && !tr.regionIsDefault();   // SAMPLE REGION: START / END (+ LOOP) of the whole sound
            if (region) { const int n0 = z; a = juce::jlimit(0, n0 - 2, (int) (tr.regStart * n0)); z = juce::jlimit(a + 2, n0, (int) std::ceil(tr.regEnd * n0)); }
            if (main && h.e.offset > 0) a += (int) ((z - a) * h.e.offset);
            if (z - a < 2) return;
            if (region && tr.loopOn) for (auto& o : voices) if (o.data && o.track == t && o.main && o.loopB > o.loopA && o.fade < 0) { o.fade = fadeDef; o.fadeLen = fadeDef; }   // a looping hit ends at the track's next hit
            Voice& v = alloc();
            v.data = sd->data.data() + a; v.dataR = sd->stereo() ? sd->right.data() + a : nullptr; v.len = z - a; v.pos = 0; v.age = ++ageCounter; v.main = main;
            v.layerSemis = layerSemis; v.semis = v.semisTarget = layerSemis + trackPitch; v.glideStep = 0; v.pitchTick = 0;
            v.penvCur = tr.pitchEnv; v.penvK = tr.pitchEnv != 0 ? std::exp(-16.0f / (juce::jmax(1.0f, tr.pitchEnvTime) * 0.001f * (float) sr)) : 1.0f;
            v.srcRatio = sd->rate / sr * (main ? h.e.rateMul : 1.0f); v.rate = v.srcRatio * std::exp2((v.semis + v.penvCur) / 12.0); v.stretch = (float) juce::jmax(1.0, v.rate);
            v.reverse = tr.reverse != h.e.reverse; v.track = t; v.fade = -1; v.wait = wait; v.gate = h.gate > 0 ? h.gate : -1;
            v.crush = main ? juce::jmax(1, h.e.crush) : 1; v.crushN = 0; v.hold = v.holdR = 0;
            v.fadeLen = main && h.e.variant == 1 ? (int) (0.06 * sr) : fadeDef;   // a frozen tail fades out gently
            if (h.e.releaseMs > 0.0f) v.fadeLen = juce::jmax(fadeDef, (int) (h.e.releaseMs * 0.001f * (float) sr));   // a HOLD note releases smoothly (no click on a low sine)
            v.endFade = sliced || (region && tr.regEnd < 1.0f) ? (int) (0.002 * sr) : 0;   // (a cut-short END never clicks)
            v.loopA = v.loopB = 0; v.loopLeft = 0;
            if (region && tr.loopOn) { const int n0 = (int) sd->data.size(); const int la = juce::jlimit(a, z - 2, (int) (tr.loopStart * n0)), lb = juce::jlimit(la + 2, z, (int) std::ceil(tr.loopEnd * n0));
                                       if (lb - la >= 32) {
                                           // playback order: forwards = data order; reversed = from the end (pos 0 = the last sample), so the loop's ends swap
                                           if (!v.reverse) { v.loopA = la - a; v.loopB = lb - a; } else { v.loopA = z - lb; v.loopB = z - la; }
                                           // the wrap crossfades (equal power) over 10 ms, at most a quarter of the loop, and only with material before the loop to fade from
                                           v.loopXf = juce::jmin(0.010 * sd->rate, (v.loopB - v.loopA) * 0.25, v.loopA);
                                           v.loopLeft = (int) (8.0 * sr); v.endFade = 0; } }
            v.gain = gain * (ver >= 4 ? sd->trim : 1.0f); v.env = 1;   // v1 projects play the library exactly as they did
            v.attG = tr.attack > 0.05f ? 0.0f : 1.0f; v.attInc = tr.attack > 0.05f ? 1.0f / (tr.attack * 0.001f * (float) sr) : 1.0f;
            // per-step filter
            v.fType = 0; v.s1[0] = v.s1[1] = v.s2[0] = v.s2[1] = 0;
            if (main && h.e.filt && h.e.filt != 64) {
                const bool lp = h.e.filt < 64; const float u = lp ? h.e.filt / 63.0f : (h.e.filt - 64) / 63.0f;
                const float f = lp ? 150.0f * std::pow(80.0f, u) : 30.0f * std::pow(266.0f, u);
                const float gg = std::tan(juce::MathConstants<float>::pi * juce::jlimit(20.0f, (float) sr * 0.45f, f) / (float) sr); v.fk = 1.0f / 0.8f; v.fa1 = 1 / (1 + gg * (gg + v.fk)); v.fa2 = gg * v.fa1; v.fa3 = gg * v.fa2; v.fType = lp ? 1 : 2;
            }
            // decay < 1 shortens the sound with an exponential fade over its length
            const double lenSec = v.len / sd->rate / (v.rate * sr / sd->rate);
            v.envK = decay >= 0.99f ? 1.0f : (float) std::exp(-6.9 / juce::jmax(64.0, decay * lenSec * sr));
            v.panL = panL; v.panR = panR;
        };
        const Sound* mainSnd = snd;
        if (h.e.variant > 0 && h.e.variant <= 8) if (const auto* vs = S->variant[(size_t) t][(size_t) h.e.variant - 1]) if (!vs->data.empty()) mainSnd = vs;   // pre-rendered on a worker thread
        play(mainSnd, g, 0.0f, 0, true);
        for (int l = 0; l < kLayers; ++l) if (const auto* ls = S->layer[(size_t) t][(size_t) l]) if (!ls->data.empty()) {
            const auto& L = tr.layers[(size_t) l];
            play(ls, g * L.level, L.pitch, (int) (juce::jlimit(0.0f, 100.0f, L.offset) * 0.001 * sr), false);
        }
    }

    // EQ -> transient -> saturate -> punch comp -> sidechain duck (each stage bypassed at its neutral value)
    void producerChain(int t, const Track& tr, float duck, float* a, float* b, int n) {
        auto& F = fx[t]; const float fs = (float) sr;
        if (tr.lowCut > 0.01f) { F.lc.setup(20.0f * std::pow(40.0f, tr.lowCut), 0.37f, fs); for (int i = 0; i < n; ++i) { a[i] = F.lc.tick(0, a[i], 2); b[i] = F.lc.tick(1, b[i], 2); } }
        if (tr.highCut < 0.99f) { F.hc.setup(400.0f * std::pow(50.0f, tr.highCut), 0.37f, fs); for (int i = 0; i < n; ++i) { a[i] = F.hc.tick(0, a[i], 0); b[i] = F.hc.tick(1, b[i], 0); } }
        if (std::abs(tr.tilt) > 0.01f) {
            const float c = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * 900.0f / fs), gl = juce::Decibels::decibelsToGain(-6.0f * tr.tilt), gh = juce::Decibels::decibelsToGain(6.0f * tr.tilt);
            for (int i = 0; i < n; ++i) { F.tilt[0] += (a[i] - F.tilt[0]) * c; F.tilt[1] += (b[i] - F.tilt[1]) * c; a[i] = F.tilt[0] * gl + (a[i] - F.tilt[0]) * gh; b[i] = F.tilt[1] * gl + (b[i] - F.tilt[1]) * gh; }
        }
        if (std::abs(tr.tAttack) > 0.01f || std::abs(tr.tSustain) > 0.01f) {
            const float aF = 1.0f - std::exp(-1.0f / (0.0005f * fs)), aS = 1.0f - std::exp(-1.0f / (0.015f * fs)), rl = 1.0f - std::exp(-1.0f / (0.08f * fs)),
                        rF = 1.0f - std::exp(-1.0f / (0.03f * fs)), rS = 1.0f - std::exp(-1.0f / (0.3f * fs));
            for (int i = 0; i < n; ++i) {
                const float x = juce::jmax(std::abs(a[i]), std::abs(b[i]));
                F.ef += (x - F.ef) * (x > F.ef ? aF : rl); F.es += (x - F.es) * (x > F.es ? aS : rl);
                F.sf += (x - F.sf) * (x > F.sf ? aF : rF); F.ss += (x - F.ss) * (x > F.ss ? aF : rS);
                const float att = juce::jmax(0.0f, F.ef - F.es) / (F.ef + 1e-4f), sus = juce::jmax(0.0f, F.ss - F.sf) / (F.ss + 1e-4f);
                const float g = juce::jlimit(0.05f, 5.0f, 1.0f + tr.tAttack * 3.0f * att + tr.tSustain * 2.5f * sus);
                a[i] *= g; b[i] *= g;
            }
        }
        if (tr.sat > 0.01f) { const float g = 1.0f + tr.sat * 8.0f, c = 1.0f / std::pow(g, 0.85f); for (int i = 0; i < n; ++i) { a[i] = std::tanh(a[i] * g) * c; b[i] = std::tanh(b[i] * g) * c; } }
        if (tr.comp > 0.01f) {
            const float at = 1.0f - std::exp(-1.0f / (0.005f * fs)), rl = 1.0f - std::exp(-1.0f / (0.09f * fs));
            const float thr = juce::Decibels::decibelsToGain(-6.0f - tr.comp * 24.0f), mk = 1.0f + tr.comp * 0.9f;
            for (int i = 0; i < n; ++i) {
                const float x = juce::jmax(std::abs(a[i]), std::abs(b[i])); F.cenv += (x - F.cenv) * (x > F.cenv ? at : rl);
                const float g = (F.cenv > thr ? std::pow(thr / F.cenv, 0.75f) : 1.0f) * mk; a[i] *= g; b[i] *= g;
            }
        }
        if (duck > 0.01f) for (int i = 0; i < n; ++i) { const float g = 1.0f - duck * scBuf[(size_t) i]; a[i] *= g; b[i] *= g; }
    }

    void master(juce::AudioBuffer<float>& out, const EngineParams& P, double spb, int ver) {
        const int n = out.getNumSamples();
        float* L = out.getWritePointer(0); float* R = out.getWritePointer(1);
        const bool legacyVerb = ver < 4;   // v1 projects keep the reverb they were mixed with
        // GLUE: bus compressor (gentle, punchy)
        if (P.glue > 0.01f) {
            const float att = std::exp(-1.0f / (0.008f * (float) sr)), rel = std::exp(-1.0f / (0.12f * (float) sr));
            const float thr = juce::Decibels::decibelsToGain(-6.0f - P.glue * 18.0f), makeup = 1.0f + P.glue * 1.2f;
            for (int i = 0; i < n; ++i) {
                const float lvl = juce::jmax(std::abs(L[i]), std::abs(R[i]));
                glueEnv = lvl > glueEnv ? att * glueEnv + (1 - att) * lvl : rel * glueEnv + (1 - rel) * lvl;
                const float g = glueEnv > thr ? std::pow(thr / glueEnv, 0.75f) : 1.0f;
                L[i] *= g * makeup; R[i] *= g * makeup;
            }
        }
        // CLIPPER (bus): drive into a soft clipper, then a short ROOM
        if (P.clip > 0.01f) {
            const float g = 1.0f + P.clip * 4.0f, mk = 1.0f / std::pow(g, 0.8f);   // drive in, level back out (peaks shaved, loudness up a little)
            auto cl = [](float x) { x = juce::jlimit(-1.5f, 1.5f, x); return x - x * x * x * (4.0f / 27.0f); };
            for (int i = 0; i < n; ++i) { L[i] = cl(L[i] * g) * mk; R[i] = cl(R[i] * g) * mk; }
        }
        if (P.room > 0.01f) {
            if (legacyVerb) { juce::dsp::Reverb::Parameters rp; rp.roomSize = 0.28f; rp.damping = 0.6f; rp.wetLevel = P.room * 0.4f; rp.dryLevel = 1.0f; rp.width = 0.8f;
                              roomVerb.setParameters(rp); juce::dsp::AudioBlock<float> b(out); roomVerb.process(juce::dsp::ProcessContextReplacing<float>(b)); }
            else { fdnRoom.set(0.4f, 0.65f, 4.0f); fdnRoom.process(L, R, n, P.room * 0.22f); }
        }
        // DJ FILTER: low-pass <- 0.5 -> high-pass, smoothed
        for (int i = 0; i < n; ++i) {
            djCut += (P.filter - djCut) * 0.002f;
            if ((i & 15) == 0 && std::abs(djCut - 0.5f) > 0.01f)
                djFilt.setup(djCut < 0.5f ? 80.0f * std::pow(250.0f, djCut * 2.0f) : 20.0f * std::pow(250.0f, (djCut - 0.5f) * 2.0f), P.res, (float) sr);
            if (std::abs(djCut - 0.5f) > 0.01f) { const int ty = djCut < 0.5f ? 0 : 2; L[i] = djFilt.tick(0, L[i], ty); R[i] = djFilt.tick(1, R[i], ty); }
        }
        // DRIVE + CRUSH
        if (P.drive > 0.01f) { const float g = 1 + P.drive * P.drive * 14, c = 1 / (1 + P.drive * 1.2f); for (int i = 0; i < n; ++i) { L[i] = std::tanh(L[i] * g) * c; R[i] = std::tanh(R[i] * g) * c; } }
        if (P.crush > 0.01f) {
            const float q = std::exp2(12.0f - P.crush * 9.0f); const int hold = 1 + (int) (P.crush * P.crush * 12);
            for (int i = 0; i < n; ++i) { if (crushCount++ % hold == 0) { crushHold[0] = std::round(L[i] * q) / q; crushHold[1] = std::round(R[i] * q) / q; } L[i] = crushHold[0]; R[i] = crushHold[1]; }
        }
        // ECHO: dotted 1/8 ping-pong, always fed so nothing old replays when you turn it up
        {
            const int N = (int) echoL.size(); const int dly = juce::jlimit(1, N - 1, (int) (spb * 0.75)); const float mix = P.echo * 0.5f, fb = 0.3f + P.echo * 0.25f;
            for (int i = 0; i < n; ++i) {
                const int rp = (ePos - dly + N) % N; const float yl = echoL[(size_t) rp], yr = echoR[(size_t) rp];
                eLp[0] += (yr - eLp[0]) * 0.4f; eLp[1] += (yl - eLp[1]) * 0.4f;
                echoL[(size_t) ePos] = L[i] * (P.echo > 0.01f ? 1.0f : 0.0f) + eLp[0] * fb; echoR[(size_t) ePos] = eLp[1] * fb;
                L[i] += yl * mix; R[i] += yr * mix; ePos = (ePos + 1) % N;
            }
        }
        // REVERB
        if (P.verb > 0.01f) {
            if (legacyVerb) { juce::dsp::Reverb::Parameters rp; rp.roomSize = 0.5f + P.verb * 0.45f; rp.damping = 0.5f; rp.wetLevel = P.verb * 0.45f; rp.dryLevel = 1.0f; rp.width = 1.0f;
                              reverb.setParameters(rp); juce::dsp::AudioBlock<float> b(out); reverb.process(juce::dsp::ProcessContextReplacing<float>(b)); }
            else { fdn.set(1.2f + P.verb * 3.0f, 0.45f, 18.0f); fdn.process(L, R, n, P.verb * 0.28f); }
        }
        // STUTTER (hold): from the press, the music is kept (up to one beat) and its first 1/4, 1/8, 1/16 or 1/32 repeats. A new division
        // (dragging across the strip) takes over on the next grid point of THAT division, crossfaded over 64 samples, replaying from the
        // same press point: no gap, no doubled hit. Release crossfades back to the live music. No allocation.
        {
            static const double D[] = { 0, 1.0, 0.5, 0.25, 0.125 };
            const int N = (int) stutBuf.size(); const int want = juce::jlimit(0, 4, P.stutter); const int XF = 64;
            auto len = [&](int d) { return juce::jlimit(64, N - 1, (int) std::lround(spb * D[juce::jlimit(1, 4, d)])); };
            auto at = [&](int k, int d, float& l, float& r) {   // the stuttered sample k of division d (with the repeat's own edge fade after the first pass)
                const int Ld = len(d); const int kk = k % Ld; const int idx = kk % N; l = stutBuf[(size_t) idx]; r = stutBuf2[(size_t) idx];
                if (stut.rec > Ld) { const float w = juce::jmin(1.0f, juce::jmin((float) kk, (float) (Ld - kk)) / 64.0f); l *= w; r *= w; } };
            for (int i = 0; i < n; ++i) {
                const double beat = masterBeat0 + i / spb;
                if (!stut.on && want > 0) { stut = Stut(); stut.on = true; stut.div = want; stutterSwitches.fetch_add(1); }
                if (!stut.on && stut.outFade <= 0) continue;
                const float inL = L[i], inR = R[i];
                if (stut.on && stut.rec < N - 1 && stut.rec <= len(1)) { stutBuf[(size_t) stut.rec] = inL; stutBuf2[(size_t) stut.rec] = inR; ++stut.rec; }   // keep the music after the press (one beat is enough)
                if (stut.on && want == 0) { stut.on = false; stut.outFade = XF; stut.xfPos = stut.pos; stut.xfDiv = stut.div; }   // release: back to the live music
                if (stut.on && want != stut.div && want != stut.pending) { stut.pending = want; stut.switchAt = masterRunning ? std::ceil(beat / D[want] - 1e-9) * D[want] : beat; }
                if (stut.on && stut.pending && beat >= stut.switchAt - 1e-9) { stut.xfPos = stut.pos; stut.xfDiv = stut.div; stut.xf = XF; stut.div = stut.pending; stut.pending = 0; stut.pos = 0; stutterSwitches.fetch_add(1); stutterSwitchBeat.store(beat); }
                if (stut.on) {
                    float l, r; at(stut.pos, stut.div, l, r);
                    if (stut.xf > 0) { float ol, orr; at(stut.xfPos, stut.xfDiv, ol, orr); const float m = (float) stut.xf / (float) XF; l = l * (1 - m) + ol * m; r = r * (1 - m) + orr * m; ++stut.xfPos; --stut.xf; }
                    L[i] = l; R[i] = r; stut.pos = (stut.pos + 1) % len(stut.div);
                } else {   // fading back to the live music
                    float ol, orr; at(stut.xfPos, stut.xfDiv, ol, orr); const float m = (float) stut.outFade / (float) XF; L[i] = inL * (1 - m) + ol * m; R[i] = inR * (1 - m) + orr * m; ++stut.xfPos; --stut.outFade;
                }
            }
            uiStutter.store(stut.on ? stut.div : 0);
        }
        // TAPE STOP (hold): play the recent output back slower and slower
        const int RN = (int) ring.size();
        for (int i = 0; i < n; ++i) { ring[(size_t) rPos] = L[i]; ring2[(size_t) rPos] = R[i]; rPos = (rPos + 1) % RN; }
        if (P.tape) {
            if (!tapeWas) { tapePos = rPos - n; tapeRate = 1; tapeWas = true; }
            for (int i = 0; i < n; ++i) {
                tapeRate = juce::jmax(0.0f, tapeRate - (float) (1.0 / (0.9 * sr)));
                double p = std::fmod(tapePos + RN, (double) RN); const int k = (int) p; const float t = (float) (p - k);
                L[i] = (ring[(size_t) k] + (ring[(size_t) ((k + 1) % RN)] - ring[(size_t) k]) * t) * tapeRate;
                R[i] = (ring2[(size_t) k] + (ring2[(size_t) ((k + 1) % RN)] - ring2[(size_t) k]) * t) * tapeRate;
                tapePos += tapeRate;
            }
        } else tapeWas = false;
        // VOLUME + the output stage. CLEAN: linear volume (unity at the default 0.8) into a look-ahead peak limiter at the ceiling.
        // SOFT: the v1 tanh stage (v1 projects open with it, so they sound as they did). OFF: linear volume only (mix in the DAW).
        const float top = P.ceiling < -0.001f ? juce::jmin(0.95f, juce::Decibels::decibelsToGain(P.ceiling)) : 0.95f;   // the limiter can't go over the ceiling
        if (P.limiter == 1) { for (int i = 0; i < n; ++i) { L[i] = std::tanh(L[i] * P.volume * 1.1f) * top; R[i] = std::tanh(R[i] * P.volume * 1.1f) * top; } }
        else {
            const float g = P.volume * 1.25f; for (int i = 0; i < n; ++i) { L[i] *= g; R[i] *= g; }
            if (P.limiter == 0) limiter.process(L, R, n, top);
        }
    }
};

} // namespace gb
