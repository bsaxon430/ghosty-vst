// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
//
// The engine's side of the tap (Tap.h, studio/TAP.md):
//  - AUDIO thread: Engine::tapBegin / tapEnd once per buffer, tapBefore / tapPicked / tapEnded from the
//    renderer's observer. They read the engine's own numbers and memcpy 32-byte cells into the queue.
//    No lock, no allocation, no waiting, nothing written back: with nobody attached they return at once.
//  - WORKER: Engine::tapSnapBrain / tapSnapTarget copy every piece's measurements into a snapshot before
//    the library / target is handed to the audio thread.
#include "Engine.h"
#include "TapPeek.h"
#include <cfloat>
#include <cmath>
#include <complex>

using spiralcore::block;

namespace mosaic
{
namespace
{
inline float usageOf (spiralcore::brain* b, u32 i) { return const_cast<block&> (b->get_block (i)).get_usage(); }

// a piece's four rows as the engine holds them: bands, timbre, flattened bands, flattened timbre
struct Measured { const float* row[4] = { nullptr, nullptr, nullptr, nullptr }; u32 len[4] = { 0, 0, 0, 0 }; };

#if GHOSTMOSAIC_TAP_PEEK
inline u32 pieceAt (spiralcore::brain& b) { return b.*get (tap::peek::At()); }
inline Measured measuredNow (const block& b)                 // any thread: pointers into the piece itself
{
    Measured m;
    const spiralcore::sample* s[4] = { &(b.*get (tap::peek::Bands())), &(b.*get (tap::peek::Timbre())),
                                       &(b.*get (tap::peek::FlatBands())), &(b.*get (tap::peek::FlatTimbre())) };
    for (int k = 0; k < 4; ++k) { m.row[k] = s[k]->get_buffer(); m.len[k] = s[k]->get_length(); }
    return m;
}
struct RowReader { Measured of (const block& b) { return measuredNow (b); } };
#else
// THE FALLBACK (TapPeek.h): the same measurements made again from the piece's own audio, as block::process does
struct RowReader
{
    std::unique_ptr<spiralcore::FFT> fft; std::unique_ptr<Aquila::Mfcc> mfcc; u32 size = 0;
    float out[4][tap::kBands];
    Measured of (const block& b)
    {
        Measured m;
        const spiralcore::sample* pcm[2] = { &b.get_pcm(), &b.get_n_pcm() };
        for (int half = 0; half < 2; ++half)
        {
            const u32 n = pcm[half]->get_length();
            if (n == 0) continue;
            if (fft == nullptr || size != n) { fft = std::make_unique<spiralcore::FFT> (n, 44100u, 100u); mfcc = std::make_unique<Aquila::Mfcc> (n, 44100u); size = n; }
            fft->impulse2freq (pcm[half]->get_buffer());
            fft->calculate_bins();
            std::vector<std::complex<double>> spec;
            spec.reserve (n);
            for (u32 i = 0; i < n; ++i) spec.push_back (std::complex<double> (fft->m_spectrum[i][0], fft->m_spectrum[i][1]));
            const u32 nb = std::min (n, (u32) tap::kBands);
            for (u32 i = 0; i < nb; ++i) out[half * 2][i] = fft->m_bin[i];
            const std::vector<double> t = mfcc->calculate (spec, (std::size_t) tap::kTimbre);
            for (int i = 0; i < tap::kTimbre; ++i) out[half * 2 + 1][i] = (float) t[(size_t) i];
            m.row[half * 2] = out[half * 2]; m.len[half * 2] = nb;
            m.row[half * 2 + 1] = out[half * 2 + 1]; m.len[half * 2 + 1] = (u32) tap::kTimbre;
        }
        return m;
    }
};
#endif

// copies `want` numbers (zero where the piece has fewer); true when one was not a finite number (stored as 0)
bool copyRow (float* dst, const float* src, u32 have, int want)
{
    bool bad = false;
    for (int k = 0; k < want; ++k)
    {
        float v = src != nullptr && (u32) k < have ? src[k] : 0.0f;
        if (! std::isfinite (v)) { v = 0.0f; bad = true; }
        dst[k] = v;
    }
    return bad;
}
bool rowBad (const float* src, u32 have, int want)
{
    for (int k = 0; k < want && (u32) k < have; ++k) if (! std::isfinite (src[k])) return true;
    return false;
}

// the piece's outline (min / max in kEnvSlices slices, as fractions of its own peak), its peak and RMS
void outline (const spiralcore::sample& pcm, int8_t* env, float& peak, float& rms, uint8_t& flags)
{
    const u32 n = pcm.get_length();
    const float* x = pcm.get_buffer();
    float pk = 0; double e = 0;
    for (u32 i = 0; i < n; ++i) { const float v = x[i]; if (! std::isfinite (v)) continue; pk = std::max (pk, std::abs (v)); e += (double) v * v; }
    peak = pk; rms = n > 0 ? (float) std::sqrt (e / (double) n) : 0.0f;
    if (! (pk > 0.0f)) { flags |= tap::rowFlag::silent; return; }
    const float k = 127.0f / pk;
    for (int s = 0; s < tap::kEnvSlices; ++s)
    {
        const u32 from = (u32) ((uint64_t) n * (uint64_t) s / tap::kEnvSlices);
        const u32 to = std::min (n, std::max (from + 1, (u32) ((uint64_t) n * (uint64_t) (s + 1) / tap::kEnvSlices)));
        float lo = 0, hi = 0; bool any = false;
        for (u32 i = from; i < to; ++i)
        {
            const float v = x[i];
            if (! std::isfinite (v)) continue;
            lo = any ? std::min (lo, v) : v; hi = any ? std::max (hi, v) : v; any = true;
        }
        env[2 * s] = (int8_t) std::lround (lo * k); env[2 * s + 1] = (int8_t) std::lround (hi * k);
    }
}

size_t rowBytes (int n, int bandCount, bool measurements, bool flattened)
{
    const size_t perPiece = (measurements ? (size_t) (bandCount + tap::kTimbre) * sizeof (float) * (flattened ? 2u : 1u) : 0u)
                            + 3 * sizeof (float) + 2 * tap::kEnvSlices + 1;
    return (size_t) n * perPiece;
}

void fillRows (tap::Rows& rows, const spiralcore::brain& b, int n, int bandCount, bool measurements, bool flattened, RowReader& reader)
{
    const size_t N = (size_t) n, B = (size_t) bandCount, T = (size_t) tap::kTimbre;
    rows.freq.assign (N, 0.0f); rows.peak.assign (N, 0.0f); rows.rms.assign (N, 0.0f);
    rows.flags.assign (N, 0); rows.env.assign (N * 2 * tap::kEnvSlices, 0);
    if (measurements) { rows.bands.assign (N * B, 0.0f); rows.timbre.assign (N * T, 0.0f); }
    if (measurements && flattened) { rows.flatBands.assign (N * B, 0.0f); rows.flatTimbre.assign (N * T, 0.0f); }
    for (size_t i = 0; i < N; ++i)
    {
        const block& bl = b.get_block ((u32) i);
        uint8_t f = 0;
        const float fr = bl.get_freq();
        rows.freq[i] = std::isfinite (fr) ? fr : 0.0f;
        outline (bl.get_pcm(), rows.env.data() + i * 2 * tap::kEnvSlices, rows.peak[i], rows.rms[i], f);
        const Measured m = reader.of (bl);
        if (measurements)
        {
            if (copyRow (rows.bands.data() + i * B, m.row[0], m.len[0], bandCount)) f |= tap::rowFlag::badBands;
            if (copyRow (rows.timbre.data() + i * T, m.row[1], m.len[1], tap::kTimbre)) f |= tap::rowFlag::badTimbre;
        }
        if (measurements && flattened)
        {
            if (copyRow (rows.flatBands.data() + i * B, m.row[2], m.len[2], bandCount)) f |= tap::rowFlag::badFlatBands;
            if (copyRow (rows.flatTimbre.data() + i * T, m.row[3], m.len[3], tap::kTimbre)) f |= tap::rowFlag::badFlatTimbre;
        }
        else
        {
            if (m.row[2] != nullptr && rowBad (m.row[2], m.len[2], bandCount)) f |= tap::rowFlag::badFlatBands;
            if (m.row[3] != nullptr && rowBad (m.row[3], m.len[3], tap::kTimbre)) f |= tap::rowFlag::badFlatTimbre;
        }
        rows.flags[i] = f;
    }
}

int pcmLengthOf (const spiralcore::brain& b) { return b.get_num_blocks() > 0 ? (int) b.get_block (0).get_pcm().get_length() : 0; }

int bandCountOf (const spiralcore::brain& b, RowReader& reader)
{
    if (b.get_num_blocks() == 0) return 0;
    const Measured m = reader.of (b.get_block (0));
    return (int) std::min ((u32) tap::kBands, m.len[0]);
}

// the fade every piece was given, as numbers (empty when it cannot be read)
void fadeOf (const spiralcore::brain& b, int pcmLen, std::vector<float>& out, int& type)
{
    out.clear(); type = -1;
   #if GHOSTMOSAIC_TAP_PEEK
    if (pcmLen <= 0 || b.get_num_blocks() == 0 || b.get_block_size() == 0) return;
    const spiralcore::window& w = b.*get (tap::peek::Fade());
    const int t = (int) (w.*get (tap::peek::FadeType()));
    if (t < 0 || t >= (int) spiralcore::window::MAX_TYPES) return;
    type = t;
    spiralcore::sample ones ((unsigned int) pcmLen);
    for (int i = 0; i < pcmLen; ++i) ones[(unsigned int) i] = 1.0f;
    w.run (ones);
    out.resize ((size_t) pcmLen);
    for (int i = 0; i < pcmLen; ++i) { const float v = ones[(unsigned int) i]; out[(size_t) i] = std::isfinite (v) ? v : 0.0f; }
   #else
    juce::ignoreUnused (b, pcmLen);
   #endif
}

inline bool sameCell (const void* a, const void* b) noexcept { return std::memcmp (a, b, 32) == 0; }
}

namespace tap { bool fullRead() noexcept { return GHOSTMOSAIC_TAP_PEEK != 0; } }

// ------------------------------------------------------------------------------------ AUDIO thread
bool Engine::tapBegin (int numSamples, int lastNote, bool stereo, int mode, bool hostPlaying, bool hostInfoValid)
{
    auto& a = m_tap.audio;
    if (! m_tap.on()) { a.on = false; return false; }
    auto& q = m_tap.queue;
    const uint32_t asked = m_tap.resyncAsked();
    const bool fresh = ! a.on || asked != a.resyncSeen;          // a reader has just attached: everything is told again
    a.on = true; a.resyncSeen = asked;
    a.wantUsage = m_tap.usageWanted(); a.wantLive = m_tap.liveWanted();
    if (lastNote >= 0) a.lastNote = lastNote;
    if (fresh)
    {
        a.pa.kind = 0; a.pb.kind = 0; a.tr.kind = 0; a.wo.kind = 0;
        a.weightsSent = false; a.soundSig = 0; a.sinceState = 0; a.sinceMeter = 0; a.buffers = 0;
        a.peak = 0; a.limGain = 1; a.outMin[0] = a.outMin[1] = FLT_MAX; a.outMax[0] = a.outMax[1] = -FLT_MAX;
        a.every = (uint32_t) juce::jmax (16.0, m_sr / 120.0);
        a.resetSeen[0] = m_left.resetCount(); a.resetSeen[1] = m_right.resetCount();
        a.usageAt = 0; a.mirrorKnown = false; a.numRests = 0; a.restsLost = false;
        if (a.dryIdx < 0) a.dryIdx = pidx ("dry");
    }
    a.sinceState += (uint32_t) numSamples;
    const bool all = fresh || (double) a.sinceState >= 0.5 * m_sr;   // and again twice a second, whatever happened
    if (all) a.sinceState = 0;

    tapResets (0);

    // ---- which library and target are in use
    const bool live = m_live != nullptr && m_left.getTargetSource() == &m_live->left();
    const u32 srcN = m_source != nullptr ? m_source->get_num_blocks() : 0;
    const u32 tgtN = ! live && m_tl != nullptr ? m_tl->get_num_blocks() : 0;
    {
        tap::World w {};
        w.kind = tap::kWorld;
        w.flags = (uint8_t) ((srcN > 0 ? tap::worldFlag::hasLibrary : 0) | (live || tgtN > 0 ? tap::worldFlag::hasTarget : 0)
                             | (live ? tap::worldFlag::live : 0) | (m_isEffect ? tap::worldFlag::effect : 0));
        w.brainGen = a.brainGen; w.targetGen = a.targetGen; w.srcBlocks = srcN; w.tgtBlocks = tgtN;
        w.hop = live ? m_live->hop() : (tgtN > 0 ? m_tl->get_block_size() - m_tl->get_overlap() : 0);
        w.srcLen = srcN > 0 ? m_source->get_block (0).get_pcm().get_length() : 0;
        w.liveBase = live ? (int32_t) (m_liveOutStart / juce::jmax<juce::int64> (1, (juce::int64) m_live->hop())) : 0;
        if ((all || ! sameCell (&w, &a.wo)) && q.push (&w, 1)) a.wo = w;
    }

    // ---- which sounds the search may use right now (your switches AND the landscape's pick), in the engine's order
    if (m_source != nullptr)
    {
        uint32_t sig = 2166136261u ^ a.brainGen, count = 0;
        for (auto& s : m_source->get_samples()) { sig = (sig ^ (s.m_start * 2u + (s.m_enabled ? 1u : 0u))) * 16777619u; sig = (sig ^ s.m_end) * 16777619u; ++count; }
        sig ^= count;
        if (sig == 0) sig = 1;
        if (all || sig != a.soundSig)
        {
            std::memset (a.scratch, 0, sizeof (a.scratch));
            int k = 0;
            for (auto& s : m_source->get_samples())
            {
                if (k >= tap::kMaxSoundsSent) break;
                const uint32_t v[2] = { (s.m_start & 0x7fffffffu) | (s.m_enabled ? 0x80000000u : 0u), s.m_end };
                std::memcpy (reinterpret_cast<char*> (a.scratch + 1) + 8 * k, v, 8);
                ++k;
            }
            const int cells = (k + 3) / 4;
            a.scratch[0].kind = tap::kSounds; a.scratch[0].a = count > (uint32_t) k ? 1 : 0; a.scratch[0].more = (uint16_t) cells;
            a.scratch[0].w[0] = count; a.scratch[0].w[1] = a.brainGen; a.scratch[0].w[2] = (uint32_t) k;
            if (q.push (a.scratch, 1 + cells)) a.soundSig = sig;
        }
    }

    // ---- the settings the searches of this buffer will use (the renderer's own, after TERRAIN moved them)
    const Shaping& L = m_shapeLive;
    {
        const auto* sp = m_left.get_params();
        tap::ParamsA p {};
        p.kind = tap::kParamsA; p.algo = (uint8_t) m_left.get_search_algo();
        p.flags = (uint16_t) ((L.weightsOn ? tap::paramFlag::weightsOn : 0) | (! juce::exactlyEqual (m_biasTilt, 0.0f) ? tap::paramFlag::factorOn : 0)
                              | (L.timeShaped() ? tap::paramFlag::timeShaped : 0) | (L.maskOn ? tap::paramFlag::maskOn : 0)
                              | (m_netNow ? tap::paramFlag::terrainStyle : 0) | (stereo ? tap::paramFlag::stereo : 0));
        p.fftStart = sp->m_fft1_start; p.fftEnd = sp->m_fft1_end; p.stretch = m_left.get_stretch(); p.synapses = sp->m_num_synapses;
        p.ratio = sp->m_ratio; p.nRatio = sp->m_n_ratio; p.stickyness = sp->m_stickyness;
        if ((all || ! sameCell (&p, &a.pa)) && q.push (&p, 1)) a.pa = p;

        tap::ParamsB b {};
        b.kind = tap::kParamsB;
        b.autotune = m_left.get_autotune(); b.nMix = m_left.get_n_mix(); b.targetMix = m_left.get_target_mix(); b.volume = m_left.get_volume();
        b.slideError = (float) m_left.get_slide_error(); b.biasTilt = m_biasTilt;
        b.dry = m_isEffect && m_raw[(size_t) a.dryIdx] != nullptr ? m_raw[(size_t) a.dryIdx]->load (std::memory_order_relaxed) : 0.0f;
        if ((all || ! sameCell (&b, &a.pb)) && q.push (&b, 1)) a.pb = b;
    }
    if (L.weightsOn)
    {
        if (all || ! a.weightsSent || std::memcmp (a.weights, L.weights, sizeof (a.weights)) != 0)
        {
            std::memset (a.scratch, 0, 32 * (1 + tap::kWeightCells));
            a.scratch[0].kind = tap::kWeights; a.scratch[0].more = (uint16_t) tap::kWeightCells;
            std::memcpy (a.scratch + 1, L.weights, sizeof (L.weights));
            if (q.push (a.scratch, 1 + tap::kWeightCells)) { std::memcpy (a.weights, L.weights, sizeof (a.weights)); a.weightsSent = true; }
        }
    }
    else a.weightsSent = false;

    // ---- the transport (the tempo position alone does not make a new cell here: it rides with the meter)
    {
        tap::Play t {};
        t.kind = tap::kTransport; t.playMode = (uint8_t) mode;
        t.flags = (uint8_t) ((m_playingUi.load (std::memory_order_relaxed) ? tap::playFlag::wish : 0) | (m_running ? tap::playFlag::running : 0)
                             | (hostPlaying ? tap::playFlag::hostPlaying : 0) | (hostInfoValid ? tap::playFlag::hostReports : 0)
                             | (m_stopAfterFade ? tap::playFlag::stopAfterFade : 0) | (m_recorderOn.load (std::memory_order_relaxed) ? tap::playFlag::recording : 0)
                             | (live && m_livePending ? tap::playFlag::livePending : 0));
        t.notesDown = (uint8_t) juce::jmin (255, m_notesDown);
        t.velocity = m_velocity; t.bpm = (float) m_hostBpm.load (std::memory_order_relaxed); t.lastNote = a.lastNote;
        t.ppq = a.ppqSent;
        if (all || ! sameCell (&t, &a.tr))
        {
            t.ppq = m_hostPpq.load (std::memory_order_relaxed);
            if (q.push (&t, 1)) { a.ppqSent = t.ppq; a.tr = t; }
        }
    }

    // ---- pieces rested from the screen (Engine::restBlocks) since the last buffer: their "used" counter as it is now
    if (a.numRests > 0 && m_source != nullptr)
    {
        int sent = 0;
        for (; sent < a.numRests; ++sent)
        {
            tap::Rest r {};
            r.kind = tap::kRest; r.flags = a.restsLost ? tap::restFlag::someLost : 0; r.piece = a.rests[sent];
            r.usageAfter = r.piece < srcN ? usageOf (m_source, r.piece) : 0.0f;
            if (! q.push (&r, 1)) break;
        }
        if (sent == a.numRests) { a.numRests = 0; a.restsLost = false; }
    }
    else a.numRests = 0;

    // ---- (only when asked) the engine's "used" counters, one slice per buffer, going round the library
    if (a.wantUsage && srcN > 0)
    {
        if (a.usageAt >= srcN) a.usageAt = 0;
        const u32 count = juce::jmin ((u32) tap::kUsageFloats, srcN - a.usageAt);
        std::memset (a.scratch, 0, 32 * (1 + tap::kUsageCells));
        a.scratch[0].kind = tap::kUsage; a.scratch[0].more = (uint16_t) tap::kUsageCells;
        a.scratch[0].w[0] = a.usageAt; a.scratch[0].w[1] = count; a.scratch[0].w[2] = a.brainGen;
        for (u32 i = 0; i < count; ++i)
        {
            const float v = usageOf (m_source, a.usageAt + i);
            std::memcpy (reinterpret_cast<char*> (a.scratch + 1) + 4 * i, &v, 4);
        }
        if (q.push (a.scratch, 1 + tap::kUsageCells, tap::Tap::kReserve)) a.usageAt += count;
    }
    return true;
}

void Engine::tapResets (int where)
{
    auto& a = m_tap.audio;
    const RtRenderer* rs[2] = { &m_left, &m_right };
    for (int ch = 0; ch < 2; ++ch)
    {
        const uint32_t c = rs[ch]->resetCount();
        if (c == a.resetSeen[ch]) continue;
        tap::Reset r {};
        r.kind = tap::kReset; r.ch = (uint8_t) ch; r.where = (uint8_t) where; r.count = c - a.resetSeen[ch];
        if (m_tap.queue.push (&r, 1)) a.resetSeen[ch] = c;
        a.mirrorKnown = false;                                   // (a restart moves the library's "at" piece at random)
    }
}

void Engine::tapRested (juce::uint32 piece)
{
    // (runs wherever drainQueues runs: the audio thread, or the worker while the audio thread is parked)
    auto& a = m_tap.audio;
    if (! a.on) return;
    if (a.numRests < tap::kMaxRests) a.rests[a.numRests++] = piece;
    else a.restsLost = true;
}

void Engine::tapBefore (void* ctx)
{
    auto* c = static_cast<ObsCtx*> (ctx);
    auto& e = *c->e;
    auto& a = e.m_tap.audio;
    if (! a.on) return;
    RtRenderer& rr = c->ch == 0 ? e.m_left : e.m_right;
    if (rr.resetCount() != a.resetSeen[c->ch]) e.tapResets (0);
    spiralcore::brain* src = e.m_source;
    const u32 n = src->get_num_blocks();
   #if GHOSTMOSAIC_TAP_PEEK
    a.curBefore = pieceAt (*src);
   #else
    a.curBefore = a.mirrorKnown ? a.mirror : n;                  // (unknown: no "next" either)
   #endif
    a.synBefore = rr.get_params()->m_num_synapses;
    a.nextExists = a.curBefore < n && a.curBefore + 1 < n;
    a.nextBefore = a.nextExists ? usageOf (src, a.curBefore + 1) : 0.0f;
}

void Engine::tapPicked (void* ctx, juce::uint32 srcIndex, juce::uint32 tgt, juce::uint32 time, juce::uint32 renderIndex, bool advanced)
{
    auto* c = static_cast<ObsCtx*> (ctx);
    auto& e = *c->e;
    auto& a = e.m_tap.audio;
    if (! a.on) return;
    RtRenderer& rr = c->ch == 0 ? e.m_left : e.m_right;
    spiralcore::brain* src = e.m_source;
    const u32 n = src->get_num_blocks();
    const int algo = (int) rr.get_search_algo();
    const double err = src->get_current_error();
    const bool live = e.m_live != nullptr && rr.getTargetSource() == (c->ch == 0 ? &e.m_live->left() : &e.m_live->right());
   #if GHOSTMOSAIC_TAP_PEEK
    const bool prevKnown = true;
   #else
    const bool prevKnown = a.mirrorKnown;
    a.mirror = srcIndex; a.mirrorKnown = true;
   #endif

    alignas (8) tap::Cell rec[2 + 1 + tap::kLiveBlockCells];
    int cells = 2;
    tap::Match m {};
    m.kind = tap::kMatch; m.ch = (uint8_t) c->ch; m.algo = (uint8_t) algo;
    m.flags = (uint8_t) ((advanced ? tap::matchFlag::movesOn : 0) | (algo >= 2 ? tap::matchFlag::errorValid : 0)
                         | (algo >= 2 && err >= DBL_MAX ? tap::matchFlag::nothingFound : 0) | (a.nextExists ? tap::matchFlag::nextExists : 0)
                         | (live ? tap::matchFlag::liveTarget : 0) | (prevKnown ? 0 : tap::matchFlag::prevUnknown));
    m.src = srcIndex; m.tgt = tgt; m.time = time; m.prevSrc = prevKnown ? a.curBefore : 0; m.renderIndex = renderIndex;
    m.novelty = rr.get_params()->m_usage_importance; m.boredom = src->get_usage_falloff();
    tap::Used u {};
    u.kind = tap::kUsed; u.ch = (uint8_t) c->ch;
    u.nextBefore = a.nextBefore;
    u.srcAfter = srcIndex < n ? usageOf (src, srcIndex) : 0.0f;
    u.nextAfter = a.nextExists ? usageOf (src, a.curBefore + 1) : 0.0f;
    u.synapses = a.synBefore; u.error = err;

   #if GHOSTMOSAIC_TAP_PEEK
    if (live && a.wantLive)
    {
        // the live-input piece this pick was matched to: its measurements, copied as they are (the reader tidies them)
        const block& tb = rr.getTargetSource()->get_block (tgt);
        const Measured tm = measuredNow (tb);
        std::memset (rec + 2, 0, 32 * (1 + tap::kLiveBlockCells));
        rec[2].kind = tap::kLiveBlock; rec[2].a = (uint8_t) c->ch; rec[2].more = (uint16_t) tap::kLiveBlockCells; rec[2].w[0] = tgt;
        const float fr = tb.get_freq();
        std::memcpy (&rec[2].w[1], &fr, 4);
        auto* out = reinterpret_cast<char*> (rec + 3);
        static constexpr int at[4] = { 0, tap::kBands, tap::kBands + tap::kTimbre, 2 * tap::kBands + tap::kTimbre };
        static constexpr int want[4] = { tap::kBands, tap::kTimbre, tap::kBands, tap::kTimbre };
        for (int k = 0; k < 4; ++k)
            if (tm.row[k] != nullptr)
                std::memcpy (out + 4 * at[k], tm.row[k], 4 * (size_t) juce::jmin ((u32) want[k], tm.len[k]));
        rec[2].w[2] = juce::jmin ((u32) tap::kBands, tm.len[0]);
        m.flags = (uint8_t) (m.flags | tap::matchFlag::liveBlock);
        cells += 1 + tap::kLiveBlockCells;
    }
   #endif
    std::memcpy (rec, &m, 32); std::memcpy (rec + 1, &u, 32);
    e.m_tap.queue.push (rec, cells, tap::Tap::kReserve);
}

void Engine::tapEnded (void* ctx, juce::uint32 srcIndex, juce::uint32 tgt, juce::uint32 time, float pos)
{
    auto* c = static_cast<ObsCtx*> (ctx);
    auto& a = c->e->m_tap.audio;
    if (! a.on) return;
    tap::End x {};
    x.kind = tap::kEnd; x.ch = (uint8_t) c->ch; x.src = srcIndex; x.tgt = tgt; x.time = time; x.lastPos = pos;
    c->e->m_tap.queue.push (&x, 1, tap::Tap::kReserve);
}

void Engine::tapEnd (const float* left, const float* right, int len, float peak)
{
    auto& a = m_tap.audio;
    auto& q = m_tap.queue;
    tapResets (1);
    a.clock += (uint32_t) len; a.sinceMeter += (uint32_t) len; ++a.buffers;
    a.peak = juce::jmax (a.peak, peak);
    a.limGain = juce::jmin (a.limGain, m_limGain.load (std::memory_order_relaxed));
    {
        // the shape of the finished output: one read-only pass per channel (as the level meter's)
        const auto l = juce::FloatVectorOperations::findMinAndMax (left, len), r = juce::FloatVectorOperations::findMinAndMax (right, len);
        a.outMin[0] = juce::jmin (a.outMin[0], l.getStart()); a.outMax[0] = juce::jmax (a.outMax[0], l.getEnd());
        a.outMin[1] = juce::jmin (a.outMin[1], r.getStart()); a.outMax[1] = juce::jmax (a.outMax[1], r.getEnd());
    }
    if (a.sinceMeter < a.every) return;

    tap::Meter m {};
    m.kind = tap::kMeter;
    m.flags = (uint8_t) ((m_running ? tap::meterFlag::running : 0) | (m_limOn ? tap::meterFlag::limiterOn : 0)
                         | ((a.pa.flags & tap::paramFlag::stereo) != 0 ? tap::meterFlag::stereo : 0) | ((a.wo.flags & tap::worldFlag::live) != 0 ? tap::meterFlag::live : 0));
    m.buffers = (uint16_t) juce::jmin (65535u, a.buffers); m.clock = a.clock;
    m.peak = a.peak; m.limGain = a.limGain; m.fade = m_fade; m.velGain = a.tr.playMode == 1 ? m_velSmooth : 1.0f;
    m.lost = (uint32_t) q.lostCells(); m.parked = a.parked;
    q.push (&m, 1);
    tap::Shape s {};
    s.kind = tap::kShape; s.minL = a.outMin[0]; s.maxL = a.outMax[0]; s.minR = a.outMin[1]; s.maxR = a.outMax[1];
    q.push (&s, 1);
    const RtRenderer* rs[2] = { &m_left, &m_right };
    for (int ch = 0; ch < 2; ++ch)
    {
        tap::Clock k {};
        k.kind = tap::kClock; k.ch = (uint8_t) ch; k.renderTime = rs[ch]->getRenderTime(); k.targetTime = rs[ch]->getTargetTime();
        k.active = (uint32_t) rs[ch]->activeBlocks(); k.droppedBlocks = (uint32_t) rs[ch]->droppedBlocks(); k.keyPitch = rs[ch]->getKeyPitch();
        q.push (&k, 1);
    }
    if (const double ppq = m_hostPpq.load (std::memory_order_relaxed); ! juce::exactlyEqual (ppq, a.ppqSent) && a.tr.kind != 0)
    {
        tap::Play t = a.tr;
        t.ppq = ppq; t.bpm = (float) m_hostBpm.load (std::memory_order_relaxed);
        if (q.push (&t, 1)) { a.ppqSent = ppq; a.tr = t; }
    }
    a.sinceMeter = 0; a.buffers = 0; a.peak = 0; a.limGain = 1;
    a.outMin[0] = a.outMin[1] = FLT_MAX; a.outMax[0] = a.outMax[1] = -FLT_MAX;
}

// ------------------------------------------------------------------------------------ WORKER: snapshots
void Engine::tapSnapBrain (const brain& b, const LibraryPicture& pic)
{
    auto s = std::make_shared<tap::BrainSnapshot>();
    RowReader reader;
    const int n = (int) b.get_num_blocks();
    s->gen = ++m_tapGens;
    s->numBlocks = n; s->rate = m_sr;
    if (n > 0)
    {
        s->blockSize = (int) b.get_block_size(); s->overlap = (int) b.get_overlap(); s->hop = s->blockSize - s->overlap;
        s->pcmLen = pcmLengthOf (b); s->bandCount = bandCountOf (b, reader);
    }
    // the sounds, in the engine's order (a sound's last piece is `last`: the search stops one short of it)
    {
        s->sound.assign ((size_t) n, (uint16_t) 0xffff);
        int k = 0;
        for (auto& snd : const_cast<brain&> (b).get_samples())
        {
            tap::BrainSnapshot::Sound o;
            o.key = juce::String (snd.m_filename);
            o.name = k < (int) pic.soundNames.size() ? pic.soundNames[(size_t) k] : o.key;
            o.enabledAtBuild = snd.m_enabled;
            o.lengthSamples = (int64_t) snd.m_sample.get_length();
            o.first = (int) juce::jmin (snd.m_start, (u32) n);
            const bool any = n > 0 && snd.m_start < (u32) n && snd.m_end >= snd.m_start && snd.m_end != 0xffffffffu;
            o.last = any ? (int) juce::jmin (snd.m_end, (u32) (n - 1)) : o.first - 1;
            if (k < 0xffff) for (int i = o.first; i <= o.last; ++i) s->sound[(size_t) i] = (uint16_t) k;
            s->sounds.push_back (o);
            ++k;
        }
    }
    // the measurements, inside the byte budget (the flattened rows go first, then all of them)
    {
        const size_t fixed = (size_t) n * (2 + 3 * sizeof (float) + 2) + tap::kSynapseBudget;
        const bool fits = fixed + rowBytes (n, s->bandCount, true, true) <= tap::kSnapshotBudget;
        const bool fitsPlain = fixed + rowBytes (n, s->bandCount, true, false) <= tap::kSnapshotBudget;
        s->hasMeasurements = n > 0 && (fits || fitsPlain); s->hasFlattened = n > 0 && fits;
        fillRows (s->rows, b, n, s->bandCount, s->hasMeasurements, s->hasFlattened, reader);
    }
    if (pic.bright.size() == (size_t) n && pic.centroidHz.size() == (size_t) n) { s->bright = pic.bright; s->centroidHz = pic.centroidHz; }
    else pieceBrightness (b, m_sr, s->bright, s->centroidHz);
    for (auto* v : { &s->bright, &s->centroidHz }) for (auto& x : *v) if (! std::isfinite (x)) x = 0.0f;
    // the "used" counters as they are handed over (raw: a library read from a file does not start at 0), and the network
    s->usage0.resize ((size_t) n); s->synCount.assign ((size_t) n, 0);
    size_t most = 0;
    for (int i = 0; i < n; ++i)
    {
        s->usage0[(size_t) i] = const_cast<block&> (b.get_block ((u32) i)).get_usage();
        const size_t c = b.get_block ((u32) i).get_synapse_const().size();
        s->synCount[(size_t) i] = (uint16_t) juce::jmin ((size_t) 0xffff, c);
        most = juce::jmax (most, c);
    }
    if (n > 0)
    {
        s->synK = (int) juce::jmin (most, juce::jmax ((size_t) 64, tap::kSynapseBudget / (4 * (size_t) n)));
        s->syn.assign ((size_t) n * (size_t) s->synK, 0xffffffffu);
        for (int i = 0; i < n; ++i)
        {
            const auto& syn = b.get_block ((u32) i).get_synapse_const();
            const size_t c = juce::jmin (syn.size(), (size_t) s->synK);
            if (c > 0) std::memcpy (s->syn.data() + (size_t) i * (size_t) s->synK, syn.data(), c * sizeof (uint32_t));
        }
    }
    fadeOf (b, s->pcmLen, s->window, s->windowType);
    s->bytes = s->rows.bytes() + s->sound.size() * 2 + (s->bright.size() + s->centroidHz.size() + s->usage0.size() + s->window.size()) * sizeof (float)
               + s->synCount.size() * 2 + s->syn.size() * 4;
    { const juce::ScopedLock l (m_modelLock); m_tapBrain = s; }
    m_tapPendingBrain = s->gen;
}

void Engine::tapSnapTarget (const brain& left, const brain& right)
{
    auto s = std::make_shared<tap::TargetSnapshot>();
    RowReader reader;
    const int n = (int) juce::jmin (left.get_num_blocks(), right.get_num_blocks());
    s->gen = ++m_tapGens;
    s->numBlocks = n; s->rate = m_sr;
    if (n > 0)
    {
        s->blockSize = (int) left.get_block_size(); s->overlap = (int) left.get_overlap(); s->hop = s->blockSize - s->overlap;
        s->pcmLen = pcmLengthOf (left); s->bandCount = bandCountOf (left, reader);
    }
    const bool fits = 2 * rowBytes (n, s->bandCount, true, true) <= tap::kSnapshotBudget;
    const bool fitsPlain = 2 * rowBytes (n, s->bandCount, true, false) <= tap::kSnapshotBudget;
    s->hasMeasurements = n > 0 && (fits || fitsPlain); s->hasFlattened = n > 0 && fits;
    fillRows (s->rows[0], left, n, s->bandCount, s->hasMeasurements, s->hasFlattened, reader);
    fillRows (s->rows[1], right, n, s->bandCount, s->hasMeasurements, s->hasFlattened, reader);
    fadeOf (left, s->pcmLen, s->window, s->windowType);
    s->bytes = s->rows[0].bytes() + s->rows[1].bytes() + s->window.size() * sizeof (float);
    {
        const juce::ScopedLock l (m_modelLock);
        s->name = m_targetName;
        m_tapTarget = s;
    }
    m_tapPendingTarget = s->gen;
}
}
