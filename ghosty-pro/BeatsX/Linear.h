#pragma once
// LINEAR DRUMMING (Ghosty Beats): only ONE voice strikes at any instant. A pure function on the performance (never on the
// pattern): every hit collectEvents() produces (retrigs, rolls, flams, EVOLVE, macros, rules, scripts, SONG blocks) is a
// candidate; at each collision inside the STRICTNESS window one voice wins by the chosen rule, the losers MOVE to the
// nearest free slot of their chunk (flams / rolls / retrig bursts move as one gesture) or DROP. LOCK lanes always survive.
// VARIATION 1.. gives seeded alternative versions. Off = collectEvents() exactly, so toggling back is bit-exact.
//
// THE PERFORMANCE (Perf) is resolved ONCE, off the audio thread, and carried by the snapshot:
//  - pattern mode: one period of the loop, CIRCULAR (the first beat sees the loop's own tail as context), so every loop is
//    identical, bar 1 == bar 3, and DRAG MIDI (one period) == what plays. The period is the loop of all tracks together (the
//    LCM of the track lengths, or the CONVERGENCE length); a beat that never repeats (EVOLVE, macros, fills, chance < 100 %,
//    humanize, glitch, destruction, rules, scripts) becomes a fixed performance of the EXPORT BARS length (Rhythm Lab
//    PERFORMANCE), which repeats - exactly what the drag-out holds.
//  - song mode: the whole song, start to end.
//  - it is split into chunks of ~one bar that follow the loop (a 7/16 loop is one 7/16 chunk, not cut at 4 beats); each chunk is
//    resolved from its raw hits plus one beat of context each side, then its head is checked against the previous chunk's tail.
//  - every seeded choice (VARIATION, Random, MOVE direction) hashes the position INSIDE the period, never the absolute beat.
//  - no fixed caps: a 16-track 1/64 beat with 16-hit rolls and flams keeps every hit it can (Stats::overflow stays 0).
// The audio thread only plays a Perf (binary search, no allocation). A new Perf (an edit, or a macro / swing / tempo change that
// alters the hits) switches in at the next bar line, so a bar that already started is never re-resolved mid-way.
//
// Wiring (done): BeatModel.h holds Settings in Model + Snapshot and `std::shared_ptr<const Perf> linPerf` + `uint64_t serial` in
// Snapshot; BeatsXProcessor::publish() builds the Perf (and a worker rebuilds it when event-relevant parameters change);
// BeatEngineHooks.h binds linear::Player into the engine; exportMidi() / renders use the snapshot's Perf (rebuilt when stale).
#include "LinearSettings.h"
#include "BeatEngine.h"   // (when BeatEngine.h includes this file mid-way, this line is a no-op and everything used below is already declared)
#include <memory>
#include <vector>

namespace gb::linear {

inline const juce::StringArray& ruleNames() { static juce::StringArray r { "Priority order", "Keep accents + downbeats", "Alternate", "Keep the groove", "Random (seeded)" }; return r; }
inline double windowBeats(const Settings& s) { return juce::jmax(1.0e-4, (double) juce::jlimit(0.0f, 1.0f, s.strictness) * 0.25); }
// the window in words: "1/64 = 27 ms" (for the STRICTNESS control)
juce::String windowText(const Settings& s, double bpm);

enum EvFlags : uint8_t { FlagMoved = 1, FlagLocked = 2, FlagDropped = 4 };
struct Ev { double beat = 0, orig = 0; float vel = 0; EvInfo e; int8_t track = 0; uint8_t flags = 0; };
struct Stats { int in = 0, kept = 0, moved = 0, dropped = 0, overflow = 0; };

// the EngineParams fields collectEvents() reads (everything else can change without touching the hits)
struct ParamKey {
    float swing = 0, human = 0, glitch = 0, chaos = 0, density = 0.5f, mutation = 0, fracture = 0, gravity = 0.5f, memory = 0.5f, tension = 0, resolve = 0;
    int groove = 0, pattern = 0; bool songMode = false;
    static ParamKey of(const EngineParams& P);
    bool operator==(const ParamKey& o) const;
};

struct Perf {
    uint64_t content = 0;      // Snapshot::serial it was built from
    Settings set; ParamKey key; double bpm = 0; bool bpmMatters = false; int horizonBars = 16;
    bool song = false, periodic = true; double span = 4; int chunks = 1;   // pattern: repeats every `span` beats; song: plays [0, span)
    std::vector<Ev> ev;        // what plays, sorted by beat
    std::vector<Ev> dropped;   // what LINEAR removed (orig = where it was), for the preview
    Stats st;
    double exportBeats() const { return span; }
};
using PerfPtr = std::shared_ptr<const Perf>;

// build the performance (message / worker thread; allocates). horizonBars = the EXPORT BARS length for beats that never repeat.
PerfPtr build(const Snapshot& S, const EngineParams& P, double bpm, const Settings& set, int horizonBars = 16);
// is `p` still what build() would return for these inputs?
bool fresh(const Perf* p, const Snapshot& S, const EngineParams& P, double bpm, const Settings& set, int horizonBars);
// does the tempo change the hits (flams in ms, track humanize, freeze / destruction gates, scripts)?
bool tempoMatters(const Snapshot& S, const EngineParams& P);

// play a Perf: emit(track, beat, vel, EvInfo) for every hit with beat in [b0, b1). Real-time safe.
template <class F>
inline void play(const Perf& p, double b0, double b1, F&& emit) {
    b0 = juce::jmax(0.0, b0); if (!(b1 > b0) || p.ev.empty()) return;
    auto run = [&](double x0, double x1, double shift) {
        auto it = std::lower_bound(p.ev.begin(), p.ev.end(), x0, [](const Ev& e, double b) { return e.beat < b; });
        for (; it != p.ev.end() && it->beat < x1; ++it) emit((int) it->track, it->beat + shift, it->vel, it->e);
    };
    if (p.song) { run(b0, juce::jmin(b1, p.span), 0.0); return; }
    const double L = p.span; const long q0 = (long) std::floor(b0 / L), q1 = (long) std::floor(b1 / L);
    for (long q = q0; q <= q1; ++q) { const double base = q * L; run(juce::jmax(b0, base) - base, juce::jmin(b1, base + L) - base, base); }
}

// The engine side (one per BeatEngine, made in prepare()): plays the snapshot's Perf; a new Perf takes over at the next bar line.
struct Player {
    const Perf* now = nullptr; const Perf* next = nullptr; double switchAt = 0, lastEnd = -1;
    template <class F>
    void process(const Snapshot& S, const EngineParams& P, double bpm, double b0, double b1, F&& emit) {
        const Perf* p = S.linPerf.get();
        if (std::abs(b0 - lastEnd) > 1e-6) now = next = nullptr;   // stop / jump / loop: the newest Perf at once (and an old one is never touched again)
        if (now && now != p && now != S.linPerfPrev.get()) now = next = nullptr;   // only finish a bar on a Perf this snapshot still owns
        lastEnd = b1;
        if (!S.linear.on || !p) { now = next = nullptr; collectEvents(S, P, bpm, b0, b1, emit); return; }
        if (!now) { now = p; next = nullptr; }
        else if (p != now && p != next) { next = p; switchAt = std::ceil(b0 / 4.0 - 1e-9) * 4.0; }   // (a bar that started keeps its hits)
        if (next && b1 > switchAt) {
            if (b0 < switchAt) play(*now, b0, switchAt, emit);
            now = next; next = nullptr; play(*now, juce::jmax(b0, switchAt), b1, emit); return;
        }
        play(*now, b0, b1, emit);
    }
    void reset() { now = next = nullptr; lastEnd = -1; }
};

// allocating convenience (tests, previews): build + play
template <class F>
inline void collect(const Snapshot& S, const EngineParams& P, double bpm, double b0, double b1, const Settings& set, F&& emit) {
    if (!set.on) { collectEvents(S, P, bpm, b0, b1, emit); return; }
    if (S.linPerf && fresh(S.linPerf.get(), S, P, bpm, set, S.linPerf->horizonBars)) { play(*S.linPerf, b0, b1, emit); return; }
    const auto p = build(S, P, bpm, set); play(*p, b0, b1, emit);
}

// every hit in [b0, b1) as a list (sorted), with what LINEAR did to it; withDropped adds the dropped ones (FlagDropped, at orig)
std::vector<Ev> render(const Snapshot& S, const EngineParams& P, double bpm, double b0, double b1, const Settings& set, bool withDropped = false, Stats* st = nullptr, int horizonBars = 16);
std::vector<Ev> render(const Perf& p, double b0, double b1, bool withDropped = false);

// the priority rank of a track (0 = wins first). An explicit order is a full permutation (normalised on load / edit).
int rankOf(const Settings& set, const Snapshot& S, int t);
int autoRank(uint8_t role, int t);
// make `order` a complete 1..16 permutation (explicit ranks kept in their order, the rest in automatic order after them)
void normaliseOrder(Settings& s, const std::array<uint8_t, 16>& roles);
// move track t to rank r (0-based) in the resolved order, rewriting all 16 ranks
void moveInOrder(Settings& s, const std::array<uint8_t, 16>& roles, int t, int r);
// the 16 tracks in resolved order (first = wins)
std::array<int, 16> resolvedOrder(const Settings& s, const std::array<uint8_t, 16>& roles);

// the panel's actions (pure, so they are testable): switching on seeds ONCE (never re-seeds), VARIATION steps keep the seed
inline void setOn(Settings& s, bool on, int toolSeed) { s.on = on; if (on && s.seed == 0) s.seed = juce::jmax(1, toolSeed); }
inline bool stepVariation(Settings& s, int d) { const int v = juce::jlimit(0, 99, s.variation + d); if (v == s.variation) return false; s.variation = v; if (s.seed == 0) s.seed = 1; return true; }

juce::ValueTree toTree(const Settings& s);
Settings fromTree(const juce::ValueTree& t);

} // namespace gb::linear
