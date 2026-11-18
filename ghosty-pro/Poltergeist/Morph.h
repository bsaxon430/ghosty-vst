#pragma once
// MORPH BOARD (Shared/SpiritBoard.h): ghosts on a board each hold a whole sound (a snapshot, a preset or the current sound); the puck
// (MORPH X / Y, host-automatable, or an ORBIT) blends them by weight.
//   continuous parameters: weighted blend in the host's normalised 0..1 domain (so skewed ranges like cutoff blend musically)
//   discrete parameters (switches, choices, ints): the LEADING ghost's value, with hysteresis (a new leader needs +0.08 weight)
//   per-block smoothing (~30 ms, exact once settled), no allocation on the audio thread, the ghost table guarded by a try-lock
//   samples: the leading ghost's sample is put on the Sample Ghost by the message thread (the normal sample-load path)
// The audio thread reads the blend through its own ParamReader (attached to the blended values), so with MORPH off nothing changes.
#include "Params.h"
#include "SpiritBoard.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace gs {

struct Morph {
    static constexpr int kG = ghosty::spirit::kMaxGhosts;
    struct P { juce::RangedAudioParameter* prm = nullptr; std::atomic<float>* raw = nullptr; bool discrete = false, keep = false; };
    std::vector<P> params; int n = 0;
    std::unique_ptr<std::atomic<float>[]> out;     // the blended REAL values (what morphReader reads)
    std::vector<float> table;                      // [kG][n] normalised values of each ghost
    std::vector<float> cur;                        // [n] the smoothed normalised blend
    int count = 0;                                 // ghosts in the table (== board ghosts)
    juce::SpinLock lock;
    ghosty::spirit::SharedBoard shared;
    ParamReader reader;
    std::atomic<int> leaderOut { -1 };
    int leader = -1; bool primed = false; double freeTime = 0;
    std::unordered_map<juce::String, int> index;

    void build(juce::AudioProcessorValueTreeState& s, juce::AudioProcessor& proc) {
        for (auto* a : proc.getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(a)) {
                P p; p.prm = r; p.raw = s.getRawParameterValue(r->getParameterID());
                p.discrete = r->isDiscrete() || r->isBoolean() || dynamic_cast<juce::AudioParameterInt*>(r) || dynamic_cast<juce::AudioParameterChoice*>(r);
                const auto id = r->getParameterID();
                p.keep = id == "bypass" || id.startsWith("morph_") || id == "quality" || id == "render_q";   // never part of the blend
                index[id] = (int) params.size(); params.push_back(p);
            }
        n = (int) params.size();
        out.reset(new std::atomic<float>[(size_t) n]);
        for (int i = 0; i < n; ++i) out[(size_t) i].store(params[(size_t) i].raw->load());
        table.assign((size_t) (kG * n), 0.0f); cur.assign((size_t) n, 0.0f);
        reader.attachWith([this](const juce::String& id) { auto it = index.find(id); jassert(it != index.end()); return &out[(size_t) it->second]; });
    }
    // message thread: a ghost's sound as normalised values (from a state tree with PARAM children: missing ids = the parameter's default)
    std::vector<float> valuesFromState(const juce::ValueTree& t) const {
        std::vector<float> v((size_t) n);
        for (int i = 0; i < n; ++i) v[(size_t) i] = params[(size_t) i].prm->getDefaultValue();
        for (auto c : t) if (c.hasType("PARAM")) { auto it = index.find(c.getProperty("id").toString()); if (it != index.end()) v[(size_t) it->second] = params[(size_t) it->second].prm->convertTo0to1((float) c.getProperty("value")); }
        return v;
    }
    std::vector<float> currentValues() const { std::vector<float> v((size_t) n); for (int i = 0; i < n; ++i) v[(size_t) i] = params[(size_t) i].prm->getValue(); return v; }
    void setTable(const std::vector<std::vector<float>>& ghosts) {   // message thread, after any board content change
        const juce::SpinLock::ScopedLockType sl(lock);
        count = juce::jmin(kG, (int) ghosts.size());
        for (int k = 0; k < count; ++k) if ((int) ghosts[(size_t) k].size() == n) std::copy(ghosts[(size_t) k].begin(), ghosts[(size_t) k].end(), table.begin() + (size_t) k * (size_t) n);
        primed = false;
    }
    // audio thread: blend into snap; false = morph not usable this block (the caller reads the normal parameters)
    bool process(Snap& snap, int numSamples, double sr, double ppq, bool playing, double bpm, float px, float py) {
        const juce::SpinLock::ScopedTryLockType tl(lock);
        if (!tl.isLocked()) { if (!primed) return false; reader.read(snap); return true; }   // (mid-edit: last block's blend)
        float xs[kG], ys[kG], w[kG]; ghosty::spirit::WeightParams wp; ghosty::spirit::OrbitParams op;
        const int g = juce::jmin(count, shared.read(xs, ys, wp, op));
        if (g <= 0) return false;
        const double t = op.sync ? (playing ? ppq : freeTime * bpm / 60.0) : freeTime;
        freeTime += numSamples / juce::jmax(1.0, sr);
        const auto off = ghosty::spirit::orbitOffset(op, t);
        const auto pk = ghosty::spirit::clampToBoard({ px + off.x, py + off.y });
        ghosty::spirit::computeWeights(xs, ys, g, pk.x, pk.y, wp, w);
        int best = 0; for (int k = 1; k < g; ++k) if (w[k] > w[best]) best = k;
        if (leader < 0 || leader >= g || !primed || w[best] > w[leader] + 0.08f || w[best] >= 0.999f) leader = best;
        const float a = primed ? (float) (1.0 - std::exp(-numSamples / (0.03 * sr))) : 1.0f;
        for (int i = 0; i < n; ++i) {
            const auto& p = params[(size_t) i];
            if (p.keep) { out[(size_t) i].store(p.raw->load(), std::memory_order_relaxed); continue; }
            float target;
            if (p.discrete) target = table[(size_t) leader * (size_t) n + (size_t) i];
            else { double s = 0; for (int k = 0; k < g; ++k) s += (double) w[k] * table[(size_t) k * (size_t) n + (size_t) i]; target = (float) s; }
            float& c = cur[(size_t) i];
            c = p.discrete ? target : (std::abs(target - c) < 1.0e-6f ? target : c + (target - c) * a);
            out[(size_t) i].store(p.prm->convertFrom0to1(juce::jlimit(0.0f, 1.0f, c)), std::memory_order_relaxed);
        }
        primed = true;
        shared.postLive(pk.x, pk.y, w, g); leaderOut.store(leader, std::memory_order_relaxed);
        reader.read(snap);
        return true;
    }
};

} // namespace gs
