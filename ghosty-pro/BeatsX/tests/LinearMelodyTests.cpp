// Ghosty Beats LINEAR DRUMMING + MELODIC TRACKER tests.   LinearMelodyTests [test | shots <dir> | demos <dir> | all <shots> <demos>]
#include "../Linear.h"
#include "../MelodyTracker.h"
#include "../MelodyTrackerView.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <cstdio>

// allocations on the thread that runs the audio paths: counted only while gRtCount is set on that thread
#include <new>
#include <cstdlib>
static thread_local bool gRtCount = false; static std::atomic<long> gRtAllocs { 0 };
static void* rtAlloc(std::size_t n) { if (gRtCount) ++gRtAllocs; if (void* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new(std::size_t n) { return rtAlloc(n); } void* operator new[](std::size_t n) { return rtAlloc(n); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { if (gRtCount) ++gRtAllocs; return std::malloc(n ? n : 1); } void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { if (gRtCount) ++gRtAllocs; return std::malloc(n ? n : 1); }
void operator delete(void* p) noexcept { std::free(p); } void operator delete(void* p, std::size_t) noexcept { std::free(p); } void operator delete[](void* p) noexcept { std::free(p); } void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); } void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }

static int fails = 0, passes = 0;
#define CHECK(cond, ...) do { if (cond) { ++passes; } else { ++fails; std::printf("  FAIL %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)
static void section(const char* s) { std::printf("== %s\n", s); }
#if defined(__SANITIZE_ADDRESS__)
static bool sanitized() { return true; }
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
static bool sanitized() { return true; }
#else
static bool sanitized() { return false; }
#endif
#else
static bool sanitized() { return false; }
#endif
using namespace gb;
using linear::Ev;

// ---------------------------------------------------------------- LINEAR helpers
static std::unique_ptr<Snapshot> baseSnap() {
    auto S = std::make_unique<Snapshot>();
    const uint8_t roles[kTracks] { RoleKick, RoleSnare, RoleHat, RoleOpenHat, RolePerc, Role808, RoleOther, RolePerc, RoleOther, RoleOther, RoleOther, RoleOther, RoleOther, RoleOther, RoleOther, RoleOther };
    for (int t = 0; t < kTracks; ++t) S->role[(size_t) t] = roles[t];
    for (auto& p : S->patterns) p.clear();
    return S;
}
static void randomPattern(Pattern& p, juce::Random& r, bool fancy) {
    p.clear(); const int resOpt[] { 1, 1, 1, 2, 3 }; p.res = resOpt[r.nextInt(5)]; p.length = p.res == 2 ? 32 : (p.res == 3 ? 24 : 16);
    for (int t = 0; t < 8; ++t) {
        const float dens = t == 2 ? 0.5f + r.nextFloat() * 0.5f : r.nextFloat() * 0.45f;
        if (fancy && r.nextFloat() < 0.2f) p.trackLen[(size_t) t] = (uint8_t) (10 + r.nextInt(8));
        for (int s = 0; s < p.length; ++s) {
            if (r.nextFloat() >= dens) continue;
            auto& st = p.steps[(size_t) t][(size_t) s]; st.vel = (uint8_t) (40 + r.nextInt(88));
            if (fancy) {
                if (r.nextFloat() < 0.1f) st.roll = (uint8_t) (2 + r.nextInt(3));
                if (st.roll > 1 && r.nextFloat() < 0.5f) st.rate = (uint8_t) (r.nextBool() ? 16 : 8);
                if (r.nextFloat() < 0.06f) st.flam = 18;
                if (r.nextFloat() < 0.2f) st.nudge = (int8_t) (r.nextInt(41) - 20);
                if (r.nextFloat() < 0.1f) st.prob = 60;
            }
        }
    }
    if (fancy && r.nextFloat() < 0.3f) { p.evolve = 0.6f; p.seed = r.nextInt(1000); }
}
static int overlaps(const std::vector<Ev>& v, double w, uint16_t lock, double* worst = nullptr) {
    int n = 0;
    for (size_t i = 0; i < v.size(); ++i)
        for (size_t j = i + 1; j < v.size() && v[j].beat - v[i].beat < w - 1e-9; ++j)
            if (v[j].track != v[i].track && !((lock >> v[i].track & 1) && (lock >> v[j].track & 1))) { ++n; if (worst) *worst = juce::jmax(*worst, w - (v[j].beat - v[i].beat)); }
    return n;
}
static void attach(Snapshot& S, const EngineParams& P, double bpm, const linear::Settings& set, int horizon = 16) { ++S.serial; S.linear = set; S.linPerfPrev = S.linPerf; S.linPerf = set.on ? linear::build(S, P, bpm, set, horizon) : nullptr; }
// what the engine plays: the snapshot's Perf through a linear::Player, in blocks of `blk` beats (random sizes when blk <= 0)
static std::vector<Ev> played(const Snapshot& S, const EngineParams& P, double bpm, double b0, double b1, double blk, juce::Random* r = nullptr, linear::Player* pl = nullptr) {
    std::vector<Ev> v; linear::Player own; linear::Player& L = pl ? *pl : own;
    for (double b = b0; b < b1;) { const double len = blk > 0 ? blk : (16 + r->nextInt(2000)) / 48000.0 * 2.5; const double e = juce::jmin(b1, b + len);
        L.process(S, P, bpm, b, e, [&](int t, double beat, float vel, const EvInfo& ei) { Ev x; x.beat = beat; x.track = (int8_t) t; x.vel = vel; x.e = ei; v.push_back(x); }); b = e; }
    return v;
}
namespace gb::linear { struct Strip { std::array<int8_t, 64> track, droppedTrack; std::array<uint8_t, 64> flags {}; Strip() { track.fill(-1); droppedTrack.fill(-1); } }; }
static std::vector<Ev> window(const std::vector<Ev>& v, double a, double b, double shift) { std::vector<Ev> o; for (auto e : v) if (e.beat >= a && e.beat < b) { e.beat -= shift; o.push_back(e); } return o; }
static std::vector<Ev> raw(const Snapshot& S, const EngineParams& P, double bpm, double b0, double b1) { linear::Settings off; return linear::render(S, P, bpm, b0, b1, off); }
static int countTrack(const std::vector<Ev>& v, int t) { int n = 0; for (auto& e : v) if (e.track == t) ++n; return n; }
static bool sameEvents(const std::vector<Ev>& a, const std::vector<Ev>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) if (std::abs(a[i].beat - b[i].beat) > 1e-9 || a[i].track != b[i].track || a[i].vel != b[i].vel || a[i].e.gate != b[i].e.gate || a[i].e.pitch != b[i].e.pitch) return false;
    return true;
}

static void linearTests() {
    section("LINEAR: zero overlaps (random patterns x rules x strictness x MOVE/DROP, 8 bars each)");
    {
        juce::Random r(7); int runs = 0, bad = 0, hitsIn = 0, kept = 0, moved = 0, dropped = 0; double worst = 0;
        for (int it = 0; it < 240; ++it) {
            auto S = baseSnap(); randomPattern(S->patterns[0], r, it % 2 == 0);
            EngineParams P; P.bpm = 140; if (it % 5 == 0) { P.chaos = 0.4f; P.glitch = 0.2f; P.human = 0.3f; P.swing = 0.3f; }
            linear::Settings set; set.on = true; set.rule = (uint8_t) (it % linear::RuleCount); set.strictness = (float) ((it / 5) % 5) * 0.25f; set.move = (it / 3) % 2 == 0;
            set.variation = it % 4; set.seed = it; if (it % 7 == 0) set.lock = 0x0004;
            linear::Stats st; const auto v = linear::render(*S, P, 140, 0, 32, set, false, &st);
            const int o = overlaps(v, linear::windowBeats(set), set.lock, &worst); ++runs; if (o) bad++;
            hitsIn += st.in; kept += st.kept; moved += st.moved; dropped += st.dropped;
            if (st.in != st.kept + st.moved + st.dropped) { CHECK(false, "stats don't add up: in %d kept %d moved %d dropped %d", st.in, st.kept, st.moved, st.dropped); }
        }
        std::printf("  %d runs, %d with overlaps (worst %.4f beats); hits in %d -> kept %d, moved %d, dropped %d\n", runs, bad, worst, hitsIn, kept, moved, dropped);
        CHECK(bad == 0, "overlaps in %d runs", bad);
    }
    section("LINEAR: SONG + EVOLVE + macros");
    {
        juce::Random r(99); int bad = 0, total = 0;
        for (int it = 0; it < 40; ++it) {
            auto S = baseSnap(); for (int p = 0; p < 3; ++p) randomPattern(S->patterns[(size_t) p], r, true);
            S->patterns[1].evolve = 0.8f; S->song[0] = { 0, 0, 2 }; S->song[1] = { 1, 2, 3 }; S->song[2] = { 2, 5, 1 }; S->song[3] = { 0, 6, 2 }; S->songCount = 4;
            EngineParams P; P.songMode = true; P.bpm = 128; P.mutation = 0.3f; P.tension = 0.4f;
            linear::Settings set; set.on = true; set.rule = (uint8_t) (it % linear::RuleCount); set.strictness = 0.3f + 0.15f * (it % 4); set.move = it % 2 == 0; set.variation = it % 3;
            const auto v = linear::render(*S, P, 128, 0, 32, set); total += (int) v.size(); if (overlaps(v, linear::windowBeats(set), set.lock)) ++bad;
        }
        std::printf("  40 songs (8 bars, 4 blocks, evolve + mutation + tension): %d hits, %d with overlaps\n", total, bad); CHECK(bad == 0 && total > 1000, "song overlaps");
    }
    section("LINEAR: engine playback in any block size == one-shot render == export");
    {
        juce::Random r(3); int same = 0, runs = 0;
        for (int it = 0; it < 30; ++it) {
            auto S = baseSnap(); randomPattern(S->patterns[0], r, true); EngineParams P; P.bpm = 150;
            linear::Settings set; set.on = true; set.rule = (uint8_t) (it % 5); set.strictness = 0.5f; set.move = true; set.variation = it % 2; set.seed = 7;
            attach(*S, P, 150, set);
            const auto whole = linear::render(*S, P, 150, 0, 16, set);
            const auto blocks = played(*S, P, 150, 0, 16, 0, &r);
            std::vector<Ev> exp; linear::collect(*S, P, 150, 0, 16, set, [&](int t, double beat, float vel, const EvInfo& ei) { Ev x; x.beat = beat; x.track = (int8_t) t; x.vel = vel; x.e = ei; exp.push_back(x); });
            ++runs; if (sameEvents(whole, blocks) && sameEvents(whole, exp)) ++same;
        }
        std::printf("  %d / %d random patterns: random-size engine blocks, one-shot render and the export give identical hits\n", same, runs); CHECK(same == runs, "block independence");
    }
    section("LINEAR blocker 1: the Perf is keyed by the snapshot's content serial, never its address");
    {
        auto S = baseSnap(); auto& p = S->patterns[0]; for (int s = 0; s < 16; s += 4) p.steps[0][(size_t) s].vel = 110; for (int s = 0; s < 16; s += 2) p.steps[2][(size_t) s].vel = 90;
        EngineParams P; P.bpm = 120; linear::Settings set; set.on = true; set.seed = 3; attach(*S, P, 120, set);
        const auto before = played(*S, P, 120, 0, 4, 0.05);
        const linear::Perf* oldPerf = S->linPerf.get(); const bool freshBefore = linear::fresh(oldPerf, *S, P, 120, set, 16);
        // the SAME object (same address) gets new content: kick moves to the off-beats
        for (int s = 0; s < 16; ++s) p.steps[0][(size_t) s].vel = 0; for (int s = 2; s < 16; s += 4) p.steps[0][(size_t) s].vel = 110; ++S->serial;
        const bool staleSeen = !linear::fresh(oldPerf, *S, P, 120, set, 16);
        S->linPerfPrev = S->linPerf; S->linPerf = linear::build(*S, P, 120, set); const auto after = played(*S, P, 120, 0, 4, 0.05);
        int kicksOff = 0; for (auto& e : after) if (e.track == 0 && std::abs(std::fmod(e.beat, 1.0) - 0.5) < 1e-6) ++kicksOff;
        std::printf("  same address, new content: old Perf fresh before %d, recognised stale after %d; new Perf plays the edit: %d / 4 off-beat kicks (before: %d hits)\n", (int) freshBefore, (int) staleSeen, kicksOff, (int) before.size());
        CHECK(freshBefore && staleSeen && kicksOff == 4, "serial key (ABA)");
    }
    section("LINEAR blocker 2 + 3: a loop resolves identically every time (bar 0 == bar 1 == bar 7), all rules + variations");
    {
        juce::Random r(9); int same = 0, runs = 0, exportSame = 0;
        for (int it = 0; it < 200; ++it) {
            auto S = baseSnap(); auto& p = S->patterns[0]; randomPattern(p, r, false); p.res = 1; p.length = 16; for (auto& l : p.trackLen) l = 0;
            for (int t = 0; t < 8; ++t) for (int s = 0; s < 16; ++s) if (p.steps[(size_t) t][(size_t) s].vel && r.nextFloat() < 0.3f) p.steps[(size_t) t][(size_t) s].nudge = (int8_t) (r.nextInt(41) - 20);
            EngineParams P; P.bpm = 128; linear::Settings set; set.on = true; set.rule = (uint8_t) (it % linear::RuleCount); set.variation = it % 4; set.seed = 11 + it; set.strictness = 0.6f; set.move = it % 2 == 0;
            attach(*S, P, 128, set);
            const auto v = played(*S, P, 128, 0, 32, 0, &r);
            const auto b0 = window(v, 0, 4, 0), b1 = window(v, 4, 8, 4), b7 = window(v, 28, 32, 28);
            ++runs; if (sameEvents(b0, b1) && sameEvents(b1, b7)) ++same;
            else if (std::getenv("LINDEBUG")) { std::printf("   it %d sizes %d %d %d span %.3f\n", it, (int) b0.size(), (int) b1.size(), (int) b7.size(), S->linPerf->span);
                for (size_t q = 0; q < juce::jmax(b0.size(), b1.size()); ++q) std::printf("     %s | %s\n", q < b0.size() ? (juce::String(b0[q].beat, 6) + " t" + juce::String(b0[q].track)).toRawUTF8() : "-", q < b1.size() ? (juce::String(b1[q].beat, 6) + " t" + juce::String(b1[q].track)).toRawUTF8() : "-"); }
            // DRAG MIDI exports one period: it equals every loop the engine plays
            std::vector<Ev> exp; linear::play(*S->linPerf, 0, S->linPerf->exportBeats(), [&](int t, double beat, float vel, const EvInfo& ei) { Ev x; x.beat = beat; x.track = (int8_t) t; x.vel = vel; x.e = ei; exp.push_back(x); });
            if (S->linPerf->span == 4.0 && sameEvents(exp, b0) && sameEvents(exp, b7)) ++exportSame;
        }
        std::printf("  200 random 1-bar beats with nudges (strictness 60%%, every rule, variations 0-3): bar 0 == bar 1 == bar 7 in %d; the exported period == loops 1 and 8 in %d\n", same, exportSame);
        CHECK(same == runs && exportSame == runs, "loop repeats + export");
    }
    section("LINEAR major 5: no hits lost at the extremes (16 tracks, 1/64, 128 steps, roll 16, retrig 1/128, flam, accel 0/30/60)");
    {
        auto S = baseSnap(); auto& p = S->patterns[0]; p.res = 5; p.length = 128;
        for (int t = 0; t < 16; ++t) for (int s = 0; s < 128; ++s) { auto& st = p.steps[(size_t) t][(size_t) s]; st.vel = 100; st.roll = 16; st.rate = 32; st.flam = 10; st.accel = (int8_t) ((t % 3) * 30); }   // (speeding up: a slowing 16-hit roll runs past collectEvents' one-beat look-back, for the plain engine too)
        EngineParams P; P.bpm = 140; linear::Settings set; set.on = true; set.strictness = 0.0f; set.move = true;
        const auto rawHits = raw(*S, P, 140, 0, 8); int rawBeat4 = 0; for (auto& e : rawHits) if (e.beat >= 3 && e.beat < 4) ++rawBeat4;
        const double t0 = juce::Time::getMillisecondCounterHiRes(); attach(*S, P, 140, set); const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        const auto& st = S->linPerf->st; int linBeat4 = 0; for (auto& e : S->linPerf->ev) if (e.beat >= 3 && e.beat < 4) ++linBeat4;
        std::printf("  raw %d hits in the 2-bar loop (%d in beat 4); LINEAR in %d = kept %d + moved %d + dropped %d, overflow %d; beat 4 plays %d; built in %.0f ms (worker / publish, never the audio thread)\n",
                    (int) rawHits.size(), rawBeat4, st.in, st.kept, st.moved, st.dropped, st.overflow, linBeat4, ms);
        CHECK(st.in == (int) rawHits.size() && st.overflow == 0 && st.in == st.kept + st.moved + st.dropped && linBeat4 > 0, "no silent caps");
    }
    section("LINEAR major 4: the audio thread never resolves (dense 1/64 beat, macro + swing + tempo automated every block)");
    {
        auto S = baseSnap(); auto& p = S->patterns[0]; p.res = 5; p.length = 64;
        for (int t = 0; t < 16; ++t) for (int s = 0; s < 64; ++s) { auto& st = p.steps[(size_t) t][(size_t) s]; st.vel = 100; st.roll = 4; st.flam = 12; }
        EngineParams P; P.bpm = 174; linear::Settings set; set.on = true; set.strictness = 1.0f; attach(*S, P, 174, set);
        linear::Player pl; const double spb = 48000 * 60.0 / 174, blk = 64 / spb; double worst = 0, total = 0; long hits = 0, blocks = 0;
        gRtAllocs = 0; gRtCount = true;
        for (double b = 0; b < 64; b += blk) {
            P.chaos = (float) std::fmod(b * 0.01, 0.5); P.swing = (float) std::fmod(b * 0.003, 0.3); const double bpm = 174 + std::fmod(b, 7.0);   // automation every block
            const double t0 = juce::Time::getMillisecondCounterHiRes();
            pl.process(*S, P, bpm, b, b + blk, [&](int, double, float, const EvInfo&) { ++hits; });
            const double d = juce::Time::getMillisecondCounterHiRes() - t0; worst = juce::jmax(worst, d); total += d; ++blocks;
        }
        gRtCount = false;
        std::printf("  %ld 64-sample blocks (1.33 ms each): mean %.4f ms, worst %.4f ms per block, %ld hits, allocations %ld\n", blocks, total / blocks, worst, hits, gRtAllocs.load());
        CHECK(gRtAllocs.load() == 0 && (sanitized() || (worst < 0.2 && total / blocks < 0.02)), "audio thread cost");
    }
    section("LINEAR minor 19: a new Perf takes over at the next bar line (no double / skipped hit mid-bar)");
    {
        auto S = baseSnap(); auto& p = S->patterns[0]; for (int s = 0; s < 16; s += 4) p.steps[0][(size_t) s].vel = 110; for (int s = 0; s < 16; s += 2) p.steps[2][(size_t) s].vel = 90;
        EngineParams P; P.bpm = 120; linear::Settings set; set.on = true; set.move = true; set.strictness = 0.25f; attach(*S, P, 120, set);
        linear::Player pl; auto a = played(*S, P, 120, 0, 5.5, 0.05, nullptr, &pl);   // into bar 2
        set.move = false; attach(*S, P, 120, set);                                      // DROP now: the moved hats vanish
        auto b = played(*S, P, 120, 5.5, 12, 0.05, nullptr, &pl);
        int bar2Moved = 0, bar3Moved = 0;
        // with MOVE the hat losers move to the free 16ths, with DROP they vanish: bar 2 (already started) must still be the MOVE version
        auto hatsIn = [&](const std::vector<Ev>& v, double x0, double x1) { int n = 0; for (auto& e : v) if (e.track == 2 && e.beat >= x0 && e.beat < x1) ++n; return n; };
        bar2Moved = hatsIn(a, 4, 5.5) + hatsIn(b, 5.5, 8); bar3Moved = hatsIn(b, 8, 12);
        std::printf("  edit at beat 5.5: bar 2 keeps the MOVE result (%d hats, want 8), bar 3 plays DROP (%d hats, want 4)\n", bar2Moved, bar3Moved);
        CHECK(bar2Moved == 8 && bar3Moved == 4, "switch at the bar line");
        // two more edits inside one bar: the Perf the player was finishing is no longer owned by the snapshot -> it switches at once (no stale read)
        set.move = true; attach(*S, P, 120, set); auto c = played(*S, P, 120, 12, 12.5, 0.05, nullptr, &pl); set.move = false; attach(*S, P, 120, set); set.move = true; attach(*S, P, 120, set);
        auto d2 = played(*S, P, 120, 12.5, 16, 0.05, nullptr, &pl); int hats4 = 0; for (auto& e : c) if (e.track == 2) ++hats4; for (auto& e : d2) if (e.track == 2) ++hats4;
        std::printf("  three edits inside bar 4: the player never reads a released performance; bar 4 plays %d hats (MOVE: 8)\n", hats4);
        CHECK(hats4 == 8, "no stale performance after quick edits");
    }
    section("LINEAR majors 6 + 7: a variation keeps its seed; PRIORITY moves one track without jumping the kick");
    {
        linear::Settings s; linear::setOn(s, true, 41); const int seed0 = s.seed; linear::setOn(s, false, 99); linear::setOn(s, true, 77);
        linear::stepVariation(s, 1); linear::stepVariation(s, 1); linear::stepVariation(s, 1); linear::stepVariation(s, -1); linear::stepVariation(s, 1);
        const bool seedKept = s.seed == seed0 && s.variation == 3 && !linear::stepVariation(s, 999) == false;
        std::array<uint8_t, 16> roles {}; roles[0] = RoleKick; roles[1] = RoleSnare; roles[2] = RoleHat; roles[3] = RoleOpenHat;
        linear::Settings o; linear::moveInOrder(o, roles, 2, 1);   // the hat one place up: second, after the kick
        const auto ord = linear::resolvedOrder(o, roles); std::set<int> uniq(o.order.begin(), o.order.end());
        std::printf("  seed after on/off/on with other tool seeds: %d (first %d), variation 3 after > > > < >: %d | hat moved to rank 2: order %d,%d,%d.. kick still first %d, 16 unique ranks %d\n",
                    s.seed, seed0, s.variation, ord[0], ord[1], ord[2], (int) (ord[0] == 0), (int) uniq.size());
        CHECK(seedKept && ord[0] == 0 && ord[1] == 2 && ord[2] == 1 && uniq.size() == 16, "seed + priority");
    }
    // a dense test groove: kick 4/4 accented, snare 2 + 4, 16th hats, open hat off-beats, perc
    auto groove = [] {
        auto S = baseSnap(); auto& p = S->patterns[0]; p.length = 16; p.res = 1;
        for (int s = 0; s < 16; s += 4) p.steps[0][(size_t) s].vel = 127;
        for (int s : { 4, 12 }) p.steps[1][(size_t) s].vel = 115;
        for (int s = 0; s < 16; ++s) p.steps[2][(size_t) s].vel = (uint8_t) (s % 2 ? 70 : 100);
        for (int s : { 2, 6, 10, 14 }) p.steps[3][(size_t) s].vel = 90;
        for (int s : { 3, 7, 11 }) p.steps[4][(size_t) s].vel = 80;
        return S;
    };
    section("LINEAR: seeded VARIATIONS differ, the same seed repeats");
    {
        auto S = groove(); auto& gp = S->patterns[0]; gp.length = 32; for (int t = 0; t < 5; ++t) for (int s2 = 0; s2 < 16; ++s2) gp.steps[(size_t) t][(size_t) (s2 + 16)] = gp.steps[(size_t) t][(size_t) s2];
        for (int s2 : { 17, 19, 22, 25, 27, 30 }) gp.steps[4][(size_t) s2].vel = 85; for (int s2 : { 18, 26, 29, 31 }) gp.steps[1][(size_t) s2].vel = 60;   // bar 2: more collisions
        EngineParams P; P.bpm = 140; linear::Settings set; set.on = true; set.strictness = 0.25f; set.seed = 17;
        std::vector<std::vector<Ev>> vs; for (int v = 0; v < 6; ++v) { set.variation = v; vs.push_back(linear::render(*S, P, 140, 0, 16, set)); }
        int differ = 0, pairs = 0; for (int a = 0; a < 6; ++a) for (int b = a + 1; b < 6; ++b) { ++pairs; if (!sameEvents(vs[(size_t) a], vs[(size_t) b])) ++differ; }
        set.variation = 3; const bool repeat = sameEvents(vs[3], linear::render(*S, P, 140, 0, 16, set));
        set.seed = 18; const bool otherSeed = !sameEvents(vs[3], linear::render(*S, P, 140, 0, 16, set));
        std::printf("  6 variations: %d / %d pairs differ; same variation + seed repeats: %d; another seed differs: %d\n", differ, pairs, (int) repeat, (int) otherSeed);
        CHECK(differ == pairs && repeat && otherSeed, "variations");
    }
    section("LINEAR: anchors (accents + downbeats) and LOCK lanes survive");
    {
        auto S = groove(); EngineParams P; P.bpm = 140; const auto before = raw(*S, P, 140, 0, 16);
        linear::Settings set; set.on = true; set.rule = linear::RuleAccents; set.strictness = 0.4f; set.move = false;
        auto v = linear::render(*S, P, 140, 0, 16, set);
        int kicksOnBeat = 0; for (auto& e : v) if (e.track == 0 && std::abs(e.beat - std::round(e.beat)) < 1e-9) ++kicksOnBeat;
        set.lock = 1 << 2; set.rule = linear::RulePriority; auto vl = linear::render(*S, P, 140, 0, 16, set);
        bool hatsExact = countTrack(vl, 2) == countTrack(before, 2); { std::vector<double> a, b; for (auto& e : vl) if (e.track == 2) a.push_back(e.beat); for (auto& e : before) if (e.track == 2) b.push_back(e.beat); hatsExact = hatsExact && a == b; }
        set.lock = (1 << 2) | (1 << 0); auto v2 = linear::render(*S, P, 140, 0, 16, set);
        std::printf("  KEEP ACCENTS: %d / 16 accented kicks on the beat kept | LOCK hats: %d / %d hats kept at their exact times; LOCK kick+hat: kick %d hat %d, others overlap them: %d\n",
                    kicksOnBeat, countTrack(vl, 2), countTrack(before, 2), countTrack(v2, 0), countTrack(v2, 2), overlaps(v2, linear::windowBeats(set), set.lock));
        CHECK(kicksOnBeat == 16 && hatsExact && countTrack(v2, 0) == 16 && countTrack(v2, 2) == 64 && overlaps(v2, linear::windowBeats(set), set.lock) == 0, "anchors + locks");
    }
    section("LINEAR: MOVE keeps every hit, DROP removes the losers");
    {
        auto S = baseSnap(); auto& p = S->patterns[0]; p.length = 16; p.res = 1;   // kick 4/4 + 8th hats: room for every loser on the free 16ths
        for (int s = 0; s < 16; s += 4) p.steps[0][(size_t) s].vel = 110; for (int s = 0; s < 16; s += 2) p.steps[2][(size_t) s].vel = 90; for (int s : { 4, 12 }) p.steps[1][(size_t) s].vel = 100;
        EngineParams P; P.bpm = 140; const int n0 = (int) raw(*S, P, 140, 0, 4).size();   // one period (the stats count one loop)
        linear::Settings set; set.on = true; set.strictness = 0.25f; set.move = true; linear::Stats sm, sd;
        const auto mv = linear::render(*S, P, 140, 0, 4, set, false, &sm); set.move = false; const auto dr = linear::render(*S, P, 140, 0, 4, set, false, &sd);
        std::printf("  original %d hits | MOVE %d (moved %d) | DROP %d (dropped %d)\n", n0, (int) mv.size(), sm.moved, (int) dr.size(), sd.dropped);
        CHECK((int) mv.size() == n0 && sm.moved > 0 && (int) dr.size() < n0 && sd.dropped == n0 - (int) dr.size() && sd.dropped == sm.moved, "move / drop");
    }
    section("LINEAR: retrigs, rolls and flams are hits too");
    {
        auto S = baseSnap(); auto& p = S->patterns[0]; p.length = 16; p.res = 1;
        for (int s = 0; s < 16; ++s) p.steps[2][(size_t) s].vel = 80;                                  // 16th hats
        auto& sn = p.steps[1][4]; sn.vel = 110; sn.roll = 4; sn.rate = 16;                              // a 1/64 retrig x4 on the snare
        auto& rl = p.steps[4][10]; rl.vel = 100; rl.roll = 3;                                           // a triplet roll inside the step
        auto& kf = p.steps[0][8]; kf.vel = 120; kf.flam = 20;                                           // a flam on the kick
        EngineParams P; P.bpm = 120; const auto rw = raw(*S, P, 120, 0, 4);
        linear::Settings set; set.on = true; set.strictness = 0.2f; set.move = false; set.order[1] = 1; set.order[4] = 2; set.order[0] = 3; set.order[2] = 4;
        linear::Stats st; const auto v = linear::render(*S, P, 120, 0, 4, set, false, &st);
        const double w = linear::windowBeats(set);
        std::printf("  raw hits %d (16 hats + 4 retrigs + 3 roll + 2 flam) -> linear stats in %d; snare retrigs kept %d / 4, roll %d / 3, flam grace + kick %d / 2; overlaps %d\n",
                    (int) rw.size(), st.in, countTrack(v, 1), countTrack(v, 4), countTrack(v, 0), overlaps(v, w, 0));
        CHECK(rw.size() == 25 && st.in == 25 && countTrack(v, 1) == 4 && countTrack(v, 4) == 3 && countTrack(v, 0) == 2 && overlaps(v, w, 0) == 0, "retrig / roll / flam counted");
    }
    section("LINEAR: off = collectEvents() bit for bit; the pattern is never edited");
    {
        juce::Random r(5); int exact = 0;
        for (int it = 0; it < 20; ++it) {
            auto S = baseSnap(); randomPattern(S->patterns[0], r, true); EngineParams P; P.bpm = 133; P.chaos = it % 2 ? 0.3f : 0.0f;
            const auto copy = std::make_unique<std::array<Pattern, kPatterns>>(S->patterns);
            struct E { int t; double b; float v; EvInfo e; };
            std::vector<E> a, b, c;
            collectEvents(*S, P, 133, 0, 16, [&](int t, double bb, float v, const EvInfo& e) { a.push_back({ t, bb, v, e }); });
            linear::Settings set; set.on = true; set.variation = 2; linear::Player pl; attach(*S, P, 133, set);
            pl.process(*S, P, 133, 0, 16, [&](int, double, float, const EvInfo&) {});
            set.on = false; attach(*S, P, 133, set);   // toggle back
            pl.process(*S, P, 133, 0, 16, [&](int t, double bb, float v, const EvInfo& e) { b.push_back({ t, bb, v, e }); });
            linear::collect(*S, P, 133, 0, 16, set, [&](int t, double bb, float v, const EvInfo& e) { c.push_back({ t, bb, v, e }); });
            bool same = a.size() == b.size() && a.size() == c.size();
            auto eq = [](const EvInfo& x, const EvInfo& y) { return x.pitch == y.pitch && x.decay == y.decay && x.offset == y.offset && x.rateMul == y.rateMul && x.gate == y.gate && x.slice == y.slice
                                                              && x.variant == y.variant && x.reverse == y.reverse && x.crush == y.crush && x.pan == y.pan && x.filt == y.filt; };
            for (size_t i = 0; same && i < a.size(); ++i) same = a[i].t == b[i].t && a[i].b == b[i].b && a[i].v == b[i].v && eq(a[i].e, b[i].e) && a[i].t == c[i].t && a[i].b == c[i].b && a[i].v == c[i].v && eq(a[i].e, c[i].e);
            same = same && std::memcmp(copy->data(), S->patterns.data(), sizeof(Pattern) * kPatterns) == 0;
            if (same) ++exact;
        }
        std::printf("  %d / 20: LINEAR on, then off -> identical hits, order and every EvInfo field to collectEvents(); steps untouched\n", exact); CHECK(exact == 20, "toggle back bit-exact");
    }
    section("LINEAR: settings state round trip + the preview data");
    {
        linear::Settings s; s.on = true; s.rule = linear::RuleGroove; s.strictness = 0.6f; s.move = false; s.lock = 0x8005; s.variation = 7; s.seed = 99; s.order[3] = 1; s.order[0] = 2;
        const auto back = linear::fromTree(juce::ValueTree::fromXml(*linear::toTree(s).createXml()));
        const auto none = linear::fromTree(juce::ValueTree());
        auto S = groove(); EngineParams P; P.bpm = 140; linear::Settings on; on.on = true; on.move = false; on.strictness = 0.25f;
        const auto perf = linear::build(*S, P, 140, on); const auto v = linear::render(*perf, 0, 4, true); int kept = 0, dropped = 0; for (auto& e : v) (e.flags & linear::FlagDropped ? dropped : kept)++;
        std::printf("  round trip equal %d, missing child = off %d | preview of the loop: %d play, %d shown as dropped (stats kept %d dropped %d); window text at 25%%, 140 bpm: %s\n",
                    (int) (back == s), (int) !none.on, kept, dropped, perf->st.kept, perf->st.dropped, linear::windowText(on, 140).toRawUTF8());
        CHECK(back == s && !none.on && kept == perf->st.kept + perf->st.moved && dropped == perf->st.dropped && dropped > 0, "state + preview");
    }
}

// ---------------------------------------------------------------- MELODY helpers
static constexpr double SR = 48000;
static gb::SoundPtr sineSound(double f, double secs, double rate = SR, float amp = 0.5f) {
    auto s = std::make_shared<Sound>(); s->name = "sine"; s->rate = rate; s->data.resize((size_t) (secs * rate));
    for (size_t i = 0; i < s->data.size(); ++i) s->data[i] = amp * (float) std::sin(juce::MathConstants<double>::twoPi * f * (double) i / rate);
    return s;
}
// frequency from positive-going zero crossings in [a, b)
static double f0(const float* x, int a, int b, double sr = SR) {
    double first = -1, last = -1; int n = 0;
    for (int i = juce::jmax(1, a); i < b; ++i) if (x[i - 1] < 0 && x[i] >= 0) { const double t = i - 1 + x[i - 1] / (x[i - 1] - x[i]); if (first < 0) first = t; last = t; ++n; }
    return n >= 2 ? (n - 1) * sr / (last - first) : 0.0;
}
static double semis(double f, double ref) { return 12.0 * std::log2(f / ref); }
// least-squares slope (semitones per sample) of the per-cycle pitch between samples a and b
static double slope(const float* x, double a, double b, double ref) {
    std::vector<double> zc; for (int i = juce::jmax(1, (int) a); i < (int) b; ++i) if (x[i - 1] < 0 && x[i] >= 0) zc.push_back(i - 1 + x[i - 1] / (x[i - 1] - x[i]));
    double sx = 0, sy = 0, sxx = 0, sxy = 0; int n = 0;
    for (size_t i = 1; i < zc.size(); ++i) { const double t = 0.5 * (zc[i] + zc[i - 1]), y = semis(SR / (zc[i] - zc[i - 1]), ref); sx += t; sy += y; sxx += t * t; sxy += t * y; ++n; }
    return n > 2 ? (n * sxy - sx * sy) / (n * sxx - sx * sx) : 0.0;
}
static mel::Song oneInst(int root = 60) { mel::Song s; s.on = true; s.inst[0].sound = "snd"; s.inst[0].route = 0; s.inst[0].root = root; s.inst[0].vol = 1.0f; return s; }
static void put(mel::Song& s, int ch, int row, int note, int fx = 0, int param = 0, int vol = mel::kNoVol, int inst = 1) {
    auto& c = s.pat(0).cells[(size_t) ch][(size_t) row]; c.note = (uint8_t) (note < 0 ? 0 : (note == mel::kNoteOff ? mel::kNoteOff : note + 1)); c.inst = (uint8_t) (note > 0 && note != mel::kNoteOff ? inst : 0); c.fx = (uint8_t) fx; c.param = (uint8_t) param; c.vol = (uint8_t) vol;
}
static juce::AudioBuffer<float> play(const mel::Song& s, const Sound* snd, double beats, double bpm = 120, double tail = 0.2) { const Sound* sn[mel::kInstruments] {}; for (auto& x : sn) x = snd; return mel::renderAudio(s, sn, nullptr, false, 0, SR, bpm, beats, tail); }
static double tickSamples(const mel::Song& s, double bpm = 120) { return s.tickBeats() * 60.0 / bpm * SR; }

static void melodyTests() {
    const double C4 = 261.6255653;
    section("MELODY: C-4 on an instrument rooted at C-4 plays the sound unpitched (exact copy)");
    {
        auto snd = sineSound(C4, 2); auto s = oneInst(); put(s, 0, 0, 60); s.pat(0).rows = 64;
        auto a = play(s, snd.get(), 2.0); const float* L = a.getReadPointer(0);
        const float g = std::cos(juce::MathConstants<float>::pi * 0.25f);   // centre pan
        double err = 0; for (int i = 0; i < 40000; ++i) err = juce::jmax(err, (double) std::abs(L[i] - snd->data[(size_t) i] * g));
        const double f = f0(L, 2000, 40000);
        std::printf("  max |out - sound x gain| over 40000 samples = %.2e; f0 %.3f Hz (sound %.3f)\n", err, f, C4);
        CHECK(err < 1e-6 && std::abs(f - C4) < 0.02, "unpitched");
    }
    section("MELODY: +12 = one octave, multi-octave range (measured f0)");
    {
        auto snd = sineSound(C4, 3); double worst = 0; juce::String row;
        for (int n : { 36, 48, 60, 72, 84, 96 }) {
            auto s = oneInst(); put(s, 0, 0, n); auto a = play(s, snd.get(), 1.0); const double f = f0(a.getReadPointer(0), 2000, 20000);
            const double cents = 1200 * std::log2(f / (C4 * std::exp2((n - 60) / 12.0))); worst = juce::jmax(worst, std::abs(cents)); row << juce::String(f, 2) << " ";
        }
        std::printf("  C-2..C-7 f0: %s Hz; worst error %.3f cents\n", row.toRawUTF8(), worst); CHECK(worst < 0.5, "octaves");
    }
    section("MELODY: portamento rate (1xx / 2xx / 3xx)");
    {
        auto snd = sineSound(C4, 4); auto s = oneInst(); s.speed = 6; s.lpb = 4;   // tick = 1/24 beat = 20.83 ms at 120
        put(s, 0, 0, 48, mel::FxSlideUp, 0x08); for (int r = 1; r < 6; ++r) put(s, 0, r, -1, mel::FxSlideUp, 0x08);   // +0.5 st per tick for 6 rows
        auto a = play(s, snd.get(), 1.5); const float* L = a.getReadPointer(0); const double ts = tickSamples(s);
        const double rate = slope(L, 2 * ts, 34 * ts, C4) * ts;   // semitones per tick
        auto s2 = oneInst(); put(s2, 0, 0, 60); put(s2, 0, 1, 72, mel::FxGlide, 0x20); for (int r = 2; r < 8; ++r) put(s2, 0, r, -1, mel::FxGlide, 0);   // glide up an octave at 2 st / tick
        auto b = play(s2, snd.get(), 2.0); const float* M = b.getReadPointer(0);
        const double grate = slope(M, 6.3 * ts, 11.7 * ts, C4) * ts, gEnd = f0(M, (int) (20 * ts), (int) (30 * ts));
        auto s3 = oneInst(); put(s3, 0, 0, 72, mel::FxSlideDown, 0x10); for (int r = 1; r < 4; ++r) put(s3, 0, r, -1, mel::FxSlideDown, 0x10);
        auto c3 = play(s3, snd.get(), 1.0); const double drate = slope(c3.getReadPointer(0), 2 * ts, 22 * ts, C4) * ts;
        std::printf("  1-08: %.4f st / tick (want +0.5) | 2-10: %.4f (want -1.0) | 3-20 glide: %.4f st / tick (want 2), lands on %.2f Hz (C-5 = %.2f)\n", rate, drate, grate, gEnd, C4 * 2);
        CHECK(std::abs(rate - 0.5) < 0.005 && std::abs(drate + 1.0) < 0.01 && std::abs(grate - 2.0) < 0.02 && std::abs(gEnd - C4 * 2) < 0.3, "portamento");
    }
    section("MELODY: arpeggio steps per tick (0xy)");
    {
        auto snd = sineSound(C4, 3); auto s = oneInst(); put(s, 0, 0, 60, mel::FxArp, 0x47); for (int r = 1; r < 4; ++r) put(s, 0, r, -1, mel::FxArp, 0x47);
        auto a = play(s, snd.get(), 1.0); const float* L = a.getReadPointer(0); const double ts = tickSamples(s); int good = 0; juce::String got;
        for (int k = 0; k < 18; ++k) { const double f = f0(L, (int) ((k + 0.15) * ts), (int) ((k + 0.95) * ts)); const double want = (k % 3 == 0 ? 0 : (k % 3 == 1 ? 4 : 7)); const double st = semis(f, C4);
            if (std::abs(st - want) < 0.1) ++good; if (k < 6) got << juce::String(st, 2) << " "; }
        std::printf("  first ticks (semitones): %s ... %d / 18 ticks on 0 / +4 / +7\n", got.toRawUTF8(), good); CHECK(good == 18, "arpeggio");
    }
    section("MELODY: vibrato depth + rate (4xy)");
    {
        auto snd = sineSound(C4, 4); auto s = oneInst(); put(s, 0, 0, 60, mel::FxVibrato, 0x88); for (int r = 1; r < 32; ++r) put(s, 0, r, -1, mel::FxVibrato, 0);
        auto a = play(s, snd.get(), 8.0); const float* L = a.getReadPointer(0);
        // per-cycle frequency trace
        std::vector<double> zc; for (int i = 1; i < (int) (3.5 * SR); ++i) if (L[i - 1] < 0 && L[i] >= 0) zc.push_back(i - 1 + L[i - 1] / (L[i - 1] - L[i]));
        double lo = 1e9, hi = -1e9; std::vector<double> tr; for (size_t i = 1; i < zc.size(); ++i) { const double st = semis(SR / (zc[i] - zc[i - 1]), C4); tr.push_back(st); if (zc[i] > 0.2 * SR) { lo = juce::jmin(lo, st); hi = juce::jmax(hi, st); } }
        std::vector<double> ups; for (size_t i = 2; i < tr.size(); ++i) if (tr[i - 1] < 0 && tr[i] >= 0 && zc[i] > 0.2 * SR) { const double u = tr[i - 1] / (tr[i - 1] - tr[i]); ups.push_back(0.5 * (zc[i - 1] + zc[i - 2]) + u * 0.5 * (zc[i] - zc[i - 2])); }
        const double depth = (hi - lo) / 2, rateHz = ups.size() > 2 ? (ups.size() - 1) * SR / (ups.back() - ups.front()) : 0.0;   // 8/64 cycle per tick, 48 ticks/s -> 6 Hz
        std::printf("  4-88: depth %.3f st (want 1.000), rate %.3f Hz (want 6.000)\n", depth, rateHz);
        CHECK(std::abs(depth - 1.0) < 0.03 && std::abs(rateHz - 6.0) < 0.05, "vibrato");
    }
    section("MELODY: retrig, note cut, note delay, sample offset, volume slide");
    {
        auto snd = sineSound(C4, 3); const double ts = tickSamples(oneInst());
        auto s = oneInst(); put(s, 0, 0, 60, mel::FxRetrig, 0x03); for (int r = 1; r < 4; ++r) put(s, 0, r, -1, mel::FxRetrig, 0x03);
        const Sound* sn[mel::kInstruments] {}; for (auto& x : sn) x = snd.get();
        auto pl = std::make_unique<mel::Player>(); pl->prepare(SR); juce::AudioBuffer<float> buf(2, 512);
        const double spb = SR * 60 / 120; for (int pos = 0; pos < (int) (spb * 1.0); pos += 512) { buf.clear(); float* lr[kTracks * 2]; for (int t = 0; t < kTracks; ++t) { lr[t * 2] = buf.getWritePointer(0); lr[t * 2 + 1] = buf.getWritePointer(1); } pl->process(s, sn, nullptr, nullptr, nullptr, false, 0, pos / spb, spb, 512, lr, true); }
        const auto mf = mel::toMidi(s, nullptr, false, 0, 120, 1.0); int midiNotes = 0; for (int t = 0; t < mf.getNumTracks(); ++t) for (auto* e : *mf.getTrack(t)) if (e->message.isNoteOn()) ++midiNotes;
        auto c = oneInst(); put(c, 0, 0, 60, mel::FxCut, 0x03); auto ac = play(c, snd.get(), 0.5); const float* L = ac.getReadPointer(0);
        const float before = ac.getMagnitude(0, (int) (1 * ts), (int) (1.5 * ts)), after = ac.getMagnitude(0, (int) (3 * ts + 0.0025 * SR), (int) (5 * ts));
        auto d = oneInst(); put(d, 0, 0, 60, mel::FxDelay, 0x02); auto ad = play(d, snd.get(), 0.5); int onset = -1; for (int i = 0; i < ad.getNumSamples(); ++i) if (std::abs(ad.getSample(0, i)) > 1e-6f) { onset = i; break; }
        auto o = oneInst(); put(o, 0, 0, 60, mel::FxOffset, 0x80); auto ao = play(o, snd.get(), 0.5);
        const float g = std::cos(juce::MathConstants<float>::pi * 0.25f); double oerr = 0; const int half = (int) snd->data.size() / 2;
        for (int i = 200; i < 4000; ++i) oerr = juce::jmax(oerr, (double) std::abs(ao.getSample(0, i) - snd->data[(size_t) (half + i)] * g));
        auto v = oneInst(); put(v, 0, 0, 60, mel::FxVolSlide, 0x04, 0x40); for (int r = 1; r < 4; ++r) put(v, 0, r, -1, mel::FxVolSlide, 0x04);   // -4 / tick from 64: silent after 16 ticks
        auto av = play(v, snd.get(), 1.0); const float v0 = av.getMagnitude(0, 0, (int) ts), v8 = av.getMagnitude(0, (int) (8 * ts), (int) ts), v17 = av.getMagnitude(0, (int) (17 * ts), (int) (2 * ts));
        std::printf("  Q-03 over 4 rows: %u voice starts, %d MIDI notes (want 5) | C-03: level %.3f -> %.5f after the 2 ms fade | D-02: onset at sample %d (want %d)\n",
                    (unsigned) pl->triggers, midiNotes, before, after, onset, (int) std::ceil(2 * ts));
        std::printf("  9-80: max error vs the sound from its middle %.2e | A-04: level %.3f -> %.3f (tick 8, want ~%.3f) -> %.5f (tick 17)\n", oerr, v0, v8, v0 * 0.5, v17); juce::ignoreUnused(L);
        CHECK(pl->triggers == 5 && midiNotes == 5, "retrig count");
        CHECK(before > 0.3f && after < 1e-4f, "note cut");
        CHECK(std::abs(onset - (int) std::ceil(2 * ts)) <= 1, "note delay");
        CHECK(oerr < 1e-6, "sample offset");
        CHECK(v8 < v0 * 0.62f && v8 > v0 * 0.38f && v17 < 1e-4f, "volume slide");
    }
    section("MELODY: aliasing when pitching up 12 st (windowed sinc, cutoff follows the pitch)");
    {
        double worst = -999; juce::String row;
        for (double f : { 13000.0, 15000.0, 18000.0, 20000.0, 22000.0 }) {
            auto snd = sineSound(f, 1.5); auto s = oneInst(); put(s, 0, 0, 72); auto a = play(s, snd.get(), 1.0);
            const double out = a.getRMSLevel(0, 4000, 20000), in = 0.5 / std::sqrt(2.0) * std::cos(juce::MathConstants<double>::pi * 0.25);
            const double db = juce::Decibels::gainToDecibels(out / in, -200.0); worst = juce::jmax(worst, db); row << juce::String((int) f / 1000) << "k: " << juce::String(db, 1) << " dB  ";
        }
        double worstMid = -999; juce::String rowMid;   // between the octaves (+7, +11: the full sound read with the pitch-following kernel)
        for (int st : { 3, 7, 11 }) for (double f : { 15000.0, 16500.0, 18000.0, 20000.0, 22000.0 }) { const double out2 = f * std::exp2(st / 12.0); if (48000.0 - out2 > 20000.0 || out2 < 24000.0) continue;   // aliases that would land in the audible band (< 20 kHz)
            auto snd2 = sineSound(f, 1.5); auto s2 = oneInst(); put(s2, 0, 0, 60 + st); auto a2 = play(s2, snd2.get(), 1.0);
            const double db = juce::Decibels::gainToDecibels(a2.getRMSLevel(0, 4000, 20000) / (0.5 / std::sqrt(2.0) * std::cos(juce::MathConstants<double>::pi * 0.25)), -200.0); worstMid = juce::jmax(worstMid, db); rowMid << "+" << st << " " << juce::String((int) f / 100 / 10.0, 1) << "k: " << juce::String(db, 1) << "  "; }
        std::printf("  between octaves (tones whose alias would land below 20 kHz): %s\n", rowMid.toRawUTF8()); CHECK(worstMid < -60, "aliasing between octaves");
        auto snd = sineSound(5000, 1.5); auto s = oneInst(); put(s, 0, 0, 72); auto a = play(s, snd.get(), 1.0);
        const double pass = juce::Decibels::gainToDecibels(a.getRMSLevel(0, 4000, 20000) / (0.5 / std::sqrt(2.0) * std::cos(juce::MathConstants<double>::pi * 0.25)));
        const double pf = f0(a.getReadPointer(0), 4000, 20000);
        std::printf("  alias level (all of it, the tone itself is above Nyquist): %s\n  passband: 5 kHz -> %.1f Hz at %.2f dB\n", row.toRawUTF8(), pf, pass);
        CHECK(worst < -60 && std::abs(pass) < 0.3 && std::abs(pf - 10000) < 2, "aliasing");
    }
    section("MELODY: MIDI export follows the audio (notes, pitch bend = measured pitch)");
    {
        auto snd = sineSound(C4, 6); auto s = oneInst(); s.speed = 6;
        put(s, 0, 0, 60); put(s, 0, 4, 60, mel::FxSlideUp, 0x06); for (int r = 5; r < 8; ++r) put(s, 0, r, -1, mel::FxSlideUp, 0x06);
        put(s, 0, 8, 67, mel::FxVibrato, 0x6C); for (int r = 9; r < 16; ++r) put(s, 0, r, -1, mel::FxVibrato, 0);
        put(s, 0, 16, 62, mel::FxArp, 0x37); for (int r = 17; r < 20; ++r) put(s, 0, r, -1, mel::FxArp, 0x37);
        put(s, 0, 20, 69, mel::FxGlide, 0); put(s, 0, 21, 74, mel::FxGlide, 0x10); for (int r = 22; r < 28; ++r) put(s, 0, r, -1, mel::FxGlide, 0); put(s, 0, 28, mel::kNoteOff);
        const double beats = 8.0; auto a = play(s, snd.get(), beats); const float* L = a.getReadPointer(0);
        const auto mf = mel::toMidi(s, nullptr, false, 0, 120, beats, 24); const auto* tr = mf.getTrack(0);
        auto midiPitch = [&](double beat, bool& on) {
            const double tk = beat * 960; int note = -1, bend = 8192; on = false;
            for (auto* e : *tr) { if (e->message.getTimeStamp() > tk + 1e-6) break; const auto& m = e->message; if (m.isNoteOn()) { note = m.getNoteNumber(); on = true; } else if (m.isNoteOff()) on = false; else if (m.isPitchWheel()) bend = m.getPitchWheelValue(); }
            return note + (bend - 8192) / 8191.0 * 24.0;
        };
        int checked = 0; double worst = 0;
        const double ts = tickSamples(s); const double tb = s.tickBeats();
        for (int k = 2; k < 28 * 6 - 1; ++k) {   // mid-tick, over ~4 ms (a few cycles)
            const double beat = (k + 0.5) * tb; bool on = false; const double mp = midiPitch(beat, on); if (!on) continue;
            const int c = (int) ((k + 0.5) * ts); const double f = f0(L, c - 220, c + 220); if (f <= 0) continue;
            const double d = std::abs(semis(f, C4) + 60 - mp) * 100; worst = juce::jmax(worst, d); ++checked;
        }
        int notes = 0, bends = 0, rpn = 0; for (auto* e : *tr) { if (e->message.isNoteOn()) ++notes; if (e->message.isPitchWheel()) ++bends; if (e->message.isController() && e->message.getControllerNumber() == 6 && e->message.getControllerValue() == 24) ++rpn; }
        std::printf("  %d mid-tick points: MIDI note + bend vs measured audio pitch, worst %.1f cents | %d notes, %d bends, bend range RPN sent %d\n", checked, worst, notes, bends, rpn);
        CHECK(checked > 100 && worst < 15 && rpn == 1 && notes == 3 + 24 + 1, "MIDI export matches");   // C, C slide, G vib, 24 arp ticks, back to the base note
    }
    section("MELODY: block size independence, NaN fuzz, state round trip");
    {
        auto snd = sineSound(C4, 2); juce::Random r(42); mel::Song s; s.on = true;
        for (int i = 0; i < 4; ++i) { s.inst[(size_t) i].sound = "snd"; s.inst[(size_t) i].route = i; s.inst[(size_t) i].root = 48 + 12 * (i % 2); s.inst[(size_t) i].loop = i % 2 == 1; s.inst[(size_t) i].tune = (float) (i * 13 - 20); s.inst[(size_t) i].pan = 0.3f * (float) (i - 2); }
        s.pat(0).rows = 32; s.speed = 3 + r.nextInt(6); s.lpb = 4;
        for (int c = 0; c < mel::kChannels; ++c) for (int row = 0; row < 32; ++row) if (r.nextFloat() < 0.6f) {
            auto& cell = s.pat(0).cells[(size_t) c][(size_t) row]; const float q = r.nextFloat();
            cell.note = (uint8_t) (q < 0.08f ? mel::kNoteOff : (q < 0.6f ? 13 + r.nextInt(100) : 0)); cell.inst = (uint8_t) (r.nextInt(5)); cell.vol = (uint8_t) (r.nextFloat() < 0.3f ? r.nextInt(65) : mel::kNoVol);
            cell.fx = (uint8_t) r.nextInt(mel::FxCount); cell.param = (uint8_t) r.nextInt(256);
        }
        const Sound* sn[mel::kInstruments] {}; for (auto& x : sn) x = snd.get();
        auto a = mel::renderAudio(s, sn, nullptr, false, 0, SR, 137, 16, 0.5);
        bool finite = true; float peak = 0; for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < a.getNumSamples(); ++i) { const float x = a.getSample(ch, i); if (!std::isfinite(x)) finite = false; peak = juce::jmax(peak, std::abs(x)); }
        // the same through odd blocks
        auto pl = std::make_unique<mel::Player>(); pl->prepare(SR); juce::AudioBuffer<float> b(2, a.getNumSamples()); b.clear(); const double spb = SR * 60 / 137; const int loop = (int) std::ceil(16 * spb);
        const auto fm = mel::Mips::make(*snd); const mel::Mips* fmp[mel::kInstruments] {}; for (auto& x : fmp) x = fm.get(); s.prepare();   // (what renderAudio does)
        for (int pos = 0; pos < loop;) { const int n = juce::jmin(loop - pos, 1 + r.nextInt(900)); float* lr[kTracks * 2]; for (int t = 0; t < kTracks; ++t) { lr[t * 2] = b.getWritePointer(0, pos); lr[t * 2 + 1] = b.getWritePointer(1, pos); }
            pl->process(s, sn, fmp, nullptr, nullptr, false, 0, pos / spb, spb, n, lr, true); pos += n; }
        double diff = 0; for (int i = 0; i < loop - 10; ++i) diff = juce::jmax(diff, (double) std::abs(a.getSample(0, i) - b.getSample(0, i)));
        mel::Song back; mel::fromTree(back, juce::ValueTree::fromXml(*mel::toTree(s).createXml()));
        const bool same = std::memcmp(back.pat(0).cells.data(), s.pat(0).cells.data(), sizeof(s.pat(0).cells)) == 0 && back.speed == s.speed && back.inst[3].loop && back.inst[1].tune == s.inst[1].tune && back.pat(0).rows == 32;
        std::printf("  fuzz (random notes / effects / params / loops, 8 channels): finite %d, peak %.2f | random blocks vs 512: max diff %.2e | state round trip %d\n", (int) finite, peak, diff, (int) same);
        CHECK(finite && peak < 8 && diff < 1e-5 && same, "fuzz / blocks / state");
    }
    section("MELODY major 9 + minor 23: CPU per voice at any pitch (mip levels), nothing aliases even at +60 st");
    {
        auto snd = std::make_shared<Sound>(); snd->rate = 44100; snd->data.resize(44100 * 3); snd->right.resize(snd->data.size());   // a stereo 44.1k sound: always resampled
        for (size_t i = 0; i < snd->data.size(); ++i) { snd->data[i] = (float) std::sin(i * 0.031) * 0.4f; snd->right[i] = (float) std::sin(i * 0.029) * 0.4f; }
        const auto mips = mel::Mips::make(*snd); const mel::Mips* mp[mel::kInstruments] {}; for (auto& x : mp) x = mips.get();
        const Sound* sn[mel::kInstruments] {}; for (auto& x : sn) x = snd.get();
        auto perVoice = [&](int note) {   // one voice, 10 s, % of one core
            mel::Song s; s.on = true; s.inst[0].sound = "snd"; s.inst[0].route = 0; s.inst[0].loop = true; s.inst[0].root = 60;
            auto& x = s.pat(0).cells[0][0]; x.note = (uint8_t) (note + 1); x.inst = 1; x.fx = mel::FxVibrato; x.param = 0x42; for (int r = 1; r < 64; ++r) { s.pat(0).cells[0][(size_t) r].fx = mel::FxVibrato; }
            auto pl = std::make_unique<mel::Player>(); pl->prepare(SR); juce::AudioBuffer<float> buf(2, 512); const double spb = SR * 60 / 128; const int total = (int) (SR * 10);
            const double t0 = juce::Time::getMillisecondCounterHiRes();
            for (int pos = 0; pos < total; pos += 512) { buf.clear(); float* lr[kTracks * 2]; for (int t = 0; t < kTracks; ++t) { lr[t * 2] = buf.getWritePointer(0); lr[t * 2 + 1] = buf.getWritePointer(1); } pl->process(s, sn, mp, nullptr, nullptr, false, 0, pos / spb, spb, 512, lr, true); }
            return (juce::Time::getMillisecondCounterHiRes() - t0) / 10000.0 * 100.0;
        };
        double worst = 0; juce::String row; for (int st : { 0, 7, 12, 24, 36, 48 }) { const double c = perVoice(60 + st); worst = juce::jmax(worst, c); row << "+" << st << ": " << juce::String(c, 2) << "%  "; }
        // 8 channels, as before (+1..+2 octaves, vibrato, loops), no allocation
        mel::Song s; s.on = true; s.inst[0].sound = "snd"; s.inst[0].route = 0; s.inst[0].loop = true;
        for (int c = 0; c < mel::kChannels; ++c) for (int r = 0; r < 64; r += 8) { auto& x = s.pat(0).cells[(size_t) c][(size_t) r]; x.note = (uint8_t) (73 + c * 2); x.inst = 1; x.fx = mel::FxVibrato; x.param = 0x46; }
        auto pl = std::make_unique<mel::Player>(); pl->prepare(SR); juce::AudioBuffer<float> buf(2, 512); const double spb = SR * 60 / 128; const int total = (int) (SR * 20);
        gRtAllocs = 0; gRtCount = true; const double t0 = juce::Time::getMillisecondCounterHiRes();
        for (int pos = 0; pos < total; pos += 512) { buf.clear(); float* lr[kTracks * 2]; for (int t = 0; t < kTracks; ++t) { lr[t * 2] = buf.getWritePointer(0); lr[t * 2 + 1] = buf.getWritePointer(1); } pl->process(s, sn, mp, nullptr, nullptr, false, 0, pos / spb, spb, 512, lr, true); }
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0; gRtCount = false;
        // aliasing far up: 1 kHz at +48 (16 kHz, real) and +60 (32 kHz: must be gone), +72
        auto lvl = [&](int st) { auto tone = sineSound(1000, 1.5); auto s2 = oneInst(); s2.inst[0].loop = true; s2.inst[0].loopStart = 0.1f; s2.inst[0].loopEnd = 0.9f; put(s2, 0, 0, 60 + st); auto a = play(s2, tone.get(), 1.0); return juce::Decibels::gainToDecibels(a.getRMSLevel(0, 4000, 20000) / (0.5 / std::sqrt(2.0) * std::cos(juce::MathConstants<double>::pi * 0.25)), -200.0); };
        const double l48 = lvl(48), l60 = lvl(60), l72 = lvl(72);
        std::printf("  one voice (stereo 44.1k, vibrato, loop): %s worst %.2f%% | 8 channels +1..+2 oct: %.1f%% of a core; allocations %ld\n  1 kHz at +48 st: %.1f dB (16 kHz, real) | +60 st: %.1f dB | +72 st: %.1f dB (above Nyquist: silent)\n",
                    row.toRawUTF8(), worst, ms / 200.0, gRtAllocs.load(), l48, l60, l72);
        CHECK(gRtAllocs.load() == 0 && (sanitized() || (worst < 1.0 && ms / 20000.0 < 0.12)), "melody CPU");
        CHECK(std::abs(l48) < 1.0 && l60 < -60 && l72 < -60, "no aliasing far up");
    }
}

static void melodyPunchTests() {
    const double C4 = 261.6255653;
    section("MELODY major 8: the sustain loop seam is click-free at rate 1 (note == ROOT, sound rate == host rate)");
    {
        auto snd = sineSound(110.0, 3.0); auto s = oneInst(45); s.inst[0].loop = true; s.inst[0].loopStart = 0.4f; s.inst[0].loopEnd = 0.9f; put(s, 0, 0, 45);
        auto a = play(s, snd.get(), 12.0, 120, 0.1); const float* L = a.getReadPointer(0); double worst = 0, clean = 0; int at = 0;
        for (int i = 4000; i < (int) (5.5 * SR); ++i) { const double d = std::abs(L[i] - L[i - 1]); if (d > worst) { worst = d; at = i; } }
        for (int i = 4000; i < 40000; ++i) clean = juce::jmax(clean, (double) std::abs(L[i] - L[i - 1]));
        std::printf("  110 Hz at its root, loop 40-90%%, 5.5 s (several wraps): largest sample step %.4f at %.3f s (the tone itself: %.4f)\n", worst, at / SR, clean);
        CHECK(worst < clean * 1.15, "loop seam at rate 1");
    }
    section("MELODY minors 25 + 26 + effect memory: 300 glides with no setup, C00 is silent, 00 = the last value");
    {
        auto snd = sineSound(C4, 4); const double ts = tickSamples(oneInst());
        auto g = oneInst(); put(g, 0, 0, 60); put(g, 0, 1, 72, mel::FxGlide, 0); for (int r = 2; r < 8; ++r) put(g, 0, r, -1, mel::FxGlide, 0);
        auto ag = play(g, snd.get(), 2.0); const double gEnd = f0(ag.getReadPointer(0), (int) (30 * ts), (int) (40 * ts));
        auto c = oneInst(); put(c, 0, 0, 60, mel::FxCut, 0); auto ac = play(c, snd.get(), 0.5); const float cutPeak = ac.getMagnitude(0, 0, ac.getNumSamples());
        auto m = oneInst(); put(m, 0, 0, 48, mel::FxSlideUp, 0x08); for (int r = 1; r < 4; ++r) put(m, 0, r, -1, mel::FxSlideUp, 0x00);   // 100 = "the last 1xx" = 108
        auto am = play(m, snd.get(), 1.0); const double rate = slope(am.getReadPointer(0), 2 * ts, 22 * ts, C4) * ts;
        std::printf("  300 after C-4 -> C-5 lands on %.2f Hz (C-5 = %.2f) | C00 on a note row: peak %.6f | 108 then 100 x3: %.4f st / tick all the way (want 0.5)\n", gEnd, C4 * 2, cutPeak, rate);
        CHECK(std::abs(gEnd - C4 * 2) < 0.5 && cutPeak == 0.0f && std::abs(rate - 0.5) < 0.01, "glide default + C00 + memory");
    }
    section("MELODY: Fxx speed, OWN ORDER with Bxx jump, REVERSE, CUTOFF, TREMOLO, PAN SLIDE, FINE slides, ADSR");
    {
        auto snd = sineSound(C4, 4); const double rowS = SR * 60 / 120 / 4;
        // F03: 3 ticks per row from row 0 -> an arpeggio steps every 1/3 row
        auto f = oneInst(); put(f, 1, 0, -1, mel::FxSpeed, 0x03); put(f, 0, 0, 60, mel::FxArp, 0x47); for (int r = 1; r < 4; ++r) put(f, 0, r, -1, mel::FxArp, 0x47);
        auto af = play(f, snd.get(), 1.0); int ok = 0; for (int k = 0; k < 9; ++k) { const double tk = rowS / 3; const double st2 = semis(f0(af.getReadPointer(0), (int) ((k + 0.2) * tk), (int) ((k + 0.9) * tk)), C4); if (std::abs(st2 - (k % 3 == 0 ? 0 : k % 3 == 1 ? 4 : 7)) < 0.15) ++ok; }
        // OWN ORDER: A (2 rows: C-4, then B01 jump) -> order position 1 = pattern C (2 rows: G-4) -> back to A ...
        mel::Song o = oneInst(); o.ownOrder = true; o.order = { 0, 2, 1 }; o.pat(0).rows = 2; o.pat(2).rows = 2; o.pat(1).rows = 2;
        { auto& a0 = o.pat(0).cells[0][0]; a0.note = 61; a0.inst = 1; o.pat(0).cells[1][1].fx = mel::FxJump; o.pat(0).cells[1][1].param = 2;   // A row 1: jump to position 2 (pattern B)
          auto& b0 = o.pat(1).cells[0][0]; b0.note = 68; b0.inst = 1; auto& c0 = o.pat(2).cells[0][0]; c0.note = 65; c0.inst = 1; }
        o.prepare(); std::vector<int> seq; for (long r = 0; r < 8; ++r) { mel::TickPos tp; mel::tickAt(o, nullptr, false, 0, r * o.rowBeats() + 1e-6, tp); seq.push_back(tp.pat); }
        juce::String seqS; for (int x : seq) seqS << juce::String::charToString((juce::juce_wchar) ('A' + x));
        // REVERSE: R01 on a rising ramp plays it falling
        auto ramp = std::make_shared<Sound>(); ramp->rate = SR; ramp->data.resize((size_t) SR); for (size_t i = 0; i < ramp->data.size(); ++i) ramp->data[i] = (float) i / (float) SR * 0.8f;
        auto rv = oneInst(); put(rv, 0, 0, 60, mel::FxReverse, 0x01); auto ar = play(rv, ramp.get(), 1.0); const float r1 = ar.getSample(0, 2000), r2 = ar.getSample(0, 8000);
        // CUTOFF: Z20 on a 5 kHz tone
        auto hi = sineSound(5000, 2); auto zf = oneInst(); put(zf, 0, 0, 60, mel::FxCutoff, 0x20); auto az = play(zf, hi.get(), 1.0); auto zo = oneInst(); put(zo, 0, 0, 60); auto azo = play(zo, hi.get(), 1.0);
        const double zdb = juce::Decibels::gainToDecibels(az.getRMSLevel(0, 8000, 10000) / azo.getRMSLevel(0, 8000, 10000));
        // TREMOLO 7 8F: depth 1 -> the level dips to ~0 every 8 ticks
        auto tr = oneInst(); put(tr, 0, 0, 60, mel::FxTremolo, 0x8F); for (int r = 1; r < 8; ++r) put(tr, 0, r, -1, mel::FxTremolo, 0);
        auto at2 = play(tr, snd.get(), 2.0); float tmin = 1e9f, tmax = 0; for (int w = 0; w < 30; ++w) { const float m = at2.getMagnitude(0, 4000 + w * 400, 400); tmin = juce::jmin(tmin, m); tmax = juce::jmax(tmax, m); }
        // PAN SLIDE P40: right 4/64 per tick from the centre -> after 16 ticks fully right
        auto ps = oneInst(); put(ps, 0, 0, 60, mel::FxPanSlide, 0x40); for (int r = 1; r < 4; ++r) put(ps, 0, r, -1, mel::FxPanSlide, 0x40);
        auto ap = play(ps, snd.get(), 1.0); const double tsP = tickSamples(oneInst()); const float lEnd = ap.getMagnitude(0, (int) (20 * tsP), (int) tsP), rEnd = ap.getMagnitude(1, (int) (20 * tsP), (int) tsP);
        // FINE: E18 = +0.5 st once
        auto fn = oneInst(); put(fn, 0, 0, 60, mel::FxFine, 0x18); auto afn = play(fn, snd.get(), 1.0); const double fine = semis(f0(afn.getReadPointer(0), 2000, 20000), C4);
        // ADSR: attack 50 ms, decay 100 ms to 50 %, release 200 ms after ===
        auto en = oneInst(); en.inst[0].attack = 0.05f; en.inst[0].decay = 0.1f; en.inst[0].sustain = 0.5f; en.inst[0].release = 0.2f; put(en, 0, 0, 60); put(en, 0, 8, mel::kNoteOff);
        auto ae = play(en, snd.get(), 4.0); const float e10 = ae.getMagnitude(0, (int) (0.010 * SR), 200), e50 = ae.getMagnitude(0, (int) (0.050 * SR), 400), e300 = ae.getMagnitude(0, (int) (0.3 * SR), 400);
        const float eRel = ae.getMagnitude(0, (int) (1.0 * SR + 0.1 * SR), 400), eGone = ae.getMagnitude(0, (int) (1.0 * SR + 0.25 * SR), 400);
        std::printf("  F03 arp steps on time: %d / 9 | OWN ORDER {A,C,B}, B02 on A's last row: %s (want AABBAABB: C skipped) | R01 on a rising ramp: %.3f then %.3f (falling) | Z20: %.1f dB on 5 kHz\n", ok, seqS.toRawUTF8(), r1, r2, zdb);
        std::printf("  7 8F tremolo: level %.3f .. %.3f | P40 after 20 ticks: L %.3f R %.3f | E18: %+.3f st | ADSR: 10 ms %.2f, 50 ms %.2f, 300 ms %.2f (sustain 50%%), release +100 ms %.2f, +250 ms %.3f\n", tmin, tmax, lEnd, rEnd, fine, e10, e50, e300, eRel, eGone);
        CHECK(ok >= 8, "Fxx speed"); CHECK(seqS == "AABBAABB", "own order + jump"); CHECK(r1 > r2 + 0.07f && r1 > 0.5f, "reverse"); CHECK(zdb < -20, "cutoff");
        CHECK(tmin < tmax * 0.15f, "tremolo"); CHECK(lEnd < 0.02f && rEnd > 0.3f, "pan slide"); CHECK(std::abs(fine - 0.5) < 0.02, "fine slide");
        const float full = 0.5f * std::cos(juce::MathConstants<float>::pi * 0.25f);
        CHECK(e10 < full * 0.35f && e50 > full * 0.85f && std::abs(e300 - full * 0.5f) < full * 0.06f && eRel > 0.05f * full && eRel < full * 0.5f && eGone < full * 0.01f, "ADSR");
    }
    section("MELODY minors 17 + 18 + major 11: stop / MELODY off release with RELEASE; audition is pitched and never sequenced");
    {
        auto snd = sineSound(C4, 4); const Sound* sn[mel::kInstruments] {}; for (auto& x : sn) x = snd.get();
        auto s = oneInst(); s.inst[0].release = 0.25f; s.inst[0].loop = true; put(s, 0, 0, 60);
        auto pl = std::make_unique<mel::Player>(); pl->prepare(SR); const double spb = SR * 60 / 120; juce::AudioBuffer<float> b(2, 512);
        auto run = [&](double beat, bool running, mel::Song& so) { b.clear(); float* lr[kTracks * 2]; for (int t = 0; t < kTracks; ++t) { lr[t * 2] = b.getWritePointer(0); lr[t * 2 + 1] = b.getWritePointer(1); } pl->process(so, sn, nullptr, nullptr, nullptr, false, 0, beat, spb, 512, lr, running); return b.getMagnitude(0, 0, 512); };
        double beat = 0; for (int i = 0; i < 40; ++i) { run(beat, true, s); beat += 512 / spb; }
        const float playing = run(beat, true, s); float after100 = 0, after400 = 0; double t = 0; float worstStep = 0;
        for (int i = 0; i < 60; ++i) { const float m = run(beat, false, s); for (int k = 1; k < 512; ++k) worstStep = juce::jmax(worstStep, std::abs(b.getSample(0, k) - b.getSample(0, k - 1))); t += 512 / SR; if (t > 0.09 && t < 0.11) after100 = m; if (t > 0.4 && t < 0.42) after400 = m; }
        // MELODY off while ringing: the same release (no freeze, no click)
        auto pl2 = std::make_unique<mel::Player>(); pl2->prepare(SR); std::swap(pl, pl2); beat = 0; for (int i = 0; i < 40; ++i) { run(beat, true, s); beat += 512 / spb; }
        s.on = false; float off100 = 0; t = 0; for (int i = 0; i < 20; ++i) { const float m = run(beat, true, s); beat += 512 / spb; t += 512 / SR; if (t > 0.09 && t < 0.11) off100 = m; }
        const int voicesAfter = pl->activeVoices();
        // audition: +7 st on a stopped player, through the instrument, 300 ms
        auto pl3 = std::make_unique<mel::Player>(); pl3->prepare(SR); std::swap(pl, pl3); s.on = true; s.pat(0).cells[0][0] = {};
        pl->audition(0, 67); juce::AudioBuffer<float> rec(2, (int) (0.3 * SR)); rec.clear();
        for (int pos = 0; pos < rec.getNumSamples(); pos += 512) { const int n = juce::jmin(512, rec.getNumSamples() - pos); float* lr[kTracks * 2]; for (int tt = 0; tt < kTracks; ++tt) { lr[tt * 2] = rec.getWritePointer(0, pos); lr[tt * 2 + 1] = rec.getWritePointer(1, pos); } pl->process(s, sn, nullptr, nullptr, nullptr, false, 0, 0, spb, n, lr, false); }
        const double af = f0(rec.getReadPointer(0), 2000, rec.getNumSamples() - 100);
        std::printf("  stop with RELEASE 250 ms: %.3f playing -> %.3f after 100 ms -> %.4f after 400 ms, largest step %.4f | MELODY off: %.3f after 100 ms, voices left %d | audition G-4: %.2f Hz (want %.2f)\n",
                    playing, after100, after400, worstStep, off100, voicesAfter, af, C4 * std::exp2(7 / 12.0));
        CHECK(after100 > playing * 0.4f && after100 < playing * 0.8f && after400 < 1e-4f && worstStep < 0.05f, "stop releases");
        CHECK(off100 > 0.1f * playing && off100 < playing * 0.8f && voicesAfter <= 1, "MELODY off releases");
        CHECK(std::abs(af - C4 * std::exp2(7 / 12.0)) < 0.5, "audition pitched");
    }
    section("MELODY minor 33 + 31: instrument VOL in the MIDI velocity; the song hash follows the fields");
    {
        auto s = oneInst(); s.inst[0].vol = 0.5f; put(s, 0, 0, 60); const auto mf = mel::toMidi(s, nullptr, false, 0, 120, 1.0); int vel = -1; for (auto* e : *mf.getTrack(0)) if (e->message.isNoteOn()) vel = e->message.getVelocity();
        auto h = [](const mel::Song& x) { juce::uint64 hh = 1469598103934665603ULL; mel::hashInto(x, [&](const void* d, size_t n) { const auto* b = static_cast<const uint8_t*>(d); for (size_t i = 0; i < n; ++i) hh = (hh ^ b[i]) * 1099511628211ULL; }); return hh; };
        auto s2 = s; const bool same = h(s) == h(s2); s2.inst[0].release = 0.4f; const bool differs = h(s) != h(s2);
        std::printf("  VOL 50%% at volume 64: velocity %d (want 64) | hash: copies equal %d, release change differs %d\n", vel, (int) same, (int) differs);
        CHECK(std::abs(vel - 64) <= 1 && same && differs, "velocity + hash");
    }
    section("MELODY state: own samples, envelopes, order list survive save / load");
    {
        auto s = oneInst(); s.inst[0].sample = sineSound(220, 0.3); s.inst[0].sound = {}; s.inst[0].attack = 0.02f; s.inst[0].sustain = 0.6f; s.inst[0].cutoff = 0.4f; s.inst[0].name = "Sub";
        s.ownOrder = true; s.order = { 1, 0, 1, 3 }; put(s, 0, 0, 60, mel::FxTremolo, 0x42);
        juce::MemoryOutputStream mo; mel::toTree(s).writeToStream(mo); mel::Song back; mel::fromTree(back, juce::ValueTree::readFromData(mo.getData(), mo.getDataSize()), SR);
        const bool ok = back.inst[0].sample && std::abs((int) back.inst[0].sample->data.size() - (int) s.inst[0].sample->data.size()) <= 1 && back.inst[0].attack == s.inst[0].attack && back.inst[0].sustain == s.inst[0].sustain
                        && back.inst[0].cutoff == s.inst[0].cutoff && back.inst[0].name == "Sub" && back.ownOrder && back.order == s.order && back.pat(0).cells[0][0].fx == mel::FxTremolo;
        std::printf("  own sample (%d samples), ADSR, cutoff, name, OWN ORDER {1,0,1,3}, tremolo cell: round trip %d\n", (int) s.inst[0].sample->data.size(), (int) ok);
        CHECK(ok, "melody state");
    }
}

// ---------------------------------------------------------------- screenshots
static void writePng(const juce::Image& img, const juce::File& f) { f.deleteFile(); juce::PNGImageFormat png; juce::FileOutputStream o(f); if (o.openedOk()) png.writeImageToStream(img, o); std::printf("  %s\n", f.getFullPathName().toRawUTF8()); }
static mel::Song demoSong();
static void shots(const juce::File& dir) {
    section("screenshots"); dir.createDirectory();
    for (int dark = 0; dark < 2; ++dark) {
        auto song = demoSong(); bx::MelodyTrackerView v; v.song = &song; v.trackName = [](int t) { static const char* n[] { "Kick", "Snare", "Hat", "Glass Pluck", "Sub Saw", "Bell" }; return juce::String(t < 6 ? n[t] : "Perc"); };
        if (dark) { v.look = { juce::Colour(0xff141621), juce::Colour(0xff1b1e2a), juce::Colour(0xfff2f4f5), juce::Colour(0xff9ca3af), juce::Colour(0xff4b5563), juce::Colour(0xffA06BFF),
                               juce::Colour(0xff3CFF8F), juce::Colour(0xfffbbf24), juce::Colour(0xfff472b6), juce::Colour(0xff22d3ee), juce::Colour(0xff60a5fa) }; }
        v.playRow = [] { return 9; }; v.setSize(1100, 600); v.row = 4; v.chan = 1; v.field = 3; v.inst = 1;
        juce::Image img(juce::Image::ARGB, 1100, 600, true); { juce::Graphics g(img); g.fillAll(dark ? juce::Colour(0xff0B0C10) : juce::Colour(0xffF3F4F8)); v.paintEntireComponent(g, true); }
        writePng(img, dir.getChildFile(juce::String("melody-tracker-") + (dark ? "dark" : "light") + ".png"));
        v.help = true; juce::Image h(juce::Image::ARGB, 1100, 600, true); { juce::Graphics g(h); g.fillAll(juce::Colour(0xffF3F4F8)); v.paintEntireComponent(g, true); }
        if (!dark) writePng(h, dir.getChildFile("melody-tracker-help.png"));
    }
    // LINEAR: the groove before / after + the VARIATION preview strips (what the LINEAR panel's strip draws)
    {
        auto S = baseSnap(); auto& p = S->patterns[0]; p.length = 16;
        for (int s : { 0, 6, 8, 11 }) p.steps[0][(size_t) s].vel = 127; for (int s : { 4, 12 }) p.steps[1][(size_t) s].vel = 115; p.steps[1][14].vel = 60;
        for (int s = 0; s < 16; s += 2) p.steps[2][(size_t) s].vel = 100; p.steps[2][14].roll = 3; for (int s : { 2, 10 }) p.steps[3][(size_t) s].vel = 90; p.steps[4][6].vel = 85; p.steps[4][6].flam = 25; p.steps[4][13].vel = 80;
        EngineParams P; P.bpm = 140; const int W = 1100, Hh = 520; juce::Image img(juce::Image::ARGB, W, Hh, true); juce::Graphics g(img); g.fillAll(juce::Colour(0xffF3F4F8));
        bx::MelLook L; const char* names[] { "KICK", "SNARE", "HAT", "OPEN HAT", "PERC" };
        auto lanes = [&](const std::vector<Ev>& v, juce::Rectangle<int> r, const juce::String& title) {
            g.setColour(L.card); g.fillRoundedRectangle(r.toFloat(), 8); g.setColour(L.ink); g.setFont(juce::FontOptions(13.0f, juce::Font::bold)); g.drawText(title, r.removeFromTop(22).reduced(10, 0), juce::Justification::centredLeft);
            auto body = r.reduced(10, 6); const int lw = 80, lh = body.getHeight() / 5;
            for (int t = 0; t < 5; ++t) { auto lr = body.withY(body.getY() + t * lh).withHeight(lh); g.setColour(L.grey); g.setFont(juce::FontOptions(10.5f, juce::Font::bold)); g.drawText(names[t], lr.removeFromLeft(lw), juce::Justification::centredLeft);
                g.setColour(L.ink.withAlpha(0.05f)); g.fillRect(lr.reduced(0, 2)); for (int b = 0; b <= 16; ++b) { g.setColour(L.ink.withAlpha(b % 4 == 0 ? 0.25f : 0.08f)); g.drawVerticalLine(lr.getX() + b * lr.getWidth() / 16, (float) lr.getY(), (float) lr.getBottom()); } }
            const float x0 = (float) body.getX() + lw, wb = (float) (body.getWidth() - lw) / 4.0f;
            for (auto& e : v) { if (e.track > 4) continue; const float x = x0 + (float) e.beat * wb; const float y = (float) body.getY() + e.track * lh + 3;
                g.setColour((e.flags & linear::FlagDropped) ? L.dim : L.track(e.track)); if (e.flags & linear::FlagDropped) g.drawRect(x - 3, y, 6.0f, lh - 6.0f, 1.0f); else g.fillRoundedRectangle(x - 3, y, 6.0f, lh - 6.0f, 2);
                if (e.flags & linear::FlagMoved) { g.setColour(L.ink); g.drawRoundedRectangle(x - 4, y - 1, 8.0f, lh - 4.0f, 2, 1.2f); g.setColour(L.ink.withAlpha(0.3f)); g.drawLine(x0 + (float) e.orig * wb, y + lh * 0.5f - 3, x, y + lh * 0.5f - 3, 1.0f); } }
        };
        const auto before = raw(*S, P, 140, 0, 4); linear::Settings set; set.on = true; set.strictness = 0.25f; set.move = true;
        const auto after = linear::render(*S, P, 140, 0, 4, set, true);
        lanes(before, { 10, 10, W - 20, 200 }, "ORIGINAL (" + juce::String((int) before.size()) + " hits, " + juce::String(overlaps(before, linear::windowBeats(set), 0)) + " collisions)");
        lanes(after, { 10, 220, W - 20, 200 }, "LINEAR  priority order, MOVE, strictness 25%  (moved hits outlined, the line shows where they came from)");
        g.setColour(L.ink); g.setFont(juce::FontOptions(12.0f, juce::Font::bold)); g.drawText("VARIATION  <  >   preview strips (one bar, 1/64 cells)", 14, 428, 600, 18, juce::Justification::centredLeft);
        for (int vi = 0; vi < 4; ++vi) { set.variation = vi; set.move = vi % 2 == 0; const auto pf = linear::build(*S, P, 140, set); const auto pv = linear::render(*pf, 0, 4, true); linear::Strip st; for (auto& e : pv) { const int c = juce::jlimit(0, 63, (int) std::floor((e.flags & linear::FlagDropped ? e.orig : e.beat) * 16 + 1e-6)); if (e.flags & linear::FlagDropped) { if (st.droppedTrack[(size_t) c] < 0) st.droppedTrack[(size_t) c] = e.track; } else if (st.track[(size_t) c] < 0) { st.track[(size_t) c] = e.track; st.flags[(size_t) c] = e.flags; } } const int y = 450 + (vi / 2) * 32, x = 14 + (vi % 2) * 545;
            g.setColour(L.grey); g.setFont(juce::FontOptions(10.5f, juce::Font::bold)); g.drawText("VAR " + juce::String(vi) + (set.move ? " MOVE" : " DROP"), x, y, 90, 24, juce::Justification::centredLeft);
            for (int c = 0; c < 64; ++c) { const auto cell = juce::Rectangle<float>((float) (x + 92 + c * 7), (float) y + 2, 6, 20);
                g.setColour(L.card2); g.fillRect(cell); if (st.track[(size_t) c] >= 0) { g.setColour(L.track(st.track[(size_t) c])); g.fillRect(cell); if (st.flags[(size_t) c] & linear::FlagMoved) { g.setColour(L.ink); g.drawRect(cell, 1.0f); } }
                else if (st.droppedTrack[(size_t) c] >= 0) { g.setColour(L.track(st.droppedTrack[(size_t) c]).withAlpha(0.3f)); g.fillRect(cell.reduced(0, 7)); } } }
        writePng(img, dir.getChildFile("linear-before-after.png"));
    }
}

// ---------------------------------------------------------------- demos
static void writeWav(const juce::AudioBuffer<float>& b, const juce::File& f, double sr = SR) {
    f.deleteFile(); juce::WavAudioFormat wav; auto os = std::unique_ptr<juce::OutputStream>(f.createOutputStream());
    if (os) if (auto* w = wav.createWriterFor(os.get(), sr, 2, 24, {}, 0)) { os.release(); std::unique_ptr<juce::AudioFormatWriter> wr(w); wr->writeFromAudioSampleBuffer(b, 0, b.getNumSamples()); }
}
static void toMp3(const juce::File& wav, const juce::File& mp3) { mp3.deleteFile(); juce::ChildProcess cp; cp.start(juce::StringArray { "ffmpeg", "-loglevel", "error", "-y", "-i", wav.getFullPathName(), "-codec:a", "libmp3lame", "-b:a", "192k", mp3.getFullPathName() }); cp.waitForProcessToFinish(60000); wav.deleteFile(); std::printf("  %s\n", mp3.getFullPathName().toRawUTF8()); }
static void normalise(juce::AudioBuffer<float>& b, float peakDb = -1.0f) { const float m = b.getMagnitude(0, b.getNumSamples()); if (m > 1e-6f) b.applyGain(juce::Decibels::decibelsToGain(peakDb) / m); }

// a synthesised instrument: band-limited saw + square blend, gentle low-pass decay, 2.5 s, rooted at C-4, loop in the sustain
static gb::SoundPtr synthPluck(double f = 261.6255653, double secs = 2.5, int flavour = 0) {
    auto s = std::make_shared<Sound>(); s->name = "Glass Pluck"; s->rate = SR; const int n = (int) (secs * SR); s->data.resize((size_t) n); s->right.resize((size_t) n);
    for (int i = 0; i < n; ++i) {
        const double t = i / SR; double a = 0, b = 0;
        for (int k = 1; k * f < 18000; ++k) {
            const double bright = std::exp(-t * (flavour == 1 ? 1.2 : 3.5) * k * 0.35);   // higher partials decay faster
            const double amp = (flavour == 1 ? 1.0 / k : (k % 2 ? 1.0 / k : 0.35 / k)) * bright;
            a += amp * std::sin(juce::MathConstants<double>::twoPi * k * f * t); b += amp * std::sin(juce::MathConstants<double>::twoPi * k * f * 1.003 * t + k);
        }
        const double env = (1 - std::exp(-t * 400)) * (flavour == 1 ? (0.75 + 0.25 * std::exp(-t * 2)) : (0.55 + 0.45 * std::exp(-t * 4)));
        s->data[(size_t) i] = (float) (a * env * 0.3); s->right[(size_t) i] = (float) (b * env * 0.3);
    }
    return s;
}
static mel::Song demoSong() {
    mel::Song s; s.on = true; s.lpb = 4; s.speed = 6;
    s.inst[0] = {}; s.inst[0].sound = "snd"; s.inst[0].route = 3; s.inst[0].root = 60; s.inst[0].vol = 0.9f; s.inst[0].loop = true; s.inst[0].loopStart = 0.45f; s.inst[0].loopEnd = 0.95f; s.inst[0].release = 0.12f; s.inst[0].pan = -0.15f;
    s.inst[1] = s.inst[0]; s.inst[1].sound = "snd"; s.inst[1].route = 4; s.inst[1].pan = 0.0f; s.inst[1].vol = 0.8f; s.inst[1].release = 0.05f;
    s.inst[2] = s.inst[0]; s.inst[2].sound = "snd"; s.inst[2].route = 5; s.inst[2].pan = 0.3f; s.inst[2].vol = 0.55f; s.inst[2].tune = 5;
    auto& p = s.pat(0); p.rows = 64;
    auto set = [&](int ch, int row, const char* note, int inst, int fx = 0, int param = 0, int vol = mel::kNoVol) {
        auto& c = p.cells[(size_t) ch][(size_t) row];
        if (note) { const juce::String n(note); if (n == "===") c.note = mel::kNoteOff; else { static const char* names[] { "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-" }; int k = 0; for (int i = 0; i < 12; ++i) if (n.startsWith(names[i])) k = i; c.note = (uint8_t) ((n.getLastCharacters(1).getIntValue() + 1) * 12 + k + 1); c.inst = (uint8_t) inst; } }
        c.fx = (uint8_t) fx; c.param = (uint8_t) param; c.vol = (uint8_t) vol;
    };
    // ch 1: lead with glides + vibrato
    set(0, 0, "E-5", 1, mel::FxVibrato, 0x00, 0x30); set(0, 2, nullptr, 0, mel::FxVibrato, 0x64); set(0, 3, nullptr, 0, mel::FxVibrato, 0);
    set(0, 4, "G-5", 1, mel::FxGlide, 0x30); set(0, 5, nullptr, 0, mel::FxGlide, 0); set(0, 6, "B-5", 1); set(0, 8, "A-5", 1, mel::FxVibrato, 0x54); for (int r = 9; r < 14; ++r) set(0, r, nullptr, 0, mel::FxVibrato, 0);
    set(0, 14, nullptr, 0, mel::FxSlideDown, 0x0C); set(0, 15, nullptr, 0, mel::FxSlideDown, 0x0C); set(0, 16, "===", 0);
    set(0, 18, "E-5", 1, 0, 0, 0x2C); set(0, 20, "D-5", 1, mel::FxGlide, 0x40); set(0, 22, "E-5", 1, mel::FxRetrig, 0x93); set(0, 23, nullptr, 0, mel::FxRetrig, 0x93); set(0, 24, "G-5", 1, mel::FxVibrato, 0x66); for (int r = 25; r < 30; ++r) set(0, r, nullptr, 0, mel::FxVibrato, 0); set(0, 30, "===", 0);
    for (int r = 32; r < 64; ++r) p.cells[0][(size_t) r] = p.cells[0][(size_t) (r - 32)];
    set(0, 56, "B-5", 1, mel::FxSlideUp, 0x04); set(0, 57, nullptr, 0, mel::FxSlideUp, 0x04); set(0, 58, "C-6", 1, mel::FxVibrato, 0x88); for (int r = 59; r < 63; ++r) set(0, r, nullptr, 0, mel::FxVibrato, 0); set(0, 63, nullptr, 0, mel::FxCut, 3);
    // ch 2: arpeggiated chords
    const char* roots[] { "A-3", "F-3", "C-4", "G-3" }; const int arps[] { 0x37, 0x47, 0x47, 0x47 };
    for (int b = 0; b < 4; ++b) for (int r = 0; r < 16; ++r) set(1, b * 16 + r, r == 0 ? roots[b] : nullptr, 2, mel::FxArp, arps[b], r == 0 ? 0x26 : mel::kNoVol);
    for (int b = 0; b < 4; ++b) set(1, b * 16 + 14, nullptr, 0, mel::FxVolSlide, 0x03);
    // ch 3: sub line with glides + delays
    const char* bass[] { "A-2", "A-2", "F-2", "F-2", "C-3", "C-3", "G-2", "E-2" };
    for (int i = 0; i < 8; ++i) { set(2, i * 8, bass[i], 3, i % 2 ? mel::FxGlide : 0, 0x18, 0x34); set(2, i * 8 + 3, i % 2 ? nullptr : bass[i], 3, mel::FxDelay, 0x03); set(2, i * 8 + 6, "===", 0); }
    return s;
}
static void demos(const juce::File& dir) {
    section("demos"); dir.createDirectory();
    auto lib = loadBeatsLibrary(SR);
    auto pick = [&](std::initializer_list<const char*> names) -> SoundPtr { for (auto* n : names) if (auto s = lib->byName(n)) return s; return lib->sounds()[0]; };
    // the groove: 5 lanes that collide a lot (IDM-ish: 16th hats with rolls, a flam, a retrig, ghost snares)
    auto S = baseSnap(); const SoundPtr snds[] { pick({ "Punch Kick", "Hard Kick" }), pick({ "Trap Snare", "Snare" }), pick({ "Trap Hat", "Hat" }), pick({ "Open Hat" }), pick({ "Rim", "Perc Pop" }) };
    for (int t = 0; t < 5; ++t) { S->sound[(size_t) t] = snds[t].get(); S->tracks[(size_t) t].vol = t == 2 ? 0.55f : 0.8f; }
    S->tracks[3].choke = S->tracks[2].choke = 1;
    auto& p = S->patterns[0]; p.length = 16; p.res = 1;
    for (int s : { 0, 6, 10 }) p.steps[0][(size_t) s].vel = 127; p.steps[0][14].vel = 90;
    for (int s : { 4, 12 }) p.steps[1][(size_t) s].vel = 118; for (int s : { 7, 9, 15 }) p.steps[1][(size_t) s].vel = 50; p.steps[1][12].flam = 22;
    for (int s = 0; s < 16; ++s) p.steps[2][(size_t) s].vel = (uint8_t) (s % 4 == 0 ? 110 : (s % 2 ? 62 : 85)); p.steps[2][11].roll = 3; p.steps[2][15].roll = 4; p.steps[2][15].rate = 16;
    for (int s : { 2, 10 }) p.steps[3][(size_t) s].vel = 92;
    for (int s : { 3, 6, 11, 13 }) p.steps[4][(size_t) s].vel = 88; p.steps[4][13].roll = 2;
    p.evolve = 0.35f; p.seed = 9;
    EngineParams P; P.bpm = 140; P.limiter = 0; P.ceiling = -1; P.volume = 0.8f; P.verb = 0.08f; P.glue = 0.2f;
    const double bars = 8, beats = bars * 4, spb = SR * 60 / P.bpm;
    auto renderHits = [&](const std::vector<Ev>& ev) {   // the real engine, hits fed as sample-exact MIDI notes 36.. (= tracks 1..)
        BeatEngine e; e.prepare(SR, 512); e.setSnapshot(S.get());
        const int total = (int) (beats * spb + SR * 1.5); juce::AudioBuffer<float> out(2, total), buf(2, 512); size_t k = 0;
        for (int pos = 0; pos < total; pos += 512) { const int n = juce::jmin(512, total - pos); buf.setSize(2, n, false, false, true); juce::MidiBuffer mb;
            while (k < ev.size() && ev[k].beat * spb < pos + n) { const int off = juce::jlimit(0, n - 1, (int) std::floor(ev[k].beat * spb) - pos); mb.addEvent(juce::MidiMessage::noteOn(1, 36 + ev[k].track, juce::jlimit(0.01f, 1.0f, ev[k].vel)), off); ++k; }
            e.process(buf, mb, P, P.bpm, 0, false); for (int c = 0; c < 2; ++c) out.copyFrom(c, pos, buf, c, 0, n); }
        return out;
    };
    const auto before = raw(*S, P, P.bpm, 0, beats);
    linear::Settings set; set.on = true; set.rule = linear::RuleAccents; set.strictness = 0.25f; set.move = true; set.seed = 3;
    const auto after = linear::render(*S, P, P.bpm, 0, beats, set);
    set.variation = 2; set.rule = linear::RuleGroove; const auto var2 = linear::render(*S, P, P.bpm, 0, beats, set);
    std::printf("  groove: %d hits, %d collisions -> LINEAR %d hits, %d collisions; variation 2 (groove rule) %d hits\n", (int) before.size(), overlaps(before, 0.0625, 0), (int) after.size(), overlaps(after, 0.0625, 0), (int) var2.size());
    { auto a = renderHits(before); auto f = dir.getChildFile("linear-1-before.wav"); writeWav(a, f); toMp3(f, dir.getChildFile("linear-1-before.mp3")); }
    { auto a = renderHits(after); auto f = dir.getChildFile("linear-2-after-accents-move.wav"); writeWav(a, f); toMp3(f, dir.getChildFile("linear-2-after-accents-move.mp3")); }
    { auto a = renderHits(var2); auto f = dir.getChildFile("linear-3-variation2-groove.wav"); writeWav(a, f); toMp3(f, dir.getChildFile("linear-3-variation2-groove.mp3")); }
    // the melody on synthesised instruments (+ the linear groove under it in the last file)
    auto song = demoSong(); auto lead = synthPluck(261.6255653, 2.5, 0), sub = synthPluck(261.6255653, 2.5, 1), bell = synthPluck(261.6255653 * 1.0, 2.0, 0);
    const Sound* sn[mel::kInstruments] {}; sn[0] = lead.get(); sn[1] = bell.get(); sn[2] = sub.get();
    const double mb = 16 * 4;   // 4 bars (one 64-row pattern), played twice
    auto m = mel::renderAudio(song, sn, nullptr, false, 0, SR, 128, mb * 2 / 4, 1.5);
    { auto mm = m; normalise(mm, -1.0f); auto f = dir.getChildFile("melody-1-portamento-arp-vibrato.wav"); writeWav(mm, f); toMp3(f, dir.getChildFile("melody-1-portamento-arp-vibrato.mp3")); }
    {   // with the linear groove (at 128)
        P.bpm = 128; const double spb2 = SR * 60 / 128; const auto lin = linear::render(*S, P, 128, 0, 32, set);
        BeatEngine e; e.prepare(SR, 512); e.setSnapshot(S.get()); const int total = m.getNumSamples(); juce::AudioBuffer<float> out(2, total), buf(2, 512); size_t k = 0;
        for (int pos = 0; pos < total; pos += 512) { const int n = juce::jmin(512, total - pos); buf.setSize(2, n, false, false, true); juce::MidiBuffer mbuf;
            while (k < lin.size() && lin[k].beat * spb2 < pos + n) { const int off = juce::jlimit(0, n - 1, (int) std::floor(lin[k].beat * spb2) - pos); mbuf.addEvent(juce::MidiMessage::noteOn(1, 36 + lin[k].track, juce::jlimit(0.01f, 1.0f, lin[k].vel * 0.8f)), off); ++k; }
            e.process(buf, mbuf, P, 128, 0, false); for (int c = 0; c < 2; ++c) out.copyFrom(c, pos, buf, c, 0, n); }
        for (int c = 0; c < 2; ++c) out.addFrom(c, 0, m, c, 0, total, 0.9f);
        normalise(out, -1.0f); auto f = dir.getChildFile("melody-2-with-linear-drums.wav"); writeWav(out, f); toMp3(f, dir.getChildFile("melody-2-with-linear-drums.mp3"));
    }
    { const auto mf = mel::toMidi(song, nullptr, false, 0, 128, 16); auto f = dir.getChildFile("melody-1.mid"); f.deleteFile(); juce::FileOutputStream o(f); if (o.openedOk()) mf.writeTo(o); std::printf("  %s\n", f.getFullPathName().toRawUTF8()); }
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::String mode = argc > 1 ? argv[1] : "test";
    if (mode == "test" || mode == "all") { linearTests(); melodyTests(); melodyPunchTests(); std::printf("\n%d passed, %d failed\n", passes, fails); }
    if (mode == "shots" && argc > 2) shots(juce::File(argv[2]));
    if (mode == "demos" && argc > 2) demos(juce::File(argv[2]));
    if (mode == "all") { if (argc > 2) shots(juce::File(argv[2])); if (argc > 3) demos(juce::File(argv[3])); }
    return fails == 0 ? 0 : 1;
}
