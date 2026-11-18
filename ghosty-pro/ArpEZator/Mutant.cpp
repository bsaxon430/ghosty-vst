// MUTANT ARP engine: see Mutant.h. Every number here is listed in work/arp/RESEARCH.md section 3 (the mode table).
#include "Mutant.h"
#include "CyberpopTable.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace arp
{
namespace
{
constexpr int kFloor = 48;   // C3: octave 0 of the ladder

// ---------------------------------------------------------------- the mode table (RESEARCH.md section 3)
struct Prof
{
    double lenW[5];                 // 8 12 16 24 32
    double notesPer16;              // at NOTE DENSITY 0.5 (used by the scorer)
    double chord, pass, chrom;      // onset shares: chord tone / scale neighbour / chromatic approach
    double leap, oct, oct2, rev;    // P(interval >= 7), P(octave), P(two octaves), P(reversal after a leap)
    int pedalEvery, pedalKind;      // pedal every N steps; 1 low pedal (root), 2 high ping (top tone), 3 alternate (DIGITAL)
    int octLo, octHi;               // register (octaves above C3)
    double ratchet, burst, hold, rest;
    double gate, gate2;             // gate (DIGITAL alternates gate / gate2)
    double velSpread, swing;
    int accent;                     // 0 start + 3-3-2, 1 start + 3-3-3-3-4, 2 every other step, 3 bar start only
    int varBars, changes, sig;      // variation period, notes changed per variation, signature (locked opening) length
    double glideP, echo;            // glide share (at GLIDE 0.5), fragment-echo chance per echo point
    double stutter;                 // P(a step repeats the last pitch as a ratcheted stutter) (HYPER: chopped-vocal habit)
    double lift;                    // P(the variation bar's second half climbs an octave: the whiplash top-register ending)
    double phrasey, repeat, grace, trill;   // FLUTE: phrase-based rests (breaths) instead of per-step rests; repeated-note stabs; ornaments
    const CyberTable* learned = nullptr;    // CYBERPOP: the cell comes from the learned table (generateLearned), these numbers are derived from it
};
Prof learnedProf (const CyberTable& T)
{   // the mode table row CYBERPOP gets from its learned table (the scorer and MUTATE read it; the cell itself is drawn in generateLearned)
    Prof M {};
    const double one = T.motifBars[0], two = T.motifBars[1] + T.motifBars[2];
    M.lenW[0] = 0; M.lenW[1] = 0; M.lenW[2] = one; M.lenW[3] = 0; M.lenW[4] = two;
    M.notesPer16 = T.perBar; M.chord = clampv (0.5 * (T.chordStrong + T.chordWeak), 0.3, 0.95); M.pass = clampv (T.scale - M.chord, 0.0, 0.6); M.chrom = clampv (1.0 - T.scale, 0.0, 0.2);
    M.leap = T.leap; M.oct = T.oct; M.oct2 = 0.01; M.rev = T.reversal;
    M.pedalEvery = 0; M.pedalKind = 0;
    M.octLo = clampv ((T.reg - T.range / 2 - kFloor) / 12, 0, 3); M.octHi = clampv (M.octLo + std::max (1, (T.range + 2) / 12), M.octLo + 1, 4);
    M.ratchet = clampv (T.ratchet, 0.0, 0.5); M.burst = 0.04; M.hold = clampv (T.dur[2] + T.dur[3] + T.dur[4] + T.dur[5], 0.0, 0.8); M.rest = clampv (1.0 - T.perBar / 16.0, 0.05, 0.8);
    M.gate = 0.6; M.gate2 = 0.6; M.velSpread = clampv (T.velSd * 2.5, 0.15, 0.5); M.swing = clampv ((T.swing - 0.5) * 2.0, 0.0, 0.5);
    M.accent = 3; M.varBars = T.motifBars[2] > std::max (T.motifBars[0], T.motifBars[1]) ? 4 : 2; M.changes = clampv ((int) std::lround (T.variation * 16.0), 1, 6); M.sig = 4;
    M.glideP = 0.05; M.echo = clampv (T.barRepeat, 0.0, 0.9); M.stutter = clampv (T.ratchet * 0.5, 0.0, 0.3); M.lift = 0;
    M.phrasey = 0; M.repeat = 0; M.grace = 0; M.trill = 0; M.learned = &T;
    return M;
}
const Prof& prof (int m)
{
    if (m == MutCyberpop) { static const Prof C = learnedProf (cyberTable()); return C; }
    static const Prof T[kMutantModes - 1] {
        //  8    12   16   24   32   n/16 chord pass chrom  leap  oct  oct2  rev  pedN kind lo hi  ratch burst hold  rest  gate  gate2 vel   swing acc var chg sig glide echo  stut  lift  phr  rep   grace trill
        { { 0.30, 0.10, 0.40, 0.10, 0.10 }, 14, 0.80, 0.18, 0.02, 0.32, 0.18, 0.03, 0.78, 6, 2, 2, 3, 0.08, 0.06, 0.05, 0.12, 0.45, 0.45, 0.35, 0.00, 0, 2, 3, 4, 0.10, 0.40, 0.00, 0.10, 0, 0.00, 0.00, 0.00 },   // RAGE
        { { 0.20, 0.25, 0.30, 0.15, 0.10 }, 12, 0.62, 0.22, 0.16, 0.30, 0.10, 0.02, 0.65, 5, 1, 2, 3, 0.06, 0.10, 0.06, 0.25, 0.55, 0.55, 0.30, 0.08, 1, 4, 4, 3, 0.25, 0.35, 0.00, 0.00, 0, 0.00, 0.00, 0.00 },   // ALIEN
        { { 0.35, 0.05, 0.40, 0.05, 0.15 }, 13, 0.78, 0.20, 0.02, 0.26, 0.16, 0.02, 0.80, 2, 3, 2, 3, 0.12, 0.08, 0.08, 0.18, 0.30, 0.75, 0.40, 0.00, 2, 2, 2, 4, 0.05, 0.45, 0.04, 0.00, 0, 0.00, 0.00, 0.00 },   // DIGITAL
        { { 0.10, 0.10, 0.45, 0.10, 0.25 },  9, 0.72, 0.34, 0.02, 0.18, 0.09, 0.00, 0.85, 0, 2, 1, 2, 0.02, 0.00, 0.22, 0.42, 0.95, 0.95, 0.20, 0.12, 3, 4, 2, 2, 0.55, 0.30, 0.00, 0.00, 0, 0.00, 0.00, 0.00 },   // DREAM
        { { 0.25, 0.10, 0.45, 0.05, 0.15 }, 15, 0.72, 0.26, 0.04, 0.30, 0.22, 0.06, 0.70, 4, 2, 2, 3, 0.20, 0.12, 0.06, 0.08, 0.85, 0.85, 0.40, 0.00, 4, 2, 4, 4, 0.08, 0.45, 0.18, 0.60, 0, 0.00, 0.00, 0.00 },   // HYPER
        { { 0.10, 0.10, 0.50, 0.10, 0.20 },  6, 0.70, 0.30, 0.00, 0.12, 0.04, 0.00, 0.90, 0, 1, 2, 3, 0.04, 0.00, 0.35, 0.50, 0.70, 0.70, 0.30, 0.06, 3, 2, 2, 3, 0.20, 0.50, 0.00, 0.00, 1, 0.40, 0.18, 0.08 } }; // FLUTE
    return T[clampv (m, 0, kMutantModes - 2)];
}
double knob (double v, double at, double maxMul = 2.5) { return clampv (v / std::max (0.001, at), 0.0, maxMul); }   // 1.0 at the knob's reference position

// chord tones ordered root, third, fifth, seventh (then the rest) as pitch classes
std::vector<int> ladderPcs (const HChord& c)
{
    std::vector<int> out;
    if (c.pcs.empty()) { out.push_back (c.root); return out; }
    auto pick = [&] (int lo, int hi) { for (int pc : c.pcs) { const int iv = mod12 (pc - c.root); if (iv >= lo && iv <= hi && std::find (out.begin(), out.end(), pc) == out.end()) { out.push_back (pc); return; } } };
    out.push_back (c.root);
    pick (3, 4); pick (6, 8); pick (10, 11);
    for (int pc : c.pcs) if (std::find (out.begin(), out.end(), pc) == out.end() && out.size() < 4) out.push_back (pc);
    return out;
}
int nearestScale (const std::vector<int>& scale, int p, int dir)   // the next scale tone above (dir > 0) / below p; p itself if the scale is empty
{
    if (scale.empty()) return p;
    for (int k = 1; k <= 12; ++k) { const int q = p + dir * k; if (std::find (scale.begin(), scale.end(), mod12 (q)) != scale.end()) return q; }
    return p;
}
int accentOf (int pattern, int step, int len)
{
    switch (pattern)
    {
        case 0: { static const int g[] { 3, 3, 2 }; int s = 0, k = 0; while (s < len) { if (step == s) return 1; s += g[k % 3]; ++k; } return 0; }
        case 1: { static const int g[] { 3, 3, 3, 3, 4 }; int s = 0, k = 0; while (s < len) { if (step == s) return 1; s += g[k % 5]; ++k; } return 0; }
        case 2: return step % 2 == 0 ? 1 : 0;
        case 4: { static const int g[] { 4, 4, 3, 3, 2 }; int s = 0, k = 0; while (s < len) { if (step == s) return 1; s += g[k % 5]; ++k; } return 0; }
        default: return step == 0 || (len >= 16 && step == len / 2) ? 1 : 0;
    }
}
struct Ctx { const Harmony* H; const Prof* M; MutantParams P; int n = 3; int lo = 0, hi = 0; };   // n = ladder size of the first chord, lo / hi = ladder positions
int posOf (const MutantStep& s, int n) { return s.oct * n + s.idx; }
void setPos (MutantStep& s, int pos, int n) { pos = std::max (0, pos); s.oct = pos / n; s.idx = pos % n; }
int stepPitchRaw (const MutantStep& s, const HChord& c, int reg)
{
    const auto L = ladderPcs (c); const int n = (int) L.size();
    int oct = s.oct + (s.idx >= 0 ? s.idx / n : 0), i = ((s.idx % n) + n) % n;
    const int base = kFloor + 12 * (oct + reg);
    return base + mod12 (L[(size_t) i] - base);
}
} // namespace

const char* mutantModeName (int m) { static const char* n[] { "RAGE", "ALIEN", "DIGITAL", "DREAM", "HYPER", "FLUTE", "CYBERPOP" }; return n[clampv (m, 0, kMutantModes - 1)]; }
const char* mutantTwoLineName (int k) { static const char* n[] { "ONE LINE", "ANSWER", "INTERLOCK", "SHADOW" }; return n[clampv (k, 0, kTwoLines - 1)]; }
const char* mutantModeBlurb (int m)
{
    static const char* b[] { "Fast, aggressive, octave-jumping synth sequences", "Unusual intervals, chromatic approaches, unpredictable turns",
                             "Video-game plucks: alternating pitches, fast runs, glitch fills", "Floating, overlapping lines with glides and space",
                             "Sugar-rush chaos: bright top register, club kick accents, stutters and whiplash octaves",
                             "FLUTE (an older session's mode): plays as saved; pick a mode to generate anew",
                             "Futuristic pop hooks from the learned style table: bright catchy lines, octave lifts, bar-to-bar repeats" };
    return b[clampv (m, 0, kMutantModes - 1)];
}
int mutantVariationBars (const MutantParams& P) { return P.variationBars > 0 ? P.variationBars : prof (P.mode).varBars; }
int mutantStepTicks (int div) { return std::max (1, BAR / clampv (div, 4, 64)); }

int mutantPitch (const MutantStep& s, const Harmony& H, int t, const MutantParams& P)
{
    static const HChord none;
    const HChord& c = H.chords.empty() ? none : (P.chordFollow ? H.at (t) : H.chords.front());
    int p = stepPitchRaw (s, c, P.reg);
    if (s.col == 1 || s.col == -1) p = nearestScale (c.scale, p, s.col);
    else if (s.col == 2 || s.col == -2)
    {
        if (P.scaleLock) p = nearestScale (c.scale, p, s.col > 0 ? -1 : 1);   // SCALE LOCK: the approach note is the scale neighbour instead
        else p += s.col > 0 ? -1 : 1;
    }
    return clampv (p, 24, 108);
}

// ---------------------------------------------------------------- GENERATE
namespace
{
MutantCell generateOne (const Ctx& X, Rng& R, const MutantCell* keep)
{
    const Prof& M = *X.M; const MutantParams& P = X.P; const int n = X.n;
    MutantCell c;
    // length
    if (P.cellSteps >= 4) c.len = clampv (P.cellSteps, 4, 64);
    else { static const int L[] { 8, 12, 16, 24, 32 }; double tot = 0; for (double w : M.lenW) tot += w; double r = R() * tot; c.len = 16; for (int k = 0; k < 5; ++k) { r -= M.lenW[k]; if (r <= 0) { c.len = L[k]; break; } } }
    if (keep != nullptr && keep->valid() && mutantLockedCount (*keep) > 0) c.len = keep->len;   // locked steps keep their places
    c.s.assign ((size_t) c.len, MutantStep {});
    std::vector<bool> forced ((size_t) c.len, false);
    if (keep != nullptr && keep->valid() && keep->len == c.len) for (int i = 0; i < c.len; ++i) if (keep->s[(size_t) i].lock) { c.s[(size_t) i] = keep->s[(size_t) i]; forced[(size_t) i] = true; }
    // ---- rhythm skeleton
    const double pRest = clampv (M.rest * (1.0 - P.density) * 4.0, 0.0, 0.85);   // (accents and the signature never rest, so the eligible steps carry the whole share)
    const double pRatchet = clampv (M.ratchet * knob (P.retrigger, 0.3) + (P.density > 0.75 ? (P.density - 0.75) * 0.4 : 0.0), 0.0, 0.6);
    const double pBurst = clampv (M.burst * knob (P.retrigger, 0.3), 0.0, 0.5);
    const double pHold = clampv (M.hold * (1.0 + (0.5 - P.density)), 0.0, 0.5);
    const double gateMul = knob (P.gate, 0.5, 2.0);
    for (int i = 0; i < c.len; ++i)
    {
        auto& s = c.s[(size_t) i];
        if (forced[(size_t) i]) continue;
        s.acc = accentOf (M.accent, i, c.len) != 0;
        const bool sig = i < M.sig;
        s.rest = ! sig && ! s.acc && R() < pRest;
        if (! s.rest && ! sig && i > 0 && c.s[(size_t) i - 1].hold && ! forced[(size_t) i - 1]) { s.rest = true; continue; }   // the step a hold runs into
        if (s.rest) continue;
        s.gate = clampv ((M.accent == 2 && i % 2 == 1 ? M.gate2 : M.gate) * gateMul, 0.1, 1.5);
        if (i + 1 < c.len && ! sig && R() < pHold) { s.hold = true; s.gate = clampv (std::max (s.gate, 0.6), 0.5, 1.0); }
        else if (R() < pRatchet) s.ratchet = R() < 0.15 ? 3 : 2;
        s.vel = s.acc ? 0.92 : clampv (0.92 - M.velSpread, 0.3, 0.9);
        if (P.stepSkip > 0 && ! sig && ! s.acc) s.prob = clampv (1.0 - P.stepSkip * 0.6, 0.3, 1.0);
    }
    if (M.phrasey > 0.5)
    {   // FLUTE: phrases of 2..7 notes, then a breath of 2..5 steps (longer with less NOTE DENSITY); the first note of a phrase is accented,
        // its last note often held; repeated-note stabs, a grace note on some phrase starts, a trill on some long notes
        const double breathMul = clampv (1.6 - P.density * 1.2, 0.4, 1.6);
        int i = 0; bool first = true;
        while (i < c.len)
        {
            const int ph = 2 + (int) (R() * 6.0), br = std::max (1, (int) std::lround ((3 + (int) (R() * 4.0)) * breathMul));
            for (int k = 0; k < ph && i < c.len; ++k, ++i)
            {
                auto& s = c.s[(size_t) i]; if (forced[(size_t) i]) continue;
                s.rest = false; s.hold = false; s.ratchet = 1; s.acc = k == 0; s.vel = s.acc ? 0.9 : 0.68 + R() * 0.12; s.gate = clampv (0.55 * gateMul, 0.2, 1.0);
                if (k == 0 && ! first && R() < M.grace) s.grace = true;
                if (k == ph - 1 || R() < 0.12) { s.hold = i + 1 < c.len; s.gate = clampv (0.9 * gateMul, 0.5, 1.2); if (R() < M.trill) s.trill = true; }
                if (s.hold) { ++i; if (i < c.len && ! forced[(size_t) i]) { c.s[(size_t) i].rest = true; } ++k; }
            }
            for (int k = 0; k < br && i < c.len; ++k, ++i) if (! forced[(size_t) i]) { c.s[(size_t) i].rest = true; c.s[(size_t) i].hold = false; }
            first = false;
        }
        if (! forced[0]) { c.s[0].rest = false; c.s[0].acc = true; }
    }
    if (c.len >= 8 && M.phrasey < 0.5 && R() < pBurst)   // a 1/64 burst at the end of the cell (the fill)
    {
        const int k = c.len - 1 - (R() < 0.5 ? 0 : 1);
        if (! forced[(size_t) k] && ! c.s[(size_t) k].hold) { c.s[(size_t) k].rest = false; c.s[(size_t) k].ratchet = 4; c.s[(size_t) k].gate = 0.9; }
    }
    // ---- pitch walk
    const double pOct = clampv (M.oct * knob (P.octaveJump, 0.5), 0.0, 0.6), pOct2 = clampv (M.oct2 * knob (P.octaveJump, 0.5), 0.0, 0.2);
    const double pLeap = clampv (M.leap * 0.35 * std::pow (knob (P.intervalChaos, 0.3), 0.7), 0.03, 0.6);   // (the pedal / ping jumps and octave moves make the rest of the leap share)
    const double pPass = clampv (M.pass * 2.2 * (0.6 + knob (P.intervalChaos, 0.3) * 0.4), 0.0, 0.7);   // (only weak steps that resolve are eligible: about half)
    const double pChrom = P.scaleLock ? 0.0 : clampv (M.chrom * knob (P.intervalChaos, 0.3), 0.0, 0.4);
    const double pRev = clampv (M.rev - (P.intervalChaos - 0.3) * 0.4, 0.3, 0.95);
    const double pGlide = clampv (M.glideP * knob (P.glide, 0.5), 0.0, 1.0);
    const int spanOct = clampv ((int) std::lround ((M.octHi - M.octLo) + (P.octaveJump - 0.5) * 2.0), 0, 3);
    const int lo = M.octLo * n, hi = (M.octLo + spanOct + 1) * n - 1;
    int pos = M.octLo * n + (R() < 0.6 ? 0 : (n > 2 ? 2 : 1)); if (M.pedalKind == 2) pos = (M.octLo + 1) * n;   // start on the root (or the fifth)
    int dir = R() < 0.6 ? 1 : -1, lastIv = 0, lastDir = 1; int lastPos = pos; bool lastColoured = false;
    auto pitchOfPos = [&] (int q) { MutantStep t; setPos (t, q, n); return stepPitchRaw (t, X.H->chords.empty() ? HChord {} : X.H->chords.front(), P.reg); };
    for (int i = 0; i < c.len; ++i)
    {
        auto& s = c.s[(size_t) i];
        if (forced[(size_t) i]) { if (! s.rest) { const int q = posOf (s, n); lastIv = pitchOfPos (q) - pitchOfPos (lastPos); if (q != lastPos) lastDir = q > lastPos ? 1 : -1; lastPos = q; } continue; }
        if (s.rest) continue;
        // echo: a fragment from earlier in the cell (often the signature), maybe an octave away
        if (i >= 6 && i + 3 <= c.len && i % 4 == 0 && R() < M.echo)
        {
            const int a = R() < 0.55 ? 0 : (int) (R() * (i - 3)) / 2 * 2; const int sh = R() < 0.35 ? (R() < 0.5 ? 1 : -1) : 0;
            bool ok = true; for (int k = 0; k < 3; ++k) if (c.s[(size_t) (a + k)].rest || forced[(size_t) (i + k)]) ok = false;
            if (ok)
            {
                for (int k = 0; k < 3; ++k)
                {
                    auto& d = c.s[(size_t) (i + k)]; const auto& src = c.s[(size_t) (a + k)];
                    d.rest = false; d.idx = src.idx; d.oct = clampv (src.oct + sh, lo / n, hi / n); d.col = src.col; d.role = src.role; forced[(size_t) (i + k)] = true;
                    if (d.hold) { d.hold = false; }
                }
                --i; continue;   // the loop re-enters this step as forced (updates the walk state)
            }
        }
        const bool pedalStep = M.pedalEvery > 0 && i > 0 && (M.pedalKind == 3 ? i % 2 == 1 : i % M.pedalEvery == M.pedalEvery - 1) && ! s.acc;
        if (pedalStep && ! lastColoured)
        {
            if (M.pedalKind == 2) { s.role = 2; setPos (s, hi - (n > 3 ? 1 : 0), n); }               // the high ping: the top tone of the register
            else { s.role = 1; setPos (s, M.octLo * n, n); if (M.pedalKind == 3 && R() < 0.3) s.idx = n > 2 ? 2 : 0; }   // the low pedal: the root (DIGITAL: sometimes the fifth)
            const int q = posOf (s, n); lastIv = pitchOfPos (q) - pitchOfPos (lastPos); if (q != lastPos) lastDir = q > lastPos ? 1 : -1; lastPos = q; pos = q; lastColoured = false;
            continue;
        }
        if (M.repeat > 0 && i > 0 && ! s.acc && ! c.s[(size_t) i - 1].rest && R() < M.repeat)
        {   // a repeated-note stab (the flute habit): the same pitch again, a touch softer
            setPos (s, lastPos, n); s.col = 0; s.role = 0; s.vel = clampv (s.vel * 0.9, 0.3, 1.0); lastIv = 0; continue;
        }
        if (M.stutter > 0 && i > 0 && ! s.acc && R() < M.stutter * knob (P.retrigger, 0.3, 2.0))
        {   // stutter: the last pitch again as a chopped ratchet (the vocal-chop habit)
            setPos (s, lastPos, n); s.col = 0; s.role = 0; s.ratchet = R() < 0.3 ? 3 : 2; s.hold = false; s.gate = std::min (s.gate, 0.6);
            lastIv = 0; continue;
        }
        // direction: after a leap the line turns back (post-skip reversal); otherwise it mostly keeps going
        if (std::abs (lastIv) >= 7) dir = R() < pRev ? -lastDir : lastDir;
        else dir = R() < 0.58 ? lastDir : -lastDir;
        if (pos <= lo + 1) dir = 1; else if (pos >= hi - 1) dir = -1;
        int next = pos; const double r = R();
        if (r < pOct2 && pos + dir * 2 * n >= lo && pos + dir * 2 * n <= hi) next = pos + dir * 2 * n;
        else if (r < pOct2 + pOct && pos + dir * n >= lo && pos + dir * n <= hi) next = pos + dir * n;
        else if (r < pOct2 + pOct + pLeap) next = pos + dir * (2 + (int) (R() * n));
        else if (r < pOct2 + pOct + pLeap + 0.08) next = pos;
        else next = pos + dir;
        if (next < lo) next = lo + (lo - next) % std::max (1, n);
        if (next > hi) next = hi - (next - hi) % std::max (1, n);
        next = clampv (next, lo, hi);
        if (next == lastPos && i > 0 && R() < 0.7) next = clampv (next + dir, lo, hi);   // no dull repeats unless meant
        setPos (s, next, n); s.role = 0; s.col = 0;
        // colour: a neighbour / approach note on a weak step that resolves on the next sounding step
        const bool nextSounds = i + 1 < c.len && ! c.s[(size_t) i + 1].rest && ! forced[(size_t) i + 1] && ! s.hold;
        if (nextSounds && ! lastColoured && i >= M.sig && (! s.acc || M.accent == 2))
        {   // (accents stay chord tones: they are the intentional ones; DIGITAL's alternating accents are just the line against its pedal)
            const double rc = R();
            if (rc < pChrom) { s.col = R() < 0.6 ? 2 : -2; s.ratchet = 1; s.hold = false; auto& nx = c.s[(size_t) i + 1]; nx.idx = s.idx; nx.oct = s.oct; nx.col = 0; nx.role = 0; forced[(size_t) i + 1] = true; }   // (an approach note is one note)
            else if (rc < pChrom + pPass * 1.3) s.col = R() < 0.5 ? 1 : -1;
        }
        lastColoured = s.col == 2 || s.col == -2;
        lastIv = pitchOfPos (next) - pitchOfPos (lastPos); if (next != lastPos) lastDir = next > lastPos ? 1 : -1; lastPos = next; pos = next;
        if (nextSounds && R() < pGlide && std::abs (lastIv) <= 12) s.glide = true;
    }
    // the signature (the locked-by-default opening) always sounds and never repeats a pitch twice in a row
    for (int i = 0; i < std::min (M.sig, c.len); ++i)
    {
        auto& s = c.s[(size_t) i]; if (forced[(size_t) i]) continue;
        s.rest = false;
        if (i > 0 && posOf (s, n) == posOf (c.s[(size_t) i - 1], n) && s.col == c.s[(size_t) i - 1].col) setPos (s, clampv (posOf (s, n) + (R() < 0.5 ? n : 2), lo, hi), n);
    }
    // holds: the step after a hold is silent
    for (int i = 0; i + 1 < c.len; ++i) if (c.s[(size_t) i].hold && ! c.s[(size_t) i].rest && ! forced[(size_t) i + 1]) c.s[(size_t) i + 1].rest = true;
    return c;
}


uint32_t mix32 (uint32_t a, uint32_t b, uint32_t c);
// ---------------------------------------------------------------- CYBERPOP: the cell drawn from the learned style table
// a pitch -> the ladder step that plays it over this chord: the chord tone itself (col 0), else the chord tone a scale step below /
// above it (col +1 / -1), so the step follows chord changes like every other step
bool stepFromPitch (MutantStep& s, int p, const HChord& c, int reg, int n)
{
    for (int pass = 0; pass < 2; ++pass)
        for (int oct = 0; oct <= 5; ++oct) for (int i = 0; i < n; ++i)
        {
            MutantStep t; t.idx = i; t.oct = oct; const int q = stepPitchRaw (t, c, reg);
            if (pass == 0) { if (q == p) { s.idx = i; s.oct = oct; s.col = 0; return true; } }
            else if (nearestScale (c.scale, q, 1) == p) { s.idx = i; s.oct = oct; s.col = 1; return true; }
            else if (nearestScale (c.scale, q, -1) == p) { s.idx = i; s.oct = oct; s.col = -1; return true; }
        }
    return false;
}
int drawIndex (Rng& R, const std::vector<double>& w)
{
    double tot = 0; for (double x : w) tot += x; double r = R() * tot;
    for (size_t k = 0; k < w.size(); ++k) { r -= w[k]; if (r <= 0) return (int) k; }
    return (int) w.size() - 1;
}
// the zigzag stair: four notes whose moves alternate up / down while the line steps the same way (a two-note figure sequenced
// down or up, e.g. +2 -4 +2), its first three on an even pulse. The stock pop-hook figure, and the shape of the lead lines the
// table was learned from: drawn move by move the walk lands on their exact four-note figures now and then, so the fourth note
// keeps the last direction instead (the zigzag share of the line stays, the sequenced stair does not). Locked steps are left.
bool zigzagStair (int a, int b, int c, int d)
{
    const int i1 = b - a, i2 = c - b, i3 = d - c;
    if (i1 == 0 || i2 == 0 || i3 == 0 || (i1 > 0) == (i2 > 0) || (i2 > 0) == (i3 > 0)) return false;
    const int s1 = i1 + i2, s2 = i2 + i3;
    return s1 != 0 && s2 != 0 && (s1 > 0) == (s2 > 0);
}
void breakStairs (MutantCell& c, const std::vector<bool>& forced, const Ctx& X, const HChord& ch, int pLo, int pHi, int n)
{
    std::vector<int> at, pit;
    for (int i = 0; i < c.len; ++i) if (! c.s[(size_t) i].rest) { at.push_back (i); pit.push_back (mutantPitch (c.s[(size_t) i], *X.H, 0, X.P)); }
    for (size_t k = 3; k < at.size(); ++k)
    {
        const int g1 = at[k - 2] - at[k - 3], g2 = at[k - 1] - at[k - 2];
        if (g1 != g2 || g1 > 4 || ! zigzagStair (pit[k - 3], pit[k - 2], pit[k - 1], pit[k])) continue;
        auto& s = c.s[(size_t) at[k]];
        if (forced[(size_t) at[k]] || s.lock) continue;
        const int dir = pit[k - 1] > pit[k - 2] ? 1 : -1;   // keep going the way the third note went
        for (int tries = 0; tries < 2; ++tries)
        {
            const int d = tries == 0 ? dir : -dir;
            int q = nearestScale (ch.scale, pit[k - 1] + d, d);
            if (q < pLo || q > pHi) continue;
            MutantStep t = s;
            if (! stepFromPitch (t, q, ch, X.P.reg, n)) continue;
            const int r = mutantPitch (t, *X.H, 0, X.P);
            if (zigzagStair (pit[k - 3], pit[k - 2], pit[k - 1], r) || r == pit[k - 1]) continue;
            s = t; pit[k] = r; break;
        }
    }
}
MutantCell generateLearned (const Ctx& X, Rng& R, const MutantCell* keep)
{
    const Prof& M = *X.M; const CyberTable& T = *M.learned; const MutantParams& P = X.P; const int n = X.n;
    const HChord ch = X.H->chords.empty() ? HChord {} : X.H->chords.front();
    MutantCell c;
    // length: the packs' repeat period (1 bar = 16 steps, 2 or 4 bars = 32); a chosen length wins; locked steps keep their places
    if (P.cellSteps >= 4) c.len = clampv (P.cellSteps, 4, 64);
    else c.len = R() * (T.motifBars[0] + T.motifBars[1] + T.motifBars[2]) < T.motifBars[0] ? 16 : 32;
    if (keep != nullptr && keep->valid() && mutantLockedCount (*keep) > 0) c.len = keep->len;
    c.s.assign ((size_t) c.len, MutantStep {});
    std::vector<bool> forced ((size_t) c.len, false);
    if (keep != nullptr && keep->valid() && keep->len == c.len) for (int i = 0; i < c.len; ++i) if (keep->s[(size_t) i].lock) { c.s[(size_t) i] = keep->s[(size_t) i]; forced[(size_t) i] = true; }
    // the pitch walk's own stream, seeded before the rhythm is drawn: the rhythm knobs (NOTE DENSITY, RETRIGGER, GATE) do not
    // re-roll the tune (3.2.2 build 4 seeded it after the rhythm and drew the moves from the rhythm stream, so they did)
    Rng Rp (mix32 (P.seed ^ 0xC1BE2u, (uint32_t) c.len, (uint32_t) std::lround (R() * 65536.0)));
    // ---- rhythm: the onset grid (NOTE DENSITY scales it around the packs' own density), a note length drawn from their shares,
    // ratchets at their rate and beat positions (RETRIGGER), the velocity curve by position (VELOCITY RANDOM widens it)
    const double dens = 0.5 + P.density, gateMul = knob (P.gate, 0.5, 2.0);
    const double pRatchet = clampv (T.ratchet * knob (P.retrigger, 0.3), 0.0, 0.5);
    static const int lens[] { 1, 1, 2, 4, 8, 12 };
    std::vector<double> durW (T.dur, T.dur + 6);
    for (int i = 0; i < c.len;)
    {
        auto& s = c.s[(size_t) i];
        if (forced[(size_t) i]) { ++i; continue; }
        const int g = i % 16;
        if (! (i == 0 || R() < clampv (T.onset[g] * dens, 0.0, 0.98))) { s.rest = true; ++i; continue; }
        const int k = drawIndex (R, durW); int L = lens[k];
        int room = 1; while (i + room < c.len && ! forced[(size_t) (i + room)] && room < L) ++room; L = std::min (L, room);
        s.rest = false; s.hold = false; s.sus = L; s.ratchet = k == 0 ? 2 : 1;
        s.gate = clampv ((L >= 2 ? 0.92 : 0.6) * gateMul, 0.1, 1.5);
        if (L == 1 && s.ratchet == 1 && R() < pRatchet * 2.4 * T.ratchetPos[g % 4]) s.ratchet = R() < 0.2 ? 3 : 2;
        s.vel = clampv (T.velPos[g] + (R() - 0.5) * 2.0 * T.velSd * knob (P.velRandom, 0.3, 2.0), 0.3, 1.0); s.acc = g == 0 || T.velPos[g] >= 0.85;
        if (P.stepSkip > 0 && g != 0 && ! s.acc) s.prob = clampv (1.0 - P.stepSkip * 0.6, 0.3, 1.0);
        for (int q = 1; q < L; ++q) c.s[(size_t) (i + q)].rest = true;   // the steps the note covers
        i += L;
    }
    // ---- pitches: a walk whose moves are drawn from the packs' interval histogram (INTERVAL CHAOS flattens it toward wilder
    // moves, OCTAVE JUMP scales the octave bins), turning back after a leap at their reversal rate, snapped to a chord tone on
    // strong steps / the scale otherwise at their shares, inside their register, all from the walk's own stream (Rp, above)
    // the register: the packs' median pitch +- half their range (OCTAVE JUMP widens it up to an octave each way; REG moves it)
    const int widen = (int) std::lround ((P.octaveJump - 0.5) * 24.0);
    const int pLo = clampv (T.reg - T.range / 2 - 1 - std::max (0, widen) / 2 + 12 * P.reg, 36, 96), pHi = clampv (T.reg + (T.range + 1) / 2 + 1 + std::max (0, widen) + 12 * P.reg, pLo + 7, 108);
    const int lo = 0, hi = 5 * n;
    auto pitchOfPos = [&] (int q) { MutantStep t; setPos (t, q, n); return stepPitchRaw (t, ch, P.reg); };
    const double temp = std::max (0.3, knob (P.intervalChaos, 0.3));
    // the learned histogram comes from a few hundred notes, most of them one hook, so drawn as-is the walk can chain that hook's
    // moves; it is smoothed with a generic prior first (moves shrink in weight with size, as in melodies in general, RESEARCH 1.4;
    // no unison, which the packs never play): shares stay near the table, exact chains of their moves become rare
    std::vector<double> w (25);
    {
        constexpr double kPrior = 0.22; double pz = 0; std::array<double, 25> pr {};
        for (int k = 0; k < 25; ++k) { const int a = std::abs (k - 12); pr[(size_t) k] = a == 0 ? 0.0 : 1.0 / (1.0 + 0.35 * a); pz += pr[(size_t) k]; }
        for (int k = 0; k < 25; ++k) w[(size_t) k] = std::pow ((1.0 - kPrior) * T.interval[k] + kPrior * pr[(size_t) k] / pz + 1e-4, 1.0 / temp);
    }
    const double octMul = knob (P.octaveJump, 0.5); w[0] *= octMul; w[24] *= octMul;
    auto chordNear = [&] (int q, int dir) { for (int d = 0; d <= 2; ++d) { for (int sgn : { dir, -dir }) { const int r = q + sgn * d; if (std::find (ch.pcs.begin(), ch.pcs.end(), mod12 (r)) != ch.pcs.end()) return r; } } return -1; };   // a chord tone within a tone, else none (the scale keeps the drawn move)
    auto scaleNear = [&] (int q, int dir) { if (std::find (ch.scale.begin(), ch.scale.end(), mod12 (q)) != ch.scale.end()) return q; const int a = nearestScale (ch.scale, q, dir), b = nearestScale (ch.scale, q, -dir); return std::abs (a - q) <= std::abs (b - q) ? a : b; };
    int p = clampv (scaleNear ((pLo + pHi) / 2, 1), pLo, pHi), lastIv = 0; { const int cq = chordNear (p, 1); if (cq > 0) p = cq; }
    for (int i = 0; i < c.len; ++i)
    {
        auto& s = c.s[(size_t) i];
        if (s.rest) continue;
        if (forced[(size_t) i]) { const int q = mutantPitch (s, *X.H, 0, P); lastIv = q - p; p = q; continue; }
        if (i > 0)
        {
            int iv = drawIndex (Rp, w) - 12;
            if (std::abs (lastIv) >= 7 && iv != 0 && (iv > 0) == (lastIv > 0) && Rp() < T.reversal) iv = -iv;   // after a leap the line turns back
            int q = p + iv; if (q < pLo || q > pHi) q = p - iv; q = clampv (q, pLo, pHi);
            const int dir = iv >= 0 ? 1 : -1;
            { const int cq = Rp() < ((i % 4) == 0 ? T.chordStrong : T.chordWeak) ? chordNear (q, dir) : -1; q = cq > 0 ? cq : scaleNear (q, dir); }
            if (q == p && iv != 0 && Rp() > T.repeat * 2.0) q = clampv (scaleNear (p + dir, dir), pLo, pHi);   // (the snap made a repeat the packs would not)
            lastIv = q - p; p = q;
        }
        if (! stepFromPitch (s, p, ch, P.reg, n)) { setPos (s, clampv ((lo + hi) / 2, lo, hi), n); s.col = 0; }
        s.role = 0;
    }
    (void) pitchOfPos;
    if (c.len >= 32 && R() < T.barRepeat)
    {   // the packs' form: the second bar repeats the first (its rhythm; its pitches too at their exact-repeat rate, else with their
        // share of notes moved by a step or two): the hook's A A' shape
        const bool exact = R() < T.barRepeatExact / std::max (0.05, T.barRepeat);
        for (int i = 0; i < 16 && 16 + i < c.len; ++i)
        {
            if (forced[(size_t) (16 + i)]) continue;
            MutantStep d = c.s[(size_t) i]; d.lock = false;
            if (! exact && ! d.rest && R() < T.variation)
            {
                const int q0 = mutantPitch (d, *X.H, 0, P); const int dir = R() < 0.5 ? 1 : -1;
                int q = clampv (nearestScale (ch.scale, q0, dir), pLo, pHi); if (R() < 0.4) q = clampv (nearestScale (ch.scale, q, dir), pLo, pHi);
                if (i % 4 == 0 && R() < T.chordStrong) { const int cq = chordNear (q, dir); if (cq > 0) q = clampv (cq, pLo, pHi); }
                if (! stepFromPitch (d, q, ch, P.reg, n)) d = c.s[(size_t) i];
            }
            c.s[(size_t) (16 + i)] = d;
        }
    }
    breakStairs (c, forced, X, ch, pLo, pHi, n);
    return c;
}

// how close a cell comes to the mode's numbers (RESEARCH.md): the best of K candidates is kept
double scoreCell (const MutantCell& c, const Ctx& X)
{
    const Prof& M = *X.M; MutantParams P = X.P; P.evolveBars = 0; P.stepSkip = 0;
    const int bars = 2; const auto notes = mutantRealize (c, *X.H, P, BAR * bars);
    const auto st = mutantAnalyse (notes, *X.H, BAR * bars);
    double s = 0;
    auto fit = [&] (double v, double target, double tol, double w) { s -= w * std::min (3.0, std::abs (v - target) / tol); };
    const double wantNotes = M.notesPer16 * (0.5 + P.density) * (16.0 / 16.0) * (P.div / 16.0);
    fit (st.notesPerBar, wantNotes, 2.5, 1.0);
    fit (st.leapShare, clampv (M.leap * std::pow (knob (P.intervalChaos, 0.3), 0.7), 0.05, 0.6), 0.10, 1.4);
    fit (st.octShare, clampv (M.oct * knob (P.octaveJump, 0.5), 0.0, 0.6), 0.08, 1.2);
    fit (st.chordShare, P.scaleLock ? M.chord + M.chrom * 0.5 : M.chord, 0.10, 1.0);
    fit (st.reversal, M.rev, 0.15, 0.8);
    if (st.repeatFrag > 0.5) s += 0.6;
    if (st.range > 31) s -= 0.8;
    // a strong opening: the first sig steps all sound, and the top note of the cell is rare (the "unexpected high note")
    int top = -1, topN = 0; std::map<int, int> cnt;
    for (int i = 0; i < c.len; ++i) if (! c.s[(size_t) i].rest) { const int q = posOf (c.s[(size_t) i], X.n); if (q > top) { top = q; topN = 1; } else if (q == top) ++topN; ++cnt[q]; }
    if (topN > 2) s -= 0.4 * (topN - 2);
    if (cnt.size() < 3) s -= 1.5;
    int same = 1, worst = 1; for (int i = 1; i < c.len; ++i) { const auto& a = c.s[(size_t) i], & b = c.s[(size_t) i - 1]; if (! a.rest && ! b.rest && posOf (a, X.n) == posOf (b, X.n) && a.col == b.col) worst = std::max (worst, ++same); else same = 1; }
    if (worst > 3) s -= 1.0 * (worst - 3);
    return s;
}
} // namespace

MutantCell mutantGenerate (const Harmony& H, const MutantParams& P, const MutantCell* keep)
{
    Ctx X; X.H = &H; X.M = &prof (P.mode); X.P = P;
    if (P.twoLine == TwoInterlock) X.P.density = clampv (P.density * 0.7, 0.0, 1.0);   // INTERLOCK: the first line leaves room for the second
    X.n = H.chords.empty() ? 3 : (int) ladderPcs (H.chords.front()).size();
    MutantCell best; double bs = -1e18; constexpr int K = 32;
    for (int k = 0; k < K; ++k)
    {
        Rng R (P.seed * 2654435761u + (uint32_t) k * 40503u + 17u);
        auto c = X.M->learned != nullptr ? generateLearned (X, R, keep) : generateOne (X, R, keep);
        const double s = scoreCell (c, X) - k * 1e-6;
        if (s > bs) { bs = s; best = c; }
    }
    return best;
}

// ---------------------------------------------------------------- MUTATE
namespace
{
// CYBERPOP's MUTATE: the same vocabulary the table was learned from. A pitch op redraws one note's move from the interval histogram
// (OCTAVE CHAOS: an octave, when the packs have any); a rhythm op moves a note by the onset grid, redraws its length from the
// length shares, or (rarely, at the packs' rate) ratchets it. No fills, bursts or leaps to the top the packs do not show.
MutantCell mutateLearned (MutantCell c, const Harmony& H, const MutantParams& P, const CyberTable& T, Rng& R, const std::vector<int>& free, int ops, int kind, int n)
{
    const HChord ch = H.chords.empty() ? HChord {} : H.chords.front();
    const int pLo = clampv (T.reg - T.range / 2 - 1 + 12 * P.reg, 36, 96), pHi = clampv (T.reg + (T.range + 1) / 2 + 1 + 12 * P.reg, pLo + 7, 108);
    auto chordNear = [&] (int q, int dir) { for (int d = 0; d <= 2; ++d) { for (int sgn : { dir, -dir }) { const int r = q + sgn * d; if (std::find (ch.pcs.begin(), ch.pcs.end(), mod12 (r)) != ch.pcs.end()) return r; } } return -1; };   // a chord tone within a tone, else none (the scale keeps the drawn move)
    auto scaleNear = [&] (int q, int dir) { if (std::find (ch.scale.begin(), ch.scale.end(), mod12 (q)) != ch.scale.end()) return q; const int a = nearestScale (ch.scale, q, dir), b = nearestScale (ch.scale, q, -dir); return std::abs (a - q) <= std::abs (b - q) ? a : b; };
    std::vector<double> w (T.interval, T.interval + 25);
    static const int lens[] { 1, 1, 2, 4, 8, 12 }; std::vector<double> durW (T.dur, T.dur + 6);
    auto pick = [&] { return free[(size_t) (R() * (double) free.size()) % free.size()]; };
    for (int o = 0; o < ops; ++o)
    {
        const int i = pick(); auto& s = c.s[(size_t) i];
        const bool pitchOp = kind == 1 || (kind == 0 && R() < 0.6);
        if (pitchOp)
        {
            if (s.rest) continue;
            int prev = -1; for (int k = i - 1; k >= 0; --k) if (! c.s[(size_t) k].rest) { prev = mutantPitch (c.s[(size_t) k], H, 0, P); break; }
            const int cur = mutantPitch (s, H, 0, P); if (prev < 0) prev = cur;
            int iv = kind == 1 && T.oct > 0.005 ? (R() < 0.5 ? 12 : -12) : drawIndex (R, w) - 12;
            int q = prev + iv; if (q < pLo || q > pHi) q = prev - iv; q = clampv (q, pLo, pHi);
            { const int cq = R() < ((i % 4) == 0 ? T.chordStrong : T.chordWeak) ? chordNear (q, iv >= 0 ? 1 : -1) : -1; q = cq > 0 ? cq : scaleNear (q, iv >= 0 ? 1 : -1); }
            if (q == cur) q = clampv (scaleNear (cur + (R() < 0.5 ? 1 : -1), 1), pLo, pHi);
            MutantStep t = s; if (stepFromPitch (t, q, ch, P.reg, n)) { s.idx = t.idx; s.oct = t.oct; s.col = t.col; }
            s.role = 0;
        }
        else
        {
            const double q = R(); const int g = i % 16;
            if (q < 0.35 && i != 0)
            {   // a note moves to a neighbouring grid step the packs use (or rests there)
                const int j = i + (R() < 0.5 ? -1 : 1); if (j < 0 || j >= c.len || c.s[(size_t) j].lock) continue;
                if (s.rest) { if (R() < T.onset[g]) { s = c.s[(size_t) j]; s.rest = false; s.sus = 1; s.lock = false; c.s[(size_t) j].rest = true; } }
                else if (c.s[(size_t) j].rest && R() < T.onset[j % 16] + 0.2) { c.s[(size_t) j] = s; c.s[(size_t) j].lock = false; s = MutantStep {}; s.rest = true; }
            }
            else if (q < 0.75)
            {   // a new length from the packs' shares (the steps it covers rest)
                if (s.rest) { s.rest = false; s.gate = 0.9; s.vel = T.velPos[g]; }
                int L = lens[drawIndex (R, durW)]; int room = 1; while (i + room < c.len && ! c.s[(size_t) (i + room)].lock && room < L) ++room; L = std::min (L, room);
                for (int k = 1; k < s.sus && i + k < c.len; ++k) if (! c.s[(size_t) (i + k)].lock && c.s[(size_t) (i + k)].rest && R() < T.onset[(i + k) % 16]) c.s[(size_t) (i + k)].rest = false;   // (a shortened note frees its steps)
                s.sus = L; s.hold = false; s.ratchet = 1; for (int k = 1; k < L; ++k) c.s[(size_t) (i + k)].rest = true;
                s.gate = clampv (L >= 2 ? 0.92 : 0.6, 0.1, 1.5);
            }
            else if (q < 0.9) { s.acc = ! s.acc; s.vel = clampv (s.acc ? T.velPos[g] + T.velSd : T.velPos[g] - T.velSd, 0.3, 1.0); }
            else if (R() < T.ratchet * 4.0 && ! s.rest && s.sus <= 1) s.ratchet = s.ratchet == 1 ? 2 : 1;
        }
    }
    if (! c.s[0].lock) c.s[0].rest = false;
    for (int i = 0; i < c.len; ++i) if (! c.s[(size_t) i].rest) for (int k = 1; k < c.s[(size_t) i].sus && i + k < c.len; ++k) if (! c.s[(size_t) (i + k)].lock) c.s[(size_t) (i + k)].rest = true;
    return c;
}
} // namespace

MutantCell mutantMutate (const MutantCell& c0, const Harmony& H, const MutantParams& P, uint32_t seed, double amount, int kind)
{
    MutantCell c = c0; if (! c.valid()) return c;
    const Prof& M = prof (P.mode); const int n = H.chords.empty() ? 3 : (int) ladderPcs (H.chords.front()).size();
    Rng R (seed * 1597334677u + (uint32_t) c.len * 7919u + 101u);
    std::vector<int> free; for (int i = 0; i < c.len; ++i) if (! c.s[(size_t) i].lock) free.push_back (i);
    if (free.empty()) return c;
    if (M.learned != nullptr)
    {   // CYBERPOP: as many notes change as the packs change on a repeat (their `variation` share of the sounding notes at the default
        // amount; MUTATE / EVOLVE scale it), at least one
        int sounding = 0; for (int i : free) sounding += c.s[(size_t) i].rest ? 0 : 1;
        return mutateLearned (c, H, P, *M.learned, R, free, std::max (1, (int) std::lround (clampv (amount, 0.0, 1.0) * 3.3 * M.learned->variation * sounding)), kind, n);
    }
    const int spanOct = clampv ((int) std::lround ((M.octHi - M.octLo) + (P.octaveJump - 0.5) * 2.0), 0, 3);
    const int lo = M.octLo * n, hi = (M.octLo + spanOct + 1) * n - 1;
    const int ops = std::max (1, (int) std::lround (clampv (amount, 0.0, 1.0) * (double) free.size() * 0.35));
    auto pick = [&] { return free[(size_t) (R() * (double) free.size()) % free.size()]; };
    for (int o = 0; o < ops; ++o)
    {
        const int i = pick(); auto& s = c.s[(size_t) i];
        const double r = R();
        const bool pitchOp = kind == 1 || (kind == 0 && r < 0.6);
        if (pitchOp)
        {
            const double q = R();
            if (s.rest) { s.rest = false; s.gate = M.gate; s.vel = 0.92 - M.velSpread; }
            if (q < 0.35) setPos (s, clampv (posOf (s, n) + (R() < 0.5 ? n : -n), lo, hi), n);                      // octave pop
            else if (q < 0.55) { s.idx = (s.idx + 1 + (int) (R() * (n - 1))) % n; }                                   // another chord tone
            else if (q < 0.70 && kind != 1) s.col = s.col != 0 ? 0 : (R() < 0.5 ? 1 : -1);                             // neighbour on / off
            else if (q < 0.82) { setPos (s, hi - (R() < 0.5 ? 0 : 1), n); s.col = 0; }                                 // leap to the top
            else if (q < 0.92 && kind != 1) { const int d = R() < 0.5 ? 1 : -1; setPos (s, clampv (posOf (s, n) + d * (1 + (int) (R() * 2)), lo, hi), n); }   // a step / skip
            else if (c.len >= 8 && i + 3 <= c.len)                                                                      // echo the signature here
            { for (int k = 0; k < 3; ++k) { auto& d = c.s[(size_t) (i + k)]; if (d.lock) continue; const auto& src = c.s[(size_t) k]; d.rest = src.rest; d.idx = src.idx; d.oct = clampv (src.oct + (R() < 0.4 ? 1 : 0), lo / n, hi / n); d.col = src.col; } }
            s.role = 0;
        }
        else
        {
            const double q = R();
            if (q < 0.25 && i != 0) { s.rest = ! s.rest; if (! s.rest) { s.gate = M.gate; s.vel = 0.92 - M.velSpread; s.ratchet = 1; } }   // rest on / off
            else if (q < 0.50) { if (s.rest) s.rest = false; s.ratchet = s.ratchet == 1 ? 2 : 1; }                               // ratchet on / off
            else if (q < 0.62 && i + 1 < c.len) { s.hold = ! s.hold; if (s.hold) { s.rest = false; s.ratchet = 1; } }            // hold on / off
            else if (q < 0.80 && c.len >= 8)                                                                                      // a fill: the last steps run up, the last one ratchets
            {
                const int f = 3 + (int) (R() * 2); int p0 = clampv (posOf (c.s[(size_t) (c.len - f - 1 >= 0 ? c.len - f - 1 : 0)], n), lo, hi - f);
                for (int k = 0; k < f; ++k) { auto& d = c.s[(size_t) (c.len - f + k)]; if (d.lock) continue; d.rest = false; d.hold = false; setPos (d, clampv (p0 + k + 1, lo, hi), n); d.col = 0; d.role = 0; d.ratchet = k == f - 1 ? (R() < 0.3 ? 4 : 2) : 1; d.gate = M.gate; d.vel = 0.92 - M.velSpread * 0.5; }
            }
            else if (q < 0.90) { s.acc = ! s.acc; s.vel = s.acc ? 0.92 : 0.92 - M.velSpread; }                                  // accent moves
            else { s.rest = false; s.ratchet = 4; s.gate = 0.9; }                                                                 // a 1/64 burst
        }
    }
    // tidy: step 0 sounds, holds silence the step after them, a chromatic approach always resolves (else it becomes a neighbour)
    if (! c.s[0].lock) { c.s[0].rest = false; c.s[0].hold = c.s[0].hold && c.len > 1; }
    for (int i = 0; i < c.len; ++i)
    {
        auto& s = c.s[(size_t) i];
        if (s.hold && i + 1 < c.len && ! c.s[(size_t) i + 1].lock) c.s[(size_t) i + 1].rest = true;
        if ((s.col == 2 || s.col == -2) && ! s.rest)
        {
            s.ratchet = 1; s.hold = false;
            const bool resolves = i + 1 < c.len && ! c.s[(size_t) i + 1].rest && c.s[(size_t) i + 1].col == 0 && c.s[(size_t) i + 1].idx == s.idx && c.s[(size_t) i + 1].oct == s.oct;
            if (! resolves) s.col = s.col > 0 ? -1 : 1;
        }
    }
    return c;
}

// ---------------------------------------------------------------- REALIZE
namespace
{
int cellIndexAt (const MutantCell& c, const MutantParams& P, int q)   // step q of a variation period -> cell step
{
    const int L = std::max (1, c.len); q += P.rotate;
    if (P.direction == 1) return L - 1 - (q % L);
    if (P.direction == 2 && L > 1) { const int cyc = 2 * L - 2; const int k = q % cyc; return k < L ? k : cyc - k; }
    return q % L;
}
uint32_t mix32 (uint32_t a, uint32_t b, uint32_t c) { uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u ^ (c + 0x165667B1u) * 0xC2B2AE3Du; h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; return h; }
}

std::vector<Note> mutantRealize (const MutantCell& c, const Harmony& H, const MutantParams& P, int totalTicks)
{
    std::vector<Note> out, outGrace; if (! c.valid() || totalTicks <= 0) return out;
    const Prof& M = prof (P.mode);
    const int div = clampv (P.div, 4, 64), st = mutantStepTicks (div), V = std::max (1, mutantVariationBars (P));
    const int bars = std::max (1, (totalTicks + BAR - 1) / BAR), periods = (bars + V - 1) / V;
    const int evolvePeriods = P.evolveBars > 0 ? std::max (1, P.evolveBars / V) : 1;
    // the base cell of each period (EVOLVE: a chain of small mutations, wrapping after evolveBars) and its variation bar
    std::vector<MutantCell> base; base.reserve ((size_t) evolvePeriods);
    base.push_back (c);
    for (int k = 1; k < evolvePeriods; ++k) base.push_back (mutantMutate (base.back(), H, P, P.seed ^ (0x5EEDu + (uint32_t) k * 977u), 0.08 + 0.30 * P.evolve, 0));
    const int sixteenth = BAR / 16;
    for (int per = 0; per < periods; ++per)
    {
        const MutantCell& B = base[(size_t) (P.evolveBars > 0 ? per % evolvePeriods : 0)];
        MutantCell Var = mutantMutate (B, H, P, P.seed ^ (0xA5A5u + (uint32_t) per * 7331u), 0.12 + 0.45 * P.evolve, 0);
        if (M.lift > 0)
        {   // the whiplash ending: the variation bar's second half climbs an octave (within the register)
            Rng Rl (mix32 (P.seed ^ 0x11F7u, (uint32_t) per, 3u));
            if (Rl() < M.lift) { const int octHi = M.octLo + clampv ((int) std::lround ((M.octHi - M.octLo) + (P.octaveJump - 0.5) * 2.0), 0, 3); for (int i = Var.len / 2; i < Var.len; ++i) if (! Var.s[(size_t) i].lock) Var.s[(size_t) i].oct = std::min (octHi, Var.s[(size_t) i].oct + 1); }
        }
        for (int bar = 0; bar < V; ++bar)
        {
            const int b = per * V + bar; if (b >= bars) break;
            const bool varBar = bar == V - 1 && (per > 0 || V >= 2 || P.evolve > 0.6);   // the last bar of each period varies (a 1-bar period: from the 2nd bar on)
            const MutantCell& C = varBar ? Var : B;
            for (int sb = 0; sb < div; ++sb)
            {
                const int q = bar * div + sb; const int ci = cellIndexAt (C, P, q); const MutantStep& s = C.s[(size_t) ci];
                if (s.rest) continue;
                if (s.prob < 0.999) { Rng Rp (mix32 (P.seed, (uint32_t) b, (uint32_t) sb)); if (Rp() > s.prob) continue; }
                double t0 = (double) b * BAR + (double) sb * st;
                if (P.swing > 0.001 && div >= 16) { const int six = (int) std::floor (t0 / sixteenth); if (six % 2 == 1) t0 += P.swing * sixteenth * 0.33; }
                else if (P.swing > 0.001 && div == 8 && sb % 2 == 1) t0 += P.swing * st * 0.33;
                if (t0 >= totalTicks) continue;
                const int tick = (int) std::lround (t0);
                int p;
                if (M.learned != nullptr)
                {   // CYBERPOP: a pop hook keeps its pitches over the chord loop (the cell is written over the first chord); CHORD FOLLOW
                    // only nudges a note on a beat onto the current chord when a chord tone sits within a tone of it
                    p = mutantPitch (s, H, 0, P);
                    if (P.chordFollow && ! H.chords.empty() && (sb % std::max (1, div / 4)) == 0 && ! H.chordTone (p, tick))
                    {
                        const auto& chNow = H.at (tick);
                        for (int d = 1; d <= 2; ++d) { bool done = false; for (int sgn : { 1, -1 }) { const int r = p + sgn * d; if (std::find (chNow.pcs.begin(), chNow.pcs.end(), mod12 (r)) != chNow.pcs.end()) { p = r; done = true; break; } } if (done) break; }
                    }
                }
                else p = mutantPitch (s, H, tick, P);
                if ((s.col == 2 || s.col == -2) && ! P.scaleLock)
                {   // a chromatic approach note sits a semitone under / over the next played note (whatever chord that lands on); no next note -> the scale neighbour
                    int target = -1;
                    for (int k = 1; k <= 3 && sb + k < div * V; ++k) { const int cj = cellIndexAt (C, P, q + k); MutantStep nx = C.s[(size_t) cj]; if (nx.rest || nx.prob < 0.999) break; if (nx.col == 2 || nx.col == -2) nx.col = 0; target = mutantPitch (nx, H, tick + k * st, P); break; }
                    if (target > 0 && std::abs (target - p) <= 14) p = clampv (target + (s.col > 0 ? -1 : 1), 24, 108);
                    else { MutantStep t = s; t.col = s.col > 0 ? -1 : 1; p = mutantPitch (t, H, tick, P); }
                }
                Rng Rv (mix32 (P.seed ^ 0xBEEFu, (uint32_t) b, (uint32_t) sb));
                const double jitter = 1.0 + (Rv() - 0.5) * 2.0 * P.velRandom * 0.3;
                const int stepLen = st * std::max (s.sus, s.hold ? 2 : 1);
                const int nSub = s.trill ? std::max (2, std::min (6, stepLen / std::max (6, st / 2))) : s.ratchet == 4 ? (div >= 32 ? 2 : 4) : (s.ratchet == 3 ? 3 : (s.ratchet == 2 ? 2 : 1));
                const double sub = (double) stepLen / nSub;
                if (s.grace && tick >= st / 2)
                {   // a grace note a scale step below (or above) the note, a 1/32 before it, soft
                    const auto& ch = P.chordFollow ? H.at (tick) : (H.chords.empty() ? HChord {} : H.chords.front());
                    Note gn; gn.t = tick - st / 2; gn.d = std::max (3, st / 2 - 1); gn.p = clampv (Rv() < 0.7 ? nearestScale (ch.scale, p, -1) : nearestScale (ch.scale, p, 1), 24, 108);
                    gn.v = (float) clampv (s.vel * jitter * 0.6, 0.15, 1.0); gn.l = Link { 1, 0.6f, 0.5f }; outGrace.push_back (gn);
                }
                for (int k = 0; k < nSub; ++k)
                {
                    Note nt; nt.t = (int) std::lround (t0 + k * sub); if (nt.t >= totalTicks) break;
                    const double g = nSub > 1 ? std::min (s.gate, 0.85) : s.gate;
                    nt.d = std::max (3, (int) std::lround (sub * g)); nt.d = std::min (nt.d, totalTicks - nt.t);
                    nt.p = clampv (p, 24, 108);
                    if (s.trill && (k % 2) == 1) { const auto& ch = P.chordFollow ? H.at (tick) : (H.chords.empty() ? HChord {} : H.chords.front()); nt.p = clampv (nearestScale (ch.scale, p, 1), 24, 108); }   // the trill's upper neighbour
                    double v = s.vel * jitter; if (nSub > 1 && ! s.trill) v *= 0.78 + 0.22 * (k + 1) / nSub;   // a ratchet swells into its last hit
                    nt.v = (float) clampv (v, 0.15, 1.0); nt.acc = s.acc;
                    nt.lock = s.lock;
                    if (s.glide && k == nSub - 1) nt.l = Link { 1, 0.55f, 0.5f };
                    out.push_back (nt);
                }
            }
        }
    }
    out.insert (out.end(), outGrace.begin(), outGrace.end());
    sortNotes (out);
    if (P.scaleLock)   // SCALE LOCK: nothing outside the key / chord (the engine never writes any, the lock makes it a promise)
        for (auto& nt : out) if (! H.chords.empty() && ! H.scaleTone (nt.p, nt.t))
        { const auto& ch = P.chordFollow ? H.at (nt.t) : H.chords.front(); const int up = nearestScale (ch.scale, nt.p, 1), dn = nearestScale (ch.scale, nt.p, -1); nt.p = up - nt.p <= nt.p - dn ? up : dn; }
    // no glide into a note of the same pitch at the same tick, no two notes at the same tick and pitch
    std::vector<Note> clean; clean.reserve (out.size());
    for (auto& nt : out) if (clean.empty() || clean.back().t != nt.t || clean.back().p != nt.p) clean.push_back (nt);
    for (size_t i = 0; i + 1 < clean.size(); ++i) if (clean[i].l.type == 1 && clean[i + 1].t <= clean[i].t) clean[i].l = Link {};
    if (! clean.empty() && clean.back().l.type == 1) clean.back().l = Link {};
    for (size_t i = 0; i < clean.size(); ++i)   // the same pitch never overlaps itself (a MIDI note-off would cut the second one short)
        for (size_t j = i + 1; j < clean.size() && clean[j].t < clean[i].t + clean[i].d; ++j)
            if (clean[j].p == clean[i].p) { clean[i].d = std::max (1, clean[j].t - clean[i].t - 1); break; }
    return clean;
}

int mutantStepAt (const MutantCell& c, const MutantParams& P, int t)
{
    if (! c.valid() || t < 0) return -1;
    const int div = clampv (P.div, 4, 64), st = mutantStepTicks (div), V = std::max (1, mutantVariationBars (P));
    const int bar = t / BAR, per = bar / V, pStart = per * V * BAR;
    const int q = (t - pStart) / st; const int rem = (t - pStart) % st;
    if (rem > st / 2 + 1) return -1;
    return cellIndexAt (c, P, q);
}
void mutantLockFirst (MutantCell& c, int n) { for (int i = 0; i < c.len; ++i) c.s[(size_t) i].lock = i < n; }
int mutantLockedCount (const MutantCell& c) { int k = 0; for (auto& s : c.s) k += s.lock ? 1 : 0; return k; }

std::string mutantPrint (const MutantCell& c)
{
    std::string o = std::to_string (c.len) + ":";
    for (auto& s : c.s)
    {
        o += " ";
        if (s.rest) { o += "."; continue; }
        o += std::to_string (s.idx) + "/" + std::to_string (s.oct);
        if (s.col) o += (s.col > 0 ? "+" : "-") + std::to_string (std::abs (s.col));
        if (s.role == 1) o += "p";
        if (s.role == 2) o += "^";
        if (s.acc) o += ">";
        if (s.hold) o += "_";
        if (s.sus > 1) o += "=" + std::to_string (s.sus);
        if (s.ratchet > 1) o += "r" + std::to_string (s.ratchet);
        if (s.glide) o += "~";
        if (s.grace) o += "g";
        if (s.trill) o += "t";
        if (s.lock) o += "#";
        if (s.prob < 0.999) o += "?";
    }
    return o;
}

MutantStats mutantAnalyse (const std::vector<Note>& notesIn, const Harmony& H, int totalTicks)
{
    MutantStats S; auto notes = notesIn; sortNotes (notes);
    S.notes = (int) notes.size(); S.bars = std::max (1, totalTicks / BAR); S.notesPerBar = (double) S.notes / S.bars;
    if (notes.empty()) return S;
    std::vector<Note> on; for (auto& n : notes) if (on.empty() || on.back().t != n.t) on.push_back (n);   // one note per onset (the top one is enough)
    int leaps = 0, octs = 0, ivs = 0, rev = 0, chord = 0, scale = 0, ratch = 0, acc = 0, lo = 127, hi = 0;
    int lastIv = 0; const int rt = BAR / 16;
    for (size_t i = 0; i < on.size(); ++i)
    {
        const auto& n = on[i];
        lo = std::min (lo, n.p); hi = std::max (hi, n.p);
        if (! H.chords.empty()) { if (H.chordTone (n.p, n.t)) ++chord; if (H.scaleTone (n.p, n.t)) ++scale; }
        if (n.v >= 0.85f) ++acc;
        if (i > 0 && n.t - on[i - 1].t < rt) ++ratch;
        if (i > 0)
        {
            const int iv = n.p - on[i - 1].p; ++ivs;
            if (std::abs (iv) >= 7) ++leaps;
            if (std::abs (iv) == 12 || std::abs (iv) == 24) ++octs;
            if (std::abs (lastIv) >= 7 && iv != 0) { if ((iv > 0) != (lastIv > 0)) ++rev; }
            lastIv = iv;
        }
    }
    int leapsFollowed = 0; lastIv = 0;
    for (size_t i = 1; i < on.size(); ++i) { const int iv = on[i].p - on[i - 1].p; if (std::abs (lastIv) >= 7 && iv != 0) ++leapsFollowed; lastIv = iv; }
    S.leapShare = ivs ? (double) leaps / ivs : 0; S.octShare = ivs ? (double) octs / ivs : 0;
    S.chordShare = (double) chord / on.size(); S.scaleShare = (double) scale / on.size();
    S.reversal = leapsFollowed ? (double) rev / leapsFollowed : 0; S.ratchetShare = (double) ratch / on.size(); S.accentShare = (double) acc / notes.size();
    S.range = hi - lo;
    // a 3-note fragment (two intervals + two gaps) heard twice inside the first 2 bars
    std::set<std::string> seen; const int lim = 2 * BAR;
    for (size_t i = 0; i + 2 < on.size() && on[i + 2].t < lim; ++i)
    {
        const std::string key = std::to_string (on[i + 1].p - on[i].p) + "," + std::to_string (on[i + 2].p - on[i + 1].p) + "," + std::to_string (on[i + 1].t - on[i].t) + "," + std::to_string (on[i + 2].t - on[i + 1].t);
        if (! seen.insert (key).second) { S.repeatFrag = 1; break; }
    }
    return S;
}
// ---------------------------------------------------------------- TWO LINES
MutantCell mutantSecondCell (const MutantCell& a, const Harmony& H, const MutantParams& P)
{
    MutantParams Q = P; Q.seed = P.seed * 7919u + 13u; Q.reg = P.reg + P.lineBReg; Q.cellSteps = a.len; Q.density = clampv (P.density * 0.8, 0.0, 1.0); Q.twoLine = 0;
    MutantCell b = mutantGenerate (H, Q);
    if (! b.valid() || b.len != a.len) return b;
    // the hocket: B rests where A sounds on a strong step, sounds where A rests; never both silent on a bar start
    for (int i = 0; i < a.len; ++i)
    {
        const auto& sa = a.s[(size_t) i]; auto& sb = b.s[(size_t) i];
        if (! sa.rest && (sa.acc || i % 2 == 0)) { if (! sb.lock) sb.rest = true; }
        else if (sa.rest && sb.rest && ! sb.lock) { sb.rest = false; sb.gate = 0.5; sb.vel = 0.7; }
    }
    if (b.s[0].rest && ! b.s[0].lock && a.s[0].rest) b.s[0].rest = false;
    return b;
}

void mutantRealizeTwo (const MutantCell& a, const MutantCell& cellB, const Harmony& H, const MutantParams& P, int totalTicks, std::vector<Note>& outA, std::vector<Note>& outB)
{
    outA.clear(); outB.clear();
    MutantParams PA = P; PA.twoLine = 0;
    outA = mutantRealize (a, H, PA, totalTicks);
    if (P.twoLine == TwoOff || outA.empty()) return;
    const int div = clampv (P.div, 4, 64), st = mutantStepTicks (div), V = std::max (1, mutantVariationBars (P));
    const int shift = 12 * (P.lineBReg == 0 ? -1 : clampv (P.lineBReg, -2, 2));
    if (P.twoLine == TwoShadow)
    {   // a canon: A again, late by shadowSteps, an octave away, softer and shorter
        const int late = clampv (P.shadowSteps, 1, 16) * st;
        int sh = shift; for (auto& n : outA) if (n.p + sh > 108 || n.p + sh < 24) { sh = -shift; break; }   // the whole line moves the same way
        for (auto n : outA) { n.t += late; if (n.t >= totalTicks) continue; n.p = clampv (n.p + sh, 24, 108); n.v = (float) clampv (n.v * 0.78, 0.1, 1.0); n.d = std::max (3, (int) std::lround (n.d * 0.8)); n.lock = false; outB.push_back (n); }
    }
    else if (P.twoLine == TwoAnswer)
    {   // call and response: A keeps the first half of every period, B answers in the second half with A's call an octave away
        // (every other answer mirrors the contour around the call's first note); when the period is one bar, call = beats 1-2
        const int perTicks = V * BAR, half = perTicks / 2;
        std::vector<Note> call; std::vector<Note> keep;
        for (auto& n : outA) { const int ph = n.t % perTicks; if (ph < half) keep.push_back (n); }
        int sh = shift; for (auto& n : keep) if (n.p + sh > 106 || n.p + sh < 26) { sh = -shift; break; }
        for (auto& n : keep)
        {
            const int per = n.t / perTicks, ph = n.t % perTicks; Note b = n; b.t = per * perTicks + half + ph; if (b.t >= totalTicks) continue;
            const bool mirror = per % 2 == 1; int pivot = n.p; for (auto& k : keep) if (k.t / perTicks == per) { pivot = k.p; break; }   // mirrored around the call's first note
            int p = mirror ? pivot - (n.p - pivot) : n.p; p += sh;
            // keep the answer on the chord / scale under it
            if (! H.chords.empty() && ! H.scaleTone (p, b.t)) { const auto& ch = P.chordFollow ? H.at (b.t) : H.chords.front(); const int up = nearestScale (ch.scale, p, 1), dn = nearestScale (ch.scale, p, -1); p = up - p <= p - dn ? up : dn; }
            b.p = clampv (p, 24, 108); b.v = (float) clampv (n.v * 0.9, 0.1, 1.0); b.lock = false; outB.push_back (b);
        }
        outA = keep;
    }
    else   // INTERLOCK: B's own cell, only where A has no onset
    {
        MutantParams PB = P; PB.twoLine = 0; PB.reg = P.reg + P.lineBReg; PB.seed = P.seed * 7919u + 13u;
        const MutantCell& b = cellB.valid() && cellB.len == a.len ? cellB : a;
        auto nb = mutantRealize (b, H, PB, totalTicks);
        std::vector<int> onsA; for (auto& n : outA) if (onsA.empty() || onsA.back() != n.t) onsA.push_back (n.t);
        for (auto n : nb)
        {
            auto hits = [&] (int at) { for (int t : onsA) { if (t > at + st / 2) break; if (std::abs (t - at) < st / 2) return true; } return false; };
            if (hits (n.t)) { if (st >= 12 && ! hits (n.t + st / 2)) { n.t += st / 2; n.d = std::min (n.d, st / 2); } else continue; }   // between A's notes (a 1/32 hocket) or not at all
            n.v = (float) clampv (n.v * 0.85, 0.1, 1.0); n.lock = false; outB.push_back (n);
        }
    }
    sortNotes (outB);
    {   // one note per tick and pitch (a hocket shift can land on another note of the line), and the same pitch never overlaps itself
        std::vector<Note> u; for (auto& n : outB) { if (! u.empty() && u.back().t == n.t && u.back().p == n.p) { if (n.d > u.back().d) u.back() = n; } else u.push_back (n); } outB = u;
        for (size_t i = 0; i < outB.size(); ++i) for (size_t j = i + 1; j < outB.size() && outB[j].t < outB[i].t + outB[i].d; ++j) if (outB[j].p == outB[i].p) { outB[i].d = std::max (1, outB[j].t - outB[i].t - 1); break; }
    }
    for (size_t i = 0; i + 1 < outB.size(); ++i) if (outB[i].l.type == 1 && outB[i + 1].t <= outB[i].t) outB[i].l = Link {};
    if (! outB.empty() && outB.back().l.type == 1) outB.back().l = Link {};
}

MutantDuo mutantDuo (const std::vector<Note>& a, const std::vector<Note>& b, int stepTicks)
{
    MutantDuo d; if (b.empty()) return d;
    std::vector<int> onsA, onsB; for (auto& n : a) if (onsA.empty() || onsA.back() != n.t) onsA.push_back (n.t);
    for (auto& n : b) if (onsB.empty() || onsB.back() != n.t) onsB.push_back (n.t);
    int col = 0; for (int t : onsB) for (int u : onsA) if (std::abs (t - u) < std::max (1, stepTicks / 2)) { ++col; break; }
    d.collide = onsB.empty() ? 0 : (double) col / onsB.size();
    std::vector<int> gaps; int lo = 127, hi = 0; for (auto& n : a) { lo = std::min (lo, n.p); hi = std::max (hi, n.p); }
    int ma = 0, mb = 0; { std::vector<int> pa, pb; for (auto& n : a) pa.push_back (n.p); for (auto& n : b) pb.push_back (n.p); std::sort (pa.begin(), pa.end()); std::sort (pb.begin(), pb.end()); ma = pa.empty() ? 0 : pa[pa.size() / 2]; mb = pb.empty() ? 0 : pb[pb.size() / 2]; }
    d.medianGap = mb - ma;
    d.gapFill = onsA.empty() ? 1.0 : 1.0 - d.collide;
    return d;
}
} // namespace arp
