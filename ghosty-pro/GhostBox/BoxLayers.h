#pragma once
// SPIRIT BOARD LAYER mode (Ghost Box): every ghost with weight > 0 processes the SAME input at the same time through its OWN rack
// (its scene), and the outputs are mixed at the ghosts' weights. (SMOOTH, the default, blends the SETTINGS of one rack instead.)
//
//   - one private rack per ghost (LayerHost: its own parameter store with the plugin's layout, loaded with the ghost's scene)
//   - only the K ghosts with the largest weights run (K = 2..6, hysteresis on who is in); a ghost entering starts from silence (its rack is
//     reset) and fades in, one leaving fades out (30 ms): no clicks, bounded CPU
//   - WEIGHT LAW: LINEAR (gains = weights, they sum to 1) or POWER (gains = sqrt(weights): equal power)
//   - WEIGHTS: DISTANCE (the puck, as in SMOOTH), EVEN (all equal), CUSTOM (per-ghost percentages, normalised)
//   - every path is padded to the longest latency (all equal with FIXED LATENCY on, the default)
//   - real time: the message thread builds a new LayerSet (allocations, scene loads, prepare) and publishes it through an atomic pointer;
//     the audio thread only reads it; old sets are freed after the audio thread has left them.
#include "BoxProcessor.h"
#include "FxRackImpl.h"
#include "SpiritBoard.h"
#include <memory>
#include <vector>

namespace boxlayers {

inline const juce::StringArray& blendNames() { static juce::StringArray s { "SMOOTH", "LAYER" }; return s; }
inline const juce::StringArray& lawNames() { static juce::StringArray s { "POWER", "LINEAR" }; return s; }
inline const juce::StringArray& sourceNames() { static juce::StringArray s { "DISTANCE", "EVEN", "CUSTOM" }; return s; }
inline const juce::StringArray& kNames() { static juce::StringArray s { "2", "3", "4", "5", "6" }; return s; }

// a private rack with its own parameter store (the plugin's layout); message thread builds / loads it, the audio thread runs it
class LayerHost : public juce::AudioProcessor
{
public:
    LayerHost() : juce::AudioProcessor (BusesProperties().withInput ("In", juce::AudioChannelSet::stereo(), true).withOutput ("Out", juce::AudioChannelSet::stereo(), true)),
                  apvts (*this, nullptr, "GHOSTBOX", BoxProcessor::layout()) { rack.attach (apvts); }
    ~LayerHost() override = default;
    // the ghost's scene: its rack settings (real values), the rack's own state (order, patterns, curves ...), FIXED LATENCY like the plugin
    void load (const gpfx::BoxMod::Snap& snap, bool fixedLatency)
    {
        for (auto* p : getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            {
                const auto id = rp->getParameterID();
                if (! id.startsWith ("fx_")) continue;
                auto it = snap.vals.find (id);
                rp->setValueNotifyingHost (it != snap.vals.end() ? rp->convertTo0to1 (it->second) : rp->getDefaultValue());
            }
        if (auto* fl = apvts.getParameter ("box_fixlat")) fl->setValueNotifyingHost (fixedLatency ? 1.0f : 0.0f);
        if (snap.rack.isValid()) rack.loadState (snap.rack);
    }
    void prepareLayer (double sr, int block)
    {
        setRateAndBufferSizeDetails (sr, block);
        rack.prepare (sr, block); rack.reset();
        scratch.setSize (2, block, false, true, false);
        pad.prepare (16384 + block, sr);
        gain = target = 0.0f; running = false;
    }
    // ---- audio thread
    gpfx::Rack rack;
    juce::AudioBuffer<float> scratch; gpfx::LatentDry pad;
    float gain = 0, target = 0; bool running = false, inTop = false;
    std::atomic<float> pct { 100.0f };                    // CUSTOM weight (message thread writes)

    // juce::AudioProcessor boilerplate (never hosted)
    const juce::String getName() const override { return "Ghost Box layer"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    using juce::AudioProcessor::processBlock;
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
    juce::AudioProcessorValueTreeState apvts;
};

struct LayerSet
{
    std::vector<std::shared_ptr<LayerHost>> hosts;   // board order (ghost k = hosts[k])
    std::vector<juce::String> ids;
    std::atomic<int> latency { 0 };                  // the longest path (main rack included)
};

// The engine (owned by BoxProcessor). Message thread: sync() after board / mode changes and from the 10 Hz timer; prepare() from
// prepareToPlay. Audio thread: process().
class Engine
{
public:
    std::atomic<float>* blendP = nullptr; std::atomic<float>* lawP = nullptr; std::atomic<float>* srcP = nullptr; std::atomic<float>* kP = nullptr;
    std::atomic<float> cpuPct { 0 }; std::atomic<int> activeNow { 0 };
    std::atomic<bool> enabled { false };   // the board is ON (message thread mirrors it in sync())
    bool wantLayer() const noexcept { return blendP != nullptr && blendP->load() > 0.5f && enabled.load (std::memory_order_relaxed); }

    Engine() : builder (*this) {}
    ~Engine() { builder.stopThread (4000); auto* s = set.exchange (nullptr); delete s; for (auto& r : retired) delete r.first; }

    // Message thread (the 10 Hz timer, board / mode changes): keeps the set in step with the ghosts. NEVER builds a rack here: the racks
    // (about 50 ms each at 48 kHz, more at high rates) are built + prepared on a background thread, and the finished set is published on
    // a later call. Until then the plugin's own rack plays (SMOOTH sound) and the board shows "preparing...".
    void sync (gpfx::Rack::Impl& rack, bool fixedLatency, double sr, int block, int mainLatency)
    {
        retire();
        auto& box = rack.box;
        enabled.store (box.isBoardOn());
        if (blendP == nullptr || blendP->load() < 0.5f) { if (set.load() != nullptr && layerGain <= 0.0f) publish (nullptr); preparing.store (false); return; }   // (kept while it fades out)
        if (box.board.ghosts.empty()) { if (set.load() != nullptr) publish (nullptr); preparing.store (false); return; }
        const juce::String env = juce::String ((int) fixedLatency) + "|" + juce::String (sr) + "|" + juce::String (block);
        // 1) a finished build: its racks join the cache
        { const juce::ScopedLock sl (builder.lock); for (auto& [k, h] : builder.done) cache[k] = h; builder.done.clear(); }
        // 2) what every ghost needs: a key = its scene's content + the environment (only recomputed when something changed)
        if (box.boardContentVersion != seenVersion || env != seenEnv || keys.size() != box.board.ghosts.size())
        {
            keys.clear();
            for (auto& g : box.board.ghosts)
            {
                auto it = box.boardSnaps.find (g.id);
                juce::String k = env + "|";
                if (it != box.boardSnaps.end()) k << juce::String::toHexString ((juce::int64) (gpfx::BoxMod::snapText (it->second) + (it->second.rack.isValid() ? it->second.rack.toXmlString() : juce::String())).hashCode64());
                keys.push_back (k);
            }
            seenVersion = box.boardContentVersion; seenEnv = env;
        }
        // 3) racks still missing: one background job (copies of the scenes)
        std::vector<std::pair<juce::String, gpfx::BoxMod::Snap>> missing;
        for (size_t i = 0; i < keys.size(); ++i)
            if (cache.find (keys[i]) == cache.end())
            {
                auto it = box.boardSnaps.find (box.board.ghosts[i].id);
                missing.push_back ({ keys[i], it != box.boardSnaps.end() ? it->second : gpfx::BoxMod::Snap() });
            }
        if (! missing.empty())
        {
            if (builder.unavailable.load()) { preparing.store (false); return; }   // no builder thread: stay on the main rack
            preparing.store (true);
            if (! builder.busy()) builder.start (std::move (missing), fixedLatency, sr, block);
            return;
        }
        preparing.store (false);
        // 4) every ghost has its rack: publish the set if it differs from the running one
        auto* cur = set.load();
        bool same = cur != nullptr && cur->hosts.size() == keys.size();
        for (size_t i = 0; same && i < keys.size(); ++i) same = cur->hosts[i] == cache[keys[i]] && cur->ids[i] == box.board.ghosts[i].id;
        if (! same)
        {
            auto* ns = new LayerSet();
            for (size_t i = 0; i < keys.size(); ++i) { ns->hosts.push_back (cache[keys[i]]); ns->ids.push_back (box.board.ghosts[i].id); }
            publish (ns);
            for (auto it = cache.begin(); it != cache.end();)   // racks no ghost needs any more go (the old set keeps its own until retired)
                if (std::find (keys.begin(), keys.end(), it->first) == keys.end()) it = cache.erase (it); else ++it;
        }
        updateLatency (mainLatency); updatePct (box);
    }
    void prepare (double sr, int block)   // prepareToPlay (audio stopped): buffers sized; the racks for the new rate are rebuilt in the background
    {
        acc.setSize (2, block, false, true, false); mainPad.prepare (16384 + block, sr);
        if (auto* s = set.load()) for (auto& h : s->hosts) h->prepareLayer (sr, block);   // (the running set keeps playing at the new rate meanwhile)
        layerGain = mainGain = -1.0f; srate = sr;
    }
    void testNoBuilderThread() { builder.noThread.store (true); }        // tests: the host gives us no thread
    bool builderUnavailable() const { return builder.unavailable.load(); }
    std::atomic<bool> preparing { false };   // racks are being built (the board shows "preparing...")
    // test hook: wait (message thread) until the background build has finished and the set is published
    bool waitReady (gpfx::Rack::Impl& rack, bool fixedLatency, double sr, int block, int mainLatency, int timeoutMs = 20000)
    {
        const auto t0 = juce::Time::getMillisecondCounter();
        for (;;) { sync (rack, fixedLatency, sr, block, mainLatency); if (! preparing.load() && ! builder.busy()) return true; if (juce::Time::getMillisecondCounter() - t0 > (juce::uint32) timeoutMs) return false; juce::Thread::sleep (5); }
    }
    int latency() const { auto* s = set.load(); return s != nullptr && (wantLayer() || layerGain > 0.0f) ? s->latency.load() : -1; }
    bool active() const { return set.load() != nullptr && (wantLayer() || layerGain > 0.0f); }

    // ---- audio thread. buffer holds the input (after the input gain); mainProcess runs the plugin's own rack on a buffer.
    // Returns false when LAYER is fully off (the caller then runs its rack as usual).
    template <typename MainFn>
    bool process (juce::AudioBuffer<float>& buffer, const gpfx::Transport& t, gpfx::Rack::Impl& rack, const float* scL, const float* scR, MainFn&& mainProcess)
    {
        inProcess.store (1, std::memory_order_release);
        auto* s = set.load (std::memory_order_acquire);
        const bool want = wantLayer() && s != nullptr && ! s->hosts.empty();
        const bool first = layerGain < 0.0f;
        if (first) { layerGain = want ? 1.0f : 0.0f; mainGain = want ? 0.0f : 1.0f; }   // first block after prepare: no fade
        if (! want && layerGain <= 0.0f) { done(); return false; }
        const auto t0 = juce::Time::getHighResolutionTicks();
        const int n = buffer.getNumSamples();
        const float fadeStep = (float) n / (float) (0.03 * srate);
        const int nG = s != nullptr ? (int) s->hosts.size() : 0;
        // weights
        std::array<float, ghosty::spirit::kMaxGhosts> w {};
        const int src = srcP != nullptr ? (int) srcP->load() : 0;
        if (src == 1) for (int k = 0; k < nG; ++k) w[(size_t) k] = 1.0f / (float) nG;
        else if (src == 2)
        {
            float sum = 0; for (int k = 0; k < nG; ++k) { w[(size_t) k] = std::max (0.0f, s->hosts[(size_t) k]->pct.load (std::memory_order_relaxed)); sum += w[(size_t) k]; }
            if (sum > 0) for (int k = 0; k < nG; ++k) w[(size_t) k] /= sum; else for (int k = 0; k < nG; ++k) w[(size_t) k] = 1.0f / (float) juce::jmax (1, nG);
        }
        // the puck (X / Y + orbit), as the board engine computes it: the weights for DISTANCE, and always what the board shows
        // (EVEN / CUSTOM weights used to post the last puck, so the orbit never moved on screen in those modes)
        std::array<float, ghosty::spirit::kMaxGhosts> xs {}, ys {}; ghosty::spirit::WeightParams wp; ghosty::spirit::OrbitParams op;
        const int nb = rack.box.boardShared.read (xs.data(), ys.data(), wp, op);
        auto* ap = rack.apvts;
        auto raw = [ap] (const char* id, float d) { if (ap == nullptr) return d; auto* v = ap->getRawParameterValue (id); return v != nullptr ? v->load() : d; };
        op.mode = (ghosty::spirit::Orbit) juce::jlimit (0, 3, (int) raw ("box_orbit", 0)); op.sync = true;
        op.rate = (float) (1.0 / gpfx::boardOrbitBeats ((int) raw ("box_orbit_rate", 3))); op.depth = gpfx::clampf (raw ("box_orbit_depth", 0.25f), 0, 1) * 0.5f;
        juce::Point<float> pk;
        if (op.mode != ghosty::spirit::Orbit::Off || ! rack.box.boardShared.pathPuck (t.ppq, pk))   // ORBIT OFF + a DRAWN ORBIT: its path
            pk = ghosty::spirit::clampToBoard (juce::Point<float> (gpfx::clampf (raw ("box_board_x", 0.5f), 0, 1), gpfx::clampf (raw ("box_board_y", 0.5f), 0, 1)) + ghosty::spirit::orbitOffset (op, t.ppq));
        if (src == 0)
        {   // DISTANCE
            if (nb == nG && nG > 0) ghosty::spirit::computeWeights (xs.data(), ys.data(), nG, pk.x, pk.y, wp, w.data());
            else for (int k = 0; k < nG; ++k) w[(size_t) k] = lastW[(size_t) k];   // mid-edit: keep the last weights
        }
        for (int k = 0; k < nG; ++k) lastW[(size_t) k] = w[(size_t) k];
        if (nG > 0 && (src != 0 || nb == nG)) rack.box.boardShared.postLive (pk.x, pk.y, w.data(), nG);
        rack.box.boardShared.postTime (t.ppq, t.bpm, t.playing);
        // top K with hysteresis: an outsider replaces the weakest insider only when it is 0.05 stronger
        const int K = juce::jlimit (2, 6, 2 + (kP != nullptr ? (int) kP->load() : 2));
        for (int pass = 0; pass < nG; ++pass)
        {
            int inCount = 0, weakest = -1, strongest = -1;
            for (int k = 0; k < nG; ++k)
            {
                auto& h = *s->hosts[(size_t) k];
                if (w[(size_t) k] <= 1.0e-6f) h.inTop = false;
                if (h.inTop) { ++inCount; if (weakest < 0 || w[(size_t) k] < w[(size_t) weakest]) weakest = k; }
                else if (w[(size_t) k] > 1.0e-6f && (strongest < 0 || w[(size_t) k] > w[(size_t) strongest])) strongest = k;
            }
            if (strongest < 0) break;
            if (inCount < K) { s->hosts[(size_t) strongest]->inTop = true; continue; }
            if (w[(size_t) strongest] > w[(size_t) weakest] + 0.05f) { s->hosts[(size_t) weakest]->inTop = false; s->hosts[(size_t) strongest]->inTop = true; continue; }
            break;
        }
        // the gains of the running set (renormalised over the ghosts in the top K)
        float sumIn = 0; for (int k = 0; k < nG; ++k) if (s->hosts[(size_t) k]->inTop) sumIn += w[(size_t) k];
        const bool power = lawP != nullptr && lawP->load() < 0.5f;
        int act = 0;
        for (int k = 0; k < nG; ++k)
        {
            auto& h = *s->hosts[(size_t) k];
            const float wk = h.inTop && sumIn > 0 ? w[(size_t) k] / sumIn : 0.0f;
            h.target = power ? std::sqrt (wk) : wk;
            if (! h.running && h.target > 0.0f)
            {   // enters from silence (fading in): a clean rack, its latency pad already at the right length (no crossfade from an old tap)
                h.rack.reset(); h.running = true;
                h.pad.clear(); h.pad.cur = h.pad.from = juce::jmax (0, juce::jmin (s->latency.load(), 16384) - h.rack.latencySamples());
            }
            if (first) h.gain = h.target;                                                // (the first block after prepare starts at the weights)
        }
        const float layerTarget = want ? 1.0f : 0.0f, mainTarget = want ? 0.0f : 1.0f;
        // mix: acc = sum of the running racks, each gain-ramped over this block, padded to the common latency
        acc.clear (0, n);
        const int L = s != nullptr ? juce::jmin (s->latency.load(), 16384) : 0;
        for (int k = 0; k < nG; ++k)
        {
            auto& h = *s->hosts[(size_t) k];
            if (! h.running) continue;
            ++act;
            for (int c = 0; c < 2; ++c) h.scratch.copyFrom (c, 0, buffer, juce::jmin (c, buffer.getNumChannels() - 1), 0, n);
            juce::AudioBuffer<float> view (h.scratch.getArrayOfWritePointers(), 2, 0, n);
            h.rack.setSidechain (scL, scR, n);
            h.rack.process (view, t);
            h.pad.process (view.getWritePointer (0), view.getWritePointer (1), n, juce::jmax (0, L - h.rack.latencySamples()));
            const float g0 = h.gain, d = h.target - h.gain, g1 = std::abs (d) <= fadeStep ? h.target : h.gain + (d > 0 ? fadeStep : -fadeStep);
            for (int c = 0; c < 2; ++c) acc.addFromWithRamp (c, 0, view.getReadPointer (c), n, g0, g1);
            h.gain = g1;
            if (h.gain <= 0.0f && h.target <= 0.0f) h.running = false;   // faded out: costs nothing from now on
        }
        // the plugin's own rack while the mode crossfades (SMOOTH <-> LAYER), padded the same way
        const float m0 = mainGain, md = mainTarget - mainGain, m1 = std::abs (md) <= fadeStep ? mainTarget : mainGain + (md > 0 ? fadeStep : -fadeStep);
        const float l0 = layerGain, ld = layerTarget - layerGain, l1 = std::abs (ld) <= fadeStep ? layerTarget : layerGain + (ld > 0 ? fadeStep : -fadeStep);
        if (m0 > 0.0f || m1 > 0.0f)
        {
            if (m0 <= 0.0f) rack.owner->reset();   // the plugin's rack comes back from silence
            mainProcess (buffer);
            mainPad.process (buffer.getWritePointer (0), buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : buffer.getWritePointer (0), n, juce::jmax (0, L - rack.owner->latencySamples()));
            for (int c = 0; c < buffer.getNumChannels(); ++c) buffer.applyGainRamp (c, 0, n, m0, m1);
            for (int c = 0; c < buffer.getNumChannels(); ++c) buffer.addFromWithRamp (c, 0, acc.getReadPointer (juce::jmin (c, 1)), n, l0, l1);
        }
        else
            for (int c = 0; c < buffer.getNumChannels(); ++c)
            {
                if (l0 == 1.0f && l1 == 1.0f) buffer.copyFrom (c, 0, acc, juce::jmin (c, 1), 0, n);
                else { buffer.clear (c, 0, n); buffer.addFromWithRamp (c, 0, acc.getReadPointer (juce::jmin (c, 1)), n, l0, l1); }
            }
        mainGain = m1; layerGain = l1;
        activeNow.store (act, std::memory_order_relaxed);
        const double secs = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0), blockSecs = (double) n / srate;
        cpuPct.store (cpuPct.load (std::memory_order_relaxed) * 0.9f + 0.1f * (float) (100.0 * secs / blockSecs), std::memory_order_relaxed);
        done();
        return true;
    }

private:
    void done() { inProcess.store (0, std::memory_order_release); blocks.fetch_add (1, std::memory_order_acq_rel); }
    void publish (LayerSet* ns)
    {
        auto* old = set.exchange (ns, std::memory_order_acq_rel);
        if (old != nullptr) retired.push_back ({ old, blocks.load() });
        retire();
    }
    void retire()
    {   // a set is freed once the audio thread has finished a block after it was replaced (or is not running at all)
        const bool idle = inProcess.load() == 0; const auto b = blocks.load();
        for (auto it = retired.begin(); it != retired.end();)
            if (idle || b >= it->second + 2) { delete it->first; it = retired.erase (it); } else ++it;
    }
    void updateLatency (int mainLatency)
    {
        if (auto* s = set.load())
        {
            int L = mainLatency; for (auto& h : s->hosts) L = juce::jmax (L, h->rack.latencySamples());
            s->latency.store (L);
        }
    }
    void updatePct (gpfx::BoxMod& box)
    {
        if (auto* s = set.load())
            for (size_t i = 0; i < s->ids.size(); ++i) { auto it = box.boardPct.find (s->ids[i]); s->hosts[i]->pct.store (it != box.boardPct.end() ? it->second : 100.0f); }
    }
    struct Builder : juce::Thread
    {
        explicit Builder (Engine& e) : juce::Thread ("Ghost Box layers"), engine (e) {}
        juce::CriticalSection lock;                                   // job / done (message thread <-> this thread; never the audio thread)
        std::vector<std::pair<juce::String, gpfx::BoxMod::Snap>> job; bool fixlat = true; double sr = 48000; int block = 512;
        std::vector<std::pair<juce::String, std::shared_ptr<LayerHost>>> done;
        std::atomic<bool> working { false }, unavailable { false }, noThread { false };   // (noThread: tests simulate a host out of threads)
        bool busy() const { return working.load(); }
        void start (std::vector<std::pair<juce::String, gpfx::BoxMod::Snap>> j, bool fl, double rate, int blk)
        {
            { const juce::ScopedLock sl (lock); job = std::move (j); fixlat = fl; sr = rate; block = blk; }
            working.store (true);
            if (isThreadRunning()) { notify(); return; }
            if (noThread.load() || ! startThread (juce::Thread::Priority::low))
            {   // no thread could be made (out of resources): LAYER stays off (the main rack plays), nothing waits for it
                { const juce::ScopedLock sl (lock); job.clear(); }
                working.store (false); unavailable.store (true);
            }
        }
        void run() override
        {
            while (! threadShouldExit())
            {
                std::vector<std::pair<juce::String, gpfx::BoxMod::Snap>> j; bool fl; double rate; int blk;
                { const juce::ScopedLock sl (lock); j = std::move (job); job.clear(); fl = fixlat; rate = sr; blk = block; }
                for (auto& [k, snap] : j)
                {
                    if (threadShouldExit()) return;
                    auto h = std::make_shared<LayerHost>();
                    h->load (snap, fl); h->prepareLayer (rate, blk);
                    const juce::ScopedLock sl (lock); done.push_back ({ k, h });
                }
                working.store (false);
                wait (-1);
            }
        }
        Engine& engine;
    } builder;
    std::map<juce::String, std::shared_ptr<LayerHost>> cache; std::vector<juce::String> keys; int seenVersion = -1; juce::String seenEnv;
    std::atomic<LayerSet*> set { nullptr };
    std::vector<std::pair<LayerSet*, int64_t>> retired;
    std::atomic<int> inProcess { 0 }; std::atomic<int64_t> blocks { 0 };
    juce::AudioBuffer<float> acc; gpfx::LatentDry mainPad;
    std::array<float, ghosty::spirit::kMaxGhosts> lastW {};
    float layerGain = -1.0f, mainGain = -1.0f; double srate = 48000;
};

} // namespace boxlayers
