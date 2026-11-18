#include "Composer.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>

namespace arp
{
void realizeRecipeArp (std::vector<Note>& out, const Figure& base, const std::vector<FigEvent>* events, const Harmony& H, const ArpParams& P, int t0, int t1);
ArpResult composeRecipeArp (const Harmony& H, const ArpParams& P, int totalTicks);
std::vector<Note> composeRecipeHook (const Harmony& H, const MelodyParams& P, int phraseTicks);
namespace
{
bool has (const std::vector<int>& v, int x) { return std::find (v.begin(), v.end(), x) != v.end(); }
int irand (Rng& R, int n) { return n <= 0 ? 0 : std::min (n - 1, (int) std::floor (R() * n)); }
template <typename T> const T& pickV (Rng& R, const std::vector<T>& v) { return v[(size_t) irand (R, (int) v.size())]; }
int pickWeighted (Rng& R, const std::vector<double>& w)
{
    double t = 0; for (double x : w) t += std::max (0.0, x);
    double u = R() * t;
    for (size_t i = 0; i < w.size(); ++i) { u -= std::max (0.0, w[i]); if (u < 0) return (int) i; }
    return 0;
}
int barOf (int t) { return t / BAR; }
bool barLocked (const std::vector<bool>& L, int t) { const int b = barOf (t); return b >= 0 && b < (int) L.size() && L[(size_t) b]; }
} // namespace

// ---------------------------------------------------------------- recipe defaults
Recipe::Recipe()
{
    groupings16 = { { 3, 3, 2, 3, 3, 2 }, { 3, 3, 3, 3, 4 }, { 3, 3, 4, 3, 3 }, { 4, 3, 3, 3, 3 }, { 2, 3, 3, 2, 3, 3 }, { 3, 3, 2, 2, 2, 4 }, { 4, 4, 4, 4 }, { 3, 5, 3, 5 } };
    groupings8 = { { 3, 3, 2 }, { 2, 2, 2, 2 }, { 3, 2, 3 }, { 2, 3, 3 }, { 4, 4 } };
    groupings24 = { { 3, 3, 3, 3, 3, 3, 3, 3 }, { 6, 6, 6, 6 }, { 6, 3, 3, 6, 6 }, { 3, 3, 6, 3, 3, 6 } };
    sunriseHalves = { { 3, 3, 2 }, { 3, 3, 2 }, { 2, 3, 3 }, { 3, 2, 3 }, { 4, 2, 2 }, { 2, 2, 4 } };
    // lane mutes [CHORDS, ARP, MELODY, BASS] (2026-11-13: every mood opens with CHORDS + MELODY + BASS playing, ARP muted)
    ModeRecipe& S = mode[Sunrise];
    S.label = "SUNRISE"; S.words = "euphoric happy progressive 4 bars"; S.scales = { "major" };
    S.progs = { "I V vi IV", "vi IV I V", "IV I V vi", "I vi IV V", "IV V vi I", "I IV vi V" };
    S.chordBars = 1; S.voicing = 0; S.chordRhythm = 2; S.phraseBars = 8; S.form = 0; S.staggerTicks = 2 * PPQ; S.stutter = 0.35;
    S.arpRange = 2; S.melRange = 2; S.mute[0] = false; S.mute[1] = true; S.mute[2] = false; S.mute[3] = false; S.sound = "piano"; S.stabs = true;
    S.cycleW = { 1, 3, 1, 2, 0.5, 2 }; S.ruleW = { 1, 1, 3, 2, 2, 2 }; S.groupW = { 3, 2, 2, 1, 2, 2, 1.5, 1 };
    S.stepTarget = 0.6; S.antP = 0.6; S.susP = 0.35;
    ModeRecipe& D = mode[Dusk];
    D.label = "DUSK"; D.words = "bittersweet euphoric progressive 4 bars"; D.scales = { "major", "minor" };
    D.progs = { "vi IV I V", "IV iv I I", "vi V IV V", "vi IV V iii", "IV V iii vi", "I iii vi IV", "i III VII VI", "i VI III VII", "VI VII i i", "iv VI i VII" };
    D.chordBars = 1; D.voicing = 0; D.chordRhythm = 0; D.phraseBars = 8; D.form = 1; D.staggerTicks = 0; D.stutter = 0.15;
    D.mute[0] = false; D.mute[1] = true; D.mute[2] = false; D.mute[3] = false; D.sound = "keys"; D.stabs = false;
    D.cycleW = { 1.5, 2, 1, 2.5, 1, 1 }; D.ruleW = { 2, 2, 2, 2, 1.5, 1.5 }; D.groupW = { 3, 2, 1.5, 1, 1, 1, 1.5, 1 };
    D.stepTarget = 0.5; D.antP = 0.45; D.susP = 0.3;
    ModeRecipe& M = mode[Midnight];
    M.label = "MIDNIGHT"; M.words = "dark hypnotic progressive 4 bars"; M.scales = { "minor", "dorian" }; M.progBias = 0;
    M.chordBars = 2; M.voicing = 0; M.chordRhythm = 0; M.phraseBars = 0; M.form = 0; M.staggerTicks = 0; M.stutter = 0;
    M.mute[0] = false; M.mute[1] = true; M.mute[2] = false; M.mute[3] = false; M.sound = "neon"; M.stabs = false;
    M.cycleW = { 1.5, 0.8, 2, 3.5, 2, 0.8 }; M.ruleW = { 3.5, 3, 1, 1.2, 2, 1 }; M.groupW = { 2, 3, 1, 1, 1, 1, 2, 1 };
    M.stepTarget = 0.45; M.antP = 0.2; M.susP = 0.15;
    // TRAP / HIP HOP: their own engines (UrbanMoods.cpp) set chords, tempo and every lane; the recipe only names them + lane mutes
    ModeRecipe& T = mode[Trap];
    T.label = "TRAP"; T.words = "dark tense 4 bars"; T.progBias = 0; T.chordBars = 1; T.mute[0] = false; T.mute[1] = true; T.mute[2] = false; T.mute[3] = false; T.sound = "flute";
    ModeRecipe& H = mode[HipHop];
    H.label = "HIP HOP"; H.words = "chill sad 4 bars"; H.progBias = 0; H.chordBars = 1; H.mute[0] = false; H.mute[1] = true; H.mute[2] = false; H.mute[3] = false; H.sound = "keys";
}
static Recipe& recipeRef() { static Recipe r; return r; }
const Recipe& recipe() { return recipeRef(); }
void setRecipe (const Recipe& r) { recipeRef() = r; }
static const ModeRecipe& modeR (int mood) { return recipe().mode[clampv (mood, 0, kMoods - 1)]; }

// ---------------------------------------------------------------- notes
float playVel (const std::vector<Note>& notes, size_t i)
{
    const Link* pl = i > 0 ? &notes[i - 1].l : nullptr;
    return clampv (notes[i].v * (pl && pl->type == 2 ? 1.0f + 0.7f * pl->f : 1.0f), 0.05f, 1.0f);
}
void sortNotes (std::vector<Note>& n) { std::stable_sort (n.begin(), n.end(), [] (const Note& a, const Note& b) { return a.t != b.t ? a.t < b.t : a.p < b.p; }); }
std::vector<MidiNote> laneMidiNotes (const std::vector<Note>& notes, int loopTicks)
{
    std::vector<MidiNote> out;
    for (size_t i = 0; i < notes.size(); ++i)
    {
        const Note& n = notes[i]; int d = n.d;
        if (i + 1 < notes.size() && n.l.type == 1)
        {   // GLIDE: legato overlap into the next note (longer for an earlier glide start), never into the same pitch
            const Note& b = notes[i + 1]; const int gap = std::max (1, b.t - n.t);
            const int ov = (int) clampv (jsRound ((1.0 - n.l.w) * gap * 0.5), 6.0, 48.0);
            d = b.p == n.p ? std::min (d, gap - 1) : std::max (d, gap + ov);
        }
        d = std::max (1, std::min (d, loopTicks - n.t));
        double v = n.v;
        if (i > 0 && notes[i - 1].l.type == 2) v = n.v + 40.0 / 127.0 * notes[i - 1].l.f;   // FATTEN: the next note up to +40 velocity
        out.push_back ({ n.t, d, n.p, clampv (v, 1.0 / 127.0, 1.0) });
    }
    return out;
}
std::vector<MidiCtl> laneMidiCtl (const std::vector<Note>& notes, int loopTicks, bool bend, int bendRange)
{
    std::vector<MidiCtl> c; bool any = false;
    bendRange = std::max (1, bendRange);
    for (size_t i = 0; i + 1 < notes.size(); ++i)
    {
        const Note& a = notes[i]; const Note& b = notes[i + 1];
        if (b.t >= loopTicks || b.t <= a.t) continue;
        if (a.l.type == 1 && bend && a.p != b.p)
        {   // GLIDE as pitch bend: ramp from the first note to the second over the glide part, back to centre on the next note
            const double gs = clampv ((double) a.t + a.l.w * (double) (b.t - a.t), a.t + 1.0, b.t - 2.0);
            const double tgt = clampv ((double) (b.p - a.p) / bendRange, -1.0, 1.0);
            for (int k = 0; k <= 8; ++k) { const double u = k / 8.0; c.push_back ({ (int) jsRound (gs + (b.t - 1 - gs) * u), 0, 0, (int) jsRound (8192 + (tgt > 0 ? 8191 : 8192) * tgt * u) }); }
            c.push_back ({ b.t, 0, 0, 8192 }); any = true;
        }
        else if (a.l.type == 2)
        {   // FATTEN: an expression (CC11) swell over the line
            for (int k = 0; k <= 6; ++k) { const double u = k / 6.0; c.push_back ({ (int) jsRound (a.t + (b.t - a.t) * u), 1, 11, (int) jsRound (92 + 35 * u) }); }
            any = true;
        }
    }
    if (any)
    {   // start clean: bend centre, expression full, bend range (RPN 0) for synths that read it
        std::vector<MidiCtl> head { { 0, 1, 101, 0 }, { 0, 1, 100, 0 }, { 0, 1, 6, bendRange }, { 0, 1, 38, 0 }, { 0, 1, 101, 127 }, { 0, 1, 100, 127 }, { 0, 0, 0, 8192 }, { 0, 1, 11, 127 } };
        c.insert (c.begin(), head.begin(), head.end());
    }
    return c;
}

// ---------------------------------------------------------------- harmony
const HChord& Harmony::at (int t) const
{
    static const HChord none;
    if (chords.empty()) return none;
    const int L = std::max (1, loopTicks), tt = (((t + offset) % L) + L) % L;
    for (auto& c : chords) if (tt >= c.t && tt < c.tEnd) return c;
    return chords.back();
}
const HChord& Harmony::next (int t) const
{
    static const HChord none;
    if (chords.empty()) return none;
    const int L = std::max (1, loopTicks), tt = (((t + offset) % L) + L) % L;
    for (size_t i = 0; i < chords.size(); ++i) if (tt >= chords[i].t && tt < chords[i].tEnd) return chords[(i + 1) % chords.size()];
    return chords[0];
}
bool Harmony::chordTone (int p, int t) const { return has (at (t).pcs, mod12 (p)); }
bool Harmony::scaleTone (int p, int t) const { const auto& c = at (t); return has (c.scale, mod12 (p)) || has (c.pcs, mod12 (p)); }
bool Harmony::clash (int p, int t) const
{
    const auto& c = at (t); const int pc = mod12 (p);
    if (has (c.pcs, pc)) return false;
    for (int q : c.pcs) if (mod12 (pc - q) == 1) return true;   // a semitone above a chord tone (b9, 4 over 3, b6 over 5)
    return false;
}

Harmony makeHarmony (int key, const std::string& scale, const std::vector<Resolved>& chords, int voicingMode)
{
    Harmony H; H.key = key; H.scaleName = scale;
    for (int s : scaleOf (scale)) H.keyPcs.push_back (mod12 (key + s));
    for (auto& c : chords)
    {
        HChord h; h.t = c.t; h.tEnd = c.tEnd; h.root = mod12 (key + c.root); h.bassNote = c.bassNote; h.name = c.name;
        for (int p : c.pcs) if (! has (h.pcs, p)) h.pcs.push_back (p);
        h.voicing = c.voicing;
        if (voicingMode == 1)   // POWER: root, 5th, octave (voicing 3 = the web version's own voicing)
        {
            const int v0 = c.voicing.empty() ? 52 : c.voicing.front();
            int r = v0 + 5; while (mod12 (r) != h.root) --r;
            h.voicing = { r, r + 7, r + 12 }; h.pcs = { h.root, mod12 (h.root + 7) };
            h.name = noteName (h.root, key, scale) + "5";
        }
        else if (voicingMode == 2 && c.voicing.size() >= 3)   // OPEN: lift every second inner voice an octave
        {
            std::vector<int> v = c.voicing;
            for (size_t i = 1; i + 1 < v.size(); i += 2) v[i] += 12;
            std::sort (v.begin(), v.end()); h.voicing = v;
        }
        std::vector<int> sc;
        if (! c.cs.empty()) for (int s : c.cs) sc.push_back (mod12 (key + s)); else sc = H.keyPcs;
        for (int p : h.pcs) if (! has (sc, p)) { sc.erase (std::remove_if (sc.begin(), sc.end(), [p] (int q) { return mod12 (q - p) == 1 || mod12 (p - q) == 1; }), sc.end()); sc.push_back (p); }
        std::sort (sc.begin(), sc.end()); sc.erase (std::unique (sc.begin(), sc.end()), sc.end());
        h.scale = sc;
        H.chords.push_back (h);
    }
    H.loopTicks = chords.empty() ? BAR * 4 : chords.back().tEnd;
    if (voicingMode == 0)
    {   // SMOOTH: voice-led in MIDI 52-76 around 63, top note held when it can be (the composer's voice_chords)
        auto movement = [] (std::vector<int> a, std::vector<int> b) {
            std::sort (a.begin(), a.end()); std::sort (b.begin(), b.end());
            if (a.size() == b.size()) { double s = 0; for (size_t i = 0; i < a.size(); ++i) s += std::abs (a[i] - b[i]); return s; }
            const auto& sm = a.size() < b.size() ? a : b; const auto& bg = a.size() < b.size() ? b : a; double tot = 0;
            for (int x : sm) { int m = 99; for (int y : bg) m = std::min (m, std::abs (x - y)); tot += m; }
            for (int y : bg) if (std::find (sm.begin(), sm.end(), y) == sm.end()) { int m = 99; for (int x : sm) m = std::min (m, std::abs (x - y)); tot += m * 0.5; }
            return tot; };
        std::vector<int> prev;
        for (auto& h : H.chords)
        {
            std::vector<int> pcs; for (int p : h.pcs) if (std::find (pcs.begin(), pcs.end(), p) == pcs.end()) pcs.push_back (p);
            if (pcs.size() > 5) pcs.resize (5);
            std::vector<std::vector<int>> opts; for (int pc : pcs) { std::vector<int> o; for (int p = 52; p <= 76; ++p) if (mod12 (p) == pc) o.push_back (p); opts.push_back (o); }
            std::vector<int> best; double bc = 1e9; std::vector<size_t> idx (opts.size(), 0);
            while (true)
            {
                std::vector<int> v; for (size_t i = 0; i < opts.size(); ++i) v.push_back (opts[i][idx[i]]);
                std::sort (v.begin(), v.end());
                bool ok = v.back() - v.front() <= 19;
                for (size_t i = 1; ok && i < v.size(); ++i) if (v[i] - v[i - 1] < 2) ok = false;
                if (ok && v.size() > 1 && v[1] - v[0] < 3 && v[0] < 57) ok = false;
                if (ok)
                {
                    double mean = 0; for (int x : v) mean += x; mean /= (double) v.size();
                    double cost = std::abs (mean - 63) * 0.6;
                    if (! prev.empty()) { cost += movement (prev, v); if (v.back() == prev.back()) cost -= 1.5; }
                    if (cost < bc) { bc = cost; best = v; }
                }
                size_t k = 0; while (k < idx.size() && ++idx[k] >= opts[k].size()) { idx[k] = 0; ++k; }
                if (k >= idx.size()) break;
            }
            if (best.empty()) { for (auto& o : opts) best.push_back (o[o.size() / 2]); std::sort (best.begin(), best.end()); }
            h.voicing = best; prev = best;
        }
    }
    return H;
}

int snapToKey (int p, int t, const Harmony& H, int dir)
{
    auto ok = [&] (int q) { return has (H.keyPcs, mod12 (q)) || H.chordTone (q, t); };
    if (ok (p)) return p;
    for (int k = 1; k < 7; ++k) { const int a = p + (dir >= 0 ? k : -k), b = p - (dir >= 0 ? k : -k); if (ok (a)) return a; if (ok (b)) return b; }
    return p;
}

namespace
{
// pitches of the chord-scale lattice / chord tones in a range
std::vector<int> lattice (const HChord& c, int lo, int hi)
{
    std::vector<int> o;
    for (int p = lo; p <= hi; ++p) if (has (c.scale, mod12 (p)) || has (c.pcs, mod12 (p))) o.push_back (p);
    return o;
}
std::vector<int> chordTones (const HChord& c, int lo, int hi)
{
    std::vector<int> o; for (int p = lo; p <= hi; ++p) if (has (c.pcs, mod12 (p))) o.push_back (p);
    return o;
}
int nearestIn (const std::vector<int>& v, int p, int dirPref = 0)
{
    if (v.empty()) return p;
    int best = v[0], bd = 999;
    for (int q : v)
    {
        const int d = std::abs (q - p) * 2 + (dirPref != 0 && (q - p) * dirPref < 0 ? 1 : 0);
        if (d < bd) { bd = d; best = q; }
    }
    return best;
}
int latticeMove (const HChord& c, int p, int steps)
{
    auto L = lattice (c, p - 30, p + 30);
    int i = 0, bd = 999; for (size_t k = 0; k < L.size(); ++k) if (std::abs (L[k] - p) < bd) { bd = std::abs (L[k] - p); i = (int) k; }
    i = clampv (i + steps, 0, (int) L.size() - 1);
    return L.empty() ? p : L[(size_t) i];
}
int nearestChordTone (const Harmony& H, int p, int t, int dirPref = 0) { return nearestIn (chordTones (H.at (t), p - 12, p + 12), p, dirPref); }
int nearestScaleTone (const Harmony& H, int p, int t, int dirPref = 0) { return nearestIn (lattice (H.at (t), p - 12, p + 12), p, dirPref); }
bool strongPos (int t, int spb) { (void) spb; return t % PPQ == 0; }
int clampPitch (int p) { while (p > 108) p -= 12; while (p < 24) p += 12; return p; }
} // namespace

// ---------------------------------------------------------------- taste
Taste taste (const std::vector<Note>& notes, const Harmony& H, int mood, bool pedalMode, int totalTicks)
{
    Taste T; const size_t n = notes.size();
    if (n < 2) { T.score = -20; return T; }
    std::map<int, int> cnt; for (auto& x : notes) cnt[x.p]++;
    int topP = notes[0].p, topC = 0; for (auto& kv : cnt) if (kv.second > topC) { topC = kv.second; topP = kv.first; }
    T.topShare = (double) topC / (double) n;
    const bool dropPedal = pedalMode && T.topShare > 0.22;
    std::vector<int> line; for (auto& x : notes) if (! (dropPedal && x.p == topP)) line.push_back (x.p);
    if (line.size() < 2) line.clear(), [&] { for (auto& x : notes) line.push_back (x.p); }();
    int big = 0, octs = 0, steps = 0, reps = 0, dirCh = 0, lastDir = 0;
    for (size_t i = 1; i < line.size(); ++i)
    {
        const int d = line[i] - line[i - 1], ad = std::abs (d);
        if (ad == 0) ++reps; else if (ad <= 2) ++steps; else if (ad == 12) ++octs; else if (ad > 7) ++big;
        const int dir = d > 0 ? 1 : d < 0 ? -1 : 0;
        if (dir != 0) { if (lastDir != 0 && dir != lastDir) ++dirCh; lastDir = dir; }
    }
    int clashes = 0, weakStrong = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const Note& x = notes[i]; const bool strong = x.t % PPQ == 0;
        if (H.clash (x.p, x.t) && (strong || x.d >= PPQ / 2))
        {
            bool resolved = false;   // a suspension that steps down into a chord tone soon after is fine
            if (i + 1 < n && notes[i + 1].t - x.t <= PPQ && H.chordTone (notes[i + 1].p, notes[i + 1].t) && (x.p - notes[i + 1].p == 1 || x.p - notes[i + 1].p == 2)) resolved = true;
            if (! resolved) ++clashes;
        }
        else if (strong && ! H.chordTone (x.p, x.t) && ! (dropPedal && x.p == topP)) ++weakStrong;
    }
    int lo = 999, hi = -999; for (auto& x : notes) { lo = std::min (lo, x.p); hi = std::max (hi, x.p); }
    T.range = hi - lo; T.bigLeaps = big; T.clashes = clashes;
    const double per = std::max (1.0, (double) n / 16.0);
    const double L1 = std::max (1.0, (double) line.size() - 1);
    T.stepRatio = steps / L1;
    double s = 10;
    const Recipe& W = recipe();
    s -= W.wBigLeap * big / per;
    s -= W.wOct * std::max (0.0, octs - 2 * per);
    s -= W.wClash * clashes / per;
    s -= W.wWeakStrong * weakStrong / per;
    if (T.range < 5) s -= (5 - T.range) * W.wNarrow;
    if (T.range > 26) s -= (T.range - 26) * 0.5;
    if (! pedalMode && T.topShare > 0.4) s -= (T.topShare - 0.4) * W.wTopShare;
    const double rr = reps / L1; if (! pedalMode && rr > 0.3) s -= (rr - 0.3) * W.wRep;
    const double target = modeR (mood).stepTarget;
    s -= std::abs (T.stepRatio - target) * W.wStep;
    const double dc = line.size() > 2 ? dirCh / (double) (line.size() - 2) : 0.5;
    s -= std::max (0.0, dc - 0.72) * W.wDir + std::max (0.0, 0.15 - dc) * W.wDir;
    // flat bars
    const int nb = std::max (1, totalTicks / BAR);
    for (int b = 0; b < nb; ++b)
    {
        std::set<int> ps; for (auto& x : notes) if (x.t >= b * BAR && x.t < (b + 1) * BAR) ps.insert (x.p);
        if (! pedalMode && ps.size() == 1) s -= W.wFlatBar;
    }
    const int dl = std::abs (notes.back().p - notes.front().p);
    if (dl <= 2) s += W.wLoop; else if (dl > 9) s -= W.wLoop;
    // motif repetition: distinct interval 3-grams
    if (line.size() > 6)
    {
        std::set<long> g; int tot = 0;
        for (size_t i = 3; i < line.size(); ++i) { g.insert ((long) (line[i - 2] - line[i - 3] + 50) * 10000 + (line[i - 1] - line[i - 2] + 50) * 100 + (line[i] - line[i - 1] + 50)); ++tot; }
        const double ratio = (double) g.size() / tot;
        if (ratio < 0.12) s -= W.wStatic;
        if (ratio > 0.8) s -= (ratio - 0.8) * W.wRandom;
    }
    T.score = s;
    return T;
}

std::vector<Note> applyGroove (const std::vector<Note>& notes, double groove, int spb, uint32_t seed)
{
    if (groove <= 0.0001) return notes;
    std::vector<Note> o = notes; Rng R (seed ^ 0x6a09e667u);
    const int st = BAR / std::max (1, spb);
    for (auto& n : o)
    {
        if ((n.t / st) % 2 == 1 && spb != 24) n.t += (int) std::lround (groove * st * 0.33);
        n.v = clampv (n.v * (float) (1.0 + (R() - 0.5) * 0.3 * groove), 0.08f, 1.0f);
    }
    return o;
}

// ================================================================= ARP figures
int ladderSize (const ArpParams& P) { return 3 * clampv (P.range, 1, 3) + 1; }

namespace
{
struct LadderCtx { int lo, hi; };
LadderCtx ladderRange (const ArpParams& P)
{
    const int r = clampv (P.range, 1, 3), span = 12 * r, center = 72 + 12 * P.reg;
    return { center - span / 2, center - span / 2 + span };
}
std::vector<int> ladderFor (const HChord& c, const ArpParams& P)
{
    const auto R = ladderRange (P);
    auto v = chordTones (c, R.lo, R.hi);
    if (v.size() < 2) v = chordTones (c, R.lo - 6, R.hi + 6);
    if (v.empty()) v.push_back (R.lo);
    return v;
}
int ladderPitch (const std::vector<int>& L, int idx)
{
    const int n = (int) L.size(), i = idx - 1;
    const int oct = (int) std::floor ((double) i / n), k = ((i % n) + n) % n;
    return L[(size_t) k] + 12 * oct;
}
int pedalPitchFor (const HChord& c, const ArpParams& P, int pedalPc, bool hold, int refTop)
{
    // the pedal sits on (or just above) the figure's own top note, so it never leaps far from the other notes
    int p = refTop; { int best = refTop, bd = 99; for (int q = refTop - 2; q <= refTop + 9; ++q) if (mod12 (q) == mod12 (pedalPc)) { const int d = std::abs (q - (refTop + 2)); if (d < bd) { bd = d; best = q; } } p = best; }
    const bool fits = has (c.pcs, mod12 (p)) || (has (c.scale, mod12 (p)) && [&] { for (int q : c.pcs) if (mod12 (p - q) == 1) return false; return true; }());
    if (hold && fits) return p;
    (void) P;
    return nearestIn (chordTones (c, p - 7, p + 7), p, -1);
}
int resyncTicks (const ArpParams& P, const Harmony& H)
{
    if (P.resync < 0) return 1 << 30;
    if (P.resync > 0) return P.resync * BAR;
    return H.loopTicks >= 4 * BAR ? 4 * BAR : std::max (BAR, H.loopTicks);
}
void realizeRange (std::vector<Note>& out, const Figure& f, const Harmony& H, const ArpParams& P, int t0, int t1, int pedalPc, int resync)
{
    const int st = BAR / std::max (1, P.spb), len = clampv (f.len, 1, 8);
    int refTop = -1;   // the figure's highest ladder note on the first chord
    if (! H.chords.empty()) { const auto L0 = ladderFor (H.chords[0], P); for (int i = 0; i < len; ++i) { const auto& s = f.s[(size_t) i]; if (! s.pedal && ! s.rest) refTop = std::max (refTop, ladderPitch (L0, s.idx) + 12 * s.oct); } if (refTop < 0) refTop = ladderPitch (L0, ladderSize (P)); }
    else refTop = 76;
    int sync = t0;
    for (int t = t0; t < t1; t += st)
    {
        if (t > t0 && (t - t0) % resync == 0) sync = t;
        const int phase = ((t - sync) / st) % len;
        const Step& s = f.s[(size_t) phase];
        if (s.rest) continue;
        const HChord& c = H.at (t);
        int p = s.pedal ? pedalPitchFor (c, P, pedalPc, P.holdPedal, refTop) : ladderPitch (ladderFor (c, P), s.idx);
        p = clampPitch (p + 12 * s.oct);
        Note n; n.t = t; n.p = p;
        n.v = phase == 0 ? 0.9f : s.acc ? 0.84f : 0.64f;
        n.acc = phase == 0 || s.acc;
        n.d = s.tie ? st : std::max (6, (int) std::lround (st * P.gate));
        // legato tie at each phrase end (the last step before a re-sync or the loop end)
        const int nt = t + st;
        if (nt >= t1 || (nt - t0) % resync == 0) n.d = st;
        out.push_back (n);
    }
}
int pickLen (Rng& R, const ArpParams& P)
{
    if (P.cycle >= 3 && P.cycle <= 8) return P.cycle;
    const double m = P.master;
    std::vector<double> w = modeR (P.mood).cycleW; w.resize (6, 0.5);   // 3 4 5 6 7 8
    for (size_t i = 0; i < w.size(); ++i) { const int k = (int) i + 3; if (k <= 4) w[i] *= 1.4 - m; else if (k >= 6) w[i] *= 0.6 + m; }
    return 3 + pickWeighted (R, w);
}
Figure makeFigure (Rng& R, const ArpParams& P, int rule)
{
    Figure f; f.len = pickLen (R, P); f.rule = rule;
    const int N = ladderSize (P), k = f.len;
    for (auto& s : f.s) s = Step();
    auto setIdx = [&] (int i, int v) { f.s[(size_t) i].idx = clampv (v, 1, N + 2); };
    switch (rule)
    {
        case 0:   // repeated high index with a dip and a top-out
        {
            const int h = clampv (N - 1 - irand (R, 2), 2, N);
            for (int i = 0; i < k; ++i) setIdx (i, h);
            int dip = 1 + irand (R, k - 1), top = k - 1 - irand (R, std::min (2, k - 1));
            if (top == dip) top = (dip + 1 < k) ? dip + 1 : std::max (1, dip - 1);
            setIdx (dip, h - 1 - irand (R, 2)); setIdx (top, h + 1); f.s[(size_t) top].acc = R() < 0.6;
            if (k >= 6 && R() < 0.5) { int d2 = (dip + k / 2) % k; if (d2 != 0 && d2 != top) setIdx (d2, h - 1); }
            if (P.holdPedal && P.mood >= Sad && R() < 0.45) for (int i = 0; i < k; ++i) if (f.s[(size_t) i].idx == h) f.s[(size_t) i].pedal = true;
            break;
        }
        case 1:   // pedal on top + lower notes walking just under it (no wide leaps)
        {
            int v = clampv (N - 1 - irand (R, 2), 1, N), dir = R() < 0.5 ? 1 : -1; const int lo = std::max (1, N - 3);
            for (int i = 0; i < k; ++i)
            {
                const bool ped = i == 0 || (k >= 6 && i == k / 2) || (k == 4 && i == 2 && R() < 0.5);
                if (ped) { f.s[(size_t) i].pedal = true; f.s[(size_t) i].idx = N; continue; }
                setIdx (i, v); v += dir; if (v > N - 1 || v < lo) { dir = -dir; v += 2 * dir; }
            }
            break;
        }
        case 2:   // a run with a turn (spans at most about an octave and a half)
        {
            const int up = R() < 0.65; const int span = std::min (4, std::max (2, k / 2));
            const int b = clampv (N - span - irand (R, 2), 1, N);
            int v = up ? b : b + span, dir = up ? 1 : -1;
            for (int i = 0; i < k; ++i) { setIdx (i, v); v += dir; if (v > b + span || v < b) { dir = -dir; v += 2 * dir; } }
            break;
        }
        case 3:   // broken thirds, folded back into a small window
        {
            const int b = clampv (N - 4 - irand (R, 2), 1, N); int v = b;
            for (int i = 0; i < k; ++i) { setIdx (i, v); v += (i % 2 == 0) ? 2 : -1; if (v > b + 4) v -= 4; }
            break;
        }
        case 4:   // bounce: fixed top, moving notes just below it
        {
            const int h = clampv (N - irand (R, 2), 3, N); int lo = clampv (h - 1 - irand (R, 2), 1, h - 1);
            for (int i = 0; i < k; ++i) { if (i % 2 == 1) setIdx (i, h); else { setIdx (i, lo); lo += R() < 0.5 ? 1 : -1; lo = clampv (lo, std::max (1, h - 3), h - 1); } }
            if (P.holdPedal && P.mood >= Sad && R() < 0.5) for (int i = 1; i < k; i += 2) f.s[(size_t) i].pedal = true;
            break;
        }
        default:  // cascade down four ladder notes, then a pick-up
        {
            const int h = clampv (N - irand (R, 2), 3, N);
            for (int i = 0; i < k; ++i) setIdx (i, h - (i % 4));
            if (k >= 5) setIdx (k - 1, h - 1);
            break;
        }
    }
    // spice by MASTER
    if (P.master < 0.3 && k >= 5 && R() < 0.35) { const int i = 1 + irand (R, k - 1); f.s[(size_t) i].rest = true; }
    if (P.master > 0.55 && R() < (P.master - 0.45)) { const int i = 1 + irand (R, k - 1); if (! f.s[(size_t) i].pedal) f.s[(size_t) i].oct = 1; }
    if (R() < 0.2) f.s[(size_t) (k - 1)].tie = true;
    return f;
}
std::vector<double> ruleWeights (int mood) { auto w = modeR (mood).ruleW; w.resize (6, 1.0); return w; }
double arpScore (const std::vector<Note>& notes, const Figure& f, const Harmony& H, const ArpParams& P, Rng& R)
{
    const bool pedal = std::any_of (f.s.begin(), f.s.begin() + f.len, [] (const Step& s) { return s.pedal; }) || f.rule == 0 || f.rule == 4;
    Taste T = taste (notes, H, P.mood, pedal, H.loopTicks);
    double s = T.score;
    // distinct pitches in the first bar
    std::set<int> d1; for (auto& n : notes) if (n.t < BAR) d1.insert (n.p);
    const int d = (int) d1.size();
    if (d < 2) s -= 10; else if (d == 2) s -= 3; else if (d > 6) s -= 2.0 * (d - 6);
    // smooth chord changes: how many pitches of the bar-sized window change at each chord change
    double chg = 0; int nch = 0;
    for (size_t ci = 1; ci < H.chords.size(); ++ci)
    {
        const int t = H.chords[ci].t; std::multiset<int> a, b;
        for (auto& n : notes) { if (n.t >= t - BAR / 2 && n.t < t) a.insert (n.p); if (n.t >= t && n.t < t + BAR / 2) b.insert (n.p); }
        std::set<int> sa (a.begin(), a.end()), sb (b.begin(), b.end()); int changed = 0;
        for (int x : sb) if (! sa.count (x)) ++changed;
        chg += changed; ++nch;
    }
    if (nch) { const double avg = chg / nch; if (avg > 2) s -= (avg - 2) * 1.5; }
    // a top note that keeps returning = hypnotic
    if (P.mood >= Sad && T.topShare > 0.25) s += 1.5;
    // no wide jumps anywhere in the figure (octave lifts are fine)
    int wide = 0; for (size_t i = 1; i < notes.size(); ++i) { const int a = std::abs (notes[i].p - notes[i - 1].p); if (a > 9 && a != 12) ++wide; }
    s -= 0.8 * wide / std::max (1.0, notes.size() / 16.0);
    s += R() * 0.5;
    return s;
}
} // namespace

int choosePedalPc (const Harmony& H)
{
    double best = -1e9; int bp = H.key;
    for (int pc = 0; pc < 12; ++pc)
    {
        double sc = 0;
        for (auto& c : H.chords)
        {
            const double w = (c.tEnd - c.t) / (double) BAR;
            bool cl = false; for (int q : c.pcs) if (mod12 (pc - q) == 1) cl = true;
            if (has (c.pcs, pc)) sc += 2 * w; else if (has (c.scale, pc) && ! cl) sc += 1 * w; else sc -= 3 * w;
        }
        if (pc == H.key) sc += 0.15;
        if (pc == mod12 (H.key + 7)) sc += 0.1;
        if (sc > best) { best = sc; bp = pc; }
    }
    return bp;
}

std::vector<Note> realizeArp (const Figure& f, const Harmony& H, const ArpParams& P, int totalTicks, int pedalPc, const std::vector<FigEvent>* events)
{
    std::vector<Note> out;
    if (f.recipe) { realizeRecipeArp (out, f, events, H, P, 0, totalTicks); return out; }
    realizeRange (out, f, H, P, 0, totalTicks, pedalPc, resyncTicks (P, H));
    return out;
}

namespace
{
Figure mutateOne (const Figure& f0, Rng& R, const ArpParams& P, int kind)
{
    Figure f = f0; const int N = ladderSize (P), k = f.len;
    int lo = 99, hi = 0; for (int i = 0; i < k; ++i) if (! f.s[(size_t) i].pedal && ! f.s[(size_t) i].rest) { lo = std::min (lo, f.s[(size_t) i].idx); hi = std::max (hi, f.s[(size_t) i].idx); }
    if (lo > hi) { lo = std::max (1, N - 3); hi = N; }
    lo = std::max (1, lo - 1); hi = std::min (N + 1, hi + 1);   // stay inside the figure's own window (+-1 ladder step)
    switch (kind)
    {
        case 0: { int i = 1 + irand (R, std::max (1, k - 1)); for (int tries = 0; tries < 8 && (f.s[(size_t) i].pedal || f.s[(size_t) i].rest); ++tries) i = 1 + irand (R, std::max (1, k - 1));
                  auto& s = f.s[(size_t) i]; if (s.pedal) break; s.idx = clampv (s.idx + (R() < 0.5 ? -1 : 1), lo, hi); break; }   // swap one note to a neighbour
        case 1: { if (k < 8 && (k + 1 == 5 || k + 1 == 6 || k + 1 == 7)) { const int at = 1 + irand (R, k - 1);   // only to lengths that keep drifting against 16 steps
                  for (int i = k; i > at; --i) f.s[(size_t) i] = f.s[(size_t) i - 1];
                  auto& s = f.s[(size_t) at]; const auto& prev = f.s[(size_t) at - 1]; s = Step(); s.idx = clampv ((prev.pedal ? hi : prev.idx) + (R() < 0.5 ? 1 : -1), lo, hi); f.len = k + 1; } break; }   // extend by one step
        case 2: { int best = -1, bi = 99; for (int i = 0; i < k; ++i) { const auto& s = f.s[(size_t) i]; if (! s.pedal && ! s.rest && s.oct == 0 && s.idx < bi) { bi = s.idx; best = i; } }
                  if (best >= 0 && hi - bi <= 4) f.s[(size_t) best].oct = 1;
                  break; }   // lift the lowest note an octave (stays near the top)
        default: { for (int i = 0; i < k; ++i) if (f.s[(size_t) i].pedal) { f.s[(size_t) i].pedal = false; f.s[(size_t) i].idx = hi; break; } break; }   // one pedal hit becomes a ladder note
    }
    return f;
}
int widestLeap (const std::vector<Note>& n) { int w = 0; for (size_t i = 1; i < n.size(); ++i) { const int a = std::abs (n[i].p - n[i - 1].p); if (a != 12) w = std::max (w, a); } return w; }
} // namespace

ArpResult composeArp (const Harmony& H, const ArpParams& P, int totalTicks)
{
    if (! modeR (P.mood).figures.empty())
    {   // the composer's figure templates (falls back to the built-in rules when nothing passes the hard rules)
        auto r = composeRecipeArp (H, P, totalTicks);
        if (! r.notes.empty()) return r;
    }
    ArpResult best; best.score = -1e18;
    const int pedalPc = choosePedalPc (H);
    const auto rw = ruleWeights (P.mood);
    for (int c = 0; c < recipe().arpCandidates; ++c)
    {
        Rng R (P.seed * 2654435761u + (uint32_t) c * 40503u + 17u);
        const int rule = pickWeighted (R, rw);
        Figure f = makeFigure (R, P, rule);
        auto notes = realizeArp (f, H, P, H.loopTicks, pedalPc);
        const double s = arpScore (notes, f, H, P, R);
        if (s > best.score) { best.score = s; best.fig = f; best.pedalPc = pedalPc; }
    }
    // EVOLVE: bake one small change every 2-4 bars into a long take
    if (P.evolveBars > 0 && totalTicks > H.loopTicks)
    {
        Rng R (P.seed ^ 0xE7017Eu);
        std::vector<Note> out; Figure cur = best.fig; int t = 0, sec = 0, pc = pedalPc;
        while (t < totalTicks)
        {
            const int bars = (sec % 3 == 2) ? 2 : 4, t1 = std::min (totalTicks, t + bars * BAR);
            if (sec > 0)
            {   // ONE small change per section; try a few and keep the one that still sounds best (no new wide leaps)
                if (sec % 4 == 0) cur = best.fig;   // come home now and then, so it evolves instead of drifting away
                Figure pick = cur; double ps = -1e18;
                const int baseLeap = widestLeap (realizeArp (cur, H, P, H.loopTicks, pc));
                for (int k = 0; k < 10; ++k)
                {
                    const int kind = pickWeighted (R, { 3, 1.5, 1.2, P.holdPedal ? 0.6 : 0.0 });
                    Figure cand = mutateOne (cur, R, P, kind);
                    const auto nn = realizeArp (cand, H, P, H.loopTicks, pc);
                    if (widestLeap (nn) > std::max (9, baseLeap)) continue;
                    double s = arpScore (nn, cand, H, P, R);
                    bool same = cand.len == cur.len; for (int i = 0; same && i < cand.len; ++i) same = cand.s[(size_t) i].idx == cur.s[(size_t) i].idx && cand.s[(size_t) i].oct == cur.s[(size_t) i].oct && cand.s[(size_t) i].pedal == cur.s[(size_t) i].pedal;
                    if (same) s -= 100;
                    if (s > ps) { ps = s; pick = cand; }
                }
                cur = pick;
            }
            realizeRange (out, cur, H, P, t, t1, pc, resyncTicks (P, H));
            t = t1; ++sec;
        }
        best.notes = out;
    }
    else best.notes = realizeArp (best.fig, H, P, totalTicks, pedalPc);
    return best;
}

namespace
{
template <typename Fn>
Figure bestOf (const Figure& orig, const ArpParams& P, uint32_t seed, const Harmony& H, int pedalPc, int K, Fn make)
{
    Figure best = orig; double bs = -1e18; bool found = false;
    for (int c = 0; c < K; ++c)
    {
        Rng R (seed * 747796405u + (uint32_t) c * 2891336453u + 1u);
        Figure f = make (R);
        bool same = f.len == orig.len;
        for (int i = 0; same && i < f.len; ++i) { const auto& a = f.s[(size_t) i]; const auto& b = orig.s[(size_t) i]; same = a.idx == b.idx && a.pedal == b.pedal && a.rest == b.rest && a.oct == b.oct && a.acc == b.acc && a.tie == b.tie; }
        if (same) continue;
        const auto n = realizeArp (f, H, P, H.loopTicks, pedalPc);
        const double s = arpScore (n, f, H, P, R);
        if (s > bs) { bs = s; best = f; found = true; }
    }
    (void) found;
    return best;
}
} // namespace

Figure figureVary (const Figure& f, const ArpParams& P, uint32_t seed, int amount, const Harmony& H, int, int pedalPc)
{
    return bestOf (f, P, seed, H, pedalPc, 20, [&] (Rng& R) {
        Figure g = f; const int n = amount <= 1 ? 1 : 2 + irand (R, 2);
        for (int i = 0; i < n; ++i) g = mutateOne (g, R, P, amount <= 1 ? (R() < 0.8 ? 0 : 2) : pickWeighted (R, { 3, 1, 1, 0.5 }));
        return g; });
}
Figure figureNewNotes (const Figure& f, const ArpParams& P, uint32_t seed, const Harmony& H, int, int pedalPc)
{
    return bestOf (f, P, seed, H, pedalPc, 30, [&] (Rng& R) {
        Figure g = makeFigure (R, P, pickWeighted (R, ruleWeights (P.mood)));
        Figure o = f;   // same rhythm (length, rests, accents, ties), new ladder notes
        for (int i = 0; i < o.len; ++i) { const auto& src = g.s[(size_t) (i % g.len)]; o.s[(size_t) i].idx = o.recipe ? clampv (src.idx - (ladderSize (P) - 4), 1, 4) : src.idx; o.s[(size_t) i].pedal = src.pedal && ! o.s[(size_t) i].rest; o.s[(size_t) i].oct = o.recipe ? 0 : src.oct; }
        o.rule = g.rule; return o; });
}
Figure figureNewRhythm (const Figure& f, const ArpParams& P, uint32_t seed, const Harmony& H, int, int pedalPc)
{
    return bestOf (f, P, seed, H, pedalPc, 30, [&] (Rng& R) {
        std::vector<Step> seq; for (int i = 0; i < f.len; ++i) if (! f.s[(size_t) i].rest) seq.push_back (f.s[(size_t) i]);
        if (seq.empty()) seq.push_back (Step());
        Figure o = f; o.len = pickLen (R, P);
        if (P.cycle == 0 && o.len == f.len) o.len = clampv (f.len + (R() < 0.5 ? -1 : 1), 3, 8);
        for (int i = 0; i < o.len; ++i) { o.s[(size_t) i] = seq[(size_t) i % seq.size()]; o.s[(size_t) i].rest = false; o.s[(size_t) i].tie = false; o.s[(size_t) i].acc = false; }
        if (o.len >= 5 && R() < 0.35) o.s[(size_t) (1 + irand (R, o.len - 1))].rest = true;
        if (R() < 0.5) o.s[(size_t) (1 + irand (R, o.len - 1))].acc = true;
        if (R() < 0.3) o.s[(size_t) (o.len - 1)].tie = true;
        return o; });
}
Figure figureSimpler (const Figure& f)
{
    Figure o = f; std::map<int, int> cnt;
    for (int i = 0; i < o.len; ++i) { o.s[(size_t) i].oct = 0; if (! o.s[(size_t) i].rest && ! o.s[(size_t) i].pedal) cnt[o.s[(size_t) i].idx]++; }
    if (cnt.size() > 2)
    {
        int common = cnt.begin()->first, cc = 0; for (auto& kv : cnt) if (kv.second > cc) { cc = kv.second; common = kv.first; }
        int far = -1, fd = -1;
        for (int i = 0; i < o.len; ++i) { const auto& s = o.s[(size_t) i]; if (s.rest || s.pedal) continue; const int d = std::abs (s.idx - common); if (d > fd) { fd = d; far = i; } }
        if (far >= 0) o.s[(size_t) far].idx = common;
    }
    else if (o.len > 3) o.len -= 1;
    return o;
}
Figure figureBusier (const Figure& f, const ArpParams& P)
{
    Figure o = f; bool changed = false;
    for (int i = 0; i < o.len; ++i) if (o.s[(size_t) i].rest) { o.s[(size_t) i].rest = false; changed = true; }
    if (! changed && o.len < 8) { o.s[(size_t) o.len] = o.s[(size_t) o.len - 1]; o.s[(size_t) o.len].idx = clampv (o.s[(size_t) o.len].idx + 1, 1, ladderSize (P) + 1); o.s[(size_t) o.len].pedal = false; o.len += 1; changed = true; }
    if (! changed) { for (int i = o.len - 1; i > 0; --i) if (o.s[(size_t) i].oct == 0 && ! o.s[(size_t) i].pedal) { o.s[(size_t) i].oct = 1; break; } }
    return o;
}

// ================================================================= MELODY: hook composer
namespace
{
struct Onset { int step, len; bool gs; };
std::vector<std::vector<int>> groupings (int spb, int mood)
{
    (void) mood; const Recipe& Rc = recipe();
    if (spb == 8 && ! Rc.groupings8.empty()) return Rc.groupings8;
    if (spb == 24 && ! Rc.groupings24.empty()) return Rc.groupings24;
    auto g16 = Rc.groupings16;
    if (spb == 32) for (auto& g : g16) for (auto& x : g) x *= 2;
    return g16;
}
std::vector<double> groupingWeights (int spb, int mood)
{
    const auto n = groupings (spb, mood).size();
    std::vector<double> w (n, 1.0);
    if (spb == 16 || spb == 32) { const auto& gw = modeR (mood).groupW; for (size_t i = 0; i < n && i < gw.size(); ++i) w[i] = gw[i]; }
    return w;
}
std::vector<int> groupPositions (Rng& R, int g, int n)
{
    if (n <= 1) return { 0 };
    if (n >= g) { std::vector<int> v; for (int i = 0; i < g; ++i) v.push_back (i); return v; }
    if (n == 2) { if (g == 2) return { 0, 1 }; if (g == 3) return R() < 0.6 ? std::vector<int> { 0, 2 } : std::vector<int> { 0, 1 }; if (g == 4) return R() < 0.5 ? std::vector<int> { 0, 2 } : std::vector<int> { 0, 3 }; return { 0, g / 2 + 1 }; }
    std::vector<int> v; for (int i = 0; i < n; ++i) v.push_back ((int) std::lround ((double) i * g / n)); return v;
}
double shapeVal (int shape, double x)
{
    const double pi = 3.14159265358979;
    switch (shape)
    {
        case 0: return x;                                   // rise
        case 1: return 1 - x;                               // fall
        case 2: return std::sin (pi * x);                   // arch
        case 3: return 1 - std::sin (pi * x);               // valley
        default: return 0.5 + 0.5 * std::sin (2 * pi * x);  // wave
    }
}
struct MelCand
{
    std::vector<Onset> cell; std::map<int, std::vector<int>> pos, moves;
    int callShape = 2, respShape = 1; bool seqRefit = false; double antP = 0.3, susP = 0.2; int lo = 64, hi = 83; int bLift = 0;
};
std::vector<int> movesFor (Rng& R, int n, int mood)
{
    // in-group moves (lattice steps from the group's first note)
    static const std::vector<std::vector<int>> m2 { { 0, 1 }, { 0, -1 }, { 0, 2 }, { 0, -2 } };
    static const std::vector<std::vector<int>> m3 { { 0, 1, 2 }, { 0, -1, -2 }, { 0, 1, -1 }, { 0, -1, 1 }, { 0, 2, 1 }, { 0, -2, -1 }, { 0, 2, 4 }, { 0, -1, 0 } };
    static const std::vector<std::vector<int>> m4 { { 0, 1, 2, 1 }, { 0, -1, -2, -1 }, { 0, 2, 1, -1 }, { 0, 1, -1, 0 }, { 0, -2, -1, 1 }, { 0, 2, 4, 2 } };
    if (n <= 1) return { 0 };
    if (n == 2) return pickV (R, m2);
    if (n == 3) { auto v = pickV (R, m3); if (mood >= Sad && R() < 0.3) v = { 0, 2, 4 }; return v; }
    if (n == 4) return pickV (R, m4);
    std::vector<int> v { 0 }; for (int i = 1; i < n; ++i) v.push_back (v.back() + (R() < 0.6 ? 1 : -1)); return v;
}
// one bar of melody from the rhythm cell, following a contour
std::vector<Note> buildBar (const MelCand& C, const Harmony& H, int bar, int spb, int shape, int lift, int prevPitch, double gateF, Rng& R)
{
    std::vector<Note> out; const int st = BAR / spb; int anchor = prevPitch;
    int gStartPitch = prevPitch; int gStep = 0; int gi = 0; int inG = 0; int glen = 1;
    for (size_t j = 0; j < C.cell.size(); ++j)
    {
        const Onset& o = C.cell[j]; const int t = bar * BAR + o.step * st;
        const HChord& ch = H.at (t);
        int p;
        if (o.gs)
        {
            const double x = (double) o.step / spb;
            const double target = C.lo + (C.hi - C.lo) * (0.2 + 0.6 * shapeVal (shape, x)) + lift;
            auto cts = chordTones (ch, C.lo - 2, C.hi + 2 + lift);
            int best = target, bc = 1 << 29;
            for (int ct : cts)
            {
                double cost = std::abs (ct - target) + 0.6 * std::max (0, std::abs (ct - anchor) - 5) + (std::abs (ct - anchor) > 9 ? 4 : 0) + R() * 0.8;
                if ((int) (cost * 100) < bc) { bc = (int) (cost * 100); best = ct; }
            }
            p = best; anchor = p; gStartPitch = p; gStep = o.step; inG = 0; ++gi;
            int k = 1; while (j + (size_t) k < C.cell.size() && ! C.cell[j + (size_t) k].gs) ++k; glen = k;
        }
        else
        {
            ++inG;
            auto it = C.moves.find (glen);
            const auto& mv = it != C.moves.end() ? it->second : std::vector<int> { 0, 1, 2, 3, 4, 5, 6, 7 };
            const int m = mv[(size_t) std::min ((int) mv.size() - 1, inG)];
            p = latticeMove (ch, gStartPitch, m);
        }
        (void) gStep;
        Note n; n.t = t; n.p = clampPitch (p);
        const int gapSteps = o.len;
        n.d = std::max (6, (int) std::lround (gapSteps * st * gateF));
        n.v = o.gs ? 0.86f : 0.7f; n.acc = o.gs;
        out.push_back (n);
    }
    return out;
}
std::vector<Note> refitBar (const std::vector<Note>& src, const Harmony& H, int fromBar, int toBar, bool seq, int spb)
{
    std::vector<Note> out; const int sh = (toBar - fromBar) * BAR;
    int steps = 0;
    if (seq)
    {
        const int r0 = H.at (fromBar * BAR).root, r1 = H.at (toBar * BAR).root;
        int i0 = 0, i1 = 0; for (size_t i = 0; i < H.keyPcs.size(); ++i) { if (H.keyPcs[i] == r0) i0 = (int) i; if (H.keyPcs[i] == r1) i1 = (int) i; }
        steps = i1 - i0; if (steps > 3) steps -= 7; if (steps < -3) steps += 7;
    }
    int prev = -1;
    for (auto n : src)
    {
        n.t += sh;
        if (steps) n.p = latticeMove (H.at (n.t), n.p, steps);
        const bool strong = n.acc || strongPos (n.t, spb);
        if (strong && ! H.chordTone (n.p, n.t)) n.p = nearestChordTone (H, n.p, n.t, prev >= 0 ? (n.p >= prev ? 1 : -1) : 0);
        else if (! H.scaleTone (n.p, n.t)) n.p = nearestScaleTone (H, n.p, n.t);
        else if (H.clash (n.p, n.t) && n.d >= PPQ / 2) n.p = nearestChordTone (H, n.p, n.t);
        prev = n.p; out.push_back (n);
    }
    return out;
}
std::vector<std::string> phrasePlan (int nb)
{
    if (nb <= 1) return { "A" };
    if (nb == 2) return { "A", "B" };
    if (nb == 4) return { "A", "R", "A", "B" };
    if (nb == 8) return { "A", "R", "A", "B", "A", "R", "A", "C" };
    if (nb == 16) return { "A", "R", "A", "B", "A", "R", "A", "C", "A", "R", "A", "B2", "A", "R", "A", "C" };
    std::vector<std::string> v ((size_t) nb, "A"); v.back() = "B"; return v;
}
} // namespace

namespace
{
// ---- MIDNIGHT / DUSK hooks: rhythm groupings + contour + A R A B plan
std::vector<Note> composeGeneric (const Harmony& H, const MelodyParams& P, int phraseTicks)
{
    const int spb = P.spb, nb = std::max (1, phraseTicks / BAR);
    const auto plan = phrasePlan (nb);
    std::vector<Note> best; double bestS = -1e18;
    const auto G = groupings (spb, P.mood); const auto GW = groupingWeights (spb, P.mood);
    for (int c = 0; c < recipe().melodyCandidates; ++c)
    {
        Rng R (P.seed * 2246822519u + (uint32_t) c * 3266489917u + 7u);
        MelCand C;
        const auto& grp = G[(size_t) pickWeighted (R, GW)];
        const double dens = clampv (0.12 + 0.75 * P.master + (R() - 0.5) * 0.2, 0.0, 1.0);
        std::map<int, int> nPer;
        for (int g : grp) if (! nPer.count (g)) nPer[g] = clampv (1 + (int) std::floor ((g - 1) * dens + R() * 0.6), 1, g);
        for (auto& kv : nPer) { C.pos[kv.first] = groupPositions (R, kv.first, kv.second); C.moves[kv.second] = movesFor (R, kv.second, P.mood); }
        int step = 0;
        for (int g : grp)
        {
            const auto& ps = C.pos[g];
            for (size_t k = 0; k < ps.size(); ++k)
            {
                const int nxt = k + 1 < ps.size() ? ps[k + 1] : g;
                C.cell.push_back ({ step + ps[k], nxt - ps[k], k == 0 });
            }
            step += g;
        }
        const int r = clampv (P.range, 1, 3);
        C.lo = 64 + 12 * P.reg - (r - 1) * 3; C.hi = C.lo + 7 + 6 * r;
        if (P.mood == Bittersweet) { C.callShape = pickV (R, std::vector<int> { 2, 0, 4, 1 }); C.respShape = 1; C.seqRefit = R() < 0.35; }
        else { C.callShape = pickV (R, std::vector<int> { 2, 1, 4, 3 }); C.respShape = pickV (R, std::vector<int> { 3, 4, 1 }); C.seqRefit = R() < 0.2; }
        C.antP = modeR (P.mood).antP; C.susP = modeR (P.mood).susP;
        const double gateF = P.mood == Bittersweet ? 0.75 : clampv (P.gate + 0.15, 0.3, 0.9);
        const int startP = (C.lo + C.hi) / 2;
        const auto A0 = buildBar (C, H, 0, spb, C.callShape, 0, startP, gateF, R);
        std::vector<Note> notes;
        for (int b = 0; b < nb; ++b)
        {
            const std::string role = plan[(size_t) b];
            std::vector<Note> bn;
            const int prevP = notes.empty() ? startP : notes.back().p;
            if (b == 0) bn = A0;
            else if (role == "A") bn = refitBar (A0, H, 0, b, C.seqRefit, spb);
            else if (role == "R") bn = (P.mood == Bittersweet) ? buildBar (C, H, b, spb, C.respShape, 0, prevP, gateF, R) : refitBar (A0, H, 0, b, false, spb);
            else if (role == "B" || role == "B2")
            {
                if (P.mood == Bittersweet && (R() < 0.55 || role == "B2"))
                {
                    bn = refitBar (A0, H, 0, b, C.seqRefit, spb);
                    const int from = role == "B2" ? 0 : BAR / 2; int mx = 0; for (auto& n : bn) mx = std::max (mx, n.p);
                    if (mx + 12 <= C.hi + 7) for (auto& n : bn) if (n.t - b * BAR >= from) n.p += 12;
                }
                else bn = buildBar (C, H, b, spb, 0, P.mood == Bittersweet ? 5 : 3, prevP, gateF, R);
            }
            else
            {
                bn = refitBar (A0, H, 0, b, false, spb);
                if (! bn.empty()) { auto& l = bn.back(); const auto& ch = H.at (l.t); l.p = nearestIn (chordTones (ch, l.p - 7, l.p), l.p, -1); l.d = std::max (l.d, BAR / spb * 2); }
            }
            if (b > 0 && ! bn.empty() && role != "B" && role != "B2")
            {
                double avg = 0; for (auto& n : bn) avg += n.p; avg /= (double) bn.size();
                if (avg - prevP > 8) for (auto& n : bn) n.p -= 12; else if (prevP - avg > 8) for (auto& n : bn) n.p += 12;
            }
            notes.insert (notes.end(), bn.begin(), bn.end());
        }
        const int st = BAR / spb;
        for (size_t i = 1; i + 1 < notes.size(); ++i)   // suspensions
        {
            Note& n = notes[i];
            if (n.t % BAR != 0 || R() >= C.susP) continue;
            const int prevP = notes[i - 1].p;
            if (! H.chordTone (prevP, n.t) && H.scaleTone (prevP, n.t))
            {
                const auto below = chordTones (H.at (n.t), prevP - 2, prevP - 1);   // resolve down a step into a chord tone (if there is one)
                if (! below.empty() && notes[i + 1].t - n.t <= PPQ) { n.p = prevP; notes[i + 1].p = below.back(); }
            }
        }
        for (int b = 0; b + 1 < nb; ++b)   // anticipations
        {
            int last = -1, first = -1;
            for (size_t i = 0; i < notes.size(); ++i) { if (barOf (notes[i].t) == b) last = (int) i; if (barOf (notes[i].t) == b + 1 && first < 0) first = (int) i; }
            if (last < 0 || first < 0 || R() >= C.antP) continue;
            Note& l = notes[(size_t) last];
            if ((l.t % BAR) / st < spb - 3) continue;
            const Note f = notes[(size_t) first];
            const int nextT = (b + 1) * BAR;
            {   // a next-chord tone that is also in key over the current chord (no C over A major in C# minor)
                std::vector<int> ok; for (int q : chordTones (H.at (nextT), f.p - 7, f.p + 7)) if (H.scaleTone (q, l.t)) ok.push_back (q);
                l.p = nearestIn (ok.empty() ? chordTones (H.at (nextT), f.p - 7, f.p + 7) : ok, f.p);
            }
            if (f.t == nextT)
            {
                const int endT = (size_t) first + 1 < notes.size() ? notes[(size_t) first + 1].t : nextT + st * 2;
                l.d = std::max (l.d, (int) std::lround ((endT - l.t) * 0.9)); l.acc = true; l.v = 0.9f;
                notes.erase (notes.begin() + first);
            }
        }
        for (auto& n : notes) n.p = clampPitch (n.p);
        sortNotes (notes);
        const double s = taste (notes, H, P.mood, false, phraseTicks).score + R() * 0.3;
        if (s > bestS) { bestS = s; best = notes; }
    }
    return best;
}

// ---- SUNRISE hooks: 2-bar units, antecedent (open ending) / consequent (closed, down to the tonic), mostly descending
// stepwise lines that keep landing on one anchor note, syncopated 1/8 + 1/16 rhythm, held notes at phrase ends,
// anticipations an 1/8 before the bar, STUTTER bounces.
struct UOn { int step, len; bool gs; bool held = false, pickup = false; };
std::vector<UOn> unitRhythm (Rng& R, int unitSteps, double master)
{
    const auto half = recipe().sunriseHalves.empty() ? std::vector<std::vector<int>> { { 3, 3, 2 } } : recipe().sunriseHalves;
    std::vector<UOn> on;
    const int bars = unitSteps / 16;
    for (int b = 0; b < bars; ++b)
    {
        const bool lastBar = b == bars - 1;
        const auto& h1 = pickV (R, half); const auto& h2 = R() < 0.6 ? h1 : pickV (R, half);
        int step = b * 16;
        auto addHalf = [&] (const std::vector<int>& g)
        {
            for (int len : g)
            {
                on.push_back ({ step, len, true });
                if (len >= 3 && R() < 0.25 + 0.5 * master) { on.back().len = 2; on.push_back ({ step + 2, len - 2, false }); }   // 8th + 16th syncopation
                step += len;
            }
        };
        addHalf (h1);
        if (! lastBar) addHalf (h2);
        else
        {   // phrase end: a held note, then an 1/8 pickup that anticipates the next downbeat
            const int heldAt = step + (R() < 0.5 ? 0 : 2);
            if (heldAt > step) on.push_back ({ step, heldAt - step, true });
            on.push_back ({ heldAt, (b * 16 + 14) - heldAt, true }); on.back().held = true;
            on.push_back ({ b * 16 + 14, 2, true }); on.back().pickup = true;
        }
    }
    return on;
}
std::vector<std::string> sunriseForm (int units, int form)
{
    if (units <= 1) return { "Ac" };
    if (units == 2) return form == 1 || form == 2 ? std::vector<std::string> { "Ao", "Bc" } : std::vector<std::string> { "Ao", "Ac" };
    std::vector<std::string> f4;
    switch (form)
    {
        case 1: f4 = { "Ao", "Bc", "Ao", "B2c" }; break;   // ABAB
        case 2: f4 = { "Ao", "Bc", "Ao", "Cc" }; break;    // ABAC
        case 3: f4 = { "Ao", "Ac", "Vo", "Ac" }; break;    // AAAA with variation
        default: f4 = { "Ao", "Ac", "Ao", "Bc" }; break;   // AAAB
    }
    std::vector<std::string> out;
    for (int i = 0; i < units; ++i) out.push_back (f4[(size_t) i % 4]);
    if (units >= 8) out[7] = "B2c";
    return out;
}
std::vector<Note> composeSunrise (const Harmony& H, const MelodyParams& P, int phraseTicks)
{
    const int st = BAR / 16;
    const int phraseBars = std::max (1, phraseTicks / BAR), unitBars = phraseBars >= 4 ? 2 : 1, units = std::max (1, phraseBars / unitBars), unitSteps = unitBars * 16;
    const auto form = sunriseForm (units, P.form);
    const int tonic = H.key;
    std::vector<Note> best; double bestS = -1e18;
    for (int c = 0; c < recipe().melodyCandidates; ++c)
    {
        Rng R (P.seed * 1597334677u + (uint32_t) c * 2654435761u + 11u);
        const int r = clampv (P.range, 1, 3), span = r == 1 ? 12 : r == 2 ? 16 : 19;
        const int lo = 67 + 12 * P.reg - span / 3, hi = lo + span;
        // the anchor: a chord tone that most chords share (not the tonic, so the hook "hangs" until it resolves)
        int anchorPc = mod12 (tonic + 4); double ab = -1e9;
        for (int deg : { 2, 4, 0, 1, 5 })
        {
            if ((size_t) deg >= H.keyPcs.size()) continue;
            const int pc = H.keyPcs[(size_t) deg]; double s = (deg == 0 ? -0.5 : 0) + R() * 0.8;
            for (auto& ch : H.chords) { if (has (ch.pcs, pc)) s += 1; else for (int q : ch.pcs) if (mod12 (pc - q) == 1) s -= 1.5; }
            if (s > ab) { ab = s; anchorPc = pc; }
        }
        int anchor = lo + (int) (span * 0.55); { int near = anchor, bd = 99; for (int q = anchor - 6; q <= anchor + 6; ++q) if (mod12 (q) == anchorPc && std::abs (q - anchor) < bd) { bd = std::abs (q - anchor); near = q; } anchor = near; }
        const auto rhythm = unitRhythm (R, unitSteps, P.master);
        // pitch plan in key-scale steps above the anchor: group starts walk down an envelope, notes inside groups step down or repeat
        const int top = 3 + irand (R, 3);
        std::vector<int> plan; int gIdx = 0, nG = 0; for (auto& o : rhythm) if (o.gs && ! o.held && ! o.pickup) ++nG;
        int cur = top;
        for (auto& o : rhythm)
        {
            if (o.held) { plan.push_back (0); continue; }
            if (o.pickup) { plan.push_back (1); continue; }
            if (o.gs)
            {
                const double x = nG > 1 ? (double) gIdx / (nG - 1) : 0;
                cur = (int) std::lround (top * (1 - x)) + (R() < 0.2 ? 1 : 0);
                if (gIdx > 0 && o.step % 16 == 0 && R() < 0.5) cur = top - irand (R, 2);   // restart a little lower at the next bar (saw-tooth descent)
                ++gIdx;
            }
            else { const double u = R(); cur += u < 0.74 ? -1 : u < 0.8 ? 0 : 1; }
            plan.push_back (cur);
        }
        auto keyStep = [&] (int base, int steps) { int p = base, k = steps;
            while (k > 0) { ++p; while (! has (H.keyPcs, mod12 (p))) ++p; --k; }
            while (k < 0) { --p; while (! has (H.keyPcs, mod12 (p))) --p; ++k; }
            return p; };
        auto realizeUnit = [&] (int u, const std::string& role, int liftSteps) {
            std::vector<Note> out; const int t0 = u * unitSteps * st;
            const bool closed = role.back() == 'c';
            int prev = -1;
            for (size_t i = 0; i < rhythm.size(); ++i)
            {
                const UOn& o = rhythm[i]; const int t = t0 + o.step * st;
                int target = keyStep (anchor, plan[i] + liftSteps);
                if (role[0] == 'V' && o.gs && (o.step / 4) % 3 == 1) target = keyStep (target, R() < 0.5 ? 1 : -1);
                int p;
                if (o.held)
                {
                    const auto cts = chordTones (H.at (t), anchor - 9, anchor + 9);
                    if (closed)
                    {   // resolve down to the tonic when the chord holds it, else the nearest stable tone below
                        const int ref = prev < 0 ? anchor : prev;   // resolve DOWN to the tonic, but never far from where the line is
                        std::vector<int> tons; for (int q : cts) if (mod12 (q) == tonic && q <= ref + 2 && q >= std::max (ref - 7, lo - 2)) tons.push_back (q);
                        p = ! tons.empty() ? nearestIn (tons, ref - 1, -1) : nearestIn (cts, ref - 1, -1);
                    }
                    else
                    {   // open: a chord tone that is not the tonic
                        std::vector<int> o2; for (int q : cts) if (mod12 (q) != tonic) o2.push_back (q);
                        p = nearestIn (o2.empty() ? cts : o2, anchor, 0);
                    }
                }
                else if (o.pickup)
                {
                    const int nt = t0 + unitSteps * st;
                    p = nearestIn (chordTones (H.at (nt), anchor - 5, anchor + 9), keyStep (anchor, plan[0] + liftSteps), 0);
                }
                else if (t % PPQ == 0)
                {   // on the beat: a chord tone near the plan; don't just repeat the last note when the plan moves
                    auto cts = chordTones (H.at (t), target - 4, target + 4);
                    p = nearestIn (cts, target, -1);
                    if (p == prev && plan[i] != (i ? plan[i - 1] : plan[i]))
                    {
                        const int dir = plan[i] < (i ? plan[i - 1] : plan[i]) ? -1 : 1; int alt = p;
                        for (int q : cts) if ((q - p) * dir > 0 && (alt == p || std::abs (q - p) < std::abs (alt - p))) alt = q;
                        p = alt;
                    }
                }
                else
                {   // off the beat (syncopated group starts too): the planned scale step, avoiding a semitone rub on long notes
                    p = target; if (! H.scaleTone (p, t)) p = nearestScaleTone (H, p, t, -1);
                    if (o.len >= 3 && H.clash (p, t)) p = nearestIn (chordTones (H.at (t), p - 3, p + 3), p, -1);
                    if (p == prev && plan[i] != (i ? plan[i - 1] : plan[i])) p = latticeMove (H.at (t), p, plan[i] < plan[i - 1] ? -1 : 1);
                }
                Note n; n.t = t; n.p = clampPitch (p);
                n.d = std::max (6, (int) std::lround (o.len * st * (o.held ? 0.95 : 0.82)));
                n.v = o.gs ? 0.86f : 0.72f; n.acc = o.gs; if (o.pickup) { n.v = 0.9f; n.acc = true; }
                out.push_back (n); prev = n.p;
            }
            return out;
        };
        std::vector<Note> notes;
        for (int u = 0; u < units; ++u)
        {
            const std::string& role = form[(size_t) u];
            int lift = 0;
            if (role[0] == 'B') lift = role.size() > 2 && role[1] == '2' ? 7 : 2 + irand (R, 2);
            if (role[0] == 'C') lift = -1;
            auto un = realizeUnit (u, role, lift);
            if (role[0] == 'B' && R() < 0.4)   // octave answer instead: the A unit again, an octave up for its first half (when it fits)
            {
                auto oa = realizeUnit (u, "Ac", 0);
                int mx = 0; for (auto& n : oa) if (n.t < u * unitSteps * st + unitSteps * st / 2) mx = std::max (mx, n.p);
                if (mx + 12 <= hi + 5) { for (auto& n : oa) if (n.t < u * unitSteps * st + unitSteps * st / 2) n.p += 12; un = oa; }
            }
            notes.insert (notes.end(), un.begin(), un.end());
        }
        // the pickup ties over the bar line: it replaces the next unit's downbeat (inside the phrase only)
        for (size_t i = 0; i + 1 < notes.size(); ++i)
            if (notes[i].acc && (notes[i].t / st) % unitSteps == unitSteps - 2 && notes[i + 1].t == notes[i].t + 2 * st && notes[i].t + 2 * st < phraseTicks)
            { const int endT = i + 2 < notes.size() ? notes[i + 2].t : notes[i + 1].t + notes[i + 1].d; notes[i].d = std::max (notes[i].d, (int) std::lround ((endT - notes[i].t) * 0.9)); notes.erase (notes.begin() + (long) i + 1); }
        // STUTTER: a note becomes 2-3 quick repeats of the same pitch (inside its own space, never into the next note)
        if (P.stutter > 0.001)
        {
            sortNotes (notes);
            std::vector<Note> o2;
            for (size_t i = 0; i < notes.size(); ++i)
            {
                const Note& n = notes[i];
                const int gap = (i + 1 < notes.size() ? notes[i + 1].t : phraseTicks) - n.t;
                const int steps = gap / st;
                if (steps >= 2 && n.d < PPQ * 2 && n.acc && R() < P.stutter * 0.4 && (i + 1 >= notes.size() || notes[i + 1].p != n.p) && (i == 0 || notes[i - 1].p != n.p))
                {
                    const int len = steps >= 4 && R() < 0.4 ? 2 : 1, k = std::min (steps / len, R() < 0.5 ? 3 : 2);
                    for (int j = 0; j < k; ++j) { Note s = n; s.t = n.t + j * len * st; s.d = std::max (6, (int) std::lround (len * st * 0.6)); s.v = j ? n.v * 0.84f : n.v; s.acc = j == 0; o2.push_back (s); }
                }
                else o2.push_back (n);
            }
            notes = o2;
        }
        sortNotes (notes);
        // score: taste + the SUNRISE shape (mostly descending steps, a recurring anchor, closed endings on the tonic)
        Taste T = taste (notes, H, Sunrise, false, phraseTicks);
        double s = T.score;
        int down = 0, up = 0, anch = 0; for (size_t i = 1; i < notes.size(); ++i) { const int d = notes[i].p - notes[i - 1].p; if (d < 0 && d >= -3) ++down; else if (d > 0) ++up; }
        for (auto& n : notes) if (mod12 (n.p) == anchorPc) ++anch;
        const double dr = (down + up) ? (double) down / (down + up) : 0.5;
        s += clampv ((dr - 0.5) * 6, -2.0, 2.0);
        const double as = notes.empty() ? 0 : (double) anch / notes.size();
        if (as >= 0.15 && as <= 0.4) s += 2; else s -= 1;
        if (! notes.empty() && mod12 (notes.back().p) == tonic) s += 1;
        s += R() * 0.3;
        if (s > bestS) { bestS = s; best = notes; }
    }
    return best;
}
} // namespace

std::vector<Note> composeMelody (const Harmony& Hin, const MelodyParams& P, int totalTicks, bool evolve)
{
    const int phraseTicks = std::max (BAR, std::min (totalTicks, P.phraseBars > 0 ? P.phraseBars * BAR : Hin.loopTicks));
    Harmony H = Hin; H.offset = P.stagger;
    std::vector<Note> phrase;
    if ((P.mood == Sunrise || P.mood == Dusk) && ! modeR (P.mood).cellsTwo.empty()) phrase = composeRecipeHook (H, P, phraseTicks);   // the composer's hook recipe
    if (phrase.empty()) phrase = P.mood == Sunrise ? composeSunrise (H, P, phraseTicks) : composeGeneric (H, P, phraseTicks);
    if (P.stagger)   // back to song time: the phrase starts STAGGER ticks into the chord loop
    {
        for (auto& n : phrase) n.t = (n.t + P.stagger) % phraseTicks;
        sortNotes (phrase);
    }
    for (auto& n : phrase) n.d = std::min (n.d, std::max (6, phraseTicks - n.t));
    if (totalTicks <= phraseTicks) return phrase;
    std::vector<Note> out = phrase, cur = phrase; int t = phraseTicks, k = 1;
    while (t < totalTicks)
    {
        if (evolve)   // EVOLVE: each repeat changes one little thing
        {
            EditCtx ec; ec.H = &Hin; ec.totalTicks = phraseTicks; ec.spb = P.spb; ec.mood = P.mood;
            cur = toolVary (cur, ec, P.seed + (uint32_t) k * 977u, 1);
        }
        for (auto n : cur) { n.t += t; if (n.t < totalTicks) out.push_back (n); }
        t += phraseTicks; ++k;
    }
    return out;
}

// ================================================================= BASS
std::vector<Note> composeBass (const Harmony& H, const BassParams& P, int totalTicks)
{
    std::vector<Note> out;
    const int st = P.rate == 1 ? BAR / 16 : BAR / 8, off = P.rate == 2 ? BAR / 8 : 0, stride = P.rate == 2 ? PPQ : st;
    if (P.seed == 0)
    {   // the plain line: the chord's bass note on every step
        for (int t = off; t < totalTicks; t += stride)
        {
            const auto& c = H.at (t);
            Note n; n.t = t; n.p = clampv (c.bassNote, 28, 55); n.d = std::max (6, (int) std::lround (st * P.gate)); n.v = t % PPQ == 0 ? 0.85f : 0.74f; n.acc = t % PPQ == 0;
            out.push_back (n);
        }
        return out;
    }
    // a seeded take (NEW): one of four bass shapes on the same grid, always on the chord's bass note at each chord change and bar start
    Rng R (P.seed * 2654435761u + 0x5BA55u);
    const int shape = std::min (3, (int) (R() * 4.0));          // 0 octave bounce, 1 root-fifth, 2 syncopated (rests), 3 approach notes
    const double gate = clampv (P.gate + (R() - 0.5) * 0.3, 0.35, 0.9);
    uint32_t restMask = 0; for (int k = 0; k < 8; ++k) if (R() < 0.35) restMask |= 1u << k;   // shape 2: which of the 8 steps of a bar-half rest
    const bool upFirst = R() < 0.5;
    auto fold = [] (int p) { while (p > 55) p -= 12; while (p < 28) p += 12; return p; };
    auto fifthOf = [&] (const HChord& c, int root) {
        for (int pc : c.pcs) if (((pc - root) % 12 + 12) % 12 == 7) return fold (root + 7);
        for (int pc : c.pcs) { const int iv = ((pc - root) % 12 + 12) % 12; if (iv == 3 || iv == 4) return fold (root + iv); }
        return fold (root + 12);
    };
    int idx = 0;
    for (int t = off; t < totalTicks; t += stride, ++idx)
    {
        const auto& c = H.at (t);
        const int root = clampv (c.bassNote, 28, 55);
        const bool anchor = t % BAR == off || t == std::max (off, c.t) || (t - stride >= 0 && H.at (t - stride).bassNote != c.bassNote);
        int p = root; bool play = true;
        switch (shape)
        {
            case 0: if (! anchor && (idx % 2 == 1) == upFirst) p = root + 12 <= 55 ? root + 12 : root - 12 >= 28 ? root - 12 : root; break;
            case 1: if (! anchor && (t % PPQ != 0 || (t / PPQ) % 2 == 1)) p = (idx % 2 == 1) ? fifthOf (c, root) : root; break;
            case 2: play = anchor || ((restMask >> (idx % 8)) & 1u) == 0; break;
            default:
            {
                const int nt = t + stride; const int next = nt < totalTicks ? clampv (H.at (nt).bassNote, 28, 55) : root;
                if (! anchor && next != root) p = fold (next + (next > root ? -1 : 1));          // a chromatic step into the next chord
                else if (! anchor && t % PPQ == 0 && (t / PPQ) % 4 == 2) p = fifthOf (c, root);
                break;
            }
        }
        if (! play) continue;
        Note n; n.t = t; n.p = p; n.d = std::max (6, (int) std::lround (st * gate)); n.v = t % PPQ == 0 ? 0.85f : 0.74f; n.acc = t % PPQ == 0;
        out.push_back (n);
    }
    if (shape == 2)   // a rest lets the note before it ring into the gap
        for (size_t i = 0; i + 1 < out.size(); ++i) out[i].d = std::max (out[i].d, (int) std::lround ((out[i + 1].t - out[i].t) * gate));
    return out;
}

// ================================================================= note tools
bool editable (const EditCtx& c, const std::vector<Note>& notes, size_t i)
{
    if (notes[i].lock || barLocked (c.barLocks, notes[i].t)) return false;
    if (c.sel != nullptr && ! c.sel->empty()) return i < c.sel->size() && (*c.sel)[i];
    return true;
}

std::vector<Note> mergeLocked (const std::vector<Note>& fresh, const std::vector<Note>& old, const EditCtx& c)
{
    std::vector<Note> out;
    std::set<int> lockedT; for (auto& n : old) if (n.lock || barLocked (c.barLocks, n.t)) { out.push_back (n); lockedT.insert (n.t); }
    for (auto& n : fresh) if (! barLocked (c.barLocks, n.t) && ! lockedT.count (n.t)) out.push_back (n);
    sortNotes (out); return out;
}

namespace
{
template <typename Fn>
std::vector<Note> bestVariant (const std::vector<Note>& notes, const EditCtx& c, uint32_t seed, int K, Fn make)
{
    std::vector<Note> best = notes; double bs = -1e18;
    for (int k = 0; k < K; ++k)
    {
        Rng R (seed * 3266489917u + (uint32_t) k * 668265263u + 3u);
        auto v = make (R);
        bool diff = v.size() != notes.size();
        for (size_t i = 0; ! diff && i < v.size(); ++i) diff = v[i].p != notes[i].p || v[i].t != notes[i].t || v[i].d != notes[i].d;
        if (! diff) continue;
        sortNotes (v);
        const double s = taste (v, *c.H, c.mood, c.pedalMode, c.totalTicks).score + R() * 0.4;
        if (s > bs) { bs = s; best = v; }
    }
    return best;
}
} // namespace

std::vector<Note> toolVary (const std::vector<Note>& notes, const EditCtx& c, uint32_t seed, int amount)
{
    if (notes.empty()) return notes;
    const int st = BAR / std::max (1, c.spb);
    return bestVariant (notes, c, seed, 24, [&] (Rng& R) {
        std::vector<Note> v = notes;
        std::vector<size_t> ed; for (size_t i = 0; i < v.size(); ++i) if (editable (c, v, i)) ed.push_back (i);
        if (ed.empty()) return v;
        const int groups = amount <= 1 ? 1 + (R() < 0.25 ? 1 : 0) : 3 + irand (R, 2);
        for (int g = 0; g < groups; ++g)
        {
            const size_t s0 = ed[(size_t) irand (R, (int) ed.size())];
            const int pos = v[s0].t % BAR, p0 = v[s0].p, mv = (R() < 0.5 ? -1 : 1) * (1 + (R() < 0.3 ? 1 : 0));
            for (size_t i : ed)   // the same note in the repeated figure changes everywhere (keeps the hook a hook)
            {
                if (v[i].t % BAR != pos || v[i].p != p0) continue;
                int q = latticeMove (c.H->at (v[i].t), v[i].p, mv);
                if (v[i].acc || strongPos (v[i].t, c.spb)) q = nearestChordTone (*c.H, q, v[i].t, mv);
                v[i].p = clampPitch (q);
            }
        }
        if (amount >= 2 && R() < 0.7)   // and one rhythm change
        {
            const size_t i = ed[(size_t) irand (R, (int) ed.size())];
            const double u = R();
            if (u < 0.4 && v[i].d >= 2 * st) { Note n2 = v[i]; n2.t += st; n2.d = std::max (6, v[i].d - st); n2.acc = false; v[i].d = st; n2.p = latticeMove (c.H->at (n2.t), v[i].p, R() < 0.5 ? 1 : -1); v.push_back (n2); }
            else if (u < 0.7 && i + 1 < v.size() && editable (c, v, i + 1) && barOf (v[i + 1].t) == barOf (v[i].t)) { v[i].d = v[i + 1].t + v[i + 1].d - v[i].t; v.erase (v.begin() + (long) i + 1); }
            else
            {
                const int nt = v[i].t + (R() < 0.5 ? -st : st);
                bool clash = nt < 0 || nt >= c.totalTicks || barOf (nt) != barOf (v[i].t);
                for (auto& n : v) if (n.t == nt) clash = true;
                if (! clash) v[i].t = nt;
            }
        }
        return v; });
}

std::vector<Note> toolNewNotes (const std::vector<Note>& notes, const EditCtx& c, uint32_t seed)
{
    if (notes.empty()) return notes;
    return bestVariant (notes, c, seed, 32, [&] (Rng& R) {
        std::vector<Note> v = notes;
        int lo = 999, hi = -999; for (auto& n : notes) { lo = std::min (lo, n.p); hi = std::max (hi, n.p); }
        if (hi - lo < 9) { const int m = (lo + hi) / 2; lo = m - 6; hi = m + 6; }
        const int shape = irand (R, 5); int prev = -1, anchor = (lo + hi) / 2;
        for (size_t i = 0; i < v.size(); ++i)
        {
            if (! editable (c, v, i)) { prev = v[i].p; continue; }
            const bool strong = v[i].acc || strongPos (v[i].t, c.spb) || i == 0;
            const double x = (double) (v[i].t % BAR) / BAR;
            const double target = lo + (hi - lo) * (0.15 + 0.7 * shapeVal (shape, x));
            if (strong || prev < 0)
            {
                auto cts = chordTones (c.H->at (v[i].t), lo - 2, hi + 2); int best = (int) target; double bc = 1e9;
                for (int ct : cts) { const double cost = std::abs (ct - target) + 0.5 * std::max (0, std::abs (ct - (prev < 0 ? anchor : prev)) - 5) + R() * 1.2; if (cost < bc) { bc = cost; best = ct; } }
                v[i].p = best;
            }
            else v[i].p = latticeMove (c.H->at (v[i].t), prev, (R() < 0.5 ? -1 : 1) * (1 + (R() < 0.35 ? 1 : 0)));
            v[i].p = clampPitch (v[i].p); prev = v[i].p;
        }
        return v; });
}

std::vector<Note> toolNewRhythm (const std::vector<Note>& notes, const EditCtx& c, uint32_t seed)
{
    if (notes.empty()) return notes;
    const int spb = c.spb == 8 || c.spb == 16 || c.spb == 24 || c.spb == 32 ? c.spb : 16, st = BAR / spb;
    const int nb = std::max (1, c.totalTicks / BAR);
    return bestVariant (notes, c, seed, 28, [&] (Rng& R) {
        const auto G = groupings (spb, c.mood); const auto& grp = G[(size_t) pickWeighted (R, groupingWeights (spb, c.mood))];
        std::vector<Note> out;
        for (int b = 0; b < nb; ++b)
        {
            std::vector<size_t> idx; bool allEd = true;
            for (size_t i = 0; i < notes.size(); ++i) if (barOf (notes[i].t) == b) { idx.push_back (i); if (! editable (c, notes, i)) allEd = false; }
            if (idx.empty() || ! allEd) { for (size_t i : idx) out.push_back (notes[i]); continue; }
            const int want = (int) idx.size();
            // fill the groups until the bar has about as many onsets as before
            std::vector<std::pair<int, bool>> on; int step = 0;
            const double dens = clampv ((double) want / spb, 0.0, 1.0);
            for (int g : grp)
            {
                const int n = clampv ((int) std::lround (g * dens + (R() - 0.5) * 0.8), 1, g);
                const auto ps = groupPositions (R, g, n);
                for (size_t k = 0; k < ps.size(); ++k) on.push_back ({ step + ps[k], k == 0 });
                step += g;
            }
            float gate = 0.5f; { double gs = 0; for (size_t i : idx) gs += (double) notes[i].d / st; gate = (float) clampv (gs / idx.size(), 0.25, 1.8); }
            for (size_t k = 0; k < on.size(); ++k)
            {
                const Note& src = notes[idx[k % idx.size()]];
                Note n = src; n.t = b * BAR + on[k].first * st; n.acc = on[k].second; n.v = on[k].second ? 0.86f : 0.7f;
                const int nextStep = k + 1 < on.size() ? on[k + 1].first : spb;
                n.d = std::max (6, (int) std::lround (std::min ((double) (nextStep - on[k].first), (double) gate) * st));
                if ((n.acc || strongPos (n.t, spb)) && ! c.H->chordTone (n.p, n.t)) n.p = nearestChordTone (*c.H, n.p, n.t);
                n.l = Link();
                out.push_back (n);
            }
        }
        return out; });
}

std::vector<Note> toolSimpler (const std::vector<Note>& notes, const EditCtx& c)
{
    auto strength = [] (int t) { return t % PPQ == 0 ? 2 : t % (PPQ / 2) == 0 ? 1 : 0; };
    for (int level = 0; level <= 1; ++level)
    {
        std::vector<Note> out; std::vector<bool> ed; bool removed = false;
        for (size_t i = 0; i < notes.size(); ++i)
        {
            const bool e = editable (c, notes, i);
            if (e && strength (notes[i].t) <= level)
            {
                int left = 0; for (size_t j = 0; j < notes.size(); ++j) if (barOf (notes[j].t) == barOf (notes[i].t) && strength (notes[j].t) > level) ++left;
                if (left >= 2) { removed = true; continue; }
            }
            out.push_back (notes[i]); ed.push_back (e);
        }
        if (removed)
        {   // the notes that stay may breathe into the space (locked ones keep their length)
            for (size_t i = 0; i + 1 < out.size(); ++i) if (ed[i]) out[i].d = std::min (std::max (out[i].d, (out[i + 1].t - out[i].t) / 2), out[i + 1].t - out[i].t);
            return out;
        }
    }
    return notes;
}

std::vector<Note> toolBusier (const std::vector<Note>& notes, const EditCtx& c, uint32_t seed)
{
    if (notes.empty()) return notes;
    const int st = BAR / std::max (1, c.spb);
    Rng R (seed);
    std::vector<Note> out = notes; std::map<int, int> perBar; bool added = false;
    for (size_t i = 0; i + 1 < notes.size(); ++i)
    {
        const Note& a = notes[i]; const Note& b = notes[i + 1];
        if (! editable (c, notes, i) || barOf (a.t) != barOf (b.t)) continue;
        const int gap = (b.t - a.t) / st;
        if (gap < 2 || perBar[barOf (a.t)] >= 4) continue;
        Note n = a; n.t = a.t + st * (gap / 2); n.acc = false; n.v = 0.66f; n.lock = false; n.l = Link();
        const int dir = b.p > a.p ? 1 : b.p < a.p ? -1 : (R() < 0.5 ? 1 : -1);
        n.p = latticeMove (c.H->at (n.t), a.p, dir);
        if (n.p == b.p && std::abs (b.p - a.p) > 1) n.p = latticeMove (c.H->at (n.t), a.p, dir);
        n.d = std::max (6, std::min (a.d, (b.t - n.t)));
        out.push_back (n); perBar[barOf (a.t)]++; added = true;
    }
    if (! added)   // already dense: little 32nd ratchets on some accents
    {
        std::vector<Note> extra;
        for (size_t i = 0; i < out.size(); ++i)
            if (editable (c, notes, i) && out[i].acc && R() < 0.5 && out[i].d >= st / 2 + 3)
            { Note n = out[i]; n.t += st / 2; n.d = std::max (6, st / 2 - 2); n.v *= 0.82f; n.acc = false; out[i].d = std::max (6, st / 2 - 2); extra.push_back (n); }
        out.insert (out.end(), extra.begin(), extra.end());
    }
    sortNotes (out);
    return out;
}

std::vector<Note> toolOctave (const std::vector<Note>& notes, const EditCtx& c, int dir)
{
    std::vector<Note> out = notes;
    for (size_t i = 0; i < out.size(); ++i) if (editable (c, notes, i)) { const int q = out[i].p + 12 * dir; if (q >= 24 && q <= 108) out[i].p = q; }
    return out;
}

std::vector<Note> refitToHarmony (const std::vector<Note>& notes, const EditCtx& c)
{
    std::vector<Note> out = notes; int prev = -1;
    for (size_t i = 0; i < out.size(); ++i)
    {
        Note& n = out[i];
        if (n.lock) { prev = n.p; continue; }
        const bool strong = n.acc || strongPos (n.t, c.spb);
        if (strong && ! c.H->chordTone (n.p, n.t) && ! (c.pedalMode && c.H->scaleTone (n.p, n.t) && ! c.H->clash (n.p, n.t)))
            n.p = nearestChordTone (*c.H, n.p, n.t, prev >= 0 ? (n.p >= prev ? 1 : -1) : 0);
        else if (! c.H->scaleTone (n.p, n.t)) n.p = nearestScaleTone (*c.H, n.p, n.t);
        else if (c.H->clash (n.p, n.t) && n.d >= PPQ / 2) n.p = nearestChordTone (*c.H, n.p, n.t);
        prev = n.p;
    }
    return out;
}
} // namespace arp
