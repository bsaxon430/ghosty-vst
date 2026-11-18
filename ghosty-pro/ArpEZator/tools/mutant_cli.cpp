// MUTANT ARP prototype: a JUCE-free command line that writes .mid files from the engine and prints the statistics
// RESEARCH.md asks for (leap / octave / chord-tone / reversal shares, density, ratchets) per mode against the targets.
//   g++ -std=c++17 -O2 -I.. mutant_cli.cpp ../Mutant.cpp ../Composer.cpp ../ArpCore.cpp ../Recipe.cpp -o mutant_cli
//   mutant_cli --out dir [--seeds N] [--bars B] [--bpm X]   (one .mid per mode + lock/mutate + evolve examples, stats on N seeds)
#include "../Mutant.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

using namespace arp;

static Harmony harmonyFor (int key, const std::string& scale, const std::vector<std::string>& toks, int barsPerChord)
{
    ProgState S; S.key = key; S.scale = scale; S.colour = 0.2; S.simple = true;
    for (auto& t : toks) { Slot s; s.tok = t; s.len = 4 * barsPerChord; S.prog.push_back (s); }
    const auto chords = resolveProg (S);
    return makeHarmony (key, scale, chords, 0);
}
static void writeMid (const std::string& path, double bpm, const std::vector<Note>& notes, int total, const Harmony& H, const std::vector<Note>* lineB = nullptr)
{
    MidiTrack arp; arp.name = "MUTANT ARP"; arp.ch = 0; arp.notes = laneMidiNotes (notes, total);
    MidiTrack b; b.name = "LINE B"; b.ch = 1; if (lineB != nullptr) b.notes = laneMidiNotes (*lineB, total);
    MidiTrack ch; ch.name = "CHORDS"; ch.ch = 2;
    for (int b = 0; b < total; b += std::max (BAR, H.loopTicks)) for (auto& c : H.chords) if (b + c.t < total) for (int p : c.voicing) ch.notes.push_back ({ b + c.t, std::min (c.tEnd - c.t, total - b - c.t), p, 0.6 });
    const auto bytes = lineB != nullptr ? writeMidi (bpm, { arp, b, ch }) : writeMidi (bpm, { arp, ch });
    std::ofstream f (path, std::ios::binary); f.write ((const char*) bytes.data(), (std::streamsize) bytes.size());
}
static void printStats (const char* label, const MutantStats& s)
{
    std::printf ("%-28s notes/bar %5.1f  leap %4.2f  oct %4.2f  chord %4.2f  inkey %4.2f  rev %4.2f  ratchet %4.2f  accent %4.2f  frag %3.0f%%  range %2d\n",
                 label, s.notesPerBar, s.leapShare, s.octShare, s.chordShare, s.scaleShare, s.reversal, s.ratchetShare, s.accentShare, s.repeatFrag * 100, s.range);
}

int main (int argc, char** argv)
{
    std::string out = "."; int seeds = 200, bars = 8, dump = 0; double bpm = 150;
    for (int i = 1; i < argc; ++i)
    {
        if (! std::strcmp (argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else if (! std::strcmp (argv[i], "--seeds") && i + 1 < argc) seeds = std::atoi (argv[++i]);
        else if (! std::strcmp (argv[i], "--bars") && i + 1 < argc) bars = std::atoi (argv[++i]);
        else if (! std::strcmp (argv[i], "--bpm") && i + 1 < argc) bpm = std::atof (argv[++i]);
        else if (! std::strcmp (argv[i], "--dump") && i + 1 < argc) dump = std::atoi (argv[++i]);
    }
    struct Key { int key; std::string scale; std::vector<std::string> prog; };
    const Key keys[] { { 1, "minor", { "i", "VI", "III", "VII" } }, { 4, "phrygian", { "i", "II", "i", "VII" } }, { 9, "harmonic", { "i", "iv", "V", "i" } }, { 7, "minor", { "i", "VII", "VI", "VII" } } };
    // targets from RESEARCH.md section 3 (at the default knobs)
    const double tgtLeap[] { 0.32, 0.30, 0.26, 0.18, 0.30, 0.12 }, tgtOct[] { 0.18, 0.10, 0.16, 0.08, 0.20, 0.04 }, tgtChord[] { 0.80, 0.62, 0.78, 0.72, 0.72, 0.70 }, tgtRev[] { 0.78, 0.65, 0.80, 0.85, 0.70, 0.90 }, tgtNotes[] { 14, 12, 13, 9, 15, 6 };
    std::printf ("MUTANT ARP prototype: %d seeds x %d modes, %d bars at %.0f BPM\n", seeds, kMutantModes, bars, bpm);
    for (int m = 0; m < kMutantModes; ++m)
    {
        MutantStats acc {}; int n = 0; int inKeyFail = 0;
        for (int s = 1; s <= seeds; ++s)
        {
            const Key& K = keys[s % 4]; const Harmony H = harmonyFor (K.key, K.scale, K.prog, 1);
            MutantParams P; P.mode = m; P.seed = (uint32_t) s; P.scaleLock = m != MutAlien; if (m == MutDream) P.glide = 0.5; if (m == MutAlien) P.glide = 0.2; if (m == MutFlute) { P.density = 0.45; P.glide = 0.25; P.retrigger = 0.15; P.octaveJump = 0.25; }
            const auto cell = mutantGenerate (H, P);
            const auto notes = mutantRealize (cell, H, P, BAR * bars);
            const auto st = mutantAnalyse (notes, H, BAR * bars);
            if (P.scaleLock && st.scaleShare < 0.999) ++inKeyFail;
            acc.notesPerBar += st.notesPerBar; acc.leapShare += st.leapShare; acc.octShare += st.octShare; acc.chordShare += st.chordShare; acc.scaleShare += st.scaleShare;
            acc.reversal += st.reversal; acc.ratchetShare += st.ratchetShare; acc.accentShare += st.accentShare; acc.repeatFrag += st.repeatFrag; acc.range += st.range; ++n;
            if (s == 1) { writeMid (out + "/mutant_" + std::string (mutantModeName (m)) + ".mid", bpm, notes, BAR * bars, H); std::printf ("%s cell: %s\n", mutantModeName (m), mutantPrint (cell).c_str()); }
            if (s <= dump) writeMid (out + "/dump_" + std::string (mutantModeName (m)) + "_" + std::to_string (s) + ".mid", bpm, notes, BAR * bars, H);   // (--dump N: the first N seeds per mode for the verbatim check)
        }
        acc.notesPerBar /= n; acc.leapShare /= n; acc.octShare /= n; acc.chordShare /= n; acc.scaleShare /= n; acc.reversal /= n; acc.ratchetShare /= n; acc.accentShare /= n; acc.repeatFrag /= n; acc.range /= n;
        printStats (mutantModeName (m), acc);
        std::printf ("%-28s target    %5.1f       %4.2f      %4.2f        %4.2f              %4.2f   (in-key failures with SCALE LOCK: %d)\n", "", tgtNotes[m], tgtLeap[m], tgtOct[m], tgtChord[m], tgtRev[m], inKeyFail);
    }
    {   // LOCK + MUTATE: lock the first 4 steps, mutate three times; the locked steps never move
        const Harmony H = harmonyFor (1, "minor", { "i", "VI", "III", "VII" }, 1);
        MutantParams P; P.mode = MutRage; P.seed = 7;
        auto cell = mutantGenerate (H, P); mutantLockFirst (cell, 4);
        std::vector<Note> all; int t0 = 0; const int seg = BAR * 4;
        auto append = [&] (const MutantCell& c) { for (auto n : mutantRealize (c, H, P, seg)) { n.t += t0; all.push_back (n); } t0 += seg; };
        append (cell);
        auto c2 = cell;
        for (int k = 1; k <= 3; ++k) { c2 = mutantMutate (c2, H, P, 1000u + (uint32_t) k, 0.5); append (c2); }
        bool lockOk = true; for (int i = 0; i < 4; ++i) { const auto& a = cell.s[(size_t) i], & b = c2.s[(size_t) i]; lockOk = lockOk && a.idx == b.idx && a.oct == b.oct && a.col == b.col && a.rest == b.rest && a.ratchet == b.ratchet; }
        std::printf ("LOCK + MUTATE: %s\n  base   %s\n  mutate %s\n", lockOk ? "locked steps identical" : "LOCK BROKEN", mutantPrint (cell).c_str(), mutantPrint (c2).c_str());
        writeMid (out + "/mutant_LOCK_MUTATE.mid", bpm, all, t0, H);
    }
    {   // EVOLVE over 16 bars: the distance to the first bar grows gradually
        const Harmony H = harmonyFor (9, "harmonic", { "i", "iv", "V", "i" }, 1);
        MutantParams P; P.mode = MutDigital; P.seed = 3; P.evolveBars = 16; P.evolve = 0.5;
        const auto cell = mutantGenerate (H, P);
        const auto notes = mutantRealize (cell, H, P, BAR * 16);
        writeMid (out + "/mutant_EVOLVE16.mid", bpm, notes, BAR * 16, H);
        auto barSet = [&] (int b) { std::vector<std::pair<int, int>> v; for (auto& n : notes) if (n.t >= b * BAR && n.t < (b + 1) * BAR) v.push_back ({ n.t - b * BAR, n.p }); return v; };
        const auto b0 = barSet (0); std::printf ("EVOLVE 16: notes changed vs bar 1:");
        for (int b = 0; b < 16; ++b) { const auto bb = barSet (b); int same = 0; for (auto& x : bb) for (auto& y : b0) if (x == y) { ++same; break; } std::printf (" %d", (int) bb.size() - same); }
        std::printf ("\n");
    }
    {   // TWO LINES: ANSWER (HYPER), INTERLOCK (RAGE), SHADOW (DIGITAL)
        const Harmony H = harmonyFor (4, "minor", { "i", "VI", "III", "VII" }, 1);
        const int kinds[] { TwoAnswer, TwoInterlock, TwoShadow }; const int modes[] { MutHyper, MutRage, MutDigital };
        for (int k = 0; k < 3; ++k)
        {
            MutantParams P; P.mode = modes[k]; P.seed = 11 + (uint32_t) k; P.twoLine = kinds[k];
            const auto a = mutantGenerate (H, P); const auto b = P.twoLine == TwoInterlock ? mutantSecondCell (a, H, P) : MutantCell {};
            std::vector<Note> A, B; mutantRealizeTwo (a, b, H, P, BAR * 8, A, B);
            const auto d = mutantDuo (A, B, BAR / 16);
            std::printf ("TWO LINES %-9s (%s): A %zu notes, B %zu notes, B onsets on an A onset %.0f%%, B median %+d semitones\n", mutantTwoLineName (P.twoLine), mutantModeName (P.mode), A.size(), B.size(), d.collide * 100, d.medianGap);
            writeMid (out + "/mutant_TWO_" + std::string (mutantTwoLineName (P.twoLine)) + ".mid", bpm, A, BAR * 8, H, &B);
        }
    }
    return 0;
}
