#include "GenDsp.h"
#include "Psola.h"
#include "Hold.h"
#include "CleanStage.h"
#include <signalsmith-stretch/signalsmith-stretch.h>
#include <cmath>

namespace gslice::gen::dsp
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    inline double run (double x) noexcept { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void reset() { z1 = z2 = 0; }
    static Biquad lowpass (double sr, double hz, double q)
    {
        Biquad f; const double w = 2.0 * kPi * juce::jlimit (10.0, 0.49 * sr, hz) / sr, al = std::sin (w) / (2.0 * q), c = std::cos (w), a0 = 1.0 + al;
        f.b0 = (1.0 - c) / 2.0 / a0; f.b1 = (1.0 - c) / a0; f.b2 = f.b0; f.a1 = -2.0 * c / a0; f.a2 = (1.0 - al) / a0; return f;
    }
    static Biquad highpass (double sr, double hz, double q)
    {
        Biquad f; const double w = 2.0 * kPi * juce::jlimit (5.0, 0.49 * sr, hz) / sr, al = std::sin (w) / (2.0 * q), c = std::cos (w), a0 = 1.0 + al;
        f.b0 = (1.0 + c) / 2.0 / a0; f.b1 = -(1.0 + c) / a0; f.b2 = f.b0; f.a1 = -2.0 * c / a0; f.a2 = (1.0 - al) / a0; return f;
    }
};

inline float monoRead (const Buf& b, double p) noexcept
{
    if (b.getNumChannels() < 2) return readAt (b, 0, p);
    return 0.5f * (readAt (b, 0, p) + readAt (b, 1, p));
}
inline float tukey (double x, double taper) noexcept
{   // x in [0, 1]; taper = the share of the grain in the two raised-cosine ramps (1 = Hann)
    const double r = juce::jlimit (0.02, 1.0, taper) * 0.5;
    if (x <= 0.0 || x >= 1.0) return 0.0f;
    if (x < r) return (float) (0.5 - 0.5 * std::cos (kPi * x / r));
    if (x > 1.0 - r) return (float) (0.5 - 0.5 * std::cos (kPi * (1.0 - x) / r));
    return 1.0f;
}
// a smooth random curve: cosine-interpolated points every `step` seconds, values in [-1, 1]
struct Wander
{
    std::vector<double> p; double step = 0.5;
    Wander (Rng& r, double seconds, double stepS) : step (stepS) { for (double t = 0; t <= seconds + 2 * stepS; t += stepS) p.push_back (r.sym()); }
    double at (double t) const
    {
        const double x = t / step; const int i = juce::jlimit (0, (int) p.size() - 2, (int) std::floor (x)); const double f = juce::jlimit (0.0, 1.0, x - i);
        const double w = 0.5 - 0.5 * std::cos (kPi * f);
        return p[(size_t) i] * (1.0 - w) + p[(size_t) i + 1] * w;
    }
};
} // namespace

// ---------------------------------------------------------------- CLEAR's reads
Sinc::Sinc()
{
    auto i0 = [] (double x) { double s = 1.0, term = 1.0; for (int k = 1; k < 40; ++k) { term *= (x / (2.0 * k)) * (x / (2.0 * k)); s += term; } return s; };
    const double beta = 7.5, norm = i0 (beta);
    for (int p = 0; p <= kPhases; ++p)
    {
        const double frac = (double) p / kPhases;
        double h[kTaps], sum = 0.0;
        for (int k = 0; k < kTaps; ++k)
        {
            const double d = (double) (k - kHalf + 1) - frac;   // the tap's distance from the read position (samples)
            const double sn = std::abs (d) < 1e-12 ? 1.0 : (std::abs (d - std::round (d)) < 1e-12 ? 0.0 : std::sin (kPi * d) / (kPi * d));
            const double u = d / (double) kHalf, w = std::abs (u) >= 1.0 ? 0.0 : i0 (beta * std::sqrt (1.0 - u * u)) / norm;
            h[k] = sn * w; sum += h[k];
        }
        for (int k = 0; k < kTaps; ++k) t[p * kTaps + k] = (float) (h[k] / sum);
    }
}
const Sinc& Sinc::get() { static const Sinc s; return s; }

Buf varispeed (const Buf& in, double sr, double ratio)
{
    const int n = in.getNumSamples();
    if (n == 0 || std::abs (ratio - 1.0) < 1e-9) { Buf o; o.makeCopyOf (in); return o; }
    Buf out (in.getNumChannels(), juce::jmax (1, (int) std::llround ((double) n / ratio)));
    source::resampleChannels (in, n, sr * ratio, sr, out);
    return out;
}

int phaseMatchedLoopEnd (const Buf& b, int ls, int le, int search, int win)
{
    const int n = b.getNumSamples();
    if (ls - win < 0 || le - search - win <= ls) return le;
    std::vector<double> m ((size_t) n);
    for (int i = 0; i < n; ++i) { double s = 0; for (int c = 0; c < b.getNumChannels(); ++c) s += b.getSample (c, i); m[(size_t) i] = s; }
    double rr = 0; for (int i = 0; i < win; ++i) rr += m[(size_t) (ls - win + i)] * m[(size_t) (ls - win + i)];
    int best = le; double bc = -2.0;
    for (int e = le - search; e <= juce::jmin (n - 1, le + search); ++e)
    {
        double xy = 0, xx = 0;
        for (int i = 0; i < win; ++i) { const double x = m[(size_t) (e - win + i)]; xy += x * m[(size_t) (ls - win + i)]; xx += x * x; }
        const double cc = xx > 0 && rr > 0 ? xy / std::sqrt (xx * rr) : -1.0;
        if (cc > bc) { bc = cc; best = e; }
    }
    return best;
}

Buf monoOf (const Buf& b)
{
    Buf m (1, b.getNumSamples()); m.clear();
    for (int c = 0; c < b.getNumChannels(); ++c) m.addFrom (0, 0, b, c, 0, b.getNumSamples(), 1.0f / (float) b.getNumChannels());
    return m;
}

// ---------------------------------------------------------------- basics
Buf stereoOf (const Buf& in)
{
    Buf o (2, in.getNumSamples());
    for (int ch = 0; ch < 2; ++ch) o.copyFrom (ch, 0, in, juce::jmin (ch, juce::jmax (0, in.getNumChannels() - 1)), 0, in.getNumSamples());
    return o;
}

Buf slice (const Buf& in, double a, double b)
{
    const int n = juce::jmax (0, (int) std::llround (b - a));
    Buf o (2, n); o.clear();
    if (in.getNumChannels() == 0) return o;
    const bool whole = a == std::floor (a);
    for (int ch = 0; ch < 2; ++ch)
    {
        float* y = o.getWritePointer (ch); const int sc = juce::jmin (ch, in.getNumChannels() - 1); const float* x = in.getReadPointer (sc); const int N = in.getNumSamples();
        for (int i = 0; i < n; ++i)
        {
            if (whole) { const int k = (int) a + i; y[i] = k >= 0 && k < N ? x[k] : 0.0f; }
            else y[i] = readAt (in, sc, a + i);
        }
    }
    return o;
}

void fadeEdges (Buf& b, int in, int out)
{
    const int n = b.getNumSamples();
    in = juce::jmin (in, n / 2); out = juce::jmin (out, n / 2);
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
    {
        float* y = b.getWritePointer (ch);
        for (int i = 0; i < in; ++i) y[i] *= fadeShape ((double) i / (double) in);
        for (int i = 0; i < out; ++i) y[n - 1 - i] *= fadeShape ((double) i / (double) out);
    }
}

void addInto (Buf& dst, const Buf& src, int at, float gain, float gl, float gr)
{
    if (src.getNumSamples() == 0 || src.getNumChannels() == 0) return;
    int s0 = 0; if (at < 0) { s0 = -at; at = 0; }
    const int n = juce::jmin (src.getNumSamples() - s0, dst.getNumSamples() - at);
    if (n <= 0) return;
    for (int ch = 0; ch < juce::jmin (2, dst.getNumChannels()); ++ch)
        dst.addFrom (ch, at, src, juce::jmin (ch, src.getNumChannels() - 1), s0, n, gain * (ch == 0 ? gl : gr));
}

double rmsOf (const Buf& b, int from, int to)
{
    if (to < 0) to = b.getNumSamples();
    from = juce::jlimit (0, b.getNumSamples(), from); to = juce::jlimit (from, b.getNumSamples(), to);
    double s = 0; int64 n = 0;
    for (int ch = 0; ch < b.getNumChannels(); ++ch) { const float* x = b.getReadPointer (ch); for (int i = from; i < to; ++i) { s += (double) x[i] * x[i]; ++n; } }
    return n > 0 ? std::sqrt (s / (double) n) : 0.0;
}

float peakOf (const Buf& b) { float m = 0; for (int ch = 0; ch < b.getNumChannels(); ++ch) m = std::max (m, b.getMagnitude (ch, 0, b.getNumSamples())); return m; }

Buf lowpass (const Buf& in, double sr, double hz)
{
    Buf o; o.makeCopyOf (in);
    if (hz >= 0.49 * sr) return o;
    // 8th-order Butterworth (4 sections), run forward then backward: zero phase, 16th-order skirts, -6 dB at hz
    static const double Q[4] { 0.50979558, 0.60134489, 0.89997622, 2.56291545 };
    const int n = o.getNumSamples();
    for (int ch = 0; ch < o.getNumChannels(); ++ch)
    {
        float* y = o.getWritePointer (ch);
        std::vector<double> v ((size_t) n); for (int i = 0; i < n; ++i) v[(size_t) i] = y[i];
        for (int pass = 0; pass < 2; ++pass)
            for (double q : Q)
            {
                auto f = Biquad::lowpass (sr, hz, q);
                if (pass == 0) for (int i = 0; i < n; ++i) v[(size_t) i] = f.run (v[(size_t) i]);
                else for (int i = n; i-- > 0;) v[(size_t) i] = f.run (v[(size_t) i]);
            }
        for (int i = 0; i < n; ++i) y[i] = (float) v[(size_t) i];
    }
    return o;
}

void highpassInPlace (Buf& b, double sr, double hz)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
    {
        auto f = Biquad::highpass (sr, hz, 0.7071); float* y = b.getWritePointer (ch);
        for (int i = 0; i < b.getNumSamples(); ++i) y[i] = (float) f.run (y[i]);
    }
}

void BandBank::build (const Buf& in, double sampleRate)
{
    sr = sampleRate;
    ratios = { 1.0, 1.19, 1.41, 1.68, 2.0, 2.52, 3.17, 4.0 };
    copies.clear(); copies.reserve (ratios.size());
    // copy 0 is cleaned a little too: nothing within 4 % of Nyquist (a 4-point read of the top octave would fold it)
    for (double r : ratios) copies.push_back (lowpass (in, sr, (r <= 1.0 ? 0.47 : 0.46 / r) * sr));
}
const Buf& BandBank::forRatio (double r) const
{
    for (size_t i = 0; i < ratios.size(); ++i) if (r <= ratios[i] + 1e-9) return copies[i];
    return copies.back();
}

// ---------------------------------------------------------------- the voice
Voice analyseVoice (const Buf& work, double sr, double from, double c0, double c1, double to, const std::function<double (double)>& f0At)
{
    Voice v; v.sr = sr;
    const double pad = 0.06 * sr;
    const double lo = std::max (0.0, std::floor (std::min (from, c0) - pad)), hi = std::min ((double) work.getNumSamples(), std::ceil (std::max (to, c1) + pad));
    if (hi - lo < 0.05 * sr) return v;
    v.offset = lo; v.audio = slice (work, lo, hi);
    const int n = v.audio.getNumSamples();
    std::vector<float> m ((size_t) n);
    for (int i = 0; i < n; ++i) m[(size_t) i] = 0.5f * (v.audio.getSample (0, i) + v.audio.getSample (1, i));
    auto ep = psola::markEpochs (m.data(), n, sr, [&] (int64 i) { return f0At (lo + (double) i); });
    for (auto& e : ep) if (v.E.empty() || e.pos > v.E.back() + 8) v.E.push_back (e.pos);
    const int N = (int) v.E.size();
    if (N < 8) return v;
    // local periods (median of the three neighbouring gaps: a gap across an unvoiced hole does not count)
    std::vector<double> gaps ((size_t) N - 1); for (int k = 0; k + 1 < N; ++k) gaps[(size_t) k] = v.E[(size_t) k + 1] - v.E[(size_t) k];
    v.period.resize ((size_t) N);
    for (int k = 0; k < N; ++k)
    {
        double g[3]; int c = 0; for (int d = -2; d <= 1; ++d) { const int j = k + d; if (j >= 0 && j < N - 1 && c < 3) g[c++] = gaps[(size_t) j]; }
        std::sort (g, g + c); v.period[(size_t) k] = c > 0 ? g[c / 2] : sr / 200.0;
    }
    const double a0 = c0 - lo, a1 = c1 - lo, f = from - lo;
    v.first = 1; while (v.first < N - 2 && v.E[(size_t) v.first] < f) ++v.first;
    v.s0 = 1; while (v.s0 < N - 2 && v.E[(size_t) v.s0] < a0) ++v.s0;
    v.s1 = N - 2; while (v.s1 > v.s0 && v.E[(size_t) v.s1] > a1) --v.s1;
    if (v.s1 - v.s0 < 3) { v.s0 = juce::jmax (1, v.s0 - 3); v.s1 = juce::jmin (N - 2, v.s0 + 6); }
    // grain alignment: every epoch moves to where its 2-period grain best matches a reference grain from the middle of the core
    // (normalised cross-correlation, +-30 % of a period, parabolic sub-sample peak). Marks from the low-pass / residual peaks jitter
    // by several percent of a period on real voices; aligned grains overlap in phase, so the tone is clean at any pitch.
    {
        const int ref = (v.s0 + v.s1) / 2; const double P0 = v.period[(size_t) ref];
        const int H = juce::jmax (8, (int) std::lround (P0));
        std::vector<double> tm ((size_t) (2 * H + 1)); double tn = 0;
        for (int i = -H; i <= H; ++i) { const double w = 0.5 + 0.5 * std::cos (kPi * i / (H + 1)); tm[(size_t) (i + H)] = monoRead (v.audio, v.E[(size_t) ref] + i) * w; tn += tm[(size_t) (i + H)] * tm[(size_t) (i + H)]; }
        v.match.assign ((size_t) N, 0.0f);
        const int R = juce::jmax (2, (int) std::lround (0.3 * P0));
        for (int k = juce::jmax (1, v.first - 1); k <= juce::jmin (N - 2, v.s1 + 1); ++k)
        {
            double best = -2, bd = 0, cm = 0, cp = 0;
            std::vector<double> cs ((size_t) (2 * R + 1));
            for (int d = -R; d <= R; ++d)
            {
                double xy = 0, xx = 0;
                const double c = std::round (v.E[(size_t) k]) + d;
                for (int i = -H; i <= H; ++i) { const double x = monoRead (v.audio, c + i); xy += x * tm[(size_t) (i + H)]; xx += x * x; }
                cs[(size_t) (d + R)] = xx > 0 && tn > 0 ? xy / std::sqrt (xx * tn) : -1.0;
            }
            for (int d = -R; d <= R; ++d) if (cs[(size_t) (d + R)] > best) { best = cs[(size_t) (d + R)]; bd = d; }
            if (bd > -R && bd < R) { cm = cs[(size_t) (bd - 1 + R)]; cp = cs[(size_t) (bd + 1 + R)]; const double den = cm - 2 * best + cp; if (std::abs (den) > 1e-12) bd += juce::jlimit (-0.5, 0.5, 0.5 * (cm - cp) / den); }
            v.E[(size_t) k] = std::round (v.E[(size_t) k]) + bd; v.match[(size_t) k] = (float) best;
        }
        for (int k = 1; k < N; ++k) if (v.E[(size_t) k] <= v.E[(size_t) k - 1] + 0.4 * P0) v.E[(size_t) k] = v.E[(size_t) k - 1] + 0.4 * P0;
        for (int k = 0; k + 1 < N; ++k) gaps[(size_t) k] = v.E[(size_t) k + 1] - v.E[(size_t) k];
        for (int k = 0; k < N; ++k)
        {
            double g[3]; int cc = 0; for (int d = -2; d <= 1; ++d) { const int j = k + d; if (j >= 0 && j < N - 1 && cc < 3) g[cc++] = gaps[(size_t) j]; }
            std::sort (g, g + cc); v.period[(size_t) k] = cc > 0 ? g[cc / 2] : P0;
        }
    }
    std::vector<double> ps; for (int k = v.s0; k <= v.s1; ++k) ps.push_back (v.period[(size_t) k]);
    std::sort (ps.begin(), ps.end());
    v.f0 = ps.empty() ? 0.0 : sr / ps[ps.size() / 2];
    // grain loudness
    v.rms.resize ((size_t) N); double s = 0; int cnt = 0;
    for (int k = 0; k < N; ++k)
    {
        const double T = v.period[(size_t) k]; double e = 0, ws = 0;
        for (int i = (int) -T; i <= (int) T; ++i) { const double w = 0.5 + 0.5 * std::cos (kPi * i / T); const double x = monoRead (v.audio, v.E[(size_t) k] + i) * w; e += x * x; ws += w * w; }
        v.rms[(size_t) k] = (float) std::sqrt (e / std::max (1e-9, ws));
        if (k >= v.s0 && k <= v.s1) { s += v.rms[(size_t) k]; ++cnt; }
    }
    v.coreRms = cnt > 0 ? s / cnt : 0.0;
    v.bank.build (v.audio, sr);
    return v;
}

namespace
{
// one walk through the voice: a mono PSOLA stream of n frames
std::vector<float> psolaWalk (const Voice& v, const ToneSpec& s, uint64 seed, int n)
{
    const double sr = v.sr, phi = std::exp2 (s.formant / 12.0);
    const Buf& src = phi > 1.001 ? v.bank.forRatio (phi) : v.audio;
    std::vector<float> mono;   // (CLEAR: the span as one channel, read by the sinc)
    if (s.clean) { mono.resize ((size_t) src.getNumSamples()); for (int i = 0; i < src.getNumSamples(); ++i) mono[(size_t) i] = src.getNumChannels() < 2 ? src.getSample (0, i) : 0.5f * (src.getSample (0, i) + src.getSample (1, i)); }
    const int mN = (int) mono.size();
    double ownSm = 0.0; bool ownInit = false;
    std::vector<double> y ((size_t) n + 8, 0.0), ws ((size_t) n + 8, 0.0);
    Rng rng (seed);
    const double seconds = (double) n / sr;
    Wander drift (rng, seconds, 0.55), vibWobble (rng, seconds, 0.9);
    const double vibPhase = rng.uni() * 2.0 * kPi;
    const int N = (int) v.E.size();
    const int lo = juce::jlimit (1, N - 2, v.s0), hi = juce::jlimit (lo, N - 2, v.s1);
    int k = s.onset ? juce::jlimit (1, hi, v.first) : lo + rng.pick (hi - lo + 1);
    bool walking = ! s.onset || k >= lo;
    const double base = v.f0 * std::exp2 (s.semis / 12.0);
    const double stepW = juce::jlimit (0.3, 3.0, s.walk);
    const std::vector<int> steps { -2, -1, 1, 2, 3, 4 };
    const std::vector<double> wts { 0.55, 1.0, 1.7 * stepW, 1.1 * stepW, 0.6 * stepW, 0.25 * stepW };
    double t = 0;
    while (t < n + 4)
    {
        const double ts = t / sr;
        const double own = juce::jlimit (-200.0, 200.0, 1200.0 * std::log2 ((sr / v.period[(size_t) k]) / v.f0));
        const double vibOn = juce::jlimit (0.0, 1.0, (ts - s.vibratoDelayS) / 0.6);
        const double vib = s.vibratoCents * vibOn * (0.85 + 0.15 * vibWobble.at (ts)) * std::sin (2.0 * kPi * s.vibratoHz * ts + vibPhase);
        double cents = s.natural * own + vib + s.driftCents * drift.at (ts);
        if (s.clean)
        {   // the vowel's start in order: its own pitch (the scoop into the note); the walk: a smoothed curve of it (no jitter)
            if (! ownInit) { ownSm = own; ownInit = true; }
            else ownSm += (own - ownSm) * (1.0 - std::exp (-(sr / base) / (0.09 * sr)));   // (about 90 ms)
            cents = (walking ? s.natural * ownSm : std::max (s.natural, 0.5) * own) + vib + s.driftCents * drift.at (ts);
        }
        const double Tout = sr / (base * std::exp2 (cents / 1200.0));
        // the grain: 2 source periods around the epoch, asymmetric Hann, read at the formant ratio
        const double P = v.period[(size_t) k];
        const double Lh = std::min (v.E[(size_t) k] - v.E[(size_t) k - 1], 1.5 * P) / phi, Rh = std::min (v.E[(size_t) k + 1] - v.E[(size_t) k], 1.5 * P) / phi;
        const double g = juce::jlimit (0.6, 1.6, v.coreRms / std::max (1e-6, (double) v.rms[(size_t) k]));
        const int i0 = (int) std::ceil (t - Lh), i1 = (int) std::floor (t + Rh);
        if (s.clean && std::abs (phi - 1.0) < 1e-9)
        {   // CLEAR at the voice's own formants: the grain is the source at one fractional offset -> one sinc kernel for all of it
            const double b0 = v.E[(size_t) k] - t, fl = std::floor (b0); const int ib = (int) fl; const float fr = (float) (b0 - fl);
            float h[Sinc::kTaps]; Sinc::get().kernel (fr, h);
            for (int i = std::max (0, i0); i <= std::min (n + 7, i1); ++i)
            {
                const double tau = (double) i - t;
                const double w = tau < 0 ? 0.5 + 0.5 * std::cos (kPi * tau / Lh) : 0.5 + 0.5 * std::cos (kPi * tau / Rh);
                const int c0 = ib + i - Sinc::kHalf + 1;
                float x = 0.0f;
                if (fr == 0.0f) x = ib + i >= 0 && ib + i < mN ? mono[(size_t) (ib + i)] : 0.0f;
                else if (c0 >= 0 && c0 + Sinc::kTaps <= mN) { const float* p = mono.data() + c0; for (int q = 0; q < Sinc::kTaps; ++q) x += h[q] * p[q]; }
                else for (int q = 0; q < Sinc::kTaps; ++q) { const int j = c0 + q; if (j >= 0 && j < mN) x += h[q] * mono[(size_t) j]; }
                y[(size_t) i] += w * g * x;
                ws[(size_t) i] += w;
            }
        }
        else
        for (int i = std::max (0, i0); i <= std::min (n + 7, i1); ++i)
        {
            const double tau = (double) i - t;
            const double w = tau < 0 ? 0.5 + 0.5 * std::cos (kPi * tau / Lh) : 0.5 + 0.5 * std::cos (kPi * tau / Rh);
            y[(size_t) i] += w * g * (s.clean ? sincAt (mono.data(), mN, v.E[(size_t) k] + phi * tau) : monoRead (src, v.E[(size_t) k] + phi * tau));
            ws[(size_t) i] += w;
        }
        t += Tout;
        // the next epoch: in order through the vowel's start, then a seeded random walk over the stable core
        if (! walking) { ++k; if (k >= lo) walking = true; }
        else
        {
            int nk = k;
            for (int tries = 0; tries < 4; ++tries)
            {
                nk = k + steps[(size_t) rng.weighted (wts)];
                if (nk > hi) nk = 2 * hi - nk;
                if (nk < lo) nk = 2 * lo - nk;
                nk = juce::jlimit (lo, hi, nk);
                if (v.match.empty() || v.match[(size_t) nk] >= (s.clean && tries < 3 ? 0.75f : 0.55f)) break;   // (a grain unlike the vowel: pick again)
            }
            k = nk;
        }
    }
    std::vector<float> out ((size_t) n);
    for (int i = 0; i < n; ++i) out[(size_t) i] = (float) (y[(size_t) i] / std::max (1.0, ws[(size_t) i]));
    return out;
}

Buf toStereo (const std::vector<float>& A, const std::vector<float>& B, double width)
{   // mid = A, side = B (decorrelated): the mono sum is exactly A
    const int n = (int) A.size(); Buf o (2, n);
    const float s = (float) (0.5 * juce::jlimit (0.0, 1.0, width));
    for (int i = 0; i < n; ++i) { const float b = B.empty() ? 0.0f : B[(size_t) i]; o.setSample (0, i, A[(size_t) i] + s * b); o.setSample (1, i, A[(size_t) i] - s * b); }
    return o;
}
} // namespace

Buf psolaTone (const Voice& v, const ToneSpec& s)
{
    const int n = juce::jmax (16, (int) (s.seconds * v.sr));
    if (! v.ok()) { Buf o (2, n); o.clear(); return o; }
    const auto A = psolaWalk (v, s, (uint64) s.seed * 7919u + 17u, n);
    std::vector<float> B; if (s.width > 0.01) { ToneSpec s2 = s; s2.onset = false; B = psolaWalk (v, s2, (uint64) s.seed * 104729u + 91u, n); }
    auto o = toStereo (A, B, s.width);
    fadeEdges (o, (int) (0.003 * v.sr), 0);
    return o;
}

Buf grainTone (const Buf& work, const BandBank& bank, double sr, double c0, double c1, const ToneSpec& s)
{
    const int n = juce::jmax (16, (int) (s.seconds * sr));
    auto walk = [&] (uint64 seed)
    {
        std::vector<double> y ((size_t) n, 0.0), ws ((size_t) n, 0.0);
        Rng rng (seed);
        const double len = 0.085 * sr, ratio = std::exp2 (s.semis / 12.0);
        const Buf& src = bank.forRatio (ratio);
        const double span = std::max (1.0, c1 - c0 - len * ratio);
        double head = c0 + 0.5 * span;
        for (double t = -len; t < n; t += len * (0.22 + 0.08 * rng.uni()))
        {
            head += (rng.sym() * 0.25 + 0.05) * len; if (head < c0) head = c0 + (c0 - head); if (head > c0 + span) head = c0 + span - (head - c0 - span);
            head = juce::jlimit (c0, c0 + span, head);
            const double r = ratio * std::exp2 (rng.sym() * s.driftCents / 1200.0);
            for (int i = 0; i < (int) len; ++i)
            {
                const int o = (int) t + i; if (o < 0 || o >= n) continue;
                const double w = tukey ((double) i / len, 0.7);
                y[(size_t) o] += w * (s.clean ? readSinc (src, 0, head + i * r) : monoRead (src, head + i * r)); ws[(size_t) o] += w;
            }
        }
        double mw = 0; for (double w : ws) mw += w; mw /= std::max (1, n);
        std::vector<float> out ((size_t) n); for (int i = 0; i < n; ++i) out[(size_t) i] = (float) (y[(size_t) i] / std::max (0.5 * mw, ws[(size_t) i]));
        return out;
    };
    const auto A = walk ((uint64) s.seed * 31u + 3u);
    std::vector<float> B; if (s.width > 0.01) B = walk ((uint64) s.seed * 131u + 7u);
    auto o = toStereo (A, B, s.width);
    fadeEdges (o, (int) (0.003 * sr), 0);
    return o;
}

// ---------------------------------------------------------------- the grain cloud
Buf cloud (const Buf& work, const BandBank& bank, double sr, const std::vector<GrainSource>& src, const CloudSpec& c)
{
    const int n = juce::jmax (16, (int) (c.seconds * sr));
    const bool mono = c.channels == 1;
    Buf out (mono ? 1 : 2, n); out.clear();
    if (src.empty()) return out;
    Rng rng ((uint64) c.seed * 2654435761u + 11u);
    std::vector<double> sw; for (auto& s : src) sw.push_back (std::max (1e-6, s.weight));
    std::vector<double> head (src.size()), dir (src.size(), 1.0);
    for (size_t i = 0; i < src.size(); ++i) head[i] = rng.uni();
    std::vector<double> nw = c.noteWeights; if (nw.size() != c.notes.size()) nw.assign (c.notes.size(), 1.0);
    float* L = out.getWritePointer (0); float* R = mono ? nullptr : out.getWritePointer (1);
    double density = c.density;
    if (c.normalise && c.clean) density = juce::jmax (density, 2.0 / juce::jmax (0.01, c.grainS));   // (at least 50 % overlap)
    const double meanGap = 1.0 / juce::jmax (0.2, density);
    auto win = [&] (double x) -> float
    {
        if (c.window == 1) return (float) (0.5 - 0.5 * std::cos (2.0 * kPi * x));
        if (c.window == 2) { const double u = (x - 0.5) / 0.18; return (float) (std::exp (-0.5 * u * u) * (0.5 - 0.5 * std::cos (2.0 * kPi * x))); }   // (a Gaussian, its ends tapered to 0)
        return tukey (x, c.taper);
    };
    double winPow = 0; for (int i = 0; i < 256; ++i) { const double w = win ((i + 0.5) / 256.0); winPow += w * w; } winPow /= 256.0;
    double t = 0;
    while (t < c.seconds)
    {
        const double shape = c.densityAt ? juce::jlimit (0.05, 1.5, c.densityAt (t)) : 1.0;
        const double gap = meanGap / shape * (0.4 + 1.2 * rng.uni());
        const int si = rng.weighted (sw); const auto& S = src[(size_t) si];
        const double spanS = std::max (1e-3, (S.b - S.a) / sr);
        head[(size_t) si] += dir[(size_t) si] * gap * c.headSpeed / spanS;
        if (head[(size_t) si] > 1.0) { head[(size_t) si] = 2.0 - head[(size_t) si]; dir[(size_t) si] = -1.0; }
        if (head[(size_t) si] < 0.0) { head[(size_t) si] = -head[(size_t) si]; dir[(size_t) si] = 1.0; }
        const double lenS = c.grainS * (1.0 + c.grainJitter * rng.sym());
        const double len = std::max (64.0, lenS * sr);
        double shiftSt = 0;
        if (! c.notes.empty())
        {
            double note = c.notes[(size_t) rng.weighted (nw)];
            if (S.midi > 0)
            {
                if (c.foldOctaves) note += 12.0 * std::round ((S.midi - note) / 12.0);
                const double full = note - S.midi;
                const double octaveOnly = 12.0 * std::round (full / 12.0);
                shiftSt = full + (octaveOnly - full) * juce::jlimit (0.0, 1.0, c.natural);
            }
        }
        shiftSt += rng.sym() * c.scatterCents / 100.0;
        const double ratio = std::exp2 (shiftSt / 12.0);
        const double need = len * ratio;
        const double room = std::max (0.0, (S.b - S.a) - need);
        double pos = S.a + room * juce::jlimit (0.0, 1.0, head[(size_t) si] + c.spray * rng.sym() * 0.5);
        pos = juce::jlimit (S.a, std::max (S.a, S.b - need), pos);
        const bool rev = rng.chance (c.reverse);
        double g = dbToGain (rng.sym() * c.gainJitterDb);
        if (c.normalise) g /= std::sqrt (juce::jmax (1.0, density * shape * lenS * winPow));   // (uncorrelated grains add in power)
        const double pan = juce::jlimit (-1.0, 1.0, rng.sym() * c.spread);
        const float gl = (float) (g * std::cos ((pan + 1.0) * kPi / 4.0)), gr = (float) (g * std::sin ((pan + 1.0) * kPi / 4.0));
        const Buf& b = bank.forRatio (ratio);
        const int start = (int) (t * sr);
        const bool stereo = b.getNumChannels() > 1;
        if (mono || c.clean)
        {
            for (int i = 0; i < (int) len && start + i < n; ++i)
            {
                const float w = c.normalise ? win ((double) i / len) : tukey ((double) i / len, c.taper);
                const double p = rev ? pos + need - i * ratio : pos + i * ratio;
                const float l = c.clean ? readSinc (b, 0, p) : readAt (b, 0, p);
                if (mono) { L[start + i] += w * (float) g * (stereo ? 0.5f * (l + (c.clean ? readSinc (b, 1, p) : readAt (b, 1, p))) : l); continue; }
                const float r = stereo ? readSinc (b, 1, p) : l;
                L[start + i] += w * gl * l; R[start + i] += w * gr * r;
            }
        }
        else
        for (int i = 0; i < (int) len && start + i < n; ++i)
        {
            const double x = (double) i / len;
            const float w = c.normalise ? win (x) : tukey (x, c.taper);
            const double p = rev ? pos + need - i * ratio : pos + i * ratio;
            const float l = readAt (b, 0, p), r = stereo ? readAt (b, 1, p) : l;
            L[start + i] += w * gl * l; R[start + i] += w * gr * r;
        }
        t += gap;
    }
    fadeEdges (out, (int) (c.fadeS * sr), (int) (c.fadeS * sr));
    juce::ignoreUnused (work);
    return out;
}

// ---------------------------------------------------------------- spectral
namespace
{
Buf spectralOf (const Buf& in, double sr, double a, double b, double seconds, HoldMode mode, double smear, uint32_t seed)
{
    auto s = std::make_shared<SourceAudio>();
    s->audio = stereoOf (in); s->sampleRate = sr; s->frames = s->audio.getNumSamples(); s->channels = 2; s->fingerprint = "gen";
    WorkingAudio w; w.src = s; w.rate = sr; w.ratio = 1.0;
    hold::HoldPlan P;
    P.mode = P.chosen = mode; P.holdId = (int64) seed + 1; P.uStart = 0; P.len = juce::jmax<int64> (64, (int64) (seconds * sr));
    P.sa = juce::jlimit (0.0, (double) s->frames, a); P.sb = juce::jlimit (P.sa, (double) s->frames, b); P.selA = P.sa; P.selB = P.sb;
    P.joinIn = P.joinOut = false; P.xf = juce::jmax<int64> (1, (int64) (0.03 * sr)); P.smear = juce::jlimit (0.05, 1.0, smear);
    P.seed = seed; P.rate = sr; P.hash = (uint64) seed * 0x9E3779B97F4A7C15ull + 0x51u;
    Buf out;
    if (! hold::renderSpectral (P, w, out, {})) { out.setSize (2, (int) P.len); out.clear(); }
    return out;
}
}
Buf freeze (const Buf& in, double sr, double a, double b, double seconds, uint32_t seed) { return spectralOf (in, sr, a, b, seconds, HoldMode::Freeze, 1.0, seed); }
Buf blur (const Buf& in, double sr, double a, double b, double seconds, double smear, uint32_t seed) { return spectralOf (in, sr, a, b, seconds, HoldMode::Texture, smear, seed); }

Buf shift (const Buf& in, double sr, double semis, bool keepFormants, double formantSemis, double f0Hz, bool monoOut)
{
    const int nIn = in.getNumSamples();
    if (nIn == 0) return {};
    const bool one = monoOut && in.getNumChannels() == 1;
    if (std::abs (semis) < 1e-6 && std::abs (formantSemis) < 1e-6) { if (one) { Buf o; o.makeCopyOf (in); return o; } return stereoOf (in); }
    signalsmith::stretch::SignalsmithStretch<float> st;
    if (one)
    {
        st.presetDefault (1, (float) sr, false);
        st.setTransposeSemitones ((float) semis, (float) (8000.0 / sr));
        st.setFormantBase (f0Hz > 0 ? (float) (f0Hz / sr) : 0.0f);
        if (keepFormants) st.setFormantSemitones ((float) formantSemis, true);
        else if (std::abs (formantSemis) > 1e-6) st.setFormantSemitones ((float) formantSemis, false);
        else st.setFormantFactor (1.0f, false);
        const int lat = st.inputLatency() + st.outputLatency();
        std::vector<float> src ((size_t) (nIn + lat * 2), 0.0f), dst ((size_t) (nIn + lat * 2), 0.0f);
        std::copy (in.getReadPointer (0), in.getReadPointer (0) + nIn, src.begin());
        float* ip[1] { src.data() }; float* op[1] { dst.data() };
        st.process (ip, nIn + lat * 2, op, nIn + lat * 2);
        Buf res (1, nIn); res.copyFrom (0, 0, dst.data() + juce::jmin (lat, (int) dst.size() - nIn), nIn);
        return res;
    }
    st.presetDefault (2, (float) sr, false);
    st.setTransposeSemitones ((float) semis, (float) (8000.0 / sr));
    st.setFormantBase (f0Hz > 0 ? (float) (f0Hz / sr) : 0.0f);
    if (keepFormants) st.setFormantSemitones ((float) formantSemis, true);
    else if (std::abs (formantSemis) > 1e-6) st.setFormantSemitones ((float) formantSemis, false);
    else st.setFormantFactor (1.0f, false);
    // the stretcher's latency: feed a padded copy and read the aligned part (seek-free, exact length)
    const int lat = st.inputLatency() + st.outputLatency();
    Buf src (2, nIn + lat * 2); src.clear();
    for (int ch = 0; ch < 2; ++ch) src.copyFrom (ch, 0, in, juce::jmin (ch, in.getNumChannels() - 1), 0, nIn);
    Buf out (2, nIn + lat * 2); out.clear();
    float* ip[2] { src.getWritePointer (0), src.getWritePointer (1) }; float* op[2] { out.getWritePointer (0), out.getWritePointer (1) };
    st.process (ip, nIn + lat * 2, op, nIn + lat * 2);
    Buf res (2, nIn);
    for (int ch = 0; ch < 2; ++ch) res.copyFrom (ch, 0, out, ch, juce::jmin (lat, out.getNumSamples() - nIn), nIn);
    return res;
}

// ---------------------------------------------------------------- loops, loudness, finishing
void bakeLoop (Buf& y, int loopStart, int loopEnd, int xf)
{
    xf = juce::jmin (xf, loopStart, (loopEnd - loopStart) / 2);
    if (xf <= 1) return;
    for (int ch = 0; ch < y.getNumChannels(); ++ch)
    {
        float* d = y.getWritePointer (ch);
        for (int i = 0; i < xf; ++i)
        {
            const double x = (double) (i + 1) / (double) xf;
            const float wa = (float) std::cos (x * kPi * 0.5), wb = (float) std::sin (x * kPi * 0.5);
            d[loopEnd - xf + i] = wa * d[loopEnd - xf + i] + wb * d[loopStart - xf + i];
        }
    }
}

double lufs (const Buf& b, double rate)
{
    const int n = b.getNumSamples(); if (n < (int) (0.4 * rate)) return -100.0;
    // K-weighting (BS.1770-4 stage 1 shelf + stage 2 high-pass), recomputed for the rate
    auto shelf = [rate]
    {
        Biquad f; const double G = 3.999843853973347, f0 = 1681.974450955533, Q = 0.7071752369554196;
        const double K = std::tan (kPi * f0 / rate), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        f.b0 = (Vh + Vb * K / Q + K * K) / a0; f.b1 = 2.0 * (K * K - Vh) / a0; f.b2 = (Vh - Vb * K / Q + K * K) / a0;
        f.a1 = 2.0 * (K * K - 1.0) / a0; f.a2 = (1.0 - K / Q + K * K) / a0; return f;
    };
    auto hp = [rate]
    {
        Biquad f; const double f0 = 38.13547087602444, Q = 0.5003270373238773, K = std::tan (kPi * f0 / rate);
        const double a0 = 1.0 + K / Q + K * K;
        f.b0 = 1.0 / a0; f.b1 = -2.0 / a0; f.b2 = 1.0 / a0; f.a1 = 2.0 * (K * K - 1.0) / a0; f.a2 = (1.0 - K / Q + K * K) / a0; return f;
    };
    const int block = (int) (0.4 * rate), hop = block / 4;
    std::vector<double> e2 ((size_t) n, 0.0);
    for (int ch = 0; ch < juce::jmin (2, b.getNumChannels()); ++ch)
    {
        auto s = shelf(); auto h = hp(); const float* x = b.getReadPointer (ch);
        for (int i = 0; i < n; ++i) { const double v = h.run (s.run (x[i])); e2[(size_t) i] += v * v; }
    }
    if (b.getNumChannels() == 1) for (auto& v : e2) v *= 2.0;   // a mono file plays on both speakers
    std::vector<double> csum ((size_t) n + 1, 0.0); for (int i = 0; i < n; ++i) csum[(size_t) i + 1] = csum[(size_t) i] + e2[(size_t) i];
    std::vector<double> blocks;
    for (int s0 = 0; s0 + block <= n; s0 += hop) blocks.push_back ((csum[(size_t) (s0 + block)] - csum[(size_t) s0]) / block);
    auto L = [] (double ms) { return -0.691 + 10.0 * std::log10 (ms + 1e-30); };
    double sum = 0; int cnt = 0;
    for (double m : blocks) if (L (m) > -70.0) { sum += m; ++cnt; }
    if (cnt == 0) return -100.0;
    const double rel = L (sum / cnt) - 10.0;
    double s2 = 0; int c2 = 0;
    for (double m : blocks) if (L (m) > -70.0 && L (m) > rel) { s2 += m; ++c2; }
    return c2 > 0 ? L (s2 / c2) : L (sum / cnt);
}

double truePeakDb (const Buf& b)
{
    ghosty::clean::Interp4 ip; float pk = 0;
    const int n = b.getNumSamples(); const bool st = b.getNumChannels() > 1;
    for (int i = 0; i < n + ghosty::clean::Interp4::T; ++i)
    {
        const float l = i < n ? b.getSample (0, i) : 0.0f, r = i < n ? (st ? b.getSample (1, i) : l) : 0.0f;
        ip.push (l, r);
        for (int ch = 0; ch < 2; ++ch)
        {
            pk = std::max (pk, std::abs (ip.delayed (ch)));
            for (int p = 1; p < 4; ++p) pk = std::max (pk, std::abs (ip.at (ch, p)));
        }
    }
    return gainToDb (pk);
}

double finish (Buf& b, double sr, double targetLufs, double peakDb, bool limit)
{
    juce::ScopedNoDenormals nd;   // (offline: the same flush-to-zero the audio thread has)
    if (b.getNumChannels() < 2) b = stereoOf (b);
    const double l = lufs (b, sr);
    if (l <= -99.0) return l;
    b.applyGain ((float) dbToGain (targetLufs - l));
    if (! limit)
    {   // CLEAR: one gain, nothing limited
        const double tp = truePeakDb (b);
        if (tp > peakDb - 0.1) b.applyGain ((float) dbToGain (peakDb - 0.1 - tp));
        return lufs (b, sr);
    }
    ghosty::clean::Ceiling c; c.ceilingDb = (float) (peakDb - 0.2);   // (its soft clip keeps its own knee: -0.7 dBFS, above the target)
    c.prepare (sr);
    const int lat = c.latency(), n = b.getNumSamples();
    Buf t (2, n + lat); t.clear();
    for (int ch = 0; ch < 2; ++ch) t.copyFrom (ch, 0, b, ch, 0, n);
    const int blk = 512;
    for (int i = 0; i < n + lat; i += blk) c.process (t.getWritePointer (0) + i, t.getWritePointer (1) + i, juce::jmin (blk, n + lat - i));
    for (int ch = 0; ch < 2; ++ch) b.copyFrom (ch, 0, t, ch, lat, n);
    const double tp = truePeakDb (b);
    if (tp > peakDb - 0.05) b.applyGain ((float) dbToGain (peakDb - 0.1 - tp));
    return lufs (b, sr);
}

int clicksIn (const Buf& b, float jump)
{   // a click = a second difference far above its neighbourhood's (a step or a spike the material itself does not have)
    int count = 0; const int n = b.getNumSamples(), W = 256;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
    {
        const float* x = b.getReadPointer (ch);
        std::vector<float> d2 ((size_t) juce::jmax (0, n));
        for (int i = 1; i + 1 < n; ++i) d2[(size_t) i] = std::abs (x[i + 1] - 2.0f * x[i] + x[i - 1]);
        std::vector<double> cs ((size_t) n + 1, 0.0); for (int i = 0; i < n; ++i) cs[(size_t) i + 1] = cs[(size_t) i] + (double) d2[(size_t) i] * d2[(size_t) i];
        for (int i = 1; i + 1 < n; ++i)
        {
            const int a = juce::jmax (0, i - W), e = juce::jmin (n, i + W);
            const double loc = std::sqrt ((cs[(size_t) e] - cs[(size_t) a] - (double) d2[(size_t) i] * d2[(size_t) i]) / juce::jmax (1, e - a - 1));
            if (d2[(size_t) i] > jump * 0.25f && d2[(size_t) i] > 10.0 * loc + 1e-4) { ++count; i += 32; }
        }
    }
    return count;
}

bool finiteAndClean (const Buf& b)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
    {
        const float* x = b.getReadPointer (ch);
        for (int i = 0; i < b.getNumSamples(); ++i) { const int c = std::fpclassify (x[i]); if (c == FP_NAN || c == FP_INFINITE || c == FP_SUBNORMAL) return false; }
    }
    return true;
}
} // namespace gslice::gen::dsp
