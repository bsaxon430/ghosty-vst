#include "VoiceFusion.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <random>

namespace fusion {
namespace {
const double kPi = juce::MathConstants<double>::pi;
constexpr double kLnFloor = -46.0;                                  // ~ -200 dB: the log-power floor
constexpr double kDbToLn = 2.302585092994046 / 10.0;                 // power dB -> natural log (ln 10 / 10; a literal: std::log is not constexpr on clang)
inline double lnp(double p) { return std::log(std::max(1e-20, p)); }
inline double clampLn(double l) { return std::isfinite(l) ? std::max(kLnFloor, l) : kLnFloor; }
int gridLo200 = -1, gridHi4k = -1;                                  // the level window on the grid
void gridWindow() { if (gridLo200 < 0) { gridLo200 = (int) std::ceil(gridPos(200.0)); gridHi4k = (int) std::floor(gridPos(4000.0)); } }
double levelOf(const double* g) { gridWindow(); double s = 0; for (int i = gridLo200; i <= gridHi4k; ++i) s += g[i]; return s / (gridHi4k - gridLo200 + 1); }
template <typename T> double interpAt(const T* v, int n, double pos) {
    pos = juce::jlimit(0.0, (double) n - 1, pos); const int i = (int) pos; const int j = std::min(n - 1, i + 1); const double f = pos - i;
    return (double) v[i] + ((double) v[j] - (double) v[i]) * f;
}
// a piecewise-linear frequency map through (x_k, y_k), identity outside; monotonic in both. Evaluates y(x) and x(y).
struct Warp {
    std::vector<double> xs, ys;
    void add(double x, double y) { xs.push_back(x); ys.push_back(y); }
    static double eval(const std::vector<double>& from, const std::vector<double>& to, double v) {
        if (from.empty()) return v;
        if (v <= from.front()) return to.front() + (v - from.front());
        if (v >= from.back()) return to.back() + (v - from.back());
        auto it = std::upper_bound(from.begin(), from.end(), v); const size_t j = (size_t) (it - from.begin()); const size_t i = j - 1;
        const double span = from[j] - from[i]; const double f = span > 1e-9 ? (v - from[i]) / span : 0.0; return to[i] + (to[j] - to[i]) * f;
    }
    double forward(double x) const { return eval(xs, ys, x); }
    double inverse(double y) const { return eval(ys, xs, y); }
};
// the formant anchors of n voices: voice 0's peaks, each paired with ONE peak of every other voice by a monotone nearest match
// (within ~7 semitones); an anchor needs a partner in every voice (dynamic-frequency-warping style: unpaired peaks just ride along)
struct Anchors { int m = 0; double hz[8][voice::kFormants] {}; };   // hz[v][j] = voice v's frequency for anchor j
Anchors findAnchors(const voice::Formants* F, int n) {
    Anchors A; if (n <= 0) return A;
    std::vector<double> ref; for (int j = 0; j < voice::kFormants; ++j) { const double f = F[0][(size_t) j].hz; if (f > 0 && std::isfinite(f) && (ref.empty() || f > ref.back() * 1.08)) ref.push_back(f); }
    std::vector<std::vector<double>> part((size_t) n, std::vector<double>(ref.size(), 0.0));
    for (size_t j = 0; j < ref.size(); ++j) part[0][j] = ref[j];
    for (int v = 1; v < n; ++v) {
        std::vector<double> cand; for (int j = 0; j < voice::kFormants; ++j) { const double f = F[v][(size_t) j].hz; if (f > 0 && std::isfinite(f) && (cand.empty() || f > cand.back() * 1.08)) cand.push_back(f); }
        size_t c = 0;
        for (size_t j = 0; j < ref.size() && c < cand.size(); ++j) {   // monotone greedy: the nearest unused candidate, if within tolerance
            size_t best = c; double bd = 1e9; for (size_t k = c; k < cand.size(); ++k) { const double d = std::abs(std::log(cand[k] / ref[j])); if (d < bd) { bd = d; best = k; } }
            if (bd < 0.7) { part[(size_t) v][j] = cand[best]; c = best + 1; }
        }
    }
    for (size_t j = 0; j < ref.size() && A.m < voice::kFormants; ++j) {
        bool all = true; for (int v = 0; v < n; ++v) all = all && part[(size_t) v][j] > 0;
        if (!all) continue;
        bool mono = true; for (int v = 0; v < n; ++v) if (A.m > 0 && part[(size_t) v][j] <= A.hz[v][A.m - 1] * 1.05) mono = false;
        if (!mono) continue;
        for (int v = 0; v < n; ++v) A.hz[v][A.m] = part[(size_t) v][j]; ++A.m;
    }
    return A;
}
} // namespace

// ================================================================================================================ formants (LPC on the envelope)
namespace {
bool levinson(const std::vector<double>& r, int order, std::vector<double>& a) {
    a.assign((size_t) order + 1, 0.0); a[0] = 1.0; double err = r[0]; if (err <= 0) return false; std::vector<double> tmp((size_t) order + 1);
    for (int i = 1; i <= order; ++i) {
        double acc = r[(size_t) i]; for (int j = 1; j < i; ++j) acc += a[(size_t) j] * r[(size_t) (i - j)];
        const double k = -acc / err; if (!std::isfinite(k) || std::abs(k) >= 1.0) return i > 2;
        tmp = a; for (int j = 1; j < i; ++j) a[(size_t) j] = tmp[(size_t) j] + k * tmp[(size_t) (i - j)]; a[(size_t) i] = k; err *= (1.0 - k * k); if (err <= 1e-12 * r[0]) break;
    }
    return true;
}
} // namespace
voice::Formants formantsLpc(const double* spec, int bins, int fs, double f0) {
    voice::Formants out {}; const double binHz = fs / (2.0 * (bins - 1)), fMax = std::min(5500.0, fs * 0.45); const int kMax = std::max(8, std::min(bins - 1, (int) (fMax / binHz)));
    double peak = 0; for (int k = 0; k <= kMax; ++k) peak = std::max(peak, spec[k]); if (!(peak > 0)) return out;
    // autocorrelation of the (pre-emphasised) power envelope = an all-pole fit of the envelope itself
    constexpr int order = 14; std::vector<double> P((size_t) kMax + 1), r((size_t) order + 1, 0.0);
    for (int k = 0; k <= kMax; ++k) { const double pre = 1.0 + 0.97 * 0.97 - 2.0 * 0.97 * std::cos(kPi * k / kMax); P[(size_t) k] = std::max(spec[k], peak * 1e-9) * pre * ((k == 0 || k == kMax) ? 0.5 : 1.0); }
    for (int l = 0; l <= order; ++l) { double s = 0; for (int k = 0; k <= kMax; ++k) s += P[(size_t) k] * std::cos(kPi * k * l / kMax); r[(size_t) l] = s; }
    for (size_t i = 0; i < r.size(); ++i) r[i] *= std::exp(-0.5 * std::pow(0.002 * (double) i, 2)) * (i == 0 ? 1.0001 : 1.0);   // a little lag window + white-noise correction
    std::vector<double> a; if (!levinson(r, order, a)) return voice::formantsOf(spec, bins, fs, f0);
    const double fTop = kMax * binHz; constexpr int M = 1024; std::vector<double> H((size_t) M);
    for (int m = 0; m < M; ++m) { const double w = kPi * (m + 0.5) / M; double re = 0, im = 0; for (int i = 0; i <= order; ++i) { re += a[(size_t) i] * std::cos(w * i); im -= a[(size_t) i] * std::sin(w * i); } H[(size_t) m] = -10.0 * std::log10(std::max(1e-12, re * re + im * im)); }
    struct Pk { double f, db, bw; }; std::vector<Pk> pk; const double fMin = std::max(150.0, 1.2 * f0);
    for (int m = 1; m < M - 1; ++m) {
        if (!(H[(size_t) m] > H[(size_t) m - 1] && H[(size_t) m] >= H[(size_t) m + 1])) continue;
        const double y0 = H[(size_t) m - 1], y1 = H[(size_t) m], y2 = H[(size_t) m + 1], den = y0 - 2 * y1 + y2; const double off = std::abs(den) > 1e-12 ? juce::jlimit(-0.5, 0.5, 0.5 * (y0 - y2) / den) : 0.0;
        const double f = (m + 0.5 + off) / M * fTop; if (f < fMin || f > fTop - 100.0) continue;
        int lo = m, hi = m; while (lo > 0 && H[(size_t) lo] > y1 - 3.0) --lo; while (hi < M - 1 && H[(size_t) hi] > y1 - 3.0) ++hi;
        const double bw = std::max(20.0, (hi - lo) / (double) M * fTop); if (bw > 700.0) continue;   // too broad to be a formant
        pk.push_back({ f, y1, bw });
    }
    // merge with the envelope's own clear peaks (both agree on most vowels; the LPC splits merged F1 / F2, the envelope confirms F3 / F4)
    const auto env = voice::formantsOf(spec, bins, fs, f0);
    for (auto& e : env) { if (e.hz <= 0) continue; bool near = false; for (auto& q : pk) near = near || std::abs(q.f - e.hz) < std::max(120.0, 0.6 * e.bwHz); if (!near) pk.push_back({ e.hz, e.dB, e.bwHz }); }
    std::sort(pk.begin(), pk.end(), [](const Pk& x, const Pk& y) { return x.f < y.f; });
    std::vector<Pk> merged; for (auto& q : pk) { if (!merged.empty() && q.f - merged.back().f < 110.0) { if (q.db > merged.back().db) merged.back() = q; } else merged.push_back(q); }
    for (size_t i = 0; i < merged.size() && i < (size_t) voice::kFormants; ++i) out[i] = { (float) merged[i].f, (float) merged[i].bw, (float) merged[i].db };
    return out;
}

// ================================================================================================================ grid <-> bins
double toGrid(const double* spec, int bins, int fs, double* out) {
    const double binHz = fs / (2.0 * (bins - 1));
    for (int i = 0; i < kGrid; ++i) { const double pos = gridHz(i) / binHz; if (pos >= bins - 1) { out[i] = lnp(spec[bins - 1]); continue; }
        const int k = (int) pos; const double f = pos - k; out[i] = clampLn(lnp(spec[k]) + (lnp(spec[k + 1]) - lnp(spec[k])) * f); }
    const double lvl = levelOf(out); for (int i = 0; i < kGrid; ++i) out[i] -= lvl; return lvl;
}
void fromGrid(const double* g, int bins, int fs, double* out) {
    const double binHz = fs / (2.0 * (bins - 1));
    for (int k = 0; k < bins; ++k) {
        const double hz = k * binHz;
        if (hz <= kGridLo) out[k] = g[0];
        else if (hz >= kGridHi) out[k] = g[kGrid - 1] - 12.0 * kDbToLn * std::log2(hz / kGridHi);   // -12 dB / oct above the grid
        else out[k] = interpAt(g, kGrid, gridPos(hz));
    }
}

// ================================================================================================================ morph
void morphGrid(const std::array<double, kGrid>* L, const voice::Formants* F, const double* w, int n, double* out, voice::Formants* outF) {
    std::fill(out, out + kGrid, 0.0);
    if (n <= 0 || n > 8) return;
    const Anchors A = findAnchors(F, n); const int m = A.m;
    double T[voice::kFormants] {};
    for (int j = 0; j < m; ++j) { double s = 0; for (int v = 0; v < n; ++v) s += w[v] * std::log(A.hz[v][j]); T[j] = std::exp(s); }
    double topT = 5500.0; for (int j = 0; j < m; ++j) topT = std::max(topT, T[j] * 1.25);
    for (int v = 0; v < n; ++v) {
        if (w[v] <= 1e-6) continue;
        Warp wp; wp.add(0, 0);
        double top = 5500.0; for (int j = 0; j < m; ++j) top = std::max(top, A.hz[v][j] * 1.25);
        for (int j = 0; j < m; ++j) wp.add(A.hz[v][j], T[j]);
        const double topBoth = std::max(top, topT); wp.add(topBoth, topBoth);
        for (int i = 0; i < kGrid; ++i) { const double src = m > 0 ? wp.inverse(gridHz(i)) : gridHz(i); out[i] += w[v] * interpAt(L[v].data(), kGrid, gridPos(src)); }
    }
    if (outF) {   // the anchored formants at their targets; the rest of voice 0's peaks where the warp puts them
        voice::Formants o {}; int q = 0;
        Warp w0; w0.add(0, 0); for (int j = 0; j < m; ++j) w0.add(A.hz[0][j], T[j]); { double top = 5500.0; for (int j = 0; j < m; ++j) top = std::max(top, std::max(A.hz[0][j], T[j]) * 1.25); w0.add(top, top); }
        for (int j = 0; j < voice::kFormants && q < voice::kFormants; ++j) { const auto& f = F[0][(size_t) j]; if (!(f.hz > 0)) continue; o[(size_t) q] = f; o[(size_t) q].hz = (float) (m > 0 ? w0.forward(f.hz) : f.hz); ++q; }
        *outF = o;
    }
}

// ================================================================================================================ identity
namespace {
std::array<float, kApBands> apBands(const double* ap, int bins, int fs) {
    std::array<float, kApBands> o {}; const double binHz = fs / (2.0 * (bins - 1));
    for (int b = 0; b < kApBands; ++b) {
        const double lo = b == 0 ? 0.0 : std::sqrt(bandHz(b - 1) * bandHz(b)), hi = b == kApBands - 1 ? fs * 0.5 : std::sqrt(bandHz(b) * bandHz(b + 1));
        const int k0 = std::max(0, (int) (lo / binHz)), k1 = std::min(bins - 1, std::max(k0, (int) (hi / binHz))); double s = 0; int c = 0;
        for (int k = k0; k <= k1; ++k) { s += juce::jlimit(0.0, 1.0, ap[k]); ++c; } o[(size_t) b] = c ? (float) (s / c) : 0.5f;
    }
    return o;
}
double apAt(const std::array<float, kApBands>& a, double hz) {
    if (hz <= bandHz(0)) return a[0]; if (hz >= bandHz(kApBands - 1)) return a[kApBands - 1];
    const double pos = (kApBands - 1) * std::log2(hz / 100.0) / std::log2(12000.0 / 100.0); return interpAt(a.data(), kApBands, pos);
}
double tiltOf(const double* g) {   // dB / oct, least squares over 300 Hz .. 3 kHz on the grid
    double sx = 0, sy = 0, sxx = 0, sxy = 0; int n = 0;
    for (int i = 0; i < kGrid; ++i) { const double hz = gridHz(i); if (hz < 300 || hz > 3000) continue; const double x = std::log2(hz / 1000.0), y = g[i] / kDbToLn; sx += x; sy += y; sxx += x * x; sxy += x * y; ++n; }
    const double den = n * sxx - sx * sx; return n > 2 && std::abs(den) > 1e-9 ? (n * sxy - sx * sy) / den : 0.0;
}
struct Feat { double x[3]; };
Feat featOf(const voice::Formants& F) {
    Feat f; f.x[0] = std::log(F[0].hz > 0 ? F[0].hz : 500.0); f.x[1] = std::log(F[1].hz > 0 ? F[1].hz : 1500.0); f.x[2] = 0.5 * std::log(F[2].hz > 0 ? F[2].hz : 2500.0); return f;
}
double featDist(const Feat& a, const Feat& b) { double d = 0; for (int i = 0; i < 3; ++i) d += (a.x[i] - b.x[i]) * (a.x[i] - b.x[i]); return d; }
} // namespace

Identity extractIdentity(const voice::Analysis& a, float voicing, int maxFrames, double t0, double t1) {
    Identity id; if (a.empty() || a.frames() < 2) return id;
    const auto f0 = voice::gateF0(a.f0Raw, a.periodicity, juce::jlimit(0.05f, 0.95f, voicing));
    const int F = a.frames(); float mx = 0; for (float r : a.rms) mx = std::max(mx, r); const float gate = mx * 0.01f;   // -40 dB
    std::vector<int> idx;
    for (int i = 0; i < F; ++i) {
        const double t = a.frameTime(i); if (t0 >= 0 && t < t0) continue; if (t1 > 0 && t > t1) continue;
        if (!(f0[(size_t) i] > 0) || a.rms[(size_t) i] < gate) continue;
        if (!(a.formants[(size_t) i][0].hz > 0) || !(a.formants[(size_t) i][1].hz > 0)) continue;
        idx.push_back(i);
    }
    if (idx.empty()) {   // no voiced vowel at all (a pad, a noise): one frame of the mean envelope, matched by shape only
        std::vector<double> row((size_t) a.bins()); std::array<double, kGrid> g {}; std::array<double, kGrid> acc {}; int c = 0; std::array<float, kApBands> ap {};
        for (int i = 0; i < F; i += std::max(1, F / 200)) { if (a.rms[(size_t) i] < gate) continue; a.specRow(i, row.data()); toGrid(row.data(), a.bins(), a.fs, g.data()); for (int k = 0; k < kGrid; ++k) acc[(size_t) k] += g[(size_t) k];
            a.apRow(i, row.data()); const auto b = apBands(row.data(), a.bins(), a.fs); for (int k = 0; k < kApBands; ++k) ap[(size_t) k] += b[(size_t) k]; ++c; }
        if (c == 0) return id;
        Frame fr; for (int k = 0; k < kGrid; ++k) fr.logEnv[(size_t) k] = (float) (acc[(size_t) k] / c); for (int k = 0; k < kApBands; ++k) fr.ap[(size_t) k] = ap[(size_t) k] / c;
        fr.F = voice::formantsOf([&] { std::vector<double> p((size_t) a.bins()); std::vector<double> l((size_t) a.bins()); fromGrid(acc.data(), a.bins(), a.fs, l.data()); for (size_t k = 0; k < p.size(); ++k) p[k] = std::exp(l[k] / c); return p; }().data(), a.bins(), a.fs, 0);
        id.frames.push_back(fr); id.medianF0 = (float) a.medianF0(); id.tilt = (float) tiltOf(acc.data()) / c; id.breath = 0.5f * (fr.ap[3] + fr.ap[4]); id.formantScale = 1; return id;
    }
    const int stride = std::max(1, (int) idx.size() / 2000); std::vector<int> use; for (size_t k = 0; k < idx.size(); k += (size_t) stride) use.push_back(idx[k]);
    const int N = (int) use.size();
    std::vector<Feat> feats((size_t) N); std::vector<std::array<float, kGrid>> grids((size_t) N); std::vector<std::array<float, kApBands>> aps((size_t) N);
    std::vector<double> row((size_t) a.bins()); std::array<double, kGrid> g {}; std::vector<voice::Formants> fmts((size_t) N);
    for (int n = 0; n < N; ++n) { const int i = use[(size_t) n];
        a.specRow(i, row.data()); fmts[(size_t) n] = formantsLpc(row.data(), a.bins(), a.fs, f0[(size_t) i]); if (!(fmts[(size_t) n][0].hz > 0)) fmts[(size_t) n] = a.formants[(size_t) i]; feats[(size_t) n] = featOf(fmts[(size_t) n]);
        toGrid(row.data(), a.bins(), a.fs, g.data()); for (int k = 0; k < kGrid; ++k) grids[(size_t) n][(size_t) k] = (float) g[(size_t) k];
        a.apRow(i, row.data()); aps[(size_t) n] = apBands(row.data(), a.bins(), a.fs); }
    // k-means on (log F1, log F2, 0.5 log F3): farthest-point seeds, 12 rounds
    const int K = juce::jlimit(1, std::max(1, maxFrames), N / 8);
    std::vector<Feat> cen; cen.push_back(feats[(size_t) (N / 2)]);
    std::vector<double> best((size_t) N, 1e9);
    while ((int) cen.size() < K) { int far = 0; double fd = -1; for (int n = 0; n < N; ++n) { best[(size_t) n] = std::min(best[(size_t) n], featDist(feats[(size_t) n], cen.back())); if (best[(size_t) n] > fd) { fd = best[(size_t) n]; far = n; } } if (fd < 1e-4) break; cen.push_back(feats[(size_t) far]); }
    std::vector<int> label((size_t) N, 0);
    for (int it = 0; it < 12; ++it) {
        for (int n = 0; n < N; ++n) { int b = 0; double bd = 1e9; for (size_t c = 0; c < cen.size(); ++c) { const double d = featDist(feats[(size_t) n], cen[c]); if (d < bd) { bd = d; b = (int) c; } } label[(size_t) n] = b; }
        std::vector<Feat> acc(cen.size(), Feat { { 0, 0, 0 } }); std::vector<int> cnt(cen.size(), 0);
        for (int n = 0; n < N; ++n) { auto& A = acc[(size_t) label[(size_t) n]]; for (int i = 0; i < 3; ++i) A.x[i] += feats[(size_t) n].x[i]; ++cnt[(size_t) label[(size_t) n]]; }
        for (size_t c = 0; c < cen.size(); ++c) if (cnt[c] > 0) for (int i = 0; i < 3; ++i) cen[c].x[i] = acc[c].x[i] / cnt[c];
    }
    std::array<double, kGrid> all {}; double tiltSum = 0; double breathSum = 0;
    for (size_t c = 0; c < cen.size(); ++c) {
        std::vector<int> members; for (int n = 0; n < N; ++n) if (label[(size_t) n] == (int) c) members.push_back(n);
        if (members.empty()) continue;
        Frame fr; std::array<double, kGrid> acc {}; std::array<double, kApBands> apAcc {}; std::vector<float> fh[voice::kFormants], fb[voice::kFormants], fd[voice::kFormants], f0s;
        for (int n : members) { for (int k = 0; k < kGrid; ++k) acc[(size_t) k] += grids[(size_t) n][(size_t) k]; for (int k = 0; k < kApBands; ++k) apAcc[(size_t) k] += aps[(size_t) n][(size_t) k];
            const auto& Fm = fmts[(size_t) n]; for (int j = 0; j < voice::kFormants; ++j) if (Fm[(size_t) j].hz > 0) { fh[j].push_back(Fm[(size_t) j].hz); fb[j].push_back(Fm[(size_t) j].bwHz); fd[j].push_back(Fm[(size_t) j].dB); }
            f0s.push_back((float) f0[(size_t) use[(size_t) n]]); }
        const double inv = 1.0 / (double) members.size();
        for (int k = 0; k < kGrid; ++k) { fr.logEnv[(size_t) k] = (float) (acc[(size_t) k] * inv); all[(size_t) k] += acc[(size_t) k]; }
        for (int k = 0; k < kApBands; ++k) fr.ap[(size_t) k] = (float) (apAcc[(size_t) k] * inv);
        for (int j = 0; j < voice::kFormants; ++j) if (fh[j].size() >= std::max<size_t>(1, members.size() / 2)) {   // a formant the cluster really has
            auto med = [](std::vector<float>& v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; };
            fr.F[(size_t) j] = { med(fh[j]), med(fb[j]), med(fd[j]) }; }
        std::sort(f0s.begin(), f0s.end()); fr.f0 = f0s[f0s.size() / 2]; fr.weight = (float) members.size() / (float) N;
        breathSum += 0.5 * (fr.ap[3] + fr.ap[4]) * fr.weight;
        id.frames.push_back(fr);
    }
    for (auto& v : all) v /= N; tiltSum = tiltOf(all.data());
    std::sort(id.frames.begin(), id.frames.end(), [](const Frame& x, const Frame& y) { return x.F[0].hz < y.F[0].hz; });
    id.medianF0 = (float) a.medianF0(); id.tilt = (float) tiltSum; id.breath = (float) breathSum; id.formantScale = voice::fitVowelScale(a);
    return id;
}

// ================================================================================================================ designed identities
const std::vector<Design>& factoryDesigns() {
    static const std::vector<Design> d {
        //  name              blurb                                                     set  scale  F1    F2    F3    F4   bw    tilt  breath ring chest  f0
        { "Glass Angel",   "airy, bright, delicate",                                    1, 1.06f,  0.0f, 1.0f, 2.0f, 2.0f, 0.85f,  3.0f, 0.35f, 4.0f, 0.0f, 330 },
        { "Midnight",      "dark, warm, intimate",                                      0, 0.94f, -1.0f,-1.5f, 0.0f, 0.0f, 1.10f, -4.0f, 0.25f, 0.0f, 5.0f, 110 },
        { "Electric Soul", "powerful, textured, a ring on top",                          0, 1.00f,  0.0f, 1.0f, 1.0f, 0.0f, 0.75f,  1.0f, 0.08f, 7.0f, 3.0f, 165 },
        { "Dream Machine", "soft, breathy, atmospheric",                                 1, 1.02f,  0.0f, 0.0f, 0.0f, 0.0f, 1.35f, -1.0f, 0.55f, 1.0f, 1.0f, 260 },
        { "Velvet",        "smooth, round, close",                                       1, 0.97f,  0.0f,-1.0f, 0.0f, 0.0f, 1.20f, -3.0f, 0.30f, 2.0f, 4.0f, 200 },
        { "Tiny",          "a very small singer (strange)",                              2, 1.25f,  0.0f, 0.0f, 0.0f, 0.0f, 0.90f,  2.0f, 0.20f, 2.0f, 0.0f, 420 },
        { "Giant",         "a huge voice from a huge chest (strange)",                   0, 0.72f, -2.0f, 0.0f, 0.0f, 0.0f, 1.20f, -5.0f, 0.15f, 0.0f, 7.0f,  75 },
        { "Alien",         "vowels that no mouth makes (strange)",                       0, 1.00f,  3.0f,-4.0f, 5.0f,-2.0f, 0.55f,  2.0f, 0.10f, 6.0f, 0.0f, 150 },
        { "Choir Of One",  "wide and soft - turn STACK up to 4 (strange)",               1, 1.00f,  0.0f, 0.0f, 0.0f, 0.0f, 1.60f, -2.0f, 0.40f, 3.0f, 2.0f, 240 },
        { "Static Ghost",  "a bright whisper with an edge (strange)",                    0, 1.08f,  0.0f, 2.0f, 0.0f, 0.0f, 0.65f,  4.0f, 0.60f, 5.0f, 0.0f, 190 },
    };
    return d;
}
Identity makeIdentity(const Design& d) {
    Identity id; id.name = d.name; id.designed = true;
    const float semis[4] = { d.f1Semis, d.f2Semis, d.f3Semis, d.f4Semis };
    for (int q = 0; q < 9; ++q) {   // A E I O U + the four midpoints
        const float pos = q < 5 ? (float) q : (float) (q - 5) + 0.5f;
        voice::Vowel v = voice::vowelAt(pos, d.scale, d.vowelSet);
        Frame fr; fr.weight = q < 5 ? 1.0f : 0.6f; fr.f0 = d.f0;
        for (int k = 0; k < voice::kFormants; ++k) { v.F[k] *= std::exp2(semis[k] / 12.0f); v.BW[k] *= d.bwScale; fr.F[(size_t) k] = { v.F[k], v.BW[k], v.dB[k] }; }
        std::array<double, kGrid> g {};
        for (int i = 0; i < kGrid; ++i) {
            const double f = gridHz(i); double s = 0;
            for (int k = 0; k < voice::kFormants; ++k) { const double hb = v.BW[k] * 0.5, dd = f - v.F[k]; s += std::pow(10.0, v.dB[k] / 10.0) * (hb * hb) / (dd * dd + hb * hb); }
            const double tilt = 1.0 / (1.0 + std::pow(f / 1200.0, 2.0)) / (1.0 + std::pow(f / 7000.0, 4.0));
            double l = lnp(s * tilt + 1e-6 * tilt);
            l += d.tiltDb * std::log2(f / 1000.0) * kDbToLn;                                             // the design's own tilt around 1 kHz
            if (d.singerBump != 0) { const double hb = 220.0, dd = f - 3000.0; l += d.singerBump * kDbToLn * (hb * hb) / (dd * dd + hb * hb); }
            if (d.chestBump != 0) { const double hb = 70.0, dd = f - 210.0; l += d.chestBump * kDbToLn * (hb * hb) / (dd * dd + hb * hb); }
            g[(size_t) i] = l;
        }
        const double lvl = levelOf(g.data()); for (int i = 0; i < kGrid; ++i) fr.logEnv[(size_t) i] = (float) (g[(size_t) i] - lvl);
        for (int b = 0; b < kApBands; ++b) fr.ap[(size_t) b] = juce::jlimit(0.0f, 1.0f, d.breath * (0.25f + 0.75f * (float) b / (kApBands - 1)));
        id.frames.push_back(fr);
    }
    id.medianF0 = d.f0; id.tilt = d.tiltDb - 6.0f; id.breath = d.breath; id.formantScale = d.scale * (d.vowelSet == 1 ? 1.17f : d.vowelSet == 2 ? 1.38f : 1.0f);
    return id;
}
const std::vector<Identity>& factoryIdentities() { static const std::vector<Identity> v = [] { std::vector<Identity> o; for (auto& d : factoryDesigns()) o.push_back(makeIdentity(d)); return o; }(); return v; }

// ================================================================================================================ view (A frame -> identity)
View viewFor(const Identity& id, const voice::Formants& aF, const double* aGrid) {
    View v; if (id.empty()) return v;
    const int n = (int) id.frames.size(); std::vector<double> d((size_t) n);
    const bool haveF = aF[0].hz > 0 && aF[1].hz > 0; const Feat fa = featOf(aF); gridWindow();
    for (int k = 0; k < n; ++k) {
        const auto& fr = id.frames[(size_t) k]; double df = 0;
        if (haveF && fr.F[0].hz > 0 && fr.F[1].hz > 0) df = featDist(fa, featOf(fr.F)); else df = 0.15;
        double ds = 0;
        if (aGrid) { double s = 0; int c = 0; for (int i = gridLo200; i <= gridHi4k; i += 2) { const double e = aGrid[i] - fr.logEnv[(size_t) i]; s += e * e; ++c; } ds = c ? s / c : 0; }
        d[(size_t) k] = df + 0.05 * ds - 0.01 * std::log(std::max(0.02f, fr.weight));
    }
    std::vector<int> order((size_t) n); std::iota(order.begin(), order.end(), 0); std::sort(order.begin(), order.end(), [&](int x, int y) { return d[(size_t) x] < d[(size_t) y]; });
    const int top = std::min(3, n); std::array<double, 3> w {}; double ws = 0; const double d0 = d[(size_t) order[0]];
    for (int t = 0; t < top; ++t) { w[(size_t) t] = std::exp(-(d[(size_t) order[(size_t) t]] - d0) / 0.03); ws += w[(size_t) t]; }
    std::array<std::array<double, kGrid>, 3> L {}; std::array<voice::Formants, 3> Fs {}; int used = 0; std::array<double, 3> ww {};
    for (int t = 0; t < top; ++t) { if (w[(size_t) t] / ws < 0.02) continue; const auto& fr = id.frames[(size_t) order[(size_t) t]]; for (int i = 0; i < kGrid; ++i) L[(size_t) used][(size_t) i] = fr.logEnv[(size_t) i]; Fs[(size_t) used] = fr.F; ww[(size_t) used] = w[(size_t) t]; ++used; }
    double tot = 0; for (int u = 0; u < used; ++u) tot += ww[(size_t) u]; for (int u = 0; u < used; ++u) ww[(size_t) u] /= tot;
    morphGrid(L.data(), Fs.data(), ww.data(), used, v.logEnv.data(), &v.F);
    for (int b = 0; b < kApBands; ++b) { double s = 0; int u2 = 0; for (int t = 0; t < top; ++t) { if (w[(size_t) t] / ws < 0.02) continue; s += ww[(size_t) u2] * id.frames[(size_t) order[(size_t) t]].ap[(size_t) b]; ++u2; } v.ap[(size_t) b] = (float) s; }
    v.valid = true; return v;
}

// ================================================================================================================ params
double designWarp(const Params& p) { return std::exp2(juce::jlimit(-12.0f, 12.0f, p.formant) / 12.0) * std::exp2(juce::jlimit(-1.0f, 1.0f, p.genderAge) * 0.25) * std::exp2(-0.05 * juce::jlimit(-1.0f, 1.0f, p.weight)); }
double designPitchSemis(const Params& p) { return 5.0 * juce::jlimit(-1.0f, 1.0f, p.genderAge); }
void Params::sanitise(Params& p) {
    auto cf = [](float& v, float lo, float hi, float def) { v = std::isfinite(v) ? juce::jlimit(lo, hi, v) : def; };
    cf(p.wB, 0, 1, 0.5f); cf(p.wC, 0, 1, 0); if (p.wB + p.wC > 1) { const float s = 1.0f / (p.wB + p.wC); p.wB *= s; p.wC *= s; }
    cf(p.clarity, 0, 1, 0.5f); cf(p.formant, -12, 12, 0); cf(p.brightness, -1, 1, 0); cf(p.warmth, -1, 1, 0); cf(p.breath, -1, 1, 0); cf(p.grit, 0, 1, 0); cf(p.resonance, -1, 1, 0); cf(p.weight, -1, 1, 0); cf(p.genderAge, -1, 1, 0);
    p.tuneMode = juce::jlimit(0, 1, p.tuneMode); p.root = juce::jlimit(0, 11, p.root); p.scale = juce::jlimit(0, 6, p.scale); cf(p.retuneMs, 0, 400, 60); cf(p.humanize, 0, 1, 0.3f);
    p.octave = juce::jlimit(-2, 2, p.octave); cf(p.semis, -12, 12, 0); cf(p.voicing, 0.05f, 0.95f, 0.45f);
    p.voices = juce::jlimit(1, 4, p.voices); cf(p.detuneCents, 0, 50, 10); cf(p.spreadMs, 0, 60, 14); cf(p.width, 0, 1, 0.8f); cf(p.octDown, 0, 1, 0); cf(p.octUp, 0, 1, 0);
    if (!std::isfinite(p.trimStart) || !std::isfinite(p.trimEnd) || (p.trimEnd > 0 && p.trimEnd <= std::max(0.0, p.trimStart) + voice::kMinSeconds)) { p.trimStart = p.trimEnd = -1; }
    cf(p.gainDb, -24, 12, 0);
}
juce::ValueTree Params::toTree() const {
    juce::ValueTree t("GVPARAMS"); auto s = [&](const char* k, const juce::var& v) { t.setProperty(k, v, nullptr); };
    s("wB", wB); s("wC", wC); s("cons", keepConsonants); s("clarity", clarity);
    s("formant", formant); s("bright", brightness); s("warmth", warmth); s("breath", breath); s("grit", grit); s("reso", resonance); s("weight", weight); s("gender", genderAge);
    s("tune", tune); s("tuneMode", tuneMode); s("root", root); s("scale", scale); s("retune", retuneMs); s("humanize", humanize); s("vibrato", keepVibrato); s("octave", octave); s("semis", semis); s("voicing", voicing);
    s("voices", voices); s("detune", detuneCents); s("spread", spreadMs); s("width", width); s("octDown", octDown); s("octUp", octUp);
    if (trimStart >= 0 || trimEnd > 0) { s("trim0", trimStart); s("trim1", trimEnd); } s("trans", transients); s("gain", gainDb); s("norm", normalise);
    return t;
}
Params Params::fromTree(const juce::ValueTree& t) {
    Params p, d; if (!t.isValid()) return p;
    auto f = [&](const char* k, float def) { const auto v = t.getProperty(k); if (v.isVoid()) return def; const double x = (double) v; return std::isfinite(x) ? (float) x : def; };
    auto i = [&](const char* k, int def) { return (int) t.getProperty(k, def); }; auto b = [&](const char* k, bool def) { return (bool) t.getProperty(k, def); };
    p.wB = f("wB", d.wB); p.wC = f("wC", d.wC); p.keepConsonants = b("cons", d.keepConsonants); p.clarity = f("clarity", d.clarity);
    p.formant = f("formant", 0); p.brightness = f("bright", 0); p.warmth = f("warmth", 0); p.breath = f("breath", 0); p.grit = f("grit", 0); p.resonance = f("reso", 0); p.weight = f("weight", 0); p.genderAge = f("gender", 0);
    p.tune = b("tune", false); p.tuneMode = i("tuneMode", 0); p.root = i("root", 0); p.scale = i("scale", 1); p.retuneMs = f("retune", d.retuneMs); p.humanize = f("humanize", d.humanize); p.keepVibrato = b("vibrato", true); p.octave = i("octave", 0); p.semis = f("semis", 0); p.voicing = f("voicing", d.voicing);
    p.voices = i("voices", 1); p.detuneCents = f("detune", d.detuneCents); p.spreadMs = f("spread", d.spreadMs); p.width = f("width", d.width); p.octDown = f("octDown", 0); p.octUp = f("octUp", 0);
    p.trimStart = f("trim0", -1); p.trimEnd = f("trim1", -1); p.transients = b("trans", true); p.gainDb = f("gain", 0); p.normalise = b("norm", true);
    sanitise(p); return p;
}
bool Params::operator==(const Params& o) const { return toTree().isEquivalentTo(o.toTree()); }

// ================================================================================================================ identity state
juce::ValueTree Identity::toTree() const {
    juce::ValueTree t("IDENTITY"); t.setProperty("name", name, nullptr); t.setProperty("designed", designed, nullptr); t.setProperty("f0", medianF0, nullptr); t.setProperty("tilt", tilt, nullptr); t.setProperty("breath", breath, nullptr); t.setProperty("scale", formantScale, nullptr);
    t.setProperty("grid", kGrid, nullptr); t.setProperty("bands", kApBands, nullptr);
    for (const auto& fr : frames) {
        juce::ValueTree f("FRAME"); juce::MemoryBlock mb(fr.logEnv.data(), sizeof(float) * kGrid); f.setProperty("env", mb.toBase64Encoding(), nullptr);
        juce::String fs; for (auto& x : fr.F) fs << juce::String(x.hz, 1) << ":" << juce::String(x.bwHz, 1) << ":" << juce::String(x.dB, 2) << ";"; f.setProperty("F", fs, nullptr);
        juce::String ap; for (float v : fr.ap) ap << juce::String(v, 4) << ";"; f.setProperty("ap", ap, nullptr);
        f.setProperty("weight", fr.weight, nullptr); f.setProperty("f0", fr.f0, nullptr); t.appendChild(f, nullptr);
    }
    return t;
}
Identity Identity::fromTree(const juce::ValueTree& t) {
    Identity id; if (!t.hasType("IDENTITY")) return id;
    id.name = t.getProperty("name").toString(); id.designed = (bool) t.getProperty("designed", false); id.medianF0 = (float) (double) t.getProperty("f0", 0.0); id.tilt = (float) (double) t.getProperty("tilt", 0.0);
    id.breath = (float) (double) t.getProperty("breath", 0.15); id.formantScale = (float) (double) t.getProperty("scale", 1.0);
    if ((int) t.getProperty("grid", kGrid) != kGrid || (int) t.getProperty("bands", kApBands) != kApBands) return id;   // another grid: unreadable (no frames)
    for (const auto& f : t) {
        if (!f.hasType("FRAME") || (int) id.frames.size() >= kMaxFrames * 2) continue;
        Frame fr; juce::MemoryBlock mb; if (!mb.fromBase64Encoding(f.getProperty("env").toString()) || mb.getSize() != sizeof(float) * kGrid) continue;
        std::memcpy(fr.logEnv.data(), mb.getData(), mb.getSize()); for (auto& v : fr.logEnv) if (!std::isfinite(v)) v = 0;
        { auto parts = juce::StringArray::fromTokens(f.getProperty("F").toString(), ";", ""); int k = 0; for (auto& s : parts) { if (s.isEmpty() || k >= voice::kFormants) continue; auto q = juce::StringArray::fromTokens(s, ":", ""); if (q.size() == 3) fr.F[(size_t) k] = { q[0].getFloatValue(), q[1].getFloatValue(), q[2].getFloatValue() }; ++k; } }
        { auto parts = juce::StringArray::fromTokens(f.getProperty("ap").toString(), ";", ""); int k = 0; for (auto& s : parts) { if (s.isEmpty() || k >= kApBands) continue; fr.ap[(size_t) k++] = juce::jlimit(0.0f, 1.0f, s.getFloatValue()); } }
        fr.weight = juce::jlimit(0.001f, 1.0f, (float) (double) f.getProperty("weight", 1.0)); fr.f0 = (float) (double) f.getProperty("f0", 0.0);
        for (auto& x : fr.F) if (!std::isfinite(x.hz) || x.hz < 0 || x.hz > 20000) x = {};
        id.frames.push_back(fr);
    }
    return id;
}
bool Identity::operator==(const Identity& o) const { return toTree().isEquivalentTo(o.toTree()); }

Identity blendIdentity(const Identity& a, const Identity* b, const Identity* c, const Params& p) {
    Identity out = a; out.designed = false; if (a.empty()) return out;
    const double wB = (b && !b->empty()) ? p.wB : 0.0, wC = (c && !c->empty()) ? p.wC : 0.0, wA = std::max(0.0, 1.0 - wB - wC);
    for (auto& fr : out.frames) {
        std::array<std::array<double, kGrid>, 3> L {}; std::array<voice::Formants, 3> F {}; std::array<double, 3> w {}; std::array<std::array<float, kApBands>, 3> ap {}; int n = 0;
        for (int i = 0; i < kGrid; ++i) L[0][(size_t) i] = fr.logEnv[(size_t) i]; F[0] = fr.F; w[0] = wA; ap[0] = fr.ap; n = 1;
        if (wB > 0) { const auto v = viewFor(*b, fr.F, L[0].data()); L[(size_t) n] = v.logEnv; F[(size_t) n] = v.F; w[(size_t) n] = wB; ap[(size_t) n] = v.ap; ++n; }
        if (wC > 0) { const auto v = viewFor(*c, fr.F, L[0].data()); L[(size_t) n] = v.logEnv; F[(size_t) n] = v.F; w[(size_t) n] = wC; ap[(size_t) n] = v.ap; ++n; }
        std::array<double, kGrid> o {}; voice::Formants oF {}; morphGrid(L.data(), F.data(), w.data(), n, o.data(), &oF);
        const double lvl = levelOf(o.data()); for (int i = 0; i < kGrid; ++i) fr.logEnv[(size_t) i] = (float) (o[(size_t) i] - lvl); fr.F = oF;
        for (int k = 0; k < kApBands; ++k) { double s = 0; for (int v = 0; v < n; ++v) s += w[(size_t) v] * ap[(size_t) v][(size_t) k]; fr.ap[(size_t) k] = (float) s; }
    }
    auto mixf = [&](float xa, float xb, float xc) { return (float) (wA * xa + wB * xb + wC * xc); };
    out.tilt = mixf(a.tilt, b ? b->tilt : 0, c ? c->tilt : 0); out.breath = mixf(a.breath, b ? b->breath : 0, c ? c->breath : 0);
    out.formantScale = std::exp(mixf(std::log(std::max(0.5f, a.formantScale)), std::log(std::max(0.5f, b ? b->formantScale : 1.0f)), std::log(std::max(0.5f, c ? c->formantScale : 1.0f))));
    return out;
}

juce::String describe(const Identity& id) {
    if (id.empty()) return "empty";
    juce::String s;
    if (id.medianF0 > 0) { static const char* N[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; const int m = (int) std::lround(voice::hzMidi(id.medianF0)); s << "pitch around " << N[((m % 12) + 12) % 12] << (m / 12 - 1); }
    s << (s.isEmpty() ? "" : "  .  ") << (id.tilt > -3 ? "bright" : id.tilt < -9 ? "dark" : "even");
    s << "  .  " << (id.breath > 0.4f ? "breathy" : id.breath > 0.2f ? "soft" : "clean");
    s << "  .  " << (id.formantScale < 0.9f ? "big" : id.formantScale > 1.15f ? "small" : "medium") << " voice";
    s << "  .  " << (int) id.frames.size() << (id.frames.size() == 1 ? " vowel" : " vowels");
    return s;
}

// ================================================================================================================ render
namespace {
struct HP2 { double b0, b1, b2, a1, a2, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    HP2(double fc, double fs) { const double w = 2 * kPi * fc / fs, c = std::cos(w), s = std::sin(w), al = s / std::sqrt(2.0), a0 = 1 + al; b0 = (1 + c) / 2 / a0; b1 = -(1 + c) / a0; b2 = (1 + c) / 2 / a0; a1 = -2 * c / a0; a2 = (1 - al) / a0; }
    double tick(double x) { const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2; x2 = x1; x1 = x; y2 = y1; y1 = y; return y; } };
float monoAt(const voice::AudioRef& x, int i) { if (i < 0 || i >= x.n) return 0.0f; if (x.ch.size() == 1) return x.ch[0][i]; return 0.5f * (x.ch[0][i] + x.ch[1][i]); }
void finalise(voice::Rendered& out, const Params& p) {
    float pk = 0; for (float v : out.L) pk = std::max(pk, std::abs(v)); for (float v : out.R) pk = std::max(pk, std::abs(v));
    const float n = (p.normalise && pk > 1e-9f) ? 0.89f / pk : 1.0f; const float g = n * juce::Decibels::decibelsToGain(juce::jlimit(-48.0f, 24.0f, p.gainDb));
    auto soft = [](float v) { if (!std::isfinite(v)) return 0.0f; const float a = std::abs(v); if (a <= 0.95f) return v; return std::copysign(0.95f + 0.05f * std::tanh((a - 0.95f) / 0.05f), v); };
    for (auto& v : out.L) v = soft(v * g); for (auto& v : out.R) v = soft(v * g);
}
// the design curve on the grid (log power), added to the blended envelope
void designCurve(const Params& p, double* add) {
    for (int i = 0; i < kGrid; ++i) {
        const double f = gridHz(i); double db = 0;
        if (f > 1000) db += 6.0 * p.brightness * std::log2(f / 1000.0);                                             // BRIGHTNESS: tilt above 1 kHz
        { const double x = std::log2(f / 250.0); db += 5.0 * p.warmth * std::exp(-x * x / (2 * 0.6 * 0.6)); }          // WARMTH: a bell at 250 Hz
        { const double low = 1.0 / (1.0 + std::pow(f / 260.0, 3.0)); db += 6.0 * p.weight * low; const double x = std::log2(f / 500.0); db += 2.0 * p.weight * std::exp(-x * x / (2 * 0.5 * 0.5)); }   // WEIGHT: chest + F1 region
        add[i] = db * kDbToLn;
    }
}
} // namespace

bool render(const voice::Analysis& a, const voice::AudioRef& aAudio, const Identity* b0, const Identity* c0, const Params& p0, voice::Rendered& out, voice::Progress prog, Info* info) {
    out = voice::Rendered(); if (info) *info = Info();
    if (a.empty() || a.fs <= 0 || a.frames() < 2) return true;
    Params p = p0; Params::sanitise(p);
    const Identity* b = (b0 && !b0->empty()) ? b0 : nullptr; const Identity* c = (c0 && !c0->empty()) ? c0 : nullptr;
    const double wB = b ? p.wB : 0.0, wC = c ? p.wC : 0.0;
    const int fs = a.fs, bins = a.bins(), Fa = a.frames(); const double fp = a.framePeriod;
    // ---- the frame range (START / END)
    int i0 = 0, i1 = Fa;
    if (p.trimStart > 0 || (p.trimEnd > 0 && p.trimEnd < a.seconds())) {
        const double tA = juce::jlimit(0.0, a.seconds(), std::max(0.0, p.trimStart)), tB = p.trimEnd > 0 ? juce::jlimit(tA, a.seconds(), p.trimEnd) : a.seconds();
        i0 = juce::jlimit(0, std::max(0, Fa - 2), (int) std::floor(tA * 1000.0 / fp)); i1 = juce::jlimit(i0 + 2, Fa, (int) std::ceil(tB * 1000.0 / fp) + 1);
    }
    const int F = i1 - i0; const double t0 = i0 * fp / 1000.0;
    // ---- pitch: A's track, TUNE, transpose (formants never follow)
    const auto ef = voice::gateF0(a.f0Raw, a.periodicity, p.voicing);
    std::vector<double> f0((size_t) F);
    { std::vector<double> midi((size_t) F); for (int j = 0; j < F; ++j) midi[(size_t) j] = ef[(size_t) (i0 + j)] > 0 ? voice::hzMidi(ef[(size_t) (i0 + j)]) : std::numeric_limits<double>::quiet_NaN();
      if (p.tune) { const bool hard = p.tuneMode == 1; midi = voice::tunePitch(midi, fp, p.root, p.scale, hard ? 0.0f : p.retuneMs, hard ? 1.0f : (p.keepVibrato ? 0.0f : 0.6f), hard ? 0.0f : p.humanize); }
      const double semis = p.octave * 12.0 + p.semis + designPitchSemis(p);
      for (int j = 0; j < F; ++j) f0[(size_t) j] = std::isfinite(midi[(size_t) j]) ? juce::jlimit(20.0, fs / 4.0, voice::midiHz(midi[(size_t) j] + semis)) : 0.0; }
    // ---- the blend weight per frame: consonants (unvoiced frames) keep A when asked; ramped over 3 frames either side
    std::vector<float> mixScale((size_t) F, 1.0f);
    if (p.keepConsonants) { std::vector<float> raw((size_t) F); for (int j = 0; j < F; ++j) raw[(size_t) j] = ef[(size_t) (i0 + j)] > 0 ? 1.0f : 0.0f;
        for (int j = 0; j < F; ++j) { float s = 0, w = 0; for (int d = -3; d <= 3; ++d) { const int k = juce::jlimit(0, F - 1, j + d); const float ww = 4.0f - std::abs((float) d); s += ww * raw[(size_t) k]; w += ww; } mixScale[(size_t) j] = s / w; } }
    // ---- the blended envelope per frame (once; the stack voices share it)
    std::vector<float> envs((size_t) F * (size_t) kGrid); std::vector<voice::Formants> fms((size_t) F); std::vector<std::array<float, kApBands>> apsId((size_t) F); std::vector<double> levels((size_t) F); std::vector<float> wId((size_t) F, 0.0f);
    {
        std::vector<double> row((size_t) bins); std::array<double, kGrid> gA {}, dsg {}; designCurve(p, dsg.data());
        std::array<double, kGrid> smB {}, smC {}; voice::Formants smFB {}, smFC {}; bool smOn = false; int gap = 0;
        const double alpha = 1.0 - std::exp(-fp / 20.0);   // the matched identity view is smoothed over ~20 ms (no cluster jumps)
        const int half = std::max(1, (int) std::round(12.0));   // RESONANCE: the envelope's smooth trend = a 25-point boxcar on the grid
        std::array<double, kGrid> trend {}, pre {};
        for (int j = 0; j < F; ++j) {
            if ((j & 63) == 0 && prog.stop()) return false;
            const int i = i0 + j; a.specRow(i, row.data());
            const bool voiced = ef[(size_t) i] > 0;
            voice::Formants Fa_ = voiced ? formantsLpc(row.data(), bins, fs, ef[(size_t) i]) : a.formants[(size_t) i]; if (!(Fa_[0].hz > 0)) Fa_ = a.formants[(size_t) i];
            levels[(size_t) j] = toGrid(row.data(), bins, fs, gA.data());
            const double m = mixScale[(size_t) j]; const double wb = wB * m, wc = wC * m, wa = std::max(0.0, 1.0 - wb - wc);
            std::array<std::array<double, kGrid>, 3> L {}; std::array<voice::Formants, 3> Fs {}; std::array<double, 3> w {}; std::array<std::array<float, kApBands>, 3> ap {}; int n = 1;
            L[0] = gA; Fs[0] = Fa_; w[0] = wa;
            if (wb > 0 || wc > 0) {
                View vb, vc; if (b) vb = viewFor(*b, Fa_, gA.data()); if (c) vc = viewFor(*c, Fa_, gA.data());
                if (!voiced) ++gap; else gap = 0;
                if (!smOn || gap > 2) { smB = vb.logEnv; smC = vc.logEnv; smFB = vb.F; smFC = vc.F; smOn = true; }
                else { auto sm = [&](std::array<double, kGrid>& s, const View& v, voice::Formants& sf) { if (!v.valid) return; for (int k = 0; k < kGrid; ++k) s[(size_t) k] += (v.logEnv[(size_t) k] - s[(size_t) k]) * alpha;
                           for (int k = 0; k < voice::kFormants; ++k) { if (v.F[(size_t) k].hz > 0 && sf[(size_t) k].hz > 0) sf[(size_t) k].hz = (float) std::exp(std::log(sf[(size_t) k].hz) + (std::log(v.F[(size_t) k].hz) - std::log(sf[(size_t) k].hz)) * alpha); else sf[(size_t) k] = v.F[(size_t) k]; } };
                       sm(smB, vb, smFB); sm(smC, vc, smFC); }
                if (wb > 0 && vb.valid) { L[(size_t) n] = smB; Fs[(size_t) n] = smFB; w[(size_t) n] = wb; ap[(size_t) n] = vb.ap; ++n; } else w[0] += wb;
                if (wc > 0 && vc.valid) { L[(size_t) n] = smC; Fs[(size_t) n] = smFC; w[(size_t) n] = wc; ap[(size_t) n] = vc.ap; ++n; } else w[0] += wc;
            }
            std::array<double, kGrid> o {}; voice::Formants oF {};
            if (n == 1) { o = gA; oF = Fa_; } else morphGrid(L.data(), Fs.data(), w.data(), n, o.data(), &oF);
            wId[(size_t) j] = (float) (1.0 - w[0]);
            // KEEP MY CONSONANTS: breath / s / f detail above 5 kHz leans back to A (CLARITY)
            if (p.keepConsonants && n > 1 && p.clarity > 0) for (int k = 0; k < kGrid; ++k) { const double f = gridHz(k); if (f <= 5000) continue; const double r = std::min(1.0, (f - 5000.0) / 3000.0) * p.clarity; o[(size_t) k] += (gA[(size_t) k] - o[(size_t) k]) * r; }
            // RESONANCE: peaks vs valleys around the smooth trend
            if (std::abs(p.resonance) > 1e-3) {
                pre[0] = 0; double run = 0; for (int k = 0; k < kGrid; ++k) { run += o[(size_t) k]; pre[(size_t) k] = run; }
                for (int k = 0; k < kGrid; ++k) { const int lo = std::max(0, k - half), hi = std::min(kGrid - 1, k + half); trend[(size_t) k] = (pre[(size_t) hi] - (lo > 0 ? pre[(size_t) lo - 1] : 0.0)) / (hi - lo + 1); }
                const double gain = p.resonance > 0 ? 1.0 + 0.6 * p.resonance : 1.0 + 0.8 * p.resonance;
                for (int k = 0; k < kGrid; ++k) o[(size_t) k] = trend[(size_t) k] + (o[(size_t) k] - trend[(size_t) k]) * gain;
            }
            for (int k = 0; k < kGrid; ++k) { o[(size_t) k] += dsg[(size_t) k]; envs[(size_t) j * kGrid + (size_t) k] = (float) clampLn(o[(size_t) k]); }
            fms[(size_t) j] = oF;
            for (int k = 0; k < kApBands; ++k) { double s = 0; for (int v = 1; v < n; ++v) s += w[(size_t) v] * ap[(size_t) v][(size_t) k]; apsId[(size_t) j][(size_t) k] = (float) (n > 1 && (1.0 - w[0]) > 1e-6 ? s / (1.0 - w[0]) : 0.0); }
            if ((j & 63) == 0) prog.set((int) (30.0 * j / F));
        }
    }
    // ---- the stack
    struct VoiceDef { double cents = 0, drift = 0, driftHz = 0, driftPh = 0, delay = 0, gL = 1, gR = 1, level = 1; };
    const bool stereo = p.stereoOut();
    auto pan = [](double pn, double level, VoiceDef& v) { const double ang = (juce::jlimit(-1.0, 1.0, pn) + 1.0) * kPi / 4.0; v.gL = std::cos(ang) * level; v.gR = std::sin(ang) * level; v.level = level; };
    std::vector<VoiceDef> voices(1); if (stereo) pan(0, 1.0, voices[0]);
    if (p.voices >= 2) { const int extra = p.voices - 1, ranks = (extra + 1) / 2;
        for (int i = 0; i < extra; ++i) { VoiceDef v; const double side = i % 2 == 0 ? -1.0 : 1.0, rank = i / 2 + 1; v.cents = side * p.detuneCents * rank / ranks; v.drift = 0.25 * p.detuneCents; v.driftHz = 0.17 + 0.07 * i; v.driftPh = 1.7 * i;
            v.delay = p.spreadMs * (0.3 + 0.7 * (i + 1) / extra) / 1000.0; pan(side * p.width * rank / ranks, 0.75, v); voices.push_back(v); } }
    if (p.octDown > 0.001f) { VoiceDef v; v.cents = -1200; v.delay = 0.004; pan(0, p.octDown, v); voices.push_back(v); }
    if (p.octUp > 0.001f) { VoiceDef v; v.cents = 1200; v.delay = 0.006; pan(0, 0.8 * p.octUp, v); voices.push_back(v); }
    const int yLen = (int) ((F - 1) * fp / 1000.0 * fs) + 1;
    voice::Rendered seg; seg.fs = fs; seg.L.assign((size_t) yLen, 0.0f); if (stereo) seg.R.assign((size_t) yLen, 0.0f);
    std::vector<double> S((size_t) bins), P((size_t) bins), logS((size_t) bins), row((size_t) bins); std::vector<double> gridRow((size_t) kGrid);
    const double warp = designWarp(p); const double binHz = fs / (2.0 * (bins - 1));
    std::mt19937 rng(0x60057); std::normal_distribution<double> gauss(0.0, 1.0);
    if (info) { info->formants.resize((size_t) F); info->mixUsed = wId; }
    for (size_t vi = 0; vi < voices.size(); ++vi) {
        const auto& V = voices[vi]; voice::StreamSynth syn(fs, fp, a.fftSize, (size_t) yLen); double jit = 0, shim = 0; rng.seed(0x60057 + (unsigned) vi * 977u);
        for (int j = 0; j < F; ++j) {
            if ((j & 63) == 0) { if (prog.stop()) return false; prog.set(30 + (int) (55.0 * (vi + j / (double) F) / voices.size())); }
            const int i = i0 + j; const double f = f0[(size_t) j]; double fv = 0;
            jit = 0.85 * jit + 0.15 * gauss(rng); shim = 0.85 * shim + 0.15 * gauss(rng);
            if (f > 0) { double cents = V.cents + (V.drift > 0 ? V.drift * std::sin(2 * kPi * V.driftHz * j * fp / 1000.0 + V.driftPh) : 0.0) + 30.0 * p.grit * jit; fv = juce::jlimit(20.0, fs / 4.0, f * std::exp2(cents / 1200.0)); }
            // envelope: grid -> bins, A's level back, FORMANT / GENDER-AGE warp, GRIT shimmer
            for (int k = 0; k < kGrid; ++k) gridRow[(size_t) k] = envs[(size_t) j * kGrid + (size_t) k];
            fromGrid(gridRow.data(), bins, fs, logS.data());
            const double lvl = levels[(size_t) j] + 3.0 * p.grit * shim * kDbToLn;
            for (int k = 0; k < bins; ++k) S[(size_t) k] = std::exp(clampLn(logS[(size_t) k] + lvl));
            if (std::abs(warp - 1.0) > 1e-6) voice::warpEnvelope(S.data(), bins, warp, S.data());
            // aperiodicity: A's (calibrated), blended with the identity's breath, then BREATH / GRIT
            a.apRow(i, P.data()); voice::calibrateAperiodicity(P.data(), bins, fs, a.periodicity[(size_t) i]);
            const double wi = wId[(size_t) j];
            for (int k = 0; k < bins; ++k) {
                const double hz = k * binHz; double v = P[(size_t) k];
                if (wi > 1e-6) v = (1.0 - wi) * v + wi * apAt(apsId[(size_t) j], hz);
                if (p.breath > 0) v += p.breath * (1.0 - v); else if (p.breath < 0) v *= (1.0 + p.breath);
                if (p.grit > 0 && hz > 1500 && hz < 5000) v += 0.25 * p.grit * (1.0 - v);
                P[(size_t) k] = juce::jlimit(0.0, 1.0, v);
            }
            if (vi == 0 && info) info->formants[(size_t) j] = voice::formantsOf(S.data(), bins, fs, fv);
            syn.push(fv, S.data(), P.data());
        }
        const auto y = syn.finish(); const int d = (int) std::lround(V.delay * fs);
        for (int n = 0; n + d < yLen; ++n) { if (stereo) { seg.L[(size_t) (n + d)] += (float) (V.gL * y[(size_t) n]); seg.R[(size_t) (n + d)] += (float) (V.gR * y[(size_t) n]); } else seg.L[(size_t) (n + d)] += y[(size_t) n]; }
    }
    // ---- A's own attacks back over the onsets (above 2.5 kHz: the click / the consonant burst, never the pitch)
    if (p.transients && aAudio.valid() && aAudio.fs == fs && !a.onsets.empty()) {
        const int pre = (int) (0.012 * fs), rise = std::max(1, (int) (0.002 * fs)), hold = (int) (0.015 * fs), fall = (int) (0.008 * fs), warm = (int) (0.01 * fs);
        const double tEnd = t0 + (F - 1) * fp / 1000.0;
        for (const auto& o : a.onsets) {
            if (o.strength < 0.3f || o.t < t0 || o.t > tEnd) continue;
            const int on = (int) std::lround((o.t - t0) * fs), sn = (int) std::lround(o.t * fs); const int s0 = on - pre - rise, s1 = on + hold + fall; if (s1 <= 0 || s0 >= yLen) continue;
            auto w = [&](int n) { if (n < on - pre - rise || n >= on + hold + fall) return 0.0; if (n < on - pre) return (n - (on - pre - rise)) / (double) rise; if (n < on + hold) return 1.0; return 1.0 - (n - (on + hold)) / (double) fall; };
            for (int ch = 0; ch < (stereo ? 2 : 1); ++ch) { auto& y = ch == 0 ? seg.L : seg.R; HP2 hy(2500, fs), hx(2500, fs); const float g = stereo ? 0.7071f : 1.0f;
                for (int n = std::max(0, s0 - warm); n < std::min(yLen, s1); ++n) { const double yh = hy.tick(y[(size_t) n]), xh = hx.tick(monoAt(aAudio, sn + (n - on)) * g); const double ww = w(n); if (ww > 0) y[(size_t) n] = (float) (y[(size_t) n] - ww * yh + ww * xh); } }
        }
    }
    out = std::move(seg); finalise(out, p);
    if (info) { info->f0 = f0; info->framePeriod = fp; info->fs = fs; info->t0 = t0; }
    prog.set(100); return true;
}
} // namespace fusion
