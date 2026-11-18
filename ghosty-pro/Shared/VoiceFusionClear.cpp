#include "VoiceFusionClear.h"
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <algorithm>
#include <random>

namespace fusion::clear {
namespace {
const double kPi = juce::MathConstants<double>::pi;
inline double gridStep() { return std::log(kGridHi / kGridLo) / (kGrid - 1); }
template <typename T> double at(const T* v, double pos) {
    pos = juce::jlimit(0.0, (double) kGrid - 1, pos); const int i = (int) pos; const int j = std::min(kGrid - 1, i + 1); const double f = pos - i;
    return (double) v[i] + ((double) v[j] - (double) v[i]) * f;
}
} // namespace

int bandLo() { static const int v = (int) std::ceil(gridPos(100.0)); return v; }
int bandHi() { static const int v = (int) std::floor(gridPos(8000.0)); return v; }
void shiftGrid(const double* in, double u, double* out) { const double d = u / gridStep(); for (int i = 0; i < kGrid; ++i) out[i] = at(in, i - d); }
double bandMean(const double* g) { double s = 0; for (int i = bandLo(); i <= bandHi(); ++i) s += g[i]; return s / (bandHi() - bandLo() + 1); }

std::array<double, kGrid> meanEnvelope(const Identity& id) {
    std::array<double, kGrid> m {}; double ws = 0;
    for (const auto& fr : id.frames) { const double w = std::max(1e-4f, fr.weight); for (int i = 0; i < kGrid; ++i) m[(size_t) i] += w * fr.logEnv[(size_t) i]; ws += w; }
    if (ws > 0) for (auto& v : m) v /= ws;
    return m;
}

double tractShift(const Identity& ref, const Identity& other) {
    if (ref.empty() || other.empty()) return 0.0;
    const auto a = meanEnvelope(ref), b = meanEnvelope(other);
    const int i0 = (int) std::ceil(gridPos(200.0)), i1 = (int) std::floor(gridPos(5000.0));
    auto detrend = [&](const std::array<double, kGrid>& g) {   // the overall tilt is colour, not size
        std::array<double, kGrid> o = g; double sx = 0, sy = 0, sxx = 0, sxy = 0; int n = 0;
        for (int i = i0; i <= i1; ++i) { sx += i; sy += g[(size_t) i]; sxx += (double) i * i; sxy += i * g[(size_t) i]; ++n; }
        const double k = (n * sxy - sx * sy) / std::max(1e-9, n * sxx - sx * sx), c = (sy - k * sx) / n;
        for (int i = 0; i < kGrid; ++i) o[(size_t) i] = g[(size_t) i] - (c + k * i); return o; };
    const auto da = detrend(a), db = detrend(b);
    const double maxU = std::log(std::exp2(8.0 / 12.0)); double best = 0, bestScore = -1e18;
    for (double u = -maxU; u <= maxU + 1e-9; u += gridStep() * 0.25) {
        std::array<double, kGrid> s {}; shiftGrid(da.data(), u, s.data());
        double sab = 0, saa = 0, sbb = 0;
        for (int i = i0; i <= i1; ++i) { sab += s[(size_t) i] * db[(size_t) i]; saa += s[(size_t) i] * s[(size_t) i]; sbb += db[(size_t) i] * db[(size_t) i]; }
        const double score = sab / std::sqrt(std::max(1e-12, saa * sbb)) - 0.15 * std::abs(u) / maxU;
        if (score > bestScore) { bestScore = score; best = u; }
    }
    const double fit = std::log(std::max(0.4f, other.formantScale) / std::max(0.4f, ref.formantScale));   // the fitted vowel-table scales
    if (std::isfinite(fit) && std::abs(fit - best) < std::log(std::exp2(4.0 / 12.0))) best = 0.5 * (best + fit);
    return juce::jlimit(-maxU, maxU, best);
}

void slerp(const double* x, const double* y, int n, double t, double* out, double* angle) {
    double nx = 0, ny = 0, d = 0; for (int i = 0; i < n; ++i) { nx += x[i] * x[i]; ny += y[i] * y[i]; d += x[i] * y[i]; }
    nx = std::sqrt(nx); ny = std::sqrt(ny);
    if (nx < 1e-9 || ny < 1e-9) { for (int i = 0; i < n; ++i) out[i] = (1 - t) * x[i] + t * y[i]; if (angle) *angle = 0; return; }
    const double c = juce::jlimit(-1.0, 1.0, d / (nx * ny)), th = std::acos(c); if (angle) *angle = th;
    const double len = (1 - t) * nx + t * ny;
    if (th < 1e-4) { for (int i = 0; i < n; ++i) out[i] = ((1 - t) * x[i] / nx + t * y[i] / ny) * len; return; }
    const double s = std::sin(th), ka = std::sin((1 - t) * th) / s, kb = std::sin(t * th) / s;
    for (int i = 0; i < n; ++i) out[i] = (ka * x[i] / nx + kb * y[i] / ny) * len;
}

// ================================================================================================================ STFT shaping
std::vector<float> shape(const voice::AudioRef& x, int start, int length, const std::vector<float>& fill, const Shaping& s,
                         voice::Progress prog, int pFrom, int pTo, bool& cancelled) {
    cancelled = false; std::vector<float> y((size_t) std::max(0, length), 0.0f); if (length <= 0 || !x.valid() || s.frames < 1) return y;
    const int fs = x.fs; int L = 256; while (L < (int) (0.023 * fs)) L *= 2;   // ~23 ms frames (1024 at 44.1 / 48 kHz)
    const int N = 2 * L, order = (int) std::round(std::log2((double) N)), hop = L / 4, nb = N / 2 + 1;
    juce::dsp::FFT fft(order); std::vector<float> buf((size_t) N * 2), wbuf((size_t) N * 2), win((size_t) L);
    for (int n = 0; n < L; ++n) win[(size_t) n] = (float) (0.5 - 0.5 * std::cos(2 * kPi * n / L));
    const double olaNorm = 1.0 / (0.5 * L / hop);
    auto mono = [&](int i) -> float { if (i < 0 || i >= x.n) return 0.0f; return x.ch.size() == 1 ? x.ch[0][i] : 0.5f * (x.ch[0][i] + x.ch[1][i]); };
    const bool haveFill = !fill.empty() && !s.deficit.empty(), haveNoise = !s.noise.empty();
    // bin -> grid position, bin -> breath band position
    std::vector<double> gpos((size_t) nb), bpos((size_t) nb);
    for (int k = 0; k < nb; ++k) { const double hz = k * fs / (double) N; gpos[(size_t) k] = gridPos(std::max(kGridLo, std::min(kGridHi, hz)));
        bpos[(size_t) k] = hz <= bandHz(0) ? 0.0 : hz >= bandHz(kApBands - 1) ? kApBands - 1.0 : (kApBands - 1) * std::log2(hz / 100.0) / std::log2(12000.0 / 100.0); }
    std::vector<double> g((size_t) nb), d((size_t) nb), na((size_t) nb), pw((size_t) nb);
    std::mt19937 rng(0x5eed); std::normal_distribution<float> gauss(0.0f, 1.0f);
    const int total = (length + L) / hop + 1;
    for (int f = 0; f < total; ++f) {
        if ((f & 255) == 0) { if (prog.stop()) { cancelled = true; return {}; } prog.set(pFrom + (int) ((pTo - pFrom) * f / (double) total)); }
        const int c = f * hop - L / 2;
        const double tc = (c + L / 2) / (double) fs * 1000.0 / s.fp; const int i = juce::jlimit(0, s.frames - 1, (int) tc); const int j = std::min(s.frames - 1, i + 1); const double a = juce::jlimit(0.0, 1.0, tc - i);
        const float* gi = s.gain.data() + (size_t) i * kGrid; const float* gj = s.gain.data() + (size_t) j * kGrid;
        double dmax = 0, nmax = 0;
        for (int k = 0; k < nb; ++k) { const double p = gpos[(size_t) k]; g[(size_t) k] = std::exp((1 - a) * at(gi, p) + a * at(gj, p));
            if (haveFill) { d[(size_t) k] = (1 - a) * at(s.deficit.data() + (size_t) i * kGrid, p) + a * at(s.deficit.data() + (size_t) j * kGrid, p); dmax = std::max(dmax, d[(size_t) k]); }
            if (haveNoise) { const double q = bpos[(size_t) k]; const int b0 = (int) q, b1 = std::min(kApBands - 1, b0 + 1); const double fq = q - b0;
                const double ni = s.noise[(size_t) i * kApBands + b0] * (1 - fq) + s.noise[(size_t) i * kApBands + b1] * fq, nj = s.noise[(size_t) j * kApBands + b0] * (1 - fq) + s.noise[(size_t) j * kApBands + b1] * fq;
                na[(size_t) k] = (1 - a) * ni + a * nj; nmax = std::max(nmax, na[(size_t) k]); } }
        std::fill(buf.begin(), buf.end(), 0.0f);
        bool any = false; for (int n = 0; n < L; ++n) { const float v = mono(start + c + n) * win[(size_t) n]; buf[(size_t) (n + L / 2)] = v; any = any || v != 0.0f; }
        const bool useFill = haveFill && dmax > 1e-3;
        if (useFill) { std::fill(wbuf.begin(), wbuf.end(), 0.0f); for (int n = 0; n < L; ++n) { const int o = c + n; wbuf[(size_t) (n + L / 2)] = (o >= 0 && o < (int) fill.size()) ? fill[(size_t) o] * win[(size_t) n] : 0.0f; } }
        if (!any && !useFill) continue;
        fft.performRealOnlyForwardTransform(buf.data(), true);
        if (useFill) fft.performRealOnlyForwardTransform(wbuf.data(), true);
        if (haveNoise && nmax > 1e-4) {   // the local spectral power of the shaped frame (smoothed over +-4 bins) = the noise's scale
            for (int k = 0; k < nb; ++k) pw[(size_t) k] = (double) buf[(size_t) (2 * k)] * buf[(size_t) (2 * k)] + (double) buf[(size_t) (2 * k + 1)] * buf[(size_t) (2 * k + 1)];
            double run = 0; std::vector<double> cs((size_t) nb + 1, 0.0); for (int k = 0; k < nb; ++k) { run += pw[(size_t) k]; cs[(size_t) k + 1] = run; }
            for (int k = 0; k < nb; ++k) { const int lo = std::max(0, k - 4), hi = std::min(nb - 1, k + 4); pw[(size_t) k] = (cs[(size_t) hi + 1] - cs[(size_t) lo]) / (hi - lo + 1); }
        }
        for (int k = 0; k < nb; ++k) {
            const float gk = (float) g[(size_t) k]; float re = buf[(size_t) (2 * k)] * gk, im = buf[(size_t) (2 * k + 1)] * gk;
            if (useFill && d[(size_t) k] > 1e-4) { const float h = (float) std::sqrt(d[(size_t) k]); re += h * wbuf[(size_t) (2 * k)]; im += h * wbuf[(size_t) (2 * k + 1)]; }
            if (haveNoise && na[(size_t) k] > 1e-5) { const float sc = (float) (gk * std::sqrt(na[(size_t) k] * pw[(size_t) k] * 0.5)); re += sc * gauss(rng); im += sc * gauss(rng); }
            buf[(size_t) (2 * k)] = re; buf[(size_t) (2 * k + 1)] = im;
        }
        fft.performRealOnlyInverseTransform(buf.data());
        for (int n = 0; n < N; ++n) { const int o = c - L / 2 + n; if (o >= 0 && o < length) y[(size_t) o] += (float) (buf[(size_t) n] * olaNorm); }
    }
    return y;
}

// ================================================================================================================ TD-PSOLA
std::vector<float> psola(const std::vector<float>& x, int fs, const std::vector<double>& f0, double fp,
                         const std::function<double(int)>& ratio, float grit, unsigned seed) {
    const int L = (int) x.size(); std::vector<float> y((size_t) L, 0.0f); if (L == 0 || f0.empty()) return y;
    const int F = (int) f0.size(); auto frameAt = [&](double n) { return juce::jlimit(0, F - 1, (int) std::lround(n / fs * 1000.0 / fp)); };
    const int hopU = std::max(16, (int) (0.005 * fs));
    // a lightly smoothed copy for the pitch marks (peaks of the low band are stable glottal landmarks)
    std::vector<float> lp((size_t) L); { const double al = 1.0 - std::exp(-2 * kPi * 900.0 / fs); double s1 = 0, s2 = 0; for (int n = 0; n < L; ++n) { s1 += al * (x[(size_t) n] - s1); s2 += al * (s1 - s2); lp[(size_t) n] = (float) s2; } }
    struct Mark { int pos; int period; bool voiced; };
    std::vector<Mark> marks; marks.reserve((size_t) (L / hopU * 2 + 16));
    { int t = 0; bool prevV = false;
      while (t < L) {
          const double hz = f0[(size_t) frameAt(t)];
          if (hz > 0) {
              const int T = std::max(8, (int) std::lround(fs / hz)); int pos;
              if (!prevV) { int best = t; float bv = -1e9f; for (int n = t; n < std::min(L, t + T); ++n) if (lp[(size_t) n] > bv) { bv = lp[(size_t) n]; best = n; } pos = best; }
              else { const int c = marks.back().pos + T, r = std::max(1, T / 4); int best = c; float bv = -1e9f; for (int n = std::max(0, c - r); n <= std::min(L - 1, c + r); ++n) if (lp[(size_t) n] > bv) { bv = lp[(size_t) n]; best = n; } pos = best; }
              if (!marks.empty() && pos <= marks.back().pos) pos = marks.back().pos + std::max(1, T / 2);
              if (pos >= L) break;
              marks.push_back({ pos, T, true }); prevV = true; t = pos + 1;
          } else { const int pos = marks.empty() ? 0 : std::max(t, marks.back().pos + hopU); if (pos >= L) break; marks.push_back({ pos, hopU, false }); prevV = false; t = pos + 1; }
      } }
    if (marks.empty()) return x;
    std::vector<float> wsum((size_t) L, 0.0f);
    std::mt19937 rng(seed); std::normal_distribution<double> gauss(0.0, 1.0); double jit = 0, shim = 0;
    double ts = marks.front().pos; size_t k = 0;
    while (ts < L + 1) {
        while (k + 1 < marks.size() && std::abs(marks[k + 1].pos - ts) <= std::abs(marks[k].pos - ts)) ++k;
        const Mark& m = marks[k]; const int fr = frameAt(ts);
        double r = m.voiced ? juce::jlimit(0.25, 4.0, ratio(fr)) : 1.0, gain = 1.0;
        if (m.voiced && grit > 0) { jit = 0.7 * jit + 0.3 * gauss(rng); shim = 0.7 * shim + 0.3 * gauss(rng); r *= std::exp2(0.06 * grit * jit); gain *= 1.0 + 0.25 * grit * shim; }
        const int h = m.period;                                        // grain = 2 periods, Hann
        const double Ts = m.voiced ? m.period / r : m.period;
        if (m.voiced && r < 1.0) gain *= std::sqrt(1.0 / r) * 0.5 + 0.5;   // fewer grains per second when lower: keep the level
        const int c = (int) std::lround(ts);
        for (int n = -h; n < h; ++n) {
            const int src = m.pos + n, dst = c + n; if (src < 0 || src >= L || dst < 0 || dst >= L) continue;
            const float w = (float) (0.5 + 0.5 * std::cos(kPi * n / h));
            y[(size_t) dst] += (float) (gain * w * x[(size_t) src]); wsum[(size_t) dst] += w;
        }
        ts += std::max(2.0, Ts);
    }
    // overlap normalisation where grains pile up (higher pitch); gaps between grains (lower pitch) are left as they are
    for (int n = 0; n < L; ++n) if (wsum[(size_t) n] > 1.0f) y[(size_t) n] /= wsum[(size_t) n];
    return y;
}

// ================================================================================================================ loudness
namespace {
struct Biquad { double b0, b1, b2, a1, a2, z1 = 0, z2 = 0; double tick(double x) { const double yv = b0 * x + z1; z1 = b1 * x - a1 * yv + z2; z2 = b2 * x - a2 * yv; return yv; } };
std::vector<double> kWeighted(const std::vector<float>& x, int fs) {
    Biquad s1 {}, s2 {};
    { const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196, K = std::tan(kPi * f0 / fs), Vh = std::pow(10.0, G / 20), Vb = std::pow(Vh, 0.4996667741545416), a0 = 1 + K / Q + K * K;
      s1.b0 = (Vh + Vb * K / Q + K * K) / a0; s1.b1 = 2 * (K * K - Vh) / a0; s1.b2 = (Vh - Vb * K / Q + K * K) / a0; s1.a1 = 2 * (K * K - 1) / a0; s1.a2 = (1 - K / Q + K * K) / a0; }
    { const double f0 = 38.13547087602444, Q = 0.5003270373238773, K = std::tan(kPi * f0 / fs), a0 = 1 + K / Q + K * K;
      s2.b0 = 1 / a0; s2.b1 = -2 / a0; s2.b2 = 1 / a0; s2.a1 = 2 * (K * K - 1) / a0; s2.a2 = (1 - K / Q + K * K) / a0; }
    std::vector<double> y(x.size()); for (size_t n = 0; n < x.size(); ++n) y[n] = s2.tick(s1.tick(std::isfinite(x[n]) ? x[n] : 0.0f)); return y;
}
// a linked stereo look-ahead limiter: gain never lets a sample past `ceiling`; 3 ms look-ahead, 60 ms release
void limit(voice::Rendered& r, double ceiling) {
    const int n = (int) r.L.size(); if (n == 0) return; const int la = std::max(1, (int) (0.003 * r.fs)); const double rel = std::exp(-1.0 / (0.06 * r.fs));
    std::vector<float> req((size_t) n);
    for (int i = 0; i < n; ++i) { double pk = std::abs(r.L[(size_t) i]); if (r.stereo()) pk = std::max(pk, (double) std::abs(r.R[(size_t) i])); req[(size_t) i] = (float) (pk > ceiling ? ceiling / pk : 1.0); }
    // the minimum over the look-ahead window (monotone deque), then a smooth release
    std::vector<float> mn((size_t) n); std::vector<int> dq; dq.reserve((size_t) la * 2 + 4); size_t head = 0;
    for (int i = 0; i < n + la; ++i) {
        if (i < n) { while (dq.size() > head && req[(size_t) dq.back()] >= req[(size_t) i]) dq.pop_back(); dq.push_back(i); }
        const int o = i - la; if (o < 0) continue; while (dq[head] < o - la) ++head; mn[(size_t) o] = req[(size_t) dq[head]];
        if (head > 4096) { dq.erase(dq.begin(), dq.begin() + (long) head); head = 0; }
    }
    // a box average of length la over the windowed minimum = a smooth ramp that still reaches the minimum at the peak
    { std::vector<float> sm((size_t) n); double acc = 0; const int h = la / 2;
      for (int i = 0; i < n + h; ++i) { if (i < n) acc += mn[(size_t) i]; else acc += 1.0; const int drop = i - la; if (drop >= 0) acc -= drop < n ? mn[(size_t) drop] : 1.0;
          const int o = i - h; if (o >= 0 && o < n) sm[(size_t) o] = (float) std::min<double>(1.0, acc / std::min(la, i + 1)); }
      mn.swap(sm); }
    double g = 1.0;
    for (int i = 0; i < n; ++i) { const double t = mn[(size_t) i]; g = t < g ? t : t + (g - t) * rel; r.L[(size_t) i] = (float) (r.L[(size_t) i] * g); if (r.stereo()) r.R[(size_t) i] = (float) (r.R[(size_t) i] * g); }
}
} // namespace

double integratedLufs(const voice::Rendered& r) {
    if (r.fs <= 0 || r.L.empty()) return -100.0;
    const auto kl = kWeighted(r.L, r.fs); std::vector<double> kr; if (r.stereo()) kr = kWeighted(r.R, r.fs);
    const int win = (int) (0.4 * r.fs), hop = (int) (0.1 * r.fs); if ((int) kl.size() < win) return -100.0;
    std::vector<double> z;
    for (size_t s = 0; s + (size_t) win <= kl.size(); s += (size_t) hop) {
        double e = 0; for (int n = 0; n < win; ++n) e += kl[s + (size_t) n] * kl[s + (size_t) n];
        if (!kr.empty()) for (int n = 0; n < win; ++n) e += kr[s + (size_t) n] * kr[s + (size_t) n];
        z.push_back(e / win);
    }
    auto lk = [](double e) { return -0.691 + 10.0 * std::log10(std::max(1e-30, e)); };
    double s = 0; int c = 0; for (double e : z) if (lk(e) > -70) { s += e; ++c; } if (c == 0) return -100.0;
    const double rel = lk(s / c) - 10.0; s = 0; c = 0; for (double e : z) if (lk(e) > -70 && lk(e) > rel) { s += e; ++c; }
    return c ? lk(s / c) : -100.0;
}

void finaliseLoudness(voice::Rendered& out, bool normalise, float gainDb) {
    for (auto& v : out.L) if (!std::isfinite(v)) v = 0; for (auto& v : out.R) if (!std::isfinite(v)) v = 0;
    const double ceiling = 0.89;   // -1 dBFS
    if (normalise) {
        const voice::Rendered src = out; double gDb = 0;
        for (int it = 0; it < 4; ++it) {   // gain -> limiter -> measure, until the limited result sits on the target
            out = src; const double g = std::pow(10.0, gDb / 20.0); for (auto& v : out.L) v = (float) (v * g); for (auto& v : out.R) v = (float) (v * g);
            limit(out, ceiling); const double l = integratedLufs(out); if (l < -99) break;
            const double err = kTargetLufs - l; if (std::abs(err) < 0.05) break; gDb += err;
        }
    }
    const double g = juce::Decibels::decibelsToGain(juce::jlimit(-48.0f, 24.0f, gainDb));
    if (std::abs(g - 1.0) > 1e-9) { for (auto& v : out.L) v = (float) (v * g); for (auto& v : out.R) v = (float) (v * g); }
    limit(out, ceiling);
}

} // namespace fusion::clear
