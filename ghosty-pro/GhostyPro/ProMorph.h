#pragma once
// MORPH BOARD (Ghosty Pro's SPIRIT BOARD, Shared/SpiritBoard.h): ghosts on a 2-D board each hold a whole sound (the CURRENT SOUND, a
// SNAPSHOT or a factory preset); the puck (parameters "morph_x" / "morph_y", host-automatable, plus an ORBIT) blends them.
//
//   continuous (every float parameter the engine reads through the MacroEngine shadows): blended per block on the audio thread in
//       the parameter's own normalised domain (its skew = log-like for times, cutoff is already exponential); a ghost with weight 1
//       gives exactly its own value. Moves are slewed (30 ms to cross the whole range), so nothing zips and it lands exactly.
//   everything else (switches, choices, tables, drawn waves, the sample, layers, the FX rack, the macro map): the highest-weight
//       ghost's sound, loaded on the message thread when the leader changes (with 10 % hysteresis); wavetables crossfade on the swap.
//   the per-preset loudness trim: blended in dB.
// Nothing allocates on the audio thread: the compiled set (one value per slot per ghost) is handed over by pointer (pending -> live,
// the old one to 'trash', freed on the message thread).
#include "ProMacros.h"
#include "SpiritBoard.h"
#include <vector>

namespace gpro
{
struct MorphSet
{
    int ghosts = 0, slots = 0;
    std::vector<float> plain;      // ghosts x slots, plain parameter values (NaN = this ghost's sound has no value: the slot isn't morphed)
    std::vector<float> norm;       // the same, normalised 0..1
    std::vector<float> trimDb;     // per ghost
    // the Ghost FX rack's continuous parameters (the rack reads the parameters' own atomics, not the MacroEngine shadows)
    int fx = 0;
    std::vector<float> fxPlain, fxNorm;                  // ghosts x fx
};

class MorphEngine
{
public:
    ~MorphEngine() { delete pending.exchange (nullptr); delete trash.exchange (nullptr); delete live; }
    void init (int slotCount) { out.assign ((size_t) slotCount, 0.0f); use.assign ((size_t) slotCount, 0); sm.assign ((size_t) slotCount, 0.0f); }
    // the rack's float parameters (constructor, message thread): blended values are written into their atomics on the audio thread
    void initFx (std::vector<juce::RangedAudioParameter*> prms, std::vector<std::atomic<float>*> raws)
    { fxPrm = std::move (prms); fxRaw = std::move (raws); fxSm.assign (fxPrm.size(), 0.0f); }
    int fxCount() const noexcept { return (int) fxPrm.size(); }
    // message thread: the value the board gives slot s right now (false = the slot isn't blended)
    bool blended (int s, float& v) const noexcept { if (s < 0 || s >= (int) use.size() || use[(size_t) s] == 0 || ! wasOn) return false; v = out[(size_t) s]; return true; }
    juce::RangedAudioParameter* fxParam (int i) const noexcept { return fxPrm[(size_t) i]; }
    // message thread
    void setSet (std::unique_ptr<MorphSet> s) { collect(); delete pending.exchange (s.release(), std::memory_order_acq_rel); collect(); }
    void collect() { delete trash.exchange (nullptr, std::memory_order_acq_rel); }
    std::atomic<bool> enabled { false };
    ghosty::spirit::SharedBoard board;

    // audio thread, once per block before MacroEngine::update. Returns the blended trim gain (linear), or -1 when the board is off.
    float process (MacroEngine& macros, float px, float py, double ppq, bool playing, double freeSecs, int numSamples, double sr) noexcept
    {
        if (pending.load (std::memory_order_acquire) != nullptr && trash.load (std::memory_order_acquire) == nullptr)
            if (auto* c = pending.exchange (nullptr, std::memory_order_acq_rel)) { trash.store (live, std::memory_order_release); live = c; snap = true; }
        float xs[ghosty::spirit::kMaxGhosts], ys[ghosty::spirit::kMaxGhosts];
        ghosty::spirit::WeightParams wp; ghosty::spirit::OrbitParams op;
        const int n = board.read (xs, ys, wp, op);
        if (! enabled.load (std::memory_order_relaxed) || live == nullptr || n <= 0 || live->ghosts != n || live->slots != (int) out.size())
        {
            macros.setOverride (nullptr, nullptr); wasOn = false; return -1.0f;
        }
        const auto off = ghosty::spirit::orbitOffset (op, (op.sync && playing) ? ppq : freeSecs * (op.sync ? 2.0 : 1.0));   // synced: beats (free running: 120 BPM)
        const auto pk = ghosty::spirit::clampToBoard ({ px + off.x, py + off.y });
        float w[ghosty::spirit::kMaxGhosts] {};
        ghosty::spirit::computeWeights (xs, ys, n, pk.x, pk.y, wp, w);
        board.postLive (pk.x, pk.y, w, n);
        int exact = -1; for (int k = 0; k < n; ++k) if (w[k] >= 1.0f) exact = k;
        const int S = live->slots;
        const float frac = juce::jmin (1.0f, (float) numSamples / (float) (0.030 * sr));    // 30 ms to cross a whole range
        for (int s = 0; s < S; ++s)
        {
            bool all = true; double acc = 0;
            for (int k = 0; k < n && all; ++k) { const float v = live->norm[(size_t) (k * S + s)]; all = std::isfinite (v); acc += (double) w[k] * v; }
            if (! all) { use[(size_t) s] = 0; continue; }
            auto* prm = macros.slotParam (s);
            const float target = exact >= 0 ? live->plain[(size_t) (exact * S + s)] : prm->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, (float) acc));
            float& cur = sm[(size_t) s];
            if (snap || ! wasOn || use[(size_t) s] == 0) cur = target;
            else { const float range = prm->range.end - prm->range.start, step = range * frac; cur += juce::jlimit (-step, step, target - cur); }
            out[(size_t) s] = cur; use[(size_t) s] = 1;
        }
        // the FX rack: same blend, written into the parameters' atomics (what the rack reads); a ghost alone = its exact values
        if (live->fx == (int) fxPrm.size())
            for (int f = 0; f < live->fx; ++f)
            {
                auto* prm = fxPrm[(size_t) f];
                double acc = 0; for (int k = 0; k < n; ++k) acc += (double) w[k] * live->fxNorm[(size_t) (k * live->fx + f)];
                const float target = exact >= 0 ? live->fxPlain[(size_t) (exact * live->fx + f)] : prm->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, (float) acc));
                float& cur = fxSm[(size_t) f];
                if (snap || ! wasOn) cur = target;
                else { const auto& rg = prm->getNormalisableRange(); const float step = (rg.end - rg.start) * frac; cur += juce::jlimit (-step, step, target - cur); }
                if (fxRaw[(size_t) f]->load (std::memory_order_relaxed) != cur) fxRaw[(size_t) f]->store (cur, std::memory_order_relaxed);
            }
        snap = false; wasOn = true;
        macros.setOverride (out.data(), use.data());
        double db = 0; for (int k = 0; k < n; ++k) db += (double) w[k] * live->trimDb[(size_t) k];
        return juce::Decibels::decibelsToGain ((float) db);
    }
    int leader() const noexcept
    {   // message thread: the highest live weight (with 10 % hysteresis against the current leader)
        const int n = board.liveN.load();
        int best = -1; float bw = -1;
        for (int k = 0; k < n; ++k) { const float w = board.liveW[(size_t) k].load(); if (w > bw) { bw = w; best = k; } }
        if (lastLeader >= 0 && lastLeader < n && best != lastLeader && bw < board.liveW[(size_t) lastLeader].load() + 0.10f) return lastLeader;
        return best;
    }
    mutable int lastLeader = -1;

private:
    MorphSet* live = nullptr;
    std::atomic<MorphSet*> pending { nullptr }, trash { nullptr };
    std::vector<float> out, sm; std::vector<uint8_t> use;
    std::vector<juce::RangedAudioParameter*> fxPrm; std::vector<std::atomic<float>*> fxRaw; std::vector<float> fxSm;
    bool snap = false, wasOn = false;
};
} // namespace gpro
