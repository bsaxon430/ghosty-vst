#pragma once
// Chord-generator parity with the web version + melody-engine checks (no JUCE needed).
#include "../ArpCore.h"
#include "../Composer.h"
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace arptest
{
struct Counter { int pass = 0, fail = 0; void ok (bool c, const std::string& what) { if (c) ++pass; else { ++fail; std::printf ("FAIL %s\n", what.c_str()); } } };

inline long long R9 (double x) { return (long long) arp::jsRound (x * 1e9); }
inline std::string slotStr (const arp::Slot& s)
{
    std::ostringstream o; o << s.tok << "," << R9 (s.r) << "," << R9 (s.r2) << "," << s.alt << "," << (s.len ? s.len : 4) << "," << s.q << "," << s.sl; return o.str();
}
inline std::vector<std::string> dumpCase (const std::string& pr, uint32_t sd, double amt)
{
    using namespace arp;
    StyleArg sty; if (amt > 0) { sty.amt = amt; sty.prof = &builtinProfile(); }
    const Gen g = generate (pr, sd, sty);
    std::vector<std::string> out;
    std::ostringstream c; c << "CASE " << pr << "|" << sd << "|" << (amt == 0 ? "0" : amt == 1 ? "1" : "0.5"); out.push_back (c.str());
    std::ostringstream G;
    std::string moods; for (size_t i = 0; i < g.moods.size(); ++i) moods += (i ? "," : "") + g.moods[i];
    G << "G genre=" << g.genre << ";moods=" << moods << ";key=" << g.key << ";scale=" << g.scale << ";bars=" << g.bars << ";colour=" << R9 (g.colour) << ";style=" << g.style << ";preset=" << g.preset
      << ";rate=" << g.rate << ";bpm=" << g.bpm << ";master=" << R9 (g.master) << ";oct=" << g.oct << ";reg=" << g.reg << ";glide=" << (int) g.glide << ";pedal=" << (int) g.pedal
      << ";gate=" << R9 (g.gate) << ";swing=" << R9 (g.swing) << ";groove=" << R9 (g.groove) << ";arpSeed=" << g.arpSeed << ";progSeed=" << g.progSeed;
    out.push_back (G.str());
    std::string P = "P "; for (size_t i = 0; i < g.prog.size(); ++i) P += (i ? ";" : "") + slotStr (g.prog[i]); out.push_back (P);
    ProgState ps; ps.key = g.key; ps.scale = g.scale; ps.colour = g.colour; ps.pedal = g.pedal; ps.prog = g.prog; ps.simple = false;   // parity holds in RICH
    const auto ch = resolveProg (ps);
    std::string C = "C ";
    for (size_t i = 0; i < ch.size(); ++i)
    {
        std::string v; for (size_t k = 0; k < ch[i].voicing.size(); ++k) v += (k ? "." : "") + std::to_string (ch[i].voicing[k]);
        C += (i ? ";" : "") + ch[i].name + "," + ch[i].roman + "," + v + "," + std::to_string (ch[i].bassNote) + "," + std::to_string (ch[i].t) + "," + std::to_string (ch[i].len);
    }
    out.push_back (C);
    const auto np = newProg (g, sd + 5, sty);
    std::string N = "N "; for (size_t i = 0; i < np.size(); ++i) N += (i ? ";" : "") + slotStr (np[i]); out.push_back (N);
    out.push_back ("M " + analyzeMood (ch, g.scale, g.bpm, g.master, g.preset, g.rate).mood);
    std::string A = "A ";
    for (int a = 0; a < 8; ++a) { ProgState s2 = ps; s2.prog[0].alt = a; A += (a ? "," : "") + resolveProg (s2)[0].name; }
    out.push_back (A);
    return out;
}

inline void chordParity (Counter& C, const std::string& refPath)
{
    std::ifstream in (refPath); std::string line; std::vector<std::string> ref;
    while (std::getline (in, line)) ref.push_back (line);
    C.ok (! ref.empty(), "reference file " + refPath);
    int cases = 0, bad = 0;
    for (size_t i = 0; i < ref.size(); )
    {
        if (ref[i].rfind ("CASE ", 0) != 0) { ++i; continue; }
        const std::string head = ref[i].substr (5);
        const size_t a = head.rfind ('|'), b = head.rfind ('|', a - 1);
        const std::string pr = head.substr (0, b); const uint32_t sd = (uint32_t) std::stoul (head.substr (b + 1, a - b - 1)); const double amt = std::stod (head.substr (a + 1));
        const auto mine = dumpCase (pr, sd, amt);
        ++cases; bool same = true;
        for (size_t k = 0; k < mine.size(); ++k)
            if (i + k >= ref.size() || ref[i + k] != mine[k]) { same = false; std::printf ("  mismatch [%s|%u|%g]\n    js : %s\n    c++: %s\n", pr.c_str(), sd, amt, i + k < ref.size() ? ref[i + k].c_str() : "-", mine[k].c_str()); break; }
        if (! same) ++bad;
        i += mine.size();
    }
    std::printf ("chord parity: %d cases, %d mismatches\n", cases, bad);
    C.ok (cases >= 20 && bad == 0, "chords match the web version (" + std::to_string (cases) + " cases)");
}
} // namespace arptest
