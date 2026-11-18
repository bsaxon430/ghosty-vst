#pragma once
// "Listen-proxy" statistics for generated lanes: stepwise %, leaps, repetition, chord tones on strong beats, contour.
#include "../Idea.h"
#include <cstdio>
#include <set>
#include <string>
#include <vector>

namespace arptest
{
struct LineStats
{
    int n = 0; double perBar = 0, step = 0, rep = 0, skip = 0, leap = 0, octave = 0; int bigLeaps = 0;
    double ctStrong = 0, clashes = 0; int range = 0; double dirChange = 0, desc = 0; int distinct = 0; double taste = 0;
    void add (const LineStats& o) { n += o.n; perBar += o.perBar; step += o.step; rep += o.rep; skip += o.skip; leap += o.leap; octave += o.octave; bigLeaps += o.bigLeaps; ctStrong += o.ctStrong; clashes += o.clashes; range += o.range; dirChange += o.dirChange; desc += o.desc; distinct += o.distinct; taste += o.taste; }
};
inline LineStats lineStats (const std::vector<arp::Note>& notes, const arp::Harmony& H, int total, int mood, bool pedal)
{
    LineStats S; S.n = (int) notes.size(); if (S.n < 2) return S;
    S.perBar = (double) S.n / std::max (1, total / arp::BAR);
    int st = 0, rp = 0, sk = 0, lp = 0, oc = 0, big = 0, dc = 0, down = 0, moves = 0, last = 0;
    for (size_t i = 1; i < notes.size(); ++i)
    {
        const int d = notes[i].p - notes[i - 1].p, a = std::abs (d);
        if (a == 0) ++rp; else if (a <= 2) ++st; else if (a <= 5) ++sk; else if (a <= 7) ++lp; else if (a == 12) ++oc; else ++big;
        if (d) { ++moves; if (d < 0) ++down; const int dir = d > 0 ? 1 : -1; if (last && dir != last) ++dc; last = dir; }
    }
    const double m = (double) (notes.size() - 1);
    S.step = st / m; S.rep = rp / m; S.skip = sk / m; S.leap = lp / m; S.octave = oc / m; S.bigLeaps = big;
    int strong = 0, ct = 0, cl = 0, lo = 999, hi = -999; std::set<int> ps;
    for (auto& n : notes)
    {
        if (n.t % arp::PPQ == 0) { ++strong; if (H.chordTone (n.p, n.t)) ++ct; }
        if (H.clash (n.p, n.t) && (n.t % arp::PPQ == 0 || n.d >= arp::PPQ / 2)) ++cl;
        lo = std::min (lo, n.p); hi = std::max (hi, n.p); ps.insert (n.p);
    }
    S.ctStrong = strong ? (double) ct / strong : 1; S.clashes = cl; S.range = hi - lo; S.distinct = (int) ps.size();
    S.dirChange = moves > 1 ? (double) dc / (moves - 1) : 0; S.desc = moves ? (double) down / moves : 0;
    S.taste = arp::taste (notes, H, mood, pedal, total).score;
    return S;
}
inline void printStats (const char* label, const LineStats& s, int k)
{
    const double K = std::max (1, k);
    std::printf ("  %-8s notes/bar %.1f | step %2.0f%% rep %2.0f%% skip %2.0f%% leap6-7 %2.0f%% oct %2.0f%% big>7 %.2f/idea | chord tone on beats %3.0f%% | clashes %.2f/idea | range %.1f | dir-change %2.0f%% | desc %2.0f%% | distinct %.1f | taste %.2f\n",
                 label, s.perBar / K, s.step / K * 100, s.rep / K * 100, s.skip / K * 100, s.leap / K * 100, s.octave / K * 100, s.bigLeaps / K, s.ctStrong / K * 100, s.clashes / K, s.range / K, s.dirChange / K * 100, s.desc / K * 100, s.distinct / K, s.taste / K);
}
inline std::string pitchName (int p) { static const char* N[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; return std::string (N[p % 12]) + std::to_string (p / 12 - 1); }
inline void modeReport (int mode, int ideas, bool verbose)
{
    LineStats arpS, melS;
    for (int i = 0; i < ideas; ++i)
    {
        arp::Idea I; I.mode = mode; I.seed = (uint32_t) (1 + i * 37); arp::applyModeDefaults (I); arp::newIdea (I);
        arpS.add (lineStats (I.lanes[arp::LaneArp].notes, I.H, I.total, mode, true));
        melS.add (lineStats (I.lanes[arp::LaneMelody].notes, I.H, I.total, mode, false));
        if (verbose && i < 3)
        {
            std::string ch; for (auto& c : I.chords) ch += c.name + " ";
            std::printf ("  [%s seed %u] %s %s | %s| %d bars\n", arp::modeName (mode).c_str(), I.seedUsed, arp::keyName (I.g.key, I.g.scale).c_str(), I.g.scale.c_str(), ch.c_str(), I.total / arp::BAR);
            std::string a; int cnt = 0; for (auto& n : I.lanes[arp::LaneArp].notes) { if (n.t >= 2 * arp::BAR) break; a += pitchName (n.p) + " "; ++cnt; }
            std::printf ("     arp fig len %d: %s\n", I.fig.len, a.c_str());
            std::string m; for (auto& n : I.lanes[arp::LaneMelody].notes) { if (n.t >= 4 * arp::BAR) break; m += pitchName (n.p) + "@" + std::to_string (n.t / 24) + "/" + std::to_string (n.d / 24) + " "; }
            std::printf ("     melody (16th@pos/len): %s\n", m.c_str());
        }
    }
    std::printf ("%s: %d ideas\n", arp::modeName (mode).c_str(), ideas);
    printStats ("ARP", arpS, ideas); printStats ("MELODY", melS, ideas);
}
} // namespace arptest
