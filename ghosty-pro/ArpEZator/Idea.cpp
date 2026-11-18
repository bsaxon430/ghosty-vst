#include "Idea.h"
#include "HookLab.h"
#include "UrbanMoods.h"
#include <algorithm>
#include <cmath>

namespace arp
{
std::string modeName (int mode) { return recipe().mode[clampv (mode, 0, kMoods - 1)].label; }
std::string modeWords (int mode) { return recipe().mode[clampv (mode, 0, kMoods - 1)].words; }

static std::string joinToks (const std::vector<Slot>& p) { std::string s; for (size_t i = 0; i < p.size(); ++i) s += (i ? " " : "") + p[i].tok; return s; }

Gen generateForMode (int mode, const std::string& words, uint32_t seed, std::string& promptOut, uint32_t& seedUsed)
{
    const ModeRecipe& M = recipe().mode[clampv (mode, 0, kMoods - 1)];
    std::string prompt = M.words; if (! words.empty()) prompt += " " + words;
    promptOut = prompt;
    const bool userScale = ! words.empty() && parsePrompt (words).scale.has_value();
    // re-roll with the next seeds until the scale suits the mode and (often) a preferred progression shows up, aiming at one
    // progression picked by the recipe's weights. The result is still exactly generate (prompt, seedUsed): the web version
    // gives the same chords for it.
    Rng pick (seed ^ 0xB1A5u);
    const bool insist = ! M.progs.empty() && pick() < M.progBias;
    int target = -1;
    if (insist)
    {
        double t = 0; for (size_t i = 0; i < M.progs.size(); ++i) t += i < M.progW.size() ? M.progW[i] : 1.0;
        double u = pick() * t; for (size_t i = 0; i < M.progs.size(); ++i) { u -= i < M.progW.size() ? M.progW[i] : 1.0; if (u < 0) { target = (int) i; break; } }
    }
    Gen firstOk, firstPref; bool haveOk = false, havePref = false; uint32_t okSeed = seed, prefSeed = seed;
    for (int k = 0; k < 160; ++k)
    {
        const uint32_t s = seed + (uint32_t) k * 7919u;
        Gen g = generate (prompt, s);
        const bool scaleOk = userScale || M.scales.empty() || std::find (M.scales.begin(), M.scales.end(), g.scale) != M.scales.end();
        if (! scaleOk) continue;
        if (! haveOk) { firstOk = g; okSeed = s; haveOk = true; if (! insist) break; }
        const std::string toks = joinToks (g.prog);
        if (target >= 0 && toks == M.progs[(size_t) target]) { seedUsed = s; return g; }
        if (! havePref && std::find (M.progs.begin(), M.progs.end(), toks) != M.progs.end()) { firstPref = g; prefSeed = s; havePref = true; if (k > 60) break; }
    }
    if (havePref) { seedUsed = prefSeed; return firstPref; }
    seedUsed = haveOk ? okSeed : seed;
    return haveOk ? firstOk : generate (prompt, seed);
}

void applyModeDefaults (Idea& I)
{
    const ModeRecipe& M = recipe().mode[clampv (I.mode, 0, kMoods - 1)];
    I.chordBars = M.chordBars; I.voicing = M.voicing; I.chordRhythm = M.chordRhythm; I.chordOct = 0;
    I.phraseBars = M.phraseBars; I.form = M.form; I.stagger = M.staggerTicks; I.stutter = M.stutter;
    I.arpRange = M.arpRange; I.melRange = M.melRange;
    for (int l = 0; l < kLanes; ++l) I.lanes[l].mute = M.mute[l];
}

std::vector<Slot>& chordProg (Idea& I) { return I.simple && ! I.simpleProg.empty() ? I.simpleProg : I.g.prog; }
const std::vector<Slot>& chordProg (const Idea& I) { return I.simple && ! I.simpleProg.empty() ? I.simpleProg : I.g.prog; }
std::string chordScale (const Idea& I) { return I.simple && ! urbanMood (I.mode) ? simpleScale (I.g.scale) : I.g.scale; }
static int laneMoodOf (const Idea& I, int lane) { const int lm = lane >= 0 && lane < 4 ? I.laneMood[(size_t) lane] : -1; return lm >= 0 ? lm : I.mode; }
int chordLoopTicks (const Idea& I) { return progTicks (chordProg (I)) * std::max (1, I.chordBars); }

ProgState progState (const Idea& I)
{
    ProgState ps; ps.key = I.g.key; ps.scale = chordScale (I); ps.colour = I.g.colour; ps.pedal = I.g.pedal; ps.prog = chordProg (I);
    ps.simple = I.simple; ps.mood = I.mode;
    for (auto& s : ps.prog) s.len = (s.len ? s.len : 4) * std::max (1, I.chordBars);
    return ps;
}

void pickSimpleProg (Idea& I, uint32_t seed, bool avoidCurrent)
{
    if (urbanMood (I.mode)) { urbanPickProg (I, seed, avoidCurrent); return; }   // TRAP / HIP HOP: the flavour's own progressions
    const int bars = std::max (4, (int) std::lround ((double) progTicks (I.g.prog) / BAR));
    I.simpleProg = classicProg (I.mode, I.g.scale, seed, bars, avoidCurrent && ! I.simpleProg.empty() ? &I.simpleProg : nullptr);
}

void setSimple (Idea& I, bool simple)
{
    if (I.simple == simple) return;
    I.simple = simple;
    rebuildChords (I); refitLanesToChords (I);
}

static const int SIMPLE_ALTS[] { 0, 5, 6, 7 };
int altCount (const Idea& I, const Slot& s) { return s.fixed ? 3 : I.simple ? 4 : (int) altNames().size(); }
int altAt (const Idea& I, const Slot& s, int k) { return ! s.fixed && I.simple ? SIMPLE_ALTS[mod12 (k) % 4] : k; }
int altIndex (const Idea& I, const Slot& s)
{
    const int n = altCount (I, s);
    if (! s.fixed && I.simple) { const int a = s.alt % (int) altNames().size(); for (int k = 0; k < 4; ++k) if (SIMPLE_ALTS[k] == a) return k; return 0; }
    return s.alt % n;
}
void nextAlt (Idea& I, int slot)
{
    auto& pr = chordProg (I); if (slot < 0 || slot >= (int) pr.size()) return;
    auto& s = pr[(size_t) slot];
    s.alt = altAt (I, s, (altIndex (I, s) + 1) % altCount (I, s));
    rebuildChords (I); refitLanesToChords (I);
}

static uint64_t chordSigOf (const Idea& I)
{   // the chord lane as generated: every chord's timing + voicing, the rhythm and the loop (FNV-1a)
    uint64_t h = 1469598103934665603ull; auto mix = [&] (int v) { h = (h ^ (uint64_t) (uint32_t) v) * 1099511628211ull; };
    mix (I.chordRhythm); mix (chordLoopTicks (I));
    for (auto& c : I.H.chords) { mix (c.t); mix (c.tEnd); mix ((int) c.voicing.size()); for (int v : c.voicing) mix (v); }
    return h | 1;
}
static bool barLockedAt (const std::vector<bool>& bl, int t) { const int b = t / BAR; return t >= 0 && b < (int) bl.size() && bl[(size_t) b]; }
static std::vector<MidiNote> generatedChordNotes (const Idea& I);
void applyChordOct (Idea& I)
{   // chordOct as far as it fits; the harmony's voicings follow (the stored edits stay at octave 0)
    const int u = chordOctFit (I, I.chordOct), d = u - I.chordOctUsed;
    if (d != 0) for (auto& c : I.H.chords) for (auto& v : c.voicing) v += 12 * d;
    I.chordOctUsed = u;
}
static void rewriteChordEdits (Idea& I)
{   // the chords were rebuilt: as MELODY's locks, locked notes + the notes in locked bars stay, the new chords fill the rest
    auto& L = I.lanes[LaneChords];
    std::vector<Note> keep; for (auto& n : L.notes) if ((n.lock || barLockedAt (L.barLocks, n.t)) && n.t < I.total) keep.push_back (n);
    ++I.chordRewrites;
    if (keep.empty()) { L.notes.clear(); L.edited = false; I.chordRewriteKind = 1; return; }
    std::vector<Note> out = keep; const int sh = 12 * I.chordOctUsed;
    for (auto& m : generatedChordNotes (I))
    {
        if (barLockedAt (L.barLocks, m.t)) continue;
        Note n; n.t = m.t; n.d = m.d; n.p = m.p - sh; n.v = (float) m.v;
        bool clash = false; for (auto& k : keep) clash |= k.p == n.p && k.t < n.t + n.d && n.t < k.t + k.d;
        if (! clash) out.push_back (n);
    }
    sortNotes (out); L.notes = out; L.edited = true; I.chordRewriteKind = 2;
}
static void fitChordEditsToLength (Idea& I, int oldTotal)
{   // the idea got longer / shorter (EVOLVE, MAKE IT A SONG, phrase): the edited chords repeat / are cut, as they were
    auto& ns = I.lanes[LaneChords].notes; std::vector<Note> out;
    for (auto& n : ns)
        for (int base = 0; n.t + base < I.total; base += oldTotal) { Note m = n; m.t += base; m.d = std::max (1, std::min (m.d, I.total - m.t)); out.push_back (m); if (n.t >= oldTotal) break; }
    sortNotes (out); ns = out;
}

void rebuildChords (Idea& I)
{
    const int oldTotal = I.total;
    std::vector<Note> prevGen;   // locked CHORDS bars on generated chords: what they were (kept through the rebuild)
    if (! chordsEdited (I) && I.chordSig != 0 && std::find (I.lanes[LaneChords].barLocks.begin(), I.lanes[LaneChords].barLocks.end(), true) != I.lanes[LaneChords].barLocks.end())
    { prevGen = chordDisplayNotes (I); for (auto& n : prevGen) n.p -= 12 * I.chordOctUsed; }
    const ProgState ps = progState (I);
    I.chords = resolveProg (ps);
    I.H = makeHarmony (I.g.key, ps.scale, I.chords, I.voicing);
    if (urbanMood (I.mode) && I.voicing == 0) urbanVoicings (I);   // TRAP / HIP HOP voicings (SMOOTH only: POWER / OPEN / WEB stay as chosen)
    const uint64_t sig = chordSigOf (I);
    if (I.keyShift != 0) shiftHarmony (I.H, I.chords, I.keyShift);   // a BAKEd transpose: the same voicings, moved exactly
    I.chordOctUsed = 0; applyChordOct (I);   // CHORDS OCT: only the chord lane's voicings move (bass notes, roots, names stay)
    const int loop = std::max (BAR, chordLoopTicks (I));
    int total = loop;
    if (I.phraseBars > 0) total = std::max (total, I.phraseBars * BAR);
    if (I.evolve) total = std::max (total, I.evolveBars * BAR);
    if (I.songBars > 0) total = std::max (total, I.songBars * BAR);
    total = ((total + loop - 1) / loop) * loop;   // every lane loops inside the whole length
    I.total = total;
    for (auto& L : I.lanes) L.barLocks.resize ((size_t) (total / BAR), false);
    const bool changed = I.chordSig != 0 && sig != I.chordSig;
    if (changed && ! prevGen.empty()) { I.lanes[LaneChords].notes = prevGen; I.lanes[LaneChords].edited = true; }
    if (chordsEdited (I))
    {
        if (changed) rewriteChordEdits (I);
        else if (oldTotal > 0 && oldTotal != I.total) fitChordEditsToLength (I, oldTotal);
        applyChordOct (I);
    }
    I.chordSig = sig;
}

int chordOctFit (const Idea& I, int want)
{   // the range the lane covers at octave 0: the edited notes when there are some, else the voicings
    want = clampv (want, -2, 2);
    int lo = 999, hi = -999;
    if (chordsEdited (I)) for (auto& n : I.lanes[LaneChords].notes) { lo = std::min (lo, n.p); hi = std::max (hi, n.p); }
    else for (auto& c : I.H.chords) for (int v : c.voicing) { lo = std::min (lo, v - 12 * I.chordOctUsed); hi = std::max (hi, v - 12 * I.chordOctUsed); }
    if (lo > hi) return want;
    for (int k = want; k != 0; k += want > 0 ? -1 : 1) if (lo + 12 * k >= 24 && hi + 12 * k <= 108) return k;
    return 0;
}
bool setChordOct (Idea& I, int want)
{
    I.chordOct = chordOctFit (I, clampv (want, -2, 2));
    applyChordOct (I);
    return I.chordOct == want;
}
bool chordsEdited (const Idea& I) { const auto& L = I.lanes[LaneChords]; return L.edited && ! L.notes.empty(); }
int foldChordPitch (int p) { while (p > 108) p -= 12; while (p < 24) p += 12; return p; }
std::vector<Note> chordDisplayNotes (const Idea& I)
{
    std::vector<Note> out;
    if (chordsEdited (I)) { out = I.lanes[LaneChords].notes; for (auto& n : out) n.p += 12 * I.chordOctUsed; return out; }
    for (auto& m : generatedChordNotes (I)) { Note n; n.t = m.t; n.d = m.d; n.p = m.p; n.v = (float) m.v; out.push_back (n); }
    return out;
}
void materializeChords (Idea& I)
{
    if (chordsEdited (I)) return;
    auto& L = I.lanes[LaneChords]; L.notes = chordDisplayNotes (I);
    for (auto& n : L.notes) n.p -= 12 * I.chordOctUsed;
    L.edited = ! L.notes.empty();
}

std::string transposeChordName (const std::string& name, int semis, int newKey, const std::string& scale)
{
    auto pcOf = [] (const std::string& s, size_t& len) {
        static const int base[] { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G
        if (s.empty() || s[0] < 'A' || s[0] > 'G') { len = 0; return -1; }
        int pc = base[s[0] - 'A']; len = 1;
        while (len < s.size() && (s[len] == '#' || s[len] == 'b')) { pc += s[len] == '#' ? 1 : -1; ++len; }
        return mod12 (pc); };
    std::string head = name, bass; const auto slash = name.find ('/');
    if (slash != std::string::npos) { head = name.substr (0, slash); bass = name.substr (slash + 1); }
    size_t n = 0; const int r = pcOf (head, n); if (r < 0) return name;
    std::string out = noteName (r + semis, newKey, scale) + head.substr (n);
    if (! bass.empty()) { size_t m = 0; const int b = pcOf (bass, m); out += "/" + (b < 0 ? bass : noteName (b + semis, newKey, scale) + bass.substr (m)); }
    return out;
}
void shiftHarmony (Harmony& H, std::vector<Resolved>& chords, int s)
{
    if (s == 0) return;
    H.key = mod12 (H.key + s); for (auto& p : H.keyPcs) p = mod12 (p + s);
    for (auto& c : H.chords)
    {
        c.root = mod12 (c.root + s); for (auto& p : c.pcs) p = mod12 (p + s); for (auto& p : c.scale) p = mod12 (p + s);
        for (auto& v : c.voicing) v += s; c.bassNote += s;
        c.name = transposeChordName (c.name, s, H.key, H.scaleName);
    }
    for (auto& c : chords) { for (auto& p : c.pcs) p = mod12 (p + s); for (auto& v : c.voicing) v += s; c.bassNote += s; c.name = transposeChordName (c.name, s, H.key, H.scaleName); }
}
bool foldsAt (int p, int lane) { return lane == LaneBass ? p < 24 : p > 96 || p < 12; }
int foldPitch (int p, int lane)
{
    if (lane == LaneBass) { while (p < 24) p += 12; while (p > 72) p -= 12; }
    else { while (p > 96) p -= 12; while (p < 12) p += 12; }
    return clampv (p, 0, 127);
}
void bakeTranspose (Idea& I, int s, int mask)
{
    if (s == 0) return;
    for (int l : { LaneArp, LaneMelody, LaneBass })
        if (mask & (1 << l)) for (auto& n : I.lanes[l].notes) n.p = foldPitch (n.p + s, l);
    if (mask & (1 << LaneMelody)) for (auto& n : I.motif) n.p = foldPitch (n.p + s, LaneMelody);
    if (mask & (1 << LaneChords))
    {
        if (chordsEdited (I)) for (auto& n : I.lanes[LaneChords].notes) n.p = foldChordPitch (n.p + s + 12 * I.chordOctUsed) - 12 * I.chordOctUsed;   // EDITED CHORDS move exactly too
        I.keyShift += s; rebuildChords (I);
    }
}
int keyShiftTo (int fromKey, int toRoot) { int d = mod12 (toRoot - fromKey); if (d > 6) d -= 12; return d; }

int melodyPhraseTicks (const Idea& I)
{
    return std::max (BAR, std::min (I.total, I.phraseBars > 0 ? I.phraseBars * BAR : std::max (BAR, I.H.loopTicks)));
}

ArpParams arpParams (const Idea& I)
{
    ArpParams P; P.seed = I.g.arpSeed; P.mood = I.mode; P.spb = I.arpSpb; P.range = I.arpRange; P.reg = I.reg; P.gate = I.arpGate;
    P.cycle = I.cycle; P.resync = I.resync; P.holdPedal = I.holdPedal; P.evolveBars = I.evolve ? I.evolveBars : 0; P.master = I.master;
    return P;
}
MelodyParams melodyParams (const Idea& I)
{
    MelodyParams P; P.seed = I.g.arpSeed ^ 0x9E3779B9u; P.mood = I.mode; P.spb = I.melSpb; P.range = I.melRange; P.reg = I.reg; P.master = I.master;
    P.gate = 0.6; P.stutter = I.stutter; P.stagger = I.stagger; P.form = I.form; P.phraseBars = I.phraseBars;
    return P;
}
EditCtx editCtx (const Idea& I, int lane)
{
    EditCtx c; c.H = &I.H; c.totalTicks = I.total; c.spb = lane == LaneArp ? I.arpSpb : lane == LaneMelody ? I.melSpb : 8; c.mood = I.mode;
    c.barLocks = I.lanes[lane].barLocks; c.pedalMode = lane == LaneArp;
    return c;
}

void composeLaneBody (Idea& I, int lane, bool keepLocks);
void composeLane (Idea& I, int lane, bool keepLocks)
{   // LANE MOOD: the lane composes in its own mood / variation; everything else of the idea stays (restored right after)
    const int lm = lane >= 0 && lane < 4 ? I.laneMood[(size_t) lane] : -1, lv = lane >= 0 && lane < 4 ? I.laneVar[(size_t) lane] : 0;
    if ((lm < 0 || lm == I.mode) && lv == 0) { composeLaneBody (I, lane, keepLocks); return; }
    const int mode0 = I.mode; const uint32_t seed0 = I.g.arpSeed;
    const int fl0 = I.flavourUsed; const std::string col0 = I.colour; const bool dt0 = I.doubleTime;
    if (lm >= 0) I.mode = clampv (lm, 0, kMoods - 1);
    if (lv != 0) I.g.arpSeed = seed0 ^ (0x9E3779B9u * (uint32_t) lv + (uint32_t) lane * 0x85EBCA6Bu);
    if (urbanMood (I.mode) && I.mode != mode0) { I.flavourUsed = -1; I.colour.clear(); I.doubleTime = false; }   // a borrowed TRAP / HIP HOP lane: a seeded flavour
    composeLaneBody (I, lane, keepLocks);
    I.mode = mode0; I.g.arpSeed = seed0; I.flavourUsed = fl0; I.colour = col0; I.doubleTime = dt0;
}
void composeLaneBody (Idea& I, int lane, bool keepLocks)
{
    LaneState& L = I.lanes[lane];
    const auto old = L.notes;
    std::vector<Note> fresh;
    if (urbanMood (I.mode) && (lane == LaneArp || lane == LaneBass))
    {   // TRAP / HIP HOP: the arp answers in the melody's rests, the bass plays the mood's 808 / sub archetypes
        if (lane == LaneArp) fresh = urbanArp (I, I.mode, I.g.arpSeed, I.lanes[LaneMelody].notes);
        else fresh = urbanBass (I, I.mode, I.g.arpSeed ^ 0xBA55B055u, &I.urbanBassArch);
    }
    else if (lane == LaneArp)
    {
        const auto r = composeArp (I.H, arpParams (I), I.total);
        I.fig = r.fig; I.figEvents = r.events; I.pedalPc = r.pedalPc; fresh = r.notes;
        if (I.arpDir || I.arpContour || I.arpAccent) fresh = shapeArp (fresh, I.H, I.arpDir, I.arpContour, I.arpAccent, I.fig.len, std::max (BAR, I.H.loopTicks), I.g.arpSeed);
        if (I.arpEuclid > 0 || I.arpMeter > 0) fresh = shapeArpIdm (I, fresh);
        fresh = evolveArp (I, fresh);
    }
    else if (lane == LaneMelody)
    {
        const auto MP = melodyParams (I);
        const int P = melodyPhraseTicks (I);
        int style = I.style;
        if (urbanMood (I.mode))
        {   // TRAP / HIP HOP: a style of the mood (DARK, TRIUMPHANT, HYPNO LOOP / BOOM BAP, SOULFUL, LAID BACK) writes the melody in that flavour
            const int fl = styleFlavour (style, I.mode), fl0 = I.flavourUsed;
            if (fl >= 0) { I.flavourUsed = fl; style = StyleAuto; }
            fresh = urbanMelody (I, I.mode, MP.seed, &I.urbanMelArch);
            I.flavourUsed = fl0;
        }
        else fresh = composeMelody (I.H, MP, I.total, I.evolve && I.evolveMode == EvolveClassic);
        if (style > StyleAuto || I.candy > CandyOff || I.tension >= 0) fresh = shapeLine (I, fresh, I.total, style, I.candy, I.tension, MP.seed, RegMid, I.reg);
        if (I.evolve && I.evolveMode > EvolveClassic)
        {
            MotifSpan keep; if (! I.motif.empty()) keep = { I.motifT0, I.motifT0 + I.motifLen };
            fresh = evolveLine (fresh, I.H, P, I.total, I.evolveMode, keep, MP.seed ^ 0xE70u, I.melSpb, I.mode);
        }
        if (! I.motif.empty()) fresh = placeMotif (fresh, I.motif, I.motifT0, I.motifLen, P, I.total);
    }
    else if (lane == LaneBass) { BassParams B; B.rate = I.bassRate; B.seed = I.laneVar[(size_t) LaneBass] != 0 ? (I.g.arpSeed ^ 0xBA55B055u) | 1u : 0u; fresh = composeBass (I.H, B, I.total); }   // (composeLane varied arpSeed for the lane's take)
    else return;
    L.notes = keepLocks ? mergeLocked (fresh, old, editCtx (I, lane)) : fresh;
    L.edited = false;
}

void newIdea (Idea& I)
{
    I.g = generateForMode (I.mode, I.words, I.seed, I.prompt, I.seedUsed);
    if (! urbanMood (I.mode)) pickSimpleProg (I, I.seedUsed ^ 0x5EED5u);   // the SIMPLE loop (kept even in RICH, so the switch flips both ways)
    const Parsed f = parsePrompt (I.prompt);
    I.master = I.g.master; I.groove = 0;
    if (urbanMood (I.mode)) urbanSetup (I);   // TRAP / HIP HOP: flavour, scale, key, tempo, swing, progression
    {   // the phrase form: the recipe's winners' weights (seeded), else the mode default
        const auto& M = recipe().mode[clampv (I.mode, 0, kMoods - 1)];
        if (! M.formW.empty()) { Rng R (I.seedUsed ^ 0xF0F0u); double t = 0; for (auto& fw : M.formW) t += fw.second; double u = R() * t; for (auto& fw : M.formW) { u -= fw.second; if (u < 0) { I.form = fw.first; break; } } }
    }
    I.arpSpb = 16;
    if (f.rate || ! recipe().mode[clampv (I.mode, 0, kMoods - 1)].forceArp16) I.arpSpb = I.g.rate == "1/8" ? 8 : I.g.rate == "1/32" ? 32 : I.g.rate == "1/16T" ? 24 : 16;
    if (chordsEdited (I)) { ++I.chordRewrites; I.chordRewriteKind = 1; }   // (a new idea: edited chords go, as every lane's edits)
    for (auto& L : I.lanes) { L.notes.clear(); std::fill (L.barLocks.begin(), L.barLocks.end(), false); L.edited = false; }
    I.songBars = 0; I.keyShift = 0;
    rebuildChords (I);
    if (urbanMood (I.mode)) { for (int l : { LaneMelody, LaneArp, LaneBass }) composeLane (I, l, false); return; }   // (the arp fills the melody's rests)
    for (int l = LaneArp; l < kLanes; ++l) composeLane (I, l, false);
}

// ---------------------------------------------------------------- PUSH FURTHER
static std::vector<int> pcToks (const std::vector<Note>& n, int key) { std::vector<int> t; for (auto& x : n) if (x.t < 4 * BAR) t.push_back (mod12 (x.p - key)); return t; }
static std::vector<int> rhToks (const std::vector<Note>& n) { std::vector<int> t; for (auto& x : n) if (x.t < 4 * BAR) t.push_back (((x.t % BAR) / 24) | (clampv (x.d / 24, 0, 31) << 4)); return t; }
static double editDist (const std::vector<int>& a, const std::vector<int>& b)
{
    const size_t n = a.size(), m = b.size(); if (n == 0 && m == 0) return 0.0;
    std::vector<size_t> prev (m + 1), cur (m + 1); for (size_t j = 0; j <= m; ++j) prev[j] = j;
    for (size_t i = 1; i <= n; ++i) { cur[0] = i; for (size_t j = 1; j <= m; ++j) cur[j] = std::min ({ prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] == b[j - 1] ? 0u : 1u) }); std::swap (prev, cur); }
    return (double) prev[m] / (double) std::max (n, m);
}
double melodyDistance (const std::vector<Note>& a, int keyA, const std::vector<Note>& b, int keyB) { return 0.5 * (editDist (pcToks (a, keyA), pcToks (b, keyB)) + editDist (rhToks (a), rhToks (b))); }
RecentIdea recentOf (const Idea& I)
{
    RecentIdea r; r.melody = I.lanes[LaneMelody].notes; r.key = I.H.key; r.scale = I.g.scale + "/" + I.colour; r.bassArch = I.urbanBassArch; r.melArch = I.urbanMelArch;
    if (urbanMood (I.mode))
    {
        r.flavour = I.flavourUsed; r.bpm = I.g.bpm; r.groove = I.groove; r.prog = I.urbanProg;
        for (auto& n : I.lanes[LaneBass].notes) if (n.t < 8 * BAR && n.l.type == 1) ++r.slides;
        r.lead = isEmo (I) ? emoLead (I) : 0; r.arpArch = isEmo (I) ? emoArchFor (I) : -1;
        r.firstChord = I.H.chords.empty() ? -1 : I.H.chords.front().root;
    }
    return r;
}
int newIdeaFurther (Idea& I, const std::vector<RecentIdea>& recent, double threshold, int maxTries)
{
    const uint32_t seed0 = I.seed; const Idea start = I;
    const bool keyTyped = ! I.words.empty() && parsePrompt (I.words).key.has_value();
    // TRAP AUTO: rotate the styles (DARK / DRILL / EMO by the seed): never the last idea's style, >= 4 of the 8 rotation settings
    // different from the last idea and >= 3 from the one before (a candidate that breaks it only wins when none keeps it)
    const bool rotate = start.mode == Trap && start.flavour < 0 && ! recent.empty() && recent.back().flavour >= 0;
    Idea best; double bestScore = -1; int used = 0;
    for (int k = 0; k < (rotate ? 64 : std::max (1, maxTries)); ++k)
    {
        if (rotate && k >= std::max (1, maxTries) && bestScore >= 0) break;   // (past the usual tries only while no candidate rotates)
        Idea X = start; X.seed = seed0 + (uint32_t) k * 7919u; newIdea (X); ++used;
        double score = 1.0;
        for (auto& r : recent) score = std::min (score, melodyDistance (X.lanes[LaneMelody].notes, X.H.key, r.melody, r.key));
        if (! recent.empty())
        {
            const RecentIdea& last = recent.back(); const RecentIdea me = recentOf (X);
            if (urbanMood (X.mode) && me.scale == last.scale && me.bassArch == last.bassArch && me.melArch == last.melArch) score *= 0.5;   // never the same scale + 808 + melody archetype twice
            if (urbanMood (X.mode) && ! keyTyped && me.key == last.key) score *= 0.8;                                                      // move the key between ideas
            if (isEmo (X) && last.flavour == kEmo && (me.prog == last.prog || me.firstChord == last.firstChord || me.arpArch == last.arpArch)) score *= 0.5;   // EMO after EMO: another loop, first chord, guitar figure
            if (rotate)
            {
                const bool ok = me.flavour != last.flavour && rotationDistance (me, last) >= 4 && (recent.size() < 2 || rotationDistance (me, recent[recent.size() - 2]) >= 3);
                if (! ok) score -= me.flavour != last.flavour ? 2.0 : 4.0;
            }
        }
        if (score > bestScore || used == 1) { bestScore = score; best = X; }
        if (score >= threshold) break;
    }
    I = best;
    return used;
}

void rerealizeArp (Idea& I)
{
    if (urbanMood (laneMoodOf (I, LaneArp))) { composeLane (I, LaneArp, true); return; }   // TRAP / HIP HOP arps are not figures
    auto& L = I.lanes[LaneArp];
    auto fresh = realizeArp (I.fig, I.H, arpParams (I), I.total, I.pedalPc, &I.figEvents);
    if (I.arpDir || I.arpContour || I.arpAccent) fresh = shapeArp (fresh, I.H, I.arpDir, I.arpContour, I.arpAccent, I.fig.len, std::max (BAR, I.H.loopTicks), I.g.arpSeed);
    if (I.arpEuclid > 0 || I.arpMeter > 0) fresh = shapeArpIdm (I, fresh);
    fresh = evolveArp (I, fresh);
    L.notes = mergeLocked (fresh, L.notes, editCtx (I, LaneArp));
    L.edited = false;
}

bool laneHardOk (const Idea& I, int lane)
{
    const auto& ns = I.lanes[lane].notes;
    if (urbanMood (laneMoodOf (I, lane))) return true;   // (their own engines keep their own rules)
    if (lane == LaneArp) return arpHardRules (ns, I.fig.len, I.total);
    if (lane != LaneMelody || ns.empty()) return true;
    Harmony Hs = I.H; Hs.offset = I.stagger;
    std::vector<Note> ph = ns; if (I.stagger) { for (auto& x : ph) x.t = ((x.t - I.stagger) % I.total + I.total) % I.total; sortNotes (ph); }
    return melodyHardRules (ph, Hs, I.total, I.mode, false, 0, 0);
}

void refitLanesToChords (Idea& I)
{
    for (int l = LaneArp; l <= LaneMelody; ++l)
    {
        auto& L = I.lanes[l];
        if (l == LaneArp && ! L.edited) rerealizeArp (I);
        else L.notes = refitToHarmony (L.notes, editCtx (I, l));
        // SIMPLE: a lane the user hasn't edited that breaks a hard rule on the new chords is written again (locks kept)
        if (I.simple && ! L.edited && ! L.notes.empty() && (l == LaneArp || I.mode != Midnight) && ! laneHardOk (I, l)) composeLane (I, l, true);
    }
    if (urbanMood (laneMoodOf (I, LaneBass))) { if (! I.lanes[LaneBass].edited) composeLane (I, LaneBass, true); else I.lanes[LaneBass].notes = refitToHarmony (I.lanes[LaneBass].notes, editCtx (I, LaneBass)); return; }
    if (I.laneVar[(size_t) LaneBass] != 0) { composeLane (I, LaneBass, true); return; }   // a NEW take keeps its shape on the new chords
    BassParams B; B.rate = I.bassRate; I.lanes[LaneBass].notes = composeBass (I.H, B, I.total);
}

std::vector<MidiNote> chordLaneNotes (const Idea& I)
{
    if (chordsEdited (I))
    {   // EDITED CHORDS: the notes as edited (+ the octave)
        std::vector<MidiNote> out; const int sh = 12 * I.chordOctUsed;
        for (auto& n : I.lanes[LaneChords].notes) out.push_back ({ n.t, std::max (1, n.d), clampv (n.p + sh, 0, 127), (double) n.v });
        std::stable_sort (out.begin(), out.end(), [] (const MidiNote& a, const MidiNote& b) { return a.t < b.t; });
        return out;
    }
    return generatedChordNotes (I);
}
static std::vector<MidiNote> generatedChordNotes (const Idea& I)
{
    std::vector<MidiNote> out;
    const int loop = std::max (BAR, chordLoopTicks (I));
    for (int base = 0; base < I.total; base += loop)
        for (auto& c : I.H.chords)
        {
            const int t0 = base + c.t, t1 = base + c.tEnd;
            const bool loopTop = &c == &I.H.chords.front();
            auto add = [&] (int t, int d, double v) {   // a little shape: the top voice sings, the bar's downbeat and the loop's first chord lean in
                const int pos = ((t % BAR) + BAR) % BAR; const double acc = pos == 0 ? 0.06 : pos % PPQ == 0 ? 0.02 : -0.02;
                for (size_t k = 0; k < c.voicing.size(); ++k)
                {
                    const double voice = k + 1 == c.voicing.size() ? 0.05 : k == 0 ? -0.03 : 0.0;
                    out.push_back ({ t, d, c.voicing[k], clampv (v + acc + voice + (loopTop && pos == 0 && t == t0 ? 0.04 : 0.0), 0.2, 1.0) });
                }
            };
            if (I.chordRhythm == 0) add (t0, std::max (1, t1 - t0 - 2), 0.62);
            else if (I.chordRhythm == 1) { for (int t = t0 + PPQ / 2; t < t1; t += PPQ) add (t, PPQ / 2 - 12, 0.7); }   // offbeat 8th stabs
            else
            {   // syncopated house stabs (16ths: x..x..x...x..x..)
                static const int pat[] { 0, 3, 6, 10, 13 };
                for (int b = t0; b < t1; b += BAR) for (int s : pat) { const int t = b + s * (BAR / 16); if (t < t1) add (t, BAR / 16 * 2 - 6, s == 0 ? 0.74 : 0.66); }
            }
        }
    std::stable_sort (out.begin(), out.end(), [] (const MidiNote& a, const MidiNote& b) { return a.t < b.t; });
    return out;
}

void trimOverlaps (std::vector<MidiTrack>& tracks)
{   // one channel: a pitch never overlaps itself (across all tracks); the earlier note ends 1 tick before the next
    struct Ref { size_t tr, i; };
    std::vector<std::vector<Ref>> byPitch (128);
    for (size_t k = 0; k < tracks.size(); ++k) for (size_t i = 0; i < tracks[k].notes.size(); ++i) { const int p = tracks[k].notes[i].p; if (p >= 0 && p < 128) byPitch[(size_t) p].push_back ({ k, i }); }
    std::vector<std::vector<bool>> drop (tracks.size()); for (size_t k = 0; k < tracks.size(); ++k) drop[k].assign (tracks[k].notes.size(), false);
    for (auto& v : byPitch)
    {
        std::stable_sort (v.begin(), v.end(), [&] (const Ref& a, const Ref& b) { return tracks[a.tr].notes[a.i].t < tracks[b.tr].notes[b.i].t; });
        for (size_t j = 0; j + 1 < v.size(); ++j)
        {
            auto& a = tracks[v[j].tr].notes[v[j].i]; const auto& b = tracks[v[j + 1].tr].notes[v[j + 1].i];
            if (b.t <= a.t) { drop[v[j].tr][v[j].i] = true; continue; }   // the same pitch twice on one tick: keep one
            if (a.t + std::max (1, a.d) > b.t - 1) a.d = std::max (1, b.t - 1 - a.t);
        }
    }
    for (size_t k = 0; k < tracks.size(); ++k) { std::vector<MidiNote> o; for (size_t i = 0; i < tracks[k].notes.size(); ++i) if (! drop[k][i]) o.push_back (tracks[k].notes[i]); tracks[k].notes = o; }
}

std::vector<uint8_t> ideaMidi (const Idea& I, int laneMask, bool oneTrack, double bpm, const MidiExport& X, int secStart, int secEnd)
{
    const bool sec = secStart >= 0 && secEnd > secStart;
    auto cut = [&] (MidiTrack& t) {   // DRAG SECTION: only [secStart, secEnd), shifted to 0 (a note crossing the end stops there)
        if (! sec) return;
        std::vector<MidiNote> o; for (auto n : t.notes) if (n.t >= secStart && n.t < secEnd) { n.d = std::min (n.d, secEnd - n.t); n.t -= secStart; o.push_back (n); }
        std::vector<MidiCtl> c; for (auto x : t.ctl) if (x.t >= secStart && x.t < secEnd) { x.t -= secStart; c.push_back (x); }
        t.notes = o; t.ctl = c; };
    std::vector<MidiTrack> tr;
    static const char* names[] { "Chords", "Arp", "Melody", "Bass" };
    for (int l = 0; l < kLanes; ++l)
    {
        if (! (laneMask & (1 << l))) continue;
        MidiTrack t; t.name = names[l]; t.ch = oneTrack ? 0 : l;
        if (l == LaneChords) t.notes = chordLaneNotes (I);
        else
        {
            const int spb = l == LaneArp ? I.arpSpb : l == LaneMelody ? I.melSpb : 8;
            const auto ns = applyGroove (I.lanes[l].notes, I.groove, spb, I.seedUsed + (uint32_t) l);
            t.notes = laneMidiNotes (ns, I.total);
            if (! oneTrack) t.ctl = laneMidiCtl (ns, I.total, X.glideBend && l != LaneArp, X.bendRange);   // bends + CC11 only where a track is the lane's own
            if (! oneTrack && l == LaneArp) { const auto e = arpEnergyCtl (I); t.ctl.insert (t.ctl.end(), e.begin(), e.end()); std::stable_sort (t.ctl.begin(), t.ctl.end(), [] (const MidiCtl& a, const MidiCtl& b) { return a.t < b.t; }); }   // arp EVOLVE: CC74 + CC11
        }
        cut (t);
        const float g = X.laneGain[l];
        if (g < 0.999f || g > 1.001f) { std::vector<MidiNote> keep; for (auto& n : t.notes) if (g > 0.002f) { n.v = clampv (n.v * std::min (1.0f, g), 1.0 / 127.0, 1.0); keep.push_back (n); } t.notes = keep; }
        if (oneTrack && ! tr.empty()) tr[0].notes.insert (tr[0].notes.end(), t.notes.begin(), t.notes.end());
        else tr.push_back (t);
    }
    if (oneTrack && ! tr.empty()) { tr[0].name = "ArpEZator"; std::stable_sort (tr[0].notes.begin(), tr[0].notes.end(), [] (const MidiNote& a, const MidiNote& b) { return a.t < b.t; }); }
    for (auto& t : tr) t.ch = 0;   // every note on MIDI channel 1 (lanes stay separate tracks / files)
    trimOverlaps (tr);
    return writeMidi (bpm, tr);
}
} // namespace arp
