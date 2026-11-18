#include "HookLab.h"
#include "UrbanMoods.h"
#include <algorithm>
#include <chrono>
#include <functional>
#include <cmath>
#include <map>
#include <set>
#include <sstream>

namespace arp
{
namespace
{
uint32_t hmix (uint32_t a, uint32_t b)
{
    uint32_t h = a ^ (b + 0x9E3779B9u + (a << 6) + (a >> 2));
    h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
    return h;
}
double u01 (uint32_t seed, uint32_t i) { return (double) (hmix (seed, i * 2654435761u + 17u) >> 8) / 16777216.0; }
bool hasPc (const std::vector<int>& v, int pc) { return std::find (v.begin(), v.end(), pc) != v.end(); }
std::vector<int> tonesIn (const HChord& c, int lo, int hi, bool chordOnly)
{
    std::vector<int> v;
    for (int p = lo; p <= hi; ++p) { const int pc = mod12 (p); if (hasPc (c.pcs, pc) || (! chordOnly && hasPc (c.scale, pc))) v.push_back (p); }
    return v;
}
int nearestIn (const std::vector<int>& v, int p, int dirPref)
{
    if (v.empty()) return p;
    int best = v[0], bd = 1 << 20;
    for (int x : v)
    {
        int d = std::abs (x - p) * 4;
        if (dirPref > 0 && x < p) d += 1; else if (dirPref < 0 && x > p) d += 1;
        if (d < bd) { bd = d; best = x; }
    }
    return best;
}
int nearestCT (const Harmony& H, int p, int t, int dir = 0) { return nearestIn (tonesIn (H.at (t), p - 12, p + 12, true), p, dir); }
int nearestST (const Harmony& H, int p, int t, int dir = 0) { return nearestIn (tonesIn (H.at (t), p - 12, p + 12, false), p, dir); }
int stepScale (const Harmony& H, int p, int t, int steps)   // move by scale (key + chord) steps
{
    auto v = tonesIn (H.at (t), p - 24, p + 24, false);
    if (v.empty()) return p;
    const int base = nearestIn (v, p, 0);
    int i = (int) (std::find (v.begin(), v.end(), base) - v.begin());
    i = clampv (i + steps, 0, (int) v.size() - 1);
    return v[(size_t) i];
}
int stepChord (const Harmony& H, int p, int t, int steps)
{
    auto v = tonesIn (H.at (t), p - 30, p + 30, true);
    if (v.empty()) return p;
    const int base = nearestIn (v, p, 0);
    int i = (int) (std::find (v.begin(), v.end(), base) - v.begin());
    i = clampv (i + steps, 0, (int) v.size() - 1);
    return v[(size_t) i];
}
bool strongT (int t) { return t % PPQ == 0; }
int clampP (int p) { while (p > 100) p -= 12; while (p < 36) p += 12; return p; }

// sorted, inside [0, total), one note per onset, no overlaps unless it's a glide, durations sane
void tidy (std::vector<Note>& n, int total)
{
    sortNotes (n);
    std::vector<Note> o;
    for (auto& x : n)
    {
        if (x.t < 0 || x.t >= total) continue;
        if (! o.empty() && o.back().t == x.t) continue;
        x.p = clampP (x.p); x.d = std::max (6, x.d); o.push_back (x);
    }
    for (size_t i = 0; i < o.size(); ++i)
    {
        const int lim = i + 1 < o.size() ? o[i + 1].t - o[i].t : total - o[i].t;
        if (o[i].l.type != 1 || i + 1 >= o.size()) o[i].d = std::max (6, std::min (o[i].d, lim));
    }
    n = o;
}
std::vector<int> barOnsets (const std::vector<Note>& n, int b)
{
    std::vector<int> v; for (auto& x : n) if (x.t >= b * BAR && x.t < (b + 1) * BAR) v.push_back (x.t - b * BAR);
    return v;
}
EditCtx ctxFor (const Harmony& H, int total, int spb, int mood) { EditCtx c; c.H = &H; c.totalTicks = total; c.spb = spb; c.mood = mood; return c; }
std::vector<Note> refit (const std::vector<Note>& n, const Harmony& H, int total, int spb = 16, int mood = 0) { return refitToHarmony (n, ctxFor (H, total, spb, mood)); }
double fitRange (double x, double lo, double hi, double width)
{
    if (x >= lo && x <= hi) return 1.0;
    const double d = x < lo ? lo - x : x - hi;
    return std::max (0.0, 1.0 - d / std::max (1e-6, width));
}
} // namespace

const char* candyName (int c) { static const char* n[] { "OFF", "SUBTLE", "HOOK", "EUPHORIC", "UNHINGED" }; return n[clampv (c, 0, 4)]; }
const char* styleName (int s) { static const char* n[] { "AUTO", "HYPNOTIC", "ANTHEMIC", "MINIMAL", "EMOTIONAL", "PLAYFUL", "DARK", "TRIUMPHANT", "HYPNO LOOP", "BOOM BAP", "SOULFUL", "LAID BACK", "EMO" }; return n[clampv (s, 0, kStyles - 1)]; }
int baseStyle (int s)
{
    switch (s) { case StyleDark: return StyleMinimal; case StyleTriumphant: return StyleAnthemic; case StyleHypnoLoop: return StyleHypnotic;
                 case StyleBoomBap: return StylePlayful; case StyleSoulful: return StyleEmotional; case StyleLaidBack: return StyleMinimal; case StyleEmo: return StyleEmotional; default: return s; }
}
int styleFlavour (int s, int mood)
{
    if (mood == Trap) return s == StyleDark ? 0 : s == StyleTriumphant ? 1 : s == StyleHypnoLoop ? 2 : s == StyleEmo ? kEmo : -1;   // DARK / TRIUMPHANT / HYPNOTIC / EMO
    if (mood == HipHop) return s == StyleBoomBap ? 0 : s == StyleSoulful ? 4 : s == StyleLaidBack ? 3 : -1;          // BOOM BAP / JAZZ SOUL / UNDERWATER
    return -1;
}
int styleCountFor (int mood) { return mood == Trap ? kClassicStyles + 4 : urbanMood (mood) ? kClassicStyles + 3 : kClassicStyles; }
int styleAt (int mood, int i)
{
    if (i < kClassicStyles) return clampv (i, 0, kClassicStyles - 1);
    if (mood == Trap && i == kClassicStyles + 3) return StyleEmo;
    return clampv ((mood == Trap ? StyleDark : StyleBoomBap) + std::min (2, i - kClassicStyles), 0, kStyles - 1);
}
int styleButton (int mood, int s)
{
    if (s < kClassicStyles) return s;
    if (mood == Trap && s == StyleEmo) return kClassicStyles + 3;
    const int b0 = mood == Trap ? StyleDark : mood == HipHop ? StyleBoomBap : -100; return s >= b0 && s < b0 + 3 ? kClassicStyles + (s - b0) : 0;
}
const char* evolveModeName (int m) { static const char* n[] { "SUBTLE", "GROW", "WAVE", "JOURNEY" }; return n[clampv (m, 0, 3)]; }
const char* arpDirName (int d) { static const char* n[] { "FIGURE", "UP", "DOWN", "UP-DOWN", "RANDOM", "PENDULUM", "CONVERGE" }; return n[clampv (d, 0, 6)]; }
const char* arpContourName (int c) { static const char* n[] { "FLAT", "RISE", "FALL", "ARCH", "VALLEY" }; return n[clampv (c, 0, 4)]; }
const char* arpAccentName (int a) { static const char* n[] { "FIGURE", "DOWNBEAT", "BUILD", "OFFBEAT", "GROW" }; return n[clampv (a, 0, 4)]; }

// ================================================================= features
int medianPitch (const std::vector<Note>& n)
{
    if (n.empty()) return 60;
    std::vector<int> p; for (auto& x : n) p.push_back (x.p);
    std::nth_element (p.begin(), p.begin() + (long) (p.size() / 2), p.end());
    return p[p.size() / 2];
}
double nonChordShare (const std::vector<Note>& n, const Harmony& H)
{
    if (n.empty()) return 0;
    int k = 0; for (auto& x : n) if (! H.chordTone (x.p, x.t)) ++k;
    return (double) k / (double) n.size();
}
bool isResolvedChromatic (const std::vector<Note>& n, size_t i, const Harmony& H)
{
    if (i + 1 >= n.size() || H.scaleTone (n[i].p, n[i].t)) return false;
    const Note& a = n[i]; const Note& b = n[i + 1];
    return b.t - (a.t + a.d) <= PPQ / 4 && b.t > a.t && std::abs (b.p - a.p) == 1 && H.scaleTone (b.p, b.t);
}
int outOfKey (const std::vector<Note>& n, const Harmony& H, bool countResolved)
{
    int k = 0;
    for (size_t i = 0; i < n.size(); ++i) if (! H.scaleTone (n[i].p, n[i].t) && (countResolved || ! isResolvedChromatic (n, i, H))) ++k;
    return k;
}
HookFeatures hookFeatures (const std::vector<Note>& in, const Harmony& H, int span)
{
    HookFeatures f; auto n = in; sortNotes (n);
    if (n.empty()) return f;
    span = std::max (BAR, span);
    const int bars = std::max (1, span / BAR);
    for (auto& x : n) if (x.t < BAR) f.rhythm.push_back (x.t);
    int lo = 127, hi = 0, peakT = 0, steps = 0, leaps = 0, syn = 0; double dur = 0;
    for (size_t i = 0; i < n.size(); ++i)
    {
        if (n[i].p > hi) { hi = n[i].p; peakT = n[i].t; }
        lo = std::min (lo, n[i].p); dur += n[i].d;
        if (n[i].t % (PPQ / 2) != 0) ++syn;
        else if (n[i].t % PPQ != 0 && n[i].t + n[i].d > (n[i].t / PPQ + 1) * PPQ) ++syn;   // an offbeat 8th held over the beat
        if (i > 0)
        {
            const int iv = n[i].p - n[i - 1].p;
            f.contour.push_back (iv > 0 ? 1 : iv < 0 ? -1 : 0);
            if (n[i - 1].t < BAR && f.intervals.size() < 4) f.intervals.push_back (iv);
            if (std::abs (iv) >= 1 && std::abs (iv) <= 2) ++steps;
            if (std::abs (iv) > 4) ++leaps;
        }
    }
    const double ni = std::max<size_t> (1, n.size() - 1);
    f.peakPos = (double) peakT / span; f.density = (double) n.size() / bars; f.range = hi - lo; f.avgDur = dur / n.size() / PPQ;
    f.sync = (double) syn / n.size(); f.leap = leaps / ni; f.step = steps / ni; f.nct = nonChordShare (n, H);
    int rep = 0;
    for (int b = 1; b < bars; ++b) { const auto ob = barOnsets (n, b); for (int a = 0; a < b; ++a) if (! ob.empty() && ob == barOnsets (n, a)) { ++rep; break; } }
    f.repeat = bars > 1 ? (double) rep / (bars - 1) : 0;
    std::vector<bool> on ((size_t) (span / 12 + 1), false);
    for (auto& x : n) for (int t = x.t; t < std::min (span, x.t + x.d); t += 12) on[(size_t) (t / 12)] = true;
    int silent = 0; for (int t = 0; t < span; t += 12) if (! on[(size_t) (t / 12)]) ++silent;
    f.rest = (double) silent / (span / 12);
    return f;
}
double noteShare (const std::vector<Note>& a, const std::vector<Note>& b, int span)
{
    if (a.empty() || b.empty()) return 0;
    std::set<std::pair<int, int>> A; for (auto& x : a) A.insert ({ x.t % span, x.p });
    int k = 0; std::set<std::pair<int, int>> B;
    for (auto& x : b) if (B.insert ({ x.t % span, x.p }).second && A.count ({ x.t % span, x.p })) ++k;
    return (double) k / (double) std::max (A.size(), B.size());
}
double phraseSimilarity (const std::vector<Note>& a0, const std::vector<Note>& b0, int span)
{
    auto a = a0, b = b0; sortNotes (a); sortNotes (b);
    if (a.empty() || b.empty()) return 0;
    std::set<int> A, B; for (auto& x : a) A.insert ((x.t % span) / 24); for (auto& x : b) B.insert ((x.t % span) / 24);
    int inter = 0; for (int x : A) if (B.count (x)) ++inter;
    const double jac = (double) inter / ((double) (A.size() + B.size()) - (double) inter);
    const size_t m = std::max (a.size(), b.size());
    int agree = 0;
    for (size_t i = 1; i < std::min (a.size(), b.size()); ++i)
    {
        const int sa = (a[i].p > a[i - 1].p) - (a[i].p < a[i - 1].p), sb = (b[i].p > b[i - 1].p) - (b[i].p < b[i - 1].p);
        if (sa == sb) ++agree;
    }
    const double con = m > 1 ? (double) agree / (double) (m - 1) : 1.0;
    return 0.5 * jac + 0.5 * con;
}

// ================================================================= EAR CANDY
int candyWindowBars (int candy) { return candy == CandySubtle ? 8 : 4; }
int candyBudget (int candy) { static const int b[] { 0, 1, 1, 2, 3 }; return b[clampv (candy, 0, 4)]; }
const char* candyEventName (int k) { static const char* n[] { "-", "grace", "echo", "octave answer", "stutter", "run-up", "octave pop", "glide", "ratchet", "chromatic slide", "drop" }; return n[clampv (k, 0, 10)]; }

std::vector<Note> applyEarCandy (const std::vector<Note>& in, const Harmony& H, int candy, int total, uint32_t seed, std::vector<CandyEvent>* events)
{
    auto n = in; tidy (n, total);
    if (candy <= CandyOff || n.empty()) return n;
    static const std::vector<int> kinds[] { {}, { 1, 2 }, { 3, 4 }, { 5, 6, 7 }, { 8, 9, 10, 5 } };
    const auto& K = kinds[clampv (candy, 0, 4)];
    const int W = candyWindowBars (candy) * BAR, budget = candyBudget (candy);
    std::vector<Note> add; std::vector<int> used;
    auto gapBefore = [&] (size_t i) { return i == 0 ? n[i].t : n[i].t - (n[i - 1].t + n[i - 1].d); };
    auto gapAfter = [&] (size_t i) { return (i + 1 < n.size() ? n[i + 1].t : total) - (n[i].t + n[i].d); };
    auto tryKind = [&] (size_t i, int kind) -> bool {
        Note& a = n[i];
        switch (kind)
        {
            case 1: {   // grace: a 32nd from the scale neighbour below
                if (gapBefore (i) < 12) { if (i == 0 || n[i - 1].d < 36) return false; n[i - 1].d = a.t - 12 - n[i - 1].t; }
                Note g = a; g.t = a.t - 12; g.d = 12; g.p = stepScale (H, a.p, a.t, -1); g.v = 0.55f; g.acc = false; g.l = {}; add.push_back (g); return true; }
            case 2: {   // echo: the note again, soft, in the gap after it
                if (gapAfter (i) < PPQ / 2) return false;
                Note e = a; e.t = ((a.t + a.d) / 24 + 1) * 24; e.d = 24; e.v = 0.42f; e.acc = false; e.l = {};
                if (e.t + e.d > (i + 1 < n.size() ? n[i + 1].t : total)) return false;
                add.push_back (e); return true; }
            case 3: {   // octave answer
                if (gapAfter (i) < PPQ / 2 || a.p + 12 > 96) return false;
                Note e = a; e.t = ((a.t + a.d + 23) / 24) * 24; e.d = 36; e.p = a.p + 12; e.v = 0.7f; e.l = {};
                if (e.t + 12 > (i + 1 < n.size() ? n[i + 1].t : total)) return false;
                e.d = std::min (e.d, (i + 1 < n.size() ? n[i + 1].t : total) - e.t);
                add.push_back (e); return true; }
            case 4: {   // stutter: the note bounces twice before it holds
                if (a.d < PPQ / 2 + 24) return false;
                for (int k = 0; k < 2; ++k) { Note s = a; s.t = a.t + k * 24; s.d = 20; s.v = 0.62f + 0.08f * k; s.l = {}; add.push_back (s); }
                a.t += 48; a.d -= 48; return true; }
            case 5: {   // run-up: three scale 32nds into the note
                if (gapBefore (i) < 36) { if (i == 0 || n[i - 1].t + 24 > a.t - 36) return false; n[i - 1].d = a.t - 36 - n[i - 1].t; }
                for (int k = 3; k >= 1; --k) { Note r = a; r.t = a.t - 12 * k; r.d = 12; r.p = stepScale (H, a.p, a.t, -k); r.v = 0.5f + 0.07f * (3 - k); r.acc = false; r.l = {}; add.push_back (r); }
                return true; }
            case 6: { if (a.p + 12 > 96) return false; a.p += 12; a.v = std::min (1.0f, a.v + 0.12f); return true; }   // octave pop
            case 7: { if (i == 0 || std::abs (a.p - n[i - 1].p) < 3 || n[i - 1].l.type != 0) return false; n[i - 1].l.type = 1; n[i - 1].l.w = 0.6f; return true; }   // glide in
            case 8: {   // ratchet: 32nd-triplet repeats
                if (a.d < 48) return false;
                for (int k = 0; k < 2; ++k) { Note s = a; s.t = a.t + k * 16; s.d = 14; s.v = 0.55f + 0.1f * k; s.l = {}; add.push_back (s); }
                a.t += 32; a.d -= 32; return true; }
            case 9: {   // chromatic slide: a semitone below, resolved into the note
                if (gapBefore (i) < 24) { if (i == 0 || n[i - 1].t + 24 > a.t - 24) return false; n[i - 1].d = a.t - 24 - n[i - 1].t; }
                if (! H.scaleTone (a.p, a.t)) return false;
                Note c = a; c.t = a.t - 24; c.d = 22; c.p = a.p - 1; c.v = 0.6f; c.acc = false; c.l = {}; add.push_back (c); return true; }
            case 10: { if (a.p - 12 < 48) return false; a.p -= 12; return true; }   // drop
            default: return false;
        }
    };
    int win = 0;
    for (int w0 = 0; w0 < total; w0 += W, ++win)
    {
        // anchors: notes at phrase ends (the last note of each half-window) first, then the highest, then any
        std::vector<size_t> cand;
        for (size_t i = 0; i < n.size(); ++i) if (n[i].t >= w0 && n[i].t < w0 + W) cand.push_back (i);
        if (cand.empty()) continue;
        std::vector<std::pair<double, size_t>> ranked;
        for (size_t i : cand)
        {
            const bool phraseEnd = i + 1 >= n.size() || n[i + 1].t >= w0 + W || (n[i + 1].t - w0) / (W / 2) != (n[i].t - w0) / (W / 2);
            const bool peak = n[i].p == std::max_element (n.begin() + (long) cand.front(), n.begin() + (long) cand.back() + 1, [] (const Note& a, const Note& b) { return a.p < b.p; })->p;
            ranked.push_back ({ (phraseEnd ? 2.0 : 0.0) + (peak ? 1.0 : 0.0) + (strongT (n[i].t) ? 0.4 : 0.0) + u01 (seed, (uint32_t) (i * 7 + 3)), i });
        }
        std::sort (ranked.begin(), ranked.end(), [] (auto& a, auto& b) { return a.first > b.first; });
        int made = 0;
        for (auto& r : ranked)
        {
            if (made >= budget) break;
            const size_t i = r.second;
            bool near = false; for (int u : used) if (std::abs (n[i].t - u) < BAR / 2) near = true;
            if (near) continue;
            const int k0 = (int) (u01 (seed, (uint32_t) (win * 31 + made * 5 + 1)) * (double) K.size());
            for (size_t j = 0; j < K.size(); ++j)
            {
                const int kind = K[(size_t) (k0 + (int) j) % K.size()];
                const int t0 = n[i].t;
                if (tryKind (i, kind)) { used.push_back (t0); ++made; if (events) events->push_back ({ t0, kind }); break; }
            }
        }
    }
    n.insert (n.end(), add.begin(), add.end());
    tidy (n, total);
    return n;
}

// ================================================================= STYLE
double styleFit (const HookFeatures& f, int s)
{
    switch (baseStyle (s))
    {
        case StyleHypnotic:  return (fitRange (f.repeat, 0.6, 1, 0.5) + fitRange (f.range, 0, 9, 6) + fitRange (f.leap, 0, 0.15, 0.3)) / 3;
        case StyleAnthemic:  return (fitRange (f.range, 12, 24, 6) + fitRange (f.avgDur, 0.75, 3, 0.5) + fitRange (f.peakPos, 0.45, 0.85, 0.3)) / 3;
        case StyleMinimal:   return (fitRange (f.density, 0, 3.2, 3) + fitRange (f.rest, 0.3, 1, 0.3)) / 2;
        case StyleEmotional: return (fitRange (f.avgDur, 0.8, 3, 0.5) + fitRange (f.leap, 0.1, 0.35, 0.2) + fitRange (f.peakPos, 0.3, 0.75, 0.3)) / 3;
        case StylePlayful:   return (fitRange (f.sync, 0.25, 1, 0.25) + fitRange (f.avgDur, 0, 0.5, 0.5) + fitRange (f.density, 4.5, 16, 3) + fitRange (f.leap, 0.15, 0.6, 0.2)) / 4;
        default: return 0.5;
    }
}

std::vector<Note> applyStyle (const std::vector<Note>& in, const Harmony& H, int style, int total, uint32_t seed)
{
    auto n = in; tidy (n, total);
    style = baseStyle (style);
    if (style <= StyleAuto || n.empty()) return n;
    const int bars = std::max (1, total / BAR);
    auto nextT = [&] (size_t i) { return i + 1 < n.size() ? n[i + 1].t : total; };
    if (style == StyleHypnotic)
    {   // one cell (1 bar, or 2 when bar 1 is thin) repeated; the last bar keeps its own ending; narrow range
        const int cellBars = barOnsets (n, 0).size() >= 3 || bars < 2 ? 1 : 2, cell = cellBars * BAR;
        std::vector<Note> c0; for (auto& x : n) if (x.t < cell) c0.push_back (x);
        std::vector<Note> o;
        for (int b = 0; b < bars; b += cellBars)
        {
            if (b > 0 && b + cellBars >= bars && bars >= 4) { for (auto& x : n) if (x.t >= b * BAR) o.push_back (x); break; }
            for (auto x : c0) { x.t += b * BAR; if (x.t >= total) continue; x.p = strongT (x.t) ? nearestCT (H, x.p, x.t) : nearestST (H, x.p, x.t); o.push_back (x); }
        }
        n = o; tidy (n, total);
        const int med = medianPitch (n);
        for (auto& x : n) { while (x.p > med + 6) x.p -= 12; while (x.p < med - 6) x.p += 12; }
    }
    else if (style == StyleAnthemic)
    {   // fewer, longer notes; the climax (50..80 %) lifted; a low start
        std::vector<Note> o;
        for (size_t i = 0; i < n.size(); ++i) if (! (n[i].d < PPQ / 2 && n[i].t % (PPQ / 2) != 0) || o.empty()) o.push_back (n[i]);
        n = o;
        for (size_t i = 0; i < n.size(); ++i) n[i].d = std::max (n[i].d, (int) (std::min (nextT (i) - n[i].t, 2 * PPQ) * 0.95));
        for (auto& x : n) if (x.t >= total / 2 && x.t < total * 4 / 5) x.p = nearestCT (H, x.p + 7, x.t, 1);
        if (! n.empty()) n[0].p = nearestCT (H, n[0].p - 5, n[0].t, -1);
        int lo = 127, hi = 0; for (auto& x : n) { lo = std::min (lo, x.p); hi = std::max (hi, x.p); }
        if (hi - lo < 12) for (auto& x : n) if (x.p == hi && x.p + 12 <= 96) { x.p += 12; break; }
    }
    else if (style == StyleMinimal)
    {   // the first note of each bar + a few beat notes; at most 3 a bar, long notes, space
        std::vector<Note> o;
        for (int b = 0; b < bars; ++b)
        {
            int k = 0;
            for (size_t i = 0; i < n.size(); ++i)
            {
                if (n[i].t < b * BAR || n[i].t >= (b + 1) * BAR) continue;
                const bool first = k == 0;
                if (k < 3 && (first || (strongT (n[i].t) && u01 (seed, (uint32_t) i) < 0.55))) { o.push_back (n[i]); ++k; }
            }
        }
        n = o;
        for (size_t i = 0; i < n.size(); ++i) n[i].d = std::max (n[i].d, (int) (std::min (nextT (i) - n[i].t, 2 * PPQ) * 0.75));
    }
    else if (style == StyleEmotional)
    {   // long strong-beat notes, a leap up into the peak, a falling tail
        for (size_t i = 0; i < n.size(); ++i) if (strongT (n[i].t)) n[i].d = std::max (n[i].d, (int) (std::min (nextT (i) - n[i].t, 2 * PPQ) * 0.95));
        size_t pk = 0; for (size_t i = 1; i < n.size(); ++i) if (n[i].p > n[pk].p) pk = i;
        if (pk > 0) n[pk - 1].p = nearestST (H, n[pk].p - 9, n[pk - 1].t, -1);
        for (size_t i = pk + 1; i < n.size(); ++i) if (n[i].t >= total - BAR && n[i].p >= n[i - 1].p) n[i].p = stepScale (H, n[i - 1].p, n[i].t, -1);
        // a slower line: drop short weak notes that sit between two longer ones
        std::vector<Note> o;
        for (size_t i = 0; i < n.size(); ++i)
        {
            const bool shortWeak = n[i].d < PPQ / 2 && ! strongT (n[i].t) && i > 0 && i + 1 < n.size() && u01 (seed, (uint32_t) (i + 99)) < 0.6;
            if (! shortWeak) o.push_back (n[i]);
        }
        n = o;
        for (size_t i = 0; i < n.size(); ++i) n[i].d = std::max (n[i].d, (int) ((nextT (i) - n[i].t) * 0.9));
    }
    else if (style == StylePlayful)
    {   // staccato, pushed beats, split long notes, octave bounces
        std::vector<Note> o;
        for (size_t i = 0; i < n.size(); ++i)
        {
            Note x = n[i];
            const int gap = nextT (i) - x.t;
            if (strongT (x.t) && (x.t / PPQ) % 2 == 1 && u01 (seed, (uint32_t) i) < 0.8 && (o.empty() || o.back().t < x.t - PPQ / 4)) x.t -= PPQ / 4;   // push
            else if (x.t % PPQ == PPQ / 2 && u01 (seed, (uint32_t) (i + 300)) < 0.5 && x.t + PPQ / 4 < nextT (i)) x.t += PPQ / 4;   // skip onto the 16th
            if (! strongT (x.t) && u01 (seed, (uint32_t) (i + 50)) < 0.3) x.p = x.p + 12 <= 93 ? x.p + 12 : x.p - 12;   // bounce
            x.d = std::min (x.d, PPQ / 3); x.l = {};
            o.push_back (x);
            if (gap >= PPQ) { Note y = x; y.t = n[i].t + PPQ / 2 + (u01 (seed, (uint32_t) (i + 7)) < 0.5 ? PPQ / 4 : 0); y.d = PPQ / 4; y.p = (i % 2) ? stepScale (H, x.p, y.t, 1) : x.p; y.v = 0.66f; y.acc = false; o.push_back (y); }
        }
        n = o;
    }
    tidy (n, total);
    // pitches the shape moved: scale tones everywhere, chord tones on strong beats (the tension knob adds colour later)
    for (auto& x : n) x.p = strongT (x.t) && ! H.chordTone (x.p, x.t) && ! H.scaleTone (x.p, x.t) ? nearestCT (H, x.p, x.t) : (! H.scaleTone (x.p, x.t) ? nearestST (H, x.p, x.t) : x.p);
    return n;
}

// ================================================================= TENSION
std::vector<Note> applyTension (const std::vector<Note>& in, const Harmony& H, double tension, uint32_t seed)
{
    auto n = in; sortNotes (n);
    if (tension < 0 || n.empty()) return n;
    tension = clampv (tension, 0.0, 1.0);
    const double share = 0.6 * tension;
    std::vector<Note> o; o.reserve (n.size() + 8);
    for (size_t i = 0; i < n.size(); ++i)
    {
        Note x = n[i];
        const double u = u01 (seed, (uint32_t) i), v = u01 (seed ^ 0x51ED, (uint32_t) i);
        const int prevP = o.empty() ? x.p : o.back().p, nextP = i + 1 < n.size() ? n[i + 1].p : x.p;
        const int dir = nextP > prevP ? 1 : nextP < prevP ? -1 : 0;
        if (u >= share) { if (! H.chordTone (x.p, x.t)) x.p = nearestCT (H, x.p, x.t, dir); o.push_back (x); continue; }
        // a colour note
        const HChord& c = H.at (x.t);
        const int ct = H.chordTone (x.p, x.t) ? x.p : nearestCT (H, x.p, x.t, dir);
        auto nctNear = [&] (int from, int d) { int p = from; for (int k = 0; k < 4; ++k) { p = stepScale (H, p, x.t, d); if (! H.chordTone (p, x.t)) return p; } return from + d; };
        if (strongT (x.t) && x.d >= PPQ / 2 && tension >= 0.3 && v < 0.6)
        {   // suspension / appoggiatura: the step above, resolved down to the chord tone halfway through
            Note a = x; a.p = nctNear (ct, 1); a.d = x.d / 2; o.push_back (a);
            Note r = x; r.t = x.t + x.d / 2; r.d = x.d - x.d / 2; r.p = ct; r.acc = false; o.push_back (r);
            continue;
        }
        if (strongT (x.t) && x.d >= PPQ && tension >= 0.5)
        {   // extension: the 9th or the 7th over the chord
            const int r9 = mod12 (c.root + 2), r7 = hasPc (c.scale, mod12 (c.root + 10)) ? mod12 (c.root + 10) : mod12 (c.root + 11);
            const int pc = ! hasPc (c.pcs, r9) && hasPc (c.scale, r9) ? r9 : r7;
            int best = x.p, bd = 99; for (int p = ct - 7; p <= ct + 7; ++p) if (mod12 (p) == pc && std::abs (p - ct) < bd) { bd = std::abs (p - ct); best = p; }
            if (! H.chordTone (best, x.t)) { x.p = best; o.push_back (x); continue; }
        }
        // passing / neighbour tone toward the next note
        x.p = nctNear (ct, dir >= 0 ? 1 : -1);
        o.push_back (x);
    }
    if (tension > 0.6)
    {   // resolved chromatic: a weak note just before a chord tone becomes the semitone below it
        const double cs = (tension - 0.6) * 1.2;
        for (size_t i = 0; i + 1 < o.size(); ++i)
        {
            if (strongT (o[i].t) || u01 (seed ^ 0xC4u, (uint32_t) i) >= cs) continue;
            const Note& b = o[i + 1];
            if (! H.chordTone (b.p, b.t) || b.t - (o[i].t + o[i].d) > PPQ / 4) continue;
            if (H.scaleTone (b.p - 1, o[i].t)) continue;   // the semitone below is diatonic: nothing chromatic to add
            o[i].p = b.p - 1;
        }
    }
    sortNotes (o);
    return o;
}
std::vector<Note> keyGuard (const std::vector<Note>& in, const Harmony& H, bool allowChromatic)
{
    auto n = in; sortNotes (n);
    for (size_t i = 0; i < n.size(); ++i)
        if (! H.scaleTone (n[i].p, n[i].t) && ! (allowChromatic && isResolvedChromatic (n, i, H))) n[i].p = nearestST (H, n[i].p, n[i].t);
    return n;
}

// ================================================================= lane relationships
std::vector<Note> separateRegister (const std::vector<Note>& mel, const std::vector<Note>& arpN, int gap)
{
    if (mel.empty() || arpN.empty()) return mel;
    const int am = medianPitch (arpN), mm = medianPitch (mel);
    if (std::abs (mm - am) >= gap) return mel;
    int lo = 127, hi = 0; for (auto& x : mel) { lo = std::min (lo, x.p); hi = std::max (hi, x.p); }
    int shift = 0;
    for (int k = 1; k <= 2; ++k) if (mm + 12 * k - am >= gap && hi + 12 * k <= 100) { shift = 12 * k; break; }
    if (shift == 0) for (int k = 1; k <= 2; ++k) if (am - (mm - 12 * k) >= gap && lo - 12 * k >= 36) { shift = -12 * k; break; }
    auto o = mel; for (auto& x : o) x.p += shift;
    return o;
}
const char* registerName (int r) { static const char* n[] { "LOW", "MID", "HIGH" }; return n[clampv (r, 0, 2)]; }
RegWindow registerWindow (int reg, int oct)
{
    static const RegWindow W[] { { 58, 68, 81 }, { 67, 78, 88 }, { 75, 84, 88 } };
    RegWindow w = W[clampv (reg, 0, 2)];
    oct = clampv (oct, -2, 2); w.lo += 12 * oct; w.hi += 12 * oct; w.top += 12 * oct;   // the user's OCT +/- moves the window (and the ceiling)
    return w;
}
std::vector<Note> placeRegister (const std::vector<Note>& mel, const std::vector<Note>& arpN, int reg, int oct)
{
    if (mel.empty()) return mel;
    const RegWindow w = registerWindow (reg, oct);
    const int mm = medianPitch (mel), am = arpN.empty() ? -100 : medianPitch (arpN);
    int lo = 127, hi = 0; for (auto& x : mel) { lo = std::min (lo, x.p); hi = std::max (hi, x.p); }
    int best = 0; double bc = 1e18;
    for (int sh : { 0, -12, 12, -24, 24 })
    {
        const int m = mm + sh;
        double c = (m < w.lo ? w.lo - m : m > w.hi ? m - w.hi : 0) * 3.0;      // the median inside the window
        c += std::max (0, hi + sh - w.top) * 4.0 + std::max (0, 43 - (lo + sh)) * 4.0;   // no screaming tops, no mud
        if (am > 0 && std::abs (m - am) < 7) c += 2.5;                          // apart from the arp when it costs nothing
        c += std::abs (sh) / 12 * 0.5;
        if (c < bc) { bc = c; best = sh; }
    }
    auto o = mel;
    for (auto& x : o) { x.p += best; while (x.p > w.top && x.p - 12 >= 36) x.p -= 12; while (x.p < 36) x.p += 12; }   // a candy pop above the ceiling folds down
    return o;
}
double collisionShare (const std::vector<Note>& mel, const std::vector<Note>& arpN)
{
    if (mel.empty()) return 0;
    std::set<int> on; for (auto& x : arpN) on.insert (x.t);
    int k = 0; for (auto& x : mel) if (on.count (x.t)) ++k;
    return (double) k / (double) mel.size();
}
std::vector<Note> complementRhythm (const std::vector<Note>& mel, const std::vector<Note>& arpN, const Harmony& H, int spb)
{
    auto n = mel; sortNotes (n);
    if (n.empty() || arpN.empty()) return n;
    std::set<int> on; for (auto& x : arpN) on.insert (x.t);
    const int st = std::max (12, BAR / std::max (8, spb == 8 ? 16 : spb));
    std::set<int> mine; for (auto& x : n) mine.insert (x.t);
    for (size_t i = 0; i < n.size(); ++i)
    {
        Note& x = n[i];
        if (! on.count (x.t) || strongT (x.t)) continue;   // the beat is shared on purpose
        const int lo = i > 0 ? n[i - 1].t + 12 : 0, hi = i + 1 < n.size() ? n[i + 1].t - 12 : x.t + x.d;
        for (int d : { st, -st })
        {
            const int t = x.t + d;
            if (t < lo || t > hi || on.count (t) || mine.count (t)) continue;
            mine.erase (x.t); x.t = t; mine.insert (t); x.d = std::max (12, x.d - std::max (0, d)); break;
        }
    }
    sortNotes (n);
    for (auto& x : n) if (! H.scaleTone (x.p, x.t)) x.p = nearestST (H, x.p, x.t);
    return n;
}
std::vector<Note> ensureRests (const std::vector<Note>& mel, int total)
{
    auto n = mel; sortNotes (n);
    for (int w0 = 0; w0 < total; w0 += 2 * BAR)
    {
        std::vector<size_t> ix; for (size_t i = 0; i < n.size(); ++i) if (n[i].t >= w0 && n[i].t < w0 + 2 * BAR) ix.push_back (i);
        if (ix.size() < 2) continue;
        int bestGap = 0;
        for (size_t k = 0; k < ix.size(); ++k)
        {
            const size_t i = ix[k];
            const int nt = i + 1 < n.size() ? n[i + 1].t : total;
            bestGap = std::max (bestGap, nt - (n[i].t + n[i].d));
        }
        if (bestGap >= PPQ / 2) continue;
        // breathe: shorten the longest note of the window (it keeps its onset), else the last one
        size_t li = ix.back(); for (size_t i : ix) if (n[i].d > n[li].d) li = i;
        n[li].d = std::max (12, n[li].d - (PPQ / 2 - bestGap)); n[li].l = {};
    }
    return n;
}

// ================================================================= IDM + FEEL
const char* meterName (int m) { static const char* n[] { "4/4", "3/4", "5/4", "7/8", "9/8" }; return n[clampv (m, 0, 4)]; }
int meterSteps (int m) { static const int n[] { 16, 12, 20, 14, 18 }; return n[clampv (m, 0, 4)]; }
std::vector<int> meterGroups (int m)
{
    switch (clampv (m, 0, 4))
    {
        case Meter34: return { 4, 4, 4 };
        case Meter54: return { 4, 4, 4, 4, 4 };
        case Meter78: return { 4, 4, 6 };
        case Meter98: return { 4, 4, 4, 6 };
        default:      return { 4, 4, 4, 4 };
    }
}
const char* feelName (int f) { static const char* n[] { "NORMAL", "HALF-TIME", "DROP" }; return n[clampv (f, 0, 2)]; }
std::vector<bool> euclid (int hits, int steps, int rot)
{   // Bresenham form of Bjorklund: hits spread as evenly as possible over steps, rotated
    steps = std::max (1, steps); hits = clampv (hits, 0, steps);
    std::vector<bool> v ((size_t) steps, false);
    for (int i = 0; i < steps; ++i) v[(size_t) i] = ((i * hits) % steps) < hits;
    std::vector<bool> o ((size_t) steps);
    rot = ((rot % steps) + steps) % steps;
    for (int i = 0; i < steps; ++i) o[(size_t) ((i + rot) % steps)] = v[(size_t) i];
    return o;
}
namespace
{
int pitchAtT (const std::vector<Note>& n, int t)
{   // what the line sounds at t (or the nearest note)
    int best = n.empty() ? 72 : n[0].p, bd = 1 << 30;
    for (auto& x : n) { if (x.t <= t && t < x.t + std::max (12, x.d)) return x.p; const int d = std::abs (x.t - t); if (d < bd) { bd = d; best = x.p; } }
    return best;
}
std::vector<Note> fitTo (std::vector<Note> o, const Harmony& H)
{   // pitches onto the chords under them: chord tones on the beat, scale tones between
    for (auto& x : o) x.p = strongT (x.t) ? nearestCT (H, x.p, x.t) : nearestST (H, x.p, x.t);
    return o;
}
}
std::vector<Note> meterAccents (const std::vector<Note>& in, int meter)
{
    auto n = in; if (meter <= Meter44) return n;
    const int cyc = meterSteps (meter) * 24; const auto g = meterGroups (meter);
    std::set<int> starts; int a = 0; for (int k : g) { starts.insert (a * 24); a += k; }
    for (auto& x : n) { const int ph = x.t % cyc; const bool on = starts.count (ph) > 0; x.acc = on; x.v = on ? std::max (x.v, ph == 0 ? 0.92f : 0.84f) : std::min (x.v, 0.68f); }
    return n;
}
std::vector<Note> meterLine (const std::vector<Note>& src, const Harmony& H, int meter, int total)
{
    if (meter <= Meter44 || src.empty()) return src;
    const int cyc = meterSteps (meter) * 24;
    std::vector<Note> cell; for (auto x : src) if (x.t < cyc) { x.d = std::min (x.d, cyc - x.t); cell.push_back (x); }
    if (cell.size() < 2) { cell.clear(); for (auto x : src) if (x.t < 2 * cyc) { x.t = x.t / 2; x.d = std::max (12, x.d / 2); cell.push_back (x); } }
    std::vector<Note> o;
    for (int b = 0; b < total; b += cyc) for (auto x : cell) { x.t += b; if (x.t < total) { x.d = std::min (x.d, total - x.t); o.push_back (x); } }
    o = fitTo (o, H);   // each cycle lands on other chords (it phases against the 4/4 bars)
    tidy (o, total);
    return meterAccents (o, meter);
}
std::vector<Note> euclidLine (const std::vector<Note>& src, const Harmony& H, int hits, int steps, int rot, int total)
{
    if (hits <= 0 || src.empty()) return src;
    const auto e = euclid (hits, steps, rot); const int st = 24, cyc = steps * st;
    std::vector<Note> o;
    for (int b = 0; b < total; b += cyc)
    {
        std::vector<int> on; for (int i = 0; i < steps; ++i) if (e[(size_t) i] && b + i * st < total) on.push_back (b + i * st);
        for (size_t k = 0; k < on.size(); ++k)
        {
            Note x; x.t = on[k]; const int next = k + 1 < on.size() ? on[k + 1] : std::min (total, b + cyc + (e[0] ? 0 : st));
            x.d = clampv (next - x.t - 6, 12, 2 * PPQ); x.p = pitchAtT (src, x.t); x.v = k == 0 ? 0.9f : 0.74f;
            o.push_back (x);
        }
    }
    o = fitTo (o, H);
    for (size_t i = 1; i + 1 < o.size(); ++i) if (o[i].p == o[i - 1].p && o[i].p == o[i + 1].p) o[i].p = stepScale (H, o[i].p, o[i].t, 1);   // no three in a row
    tidy (o, total);
    return o;
}
std::vector<Note> halfTimeLine (const std::vector<Note>& src, const Harmony& H, int total)
{
    std::vector<Note> o;
    for (auto x : src) if (x.t < total / 2) { x.t *= 2; x.d = std::min (x.d * 2, 3 * PPQ); x.t = (x.t / 24) * 24; o.push_back (x); }
    o = fitTo (o, H);
    tidy (o, total);
    return o;
}
std::vector<Note> dropLine (const std::vector<Note>& src, const Harmony& H, int total, uint32_t seed)
{   // the hook as a drop: a 3-3-2 kick grid (dotted 8ths), one note per slot, chord tones, wider leaps, a register lower
    static const int slots[] { 0, 3, 6, 8, 11, 14 };   // 16ths of a bar
    std::vector<Note> o; int prev = -1;
    const int c = medianPitch (src) - 12;   // a register lower, and the leaps stay inside an octave and a half around it
    for (int b = 0; b < total; b += BAR)
        for (int k = 0; k < 6; ++k)
        {
            const int t = b + slots[k] * 24;
            if (t >= total) break;
            if (k == 5 && u01 (seed, (uint32_t) (b / BAR)) < 0.5) continue;   // breathe at the bar end now and then
            Note x; x.t = t; x.d = (k == 2 ? 2 : 3) * 24 - 6; x.v = k == 0 || k == 3 ? 0.95f : 0.8f;
            int p = nearestCT (H, pitchAtT (src, t) - 12, t);
            if (prev >= 0 && std::abs (p - prev) < 5) p = stepChord (H, p, t, (u01 (seed, (uint32_t) t) < 0.5 ? 2 : -2));   // leaps, not steps
            while (p > c + 9) p -= 12; while (p < c - 9) p += 12;
            if (prev >= 0 && p == prev) p = stepChord (H, p, t, p > c ? -1 : 1);
            x.p = p; prev = p; o.push_back (x);
        }
    tidy (o, total);
    return o;
}
std::vector<Note> euclidArp (const std::vector<Note>& in, const Harmony& H, int hits, int steps, int rot, int st, int total)
{
    if (hits <= 0 || in.empty()) return in;
    auto n = in; sortNotes (n);
    const auto e = euclid (hits, steps, rot); st = std::max (6, st); const int cyc = steps * st;
    std::vector<Note> o;
    for (auto& x : n) { const int ph = (x.t % cyc) / st; if (x.t % st == 0 && e[(size_t) clampv (ph, 0, steps - 1)]) o.push_back (x); }
    if (o.size() < 2) return in;
    return cleanArp (o, H);
}
std::vector<Note> shapeArpIdm (const Idea& I, const std::vector<Note>& in)
{
    auto n = in;
    const int steps = meterSteps (I.arpMeter) * std::max (1, I.arpSpb) / 16;   // the meter's cycle in arp steps
    if (I.arpEuclid > 0) n = euclidArp (n, I.H, I.arpEuclid, std::max (2, steps), I.arpEuclidRot, BAR / std::max (1, I.arpSpb), I.total);
    if (I.arpMeter > Meter44) n = meterAccents (n, I.arpMeter);
    return n;
}

// ================================================================= RESPONSE
std::vector<Note> generateResponse (const std::vector<Note>& line, const Harmony& H, int c0, int c1, int d0, double sim, uint32_t seed)
{
    std::vector<Note> call; for (auto& x : line) if (x.t >= c0 && x.t < c1) call.push_back (x);
    sortNotes (call);
    const int len = c1 - c0;
    if (call.empty() || len <= 0) return {};
    sim = clampv (sim, 0.0, 1.0);
    std::vector<Note> o; int prev = call[0].p;
    const int cm = medianPitch (call);   // the answer stays in the call's register (mirrored intervals must not walk away)
    for (size_t i = 0; i < call.size(); ++i)
    {
        Note x = call[i]; int t = x.t - c0;
        if (u01 (seed, (uint32_t) i) >= sim)
        {
            if (i > 0 && u01 (seed ^ 0xD0D0, (uint32_t) i) < 0.45) { continue; }   // dropped
            t = clampv (((t + (u01 (seed ^ 0xBEEF, (uint32_t) i) < 0.5 ? -PPQ / 2 : PPQ / 2)) / 24) * 24, 0, len - 24);
        }
        int p;
        if (i == 0) p = u01 (seed, 999) < sim ? call[0].p : call[0].p + (u01 (seed, 998) < 0.5 ? -3 : 4);
        else
        {
            int iv = call[i].p - call[i - 1].p;
            if (u01 (seed ^ 0xC0C0, (uint32_t) i) >= sim) iv = -iv;   // mirrored
            p = prev + iv;
            while (p > cm + 7) p -= 12;
            while (p < cm - 7) p += 12;
        }
        x.t = d0 + t; x.d = std::min (x.d, len - t); x.p = clampP (p); x.lock = false;
        o.push_back (x); prev = x.p;
    }
    tidy (o, d0 + len);
    for (auto& x : o) x.p = strongT (x.t) ? nearestCT (H, x.p, x.t) : nearestST (H, x.p, x.t);
    if (sim < 0.999 && ! o.empty())
    {   // the answer comes home: the last note on the tonic when it is in the chord, else the nearest chord tone
        Note& e = o.back(); int best = e.p, bd = 99;
        for (int p = e.p - 7; p <= e.p + 7; ++p) if (mod12 (p) == mod12 (H.key) && H.chordTone (p, e.t) && std::abs (p - e.p) < bd) { bd = std::abs (p - e.p); best = p; }
        e.p = bd < 99 ? best : nearestCT (H, e.p, e.t);
    }
    return o;
}

// ================================================================= ARP shape
std::vector<Note> cleanArp (const std::vector<Note>& in, const Harmony& H)
{   // span <= 15, no same note twice in a row, none across the loop wrap
    auto n = in; sortNotes (n);
    if (n.size() < 2) return n;
    std::vector<int> ps; for (auto& x : n) ps.push_back (x.p);
    std::sort (ps.begin(), ps.end());
    int w0 = ps.front(), bestN = -1;   // the 15-semitone window that holds the most notes
    for (int lo : ps) { const int c = (int) (std::upper_bound (ps.begin(), ps.end(), lo + 15) - std::lower_bound (ps.begin(), ps.end(), lo)); if (c > bestN) { bestN = c; w0 = lo; } }
    for (auto& x : n) { while (x.p > w0 + 15) x.p -= 12; while (x.p < w0) x.p += 12; }
    const size_t N = n.size();
    for (int pass = 0; pass < 3; ++pass)
    {
        bool clean = true;
        for (size_t i = 0; i < N; ++i)
        {
            const size_t pi = (i + N - 1) % N, ni = (i + 1) % N;
            if (n[i].p != n[pi].p || (i == 0 && n[N - 1].t == n[0].t)) continue;
            clean = false;
            for (int k : { 1, -1, 2, -2, 3, -3 })
            {
                const int q = stepChord (H, n[i].p, n[i].t, k);
                if (q != n[pi].p && q != n[ni].p && q >= w0 && q <= w0 + 15) { n[i].p = q; break; }
            }
        }
        if (clean) break;
    }
    return n;
}
std::vector<Note> shapeArp (const std::vector<Note>& in, const Harmony& H, int dir, int contour, int accent, int figLen, int cycle, uint32_t seed)
{
    auto n = in; sortNotes (n);
    if (n.empty()) return n;
    figLen = clampv (figLen, 2, 8); cycle = std::max (BAR, cycle);
    if (dir > DirFigure)
    {
        size_t i = 0; int chunk = 0;
        while (i < n.size())
        {
            const int ct0 = H.at (n[i].t).t; size_t j = i;
            while (j < n.size() && j - i < (size_t) figLen && H.at (n[j].t).t == ct0 && (n[j].t / cycle) == (n[i].t / cycle)) ++j;
            // the pedal (a pitch that repeats a lot) keeps its place; the rest is reordered
            std::map<int, int> cnt; for (size_t k = i; k < j; ++k) cnt[n[k].p]++;
            int pedal = -1; for (auto& c : cnt) if (c.second >= 2 && c.second * 3 >= (int) (j - i) && (j - i) >= 4) pedal = c.first;
            std::vector<size_t> slots; std::vector<int> ps;
            for (size_t k = i; k < j; ++k) if (n[k].p != pedal) { slots.push_back (k); ps.push_back (n[k].p); }
            std::sort (ps.begin(), ps.end());
            if (std::adjacent_find (ps.begin(), ps.end()) != ps.end() && dir != DirRandom)
            {   // a figure that revisits a note: the classic ladder (the distinct notes, then on up the chord) instead of a note played twice
                std::vector<int> u; for (int q : ps) if (u.empty() || u.back() != q) u.push_back (q);
                while (u.size() < ps.size()) { const int q = stepChord (H, u.back(), n[i].t, 1); if (q <= u.back() || q - u.front() > 15) break; u.push_back (q); }
                for (int k = 0; u.size() < ps.size(); ++k) { const int q = stepChord (H, u.front(), n[i].t, -1); if (q >= u.front() || u.back() - q > 15) break; u.insert (u.begin(), q); }
                if (u.size() == ps.size()) ps = u;
            }
            const size_t m = ps.size(); std::vector<int> r;
            if (dir == DirUp) r = ps;
            else if (dir == DirDown) r.assign (ps.rbegin(), ps.rend());
            else if (dir == DirUpDown) { for (size_t k = 0; k < m; k += 2) r.push_back (ps[k]); for (size_t k = (m % 2 ? m - 2 : m - 1); k < m; k -= 2) { r.push_back (ps[k]); if (k < 2) break; } }
            else if (dir == DirRandom) { r = ps; for (size_t k = m; k > 1; --k) std::swap (r[k - 1], r[(size_t) (u01 (seed, (uint32_t) (chunk * 17 + (int) k)) * (double) k)]); }
            else if (dir == DirPendulum) { const int mid = (int) m / 2; r.push_back (ps[(size_t) mid]); for (int k = 1; (int) r.size() < (int) m; ++k) { if (mid + k < (int) m) r.push_back (ps[(size_t) (mid + k)]); if (mid - k >= 0 && (int) r.size() < (int) m) r.push_back (ps[(size_t) (mid - k)]); } }
            else if (dir == DirConverge) { size_t a = 0, b = m ? m - 1 : 0; bool lo = true; while (r.size() < m) { r.push_back (lo ? ps[a++] : ps[b--]); lo = ! lo; } }
            for (size_t k = 0; k < slots.size() && k < r.size(); ++k) n[slots[k]].p = r[k];
            i = j; ++chunk;
        }
    }
    if (contour > ContFlat)
    {   // lift the figure up the chord by the contour, inside the arp's 15-semitone window (a note that would leave it takes the top chord tone)
        int lo = 999; for (auto& x : n) lo = std::min (lo, x.p);
        const int top = lo + 15;
        for (auto& x : n)
        {
            const double u = (double) (x.t % cycle) / cycle;
            double s = 0;   // 0..1, one-sided (up), so the window holds
            if (contour == ContRise) s = u; else if (contour == ContFall) s = 1 - u;
            else if (contour == ContArch) s = std::sin (u * 3.14159265); else if (contour == ContValley) s = 1 - std::sin (u * 3.14159265);
            const int k = (int) std::lround (s * 2.4);
            if (k == 0) continue;
            int q = clampv (stepChord (H, x.p, x.t, k), 36, 100);
            for (int g = 0; q > top && g < 8; ++g) { const int d = stepChord (H, q, x.t, -1); if (d >= q) break; q = d; }
            if (q <= top && q > x.p) x.p = q;
        }
    }
    if (dir > DirFigure || contour > ContFlat) n = cleanArp (n, H);   // the shaping may never break the arp's hard rules
    if (accent > AccFigure)
    {
        int idx = 0, lastChunkT = -1;
        for (auto& x : n)
        {
            const double u = (double) (x.t % cycle) / cycle;
            const int pos = x.t % BAR;
            float v = 0.64f; bool acc = false;
            if (accent == AccDownbeat) { acc = pos % PPQ == 0; v = pos == 0 ? 0.92f : acc ? 0.8f : 0.58f; }
            else if (accent == AccBuild) { v = (float) (0.48 + 0.44 * u); acc = u > 0.75 && pos % PPQ == 0; }
            else if (accent == AccOffbeat) { acc = pos % PPQ == PPQ / 2; v = acc ? 0.9f : 0.6f; }
            else if (accent == AccGrow)
            {   // one more accented step in every bar of the cycle
                if (x.t / (BAR / 4) != lastChunkT) { lastChunkT = x.t / (BAR / 4); idx = 0; }
                const int barIn = (x.t % cycle) / BAR;
                acc = idx < 1 + barIn % figLen; v = acc ? 0.88f : 0.6f; ++idx;
            }
            x.v = v; x.acc = acc;
        }
    }
    return n;
}
double arpEnergy (int mode, int t, int total)
{
    const double u = clampv ((double) t / std::max (1, total), 0.0, 1.0);
    const double barU = (double) (t / BAR) / std::max (1, total / BAR - 1);   // per bar, so a bar keeps one level (like a producer's automation)
    switch (mode)
    {
        case EvolveGrow: { const double x = clampv (barU, 0.0, 1.0); return 0.12 + 0.88 * x * x * (3 - 2 * x); }
        case EvolveWave: return 0.5 - 0.5 * std::cos (2 * 3.14159265358979 * (double) (t / BAR % 8) / 8.0);
        case EvolveJourney: { const double x = clampv (barU, 0.0, 1.0); return x < 0.7 ? 0.2 + 0.8 * std::sin (x / 0.7 * 1.5707963) : 0.2 + 0.8 * std::cos ((x - 0.7) / 0.3 * 1.5707963); }
        default: (void) u; return 1.0;
    }
}
namespace
{
double gateK (double e) { return 0.8 + 0.2 * e; }
double velK (double e) { return 0.35 + 0.7 * e; }
std::vector<Note> evolveArpCurve (const std::vector<Note>& in, const std::function<double (int)>& energyAt, int figTicks, int rs)
{
    auto n = in; figTicks = std::max (1, figTicks); rs = std::max (figTicks, rs);
    for (auto& x : n)
    {
        const double e = energyAt (x.t);
        x.v = (float) clampv (x.v * velK (e), 0.12, 1.0);                                   // level: the synth adds brightness + space from the same curve (arpEnergyAt)
        x.d = std::max (std::min (x.d, 12), (int) std::lround (x.d * gateK (e)));          // gate 80..100 % of the figure's: dark, never clicky; never longer (no new overlaps)
        if (e > 0.78 && (x.t % rs) % figTicks == 0) { x.acc = true; x.v = std::min (1.0f, x.v + 0.06f); }
    }
    return n;
}
}
std::vector<Note> evolveArp (const std::vector<Note>& in, int total, int mode, int figTicks, int rs)
{
    if (mode <= EvolveClassic || in.empty()) return in;
    return evolveArpCurve (in, [&] (int t) { return arpEnergy (mode, t, total); }, figTicks, rs);
}
double arpEnergyAt (const Idea& I, int t)
{
    if (! I.energy.empty()) { const size_t b = (size_t) (std::max (0, t) / BAR) % I.energy.size(); return clampv ((double) I.energy[b], 0.0, 1.0); }
    return arpEnergy (I.evolveMode, t, I.total);
}
void setArpEnergy (Idea& I, const std::vector<float>& curve)
{   // the arp's notes carry the old curve (level, gate): swap it for the new one without recomposing (the user's arp edits stay)
    std::vector<double> before; for (auto& x : I.lanes[LaneArp].notes) before.push_back (arpEnergyAt (I, x.t));
    I.energy = curve; for (auto& v : I.energy) v = clampv (v, 0.0f, 1.0f);
    if (! arpEvolving (I)) return;
    const int figT = std::max (1, I.fig.len) * (BAR / std::max (1, I.arpSpb)), rs = std::max (figT, arpResyncTicks (I));
    auto& ns = I.lanes[LaneArp].notes;
    for (size_t i = 0; i < ns.size(); ++i)
    {
        auto& x = ns[i]; const double eo = before[i], en = arpEnergyAt (I, x.t);
        if (x.acc && eo > 0.78) x.v = std::max (0.12f, x.v - 0.06f);
        x.v = (float) clampv (x.v / velK (eo) * velK (en), 0.12, 1.0);
        const int d0 = (int) std::lround (x.d / gateK (eo)); x.d = std::max (std::min (d0, 12), (int) std::lround (d0 * gateK (en)));
        if (en > 0.78 && (x.t % rs) % figT == 0) { x.acc = true; x.v = std::min (1.0f, x.v + 0.06f); }
    }
}
int arpResyncTicks (const Idea& I)
{
    const int loop = std::max (BAR, I.H.loopTicks);
    int rs = I.resync < 0 ? 1 << 30 : I.resync > 0 ? I.resync * BAR : (loop >= 4 * BAR ? 4 * BAR : loop);
    if (I.fig.recipe && I.resync == 0) rs = std::max (1, I.fig.resync) * BAR;
    return rs;
}
std::vector<Note> evolveArp (const Idea& I, const std::vector<Note>& notes)
{
    if (! arpEvolving (I) || notes.empty()) return notes;
    return evolveArpCurve (notes, [&] (int t) { return arpEnergyAt (I, t); }, std::max (1, I.fig.len) * (BAR / std::max (1, I.arpSpb)), arpResyncTicks (I));
}
bool arpEvolving (const Idea& I) { return I.evolve && I.evolveMode > EvolveClassic; }
std::vector<MidiCtl> arpEnergyCtl (const Idea& I)
{
    std::vector<MidiCtl> o;
    if (! arpEvolving (I)) return o;
    int last74 = -1, last11 = -1;
    for (int t = 0; t < I.total; t += BAR)
    {
        const double e = arpEnergyAt (I, t);
        const int c74 = clampv ((int) std::lround (24 + 103 * e), 0, 127), c11 = clampv ((int) std::lround (70 + 57 * e), 0, 127);
        if (c74 != last74) { MidiCtl c; c.t = t; c.kind = 1; c.num = 74; c.val = c74; o.push_back (c); last74 = c74; }
        if (c11 != last11) { MidiCtl c; c.t = t; c.kind = 1; c.num = 11; c.val = c11; o.push_back (c); last11 = c11; }
    }
    return o;
}
std::vector<AlignMark> arpAlignment (const Idea& I)
{
    std::vector<AlignMark> m;
    const int st = BAR / std::max (1, I.arpSpb), figT = std::max (1, I.fig.len) * st;
    const int loop = std::max (BAR, I.H.loopTicks);
    const int rs = arpResyncTicks (I);
    const int span = std::min (I.total, std::max (loop, 4 * BAR));
    std::set<int> fs, cs;
    for (int s0 = 0; s0 < span; s0 += std::min (rs, span)) for (int t = s0; t < std::min (span, s0 + rs); t += figT) fs.insert (t);
    for (int b = 0; b < span; b += loop) for (auto& c : I.H.chords) if (b + c.t < span) cs.insert (b + c.t);
    for (int t : fs) m.push_back ({ t, cs.count (t) ? 2 : 0 });
    for (int t : cs) if (! fs.count (t)) m.push_back ({ t, 1 });
    std::sort (m.begin(), m.end(), [] (const AlignMark& a, const AlignMark& b) { return a.t < b.t; });
    return m;
}
double arpAlignmentScore (const Idea& I)
{
    int c = 0, both = 0; for (auto& x : arpAlignment (I)) { if (x.kind >= 1) ++c; if (x.kind == 2) ++both; }
    return c ? (double) both / c : 1.0;
}

// ================================================================= EVOLVE + motifs
const char* harvestFeatureName (int k) { static const char* n[] { "repetition", "rhythm", "contour", "intervals", "landing", "range" }; return n[clampv (k, 0, 5)]; }
HFeat fragmentFeatures (const std::vector<Note>& frag0, const std::vector<Note>& whole, const Harmony& H, int t0, int t1)
{
    HFeat f {}; auto frag = frag0; sortNotes (frag);
    if (frag.size() < 2) return f;
    const int len = t1 - t0;
    std::vector<int> pat; for (auto& x : frag) pat.push_back (x.t - t0);
    int reps = 0, maxT = 0; for (auto& x : whole) maxT = std::max (maxT, x.t + 1);
    for (int s = 0; s + len <= maxT; s += PPQ)
    {
        if (s == t0) continue;
        std::vector<int> q; for (auto& x : whole) if (x.t >= s && x.t < s + len) q.push_back (x.t - s);
        if (q == pat) ++reps;
    }
    f[0] = std::min (1.0, reps / 3.0);
    std::set<int> durs; int syn = 0; for (auto& x : frag) { durs.insert (x.d / 12); if (x.t % (PPQ / 2) != 0) ++syn; }
    f[1] = clampv (0.5 * std::min (1.0, (durs.size() - 1) / 2.0) + 0.5 * std::min (1.0, 2.0 * syn / frag.size()), 0.0, 1.0);
    int turns = 0, lastS = 0; std::set<int> ivs; int big = 0;
    for (size_t i = 1; i < frag.size(); ++i)
    {
        const int iv = frag[i].p - frag[i - 1].p, s = (iv > 0) - (iv < 0);
        if (s && lastS && s != lastS) ++turns;
        if (s) lastS = s;
        ivs.insert (std::abs (iv)); if (std::abs (iv) > 7 && std::abs (iv) != 12) ++big;
    }
    f[2] = turns == 1 || turns == 2 ? 1.0 : turns == 0 ? 0.45 : 0.3;
    f[3] = clampv (std::min (1.0, (ivs.size() - 1) / 3.0) - 0.3 * big, 0.0, 1.0);
    f[4] = H.chordTone (frag.back().p, frag.back().t) ? (strongT (frag.back().t) || frag.back().d >= PPQ / 2 ? 1.0 : 0.7) : 0.2;
    int lo = 127, hi = 0; for (auto& x : frag) { lo = std::min (lo, x.p); hi = std::max (hi, x.p); }
    f[5] = fitRange (hi - lo, 3, 9, 6);
    return f;
}
MotifSpan strongestMotif (const std::vector<Note>& phrase, const Harmony& H, int phraseTicks)
{
    MotifSpan best; double bs = -1;
    for (int len : { BAR, BAR / 2 })
        for (int t0 = 0; t0 + len <= phraseTicks; t0 += len)
        {
            std::vector<Note> fr; for (auto& x : phrase) if (x.t >= t0 && x.t < t0 + len) fr.push_back (x);
            if (fr.size() < 3) continue;
            const auto f = fragmentFeatures (fr, phrase, H, t0, t0 + len);
            double s = 0; for (double x : f) s += x;
            s += t0 == 0 ? 0.3 : 0;   // the opening is what people remember
            if (s > bs) { bs = s; best = { t0, t0 + len }; }
        }
    return best;
}
std::vector<Note> placeMotif (const std::vector<Note>& line, const std::vector<Note>& motif, int mt0, int mlen, int P, int total)
{
    if (motif.empty() || mlen <= 0 || P <= 0) return line;
    auto o = line;
    for (int b = 0; b < total; b += P)
    {
        const int w0 = b + mt0, w1 = w0 + mlen;
        if (w0 >= total) break;
        std::vector<Note> k;
        for (auto x : o) { if (x.t >= w0 && x.t < w1) continue; if (x.t < w0 && x.t + x.d > w0) x.d = std::max (6, w0 - x.t); k.push_back (x); }
        for (auto x : motif) { x.t += w0; if (x.t < total) { x.lock = true; k.push_back (x); } }
        o = k;
    }
    sortNotes (o);
    return o;
}
std::vector<Note> evolveLine (const std::vector<Note>& phrase0, const Harmony& H, int P, int total, int mode, MotifSpan keep, uint32_t seed, int spb, int mood)
{
    std::vector<Note> phrase; for (auto& x : phrase0) if (x.t < P) phrase.push_back (x);
    if (phrase.empty() || P <= 0) return phrase0;
    if (! keep.valid()) keep = strongestMotif (phrase, H, P);
    std::vector<Note> motif; for (auto& x : phrase) if (keep.valid() && x.t >= keep.t0 && x.t < keep.t1) { auto y = x; y.t -= keep.t0; motif.push_back (y); }
    const int reps = std::max (1, (total + P - 1) / P);
    const EditCtx ec = ctxFor (H, P, spb, mood);
    std::vector<Note> out;
    for (int k = 0; k < reps; ++k)
    {
        std::vector<Note> cur = phrase;
        const double x = reps > 1 ? (double) k / (reps - 1) : 0;
        const uint32_t s = seed + (uint32_t) k * 7919u;
        if (mode == EvolveGrow && k > 0)
        {   // more and more: busier, a little candy, the last pass lifts its second half
            cur = toolBusier (cur, ec, s);
            if (x > 0.4) cur = applyEarCandy (cur, H, CandyHook, P, s);
            if (k == reps - 1) for (auto& y : cur) if (y.t >= P / 2 && y.p + 12 <= 96 && ! (keep.valid() && y.t >= keep.t0 && y.t < keep.t1)) y.p += 12;
        }
        else if (mode == EvolveWave && k > 0)
        {   // breathes: thin, full, busy, full, ...
            const int ph = k % 4;
            if (ph == 1) cur = toolSimpler (cur, ec); else if (ph == 2) cur = toolVary (cur, ec, s, 1); else if (ph == 3) cur = toolBusier (cur, ec, s);
        }
        else if (mode == EvolveJourney && k > 0 && (k < reps - 1 || reps == 2))
        {   // travels away (answers less and less like the call), then comes home on the last pass
            const double sim = reps == 2 ? 0.6 : 1.0 - 0.6 * std::sin (3.14159265 * x);
            cur = generateResponse (phrase, H, 0, P, 0, sim, s);
        }
        // the strongest motif stays exactly as it is
        if (keep.valid()) cur = placeMotif (cur, motif, keep.t0, keep.t1 - keep.t0, P, P);
        for (auto y : cur) { y.t += k * P; if (y.t < total) out.push_back (y); }
    }
    sortNotes (out);
    // refit the moved notes (outside the motif) to the chords under them
    std::vector<Note> res = out;
    for (auto& y : res)
    {
        const int ph = y.t % P;
        if (keep.valid() && ph >= keep.t0 && ph < keep.t1) continue;
        if (! H.scaleTone (y.p, y.t)) y.p = nearestST (H, y.p, y.t);
        if (strongT (y.t) && H.clash (y.p, y.t)) y.p = nearestCT (H, y.p, y.t);
    }
    for (auto& y : res) y.lock = false;
    return res;
}

// ================================================================= HARVESTER
std::vector<Fragment> harvestFragments (const Idea& I, const HarvestWeights& W, int maxN)
{
    std::vector<Fragment> all;
    const int span = std::min (I.total, std::max (BAR, I.H.loopTicks));
    double ws = 0; for (double w : W.w) ws += w;
    for (int lane : { LaneMelody, LaneArp })
    {
        const auto& ns = I.lanes[lane].notes;
        for (int len : { BAR, 2 * BAR, BAR / 2 })
            for (int t0 = 0; t0 + len <= span; t0 += std::max (BAR / 2, len / 2))
            {
                Fragment F; F.t0 = t0; F.t1 = t0 + len; F.lane = lane;
                for (auto& x : ns) if (x.t >= t0 && x.t < t0 + len) F.notes.push_back (x);
                if (F.notes.size() < 3) continue;
                F.f = fragmentFeatures (F.notes, ns, I.H, t0, t0 + len);
                double s = 0; for (int k = 0; k < kHarvestFeatures; ++k) s += W.w[(size_t) k] * F.f[(size_t) k];
                F.score = ws > 0 ? s / ws : 0;
                if (lane == LaneArp) F.score -= 0.03;   // ties go to the melody
                all.push_back (F);
            }
    }
    std::stable_sort (all.begin(), all.end(), [] (const Fragment& a, const Fragment& b) { return a.score > b.score; });
    std::vector<Fragment> out;
    for (auto& F : all)
    {
        bool dup = false;
        for (auto& G : out)
        {
            std::vector<Note> a = F.notes, b = G.notes; for (auto& x : a) x.t -= F.t0; for (auto& x : b) x.t -= G.t0;
            if (noteShare (a, b, 1 << 20) > 0.8 || (F.lane == G.lane && F.t0 < G.t1 && G.t0 < F.t1)) dup = true;
        }
        if (! dup) out.push_back (F);
        if ((int) out.size() >= maxN) break;
    }
    return out;
}
void learnKeep (HarvestWeights& W, const HFeat& f, bool kept)
{
    double mean = 0; for (double x : f) mean += x; mean /= kHarvestFeatures;
    const double lr = 0.25 * (kept ? 1.0 : -0.6);
    for (int k = 0; k < kHarvestFeatures; ++k) W.w[(size_t) k] = clampv (W.w[(size_t) k] + lr * (f[(size_t) k] - mean), 0.2, 3.0);
    if (kept) ++W.keeps;
}
std::string weightsToString (const HarvestWeights& W)
{
    std::ostringstream o; for (double w : W.w) o << w << ","; o << W.keeps; return o.str();
}
HarvestWeights weightsFromString (const std::string& s)
{
    HarvestWeights W; std::stringstream in (s); std::string tok; int k = 0;
    while (std::getline (in, tok, ','))
    {
        if (tok.empty()) break;
        if (k < kHarvestFeatures) W.w[(size_t) k] = clampv (std::atof (tok.c_str()), 0.2, 3.0); else W.keeps = std::max (0, std::atoi (tok.c_str()));
        ++k;
    }
    return W;
}

// ================================================================= PIANO TEST
double pianoTest (const std::vector<Note>& in, const Harmony& H, int span)
{
    auto n = in; sortNotes (n);
    if (n.size() < 3) return 0;
    span = std::max (BAR, span);
    const int bars = std::max (1, span / BAR);
    for (auto& x : n) { x.l = {}; x.v = 0.7f; x.d = std::min (x.d, 3 * PPQ / 2); }   // a piano: no glides, flat touch, notes decay
    int hi = 0, hiN = 0; for (auto& x : n) hi = std::max (hi, x.p); for (auto& x : n) if (x.p == hi) ++hiN;
    const double peak = hiN <= 2 ? 1.0 : hiN <= 3 ? 0.7 : 0.45;
    std::set<int> pos; std::set<int> durs; for (auto& x : n) { pos.insert (x.t % PPQ); durs.insert (std::min (x.d, 2 * PPQ) / 24); }
    const double dens = (double) n.size() / bars;
    double rid = 0.5 * std::min (1.0, (pos.size() - 1) / 2.0) + 0.5 * std::min (1.0, (durs.size() - 1) / 2.0);
    if (dens > 11) rid *= 0.5;   // a wall of 16ths is the FX doing the work
    if (dens < 1.5) rid *= 0.6;
    int sing = 0; for (size_t i = 1; i < n.size(); ++i) { const int a = std::abs (n[i].p - n[i - 1].p); if (a <= 7 || a == 12) ++sing; }
    const double singable = (double) sing / (n.size() - 1);
    const double resol = (H.chordTone (n.back().p, n.back().t) ? 0.7 : 0.2) + (mod12 (n.back().p) == mod12 (H.key) || mod12 (n.back().p) == mod12 (H.at (n.back().t).root) ? 0.3 : 0.0);
    int strong = 0, strongCt = 0; for (auto& x : n) if (strongT (x.t)) { ++strong; if (H.chordTone (x.p, x.t)) ++strongCt; }
    const double ct = strong ? (double) strongCt / strong : 0.5;
    // a motif people can hear come back: a repeated bar rhythm or a repeated 2-beat cell
    double rep = 0.5;
    for (int b = 1; b < bars && rep < 1; ++b) for (int a = 0; a < b; ++a) if (! barOnsets (n, b).empty() && barOnsets (n, a) == barOnsets (n, b)) rep = 1.0;
    if (rep < 1)
    {
        std::map<std::vector<int>, int> cells;
        for (int c = 0; c + 2 * PPQ <= span; c += 2 * PPQ) { std::vector<int> v; for (auto& x : n) if (x.t >= c && x.t < c + 2 * PPQ) v.push_back (x.t - c); if (v.size() >= 2 && ++cells[v] >= 2) rep = 0.85; }
    }
    // breathing (what a dry piano exposes)
    std::vector<bool> on ((size_t) (span / 12 + 1), false);
    for (auto& x : n) for (int t = x.t; t < std::min (span, x.t + x.d); t += 12) on[(size_t) (t / 12)] = true;
    int silent = 0; for (int t = 0; t < span; t += 12) if (! on[(size_t) (t / 12)]) ++silent;
    const double restS = fitRange ((double) silent / (span / 12), 0.08, 0.55, 0.25);
    return (1.0 * peak + 1.4 * rid + 1.2 * singable + 1.0 * resol + 1.2 * ct + 1.2 * rep + 0.8 * restS) / 7.8;
}

// ================================================================= HOOK LAB
HookParams hookParams (const Idea& I, int bars, uint32_t seed)
{
    HookParams hp; hp.bars = bars; hp.style = I.style; hp.candy = I.candy; hp.tension = I.tension; hp.seed = seed; return hp;
}
std::vector<Note> shapeLine (const Idea& I, const std::vector<Note>& in, int total, int style, int candy, double tension, uint32_t seed, int reg, int octShift, bool keepRhythm)
{
    auto n = in; tidy (n, total);
    if (style > StyleAuto && ! keepRhythm) n = applyStyle (n, I.H, style, total, seed ^ 0x57u);
    if (tension >= 0) n = applyTension (n, I.H, tension, seed ^ 0x7Eu);
    if (candy > CandyOff) n = applyEarCandy (n, I.H, candy, total, seed ^ 0xCAu);
    const auto& arpN = I.lanes[LaneArp].notes;
    if (! I.lanes[LaneArp].mute && ! arpN.empty())
    {
        std::vector<Note> a; for (auto& x : arpN) if (x.t < total) a.push_back (x);
        n = placeRegister (n, a, reg, octShift);
        if (! keepRhythm && collisionShare (n, a) > 0.5) n = complementRhythm (n, a, I.H, I.melSpb);   // (a euclid / meter / drop rhythm stays exact)
    }
    else n = placeRegister (n, {}, reg, octShift);
    n = ensureRests (n, total);
    n = keyGuard (n, I.H, tension > 0.6 || candy == CandyUnhinged);
    tidy (n, total);
    return n;
}
double hookScore (const Idea& I, const std::vector<Note>& n, int ticks, int style, const HarvestWeights* taste, double* pianoOut)
{
    const auto& M = recipe().mode[clampv (I.mode, 0, kMoods - 1)];
    const double base = M.hasRecipe() ? recipeScore (analyseLane (n, I.H, ticks), M) : 1.0 / (1.0 + std::max (0.0, -arp::taste (n, I.H, I.mode, false, ticks).score) / 10.0);
    const auto f = hookFeatures (n, I.H, ticks);
    const double fit = style > StyleAuto ? styleFit (f, style) : 0.5;
    const double piano = pianoTest (n, I.H, ticks);
    if (pianoOut) *pianoOut = piano;
    double tb = 0.5;
    if (taste)
    {
        const auto ms = strongestMotif (n, I.H, ticks);
        if (ms.valid()) { std::vector<Note> fr; for (auto& x : n) if (x.t >= ms.t0 && x.t < ms.t1) fr.push_back (x); const auto ff = fragmentFeatures (fr, n, I.H, ms.t0, ms.t1); double s = 0, ws = 0; for (int k = 0; k < kHarvestFeatures; ++k) { s += taste->w[(size_t) k] * ff[(size_t) k]; ws += taste->w[(size_t) k]; } tb = ws > 0 ? s / ws : 0.5; }
    }
    return 0.42 * base + 0.25 * fit + 0.23 * piano + 0.10 * tb;
}
std::vector<Note> loopHook (const std::vector<Note>& hook, int ht, int total, const Harmony& H)
{
    std::vector<Note> o;
    if (ht <= 0) return o;
    const bool sameChords = ht % std::max (1, H.loopTicks) == 0;
    for (int b = 0; b < total; b += ht)
    {
        std::vector<Note> c; for (auto x : hook) { if (x.t >= ht) continue; x.t += b; if (x.t < total) c.push_back (x); }
        if ((b > 0 && ! sameChords) || outOfKey (c, H, false) > 0) c = refit (c, H, total);   // a pass that doesn't fit the chords under it is refit
        o.insert (o.end(), c.begin(), c.end());
    }
    sortNotes (o);
    return o;
}
namespace
{
std::string formName (int f) { static const char* n[] { "AAAB", "ABAB", "ABAC", "AAAA" }; return n[clampv (f, 0, 3)]; }
}
std::string harmonyPrint (const Harmony& H)
{
    std::ostringstream o; o << H.key << ':' << H.scaleName << ':' << H.loopTicks << ':' << H.offset;
    for (auto& c : H.chords) { o << '|' << c.t << ',' << c.tEnd << ',' << c.root; for (int pc : c.pcs) o << '.' << pc; }
    return o.str();
}
std::string hookCharacter (const Hook& h)
{
    const auto& f = h.f;
    std::string c;
    if (f.repeat >= 0.8) c = "riff";
    else if (h.label.rfind ("ABAB", 0) == 0 || h.label.rfind ("ABAC", 0) == 0) c = "call & answer";
    else if (f.peakPos >= 0.6) c = "rising";
    else if (f.peakPos <= 0.25) c = "falling";
    else if (f.leap >= 0.3) c = "leaping";
    else if (f.step >= 0.6) c = "stepwise";
    else c = "flowing";
    if (f.density < 3.0) c += ", sparse"; else if (f.density > 8.5) c += ", busy";
    return c;
}
void refitHook (const Idea& I, Hook& h, int reg)
{
    const int ticks = h.ticks();
    auto n = refit (h.notes, I.H, ticks, I.melSpb, I.mode);
    n = keyGuard (n, I.H, I.tension > 0.6);
    n = placeRegister (n, {}, reg);
    tidy (n, ticks);
    h.notes = n;
    h.score = hookScore (I, h.notes, ticks, I.style, nullptr, &h.piano);
    h.f = hookFeatures (h.notes, I.H, ticks);
    h.harm = harmonyPrint (I.H); h.kept = true;
}
namespace
{
// an 8-bar hook is A then A' (not A twice): same opening + a new ending, an answer, or a lift a chord step up with a new close
std::vector<Note> developEight (const Idea& I, const std::vector<Note>& A, const MelodyParams& P, int variant, uint32_t seed)
{
    const auto& H = I.H; const int half = 4 * BAR, ticks = 8 * BAR;
    std::vector<Note> second;
    auto newEnding = [&] (int from) {   // bars [from, 8) from another take of the engine over the 8-bar harmony
        MelodyParams Q = P; Q.seed = hmix (seed, 0xE11Du); Q.phraseBars = 8; Q.form = (P.form + 1) % 4;
        auto alt = composeMelody (H, Q, ticks, false);
        std::vector<Note> o; for (auto& x : alt) if (x.t >= from) o.push_back (x);
        return o;
    };
    if (variant == 0)
    {   // same start, new ending (antecedent / consequent)
        for (auto x : A) if (x.t < 2 * BAR) { x.t += half; second.push_back (x); }
        second = refit (second, H, ticks, I.melSpb, I.mode);
        auto e = newEnding (6 * BAR); second.insert (second.end(), e.begin(), e.end());
    }
    else if (variant == 1)
    {   // the answer
        second = generateResponse (A, H, 0, half, half, 0.6 + 0.2 * u01 (seed, 3), seed ^ 0xA25u);
        auto e = newEnding (7 * BAR); std::vector<Note> o; for (auto& x : second) if (x.t < 7 * BAR) o.push_back (x);
        o.insert (o.end(), e.begin(), e.end()); second = o;
    }
    else
    {   // the lift: bars 5-6 a chord step up, then a new close
        for (auto x : A) if (x.t < 2 * BAR) { x.t += half; x.p = stepChord (H, x.p, x.t, 1); second.push_back (x); }
        auto e = newEnding (6 * BAR); second.insert (second.end(), e.begin(), e.end());
    }
    std::vector<Note> o = A; for (auto& x : second) if (x.t >= half && x.t < ticks) o.push_back (x);
    tidy (o, ticks);
    return o;
}
}
std::vector<Hook> generateHooks (const Idea& I, const HookParams& hp)
{
    std::vector<Hook> hooks;
    const int bars = hp.bars >= 8 ? 8 : 4, ticks = bars * BAR;
    const MelodyParams mp = melodyParams (I);
    const std::string print = harmonyPrint (I.H);
    const bool urban = urbanMood (I.mode);
    Idea UX; if (urban) { UX = I; const int fl = styleFlavour (hp.style, I.mode); if (fl >= 0) UX.flavourUsed = fl; }   // a TRAP / HIP HOP style = that flavour's hooks
    for (int i = 0; i < hp.count; ++i)
    {
        if (hp.cancel != nullptr && hp.cancel->load()) break;
        Hook best; double bestS = -1e9; bool bestPiano = false; int bestVar = -1;
        for (int k = 0; k < 48; ++k)
        {
            if (k >= 6 && bestS > -1e8 && (bestPiano || k >= 16)) break;
            MelodyParams P = mp;
            P.seed = hmix (hp.seed * 2654435761u + 0x40C0u, (uint32_t) (i * 131 + k * 7 + 1));
            P.form = (i + k) % 4; P.phraseBars = 4; P.stagger = 0;
            P.range = 2 + ((i >> 1) & 1);
            if (i % 5 == 4) P.spb = 8;
            if (hp.style == StylePlayful) P.stutter = std::max (P.stutter, 0.4);
            std::vector<Note> raw;
            if (urban)
            {   // TRAP / HIP HOP: the mood's own engine writes the hook (its scales, rhythm archetypes, flavour), the whole length at once
                for (auto& x : urbanMelody (UX, I.mode, P.seed)) if (x.t < ticks) raw.push_back (x);
                if (! raw.empty() && I.total < ticks) raw = loopHook (raw, std::max (BAR, I.total), ticks, I.H);
            }
            else raw = composeMelody (I.H, P, 4 * BAR, false);
            if (raw.size() < 3) continue;
            int variant = -1;
            if (bars == 8 && ! urban)
            {
                variant = (i + k) % 3;
                raw = developEight (I, raw, P, variant, P.seed);
                std::vector<Note> a, b; for (auto x : raw) { if (x.t < 4 * BAR) a.push_back (x); else { x.t -= 4 * BAR; b.push_back (x); } }
                if (noteShare (a, b, 4 * BAR) > 0.8) continue;   // that's 4 + 4 again
            }
            const bool idm = hp.meter > Meter44 || hp.euclidHits > 0 || hp.feel != FeelNormal;
            if (hp.feel == FeelHalf) raw = halfTimeLine (raw, I.H, ticks);
            if (hp.meter > Meter44) raw = meterLine (raw, I.H, hp.meter, ticks);
            if (hp.euclidHits > 0) { raw = euclidLine (raw, I.H, hp.euclidHits, meterSteps (hp.meter), hp.euclidRot, ticks); if (hp.meter > Meter44) raw = meterAccents (raw, hp.meter); }
            if (hp.feel == FeelDrop) raw = dropLine (raw, I.H, ticks, P.seed);
            if (raw.size() < 3) continue;
            auto line = shapeLine (I, raw, ticks, urban && hp.style >= kClassicStyles ? (int) StyleAuto : hp.style, idm ? CandyOff : hp.candy, hp.tension, P.seed, hp.feel == FeelDrop ? std::max (0, hp.reg - 1) : hp.reg, 0, idm);
            if (line.size() < 3) continue;
            if (bars == 8 && ! idm && ! urban)   // (TRAP / HIP HOP loop on purpose: their second half is the varied repeat)
            {
                std::vector<Note> a, b; for (auto x : line) { if (x.t < 4 * BAR) a.push_back (x); else { x.t -= 4 * BAR; b.push_back (x); } }
                if (noteShare (a, b, 4 * BAR) > 0.8) continue;
            }
            double share = 0; for (auto& h : hooks) share = std::max (share, noteShare (line, h.notes, ticks));
            if (share > 0.7) continue;
            if (urban && k < 40) { double md = 1; for (auto& h : hooks) md = std::min (md, melodyDistance (line, I.H.key, h.notes, I.H.key)); if (md < 0.5) continue; }   // PUSH FURTHER between the tiles
            double piano = 0;
            double s = hookScore (I, line, ticks, hp.style, hp.taste, &piano);
            const bool pianoOk = piano >= kPianoMin;
            if (! pianoOk) s -= 1.0;   // the piano test drops it unless nothing else is left
            if (s > bestS)
            {
                bestS = s; bestPiano = pianoOk; bestVar = variant;
                best.notes = line; best.bars = bars; best.seed = P.seed; best.score = s; best.piano = piano;
                best.label = formName (P.form) + (P.spb == 8 ? " 8ths" : "");
            }
        }
        if (best.notes.empty()) continue;
        best.f = hookFeatures (best.notes, I.H, ticks); best.harm = print;
        best.label = hookCharacter (best);
        if (bars == 8) { static const char* v[] { ", new ending", ", answered", ", lifted" }; if (bestVar >= 0) best.label += v[bestVar]; }
        best.meter = hp.meter; best.feel = hp.feel;
        if (hp.feel == FeelHalf) best.label = "half-time, " + best.label; else if (hp.feel == FeelDrop) best.label = "drop, " + best.label;
        if (hp.meter > Meter44) best.label += std::string (", ") + meterName (hp.meter);
        if (hp.euclidHits > 0) best.label += ", E" + std::to_string (hp.euclidHits) + "/" + std::to_string (meterSteps (hp.meter)) + (hp.euclidRot ? " r" + std::to_string (hp.euclidRot) : "");
        hooks.push_back (best);
        if (hp.progress != nullptr) hp.progress->store ((int) hooks.size());
    }
    return hooks;
}
bool keepsMagic (const Hook& a, const Hook& b, const Harmony& H)
{   // what makes it that hook: the opening bar's rhythm, the interval signature, where the peak sits, the motif's contour
    const auto fa = hookFeatures (a.notes, H, a.ticks()), fb = hookFeatures (b.notes, H, b.ticks());
    if (fa.rhythm != fb.rhythm) return false;
    const size_t m = std::min (fa.intervals.size(), fb.intervals.size());
    for (size_t i = 0; i < std::min<size_t> (m, 3); ++i) if (fa.intervals[i] != fb.intervals[i]) return false;
    if (std::abs (fa.peakPos - fb.peakPos) > 0.13) return false;
    // contour of the motif (the notes of the first half)
    auto motifContour = [] (const Hook& h) {
        auto n = h.notes; sortNotes (n); std::vector<int> c;
        for (size_t i = 1; i < n.size() && n[i].t < h.ticks() / 2; ++i) { const int iv = n[i].p - n[i - 1].p; c.push_back ((iv > 0) - (iv < 0)); }
        return c; };
    const auto ca = motifContour (a), cb = motifContour (b);
    const size_t cm = std::max (ca.size(), cb.size());
    int agree = 0; for (size_t i = 0; i < std::min (ca.size(), cb.size()); ++i) if (ca[i] == cb[i]) ++agree;
    return cm == 0 || (double) agree / cm >= 0.6;
}
std::vector<Hook> findMagic (const Idea& I, const Hook& h, const HookParams& hp, int count)
{
    std::vector<Hook> out;
    const int ticks = h.ticks(); const auto& H = I.H;
    int pkT = -1, pkP = 0; for (auto& x : h.notes) if (x.p > pkP) { pkP = x.p; pkT = x.t; }
    const std::string print = harmonyPrint (H);
    auto underPeak = [&] (int p, int t) { return p >= pkP ? stepScale (H, pkP, t, -1) : p; };
    constexpr int kOps = 10;
    std::vector<Hook> near;   // kept the magic but a little too close: the fallback when the strict limits can't fill the grid
    const auto t0 = std::chrono::steady_clock::now();
    auto elapsedMs = [&] { return std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count(); };
    for (int k = 0; (int) out.size() < count; ++k)
    {
        if (hp.cancel != nullptr && hp.cancel->load()) break;
        if (k >= count * 400 || (k >= count * 40 && elapsedMs() > 600.0)) break;   // the time budget: then the near misses fill in
        const uint32_t s = hmix (h.seed ^ 0xA11CEu ^ hp.seed, (uint32_t) k);
        auto v = h.notes; sortNotes (v);
        auto op = [&] (int which, uint32_t s2) {
            switch (which)
            {
                case 0:   // new pitches after the motif, same contour, under the peak
                    for (size_t i = 1; i < v.size(); ++i)
                    {
                        if (v[i].t < BAR || v[i].t == pkT || u01 (s2, (uint32_t) i) < 0.3) continue;
                        const int iv = v[i].p - v[i - 1].p; const int sg = (iv > 0) - (iv < 0);
                        v[i].p = underPeak (sg == 0 ? v[i - 1].p : stepScale (H, v[i - 1].p, v[i].t, sg * (1 + (int) (u01 (s2 ^ 1, (uint32_t) i) * 2))), v[i].t);
                    }
                    break;
                case 1: {   // rhythm play after the motif: split long notes
                    std::vector<Note> o;
                    for (size_t i = 0; i < v.size(); ++i)
                    {
                        Note x = v[i];
                        if (x.t >= BAR && x.d >= PPQ && x.t != pkT && u01 (s2, (uint32_t) (i + 40)) < 0.6) { Note y = x; x.d /= 2; y.t = x.t + x.d; y.d = x.d; y.p = underPeak (stepScale (H, x.p, y.t, u01 (s2, (uint32_t) i) < 0.5 ? 1 : -1), y.t); o.push_back (x); o.push_back (y); continue; }
                        o.push_back (x);
                    }
                    v = o; break; }
                case 2: case 6: {   // a new ending (2: the last bar) / a new second half (6), from the engine
                    const int from = which == 2 ? ticks - BAR : ticks / 2;
                    if (pkT >= from) break;
                    MelodyParams P = melodyParams (I); P.seed = s2; P.phraseBars = h.bars; P.stagger = 0; P.form = (int) (s2 % 4);
                    auto alt = composeMelody (H, P, ticks, false);
                    std::vector<Note> o; for (auto& x : v) if (x.t < from) o.push_back (x);
                    for (auto x : alt) if (x.t >= from) { x.p = underPeak (x.p, x.t); o.push_back (x); }
                    v = o; break; }
                case 3: {   // a sequence: bar 3 answers bar 1 a chord step away
                    if (h.bars < 4 || (pkT >= 2 * BAR && pkT < 3 * BAR)) break;
                    std::vector<Note> o; for (auto& x : v) if (x.t < 2 * BAR || x.t >= 3 * BAR) o.push_back (x);
                    for (auto x : h.notes) if (x.t < BAR) { x.t += 2 * BAR; x.p = underPeak (stepChord (H, x.p, x.t, u01 (s2, 5) < 0.5 ? 1 : -1), x.t); o.push_back (x); }
                    v = o; break; }
                case 4: v = applyEarCandy (v, H, CandySubtle, ticks, s2); break;
                case 5: {   // bar 2 becomes an answer to bar 1
                    if (pkT >= BAR && pkT < 2 * BAR) break;
                    auto r = generateResponse (h.notes, H, 0, BAR, BAR, 0.6, s2);
                    std::vector<Note> o; for (auto& x : v) if (x.t < BAR || x.t >= 2 * BAR) o.push_back (x);
                    for (auto x : r) { x.p = underPeak (x.p, x.t); o.push_back (x); }
                    v = o; break; }
                case 7: {   // the motif displaced: bar 3 (or the second half) starts the motif a beat late
                    const int at = h.bars >= 8 ? 4 * BAR : 2 * BAR;
                    if (pkT >= at && pkT < at + BAR + PPQ) break;
                    std::vector<Note> o; for (auto& x : v) if (x.t < at || x.t >= at + BAR + PPQ) o.push_back (x);
                    for (auto x : h.notes) if (x.t < BAR) { x.t += at + PPQ; x.p = underPeak (x.p, x.t); o.push_back (x); }
                    v = refit (o, H, ticks, I.melSpb, I.mode); break; }
                case 8: {   // the motif augmented (twice as slow, bars 3-4) or diminished (twice as fast, twice in bar 3)
                    const int at = h.bars >= 8 ? 4 * BAR : 2 * BAR;
                    const bool aug = u01 (s2, 8) < 0.5; const int len = aug ? 2 * BAR : BAR;
                    if (pkT >= at && pkT < at + len) break;
                    std::vector<Note> o; for (auto& x : v) if (x.t < at || x.t >= at + len) o.push_back (x);
                    for (int rep = 0; rep < (aug ? 1 : 2); ++rep)
                        for (auto x : h.notes) if (x.t < BAR) { x.t = at + (aug ? x.t * 2 : rep * BAR / 2 + x.t / 2); x.d = aug ? x.d * 2 : std::max (12, x.d / 2); x.p = underPeak (x.p, x.t); o.push_back (x); }
                    v = refit (o, H, ticks, I.melSpb, I.mode); break; }
                case 9: {   // another cadence: the last phrase falls to a different chord tone and holds it
                    if (v.size() < 3 || pkT >= ticks - BAR) break;
                    const int tEnd = ticks - BAR;
                    std::vector<Note> o; for (auto& x : v) if (x.t < tEnd) o.push_back (x);
                    const int base = o.empty() ? h.notes.back().p : o.back().p;
                    const int k2 = u01 (s2, 9) < 0.5 ? -1 : -2;
                    Note a; a.t = tEnd; a.d = PPQ; a.p = underPeak (stepChord (H, base, tEnd, k2 + 1), tEnd); a.v = 0.75f;
                    Note b2; b2.t = tEnd + PPQ; b2.d = PPQ / 2; b2.p = underPeak (stepScale (H, a.p, b2.t, -1), b2.t); b2.v = 0.7f;
                    Note c; c.t = tEnd + 2 * PPQ; c.d = 2 * PPQ - 12; c.p = underPeak (stepChord (H, a.p, c.t, k2), c.t); c.v = 0.8f;
                    o.push_back (a); o.push_back (b2); o.push_back (c); v = o; break; }
                default: break;
            }
        };
        // always two different moves (three once the easy ones are used up)
        const int a = (int) (s % kOps); int b = (int) ((s >> 8) % (kOps - 1)); if (b >= a) ++b;
        op (a, s); op (b, hmix (s, 77));
        if (k >= count * 20) { int c = (int) ((s >> 16) % kOps); if (c == a || c == b) c = (c + 1) % kOps; if (c == a || c == b) c = (c + 1) % kOps; op (c, hmix (s, 99)); }
        tidy (v, ticks);
        v = keyGuard (v, H, I.tension > 0.6);
        Hook m; m.notes = v; m.bars = h.bars; m.seed = s;
        const double shared = noteShare (h.notes, v, ticks);
        if (! keepsMagic (h, m, H) || shared > 0.97) continue;
        double sib = 0; for (auto& o : out) sib = std::max (sib, noteShare (o.notes, v, ticks));
        if (shared > 0.75 || sib > 0.65)
        {   // a near miss: remember the freshest few
            bool dupN = sib > 0.9; for (auto& o : near) if (noteShare (o.notes, v, ticks) > 0.9) dupN = true;
            if (! dupN) { m.fresh = 1.0 - shared; m.f = hookFeatures (v, H, ticks); m.harm = print; near.push_back (m); }
            continue;
        }
        m.score = hookScore (I, v, ticks, hp.style, hp.taste, &m.piano);
        m.f = hookFeatures (v, H, ticks); m.harm = print; m.fresh = 1.0 - shared;
        m.label = "MAGIC " + std::to_string (out.size() + 1) + " \xc2\xb7 " + std::to_string ((int) std::lround (m.fresh * 100)) + "% new";
        out.push_back (m);
        if (hp.progress != nullptr) hp.progress->store ((int) out.size());
    }
    if ((int) out.size() < count && ! near.empty() && ! (hp.cancel != nullptr && hp.cancel->load()))
    {   // fill the grid: the freshest near misses that differ from what is there, labelled with how much is new
        std::stable_sort (near.begin(), near.end(), [] (const Hook& a, const Hook& b) { return a.fresh > b.fresh; });
        for (auto& m : near)
        {
            if ((int) out.size() >= count) break;
            bool dup = false; for (auto& o : out) if (noteShare (o.notes, m.notes, ticks) > 0.9) dup = true;
            if (dup) continue;
            m.score = hookScore (I, m.notes, ticks, hp.style, hp.taste, &m.piano);
            m.label = "NEAR " + std::to_string (out.size() + 1) + " \xc2\xb7 " + std::to_string ((int) std::lround (m.fresh * 100)) + "% new";
            out.push_back (m);
            if (hp.progress != nullptr) hp.progress->store ((int) out.size());
        }
    }
    return out;
}
std::vector<Note> makeSong (const Idea& I, const Hook& h, int bars, const HookParams& hp)
{
    const auto& H = I.H; const int L = h.ticks(), total = std::max (2 * L, bars * BAR);
    const int nsec = total / L;
    const EditCtx ec = ctxFor (H, L, I.melSpb, I.mode);
    auto A = h.notes; sortNotes (A);
    if (A.empty()) return {};
    // the song lives in the hook's register: every section folds (by octaves) into [hook low - 5, hook high + 5]
    int hLo = 127, hHi = 0; for (auto& x : A) { hLo = std::min (hLo, x.p); hHi = std::max (hHi, x.p); }
    int wLo = hLo - 5, wHi = hHi + 5; if (wHi - wLo < 12) { const int c = (wLo + wHi) / 2; wLo = c - 6; wHi = c + 6; }
    auto fold = [&] (std::vector<Note>& sec) { for (auto& x : sec) { while (x.p > wHi) x.p -= 12; while (x.p < wLo) x.p += 12; } };
    auto excess = [&] (const std::vector<Note>& sec) { int lo = 127, hi = 0; for (auto& x : sec) { lo = std::min (lo, x.p); hi = std::max (hi, x.p); } return sec.empty() ? 99 : (hi - lo) - (hHi - hLo); };
    auto A1 = toolVary (A, ec, hp.seed ^ 0xA1u, 2); fold (A1);
    auto section = [&] (double sim, uint32_t salt) {   // a B section: an answer that sits a little higher; the tightest of a few takes
        std::vector<Note> best; int bx = 1 << 20;
        for (uint32_t k = 0; k < 8 && bx > 5; ++k)
        {
            auto b = generateResponse (A, H, 0, L, 0, sim, hp.seed ^ salt ^ (k * 0x9E37u));
            for (auto& x : b) x.p = nearestCT (H, x.p + 4, x.t, 1);
            fold (b);
            const int ex = excess (b);
            if (! b.empty() && ex < bx) { bx = ex; best = b; }
        }
        return best.empty() ? A1 : best; };
    auto B = section (0.35, 0xB0u), B1 = section (0.5, 0xB1u);
    auto A2 = A;
    {   // the cadence: the last note home on the tonic, held
        Note& e = A2.back(); int best = e.p, bd = 99;
        for (int p = e.p - 7; p <= e.p + 7; ++p) if (mod12 (p) == mod12 (H.key) && std::abs (p - e.p) < bd) { bd = std::abs (p - e.p); best = p; }
        e.p = best; e.d = std::max (e.d, L - e.t - 12);
        fold (A2);
    }
    std::vector<std::vector<Note>*> plan;
    if (nsec <= 2) plan = { &A, &B };
    else if (nsec <= 4) plan = { &A, &A1, &B, &A2 };
    else plan = { &A, &A1, &B, &A2, &A, &A1, &B1, &A2 };
    std::vector<Note> out;
    for (int s = 0; s < nsec; ++s)
    {
        auto sec = *plan[(size_t) s % plan.size()];
        for (auto& x : sec) x.t += s * L;
        out.insert (out.end(), sec.begin(), sec.end());
    }
    tidy (out, total);
    out = refit (out, H, total, I.melSpb, I.mode);
    out = keyGuard (out, H, I.tension > 0.6);
    fold (out);   // the refit may nudge a note out of the window: octaves keep the pitch class (still in key)
    return out;
}
Hook combineHooks (const Idea& I, const Hook& R, const Hook& O, const Hook& E)
{
    Hook h; h.bars = R.bars; h.seed = R.seed ^ (O.seed * 3u) ^ (E.seed * 7u);
    const int span = R.ticks(), q = span / 4;
    auto pitchAt = [] (const Hook& src, int t) {
        int best = -1, bd = 1 << 20;
        for (auto& x : src.notes) { if (x.t <= t && t < x.t + std::max (x.d, 1)) return x.p; const int d = std::abs (x.t - t); if (d < bd) { bd = d; best = x.p; } }
        return best < 0 ? 60 : best;
    };
    auto n = R.notes; sortNotes (n);
    for (auto& x : n)
    {
        if (x.t < q) x.p = pitchAt (O, x.t % O.ticks());
        else if (x.t >= span - q) x.p = pitchAt (E, std::max (0, E.ticks() - (span - x.t)));
        x.l = {}; x.lock = false;
    }
    h.notes = keyGuard (n, I.H, I.tension > 0.6);
    h.score = hookScore (I, h.notes, span, I.style, nullptr, &h.piano);
    h.f = hookFeatures (h.notes, I.H, span); h.label = "COMBO"; h.harm = harmonyPrint (I.H);
    return h;
}
} // namespace arp
