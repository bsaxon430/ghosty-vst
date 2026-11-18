// Flayr Chop audio analysis engine.
//
// Header-only, framework-free C++17 (no JUCE). Everything here is pure computation on a mono float buffer,
// is re-entrant (no globals, no static mutable state) and is meant to run on a background thread.
//
// Pipeline overview:
//   1. Peak-normalise and resample to a fixed 22.05 kHz working rate (windowed-sinc FIR decimation plus a
//      fractional windowed-sinc step for rates that are not multiples), so results do not depend on the
//      file's sample rate.
//   2. Onset detection function (ODF): log-magnitude spectral flux over ~12 bands per octave with a
//      SuperFlux-style max filter across neighbouring bands (suppresses vibrato), 46 ms frames, 5.8 ms hop.
//      Band-limited ODFs are kept for < 160 Hz (kick) and 160 Hz..3 kHz (snare body) for metrical decisions.
//   3. Onsets: local-maximum test plus an adaptive threshold (local median contrast, local mean, and floors
//      tied to the file's loudest hits and to the surrounding +-1.5 s). Each onset is then refined in the time
//      domain to the foot of the attack (sample accurate, on a 20 ms trailing peak envelope of either the
//      signal or its first difference, whichever shows the clearer rise), so reported times are not smeared
//      by the analysis frame. Weak candidates without a 2x waveform rise are dropped (beating, vibrato).
//   4. Tempo: autocorrelation of the ODF, comb filtered over 4 harmonics with a mild log-tempo prior. The
//      winning tempo family (all its octaves) competes with its 3:2 and 4:3 relatives and, for files up to
//      70 s, the family implied by the length being a whole number of bars. Each family is refined on its
//      own and scored on onset strength on its straight 8th grid, ACF at one and two bars, and how well the
//      length holds a whole (preferably power-of-two) number of bars when the file starts on its grid. Period
//      and phase are refined by weighted linear regression of onset times against beat indices over a window
//      that doubles until it spans the file, then polished by a direct +-0.6 % period / phase search
//      maximising the ODF on every beat (a 10 minute file stays phase locked). Octave: below 80 BPM take the
//      double (prefer 80..160); from 80 to 100 BPM double only if half-beats carry nearly as much
//      kick+snare energy as beats, or the snare band hits every beat on the kick's off-beat (a backbeat at the
//      double tempo, as in drum and bass). Confidence is the share of onsets on the beat's 16th / triplet
//      grid, reduced when a 3:2 / 4:3 family scored nearly as well; the other octave and those families are
//      reported as tempo candidates.
//   5. Downbeat: the beat class (mod 4) with kick but without snare energy, nudged towards the class of the
//      first strong onset (loops and most songs start on the one).
//   6. Key: 0.74 s FFT frames at ~11 kHz, spectral peak picking with parabolic interpolation, global tuning
//      estimate, compressed chroma with harmonic suppression, a bass chroma and lowest-note histogram, the
//      chord of every bar and the first / last chord. A linear model over these (rotated to each candidate
//      tonic) scores the 24 keys; confidence is the winner's softmax probability. See keyScores.
//   7. Stems (analyseWithStems): rhythm from the drum stem, chroma from the harmonic + bass stems, bass
//      evidence from the bass stem; the mix stands in for any stem that is missing or near silent.
//
// Known limits: one global tempo (no tempo map for live or rubato material), 4/4 assumed for the downbeat,
// one global key (no modulation tracking), single-line material (a bass line or riff alone) often does not
// state its key, a loop whose rhythm reads equally well at a 3:2 related tempo stays a coin toss (reported
// with low confidence and both tempi as candidates), and very quiet hits inside the decay of a much louder
// one (ghost hats right after a snare) can be missed.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <vector>

namespace chop {
namespace analysis {

struct TempoCandidate { double bpm; float score; };        // score 0..1 relative to the best
struct KeyCandidate { int key; bool minor; float score; };  // score 0..1 relative to the best

struct Result {
    double bpm = 0;            // 60..200 folded to the most musical octave (prefer 80..160 unless clearly otherwise)
    double firstBeatSec = 0;   // phase of the beat grid (a downbeat if one can be found)
    float bpmConfidence = 0;   // 0..1
    int key = 0;               // pitch class 0=C..11=B
    bool minor = false;
    float keyConfidence = 0;   // 0..1
    std::vector<double> onsetsSec;     // transient times, sorted
    std::vector<float> onsetStrength;  // same size, 0..1 relative to the strongest hits in the file
    // Alternatives for the UI, best first (the first entry matches bpm / key). Tempo: up to 4 musically
    // distinct tempi (the other octave and 3:2 / 4:3 relatives that scored well). Key: up to 3 keys (the
    // relative major/minor is usually second).
    std::vector<TempoCandidate> tempoCandidates;
    std::vector<KeyCandidate> keyCandidates;
};

enum class ChopMode { Auto, Beats, Even };

struct Overview { std::vector<float> mn, mx, low, mid, high; };

namespace detail {

constexpr double kPi = 3.14159265358979323846;

inline int nextPow2(int v) { int p = 1; while (p < v) p <<= 1; return p; }

inline float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

// ---------------------------------------------------------------------------------------------------------
// Real FFT of power-of-two size n via an n/2 point iterative radix-2 complex FFT plus the usual split step.
// Separate re/im arrays (no std::complex) so -O2 does not route multiplies through the C99 complex helpers.
class RealFFT {
public:
    explicit RealFFT(int n) : n_(std::max(2, n)), m_(std::max(2, n) / 2) {
        int bits = 0;
        while ((1 << bits) < m_) ++bits;
        rev_.resize((size_t)m_);
        for (int i = 0; i < m_; ++i) {
            int r = 0;
            for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            rev_[(size_t)i] = r;
        }
        twr_.resize((size_t)std::max(1, m_ / 2));
        twi_.resize((size_t)std::max(1, m_ / 2));
        for (int j = 0; j < m_ / 2; ++j) {
            twr_[(size_t)j] = (float)std::cos(2.0 * kPi * j / m_);
            twi_[(size_t)j] = (float)-std::sin(2.0 * kPi * j / m_);
        }
        pr_.resize((size_t)m_ + 1);
        pi_.resize((size_t)m_ + 1);
        for (int k = 0; k <= m_; ++k) {
            pr_[(size_t)k] = (float)std::cos(2.0 * kPi * k / n_);
            pi_[(size_t)k] = (float)-std::sin(2.0 * kPi * k / n_);
        }
        re_.resize((size_t)m_);
        im_.resize((size_t)m_);
    }

    int size() const { return n_; }

    // in: n real samples. out: n/2 + 1 magnitudes |X[k]|.
    void magnitudes(const float* in, float* out) {
        transform(in);
        for (int k = 0; k <= m_; ++k) {
            float xr, xi;
            bin(k, xr, xi);
            out[k] = std::sqrt(xr * xr + xi * xi);
        }
    }

    // in: n real samples. outRe/outIm: n/2 + 1 complex bins.
    void complexBins(const float* in, float* outRe, float* outIm) {
        transform(in);
        for (int k = 0; k <= m_; ++k) bin(k, outRe[k], outIm[k]);
    }

private:
    void transform(const float* in) {
        float* re = re_.data();
        float* im = im_.data();
        for (int k = 0; k < m_; ++k) {
            const int r = rev_[(size_t)k];
            re[r] = in[2 * k];
            im[r] = in[2 * k + 1];
        }
        for (int len = 2; len <= m_; len <<= 1) {
            const int h = len >> 1;
            const int step = m_ / len;
            for (int i = 0; i < m_; i += len) {
                for (int k = 0; k < h; ++k) {
                    const float wr = twr_[(size_t)(k * step)], wi = twi_[(size_t)(k * step)];
                    const int a = i + k, b = a + h;
                    const float tr = re[b] * wr - im[b] * wi;
                    const float ti = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - tr; im[b] = im[a] - ti;
                    re[a] += tr;        im[a] += ti;
                }
            }
        }
    }

    void bin(int k, float& xr, float& xi) const {
        const int k1 = (k == m_) ? 0 : k;
        const int k2 = (m_ - k) % m_;
        const float zr1 = re_[(size_t)k1], zi1 = im_[(size_t)k1];
        const float zr2 = re_[(size_t)k2], zi2 = im_[(size_t)k2];
        const float er = 0.5f * (zr1 + zr2), ei = 0.5f * (zi1 - zi2);
        const float orr = 0.5f * (zi1 + zi2), oi = -0.5f * (zr1 - zr2);
        const float wr = pr_[(size_t)k], wi = pi_[(size_t)k];
        xr = er + wr * orr - wi * oi;
        xi = ei + wr * oi + wi * orr;
    }

    int n_, m_;
    std::vector<int> rev_;
    std::vector<float> twr_, twi_, pr_, pi_, re_, im_;
};

// ---------------------------------------------------------------------------------------------------------
// Windowed-sinc (Blackman) low-pass + integer decimation, with a gain applied on the way.
inline std::vector<float> decimate(const float* x, int n, int factor, float gain) {
    std::vector<float> out;
    if (n <= 0) return out;
    if (factor <= 1) {
        out.resize((size_t)n);
        for (int i = 0; i < n; ++i) out[(size_t)i] = x[i] * gain;
        return out;
    }
    const int half = 8 * factor;
    const int L = 2 * half + 1;
    const double fc = 0.45 / factor;  // cycles per input sample
    std::vector<float> h((size_t)L);
    double sum = 0;
    for (int i = 0; i < L; ++i) {
        const int m = i - half;
        const double s = (m == 0) ? 2.0 * fc : std::sin(2.0 * kPi * fc * m) / (kPi * m);
        const double w = 0.42 - 0.5 * std::cos(2.0 * kPi * i / (L - 1)) + 0.08 * std::cos(4.0 * kPi * i / (L - 1));
        h[(size_t)i] = (float)(s * w);
        sum += s * w;
    }
    for (auto& v : h) v = (float)(v * gain / sum);
    const int outN = (n + factor - 1) / factor;
    out.resize((size_t)outN);
    for (int m = 0; m < outN; ++m) {
        const int c = m * factor;
        const int lo = c - half, hi = c + half;
        float acc = 0;
        if (lo >= 0 && hi < n) {
            const float* xp = x + lo;
            for (int i = 0; i < L; ++i) acc += h[(size_t)i] * xp[i];
        } else {
            for (int i = 0; i < L; ++i) {
                const int j = lo + i;
                if (j >= 0 && j < n) acc += h[(size_t)i] * x[j];
            }
        }
        out[(size_t)m] = acc;
    }
    return out;
}

// Fractional-ratio windowed-sinc (Blackman, 8 zero crossings a side) resampler with a 1024-phase kernel
// table. Linear phase (no time shift); cut-off at 0.45 of the lower rate, the same as decimate(), so a 48 kHz
// file reaches the working rate with the same band limit as a 44.1 kHz one.
inline std::vector<float> resample(const std::vector<float>& x, double srIn, double srOut) {
    std::vector<float> out;
    const int n = (int)x.size();
    if (n <= 0 || !(srIn > 0) || !(srOut > 0)) return out;
    const double ratio = srIn / srOut;                         // input samples per output sample
    const double fc = 0.45 * std::min(srIn, srOut) / srIn;     // cycles per input sample (as decimate)
    const int half = (int)std::ceil(8.0 / (2.0 * fc));
    const int L = 2 * half + 1, P = 1024;
    std::vector<float> table((size_t)(P + 1) * (size_t)L);
    for (int ph = 0; ph <= P; ++ph) {
        const double frac = (double)ph / P;
        double sum = 0;
        float* row = &table[(size_t)ph * (size_t)L];
        for (int i = 0; i < L; ++i) {
            const double u = i - half - frac;  // tap position relative to the output instant
            const double sv = std::fabs(u) < 1e-12 ? 2.0 * fc : std::sin(2.0 * kPi * fc * u) / (kPi * u);
            const double wpos = (u + half + 1.0) / (2.0 * half + 2.0);  // 0..1 across the window
            const double w = 0.42 - 0.5 * std::cos(2.0 * kPi * wpos) + 0.08 * std::cos(4.0 * kPi * wpos);
            row[i] = (float)(sv * w);
            sum += sv * w;
        }
        for (int i = 0; i < L; ++i) row[i] = (float)(row[i] / sum);
    }
    const long outN = (long)std::floor((n - 1) / ratio) + 1;
    out.resize((size_t)outN);
    for (long m = 0; m < outN; ++m) {
        const double t = m * ratio;
        long k = (long)std::floor(t);
        int ph = (int)std::lround((t - k) * P);
        const float* h = &table[(size_t)ph * (size_t)L];
        const long lo = k - half;
        float acc = 0;
        if (lo >= 0 && lo + L <= n) {
            const float* xp = x.data() + lo;
            for (int i = 0; i < L; ++i) acc += h[i] * xp[i];
        } else {
            for (int i = 0; i < L; ++i) {
                const long j = lo + i;
                if (j >= 0 && j < n) acc += h[i] * x[(size_t)j];
            }
        }
        out[(size_t)m] = acc;
    }
    return out;
}

// Every analysis runs at one fixed working rate, whatever the file's rate: frame lengths, hops, band edges
// and every threshold were tuned there, and the results must not depend on whether a file is 44.1, 48 or
// 96 kHz. Integer decimation first (cheap), then a fractional step when the rate is not a multiple.
constexpr double kWorkRate = 22050.0;

inline std::vector<float> toWorkingRate(const float* x, int n, double sampleRate, float gain) {
    const int dec = std::max(1, (int)std::floor(sampleRate / kWorkRate + 1e-9));
    std::vector<float> y = decimate(x, n, dec, gain);
    const double r1 = sampleRate / dec;
    if (std::fabs(r1 - kWorkRate) > 1e-6 * kWorkRate) y = resample(y, r1, kWorkRate);
    return y;
}

// ---------------------------------------------------------------------------------------------------------
struct OdfData {
    std::vector<float> full, low, mid;  // all bands, < 160 Hz (kick), 160 Hz..3 kHz (snare body, keys, voice)
    int N = 1024, hop = 128;
    double sr = 22050, fr = 172.27;  // working sample rate, frames per second
    double offset = 0;               // seconds to add to frame centre times to land on attack feet
};

// Centred frames: frame t covers [t*hop - N/2, t*hop + N/2). 46 ms frames, 5.8 ms hop at 22.05 kHz.
inline OdfData computeOdf(const std::vector<float>& y, double sr) {
    OdfData d;
    d.sr = sr;
    d.N = sr >= 12000 ? 1024 : 512;
    d.hop = d.N / 8;
    d.fr = sr / d.hop;
    const int N = d.N, hop = d.hop, nb = N / 2 + 1;
    const int n = (int)y.size();
    const int F = n / hop + 1;
    d.full.assign((size_t)F, 0.f);
    d.low.assign((size_t)F, 0.f);
    d.mid.assign((size_t)F, 0.f);

    // Bin -> band map, 12 bands per octave from 30 Hz, merged where bins are wider than a band.
    std::vector<int> band((size_t)nb, -1);
    int B = 0;
    std::vector<char> cls;  // 0 low, 1 mid, 2 high
    {
        int last = -1;
        const double fmax = std::min(16000.0, 0.48 * sr);
        for (int k = 1; k < nb; ++k) {
            const double f = k * sr / N;
            if (f < 30.0 || f > fmax) continue;
            const int b = (int)std::floor(12.0 * std::log2(f / 30.0));
            if (b != last) { last = b; ++B; cls.push_back(0); }
            band[(size_t)k] = B - 1;
            cls[(size_t)(B - 1)] = (char)(f < 160.0 ? 0 : (f < 3000.0 ? 1 : 2));
        }
    }
    if (B == 0) return d;

    std::vector<float> win((size_t)N);
    for (int i = 0; i < N; ++i) win[(size_t)i] = (float)(0.5 - 0.5 * std::cos(2.0 * kPi * i / N));
    RealFFT fft(N);
    std::vector<float> buf((size_t)N), mag((size_t)nb);
    std::vector<float> ring((size_t)(3 * B), 0.f);  // last three frames of log band energies
    std::vector<float> bs((size_t)B);
    const int lag = 2;  // compare with the frame 11.6 ms earlier

    for (int t = 0; t < F; ++t) {
        const int start = t * hop - N / 2;
        if (start >= 0 && start + N <= n) {
            const float* yp = y.data() + start;
            for (int i = 0; i < N; ++i) buf[(size_t)i] = yp[i] * win[(size_t)i];
        } else {
            for (int i = 0; i < N; ++i) {
                const int j = start + i;
                buf[(size_t)i] = (j >= 0 && j < n) ? y[(size_t)j] * win[(size_t)i] : 0.f;
            }
        }
        fft.magnitudes(buf.data(), mag.data());
        std::fill(bs.begin(), bs.end(), 0.f);
        for (int k = 1; k < nb; ++k) {
            const int b = band[(size_t)k];
            if (b >= 0) bs[(size_t)b] += mag[(size_t)k];
        }
        float* cur = ring.data() + (size_t)((t % 3) * B);
        for (int b = 0; b < B; ++b) cur[b] = std::log10(1.f + bs[(size_t)b]);
        float acc[3] = {0, 0, 0};
        if (t >= lag) {
            const float* prev = ring.data() + (size_t)(((t - lag) % 3) * B);
            for (int b = 0; b < B; ++b) {
                float pm = prev[b];
                if (b > 0) pm = std::max(pm, prev[b - 1]);
                if (b + 1 < B) pm = std::max(pm, prev[b + 1]);
                const float diff = cur[b] - pm;
                if (diff > 0) acc[(int)cls[(size_t)b]] += diff;
            }
        } else {
            for (int b = 0; b < B; ++b) acc[(int)cls[(size_t)b]] += cur[b];  // silence before the file
        }
        d.full[(size_t)t] = acc[0] + acc[1] + acc[2];
        d.low[(size_t)t] = acc[0];
        d.mid[(size_t)t] = acc[1];
    }
    return d;
}

// Mean of the top `frac` of values (a robust "loud hit" level).
inline float topMean(const std::vector<float>& v, double frac) {
    if (v.empty()) return 0.f;
    std::vector<float> c(v);
    const size_t k = std::max<size_t>(1, (size_t)(c.size() * frac));
    std::nth_element(c.begin(), c.begin() + (ptrdiff_t)(c.size() - k), c.end());
    double s = 0;
    for (size_t i = c.size() - k; i < c.size(); ++i) s += c[i];
    return (float)(s / k);
}

// Time-domain refinement: find the foot of the attack in [lo, hi] seconds. Returns -1 when no clear rise.
inline double refineOnset(const std::vector<float>& y, double sr, double lo, double hi, float* ratioOut = nullptr) {
    const int n = (int)y.size();
    const int blk = std::max(1, (int)std::lround(sr * 0.0005));  // 0.5 ms blocks
    const int W = 40;                                             // 20 ms trailing peak window (> one 50 Hz cycle)
    long s0 = (long)std::floor(lo * sr) - (long)(2 * W * blk);
    long s1 = (long)std::ceil(hi * sr) + blk;
    if (s0 < 1) s0 = 1;
    if (s1 > n) s1 = n;
    const int nbk = (int)((s1 - s0) / blk);
    if (nbk < 2 * W + 2) return -1;
    std::vector<float> bA((size_t)nbk), bD((size_t)nbk);
    for (int k = 0; k < nbk; ++k) {
        float ma = 0, md = 0;
        const long b0 = s0 + (long)k * blk;
        for (long i = b0; i < b0 + blk; ++i) {
            ma = std::max(ma, std::fabs(y[(size_t)i]));
            md = std::max(md, std::fabs(y[(size_t)i] - y[(size_t)(i - 1)]));
        }
        bA[(size_t)k] = ma;
        bD[(size_t)k] = md;
    }
    auto trailing = [&](const std::vector<float>& b) {
        std::vector<float> e((size_t)nbk, 0.f);
        for (int k = 0; k < nbk; ++k) {
            float m = 0;
            for (int j = std::max(0, k - W + 1); j <= k; ++j) m = std::max(m, b[(size_t)j]);
            e[(size_t)k] = m;
        }
        return e;
    };
    const std::vector<float> eA = trailing(bA), eD = trailing(bD);
    const int kLo = std::max(2 * W - 1, (int)std::floor((lo * sr - s0) / blk));
    const int kHi = std::min(nbk - 1, (int)std::ceil((hi * sr - s0) / blk));
    if (kHi <= kLo) return -1;

    // The window can hold more than one attack (the analysis frame is 46 ms wide), so take the earliest
    // rise that is at least half as clear (in ratio terms) as the clearest one, at its local top.
    struct Best { int k = -1; float rise = 0, ratio = 0; };
    auto findRise = [&](const std::vector<float>& e, float floorV) {
        Best b;
        float bestRatio = 0;
        for (int k = kLo; k <= kHi; ++k)
            bestRatio = std::max(bestRatio, e[(size_t)k] / (e[(size_t)(k - W)] + floorV));
        if (bestRatio <= 0) return b;
        const float need = std::max(1.4f, 0.5f * bestRatio);
        for (int k = kLo; k <= kHi; ++k) {
            const float ratio = e[(size_t)k] / (e[(size_t)(k - W)] + floorV);
            if (ratio < need) continue;
            // Climb to the top of this rise (rise keeps growing while the attack continues).
            int kk = k;
            while (kk + 1 <= kHi && e[(size_t)(kk + 1)] - e[(size_t)(kk + 1 - W)] >= e[(size_t)kk] - e[(size_t)(kk - W)]) ++kk;
            b.k = kk;
            b.rise = e[(size_t)kk] - e[(size_t)(kk - W)];
            b.ratio = e[(size_t)kk] / (e[(size_t)(kk - W)] + floorV);
            return b;
        }
        return b;
    };
    const Best ba = findRise(eA, 1e-4f), bd = findRise(eD, 1e-4f);
    const bool useD = bd.ratio > ba.ratio * 1.15f;
    const Best& b = useD ? bd : ba;
    if (ratioOut) *ratioOut = b.ratio;
    if (b.k < 0 || b.ratio < 1.4f) return -1;
    const std::vector<float>& e = useD ? eD : eA;
    const float base = e[(size_t)(b.k - W)], top = e[(size_t)b.k];
    const float thr = base + 0.1f * (top - base);
    int j = b.k;
    for (int q = b.k - W + 1; q <= b.k; ++q) if (e[(size_t)q] >= thr) { j = q; break; }
    // Sample-accurate position inside block j.
    const long b0 = s0 + (long)j * blk;
    for (long i = b0; i < b0 + blk && i < n; ++i) {
        const float v = useD ? std::fabs(y[(size_t)i] - y[(size_t)(i - 1)]) : std::fabs(y[(size_t)i]);
        if (v >= thr) return (double)i / sr;
    }
    return (double)b0 / sr;
}

struct Onsets { std::vector<double> t; std::vector<float> s; std::vector<int> frame; };

inline Onsets detectOnsets(const OdfData& d, const std::vector<float>& y) {
    Onsets o;
    const std::vector<float>& f = d.full;
    const int F = (int)f.size();
    if (F < 3) return o;
    const float scale = topMean(f, 0.01);
    if (!(scale > 1e-6f)) return o;
    const double fr = d.fr;
    const int preMax = std::max(1, (int)std::lround(0.03 * fr)), postMax = std::max(1, (int)std::lround(0.03 * fr));
    const int preAvg = std::max(1, (int)std::lround(0.10 * fr)), postAvg = std::max(1, (int)std::lround(0.07 * fr));
    const int wait = std::max(1, (int)std::lround(0.03 * fr));

    std::vector<double> pre((size_t)F + 1, 0.0);
    for (int t = 0; t < F; ++t) pre[(size_t)t + 1] = pre[(size_t)t] + f[(size_t)t];
    // Local level: maximum over ~0.5 s blocks, then over +-3 blocks (about +-1.5 s).
    const int blkLen = std::max(1, (int)std::lround(0.5 * fr));
    const int nBlk = (F + blkLen - 1) / blkLen;
    std::vector<float> bmax((size_t)nBlk, 0.f);
    for (int t = 0; t < F; ++t) bmax[(size_t)(t / blkLen)] = std::max(bmax[(size_t)(t / blkLen)], f[(size_t)t]);

    const int medHalf = std::max(2, (int)std::lround(0.06 * fr));
    std::vector<float> medBuf;
    std::vector<int> frames;
    std::vector<float> vals;
    int lastT = -1000000;
    for (int t = 1; t < F; ++t) {
        const float v = f[(size_t)t];
        if (v < 0.02f * scale) continue;
        bool isMax = true;
        for (int j = std::max(0, t - preMax); j < t && isMax; ++j) if (f[(size_t)j] >= v) isMax = false;
        for (int j = t + 1; j <= std::min(F - 1, t + postMax) && isMax; ++j) if (f[(size_t)j] > v) isMax = false;
        if (!isMax) continue;
        const int a0 = std::max(0, t - preAvg), a1 = std::min(F - 1, t + postAvg);
        const double avg = (pre[(size_t)a1 + 1] - pre[(size_t)a0]) / (a1 - a0 + 1);
        const int bi = t / blkLen;
        float loc = 0;
        for (int q = std::max(0, bi - 3); q <= std::min(nBlk - 1, bi + 3); ++q) loc = std::max(loc, bmax[(size_t)q]);
        // Contrast against the local median (robust to neighbouring hits, rejects sustained-note wobble)
        // plus a floor tied to both the file's loud hits and the surrounding +-1.5 s.
        const int m0 = std::max(0, t - medHalf), m1 = std::min(F - 1, t + medHalf);
        medBuf.assign(f.begin() + m0, f.begin() + m1 + 1);
        std::nth_element(medBuf.begin(), medBuf.begin() + (ptrdiff_t)(medBuf.size() / 2), medBuf.end());
        const double med = medBuf[medBuf.size() / 2];
        const double delta = std::max(0.01 * scale, 0.01 * loc);
        if (v < 2.0 * med + delta || v < avg + 0.5 * delta) continue;
        if (t - lastT < wait) {
            if (!vals.empty() && v > vals.back()) { frames.back() = t; vals.back() = v; lastT = t; }
            continue;
        }
        frames.push_back(t);
        vals.push_back(v);
        lastT = t;
    }

    // Refine each onset in the time domain.
    const double halfFrame = 0.5 * d.N / d.sr;
    std::vector<double> times(frames.size());
    std::vector<char> refined(frames.size(), 0);
    std::vector<float> rise(frames.size(), 0.f);
    std::vector<double> offs;
    double prevT = -1.0;
    for (size_t i = 0; i < frames.size(); ++i) {
        const double tc = frames[i] * (double)d.hop / d.sr;
        double lo = tc - halfFrame - 0.008, hi = tc + halfFrame + 0.005;
        if (prevT >= 0) lo = std::max(lo, prevT + 0.015);
        float ratio = 0;
        const double r = (hi > lo) ? refineOnset(y, d.sr, lo, hi, &ratio) : -1.0;
        rise[i] = ratio;
        if (r >= 0) { times[i] = r; refined[i] = 1; offs.push_back(r - tc); }
        else times[i] = -1;
        if (times[i] >= 0) prevT = times[i];
    }
    double off = -0.25 * d.N / d.sr;
    if (!offs.empty()) {
        std::nth_element(offs.begin(), offs.begin() + (ptrdiff_t)(offs.size() / 2), offs.end());
        off = offs[offs.size() / 2];
    }
    for (size_t i = 0; i < frames.size(); ++i)
        if (!refined[i]) times[i] = std::max(0.0, frames[i] * (double)d.hop / d.sr + off);

    // Sort and merge onsets that collapsed onto each other (keep the stronger).
    std::vector<size_t> idx(frames.size());
    std::iota(idx.begin(), idx.end(), 0);
    std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return times[a] < times[b]; });
    for (size_t q : idx) {
        const float s = clamp01(vals[q] / scale);
        // Weak candidates with no attack in the waveform are spectral wobble (beating, vibrato), not onsets.
        if (s < 0.15f && rise[q] < 2.0f) continue;
        if (!o.t.empty() && times[q] - o.t.back() < 0.025) {
            if (s > o.s.back()) { o.t.back() = times[q]; o.s.back() = s; o.frame.back() = frames[q]; }
            continue;
        }
        o.t.push_back(times[q]);
        o.s.push_back(s);
        o.frame.push_back(frames[q]);
    }
    return o;
}

// ---------------------------------------------------------------------------------------------------------
// Tempo helpers.

// ODF value at a time (seconds), taking the max over +-r frames to tolerate jitter.
inline float odfAt(const std::vector<float>& o, const OdfData& d, double tSec, int r) {
    const int c = (int)std::lround((tSec - d.offset) * d.fr);
    float m = 0;
    for (int j = c - r; j <= c + r; ++j)
        if (j >= 0 && j < (int)o.size()) m = std::max(m, o[(size_t)j]);
    return m;
}

struct GridFit { double period = 0, phase = 0; int used = 0; };

// Weighted least squares t = a + P * n over onsets inside [t0, t1] that lie within tol*P of the grid.
inline GridFit regressGrid(const Onsets& on, double P, double a, double t0, double t1, double tol) {
    GridFit g{P, a, 0};
    const double c = 0.5 * (t0 + t1);
    const long n0 = std::lround((c - a) / P);
    double sw = 0, sn = 0, st = 0, snn = 0, snt = 0;
    long nmin = std::numeric_limits<long>::max(), nmax = std::numeric_limits<long>::min();
    int cnt = 0;
    for (size_t i = 0; i < on.t.size(); ++i) {
        const double t = on.t[i];
        if (t < t0 || t > t1) continue;
        const long n = std::lround((t - a) / P);
        const double dev = t - (a + n * P);
        if (std::fabs(dev) > tol * P) continue;
        const double w = 0.05 + on.s[i];
        const double x = (double)(n - n0);
        sw += w; sn += w * x; st += w * t; snn += w * x * x; snt += w * x * t;
        nmin = std::min(nmin, n); nmax = std::max(nmax, n);
        ++cnt;
    }
    if (cnt < 4 || nmax - nmin < 3) return g;
    const double den = sw * snn - sn * sn;
    if (std::fabs(den) < 1e-12) return g;
    const double Pn = (sw * snt - sn * st) / den;
    const double an = (st - Pn * sn) / sw;  // time of beat n0
    if (!(Pn > P * 0.97 && Pn < P * 1.03)) return g;
    g.period = Pn;
    g.phase = an - n0 * Pn;
    g.used = cnt;
    return g;
}

// Best phase (seconds) of a grid with period P over the ODF frames of [t0, t1].
inline double fitPhase(const OdfData& d, double P, double t0, double t1) {
    const double Pf = P * d.fr;
    const int steps = std::max(1, (int)std::ceil(Pf));
    double best = -1, bestPh = 0;
    for (int s = 0; s < steps; ++s) {
        const double ph = (double)s / d.fr + d.offset;  // candidate beat time modulo P
        double sc = 0;
        int cnt = 0;
        const long n0 = (long)std::ceil((t0 - ph) / P);
        for (long n = n0;; ++n) {
            const double t = ph + n * P;
            if (t > t1) break;
            sc += odfAt(d.full, d, t, 1);
            ++cnt;
        }
        if (cnt > 0) sc /= cnt;
        if (sc > best) { best = sc; bestPh = ph; }
    }
    return bestPh;
}

// How well a file of `len` seconds suits a tempo family as a trimmed loop: the tempo that makes the file
// a whole number of 4/4 bars (1, 2, 4, 8, ... bars score 1; other even counts 0.6, odd 0.4) must lie within
// 2 % of an octave of `bpm`. 0 when no bar count fits or the file is too long for its length to say
// anything. Trimmed loops are the common case in a sampler, and their length is the one cue that separates
// 128 from 96 or 85.3 when the rhythm is syncopated (3-3-2 patterns) or absent (pads).
inline double loopFit(double bpm, double len) {
    if (len < 1.0 || len > 70.0 || !(bpm > 0)) return 0;
    double best = 0;
    for (int bars = 1; bars <= 64; ++bars) {
        const double T = 240.0 * bars / len;
        if (T > 250.0) break;
        if (T < 40.0) continue;
        double r = std::log2(T / bpm);
        r -= std::round(r);  // octave equivalent
        const double dev = std::fabs(std::exp2(r) - 1.0);
        if (dev > 0.02) continue;
        const double shape = (bars & (bars - 1)) == 0 ? 1.0 : (bars % 2 == 0 ? 0.6 : 0.4);
        best = std::max(best, shape * (1.0 - 0.5 * dev / 0.02));
    }
    return best;
}

// Features of one tempo family (all octaves of a base tempo in 60..120 BPM), exposed for offline tuning.
struct TempoFamily {
    double base = 0;   // BPM in [60, 120)
    double P = 0, a = 0;  // refined beat period / phase (seconds) of the base
    double grid = 0;   // chance-corrected share of onset strength on its straight 8th grid
    double comb = 0;   // ACF comb strength relative to the overall winner
    double loop = 0;   // loopFit of the best octave member
    double bar = 0;    // ACF at one and two bars of the member in 80..160 BPM
    double prior = 0;  // 1 for the comb winner, 0 otherwise
    double score = 0;
};

struct TempoInfo {
    double bpm = 0, firstBeat = 0;
    float conf = 0;
    std::vector<TempoCandidate> cands;
};

// Chance-corrected share of onset strength that sits on the straight 8th-note grid of period P (seconds),
// best phase, onsets restricted to [t0, t1]. Used to pick between tempo families (e.g. 90 vs 120, or 128 vs
// 170.7). Swung or triplet positions are deliberately not counted: they would let a 3:4-related grid claim
// the real beats.
inline double gridScore(const Onsets& on, double P, double t0, double t1) {
    const double tol = std::max(0.02, 0.05 * P);
    double tot = 0;
    for (size_t i = 0; i < on.t.size(); ++i) if (on.t[i] >= t0 && on.t[i] <= t1) tot += on.s[i];
    if (tot <= 0) return 0;
    const double chance = std::min(0.95, 2.0 * 2.0 * tol / P);
    double best = 0;
    const int steps = std::max(8, (int)std::ceil(P / 0.005));
    for (int k = 0; k < steps; ++k) {
        const double ph = P * k / steps;
        double hit = 0;
        for (size_t i = 0; i < on.t.size(); ++i) {
            const double t = on.t[i];
            if (t < t0 || t > t1) continue;
            const double x = (t - ph) / P;
            const double f = (x - std::floor(x)) * P;
            const double dist = std::min({f, P - f, std::fabs(f - 0.5 * P)});
            if (dist <= tol) hit += on.s[i];
        }
        best = std::max(best, hit / tot);
    }
    return (best - chance) / (1.0 - chance);
}

inline TempoInfo estimateTempo(const OdfData& d, const Onsets& on, double lengthSec, double firstSound,
                               std::vector<TempoFamily>* familiesOut = nullptr) {
    TempoInfo ti;
    const int F = (int)d.full.size();
    if (F < 16 || on.t.size() < 4) return ti;
    const double fr = d.fr;

    // 1. Detrended, lightly smoothed ODF.
    std::vector<float> o((size_t)F, 0.f);
    {
        std::vector<float> sm((size_t)F);
        for (int t = 0; t < F; ++t) {
            const float a = d.full[(size_t)std::max(0, t - 1)], b = d.full[(size_t)t], c = d.full[(size_t)std::min(F - 1, t + 1)];
            sm[(size_t)t] = 0.25f * a + 0.5f * b + 0.25f * c;
        }
        const int w = std::max(1, (int)std::lround(0.4 * fr));
        std::vector<double> pre((size_t)F + 1, 0.0);
        for (int t = 0; t < F; ++t) pre[(size_t)t + 1] = pre[(size_t)t] + sm[(size_t)t];
        for (int t = 0; t < F; ++t) {
            const int a0 = std::max(0, t - w), a1 = std::min(F - 1, t + w);
            const double m = (pre[(size_t)a1 + 1] - pre[(size_t)a0]) / (a1 - a0 + 1);
            o[(size_t)t] = (float)std::max(0.0, sm[(size_t)t] - m);
        }
    }
    // 2. Autocorrelation up to 4 periods of 50 BPM (and two bars at 80 BPM), comb filtered over 4 harmonics.
    const int maxLag = std::min(F - 2, (int)std::ceil(fr * 6.0) + 2);
    if (maxLag < 8) return ti;
    std::vector<double> acf((size_t)maxLag + 1, 0.0);
    for (int l = 0; l <= maxLag; ++l) {
        const int len = F - l;
        const float* a = o.data();
        const float* b = o.data() + l;
        float s0 = 0, s1 = 0, s2 = 0, s3 = 0, s4 = 0, s5 = 0, s6 = 0, s7 = 0;
        int i = 0;
        for (; i + 8 <= len; i += 8) {
            s0 += a[i] * b[i]; s1 += a[i + 1] * b[i + 1]; s2 += a[i + 2] * b[i + 2]; s3 += a[i + 3] * b[i + 3];
            s4 += a[i + 4] * b[i + 4]; s5 += a[i + 5] * b[i + 5]; s6 += a[i + 6] * b[i + 6]; s7 += a[i + 7] * b[i + 7];
        }
        double s = (double)s0 + s1 + s2 + s3 + s4 + s5 + s6 + s7;
        for (; i < len; ++i) s += (double)a[i] * b[i];
        acf[(size_t)l] = s / len;
    }
    if (!(acf[0] > 0)) return ti;
    const double acf0 = acf[0];
    for (auto& v : acf) v /= acf0;
    auto A = [&](double lag, double tolFrames) {
        double m = 0;
        const int lo = (int)std::floor(lag - tolFrames), hi = (int)std::ceil(lag + tolFrames);
        for (int j = lo; j <= hi; ++j)
            if (j >= 1 && j <= maxLag) m = std::max(m, acf[(size_t)j]);
        return m;
    };
    auto comb = [&](double bpm) {
        const double L = fr * 60.0 / bpm;
        double s = 0;
        for (int k = 1; k <= 4; ++k) s += A(k * L, 0.5 * k);
        return s / 4.0;
    };
    auto prior = [](double T) { const double lg = std::log2(T / 120.0); return std::exp(-0.5 * lg * lg); };
    double bestS = -1, T0 = 120;
    for (double T = 50; T <= 220.0001; T += 0.5) {
        const double s = comb(T) * prior(T);
        if (s > bestS) { bestS = s; T0 = T; }
    }
    for (double T = T0 - 0.5; T <= T0 + 0.5; T += 0.02) {
        const double s = comb(T) * prior(T);
        if (s > bestS) { bestS = s; T0 = T; }
    }
    auto foldBase = [](double T) { while (T >= 120.0) T *= 0.5; while (T < 60.0) T *= 2.0; return T; };
    auto familyComb = [&](double base) {  // best comb over the octave family members in 50..220
        double m = 0;
        for (double T = base / 2; T <= 220.0; T *= 2.0) if (T >= 50.0) m = std::max(m, comb(T));
        return m;
    };

    // Densest 12 s region of onsets as the anchor for local fits.
    double c = 0.5 * lengthSec;
    {
        double bestW = -1, sum = 0;
        size_t j = 0;
        for (size_t i = 0; i < on.t.size(); ++i) {
            sum += on.s[i];
            while (on.t[i] - on.t[j] > 12.0) { sum -= on.s[j]; ++j; }
            if (sum > bestW) { bestW = sum; c = 0.5 * (on.t[i] + on.t[j]); }
        }
    }

    // Period/phase refinement for a base tempo: local ODF phase fit at the dense anchor, then weighted
    // regression of onset times on beat indices over a window that doubles until it spans the file.
    auto refine = [&](double baseBpm, double& Pout, double& aout) {
        double P = 60.0 / baseBpm;
        double h = 6.0;
        double a = fitPhase(d, P, c - h, c + h);
        for (int guard = 0; guard < 32; ++guard) {
            const double t0 = c - h, t1 = c + h;
            GridFit g = regressGrid(on, P, a, t0, t1, 0.12);
            if (g.used > 0) { P = g.period; a = g.phase; }
            if (t0 <= 0 && t1 >= lengthSec) {
                for (int it = 0; it < 2; ++it) {
                    g = regressGrid(on, P, a, -1.0, lengthSec + 1.0, 0.10);
                    if (g.used > 0) { P = g.period; a = g.phase; }
                }
                break;
            }
            h *= 2.0;
        }
        Pout = P;
        aout = a;
    };

    // 3. Tempo family competition: the comb winner against its 3:2 / 4:3 relatives and (for short files)
    //    the loop-length family. Octaves are one family here (4:3 and 2:3 fold together); the octave is
    //    decided in step 4. Each family is refined on its own and judged on onset strength on its straight
    //    8th grid, the ACF at one and two bars (patterns repeat per bar, not per 3 beats) and how well the
    //    file length holds a whole number of bars (trimmed loops). Families need real ACF comb support or a
    //    loop-length fit to compete. (Kick + snare band level on each family's beats was also tried and
    //    did not help on the evaluation set.)
    double P = 60.0 / foldBase(T0), a = 0;
    std::vector<TempoFamily> fams;
    size_t bestFam = 0;
    {
        const double b0 = foldBase(T0);
        std::vector<double> bases = {b0};
        for (double r : {4.0 / 3.0, 3.0 / 2.0}) bases.push_back(foldBase(T0 * r));
        if (lengthSec >= 2.0 && lengthSec <= 70.0) {
            // The loop-length family: the whole-bar count whose tempo has the best comb support.
            double bestLc = -1, lb = 0;
            for (int bars : {1, 2, 4, 8, 16, 32}) {
                const double T = 240.0 * bars / lengthSec;
                if (T < 50.0 || T > 220.0) continue;
                const double s = comb(T);
                if (s > bestLc) { bestLc = s; lb = T; }
            }
            if (lb > 0) bases.push_back(foldBase(lb));
        }
        const double c0 = familyComb(b0);
        for (double base : bases) {
            bool dup = false;
            for (const TempoFamily& f : fams) if (std::fabs(f.base / base - 1.0) < 0.015) dup = true;
            if (dup) continue;
            TempoFamily f;
            f.base = base;
            f.prior = fams.empty() ? 1.0 : 0.0;
            f.comb = familyComb(base) / std::max(1e-9, c0);
            refine(base, f.P, f.a);
            f.grid = gridScore(on, f.P, c - 10.0, c + 10.0);
            // Only a file that starts on a beat of this grid can be a trimmed loop at this tempo (an
            // excerpt cut anywhere would otherwise fit some bar count by chance).
            {
                double ph = std::fmod(f.a, f.P);
                if (ph < 0) ph += f.P;
                const double dph = std::min(ph, f.P - ph), tolPh = std::max(0.012, 0.03 * f.P);
                f.loop = loopFit(60.0 / f.P, lengthSec) * std::exp(-0.5 * (dph / tolPh) * (dph / tolPh));
            }
            double Tm = base;
            while (Tm < 80.0) Tm *= 2.0;
            const double L = fr * 60.0 / Tm;
            f.bar = 4.0 * L + 1 <= maxLag ? A(4.0 * L, 1.0) : 0.0;
            if (8.0 * L + 1 <= maxLag) f.bar = 0.5 * (f.bar + A(8.0 * L, 1.5));
            fams.push_back(f);
        }
        double bestScore = -1e9;
        for (size_t i = 0; i < fams.size(); ++i) {
            TempoFamily& f = fams[i];
            // Weights fitted on the development half of tests/eval, jointly on trimmed loops and on random
            // 5..20 s excerpts of the same material (where the length says nothing and must not mislead).
            f.score = 0.25 * f.grid + 0.03 * f.prior + 0.08 * f.loop + 0.1 * f.bar;
            const bool supported = f.comb >= 0.7 || f.loop > 0.3;
            if (!supported && i != 0) continue;
            if (f.score > bestScore) { bestScore = f.score; bestFam = i; }
        }
        P = fams[bestFam].P;
        a = fams[bestFam].a;
        if (familiesOut) *familiesOut = fams;
    }

    // 3b. Polish: the onset regression can be pulled by off-grid vocal/fill onsets in dense mixes, so finish
    //     with a direct search over +-0.6 % period (0.01 % steps) and a small phase range that maximises the
    //     ODF on every beat of the file. The grid is pivoted at the anchor so period and phase decouple.
    {
        std::vector<float> sm((size_t)F);
        for (int t = 0; t < F; ++t) {
            float m = d.full[(size_t)t];
            if (t > 0) m = std::max(m, d.full[(size_t)t - 1]);
            if (t + 1 < F) m = std::max(m, d.full[(size_t)t + 1]);
            sm[(size_t)t] = m;
        }
        auto at = [&](double tSec) {
            const double x = (tSec - d.offset) * fr;
            const int i = (int)std::floor(x);
            if (i < 0 || i + 1 >= F) return 0.0;
            const double w = x - i;
            return (1.0 - w) * sm[(size_t)i] + w * sm[(size_t)i + 1];
        };
        const double pivot = a + std::round((c - a) / P) * P;
        double polishBest = -1, bestP = P, bestPivot = pivot;
        const double phRange = 0.06 * P, phStep = 0.5 / fr;
        for (int k = -60; k <= 60; ++k) {
            const double Pq = P * (1.0 + 0.0001 * k);
            const long nLo = (long)std::ceil((0.0 - pivot) / Pq), nHi = (long)std::floor((lengthSec - pivot) / Pq);
            for (double dph = -phRange; dph <= phRange + 1e-12; dph += phStep) {
                double sc = 0;
                for (long n = nLo; n <= nHi; ++n) sc += at(pivot + dph + n * Pq);
                if (sc > polishBest) { polishBest = sc; bestP = Pq; bestPivot = pivot + dph; }
            }
        }
        P = bestP;
        a = bestPivot;
        // Final tight regression on attack-foot times for millisecond phase (kept only if it agrees).
        const GridFit g = regressGrid(on, P, a, -1.0, lengthSec + 1.0, 0.04);
        if (g.used > 0 && std::fabs(g.period / P - 1.0) < 0.001) { P = g.period; a = g.phase; }
    }

    // 4. Octave: below ~80 BPM take the double. From 80 to 100 BPM double only when the half-beat is a
    //    real pulse: either kick+snare energy on half-beats matches the beats, or the snare-band hits every
    //    base beat on the half-beat of the kick (a backbeat at the double tempo, e.g. drum and bass).
    auto sumAt = [&](const std::vector<float>& band, double Pq, double aq, double frac, int period, int which) {
        double s = 0;
        const long n0 = (long)std::ceil((0.0 - aq) / Pq);
        for (long n = n0;; ++n) {
            const double t = aq + (n + frac) * Pq;
            if (t > lengthSec) break;
            if (period > 1 && (((n % period) + period) % period) != which) continue;
            s += odfAt(band, d, t, 2);
        }
        return s;
    };
    double bpm = 60.0 / P;
    double octScore = 0.5;  // how plausible the other octave is (0..1), for the candidate list
    if (bpm * 2.0 <= 200.5) {
        bool dbl = bpm < 79.5;
        if (dbl) octScore = 0.6;
        if (!dbl) {
            const double onLM = sumAt(d.low, P, a, 0.0, 1, 0) + sumAt(d.mid, P, a, 0.0, 1, 0);
            const double offLM = sumAt(d.low, P, a, 0.5, 1, 0) + sumAt(d.mid, P, a, 0.5, 1, 0);
            const double rs = std::min(onLM, offLM) / std::max(1e-9, std::max(onLM, offLM));
            // Snare band on the four half-beat slots of a two-beat cycle.
            double M[4], L[4];
            for (int q = 0; q < 4; ++q) {
                M[q] = sumAt(d.mid, P, a, 0.5 * (q % 2), 2, q / 2);
                L[q] = sumAt(d.low, P, a, 0.5 * (q % 2), 2, q / 2);
            }
            // Slots 0,2 are beats, 1,3 are half-beats.
            const int sCls = (M[1] + M[3] > M[0] + M[2]) ? 1 : 0;
            const int kCls = (L[1] + L[3] > L[0] + L[2]) ? 1 : 0;
            const double s1 = M[sCls], s2 = M[sCls + 2];
            const double alt = std::min(s1, s2) / std::max(1e-9, std::max(s1, s2));
            const double mTot = M[0] + M[1] + M[2] + M[3];
            const double snareShare = mTot > 0 ? (s1 + s2) / mTot : 0.0;
            const bool backbeatAtDouble = sCls != kCls && alt > 0.6 && snareShare > 0.6;
            dbl = rs >= 0.85 || backbeatAtDouble;
            octScore = dbl ? 0.6 : std::min(0.9, 0.3 + 0.6 * rs);
        }
        if (dbl) P *= 0.5;
    }
    bpm = 60.0 / P;

    // 5. Confidence: share of onset strength on the 16th / triplet grid, beyond what random onsets
    //    would score, times a ramp on the number of on-beat onsets.
    {
        const double tol = std::max(0.02, 0.03 * P);
        static const double kSub[6] = {0.0, 0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0, 0.75};
        double tot = 0, hit = 0;
        int onBeat = 0;
        for (size_t i = 0; i < on.t.size(); ++i) {
            const double x = (on.t[i] - a) / P;
            const double ph = (x - std::floor(x)) * P;
            double dist = P - ph;
            for (double sd : kSub) dist = std::min(dist, std::fabs(ph - sd * P));
            tot += on.s[i];
            if (dist <= tol) { hit += on.s[i]; if (std::min(ph, P - ph) <= tol) ++onBeat; }
        }
        const double chance = std::min(0.95, 6.0 * 2.0 * tol / P);
        double conf = tot > 0 ? (hit / tot - chance) / (1.0 - chance) : 0.0;
        conf = std::max(0.0, std::min(1.0, conf * 1.25));
        conf *= std::min(1.0, onBeat / 8.0);
        // A 3:2 or 4:3 related family that scored nearly as well makes the choice a coin toss, however
        // cleanly the onsets sit on either grid.
        double second = -1e9;
        for (size_t i = 0; i < fams.size(); ++i)
            if (i != bestFam && (fams[i].comb >= 0.7 || fams[i].loop > 0.3)) second = std::max(second, fams[i].score);
        if (second > -1e9) {
            const double margin = fams[bestFam].score - second;
            conf *= std::max(0.2, std::min(1.0, 0.2 + 0.8 * margin / 0.15));
        }
        ti.conf = (float)conf;
    }

    // Candidates: the winner, its other octave, then the other families at the octave nearest the winner.
    {
        auto add = [&](double T, double s) {
            if (!(T >= 40.0 && T <= 240.0)) return;
            for (const TempoCandidate& q : ti.cands) if (std::fabs(q.bpm / T - 1.0) < 0.03) return;
            ti.cands.push_back({T, (float)std::max(0.0, std::min(1.0, s))});
        };
        add(bpm, 1.0);
        add(bpm < 120.0 ? bpm * 2.0 : bpm * 0.5, octScore);
        const double sb = std::max(1e-6, fams[bestFam].score);
        for (size_t i = 0; i < fams.size(); ++i) {
            if (i == bestFam) continue;
            // The octave of this family nearest the winner, kept inside 60..200 BPM.
            double T = 60.0 / fams[i].P;
            while (T * 2.0 < bpm * 1.4142 && T * 2.0 <= 200.5) T *= 2.0;
            while (T > bpm * 1.4142 && T * 0.5 >= 59.5) T *= 0.5;
            while (T > 200.5) T *= 0.5;
            const double s = std::max(0.0, fams[i].score) / sb;
            if (s >= 0.15) add(T, 0.95 * s);
        }
        std::stable_sort(ti.cands.begin() + 1, ti.cands.end(), [](const TempoCandidate& x, const TempoCandidate& y) { return x.score > y.score; });
        if (ti.cands.size() > 4) ti.cands.resize(4);
    }

    // 6. Downbeat: the beat class (mod 4) with kick but without snare, nudged towards the class of the
    //    first strong onset (loops and most songs start on the one).
    double K[4] = {0, 0, 0, 0}, S[4] = {0, 0, 0, 0}, Fu[4] = {0, 0, 0, 0};
    for (int m = 0; m < 4; ++m) {
        K[m] = sumAt(d.low, P, a, 0.0, 4, m);
        S[m] = sumAt(d.mid, P, a, 0.0, 4, m);
        Fu[m] = sumAt(d.full, P, a, 0.0, 4, m);
    }
    const double kMax = std::max({K[0], K[1], K[2], K[3], 1e-9});
    const double sMax = std::max({S[0], S[1], S[2], S[3], 1e-9});
    const double fMax = std::max({Fu[0], Fu[1], Fu[2], Fu[3], 1e-9});
    double score[4];
    for (int m = 0; m < 4; ++m) score[m] = K[m] / kMax - 0.5 * S[m] / sMax + 0.2 * Fu[m] / fMax;
    for (size_t i = 0; i < on.t.size(); ++i) {
        if (on.s[i] < 0.3f) continue;
        const double x = (on.t[i] - a) / P;
        const long n = std::lround(x);
        if (std::fabs(x - n) < 0.12) score[((n % 4) + 4) % 4] += 0.3;
        break;
    }
    int m = 0;
    for (int q = 1; q < 4; ++q) if (score[q] > score[m]) m = q;
    const long nStart = (long)std::ceil((0.0 - a) / P);
    double fb = -1;
    for (long n = nStart;; ++n) {
        const double t = a + n * P;
        if (t >= lengthSec) break;
        if ((((n % 4) + 4) % 4) != m) continue;
        if (t >= firstSound - 0.05 && t >= 0) { fb = t; break; }
    }
    if (fb < 0) fb = a - std::floor(a / P) * P;  // shorter than a bar after the first sound
    ti.bpm = bpm;
    ti.firstBeat = std::max(0.0, fb);
    return ti;
}

// ---------------------------------------------------------------------------------------------------------
// Key detection.
//
// Pitch content comes from spectral peaks of 0.74 s frames at ~11 kHz (parabolic interpolation, global
// tuning estimate, square-root compressed so one loud note does not swamp the rest, and peaks that sit on
// the 3rd / 5th / 6th / 7th harmonic of a stronger lower peak attenuated, since they would add a fifth, a
// major third or a minor seventh above every note). Profile correlation alone sees relative major and minor
// as near ties (same scale) and confuses a dorian or mixolydian loop with the key a fifth away, so the
// decision also weighs tonic evidence: the bass (pitch classes below ~250 Hz and the lowest clear note of
// every frame), the chord on every bar (triad template on the bar's chroma with the strong-beat half
// weighted up, plus its bass) and the first and last chord of the file (phrases start and end on the
// tonic). See keyScores for how these are combined.

struct KeyFeatures {
    double chroma[12] = {};  // pitch-class energy of the whole file (tuning corrected), sums to 1
    double bass[12] = {};    // the same below ~250 Hz, sums to 1 (all 0 without bass)
    double lowest[12] = {};  // lowest clear note of every frame, weighted by frame level, sums to 1
    double roots[24] = {};   // chord of every bar (0..11 major triads, 12..23 minor), weighted by level, sums to 1
    double edge[24] = {};    // first and last chord of the file, 0.5 each
    double tuningCents = 0;
    bool valid = false;
};

struct KeyInfo {
    int key = 0;
    bool minor = false;
    float conf = 0;
    double tuningCents = 0;
    std::vector<KeyCandidate> cands;
};

struct SpecPeak { float midi; float w; int frame; };

struct PeakFrames {
    std::vector<SpecPeak> peaks;  // w is relative to the loudest bin of its frame
    std::vector<float> level;     // loudest bin per frame (0 = silent frame)
    double hopSec = 0, frameSec = 0;
};

// Spectral peaks (50 Hz..2.5 kHz) of `y` (the ~22 kHz working signal) in 0.74 s frames, 1/4 overlap.
inline PeakFrames spectralPeaks(const std::vector<float>& y, double sr) {
    PeakFrames pf;
    const int f2 = sr > 16000 ? 2 : 1;
    const std::vector<float> z = decimate(y.data(), (int)y.size(), f2, 1.0f);
    const double sr3 = sr / f2;
    const int N = nextPow2((int)(0.7 * sr3));
    const int hop = N / 4;
    const int n = (int)z.size();
    pf.hopSec = hop / sr3;
    pf.frameSec = N / sr3;
    if (n < N / 4) return pf;
    const int nb = N / 2 + 1;
    const double binHz = sr3 / N;
    const int kMin = std::max(2, (int)std::floor(50.0 / binHz));
    const int kMax = std::min(nb - 3, (int)std::ceil(2500.0 / binHz));
    if (kMax <= kMin + 4) return pf;

    std::vector<float> win((size_t)N);
    for (int i = 0; i < N; ++i) win[(size_t)i] = (float)(0.5 - 0.5 * std::cos(2.0 * kPi * i / N));
    RealFFT fft(N);
    std::vector<float> buf((size_t)N), mag((size_t)nb), lm((size_t)nb);
    std::vector<double> csum((size_t)nb + 1);
    pf.peaks.reserve(1 << 16);
    const int frames = std::max(1, (n - N) / hop + 1);
    pf.level.assign((size_t)frames, 0.f);
    for (int fi = 0; fi < frames; ++fi) {
        const int start = fi * hop;
        for (int i = 0; i < N; ++i) {
            const int j = start + i;
            buf[(size_t)i] = (j < n) ? z[(size_t)j] * win[(size_t)i] : 0.f;
        }
        fft.magnitudes(buf.data(), mag.data());
        float fmax = 0;
        for (int k = kMin; k <= kMax; ++k) fmax = std::max(fmax, mag[(size_t)k]);
        if (fmax < 1e-3f) continue;
        pf.level[(size_t)fi] = fmax;
        csum[0] = 0;
        for (int k = 0; k < nb; ++k) csum[(size_t)k + 1] = csum[(size_t)k] + mag[(size_t)k];
        for (int k = kMin; k <= kMax; ++k) lm[(size_t)k] = std::log(mag[(size_t)k] + 1e-9f);
        for (int k = kMin; k <= kMax; ++k) {
            const float v = mag[(size_t)k];
            if (v < fmax * 0.01f) continue;
            if (!(v > mag[(size_t)k - 1] && v >= mag[(size_t)k + 1] && v > mag[(size_t)k - 2] && v >= mag[(size_t)k + 2])) continue;
            const int a0 = std::max(0, k - 12), a1 = std::min(nb - 1, k + 12);
            const double localMean = (csum[(size_t)a1 + 1] - csum[(size_t)a0]) / (a1 - a0 + 1);
            if (v < 2.0 * localMean) continue;
            const float l0 = (k - 1 >= kMin) ? lm[(size_t)k - 1] : std::log(mag[(size_t)k - 1] + 1e-9f);
            const float l2 = (k + 1 <= kMax) ? lm[(size_t)k + 1] : std::log(mag[(size_t)k + 1] + 1e-9f);
            const float l1 = lm[(size_t)k];
            const float den = l0 - 2 * l1 + l2;
            const float delta = (std::fabs(den) > 1e-9f) ? 0.5f * (l0 - l2) / den : 0.f;
            const double freq = (k + std::max(-0.5f, std::min(0.5f, delta))) * binHz;
            if (freq < 50.0) continue;
            pf.peaks.push_back({(float)(69.0 + 12.0 * std::log2(freq / 440.0)), v / fmax, fi});
        }
    }
    return pf;
}

inline std::vector<float> harmonicAttenuation(const PeakFrames& pf) {
    std::vector<float> att(pf.peaks.size(), 1.f);
    static const float kHarm[4] = {19.0196f, 27.8631f, 31.0196f, 33.6883f};  // semitones up to harmonics 3, 5, 6, 7
    size_t a = 0;
    while (a < pf.peaks.size()) {
        size_t b = a;
        while (b < pf.peaks.size() && pf.peaks[b].frame == pf.peaks[a].frame) ++b;
        // Peaks of a frame are in ascending frequency order, so each lookup is a binary search.
        auto lo = pf.peaks.begin() + (ptrdiff_t)a, hi = pf.peaks.begin() + (ptrdiff_t)b;
        for (size_t i = a; i < b; ++i) {
            float strongest = 0;
            for (float h : kHarm) {
                const float target = pf.peaks[i].midi - h;
                auto it = std::lower_bound(lo, hi, target - 0.35f, [](const SpecPeak& p, float v) { return p.midi < v; });
                for (; it != hi && it->midi < target + 0.35f; ++it) strongest = std::max(strongest, it->w);
            }
            if (strongest > 0) att[i] = (float)(1.0 - 0.8 * std::min(1.0, (double)strongest / pf.peaks[i].w));
        }
        a = b;
    }
    return att;
}

// Key evidence from the tonal signal's peaks and (optionally) a separate bass signal's peaks. `barSec` and
// `barPhase` place the bars (barSec <= 0: fixed 2 s segments).
inline KeyFeatures keyFeatures(const PeakFrames& tonal, const PeakFrames* bassPf, double barSec, double barPhase) {
    KeyFeatures kf;
    if (tonal.peaks.empty()) return kf;
    // Tuning: circular mean of deviations from the equal-tempered grid.
    double cs = 0, sn = 0;
    for (const auto& p : tonal.peaks) {
        const double dev = p.midi - std::round(p.midi);
        cs += p.w * std::cos(2 * kPi * dev);
        sn += p.w * std::sin(2 * kPi * dev);
    }
    const double tuning = std::atan2(sn, cs) / (2 * kPi);  // semitones in (-0.5, 0.5]
    kf.tuningCents = tuning * 100.0;
    const PeakFrames& bp = bassPf && !bassPf->peaks.empty() ? *bassPf : tonal;

    const int frames = (int)tonal.level.size();
    float gl = 0;
    for (float v : tonal.level) gl = std::max(gl, v);
    float gb = 0;
    for (float v : bp.level) gb = std::max(gb, v);
    if (!(gl > 0)) return kf;
    auto frameW = [](const PeakFrames& p, int f, float g) {
        return (f >= 0 && f < (int)p.level.size() && g > 0) ? std::sqrt(p.level[(size_t)f] / g) : 0.f;
    };
    auto pcOf = [&](float midi, double& w) {
        const double m = midi - tuning;
        const double dev = std::fabs(m - std::round(m));
        w *= 1.0 - std::min(1.0, dev / 0.5) * 0.6;  // de-weight peaks between semitones
        return (((int)std::lround(m)) % 12 + 12) % 12;
    };

    // Harmonic suppression: a peak sitting where the 3rd, 5th, 6th or 7th harmonic of a stronger lower
    // peak of the same frame would be is mostly that harmonic (it would add a fifth, a major third or a
    // minor seventh above every note), so it is attenuated in proportion to the lower peak's strength.
    const std::vector<float> hAtt = harmonicAttenuation(tonal);
    std::vector<float> fc((size_t)frames * 12, 0.f), fb((size_t)frames * 12, 0.f);
    for (size_t pi = 0; pi < tonal.peaks.size(); ++pi) {
        const SpecPeak& p = tonal.peaks[pi];
        double w = std::sqrt((double)p.w) * hAtt[pi];  // compressed: one loud note must not swamp the rest
        const int pc = pcOf(p.midi, w);
        kf.chroma[pc] += w;
        fc[(size_t)p.frame * 12 + (size_t)pc] += (float)(w * frameW(tonal, p.frame, gl));
    }
    // Bass: energy below ~250 Hz and the lowest clear note per frame.
    std::vector<float> lowMidi(bp.level.size(), 1e9f);
    for (const auto& p : bp.peaks) {
        if (p.w >= 0.1f && p.midi < 71.5f) lowMidi[(size_t)p.frame] = std::min(lowMidi[(size_t)p.frame], p.midi);
        if (p.midi >= 59.5f) continue;
        double w = p.w;
        const int pc = pcOf(p.midi, w);
        const double fw = frameW(bp, p.frame, gb);
        kf.bass[pc] += w * fw;
        if (p.frame < frames) fb[(size_t)p.frame * 12 + (size_t)pc] += (float)(w * fw);
    }
    for (size_t f = 0; f < lowMidi.size(); ++f) {
        if (lowMidi[f] > 1e8f) continue;
        double w = 1.0;
        const int pc = pcOf(lowMidi[f], w);
        kf.lowest[pc] += w * frameW(bp, (int)f, gb);
    }

    // Chord per segment (bar), strong-beat half weighted up.
    const double seg = barSec > 0.5 ? barSec : 2.0;
    double s0 = barPhase - std::ceil(barPhase / seg) * seg;
    const double centre0 = 0.5 * tonal.frameSec;
    const double lenSec = (frames - 1) * tonal.hopSec + tonal.frameSec;
    struct Seg { int chord; double w; };
    std::vector<Seg> segs;
    for (double s = s0; s < lenSec; s += seg) {
        double sc[12] = {}, sb[12] = {}, lw = 0;
        int cnt = 0;
        const int f0 = std::max(0, (int)std::ceil((s - centre0) / tonal.hopSec));
        for (int f = f0; f < frames; ++f) {
            const double tc = centre0 + f * tonal.hopSec;
            if (tc >= s + seg) break;
            const double g = tc < s + 0.5 * seg ? 1.5 : 1.0;
            for (int q = 0; q < 12; ++q) { sc[q] += g * fc[(size_t)f * 12 + (size_t)q]; sb[q] += fb[(size_t)f * 12 + (size_t)q]; }
            lw += frameW(tonal, f, gl);
            ++cnt;
        }
        double tc = 0, tb = 0;
        for (int q = 0; q < 12; ++q) { tc += sc[q]; tb += sb[q]; }
        if (cnt == 0 || tc <= 0) { segs.push_back({-1, 0.0}); continue; }
        double best = -1e9;
        int bc = -1;
        for (int r = 0; r < 12; ++r)
            for (int q = 0; q < 2; ++q) {
                const int third = q ? 3 : 4, other = q ? 4 : 3;
                const double v = (sc[r] + sc[(r + third) % 12] + sc[(r + 7) % 12] - 0.5 * sc[(r + other) % 12]) / tc +
                                 (tb > 0 ? 0.5 * sb[r] / tb : 0.0);
                if (v > best) { best = v; bc = r + 12 * q; }
            }
        segs.push_back({bc, lw / cnt});
    }
    double wMax = 0;
    for (const Seg& s : segs) wMax = std::max(wMax, s.w);
    int firstSeg = -1, lastSeg = -1;
    for (size_t i = 0; i < segs.size(); ++i) {
        if (segs[i].chord < 0 || segs[i].w < 0.3 * wMax) continue;
        kf.roots[segs[i].chord] += segs[i].w;
        if (firstSeg < 0) firstSeg = (int)i;
        lastSeg = (int)i;
    }
    if (firstSeg >= 0) { kf.edge[segs[(size_t)firstSeg].chord] += 0.5; kf.edge[segs[(size_t)lastSeg].chord] += 0.5; }

    auto norm = [](double* v, int n) {
        double t = 0;
        for (int i = 0; i < n; ++i) t += v[i];
        if (t > 0) for (int i = 0; i < n; ++i) v[i] /= t;
    };
    norm(kf.chroma, 12); norm(kf.bass, 12); norm(kf.lowest, 12); norm(kf.roots, 24);
    double ct = 0;
    for (double v : kf.chroma) ct += v;
    kf.valid = ct > 0;
    return kf;
}

// Score of each key (0..11 major, 12..23 minor): a linear model over the evidence expressed relative to the
// candidate tonic (chroma, bass and lowest-note profiles, the chord-per-bar distribution and the first and
// last chords, each rotated so index 0 is the tonic). It starts from Faraldo's EDMA profile for the chroma
// part and was fitted (softmax over the 24 keys, L2 pull towards that start, major and minor examples
// weighted equally) on the development half of tests/eval; the held-out half was not used. Read as
// profiles the weights are musical: tonic and fifth in the chroma, the tonic as lowest note, the
// diatonic chords (ii, iii, IV, V, vi in major; III, iv, v, VI, VII in minor) and a tonic first or last
// chord. Compared with plain profile correlation on that data (v1.0.0 averaged Krumhansl-Kessler and
// Temperley), it mostly removes relative major/minor errors.
inline void keyScores(const KeyFeatures& kf, double out[24]) {
    static const double kWMaj[84] = {
         6.10, -2.73, -0.10, -1.35,  2.30,  0.78, -2.00,  4.43, -3.17, -1.24, -1.74, -1.38,  // chroma
        -0.88, -1.26, -0.39,  0.64,  0.23, -0.35,  0.23,  1.34,  0.01,  0.30,  0.22, -0.67,  // bass
         2.18, -0.06,  0.18, -0.79, -0.05, -0.76, -0.08,  0.30, -0.91,  0.39,  0.17, -0.45,  // lowest note
         0.64, -0.63,  0.19, -0.51, -0.99,  0.88,  0.14,  0.54,  0.13, -0.38,  1.50, -0.37,  // major chords
        -0.40, -0.50, -0.68, -0.50,  1.03, -0.73,  0.16,  0.07, -0.21,  1.34, -0.41, -0.43,  // minor chords
         1.73, -0.60, -0.72, -0.65, -1.03,  0.78,  0.57,  0.47, -0.29, -0.29,  0.59, -0.42,  // first/last major
         1.26, -0.49, -1.31, -0.56,  1.20, -0.35,  0.25,  0.44,  0.06, -0.41, -0.35,  0.04,  // first/last minor
    };
    static const double kWMin[84] = {
         5.89, -2.42, -0.37,  2.09, -2.02, -0.16, -1.98,  3.45, -0.79, -2.60,  0.85, -1.81,  // chroma
        -0.27, -0.77, -0.08,  0.37, -0.33,  0.33,  0.09,  1.35,  0.28, -0.74,  0.70, -0.36,  // bass
         1.29, -0.73, -0.98, -0.16, -1.08,  0.08,  0.07, -0.23,  0.99, -0.91,  1.04,  0.52,  // lowest note
        -0.09, -0.70, -0.18,  0.56, -0.61, -0.18, -0.31, -0.25,  0.75, -0.35,  0.53, -0.31,  // major chords
         1.57, -0.06,  0.09, -0.26, -0.22,  0.41, -0.17,  0.93, -0.23, -0.41, -0.52,  0.12,  // minor chords
         1.30, -0.41, -0.39, -0.22, -0.50,  0.18, -0.44,  0.45,  0.49, -0.59,  0.05, -0.05,  // first/last major
         0.99, -0.30,  0.18,  0.35, -0.18,  0.57, -0.22,  0.25, -0.37, -0.76, -0.23, -0.05,  // first/last minor
    };
    const double kBias[2] = {-0.15, 0.15};
    for (int k = 0; k < 24; ++k) {
        const int t = k % 12, m = k / 12;
        const double* w = m ? kWMin : kWMaj;
        double s = kBias[m];
        for (int i = 0; i < 12; ++i) {
            const int pc = (t + i) % 12;
            s += w[i] * kf.chroma[pc] + w[12 + i] * kf.bass[pc] + w[24 + i] * kf.lowest[pc] + w[36 + i] * kf.roots[pc] +
                 w[48 + i] * kf.roots[12 + pc] + w[60 + i] * kf.edge[pc] + w[72 + i] * kf.edge[12 + pc];
        }
        out[k] = s;
    }
}

inline KeyInfo decideKey(const KeyFeatures& kf) {
    KeyInfo ki;
    if (!kf.valid) return ki;
    ki.tuningCents = kf.tuningCents;
    double sc[24];
    keyScores(kf, sc);
    int order[24];
    std::iota(order, order + 24, 0);
    std::sort(order, order + 24, [&](int a, int b) { return sc[a] > sc[b]; });
    const int best = order[0];
    ki.key = best % 12;
    ki.minor = best >= 12;
    // Confidence: the winner's probability under the model (softmax over the 24 keys). Candidates: the
    // three most probable keys, each with its probability relative to the winner's. (Forcing the relative
    // major/minor into the list was tried: on the evaluation set it covered the true key less often, 75 %
    // against 82 % for the plain top three, since the model rarely leaves the relative key as runner-up.)
    double z = 0;
    for (int i = 0; i < 24; ++i) z += std::exp(sc[i] - sc[best]);
    ki.conf = clamp01((float)(1.0 / z));
    for (int i = 0; i < 3; ++i) ki.cands.push_back({order[i] % 12, order[i] >= 12, (float)std::exp(sc[order[i]] - sc[best])});
    return ki;
}

inline double firstSoundSec(const float* x, int n, double sr) {
    float peak = 0;
    for (int i = 0; i < n; ++i) peak = std::max(peak, std::fabs(x[i]));
    if (peak <= 0) return 0;
    const float thr = peak * 0.0158f;  // -36 dB re peak
    for (int i = 0; i < n; ++i) if (std::fabs(x[i]) >= thr) return i / sr;
    return 0;
}

}  // namespace detail

namespace detail {

// Peak-normalised (or scaled by `gainIn` when > 0), decimated working signal plus its ODF and onsets.
struct Prepared {
    std::vector<float> y;
    double sr = 0;
    float gain = 0;
    OdfData d;
    Onsets on;
};

inline float peakOf(const float* x, int n) {
    float peak = 0;
    for (int i = 0; i < n; ++i) {
        const float a = std::fabs(x[i]);
        if (a > peak && std::isfinite(a)) peak = a;
    }
    return peak;
}

inline double rmsOf(const float* x, int n) {
    double s = 0;
    for (int i = 0; i < n; ++i) if (std::isfinite(x[i])) s += (double)x[i] * x[i];
    return n > 0 ? std::sqrt(s / n) : 0.0;
}

inline bool prepare(const float* x, int n, double sampleRate, Prepared& p, bool withOnsets = true) {
    const float peak = peakOf(x, n);
    if (peak < 1e-7f) return false;
    p.gain = std::min(1.0f / peak, 1e4f);
    p.y = toWorkingRate(x, n, sampleRate, p.gain);
    p.sr = kWorkRate;
    if (!withOnsets) return true;
    p.d = computeOdf(p.y, p.sr);
    p.on = detectOnsets(p.d, p.y);
    // Map ODF frame centres onto attack-foot times for grid work.
    std::vector<double> offs;
    for (size_t i = 0; i < p.on.t.size(); ++i) offs.push_back(p.on.t[i] - p.on.frame[i] * (double)p.d.hop / p.d.sr);
    if (!offs.empty()) {
        std::nth_element(offs.begin(), offs.begin() + (ptrdiff_t)(offs.size() / 2), offs.end());
        p.d.offset = offs[offs.size() / 2];
    }
    return true;
}

// Internal values exposed for offline tuning (tests/eval).
struct AnalysisDebug {
    std::vector<TempoFamily> families;
    KeyFeatures key;
};

inline Result analyseImpl(const float* mix, const float* drums, const float* bass, const float* harmonic, int numSamples,
                          double sampleRate, AnalysisDebug* dbg) {
    Result r;
    if (!mix || numSamples <= 0 || !(sampleRate > 0)) return r;
    Prepared pm;
    if (!prepare(mix, numSamples, sampleRate, pm)) return r;
    // A stem counts when it carries real energy (above -30 dB re the mix); separation leaves faint
    // residue in empty stems that must not drive anything.
    const double mixRms = rmsOf(mix, numSamples);
    auto usable = [&](const float* s) { return s && rmsOf(s, numSamples) >= 0.03 * mixRms; };

    // Rhythm: onsets, tempo and grid from the drums when there are drums, else from the mix.
    Prepared pdr;
    const Prepared* rh = &pm;
    if (usable(drums) && prepare(drums, numSamples, sampleRate, pdr) && pdr.on.t.size() >= 8) rh = &pdr;
    r.onsetsSec = rh->on.t;
    r.onsetStrength = rh->on.s;
    const double len = numSamples / sampleRate;
    const double fs = firstSoundSec(mix, numSamples, sampleRate);
    TempoInfo ti = estimateTempo(rh->d, rh->on, len, fs, dbg ? &dbg->families : nullptr);
    r.bpm = ti.bpm;
    r.firstBeatSec = ti.firstBeat;
    r.bpmConfidence = ti.conf;
    r.tempoCandidates = std::move(ti.cands);

    // Key: chroma from the pitched parts (harmonic + bass stems, else the mix), bass evidence from the
    // bass stem when there is one. Stems are scaled with the mix gain so their levels stay comparable.
    const bool hasBass = usable(bass), hasHarm = usable(harmonic);
    PeakFrames tonalPf, bassPf;
    if (hasHarm) {
        std::vector<float> t(harmonic, harmonic + numSamples);
        if (hasBass) for (int i = 0; i < numSamples; ++i) t[(size_t)i] += bass[i];
        tonalPf = spectralPeaks(toWorkingRate(t.data(), numSamples, sampleRate, pm.gain), pm.sr);
    } else {
        tonalPf = spectralPeaks(pm.y, pm.sr);
    }
    if (hasBass) bassPf = spectralPeaks(toWorkingRate(bass, numSamples, sampleRate, pm.gain), pm.sr);
    const double bar = (r.bpm > 0 && r.bpmConfidence >= 0.3f) ? 240.0 / r.bpm : 0.0;
    const KeyFeatures kf = keyFeatures(tonalPf, hasBass ? &bassPf : nullptr, bar, r.firstBeatSec);
    if (dbg) dbg->key = kf;
    KeyInfo ki = decideKey(kf);
    r.key = ki.key;
    r.minor = ki.minor;
    r.keyConfidence = ki.conf;
    r.keyCandidates = std::move(ki.cands);
    return r;
}

}  // namespace detail

// ---------------------------------------------------------------------------------------------------------
inline Result analyse(const float* mono, int numSamples, double sampleRate) {
    return detail::analyseImpl(mono, nullptr, nullptr, nullptr, numSamples, sampleRate, nullptr);
}

// Stem-aware analysis: tempo, grid and onsets from the drums, key from the pitched stems with the bass
// stem as tonic evidence. Any stem may be null (or near silent); the mix stands in for what is missing.
// All buffers hold numSamples mono samples at sampleRate.
inline Result analyseWithStems(const float* mixMono, const float* drumsMono, const float* bassMono, const float* harmonicMono,
                               int numSamples, double sampleRate) {
    return detail::analyseImpl(mixMono, drumsMono, bassMono, harmonicMono, numSamples, sampleRate, nullptr);
}

inline double snapToTransient(const Result& r, double timeSec, double windowSec = 0.03) {
    const auto& o = r.onsetsSec;
    if (o.empty()) return timeSec;
    auto it = std::lower_bound(o.begin(), o.end(), timeSec);
    double best = timeSec, bd = windowSec;
    bool found = false;
    if (it != o.end() && std::fabs(*it - timeSec) <= bd) { best = *it; bd = std::fabs(*it - timeSec); found = true; }
    if (it != o.begin()) {
        const double p = *(it - 1);
        if (std::fabs(p - timeSec) <= bd && (!found || std::fabs(p - timeSec) < bd)) { best = p; found = true; }
    }
    return found ? best : timeSec;
}

namespace detail {

// Insert midpoints of the largest gaps until `count` cues exist (keeps cues distinct and sorted).
inline void fillGaps(std::vector<double>& cues, double len, int count) {
    std::sort(cues.begin(), cues.end());
    if (cues.empty()) cues.push_back(0.0);
    while ((int)cues.size() < count) {
        double bestGap = -1;
        size_t bestI = 0;
        for (size_t i = 0; i < cues.size(); ++i) {
            const double nxt = (i + 1 < cues.size()) ? cues[i + 1] : len;
            if (nxt - cues[i] > bestGap) { bestGap = nxt - cues[i]; bestI = i; }
        }
        if (cues.front() > bestGap) {  // gap before the first cue
            cues.insert(cues.begin(), 0.5 * cues.front());
            continue;
        }
        const double nxt = (bestI + 1 < cues.size()) ? cues[bestI + 1] : len;
        cues.insert(cues.begin() + (ptrdiff_t)bestI + 1, 0.5 * (cues[bestI] + nxt));
    }
}

inline void finaliseCues(std::vector<double>& cues, double len, int count, double sr) {
    const double maxT = std::max(0.0, len - 1.0 / sr);
    for (auto& c : cues) c = std::min(std::max(0.0, c), maxT);
    std::sort(cues.begin(), cues.end());
    cues.erase(std::unique(cues.begin(), cues.end(), [](double a, double b) { return std::fabs(a - b) < 1e-9; }), cues.end());
    if ((int)cues.size() > count) cues.resize((size_t)count);
    if ((int)cues.size() < count) fillGaps(cues, len, count);
    if ((int)cues.size() > count) cues.resize((size_t)count);
}

}  // namespace detail

inline std::vector<double> chop(const Result& r, const float* mono, int numSamples, double sampleRate, int count,
                                ChopMode mode, double beatsPerCue = 1.0) {
    std::vector<double> cues;
    if (count <= 0 || numSamples <= 0 || !(sampleRate > 0)) return cues;
    const double len = numSamples / sampleRate;
    const double P = r.bpm > 0 ? 60.0 / r.bpm : 0.0;

    if (mode == ChopMode::Even || (mode == ChopMode::Beats && P <= 0)) {
        for (int i = 0; i < count; ++i) cues.push_back(len * i / count);
        detail::finaliseCues(cues, len, count, sampleRate);
        return cues;
    }

    if (mode == ChopMode::Beats) {
        const double step = P * (beatsPerCue > 0 ? beatsPerCue : 1.0);
        for (int i = 0; i < count; ++i) {
            const double t = r.firstBeatSec + i * step;
            if (t >= len) break;
            cues.push_back(snapToTransient(r, t, 0.03));
        }
        // File shorter than count cues: extend the grid backwards before the first beat, then subdivide.
        for (int i = 1; (int)cues.size() < count; ++i) {
            const double t = r.firstBeatSec - i * step;
            if (t < 0) break;
            cues.push_back(snapToTransient(r, t, 0.03));
        }
        detail::finaliseCues(cues, len, count, sampleRate);
        return cues;
    }

    // Auto.
    const double first0 = mono ? detail::firstSoundSec(mono, numSamples, sampleRate) : 0.0;
    double first = first0;
    for (size_t i = 0; i < r.onsetsSec.size(); ++i) {
        const double t = r.onsetsSec[i];
        if (t > first0 + 0.08) break;
        if (t >= first0 - 0.03) { first = t; break; }
    }
    first = std::min(std::max(0.0, first), len);
    const double span = std::max(1e-6, len - first);
    double minGap = P > 0 ? P / 4.0 : 0.06;
    minGap = std::min(minGap, span / count * 0.9);

    double rmsG = 0;
    if (mono) {
        double s = 0;
        for (int i = 0; i < numSamples; ++i) s += (double)mono[i] * mono[i];
        rmsG = std::sqrt(s / numSamples);
    }
    const double gridConf = P > 0 ? std::max(0.3, (double)r.bpmConfidence) : 0.0;

    struct Cand { double t; double score; };
    std::vector<Cand> cand;
    for (size_t i = 0; i < r.onsetsSec.size(); ++i) {
        const double t = r.onsetsSec[i];
        if (t <= first + 1e-6 || t >= len - 0.01) continue;
        double s = std::pow((double)std::max(0.01f, r.onsetStrength[i]), 0.7);
        if (P > 0) {
            const double x = (t - r.firstBeatSec) / P;
            const long n = std::lround(x);
            const double dev = std::fabs(t - (r.firstBeatSec + n * P));
            double bonus = 0;
            if (dev < std::max(0.03, 0.06 * P)) {
                bonus = 0.8;
                const long m4 = ((n % 4) + 4) % 4, m16 = ((n % 16) + 16) % 16;
                if (m4 == 0) bonus += 0.6;
                if (m16 == 0) bonus += 0.8;
            } else {
                const double xh = x * 2.0;
                if (std::fabs(xh - std::round(xh)) * P * 0.5 < std::max(0.025, 0.05 * P)) bonus = 0.15;
            }
            s *= 1.0 + gridConf * bonus;
        }
        if (mono && rmsG > 0) {  // prefer chops that start something with body
            const int a0 = (int)(t * sampleRate);
            const int a1 = std::min(numSamples, a0 + (int)(0.25 * sampleRate));
            double e = 0;
            for (int j = a0; j < a1; ++j) e += (double)mono[j] * mono[j];
            const double rmsA = a1 > a0 ? std::sqrt(e / (a1 - a0)) : 0.0;
            s *= 0.5 + 0.5 * std::min(1.0, rmsA / rmsG);
        }
        cand.push_back({t, s});
    }
    // Grid beats as low-priority fallbacks so sparse material still gets musical positions.
    if (P > 0) {
        for (long n = (long)std::ceil((first - r.firstBeatSec) / P);; ++n) {
            const double t = r.firstBeatSec + n * P;
            if (t >= len - 0.01) break;
            if (t <= first + 1e-6) continue;
            cand.push_back({t, 0.02});
        }
    }

    cues.push_back(first);
    const double ideal = span / count;
    std::vector<double> dmin(cand.size());
    for (size_t i = 0; i < cand.size(); ++i) dmin[i] = std::fabs(cand[i].t - first);
    std::vector<char> used(cand.size(), 0);
    while ((int)cues.size() < count) {
        double best = -1;
        size_t bi = 0;
        for (size_t i = 0; i < cand.size(); ++i) {
            if (used[i] || dmin[i] < minGap) continue;
            const double spread = std::min(1.0, dmin[i] / ideal);
            const double v = cand[i].score * spread;
            if (v > best) { best = v; bi = i; }
        }
        if (best < 0) break;
        used[bi] = 1;
        cues.push_back(cand[bi].t);
        for (size_t i = 0; i < cand.size(); ++i) dmin[i] = std::min(dmin[i], std::fabs(cand[i].t - cand[bi].t));
    }
    detail::finaliseCues(cues, len, count, sampleRate);
    return cues;
}

inline Overview overview(const float* mono, int numSamples, double sampleRate, int buckets) {
    Overview ov;
    if (buckets <= 0) return ov;
    const size_t B = (size_t)buckets;
    ov.mn.assign(B, 0.f); ov.mx.assign(B, 0.f); ov.low.assign(B, 0.f); ov.mid.assign(B, 0.f); ov.high.assign(B, 0.f);
    if (!mono || numSamples <= 0 || !(sampleRate > 0)) return ov;

    // 2nd-order Butterworth low-pass at 200 Hz and high-pass at 2.5 kHz (RBJ biquads), mid = rest.
    struct Biquad {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        inline float run(float x) {
            const double yv = b0 * x + z1;
            z1 = b1 * x - a1 * yv + z2;
            z2 = b2 * x - a2 * yv;
            return (float)yv;
        }
    };
    auto design = [&](double f, bool hp) {
        Biquad q;
        const double fc = std::min(f, 0.45 * sampleRate);
        const double w = 2.0 * detail::kPi * fc / sampleRate, cw = std::cos(w), sw = std::sin(w);
        const double alpha = sw / (2.0 * 0.70710678);
        const double a0 = 1 + alpha;
        if (hp) { q.b0 = (1 + cw) / 2; q.b1 = -(1 + cw); q.b2 = (1 + cw) / 2; }
        else    { q.b0 = (1 - cw) / 2; q.b1 = 1 - cw;    q.b2 = (1 - cw) / 2; }
        q.a1 = -2 * cw; q.a2 = 1 - alpha;
        q.b0 /= a0; q.b1 /= a0; q.b2 /= a0; q.a1 /= a0; q.a2 /= a0;
        return q;
    };
    Biquad lp = design(200.0, false), hp = design(2500.0, true);
    std::vector<double> eAll(B, 0.0);
    int pos = 0;
    for (size_t b = 0; b < B; ++b) {
        int s0 = (int)((double)b * numSamples / buckets);
        int s1 = (int)((double)(b + 1) * numSamples / buckets);
        s0 = std::min(s0, numSamples - 1);
        s1 = std::max(s1, s0 + 1);
        s1 = std::min(s1, numSamples);
        float mn = mono[s0], mx = mono[s0];
        double el = 0, em = 0, eh = 0, ea = 0;
        int cnt = 0;
        for (int i = s0; i < s1; ++i) {
            const float x = mono[i];
            mn = std::min(mn, x); mx = std::max(mx, x);
            if (i >= pos) {  // run the filters once per sample even if buckets overlap
                const float l = lp.run(x), h = hp.run(x), m = x - l - h;
                el += (double)l * l; eh += (double)h * h; em += (double)m * m; ea += (double)x * x;
                ++cnt;
                pos = i + 1;
            }
        }
        ov.mn[b] = mn; ov.mx[b] = mx;
        if (cnt > 0) {
            ov.low[b] = (float)std::sqrt(el / cnt);
            ov.mid[b] = (float)std::sqrt(em / cnt);
            ov.high[b] = (float)std::sqrt(eh / cnt);
            eAll[b] = std::sqrt(ea / cnt);
        } else if (b > 0) {
            ov.low[b] = ov.low[b - 1]; ov.mid[b] = ov.mid[b - 1]; ov.high[b] = ov.high[b - 1]; eAll[b] = eAll[b - 1];
        }
    }
    // Common reference (loudest bucket) with a fixed tilt so typical mixes use the whole range per band.
    double ref = 0;
    for (double e : eAll) ref = std::max(ref, e);
    if (ref <= 0) return ov;
    for (size_t b = 0; b < B; ++b) {
        ov.low[b] = detail::clamp01((float)(ov.low[b] / ref));
        ov.mid[b] = detail::clamp01((float)(ov.mid[b] / ref * 1.5));
        ov.high[b] = detail::clamp01((float)(ov.high[b] / ref * 3.0));
    }
    return ov;
}

inline const char* keyName(int key, bool minor) {
    static const char* const maj[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static const char* const mnr[12] = {"Cm", "C#m", "Dm", "D#m", "Em", "Fm", "F#m", "Gm", "G#m", "Am", "A#m", "Bm"};
    const int k = ((key % 12) + 12) % 12;
    return minor ? mnr[k] : maj[k];
}

// Key-wheel code, e.g. C major -> "8B", A minor -> "8A".
inline const char* wheelCode(int key, bool minor) {
    static const char* const codesB[12] = {"1B", "2B", "3B", "4B", "5B", "6B", "7B", "8B", "9B", "10B", "11B", "12B"};
    static const char* const codesA[12] = {"1A", "2A", "3A", "4A", "5A", "6A", "7A", "8A", "9A", "10A", "11A", "12A"};
    const int k = ((key % 12) + 12) % 12;
    const int major = minor ? (k + 3) % 12 : k;     // relative major shares the number
    const int num = (major * 7 + 7) % 12 + 1;       // C=8, G=9, ... (circle of fifths)
    return minor ? codesA[num - 1] : codesB[num - 1];
}

}  // namespace analysis
}  // namespace chop
