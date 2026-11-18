#include "ArrangeEngine.h"
#include <juce_dsp/juce_dsp.h>
#include "signalsmith-stretch/signalsmith-stretch.h"
#include <algorithm>
#include <cmath>

namespace arr {

// ================================================================== offline DSP
namespace {
// Kaiser-windowed sinc, 24 zero crossings each side, tabulated (256 points per crossing, linear interpolation)
constexpr int kZ = 24, kRes = 256;
const std::vector<float>& sincTable() {
    static const std::vector<float> t = [] {
        auto I0 = [](double x) { double s = 1, term = 1; for (int k = 1; k < 40; ++k) { term *= (x / (2 * k)) * (x / (2 * k)); s += term; } return s; };
        const double beta = 9.0, i0b = I0(beta); std::vector<float> v((size_t) (kZ * kRes + 2), 0.0f);
        for (int i = 0; i <= kZ * kRes; ++i) {
            const double x = (double) i / kRes, r = x / kZ;
            const double sinc = x < 1e-12 ? 1.0 : std::sin(juce::MathConstants<double>::pi * x) / (juce::MathConstants<double>::pi * x);
            v[(size_t) i] = (float) (sinc * I0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0b);
        }
        return v; }();
    return t;
}
inline float kernel(double d) {   // d in crossings, any sign
    d = std::abs(d); if (d >= kZ) return 0.0f;
    const auto& t = sincTable(); const double p = d * kRes; const int i = (int) p; const float f = (float) (p - i);
    return t[(size_t) i] + f * (t[(size_t) i + 1] - t[(size_t) i]);
}
void sanitise(juce::AudioBuffer<float>& b) {
    for (int ch = 0; ch < b.getNumChannels(); ++ch) { auto* d = b.getWritePointer(ch); for (int i = 0; i < b.getNumSamples(); ++i) if (!std::isfinite(d[i])) d[i] = 0.0f; }
}
inline bool cancelled(const std::atomic<bool>* c) { return c && c->load(); }
constexpr juce::int64 kMaxProcessedSamples = (juce::int64) 1 << 28;   // ~93 min stereo at 48 k: never allocate past this
}

juce::AudioBuffer<float> resample(const juce::AudioBuffer<float>& in, double ratio, const std::atomic<bool>* cancel) {
    juce::AudioBuffer<float> out; const int n = in.getNumSamples(), nc = in.getNumChannels();
    if (n <= 0 || nc <= 0 || !(ratio > 0) || !std::isfinite(ratio)) { out.makeCopyOf(in); return out; }
    if (std::abs(ratio - 1.0) < 1e-12) { out.makeCopyOf(in); return out; }
    const int outN = (int) std::max<juce::int64>(1, std::min<juce::int64>(kMaxProcessedSamples, (juce::int64) std::floor(n / ratio)));
    out.setSize(nc, outN);
    const double fc = std::min(1.0, 1.0 / ratio) * 0.97, halfW = kZ / fc;
    for (int ch = 0; ch < nc; ++ch) {
        const float* x = in.getReadPointer(ch); float* y = out.getWritePointer(ch);
        for (int j = 0; j < outN; ++j) {
            if ((j & 65535) == 0 && cancelled(cancel)) return out;
            const double c = j * ratio; const int i0 = std::max(0, (int) std::ceil(c - halfW)), i1 = std::min(n - 1, (int) std::floor(c + halfW));
            double acc = 0; for (int i = i0; i <= i1; ++i) acc += x[i] * kernel((c - i) * fc);
            y[j] = (float) (acc * fc);
        }
    }
    return out;
}

// Signalsmith Stretch, the same steps as its exact() (outputSeek, process, flush) but in chunks so it can be cancelled
juce::AudioBuffer<float> stretchPitch(const juce::AudioBuffer<float>& in, double factor, float semitones, double sampleRate, const std::atomic<bool>* cancel) {
    const int n = in.getNumSamples(), nc = std::max(1, std::min(2, in.getNumChannels()));
    if (!(factor > 0) || !std::isfinite(factor)) factor = 1.0;
    const int outLen = (int) std::max<juce::int64>(1, std::min<juce::int64>(kMaxProcessedSamples, std::llround(n * factor)));
    juce::AudioBuffer<float> out(nc, outLen); out.clear();
    if (n <= 0) return out;
    signalsmith::stretch::SignalsmithStretch<float> st(0x6a7a11);   // fixed seed: the same edit always renders the same audio
    st.presetDefault(nc, (float) sampleRate);
    st.setTransposeSemitones(semitones, 0.0f);
    st.setFormantFactor(1.0f, false);
    const double rate = (double) n / outLen;
    const int need = st.outputSeekLength((float) rate) + 16;
    const int nPad = std::max(n, need) + st.inputLatency(), outPad = std::max(outLen, (int) std::llround(nPad / rate));
    const int inTotal = std::max(nPad, (int) std::llround(outPad * rate));
    juce::AudioBuffer<float> X(nc, inTotal + 1), Y(nc, outPad);
    X.clear(); Y.clear(); for (int ch = 0; ch < nc; ++ch) X.copyFrom(ch, 0, in, std::min(ch, in.getNumChannels() - 1), 0, n);
    std::vector<float*> xp((size_t) nc), yp((size_t) nc);
    const float playbackRate = (float) inTotal / (float) outPad;
    const int seekLength = st.outputSeekLength(playbackRate);
    for (int ch = 0; ch < nc; ++ch) xp[(size_t) ch] = X.getWritePointer(ch);
    st.outputSeek(xp.data(), seekLength);
    const int outputIndex = juce::jlimit(0, outPad, (int) (outPad - seekLength / playbackRate));
    const int inAvail = inTotal - seekLength;
    int inDone = 0; constexpr int chunk = 4096;
    for (int outDone = 0; outDone < outputIndex;) {
        if (cancelled(cancel)) return out;
        const int oc = std::min(chunk, outputIndex - outDone);
        const int inTarget = outDone + oc >= outputIndex ? inAvail : juce::jlimit(inDone, inAvail, (int) std::llround((double) (outDone + oc) * inAvail / outputIndex));
        for (int ch = 0; ch < nc; ++ch) { xp[(size_t) ch] = X.getWritePointer(ch) + seekLength + inDone; yp[(size_t) ch] = Y.getWritePointer(ch) + outDone; }
        st.process(xp.data(), inTarget - inDone, yp.data(), oc);
        inDone = inTarget; outDone += oc;
    }
    for (int ch = 0; ch < nc; ++ch) yp[(size_t) ch] = Y.getWritePointer(ch) + outputIndex;
    st.flush(yp.data(), outPad - outputIndex, playbackRate);
    for (int ch = 0; ch < nc; ++ch) out.copyFrom(ch, 0, Y, ch, 0, outLen);
    sanitise(out); return out;
}

juce::AudioBuffer<float> processSound(const Payload& p, float pitch, bool keepLength, float stretch, bool reverse, double sampleRate, double warpRatio, const std::atomic<bool>* cancel) {
    juce::AudioBuffer<float> x(2, std::max(1, p.audio.getNumSamples())); x.clear();
    if (p.audio.getNumChannels() > 0) for (int ch = 0; ch < 2; ++ch) x.copyFrom(ch, 0, p.audio, std::min(ch, p.audio.getNumChannels() - 1), 0, p.audio.getNumSamples());
    sanitise(x);
    pitch = (float) clampF(pitch, -24.0, 24.0, 0.0); stretch = (float) clampF(stretch, 0.25, 4.0, 1.0); warpRatio = clampF(warpRatio, 0.05, 20.0, 1.0);
    const double srcRate = p.sampleRate > 1000 ? p.sampleRate : sampleRate;
    // tape: speed = 2^(pitch/12) (and WARP speeds the tape up / down too); keep length: Signalsmith does pitch + stretch + warp
    const double ratio = srcRate / sampleRate * (keepLength ? 1.0 : std::pow(2.0, pitch / 12.0) / warpRatio);
    if (std::abs(ratio - 1.0) > 1e-12) x = resample(x, ratio, cancel);
    if (cancelled(cancel)) return x;
    const float semis = keepLength ? pitch : 0.0f; const double factor = keepLength ? stretch * warpRatio : stretch;
    if (std::abs(semis) > 1e-4f || std::abs(factor - 1.0) > 1e-6) x = stretchPitch(x, factor, semis, sampleRate, cancel);
    if (reverse) x.reverse(0, x.getNumSamples());
    return x;
}

void spectralMorph(const float* a, const float* b, float* out, int n, MorphShape shape, float amount, float smear) {
    if (n <= 0) return;
    amount = (float) clampF(amount, 0.0, 1.0, 1.0); smear = (float) clampF(smear, 0.0, 1.0, 0.0);
    constexpr int order = 11, N = 1 << order, hop = N / 4;
    auto xfade = [&](float* dst) { for (int i = 0; i < n; ++i) { const double x = morphCurve(shape, n > 1 ? (double) i / (n - 1) : 1.0);
        dst[i] = (float) (a[i] * std::cos(x * juce::MathConstants<double>::halfPi) + b[i] * std::sin(x * juce::MathConstants<double>::halfPi)); } };
    if (n <= 2 * N || amount <= 0.0f) { xfade(out); return; }   // too short for frames, or a plain crossfade (exact at both ends)
    juce::dsp::FFT fft(order); std::vector<float> win((size_t) N), fa((size_t) (2 * N)), fb((size_t) (2 * N)), held((size_t) (N / 2 + 1), 0.0f);
    for (int i = 0; i < N; ++i) win[(size_t) i] = (float) (0.5 - 0.5 * std::cos(2 * juce::MathConstants<double>::pi * i / N));
    std::vector<double> acc((size_t) n, 0.0), wsum((size_t) n, 0.0); bool haveHeld = false;
    for (int f0 = -N + hop; f0 < n; f0 += hop) {
        const double centre = f0 + N / 2.0;
        // frames whose centre is within one frame of an end stay pure (that is what makes the ends exactly a and b)
        const double lin = juce::jlimit(0.0, 1.0, (centre - N) / (double) (n - 2 * N)); const double t = morphCurve(shape, lin);
        std::fill(fa.begin(), fa.end(), 0.0f); std::fill(fb.begin(), fb.end(), 0.0f);
        for (int i = 0; i < N; ++i) { const int s = f0 + i; if (s >= 0 && s < n) { fa[(size_t) i] = a[s] * win[(size_t) i]; fb[(size_t) i] = b[s] * win[(size_t) i]; } }
        if (lin <= 0.0 || lin >= 1.0) {
            const auto& src = lin <= 0.0 ? fa : fb;
            for (int i = 0; i < N; ++i) { const int s = f0 + i; if (s >= 0 && s < n) { acc[(size_t) s] += src[(size_t) i] * win[(size_t) i]; wsum[(size_t) s] += (double) win[(size_t) i] * win[(size_t) i]; } }
            continue;
        }
        fft.performRealOnlyForwardTransform(fa.data(), true); fft.performRealOnlyForwardTransform(fb.data(), true);
        const double bump = smear > 0 ? smear * std::pow(std::sin(juce::MathConstants<double>::pi * lin), 2.0) : 0.0;   // smear peaks at the centre
        const float ca = (float) std::cos(t * juce::MathConstants<double>::halfPi), cb = (float) std::sin(t * juce::MathConstants<double>::halfPi);
        for (int k = 0; k <= N / 2; ++k) {
            const std::complex<float> A(fa[(size_t) (2 * k)], fa[(size_t) (2 * k + 1)]), B(fb[(size_t) (2 * k)], fb[(size_t) (2 * k + 1)]);
            float mag = (float) ((1 - t) * std::abs(A) + t * std::abs(B));
            if (bump > 0) { if (!haveHeld) held[(size_t) k] = mag; held[(size_t) k] += (float) (0.15 + 0.85 * (1 - bump)) * (mag - held[(size_t) k]); mag = (float) (bump * held[(size_t) k] + (1 - bump) * mag); }
            std::complex<float> Z = (float) (1 - t) * A + (float) t * B; float zm = std::abs(Z);
            if (zm < 1e-12f) { Z = t < 0.5 ? A : B; zm = std::abs(Z); }
            std::complex<float> Y = zm > 1e-12f ? Z * (mag / zm) : std::complex<float>(0, 0);
            if (amount < 1.0f) Y = amount * Y + (1.0f - amount) * (ca * A + cb * B);   // part spectral, part plain crossfade
            fa[(size_t) (2 * k)] = Y.real(); fa[(size_t) (2 * k + 1)] = Y.imag();
        }
        haveHeld = bump > 0;
        for (int k = 1; k < N / 2; ++k) { fa[(size_t) (2 * (N - k))] = fa[(size_t) (2 * k)]; fa[(size_t) (2 * (N - k) + 1)] = -fa[(size_t) (2 * k + 1)]; }
        fft.performRealOnlyInverseTransform(fa.data());
        for (int i = 0; i < N; ++i) { const int s = f0 + i; if (s >= 0 && s < n) { acc[(size_t) s] += fa[(size_t) i] * win[(size_t) i]; wsum[(size_t) s] += (double) win[(size_t) i] * win[(size_t) i]; } }
    }
    for (int i = 0; i < n; ++i) { const float v = wsum[(size_t) i] > 1e-9 ? (float) (acc[(size_t) i] / wsum[(size_t) i]) : 0.0f; out[i] = std::isfinite(v) ? v : 0.0f; }
}

// ================================================================== cache
Buf Cache::find(const juce::String& key) { const std::lock_guard<std::mutex> l(m); auto it = map.find(key); if (it == map.end()) return nullptr; it->second.lastPass = pass; return it->second.buf; }
void Cache::put(const juce::String& key, Buf b) { const std::lock_guard<std::mutex> l(m); map[key] = { std::move(b), pass }; }
Buf Cache::get(const Clip& c, double sr, double bpm, const std::atomic<bool>* cancel) {
    if (!c.source) return nullptr;
    const auto key = c.processKey(bpm) + "@" + juce::String(sr, 1);
    if (auto b = find(key)) { const std::lock_guard<std::mutex> l(m); latestBase[c.baseKey() + "@" + juce::String(sr, 1)] = b; latestClip[c.id] = b; return b; }
    auto x = processSound(*c.source, c.pitch, c.keepLength, c.stretch, c.reverse, sr, c.warpRatio(bpm), cancel);
    if (cancelled(cancel)) return nullptr;
    auto b = std::make_shared<const juce::AudioBuffer<float>>(std::move(x));
    { const std::lock_guard<std::mutex> l(m); map[key] = { b, pass }; latestBase[c.baseKey() + "@" + juce::String(sr, 1)] = b; latestClip[c.id] = b; }
    return b;
}
Buf Cache::getOrStale(const Clip& c, double sr, double bpm, bool& stale) {
    stale = false; if (!c.source) return nullptr;
    if (auto b = find(c.processKey(bpm) + "@" + juce::String(sr, 1))) return b;
    const std::lock_guard<std::mutex> l(m);
    auto it = latestBase.find(c.baseKey() + "@" + juce::String(sr, 1)); if (it != latestBase.end() && it->second) { stale = true; return it->second; }
    return nullptr;   // a different pitch etc.: the caller processes (a clip's old version at the old rate would play at the wrong speed)
}
size_t Cache::bytes() const { const std::lock_guard<std::mutex> l(m); size_t s = 0; for (auto& e : map) if (e.second.buf) s += (size_t) e.second.buf->getNumSamples() * (size_t) e.second.buf->getNumChannels() * sizeof(float); return s; }
void Cache::prune(int keepPasses, size_t maxBytes) {
    const std::lock_guard<std::mutex> l(m);
    for (auto it = map.begin(); it != map.end();) { if (pass - it->second.lastPass > keepPasses) it = map.erase(it); else ++it; }
    auto total = [&] { size_t s = 0; for (auto& e : map) s += (size_t) e.second.buf->getNumSamples() * 2 * sizeof(float); return s; };
    while (map.size() > 1 && total() > maxBytes) {
        auto old = map.begin(); for (auto it = map.begin(); it != map.end(); ++it) if (it->second.lastPass < old->second.lastPass) old = it;
        if (old->second.lastPass >= pass) break; map.erase(old);
    }
    // stand-ins only for buffers something still holds (the map, or a live snapshot)
    for (auto it = latestBase.begin(); it != latestBase.end();) { if (it->second.use_count() <= 1) it = latestBase.erase(it); else ++it; }
    for (auto it = latestClip.begin(); it != latestClip.end();) { if (it->second.use_count() <= 1) it = latestClip.erase(it); else ++it; }
}

// ================================================================== snapshot
namespace {
inline float envAt(const EnvS& e, juce::int64 t, size_t& i) noexcept {   // linear; i advances with t (call with increasing t)
    const size_t n = e.t.size(); if (n == 0) return 1.0f;
    if (i > n || (i > 0 && e.t[i - 1] > t)) i = (size_t) (std::upper_bound(e.t.begin(), e.t.end(), t) - e.t.begin());
    while (i < n && e.t[i] <= t) ++i;
    if (i == 0) return e.v.front(); if (i >= n) return e.v.back();
    const juce::int64 t0 = e.t[i - 1], t1 = e.t[i]; const float v0 = e.v[i - 1], v1 = e.v[i];
    return t1 > t0 ? v0 + (v1 - v0) * (float) ((double) (t - t0) / (double) (t1 - t0)) : v1;
}
inline float eqp(double x) noexcept { return (float) std::sin(juce::jlimit(0.0, 1.0, x) * juce::MathConstants<double>::halfPi); }
// clip-relative gain from fades + micro fades (k = sample index inside the clip span)
inline float fadeGain(const Item& it, juce::int64 k) noexcept {
    float g = 1.0f;
    if (it.microIn > it.fadeIn) { if (k < it.microIn) g = eqp((double) k / (double) it.microIn); }
    else if (k < it.fadeIn) g = fadeInGain(it.inCurve, (double) k / (double) it.fadeIn);
    const juce::int64 rem = it.clipLen - k;
    if (it.microOut > it.fadeOut) { if (rem <= it.microOut) g *= eqp((double) rem / (double) it.microOut); }
    else if (it.fadeOut > 0 && rem <= it.fadeOut) g *= fadeInGain(it.outCurve, (double) rem / (double) it.fadeOut);
    return g;
}
struct Piece { juce::int64 a, b, eIn = 0, eOut = 0; };
}

std::unique_ptr<Snapshot> buildSnapshot(const Arrangement& a, double sr, Cache& cache, const BuildOptions& o, const std::atomic<bool>* cancel, std::function<void(float)> progress) {
    auto s = std::make_unique<Snapshot>(); s->sampleRate = sr; s->bpm = clampF(a.bpm, 20.0, 400.0, 120.0); s->beatsPerBar = a.beatsPerBar; s->numTracks = (int) a.tracks.size();
    const double spb = sr * 60.0 / s->bpm; auto toS = [&](double beats) { return (juce::int64) std::llround(clampF(beats, -kMaxBeats, kMaxBeats, 0.0) * spb); };
    s->loopOn = a.loopOn; s->loopStart = toS(std::max(0.0, a.loopStart)); s->loopEnd = std::max(s->loopStart + 1, toS(a.loopEnd));
    s->tracks.resize(a.tracks.size());
    const juce::int64 X = std::max<juce::int64>(1, (juce::int64) std::llround(kMicroFadeSeconds * sr)), XO = std::max<juce::int64>(1, (juce::int64) std::llround(kOverlapFadeSeconds * sr));
    auto makeEnv = [&](const Envelope& e, bool db) -> const EnvS* {   // clip envelopes: clip-relative beats; track lanes: song beats
        if (e.empty()) return nullptr; s->envs.emplace_back(); auto& x = s->envs.back();
        for (auto& p : e) { x.t.push_back(toS(p.beat)); x.v.push_back(db ? juce::Decibels::decibelsToGain(p.value, -100.0f) : p.value); }
        return &x;
    };
    int total = 0, done = 0; for (auto& t : a.tracks) total += (int) t.clips.size();
    for (int ti = 0; ti < (int) a.tracks.size(); ++ti) {
        const auto& tr = a.tracks[(size_t) ti];
        if (o.onlyClipTrack >= 0 && ti != o.onlyClipTrack) continue;
        if (!o.includeInaudible && o.onlyClip == 0 && o.onlyClips == nullptr && !a.trackAudible(ti)) { done += (int) tr.clips.size(); continue; }
        auto& T = s->tracks[(size_t) ti];
        if (!o.clipLevelOnly) {
            T.gain = juce::Decibels::decibelsToGain(tr.gainDb, -100.0f); T.panV = tr.pan; T.tL = panLeft(tr.pan) * T.gain; T.tR = panRight(tr.pan) * T.gain;
            T.vol = makeEnv(tr.volEnv, true); T.pan = makeEnv(tr.panEnv, false);
        }
        struct C { const Clip* c; Buf buf; juce::int64 S, E, off, fi, fo, mi = 0, mo = 0, L = 0; float cL, cR; const EnvS* env = nullptr; std::vector<Piece> pieces;
                   bool dropIn = false, dropOut = false, stale = false; juce::int64 xIn = 0, xOut = 0; };
        std::vector<C> cs;
        for (auto& c : tr.clips) {
            if (cancelled(cancel)) return nullptr;
            ++done; if (progress) progress(total > 0 ? 0.9f * done / total : 0.9f);
            if (c.mute || !c.source) continue;
            if (o.onlyClip != 0 && c.id != o.onlyClip) continue;
            if (o.onlyClips != nullptr && !o.onlyClips->count(c.id)) continue;
            C x; x.c = &c;
            if (o.allowStale) { x.buf = cache.getOrStale(c, sr, s->bpm, x.stale); if (x.stale) s->stale = true; }
            if (!x.buf) x.buf = cache.get(c, sr, s->bpm, cancel);
            if (cancelled(cancel)) return nullptr;
            if (!x.buf || x.buf->getNumSamples() <= 0) continue;
            x.L = x.buf->getNumSamples();
            x.S = toS(c.start); x.E = std::max(x.S + 1, toS(c.end())); x.off = (juce::int64) std::llround(clampF(c.offset, 0.0, kMaxOffsetSeconds, 0.0) * sr);
            if (c.loop) x.off %= x.L;
            const juce::int64 len = x.E - x.S; x.fi = std::max<juce::int64>(0, toS(c.fadeIn)); x.fo = std::max<juce::int64>(0, toS(c.fadeOut));
            if (x.fi + x.fo > len) { const double k = (double) len / (double) (x.fi + x.fo); x.fi = (juce::int64) std::floor(x.fi * k); x.fo = len - x.fi; }
            // automatic declick where the clip cuts into its sound (not at the sound's own start / end)
            const juce::int64 mx = std::min(X, len / 2);
            x.mi = x.off > 0 ? mx : 0;
            x.mo = (c.loop ? ((x.off + len) % x.L != 0) : (x.off + len < x.L)) ? mx : 0;
            const float g = juce::Decibels::decibelsToGain(c.gainDb, -100.0f); x.cL = g * panLeft(c.pan); x.cR = g * panRight(c.pan);
            x.env = makeEnv(c.gainEnv, true);
            x.pieces.push_back({ x.S, x.E }); cs.push_back(std::move(x));
        }
        // a split clip that continues seamlessly into its other half: no declick at the join
        for (size_t i = 0; i + 1 < cs.size(); ++i) { auto& A = cs[i]; auto& B = cs[i + 1];
            if (A.E == B.S && A.buf == B.buf && !A.c->loop && !B.c->loop && A.off + (A.E - A.S) == B.off && A.cL == B.cL && A.cR == B.cR) { A.mo = 0; B.mi = 0; } }
        auto itemFor = [&](const C& x, bool useIn, bool useOut) { Item it; it.clipStart = x.S; it.clipLen = x.E - x.S; it.fadeIn = useIn ? x.fi : 0; it.fadeOut = useOut ? x.fo : 0;
            it.inCurve = x.c->fadeInCurve; it.outCurve = x.c->fadeOutCurve; it.microIn = x.mi; it.microOut = x.mo; it.loop = x.c->loop; it.loopX = x.c->loop ? std::min(X, x.L / 4) : 0; return it; };
        auto sampleOf = [&](const C& x, juce::int64 t, int ch, bool useIn, bool useOut) {   // what clip x plays at timeline sample t (clip level, no edges)
            const juce::int64 k = t - x.S; juce::int64 si = x.off + k; if (x.c->loop) si %= x.L; if (si < 0 || si >= x.L) return 0.0f;
            const Item it = itemFor(x, useIn, useOut); float g = fadeGain(it, k);
            if (x.env) { size_t ei = 0; g *= envAt(*x.env, k, ei); }
            return x.buf->getSample(ch, (int) si) * g * (ch == 0 ? x.cL : x.cR);
        };
        auto addMorph = [&](const C& A, const C& B, juce::int64 a0, juce::int64 a1, bool forward, bool aOut, bool bOut) {   // forward: A -> B, else B -> A
            const int n = (int) std::min<juce::int64>(a1 - a0, (juce::int64) 1 << 26); if (n < 2) return;
            const auto& F = forward ? A : B; const auto& G = forward ? B : A;
            const bool fIn = forward, fOut = forward ? aOut : bOut, gIn = !forward, gOut = forward ? bOut : aOut;
            const auto key = "morph|" + juce::String(forward ? 1 : 0) + "|" + A.c->processKey(s->bpm) + "|" + juce::String(A.S - a0) + "|" + juce::String(A.off) + "|" + juce::String(A.cL, 6) + "|" + juce::String(A.cR, 6) + "|" + juce::String(A.fi) + "|" + juce::String(A.fo) + "|" + juce::String((int) A.c->morphShape) + juce::String(A.c->morphAmount, 4) + juce::String(A.c->morphSmear, 4) + "|" + juce::String((juce::int64) (size_t) A.env) /* env: no cache */
                + B.c->processKey(s->bpm) + "|" + juce::String(B.S - a0) + "|" + juce::String(B.off) + "|" + juce::String(B.cL, 6) + "|" + juce::String(B.cR, 6) + "|" + juce::String(B.fo) + "|" + juce::String(B.E - a0) + "|" + juce::String(n) + "|" + juce::String((int) aOut) + juce::String((int) bOut) + "@" + juce::String(sr, 1);
            const bool cacheable = !A.stale && !B.stale && !A.env && !B.env;
            Buf mb = cacheable ? cache.find(key) : nullptr;
            if (!mb) {
                auto m = std::make_shared<juce::AudioBuffer<float>>(2, n); std::vector<float> sa((size_t) n), sb((size_t) n);
                for (int ch = 0; ch < 2; ++ch) {
                    for (int k = 0; k < n; ++k) { sa[(size_t) k] = sampleOf(F, a0 + k, ch, fIn, fOut); sb[(size_t) k] = sampleOf(G, a0 + k, ch, gIn, gOut); }
                    spectralMorph(sa.data(), sb.data(), m->getWritePointer(ch), n, A.c->morphShape, A.c->morphAmount, A.c->morphSmear);
                }
                mb = m; if (cacheable) cache.put(key, mb);
            }
            Item it; it.track = ti; it.clipId = A.c->id; it.morph = true; it.start = it.clipStart = a0; it.end = a0 + n; it.clipLen = n; it.bufOffset = 0; it.buf = mb; s->items.push_back(it);
        };
        auto cutPieces = [](std::vector<Piece>& v, juce::int64 c0, juce::int64 c1, juce::int64 xf) {   // remove [c0, c1); fade the new ends over xf
            std::vector<Piece> r;
            for (auto& p : v) {
                if (p.b <= c0 || p.a >= c1) { r.push_back(p); continue; }
                if (p.a < c0) { Piece l = p; l.b = c0; l.eOut = std::min(xf, l.b - l.a); r.push_back(l); }
                if (p.b > c1) { Piece q = p; q.a = c1; q.eIn = std::min(xf, q.b - q.a); r.push_back(q); }
            }
            v.swap(r);
        };
        // overlaps: a clip with MORPH morphs into the next one; otherwise the later clip wins (with a short crossfade)
        for (size_t j = 1; j < cs.size(); ++j) for (size_t i = 0; i < j; ++i) {
            auto& A = cs[i]; auto& B = cs[j]; if (A.E <= B.S || B.S >= B.E) continue;
            if (i + 1 == j && A.c->morph) {
                if (B.E < A.E) {   // B sits inside A: morph into B and back out, so A resumes without a step
                    const juce::int64 mid = B.S + (B.E - B.S) / 2;
                    addMorph(A, B, B.S, mid, true, true, false); addMorph(A, B, mid, B.E, false, true, false);
                    cutPieces(A.pieces, B.S, B.E, 0); cutPieces(B.pieces, B.S, B.E, 0); B.dropIn = B.dropOut = true;
                } else {
                    addMorph(A, B, B.S, A.E, true, false, true);
                    A.dropOut = true; B.dropIn = true; cutPieces(A.pieces, B.S, A.E, 0); cutPieces(B.pieces, B.S, A.E, 0);
                }
                continue;
            }
            const juce::int64 xf = std::min(XO, (B.E - B.S) / 2);
            const bool resumes = A.E > B.E;
            const juce::int64 c0 = B.S + xf, c1 = resumes ? B.E - xf : A.E;
            bool covered = false; for (auto& p : A.pieces) if (p.a <= B.S && p.b > B.S) covered = true;
            if (c1 <= c0) {   // A ends inside B's crossfade: A just fades out under B
                for (auto& p : A.pieces) if (p.b == A.E && p.a < B.S) p.eOut = std::max(p.eOut, std::min(A.E - B.S, p.b - p.a));
                if (covered) B.xIn = std::max(B.xIn, A.E - B.S); continue;
            }
            cutPieces(A.pieces, c0, c1, xf);
            if (covered) B.xIn = std::max(B.xIn, xf);
            if (resumes) B.xOut = std::max(B.xOut, xf);
        }
        for (auto& x : cs) {
            for (auto& p : x.pieces) { if (p.a == x.S && x.xIn > 0) { p.eIn = std::max(p.eIn, x.xIn); x.mi = 0; } if (p.b == x.E && x.xOut > 0) { p.eOut = std::max(p.eOut, x.xOut); x.mo = 0; } }
            for (auto& p : x.pieces) {
                if (p.b <= p.a) continue;
                Item it = itemFor(x, !x.dropIn, !x.dropOut); it.track = ti; it.clipId = x.c->id; it.start = p.a;
                it.end = x.c->loop ? p.b : std::min(p.b, x.S + x.L - x.off);
                if (it.end <= it.start) continue;   // past the end of the sound: silence
                it.edgeIn = p.eIn; it.edgeOut = it.end == p.b ? p.eOut : 0; it.bufOffset = x.off; it.cL = x.cL; it.cR = x.cR; it.clipEnv = x.env; it.buf = x.buf;
                s->items.push_back(it);
            }
        }
    }
    std::stable_sort(s->items.begin(), s->items.end(), [](const Item& x, const Item& y) { return x.start < y.start; });
    for (auto& it : s->items) { s->maxItemLen = std::max(s->maxItemLen, it.end - it.start); s->endSample = std::max(s->endSample, it.end); if (it.track >= 0 && it.track < (int) s->tracks.size()) s->tracks[(size_t) it.track].used = true; }
    s->endSample = std::max(s->endSample, toS(a.lengthBeats()));
    if (progress) progress(1.0f);
    return s;
}

void renderItems(const Snapshot& s, float* L, float* R, juce::int64 t0, int n, int onlyTrack) noexcept {
    if (n <= 0 || s.items.empty()) return;
    const juce::int64 t1 = t0 + n;
    // first item that can reach t0 (items are sorted by start, none is longer than maxItemLen)
    auto it0 = std::lower_bound(s.items.begin(), s.items.end(), t0 - s.maxItemLen, [](const Item& x, juce::int64 v) { return x.start < v; });
    for (auto it = it0; it != s.items.end() && it->start < t1; ++it) {
        const Item& x = *it; if (x.end <= t0 || (onlyTrack >= 0 && x.track != onlyTrack) || !x.buf) continue;
        const TrackS* T = x.track >= 0 && x.track < (int) s.tracks.size() ? &s.tracks[(size_t) x.track] : nullptr;
        const juce::int64 a = std::max(t0, x.start), b = std::min(t1, x.end);
        const float* bl = x.buf->getReadPointer(0); const float* br = x.buf->getReadPointer(x.buf->getNumChannels() > 1 ? 1 : 0);
        const juce::int64 blen = x.buf->getNumSamples(); if (blen <= 0) continue;
        const juce::int64 inEnd = std::max(x.fadeIn, x.microIn), outStart = x.clipLen - std::max(x.fadeOut, x.microOut);
        const bool trackAuto = T && (T->vol || T->pan); const float tl0 = T ? T->tL : 1.0f, tr0 = T ? T->tR : 1.0f;
        size_t ei = 0, vi = 0, pi = 0;
        for (juce::int64 t = a; t < b; ++t) {
            const juce::int64 k = t - x.clipStart; juce::int64 si = x.bufOffset + k;
            float g = 1.0f;
            if (x.loop) {
                if (si < 0) continue;
                const juce::int64 q = si % blen;
                if (x.loopX > 0) {
                    if (si / blen > x.bufOffset / blen && q < x.loopX) g *= eqp((double) q / (double) x.loopX);   // after a wrap
                    const juce::int64 toWrap = blen - q; if (toWrap <= x.loopX && k + toWrap < x.clipLen) g *= eqp((double) toWrap / (double) x.loopX);   // before a wrap
                }
                si = q;
            } else { if (si < 0) continue; if (si >= blen) break; }
            if (k < inEnd || k >= outStart) g *= fadeGain(x, k);
            if (x.edgeIn > 0 && t - x.start < x.edgeIn) g *= eqp((double) (t - x.start) / (double) x.edgeIn);
            if (x.edgeOut > 0 && x.end - t <= x.edgeOut) g *= eqp((double) (x.end - t) / (double) x.edgeOut);
            if (x.clipEnv) g *= envAt(*x.clipEnv, k, ei);
            float tl = tl0, tr = tr0;
            if (trackAuto) { const float tg = T->vol ? envAt(*T->vol, t, vi) : T->gain, tp = T->pan ? juce::jlimit(-1.0f, 1.0f, envAt(*T->pan, t, pi)) : T->panV; tl = tg * panLeft(tp); tr = tg * panRight(tp); }
            const int o = (int) (t - t0);
            if (L == R) L[o] += 0.5f * g * (bl[si] * x.cL * tl + br[si] * x.cR * tr);
            else { L[o] += bl[si] * g * x.cL * tl; if (R) R[o] += br[si] * g * x.cR * tr; }
        }
    }
}
void renderMix(const Snapshot& s, float* L, float* R, juce::int64 t0, int n, float* sL, float* sR, float* trackPeaks) noexcept {
    for (int tr = 0; tr < (int) s.tracks.size(); ++tr) {
        if (!s.tracks[(size_t) tr].used) { if (trackPeaks) trackPeaks[tr] = 0; continue; }
        std::fill(sL, sL + n, 0.0f); std::fill(sR, sR + n, 0.0f);
        renderItems(s, sL, sR, t0, n, tr);
        float pk = 0;
        if (L == R) for (int i = 0; i < n; ++i) { L[i] += 0.5f * (sL[i] + sR[i]); pk = std::max(pk, std::max(std::abs(sL[i]), std::abs(sR[i]))); }
        else for (int i = 0; i < n; ++i) { L[i] += sL[i]; if (R) R[i] += sR[i]; pk = std::max(pk, std::max(std::abs(sL[i]), std::abs(sR[i]))); }
        if (trackPeaks) trackPeaks[tr] = pk;
    }
}

// ================================================================== render
namespace {
// renders [from, to) in blocks; sink(mixL, mixR, stems(L,R per audible track), position, n). mix = sum of the stems in track order.
bool renderBlocks(const Snapshot& snap, juce::int64 from, juce::int64 to, const std::vector<int>& tracks, bool wantStems, const std::atomic<bool>* cancel,
                  std::function<void(float)> progress, std::function<bool(const float*, const float*, const std::vector<juce::AudioBuffer<float>>&, juce::int64, int)> sink) {
    constexpr int B = 16384; juce::AudioBuffer<float> mix(2, B); std::vector<juce::AudioBuffer<float>> st(wantStems ? tracks.size() : 0); juce::AudioBuffer<float> tmp(2, B);
    for (auto& b : st) b.setSize(2, B);
    for (juce::int64 p = from; p < to; p += B) {
        if (cancelled(cancel)) return false;
        const int n = (int) std::min<juce::int64>(B, to - p); mix.clear();
        for (size_t i = 0; i < tracks.size(); ++i) {
            auto& b = wantStems ? st[i] : tmp; b.clear();
            renderItems(snap, b.getWritePointer(0), b.getWritePointer(1), p, n, tracks[i]);
            float* ml = mix.getWritePointer(0); float* mr = mix.getWritePointer(1); const float* bl = b.getReadPointer(0); const float* br = b.getReadPointer(1);
            for (int k = 0; k < n; ++k) { ml[k] += bl[k]; mr[k] += br[k]; }
        }
        if (!sink(mix.getReadPointer(0), mix.getReadPointer(1), st, p - from, n)) return false;
        if (progress) progress(0.5f + 0.5f * (float) (p + n - from) / (float) std::max<juce::int64>(1, to - from));
    }
    return true;
}
struct Prepared { std::unique_ptr<Snapshot> snap; juce::int64 from = 0, to = 0; std::vector<int> tracks; };
Prepared prepareRender(const Arrangement& a, double sr, Cache& cache, bool loopOnly, double tail, const std::atomic<bool>* cancel, std::function<void(float)> progress) {
    Prepared p; p.snap = buildSnapshot(a, sr, cache, {}, cancel, [&](float x) { if (progress) progress(x * 0.5f); });
    if (!p.snap) return p;
    if (loopOnly) { p.from = p.snap->loopStart; p.to = p.snap->loopEnd; }
    else { p.from = 0; p.to = p.snap->endSample + (juce::int64) (clampF(tail, 0.0, 60.0, 0.0) * sr); }
    p.to = std::min(p.to, p.from + (juce::int64) (kMaxRenderSeconds * sr));
    for (int t = 0; t < (int) a.tracks.size(); ++t) if (t < (int) p.snap->tracks.size() && p.snap->tracks[(size_t) t].used) p.tracks.push_back(t);   // empty / silent tracks give no stem
    return p;
}
}

std::shared_ptr<RenderResult> renderArrangement(const Arrangement& a, double sr, Cache& cache, bool stems, double tail, const std::atomic<bool>* cancel, std::function<void(float)> progress, bool loopOnly) {
    auto r = std::make_shared<RenderResult>(); r->sampleRate = sr; r->bpm = a.bpm; r->loopOnly = loopOnly;
    try {
        auto P = prepareRender(a, sr, cache, loopOnly, tail, cancel, progress);
        if (!P.snap) { r->cancelled = true; return r; }
        const int N = (int) std::max<juce::int64>(1, std::min<juce::int64>(P.to - P.from, (juce::int64) (20 * 60 * sr)));   // in memory: 20 min at most
        r->mix.setSize(2, N); r->mix.clear(); r->fromBeat = P.from / P.snap->samplesPerBeat(); r->toBeat = (P.from + N) / P.snap->samplesPerBeat();
        if (stems) for (int t : P.tracks) { r->stems.emplace_back(2, N); r->stems.back().clear(); r->stemNames.add(a.tracks[(size_t) t].name); }
        const bool ok = renderBlocks(*P.snap, P.from, P.from + N, P.tracks, stems, cancel, progress, [&](const float* ml, const float* mr, const std::vector<juce::AudioBuffer<float>>& st, juce::int64 pos, int n) {
            r->mix.copyFrom(0, (int) pos, ml, n); r->mix.copyFrom(1, (int) pos, mr, n);
            for (size_t i = 0; i < st.size(); ++i) for (int c = 0; c < 2; ++c) r->stems[i].copyFrom(c, (int) pos, st[i], c, 0, n);
            return true; });
        if (!ok) r->cancelled = true;
        r->peak = r->mix.getMagnitude(0, r->mix.getNumSamples());
    } catch (const std::bad_alloc&) { r->failed = true; r->error = "Not enough memory to render this arrangement."; r->mix.setSize(2, 1); r->stems.clear(); }
    return r;
}
std::shared_ptr<RenderResult> renderToFiles(const Arrangement& a, double sr, Cache& cache, const RenderRequest& req, const std::atomic<bool>* cancel, std::function<void(float)> progress) {
    auto r = std::make_shared<RenderResult>(); r->sampleRate = sr; r->bpm = a.bpm; r->loopOnly = req.loopOnly;
    try {
        auto P = prepareRender(a, sr, cache, req.loopOnly, req.tailSeconds, cancel, progress);
        if (!P.snap) { r->cancelled = true; return r; }
        r->fromBeat = P.from / P.snap->samplesPerBeat(); r->toBeat = P.to / P.snap->samplesPerBeat();
        if (P.to <= P.from) { r->failed = true; r->error = "Nothing to render - the arrangement is empty."; return r; }
        auto dir = req.dir == juce::File() ? Session::dragDir() : req.dir; dir.createDirectory();
        const int bits = req.bits == 16 || req.bits == 24 ? req.bits : 32;
        auto open = [&](const juce::File& f) -> std::unique_ptr<juce::AudioFormatWriter> {
            f.deleteFile(); std::unique_ptr<juce::FileOutputStream> os(f.createOutputStream()); if (!os) return nullptr;
            juce::WavAudioFormat wav; std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(os.get(), sr, 2, bits, {}, 0)); if (w) os.release(); return w; };   // GATE: via-caller (ArrangePage asked spec.arrange.*: render / drags / auto-mix / send-beats)
        std::unique_ptr<juce::AudioFormatWriter> mixW; std::vector<std::unique_ptr<juce::AudioFormatWriter>> stemW;
        if (req.mix) { r->mixFile = Session::uniqueFile(dir, req.base + (req.loopOnly ? " loop" : ""), ".wav"); mixW = open(r->mixFile); if (!mixW) { r->failed = true; r->error = "Can't write " + r->mixFile.getFullPathName(); return r; } }
        if (req.stems) for (size_t i = 0; i < P.tracks.size(); ++i) {
            auto f = Session::uniqueFile(dir, req.base + (req.loopOnly ? " loop " : " ") + juce::String((int) i + 1).paddedLeft('0', 2) + " " + a.tracks[(size_t) P.tracks[i]].name, ".wav");
            auto w = open(f); if (!w) { r->failed = true; r->error = "Can't write " + f.getFullPathName(); return r; } stemW.push_back(std::move(w)); r->stemFiles.add(f); r->stemNames.add(a.tracks[(size_t) P.tracks[i]].name);
        }
        float peak = 0;
        const bool ok = renderBlocks(*P.snap, P.from, P.to, P.tracks, req.stems, cancel, progress, [&](const float* ml, const float* mr, const std::vector<juce::AudioBuffer<float>>& st, juce::int64, int n) {
            for (int i = 0; i < n; ++i) peak = std::max(peak, std::max(std::abs(ml[i]), std::abs(mr[i])));
            if (mixW) { const float* ch[2] = { ml, mr }; if (!mixW->writeFromFloatArrays(ch, 2, n)) return false; }
            for (size_t i = 0; i < stemW.size(); ++i) { const float* ch[2] = { st[i].getReadPointer(0), st[i].getReadPointer(1) }; if (!stemW[i]->writeFromFloatArrays(ch, 2, n)) return false; }
            return true; });
        mixW.reset(); stemW.clear(); r->peak = peak;
        if (!ok) { r->cancelled = cancelled(cancel); if (!r->cancelled) { r->failed = true; r->error = "Writing the render failed (disk full?)"; } r->mixFile.deleteFile(); for (auto& f : r->stemFiles) f.deleteFile(); }
    } catch (const std::bad_alloc&) { r->failed = true; r->error = "Not enough memory to render this arrangement."; }
    return r;
}

juce::MidiFile arrangementMidi(const Arrangement& a, bool includeInaudible) {
    juce::MidiFile mf; constexpr int ppq = 960; mf.setTicksPerQuarterNote(ppq);
    juce::MidiMessageSequence meta; meta.addEvent(juce::MidiMessage::tempoMetaEvent((int) std::llround(60000000.0 / clampF(a.bpm, 20.0, 400.0, 120.0))), 0);
    meta.addEvent(juce::MidiMessage::timeSignatureMetaEvent(a.beatsPerBar, 4), 0); mf.addTrack(meta);
    for (int ti = 0; ti < (int) a.tracks.size(); ++ti) {
        if (!includeInaudible && !a.trackAudible(ti)) continue;
        const auto& tr = a.tracks[(size_t) ti]; juce::MidiMessageSequence seq; bool any = false;
        seq.addEvent(juce::MidiMessage::textMetaEvent(3, tr.name), 0);
        const int chan = (ti % 15) + 1 + (ti % 15 >= 9 ? 1 : 0);   // skip channel 10 (drums)
        for (auto& c : tr.clips) {
            if (c.mute || !c.noteBased()) continue;
            const double k = juce::jlimit(0.25f, 4.0f, c.stretch) * c.warpRatio(a.bpm) / (c.keepLength ? 1.0 : std::pow(2.0, c.pitch / 12.0));
            const double P = c.processedSeconds(a.bpm), clipSec = a.beatsToSeconds(c.length); const int shift = (int) std::lround(c.pitch);
            if (P <= 0) continue;
            const int c0 = c.loop ? (int) std::floor(c.offset / P) : 0, c1 = c.loop ? std::min(c0 + 4096, (int) std::ceil((c.offset + clipSec) / P)) : 1;
            for (int cyc = c0; cyc < c1; ++cyc)
                for (auto& nt : c.source->notes) {
                    double s0 = nt.start * k, s1 = (nt.start + std::max(0.0, nt.length)) * k;
                    if (c.reverse) { const double r0 = P - s1, r1 = P - s0; s0 = r0; s1 = r1; }
                    s0 += cyc * P - c.offset; s1 = std::min(s1 + cyc * P, (cyc + 1) * P) - c.offset; s0 = std::max(0.0, s0); s1 = std::min(clipSec, s1);
                    if (s1 - s0 <= 1e-6) continue;
                    const double b0 = c.start + a.secondsToBeats(s0), b1 = c.start + a.secondsToBeats(s1);
                    const int pitch = juce::jlimit(0, 127, nt.pitch + shift); const auto vel = (juce::uint8) juce::jlimit(1, 127, (int) std::lround(nt.velocity * 127));
                    seq.addEvent(juce::MidiMessage::noteOn(chan, pitch, vel), std::round(b0 * ppq));
                    seq.addEvent(juce::MidiMessage::noteOff(chan, pitch), std::max(std::round(b0 * ppq) + 1, std::round(b1 * ppq)));
                    any = true;
                }
        }
        if (any) { seq.updateMatchedPairs(); mf.addTrack(seq); }
    }
    return mf;
}
int midiNoteCount(const juce::MidiFile& m) { int n = 0; for (int t = 0; t < m.getNumTracks(); ++t) for (auto* e : *m.getTrack(t)) if (e->message.isNoteOn()) ++n; return n; }

bool writeWav(const juce::File& f, const juce::AudioBuffer<float>& audio, double sr, int bits) {
    f.deleteFile(); std::unique_ptr<juce::FileOutputStream> os(f.createOutputStream()); if (!os) return false;
    juce::WavAudioFormat wav; std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(os.get(), sr, (unsigned) std::max(1, audio.getNumChannels()), bits == 16 || bits == 24 ? bits : 32, {}, 0));   // GATE: via-caller (ArrangePage asked spec.arrange.*)
    if (!w) return false; os.release(); return w->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples());
}

juce::uint64 audioHash(const Arrangement& a, double sr, double bpm, bool loopOnly) {
    juce::MemoryOutputStream m; m.writeDouble(sr); m.writeDouble(bpm); m.writeBool(loopOnly); if (loopOnly) { m.writeDouble(a.loopStart); m.writeDouble(a.loopEnd); }
    auto env = [&](const Envelope& e) { m.writeInt((int) e.size()); for (auto& p : e) { m.writeDouble(p.beat); m.writeFloat(p.value); } };
    for (int ti = 0; ti < (int) a.tracks.size(); ++ti) {
        const auto& t = a.tracks[(size_t) ti]; m.writeBool(a.trackAudible(ti)); if (!a.trackAudible(ti)) continue;
        m.writeFloat(t.gainDb); m.writeFloat(t.pan); env(t.volEnv); env(t.panEnv); m.writeString(t.name);
        for (auto& c : t.clips) {
            if (c.mute) continue;
            m.writeInt64((juce::int64) (c.source ? c.source->uid : 0)); m.writeDouble(c.start); m.writeDouble(c.length); m.writeDouble(c.offset); m.writeFloat(c.pitch); m.writeBool(c.keepLength);
            m.writeFloat(c.stretch); m.writeBool(c.warp); m.writeBool(c.loop); m.writeBool(c.reverse); m.writeDouble(c.fadeIn); m.writeDouble(c.fadeOut); m.writeInt((int) c.fadeInCurve); m.writeInt((int) c.fadeOutCurve);
            m.writeFloat(c.gainDb); m.writeFloat(c.pan); env(c.gainEnv); m.writeBool(c.morph); m.writeInt((int) c.morphShape); m.writeFloat(c.morphAmount); m.writeFloat(c.morphSmear);
        }
    }
    juce::uint64 h = 1469598103934665603ULL; auto* d = static_cast<const juce::uint8*>(m.getData());
    for (size_t i = 0; i < m.getDataSize(); ++i) { h ^= d[i]; h *= 1099511628211ULL; }
    return h;
}

HostPos hostPos(juce::AudioPlayHead* ph, bool nonRealtime) {
    HostPos h; h.nonRealtime = nonRealtime; if (!ph) return h; const auto pos = ph->getPosition(); if (!pos.hasValue()) return h;
    h.valid = true; h.playing = pos->getIsPlaying(); h.looping = pos->getIsLooping();
    if (auto t = pos->getTimeInSamples()) h.timeSamples = *t;
    if (auto b = pos->getBpm()) h.bpm = *b;
    if (auto q = pos->getPpqPosition()) if (std::isfinite(*q)) { h.hasPpq = true; h.ppq = *q; }
    return h;
}

// ================================================================== session
namespace { juce::File& sharedOverride() { static juce::File f; return f; } }
Session::Session() {
    auto reload = [](const SourceRef& r) -> PayloadPtr { juce::File f(juce::String::fromUTF8((const char*) r.state.getData(), (int) r.state.getSize())); return f.existsAsFile() ? Session::loadSound(f, r.page) : nullptr; };
    fileReg = registry.add("File", reload); sharedReg = registry.add("Shared", reload);
    scratchN = 4096; scratch.assign((size_t) scratchN * 4, 0.0f);
    builder = std::thread([this] { builderLoop(); }); worker = std::thread([this] { workerLoop(); });
    changed();
    // drag-out files from earlier sessions: delete what is more than a day old
    post([] { const auto cut = juce::Time::getCurrentTime() - juce::RelativeTime::days(1);
        for (auto& f : dragDir().findChildFiles(juce::File::findFilesAndDirectories, false)) if (f.getLastModificationTime() < cut) { if (f.isDirectory()) f.deleteRecursively(); else f.deleteFile(); } });
}
Session::~Session() {
    alive->store(false); renderCancel.store(true);
    if (renderThread.joinable()) renderThread.join();
    { const std::lock_guard<std::mutex> l(wm); wquit = true; } wcv.notify_all(); if (worker.joinable()) worker.join();
    { const std::lock_guard<std::mutex> l(bm); quit = true; } bcv.notify_all(); if (builder.joinable()) builder.join();
    delete live.exchange(nullptr); for (auto& r : retired) delete r.first; retired.clear();
}
double Session::effectiveBpm() const { const double h = hostBpm.load(); return followHostTempo.load() && h >= 20 && h <= 400 ? h : clampF(arrangement.bpm, 20.0, 400.0, 120.0); }
void Session::changed() {
    auto copy = std::make_shared<Arrangement>(arrangement);
    { const std::lock_guard<std::mutex> l(stateM); committed = copy; }
    { const std::lock_guard<std::mutex> l(bm); pending = copy; requested.fetch_add(1); }
    bcv.notify_all();
}
std::shared_ptr<Arrangement> Session::latestArrangement() { const std::lock_guard<std::mutex> l(bm); return pending ? pending : last; }
bool Session::upToDate() const { return built.load() >= requested.load(); }
bool Session::waitUntilUpToDate(int ms) const {
    const auto until = juce::Time::getMillisecondCounterHiRes() + ms;
    while (!upToDate()) { if (juce::Time::getMillisecondCounterHiRes() > until) return false; std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
    return true;
}
void Session::builderLoop() {
    double lastRate = 0, lastBpm = 0;
    while (true) {
        std::shared_ptr<Arrangement> job; juce::uint64 gen = 0; bool retime = false;
        {
            std::unique_lock<std::mutex> l(bm);
            bcv.wait_for(l, std::chrono::milliseconds(30), [this] { return quit || pending != nullptr; });
            if (quit) return;
            if (pending) { job = pending; pending.reset(); last = job; gen = requested.load(); }
            else if (last) {   // host tempo / sample rate moved: re-time the last arrangement
                const double h = hostBpm.load(), bpm = followHostTempo.load() && h >= 20 && h <= 400 ? h : last->bpm;
                if (std::abs(bpm - lastBpm) > 1e-6 || engineRate.load() != lastRate) { job = last; gen = built.load(); retime = true; }
            }
        }
        if (!job) continue;
        try {
            Arrangement a = *job; const double h = hostBpm.load(); if (followHostTempo.load() && h >= 20 && h <= 400) a.bpm = h;
            lastRate = engineRate.load(); lastBpm = a.bpm;
            cache.beginPass();
            BuildOptions fast; fast.allowStale = true;   // first publish what can play now (stand-ins for sounds being re-processed)...
            auto s = buildSnapshot(a, lastRate, cache, fast);
            if (s && s->stale) { s->generation = gen; publish(std::move(s)); s = buildSnapshot(a, lastRate, cache); }   // ...then the exact version
            if (s) { s->generation = gen; publish(std::move(s)); }
            cache.prune();
        } catch (const std::bad_alloc&) { cache.prune(0, 0); }   // out of memory: drop the cache, keep the last snapshot playing
        juce::ignoreUnused(retime);
        if (gen > built.load()) built.store(gen);
    }
}
bool Session::publish(std::unique_ptr<Snapshot> s) {
    if (!s || s->sampleRate != engineRate.load()) return false;   // a build at the old rate finished after prepare(): drop it
    const std::lock_guard<std::mutex> l(retireLock);
    if (auto* old = live.exchange(s.release())) retired.push_back({ old, audioEpoch.load() });
    for (auto it = retired.begin(); it != retired.end();) {
        if (inProcess.load() == 0 || audioEpoch.load() > it->second) { delete it->first; it = retired.erase(it); } else ++it;
    }
    return true;
}
void Session::housekeeping() {
    { const std::lock_guard<std::mutex> l(retireLock);
      for (auto it = retired.begin(); it != retired.end();) { if (inProcess.load() == 0 || audioEpoch.load() > it->second) { delete it->first; it = retired.erase(it); } else ++it; } }
    applyPendingState();
}
void Session::prepare(double sr, int maxBlock) {
    if (maxBlock > scratchN) { scratchN = maxBlock; scratch.assign((size_t) scratchN * 4, 0.0f); }
    if (!(sr > 1000) || !std::isfinite(sr)) return;
    const double old = engineRate.load(); if (sr == old) return;
    const auto scale = [&](juce::int64 v) { return (juce::int64) std::llround((double) v * sr / old); };
    playhead.store(scale(playhead.load())); heard.store(scale(heard.load()));
    engineRate.store(sr);
    // build the new-rate snapshot right here (prepareToPlay may block), so the first block after it already plays
    if (auto a = latestArrangement()) {
        const auto gen = requested.load();
        try { Arrangement b = *a; b.bpm = effectiveBpm(); cache.beginPass(); if (auto s = buildSnapshot(b, sr, cache)) { s->generation = gen; publish(std::move(s)); } } catch (const std::bad_alloc&) {}
    }
    bcv.notify_all();
}
void Session::release() {}
void Session::process(juce::AudioBuffer<float>& out, const HostPos& host) {
    inProcess.store(1); audioEpoch.fetch_add(1);
    const Snapshot* s = live.load();
    if (host.valid && host.bpm >= 20 && host.bpm <= 400) hostBpm.store(host.bpm);
    const bool hp = host.valid && host.playing;
    if (hp && !hostWasPlaying) playing.store(false);   // the host took over: the internal PLAY stops
    hostWasPlaying = hp; hostIsPlaying.store(hp);
    if (host.nonRealtime && (!s || s->sampleRate != engineRate.load() || !upToDate() || s->stale)) {   // offline bounce: wait for the exact arrangement
        for (int i = 0; i < 20000 && (!upToDate() || !live.load() || live.load()->sampleRate != engineRate.load()); ++i) std::this_thread::sleep_for(std::chrono::microseconds(500));
        s = live.load();
    }
    { const juce::int64 sk = seekReq.exchange(-1); if (sk >= 0) playhead.store(sk); }
    const int n = out.getNumSamples(), nc = out.getNumChannels();
    const juce::int64 hostT = !hp ? 0 : host.hasPpq ? (juce::int64) std::llround(host.ppq * (s ? s->samplesPerBeat() : engineRate.load() * 60.0 / effectiveBpm())) : host.timeSamples;
    if (hp) { playhead.store(hostT + n); heard.store(hostT + n); }   // the playhead follows the host (also before the first snapshot)
    if (s && n > 0 && nc > 0 && s->sampleRate == engineRate.load() && scratchN > 0) {
        float* L = out.getWritePointer(0); float* R = nc > 1 ? out.getWritePointer(1) : L;
        float* sL = scratch.data(); float* sR = sL + scratchN; float* mL = sR + scratchN; float* mR = mL + scratchN;
        float peaks[kMaxTracks]; float master = 0; std::array<float, kMaxTracks> tp {};
        auto block = [&](juce::int64 t, int off, int m) {   // renders m samples of timeline t into out[off..]
            std::fill(mL, mL + m, 0.0f); std::fill(mR, mR + m, 0.0f);
            renderMix(*s, mL, mR, t, m, sL, sR, peaks);
            for (int i = 0; i < std::min(kMaxTracks, (int) s->tracks.size()); ++i) tp[(size_t) i] = std::max(tp[(size_t) i], peaks[i]);
            for (int i = 0; i < m; ++i) { master = std::max(master, std::max(std::abs(mL[i]), std::abs(mR[i]))); if (L == R) L[off + i] += 0.5f * (mL[i] + mR[i]); else { L[off + i] += mL[i]; R[off + i] += mR[i]; } }
        };
        if (hp) {   // follow the host's timeline in beats (tempo changes stay in sync); the ARRANGE loop doesn't apply
            const juce::int64 t = hostT;
            for (int done = 0; done < n;) { const int m = std::min(scratchN, n - done); block(t + done, done, m); done += m; }
        } else if (playing.load()) {
            juce::int64 p = playhead.load();
            for (int done = 0; done < n;) {
                int m = std::min(scratchN, n - done);
                if (s->loopOn && p < s->loopEnd) m = (int) std::min<juce::int64>(m, s->loopEnd - p);
                block(p, done, m); p += m; done += m;
                if (s->loopOn && p == s->loopEnd) p = s->loopStart;
            }
            playhead.store(p); heard.store(p);
        }
        for (int i = 0; i < kMaxTracks; ++i) { float cur = trackPeaks[(size_t) i].load(); while (tp[(size_t) i] > cur && !trackPeaks[(size_t) i].compare_exchange_weak(cur, tp[(size_t) i])) {} }
        { float cur = masterPeak.load(); while (master > cur && !masterPeak.compare_exchange_weak(cur, master)) {} }
    }
    inProcess.store(0);
}
float Session::takeTrackPeak(int t) { return t >= 0 && t < kMaxTracks ? trackPeaks[(size_t) t].exchange(0.0f) : 0.0f; }
float Session::takeMasterPeak() { return masterPeak.exchange(0.0f); }
void Session::play() { playing.store(true); }
void Session::stop() { playing.store(false); }
void Session::setPlayheadBeats(double beats) {
    const double sr = engineRate.load(), bpm = effectiveBpm(); const auto p = (juce::int64) std::llround(clampF(beats, 0.0, kMaxBeats, 0.0) * sr * 60.0 / bpm);
    if (playing.load()) seekReq.store(p); else playhead.store(p);   // while playing, the audio thread applies the seek (no lost update)
    heard.store(p);
}
double Session::playheadBeats() const { return (double) heard.load() / (engineRate.load() * 60.0 / effectiveBpm()); }

bool Session::undo() { if (!history.undo(arrangement)) return false; if (followHostTempo.load() && hostTempoKnown()) arrangement.bpm = effectiveBpm(); changed(); return true; }
bool Session::redo() { if (!history.redo(arrangement)) return false; if (followHostTempo.load() && hostTempoKnown()) arrangement.bpm = effectiveBpm(); changed(); return true; }

juce::uint32 Session::receive(PayloadPtr p, int track, double atBeats) {
    if (!p || !p->valid()) return 0;
    const Arrangement before = arrangement;
    if (track < 0) { int ti = -1; if (arrangement.findClip(selected, &ti)) track = ti; }
    if (track < 0) { track = (int) arrangement.tracks.size(); for (int i = 0; i < (int) arrangement.tracks.size(); ++i) if (arrangement.tracks[(size_t) i].clips.empty()) { track = i; break; } }
    track = juce::jlimit(0, kMaxTracks - 1, track);
    if (atBeats < 0 || !std::isfinite(atBeats)) {
        atBeats = arrangement.snapDown(playheadBeats());
        if (track < (int) arrangement.tracks.size()) for (auto& c : arrangement.tracks[(size_t) track].clips) if (c.start < atBeats + kMinClipBeats && c.end() > atBeats) atBeats = arrangement.snap(c.end());
    }
    arrangement.bpm = effectiveBpm();
    auto* c = arrangement.addClip(track, p, atBeats); if (!c) return 0;
    selected = c->id; selection = { selected }; commit(before); return selected;
}
bool Session::canRerender(juce::uint32 id) {
    auto* c = arrangement.findClip(id); return c != nullptr && c->source && c->source->source.page.isNotEmpty() && registry.has(c->source->source.page);
}
bool Session::rerenderClip(juce::uint32 id) {
    auto* c = arrangement.findClip(id); if (!c || !c->source) return false;
    auto fresh = rerender(c->source->source); if (!fresh || !fresh->valid()) return false;
    c = arrangement.findClip(id); if (!c) return false;   // the page may have changed things while rendering
    const Arrangement before = arrangement; const auto old = c->source;
    for (auto& t : arrangement.tracks) for (auto& k : t.clips) if (k.source == old) k.source = fresh;   // every duplicate follows
    commit(before); return true;
}
juce::uint32 Session::bounce(const std::set<juce::uint32>& ids, bool replace) {
    if (ids.empty()) return 0;
    Arrangement a = arrangement; a.bpm = effectiveBpm(); const double sr = engineRate.load();
    double b0 = 1e300, b1 = -1; int topTrack = kMaxTracks; juce::String name;
    for (int ti = 0; ti < (int) a.tracks.size(); ++ti) for (auto& c : a.tracks[(size_t) ti].clips) if (ids.count(c.id)) { b0 = std::min(b0, c.start); b1 = std::max(b1, c.end()); if (ti < topTrack) { topTrack = ti; name = c.name; } }
    if (b1 <= b0) return 0;
    BuildOptions o; o.onlyClips = &ids; o.clipLevelOnly = true; o.includeInaudible = true;
    auto s = buildSnapshot(a, sr, cache, o); if (!s) return 0;
    const juce::int64 S = (juce::int64) std::llround(b0 * s->samplesPerBeat()), E = (juce::int64) std::llround(b1 * s->samplesPerBeat());
    if (E - S > (juce::int64) (kMaxSoundSeconds * sr)) return 0;
    juce::AudioBuffer<float> buf(2, (int) std::max<juce::int64>(1, E - S)); buf.clear(); renderItems(*s, buf.getWritePointer(0), buf.getWritePointer(1), S, buf.getNumSamples());
    auto p = makePayload(buf, sr, (ids.size() > 1 ? "bounce " : "") + name, {}, { "Bounce", name, {} }, a.bpm);
    const Arrangement before = arrangement;
    if (replace) for (auto id : ids) arrangement.removeClip(id);
    arrangement.bpm = a.bpm;
    auto* c = arrangement.addClip(topTrack, p, b0, b1 - b0); if (!c) { arrangement = before; return 0; }
    selected = c->id; selection = { selected }; commit(before); return selected;
}

void Session::startRender(bool stems, std::function<void(std::shared_ptr<RenderResult>)> done) { RenderRequest r; r.stems = stems; startRender(r, std::move(done)); }
void Session::startRender(const RenderRequest& req, std::function<void(std::shared_ptr<RenderResult>)> done) {
    if (renderBusy.load()) return;
    if (renderThread.joinable()) renderThread.join();
    Arrangement a = arrangement; a.bpm = effectiveBpm(); const auto rev = audioHash(a, engineRate.load(), a.bpm, req.loopOnly);
    renderBusy.store(true); renderCancel.store(false); renderProg.store(0);
    auto alv = alive; RenderRequest rq = req; if (rq.dir == juce::File()) rq.dir = dragDir().getChildFile(juce::String(juce::Time::currentTimeMillis()) + "-" + juce::String((int) (rev & 0xffff)));
    renderThread = std::thread([this, a, rq, done, alv, rev] {
        std::shared_ptr<RenderResult> r;
        try { r = renderToFiles(a, engineRate.load(), cache, rq, &renderCancel, [this](float p) { renderProg.store(p); }); }
        catch (...) { r = std::make_shared<RenderResult>(); r->failed = true; r->error = "The render failed (out of memory?)"; }
        r->revision = rev; renderProg.store(1.0f); renderBusy.store(false);
        juce::MessageManager::callAsync([this, r, done, alv, rq] { if (!alv->load()) return;
            if (!r->cancelled && !r->failed) { if (rq.mix) lastRender = r; if (rq.stems) lastStems = r; }
            if (done) done(r); });
    });
}

juce::File Session::sharedSoundsDir() {
    auto d = sharedOverride() != juce::File() ? sharedOverride() : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Ghosty").getChildFile("Shared Sounds");
    d.createDirectory(); return d;
}
void Session::setSharedSoundsDir(const juce::File& dir) { sharedOverride() = dir; }
juce::File Session::dragDir() { auto d = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("Ghosty Arrange"); d.createDirectory(); return d; }
juce::File Session::uniqueFile(const juce::File& dir, const juce::String& base, const juce::String& ext) {
    auto name = juce::File::createLegalFileName(base).trim(); if (name.isEmpty()) name = "Arrange";
    auto f = dir.getChildFile(name + ext); return f.exists() ? f.getNonexistentSibling(true) : f;
}
juce::Array<juce::File> Session::listShared() {
    std::vector<std::pair<juce::Time, juce::File>> v;
    for (auto& f : sharedSoundsDir().findChildFiles(juce::File::findFiles, false, "*"))
        if (f.hasFileExtension("wav;aif;aiff;flac;ogg") && !f.getFileName().startsWithChar('.')) v.push_back({ f.getLastModificationTime(), f });   // any case of extension
    std::sort(v.begin(), v.end(), [](const auto& x, const auto& y) { return x.first > y.first; });
    juce::Array<juce::File> out; for (auto& x : v) out.add(x.second); return out;
}
PayloadPtr Session::loadSound(const juce::File& f, const juce::String& page, juce::String* note) {
    try {
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(f)); if (!r || r->lengthInSamples <= 0 || !(r->sampleRate >= 8000 && r->sampleRate <= 384000)) return nullptr;
        const juce::int64 cap = (juce::int64) (r->sampleRate * kMaxSoundSeconds);
        const int n = (int) std::min<juce::int64>(r->lengthInSamples, cap);
        if (r->lengthInSamples > cap && note) *note = f.getFileName() + " is longer than 20 minutes - only the first 20 minutes were added.";
        juce::AudioBuffer<float> b((int) std::min(2u, std::max(1u, r->numChannels)), n); r->read(&b, 0, n, 0, true, b.getNumChannels() > 1);
        SourceRef src; src.page = page; src.name = f.getFileNameWithoutExtension(); src.state.append(f.getFullPathName().toRawUTF8(), f.getFullPathName().getNumBytesAsUTF8());
        return makePayload(b, r->sampleRate, f.getFileNameWithoutExtension(), {}, src);
    } catch (const std::bad_alloc&) { if (note) *note = "Not enough memory to load " + f.getFileName(); return nullptr; }
}
juce::Array<juce::File> Session::writeAudio(const RenderResult& r, const juce::File& dir, const juce::String& base, bool stems) const {
    juce::Array<juce::File> out; dir.createDirectory();
    if (!stems) {
        if (r.mixFile.existsAsFile()) { auto f = uniqueFile(dir, base, ".wav"); if (r.mixFile.copyFileTo(f)) out.add(f); return out; }
        auto f = uniqueFile(dir, base, ".wav"); if (writeWav(f, r.mix, r.sampleRate)) out.add(f); return out;
    }
    if (!r.stemFiles.isEmpty()) { for (int i = 0; i < r.stemFiles.size(); ++i) { auto f = uniqueFile(dir, base + " " + juce::String(i + 1).paddedLeft('0', 2) + " " + r.stemNames[i], ".wav"); if (r.stemFiles[i].copyFileTo(f)) out.add(f); } return out; }
    for (size_t i = 0; i < r.stems.size(); ++i) { auto f = uniqueFile(dir, base + " " + juce::String((int) i + 1).paddedLeft('0', 2) + " " + r.stemNames[(int) i], ".wav"); if (writeWav(f, r.stems[i], r.sampleRate)) out.add(f); }
    return out;
}
juce::File Session::writeMidi(const juce::File& dir, const juce::String& base) const {
    Arrangement a = arrangement; a.bpm = effectiveBpm(); auto m = arrangementMidi(a); if (midiNoteCount(m) == 0) return {};
    dir.createDirectory(); auto f = uniqueFile(dir, base, ".mid"); f.deleteFile(); juce::FileOutputStream os(f); if (!os.openedOk()) return {};
    m.writeTo(os); os.flush(); return f;   // GATE: via-caller (ArrangePage asked spec.arrange.midi-drag)
}
namespace {
juce::Array<juce::File> sendNow(Arrangement a, juce::uint32 clipId, double sr, Cache& cache, juce::String& error) {
    juce::Array<juce::File> out; const auto d = Session::sharedSoundsDir(); if (!d.isDirectory()) { error = "Can't create the folder " + d.getFullPathName(); return out; }
    const auto stamp = juce::Time::getCurrentTime().formatted("%Y-%m-%d %H%M%S");
    int ti = -1; const Clip* c = a.findClip(clipId, &ti);
    if (c) {   // the clip as heard on its own: processed, faded, enveloped, clip gain / pan; not the track's level
        BuildOptions o; o.onlyClip = c->id; o.onlyClipTrack = ti; o.clipLevelOnly = true; o.includeInaudible = true; auto s = buildSnapshot(a, sr, cache, o);
        const juce::int64 S = (juce::int64) std::llround(c->start * s->samplesPerBeat()), E = (juce::int64) std::llround(c->end() * s->samplesPerBeat());
        juce::AudioBuffer<float> b(2, (int) std::max<juce::int64>(1, std::min<juce::int64>(E - S, (juce::int64) (kMaxSoundSeconds * sr)))); b.clear();
        renderItems(*s, b.getWritePointer(0), b.getWritePointer(1), S, b.getNumSamples());
        auto f = Session::uniqueFile(d, "Arrange " + c->name + " " + stamp, ".wav"); if (writeWav(f, b, sr)) out.add(f); else error = "Can't write " + f.getFileName() + " (disk full or no permission?)";
        return out;
    }
    RenderRequest rq; rq.dir = d; rq.base = "Arrange mix " + stamp; auto r = renderToFiles(a, sr, cache, rq);
    if (r->failed || !r->mixFile.existsAsFile()) { error = r->error.isNotEmpty() ? r->error : "Nothing to send - the arrangement is empty."; return out; }
    out.add(r->mixFile); return out;
}
}
juce::Array<juce::File> Session::sendToBeats(juce::uint32 clipId, juce::String& error) {
    Arrangement a = arrangement; a.bpm = effectiveBpm(); try { return sendNow(a, clipId, engineRate.load(), cache, error); } catch (const std::bad_alloc&) { error = "Not enough memory."; return {}; }
}
void Session::post(std::function<void()> job) { { const std::lock_guard<std::mutex> l(wm); jobs.push_back(std::move(job)); } wcv.notify_all(); }
void Session::workerLoop() {
    while (true) {
        std::function<void()> job;
        { std::unique_lock<std::mutex> l(wm); wcv.wait(l, [this] { return wquit || !jobs.empty(); }); if (wquit) return; job = std::move(jobs.front()); jobs.pop_front(); }
        try { job(); } catch (...) {}
    }
}
void Session::sendToBeatsAsync(juce::uint32 clipId, std::function<void(juce::Array<juce::File>, juce::String)> done) {
    Arrangement a = arrangement; a.bpm = effectiveBpm(); const double sr = engineRate.load(); auto alv = alive;
    post([this, a, clipId, sr, done, alv] { juce::String err; juce::Array<juce::File> f;
        try { f = sendNow(a, clipId, sr, cache, err); } catch (const std::bad_alloc&) { err = "Not enough memory."; }
        juce::MessageManager::callAsync([f, err, done, alv] { if (alv->load() && done) done(f, err); }); });
}
void Session::loadSoundsAsync(const juce::StringArray& files, const juce::String& page, std::function<void(std::vector<PayloadPtr>, juce::StringArray)> done) {
    auto alv = alive;
    post([files, page, done, alv] { std::vector<PayloadPtr> ps; juce::StringArray notes;
        for (auto& path : files) { if (!alv->load()) return; juce::String note; auto p = loadSound(juce::File(path), page, &note); if (note.isNotEmpty()) notes.add(note); if (p) ps.push_back(p); else if (note.isEmpty()) notes.add("Can't read " + juce::File(path).getFileName()); }
        juce::MessageManager::callAsync([ps, notes, done, alv] { if (alv->load() && done) done(ps, notes); }); });
}

juce::MemoryBlock Session::getState(bool keepRendered, int* stripped) const {
    std::shared_ptr<const Arrangement> a; { const std::lock_guard<std::mutex> l(stateM); a = committed; }
    if (!a) return saveState(arrangement, keepRendered, stripped);
    return saveState(*a, keepRendered, stripped);
}
bool Session::setState(const void* data, size_t size, juce::String* error) {
    auto a = std::make_unique<Arrangement>(); if (!loadState(data, size, *a, error)) return false;
    auto* mm = juce::MessageManager::getInstanceWithoutCreating();
    if (mm == nullptr || mm->isThisTheMessageThread()) {
        arrangement = std::move(*a); history.clear(); selected = 0; selection.clear(); changed(); return true;
    }
    // a host thread: the audio follows at once, the document is swapped on the message thread
    auto copy = std::make_shared<Arrangement>(*a);
    { const std::lock_guard<std::mutex> l(stateM); committed = copy; pendingState = std::move(a); }
    { const std::lock_guard<std::mutex> l(bm); pending = copy; requested.fetch_add(1); } bcv.notify_all();
    auto alv = alive; juce::MessageManager::callAsync([this, alv] { if (alv->load()) applyPendingState(); });
    return true;
}
bool Session::applyPendingState() {
    std::unique_ptr<Arrangement> p; { const std::lock_guard<std::mutex> l(stateM); p = std::move(pendingState); }
    if (!p) return false;
    arrangement = std::move(*p); history.clear(); selected = 0; selection.clear(); changed(); return true;
}

} // namespace arr
