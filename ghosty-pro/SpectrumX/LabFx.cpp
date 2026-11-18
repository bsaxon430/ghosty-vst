#include "LabFx.h"
#include <algorithm>
#include <numeric>

namespace lab {
namespace {
inline float hat(float u, float a, float peak, float b) { if (u <= a || u >= b) return 0.0f; return u < peak ? (u - a) / (peak - a) : (b - u) / (b - peak); }
inline uint32_t hash3(uint32_t a, uint32_t b, uint32_t c) { uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u ^ (c + 0x165667B1u) * 0xC2B2AE3Du; h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15; return h; }
inline float rnd01(uint32_t h) { return (float) (h & 0xFFFFFF) / 16777216.0f; }
Sel all(const Canvas& c) { Sel s; s.t0 = 0; s.t1 = c.T; s.r0 = 0; s.r1 = R - 1; return s; }

Canvas blend(const Canvas& a, const Canvas& b, float m) {   // a -> b by m (same size)
    if (a.T != b.T || m >= 0.999f) return b; if (m <= 0.001f) return a;
    Canvas o = m >= 0.5f ? b : a;
    for (size_t i = 0; i < o.cells.size(); ++i) o.cells[i].mag = a.cells[i].mag + (b.cells[i].mag - a.cells[i].mag) * m;
    for (size_t i = 0; i < o.amp.size(); ++i) o.amp[i] = a.amp[i] + (b.amp[i] - a.amp[i]) * m;
    return o;
}
void ring(Canvas& c, float coef) {   // metallic ring: energy hangs on in time
    for (int t = 1; t < c.T; ++t) {
        Cell* cc = c.col(t); const Cell* pc = c.col(t - 1);
        for (int k = 1; k < K - 1; ++k) if (pc[k].kind == Moved && pc[k].mag * coef > cc[k].mag) { cc[k] = pc[k]; cc[k].mag = pc[k].mag * coef; }
        for (int r = 0; r < R; ++r) { const float v = c.A(t - 1, r) * coef; if (v > c.A(t, r)) { c.A(t, r) = v; c.fq[(size_t) t * R + (size_t) r] = c.fq[(size_t) (t - 1) * R + (size_t) r]; } }
    }
}
void copyFrame(Canvas& c, int dst, int src) {
    std::copy(c.col(src), c.col(src) + K, c.col(dst)); lockColumn(c.col(dst), 1, K - 2);
    for (int r = 0; r < R; ++r) { const size_t a = (size_t) dst * R + (size_t) r, b = (size_t) src * R + (size_t) r; c.amp[a] = c.amp[b]; c.fq[a] = c.fq[b]; c.noise[a] = c.noise[b]; }
}
void silenceFrame(Canvas& c, int t, float g) { for (int k = 0; k < K; ++k) c.col(t)[k].mag *= g; for (int r = 0; r < R; ++r) c.A(t, r) *= g; }
} // namespace

// ================================================================ SPECTRAL GHOST
namespace {
struct Stages { float metal, up, frag, grain; };
Stages stagesAt(float u, float e) { return { e * hat(u, 0.0f, 0.3f, 0.62f), e * hat(u, 0.15f, 0.5f, 0.82f), e * hat(u, 0.38f, 0.66f, 0.9f), e * hat(u, 0.5f, 0.8f, 0.97f) }; }
}
const char* ghostStage(int i, int count, float evolve) {
    if (i <= 0) return "SOURCE";
    const float u = count > 1 ? (float) i / (count - 1) : 0.0f; if (u > 0.86f) return "RETURN";
    const auto s = stagesAt(u, std::max(0.05f, evolve));
    const float v[] = { s.metal, s.up, s.frag, s.grain }; static const char* names[] = { "METAL", "RISE", "FRAGMENT", "DUST" };
    int best = 0; for (int k = 1; k < 4; ++k) if (v[k] > v[best]) best = k;
    return names[best];
}
void ghostEach(const Canvas& src, const GhostParams& p, const std::function<bool(int, const Canvas&)>& fn) {
    const int n = juce::jlimit(1, kMaxGhosts, p.count);
    if (src.T <= 0) return;
    if (!fn(0, src)) return;
    Canvas prev = src;
    juce::Random base((juce::int64) p.seed * 2654435761u + 11);
    const double fres = 110.0 * std::exp2(base.nextFloat() * 2.0);       // the ghost's metal: 110..440 Hz series
    for (int i = 1; i < n; ++i) {
        if (cancelled()) return;
        const float u = n > 1 ? (float) i / (n - 1) : 0.0f;
        const float e = juce::jlimit(0.0f, 1.0f, p.evolve);
        const bool returning = u > 0.86f;
        Canvas c = returning ? blend(src, prev, p.memory * 0.25f) : blend(src, prev, p.memory);
        const auto st = stagesAt(u, e); const float metal = st.metal, up = st.up, frag = st.frag, grain = st.grain;
        const float mem = 1.0f - p.memory * 0.6f;   // with memory the changes pile up, so each step adds less
        const uint32_t sd = p.seed * 7919u + (uint32_t) i * 104729u;
        if (metal > 0.01f) {
            const float m = juce::jlimit(0.0f, 1.0f, metal * (i == 1 ? 1.0f : mem));
            auto toSeries = [fres, m](double f) { const double h = std::max(1.0, std::round(f / fres)); return std::exp(std::log(f) * (1 - m) + std::log(h * fres) * m); };
            for (int t = 0; t < c.T; ++t) moveFrame(c.col(t), 1, K - 2, toSeries);
            for (int t = 0; t < c.T; ++t) for (int r = 0; r < R; ++r) if (c.A(t, r) > 0) { const size_t ix = (size_t) t * R + (size_t) r; const double f = c.fq[ix] > 0 ? c.fq[ix] : rowHz(r); const double f2 = toSeries(f);
                const int r2 = (int) std::lround(hzRow(f2)); const float a = c.amp[ix]; c.amp[ix] = 0; if (r2 >= 0 && r2 < R) { const size_t j = (size_t) t * R + (size_t) r2; if (a > c.amp[j]) { c.amp[j] = a; c.fq[j] = (float) f2; } } }
            ring(c, 0.55f + 0.4f * m);
        }
        if (up > 0.01f) shift(c, all(c), up * 12.0 * (p.memory > 0.5f ? 0.5 : 1.0), p.keepTransients);
        if (frag > 0.01f) {
            juce::Random r((juce::int64) sd + 5);
            for (int t = 0; t < c.T; ) {
                const int len = 2 + r.nextInt(10); const float x = r.nextFloat();
                if (x < frag * 0.5f) for (int q = t; q < std::min(c.T, t + len); ++q) silenceFrame(c, q, 0.0f);
                else if (x < frag * 0.8f && t >= len) for (int q = t; q < std::min(c.T, t + len); ++q) copyFrame(c, q, q - len);
                t += len;
            }
        }
        if (grain > 0.01f) {
            const float drop = grain * 0.85f, boost = 1.0f / std::sqrt(std::max(0.15f, 1.0f - drop));
            for (int t = 0; t < c.T; ++t) {
                Cell* cc = c.col(t);
                for (int k = 0; k < K; ++k) { if (rnd01(hash3(sd, (uint32_t) t, (uint32_t) k)) < drop) cc[k].mag = 0; else cc[k].mag *= boost; }
                for (int rr = 0; rr < R; ++rr) { float& a = c.A(t, rr); if (a <= 0) continue; if (rnd01(hash3(sd + 1, (uint32_t) t, (uint32_t) rr)) < drop) a = 0; else { a *= boost; c.noise[(size_t) t * R + (size_t) rr] = std::max(c.noise[(size_t) t * R + (size_t) rr], grain * 0.5f); } }
            }
            juce::Random r((juce::int64) sd + 9);
            for (int t = 3; t < c.T - 3; ++t) if (r.nextFloat() < grain * 0.3f) { const int o = t + r.nextInt(7) - 3; if (o != t) copyFrame(c, t, o); }
        }
        // DECAY: each repeat a little quieter and darker
        const float lvl = std::pow(1.0f - 0.55f * p.decay, (float) i), tilt = p.decay * i * 0.12f;
        for (int t = 0; t < c.T; ++t) {
            Cell* cc = c.col(t); for (int k = 1; k < K; ++k) { const double f = k * BIN_HZ; cc[k].mag *= lvl * (f > 1500 ? (float) std::pow(f / 1500.0, -tilt) : 1.0f); }
            for (int r = 0; r < R; ++r) { const double f = rowHz(r); c.A(t, r) *= lvl * (f > 1500 ? (float) std::pow(f / 1500.0, -tilt) : 1.0f); }
        }
        if (!fn(i, c)) return;
        prev = std::move(c);
    }
}
std::vector<Canvas> ghostSet(const Canvas& src, const GhostParams& p) {
    std::vector<Canvas> out; ghostEach(src, p, [&out](int, const Canvas& c) { out.push_back(c); return true; }); return out;
}

// ================================================================ SPECTRAL SPLIT
void SplitSetup::sanitise() {
    bands = juce::jlimit(2, kMaxBands, bands);
    for (auto& x : xover) x = std::isfinite(x) ? juce::jlimit(40.0f, 18000.0f, x) : 1000.0f;
    std::sort(xover.begin(), xover.begin() + (bands - 1));
    for (int i = 1; i < bands - 1; ++i) xover[(size_t) i] = std::max(xover[(size_t) i], std::min(18000.0f, xover[(size_t) i - 1] * 1.19f));
    swing = std::isfinite(swing) ? juce::jlimit(0.0f, 0.5f, swing) : 0.0f;
    for (auto& b : seq) {
        b.length = juce::jlimit(1, kSteps, b.length); b.rateDiv = juce::jlimit(4, 64, b.rateDiv);
        b.gate = std::isfinite(b.gate) ? juce::jlimit(0.05f, 1.0f, b.gate) : 1.0f; b.level = std::isfinite(b.level) ? juce::jlimit(0.0f, 2.0f, b.level) : 1.0f;
        for (auto& v : b.prob) v = (uint8_t) std::min<int>(100, v); for (auto& v : b.pitch) v = (int8_t) juce::jlimit(-12, 12, (int) v);
    }
}
float splitMask(const SplitSetup& s, int band, double hz) {
    const int nb = juce::jlimit(2, kMaxBands, s.bands);
    auto step = [&](int j) {   // 0 below crossover j, 1 above (raised cosine over half an octave)
        const double x = std::log2(std::max(1.0, hz) / std::max(10.0f, s.xover[(size_t) j])) / 0.5 + 0.5;
        if (x <= 0) return 0.0;
        if (x >= 1) return 1.0;
        return 0.5 - 0.5 * std::cos(juce::MathConstants<double>::pi * x);
    };
    if (band < 0 || band >= nb) return 0.0f;
    const double lo = band == 0 ? 1.0 : step(band - 1), hi = band == nb - 1 ? 0.0 : step(band);
    return (float) std::max(0.0, lo - hi);
}
std::vector<std::vector<float>> splitBands(const std::vector<float>& x, const SplitSetup& sIn) {
    SplitSetup s = sIn; s.sanitise();
    const int nb = s.bands; int T = 0; auto X = stftFrames(x, T);
    std::vector<std::vector<float>> out;
    for (int b = 0; b < nb; ++b) {
        if (cancelled()) break;
        std::vector<float> m((size_t) K); for (int k = 0; k < K; ++k) m[(size_t) k] = splitMask(s, b, k * BIN_HZ);
        auto Y = X; for (int t = 0; t < T; ++t) for (int k = 0; k < K; ++k) Y[(size_t) t * K + (size_t) k] *= m[(size_t) k];
        out.push_back(istftFrames(Y, T, (int) x.size()));
    }
    return out;
}
StepEval evalBand(const BandSeq& b, double beats, int band, float swing, uint32_t seed, bool muted) {
    StepEval e; e.srcBeats = beats;
    const double stepLen = 4.0 / juce::jlimit(4, 64, b.rateDiv);
    // swing: every second step starts late by swing x a step (and the one after it is that much shorter)
    const double pair = 2.0 * stepLen, sw = juce::jlimit(0.0, 0.5, (double) swing) * stepLen;
    const double pi = std::floor(beats / pair), inPair = beats - pi * pair;
    long long idx; double within, len;
    if (inPair < stepLen + sw) { idx = (long long) pi * 2; within = inPair; len = stepLen + sw; }
    else { idx = (long long) pi * 2 + 1; within = inPair - stepLen - sw; len = stepLen - sw; }
    const int L = juce::jlimit(1, kSteps, b.length);
    const int i = (int) ((idx % L + L) % L); e.step = i;
    if (b.mute || muted) { e.gain = 0; return e; }
    bool on = b.steps[(size_t) i] != 0;
    if (on && b.prob[(size_t) i] < 100) {   // a fresh coin for every pass of the step
        uint32_t h = (uint32_t) (idx & 0xFFFFFFFF) * 0x9E3779B1u ^ (uint32_t) (band + 1) * 0x85EBCA77u ^ seed * 0xC2B2AE3Du; h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12;
        on = (int) (h % 100u) < (int) b.prob[(size_t) i];
    }
    e.gain = on && within < juce::jlimit(0.05f, 1.0f, b.gate) * len + 1e-9 ? b.level : 0.0f;
    const double stepStart = beats - within, rate = std::exp2(b.pitch[(size_t) i] / 12.0);
    if (b.retrig && on) { const double region = stepLen >= 1.0 ? 4.0 : 1.0; e.srcBeats = std::floor(beats / region) * region + within * rate; }
    else if (b.pitch[(size_t) i] != 0) e.srcBeats = stepStart + within * rate;
    return e;
}
std::vector<float> renderSplit(const std::vector<std::vector<float>>& bands, const SplitSetup& sIn, double bpm, int length) {
    SplitSetup s = sIn; s.sanitise();
    std::vector<float> y((size_t) std::max(0, length), 0.0f); const double spb = 60.0 / juce::jlimit(20.0, 400.0, bpm) * SR;
    for (size_t b = 0; b < bands.size() && b < (size_t) kMaxBands; ++b) {
        float g = 0; const auto& x = bands[b]; const float coef = 1.0f - std::exp(-1.0f / (0.0015f * (float) SR));
        for (int n = 0; n < length; ++n) {
            const auto e = evalBand(s.seq[b], n / spb, (int) b, s.swing, s.seed); g += (e.gain - g) * coef;
            const double src = e.srcBeats * spb; const int i = (int) src; const float fr = (float) (src - i);
            if (i >= 0 && i + 1 < (int) x.size()) y[(size_t) n] += g * (x[(size_t) i] * (1 - fr) + x[(size_t) i + 1] * fr);
        }
    }
    return y;
}

// ================================================================ CHAOS PAINT
void chaosPaint(Canvas& c, const ChaosParams& p, const std::vector<Sel>& locks, const Canvas* follow) {
    if (c.T <= 0) return;
    auto locked = [&](int t, int r) { for (auto& s : locks) if (t >= s.t0 && t < s.t1 && r >= s.r0 && r <= s.r1) return true; return false; };
    for (int t = 0; t < c.T; ++t) for (int r = 0; r < R; ++r) if (!locked(t, r)) { const size_t i = (size_t) t * R + (size_t) r; c.amp[i] = 0; c.fq[i] = 0; c.noise[i] = 0; }
    juce::Random rng((juce::int64) p.seed * 48271 + 7);
    const double secs = c.T * HOP / SR, stepFrames = std::max(1.0, 60.0 / p.bpm / 4.0 * SR / HOP);   // a 1/16 note in frames
    const bool fol = follow && follow->T > 0 && p.constraint > 0.001f;
    // follow: onset strength per frame (spectral flux) -> where events like to start; level per (frame,row) -> where they may sound
    std::vector<float> cdf; std::vector<float> fl;
    if (fol) {
        fl.assign((size_t) follow->T * R, 0); float mx = 1e-9f;
        for (int t = 0; t < follow->T; ++t) for (int r = 0; r < R; ++r) { const float v = follow->level(t, r); fl[(size_t) t * R + (size_t) r] = v; mx = std::max(mx, v); }
        for (auto& v : fl) v = juce::jlimit(0.0f, 1.0f, (20.0f * std::log10(std::max(1e-9f, v / mx)) + 60.0f) / 60.0f);
        cdf.assign((size_t) follow->T, 0); double acc = 0;
        for (int t = 0; t < follow->T; ++t) { double f = 0; if (t > 0) for (int r = 0; r < R; ++r) f += std::max(0.0f, fl[(size_t) t * R + (size_t) r] - fl[(size_t) (t - 1) * R + (size_t) r]); acc += f + 1e-3; cdf[(size_t) t] = (float) acc; }
    }
    auto folLevel = [&](int t, int r) { if (!fol) return 1.0f; const int ft = juce::jlimit(0, follow->T - 1, (int) ((long long) t * follow->T / c.T)); float v = 0; for (int d = -3; d <= 3; ++d) { const int rr = r + d; if (rr >= 0 && rr < R) v = std::max(v, fl[(size_t) ft * R + (size_t) rr]); } return v; };
    auto folPeak = [&](int t) { const int ft = juce::jlimit(0, follow->T - 1, (int) ((long long) t * follow->T / c.T)); int best = 0; float bv = 0; for (int r = 40; r < R - 40; ++r) { const float v = fl[(size_t) ft * R + (size_t) r]; if (v > bv) { bv = v; best = r; } } return rowHz(best); };
    const int events = std::max(1, (int) std::round((1.0 + p.density * p.density * 15.0) * secs));
    const auto& sc = scaleSteps(p.scale);
    for (int ev = 0; ev < events; ++ev) {
        // when
        double t0;
        if (fol && rng.nextFloat() < p.constraint) { const float x = rng.nextFloat() * cdf.back(); const int ft = (int) (std::lower_bound(cdf.begin(), cdf.end(), x) - cdf.begin()); t0 = (double) ft * c.T / follow->T; }
        else t0 = rng.nextDouble() * c.T;
        if (rng.nextFloat() > p.chaos) t0 = std::round(t0 / stepFrames) * stepFrames;
        static const int lens[] = { 1, 2, 4, 8 };
        const double len = (p.chaos > rng.nextFloat() ? 1.0 + rng.nextDouble() * 15.0 : lens[rng.nextInt(4)]) * stepFrames;
        // what pitch
        double f;
        if (fol && rng.nextFloat() < p.constraint * 0.7f) f = folPeak((int) t0);
        else if (p.harmony < 0.34f) f = midiHz(36.0f + rng.nextFloat() * 60.0f);
        else if (p.harmony < 0.67f) { const int oct = 3 + rng.nextInt(4); f = midiHz((float) (12 * oct + p.root + sc[(size_t) rng.nextInt((int) sc.size())])); }
        else { static const int H[] = { 1, 2, 3, 4, 5, 6, 8, 10, 12, 16 }; f = midiHz((float) (36 + p.root)) * H[rng.nextInt(10)]; }
        if (p.harmony >= 0.34f && p.harmony < 0.67f) f = midiHz(snapPitch(hzMidi((float) f), p.root, p.scale));
        if (p.harmony < 0.34f) f *= std::exp2((rng.nextFloat() - 0.5f) * p.chaos / 6.0f);
        const int partials = 1 + (int) std::round(rng.nextFloat() * p.density * 6.0f);
        const float level = 0.12f + rng.nextFloat() * 0.3f, noise = rng.nextFloat() < p.chaos * 0.4f ? rng.nextFloat() * p.chaos : 0.0f;
        const double glide = (rng.nextFloat() - 0.5f) * 2.0f * p.motion * 12.0f * (rng.nextFloat() < 0.5f ? 1 : 0);
        const float vib = p.motion * 0.4f * rng.nextFloat(), vibHz = 3.0f + rng.nextFloat() * 4.0f;
        const float attack = (float) std::max(1.0, len * (0.05 + 0.4 * rng.nextFloat() * p.chaos));
        const float inh = p.harmony < 0.34f ? p.chaos * 0.08f * rng.nextFloat() : 0.0f;
        for (int q = 0; q < (int) std::ceil(len) && (int) t0 + q < c.T; ++q) {
            const int t = (int) t0 + q; if (t < 0) continue;
            const float u = (float) (q / len);
            const float env = std::min(1.0f, (q + 1) / attack) * std::exp(-2.5f * u * (1.0f - p.motion * 0.5f));
            const double semi = glide * u + vib * std::sin(2.0 * juce::MathConstants<double>::pi * vibHz * t * HOP / SR);
            for (int h = 1; h <= partials; ++h) {
                const double fh = f * std::pow((double) h, 1.0 + inh) * std::exp2(semi / 12.0);
                if (fh < F0 || fh > 16000) continue;
                const int r = (int) std::lround(hzRow(fh)); if (r < 0 || r >= R || locked(t, r)) continue;
                const float a = level * env / std::pow((float) h, 1.1f) * (fol ? 1.0f - p.constraint + p.constraint * folLevel(t, r) : 1.0f);
                const size_t i = (size_t) t * R + (size_t) r;
                if (a > c.amp[i]) { c.amp[i] = a; c.fq[i] = (float) fh; c.noise[i] = noise; }
            }
        }
    }
}

// ================================================================ SPECTRAL DNA
namespace {
std::vector<float> envelopeRows(const Canvas& c) {   // smoothed level per (frame,row) in dB-ish linear
    std::vector<float> e((size_t) c.T * R, 0.0f);
    for (int t = 0; t < c.T; ++t) {
        float raw[R]; for (int r = 0; r < R; ++r) raw[r] = c.level(t, r);
        for (int r = 0; r < R; ++r) { double s = 0; int n = 0; for (int d = -6; d <= 6; ++d) { const int q = r + d; if (q >= 0 && q < R) { s += raw[q]; ++n; } } e[(size_t) t * R + (size_t) r] = (float) (s / n) + 1e-7f; }
    }
    return e;
}
float envAt(const std::vector<float>& e, int t, double hz) { const double r = juce::jlimit(0.0, (double) R - 1.001, hzRow(hz)); const int i = (int) r; const float a = (float) (r - i); return e[(size_t) t * R + (size_t) i] * (1 - a) + e[(size_t) t * R + (size_t) i + 1] * a; }
std::vector<float> bandFeatures(const Canvas& c) {   // 24 log bands, dB, mean removed
    constexpr int B = 24; std::vector<float> f((size_t) c.T * B, 0.0f);
    for (int t = 0; t < c.T; ++t) {
        for (int b = 0; b < B; ++b) { double s = 0; const int r0 = b * R / B, r1 = (b + 1) * R / B; for (int r = r0; r < r1; ++r) { const float v = c.level(t, r); s += (double) v * v; } f[(size_t) t * B + (size_t) b] = (float) (10.0 * std::log10(s + 1e-10)); }
    }
    double m = 0; for (float v : f) m += v; m /= std::max<size_t>(1, f.size()); for (auto& v : f) v = std::max(-60.0f, (float) (v - m));
    return f;
}
} // namespace
std::shared_ptr<MorphPrep> morphPrepare(const std::vector<float>& a, const std::vector<float>& b) {
    auto m = std::make_shared<MorphPrep>(); analyse(a, m->A); analyse(b, m->B);
    const int TA = m->A.T, TB = m->B.T; if (TA <= 0 || TB <= 0) return m;
    const auto fa = bandFeatures(m->A), fb = bandFeatures(m->B); constexpr int B = 24;
    // DTW (steps right / down / diagonal), slightly favouring the diagonal
    std::vector<float> D((size_t) TA * TB, 1e30f);
    auto cost = [&](int i, int j) { double s = 0; for (int q = 0; q < B; ++q) { const double d = fa[(size_t) i * B + (size_t) q] - fb[(size_t) j * B + (size_t) q]; s += d * d; } return (float) std::sqrt(s); };
    for (int i = 0; i < TA; ++i) { if ((i & 31) == 0 && cancelled()) return nullptr; for (int j = 0; j < TB; ++j) {
        const float c = cost(i, j); float best = (i == 0 && j == 0) ? 0.0f : 1e30f;
        if (i > 0) best = std::min(best, D[(size_t) (i - 1) * TB + (size_t) j] + c * 0.15f);
        if (j > 0) best = std::min(best, D[(size_t) i * TB + (size_t) (j - 1)] + c * 0.15f);
        if (i > 0 && j > 0) best = std::min(best, D[(size_t) (i - 1) * TB + (size_t) (j - 1)]);
        D[(size_t) i * TB + (size_t) j] = best + c;
    } }
    int i = TA - 1, j = TB - 1; m->path.push_back({ i, j });
    while (i > 0 || j > 0) {
        if (i == 0) --j; else if (j == 0) --i;
        else { const float d = D[(size_t) (i - 1) * TB + (size_t) (j - 1)], u = D[(size_t) (i - 1) * TB + (size_t) j], l = D[(size_t) i * TB + (size_t) (j - 1)];
               if (d <= u && d <= l) { --i; --j; } else if (u < l) --i; else --j; }
        m->path.push_back({ i, j });
    }
    std::reverse(m->path.begin(), m->path.end());
    m->envA = envelopeRows(m->A); m->envB = envelopeRows(m->B);
    return m;
}
void morph(const MorphPrep& m, float t, Canvas& out) {
    if (m.A.T <= 0 || m.B.T <= 0) { out = t < 0.5f ? m.A : m.B; return; }
    if (t <= 0.0f) { out = m.A; return; }
    if (t >= 1.0f) { out = m.B; return; }
    const int T = std::max(2, (int) std::lround(m.A.T + (m.B.T - m.A.T) * t));
    out.resize(T); out.srcLen = (int) std::lround(m.A.srcLen + (m.B.srcLen - m.A.srcLen) * t);
    std::vector<Cell> dA((size_t) K), dB((size_t) K); std::vector<int> pa, pb;
    for (int j = 0; j < T; ++j) {
        if ((j & 31) == 0 && cancelled()) return;
        const auto& pr = m.path[(size_t) std::lround((double) j / (T - 1) * (m.path.size() - 1))];
        const int ia = pr.first, ib = pr.second; const Cell* ca = m.A.col(ia); const Cell* cb = m.B.col(ib);
        // strongest peaks of each, then a monotonic alignment in log frequency (DP with skips)
        auto peaks = [](const Cell* c) { std::vector<int> p; float mx = 0; for (int k = 1; k < K - 1; ++k) mx = std::max(mx, c[k].mag);
            for (int k = 2; k < K - 2; ++k) if (c[k].mag > mx * 1e-3f && c[k].mag >= c[k - 1].mag && c[k].mag > c[k + 1].mag) p.push_back(k);
            std::sort(p.begin(), p.end(), [c](int a, int b) { return c[a].mag > c[b].mag; }); if (p.size() > 40) p.resize(40); std::sort(p.begin(), p.end()); return p; };
        const auto PA = peaks(ca), PB = peaks(cb);
        auto fr = [](const Cell* c, int k) { return c[k].freq > 0 ? (double) c[k].freq : k * BIN_HZ; };
        const int na = (int) PA.size(), nb = (int) PB.size();
        std::vector<int> matchA((size_t) na, -1), matchB((size_t) nb, -1);
        if (na > 0 && nb > 0) {
            const double rho = std::log(fr(cb, PB[0]) / fr(ca, PA[0]));   // lowest partials line up
            std::vector<float> C((size_t) (na + 1) * (nb + 1), 0); std::vector<uint8_t> mv((size_t) (na + 1) * (nb + 1), 0);
            const float skip = 0.22f;
            for (int x = 0; x <= na; ++x) for (int y = 0; y <= nb; ++y) {
                if (x == 0 && y == 0) continue; float best = 1e30f; uint8_t bm = 0;
                if (x > 0 && y > 0) { const float c = C[(size_t) (x - 1) * (nb + 1) + (size_t) (y - 1)] + (float) std::abs(std::log(fr(ca, PA[(size_t) x - 1])) + rho - std::log(fr(cb, PB[(size_t) y - 1]))); if (c < best) { best = c; bm = 1; } }
                if (x > 0) { const float c = C[(size_t) (x - 1) * (nb + 1) + (size_t) y] + skip; if (c < best) { best = c; bm = 2; } }
                if (y > 0) { const float c = C[(size_t) x * (nb + 1) + (size_t) (y - 1)] + skip; if (c < best) { best = c; bm = 3; } }
                C[(size_t) x * (nb + 1) + (size_t) y] = best; mv[(size_t) x * (nb + 1) + (size_t) y] = bm;
            }
            for (int x = na, y = nb; x > 0 || y > 0; ) { const uint8_t b = mv[(size_t) x * (nb + 1) + (size_t) y]; if (b == 1) { matchA[(size_t) x - 1] = y - 1; matchB[(size_t) y - 1] = x - 1; --x; --y; } else if (b == 2) --x; else --y; }
        }
        std::fill(dA.begin(), dA.end(), Cell()); std::fill(dB.begin(), dB.end(), Cell());
        auto idxOf = [](const std::vector<int>& v, int k) { auto it = std::lower_bound(v.begin(), v.end(), k); return (it != v.end() && *it == k) ? (int) (it - v.begin()) : -1; };
        placeRegions(ca + 1, 1, K - 2, [&](int pk, double fp, double& f2, float& g) {
            const int x = idxOf(PA, pk); if (x < 0 || matchA[(size_t) x] < 0) { f2 = fp; g = 1.0f; return true; }
            const double fb = fr(cb, PB[(size_t) matchA[(size_t) x]]); f2 = std::exp(std::log(fp) * (1 - t) + std::log(fb) * t);
            g = (float) std::pow(envAt(m.envB, ib, fb) / envAt(m.envA, ia, fp), t); g = juce::jlimit(0.05f, 8.0f, g); return true; }, dA.data());
        placeRegions(cb + 1, 1, K - 2, [&](int pk, double fp, double& f2, float& g) {
            const int y = idxOf(PB, pk); if (y < 0 || matchB[(size_t) y] < 0) { f2 = fp; g = 1.0f; return true; }
            const double fa = fr(ca, PA[(size_t) matchB[(size_t) y]]); f2 = std::exp(std::log(fa) * (1 - t) + std::log(fp) * t);
            g = (float) std::pow(envAt(m.envA, ia, fa) / envAt(m.envB, ib, fp), 1.0f - t); g = juce::jlimit(0.05f, 8.0f, g); return true; }, dB.data());
        Cell* o = out.col(j);
        for (int k = 1; k < K - 1; ++k) {
            const float wa = dA[(size_t) k].mag * (1 - t), wb = dB[(size_t) k].mag * t;
            if (wa + wb <= 0) continue;
            o[k] = wa >= wb ? dA[(size_t) k] : dB[(size_t) k]; o[k].mag = wa + wb; o[k].kind = Moved;
        }
    }
}
} // namespace lab
