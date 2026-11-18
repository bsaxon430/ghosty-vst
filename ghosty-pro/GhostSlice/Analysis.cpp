// GHOST SLICE analysis, part 1: FFT, helpers, onsets (flux + HFC), syllables, AUTO material, tempo. See Analysis.h for the
// references; the code follows the study's pseudo-code (GHOST-SLICE-STUDY.md 3.1 - 3.4) and is our own.
#include "AnalysisInternal.h"
#include "SourceAudio.h"

namespace gslice::analysis
{
using namespace detail;
static constexpr double kPi = juce::MathConstants<double>::pi;

// ---------------------------------------------------------------- FFT
FFT::FFT (int size) : n (size)
{
    jassert (n >= 2 && (n & (n - 1)) == 0);
    int lg = 0; while ((1 << lg) < n) ++lg;
    rev.resize ((size_t) n);
    for (int i = 0; i < n; ++i) { int r = 0; for (int b = 0; b < lg; ++b) if ((i >> b) & 1) r |= 1 << (lg - 1 - b); rev[(size_t) i] = r; }
    cs.resize ((size_t) n / 2); sn.resize ((size_t) n / 2);
    for (int k = 0; k < n / 2; ++k) { const double a = 2.0 * kPi * k / n; cs[(size_t) k] = std::cos (a); sn[(size_t) k] = std::sin (a); }
}

void FFT::transform (double* re, double* im, bool inverse) const
{
    for (int i = 0; i < n; ++i) { const int j = rev[(size_t) i]; if (i < j) { std::swap (re[i], re[j]); std::swap (im[i], im[j]); } }
    for (int len = 2; len <= n; len <<= 1)
    {
        const int half = len / 2, step = n / len;
        for (int i = 0; i < n; i += len)
            for (int j = 0; j < half; ++j)
            {
                const double wr = cs[(size_t) (j * step)], wi = inverse ? sn[(size_t) (j * step)] : -sn[(size_t) (j * step)];
                const int a = i + j, b = a + half;
                const double tr = re[b] * wr - im[b] * wi, ti = re[b] * wi + im[b] * wr;
                re[b] = re[a] - tr; im[b] = im[a] - ti; re[a] += tr; im[a] += ti;
            }
    }
}

std::vector<float> mono (const juce::AudioBuffer<float>& a)
{
    const int n = a.getNumSamples(), ch = a.getNumChannels();
    std::vector<float> m ((size_t) juce::jmax (0, n), 0.0f);
    if (ch == 0) return m;
    for (int c = 0; c < ch; ++c) { const float* x = a.getReadPointer (c); for (int i = 0; i < n; ++i) m[(size_t) i] += x[i]; }
    if (ch > 1) { const float k = 1.0f / (float) ch; for (auto& v : m) v *= k; }
    return m;
}

const char* materialName (Material m)
{
    switch (m) { case Material::Drums: return "DRUMS"; case Material::Vocal: return "VOCAL"; case Material::Melodic: return "MELODIC"; case Material::Auto: break; }
    return "AUTO";
}
Material materialFromName (const juce::String& s)
{
    const auto u = s.toUpperCase();
    if (u == "DRUMS") return Material::Drums; if (u == "VOCAL") return Material::Vocal; if (u == "MELODIC") return Material::Melodic;
    return Material::Auto;
}

Profile profileFor (Material m, double sr)
{
    // frame sizes are the study's 44.1 / 48 kHz values, doubled at 88.2 / 96 kHz and up (the same time spans)
    const int k = sr > 80000 ? 2 : 1;
    Profile p;
    switch (m)
    {
        case Material::Vocal:   p = { 2048 * k, (int) std::lround (sr * 0.010), 0.10, 0.08, 1.6, 150, 100, 50, 50, 70 }; break;
        case Material::Melodic: p = { 2048 * k, (int) std::lround (sr * 0.010), 0.25, 0.06, 1.5, 120, 80, 40, 40, 50 }; break;
        case Material::Auto: case Material::Drums: p = { 1024 * k, 256 * k, 0.50, 0.05, 1.4, 100, 50, 30, 30, 30 }; break;
    }
    return p;
}

// ---------------------------------------------------------------- helpers
namespace detail
{
Yin::Yin (int w) : W (w), M (nextPow2 (w + w / 2 + 1)), fft (M), re ((size_t) M), im ((size_t) M), sq ((size_t) w + 1), cr ((size_t) M), ci ((size_t) M) {}

void Yin::cmnd (const double* x, std::vector<double>& d)
{
    const int H = W / 2;
    d.assign ((size_t) H + 1, 1.0);
    // pack a = x[0..H) (real) and b = x[0..W) (imag) into one complex FFT, then r = IFFT (conj (A) * B)
    std::fill (re.begin(), re.end(), 0.0); std::fill (im.begin(), im.end(), 0.0);
    for (int i = 0; i < H; ++i) re[(size_t) i] = x[i];
    for (int i = 0; i < W; ++i) im[(size_t) i] = x[i];
    fft.transform (re.data(), im.data(), false);
    for (int k = 0; k < M; ++k)
    {
        const int j = (M - k) & (M - 1);
        const double zr = re[(size_t) k], zi = im[(size_t) k], wr = re[(size_t) j], wi = -im[(size_t) j];   // conj Z[M-k]
        const double ar = 0.5 * (zr + wr), ai = 0.5 * (zi + wi);          // A = (Z + conj Z') / 2
        const double br = 0.5 * (zi - wi), bi = -0.5 * (zr - wr);         // B = (Z - conj Z') / 2i
        cr[(size_t) k] = ar * br + ai * bi; ci[(size_t) k] = ar * bi - ai * br;   // conj (A) * B
    }
    fft.transform (cr.data(), ci.data(), true);
    const double inv = 1.0 / M;
    sq[0] = 0; for (int i = 0; i < W; ++i) sq[(size_t) i + 1] = sq[(size_t) i] + x[i] * x[i];
    const double e0 = sq[(size_t) H];
    double run = 0;
    for (int tau = 1; tau <= H; ++tau)
    {
        const double et = sq[(size_t) (tau + H)] - sq[(size_t) tau];
        const double dt = std::max (0.0, e0 + et - 2.0 * cr[(size_t) tau] * inv);
        run += dt;
        d[(size_t) tau] = run > 1e-20 ? dt * tau / run : 1.0;
    }
}

std::vector<double> hann (int N) { std::vector<double> w ((size_t) N); for (int i = 0; i < N; ++i) w[(size_t) i] = 0.5 - 0.5 * std::cos (2.0 * kPi * i / N); return w; }

double percentile (std::vector<double> v, double q)
{
    if (v.empty()) return 0.0;
    const size_t k = (size_t) juce::jlimit (0.0, (double) v.size() - 1.0, std::floor (q * (double) (v.size() - 1) + 0.5));
    std::nth_element (v.begin(), v.begin() + (std::ptrdiff_t) k, v.end());
    return v[k];
}

static void biquadPass (std::vector<double>& v, double b0, double b1, double b2, double a1, double a2)
{
    if (v.empty()) return;
    double x1 = v[0], x2 = v[0], y1 = v[0] * (b0 + b1 + b2) / (1 + a1 + a2), y2 = y1;   // start in the steady state of the first value
    for (auto& s : v) { const double x = s, y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2; x2 = x1; x1 = x; y2 = y1; y1 = y; s = y; }
}
static void butter2 (std::vector<double>& v, double fc, double fs, bool high)
{
    if (v.size() < 4 || fc <= 0 || fc >= fs * 0.5) return;
    const double K = std::tan (kPi * fc / fs), q = std::sqrt (2.0), nrm = 1.0 / (1.0 + K * q + K * K);
    double b0, b1, b2;
    if (high) { b0 = nrm; b1 = -2 * nrm; b2 = nrm; } else { b0 = K * K * nrm; b1 = 2 * b0; b2 = b0; }
    const double a1 = 2 * (K * K - 1) * nrm, a2 = (1 - K * q + K * K) * nrm;
    biquadPass (v, b0, b1, b2, a1, a2);
    std::reverse (v.begin(), v.end()); biquadPass (v, b0, b1, b2, a1, a2); std::reverse (v.begin(), v.end());
}
void zeroPhaseLowpass (std::vector<double>& v, double fc, double fs) { butter2 (v, fc, fs, false); }
void zeroPhaseHighpass (std::vector<double>& v, double fc, double fs)
{
    if (v.empty()) return;
    const double m0 = v.front(); for (auto& s : v) s -= m0;   // the steady-state start of a high-pass is 0
    butter2 (v, fc, fs, true);
}

double levelDb (const float* x, int64 n, int64 a, int64 b)
{
    a = std::max<int64> (0, a); b = std::min (n, b); if (b <= a) return -120.0;
    double e = 0; for (int64 i = a; i < b; ++i) e += (double) x[i] * x[i];
    return 10.0 * std::log10 (e / (double) (b - a) + 1e-12);
}

int64 refineOnset (const float* x, int64 n, double sr, int64 a, int64 b)
{
    a = std::max<int64> (0, a); b = std::min (n, b);
    const int hop = juce::jmax (4, (int) std::lround (sr * 0.001)), win = 2 * hop;
    const int nb = (int) ((b - a) / hop);
    if (nb < 3) return a;
    std::vector<double> env ((size_t) nb);
    for (int k = 0; k < nb; ++k)
    {
        double e = 0; const int64 s0 = a + (int64) k * hop, s1 = std::min (n, s0 + win);
        for (int64 i = s0; i < s1; ++i) e += (double) x[i] * x[i];
        env[(size_t) k] = std::sqrt (e / (double) juce::jmax<int64> (1, s1 - s0));
    }
    int pk = 0; for (int k = 1; k < nb; ++k) if (env[(size_t) k] > env[(size_t) pk]) pk = k;
    int lo = pk; while (lo > 0 && env[(size_t) lo - 1] <= env[(size_t) lo] * 1.02 + 1e-12) --lo;   // the last local minimum before the rise
    const double floorE = env[(size_t) lo], thr = floorE + 0.15 * (env[(size_t) pk] - floorE);
    int j = lo; while (j < pk && env[(size_t) j] < thr) ++j;
    // sample level: the first sample above 15 % of the local peak (above the floor's peak level)
    const int64 s0 = a + (int64) juce::jmax (lo, j - 1) * hop, s1 = std::min (n, a + (int64) pk * hop + win);
    double pkA = 0; for (int64 i = s0; i < s1; ++i) pkA = std::max (pkA, (double) std::abs (x[i]));
    const double flA = floorE * 1.41, tA = flA + 0.15 * (pkA - flA);
    for (int64 i = std::max (a + (int64) lo * hop, s0 - hop); i < s1; ++i) if (std::abs (x[i]) >= tA) return i;
    return a + (int64) j * hop;
}

double flatness (const float* x, int64 n, double sr, int64 a)
{
    const int L = juce::jmax (64, (int) std::lround (sr * 0.020)), N = nextPow2 (L);
    FFT f (N); std::vector<double> re ((size_t) N, 0.0), im ((size_t) N, 0.0); const auto w = hann (L);
    for (int i = 0; i < L; ++i) { const int64 s = a + i; re[(size_t) i] = s >= 0 && s < n ? x[s] * w[(size_t) i] : 0.0; }
    f.transform (re.data(), im.data(), false);
    double lg = 0, ar = 0; int cnt = 0;
    for (int k = 1; k < N / 2; ++k) { const double p = re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k] + 1e-14; lg += std::log (p); ar += p; ++cnt; }
    if (cnt == 0 || ar <= 0) return 0.0;
    return juce::jlimit (0.0, 1.0, std::exp (lg / cnt) / (ar / cnt));
}
} // namespace detail

// ---------------------------------------------------------------- onset strength (3.1 + 3.2)
OdfFrames odfFrames (const float* x, int64 n, double sr, int N, int H, const std::atomic<bool>* cancel)
{
    OdfFrames o; o.N = N; o.H = H; o.sr = sr;
    if (n <= 0 || N < 16 || H < 1) return o;
    const int F = (int) (n / H) + 1, B = N / 2;
    const auto w = hann (N); double wsum = 0; for (double v : w) wsum += v;
    const double norm = 2.0 / wsum;   // a sine of amplitude A peaks at |X| = A
    // log-frequency triangular filterbank, 24 bands / octave, 30 Hz .. 17 kHz; bands that land on one bin merge
    std::vector<int> centres;
    for (int k = 0; ; ++k)
    {
        const double f = 30.0 * std::pow (2.0, k / 24.0); if (f > std::min (17000.0, sr * 0.475)) break;
        const int bin = juce::jlimit (1, B - 1, (int) std::lround (f / sr * N));
        if (centres.empty() || bin > centres.back()) centres.push_back (bin);
    }
    const int nb = (int) centres.size();
    struct Band { int lo, hi; std::vector<double> wt; };
    std::vector<Band> bands ((size_t) nb);
    for (int b = 0; b < nb; ++b)
    {
        const int c = centres[(size_t) b], l = b > 0 ? centres[(size_t) b - 1] : c - 1, r = b + 1 < nb ? centres[(size_t) b + 1] : c + 1;
        auto& bd = bands[(size_t) b]; bd.lo = juce::jmax (0, l); bd.hi = juce::jmin (B, r); double s = 0;
        for (int k = bd.lo; k <= bd.hi; ++k)
        {
            const double t = k <= c ? (c == l ? 1.0 : (double) (k - l) / (c - l)) : (r == c ? 1.0 : (double) (r - k) / (r - c));
            bd.wt.push_back (std::max (0.0, t)); s += std::max (0.0, t);
        }
        for (auto& v : bd.wt) v /= std::max (1e-12, s);
    }
    const int lowTop = juce::jmax (1, (int) std::floor (150.0 / sr * N));
    FFT fft (N); std::vector<double> re ((size_t) N), im ((size_t) N), mag ((size_t) B + 1), Y ((size_t) nb), Yp ((size_t) nb, 0.0), lm ((size_t) lowTop + 1), lmp ((size_t) lowTop + 1, 0.0);
    o.flux.assign ((size_t) F, 0.0); o.hfc.assign ((size_t) F, 0.0); o.low.assign ((size_t) F, 0.0);
    double hPrev = 0;
    for (int f = 0; f < F; ++f)
    {
        if (cancel != nullptr && (f & 255) == 0 && cancel->load()) return {};
        const int64 s0 = (int64) f * H - N / 2;
        for (int i = 0; i < N; ++i) { const int64 s = s0 + i; re[(size_t) i] = s >= 0 && s < n ? x[s] * w[(size_t) i] : 0.0; im[(size_t) i] = 0.0; }
        fft.transform (re.data(), im.data(), false);
        double hs = 0;
        for (int k = 0; k <= B; ++k) { const double m = std::sqrt (re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k]) * norm; mag[(size_t) k] = m; hs += (double) k / B * m * m; }
        for (int b = 0; b < nb; ++b) { const auto& bd = bands[(size_t) b]; double s = 0; for (int k = bd.lo; k <= bd.hi; ++k) s += bd.wt[(size_t) (k - bd.lo)] * mag[(size_t) k]; Y[(size_t) b] = std::log1p (100.0 * s); }
        double fl = 0;
        if (f > 0)
            for (int b = 0; b < nb; ++b)
            {   // SuperFlux: the previous frame through a 3-band maximum filter (vibrato suppression)
                double pm = Yp[(size_t) b]; if (b > 0) pm = std::max (pm, Yp[(size_t) b - 1]); if (b + 1 < nb) pm = std::max (pm, Yp[(size_t) b + 1]);
                fl += std::max (0.0, Y[(size_t) b] - pm);
            }
        o.flux[(size_t) f] = fl / std::max (1, nb);
        const double h = std::log1p (1000.0 * hs);
        o.hfc[(size_t) f] = f > 0 ? std::max (0.0, h - hPrev) : 0.0; hPrev = h;
        double lw = 0;
        for (int k = 1; k <= lowTop; ++k) { lm[(size_t) k] = mag[(size_t) k] * mag[(size_t) k]; if (f > 0) lw += std::max (0.0, lm[(size_t) k] - lmp[(size_t) k]); }   // linear power: the kick, not the snare's noise
        o.low[(size_t) f] = lw / lowTop;
        std::swap (Y, Yp); std::swap (lm, lmp);
    }
    return o;
}

// v / max (running 95th percentile of the positive values over 5 s, floor): loud and quiet passages compare fairly, and a
// near-stationary sound (whose flux is tiny) is never blown up into fake onsets (the floor is an absolute log-flux level)
static std::vector<double> normalise (const std::vector<double>& v, double fps, double floorV)
{
    const int F = (int) v.size(); std::vector<double> out ((size_t) F, 0.0);
    if (F == 0) return out;
    const int span = juce::jmax (1, (int) std::lround (5.0 * fps)), step = juce::jmax (1, (int) std::lround (fps));
    std::vector<double> centres, dens;
    for (int c = 0; c < F + step; c += step)
    {
        const int a = juce::jmax (0, c - span / 2), b = juce::jmin (F, c + span / 2 + 1);
        std::vector<double> pos; for (int i = a; i < b; ++i) if (v[(size_t) i] > 1e-9) pos.push_back (v[(size_t) i]);
        centres.push_back (c); dens.push_back (std::max (floorV, percentile (pos, 0.95)));
        if (c >= F) break;
    }
    for (int i = 0; i < F; ++i)
    {
        const double k = (double) i / step; const auto j = (size_t) k; const double fr = k - (double) j;
        const double d = j + 1 < dens.size() ? dens[j] * (1 - fr) + dens[j + 1] * fr : dens.back();
        out[(size_t) i] = v[(size_t) i] / d;
    }
    return out;
}

static constexpr double kFluxFloor = 0.12, kHfcFloor = 0.25;

static std::vector<Event> pickPeaks (const float* x, int64 n, double sr, const Profile& P, const OdfFrames& o, std::vector<double>* odfOut = nullptr)
{
    const double fps = sr / P.H;
    const auto nf = normalise (o.flux, fps, kFluxFloor), nh = normalise (o.hfc, fps, kHfcFloor);
    const int F = (int) nf.size();
    std::vector<double> odf ((size_t) F);
    for (int i = 0; i < F; ++i) odf[(size_t) i] = nf[(size_t) i] + P.wHfc * nh[(size_t) i];
    auto fr = [fps] (double ms) { return juce::jmax (1, (int) std::lround (ms * 0.001 * fps)); };
    const int ma = fr (P.medPreMs), mb = fr (P.medPostMs), m1 = fr (P.maxPreMs), m2 = fr (P.maxPostMs), gap = fr (P.minGapMs);
    std::vector<int> peaks;
    int last = -gap - 1;
    std::vector<double> win;
    for (int i = 1; i < F; ++i)
    {
        const double v = odf[(size_t) i];
        if (v <= P.delta) continue;
        bool isMax = true;
        for (int k = juce::jmax (0, i - m1); k < i && isMax; ++k) if (odf[(size_t) k] >= v) isMax = false;
        for (int k = i + 1; k <= juce::jmin (F - 1, i + m2) && isMax; ++k) if (odf[(size_t) k] > v) isMax = false;
        if (! isMax) continue;
        win.assign (odf.begin() + juce::jmax (0, i - ma), odf.begin() + juce::jmin (F, i + mb + 1));
        const double thr = P.delta + P.lambda * median (win);
        if (v < thr) continue;
        if (i - last < gap) continue;
        peaks.push_back (i); last = i;
    }
    if (odfOut) *odfOut = odf;
    std::vector<Event> ev;
    const int64 endGuard = n - (int64) std::lround (sr * 0.010);   // the abrupt end of a file is no attack
    const int64 pre = (int64) std::lround (sr * 0.025), post = P.N / 2 + (int64) std::lround (sr * 0.005);
    const int64 minGapS = (int64) std::lround (P.minGapMs * 0.001 * sr);
    for (size_t q = 0; q < peaks.size(); ++q)
    {
        const int64 c = (int64) peaks[q] * P.H;
        int64 a = c - pre, b = c + post;
        if (! ev.empty()) a = std::max (a, ev.back().pos + minGapS / 2);
        if (q + 1 < peaks.size()) b = std::min (b, (int64) peaks[q + 1] * P.H - P.N / 4);
        if (b <= a + 8) b = a + 8;
        const int64 at = refineOnset (x, n, sr, a, b);
        if (at >= endGuard) continue;
        Event e; e.pos = juce::jlimit<int64> (0, juce::jmax<int64> (0, n - 1), at);
        e.strength = (float) juce::jlimit (0.0, 1.0, odf[(size_t) peaks[q]] / 0.8);
        if (! ev.empty() && e.pos - ev.back().pos < minGapS / 2) { if (e.strength > ev.back().strength) ev.back() = e; continue; }
        ev.push_back (e);
    }
    return ev;
}

// ---------------------------------------------------------------- AUTO
Material classify (const float* x, int64 n, double sr)
{
    if (n < (int64) (sr * 0.1)) return Material::Drums;
    const int W = nextPow2 ((int) std::lround (sr * 0.04)), hop = (int) std::lround (sr * 0.02);
    Yin yin (W); std::vector<double> fr ((size_t) W), d;
    const int tmin = juce::jmax (2, (int) (sr / 1000.0)), tmax = juce::jmin (W / 2 - 1, (int) (sr / 60.0));
    double peakDb = -120; for (int64 s = 0; s + W <= n; s += hop) peakDb = std::max (peakDb, levelDb (x, n, s, s + W));
    int loud = 0, voiced = 0, loudUnvoiced = 0, runs = 0; double runLen = 0; int cur = 0;
    for (int64 s = 0; s + W <= n; s += hop)
    {
        if (levelDb (x, n, s, s + W) < peakDb - 40) { if (cur > 0) { runLen += cur; ++runs; cur = 0; } continue; }
        ++loud;
        for (int i = 0; i < W; ++i) fr[(size_t) i] = x[s + i];
        yin.cmnd (fr.data(), d);
        double mn = 1; for (int t = tmin; t <= tmax; ++t) mn = std::min (mn, d[(size_t) t]);
        if (mn < 0.2) { ++voiced; ++cur; }
        else { if (mn > 0.45) ++loudUnvoiced; if (cur > 0) { runLen += cur; ++runs; cur = 0; } }
    }
    if (cur > 0) { runLen += cur; ++runs; }
    if (loud == 0) return Material::Drums;
    const double vf = (double) voiced / loud, uf = (double) loudUnvoiced / loud, meanRun = runs > 0 ? runLen / runs * 0.02 : 0;
    if (vf < 0.35) return Material::Drums;
    if (uf >= 0.06 && meanRun < 0.6) return Material::Vocal;
    return Material::Melodic;
}

// ---------------------------------------------------------------- syllables (3.3)
std::vector<Event> syllableOnsets (const float* x, int64 n, double sr, const std::atomic<bool>* cancel)
{
    std::vector<Event> out;
    const int hop = (int) std::lround (sr * 0.010), win = (int) std::lround (sr * 0.030), M = nextPow2 (win);
    if (n < win * 4) return out;
    const int F = (int) (n / hop) + 1;
    const auto w = hann (win);
    FFT fft (M); Yin yin (win);
    std::vector<double> re ((size_t) M), im ((size_t) M), fr ((size_t) win), d;
    std::vector<double> Eb ((size_t) F), Ebb ((size_t) F), V ((size_t) F, 0.0), SCf ((size_t) F, 0.0), Fx ((size_t) F, 0.0);
    // 40 mel bands 60 Hz .. 8 kHz
    auto mel = [] (double f) { return 2595.0 * std::log10 (1.0 + f / 700.0); };
    auto imel = [] (double m) { return 700.0 * (std::pow (10.0, m / 2595.0) - 1.0); };
    const int nm = 40; std::vector<double> edges (nm + 2);
    for (int i = 0; i < nm + 2; ++i) edges[(size_t) i] = imel (mel (60.0) + (mel (std::min (8000.0, sr * 0.45)) - mel (60.0)) * i / (nm + 1)) / sr * M;
    std::vector<std::vector<double>> melF ((size_t) F, std::vector<double> ((size_t) nm, 0.0));
    const int k300 = (int) std::lround (300.0 / sr * M), k3k = (int) std::lround (3000.0 / sr * M);
    const int tmin = juce::jmax (2, (int) (sr / 800.0)), tmax = juce::jmin (win / 2 - 1, (int) (sr / 70.0));
    double peakBb = -120;
    for (int f = 0; f < F; ++f)
    {
        if (cancel != nullptr && (f & 127) == 0 && cancel->load()) return {};
        const int64 s0 = (int64) f * hop - win / 2;
        std::fill (re.begin(), re.end(), 0.0); std::fill (im.begin(), im.end(), 0.0);
        for (int i = 0; i < win; ++i) { const int64 s = s0 + i; const double v = s >= 0 && s < n ? x[s] : 0.0; fr[(size_t) i] = v; re[(size_t) i] = v * w[(size_t) i]; }
        Ebb[(size_t) f] = levelDb (x, n, s0, s0 + win); peakBb = std::max (peakBb, Ebb[(size_t) f]);
        fft.transform (re.data(), im.data(), false);
        double band = 0;
        for (int k = k300; k <= k3k; ++k) band += re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k];
        Eb[(size_t) f] = 10.0 * std::log10 (band / (double) M + 1e-12);
        for (int b = 0; b < nm; ++b)
        {
            const double l = edges[(size_t) b], c = edges[(size_t) b + 1], r = edges[(size_t) b + 2]; double s = 0;
            for (int k = (int) std::ceil (l); k <= (int) std::floor (r) && k < M / 2; ++k)
            {
                const double t = k <= c ? (k - l) / std::max (1e-9, c - l) : (r - k) / std::max (1e-9, r - c);
                s += std::max (0.0, t) * (re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k]);
            }
            melF[(size_t) f][(size_t) b] = std::max (0.0, 10.0 * std::log10 (s / (double) M + 1e-12) + 100.0);
        }
        yin.cmnd (fr.data(), d);
        double mn = 1; for (int t = tmin; t <= tmax; ++t) mn = std::min (mn, d[(size_t) t]);
        V[(size_t) f] = juce::jlimit (0.0, 1.0, (0.45 - mn) / 0.35);
    }
    for (int f = 0; f < F; ++f) if (Ebb[(size_t) f] < peakBb - 50) V[(size_t) f] = 0;
    std::vector<double> Es ((size_t) F);
    for (int f = 0; f < F; ++f) { double s = 0; int c = 0; for (int k = juce::jmax (0, f - 2); k <= juce::jmin (F - 1, f + 2); ++k) { s += Eb[(size_t) k]; ++c; } Es[(size_t) f] = s / c; }
    double fxMax = 1e-9;
    for (int f = 0; f < F; ++f)
    {
        const auto& a = melF[(size_t) juce::jmax (0, f - 3)]; const auto& b = melF[(size_t) juce::jmin (F - 1, f + 3)];
        double ab = 0, aa = 0, bb = 0; for (int k = 0; k < nm; ++k) { ab += a[(size_t) k] * b[(size_t) k]; aa += a[(size_t) k] * a[(size_t) k]; bb += b[(size_t) k] * b[(size_t) k]; }
        SCf[(size_t) f] = aa > 0 && bb > 0 ? juce::jlimit (0.0, 1.0, 1.0 - ab / std::sqrt (aa * bb)) * 10.0 : 0.0;   // (scaled: typical changes 0.02 .. 0.1)
        if (f > 0) { double s = 0; for (int k = 0; k < nm; ++k) s += std::max (0.0, melF[(size_t) f][(size_t) k] - melF[(size_t) f - 1][(size_t) k]); Fx[(size_t) f] = s; fxMax = std::max (fxMax, s); }
    }
    for (auto& v : Fx) v /= fxMax;
    // nuclei
    std::vector<double> loudE; for (int f = 0; f < F; ++f) if (Ebb[(size_t) f] > peakBb - 50) loudE.push_back (Es[(size_t) f]);
    const double medE = median (loudE);
    std::vector<int> nuc;
    for (int f = 1; f + 1 < F; ++f)
    {
        if (! (Es[(size_t) f] >= Es[(size_t) f - 1] && Es[(size_t) f] > Es[(size_t) f + 1])) continue;
        if (Es[(size_t) f] < medE - 2.0 || Ebb[(size_t) f] < peakBb - 50) continue;
        double vmax = 0; for (int k = juce::jmax (0, f - 2); k <= juce::jmin (F - 1, f + 2); ++k) vmax = std::max (vmax, V[(size_t) k]);
        if (vmax < 0.5) continue;
        double lmin = Es[(size_t) f], rmin = Es[(size_t) f];
        for (int k = f - 1; k >= juce::jmax (0, f - 30) && Es[(size_t) k] <= Es[(size_t) f]; --k) lmin = std::min (lmin, Es[(size_t) k]);
        for (int k = f + 1; k <= juce::jmin (F - 1, f + 30) && Es[(size_t) k] <= Es[(size_t) f]; ++k) rmin = std::min (rmin, Es[(size_t) k]);
        if (Es[(size_t) f] - std::max (lmin, rmin) < 3.0) continue;
        nuc.push_back (f);
    }
    // spacing >= 90 ms and an intensity dip >= 2 dB between neighbours, else merge (keep the louder)
    for (bool changed = true; changed;)
    {
        changed = false;
        for (size_t i = 0; i + 1 < nuc.size(); ++i)
        {
            const int a = nuc[i], b = nuc[i + 1];
            double dip = Es[(size_t) a]; for (int k = a; k <= b; ++k) dip = std::min (dip, Es[(size_t) k]);
            if (b - a < 9 || std::min (Es[(size_t) a], Es[(size_t) b]) - dip < 2.0)
            { nuc.erase (nuc.begin() + (std::ptrdiff_t) (Es[(size_t) a] >= Es[(size_t) b] ? i + 1 : i)); changed = true; break; }
        }
    }
    // boundaries: argmin of E - 3 SC - 4 ODF + 2 V between nuclei; the syllable onset = the first rise after it
    int prevB = -1;
    for (size_t i = 0; i < nuc.size(); ++i)
    {
        const int c = nuc[i];
        int lo;
        if (i == 0)
        {
            lo = c; while (lo > 0 && c - lo < 40 && Ebb[(size_t) lo - 1] > Ebb[(size_t) c] - 30.0 && Ebb[(size_t) lo - 1] > peakBb - 55) --lo;
            if (lo > 0) --lo;
        }
        else lo = prevB;
        int bnd = -1;
        if (i + 1 < nuc.size())
        {
            const int c2 = nuc[i + 1]; double best = 1e30;
            for (int k = c + 1; k < c2; ++k) { const double cost = Es[(size_t) k] - 3.0 * SCf[(size_t) k] - 4.0 * Fx[(size_t) k] + 2.0 * V[(size_t) k]; if (cost < best) { best = cost; bnd = k; } }
        }
        // the start: from the boundary (its lowest broadband frame) to the first frame 30 % of the way up to the nucleus (in dB)
        int m = lo; for (int k = lo; k < c; ++k) if (Ebb[(size_t) k] < Ebb[(size_t) m]) m = k;
        const double base = Ebb[(size_t) m], top = Ebb[(size_t) c];
        int g = m; while (g < c && Ebb[(size_t) g] < base + 0.3 * (top - base)) ++g;
        const int64 a = std::max<int64> ((int64) m * hop, (int64) g * hop - win), b = (int64) g * hop + win / 2;
        Event e; e.pos = refineOnset (x, n, sr, a, std::min (n, b)); e.flags = kSyllable;
        e.strength = (float) juce::jlimit (0.0, 1.0, 0.4 + (top - base) / 30.0);
        if (out.empty() || e.pos > out.back().pos + (int64) (0.05 * sr)) out.push_back (e);
        prevB = bnd >= 0 ? bnd : c;
    }
    return out;
}

// ---------------------------------------------------------------- the detector
juce::String OnsetSet::digest() const
{
    Hasher h; h.add ((int) asked).add ((int) used).add ((int) events.size());
    for (auto& e : events) h.add (e.pos).add (e.strength).add (e.flatness).add ((int) e.flags).add (e.group);
    return h.hex();
}

OnsetSet detectOnsets (const float* x, int64 n, double sr, Material m, const std::atomic<bool>* cancel)
{
    OnsetSet o; o.asked = m;
    o.used = m == Material::Auto ? classify (x, n, sr) : m;
    if (n <= 0) return o;
    const auto P = profileFor (o.used, sr);
    const auto odf = odfFrames (x, n, sr, P.N, P.H, cancel);
    if (odf.flux.empty()) return o;
    auto ev = pickPeaks (x, n, sr, P, odf);
    if (o.used == Material::Vocal)
    {   // syllables win over flux onsets near them (within 50 ms); flux onsets elsewhere stay (note changes inside a vowel)
        auto syl = syllableOnsets (x, n, sr, cancel);
        if (cancel != nullptr && cancel->load()) return {};
        const int64 near = (int64) std::lround (0.05 * sr);
        std::vector<Event> merged = syl;
        for (auto& e : ev)
        {
            bool close = false; for (auto& s : syl) if (std::abs (s.pos - e.pos) < near) close = true;
            if (! close) { e.flags |= kSoft; merged.push_back (e); }
        }
        std::sort (merged.begin(), merged.end(), [] (const Event& a, const Event& b) { return a.pos < b.pos; });
        ev = std::move (merged);
    }
    // noisiness + breath, then (VOCAL) word groups
    for (auto& e : ev)
    {
        e.flatness = (float) flatness (x, n, sr, e.pos);
        const double lvl = levelDb (x, n, e.pos, e.pos + (int64) (0.02 * sr));
        double loud = -120; for (int64 s = std::max<int64> (0, e.pos - (int64) sr); s < std::min (n, e.pos + (int64) sr); s += (int64) (0.02 * sr)) loud = std::max (loud, levelDb (x, n, s, s + (int64) (0.02 * sr)));
        if (e.flatness > 0.5f && lvl < loud - 30.0 && (e.flags & kSyllable) == 0) e.flags |= kBreath;
    }
    if (o.used == Material::Vocal && ev.size() > 1)
    {
        const int64 fr10 = (int64) std::lround (0.01 * sr);
        int gid = 0;
        for (size_t i = 0; i + 1 < ev.size(); ++i)
        {
            const int64 a = ev[i].pos, b = ev[i + 1].pos;
            bool joined = b - a < (int64) (0.35 * sr);
            if (joined)
            {   // continuous energy: no 40 ms run below (the louder neighbour - 35 dB)
                double top = -120; for (int64 s = a; s < b; s += fr10) top = std::max (top, levelDb (x, n, s, s + fr10));
                int quiet = 0;
                for (int64 s = a; s < b && joined; s += fr10) { if (levelDb (x, n, s, s + fr10) < top - 35.0) { if (++quiet >= 4) joined = false; } else quiet = 0; }
            }
            if (joined) { if (ev[i].group < 0) ev[i].group = ++gid; ev[i + 1].group = ev[i].group; }
        }
    }
    o.events = std::move (ev);
    return o;
}

// ---------------------------------------------------------------- tempo (3.4)
Tempo estimateTempo (const float* x, int64 n, double sr, const std::atomic<bool>* cancel)
{
    Tempo t;
    if (n < (int64) (sr * 1.5)) return t;
    const int k = sr > 80000 ? 2 : 1;
    const auto o = odfFrames (x, n, sr, 1024 * k, 256 * k, cancel);
    if (o.flux.size() < 64) return t;
    const double fps = sr / o.H, lenSec = (double) n / sr;
    std::vector<double> ose = o.flux;
    double mean = 0; for (double v : ose) mean += v; mean /= (double) ose.size();
    for (auto& v : ose) v -= mean;
    zeroPhaseHighpass (ose, 0.4, fps);
    const int L = (int) ose.size();
    const int maxLag = juce::jmin (L - 2, (int) std::ceil (4.0 * 60.0 * fps / 60.0) + 4);
    std::vector<double> acf ((size_t) maxLag + 1, 0.0);
    for (int tau = 0; tau <= maxLag; ++tau)
    {
        double s = 0; for (int i = 0; i + tau < L; ++i) s += ose[(size_t) i] * ose[(size_t) (i + tau)];
        acf[(size_t) tau] = s / (double) (L - tau);
    }
    if (acf[0] <= 1e-15) return t;
    for (auto& v : acf) v /= acf[0];
    auto A = [&] (double tau) { return tau > maxLag ? 0.0 : interp (acf, tau); };
    struct C { double bpm, score; bool loop; int bars; };
    std::vector<C> cand;
    for (double bpm = 60.0; bpm <= 200.0 + 1e-9; bpm += 0.05)
    {
        const double tau = 60.0 * fps / bpm;
        // metrical support: the bar-level and the 8th-level pulses; a beat that would split into four regular parts is
        // probably the half-time reading of a faster beat (16th-level support costs a little)
        double s = A (tau) + 0.5 * A (2 * tau) + 0.25 * A (tau / 2) - 0.25 * std::max (0.0, A (tau / 4));
        s = std::max (0.0, s) * std::exp (-0.5 * std::pow (std::log2 (bpm / 120.0) / 1.0, 2));
        const double bars = lenSec * bpm / 240.0; const int rb = (int) std::lround (bars);
        bool loop = false;
        if ((rb == 1 || rb == 2 || rb == 4 || rb == 8 || rb == 16) && std::abs (bars - rb) < 0.01) { s *= 2.0; loop = true; }
        cand.push_back ({ bpm, s, loop, loop ? rb : 0 });
    }
    size_t bi = 0; for (size_t i = 1; i < cand.size(); ++i) if (cand[i].score > cand[bi].score) bi = i;
    if (cand[bi].score <= 0) return t;
    // octave disambiguation (a slow loop read x2, a fast one read /2): read at the slower tempo S of the pair (S, 2S), the beats of
    // a slow loop carry the kick / snare and its in-between pulses only the hats, so the median LOUDNESS rise off the beat is far
    // below the one on it; a fast loop read at S has its backbeat on those in-between pulses (as loud as the beats). The spectral
    // flux weighs hats like drums, so this uses a linear energy rise (loud events dominate). Swing: the off-beat may sit anywhere in
    // the middle 40 % of the beat. Between the two thresholds the autocorrelation's reading stands.
    t.octaveRatio = -1;
    {
        const double b0 = cand[bi].bpm, slow = b0 * 0.5 >= 60.0 - 1e-9 ? b0 * 0.5 : b0;
        if (slow * 2.0 <= 200.0 + 1e-9)
        {
            std::vector<double> en ((size_t) L, 0.0);
            for (int f = 0; f < L; ++f)
            {
                const int64 c = (int64) f * o.H, a0 = std::max<int64> (0, c - o.N / 2), a1 = std::min (n, c + o.N / 2);
                double e = 0; for (int64 i = a0; i < a1; ++i) e += (double) x[i] * x[i];
                en[(size_t) f] = e;
            }
            std::vector<double> rise ((size_t) L, 0.0);
            for (int f = 1; f < L; ++f) rise[(size_t) f] = std::max (0.0, en[(size_t) f] - en[(size_t) f - 1]);
            const double tau = 60.0 * fps / slow;
            // the phase: the fast reading's beat comb first (so the slow beat lands on a real beat, not on an off-beat kick), then
            // the stronger of its two interleaved slow phases
            auto peakIn = [&] (double c, double w) { double m = 0; for (int q = (int) std::floor (c - w); q <= (int) std::ceil (c + w); ++q) if (q >= 0 && q < L) m = std::max (m, rise[(size_t) q]); return m; };
            auto comb = [&] (double q, double per) { double s = 0; int cnt = 0; for (double p = q; p < L; p += per, ++cnt) s += peakIn (p, 2.0); return cnt > 0 ? s / cnt : 0.0; };
            double ph = 0, phS = -1;
            for (double q = 0; q < tau * 0.5; q += 0.25) if (const double s = comb (q, tau * 0.5); s > phS) { phS = s; ph = q; }
            if (ph > tau * 0.5 - 3.0) ph = std::max (0.0, ph - tau * 0.5);   // a comb just before the period is the pulse at 0
            if (comb (ph + tau * 0.5, tau) > comb (ph, tau)) ph += tau * 0.5;
            std::vector<double> on, off;
            for (double p = ph; p + 0.7 * tau < L; p += tau) { on.push_back (peakIn (p, 2.0)); off.push_back (peakIn (p + 0.5 * tau, 0.2 * tau)); }
            if (on.size() >= 4)
            {
                // a pulse is STRONG when it rises above the geometric middle of the loud (90th percentile) and the quiet
                // (10th) pulses: the drums, not the hats. ratio = the share of strong off-beats / the share of strong beats
                std::vector<double> all (on); all.insert (all.end(), off.begin(), off.end());
                const double p90 = percentile (all, 0.9), p10 = percentile (all, 0.1), thr = std::max (std::sqrt (p90 * p10), 0.1 * p90);
                double fOn = 0, fOff = 0;
                for (double v : on) fOn += v > thr ? 1.0 : 0.0;
                for (double v : off) fOff += v > thr ? 1.0 : 0.0;
                fOn /= (double) on.size(); fOff /= (double) off.size();
                t.octaveRatio = p90 > 1e-15 ? fOff / std::max (fOn, 1e-9) : 1.0;
                const double want = t.octaveRatio < kOctaveSlow ? slow : t.octaveRatio > kOctaveFast ? slow * 2.0 : b0;
                if (std::abs (want / b0 - 1.0) > 0.02)
                {   // the best candidate within +-1.5 % of the wanted reading (the grid is 0.05 BPM)
                    size_t hi = bi; double hs = -1;
                    for (size_t i = 0; i < cand.size(); ++i) if (std::abs (cand[i].bpm / want - 1.0) < 0.015 && cand[i].score > hs) { hs = cand[i].score; hi = i; }
                    if (hs > 0) bi = hi;
                }
            }
        }
    }
    const auto best = cand[bi];
    double bpm = best.bpm;
    if (best.loop) { bpm = 240.0 * best.bars / lenSec; t.bars = best.bars; }
    else
    {   // refine on the furthest multiple of the beat lag that fits (parabolic peak of the ACF)
        const double tau0 = 60.0 * fps / bpm;
        for (int m = 4; m >= 1; --m)
        {
            if (m * tau0 + 2 >= maxLag) continue;
            const int c = (int) std::lround (m * tau0); int pk = c;
            for (int j = c - 2 * m; j <= c + 2 * m; ++j) if (j > 1 && j < maxLag && acf[(size_t) j] > acf[(size_t) pk]) pk = j;
            if (pk <= 1 || pk >= maxLag) continue;
            const double y0 = acf[(size_t) pk - 1], y1 = acf[(size_t) pk], y2 = acf[(size_t) pk + 1], den = y0 - 2 * y1 + y2;
            const double off = std::abs (den) > 1e-12 ? juce::jlimit (-0.5, 0.5, 0.5 * (y0 - y2) / den) : 0.0;
            const double tau = (pk + off) / m;
            if (std::abs (tau - tau0) < 0.06 * tau0) bpm = 60.0 * fps / tau;
            break;
        }
    }
    // confidence: the margin over the best rival tempo that is not the same pulse (x1/2, x2) + how periodic it is
    double rival = 0;
    for (auto& c : cand)
    {
        bool same = false; for (double f : { 1.0, 0.5, 2.0 }) if (std::abs (c.bpm / (best.bpm * f) - 1.0) < 0.04) same = true;
        if (! same) rival = std::max (rival, c.score);
    }
    const double tauB = 60.0 * fps / bpm;
    t.confidence = juce::jlimit (0.0, 1.0, 0.5 * juce::jlimit (0.0, 1.0, (best.score - rival) / best.score * 2.0) + 0.5 * juce::jlimit (0.0, 1.0, A (tauB) * 2.0) + (best.loop ? 0.15 : 0.0));
    t.bpm = bpm;
    // beat phase: the comb with the most onset energy; downbeat: the beat of the bar with the most low-band (<= 150 Hz) onsets
    double bestPh = 0, bestS = -1;
    for (double ph = 0; ph < tauB; ph += 0.25)
    {
        double s = 0; for (double p = ph; p < L; p += tauB) s += interp (o.flux, p);
        if (s > bestS) { bestS = s; bestPh = ph; }
    }
    if (bestPh > tauB - 3.0) bestPh = std::max (0.0, bestPh - tauB);   // a comb just before tau is the beat at 0
    int bestJ = 0; double bestD = -1; std::array<double, 4> D {};
    auto lowNear = [&] (double p) { double m = 0; for (int q = (int) std::floor (p) - 1; q <= (int) std::ceil (p) + 4; ++q) if (q >= 0 && q < L) m = std::max (m, o.low[(size_t) q]); return m; };
    double lowTot = 1e-30; for (double v : o.low) lowTot = std::max (lowTot, v);
    for (int j = 0; j < 4; ++j) { double s = 0; for (double p = bestPh + j * tauB; p < L; p += 4 * tauB) s += lowNear (p) / lowTot + 0.1 * interp (o.flux, p); D[(size_t) j] = s; if (s > bestD) { bestD = s; bestJ = j; } }
    if (t.bars > 0 && D[0] >= 0.8 * bestD) bestJ = 0;   // a loop file starts on its downbeat unless the low end clearly says otherwise
    auto snapToOnset = [&] (double framePos)
    {
        const int64 c = (int64) std::llround (framePos * o.H);
        const int64 at = refineOnset (x, n, sr, c - (int64) (0.03 * sr), c + o.N / 2 + (int64) (0.005 * sr));
        return (double) at / sr;
    };
    t.firstBeat = snapToOnset (bestPh);
    t.downbeat = snapToOnset (bestPh + bestJ * tauB);
    return t;
}
} // namespace gslice::analysis
