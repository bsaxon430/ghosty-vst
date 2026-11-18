// GHOST SLICE analysis, part 2: the monophonic pitch tracker (YIN / pYIN style), note segmentation and key estimation
// (GHOST-SLICE-STUDY.md 3.5 - 3.7). Written from the papers: de Cheveigne & Kawahara, "YIN, a fundamental frequency
// estimator for speech and music", JASA 111(4) 2002; Mauch & Dixon, "pYIN: a fundamental frequency estimator using
// probabilistic threshold distributions", ICASSP 2014; Krumhansl & Kessler, Psychological Review 89(4) 1982 (key profiles).
// No reference implementation (GPL or otherwise) was used.
#include "AnalysisInternal.h"

namespace gslice::analysis
{
using namespace detail;

juce::String PitchTrack::digest() const
{
    Hasher h; h.add (sr).add (hop).add (win).add ((int) f0.size());
    for (size_t i = 0; i < f0.size(); ++i) h.add (f0[i]).add (voicedProb[i]).add (rmsDb[i]);
    return h.hex();
}

// ---------------------------------------------------------------- the tracker
PitchTrack trackPitch (const float* xin, int64 nin, double srIn, double fmin, double fmax, const std::atomic<bool>* cancel)
{
    PitchTrack t; t.sr = srIn; t.fmin = fmin; t.fmax = fmax;
    if (nin <= 0) return t;
    // analysis rate: high rates are halved (zero-phase low-pass first), so the frame keeps ~43 ms
    int dec = srIn > 60000 ? 2 : 1;
    std::vector<double> xs ((size_t) (nin / dec + 1));
    if (dec == 1) for (int64 i = 0; i < nin; ++i) xs[(size_t) i] = xin[i];
    else
    {
        std::vector<double> lp ((size_t) nin); for (int64 i = 0; i < nin; ++i) lp[(size_t) i] = xin[i];
        zeroPhaseLowpass (lp, srIn * 0.21, srIn); zeroPhaseLowpass (lp, srIn * 0.21, srIn);
        for (int64 i = 0; i < nin; i += dec) xs[(size_t) (i / dec)] = lp[(size_t) i];
    }
    const int64 n = (int64) xs.size();
    const double sr = srIn / dec;
    const int W = sr < 30000 ? 1024 : 2048, hop = 256;
    t.hop = hop * dec; t.win = W * dec;
    fmax = std::min (fmax, sr * 0.45);
    const int tauMin = juce::jmax (2, (int) std::floor (sr / fmax)), tauMax = juce::jmin (W / 2 - 1, (int) std::ceil (sr / fmin));
    const int F = (int) (n / hop) + 1;
    const int tauMid = (int) std::lround (sr / std::sqrt (fmin * fmax));
    t.f0.assign ((size_t) F, 0.0f); t.voicedProb.assign ((size_t) F, 0.0f); t.rmsDb.assign ((size_t) F, -120.0f);
    double peakDb = -120;
    for (int f = 0; f < F; ++f)
    {
        const int64 c = (int64) f * hop; double e = 0; int cnt = 0;
        for (int64 i = std::max<int64> (0, c - W / 4); i < std::min (n, c + W / 4); ++i) { e += xs[(size_t) i] * xs[(size_t) i]; ++cnt; }
        t.rmsDb[(size_t) f] = (float) (10.0 * std::log10 (e / juce::jmax (1, cnt) + 1e-12)); peakDb = std::max (peakDb, (double) t.rmsDb[(size_t) f]);
    }
    const double gateDb = std::max (-60.0, peakDb - 50.0);

    // threshold distribution: 100 thresholds 0.01 .. 1.00 weighted by Beta(2, 18) (mean 0.1), as in the pYIN paper
    std::array<double, 100> thr {}, wt {}; double ws = 0;
    for (int i = 0; i < 100; ++i) { thr[(size_t) i] = (i + 1) / 100.0; wt[(size_t) i] = thr[(size_t) i] * std::pow (1.0 - thr[(size_t) i], 17.0); ws += wt[(size_t) i]; }
    for (auto& v : wt) v /= ws;
    constexpr double kAbsMin = 0.01;

    // pitch bins of 20 cents from fmin (the Viterbi states; the output f0 is the exact candidate nearest the chosen bin)
    constexpr double kBinCents = 20.0;
    const int nB = (int) std::floor (1200.0 * std::log2 (fmax / fmin) / kBinCents) + 1;
    auto binOf = [&] (double f) { return juce::jlimit (0, nB - 1, (int) std::lround (1200.0 * std::log2 (f / fmin) / kBinCents)); };
    struct Cand { double f0, p; int bin; };
    std::vector<std::vector<Cand>> cands ((size_t) F);

    Yin yin (W); std::vector<double> frame ((size_t) W), d;
    struct Trough { double tau, val; };
    std::vector<Trough> tr;
    for (int f = 0; f < F; ++f)
    {
        if (cancel != nullptr && (f & 63) == 0 && cancel->load()) return {};
        if (t.rmsDb[(size_t) f] < gateDb) continue;
        // YIN compares x[j] with x[j + tau] for j in the first half of the frame: start the frame so that this comparison is
        // centred on the frame time (for a typical lag), or a gliding pitch reads late
        const int64 s0 = (int64) f * hop - W / 4 - tauMid / 2;
        for (int i = 0; i < W; ++i) { const int64 s = s0 + i; frame[(size_t) i] = s >= 0 && s < n ? xs[(size_t) s] : 0.0; }
        yin.cmnd (frame.data(), d);
        tr.clear();
        for (int tau = tauMin; tau <= tauMax; ++tau)
            if (d[(size_t) tau] < d[(size_t) tau - 1] && d[(size_t) tau] <= d[(size_t) tau + 1])
            {   // parabolic interpolation of the trough (YIN step 5)
                const double a = d[(size_t) tau - 1], b = d[(size_t) tau], c = d[(size_t) tau + 1], den = a - 2 * b + c;
                const double off = std::abs (den) > 1e-12 ? juce::jlimit (-0.5, 0.5, 0.5 * (a - c) / den) : 0.0;
                tr.push_back ({ tau + off, b - 0.25 * (a - c) * off });
            }
        if (tr.empty()) continue;
        std::vector<double> prob (tr.size(), 0.0);
        size_t gmin = 0; for (size_t k = 1; k < tr.size(); ++k) if (tr[k].val < tr[gmin].val) gmin = k;
        for (int i = 0; i < 100; ++i)
        {
            size_t pick = tr.size();
            for (size_t k = 0; k < tr.size(); ++k) if (tr[k].val < thr[(size_t) i]) { pick = k; break; }
            if (pick < tr.size()) prob[pick] += wt[(size_t) i];
            else prob[gmin] += wt[(size_t) i] * kAbsMin;
        }
        // octave guard (the sub-octave trap): a trough at half the lag that is nearly as deep takes the candidate's weight
        for (size_t k = 0; k < tr.size(); ++k)
        {
            if (prob[k] <= 0) continue;
            for (size_t h = 0; h < k; ++h)
                if (std::abs (tr[h].tau * 2.0 / tr[k].tau - 1.0) < 0.04 && tr[h].val < tr[k].val + 0.05) { prob[h] += prob[k]; prob[k] = 0; break; }
        }
        double tot = 0;
        for (size_t k = 0; k < tr.size(); ++k)
            if (prob[k] > 1e-6) { const double f0 = sr / tr[k].tau; if (f0 >= fmin && f0 <= fmax) { cands[(size_t) f].push_back ({ f0, prob[k], binOf (f0) }); tot += prob[k]; } }
        t.voicedProb[(size_t) f] = (float) juce::jlimit (0.0, 1.0, tot);
    }

    // Viterbi over nB pitch bins x {voiced, unvoiced}
    const double hopSec = (double) hop / sr;
    const int S = juce::jmax (1, (int) std::ceil (35.92 * 1200.0 / kBinCents * hopSec));   // the largest pitch move per hop
    std::vector<double> logTri ((size_t) (2 * S + 1)); { double s = 0; for (int k = -S; k <= S; ++k) s += S + 1 - std::abs (k); for (int k = -S; k <= S; ++k) logTri[(size_t) (k + S)] = std::log ((S + 1 - std::abs (k)) / s); }
    constexpr double pSwitch = 0.01;
    const double lStay = std::log (1.0 - pSwitch), lSw = std::log (pSwitch), lFloor = std::log (1e-12);
    const int NS = 2 * nB;
    std::vector<double> delta ((size_t) NS), nd ((size_t) NS), candV ((size_t) nB), candU ((size_t) nB), em ((size_t) NS);
    std::vector<int16_t> back ((size_t) F * (size_t) NS);
    std::vector<uint8_t> fromV ((size_t) nB), fromU ((size_t) nB);
    auto emit = [&] (int f)
    {
        double tot = 0; std::fill (em.begin(), em.begin() + nB, 0.0);
        for (auto& c : cands[(size_t) f]) { em[(size_t) c.bin] += c.p; tot += c.p; }
        const double un = std::max (0.0, 1.0 - tot) / nB;
        for (int j = 0; j < nB; ++j) { em[(size_t) j] = em[(size_t) j] > 0 ? std::log (em[(size_t) j]) : lFloor; em[(size_t) (nB + j)] = un > 0 ? std::log (un) : lFloor; }
    };
    emit (0);
    for (int j = 0; j < NS; ++j) delta[(size_t) j] = em[(size_t) j] - std::log ((double) NS);
    for (int f = 1; f < F; ++f)
    {
        if (cancel != nullptr && (f & 255) == 0 && cancel->load()) return {};
        emit (f);
        for (int i = 0; i < nB; ++i)
        {   // into voiced: stay voiced or switch from unvoiced; into unvoiced: the mirror
            const double vv = delta[(size_t) i] + lStay, uv = delta[(size_t) (nB + i)] + lSw;
            candV[(size_t) i] = std::max (vv, uv); fromV[(size_t) i] = vv >= uv ? 0 : 1;
            const double uu = delta[(size_t) (nB + i)] + lStay, vu = delta[(size_t) i] + lSw;
            candU[(size_t) i] = std::max (uu, vu); fromU[(size_t) i] = uu >= vu ? 1 : 0;
        }
        int16_t* bp = back.data() + (size_t) f * (size_t) NS;
        for (int j = 0; j < nB; ++j)
        {
            double bv = -1e300, bu = -1e300; int iv = j, iu = j;
            for (int i = juce::jmax (0, j - S); i <= juce::jmin (nB - 1, j + S); ++i)
            {
                const double lt = logTri[(size_t) (j - i + S)];
                if (candV[(size_t) i] + lt > bv) { bv = candV[(size_t) i] + lt; iv = i; }
                if (candU[(size_t) i] + lt > bu) { bu = candU[(size_t) i] + lt; iu = i; }
            }
            nd[(size_t) j] = bv + em[(size_t) j];             bp[j] = (int16_t) (fromV[(size_t) iv] ? nB + iv : iv);
            nd[(size_t) (nB + j)] = bu + em[(size_t) (nB + j)]; bp[nB + j] = (int16_t) (fromU[(size_t) iu] ? nB + iu : iu);
        }
        const double mx = *std::max_element (nd.begin(), nd.end());
        for (int j = 0; j < NS; ++j) delta[(size_t) j] = nd[(size_t) j] - mx;   // keep the numbers in range
    }
    std::vector<int> path ((size_t) F);
    path[(size_t) F - 1] = (int) (std::max_element (delta.begin(), delta.end()) - delta.begin());
    for (int f = F - 1; f > 0; --f) path[(size_t) f - 1] = back[(size_t) f * (size_t) NS + (size_t) path[(size_t) f]];
    std::vector<double> cents ((size_t) F, 0.0);
    for (int f = 0; f < F; ++f)
    {
        const int s = path[(size_t) f];
        if (s >= nB || cands[(size_t) f].empty()) { t.f0[(size_t) f] = 0; continue; }
        const Cand* best = nullptr; int bd = 3;
        for (auto& c : cands[(size_t) f]) { const int dd = std::abs (c.bin - s); if (dd < bd || (dd == bd && best != nullptr && c.p > best->p)) { bd = dd; best = &c; } }
        const double f0 = best != nullptr ? best->f0 : fmin * std::pow (2.0, s * kBinCents / 1200.0);
        t.f0[(size_t) f] = (float) f0;
    }
    // octave post-check: a short (< 60 ms) stretch an octave off the local median of its run is folded back
    const int shortF = juce::jmax (2, (int) std::lround (0.060 / hopSec)), ctx = juce::jmax (4, (int) std::lround (0.100 / hopSec));
    for (int a = 0; a < F;)
    {
        if (t.f0[(size_t) a] <= 0) { ++a; continue; }
        int b = a; while (b < F && t.f0[(size_t) b] > 0) ++b;
        if ((b - a) * hopSec > 0.1)
        {
            for (int f = a; f < b; ++f) cents[(size_t) f] = 1200.0 * std::log2 (t.f0[(size_t) f] / 440.0);
            std::vector<int> off ((size_t) (b - a), 0);
            for (int f = a; f < b; ++f)
            {
                std::vector<double> w; for (int k = juce::jmax (a, f - ctx); k < juce::jmin (b, f + ctx + 1); ++k) w.push_back (cents[(size_t) k]);
                const double md = median (w), dlt = cents[(size_t) f] - md;
                if (std::abs (dlt - 1200) < 80) off[(size_t) (f - a)] = 1; else if (std::abs (dlt + 1200) < 80) off[(size_t) (f - a)] = -1;
            }
            for (int i = 0; i < b - a;)
            {
                if (off[(size_t) i] == 0) { ++i; continue; }
                int j = i; while (j < b - a && off[(size_t) j] == off[(size_t) i]) ++j;
                if (j - i < shortF) for (int k = i; k < j; ++k) t.f0[(size_t) (a + k)] *= off[(size_t) i] > 0 ? 0.5f : 2.0f;
                i = j;
            }
        }
        a = b;
    }
    return t;
}

// ---------------------------------------------------------------- notes (3.6) + the note's pitch decomposition (6.1)
static double weightedMedian (std::vector<std::pair<double, double>> v)
{
    if (v.empty()) return 0;
    std::sort (v.begin(), v.end()); double tot = 0; for (auto& p : v) tot += p.second;
    double acc = 0; for (auto& p : v) { acc += p.second; if (acc >= tot * 0.5) return p.first; }
    return v.back().first;
}

std::vector<Note> segmentNotes (const PitchTrack& t, const std::vector<Event>& onsets)
{
    std::vector<Note> notes;
    const int F = t.frames(); if (F < 4) return notes;
    const double fps = t.sr / t.hop;
    auto frames = [&] (double sec) { return juce::jmax (1, (int) std::lround (sec * fps)); };
    const int minNote = frames (0.060);
    // voiced runs (1 - 2 frame drop-outs bridged)
    std::vector<uint8_t> v ((size_t) F); for (int f = 0; f < F; ++f) v[(size_t) f] = t.f0[(size_t) f] > 0 ? 1 : 0;
    for (int f = 1; f + 2 < F; ++f) if (! v[(size_t) f] && v[(size_t) f - 1] && (v[(size_t) f + 1] || v[(size_t) f + 2])) v[(size_t) f] = 2;
    std::vector<std::pair<int, int>> segs;   // [a, b)
    for (int a = 0; a < F;)
    {
        if (! v[(size_t) a]) { ++a; continue; }
        int b = a; while (b < F && v[(size_t) b]) ++b;
        if (b - a < minNote) { a = b; continue; }
        // cents, median 5, low-pass 12 Hz (zero phase)
        std::vector<double> c ((size_t) (b - a));
        double lastF = 0; for (int f = a; f < b; ++f) { if (t.f0[(size_t) f] > 0) lastF = t.f0[(size_t) f]; c[(size_t) (f - a)] = 1200.0 * std::log2 (std::max (1.0, lastF > 0 ? lastF : 440.0) / 440.0) + 6900.0; }
        std::vector<double> cs (c.size());
        for (size_t i = 0; i < c.size(); ++i) { std::vector<double> w; for (size_t k = i >= 2 ? i - 2 : 0; k <= std::min (c.size() - 1, i + 2); ++k) w.push_back (c[k]); cs[i] = median (w); }
        zeroPhaseLowpass (cs, 12.0, fps);
        // split points inside the run
        std::vector<int> cuts;
        for (auto& e : onsets)
        {
            if (e.strength < 0.5f) continue;
            const int g = (int) std::lround ((double) e.pos / t.hop);
            if (g <= a + minNote || g >= b - minNote) continue;
            const double rise = t.rmsDb[(size_t) juce::jmin (F - 1, g + 4)] - t.rmsDb[(size_t) juce::jmax (0, g - 4)];
            if (rise >= 3.0) cuts.push_back (g);
        }
        // pitch-plateau changes (3.6): on a running median over ~one vibrato period (180 ms; a median keeps a note step sharp and
        // cancels vibrato) the level 40 ms before and 40 ms after differ by >= 70 cents, AND the 12 Hz curve between them is
        // steep (>= 0.8 semitone per 100 ms): a new note - not vibrato (the median cancels it), not a slow drift (not steep).
        // The cut goes where the curve crosses half way.
        const int L = b - a, hw = frames (0.090), gp = frames (0.040), lag = frames (0.020), hold = frames (0.050);
        std::vector<double> sm ((size_t) L);
        for (int i = 0; i < L; ++i) sm[(size_t) i] = median (std::vector<double> (cs.begin() + juce::jmax (0, i - hw), cs.begin() + juce::jmin (L, i + hw + 1)));
        int ns = a;
        for (int f = a + minNote; f < b - minNote;)
        {
            if (f - gp < ns + gp || f + gp >= b) { ++f; continue; }
            const double m0 = sm[(size_t) (f - gp - a)], m1 = sm[(size_t) (f + gp - a)], m2 = sm[(size_t) (juce::jmin (b - 1, f + gp + hold) - a)];
            if (std::abs (m1 - m0) >= 70.0 && (m2 - m0) * (m1 - m0) > 0 && std::abs (m2 - m0) >= 70.0)   // ... and it holds
            {
                double steep = 0;
                for (int k = f - gp; k + lag <= f + gp; ++k) steep = std::max (steep, std::abs (cs[(size_t) (k + lag - a)] - cs[(size_t) (k - a)]) * fps / lag);
                if (steep >= 800.0)
                {
                    const double mid = 0.5 * (m0 + m1), dir = m1 > m0 ? 1.0 : -1.0; int sp = f;
                    for (int k = f - gp; k <= f + gp; ++k) if ((cs[(size_t) (k - a)] - mid) * dir >= 0) { sp = k; break; }
                    cuts.push_back (sp); ns = sp; f = sp + minNote; continue;
                }
            }
            ++f;
        }
        std::sort (cuts.begin(), cuts.end());
        std::vector<std::pair<int, int>> pieces; int s0 = a;
        for (int cpos : cuts) if (cpos > s0 && cpos < b) { pieces.push_back ({ s0, cpos }); s0 = cpos; }
        pieces.push_back ({ s0, b });
        for (size_t i = 0; i < pieces.size();)   // a piece shorter than 60 ms joins its neighbour
        {
            if (pieces.size() > 1 && pieces[i].second - pieces[i].first < minNote)
            {
                if (i > 0) pieces[i - 1].second = pieces[i].second; else pieces[i + 1].first = pieces[i].first;
                pieces.erase (pieces.begin() + (std::ptrdiff_t) i);
            }
            else ++i;
        }
        for (auto& pc : pieces) segs.push_back (pc);
        a = b;
    }
    for (auto [a, b] : segs)
    {
        int voiced = 0; for (int f = a; f < b; ++f) if (t.f0[(size_t) f] > 0) ++voiced;
        if (voiced < 3) continue;
        std::vector<double> c; std::vector<double> lin;
        for (int f = a; f < b; ++f) { c.push_back (t.f0[(size_t) f] > 0 ? 1200.0 * std::log2 (t.f0[(size_t) f] / 440.0) + 6900.0 : (c.empty() ? 0.0 : c.back())); lin.push_back (std::pow (10.0, t.rmsDb[(size_t) f] / 20.0)); }
        if (c.front() == 0.0) { double first = 0; for (double x : c) if (x != 0.0) { first = x; break; } for (auto& x : c) if (x == 0.0) x = first; else break; }
        const int L = b - a, m0 = (int) std::floor (L * 0.2), m1 = juce::jmax (m0 + 1, (int) std::ceil (L * 0.8));
        Note nt;
        std::vector<std::pair<double, double>> wm; for (int i = m0; i < m1; ++i) wm.push_back ({ c[(size_t) i], lin[(size_t) i] });
        double C = weightedMedian (wm);
        nt.s0 = std::max<int64> (0, (int64) a * t.hop - t.hop / 2); nt.s1 = (int64) (b - 1) * t.hop + t.hop / 2;
        double vp = 0, ld = 0; for (int f = a; f < b; ++f) { vp += t.voicedProb[(size_t) f]; ld += lin[(size_t) (f - a)]; }
        nt.confidence = (float) juce::jlimit (0.0, 1.0, vp / L); nt.loudness = (float) (ld / L);
        // drift (< 1.5 Hz) and vibrato (6.1): a 3.5 - 9 Hz peak of the Hann-windowed spectrum of the middle 80 %, at least 2x the
        // 1-3 Hz and 10-15 Hz bands and >= 15 cents rms. With vibrato, the centre is the median after the fitted sine is removed.
        std::vector<double> dev ((size_t) L); for (int i = 0; i < L; ++i) dev[(size_t) i] = c[(size_t) i] - C;
        std::vector<double> drift = dev; zeroPhaseLowpass (drift, 1.5, fps);
        if (L * t.hop / t.sr >= 0.25)
        {
            // the core: the transitions into / out of the note (more than 60 cents off the centre at its ends) and 20 ms more go
            int i0 = 0, i1 = L; const int pad = frames (0.020);
            while (i0 < L / 3 && std::abs (dev[(size_t) i0]) > 60.0) ++i0;
            while (i1 > L - L / 3 && std::abs (dev[(size_t) i1 - 1]) > 60.0) --i1;
            i0 = juce::jmin (L / 3, i0 + pad); i1 = juce::jmax (L - L / 3, i1 - pad);
            const int M = i1 - i0;
            { std::vector<double> core (dev.begin() + i0, dev.begin() + i1); zeroPhaseLowpass (core, 1.5, fps); for (int i = 0; i < M; ++i) drift[(size_t) (i0 + i)] = core[(size_t) i]; }
            std::vector<double> w ((size_t) M); double ws = 0;
            for (int i = 0; i < M; ++i) { w[(size_t) i] = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * (i + 0.5) / M); ws += w[(size_t) i]; }
            auto dft = [&] (double hz, double& ph)
            {
                double re = 0, im = 0;
                for (int i = 0; i < M; ++i) { const double x = (dev[(size_t) (i0 + i)] - drift[(size_t) (i0 + i)]) * w[(size_t) i], a2 = 2.0 * juce::MathConstants<double>::pi * hz * (i0 + i) / fps; re += x * std::cos (a2); im -= x * std::sin (a2); }
                ph = std::atan2 (im, re); return 2.0 * std::sqrt (re * re + im * im) / ws;
            };
            double pk = 0, pf = 0, pph = 0, side = 0; int nside = 0;
            for (double hz = 1.0; hz <= 15.0 + 1e-9; hz += 0.05)
            {
                double ph = 0; const double am = dft (hz, ph);
                if (hz >= 3.5 && hz <= 9.0) { if (am > pk) { pk = am; pf = hz; pph = ph; } }
                else if (hz <= 3.0 || hz >= 10.0) { side += am; ++nside; }
            }
            side /= juce::jmax (1, nside);
            if (pk / std::sqrt (2.0) >= 15.0 && pk >= 2.0 * side)
            {   // the rate from the residual's autocorrelation (sharper than a 2-cycle spectrum), the depth at that rate
                std::vector<double> r ((size_t) M); for (int i = 0; i < M; ++i) r[(size_t) i] = dev[(size_t) (i0 + i)] - drift[(size_t) (i0 + i)];
                const int lo = juce::jmax (2, (int) std::floor (fps / 9.0)), hi = juce::jmin (M - 3, (int) std::ceil (fps / 3.5));
                std::vector<double> ac ((size_t) juce::jmax (0, hi + 2), 0.0);
                for (int lagA = lo - 1; lagA <= hi + 1 && lagA < M; ++lagA) { double sacc = 0; for (int i = 0; i + lagA < M; ++i) sacc += r[(size_t) i] * r[(size_t) (i + lagA)]; ac[(size_t) lagA] = sacc / (M - lagA); }
                int best = -1; for (int lagA = lo; lagA <= hi; ++lagA) if (ac[(size_t) lagA] > ac[(size_t) lagA - 1] && ac[(size_t) lagA] >= ac[(size_t) lagA + 1] && (best < 0 || ac[(size_t) lagA] > ac[(size_t) best])) best = lagA;
                if (best > 0)
                {
                    const double y0 = ac[(size_t) best - 1], y1 = ac[(size_t) best], y2 = ac[(size_t) best + 1], den = y0 - 2 * y1 + y2;
                    const double off = std::abs (den) > 1e-12 ? juce::jlimit (-0.5, 0.5, 0.5 * (y0 - y2) / den) : 0.0;
                    pf = fps / (best + off);
                    pk = dft (pf, pph);
                }
                nt.vibratoHz = pf; nt.vibratoCents = 2.0 * pk;
                std::vector<std::pair<double, double>> wv;
                for (int i = m0; i < m1; ++i) wv.push_back ({ c[(size_t) i] - pk * std::cos (2.0 * juce::MathConstants<double>::pi * pf * i / fps + pph), lin[(size_t) i] });
                const double C2 = weightedMedian (wv);
                for (auto& x : drift) x += C - C2;   // (the drift is relative to the centre)
                C = C2;
            }
        }
        double dr = 0; for (double x : drift) dr += x * x; nt.driftCents = std::sqrt (dr / L);
        nt.pitch = C / 100.0;
        notes.push_back (nt);
    }
    return notes;
}

// ---------------------------------------------------------------- key (3.7)
static const double kMajor[12] { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
static const double kMinor[12] { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

juce::String pitchClassName (int pc) { static const char* n[12] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; return n[((pc % 12) + 12) % 12]; }
juce::String Key::name() const { return valid() ? pitchClassName (tonic) + (minor ? " MIN" : " MAJ") : juce::String ("--"); }
juce::String Key::readout() const { return valid() ? name() + " " + juce::String (juce::roundToInt (confidence * 100.0)) + " %" : juce::String ("--"); }

Key estimateKey (const std::array<double, 12>& w)
{
    Key k;
    double tot = 0; for (double v : w) tot += v;
    if (tot <= 0) return k;
    auto pearson = [] (const double* a, const double* b)
    {
        double ma = 0, mb = 0; for (int i = 0; i < 12; ++i) { ma += a[i]; mb += b[i]; } ma /= 12; mb /= 12;
        double s = 0, sa = 0, sb = 0; for (int i = 0; i < 12; ++i) { s += (a[i] - ma) * (b[i] - mb); sa += (a[i] - ma) * (a[i] - ma); sb += (b[i] - mb) * (b[i] - mb); }
        return sa > 0 && sb > 0 ? s / std::sqrt (sa * sb) : 0.0;
    };
    for (int m = 0; m < 2; ++m)
        for (int tonic = 0; tonic < 12; ++tonic)
        {
            double tpl[12]; for (int i = 0; i < 12; ++i) tpl[(i + tonic) % 12] = m == 0 ? kMajor[i] : kMinor[i];
            k.r[(size_t) (m * 12 + tonic)] = pearson (w.data(), tpl);
        }
    int best = 0; for (int i = 1; i < 24; ++i) if (k.r[(size_t) i] > k.r[(size_t) best]) best = i;
    k.minor = best >= 12; k.tonic = best % 12;
    const int rel = k.minor ? (k.tonic + 3) % 12 : 12 + (k.tonic + 9) % 12;   // the relative major / minor
    double r2 = -1; for (int i = 0; i < 24; ++i) if (i != best && i != rel) r2 = std::max (r2, k.r[(size_t) i]);
    const double r1 = k.r[(size_t) best];
    k.confidence = juce::jlimit (0.0, 1.0, 0.5 * (r1 - r2) / 0.15 + 0.5 * (r1 - 0.5) / 0.4);
    return k;
}

Key estimateKey (const std::vector<Note>& notes)
{
    std::array<double, 12> w {};
    for (auto& n : notes)
    {
        if (n.confidence < 0.3f) continue;
        const int pc = (((int) std::lround (n.pitch)) % 12 + 12) % 12;
        w[(size_t) pc] += (double) (n.s1 - n.s0) * (double) n.loudness;
    }
    return estimateKey (w);
}

int parseKey (const juce::String& in, bool& minor)
{
    auto s = in.trim().toUpperCase().removeCharacters (" ");
    if (s.isEmpty() || s == "--") return -1;
    static const char* names[12] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    static const char* flats[12] { "C", "DB", "D", "EB", "FB", "F", "GB", "G", "AB", "A", "BB", "CB" };
    int pc = -1; int len = 0;
    for (int i = 0; i < 12; ++i)
    {
        for (auto* tbl : { names, flats })
        {
            const juce::String nm (tbl[i]);
            if (s.startsWith (nm) && nm.length() > len) { pc = i; len = nm.length(); }
        }
    }
    if (pc < 0) return -1;
    const auto rest = s.substring (len);
    minor = rest.startsWith ("MIN") || rest == "M" || rest.startsWith ("MINOR");
    if (rest.startsWith ("MAJ")) minor = false;
    if (! (rest.isEmpty() || rest.startsWith ("MAJ") || rest.startsWith ("MIN") || rest == "M")) return -1;
    return pc;
}

int keyShift (int src, bool srcMinor, int dst, bool dstMinor)
{
    if (src < 0 || dst < 0) return 0;
    int target = dst;
    if (srcMinor != dstMinor) target = srcMinor ? (dst + 9) % 12 : (dst + 3) % 12;   // onto the project key's relative of the same mode
    int s = ((target - src) % 12 + 12) % 12;
    if (s > 5) s -= 12;
    return s;
}
} // namespace gslice::analysis
