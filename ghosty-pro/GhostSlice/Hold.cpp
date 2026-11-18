#include "Hold.h"
#include "Syllables.h"
#include <juce_dsp/juce_dsp.h>
#include <map>
#include <mutex>
#include <complex>

namespace gslice::hold
{
namespace
{
// splitmix64: a portable, seeded stream (the std distributions differ between standard libraries)
struct Rng
{
    uint64 s;
    explicit Rng (uint64 seed) : s (seed * 0x9E3779B97F4A7C15ull + 0x2545F4914F6CDD1Dull) {}
    uint64 next() { uint64 z = (s += 0x9E3779B97F4A7C15ull); z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; return z ^ (z >> 31); }
    double uni() { return (double) (next() >> 11) * (1.0 / 9007199254740992.0); }   // [0, 1)
    double sym() { return uni() * 2.0 - 1.0; }                                         // [-1, 1)
};

inline float monoAt (const SourceAudio& s, int64 i)
{
    if (i < 0 || i >= (int64) s.audio.getNumSamples() || s.audio.getNumChannels() == 0) return 0.0f;
    float v = 0; const int nc = s.audio.getNumChannels();
    for (int c = 0; c < nc; ++c) v += s.audio.getReadPointer (c)[i];
    return v / (float) nc;
}
inline float wAt (const WorkingAudio& w, int ch, double pos) noexcept
{
    const double fl = std::floor (pos); const int64 i = (int64) fl; const double f = pos - fl;
    if (f == 0.0) return w.at (ch, i);
    return (float) ((1.0 - f) * w.at (ch, i) + f * w.at (ch, i + 1));
}

// ---------------------------------------------------------------- the analysis of a held segment (cached: a drag of the held length reuses it)
struct Analysis
{
    std::vector<double> E;          // epochs (SF)
    std::vector<float> rms;         // per epoch: the 2-period grain's RMS
    bool voiced = false; double f0 = 0, T0 = 0;
    double vibRate = 0, vibDepth = 0, vibPhase = 0, vibMean = 0;   // cents(t) = mean + depth sin (2 pi rate t + phase), t from sa (s)
    double meanRms = 0;
};

std::shared_ptr<const Analysis> analyse (const SourceAudio& src, int64 saS, int64 sbS, int64 selAS, int64 selBS)
{
    static std::mutex m; static std::map<juce::String, std::shared_ptr<const Analysis>> cache;
    const juce::String key = src.fingerprint + ":" + juce::String (saS) + ":" + juce::String (sbS) + ":" + juce::String (selAS) + ":" + juce::String (selBS);
    { std::lock_guard<std::mutex> l (m); auto it = cache.find (key); if (it != cache.end()) return it->second; }
    auto A = std::make_shared<Analysis>();
    const double sr = src.sampleRate;
    const int64 pad = (int64) (0.04 * sr);
    const int64 lo = std::max<int64> (std::max<int64> (0, selAS), saS - pad), hi = std::min<int64> (std::min<int64> (src.frames, selBS), sbS + pad);
    const int hop = juce::jmax (16, (int) std::lround (0.005 * sr));
    std::vector<double> ap; const auto f = pitchTrack (src, lo, hi, hop, &ap);
    std::vector<double> inSeg; int nIn = 0;
    for (size_t k = 0; k < f.size(); ++k) { const int64 c = lo + (int64) k * hop; if (c >= saS && c < sbS) { ++nIn; if (f[k] > 0) inSeg.push_back (f[k]); } }
    A->voiced = nIn > 0 && (double) inSeg.size() >= 0.7 * nIn && inSeg.size() >= 3;
    if (A->voiced)
    {
        auto s = inSeg; std::sort (s.begin(), s.end()); A->f0 = s[s.size() / 2]; A->T0 = sr / A->f0;
        // the vibrato: a least-squares sinusoid on the cents track (3.5 .. 8.5 Hz), kept when it explains most of the movement
        std::vector<double> tt, cc;
        for (size_t k = 0; k < f.size(); ++k) { const int64 c = lo + (int64) k * hop; if (c >= saS && c < sbS && f[k] > 0) { tt.push_back ((double) (c - saS) / sr); cc.push_back (1200.0 * std::log2 (f[k] / A->f0)); } }
        const double span = (double) (sbS - saS) / sr;
        if (cc.size() >= 16 && span >= 0.22)
        {
            double mean = 0; for (double c : cc) mean += c; mean /= (double) cc.size();
            double var = 0; for (double c : cc) var += (c - mean) * (c - mean);
            double best = 1e300, br = 0, bA = 0, bB = 0, bm = mean;
            for (double r = 3.5; r <= 8.5 + 1e-9; r += 0.05)
            {   // c = m + a sin + b cos: normal equations (3 x 3)
                double S[3][3] {}, Y[3] {};
                for (size_t k = 0; k < cc.size(); ++k)
                {
                    const double ph = 2.0 * juce::MathConstants<double>::pi * r * tt[k], v[3] { 1.0, std::sin (ph), std::cos (ph) };
                    for (int i = 0; i < 3; ++i) { Y[i] += v[i] * cc[k]; for (int j = 0; j < 3; ++j) S[i][j] += v[i] * v[j]; }
                }
                // solve by Cramer
                auto det3 = [] (double a[3][3]) { return a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]); };
                const double D = det3 (S); if (std::abs (D) < 1e-12) continue;
                double x[3];
                for (int c = 0; c < 3; ++c) { double T[3][3]; for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) T[i][j] = j == c ? Y[i] : S[i][j]; x[c] = det3 (T) / D; }
                double res = 0;
                for (size_t k = 0; k < cc.size(); ++k) { const double ph = 2.0 * juce::MathConstants<double>::pi * r * tt[k]; const double e = cc[k] - (x[0] + x[1] * std::sin (ph) + x[2] * std::cos (ph)); res += e * e; }
                if (res < best) { best = res; br = r; bm = x[0]; bA = x[1]; bB = x[2]; }
            }
            const double depth = std::sqrt (bA * bA + bB * bB);
            if (depth >= 4.0 && best < 0.5 * var) { A->vibRate = br; A->vibDepth = depth; A->vibPhase = std::atan2 (bB, bA); A->vibMean = bm; }
            else A->vibMean = mean;
        }
        // epochs: the positive peaks of the (zero-phase) low-passed signal, one per period, each searched 0.8 .. 1.2 periods on
        const int64 a = std::max<int64> (0, lo - (int64) (2 * A->T0)), b = std::min<int64> (src.frames, hi + (int64) (2 * A->T0));
        std::vector<double> y ((size_t) std::max<int64> (1, b - a));
        for (int64 i = a; i < b; ++i) y[(size_t) (i - a)] = monoAt (src, i);
        const double fc = juce::jlimit (60.0, 0.4 * sr, 1.5 * A->f0), k1 = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * fc / sr);
        for (int pass = 0; pass < 2; ++pass)
        {
            double z = 0; for (auto& v : y) { z += k1 * (v - z); v = z; }
            z = 0; for (size_t i = y.size(); i-- > 0;) { z += k1 * (y[i] - z); y[i] = z; }
        }
        auto Tat = [&] (int64 pos) { const int64 k = juce::jlimit<int64> (0, (int64) f.size() - 1, (pos - lo + hop / 2) / hop); double fk = f.empty() ? 0 : f[(size_t) k];
                                     for (int64 d = 1; fk <= 0 && d < (int64) f.size(); ++d) { if (k - d >= 0 && f[(size_t) (k - d)] > 0) fk = f[(size_t) (k - d)]; else if (k + d < (int64) f.size() && f[(size_t) (k + d)] > 0) fk = f[(size_t) (k + d)]; }
                                     return fk > 0 ? sr / fk : A->T0; };
        auto argmax = [&] (int64 from, int64 to) { from = std::max (from, a); to = std::min (to, b - 1); int64 best2 = from; double bv = -1e30;
                                                   for (int64 i = from; i <= to; ++i) if (y[(size_t) (i - a)] > bv) { bv = y[(size_t) (i - a)]; best2 = i; } return best2; };
        int64 e = argmax (lo, lo + (int64) std::ceil (Tat (lo)));
        while (e <= hi)
        {
            A->E.push_back ((double) e);
            const double T = Tat (e);
            const int64 ne = argmax (e + (int64) std::floor (0.8 * T), e + (int64) std::ceil (1.2 * T));
            if (ne <= e) break;
            e = ne;
        }
    }
    else
    {   // unvoiced: a 10 ms grain grid (granular), no pitch to follow
        A->T0 = std::max (16.0, std::round (0.01 * sr));
        for (double e = (double) lo; e <= (double) hi; e += A->T0) A->E.push_back (e);
    }
    // the grains' loudness
    double sum = 0; int cnt = 0;
    for (size_t i = 0; i < A->E.size(); ++i)
    {
        const int64 c = (int64) A->E[i], T = (int64) std::max (8.0, i + 1 < A->E.size() ? A->E[i + 1] - A->E[i] : A->T0);
        double e2 = 0; for (int64 k = -T; k < T; ++k) { const double w = 0.5 + 0.5 * std::cos (juce::MathConstants<double>::pi * (double) k / (double) T); const double v = monoAt (src, c + k) * w; e2 += v * v; }
        const float r = (float) std::sqrt (e2 / (double) (2 * T));
        A->rms.push_back (r);
        if (A->E[i] >= (double) saS && A->E[i] <= (double) sbS) { sum += r; ++cnt; }
    }
    A->meanRms = cnt > 0 ? sum / cnt : 0;
    std::lock_guard<std::mutex> l (m);
    if (cache.size() > 64) cache.clear();
    cache[key] = A;
    return A;
}

// the normalised correlation of two grains (mono, 2 periods around their epochs)
double grainCorr (const SourceAudio& src, double ei, double ej, double T)
{
    const int64 h = (int64) std::max (8.0, T);
    double xy = 0, xx = 0, yy = 0;
    for (int64 k = -h; k < h; k += 2) { const double x = monoAt (src, (int64) ei + k), y = monoAt (src, (int64) ej + k); xy += x * y; xx += x * x; yy += y * y; }
    return xx > 0 && yy > 0 ? xy / std::sqrt (xx * yy) : 0.0;
}
} // namespace

// ---------------------------------------------------------------- YIN
double yin (const float* x, int n, double sr, double fmin, double fmax, double* aper)
{
    if (aper) *aper = 1.0;
    const int tauMax = juce::jmin (n / 2, (int) std::ceil (sr / fmin)), tauMin = juce::jmax (2, (int) std::floor (sr / fmax));
    if (tauMax <= tauMin + 2) return 0;
    const int W = n - tauMax;
    double energy = 0; for (int j = 0; j < n; ++j) energy += (double) x[j] * x[j];
    if (energy < 1e-10 * n) return 0;
    std::vector<double> d ((size_t) tauMax + 2, 0.0), cm ((size_t) tauMax + 2, 1.0);
    for (int tau = 1; tau <= tauMax + 1 && tau < n - W + 1; ++tau)
    {
        double s = 0; for (int j = 0; j < W; ++j) { const double df = (double) x[j] - x[j + tau]; s += df * df; }
        d[(size_t) tau] = s;
    }
    double run = 0;
    for (int tau = 1; tau <= tauMax; ++tau) { run += d[(size_t) tau]; cm[(size_t) tau] = run > 0 ? d[(size_t) tau] * tau / run : 1.0; }
    int tb = -1;
    for (int tau = tauMin; tau < tauMax; ++tau)
        if (cm[(size_t) tau] < 0.15) { while (tau + 1 < tauMax && cm[(size_t) tau + 1] < cm[(size_t) tau]) ++tau; tb = tau; break; }
    if (tb < 0) { double mn = 1e30; for (int tau = tauMin; tau < tauMax; ++tau) if (cm[(size_t) tau] < mn) { mn = cm[(size_t) tau]; tb = tau; } }
    if (tb <= 1) return 0;
    if (aper) *aper = cm[(size_t) tb];
    if (cm[(size_t) tb] > 0.4) return 0;
    // parabolic interpolation of the dip
    const double a = cm[(size_t) tb - 1], b = cm[(size_t) tb], c = cm[(size_t) tb + 1];
    const double den = a - 2 * b + c; double t = (double) tb;
    if (std::abs (den) > 1e-12) t += juce::jlimit (-0.5, 0.5, 0.5 * (a - c) / den);
    return sr / t;
}

std::vector<double> pitchTrack (const SourceAudio& src, int64 a, int64 b, int hop, std::vector<double>* aper)
{
    std::vector<double> out; if (aper) aper->clear();
    const double sr = src.sampleRate;
    const int n = juce::jmax (256, (int) std::lround (2.0 * sr / 60.0));
    std::vector<float> buf ((size_t) n);
    for (int64 c = a; c < b; c += hop)
    {
        for (int i = 0; i < n; ++i) buf[(size_t) i] = monoAt (src, c - n / 2 + i);
        double ap = 1; out.push_back (yin (buf.data(), n, sr, 60.0, 1000.0, &ap));
        if (aper) aper->push_back (ap);
    }
    return out;
}

// ---------------------------------------------------------------- the stable region
Stable pickStable (const SourceAudio& src, int64 selA, int64 selB)
{
    Stable st; st.a = selA; st.b = selB;
    const double sr = src.sampleRate;
    selA = juce::jlimit<int64> (0, src.frames, selA); selB = juce::jlimit<int64> (selA, src.frames, selB);
    const int hop = juce::jmax (16, (int) std::lround (0.01 * sr));
    const int nf = (int) ((selB - selA) / hop);
    if (nf < 10) { st.a = selA + (selB - selA) / 4; st.b = selB - (selB - selA) / 4; st.score = 0; st.unstable = true; return st; }
    std::vector<double> ap; const auto f = pitchTrack (src, selA + hop / 2, selA + hop / 2 + (int64) nf * hop, hop, &ap);
    juce::dsp::FFT fft (10); const int N = 1024; std::vector<float> fb ((size_t) N * 2), prev ((size_t) N / 2 + 1, 0.0f);
    std::vector<double> S ((size_t) nf, 0.0); double prevDb = 0;
    for (int k = 0; k < nf; ++k)
    {
        const int64 c = selA + hop / 2 + (int64) k * hop;
        double e = 0;
        std::fill (fb.begin(), fb.end(), 0.0f);
        for (int i = 0; i < N; ++i) { const float v = monoAt (src, c - N / 2 + i); e += (double) v * v; fb[(size_t) i] = v * (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / N)); }
        fft.performFrequencyOnlyForwardTransform (fb.data(), true);
        double flux = 0, tot = 0;
        for (int i = 0; i <= N / 2; ++i) { tot += fb[(size_t) i]; flux += std::max (0.0f, fb[(size_t) i] - prev[(size_t) i]); prev[(size_t) i] = fb[(size_t) i]; }
        const double fluxN = k == 0 ? 0.0 : juce::jlimit (0.0, 1.0, tot > 0 ? flux / tot : 1.0);
        const double db = 10.0 * std::log10 (e / N + 1e-12);
        const double dE = k == 0 ? 0.0 : juce::jlimit (0.0, 1.0, std::abs (db - prevDb) / 6.0); prevDb = db;
        const double V = f[(size_t) k] > 0 ? juce::jlimit (0.0, 1.0, 1.0 - ap[(size_t) k] / 0.4) : 0.0;
        double dc = 1.0;
        if (k > 0 && f[(size_t) k] > 0 && f[(size_t) k - 1] > 0) dc = std::abs (1200.0 * std::log2 (f[(size_t) k] / f[(size_t) k - 1])) / 0.01;
        else if (f[(size_t) k] > 0) dc = 0; else dc = 300;
        const double silent = db < -60 ? 0.0 : 1.0;
        S[(size_t) k] = silent * (0.35 * V + 0.25 * (1.0 - juce::jlimit (0.0, 1.0, dc / 300.0)) + 0.2 * (1.0 - fluxN) + 0.2 * (1.0 - dE));
    }
    const int margin = nf >= 8 + 6 ? 3 : 0;
    const int minL = juce::jmin (8, nf - 2 * margin), maxL = juce::jmin (30, nf - 2 * margin);
    double best = -1; int bi = margin, bl = juce::jmax (1, minL);
    std::vector<double> pre ((size_t) nf + 1, 0.0); for (int k = 0; k < nf; ++k) pre[(size_t) k + 1] = pre[(size_t) k] + S[(size_t) k];
    for (int L = juce::jmax (1, minL); L <= maxL; ++L)
        for (int i = margin; i + L <= nf - margin; ++i)
        {
            const double m = (pre[(size_t) (i + L)] - pre[(size_t) i]) / L + 0.002 * L;
            if (m > best) { best = m; bi = i; bl = L; }
        }
    st.a = selA + (int64) bi * hop; st.b = selA + (int64) (bi + bl) * hop;
    st.score = (pre[(size_t) (bi + bl)] - pre[(size_t) bi]) / bl;
    st.unstable = st.score < 0.5;
    return st;
}

Stable pickStable (const SourceAudio& src, int64 selA, int64 selB, const std::vector<analysis::Syllable>* syllables)
{
    if (syllables != nullptr && ! syllables->empty())
        if (const auto* s = analysis::syllableAt (*syllables, selA, selB))
        {
            const int64 edge = (int64) std::lround (0.03 * src.sampleRate);
            const int64 a = std::max (selA, s->vowelStart + edge), b = std::min (selB, s->vowelEnd - edge);
            if (b - a >= (int64) std::lround (0.1 * src.sampleRate))
            {
                auto st = pickStable (src, a, b);
                st.inVowel = true;
                return st;
            }
            if (b - a >= (int64) std::lround (0.04 * src.sampleRate))
            {   // a short vowel: its core is the stable region
                Stable st; st.a = a; st.b = b; st.score = 0.6; st.inVowel = true;
                return st;
            }
        }
    return pickStable (src, selA, selB);
}

// ---------------------------------------------------------------- the plan of one held segment
std::shared_ptr<const HoldPlan> makePlan (const SourceAudio* src, double kS, double hostRate, const HoldOp& op, int64 uStart, int64 len,
                                          double sa, double sb, double selA, double selB, bool joinIn, bool joinOut, bool pitched)
{
    auto P = std::make_shared<HoldPlan>();
    P->chosen = op.mode; P->mode = pitched || src == nullptr ? HoldMode::Elastic : op.mode;
    P->holdId = op.id; P->uStart = uStart; P->len = len; P->sa = sa; P->sb = sb; P->selA = selA; P->selB = selB;
    P->joinIn = joinIn && ! pitched; P->joinOut = joinOut && ! pitched;
    P->xf = juce::jmax<int64> (1, juce::jmin<int64> ((int64) std::lround (0.03 * hostRate), len / 3));
    P->smear = juce::jlimit (0.05, 1.0, op.smear); P->driftCents = juce::jlimit (0.0, 50.0, op.driftCents); P->seed = op.seed;
    P->rate = hostRate;
    if (P->mode == HoldMode::Elastic && P->joinOut && src != nullptr && src->channels > 0)
    {   // the out join's phase steer needs the period at the end of the held segment (YIN on the last 40 ms of the stable region)
        const int64 sbS = (int64) std::llround (sb / kS), W = (int64) std::lround (0.04 * src->sampleRate), a = std::max<int64> (0, sbS - W);
        if (sbS - a >= W / 2 && sbS <= src->frames)
        {
            std::vector<float> m ((size_t) (sbS - a), 0.0f);
            for (int c = 0; c < src->channels; ++c) { const float* x = src->audio.getReadPointer (c); for (int64 i = a; i < sbS; ++i) m[(size_t) (i - a)] += x[i] / (float) src->channels; }
            double ap = 1.0; const double f = yin (m.data(), (int) m.size(), src->sampleRate, 60.0, 1000.0, &ap);
            if (f > 0 && ap < 0.3) { P->f0 = f; P->voiced = true; }
        }
    }
    if (P->mode != HoldMode::Elastic && src != nullptr && len > 8)
    {
        const double sr = src->sampleRate;
        const int64 saS = (int64) std::llround (sa / kS), sbS = (int64) std::llround (sb / kS);
        const auto A = analyse (*src, saS, sbS, (int64) std::llround (selA / kS), (int64) std::llround (selB / kS));
        P->voiced = A->voiced; P->f0 = A->f0; P->vibRate = A->vibRate; P->vibDepth = A->vibDepth; P->srcRms = A->meanRms;
        const auto& E = A->E;
        int s = -1, e = -1;
        for (int i = 0; i < (int) E.size(); ++i) { if (s < 0 && E[(size_t) i] >= (double) saS + 1) s = i; if (E[(size_t) i] <= (double) sbS - 1) e = i; }
        const double lenS = (double) len / kS, extraS = lenS - (double) (sbS - saS);
        if (s >= 0 && e - s >= 3 && extraS > -0.5)
        {
            Rng rng (((uint64) op.seed << 20) ^ (uint64) op.id ^ 0x5157u);
            std::vector<std::pair<double, int>> sched;   // (output position SF from sa, epoch index); gain from the index
            sched.push_back ({ E[(size_t) s] - (double) saS, s }); sched.push_back ({ E[(size_t) s + 1] - (double) saS, s + 1 });
            const double oS1 = E[(size_t) s + 1] - (double) saS, oE1 = E[(size_t) e - 1] - (double) saS + extraS;
            if (extraS < 1.0)
            {   // (no extra length: the source's own grains, in order)
                for (int i = s + 2; i <= e - 2; ++i) sched.push_back ({ E[(size_t) i] - (double) saS, i });
            }
            else if (oE1 > oS1 + 1.0)
            {
                // the target frequency over the random part
                const double fStart = sr / (E[(size_t) s + 1] - E[(size_t) s]), fEnd = sr / (E[(size_t) e] - E[(size_t) e - 1]);
                const double fc = A->voiced ? A->f0 * std::exp2 (A->vibMean / 1200.0) : sr / A->T0;
                const double spanS = (oE1 - oS1) / sr;
                double rr = 0, ph0 = 0;
                if (A->vibDepth > 0)
                {
                    const double twoPi = juce::MathConstants<double>::twoPi;
                    ph0 = twoPi * A->vibRate * (E[(size_t) s + 1] - (double) saS) / sr + A->vibPhase;
                    const double ph1 = twoPi * A->vibRate * (E[(size_t) e - 1] - (double) saS) / sr + A->vibPhase;
                    double K = std::round (A->vibRate * spanS - (ph1 - ph0) / twoPi);
                    rr = (ph1 - ph0 + twoPi * K) / (twoPi * spanS);
                    if (rr < 0.5 * A->vibRate) rr = (ph1 - ph0 + twoPi * (K + 1)) / (twoPi * spanS);
                }
                auto vib = [&] (double v) { return A->vibDepth > 0 ? A->vibDepth * std::sin (ph0 + juce::MathConstants<double>::twoPi * rr * (v - oS1) / sr) : 0.0; };
                const double cS = juce::jlimit (-100.0, 100.0, 1200.0 * std::log2 (fStart / (fc * std::exp2 (vib (oS1) / 1200.0))));
                const double cE = juce::jlimit (-100.0, 100.0, 1200.0 * std::log2 (fEnd / (fc * std::exp2 (vib (oE1) / 1200.0))));
                // drift: cosine-interpolated random points every 0.4 s, faded in / out over 0.15 s
                const double step = 0.4 * sr; std::vector<double> dp; for (double v = 0; v <= (oE1 - oS1) + 2 * step; v += step) dp.push_back (rng.sym() * P->driftCents);
                auto drift = [&] (double v)
                {
                    const double x = (v - oS1) / step; const int i = juce::jlimit (0, (int) dp.size() - 2, (int) std::floor (x)); const double fr = juce::jlimit (0.0, 1.0, x - i);
                    const double w = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * fr);
                    const double edge = juce::jlimit (0.0, 1.0, std::min (v - oS1, oE1 - v) / (0.15 * sr));
                    return (dp[(size_t) i] * (1 - w) + dp[(size_t) i + 1] * w) * edge * edge * (3 - 2 * edge);
                };
                const double tau = 0.04 * sr;
                auto freq = [&] (double v) { return fc * std::exp2 ((vib (v) + cS * std::exp (-(v - oS1) / tau) + cE * std::exp (-(oE1 - v) / tau) + drift (v)) / 1200.0); };
                double Phi = 0; for (double v = oS1; v < oE1; v += 1.0) Phi += freq (v) * std::min (1.0, oE1 - v) / sr;
                const int K = juce::jmax (1, (int) std::lround (Phi));
                const double scale = (double) K / Phi;
                // grain choice: a constrained random walk over the stable epochs (never a sequential loop)
                const int lo = s + 1, hi = e - 1;
                const int back = juce::jmax (1, (int) std::lround (2.0 * op.variation)), fwd = juce::jmax (1, (int) std::lround (3.0 * op.variation));
                std::map<std::pair<int, int>, double> cc;
                auto corr = [&] (int i, int j) { const auto k = std::make_pair (std::min (i, j), std::max (i, j)); auto it = cc.find (k); if (it != cc.end()) return it->second;
                                                 const double c = grainCorr (*src, E[(size_t) i], E[(size_t) j], A->T0); cc[k] = c; return c; };
                std::vector<double> nb; for (int i = lo; i < hi; ++i) nb.push_back (corr (i, i + 1));
                std::sort (nb.begin(), nb.end());
                const double th = nb.empty() ? -1.0 : nb[(size_t) ((double) (nb.size() - 1) * 0.2)] - 0.02;
                auto reflect = [lo, hi] (int j) { const int span = hi - lo; if (span <= 0) return lo; int x = j - lo; const int per = 2 * span; x = ((x % per) + per) % per; return lo + (x <= span ? x : per - x); };
                int cur = s + 1; double acc = 0; int k = 1;
                for (double v = oS1; v < oE1 && k < K; v += 1.0)
                {
                    const double inc = freq (v) * scale * std::min (1.0, oE1 - v) / sr;
                    if (acc + inc >= (double) k)
                    {
                        const double pos = v + ((double) k - acc) / inc;
                        // the next grain: near steps (biased forward, similar grains) or a jump to a grain whose own period
                        // matches the target here (so the vibrato / drift the spacing makes is what the grains carry too)
                        const double Tt = sr / (freq (pos) * scale);
                        auto periodOf = [&] (int j) { return 0.5 * (E[(size_t) juce::jmin ((int) E.size() - 1, j + 1)] - E[(size_t) juce::jmax (0, j - 1)]); };
                        auto match = [&] (int j) { const double c = 1200.0 * std::log2 (periodOf (j) / Tt); return std::exp (-(c * c) / 25.0); };
                        int cand[256]; double wts[256]; int nc = 0; double wsum = 0;
                        for (int st2 = -back; st2 <= fwd && nc < 256; ++st2)
                        {
                            if (st2 == 0) continue;
                            const int j = reflect (cur + st2); if (j == cur) continue;
                            if (corr (cur, j) < th) continue;
                            cand[nc] = j; wts[nc] = (st2 > 0 ? 1.6 : 1.0) * (0.05 + match (j)); wsum += wts[nc]; ++nc;
                        }
                        if (A->voiced)
                            for (int j = lo; j <= hi && nc < 256; ++j)
                            {
                                if (std::abs (j - cur) <= fwd) continue;
                                const double mw = match (j); if (mw < 0.3) continue;
                                cand[nc] = j; wts[nc] = 0.5 * mw; wsum += wts[nc]; ++nc;
                            }
                        if (nc == 0) { cand[0] = reflect (cur + 1); wts[0] = 1; wsum = 1; nc = 1; }
                        double r = rng.uni() * wsum; int pick = cand[nc - 1];
                        for (int q = 0; q < nc; ++q) { if (r < wts[q]) { pick = cand[q]; break; } r -= wts[q]; }
                        cur = pick;
                        sched.push_back ({ pos, cur });
                        ++k;
                    }
                    acc += inc;
                }
            }
            sched.push_back ({ E[(size_t) e - 1] - (double) saS + std::max (0.0, extraS), e - 1 });
            sched.push_back ({ E[(size_t) e] - (double) saS + std::max (0.0, extraS), e });
            const size_t nSeqEnd = 2, nSeqStart = 2;
            for (size_t q = 0; q < sched.size(); ++q)
            {
                Grain g; g.out = (int64) std::llround (sched[q].first * kS); g.src = E[(size_t) sched[q].second] * kS;
                const bool seq = q < nSeqStart || q + nSeqEnd >= sched.size() || extraS < 1.0;
                const float r = A->rms[(size_t) sched[q].second];
                g.gain = seq || r <= 0 ? 1.0f : (float) juce::jlimit (0.7, 1.4, A->meanRms / r);
                if (! P->grains.empty() && g.out <= P->grains.back().out) continue;
                P->grains.push_back (g);
            }
            if (P->grains.size() < 4 || P->grains.back().out >= len) P->grains.clear();
        }
    }
    Hasher h; h.add ("hold").add ((int) P->mode).add ((int) P->chosen).add (P->uStart).add (P->len).add (P->sa).add (P->sb).add (P->selA).add (P->selB)
               .add (P->joinIn).add (P->joinOut).add (P->xf).add (P->smear).add (P->driftCents).add ((int64) P->seed).add ((int) P->grains.size());
    for (auto& g : P->grains) h.add (g.out).add (g.src).add (g.gain);
    P->hash = h.key();
    return P;
}

// ---------------------------------------------------------------- rendering
void renderGrains (const HoldPlan& P, const WorkingAudio& w, int64 v0, float* L, float* R, int n) noexcept
{
    const auto& G = P.grains;
    if (G.empty()) { for (int i = 0; i < n; ++i) { L[i] = wAt (w, 0, P.sa + (double) (v0 + i)); R[i] = wAt (w, 1, P.sa + (double) (v0 + i)); } return; }
    const int64 first = G.front().out, last = G.back().out;
    size_t k = 0;
    {
        auto it = std::upper_bound (G.begin(), G.end(), v0, [] (int64 v, const Grain& g) { return v < g.out; });
        k = it == G.begin() ? 0 : (size_t) (it - G.begin()) - 1;
    }
    for (int i = 0; i < n; ++i)
    {
        const int64 v = v0 + i;
        if (v < first) { L[i] = wAt (w, 0, P.sa + (double) v); R[i] = wAt (w, 1, P.sa + (double) v); continue; }
        if (v >= last) { const double p = G.back().src + (double) (v - last); L[i] = wAt (w, 0, p); R[i] = wAt (w, 1, p); continue; }
        while (k + 2 < G.size() && G[k + 1].out <= v) ++k;
        const auto& a = G[k]; const auto& b = G[k + 1];
        const double x = (double) (v - a.out) / (double) (b.out - a.out);
        const float wa = (float) (0.5 + 0.5 * std::cos (juce::MathConstants<double>::pi * x)), wb = 1.0f - wa;
        const double pa = a.src + (double) (v - a.out), pb = b.src + (double) (v - b.out);
        const float ga = wa * a.gain, gb = wb * b.gain;
        L[i] = ga * wAt (w, 0, pa) + gb * wAt (w, 0, pb);
        R[i] = ga * wAt (w, 1, pa) + gb * wAt (w, 1, pb);
    }
}

void applyJoins (const HoldPlan& P, const WorkingAudio& w, int64 v0, float* L, float* R, int n) noexcept
{
    if (P.mode == HoldMode::Sustain && ! P.grains.empty()) return;
    const double xf = (double) P.xf;
    for (int i = 0; i < n; ++i)
    {
        const int64 v = v0 + i;
        if (P.joinIn && v < P.xf)
        {
            const float ws = 1.0f - fadeShape ((double) v / xf);
            const double p = P.sa + (double) v;
            L[i] += ws * (wAt (w, 0, p) - L[i]); R[i] += ws * (wAt (w, 1, p) - R[i]);
        }
        if (P.joinOut && v >= P.len - P.xf && v < P.len)
        {
            const float we = fadeShape ((double) (v - (P.len - P.xf) + 1) / xf);
            const double p = P.sb - (double) P.len + (double) v;
            L[i] += we * (wAt (w, 0, p) - L[i]); R[i] += we * (wAt (w, 1, p) - R[i]);
        }
    }
}

namespace
{
inline double princarg (double x) { const double tp = juce::MathConstants<double>::twoPi; return x - tp * std::round (x / tp); }

// TEXTURE: magnitude frames captured over the stable region, interpolated by a slow random walk; peaks keep their partials
// (identity phase locking), the other bins advance with SMEAR randomisation
bool texture (const HoldPlan& P, const WorkingAudio& w, juce::AudioBuffer<float>& out, const std::function<bool()>& cancel)
{
    const double rate = w.rate;
    const int order = juce::jlimit (10, 14, (int) std::lround (std::log2 (0.085 * rate)));
    const int N = 1 << order, hop = N / 4, nb = N / 2 + 1;
    juce::dsp::FFT fft (order);
    std::vector<float> win ((size_t) N), buf ((size_t) N * 2);
    for (int i = 0; i < N; ++i) win[(size_t) i] = (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / N));
    // the round trip scale of this FFT engine (measured, so any engine works)
    double rt = 1.0;
    { std::fill (buf.begin(), buf.end(), 0.0f); buf[1] = 1.0f; fft.performRealOnlyForwardTransform (buf.data()); fft.performRealOnlyInverseTransform (buf.data()); rt = std::abs (buf[1]) > 1e-9 ? 1.0 / buf[1] : 1.0; }
    constexpr int M = 6;
    struct Cap { std::vector<float> mag; std::vector<float> ph; std::vector<float> inst; };
    std::vector<Cap> cap ((size_t) M * 2);
    const double span = std::max (0.0, P.sb - P.sa - N);
    for (int m = 0; m < M; ++m)
        for (int ch = 0; ch < 2; ++ch)
        {
            const double c0 = span > 0 ? P.sa + span * m / (M - 1) : 0.5 * (P.sa + P.sb) - N / 2;
            auto& C = cap[(size_t) (m * 2 + ch)]; C.mag.assign ((size_t) nb, 0); C.ph.assign ((size_t) nb, 0); C.inst.assign ((size_t) nb, 0);
            std::vector<float> ph2 ((size_t) nb);
            for (int pass = 0; pass < 2; ++pass)
            {
                std::fill (buf.begin(), buf.end(), 0.0f);
                for (int i = 0; i < N; ++i) buf[(size_t) i] = wAt (w, ch, std::round (c0) + (double) (pass * hop + i)) * win[(size_t) i];
                fft.performRealOnlyForwardTransform (buf.data());
                for (int k = 0; k < nb; ++k)
                {
                    const std::complex<float> X (buf[(size_t) (2 * k)], buf[(size_t) (2 * k + 1)]);
                    if (pass == 0) { C.mag[(size_t) k] = std::abs (X); C.ph[(size_t) k] = std::arg (X); } else ph2[(size_t) k] = std::arg (X);
                }
            }
            for (int k = 0; k < nb; ++k)
            {
                const double om = juce::MathConstants<double>::twoPi * k / N;
                C.inst[(size_t) k] = (float) (om + princarg ((double) ph2[(size_t) k] - C.ph[(size_t) k] - om * hop) / hop);
            }
        }
    const int64 len = P.len;
    juce::AudioBuffer<float> acc (2, (int) (len + 2 * N)); acc.clear();
    const int64 off = N;   // output index t is at acc[t + off]
    for (int ch = 0; ch < 2; ++ch)
    {
        Rng rng (((uint64) P.seed << 8) ^ (uint64) (ch + 1) * 0x9E37ull ^ P.hash);
        std::vector<double> phase ((size_t) nb, 0.0);
        for (int k = 0; k < nb; ++k) phase[(size_t) k] = cap[(size_t) ch].ph[(size_t) k];
        double wpos = 0, wvel = 0;
        std::vector<float> mag ((size_t) nb); std::vector<int> peakOf ((size_t) nb);
        for (int64 j = -4; j * hop < len + N; ++j)
        {
            if (cancel && (j & 31) == 0 && cancel()) return false;
            wvel = 0.92 * wvel + 0.08 * rng.sym() * (M - 1) * 0.6 * hop / rate;
            wpos += wvel; if (wpos < 0) { wpos = -wpos; wvel = -wvel; } if (wpos > M - 1) { wpos = 2 * (M - 1) - wpos; wvel = -wvel; }
            const int m0 = juce::jlimit (0, M - 2, (int) std::floor (wpos)); const float fr = (float) juce::jlimit (0.0, 1.0, wpos - m0);
            const auto& A = cap[(size_t) (m0 * 2 + ch)]; const auto& B = cap[(size_t) ((m0 + 1) * 2 + ch)];
            float mx = 0; for (int k = 0; k < nb; ++k) { mag[(size_t) k] = A.mag[(size_t) k] * (1 - fr) + B.mag[(size_t) k] * fr; mx = std::max (mx, mag[(size_t) k]); }
            // peaks and the bins locked to them (+-1)
            std::fill (peakOf.begin(), peakOf.end(), -1);
            for (int k = 2; k < nb - 2; ++k)
                if (mag[(size_t) k] > mx * 1e-3f && mag[(size_t) k] >= mag[(size_t) k - 1] && mag[(size_t) k] >= mag[(size_t) k + 1] && mag[(size_t) k] >= mag[(size_t) k - 2] && mag[(size_t) k] >= mag[(size_t) k + 2])
                    for (int d = -1; d <= 1; ++d) peakOf[(size_t) (k + d)] = k;
            for (int k = 0; k < nb; ++k)
                if (peakOf[(size_t) k] == k) phase[(size_t) k] += A.inst[(size_t) k] * hop;
            for (int k = 0; k < nb; ++k)
            {
                const int p = peakOf[(size_t) k];
                if (p == k) continue;
                if (p >= 0) phase[(size_t) k] = phase[(size_t) p] + (A.ph[(size_t) k] - A.ph[(size_t) p]);
                else phase[(size_t) k] += juce::MathConstants<double>::twoPi * k / N * hop + P.smear * juce::MathConstants<double>::pi * rng.sym();
                phase[(size_t) k] = princarg (phase[(size_t) k]);
            }
            for (int k = 0; k < nb; ++k) { buf[(size_t) (2 * k)] = mag[(size_t) k] * (float) std::cos (phase[(size_t) k]); buf[(size_t) (2 * k + 1)] = mag[(size_t) k] * (float) std::sin (phase[(size_t) k]); }
            for (int k = 2 * nb; k < 2 * N; ++k) buf[(size_t) k] = 0.0f;
            fft.performRealOnlyInverseTransform (buf.data());
            const int64 t0 = j * hop - N / 2;
            float* y = acc.getWritePointer (ch);
            for (int i = 0; i < N; ++i)
            {
                const int64 t = t0 + i + off; if (t < 0 || t >= acc.getNumSamples()) continue;
                y[t] += (float) (buf[(size_t) i] * rt * win[(size_t) i] / 1.5);
            }
        }
    }
    out.setSize (2, (int) len); out.clear();
    for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, 0, acc, ch, (int) off, (int) len);
    return true;
}

// FREEZE: Paulstretch-style (very long windows, a fresh random phase for every bin of every frame)
bool freeze (const HoldPlan& P, const WorkingAudio& w, juce::AudioBuffer<float>& out, const std::function<bool()>& cancel)
{
    const double rate = w.rate;
    const double avail = std::max (2048.0, P.selB - P.selA);
    int order = juce::jlimit (11, 16, (int) std::floor (std::log2 (std::min (avail, 0.7 * rate))));
    const int N = 1 << order, hop = N / 4, nb = N / 2 + 1;
    juce::dsp::FFT fft (order);
    std::vector<float> win ((size_t) N), buf ((size_t) N * 2);
    for (int i = 0; i < N; ++i) { const double x = 2.0 * i / N - 1.0; win[(size_t) i] = (float) std::pow (std::max (0.0, 1.0 - x * x), 1.25); }
    double rt = 1.0;
    { std::fill (buf.begin(), buf.end(), 0.0f); buf[1] = 1.0f; fft.performRealOnlyForwardTransform (buf.data()); fft.performRealOnlyInverseTransform (buf.data()); rt = std::abs (buf[1]) > 1e-9 ? 1.0 / buf[1] : 1.0; }
    // overlap-add gain of w^2 at this hop (averaged over one hop)
    double ola = 0; for (int i = 0; i < hop; ++i) { double s = 0; for (int k = i; k < N; k += hop) s += (double) win[(size_t) k] * win[(size_t) k]; ola += s; } ola /= hop;
    const int64 len = P.len;
    juce::AudioBuffer<float> acc (2, (int) (len + 2 * N)); acc.clear();
    const int64 off = N;
    const double lo = P.selA, hi = std::max (P.selA, P.selB - N);
    for (int ch = 0; ch < 2; ++ch)
    {
        Rng rng (((uint64) P.seed << 9) ^ (uint64) (ch + 7) * 0x51EDull ^ P.hash);
        Rng walk (((uint64) P.seed << 3) ^ P.hash ^ 0xF00Dull);   // the same read position for both channels
        double pos = 0.5 * (lo + hi);
        for (int64 j = -4; j * hop < len + N; ++j)
        {
            if (cancel && (j & 15) == 0 && cancel()) return false;
            pos += walk.sym() * 0.08 * N; if (pos < lo) pos = 2 * lo - pos; if (pos > hi) pos = 2 * hi - pos; pos = juce::jlimit (lo, hi, pos);
            std::fill (buf.begin(), buf.end(), 0.0f);
            const double p0 = std::round (pos);
            for (int i = 0; i < N; ++i) buf[(size_t) i] = wAt (w, ch, p0 + i) * win[(size_t) i];
            fft.performRealOnlyForwardTransform (buf.data());
            for (int k = 0; k < nb; ++k)
            {
                const float m = std::hypot (buf[(size_t) (2 * k)], buf[(size_t) (2 * k + 1)]);
                const double ph = rng.uni() * juce::MathConstants<double>::twoPi;
                buf[(size_t) (2 * k)] = m * (float) std::cos (ph); buf[(size_t) (2 * k + 1)] = m * (float) std::sin (ph);
            }
            buf[1] = 0.0f; buf[(size_t) (2 * (nb - 1) + 1)] = 0.0f;
            for (int k = 2 * nb; k < 2 * N; ++k) buf[(size_t) k] = 0.0f;
            fft.performRealOnlyInverseTransform (buf.data());
            const int64 t0 = j * hop - N / 2;
            float* y = acc.getWritePointer (ch);
            for (int i = 0; i < N; ++i) { const int64 t = t0 + i + off; if (t >= 0 && t < acc.getNumSamples()) y[t] += (float) (buf[(size_t) i] * rt * win[(size_t) i] / ola); }
        }
    }
    out.setSize (2, (int) len); out.clear();
    for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, 0, acc, ch, (int) off, (int) len);
    return true;
}
} // namespace

bool renderSpectral (const HoldPlan& P, const WorkingAudio& w, juce::AudioBuffer<float>& out, const std::function<bool()>& cancel)
{
    const bool ok = P.mode == HoldMode::Freeze ? freeze (P, w, out, cancel) : texture (P, w, out, cancel);
    if (! ok) return false;
    // loudness matched to the held source
    double es = 0, eo = 0; const int64 a = (int64) P.sa, b = (int64) P.sb;
    for (int ch = 0; ch < 2; ++ch) for (int64 i = a; i < b; ++i) { const float v = w.at (ch, i); es += (double) v * v; }
    const int64 m0 = P.xf, m1 = juce::jmax (m0 + 1, P.len - P.xf);
    for (int ch = 0; ch < 2; ++ch) { const float* y = out.getReadPointer (ch); for (int64 i = m0; i < m1; ++i) eo += (double) y[i] * y[i]; }
    const double rs = std::sqrt (es / std::max<int64> (1, (b - a) * 2)), ro = std::sqrt (eo / std::max<int64> (1, (m1 - m0) * 2));
    const float g = ro > 1e-9 ? (float) juce::jlimit (0.1, 10.0, rs / ro) : 1.0f;
    out.applyGain (g);
    applyJoins (P, w, 0, out.getWritePointer (0), out.getWritePointer (1), out.getNumSamples());
    return true;
}
} // namespace gslice::hold
