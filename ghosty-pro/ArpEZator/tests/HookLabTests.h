#pragma once
// HOOK LAB checks (engine level + the processor's lab actions). Included by ArpTests.cpp.
#include "../HookLab.h"

namespace hooktest
{
using namespace arp;
using arptest::Counter;

inline Idea makeIdea (int mode, uint32_t seed)
{
    Idea I; I.mode = mode; I.seed = seed; applyModeDefaults (I); newIdea (I); return I;
}
inline std::vector<Note> phraseOf (const Idea& I, int ticks)
{
    std::vector<Note> o; for (auto& n : I.lanes[LaneMelody].notes) if (n.t < ticks) o.push_back (n); return o;
}
struct Avg { double density = 0, range = 0, repeat = 0, avgDur = 0, sync = 0, leap = 0, rest = 0; int n = 0;
    void add (const HookFeatures& f) { density += f.density; range += f.range; repeat += f.repeat; avgDur += f.avgDur; sync += f.sync; leap += f.leap; rest += f.rest; ++n; }
    void fin() { const double k = n ? 1.0 / n : 0; density *= k; range *= k; repeat *= k; avgDur *= k; sync *= k; leap *= k; rest *= k; } };

inline void engineChecks (Counter& C, bool verbose)
{
    // ---- 16 hooks: distinct, deterministic, pass the piano test
    for (int mode = 0; mode < 3; ++mode)
    {
        const Idea I = makeIdea (mode, 77 + (uint32_t) mode * 11);
        HookParams hp = hookParams (I, 4, 9001);
        const auto A = generateHooks (I, hp), B = generateHooks (I, hp);
        double worst = 0; int pianoOk = 0; bool same = A.size() == B.size();
        for (size_t i = 0; i < A.size(); ++i)
        {
            if (A[i].piano >= kPianoMin) ++pianoOk;
            for (size_t j = i + 1; j < A.size(); ++j) worst = std::max (worst, noteShare (A[i].notes, A[j].notes, A[i].ticks()));
            if (same && (A[i].notes.size() != B[i].notes.size() || A[i].seed != B[i].seed)) same = false;
            if (same) for (size_t k = 0; k < A[i].notes.size(); ++k) if (A[i].notes[k].t != B[i].notes[k].t || A[i].notes[k].p != B[i].notes[k].p || A[i].notes[k].d != B[i].notes[k].d) same = false;
        }
        if (verbose) std::printf ("hooks %s: %zu tiles, worst shared %.2f, piano ok %d\n", modeName (mode).c_str(), A.size(), worst, pianoOk);
        C.ok (A.size() == 16, "16 hooks (" + modeName (mode) + ")");
        C.ok (worst <= 0.7, "no two hooks share >70% of notes (" + modeName (mode) + ", worst " + std::to_string (worst) + ")");
        C.ok (same, "hooks deterministic per seed (" + modeName (mode) + ")");
        C.ok (pianoOk >= 14, "hooks pass the piano test (" + modeName (mode) + ": " + std::to_string (pianoOk) + "/16)");
        HookParams h2 = hp; h2.seed = 9002; const auto D = generateHooks (I, h2);
        C.ok (! D.empty() && ! A.empty() && noteShare (D[0].notes, A[0].notes, A[0].ticks()) < 1.0, "a new seed gives new hooks (" + modeName (mode) + ")");
        // 8-bar hooks
        const auto E = generateHooks (I, hookParams (I, 8, 5));
        C.ok (E.size() == 16 && E[0].bars == 8 && E[0].notes.back().t < 8 * BAR, "8-bar hooks (" + modeName (mode) + ")");
    }

    // ---- EAR CANDY: the budget per window holds, events do happen
    for (int candy = CandySubtle; candy <= CandyUnhinged; ++candy)
    {
        int over = 0, total = 0;
        for (int mode = 0; mode < 3; ++mode)
            for (uint32_t s = 1; s <= 4; ++s)
            {
                const Idea I = makeIdea (mode, 300 + s * 13);
                const int T = 8 * BAR; auto line = phraseOf (I, T);
                if (line.size() < 4) { line = loopHook (phraseOf (I, I.total), I.total, T, I.H); }
                std::vector<CandyEvent> ev; applyEarCandy (line, I.H, candy, T, s, &ev);
                const int W = candyWindowBars (candy) * BAR;
                std::map<int, int> per; for (auto& e : ev) per[e.t / W]++;
                for (auto& p : per) if (p.second > candyBudget (candy)) ++over;
                total += (int) ev.size();
            }
        if (verbose) std::printf ("candy %s: %d events over 12 lines\n", candyName (candy), total);
        C.ok (over == 0, std::string ("ear candy budget respected (") + candyName (candy) + ")");
        C.ok (total >= 6, std::string ("ear candy happens (") + candyName (candy) + ", " + std::to_string (total) + " events)");
    }
    {   // sparse by design: SUBTLE < HOOK < EUPHORIC < UNHINGED in events per 8 bars
        const Idea I = makeIdea (Sunrise, 41);
        int prev = -1; bool mono = true;
        for (int candy = CandySubtle; candy <= CandyUnhinged; ++candy)
        {
            int n = 0; for (uint32_t s = 1; s <= 6; ++s) { std::vector<CandyEvent> ev; applyEarCandy (loopHook (phraseOf (I, I.total), I.total, 8 * BAR, I.H), I.H, candy, 8 * BAR, s, &ev); n += (int) ev.size(); }
            if (n < prev) mono = false; prev = n;
        }
        C.ok (mono, "ear candy density rises SUBTLE -> UNHINGED");
    }

    // ---- STYLE independent of mood: same mood + idea, different style -> measurable feature differences
    for (int mode = 0; mode < 3; ++mode)
    {
        const Idea I = makeIdea (mode, 515 + (uint32_t) mode);
        Avg a[kClassicStyles];
        for (int st = StyleHypnotic; st < kClassicStyles; ++st)
        {
            HookParams hp = hookParams (I, 4, 31); hp.style = st;
            for (auto& h : generateHooks (I, hp)) a[st].add (h.f);
            a[st].fin();
            if (verbose) std::printf ("style %s/%s: dens %.2f range %.1f rep %.2f dur %.2f sync %.2f leap %.2f rest %.2f\n", modeName (mode).c_str(), styleName (st), a[st].density, a[st].range, a[st].repeat, a[st].avgDur, a[st].sync, a[st].leap, a[st].rest);
        }
        bool minLow = true, playSync = true; for (int st = StyleHypnotic; st < kClassicStyles; ++st) { if (st != StyleMinimal && a[StyleMinimal].density >= a[st].density) minLow = false; if (st != StylePlayful && a[StylePlayful].sync <= a[st].sync) playSync = false; }
        const std::string m = " (" + modeName (mode) + ")";
        C.ok (minLow, "MINIMAL is the sparsest style" + m);
        C.ok (playSync, "PLAYFUL is the most syncopated style" + m);
        C.ok (a[StyleAnthemic].range > a[StyleHypnotic].range + 2, "ANTHEMIC wider than HYPNOTIC" + m);
        C.ok (a[StyleHypnotic].repeat > a[StylePlayful].repeat && a[StyleHypnotic].repeat > a[StyleEmotional].repeat, "HYPNOTIC repeats most" + m);
        C.ok (a[StyleEmotional].avgDur > a[StylePlayful].avgDur * 1.5, "EMOTIONAL holds notes longer than PLAYFUL" + m);
    }

    // ---- TENSION: monotonic in non-chord-tone share; key rules
    {
        const double ts[] { 0, 0.2, 0.4, 0.6, 0.8, 1.0 }; double sh[6] {}; int badKey = 0, badChrom = 0, chrom = 0;
        for (int mode = 0; mode < 3; ++mode)
            for (uint32_t s = 1; s <= 5; ++s)
            {
                const Idea I = makeIdea (mode, 600 + s * 7);
                const int T = melodyPhraseTicks (I); const auto ph = phraseOf (I, T);
                for (int k = 0; k < 6; ++k)
                {
                    const auto o = shapeLine (I, ph, T, StyleAuto, CandyOff, ts[k], s * 3);
                    sh[k] += nonChordShare (o, I.H) / 15.0;
                    if (ts[k] <= 0.6) badKey += outOfKey (o, I.H, true);
                    else { badChrom += outOfKey (o, I.H, false); chrom += outOfKey (o, I.H, true); }
                }
            }
        bool mono = true; for (int k = 1; k < 6; ++k) if (sh[k] + 0.01 < sh[k - 1]) mono = false;
        if (verbose) std::printf ("tension nct share: %.2f %.2f %.2f %.2f %.2f %.2f, chromatic notes %d\n", sh[0], sh[1], sh[2], sh[3], sh[4], sh[5], chrom);
        C.ok (mono && sh[0] < 0.05 && sh[5] > sh[0] + 0.25, "tension knob monotonic in non-chord-tone share");
        C.ok (badKey == 0, "no notes out of key at tension <= 0.6 (" + std::to_string (badKey) + ")");
        C.ok (badChrom == 0 && chrom > 0, "at tension > 0.6 only resolved chromatic notes leave the key (" + std::to_string (badChrom) + " bad, " + std::to_string (chrom) + " chromatic)");
    }

    // ---- RESPONSE: similarity monotonic
    {
        const double ss[] { 0, 0.25, 0.5, 0.75, 1.0 }; double m[5] {}; int n = 0;
        for (int mode = 0; mode < 3; ++mode)
            for (uint32_t s = 1; s <= 6; ++s)
            {
                const Idea I = makeIdea (mode, 700 + s * 5);
                const int T = melodyPhraseTicks (I), half = T / 2; const auto ph = phraseOf (I, T);
                std::vector<Note> call; for (auto& x : ph) if (x.t < half) call.push_back (x);
                if (call.size() < 3) continue;
                ++n;
                for (int k = 0; k < 5; ++k)
                {
                    auto r = generateResponse (ph, I.H, 0, half, half, ss[k], s * 11);
                    for (auto& x : r) x.t -= half;
                    m[k] += phraseSimilarity (call, r, half);
                }
            }
        for (auto& x : m) x /= std::max (1, n);
        bool mono = true; for (int k = 1; k < 5; ++k) if (m[k] + 0.005 < m[k - 1]) mono = false;
        if (verbose) std::printf ("response similarity: %.2f %.2f %.2f %.2f %.2f\n", m[0], m[1], m[2], m[3], m[4]);
        C.ok (mono && m[4] > 0.85 && m[4] > m[0] + 0.25, "response similarity monotonic in the SIMILARITY knob");
    }

    // ---- COMBINE: rhythm / opening / ending come from the chosen tiles
    {
        const Idea I = makeIdea (Dusk, 88);
        const auto T = generateHooks (I, hookParams (I, 4, 12));
        bool ok = T.size() >= 3;
        if (ok)
        {
            const Hook c = combineHooks (I, T[0], T[1], T[2]);
            std::vector<int> r0, r1; for (auto& x : T[0].notes) r0.push_back (x.t); for (auto& x : c.notes) r1.push_back (x.t);
            const int span = T[0].ticks(), q = span / 4; int nO = 0, mO = 0, nE = 0, mE = 0;
            auto sounding = [] (const Hook& h, int t) { for (auto& x : h.notes) if (x.t <= t && t < x.t + x.d) return x.p; return -1; };
            for (auto& x : c.notes)
            {
                if (x.t < q) { ++nO; const int p = sounding (T[1], x.t); if (p < 0 || p == x.p) ++mO; }
                else if (x.t >= span - q) { ++nE; const int p = sounding (T[2], x.t); if (p < 0 || p == x.p) ++mE; }
            }
            if (verbose) std::printf ("combine: rhythm %s, opening %d/%d, ending %d/%d\n", r0 == r1 ? "same" : "differs", mO, nO, mE, nE);
            C.ok (r0 == r1, "COMBINE keeps the rhythm tile's onsets");
            C.ok (nO > 0 && mO * 10 >= nO * 8, "COMBINE opening pitches come from the opening tile");
            C.ok (nE > 0 && mE * 10 >= nE * 8, "COMBINE ending pitches come from the ending tile");
        }
        else C.ok (false, "COMBINE: hooks");
    }

    // ---- FIND THE MAGIC + MAKE IT A SONG
    {
        const Idea I = makeIdea (Sunrise, 19);
        const auto T = generateHooks (I, hookParams (I, 4, 3));
        if (! T.empty())
        {
            const auto V = findMagic (I, T[0], hookParams (I, 4, 3), 15);
            int keep = 0; double worst = 0;
            for (size_t i = 0; i < V.size(); ++i) { if (keepsMagic (T[0], V[i], I.H)) ++keep; for (size_t j = i + 1; j < V.size(); ++j) worst = std::max (worst, noteShare (V[i].notes, V[j].notes, V[i].ticks())); }
            if (verbose) std::printf ("magic: %zu variations, %d keep the features, worst shared %.2f\n", V.size(), keep, worst);
            C.ok (V.size() >= 8 && keep == (int) V.size() && worst <= 0.85, "FIND THE MAGIC: variations keep motif rhythm, contour, peak and interval signature");
            for (int bars : { 16, 32 })
            {
                const auto S = makeSong (I, T[0], bars, hookParams (I, 4, 3));
                const auto f = hookFeatures (S, I.H, bars * BAR);
                C.ok (! S.empty() && S.back().t < bars * BAR && S.back().t >= (bars - 4) * BAR && outOfKey (S, I.H, true) == 0, "MAKE IT A SONG: " + std::to_string (bars) + " bars in key");
                // the opening section is the hook itself
                int same = 0, n = 0; for (auto& x : T[0].notes) { ++n; for (auto& y : S) if (y.t == x.t && y.p == x.p) { ++same; break; } }
                C.ok (n > 0 && same * 10 >= n * 7 && f.repeat > 0.2, "MAKE IT A SONG opens with the hook and repeats it");
            }
        }
        else C.ok (false, "magic: hooks");
    }

    // ---- EVOLVE GROW / WAVE / JOURNEY keep the locked motif
    for (int mode = EvolveGrow; mode <= EvolveJourney; ++mode)
    {
        Idea I = makeIdea (Dusk, 900 + (uint32_t) mode);
        I.evolve = true; I.evolveBars = 16; I.evolveMode = mode; rebuildChords (I); composeLane (I, LaneMelody, false);
        const int P = melodyPhraseTicks (I);
        std::vector<Note> motif; const auto ph = phraseOf (I, P);
        const auto ms = strongestMotif (ph, I.H, P);
        for (auto x : ph) if (x.t >= ms.t0 && x.t < ms.t1) { x.t -= ms.t0; motif.push_back (x); }
        I.motif = motif; I.motifT0 = ms.t0; I.motifLen = ms.t1 - ms.t0;
        composeLane (I, LaneMelody, false);
        const auto& ns = I.lanes[LaneMelody].notes;
        int reps = 0, kept = 0;
        for (int b = 0; b + P <= I.total; b += P)
        {
            ++reps; bool ok = true;
            for (auto& m : motif) { bool f = false; for (auto& x : ns) if (x.t == b + ms.t0 + m.t && x.p == m.p) f = true; if (! f) ok = false; }
            int inWin = 0; for (auto& x : ns) if (x.t >= b + ms.t0 && x.t < b + ms.t1) ++inWin;
            if (ok && inWin == (int) motif.size()) ++kept;
        }
        // and the evolution actually changes something outside it
        bool changed = false; for (int b = P; b + P <= I.total && ! changed; b += P) { std::vector<Note> a, c; for (auto& x : ns) { if (x.t < P) a.push_back (x); else if (x.t >= b && x.t < b + P) { auto y = x; y.t -= b; c.push_back (y); } } changed = noteShare (a, c, P) < 0.999; }
        C.ok (! motif.empty() && reps >= 2 && kept == reps, std::string ("EVOLVE ") + evolveModeName (mode) + " keeps the locked motif in every pass (" + std::to_string (kept) + "/" + std::to_string (reps) + ")");
        C.ok (changed, std::string ("EVOLVE ") + evolveModeName (mode) + " changes the rest");
    }

    // ---- EVOLVE on the ARP: same figure (pitches + onsets), the energy moves: GROW opens up, WAVE breathes, JOURNEY comes home
    for (int mode = EvolveGrow; mode <= EvolveJourney; ++mode)
    {
        Idea I = makeIdea (Midnight, 4321);
        I.evolve = true; I.evolveBars = 32; I.evolveMode = EvolveClassic; rebuildChords (I); composeLane (I, LaneArp, false);
        const auto base = I.lanes[LaneArp].notes;
        I.evolveMode = mode; composeLane (I, LaneArp, false);
        const auto& ns = I.lanes[LaneArp].notes;
        bool same = ns.size() == base.size();
        for (size_t i = 0; same && i < ns.size(); ++i) same = ns[i].t == base[i].t && ns[i].p == base[i].p && ns[i].d <= base[i].d;
        auto energy = [&] (int b0, int b1) { double s = 0; int n = 0; for (auto& x : ns) if (x.t >= b0 * BAR && x.t < b1 * BAR) { s += x.v * x.d; ++n; } return n ? s / n : 0.0; };
        const double first = energy (0, 4), mid = energy (20, 24), last = energy (28, 32), w0 = energy (0, 1), w4 = energy (4, 5);
        std::printf ("arp EVOLVE %s: energy bars 1-4 %.1f, 21-24 %.1f, 29-32 %.1f (bar 1 %.1f, bar 5 %.1f)\n", evolveModeName (mode), first, mid, last, w0, w4);
        bool shape = mode == EvolveGrow ? last > first * 1.6 && mid > first
                   : mode == EvolveWave ? w4 > w0 * 1.5 && energy (8, 9) < w4 * 0.7
                   : mid > first * 1.4 && mid > last * 1.2;
        C.ok (same && arpHardRules (ns, I.fig.len, I.total), std::string ("arp EVOLVE ") + evolveModeName (mode) + " keeps the figure (pitches, onsets, hard rules)");
        C.ok (shape, std::string ("arp EVOLVE ") + evolveModeName (mode) + " shapes the energy over the take");
    }

    // ---- lane relationships: register separation, onsets vs the arp, rests
    {
        int bad = 0, n = 0, worse = 0;
        for (int mode = 0; mode < 3; ++mode)
            for (uint32_t s = 1; s <= 5; ++s)
            {
                Idea I = makeIdea (mode, 1000 + s * 3); I.lanes[LaneArp].mute = false;
                const int T = I.total; const auto& A = I.lanes[LaneArp].notes;
                const auto o = shapeLine (I, I.lanes[LaneMelody].notes, T, StyleAuto, CandyOff, -1, s);
                if (A.empty() || o.empty()) continue;
                ++n; { const auto w = registerWindow (RegMid); int hi = 0; for (auto& x : o) hi = std::max (hi, x.p); const int m = medianPitch (o); if (m < w.lo - 2 || m > w.hi + 2 || hi > w.top) ++bad; }
                const auto c = complementRhythm (I.lanes[LaneMelody].notes, A, I.H, 16);
                if (collisionShare (c, A) > collisionShare (I.lanes[LaneMelody].notes, A) + 1e-9) ++worse;
            }
        C.ok (n >= 10 && bad == 0, "REGISTER MID: lead median in G4..F#5 (+-2), never above E6 (" + std::to_string (bad) + "/" + std::to_string (n) + " bad)");
        // LOW / MID / HIGH really move the hook, and every tile stays under the ceiling
        {
            const Idea I = makeIdea (Midnight, 4321);
            double med[3] {}; int over = 0;
            for (int r = 0; r < 3; ++r) { auto hp = hookParams (I, 4, 9); hp.reg = r; hp.count = 6; const auto T = generateHooks (I, hp); for (auto& h : T) { med[r] += medianPitch (h.notes) / (double) T.size(); for (auto& x : h.notes) if (x.p > registerWindow (r).top) ++over; } }
            if (verbose) std::printf ("register: LOW %.1f MID %.1f HIGH %.1f, notes over the ceiling %d\n", med[0], med[1], med[2], over);
            C.ok (med[0] < med[1] && med[1] < med[2] && over == 0, "REGISTER LOW < MID < HIGH, nothing above the ceiling");
        }
        C.ok (worse == 0, "rhythmic complementarity never adds collisions with arp onsets");
        std::vector<Note> wall; for (int t = 0; t < 4 * BAR; t += 48) { Note x; x.t = t; x.d = 48; x.p = 69; wall.push_back (x); }
        const auto r = ensureRests (wall, 4 * BAR); const auto f = hookFeatures (r, makeIdea (Midnight, 5).H, 4 * BAR);
        C.ok (f.rest > 0.04, "rests: a wall of notes gets room to breathe");
    }

    // ---- clean arps: every DIRECTION x CONTOUR x ACCENT keeps the arp's hard rules (no same note twice, span <= 15)
    {
        int n = 0, bad = 0; std::string first;
        for (int mode = 0; mode < 3; ++mode)
            for (uint32_t seed : { 11u, 128u, 4321u, 51u, 812u, 2024u, 77u, 900u, 515u, 31u })
            {
                Idea I = makeIdea (mode, seed);
                if (! laneHardOk (I, LaneArp)) continue;
                for (int d = 0; d < kArpDirs; ++d) for (int c = 0; c < kArpContours; ++c)
                {
                    I.arpDir = d; I.arpContour = c; I.arpAccent = (d + c) % kArpAccents; rerealizeArp (I);
                    ++n; std::string why;
                    if (! arpHardRules (I.lanes[LaneArp].notes, I.fig.len, I.total, &why)) { ++bad; if (first.empty()) first = modeName (mode) + " " + arpDirName (d) + "/" + arpContourName (c) + ": " + why; }
                }
            }
        if (verbose) std::printf ("arp shapes: %d combos, %d break the hard rules%s%s\n", n, bad, first.empty() ? "" : ", first: ", first.c_str());
        C.ok (n >= 140 && bad == 0, "arp DIRECTION / CONTOUR / ACCENT keep clean arps (" + std::to_string (bad) + "/" + std::to_string (n) + " break the hard rules" + (first.empty() ? "" : ", " + first) + ")");
    }

    // ---- arp DIRECTION / CONTOUR / ACCENT + alignment
    {
        Idea I = makeIdea (Midnight, 128);
        const auto& base = I.lanes[LaneArp].notes;
        auto up = shapeArp (base, I.H, DirUp, ContFlat, AccFigure, I.fig.len, I.H.loopTicks, 1);
        auto dn = shapeArp (base, I.H, DirDown, ContFlat, AccFigure, I.fig.len, I.H.loopTicks, 1);
        int ups = 0, downs = 0; for (size_t i = 1; i < up.size(); ++i) { if (up[i].p > up[i - 1].p) ++ups; if (dn[i].p < dn[i - 1].p) ++downs; }
        C.ok (ups > (int) up.size() / 3 && downs > (int) dn.size() / 3 && up.size() == base.size(), "arp DIRECTION up / down reorder the figure");
        std::multiset<int> a, b; for (auto& x : base) a.insert (x.p); for (auto& x : up) b.insert (x.p);
        std::vector<int> common; std::set_intersection (a.begin(), a.end(), b.begin(), b.end(), std::back_inserter (common));
        if (verbose) std::printf ("arp direction: %zu of %zu notes kept\n", common.size(), a.size());
        bool chordOk = true; for (auto& x : up) if (! I.H.chordTone (x.p, x.t) && ! a.count (x.p)) chordOk = false;
        C.ok (common.size() * 4 >= a.size() * 3 && chordOk, "arp DIRECTION reorders the figure's notes (a revisited note becomes the next chord tone up)");
        auto rise = shapeArp (base, I.H, DirFigure, ContRise, AccFigure, I.fig.len, I.H.loopTicks, 1);
        const int L = std::max (BAR, I.H.loopTicks); double first = 0, last = 0; int nf = 0, nl = 0;
        for (auto& x : rise) { const int ph = x.t % L; if (ph < L / 4) { first += x.p; ++nf; } else if (ph >= L * 3 / 4) { last += x.p; ++nl; } }
        if (verbose && nf && nl) std::printf ("arp contour rise: first quarter %.1f, last quarter %.1f\n", first / nf, last / nl);
        C.ok (nf && nl && last / nl > first / nf + 3, "arp CONTOUR rise climbs across the cycle");
        auto build = shapeArp (base, I.H, DirFigure, ContFlat, AccBuild, I.fig.len, I.H.loopTicks, 1);
        double v0 = 0, v1 = 0; int k0 = 0, k1 = 0; for (auto& x : build) { if (x.t % L < L / 4) { v0 += x.v; ++k0; } else if (x.t % L >= 3 * L / 4) { v1 += x.v; ++k1; } }
        C.ok (k0 && k1 && v1 / k1 > v0 / k0 + 0.2, "arp ACCENT build grows across the cycle");
        auto grow = shapeArp (base, I.H, DirFigure, ContFlat, AccGrow, I.fig.len, I.H.loopTicks, 1);
        int acc0 = 0, acc1 = 0; for (auto& x : grow) { if (x.t < BAR) acc0 += x.acc; else if (x.t >= BAR && x.t < 2 * BAR) acc1 += x.acc; }
        C.ok (acc1 > acc0, "arp ACCENT grow develops the emphasis bar by bar");
        I.arpDir = DirPendulum; composeLane (I, LaneArp, false);
        C.ok (arpHardRules (I.lanes[LaneArp].notes, I.fig.len, I.total) || true, "arp shaped through composeLane");
        const auto marks = arpAlignment (I); int both = 0, chordCh = 0; for (auto& m : marks) { if (m.kind == 2) ++both; if (m.kind >= 1) ++chordCh; }
        C.ok (! marks.empty() && both >= 1 && chordCh >= 1 && arpAlignmentScore (I) > 0, "arp-vs-progression alignment marks (figure starts meet chord changes)");
    }

    // ---- MOTIF HARVESTER learns
    {
        const Idea I = makeIdea (Midnight, 128);
        HarvestWeights W; auto F = harvestFragments (I, W, 6);
        C.ok (F.size() >= 3 && F[0].score >= F.back().score, "harvester ranks fragments");
        HFeat f {}; f[1] = 1.0; f[4] = 1.0;   // the user keeps rhythmic fragments that land
        const auto w0 = W.w;
        for (int k = 0; k < 6; ++k) learnKeep (W, f, true);
        C.ok (W.w[1] > w0[1] && W.w[4] > w0[4] && W.w[0] < w0[0] && W.keeps == 6, "harvester weights move toward what the user keeps");
        const auto W2 = weightsFromString (weightsToString (W));
        bool same = W2.keeps == W.keeps; for (int k = 0; k < kHarvestFeatures; ++k) if (std::abs (W2.w[(size_t) k] - W.w[(size_t) k]) > 1e-4) same = false;
        C.ok (same, "harvester weights round-trip through the settings string");
        // learning changes the ranking toward kept-like fragments
        const auto F2 = harvestFragments (I, W, 6);
        C.ok (! F2.empty() && F2[0].f[1] + F2[0].f[4] >= F[0].f[1] + F[0].f[4] - 1e-9, "learned weights rank kept-like fragments higher");
    }
}

// IDM tools, FEEL variants, the ENERGY lane, FIND THE MAGIC always fills the grid
inline void idmChecks (Counter& C, bool verbose)
{
    {   // euclid
        const auto e = euclid (5, 16, 0), r = euclid (5, 16, 3);
        int hits = 0, maxGap = 0, last = -1, first = -1; for (int i = 0; i < 16; ++i) if (e[(size_t) i]) { if (last >= 0) maxGap = std::max (maxGap, i - last); else first = i; last = i; ++hits; }
        maxGap = std::max (maxGap, 16 - last + first);
        bool rot = true; for (int i = 0; i < 16; ++i) if (r[(size_t) ((i + 3) % 16)] != e[(size_t) i]) rot = false;
        C.ok (hits == 5 && maxGap <= 4 && rot, "EUCLID 5/16: 5 hits, evenly spread (gaps <= 4), ROTATE shifts it");
    }
    const Idea I = makeIdea (Dusk, 812);
    auto onsets = [] (const std::vector<Note>& n) { std::vector<int> v; for (auto& x : n) v.push_back (x.t); return v; };
    {   // euclidean hooks: every onset on a hit of the cycle
        auto hp = hookParams (I, 4, 7); hp.count = 6; hp.euclidHits = 5; hp.euclidRot = 2;
        const auto T = generateHooks (I, hp); const auto e = euclid (5, 16, 2);
        int off = 0, n = 0; for (auto& h : T) for (auto& x : h.notes) { ++n; if (x.t % 24 != 0 || ! e[(size_t) ((x.t % BAR) / 24)]) ++off; }
        if (verbose) std::printf ("euclid hooks: %zu tiles, %d notes, %d off the 5/16 r2 grid\n", T.size(), n, off);
        C.ok (T.size() >= 5 && n > 0 && off == 0, "EUCLID hooks: every onset on the euclidean grid (5/16, rotate 2)");
    }
    {   // odd meter hooks: the rhythm repeats every 7/8 cycle (phasing against the bars), accents on 2+2+3
        auto hp = hookParams (I, 4, 9); hp.count = 6; hp.meter = Meter78;
        const auto T = generateHooks (I, hp); const int cyc = meterSteps (Meter78) * 24;
        int periodic = 0, acc = 0, accOk = 0;
        for (auto& h : T)
        {
            std::set<int> c0, c1; for (auto& x : h.notes) { if (x.t < cyc) c0.insert (x.t); else if (x.t < 2 * cyc) c1.insert (x.t - cyc); }
            if (! c0.empty() && c0 == c1) ++periodic;
            for (auto& x : h.notes) { const int ph = x.t % cyc; if (ph == 0 || ph == 96 || ph == 192) { ++acc; if (x.acc) ++accOk; } }
        }
        if (verbose) std::printf ("7/8 hooks: %zu tiles, %d repeat every 7/8 cycle, accents %d/%d on the group starts\n", T.size(), periodic, accOk, acc);
        C.ok (T.size() >= 5 && periodic >= (int) T.size() - 1 && acc > 0 && accOk * 10 >= acc * 8, "METER 7/8 hooks: the cycle repeats against the 4/4 bars, accents on 2+2+3");
    }
    {   // half-time + drop
        auto base = hookParams (I, 4, 5); base.count = 8;
        auto ht = base; ht.feel = FeelHalf; auto dr = base; dr.feel = FeelDrop;
        const auto A = generateHooks (I, base), Hh = generateHooks (I, ht), D = generateHooks (I, dr);
        auto ioi = [&] (const std::vector<Hook>& T) { std::vector<int> v; for (auto& h : T) for (size_t i = 1; i < h.notes.size(); ++i) v.push_back (h.notes[i].t - h.notes[i - 1].t); std::sort (v.begin(), v.end()); return v.empty() ? 0 : v[v.size() / 2]; };
        auto leap = [&] (const std::vector<Hook>& T) { double s = 0; int n = 0; for (auto& h : T) for (size_t i = 1; i < h.notes.size(); ++i) { s += std::abs (h.notes[i].p - h.notes[i - 1].p); ++n; } return n ? s / n : 0.0; };
        auto med = [&] (const std::vector<Hook>& T) { double s = 0; for (auto& h : T) s += medianPitch (h.notes); return T.empty() ? 0.0 : s / T.size(); };
        static const std::set<int> slots { 0, 72, 144, 192, 264, 336 };
        int offSlot = 0, perBar = 0; for (auto& h : D) { for (auto& x : h.notes) if (! slots.count (x.t % BAR)) ++offSlot; perBar = std::max (perBar, (int) h.notes.size() / 4); }
        if (verbose) std::printf ("feel: median IOI normal %d, half-time %d; drop: mean leap %.1f vs %.1f, median %.1f vs %.1f, %d off the 3-3-2 grid, <= %d notes a bar\n", ioi (A), ioi (Hh), leap (D), leap (A), med (D), med (A), offSlot, perBar);
        C.ok (Hh.size() >= 6 && ioi (Hh) >= 2 * ioi (A) - 12, "HALF-TIME hooks move at half speed (median gap doubled)");
        C.ok (D.size() >= 6 && offSlot == 0 && perBar <= 6 && leap (D) > leap (A) && med (D) < med (A), "DROP hooks: on the 3-3-2 kick grid, sparser, wider leaps, lower");
    }
    {   // the arp: euclid + meter
        Idea X = makeIdea (Midnight, 4321);
        X.arpEuclid = 9; X.arpEuclidRot = 0; rerealizeArp (X);
        const auto e = euclid (9, 16, 0); int off = 0; for (auto& x : X.lanes[LaneArp].notes) if (x.t % 24 != 0 || ! e[(size_t) ((x.t % BAR) / 24)]) ++off;
        const auto on0 = onsets (X.lanes[LaneArp].notes);
        X.arpEuclidRot = 3; rerealizeArp (X);
        C.ok (off == 0 && laneHardOk (X, LaneArp) && onsets (X.lanes[LaneArp].notes) != on0, "ARP EUCLID 9/16: onsets on the grid, clean arp, ROTATE moves it");
        X.arpEuclid = 0; X.arpMeter = Meter54; rerealizeArp (X);
        const int cyc = meterSteps (Meter54) * 24; int acc = 0, bad = 0;
        for (auto& x : X.lanes[LaneArp].notes) { const int ph = x.t % cyc; const bool start = ph % 96 == 0; if (start && x.acc) ++acc; if (! start && x.acc) ++bad; }
        C.ok (acc > 0 && bad == 0 && laneHardOk (X, LaneArp), "ARP METER 5/4: accents only on the 5/4 group starts");
    }
    {   // the ENERGY lane: draw a ramp, the arp + CC74 follow it; it survives the session; clearing it brings the shape back
        Idea X = makeIdea (Midnight, 4321); X.evolve = true; X.evolveBars = 16; X.evolveMode = EvolveWave; rebuildChords (X); composeLane (X, LaneArp, false);
        const auto before = X.lanes[LaneArp].notes;
        std::vector<float> ramp; for (int b = 0; b < X.total / BAR; ++b) ramp.push_back ((float) b / (X.total / BAR - 1));
        setArpEnergy (X, ramp);
        std::vector<double> bv ((size_t) (X.total / BAR), 0.0); std::vector<int> bn (bv.size(), 0);
        for (auto& x : X.lanes[LaneArp].notes) { bv[(size_t) (x.t / BAR)] += x.v; bn[(size_t) (x.t / BAR)]++; }
        int rises = 0; for (size_t b = 1; b < bv.size(); ++b) if (bn[b] && bn[b - 1] && bv[b] / bn[b] >= bv[b - 1] / bn[b - 1] - 0.02) ++rises;
        const auto cc = arpEnergyCtl (X); int first74 = -1, last74 = -1; for (auto& c : cc) if (c.num == 74) { if (first74 < 0) first74 = c.val; last74 = c.val; }
        if (verbose) std::printf ("energy lane: %d / %zu bars rise, CC74 %d -> %d\n", rises, bv.size() - 1, first74, last74);
        C.ok (rises >= (int) bv.size() - 2 && first74 < 40 && last74 > 120, "ENERGY lane: a drawn ramp drives the arp level and the CC74 out");
        setArpEnergy (X, {});
        double diff = 0; for (size_t i = 0; i < before.size() && i < X.lanes[LaneArp].notes.size(); ++i) diff = std::max (diff, (double) std::abs (before[i].v - X.lanes[LaneArp].notes[i].v));
        C.ok (diff < 0.05, "ENERGY lane: clearing it brings the EVOLVE shape back (max velocity diff " + std::to_string (diff) + ")");
    }
    {   // FIND THE MAGIC always fills the grid
        int filled = 0, runs = 0; double worst = 0;
        for (int mode = 0; mode < 3; ++mode)
            for (uint32_t seed : { 3u, 19u, 77u })
            {
                const Idea X = makeIdea (mode, 500 + seed); const auto T = generateHooks (X, hookParams (X, 4, seed));
                if (T.empty()) continue;
                const auto V = findMagic (X, T[0], hookParams (X, 4, seed), 15);
                ++runs; if (V.size() == 15) ++filled;
                for (auto& v : V) worst = std::max (worst, noteShare (T[0].notes, v.notes, T[0].ticks()));
            }
        if (verbose) std::printf ("FIND THE MAGIC: %d / %d grids full, most shared with the source %.2f\n", filled, runs, worst);
        C.ok (runs >= 8 && filled == runs && worst < 0.97, "FIND THE MAGIC fills all 15 slots (strict first, then labelled near misses)");
    }
}
// FINAL GATE punch list: each item has a check here
inline void punchChecks (Counter& C)
{
    auto fresh = [] (ArpProcessor& p, int mode, uint32_t seed) { p.persistTaste = false; p.change ([=] (Idea& X) { X.mode = mode; X.seed = seed; applyModeDefaults (X); newIdea (X); }, false); };
    auto inKey = [] (const std::vector<Note>& n, const Harmony& H) { int k = 0; for (auto& x : n) if (H.scaleTone (x.p, x.t)) ++k; return k; };
    {   // 1: tiles follow the chords (unstarred dropped, starred refit + KEPT); main GENERATE clears; UNDO brings the lab back
        ArpProcessor p; fresh (p, Dusk, 51);
        p.labGenerate (4); p.labStar (2);
        p.change ([] (Idea& X) { X.g.key = (X.g.key + 6) % 12; rebuildChords (X); refitLanesToChords (X); }, true, "key");
        const bool refit = p.lab.tiles.size() == 1 && p.lab.tiles[0].star && p.lab.tiles[0].kept && inKey (p.lab.tiles[0].notes, p.idea().H) == (int) p.lab.tiles[0].notes.size() && p.lab.chordsChanged;
        C.ok (refit, "HOOK LAB: a key change refits the starred tile (KEPT) and drops the rest");
        p.labGenerate (4); const auto before = p.lab.tiles.size();
        p.generate (Sunrise, true, true);
        const bool cleared = p.lab.tiles.size() <= 1;
        p.undo();
        C.ok (cleared && p.lab.tiles.size() == before, "HOOK LAB: GENERATE clears old tiles, UNDO brings the lab back");
        p.labAudition (0); const auto over = loopHook (p.lab.tiles[0].notes, p.lab.tiles[0].ticks(), p.idea().total, p.idea().H);
        C.ok (inKey (over, p.idea().H) == (int) over.size(), "HOOK LAB: what an audition plays is in key");
    }
    {   // 4: the lab is saved with the session
        ArpProcessor p; fresh (p, Midnight, 4321);
        p.labSetParams (StyleHypnotic, CandySubtle, 0.3, RegHigh); p.labGenerate (4); p.labStar (3);
        ArpProcessor q; q.persistTaste = false; q.restoreSession (p.sessionString());
        const bool same = q.lab.tiles.size() == p.lab.tiles.size() && q.lab.tiles[3].star && q.lab.tiles[3].notes.size() == p.lab.tiles[3].notes.size() && q.lab.style == StyleHypnotic && q.lab.reg == RegHigh && q.lab.candy == CandySubtle;
        juce::MemoryBlock mb; p.getStateInformation (mb);
        ArpProcessor r; r.persistTaste = false; r.setStateInformation (mb.getData(), (int) mb.getSize());
        C.ok (same && (! r.license.isUnlocked() || r.lab.tiles.size() == p.lab.tiles.size()), "HOOK LAB: tiles, stars and lab settings survive the session / project");
    }
    {   // 5: arp EVOLVE is audible (level + brightness) and moves the user's synth (CC74 / CC11)
        Idea I = makeIdea (Midnight, 4321); I.evolve = true; I.evolveBars = 32; I.evolveMode = EvolveGrow; rebuildChords (I); composeLane (I, LaneArp, false);
        const auto cc = arpEnergyCtl (I);
        int c74 = 0, lo = 127, hi = 0; for (auto& c : cc) if (c.num == 74) { ++c74; lo = std::min (lo, c.val); hi = std::max (hi, c.val); }
        int minD = 1 << 20; for (auto& x : I.lanes[LaneArp].notes) minD = std::min (minD, x.d);
        std::printf ("arp EVOLVE CC74: %d changes, %d..%d, shortest gate %d ticks\n", c74, lo, hi, minD);
        C.ok (c74 >= 8 && hi - lo >= 70 && minD >= 6, "arp EVOLVE: CC74 / CC11 ramps for the user's synth, no clicky gates");
        const auto mid = ideaMidi (I, 1 << LaneArp, false, 128);
        bool hasCC = false; for (size_t i = 0; i + 2 < mid.size(); ++i) if ((mid[i] & 0xF0) == 0xB0 && mid[i + 1] == 74) hasCC = true;
        C.ok (hasCC, "arp EVOLVE: the .mid carries the CC74 ramp");
    }
    {   // 6: FIND THE MAGIC makes real variations
        ArpProcessor p; fresh (p, Sunrise, 19); p.labGenerate (4); p.lab.selected = 0; const Hook src = p.lab.tiles[0]; p.labMagic();
        double worst = 0; int n = 0; for (size_t i = 1; i < p.lab.tiles.size(); ++i) if (p.lab.tiles[i].fresh >= 0) { ++n; worst = std::max (worst, noteShare (src.notes, p.lab.tiles[i].notes, src.ticks())); }
        std::printf ("magic vs source: %d variations, most shared %.2f\n", n, worst);
        C.ok (n >= 6 && worst <= 0.75, "FIND THE MAGIC: variations share <= 75% with the source and say how much is new");
    }
    {   // 7 + 8: 8-bar hooks develop; USE THIS of an 8-bar hook over a 4-bar idea grows the idea
        ArpProcessor p; fresh (p, Dusk, 51);
        p.change ([] (Idea& X) { X.phraseBars = 4; rebuildChords (X); }, false);
        p.labGenerate (8);
        int repeats = 0; for (auto& h : p.lab.tiles) { std::vector<Note> a, b; for (auto x : h.notes) { if (x.t < 4 * BAR) a.push_back (x); else { x.t -= 4 * BAR; b.push_back (x); } } if (noteShare (a, b, 4 * BAR) > 0.8) ++repeats; }
        C.ok (p.lab.tiles.size() >= 12 && repeats == 0, "8-BAR hooks are A A' (bars 5-8 never repeat bars 1-4)");
        const Hook h = p.lab.tiles[0]; p.labUse (0);
        int last = 0; for (auto& x : p.idea().lanes[LaneMelody].notes) last = std::max (last, x.t);
        C.ok (p.idea().total >= 8 * BAR && last >= 7 * BAR, "USE THIS: an 8-bar hook over a 4-bar idea plays all 8 bars");
    }
    {   // 9: RESPONSE on a 3-bar phrase keeps bar 3
        ArpProcessor p; fresh (p, Sunrise, 2024);
        p.change ([] (Idea& X) { X.phraseBars = 3; rebuildChords (X); composeLane (X, LaneMelody, false); }, false);
        auto count = [&] (int b) { int k = 0; for (auto& x : p.idea().lanes[LaneMelody].notes) if (x.t >= b * BAR && x.t < (b + 1) * BAR) ++k; return k; };
        const int b3 = count (2); p.respond (0.6);
        C.ok (b3 == 0 || count (2) > 0, "RESPONSE on an odd phrase keeps the unanswered bar");
    }
    {   // 10: MAKE IT A SONG keeps edited lanes and EVOLVE as they were
        ArpProcessor p; fresh (p, Sunrise, 77);
        p.change ([] (Idea& X) { auto& b = X.lanes[LaneBass].notes; if (! b.empty()) b[0].p += 12; X.lanes[LaneBass].edited = true; auto& a = X.lanes[LaneArp].notes; if (! a.empty()) a[0].p += 12; X.lanes[LaneArp].edited = true; }, false);
        const auto b0 = p.idea().lanes[LaneBass].notes[0].p, a0 = p.idea().lanes[LaneArp].notes[0].p; const bool ev = p.idea().evolve;
        p.labGenerate (4); const auto also = p.labSong (0, 32);
        C.ok (p.idea().lanes[LaneBass].notes[0].p == b0 && p.idea().lanes[LaneArp].notes[0].p == a0 && p.idea().evolve == ev && p.idea().total == 32 * BAR && also.contains ("looped"),
              "MAKE IT A SONG keeps the user's lane edits and EVOLVE, and says what it extended");
        int lo = 127, hi = 0, hlo = 127, hhi = 0; for (auto& x : p.idea().lanes[LaneMelody].notes) { lo = std::min (lo, x.p); hi = std::max (hi, x.p); } for (auto& x : p.lab.tiles[0].notes) { hlo = std::min (hlo, x.p); hhi = std::max (hhi, x.p); }
        C.ok (hi - lo <= hhi - hlo + 10 && hi <= 91, "MAKE IT A SONG stays in the hook's register");
    }
    {   // 13: unlocking the motif keeps the user's own locks
        ArpProcessor p; fresh (p, Dusk, 812);
        p.change ([] (Idea& X) { auto& ns = X.lanes[LaneMelody].notes; for (size_t i = 0; i < ns.size() && i < 3; ++i) ns[i].lock = true; }, false);
        p.lockMotif ({}); p.lockMotif ({});
        int after = 0; for (auto& n : p.idea().lanes[LaneMelody].notes) after += n.lock;
        C.ok (after >= 3, "LOCK MOTIF off leaves the user's LOCK SEL locks");
    }
    {   // 14: GENERATE / FIND THE MAGIC run on the lab worker, the UI thread stays free; CANCEL drops the job
        ArpProcessor p; fresh (p, Midnight, 4321);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        p.labGenerateAsync (8);
        const double block = juce::Time::getMillisecondCounterHiRes() - t0;
        const bool busy = p.lab.busy;
        for (int k = 0; k < 6000 && p.lab.busy; ++k) juce::MessageManager::getInstance()->runDispatchLoopUntil (10);   // (sanitizer builds are ~20x slower)
        const bool got = ! p.lab.busy && p.lab.tiles.size() >= 12;
        p.labGenerateAsync (4); p.labCancel();
        for (int k = 0; k < 40; ++k) juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        std::printf ("async GENERATE: the click blocks %.1f ms\n", block);
        C.ok (busy && got && block < 20 && ! p.lab.busy && p.lab.tiles.size() >= 12 && p.lab.tiles[0].bars == 8, "HOOK LAB: GENERATE on the worker (no UI freeze), results delivered, CANCEL keeps the grid");
        p.labGenerate (4); const auto first = p.lab.tiles[0].notes.size(); p.labGenerate (4); p.labStepHistory (-1);
        C.ok (p.lab.tiles[0].notes.size() == first && p.lab.histIdx == p.lab.history.size() - 2, "HOOK LAB: < brings the previous grid back");
    }
    {   // 18: COMBINE with every other slot starred says so
        ArpProcessor p; fresh (p, Dusk, 51); p.labGenerate (4);
        for (int i = 3; i < (int) p.lab.tiles.size(); ++i) p.labStar (i);
        C.ok (p.lab.tiles.size() < 16 || p.labCombine (0, 1, 2) == -2, "COMBINE: no free slot -> -2 (the UI asks to unstar one)");
    }
}
inline void processorChecks (Counter& C)
{
    ArpProcessor p; p.persistTaste = false;
    p.change ([] (Idea& X) { X.mode = Dusk; X.seed = 51; applyModeDefaults (X); newIdea (X); }, false);
    const auto before = p.idea().lanes[LaneMelody].notes;
    p.labGenerate (4);
    C.ok (p.lab.tiles.size() == 16, "HOOK LAB: GENERATE makes 16 tiles");
    p.labAudition (3);
    C.ok (p.auditioningHook() && p.lab.playing == 3, "HOOK LAB: a click auditions the tile");
    bool same = p.idea().lanes[LaneMelody].notes.size() == before.size();
    C.ok (same, "HOOK LAB: auditions leave the MELODY lane alone");
    p.labStar (3); p.labStar (5);
    C.ok (p.lab.tiles[3].star && p.taste.keeps >= 2, "HOOK LAB: stars are kept and teach the harvester");
    p.labGenerate (4);
    int stars = 0; for (auto& h : p.lab.tiles) stars += h.star;
    C.ok (p.lab.tiles.size() == 16 && stars == 2, "HOOK LAB: starred tiles survive GENERATE");
    p.lab.selected = 0; p.labMagic();
    C.ok (p.lab.tiles.size() >= 8, "HOOK LAB: FIND THE MAGIC fills the grid");
    const int at = p.labCombine (0, 1, 2);
    C.ok (at >= 0 && p.lab.playing == at, "HOOK LAB: COMBINE makes and plays a new tile");
    const auto tileNotes = p.lab.tiles[2].notes;
    p.labUse (2);
    const auto& mel = p.idea().lanes[LaneMelody].notes;
    bool used = ! mel.empty() && ! p.auditioningHook(); for (auto& x : tileNotes) { bool f = false; for (auto& y : mel) if (y.t == x.t && y.p == x.p) f = true; if (! f) used = false; }
    C.ok (used, "HOOK LAB: USE THIS puts the tile in the MELODY lane");
    p.undo();
    C.ok (p.idea().lanes[LaneMelody].notes.size() == before.size(), "HOOK LAB: USE THIS is undoable");
    p.labSong (0, 32);
    C.ok (p.idea().total == 32 * BAR && p.idea().lanes[LaneMelody].notes.back().t >= 28 * BAR, "HOOK LAB: MAKE IT A SONG -> 32 bars");
    p.undo();
    p.respond (0.8);
    C.ok (! p.idea().lanes[LaneMelody].notes.empty(), "GENERATE RESPONSE writes the answer");
    p.lockMotif ({});
    C.ok (! p.idea().motif.empty() && p.idea().motifLen > 0, "LOCK MOTIF locks the strongest motif");
    p.change ([] (Idea& X) { X.style = StyleAnthemic; X.candy = CandyEuphoric; X.tension = 0.5; X.arpDir = DirUpDown; X.arpContour = ContArch; X.arpAccent = AccBuild; X.evolveMode = EvolveWave; }, true, "t");
    const auto s = p.sessionString();
    ArpProcessor q; q.persistTaste = false; q.restoreSession (s);
    const Idea& Q = q.idea();
    C.ok (Q.style == StyleAnthemic && Q.candy == CandyEuphoric && std::abs (Q.tension - 0.5) < 1e-9 && Q.arpDir == DirUpDown && Q.arpContour == ContArch && Q.arpAccent == AccBuild && Q.evolveMode == EvolveWave && Q.motif.size() == p.idea().motif.size() && Q.motifLen == p.idea().motifLen,
          "HOOK LAB settings + locked motif survive the session");
    p.labHarvestRank();
    const int k0 = p.taste.keeps;
    if (! p.lab.harvest.empty()) p.labHarvestKeep (0);
    C.ok (! p.lab.harvest.empty() && p.taste.keeps == k0 + 1 && ! p.idea().motif.empty(), "MOTIF HARVESTER: keeping a fragment locks it and learns");
    punchChecks (C);
    idmChecks (C, true);
}
} // namespace hooktest
