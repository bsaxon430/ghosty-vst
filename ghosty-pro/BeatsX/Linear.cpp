// LINEAR DRUMMING: see Linear.h.
#include "Linear.h"
#include <algorithm>
#include <numeric>
#include <set>

namespace gb::linear {

// ---------------------------------------------------------------- order
int autoRank(uint8_t role, int t) {
    static const int roleRank[] { 7, 0, 1, 5, 3, 2, 4, 6 };   // by Role: other, kick, snare, hat, perc, 808, open hat, break
    return 16 + (role < 8 ? roleRank[role] : 7) * 16 + t;
}
int rankOf(const Settings& set, const Snapshot& S, int t) {
    if (set.order[(size_t) t] > 0) return juce::jlimit(1, 16, (int) set.order[(size_t) t]) - 1;
    return autoRank(S.role[(size_t) t], t);
}
std::array<int, 16> resolvedOrder(const Settings& s, const std::array<uint8_t, 16>& roles) {
    std::array<int, 16> o; std::iota(o.begin(), o.end(), 0);
    auto rk = [&](int t) { return s.order[(size_t) t] > 0 ? (int) s.order[(size_t) t] - 1 : autoRank(roles[(size_t) t], t); };
    std::stable_sort(o.begin(), o.end(), [&](int a, int b) { return rk(a) < rk(b); });
    return o;
}
void normaliseOrder(Settings& s, const std::array<uint8_t, 16>& roles) {
    bool any = false; for (auto v : s.order) if (v) any = true;
    if (!any) return;
    const auto o = resolvedOrder(s, roles);
    for (int r = 0; r < 16; ++r) s.order[(size_t) o[(size_t) r]] = (uint8_t) (r + 1);
}
void moveInOrder(Settings& s, const std::array<uint8_t, 16>& roles, int t, int r) {
    auto o = resolvedOrder(s, roles); std::vector<int> v(o.begin(), o.end());
    v.erase(std::find(v.begin(), v.end(), t)); v.insert(v.begin() + juce::jlimit(0, 15, r), t);
    for (int k = 0; k < 16; ++k) s.order[(size_t) v[(size_t) k]] = (uint8_t) (k + 1);
}

juce::String windowText(const Settings& s, double bpm) {
    const double w = windowBeats(s), ms = w * 60000.0 / juce::jmax(20.0, bpm);
    const juce::String m = juce::String(ms < 10 ? std::round(ms * 10) / 10 : std::round(ms), ms < 10 ? 1 : 0) + " ms";
    if (s.strictness <= 0.001f) return "the same instant only";
    const double den = 4.0 / w; for (int d : { 8, 12, 16, 24, 32, 48, 64, 96, 128 }) if (std::abs(den - d) < 0.01 * d) return "1/" + juce::String(d) + " = " + m;
    return m;
}

// ---------------------------------------------------------------- keys
ParamKey ParamKey::of(const EngineParams& P) {
    ParamKey k; k.swing = P.swing; k.human = P.human; k.glitch = P.glitch; k.chaos = P.chaos; k.density = P.density; k.mutation = P.mutation; k.fracture = P.fracture;
    k.gravity = P.gravity; k.memory = P.memory; k.tension = P.tension; k.resolve = P.resolve; k.groove = P.groove; k.pattern = P.pattern; k.songMode = P.songMode; return k;
}
bool ParamKey::operator==(const ParamKey& o) const {
    auto eq = [](float a, float b) { return std::abs(a - b) < 1e-7f; };
    return eq(swing, o.swing) && eq(human, o.human) && eq(glitch, o.glitch) && eq(chaos, o.chaos) && eq(density, o.density) && eq(mutation, o.mutation) && eq(fracture, o.fracture)
        && eq(gravity, o.gravity) && eq(memory, o.memory) && eq(tension, o.tension) && eq(resolve, o.resolve) && groove == o.groove && pattern == o.pattern && songMode == o.songMode;
}

namespace {

bool songMode(const Snapshot& S, const EngineParams& P) { return P.songMode && S.songCount > 0; }
bool hasHits(const Pattern& p, int t) { for (int s = 0; s < p.lenOf(t); ++s) if (p.steps[(size_t) t][(size_t) s].vel) return true; return false; }

// does the pattern-mode performance repeat exactly? (no bar-seeded or hit-seeded randomness anywhere)
bool periodic(const Snapshot& S, const EngineParams& P, const Pattern& p) {
    const EventCtx C(S, P, p, 0.0, 120.0);
    if (C.evolving || S.nProgs > 0 || P.human > 0.01f || P.glitch > 0.01f) return false;
    for (int t = 0; t < kTracks; ++t) {
        const auto& tr = S.tracks[(size_t) t]; if (tr.human > 0.001f) return false;
        for (auto& r : tr.rules) if (r.type) return false;
        for (int s = 0; s < p.lenOf(t); ++s) { const auto& st = p.steps[(size_t) t][(size_t) s]; if (st.vel && (st.prob < 100 || st.destroy > 0)) return false; }
    }
    return true;
}
long gcdl(long a, long b) { while (b) { const long t = a % b; a = b; b = t; } return a; }

bool evLess(const Ev& a, const Ev& b) {   // a total order: std::sort is deterministic
    if (a.beat != b.beat) return a.beat < b.beat; if (a.track != b.track) return a.track < b.track; if (a.vel != b.vel) return a.vel < b.vel; return a.orig < b.orig;
}

struct Builder {
    const Snapshot& S; const EngineParams& P; double bpm; const Settings& set; Perf& out;
    double w = 0.0625; long seedA = 0; int rank[kTracks] {};
    double chunkLen = 4; bool song = false;

    // raw hits of [a, b) in performance coordinates (pattern mode wraps around the period)
    void gather(double a, double b, std::vector<Ev>& v) {
        auto add = [&](double shift) { return [&, shift](int t, double beat, float vel, const EvInfo& e) {
            Ev x; x.beat = x.orig = beat + shift; x.vel = vel; x.e = e; x.track = (int8_t) t; x.flags = (set.lock >> t & 1) ? FlagLocked : 0; v.push_back(x); }; };
        if (song) { collectEvents(S, P, bpm, juce::jmax(0.0, a), juce::jmin(b, out.span), add(0.0)); return; }
        const double L = out.span; const long q0 = (long) std::floor(a / L), q1 = (long) std::floor(b / L);
        for (long q = q0; q <= q1; ++q) { const double base = q * L, lo = juce::jmax(a, base) - base, hi = juce::jmin(b, base + L) - base; if (hi > lo) collectEvents(S, P, bpm, lo, hi, add(base)); }
    }
    long seedPos(double beat) const {   // the position inside the period (pattern) or the song: a loop repeats its choices
        const double x = song ? beat : beat - std::floor(beat / out.span) * out.span;
        return (long) std::llround(x * 192.0);
    }
    double slotAt(double beat) const {
        int pat = 0; double org = 0; EngineParams Q = P; Q.songMode = song;
        const double b = song ? beat : beat - std::floor(beat / out.span) * out.span;
        if (locate(S, Q, juce::jmax(0.0, b), pat, org)) return stepBeats(S.patterns[(size_t) pat].res);
        return 0.25;
    }

    // chunk k: [k c, (k+1) c). Returns the hits that play there (sorted) and records drops / stats.
    void resolveRaw(int k, std::vector<Ev>& res, std::vector<Ev>& drops, Stats& st) {
        const double c0 = k * chunkLen, c1 = (k + 1 == out.chunks) ? out.span : c0 + chunkLen;
        std::vector<Ev> W; gather(c0 - 1.0, c1 + 1.0, W);
        std::sort(W.begin(), W.end(), evLess);
        const int n = (int) W.size(); std::vector<uint8_t> state((size_t) n, 0);
        int dens[kTracks] {}; for (auto& e : W) ++dens[e.track];
        for (auto& e : W) if (e.beat >= c0 && e.beat < c1) ++st.in;
        int wins[kTracks] {};
        const int rule = juce::jlimit(0, (int) RuleCount - 1, (int) set.rule);
        std::vector<int> order;
        std::set<std::pair<double, int>> accC;   // accepted hits of the current collision chain (beat, track)
        for (int i = 0; i < n;) {
            int j = i; while (j + 1 < n && W[(size_t) j + 1].beat - W[(size_t) j].beat < w) ++j;
            bool multi = false; for (int q = i + 1; q <= j && !multi; ++q) if (W[(size_t) q].track != W[(size_t) i].track) multi = true;
            if (!multi) { for (int q = i; q <= j; ++q) state[(size_t) q] = 1; i = j + 1; continue; }
            const long q192 = seedPos(W[(size_t) i].beat);
            const bool shake = set.variation > 0 && hash01(seedA + 11, q192) < 0.5f;   // VARIATION: this collision goes another way
            auto key = [&](const Ev& e) -> double {
                const int t = e.track;
                if (e.flags & FlagLocked) return 1e12;
                if (shake || rule == RuleRandom) return hash01(seedA + t * 131L + 3, q192) * 1e3;
                const double pr = -rank[t];
                switch (rule) {
                    case RuleAccents: {
                        double pos = std::fmod(e.beat, 4.0); if (pos < 0) pos += 4.0;
                        const double d16 = std::abs(pos / 0.25 - std::round(pos / 0.25)) * 0.25; const int p16 = ((int) std::lround(pos / 0.25)) & 15;
                        const bool onGrid = d16 < juce::jmax(w * 0.5, 1e-4);
                        const double strong = onGrid ? (p16 == 0 ? 3 : (p16 == 8 ? 2.5 : ((p16 & 3) == 0 ? 2 : ((p16 & 1) == 0 ? 1 : 0)))) : 0;
                        return (e.vel >= 0.93f ? 10.0 : 0.0) * 1e6 + strong * 1e6 + std::round(e.vel * 127.0) * 1e3 + pr;
                    }
                    case RuleAlternate: return -wins[t] * 1e6 + pr;
                    case RuleGroove: return -dens[t] * 1e3 + pr;
                    default: return pr;
                }
            };
            order.clear(); std::vector<double> keys; for (int q = i; q <= j; ++q) { order.push_back(q); keys.push_back(key(W[(size_t) q])); }
            std::vector<int> idx(order.size()); std::iota(idx.begin(), idx.end(), 0);
            std::sort(idx.begin(), idx.end(), [&](int a, int b) { return keys[(size_t) a] > keys[(size_t) b] || (keys[(size_t) a] == keys[(size_t) b] && a < b); });
            accC.clear(); bool won[kTracks] {};
            for (int q : idx) {
                const int a = order[(size_t) q]; const Ev& e = W[(size_t) a]; bool ok = true;
                if (!(e.flags & FlagLocked))
                    for (auto it = accC.lower_bound({ e.beat - w + 1e-12, -1 }); it != accC.end() && it->first < e.beat + w - 1e-12; ++it) if (it->second != e.track) { ok = false; break; }
                state[(size_t) a] = ok ? 1 : 2; if (ok) { accC.insert({ e.beat, e.track }); won[e.track] = true; }
            }
            for (int t = 0; t < kTracks; ++t) if (won[t]) ++wins[t];
            i = j + 1;
        }
        // winners fixed; the losers of this chunk move as gestures (or drop)
        std::set<std::pair<double, int>> acc; std::vector<Ev> moved;
        for (int i = 0; i < n; ++i) if (state[(size_t) i] == 1) acc.insert({ W[(size_t) i].beat, W[(size_t) i].track });
        auto freeAt = [&](double b) { auto it = acc.lower_bound({ b - w + 1e-12, -1 }); return !(it != acc.end() && it->first < b + w - 1e-12); };
        const double gGap = juce::jmax(w * 1.01, 0.1);
        auto tryMove = [&](const std::vector<int>& g) {
            const Ev& e0 = W[(size_t) g[0]];
            const double slot = juce::jmax(1.0 / 64.0, slotAt(e0.orig));
            const bool earlyFirst = set.variation > 0 && hash01(seedA + 29 + e0.track, seedPos(e0.orig)) < 0.5f;
            const int maxD = juce::jmax(1, (int) std::floor(1.0 / slot + 1e-9));   // at most a beat away
            for (int d = 1; d <= maxD; ++d)
                for (int sd = 0; sd < 2; ++sd) {
                    const double off = ((sd == 0) != earlyFirst ? d : -d) * slot;   // whole steps: swing / nudge / groove ride along
                    bool ok = true; for (int q : g) { const double bq = W[(size_t) q].orig + off; ok = ok && bq >= c0 && bq < c1 && freeAt(bq); }
                    if (!ok) continue;
                    for (int q : g) { Ev m2 = W[(size_t) q]; m2.beat = m2.orig + off; m2.flags |= FlagMoved; acc.insert({ m2.beat, m2.track }); moved.push_back(m2); }
                    return true;
                }
            return false;
        };
        for (int i = 0; i < n; ++i) {
            if (state[(size_t) i] != 2 || !(W[(size_t) i].beat >= c0 && W[(size_t) i].beat < c1)) continue;
            std::vector<int> g { i }; state[(size_t) i] = 3;
            if (set.move) for (int q = i + 1; q < n && W[(size_t) q].beat - W[(size_t) g.back()].beat < gGap; ++q)
                if (W[(size_t) q].track == W[(size_t) i].track && state[(size_t) q] == 2 && W[(size_t) q].beat < c1) { g.push_back(q); state[(size_t) q] = 3; }
            if (set.move && tryMove(g)) continue;
            for (int q : g) { if (g.size() > 1 && set.move && tryMove({ q })) continue; Ev e = W[(size_t) q]; e.flags |= FlagDropped; drops.push_back(e); }
        }
        res.clear();
        for (int i = 0; i < n; ++i) if (state[(size_t) i] == 1 && W[(size_t) i].beat >= c0 && W[(size_t) i].beat < c1) res.push_back(W[(size_t) i]);
        for (auto& m : moved) res.push_back(m);
        std::sort(res.begin(), res.end(), evLess);
    }
};

} // namespace

bool tempoMatters(const Snapshot& S, const EngineParams& P) {
    if (S.nProgs > 0 || P.fracture > 0.001f) return true;
    auto scan = [&](const Pattern& p) {
        for (int t = 0; t < kTracks; ++t) { if (S.tracks[(size_t) t].human > 0.001f) return true;
            for (int s = 0; s < p.lenOf(t); ++s) { const auto& st = p.steps[(size_t) t][(size_t) s]; if (st.vel && (st.flam || st.freeze || st.destroy)) return true; } }
        return false;
    };
    if (songMode(S, P)) { for (int i = 0; i < S.songCount; ++i) if (scan(S.patterns[(size_t) juce::jlimit(0, kPatterns - 1, S.song[(size_t) i].pattern)])) return true; return false; }
    return scan(S.patterns[(size_t) juce::jlimit(0, kPatterns - 1, P.pattern)]);
}

bool fresh(const Perf* p, const Snapshot& S, const EngineParams& P, double bpm, const Settings& set, int horizonBars) {
    if (!p) return false;
    if (p->content != S.serial || p->set != set || !(p->key == ParamKey::of(P)) || (!p->periodic && p->horizonBars != horizonBars)) return false;
    if (p->bpmMatters && std::abs(p->bpm - bpm) > 1e-6) return false;
    return true;
}

PerfPtr build(const Snapshot& S, const EngineParams& P, double bpm, const Settings& set, int horizonBars) {
    auto perf = std::make_shared<Perf>(); Perf& out = *perf;
    out.content = S.serial; out.set = set; out.key = ParamKey::of(P); out.bpm = bpm; out.bpmMatters = tempoMatters(S, P); out.horizonBars = juce::jlimit(1, 512, horizonBars);
    Builder B { S, P, bpm, set, out };
    B.w = windowBeats(set); B.seedA = (long) juce::jmax(1, set.seed) * 7919L + (long) set.variation * 104729L;
    for (int t = 0; t < kTracks; ++t) B.rank[t] = rankOf(set, S, t);
    B.song = out.song = songMode(S, P);
    if (out.song) {
        int end = 1; for (int i = 0; i < S.songCount; ++i) end = juce::jmax(end, S.song[(size_t) i].startBar + S.song[(size_t) i].bars);
        out.span = end * 4.0; out.chunks = end; out.periodic = false; B.chunkLen = 4.0;
    } else {
        const auto& p = S.patterns[(size_t) juce::jlimit(0, kPatterns - 1, P.pattern)];
        const double sb = stepBeats(p.res); double span = 0;
        if (periodic(S, P, p)) {
            if (S.converge > 0) span = S.converge * 4.0;
            else { long l = 1; bool any = false; for (int t = 0; t < kTracks; ++t) if (hasHits(p, t)) { any = true; const long len = p.lenOf(t); l = l / gcdl(l, len) * len; if (l * sb > 256.0) break; }
                   span = (any ? (double) l : (double) p.length) * sb; }
            if (span > 256.0 + 1e-9) span = 0;   // a polymeter that needs more than 64 bars to come round: a fixed performance instead
        }
        out.periodic = span > 0;
        if (!out.periodic) span = out.horizonBars * 4.0;
        out.span = span; out.chunks = juce::jmax(1, (int) std::lround(span / 4.0)); B.chunkLen = span / out.chunks;
    }
    // resolve each chunk, then stitch: the head of chunk k against the tail of chunk k-1 (pattern mode: chunk 0 against the last one)
    const int m = out.chunks; const double w = B.w;
    std::vector<std::vector<Ev>> R((size_t) m); std::vector<Ev> drops;
    for (int k = 0; k < m; ++k) B.resolveRaw(k, R[(size_t) k], drops, out.st);
    for (int k = 0; k < m; ++k) {
        const double c0 = k * B.chunkLen, c1 = (k + 1 == m) ? out.span : c0 + B.chunkLen;
        std::vector<std::pair<double, int>> tail; std::vector<uint8_t> tailLock;
        if (k > 0 || !out.song) {
            const int pk = (k + m - 1) % m; const double shift = (k == 0) ? -out.span : 0.0;
            for (auto it = R[(size_t) pk].rbegin(); it != R[(size_t) pk].rend() && it->beat + shift >= c0 - w; ++it) { tail.push_back({ it->beat + shift, it->track }); tailLock.push_back(it->flags & FlagLocked); }
        }
        auto& cur = R[(size_t) k]; std::vector<Ev> keep; std::vector<Ev> redo;
        for (auto& e : cur) {
            bool clash = false;
            if (e.beat < c0 + w && !(e.flags & FlagLocked)) for (size_t q = 0; q < tail.size(); ++q) if (tail[q].second != e.track && std::abs(tail[q].first - e.beat) < w) { clash = true; break; }
            (clash ? redo : keep).push_back(e);
        }
        for (auto e : redo) {   // put it back somewhere free in this chunk, or drop it
            bool placed = false;
            if (set.move) {
                const double slot = juce::jmax(1.0 / 64.0, B.slotAt(e.orig)); const int maxD = juce::jmax(1, (int) std::floor(1.0 / slot + 1e-9));
                for (int d = 1; d <= maxD && !placed; ++d) {
                    const double b = e.orig + d * slot; if (b >= c1) break;
                    bool ok = true; for (auto& x : keep) if (std::abs(x.beat - b) < w) { ok = false; break; }
                    for (auto& t : tail) if (ok && std::abs(t.first - b) < w) ok = false;
                    if (ok) { if (!(e.flags & FlagMoved)) { e.flags |= FlagMoved; } e.beat = b; keep.push_back(e); std::sort(keep.begin(), keep.end(), evLess); placed = true; }
                }
            }
            if (!placed) { e.flags |= FlagDropped; e.beat = e.orig; drops.push_back(e); }
        }
        cur.swap(keep);
    }
    for (auto& r : R) for (auto& e : r) { out.ev.push_back(e); if (e.flags & FlagMoved) ++out.st.moved; else ++out.st.kept; }
    std::sort(out.ev.begin(), out.ev.end(), evLess);
    out.dropped = std::move(drops); out.st.dropped = (int) out.dropped.size();
    std::sort(out.dropped.begin(), out.dropped.end(), [](const Ev& a, const Ev& b) { return a.orig < b.orig || (a.orig == b.orig && a.track < b.track); });
    out.st.overflow = out.st.in - (out.st.kept + out.st.moved + out.st.dropped);   // (0: every hit is accounted for)
    return perf;
}

std::vector<Ev> render(const Perf& p, double b0, double b1, bool withDropped) {
    std::vector<Ev> out; b0 = juce::jmax(0.0, b0); if (!(b1 > b0)) return out;
    auto take = [&](const std::vector<Ev>& v, bool byOrig) {
        if (p.song) { for (auto& e : v) { const double x = byOrig ? e.orig : e.beat; if (x >= b0 && x < b1) out.push_back(e); } return; }
        const long q0 = (long) std::floor(b0 / p.span), q1 = (long) std::floor(b1 / p.span);
        for (long q = q0; q <= q1; ++q) for (auto e : v) { const double sh = q * p.span; e.beat += sh; e.orig += sh; const double x = byOrig ? e.orig : e.beat; if (x >= b0 && x < b1) out.push_back(e); }
    };
    take(p.ev, false); if (withDropped) take(p.dropped, true);
    std::sort(out.begin(), out.end(), evLess);
    return out;
}

std::vector<Ev> render(const Snapshot& S, const EngineParams& P, double bpm, double b0, double b1, const Settings& set, bool withDropped, Stats* st, int horizonBars) {
    if (!set.on) {
        std::vector<Ev> out;
        collectEvents(S, P, bpm, b0, b1, [&](int t, double beat, float vel, const EvInfo& e) { Ev x; x.beat = x.orig = beat; x.vel = vel; x.e = e; x.track = (int8_t) t; out.push_back(x); });
        std::sort(out.begin(), out.end(), evLess); if (st) { *st = {}; st->in = st->kept = (int) out.size(); } return out;
    }
    const auto p = build(S, P, bpm, set, horizonBars); if (st) *st = p->st;
    return render(*p, b0, b1, withDropped);
}

juce::ValueTree toTree(const Settings& s) {
    juce::ValueTree t("LINEAR");
    t.setProperty("on", s.on, nullptr); t.setProperty("rule", (int) s.rule, nullptr); t.setProperty("strict", s.strictness, nullptr); t.setProperty("move", s.move, nullptr);
    t.setProperty("lock", (int) s.lock, nullptr); t.setProperty("var", s.variation, nullptr); t.setProperty("seed", s.seed, nullptr);
    juce::StringArray a; for (auto v : s.order) a.add(juce::String((int) v)); t.setProperty("order", a.joinIntoString(","), nullptr);
    return t;
}
Settings fromTree(const juce::ValueTree& t) {
    Settings s; if (!t.isValid()) return s;
    s.on = t.getProperty("on", false); s.rule = (uint8_t) juce::jlimit(0, (int) RuleCount - 1, (int) t.getProperty("rule", 0));
    s.strictness = juce::jlimit(0.0f, 1.0f, (float) (double) t.getProperty("strict", 0.25)); s.move = t.getProperty("move", true);
    s.lock = (uint16_t) ((int) t.getProperty("lock", 0) & 0xffff); s.variation = juce::jlimit(0, 999, (int) t.getProperty("var", 0)); s.seed = juce::jmax(0, (int) t.getProperty("seed", 0));
    auto a = juce::StringArray::fromTokens(t.getProperty("order").toString(), ",", "");
    for (int i = 0; i < 16; ++i) s.order[(size_t) i] = (uint8_t) juce::jlimit(0, 16, i < a.size() ? a[i].getIntValue() : 0);
    return s;
}

} // namespace gb::linear
