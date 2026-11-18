#include "VoiceMorph.h"
#include <juce_dsp/juce_dsp.h>

namespace gslice::gen::vm
{
namespace
{
// cos to ~1e-7 (deterministic, about 3x std::cos): range-reduce to [-pi, pi], then cos(x) = 1 - 2 sin^2(x/2) with an odd polynomial
inline double fcos (double x)
{
    constexpr double inv2Pi = 0.15915494309189535, twoPi = 6.283185307179586;
    x -= twoPi * std::floor (x * inv2Pi + 0.5);
    const double h = 0.5 * x, h2 = h * h;
    const double sn = h * (1.0 + h2 * (-1.0 / 6 + h2 * (1.0 / 120 + h2 * (-1.0 / 5040 + h2 * (1.0 / 362880 + h2 * (-1.0 / 39916800))))));
    return 1.0 - 2.0 * sn * sn;
}
constexpr double kPi = 3.14159265358979323846, k2Pi = 2.0 * kPi;
inline double wrapPi (double x) { x = std::fmod (x + kPi, k2Pi); if (x < 0) x += k2Pi; return x - kPi; }
inline double melOf (double hz) { return 2595.0 * std::log10 (1.0 + hz / 700.0); }
inline double bandHz (int b, double fs) { const double m = melOf (0.5 * fs) * (b + 0.5) / kBands; return 700.0 * (std::pow (10.0, m / 2595.0) - 1.0); }
inline int bandOf (double hz, double fs) { return juce::jlimit (0, kBands - 1, (int) (melOf (juce::jmax (0.0, hz)) / melOf (0.5 * fs) * kBands)); }
double topHz (double fs) { return juce::jmin (16000.0, 0.45 * fs); }

// shaped noise by FFT overlap-add (Hann, 1024, hop 256): mag(k) per bin
struct Ola
{
    static constexpr int N = 1024, H = 256;
    juce::dsp::FFT fft { 10 }; std::vector<float> win, buf;
    Ola() : win ((size_t) N), buf ((size_t) N * 2) { for (int i = 0; i < N; ++i) win[(size_t) i] = (float) (0.5 - 0.5 * std::cos (k2Pi * i / N)); }
    template <typename Mag> void add (std::vector<float>& out, int centre, dsp::Rng& r, Mag&& mag)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < N; ++i) buf[(size_t) i] = (float) (1.7320508075688772 * r.sym()) * win[(size_t) i];
        fft.performRealOnlyForwardTransform (buf.data(), true);
        for (int k = 0; k <= N / 2; ++k) { const float m = (float) mag (k); buf[(size_t) (2 * k)] *= m; buf[(size_t) (2 * k + 1)] *= m; }
        fft.performRealOnlyInverseTransform (buf.data());
        const int a = centre - N / 2;
        for (int i = 0; i < N; ++i) { const int j = a + i; if (j >= 0 && j < (int) out.size()) out[(size_t) j] += buf[(size_t) i] * win[(size_t) i] * (2.0f / 3.0f); }   // (Hann^2 at hop N/4 sums to 1.5)
    }
};
} // namespace

double Model::envAt (const HFrame& f, double hz) const
{   // the harmonic peaks joined in log amplitude (below the first: its level; above the last: falling 12 dB per octave)
    if (f.nh <= 0 || f.f0 <= 0) return -30.0;
    const double x = hz / f.f0;
    auto la = [&] (int h) { return std::log (juce::jmax (1e-9f, f.a[(size_t) h - 1])); };
    if (x <= 1.0) return la (1);
    if (x >= f.nh) return la (f.nh) - 12.0 / 8.685889638 * std::log2 (x / f.nh);
    const int h = (int) x; const double w = x - h;
    return la (h) + (la (h + 1) - la (h)) * w;
}

Model analyse (const float* x, int n, double fs, const std::function<double (double)>& f0At, int periods)
{
    Model m; m.fs = fs; m.n = n; m.x.assign (x, x + n);
    const int nf = n / kHop + 2; m.fr.resize ((size_t) nf);
    const double top = topHz (fs);
    // ---- harmonics: a DFT at exactly h x f0 under a 4-period Hann window centred on the frame
    for (int j = 0; j < nf; ++j)
    {
        auto& F = m.fr[(size_t) j]; const int c = j * kHop;
        double f0 = f0At ((double) juce::jmin (c, n - 1));
        if (! (f0 >= 50.0 && f0 <= 1400.0)) f0 = 0;
        F.f0 = f0;
        if (f0 <= 0) continue;
        const int L = juce::jlimit (128, 16384, (int) std::lround (juce::jlimit (2, 8, periods) * fs / f0)), half = L / 2;   // (whole periods: the Hann's zeros fall on the other harmonics)
        std::vector<double> w ((size_t) L); double ws = 0;
        for (int i = 0; i < L; ++i) { w[(size_t) i] = 0.5 - 0.5 * std::cos (k2Pi * (i + 0.5) / L); ws += w[(size_t) i]; }
        F.nh = juce::jlimit (0, kMaxH, (int) (top / f0)); F.a.assign ((size_t) F.nh, 0.0f); F.ph.assign ((size_t) F.nh, 0.0f);
        for (int h = 1; h <= F.nh; ++h)
        {
            // X = sum v_i e^{-i om (i - half)}: the phasor z starts at e^{i om half} and turns by e^{-i om} each sample
            const double om = k2Pi * h * f0 / fs; double re = 0, im = 0;
            const double cw = std::cos (om), sw = std::sin (om); double zr = std::cos (om * half), zi = std::sin (om * half);
            for (int i = 0; i < L; ++i)
            {
                const int k = c - half + i;
                if (k >= 0 && k < n) { const double v = (double) x[k] * w[(size_t) i]; re += v * zr; im += v * zi; }
                const double t = zr * cw + zi * sw; zi = zi * cw - zr * sw; zr = t;
            }
            F.a[(size_t) h - 1] = (float) (2.0 * std::sqrt (re * re + im * im) / ws);
            F.ph[(size_t) h - 1] = (float) std::atan2 (im, re);   // x ~ a cos (om t + ph) around the centre
        }
    }
    // ---- the harmonics resynthesised with their own amplitudes and phases (McAulay-Quatieri cubic phase), then the residual
    m.harm.assign ((size_t) n, 0.0f);
    for (int j = 0; j + 1 < nf; ++j)
    {
        const auto& A = m.fr[(size_t) j]; const auto& B = m.fr[(size_t) j + 1];
        const int s0 = j * kHop; if (s0 >= n) break;
        const int T = kHop;
        const int H = juce::jmax (A.nh, B.nh);
        for (int h = 1; h <= H; ++h)
        {
            const bool ha = h <= A.nh && A.f0 > 0, hb = h <= B.nh && B.f0 > 0;
            if (! ha && ! hb) continue;
            const double fa = ha ? A.f0 : B.f0, fb = hb ? B.f0 : A.f0;
            const double a0 = ha ? A.a[(size_t) h - 1] : 0.0, a1 = hb ? B.a[(size_t) h - 1] : 0.0;
            const double w0 = k2Pi * h * fa / m.fs, w1 = k2Pi * h * fb / m.fs;
            // a harmonic that is born / dies: its phase follows its own frequency from the side that has it
            const double th0 = ha ? A.ph[(size_t) h - 1] : (double) B.ph[(size_t) h - 1] - w1 * T, th1 = hb ? B.ph[(size_t) h - 1] : (double) A.ph[(size_t) h - 1] + w0 * T;
            const double M = std::round (((th0 + w0 * T - th1) + (w1 - w0) * T * 0.5) / k2Pi);
            const double e = th1 - th0 - w0 * T + k2Pi * M;
            const double al = 3.0 / (T * T) * e - (w1 - w0) / T, be = -2.0 / ((double) T * T * T) * e + (w1 - w0) / ((double) T * T);
            for (int t = 0; t < T && s0 + t < n; ++t)
            {
                const double th = th0 + w0 * t + al * t * t + be * t * t * t;
                m.harm[(size_t) (s0 + t)] += (float) ((a0 + (a1 - a0) * t / T) * fcos (th));
            }
        }
    }
    m.resid.resize ((size_t) n); for (int i = 0; i < n; ++i) m.resid[(size_t) i] = x[i] - m.harm[(size_t) i];
    // ---- the residual's noise shape per frame (band RMS, mel bands)
    {
        juce::dsp::FFT fft (10); std::vector<float> buf (2048);
        for (int j = 0; j < nf; ++j)
        {
            std::fill (buf.begin(), buf.end(), 0.0f); const int c = j * kHop;
            for (int i = 0; i < 1024; ++i) { const int k = c - 512 + i; buf[(size_t) i] = k >= 0 && k < n ? m.resid[(size_t) k] * (float) (0.5 - 0.5 * std::cos (k2Pi * i / 1024)) : 0.0f; }
            fft.performFrequencyOnlyForwardTransform (buf.data());
            std::array<double, kBands> e {}; std::array<int, kBands> cnt {};
            for (int k = 1; k < 512; ++k) { const int b = bandOf (k * fs / 1024.0, fs); e[(size_t) b] += (double) buf[(size_t) k] * buf[(size_t) k]; ++cnt[(size_t) b]; }
            for (int b = 0; b < kBands; ++b) m.fr[(size_t) j].rb[(size_t) b] = (float) std::sqrt (cnt[(size_t) b] ? e[(size_t) b] / cnt[(size_t) b] : 0.0);
        }
    }
    return m;
}

std::vector<float> render (const Model& m, const Plan& p)
{
    const int nOut = (int) p.f0.size(); const int n = nOut * kHop;
    std::vector<float> out ((size_t) juce::jmax (1, n), 0.0f);
    if (nOut == 0 || m.fr.empty()) return out;
    const double fs = m.fs, top = topHz (fs);
    auto frameAt = [&] (double s) -> std::pair<int, double> { s = juce::jlimit (0.0, (double) m.fr.size() - 1.0, s); const int i = juce::jmin ((int) m.fr.size() - 2, (int) s); return { juce::jmax (0, i), s - juce::jmax (0, i) }; };
    // ---- per output frame: each harmonic's target amplitude and phase offset (to h x the fundamental)
    struct OF { double f0 = 0; int nh = 0; std::vector<float> a, d; };
    std::vector<OF> of ((size_t) nOut);
    dsp::Rng pr ((uint64) p.seed * 7919u + 1u); std::vector<double> fixedPh ((size_t) kMaxH + 1); for (auto& x : fixedPh) x = pr.uni() * k2Pi;
    voc::SynthSpec iss; iss.formantRatio = p.formantRatio;
    for (int j = 0; j < nOut; ++j)
    {
        auto& O = of[(size_t) j]; O.f0 = p.f0[(size_t) j]; if (O.f0 <= 0) continue;
        const auto [i0, w] = frameAt (p.src[(size_t) j]);
        const auto& A = m.fr[(size_t) i0]; const auto& B = m.fr[(size_t) i0 + 1];
        const HFrame* src = A.f0 > 0 ? (B.f0 > 0 && w > 0.5 ? &B : &A) : (B.f0 > 0 ? &B : nullptr);
        O.nh = juce::jlimit (0, kMaxH, (int) (top / O.f0)); O.a.assign ((size_t) O.nh, 0.0f); O.d.assign ((size_t) O.nh, 0.0f);
        const double iw = juce::jlimit (0.0f, 1.0f, p.inst[(size_t) j]), pw = juce::jlimit (0.0f, 1.0f, p.phase[(size_t) j]);
        voc::Frame inF; const bool needInst = iw > 1e-6 || src == nullptr; if (needInst && p.instFrame) inF = p.instFrame (j);
        for (int h = 1; h <= O.nh; ++h)
        {
            const double hz = h * O.f0;
            // the voice's own: its harmonic-peak envelope (both neighbouring frames, blended) at hz / formant ratio; its phase offset
            double lnV = -1e9, dV = 0;
            if (src != nullptr)
            {
                const double ea = A.f0 > 0 ? m.envAt (A, hz / p.formantRatio) : m.envAt (B, hz / p.formantRatio), eb = B.f0 > 0 ? m.envAt (B, hz / p.formantRatio) : ea;
                lnV = ea + (eb - ea) * w;
                // the period's shape: harmonic h's phase relative to h x the fundamental's (the nearest harmonic it had)
                const int hs = juce::jlimit (1, juce::jmax (1, src->nh), h);
                dV = src->nh > 0 ? wrapPi ((double) src->ph[(size_t) hs - 1] - hs * (double) src->ph[0]) : 0.0;
            }
            double lnI = needInst && p.instFrame ? voc::harmonicLnAmp (inF, hz, O.f0, iss, fs) : -1e9;
            double ln = src == nullptr ? lnI : (iw <= 0 ? lnV : lnI < -1e8 ? lnV + std::log (juce::jmax (1e-9, 1.0 - iw)) : lnV + (lnI - lnV) * iw);
            O.a[(size_t) h - 1] = ln < -50 ? 0.0f : (float) (std::exp (ln) * p.gain[(size_t) j]);
            // toward the fixed offset by `phase` (circular interpolation)
            O.d[(size_t) h - 1] = (float) wrapPi (dV + wrapPi (fixedPh[(size_t) h] - dV) * (src == nullptr ? 1.0 : pw));
        }
    }
    // ---- the harmonics: harmonic h's phase at each frame = h x Phi_j + D_h (shape-invariant: the period's shape is the voice's), then
    //      McAulay-Quatieri cubic phase between frames (each harmonic meets its phase AND its frequency at both ends: the smoothest path)
    {
        std::vector<double> Phi ((size_t) nOut, 0.0);   // the fundamental's phase at each frame centre (0 .. 2 pi)
        double ph = 0;
        {   // start on the recording's own fundamental phase where the first frame is locked
            const auto [k0, kw] = frameAt (p.src[0]); const auto& M = m.fr[(size_t) (kw > 0.5 ? k0 + 1 : k0)];
            if (! p.lock.empty() && p.lock[0] > 0.5f && M.nh > 0) ph = M.ph[0];
        }
        for (int j = 0; j < nOut; ++j)
        {
            if (j > 0)
            {
                const double fa = of[(size_t) j - 1].f0 > 0 ? of[(size_t) j - 1].f0 : of[(size_t) j].f0, fb = of[(size_t) j].f0 > 0 ? of[(size_t) j].f0 : fa;
                if (fa > 0) { const double mean = std::abs (fb / fa - 1.0) < 1e-9 ? fa : (fb - fa) / std::log (fb / fa); ph += k2Pi * kHop * mean / fs; }   // (the geometric glide's mean frequency)
                // the lock: meet the recording's fundamental phase here
                const float lk = p.lock.empty() ? 0.0f : p.lock[(size_t) j];
                if (lk > 1e-6f && of[(size_t) j].f0 > 0)
                {
                    const auto [k0, kw] = frameAt (p.src[(size_t) j]); const auto& M = m.fr[(size_t) (kw > 0.5 ? k0 + 1 : k0)];
                    if (M.nh > 0) { const double target = (double) M.ph[0] + (kw > 0.5 ? -(1.0 - kw) : kw) * kHop * k2Pi * M.f0 / fs; ph += lk * wrapPi (target - ph); }
                }
            }
            ph = std::fmod (ph, k2Pi); if (ph < 0) ph += k2Pi;
            Phi[(size_t) j] = ph;
        }
        const int T = kHop;
        for (int j = 0; j + 1 < nOut; ++j)
        {
            const auto& A = of[(size_t) j]; const auto& B = of[(size_t) j + 1];
            if (A.f0 <= 0 && B.f0 <= 0) continue;
            const int H = juce::jmax (A.nh, B.nh), s0 = j * T;
            for (int h = 1; h <= H; ++h)
            {
                const bool ha = A.f0 > 0 && h <= A.nh, hb = B.f0 > 0 && h <= B.nh;
                if (! ha && ! hb) continue;
                const double a0 = ha ? A.a[(size_t) h - 1] : 0.0, a1 = hb ? B.a[(size_t) h - 1] : 0.0;
                if (a0 <= 0 && a1 <= 0) continue;
                const double fa = ha ? A.f0 : B.f0, fb = hb ? B.f0 : A.f0;
                const double w0 = k2Pi * h * fa / fs, w1 = k2Pi * h * fb / fs;
                const double t0 = ha ? std::fmod (h * Phi[(size_t) j], k2Pi) + A.d[(size_t) h - 1] : 0.0, t1 = hb ? std::fmod (h * Phi[(size_t) j + 1], k2Pi) + B.d[(size_t) h - 1] : 0.0;
                const double th0 = ha ? t0 : t1 - w1 * T, th1 = hb ? t1 : t0 + w0 * T;
                const double M = std::round (((th0 + w0 * T - th1) + (w1 - w0) * T * 0.5) / k2Pi);
                const double e = th1 - th0 - w0 * T + k2Pi * M;
                const double al = 3.0 / ((double) T * T) * e - (w1 - w0) / T, be = -2.0 / ((double) T * T * T) * e + (w1 - w0) / ((double) T * T);
                for (int t = 0; t < T && s0 + t < n; ++t)
                {
                    if (h * (fa + (fb - fa) * t / T) >= top) break;
                    out[(size_t) (s0 + t)] += (float) ((a0 + (a1 - a0) * t / T) * fcos (th0 + w0 * t + al * t * t + be * t * t * t));
                }
            }
        }
    }
    // ---- the residual: the recording's own samples where time runs 1:1, noise shaped like it where the vowel is stretched
    {
        std::vector<float> noise ((size_t) n, 0.0f); Ola ola; dsp::Rng nr ((uint64) p.seed * 104729u + 5u);
        bool anyNoise = false;
        for (int c = 0; c < n + Ola::N / 2; c += Ola::H)
        {
            const int j = juce::jlimit (0, nOut - 1, c / kHop);
            if (p.copy[(size_t) j] && p.air[(size_t) j] <= 1e-6f) continue;
            const auto [i0, w] = frameAt (p.src[(size_t) j]);
            const auto& A = m.fr[(size_t) i0]; const auto& B = m.fr[(size_t) i0 + 1];
            const float rl = p.copy[(size_t) j] ? 0.0f : p.resid[(size_t) j], al = p.air[(size_t) j];
            if (rl <= 1e-6f && al <= 1e-6f) continue;
            anyNoise = true;
            voc::Frame inF; if (al > 1e-6f && p.instFrame) inF = p.instFrame (j);
            // (the residual band level is the RMS of a Hann-windowed 1024 FFT: the OLA's white noise has unit variance per bin,
            //  so the band RMS / window RMS gives the bin magnitude)
            const double winRms = std::sqrt (1024.0 * 0.375);
            ola.add (noise, c, nr, [&] (int k)
            {
                const double hz = k * fs / Ola::N; if (hz < 40.0 || hz >= top) return 0.0;
                const int b = bandOf (hz, fs);
                double mag = rl * ((1.0 - w) * A.rb[(size_t) b] + w * B.rb[(size_t) b]) / winRms;
                if (al > 1e-6f) { const double e = std::exp (0.5 * inF.logEnvAt (hz, fs)) * 0.00507 * std::sqrt (fs * 0.5); const double ap = juce::jlimit (0.02, 1.0, inF.apAt (hz, fs) * juce::jlimit (0.1, 1.0, hz / p.breathAirHz)); mag += al * e * std::sqrt (ap) * inF.gain / std::sqrt ((double) Ola::N); }
                return mag;
            });
        }
        if (anyNoise) for (int i = 0; i < n; ++i) out[(size_t) i] += noise[(size_t) i];
        // the residual's own samples (time 1:1): src frame position -> sample position, with 5 ms crossfades at the region edges
        for (int j = 0; j < nOut; ++j)
        {
            if (! p.copy[(size_t) j] || p.resid[(size_t) j] <= 1e-6f) continue;
            // (the read position glides from this frame's source position to the next one's: continuous, never a jump at a frame edge)
            const double s0 = p.src[(size_t) j] * kHop, s1 = (j + 1 < nOut ? p.src[(size_t) j + 1] : p.src[(size_t) j] + 1.0) * kHop;
            const bool edgeIn = j == 0 || ! p.copy[(size_t) j - 1], edgeOut = j + 1 >= nOut || ! p.copy[(size_t) j + 1];
            for (int t = 0; t < kHop; ++t)
            {
                const double sp = s0 + (s1 - s0) * t / kHop; const int si = (int) std::floor (sp); if (si < 0 || si + 1 >= m.n) continue;
                const float fr = (float) (sp - si), r = m.resid[(size_t) si] + fr * (m.resid[(size_t) si + 1] - m.resid[(size_t) si]);
                float g = p.resid[(size_t) j] + (j + 1 < nOut ? (p.resid[(size_t) j + 1] - p.resid[(size_t) j]) * (float) t / kHop : 0.0f);
                if (edgeIn) g *= (float) t / kHop; if (edgeOut) g *= (float) (kHop - t) / kHop;
                out[(size_t) (j * kHop + t)] += g * r;
            }
        }
    }
    return out;
}
} // namespace gslice::gen::vm
