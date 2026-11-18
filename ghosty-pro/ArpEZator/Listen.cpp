// Port of listen.js. Arrays that are Float32Array in the JS are float here (values computed in double, stored as float),
// everything else is double, and the order of operations follows the JS so both give the same answers.
#include "Listen.h"
#include <algorithm>
#if defined (__GNUC__) || defined (__clang__)
 #pragma GCC diagnostic ignored "-Wfloat-equal"   // exact comparisons on purpose: same semantics as the JS original
#endif
#include <cmath>
#include <map>
#include <numeric>

namespace listen
{
namespace
{
const double PI = 3.14159265358979323846;
int m12 (int x) { return ((x % 12) + 12) % 12; }
double cl (double x, double a, double b) { return std::max (a, std::min (b, x)); }
double jsRound (double x) { const double f = std::floor (x); return x - f >= 0.5 ? f + 1 : f; }
const char* PC[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

void fft (std::vector<float>& re, std::vector<float>& im)
{
    const int n = (int) re.size();
    for (int i = 1, j = 0; i < n; ++i)
    {
        int b = n >> 1; for (; j & b; b >>= 1) j ^= b; j ^= b;
        if (i < j) { std::swap (re[(size_t) i], re[(size_t) j]); std::swap (im[(size_t) i], im[(size_t) j]); }
    }
    for (int len = 2; len <= n; len <<= 1)
    {
        const double a = -2 * PI / len, wr = std::cos (a), wi = std::sin (a); const int h = len >> 1;
        for (int i = 0; i < n; i += len)
        {
            double cr = 1, ci = 0;
            for (int k = 0; k < h; ++k)
            {
                const size_t p = (size_t) (i + k), q = p + (size_t) h;
                const double xr = re[q] * cr - im[q] * ci, xi = re[q] * ci + im[q] * cr;
                re[q] = (float) (re[p] - xr); im[q] = (float) (im[p] - xi); re[p] = (float) (re[p] + xr); im[p] = (float) (im[p] + xi);
                const double t = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = t;
            }
        }
    }
}
template <typename CB>
int stft (const std::vector<float>& x, int N, int hop, CB cb)
{
    std::vector<float> win ((size_t) N), re ((size_t) N), im ((size_t) N), mag ((size_t) N / 2);
    for (int i = 0; i < N; ++i) win[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2 * PI * i / N));
    int f = 0;
    for (size_t s = 0; s + (size_t) N <= x.size(); s += (size_t) hop, ++f)
    {
        for (int i = 0; i < N; ++i) { re[(size_t) i] = (float) ((double) x[s + (size_t) i] * win[(size_t) i]); im[(size_t) i] = 0; }
        fft (re, im);
        for (int k = 0; k < N / 2; ++k) mag[(size_t) k] = (float) std::hypot ((double) re[(size_t) k], (double) im[(size_t) k]);
        cb (f, mag);
    }
    return f;
}
std::vector<float> norm (const std::vector<float>& v)
{
    double s = 0; for (float x : v) s += (double) x * x;
    s = std::sqrt (s); if (s == 0) s = 1;
    std::vector<float> o (v.size()); for (size_t i = 0; i < v.size(); ++i) o[i] = (float) (v[i] / s); return o;
}
double dot (const std::vector<float>& a, const std::vector<float>& b) { double s = 0; for (int i = 0; i < 12; ++i) s += (double) a[(size_t) i] * b[(size_t) i]; return s; }
double corr (const std::vector<double>& a, const double* b)
{
    double ma = 0, mb = 0; for (int i = 0; i < 12; ++i) { ma += a[(size_t) i]; } ma /= 12; for (int i = 0; i < 12; ++i) mb += b[i]; mb /= 12;
    double n = 0, da = 0, db = 0;
    for (int i = 0; i < 12; ++i) { n += (a[(size_t) i] - ma) * (b[i] - mb); da += std::pow (a[(size_t) i] - ma, 2); db += std::pow (b[i] - mb, 2); }
    const double d = std::sqrt (da * db); return n / (d != 0 ? d : 1);
}
// chord templates, in JS property order: '7' (an array index) first
const std::vector<std::string> TYPES { "7", "maj", "min", "sus2", "sus4", "m7", "maj7", "add9" };
const std::map<std::string, std::vector<int>> TIV { { "maj", { 0, 4, 7 } }, { "min", { 0, 3, 7 } }, { "sus2", { 0, 2, 7 } }, { "sus4", { 0, 5, 7 } }, { "7", { 0, 4, 7, 10 } }, { "m7", { 0, 3, 7, 10 } }, { "maj7", { 0, 4, 7, 11 } }, { "add9", { 0, 4, 7, 2 } } };
const std::map<std::string, std::string> SUF { { "maj", "" }, { "min", "m" }, { "sus2", "sus2" }, { "sus4", "sus4" }, { "7", "7" }, { "m7", "m7" }, { "maj7", "maj7" }, { "add9", "add9" } };
const std::map<std::string, double> PRIOR { { "maj", 0 }, { "min", 0 }, { "sus2", -0.05 }, { "sus4", -0.05 }, { "7", -0.035 }, { "m7", -0.035 }, { "maj7", -0.04 }, { "add9", -0.045 } };
struct Tpl { int root; std::string type; std::vector<int> iv; std::vector<float> v; };
const std::vector<Tpl>& TPL()
{
    static const std::vector<Tpl> T = [] {
        std::vector<Tpl> o; const double HARM[4][2] { { 0, 1 }, { 12, 0.45 }, { 19, 0.3 }, { 24, 0.2 } };
        for (int r = 0; r < 12; ++r) for (auto& t : TYPES)
        {
            std::vector<float> v (12, 0.0f);
            for (int iv : TIV.at (t)) for (auto& h : HARM) { auto& c = v[(size_t) m12 (r + iv + (int) h[0])]; c = (float) (c + h[1]); }
            o.push_back ({ r, t, TIV.at (t), norm (v) });
        }
        return o; }();
    return T;
}
const double KKmaj[12] { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 }, KKmin[12] { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
const double TPmaj[12] { 0.748, 0.060, 0.488, 0.082, 0.670, 0.460, 0.096, 0.715, 0.104, 0.366, 0.057, 0.400 }, TPmin[12] { 0.712, 0.084, 0.474, 0.618, 0.049, 0.460, 0.105, 0.747, 0.404, 0.067, 0.133, 0.330 };
const int SCmaj[7] { 0, 2, 4, 5, 7, 9, 11 }, SCmin[7] { 0, 2, 3, 5, 7, 8, 10 };
struct Key { int key; std::string scale; double r; };
bool inScale (const std::string& sc, int pc) { const int* S = sc == "major" ? SCmaj : SCmin; for (int i = 0; i < 7; ++i) if (S[i] == pc) return true; return false; }
bool inKey (int r, const std::vector<int>& iv, const Key& k) { for (int x : iv) if (! inScale (k.scale, m12 (r + x - k.key))) return false; return true; }
Key rel (const Key& k) { return k.scale == "major" ? Key { m12 (k.key + 9), "minor", 0 } : Key { m12 (k.key + 3), "major", 0 }; }
bool sameKey (const Key& a, const Key& b) { return a.key == b.key && a.scale == b.scale; }
} // namespace

Result analyze (const std::vector<float>& input, double srIn)
{
    Result res;
    // down to ~22 kHz (box filter + decimate)
    std::vector<float> x; double sr = srIn;
    {
        const int f = std::max (1, (int) jsRound (srIn / 22050));
        if (f == 1) x = input;
        else { x.resize (input.size() / (size_t) f); for (size_t i = 0; i < x.size(); ++i) { double s = 0; for (int j = 0; j < f; ++j) s += input[i * (size_t) f + (size_t) j]; x[i] = (float) (s / f); } sr = srIn / f; }
    }
    if ((double) x.size() > sr * 75) { const size_t s = (size_t) std::floor ((x.size() - sr * 60) / 2); const size_t e = std::min (x.size(), (size_t) (s + sr * 60)); x = std::vector<float> (x.begin() + (long) s, x.begin() + (long) e); }
    double rms = 0; for (float v : x) rms += (double) v * v; rms = std::sqrt (rms / std::max<size_t> (1, x.size()));
    const double dur = x.size() / sr; res.dur = dur; res.rms = rms;
    if (dur < 3 || rms < 2e-4) { res.why = dur < 3 ? "too short" : "too quiet"; return res; }
    if (rms < 0.05) { const double g = 0.1 / rms; for (auto& v : x) v = (float) (v * g); }
    // onset strength (log spectral flux), 1024 / 256
    const int H1 = 256, N1 = 1024; const double fr = sr / H1;
    const int kMax = std::min (N1 / 2, (int) jsRound (9000 / sr * N1)), kLow = (int) jsRound (200 / sr * N1);
    std::vector<float> prev ((size_t) N1 / 2, 0.0f); std::vector<double> flux, low;
    stft (x, N1, H1, [&] (int f, const std::vector<float>& mag) {
        double s = 0, l = 0;
        for (int k = 1; k < kMax; ++k) { const double v = std::log (1 + 100 * (double) mag[(size_t) k]), d = v - prev[(size_t) k]; if (d > 0) { s += d; if (k < kLow) l += d; } prev[(size_t) k] = (float) v; }
        flux.push_back (s); low.push_back (l); if (f == 0) { flux[0] = 0; low[0] = 0; } });
    const int nO = (int) flux.size(); std::vector<float> o ((size_t) nO); const int W = (int) jsRound (fr * 0.4);
    for (int i = 0; i < nO; ++i) { double s = 0; int c = 0; for (int j = std::max (0, i - W); j <= std::min (nO - 1, i + W); ++j) { s += flux[(size_t) j]; ++c; } o[(size_t) i] = (float) std::max (0.0, flux[(size_t) i] - s / c); }
    double oMean = 0; for (float v : o) oMean += v; oMean /= nO; if (oMean == 0 || ! std::isfinite (oMean)) oMean = 1;
    for (auto& v : o) v = (float) (v / oMean);
    auto oAt = [&] (long i) -> double { return i >= 0 && i < nO ? (double) o[(size_t) i] : 0.0; };
    // tempo: autocorrelation + comb over multiples, log-normal prior around 120
    const int maxLag = std::min (nO - 2, (int) std::ceil (60 * fr / 70 * 4) + 2);
    if (maxLag < 8) { res.why = "too short"; return res; }
    std::vector<float> ac ((size_t) maxLag + 1);
    for (int L = 0; L <= maxLag; ++L) { double s = 0; for (int i = 0; i + L < nO; ++i) s += ((double) o[(size_t) i] - 1) * ((double) o[(size_t) (i + L)] - 1); ac[(size_t) L] = (float) (s / (nO - L)); }
    double a0 = ac[0]; if (a0 == 0) a0 = 1; for (auto& v : ac) v = (float) (v / a0);
    auto acAt = [&] (double l) -> double { if (l >= maxLag || l < 0) return 0; const int i = (int) std::floor (l); const double f = l - i; return ac[(size_t) i] * (1 - f) + ac[(size_t) i + 1] * f; };
    auto acPk = [&] (double l) { return std::max ({ acAt (l - 1), acAt (l), acAt (l + 1) }); };
    auto comb = [&] (double b) { const double L = 60 * fr / b; double s = 0, w = 0; for (int k = 1; k <= 4; ++k) { if (k * L + 1 >= maxLag) break; s += acPk (k * L) / k; w += 1.0 / k; } return w != 0 ? s / w : 0.0; };
    auto prior = [] (double b) { return std::exp (-0.5 * std::pow (std::log2 (b / 120) / 0.9, 2)); };
    double best = 0, bScore = -1; std::vector<double> scores;
    for (double b = 70; b <= 180; b += 0.5) { const double s = comb (b) * (0.6 + 0.4 * prior (b)); scores.push_back (s); if (s > bScore) { bScore = s; best = b; } }
    for (double alt : { best / 2, best * 2, best * 2 / 3, best * 1.5 })
        if (alt >= 70 && alt <= 180) { const double s = comb (alt) * (0.6 + 0.4 * prior (alt)); if (s > bScore * 0.92 && prior (alt) > prior (best) * 1.15 && (alt == best / 2 || alt == best * 2)) { best = alt; bScore = s; } }
    {
        auto fine = [&] (double b) { const double L = 60 * fr / b; double s = 0; for (int k = 1; k <= 8 && k * L + 1 < maxLag; ++k) s += acAt (k * L); return s; };
        double bb = best, sb = fine (best);
        for (double b = best - 1.5; b <= best + 1.501; b += 0.05) { const double s = fine (b); if (s > sb) { sb = s; bb = b; } }
        best = bb;
    }
    auto sorted = scores; std::sort (sorted.begin(), sorted.end()); const double med = sorted[sorted.size() >> 1];
    res.tempoConf = cl ((bScore - med - 0.15) / 0.4, 0, 1) * cl (1 - 3 * std::max (0.0, med), 0, 1);
    double bpm = jsRound (best * 10) / 10;
    // beats: best phase of a comb at the beat period
    const double P = 60 * fr / best; int ph = 0; double phS = -1;
    for (int p = 0; p < P; ++p) { double s = 0; for (double t = p; t < nO; t += P) { const long i = (long) jsRound (t); s += oAt (i) + 0.5 * (oAt (i - 1) + oAt (i + 1)); } if (s > phS) { phS = s; ph = p; } }
    auto fT = [&] (double i) { return (i * H1 + N1 / 2.0) / sr; };
    std::vector<double> beats; for (double t = ph; t < nO; t += P) beats.push_back (fT (t));
    // chroma, 4096 / 1024, semitones 45..96, with 3rd / 5th harmonics of lower notes subtracted
    const int N2 = 4096, H2 = 1024, nb = N2 / 2; std::vector<short> bS ((size_t) nb, -1); std::vector<float> bW ((size_t) nb, 0.0f);
    for (int k = 1; k < nb; ++k) { const double f = k * sr / N2, m = 69 + 12 * std::log2 (f / 440); const int s = (int) jsRound (m); if (s >= 45 && s <= 100) { bS[(size_t) k] = (short) s; bW[(size_t) k] = (float) std::max (0.0, 1 - 2 * std::abs (m - s)); } }
    std::vector<std::vector<float>> chroma; std::vector<double> cT, cE; double cent = 0, centW = 0;
    stft (x, N2, H2, [&] (int f, const std::vector<float>& mag) {
        std::vector<float> E (101, 0.0f); double tot = 0, cs = 0;
        for (int k = 1; k < nb; ++k) { const double v = mag[(size_t) k]; tot += v; cs += v * k; if (bS[(size_t) k] >= 0) { auto& e = E[(size_t) bS[(size_t) k]]; e = (float) (e + v * bW[(size_t) k]); } }
        cent += cs * sr / N2; centW += tot;
        std::vector<float> c (12, 0.0f); double e = 0;
        for (int s = 45; s <= 100; ++s)
        {
            const double v = std::max (0.0, (double) E[(size_t) s] - 0.33 * E[(size_t) (s - 19)] - 0.2 * E[(size_t) (s - 28)]);
            E[(size_t) s] = (float) v;
            if (s <= 96) { auto& cc = c[(size_t) (s % 12)]; cc = (float) (cc + v); e += v; }
        }
        chroma.push_back (c); cT.push_back ((f * H2 + N2 / 2.0) / sr); cE.push_back (e); });
    const double centroid = centW != 0 ? cent / centW : 1500;
    double eMax = 1e-9; for (double e : cE) eMax = std::max (eMax, e);
    std::vector<std::vector<float>> frameC;
    for (size_t i = 0; i < chroma.size(); ++i)
    {
        double m = 0; for (float v : chroma[i]) m = std::max (m, (double) v); if (m == 0) m = 1;
        const double w = std::sqrt (cE[i] / eMax); std::vector<float> c (12);
        for (int k = 0; k < 12; ++k) c[(size_t) k] = (float) (std::sqrt (chroma[i][(size_t) k] / m) * w);
        frameC.push_back (c);
    }
    // key: profile correlation over the whole clip
    std::vector<float> tot (12, 0.0f); for (auto& c : frameC) for (int i = 0; i < 12; ++i) tot[(size_t) i] = (float) (tot[(size_t) i] + (double) c[(size_t) i]);
    std::vector<Key> keys;
    for (const char* sc : { "major", "minor" }) for (int k = 0; k < 12; ++k)
    {
        std::vector<double> a (12); for (int i = 0; i < 12; ++i) a[(size_t) i] = tot[(size_t) m12 (i + k)];
        const bool maj = std::string (sc) == "major";
        keys.push_back ({ k, sc, (corr (a, maj ? KKmaj : KKmin) + corr (a, maj ? TPmaj : TPmin)) / 2 });
    }
    std::stable_sort (keys.begin(), keys.end(), [] (const Key& a, const Key& b) { return a.r > b.r; });
    Key K = keys[0];
    Key other = keys[1]; for (auto& k : keys) if (! sameKey (k, K) && ! sameKey (k, rel (K))) { other = k; break; }
    double keyConf = cl ((K.r - 0.35) / 0.4, 0, 1) * cl ((K.r - other.r) / 0.12, 0.15, 1);
    // beat-synchronous chroma
    std::vector<std::vector<float>> bc; std::vector<bool> bcOk;
    for (size_t i = 0; i + 1 < beats.size(); ++i)
    {
        std::vector<float> v (12, 0.0f); int n = 0;
        for (size_t j = 0; j < cT.size(); ++j) if (cT[j] >= beats[i] && cT[j] < beats[i + 1]) { for (int q = 0; q < 12; ++q) v[(size_t) q] = (float) (v[(size_t) q] + (double) frameC[j][(size_t) q]); ++n; }
        bc.push_back (v); bcOk.push_back (n > 0);
    }
    std::vector<double> nov (bc.size(), 0.0);
    for (size_t i = 1; i < bc.size(); ++i) if (bcOk[i] && bcOk[i - 1]) nov[i] = 1 - dot (norm (bc[i]), norm (bc[i - 1]));
    auto lowAt = [&] (double t) { const long i = (long) jsRound ((t * sr - N1 / 2.0) / H1); return i >= 0 && i < (long) low.size() ? low[(size_t) i] : 0.0; };
    int off = 0; double offS = -1e9;
    for (int ofs = 0; ofs < 4; ++ofs)
    {
        double s = 0, kk = 0; int n = 0;
        for (size_t i = 0; i < nov.size(); ++i) { if ((int) i % 4 == ofs) { s += nov[i]; ++n; kk += lowAt (beats[i]); } else s -= nov[i] / 3; }
        const double v = s / std::max (1, n) + 1e-4 * kk;
        if (v > offS) { offS = v; off = ofs; }
    }
    std::vector<std::vector<float>> bars;
    for (int s = off - 4; s < (int) bc.size(); s += 4)
    {
        std::vector<float> v (12, 0.0f); int n = 0;
        for (int i = std::max (0, s); i < std::min ((int) bc.size(), s + 4); ++i) if (bcOk[(size_t) i]) { for (int q = 0; q < 12; ++q) v[(size_t) q] = (float) (v[(size_t) q] + (double) bc[(size_t) i][(size_t) q]); ++n; }
        if (n >= 2) bars.push_back (norm (v));
    }
    const auto& T = TPL();
    std::vector<std::vector<double>> emis;
    for (auto& v : bars) { std::vector<double> e; for (auto& t : T) e.push_back (dot (v, t.v) + PRIOR.at (t.type) + (inKey (t.root, t.iv, K) ? 0.03 : 0)); emis.push_back (e); }
    std::vector<int> path;
    if (! bars.empty())
    {
        const int nS = (int) T.size(); const double stay = 0.04, sc = 25;
        std::vector<double> dp (emis[0].size()); for (size_t i = 0; i < dp.size(); ++i) dp[i] = emis[0][i] * sc;
        std::vector<std::vector<int>> bk;
        for (size_t b = 1; b < bars.size(); ++b)
        {
            std::vector<double> nd ((size_t) nS); std::vector<int> bb ((size_t) nS); double mx = -1e9; int mi = 0;
            for (int i = 0; i < nS; ++i) if (dp[(size_t) i] > mx) { mx = dp[(size_t) i]; mi = i; }
            for (int s = 0; s < nS; ++s) { const double st = dp[(size_t) s] + stay * sc; if (st >= mx) { nd[(size_t) s] = st; bb[(size_t) s] = s; } else { nd[(size_t) s] = mx; bb[(size_t) s] = mi; } nd[(size_t) s] += emis[b][(size_t) s] * sc; }
            dp = nd; bk.push_back (bb);
        }
        int s = (int) (std::max_element (dp.begin(), dp.end()) - dp.begin()); path = { s };
        for (int b = (int) bk.size() - 1; b >= 0; --b) { s = bk[(size_t) b][(size_t) s]; path.insert (path.begin(), s); }
    }
    std::vector<const Tpl*> barCh; for (int i : path) barCh.push_back (&T[(size_t) i]);
    double chordConf = 0;
    if (! barCh.empty()) { double a = 0; for (size_t b = 0; b < path.size(); ++b) a += emis[b][(size_t) path[b]]; chordConf = cl ((a / path.size() - 0.6) / 0.3, 0, 1); }
    auto isT = [] (const Tpl* c, const Key& k) { return c && c->root == k.key && (k.scale == "minor" ? c->iv[1] == 3 : c->iv[1] == 4); };
    auto tScore = [&] (const Key& k) { double a = 0; for (size_t i = 0; i < barCh.size(); ++i) a += isT (barCh[i], k) ? 1 + (i == 0 || i == barCh.size() - 1 ? 0.5 : 0) : 0; return a; };
    if (! barCh.empty())
    {
        auto kS = [&] (const Key& k) { int c = 0; for (auto* ch : barCh) if (inKey (ch->root, std::vector<int> (ch->iv.begin(), ch->iv.begin() + 3), k)) ++c;
            return k.r + 0.15 * c / barCh.size() * 2 + 0.12 * tScore (k) / barCh.size(); };
        std::vector<std::pair<Key, double>> top; for (int i = 0; i < 6 && i < (int) keys.size(); ++i) top.push_back ({ keys[(size_t) i], kS (keys[(size_t) i]) });
        std::stable_sort (top.begin(), top.end(), [] (const std::pair<Key, double>& a, const std::pair<Key, double>& b) { return a.second > b.second; });
        K = top[0].first;
        const std::pair<Key, double>* o2 = nullptr; for (auto& t : top) if (! sameKey (t.first, K) && ! sameKey (t.first, rel (K))) { o2 = &t; break; }
        keyConf = cl ((K.r - 0.35) / 0.4, 0, 1) * cl (o2 ? (top[0].second - o2->second) / 0.06 : 1, 0.15, 1);
    }
    { Key R2 = rel (K); if (tScore (R2) > tScore (K) + 0.6) { R2.r = K.r; K = R2; } }
    // the loop: smallest period (2, 4, 8 bars) that repeats, the most repeated window, started on the tonic
    std::vector<int> ids; for (auto* c : barCh) ids.push_back (c->root * 16 + (int) (std::find (TYPES.begin(), TYPES.end(), c->type) - TYPES.begin()));
    const int B = (int) ids.size();
    auto per = [&] (int L) { int m = 0, n = 0; for (int i = 0; i + L < B; ++i) { ++n; if (ids[(size_t) i] == ids[(size_t) (i + L)]) ++m; } return n ? (double) m / n : -1.0; };
    int L = B >= 4 ? 4 : std::max (1, B);
    for (int c : { 2, 4, 8 }) { const double p = per (c); if (c <= B && p >= 0.75) { L = c; break; } if (c == 8 && B >= 12 && p > per (4) + 0.2) L = 8; }
    if (L == 1 && B >= 2) L = 2;
    int ws = 0, wS = -1;
    for (int s = 0; s + L <= B; ++s) { int a = 0; for (int i = 0; i < B; ++i) if (ids[(size_t) i] == ids[(size_t) (s + (((i - s) % L) + L) % L)]) ++a; if (a > wS) { wS = a; ws = s; } }
    std::vector<const Tpl*> loop; for (int i = ws; i < ws + L && i < B; ++i) loop.push_back (barCh[(size_t) i]);
    int ti = -1; for (size_t i = 0; i < loop.size(); ++i) if (isT (loop[i], K)) { ti = (int) i; break; }
    if (ti > 0) std::rotate (loop.begin(), loop.begin() + ti, loop.end());
    // feel: onset density, triplets, syncopation, brightness
    int nOn = 0; const double thr = 1.5;
    for (int i = 2; i < nO - 2; ++i) { const double v = o[(size_t) i]; if (v > thr && v >= o[(size_t) i - 1] && v >= o[(size_t) i + 1] && v >= o[(size_t) i - 2] && v >= o[(size_t) i + 2]) ++nOn; }
    const double density = nOn / std::max (1.0, dur * best / 60);
    const bool trip = acPk (P / 3) > 0.15 && acPk (P / 3) > acPk (P / 4) * 1.35 && acPk (P / 3) > acPk (P / 2) * 0.9;
    double on = 0, offE = 0;
    for (double t = ph; t + P < nO; t += P) for (int q = 0; q < 4; ++q) { const long i = (long) jsRound (t + q * P / 4); const double v = std::max ({ oAt (i), oAt (i - 1), oAt (i + 1) }); if (q == 1 || q == 3) offE += v; else on += v; }
    const double sync = offE / std::max (1e-6, on + offE);
    res.rate = trip ? "1/16T" : density < 1.4 ? "1/8" : "1/16";
    res.groove = jsRound (cl ((sync - 0.22) * 2.2, 0, 0.8) * 100) / 100;
    res.oct = centroid < 1100 ? 1 : centroid > 2600 ? 3 : 2;
    res.preset = centroid < 1100 ? "keys" : centroid > 2600 ? "supersaw" : "pluck";
    if (bpm < 70) bpm *= 2;
    if (bpm > 180) bpm /= 2;
    res.conf = jsRound ((res.tempoConf * 0.35 + keyConf * 0.35 + chordConf * 0.3) * 100) / 100;
    res.ok = true; res.bpm = bpm; res.bpmInt = (int) jsRound (bpm); res.key = K.key; res.scale = K.scale; res.keyConf = keyConf; res.keyR = K.r;
    double mx = 0; for (float v : tot) mx = std::max (mx, (double) v); if (mx == 0) mx = 1;
    for (float v : tot) res.chroma.push_back (v / mx);
    for (auto* c : loop) res.chords.push_back ({ c->root, c->type, c->iv, std::string (PC[c->root]) + SUF.at (c->type) });
    for (auto* c : barCh) res.bars.push_back (std::string (PC[c->root]) + SUF.at (c->type));
    res.chordConf = chordConf; res.density = density; res.triplet = trip; res.sync = sync; res.centroid = centroid;
    return res;
}

// ---------------------------------------------------------------- test audio
namespace
{
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void set (bool hp, double f, double q, double sr)
    {
        const double w = 2 * PI * f / sr, c = std::cos (w), s = std::sin (w), al = s / (2 * q), a0 = 1 + al;
        if (hp) { b0 = (1 + c) / 2 / a0; b1 = -(1 + c) / a0; b2 = (1 + c) / 2 / a0; } else { b0 = (1 - c) / 2 / a0; b1 = (1 - c) / a0; b2 = (1 - c) / 2 / a0; }
        a1 = -2 * c / a0; a2 = (1 - al) / a0;
    }
    double proc (double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
};
double blep (double t, double dt) { if (t < dt) { t /= dt; return t + t - t * t - 1; } if (t > 1 - dt) { t = (t - 1) / dt; return t * t + t + t + 1; } return 0; }
} // namespace

std::vector<float> synth (const SynthSpec& S)
{
    const size_t n = (size_t) jsRound (S.sr * S.dur); std::vector<double> out (n, 0.0);
    const size_t nl = S.noise ? n : (size_t) S.sr; std::vector<double> nd (nl);
    uint32_t z = 12345; for (size_t i = 0; i < nl; ++i) { z = z * 1103515245u + 12345u; nd[i] = z / 2147483648.0 - 1; }
    if (S.noise) { for (size_t i = 0; i < n; ++i) out[i] = nd[i % nl] * 0.5; std::vector<float> o (n); for (size_t i = 0; i < n; ++i) o[i] = (float) out[i]; return o; }
    uint32_t rz = 777;
    auto expRamp = [] (double v0, double v1, double x) { return v0 * std::pow (v1 / v0, x); };
    auto hit = [&] (double t, double f0, double f1, double d, double g) {
        t -= S.lead; if (t < 0) return;
        double ph = 0; const size_t a = (size_t) (t * S.sr), b = std::min (n, (size_t) ((t + d + 0.01) * S.sr));
        for (size_t i = a; i < b; ++i) { const double x = std::min (1.0, (i / S.sr - t) / d); const double f = expRamp (f0, f1, x); ph += f / S.sr; out[i] += std::sin (2 * PI * ph) * expRamp (g, 0.001, x) * 0.5; } };
    auto nz = [&] (double t, double d, double g, double hpf) {
        t -= S.lead; if (t < 0) return;
        Biquad bq; bq.set (true, hpf, 0.707, S.sr); rz = rz * 1664525u + 1013904223u; size_t o0 = (size_t) ((rz >> 8) / 16777216.0 * 0.5 * S.sr);
        const size_t a = (size_t) (t * S.sr), b = std::min (n, (size_t) ((t + d + 0.01) * S.sr));
        for (size_t i = a; i < b; ++i) { const double x = std::min (1.0, (i / S.sr - t) / d); out[i] += bq.proc (nd[(o0 + i - a) % nl]) * expRamp (g, 0.001, x) * 0.5; } };
    const double beat = 60 / S.bpm;
    for (int i = 0; i * beat < S.dur; ++i)
    {
        const double t = i * beat; hit (t, 150, 45, 0.25, 0.9);
        if (i % 2) nz (t, 0.15, 0.35, 1500);
        nz (t + beat / 2 + S.swing * beat / 4, 0.05, 0.15, 7000); nz (t, 0.04, 0.1, 7000);
    }
    const double bar = beat * 4;
    for (int b = 0; b * bar < S.dur + S.lead && ! S.prog.empty(); ++b)
    {
        const auto& ch = S.prog[(size_t) b % S.prog.size()]; const double t = std::max (0.0, b * bar - S.lead);
        if (b * bar + bar - S.lead <= 0.1) continue;
        const double e = b * bar + bar - S.lead;
        for (int m : ch)
        {
            const double f = 440 * std::pow (2.0, (m - 69) / 12.0), dt = f / S.sr; double ph = 0; Biquad lp; lp.set (false, 1800, 0.75, S.sr);
            const size_t a = (size_t) (t * S.sr), bEnd = std::min (n, (size_t) (e * S.sr));
            for (size_t i = a; i < bEnd; ++i)
            {
                const double tt = i / S.sr; double g = 0.07;
                if (tt < t + 0.05) g = 0.07 * (tt - t) / 0.05; else if (tt > e - 0.08) g = std::max (0.0, 0.07 * (1 - (tt - (e - 0.08)) / 0.07));
                ph += dt; if (ph >= 1) ph -= 1;
                out[i] += lp.proc (2 * ph - 1 - blep (ph, dt)) * g * 0.5;
            }
        }
    }
    std::vector<float> o (n); for (size_t i = 0; i < n; ++i) o[i] = (float) out[i];
    return o;
}
} // namespace listen
