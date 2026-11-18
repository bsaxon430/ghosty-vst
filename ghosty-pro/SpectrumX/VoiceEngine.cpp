#include "VoiceEngine.h"
#include "world/harvest.h"
#include "world/dio.h"
#include "world/stonemask.h"
#include "world/cheaptrick.h"
#include "world/d4c.h"
#include "world/codec.h"
#include "world/synthesisrealtime.h"
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <thread>
#include <exception>
#include <mutex>
#include <deque>

namespace voice {
namespace {
constexpr double kLogFloor = 1e-20;              // power floor before a log
constexpr double kQMin = -60.0, kQStep = 80.0 / 65535.0;   // 16-bit log-power code: e^-60 .. e^20
constexpr int kMaxStoredBins = 1100;             // envelope storage per frame (full resolution below 1.5 kHz)
constexpr int kChunk = 128;                      // frames per CheapTrick / D4C job
constexpr double kSegSeconds = 4.0, kOverlapSeconds = 0.35;   // f0 tracking runs in overlapping pieces
constexpr double kSliceMargin = 0.35;            // seconds of context around an envelope / aperiodicity chunk
constexpr double kMaxOutputSeconds = 900.0;
const double kPi = juce::MathConstants<double>::pi;

inline double lg(double p) { return std::log(std::max(kLogFloor, p)); }

// ---- a tiny parallel-for (std::thread), first exception is re-thrown on the caller ------------------------------
void parallelFor(int tasks, int threads, const std::function<void(int)>& fn) {
    if (tasks <= 0) return;
    threads = juce::jlimit(1, tasks, threads);
    if (threads == 1) { for (int i = 0; i < tasks; ++i) fn(i); return; }
    std::atomic<int> next { 0 }; std::exception_ptr err; std::mutex em;
    auto work = [&] { for (;;) { const int i = next.fetch_add(1); if (i >= tasks) return;
                                 try { fn(i); } catch (...) { std::lock_guard<std::mutex> l(em); if (!err) err = std::current_exception(); next = tasks; return; } } };
    std::vector<std::thread> pool; pool.reserve((size_t) threads - 1);
    for (int t = 1; t < threads; ++t) pool.emplace_back(work);
    work(); for (auto& t : pool) t.join();
    if (err) std::rethrow_exception(err);
}
int autoThreads(int requested) { if (requested > 0) return requested; const int hw = (int) std::thread::hardware_concurrency(); return juce::jlimit(1, 4, hw - 1); }

// ---- LPC helpers -------------------------------------------------------------------------------------------------
bool levinson(const std::vector<double>& r, int order, std::vector<double>& a) {
    a.assign((size_t) order + 1, 0.0); a[0] = 1.0; double err = r[0]; if (err <= 0) return false;
    std::vector<double> tmp((size_t) order + 1);
    for (int i = 1; i <= order; ++i) {
        double acc = r[(size_t) i]; for (int j = 1; j < i; ++j) acc += a[(size_t) j] * r[(size_t) (i - j)];
        const double k = -acc / err; if (!std::isfinite(k) || std::abs(k) >= 1.0) return i > 2;
        tmp = a; for (int j = 1; j < i; ++j) a[(size_t) j] = tmp[(size_t) j] + k * tmp[(size_t) (i - j)]; a[(size_t) i] = k;
        err *= (1.0 - k * k); if (err <= 1e-12 * r[0]) break;
    }
    return true;
}
Formants lpcPeaks(const std::vector<double>& a, double fMax) {
    constexpr int M = 640; std::vector<double> H((size_t) M);
    const int order = (int) a.size() - 1;
    for (int m = 0; m < M; ++m) {
        const double w = kPi * (m + 0.5) / M; double re = 0, im = 0;
        for (int i = 0; i <= order; ++i) { re += a[(size_t) i] * std::cos(w * i); im -= a[(size_t) i] * std::sin(w * i); }
        H[(size_t) m] = 10.0 * std::log10(1.0 / std::max(1e-12, re * re + im * im));
    }
    struct Pk { double f, db, bw; };
    std::vector<Pk> pk;
    for (int m = 1; m < M - 1; ++m) if (H[(size_t) m] > H[(size_t) m - 1] && H[(size_t) m] >= H[(size_t) m + 1]) {
        const double f = (m + 0.5) / M * fMax; if (f < 90.0 || f > fMax - 60.0) continue;
        const double y0 = H[(size_t) m - 1], y1 = H[(size_t) m], y2 = H[(size_t) m + 1]; const double den = y0 - 2 * y1 + y2;
        const double off = std::abs(den) > 1e-9 ? 0.5 * (y0 - y2) / den : 0.0;
        int lo = m, hi = m; while (lo > 0 && H[(size_t) lo] > y1 - 3.0) --lo;
        while (hi < M - 1 && H[(size_t) hi] > y1 - 3.0) ++hi;
        pk.push_back({ (m + 0.5 + off) / M * fMax, y1, std::max(20.0, (hi - lo) / (double) M * fMax) });
    }
    std::vector<Pk> merged;
    for (auto& p : pk) { if (!merged.empty() && p.f - merged.back().f < 150.0) { if (p.db > merged.back().db) merged.back() = p; } else merged.push_back(p); }
    Formants out {};
    for (size_t i = 0; i < merged.size() && i < (size_t) kFormants; ++i) out[i] = { (float) merged[i].f, (float) merged[i].bw, (float) merged[i].db };
    return out;
}
Formants formantsFromAutocorr(std::vector<double> r, int order, double fMax) {
    if (r.empty() || r[0] <= 0) return {};
    for (size_t i = 0; i < r.size(); ++i) r[i] *= std::exp(-0.5 * std::pow(0.001 * (double) i, 2)) * (i == 0 ? 1.0001 : 1.0);
    std::vector<double> a; if (!levinson(r, order, a)) return {};
    return lpcPeaks(a, fMax);
}
std::vector<double> autocorrOfWave(const float* x, int n, int order) {
    std::vector<double> y((size_t) n); double prev = 0;
    for (int i = 0; i < n; ++i) { const double w = 0.5 - 0.5 * std::cos(2 * kPi * (i + 0.5) / n); const double v = x[i] - 0.97 * prev; prev = x[i]; y[(size_t) i] = v * w; }
    std::vector<double> r((size_t) order + 1, 0.0);
    for (int l = 0; l <= order; ++l) { double s = 0; for (int i = l; i < n; ++i) s += y[(size_t) i] * y[(size_t) (i - l)]; r[(size_t) l] = s; }
    return r;
}

// ---- envelope resampling (B's bins onto A's grid). Above B's Nyquist the envelope fades to the floor (no flat band).
void resampleRow(const double* in, int binsIn, int fsIn, int binsOut, int fsOut, double* out) {
    const double hzOut = fsOut / (2.0 * (binsOut - 1)), binIn = 2.0 * (binsIn - 1) / fsIn, nyqIn = fsIn * 0.5;
    for (int k = 0; k < binsOut; ++k) {
        const double hz = k * hzOut;
        const double pos = juce::jlimit(0.0, (double) binsIn - 1, hz * binIn); const int i = (int) pos; const double f = pos - i;
        const double l0 = lg(in[i]), l1 = lg(in[std::min(binsIn - 1, i + 1)]);
        double l = l0 + (l1 - l0) * f;
        if (hz > nyqIn) l -= std::log(10.0) * 6.0 * std::min(1.0, (hz - nyqIn) / 2000.0);   // -60 dB over 2 kHz
        out[k] = std::exp(l);
    }
}

// ---- the voicing gate (periodicity threshold) + cleanup; used by analysis and at render time ----------------------
std::vector<double> gateTrack(const std::vector<float>& raw, const std::vector<float>& per, float thr) {
    const int F = (int) raw.size(); std::vector<double> f0((size_t) F, 0.0);
    // the periodicity is median-smoothed over 5 frames (25 ms) so a breathy note doesn't flicker voiced / unvoiced
    for (int i = 0; i < F; ++i) {
        if (!(raw[(size_t) i] > 0)) continue;
        float w[5]; int n = 0; for (int d = -2; d <= 2; ++d) { const int j = i + d; if (j >= 0 && j < F && raw[(size_t) j] > 0) w[n++] = per[(size_t) j]; }
        std::sort(w, w + n); if (w[n / 2] >= thr) f0[(size_t) i] = raw[(size_t) i];
    }
    for (int i = 0; i < F;) { if (f0[(size_t) i] <= 0) { ++i; continue; } int j = i; while (j < F && f0[(size_t) j] > 0) ++j; if (j - i < 3) std::fill(f0.begin() + i, f0.begin() + j, 0.0); i = j; }
    for (int i = 1; i < F; ++i) if (f0[(size_t) i] <= 0 && f0[(size_t) i - 1] > 0) {   // fill holes up to 3 frames inside a voiced run
        int j = i; while (j < F && f0[(size_t) j] <= 0 && j - i < 4) ++j;
        if (j < F && j - i <= 3 && f0[(size_t) j] > 0) for (int k = i; k < j; ++k) { const double t = (k - i + 1) / (double) (j - i + 1); f0[(size_t) k] = std::exp(std::log(f0[(size_t) i - 1]) * (1 - t) + std::log(f0[(size_t) j]) * t); }
    }
    return f0;
}

// ---- envelope storage grid -----------------------------------------------------------------------------------------
void setupGrid(Analysis& a) {
    const int bins = a.bins(); const double binHz = a.fs / (double) a.fftSize;
    if (bins <= kMaxStoredBins) { a.lowBins = bins; a.dec = 1; a.storedBins = bins; return; }
    a.lowBins = std::min(bins, (int) std::ceil(1500.0 / binHz) + 1);
    a.dec = std::max(1, (int) std::ceil((bins - 1 - a.lowBins) / (double) (kMaxStoredBins - a.lowBins - 1)));
    a.storedBins = a.lowBins + (int) std::ceil((bins - 1 - a.lowBins) / (double) a.dec) + 1;
}
inline int storedBin(const Analysis& a, int s) { return s < a.lowBins ? s : std::min(a.bins() - 1, a.lowBins + (s - a.lowBins) * a.dec); }
inline uint16_t qEncode(double p) { const double l = lg(p); return (uint16_t) juce::jlimit(0.0, 65535.0, std::round((l - kQMin) / kQStep)); }
inline double qLog(uint16_t q) { return kQMin + q * kQStep; }
void encodeRow(Analysis& a, int f, const double* row) {
    uint16_t* d = a.specQ.data() + (size_t) f * (size_t) a.storedBins;
    for (int s = 0; s < a.storedBins; ++s) d[s] = qEncode(row[storedBin(a, s)]);
}
// log-power of frame f at every bin
void decodeLogRow(const Analysis& a, int f, double* out) {
    const uint16_t* d = a.specQ.data() + (size_t) f * (size_t) a.storedBins; const int bins = a.bins();
    for (int k = 0; k < bins; ++k) {
        if (k < a.lowBins) { out[k] = qLog(d[k]); continue; }
        const int s0 = std::min(a.storedBins - 1, a.lowBins + (k - a.lowBins) / a.dec), s1 = std::min(a.storedBins - 1, s0 + 1);
        const int b0 = storedBin(a, s0), b1 = storedBin(a, s1); const double t = b1 > b0 ? (k - b0) / (double) (b1 - b0) : 0.0;
        out[k] = qLog(d[s0]) + (qLog(d[s1]) - qLog(d[s0])) * t;
    }
}
} // namespace

// ================================================================================================================ Analysis
float Analysis::voicedFraction() const { if (f0.empty()) return 0; int v = 0; for (double f : f0) v += f > 0; return v / (float) f0.size(); }
double Analysis::medianF0() const { std::vector<double> v; for (double f : f0) if (f > 0) v.push_back(f); if (v.empty()) return 0; std::sort(v.begin(), v.end()); return v[v.size() / 2]; }
void Analysis::specRow(int f, double* out) const { decodeLogRow(*this, juce::jlimit(0, frames() - 1, f), out); for (int k = 0; k < bins(); ++k) out[k] = std::exp(out[k]); }
void Analysis::apRow(int f, double* out) const {
    f = juce::jlimit(0, frames() - 1, f);
    const double* c = apCoded.data() + (size_t) f * (size_t) nAp; double* o = out;
    DecodeAperiodicity(&c, 1, fs, fftSize, &o);
    for (int k = 0; k < bins(); ++k) if (!std::isfinite(out[k])) out[k] = 1.0;
}
size_t Analysis::memoryBytes() const {
    return f0.capacity() * sizeof(double) + (f0Raw.capacity() + periodicity.capacity() + rms.capacity()) * sizeof(float) + formants.capacity() * sizeof(Formants)
         + specQ.capacity() * sizeof(uint16_t) + apCoded.capacity() * sizeof(double) + onsets.capacity() * sizeof(Onset);
}

// ================================================================================================================ analysis
namespace {
std::vector<double> toDouble(const float* x, int n) { std::vector<double> d((size_t) std::max(0, n)); for (int i = 0; i < n; ++i) d[(size_t) i] = std::isfinite(x[i]) ? (double) x[i] : 0.0; return d; }

// frame rms (+-1 frame period) of x
std::vector<float> frameRms(const float* x, int n, int fs, double fp, int F) {
    std::vector<float> r((size_t) F, 0.0f); const int w = std::max(16, (int) (fp / 1000.0 * fs));
    for (int i = 0; i < F; ++i) {
        const int c = (int) std::lround(i * fp / 1000.0 * fs); double e = 0; int cnt = 0;
        for (int k = std::max(0, c - w); k < std::min(n, c + w); ++k) { const double v = std::isfinite(x[k]) ? x[k] : 0.0; e += v * v; ++cnt; }
        r[(size_t) i] = cnt ? (float) std::sqrt(e / cnt) : 0.0f;
    }
    return r;
}

// raw WORLD f0 track of x in overlapping segments (parallel), on the global frame grid
bool rawF0(const float* x, int n, int fs, const AnalysisOptions& o, std::vector<float>& f0, Progress p, int pct0, int pct1, int threads) {
    const double fp = o.framePeriodMs; const int F = GetSamplesForHarvest(fs, n, fp); f0.assign((size_t) F, 0.0f);
    auto run = [&](int s0, int s1, std::vector<double>& f) {
        const auto seg = toDouble(x + s0, s1 - s0); const int len = s1 - s0;
        const int L = GetSamplesForHarvest(fs, len, fp); std::vector<double> t((size_t) L, 0.0); f.assign((size_t) L, 0.0);
        if (o.method == AnalysisOptions::Fast) {
            DioOption d; InitializeDioOption(&d); d.f0_floor = o.f0Floor; d.f0_ceil = o.f0Ceil; d.frame_period = fp; d.speed = 1; d.allowed_range = 0.1;
            Dio(seg.data(), len, fs, &d, t.data(), f.data()); std::vector<double> ref((size_t) L); StoneMask(seg.data(), len, fs, t.data(), f.data(), L, ref.data()); f = ref;
        } else {
            HarvestOption h; InitializeHarvestOption(&h); h.f0_floor = o.f0Floor; h.f0_ceil = o.f0Ceil; h.frame_period = fp;
            Harvest(seg.data(), len, fs, &h, t.data(), f.data());
        }
    };
    const int segFrames = (int) (kSegSeconds * 1000.0 / fp), ovlFrames = (int) (kOverlapSeconds * 1000.0 / fp);
    if (F <= segFrames + 2 * ovlFrames) { std::vector<double> f; run(0, n, f); for (int i = 0; i < F && i < (int) f.size(); ++i) f0[(size_t) i] = (float) f[(size_t) i]; p.set(pct1); return !p.stop(); }
    const int segs = (F + segFrames - 1) / segFrames; std::atomic<int> done { 0 };
    parallelFor(segs, threads, [&](int s) {
        if (p.stop()) return;
        const int c0 = s * segFrames, c1 = std::min(F, c0 + segFrames);
        const int g0 = std::max(0, c0 - ovlFrames), g1 = std::min(F, c1 + ovlFrames);
        const int s0 = (int) std::lround(g0 * fp / 1000.0 * fs), s1 = std::min(n, (int) std::lround(g1 * fp / 1000.0 * fs) + 1);
        std::vector<double> f; run(s0, s1, f);
        for (int i = c0; i < c1; ++i) { const int j = i - g0; if (j >= 0 && j < (int) f.size()) f0[(size_t) i] = (float) f[(size_t) j]; }
        p.set(pct0 + (pct1 - pct0) * (++done) / segs);
    });
    return !p.stop();
}

float periodicityAt(const float* x, int n, int fs, double fp, int i, double f0) {
    const int c = (int) std::lround(i * fp / 1000.0 * fs); const int lag = (int) std::lround(fs / f0); if (lag < 2) return 0;
    const int half = std::max(lag * 3 / 2, lag + 8); const int k0 = std::max(0, c - half), k1 = std::min(n - lag, c + half - lag);
    if (k1 - k0 < lag) return 1.0f;
    double s01 = 0, s00 = 0, s11 = 0;
    for (int k = k0; k < k1; ++k) { const double a = x[k], b = x[k + lag]; if (!std::isfinite(a) || !std::isfinite(b)) continue; s01 += a * b; s00 += a * a; s11 += b * b; }
    return (float) juce::jlimit(0.0, 1.0, s01 / std::sqrt(std::max(1e-30, s00 * s11)));
}

// percussive onsets: positive log-spectral flux peaks with a fast energy rise
std::vector<Onset> findOnsets(const float* x, int n, int fs) {
    std::vector<Onset> out; int order = fs > 60000 ? 11 : 10; const int N = 1 << order, hop = N / 4; if (n < N * 2) return out;
    juce::dsp::FFT fft(order); std::vector<float> buf((size_t) N * 2), prev((size_t) N / 2 + 1, 0.0f); std::vector<float> flux, energy;
    const int k0 = (int) (150.0 * N / fs);
    for (int s = 0; s + N <= n; s += hop) {
        std::fill(buf.begin(), buf.end(), 0.0f); double e = 0;
        for (int i = 0; i < N; ++i) { const float v = std::isfinite(x[s + i]) ? x[s + i] : 0.0f; buf[(size_t) i] = v * (float) (0.5 - 0.5 * std::cos(2 * kPi * (i + 0.5) / N)); e += v * v; }
        fft.performFrequencyOnlyForwardTransform(buf.data(), true);
        double fl = 0; for (int k = k0; k <= N / 2; ++k) { const float l = std::log(1e-6f + buf[(size_t) k]); fl += std::max(0.0f, l - prev[(size_t) k]); prev[(size_t) k] = l; }
        flux.push_back((float) (fl / (N / 2 - k0 + 1))); energy.push_back((float) (10.0 * std::log10(1e-12 + e / N)));
    }
    const int M = (int) flux.size(); const int win = std::max(2, (int) (0.25 * fs / hop));
    for (int m = 2; m < M - 1; ++m) {
        bool peak = true; for (int d = -3; d <= 3; ++d) if (d != 0 && m + d >= 0 && m + d < M && flux[(size_t) (m + d)] > flux[(size_t) m]) peak = false;
        if (!peak) continue;
        double avg = 0; int c = 0; for (int d = -win; d <= win; ++d) if (m + d >= 0 && m + d < M) { avg += flux[(size_t) (m + d)]; ++c; } avg /= std::max(1, c);
        if (flux[(size_t) m] < 1.8 * avg + 0.05) continue;
        const float before = energy[(size_t) std::max(0, m - 2)], after = std::max(energy[(size_t) m], energy[(size_t) std::min(M - 1, m + 1)]);
        const float rise = after - before; if (rise < 6.0f || after < -70.0f) continue;
        // the onset time: the frame's centre is s + N/2; the attack starts around the start of the rise
        const double t = (m * hop + N * 0.5 - hop) / (double) fs;
        if (!out.empty() && t - out.back().t < 0.05) continue;
        out.push_back({ (float) std::max(0.0, t), juce::jlimit(0.0f, 1.0f, (rise - 6.0f) / 24.0f) });
    }
    // refine each onset to the first sample whose envelope passes 10 % of the local peak (sample accurate)
    for (auto& o : out) {
        const int c = (int) (o.t * fs); const int a = std::max(0, c - N), b = std::min(n, c + N); float pk = 0; for (int i = a; i < b; ++i) pk = std::max(pk, std::abs(x[i]));
        int i = a; while (i < b && std::abs(x[i]) < 0.1f * pk) ++i; o.t = (float) (std::max(0, i) / (double) fs);
    }
    return out;
}
} // namespace

bool trackF0(const float* x, int n, int fs, const AnalysisOptions& o, std::vector<double>& f0, Progress p) {
    f0.clear(); if (n < (int) (kMinSeconds * fs) || fs <= 0) return true;
    std::vector<float> raw; if (!rawF0(x, n, fs, o, raw, p, 0, 100, autoThreads(o.threads))) { f0.clear(); return false; }
    const int F = (int) raw.size(); const auto rms = frameRms(x, n, fs, o.framePeriodMs, F);
    const float mx = rms.empty() ? 0.0f : *std::max_element(rms.begin(), rms.end()); const float sil = std::max(1e-5f, mx * std::pow(10.0f, o.silenceDb / 20.0f));
    std::vector<float> per((size_t) F, 0.0f);
    for (int i = 0; i < F; ++i) { if (rms[(size_t) i] < sil) raw[(size_t) i] = 0; if (raw[(size_t) i] > 0) per[(size_t) i] = periodicityAt(x, n, fs, o.framePeriodMs, i, raw[(size_t) i]); }
    f0 = gateTrack(raw, per, o.voicingThreshold); return true;
}

bool analyse(const float* x, int n, int fs, const AnalysisOptions& o, Analysis& out, Progress p) {
    out = Analysis(); out.fs = fs; out.framePeriod = o.framePeriodMs; out.length = std::max(0, n);
    if (n < (int) (kMinSeconds * fs) || fs <= 0) return true;   // too short to say anything: an empty analysis
    const int threads = autoThreads(o.threads); const double fp = o.framePeriodMs;
    p.set(0);
    if (!rawF0(x, n, fs, o, out.f0Raw, p, 0, 40, threads)) { out = Analysis(); return false; }
    const int F = (int) out.f0Raw.size();
    out.rms = frameRms(x, n, fs, fp, F);
    const float mx = *std::max_element(out.rms.begin(), out.rms.end()); const float sil = std::max(1e-5f, mx * std::pow(10.0f, o.silenceDb / 20.0f));
    out.periodicity.assign((size_t) F, 0.0f);
    for (int i = 0; i < F; ++i) {
        float& f = out.f0Raw[(size_t) i]; if (!std::isfinite(f) || f < 0) f = 0;
        if (out.rms[(size_t) i] < sil) f = 0;
        if (f > 0) out.periodicity[(size_t) i] = periodicityAt(x, n, fs, fp, i, f);
    }
    out.f0 = gateTrack(out.f0Raw, out.periodicity, o.voicingThreshold);
    // envelope + aperiodicity in parallel chunks, each on its own slice of the signal
    CheapTrickOption ct; InitializeCheapTrickOption(fs, &ct); ct.f0_floor = o.f0Floor; ct.fft_size = GetFFTSizeForCheapTrick(fs, &ct);
    out.fftSize = ct.fft_size; const int bins = out.bins(); setupGrid(out);
    out.nAp = GetNumberOfAperiodicities(fs);
    out.specQ.assign((size_t) F * (size_t) out.storedBins, 0); out.apCoded.assign((size_t) F * (size_t) out.nAp, 0.0); out.formants.resize((size_t) F);
    const int chunks = (F + kChunk - 1) / kChunk; std::atomic<int> done { 0 };
    const double margin = std::max(kSliceMargin, 3.0 / o.f0Floor);
    parallelFor(chunks, threads, [&](int c) {
        if (p.stop()) return;
        const int i0 = c * kChunk, cnt = std::min(kChunk, F - i0);
        const double tA = i0 * fp / 1000.0 - margin, tB = (i0 + cnt - 1) * fp / 1000.0 + margin;
        const int s0 = std::max(0, (int) std::floor(tA * fs)), s1 = std::min(n, (int) std::ceil(tB * fs) + 1);
        const auto seg = toDouble(x + s0, s1 - s0); const double off = s0 / (double) fs;
        std::vector<double> t((size_t) cnt), f((size_t) cnt);
        for (int k = 0; k < cnt; ++k) { t[(size_t) k] = (i0 + k) * fp / 1000.0 - off; f[(size_t) k] = out.f0Raw[(size_t) (i0 + k)]; }
        std::vector<double> spec((size_t) cnt * (size_t) bins), ap((size_t) cnt * (size_t) bins);
        std::vector<double*> rs((size_t) cnt), as((size_t) cnt); for (int k = 0; k < cnt; ++k) { rs[(size_t) k] = spec.data() + (size_t) k * (size_t) bins; as[(size_t) k] = ap.data() + (size_t) k * (size_t) bins; }
        CheapTrickOption ctl = ct; CheapTrick(seg.data(), (int) seg.size(), fs, t.data(), f.data(), cnt, &ctl, rs.data());
        if (p.stop()) return;
        D4COption d4; InitializeD4COption(&d4); D4C(seg.data(), (int) seg.size(), fs, t.data(), f.data(), cnt, out.fftSize, &d4, as.data());
        for (auto& v : spec) if (!std::isfinite(v) || v < 0) v = kLogFloor;
        for (auto& v : ap) if (!std::isfinite(v)) v = 1.0;
        std::vector<double*> cs((size_t) cnt); for (int k = 0; k < cnt; ++k) cs[(size_t) k] = out.apCoded.data() + (size_t) (i0 + k) * (size_t) out.nAp;
        CodeAperiodicity(as.data(), cnt, fs, out.fftSize, cs.data());
        for (int k = 0; k < cnt; ++k) { encodeRow(out, i0 + k, rs[(size_t) k]); out.formants[(size_t) (i0 + k)] = formantsOf(rs[(size_t) k], bins, fs, out.f0Raw[(size_t) (i0 + k)]); }
        p.set(40 + 55 * (++done) / chunks);
    });
    if (p.stop()) { out = Analysis(); return false; }
    for (auto& v : out.apCoded) if (!std::isfinite(v)) v = 0.0;
    out.onsets = findOnsets(x, n, fs);
    p.set(100); return true;
}

// The envelope is already free of the harmonic comb, so its own peaks are the formants: a mild tilt correction
// (+6 dB/oct above 300 Hz: glottal roll-off), a boxcar exactly f0 wide (nulls what is left of the harmonic ripple;
// 40 Hz when f0 is unknown), local maxima with >= 1 dB prominence, parabolic refinement on a dB scale.
// LPC (order 14 over 0..5.5 kHz) fills in when two formants merge into one peak.
Formants formantsOf(const double* spec, int bins, int fs, double f0) {
    const double binHz = fs / (2.0 * (bins - 1)), fMax = std::min(5500.0, fs * 0.45);
    const int kMax = std::max(8, std::min(bins - 1, (int) (fMax / binHz))); const double fMaxEff = kMax * binHz;
    double peak = 0; for (int k = 0; k <= kMax; ++k) peak = std::max(peak, spec[k]); if (!(peak > 0)) return {};
    std::vector<double> L((size_t) kMax + 1), S((size_t) kMax + 1);
    for (int k = 0; k <= kMax; ++k) { const double f = k * binHz; L[(size_t) k] = 10.0 * std::log10(std::max(spec[k], peak * 1e-10) / peak) + 10.0 * std::log10(1.0 + (f / 300.0) * (f / 300.0)); }
    {
        const double w = juce::jlimit(40.0, 600.0, f0 > 0 ? f0 : 40.0) / binHz, half = 0.5 * w; const int hw = (int) std::floor(half); const double edge = half - hw;
        for (int k = 0; k <= kMax; ++k) { double s = 0, c = 0;
            for (int d = -hw - 1; d <= hw + 1; ++d) { const int j = juce::jlimit(0, kMax, k + d); const double wt = std::abs(d) <= hw ? 1.0 : edge; s += wt * L[(size_t) j]; c += wt; }
            S[(size_t) k] = s / c; }
    }
    struct Pk { double f, db, bw; };
    std::vector<Pk> pk; const double fMin = std::max(150.0, 1.25 * f0);   // the glottal hump sits at f0: never a formant
    for (int k = 1; k < kMax; ++k) {
        if (!(S[(size_t) k] > S[(size_t) k - 1] && S[(size_t) k] >= S[(size_t) k + 1])) continue;
        const double f = k * binHz; if (f < fMin || f > fMax - 100.0) continue;
        const int span = (int) (600.0 / binHz); double lmin = S[(size_t) k], rmin = S[(size_t) k];
        for (int j = k - 1; j >= std::max(0, k - span) && S[(size_t) j] <= S[(size_t) k]; --j) lmin = std::min(lmin, S[(size_t) j]);
        for (int j = k + 1; j <= std::min(kMax, k + span) && S[(size_t) j] <= S[(size_t) k]; ++j) rmin = std::min(rmin, S[(size_t) j]);
        if (S[(size_t) k] - std::max(lmin, rmin) < 1.0) continue;
        const double y0 = S[(size_t) k - 1], y1 = S[(size_t) k], y2 = S[(size_t) k + 1], den = y0 - 2 * y1 + y2;
        const double off = std::abs(den) > 1e-12 ? juce::jlimit(-0.5, 0.5, 0.5 * (y0 - y2) / den) : 0.0;
        int lo = k, hi = k; while (lo > 0 && S[(size_t) lo] > y1 - 3.0) --lo;
        while (hi < kMax && S[(size_t) hi] > y1 - 3.0) ++hi;
        pk.push_back({ (k + off) * binHz, y1, std::max(20.0, (hi - lo) * binHz) });
    }
    std::vector<Pk> merged;
    for (auto& p : pk) { if (!merged.empty() && p.f - merged.back().f < 120.0) { if (p.db > merged.back().db) merged.back() = p; } else merged.push_back(p); }
    if ((int) merged.size() < kFormants) {
        constexpr int order = 14; std::vector<double> r((size_t) order + 1, 0.0), P((size_t) kMax + 1);
        for (int k = 0; k <= kMax; ++k) { const double pre = 1.0 + 0.97 * 0.97 - 2.0 * 0.97 * std::cos(kPi * k / kMax); P[(size_t) k] = std::max(spec[k], peak * 1e-9) * pre * ((k == 0 || k == kMax) ? 0.5 : 1.0); }
        for (int l = 0; l <= order; ++l) { double s = 0; for (int k = 0; k <= kMax; ++k) s += P[(size_t) k] * std::cos(kPi * k * l / kMax); r[(size_t) l] = s; }
        const auto lp = formantsFromAutocorr(r, order, fMaxEff);
        for (auto& f : lp) {
            if (f.hz <= 0 || (int) merged.size() >= kFormants) continue;
            bool near = false; for (auto& m : merged) near = near || std::abs(m.f - f.hz) < 200.0;
            if (!near) merged.push_back({ f.hz, f.dB, f.bwHz });
        }
        std::sort(merged.begin(), merged.end(), [](const Pk& a, const Pk& b) { return a.f < b.f; });
    }
    Formants out {};
    for (size_t i = 0; i < merged.size() && i < (size_t) kFormants; ++i) out[i] = { (float) merged[i].f, (float) merged[i].bw, (float) merged[i].db };
    return out;
}

Formants formantsOfWave(const float* x, int n, int fs) {
    if (n < 64) return {};
    const double target = 11025.0; std::vector<float> y(x, x + n);
    if (fs > target * 1.1) y = resample(y, fs, target);
    const double fsE = fs > target * 1.1 ? target : fs;
    const int order = 2 + (int) std::lround(fsE / 1000.0);
    return formantsFromAutocorr(autocorrOfWave(y.data(), (int) y.size(), order), order, fsE / 2);
}

// ================================================================================================================ vowels
const std::array<Vowel, kVowels>& vowelTable(int set) {
    // average targets (textbook values: adult male, adult female, child), bandwidths and relative levels of a parallel formant model
    static const std::array<Vowel, kVowels> M { {
        { "A", { 730, 1090, 2440, 3400 }, { 80, 90, 130, 160 }, { 0, -6, -12, -18 } },
        { "E", { 530, 1840, 2480, 3480 }, { 60, 90, 150, 200 }, { 0, -8, -14, -20 } },
        { "I", { 270, 2290, 3010, 3700 }, { 55, 90, 110, 140 }, { 0, -14, -18, -24 } },
        { "O", { 570,  840, 2410, 3400 }, { 60, 80, 150, 200 }, { 0, -5, -16, -22 } },
        { "U", { 300,  870, 2240, 3400 }, { 55, 80, 150, 200 }, { 0, -9, -20, -26 } } } };
    static const std::array<Vowel, kVowels> Fm { {
        { "A", { 850, 1220, 2810, 4000 }, { 90, 100, 150, 180 }, { 0, -6, -12, -18 } },
        { "E", { 610, 2330, 2990, 4100 }, { 70, 100, 160, 210 }, { 0, -8, -14, -20 } },
        { "I", { 310, 2790, 3310, 4200 }, { 60, 100, 120, 160 }, { 0, -14, -18, -24 } },
        { "O", { 590,  920, 2710, 3900 }, { 70, 90, 160, 210 }, { 0, -5, -16, -22 } },
        { "U", { 370,  950, 2670, 3900 }, { 60, 90, 160, 210 }, { 0, -9, -20, -26 } } } };
    static const std::array<Vowel, kVowels> C { {
        { "A", { 1030, 1370, 3170, 4500 }, { 100, 110, 170, 200 }, { 0, -6, -12, -18 } },
        { "E", { 690, 2610, 3570, 4600 }, { 80, 110, 180, 220 }, { 0, -8, -14, -20 } },
        { "I", { 370, 3200, 3730, 4700 }, { 70, 110, 140, 180 }, { 0, -14, -18, -24 } },
        { "O", { 680, 1060, 3180, 4400 }, { 80, 100, 180, 220 }, { 0, -5, -16, -22 } },
        { "U", { 430, 1170, 3260, 4400 }, { 70, 100, 180, 220 }, { 0, -9, -20, -26 } } } };
    return set == VowelsFemale ? Fm : set == VowelsChild ? C : M;
}
Vowel vowelAt(float pos, float scale, int set, const Vowel* custom) {
    const auto& T = vowelTable(set); const float maxPos = custom ? (float) kVowels : (float) (kVowels - 1);
    pos = juce::jlimit(0.0f, maxPos, std::isfinite(pos) ? pos : 0.0f);
    auto at = [&](int i) -> const Vowel& { return i >= kVowels && custom ? *custom : T[(size_t) std::min(i, kVowels - 1)]; };
    const int i = std::min((int) maxPos - 1, (int) pos); const float f = pos - i;
    const Vowel& a = at(i); const Vowel& b = at(i + 1);
    Vowel v = a; v.name = f < 0.5f ? a.name : b.name;
    for (int k = 0; k < kFormants; ++k) {
        v.F[k] = (a.F[k] * std::pow(b.F[k] / a.F[k], f)) * scale; v.BW[k] = (a.BW[k] + (b.BW[k] - a.BW[k]) * f) * std::sqrt(scale); v.dB[k] = a.dB[k] + (b.dB[k] - a.dB[k]) * f;
    }
    return v;
}
void vowelEnvelope(const Vowel& v, int bins, int fs, double* out) {
    const double binHz = fs / (2.0 * (bins - 1));
    for (int k = 0; k < bins; ++k) {
        const double f = k * binHz; double s = 0;
        for (int i = 0; i < kFormants; ++i) { const double hb = v.BW[i] * 0.5, d = f - v.F[i]; s += std::pow(10.0, v.dB[i] / 10.0) * (hb * hb) / (d * d + hb * hb); }
        const double tilt = 1.0 / (1.0 + std::pow(f / 1200.0, 2.0)) * 1.0 / (1.0 + std::pow(f / 6000.0, 4.0));
        out[k] = std::max(1e-7, s * tilt + 1e-6 * tilt);
    }
}
float fitVowelScale(const Analysis& a, int set) {
    const auto& T = vowelTable(set); std::vector<std::pair<float, float>> m;
    for (int i = 0; i < a.frames(); i += 2) if (a.voiced(i) && a.formants[(size_t) i][0].hz > 0 && a.formants[(size_t) i][1].hz > 0) m.push_back({ a.formants[(size_t) i][0].hz, a.formants[(size_t) i][1].hz });
    if (m.size() < 5) return 1.0f;
    double s = 1.0;
    for (int it = 0; it < 4; ++it) {
        double sum = 0; int cnt = 0;
        for (auto& q : m) {
            int best = 0; double bd = 1e9;
            for (int v = 0; v < kVowels; ++v) { const double d = std::pow(std::log(q.first / (s * T[(size_t) v].F[0])), 2) + std::pow(std::log(q.second / (s * T[(size_t) v].F[1])), 2); if (d < bd) { bd = d; best = v; } }
            sum += std::log(q.first / T[(size_t) best].F[0]) + std::log(q.second / T[(size_t) best].F[1]); cnt += 2;
        }
        s = std::exp(sum / cnt);
    }
    return juce::jlimit(0.7f, 1.4f, (float) s);
}

float envelopeContrast(const Analysis& a) {
    std::vector<int> idx; for (int i = 0; i < a.frames(); ++i) if (a.voiced(i)) idx.push_back(i);
    if (idx.empty()) return 0;
    std::vector<float> c; const int step = std::max(1, (int) idx.size() / 40); std::vector<double> row((size_t) a.bins());
    const double binHz = a.fs / (double) a.fftSize; const int k0 = (int) (200 / binHz), k1 = std::min(a.bins() - 2, (int) (4000 / binHz));
    for (size_t n = 0; n < idx.size(); n += (size_t) step) {
        a.specRow(idx[n], row.data()); double lo = 1e9, hi = -1e9;
        for (int k = k0 + 1; k < k1; ++k) { const double f = k * binHz; const double v = 10 * std::log10(1e-30 + (row[(size_t) k - 1] + row[(size_t) k] + row[(size_t) k + 1]) / 3) + 10 * std::log10(1 + (f / 300) * (f / 300)); lo = std::min(lo, v); hi = std::max(hi, v); }
        c.push_back((float) (hi - lo));
    }
    std::sort(c.begin(), c.end()); return c[c.size() / 2];
}

// ================================================================================================================ curves
float Curve::at(double t) const {
    if (pts.empty()) return 0;
    if (!(t > pts.front().t)) return pts.front().v;
    if (t >= pts.back().t) return pts.back().v;
    auto it = std::upper_bound(pts.begin(), pts.end(), t, [](double a, const Pt& p) { return a < p.t; });
    const Pt& b = *it; const Pt& a = *(it - 1); const double span = b.t - a.t; const float f = span > 1e-9 ? (float) ((t - a.t) / span) : 0.0f;
    return a.v + (b.v - a.v) * f;
}
void Curve::add(double t, float v, double mergeWithin) {
    if (!std::isfinite(t) || !std::isfinite(v)) return;
    for (auto& p : pts) if (std::abs(p.t - t) <= mergeWithin) { p.v = v; return; }
    if ((int) pts.size() >= kMaxPoints) return;
    auto it = std::lower_bound(pts.begin(), pts.end(), t, [](const Pt& p, double a) { return p.t < a; }); pts.insert(it, { t, v });
}
void Curve::erase(double t0, double t1) { pts.erase(std::remove_if(pts.begin(), pts.end(), [=](const Pt& p) { return p.t >= t0 && p.t <= t1; }), pts.end()); }
void Curve::sanitise(float lo, float hi) {
    pts.erase(std::remove_if(pts.begin(), pts.end(), [](const Pt& p) { return !std::isfinite(p.t) || !std::isfinite(p.v) || p.t < 0 || p.t > 86400; }), pts.end());
    for (auto& p : pts) p.v = juce::jlimit(lo, hi, p.v);
    std::stable_sort(pts.begin(), pts.end(), [](const Pt& a, const Pt& b) { return a.t < b.t; });
    if ((int) pts.size() > kMaxPoints) pts.resize((size_t) kMaxPoints);
}
juce::String Curve::save() const { juce::String s; for (auto& p : pts) s << juce::String(p.t, 5) << ":" << juce::String(p.v, 4) << ";"; return s; }
bool Curve::load(const juce::String& s) {
    pts.clear();
    for (auto& tok : juce::StringArray::fromTokens(s, ";", "")) {
        if (tok.isEmpty()) continue;
        const int c = tok.indexOfChar(':'); if (c < 0) continue;
        const double t = tok.substring(0, c).getDoubleValue(); const float v = tok.substring(c + 1).getFloatValue();
        if (std::isfinite(t) && std::isfinite(v)) pts.push_back({ t, v });
        if ((int) pts.size() >= kMaxPoints) break;
    }
    std::stable_sort(pts.begin(), pts.end(), [](const Pt& a, const Pt& b) { return a.t < b.t; }); return true;
}
bool Curve::operator==(const Curve& o) const {
    if (pts.size() != o.pts.size()) return false;
    for (size_t i = 0; i < pts.size(); ++i) if (std::abs(pts[i].t - o.pts[i].t) > 1e-9 || std::abs(pts[i].v - o.pts[i].v) > 1e-6f) return false;
    return true;
}
const juce::StringArray& scaleNames() { static const juce::StringArray s { "Chromatic", "Major", "Minor", "Pentatonic", "Dorian", "Mixolydian", "Whole tone" }; return s; }
static const int kMasks[7][12] = { { 1,1,1,1,1,1,1,1,1,1,1,1 }, { 1,0,1,0,1,1,0,1,0,1,0,1 }, { 1,0,1,1,0,1,0,1,1,0,1,0 }, { 1,0,1,0,1,0,0,1,0,1,0,0 },
                                   { 1,0,1,1,0,1,0,1,0,1,1,0 }, { 1,0,1,0,1,1,0,1,0,1,1,0 }, { 1,0,1,0,1,0,1,0,1,0,1,0 } };
static bool inScale(int n, int root, int scale) { return kMasks[juce::jlimit(0, 6, scale)][((n - root) % 12 + 12) % 12] != 0; }
float snapMidi(float midi, int root, int scale) {
    if (!std::isfinite(midi)) return midi;
    float best = std::round(midi); float bd = 1e9f;
    for (int d = -6; d <= 6; ++d) { const int n = (int) std::round(midi) + d; if (!inScale(n, root, scale)) continue; const float dist = std::abs((float) n - midi); if (dist < bd) { bd = dist; best = (float) n; } }
    return best;
}
float scaleStep(float midi, int steps, int root, int scale) {
    int n = (int) snapMidi(midi, root, scale); const int dir = steps > 0 ? 1 : -1;
    for (int s = 0; s < std::abs(steps); ++s) { do { n += dir; } while (!inScale(n, root, scale)); }
    return (float) n;
}

// ================================================================================================================ render helpers
void warpEnvelope(const double* in, int bins, double factor, double* out) {
    if (std::abs(factor - 1.0) < 1e-6) { if (out != in) std::copy(in, in + bins, out); return; }
    std::vector<double> tmp((size_t) bins);
    for (int k = 0; k < bins; ++k) {
        const double pos = juce::jlimit(0.0, (double) bins - 1, k / factor); const int i = (int) pos; const double f = pos - i;
        const double l0 = lg(in[i]), l1 = lg(in[std::min(bins - 1, i + 1)]); tmp[(size_t) k] = std::exp(l0 + (l1 - l0) * f);
    }
    std::copy(tmp.begin(), tmp.end(), out);
}
double frameEnergy(const double* spec, int bins) { double e = 0; for (int k = 0; k < bins; ++k) e += spec[k]; return e; }
// Breath calibration. D4C estimates aperiodicity in 3 kHz bands anchored at -60 dB at 0 Hz, so below ~3 kHz a breathy
// voice comes back clean and buzzy, and its upper bands come back ~6 dB too harmonic. Below 2.5 kHz the aperiodicity is
// therefore taken from the frame's measured periodicity r (a logistic in -log10(1 - r), fitted so the resynthesised
// harmonic-to-noise ratio matches the input on breathy, medium and clean synthetic vowels); above 4 kHz
// D4C's value is raised by 6 dB; between the two they cross-fade. BREATH then moves around this natural point.
double breathCalibration(double hz) { return hz >= 4000 ? std::pow(10.0, 6.0 / 20.0) : hz <= 2500 ? 1.0 : std::pow(10.0, 6.0 * (hz - 2500) / 1500.0 / 20.0); }
void calibrateAperiodicity(double* ap, int bins, int fs, double periodicity) {
    const double binHz = fs / (2.0 * (bins - 1)); const double r = juce::jlimit(0.05, 0.9999, periodicity);
    const double x = -std::log10(std::max(1e-4, 1.0 - r)), low = 0.85 / (1.0 + std::exp(2.585 * (x - 1.586)));
    for (int k = 0; k < bins; ++k) {
        const double f = k * binHz; const double hi = std::min(1.0, ap[k] * breathCalibration(f));
        const double lo = f < 500 ? low * (0.5 + 0.5 * f / 500.0) : low;
        const double w = f <= 2500 ? 0.0 : f >= 4000 ? 1.0 : (f - 2500) / 1500.0;
        ap[k] = juce::jlimit(0.0, 1.0, (1.0 - w) * lo + w * hi);
    }
}
bool RenderParams::stereoOut() const { if (doubles >= 2) return true; for (auto& h : harmony) if (h.steps != 0 && h.level > 0) return true; return false; }

double RenderInfo::outputToSource(double tOut) const {
    if (tSrc.empty()) return tOut;
    const double pos = juce::jlimit(0.0, (double) tSrc.size() - 1, tOut * 1000.0 / framePeriod); const int i = (int) pos; const int j = std::min((int) tSrc.size() - 1, i + 1);
    return tSrc[(size_t) i] + (tSrc[(size_t) j] - tSrc[(size_t) i]) * (pos - i);
}
double RenderInfo::sourceToOutput(double t) const {
    if (tSrc.empty()) return t;
    for (size_t j = 1; j < tSrc.size(); ++j) if (tSrc[j] >= t && tSrc[j - 1] <= t) { const double d = tSrc[j] - tSrc[j - 1]; const double f = d > 1e-9 ? (t - tSrc[j - 1]) / d : 0.0; return (j - 1 + f) * framePeriod / 1000.0; }
    return t <= tSrc.front() ? 0.0 : (tSrc.size() - 1) * framePeriod / 1000.0;
}

namespace {
std::vector<double> effF0(const Analysis& a, const RenderParams& p, std::vector<uint8_t>* forced) {
    auto f0 = gateTrack(a.f0Raw, a.periodicity, juce::jlimit(0.0f, 1.0f, p.voicing));
    const int F = (int) f0.size(); std::vector<uint8_t> fr((size_t) F, 0);
    for (const auto& fx : p.fixes) {
        if (!std::isfinite(fx.t0) || !std::isfinite(fx.t1)) continue;
        const int i0 = a.frameAt(std::min(fx.t0, fx.t1)), i1 = a.frameAt(std::max(fx.t0, fx.t1));
        if (fx.voicing < 0) { for (int i = i0; i <= i1; ++i) f0[(size_t) i] = 0; continue; }
        if (fx.voicing > 0) {
            for (int i = i0; i <= i1; ++i) if (f0[(size_t) i] <= 0) { if (a.f0Raw[(size_t) i] > 0) { f0[(size_t) i] = a.f0Raw[(size_t) i]; fr[(size_t) i] = 1; } }
            for (int i = i0; i <= i1; ++i) if (f0[(size_t) i] <= 0) {   // no pitch at all here: bridge from the neighbours
                int l = i - 1; while (l >= 0 && f0[(size_t) l] <= 0) --l;
                int r = i + 1; while (r < F && f0[(size_t) r] <= 0) ++r;
                double v = 0;
                if (l >= 0 && r < F) v = std::exp(std::log(f0[(size_t) l]) + (std::log(f0[(size_t) r]) - std::log(f0[(size_t) l])) * (i - l) / (double) (r - l));
                else if (l >= 0) v = f0[(size_t) l]; else if (r < F) v = f0[(size_t) r];
                if (v > 0) { f0[(size_t) i] = v; fr[(size_t) i] = 1; }
            }
        }
    }
    for (const auto& fx : p.fixes) {
        if (std::abs(fx.semis) < 1e-4f || !std::isfinite(fx.semis)) continue;
        const int i0 = a.frameAt(std::min(fx.t0, fx.t1)), i1 = a.frameAt(std::max(fx.t0, fx.t1)); const double r = std::exp2(juce::jlimit(-36.0f, 36.0f, fx.semis) / 12.0);
        for (int i = i0; i <= i1; ++i) if (f0[(size_t) i] > 0) f0[(size_t) i] *= r;
    }
    if (forced) *forced = std::move(fr);
    return f0;
}
} // namespace
std::vector<double> effectiveF0(const Analysis& a, const RenderParams& p) { return effF0(a, p, nullptr); }

std::vector<double> tunePitch(const std::vector<double>& midi, double fp, int root, int scale, float retuneMs, float flatten, float humanize) {
    const int F = (int) midi.size(); std::vector<double> out(midi);
    const int half = std::max(1, (int) std::lround(90.0 / fp));       // 180 ms boxcar: one vibrato period ~5.5 Hz
    const int stable = std::max(1, (int) std::lround(40.0 / fp));
    flatten = juce::jlimit(0.0f, 1.0f, flatten); humanize = juce::jlimit(0.0f, 1.0f, humanize);
    for (int i = 0; i < F;) {
        if (!std::isfinite(midi[(size_t) i])) { ++i; continue; }
        int j = i; while (j < F && std::isfinite(midi[(size_t) j])) ++j;   // voiced run [i, j)
        const int len = j - i; std::vector<double> c((size_t) len), pre((size_t) len + 1, 0.0);
        for (int k = 0; k < len; ++k) pre[(size_t) k + 1] = pre[(size_t) k] + midi[(size_t) (i + k)];
        for (int k = 0; k < len; ++k) { const int a = std::max(0, k - half), b = std::min(len, k + half + 1); c[(size_t) k] = (pre[(size_t) b] - pre[(size_t) a]) / (b - a); }
        double cur = snapMidi((float) c[0], root, scale), T = cur; int cand = 0; double candNote = cur; int age = 0;
        for (int k = 0; k < len; ++k) {
            const double s = snapMidi((float) c[(size_t) k], root, scale);
            if (std::abs(s - cur) > 0.01 && std::abs(c[(size_t) k] - cur) > 0.65) { if (std::abs(s - candNote) < 0.01) ++cand; else { candNote = s; cand = 1; } if (cand >= stable) { cur = candNote; cand = 0; age = 0; } }
            else cand = 0;
            double tau = retuneMs;
            if (humanize > 0 && age * fp > 250.0) tau = std::max((double) retuneMs, 20.0) * (1.0 + 4.0 * humanize * std::min(1.0, (age * fp - 250.0) / 750.0));
            const double alpha = tau <= 0.5 ? 1.0 : 1.0 - std::exp(-fp / tau);
            T += (cur - T) * alpha; ++age;
            out[(size_t) (i + k)] = T + (1.0 - flatten) * (midi[(size_t) (i + k)] - c[(size_t) k]);
        }
        i = j;
    }
    return out;
}

namespace {
// ---- streamed WORLD synthesis: frames go in, samples come out; memory stays flat however long the render is ------
class StreamSynth {
public:
    StreamSynth(int fs_, double fp_, int fft_, size_t target_) : fs(fs_), fp(fp_), fft(fft_), bins(fft_ / 2 + 1), target(target_) {
        InitializeSynthesizer(fs, fp, fft, kBuf, kRing, &s); out.reserve(target + (size_t) kBuf);
    }
    ~StreamSynth() { DestroySynthesizer(&s); }
    void push(double f0, const double* S, const double* P) {
        if (!cur) { cur = std::make_unique<Block>(); cur->f0.resize(kBlock); cur->spec.resize((size_t) kBlock * (size_t) bins); cur->ap.resize((size_t) kBlock * (size_t) bins); }
        const int k = cur->n++; cur->f0[(size_t) k] = f0;
        std::copy(S, S + bins, cur->spec.data() + (size_t) k * (size_t) bins); std::copy(P, P + bins, cur->ap.data() + (size_t) k * (size_t) bins);
        if (cur->n == kBlock) flush();
    }
    bool ok() const { return !locked; }
    std::vector<float> finish() {
        std::vector<double> S((size_t) bins, 1e-16), P((size_t) bins, 1.0);
        const int extra = (int) std::ceil((fft + 2.0 * kBuf) / (fp / 1000.0 * fs)) + 2;
        for (int i = 0; i < extra && out.size() < target; ++i) push(0.0, S.data(), P.data());
        flush(); drain(); out.resize(target, 0.0f); return std::move(out);
    }
private:
    static constexpr int kBuf = 256, kRing = 32, kBlock = 8;
    struct Block { std::vector<double> f0, spec, ap; std::vector<double*> sp, aps; int n = 0; };
    void flush() {
        if (!cur || cur->n == 0) return;
        Block& b = *cur; b.sp.resize((size_t) b.n); b.aps.resize((size_t) b.n);
        for (int k = 0; k < b.n; ++k) { b.sp[(size_t) k] = b.spec.data() + (size_t) k * (size_t) bins; b.aps[(size_t) k] = b.ap.data() + (size_t) k * (size_t) bins; }
        int guard = 0;
        while (AddParameters(b.f0.data(), b.n, b.sp.data(), b.aps.data(), &s) == 0) { if (!drain() && ++guard > 4) { locked = true; break; } }
        drain();
        live.push_back(std::move(cur)); while ((int) live.size() > kRing + 2) live.pop_front();
    }
    bool drain() { bool any = false; while (out.size() < target + (size_t) kBuf && Synthesis2(&s) != 0) { for (int j = 0; j < kBuf; ++j) out.push_back((float) s.buffer[j]); any = true; } return any; }
    WorldSynthesizer s {}; int fs; double fp; int fft, bins; size_t target; bool locked = false;
    std::unique_ptr<Block> cur; std::deque<std::unique_ptr<Block>> live; std::vector<float> out;
};

// ---- the per-output-frame plan --------------------------------------------------------------------------------------
struct Plan {
    int F = 0; int src0 = 0, src1 = 0; bool borrow = false, loopA = false;
    std::vector<double> posA, posB, f0, fIn; std::vector<float> tSrc; std::vector<uint8_t> forced;
    std::vector<float> loopW; std::vector<double> loopAlt;   // FollowBLoop crossfade into the loop start
};
double interpTrack(const std::vector<double>& f, double pos) {   // log-linear between two voiced frames, else the nearest
    const int n = (int) f.size(); if (n == 0) return 0; const int i = juce::jlimit(0, n - 1, (int) pos), j = std::min(n - 1, i + 1); const double fr = pos - i;
    const double x = f[(size_t) i], y = f[(size_t) j];
    if (x > 0 && y > 0) return std::exp(std::log(x) + (std::log(y) - std::log(x)) * fr);
    return fr < 0.5 ? x : y;
}

// decodes A's / B's frames with caching (each voice walks the frames in order)
struct FrameCache {
    const Analysis* an = nullptr; int idx[2] { -1, -1 }; std::vector<double> l[2], ap[2];
    int get(int i, int avoidSlot) {
        for (int s = 0; s < 2; ++s) if (idx[s] == i) return s;
        const int s = avoidSlot == 0 ? 1 : avoidSlot == 1 ? 0 : (idx[0] < 0 ? 0 : 1);
        l[s].resize((size_t) an->bins()); ap[s].resize((size_t) an->bins());
        decodeLogRow(*an, i, l[s].data()); an->apRow(i, ap[s].data()); idx[s] = i; return s;
    }
    // log spec + ap at a fractional frame
    void at(double pos, double* logS, double* apOut) {
        const int n = an->frames(); const int i = juce::jlimit(0, n - 1, (int) pos), j = std::min(n - 1, i + 1); const double f = juce::jlimit(0.0, 1.0, pos - i);
        const int si = get(i, -1), sj = get(j, si);
        const int bins = an->bins(); const auto& L0 = l[si]; const auto& L1 = l[sj]; const auto& A0 = ap[si]; const auto& A1 = ap[sj];
        for (int k = 0; k < bins; ++k) { logS[k] = L0[(size_t) k] + (L1[(size_t) k] - L0[(size_t) k]) * f; if (apOut) apOut[k] = A0[(size_t) k] + (A1[(size_t) k] - A0[(size_t) k]) * f; }
    }
};

struct EnvBuilder {
    const Analysis& a; const Analysis* b; const RenderParams& p; const Plan& plan; int fs, bins; bool sameGrid;
    FrameCache ca, cb, cl; std::vector<double> L, Ap, Lb, Apb, Bres, ApbRes, tmp, held, cal, La, Apa; double heldE = 0, bMax = 0, bGate = 0;
    Vowel custom {};
    EnvBuilder(const Analysis& a_, const Analysis* b_, const RenderParams& p_, const Plan& pl, double bMax_) : a(a_), b(b_), p(p_), plan(pl), fs(a_.fs), bins(a_.bins()), bMax(bMax_) {
        ca.an = &a; cl.an = &a; if (b) cb.an = b; sameGrid = b && b->bins() == bins && b->fs == fs;
        L.resize((size_t) bins); Ap.resize((size_t) bins); La.resize((size_t) bins); Apa.resize((size_t) bins); tmp.resize((size_t) bins); cal.resize((size_t) bins);
        if (b) { Lb.resize((size_t) b->bins()); Apb.resize((size_t) b->bins()); Bres.resize((size_t) bins); ApbRes.resize((size_t) bins); }
        bGate = bMax * 1e-6 * (b ? bins / (double) std::max(1, b->bins()) : 1.0);   // B's energies were summed on B's grid
        custom.name = "X"; for (int k = 0; k < kFormants; ++k) { custom.F[k] = p.customF[(size_t) k]; custom.BW[k] = 50.0f + 0.05f * p.customF[(size_t) k]; } const float d[4] = { 0, -6, -12, -18 }; std::copy(d, d + 4, custom.dB);
    }
    // the envelope S (power) + aperiodicity P of output frame j for a voice at fVoice (fIn = the source pitch there)
    void build(int j, double fVoice, double extraFormantSemis, double* S, double* P) {
        const double tSrc = plan.tSrc[(size_t) j];
        ca.at(plan.posA[(size_t) j], L.data(), Ap.data());
        if (!plan.loopW.empty() && plan.loopW[(size_t) j] > 0) {   // crossfade into the loop start
            const double w = plan.loopW[(size_t) j]; cl.at(plan.loopAlt[(size_t) j], La.data(), Apa.data());
            for (int k = 0; k < bins; ++k) { L[(size_t) k] = (1 - w) * L[(size_t) k] + w * La[(size_t) k]; Ap[(size_t) k] = (1 - w) * Ap[(size_t) k] + w * Apa[(size_t) k]; }
        }
        for (int k = 0; k < bins; ++k) { S[k] = std::exp(L[(size_t) k]); P[k] = Ap[(size_t) k]; }
        const double eA = frameEnergy(S, bins);
        if (plan.borrow && b) {
            cb.at(plan.posB[(size_t) j], Lb.data(), Apb.data());
            const double* srcB; const double* apB;
            for (size_t k = 0; k < Lb.size(); ++k) Lb[k] = std::exp(Lb[k]);
            if (sameGrid) { srcB = Lb.data(); apB = Apb.data(); }
            else { resampleRow(Lb.data(), b->bins(), b->fs, bins, fs, Bres.data()); resampleRow(Apb.data(), b->bins(), b->fs, bins, fs, ApbRes.data()); for (auto& v : ApbRes) v = juce::jlimit(0.0, 1.0, v); srcB = Bres.data(); apB = ApbRes.data(); }
            const double eB = frameEnergy(srcB, bins);
            const double* shape = srcB; double shapeE = eB;
            if (eB <= bGate) { if (!held.empty()) { shape = held.data(); shapeE = heldE; } }
            else { held.assign(srcB, srcB + bins); heldE = eB; }
            const double eBf = std::max(eB, bMax * 1e-9), eT = std::exp((1.0 - p.dynamics) * lg(eA) + p.dynamics * lg(eBf));
            const double gainB = shapeE > 0 ? eT / shapeE : 0.0, mix = juce::jlimit(0.0f, 1.0f, p.borrowMix);
            for (int k = 0; k < bins; ++k) S[k] = std::exp((1.0 - mix) * lg(S[k]) + mix * lg(shape[k] * gainB));
            const double bb = juce::jlimit(0.0f, 1.0f, p.breathFromB);
            for (int k = 0; k < bins; ++k) P[k] = juce::jlimit(0.0, 1.0, (1.0 - bb) * P[k] + bb * apB[k]);
        } else if (p.mode == RenderParams::VowelMorph) {
            const float pos = p.lane.empty() ? 0.0f : p.lane.at(tSrc);
            const auto v = vowelAt(pos, p.vowelScale, p.vowelSet, p.hasCustomVowel ? &custom : nullptr);
            vowelEnvelope(v, bins, fs, tmp.data()); const double eV = frameEnergy(tmp.data(), bins), g = eV > 0 ? eA / eV : 0.0;
            const double amt = juce::jlimit(0.0f, 1.0f, p.vowelAmount);
            for (int k = 0; k < bins; ++k) S[k] = std::exp((1.0 - amt) * lg(S[k]) + amt * lg(tmp[(size_t) k] * g));
        }
        // formants: KEEP = stay put; otherwise they follow this voice's pitch; FORMANT (+ lane, + the voice's offset) always
        const double fIn = plan.fIn[(size_t) j];
        const double fShift = p.formantSemis + (p.formantLane.empty() ? 0.0f : p.formantLane.at(tSrc)) + extraFormantSemis;
        double warp = std::exp2(juce::jlimit(-24.0, 24.0, fShift) / 12.0);
        if (!p.keepFormants) warp *= (fIn > 0 && fVoice > 0) ? fVoice / fIn : std::exp2(p.pitchSemis / 12.0);
        if (std::abs(warp - 1.0) > 1e-6) warpEnvelope(S, bins, warp, S);
        // breath: calibrated to the input's own breathiness, then BREATH (-1..+1, + lane)
        const double br = juce::jlimit(-1.0, 1.0, (double) p.breath + (p.breathLane.empty() ? 0.0 : p.breathLane.at(tSrc)));
        { const double pos = plan.posA[(size_t) j]; const int i0 = juce::jlimit(0, a.frames() - 1, (int) pos), i1 = std::min(a.frames() - 1, i0 + 1);
          const double per = a.periodicity[(size_t) i0] + (a.periodicity[(size_t) i1] - a.periodicity[(size_t) i0]) * (pos - i0);
          calibrateAperiodicity(P, bins, fs, plan.forced[(size_t) j] ? 0.5 : per); }
        for (int k = 0; k < bins; ++k) {
            double v = P[k];
            if (br > 0) v += br * (1.0 - v); else if (br < 0) v *= (1.0 + br);
            if (plan.forced[(size_t) j]) v *= 0.35;                 // a whisper / gap pushed into a note: mostly harmonic
            P[k] = juce::jlimit(0.0, 1.0, v);
        }
    }
};
} // namespace

// ================================================================================================================ render
namespace {
constexpr double kPiD = 3.14159265358979323846;
float monoSample(const AudioRef& x, int i) { if (i < 0 || i >= x.n) return 0.0f; if (x.ch.size() == 1) return x.ch[0][i]; return 0.5f * (x.ch[0][i] + x.ch[1][i]); }
float chanSample(const AudioRef& x, int c, int i) { if (i < 0 || i >= x.n) return 0.0f; return x.ch[(size_t) std::min(c, (int) x.ch.size() - 1)][i]; }

void finalise(Rendered& out, const RenderParams& p, bool normaliseAll) {
    float pk = 0; for (float v : out.L) pk = std::max(pk, std::abs(v)); for (float v : out.R) pk = std::max(pk, std::abs(v));
    const float n = (normaliseAll && p.normalise && pk > 1e-9f) ? 0.89f / pk : 1.0f;
    const float g = n * juce::Decibels::decibelsToGain(juce::jlimit(-48.0f, 24.0f, p.gainDb));
    auto soft = [](float v) { if (!std::isfinite(v)) return 0.0f; const float a = std::abs(v); if (a <= 0.95f) return v; return std::copysign(0.95f + 0.05f * std::tanh((a - 0.95f) / 0.05f), v); };
    for (auto& v : out.L) v = soft(v * g);
    for (auto& v : out.R) v = soft(v * g);
}

// ---- VOCODE carrier: A's real audio (every channel, chords and detune intact) filtered by (B's envelope / A's) ------
bool renderVocode(const Analysis& a, const AudioRef& ax, const Analysis& b, const RenderParams& p, Rendered& out, Progress prog, RenderInfo* info) {
    const int fs = a.fs; const int order = fs > 60000 ? 12 : 11, N = 1 << order, hop = N / 4, nb = N / 2 + 1;
    const int lenA = ax.n; const int lenB = (int) std::lround(b.seconds() * fs);
    int outLen = p.timeMode == RenderParams::StretchBToA ? lenA : lenB; outLen = std::min(outLen, (int) (kMaxOutputSeconds * fs)); if (outLen <= 0 || lenA <= 0) return true;
    const int chans = juce::jlimit(1, 2, (int) ax.ch.size()); const int xf = std::min(lenA / 4, (int) (0.02 * fs)); const int loopLen = std::max(1, lenA - xf);
    // the (looped) carrier
    std::vector<std::vector<float>> car((size_t) chans, std::vector<float>((size_t) outLen, 0.0f));
    for (int c = 0; c < chans; ++c) for (int pos = 0; pos < outLen; pos += loopLen)
        for (int i = 0; i < lenA && pos + i < outLen; ++i) { const float w = (pos > 0 && i < xf) ? (float) i / xf : 1.0f; auto& d = car[(size_t) c][(size_t) (pos + i)]; d = d * (1 - w) + chanSample(ax, c, i) * w; }
    auto srcSample = [&](int n) { if (n < lenA) return n; const int k = n / loopLen; return n - k * loopLen; };
    const double bRatio = p.timeMode == RenderParams::StretchBToA ? b.seconds() / std::max(1e-9, a.seconds()) : 1.0;
    const auto bVoiced = gateTrack(b.f0Raw, b.periodicity, p.voicing);
    // B frame energies (gate / hold / consonants)
    std::vector<double> bE((size_t) b.frames()); double bMax = 0; { std::vector<double> r((size_t) b.bins()); for (int i = 0; i < b.frames(); ++i) { b.specRow(i, r.data()); bE[(size_t) i] = frameEnergy(r.data(), b.bins()); bMax = std::max(bMax, bE[(size_t) i]); } }
    const double bGate = bMax * 1e-6, bAudible = bMax * 3.2e-5;
    juce::dsp::FFT fft(order); std::vector<float> win((size_t) N), buf((size_t) N * 2);
    for (int i = 0; i < N; ++i) win[(size_t) i] = (float) std::sqrt(0.5 - 0.5 * std::cos(2 * kPiD * i / N));
    float fftScale = 1.0f; { std::fill(buf.begin(), buf.end(), 0.0f); buf[0] = 1.0f; fft.performRealOnlyForwardTransform(buf.data()); fft.performRealOnlyInverseTransform(buf.data()); fftScale = std::abs(buf[0]) > 1e-12f ? buf[0] : 1.0f; }
    const float olaNorm = 1.0f / (2.0f * fftScale);   // sqrt-Hann^2 at 75 % overlap sums to 2
    out.fs = fs; out.L.assign((size_t) outLen, 0.0f); if (chans == 2) out.R.assign((size_t) outLen, 0.0f);
    std::vector<double> rowA((size_t) a.bins()), rowB((size_t) b.bins()), EA((size_t) nb), EB((size_t) nb), held, G((size_t) nb), noiseMag((size_t) nb);
    double heldE = 0; juce::Random rnd(0x5eed);
    const double mix = juce::jlimit(0.0f, 1.0f, p.borrowMix), dyn = juce::jlimit(0.0f, 1.0f, p.dynamics);
    const int frames = (outLen + N) / hop + 1;
    for (int m = 0; m < frames; ++m) {
        if ((m & 31) == 0) { if (prog.stop()) return false; prog.set(90 * m / frames); }
        const int c0 = m * hop - N / 2, centre = juce::jlimit(0, outLen - 1, m * hop);
        const double posA = srcSample(centre) / (double) fs * 1000.0 / a.framePeriod, tB = centre / (double) fs * bRatio;
        const int iA = juce::jlimit(0, a.frames() - 1, (int) std::lround(posA)), iB = juce::jlimit(0, b.frames() - 1, (int) std::lround(tB * 1000.0 / b.framePeriod));
        a.specRow(iA, rowA.data()); resampleRow(rowA.data(), a.bins(), fs, nb, fs, EA.data());
        b.specRow(iB, rowB.data()); resampleRow(rowB.data(), b.bins(), b.fs, nb, fs, EB.data());
        const double eA = std::max(1e-30, frameEnergy(EA.data(), nb)); double eB = frameEnergy(EB.data(), nb);
        const double* shape = EB.data(); double shapeE = eB;
        if (bE[(size_t) iB] <= bGate) { if (!held.empty()) { shape = held.data(); shapeE = heldE; } } else { held = EB; heldE = eB; }
        // per-bin filter (B's shape over A's, at A's level) + a frame gain so the loudness follows A^(1-dyn) * B^dyn
        double eShaped = 0;
        for (int k = 0; k < nb; ++k) { const double r = juce::jlimit(1e-6, 1e4, (shape[k] * (eA / std::max(1e-30, shapeE))) / std::max(1e-30, EA[(size_t) k])); G[(size_t) k] = std::pow(r, 0.5 * mix); eShaped += EA[(size_t) k] * G[(size_t) k] * G[(size_t) k]; }
        const double eT = std::exp((1.0 - dyn) * std::log(eA) + dyn * std::log(std::max(eB, 1e-30)));
        const double post = std::sqrt(eT / std::max(1e-30, eShaped)); for (auto& g : G) g *= post;
        const bool consonant = p.consonantsFromB && !(bVoiced[(size_t) iB] > 0) && bE[(size_t) iB] > bAudible;
        for (int c = 0; c < chans; ++c) {
            std::fill(buf.begin(), buf.end(), 0.0f); double eIn = 0;
            for (int i = 0; i < N; ++i) { const int n = c0 + i; const float v = (n >= 0 && n < outLen) ? car[(size_t) c][(size_t) n] : 0.0f; buf[(size_t) i] = v * win[(size_t) i]; eIn += (double) buf[(size_t) i] * buf[(size_t) i]; }
            fft.performRealOnlyForwardTransform(buf.data(), true);
            for (int k = 0; k < nb; ++k) { buf[(size_t) (2 * k)] *= (float) G[(size_t) k]; buf[(size_t) (2 * k + 1)] *= (float) G[(size_t) k]; }
            if (consonant) {   // B's noise (s, f, t...) at the frame's target level
                const double kappa = 0.5 * N * eIn / eA;   // envelope power -> |bin|^2 units for this carrier frame (Parseval)
                const double gN = std::sqrt(std::max(0.0, kappa * eT / std::max(1e-30, eB)));
                for (int k = 1; k < nb - 1; ++k) { const double mag = std::sqrt(std::max(0.0, EB[(size_t) k])) * gN; const double ph = rnd.nextDouble() * 2 * kPiD;
                    buf[(size_t) (2 * k)] += (float) (mag * std::cos(ph)); buf[(size_t) (2 * k + 1)] += (float) (mag * std::sin(ph)); }
            }
            fft.performRealOnlyInverseTransform(buf.data());
            auto& o = c == 0 ? out.L : out.R;
            for (int i = 0; i < N; ++i) { const int n = c0 + i; if (n >= 0 && n < outLen) o[(size_t) n] += buf[(size_t) i] * win[(size_t) i] * olaNorm; }
        }
    }
    if (info) {
        info->fs = fs; info->framePeriod = a.framePeriod; info->vocoded = true; const int F = (int) (outLen / (a.framePeriod / 1000.0 * fs)) + 1;
        info->f0.resize((size_t) F); info->tSrc.resize((size_t) F); info->formants.resize((size_t) F); std::vector<double> r((size_t) b.bins());
        for (int j = 0; j < F; ++j) { const int n = std::min(outLen - 1, (int) (j * a.framePeriod / 1000.0 * fs)); const int iA = a.frameAt(srcSample(n) / (double) fs);
            info->tSrc[(size_t) j] = (float) (srcSample(n) / (double) fs); info->f0[(size_t) j] = a.f0[(size_t) iA];
            const int iB = b.frameAt(n / (double) fs * bRatio); b.specRow(iB, r.data()); info->formants[(size_t) j] = b.formants[(size_t) iB]; }
    }
    prog.set(95); return true;
}

// ---- keep percussive attacks: around each onset, the original audio (full band when the pitch there is unchanged,
//      else only above 2.5 kHz) replaces the resynthesis for ~27 ms, which also removes WORLD's pre-echo -------------
struct HP2 { double b0, b1, b2, a1, a2, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    HP2(double fc, double fs) { const double w = 2 * kPiD * fc / fs, c = std::cos(w), s = std::sin(w), al = s / std::sqrt(2.0), a0 = 1 + al; b0 = (1 + c) / 2 / a0; b1 = -(1 + c) / a0; b2 = (1 + c) / 2 / a0; a1 = -2 * c / a0; a2 = (1 - al) / a0; }
    double tick(double x) { const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2; x2 = x1; x1 = x; y2 = y1; y1 = y; return y; } };
void applyTransients(Rendered& seg, const AudioRef& x, const Analysis& a, const RenderParams& p, const Plan& plan, const std::vector<double>& ef) {
    if (!p.transients || !x.valid() || a.onsets.empty() || plan.F < 2) return;
    const int fs = a.fs; const double fp = a.framePeriod, t0 = plan.tSrc[0], tEnd = plan.tSrc[(size_t) plan.F - 1];
    const double speed = (plan.F > 1) ? (plan.tSrc[(size_t) plan.F - 1] - t0) / ((plan.F - 1) * fp / 1000.0) : 1.0;
    const float need = 1.0f - juce::jlimit(0.0f, 1.0f, p.transientSens);
    const int pre = (int) (0.012 * fs), rise = std::max(1, (int) (0.002 * fs)), hold = (int) (0.015 * fs), fall = (int) (0.008 * fs);
    const bool vowelOrBorrow = p.mode == RenderParams::VowelMorph || plan.borrow;
    const double fShift = std::abs(p.formantSemis) + (p.formantLane.empty() ? 0.0 : 1.0);
    for (const auto& o : a.onsets) {
        if (o.strength < need || o.t < t0 || o.t > tEnd) continue;
        const double to = (o.t - t0) / std::max(1e-6, speed); const int on = (int) std::lround(to * fs), sn = (int) std::lround(o.t * fs);
        const int jo = juce::jlimit(0, plan.F - 1, (int) std::lround(to * 1000.0 / fp)); const int js = a.frameAt(o.t + 0.02);
        const double fo = plan.f0[(size_t) jo], fi = ef[(size_t) js];
        const bool samePitch = (fo <= 0 && fi <= 0) || (fo > 0 && fi > 0 && std::abs(1200.0 * std::log2(fo / fi)) < 30.0);
        const bool full = samePitch && !vowelOrBorrow && fShift < 0.3;
        const int s0 = on - pre - rise, s1 = on + hold + fall; if (s1 <= 0 || s0 >= (int) seg.L.size()) continue;
        auto w = [&](int n) { if (n < on - pre - rise || n >= on + hold + fall) return 0.0; if (n < on - pre) return (n - (on - pre - rise)) / (double) rise; if (n < on + hold) return 1.0; return 1.0 - (n - (on + hold)) / (double) fall; };
        for (int c = 0; c < (seg.stereo() ? 2 : 1); ++c) {
            auto& y = c == 0 ? seg.L : seg.R; const float pan = seg.stereo() ? 0.7071f : 1.0f;
            auto orig = [&](int n) { return (seg.stereo() && x.ch.size() == 2 ? chanSample(x, c, sn + (n - on)) : monoSample(x, sn + (n - on))) * pan; };
            if (full) { for (int n = std::max(0, s0); n < std::min((int) y.size(), s1); ++n) { const double ww = w(n); y[(size_t) n] = (float) ((1 - ww) * y[(size_t) n] + ww * orig(n)); } }
            else {
                HP2 hy(2500, fs), hx(2500, fs); const int warm = (int) (0.01 * fs);
                for (int n = std::max(0, s0 - warm); n < std::min((int) y.size(), s1); ++n) {
                    const double yh = hy.tick(y[(size_t) n]), xh = hx.tick(orig(n)); const double ww = w(n);
                    if (ww > 0) y[(size_t) n] = (float) (y[(size_t) n] - ww * yh + ww * xh);
                }
            }
        }
    }
}
} // namespace

bool preferVocode(const Analysis& a, const AudioRef& x, float voicing) {
    const auto f0 = gateTrack(a.f0Raw, a.periodicity, voicing); int v = 0; std::vector<float> per;
    for (size_t i = 0; i < f0.size(); ++i) if (f0[i] > 0) { ++v; per.push_back(a.periodicity[i]); }
    if (f0.empty() || v < 0.6 * (double) f0.size()) return true;
    std::sort(per.begin(), per.end()); if (per[per.size() / 2] < 0.85f) return true;           // beating / detuned unison
    if (x.valid() && x.ch.size() >= 2) {                                                    // a wide stereo stack
        double ab = 0, aa = 0, bb = 0; for (int i = 0; i < x.n; i += 7) { const double l = x.ch[0][i], r = x.ch[1][i]; ab += l * r; aa += l * l; bb += r * r; }
        if (aa > 1e-12 && bb > 1e-12 && ab / std::sqrt(aa * bb) < 0.95) return true;
    }
    return false;
}

namespace {
bool renderCore(const Analysis& a, const AudioRef& aAudio, const Analysis* b, const RenderParams& p, Rendered& out, Progress prog, RenderInfo* info);
Curve shifted(const Curve& c, double dt) { Curve o = c; for (auto& q : o.pts) q.t -= dt; return o; }
} // namespace

// START / END (trim): the core renders a view of A's analysis + audio cut to [START, END) with every source-time
// setting moved by START; the info's source times are moved back. An untouched trim never comes here (old path).
bool render(const Analysis& a, const AudioRef& aAudio, const Analysis* b, const RenderParams& p, Rendered& out, Progress prog, RenderInfo* info) {
    if (a.empty() || !p.trimmed(a.seconds())) return renderCore(a, aAudio, b, p, out, prog, info);
    const double fp = a.framePeriod; const int Fa = a.frames();
    const double tA = juce::jlimit(0.0, a.seconds(), std::max(0.0, p.trimStart)), tB = p.trimEnd > 0 ? juce::jlimit(tA, a.seconds(), p.trimEnd) : a.seconds();
    const int i0 = juce::jlimit(0, std::max(0, Fa - 2), (int) std::floor(tA * 1000.0 / fp)), i1 = juce::jlimit(i0 + 2, Fa, (int) std::ceil(tB * 1000.0 / fp) + 1);
    const double t0 = i0 * fp / 1000.0;
    const int s0 = juce::jlimit(0, a.length, (int) std::lround(t0 * a.fs)), s1 = juce::jlimit(s0, a.length, (int) std::lround((i1 - 1) * fp / 1000.0 * a.fs) + 1);
    Analysis v; v.fs = a.fs; v.fftSize = a.fftSize; v.framePeriod = fp; v.length = s1 - s0; v.storedBins = a.storedBins; v.lowBins = a.lowBins; v.dec = a.dec; v.nAp = a.nAp;
    auto cut = [&](const auto& src, auto& dst) { dst.assign(src.begin() + i0, src.begin() + i1); };
    cut(a.f0, v.f0); cut(a.f0Raw, v.f0Raw); cut(a.periodicity, v.periodicity); cut(a.rms, v.rms); cut(a.formants, v.formants);
    v.specQ.assign(a.specQ.begin() + (long) i0 * a.storedBins, a.specQ.begin() + (long) i1 * a.storedBins);
    v.apCoded.assign(a.apCoded.begin() + (long) i0 * a.nAp, a.apCoded.begin() + (long) i1 * a.nAp);
    for (auto o : a.onsets) if (o.t >= t0 && o.t < tB) { o.t -= (float) t0; v.onsets.push_back(o); }
    AudioRef x; if (aAudio.valid()) { for (auto* c : aAudio.ch) x.ch.push_back(c + s0); x.n = std::min(aAudio.n, s1) - s0; x.fs = aAudio.fs; }
    RenderParams q = p; q.trimStart = q.trimEnd = -1;
    q.contour = shifted(p.contour, t0); q.lane = shifted(p.lane, t0); q.pitchLane = shifted(p.pitchLane, t0); q.formantLane = shifted(p.formantLane, t0); q.breathLane = shifted(p.breathLane, t0);
    for (auto& f : q.fixes) { f.t0 -= t0; f.t1 -= t0; }
    if (q.regionStart >= 0) { q.regionStart = std::max(0.0, q.regionStart - t0); q.regionEnd -= t0; if (q.regionEnd <= q.regionStart + 0.02) q.regionStart = q.regionEnd = -1; }
    const bool ok = renderCore(v, x, b, q, out, prog, info);
    if (info) for (auto& t : info->tSrc) t += (float) t0;
    return ok;
}

namespace {
bool renderCore(const Analysis& a, const AudioRef& aAudio, const Analysis* b, const RenderParams& p, Rendered& out, Progress prog, RenderInfo* info) {
    out = Rendered(); if (info) *info = RenderInfo();
    if (a.empty() || a.fs <= 0 || a.frames() < 2) return true;
    const int fs = a.fs, bins = a.bins(), Fa = a.frames(); const double fp = a.framePeriod;
    const bool borrow = p.mode == RenderParams::Borrow && b != nullptr && !b->empty() && b->frames() >= 2;
    std::vector<uint8_t> forcedSrc; const auto ef = effF0(a, p, &forcedSrc);
    const bool audioOk = aAudio.valid() && aAudio.fs == fs;
    if (borrow && audioOk) {
        bool vocode = p.carrier == RenderParams::CarrierVocode;
        if (p.carrier == RenderParams::CarrierAuto) vocode = preferVocode(a, aAudio, p.voicing);
        if (vocode) { if (!renderVocode(a, aAudio, *b, p, out, prog, info)) { out = Rendered(); return false; } finalise(out, p, true); prog.set(100); return true; }
    }
    // ---- region: process [rs, re] (+10 ms each side for the crossfades), the rest is A untouched
    const bool region = !borrow && audioOk && p.regionStart >= 0 && p.regionEnd > p.regionStart + 0.02 && std::isfinite(p.regionStart) && std::isfinite(p.regionEnd);
    int src0 = 0, src1 = Fa;
    if (region) { src0 = a.frameAt(std::max(0.0, p.regionStart - 0.01)); src1 = std::max(src0 + 2, std::min(Fa, a.frameAt(p.regionEnd + 0.01) + 1)); }
    // ---- the plan: where each output frame reads A (and B), and its pitch
    Plan plan; plan.borrow = borrow; plan.src0 = src0; plan.src1 = src1;
    const int Fb = borrow ? b->frames() : 0;
    int Fo = src1 - src0; double aScale = 1.0, bScale = 0.0; bool loopA = false;
    if (borrow) {
        if (p.timeMode == RenderParams::StretchBToA) { Fo = Fa; bScale = Fa > 1 ? (Fb - 1) / (double) (Fa - 1) : 0.0; }
        else { Fo = Fb; bScale = 1.0; if (p.timeMode == RenderParams::FollowBLoop) loopA = true; else aScale = Fb > 1 ? (Fa - 1) / (double) (Fb - 1) : 0.0; }
    }
    const double speed = juce::jlimit(0.25, 4.0, std::isfinite(p.speed) ? (double) p.speed : 1.0);
    int F = std::max(2, (int) std::lround(Fo / speed)); F = std::min(F, (int) (kMaxOutputSeconds * 1000.0 / fp));
    plan.F = F; plan.loopA = loopA;
    plan.posA.resize((size_t) F); plan.posB.assign((size_t) F, 0.0); plan.f0.resize((size_t) F); plan.fIn.resize((size_t) F); plan.tSrc.resize((size_t) F); plan.forced.assign((size_t) F, 0);
    std::vector<double> tuned;
    if (p.mode == RenderParams::Tune) {
        std::vector<double> m((size_t) Fa); for (int i = 0; i < Fa; ++i) m[(size_t) i] = ef[(size_t) i] > 0 ? hzMidi(ef[(size_t) i]) : std::numeric_limits<double>::quiet_NaN();
        const auto t = tunePitch(m, fp, p.root, p.scale, p.retuneMs, p.flatten, p.humanize); tuned.resize((size_t) Fa);
        for (int i = 0; i < Fa; ++i) tuned[(size_t) i] = std::isfinite(t[(size_t) i]) ? midiHz(t[(size_t) i]) : 0.0;
    }
    std::vector<double> bE; double bMax = 0;
    if (borrow) { bE.resize((size_t) Fb); std::vector<double> r((size_t) b->bins()); for (int i = 0; i < Fb; ++i) { b->specRow(i, r.data()); bE[(size_t) i] = frameEnergy(r.data(), b->bins()); bMax = std::max(bMax, bE[(size_t) i]); } }
    const double bAudible = bMax * 3.2e-5;
    const auto bVoiced = borrow ? gateTrack(b->f0Raw, b->periodicity, p.voicing) : std::vector<double>();
    constexpr int X = 4; const double Lp = Fa - X;
    if (loopA && Fa > 2 * X) { plan.loopW.assign((size_t) F, 0.0f); plan.loopAlt.assign((size_t) F, 0.0); }
    const auto& pitchSrc = p.mode == RenderParams::Tune ? tuned : ef;
    for (int j = 0; j < F; ++j) {
        const double u = j * speed; double posA; double w = 0, alt = 0;
        if (borrow) {
            if (loopA && Fa > 2 * X) { if (u < Lp) posA = u; else { posA = std::fmod(u, Lp); if (posA < X) { w = 1.0 - posA / X; alt = Lp + posA; } } }
            else if (loopA) posA = std::fmod(u, (double) Fa);
            else posA = u * aScale;
            plan.posB[(size_t) j] = juce::jlimit(0.0, (double) Fb - 1, u * bScale);
        } else posA = src0 + u;
        posA = juce::jlimit(0.0, (double) Fa - 1, posA);
        plan.posA[(size_t) j] = posA; if (!plan.loopW.empty()) { plan.loopW[(size_t) j] = (float) w; plan.loopAlt[(size_t) j] = juce::jlimit(0.0, (double) Fa - 1, alt); }
        auto blend = [&](const std::vector<double>& tr) { const double x = interpTrack(tr, posA); if (w <= 0) return x; const double y = interpTrack(tr, alt); if (x > 0 && y > 0) return std::exp((1 - w) * std::log(x) + w * std::log(y)); return w > 0.5 ? y : x; };
        plan.fIn[(size_t) j] = blend(ef); plan.f0[(size_t) j] = blend(pitchSrc);
        plan.tSrc[(size_t) j] = (float) (posA * fp / 1000.0);
        plan.forced[(size_t) j] = forcedSrc[(size_t) juce::jlimit(0, Fa - 1, (int) std::lround(posA))];
    }
    if (p.mode == RenderParams::Contour && !p.contour.empty()) {
        const double alpha = p.contourSnap && p.retuneMs > 0.5f ? 1.0 - std::exp(-fp / p.retuneMs) : 1.0; double sm = 0; bool first = true;
        const double amt = juce::jlimit(0.0f, 1.0f, p.contourAmount);
        for (int j = 0; j < F; ++j) {
            double m = p.contour.at(plan.tSrc[(size_t) j]); if (p.contourSnap) m = snapMidi((float) m, p.root, p.scale);
            if (first) { sm = m; first = false; } else sm += (m - sm) * alpha;
            const double target = midiHz(p.contourSnap ? sm : m); double& f = plan.f0[(size_t) j];
            if (f > 0) f = std::exp((1.0 - amt) * std::log(f) + amt * std::log(target)); else if (p.forceVoiced) { f = target; plan.forced[(size_t) j] = 1; }
        }
    }
    for (int j = 0; j < F; ++j) {
        double& f = plan.f0[(size_t) j]; const double semis = p.pitchSemis + (p.pitchLane.empty() ? 0.0f : p.pitchLane.at(plan.tSrc[(size_t) j]));
        if (f > 0) f *= std::exp2(juce::jlimit(-48.0, 48.0, semis) / 12.0);
        if (borrow && p.consonantsFromB) { const int iB = juce::jlimit(0, Fb - 1, (int) std::lround(plan.posB[(size_t) j])); if (!(bVoiced[(size_t) iB] > 0) && bE[(size_t) iB] > bAudible) f = 0; }
        f = f > 0 ? juce::jlimit(20.0, fs / 4.0, f) : 0.0;
    }
    // ---- voices
    struct VoiceDef { double cents = 0, drift = 0, driftHz = 0, driftPh = 0, delay = 0, formant = 0, gL = 1, gR = 1; int steps = 0; };
    const bool stereo = p.stereoOut();
    auto panGains = [](double pan, double level, VoiceDef& v) { const double ang = (juce::jlimit(-1.0, 1.0, pan) + 1.0) * kPiD / 4.0; v.gL = std::cos(ang) * level; v.gR = std::sin(ang) * level; };
    std::vector<VoiceDef> voices(1); if (stereo) panGains(0, 1.0, voices[0]);
    if (p.doubles >= 2) {
        const int extra = std::min(8, p.doubles) - 1, ranks = (extra + 1) / 2;
        for (int i = 0; i < extra; ++i) {
            VoiceDef v; const double side = i % 2 == 0 ? -1.0 : 1.0, rank = i / 2 + 1;
            v.cents = side * p.doubleCents * rank / ranks; v.drift = 0.25 * std::abs(p.doubleCents); v.driftHz = 0.17 + 0.07 * i; v.driftPh = 1.7 * i;
            v.delay = std::max(0.0, (double) p.doubleMs) * (0.3 + 0.7 * (i + 1) / extra) / 1000.0; v.formant = side * p.doubleFormant * rank / ranks;
            panGains(side * p.width * rank / ranks, 0.75, v); voices.push_back(v);
        }
    }
    for (const auto& h : p.harmony) if (h.steps != 0 && h.level > 0) { VoiceDef v; v.steps = juce::jlimit(-14, 14, h.steps); panGains(h.pan, juce::jlimit(0.0f, 1.0f, h.level), v); voices.push_back(v); }
    const int yLen = (int) ((F - 1) * fp / 1000.0 * fs) + 1;
    Rendered seg; seg.fs = fs; seg.L.assign((size_t) yLen, 0.0f); if (stereo) seg.R.assign((size_t) yLen, 0.0f);
    std::vector<double> S((size_t) bins), P((size_t) bins);
    if (info) info->formants.resize((size_t) F);
    const double hAlpha = 1.0 - std::exp(-fp / 30.0);
    for (size_t vi = 0; vi < voices.size(); ++vi) {
        const auto& V = voices[vi];
        EnvBuilder env(a, borrow ? b : nullptr, p, plan, bMax);
        StreamSynth syn(fs, fp, a.fftSize, (size_t) yLen); double hd = 0; bool hInit = false;
        for (int j = 0; j < F; ++j) {
            if ((j & 63) == 0) { if (prog.stop()) return false; prog.set((int) (85.0 * (vi + j / (double) F) / voices.size())); }
            const double f = plan.f0[(size_t) j]; double fv = 0;
            if (f > 0) {
                double cents = V.cents + (V.drift > 0 ? V.drift * std::sin(2 * kPiD * V.driftHz * j * fp / 1000.0 + V.driftPh) : 0.0);
                if (V.steps != 0) { const double m = hzMidi(f); const double target = scaleStep((float) m, V.steps, p.root, p.scale) - snapMidi((float) m, p.root, p.scale);
                                    if (!hInit) { hd = target; hInit = true; } else hd += (target - hd) * hAlpha; cents += 100.0 * hd; }
                fv = juce::jlimit(20.0, fs / 4.0, f * std::exp2(cents / 1200.0));
            }
            env.build(j, fv, V.formant, S.data(), P.data());
            if (vi == 0 && info) info->formants[(size_t) j] = formantsOf(S.data(), bins, fs, fv);
            syn.push(fv, S.data(), P.data());
        }
        const auto y = syn.finish(); const int d = (int) std::lround(V.delay * fs);
        for (int n = 0; n + d < yLen; ++n) { if (stereo) { seg.L[(size_t) (n + d)] += (float) (V.gL * y[(size_t) n]); seg.R[(size_t) (n + d)] += (float) (V.gR * y[(size_t) n]); } else seg.L[(size_t) (n + d)] += y[(size_t) n]; }
    }
    if (!borrow) applyTransients(seg, aAudio, a, p, plan, ef);
    prog.set(90);
    // ---- splice the region back into A, or the whole thing is the result
    if (!region) { out = std::move(seg); finalise(out, p, true); }
    else {
        const int chans = (stereo || aAudio.ch.size() >= 2) ? 2 : 1;
        const int rsS = (int) std::lround(src0 * fp / 1000.0 * fs), reS = std::min(aAudio.n, (int) std::lround((src1 - 1) * fp / 1000.0 * fs) + 1);
        const int xf = std::max(1, (int) (0.01 * fs)), segLen = (int) seg.L.size(), total = rsS + segLen + std::max(0, aAudio.n - reS);
        out.fs = fs; out.L.assign((size_t) total, 0.0f); if (chans == 2) out.R.assign((size_t) total, 0.0f);
        // the processed part gets GAIN (no normalise: the untouched audio around it keeps its level)
        RenderParams pg = p; Rendered sg = std::move(seg); finalise(sg, pg, false);
        for (int c = 0; c < chans; ++c) {
            auto& o = c == 0 ? out.L : out.R; const auto& s = (c == 1 && sg.stereo()) ? sg.R : sg.L; const float segGain = 1.0f;
            auto orig = [&](int i) { return chans == 2 && aAudio.ch.size() >= 2 ? chanSample(aAudio, c, i) : monoSample(aAudio, i); };
            for (int i = 0; i < rsS; ++i) o[(size_t) i] = orig(i);
            for (int i = 0; i < segLen; ++i) {
                float v = s[(size_t) i] * segGain;
                if (i < xf) { const float w = (float) i / xf; v = v * w + orig(rsS + i) * (1 - w); }
                if (i >= segLen - xf) { const float w = (float) (segLen - 1 - i) / xf; v = v * w + orig(reS - segLen + i) * (1 - w); }
                o[(size_t) (rsS + i)] = v;
            }
            for (int i = reS; i < aAudio.n; ++i) o[(size_t) (rsS + segLen + i - reS)] = orig(i);
        }
    }
    if (info) {
        info->fs = fs; info->framePeriod = fp;
        if (!region) { info->f0 = plan.f0; info->tSrc = plan.tSrc; }
        else {
            const int pre = src0, post = std::max(0, Fa - src1); std::vector<Formants> fm = std::move(info->formants);
            info->f0.clear(); info->tSrc.clear(); info->formants.clear();
            for (int i = 0; i < pre; ++i) { info->f0.push_back(ef[(size_t) i]); info->tSrc.push_back((float) (i * fp / 1000.0)); info->formants.push_back(a.formants[(size_t) i]); }
            for (int j = 0; j < F; ++j) { info->f0.push_back(plan.f0[(size_t) j]); info->tSrc.push_back(plan.tSrc[(size_t) j]); info->formants.push_back(fm[(size_t) j]); }
            for (int i = 0; i < post; ++i) { const int k = src1 + i; info->f0.push_back(ef[(size_t) k]); info->tSrc.push_back((float) (k * fp / 1000.0)); info->formants.push_back(a.formants[(size_t) k]); }
            info->regionOut0 = src0 * fp / 1000.0; info->regionOut1 = info->regionOut0 + (F - 1) * fp / 1000.0;
        }
    }
    prog.set(100); return true;
}

} // namespace

bool render(const Analysis& a, const Analysis* b, const RenderParams& p, std::vector<float>& out, Progress prog, RenderInfo* info) {
    Rendered r; const bool ok = render(a, AudioRef(), b, p, r, prog, info);
    if (!r.stereo()) out = std::move(r.L); else { out.resize(r.L.size()); for (size_t i = 0; i < r.L.size(); ++i) out[i] = 0.5f * (r.L[i] + r.R[i]); }
    return ok;
}

// ================================================================================================================ utilities
int workingRate(int fs) { return (fs >= 16000 && fs <= 96000) ? fs : 48000; }

// Kaiser-windowed sinc, tabulated (512 phases per tap, linear between), cut-off 0.45 x the lower rate, ~100 dB stop band
std::vector<float> resample(const std::vector<float>& x, double fromRate, double toRate) {
    if (x.empty() || fromRate <= 0 || toRate <= 0 || std::abs(fromRate - toRate) < 1e-6) return x;
    const double lower = std::min(fromRate, toRate), fc = 0.45 * lower / fromRate;           // cycles per input sample
    const double dOmega = 2 * kPiD * (0.5 - 0.45) * lower / fromRate, A = 100.0, beta = 0.1102 * (A - 8.7);
    const int half = std::max(8, (int) std::ceil((A - 8.0) / (2.285 * dOmega) * 0.5));
    constexpr int P = 512; const int tableLen = half * P + 2; std::vector<float> h((size_t) tableLen);
    auto bessel0 = [](double v) { double s = 1, t = 1; for (int k = 1; k < 40; ++k) { t *= (v / (2 * k)) * (v / (2 * k)); s += t; if (t < 1e-12 * s) break; } return s; };
    const double ib = 1.0 / bessel0(beta);
    for (int i = 0; i < tableLen; ++i) { const double tau = i / (double) P; const double r = tau / half; const double win = r >= 1 ? 0.0 : bessel0(beta * std::sqrt(1 - r * r)) * ib;
        const double arg = 2 * fc * tau; const double sinc = i == 0 ? 1.0 : std::sin(kPiD * arg) / (kPiD * arg); h[(size_t) i] = (float) (2 * fc * sinc * win); }
    const double step = fromRate / toRate; const int nOut = (int) std::floor((x.size() - 1) / step) + 1; std::vector<float> out((size_t) std::max(1, nOut));
    const int n = (int) x.size();
    for (int o = 0; o < nOut; ++o) {
        const double t = o * step; const int i0 = std::max(0, (int) std::ceil(t - half)), i1 = std::min(n - 1, (int) std::floor(t + half)); double s = 0;
        for (int i = i0; i <= i1; ++i) { const double d = std::abs(t - i) * P; const int k = (int) d; const double f = d - k; if (k + 1 >= tableLen) continue; s += x[(size_t) i] * (h[(size_t) k] + (h[(size_t) k + 1] - h[(size_t) k]) * f); }
        out[(size_t) o] = (float) s;
    }
    return out;
}
void normalise(std::vector<float>& x, float peak) { float m = 0; for (float v : x) m = std::max(m, std::abs(v)); if (m > 1e-9f) { const float g = peak / m; for (auto& v : x) v *= g; } }
bool finite(const std::vector<float>& x) { for (float v : x) if (!std::isfinite(v)) return false; return true; }
bool finite(const Analysis& a) {
    for (double v : a.f0) if (!std::isfinite(v)) return false;
    for (float v : a.f0Raw) if (!std::isfinite(v)) return false;
    for (double v : a.apCoded) if (!std::isfinite(v)) return false;
    for (auto& fm : a.formants) for (auto& f : fm) if (!std::isfinite(f.hz) || !std::isfinite(f.bwHz)) return false;
    return true;
}
const char* thirdPartyNotice() {
    return "Ghosty Spectrum VOICE uses WORLD v1.0.1 (https://github.com/mmorise/World).\n\n"
           "WORLD: High-quality speech analysis,\nmanipulation and synthesis system\ndeveloped by M. Morise\nhttp://www.kisc.meiji.ac.jp/~mmorise/world/english/\nCopyright (c) 2010  M. Morise\nAll rights reserved.\nRedistribution and use in source and binary forms, with or\nwithout modification, are permitted provided that the following\nconditions are met:\n- Redistributions of source code must retain the above copyright\nnotice, this list of conditions and the following disclaimer.\n- Redistributions in binary form must reproduce the above\ncopyright notice, this list of conditions and the following\ndisclaimer in the documentation and/or other materials provided\nwith the distribution.\n- Neither the name of the M. Morise nor the names of its\ncontributors may be used to endorse or promote products derived\nfrom this software without specific prior written permission.\nTHIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND\nCONTRIBUTORS \"AS IS\" AND ANY EXPRESS OR IMPLIED WARRANTIES,\nINCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF\nMERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE\nDISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS\nBE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,\nEXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED\nTO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,\nDATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON\nANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,\nOR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY\nOUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE\nPOSSIBILITY OF SUCH DAMAGE.\n";
}
} // namespace voice
