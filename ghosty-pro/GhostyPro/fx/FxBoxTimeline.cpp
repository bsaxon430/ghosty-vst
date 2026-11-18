// GHOST MOTION timeline: onset analysis + rules (Ghost Box only; see FxBoxTimeline.h). Original code. Method: band-wise log-energy
// flux with an adaptive threshold and peak picking (the general approach in Bello et al., "A Tutorial on Onset Detection in Music
// Signals", IEEE TSAP 2005), on 4 one-pole bands instead of an FFT so the feed costs a few multiplies per sample. Detection runs on a
// smoothed (8 ms) band energy so a steady sound's ripple never counts; the onset time comes from a fast (0.3 ms attack) follower.
#include "FxBoxTimeline.h"
#if GPFX_BOX
#include <algorithm>

namespace gpfx {

TlAnalysis tlAnalyse (const std::vector<OnsetRec>& recs, double loopStart, double loopBeats, double samplesPerBeat, double hopSamples)
{
    TlAnalysis a; a.loopStart = loopStart;
    const double loopEnd = loopStart + loopBeats, hopBeats = hopSamples / std::max (1.0, samplesPerBeat);
    // hops are sr / 750 long at any rate: lag 6 hops = 8 ms, 40 ms = 30 hops, 30 ms = 23 hops
    constexpr int kLag = 6, kPre = 16, kGap = 30, kLevel = 23, kPeakWin = 8;
    // the records of the window (+ kPre hops before it for the flux reference and the onset search, + 30 ms after it)
    std::vector<int> idx;
    for (int i = 0; i < (int) recs.size(); ++i)
        if (recs[(size_t) i].ppq >= loopStart - (kPre + 0.5) * hopBeats && recs[(size_t) i].ppq < loopEnd + (kLevel + 0.5) * hopBeats) idx.push_back (i);
    if ((int) idx.size() < 2 * kPre) { a.why = "Play one full loop first (Ghost Box listens to the input)"; return a; }
    const int n = (int) idx.size();
    auto rec = [&] (int i) -> const OnsetRec& { return recs[(size_t) idx[(size_t) i]]; };
    std::vector<std::array<float, 4>> le ((size_t) n);
    for (int i = 0; i < n; ++i) for (int b = 0; b < 4; ++b) le[(size_t) i][(size_t) b] = 10.0f * std::log10 (std::max (rec (i).e[b], 1.0e-8f));   // (floor -80 dB)
    static const float wBand[4] { 1.0f, 1.0f, 0.8f, 0.7f };
    // detection function: the rise of each band's smoothed energy over the last 8 ms (positive parts, weighted, summed)
    std::vector<float> odf ((size_t) n, 0.0f);
    for (int i = kLag; i < n; ++i)
    {
        float total = 0; for (int b = 0; b < 4; ++b) total += rec (i).e[b];
        if (10.0f * std::log10 (total + 1.0e-12f) < -63.0f) continue;   // (below about -60 dBFS: no hit)
        float f = 0;
        for (int b = 0; b < 4; ++b) f += wBand[b] * std::max (0.0f, le[(size_t) i][(size_t) b] - le[(size_t) (i - kLag)][(size_t) b]);
        odf[(size_t) i] = f;
    }
    // adaptive threshold over the window: the largest of 6 dB, a tenth of the strongest rise, and median + 3 x the mean absolute
    // deviation of every hop's value (a steady sound's ripple stays under it; a hit rises far above it)
    float thr = 6.0f;
    {
        std::vector<float> s2 (odf.begin() + kLag, odf.end());
        if (! s2.empty())
        {
            const float mx = *std::max_element (s2.begin(), s2.end());
            std::nth_element (s2.begin(), s2.begin() + (long) s2.size() / 2, s2.end());
            const float med = s2[s2.size() / 2]; double mad = 0; for (size_t i = kLag; i < odf.size(); ++i) mad += std::abs (odf[i] - med); mad /= (double) (odf.size() - kLag);
            thr = std::max ({ 6.0f, 0.1f * mx, med + 3.0f * (float) mad });
        }
    }
    int last = -1000000;
    for (int i = kPre; i < n; ++i)
    {
        const float v = odf[(size_t) i];
        if (v < thr) continue;
        bool isMax = true;
        for (int j = std::max (0, i - kPeakWin); j <= std::min (n - 1, i + kPeakWin) && isMax; ++j) if (j != i && (odf[(size_t) j] > v || (odf[(size_t) j] == v && j < i))) isMax = false;
        if (! isMax) continue;
        // where the hit starts: in the band that rose most, the first hop whose FAST energy is 6 dB over its level just before the rise
        int bb = 0; float br = -1e9f;
        for (int b = 0; b < 4; ++b) { const float r = wBand[b] * (le[(size_t) i][(size_t) b] - le[(size_t) (i - kLag)][(size_t) b]); if (r > br) { br = r; bb = b; } }
        float base = 1.0e9f; for (int j = i - kPre; j <= i - kLag - 2; ++j) base = std::min (base, rec (j).f[bb]);
        int s = i;
        for (int j = i - kLag - 1; j <= i; ++j) if (rec (j).f[bb] > 4.0f * base + 1.0e-12f) { s = j; break; }
        if (s - last < kGap) continue;
        const double ppq = rec (s).ppq;
        if (ppq < loopStart - 0.5 * hopBeats || ppq >= loopEnd - 0.5 * hopBeats) continue;
        float pk = 0; for (int j = s; j < std::min (n, s + kLevel); ++j) pk = std::max (pk, rec (j).peak);
        TlHit h; h.beat = juce::jlimit (0.0, loopBeats, ppq - loopStart); h.levelDb = 20.0f * std::log10 (pk + 1.0e-9f); h.strength = v;
        a.hits.push_back (h); last = s;
    }
    a.ok = true;
    return a;
}

TlAnalysis tlAnalyseLastLoop (const std::vector<OnsetRec>& recs, double origin, double loopBeats, double samplesPerBeat, double hopSamples)
{
    TlAnalysis a;
    if (recs.size() < 16 || loopBeats <= 0) { a.why = "Play one full loop first (Ghost Box listens to the input)"; return a; }
    const double hopBeats = hopSamples / std::max (1.0, samplesPerBeat);
    // the newest stretch of records without a jump (song position moving forward by about one hop each)
    int s = (int) recs.size() - 1;
    while (s > 0)
    {
        const double d = recs[(size_t) s].ppq - recs[(size_t) s - 1].ppq;
        if (d < 0.25 * hopBeats || d > 4.0 * hopBeats) break;
        --s;
    }
    const double first = recs[(size_t) s].ppq, newest = recs.back().ppq;
    const double endLoop = origin + std::floor ((newest - origin) / loopBeats + 1.0e-9) * loopBeats, startLoop = endLoop - loopBeats;
    if (startLoop < first - 1.5 * hopBeats) { a.why = "Play one full loop first (" + juce::String (loopBeats / 4.0, 0) + " bar loop): Ghost Box listens to the input"; return a; }
    std::vector<OnsetRec> win (recs.begin() + s, recs.end());
    return tlAnalyse (win, startLoop, loopBeats, samplesPerBeat, hopSamples);
}

std::vector<int> tlPickHits (const std::vector<TlHit>& hits, int rule, double beatsPerBar)
{
    std::vector<int> r;
    const int n = (int) hits.size();
    if (n == 0) return r;
    switch (rule)
    {
        case trEvery2nd: for (int i = 1; i < n; i += 2) r.push_back (i); break;
        case trLastOfBar:
        {
            const double bpb = beatsPerBar > 0 ? beatsPerBar : 4.0;
            for (int i = 0; i < n; ++i)
            {
                const int bar = (int) std::floor (hits[(size_t) i].beat / bpb + 1.0e-9);
                const bool lastInBar = i == n - 1 || (int) std::floor (hits[(size_t) i + 1].beat / bpb + 1.0e-9) != bar;
                if (lastInBar) r.push_back (i);
            }
            break;
        }
        case trQuiet: case trLoud:
        {
            float lo = 1.0e9f, hi = -1.0e9f; for (auto& h : hits) { lo = std::min (lo, h.levelDb); hi = std::max (hi, h.levelDb); }
            const bool flat = hi - lo < 3.0f; const float mid = 0.5f * (lo + hi);
            for (int i = 0; i < n; ++i)
            {
                const bool loud = flat || hits[(size_t) i].levelDb >= mid;
                if ((rule == trLoud) == loud) r.push_back (i);
            }
            break;
        }
        default: for (int i = 0; i < n; ++i) r.push_back (i); break;
    }
    return r;
}

} // namespace gpfx
#endif
