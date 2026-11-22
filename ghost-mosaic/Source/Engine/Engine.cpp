// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
#include "Engine.h"
#include "Synapses.h"
#include "TerrainShaping.h"
#include "Brightness.h"
#include <thread>

using namespace spiralcore;

namespace mosaic
{
static constexpr double kMaxPieces = 40000;

// how long a library build takes on this computer, learnt from each build (first guess: a slow 4-core Intel Mac)
//   analysing: seconds per piece; the network: seconds per pair of pieces compared, times the threads used
static std::atomic<double> gAnalyseCost { 6.0e-5 }, gPairCost { 2.0e-7 };
static int networkThreads()
{
    const auto hw = std::thread::hardware_concurrency();
    return (int) juce::jlimit (1u, 8u, hw > 1 ? hw - 1 : 1u);   // (as buildSynapses picks them)
}
static inline float q100 (float v) { return (float) (int) std::lround (v * 100.0f) / 100.0f; }   // the Qt sliders' s/100.0f

// the upstream engine's status lines, in the screen's words (it never says "brain": that is the library)
static juce::String plainStatus (const juce::String& s)
{
    if (s.startsWith ("brain ready")) return "Library ready";
    if (s.startsWith ("building synapses")) return "Building the network " + s.fromFirstOccurrenceOf ("synapses ", false, false);
    if (s.startsWith ("processing sample")) return "Analysing sound " + s.fromFirstOccurrenceOf ("sample ", false, false);
    if (s.startsWith ("all samples processed")) return "All sounds analysed";
    if (s.startsWith ("Done:")) return "Network built: " + s.fromFirstOccurrenceOf ("for ", false, false).replace ("blocks", "pieces");
    if (s.startsWith ("loaded ")) return "Loaded " + juce::File (s.substring (7)).getFileNameWithoutExtension().fromLastOccurrenceOf (":", false, false);
    if (s.startsWith ("deleted ")) return "Removed " + juce::File (s.substring (8)).getFileNameWithoutExtension().fromLastOccurrenceOf (":", false, false);
    if (s.startsWith ("problem loading")) return "Couldn't load " + juce::File (s.substring (16)).getFileName();
    return s.replace ("brain", "library").replace ("synapses", "network links").replace ("blocks", "pieces");
}

// ------------------------------------------------------------------------------------ worker
class Engine::Worker : public juce::Thread
{
public:
    explicit Worker (Engine& e) : juce::Thread ("Ghost Mosaic worker"), engine (e) {}
    void add (std::function<void()> f)
    {
        { const juce::ScopedLock l (lock); jobs.push_back (std::move (f)); }
        engine.m_jobsPending.fetch_add (1);
        wake.signal();
    }
    void run() override
    {
        compat::initFftThreads();
        compat::StatusSink sink = [this] (const juce::String& path, const juce::StringArray& args)
        {
            if (path == "/report" && args.size() > 0) engine.setStatus (plainStatus (args[0]));
            if (engine.onStatusMessage) engine.onStatusMessage (path, args);
        };
        compat::setThreadStatusSink (&sink);
        while (! threadShouldExit())
        {
            std::function<void()> job;
            { const juce::ScopedLock l (lock); if (! jobs.empty()) { job = std::move (jobs.front()); jobs.pop_front(); } }
            if (job)
            {
                compat::setThreadEngineRate (engine.m_sr);
                engine.runJob (job);
                engine.m_jobsPending.fetch_sub (1);
                engine.sendChangeMessage();
                continue;
            }
            engine.freeRetired();
            wake.wait (20);
        }
        compat::setThreadStatusSink (nullptr);
    }
    Engine& engine;
    juce::CriticalSection lock;
    std::deque<std::function<void()>> jobs;
    juce::WaitableEvent wake;
};

// ------------------------------------------------------------------------------------ setup
Engine::Engine (juce::AudioProcessorValueTreeState& params, bool isEffect)
    : m_params (params), m_isEffect (isEffect)
{
    for (auto& p : paramTable())
        m_raw.push_back (params.getRawParameterValue (p.id));
    m_left.setObserver ({ &Engine::observe, &m_obsL, &Engine::tapBefore, &Engine::tapPicked, &Engine::tapEnded });
    m_right.setObserver ({ &Engine::observe, &m_obsR, &Engine::tapBefore, &Engine::tapPicked, &Engine::tapEnded });
    m_left.init (nullptr, nullptr);
    m_right.init (nullptr, nullptr);
    m_scratch.setSize (2, 16384);
    for (auto& a : m_liveSet) a.store (0.0f);
    for (auto& a : m_liveNow) a.store (0.0f);
    for (auto& a : m_playBits) a.store (0xffffffffu);
    m_dryDelay.setSize (2, 1 << 18);
    m_limiter.prepare (44100, 2);
    if (isEffect) m_playingUi = true;
    m_writerThread.startThread();
    m_worker = std::make_unique<Worker> (*this);
    m_worker->startThread (juce::Thread::Priority::low);   // below the audio thread and the UI
}

Engine::~Engine()
{
    stopRecording();
    m_worker->stopThread (20000);
    m_writerThread.stopThread (2000);
    // free everything the audio side held
    drainQueues();
    delete m_source; delete m_graveyard; delete m_tl; delete m_tr; delete m_live; delete m_info;
    freeRetired();
}

int Engine::pidx (const char* id) const
{
    auto& t = paramTable();
    for (size_t i = 0; i < t.size(); ++i) if (std::strcmp (t[i].id, id) == 0) return (int) i;
    jassertfalse; return 0;
}

float Engine::P (const char* id) const
{
    const int i = pidx (id);
    auto* r = m_raw[(size_t) i];
    return r != nullptr ? r->load() : paramTable()[(size_t) i].def;
}

void Engine::setStatus (const juce::String& s)
{
    { const juce::ScopedLock l (m_modelLock); m_status = s; }
    sendChangeMessage();
}

juce::String Engine::newKey (const juce::String& name)
{
    const juce::ScopedLock l (m_modelLock);
    return "mem:" + juce::String (++m_keyCounter) + ":" + name;
}

void Engine::post (std::function<void()> job) { m_worker->add (std::move (job)); }

void Engine::runJob (std::function<void()>& job)
{
    try { job(); }
    catch (const std::exception& e) { setStatus (juce::String ("Problem: ") + e.what()); }
    catch (...) { setStatus ("Problem: something went wrong in the background job"); }
    m_phase = 0;
}

void Engine::waitUntilIdle (int timeoutMs)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (m_jobsPending.load() > 0 && juce::Time::getMillisecondCounter() < end)
        juce::Thread::sleep (2);
}

// ------------------------------------------------------------------------------------ audio side
void Engine::prepare (double sampleRate, int maxBlock)
{
    const bool rateChanged = std::abs (sampleRate - m_sr) > 0.5;
    m_sr = sampleRate;
    m_maxBlock = juce::jmax (64, maxBlock);
    if (m_scratch.getNumSamples() < m_maxBlock) m_scratch.setSize (2, m_maxBlock);
    m_limiter.prepare (sampleRate, 2);
    m_dryDelay.clear(); m_dryPos = 0;
    m_limOn = P ("limiter") > 0.5f; m_limFade = 1; m_limFadeDir = 0; m_limHold = 0;
    int lat = 0;
    if (m_isEffect)
    {
        const u32 bs = (u32) std::lround (juce::jlimit (10.0f, 99999.0f, P ("tgt_block")));   // (rounded, as LiveTarget gets it)
        lat += (int) bs + juce::jmax (64, m_maxBlock) + 1024;
        rebuildLive();
    }
    m_latency = lat;
    m_applied.valid = false;
    m_livePending = true;
    if (rateChanged && ! m_sounds.empty()) scheduleAutoBuild (true, true);
    if (rateChanged)
    {
        // the engine's bands are hundredths of half the sample rate: the weights follow the rate
        ghost::terrain::Terrain t; juce::String st;
        { const juce::ScopedLock l (m_modelLock); t = m_terrainMsg; st = m_terrainState; }
        if (! t.empty() || t.grit > 0) setTerrain (t, st);
    }
}

// ------------------------------------------------------------------------------------ TERRAIN
void Engine::pieceBrightness (const brain& b, double rate, std::vector<float>& rank, std::vector<float>& hz)
{
    // each piece's spectral centroid (power-weighted mean frequency of its own, already-faded audio), then its rank
    const u32 n = b.get_num_blocks();
    hz.assign (n, 0.0f); rank.assign (n, 0.5f);
    if (n == 0) return;
    for (u32 i = 0; i < n; ++i)
    {
        const auto& pcm = b.get_block (i).get_pcm();
        hz[i] = spectralCentroidHz (pcm.get_buffer(), (int) pcm.get_length(), rate);
    }
    std::vector<u32> idx (n);
    for (u32 i = 0; i < n; ++i) idx[i] = i;
    std::stable_sort (idx.begin(), idx.end(), [&] (u32 a, u32 c) { return hz[a] < hz[c]; });
    for (u32 k = 0; k < n; ++k) rank[idx[k]] = n > 1 ? (float) k / (float) (n - 1) : 0.5f;
}

void Engine::updateBias()
{
    // audio thread: the per-piece factors follow the tilt (the land's plus the drag's), remade only when it moves
    const float tilt = juce::jlimit (-1.0f, 1.0f, m_shapeLive.tilt + 0.6f * m_mSide);
    const bool on = std::abs (tilt) > 1e-3f && m_info != nullptr && ! m_info->bright.empty();
    if (! on)
    {
        if (m_biasTilt != 0.0f) { m_left.setBlockBias (nullptr, 0); m_right.setBlockBias (nullptr, 0); m_biasTilt = 0; }
        return;
    }
    if (std::abs (tilt - m_biasTilt) < 2e-3f) return;
    auto& f = m_info->factor;
    const auto& br = m_info->bright;
    for (size_t i = 0; i < br.size(); ++i) f[i] = Shaping::tiltFactor (tilt, br[i]);
    m_left.setBlockBias (f.data(), (u32) f.size()); m_right.setBlockBias (f.data(), (u32) f.size());
    m_biasTilt = tilt;
}

void Engine::setTerrain (const ghost::terrain::Terrain& t, const juce::String& state)
{
    const Shaping s = makeShaping (t, m_sr, soundBrightness());
    { const juce::ScopedLock l (m_modelLock); m_shapeMsg = s; m_terrainMsg = t; m_terrainState = state; }
    { const juce::SpinLock::ScopedLockType l (m_shapeLock); m_shapePosted = s; }
    m_shapeGen.fetch_add (1);
}

std::vector<float> Engine::soundBrightness() const
{
    // a sound's brightness = its pieces' pitch (the engine's dominant frequency), level-weighted, as an x across the land
    std::vector<float> x;
    auto pic = libraryPicture();
    if (pic == nullptr) return x;
    const size_t n = pic->soundNames.size();
    std::vector<double> sum (n, 0.0), w (n, 0.0);
    for (auto& d : pic->dots)
        if (d.sound >= 0 && (size_t) d.sound < n) { const double lw = 0.05 + d.level; sum[(size_t) d.sound] += lw * d.x; w[(size_t) d.sound] += lw; }
    for (size_t k = 0; k < n; ++k)
    {
        const float f = w[k] > 0 ? (float) (sum[k] / w[k]) : 0.5f;
        x.push_back (ghost::terrain::hzToX (20.0f * std::pow (2.0f, 10.0f * f), terrainRange()));   // (makeLibraryPicture: f = log2 (freq / 20) / 10)
    }
    return x;
}

void Engine::applySoundMask()
{
    if (m_source == nullptr) return;
    const bool on = m_shapeLive.maskOn;
    if (! on && ! m_maskApplied && ! m_maskDirty) return;
    auto& list = const_cast<std::list<brain::sound>&> (m_source->get_samples());
    int k = 0;
    for (auto it = list.begin(); it != list.end(); ++it, ++k)
    {
        const bool user = k < m_userCount ? m_userOn[k] : it->m_enabled;
        it->m_enabled = user && (! on || m_shapeLive.allows (k));
    }
    m_maskApplied = on;
    m_maskDirty = false;
}

Engine::ShapeNow Engine::shapeNow() const
{
    ShapeNow n;
    n.weights = m_shWeights.load(); n.time = m_shTime.load();
    n.novelty = m_shNov.load(); n.boredom = m_shBor.load(); n.wild = m_shWild.load(); n.pos = m_shPos.load(); n.tilt = m_shTilt.load();
    return n;
}

void Engine::updateShaping (int numSamples)
{
    // audio thread: pick up the newest landscape (never waits: a busy lock is tried again next buffer)
    const int gen = m_shapeGen.load();
    if (gen != m_shapeSeen)
    {
        const juce::SpinLock::ScopedTryLockType l (m_shapeLock);
        if (l.isLocked()) { m_shapeTarget = m_shapePosted; m_shapeSeen = gen; }
    }
    auto& T = m_shapeTarget;
    auto& L = m_shapeLive;
    if (T.active() || L.active())
    {
        // glide there (~50 ms), so a drag never jumps the matching about; flat again = exactly off
        const float a = 1.0f - std::exp (-(float) numSamples / (float) (0.05 * m_sr));
        float dw = 0, dt = 0;
        if (T.weightsOn || L.weightsOn)
        {
            for (int b = 0; b < Shaping::kBands; ++b)
            {
                const float to = T.weightsOn ? T.weights[b] : 1.0f;
                L.weights[b] += (to - L.weights[b]) * a;
                dw = juce::jmax (dw, std::abs (to - L.weights[b]));
            }
            if (T.weightsOn) L.weightsOn = true;
            else if (dw < 1e-3f) { L.weightsOn = false; for (auto& w : L.weights) w = 1.0f; }
        }
        if (T.timeOn || L.timeOn)
        {
            for (int j = 0; j < Shaping::kSteps; ++j)
            {
                const float to = T.timeOn ? T.wild[j] : 0.0f;
                L.wild[j] += (to - L.wild[j]) * a;
                dt = juce::jmax (dt, std::abs (to - L.wild[j]));
            }
            if (T.timeOn) L.timeOn = true;
            else if (dt < 1e-3f) { L.timeOn = false; for (auto& w : L.wild) w = 0.0f; }
        }
    }
    // the whole-landscape moves glide the same way
    {
        const float a = 1.0f - std::exp (-(float) numSamples / (float) (0.05 * m_sr));
        if (T.macrosOn || L.macrosOn)
        {
            float d = 0;
            auto glide = [&] (float& v, float to) { v += (to - v) * a; d = juce::jmax (d, std::abs (to - v)); };
            glide (L.smooth, T.macrosOn ? T.smooth : 0.0f); glide (L.stutter, T.macrosOn ? T.stutter : 0.0f);
            glide (L.blend, T.macrosOn ? T.blend : 0.0f);
            if (T.macrosOn) L.macrosOn = true;
            else if (d < 1e-3f) { L.macrosOn = false; L.smooth = L.stutter = L.blend = 0; }
        }
        // build 15: the search moves (TONE / TIMBRE, SHAPE / PITCH ONLY, SEARCH STYLE) glide the same way
        if (T.searchOn || L.searchOn)
        {
            float d = 0;
            auto glide = [&] (float& v, float to) { v += (to - v) * a; d = juce::jmax (d, std::abs (to - v)); };
            glide (L.tone, T.searchOn ? T.tone : 0.0f); glide (L.timbre, T.searchOn ? T.timbre : 0.0f);
            glide (L.pitch, T.searchOn ? T.pitch : 0.0f); glide (L.net, T.searchOn ? T.net : 0.0f);
            glide (L.energy, T.searchOn ? T.energy : 0.0f); glide (L.valley, T.searchOn ? T.valley : 0.0f); glide (L.tall, T.searchOn ? T.tall : 0.0f);
            if (T.searchOn) L.searchOn = true;
            else if (d < 1e-3f) { L.searchOn = false; L.tone = L.timbre = L.pitch = L.net = L.energy = L.valley = L.tall = 0; }
        }
        // the lows / mids / highs under each moment of the target glide the same way
        if (T.zonesOn || L.zonesOn)
        {
            float d = 0;
            for (int j = 0; j < Shaping::kSteps; ++j)
            {
                const float a2 = a;
                auto gl = [&] (float& v, float to) { v += (to - v) * a2; d = juce::jmax (d, std::abs (to - v)); };
                gl (L.zl[j], T.zonesOn ? T.zl[j] : 0.0f); gl (L.zm[j], T.zonesOn ? T.zm[j] : 0.0f); gl (L.zh[j], T.zonesOn ? T.zh[j] : 0.0f);
            }
            if (T.zonesOn) L.zonesOn = true;
            else if (d < 1e-3f) { L.zonesOn = false; for (auto& w : L.zl) w = 0; for (auto& w : L.zm) w = 0; for (auto& w : L.zh) w = 0; }
        }
        // which sounds are in play: a switch, not a glide
        bool maskChanged = L.maskOn != T.maskOn;
        if (! maskChanged && T.maskOn) for (int i = 0; i < Shaping::kMaxSounds / 32 && ! maskChanged; ++i) maskChanged = L.mask[i] != T.mask[i];
        if (maskChanged)
        {
            L.maskOn = T.maskOn; L.inPlay = T.inPlay; L.ofSounds = T.ofSounds;
            for (int i = 0; i < Shaping::kMaxSounds / 32; ++i) L.mask[i] = T.mask[i];
            m_maskDirty = true;
        }
        if (m_maskDirty) applySoundMask();
    }
    // ---- the controls TERRAIN moves (build 15: every original control that can change live without a rebuild). Each is moved from
    // the value you set by the land: its global character (stutter, smooth, blend, tone, timbre, pitch, net), what stands in the
    // lows / mids / highs under the playhead (zl / zm / zh), and MOVEMENT's random wandering (tempo-synced steps or free).
    // The same engine settings the Qt app's sliders write; flat land with MOVEMENT's wandering off = none of this runs.
    {
        const float dragStutter = 0.7f * m_mBurst;
        const float amount = juce::jlimit (0.0f, 1.0f, P ("move_amount"));
        const float wa = amount * L.energy;                   // flat land = no wandering (it has nothing to wander)
        const bool wander = wa > 0.001f;
        const bool any = L.macrosOn || L.searchOn || L.zonesOn || dragStutter > 0.0f || wander;
        if (any)
        {
            stepWander (numSamples, wander);
            const float pos = m_left.shapedPosition();
            const float zl = L.zonesOn ? L.zoneAt (L.zl, pos) : 0.0f, zm = L.zonesOn ? L.zoneAt (L.zm, pos) : 0.0f, zh = L.zonesOn ? L.zoneAt (L.zh, pos) : 0.0f;
            auto W = [&] (Live i) { return m_wCur[i] * wa; };      // -1..1 times how lively
            auto clamp01 = [] (float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
            const float s0 = q100 (P ("stickyness")), t0 = q100 (P ("target_mix")), r0 = q100 (P ("ratio")), n0 = q100 (P ("n_ratio"));
            const float a0 = q100 (P ("autotune")), nm0 = q100 (P ("n_mix"));
            const u32 rp0 = (u32) juce::jmax (1L, std::lround (P ("stretch")));
            const int fs0 = (int) std::lround (P ("fft_start")), fe0 = (int) std::lround (P ("fft_end"));
            const int sy0 = (int) std::lround (P ("synapses"));
            const int se0 = (int) std::lround (P ("slide_error"));
            const int algo0 = (int) juce::jlimit (0L, 3L, std::lround (P ("algo")));
            const float stut = juce::jmin (1.0f, L.stutter + dragStutter);
            // SMOOTH: plateaus and mids raise it, spires and the drag's stutter lower it
            float st = s0 < 0.9f ? s0 + (0.9f - s0) * L.smooth : s0;
            st = clamp01 (st + 0.4f * zm * (1.0f - s0) + W (kLiveSmooth) * 0.5f);
            st *= 1.0f - 0.85f * stut;
            // REPEAT: spires, raised highs
            u32 rep = (u32) juce::jlimit (1L, 50L, (long) rp0 + std::lround (3.0f * stut) + std::lround (3.0f * juce::jmax (0.0f, zh)) + std::lround (W (kLiveRepeat) * 4.0f));
            // ORIGINAL MIX: merging mountains, raised lows
            const float tm = juce::jmin (juce::jmin (1.0f, t0 + 0.4f), clamp01 (t0 + (juce::jmin (1.0f, t0 + 0.4f) - t0) * L.blend + 0.2f * juce::jmax (0.0f, zl) + W (kLiveMix) * 0.2f));   // never more than the BLEND ceiling (+0.4): level-safe
            // TONE / TIMBRE: weighted land leans to TONE, rough land and raised highs to TIMBRE
            float r = r0 * (1.0f - 0.7f * L.tone);
            r = clamp01 (r + (1.0f - r) * 0.6f * L.timbre + 0.3f * zh + W (kLiveRatio) * 0.4f);
            // SHAPE / PITCH ONLY: plateaus, merges, raised lows
            float nr = clamp01 (n0 + (1.0f - n0) * 0.8f * L.pitch + 0.3f * zl + W (kLiveNRatio) * 0.4f);
            // LOWEST / HIGHEST BAND: a valley in the lows (highs) stops the matching counting them
            int fs = fs0 + (int) std::lround (35.0f * juce::jmax (0.0f, -zl)) + (int) std::lround (W (kLiveFftStart) * 15.0f);
            int fe = fe0 - (int) std::lround (40.0f * juce::jmax (0.0f, -zh)) + (int) std::lround (W (kLiveFftEnd) * 15.0f);
            fs = juce::jlimit (0, 90, fs); fe = juce::jlimit (juce::jmin (100, fs + 8), 100, fe);
            // CONNECTIONS / WAIT UNTIL CLOSE (the Network styles): plateaus narrow the neighbourhood, spires widen it; the highs ask for a closer match
            int sy = (int) juce::jlimit (1L, 1000L, std::lround ((float) sy0 * (1.0f - 0.6f * L.smooth + 1.0f * L.stutter) * std::exp (W (kLiveSynapses) * 0.7f)));
            int se = (int) juce::jlimit (0L, 10000L, std::lround ((float) se0 * std::exp (0.8f * zl - 0.8f * zh + W (kLiveSlideError) * 0.8f)));
            // AUTOTUNE: the raised highs pull the pieces towards the target's pitch, gently
            // (only when you've turned AUTOTUNE up: any amount switches the whole pitch-matching path on, which adds clicks and peaks from a standing start)
            float at = a0 < 0.02f ? a0 : clamp01 (a0 + 0.12f * juce::jmax (0.0f, zh) + juce::jmax (0.0f, W (kLiveAutotune)) * 0.12f);
            // FLATTEN: a valley in the mids flattens the loudness
            // (same for FLATTEN: only when you've turned it up)
            float nm = nm0 < 0.02f ? nm0 : clamp01 (nm0 + 0.3f * juce::jmax (0.0f, -zm) + juce::jmax (0.0f, W (kLiveNMix)) * 0.2f);
            // SEARCH STYLE: wide plateaus take Closest to Network (needs the library's links; otherwise it stays as set)
            const bool links = m_source != nullptr && m_source->get_num_blocks() > 1 && m_source->get_block (0).get_synapse_const().size() > 1;
            if (algo0 == 0 && links) { if (L.net > 0.5f) m_netNow = true; else if (L.net < 0.3f) m_netNow = false; }
            else m_netNow = false;
            for (auto* rr : { &m_left, &m_right })
            {
                auto* p = rr->get_params();
                p->m_stickyness = st; rr->set_stretch (rep); rr->glideTargetMix (tm);
                p->m_ratio = r; p->m_n_ratio = nr; p->m_fft1_start = (u32) fs; p->m_fft1_end = (u32) fe; p->m_num_synapses = (u32) sy;
                rr->set_slide_error ((double) se); rr->set_autotune (at); rr->set_n_mix (nm);
                rr->set_search_algo (m_netNow ? RtRenderer::search_algo (2) : (RtRenderer::search_algo) algo0);
            }
            m_macrosApplied = true;
        }
        else if (m_macrosApplied)
        {
            // flat again (and no wandering): the knobs' own values, the same expressions applyParams uses
            for (auto* rr : { &m_left, &m_right })
            {
                auto* p = rr->get_params();
                p->m_stickyness = q100 (P ("stickyness"));
                rr->set_stretch ((u32) juce::jmax (1L, std::lround (P ("stretch"))));
                rr->glideTargetMix (q100 (P ("target_mix")));
                p->m_ratio = q100 (P ("ratio")); p->m_n_ratio = q100 (P ("n_ratio"));
                p->m_fft1_start = (u32) std::lround (P ("fft_start")); p->m_fft1_end = (u32) std::lround (P ("fft_end"));
                p->m_num_synapses = (u32) std::lround (P ("synapses"));
                rr->set_slide_error ((double) (int) std::lround (P ("slide_error"))); rr->set_autotune (q100 (P ("autotune"))); rr->set_n_mix (q100 (P ("n_mix")));
                rr->set_search_algo ((RtRenderer::search_algo) juce::jlimit (0L, 3L, std::lround (P ("algo"))));
            }
            m_netNow = false;
            m_macrosApplied = false;
        }
    }
    // the brightness tilt glides like the rest; flat again = exactly off
    {
        const float a = 1.0f - std::exp (-(float) numSamples / (float) (0.05 * m_sr));
        if (T.tilt != 0.0f || L.tilt != 0.0f)
        {
            L.tilt += (T.tilt - L.tilt) * a;
            if (T.tilt == 0.0f && std::abs (L.tilt) < 1e-3f) L.tilt = 0.0f;
        }
    }
    // DRAG: a fast pull is a burst (heard within ~20 ms), which dies away (0.35 s time constant) once it stops
    {
        const float b = m_dragBurst.load(), sd = m_dragSide.load(), lf = m_dragLift.load();
        auto follow = [&] (float& v, float to)
        {
            const bool rising = std::abs (to) > std::abs (v);
            const float k = 1.0f - std::exp (-(float) numSamples / (float) ((rising ? 0.02 : 0.35) * m_sr));
            v += (to - v) * k;
            if (to == 0.0f && std::abs (v) < 5e-3f) v = 0.0f;   // (gone ~1.9 s after you stop)
        };
        follow (m_mBurst, b); follow (m_mSide, sd); follow (m_mLift, lf);
        // up = push it wilder, down = pull it back towards the target; speed is how much
        L.motionWild = m_mBurst == 0.0f && m_mLift == 0.0f ? 0.0f
                     : juce::jlimit (-1.0f, 1.0f, 0.8f * m_mBurst * (m_mLift >= 0.0f ? 1.0f : 0.25f) + 0.5f * m_mLift);
        // MOVEMENT: the random wandering also moves NOVELTY and BOREDOM
        if (const float wn = m_wCur[kLiveNovelty] * juce::jlimit (0.0f, 1.0f, P ("move_amount")) * L.energy * 0.5f; wn != 0.0f)
            L.motionWild = juce::jlimit (-1.0f, 1.0f, L.motionWild + wn);
        m_shDrag = juce::jmax (m_mBurst, std::abs (m_mSide), std::abs (m_mLift));
    }
    updateBias();
    const bool on = L.active() || m_biasTilt != 0.0f;
    const float nov = q100 (P ("novelty")), bor = q100 (P ("boredom"));
    for (auto* r : { &m_left, &m_right })
    {
        r->setShaping (on ? &L : nullptr);
        r->setShapingBase (nov, bor);
        if (m_live != nullptr) r->setShapingSpan ((u32) juce::jmax (1.0, 8.0 * m_sr / juce::jmax (1u, m_live->hop())));   // the effect: an 8-second loop
    }
    m_shWeights = L.weightsOn; m_shTime = L.timeShaped(); m_shTilt = m_biasTilt;
    m_shNov = m_left.shapedNovelty(); m_shBor = L.timeOn ? Shaping::boredom (bor, m_left.shapedWild()) : bor;
    m_shWild = m_left.shapedWild(); m_shPos = m_left.shapedPosition();
    publishLive();
}

// ---- MOVING NOW (build 15): what TERRAIN is doing to the original controls, for the screen
static const char* const kLiveIds[Engine::kLiveCount] = { "novelty", "boredom", "stickyness", "stretch", "target_mix", "ratio", "n_ratio", "algo",
                                                          "fft_start", "fft_end", "synapses", "slide_error", "autotune", "n_mix" };
int Engine::liveIndexOf (const juce::String& id) { for (int i = 0; i < kLiveCount; ++i) if (id == kLiveIds[i]) return i; return -1; }
const char* Engine::liveParamId (int live) { return live >= 0 && live < kLiveCount ? kLiveIds[live] : ""; }

Engine::LiveValue Engine::liveValue (int live) const
{
    LiveValue v;
    if (live < 0 || live >= kLiveCount) return v;
    v.set = m_liveSet[live].load(); v.now = m_liveNow[live].load(); v.moving = ((m_liveMoving.load() >> live) & 1u) != 0;
    return v;
}

bool Engine::soundBenched (int k) const
{
    return m_playMaskOn.load() && k >= 0 && k < Shaping::kMaxSounds && ((m_playBits[k >> 5].load() >> (k & 31)) & 1u) == 0;
}

// MOVEMENT (build 15): each moved control wanders on its own. In a tempo-synced rate every control takes a new random target on the
// beat grid (1 beat .. 4 bars; the host's position when it gives one, else a clock at its tempo), in FREE mode each at its own random
// time. The value glides to the target (no steps, so no zipper). Audio thread; no allocation.
void Engine::stepWander (int n, bool wander)
{
    const double bpm = juce::jlimit (30.0, 300.0, m_hostBpm.load());
    const int rate = (int) juce::jlimit (0L, 5L, std::lround (P ("move_rate")));
    static const double beats[6] = { 0, 1, 2, 4, 8, 16 };
    auto next = [&]() { m_wRng ^= m_wRng << 13; m_wRng ^= m_wRng >> 17; m_wRng ^= m_wRng << 5; return (float) (m_wRng & 0xffffff) / (float) 0x7fffff - 1.0f; };   // -1..1
    const double dBeats = (double) n / m_sr * bpm / 60.0;
    if (m_hostPpqValid.load() && m_hostPlayingUi.load()) m_wBeats = m_hostPpq.load(); else m_wBeats += dBeats;
    const long step = rate > 0 ? (long) std::floor (m_wBeats / beats[rate]) : 0;
    const bool newStep = rate > 0 && step != m_wStep;
    if (rate > 0) m_wStep = step;
    for (int i = 0; i < kLiveCount; ++i)
    {
        float period;
        if (rate > 0) { if (newStep || m_wTimer[i] == 0) { m_wTgt[i] = wander ? next() : 0.0f; m_wTimer[i] = 1; } period = (float) (beats[rate] * 60.0 / bpm * m_sr); }
        else
        {
            m_wTimer[i] -= n;
            if (m_wTimer[i] <= 0) { m_wTgt[i] = wander ? next() : 0.0f; m_wTimer[i] = (int) ((0.3f + 0.7f * (next() * 0.5f + 0.5f) * 2.0f) * (float) m_sr); }
            period = (float) (0.9 * m_sr);
        }
        if (! wander) m_wTgt[i] = 0.0f;
        const float k = 1.0f - std::exp (-(float) n / juce::jmax (1.0f, 0.22f * period));
        m_wCur[i] += (m_wTgt[i] - m_wCur[i]) * k;
        if (m_wTgt[i] == 0.0f && std::abs (m_wCur[i]) < 1e-3f) m_wCur[i] = 0.0f;
    }
}

void Engine::publishLive()
{
    // audio thread: lock-free, no allocation (atomics only)
    const auto& L = m_shapeLive;
    const auto* pl = m_left.get_params();
    const float nov = q100 (P ("novelty")), bor = q100 (P ("boredom"));
    const bool shaped = L.active() || m_biasTilt != 0.0f;
    const float setv[kLiveCount] = { nov, bor, q100 (P ("stickyness")), (float) juce::jmax (1L, std::lround (P ("stretch"))), q100 (P ("target_mix")),
                                     q100 (P ("ratio")), q100 (P ("n_ratio")), (float) juce::jlimit (0L, 3L, std::lround (P ("algo"))),
                                     (float) std::lround (P ("fft_start")), (float) std::lround (P ("fft_end")), (float) std::lround (P ("synapses")),
                                     (float) std::lround (P ("slide_error")), q100 (P ("autotune")), q100 (P ("n_mix")) };
    const float nowv[kLiveCount] = { m_left.shapedNovelty(), L.timeShaped() ? Shaping::boredom (bor, m_left.shapedWild()) : bor, pl->m_stickyness,
                                     (float) m_left.get_stretch(), m_left.get_target_mix(), pl->m_ratio, pl->m_n_ratio, (float) (int) m_left.get_search_algo(),
                                     (float) pl->m_fft1_start, (float) pl->m_fft1_end, (float) pl->m_num_synapses, (float) m_left.get_slide_error(),
                                     m_left.get_autotune(), m_left.get_n_mix() };
    unsigned moving = 0;
    for (int i = 0; i < kLiveCount; ++i)
    {
        const float eps = (i == kLiveRepeat || i == kLiveAlgo || i >= kLiveFftStart && i <= kLiveSlideError) ? 0.5f : 0.004f;
        const float nv = shaped ? nowv[i] : setv[i];
        m_liveSet[i].store (setv[i], std::memory_order_relaxed); m_liveNow[i].store (nv, std::memory_order_relaxed);
        if (shaped && std::abs (nv - setv[i]) > eps) moving |= 1u << i;
    }
    m_liveMoving.store (moving, std::memory_order_relaxed);
    for (int i = 0; i < Shaping::kMaxSounds / 32; ++i) m_playBits[i].store (L.maskOn ? L.mask[i] : 0xffffffffu, std::memory_order_relaxed);
    m_playMaskOn.store (L.maskOn, std::memory_order_relaxed);
}

void Engine::observe (void* ctx, juce::uint32 src, juce::uint32 tgt, juce::uint32 time)
{
    auto* c = static_cast<ObsCtx*> (ctx);
    auto& e = *c->e;
    int s1, n1, s2, n2;
    e.m_evFifo.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 > 0) { e.m_evSlots[s1] = { src, tgt, time, (juce::uint8) c->ch }; e.m_evFifo.finishedWrite (1); }
    if (c->ch == 0) e.m_curTgt.store ((int) tgt, std::memory_order_relaxed);
}

int Engine::popEvents (Event* out, int max)
{
    int s1, n1, s2, n2;
    m_evFifo.prepareToRead (max, s1, n1, s2, n2);
    for (int i = 0; i < n1; ++i) out[i] = m_evSlots[s1 + i];
    for (int i = 0; i < n2; ++i) out[n1 + i] = m_evSlots[s2 + i];
    m_evFifo.finishedRead (n1 + n2);
    return n1 + n2;
}

void Engine::pushCmd (Cmd c)
{
    const juce::ScopedLock l (m_cmdWriteLock);
    int s1, n1, s2, n2;
    m_cmdFifo.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 > 0) { m_cmdSlots[s1] = c; m_cmdFifo.finishedWrite (1); }
}

void Engine::publish (std::unique_ptr<Swap> s)
{
    if (s->source != nullptr && s->info == nullptr)
    {
        // the new library's per-piece brightness (from its picture, made just before on this thread)
        s->info = new PieceInfo();
        if (auto pic = libraryPicture(); pic != nullptr && pic->bright.size() == (size_t) s->source->get_num_blocks()) s->info->bright = pic->bright;
        else { std::vector<float> hz; pieceBrightness (*s->source, m_sr, s->info->bright, hz); }
        s->info->factor.assign (s->info->bright.size(), 1.0f);
    }
    if (s->source != nullptr) s->brainGen = m_tapPendingBrain;    // (the tap: which snapshot describes it)
    if (s->left != nullptr) s->targetGen = m_tapPendingTarget;
    for (;;)
    {
        int s1, n1, s2, n2;
        m_swapFifo.prepareToWrite (1, s1, n1, s2, n2);
        if (n1 > 0) { m_swapSlots[s1] = s.release(); m_swapFifo.finishedWrite (1); return; }
        juce::Thread::sleep (2);   // the audio thread hasn't picked up the last ones yet
        if (! m_audioBusy.load()) { withExclusiveWorld ([] {}); }
    }
}

void Engine::drainQueues()
{
    auto retire = [this] (Retired r)
    {
        int s1, n1, s2, n2;
        m_retireFifo.prepareToWrite (1, s1, n1, s2, n2);
        if (n1 > 0) { m_retireSlots[s1] = r; m_retireFifo.finishedWrite (1); }
        else jassertfalse;   // (64 deep: the worker frees them every 20 ms)
    };
    for (;;)
    {
        int s1, n1, s2, n2;
        m_swapFifo.prepareToRead (1, s1, n1, s2, n2);
        if (n1 == 0) break;
        Swap* s = m_swapSlots[s1];
        m_swapFifo.finishedRead (1);
        Retired r;
        if (s->source != nullptr)
        {
            r.b = m_source; r.g = m_graveyard;
            m_source = s->source; m_graveyard = s->graveyard;
            m_left.setSource (m_source); m_right.setSource (m_source);
            r.info = m_info; m_info = s->info; s->info = nullptr;
            m_tap.audio.brainGen = s->brainGen;
            m_biasTilt = 0; m_left.setBlockBias (nullptr, 0); m_right.setBlockBias (nullptr, 0);   // (made again for this library)
            {
                // your switches as the new library was built with them (the landscape's pick goes on top)
                m_userCount = 0;
                for (auto& snd : m_source->get_samples()) if (m_userCount < Shaping::kMaxSounds) m_userOn[m_userCount++] = snd.m_enabled;
                m_maskDirty = m_maskApplied || m_shapeLive.maskOn;
            }
            m_source->set_usage_falloff (q100 (P ("boredom")));
        }
        if (s->left != nullptr)
        {
            Retired rt; rt.b = m_tl; retire (rt);
            Retired rr; rr.b = m_tr; retire (rr);
            m_tl = s->left; m_tr = s->right;
            m_tap.audio.targetGen = s->targetGen;
        }
        if (s->setLive)
        {
            r.live = m_live;
            m_live = s->live;
            m_livePending = true;
        }
        if (! (m_isEffect && m_live != nullptr && P ("live_input") > 0.5f))
        {
            m_left.setTargetSource (m_tl); m_right.setTargetSource (m_tr);
        }
        else
        {
            m_left.setTargetSource (&m_live->left()); m_right.setTargetSource (&m_live->right());
        }
        if (s->hasRendererState)
        {
            // a session restores both renderers as they were saved (process_thread::load_session)
            m_left.copyStateFrom (*s->stateL); m_right.copyStateFrom (*s->stateR);
            m_running = m_left.get_playing(); m_playingUi = m_running;
        }
        if (s->resetRenderers) { m_left.reset(); m_right.reset(); m_livePending = true; }
        r.swap = s;
        retire (r);
    }
    for (;;)
    {
        int s1, n1, s2, n2;
        m_cmdFifo.prepareToRead (1, s1, n1, s2, n2);
        if (n1 == 0) break;
        const Cmd c = m_cmdSlots[s1];
        m_cmdFifo.finishedRead (1);
        switch (c.type)
        {
            case Cmd::Play: m_playingUi = true; break;
            case Cmd::Pause: m_playingUi = false; break;
            case Cmd::Restart: m_left.reset(); m_right.reset(); m_livePending = true; break;
            case Cmd::Enable:
            case Cmd::Delete:
            {
                if (m_source == nullptr) break;
                auto& list = const_cast<std::list<brain::sound>&> (m_source->get_samples());
                int k = 0;
                for (auto it = list.begin(); it != list.end(); ++it, ++k)
                    if (k == c.a)
                    {
                        if (c.type == Cmd::Enable)
                        {
                            it->m_enabled = c.b != 0 && (! m_shapeLive.maskOn || m_shapeLive.allows (k));
                            if (k < m_userCount) m_userOn[k] = c.b != 0;
                        }
                        else
                        {
                            if (m_graveyard != nullptr) m_graveyard->splice (m_graveyard->end(), list, it);   // as brain::delete_sound, without freeing here
                            if (k < m_userCount) { for (int j = k; j + 1 < m_userCount; ++j) m_userOn[j] = m_userOn[j + 1]; --m_userCount; }
                            m_maskDirty = m_maskApplied;
                        }
                        break;
                    }
                break;
            }
            case Cmd::Clear:
                if (m_source != nullptr && m_graveyard != nullptr)
                {
                    auto& list = const_cast<std::list<brain::sound>&> (m_source->get_samples());
                    m_graveyard->splice (m_graveyard->end(), list);
                    m_userCount = 0;
                }
                break;
            case Cmd::Rest:
                if (m_source != nullptr && c.a >= 0 && (u32) c.a < m_source->get_num_blocks())
                {
                    const_cast<block&> (m_source->get_block ((u32) c.a)).get_usage() += 1000.0f;   // brain.cpp usage_factor
                    tapRested ((u32) c.a);
                }
                break;
        }
    }
}

void Engine::freeRetired()
{
    for (;;)
    {
        int s1, n1, s2, n2;
        m_retireFifo.prepareToRead (1, s1, n1, s2, n2);
        if (n1 == 0) break;
        Retired r = m_retireSlots[s1];
        m_retireFifo.finishedRead (1);
        delete r.b; delete r.g; delete r.live; delete r.swap; delete r.info;
    }
}

template <class F> bool Engine::withExclusiveWorld (F&& f)
{
    m_exclusiveRequest.store (true);
    const auto end = juce::Time::getMillisecondCounter() + 4000;
    while (m_audioBusy.load() && ! m_exclusiveGranted.load() && juce::Time::getMillisecondCounter() < end)
        juce::Thread::sleep (1);
    const bool ok = ! m_audioBusy.load() || m_exclusiveGranted.load();
    if (ok) { drainQueues(); f(); }
    m_exclusiveRequest.store (false);
    return ok;
}

void Engine::applyParams (bool force)
{
    auto& t = paramTable();
    float now[64];
    for (size_t i = 0; i < t.size(); ++i) now[i] = m_raw[i] != nullptr ? m_raw[i]->load() : t[i].def;
    auto changed = [&] (const char* id) { const int i = pidx (id); return force || ! m_applied.valid || now[i] != m_applied.v[i]; };
    auto v = [&] (const char* id) { return now[pidx (id)]; };
    RtRenderer* rs[2] = { &m_left, &m_right };
    for (auto* r : rs)
    {
        auto* p = r->get_params();
        if (changed ("ratio")) p->m_ratio = q100 (v ("ratio"));
        if (changed ("n_ratio")) p->m_n_ratio = q100 (v ("n_ratio"));
        if (changed ("fft_start")) p->m_fft1_start = (u32) std::lround (v ("fft_start"));
        if (changed ("fft_end")) p->m_fft1_end = (u32) std::lround (v ("fft_end"));
        if (changed ("novelty")) p->m_usage_importance = q100 (v ("novelty"));
        if (changed ("stickyness")) p->m_stickyness = q100 (v ("stickyness"));
        if (changed ("synapses")) p->m_num_synapses = (u32) std::lround (v ("synapses"));
        if (changed ("autotune")) r->set_autotune (q100 (v ("autotune")));
        if (changed ("volume")) r->set_volume (q100 (v ("volume")) * 10);
        if (changed ("algo")) r->set_search_algo ((RtRenderer::search_algo) juce::jlimit (0, 3, (int) std::lround (v ("algo"))));
        if (changed ("n_mix")) r->set_n_mix (q100 (v ("n_mix")));
        if (changed ("target_mix")) r->set_target_mix (q100 (v ("target_mix")));
        if (changed ("stretch")) r->set_stretch ((u32) juce::jmax (1L, std::lround (v ("stretch"))));
        if (changed ("slide_error")) r->set_slide_error ((double) (int) std::lround (v ("slide_error")));
    }
    if (changed ("boredom") && m_source != nullptr) m_source->set_usage_falloff (q100 (v ("boredom")));
    if (m_applied.valid && ! force && now[pidx ("stereo")] != m_applied.v[pidx ("stereo")])
    {
        m_left.reset(); m_right.reset(); m_livePending = true;   // as the Qt app's /stereo
    }
    if (m_isEffect && (changed ("live_input")))
    {
        if (m_live != nullptr && v ("live_input") > 0.5f) { m_left.setTargetSource (&m_live->left()); m_right.setTargetSource (&m_live->right()); }
        else { m_left.setTargetSource (m_tl); m_right.setTargetSource (m_tr); }
        m_left.reset(); m_right.reset(); m_livePending = true;
    }
    for (size_t i = 0; i < t.size(); ++i) m_applied.v[i] = now[i];
    m_applied.valid = true;
}

void Engine::startPlaying (bool fromTop)
{
    if (fromTop) { m_left.reset(); m_right.reset(); m_livePending = true; }
    m_left.set_playing (true); m_right.set_playing (true);
    m_running = true;
    m_stopAfterFade = false;
    const bool declick = P ("declick") > 0.5f;
    if (declick && (fromTop || m_fade >= 1.0f)) m_fade = 0;
    m_fadeTarget = 1;
    if (! declick) m_fade = 1;
}

void Engine::stopPlaying()
{
    if (P ("declick") > 0.5f && m_fade > 0) { m_fadeTarget = 0; m_stopAfterFade = true; }
    else { m_left.set_playing (false); m_right.set_playing (false); m_running = false; m_fade = 1; }
}

void Engine::process (juce::AudioBuffer<float>& io, juce::MidiBuffer& midi, bool hostPlaying, bool hostInfoValid)
{
    juce::ScopedNoDenormals nd;
    const int n = io.getNumSamples();
    const int outCh = io.getNumChannels();
    m_audioBusy.store (true);
    if (m_exclusiveRequest.load())
    {
        m_exclusiveGranted.store (true);
        if (m_tap.audio.on) ++m_tap.audio.parked;   // (the tap: a buffer spent waiting for a library job)
        io.clear();
        m_audioBusy.store (false);
        return;
    }
    m_exclusiveGranted.store (false);

    drainQueues();
    applyParams (false);
    updateShaping (n);

    const bool stereo = P ("stereo") > 0.5f;
    const bool live = m_isEffect && m_live != nullptr && P ("live_input") > 0.5f;
    const int mode = (int) std::lround (P ("play_mode"));

    // ---- MIDI (instrument)
    int lastNote = -1;
    for (const auto m : midi)
    {
        const auto msg = m.getMessage();
        if (msg.isNoteOn()) { ++m_notesDown; lastNote = msg.getNoteNumber(); m_velocity = msg.getFloatVelocity(); if (mode == 1) startPlaying (true); }
        else if (msg.isNoteOff()) m_notesDown = juce::jmax (0, m_notesDown - 1);
        else if (msg.isAllNotesOff() || msg.isAllSoundOff()) m_notesDown = 0;
    }
    if (lastNote >= 0 && P ("key_pitch") > 0.5f)
    {
        const float r = std::pow (2.0f, (float) (lastNote - 60) / 12.0f);
        m_left.setKeyPitch (r); m_right.setKeyPitch (r);
    }
    else if (P ("key_pitch") <= 0.5f) { m_left.setKeyPitch (1); m_right.setKeyPitch (1); }

    // ---- what should be playing
    bool want = m_playingUi.load();
    if (mode == 1) want = m_notesDown > 0;
    if (mode == 2 && hostInfoValid) want = hostPlaying;
    const bool hostStarted = mode == 2 && hostPlaying && ! m_lastHostPlaying;
    m_lastHostPlaying = hostPlaying;
    if (want && (! m_running || m_stopAfterFade)) startPlaying (hostStarted);
    else if (! want && m_running && ! m_stopAfterFade) stopPlaying();
    const bool tapOn = tapBegin (n, lastNote, stereo, mode, hostPlaying, hostInfoValid);   // (the tap: read-only, Tap.cpp)

    // ---- render
    auto* L = m_scratch.getWritePointer (0);
    auto* R = m_scratch.getWritePointer (1);
    for (int done = 0; done < n;)
    {
        const int len = juce::jmin (n - done, m_scratch.getNumSamples());
        juce::FloatVectorOperations::clear (L, len);
        juce::FloatVectorOperations::clear (R, len);
        const float* inL = io.getReadPointer (0, done);
        const float* inR = io.getReadPointer (juce::jmin (1, io.getNumChannels() - 1), done);

        if (live)
        {
            const int64_t inStart = m_live->written();
            m_live->push (inL, inR, len);
            const int liveLat = m_live->latencySamples();
            const int64_t o0 = inStart - liveLat;
            const int64_t hop = m_live->hop();
            int off = 0;
            bool go = m_running;
            if (go && m_livePending)
            {
                // start piece 0 exactly on a hop boundary of the output time line, so the latency is exact
                int64_t t = juce::jmax<int64_t> (0, o0);
                t = ((t + hop - 1) / hop) * hop;
                if (t < o0 + len)
                {
                    off = (int) (t - o0);
                    m_left.reset(); m_right.reset();
                    m_left.set_playing (true); m_right.set_playing (true);
                    m_left.primeShift ((u32) hop); m_right.primeShift ((u32) hop);
                    m_live->setBase (t / hop);
                    m_liveOutStart = t;
                    m_livePending = false;
                }
                else go = false;
            }
            if (go && ! m_livePending)
            {
                const int segLen = len - off;
                // keep the target in step with the input (stretch / slide can drift it)
                const int64_t base = m_liveOutStart / hop;
                const float tt = m_left.getTargetTime();
                const int64_t needEnd = base + (int64_t) ((tt + (float) segLen) / (float) hop);
                const int64_t latest = m_live->latestReady();
                const int64_t realNow = (o0 + off + segLen) / hop;
                if (needEnd > latest) { m_live->noteLate(); const float h = (float) juce::jmax<int64_t> (0, (latest - base) * hop - segLen); m_left.holdTargetTime (h); m_right.holdTargetTime (h); }
                else if (realNow - needEnd > 64) { const float h = (float) juce::jmax<int64_t> (0, juce::jmin (latest, realNow) * hop - base * hop - segLen); m_left.holdTargetTime (h); m_right.holdTargetTime (h); }
                m_left.process ((u32) segLen, L + off);
                if (stereo) m_right.process ((u32) segLen, R + off);
                else juce::FloatVectorOperations::copy (R + off, L + off, segLen);   // right_out = left_out (upstream's mono)
            }
        }
        else
        {
            m_left.process ((u32) len, L);
            if (stereo) m_right.process ((u32) len, R);
            else juce::FloatVectorOperations::copy (R, L, len);   // right_out = left_out (upstream's mono)
        }

        // ---- fades, velocity, dry, limiter, output
        const float fadeStep = 1.0f / (float) (0.006 * m_sr);
        const bool velOn = mode == 1;
        for (int i = 0; i < len; ++i)
        {
            if (m_fade != m_fadeTarget)
            {
                m_fade = m_fadeTarget > m_fade ? juce::jmin (m_fadeTarget, m_fade + fadeStep) : juce::jmax (m_fadeTarget, m_fade - fadeStep);
                if (m_fade <= 0 && m_stopAfterFade) { m_left.set_playing (false); m_right.set_playing (false); m_running = false; m_stopAfterFade = false; m_fade = 1; m_fadeTarget = 1; for (int k = i; k < len; ++k) { L[k] = 0; R[k] = 0; } break; }
            }
            float g = m_fade;
            if (velOn) { m_velSmooth += (m_velocity - m_velSmooth) * 0.002f; g *= m_velSmooth; }
            if (g != 1.0f) { L[i] *= g; R[i] *= g; }
        }
        if (m_isEffect)
        {
            const float dry = P ("dry");
            const int dlen = m_dryDelay.getNumSamples();
            const int dl = live && m_live != nullptr ? m_live->latencySamples() : 0;
            auto* d0 = m_dryDelay.getWritePointer (0);
            auto* d1 = m_dryDelay.getWritePointer (1);
            for (int i = 0; i < len; ++i)
            {
                d0[m_dryPos] = inL[i]; d1[m_dryPos] = inR[i];
                const int rp = (m_dryPos - dl + dlen) % dlen;
                if (dry > 0) { L[i] += dry * d0[rp]; R[i] += dry * d1[rp]; }
                m_dryPos = (m_dryPos + 1) % dlen;
            }
        }
        // SAFETY LIMITER (ours, an extra, off by default): when off it is not in the path at all (no delay, no change).
        // Switching it: the output dips to silence over 3 ms, the swap happens at a buffer edge, then it comes back
        // (the limiter's look-ahead shifts the sound by kLatency samples, which would click if swapped mid-sound).
        float* lr[2] = { L, R };
        {
            const bool want = P ("limiter") > 0.5f;
            if (m_limFadeDir == -1 && m_limFade <= 0.0f)
            {
                m_limOn = want; m_limFadeDir = 1;
                if (m_limOn) { m_limiter.reset(); m_limHold = TruePeakLimiter::kLatency; }
            }
            else if (want != m_limOn && m_limFadeDir == 0) m_limFadeDir = -1;
            if (m_limOn) m_limiter.process (lr, len);
            if (m_limFadeDir != 0)
            {
                const float step = 1.0f / (float) (0.003 * m_sr);
                for (int i = 0; i < len; ++i)
                {
                    if (m_limHold > 0) --m_limHold;
                    else m_limFade = juce::jlimit (0.0f, 1.0f, m_limFade + (float) m_limFadeDir * step);
                    const float g = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * m_limFade);   // (an S-curve: no corners)
                    L[i] *= g; R[i] *= g;
                }
                if (m_limFadeDir == 1 && m_limFade >= 1.0f) m_limFadeDir = 0;
            }
        }

        // recorder (the Qt app's record button records what is heard)
        if (auto* w = m_writerPtr.load())
        {
            m_writerInUse.store (true);
            if (m_writerPtr.load() == w) w->write (lr, len);
            m_writerInUse.store (false);
        }
        float peak = 0;
        for (int c = 0; c < outCh; ++c)
        {
            io.copyFrom (c, done, c == 0 ? L : R, len);
            peak = juce::jmax (peak, juce::FloatVectorOperations::findMaximum (c == 0 ? L : R, len), -juce::FloatVectorOperations::findMinimum (c == 0 ? L : R, len));
        }
        m_level.store (juce::jmax (peak, m_level.load() * 0.9f));
        m_limGain.store (m_limiter.takeMinGain());
        if (tapOn) tapEnd (L, R, len, peak);
        done += len;
    }
    m_runningUi.store (m_running); m_notesUi.store (m_notesDown);
    m_hostPlayingUi.store (hostPlaying); m_hostValidUi.store (hostInfoValid);
    m_audioBusy.store (false);
}

juce::int64 Engine::lateBlocks() const { return m_live != nullptr ? m_live->lateCount() : 0; }

// ------------------------------------------------------------------------------------ message-thread API
void Engine::play() { pushCmd ({ Cmd::Play }); m_playingUi = true; }
void Engine::pause() { pushCmd ({ Cmd::Pause }); m_playingUi = false; }
void Engine::restart() { pushCmd ({ Cmd::Restart }); }
void Engine::restBlocks (const std::vector<int>& idx) { for (int i : idx) pushCmd ({ Cmd::Rest, i }); }

void Engine::addFiles (const juce::StringArray& files)
{
    post ([this, files]
    {
        int added = 0;
        juce::String longest; double longestSecs = 0;
        setStatus ("Loading " + juce::String (files.size()) + (files.size() == 1 ? " sound..." : " sounds..."));
        for (auto& path : files)
        {
            juce::File f (path);
            juce::Array<juce::File> list;
            if (f.isDirectory()) f.findChildFiles (list, juce::File::findFiles, true, "*.wav;*.WAV;*.aif;*.aiff;*.AIF;*.AIFF;*.flac;*.FLAC;*.mp3;*.MP3;*.ogg");   // "directory" (recursive, as the Qt app)
            else list.add (f);
            list.sort();
            for (auto& file : list)
            {
                std::unique_ptr<juce::AudioFormatReader> r (compat::formats().createReaderFor (file));
                if (r == nullptr) { setStatus ("Can't read " + file.getFileName()); continue; }
                const auto n = (int) juce::jmin<juce::int64> (r->lengthInSamples, (juce::int64) (r->sampleRate * 1200));
                juce::AudioBuffer<float> b ((int) juce::jmax (1u, r->numChannels), n);
                r->read (&b, 0, n, 0, true, true);
                const auto key = newKey (file.getFileName());
                compat::putMemorySound (key, b, r->sampleRate);
                const juce::ScopedLock l (m_modelLock);
                m_sounds.push_back ({ key, file.getFileNameWithoutExtension(), true, n / r->sampleRate });
                ++added;
                if (n / r->sampleRate > longestSecs) { longestSecs = n / r->sampleRate; longest = file.getFileNameWithoutExtension(); }
            }
        }
        setStatus ("Added " + juce::String (added) + (added == 1 ? " sound" : " sounds"));
        noticeLongSounds (longest, longestSecs);
        scheduleAutoBuild (true, false);
    });
}

void Engine::addMemorySound (const juce::String& name, const juce::AudioBuffer<float>& audio, double rate)
{
    const auto key = newKey (name);
    compat::putMemorySound (key, audio, rate);
    { const juce::ScopedLock l (m_modelLock); m_sounds.push_back ({ key, name, true, audio.getNumSamples() / rate }); }
    sendChangeMessage();
    post ([this] { scheduleAutoBuild (true, false); });
}

// the live brain's sound list is the model's sound list at the last build (same order)
static int liveIndexOf (const std::vector<juce::String>& liveKeys, const juce::String& key)
{
    for (size_t i = 0; i < liveKeys.size(); ++i) if (liveKeys[i] == key) return (int) i;
    return -1;
}
static std::vector<juce::String> gNoKeys;

void Engine::setSoundEnabled (int index, bool on)
{
    juce::String key;
    { const juce::ScopedLock l (m_modelLock); if (index < 0 || index >= (int) m_sounds.size()) return; m_sounds[(size_t) index].enabled = on; key = m_sounds[(size_t) index].key; }
    refreshNotice();
    post ([this, key, on]
    {
        withExclusiveWorld ([&]
        {
            if (m_source == nullptr) return;
            int k = 0;
            for (auto& s : const_cast<std::list<brain::sound>&> (m_source->get_samples()))
            {
                if (juce::String (s.m_filename) == key) s.m_enabled = on;   // as brain::activate_sound
                ++k;
            }
        });
    });
    sendChangeMessage();
}

void Engine::setAllEnabled (bool on)
{
    int n;
    { const juce::ScopedLock l (m_modelLock); n = (int) m_sounds.size(); }
    for (int i = 0; i < n; ++i) setSoundEnabled (i, on);
}

void Engine::deleteSound (int index)
{
    juce::String key;
    {
        const juce::ScopedLock l (m_modelLock);
        if (index < 0 || index >= (int) m_sounds.size()) return;
        key = m_sounds[(size_t) index].key;
        m_sounds.erase (m_sounds.begin() + index);
    }
    refreshNotice();
    post ([this, key]
    {
        withExclusiveWorld ([&]
        {
            if (m_source == nullptr || m_graveyard == nullptr) return;
            auto& list = const_cast<std::list<brain::sound>&> (m_source->get_samples());
            for (auto it = list.begin(); it != list.end(); ++it)
                if (juce::String (it->m_filename) == key) { m_graveyard->splice (m_graveyard->end(), list, it); break; }   // as brain::delete_sound
        });
        compat::removeMemorySound (key);
        scheduleAutoBuild (true, false);
    });
    sendChangeMessage();
}

void Engine::clearSounds()
{
    std::vector<SoundItem> old;
    { const juce::ScopedLock l (m_modelLock); old.swap (m_sounds); }
    post ([this, old]
    {
        withExclusiveWorld ([&]
        {
            if (m_source == nullptr || m_graveyard == nullptr) return;
            m_graveyard->splice (m_graveyard->end(), const_cast<std::list<brain::sound>&> (m_source->get_samples()));
        });
        for (auto& s : old) compat::removeMemorySound (s.key);
        { const juce::ScopedLock l (m_modelLock); m_notice = {}; }
        setStatus ("Library cleared");
    });
    sendChangeMessage();
}

void Engine::scheduleAutoBuild (bool library, bool target)
{
    if (P ("auto_build") <= 0.5f) return;
    if (library) m_libDirty = true;
    if (target) m_tgtDirty = true;
    post ([this]
    {
        if (m_libDirty.exchange (false)) { m_libDirty = false; generateLibrary (true); }
        if (m_tgtDirty.exchange (false)) generateTarget();
    });
}

std::unique_ptr<brain> Engine::buildLibrary (const std::vector<SoundItem>& sounds, double& seconds, bool& refused)
{
    auto b = std::make_unique<brain>();
    seconds = 0;
    refused = false;
    for (auto& s : sounds) seconds += s.seconds;
    {
        // every piece keeps two copies of its audio plus 1000 network links: keep it within memory and minutes
        const double hop = juce::jmax (1.0, P ("src_block") * (1.0 - q100 (P ("src_overlap"))));
        const double pieces = seconds * m_sr / hop;
        if (pieces > kMaxPieces)
        {
            setStatus ("Library too big: " + juce::String ((int) pieces) + " pieces (most " + juce::String ((int) kMaxPieces)
                       + "). Use bigger PIECE SIZE or less PIECE OVERLAP, or fewer sounds.");
            refused = true;
            return b;
        }
    }
    // the status line's progress and time left (the estimate is corrected as the build goes)
    {
        int pieces = 0;
        estimateBuildSeconds (seconds, &pieces);
        const double n = pieces;
        m_buildPieces = pieces;
        m_estAnalyse = gAnalyseCost.load() * n;
        m_estNetwork = gPairCost.load() * n * n / networkThreads();
        m_phaseStartMs = juce::Time::getMillisecondCounterHiRes();
        m_buildStartMs = m_phaseStartMs.load();
        m_phase = 1;
    }
    for (auto& s : sounds) b->load_sound (s.key.toStdString(), brain::MIX);
    for (auto& s : sounds) if (! s.enabled) b->activate_sound (s.key.toStdString(), false);
    const u32 bs = (u32) std::lround (P ("src_block"));
    const float ov = (float) bs * q100 (P ("src_overlap"));   // process_thread: m_source_overlap = block_size * fraction
    b->init (bs, (u32) ov, (window::type) std::lround (P ("src_window")));
    search_params p (1, 0, 0, 100, 0);                          // process_thread's /generate_brain
    const double n = (double) b->get_num_blocks();
    m_buildPieces = (int) n;
    const double t1 = juce::Time::getMillisecondCounterHiRes();
    const double analysed = (t1 - m_phaseStartMs.load()) / 1000.0;
    if (n > 300) gAnalyseCost = analysed / n;
    m_estNetwork = gPairCost.load() * n * n / networkThreads();
    m_phaseStartMs = t1;
    m_progress = 0;
    m_phase = 2;
    buildSynapses (*b, p, m_progress);
    const double net = (juce::Time::getMillisecondCounterHiRes() - t1) / 1000.0;
    if (n > 800) gPairCost = net * networkThreads() / (n * n);
    return b;
}

juce::String Engine::librarySignature (const std::vector<SoundItem>& sounds) const
{
    juce::String sig;
    for (auto& s : sounds) sig << s.key << (s.enabled ? "+" : "-") << "|";
    sig << P ("src_block") << "/" << q100 (P ("src_overlap")) << "/" << P ("src_window") << "@" << m_sr;
    return sig;
}

void Engine::generateLibrary (bool onlyIfChanged)
{
    post ([this, onlyIfChanged]
    {
        std::vector<SoundItem> snap;
        { const juce::ScopedLock l (m_modelLock); snap = m_sounds; }
        const auto sig = librarySignature (snap);
        if (onlyIfChanged)
        {
            const juce::ScopedLock l (m_modelLock);
            if (m_builtSig.isNotEmpty() && sig == m_builtSig) return;   // (already built from exactly these: no second wait)
        }
        m_buildStartMs = juce::Time::getMillisecondCounterHiRes();
        m_progress = 0;
        setStatus ("Cutting " + juce::String ((int) snap.size()) + " sounds into pieces...");
        double secs = 0;
        bool refused = false;
        auto b = buildLibrary (snap, secs, refused);
        m_phase = 0;
        const double took = (juce::Time::getMillisecondCounterHiRes() - m_buildStartMs.load()) / 1000.0;
        makeLibraryPicture (*b);
        auto s = std::make_unique<Swap>();
        s->source = b.release();
        s->graveyard = new std::list<brain::sound>();
        s->resetRenderers = true;                                   // as /generate_brain
        const auto nb = s->source->get_num_blocks();
        publish (std::move (s));
        m_progress = 1;
        { const juce::ScopedLock l (m_modelLock); m_builtSig = refused || snap.empty() ? juce::String() : sig; if (! refused && m_notice.startsWith ("\"")) m_notice = {}; }
        if (refused) return;   // (the status says why)
        setStatus ("Library ready: " + juce::String ((int) nb) + " pieces from " + juce::String ((int) snap.size()) + " sounds"
                   + (took >= 2 ? " (built in " + timeWords (took).fromFirstOccurrenceOf ("about ", false, false) + ")" : juce::String()));
    });
}

void Engine::setTargetFile (const juce::File& f)
{
    post ([this, f]
    {
        std::unique_ptr<juce::AudioFormatReader> r (compat::formats().createReaderFor (f));
        if (r == nullptr) { setStatus ("Can't read " + f.getFileName()); return; }
        const auto n = (int) juce::jmin<juce::int64> (r->lengthInSamples, (juce::int64) (r->sampleRate * 1200));
        juce::AudioBuffer<float> b ((int) juce::jmax (1u, r->numChannels), n);
        r->read (&b, 0, n, 0, true, true);
        const auto key = newKey (f.getFileName());
        compat::putMemorySound (key, b, r->sampleRate);
        juce::String old;
        { const juce::ScopedLock l (m_modelLock); old = m_targetKey; m_targetKey = key; m_targetName = f.getFileNameWithoutExtension(); if (m_notice.startsWith ("No target")) m_notice = {}; }
        if (old.isNotEmpty()) compat::removeMemorySound (old);
        targetJob();   // (now, so a GENERATE pressed meanwhile plays the new target)
    });
}

void Engine::setTargetMemory (const juce::String& name, const juce::AudioBuffer<float>& audio, double rate)
{
    const auto key = newKey (name);
    compat::putMemorySound (key, audio, rate);
    juce::String old;
    { const juce::ScopedLock l (m_modelLock); old = m_targetKey; m_targetKey = key; m_targetName = name; }
    post ([this, old] { if (old.isNotEmpty()) compat::removeMemorySound (old); targetJob(); });
    sendChangeMessage();
}

void Engine::buildTarget (std::unique_ptr<brain>& l, std::unique_ptr<brain>& r)
{
    juce::String key;
    { const juce::ScopedLock lk (m_modelLock); key = m_targetKey; }
    l = std::make_unique<brain>(); r = std::make_unique<brain>();
    if (key.isEmpty()) return;
    l->load_sound (key.toStdString(), brain::LEFT);             // process_thread's /load_target
    r->load_sound (key.toStdString(), brain::RIGHT);
    const u32 bs = (u32) std::lround (P ("tgt_block"));
    const float ov = (float) bs * q100 (P ("tgt_overlap"));
    const auto w = (window::type) std::lround (P ("tgt_window"));
    l->init (bs, (u32) ov, w);                                   // /generate_target
    r->init (bs, (u32) ov, w);
}

void Engine::generateTarget() { post ([this] { targetJob(); }); }

void Engine::targetJob()   // (worker)
{
    {
        std::unique_ptr<brain> l, r;
        buildTarget (l, r);
        makeTargetPicture (*l);
        tapSnapTarget (*l, *r);
        auto s = std::make_unique<Swap>();
        s->left = l.release(); s->right = r.release();
        const auto nb = s->left->get_num_blocks();
        publish (std::move (s));
        if (m_isEffect) rebuildLive();
        if (nb > 0) setStatus ("Target ready: " + juce::String ((int) nb) + " pieces");
    }
}

void Engine::rebuildLive()
{
    if (! m_isEffect) return;
    post ([this]
    {
        const u32 bs = (u32) std::lround (P ("tgt_block"));
        const float ov = (float) bs * q100 (P ("tgt_overlap"));
        auto* lt = new LiveTarget (bs, (u32) ov, (window::type) std::lround (P ("tgt_window")), m_maxBlock);
        m_latency = lt->latencySamples();
        auto s = std::make_unique<Swap>();
        s->live = lt; s->setLive = true;
        publish (std::move (s));
        sendChangeMessage();
    });
}

void Engine::clearTarget()
{
    juce::String old;
    { const juce::ScopedLock l (m_modelLock); old = m_targetKey; m_targetKey = {}; m_targetName = {}; }
    post ([this, old] { if (old.isNotEmpty()) compat::removeMemorySound (old); });
    generateTarget();
    sendChangeMessage();
}

void Engine::generateAll (bool restartFromTop)
{
    // (a library already built from these sounds is not built again: dropping a long song starts its build at once,
    //  and GENERATE pressed meanwhile waits for that one build, then plays)
    if (restartFromTop) m_playPending = true;
    generateLibrary (true);
    generateTarget();
    if (restartFromTop) post ([this]
    {
        restart(); play(); m_playPending = false;
        const auto lib = libraryPicture();
        const auto snd = sounds();
        if (lib != nullptr && lib->numBlocks > 0)
            setStatus ("Rebuilt from " + juce::String (lib->numBlocks) + " pieces of " + juce::String ((int) snd.size()) + (snd.size() == 1 ? " sound" : " sounds")
                       + (m_isEffect ? juce::String() : ", target \"" + targetName() + "\""));
    });
}

// ------------------------------------------------------------------------------------ status line
juce::String Engine::timeWords (double s)
{
    if (s < 1.5) return "about 1 s";
    if (s < 60) return "about " + juce::String ((int) std::lround (s)) + " s";
    if (s < 3600)
    {
        const int m = (int) (s / 60), sec = (int) std::lround ((s - m * 60) / 10) * 10;
        return "about " + juce::String (m) + " min" + (sec > 0 && sec < 60 && m < 10 ? " " + juce::String (sec) + " s" : juce::String());
    }
    return "about " + juce::String (s / 3600, 1) + " hours";
}

double Engine::estimateBuildSeconds (double secs, int* piecesOut) const
{
    const double hop = juce::jmax (1.0, P ("src_block") * (1.0 - q100 (P ("src_overlap"))));
    const double n = secs * m_sr / hop;
    if (piecesOut != nullptr) *piecesOut = (int) n;
    return gAnalyseCost.load() * n + gPairCost.load() * n * n / networkThreads();
}

Engine::BuildInfo Engine::buildInfo() const
{
    BuildInfo b;
    b.phase = m_phase.load();
    if (b.phase == 0) return b;
    const double now = juce::Time::getMillisecondCounterHiRes();
    b.elapsed = (now - m_buildStartMs.load()) / 1000.0;
    b.pieces = m_buildPieces.load();
    const double inPhase = (now - m_phaseStartMs.load()) / 1000.0;
    if (b.phase == 1) b.eta = juce::jmax (0.0, m_estAnalyse.load() - inPhase) + m_estNetwork.load();
    else
    {
        const double p = m_progress.load();
        // the network's own pace once it is under way (each piece takes about as long as the next)
        b.eta = p > 0.02 && inPhase > 1.0 ? inPhase * (1.0 - p) / p : juce::jmax (0.0, m_estNetwork.load() - inPhase);
    }
    b.fraction = (float) juce::jlimit (0.0, 0.99, b.elapsed / juce::jmax (1e-3, b.elapsed + b.eta));
    return b;
}

// the note always describes the sounds that are in the library right now: drop it, then look again at what is left
void Engine::refreshNotice()
{
    juce::String name; double secs = 0;
    { const juce::ScopedLock l (m_modelLock); m_notice = {};
      for (auto& s : m_sounds) if (s.enabled && s.seconds > secs) { secs = s.seconds; name = s.name; } }
    if (secs > 0) noticeLongSounds (name, secs);
    sendChangeMessage();
}

void Engine::noticeLongSounds (const juce::String& name, double secs)
{
    double total = 0;
    { const juce::ScopedLock l (m_modelLock); for (auto& s : m_sounds) if (s.enabled) total += s.seconds; }
    int pieces = 0;
    const double est = estimateBuildSeconds (total, &pieces);
    if (secs < 45 && est < 10) return;
    const int m = (int) (secs / 60), sc = (int) secs % 60;
    juce::String len = m > 0 ? juce::String (m) + ":" + juce::String (sc).paddedLeft ('0', 2) : juce::String (sc) + " s";
    juce::String n = "\"" + name + "\" is " + len + " long";
    if (pieces > kMaxPieces)
        n << ": too long for one library (" << pieces << " pieces, the most is " << (int) kMaxPieces << "). Tip: use a shorter part of it, or a bigger PIECE SIZE.";
    else if (est >= 10 || secs >= 45)
        n << ", so the library will take " << timeWords (est) << " to build on this computer. It plays when it's done. Tip: shorter sounds build faster.";
    else return;
    setNotice (n);
}

// ------------------------------------------------------------------------------------ pictures
void Engine::makeLibraryPicture (const brain& b)
{
    auto pic = std::make_shared<LibraryPicture>();
    const u32 n = b.get_num_blocks();
    pic->numBlocks = (int) n;
    pic->blockSize = (int) b.get_block_size();
    std::vector<std::pair<u32, u32>> ranges;
    for (auto& s : const_cast<brain&> (b).get_samples()) { ranges.push_back ({ s.m_start, s.m_end }); pic->soundNames.push_back (juce::File (s.m_filename).getFileNameWithoutExtension().fromLastOccurrenceOf (":", false, false)); }
    pic->dots.resize (n);
    pic->links.resize (n);
    for (u32 i = 0; i < n; ++i)
    {
        const auto& bl = b.get_block (i);
        const auto& pcm = bl.get_pcm();
        double e = 0;
        for (u32 k = 0; k < pcm.get_length(); ++k) e += (double) pcm[k] * pcm[k];
        const float rms = (float) std::sqrt (e / juce::jmax (1u, pcm.get_length()));
        int snd = 0; float pos = 0;
        for (size_t k = 0; k < ranges.size(); ++k)
            if (i >= ranges[k].first && i <= ranges[k].second) { snd = (int) k; pos = (float) (i - ranges[k].first) / (float) juce::jmax (1u, ranges[k].second - ranges[k].first); break; }
        const float f = juce::jlimit (0.0f, 1.0f, std::log2 (juce::jmax (20.0f, bl.get_freq()) / 20.0f) / 10.0f);
        const float lv = juce::jlimit (0.0f, 1.0f, 1.0f + juce::Decibels::gainToDecibels (rms, -60.0f) / 60.0f);
        pic->dots[i] = { f, lv, snd, pos, lv };
        auto& syn = bl.get_synapse_const();
        for (int k = 0; k < 4; ++k) pic->links[i][(size_t) k] = k < (int) syn.size() ? (int) syn[(size_t) k] : -1;
    }
    pieceBrightness (b, m_sr, pic->bright, pic->centroidHz);
    {
        const juce::ScopedLock l (m_modelLock);
        m_libPic = pic;
        m_builtRate = m_sr;
    }
    m_terrainRefresh = true;   // (which sounds a mountain picks depends on the library: redone on the message thread)
    tapSnapBrain (b, *pic);    // (the tap: the same library as plain read-only numbers)
}

void Engine::refreshTerrain()
{
    if (! m_terrainRefresh.exchange (false)) return;
    ghost::terrain::Terrain land; juce::String st;
    { const juce::ScopedLock l (m_modelLock); land = m_terrainMsg; st = m_terrainState; }
    if (! land.empty()) setTerrain (land, st);
}

void Engine::makeTargetPicture (const brain& b)
{
    auto pic = std::make_shared<TargetPicture>();
    const u32 n = b.get_num_blocks();
    pic->numBlocks = (int) n;
    pic->blockSize = (int) b.get_block_size();
    pic->hop = (int) (b.get_block_size() - b.get_overlap());
    for (u32 i = 0; i < n; ++i)
    {
        const auto& pcm = b.get_block (i).get_pcm();
        float pk = 0;
        for (u32 k = 0; k < pcm.get_length(); ++k) pk = juce::jmax (pk, std::abs (pcm[k]));
        pic->peaks.push_back (pk);
        pic->bright.push_back (juce::jlimit (0.0f, 1.0f, std::log2 (juce::jmax (20.0f, b.get_block (i).get_freq()) / 20.0f) / 10.0f));
    }
    const juce::ScopedLock l (m_modelLock);
    pic->name = m_targetName;
    pic->seconds = m_sr > 0 ? (double) n * pic->hop / m_sr : 0;
    m_tgtPic = pic;
}

// ------------------------------------------------------------------------------------ recording
void Engine::startRecording (const juce::File& f)
{
    stopRecording();
    f.getParentDirectory().createDirectory();
    f.deleteFile();
    auto os = f.createOutputStream();
    if (os == nullptr) { setStatus ("Can't write " + f.getFullPathName()); return; }
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), m_sr, 2, 24, {}, 0));
    if (w == nullptr) return;
    os.release();
    m_writer = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (w.release(), m_writerThread, 1 << 17);
    m_writerPtr.store (m_writer.get());
    m_recorderOn = true;
    m_lastRecording = f;
    play();
    setStatus ("Recording to " + f.getFileName());
}

void Engine::stopRecording()
{
    if (m_writer == nullptr) return;
    m_writerPtr.store (nullptr);
    while (m_writerInUse.load()) juce::Thread::yield();
    m_writer.reset();
    m_recorderOn = false;
    pause();
    setStatus ("Saved " + m_lastRecording.getFileName());
}

// ------------------------------------------------------------------------------------ files (upstream formats)
void Engine::saveLibraryFile (const juce::File& f)
{
    post ([this, f]
    {
        const bool ok = withExclusiveWorld ([&]
        {
            if (m_source == nullptr) return;
            std::ofstream ofs (f.getFullPathName().toStdString().c_str(), std::ios::binary);
            ofs || *m_source;                                        // process_thread::save_source
            ofs.close();
        });
        setStatus (ok ? "Saved library " + f.getFileName() : "Couldn't save (audio busy)");
    });
}

void Engine::loadLibraryFile (const juce::File& f)
{
    post ([this, f]
    {
        auto b = std::make_unique<brain>();
        {
            std::ifstream ifs (f.getFullPathName().toStdString().c_str(), std::ios::binary);
            if (! ifs) { setStatus ("Can't open " + f.getFileName()); return; }
            ifs || *b;                                               // process_thread::load_source
        }
        // the library's sounds become ours (so the plug-in state can carry them)
        std::vector<SoundItem> items;
        for (auto& s : b->get_samples())
        {
            juce::AudioBuffer<float> a (1, (int) s.m_sample.get_length());
            for (u32 i = 0; i < s.m_sample.get_length(); ++i) a.setSample (0, (int) i, s.m_sample[i]);
            const auto name = juce::File (juce::String (s.m_filename)).getFileNameWithoutExtension();
            const auto key = newKey (name);
            compat::putMemorySound (key, a, m_sr);
            items.push_back ({ key, name, s.m_enabled, a.getNumSamples() / m_sr });
        }
        const float falloff = b->get_usage_falloff();
        makeLibraryPicture (*b);
        juce::MessageManager::callAsync ([this, falloff, bs = b->get_block_size(), ov = b->get_overlap()]
        {
            if (auto* p = m_params.getParameter ("boredom")) p->setValueNotifyingHost (p->convertTo0to1 (falloff));
            if (auto* p = m_params.getParameter ("src_block")) p->setValueNotifyingHost (p->convertTo0to1 ((float) bs));
            if (auto* p = m_params.getParameter ("src_overlap")) p->setValueNotifyingHost (p->convertTo0to1 (bs > 0 ? (float) ov / (float) bs : 0.0f));
        });
        { const juce::ScopedLock l (m_modelLock); for (auto& s : m_sounds) compat::removeMemorySound (s.key); m_sounds = items; }
        auto s = std::make_unique<Swap>();
        s->source = b.release(); s->graveyard = new std::list<brain::sound>(); s->resetRenderers = true;
        publish (std::move (s));
        setStatus ("Loaded library " + f.getFileName());
    });
}

void Engine::saveSessionFile (const juce::File& f)
{
    post ([this, f]
    {
        const bool ok = withExclusiveWorld ([&]
        {
            applyParams (false);                                     // the renderers carry the settings into the file (as upstream's do)
            for (auto* r : { &m_left, &m_right }) r->get_params()->m_usage_importance = q100 (P ("novelty"));   // (the knobs' own values, not TERRAIN's shaped ones)
            if (m_source != nullptr) m_source->set_usage_falloff (q100 (P ("boredom")));
            for (auto* r : { &m_left, &m_right })
            {
                r->get_params()->m_stickyness = q100 (P ("stickyness"));
                r->set_stretch ((u32) juce::jmax (1L, std::lround (P ("stretch")))); r->set_target_mix (q100 (P ("target_mix")));
            }
            m_macrosApplied = m_shapeLive.macrosOn;   // (re-applied on the next buffer)
            if (m_source != nullptr)
            {
                // your own switches go in the file, not the landscape's pick (applied again on the next buffer)
                int k = 0;
                for (auto& snd : const_cast<std::list<brain::sound>&> (m_source->get_samples())) { if (k < m_userCount) snd.m_enabled = m_userOn[k]; ++k; }
                m_maskDirty = true;
            }
            brain empty;
            std::ofstream ofs (f.getFullPathName().toStdString().c_str(), std::ios::binary);
            u32 version = 0;
            ofs || version;                                          // process_thread::save_session
            m_left.stream (ofs);
            m_right.stream (ofs);
            u32 sbs = (u32) std::lround (P ("src_block")); float sov = (float) sbs * q100 (P ("src_overlap"));
            u32 tbs = (u32) std::lround (P ("tgt_block")); float tov = (float) tbs * q100 (P ("tgt_overlap"));
            window::type sw = (window::type) std::lround (P ("src_window")), tw = (window::type) std::lround (P ("tgt_window"));
            ofs || sbs || sov;
            ofs || tbs || tov;
            ofs || sw || tw;
            ofs || (m_source != nullptr ? *m_source : empty);
            ofs || (m_tl != nullptr ? *m_tl : empty);
            ofs || (m_tr != nullptr ? *m_tr : empty);
            ofs.close();
        });
        // the landscape rides beside the session (the .samplebrain file itself stays upstream's format)
        const auto side = f.withFileExtension ("terrain.json");
        const auto st = terrainState();
        if (ok && st.isNotEmpty()) side.replaceWithText (st); else side.deleteFile();
        setStatus (ok ? "Saved session " + f.getFileName() : "Couldn't save (audio busy)");
    });
}

void Engine::loadSessionFile (const juce::File& f)
{
    post ([this, f]
    {
        auto src = std::make_unique<brain>(), tl = std::make_unique<brain>(), tr = std::make_unique<brain>();
        auto rl = std::make_unique<RtRenderer> (1), rr = std::make_unique<RtRenderer> (1);
        u32 sbs = 1000, tbs = 1000; float sov = 0, tov = 0; window::type sw = window::DODGY, tw = window::DODGY;
        {
            std::ifstream ifs (f.getFullPathName().toStdString().c_str(), std::ios::binary);
            if (! ifs) { setStatus ("Can't open " + f.getFileName()); return; }
            u32 version = 0;
            ifs || version;                                          // process_thread::load_session
            rl->stream (ifs); rr->stream (ifs);
            ifs || sbs || sov;
            ifs || tbs || tov;
            ifs || sw || tw;
            ifs || *src;
            ifs || *tl;
            ifs || *tr;
            if (! ifs) { setStatus ("That session file looks damaged"); return; }
        }
        // sounds + target into our model
        std::vector<SoundItem> items;
        for (auto& s : src->get_samples())
        {
            juce::AudioBuffer<float> a (1, (int) s.m_sample.get_length());
            for (u32 i = 0; i < s.m_sample.get_length(); ++i) a.setSample (0, (int) i, s.m_sample[i]);
            const auto name = juce::File (juce::String (s.m_filename)).getFileNameWithoutExtension();
            const auto key = newKey (name);
            compat::putMemorySound (key, a, m_sr);
            items.push_back ({ key, name, s.m_enabled, a.getNumSamples() / m_sr });
        }
        juce::String tkey, tname;
        if (! tl->get_samples().empty())
        {
            const auto& ls = tl->get_samples().front();
            const spiralcore::sample* rs = tr->get_samples().empty() ? &ls.m_sample : &tr->get_samples().front().m_sample;
            const int n = (int) juce::jmin (ls.m_sample.get_length(), rs->get_length());
            juce::AudioBuffer<float> a (2, n);
            for (int i = 0; i < n; ++i) { a.setSample (0, i, ls.m_sample[(u32) i]); a.setSample (1, i, (*rs)[(u32) i]); }
            tname = juce::File (juce::String (ls.m_filename)).getFileNameWithoutExtension();
            tkey = newKey (tname);
            compat::putMemorySound (tkey, a, m_sr);
        }
        makeLibraryPicture (*src);
        {
            const juce::ScopedLock l (m_modelLock);
            for (auto& s : m_sounds) compat::removeMemorySound (s.key);
            if (m_targetKey.isNotEmpty()) compat::removeMemorySound (m_targetKey);
            m_sounds = items; m_targetKey = tkey; m_targetName = tname;
        }
        makeTargetPicture (*tl);
        tapSnapTarget (*tl, *tr);
        // the session's settings onto the controls (MainWindow::init_from_session)
        auto* p = rl->get_params();
        struct KV { const char* id; float v; };
        std::vector<KV> kv = {
            { "ratio", p->m_ratio }, { "n_ratio", p->m_n_ratio }, { "fft_start", (float) p->m_fft1_start }, { "fft_end", (float) p->m_fft1_end },
            { "novelty", p->m_usage_importance }, { "boredom", src->get_usage_falloff() }, { "stickyness", p->m_stickyness },
            { "stretch", (float) rl->get_stretch() }, { "algo", (float) rl->get_search_algo() }, { "synapses", (float) p->m_num_synapses },
            { "slide_error", (float) rl->get_slide_error() }, { "tgt_block", (float) tl->get_block_size() },
            { "tgt_overlap", tl->get_block_size() > 0 ? (float) tl->get_overlap() / (float) tl->get_block_size() : 0.0f }, { "tgt_window", (float) tw },
            { "src_block", (float) src->get_block_size() }, { "src_overlap", src->get_block_size() > 0 ? (float) src->get_overlap() / (float) src->get_block_size() : 0.0f },
            { "src_window", (float) sw }, { "target_mix", rl->get_target_mix() }, { "n_mix", rl->get_n_mix() }, { "autotune", rl->get_autotune() },
            { "volume", rl->get_volume() / 10.0f } };
        juce::WaitableEvent done;
        juce::MessageManager::callAsync ([this, kv, &done]
        {
            for (auto& e : kv)
                if (auto* prm = m_params.getParameter (e.id)) prm->setValueNotifyingHost (prm->convertTo0to1 (e.v));
            done.signal();
        });
        done.wait (2000);
        auto s = std::make_unique<Swap>();
        s->source = src.release(); s->graveyard = new std::list<brain::sound>();
        s->left = tl.release(); s->right = tr.release();
        s->hasRendererState = true; s->stateL = std::move (rl); s->stateR = std::move (rr);
        publish (std::move (s));
        {
            // its landscape, if it was saved here (older sessions and upstream's have none: flat)
            const auto side = f.withFileExtension ("terrain.json");
            const juce::String st = side.existsAsFile() ? side.loadFileAsString() : juce::String();
            juce::MessageManager::callAsync ([this, st] { if (onTerrainLoaded) onTerrainLoaded (st); });
        }
        setStatus ("Loaded session " + f.getFileName());
    });
}

// ------------------------------------------------------------------------------------ offline render
void Engine::renderToFile (const juce::File& f, double maxSeconds, std::function<void (bool)> done)
{
    post ([this, f, maxSeconds, done]
    {
        std::vector<SoundItem> snap;
        { const juce::ScopedLock l (m_modelLock); snap = m_sounds; }
        double secs = 0; bool refused = false;
        auto src = buildLibrary (snap, secs, refused);
        m_phase = 0;
        std::unique_ptr<brain> tl, tr;
        buildTarget (tl, tr);
        bool ok = false;
        if (src->get_num_blocks() > 0 && tl->get_num_blocks() > 0)
        {
            // the same settings the audio thread uses (applyParams), on private renderers
            auto apply = [this] (RtRenderer& x)
            {
                auto* p = x.get_params();
                p->m_ratio = q100 (P ("ratio")); p->m_n_ratio = q100 (P ("n_ratio"));
                p->m_fft1_start = (u32) std::lround (P ("fft_start")); p->m_fft1_end = (u32) std::lround (P ("fft_end"));
                p->m_usage_importance = q100 (P ("novelty")); p->m_stickyness = q100 (P ("stickyness"));
                p->m_num_synapses = (u32) std::lround (P ("synapses"));
                x.set_autotune (q100 (P ("autotune"))); x.set_volume (q100 (P ("volume")) * 10);
                x.set_search_algo ((RtRenderer::search_algo) (int) std::lround (P ("algo")));
                x.set_n_mix (q100 (P ("n_mix"))); x.set_target_mix (q100 (P ("target_mix")));
                x.set_stretch ((u32) juce::jmax (1L, std::lround (P ("stretch")))); x.set_slide_error ((double) (int) std::lround (P ("slide_error")));
                x.set_playing (true);
            };
            RtRenderer lr, rr;
            lr.init (src.get(), tl.get()); rr.init (src.get(), tr.get());
            apply (lr); apply (rr);
            const Shaping shape = shaping();                         // TERRAIN, as set now (no glide offline)
            for (auto* x : { &lr, &rr })
            {
                x->setShaping (shape.active() ? &shape : nullptr);
                x->setShapingBase (q100 (P ("novelty")), q100 (P ("boredom")));
                if (shape.macrosOn)
                {
                    const float s0 = q100 (P ("stickyness")), t0 = q100 (P ("target_mix"));
                    float st = s0 < 0.9f ? s0 + (0.9f - s0) * shape.smooth : s0;
                    st *= 1.0f - 0.85f * shape.stutter;
                    x->get_params()->m_stickyness = st;
                    x->set_stretch (juce::jmin (50u, (u32) juce::jmax (1L, std::lround (P ("stretch"))) + (u32) std::lround (3.0f * shape.stutter)));
                    x->set_target_mix (t0 + (juce::jmin (1.0f, t0 + 0.4f) - t0) * shape.blend);
                }
            }
            if (shape.maskOn)
            {
                int k = 0;
                for (auto& snd : const_cast<std::list<brain::sound>&> (src->get_samples())) { snd.m_enabled = snd.m_enabled && shape.allows (k); ++k; }
            }
            std::vector<float> rank, hzs, factor;
            if (shape.tilt != 0.0f)
            {
                pieceBrightness (*src, m_sr, rank, hzs);
                factor.resize (rank.size());
                for (size_t i = 0; i < rank.size(); ++i) factor[i] = Shaping::tiltFactor (shape.tilt, rank[i]);
                lr.setBlockBias (factor.data(), (u32) factor.size()); rr.setBlockBias (factor.data(), (u32) factor.size());
            }
            src->set_usage_falloff (q100 (P ("boredom")));
            const bool stereo = P ("stereo") > 0.5f;
            const int bufN = 512;
            const auto maxN = (juce::int64) (maxSeconds * m_sr);
            juce::AudioBuffer<float> out (2, (int) juce::jmin<juce::int64> (maxN + bufN, (juce::int64) m_sr * 600));
            out.clear();
            juce::int64 pos = 0;
            bool started = false;
            while (pos + bufN <= out.getNumSamples() && pos < maxN)
            {
                const u32 before = lr.getRenderTime();
                lr.process (bufN, out.getWritePointer (0, (int) pos));
                if (stereo) rr.process (bufN, out.getWritePointer (1, (int) pos));
                else out.copyFrom (1, (int) pos, out, 0, (int) pos, bufN);
                const u32 after = lr.getRenderTime();
                if (after > before) started = true;
                if (started && after == 0 && before == 0 && pos > 0) break;   // the target ran out: one pass done
                if (started && after < before) break;
                pos += bufN;
            }
            TruePeakLimiter lim; lim.prepare (m_sr, 2);
            juce::AudioBuffer<float> tail (2, TruePeakLimiter::kLatency);
            tail.clear();
            float* ch[2] = { out.getWritePointer (0), out.getWritePointer (1) };
            if (P ("limiter") > 0.5f) { lim.process (ch, (int) pos); float* t2[2] = { tail.getWritePointer (0), tail.getWritePointer (1) }; lim.process (t2, tail.getNumSamples()); }
            f.deleteFile();
            if (auto os = f.createOutputStream())
            {
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), m_sr, 2, 24, {}, 0));
                if (w != nullptr)
                {
                    os.release();
                    const int skip = P ("limiter") > 0.5f ? TruePeakLimiter::kLatency : 0;
                    w->writeFromAudioSampleBuffer (out, skip, (int) pos - skip);
                    if (skip > 0) w->writeFromAudioSampleBuffer (tail, 0, skip);
                    ok = true;
                }
            }
        }
        juce::MessageManager::callAsync ([done, ok] { if (done) done (ok); });
    });
}

// ------------------------------------------------------------------------------------ plug-in state
void Engine::getState (juce::MemoryBlock& out)
{
    juce::MemoryOutputStream mo (out, false);
    juce::ValueTree params = m_params.copyState();
    std::vector<SoundItem> snd; juce::String tkey, tname;
    { const juce::ScopedLock l (m_modelLock); snd = m_sounds; tkey = m_targetKey; tname = m_targetName; }
    juce::ValueTree model ("MOSAIC");
    model.setProperty ("version", 1, nullptr);
    { const auto st = terrainState(); if (st.isNotEmpty()) model.setProperty ("terrain", st, nullptr); }   // (build 9; older states have none: flat)
    juce::MemoryBlock audio;
    {
        juce::MemoryOutputStream ao (audio, false);
        juce::GZIPCompressorOutputStream gz (ao, 6);
        auto writeSound = [&] (const juce::String& key)
        {
            compat::Audio a;
            compat::setThreadEngineRate (0);
            if (! compat::readAudio (key, a)) { gz.writeInt (0); gz.writeInt (0); gz.writeDouble (0); return; }
            gz.writeInt (a.channels); gz.writeInt ((int) a.frames); gz.writeDouble (a.rate);
            gz.write (a.interleaved.data(), a.interleaved.size() * sizeof (float));
        };
        for (auto& s : snd)
        {
            juce::ValueTree t ("SOUND");
            t.setProperty ("name", s.name, nullptr); t.setProperty ("enabled", s.enabled, nullptr);
            model.appendChild (t, nullptr);
            writeSound (s.key);
        }
        if (tkey.isNotEmpty()) { model.setProperty ("target", tname, nullptr); writeSound (tkey); }
        gz.flush();
    }
    mo.writeString ("GHOSTMOSAIC1");
    juce::MemoryBlock xml;
    { juce::MemoryOutputStream xo (xml, false); params.writeToStream (xo); }
    mo.writeInt ((int) xml.getSize()); mo << xml;
    juce::MemoryBlock mxml;
    { juce::MemoryOutputStream xo (mxml, false); model.writeToStream (xo); }
    mo.writeInt ((int) mxml.getSize()); mo << mxml;
    mo.writeInt64 ((juce::int64) audio.getSize()); mo << audio;
}

void Engine::setState (const void* data, int size)
{
    juce::MemoryInputStream mi (data, (size_t) size, false);
    if (mi.readString() != "GHOSTMOSAIC1") return;
    juce::MemoryBlock xml; xml.setSize ((size_t) mi.readInt()); mi.read (xml.getData(), (int) xml.getSize());
    auto params = juce::ValueTree::readFromData (xml.getData(), xml.getSize());
    if (params.isValid()) m_params.replaceState (params);
    juce::MemoryBlock mxml; mxml.setSize ((size_t) mi.readInt()); mi.read (mxml.getData(), (int) mxml.getSize());
    auto model = juce::ValueTree::readFromData (mxml.getData(), mxml.getSize());
    juce::MemoryBlock audio; audio.setSize ((size_t) mi.readInt64()); mi.read (audio.getData(), (int) audio.getSize());
    juce::MemoryInputStream ai (audio, false);
    juce::GZIPDecompressorInputStream gz (ai);
    auto readSound = [&] (const juce::String& name, juce::String& keyOut, double& secs)
    {
        const int ch = gz.readInt(), frames = gz.readInt(); const double rate = gz.readDouble();
        if (ch <= 0 || frames <= 0) return false;
        std::vector<float> il ((size_t) ch * (size_t) frames);
        gz.read (il.data(), (int) (il.size() * sizeof (float)));
        juce::AudioBuffer<float> b (ch, frames);
        for (int c = 0; c < ch; ++c) for (int i = 0; i < frames; ++i) b.setSample (c, i, il[(size_t) i * (size_t) ch + (size_t) c]);
        keyOut = newKey (name);
        compat::putMemorySound (keyOut, b, rate);
        secs = frames / rate;
        return true;
    };
    std::vector<SoundItem> items;
    for (auto t : model)
    {
        if (! t.hasType ("SOUND")) continue;
        juce::String key; double secs = 0;
        if (readSound (t["name"].toString(), key, secs)) items.push_back ({ key, t["name"].toString(), (bool) t["enabled"], secs });
    }
    juce::String tkey, tname = model["target"].toString();
    double ts = 0;
    if (tname.isNotEmpty()) readSound (tname, tkey, ts);
    {
        const juce::ScopedLock l (m_modelLock);
        for (auto& s : m_sounds) compat::removeMemorySound (s.key);
        if (m_targetKey.isNotEmpty()) compat::removeMemorySound (m_targetKey);
        m_sounds = items; m_targetKey = tkey; m_targetName = tname;
        m_terrainState = model["terrain"].toString();   // (the processor puts it on the TERRAIN page; none = flat)
    }
    generateLibrary();
    generateTarget();
    sendChangeMessage();
}

// ------------------------------------------------------------------------------------ OSC (the Qt app's messages)
void Engine::handleOsc (const juce::String& path, const juce::var& a0)
{
    auto setParam = [this] (const char* id, float v)
    {
        juce::MessageManager::callAsync ([this, id, v] { if (auto* p = m_params.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v)); });
    };
    const float f = (float) (double) a0;
    // audio port (audio_thread.cpp)
    if (path == "/start") play();
    else if (path == "/pause") pause();
    else if (path == "/ratio") setParam ("ratio", f);
    else if (path == "/n_ratio") setParam ("n_ratio", f);
    else if (path == "/fft1_start") setParam ("fft_start", f);
    else if (path == "/fft1_end") setParam ("fft_end", f);
    else if (path == "/novelty") setParam ("novelty", f);
    else if (path == "/stickyness") setParam ("stickyness", f);
    else if (path == "/autotune") setParam ("autotune", f);
    else if (path == "/volume") setParam ("volume", f);
    else if (path == "/search_algo") setParam ("algo", f);
    else if (path == "/n_mix") setParam ("n_mix", f);
    else if (path == "/target_mix") setParam ("target_mix", f);
    else if (path == "/boredom") setParam ("boredom", f);
    else if (path == "/synapses") setParam ("synapses", f);
    else if (path == "/search-stretch") setParam ("stretch", f);
    else if (path == "/slide-error") setParam ("slide_error", f);
    else if (path == "/stereo") setParam ("stereo", f);
    else if (path == "/mic") setParam ("live_input", f);
    else if (path == "/record") juce::MessageManager::callAsync ([this, s = a0.toString()] { startRecording (juce::File (s + ".wav")); });
    else if (path == "/stop") juce::MessageManager::callAsync ([this] { stopRecording(); pause(); });
    else if (path == "/restart_audio") restart();
    // process port (process_thread.cpp)
    else if (path == "/load_sample") addFiles (juce::StringArray (a0.toString()));
    else if (path == "/delete_sample" || path == "/activate_sound" || path == "/deactivate_sound")
    {
        const auto name = juce::File (a0.toString()).getFileNameWithoutExtension();
        juce::MessageManager::callAsync ([this, path, name]
        {
            auto s = sounds();
            for (int i = 0; i < (int) s.size(); ++i)
                if (s[(size_t) i].name == name)
                {
                    if (path == "/delete_sample") { deleteSound (i); break; }
                    setSoundEnabled (i, path == "/activate_sound");
                }
        });
    }
    else if (path == "/source_block_size") setParam ("src_block", f);
    else if (path == "/source_overlap") setParam ("src_overlap", f);
    else if (path == "/generate_brain") generateLibrary();
    else if (path == "/load_target") setTargetFile (juce::File (a0.toString()));
    else if (path == "/target_block_size") setParam ("tgt_block", f);
    else if (path == "/target_overlap") setParam ("tgt_overlap", f);
    else if (path == "/generate_target") generateTarget();
    else if (path == "/window_type") setParam ("src_window", f);
    else if (path == "/target_window_type") setParam ("tgt_window", f);
    else if (path == "/load_brain") loadLibraryFile (juce::File (a0.toString()));
    else if (path == "/save_brain") saveLibraryFile (juce::File (a0.toString()));
    else if (path == "/load_session") loadSessionFile (juce::File (a0.toString()));
    else if (path == "/save_session") saveSessionFile (juce::File (a0.toString()));
}
}
