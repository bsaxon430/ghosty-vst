#include "FormantFix.h"
#include "Lpc.h"

namespace gslice::formantfix
{
std::atomic<bool> enabled { true };

namespace detail
{
bool harmonics (const float* x, int n, double sr, double f0, const analysis::FFT& fft, std::vector<double>& re, std::vector<double>& im, double fmax, Harm& out)
{
    out.f.clear(); out.a.clear(); out.f0 = 0;
    if (! (f0 > 40.0)) return false;
    double e = 0;
    for (int i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * (i + 0.5) / n);
        re[(size_t) i] = x[i] * w; im[(size_t) i] = 0; e += (double) x[i] * x[i];
    }
    if (e < 1.0e-9 * n) return false;
    const double bin = sr / n;
    if (f0 < 2.5 * bin) return false;   // the harmonics would not resolve: the caller falls back to LPC
    fft.transform (re.data(), im.data(), false);
    const int half = n / 2;
    for (int k = 0; k <= half; ++k) re[(size_t) k] = std::sqrt (re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k]);   // |X| in re[0..half]
    auto peak = [&] (double fc, double& pf, double& pa) -> bool
    {
        const int lo = juce::jmax (1, (int) std::floor ((fc - 0.3 * f0) / bin)), hi = juce::jmin (half - 1, (int) std::ceil ((fc + 0.3 * f0) / bin));
        if (hi <= lo) return false;
        int b = lo; for (int k = lo; k <= hi; ++k) if (re[(size_t) k] > re[(size_t) b]) b = k;
        const double y0 = std::log (re[(size_t) b - 1] + 1e-30), y1 = std::log (re[(size_t) b] + 1e-30), y2 = std::log (re[(size_t) b + 1] + 1e-30);
        const double den = y0 - 2 * y1 + y2, d = std::abs (den) > 1e-12 ? juce::jlimit (-0.5, 0.5, 0.5 * (y0 - y2) / den) : 0.0;
        pf = (b + d) * bin; pa = std::exp (y1 - 0.25 * (y0 - y2) * d);
        return true;
    };
    // f0 re-fitted from harmonics 1..4 (a stream's pitch can sit a little off the curve)
    double num = 0, den = 0;
    for (int h = 1; h <= 4; ++h) { double pf = 0, pa = 0; if (peak (h * f0, pf, pa)) { num += pa * pf / h; den += pa; } }
    if (den > 0) f0 = juce::jlimit (0.94 * f0, 1.06 * f0, num / den);
    out.f0 = f0;
    for (int h = 1; h * f0 <= fmax; ++h) { double pf = 0, pa = 0; if (! peak (h * f0, pf, pa)) break; out.f.push_back (pf); out.a.push_back (pa); }
    return out.f.size() >= 3;
}

double bandLimitedAt (const Harm& h, double x, double stretch)
{
    const int H = (int) h.a.size();
    if (H == 0) return 1.0;
    const double a = 3.0, sc = std::max (1.0, stretch), reach = a * sc;
    auto A = [&] (int k) { return h.a[(size_t) juce::jlimit (0, H - 1, k - 1)]; };   // harmonic k (1-based), held beyond the ends
    double num = 0, den = 0;
    for (int k = (int) std::floor (x - reach); k <= (int) std::ceil (x + reach); ++k)
    {
        const double d = (x - k) / sc;
        if (std::abs (d) >= a) continue;
        const double pd = juce::MathConstants<double>::pi * d;
        const double w = d == 0.0 ? 1.0 : a * std::sin (pd) * std::sin (pd / a) / (pd * pd);
        num += w * A (k); den += w;
    }
    return den != 0 ? num / den : A ((int) std::lround (x));
}
} // namespace detail
using namespace detail;

namespace
{
// one pass over the stream (see apply)
void pass (const Piece& pc, const WorkingAudio& w, double pitchSemis, double formantSemis, float* L, float* R, int64 len,
           const std::vector<float>& fw, int64 first, int N, int hop, const Settings& st)
{
    const PitchCurve& C = *pc.curve;
    const double sr = w.rate;
    const int nF = (int) fw.size(), NE = 2 * N;   // the envelope frame: twice the STFT frame (the harmonics of low voices resolve)
    const int order = juce::jlimit (16, 48, (int) std::lround (st.order * sr / 48000.0));
    const double gC = pitchSemis * 100.0, gF = formantSemis * 100.0;
    auto actualAt = [&] (double u) { const double v = pc.map.srcAt (u); return pc.reverse ? pc.mirror - 1.0 - v : v; };
    auto mixAt = [&] (int64 u) -> float   // per sample: linear between the frame centres
    {
        const double x = ((double) u - (double) first - 0.5 * N) / hop;
        if (x <= 0) return fw[0];
        const int i = (int) x; if (i >= nF - 1) return fw[(size_t) nF - 1];
        const float k = (float) (x - i); return fw[(size_t) i] + (fw[(size_t) i + 1] - fw[(size_t) i]) * k;
    };
    analysis::FFT fft (N), fftE (NE);
    std::vector<double> win ((size_t) N);
    for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / N);   // periodic Hann
    std::vector<double> acc0 ((size_t) (len + 2 * N), 0.0), acc1 ((size_t) (len + 2 * N), 0.0), wsum ((size_t) (len + 2 * N), 0.0);
    std::vector<double> re0 ((size_t) N), im0 ((size_t) N), re1 ((size_t) N), im1 ((size_t) N), G ((size_t) N / 2 + 1), Gs ((size_t) N / 2 + 1);
    std::vector<double> reE ((size_t) NE), imE ((size_t) NE), aO ((size_t) order + 1), aS ((size_t) order + 1), scratch ((size_t) (NE + 2 * (order + 1)));
    std::vector<float> fo ((size_t) NE), fsrc ((size_t) NE);
    std::vector<double> gf, gd, ga;   // the gain points: frequency, dB, the output's own power there (the level weight)
    const int nc = juce::jmax (1, w.channels());
    auto yAt = [&] (int ch, int64 u) -> double { const int64 i = u - pc.u0; return i >= 0 && i < len ? (double) (ch == 0 ? L : R)[i] : 0.0; };
    const double fmax = std::min (0.45 * sr, st.maxHz);
    Harm hs, ho;
    for (int j = 0; j < nF; ++j)
    {
        const int64 f0 = first + (int64) j * hop;
        // does any sample of this frame take the correction? (else it would only rebuild the stream: skip it)
        bool needed = false;
        for (int jj = juce::jmax (0, j - 4); jj <= juce::jmin (nF - 1, j + 4) && ! needed; ++jj) needed = fw[(size_t) jj] > 0;
        if (! needed) continue;
        for (int i = 0; i < N; ++i) { re0[(size_t) i] = yAt (0, f0 + i) * win[(size_t) i]; im0[(size_t) i] = 0; re1[(size_t) i] = yAt (1, f0 + i) * win[(size_t) i]; im1[(size_t) i] = 0; }
        std::fill (G.begin(), G.end(), 0.0);   // dB
        if (fw[(size_t) j] > 0)
        {
            const double uc = (double) f0 + 0.5 * N, sc = actualAt (uc);
            float c = 0, fcents = 0, base = 0; C.at (sc, c, fcents, base);
            const double f0src = C.f0At (sc), f0out = f0src * std::pow (2.0, (gC + (double) c) / 1200.0), phi = std::pow (2.0, (gF + (double) fcents) / 1200.0);
            const int64 s0 = (int64) std::llround (sc) - NE / 2, o0 = (int64) std::llround (uc) - NE / 2;
            for (int i = 0; i < NE; ++i)
            {
                double v = 0; for (int ch = 0; ch < nc; ++ch) v += w.at (ch, s0 + i);
                fsrc[(size_t) i] = (float) (v / nc); fo[(size_t) i] = (float) (0.5 * (yAt (0, o0 + i) + yAt (1, o0 + i)));
            }
            gf.clear(); gd.clear(); ga.clear();
            if (harmonics (fsrc.data(), NE, sr, f0src, fftE, reE, imE, fmax, hs) && harmonics (fo.data(), NE, sr, f0out, fftE, reE, imE, fmax * std::min (1.0, phi), ho))
            {   // the gain at each output harmonic: the source's band-limited harmonic envelope there (moved by FORMANT) over its level now
                const double floorA = 1.0e-4 * *std::max_element (hs.a.begin(), hs.a.end());
                for (size_t h = 0; h < ho.f.size(); ++h)
                {
                    const double tgt = std::max (floorA, bandLimitedAt (hs, ho.f[h] / phi / hs.f0, 1.0));
                    gf.push_back (ho.f[h]); gd.push_back (20.0 * std::log10 (tgt / std::max (1e-30, ho.a[h]))); ga.push_back (ho.a[h] * ho.a[h]);
                }
            }
            else
            {   // the fallback: the all-pole envelopes of the two frames (LPC, pre-emphasis, a lag window of f0 / 2)
                const double eS = lpc::analyseFrame (fsrc.data(), NE, order, sr, std::max (60.0, 0.5 * f0src), st.preEmph, aS.data(), scratch.data());
                const double eO = lpc::analyseFrame (fo.data(), NE, order, sr, std::max (60.0, 0.5 * f0out), st.preEmph, aO.data(), scratch.data());
                if (eS > 0 && eO > 0)
                    for (int k = 1; k <= N / 2; ++k)
                    {
                        const double wk = 2.0 * juce::MathConstants<double>::pi * k / N;
                        const double tgt = 1.0 / std::max (1e-12, lpc::magA (aS.data(), order, std::min (juce::MathConstants<double>::pi, wk / phi)));
                        const double cur = 1.0 / std::max (1e-12, lpc::magA (aO.data(), order, wk));
                        gf.push_back (k * sr / N); gd.push_back (20.0 * std::log10 (tgt / cur)); ga.push_back (1.0 / (cur * cur) * eO);
                    }
            }
            if (gf.size() >= 2)
            {
                double eo = 0, ec = 0;   // shape only: the frame keeps its level
                for (size_t h = 0; h < gf.size(); ++h) { eo += ga[h]; ec += ga[h] * std::pow (10.0, gd[h] / 10.0); }
                const double lvl = ec > 0 ? 10.0 * std::log10 (eo / ec) : 0.0;
                for (auto& d : gd) d = juce::jlimit (-st.maxDb, st.maxDb, d + lvl);
                for (int k = 0; k <= N / 2; ++k)
                {
                    const double f = k * sr / N;
                    if (f <= gf.front()) { G[(size_t) k] = gd.front(); continue; }
                    if (f >= gf.back()) { G[(size_t) k] = gd.back(); continue; }
                    // flat around each point (a harmonic's whole main lobe takes its gain), the change in the middle third between them
                    const size_t q = (size_t) (std::upper_bound (gf.begin(), gf.end(), f) - gf.begin());
                    const double u = (f - gf[q - 1]) / (gf[q] - gf[q - 1]), sh = juce::jlimit (0.0, 1.0, (u - 1.0 / 3.0) * 3.0);
                    G[(size_t) k] = gd[q - 1] + (gd[q] - gd[q - 1]) * sh;
                }
            }
        }
        for (int k = 0; k <= N / 2; ++k)   // smoothed over 3 bins (dB), then linear
        {
            double sm = 0; int cnt = 0;
            for (int d = -1; d <= 1; ++d) if (k + d >= 0 && k + d <= N / 2) { sm += G[(size_t) (k + d)]; ++cnt; }
            Gs[(size_t) k] = std::pow (10.0, sm / cnt / 20.0);
        }
        fft.transform (re0.data(), im0.data(), false); fft.transform (re1.data(), im1.data(), false);
        for (int k = 0; k < N; ++k)
        {
            const double g = Gs[(size_t) (k <= N / 2 ? k : N - k)];
            re0[(size_t) k] *= g; im0[(size_t) k] *= g; re1[(size_t) k] *= g; im1[(size_t) k] *= g;
        }
        fft.transform (re0.data(), im0.data(), true); fft.transform (re1.data(), im1.data(), true);
        for (int i = 0; i < N; ++i)
        {
            const int64 o = f0 + i - pc.u0 + N; if (o < 0 || o >= (int64) acc0.size()) continue;
            const double wi = win[(size_t) i];
            acc0[(size_t) o] += wi * re0[(size_t) i] / N; acc1[(size_t) o] += wi * re1[(size_t) i] / N; wsum[(size_t) o] += wi * wi;
        }
    }
    for (int64 i = 0; i < len; ++i)
    {
        const float m = mixAt (pc.u0 + i);
        if (m <= 0.0f) continue;
        const size_t o = (size_t) (i + N);
        if (wsum[o] < 1.0e-6) continue;
        const float c0 = (float) (acc0[o] / wsum[o]), c1 = (float) (acc1[o] / wsum[o]);
        L[i] += m * (c0 - L[i]); R[i] += m * (c1 - R[i]);
    }
}
} // namespace

void apply (const Piece& pc, const WorkingAudio& w, double pitchSemis, double formantSemis, float* L, float* R, int64 len, const std::function<float (int64)>& weightAt, const Settings& st)
{
    if (! enabled.load() || pc.curve == nullptr || len <= 0) return;
    const PitchCurve& C = *pc.curve;
    const double sr = w.rate;
    const int N = analysis::nextPow2 ((int) std::lround (st.fftSize * sr / 48000.0)), hop = juce::jmax (1, (int) std::lround ((double) N * st.hop / st.fftSize));
    auto actualAt = [&] (double u) { const double v = pc.map.srcAt (u); return pc.reverse ? pc.mirror - 1.0 - v : v; };
    // frames: starts first + j * hop, covering [u0, u0 + len) with full overlap; a frame is corrected where the curve shifts or
    // moves the formant on a voiced source frame (x the caller's weight)
    const int64 first = pc.u0 - N + hop;
    const int nF = (int) ((pc.u0 + len - first) / hop) + 1;
    std::vector<float> fw ((size_t) nF, 0.0f);
    bool any = false;
    for (int j = 0; j < nF; ++j)
    {
        const double uc = (double) (first + (int64) j * hop) + 0.5 * N, sc = actualAt (uc);
        float c = 0, f = 0, b = 0; C.at (sc, c, f, b);
        float wt = (std::abs (c) > 0.05f || std::abs (f) > 0.05f) && C.f0At (sc) > 0 ? 1.0f : 0.0f;
        if (wt > 0 && weightAt) wt *= juce::jlimit (0.0f, 1.0f, weightAt ((int64) uc));
        fw[(size_t) j] = wt; any |= wt > 0;
    }
    if (! any) return;
    for (int p = 0; p < juce::jmax (1, st.passes); ++p) pass (pc, w, pitchSemis, formantSemis, L, R, len, fw, first, N, hop, st);
}
} // namespace gslice::formantfix
