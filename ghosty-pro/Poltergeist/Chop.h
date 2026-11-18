#pragma once
// VOCAL CHOP: drop a vocal, it is cut at its syllables (spectral flux + energy onsets), each slice sits on its own key.
// Every slice can be transposed (Signalsmith Stretch, formants kept or "chipmunk"), reversed, faded, looped and
// gained; TUNE TO KEY snaps each slice's detected pitch (YIN) to the song key; CHOP MELODY writes a pattern from the
// slices; BOUNCE prints the whole set back into a new sample (slice lines kept) so processing can be stacked.
// All rendering runs offline (background thread); the audio thread only plays finished buffers (ChopKit).
#include "Lab.h"
#include "MidiFx.h"
#include <juce_dsp/juce_dsp.h>
#include <random>

namespace gs {

// ---- onsets ------------------------------------------------------------------------------------------------
// Returns slice starts (samples, ascending). sens 0..1 (higher = more slices). want = 0 for AUTO, else the slice count.
inline std::vector<int> detectOnsets(const float* x, int n, double rate, float sens, int want, double minSec = 0.06) {
    std::vector<int> out;
    if (n < 64) { out.push_back(0); return out; }
    constexpr int order = 10, N = 1 << order;
    const int H = juce::jmax(64, (int) std::round(rate * 0.0029));   // ~2.9 ms hop
    juce::dsp::FFT fft(order);
    std::vector<float> win(N), buf(2 * N), prev(N / 2 + 1, 0.0f);
    for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * i / N);
    const int frames = juce::jmax(1, (n + H - 1) / H);
    std::vector<float> flux((size_t) frames, 0.0f), erise((size_t) frames, 0.0f), energy((size_t) frames, 0.0f);
    float prevE = 0;
    for (int f = 0; f < frames; ++f) {
        const int c = f * H, a = c - N / 2;   // frame centred on c
        std::fill(buf.begin(), buf.end(), 0.0f);
        double e = 0;
        for (int i = 0; i < N; ++i) { const int k = a + i; const float v = (k >= 0 && k < n) ? x[k] : 0.0f; buf[(size_t) i] = v * win[(size_t) i]; e += (double) v * v; }
        fft.performFrequencyOnlyForwardTransform(buf.data(), true);
        float fl = 0;
        for (int b = 1; b <= N / 2; ++b) { const float m = std::log1p(100.0f * buf[(size_t) b]); fl += juce::jmax(0.0f, m - prev[(size_t) b]); prev[(size_t) b] = m; }
        flux[(size_t) f] = fl; energy[(size_t) f] = (float) (e / N);
        const float le = std::log10(1e-7f + energy[(size_t) f]);
        erise[(size_t) f] = f == 0 ? 0.0f : juce::jmax(0.0f, le - prevE); prevE = le;
    }
    auto norm = [](std::vector<float>& v) { float m = 1e-9f; for (float a : v) m = juce::jmax(m, a); for (float& a : v) a /= m; };
    norm(flux); norm(erise);
    float emax = 1e-12f; for (float e : energy) emax = juce::jmax(emax, e);
    std::vector<float> nov((size_t) frames);
    for (int f = 0; f < frames; ++f) nov[(size_t) f] = 0.65f * flux[(size_t) f] + 0.35f * erise[(size_t) f];
    // envelope (1 ms) for refining each onset to where the sound actually starts
    const int eh = juce::jmax(1, (int) (rate * 0.001));
    std::vector<float> env((size_t) (n / eh + 1), 0.0f);
    for (int i = 0; i < n; ++i) env[(size_t) (i / eh)] = juce::jmax(env[(size_t) (i / eh)], std::abs(x[i]));
    const float delta = 0.30f - 0.24f * juce::jlimit(0.0f, 1.0f, sens);
    struct Pk { int pos; float s; };
    std::vector<Pk> pk;
    const int W = 12;
    for (int f = 1; f < frames - 1; ++f) {
        const float v = nov[(size_t) f];
        bool isMax = true; double mean = 0; int cnt = 0;
        for (int k = juce::jmax(0, f - W); k <= juce::jmin(frames - 1, f + W); ++k) { if (k != f && std::abs(k - f) <= 4 && nov[(size_t) k] > v) isMax = false; mean += nov[(size_t) k]; ++cnt; }
        if (!isMax || v < mean / cnt + delta) continue;
        // ignore pops in silence: some energy must follow
        float after = 0, before = 0;
        for (int k = f; k < juce::jmin(frames, f + 10); ++k) after = juce::jmax(after, energy[(size_t) k]);
        for (int k = juce::jmax(0, f - 10); k < f - 1; ++k) before = juce::jmax(before, energy[(size_t) k]);
        if (after < emax * 1e-4f) continue;
        // a real syllable start gets louder (or changes sharply); vibrato / decays inside a note don't
        if (!(after > before * 1.3f || flux[(size_t) f] > 0.6f)) continue;
        // refine to where the sound starts: 10 % of the level that follows, on a 5 ms max envelope
        auto E = [&](int k) { float m = 0; for (int j = juce::jmax(0, k - 2); j <= juce::jmin((int) env.size() - 1, k + 2); ++j) m = juce::jmax(m, env[(size_t) j]); return m; };
        int k = juce::jlimit(0, (int) env.size() - 1, f * H / eh);
        float Mx = 0; for (int j = k; j < juce::jmin((int) env.size(), k + (int) (0.03 * rate / eh)); ++j) Mx = juce::jmax(Mx, env[(size_t) j]);
        const float thr = Mx * 0.1f; const int lim = (int) (0.06 * rate / eh);
        if (E(k) >= thr) { int s0 = k; while (k > 0 && s0 - k < lim && E(k - 1) >= thr && E(k - 1) <= E(k) * 1.15f) --k; }
        else { int s0 = k; while (k + 1 < (int) env.size() && k - s0 < lim && E(k) < thr) ++k; }
        pk.push_back({ juce::jlimit(0, n - 1, k * eh), v });
    }
    // strongest first, keep those at least minSec apart
    std::sort(pk.begin(), pk.end(), [](auto& a, auto& b) { return a.s > b.s; });
    const int gap = (int) (minSec * rate);
    std::vector<Pk> keep;
    for (auto& p : pk) {
        bool ok = true; for (auto& q : keep) if (std::abs(q.pos - p.pos) < gap) { ok = false; break; }
        if (ok) keep.push_back(p);
        if ((int) keep.size() >= ChopKit::kMax) break;
    }
    if (want > 0 && (int) keep.size() > want) keep.resize((size_t) want);
    for (auto& p : keep) out.push_back(p.pos);
    std::sort(out.begin(), out.end());
    // the first slice starts at the first onset (leading silence is skipped), or at 0 when that is very close
    if (out.empty() || out.front() < gap) { if (!out.empty()) out.erase(out.begin()); out.insert(out.begin(), 0); }
    // a fixed count with too few onsets: split the longest slices in half
    while (want > 0 && (int) out.size() < want) {
        int bi = 0, bl = 0;
        for (size_t i = 0; i < out.size(); ++i) { const int l = (i + 1 < out.size() ? out[i + 1] : n) - out[i]; if (l > bl) { bl = l; bi = (int) i; } }
        if (bl < 2 * gap) break;
        out.insert(out.begin() + bi + 1, out[(size_t) bi] + bl / 2);
    }
    return out;
}

// ---- pitch (YIN) -------------------------------------------------------------------------------------------
// Median pitch of the voiced frames as a MIDI note (fractional), or -1 when it has no clear pitch.
inline float yinMidi(const float* x, int n, double rate, float* clarity = nullptr) {
    const int Wn = 1024, tauMin = (int) (rate / 1000.0), tauMax = juce::jmin(Wn - 2, (int) (rate / 75.0));
    if (n < Wn + tauMax + 2) return -1.0f;
    float peak = 0; for (int i = 0; i < n; ++i) peak = juce::jmax(peak, std::abs(x[i]));
    const int span = n - Wn - tauMax - 1, frames = juce::jlimit(1, 16, span / 512 + 1);
    std::vector<float> d((size_t) tauMax + 1), notes; float clar = 0;
    for (int f = 0; f < frames; ++f) {
        const int a = frames == 1 ? 0 : (int) ((double) f / (frames - 1) * span);
        float rms = 0; for (int i = 0; i < Wn; ++i) rms += x[a + i] * x[a + i];
        if (std::sqrt(rms / Wn) < peak * 0.08f + 1e-5f) continue;
        d[0] = 1; double run = 0;
        for (int t = 1; t <= tauMax; ++t) {
            double s = 0; const float* p = x + a; const float* q = x + a + t;
            for (int i = 0; i < Wn; ++i) { const float df = p[i] - q[i]; s += (double) df * df; }
            run += s; d[(size_t) t] = (float) (s * t / juce::jmax(1e-12, run));
        }
        int tau = -1;
        for (int t = juce::jmax(2, tauMin); t < tauMax; ++t) if (d[(size_t) t] < 0.15f) { while (t + 1 < tauMax && d[(size_t) t + 1] < d[(size_t) t]) ++t; tau = t; break; }
        if (tau < 0) continue;
        const float y0 = d[(size_t) tau - 1], y1 = d[(size_t) tau], y2 = d[(size_t) tau + 1], den = y0 - 2 * y1 + y2;
        const double tt = tau + (std::abs(den) > 1e-9f ? 0.5 * (y0 - y2) / den : 0.0);
        notes.push_back((float) (69.0 + 12.0 * std::log2(rate / tt / 440.0))); clar += 1.0f - y1;
    }
    if (notes.size() < 2 && frames > 2) return -1.0f;
    if (notes.empty()) return -1.0f;
    if (clarity) *clarity = clar / notes.size();
    std::sort(notes.begin(), notes.end());
    return notes[notes.size() / 2];
}

// ---- the chop -------------------------------------------------------------------------------------------------
struct ChopSlice {
    int start = 0;                 // sample in the chop audio (a slice ends where the next one starts)
    float pitch = 0;               // semitones (fractional = cents), -24..+24
    float gainDb = 0, fadeIn = 2, fadeOut = 8;   // dB, ms
    bool reverse = false, loop = false, formant = true;   // formant = keep the voice's formants (off = chipmunk)
    float det = -2;                // detected pitch (MIDI), -1 none, -2 not measured yet
};
struct ChopStep { bool on = false, lock = false; int slice = 0; float vel = 0.85f; };

struct ChopState {
    std::shared_ptr<const SampleData> audio;
    std::vector<ChopSlice> slices;
    int slicesMode = 2;            // 0 AUTO, 1:4, 2:5, 3:8, 4:12, 5:16
    float sens = 0.5f, crunch = 0;
    int root = 60;                 // key of slice 1 (C3)
    int mapMode = 0;               // 0 consecutive keys, 1 SCALE MAP (slices on the notes of the key/scale)
    int key = 0, scale = 1;        // C minor
    bool gate = false, choke = true, keyTrack = true;
    int steps = 16; ChopStep pattern[16];
    uint32_t seed = 1;

    static int slicesFor(int mode) { static const int S[] = { 0, 4, 5, 8, 12, 16 }; return S[juce::jlimit(0, 5, mode)]; }
    int size() const { return audio ? audio->size() : 0; }
    int count() const { return (int) slices.size(); }
    int endOf(int i) const { return i + 1 < count() ? slices[(size_t) i + 1].start : size(); }
    double rate() const { return audio ? audio->rate : 44100.0; }

    void autoChop() {
        slices.clear(); if (!audio || size() < 64) return;
        for (int s : detectOnsets(audio->data.data(), size(), rate(), sens, slicesFor(slicesMode))) { ChopSlice c; c.start = s; slices.push_back(c); }
        clampPattern();
    }
    int sliceAt(int pos) const { for (int i = count() - 1; i >= 0; --i) if (pos >= slices[(size_t) i].start) return i; return 0; }
    int minGap() const { return (int) (rate() * 0.01); }
    // add a line at pos (splits the slice under it; the new half copies its settings). Returns the new slice or -1.
    int addLine(int pos) {
        if (!audio || count() >= ChopKit::kMax) return -1;
        pos = juce::jlimit(0, size() - 1, pos);
        if (slices.empty() || pos < slices[0].start) { if (!slices.empty() && slices[0].start - pos < minGap()) return -1; ChopSlice c = slices.empty() ? ChopSlice() : slices[0]; c.start = pos; c.det = -2; slices.insert(slices.begin(), c); return 0; }
        const int i = sliceAt(pos);
        if (pos - slices[(size_t) i].start < minGap() || endOf(i) - pos < minGap()) return -1;
        ChopSlice c = slices[(size_t) i]; c.start = pos; c.det = -2; slices[(size_t) i].det = -2;
        slices.insert(slices.begin() + i + 1, c); clampPattern(); return i + 1;
    }
    void moveLine(int i, int pos) {
        if (!juce::isPositiveAndBelow(i, count())) return;
        const int lo = i > 0 ? slices[(size_t) i - 1].start + minGap() : 0, hi = endOf(i) - minGap();
        slices[(size_t) i].start = juce::jlimit(lo, juce::jmax(lo, hi), pos);
        slices[(size_t) i].det = -2; if (i > 0) slices[(size_t) i - 1].det = -2;
    }
    void deleteLine(int i) {   // merge slice i into the one before it (the first line just moves the start back to 0)
        if (!juce::isPositiveAndBelow(i, count())) return;
        if (i == 0) { slices[0].start = 0; slices[0].det = -2; return; }
        slices.erase(slices.begin() + i); slices[(size_t) i - 1].det = -2; clampPattern();
    }
    void clampPattern() { for (auto& s : pattern) s.slice = juce::jlimit(0, juce::jmax(0, count() - 1), s.slice); }

    // ---- keys
    std::vector<int> scaleNotes() const { return MidiFx::scale(scale); }
    int noteFor(int i) const {
        if (mapMode == 0) return root + i;
        const auto sc = scaleNotes(); const int sz = (int) sc.size();
        const int base = root - ((root - key) % 12 + 12) % 12 + 0;   // the key's tonic at or below root
        int deg0 = 0; for (int k = 0; k < sz; ++k) if (base + sc[(size_t) k] >= root) { deg0 = k; break; }   // first scale note at or above root
        const int d = deg0 + i; return base + 12 * (d / sz) + sc[(size_t) (d % sz)];
    }
    void buildKeyTable(int8_t* slotOf, int8_t* octOf) const {
        for (int k = 0; k < 128; ++k) { slotOf[k] = -1; octOf[k] = 0; }
        const int n = count(); if (n == 0) return;
        for (int i = 0; i < n; ++i) { const int k = noteFor(i); if (juce::isPositiveAndBelow(k, 128)) { slotOf[k] = (int8_t) i; octOf[k] = 0; } }
        if (!keyTrack) return;
        // KEY TRACK: keys past the slices repeat them an octave up (or down), like a sampler zone that wraps
        const int lo = noteFor(0), hi = noteFor(n - 1), spanKeys = hi - lo + 1;
        if (mapMode == 0) {
            for (int k = 0; k < 128; ++k) if (slotOf[k] < 0) { const int rel = k - lo; const int o = (int) std::floor((double) rel / spanKeys); const int idx = rel - o * spanKeys; if (idx < n && std::abs(o) <= 3) { slotOf[k] = (int8_t) idx; octOf[k] = (int8_t) o; } }
        } else {
            for (int o = -3; o <= 3; ++o) if (o != 0) for (int i = 0; i < n; ++i) { const int k = noteFor(i) + 12 * o * ((spanKeys + 11) / 12); if (juce::isPositiveAndBelow(k, 128) && slotOf[k] < 0) { slotOf[k] = (int8_t) i; octOf[k] = (int8_t) (o * ((spanKeys + 11) / 12)); } }
        }
    }

    // ---- pitch detection / TUNE TO KEY
    float detect(int i) {
        auto& s = slices[(size_t) i];
        if (s.det > -1.5f || !audio) return s.det;
        const int a = s.start, b = endOf(i);
        s.det = yinMidi(audio->data.data() + a, b - a, rate());
        return s.det;
    }
    static float nearestInScale(float midi, int key, const std::vector<int>& sc) {
        float best = midi, bd = 1e9f;
        const int c = (int) std::round(midi);
        for (int m = c - 12; m <= c + 12; ++m) { const int rel = ((m - key) % 12 + 12) % 12; if (std::find(sc.begin(), sc.end(), rel) == sc.end()) continue; const float d = std::abs(m - midi); if (d < bd) { bd = d; best = (float) m; } }
        return best;
    }
    int tuneToKey() {   // returns how many slices had a pitch
        const auto sc = scaleNotes(); int done = 0;
        for (int i = 0; i < count(); ++i) {
            const float d = detect(i); if (d < 0) continue;
            auto& s = slices[(size_t) i];
            const float now = d + s.pitch;                 // where it sounds with its current offset (stays near the user's choice)
            s.pitch = juce::jlimit(-24.0f, 24.0f, std::round((nearestInScale(now, key, sc) - d) * 100.0f) / 100.0f); ++done;
        }
        return done;
    }

    // ---- CHOP MELODY: a catchy 1/16 pattern from the slices (locked steps stay)
    void makeMelody() {
        std::mt19937 rng(seed = seed * 1664525u + 1013904223u);
        auto R = [&](int n) { return (int) (rng() % (uint32_t) juce::jmax(1, n)); };
        static const char* rhythms[] = { "x..x..x...x.x...", "x.xx..x.x..x.x..", "x..x..x.x.x..x..", "xx.x.x..x..x.xx.", "x...x.x..x.x.x..", "x.x..x.xx..x..x.", "x..x.xx..x..x.x.", "x.x.x..x..x.x..x" };
        const char* rh = rhythms[R(8)];
        const int n = juce::jmax(1, count());
        // a motif of 3 slices (favour the voiced ones), answered in the second half with a new ending
        std::vector<int> voiced; for (int i = 0; i < count(); ++i) if (slices[(size_t) i].det >= 0 || slices[(size_t) i].det < -1.5f) voiced.push_back(i);
        if (voiced.empty()) for (int i = 0; i < n; ++i) voiced.push_back(i);
        int motif[3]; for (auto& m : motif) m = voiced[(size_t) R((int) voiced.size())];
        const int answer = voiced[(size_t) R((int) voiced.size())];
        int k = 0;
        for (int s = 0; s < 16; ++s) {
            auto& st = pattern[s]; if (st.lock) { if (st.on) ++k; continue; }
            const char c = rh[s % (steps == 8 ? 8 : 16)];
            st.on = s < steps && c == 'x';
            if (!st.on) continue;
            const bool secondHalf = s >= steps / 2;
            int sl = motif[k % 3];
            if (secondHalf && (s == steps - 2 || s == steps - 1 || (k % 3 == 2 && R(2)))) sl = answer;
            st.slice = juce::jlimit(0, n - 1, sl);
            st.vel = (s % 4 == 0) ? 1.0f : (s % 2 == 0 ? 0.85f : 0.72f) + 0.06f * (float) R(3);
            ++k;
        }
    }

    // ---- rendering one slice (offline): reverse, pitch (formant keep or chipmunk), CRUNCH, gain, fades
    static std::vector<float> renderSlice(const SampleData& A, int a, int b, const ChopSlice& s, float crunch) {
        a = juce::jlimit(0, A.size(), a); b = juce::jlimit(a, A.size(), b);
        std::vector<float> in(A.data.begin() + a, A.data.begin() + b);
        if (in.empty()) return in;
        if (s.reverse) std::reverse(in.begin(), in.end());
        std::vector<float> out;
        if (std::abs(s.pitch) < 0.001f && crunch < 0.005f) out = in;
        else {
            WarpOpts o; o.formantKeep = s.formant; o.crunch = crunch; o.baseHz = s.det > 0 ? 440.0f * std::exp2((s.det - 69.0f) / 12.0f) : 0.0f;
            out = warpRender(in.data(), (int) in.size(), { { (int) in.size(), (int) in.size(), s.pitch } }, A.rate, o);
        }
        const int N = (int) out.size(); const float g = juce::Decibels::decibelsToGain(s.gainDb);
        const int fi = juce::jlimit(1, juce::jmax(1, N / 2), (int) (s.fadeIn * 0.001 * A.rate)), fo = juce::jlimit(1, juce::jmax(1, N / 2), (int) (s.fadeOut * 0.001 * A.rate));
        for (int i = 0; i < N; ++i) {
            float w = 1; if (i < fi) w = (float) i / fi; if (N - 1 - i < fo) w = juce::jmin(w, (float) (N - 1 - i) / fo);
            out[(size_t) i] *= g * w;
        }
        return out;
    }
    juce::String cacheKey(int i) const {
        const auto& s = slices[(size_t) i];
        return juce::String::toHexString((juce::pointer_sized_int) audio.get()) + ":" + juce::String(s.start) + ":" + juce::String(endOf(i)) + ":" + juce::String(s.pitch, 3) + ":" + juce::String((int) s.reverse)
             + juce::String((int) s.formant) + ":" + juce::String(s.gainDb, 2) + ":" + juce::String(s.fadeIn, 1) + ":" + juce::String(s.fadeOut, 1) + ":" + juce::String(crunch, 3) + ":" + juce::String(s.det, 2);
    }
    using Cache = std::map<juce::String, std::shared_ptr<const std::vector<float>>>;
    // renders every slice (reusing cached ones) -> a kit for the audio thread. Returns the new cache.
    std::shared_ptr<ChopKit> buildKit(Cache& cache, std::atomic<bool>* cancel = nullptr) const {
        auto kit = std::make_shared<ChopKit>(); Cache next;
        if (!audio) return kit;
        kit->rate = rate(); kit->choke = choke; kit->gate = gate;
        kit->n = juce::jmin(count(), ChopKit::kMax);
        for (int i = 0; i < kit->n; ++i) {
            if (cancel && cancel->load()) return nullptr;
            const auto ck = cacheKey(i);
            std::shared_ptr<const std::vector<float>> buf;
            if (auto it = cache.find(ck); it != cache.end()) buf = it->second;
            else buf = std::make_shared<const std::vector<float>>(renderSlice(*audio, slices[(size_t) i].start, endOf(i), slices[(size_t) i], crunch));
            next[ck] = buf; kit->hold.push_back(buf);
            kit->slot[i] = { buf->data(), (int) buf->size(), slices[(size_t) i].loop };
        }
        buildKeyTable(kit->slotOf, kit->octOf);
        cache = std::move(next);
        return kit;
    }
    // BOUNCE: print every slice (pitch, reverse, gain, fades, crunch) into one new sample; the lines stay where they were
    void bounce(Cache& cache) {
        if (!audio || slices.empty()) return;
        auto nb = std::make_shared<SampleData>(); nb->rate = rate(); nb->name = audio->name.upToFirstOccurrenceOf(" (bounce", false, false) + " (bounce)";
        const int off = slices[0].start;
        for (int i = 0; i < count(); ++i) {
            const auto ck = cacheKey(i); std::vector<float> part;
            if (auto it = cache.find(ck); it != cache.end()) part = *it->second; else part = renderSlice(*audio, slices[(size_t) i].start, endOf(i), slices[(size_t) i], crunch);
            part.resize((size_t) (endOf(i) - slices[(size_t) i].start), 0.0f);
            nb->data.insert(nb->data.end(), part.begin(), part.end());
        }
        float pk = 1e-6f; for (float v : nb->data) pk = juce::jmax(pk, std::abs(v));
        if (pk > 0.95f) for (float& v : nb->data) v *= 0.95f / pk;
        nb->buildFrames();
        audio = nb;
        for (auto& s : slices) { s.start -= off; s.pitch = 0; s.reverse = false; s.gainDb = 0; s.det = -2; }
        crunch = 0;   // it is printed in now (turn it up again to stack more grit)
    }

    // ---- save / load
    juce::ValueTree toTree(bool withAudio = true) const {
        juce::ValueTree t("CHOP");
        t.setProperty("mode", slicesMode, nullptr); t.setProperty("sens", sens, nullptr); t.setProperty("crunch", crunch, nullptr); t.setProperty("root", root, nullptr);
        t.setProperty("map", mapMode, nullptr); t.setProperty("key", key, nullptr); t.setProperty("scale", scale, nullptr); t.setProperty("gate", gate, nullptr);
        t.setProperty("choke", choke, nullptr); t.setProperty("track", keyTrack, nullptr); t.setProperty("steps", steps, nullptr); t.setProperty("seed", (juce::int64) seed, nullptr);
        juce::String sl; for (auto& s : slices) sl << s.start << "," << s.pitch << "," << s.gainDb << "," << s.fadeIn << "," << s.fadeOut << "," << (int) s.reverse << "," << (int) s.loop << "," << (int) s.formant << "," << s.det << ";";
        t.setProperty("slices", sl, nullptr);
        juce::String pt; for (auto& p : pattern) pt << (int) p.on << "," << (int) p.lock << "," << p.slice << "," << p.vel << ";";
        t.setProperty("pattern", pt, nullptr);
        if (audio) {
            t.setProperty("name", audio->name, nullptr); t.setProperty("rate", audio->rate, nullptr);
            t.setProperty("len", audio->size(), nullptr);
            if (withAudio) { t.setProperty("data", audio->toBase64(), nullptr); if (audio->made) t.setProperty("made", true, nullptr); }
            else t.setProperty("audio", juce::String::toHexString((juce::pointer_sized_int) audio.get()), nullptr);
        }
        return t;
    }
    void fromTree(const juce::ValueTree& t) {
        slicesMode = t.getProperty("mode", 2); sens = (float) (double) t.getProperty("sens", 0.5); crunch = (float) (double) t.getProperty("crunch", 0.0); root = t.getProperty("root", 60);
        mapMode = t.getProperty("map", 0); key = t.getProperty("key", 0); scale = t.getProperty("scale", 1); gate = t.getProperty("gate", false);
        choke = t.getProperty("choke", true); keyTrack = t.getProperty("track", true); steps = t.getProperty("steps", 16); seed = (uint32_t) (juce::int64) t.getProperty("seed", 1);
        audio = nullptr;
        if (t.hasProperty("data")) { auto a = SampleData::fromBase64(t.getProperty("data").toString(), (double) t.getProperty("rate", 44100.0), t.getProperty("name").toString()); SampleData::readMade(a.get(), t); audio = a; }
        slices.clear();
        for (auto& part : juce::StringArray::fromTokens(t.getProperty("slices").toString(), ";", "")) {
            auto f = juce::StringArray::fromTokens(part, ",", ""); if (f.size() < 8) continue;
            ChopSlice s; s.start = f[0].getIntValue(); s.pitch = (float) f[1].getDoubleValue(); s.gainDb = (float) f[2].getDoubleValue(); s.fadeIn = (float) f[3].getDoubleValue();
            s.fadeOut = (float) f[4].getDoubleValue(); s.reverse = f[5].getIntValue() != 0; s.loop = f[6].getIntValue() != 0; s.formant = f[7].getIntValue() != 0; s.det = f.size() > 8 ? (float) f[8].getDoubleValue() : -2.0f;
            if (!audio || s.start < audio->size()) slices.push_back(s);
        }
        int k = 0;
        for (auto& part : juce::StringArray::fromTokens(t.getProperty("pattern").toString(), ";", "")) {
            if (k >= 16) break; auto f = juce::StringArray::fromTokens(part, ",", ""); if (f.size() < 4) continue;
            pattern[k++] = { f[0].getIntValue() != 0, f[1].getIntValue() != 0, f[2].getIntValue(), (float) f[3].getDoubleValue() };
        }
        clampPattern();
    }
};

} // namespace gs
