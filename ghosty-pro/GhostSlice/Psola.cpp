#include "Psola.h"
#include "Lpc.h"
#include "AnalysisInternal.h"

namespace gslice::psola
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

// 4-point cubic (Catmull-Rom) read; an integer position reads the sample itself
inline float readAt (const WorkingAudio& w, int ch, double pos) noexcept
{
    const double fl = std::floor (pos); const int64 i = (int64) fl; const double f = pos - fl;
    if (f == 0.0) return w.at (ch, i);
    const double y0 = w.at (ch, i - 1), y1 = w.at (ch, i), y2 = w.at (ch, i + 1), y3 = w.at (ch, i + 2);
    const double c1 = 0.5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3, c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
    return (float) (((c3 * f + c2) * f + c1) * f + y1);
}

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

void render (const Piece& pc, const WorkingAudio& w, double pitchSemis, double formantSemis, float* L, float* R, int64 len, Stats* stats)
{
    jassert (pc.curve != nullptr && ! pc.reverse);
    const PitchCurve& C = *pc.curve;
    const WarpMap& map = pc.map;
    const double sr = w.rate;
    const int64 pad = (int64) std::ceil (0.03 * sr);
    const int64 U0 = pc.u0 - pad, U1 = pc.u0 + len + pad, NU = U1 - U0;
    // the source span (WF) the piece reads, + margin for the grains
    const int64 margin = (int64) std::ceil (0.06 * sr);
    const int64 A = (int64) std::floor (std::min (map.srcAt ((double) U0), map.srcAt ((double) pc.u0))) - margin;
    const int64 B = (int64) std::ceil (map.srcAt ((double) U1)) + margin;
    std::vector<float> mono ((size_t) std::max<int64> (1, B - A));
    {
        const int nc = juce::jmax (1, w.channels());
        for (int64 i = A; i < B; ++i) { double v = 0; for (int c = 0; c < nc; ++c) v += w.at (c, i); mono[(size_t) (i - A)] = (float) (v / nc); }
    }
    const auto ep = [&]
    {
        auto e = markEpochs (mono.data(), B - A, sr, [&] (int64 i) { return (double) C.f0At ((double) (A + i)); });
        for (auto& x : e) x.pos += (double) A;
        return e;
    }();
    const int NE = (int) ep.size();
    auto nearest = [&] (double s) -> int
    {
        if (NE == 0) return -1;
        auto it = std::lower_bound (ep.begin(), ep.end(), s, [] (const Epoch& e, double v) { return e.pos < v; });
        int j = (int) (it - ep.begin());
        if (j >= NE) return NE - 1;
        if (j > 0 && s - ep[(size_t) j - 1].pos < ep[(size_t) j].pos - s) --j;
        return j;
    };
    const double gC = pitchSemis * 100.0, gF = formantSemis * 100.0;
    const double H = std::max (16.0, std::round (0.004 * sr));
    // ---- 1. the synthesis marks
    struct Mark { double t, src, hl, hr, phi; bool voiced; };
    std::vector<Mark> marks;
    {
        double t = (double) U0, delta = 0, tLastV = -1e18;
        bool prevV = false;
        while (t < (double) U1)
        {
            double s = map.srcAt (t);
            const double f0 = C.f0At (s);
            int i = nearest (s);
            bool voiced = f0 > 0 && i >= 0 && std::abs (ep[(size_t) i].pos - s) <= 0.75 * sr / f0;
            if (voiced && ! prevV)
            {   // phase-continuous entry: the first voiced mark sits where a source epoch plays at 1x
                int j = ep[(size_t) i].pos >= s ? i : i + 1;
                if (j < NE && ep[(size_t) j].run == ep[(size_t) i].run)
                {
                    const double tj = map.outAt (ep[(size_t) j].pos);
                    if (tj >= t && tj < t + 1.5 * sr / f0) { t = tj; s = ep[(size_t) j].pos; i = j; }
                }
            }
            float c = 0, f = 0, b = 0; C.at (s, c, f, b);
            const double rho = std::pow (2.0, (gC + (double) c) / 1200.0), phi = std::pow (2.0, (gF + (double) f) / 1200.0);
            if (voiced)
            {
                const double T = sr / f0;
                double pl = i > 0 && ep[(size_t) i - 1].run == ep[(size_t) i].run ? ep[(size_t) i].pos - ep[(size_t) i - 1].pos : T;
                double pr = i + 1 < NE && ep[(size_t) i + 1].run == ep[(size_t) i].run ? ep[(size_t) i + 1].pos - ep[(size_t) i].pos : T;
                pl = juce::jlimit (0.5 * T, 2.0 * T, pl); pr = juce::jlimit (0.5 * T, 2.0 * T, pr);
                marks.push_back ({ t, ep[(size_t) i].pos, pl / phi, pr / phi, phi, true });
                delta = ep[(size_t) i].pos - s; tLastV = t;
                t += pr / rho;
            }
            else
            {
                const double k = juce::jlimit (0.0, 1.0, 1.0 - (t - tLastV) / (0.03 * sr));
                marks.push_back ({ t, s + delta * k, H, H, 1.0, false });
                t += H;
            }
            prevV = voiced;
        }
    }
    // where voiced and unvoiced marks meet, the two grains reach each other (the window sum never opens a gap there)
    for (size_t k = 0; k < marks.size(); ++k)
    {
        if (k > 0 && marks[k - 1].voiced != marks[k].voiced) marks[k].hl = std::max (marks[k].hl, marks[k].t - marks[k - 1].t);
        if (k + 1 < marks.size() && marks[k + 1].voiced != marks[k].voiced) marks[k].hr = std::max (marks[k].hr, marks[k + 1].t - marks[k].t);
    }
    // ---- 2. overlap-add
    std::vector<double> acc0 ((size_t) NU, 0.0), acc1 ((size_t) NU, 0.0), ws ((size_t) NU, 0.0);
    for (auto& mk : marks)
    {
        const int64 n0 = std::max<int64> (U0, (int64) std::ceil (mk.t - mk.hl)), n1 = std::min<int64> (U1 - 1, (int64) std::floor (mk.t + mk.hr));
        const double step = mk.voiced ? mk.phi : 1.0;
        for (int64 nn = n0; nn <= n1; ++nn)
        {
            const double k = (double) nn - mk.t;
            const double wv = k < 0 ? 0.5 + 0.5 * std::cos (kPi * k / mk.hl) : 0.5 + 0.5 * std::cos (kPi * k / mk.hr);
            if (wv <= 0) continue;
            const double pos = mk.src + k * step;
            const size_t o = (size_t) (nn - U0);
            acc0[o] += wv * readAt (w, 0, pos); acc1[o] += wv * readAt (w, 1, pos); ws[o] += wv;
        }
    }
    // the normalised OLA (the window sum, floor 0.5: a -12 st gap stays a gap; Moulines & Charpentier's normalised OLA)
    for (int64 i = 0; i < NU; ++i) { const double g = 1.0 / std::max (0.5, ws[(size_t) i]); acc0[(size_t) i] *= g; acc1[(size_t) i] *= g; }
    // the loudness: re-shifted grains overlap partly out of phase (a pitch-up loses up to ~3.5 dB at +4 st), so the output follows
    // the source's level at the mapped position: RMS over 20 ms every 5 ms, +-6 dB at most, linear between the frames
    {
        const int64 fh = (int64) std::lround (0.005 * sr), fwn = 4 * fh;
        const int nfr = (int) (NU / fh) + 2;
        std::vector<double> gfr ((size_t) nfr, 1.0);
        for (int j = 0; j < nfr; ++j)
        {
            const int64 c = j * fh;   // frame centre (index into the output span)
            double eo = 0, es = 0; int cnt = 0;
            const double sc = map.srcAt ((double) (U0 + c));
            for (int64 k = -fwn / 2; k < fwn / 2; ++k)
            {
                const int64 o = c + k; if (o < 0 || o >= NU) continue;
                eo += acc0[(size_t) o] * acc0[(size_t) o] + acc1[(size_t) o] * acc1[(size_t) o];
                const double s0 = w.at (0, (int64) std::llround (sc) + k), s1 = w.at (1, (int64) std::llround (sc) + k);
                es += s0 * s0 + s1 * s1; ++cnt;
            }
            if (cnt > 0 && eo > 1.0e-12 * cnt && es > 1.0e-12 * cnt) gfr[(size_t) j] = juce::jlimit (0.5, 2.0, std::sqrt (es / eo));
        }
        for (int64 i = 0; i < NU; ++i)
        {
            const double x = (double) i / (double) fh; const int j = juce::jmin (nfr - 2, (int) x); const double k = x - j;
            const double g = gfr[(size_t) j] + (gfr[(size_t) j + 1] - gfr[(size_t) j]) * k;
            acc0[(size_t) i] *= g; acc1[(size_t) i] *= g;
        }
    }
    // ---- 3. the source itself where nothing is edited (bit-identical), raised-cosine joins 5 ms off the edits
    std::vector<int64> dist ((size_t) NU);
    {
        const bool globalOn = gC != 0.0 || gF != 0.0;
        for (int64 u = U0; u < U1; ++u)
        {
            const double v = map.srcAt ((double) u);
            const bool edited = globalOn || std::abs (map.rateAt ((double) u) - 1.0) > 1.0e-9 || ! C.neutralAt (v);
            dist[(size_t) (u - U0)] = edited ? 0 : ((int64) 1 << 40);
        }
        for (int64 i = 1; i < NU; ++i) dist[(size_t) i] = std::min (dist[(size_t) i], dist[(size_t) i - 1] + 1);
        for (int64 i = NU - 1; i-- > 0;) dist[(size_t) i] = std::min (dist[(size_t) i], dist[(size_t) i + 1] + 1);
    }
    const double D = std::round (0.005 * sr), X = std::round (0.005 * sr);
    int64 directN = 0;
    for (int64 i = 0; i < len; ++i)
    {
        const int64 u = pc.u0 + i; const size_t o = (size_t) (u - U0);
        const double d = (double) dist[o];
        const float p0 = (float) acc0[o], p1 = (float) acc1[o];
        if (d <= D) { L[i] = p0; R[i] = p1; continue; }
        const int64 idx = (int64) std::llround (map.srcAt ((double) u));
        const float d0 = w.at (0, idx), d1 = w.at (1, idx);
        if (d >= D + X) { L[i] = d0; R[i] = d1; ++directN; continue; }
        const float k = fadeShape ((d - D) / X);
        L[i] = p0 + k * (d0 - p0); R[i] = p1 + k * (d1 - p1);
    }
    if (stats != nullptr)
    {
        stats->epochs = NE; stats->directFrames = directN; stats->voicedMarks = stats->unvoicedMarks = 0;
        for (auto& mk : marks) (mk.voiced ? stats->voicedMarks : stats->unvoicedMarks) += 1;
    }
}
} // namespace gslice::psola
