#pragma once
// Binds the engine's LINEAR DRUMMING + MELODIC TRACKER hooks (see EngineHooks in BeatEngine.h). Include this (instead of
// BeatEngine.h) from every translation unit of the plugin and its tests; a static initialiser per TU does the binding.
#include "BeatEngine.h"
#include "Linear.h"
#include "MelodyTracker.h"

namespace gb {
inline bool bindEngineHooks() {
    auto& H = EngineHooks::get();
    H.makeCache = [] { return std::shared_ptr<void>(std::make_shared<linear::Player>()); };   // plays the snapshot's resolved Perf (no resolving here)
    H.collect = [](void* pl, const Snapshot& S, const EngineParams& P, double bpm, double b0, double b1, EngineHooks::Emit emit, void* ctx) {
        static_cast<linear::Player*>(pl)->process(S, P, bpm, b0, b1, [emit, ctx](int t, double b, float v, const EvInfo& e) { emit(ctx, t, b, v, e); }); };
    H.makePlayer = [](double sr) { auto p = std::make_shared<mel::Player>(); p->prepare(sr); return std::shared_ptr<void>(p); };
    H.playerProcess = [](void* player, const Snapshot& S, const EngineParams& P, double b0, double spb, int n, float* const* lr, bool running, int* uiRow) {
        if (!S.melody) { *uiRow = -1; return; }   // (MELODY off still runs the player: ringing notes release, auditions play)
        static_cast<mel::Player*>(player)->process(*S.melody, S.melSound.data(), S.melMips.data(), &S, &P, P.songMode, P.pattern, b0, spb, n, lr, running);
        mel::TickPos tp; *uiRow = running && S.melody->on && mel::tickAt(*S.melody, &S, P.songMode, P.pattern, b0, tp) ? tp.row + (tp.pat << 8) : -1; };
    H.playerReset = [](void* player) { static_cast<mel::Player*>(player)->reset(); };
    H.playerTempo = [](void* player) { return std::exchange(static_cast<mel::Player*>(player)->tempoOut, 0); };
    H.playerAudition = [](void* player, int inst, int note, float vel) { static_cast<mel::Player*>(player)->audition(inst, note, vel); };
    return true;
}
static const bool engineHooksBound = bindEngineHooks();
} // namespace gb
