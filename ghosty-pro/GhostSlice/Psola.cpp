#include "Psola.h"
#include "Lpc.h"
#include "AnalysisInternal.h"

namespace gslice::psola
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

// 4-point cubic (Catmull-Rom) read; an integer position reads the sample itself
int64 argmaxIn (const std::vector<double>& y, int64 from, int64 to, double sign = 1.0)
{
    from = std::max<int64> (0, from); to = std::min<int64> ((int64) y.size() - 1, to);
    int64 best = from; double bv = -1e300;
    for (int64 i = from; i <= to; ++i) if (sign * y[(size_t) i] > bv) { bv = sign * y[(size_t) i]; best = i; }
    return best;
}
} // namespace

std::vector<Epoch> markEpochs (const float* x, int64 n, double sr, const std::function<double (int64)>& f0At)
{
    std::vector<Epoch> out;
    if (n <= 0) return out;
    // voiced runs (f0 > 0), probed every 32 samples
    std::vector<std::pair<int64, int64>> runs;
    {
        const int64 step = 32; int64 i = 0;
        while (i < n)
        {
            while (i < n && ! (f0At (i) > 0)) i += step;
            if (i >= n) break;
            const int64 a = i;
            while (i < n && f0At (i) > 0) i += step;
            runs.push_back ({ a, std::min (i, n) });
        }
    }
    const int order = juce::jlimit (12, 32, (int) std::lround (sr / 2000.0) + 4);
    constexpr double kPre = 0.97;
    std::vector<double> a ((size_t) order + 1), scratch;
    int runNo = 0;
    for (auto [ra, rb] : runs)
    {
        // the run's median f0
        std::vector<double> fs; for (int k = 0; k < 33; ++k) { const double f = f0At (ra + (rb - 1 - ra) * k / 32); if (f > 0) fs.push_back (f); }
        if (fs.empty()) continue;
        std::sort (fs.begin(), fs.end());
        const double fmed = fs[fs.size() / 2], T0 = sr / fmed;
        if ((double) (rb - ra) < 2.0 * T0) continue;   // too short to hold two periods
        auto T = [&] (int64 p) { const double f = f0At (juce::jlimit (ra, rb - 1, p)); return f > 0 ? juce::jlimit (0.5 * T0, 2.0 * T0, sr / f) : T0; };
        const int64 pad = (int64) std::ceil (2.0 * T0) + 4;
        const int64 ya = std::max<int64> (0, ra - pad), yb = std::min<int64> (n, rb + pad);
        const int64 L = yb - ya;
        // 1. period-synchronous coarse marks: the positive peaks of the zero-phase low-passed signal
        std::vector<double> y ((size_t) L); for (int64 i = 0; i < L; ++i) y[(size_t) i] = x[ya + i];
        analysis::detail::zeroPhaseLowpass (y, juce::jlimit (50.0, 0.45 * sr, 1.5 * fmed), sr);
        std::vector<int64> m;
        {
            int64 e = argmaxIn (y, ra - ya, ra - ya + (int64) std::ceil (T (ra)));
            while (e + ya < rb)
            {
                m.push_back (e);
                const double Te = T (e + ya);
                const int64 ne = argmaxIn (y, e + (int64) std::floor (0.7 * Te), e + (int64) std::ceil (1.3 * Te));
                if (ne <= e) break;
                e = ne;
            }
        }
        if (m.size() < 2) continue;
        // 2. the LPC residual of the pre-emphasised signal (10 ms blocks, 20 ms Hann frames, order ~ sr / 2 kHz + 4)
        std::vector<double> r ((size_t) L, 0.0);
        {
            const int blk = juce::jmax (64, (int) std::lround (0.010 * sr)), win = 2 * blk;
            scratch.assign ((size_t) (win + 2 * (order + 1)), 0.0);
            std::vector<float> frame ((size_t) win);
            auto pe = [&] (int64 i) { const int64 j = ya + i; return (j >= 0 && j < n ? (double) x[j] : 0.0) - kPre * (j - 1 >= 0 && j - 1 < n ? (double) x[j - 1] : 0.0); };
            for (int64 q = 0; q < L; q += blk)
            {
                for (int i = 0; i < win; ++i) { const int64 j = ya + q + blk / 2 - win / 2 + i; frame[(size_t) i] = j >= 0 && j < n ? x[j] : 0.0f; }
                lpc::analyseFrame (frame.data(), win, order, sr, 30.0, kPre, a.data(), scratch.data());
                for (int64 i = q; i < std::min<int64> (L, q + blk); ++i)
                {
                    double e = 0; for (int k = 0; k <= order; ++k) e += a[(size_t) k] * pe (i - k);
                    r[(size_t) i] = e;
                }
            }
        }
        // 3. the residual's dominant polarity and the run's consistent phase offset (circular mean, weighted by the peak)
        double sp = 0, sn = 0;
        std::vector<int64> pp (m.size()), pn (m.size());
        for (size_t k = 0; k < m.size(); ++k)
        {
            const double Tk = T (m[k] + ya);
            const int64 lo = m[k] - (int64) std::floor (0.5 * Tk), hi = m[k] + (int64) std::floor (0.5 * Tk);
            pp[k] = argmaxIn (r, lo, hi, 1.0); pn[k] = argmaxIn (r, lo, hi, -1.0);
            sp += std::max (0.0, r[(size_t) pp[k]]); sn += std::max (0.0, -r[(size_t) pn[k]]);
        }
        const double sign = sp >= sn ? 1.0 : -1.0;
        double cs = 0, sn2 = 0;
        for (size_t k = 0; k < m.size(); ++k)
        {
            const double Tk = T (m[k] + ya);
            const int64 p = sign > 0 ? pp[k] : pn[k];
            const double th = 2.0 * kPi * (double) (p - m[k]) / Tk, wv = std::abs (r[(size_t) p]);
            cs += wv * std::cos (th); sn2 += wv * std::sin (th);
        }
        const double phase = std::atan2 (sn2, cs) / (2.0 * kPi);   // fraction of a period (-0.5 .. 0.5]
        // 4. refine each mark to the residual peak near (mark + offset); the epoch = the sub-sample low-pass peak (parabolic) + the
        //    median of the neighbours' residual offsets (5 marks): consistent to a fraction of a sample, on the closure itself
        std::vector<double> mf (m.size()), off (m.size());
        for (size_t k = 0; k < m.size(); ++k)
        {
            const int64 q = m[k]; double d = 0;
            if (q > 0 && q + 1 < L)
            {
                const double y0 = y[(size_t) q - 1], y1 = y[(size_t) q], y2 = y[(size_t) q + 1], den = y0 - 2 * y1 + y2;
                if (std::abs (den) > 1e-18) d = juce::jlimit (-0.5, 0.5, 0.5 * (y0 - y2) / den);
            }
            mf[k] = (double) q + d;
            const double Tk = T (m[k] + ya);
            const double c = mf[k] + phase * Tk;
            const int64 lo = (int64) std::floor (c - 0.25 * Tk), hi = (int64) std::ceil (c + 0.25 * Tk);
            const int64 p = argmaxIn (r, lo, hi, sign);
            double e2 = 0; int cnt = 0;
            for (int64 i = std::max<int64> (0, (int64) (c - 0.5 * Tk)); i < std::min<int64> (L, (int64) (c + 0.5 * Tk)); ++i) { e2 += r[(size_t) i] * r[(size_t) i]; ++cnt; }
            const double rms = cnt > 0 ? std::sqrt (e2 / cnt) : 0.0;
            off[k] = sign * r[(size_t) p] >= 2.0 * rms && rms > 0 ? (double) p - mf[k] : phase * Tk;
        }
        double last = -1e18;
        for (size_t k = 0; k < m.size(); ++k)
        {
            double v[5]; int nv = 0;
            for (int d = -2; d <= 2; ++d) { const int j = (int) k + d; if (j >= 0 && j < (int) m.size()) v[nv++] = off[(size_t) j]; }
            std::sort (v, v + nv);
            const double e = mf[k] + v[nv / 2] + (double) ya;
            const double Tk = T ((int64) e);
            if (e < (double) (ra - (int64) T0) || e >= (double) (rb + (int64) T0)) continue;
            if (e - last < 0.5 * Tk) continue;
            out.push_back ({ e, runNo });
            last = e;
        }
        ++runNo;
    }
    return out;
}
} // namespace gslice::psola
