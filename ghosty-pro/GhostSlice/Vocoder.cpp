#include "Vocoder.h"
#include "world/cheaptrick.h"
#include "world/d4c.h"
#include "world/stonemask.h"
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <thread>

namespace gslice::gen::voc
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
inline double melOf (double hz) { return 2595.0 * std::log10 (1.0 + hz / 700.0); }
inline double hzOfMel (double m) { return 700.0 * (std::pow (10.0, m / 2595.0) - 1.0); }
inline double gridIdx (double hz, double fs, int K) { return juce::jlimit (0.0, (double) (K - 1), melOf (juce::jmax (0.0, hz)) / melOf (0.5 * fs) * (K - 1)); }
template <size_t K> inline double lerpGrid (const std::array<float, K>& a, double idx)
{
    const int i = juce::jlimit (0, (int) K - 2, (int) idx); const double f = juce::jlimit (0.0, 1.0, idx - i);
    return a[(size_t) i] + (a[(size_t) i + 1] - a[(size_t) i]) * f;
}
} // namespace

double envHz (int i, double fs) { return hzOfMel (melOf (0.5 * fs) * i / (kEnvPts - 1)); }
double apHz (int i, double fs) { return hzOfMel (melOf (0.5 * fs) * i / (kApPts - 1)); }
double Frame::logEnvAt (double hz, double fs) const { return lerpGrid (env, gridIdx (hz, fs, kEnvPts)); }
double Frame::apAt (double hz, double fs) const { return lerpGrid (ap, gridIdx (hz, fs, kApPts)); }

void blend (Frame& a, const Frame& b, double w)
{
    w = juce::jlimit (0.0, 1.0, w); const float wf = (float) w;
    for (size_t i = 0; i < a.env.size(); ++i) a.env[i] += wf * (b.env[i] - a.env[i]);
    for (size_t i = 0; i < a.ap.size(); ++i) a.ap[i] += wf * (b.ap[i] - a.ap[i]);
    a.gain += wf * (b.gain - a.gain);
}

Frame Frames::at (double idx) const
{
    if (f.empty()) return {};
    idx = juce::jlimit (0.0, (double) (f.size() - 1), idx);
    const int i0 = (int) idx, i1 = juce::jmin ((int) f.size() - 1, i0 + 1); const double w = idx - i0;
    Frame r = f[(size_t) i0];
    if (i1 == i0 || w <= 0.0) return r;
    blend (r, f[(size_t) i1], w);
    const double a = f[(size_t) i0].f0, b = f[(size_t) i1].f0;
    r.f0 = a > 0 && b > 0 ? a * std::pow (b / a, w) : (w < 0.5 ? a : b);
    return r;
}

Frame Frames::average (int i0, int i1) const
{
    Frame r; r.env.fill (0.0f); r.ap.fill (0.0f);
    i0 = juce::jlimit (0, frames(), i0); i1 = juce::jlimit (i0, frames(), i1);
    std::vector<int> use; for (int i = i0; i < i1; ++i) if (f[(size_t) i].f0 > 0) use.push_back (i);
    if (use.empty()) for (int i = i0; i < i1; ++i) use.push_back (i);
    if (use.empty()) return r;
    std::vector<double> e (kEnvPts, 0.0), a (kApPts, 0.0), f0s;
    for (int i : use)
    {
        for (int k = 0; k < kEnvPts; ++k) e[(size_t) k] += f[(size_t) i].env[(size_t) k];
        for (int k = 0; k < kApPts; ++k) a[(size_t) k] += f[(size_t) i].ap[(size_t) k];
        if (f[(size_t) i].f0 > 0) f0s.push_back (f[(size_t) i].f0);
    }
    for (int k = 0; k < kEnvPts; ++k) r.env[(size_t) k] = (float) (e[(size_t) k] / use.size());
    for (int k = 0; k < kApPts; ++k) r.ap[(size_t) k] = (float) (a[(size_t) k] / use.size());
    std::sort (f0s.begin(), f0s.end()); r.f0 = f0s.empty() ? 0.0 : f0s[f0s.size() / 2];
    return r;
}

// ---------------------------------------------------------------- analysis (WORLD)
std::shared_ptr<const Frames> analyse (const float* x, int n, double fs, const std::function<double (double)>& f0At)
{
    auto F = std::make_shared<Frames>(); F->fs = fs;
    if (n <= 0) return F;
    const int fsI = (int) std::lround (fs);
    const int nf = (int) std::floor ((double) n / (fs * kFrameS)) + 1;
    std::vector<double> xd ((size_t) n); for (int i = 0; i < n; ++i) xd[(size_t) i] = x[i];
    std::vector<double> t ((size_t) nf), f0 ((size_t) nf);
    for (int i = 0; i < nf; ++i) { t[(size_t) i] = i * kFrameS; const double v = f0At (t[(size_t) i] * fs); f0[(size_t) i] = v >= 50.0 && v <= 1400.0 ? v : 0.0; }
    // a single voiced frame between unvoiced ones is a tracker blip: off
    for (int i = 1; i + 1 < nf; ++i) if (f0[(size_t) i] > 0 && f0[(size_t) i - 1] <= 0 && f0[(size_t) i + 1] <= 0) f0[(size_t) i] = 0;
    CheapTrickOption ct; InitializeCheapTrickOption (fsI, &ct); ct.fft_size = GetFFTSizeForCheapTrick (fsI, &ct);
    const int fft = ct.fft_size, bins = fft / 2 + 1;
    D4COption d4; InitializeD4COption (&d4); d4.threshold = 0.0;   // (our tracker decides what is voiced)
    F->f.resize ((size_t) nf);
    constexpr int kChunk = 64;
    const int chunks = (nf + kChunk - 1) / kChunk;
    std::atomic<int> next { 0 };
    auto work = [&]
    {
        std::vector<double> sp ((size_t) kChunk * bins), ap ((size_t) kChunk * bins), rf ((size_t) kChunk);
        std::vector<double*> spr ((size_t) kChunk), apr ((size_t) kChunk);
        for (int k = 0; k < kChunk; ++k) { spr[(size_t) k] = sp.data() + (size_t) k * bins; apr[(size_t) k] = ap.data() + (size_t) k * bins; }
        for (int c; (c = next.fetch_add (1)) < chunks;)
        {
            const int a = c * kChunk, cnt = juce::jmin (kChunk, nf - a);
            StoneMask (xd.data(), n, fsI, t.data() + a, f0.data() + a, cnt, rf.data());
            std::vector<double> use ((size_t) cnt);
            for (int k = 0; k < cnt; ++k) { const double o = f0[(size_t) (a + k)], r = rf[(size_t) k]; use[(size_t) k] = o > 0 && r > 0 && std::abs (r / o - 1.0) < 0.06 ? r : o; }
            CheapTrickOption ctl = ct;
            CheapTrick (xd.data(), n, fsI, t.data() + a, use.data(), cnt, &ctl, spr.data());
            D4C (xd.data(), n, fsI, t.data() + a, use.data(), cnt, fft, &d4, apr.data());
            for (int k = 0; k < cnt; ++k)
            {
                Frame& fr = F->f[(size_t) (a + k)];
                fr.f0 = use[(size_t) k];
                for (int i = 0; i < kEnvPts; ++i)
                {
                    const double b = envHz (i, fs) * fft / fs; const int b0 = juce::jlimit (0, bins - 2, (int) b); const double w = juce::jlimit (0.0, 1.0, b - b0);
                    const double l0 = std::log (spr[(size_t) k][b0] + 1e-30), l1 = std::log (spr[(size_t) k][b0 + 1] + 1e-30);
                    fr.env[(size_t) i] = (float) (l0 + (l1 - l0) * w);
                }
                for (int i = 0; i < kApPts; ++i)
                {
                    const double b = apHz (i, fs) * fft / fs; const int b0 = juce::jlimit (0, bins - 2, (int) b); const double w = juce::jlimit (0.0, 1.0, b - b0);
                    const double v = apr[(size_t) k][b0] + (apr[(size_t) k][b0 + 1] - apr[(size_t) k][b0]) * w;
                    fr.ap[(size_t) i] = (float) juce::jlimit (0.0, 1.0, fr.f0 > 0 ? v : 1.0);
                }
            }
        }
    };
    const int T = juce::jlimit (1, 8, juce::jmin ((int) std::thread::hardware_concurrency(), chunks));
    if (T <= 1) work();
    else { std::vector<std::thread> th; for (int k = 0; k < T; ++k) th.emplace_back ([&] { juce::ScopedNoDenormals nd; work(); }); for (auto& h : th) h.join(); }
    return F;
}

// ---------------------------------------------------------------- synthesis (our own sinusoids + shaped noise)
namespace
{
constexpr int kNoiseN = 1024, kNoiseOrder = 10, kNoiseHop = 256;
struct NoiseOla
{
    juce::dsp::FFT fft { kNoiseOrder };
    std::vector<float> win, buf;
    NoiseOla() : win ((size_t) kNoiseN), buf ((size_t) kNoiseN * 2) { for (int i = 0; i < kNoiseN; ++i) win[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * i / kNoiseN)); }
    // out[c - N/2 .. c + N/2) += shaped noise; mag(k) = the linear magnitude for bin k
    template <typename Mag> void frame (std::vector<float>& out, int c, dsp::Rng& rng, Mag&& mag, float scale)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < kNoiseN; ++i)   // white noise, unit variance (uniform: after the shaping FFT it is Gaussian-like anyway)
            buf[(size_t) i] = (float) (1.7320508075688772 * (2.0 * rng.uni() - 1.0)) * win[(size_t) i];
        fft.performRealOnlyForwardTransform (buf.data(), true);
        for (int k = 0; k <= kNoiseN / 2; ++k) { const float m = (float) mag (k); buf[(size_t) (2 * k)] *= m; buf[(size_t) (2 * k + 1)] *= m; }
        fft.performRealOnlyInverseTransform (buf.data());
        const int a = c - kNoiseN / 2, n = (int) out.size();
        for (int i = 0; i < kNoiseN; ++i) { const int j = a + i; if (j >= 0 && j < n) out[(size_t) j] += scale * buf[(size_t) i] * win[(size_t) i]; }
    }
};
// the OLA's own gain on white noise (measured once: the FFT library's scaling stays out of the maths)
float noiseNorm()
{
    static const float g = []
    {
        NoiseOla o; dsp::Rng r (99); std::vector<float> y ((size_t) (kNoiseN * 64), 0.0f);
        for (int c = 0; c < (int) y.size() + kNoiseN; c += kNoiseHop) o.frame (y, c, r, [] (int) { return 1.0; }, 1.0f);
        double s = 0; int cnt = 0; for (int i = kNoiseN; i < (int) y.size() - kNoiseN; ++i) { s += (double) y[(size_t) i] * y[(size_t) i]; ++cnt; }
        return (float) (1.0 / std::sqrt (juce::jmax (1e-12, s / juce::jmax (1, cnt))));
    }();
    return g;
}
} // namespace

double harmonicLnAmp (const Frame& F, double hz, double f0, const SynthSpec& s, double fs)
{
    constexpr double kLevel = 0.00507;   // (= synth's)
    const double fr = juce::jlimit (0.25, 4.0, s.formantRatio), top = juce::jmin (s.topHz, 0.47 * fs), knee = 0.8 * top;
    if (hz >= top || f0 <= 0) return -1e9;
    double g = hz <= knee ? 1.0 : 0.5 + 0.5 * std::cos (kPi * (hz - knee) / (top - knee));
    if (s.tiltDbPerOct != 0.0 && hz > 1000.0) g *= std::pow (10.0, s.tiltDbPerOct * std::log2 (hz / 1000.0) / 20.0);
    double ceil = juce::jmax (s.breathFloor, s.breathCeil);
    if (s.airHz > 0.0) { const double u = juce::jlimit (0.0, 1.0, std::log2 (juce::jmax (1.0, hz) / (s.airHz / 3.0)) / std::log2 (3.0)); ceil = s.breathFloor + (ceil - s.breathFloor) * (0.12 + 0.88 * (0.5 - 0.5 * std::cos (kPi * u))); }
    const double ap = juce::jlimit (s.breathFloor, ceil, F.apAt (hz / fr, fs));
    const double a = kLevel * std::sqrt (2.0 * f0 * (1.0 - ap)) * F.gain * g;
    return a > 0 ? std::log (a) + 0.5 * F.logEnvAt (hz / fr, fs) : -1e9;
}

std::vector<float> synth (const Track& T, double fs, const SynthSpec& s)
{
    const int hop = (int) std::lround (fs * kFrameS), nF = (int) T.size();
    const int n = juce::jmax (1, nF * hop);
    std::vector<float> out ((size_t) n, 0.0f);
    if (nF == 0) return out;
    // CheapTrick's envelope is in its own (FFT power) units: this one constant brings a resynthesis to the original's level
    // (measured: GhostSliceTests diag, the vowel at its own pitch within a dB). It scales the harmonics and the noise alike.
    constexpr double kLevel = 0.00507;
    const double fr = juce::jlimit (0.25, 4.0, s.formantRatio), top = juce::jmin (s.topHz, 0.47 * fs), knee = 0.8 * top;
    auto shape = [&] (double hz)
    {   // the top roll-off and the tilt (linear amplitude factor)
        double g = hz >= top ? 0.0 : hz <= knee ? 1.0 : 0.5 + 0.5 * std::cos (kPi * (hz - knee) / (top - knee));
        if (s.tiltDbPerOct != 0.0 && hz > 1000.0) g *= std::pow (10.0, s.tiltDbPerOct * std::log2 (hz / 1000.0) / 20.0);
        return g;
    };
    // ---- the harmonics
    double minF0 = 1e9; if (s.harmonics) for (auto& F : T) if (F.f0 > 0) minF0 = std::min (minF0, F.f0);
    if (minF0 < 1e8)
    {
        const int H = juce::jlimit (1, 600, (int) (top / juce::jmax (30.0, minF0)) + 1);
        std::vector<double> ph ((size_t) H + 1);
        dsp::Rng prng ((uint64) s.seed * 7919u + 3u);
        for (int h = 1; h <= H; ++h) ph[(size_t) h] = prng.uni() * 2.0 * kPi;
        // each harmonic's phase as a unit phasor (pc, ps), carried from sub-block to sub-block by rotation (no trig per harmonic)
        std::vector<double> pc ((size_t) H + 1), ps ((size_t) H + 1);
        for (int h = 1; h <= H; ++h) { pc[(size_t) h] = std::cos (ph[(size_t) h]); ps[(size_t) h] = std::sin (ph[(size_t) h]); }
        std::vector<int> hIdx ((size_t) H + 1);
        std::vector<double> A0 ((size_t) H + 1), A1 ((size_t) H + 1);
        std::vector<double> hc ((size_t) H + 1), hs ((size_t) H + 1), hcw ((size_t) H + 1), hsw ((size_t) H + 1), ha ((size_t) H + 1), hda ((size_t) H + 1);
        // tables over frequency (every 2 Hz, read with linear interpolation): where a harmonic reads the grids, its roll-off / tilt,
        // its voiced breath ceiling (the same functions as apOf / shape, without a log / cos / pow per harmonic per frame)
        constexpr double kLutHz = 2.0;
        const int nl = (int) std::ceil (top / kLutHz) + 2;
        std::vector<double> lE ((size_t) nl), lA ((size_t) nl), lS ((size_t) nl), lC ((size_t) nl);
        for (int i = 0; i < nl; ++i)
        {
            const double hz = i * kLutHz;
            lE[(size_t) i] = gridIdx (hz / fr, fs, kEnvPts); lA[(size_t) i] = gridIdx (hz / fr, fs, kApPts); lS[(size_t) i] = shape (hz);
            double ceil = juce::jmax (s.breathFloor, s.breathCeil);
            if (s.airHz > 0.0)
            {
                const double u = juce::jlimit (0.0, 1.0, std::log2 (juce::jmax (1.0, hz) / (s.airHz / 3.0)) / std::log2 (3.0));
                ceil = s.breathFloor + (ceil - s.breathFloor) * (0.12 + 0.88 * (0.5 - 0.5 * std::cos (kPi * u)));
            }
            lC[(size_t) i] = ceil;
        }
        auto amps = [&] (const Frame& F, double f0, std::vector<double>& A)
        {
            const double g0 = kLevel * std::sqrt (2.0 * f0) * F.gain;
            for (int h = 1; h <= H; ++h)
            {
                const double hz = h * f0;
                if (F.f0 <= 0 || hz >= top) { A[(size_t) h] = 0.0; continue; }
                const double x = hz / kLutHz; const int i = juce::jmin (nl - 2, (int) x); const double w = x - i;
                auto L = [&] (const std::vector<double>& v) { return v[(size_t) i] + (v[(size_t) i + 1] - v[(size_t) i]) * w; };
                const double ap = juce::jlimit (s.breathFloor, L (lC), lerpGrid (F.ap, L (lA)));
                A[(size_t) h] = g0 * std::exp (0.5 * lerpGrid (F.env, L (lE))) * std::sqrt (1.0 - ap) * L (lS);
            }
        };
        constexpr int kSub = 48;
        for (int i = 0; i < nF; ++i)
        {
            const Frame& Fa = T[(size_t) i]; const Frame& Fb = T[(size_t) juce::jmin (nF - 1, i + 1)];
            if (Fa.f0 <= 0 && Fb.f0 <= 0) continue;
            const double fa = Fa.f0 > 0 ? Fa.f0 : Fb.f0, fb = Fb.f0 > 0 ? Fb.f0 : Fa.f0;
            amps (Fa, fa, A0); amps (Fb, fb, A1);
            if (Fa.f0 <= 0) std::fill (A0.begin(), A0.end(), 0.0);
            if (Fb.f0 <= 0) std::fill (A1.begin(), A1.end(), 0.0);
            const int s0 = i * hop;
            for (int sb = 0; sb < hop; sb += kSub)
            {
                const int len = juce::jmin (kSub, hop - sb);
                const double u0 = (double) sb / hop, u1 = (double) (sb + len) / hop, um = 0.5 * (u0 + u1);
                const double f = fa * std::pow (fb / fa, um);
                float* y = out.data() + s0 + sb;
                // the sounding harmonics of this sub-block, then sample by sample across them (the harmonics are independent, so the
                // CPU runs them side by side). The per-sample step of harmonic h is (cos w, sin w)^h and the step over the sub-block
                // (cos len w, sin len w)^h: built up harmonic by harmonic, two cos / sin per sub-block in all.
                const double w1 = 2.0 * kPi * f / fs, cb = std::cos (w1), sb1 = std::sin (w1), cL = std::cos (w1 * len), sL = std::sin (w1 * len);
                double rc = 1.0, rs = 0.0, lc = 1.0, ls = 0.0;
                int na = 0;
                for (int h = 1; h <= H; ++h)
                {
                    { const double t = rc * cb - rs * sb1; rs = rs * cb + rc * sb1; rc = t; }
                    { const double t = lc * cL - ls * sL; ls = ls * cL + lc * sL; lc = t; }
                    const double a0 = A0[(size_t) h] + (A1[(size_t) h] - A0[(size_t) h]) * u0, a1 = A0[(size_t) h] + (A1[(size_t) h] - A0[(size_t) h]) * u1;
                    if (a0 > 0.0 || a1 > 0.0)
                    {
                        const size_t j = (size_t) na++;
                        hIdx[j] = h; hc[j] = pc[(size_t) h]; hs[j] = ps[(size_t) h]; hcw[j] = rc; hsw[j] = rs;
                        ha[j] = a0; hda[j] = (a1 - a0) / len;
                    }
                    else { const double t = pc[(size_t) h] * lc - ps[(size_t) h] * ls; ps[(size_t) h] = ps[(size_t) h] * lc + pc[(size_t) h] * ls; pc[(size_t) h] = t; }
                }
                for (int k = 0; k < len; ++k)
                {
                    float yk = y[k];
                    for (int j = 0; j < na; ++j)
                    {
                        const double sn = hs[(size_t) j], c = hc[(size_t) j];
                        yk += (float) (ha[(size_t) j] * sn); ha[(size_t) j] += hda[(size_t) j];
                        hc[(size_t) j] = c * hcw[(size_t) j] - sn * hsw[(size_t) j]; hs[(size_t) j] = sn * hcw[(size_t) j] + c * hsw[(size_t) j];
                    }
                    y[k] = yk;
                }
                for (int j = 0; j < na; ++j) { pc[(size_t) hIdx[(size_t) j]] = hc[(size_t) j]; ps[(size_t) hIdx[(size_t) j]] = hs[(size_t) j]; }
            }
            // keep the phasors on the unit circle (rounding drifts them a hair over a long note)
            for (int h = 1; h <= H; ++h) { const double m = 1.0 / std::sqrt (pc[(size_t) h] * pc[(size_t) h] + ps[(size_t) h] * ps[(size_t) h]); pc[(size_t) h] *= m; ps[(size_t) h] *= m; }
        }
    }
    // ---- the noise (breath, consonants): shaped by envelope x aperiodicity
    if (s.noise > 1e-6)
    {
        NoiseOla ola; dsp::Rng rng ((uint64) s.seed * 104729u + 77u);
        const float norm = noiseNorm();
        // per bin, once: where it reads the envelope / aperiodicity grids, its roll-off / tilt, and its breath ceiling when voiced
        constexpr int nb = kNoiseN / 2 + 1;
        std::vector<double> eIdx ((size_t) nb), aIdx ((size_t) nb), shp ((size_t) nb), ceilV ((size_t) nb);
        for (int k = 0; k < nb; ++k)
        {
            const double hz = (double) k * fs / kNoiseN;
            eIdx[(size_t) k] = gridIdx (hz / fr, fs, kEnvPts); aIdx[(size_t) k] = gridIdx (hz / fr, fs, kApPts);
            shp[(size_t) k] = hz < 40.0 ? 0.0 : shape (hz);
            double ceil = juce::jmax (s.breathFloor, s.breathCeil);
            if (s.airHz > 0.0)
            {
                const double u = juce::jlimit (0.0, 1.0, std::log2 (juce::jmax (1.0, hz) / (s.airHz / 3.0)) / std::log2 (3.0));
                ceil = s.breathFloor + (ceil - s.breathFloor) * (0.12 + 0.88 * (0.5 - 0.5 * std::cos (kPi * u)));
            }
            ceilV[(size_t) k] = ceil;
        }
        std::vector<double> mag ((size_t) nb);
        for (int c = 0; c < n + kNoiseN / 2; c += kNoiseHop)
        {
            const Frame F = [&] { const double idx = juce::jlimit (0.0, (double) (nF - 1), (double) c / hop); const int i0 = (int) idx, i1 = juce::jmin (nF - 1, i0 + 1);
                                  Frame r = T[(size_t) i0]; blend (r, T[(size_t) i1], idx - i0); r.f0 = (idx - i0 < 0.5 ? T[(size_t) i0] : T[(size_t) i1]).f0; return r; }();
            if (F.gain <= 1e-6f) continue;
            const bool voiced = F.f0 > 0;
            const double g = kLevel * std::sqrt (fs * 0.5) * F.gain * s.noise;
            for (int k = 0; k < nb; ++k)
            {
                if (shp[(size_t) k] <= 0.0) { mag[(size_t) k] = 0.0; continue; }
                const double a = voiced ? juce::jlimit (s.breathFloor, ceilV[(size_t) k], lerpGrid (F.ap, aIdx[(size_t) k])) : 1.0;
                mag[(size_t) k] = g * std::exp (0.5 * lerpGrid (F.env, eIdx[(size_t) k])) * std::sqrt (a) * shp[(size_t) k];
            }
            ola.frame (out, c, rng, [&] (int k) { return mag[(size_t) k]; }, norm);
        }
    }
    return out;
}
} // namespace gslice::gen::voc
