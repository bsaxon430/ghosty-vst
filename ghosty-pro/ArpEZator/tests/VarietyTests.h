#pragma once
// VARIETY MEASURE: N ideas per mood from successive seeds, how different they are from each other.
//  (a) mean pairwise edit distance of the MELODY lane (tokens = pitch class relative to the key + onset in the bar
//      on the 1/16 grid + length in 1/16s; Levenshtein over the token strings, normalised by the longer one: 0 = same, 1 = nothing shared)
//  (b) unique rhythm patterns (the melody's onsets + lengths over its first 4 bars)
//  (c) unique contour shapes (up / down / same of the first 12 moves)
//  (d) chord-progression variety (unique progressions as numerals relative to the key + mean pairwise edit distance)
// Plus a determinism fingerprint of every lane (old moods must stay bit-identical).
// Included by ArpTests.cpp.
#include "../Idea.h"
#include <cstdio>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace arptest
{
// the melody's first phrase (4 bars), as two token strings: pitch classes relative to the key, and the rhythm (onset in the bar on
// the 1/16 grid + length in 1/16s). The melody distance = the mean of the two normalised edit distances.
inline std::vector<arp::Note> firstBars (const std::vector<arp::Note>& notes, int bars = 4) { std::vector<arp::Note> o; for (auto& n : notes) if (n.t < bars * arp::BAR) o.push_back (n); return o; }
inline std::vector<int> pcTokens (const std::vector<arp::Note>& notes, int key) { std::vector<int> t; for (auto& n : firstBars (notes)) t.push_back (arp::mod12 (n.p - key)); return t; }
inline std::vector<int> rhythmTokens (const std::vector<arp::Note>& notes) { std::vector<int> t; for (auto& n : firstBars (notes)) t.push_back (((n.t % arp::BAR) / 24) | (arp::clampv (n.d / 24, 0, 31) << 4)); return t; }
inline double editDistance (const std::vector<int>& a, const std::vector<int>& b)
{
    const size_t n = a.size(), m = b.size();
    if (n == 0 && m == 0) return 0.0;
    std::vector<size_t> prev (m + 1), cur (m + 1);
    for (size_t j = 0; j <= m; ++j) prev[j] = j;
    for (size_t i = 1; i <= n; ++i)
    {
        cur[0] = i;
        for (size_t j = 1; j <= m; ++j) cur[j] = std::min ({ prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] == b[j - 1] ? 0u : 1u) });
        std::swap (prev, cur);
    }
    return (double) prev[m] / (double) std::max (n, m);
}
inline double melodyDistance (const std::vector<arp::Note>& a, int keyA, const std::vector<arp::Note>& b, int keyB)
{
    return 0.5 * (editDistance (pcTokens (a, keyA), pcTokens (b, keyB)) + editDistance (rhythmTokens (a), rhythmTokens (b)));
}
// the distance used by the PUSH FURTHER rule (Idea.h) is the same measure: the tests cross-check it
inline std::string rhythmKey (const std::vector<arp::Note>& notes)
{
    std::string s; for (auto& n : notes) { if (n.t >= 4 * arp::BAR) break; s += std::to_string (n.t / 24) + ":" + std::to_string (n.d / 24) + " "; }
    return s;
}
inline std::string contourKey (const std::vector<arp::Note>& notes)
{
    std::string s; for (size_t i = 1; i < notes.size() && s.size() < 12; ++i) { const int d = notes[i].p - notes[i - 1].p; s += d > 0 ? 'U' : d < 0 ? 'D' : 'S'; }
    return s;
}
inline std::vector<int> chordTokens (const arp::Idea& I)
{
    std::vector<int> t; for (auto& c : I.H.chords) { int m = 0; for (int p : c.pcs) m |= 1 << arp::mod12 (p - I.H.key); t.push_back (m | (((c.tEnd - c.t) / arp::PPQ) << 12)); }
    return t;
}
inline std::string cellKey (const std::vector<arp::Note>& notes)
{   // the first bar's rhythm cell (onsets on the 1/16 grid)
    std::string s; for (auto& n : notes) { if (n.t >= arp::BAR) break; s += std::to_string (n.t / 24) + " "; }
    return s;
}
inline std::string openingKey (const std::vector<arp::Note>& notes)
{   // the opening shape: the first four intervals (semitones)
    std::string s; for (size_t i = 1; i < notes.size() && i <= 4; ++i) s += std::to_string (notes[i].p - notes[i - 1].p) + " ";
    return s;
}
struct VarietyRow
{
    std::string mood; int n = 0; double melDist = 0, succDist = 0, nearDup = 0; int rhythms = 0, contours = 0, cells = 0, openings = 0, progs = 0; double chordDist = 0; double notesPerBar = 0;
};
using Recent = std::vector<arp::RecentIdea>;
// make (idea, seed, the last <= 3 ideas' melodies) -> the idea for that seed (successive seeds 1..N, generated in order)
inline VarietyRow varietyOf (const std::string& name, int N, const std::function<void (arp::Idea&, uint32_t, const Recent&)>& make)
{
    VarietyRow r; r.mood = name; r.n = N;
    std::vector<std::vector<arp::Note>> mel; std::vector<int> keys; std::vector<std::vector<int>> ch; std::set<std::string> rh, co, ce, op, pr; double npb = 0;
    Recent hist;
    for (int i = 0; i < N; ++i)
    {
        Recent recent; for (int j = std::max (0, i - 3); j < i; ++j) recent.push_back (hist[(size_t) j]);
        arp::Idea I; make (I, (uint32_t) (i + 1), recent);
        const auto& m = I.lanes[arp::LaneMelody].notes;
        mel.push_back (m); keys.push_back (I.H.key); ch.push_back (chordTokens (I)); hist.push_back (arp::recentOf (I));
        rh.insert (rhythmKey (m)); co.insert (contourKey (m)); ce.insert (cellKey (m)); op.insert (openingKey (m));
        std::string p; for (int x : ch.back()) p += std::to_string (x) + " "; pr.insert (p);
        npb += (double) m.size() / std::max (1, I.total / arp::BAR);
    }
    double s = 0, sc = 0; int k = 0, dup = 0;
    for (int i = 0; i < N; ++i) for (int j = i + 1; j < N; ++j)
    {
        const double d = arptest::melodyDistance (mel[(size_t) i], keys[(size_t) i], mel[(size_t) j], keys[(size_t) j]);
        s += d; dup += d < 0.5 ? 1 : 0; sc += editDistance (ch[(size_t) i], ch[(size_t) j]); ++k;
    }
    double ss = 0; int sk = 0;
    for (int i = 1; i < N; ++i) { double mn = 9; for (int j = std::max (0, i - 3); j < i; ++j) mn = std::min (mn, arptest::melodyDistance (mel[(size_t) i], keys[(size_t) i], mel[(size_t) j], keys[(size_t) j])); ss += mn; ++sk; }
    r.melDist = k ? s / k : 0; r.chordDist = k ? sc / k : 0; r.nearDup = k ? 100.0 * dup / k : 0; r.succDist = sk ? ss / sk : 0;
    r.rhythms = (int) rh.size(); r.contours = (int) co.size(); r.cells = (int) ce.size(); r.openings = (int) op.size(); r.progs = (int) pr.size(); r.notesPerBar = npb / std::max (1, N);
    return r;
}
inline void printVarietyHeader()
{
    std::printf ("  %-10s %3s | %-8s | %-9s | %-7s | %-7s | %-8s | %-8s | %-8s | %-5s | %-10s | %s\n", "MOOD", "N", "mel dist", "vs last 3", "near-dup", "rhythms", "contours", "bar-1 cells", "openings", "progs", "chord dist", "notes/bar");
}
inline void printVarietyRow (const VarietyRow& r)
{
    std::printf ("  %-10s %3d | %8.3f | %9.3f | %6.1f%% | %7d | %8d | %11d | %8d | %5d | %10.3f | %.2f\n", r.mood.c_str(), r.n, r.melDist, r.succDist, r.nearDup, r.rhythms, r.contours, r.cells, r.openings, r.progs, r.chordDist, r.notesPerBar);
}

// FNV-1a over every lane of an idea (notes, links, chords): old moods must keep these exactly
inline uint64_t ideaFingerprint (const arp::Idea& I)
{
    uint64_t h = 1469598103934665603ull;
    auto mix = [&] (int64_t x) { for (int b = 0; b < 8; ++b) { h ^= (uint64_t) ((x >> (8 * b)) & 0xff); h *= 1099511628211ull; } };
    mix (I.total); mix (I.g.bpm); mix (I.g.key); mix ((int64_t) I.seedUsed);
    for (auto& c : I.H.chords) { mix (c.t); mix (c.tEnd); for (int v : c.voicing) mix (v); for (int p : c.pcs) mix (p); }
    for (int l = 1; l < arp::kLanes; ++l)
        for (auto& n : I.lanes[l].notes) { mix (n.t); mix (n.d); mix (n.p); mix ((int64_t) std::lround (n.v * 10000)); mix (n.l.type); mix ((int64_t) std::lround (n.l.w * 1000)); mix (n.acc); }
    mix ((int64_t) std::lround (I.groove * 1000)); mix (I.fig.len);
    return h;
}
inline uint64_t moodFingerprint (int mood, int seeds)
{
    uint64_t h = 0;
    for (int s = 1; s <= seeds; ++s)
    {
        arp::Idea I; I.mode = mood; I.seed = (uint32_t) (s * 101); arp::applyModeDefaults (I); arp::newIdea (I);
        h = h * 31 + ideaFingerprint (I);
        if (s % 5 == 0)
        {   // the other paths that must not move: EVOLVE, RICH chords, every lane mood of the old three
            arp::Idea E = I; E.evolve = true; E.evolveBars = 16; arp::rebuildChords (E); for (int l = arp::LaneArp; l < arp::kLanes; ++l) arp::composeLane (E, l, false); h = h * 31 + ideaFingerprint (E);
            arp::Idea R = I; arp::setSimple (R, false); h = h * 31 + ideaFingerprint (R);
            for (int lm = 0; lm < 3; ++lm) { arp::Idea L = I; for (int l = arp::LaneArp; l < arp::kLanes; ++l) L.laneMood[(size_t) l] = lm; for (int l = arp::LaneArp; l < arp::kLanes; ++l) arp::composeLane (L, l, true); h = h * 31 + ideaFingerprint (L); }
        }
    }
    return h;
}
} // namespace arptest
