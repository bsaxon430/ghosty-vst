#include <numeric>
#include "../CyberpopTable.h"
// MUTANT ARP tests (included by ArpTests.cpp after the render harness): every mode in key with CHORD FOLLOW, deterministic per
// seed, LOCK NOTES exact, EVOLVE gradual (measured), note-offs always sent + sample-exact timing at many tempos / buffer sizes,
// no audio-thread allocation, TWO LINES relationships, session round trip, old sessions byte-identical, the panel + screenshots.
//   ArpEZatorTests --mutant [--shots dir] [--demos dir]
namespace muttest
{
using namespace arp;
Harmony harmonyFor (int key, const std::string& scale, const std::vector<std::string>& toks, int barsPerChord)
{
    ProgState S; S.key = key; S.scale = scale; S.colour = 0.2; S.simple = true;
    for (auto& t : toks) { Slot s; s.tok = t; s.len = 4 * barsPerChord; S.prog.push_back (s); }
    return makeHarmony (key, scale, resolveProg (S), 0);
}
std::string notesPrint (const std::vector<Note>& n) { std::string o; for (auto& x : n) o += std::to_string (x.t) + ":" + std::to_string (x.p) + ":" + std::to_string (x.d) + ":" + std::to_string ((int) std::lround (x.v * 1000)) + ":" + std::to_string (x.l.type) + " "; return o; }
// a mutant idea in the processor: GENERATE the mood, switch the ARP lane to the mutant with the mode's defaults
void makeMutant (ArpProcessor& p, int mode, uint32_t seed, int mood = Midnight, std::function<void (MutantParams&)> tweak = nullptr)
{
    p.persistTaste = false;
    p.change ([&] (Idea& X) { X.mode = mood; X.seed = seed; applyModeDefaults (X); newIdea (X); for (auto& L : X.lanes) L.mute = false; }, false);
    auto P = ArpProcessor::mutantDefaults (mode, p.idea().g.bpm); P.seed = seed; if (tweak) tweak (P);
    p.mutantSetParams (P, true); p.mutantSetActive (true);
}

void engineChecks (Counter& C)
{
    struct Key { int key; std::string scale; std::vector<std::string> prog; };
    const Key keys[] { { 1, "minor", { "i", "VI", "III", "VII" } }, { 4, "phrygian", { "i", "II", "i", "VII" } }, { 9, "harmonic", { "i", "iv", "V", "i" } }, { 7, "minor", { "i", "VII", "VI", "VII" } }, { 0, "major", { "I", "V", "vi", "IV" } }, { 2, "dorian", { "i", "IV", "i", "VII" } } };
    // ---- every mode in key with CHORD FOLLOW (SCALE LOCK on): 100 % scale tones; chord tones on every accent; a note in every bar
    {
        int lines = 0, offKey = 0, emptyBars = 0, offChordAccents = 0, accents = 0;
        for (int m = 0; m < kMutantModes; ++m)
            for (uint32_t s = 1; s <= 12; ++s)
            {
                const Key& K = keys[s % 6]; const Harmony H = harmonyFor (K.key, K.scale, K.prog, 1);
                MutantParams P = ArpProcessor::mutantDefaults (m, 150); P.seed = s * 31 + (uint32_t) m; P.scaleLock = true;
                const auto cell = mutantGenerate (H, P); const auto notes = mutantRealize (cell, H, P, BAR * 8); ++lines;
                for (auto& n : notes) { if (! H.scaleTone (n.p, n.t)) ++offKey; if (n.acc && m != MutDigital) { ++accents; if (! H.chordTone (n.p, n.t)) ++offChordAccents; } }   // (DIGITAL's alternating accents are the line itself, neighbours allowed)
                for (int b = 0; b < 8; ++b) { bool any = false; for (auto& n : notes) if (n.t >= b * BAR && n.t < (b + 1) * BAR) { any = true; break; } if (! any) ++emptyBars; }
            }
        std::printf ("  MUTANT in key: %d lines x 8 bars (6 keys / scales), %d notes off key, %d empty bars, %d / %d accents off the chord\n", lines, offKey, emptyBars, offChordAccents, accents);
        C.ok (offKey == 0, "MUTANT: every mode, CHORD FOLLOW + SCALE LOCK: no note outside the key / chord");
        C.ok (emptyBars == 0, "MUTANT: every bar has notes");
        C.ok (accents > 0 && offChordAccents * 20 <= accents, "MUTANT: accents land on chord tones (< 5 % exceptions)");
    }
    // ---- ALIEN with SCALE LOCK off: every chromatic note resolves by a semitone to the next onset (an approach note), never more than 12 % of onsets
    {
        int chrom = 0, unresolved = 0, onsets = 0;
        for (uint32_t s = 1; s <= 24; ++s)
        {
            const Key& K = keys[s % 6]; const Harmony H = harmonyFor (K.key, K.scale, K.prog, 1);
            MutantParams P = ArpProcessor::mutantDefaults (MutAlien, 150); P.seed = s; P.scaleLock = false; P.intervalChaos = 0.6;
            const auto notes = mutantRealize (mutantGenerate (H, P), H, P, BAR * 8);
            std::vector<Note> on; for (auto& n : notes) if (on.empty() || on.back().t != n.t) on.push_back (n);
            for (size_t i = 0; i < on.size(); ++i)
            {
                ++onsets; if (H.scaleTone (on[i].p, on[i].t)) continue;
                ++chrom; if (i + 1 >= on.size() || std::abs (on[i + 1].p - on[i].p) != 1 || ! H.scaleTone (on[i + 1].p, on[i + 1].t)) { if (++unresolved <= 6) std::printf ("  unresolved chromatic: seed %u t %d p %d -> next t %d p %d (d %d)\n", s, on[i].t, on[i].p, i + 1 < on.size() ? on[i + 1].t : -1, i + 1 < on.size() ? on[i + 1].p : -1, on[i].d); }
            }
        }
        std::printf ("  ALIEN chromatic: %d of %d onsets (%.1f %%), %d unresolved\n", chrom, onsets, 100.0 * chrom / std::max (1, onsets), unresolved);
        C.ok (chrom > 0 && unresolved == 0, "MUTANT ALIEN (SCALE LOCK off): chromatic approach notes exist and every one resolves by a semitone");
        C.ok (chrom * 100 <= onsets * 12, "MUTANT ALIEN: chromatic notes stay the spice (<= 12 % of onsets)");
    }
    // ---- deterministic per seed (cell, notes, mutate chain, two lines); different seeds differ
    {
        int same = 0, diff = 0; const Harmony H = harmonyFor (1, "minor", { "i", "VI", "III", "VII" }, 1);
        for (int m = 0; m < kMutantModes; ++m)
            for (uint32_t s = 1; s <= 6; ++s)
            {
                MutantParams P = ArpProcessor::mutantDefaults (m, 150); P.seed = s * 101; P.evolveBars = 8; P.twoLine = (int) (s % kTwoLines);
                auto run = [&] { auto c = mutantGenerate (H, P); c = mutantMutate (c, H, P, 77, 0.4); const auto b = mutantSecondCell (c, H, P); std::vector<Note> A, B; mutantRealizeTwo (c, b, H, P, BAR * 8, A, B); return mutantPrint (c) + "|" + notesPrint (A) + "|" + notesPrint (B); };
                if (run() == run()) ++same;
                MutantParams Q = P; Q.seed = P.seed + 1; if (mutantPrint (mutantGenerate (H, P)) != mutantPrint (mutantGenerate (H, Q))) ++diff;
            }
        C.ok (same == kMutantModes * 6, "MUTANT: the same seed gives the same cell, mutations, notes and second line (every mode)");
        C.ok (diff >= kMutantModes * 6 - 2, "MUTANT: a different seed gives a different cell");
    }
    // ---- LOCK NOTES: locked steps survive GENERATE (new seeds), MUTATE x 10, OCTAVE / RHYTHM CHAOS, EVOLVE; the realised notes of those steps are identical too
    {
        int broken = 0, cases = 0; const Harmony H = harmonyFor (9, "harmonic", { "i", "iv", "V", "i" }, 1);
        auto stepEq = [] (const MutantStep& a, const MutantStep& b) { return a.idx == b.idx && a.oct == b.oct && a.col == b.col && a.rest == b.rest && a.hold == b.hold && a.ratchet == b.ratchet && a.acc == b.acc && std::abs (a.gate - b.gate) < 1e-9 && a.lock && b.lock; };
        for (int m = 0; m < kMutantModes; ++m)
            for (uint32_t s = 1; s <= 4; ++s)
            {
                MutantParams P = ArpProcessor::mutantDefaults (m, 150); P.seed = s * 7;
                auto base = mutantGenerate (H, P); mutantLockFirst (base, 4);
                if (base.len > 6) base.s[(size_t) base.len - 1].lock = true;   // and a chosen one at the end
                std::vector<int> locked; for (int i = 0; i < base.len; ++i) if (base.s[(size_t) i].lock) locked.push_back (i);
                auto check = [&] (const MutantCell& c, const char* what) { ++cases; for (int i : locked) if (c.len != base.len || ! stepEq (c.s[(size_t) i], base.s[(size_t) i])) { ++broken; std::printf ("  LOCK broken by %s (mode %d seed %u step %d)\n", what, m, s, i); break; } };
                MutantParams Q = P; Q.seed = P.seed + 1000; check (mutantGenerate (H, Q, &base), "GENERATE");
                auto c = base; for (int k = 1; k <= 10; ++k) { c = mutantMutate (c, H, P, (uint32_t) k, 0.6); check (c, "MUTATE"); }
                check (mutantMutate (base, H, P, 5, 1.0, 1), "OCTAVE CHAOS"); check (mutantMutate (base, H, P, 6, 1.0, 2), "RHYTHM CHAOS");
                // the realised locked steps: the same pitch at the same place in every bar of an EVOLVE 16 take (the chord under them may differ: compare per chord-equal bars)
                MutantParams E = P; E.evolveBars = 16; E.evolve = 1.0; const auto notes = mutantRealize (base, H, E, BAR * 16);
                const int st = mutantStepTicks (P.div);
                for (int b = 4; b < 16; b += 4)   // bars with the same chord as bar 0
                    for (int i : locked)
                    {
                        const auto& stp = base.s[(size_t) i]; if (stp.rest || i >= 16) continue;
                        auto at = [&] (int bar) { int p = -1; for (auto& n : notes) if (n.t == bar * BAR + i * st) { p = n.p; break; } return p; };
                        ++cases; if (at (0) != at (b)) { ++broken; std::printf ("  LOCK realised step %d differs in bar %d (mode %d seed %u)\n", i, b, m, s); }
                    }
            }
        std::printf ("  LOCK NOTES: %d checks, %d broken\n", cases, broken);
        C.ok (broken == 0, "MUTANT LOCK NOTES: locked steps are kept exactly through GENERATE, MUTATE x 10, OCTAVE / RHYTHM CHAOS and EVOLVE");
    }
    // ---- EVOLVE changes gradually: one chord all the way, EVOLVE 16 with VARY 2: the base bars (0 2 4 ...) move away from bar 0 step by step
    {
        const Harmony H = harmonyFor (1, "minor", { "i" }, 1);
        int ok = 0, runs = 0; double firstStep = 0, lastStep = 0;
        for (int m = 0; m < kMutantModes; ++m)
            for (uint32_t s = 1; s <= 5; ++s)
            {
                MutantParams P = ArpProcessor::mutantDefaults (m, 150); P.seed = s * 13; P.evolveBars = 16; P.variationBars = 2; P.evolve = 0.5; P.stepSkip = 0;
                const auto notes = mutantRealize (mutantGenerate (H, P), H, P, BAR * 16);
                auto bar = [&] (int b) { std::set<std::pair<int, int>> v; for (auto& n : notes) if (n.t >= b * BAR && n.t < (b + 1) * BAR) v.insert ({ n.t - b * BAR, n.p }); return v; };
                const auto b0 = bar (0); std::vector<double> dist;
                for (int b = 2; b < 16; b += 2) { const auto bb = bar (b); size_t same = 0; for (auto& x : bb) same += b0.count (x); dist.push_back (1.0 - (double) same / std::max<size_t> (1, std::max (bb.size(), b0.size()))); }
                // gradual: the first evolved bar differs a little, the last differs more than the first, no single jump > 0.5, and bar 0 of the second cycle (bar 16 would wrap) is not needed
                bool g = dist.front() <= 0.45 && dist.back() >= dist.front() && dist.back() <= 0.95; for (size_t i = 1; i < dist.size(); ++i) if (std::abs (dist[i] - dist[i - 1]) > 0.6) g = false;
                ++runs; if (g) ++ok; firstStep += dist.front(); lastStep += dist.back();
            }
        std::printf ("  EVOLVE 16: %d / %d runs gradual; mean distance after one step %.2f, after seven %.2f\n", ok, runs, firstStep / runs, lastStep / runs);
        C.ok (ok >= runs - 4, "MUTANT EVOLVE: the cell drifts gradually over 16 bars (small first step, larger later, no jumps)");
        C.ok (lastStep / runs > firstStep / runs, "MUTANT EVOLVE: the change accumulates");
    }
    // ---- TWO LINES: ANSWER never overlaps in time, INTERLOCK never lands on a first-line onset, SHADOW is the first line late by N steps an octave away
    {
        const Harmony H = harmonyFor (4, "minor", { "i", "VI", "III", "VII" }, 1); int bad = 0, cases = 0;
        for (int m = 0; m < kMutantModes; ++m)
            for (uint32_t s = 1; s <= 4; ++s)
            {
                for (int kind = 1; kind < kTwoLines; ++kind)
                {
                    MutantParams P = ArpProcessor::mutantDefaults (m, 150); P.seed = s * 3 + (uint32_t) kind; P.twoLine = kind;
                    const auto a = mutantGenerate (H, P); const auto b = kind == TwoInterlock ? mutantSecondCell (a, H, P) : MutantCell {};
                    std::vector<Note> A, B; mutantRealizeTwo (a, b, H, P, BAR * 8, A, B); ++cases;
                    if (B.empty()) { ++bad; std::printf ("  TWO LINES %s empty (mode %d)\n", mutantTwoLineName (kind), m); continue; }
                    const auto d = mutantDuo (A, B, mutantStepTicks (P.div));
                    if (kind == TwoAnswer) { const int V = mutantVariationBars (P), half = V * BAR / 2; bool ok = true; for (auto& n : A) if (n.t % (V * BAR) >= half) ok = false; for (auto& n : B) if (n.t % (V * BAR) < half) ok = false; if (! ok) { ++bad; std::printf ("  ANSWER overlaps (mode %d)\n", m); } }
                    if (kind == TwoInterlock && d.collide > 0.0001) { ++bad; std::printf ("  INTERLOCK collides %.2f (mode %d)\n", d.collide, m); }
                    if (kind == TwoShadow)
                    {
                        const int late = P.shadowSteps * mutantStepTicks (P.div); size_t hit = 0;
                        for (auto& n : B) for (auto& x : A) if (x.t + late == n.t && std::abs (x.p - n.p) == 12) { ++hit; break; }
                        if (hit < B.size()) { ++bad; std::printf ("  SHADOW not a canon (mode %d)\n", m); }
                    }
                    if (kind != TwoAnswer && std::abs (d.medianGap) < 3) { ++bad; std::printf ("  TWO LINES %s sits on top of line A (gap %d, mode %d)\n", mutantTwoLineName (kind), d.medianGap, m); }
                }
            }
        std::printf ("  TWO LINES: %d cases, %d bad\n", cases, bad);
        C.ok (bad == 0, "MUTANT TWO LINES: ANSWER = call then response, INTERLOCK = no shared onsets, SHADOW = an exact canon an octave away; the second line sits in its own register");
    }
    // ---- the numbers of the research table are in reach (RESEARCH.md section 3): leap share well above a normal melody's, octaves present, chord tones carry it
    {
        for (int m = 0; m < kMutantModes; ++m)
        {
            if (m == MutCyberpop) continue;   // (judged against its learned table below, not the hand-written style targets)
            double leap = 0, oct = 0, chord = 0, rev = 0, frag = 0; int n = 0;
            for (uint32_t s = 1; s <= 30; ++s)
            {
                const Key& K = keys[s % 6]; const Harmony H = harmonyFor (K.key, K.scale, K.prog, 1);
                MutantParams P = ArpProcessor::mutantDefaults (m, 150); P.seed = s; P.twoLine = 0;
                const auto st = mutantAnalyse (mutantRealize (mutantGenerate (H, P), H, P, BAR * 8), H, BAR * 8);
                leap += st.leapShare; oct += st.octShare; chord += st.chordShare; rev += st.reversal; frag += st.repeatFrag; ++n;
            }
            leap /= n; oct /= n; chord /= n; rev /= n; frag /= n;
            std::printf ("  %-8s leap %.2f oct %.2f chord %.2f reversal %.2f fragment-repeat %.0f%%\n", mutantModeName (m), leap, oct, chord, rev, frag * 100);
            const bool flute = m == MutFlute, dream = m == MutDream;
            C.ok ((flute || dream ? leap >= 0.10 : leap >= 0.22) && leap <= 0.5, std::string ("MUTANT ") + mutantModeName (m) + ": leap share in the style's range");
            C.ok (flute ? true : oct >= (dream ? 0.015 : 0.03), std::string ("MUTANT ") + mutantModeName (m) + ": octave displacement is there");
            C.ok (chord >= 0.6 && chord <= 0.97, std::string ("MUTANT ") + mutantModeName (m) + ": chord tones carry the line, with passing notes");
            C.ok (rev >= 0.6, std::string ("MUTANT ") + mutantModeName (m) + ": leaps turn back (post-skip reversal)");
            C.ok (frag >= (flute ? 0.4 : 0.6), std::string ("MUTANT ") + mutantModeName (m) + ": a fragment repeats inside the first 2 bars (recognisable)");
        }
    }
    // ---- FLUTE habits: breaths (a rest of a beat or more every 1-2 bars), short phrases, repeated stabs, ornaments present
    {
        int breaths = 0, bars = 0, stabs = 0, onsets = 0, graces = 0, trills = 0;
        for (uint32_t s = 1; s <= 20; ++s)
        {
            const Key& K = keys[s % 6]; const Harmony H = harmonyFor (K.key, K.scale, K.prog, 1);
            MutantParams P = ArpProcessor::mutantDefaults (MutFlute, 150); P.seed = s * 5;
            const auto c = mutantGenerate (H, P); for (auto& st : c.s) { graces += st.grace; trills += st.trill; }
            const auto notes = mutantRealize (c, H, P, BAR * 8); bars += 8;
            std::vector<Note> on; for (auto& n : notes) if (on.empty() || on.back().t != n.t) on.push_back (n);
            for (size_t i = 1; i < on.size(); ++i) { ++onsets; if (on[i].t - (on[i - 1].t + on[i - 1].d) >= PPQ) ++breaths; if (on[i].p == on[i - 1].p && on[i].t - on[i - 1].t >= PPQ / 4) ++stabs; }
        }
        std::printf ("  FLUTE: %.2f breaths / bar, %.0f%% repeated stabs, %d grace notes, %d trills over 20 cells\n", (double) breaths / bars, 100.0 * stabs / std::max (1, onsets), graces, trills);
        C.ok (breaths >= bars / 2, "MUTANT FLUTE: it breathes (a rest of a beat or more in most bars)");
        C.ok (stabs * 100 >= onsets * 8 && graces > 0 && trills > 0, "MUTANT FLUTE: repeated stabs, grace notes and trills are part of the lines");
    }
    // ---- FLUTE is hidden since 3.2.1 (CYBERPOP took its button): a session saved with it generates and renders exactly as 3.2.0 did
    {
        uint64_t h = 1469598103934665603ull; auto mix = [&] (int64_t x) { for (int b = 0; b < 8; ++b) { h ^= (uint64_t) ((x >> (8 * b)) & 0xff); h *= 1099511628211ull; } };
        for (int k = 0; k < 6; ++k)
            for (uint32_t s = 1; s <= 8; ++s)
            {
                struct { Harmony H; } I { harmonyFor (keys[k].key, keys[k].scale, keys[k].prog, 1 + (int) (s % 2)) };
                MutantParams P; P.mode = MutFlute; P.seed = s * 5; P.density = 0.45; P.octaveJump = 0.25; P.intervalChaos = 0.25; P.retrigger = 0.15; P.gate = 0.7; P.velRandom = 0.35; P.glide = 0.25; P.swing = 0.06; P.scaleLock = true; P.lineBReg = -1;
                if (s % 3 == 0) P.twoLine = TwoAnswer; if (s % 4 == 0) { P.evolveBars = 8; P.density = 0.7; P.retrigger = 0.5; }
                auto c = mutantGenerate (I.H, P); for (char ch : mutantPrint (c)) mix (ch);
                auto m = mutantMutate (c, I.H, P, 99u + s, 0.5, 0); for (char ch : mutantPrint (m)) mix (ch);
                std::vector<Note> A, B; mutantRealizeTwo (c, mutantSecondCell (c, I.H, P), I.H, P, BAR * 8, A, B);
                for (auto& n : A) { mix (n.t); mix (n.d); mix (n.p); mix ((int64_t) std::lround (n.v * 10000)); mix (n.l.type); }
                for (auto& n : B) { mix (n.t); mix (n.d); mix (n.p); mix ((int64_t) std::lround (n.v * 10000)); }
            }
        std::printf ("  FLUTE (hidden) fingerprint %016llx (3.2.0: 087139943b691549)\n", (unsigned long long) h);
        C.ok (h == 0x087139943b691549ull, "MUTANT FLUTE (hidden): older sessions' cells, mutations and notes are bit-identical with 3.2.0");
        C.ok (! mutantModeShown (MutFlute) && mutantButtonOfMode (MutFlute) < 0 && mutantModeOfButton (5) == MutCyberpop && mutantButtonOfMode (MutCyberpop) == 5 && kMutantButtons == 6, "MUTANT: the panel shows six modes, CYBERPOP on FLUTE's button; FLUTE has no button");
        ArpProcessor::mutantDefaults (MutFlute, 150);   // (still answers for older sessions)
    }
    // ---- CYBERPOP: the learned table's numbers come out (density, leaps, register, the A A' repeat, longer notes), always in key
    {
        const CyberTable& T = cyberTable();
        double perBar = 0, leap = 0, scale = 0, frag = 0, chordShare = 0; int n = 0, longNotes = 0, wide = 0;
        for (uint32_t s = 1; s <= 40; ++s)
        {
            const Key& K = keys[s % 6]; const Harmony H = harmonyFor (K.key, K.scale, K.prog, 1);
            MutantParams P = ArpProcessor::mutantDefaults (MutCyberpop, 150); P.seed = s * 3; P.twoLine = 0;
            const auto c = mutantGenerate (H, P); for (auto& st : c.s) if (! st.rest && st.sus >= 4) ++longNotes;
            const auto st = mutantAnalyse (mutantRealize (c, H, P, BAR * 8), H, BAR * 8);
            perBar += st.notesPerBar; leap += st.leapShare; scale += st.scaleShare; frag += st.repeatFrag; chordShare += st.chordShare; if (st.range > T.range + 12) ++wide; ++n;
        }
        perBar /= n; leap /= n; scale /= n; frag /= n; chordShare /= n;
        std::printf ("  CYBERPOP (table: %s): %.1f notes / bar (table %.1f), leap %.2f (%.2f), in key %.3f, fragment repeat %.0f%% (bar repeats in the packs %.0f%%), chord tones %.2f, %d notes >= a 1/4 over %d cells, %d cells wider than the table's range + an octave\n", T.source, perBar, T.perBar, leap, T.leap, scale, frag * 100, T.barRepeat * 100, chordShare, longNotes, n, wide);
        C.ok (perBar >= 0.6 * T.perBar && perBar <= 1.4 * T.perBar, "MUTANT CYBERPOP: the table's density comes out (within 40 %)");
        C.ok (std::abs (leap - T.leap) <= 0.15, "MUTANT CYBERPOP: the table's leap share comes out (within 0.15)");
        C.ok (scale >= 0.999, "MUTANT CYBERPOP: every note in the key (SCALE LOCK on)");
        C.ok (frag >= 0.6 * T.barRepeat && longNotes > 0 && wide <= n / 5, "MUTANT CYBERPOP: fragments repeat inside 2 bars as often as the packs repeat bars, the longer notes the packs hold are there, the register stays the packs' range");
        C.ok (chordShare >= 0.45, "MUTANT CYBERPOP: chord tones carry the line (CHORD FOLLOW nudges the beats onto the chord)");
        C.ok (T.files >= 0 && T.source != nullptr && T.source[0] != 0 && std::abs (std::accumulate (T.interval, T.interval + 25, 0.0) - 1.0) < 0.02, "MUTANT CYBERPOP: the table is well-formed (its interval histogram sums to 1, its source is named)");
    }
}

void processorChecks (Counter& C)
{
    // ---- note-offs always sent + sample-exact timing at many tempos and buffer sizes (variable blocks too), every mode, two lines on
    {
        const double sr = 48000; int cases = 0, unbalanced = 0, late = 0, onsCount = 0; double worst = 0;
        const double tempos[] { 70, 124, 140, 150, 156, 172 }; const int blocks[] { 64, 256, 1024 };
        for (int m = 0; m < kMutantModes; ++m)
        {
            ArpProcessor p; makeMutant (p, m, 31 + (uint32_t) m, Midnight, [m] (MutantParams& P) { P.twoLine = m % kTwoLines; P.evolveBars = 8; });
            setP (p, "midiOut", 1.0f); setP (p, "midiCh1", 0.0f);   // (lanes on their own channels: ARP = 1)
            for (double bpm : tempos)
                for (int blk : blocks)
                {
                    std::vector<MidiEv> ev; int byp = 0; RenderOpts o; o.sr = sr; o.bpm = bpm; o.bars = 4; o.block = blk; o.varBlocks = blk == 256;
                    render (p, o, ev, byp); ++cases;
                    std::string why; if (! balanced (ev, why))
                    {
                        ++unbalanced; std::printf ("  unbalanced (mode %d %.0f bpm blk %d): %s\n", m, bpm, blk, why.c_str());
                        if (unbalanced <= 2) { const double spt = 60.0 / bpm * sr / PPQ; const double at = std::stod (why.substr (why.find ("at ") + 3)) / spt; for (auto& e : ev) if (e.at > 0 && std::abs (e.at / spt - at) < 30) std::printf ("    ev %s ch %d p %d tick %.1f\n", e.on ? "ON " : "off", e.ch, e.pitch, e.at / spt);
                                              for (auto& n : p.idea().lanes[LaneMelody].notes) if (std::abs (n.t - at) < 80) std::printf ("    melody lane note t %d d %d p %d link %d\n", n.t, n.d, n.p, n.l.type); }
                    }
                    // every ARP (ch 1) note-on in the first pass sits exactly where its lane note's tick says (within one sample of rounding)
                    const double samplesPerTick = 60.0 / bpm * sr / PPQ; const auto& ns = p.idea().lanes[LaneArp].notes;
                    for (auto& e : ev)
                    {
                        if (! e.on || e.ch != 1 || e.at >= 4 * BAR * samplesPerTick) continue;
                        ++onsCount;
                        double best = 1e9; for (auto& n : ns) best = std::min (best, std::abs (n.t * samplesPerTick - (double) e.at));
                        worst = std::max (worst, best); if (best > 1.5) { if (++late <= 6) std::printf ("  off-tick: mode %d %.0f bpm blk %d: note-on ch1 p%d at sample %lld = tick %.2f, nearest lane note %.2f ticks away\n", m, bpm, blk, e.pitch, (long long) e.at, e.at / samplesPerTick, best / samplesPerTick); }
                    }
                }
        }
        std::printf ("  render: %d cases (6 tempos x 3 buffer sizes x %d modes), %d unbalanced, %d of %d ARP note-ons off their tick, worst %.2f samples\n", cases, kMutantModes, unbalanced, late, onsCount, worst);
        C.ok (unbalanced == 0, "MUTANT render: every note-on gets its note-off, nothing left hanging (every mode, 6 tempos, 3 buffer sizes, variable blocks)");
        C.ok (late == 0 && onsCount > 0, "MUTANT render: ARP note-ons are sample-exact at every tempo / buffer size");
    }
    // ---- no audio-thread allocation while a mutant two-line idea plays (and while the idea changes underneath)
    {
        ArpProcessor p; makeMutant (p, MutHyper, 9, Midnight, [] (MutantParams& P) { P.twoLine = TwoAnswer; });
        FakeHead head; head.bpm = 150; p.setPlayHead (&head); p.setRateAndBufferSizeDetails (48000, 256); p.prepareToPlay (48000, 256);
        gAudioAllocs = 0; juce::int64 pos = 0; const double spb = 4 * 60.0 / 150 * 48000;
        for (int k = 0; k < 400; ++k)
        {
            if (k == 120) p.mutantMutate (0, 0.5);
            if (k == 240) p.mutantGenerate (true);
            head.ppq = pos / 48000.0 * 150 / 60.0; head.samples = pos; head.playing = true;
            juce::AudioBuffer<float> b (2, 256); b.clear(); juce::MidiBuffer mb; mb.ensureSize (4096);
            gAudioThread = true; p.processBlock (b, mb); gAudioThread = false; pos += 256;
        }
        p.setPlayHead (nullptr); (void) spb;
        std::printf ("  audio-thread allocations over 400 blocks (MUTATE + GENERATE underneath): %ld\n", gAudioAllocs.load());
        C.ok (gAudioAllocs.load() == 0, "MUTANT: no allocation on the audio thread");
    }
    // ---- session round trip (byte-identical), old sessions without the key load as before, a fresh idea writes no 'mutant' key
    {
        ArpProcessor p; makeMutant (p, MutRage, 44, Trap, [] (MutantParams& P) { P.twoLine = TwoInterlock; P.evolveBars = 8; });
        p.mutantLockFirst (4); p.mutantMutate (0, 0.4);
        const auto s1 = p.sessionString();
        ArpProcessor q; q.persistTaste = false; q.restoreSession (s1); const auto s2 = q.sessionString();
        C.ok (s1 == s2 && q.idea().mutant.active && mutantLockedCount (q.idea().mutant.cell) == 4 && q.idea().mutant.generation == 1, "MUTANT: the session round-trips byte-identically (cell, locks, params, generation, lane notes)");
        ArpProcessor r; r.persistTaste = false; r.generate (Midnight, false);
        const auto sv = juce::JSON::parse (r.sessionString());
        C.ok (sv.isObject() && ! sv["idea"].hasProperty ("mutant"), "MUTANT: an idea that never used it writes no 'mutant' key (older sessions stay byte-identical)");
        // an older session string (no mutant key) loads with the classic arp lane untouched
        auto old = r.sessionString(); ArpProcessor t; t.persistTaste = false; t.restoreSession (old);
        C.ok (! t.idea().mutant.active && t.sessionString() == old, "MUTANT: a session without the key loads exactly as before (mutant inactive)");
        ArpProcessor u; makeMutant (u, MutCyberpop, 21, Midnight); bool anySus = false; for (auto& st : u.idea().mutant.cell.s) anySus = anySus || st.sus > 1;
        ArpProcessor v; v.persistTaste = false; v.restoreSession (u.sessionString());
        C.ok (anySus && mutantPrint (v.idea().mutant.cell) == mutantPrint (u.idea().mutant.cell) && v.idea().mutant.P.mode == MutCyberpop && notesPrint (v.idea().lanes[LaneArp].notes) == notesPrint (u.idea().lanes[LaneArp].notes), "MUTANT CYBERPOP: a cell with longer notes (sus) round-trips through the session");
        // a 3.2.0 session saved with FLUTE: loads as FLUTE, its lane the same notes, the panel lights no mode button
        ArpProcessor w; makeMutant (w, MutFlute, 4, Trap); const auto wNotes = notesPrint (w.idea().lanes[LaneArp].notes); auto ws = w.sessionString();
        ArpProcessor x; x.persistTaste = false; x.restoreSession (ws);
        C.ok (x.idea().mutant.P.mode == MutFlute && notesPrint (x.idea().lanes[LaneArp].notes) == wNotes && ! wNotes.empty(), "MUTANT FLUTE (hidden): a session saved with FLUTE loads as FLUTE with the same notes");
    }
    // ---- CHORD FOLLOW in the processor: swap a chord -> the lane is realised again over the new chords (same cell), still in key
    {
        ArpProcessor p; makeMutant (p, MutDigital, 12);
        const auto cell0 = mutantPrint (p.idea().mutant.cell); const auto before = p.idea().lanes[LaneArp].notes;
        p.change ([] (Idea& X) { nextAlt (X, 0); }, true, "chord");
        const auto& I = p.idea(); int off = 0; for (auto& n : I.lanes[LaneArp].notes) if (! I.H.scaleTone (n.p, n.t)) ++off;
        C.ok (mutantPrint (I.mutant.cell) == cell0 && off == 0 && notesPrint (I.lanes[LaneArp].notes) != notesPrint (before), "MUTANT CHORD FOLLOW: a chord change re-pitches the lane from the same cell, still in key");
        // CLASSIC ARP hands the lane back: the figure engine's notes return; UNDO brings the mutant back
        p.mutantSetActive (false); const bool classic = ! p.idea().mutant.active;
        p.undo(); C.ok (classic && p.idea().mutant.active, "MUTANT: CLASSIC ARP turns it off, UNDO brings it back");
    }
    // ---- old moods untouched: a plain GENERATE in every mood never activates the mutant and its ARP lane equals the pure engine's
    {
        int same = 0;
        for (int mood = 0; mood < kMoods; ++mood)
        {
            ArpProcessor p; p.persistTaste = false; p.change ([&] (Idea& X) { X.mode = mood; X.seed = 500 + (uint32_t) mood; applyModeDefaults (X); newIdea (X); }, false);
            Idea Y; Y.mode = mood; Y.seed = 500 + (uint32_t) mood; applyModeDefaults (Y); newIdea (Y);
            if (! p.idea().mutant.active && notesPrint (p.idea().lanes[LaneArp].notes) == notesPrint (Y.lanes[LaneArp].notes)) ++same;
        }
        C.ok (same == kMoods, "OLD STYLES: every mood's GENERATE is untouched by the mutant engine (bit-identical ARP lane, mutant off)");
    }
}

void uiChecks (Counter& C, const juce::File& shotDir)
{
    ArpProcessor p; makeMutant (p, MutRage, 21, Trap, [] (MutantParams& P) { P.twoLine = TwoAnswer; });
    p.hintsSeen = true;
    std::unique_ptr<ArpEditor> ed (dynamic_cast<ArpEditor*> (p.createEditor())); ed->setSize (1240, 820); ed->refresh();
    ed->openMutant (true); ed->refresh();
    auto& mp = *ed->mutantPanel;
    C.ok (mp.isVisible() && mp.modeSeg.sel == MutRage && mp.twoSeg.sel == TwoAnswer, "MUTANT panel: opens on the mutant lane with the idea's mode and TWO LINES setting");
    const auto cell0 = mutantPrint (p.idea().mutant.cell);
    mp.genB.onClick(); const auto cell1 = mutantPrint (p.idea().mutant.cell);
    mp.lockFirstB.onClick(); mp.mutB.onClick(); mp.octB.onClick(); mp.rhyB.onClick();
    const auto& c = p.idea().mutant.cell; bool locked4 = c.len >= 4; for (int i = 0; i < 4 && i < c.len; ++i) locked4 = locked4 && c.s[(size_t) i].lock;
    C.ok (cell1 != cell0 && locked4 && p.idea().mutant.generation == 3, "MUTANT panel: GENERATE makes a new cell, LOCK FIRST 4 locks, MUTATE / OCTAVE / RHYTHM count generations");
    // click a step in the strip: it toggles its lock
    const int step = juce::jmin (5, c.len - 1); const bool was = c.s[(size_t) step].lock;
    { juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(), mp.stepRect (step).getCentre(), {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &mp, &mp, juce::Time::getCurrentTime(), mp.stepRect (step).getCentre(), juce::Time::getCurrentTime(), 1, false); mp.mouseUp (e); }
    C.ok (p.idea().mutant.cell.s[(size_t) step].lock != was, "MUTANT panel: clicking a step toggles its lock");
    // the mode buttons switch the mode (new cell, the mode's defaults)
    mp.modeSeg.sel = 5; mp.modeSeg.onChange (5);
    C.ok (p.idea().mutant.P.mode == MutCyberpop && p.idea().mutant.active && mp.modeSeg.items.size() == kMutantButtons && mp.modeSeg.items[5] == "CYBERPOP", "MUTANT panel: the mode buttons switch the engine's mode (the sixth is CYBERPOP)");
    { auto P = p.idea().mutant.P; P.mode = MutFlute; p.mutantSetParams (P, true); mp.sync(); const bool none = mp.modeSeg.sel < 0; const auto P2 = mp.paramsFromUi();
      C.ok (none && P2.mode == MutFlute && juce::String (mutantModeBlurb (MutFlute)).contains ("older"), "MUTANT panel: an older session's FLUTE lights no button, keeps its mode, and the blurb says so");
      mp.modeSeg.sel = 0; mp.modeSeg.onChange (0); }
    // undo walks back through the panel's steps
    int undos = 0; while (p.canUndo() && undos < 40) { p.undo(); ++undos; }
    C.ok (undos > 5, "MUTANT panel: every button is one undo step");
    if (shotDir != juce::File())
    {
        shotDir.createDirectory();
        auto shot = [&] (const juce::String& name) {
            ed->toast ("", -1.0e15); ed->refresh(); mp.sync();
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            auto f = shotDir.getChildFile (name + ".png"); f.deleteFile(); juce::FileOutputStream os (f); juce::PNGImageFormat png; png.writeImageToStream (img, os);
            std::printf ("shot %s\n", f.getFullPathName().toRawUTF8());
        };
        for (int m = 0; m < kMutantModes; ++m)
        {
            ArpProcessor q; makeMutant (q, m, 100 + (uint32_t) m, m == MutFlute ? Trap : m == MutDream ? Dusk : Midnight, [m] (MutantParams& P) { P.twoLine = m == MutHyper ? TwoAnswer : m == MutRage ? TwoInterlock : 0; if (m == MutDigital) P.evolveBars = 8; });
            q.hintsSeen = true; if (m == MutRage) q.mutantLockFirst (4);
            std::unique_ptr<ArpEditor> e2 (dynamic_cast<ArpEditor*> (q.createEditor())); e2->setSize (1240, 820); e2->refresh(); e2->openMutant (true); e2->refresh();
            e2->toast ("", -1.0e15); e2->refresh();
            auto img = e2->createComponentSnapshot (e2->getLocalBounds(), true, 1.0f);
            auto f = shotDir.getChildFile (juce::String ("mutant_") + mutantModeName (m) + ".png"); f.deleteFile(); juce::FileOutputStream os (f); juce::PNGImageFormat png; png.writeImageToStream (img, os);
            std::printf ("shot %s\n", f.getFullPathName().toRawUTF8());
            if (m == MutHyper) { e2->applyThemeNow (q.idea().mode); q.theme = "ghost"; e2->applyThemeNow (q.idea().mode); e2->refresh(); auto im2 = e2->createComponentSnapshot (e2->getLocalBounds(), true, 1.0f); auto f2 = shotDir.getChildFile ("mutant_HYPER_ghost.png"); f2.deleteFile(); juce::FileOutputStream o2 (f2); png.writeImageToStream (im2, o2); std::printf ("shot %s\n", f2.getFullPathName().toRawUTF8()); }
            e2->openMutant (false); e2->refresh();
            if (m == MutRage) { auto im3 = e2->createComponentSnapshot (e2->getLocalBounds(), true, 1.0f); auto f3 = shotDir.getChildFile ("mutant_RAGE_roll.png"); f3.deleteFile(); juce::FileOutputStream o3 (f3); png.writeImageToStream (im3, o3); std::printf ("shot %s\n", f3.getFullPathName().toRawUTF8()); }
        }
        shot ("mutant_panel_after_edits");
    }
}

// demos: a .mid + a rendered WAV per mode through the plugin's own synth, plus LOCK + MUTATE and EVOLVE examples
void demos (const juce::File& dir)
{
    dir.createDirectory(); ghosty::License::testForce().store (1);   // (the demo build writes MIDI only when unlocked)
    struct D { const char* name; int mode; int mood; uint32_t seed; int twoLine; int evolveBars; int sound; bool lockMutate; };
    const D ds[] { { "RAGE", MutRage, Midnight, 7, 0, 0, arpsyn::Supersaw, false }, { "ALIEN", MutAlien, Midnight, 12, 0, 0, arpsyn::Acid, false }, { "DIGITAL", MutDigital, Midnight, 3, 0, 0, arpsyn::Pluck, false },
                   { "DREAM", MutDream, Dusk, 5, 0, 0, arpsyn::WarmPadKeys, false }, { "HYPER", MutHyper, Midnight, 9, TwoAnswer, 0, arpsyn::Euphoric, false },
                   { "CYBERPOP_1", MutCyberpop, Midnight, 4, 0, 0, arpsyn::Euphoric, false }, { "CYBERPOP_2", MutCyberpop, Sunrise, 21, 0, 0, arpsyn::Neon, false }, { "CYBERPOP_3", MutCyberpop, Dusk, 33, 0, 0, arpsyn::Supersaw, false },
                   { "CYBERPOP_4", MutCyberpop, Midnight, 58, 0, 8, arpsyn::Euphoric, false }, { "CYBERPOP_5", MutCyberpop, Trap, 77, 0, 0, arpsyn::Bell, false }, { "CYBERPOP_6", MutCyberpop, Midnight, 91, 0, 16, arpsyn::Pluck, false },
                   { "LOCK_MUTATE", MutRage, Midnight, 7, 0, 0, arpsyn::Supersaw, true }, { "EVOLVE16", MutDigital, Midnight, 3, 0, 16, arpsyn::Pluck, false }, { "TWO_INTERLOCK", MutRage, Midnight, 15, TwoInterlock, 0, arpsyn::Neon, false },
                   { "TWO_ANSWER", MutCyberpop, Midnight, 4, TwoAnswer, 0, arpsyn::Euphoric, false }, { "TWO_SHADOW", MutDigital, Midnight, 3, TwoShadow, 0, arpsyn::Pluck, false } };
    for (auto& d : ds)
    {
        ArpProcessor p; makeMutant (p, d.mode, d.seed, d.mood, [&] (MutantParams& P) { P.twoLine = d.twoLine; P.evolveBars = d.evolveBars; });
        p.setLaneChoice (LaneArp, d.sound + 1); if (d.twoLine) p.setLaneChoice (LaneMelody, d.sound + 1);
        p.change ([&] (Idea& X) { X.g.bpm = d.mode == MutDream ? 140 : d.mode == MutCyberpop ? 146 : 152; X.lanes[LaneMelody].mute = d.twoLine == 0; }, false);
        if (d.lockMutate)
        {   // 4 bars as generated, then 3 mutations of 4 bars each with the first 4 steps locked: one 16-bar take
            p.mutantLockFirst (4);
            std::vector<Note> all; const Idea base = p.idea(); const int seg = BAR * 4;
            auto take = [&] (int k) { for (auto n : p.idea().lanes[LaneArp].notes) if (n.t < seg) { n.t += k * seg; all.push_back (n); } };
            take (0); for (int k = 1; k <= 3; ++k) { p.mutantMutate (0, 0.5); take (k); }
            p.change ([&] (Idea& X) { X.mutant.active = false; X.songBars = 16; rebuildChords (X); X.lanes[LaneArp].notes = all; X.lanes[LaneArp].edited = true; X.lanes[LaneMelody].mute = true; }, false);
        }
        const auto f = p.writeMidiFile ((1 << LaneArp) | (1 << LaneChords) | (1 << LaneBass) | (d.twoLine ? (1 << LaneMelody) : 0), false);
        if (f != juce::File()) f.moveFileTo (dir.getChildFile (juce::String ("mutant_") + d.name + ".mid"));
        auto job = p.renderJob (48000, p.hostOrIdeaBpm()); job.tail = 1.5;
        auto buf = ArpProcessor::renderOffline (job, -1);
        ArpProcessor::writeWav (dir.getChildFile (juce::String ("mutant_") + d.name + ".wav"), buf, 48000);
        std::printf ("demo %s: %d bars, %zu arp notes, cell %s\n", d.name, p.idea().total / BAR, p.idea().lanes[LaneArp].notes.size(), mutantPrint (p.idea().mutant.cell).c_str());
    }
    ghosty::License::testForce().store (-1);
}
// --dump-mid <dir>: generated lines as .mid for the 4-note no-copy check (tools/midi_ngram_check.py --ref <packs> --gen <dir>):
// every shown mode x 12 seeds x 3 moods, the ARP lane only; CYBERPOP (the learned mode) 60 seeds x 5 moods, with EVOLVE and TWO LINES on some
void dumpMidi (const juce::File& dir, int seedBase = 0)
{   // seedBase > 0: an out-of-sample set (the same files, seeds shifted) for the no-copy check
    dir.createDirectory(); ghosty::License::testForce().store (1); int files = 0;
    for (int m = 0; m < kMutantModes; ++m)
    {
        if (! mutantModeShown (m)) continue;
        const bool learned = m == MutCyberpop;
        for (int mood : { (int) Midnight, (int) Dusk, (int) Trap, (int) Sunrise, (int) HipHop })
            for (uint32_t s = 1; s <= (learned ? 60u : 12u); ++s)
            {
                if (! learned && (mood == Sunrise || mood == HipHop)) continue;
                ArpProcessor p; makeMutant (p, m, (s + (uint32_t) seedBase) * 17 + (uint32_t) mood, mood, [&] (MutantParams& P) { if (learned && s % 5 == 0) P.evolveBars = 8; if (learned && s % 7 == 0) P.twoLine = TwoAnswer; if (learned && s % 11 == 0) P.cellSteps = 32; });
                const auto f = p.writeMidiFile ((1 << LaneArp) | (p.idea().mutant.P.twoLine ? (1 << LaneMelody) : 0), false);
                if (f != juce::File()) { f.moveFileTo (dir.getChildFile (juce::String (mutantModeName (m)) + "_" + modeName (mood) + "_s" + juce::String ((int) s) + ".mid")); ++files; }
            }
    }
    ghosty::License::testForce().store (-1);
    std::printf ("dump-mid: %d files in %s\n", files, dir.getFullPathName().toRawUTF8());
}
} // namespace muttest
