#include "LabEngine.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <numeric>
#include <cstring>

namespace lab {
namespace {
constexpr double TWO_PI = 6.283185307179586476925286766559;
inline float wrapPi(double p) { p = std::fmod(p + juce::MathConstants<double>::pi, TWO_PI); if (p < 0) p += TWO_PI; return (float) (p - juce::MathConstants<double>::pi); }
inline int binOf(double hz) { return (int) std::lround(hz / BIN_HZ); }
inline float brushWeight(float d, float hard) {
    if (d >= 1.0f) return 0.0f; hard = juce::jlimit(0.0f, 0.98f, hard);
    if (d <= hard) return 1.0f; return 0.5f + 0.5f * std::cos(juce::MathConstants<float>::pi * (d - hard) / (1.0f - hard));
}
void binRange(const Sel& s, int& k0, int& k1) {
    k0 = juce::jlimit(1, K - 2, (int) std::ceil(rowHz(s.r0) / BIN_HZ)); k1 = juce::jlimit(1, K - 2, (int) std::floor(rowHz(s.r1) / BIN_HZ));
    if (s.r0 <= 0.5f) k0 = 1;   // a box down to the bottom row takes the lowest bin too (a kick's energy reaches below 27.5 Hz)
    if (s.r1 >= R - 1.5f) k1 = K - 2;
}
// peaks + regions of influence (each bin -> the index of its peak), among bins [k0,k1] of src (src indexed from k0)
void regions(const Cell* src, int n, std::vector<int>& peakOf) {
    peakOf.assign((size_t) n, -1); std::vector<int> peaks;
    float mx = 0; for (int i = 0; i < n; ++i) mx = std::max(mx, src[i].mag);
    if (mx <= 0) return; const float floorMag = mx * 1.0e-5f;
    for (int i = 0; i < n; ++i) {
        const float m = src[i].mag; if (m <= floorMag) continue;
        const float l = i > 0 ? src[i - 1].mag : 0.0f, r = i + 1 < n ? src[i + 1].mag : 0.0f;
        if (m >= l && m > r) peaks.push_back(i);
    }
    if (peaks.empty()) return;
    // boundaries at the lowest bin between neighbouring peaks
    int start = 0;
    for (size_t p = 0; p < peaks.size(); ++p) {
        int end = n - 1;
        if (p + 1 < peaks.size()) { int lo = peaks[p]; for (int i = peaks[p]; i <= peaks[p + 1]; ++i) if (src[i].mag < src[lo].mag) lo = i; end = lo; }
        for (int i = start; i <= end; ++i) peakOf[(size_t) i] = peaks[p];
        start = end + 1;
    }
}
} // namespace

// ---------------------------------------------------------------- Canvas
float Canvas::level(int t, int r) const {
    if (t < 0 || t >= T || r < 0 || r >= R) return 0.0f;
    float v = A(t, r);
    const double fa = rowHz(r - 0.5) / BIN_HZ, fb = rowHz(r + 0.5) / BIN_HZ;
    const int ka = (int) std::ceil(fa), kb = (int) std::floor(fb);
    const Cell* c = col(t);
    if (kb >= ka) { for (int k = std::max(0, ka); k <= std::min(K - 1, kb); ++k) v = std::max(v, c[k].mag); }
    else { const double f = rowHz(r) / BIN_HZ; const int k = (int) f; const float a = (float) (f - k);
           if (k + 1 < K) v = std::max(v, c[k].mag * (1 - a) + c[k + 1].mag * a); }
    return v;
}
bool Canvas::hasStft() const { for (auto& c : cells) if (c.mag > 0) return true; return false; }
bool Canvas::isBlank() const { for (float a : amp) if (a > 0) return false; return !hasStft(); }

// ---------------------------------------------------------------- STFT
Stft::Stft() : win((size_t) N), buf((size_t) N * 2) { for (int n = 0; n < N; ++n) win[(size_t) n] = (float) (0.5 - 0.5 * std::cos(TWO_PI * n / N)); }
void Stft::forward(const float* frame, std::complex<float>* out) {
    for (int n = 0; n < N; ++n) buf[(size_t) n] = frame[n] * win[(size_t) n];
    std::fill(buf.begin() + N, buf.end(), 0.0f);
    fft.performRealOnlyForwardTransform(buf.data(), true);
    for (int k = 0; k < K; ++k) out[k] = { buf[(size_t) (2 * k)], buf[(size_t) (2 * k + 1)] };
}
void Stft::inverse(const std::complex<float>* in, float* frameOut) {
    for (int k = 0; k < K; ++k) { buf[(size_t) (2 * k)] = in[k].real(); buf[(size_t) (2 * k + 1)] = in[k].imag(); }
    for (int k = K; k < N; ++k) { const auto c = std::conj(in[N - k]); buf[(size_t) (2 * k)] = c.real(); buf[(size_t) (2 * k + 1)] = c.imag(); }
    fft.performRealOnlyInverseTransform(buf.data());
    constexpr float norm = 1.0f / 1.5f;   // sum of Hann^2 at 75 % overlap
    for (int n = 0; n < N; ++n) frameOut[n] = buf[(size_t) n] * win[(size_t) n] * norm;
}

std::vector<std::complex<float>> stftFrames(const std::vector<float>& x, int& T) {
    const int L = (int) x.size(); T = std::max(1, (L - 1 + OFFSET) / HOP + 1);
    std::vector<std::complex<float>> X((size_t) T * K); Stft s; std::vector<float> fr((size_t) N);
    for (int t = 0; t < T; ++t) {
        const int st = t * HOP - OFFSET;
        for (int n = 0; n < N; ++n) { const int i = st + n; fr[(size_t) n] = (i >= 0 && i < L) ? x[(size_t) i] : 0.0f; }
        s.forward(fr.data(), X.data() + (size_t) t * K);
    }
    return X;
}
std::vector<float> istftFrames(const std::vector<std::complex<float>>& X, int T, int length) {
    std::vector<float> y((size_t) length + N, 0.0f); Stft s; std::vector<float> fr((size_t) N);
    for (int t = 0; t < T; ++t) {
        s.inverse(X.data() + (size_t) t * K, fr.data()); const int st = t * HOP - OFFSET;
        for (int n = 0; n < N; ++n) { const int i = st + n; if (i >= 0 && i < (int) y.size()) y[(size_t) i] += fr[(size_t) n]; }
    }
    y.resize((size_t) length); return y;
}

void analyse(const std::vector<float>& x, Canvas& c) {
    int T = 0; auto X = stftFrames(x, T);
    c.resize(T); c.srcLen = (int) x.size();
    for (int t = 0; t < T; ++t) {
        Cell* cc = c.col(t); const auto* xr = X.data() + (size_t) t * K; const auto* xp = t > 0 ? X.data() + (size_t) (t - 1) * K : nullptr;
        for (int k = 0; k < K; ++k) {
            const float ph = std::arg(xr[k]);
            cc[k].mag = std::abs(xr[k]) * (4.0f / N); cc[k].phase = ph; cc[k].kind = Orig; cc[k].lock = 0;
            double f = k * BIN_HZ;
            if (xp) { const double expected = TWO_PI * k * HOP / N; const double d = wrapPi(ph - std::arg(xp[k]) - expected); f = (k + d * N / (TWO_PI * HOP)) * BIN_HZ; }
            cc[k].freq = (float) std::max(0.0, f);
        }
    }
}

// ---------------------------------------------------------------- render
namespace {
inline uint32_t mix32(uint32_t h) { h ^= h >> 16; h *= 0x7feb352dU; h ^= h >> 15; h *= 0x846ca68bU; h ^= h >> 16; return h; }
// the side's pan for a frequency: smooth value noise over log frequency (a new value every third of an octave), -1..1
float sidePan(double hz, uint32_t seed) {
    const double x = std::log2(std::max(20.0, hz) / 20.0) * 3.0; const int i = (int) std::floor(x); const double f = x - i;
    auto v = [seed](int k) { return (float) (mix32(seed * 0x9E3779B1u + (uint32_t) (k + 1000) * 0x85EBCA77u) & 0xFFFF) / 32767.5f - 1.0f; };
    const float w = (float) (0.5 - 0.5 * std::cos(juce::MathConstants<double>::pi * f));
    return v(i) * (1 - w) + v(i + 1) * w;
}
// nothing below ~120 Hz goes to the side (the low end stays mono), full width from ~350 Hz
float sideGate(double hz) { const double x = (hz - 120.0) / 230.0; if (x <= 0) return 0.0f; if (x >= 1) return 1.0f; return (float) (x * x * (3 - 2 * x)); }
} // namespace

std::vector<uint8_t> onsetFrames(const Canvas& c, bool forEdit) {
    std::vector<uint8_t> f((size_t) std::max(0, c.T), 0); if (c.T < 3) return f;
    std::vector<double> E((size_t) c.T, 0.0); double mx = 0;
    for (int t = 0; t < c.T; ++t) { const Cell* cc = c.col(t); double e = 0; for (int k = 1; k < K; ++k) e += (double) cc[k].mag * cc[k].mag; E[(size_t) t] = e; mx = std::max(mx, e); }
    if (mx <= 0) return f;
    for (int t = 1; t < c.T; ++t) {
        double prev = 0; int n = 0; for (int d = 1; d <= 4; ++d) if (t - d >= 0) { prev += E[(size_t) (t - d)]; ++n; } prev /= std::max(1, n);
        // a jump of more than ~8 dB over the last 4 frames: the 4 frames whose windows hold the onset take their own phases
        // edits: from the frame before (it sees the attack at its window edge) to 4 after; the render's own fallback stays strict
        if (forEdit) { if (E[(size_t) t] > 1.0e-6 * mx && E[(size_t) t] > 4.0 * prev) { for (int u = std::max(0, t - 1); u <= std::min(c.T - 1, t + 4); ++u) f[(size_t) u] = 1; t += 4; } }
        else if (E[(size_t) t] > 1.0e-6 * mx && E[(size_t) t] > 6.0 * prev) { for (int u = t; u <= std::min(c.T - 1, t + 3); ++u) f[(size_t) u] = 1; t += 3; }
    }
    return f;
}

Stereo renderStereo(const Canvas& c, Quality q, const RenderOpts& o) {
    Stereo out; const int T = c.T; if (T <= 0) return out;
    const int outLen = c.lengthSamples(); const int bufLen = T * HOP + N;
    std::vector<float>& y = out.m; y.assign((size_t) bufLen, 0.0f);
    std::vector<float> ysDummy; std::vector<float>& ys = o.side ? out.s : ysDummy; if (o.side) ys.assign((size_t) bufLen, 0.0f);
    const bool hq = q == Quality::HQ;
    const uint32_t sideSeed = o.seed * 2246822519u + 77;
    bool anyNoise = false; for (size_t i = 0; i < c.noise.size(); ++i) if (c.noise[i] > 0 && c.amp[i] > 0) { anyNoise = true; break; }
    // ---- STFT plane (+ noise share of the paint plane)
    if (c.hasStft() || anyNoise) {
        Stft s; std::vector<std::complex<float>> X((size_t) K), XS((size_t) K); std::vector<float> fr((size_t) N);
        std::vector<double> ph((size_t) K, 0.0), nph((size_t) K, 0.0); std::vector<uint8_t> prevMoved((size_t) K, 0);
        std::vector<float> sg((size_t) K, 0.0f); for (int k = 0; k < K; ++k) sg[(size_t) k] = sideGate(k * BIN_HZ) * sidePan(k * BIN_HZ, sideSeed);
        // attack frames: the ones an edit moved as attacks (marked), or - for canvases moved without that - detected now
        std::vector<uint8_t> onset((size_t) T, 0);
        if (o.keepTransients) {
            bool marked = false; for (int t = 0; t < T; ++t) { const Cell* cc = c.col(t); for (int k = 1; k < K - 1; ++k) if (cc[k].spare == 1 && cc[k].kind == Moved) { onset[(size_t) t] = 1; marked = true; break; } }
            if (!marked) onset = onsetFrames(c, false);
        }
        juce::Random rnd((juce::int64) o.seed * 7919 + 17), rnd2((juce::int64) o.seed * 15485863 + 29);
        const float S = N / 4.0f;
        for (int t = 0; t < T; ++t) {
            if ((t & 63) == 0 && cancelled()) break;
            const Cell* cc = c.col(t); const bool reset = onset[(size_t) t] != 0;
            // an onset frame: every moved bin gets one coherent linear phase (the attack's own position in the frame,
            // read from the phase slope inside the moved regions), so the attack stays one sharp event instead of
            // a smear of narrow bands each with its own phase
            double slope = 0, theta = 0; bool linear = false;
            if (reset) {
                double sx = 0, sy = 0; float best = 0; int kb = -1;
                for (int k = 1; k < K - 1; ++k) {
                    const Cell& e = cc[k]; const Cell& f = cc[k + 1];
                    if (e.kind == Moved && e.mag > 0 && e.mag > best) { best = e.mag; kb = k; }
                    if (e.kind != Moved || f.kind != Moved || e.mag <= 0 || f.mag <= 0 || k - e.lock != k + 1 - f.lock) continue;
                    const double d = (double) f.phase - e.phase, w = std::min(e.mag, f.mag); sx += w * std::cos(d); sy += w * std::sin(d);
                }
                if (kb >= 0 && sx * sx + sy * sy > 0) { slope = std::atan2(sy, sx); theta = cc[kb].phase - slope * kb; linear = true; }
            }
            for (int k = 0; k < K; ++k) {
                const Cell& e = cc[k]; X[(size_t) k] = {};
                if (e.kind == Orig) { nph[(size_t) k] = e.phase; if (e.mag > 0) X[(size_t) k] = std::polar(e.mag * S, e.phase); continue; }
                const int p = k - e.lock;
                const bool ownPeak = e.lock == 0 || p < 0 || p >= K || cc[p].kind != Moved || cc[p].lock != 0 || cc[p].mag <= 0;
                if (ownPeak) nph[(size_t) k] = (prevMoved[(size_t) k] && !reset) ? ph[(size_t) k] + TWO_PI * e.freq * HOP / SR : (double) e.phase;
            }
            for (int k = 0; k < K; ++k) {
                const Cell& e = cc[k]; if (e.kind != Moved) continue;
                const int p = k - e.lock;
                const bool ownPeak = e.lock == 0 || p < 0 || p >= K || cc[p].kind != Moved || cc[p].lock != 0 || cc[p].mag <= 0;
                if (linear) nph[(size_t) k] = theta + slope * k;
                else if (!ownPeak) nph[(size_t) k] = nph[(size_t) p] + (double) (e.phase - cc[p].phase);
                if (e.mag > 0) X[(size_t) k] = std::polar(e.mag * S, (float) std::fmod(nph[(size_t) k], TWO_PI));
            }
            for (int k = 0; k < K; ++k) { ph[(size_t) k] = std::fmod(nph[(size_t) k], TWO_PI); prevMoved[(size_t) k] = (uint8_t) (cc[k].kind == Moved && cc[k].mag > 0); }
            if (o.side) for (int k = 0; k < K; ++k) XS[(size_t) k] = X[(size_t) k] * sg[(size_t) k];
            if (anyNoise) for (int r = 0; r < R; ++r) {
                const float a = c.A(t, r) * c.noise[(size_t) t * R + (size_t) r]; if (a <= 1.0e-5f) continue;
                const double fa = rowHz(r - 0.5) / BIN_HZ, fb = rowHz(r + 0.5) / BIN_HZ; if (fa >= K - 2) continue;
                const int ka = std::max(1, (int) std::ceil(fa)), kb = std::min(K - 2, (int) std::floor(fb));
                const float gs = o.side ? 0.8f * sideGate(rowHz(r)) : 0.0f;
                if (kb >= ka) { const float m = a * S / std::sqrt((float) (kb - ka + 1));
                    for (int k = ka; k <= kb; ++k) { X[(size_t) k] += std::polar(m, (float) (rnd.nextDouble() * TWO_PI)); if (gs > 0) XS[(size_t) k] += std::polar(m * gs, (float) (rnd2.nextDouble() * TWO_PI)); } }
                else { const int k = juce::jlimit(1, K - 2, (int) std::lround(rowHz(r) / BIN_HZ)); const float m = a * S * std::sqrt((float) (fb - fa));
                    X[(size_t) k] += std::polar(m, (float) (rnd.nextDouble() * TWO_PI)); if (gs > 0) XS[(size_t) k] += std::polar(m * gs, (float) (rnd2.nextDouble() * TWO_PI)); }
            }
            const int st = t * HOP - OFFSET;
            s.inverse(X.data(), fr.data());
            for (int n = 0; n < N; ++n) { const int i = st + n; if (i >= 0 && i < bufLen) y[(size_t) i] += fr[(size_t) n]; }
            if (o.side) { s.inverse(XS.data(), fr.data()); for (int n = 0; n < N; ++n) { const int i = st + n; if (i >= 0 && i < bufLen) ys[(size_t) i] += fr[(size_t) n]; } }
        }
    }
    // ---- additive oscillators for the paint plane's tonal share
    const float thr = hq ? 3.0e-5f : 6.0e-4f;
    juce::Random prnd((juce::int64) o.seed * 104729 + 3);
    for (int r = 0; r < R; ++r) {
        if ((r & 15) == 0 && cancelled()) break;
        double phase = prnd.nextDouble() * TWO_PI;
        const double rowF = rowHz(r); if (rowF > SR * 0.47) continue;
        bool any = false; for (int t = 0; t < T && !any; ++t) if (c.A(t, r) > thr) any = true;
        if (!any) continue;
        const float sp = o.side ? sideGate(rowF) * sidePan(rowF, sideSeed) : 0.0f;
        auto ampAt = [&](int t) { if (t < 0 || t >= T) return 0.0f; const size_t i = (size_t) t * R + (size_t) r; return c.amp[i] * (1.0f - c.noise[i]); };
        auto fqAt = [&](int t) { if (t < 0 || t >= T) return rowF; const float f = c.fq[(size_t) t * R + (size_t) r]; return f > 0 ? (double) f : rowF; };
        for (int t = -1; t < T; ++t) {
            const float a0 = ampAt(t), a1 = ampAt(t + 1);
            double f0 = fqAt(t), f1 = fqAt(t + 1);
            if (a0 <= thr) f0 = f1;
            if (a1 <= thr) f1 = f0;
            const int n0 = (int) std::lround(frameCentre(t)), n1 = (int) std::lround(frameCentre(t + 1)); const int len = n1 - n0;
            if (a0 <= thr && a1 <= thr) { phase = std::fmod(phase + TWO_PI * 0.5 * (f0 + f1) * len / SR, TWO_PI); continue; }
            if (f0 >= SR * 0.48 || f1 >= SR * 0.48) { phase = std::fmod(phase + TWO_PI * 0.5 * (f0 + f1) * len / SR, TWO_PI); continue; }
            if (hq) {
                for (int i = 0; i < len; ++i) {
                    const double u = (double) i / len; const int n = n0 + i;
                    const double f = f0 + (f1 - f0) * u;
                    if (n >= 0 && n < bufLen) { const float v = (float) ((a0 + (a1 - a0) * u) * std::sin(phase)); y[(size_t) n] += v; if (sp != 0) ys[(size_t) n] += v * sp; }
                    phase += TWO_PI * f / SR;
                }
                phase = std::fmod(phase, TWO_PI);
            } else {
                const double f = 0.5 * (f0 + f1); const std::complex<double> w = std::polar(1.0, TWO_PI * f / SR);
                std::complex<double> z = std::polar(1.0, phase); const float da = (a1 - a0) / (float) len; float a = a0;
                for (int i = 0; i < len; ++i) { const int n = n0 + i; if (n >= 0 && n < bufLen) { const float v = a * (float) z.imag(); y[(size_t) n] += v; if (sp != 0) ys[(size_t) n] += v * sp; } z *= w; a += da; }
                phase = std::fmod(phase + TWO_PI * f * len / SR, TWO_PI);
            }
        }
    }
    y.resize((size_t) outLen); for (auto& v : y) if (!std::isfinite(v)) v = 0.0f;
    if (o.side) { ys.resize((size_t) outLen); for (auto& v : ys) if (!std::isfinite(v)) v = 0.0f; }
    return out;
}
std::vector<float> render(const Canvas& c, Quality q, uint32_t noiseSeed) { RenderOpts o; o.seed = noiseSeed; o.side = false; return std::move(renderStereo(c, q, o).m); }

// ---------------------------------------------------------------- editing
template <typename F> static void footprint(const Canvas& c, float t, float r, const Brush& b, F&& fn) {
    const float rr = std::max(0.5f, b.size), rt = std::max(0.5f, b.size * b.timeScale);
    const int t0 = std::max(0, (int) std::floor(t - rt)), t1 = std::min(c.T - 1, (int) std::ceil(t + rt));
    const int r0 = std::max(0, (int) std::floor(r - rr)), r1 = std::min(R - 1, (int) std::ceil(r + rr));
    for (int ti = t0; ti <= t1; ++ti) for (int ri = r0; ri <= r1; ++ri) {
        const float dt = (ti - t) / rt, dr = (ri - r) / rr; const float w = brushWeight(std::sqrt(dt * dt + dr * dr), b.hardness);
        if (w > 0) fn(ti, ri, w);
    }
}
void paintAt(Canvas& c, float t, float r, const Brush& b, double exactHz) {
    if (exactHz > 0) {   // a pure line: one row at the exact pitch, brush width in time only
        const int r0 = (int) std::lround(hzRow(exactHz)); if (r0 < 0 || r0 >= R) return;
        const float rt = std::max(0.5f, b.size * b.timeScale);
        for (int ti = std::max(0, (int) std::floor(t - rt)); ti <= std::min(c.T - 1, (int) std::ceil(t + rt)); ++ti) {
            const float w = brushWeight(std::abs(ti - t) / rt, b.hardness); if (w <= 0) continue;
            const size_t i = (size_t) ti * R + (size_t) r0; const float v = b.level * w;
            if (v >= c.amp[i]) { c.amp[i] = v; c.fq[i] = (float) exactHz; c.noise[i] = b.noise; }
        }
        return;
    }
    // constant power per stroke: the rows under the brush are separate partials, so their powers add
    const float rr = std::max(0.5f, b.size); float rows = 0;
    for (int d = -(int) std::ceil(rr); d <= (int) std::ceil(rr); ++d) rows += brushWeight(std::abs((float) d) / rr, b.hardness);
    const float norm = std::min(1.0f, 0.7f / std::sqrt(std::max(1.0f, rows)));
    footprint(c, t, r, b, [&](int ti, int ri, float w) {
        const size_t i = (size_t) ti * R + (size_t) ri; const float v = b.level * w * norm;
        if (v >= c.amp[i]) { c.amp[i] = v; c.fq[i] = 0; c.noise[i] = b.noise; }
    });
}
void eraseAt(Canvas& c, float t, float r, const Brush& b) {
    footprint(c, t, r, b, [&](int ti, int ri, float w) { c.amp[(size_t) ti * R + (size_t) ri] *= 1.0f - w; });
    const float rr = std::max(0.5f, b.size), rt = std::max(0.5f, b.size * b.timeScale);
    const int k0 = std::max(1, (int) std::floor(rowHz(r - rr) / BIN_HZ)), k1 = std::min(K - 1, (int) std::ceil(rowHz(r + rr) / BIN_HZ));
    const int ta = std::max(0, (int) std::floor(t - rt)), tb = std::min(c.T - 1, (int) std::ceil(t + rt)); c.touch(ta, tb);
    for (int ti = ta; ti <= tb; ++ti) {
        Cell* cc = c.col(ti);
        for (int k = k0; k <= k1; ++k) { const float dt = (ti - t) / rt, dr = (float) (hzRow(k * BIN_HZ) - r) / rr; const float w = brushWeight(std::sqrt(dt * dt + dr * dr), b.hardness); cc[k].mag *= 1.0f - w; }
    }
}
void smearAt(Canvas& c, float t, float r, const Brush& b) {
    const float rr = std::max(0.5f, b.size), rt = std::max(0.5f, b.size * b.timeScale); const int span = std::max(2, (int) rt);
    const int t0 = std::max(0, (int) std::floor(t - rt)), t1 = std::min(c.T - 1, (int) std::ceil(t + rt));
    if (t1 < t0) return;
    const int ta = std::max(0, t0 - span), tb = std::min(c.T - 1, t1 + span); c.touch(t0, t1);
    std::vector<float> A0(c.amp.begin() + (ptrdiff_t) ta * R, c.amp.begin() + (ptrdiff_t) (tb + 1) * R);
    auto avgA = [&](int ti, int ri) { float s = 0; int n = 0; for (int d = -span; d <= span; ++d) { const int u = ti + d; if (u < ta || u > tb) continue; s += A0[(size_t) (u - ta) * R + (size_t) ri]; ++n; } return n ? s / n : 0.0f; };
    footprint(c, t, r, b, [&](int ti, int ri, float w) { float& v = c.amp[(size_t) ti * R + (size_t) ri]; v = v * (1 - w) + avgA(ti, ri) * w; });
    const int k0 = std::max(1, (int) std::floor(rowHz(r - rr) / BIN_HZ)), k1 = std::min(K - 1, (int) std::ceil(rowHz(r + rr) / BIN_HZ));
    if (k1 < k0) return;
    std::vector<float> M((size_t) (tb - ta + 1) * (size_t) (k1 - k0 + 1));
    for (int u = ta; u <= tb; ++u) for (int k = k0; k <= k1; ++k) M[(size_t) (u - ta) * (size_t) (k1 - k0 + 1) + (size_t) (k - k0)] = c.col(u)[k].mag;
    for (int ti = t0; ti <= t1; ++ti) for (int k = k0; k <= k1; ++k) {
        const float dt = (ti - t) / rt, dr = (float) (hzRow(k * BIN_HZ) - r) / rr; const float w = brushWeight(std::sqrt(dt * dt + dr * dr), b.hardness); if (w <= 0) continue;
        float s = 0; int n = 0; for (int d = -span; d <= span; ++d) { const int u = ti + d; if (u < ta || u > tb) continue; s += M[(size_t) (u - ta) * (size_t) (k1 - k0 + 1) + (size_t) (k - k0)]; ++n; }
        Cell& e = c.col(ti)[k]; e.mag = e.mag * (1 - w) + (n ? s / n : 0) * w;
    }
}
void lockColumn(Cell* col, int k0, int k1) {
    const int n = k1 - k0 + 1; if (n <= 0) return; std::vector<int> pk; regions(col + k0, n, pk);
    for (int i = 0; i < n; ++i) { Cell& e = col[k0 + i]; e.kind = Moved; const int d = pk[(size_t) i] < 0 ? 0 : i - pk[(size_t) i]; e.lock = (int8_t) (std::abs(d) > 127 ? 0 : d); }
}
void peakRegions(const Cell* src, int n, std::vector<int>& peakOf) { regions(src, n, peakOf); }
int placeRegions(const Cell* src, int k0, int n, const std::function<bool(int, double, double&, float&)>& where, Cell* dst) {
    std::vector<int> pk; regions(src, n, pk); int dropped = 0;
    for (int i = 0; i < n; ) {
        const int p = pk[(size_t) i]; if (p < 0) { ++i; continue; }
        int j = i; while (j < n && pk[(size_t) j] == p) ++j;       // region [i, j)
        const double fp = src[p].freq > 0 ? src[p].freq : (k0 + p) * BIN_HZ;
        double f2 = fp; float gain = 1.0f;
        if (where(k0 + p, fp, f2, gain)) {
            if (f2 >= SR * 0.48) { if (src[p].mag > 1.0e-4f) ++dropped; }
            else if (f2 > 10.0) {
                // the lobe moves by the whole bins closest to the frequency change, so its shape stays within half a bin of the new frequency
                const double ratio = f2 / std::max(1.0, fp); const int delta = (int) std::lround((f2 - fp) / BIN_HZ);
                for (int q = i; q < j; ++q) {
                    const int d = k0 + q + delta; if (d < 1 || d > K - 2) continue;
                    const float m = src[q].mag * gain; if (m <= dst[d].mag) continue;
                    const int lk = q - p;
                    dst[d] = Cell { m, (float) (src[q].freq * ratio), src[q].phase, Moved, (int8_t) (std::abs(lk) > 127 ? 0 : lk), 0 };
                }
            }
        }
        i = j;
    }
    return dropped;
}
int moveFrame(Cell* col, int k0, int k1, const std::function<double(double)>& map, float gain) {
    const int n = k1 - k0 + 1; if (n <= 0) return 0;
    std::vector<Cell> src(col + k0, col + k1 + 1);
    for (int k = k0; k <= k1; ++k) col[k] = Cell();
    return placeRegions(src.data(), k0, n, [&](int, double fp, double& f2, float& g) { f2 = map(fp); g = gain; return true; }, col);
}
// an attack frame moved as a whole: magnitudes are re-drawn along the map (no gaps, no ripple) and every bin gets one
// linear phase that puts the attack where it was in the frame, so it stays a single sharp event
constexpr uint16_t kAttack = 1;   // Cell::spare: this frame was moved as an attack (its phases are already coherent)
static int splatFrame(Cell* col, int k0, int k1, const std::function<double(double)>& map, const Cell* prev = nullptr) {
    const int n = k1 - k0 + 1; if (n <= 1) return 0;
    std::vector<Cell> src(col + k0, col + k1 + 1);
    double sx = 0, sy = 0; int qb = 0;
    for (int q = 0; q < n; ++q) { if (src[(size_t) q].mag > src[(size_t) qb].mag) qb = q;
        if (q + 1 < n) { const double d = (double) src[(size_t) q + 1].phase - src[(size_t) q].phase, w = std::min(src[(size_t) q].mag, src[(size_t) q + 1].mag); sx += w * std::cos(d); sy += w * std::sin(d); } }
    if (src[(size_t) qb].mag <= 0) return 0;
    const double slope = sx * sx + sy * sy > 0 ? std::atan2(sy, sx) : 0.0;
    for (int k = k0; k <= k1; ++k) col[k] = Cell();
    double eSrc = 0; for (const auto& e : src) eSrc += (double) e.mag * e.mag;
    if (prev) { double ePrev = 0; for (int k = 1; k < K - 1; ++k) if (prev[k].spare == kAttack) ePrev += (double) prev[k].mag * prev[k].mag; if (ePrev < 0.25 * eSrc) prev = nullptr; }   // continue only from a frame that already holds the attack
    std::vector<uint8_t> wrote((size_t) K, 0);
    int dropped = 0; double px = -1; float pm = 0;
    for (int q = 0; q < n; ++q) {
        const double x = map((k0 + q) * BIN_HZ) / BIN_HZ; const float m = src[(size_t) q].mag;
        if (x >= K - 1) { if (m > 1.0e-4f) dropped = 1; px = -1; continue; }
        const double a = px >= 0 ? std::min(px, x) : x, b = px >= 0 ? std::max(px, x) : x;
        // phase: the nearest source bin's own phase, corrected by the slope for the distance it moved - the attack keeps
        // its shape (a kick stays a kick, not a click) and its place in the frame
        // later frames of the same attack advance each bin by its own centre frequency, so the frames add up instead of cancelling
        // the mapped instantaneous frequency of the nearest source bin; later frames of the same attack advance by it (a phase
        // vocoder inside the attack), so the frames add up and the bins of one partial stay together
        auto freqFor = [&](double srcPos) { const int qs = juce::jlimit(0, n - 1, (int) std::lround(srcPos)); const double f = src[(size_t) qs].freq > 0 ? src[(size_t) qs].freq : (k0 + qs) * BIN_HZ; return std::max(0.0, map(f)); };
        auto phaseFor = [&](int d, double srcPos) { if (prev && prev[d].mag > 0 && prev[d].spare == kAttack) return wrapPi(prev[d].phase + TWO_PI * freqFor(srcPos) * HOP / SR);
            const int qs = juce::jlimit(0, n - 1, (int) std::lround(srcPos)); return wrapPi(src[(size_t) qs].phase + slope * (d - (k0 + qs))); };
        for (int d = std::max(1, (int) std::ceil(a - 1e-9)); d <= std::min(K - 2, (int) std::floor(b + 1e-9)); ++d) {
            const double u = (px >= 0 && b > a) ? (d - px) / (x - px) : 1.0;
            const float mm = (px >= 0 && b > a) ? pm + (m - pm) * (float) u : m;
            if (mm > col[d].mag) { const double sp = px >= 0 ? (q - 1) + u : q; col[d] = Cell { mm, (float) freqFor(sp), phaseFor(d, sp), Moved, 0, kAttack }; wrote[(size_t) d] = 1; }
        }
        if (px < 0 || b - a < 1.0) { const int d = (int) std::lround(x); if (d >= 1 && d <= K - 2 && m > col[d].mag) { col[d] = Cell { m, (float) freqFor(q), phaseFor(d, q), Moved, 0, kAttack }; wrote[(size_t) d] = 1; } }
        px = x; pm = m;
    }
    // the attack keeps its own level. Stretching a spectrum by r squeezes the attack in time by r, so equal energy would make it
    // sqrt(r) louder at its peak: the target energy is the source's divided by the local stretch (taken at the energy centroid)
    double eDst = 0; for (int d = 1; d < K - 1; ++d) if (wrote[(size_t) d]) eDst += (double) col[d].mag * col[d].mag;
    double fc = 0; for (int q = 0; q < n; ++q) fc += (double) src[(size_t) q].mag * src[(size_t) q].mag * (k0 + q) * BIN_HZ;
    fc = eSrc > 0 ? std::max(BIN_HZ, fc / eSrc) : BIN_HZ;
    const double stretch = juce::jlimit(0.25, 4.0, (map(fc * 1.05) - map(fc * 0.95)) / (0.1 * fc));
    if (eDst > 0 && eSrc > 0) { const float g = (float) std::sqrt(eSrc / (eDst * stretch)); for (int d = 1; d < K - 1; ++d) if (wrote[(size_t) d]) col[d].mag *= g; }
    return dropped;
}
static void selFrames(const Canvas& c, const Sel& s, int& t0, int& t1) { t0 = juce::jlimit(0, std::max(0, c.T - 1), s.t0); t1 = juce::jlimit(t0, c.T, s.t1); }
void freeze(Canvas& c, const Sel& s) {
    int t0, t1; selFrames(c, s, t0, t1); if (t1 <= t0) return; int k0, k1; binRange(s, k0, k1);
    std::vector<Cell> hold(c.col(t0), c.col(t0) + K); lockColumn(hold.data(), k0, k1);
    const int r0 = std::max(0, (int) std::ceil(s.r0)), r1 = std::min(R - 1, (int) std::floor(s.r1));
    c.touch(t0, t1 - 1);
    for (int t = t0; t < t1; ++t) {
        for (int k = k0; k <= k1; ++k) c.col(t)[k] = hold[(size_t) k];
        for (int r = r0; r <= r1; ++r) { const size_t a = (size_t) t * R + (size_t) r, b = (size_t) t0 * R + (size_t) r; c.amp[a] = c.amp[b]; c.fq[a] = c.fq[b]; c.noise[a] = c.noise[b]; }
    }
}
static int movePaint(Canvas& c, int t, int r0, int r1, const std::function<double(double)>& map) {
    std::vector<float> A, F, Nz; int dropped = 0;
    for (int r = r0; r <= r1; ++r) { const size_t i = (size_t) t * R + (size_t) r; A.push_back(c.amp[i]); F.push_back(c.fq[i]); Nz.push_back(c.noise[i]); c.amp[i] = 0; c.fq[i] = 0; c.noise[i] = 0; }
    for (int r = r0; r <= r1; ++r) {
        const size_t j = (size_t) (r - r0); if (A[j] <= 0) continue;
        const double f = F[j] > 0 ? F[j] : rowHz(r), f2 = map(f);
        if (f2 >= SR * 0.47) { if (A[j] > 1.0e-4f) ++dropped; continue; }
        if (f2 <= 10) continue;
        const int r2 = (int) std::lround(hzRow(f2)); if (r2 < 0 || r2 >= R) { ++dropped; continue; }
        const size_t i = (size_t) t * R + (size_t) r2; if (A[j] > c.amp[i]) { c.amp[i] = A[j]; c.fq[i] = (float) f2; c.noise[i] = Nz[j]; }
    }
    return dropped;
}
int shift(Canvas& c, const Sel& s, double semis, bool keepTransients) {
    int t0, t1; selFrames(c, s, t0, t1); int k0, k1; binRange(s, k0, k1); const double ratio = std::exp2(semis / 12.0);
    const auto onset = keepTransients ? onsetFrames(c, true) : std::vector<uint8_t>((size_t) c.T, 0);
    const int r0 = std::max(0, (int) std::ceil(s.r0)), r1 = std::min(R - 1, (int) std::floor(s.r1));
    auto map = [ratio](double f) { return f * ratio; };
    if (t1 > t0) c.touch(t0, t1 - 1);
    int lostFrames = 0, worst = 0;
    for (int t = t0; t < t1; ++t) { const int d = (onset[(size_t) t] ? splatFrame(c.col(t), k0, k1, map, t > t0 && onset[(size_t) t - 1] ? c.col(t - 1) : nullptr) : moveFrame(c.col(t), k0, k1, map)) + movePaint(c, t, r0, r1, map); if (d > 0) { ++lostFrames; worst = std::max(worst, d); } }
    return lostFrames > 0 ? std::max(1, worst) : 0;   // partials per frame that went above the top
}
void stretchHarmonics(Canvas& c, const Sel& s, double factor, bool keepTransients) {
    int t0, t1; selFrames(c, s, t0, t1); int k0, k1; binRange(s, k0, k1);
    const auto onset = keepTransients ? onsetFrames(c, true) : std::vector<uint8_t>((size_t) c.T, 0);
    const int r0 = std::max(0, (int) std::ceil(s.r0)), r1 = std::min(R - 1, (int) std::floor(s.r1));
    if (t1 > t0) c.touch(t0, t1 - 1);
    for (int t = t0; t < t1; ++t) {
        // f0: the lowest strong partial in the selection (STFT or paint)
        Cell* cc = c.col(t); float mx = 0; for (int k = k0; k <= k1; ++k) mx = std::max(mx, cc[k].mag);
        double f0 = 0; for (int k = k0 + 1; k < k1; ++k) if (cc[k].mag > 0.15f * mx && cc[k].mag >= cc[k - 1].mag && cc[k].mag >= cc[k + 1].mag) { f0 = cc[k].freq > 0 ? cc[k].freq : k * BIN_HZ; break; }
        float pm = 0; for (int r = r0; r <= r1; ++r) pm = std::max(pm, c.A(t, r));
        double pf0 = 0; for (int r = r0; r <= r1; ++r) if (c.A(t, r) > 0.15f * pm && pm > 0) { const float f = c.fq[(size_t) t * R + (size_t) r]; pf0 = f > 0 ? f : rowHz(r); break; }
        if (f0 > 0) { auto mp = [f0, factor](double f) { return f0 * std::pow(std::max(1.0, f) / f0, factor); }; if (onset[(size_t) t]) splatFrame(cc, k0, k1, mp, t > t0 && onset[(size_t) t - 1] ? c.col(t - 1) : nullptr); else moveFrame(cc, k0, k1, mp); }
        if (pf0 > 0) movePaint(c, t, r0, r1, [pf0, factor](double f) { return pf0 * std::pow(f / pf0, factor); });
    }
}
void gainRegion(Canvas& c, const Sel& s, float g) {
    int t0, t1; selFrames(c, s, t0, t1); int k0, k1; binRange(s, k0, k1);
    const int r0 = std::max(0, (int) std::ceil(s.r0)), r1 = std::min(R - 1, (int) std::floor(s.r1));
    if (t1 > t0) c.touch(t0, t1 - 1);
    for (int t = t0; t < t1; ++t) { for (int k = k0; k <= k1; ++k) c.col(t)[k].mag *= g; for (int r = r0; r <= r1; ++r) c.A(t, r) *= g; }
}
void clearRegion(Canvas& c, const Sel& s) { gainRegion(c, s, 0.0f); }
Canvas trimmed(const Canvas& c, int maxFrames) {
    if (c.T <= maxFrames) return c;
    Canvas o; o.resize(maxFrames);
    std::copy(c.cells.begin(), c.cells.begin() + (ptrdiff_t) maxFrames * K, o.cells.begin());
    std::copy(c.amp.begin(), c.amp.begin() + (ptrdiff_t) maxFrames * R, o.amp.begin()); std::copy(c.fq.begin(), c.fq.begin() + (ptrdiff_t) maxFrames * R, o.fq.begin());
    std::copy(c.noise.begin(), c.noise.begin() + (ptrdiff_t) maxFrames * R, o.noise.begin());
    if (c.srcLen > 0) o.srcLen = std::min(c.srcLen, maxFrames * HOP);
    o.touched.assign((size_t) maxFrames, 1);
    return o;
}

// ---------------------------------------------------------------- image <-> canvas
void ImageMap::sanitise() {
    auto fin = [](double v, double d) { return std::isfinite(v) ? v : d; };
    seconds = juce::jlimit(0.25, 60.0, fin(seconds, 4.0)); loHz = juce::jlimit(30.0, 12000.0, fin(loHz, 80.0)); hiHz = juce::jlimit(200.0, 20000.0, fin(hiHz, 12000.0));
    if (hiHz < loHz * 1.5) { if (loHz * 1.5 <= 20000.0) hiHz = loHz * 1.5; else loHz = hiHz / 1.5; }
    gamma = juce::jlimit(0.3f, 3.0f, std::isfinite(gamma) ? gamma : 1.0f); rangeDb = juce::jlimit(20.0f, 100.0f, std::isfinite(rangeDb) ? rangeDb : 60.0f);
}
void imageToCanvas(const juce::Image& src, const ImageMap& mIn, Canvas& c) {
    ImageMap m = mIn; m.sanitise();
    const int T = framesFor(m.seconds); c.resize(T);
    const double rLo = juce::jlimit(0.0, (double) R - 2, hzRow(m.loHz)), rHi = juce::jlimit(rLo + 1, (double) R - 1, hzRow(m.hiHz));
    const int ra = (int) std::ceil(rLo), rb = (int) std::floor(rHi); const int rows = rb - ra + 1;
    if (!src.isValid() || rows < 2) return;
    const bool grey = src.getFormat() == juce::Image::SingleChannel;   // a single-channel image keeps its brightness in the alpha channel
    const auto img = (grey ? src : src.convertedToFormat(juce::Image::ARGB)).rescaled(T, rows, juce::Graphics::highResamplingQuality);
    for (int t = 0; t < T; ++t) for (int r = ra; r <= rb; ++r) {
        const auto px = img.getPixelAt(t, rb - r);
        float b = grey ? px.getFloatAlpha() : (0.2126f * px.getFloatRed() + 0.7152f * px.getFloatGreen() + 0.0722f * px.getFloatBlue()) * px.getFloatAlpha();
        b = std::pow(juce::jlimit(0.0f, 1.0f, b), m.gamma);
        c.A(t, r) = b > 0.02f ? std::pow(10.0f, -m.rangeDb * (1.0f - b) / 20.0f) : 0.0f;
    }
    float worst = 0; for (int t = 0; t < T; ++t) { double s = 0; for (int r = 0; r < R; ++r) s += (double) c.A(t, r) * c.A(t, r); worst = std::max(worst, (float) std::sqrt(s)); }
    if (worst > 0) { const float g = 0.28f / worst; for (auto& a : c.amp) a *= g; }
}
juce::Colour heat(float v) {
    v = juce::jlimit(0.0f, 1.0f, v);
    static const juce::uint32 stops[] = { 0xff05030f, 0xff1d0b45, 0xff5b1a8f, 0xffb0279f, 0xffff4f7a, 0xffff9d4a, 0xffffe066, 0xffffffff };
    const float x = v * 7.0f; const int i = std::min(6, (int) x);
    return juce::Colour(stops[i]).interpolatedWith(juce::Colour(stops[i + 1]), x - i);
}
juce::Image canvasImage(const Canvas& c, int width, int height, float rangeDb, double t0, double t1, double r0, double r1) {
    width = std::max(1, width); height = std::max(1, height);
    juce::Image img(juce::Image::RGB, width, height, true);
    if (c.T <= 0) return img;
    if (t1 <= t0) { t0 = 0; t1 = c.T; }
    t0 = juce::jlimit(0.0, (double) c.T - 1, t0); t1 = juce::jlimit(t0 + 1.0, (double) c.T, t1); r0 = juce::jlimit(0.0, (double) R - 2, r0); r1 = juce::jlimit(r0 + 1.0, (double) R - 1, r1);
    float mx = 1.0e-6f;   // brightness is relative to the loudest point of the whole canvas, so zooming keeps the colours
    for (float a : c.amp) mx = std::max(mx, a);
    for (const auto& e : c.cells) mx = std::max(mx, e.mag);
    const int ta = (int) std::floor(t0), tb = std::min(c.T, (int) std::ceil(t1));
    const double rowsPerPx = (r1 - r0) / std::max(1, height - 1);
    std::vector<float> L((size_t) (tb - ta) * R);
    for (int t = ta; t < tb; ++t) for (int r = 0; r < R; ++r) {
        float v = c.level(t, r);
        if (rowsPerPx < 1.0) { if (r > 0) v = std::max(v, c.A(t, r - 1) * 0.85f); if (r + 1 < R) v = std::max(v, c.A(t, r + 1) * 0.85f); }   // thin painted lines stay visible
        L[(size_t) (t - ta) * R + (size_t) r] = v; }
    juce::Image::BitmapData bd(img, juce::Image::BitmapData::writeOnly);
    for (int x = 0; x < width; ++x) {
        const double fa = t0 + (t1 - t0) * x / width, fb = t0 + (t1 - t0) * (x + 1) / width;
        const int xa = juce::jlimit(ta, tb - 1, (int) std::floor(fa)), xb = juce::jlimit(xa + 1, tb, (int) std::ceil(fb));
        for (int y = 0; y < height; ++y) {
            const double rf = r1 - (r1 - r0) * y / std::max(1, height - 1);
            const int ra = juce::jlimit(0, R - 1, (int) std::lround(rf)), rb = juce::jlimit(ra, R - 1, (int) std::lround(rf + rowsPerPx) - 1);
            float v = 0; for (int t = xa; t < xb; ++t) for (int r = ra; r <= std::max(ra, rb); ++r) v = std::max(v, L[(size_t) (t - ta) * R + (size_t) r]);
            const float db = 20.0f * std::log10(std::max(1.0e-9f, v / mx));
            bd.setPixelColour(x, y, heat((db + rangeDb) / rangeDb));
        }
    }
    return img;
}

// ---------------------------------------------------------------- undo
size_t Undo::Entry::bytes() const { return cells.size() * sizeof(Cell) + (amp.size() + fq.size() + noise.size()) * 4 + (whole ? whole->bytes() : 0) + side.getSize() + 64; }
size_t Undo::bytes() const { size_t b = 0; for (auto& e : past) b += e.bytes(); for (auto& e : future) b += e.bytes(); return b; }
Undo::Entry Undo::grab(const Canvas& c, int t0, int t1) {
    Entry e;
    if (getSide) { e.side = getSide(); e.hasSide = true; }
    if (t0 < 0 || c.T == 0) { e.whole = std::make_unique<Canvas>(c); return e; }
    e.t0 = juce::jlimit(0, c.T - 1, t0); e.t1 = juce::jlimit(e.t0, c.T - 1, t1);
    e.cells.assign(c.col(e.t0), c.col(e.t1) + K);
    const size_t a = (size_t) e.t0 * R, b = (size_t) (e.t1 + 1) * R;
    e.amp.assign(c.amp.begin() + (ptrdiff_t) a, c.amp.begin() + (ptrdiff_t) b); e.fq.assign(c.fq.begin() + (ptrdiff_t) a, c.fq.begin() + (ptrdiff_t) b); e.noise.assign(c.noise.begin() + (ptrdiff_t) a, c.noise.begin() + (ptrdiff_t) b);
    return e;
}
void Undo::put(Canvas& c, Entry& e) {
    if (e.hasSide && getSide && setSide) { auto cur = getSide(); setSide(e.side); e.side = std::move(cur); }
    if (!e.hasCanvas) return;
    if (e.whole) { std::swap(c, *e.whole); return; }
    if (e.t1 >= c.T) return;
    std::swap_ranges(e.cells.begin(), e.cells.end(), c.cells.begin() + (ptrdiff_t) e.t0 * K);
    const ptrdiff_t a = (ptrdiff_t) e.t0 * R;
    std::swap_ranges(e.amp.begin(), e.amp.end(), c.amp.begin() + a); std::swap_ranges(e.fq.begin(), e.fq.end(), c.fq.begin() + a); std::swap_ranges(e.noise.begin(), e.noise.end(), c.noise.begin() + a);
    c.touch(e.t0, e.t1);
}
void Undo::begin(const Canvas& c, int t0, int t1) { future.clear(); past.push_back(grab(c, t0, t1)); trim(); }
void Undo::beginFrames(int t0, int t1, std::vector<Cell>&& cells, std::vector<float>&& amp, std::vector<float>&& fq, std::vector<float>&& noise) {
    future.clear(); Entry e; if (getSide) { e.side = getSide(); e.hasSide = true; }
    e.t0 = t0; e.t1 = t1; e.cells = std::move(cells); e.amp = std::move(amp); e.fq = std::move(fq); e.noise = std::move(noise);
    past.push_back(std::move(e)); trim();
}
void Undo::beginSide() { if (!getSide) return; future.clear(); Entry e; e.hasCanvas = false; e.hasSide = true; e.side = getSide(); past.push_back(std::move(e)); trim(); }
void Undo::trim() {
    size_t total = 0; for (auto& e : past) total += e.bytes();
    while (past.size() > 100 || (past.size() > 1 && total > kBudget)) { total -= past.front().bytes(); past.erase(past.begin()); }
}
bool Undo::undo(Canvas& c) { if (past.empty()) return false; auto e = std::move(past.back()); past.pop_back(); put(c, e); future.push_back(std::move(e)); return true; }
bool Undo::redo(Canvas& c) { if (future.empty()) return false; auto e = std::move(future.back()); future.pop_back(); put(c, e); past.push_back(std::move(e)); return true; }

// ---------------------------------------------------------------- save / load
namespace {
constexpr int kMagic1 = 0x474c4231, kMagic2 = 0x474c4232;
inline uint16_t qMag(float m) { if (!(m > 0) || !std::isfinite(m)) return 0; const double db = 20.0 * std::log10((double) m); return (uint16_t) juce::jlimit(1.0, 65535.0, std::round((db + 180.0) * 300.0) + 1.0); }
inline float dqMag(uint16_t q) { return q == 0 ? 0.0f : (float) std::pow(10.0, ((q - 1) / 300.0 - 180.0) / 20.0); }
inline uint16_t qPhase(float p) { const double u = std::fmod((double) p / TWO_PI + 1.0, 1.0); return (uint16_t) ((int) std::lround(u * 65536.0) & 0xFFFF); }
inline float dqPhase(uint16_t q) { const double u = q / 65536.0; return wrapPi(u * TWO_PI); }
constexpr double kFqStep = BIN_HZ / 1024.0;   // frequency offset resolution (from the bin centre), +-32 bins
bool loadV1(juce::InputStream& z, Canvas& c) {
    const int T = z.readInt(), srcLen = z.readInt(), k = z.readInt(), r = z.readInt();
    if (T <= 0 || T > 20000 || k != K || r != R) return false;
    Canvas n; n.resize(T); n.srcLen = srcLen; n.touched.assign((size_t) T, 1);
    const auto want = [](size_t b, int got) { return (int) b == got; };
    if (!want(n.cells.size() * sizeof(Cell), z.read(n.cells.data(), (int) (n.cells.size() * sizeof(Cell))))) return false;
    if (!want(n.amp.size() * 4, z.read(n.amp.data(), (int) n.amp.size() * 4))) return false;
    if (!want(n.fq.size() * 4, z.read(n.fq.data(), (int) n.fq.size() * 4))) return false;
    if (!want(n.noise.size() * 4, z.read(n.noise.data(), (int) n.noise.size() * 4))) return false;
    for (auto& e : n.cells) if (!std::isfinite(e.mag) || !std::isfinite(e.freq) || !std::isfinite(e.phase)) e = Cell();
    for (auto* v : { &n.amp, &n.fq, &n.noise }) for (auto& x : *v) if (!std::isfinite(x)) x = 0;
    c = std::move(n); return true;
}
} // namespace
juce::MemoryBlock save(const Canvas& c, bool sourceMatches) {
    juce::MemoryBlock mb;
    const bool ref = sourceMatches && c.srcLen > 0 && (int) c.touched.size() == c.T;
    { juce::MemoryOutputStream raw(mb, false); juce::GZIPCompressorOutputStream z(raw, 1);
      z.writeInt(kMagic2); z.writeInt(c.T); z.writeInt(c.srcLen); z.writeInt(K); z.writeInt(R); z.writeInt(ref ? 1 : 0);
      // paint plane: only the cells that hold paint
      int nz = 0; for (float a : c.amp) if (a > 0) ++nz;
      z.writeInt(nz);
      { juce::MemoryOutputStream p; for (size_t i = 0; i < c.amp.size(); ++i) if (c.amp[i] > 0) { p.writeInt((int) i); p.writeFloat(c.amp[i]); p.writeFloat(c.fq[i]); p.writeFloat(c.noise[i]); } z.write(p.getData(), p.getDataSize()); }
      // STFT plane: the edited frames (or all of them when there's no source to re-analyse)
      std::vector<int> frames; for (int t = 0; t < c.T; ++t) if (!ref || c.touched[(size_t) t]) frames.push_back(t);
      z.writeInt((int) frames.size());
      std::vector<uint8_t> buf; buf.reserve((size_t) K * 8);
      for (int t : frames) {
          const Cell* cc = c.col(t); buf.clear();
          auto put16 = [&buf](uint16_t v) { buf.push_back((uint8_t) (v & 0xFF)); buf.push_back((uint8_t) (v >> 8)); };
          put16((uint16_t) (t & 0xFFFF)); put16((uint16_t) (t >> 16));
          for (int k = 0; k < K; ++k) put16(qMag(cc[k].mag));
          for (int k = 0; k < K; ++k) {
              if (qMag(cc[k].mag) == 0) continue;
              put16(qPhase(cc[k].phase));
              const double off = (cc[k].freq - k * BIN_HZ) / kFqStep;
              if (std::isfinite(off) && std::abs(off) < 32766.0) put16((uint16_t) (int16_t) std::lround(off));
              else { put16((uint16_t) 0x7FFF); float f = std::isfinite(cc[k].freq) ? cc[k].freq : 0.0f; uint8_t b4[4]; std::memcpy(b4, &f, 4); buf.insert(buf.end(), b4, b4 + 4); }
              buf.push_back((uint8_t) (cc[k].kind | ((cc[k].spare & 1) << 4))); buf.push_back((uint8_t) cc[k].lock);   // high nibble: attack mark
          }
          z.write(buf.data(), buf.size());
      }
      z.flush(); }
    return mb;
}
bool load(const juce::MemoryBlock& m, Canvas& c, const std::vector<float>* source) {
    juce::MemoryInputStream raw(m, false); juce::GZIPDecompressorInputStream z(raw);
    const int magic = z.readInt();
    if (magic == kMagic1) return loadV1(z, c);
    if (magic != kMagic2) return false;
    const int T = z.readInt(), srcLen = z.readInt(), k = z.readInt(), r = z.readInt(), ref = z.readInt();
    if (T <= 0 || T > 20000 || k != K || r != R) return false;
    Canvas n;
    if (ref) {
        if (!source || (int) source->size() != srcLen || srcLen <= 0) return false;
        analyse(*source, n); if (n.T != T) return false;
    } else { n.resize(T); n.srcLen = srcLen; }
    const int nz = z.readInt(); if (nz < 0 || nz > T * R) return false;
    for (int i = 0; i < nz; ++i) { const int ix = z.readInt(); const float a = z.readFloat(), f = z.readFloat(), ns = z.readFloat();
        if (ix < 0 || ix >= T * R) return false; n.amp[(size_t) ix] = std::isfinite(a) ? juce::jlimit(0.0f, 16.0f, a) : 0.0f; n.fq[(size_t) ix] = std::isfinite(f) ? juce::jlimit(0.0f, 24000.0f, f) : 0.0f; n.noise[(size_t) ix] = std::isfinite(ns) ? juce::jlimit(0.0f, 1.0f, ns) : 0.0f; }
    const int nf = z.readInt(); if (nf < 0 || nf > T) return false;
    std::vector<uint16_t> mags((size_t) K);
    auto get16 = [&z]() { uint8_t b[2] = { 0, 0 }; z.read(b, 2); return (uint16_t) (b[0] | (b[1] << 8)); };
    for (int f = 0; f < nf; ++f) {
        const int t = (int) get16() | ((int) get16() << 16); if (t < 0 || t >= T || z.isExhausted()) return false;
        Cell* cc = n.col(t); n.touched[(size_t) t] = 1;
        for (int q = 0; q < K; ++q) mags[(size_t) q] = get16();
        for (int q = 0; q < K; ++q) {
            Cell e; e.mag = dqMag(mags[(size_t) q]);
            if (mags[(size_t) q] != 0) {
                e.phase = dqPhase(get16()); const uint16_t fo = get16();
                if (fo == 0x7FFF) { float fl = 0; z.read(&fl, 4); e.freq = std::isfinite(fl) ? fl : 0.0f; } else e.freq = (float) std::max(0.0, q * BIN_HZ + (int16_t) fo * kFqStep);
                uint8_t kl[2] = { 0, 0 }; z.read(kl, 2); e.kind = (kl[0] & 0x0F) == Moved ? Moved : Orig; e.spare = (uint16_t) ((kl[0] >> 4) & 1); e.lock = (int8_t) kl[1];
            }
            cc[q] = e;
        }
    }
    c = std::move(n); return true;
}
juce::MemoryBlock packAudio(const std::vector<float>& x) {
    juce::MemoryBlock out; if (x.empty()) return out;
    const float pk = peakOf(x); const float g = pk > 0.999f ? pk / 0.999f : 1.0f;   // 24-bit FLAC clips at full scale: keep the gain apart
    std::vector<float> y(x); if (g != 1.0f) for (auto& v : y) v /= g;
    for (auto& v : y) if (!std::isfinite(v)) v = 0;
    juce::MemoryBlock flacData;
    { auto* os = new juce::MemoryOutputStream(flacData, false); juce::FlacAudioFormat flac;
      std::unique_ptr<juce::AudioFormatWriter> w(flac.createWriterFor(os, SR, 1, 24, {}, 5));   // NOT-EXPORT: packAudio, the Lab's sound inside the project state (memory)
      if (!w) { delete os; return out; }
      const float* ch[] = { y.data() }; w->writeFromFloatArrays(ch, 1, (int) y.size()); }
    juce::MemoryOutputStream mo(out, false); mo.write("FLC1", 4); mo.writeFloat(g); mo.writeInt((int) x.size()); mo.write(flacData.getData(), flacData.getSize());
    return out;
}
std::vector<float> unpackAudio(const juce::MemoryBlock& m) {
    if (m.getSize() > 12 && std::memcmp(m.getData(), "FLC1", 4) == 0) {
        juce::MemoryInputStream hdr(m, false); hdr.skipNextBytes(4); const float g = hdr.readFloat(); const int n = hdr.readInt();
        if (n <= 0 || n > 48000 * 120 || !std::isfinite(g) || g <= 0 || g > 1000) return {};
        juce::FlacAudioFormat flac;
        std::unique_ptr<juce::AudioFormatReader> r(flac.createReaderFor(new juce::MemoryInputStream((const char*) m.getData() + 12, m.getSize() - 12, false), true));
        if (!r) return {};
        juce::AudioBuffer<float> b(1, n); b.clear(); r->read(&b, 0, n, 0, true, false);
        std::vector<float> x((size_t) n); for (int i = 0; i < n; ++i) { const float v = b.getSample(0, i) * g; x[(size_t) i] = std::isfinite(v) ? v : 0.0f; }
        return x;
    }
    juce::MemoryInputStream raw(m, false); juce::GZIPDecompressorInputStream z(raw); const int n = z.readInt();   // the first format: gzip floats
    if (n <= 0 || n > 48000 * 120) return {}; std::vector<float> x((size_t) n); if (z.read(x.data(), n * 4) != n * 4) return {};
    for (auto& v : x) if (!std::isfinite(v)) v = 0; return x;
}

// ---------------------------------------------------------------- audio helpers
std::vector<float> resampleTo48k(const float* x, int n, double fromRate) {
    if (n <= 0) return {};
    if (std::abs(fromRate - SR) < 1.0) return std::vector<float>(x, x + n);
    juce::WindowedSincInterpolator ip;
    const double ratio = fromRate / SR; const int outN = (int) std::floor(n / ratio);
    const int lag = (int) std::lround(juce::WindowedSincInterpolator::getBaseLatency() / ratio);   // the interpolator's own delay, in output samples
    std::vector<float> in(x, x + n); in.resize((size_t) n + 64 + (size_t) std::ceil((lag + 4) * ratio), 0.0f);
    std::vector<float> out((size_t) std::max(1, outN + lag)); ip.process(ratio, in.data(), out.data(), (int) out.size());
    out.erase(out.begin(), out.begin() + std::min((ptrdiff_t) lag, (ptrdiff_t) out.size() - 1));
    out.resize((size_t) std::max(1, outN));
    return out;
}
bool writeWav(const juce::File& f, const std::vector<float>& L, const std::vector<float>& Rt, double rate) {
    f.getParentDirectory().createDirectory();
    const int n = (int) std::max(L.size(), Rt.size());
    juce::AudioBuffer<float> b(2, std::max(1, n)); b.clear();
    if (!L.empty()) b.copyFrom(0, 0, L.data(), (int) L.size());
    if (!Rt.empty()) b.copyFrom(1, 0, Rt.data(), (int) Rt.size());
    juce::WavAudioFormat wav; auto tmp = f.getSiblingFile(f.getFileName() + ".part"); tmp.deleteFile();
    { std::unique_ptr<juce::OutputStream> os(tmp.createOutputStream().release());
      if (!os) return false;
      std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(os.get(), rate, 2, 24, {}, 0));   // GATE: via-caller (LabPanel asked spec.lab.wav-drag / region-drag / send-beats)
      if (!w) return false;
      (void) os.release();   // the writer owns the stream now
      if (!w->writeFromAudioSampleBuffer(b, 0, n)) return false; }
    return tmp.moveFileTo(f);
}
bool writeWav(const juce::File& f, const std::vector<float>& x, double rate) { return writeWav(f, x, x, rate); }
float peakOf(const std::vector<float>& x) { float p = 0; for (float v : x) p = std::max(p, std::abs(v)); return p; }
float rmsOf(const std::vector<float>& x) { double s = 0; for (float v : x) s += (double) v * v; return x.empty() ? 0.0f : (float) std::sqrt(s / (double) x.size()); }
void normalise(std::vector<float>& x, float peak) { const float p = peakOf(x); if (p > 1.0e-6f) { const float g = peak / p; for (auto& v : x) v *= g; } }
} // namespace lab
