#pragma once
#include <juce_dsp/juce_dsp.h>
#include "Waves.h"
#include <map>
#include <mutex>

// Ghosty Pro wavetables: a table is 1..64 frames, every frame turned into band-limited mip tables with an FFT.
// Frames are built off the audio thread and never change after; the audio thread reads them through a WaveMailbox
// style hand-over (ProWaveSet below).
namespace gpro
{
using ghosty::Frame;
constexpr int TBL = 2048;                // samples per mip table (power of two)
constexpr int TBL_MIPS = 11;
constexpr int MAX_FRAMES = 64;
constexpr int SAMPLE_FRAMES = 32;        // frames made from an imported sample

using HiFrame = std::vector<float>;      // any power-of-two length (256 for drawn frames, 2048 for built-in shapes)

inline const juce::StringArray& tableNames()
{
    static juce::StringArray s { "Ghost A/B", "Ghosty Shapes", "Classic", "Saw", "Pulse", "Harmonics", "Hollow", "Formant", "Choir",
                                 "Organ", "Glass", "FM Growl", "FM Metal", "Fold", "Sync", "PD Res", "Dirty", "Digital", "Sample", "User" };   // ("User" = the WAVETABLE EDITOR's table, round 3 2b, 2026-10-24)
    return s;
}
enum TableId { tGhost = 0, tShapes, tClassic, tSaw, tPulse, tHarm, tHollow, tFormant, tChoir, tOrgan, tGlass, tFmGrowl, tFmMetal,
               tFold, tSync, tPdRes, tDirty, tDigital, tSample, tUser, numTables };

// One frame -> TBL_MIPS band-limited tables.
struct MipFrame
{
    std::array<std::vector<float>, TBL_MIPS> t;
    // FINE MIPS (sound audit 2026-10-26), read by the 2x / 4x engine (QUALITY High / Ultra) as mip index TBL_MIPS + k: a third-octave
    // grid from 25 Hz, each mip cut at HALF the host rate for the top note of its third (not 0.45 x for the top of an octave), so every
    // note keeps the audio band (48 kHz: 19 .. 24 kHz; the old octave mips stopped at 9.6 kHz on E4) and nothing far above it reaches the
    // engine's nonlinear stages (the ladder's input, the drive) to fold back. Each mip has 16 points per cycle of its top harmonic (11 on
    // the two lowest; 256 .. 8192 points), so the cubic read leaves no images (8 points left -84 dB on E3). About 3 x the octave mips' memory.
    static constexpr int FINE = 30;
    static constexpr double FINE_BASE = 25.0;
    std::array<std::vector<float>, FINE> f;
    std::array<int, FINE> fMask {};      // each fine mip's length - 1 (a power of two - 1), set by buildFrame
    static int fineFor (double freq) noexcept
    {
        return TBL_MIPS + juce::jlimit (0, FINE - 1, (int) std::floor (3.0 * std::log2 (std::max (FINE_BASE, freq) / FINE_BASE)));
    }
    // any read phase is memory-safe: the integer part is masked into the table (2's complement, so a negative phase wraps too)
    // and the fraction is left as it is (no floor() or branches per read: those cost 15-30 % on 256 unison voices). A phase
    // in [0, 1) reads bit-identically; the oscillators keep their phases in [0, 1) (see the phase wrap in ProVoice), so an
    // out-of-range phase only ever reads a slightly wrong value instead of memory past the table.
    static inline int safeIndex (double x, float& f) noexcept
    {
        const int i = (int) x; f = (float) (x - i);
        return i & (TBL - 1);
    }
    inline float read (int mip, double phase) const noexcept
    {
        const auto* tab = t[(size_t) mip].data();
        float fr; const int i = safeIndex (phase * TBL, fr);
        return tab[i] + (tab[i + 1] - tab[i]) * fr;
    }
    // 4-point cubic (Catmull-Rom / Hermite) read (round 3, 2026-10-24): the low mips hold up to 540 harmonics in 2048 points (under
    // 4 points per cycle of the top one) and a linear read of them left images folding back at about -56 dB on a C1 saw; the cubic read
    // puts them below -80 dB. The table is periodic, so the neighbours wrap.
    inline float read4 (int mip, double phase) const noexcept
    {
        if (mip >= TBL_MIPS) return readFine (mip - TBL_MIPS, phase);
        const auto* tab = t[(size_t) mip].data();
        constexpr int M = TBL - 1;
        float f; const int i = safeIndex (phase * TBL, f);
        const float ym1 = tab[(i - 1) & M], y0 = tab[i], y1 = tab[i + 1], y2 = tab[(i + 2) & M];   // (each mip holds TBL + 1 points: i + 1 <= TBL)
        const float c1 = 0.5f * (y1 - ym1), c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2, c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * f + c2) * f + c1) * f + y0;
    }
    // a fine mip: the same 4-point cubic on its own power-of-two length (every frame has them: buildFrame always makes them)
    inline float readFine (int k, double phase) const noexcept
    {
        const float* tab = f[(size_t) k].data(); const int M = fMask[(size_t) k];
        const double x = phase * (double) (M + 1); const int i0 = (int) x; const float fr = (float) (x - i0); const int i = i0 & M;
        const float ym1 = tab[(i - 1) & M], y0 = tab[i], y1 = tab[i + 1], y2 = tab[(i + 2) & M];
        const float c1 = 0.5f * (y1 - ym1), c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2, c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * fr + c2) * fr + c1) * fr + y0;
    }
    static int mipFor (double freq) noexcept
    {
        return juce::jlimit (0, TBL_MIPS - 1, (int) std::floor (std::log2 (std::max (20.0, freq) / 20.0)));
    }
};

struct Table
{
    std::vector<MipFrame> frames;
    std::vector<ghosty::Frame> preview;     // 256-point versions for drawing
    int id = 0;

    // pos 0..1 across the frames; reads frame k and k+1 and blends
    struct Cursor { const MipFrame* a; const MipFrame* b; float fr; };
    Cursor cursor (float pos) const noexcept
    {
        const int n = (int) frames.size();
        if (n <= 1) return { &frames[0], &frames[0], 0.0f };
        const float x = juce::jlimit (0.0f, 1.0f, pos) * (float) (n - 1);
        const int k = juce::jmin (n - 2, (int) x);
        return { &frames[(size_t) k], &frames[(size_t) k + 1], x - (float) k };
    }
};

inline juce::dsp::FFT& fftFor (int order)
{
    static juce::dsp::FFT f8 (8), f9 (9), f10 (10), f11 (11), f12 (12), f13 (13), f14 (14);
    switch (order) { case 8: return f8; case 9: return f9; case 10: return f10; case 12: return f12; case 13: return f13; case 14: return f14; default: return f11; }
}

// Builds the mips of one frame. src length must be 256, 512, 1024 or 2048.
inline void buildFrame (const HiFrame& src, double sampleRate, MipFrame& out)
{
    using C = std::complex<float>;
    const int N = (int) src.size();
    const int order = N >= 2048 ? 11 : N >= 1024 ? 10 : N >= 512 ? 9 : 8;
    const int n = 1 << order;
    std::vector<C> in ((size_t) n), spec ((size_t) n), tbl ((size_t) TBL), res ((size_t) TBL);
    for (int i = 0; i < n; ++i) in[(size_t) i] = C (src[(size_t) (i % N)], 0.0f);
    fftFor (order).perform (in.data(), spec.data(), false);
    const int maxSrcH = n / 2 - 1;
    const float scale = (float) TBL / (float) n;
    for (int m = 0; m < TBL_MIPS; ++m)
    {
        const double topFreq = 40.0 * std::pow (2.0, m);
        const int maxH = juce::jlimit (1, juce::jmin (maxSrcH, TBL / 2 - 1), (int) (sampleRate * 0.45 / topFreq));
        std::fill (tbl.begin(), tbl.end(), C());
        for (int k = 1; k <= maxH; ++k)
        {
            // gentle roll-off over the top 15% of the band: no hard brick-wall ringing
            const float edge = (float) k / (float) maxH;
            const float w = edge > 0.85f ? 0.5f + 0.5f * std::cos ((edge - 0.85f) / 0.15f * juce::MathConstants<float>::pi) : 1.0f;
            tbl[(size_t) k] = spec[(size_t) k] * scale * w;
            tbl[(size_t) (TBL - k)] = std::conj (tbl[(size_t) k]);
        }
        fftFor (11).perform (tbl.data(), res.data(), true);
        auto& t = out.t[(size_t) m];
        t.resize (TBL + 1);
        for (int i = 0; i < TBL; ++i) t[(size_t) i] = res[(size_t) i].real();
        t[TBL] = t[0];
    }
    // the fine mips (see MipFrame::FINE): no taper (the cut sits at the band's edge), length = the power of two >= 8 x the top harmonic
    for (int k = 0; k < MipFrame::FINE; ++k)
    {
        const double top = MipFrame::FINE_BASE * std::pow (2.0, (k + 1) / 3.0);
        const int maxH = juce::jlimit (1, maxSrcH, (int) (sampleRate * 0.5 / top));
        int L = 256; while (L < 16 * maxH && L < 8192) L <<= 1;
        int ord = 8; while ((1 << ord) < L) ++ord;
        std::vector<C> fi ((size_t) L), fo ((size_t) L);
        const float sc = (float) L / (float) n;
        for (int h = 1; h <= maxH && h < L / 2; ++h) { fi[(size_t) h] = spec[(size_t) h] * sc; fi[(size_t) (L - h)] = std::conj (fi[(size_t) h]); }
        fftFor (ord).perform (fi.data(), fo.data(), true);
        auto& fv = out.f[(size_t) k];
        fv.resize ((size_t) L + 1); out.fMask[(size_t) k] = L - 1;
        for (int i = 0; i < L; ++i) fv[(size_t) i] = fo[(size_t) i].real();
        fv[(size_t) L] = fv[0];
    }
}

inline void normaliseHi (HiFrame& f)
{
    double mean = 0; for (auto v : f) mean += v; mean /= (double) f.size();
    float mx = 1e-9f; for (auto& v : f) { v -= (float) mean; mx = std::max (mx, std::abs (v)); }
    for (auto& v : f) v /= mx;
}

inline ghosty::Frame toPreview (const HiFrame& f)
{
    ghosty::Frame p {};
    const int N = (int) f.size();
    for (int i = 0; i < ghosty::FRAME; ++i) p[(size_t) i] = f[(size_t) (i * N / ghosty::FRAME)];
    return p;
}

inline HiFrame fromFrame (const ghosty::Frame& f) { return HiFrame (f.begin(), f.end()); }

// ---------------------------------------------------------------- built-in tables (16 frames, 2048 points)
inline HiFrame additive (const std::function<double (int)>& amp, int maxH = 512, const std::function<double (int)>& phase = nullptr)
{
    HiFrame f ((size_t) TBL, 0.0f);
    const double tau = juce::MathConstants<double>::twoPi;
    std::vector<double> acc ((size_t) TBL, 0.0);
    for (int k = 1; k <= maxH; ++k)
    {
        const double a = amp (k);
        if (std::abs (a) < 1e-7) continue;
        const double ph = phase ? phase (k) : 0.0;
        for (int i = 0; i < TBL; ++i) acc[(size_t) i] += a * std::sin (tau * k * i / TBL + ph);
    }
    for (int i = 0; i < TBL; ++i) f[(size_t) i] = (float) acc[(size_t) i];
    return f;
}

inline HiFrame shaped (const std::function<double (double)>& fn)
{
    HiFrame f ((size_t) TBL);
    for (int i = 0; i < TBL; ++i) f[(size_t) i] = (float) fn ((double) i / TBL);
    return f;
}

inline double formantAmp (double fr, double f1, double f2, double f3, double bw)
{
    return std::exp (-std::pow ((fr - f1) / bw, 2)) + 0.6 * std::exp (-std::pow ((fr - f2) / (bw * 1.2), 2))
         + 0.28 * std::exp (-std::pow ((fr - f3) / (bw * 2.0), 2));
}

// Ghosty's 16 shapes at full resolution (same recipes as Waves.h makeShape, sampled at 2048 points)
inline HiFrame ghostyShapeHi (int shape)
{
    const double tau = juce::MathConstants<double>::twoPi;
    switch (shape)
    {
        case 2:  return additive ([] (int k) { return 1.0 / k * (k % 2 ? -1.0 : 1.0); }, 600);                   // saw, band-limited
        case 3:  return additive ([] (int k) { return k % 2 ? 1.0 / k : 0.0; }, 600);                            // square
        case 4:  return additive ([] (int k) { return std::sin (juce::MathConstants<double>::pi * k * 0.18) / k; }, 600, [] (int) { return juce::MathConstants<double>::halfPi; });
        case 12: return shaped ([] (double p) { return 2.0 * std::fmod (p * 2.6, 1.0) - 1.0; });
        default: break;
    }
    auto f = ghosty::makeShape (shape);     // the smooth ones: resample the 256 frame through its spectrum
    HiFrame h ((size_t) TBL);
    for (int i = 0; i < TBL; ++i)
    {
        const double x = (double) i / TBL * ghosty::FRAME; const int a = (int) x; const double fr = x - a;
        h[(size_t) i] = (float) (f[(size_t) (a % ghosty::FRAME)] * (1 - fr) + f[(size_t) ((a + 1) % ghosty::FRAME)] * fr);
    }
    juce::ignoreUnused (tau);
    return h;
}

inline std::vector<HiFrame> makeBuiltIn (int id)
{
    std::vector<HiFrame> fr;
    const double pi = juce::MathConstants<double>::pi, tau = 2 * pi;
    constexpr int K = 16;
    auto lerpFrames = [] (const std::vector<HiFrame>& keys, int count)
    {
        std::vector<HiFrame> out;
        for (int i = 0; i < count; ++i)
        {
            const double x = (double) i / (count - 1) * (keys.size() - 1);
            const int a = juce::jmin ((int) keys.size() - 2, (int) x); const float t = (float) (x - a);
            HiFrame f ((size_t) TBL);
            for (int s = 0; s < TBL; ++s) f[(size_t) s] = keys[(size_t) a][(size_t) s] * (1 - t) + keys[(size_t) a + 1][(size_t) s] * t;
            out.push_back (std::move (f));
        }
        return out;
    };
    switch (id)
    {
        case tShapes: for (int s = 0; s < 16; ++s) fr.push_back (ghostyShapeHi (s)); break;
        case tClassic:
            fr = lerpFrames ({ ghostyShapeHi (0), ghostyShapeHi (1), ghostyShapeHi (2), ghostyShapeHi (3), ghostyShapeHi (4) }, K); break;
        case tSaw:   // saw -> saw minus a phase-shifted saw (a pulse-ish "hollow saw")
            for (int i = 0; i < K; ++i)
            {
                const double sh = 0.5 * i / (K - 1);
                fr.push_back (additive ([sh, i, K] (int k) { juce::ignoreUnused (K); return 1.0 / k * (1.0 - (i > 0 ? 0.9 * std::cos (2 * juce::MathConstants<double>::pi * k * sh) : 0.0)); }, 600));
            }
            break;
        case tPulse:
            for (int i = 0; i < K; ++i)
            {
                const double w = 0.5 - 0.46 * i / (K - 1);
                fr.push_back (additive ([w] (int k) { return std::sin (juce::MathConstants<double>::pi * k * w) / k; }, 600, [] (int) { return juce::MathConstants<double>::halfPi; }));
            }
            break;
        case tHarm:
            for (int i = 0; i < K; ++i)
            {
                const double top = 1.0 + std::pow ((double) i / (K - 1), 2.0) * 48.0;
                fr.push_back (additive ([top] (int k) { const double x = (double) k; return x > top + 1 ? 0.0 : (x > top ? top + 1 - x : 1.0) / std::sqrt (x); }, 64));
            }
            break;
        case tHollow:
            for (int i = 0; i < K; ++i)
            {
                const double t = (double) i / (K - 1);
                fr.push_back (additive ([t] (int k) { return (k % 2 ? 1.0 : (1.0 - t) * 0.8) / std::pow ((double) k, 1.0 + t); }, 400));
            }
            break;
        case tFormant: case tChoir:
        {
            // A E I O U vowels on a harmonic comb
            static const double V[5][3] { { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 290, 2290, 3010 }, { 570, 840, 2410 }, { 320, 870, 2240 } };
            const double base = id == tFormant ? 110.0 : 165.0, bw = id == tFormant ? 140.0 : 220.0;
            std::vector<HiFrame> keys;
            for (auto& v : V)
                keys.push_back (additive ([&] (int k) { return formantAmp (k * base, v[0], v[1], v[2], bw) + (id == tChoir ? 0.05 : 0.1) / k; }, 120,
                                          id == tChoir ? std::function<double (int)> ([] (int k) { return std::fmod (k * 2.39996, 6.2832); }) : nullptr));
            fr = lerpFrames (keys, K);
            break;
        }
        case tOrgan:
        {
            static const double bars[4][9] { { 8, 8, 8, 0, 0, 0, 0, 0, 0 }, { 8, 8, 8, 8, 0, 0, 0, 0, 8 }, { 8, 6, 8, 8, 6, 8, 0, 6, 8 }, { 8, 8, 8, 8, 8, 8, 8, 8, 8 } };
            static const int harm[9] { 1, 3, 2, 4, 6, 8, 10, 12, 16 };   // 16' 5 1/3' 8' 4' 2 2/3' 2' 1 3/5' 1 1/3' 1'  (relative to the 16' sub)
            std::vector<HiFrame> keys;
            for (auto& b : bars)
                keys.push_back (additive ([&] (int k) { double a = 0; for (int d = 0; d < 9; ++d) if (harm[d] == k) a += std::pow (10.0, (b[d] - 8) * 3.0 / 20.0) * (b[d] > 0 ? 1 : 0); return a; }, 16));
            fr = lerpFrames (keys, K);
            break;
        }
        case tGlass:
            for (int i = 0; i < K; ++i)
            {
                const double t = (double) i / (K - 1);
                fr.push_back (additive ([t] (int k) {
                    static const int P[] { 1, 4, 9, 13, 17, 23, 29 };
                    for (int j = 0; j < 7; ++j) if (P[j] == k) return j == 0 ? 1.0 : std::pow (0.75, j) * (0.25 + 1.6 * t) * (j > 3 ? t : 1.0);
                    return 0.0; }, 32));
            }
            break;
        case tFmGrowl: case tFmMetal:
        {
            const double ratio = id == tFmGrowl ? 2.0 : 7.0, maxI = id == tFmGrowl ? 7.0 : 4.0;
            for (int i = 0; i < K; ++i)
            {
                const double I = maxI * std::pow ((double) i / (K - 1), 1.3);
                fr.push_back (shaped ([=] (double p) { return std::sin (tau * p + I * std::sin (tau * ratio * p)); }));
            }
            break;
        }
        case tFold:
            for (int i = 0; i < K; ++i)
            {
                const double g = 1.0 + 7.0 * (double) i / (K - 1);
                fr.push_back (shaped ([=] (double p) { return std::sin (g * std::sin (tau * p)); }));
            }
            break;
        case tSync:
            for (int i = 0; i < K; ++i)
            {
                const double r = 1.0 + 5.0 * (double) i / (K - 1);
                fr.push_back (shaped ([=] (double p) { const double q = std::fmod (p * r, 1.0); return (2.0 * q - 1.0) * (0.5 + 0.5 * std::cos (pi * (2 * p - 1))) + 0.0; }));
            }
            break;
        case tPdRes:   // phase-distortion resonance: a windowed sine that rises in frequency
            for (int i = 0; i < K; ++i)
            {
                const double r = 1.0 + 11.0 * (double) i / (K - 1);
                fr.push_back (shaped ([=] (double p) { return std::cos (tau * r * p) * (1.0 - p); }));
            }
            break;
        case tDirty:
            for (int i = 0; i < K; ++i)
            {
                const double d = (double) i / (K - 1);
                fr.push_back (shaped ([=] (double p) {
                    const double saw = 2.0 * p - 1.0, sq = p < 0.5 ? 1.0 : -1.0;
                    const double x = saw * (1 - d * 0.5) + sq * d * 0.5 + 0.4 * d * std::sin (tau * 3 * p);
                    return std::tanh ((1.0 + 6.0 * d) * x + d * 0.6) - std::tanh (d * 0.6); }));
            }
            break;
        case tDigital:
            for (int i = 0; i < K; ++i)
            {
                const double steps = std::max (1.0, 16.0 - 15.0 * (double) i / (K - 1));
                fr.push_back (shaped ([=] (double p) { const double s = std::sin (tau * p) + 0.4 * std::sin (tau * 3 * p); return std::round (s * steps) / steps; }));
            }
            break;
        default: fr.push_back (ghostyShapeHi (2)); break;
    }
    for (auto& f : fr) normaliseHi (f);
    return fr;
}

inline std::shared_ptr<const Table> buildTable (const std::vector<HiFrame>& src, double sr, int id)
{
    auto t = std::make_shared<Table>();
    t->id = id;
    t->frames.resize (src.size());
    for (size_t i = 0; i < src.size(); ++i) { buildFrame (src[i], sr, t->frames[i]); t->preview.push_back (toPreview (src[i])); }
    if (t->frames.empty()) { t->frames.resize (1); buildFrame (ghostyShapeHi (2), sr, t->frames[0]); t->preview.push_back (toPreview (ghostyShapeHi (2))); }
    return t;
}

// message thread: built-in tables are cached (a few MB each at most)
class TableCache
{
public:
    std::shared_ptr<const Table> get (int id, double sr)
    {
        const auto key = std::make_pair (id, (int) std::lround (sr));
        auto it = cache.find (key);
        if (it != cache.end()) { it->second.second = ++clock; return it->second.first; }
        auto t = buildTable (makeBuiltIn (id), sr, id);
        cache[key] = { t, ++clock };
        while (cache.size() > 10)
        {
            auto oldest = cache.begin();
            for (auto i = cache.begin(); i != cache.end(); ++i) if (i->second.second < oldest->second.second) oldest = i;
            cache.erase (oldest);
        }
        return t;
    }
private:
    std::map<std::pair<int, int>, std::pair<std::shared_ptr<const Table>, juce::uint64>> cache;
    juce::uint64 clock = 0;
};

// What the audio thread reads: one table per oscillator.
struct WaveSet
{
    std::shared_ptr<const Table> t1, t2;
    double sampleRate = 0;
};

// Same single-builder / lock-free audio hand-over as ghosty::WaveMailbox.
class Mailbox
{
public:
    Mailbox() = default;
    ~Mailbox() { delete pending.exchange (nullptr); delete trash.exchange (nullptr); delete active; delete previous; }
    void post (std::unique_ptr<WaveSet> s) { delete pending.exchange (s.release()); collect(); }
    void collect() { delete trash.exchange (nullptr); }
    // audio thread: the set to play. The one it replaces stays alive as previous() until the NEXT swap (only then it goes to the
    // message thread's trash), so the voices can crossfade from it: a redrawn / changed table never clicks in.
    const WaveSet* acquire() noexcept
    {
        if (pending.load (std::memory_order_acquire) != nullptr && trash.load (std::memory_order_acquire) == nullptr)
            if (auto* p = pending.exchange (nullptr, std::memory_order_acq_rel))
            {
                trash.store (previous, std::memory_order_release);
                previous = active; active = p; ++swaps;
            }
        return active;
    }
    const WaveSet* previousSet() const noexcept { return previous; }     // audio thread
    juce::uint32 swapCount() const noexcept { return swaps; }            // audio thread
private:
    std::atomic<WaveSet*> pending { nullptr }, trash { nullptr };
    WaveSet* active = nullptr;
    WaveSet* previous = nullptr;
    juce::uint32 swaps = 0;
    JUCE_DECLARE_NON_COPYABLE (Mailbox)
};

// ---------------------------------------------------------------- sample -> wavetable
// Pitch detector shared by sliceSample and the WAVETABLE EDITOR (moved out of sliceSample 2026-10-24, same math): the loudest 4096
// of x, normalised autocorrelation over periods 20 Hz .. 2 kHz, first strong peak. Returns the period in samples (0 = none) and its
// correlation in bestR.
inline int detectPeriod (const std::vector<float>& x, double sr, double& bestR)
{
    const int n = (int) x.size();
    bestR = 0;
    if (n < 64) return 0;
    // the loudest region for pitch detection
    int best = 0; double bestE = -1; const int win = juce::jmin (n, 4096);
    for (int s = 0; s + win <= n; s += win / 2) { double e = 0; for (int i = 0; i < win; ++i) e += x[(size_t) (s + i)] * x[(size_t) (s + i)]; if (e > bestE) { bestE = e; best = s; } }
    // normalised autocorrelation, periods 20 Hz..2 kHz
    const int minP = juce::jmax (8, (int) (sr / 2000.0)), maxP = juce::jmin (win / 2, (int) (sr / 20.0));
    int period = 0;
    if (maxP > minP)
    {
        std::vector<double> r ((size_t) maxP + 1, 0.0);
        double e0 = 0; for (int i = 0; i < win; ++i) e0 += x[(size_t) (best + i)] * x[(size_t) (best + i)];
        for (int p = minP; p <= maxP; ++p)
        {
            double s = 0, e1 = 0;
            for (int i = 0; i + p < win; ++i) { s += x[(size_t) (best + i)] * x[(size_t) (best + i + p)]; e1 += x[(size_t) (best + i + p)] * x[(size_t) (best + i + p)]; }
            r[(size_t) p] = s / std::sqrt (juce::jmax (1e-12, e0 * e1));
        }
        // first strong peak (avoids octave errors)
        double mx = 0; for (int p = minP; p <= maxP; ++p) mx = std::max (mx, r[(size_t) p]);
        for (int p = minP + 1; p < maxP; ++p)
            if (r[(size_t) p] > 0.85 * mx && r[(size_t) p] >= r[(size_t) p - 1] && r[(size_t) p] >= r[(size_t) p + 1]) { period = p; bestR = r[(size_t) p]; break; }
    }
    return period;
}

// Mono audio in, up to SAMPLE_FRAMES single-cycle frames (2048 points each) out.
// If the sample has a clear pitch, each frame is one period resampled; otherwise 2048-sample windows made loopable.
inline std::vector<HiFrame> sliceSample (const std::vector<float>& x, double sr, int wantFrames = SAMPLE_FRAMES)
{
    std::vector<HiFrame> out;
    const int n = (int) x.size();
    if (n < 64) return out;
    double bestR = 0; const int period = detectPeriod (x, sr, bestR);
    const bool pitched = period > 0 && bestR > 0.6;
    const int frameLen = pitched ? period : juce::jmin (n, TBL);
    const int frames = juce::jlimit (1, wantFrames, n / juce::jmax (1, frameLen));
    for (int f = 0; f < frames; ++f)
    {
        const int start = frames == 1 ? 0 : (int) ((double) f / (frames - 1) * (n - frameLen - 1));
        HiFrame h ((size_t) TBL);
        for (int i = 0; i < TBL; ++i)
        {
            const double pos = start + (double) i / TBL * frameLen; const int a = (int) pos; const double t = pos - a;
            h[(size_t) i] = (float) (x[(size_t) juce::jmin (n - 1, a)] * (1 - t) + x[(size_t) juce::jmin (n - 1, a + 1)] * t);
        }
        if (! pitched)
        {
            // make the window loop: crossfade the last 1/8 with what came just before the start
            const int xf = TBL / 8;
            for (int i = 0; i < xf; ++i)
            {
                const double pre = start - xf + (double) i / TBL * frameLen;
                const float before = pre >= 0 ? x[(size_t) juce::jmin (n - 1, (int) pre)] : h[(size_t) i];
                const float t = (float) i / xf;
                h[(size_t) (TBL - xf + i)] = h[(size_t) (TBL - xf + i)] * (1 - t) + before * t;
            }
        }
        float mx = 0; for (auto v : h) mx = std::max (mx, std::abs (v));
        if (mx < 1e-5f) continue;
        normaliseHi (h);
        out.push_back (std::move (h));
    }
    return out;
}
} // namespace gpro
