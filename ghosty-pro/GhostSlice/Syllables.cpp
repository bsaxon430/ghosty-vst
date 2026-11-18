// GHOST SLICE phase 16: syllable detection (see Syllables.h; GHOST-SLICE-STUDY.md 3.3). Our own code, from the paper only.
#include "Syllables.h"
#include "AnalysisInternal.h"

namespace gslice::analysis
{
using namespace detail;

namespace
{
// a fine (1 ms hop, 5 ms window) level of the 300-3000 Hz band in dB: places the vowel edges inside a 10 ms frame
std::vector<double> fineBandDb (const float* x, int64 n, double sr, int& hopOut)
{
    std::vector<double> v ((size_t) n);
    for (int64 i = 0; i < n; ++i) v[(size_t) i] = x[i];
    zeroPhaseHighpass (v, 300.0, sr); zeroPhaseLowpass (v, 3000.0, sr);
    const int hop = juce::jmax (4, (int) std::lround (sr * 0.001)), win = 5 * hop;
    const int nb = (int) (n / hop) + 1;
    std::vector<double> e ((size_t) nb);
    for (int k = 0; k < nb; ++k)
    {
        const int64 c = (int64) k * hop, a = std::max<int64> (0, c - win / 2), b = std::min (n, c + win / 2);
        double s = 0; for (int64 i = a; i < b; ++i) s += v[(size_t) i] * v[(size_t) i];
        e[(size_t) k] = 10.0 * std::log10 (s / (double) juce::jmax<int64> (1, b - a) + 1e-12);
    }
    hopOut = hop;
    return e;
}
}

std::vector<Syllable> detectSyllables (const float* x, int64 n, double sr, const PitchTrack* pitch, const std::atomic<bool>* cancel)
{
    std::vector<Syllable> out;
    const int hop = (int) std::lround (sr * 0.010), win = (int) std::lround (sr * 0.030), M = nextPow2 (win);
    if (n < win * 4) return out;
    const int F = (int) (n / hop) + 1;
    const auto w = hann (win);
    FFT fft (M); Yin yin (win);
    std::vector<double> re ((size_t) M), im ((size_t) M), fr ((size_t) win), d;
    std::vector<double> Eb ((size_t) F), Ebb ((size_t) F), V ((size_t) F, 0.0), SCf ((size_t) F, 0.0), Fx ((size_t) F, 0.0);
    auto mel = [] (double f) { return 2595.0 * std::log10 (1.0 + f / 700.0); };
    auto imel = [] (double m) { return 700.0 * (std::pow (10.0, m / 2595.0) - 1.0); };
    const int nm = 40; std::vector<double> edges (nm + 2);
    for (int i = 0; i < nm + 2; ++i) edges[(size_t) i] = imel (mel (60.0) + (mel (std::min (8000.0, sr * 0.45)) - mel (60.0)) * i / (nm + 1)) / sr * M;
    std::vector<std::vector<double>> melF ((size_t) F, std::vector<double> ((size_t) nm, 0.0));
    const int k300 = (int) std::lround (300.0 / sr * M), k3k = (int) std::lround (3000.0 / sr * M);
    const int tmin = juce::jmax (2, (int) (sr / 800.0)), tmax = juce::jmin (win / 2 - 1, (int) (sr / 70.0));
    const bool usePitch = pitch != nullptr && pitch->frames() > 0 && pitch->sr == sr;
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
        if (usePitch)
        {   // the tracker's voiced probability: the best of its frames inside this 10 ms frame
            const double c = (double) f * hop / pitch->hop;
            double v = 0;
            for (int k = (int) std::floor (c - 0.5 * hop / pitch->hop); k <= (int) std::ceil (c + 0.5 * hop / pitch->hop); ++k)
                if (k >= 0 && k < pitch->frames() && pitch->f0[(size_t) k] > 0) v = std::max (v, (double) pitch->voicedProb[(size_t) k]);
            V[(size_t) f] = v;
        }
        else
        {
            yin.cmnd (fr.data(), d);
            double mn = 1; for (int t = tmin; t <= tmax; ++t) mn = std::min (mn, d[(size_t) t]);
            V[(size_t) f] = juce::jlimit (0.0, 1.0, (0.45 - mn) / 0.35);
        }
    }
    // the floor: silence = 50 dB under the peak or within 6 dB of the quietest 5 %
    std::vector<double> allBb (Ebb.begin(), Ebb.end());
    const double floorBb = std::max (peakBb - 50.0, percentile (allBb, 0.05) + 6.0);
    for (int f = 0; f < F; ++f) if (Ebb[(size_t) f] < floorBb) V[(size_t) f] = 0;
    std::vector<double> Es ((size_t) F);
    for (int f = 0; f < F; ++f) { double s = 0; int c = 0; for (int k = juce::jmax (0, f - 2); k <= juce::jmin (F - 1, f + 2); ++k) { s += Eb[(size_t) k]; ++c; } Es[(size_t) f] = s / c; }
    double fxMax = 1e-9;
    for (int f = 0; f < F; ++f)
    {
        const auto& a = melF[(size_t) juce::jmax (0, f - 3)]; const auto& b = melF[(size_t) juce::jmin (F - 1, f + 3)];
        double ab = 0, aa = 0, bb = 0; for (int k = 0; k < nm; ++k) { ab += a[(size_t) k] * b[(size_t) k]; aa += a[(size_t) k] * a[(size_t) k]; bb += b[(size_t) k] * b[(size_t) k]; }
        SCf[(size_t) f] = aa > 0 && bb > 0 ? juce::jlimit (0.0, 1.0, 1.0 - ab / std::sqrt (aa * bb)) * 10.0 : 0.0;
        if (f > 0) { double s = 0; for (int k = 0; k < nm; ++k) s += std::max (0.0, melF[(size_t) f][(size_t) k] - melF[(size_t) f - 1][(size_t) k]); Fx[(size_t) f] = s; fxMax = std::max (fxMax, s); }
    }
    for (auto& v : Fx) v /= fxMax;
    // ---- nuclei
    std::vector<double> loudE; for (int f = 0; f < F; ++f) if (Ebb[(size_t) f] >= floorBb) loudE.push_back (Es[(size_t) f]);
    if (loudE.empty()) return out;
    const double medE = median (loudE), lowE = percentile (loudE, 0.1);
    std::vector<int> nuc;
    for (int f = 1; f + 1 < F; ++f)
    {
        if (! (Es[(size_t) f] >= Es[(size_t) f - 1] && Es[(size_t) f] > Es[(size_t) f + 1])) continue;
        if ((Es[(size_t) f] < medE - 2.0 && Es[(size_t) f] < lowE + 12.0) || Ebb[(size_t) f] < floorBb) continue;
        double vmax = 0; for (int k = juce::jmax (0, f - 2); k <= juce::jmin (F - 1, f + 2); ++k) vmax = std::max (vmax, V[(size_t) k]);
        if (vmax < 0.5) continue;
        // prominence: the lowest point on each side before the level rises above this peak (+1 dB: a sustained note's ripple is
        // one plateau, not many small peaks), within 600 ms; the file edges count as silence
        double lmin = Es[(size_t) f], rmin = Es[(size_t) f];
        int k = f - 1; for (; k >= juce::jmax (0, f - 60) && Es[(size_t) k] <= Es[(size_t) f] + 1.0; --k) lmin = std::min (lmin, Es[(size_t) k]);
        if (k < 0) lmin = std::min (lmin, Es[(size_t) f] - 30.0);
        k = f + 1; for (; k <= juce::jmin (F - 1, f + 60) && Es[(size_t) k] <= Es[(size_t) f] + 1.0; ++k) rmin = std::min (rmin, Es[(size_t) k]);
        if (k > F - 1) rmin = std::min (rmin, Es[(size_t) f] - 30.0);
        if (Es[(size_t) f] - std::max (lmin, rmin) < 3.0) continue;
        nuc.push_back (f);
    }
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
    if (nuc.empty()) return out;
    if (cancel != nullptr && cancel->load()) return {};
    // ---- vowels (frame level, then the fine band envelope)
    int fh = 1; const auto fine = fineBandDb (x, n, sr, fh);
    const int NF = (int) fine.size();
    auto fineAt = [&] (int64 s) { const int k = (int) juce::jlimit<int64> (0, NF - 1, s / fh); return fine[(size_t) k]; };
    auto silentFrame = [&] (int f) { return Ebb[(size_t) f] < floorBb; };
    struct Raw { int c, v0, v1; };
    std::vector<Raw> raw;
    for (size_t i = 0; i < nuc.size(); ++i)
    {
        const int c = nuc[i];
        const int lim0 = i > 0 ? (nuc[i - 1] + c) / 2 : 0, lim1 = i + 1 < nuc.size() ? (c + nuc[i + 1]) / 2 : F - 1;
        int v0 = c, v1 = c;
        while (v0 - 1 > lim0 && V[(size_t) v0 - 1] >= 0.5 && Es[(size_t) v0 - 1] >= Es[(size_t) c] - 10.0) --v0;
        while (v1 + 1 < lim1 && V[(size_t) v1 + 1] >= 0.5 && Es[(size_t) v1 + 1] >= Es[(size_t) c] - 10.0) ++v1;
        raw.push_back ({ c, v0, v1 });
    }
    for (size_t i = 0; i < raw.size(); ++i)
    {
        const auto& R = raw[i];
        Syllable s;
        // the nucleus: the fine band peak inside the nucleus frame's +-20 ms
        {
            const int64 a = std::max<int64> (0, (int64) R.c * hop - 2 * hop), b = std::min (n, (int64) R.c * hop + 2 * hop);
            int64 best = (int64) R.c * hop; double bv = -1e9;
            for (int64 t = a; t < b; t += fh) if (fineAt (t) > bv) { bv = fineAt (t); best = t; }
            s.nucleus = best;
        }
        // the vowel edges: where the fine band level crosses (the vowel's band peak - 10 dB), searched +-15 ms around the frame edges
        double vpk = -1e9; for (int64 t = (int64) R.v0 * hop; t <= (int64) R.v1 * hop; t += fh) vpk = std::max (vpk, fineAt (t));
        const double thr = vpk - 10.0;
        {
            const int64 lo = std::max<int64> (0, (int64) R.v0 * hop - (int64) (1.5 * hop)), hi = std::min (s.nucleus, (int64) R.v0 * hop + (int64) (1.5 * hop));
            int64 t = hi; while (t - fh >= lo && fineAt (t - fh) >= thr) t -= fh;
            s.vowelStart = juce::jlimit<int64> (0, n, t);
        }
        {
            const int64 lo = std::max (s.nucleus, (int64) R.v1 * hop - (int64) (1.5 * hop)), hi = std::min (n, (int64) R.v1 * hop + (int64) (1.5 * hop));
            int64 t = lo; while (t + fh <= hi && fineAt (t + fh) >= thr) t += fh;
            s.vowelEnd = juce::jlimit<int64> (s.vowelStart + 1, n, t);
        }
        // prominence x voicing
        double vm = 0; for (int k = R.v0; k <= R.v1; ++k) vm += V[(size_t) k]; vm /= (double) (R.v1 - R.v0 + 1);
        double lmin = Es[(size_t) R.c]; for (int k = R.c; k >= juce::jmax (0, R.c - 30); --k) lmin = std::min (lmin, Es[(size_t) k]);
        s.confidence = (float) juce::jlimit (0.0, 1.0, vm * juce::jlimit (0.0, 1.0, 0.3 + (Es[(size_t) R.c] - lmin) / 25.0));
        out.push_back (s);
    }
    // ---- starts and ends
    const int64 n10 = hop;
    for (size_t i = 0; i < out.size(); ++i)
    {
        auto& s = out[i];
        const int fv0 = (int) (s.vowelStart / hop);
        const int64 prevEnd = i > 0 ? out[i - 1].vowelEnd : 0;
        // the last silent frame between the previous vowel and this one
        int sil = -1;
        for (int f = fv0; f >= 0 && (int64) f * hop >= prevEnd; --f) if (silentFrame (f)) { sil = f; break; }
        if (sil >= 0 || i == 0)
        {   // after a silence (or the file start): the attack that ends it
            const int64 a = sil >= 0 ? std::max<int64> (prevEnd, (int64) sil * hop - n10) : 0;
            int f = sil >= 0 ? sil : 0; while (f < fv0 && silentFrame (f)) ++f;   // the first sounding frame
            const int64 b = std::min (s.vowelStart + n10, (int64) f * hop + win);
            const int64 at = b > a + 3 * (int64) std::lround (sr * 0.001) ? refineOnset (x, n, sr, std::max (a, (int64) f * hop - win), b) : s.vowelStart;
            s.start = juce::jlimit (prevEnd, s.vowelStart, at);
        }
        else
        {   // continuous: a consonant between the vowels starts where the previous vowel ends; a legato vowel change starts at the
            // lowest point of the boundary cost between the nuclei
            const int64 gap = s.vowelStart - prevEnd;
            if (gap >= (int64) std::lround (sr * 0.015))
            {   // the consonant starts where the spectrum changes most between the previous vowel's fade and this vowel (a
                // fricative's noise rising, a nasal's murmur taking over), searched from 30 ms before the previous vowel's end
                const int k0 = juce::jmax (0, (int) ((prevEnd - (int64) std::lround (0.03 * sr)) / hop)), k1 = juce::jmax (k0, (int) (s.vowelStart / hop) - 2);
                int bk = -1; double bv = 0.15;
                for (int k = k0; k <= k1 && k < F; ++k) { const double v = Fx[(size_t) k] + 0.5 * SCf[(size_t) k] / 10.0; if (v > bv) { bv = v; bk = k; } }
                s.start = bk >= 0 ? juce::jlimit (out[i - 1].nucleus + 1, s.vowelStart, (int64) bk * hop - hop / 2) : prevEnd;
            }
            else
            {
                const int a = (int) (out[i - 1].nucleus / hop), b = (int) (s.nucleus / hop);
                int bnd = fv0; double best = 1e30;
                for (int k = a + 1; k < b; ++k) { const double cost = Es[(size_t) k] - 3.0 * SCf[(size_t) k] - 4.0 * Fx[(size_t) k] + 2.0 * V[(size_t) k]; if (cost < best) { best = cost; bnd = k; } }
                s.start = juce::jlimit (out[i - 1].nucleus + 1, s.vowelStart, (int64) bnd * hop);
                if (s.start > s.vowelStart) s.start = s.vowelStart;
            }
        }
    }
    for (size_t i = 0; i < out.size(); ++i)
    {
        auto& s = out[i];
        if (i + 1 < out.size() && out[i + 1].start > s.vowelEnd && ! [&] { for (int f = (int) (s.vowelEnd / hop); (int64) f * hop < out[i + 1].start; ++f) if (silentFrame (f)) return true; return false; }())
            s.end = out[i + 1].start;
        else
        {   // the sound stops: the first silent frame after the vowel (or the next start)
            const int64 lim = i + 1 < out.size() ? out[i + 1].start : n;
            int f = (int) (s.vowelEnd / hop); while ((int64) f * hop < lim && f < F && ! silentFrame (f)) ++f;
            s.end = juce::jlimit (s.vowelEnd, lim, (int64) f * hop);
        }
        if (s.end <= s.vowelEnd) s.end = std::min (n, s.vowelEnd + 1);
    }
    return out;
}

const Syllable* syllableAt (const std::vector<Syllable>& s, int64 a, int64 b)
{
    const Syllable* best = nullptr; int64 bo = 0;
    for (auto& x : s)
    {
        const int64 o = std::min (b, x.end) - std::max (a, x.start);
        if (o > bo) { bo = o; best = &x; }
    }
    return best;
}

juce::String digest (const std::vector<Syllable>& s)
{
    Hasher h; h.add ("syl").add ((int) s.size());
    for (auto& x : s) h.add (x.start).add (x.vowelStart).add (x.nucleus).add (x.vowelEnd).add (x.end).add (x.confidence);
    return h.hex();
}
} // namespace gslice::analysis
