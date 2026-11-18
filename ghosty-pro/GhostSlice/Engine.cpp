#include "Engine.h"
#include "Hold.h"
#include "CleanStage.h"
#include "Psola.h"
#include "FormantFix.h"

namespace gslice
{
// ---------------------------------------------------------------- StretchStream
void StretchStream::configure (Preset p, double sampleRate, int maxChunk)
{
    sr = sampleRate; maxN = maxChunk;
    split = p == Preset::Live;   // LIVE spreads each spectral step over its interval (one more interval of latency, primed away)
    if (split) st.presetCheaper (2, (float) sampleRate, true);
    else st.presetDefault (2, (float) sampleRate, false);
    cap = (int) (sampleRate * 0.8) + maxChunk * 5 + 64;   // > inputLatency + 4 x outputLatency (the longest seek) and 4x a chunk
    for (int c = 0; c < 2; ++c) { in[c].assign ((size_t) cap, 0.0f); out[c].assign ((size_t) maxChunk, 0.0f); }
    curPitch = curFormant = 1e9;
}

void StretchStream::setPitch (double p, double f, double ton, double base)
{
    basePitch = p; baseFormant = f; baseTon = ton; baseHz = base;
    if (curve == nullptr) applyPitch (p, f, ton, base);
}

void StretchStream::applyCurve (int64 at)
{
    const double v = map->srcAt ((double) at);
    const double actual = rd.reverse ? rd.mirror - 1.0 - v : v;
    float c = 0, f = 0, b = 0; curve->at (actual, c, f, b);
    applyPitch (basePitch + (double) c / 100.0, baseFormant + (double) f / 100.0, baseTon, b > 0 ? (double) b : baseHz);
}

void StretchStream::applyPitch (double p, double f, double ton, double base)
{
    if (p == curPitch && f == curFormant && ton == curTon && base == curBase) return;
    curPitch = p; curFormant = f; curTon = ton; curBase = base;
    st.setTransposeSemitones ((float) (p + curSteer / 100.0), (float) (ton / sr));
    st.setFormantBase (base > 0 ? (float) (base / sr) : 0.0f);   // the voice's f0 (phase 9): the formant analysis starts from it
    if (std::abs (p) > 1e-9 || std::abs (f) > 1e-9) st.setFormantSemitones ((float) f, true);   // FORMANT 0 = keep the voice's formants
    else st.setFormantFactor (1.0f, false);
}

void StretchStream::fill (int64 from, int n)
{
    for (int c = 0; c < 2; ++c) { float* d = in[c].data(); for (int i = 0; i < n; ++i) d[i] = rd.at (c, from + i); }
}

void StretchStream::start (const WarpMap& m, const PieceReader& r, int64 u0, double p, double f, double ton, double base, const PitchCurve* c)
{
    map = &m; rd = r; u = u0; curve = c;
    curPitch = curFormant = 1e9; setPitch (p, f, ton, base);
    if (curve != nullptr) applyCurve (u0);
    const double rate = juce::jlimit (limits::kHardMin, limits::kHardMax, m.rateAt ((double) u0));
    if (split)
    {   // LIVE (split computation): outputSeek()'s reflected pre-roll lands one interval off when the computation is split (a
        // step at ~30 ms), so start the documented way instead: reset, seek() the first inputLatency() frames, then run
        // outputLatency() frames of pre-roll (dropped) - the next render() starts exactly at u0
        st.reset();
        const int inLat = st.inputLatency(), outLat = st.outputLatency();
        const int64 a = (int64) std::llround (m.srcAt ((double) u0));
        fill (a, inLat);
        st.seek (Ptrs { { in[0].data(), in[1].data() } }, inLat, rate);
        inPos = a + inLat; u = u0 - outLat;
        skip (outLat);
        return;
    }
    const int seekLen = juce::jlimit (1, cap, st.outputSeekLength ((float) rate));
    const int64 a = (int64) std::llround (m.srcAt ((double) u0));
    fill (a, seekLen);
    st.outputSeek (Ptrs { { in[0].data(), in[1].data() } }, seekLen);
    inPos = a + seekLen;
}

double StretchStream::steerAt (int64 at) const noexcept
{
    if (steerCents == 0.0 || at < steerA || at >= steerB || steerB <= steerA) return 0.0;
    return steerCents * 0.5 * (1.0 - std::cos (juce::MathConstants<double>::twoPi * (double) (at - steerA) / (double) (steerB - steerA)));
}

void StretchStream::render (float* L, float* R, int n)
{
    jassert (map != nullptr && n <= maxN);
    if (curve == nullptr && steerCents == 0.0) { renderChunk (L, R, n); return; }
    // GHOST PITCH: the shift follows the curve; the stretcher wants automation one output latency ahead of the output
    for (int i = 0; i < n; i += kCurveStep)
    {
        const int m = juce::jmin (kCurveStep, n - i);
        const int64 at = u + m / 2 + st.outputLatency();
        if (curve != nullptr) applyCurve (at);
        if (steerCents != 0.0)
        {   // the steer rides on whatever transposition is set; an unpitched stream keeps its formant settings (no mode switch)
            const double sc = steerAt (at);
            if (sc != curSteer) { curSteer = sc; st.setTransposeSemitones ((float) (curPitch + sc / 100.0), (float) (curTon / sr)); }
        }
        renderChunk (L + i, R + i, m);
    }
}

void StretchStream::skip (int64 n)
{
    while (n > 0) { const int m = (int) std::min<int64> (n, maxN); render (out[0].data(), out[1].data(), m); n -= m; }
}

void StretchStream::renderChunk (float* L, float* R, int n)
{
    const int64 target = (int64) std::llround (map->srcAt ((double) (u + n + st.outputLatency()))) + st.inputLatency();
    const int cnt = (int) juce::jlimit<int64> (0, cap, target - inPos);
    fill (inPos, cnt);
    st.process (Ptrs { { in[0].data(), in[1].data() } }, cnt, Ptrs { { L, R } }, n);
    inPos += cnt; u += n;
}

// ---------------------------------------------------------------- STUDIO renders
namespace render
{
static void directInto (const Piece& pc, const WorkingAudio& w, int64 u, float* L, float* R, int n)
{
    PieceReader rd { &w, pc.reverse, pc.mirror };
    size_t seg = pc.map.segAt ((double) u);
    for (int i = 0; i < n; ++i)
    {
        const double x = (double) (u + i);
        while (seg + 2 < pc.map.t.size() && x >= pc.map.t[seg + 1]) ++seg;
        const int64 idx = (int64) std::llround (pc.map.s[seg] + (x - pc.map.t[seg]) * pc.map.rateOf (seg));
        L[i] = rd.at (0, idx); R[i] = rd.at (1, idx);
    }
}

// TRANSIENT LOCK splice: inside each locked 1x span the original attack replaces the stretched sound (1 ms in, 4 ms out), the
// same function for LIVE and STUDIO. The spans come from the source's onset list (the spectral-flux + HFC detector, phase 7).
void spliceTransients (const Piece& pc, const WorkingAudio& w, int64 u, float* L, float* R, int n)
{
    if (pc.splices.empty()) return;
    const double in = 0.001 * w.rate, out = 0.004 * w.rate;
    PieceReader rd { &w, pc.reverse, pc.mirror };
    for (auto& [a, b] : pc.splices)
    {
        if (b <= (double) u || a >= (double) (u + n)) continue;
        for (int i = juce::jmax (0, (int) std::floor (a - (double) u)); i < n && (double) (u + i) < b; ++i)
        {
            const double x = (double) (u + i);
            if (x < a) continue;
            const float wgt = fadeShape ((x - a) / in) * fadeShape ((b - x) / out);
            const int64 idx = (int64) std::llround (pc.map.srcAt (x));
            L[i] += wgt * (rd.at (0, idx) - L[i]); R[i] += wgt * (rd.at (1, idx) - R[i]);
        }
    }
}

void markTransients (StudioBuffer& b, double sampleRate)
{
    const int fr = juce::jmax (8, (int) std::lround (sampleRate * 0.001)); b.hotFrame = fr;
    const int n = b.audio.getNumSamples(), nf = n / fr + 1;
    std::vector<double> E ((size_t) nf, 0.0); double peak = 0;
    for (int f = 0; f < nf; ++f)
    {
        double e = 0; const int a = f * fr, z = juce::jmin (n, a + fr);
        for (int c = 0; c < b.audio.getNumChannels(); ++c) { const float* x = b.audio.getReadPointer (c); for (int i = a; i < z; ++i) e += (double) x[i] * x[i]; }
        E[(size_t) f] = e; peak = std::max (peak, e);
    }
    b.hot.assign ((size_t) nf, 0);
    for (int f = 1; f < nf; ++f)
    {
        double prev = 0; int cnt = 0; for (int k = juce::jmax (0, f - 10); k < f; ++k) { prev += E[(size_t) k]; ++cnt; }
        prev /= juce::jmax (1, cnt);
        if (E[(size_t) f] > 4.0 * prev + 1e-12 && E[(size_t) f] > peak * 1e-4)
            for (int k = juce::jmax (0, f - 5); k <= juce::jmin (nf - 1, f + 5); ++k) b.hot[(size_t) k] = 1;
    }
}

std::shared_ptr<const StudioBuffer> piece (const Piece& pc, const WorkingAudio& w, double pitch, double formant, const std::function<bool()>& cancel)
{
    auto b = std::make_shared<StudioBuffer>(); b->key = pc.studioKey;
    const int64 len = pc.length();
    b->audio.setSize (2, (int) len); b->audio.clear();
    float* L = b->audio.getWritePointer (0); float* R = b->audio.getWritePointer (1);
    constexpr int kChunk = 512;   // fixed chunking: the render never depends on the host's buffer size
    const auto* H = pc.hold.get();
    if (H != nullptr && H->mode == HoldMode::Sustain && ! H->grains.empty())
    {   // SUSTAIN: the grain schedule (the LIVE path renders the very same samples)
        for (int64 i = 0; i < len; i += kChunk) hold::renderGrains (*H, w, pc.u0 - H->uStart + i, L + i, R + i, (int) std::min<int64> (kChunk, len - i));
    }
    else if (H != nullptr && (H->mode == HoldMode::Texture || H->mode == HoldMode::Freeze))
    {
        juce::AudioBuffer<float> full;
        if (! hold::renderSpectral (*H, w, full, cancel)) return nullptr;
        const int64 off = pc.u0 - H->uStart;
        for (int c = 0; c < 2; ++c) for (int64 i = 0; i < len; ++i) { const int64 k = off + i; (c == 0 ? L : R)[i] = k >= 0 && k < full.getNumSamples() ? full.getSample (c, (int) k) : 0.0f; }
    }
    else if (! pc.stretch)
    {
        for (int64 i = 0; i < len; i += kChunk) directInto (pc, w, pc.u0 + i, L + i, R + i, (int) std::min<int64> (kChunk, len - i));
    }
    else
    {
        // GHOST PITCH engines (phase 15): a curve piece renders VOCAL (our TD-PSOLA) where its notes chose it, WIDE (the stretcher
        // + the LPC formant correction) elsewhere, the two joined by the curve's 10 ms raised-cosine share
        const PitchCurve* C = pc.curve.get();
        const bool vocal = C != nullptr && C->anyVocal() && ! pc.reverse && H == nullptr && pc.pitchOffset == 0.0 && pc.formantOffset == 0.0;
        auto vocalShare = [&] (int64 u) -> float { return vocal ? C->vocalAt (pc.map.srcAt ((double) u)) : 0.0f; };
        bool needWide = ! vocal;
        if (vocal) for (int64 i = 0; i < len && ! needWide; i += 64) needWide = vocalShare (pc.u0 + i) < 1.0f;
        if (vocal && ! needWide) needWide = vocalShare (pc.u0 + len - 1) < 1.0f;
        if (needWide)
        {
            auto wide = [&] (double steerCents, int64 steerA, int64 steerB) -> bool
            {
                auto s = std::make_unique<StretchStream> ((long) (pc.studioKey & 0x7fffffff));   // seeded: the same piece always renders the same samples
                s->configure (StretchStream::Preset::Studio, w.rate, kChunk);
                const int64 pre = H != nullptr ? hold::elasticPreroll (*H, pc.u0) : 0;   // a HOLD's in join: settle at 1x on the head first
                s->start (pc.map, PieceReader { &w, pc.reverse, pc.mirror }, pc.u0 - pre, pitch + pc.pitchOffset, formant + pc.formantOffset, pc.tonalityHz, pc.formantBaseHz, pc.curve.get());
                s->skip (pre);
                s->steer (steerA, steerB, steerCents);
                for (int64 i = 0; i < len; i += kChunk)
                {
                    if (cancel && (i & 0x7fff) == 0 && cancel()) return false;
                    s->render (L + i, R + i, (int) std::min<int64> (kChunk, len - i));
                    spliceTransients (pc, w, pc.u0 + i, L + i, R + i, (int) std::min<int64> (kChunk, len - i));
                }
                return true;
            };
            if (! wide (0.0, 0, 0)) return nullptr;
            hold::Steer sp;
            if (H != nullptr && hold::steerSpan (*H, pc.u0, len, sp))
            {   // ELASTIC's out join (see hold::joinDrift): candidate phase steers, the most continuous join wins (first good one ends it)
                double bestD = hold::joinDrift (*H, w, L, R, len);
                if (bestD > hold::kSteerGoodCents)
                {
                    juce::AudioBuffer<float> best (2, (int) len); best.copyFrom (0, 0, L, (int) len); best.copyFrom (1, 0, R, (int) len);
                    for (double c : hold::kSteerCandidates)
                    {
                        if (c == 0.0) continue;
                        if (! wide (c, sp.a, sp.b)) return nullptr;
                        const double d = hold::joinDrift (*H, w, L, R, len);
                        if (d < bestD) { bestD = d; best.copyFrom (0, 0, L, (int) len); best.copyFrom (1, 0, R, (int) len); }
                        if (bestD <= hold::kSteerGoodCents) break;
                    }
                    std::copy (best.getReadPointer (0), best.getReadPointer (0) + len, L); std::copy (best.getReadPointer (1), best.getReadPointer (1) + len, R);
                }
            }
            if (H != nullptr) for (int64 i = 0; i < len; i += kChunk) hold::applyJoins (*H, w, pc.u0 - H->uStart + i, L + i, R + i, (int) std::min<int64> (kChunk, len - i));
            if (C != nullptr && H == nullptr)
                formantfix::apply (pc, w, pitch + pc.pitchOffset, formant + pc.formantOffset, L, R, len, vocal ? std::function<float (int64)> ([&] (int64 u) { return 1.0f - vocalShare (u); }) : std::function<float (int64)>());
        }
        if (vocal)
        {
            if (cancel && cancel()) return nullptr;
            juce::AudioBuffer<float> v (2, (int) len);
            psola::render (pc, w, pitch, formant, v.getWritePointer (0), v.getWritePointer (1), len);
            // the grains keep the envelope as seen through their 2-period window: narrow formants come out blurred, so the edited
            // voiced frames get the same envelope correction as WIDE (a few dB; FORMANT still moves it)
            formantfix::apply (pc, w, pitch, formant, v.getWritePointer (0), v.getWritePointer (1), len, [&] (int64 u) { return vocalShare (u); });
            const float* v0 = v.getReadPointer (0); const float* v1 = v.getReadPointer (1);
            for (int64 i = 0; i < len; ++i)
            {
                const float k = needWide ? vocalShare (pc.u0 + i) : 1.0f;
                if (k >= 1.0f) { L[i] = v0[i]; R[i] = v1[i]; }
                else if (k > 0.0f) { L[i] += k * (v0[i] - L[i]); R[i] += k * (v1[i] - R[i]); }
            }
        }
    }
    markTransients (*b, w.rate);
    return b;
}

juce::AudioBuffer<float> plan (const Plan& p, const WorkingAudio& w, StudioCache* cache, bool ceiling)
{
    juce::AudioBuffer<float> out (2, (int) juce::jmax<int64> (1, p.length)); out.clear();
    for (auto& pc : p.pieces)
    {
        std::shared_ptr<const StudioBuffer> b = cache != nullptr ? cache->get (pc.studioKey) : nullptr;
        if (b == nullptr) { b = piece (pc, w, p.pitchSemis, p.formantSemis); if (cache != nullptr && b != nullptr) cache->put (b); }
        if (b == nullptr) continue;
        for (int c = 0; c < 2; ++c)
        {
            const float* x = b->audio.getReadPointer (c); float* y = out.getWritePointer (c) + pc.dst0;
            for (int64 i = 0; i < pc.length(); ++i) y[i] += x[i] * pc.envelope (pc.u0 + i);
        }
    }
    if (ceiling)
    {
        ghosty::clean::Ceiling cl; cl.prepare (w.rate);
        const int lat = cl.latency(), n = out.getNumSamples();
        juce::AudioBuffer<float> tmp (2, n + lat); tmp.clear();
        for (int c = 0; c < 2; ++c) tmp.copyFrom (c, 0, out, c, 0, n);
        for (int i = 0; i < n + lat; i += 512) cl.process (tmp.getWritePointer (0) + i, tmp.getWritePointer (1) + i, juce::jmin (512, n + lat - i));
        for (int c = 0; c < 2; ++c) out.copyFrom (c, 0, tmp, c, lat, n);
    }
    return out;
}
} // namespace render

// ---------------------------------------------------------------- StudioCache
StudioCache::StudioCache() { worker = std::thread ([this] { run(); }); }
StudioCache::~StudioCache() { { std::lock_guard<std::mutex> l (m); quit = true; } cv.notify_all(); if (worker.joinable()) worker.join(); }

void StudioCache::request (std::shared_ptr<const WorkingAudio> w, std::vector<Job> jobs)
{
    std::lock_guard<std::mutex> l (m);
    if (w != work) { work = w; }
    wanted.clear(); queue.clear();
    std::set<uint64> seen;
    for (auto& j : jobs)
    {
        wanted.insert (j.piece.studioKey);
        if (done.count (j.piece.studioKey) || ! seen.insert (j.piece.studioKey).second) continue;
        queue.push_back (std::move (j));
    }
    std::stable_sort (queue.begin(), queue.end(), [] (const Job& a, const Job& b) { return a.distance < b.distance; });
    requested.store ((int) queue.size() + (busy && wanted.count (busyKey.load()) ? 1 : 0));
    cv.notify_all();
}

std::shared_ptr<const StudioBuffer> StudioCache::get (uint64 key) const
{
    std::lock_guard<std::mutex> l (m); auto it = done.find (key); return it == done.end() ? nullptr : it->second;
}
void StudioCache::put (std::shared_ptr<const StudioBuffer> b)
{
    if (b == nullptr) return;
    std::lock_guard<std::mutex> l (m); done[b->key] = b;
}
size_t StudioCache::pending() const { std::lock_guard<std::mutex> l (m); return queue.size() + (busy ? 1 : 0); }
size_t StudioCache::bytes() const
{
    std::lock_guard<std::mutex> l (m); size_t s = 0;
    for (auto& [k, b] : done) s += (size_t) b->audio.getNumSamples() * 8;
    return s;
}
void StudioCache::clear() { std::lock_guard<std::mutex> l (m); done.clear(); queue.clear(); wanted.clear(); }
bool StudioCache::waitIdle (int timeoutMs)
{
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    while (juce::Time::getMillisecondCounterHiRes() - t0 < timeoutMs)
    {
        { std::lock_guard<std::mutex> l (m); if (queue.empty() && ! busy) return true; }
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
    }
    return false;
}

void StudioCache::run()
{
    while (true)
    {
        Job job; std::shared_ptr<const WorkingAudio> w;
        {
            std::unique_lock<std::mutex> l (m);
            cv.wait (l, [this] { return quit || ! queue.empty(); });
            if (quit) return;
            job = std::move (queue.front()); queue.erase (queue.begin()); w = work;
            if (done.count (job.piece.studioKey) || w == nullptr) continue;
            busy = true; busyKey.store (job.piece.studioKey);
        }
        const uint64 key = job.piece.studioKey;
        auto b = render::piece (job.piece, *w, job.pitch, job.formant, [this, key] { std::lock_guard<std::mutex> l (m); return quit || wanted.count (key) == 0; });
        {
            std::lock_guard<std::mutex> l (m);
            busy = false;
            if (b != nullptr && wanted.count (key))
            {
                done[key] = b;
                // LRU-ish: over the limit, drop renders the current plan does not use
                size_t total = 0; for (auto& [k, x] : done) total += (size_t) x->audio.getNumSamples() * 8;
                for (auto it = done.begin(); it != done.end() && total > maxBytes;)
                    if (! wanted.count (it->first)) { total -= (size_t) it->second->audio.getNumSamples() * 8; it = done.erase (it); } else ++it;
            }
        }
        if (b != nullptr && onRendered) onRendered();
    }
}

// ---------------------------------------------------------------- Player
static bool wantsStudio (const Piece& pc) noexcept
{
    if (! pc.stretch) return false;
    if (pc.hold != nullptr && pc.hold->liveIsExact() && ! pc.hold->grains.empty()) return false;   // SUSTAIN plays its grains exactly
    return true;
}

void Player::fillStudio (const StudioCache& cache) noexcept
{
    PlayTable* t = current.load (std::memory_order_acquire);
    if (t == nullptr || ! t->useStudio) return;
    for (size_t i = 0; i < t->plan.pieces.size() && i < t->studio.size(); ++i)
        if (t->studio[i] == nullptr && wantsStudio (t->plan.pieces[i])) t->studio[i] = cache.get (t->plan.pieces[i].studioKey);   // (fills an empty slot: frees nothing)
}

int Player::missingStudio (int64 T, int64 n) const noexcept
{
    const PlayTable* t = current.load (std::memory_order_acquire);
    if (t == nullptr || ! t->useStudio || t->work == nullptr) return 0;
    int miss = 0;
    for (size_t i = 0; i < t->plan.pieces.size() && i < t->studio.size(); ++i)
    {
        const auto& pc = t->plan.pieces[i];
        if (pc.dst1 > T && pc.dst0 < T + n && t->studio[i] == nullptr && wantsStudio (pc)) ++miss;
    }
    return miss;
}

Player::Player()
{
    for (auto& s : stretchers) s = std::make_unique<StretchStream>();
    primeThread = std::thread ([this] { primeLoop(); });
}

Player::~Player()
{
    primeQuit.store (true);
    if (primeThread.joinable()) primeThread.join();
}

// ---------------------------------------------------------------- PRIMER (background stretcher starts)
void Player::primeLoop()
{
    uint32_t seen = renderCount.load(); int quiet = 0;
    while (! primeQuit.load (std::memory_order_relaxed))
    {
        bool any = false;
        for (auto& pr : primes)
        {
            int e = PRequested;
            if (! pr.state.compare_exchange_strong (e, PWorking, std::memory_order_acq_rel)) continue;
            any = true;
            if (! pr.cancel.load (std::memory_order_acquire))
            {   // exactly what startPath does in the block (same start, same skip chunks): the stream is sample-identical
                const PlayTable& t = *pr.table; const Piece& pc = t.plan.pieces[(size_t) pr.pi];
                auto& st = *stretchers[(size_t) pr.st];
                st.start (pc.map, PieceReader { t.work.get(), pc.reverse, pc.mirror }, pr.u - pr.pre, t.plan.pitchSemis + pc.pitchOffset,
                          t.plan.formantSemis + pc.formantOffset, pc.tonalityHz, pc.formantBaseHz, pc.curve.get());
                st.skip (pr.pre);
            }
            pr.state.store (PReady, std::memory_order_release);
        }
        if (any) continue;
        // poll: every 2 ms while the host calls the audio callback (the look-ahead is 150 ms), 20 ms when it does not
        const uint32_t rc = renderCount.load (std::memory_order_relaxed);
        if (rc != seen) { seen = rc; quiet = 0; } else if (quiet < 1000) ++quiet;
        std::this_thread::sleep_for (std::chrono::milliseconds (quiet < 250 ? 2 : 20));
    }
}

void Player::releasePrime (Prime& pr) noexcept
{
    int s = pr.state.load (std::memory_order_acquire);
    if (s == PRequested)
    {
        int e = PRequested;
        if (pr.state.compare_exchange_strong (e, PFree, std::memory_order_acq_rel)) { if (pr.st >= 0) stBusy[pr.st] = false; pr.st = -1; pr.table = nullptr; return; }
        s = pr.state.load (std::memory_order_acquire);
    }
    if (s == PWorking) { pr.cancel.store (true, std::memory_order_release); return; }   // freed once the worker hands it back
    if (s == PReady) { if (pr.st >= 0) stBusy[pr.st] = false; pr.st = -1; pr.table = nullptr; pr.cancel.store (false); pr.state.store (PFree, std::memory_order_release); }
}

void Player::dropPrimes (bool wait) noexcept
{
    for (auto& pr : primes)
    {
        releasePrime (pr);
        if (wait)
        {
            for (int i = 0; i < 4000 && pr.state.load (std::memory_order_acquire) == PWorking; ++i) std::this_thread::sleep_for (std::chrono::microseconds (500));
            releasePrime (pr);
        }
    }
}

bool Player::waitPrimesIdle (int timeoutMs) const
{
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    for (;;)
    {
        bool busy = false;
        for (auto& pr : primes) { const int s = pr.state.load (std::memory_order_acquire); busy = busy || s == PRequested || s == PWorking; }
        if (! busy) return true;
        if (juce::Time::getMillisecondCounterHiRes() - t0 > timeoutMs) return false;
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
}

void Player::wantPrime (const PlayTable& t, int pi, int64 u) noexcept
{
    const Piece& pc = t.plan.pieces[(size_t) pi];
    if (desiredKind (t, pi) != Live) return;
    for (auto& pr : primes)
        if (pr.state.load (std::memory_order_acquire) != PFree && ! pr.cancel.load (std::memory_order_acquire) && pr.key == pc.liveKey && pr.u == u)
        { pr.wanted = true; return; }
    Prime* f = nullptr;
    for (auto& pr : primes) if (pr.state.load (std::memory_order_acquire) == PFree) { f = &pr; break; }
    if (f == nullptr) return;
    int freeSt = 0; for (bool b : stBusy) freeSt += b ? 0 : 1;
    if (freeSt <= 2) return;                                   // (keep two for starts primed in the block)
    const int s = allocStretcher(); if (s < 0) return;
    f->st = s; f->pi = pi; f->key = pc.liveKey; f->u = u; f->pre = pc.hold != nullptr ? hold::elasticPreroll (*pc.hold, u) : 0; f->table = &t;
    f->wanted = true; f->cancel.store (false, std::memory_order_relaxed);
    f->state.store (PRequested, std::memory_order_release);
}

void Player::primeAhead (const PlayTable* t, int64 T, int n, bool playing) noexcept
{
    for (auto& pr : primes) pr.wanted = false;
    if (t != nullptr && t->work != nullptr && ! t->plan.pieces.empty())
    {
        const auto& P = t->plan.pieces;
        const int64 W = (int64) std::llround (kPrimeAheadS * sr);
        auto scan = [&] (int64 from, int64 to, bool covering)
        {   // the pieces that start in [from, to) (covering: also the one playing at `from`, started there mid-piece)
            auto it = std::upper_bound (P.begin(), P.end(), from, [] (int64 x, const Piece& p) { return x < p.dst1; });
            for (; it != P.end() && it->dst0 < to; ++it)
            {
                if (it->dst0 < from && ! covering) continue;
                wantPrime (*t, (int) (it - P.begin()), it->dst0 >= from ? it->u0 : from - it->dst0 + it->u0);
            }
        };
        if (playing) scan (T + n, T + n + W, false);
        if (jumpHint >= 0) scan (jumpHint, jumpHint + W, true);
    }
    jumpHint = -1;
    for (auto& pr : primes) if (pr.state.load (std::memory_order_acquire) != PFree && ! pr.wanted) releasePrime (pr);
}

void Player::prepare (double sampleRate, int block)
{
    dropPrimes (true);
    sr = sampleRate; maxBlock = juce::jmax (16, block);
    for (auto& s : stretchers) s->configure (StretchStream::Preset::Live, sr, kChunk);
    for (int c = 0; c < 2; ++c) { tmpA[c].assign ((size_t) kChunk, 0.0f); tmpB[c].assign ((size_t) kChunk, 0.0f); }
    for (auto& v : voices) v = Voice();
    for (auto& b : stBusy) b = false;
    gainCoef = (float) (1.0 - std::exp (-1.0 / (0.010 * sr)));   // 10 ms gain smoothing (per sample)
}

void Player::publish (std::shared_ptr<PlayTable> t)
{
    t->gen = ++genCounter;
    keep.push_back (t);
    current.store (t.get(), std::memory_order_release);
    collectGarbage();
}

void Player::collectGarbage()
{
    const uint64 inUse = minGenInUse.load (std::memory_order_acquire);
    // keep the newest table always; drop older ones the audio thread can no longer reach
    for (auto it = keep.begin(); it != keep.end() && keep.size() > 1;)
        if ((*it)->gen < inUse && it->get() != current.load()) it = keep.erase (it); else ++it;
    // no audio callback for a second (a suspended host): drop the voices, then every old table can go
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto rc = renderCount.load();
    if (rc != gcSeenCount) { gcSeenCount = rc; gcSeenMs = now; }
    else if (keep.size() > 4 && now - gcSeenMs > 1000.0
             && std::none_of (std::begin (primes), std::end (primes), [] (const Prime& pr) { const int s = pr.state.load(); return s == PRequested || s == PWorking; }))
    {
        dropVoices.store (true);                          // seq_cst: the audio thread sees it, or we see it rendering
        if (! inRender.load())
        {
            auto* cur = current.load();
            keep.erase (std::remove_if (keep.begin(), keep.end(), [cur] (const std::shared_ptr<PlayTable>& t) { return t.get() != cur; }), keep.end());
        }
    }
}

int Player::allocStretcher() noexcept
{
    for (int i = 0; i < kStretchers; ++i) if (! stBusy[i]) { stBusy[i] = true; return i; }
    return -1;
}

int Player::desiredKind (const PlayTable& t, int pi) const noexcept
{
    const auto& pc = t.plan.pieces[(size_t) pi];
    if (const auto* H = pc.hold.get(); H != nullptr && H->mode != HoldMode::Elastic && ! H->grains.empty())
    {   // HOLD: SUSTAIN plays its grains (exact); TEXTURE / FREEZE preview with them until their STUDIO render is there
        if (H->mode == HoldMode::Sustain) return Grain;
        if (t.useStudio && pi < (int) t.studio.size() && t.studio[(size_t) pi] != nullptr) return Studio;
        return Grain;
    }
    if (! pc.stretch) return Direct;
    if (t.useStudio && pi < (int) t.studio.size() && t.studio[(size_t) pi] != nullptr) return Studio;
    return Live;
}

bool Player::startPath (Path& p, int kind, const PlayTable& t, const Piece& pc, int64 u) noexcept
{
    p = Path(); p.u = u;
    if (kind == Direct) { p.kind = Direct; return true; }
    if (kind == Studio) { p.kind = Studio; return true; }
    if (kind == Grain) { p.kind = Grain; return pc.hold != nullptr; }
    if (kind == Live)
    {
        for (auto& pr : primes)
            if (pr.key == pc.liveKey && pr.u == u && pr.state.load (std::memory_order_acquire) != PFree && ! pr.cancel.load (std::memory_order_acquire))
            {
                if (pr.state.load (std::memory_order_acquire) == PReady && pr.table != nullptr && pr.table->work.get() == t.work.get())
                {   // primed in the background: adopt it (the stream is at u, settled exactly as below)
                    p.kind = Live; p.st = pr.st; pr.st = -1; pr.table = nullptr; pr.state.store (PFree, std::memory_order_release);
                    stretchers[p.st]->rebind (pc.map, pc.curve.get());
                    primesAdopted.fetch_add (1, std::memory_order_relaxed);
                    return true;
                }
                releasePrime (pr);   // not ready in time: prime in the block
                break;
            }
        primesMissed.fetch_add (1, std::memory_order_relaxed);
        const int s = allocStretcher(); if (s < 0) return false;
        const int64 pre = pc.hold != nullptr ? hold::elasticPreroll (*pc.hold, u) : 0;   // (a HOLD's in join: settle at 1x on the head first)
        stretchers[s]->start (pc.map, PieceReader { t.work.get(), pc.reverse, pc.mirror }, u - pre, t.plan.pitchSemis + pc.pitchOffset, t.plan.formantSemis + pc.formantOffset, pc.tonalityHz, pc.formantBaseHz, pc.curve.get());
        stretchers[s]->skip (pre);
        p.kind = Live; p.st = s; return true;
    }
    return false;
}

void Player::renderPath (Path& p, const PlayTable& t, const Piece& pc, float* L, float* R, int n) noexcept
{
    switch (p.kind)
    {
        case Direct: render::directInto (pc, *t.work, p.u, L, R, n); break;
        case Studio:
        {
            const int pi = (int) (&pc - t.plan.pieces.data());
            const StudioBuffer* b = pi >= 0 && pi < (int) t.studio.size() ? t.studio[(size_t) pi].get() : nullptr;
            for (int i = 0; i < n; ++i)
            {
                const int64 k = p.u + i - pc.u0;
                const bool ok = b != nullptr && k >= 0 && k < (int64) b->audio.getNumSamples();
                L[i] = ok ? b->audio.getSample (0, (int) k) : 0.0f; R[i] = ok ? b->audio.getSample (1, (int) k) : 0.0f;
            }
            break;
        }
        case Live:
            stretchers[p.st]->setPitch (t.plan.pitchSemis + pc.pitchOffset, t.plan.formantSemis + pc.formantOffset, pc.tonalityHz, pc.formantBaseHz);
            stretchers[p.st]->render (L, R, n);
            render::spliceTransients (pc, *t.work, p.u, L, R, n);
            if (pc.hold != nullptr) hold::applyJoins (*pc.hold, *t.work, p.u - pc.hold->uStart, L, R, n);
            break;
        case Grain:
            hold::renderGrains (*pc.hold, *t.work, p.u - pc.hold->uStart, L, R, n);
            break;
        default: std::fill (L, L + n, 0.0f); std::fill (R, R + n, 0.0f); break;
    }
    p.u += n;
}

Player::Voice* Player::findVoice (uint64 key) noexcept { for (auto& v : voices) if (v.on && ! v.releasing && v.key == key) return &v; return nullptr; }
Player::Voice* Player::freeVoice() noexcept { for (auto& v : voices) if (! v.on) return &v; return nullptr; }

void Player::renderVoice (Voice& v, const PlayTable& t, float* L, float* R, int off, int n, bool releaseOnly) noexcept
{
    const Piece& pc = v.table->plan.pieces[(size_t) v.pi];
    if (! releaseOnly && v.xfLen == 0)
    {
        const int want = desiredKind (t, v.pi);
        if (want != v.cur.kind)
        {
            const auto* sb = want == Studio ? t.studio[(size_t) v.pi].get() : nullptr;
            if (v.cur.kind == Live && want == Studio && sb != nullptr && (sb->isHot (v.cur.u - pc.u0) || sb->isHot (v.cur.u - pc.u0 + (int64) (xfadeMs * 0.001 * sr))))
                switchesDeferred.fetch_add (1, std::memory_order_relaxed);   // never inside a transient: try again next block
            else
            {
                Path np;
                if (startPath (np, want, t, pc, v.cur.u))
                {
                    v.old = v.cur; v.cur = np; v.xfPos = 0; v.xfLen = juce::jmax (1, (int) std::lround (xfadeMs * 0.001 * sr));
                    switches.fetch_add (1, std::memory_order_relaxed);
                }
            }
        }
    }
    int done = 0;
    while (done < n)
    {
        const int m = juce::jmin (kChunk, n - done);
        const int64 u = v.cur.u;
        renderPath (v.cur, t, pc, tmpA[0].data(), tmpA[1].data(), m);
        const bool xf = v.xfLen > 0;
        if (xf) renderPath (v.old, t, pc, tmpB[0].data(), tmpB[1].data(), m);
        float* oL = L + off + done; float* oR = R + off + done;
        for (int i = 0; i < m; ++i)
        {
            float a = tmpA[0][(size_t) i], b = tmpA[1][(size_t) i];
            if (v.xfLen > 0)
            {
                const double x = (double) (v.xfPos + 1) / (double) v.xfLen;
                const float gn = (float) std::sin (x * juce::MathConstants<double>::halfPi), go = (float) std::cos (x * juce::MathConstants<double>::halfPi);
                const bool fromSilence = v.old.kind == None;
                a = a * gn + (fromSilence ? 0.0f : tmpB[0][(size_t) i] * go);
                b = b * gn + (fromSilence ? 0.0f : tmpB[1][(size_t) i] * go);
                if (++v.xfPos >= v.xfLen) { v.xfLen = 0; freePath (v.old); }
            }
            float g = v.gNow;
            if (v.gNow != pc.gain) { v.gNow += (pc.gain - v.gNow) * gainCoef; if (std::abs (v.gNow - pc.gain) < 1.0e-6f) v.gNow = pc.gain; g = v.gNow; }
            float e = pc.shape (u + i) * g;
            if (v.releasing) { e *= v.relLen > 0 ? 1.0f - (float) juce::jmin (v.relPos, v.relLen) / (float) v.relLen : 0.0f; ++v.relPos; }
            oL[i] += a * e; oR[i] += b * e;
        }
        done += m;
        if (v.releasing && v.relPos >= v.relLen) break;
    }
    v.nextU = v.cur.u;
}

void Player::render (float* L, float* R, int n, int64 T, bool playing) noexcept
{
    inRender.store (true);
    if (dropVoices.exchange (false))
    {
        for (auto& v : voices) { if (v.on) { freePath (v.cur); freePath (v.old); } v = Voice(); }
        dropPrimes (false);
    }
    PlayTable* t = current.load (std::memory_order_acquire);
    if (t != nullptr && std::abs (t->plan.hostRate - sr) > 1.0e-6) t = nullptr;   // (a rate change: the message thread recompiles)
    bool touched[kVoices] {};
    int nLive = 0, nStudio = 0, nVoices = 0, nGrain = 0;
    if (playing && t != nullptr && t->work != nullptr && ! t->plan.pieces.empty())
    {
        auto& P = t->plan.pieces;
        auto it = std::upper_bound (P.begin(), P.end(), T, [] (int64 x, const Piece& p) { return x < p.dst1; });
        for (; it != P.end() && it->dst0 < T + n; ++it)
        {
            const int pi = (int) (it - P.begin()); const Piece& pc = *it;
            const int64 a = std::max (T, pc.dst0), b = std::min (T + n, pc.dst1);
            if (b <= a) continue;
            const int64 u = a - pc.dst0 + pc.u0;
            Voice* v = findVoice (pc.liveKey);
            if (v != nullptr && v->nextU != u) { v->releasing = true; v->relPos = 0; v->relLen = (int) std::lround (releaseMs * 0.001 * sr); v = nullptr; }   // relocated
            if (v == nullptr)
            {
                v = freeVoice();
                if (v == nullptr) { voicesStarved.fetch_add (1, std::memory_order_relaxed); continue; }
                *v = Voice(); v->on = true; v->key = pc.liveKey; v->table = t; v->pi = pi; v->gNow = pc.gain; v->nextU = u;
                if (! startPath (v->cur, desiredKind (*t, pi), *t, pc, u) && ! startPath (v->cur, Direct, *t, pc, u)) { v->on = false; continue; }
                if (u != pc.u0)   // starting in the middle (play from here / loop wrap / an edit): a short fade-in from silence
                { v->old = Path(); v->xfPos = 0; v->xfLen = juce::jmax (1, (int) std::lround (releaseMs * 0.001 * sr)); }
            }
            else
            {   // same stream in a newer table: keep playing (the stretchers point at the newer table's map / curve from now on)
                v->table = t; v->pi = pi;
                for (auto* path : { &v->cur, &v->old }) if (path->kind == Live && path->st >= 0) stretchers[path->st]->rebind (pc.map, pc.curve.get());
            }
            touched[v - voices] = true;
            renderVoice (*v, *t, L, R, (int) (a - T), (int) (b - a), false);
            if (v->nextU >= pc.u0 + pc.length()) { freePath (v->cur); freePath (v->old); v->on = false; }
        }
    }
    for (int i = 0; i < kVoices; ++i)
    {
        auto& v = voices[i];
        if (! v.on || touched[i]) continue;
        if (! v.releasing) { v.releasing = true; v.relPos = 0; v.relLen = (int) std::lround (releaseMs * 0.001 * sr); }
        renderVoice (v, *v.table, L, R, 0, n, true);
        if (v.relPos >= v.relLen) { freePath (v.cur); freePath (v.old); v.on = false; }
    }
    primeAhead (t, T, n, playing);
    PlayTable* cur = current.load (std::memory_order_acquire);
    uint64 minGen = t != nullptr ? t->gen : (cur != nullptr ? cur->gen : 0);
    for (auto& pr : primes) if (pr.state.load (std::memory_order_acquire) != PFree && pr.table != nullptr) minGen = std::min (minGen, pr.table->gen);
    for (auto& v : voices)
        if (v.on)
        {
            ++nVoices; if (v.table != nullptr) minGen = std::min (minGen, v.table->gen);
            if (v.cur.kind == Live) ++nLive; if (v.cur.kind == Studio) ++nStudio; if (v.cur.kind == Grain) ++nGrain;
        }
    minGenInUse.store (minGen, std::memory_order_release);
    activeVoices.store (nVoices, std::memory_order_relaxed); liveStretchers.store (nLive, std::memory_order_relaxed); studioVoices.store (nStudio, std::memory_order_relaxed);
    grainVoices.store (nGrain, std::memory_order_relaxed);
    lastPos.store (T + n, std::memory_order_relaxed);
    renderCount.fetch_add (1, std::memory_order_relaxed);
    inRender.store (false);
}
} // namespace gslice
