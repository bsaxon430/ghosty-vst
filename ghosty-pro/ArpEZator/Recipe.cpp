// The composer's recipe as engines: its metrics (a port of lib.analyse), its scoring function (targets + weights),
// its hard rules, MIDNIGHT arps from its figure templates (ladder floor, pedal degree, slow events at bars 5-7) and
// SUNRISE / DUSK hooks from its rhythm cells, motif openings and phrase forms (ABAC lift / resolve, tie-ins).
#include "Composer.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace arp
{
namespace
{
const ModeRecipe& MR (int m) { return recipe().mode[clampv (m, 0, kMoods - 1)]; }
bool hasV (const std::vector<int>& v, int x) { return std::find (v.begin(), v.end(), x) != v.end(); }
int irnd (Rng& R, int n) { return n <= 0 ? 0 : std::min (n - 1, (int) std::floor (R() * n)); }
template <typename T> int pickIdx (Rng& R, const std::vector<std::pair<T, double>>& v)
{
    double t = 0; for (auto& e : v) t += std::max (0.0, e.second);
    double u = R() * t; for (size_t i = 0; i < v.size(); ++i) { u -= std::max (0.0, v[i].second); if (u < 0) return (int) i; }
    return v.empty() ? -1 : 0;
}
struct Mono { double s, l; int p; };
std::vector<Mono> monoLine (const std::vector<Note>& notes)
{
    std::vector<Note> ns = notes;
    std::stable_sort (ns.begin(), ns.end(), [] (const Note& a, const Note& b) { return a.t != b.t ? a.t < b.t : a.p > b.p; });
    std::vector<Mono> m; int last = -1;
    for (auto& n : ns) { if (n.t == last) continue; last = n.t; m.push_back ({ n.t / 24.0, n.d / 24.0, n.p }); }
    return m;
}
double fit (double x, const ModeRecipe::Target& T)
{
    if (x >= T.lo - 0.005 && x <= T.hi + 0.005) return 1;
    const double w = std::max (1e-6, T.hi - T.lo);
    const double d = x < T.lo ? T.lo - x : x - T.hi;
    return std::max (0.0, 1 - d / w);
}
} // namespace

int tonicAtOrAbove (int key, int lo) { int p = lo; while (mod12 (p) != mod12 (key)) ++p; return p; }

int degreePitch (const std::string& tok, int key, const std::string& scale, int base)
{
    (void) key;
    size_t i = 0; int acc = 0;
    if (i < tok.size() && (tok[i] == 'b' || tok[i] == '#')) { acc = tok[i] == 'b' ? -1 : 1; ++i; }
    if (i >= tok.size() || tok[i] < '1' || tok[i] > '7') return base;
    const int d = tok[i] - '1'; ++i;
    int oct = 0; for (; i < tok.size(); ++i) { if (tok[i] == '\'') ++oct; else if (tok[i] == ',') --oct; }
    return base + scaleOf (scale)[(size_t) d] + 12 * oct + acc;
}

static std::string degreeToken (int pitch, int key, const std::string& scale, int base)
{
    const auto& sc = scaleOf (scale);
    const int rel = pitch - base, oct = (int) std::floor (rel / 12.0), pc = mod12 (rel);
    std::string acc; int d = -1;
    for (int k = 0; k < 7; ++k) if (sc[(size_t) k] == pc) d = k;
    if (d < 0) { for (int k = 0; k < 7; ++k) if (sc[(size_t) k] == mod12 (pc + 1)) { d = k; acc = "b"; } }
    if (d < 0) { for (int k = 0; k < 7; ++k) if (sc[(size_t) k] == mod12 (pc - 1)) { d = k; acc = "#"; } }
    if (d < 0) d = 0;
    std::string t = acc + std::to_string (d + 1);
    for (int k = 0; k < oct; ++k) t += "'";
    for (int k = 0; k > oct; --k) t += ",";
    (void) key;
    return t;
}

Figure figureFromTokens (const std::vector<std::string>& tokens)
{
    Figure f; f.recipe = true; f.len = clampv ((int) tokens.size(), 1, 8);
    for (int i = 0; i < f.len; ++i)
    {
        std::string t = tokens[(size_t) i]; Step s;
        for (char c : t) { if (c == '>') s.acc = true; if (c == '^') ++s.oct; }
        t.erase (std::remove_if (t.begin(), t.end(), [] (char c) { return c == '>' || c == '^'; }), t.end());
        if (t == "P") s.pedal = true; else if (t == "-") s.rest = true; else if (t == "~") s.hold = true;
        else { int v = 0; for (char c : t) if (c >= '0' && c <= '9') v = v * 10 + (c - '0'); s.idx = v + 1; }
        f.s[(size_t) i] = s;
    }
    return f;
}

// ---------------------------------------------------------------- metrics (lib.analyse)
Metrics analyseLane (const std::vector<Note>& notes, const Harmony& H, int totalTicks, const ArpInfo* arpI)
{
    Metrics st;
    const auto mono = monoLine (notes);
    const int bars = std::max (1, totalTicks / BAR);
    const double total = totalTicks / 24.0;
    st.set ("notes", (double) mono.size()); st.set ("density", (double) mono.size() / bars);
    std::vector<int> pitches; for (auto& n : mono) pitches.push_back (n.p);
    std::vector<int> ivs; for (size_t i = 1; i < pitches.size(); ++i) ivs.push_back (pitches[i] - pitches[i - 1]);
    std::vector<int> nz; for (int i : ivs) if (i) nz.push_back (i);
    auto isCt = [&] (const Mono& n) { return H.chordTone (n.p, (int) std::lround (n.s * 24)); };
    int strong = 0, cts = 0, all = 0; for (auto& n : mono) { if (std::fmod (n.s, 4.0) < 1e-9) { ++strong; if (isCt (n)) ++cts; } if (isCt (n)) ++all; }
    st.set ("ct_strong", (double) cts / std::max (1, strong)); st.set ("ct_all", (double) all / std::max<size_t> (1, mono.size()));
    int rep = 0; for (int i : ivs) if (i == 0) ++rep;
    int step = 0, leap = 0, desc = 0; for (int i : nz) { if (std::abs (i) <= 2) ++step; if (std::abs (i) >= 5) ++leap; if (i < 0) ++desc; }
    st.set ("repeat_pct", (double) rep / std::max<size_t> (1, ivs.size())); st.set ("step_pct", (double) step / std::max<size_t> (1, nz.size()));
    st.set ("leap_pct", (double) leap / std::max<size_t> (1, nz.size())); st.set ("desc_pct", (double) desc / std::max<size_t> (1, nz.size()));
    int ml = 0; for (int i : ivs) ml = std::max (ml, std::abs (i)); st.set ("max_leap", ml);
    if (! pitches.empty()) { const auto mm = std::minmax_element (pitches.begin(), pitches.end()); st.set ("range", *mm.second - *mm.first); } else st.set ("range", 0);
    { std::map<int, int> c; int top = 0; for (int p : pitches) top = std::max (top, ++c[p]); st.set ("top_share", pitches.empty() ? 0.0 : (double) top / pitches.size()); }
    {   // n-gram entropy of (interval, ioi) 3-grams, normalised
        std::vector<std::pair<int, double>> toks; for (size_t i = 1; i < mono.size(); ++i) toks.push_back ({ mono[i].p - mono[i - 1].p, mono[i].s - mono[i - 1].s });
        std::map<std::string, int> gc; int N = 0;
        for (size_t i = 0; i + 2 < toks.size(); ++i) { std::string k; for (size_t j = i; j < i + 3; ++j) k += std::to_string (toks[j].first) + "/" + std::to_string ((int) std::lround (toks[j].second * 100)) + ";"; gc[k]++; ++N; }
        double Hh = 0; for (auto& kv : gc) { const double p = (double) kv.second / N; Hh -= p * std::log2 (p); }
        st.set ("ngram_entropy", N > 1 ? Hh / std::log2 ((double) N) : 0.0); st.set ("distinct_3grams", (double) gc.size());
    }
    {   // bar repetition
        std::vector<std::vector<std::pair<int, int>>> sig ((size_t) bars);
        for (auto& n : mono) { const int b = (int) (n.s / 16); if (b >= 0 && b < bars) sig[(size_t) b].push_back ({ (int) std::lround (std::fmod (n.s, 16.0) * 100), n.p }); }
        int r = 0; for (int i = 0; i < bars; ++i) for (int j = 0; j < i; ++j) if (sig[(size_t) i] == sig[(size_t) j]) { ++r; break; }
        st.set ("bar_repeat", (double) r / bars);
    }
    {   // syncopation
        double sync = 0;
        for (auto& n : mono)
        {
            const double p = std::fmod (n.s, 4.0);
            if (std::abs (p - 1) < 1e-9 || std::abs (p - 3) < 1e-9) sync += 1; else if (std::abs (p - 2) < 1e-9 && n.l >= 2) sync += 1;
            if (p > 1e-9 && p + n.l > 4.5) sync += 0.5;
        }
        st.set ("syncopation", sync / std::max<size_t> (1, mono.size()));
    }
    {   // phrase endings
        const Mono* e4 = nullptr; const Mono* e8 = nullptr;
        for (auto& n : mono) { if (n.s < total / 2) e4 = &n; if (n.s < total) e8 = &n; }
        st.set ("end_tonic", e8 && mod12 (e8->p) == mod12 (H.key) ? 1 : 0);
        st.set ("mid_open", e4 && mod12 (e4->p) != mod12 (H.key) ? 1 : 0);
        int dph = 0, nph = 0;
        for (double b0 = 0; b0 < total; b0 += 32) { std::vector<int> seg; for (auto& n : mono) if (n.s >= b0 && n.s < b0 + 32) seg.push_back (n.p); if (seg.size() > 1) { ++nph; if (seg.back() < seg.front()) ++dph; } }
        st.set ("phrase_desc", (double) dph / std::max (1, nph));
        if (! pitches.empty()) { size_t im = 0; for (size_t i = 0; i < pitches.size(); ++i) if (pitches[i] > pitches[im]) im = i; st.set ("peak_pos", (double) im / std::max<size_t> (1, pitches.size() - 1)); }
    }
    {   // voice leading of the chord lane (lib.movement)
        auto movement = [] (std::vector<int> a, std::vector<int> b) {
            std::sort (a.begin(), a.end()); std::sort (b.begin(), b.end());
            if (a.size() == b.size()) { double s = 0; for (size_t i = 0; i < a.size(); ++i) s += std::abs (a[i] - b[i]); return s; }
            const auto& sm = a.size() < b.size() ? a : b; const auto& bg = a.size() < b.size() ? b : a; double tot = 0;
            for (int x : sm) { int m = 99; for (int y : bg) m = std::min (m, std::abs (x - y)); tot += m; }
            for (int y : bg) if (std::find (sm.begin(), sm.end(), y) == sm.end()) { int m = 99; for (int x : sm) m = std::min (m, std::abs (x - y)); tot += m * 0.5; }
            return tot; };
        std::vector<std::vector<int>> V; const int loop = std::max (1, H.loopTicks);
        for (int base = 0; base < totalTicks; base += loop) for (auto& c : H.chords) if (base + c.t < totalTicks) V.push_back (c.voicing);
        double mv = 0, moved = 0; int k = 0;
        for (size_t i = 1; i < V.size(); ++i) { if (V[i] == V[i - 1]) continue; mv += movement (V[i - 1], V[i]); int mm = 0; for (int x : V[i]) if (std::find (V[i - 1].begin(), V[i - 1].end(), x) == V[i - 1].end()) ++mm; moved += mm; ++k; }
        st.set ("vl_semitones", k ? mv / k : 0.0); st.set ("vl_notes_moved", k ? moved / k : 0.0);
    }
    {   // semitone clashes on notes >= 1/8
        int cl = 0;
        for (auto& n : mono) { if (n.l < 1.5) continue; const auto& c = H.at ((int) std::lround (n.s * 24)); bool near = false; for (int q : c.pcs) if (mod12 (n.p - q) == 1 || mod12 (n.p - q) == 11) near = true; if (near && ! hasV (c.pcs, mod12 (n.p))) ++cl; }
        st.set ("clashes", cl);
    }
    if (arpI != nullptr && arpI->valid)
    {
        st.set ("arp_len", arpI->len); st.set ("polymeter", 16 % std::max (1, arpI->len) != 0 ? 1 : 0); st.set ("resync_bars", arpI->resync);
        st.set ("pedal_share", (double) arpI->pedalSteps / std::max (1, arpI->len)); st.set ("evolve_changes", arpI->events);
    }
    {   // anticipations: onsets 1 sixteenth before a chord change (or an 1/8 before, held)
        std::set<int> changes; const int loop = std::max (1, H.loopTicks);
        for (int base = 0; base < totalTicks; base += loop) for (auto& c : H.chords) { const int s = (base + c.t) / 24 - H.offset / 24; if (s > 0) changes.insert (s); }
        int a = 0; for (auto& n : mono) { const int s = (int) std::lround (n.s); if (changes.count (s + 1) || (changes.count (s + 2) && s % 2 == 0 && n.l >= 2)) ++a; }
        st.set ("anticipations", a);
    }
    { int s = 0; for (size_t i = 1; i < mono.size(); ++i) if (mono[i].p == mono[i - 1].p && mono[i].s - mono[i - 1].s <= 2) ++s; st.set ("stutters", s); }
    return st;
}

double recipeScore (const Metrics& m, const ModeRecipe& M)
{
    double s = 0, w = 0;
    for (auto& t : M.targets) { bool have = false; for (auto& e : m.v) if (e.first == t.first) have = true; if (! have) continue; s += t.second.w * fit (m.get (t.first), t.second); w += t.second.w; }
    return w > 0 ? s / w : 0;
}

// ---------------------------------------------------------------- hard rules
bool arpHardRules (const std::vector<Note>& notes, int figLen, int totalTicks, std::string* why)
{
    auto fail = [why] (const char* w) { if (why) *why = w; return false; };
    if (notes.size() < 2) return fail ("empty");
    const auto mono = monoLine (notes);
    for (size_t i = 1; i < mono.size(); ++i) if (mono[i].p == mono[i - 1].p) return fail ("same note twice in a row");
    if (mono.back().p == mono.front().p && totalTicks > 0) return fail ("same note across the loop wrap");
    int lo = 999, hi = -999; for (auto& n : mono) { lo = std::min (lo, n.p); hi = std::max (hi, n.p); }
    if (hi - lo > 15) return fail ("span > 15");
    (void) figLen;
    return true;
}

bool melodyHardRules (const std::vector<Note>& notes, const Harmony& H, int totalTicks, int mode, bool hasB, int aPeak, int bPeak, std::string* why)
{
    auto fail = [why] (const char* w) { if (why) *why = w; return false; };
    const auto mono = monoLine (notes);
    if (mono.size() < 6) return fail ("too few notes");
    const ModeRecipe& M = MR (mode);
    int lo = 999, hi = -999; for (auto& n : mono) { lo = std::min (lo, n.p); hi = std::max (hi, n.p); }
    if (hi - lo < M.rangeLo || hi - lo > M.rangeHi) return fail ("range");
    for (size_t i = 1; i < mono.size(); ++i) if (std::abs (mono[i].p - mono[i - 1].p) > M.maxLeap) return fail ("leap");
    // stutters: at most one per 2-bar phrase, never 3 identical notes in a row on 1/16s
    std::map<int, int> perPhrase;
    for (size_t i = 1; i < mono.size(); ++i) if (mono[i].p == mono[i - 1].p && mono[i].s - mono[i - 1].s <= 2) perPhrase[(int) (mono[i].s / 32)]++;
    for (auto& kv : perPhrase) if (kv.second > M.maxStutterPerPhrase) return fail ("stutters");
    for (size_t i = 2; i < mono.size(); ++i) if (mono[i].p == mono[i - 1].p && mono[i - 1].p == mono[i - 2].p && mono[i].s - mono[i - 2].s <= 2.01) return fail ("3 in a row");
    // nursery contours
    for (size_t i = 0; i + 5 < mono.size(); ++i)
    {
        const int a = mono[i + 1].p - mono[i].p, b = mono[i + 2].p - mono[i + 1].p;
        const bool run = a != 0 && b != 0 && std::abs (a) <= 2 && std::abs (b) <= 2 && (a > 0) == (b > 0);
        if (run && mono[i + 3].p == mono[i].p && mono[i + 4].p == mono[i + 1].p && mono[i + 5].p == mono[i + 2].p) return fail ("3-2-1 3-2-1");
    }
    for (size_t i = 0; i + 3 < mono.size(); ++i)
    {
        const int x = mono[i].p, y = mono[i + 1].p;
        if (x != y && std::abs (x - y) <= 2 && mono[i + 2].p == x && mono[i + 3].p == y) return fail ("2-3-2-3 chant");
    }
    {   // 5-6-5-3 (key degrees) and 1-5-8 + scale down openings
        const auto& sc = scaleOf (H.scaleName);
        auto deg = [&] (int p) { const int pc = mod12 (p - H.key); for (int k = 0; k < 7; ++k) if (sc[(size_t) k] == pc) return k + 1; return 0; };
        for (size_t i = 0; i + 3 < mono.size(); ++i)
            if (deg (mono[i].p) == 5 && deg (mono[i + 1].p) == 6 && deg (mono[i + 2].p) == 5 && deg (mono[i + 3].p) == 3 && mono[i + 1].p > mono[i].p && mono[i + 3].p < mono[i + 2].p) return fail ("5-6-5-3");
        if (mono.size() > 4 && deg (mono[0].p) == 1 && mono[1].p - mono[0].p == 7 && mono[2].p - mono[0].p == 12 && mono[3].p < mono[2].p && mono[2].p - mono[3].p <= 2) return fail ("1-5-8 opening");
    }
    // bar 4 open (SUNRISE), bar 8 resolves
    const double total = totalTicks / 24.0;
    const Mono* e4 = nullptr; const Mono* e8 = nullptr; for (auto& n : mono) { if (n.s < total / 2) e4 = &n; if (n.s < total) e8 = &n; }
    if (mode == Sunrise && e4 && mod12 (e4->p) == mod12 (H.key)) return fail ("bar 4 closed");
    if (e8)
    {
        const int pc = mod12 (e8->p - H.key);
        const bool overI = H.at ((int) std::lround (e8->s * 24)).root == mod12 (H.key);
        const int third = minorish (H.scaleName) ? 3 : 4;
        if (! (pc == 0 || (pc == third && overI))) return fail ("bar 8 doesn't resolve");
    }
    // a sustained semitone rub on a strong beat must resolve by step within an 1/8
    for (size_t i = 0; i < mono.size(); ++i)
    {
        const auto& n = mono[i]; if (std::fmod (n.s, 4.0) > 1e-9 || n.l < 2) continue;
        const int t = (int) std::lround (n.s * 24); if (! H.clash (n.p, t)) continue;
        const bool resolves = i + 1 < mono.size() && mono[i + 1].s - n.s <= 2 && std::abs (mono[i + 1].p - n.p) <= 2 && H.chordTone (mono[i + 1].p, (int) std::lround (mono[i + 1].s * 24));
        if (! resolves) return fail ("unresolved rub");
    }
    if (hasB && ! (bPeak - aPeak >= 3 && bPeak - aPeak <= 8)) return fail ("B doesn't lift");
    return true;
}

// ================================================================= MIDNIGHT: recipe arps
namespace
{
struct FigCtx { int key; std::string scale; int floor0, base60; };
FigCtx figCtx (const Harmony& H) { return { H.key, H.scaleName, tonicAtOrAbove (H.key, 55), tonicAtOrAbove (H.key, 60) }; }
std::vector<int> recipeLadder (const HChord& c, int floor) { std::vector<int> v; for (int p = floor; p < floor + 36; ++p) if (hasV (c.pcs, mod12 (p))) v.push_back (p); if (v.empty()) v.push_back (floor); return v; }
} // namespace

void realizeRecipeArp (std::vector<Note>& out, const Figure& base, const std::vector<FigEvent>* events, const Harmony& H, const ArpParams& P, int t0, int t1)
{
    const FigCtx C = figCtx (H);
    const int st = BAR / std::max (1, P.spb), loop = std::max (BAR, H.loopTicks), shift = 12 * P.reg;
    for (int t = t0; t < t1; t += st)
    {
        const int lt = (t - t0) % loop, bar = lt / BAR;
        const Figure* cur = &base; std::string pedal = base.pedalDeg; int segStart = 0;
        if (events != nullptr)
            for (auto& e : *events) if (bar >= e.fromBar) { segStart = e.fromBar * BAR; if (e.hasFig) cur = &e.fig; if (! e.pedalDeg.empty()) pedal = e.pedalDeg; }
        const int len = clampv (cur->len, 1, 8), rs = std::max (1, base.resync) * BAR;
        const int pos = (((lt - segStart) % rs) / st) % len;
        const Step& s = cur->s[(size_t) pos];
        if (s.rest) continue;
        if (s.hold) { if (! out.empty() && out.back().t + out.back().d >= t - st) out.back().d += st; continue; }
        const HChord& c = H.at (t);
        int p, v100;
        if (s.pedal) { p = degreePitch (pedal, C.key, C.scale, C.base60) + 12 * s.oct; v100 = 62; }
        else { const auto L = recipeLadder (c, C.floor0 + base.floorOff); p = L[(size_t) std::min ((int) L.size() - 1, std::max (0, s.idx - 1))] + 12 * s.oct; v100 = 70; }
        if (t % BAR == 0) v100 += 14; else if (t % PPQ == 0) v100 += 8;
        if (pos == 0) v100 += 6;
        if (s.acc) v100 += 14;
        Note n; n.t = t; n.p = clampv (p + shift, 24, 108); n.v = std::min (1.0f, (float) v100 / 100.0f); n.acc = pos == 0 || s.acc;
        n.d = s.tie ? st : std::max (6, (int) std::lround (st * P.gate));
        out.push_back (n);
    }
}

namespace
{
int figTopLadder (const Figure& f, const Harmony& H)
{
    const FigCtx C = figCtx (H); if (H.chords.empty()) return 0;
    const auto L = recipeLadder (H.chords[0], C.floor0 + f.floorOff); int top = -1;
    for (int i = 0; i < f.len; ++i) { const auto& s = f.s[(size_t) i]; if (! s.pedal && ! s.rest && ! s.hold) top = std::max (top, L[(size_t) std::min ((int) L.size() - 1, s.idx - 1)] + 12 * s.oct); }
    return top;
}
Figure varyFigure (Figure f, Rng& R)
{
    const double u = R(); const int k = f.len;
    if (u < 0.45) return f;
    if (u < 0.65) { for (int tries = 0; tries < 8; ++tries) { auto& s = f.s[(size_t) irnd (R, k)]; if (s.pedal || s.rest || s.hold) continue; s.idx = clampv (s.idx + (R() < 0.5 ? -1 : 1), 1, 4); break; } return f; }
    if (u < 0.8) { const int r = 1 + irnd (R, std::max (1, k - 1)); Figure g = f; for (int i = 0; i < k; ++i) g.s[(size_t) i] = f.s[(size_t) ((i + r) % k)]; return g; }
    if (u < 0.9) { int lo = 9, hi = 0; for (int i = 0; i < k; ++i) if (! f.s[(size_t) i].pedal && ! f.s[(size_t) i].rest && ! f.s[(size_t) i].hold) { lo = std::min (lo, f.s[(size_t) i].idx); hi = std::max (hi, f.s[(size_t) i].idx); }
                   for (int i = 0; i < k; ++i) { auto& s = f.s[(size_t) i]; if (! s.pedal && ! s.rest && ! s.hold) s.idx = lo + hi - s.idx; } return f; }
    for (int i = 0; i < k; ++i) if (f.s[(size_t) i].rest) { f.s[(size_t) i].rest = false; f.s[(size_t) i].idx = 1 + irnd (R, 3); break; }
    return f;
}
std::vector<std::string> liftTokens (const Figure& f, const HChord& c, const Harmony& H, int pedalPitch)
{   // pedal lift targets: b3 / 3rd above, the b7 common tone, the 9th of the chord, an octave up from a low pedal; chord tone or 9th only
    const FigCtx C = figCtx (H); std::vector<int> cand;
    cand.push_back (pedalPitch + 3); cand.push_back (pedalPitch + 4);
    { int b7 = degreePitch ("7", C.key, C.scale, C.base60); while (b7 < pedalPitch - 2) b7 += 12; while (b7 > pedalPitch + 9) b7 -= 12; cand.push_back (b7); }
    { int n9 = c.root; int p = pedalPitch - 2; while (mod12 (p) != mod12 (n9 + 2)) ++p; cand.push_back (p); }
    if (pedalPitch < figTopLadder (f, H)) cand.push_back (pedalPitch + 12);
    std::vector<std::string> out;
    for (int p : cand)
    {
        if (p == pedalPitch) continue;
        const bool ok = hasV (c.pcs, mod12 (p)) || mod12 (p - c.root) == 2;
        if (ok) out.push_back (degreeToken (p, C.key, C.scale, C.base60));
    }
    return out;
}
} // namespace

ArpResult composeRecipeArp (const Harmony& H, const ArpParams& P, int totalTicks)
{
    const ModeRecipe& M = MR (P.mood);
    const FigCtx C = figCtx (H);
    ArpResult best; double bs = -1e18; bool found = false;
    std::vector<std::pair<int, double>> figW; for (size_t i = 0; i < M.figures.size(); ++i) figW.push_back ({ (int) i, M.figures[i].weight });
    const int loop = std::max (BAR, H.loopTicks);
    const int K = std::max (24, recipe().arpCandidates * 2);
    for (int c = 0; c < K; ++c)
    {
        Rng R (P.seed * 2654435761u + (uint32_t) c * 2246822519u + 99u);
        const auto& T = M.figures[(size_t) figW[(size_t) pickIdx (R, figW)].first];
        const bool asIs = R() < 0.45;
        Figure f = figureFromTokens (T.tokens); if (! asIs) f = varyFigure (f, R);
        if (P.cycle >= 3 && P.cycle <= 8 && f.len != P.cycle)
        {   // the user picked a cycle length: stretch / trim the figure
            Figure g = f; g.len = P.cycle; for (int i = 0; i < P.cycle; ++i) { g.s[(size_t) i] = f.s[(size_t) (i % f.len)]; if (i >= f.len && ! g.s[(size_t) i].pedal) g.s[(size_t) i].idx = clampv (g.s[(size_t) i].idx + (i % 2 ? 1 : -1), 1, 4); }
            f = g;
        }
        f.resync = (f.len == 3 || f.len == 5) ? 2 : clampv (T.resync, 1, 2);
        if (P.resync > 0) f.resync = P.resync;
        // the pedal: the template's degree, a recipe alternative, or the pitch class most chords share
        const double up = R();
        if (up < 0.6 || M.pedalDeg.empty()) f.pedalDeg = T.pedal;
        else if (up < 0.85) f.pedalDeg = M.pedalDeg[(size_t) std::max (0, pickIdx (R, M.pedalDeg))].first;
        else { int pp = C.base60; while (mod12 (pp) != choosePedalPc (H)) ++pp; f.pedalDeg = degreeToken (pp + 12, C.key, C.scale, C.base60); }
        // the floor so the pedal sits just above the highest ladder note used
        int bestOff = 0, bd = 999;
        for (int off : { 0, -5, 7 })
        {
            Figure g = f; g.floorOff = off; const int top = figTopLadder (g, H), ped = degreePitch (f.pedalDeg, C.key, C.scale, C.base60);
            const int d = (ped > top && ped - top <= 7) ? std::abs (ped - top - 3) : 50 + std::abs (ped - top);
            if (d < bd) { bd = d; bestOff = off; }
        }
        f.floorOff = bestOff;
        // the slow events: one step changes at bar 5, the pedal lifts to a common tone at bar 6-7
        std::vector<FigEvent> ev;
        if (asIs && ! T.evolve.empty())
        {
            for (auto& e : T.evolve) { FigEvent fe; fe.fromBar = e.fromBar; if (! e.pattern.empty()) { fe.hasFig = true; fe.fig = figureFromTokens (e.pattern); fe.fig.floorOff = f.floorOff; fe.fig.resync = f.resync; } fe.pedalDeg = e.pedal; ev.push_back (fe); }
        }
        else if (loop >= 8 * BAR)
        {
            if (R() < 0.6) { FigEvent fe; fe.fromBar = 4; fe.hasFig = true; fe.fig = f; for (int tries = 0; tries < 8; ++tries) { auto& s = fe.fig.s[(size_t) irnd (R, f.len)]; if (s.pedal || s.rest || s.hold) continue; s.idx = clampv (s.idx + (R() < 0.5 ? 1 : -1), 1, 4); break; } ev.push_back (fe); }
            if (R() < M.liftP && ! H.chords.empty())
            {
                const int fb = R() < 0.7 ? 6 : 5;
                const auto lt = liftTokens (f, H.at (fb * BAR), H, degreePitch (f.pedalDeg, C.key, C.scale, C.base60));
                if (! lt.empty()) { FigEvent fe; fe.fromBar = fb; fe.pedalDeg = lt[(size_t) irnd (R, (int) lt.size())]; ev.push_back (fe); }
            }
        }
        std::stable_sort (ev.begin(), ev.end(), [] (const FigEvent& a, const FigEvent& b) { return a.fromBar < b.fromBar; });
        std::vector<Note> notes; realizeRecipeArp (notes, f, &ev, H, P, 0, loop);
        std::string why;
        if (! arpHardRules (notes, f.len, loop, &why)) continue;
        ArpInfo ai; ai.valid = true; ai.len = f.len; ai.resync = f.resync; ai.events = (int) ev.size(); for (int i = 0; i < f.len; ++i) if (f.s[(size_t) i].pedal) ++ai.pedalSteps;
        const double s = recipeScore (analyseLane (notes, H, loop, &ai), M) + R() * 0.01;
        if (s > bs) { bs = s; best.fig = f; best.events = ev; best.score = s; found = true; }
    }
    if (! found) return ArpResult();   // the caller falls back to the built-in engine
    best.pedalPc = mod12 (degreePitch (best.fig.pedalDeg, C.key, C.scale, C.base60));
    // the whole length: the loop (with its events) repeats; EVOLVE adds one small change per loop, guarded by the hard rules
    Figure cur = best.fig; Rng R (P.seed ^ 0xE70173u);
    for (int t = 0; t < totalTicks; t += loop)
    {
        if (t > 0 && P.evolveBars > 0)
        {
            Figure pick = cur;
            for (int k = 0; k < 12; ++k)
            {
                Figure g = cur; const int op = M.evolveOps.empty() ? irnd (R, 4) : pickIdx (R, M.evolveOps);
                const std::string name = M.evolveOps.empty() ? "" : M.evolveOps[(size_t) std::max (0, op)].first;
                if (name.find ("fill") != std::string::npos) { for (int i = 0; i < g.len; ++i) if (g.s[(size_t) i].rest) { g.s[(size_t) i].rest = false; g.s[(size_t) i].idx = 1 + irnd (R, 3); break; } }
                else if (name.find ("raise") != std::string::npos) { int ti = -1, tv = 0; for (int i = 0; i < g.len; ++i) if (! g.s[(size_t) i].pedal && g.s[(size_t) i].idx > tv) { tv = g.s[(size_t) i].idx; ti = i; } if (ti >= 0) g.s[(size_t) ti].idx = std::min (5, tv + 1); }
                else if (name.find ("pedal") != std::string::npos) { const auto lt = liftTokens (g, H.at (0), H, degreePitch (g.pedalDeg, C.key, C.scale, C.base60)); if (! lt.empty()) g.pedalDeg = lt[(size_t) irnd (R, (int) lt.size())]; }
                else { for (int tries = 0; tries < 8; ++tries) { auto& s = g.s[(size_t) irnd (R, g.len)]; if (s.pedal || s.rest || s.hold) continue; s.idx = clampv (s.idx + (R() < 0.5 ? 1 : -1), 1, 4); break; } }
                std::vector<Note> nn; realizeRecipeArp (nn, g, &best.events, H, P, 0, loop);
                if (arpHardRules (nn, g.len, loop)) { pick = g; break; }
            }
            cur = pick;
        }
        realizeRecipeArp (best.notes, cur, &best.events, H, P, t, std::min (totalTicks, t + loop));
    }
    return best;
}

// ================================================================= SUNRISE / DUSK: recipe hooks
namespace
{
struct HOn { int step; int len; };
std::string cellOf (Rng& R, const std::vector<std::pair<std::string, double>>& cells, int mode /*0 any but tie, 1 tie-in only*/)
{
    std::vector<std::pair<std::string, double>> c;
    for (auto& e : cells) { const bool tie = ! e.first.empty() && e.first[0] == '-'; if ((mode == 1) == tie && e.first.size() == 8) c.push_back (e); }
    if (c.empty()) return mode == 1 ? "--x-x---" : "x--x--x-";
    return c[(size_t) std::max (0, pickIdx (R, c))].first;
}
// a 2-bar phrase rhythm: bar 1 + bar 2, with the optional 1/16 pickup tied over the bar line
std::string phraseRhythm (Rng& R, const ModeRecipe& M, bool tieIn)
{
    for (int tries = 0; tries < 12; ++tries)
    {
        std::string b1 = cellOf (R, M.cellsTwo, 0) + cellOf (R, M.cellsTwo, 0);
        std::string b2 = (tieIn ? cellOf (R, M.cellsTwo, 1) : cellOf (R, M.cellsTwo, 0)) + cellOf (R, M.cellsTwo, 0);
        if (b1[0] == '.') b1[0] = 'x';
        if (tieIn) { b1[15] = 'x'; int k = 0; while (k < 8 && b2[(size_t) k] == '-') ++k; if (k == 0 || k > 4) { b2[0] = '-'; for (int j = 1; j < 8; ++j) if (b2[(size_t) j] == '-' && j >= 3) b2[(size_t) j] = '.'; } }
        // the phrase end: the last note holds
        std::string s = b1 + b2;
        int last = -1; for (int i = 31; i >= 24; --i) if (s[(size_t) i] == 'x') { last = i; break; }
        if (last >= 0) for (int i = last + 1; i < 32; ++i) s[(size_t) i] = '-';
        int on = 0; for (char ch : s) on += ch == 'x';
        if (on >= 8 && on <= 15) return s;
    }
    return "x--x--x-x-x---xx--x-x---x-------";
}
std::vector<HOn> onsets (const std::string& r)
{
    std::vector<HOn> o;
    for (int i = 0; i < (int) r.size(); ++i)
        if (r[(size_t) i] == 'x') { int j = i + 1; while (j < (int) r.size() && r[(size_t) j] == '-') ++j; o.push_back ({ i, j - i }); }
    return o;
}
struct Scl { int base; std::vector<int> sc; int pitch (int di) const { const int o = (int) std::floor (di / 7.0); return base + sc[(size_t) (di - o * 7)] + 12 * o; } };
int nearestDi (const Scl& S, int p) { int best = 0, bd = 999; for (int d = -14; d <= 21; ++d) { const int dd = std::abs (S.pitch (d) - p); if (dd < bd) { bd = dd; best = d; } } return best; }
} // namespace

std::map<std::string, int>& hookFailCounts() { static std::map<std::string, int> m; return m; }

std::vector<Note> composeRecipeHook (const Harmony& H, const MelodyParams& P, int phraseTicks)
{
    const ModeRecipe& M = MR (P.mood);
    const int phraseBars = std::max (2, phraseTicks / BAR), units = std::max (1, phraseBars / 2);
    Scl S { tonicAtOrAbove (H.key, 60 + 12 * P.reg), scaleOf (H.scaleName) };
    std::vector<Note> best; double bs = -1e18;
    // the form: the user's pick (FORM), roles per 2-bar phrase
    static const char* forms[] { "AAAB", "ABAB", "ABAC", "AAAA" };
    const std::string form = forms[clampv (P.form, 0, 3)];
    const int K = std::max (64, recipe().melodyCandidates * 3);
    for (int c = 0; c < K; ++c)
    {
        Rng R (P.seed * 1597334677u + (uint32_t) c * 3266489917u + 5u);
        const bool tieIn = R() < M.tieInP;
        const std::string rA = phraseRhythm (R, M, tieIn);
        const auto onA = onsets (rA);
        // A: an opening (degrees or scale-step intervals), then a contour with at most one 4th-6th leap + stepwise fill back
        std::vector<int> di;
        if (! M.openDeg.empty() && R() < 0.5) { const auto& d = M.openDeg[(size_t) std::max (0, pickIdx (R, M.openDeg))].first; for (int x : d) di.push_back (x - 1); }
        else
        {
            const int st0 = R() < 0.4 ? 0 : R() < 0.5 ? 2 : 4; di.push_back (st0);
            if (! M.openSteps.empty()) for (int x : M.openSteps[(size_t) std::max (0, pickIdx (R, M.openSteps))].first) di.push_back (di.back() + x);
        }
        const int contour = pickIdx (R, std::vector<std::pair<int, double>> { { 0, M.contour[0] }, { 1, M.contour[1] }, { 2, M.contour[2] }, { 3, M.contour[3] } });
        // A stays inside a small window (about 5 scale steps) so B can lift a 3rd-5th and the whole hook stays within 9-14 semitones
        int wLo = 99, wHi = -99; for (int d : di) { wLo = std::min (wLo, d); wHi = std::max (wHi, d); }
        wLo = std::min (wLo, wHi - 4); wHi = std::max (wHi, wLo + 5); if (wHi - wLo > 6) wHi = wLo + 6;
        bool leapUsed = false; int fillDir = 0, lastMv = 0;
        while (di.size() < onA.size())
        {
            const double x = (double) di.size() / std::max<size_t> (1, onA.size() - 1);
            int mv;
            if (fillDir != 0) { mv = fillDir; if (R() < 0.45) fillDir = 0; }
            else if (! leapUsed && R() < 0.16) { mv = (R() < 0.5 ? 1 : -1) * (3 + irnd (R, 2)); leapUsed = true; fillDir = mv > 0 ? -1 : 1; }
            else if (contour == 0) mv = x < 0.55 ? (R() < 0.72 ? 1 : -1) : (R() < 0.78 ? -1 : 1);
            else if (contour == 1) mv = R() < 0.78 ? -1 : 1;
            else if (contour == 2) mv = R() < 0.72 ? 1 : -1;
            else mv = R() < 0.5 ? 1 : -1;
            if (mv == -lastMv && std::abs (mv) == 1 && R() < 0.6) mv = lastMv;   // no back-and-forth chants
            int nd = di.back() + mv;
            if (nd > wHi) nd = di.back() - 1;
            if (nd < wLo) nd = di.back() + 1;
            lastMv = nd - di.back(); di.push_back (nd);
        }
        di.resize (onA.size());
        // keep it in a singable register around the tonic
        { int mn = 99, mx = -99; for (int d : di) { mn = std::min (mn, d); mx = std::max (mx, d); } const int mid = (mn + mx) / 2; const int sh = (int) std::lround ((3 - mid) / 7.0) * 7; for (auto& d : di) d += sh; }
        // phrases from A by the operators
        struct Ph { std::vector<HOn> on; std::vector<int> di; char role; bool closed; };
        auto endOn = [&] (std::vector<int>& d, bool closed, int lastStep, int unitStart)
        {   // the last note: open (2, 5, 6) or closed (1; 3 over I)
            if (d.empty()) return;
            int& e = d.back(); (void) lastStep; (void) unitStart;
            std::vector<int> want = closed ? std::vector<int> { 0 } : std::vector<int> { 1, 4, 5 };
            int bestD = e, bd2 = 99;
            for (int k = e - 4; k <= e + 4; ++k) { const int dm = ((k % 7) + 7) % 7; if (std::find (want.begin(), want.end(), dm) != want.end() && std::abs (k - e) < bd2) { bd2 = std::abs (k - e); bestD = k; } }
            e = bestD;
        };
        std::vector<Ph> phs;
        Ph A { onA, di, 'A', false };
        Ph B = A; B.role = 'B';
        {   // B: A's bar-1 rhythm + first 3-4 notes, then lifted a 3rd-5th so it peaks above A
            const int keep = 3 + irnd (R, 2), lift = 2 + irnd (R, 2);
            int aTop = -99; for (int d : A.di) aTop = std::max (aTop, d);
            for (size_t i = (size_t) keep; i < B.di.size(); ++i) B.di[i] += lift;
            int bTop = -99; size_t bi = 0; for (size_t i = (size_t) keep; i < B.di.size(); ++i) if (B.di[i] > bTop) { bTop = B.di[i]; bi = i; }
            if (bTop < aTop + 2 && bi < B.di.size()) B.di[bi] = aTop + 2;
            if (bTop > aTop + 4) for (size_t i = (size_t) keep; i < B.di.size(); ++i) B.di[i] -= bTop - (aTop + 4);
        }
        Ph Cc = B; Cc.role = 'C';
        {   // C: B's bar 1, then a stepwise descending tail down to the tonic
            size_t firstBar2 = 0; while (firstBar2 < Cc.on.size() && Cc.on[firstBar2].step < 16) ++firstBar2;
            for (size_t i = firstBar2; i < Cc.di.size(); ++i) Cc.di[i] = (i == firstBar2 ? Cc.di[i - (i > 0 ? 1 : 0)] : Cc.di[i - 1]) - 1;
        }
        const int tailVar = R() < 0.55 ? 0 : 1 + irnd (R, 2);   // A': 0 = exact repeat, else only its tail changes
        for (int u = 0; u < units; ++u)
        {
            const char role = form[(size_t) (u % 4)];
            Ph ph = role == 'B' ? B : role == 'C' ? Cc : A;
            const bool last = u == units - 1;
            if (role == 'A' && u > 0 && tailVar != 0 && ph.di.size() > 2) ph.di[ph.di.size() - 2] += tailVar == 1 ? 1 : -1;
            ph.closed = last || (form == "AAAB" && u == 1) || (form == "AAAA" && u % 2 == 1);
            if (u == 1 && P.mood == Sunrise) ph.closed = false;   // bar 4 stays open (SUNRISE winners)
            if (u == 1 && P.mood == Dusk) ph.closed = true;       // DUSK winners land bar 4 on the tonic
            phs.push_back (ph);
        }
        // realise: degrees -> pitches; strong beats pulled onto chord tones (decided per note, so repeats stay exact),
        // the pickup onto the next chord, the endings, and no accidental repeated notes
        std::vector<Note> notes; int aPeak = -999, bPeak = -999; int prevP = -1;
        const uint32_t pullSeed = (uint32_t) (R() * 4294967295.0);
        for (int u = 0; u < (int) phs.size(); ++u)
        {
            Ph& ph = phs[(size_t) u];
            endOn (ph.di, ph.closed, ph.on.empty() ? 0 : ph.on.back().step, u);
            for (size_t i = 0; i < ph.on.size(); ++i)
            {
                const auto& o = ph.on[i]; const int t = (u * 32 + o.step) * 24;
                int p = S.pitch (ph.di[i]);
                const bool beat = o.step % 4 == 0, pickup = o.step == 15 && tieIn;
                const HChord& ch = pickup ? H.at (t + 24) : H.at (t);
                Rng pr (pullSeed ^ (uint32_t) (ph.role * 131 + (int) i * 7919));
                if (pickup)
                {   // lands on the next chord's 3rd, 9th or root
                    std::vector<int> tg; for (int q = p - 5; q <= p + 5; ++q) { const int rel = mod12 (q - ch.root); if ((rel == 0 || rel == 3 || rel == 4 || rel == 2) && hasV (S.sc, mod12 (q - H.key))) tg.push_back (q); }
                    if (! tg.empty()) { int b = tg[0]; for (int q : tg) if (std::abs (q - (prevP < 0 ? p : prevP)) < std::abs (b - (prevP < 0 ? p : prevP))) b = q; p = b; }
                }
                else if (beat && ! hasV (ch.pcs, mod12 (p)) && (pr() < 0.42 || o.len >= 3))
                {
                    int b = p, bd2 = 99; for (int q = p - 3; q <= p + 3; ++q) if (hasV (ch.pcs, mod12 (q)) && hasV (S.sc, mod12 (q - H.key)) && std::abs (q - p) < bd2) { bd2 = std::abs (q - p); b = q; }
                    p = b;
                }
                if (! hasV (S.sc, mod12 (p - H.key)) && ! hasV (ch.pcs, mod12 (p))) p = S.pitch (nearestDi (S, p));
                // no accidental repeats (the one deliberate stutter comes later)
                if (! notes.empty() && notes.back().p == p && t - notes.back().t <= 48 && ! pickup)
                {
                    const int d0 = nearestDi (S, p); const int alt1 = S.pitch (d0 - 1), alt2 = S.pitch (d0 + 1);
                    p = (i + 1 < ph.di.size() && ph.di[i + 1] > ph.di[i]) ? alt2 : alt1;
                }
                Note n; n.t = t; n.p = p; n.d = std::max (12, (int) std::lround (o.len * 24 * 0.85));
                int v100 = 68; if (o.step % 16 == 0) v100 += 14; else if (o.step % 4 == 0) v100 += 7; else if (o.step % 2 == 1) v100 -= 6;
                if (prevP == p) v100 -= 5;
                n.v = std::min (1.0f, v100 / 100.0f); n.acc = beat || pickup;
                notes.push_back (n); prevP = p;
                if (ph.role == 'A') aPeak = std::max (aPeak, p);
                if (ph.role == 'B') bPeak = std::max (bPeak, p);
            }
        }
        // the tie-in: the pickup holds 1-4 steps into the next bar (its length comes from the cell)
        // STUTTER: at most one bounce per phrase
        if (P.stutter > 0.001)
            for (int u = 0; u < (int) phs.size(); ++u)
            {
                if (R() >= P.stutter * 0.8) continue;
                for (size_t i = 0; i < notes.size(); ++i)
                {
                    if (notes[i].t / (32 * 24) != u || notes[i].d < 2 * 24 || (i + 1 < notes.size() && notes[i + 1].p == notes[i].p) || (i > 0 && notes[i - 1].p == notes[i].p) || R() < 0.6) continue;
                    Note s = notes[i]; s.t += 24; s.d = 14; s.v *= 0.84f; s.acc = false; notes[i].d = 14;
                    notes.insert (notes.begin() + (long) i + 1, s); break;
                }
            }
        sortNotes (notes);
        const bool hasB = form.find ('B') != std::string::npos && units >= 2;
        std::string why;
        if (! melodyHardRules (notes, H, phraseTicks, P.mood, hasB, aPeak, bPeak, &why)) { hookFailCounts()[why]++; continue; }
        hookFailCounts()["PASS"]++;
        const auto m = analyseLane (notes, H, phraseTicks);
        const double s = recipeScore (m, M) + m.get ("ngram_entropy") * 0.001 - m.get ("clashes") * 0.0001 + R() * 0.0005;
        if (s > bs) { bs = s; best = notes; }
    }
    return best;   // empty = nothing passed the hard rules (the caller falls back)
}
} // namespace arp
