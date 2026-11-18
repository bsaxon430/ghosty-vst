#include "ArcsEngine.h"
#include <thread>

namespace arcs {

// ---------------------------------------------------------------- wavetables
std::shared_ptr<const Wavetable> buildWavetable(const std::array<float, kWaveLen>& wave) {
    auto wt = std::make_shared<Wavetable>();
    // the drawn cycle is a closed polyline: resample it to the table length (linear), then keep its first 512 harmonics (DC dropped)
    juce::dsp::FFT fft(12); std::vector<float> spec((size_t) kTableLen * 2, 0.0f), buf((size_t) kTableLen * 2);
    for (int i = 0; i < kTableLen; ++i) {
        const float f = (float) i * kWaveLen / kTableLen; const int a = (int) f; const float r = f - (float) a;
        const float ya = wave[(size_t) a], yb = wave[(size_t) ((a + 1) % kWaveLen)];
        spec[(size_t) i] = (std::isfinite(ya) ? ya : 0.0f) * (1.0f - r) + (std::isfinite(yb) ? yb : 0.0f) * r;
    }
    fft.performRealOnlyForwardTransform(spec.data(), false);
    float norm = 0;
    for (int L = 0; L < kMipLevels; ++L) {
        const int H = Wavetable::harmonics(L);
        std::fill(buf.begin(), buf.end(), 0.0f);
        for (size_t k = 1; k <= (size_t) H; ++k) { const size_t kk = (size_t) kTableLen - k; const float re = spec[2 * k], im = spec[2 * k + 1];
            buf[2 * k] = re; buf[2 * k + 1] = im; buf[2 * kk] = re; buf[2 * kk + 1] = -im; }
        fft.performRealOnlyInverseTransform(buf.data());
        if (L == 0) { float peak = 0; for (int i = 0; i < kTableLen; ++i) peak = juce::jmax(peak, std::abs(buf[(size_t) i])); norm = peak > 1.0e-6f ? 1.0f / peak : 0.0f; }
        auto& t = wt->t[(size_t) L]; float sl = 0;
        for (int i = 0; i < kTableLen; ++i) t[(size_t) i + 1] = std::isfinite(buf[(size_t) i]) ? buf[(size_t) i] * norm : 0.0f;
        t[0] = t[(size_t) kTableLen]; t[(size_t) kTableLen + 1] = t[1]; t[(size_t) kTableLen + 2] = t[2]; t[(size_t) kTableLen + 3] = t[3];
        for (int i = 1; i <= kTableLen; ++i) sl = juce::jmax(sl, std::abs(t[(size_t) i + 1] - t[(size_t) i]));
        wt->slope[(size_t) L] = sl * (float) kTableLen;
    }
    return wt;
}

juce::uint64 SnapCache::hashOf(const std::array<float, kWaveLen>& w) {
    juce::uint64 h = 1469598103934665603ull; auto* b = reinterpret_cast<const juce::uint8*>(w.data());
    for (size_t i = 0; i < sizeof(float) * kWaveLen; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    return h;
}
std::shared_ptr<const Wavetable> SnapCache::table(const Arc& a) {
    juce::uint64 key;
    auto k = keyOf.find(a.id);
    if (k != keyOf.end() && k->second.first == a.waveRev) key = k->second.second;
    else { key = hashOf(a.wave); keyOf[a.id] = { a.waveRev, key }; }
    auto it = tableMap.find(key);
    if (it != tableMap.end()) return it->second;
    auto wt = buildWavetable(a.wave); tableMap[key] = wt; return wt;
}
void SnapCache::prune(const Page& p) {
    std::vector<juce::uint64> used;
    for (auto it = keyOf.begin(); it != keyOf.end();) { if (p.find(it->first) != nullptr) { used.push_back(it->second.second); ++it; } else it = keyOf.erase(it); }
    for (auto it = tableMap.begin(); it != tableMap.end();) { if (std::find(used.begin(), used.end(), it->first) != used.end()) ++it; else it = tableMap.erase(it); }
    for (auto it = built.begin(); it != built.end();) { if (p.find(it->first) != nullptr) ++it; else it = built.erase(it); }
}

// ---------------------------------------------------------------- snapshot
float SnapArc::pitchAt(float x, int& seg) const {
    const int n = (int) pts.size(); if (n == 0) return 69.0f; if (n == 1 || x <= pts[0].x) return pts[0].y; if (x >= pts[(size_t) n - 1].x) return pts[(size_t) n - 1].y;
    seg = juce::jlimit(0, n - 2, seg);
    // the playhead moves monotonically inside a block: walk a few segments, else binary search
    int steps = 0;
    while (seg < n - 2 && x > pts[(size_t) seg + 1].x && steps < 8) { ++seg; ++steps; }
    while (seg > 0 && x < pts[(size_t) seg].x && steps < 8) { --seg; ++steps; }
    if (x < pts[(size_t) seg].x || x > pts[(size_t) seg + 1].x) {
        auto it = std::upper_bound(pts.begin(), pts.end(), x, [](float v, const Pt& p) { return v < p.x; });
        seg = juce::jlimit(0, n - 2, (int) (it - pts.begin()) - 1);
    }
    const Pt& a = pts[(size_t) seg]; const Pt& b = pts[(size_t) seg + 1]; const float w = b.x - a.x;
    return w <= 1.0e-12f ? b.y : a.y + (b.y - a.y) * (x - a.x) / w;
}
float Snapshot::timeAtPos(float p) const {
    if (timeAt.empty()) return juce::jlimit(0.0f, 1.0f, finiteOr(p, 0.0f));
    const double f = juce::jlimit(0.0, (double) kWarp, (double) finiteOr(p, 0.0f) * kWarp); const int i = juce::jmin(kWarp - 1, (int) f); const double r = juce::jmin(1.0, f - (double) i);   // integer clamp
    return (float) (timeAt[(size_t) i] + (timeAt[(size_t) i + 1] - timeAt[(size_t) i]) * r);
}
float Snapshot::posAtTime(float t) const {
    if (posAt.empty()) return juce::jlimit(0.0f, 1.0f, finiteOr(t, 0.0f));
    const double f = juce::jlimit(0.0, (double) kWarp, (double) finiteOr(t, 0.0f) / total * kWarp); const int i = juce::jmin(kWarp - 1, (int) f); const double r = juce::jmin(1.0, f - (double) i);
    return (float) (posAt[(size_t) i] + (posAt[(size_t) i + 1] - posAt[(size_t) i]) * r);
}

static juce::uint64 arcKey(const Arc& a) {
    juce::uint64 h = 1469598103934665603ull;
    auto mixBytes = [&](const void* d, size_t n) { auto* b = static_cast<const juce::uint8*>(d); for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; } };
    auto mixPts = [&](const std::vector<Pt>& v) { const size_t n = v.size(); mixBytes(&n, sizeof(n)); if (n) mixBytes(v.data(), n * sizeof(Pt)); };
    mixBytes(&a.id, sizeof(a.id)); mixPts(a.pts); mixPts(a.env); mixPts(a.pan); mixPts(a.cutoff); mixBytes(&a.waveRev, sizeof(a.waveRev));
    const float f[] = { a.gain, a.fmDepth, a.detune, a.spread, a.sub, a.drive, a.res, a.rate }; mixBytes(f, sizeof(f));
    const int i[] = { a.fmSource, a.mute, a.modOnly, a.source, a.unison, a.filter, a.track, a.repeat }; mixBytes(i, sizeof(i));
    return h;
}

std::unique_ptr<Snapshot> buildSnapshot(const Page& p, SnapCache& cache, double* ms, int* rebuilt) {
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    auto s = std::make_unique<Snapshot>();
    s->lengthBeats = juce::jlimit(kMinBeats, kMaxBeats, finiteOr((float) p.lengthBeats, 16.0f) * 1.0); s->lengthSeconds = juce::jlimit(kMinSeconds, kMaxSeconds, std::isfinite(p.lengthSeconds) ? p.lengthSeconds : 8.0);
    s->tempoSync = p.tempoSync; s->ownBpm = juce::jlimit(20.0, 999.0, std::isfinite(p.ownBpm) ? p.ownBpm : 120.0);
    s->loopOn = p.loopOn; s->loopA = juce::jlimit(0.0f, 1.0f, finiteOr(p.loopA, 0.0f)); s->loopB = juce::jlimit(0.0f, 1.0f, finiteOr(p.loopB, 1.0f)); if (s->loopB - s->loopA < 0.001f) { s->loopA = 0; s->loopB = 1; }
    s->reverse = p.reverse; s->master = juce::jlimit(0.0f, 1.0f, finiteOr(p.master, 0.5f)); s->autoGain = p.autoGain; s->midiIn = p.midiIn;
    s->quantise = p.quantise; for (int k = 0; k < 12; ++k) s->keyMask[(size_t) k] = inScale(k, p.root, p.scale) ? 1 : 0;
    // the speed lane -> warp tables (double: a long page must not drift); no lane = exact identity, no tables
    if (p.speedOn) {
        const int N = Snapshot::kWarp; s->timeAt.assign((size_t) N + 1, 0.0);
        for (int i = 0; i < N; ++i) { const double sp = juce::jlimit(0.25, 4.0, (double) finiteOr(p.speedAt((i + 0.5f) / N), 1.0f)); s->timeAt[(size_t) i + 1] = s->timeAt[(size_t) i] + (1.0 / N) / sp; }
        s->total = s->timeAt[(size_t) N];
        s->posAt.assign((size_t) N + 1, 0.0); int i = 0;
        for (int j = 0; j <= N; ++j) { const double t = j / (double) N * s->total; while (i < N - 1 && s->timeAt[(size_t) i + 1] < t) ++i;
            const double a = s->timeAt[(size_t) i], b = s->timeAt[(size_t) i + 1]; const double r = b > a ? juce::jlimit(0.0, 1.0, (t - a) / (b - a)) : 0.0; s->posAt[(size_t) j] = ((double) i + r) / N; }
        s->posAt[(size_t) N] = 1.0;
    }
    // wavetables that are missing are built in parallel (a loaded page with many drawn cycles)
    {
        std::vector<const Arc*> need; std::vector<juce::uint64> needKeys;
        for (const auto& a : p.arcs) { if (!a.valid()) continue; juce::uint64 key; auto k = cache.keyOf.find(a.id);
            if (k != cache.keyOf.end() && k->second.first == a.waveRev) key = k->second.second; else { key = SnapCache::hashOf(a.wave); cache.keyOf[a.id] = { a.waveRev, key }; }
            if (cache.tableMap.find(key) == cache.tableMap.end() && std::find(needKeys.begin(), needKeys.end(), key) == needKeys.end()) { need.push_back(&a); needKeys.push_back(key); } }
        if (need.size() >= 4) {
            std::vector<std::shared_ptr<const Wavetable>> out(need.size());
            const int nt = juce::jlimit(1, 4, (int) std::thread::hardware_concurrency()); std::vector<std::thread> th;
            for (int w = 0; w < nt; ++w) th.emplace_back([&, w] { for (size_t k = (size_t) w; k < need.size(); k += (size_t) nt) out[k] = buildWavetable(need[k]->wave); });
            for (auto& t : th) t.join();
            for (size_t k = 0; k < need.size(); ++k) cache.tableMap[needKeys[k]] = out[k];
        }
    }
    // lines: reuse the built line while nothing about it changed
    int nRebuilt = 0; std::vector<int> fmIds;
    for (const auto& a : p.arcs) {
        if (!a.valid() || (int) s->arcs.size() >= kMaxArcs) continue;
        const juce::uint64 key = arcKey(a);
        auto it = cache.built.find(a.id);
        SnapArcPtr sp;
        if (it != cache.built.end() && it->second->key == key) sp = it->second;
        else {
            auto sa = std::make_shared<SnapArc>(); ++nRebuilt;
            sa->id = a.id; sa->key = key; sa->x0 = a.x0(); sa->x1 = a.x1(); sa->span = juce::jmax(1.0e-6f, a.span());
            sa->pts = a.pts; for (auto& q : sa->pts) { q.y = juce::jlimit(kMinMidi, kMaxMidi, finiteOr(q.y, 69.0f)); }
            sa->gain = a.gain; sa->fmDepth = a.fmSource >= 0 ? a.fmDepth : 0.0f; sa->fmSourceId = a.fmSource; sa->audible = !a.mute && !a.modOnly;
            sa->source = a.source; sa->unison = a.unison; sa->detune = a.detune; sa->spread = a.spread; sa->sub = a.sub; sa->drive = a.drive;
            sa->filter = a.filter; sa->res = a.res; sa->track = a.track; sa->rate = a.rate; sa->repeat = a.repeat;
            sa->env.resize((size_t) kEnvRes + 1); sa->pan.resize((size_t) kEnvRes + 1); sa->cut.resize((size_t) kEnvRes + 1);
            for (int k = 0; k <= kEnvRes; ++k) { const float u = (float) k / kEnvRes; sa->env[(size_t) k] = finiteOr(a.envAt(u), 0.0f); sa->pan[(size_t) k] = finiteOr(a.panAt(u), 0.0f); sa->cut[(size_t) k] = finiteOr(a.cutoffAt(u), 0.7f); }
            sa->wt = cache.table(a);
            sp = sa; cache.built[a.id] = sp;
        }
        fmIds.push_back(a.fmSource); s->arcs.push_back(sp);
        s->durUnits.push_back(juce::jmax(1.0e-6f, (s->timeAtPos(sp->x1) - s->timeAtPos(sp->x0)) / sp->rate));
    }
    const size_t na = s->arcs.size(); s->fmIdx.assign(na, -1); s->os.assign(na, 0);
    for (size_t k = 0; k < na; ++k) if (fmIds[k] >= 0 && s->arcs[k]->fmDepth > 0) for (size_t j = 0; j < na; ++j) if (s->arcs[j]->id == fmIds[k] && j != k) { s->fmIdx[k] = (int) j; s->os[k] = 1; s->os[j] = 1; break; }
    for (size_t k = 0; k < na; ++k) if (s->arcs[k]->drive > 0.001f) s->os[k] = 1;
    // a source that is itself a carrier of an oversampled line keeps the whole chain in the group
    for (int pass = 0; pass < 4; ++pass) for (size_t k = 0; k < na; ++k) if (s->os[k] && s->fmIdx[k] >= 0) s->os[(size_t) s->fmIdx[k]] = 1;
    for (char c : s->os) s->anyOs = s->anyOs || c;
    if (ms) *ms = juce::Time::getMillisecondCounterHiRes() - t0;
    if (rebuilt) *rebuilt = nRebuilt;
    return s;
}

// ---------------------------------------------------------------- engine
Engine::Engine() { for (auto& v : voices) v = Voice(); slotOf.fill(-1); prepare(48000.0, 512); }   // usable before the host prepares it

void Engine::prepare(double sampleRate, int) {
    sr = juce::jlimit(8000.0, 768000.0, std::isfinite(sampleRate) ? sampleRate : 48000.0);
    for (int m = 0; m <= kCtl; ++m) {
        kH[(size_t) m] = 1.0f - std::exp(-(float) m / (0.002f * (float) sr)); kG[(size_t) m] = 1.0f - std::exp(-(float) m / (0.003f * (float) sr)); kP[(size_t) m] = 1.0f - std::exp(-(float) m / (0.005f * (float) sr));
    }
    // oversampled group: 4x up to 50 kHz, 2x up to 100 kHz, else 1x; decimator latency is always kLatency base samples
    os = sr <= 50000.0 ? 4 : (sr <= 100000.0 ? 2 : 1);
    firN = 2 * kLatency * os + 1; fir.assign((size_t) firN, 0.0f);
    if (os == 1) fir[(size_t) kLatency] = 1.0f;
    else {
        const double fc = 0.46 / os, beta = 9.5; double sum = 0; const int c = (firN - 1) / 2;
        auto bessel0 = [](double x) { double s = 1, t = 1; for (int k = 1; k < 40; ++k) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; } return s; };
        for (int k = 0; k < firN; ++k) { const double m = k - c; const double sinc = std::abs(m) < 0.5 ? 2 * fc : std::sin(2 * juce::MathConstants<double>::pi * fc * m) / (juce::MathConstants<double>::pi * m);
            const double r = (double) m / c; const double w = bessel0(beta * std::sqrt(juce::jmax(0.0, 1 - r * r))) / bessel0(beta); fir[(size_t) k] = (float) (sinc * w); sum += sinc * w; }
        for (auto& h : fir) h = (float) (h / sum);
    }
    osL.assign((size_t) (firN - 1 + kCtl * os), 0.0f); osR = osL; osQuiet = 1 << 30;
    for (auto& d : dly) { d.assign(128, 0.0f); } dlyPos = 0;
    for (auto& v : voices) { v.gainS = 0; v.lastOut = 0; v.gm = v.gl = v.gr = 0; v.dGm = v.dGl = v.dGr = 0; v.s1L = v.s2L = v.s1R = v.s2R = 0; v.fresh = true; }
    jumped = true;
}

void Engine::remap(const Snapshot* s, bool slotChanged) {
    for (auto& v : voices) v.arc = -1;
    slotOf.fill(-1);
    if (slotChanged) jumped = true;
    if (!s) { for (auto& v : voices) { v.sa = nullptr; v.gm = v.gl = v.gr = v.gainS = 0; } return; }
    const int na = juce::jmin(kMaxArcs, (int) s->arcs.size());
    // pass 1: lines that already have a voice keep it (phase and smoothing carry on through edits)
    for (int i = 0; i < na; ++i) {
        const int id = s->arcs[(size_t) i]->id;
        for (int k = 0; k < kMaxArcs; ++k) if (voices[(size_t) k].id == id && voices[(size_t) k].arc < 0) { auto& v = voices[(size_t) k]; v.arc = i; slotOf[(size_t) i] = k;
            if (v.os != (s->os[(size_t) i] != 0)) { v.s1L = v.s2L = v.s1R = v.s2R = 0; }   // moved between the plain and the oversampled path
            v.os = s->os[(size_t) i] != 0; v.sa = s->arcs[(size_t) i].get(); break; }
    }
    // pass 2: new lines take a never-used voice, else the quietest orphan (only at the 256-line limit: that orphan is cut)
    for (int i = 0; i < na; ++i) {
        if (slotOf[(size_t) i] >= 0) continue;
        int slot2 = -1; float best = 1.0e9f;
        for (int k = 0; k < kMaxArcs; ++k) { const auto& v = voices[(size_t) k]; if (v.arc >= 0) continue; const float score = (v.id < 0 || v.sa == nullptr) ? -1.0f : v.gm; if (score < best) { best = score; slot2 = k; } }
        if (slot2 < 0) continue;
        auto& v = voices[(size_t) slot2]; v = Voice(); v.id = s->arcs[(size_t) i]->id; v.os = s->os[(size_t) i] != 0; v.sa = s->arcs[(size_t) i].get();
        v.rng = 0x9e3779b9u ^ ((juce::uint32) v.id * 2654435761u); v.arc = i; slotOf[(size_t) i] = slot2;
    }
    // orphans keep their raw SnapArc pointer: the retired snapshot that owns it lives for a grace second on the message thread
}

static inline float hermite(const float* t, float ph) {
    const float f = ph * kTableLen; const int i = juce::jlimit(0, kTableLen - 1, (int) f); const float r = f - (float) i;
    const float y0 = t[i], y1 = t[i + 1], y2 = t[i + 2], y3 = t[i + 3];
    const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * r + c2) * r + c1) * r + y1;
}
static inline float lerpTable(const std::vector<float>& t, float u) {
    const int last = (int) t.size() - 2; const float f = juce::jlimit(0.0f, (float) (last + 1), finiteOr(u, 0.0f) * (float) (last + 1)); const int i = juce::jmin(last, (int) f); const float r = juce::jmin(1.0f, f - (float) i);
    return t[(size_t) i] + (t[(size_t) i + 1] - t[(size_t) i]) * r;
}

// one sample of one voice (its own rate: plain or oversampled); adds into l / r and sets lastOut
inline void Engine::voiceSample(Voice& v, const SnapArc& a, float fmOff, float& l, float& r) {
    float sL = 0, sR = 0;
    if (a.source == SrcNoise) { v.rng ^= v.rng << 13; v.rng ^= v.rng >> 17; v.rng ^= v.rng << 5; const float n = ((float) (v.rng >> 8) * (1.0f / 8388608.0f) - 1.0f) * 0.9f; sL = sR = n; }
    else {
        const float* t0 = a.wt->t[(size_t) v.L0].data(); const float* t1 = a.wt->t[(size_t) v.L1].data(); const bool mixOn = v.mix > 0.0005f;
        for (int k = 0; k < a.unison; ++k) {
            float ph = (float) v.ph[(size_t) k] + fmOff; ph -= std::floor(ph);
            const float y0 = hermite(t0, ph); const float y = mixOn ? y0 + (hermite(t1, ph) - y0) * v.mix : y0;
            v.ph[(size_t) k] += v.inc * v.ratio[(size_t) k]; if (v.ph[(size_t) k] >= 1.0) v.ph[(size_t) k] -= 1.0;
            sL += y * v.uL[(size_t) k]; sR += y * v.uR[(size_t) k];
        }
    }
    if (a.sub > 0.0f) { const float y = std::sin((float) v.subPh * juce::MathConstants<float>::twoPi) * a.sub * 0.8f; v.subPh += 0.5 * v.inc; if (v.subPh >= 1.0) v.subPh -= 1.0; sL += y; sR += y; }
    // drive: tanh is analytic, so its harmonics fall off exponentially (gain capped at 6) and stay inside the oversampled band
    if (a.drive > 0.001f) { const float k = 1.0f + a.drive * 5.0f, mk = 0.8f / std::tanh(k * 0.8f); sL = std::tanh(sL * k) * mk; sR = std::tanh(sR * k) * mk; }
    if (a.filter != FiltOff) {
        auto svf = [&](float x, float& s1, float& s2) { const float v3 = x - s2, v1 = v.a1 * s1 + v.a2 * v3, v2 = s2 + v.a2 * s1 + v.a3 * v3; s1 = 2 * v1 - s1; s2 = 2 * v2 - s2;
            return a.filter == FiltLP ? v2 : (a.filter == FiltBP ? v1 * v.fk : x - v.fk * v1 - v2); };
        sL = svf(sL, v.s1L, v.s2L); sR = svf(sR, v.s1R, v.s2R);
        v.a1 += v.da1; v.a2 += v.da2; v.a3 += v.da3;
    }
    v.lastOut = 0.5f * (sL + sR) * v.gm;
    if (a.audible) { l += sL * v.gl; r += sR * v.gr; }
    v.inc += v.dInc; v.gm += v.dGm; v.gl += v.dGl; v.gr += v.dGr; v.mix += v.dMix; v.fm += v.dFm;
}

void Engine::process(float* L, float* R, int n, const HostInfo& host, bool add, const juce::MidiBuffer* midi) {
    juce::ScopedNoDenormals nd;
    if (!add) { std::fill(L, L + n, 0.0f); std::fill(R, R + n, 0.0f); }
    // slot / snapshot
    const int want = juce::jlimit(0, kSlots - 1, slotRequest.load());
    const Snapshot* s = ex[(size_t) want].get();
    if (s != cur || want != slot) { remap(s, want != slot); cur = s; slot = want; playingSlot.store(slot); }
    const double bpm = host.bpm > 1 && std::isfinite(host.bpm) ? host.bpm : (cur ? cur->ownBpm : 120.0);
    if (!cur) { playPos.store(-1.0f); isPlaying.store(false); activeVoices.store(0); return; }
    const double D = juce::jmax(0.001, cur->seconds(bpm)), total = cur->total;
    const bool midiMode = cur->midiIn;
    const bool hostDriven = !midiMode && followHost.load() && host.playing;
    if (wasHostPlaying && !host.playing && followHost.load()) internalPlay.store(false);   // the followed host stopped: so does the page
    wasHostPlaying = host.playing; hostRunning.store(hostDriven);
    const bool countIn = hostDriven && (host.ppq < 0 || (std::isfinite(host.timeSec) && host.timeSec < 0));
    const double ua = cur->loopOn ? cur->timeAtPos(cur->loopA) : 0.0, ub = cur->loopOn ? cur->timeAtPos(cur->loopB) : total;
    const double span = juce::jmax(1.0e-6, ub - ua);
    const double du = 1.0 / (D * sr);
    if (stopRequest.exchange(false)) { u = ua; jumpOffset = 0; jumped = true; }
    if (hostDriven && !countIn) {
        double target = std::numeric_limits<double>::quiet_NaN();
        if (cur->tempoSync) target = host.ppq / cur->lengthBeats;                    // beats -> page lengths: tempo independent
        else if (std::isfinite(host.timeSec)) target = host.timeSec / D;           // free time follows the host's seconds
        else {   // no seconds from the host: run on our own clock, re-anchor only when the host position jumps
            const double expected = lastPpq + lastN * lastBpm / 60.0 / sr, tol = juce::jmax(0.02, 2.0 * n * bpm / 60.0 / sr);
            if (!wasHostDriven || !(std::abs(host.ppq - expected) < tol)) target = host.ppq * 60.0 / bpm / D;
        }
        if (!wasHostDriven) jumpOffset = 0;
        if (std::isfinite(target)) { const double nu = target + jumpOffset; if (!wasHostDriven || std::abs(nu - u) > 2.0 * n * du + 1.0e-9) jumped = true; u = nu; }
        lastPpq = host.ppq; lastN = n; lastBpm = bpm;
    }
    wasHostDriven = hostDriven && !countIn;
    { const float j = jumpTo.exchange(-1.0f); if (j >= 0) { pendingJump = juce::jlimit(0.0f, 1.0f, j); pendingQ = juce::jmax(0.0f, jumpQuantBeats.load()); } }
    auto applyJump = [&] { double w = cur->timeAtPos(pendingJump); if (cur->reverse) w = ua + ub - w; if (hostDriven) jumpOffset += w - u; u = w; jumped = true; pendingJump = -1; };
    const double beatsPerUnit = cur->tempoSync ? cur->lengthBeats : D * bpm / 60.0;
    const float nyq1 = 0.49f * (float) sr, isr1 = (float) (1.0 / sr), isrO = (float) (1.0 / (sr * os));
    const float nyqFm = (float) (os > 1 ? (os - 0.5) * sr * 0.92 : 0.49 * sr);
    const int na = (int) cur->arcs.size();
    std::array<int, kMaxArcs> runP {}, runO {}; std::array<float, kMaxArcs> fadeInv;
    for (int i = 0; i < na; ++i) fadeInv[(size_t) i] = (float) (juce::jmax(1.0e-6, (double) cur->durUnits[(size_t) i] * D) / 0.004);
    auto posAt = [&](double uu) { if (oneShot) uu = juce::jmin(uu, total - 1.0e-9); double w = ua + std::fmod(uu - ua, span); if (w < ua) w += span; if (cur->reverse) w = ua + ub - w; return cur->posAtTime((float) w); };
    const double lagH = (0.002 * sr + kCtl) * du, lagG = (0.003 * sr + kCtl) * du, lagP = (0.005 * sr + kCtl) * du;
    auto levelOf = [](float hz, float nyq) { return std::log2(juce::jmax(1.0f, (float) kTopHarmonic * hz) / nyq) + 1.0f; };
    juce::MidiBufferIterator mIt, mEnd; if (midi != nullptr) { mIt = midi->cbegin(); mEnd = midi->cend(); }
    int active = 0; float p = -1.0f; bool kneeHit = false;
    for (int k0 = 0; k0 < n; k0 += kCtl) {
        const int m = juce::jmin(kCtl, n - k0); const float im = 1.0f / (float) m;
        // ---- MIDI-in (events of this sub-block; notes 0..7 pick a page slot)
        if (midi != nullptr) while (mIt != mEnd && (*mIt).samplePosition < k0 + m) {
            const auto msg = (*mIt).getMessage(); ++mIt;
            if (!midiMode) continue;
            if (msg.isNoteOn()) { const int note = msg.getNoteNumber();
                if (note < kSlots) { slotRequest.store(note); continue; }
                if (nHeld < 16) { held[(size_t) nHeld++] = note; } transpose = (float) (note - 60); u = ua; jumped = true; midiGate = true; midiNote.store(note); }
            else if (msg.isNoteOff()) { const int note = msg.getNoteNumber(); if (note < kSlots) continue;
                for (int h = 0; h < nHeld; ++h) if (held[(size_t) h] == note) { for (int q = h; q < nHeld - 1; ++q) held[(size_t) q] = held[(size_t) q + 1]; --nHeld; break; }
                if (nHeld == 0) { midiGate = false; midiNote.store(-1); } else { transpose = (float) (held[(size_t) nHeld - 1] - 60); midiNote.store(held[(size_t) nHeld - 1]); } }
            else if (msg.isAllNotesOff() || msg.isAllSoundOff()) { nHeld = 0; midiGate = false; midiNote.store(-1); }
        }
        const bool playing = midiMode ? midiGate : (hostDriven ? !countIn : internalPlay.load());
        if (!midiMode && std::abs(transpose) > 0.0f) { transpose = 0; jumped = true; }
        // ---- quantised jump: waits for the next beat / bar boundary while playing
        if (pendingJump >= 0) {
            if (!playing || pendingQ <= 0) applyJump();
            else { const double b0 = hostDriven ? host.ppq + k0 * bpm / 60.0 / sr : u * beatsPerUnit, b1 = b0 + m * bpm / 60.0 / sr * (hostDriven ? 1.0 : 1.0);
                if (std::floor(b1 / pendingQ) > std::floor(b0 / pendingQ)) applyJump(); }
        }
        pendingJumpStatus.store(pendingJump >= 0);
        { const double cyc = std::floor((u - ua) / span); if (std::abs(cyc - lastCycle) > 0.5) { if (cur->loopOn || cur->reverse) jumped = true; lastCycle = cyc; } }
        p = posAt(u);
        const float pH = playing ? posAt(u + lagH) : p, pG = playing ? posAt(u + lagG) : p, pP = playing ? posAt(u + lagP) : p;
        // ---- control tick: targets -> smoothing -> per-sample ramps for this sub-block
        int nP = 0, nO = 0; active = 0;
        for (int i = 0; i < na; ++i) {
            if (slotOf[(size_t) i] < 0) continue;
            const SnapArc& a = *cur->arcs[(size_t) i]; Voice& v = voices[(size_t) slotOf[(size_t) i]];
            float xg = 0, xh = 0, xp = 0;
            const bool on = playing && a.local(pG, xg);
            float gT = 0, hzT = v.hzS, panT = v.panS, cutT = v.cutS;
            if (on) {
                if (!a.local(pH, xh)) { xh = xg; } if (!a.local(pP, xp)) { xp = xg; }
                const float iw = 1.0f / a.span, t = (xg - a.x0) * iw;
                gT = a.gain * lerpTable(a.env, t) * juce::jmin(1.0f, t * fadeInv[(size_t) i], (1.0f - t) * fadeInv[(size_t) i]);
                float mm = cur->heardMidi(a, xh, v.seg) + transpose; hzT = midiToHz(juce::jlimit(kMinMidi - 24.0f, kMaxMidi + 12.0f, finiteOr(mm, 69.0f)));
                panT = lerpTable(a.pan, (xp - a.x0) * iw); cutT = lerpTable(a.cut, (xp - a.x0) * iw);
                // a restart (repeat seam, loop, jump, MIDI retrigger): the pitch lands at once instead of gliding from the old place
                if (!v.wasOn || jumped || std::abs(xg - v.lastX) > 0.5f * a.span) v.fresh = v.fresh || v.gm < 0.02f || jumped;
                v.lastX = xg;
            }
            v.wasOn = on;
            if (!on && v.gainS < 1.0e-5f && v.gm < 1.0e-5f) { v.gainS = v.gm = v.gl = v.gr = 0; v.lastOut = 0; v.fresh = true; v.s1L = v.s2L = v.s1R = v.s2R = 0; continue; }
            if (v.fresh) { v.hzS = hzT; v.panS = panT; v.cutS = cutT; v.inc = hzT * (v.os ? isrO : isr1); v.fresh = false; }
            const float hzPrev = v.hzS;
            v.hzS += (hzT - v.hzS) * kH[(size_t) m]; v.gainS += (gT - v.gainS) * kG[(size_t) m]; v.panS += (panT - v.panS) * kP[(size_t) m]; v.cutS += (cutT - v.cutS) * kP[(size_t) m];
            if (!std::isfinite(v.hzS + v.gainS + v.panS + v.cutS)) { v = Voice(); v.id = a.id; v.arc = i; v.sa = &a; v.os = cur->os[(size_t) i] != 0; continue; }   // never let a bad value stick
            const float isr = v.os ? isrO : isr1;
            const float g1 = v.gainS, l1 = g1 * std::sqrt(0.5f * (1.0f - v.panS)), r1 = g1 * std::sqrt(0.5f * (1.0f + v.panS));
            // unison layout
            const int U = a.unison; const float un = 1.0f / std::sqrt((float) U);
            for (int k = 0; k < U; ++k) { const float pos = U > 1 ? (2.0f * k / (U - 1) - 1.0f) : 0.0f; const float sp = pos * a.spread;
                v.ratio[(size_t) k] = std::exp2(pos * a.detune * 50.0f / 1200.0f);
                v.uL[(size_t) k] = (U > 1 ? std::cos((sp + 1) * juce::MathConstants<float>::pi * 0.25f) * juce::MathConstants<float>::sqrt2 : 1.0f) * un;
                v.uR[(size_t) k] = (U > 1 ? std::sin((sp + 1) * juce::MathConstants<float>::pi * 0.25f) * juce::MathConstants<float>::sqrt2 : 1.0f) * un; }
            // FM depth (cycles of phase per unit of the source's output), smoothed
            const float fmT = a.fmDepth * a.fmDepth * 2.0f; v.fmS += (fmT - v.fmS) * kG[(size_t) m];
            // mip level: from the peak instantaneous frequency (pitch + FM deviation), under this path's limit
            float hzTop = juce::jmax(hzPrev, v.hzS) * (U > 1 ? std::exp2(a.detune * 50.0f / 1200.0f) : 1.0f);
            float nyq = v.os ? (a.drive > 0.001f ? nyq1 : nyqFm) : nyq1;
            const int src = cur->fmIdx[(size_t) i];
            if (src >= 0 && slotOf[(size_t) src] >= 0) { const Voice& sv = voices[(size_t) slotOf[(size_t) src]]; const SnapArc& sa2 = *cur->arcs[(size_t) src];
                const float slope = sa2.source == SrcNoise ? 2.0f * (float) sr : sa2.wt->slope[(size_t) sv.L0] * sv.hzS;
                const float dev = juce::jmax(v.fmS, fmT) * juce::jmax(sv.gm, sv.gainS) * std::sqrt((float) sa2.unison) * slope;
                hzTop = hzTop + dev + sv.hzS; }
            const float lvA = levelOf(juce::jmax(hzPrev, hzTop), nyq), lvB = levelOf(hzTop, nyq);
            const int L0 = juce::jlimit(0, kMipLevels - 1, (int) std::floor(juce::jmax(lvA, lvB)));
            const float mixA = juce::jlimit(0.0f, 1.0f, lvA - (float) L0), mixB = juce::jlimit(0.0f, 1.0f, lvB - (float) L0);
            v.L0 = L0; v.L1 = juce::jmin(kMipLevels - 1, L0 + 1);
            const int sm = m * (v.os ? os : 1); const float ism = 1.0f / (float) sm;
            v.mix = mixA; v.dMix = (mixB - mixA) * ism;
            v.dInc = (v.hzS * isr - v.inc) * ism; v.dGm = (g1 - v.gm) * ism; v.dGl = (l1 - v.gl) * ism; v.dGr = (r1 - v.gr) * ism; v.dFm = (v.fmS - v.fm) * ism;
            // filter coefficients (TPT state-variable), ramped
            if (a.filter != FiltOff) {
                const float fs = (float) sr * (v.os ? os : 1);
                const float fc = a.track ? v.hzS * std::exp2((v.cutS - 0.5f) * 8.0f) : 20.0f * std::exp2(v.cutS * 10.0f);
                const float g = std::tan(juce::MathConstants<float>::pi * juce::jlimit(10.0f, 0.45f * fs, fc) / fs); const float kq = 2.0f - 1.96f * a.res;
                const float a1 = 1.0f / (1.0f + g * (g + kq)), a2 = g * a1, a3 = g * a2;
                if (!(v.a1 > 0.0f)) { v.a1 = a1; v.a2 = a2; v.a3 = a3; }
                v.fk = kq; v.da1 = (a1 - v.a1) * ism; v.da2 = (a2 - v.a2) * ism; v.da3 = (a3 - v.a3) * ism;
            }
            if (v.os) runO[(size_t) nO++] = i; else runP[(size_t) nP++] = i;
            if (on) ++active;
        }
        // orphans (their line was deleted): fade out on their own sound, pan kept
        for (int k = 0; k < kMaxArcs; ++k) { auto& v = voices[(size_t) k]; if (v.arc >= 0 || v.sa == nullptr) continue;
            if (v.gm < 1.0e-5f && v.gainS < 1.0e-5f) { v.id = -1; v.sa = nullptr; v.gainS = v.gm = v.gl = v.gr = 0; v.lastOut = 0; continue; }
            const float old = juce::jmax(1.0e-9f, v.gainS); v.gainS += (0.0f - v.gainS) * kG[(size_t) m]; const float ratio = v.gainS / old;
            const int sm = m * (v.os ? os : 1); const float ism = 1.0f / (float) sm;
            v.dInc = 0; v.dMix = 0; v.dFm = -v.fm * ism; v.dGm = (v.gm * ratio - v.gm) * ism; v.dGl = (v.gl * ratio - v.gl) * ism; v.dGr = (v.gr * ratio - v.gr) * ism; v.da1 = v.da2 = v.da3 = 0;
            if (v.os) runO[(size_t) nO++] = -1 - k; else runP[(size_t) nP++] = -1 - k; }
        jumped = false;
        auto voiceOf = [&](int code) -> Voice& { return code >= 0 ? voices[(size_t) slotOf[(size_t) code]] : voices[(size_t) (-1 - code)]; };
        // ---- plain path (delayed by kLatency to line up with the decimated group)
        float bl[kCtl], br[kCtl];
        for (int k = 0; k < m; ++k) {
            float l = 0, r = 0;
            for (int q = 0; q < nP; ++q) { const int code = runP[(size_t) q]; Voice& v = voiceOf(code); voiceSample(v, *v.sa, 0.0f, l, r); }
            auto& dl = dly[0]; auto& dr = dly[1]; const int sz = (int) dl.size();
            dl[(size_t) dlyPos] = l; dr[(size_t) dlyPos] = r; const int rd = (dlyPos - kLatency + sz) % sz; bl[k] = dl[(size_t) rd]; br[k] = dr[(size_t) rd]; dlyPos = (dlyPos + 1) % sz;
        }
        // ---- oversampled group -> FIR decimator
        if (nO > 0) osQuiet = 0; else osQuiet += m * os;
        if (osQuiet < firN + kCtl * os) {
            const int H = firN - 1; float* xl = osL.data() + H; float* xr = osR.data() + H; const int so = m * os;
            for (int k = 0; k < so; ++k) {
                float l = 0, r = 0;
                for (int q = 0; q < nO; ++q) { const int code = runO[(size_t) q]; Voice& v = voiceOf(code); float fmOff = 0;
                    if (code >= 0) { const int src = cur->fmIdx[(size_t) code]; if (src >= 0 && slotOf[(size_t) src] >= 0) fmOff = v.fm * voices[(size_t) slotOf[(size_t) src]].lastOut; }
                    voiceSample(v, *v.sa, fmOff, l, r); }
                xl[k] = l; xr[k] = r;
            }
            for (int j = 0; j < m; ++j) { const int idx = H + j * os + os - 1; float yl = 0, yr = 0; const float* h = fir.data();
                for (int t = 0; t < firN; ++t) { yl += h[t] * osL[(size_t) (idx - t)]; yr += h[t] * osR[(size_t) (idx - t)]; }
                bl[j] += yl; br[j] += yr; }
            std::memmove(osL.data(), osL.data() + so, sizeof(float) * (size_t) H); std::memmove(osR.data(), osR.data() + so, sizeof(float) * (size_t) H);
        }
        // ---- master, auto gain, soft knee
        const float agT = cur->autoGain ? juce::jmin(1.0f, 2.0f / std::sqrt((float) juce::jmax(1, active))) : 1.0f; const float ag0 = autoG; autoG += (agT - autoG) * kG[(size_t) m];
        auto knee = [&](float x) { if (!(std::abs(x) < 1.0e6f)) return 0.0f;   // last-resort guard: never pass a non-finite sample on
            const float ax = std::abs(x); if (ax <= 0.8f) return x; kneeHit = true; return (x < 0 ? -1.0f : 1.0f) * (0.8f + 0.2f * std::tanh((ax - 0.8f) / 0.2f)); };
        for (int k = 0; k < m; ++k) {
            const float g = cur->master * (ag0 + (autoG - ag0) * (float) (k + 1) * im);
            const float l = knee(bl[k] * g), r = knee(br[k] * g);
            L[k0 + k] += l; R[k0 + k] += r; peakL = juce::jmax(peakL, std::abs(l)); peakR = juce::jmax(peakR, std::abs(r));
        }
        if (playing) u += du * m;
    }
    if (kneeHit) kneeHold = (int) (0.3 * sr); else kneeHold = juce::jmax(0, kneeHold - n);
    kneeActive.store(kneeHold > 0);
    meterL.store(peakL); meterR.store(peakR); peakL *= 0.6f; peakR *= 0.6f;
    activeVoices.store(active); isPlaying.store(midiMode ? midiGate : (hostDriven ? !countIn : internalPlay.load())); playPos.store(isPlaying.load() ? p : -1.0f);
}

// ---------------------------------------------------------------- offline render (streamed: memory stays flat for any page length)
bool Engine::render(const Snapshot& s, double sampleRate, double bpm, const std::function<bool(const float*, const float*, int)>& sink,
                    std::atomic<int>* progress, const std::atomic<bool>* abort) {
    auto e = std::make_unique<Engine>(); e->prepare(sampleRate, 512); e->oneShot = true;   // on the heap: an engine holds 256 voices
    auto copy = std::make_unique<Snapshot>(s); copy->loopOn = false; copy->midiIn = false;
    const double tempo = bpm > 1 ? bpm : copy->ownBpm, srr = e->sampleRate();
    const double dur = copy->durationSeconds(tempo);
    e->publish(std::move(copy)); e->followHost = false; e->internalPlay = true; e->stopRequest = true;
    const juce::int64 end = (juce::int64) std::ceil(dur * srr), total = end + (juce::int64) (0.1 * srr) + kLatency;
    HostInfo h; h.bpm = tempo; h.playing = false;
    float bl[512], br[512]; juce::int64 skip = kLatency;
    for (juce::int64 pos = 0; pos < total;) {
        if (abort != nullptr && abort->load()) return false;
        if (pos >= end) e->internalPlay = false;   // page over: let the tails fade
        const juce::int64 lim = pos < end ? end : total;   // a block never runs past the end of the page (it would wrap)
        const int n = (int) juce::jmin((juce::int64) 512, lim - pos);
        e->process(bl, br, n, h, false);
        pos += n;
        const int off = (int) juce::jmin((juce::int64) n, skip); skip -= off;
        if (n - off > 0 && !sink(bl + off, br + off, n - off)) return false;
        if (progress != nullptr) progress->store((int) (100.0 * (double) pos / (double) total));
    }
    return true;
}
void Engine::render(const Snapshot& s, double sampleRate, double bpm, std::vector<float>& L, std::vector<float>& R) {
    L.clear(); R.clear(); const double secs = renderSeconds(s, bpm) * juce::jlimit(8000.0, 768000.0, sampleRate); L.reserve((size_t) secs + 1024); R.reserve((size_t) secs + 1024);
    render(s, sampleRate, bpm, [&](const float* l, const float* r, int n) { L.insert(L.end(), l, l + n); R.insert(R.end(), r, r + n); return true; });
}

} // namespace arcs
