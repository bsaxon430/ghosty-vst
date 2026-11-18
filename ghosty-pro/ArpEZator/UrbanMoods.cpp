#include "UrbanMoods.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <functional>

// TRAP + HIP HOP engines. Grids: 16 steps per bar (1/16; double-time hip hop: 1/8). Melody / bass grid symbols:
//   X attack   - held   . rest   r roll (the step re-struck as two 1/32s)   / or \ held + slide into the next attack
// Bass pitch tokens are relative to the chord root under the attack: 1 b2 2 b3 3 4 #4 5 b6 6 b7 7 8, '+' = up an octave,
// '-' = down an octave (defaults sit where a bass player reaches them: b6 / 6 / b7 / 7 below the root, the rest above).
namespace arp
{
namespace
{
int irand (Rng& R, int n) { return n <= 0 ? 0 : std::min (n - 1, (int) std::floor (R() * n)); }
template <typename T> const T& pickV (Rng& R, const std::vector<T>& v) { return v[(size_t) irand (R, (int) v.size())]; }
int pickW (Rng& R, const std::vector<double>& w)
{
    double t = 0; for (double x : w) t += std::max (0.0, x);
    double u = R() * t; for (size_t i = 0; i < w.size(); ++i) { u -= std::max (0.0, w[i]); if (u < 0) return (int) i; }
    return 0;
}
bool has (const std::vector<int>& v, int x) { return std::find (v.begin(), v.end(), x) != v.end(); }
uint32_t mix (uint32_t a, uint32_t b) { uint32_t h = a ^ (b + 0x9E3779B9u + (a << 6) + (a >> 2)); h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16; return h; }

// ---------------------------------------------------------------- data
struct BassArch { const char* name; const char* grid; const char* pitches; bool slow = false; };
// TRAP 808 (14): kick-locked hits, 'and of 2 / and of 4' pushes, long sustains vs stabs, octave drops, slides between roots, rolls
const std::vector<BassArch>& trapBass()
{
    static const std::vector<BassArch> B {
        { "anchor",            "X-----------X-..", "1 1" },
        { "and-of-2 push",     "X---..X-----X-..", "1 1 b7" },
        { "post-snare answer", "X-----....X---X.", "1 1 b3" },
        { "octave slide up",   "X--/X-..X-----X.", "1 8 1 5" },
        { "bar-end drop",      "X-----X-----X-\\X", "1 5 8 1" },
        { "stab bounce",       "X..X..X...X.X...", "1 1 b7 1 5" },
        { "rolling",           "X-------X-..rrrr", "1 b6 1 5 8 8" },
        { "glide run",         "X--/X-.X-/X-..X.X---..X/X---\\X..", "1 b3 b3 5 1 1 1 8 1" },
        { "half-bar breathe",  "X-------......X-", "1 b7" },
        { "syncopated three",  "X-.X-.X-----X-..", "1 5 8 1" },
        { "chord follower",    "X-----X-----X--/X-----X-----X-..", "1 1 1 1 1 2" },
        { "tension b2",        "X-------X--\\X-..", "1 b2 1", true },
        { "dead stop",         "X-.............r", "1 1" },
        { "ratchet bounce",    "X..X....X..X..X.", "1 1 b7 b7 5" },
        { "steady double-hit", "X-------X.X-----", "1 1 1" },
        { "steady slide home", "X-----------X--/", "1 1 b7" },
        { "bouncy follower",   "X-----X-----..X/", "1 1 b7" } };
    return B;
}
// HIP HOP bass (11): round sub on the kick, dotted pushes, late entries, two-note soul, gliding funk, walking, slow slides
const std::vector<BassArch>& hiphopBass()
{
    static const std::vector<BassArch> B {
        { "root on the kick",  "X---......X--...", "1 1" },
        { "dotted push",       "X--...X--.X.....", "1 b7 1" },
        { "late entry",        ".X--.......X--..", "1 5" },
        { "two-note soul",     "X-------X-----.X", "1 4 b7" },
        { "funk glide",        "X-.X-/X-..X.X--/", "1 1 8 5 b7" },
        { "walking",           "X.X.X.X.X.X.X.X.", "1 b3 4 #4 5 4 b3 7-" },
        { "sustain b2",        "X-------..X-----", "1 b2" },
        { "slow slide",        "X----------/X---", "1 b6", true },
        { "octave pops",       "X..X..X.X..X.X..", "1 8 1 5 8 1" },
        { "turnaround walk",   "X---........X.X.", "1 5- b7" },
        { "kick shadow",       "X-.X--...X--....", "1 1 b3" } };
    return B;
}
struct MelArch { const char* name; const char* grid; };
// TRAP melody rhythms (10, read in half-time) + HIP HOP (10, loop / chop feel)
const std::vector<MelArch>& trapMel()
{
    static const std::vector<MelArch> M {
        { "sparse dark",      "X.....X.....X-.." },
        { "question answer",  "X.X...X-........X.X...X.X---...." },
        { "bouncy",           "X..X..X..X..X..." },
        { "rolling triplets", "X...rrrrX......." },
        { "dark climb",       "X.X.X.X.X.X.X---" },
        { "pluck ostinato",   "XX.XX.XX.XX.X.X." },
        { "cinematic hold",   "X-------X---X-.." },
        { "octave hook",      "X..X..X.X.....X." },
        { "syncopated bell",  "X..X..X...X..X.." },
        { "church keys",      "X..X-.X.X-.X..X-" },
        { "hypnotic cell",    "X.X.X...X.X.X..." },
        { "two-note cell",    "X..X....X..X...." },
        { "singing pickups",  "X---..X.X-X-..XXX-----X.X---..XX" },
        { "triumphant climb", "X..X..X.X---..X." } };
    return M;
}
const std::vector<MelArch>& hiphopMel()
{
    static const std::vector<MelArch> M {
        { "chopped stabs",    "X..X..X...X....." },
        { "soul vamp",        "X---..X-.X----.." },
        { "repeat and vary",  "X.X..X..X-......X.X..X..X-......X.X..X..X-......X.X..X.X.X--...." },
        { "blue-note answer", "........X.XXX-.." },
        { "horn hits",        "X..........X-..." },
        { "gliding lead",     "X------/X-----....X----\\X-------" },
        { "triplet piano",    "X..X..X..X..X..." },
        { "underwater keys",  "X-------X-------" },
        { "off-beat comp",    "..X....X-.X....X" },
        { "pentatonic riff",  "X.X.XX..X..X...." } };
    return M;
}
// progressions: "degree:quality[:bars]" (degrees relative to the key; bars x the flavour's bars per chord)
struct Prog { const char* text; bool major = false; };
const std::vector<Prog>& trapProgs()
{
    static const std::vector<Prog> P {
        { "1:m b6:M" }, { "1:m 4:m" }, { "1:m b6:M b7:M 1:m" }, { "1:m b6:M 4:m 5:M" }, { "1:m b2:M 1:m b2:M" },
        { "1:m b7:M b6:M 5:M" }, { "1:m 1:m b6:M b3:M" }, { "1:m 5:m b6:M 4:m" }, { "1:m:4" }, { "1:m 4:m b6:M b7:M" },
        { "1:m b6:M b7:M 5:m" }, { "1:m 4:m b6:M 5:M" }, { "b6:M b7:M 1:m:2" } };   // i-VI-VII-v, i-iv-VI-V (triumphant in minor), bVI-bVII-i (dark)
    return P;
}
const std::vector<Prog>& hiphopProgs()
{
    static const std::vector<Prog> P {
        { "1:m9:2" }, { "1:m7 4:m7" }, { "1:m9 b7:69" }, { "1:mM7 1:m7" }, { "2:m7 5:7 1:M7 6:7", true },
        { "1:m7 b6:M7 2:m7b5 5:7#9" }, { "4:M7 3:m7 2:m7 1:M7", true }, { "1:m7 4:7" }, { "b6:M7 b7:M 1:m7:2" }, { "1:m b2:M 1:m 5:m" } };
    return P;
}
struct Flav
{
    const char* name; int bpmLo, bpmHi; double swLo, swHi;
    std::vector<std::pair<const char*, double>> scales;   // minor harmonic phrygian penta dorian mixolydian
    std::vector<int> progs; int barsPerChord; std::vector<int> bass, mel; int regLo, regHi; bool sevenths; int chordRhythm; double doubleTimeP;
};
const std::vector<Flav>& flavours (int mood)
{
    // TRAP poles: DARK (sparse, heavy 808 sustains + octave drops, phrygian / harmonic colour, rolls as ornaments), TRIUMPHANT (bright
    // singing 2-bar phrases with pickups over minor progressions that move, 808 on the roots with frequent slides and the and-of-2 /
    // and-of-4 bounce), HYPNOTIC (short 2-4 note cells looping with micro-variation, a steady 808 with double hits + slides), plus DRILL
    // (syncopated bells, glide-heavy 808) and RATCHET (full-time 92-104, short kick-locked stabs)
    static const std::vector<Flav> T {
        { "DARK",        136, 150, 50, 52, { { "harmonic", 40 }, { "minor", 35 }, { "phrygian", 25 } }, { 0, 3, 4, 8, 5, 12 }, 2, { 0, 4, 8, 11, 12, 1 }, { 0, 6, 1, 3 }, 57, 72, false, 0, 0 },
        { "TRIUMPHANT",  140, 150, 50, 52, { { "minor", 55 }, { "harmonic", 30 }, { "penta", 15 } }, { 10, 11, 2, 3, 9 }, 1, { 3, 10, 1, 9, 16 }, { 12, 7, 13, 1, 2 }, 60, 76, false, 0, 0 },
        { "HYPNOTIC",    130, 145, 50, 52, { { "minor", 45 }, { "phrygian", 25 }, { "harmonic", 15 }, { "penta", 15 } }, { 8, 0, 1 }, 2, { 14, 15, 0 }, { 10, 11, 5 }, 60, 74, false, 0, 0 },
        { "DRILL",       140, 145, 50, 52, { { "harmonic", 50 }, { "phrygian", 30 }, { "minor", 20 } }, { 8, 4, 0, 3 }, 2, { 7, 3, 11, 6 }, { 8, 3, 2, 4 }, 62, 77, false, 0, 0 },
        { "RATCHET",      92, 104, 50, 54, { { "minor", 50 }, { "penta", 50 } }, { 0, 1, 6 }, 2, { 13, 5, 9 }, { 2, 7, 0 }, 60, 74, false, 1, 0 },
        // EMO (appended; its own tables + engine at the end of the file): 140..170 half-time, straight 8ths with 5..10 % swing
        { "EMO",         140, 170, 50.825, 51.65, { { "minor", 100 } }, { 0 }, 2, { 0 }, { 0 }, 55, 72, false, 0, 0 } };
    static const std::vector<Flav> H {
        { "BOOM BAP",     86,  95, 56, 62, { { "dorian", 40 }, { "minor", 40 }, { "penta", 20 } }, { 0, 1, 2, 3, 5, 8 }, 1, { 0, 1, 2, 3, 9, 10 }, { 0, 1, 2, 4, 9 }, 60, 76, true, 0, 0.12 },
        { "MEMPHIS",     135, 150, 50, 54, { { "phrygian", 50 }, { "minor", 50 } }, { 9 }, 2, { 6, 0, 10 }, { 6, 0, 3 }, 60, 76, false, 0, 0 },
        { "WEST GLIDE",   90, 100, 54, 58, { { "mixolydian", 40 }, { "dorian", 40 }, { "minor", 20 } }, { 6, 4, 7 }, 1, { 4, 8, 3 }, { 5, 9, 0 }, 70, 86, true, 0, 0 },
        { "UNDERWATER",  120, 150, 50, 54, { { "minor", 50 }, { "dorian", 50 } }, { 2, 3, 8, 1 }, 2, { 7, 3, 0 }, { 7, 1, 4 }, 58, 74, true, 0, 0 },
        { "JAZZ SOUL",    80,  98, 54, 60, { { "dorian", 60 }, { "minor", 20 }, { "mixolydian", 20 } }, { 5, 4, 1, 7 }, 1, { 5, 10, 1, 2 }, { 8, 3, 2, 6 }, 60, 77, true, 0, 0.1 } };
    return mood == Trap ? T : H;
}
const Flav& flav (int mood, int f) { const auto& v = flavours (mood); return v[(size_t) clampv (f, 0, (int) v.size() - 1)]; }
const std::vector<BassArch>& bassOf (int mood) { return mood == Trap ? trapBass() : hiphopBass(); }
const std::vector<MelArch>& melOf (int mood) { return mood == Trap ? trapMel() : hiphopMel(); }

int degSemis (const std::string& d)
{
    static const std::map<std::string, int> D { { "1", 0 }, { "b2", 1 }, { "2", 2 }, { "b3", 3 }, { "3", 4 }, { "4", 5 }, { "#4", 6 }, { "b5", 6 }, { "5", 7 }, { "b6", 8 }, { "6", 9 }, { "b7", 10 }, { "7", 11 }, { "8", 12 } };
    auto it = D.find (d); return it == D.end() ? 0 : it->second;
}
std::vector<int> qualityIv (const std::string& q)
{
    static const std::map<std::string, std::vector<int>> Q {
        { "m", { 0, 3, 7 } }, { "M", { 0, 4, 7 } }, { "m7", { 0, 3, 7, 10 } }, { "M7", { 0, 4, 7, 11 } }, { "7", { 0, 4, 7, 10 } },
        { "m9", { 0, 3, 7, 10, 14 } }, { "M9", { 0, 4, 7, 11, 14 } }, { "9", { 0, 4, 7, 10, 14 } }, { "69", { 0, 4, 7, 9, 14 } },
        { "m7b5", { 0, 3, 6, 10 } }, { "7#9", { 0, 4, 7, 10, 15 } }, { "mM7", { 0, 3, 7, 11 } }, { "madd9", { 0, 3, 7, 14 } }, { "add9", { 0, 4, 7, 14 } } };
    auto it = Q.find (q); return it == Q.end() ? std::vector<int> { 0, 3, 7 } : it->second;
}
std::vector<std::string> split (const std::string& s, char c)
{
    std::vector<std::string> o; std::string cur;
    for (char ch : s) { if (ch == c) { if (! cur.empty()) o.push_back (cur); cur.clear(); } else cur += ch; }
    if (! cur.empty()) o.push_back (cur);
    return o;
}
std::string realScale (const std::string& s) { return s == "penta" ? "minor" : s; }

// ---------------------------------------------------------------- grids
struct Hit { int step = 0, len = 1; bool roll = false, slide = false; };
std::vector<Hit> parseGrid (const std::string& g)
{
    std::vector<Hit> o;
    for (int i = 0; i < (int) g.size(); ++i)
    {
        const char c = g[(size_t) i];
        if (c == 'X' || c == 'r') { Hit h; h.step = i; h.len = 1; h.roll = c == 'r'; o.push_back (h); }
        else if ((c == '-' || c == '/' || c == '\\') && ! o.empty() && o.back().step + o.back().len == i) { o.back().len++; if (c != '-') o.back().slide = true; }
    }
    return o;
}
int gridSteps (const char* g) { return (int) std::string (g).size(); }

struct Ctx
{
    const Idea& I; int mood; const Flav& F; int U; /* ticks per grid step */ int total;
    std::vector<int> pool;   // the melody's pitch classes (absolute)
    double tps;              // ticks per second
};
Ctx makeCtx (const Idea& I, int mood)
{
    const int f = flavourFor (I, mood);
    Ctx c { I, mood, flav (mood, f), (I.doubleTime && I.mode == mood ? 48 : 24), std::max (BAR, I.total), {}, 0 };
    const std::string col = I.mode == mood ? I.colour : std::string();
    if (col == "penta") for (int s : { 0, 3, 5, 7, 10 }) c.pool.push_back (mod12 (I.H.key + s));
    else for (int p : I.H.keyPcs) c.pool.push_back (p);
    if (c.pool.empty()) for (int s : scaleOf ("minor")) c.pool.push_back (mod12 (I.H.key + s));
    c.tps = std::max (40, I.g.bpm) / 60.0 * PPQ;
    return c;
}
bool inPool (const Ctx& c, int p, int t) { return has (c.pool, mod12 (p)) || c.I.H.chordTone (p, t); }
int nearestWhere (int p, int lo, int hi, const std::function<bool (int)>& ok)
{
    for (int d = 0; d < 13; ++d) { if (p + d <= hi && p + d >= lo && ok (p + d)) return p + d; if (p - d >= lo && p - d <= hi && ok (p - d)) return p - d; }
    return clampv (p, lo, hi);
}
int stepInPool (const Ctx& c, int p, int steps, int t, int lo, int hi)
{   // move by scale steps inside the melody pool
    int q = p; const int dir = steps > 0 ? 1 : -1;
    for (int k = 0; k < std::abs (steps); ++k) { int r = q + dir; for (int g = 0; g < 4 && ! inPool (c, r, t); ++g) r += dir; q = r; }
    if (q > hi) q -= 12; if (q < lo) q += 12;
    return clampv (q, lo - 2, hi + 2);
}
int chordToneNear (const Ctx& c, int p, int t, int lo, int hi)
{
    return nearestWhere (p, lo, hi, [&] (int q) { return c.I.H.chordTone (q, t); });
}
} // namespace

// EMO (TRAP's sixth flavour): the setup, chords and voicings (defined at the end of the file)
void emoPickProg (Idea& I, uint32_t seed, bool avoidCurrent);
void emoSetup (Idea& I, Rng& R);
void emoVoicings (Idea& I);

// ================================================================= public
const char* flavourName (int mood, int f) { return urbanMood (mood) ? flav (mood, f).name : ""; }   // (TRAP 5 = EMO)
int urbanBassArchCount (int mood) { return (int) bassOf (mood).size(); }
FlavourInfo flavourInfo (int mood, int f) { FlavourInfo o; if (! urbanMood (mood)) return o; const auto& F = flav (mood, f); o.bpmLo = F.bpmLo; o.bpmHi = F.bpmHi; o.swingLo = F.swLo; o.swingHi = F.swHi; o.doubleTimeOk = F.doubleTimeP > 0; return o; }
int urbanMelArchCount (int mood) { return (int) melOf (mood).size(); }
const char* urbanBassArchName (int mood, int i, int flavour)
{
    if (mood == Trap && flavour == kEmo) return i >= 0 && i < emoBassArchCount() ? emoBassName (i) : "";
    const auto& B = bassOf (mood); return urbanMood (mood) && i >= 0 && i < (int) B.size() ? B[(size_t) i].name : "";
}
const char* urbanMelArchName (int mood, int i, int flavour)
{
    if (mood == Trap && flavour == kEmo) return i >= 0 && i < emoMelArchCount() ? emoMelName (i) : "";
    if (mood == Trap && i >= 100) return fluteGestName (i - 100);   // the 3.2.1 flute writer's gestures (UrbanMoods.h)
    const auto& M = melOf (mood); return urbanMood (mood) && i >= 0 && i < (int) M.size() ? M[(size_t) i].name : "";
}
int flavourFor (const Idea& I, int mood)
{
    if (! urbanMood (mood)) return 0;
    if (I.mode == mood && I.flavourUsed >= 0) return clampv (I.flavourUsed, 0, flavourCount (mood) - 1);
    return (int) (mix (I.g.arpSeed, (uint32_t) mood * 977u) % (uint32_t) kFlavours);   // a lane that borrows the mood: a seeded flavour
}

void urbanPickProg (Idea& I, uint32_t seed, bool avoidCurrent)
{
    if (isEmo (I)) { emoPickProg (I, seed, avoidCurrent); return; }
    const auto& F = flav (I.mode, I.flavourUsed);
    const auto& P = I.mode == Trap ? trapProgs() : hiphopProgs();
    Rng R (seed ^ 0x9B0Bu);
    // DARKNESS: the darker progressions (bVI-bVII-i, i-bII, i-bVI, the drone, the harmonic V) weigh more, and join the pool when it is high
    const double dk = darknessOf (I, I.mode) / 100.0;
    std::vector<int> pool = F.progs;
    const std::vector<int> darkSet = I.mode == Trap ? std::vector<int> { 0, 3, 4, 8, 12 } : std::vector<int> { 8, 9, 5 };
    if (I.mode == Trap && dk > 0.6) for (int d : { 4, 12 }) if (std::find (pool.begin(), pool.end(), d) == pool.end()) pool.push_back (d);
    std::vector<double> pw; for (int x : pool)
    {
        const bool dark = std::find (darkSet.begin(), darkSet.end(), x) != darkSet.end(), extra = std::find (F.progs.begin(), F.progs.end(), x) == F.progs.end();
        pw.push_back ((dark ? 0.5 + 1.5 * dk : 1.5 - dk) * (extra ? (dk - 0.6) * 2.0 : 1.0));
    }
    int pi = pool[(size_t) pickW (R, pw)];
    for (int k = 0; k < 8 && avoidCurrent && pi == I.urbanProg && pool.size() > 1; ++k) pi = pool[(size_t) pickW (R, pw)];
    I.urbanProg = pi;
    const Prog& pr = P[(size_t) clampv (pi, 0, (int) P.size() - 1)];
    std::vector<Slot> out; const int mult = std::max (1, F.barsPerChord) * (I.doubleTime ? 2 : 1);
    for (auto& tok : split (pr.text, ' '))
    {
        const auto parts = split (tok, ':');
        std::string q = parts.size() > 1 ? parts[1] : "m"; const int bars = parts.size() > 2 ? std::max (1, std::atoi (parts[2].c_str())) : 1;
        if (F.sevenths && I.mode == Trap)
        {   // church keys: minor 7 / 9, major 7 / add9, a dominant V
            if (q == "m") q = R() < 0.4 ? "m9" : "m7"; else if (q == "M") q = degSemis (parts[0]) == 7 ? "7" : (R() < 0.35 ? "add9" : "M7");
        }
        Slot s; FixedChord fc; fc.root = mod12 (degSemis (parts[0])); fc.iv = qualityIv (q);
        fc.chrom = false; for (int x : fc.iv) if (! has (scaleOf (realScale (I.g.scale)), mod12 (fc.root + x))) fc.chrom = true;
        s.fixed = fc; s.tok = parts[0] + q; s.len = 4 * bars * mult; s.r = R(); s.r2 = R();
        out.push_back (s);
    }
    I.simpleProg = out; I.g.prog = out;
}

void urbanSetup (Idea& I)
{
    const int mood = I.mode;
    Rng R (I.seedUsed ^ 0x7A11CEu);
    I.flavourUsed = I.flavour >= 0 ? clampv (I.flavour, 0, flavourCount (mood) - 1) : irand (R, kFlavours);
    if (I.flavour < 0 && mood == Trap) I.flavourUsed = trapAutoFlavour (I.seedUsed);   // TRAP AUTO: DARK / DRILL / EMO (PUSH FURTHER rotates them)
    if (isEmo (I)) { emoSetup (I, R); return; }
    const auto& F = flav (mood, I.flavourUsed);
    // tempo (double-time hip hop: the same feel on the 1/8 grid at twice the tempo)
    I.doubleTime = F.doubleTimeP > 0 && R() < F.doubleTimeP;
    int bpm = F.bpmLo + irand (R, F.bpmHi - F.bpmLo + 1);
    if (I.doubleTime) bpm = clampv (bpm * 2, 160, 200);
    I.g.bpm = bpm;
    // scale (the progression's family decides major / minor)
    // DARKNESS weights the scale: phrygian / harmonic minor up, pentatonic / dorian / mixolydian down; TRAP: dorian only as a
    // brighter minor below 50, never major
    const double dk = darknessOf (I, mood) / 100.0;
    std::vector<std::pair<std::string, double>> scs; for (auto& x : F.scales) scs.push_back ({ x.first, x.second });
    if (mood == Trap && dk < 0.5) scs.push_back ({ "dorian", 30.0 * (1.0 - 2.0 * dk) });
    std::vector<double> w;
    for (auto& x : scs)
    {
        double f = 1.0;
        if (x.first == "phrygian" || x.first == "harmonic") f = 0.4 + 1.6 * dk;
        else if (x.first == "penta") f = std::max (0.05, 1.3 - dk);
        else if (x.first == "dorian") f = mood == Trap ? (dk < 0.5 ? 1.0 : 0.0) : std::max (0.1, 1.3 - 0.8 * dk);
        else if (x.first == "mixolydian") f = std::max (0.1, 1.2 - dk);
        w.push_back (x.second * f);
    }
    std::string sc = scs[(size_t) pickW (R, w)].first;
    I.colour = sc == "penta" ? "penta" : "";
    I.g.scale = realScale (sc);
    // key: a root that puts the 808 / sub at C1..B1 (any key works; these sit best)
    static const int keys[] { 5, 6, 7, 8, 9, 10, 0, 1, 2 };
    if (! I.words.empty() && parsePrompt (I.words).key) I.g.key = *parsePrompt (I.words).key;
    else I.g.key = keys[irand (R, 9)];
    // swing as the idea's GROOVE (playback + export)
    const double sw = F.swLo + R() * (F.swHi - F.swLo);
    I.groove = swingToGroove (sw);
    // chords
    I.chordBars = 1; I.voicing = 0; I.chordRhythm = F.chordRhythm; I.simple = true;
    urbanPickProg (I, I.seedUsed ^ 0x5EED5u, false);
    const auto& P = mood == Trap ? trapProgs() : hiphopProgs();
    if (P[(size_t) I.urbanProg].major) { I.g.scale = sc == "mixolydian" ? "mixolydian" : "major"; I.colour.clear(); }
    else if (I.g.scale == "mixolydian") I.g.scale = "dorian";
    I.phraseBars = progTicks (I.simpleProg) < 4 * BAR ? 4 : 0;   // at least 4 bars: the loop repeats (with its small variations)
    I.form = 0; I.stagger = 0; I.stutter = 0;
    I.arpSpb = 16; I.melSpb = 16;
}

void urbanVoicings (Idea& I)
{   // TRAP: dark 2-3 note voicings, low (inversions in 50..67); HIP HOP: 7ths / 9ths, rootless when 4+ notes (the bass has the root), 52..74
    if (isEmo (I)) { emoVoicings (I); return; }
    const bool trap = I.mode == Trap; const auto& F = flav (I.mode, I.flavourUsed);
    const int lo = trap ? 50 : 52, hi = trap ? 67 : 74;
    std::vector<int> prev;
    for (auto& h : I.H.chords)
    {
        std::vector<int> pcs = h.pcs;
        if (trap && ! F.sevenths && pcs.size() > 3) pcs.resize (3);
        if (! trap && pcs.size() >= 4) pcs.erase (std::remove (pcs.begin(), pcs.end(), h.root), pcs.end());   // rootless
        if (pcs.size() > 5) pcs.resize (5);
        std::vector<std::vector<int>> opts; for (int pc : pcs) { std::vector<int> o; for (int p = lo; p <= hi; ++p) if (mod12 (p) == pc) o.push_back (p); if (! o.empty()) opts.push_back (o); }
        if (opts.empty()) continue;
        std::vector<int> best; double bc = 1e9; std::vector<size_t> idx (opts.size(), 0);
        while (true)
        {
            std::vector<int> v; for (size_t i = 0; i < opts.size(); ++i) v.push_back (opts[i][idx[i]]);
            std::sort (v.begin(), v.end());
            bool ok = v.back() - v.front() <= (trap ? 12 : 14);
            for (size_t i = 1; ok && i < v.size(); ++i) if (v[i] - v[i - 1] < 1) ok = false;
            if (ok)
            {
                double mean = 0; for (int x : v) mean += x; mean /= (double) v.size();
                double cost = std::abs (mean - (lo + hi) * 0.5) * 0.4;
                if (! prev.empty()) { for (int x : v) { int m = 99; for (int y : prev) m = std::min (m, std::abs (x - y)); cost += m; } }
                if (trap && v.size() == 3 && mod12 (v[0]) == h.root) cost += 0.6;   // inversions welcome
                if (cost < bc) { bc = cost; best = v; }
            }
            size_t k = 0; while (k < idx.size() && ++idx[k] >= opts[k].size()) { idx[k] = 0; ++k; }
            if (k >= idx.size()) break;
        }
        if (! best.empty()) { h.voicing = best; prev = best; }
    }
}

// ================================================================= MELODY
namespace
{
struct MelNote { int t, d, p; float v; bool slide = false; };
// one bar (or the archetype's span) of hits -> pitches
std::vector<Note> pitchHits (const Ctx& c, const std::vector<Hit>& hits, int t0, Rng& R, int shape, int firstIv, int lo, int hi, int& prevP, bool resolveEnd, bool openEnd)
{
    std::vector<Note> out; int dir = R() < 0.5 ? 1 : -1;
    for (size_t i = 0; i < hits.size(); ++i)
    {
        const Hit& h = hits[i]; const int t = t0 + h.step * c.U; if (t >= c.total) break;
        int p;
        if (prevP < 0)
        {   // the anchor: a chord tone (root / third / fifth) near the middle of the register
            const auto& ch = c.I.H.at (t); const int center = (lo + hi) / 2;
            std::vector<int> cand { ch.root }; for (int q : ch.pcs) cand.push_back (q);
            for (int d : { 2, 5, 10 }) if (has (c.pool, mod12 (c.I.H.key + d))) cand.push_back (mod12 (c.I.H.key + d));   // or start on a colour tone (2, 4, b7)
            const int pc = pickV (R, cand);
            p = nearestWhere (center, lo, hi, [&] (int q) { return mod12 (q) == mod12 (pc); });
        }
        else
        {
            int iv = 0;
            if (i == 1 && firstIv != 99) iv = firstIv;
            else switch (shape)
            {
                case 0: iv = (R() < 0.75 ? 1 : 2) * dir; if (R() < 0.3) dir = -dir; break;                    // stepwise
                case 1: iv = (i > 0 && h.step - hits[i - 1].step < 2 ? 1 : (R() < 0.5 ? 3 : 4)) * dir; dir = -dir; break;   // leaping (4ths / 5ths; steps between 1/16 neighbours)
                case 2: iv = 0; break;                                                                           // arpeggiated (chord tones below)
                case 3: iv = R() < 0.7 ? 0 : (R() < 0.5 ? 1 : -1); break;                                        // single-note rhythmic
                default: iv = (R() < 0.7 ? 1 : 2) * dir; if (R() < 0.35) dir = -dir; break;                     // octave-displaced (below)
            }
            if (shape == 2)
            {   // the next chord tone up / down
                const int sgn = dir; int q = prevP + sgn; for (int g = 0; g < 12 && ! c.I.H.chordTone (q, t); ++g) q += sgn;
                if (q > hi || q < lo) { dir = -dir; q = prevP - sgn; for (int g = 0; g < 12 && ! c.I.H.chordTone (q, t); ++g) q -= sgn; }
                p = q;
            }
            else if (iv == 12 || iv == -12) p = prevP + iv;
            else p = stepInPool (c, prevP, iv, t, lo, hi);
        }
        const bool strong = (h.step * c.U) % PPQ == 0 || h.len >= 4;
        if (strong && ! c.I.H.chordTone (p, t) && R() < 0.75) p = chordToneNear (c, p, t, lo, hi);
        if (! inPool (c, p, t)) p = nearestWhere (p, lo - 2, hi + 2, [&] (int q) { return inPool (c, q, t); });
        if (h.len >= 2 && c.I.H.clash (p, t)) p = chordToneNear (c, p, t, lo, hi);
        const bool last = i + 1 == hits.size();
        if (last && resolveEnd)
        {   // the answer lands on 1 or 5
            const int k1 = mod12 (c.I.H.key), k5 = mod12 (c.I.H.key + 7);
            p = nearestWhere (p, lo, hi, [&] (int q) { return mod12 (q) == k1 || mod12 (q) == k5; });
        }
        else if (last && openEnd)
        {   // the call stays open: 2, 4 or b7 (or the chord's third)
            p = nearestWhere (p, lo, hi, [&] (int q) { const int d = mod12 (q - c.I.H.key); return (d == 2 || d == 5 || d == 10 || d == 3) && inPool (c, q, t); });
        }
        p = clampv (p, lo - 3, hi + 3);
        Note n; n.t = t; n.p = p; n.d = std::max (6, h.len * c.U - (h.len >= 2 ? 6 : 4)); n.v = (h.step * c.U) % PPQ == 0 ? 0.84f : 0.72f; n.acc = (h.step * c.U) % BAR == 0;
        if (h.slide) { n.l.type = 1; n.l.w = 0.72f; }
        if (h.roll)
        {   // a roll: the step struck as two 1/32s
            const int half = std::max (6, c.U / 2);
            for (int k = 0; k < 2; ++k) { Note r = n; r.t = t + k * half; r.d = half - 2; r.v = 0.6f + 0.1f * (float) k; r.l = Link {}; out.push_back (r); }
        }
        else out.push_back (n);
        prevP = p;
    }
    return out;
}
// HERO DEVICES: 0 roll before a downbeat, 1 octave jump, 2 chromatic grace, 3 triplet beat, 4 long slide, 5 b2 hit
void addRoll (std::vector<Note>& ph, int t0, int t1, Rng& R, bool triplet)
{   // a held note re-struck as 1/32s or 1/16 triplets for half a beat .. a beat before the end of the span
    for (int k = (int) ph.size() - 1; k >= 0; --k)
    {
        Note& n = ph[(size_t) k]; if (n.t < t0 || n.t >= t1 || n.d < PPQ / 2) continue;
        const int span = std::min (n.d, R() < 0.5 ? PPQ / 2 : PPQ), sub = triplet ? PPQ / 6 : PPQ / 8;
        const int start = n.t + n.d - span; std::vector<Note> add;
        n.d = std::max (6, start - n.t - 1);
        for (int t = start; t + sub <= n.t + n.d + span + 1 && t < t1; t += sub) { Note r = n; r.t = t; r.d = sub - 2; r.l = Link {}; r.v = clampv (0.5f + 0.35f * (float) (t - start) / (float) std::max (1, span), 0.3f, 0.9f); r.acc = false; add.push_back (r); }
        ph.insert (ph.end(), add.begin(), add.end()); sortNotes (ph); return;
    }
}
void addGrace (std::vector<Note>& ph, const Ctx& c, Rng& R)
{   // a 1/32 grace from a semitone below into one strong note
    std::vector<size_t> cand; for (size_t i = 0; i < ph.size(); ++i) if (ph[i].t % PPQ == 0 && ph[i].t >= PPQ / 8) cand.push_back (i);
    if (cand.empty()) return;
    const Note tgt = ph[pickV (R, cand)];
    for (auto& n : ph) if (n.t < tgt.t && n.t + n.d > tgt.t - PPQ / 8) n.d = std::max (6, tgt.t - PPQ / 8 - n.t - 1);
    bool clear = true; for (auto& n : ph) if (n.t >= tgt.t - PPQ / 8 && n.t < tgt.t) clear = false;
    if (! clear) return;
    Note g = tgt; g.t = tgt.t - PPQ / 8; g.d = PPQ / 8 - 2; g.p = tgt.p - 1; g.v = 0.55f; g.l = Link {}; g.acc = false; (void) c;
    ph.push_back (g); sortNotes (ph);
}
void addTripletBeat (std::vector<Note>& ph, const Ctx& c, int t0, int t1, Rng& R, int lo, int hi)
{   // one beat becomes three 1/8 triplets (stepwise from the note before)
    const int beats = (t1 - t0) / PPQ; if (beats < 2) return;
    const int b = t0 + (1 + irand (R, beats - 1)) * PPQ; if (b + PPQ > t1) return;
    int prev = -1; for (auto& n : ph) if (n.t < b) prev = n.p;
    ph.erase (std::remove_if (ph.begin(), ph.end(), [&] (const Note& n) { return n.t >= b && n.t < b + PPQ; }), ph.end());
    for (auto& n : ph) if (n.t < b && n.t + n.d > b) n.d = std::max (6, b - n.t - 2);
    if (prev < 0) prev = (lo + hi) / 2;
    int p = prev; const int dir = R() < 0.5 ? 1 : -1;
    for (int k = 0; k < 3; ++k) { p = stepInPool (c, p, dir, b, lo, hi); Note n; n.t = b + k * PPQ / 3; n.d = PPQ / 3 - 4; n.p = p; n.v = k == 0 ? 0.8f : 0.68f; ph.push_back (n); }
    sortNotes (ph);
}
void octaveJump (std::vector<Note>& ph, int t0, int t1, Rng& R, int hi)
{   // 1-2 notes of the repeat jump an octave (up, or down when that would go too high)
    std::vector<size_t> c; for (size_t i = 0; i < ph.size(); ++i) if (ph[i].t >= t0 && ph[i].t < t1 && ph[i].d >= PPQ / 4) c.push_back (i);
    const int k = std::min ((int) c.size(), 1 + irand (R, 2));
    for (int j = 0; j < k; ++j)
    {
        const size_t idx = c[(size_t) irand (R, (int) c.size())]; auto& n = ph[idx]; const int d = n.p + 12 <= hi + 7 ? 12 : -12; n.p += d;
        if (idx > 0 && ph[idx - 1].t == n.t - PPQ / 8 && ph[idx - 1].d <= PPQ / 8) ph[idx - 1].p += d;   // its grace note moves with it
    }
}
void longSlide (std::vector<Note>& ph, int t0, int t1)
{   // the longest note of the span holds and slides into the next one
    int bi = -1, bd = 0; for (size_t i = 0; i + 1 < ph.size(); ++i) if (ph[i].t >= t0 && ph[i].t < t1 && ph[i].d > bd && ph[i + 1].p != ph[i].p) { bd = ph[i].d; bi = (int) i; }
    if (bi < 0) return;
    auto& n = ph[(size_t) bi]; n.d = std::max (n.d, ph[(size_t) bi + 1].t - n.t - 2); n.l.type = 1; n.l.w = 0.6f;
}
void b2Hit (std::vector<Note>& ph, const Ctx& c, int t0, int t1)
{   // one weak-beat note becomes the b2 that falls back to the root (phrygian / harmonic minor menace)
    for (size_t i = 0; i + 1 < ph.size(); ++i)
    {
        auto& n = ph[i]; if (n.t < t0 || n.t >= t1 || n.t % PPQ == 0) continue;
        if (mod12 (ph[i + 1].p - c.I.H.key) != 0) continue;
        n.p = ph[i + 1].p + 1; n.d = std::min (n.d, PPQ / 2); return;
    }
}
// the phrase over the chords that follow: keep the shape, fix what rubs
void refitPhrase (std::vector<Note>& ph, const Ctx& c, int lo, int hi)
{
    for (auto& n : ph)
    {
        if (n.d <= PPQ / 8 + 2) continue;   // graces / roll strokes keep their pitch
        if (c.I.H.clash (n.p, n.t) || (! inPool (c, n.p, n.t) && n.d >= PPQ / 2)) n.p = chordToneNear (c, n.p, n.t, lo - 2, hi + 2);
    }
}
} // namespace


// ================================================================= TRAP FLUTE (Idea::melVer 1, 3.2.1)
// The slow, singing trap-flute line, written from the flute reference's measurements (RESEARCH.md 1.8 / 2.12 / 3.3): short phrases
// (2-7 notes, mostly 1/8s and 1/4s) that end on a long held note (4-14 sixteenths), a breath of a beat or more between them, mostly
// stepwise motion (46 %) with one expressive leap, repeated-note stabs, 1/16 runs only as ornaments into a held note, grace notes,
// trills and slides into the long notes. A 4-bar phrase is a 2-bar call (open ending: the 3rd / 5th / 2 / 4) and a 2-bar answer
// (resolved: 1 or 5); the phrase repeats over the idea with one small change each time. Deterministic from the lane's seed.
namespace
{
// a 1-bar gesture: the rhythm on the 1/16 grid and one contour letter per onset. Letters: s step (direction kept), u / d a step
// up / down, r the same note again (a stab), o an octave jump, T a held chord tone (the gesture's goal), a / b an ornament a scale
// step above / below the next T (a run of b's climbs into it). w = weights per flavour (DARK TRIUMPHANT HYPNOTIC DRILL)
struct Gest { const char* name; const char* grid; const char* con; int w[4]; };
const std::vector<Gest>& fluteGests()
{
    static const std::vector<Gest> G {
        { "sigh",         "X-X-------......", "sT",    { 5, 3, 2, 3 } },
        { "climb",        "X-X-X-X---......", "uuuT",  { 2, 5, 1, 3 } },
        { "lilt",         "X--XX--XX-------", "ududT", { 2, 4, 3, 5 } },
        { "turn",         "X------.XXX-----", "TabT",  { 3, 3, 1, 3 } },
        { "stabs",        "X.X.X.X-----....", "srrT",  { 2, 2, 5, 4 } },
        { "pickup cry",   "......X-X-X-----", "bbT",   { 2, 5, 1, 2 } },
        { "hold",         "X-----------....", "T",     { 5, 2, 2, 2 } },
        { "octave echo",  "X-X-....X-X-....", "sdod",  { 2, 2, 3, 3 } },
        { "falling line", "X-X-X-X-X-------", "ddddT", { 3, 3, 1, 2 } },
        { "question",     "X---X-X---......", "TsT",   { 3, 3, 2, 2 } },
        { "drill bounce", "X..X..X.X-------", "sudT",  { 1, 2, 2, 6 } },
        { "two stabs",    "X-..X-----......", "sT",    { 4, 2, 4, 3 } },
        { "run up",       "X-----..XXXX----", "TbbbT", { 1, 3, 1, 3 } },
        { "breath",       "................", "",      { 0, 0, 0, 0 } } };   // (bar 2 of a half only; weighted by DARKNESS)
    return G;
}
constexpr int kFluteBreath = 13, kFluteArchBase = 100;   // urbanMelArch = kFluteArchBase + the call's first gesture
int fluteFlavourCol (int f) { return f == 1 ? 1 : f == 2 ? 2 : f == 3 ? 3 : 0; }
struct FHit { int t, len; char con; int bar; bool endOfHalf = false; int p = -1; };

std::vector<Note> trapFluteMelody (const Idea& I, uint32_t seed, int* archOut)
{
    const Ctx c = makeCtx (I, Trap); const auto& F = c.F; const int fl = flavourFor (I, Trap), fc = fluteFlavourCol (fl);
    uint32_t s = mix (seed, (uint32_t) (I.H.key * 131 + I.g.bpm * 7 + I.flavourUsed * 9973) ^ 0xF1A7Eu);
    for (auto& ch : I.H.chords) s = mix (s, (uint32_t) (ch.root * 31 + (ch.tEnd - ch.t)));
    Rng R (s);
    const auto& G = fluteGests();
    const double dk = darknessOf (I, Trap) / 100.0, d0 = defaultDarkness (Trap) / 100.0;
    // register: the flavour's band (the reference sits at MIDI 59-87, median 68), DARKNESS down to an octave lower
    static const int regShiftTrap[] { 0, 0, -3, 3, -5, 2 };
    const int rs = regShiftTrap[irand (R, 6)];
    const int dShift = dk >= d0 ? -(int) std::lround (12.0 * (dk - d0) / std::max (0.01, 1.0 - d0)) : (int) std::lround (5.0 * (d0 - dk) / d0);
    const int lo = std::max (45, F.regLo + rs + dShift + 12 * clampv (I.reg, -1, 1)), hi = std::max (lo + 12, F.regHi + rs + dShift + 12 * clampv (I.reg, -1, 1));
    const int U = c.U, barT = 16 * U, phraseT = std::min (c.total, 4 * barT);
    const bool hypno = fl == 2;
    // ---- the four bars' gestures: call = bars 1-2, answer = bars 3-4. Forms: 0 call / answer (the answer opens with the call's
    // gesture), 1 motif twice then the answer (TRIUMPHANT), 2 one long sigh then its echo, HYPNOTIC = the same bar four times
    auto pickG = [&] (bool second) {
        std::vector<double> w; for (size_t i = 0; i < G.size(); ++i) w.push_back ((int) i == kFluteBreath ? (second ? 2.0 + 6.0 * dk : 0.0) : (double) G[i].w[fc] * (second && G[i].con[0] != 'T' && i != 11 ? 0.6 : 1.0));
        return pickW (R, w); };
    const int form = hypno ? 3 : irand (R, 3);
    int g[4];
    g[0] = pickG (false); g[1] = pickG (true);
    if (form == 3) { g[1] = g[0]; g[2] = g[0]; g[3] = g[0]; }
    else if (form == 1) { g[1] = g[0]; g[2] = pickG (false); g[3] = pickG (true); }
    else if (form == 2) { g[0] = 6; g[1] = 8; g[2] = R() < 0.5 ? 7 : 0; g[3] = pickG (true); }
    else { g[2] = R() < 0.65 ? g[0] : pickG (false); g[3] = pickG (true); if (g[3] == kFluteBreath && g[1] == kFluteBreath) g[3] = 0; }
    if (g[0] == kFluteBreath) g[0] = 0;
    if (archOut) *archOut = kFluteArchBase + g[0];
    // ---- the hits
    std::vector<FHit> hits;
    for (int b = 0; b < 4; ++b)
    {
        const Gest& ge = G[(size_t) g[b]]; const std::string con = ge.con; size_t k = 0;
        for (auto& h : parseGrid (ge.grid)) { if (b * barT + h.step * U >= phraseT) break; hits.push_back ({ b * barT + h.step * U, h.len * U, k < con.size() ? con[k] : 's', b }); ++k; }
    }
    if (hits.empty()) return {};
    for (int half = 0; half < 2; ++half)
    {   // the last T of each half (else its last note) carries the ending: open after the call, resolved after the answer
        int last = -1; for (size_t i = 0; i < hits.size(); ++i) if (hits[i].bar / 2 == half && (last < 0 || hits[i].con == 'T' || hits[(size_t) last].con != 'T')) last = (int) i;
        if (last >= 0) hits[(size_t) last].endOfHalf = true;
    }
    // ---- pitches: a singing contour (the call rises to a peak at the end of bar 2, the answer falls back), chord tones on the held
    // notes, scale steps between them, one leap at most per half
    const int peak = std::min (hi, lo + 12 + irand (R, 4)), startP = lo + 4 + irand (R, 3), endP = lo + 2 + irand (R, 3);
    auto want = [&] (int t) {
        const double u = (double) (t % phraseT) / (double) phraseT; auto sm = [] (double x) { x = clampv (x, 0.0, 1.0); return x * x * (3 - 2 * x); };
        return u < 0.5 ? startP + (peak - startP) * sm (u / 0.45) : peak + (endP - peak) * sm ((u - 0.5) / 0.5); };
    auto chordNear = [&] (int p, int t) { return nearestWhere (clampv (p, lo, hi), lo, hi, [&] (int q) { return c.I.H.chordTone (q, t); }); };
    auto poolNear = [&] (int p, int t) { return nearestWhere (clampv (p, lo, hi), lo - 2, hi + 2, [&] (int q) { return inPool (c, q, t); }); };
    auto stepPool = [&] (int p, int steps, int t) {   // scale steps inside the register; at its edge the line turns back (no octave wrap)
        int q = p; const int dir = steps > 0 ? 1 : -1;
        for (int k = 0; k < std::abs (steps); ++k) { int r = q + dir; for (int g = 0; g < 4 && ! inPool (c, r, t); ++g) r += dir; q = r; }
        if (q > hi || q < lo) { q = p; const int back = -dir; for (int k = 0; k < std::abs (steps); ++k) { int r = q + back; for (int g = 0; g < 4 && ! inPool (c, r, t); ++g) r += back; q = r; } }
        return clampv (q, lo, hi); };
    int prev = -1; bool leapUsedHalf[2] { R() < 0.5, R() < 0.5 };
    for (size_t i = 0; i < hits.size(); ++i)
    {
        FHit& h = hits[i]; const int t = h.t; const int wantP = (int) std::lround (want (t));
        if (h.con == 'a' || h.con == 'b') continue;   // (filled from the next T below)
        int p;
        if (prev < 0) p = h.con == 'T' ? chordNear (wantP, t) : poolNear (wantP, t);
        else if (h.con == 'T')
        {   // the held note: the chord tone nearest the contour, reached by a step or a third from the note before; one real leap
            // (a 4th .. a 6th) per half, the expressive one, then the line turns back
            const bool leapNow = ! leapUsedHalf[h.bar / 2] && std::abs (wantP - prev) >= 5 && R() < 0.6;
            const int reach = leapNow ? 7 : 4;
            const int toward = prev + clampv (wantP - prev, -reach, reach) + (R() < 0.3 ? (R() < 0.5 ? 1 : -1) : 0);
            p = chordNear (toward, t);
            if (p == prev && R() < 0.8) { const int q = chordNear (prev + (wantP >= prev ? 3 : -3), t); if (q != prev && std::abs (q - prev) <= reach + 2) p = q; }
            if (std::abs (p - prev) > reach + 1) p = chordNear (prev + (p > prev ? reach : -reach), t);
            if (std::abs (p - prev) > reach + 1) p = clampv (stepPool (prev, p > prev ? 2 : -2, t), lo, hi);
            if (std::abs (p - prev) >= 5) leapUsedHalf[h.bar / 2] = true;
        }
        else if (h.con == 'r') p = prev;
        else if (h.con == 'o') p = prev + 12 <= hi ? prev + 12 : prev - 12;
        else
        {
            int st = h.con == 'u' ? 1 : h.con == 'd' ? -1 : (wantP >= prev ? 1 : -1) * (R() < 0.25 ? -1 : 1);
            if (R() < 0.25) st *= 2;   // a third now and then
            p = stepPool (prev, st, t);
            if (h.len >= 2 * U && c.I.H.clash (p, t)) p = chordNear (p, t);
        }
        if (h.endOfHalf)
        {
            const int k1 = mod12 (c.I.H.key), k5 = mod12 (c.I.H.key + 7);
            if (h.bar < 2) p = nearestWhere (p, lo, hi, [&] (int q) { const int d = mod12 (q - c.I.H.key); return (d == 2 || d == 5 || d == 10 || d == 3 || d == 7) && inPool (c, q, t) && ! c.I.H.clash (q, t); });   // the call stays open
            else p = nearestWhere (p, lo, hi, [&] (int q) { return mod12 (q) == k1 || mod12 (q) == k5; });   // the answer lands
        }
        p = clampv (p, lo - 2, hi + 2);
        if (! inPool (c, p, t)) p = poolNear (p, t);
        h.p = prev = p;
    }
    for (size_t i = 0; i < hits.size(); ++i)
    {   // the ornaments before a held note: a / b from the T they lead into (a run of b's climbs into it)
        if (hits[i].con != 'a' && hits[i].con != 'b') continue;
        size_t j = i; while (j < hits.size() && (hits[j].con == 'a' || hits[j].con == 'b')) ++j;
        const int tgt = j < hits.size() ? hits[j].p : (i > 0 ? hits[i - 1].p : (lo + hi) / 2);
        int na = 0, nb = 0;
        for (size_t k = j; k-- > i;) { const int off = hits[k].con == 'a' ? ++na : -(++nb); hits[k].p = stepPool (tgt, off, hits[k].t); }
        i = j;
    }
    if (hypno)
    {   // HYPNOTIC: bars 2-4 play bar 1's pitches again (the loop), bar 4 with one small change
        std::vector<int> b0; for (auto& h : hits) if (h.bar == 0) b0.push_back (h.p);
        for (int b = 1; b < 4; ++b) { size_t k = 0; for (auto& h : hits) if (h.bar == b && k < b0.size()) h.p = b0[k++]; }
        for (size_t i = hits.size(); i-- > 0;) if (hits[i].bar == 3) { if (R() < 0.5) hits[i].p = stepPool (hits[i].p, R() < 0.5 ? 1 : -1, hits[i].t); else if (i > 0 && hits[i - 1].bar == 3) hits.erase (hits.begin() + (long) i - 1); break; }
    }
    // ---- notes: near-legato inside a gesture, held notes long; stabs short
    std::vector<Note> phrase;
    for (size_t i = 0; i < hits.size(); ++i)
    {
        const FHit& h = hits[i]; Note n; n.t = h.t; n.p = h.p;
        const bool orn = h.con == 'a' || h.con == 'b', stab = h.con == 'r' || (h.len <= U && ! orn);
        n.d = std::max (6, h.len - (orn ? 2 : stab ? 6 : h.len >= 4 * U ? 10 : 4));
        n.v = h.con == 'T' ? (h.t % BAR == 0 ? 0.86f : 0.8f) : orn ? 0.58f : h.con == 'r' ? 0.7f : 0.74f; n.acc = h.t % BAR == 0;
        phrase.push_back (n);
    }
    sortNotes (phrase);
    for (size_t i = 0; i + 1 < phrase.size(); ++i) if (phrase[i].t + phrase[i].d > phrase[i + 1].t) phrase[i].d = std::max (6, phrase[i + 1].t - phrase[i].t - 2);
    // ---- the flute's ornaments: a slide into a held note (p 0.3), a grace note a step (or a semitone) below it that scoops in
    // (p 0.25), a trill with the upper neighbour over the first half of a long note (p 0.18); DARKNESS: the b2 sigh in the answer
    Rng Ro (mix (s, 0xF17E5u));
    std::vector<Note> add;
    for (size_t i = 0; i < phrase.size(); ++i)
    {
        Note& n = phrase[i]; const FHit& h = hits[i];
        if (h.con != 'T') continue;
        if (i > 0 && phrase[i - 1].d > U && std::abs (phrase[i - 1].p - n.p) <= 7 && phrase[i - 1].p != n.p && n.t - (phrase[i - 1].t + phrase[i - 1].d) <= U && Ro() < 0.3)
        { phrase[i - 1].d = std::max (6, n.t - phrase[i - 1].t - 2); phrase[i - 1].l.type = 1; phrase[i - 1].l.w = 0.72f; continue; }
        if (n.t >= PPQ / 8 && (i == 0 || phrase[i - 1].t + phrase[i - 1].d <= n.t - PPQ / 8) && Ro() < 0.35)
        {   // the grace: a 1/32 before, a scale step below (or a semitone: the chromatic scoop), gliding into the note
            Note gr = n; gr.t = n.t - PPQ / 8; gr.d = PPQ / 8 - 2; gr.p = (dk >= 0.5 && Ro() < 0.5) ? n.p - 1 : stepPool (n.p, -1, n.t);   // (the chromatic scoop only on the dark side) gr.v = 0.55f; gr.acc = false; gr.l.type = 1; gr.l.w = 0.6f;
            if (gr.p != n.p) add.push_back (gr);
            continue;
        }
        if (h.len >= 8 * U && Ro() < 0.22)
        {   // the trill: main / upper neighbour at 1/16 over the first half, then the note held
            const int up = stepPool (n.p, 1, n.t); if (up == n.p) continue;
            const int sub = U, k = std::max (4, (h.len / 2) / sub); const int d0 = n.d;
            for (int q = 1; q < k; ++q) { Note tr = n; tr.t = n.t + q * sub; tr.d = sub - 2; tr.p = q % 2 == 1 ? up : n.p; tr.v = 0.66f; tr.acc = false; add.push_back (tr); }
            n.d = sub - 2;
            Note tail = n; tail.t = n.t + k * sub; tail.d = std::max (6, d0 - k * sub); tail.v = 0.78f; tail.acc = false; add.push_back (tail);
        }
    }
    phrase.insert (phrase.end(), add.begin(), add.end()); sortNotes (phrase);
    if (dk >= 0.5 && Ro() < 0.3 + 0.7 * dk) b2Hit (phrase, c, 2 * barT, phraseT);   // DARKNESS: the b2 that falls back to the root (the answer)
    if (dk >= 0.5 && Ro() < dk * 0.9)
    {   // DARKNESS: a b5 grace falling into a held 5 (one per phrase), as the shipped writer does
        for (size_t i = 0; i < phrase.size(); ++i)
        {
            const auto& tg = phrase[i]; if (mod12 (tg.p - I.H.key) != 7 || tg.t < PPQ / 8 || tg.d < PPQ / 4) continue;
            bool clear = true; for (auto& n : phrase) if (&n != &tg && n.t >= tg.t - PPQ / 8 && n.t < tg.t) clear = false;
            if (! clear) continue;
            for (auto& n : phrase) if (n.t < tg.t && n.t + n.d > tg.t - PPQ / 8) { n.d = std::max (6, tg.t - PPQ / 8 - n.t - 1); n.l = Link {}; }
            Note g = tg; g.t = tg.t - PPQ / 8; g.d = PPQ / 8 - 2; g.p = tg.p - 1; g.v = 0.55f; g.l.type = 1; g.l.w = 0.6f; g.acc = false;
            phrase.push_back (g); sortNotes (phrase); break;
        }
    }
    for (size_t i = 0; i + 1 < phrase.size(); ++i) if (phrase[i].t + phrase[i].d > phrase[i + 1].t && phrase[i].l.type != 1) phrase[i].d = std::max (4, phrase[i + 1].t - phrase[i].t - 1);
    // ---- over the idea: the phrase again (refit to the chords under it); every other time the ending resolves to the root and
    // the longest held note of the answer gets a trill, or the call's second bar answers an octave up
    std::vector<Note> out;
    for (int t = 0, k = 0; t < c.total; t += phraseT, ++k)
    {
        std::vector<Note> rep; for (auto n : phrase) { n.t += t; if (n.t < c.total) { n.d = std::min (n.d, c.total - n.t); rep.push_back (n); } }
        if (k > 0 && ! rep.empty())
        {
            Rng Rk (mix (s, (uint32_t) k * 104729u + 0xF1u));
            refitPhrase (rep, c, lo, hi);
            if (k % 2 == 1)
            {
                for (size_t i = rep.size(); i-- > 0;) if (rep[i].d >= 2 * U && rep[i].t >= t + 2 * barT) { rep[i].p = nearestWhere (rep[i].p, lo, hi, [&] (int q) { return mod12 (q) == mod12 (c.I.H.key); }); break; }
                if (Rk() < 0.5) octaveJump (rep, t + barT, t + 2 * barT, Rk, hi);
            }
        }
        out.insert (out.end(), rep.begin(), rep.end());
    }
    sortNotes (out);
    for (size_t i = 0; i + 1 < out.size(); ++i) if (out[i].t + out[i].d > out[i + 1].t && out[i].l.type != 1) out[i].d = std::max (4, out[i + 1].t - out[i].t - 1);
    for (size_t i = 0; i < out.size(); ++i)
    {   // key guard: outside the key / chord only as a short step into the next note (grace, b2)
        auto& n = out[i]; n.p = clampv (n.p, 36, 96);
        if (inPool (c, n.p, n.t)) continue;
        const bool step = n.d <= PPQ / 2 + 2 && i + 1 < out.size() && out[i + 1].t - n.t <= PPQ && std::abs (out[i + 1].p - n.p) <= 2;
        if (! step) n.p = nearestWhere (n.p, 36, 96, [&] (int q) { return inPool (c, q, n.t); });
    }
    return out;
}
} // namespace
const char* fluteGestName (int i) { const auto& G = fluteGests(); return i >= 0 && i < (int) G.size() ? G[(size_t) i].name : ""; }
int fluteGestCount() { return (int) fluteGests().size(); }

std::vector<Note> urbanMelody (const Idea& I, int mood, uint32_t seed, int* archOut)
{
    if (mood == Trap && flavourFor (I, mood) == kEmo) return emoMelody (I, seed, archOut);
    if (mood == Trap && I.melVer >= 1 && flavourFor (I, mood) <= 3) return trapFluteMelody (I, seed, archOut);   // 3.2.1: DARK / TRIUMPHANT / HYPNOTIC / DRILL sing (RATCHET keeps its stabs)
    const Ctx c = makeCtx (I, mood);
    const auto& M = melOf (mood); const auto& F = c.F;
    // the motif choice is seeded from the whole idea (key, progression, flavour, tempo), not only the first bar
    uint32_t s = mix (seed, (uint32_t) (I.H.key * 131 + I.g.bpm * 7 + I.flavourUsed * 9973));
    for (auto& ch : I.H.chords) s = mix (s, (uint32_t) (ch.root * 31 + (ch.tEnd - ch.t)));
    Rng R (s);
    // archetype: mostly the flavour's own, sometimes any of the mood's (a wider pool), sometimes two halves crossed
    int ai = R() < 0.72 ? pickV (R, F.mel) : irand (R, (int) M.size());
    if (mood == Trap && (ai == 4 || ai == 5) && std::find (F.mel.begin(), F.mel.end(), ai) == F.mel.end()) ai = pickV (R, F.mel);   // the busy 16th figures only where the flavour asks
    if (archOut) *archOut = ai;
    std::string grid = M[(size_t) ai].grid;
    const int barSteps = BAR / c.U;
    if ((int) grid.size() < barSteps) { std::string g; while ((int) g.size() < barSteps) g += grid; grid = g.substr (0, (size_t) barSteps); }
    if ((int) grid.size() == barSteps && R() < 0.3)
    {   // crossover: the second half from another archetype of the mood
        const std::string other = M[(size_t) irand (R, (int) M.size())].grid;
        if ((int) other.size() >= barSteps) grid = grid.substr (0, (size_t) barSteps / 2) + other.substr ((size_t) barSteps / 2, (size_t) barSteps / 2);
    }
    {   // the variety axes on the rhythm: density (thinner / busier), where the motif starts (on 1 / on the 'and' / after a rest)
        const double u = R();
        std::vector<int> xs, rs; for (int i = 1; i < (int) grid.size(); ++i) { if (grid[(size_t) i] == 'X') xs.push_back (i); else if (grid[(size_t) i] == '.' && grid[(size_t) i - 1] == '.') rs.push_back (i); }
        const bool sparse = mood == Trap && flavourFor (I, mood) == 0;   // DARK: fewer notes
        const double thinP = 0.25 + 0.45 * std::max (0.0, darknessOf (I, mood) / 100.0 - 0.3) + (sparse ? 0.2 : 0.0);   // DARKNESS: sparser
        if (u < thinP && xs.size() > 3) { const int i = pickV (R, xs); grid[(size_t) i] = '.'; for (int k = i + 1; k < (int) grid.size() && grid[(size_t) k] == '-'; ++k) grid[(size_t) k] = '.'; }   // thinner
        else if (u < 0.5 && ! rs.empty() && mood != Trap) { const int i = pickV (R, rs); grid[(size_t) i] = 'X'; }                                                                       // busier (not TRAP: the flute leaves space)
        const double v = R();
        if (v < 0.3 && grid[0] == 'X')
        {   // start late: the motif shifted one or two 1/16s (a pickup feel)
            const int sh = R() < 0.6 ? 1 : 2; std::string g2 (grid.size(), '.');
            for (int i = 0; i < (int) grid.size(); ++i) g2[(size_t) ((i + sh) % (int) grid.size())] = grid[(size_t) i];
            if (g2[0] == '-' || g2[0] == '/' || g2[0] == '\\') g2[0] = '.';
            grid = g2;
        }
    }
    if (R() < 0.35)
    {   // nudge one hit by a 1/16 (a rest moved)
        std::vector<int> xs; for (int i = 1; i + 1 < (int) grid.size(); ++i) if (grid[(size_t) i] == 'X' && grid[(size_t) i + 1] == '.') xs.push_back (i);
        if (! xs.empty()) { const int i = pickV (R, xs); grid[(size_t) i] = '.'; grid[(size_t) i + 1] = 'X'; }
    }
    static const int regShift[] { 0, 0, -5, 5, 7, -3 }, regShiftTrap[] { 0, 0, -3, 3, -5, 2 };   // the register axis (TRAP: the flute stays low)
    const int rs = mood == Trap ? regShiftTrap[irand (R, 6)] : regShift[irand (R, 6)];
    // DARKNESS: above the mood's default the melody goes down (an octave at 100), below it up a little (a fourth at 0)
    const double dk = darknessOf (I, mood) / 100.0, d0 = defaultDarkness (mood) / 100.0;
    const int dShift = dk >= d0 ? -(int) std::lround (12.0 * (dk - d0) / std::max (0.01, 1.0 - d0)) : (int) std::lround (5.0 * (d0 - dk) / d0);
    const int lo = std::max (45, F.regLo + rs + dShift + 12 * clampv (I.reg, -1, 1)), hi = std::max (57, F.regHi + rs + dShift + 12 * clampv (I.reg, -1, 1));
    const bool loopy = mood == HipHop || (mood == Trap && flavourFor (I, mood) == 2);   // HIP HOP + TRAP HYPNOTIC: the bar loops with micro-variation
    const int shape = mood == Trap && flavourFor (I, mood) == 2 ? (R() < 0.6 ? 3 : 0) : irand (R, 5);   // (HYPNOTIC cells: repeated notes / steps)
    static const int firstIvs[] { 0, 1, -1, 2, -2, 3, -3, 12, 99 };
    const int firstIv = firstIvs[irand (R, 9)];
    int hero = mood == Trap ? irand (R, 6) : irand (R, 4);
    Rng Rdk (mix (s, 0xDA4Cu));
    if (mood == Trap && Rdk() < 0.55 * dk) hero = 5;   // DARKNESS: the b2 more often
    // the phrase: call (bar 1) + answer (bar 2) for a one-bar archetype; the archetype as written when it is longer
    const int archBars = std::max (1, (int) grid.size() / barSteps), barT = BAR * c.U / 24;
    const int phraseBars = mood == Trap && ! loopy ? std::max (2, archBars) : (archBars >= 4 ? 4 : R() < 0.5 ? 2 : 4);
    const int phraseT = std::min (c.total, phraseBars * barT);
    std::vector<Note> phrase; int prevP = -1;
    for (int b = 0; b < phraseBars; ++b)
    {
        const int t0 = b * barT; if (t0 >= phraseT) break;
        const std::string bar = grid.substr ((size_t) ((b % archBars) * barSteps), (size_t) barSteps);
        auto hits = parseGrid (bar);
        Rng Rb (mix (s, (uint32_t) b * 7717u));
        const bool answer = b % 2 == 1;
        if (loopy && b > 0 && archBars == 1)
        {   // loop-friendly: the same bar again (same pitches), the last bar of the phrase with one small change
            std::vector<Note> rep; for (auto& n : phrase) if (n.t < barT) { Note x = n; x.t += t0; rep.push_back (x); }
            if (b == phraseBars - 1 && ! rep.empty())
            {
                const int what = irand (Rb, 3);
                if (what == 0) rep.back().p = stepInPool (c, rep.back().p, Rb() < 0.5 ? 1 : -1, rep.back().t, lo, hi);   // a note bent
                else if (what == 1 && rep.size() > 2) rep.erase (rep.begin() + 1 + irand (Rb, (int) rep.size() - 1));     // a rest
                else { Note pk = rep.back(); pk.t = t0 + barT - c.U * 2; pk.d = c.U - 4; pk.p = stepInPool (c, phrase.front().p, -1, pk.t, lo, hi); pk.l = Link {};   // a pickup
                       bool clear = true; for (auto& n : rep) if (n.t >= pk.t - c.U / 2) clear = false; if (clear) rep.push_back (pk); }
            }
            phrase.insert (phrase.end(), rep.begin(), rep.end());
            continue;
        }
        if (mood == HipHop && b > 0 && archBars > 1)
        {   // a written multi-bar loop: bars that repeat the first bar's rhythm repeat its pitches; a changed bar keeps the head
            const std::string first = grid.substr (0, (size_t) barSteps);
            std::vector<Note> b0; for (auto& n : phrase) if (n.t < barT) b0.push_back (n);
            if (bar == first) { for (auto n : b0) { n.t += t0; phrase.push_back (n); } continue; }
            int pp = prevP; auto ns = pitchHits (c, hits, t0, Rb, shape, 99, lo, hi, pp, true, false); prevP = pp;
            for (size_t k = 0; k + 1 < ns.size() && k < b0.size() && k < 2; ++k) if (b0[k].t + t0 == ns[k].t) ns[k].p = b0[k].p;
            phrase.insert (phrase.end(), ns.begin(), ns.end());
            continue;
        }
        if (mood == Trap && answer && archBars == 1)
        {   // the answer: the call's rhythm, a new ending (resolved), sometimes the last beat changed
            if (Rb() < 0.4 && hits.size() > 2) { hits.back().step = std::min (barSteps - 1, hits.back().step + (Rb() < 0.5 ? 1 : 2)); }
        }
        auto ns = pitchHits (c, hits, t0, Rb, shape, b == 0 ? firstIv : 99, lo, hi, prevP, answer || b == phraseBars - 1, ! answer && b + 1 < phraseBars);
        if (mood == Trap && answer && archBars == 1 && ! phrase.empty())
        {   // keep the call's first notes (call and response share the head)
            std::vector<Note> head; for (auto& n : phrase) if (n.t >= (b - 1) * barT && n.t < (b - 1) * barT + barT / 2) head.push_back (n);
            for (size_t k = 0; k < ns.size() && k < head.size() && head[k].t + barT == ns[k].t; ++k) ns[k].p = head[k].p;
        }
        phrase.insert (phrase.end(), ns.begin(), ns.end());
    }
    sortNotes (phrase);
    refitPhrase (phrase, c, lo, hi);   // repeated bars over other chords: nothing rubs
    if (mood == Trap)
    {   // the flute phrasing: notes held into the rest after them (long, legato), some of those slide into the next note
        Rng Rl (mix (s, 0xF17Eu));
        for (size_t i = 0; i + 1 < phrase.size(); ++i)
        {
            auto& n = phrase[i]; const auto& nx = phrase[i + 1];
            if (n.d <= c.U / 2 + 2 || nx.d <= c.U / 2 + 2) continue;   // (rolls / graces stay short)
            if (nx.t - (n.t + n.d) >= c.U && Rl() < 0.55)
            {
                n.d = std::max (n.d, nx.t - n.t - 4);
                const int dp = std::abs (nx.p - n.p);
                if (dp > 0 && dp <= 7 && Rl() < 0.35) { n.l.type = 1; n.l.w = 0.72f; }
            }
        }
    }
    // the idea's one hero device (on the phrase)
    Rng Rh (mix (s, 0xDE71CEu));
    switch (hero)
    {
        case 1: octaveJump (phrase, phraseT / 2, phraseT, Rh, hi); break;
        case 2: addGrace (phrase, c, Rh); break;
        case 3: addTripletBeat (phrase, c, phraseT / 2, phraseT, Rh, lo, hi); break;
        case 4: longSlide (phrase, 0, phraseT); break;
        case 5: if (I.g.scale == "phrygian" || I.g.scale == "harmonic" || dk >= 0.5) b2Hit (phrase, c, 0, phraseT); else addGrace (phrase, c, Rh); break;
        default: break;
    }
    if (mood == Trap && Rdk() < dk * 0.8)
    {   // DARKNESS: a b5 grace falling into the 5 (one per phrase)
        for (size_t i = 0; i < phrase.size(); ++i)
        {
            const auto& tg = phrase[i]; if (mod12 (tg.p - I.H.key) != 7 || tg.t < PPQ / 8 || tg.d < PPQ / 4) continue;
            bool clear = true; for (auto& n : phrase) if (&n != &tg && n.t >= tg.t - PPQ / 8 && n.t < tg.t) clear = false;
            if (! clear) continue;
            for (auto& n : phrase) if (n.t < tg.t && n.t + n.d > tg.t - PPQ / 8) { n.d = std::max (6, tg.t - PPQ / 8 - n.t - 1); n.l = Link {}; }
            Note g = tg; g.t = tg.t - PPQ / 8; g.d = PPQ / 8 - 2; g.p = tg.p - 1; g.v = 0.55f; g.l = Link {}; g.acc = false;
            phrase.push_back (g); sortNotes (phrase); break;
        }
    }
    if (mood == HipHop && I.colour == "penta" && I.mode == mood)
    {   // the blue note: one passing b5 between the 4 and the 5
        for (size_t i = 0; i + 1 < phrase.size(); ++i)
        {
            const int a = mod12 (phrase[i].p - I.H.key), b = mod12 (phrase[i + 1].p - I.H.key);
            if (((a == 5 && b == 7) || (a == 7 && b == 5)) && phrase[i + 1].t - phrase[i].t >= c.U * 2 && std::abs (phrase[i + 1].p - phrase[i].p) == 2)
            { Note bn = phrase[i]; bn.t = phrase[i + 1].t - c.U / 2; bn.d = c.U / 2 - 2; bn.p = (phrase[i].p + phrase[i + 1].p) / 2; bn.v = 0.6f; bn.l = Link {}; bn.acc = false;
              phrase[i].d = std::min (phrase[i].d, bn.t - phrase[i].t - 1); phrase.push_back (bn); sortNotes (phrase); break; }
        }
    }
    for (size_t i = 0; i + 1 < phrase.size(); ++i) if (phrase[i].t + phrase[i].d > phrase[i + 1].t && phrase[i].l.type != 1) phrase[i].d = std::max (4, phrase[i + 1].t - phrase[i].t - 1);
    // over the whole idea: the phrase repeats (refit to the chords under it); TRAP repeats get rolls / octave answers
    std::vector<Note> out;
    for (int t = 0, k = 0; t < c.total; t += phraseT, ++k)
    {
        std::vector<Note> rep; for (auto n : phrase) { n.t += t; if (n.t < c.total) { n.d = std::min (n.d, c.total - n.t); rep.push_back (n); } }
        if (k > 0)
        {
            Rng Rk (mix (s, (uint32_t) k * 104729u));
            refitPhrase (rep, c, lo, hi);
            if (mood == Trap)
            {
                const double rollP = hero == 0 ? 0.85 : 0.5;
                if (Rk() < rollP) addRoll (rep, t, t + phraseT, Rk, Rk() < 0.4);   // 1/32 or 1/16-triplet rolls on the repeats
                if (k % 2 == 1 && Rk() < 0.35) octaveJump (rep, t + phraseT / 2, t + phraseT, Rk, hi);
            }
            else if (k % 2 == 1 && Rk() < 0.5 && ! rep.empty())
            {   // HIP HOP: every other loop one small variation (a rest moved / a note bent)
                auto& n = rep[(size_t) irand (Rk, (int) rep.size())];
                if (Rk() < 0.5) n.p = stepInPool (c, n.p, Rk() < 0.5 ? 1 : -1, n.t, lo, hi);
                else { const int nt = n.t + c.U; bool clear = true; for (auto& m : rep) if (&m != &n && std::abs (m.t - nt) < c.U) clear = false; if (clear && nt < c.total) { n.t = nt; n.d = std::max (6, n.d - c.U); } }
                sortNotes (rep);
            }
        }
        else if (mood == Trap && hero == 0) addRoll (rep, phraseT / 2, phraseT, Rh, false);
        out.insert (out.end(), rep.begin(), rep.end());
    }
    sortNotes (out);
    for (size_t i = 0; i + 1 < out.size(); ++i) if (out[i].t + out[i].d > out[i + 1].t && out[i].l.type != 1) out[i].d = std::max (4, out[i + 1].t - out[i].t - 1);
    for (size_t i = 0; i < out.size(); ++i)
    {   // key guard: outside the key / chord only as a short step into the next note (grace, blue note, b2)
        auto& n = out[i]; n.p = clampv (n.p, 36, 96);
        if (inPool (c, n.p, n.t)) continue;
        const bool step = n.d <= PPQ / 2 + 2 && i + 1 < out.size() && out[i + 1].t - n.t <= PPQ && std::abs (out[i + 1].p - n.p) <= 2;
        if (! step) n.p = nearestWhere (n.p, 36, 96, [&] (int q) { return inPool (c, q, n.t); });
    }
    return out;
}

// ================================================================= ARP
std::vector<Note> urbanArp (const Idea& I, int mood, uint32_t seed, const std::vector<Note>& melody)
{
    if (mood == Trap && flavourFor (I, mood) == kEmo) return emoArp (I, seed);   // the guitar bed: a loop under the melody
    const Ctx c = makeCtx (I, mood);
    Rng R (mix (seed, 0xA4F1u + (uint32_t) mood));
    const int st = c.U, steps = c.total / st;
    // the melody's rests on the grid (a step after each note's end stays free)
    std::vector<bool> busy ((size_t) steps + 2, false);
    for (auto& n : melody) { const int a = std::max (0, n.t / st), b = std::min (steps, (n.t + n.d) / st + 1); for (int k = a; k <= b && k < steps; ++k) busy[(size_t) k] = true; }
    std::vector<Note> out;
    const int lo = mood == Trap ? 69 : 67, hi = mood == Trap ? 88 : 84;
    // dark arpeggio shapes (chord-tone ladder indices; 3 = the octave)
    static const std::vector<std::vector<int>> shapes { { 0, 1, 2, 3 }, { 0, 2, 3, 2 }, { 3, 2, 1, 0 }, { 0, 1, 2, 1 }, { 0, 2, 1, 3 } };
    const auto& shp = shapes[(size_t) irand (R, (int) shapes.size())];
    auto ladder = [&] (int t) { const auto& ch = I.H.at (t); std::vector<int> v; for (int p = lo; p <= hi + 12 && v.size() < 6; ++p) if (has (ch.pcs, mod12 (p))) v.push_back (p); return v; };
    int bar = -1, usedInBar = 0;
    for (int k = 0; k < steps;)
    {
        if (busy[(size_t) k]) { ++k; continue; }
        int e = k; while (e < steps && ! busy[(size_t) e] && (e == k || (e * st) % BAR != 0)) ++e;   // a rest (cut at the bar line)
        const int len = e - k; const int b = (k * st) / BAR;
        if (b != bar) { bar = b; usedInBar = 0; }
        if (len >= 3 && usedInBar < 2 && (mood == Trap || R() < 0.55))
        {
            const int t0 = (k + 1) * st, t1 = e * st;   // the first rest step stays empty
            const auto lad = ladder (t0); if (lad.size() < 2) { k = e; continue; }
            if (mood == Trap)
            {
                const int kind = R() < 0.55 ? 0 : R() < 0.6 ? 1 : 2;   // 16ths, 1/16 triplets, a 1/32 roll into the next melody note
                const int sub = kind == 0 ? st : kind == 1 ? st * 2 / 3 : st / 2;
                const int from = kind == 2 ? std::max (t0, t1 - st * 2) : t0;
                int j = 0;
                for (int t = from; t + sub <= t1; t += sub, ++j)
                {
                    Note n; n.t = t; n.d = std::max (4, sub - 4); n.p = lad[(size_t) std::min ((int) lad.size() - 1, shp[(size_t) (j % (int) shp.size())])];
                    if (kind == 2) n.p = lad[(size_t) std::min ((int) lad.size() - 1, j / 2)];
                    n.v = kind == 2 ? clampv (0.42f + 0.06f * (float) j, 0.3f, 0.75f) : (t % PPQ == 0 ? 0.62f : 0.5f);
                    out.push_back (n);
                }
            }
            else
            {   // HIP HOP: a short answer in the gap (2-3 notes, chord tones / the blue note), off the beat
                const int n3 = 2 + irand (R, 2); int p = lad[(size_t) irand (R, (int) lad.size())];
                for (int j = 0; j < n3; ++j)
                {
                    const int t = t0 + j * st * 2; if (t + st > t1) break;
                    Note n; n.t = t; n.d = st * 2 - 6; n.p = p; n.v = 0.55f; out.push_back (n);
                    p = stepInPool (c, p, R() < 0.6 ? -1 : 1, t, lo, hi);
                }
            }
            ++usedInBar;
        }
        k = e;
    }
    sortNotes (out);
    for (auto& n : out) if (! I.H.chordTone (n.p, n.t) && I.H.clash (n.p, n.t)) n.p = chordToneNear (c, n.p, n.t, lo, hi + 12);
    return out;
}

// ================================================================= BASS
std::vector<Note> urbanBass (const Idea& I, int mood, uint32_t seed, int* archOut)
{
    if (mood == Trap && flavourFor (I, mood) == kEmo) return emoBass (I, seed, archOut);
    const Ctx c = makeCtx (I, mood); const auto& F = c.F; const auto& B = bassOf (mood);
    uint32_t s = mix (seed, 0xBA55u + (uint32_t) I.H.key * 17u);
    for (auto& ch : I.H.chords) s = mix (s, (uint32_t) ch.root);
    Rng R (s);
    int ai = R() < 0.8 ? pickV (R, F.bass) : irand (R, (int) B.size());
    if (archOut) *archOut = ai;
    const BassArch& A = B[(size_t) clampv (ai, 0, (int) B.size() - 1)];
    const auto hits = parseGrid (A.grid); const auto toks = split (A.pitches, ' ');
    const int archT = gridSteps (A.grid) * c.U;
    // the home register: TRAP 808 roots at C1..B1, HIP HOP sub at E1..D#2
    const int homeLo = mood == Trap ? 24 : 28;
    auto rootPitch = [&] (int pc) { int p = homeLo + mod12 (pc - homeLo); return p; };
    const int keyHome = rootPitch (I.H.key);
    std::vector<Note> out;
    const int bars = c.total / std::max (1, archT);
    for (int rep = 0; rep * archT < c.total; ++rep)
    {
        const int base = rep * archT;
        for (size_t i = 0; i < hits.size(); ++i)
        {
            const Hit& h = hits[i]; const int t = base + h.step * c.U; if (t >= c.total) break;
            const auto& ch = I.H.at (t);
            int r = rootPitch (ch.root); if (r - keyHome > 6) r -= 12; if (keyHome - r > 5) r += 12;   // follow the chord, close to home
            std::string tk = i < toks.size() ? toks[i] : "1";
            int off = 0; char sign = 0; if (! tk.empty() && (tk.back() == '+' || tk.back() == '-')) { sign = tk.back(); tk.pop_back(); }
            off = degSemis (tk);
            if (sign == '-') off -= 12;
            else if (sign == 0 && (tk == "b6" || tk == "6" || tk == "b7" || tk == "7")) off -= 12;
            int p = r + off; while (p < 24) p += 12; while (p > 50) p -= 12;
            Note n; n.t = t; n.p = p;
            n.d = std::max (6, h.len * c.U - (h.len <= 1 ? 6 : 3));
            n.v = (h.step * c.U) % PPQ == 0 ? 0.9f : 0.78f; n.acc = (h.step * c.U) % BAR == 0;
            if (h.slide) { n.l.type = 1; n.l.w = 0.9f; }
            if (h.roll) { const int half = c.U / 2; for (int k = 0; k < 2; ++k) { Note x = n; x.t = t + k * half; x.d = half - 2; x.v = 0.7f + 0.08f * (float) k; x.l = Link {}; out.push_back (x); } }
            else out.push_back (n);
        }
    }
    sortNotes (out);
    // phrase ends: the last hit drops an octave (or rolls into the downbeat); TRAP DARK / DRILL: sometimes half a bar of silence
    Rng Re (mix (s, 0xE9Du));
    if (! out.empty() && bars >= 2)
    {
        const double drop = mood == Trap ? 0.45 : 0.25;
        if (Re() < drop) { auto& L = out.back(); if (L.p - 12 >= 24 && ! L.l.type) L.p -= 12; }
        if (mood == Trap && (flavourFor (I, mood) == 0 || flavourFor (I, mood) == 3) && Re() < 0.25 && c.total >= 4 * BAR)
        {
            const int cut = c.total - BAR / 2;
            out.erase (std::remove_if (out.begin(), out.end(), [&] (const Note& n) { return n.t >= cut; }), out.end());
            for (auto& n : out) if (n.t + n.d > cut) { n.d = std::max (6, cut - n.t); n.l = Link {}; }
        }
    }
    {   // DARKNESS: longer 808 sustains (a note rings into the gap after it)
        const double dk = darknessOf (I, mood) / 100.0; Rng Rs (mix (s, 0x5057u));
        for (size_t i = 0; i + 1 < out.size(); ++i)
        {
            auto& n = out[i]; if (n.l.type == 1) continue;
            const int gap = out[i + 1].t - (n.t + n.d);
            if (gap >= 2 * c.U && Rs() < dk * 0.8) n.d = out[i + 1].t - n.t - 4;
        }
    }
    // slides: the glide starts ~70 ms before the next hit (an 808 slide is a short portamento into the note), never into itself
    for (size_t i = 0; i < out.size(); ++i)
    {
        auto& n = out[i];
        if (n.l.type != 1) continue;
        if (i + 1 >= out.size() || out[i + 1].p == n.p) { n.l = Link {}; continue; }
        const int gap = out[i + 1].t - n.t; const double g = (A.slow ? 0.18 : 0.07) * (0.6 + 0.8 * darknessOf (I, mood) / 100.0) * c.tps;   // DARKNESS: slower slides
        n.l.w = (float) clampv (1.0 - g / std::max (1, gap), 0.3, 0.97);
        n.d = std::max (n.d, gap - 2);
    }
    for (size_t i = 0; i + 1 < out.size(); ++i) if (out[i].l.type != 1 && out[i].t + out[i].d > out[i + 1].t) out[i].d = std::max (4, out[i + 1].t - out[i].t - 2);   // mono
    return out;
}

// ================================================================= EMO (TRAP's sixth flavour)
// Felt-tempo grids: 16 steps = one felt bar = 2 bars at the session tempo (140..170 half-time = 70..85 felt); one step = 48 ticks.
namespace
{
constexpr int EU = 48;                 // one felt 1/16
constexpr int EBAR = kEmoFeltBar;      // one felt bar
struct EmoProg { const char* name; const char* text; int start; bool minorSide; };
// tokens "degree:quality:felt beats[:bass degree]", degrees in the MAJOR key (1 = I, 6 = vi; the loops start on vi / IV, or on I
// only on the bright side), no V -> I anywhere (wrap included), loops end open (V, IV, iii, iv)
const std::vector<EmoProg>& emoProgs()
{
    static const std::vector<EmoProg> P {
        { "vi(add9)-IVmaj7-I-Vsus4 V", "6:madd9:4 4:M7:4 1:add9:4 5:sus4:2 5:M:2", 0, false },              // P1 the default sad loop
        { "i-VI-III-v",                "6:m:4 4:M:4 1:M:4 3:m:4", 0, true },                                 // P2 minor v keeps it from lifting
        { "I-V/7-vi-IV",               "1:add9:4 5:M:4:7 6:m7:4 4:M7:4", 2, false },                         // P3 descending bass 1-7-6-4
        { "vi-V-IV-iv",                "6:m:4 5:M:4 4:M:4 4:m:4", 0, true },                                 // P4 the borrowed iv
        { "IV-I/3-vi-V",               "4:M7:4 1:M:4:3 6:m7:4 5:sus4:2 5:M:2", 1, false },                   // P5 off-tonic, floats
        { "i-VII-VI-VI / i-VII-iv-v",  "6:madd9:4 5:M:4 4:M7:8 6:m:4 5:M:4 2:m7:4 3:m:4", 0, true },        // P6 two-bar hold, darker answer
        { "vi-iii-IV-I",               "6:m:4 3:m:4 4:M:4 1:M:4", 0, false },                                // P7 nostalgic
        { "i(add9)-VImaj7-iv7-V",      "6:madd9:4 4:M7:4 2:m7:4 3:M:4", 0, true },                           // P8 the harmonic-minor V lift
        { "IV-V-vi-vi7",               "4:add9:4 5:M:4 6:m:4 6:m7:4", 1, false },                            // P9 climbs to the sad chord, stays
        { "vi-IV-I-iii",               "6:m:4 4:M7:4 1:M:4 3:m:4", 0, false },                               // P10 ends on iii
        { "I5-vi5-iii5-IV5",           "1:5:4 6:5:4 3:5:4 4:5:4", 2, false } };                              // P11 power chords (hook)
    return P;
}
// GA: the fingerpicked guitar bed (1 felt bar; pitch tokens = chord tones: 1 root, 3 third, 5 fifth, 8 octave, 9 ninth, 10 the
// third an octave up, S the whole chord strummed, P a palm-muted power chord); reg = octaves up
struct EmoArp { const char* name; const char* grid; const char* pitches; int reg; double w; };
const std::vector<EmoArp>& emoArps()
{
    static const std::vector<EmoArp> A {
        { "GA1 rolling 8ths",        "X.X.X.X.X.X.X.X.", "1 5 8 9 10 9 8 5", 0, 1.2 },
        { "GA2 alternating thumb",   "X.XX.XX.X.XX.XX.", "1 10 8 9 10 5 10 8 9 10", 0, 1.0 },
        { "GA3 dotted lilt",         "X..X..X.X..X..X.", "1 5 9 1 5 10", 0, 1.0 },
        { "GA4 high-string shimmer", "XXXXXXXXXXXXXXXX", "5 8 9 8", 1, 0.5 },
        { "GA5 strum and pick",      "X.....X.X.X.....", "S 9 8 5", 0, 1.0 },
        { "GA7 suspension",          "X-------X-----X.", "9 8 5", 1, 0.8 },
        { "GA8 call and fade",       "X.X.X.X---------................", "1 5 8 10", 0, 0.7 },
        { "GA9 octave bell",         "X...X...X..X..X.", "1 8 5 8 5", 1, 0.9 },
        { "GA10 palm-mute push",     "X.X.X.X.X.X.X.X.", "P", 0, 0.4 } };
    return A;
}
// EB: the softer 808 (1 felt bar = 1 chord; tokens: 1 the chord's bass, 8 its octave, 5 its fifth, N the next chord's bass,
// w1 / w2 one / two scale steps down)
struct EmoBass { const char* name; const char* grid; const char* pitches; };
const std::vector<EmoBass>& emoBasses()
{
    static const std::vector<EmoBass> B {
        { "EB1 whole-note root",   "X---------------", "1" },
        { "EB2 root and push",     "X---------X-----", "1 1" },
        { "EB3 anticipate",        "X-------------X-", "1 N" },
        { "EB4 octave lift",       "X-------X-------", "1 8" },
        { "EB5 dotted heartbeat",  "X-----X-----....", "1 1" },
        { "EB6 walk down",         "X-------X---X---", "1 w1 w2" },
        { "EB7 sigh slide",        "X--------------/", "1" },
        { "EB8 half-time thump",   "X-----..X-----..", "1 1" },
        { "EB9 fifth pedal",       "X-------X-------", "1 5" },
        { "EB10 drop-in",          "........X-------", "1" } };
    return B;
}
// the melody's rhythm cells (1 felt bar) + the phrase endings (a long held note)
struct EmoCell { const char* name; const char* grid; };
const std::vector<EmoCell>& emoCells()
{
    static const std::vector<EmoCell> M {
        { "syllabic 8ths",   "X.X.X.X.X-X.X---" },
        { "dotted lilt",     "X..X..X.X..X.X--" },
        { "pickup cry",      "....X---X-X-X---" },
        { "late start",      "..X.X.X.X-X-X..." },
        { "push and hold",   "X-X.X..X--X.X---" },
        { "talking 8ths",    "X.X.X.X..XX.X..." },
        { "suspension",      "X-------X-X.X..." },
        { "call and fade",   "X.X.X.X-------.." } };
    return M;
}
const std::vector<EmoCell>& emoEnds()
{
    static const std::vector<EmoCell> E { { "held end", "X.X.X-----------" }, { "long end", "X---------------" }, { "falling end", "X.X.X.X---------" }, { "late held", "X-X.X-X---------" } };
    return E;
}
const std::map<std::string, int>& majorDeg() { static const std::map<std::string, int> D { { "1", 0 }, { "2", 2 }, { "3", 4 }, { "4", 5 }, { "5", 7 }, { "6", 9 }, { "7", 11 } }; return D; }
int majDeg (const std::string& d) { auto it = majorDeg().find (d); return it == majorDeg().end() ? 0 : it->second; }
std::vector<int> emoQuality (const std::string& q)
{
    if (q == "sus4") return { 0, 5, 7 };
    if (q == "sus2") return { 0, 2, 7 };
    if (q == "5") return { 0, 7 };
    return qualityIv (q);
}
bool emoMinorRep (const Idea& I) { return darknessOf (I, Trap) >= 50; }   // DARKNESS: >= 50 written as the relative minor (vi = i)
Rng emoRng (const Idea& I, uint32_t seed, uint32_t salt)
{
    uint32_t s = mix (seed, salt + (uint32_t) I.H.key * 131u + (uint32_t) I.g.bpm * 7u);
    for (auto& ch : I.H.chords) s = mix (s, (uint32_t) (ch.root * 31 + (ch.tEnd - ch.t)));
    return Rng (s);
}
int chordThird (const HChord& ch)   // semitones above the root: the 3rd (or the sus tone; 12 for a power chord)
{
    for (int x : { 3, 4 }) if (has (ch.pcs, mod12 (ch.root + x))) return x;
    for (int x : { 5, 2 }) if (has (ch.pcs, mod12 (ch.root + x))) return x;
    return 12;
}
int chordFifth (const HChord& ch) { for (int x : { 7, 6, 8 }) if (has (ch.pcs, mod12 (ch.root + x))) return x; return 7; }
int bassPc (const HChord& ch) { return mod12 (ch.bassNote); }
} // namespace

int emoArchFor (const Idea& I)
{   // the guitar bed's archetype: from the arp seed + the chords (stays when only the melody changes)
    Rng R = emoRng (I, I.g.arpSeed, 0x6A11u);
    std::vector<double> w; for (auto& a : emoArps()) w.push_back (a.w);
    return pickW (R, w);
}
int emoProgCount() { return (int) emoProgs().size(); }
const char* emoProgName (int i) { return emoProgs()[(size_t) clampv (i, 0, emoProgCount() - 1)].name; }
std::string emoProgText (int i) { return emoProgs()[(size_t) clampv (i, 0, emoProgCount() - 1)].text; }
int emoArpArchCount() { return (int) emoArps().size(); }
const char* emoArpArchName (int i) { return emoArps()[(size_t) clampv (i, 0, emoArpArchCount() - 1)].name; }
int emoBassArchCount() { return (int) emoBasses().size(); }
const char* emoBassName (int i) { return emoBasses()[(size_t) clampv (i, 0, emoBassArchCount() - 1)].name; }
const char* emoMelName (int i) { return emoCells()[(size_t) clampv (i, 0, (int) emoCells().size() - 1)].name; }
int emoMelArchCount() { return (int) emoCells().size(); }
int emoLead (const Idea& I) { return (mix (I.seedUsed, 0x1EADu) >> 8) % 100 < 35 ? 1 : 0; }
int trapAutoFlavour (uint32_t seedUsed)
{
    const double u = (double) (mix (seedUsed, 0xA070u) >> 8) / 16777216.0;
    return u < 0.40 ? 0 : u < 0.75 ? 3 : kEmo;
}
int rotationDistance (const RecentIdea& a, const RecentIdea& b)
{   // the drum axes come from the style's grammar (snare: beat 3 / displaced / bounce / beat 3 + big reverb; hats: 16ths + rolls /
    // 3-3-2 cells / stabs / straight 8ths felt) and the GROOVE; 808 + melody archetypes count as different across style tables
    auto snare = [] (int f) { return f == 3 ? 1 : f == 4 ? 2 : f == kEmo ? 3 : 0; };
    auto slideB = [] (int n) { return n <= 0 ? 0 : n == 1 ? 1 : n <= 3 ? 2 : 3; };
    const bool ea = a.flavour == kEmo, eb = b.flavour == kEmo;
    int d = 0;
    d += std::abs (a.bpm - b.bpm) >= 6;
    d += a.scale != b.scale;
    d += snare (a.flavour) != snare (b.flavour);
    d += snare (a.flavour) != snare (b.flavour) || std::abs (a.groove - b.groove) >= 0.05;
    d += ea != eb || a.bassArch != b.bassArch;
    d += slideB (a.slides) != slideB (b.slides);
    d += a.lead != b.lead;
    d += ea != eb || a.melArch != b.melArch;
    return d;
}

void emoPickProg (Idea& I, uint32_t seed, bool avoidCurrent)
{
    const auto& P = emoProgs();
    Rng R (seed ^ 0xE30u);
    const double dk = darknessOf (I, Trap) / 100.0; const bool minorRep = emoMinorRep (I);
    // DARKNESS: low = the bright, major-leaning loops (I / IV starts, the power-chord hook); high = the minor colours (v, iv, the
    // harmonic V, the darker 8-bar answer); the default sad loop always weighs in
    std::vector<double> w;
    for (size_t i = 0; i < P.size(); ++i)
    {
        double x = P[i].minorSide ? 0.4 + 1.4 * dk : P[i].start == 2 ? (minorRep ? 0.0 : 1.4 - 1.6 * dk) : P[i].start == 1 ? 1.25 - 0.6 * dk : 1.0;
        if (i == 0) x = std::max (x, 1.3);
        w.push_back (std::max (0.0, x));
    }
    int pi = pickW (R, w);
    for (int k = 0; k < 8 && avoidCurrent && pi == I.urbanProg; ++k) pi = pickW (R, w);
    I.urbanProg = pi;
    // written in the major key (minorRep: the same chords seen from vi, the key = vi)
    const int off = minorRep ? 3 : 0;
    std::vector<Slot> out;
    const auto sc = scaleOf (realScale (I.g.scale));
    for (auto& tok : split (P[(size_t) pi].text, ' '))
    {
        const auto parts = split (tok, ':');
        const std::string q = parts.size() > 1 ? parts[1] : "m"; const int beats = parts.size() > 2 ? std::max (1, std::atoi (parts[2].c_str())) : 4;
        Slot s; FixedChord fc; fc.root = mod12 (majDeg (parts[0]) + off); fc.iv = emoQuality (q);
        if (parts.size() > 3) fc.bass = mod12 (majDeg (parts[3]) + off);
        fc.chrom = false; for (int x : fc.iv) if (! has (sc, mod12 (fc.root + x))) fc.chrom = true;
        s.fixed = fc; s.tok = parts[0] + q; s.len = 2 * beats; s.r = R(); s.r2 = R();   // (a felt beat = 2 beats)
        out.push_back (s);
    }
    I.simpleProg = out; I.g.prog = out;
}
void emoSetup (Idea& I, Rng& R)
{
    I.doubleTime = false;
    I.g.bpm = 140 + irand (R, 31);                         // 140..170 half-time = 70..85 felt
    const double dk = darknessOf (I, Trap) / 100.0; (void) dk;
    const bool minorRep = emoMinorRep (I);
    I.colour.clear();
    I.g.scale = minorRep ? "minor" : "major";
    // guitar-friendly keys: E A D G major (or their relative minors C#m F#m Bm Em)
    static const int homes[] { 4, 9, 2, 7 };
    if (! I.words.empty() && parsePrompt (I.words).key) I.g.key = *parsePrompt (I.words).key;
    else { const int K = homes[irand (R, 4)]; I.g.key = minorRep ? mod12 (K + 9) : K; }
    I.groove = 0.05 + 0.05 * R();                          // straight 8ths (felt) with 5..10 % swing
    I.chordBars = 1; I.voicing = 0; I.chordRhythm = 0; I.simple = true;
    emoPickProg (I, I.seedUsed ^ 0x5EED5u, false);
    if (minorRep && I.urbanProg == 7) { I.g.scale = "harmonic"; for (auto& sl : I.simpleProg) { sl.fixed->chrom = false; for (int x : sl.fixed->iv) if (! has (scaleOf ("harmonic"), mod12 (sl.fixed->root + x))) sl.fixed->chrom = true; } I.g.prog = I.simpleProg; }
    I.phraseBars = 0; I.form = 0; I.stagger = 0; I.stutter = 0;
    I.arpSpb = 16; I.melSpb = 16;
}
void emoVoicings (Idea& I)
{   // SAD PAD voicings: open 5ths + a 9th (when it is in the key), the 3rd an octave up (never doubled), C3..C5, voice-led roots
    Rng R (mix (I.seedUsed, 0x0FE9u)); const bool ninth = R() < 0.7;
    int prevRoot = -1;
    for (auto& h : I.H.chords)
    {
        int r = 48 + mod12 (h.root - 48);
        if (prevRoot >= 0 && r - prevRoot > 6 && r - 12 >= 45) r -= 12;
        if (prevRoot >= 0 && prevRoot - r > 6 && r + 12 <= 57) r += 12;
        prevRoot = r;
        std::vector<int> v { r };
        const int th = chordThird (h), fi = chordFifth (h);
        if (has (h.pcs, mod12 (h.root + fi))) v.push_back (r + fi);
        for (int pc : h.pcs)
        {
            const int x = mod12 (pc - h.root); if (x == 0 || x == fi) continue;
            int q = r + x; if (x == th && x <= 5) q += 12; else if (x < 7) q += 12;   // 3rd / 9th up high, 7ths in the middle
            while (q > 76) q -= 12;
            if (! has (v, q)) v.push_back (q);
        }
        if (ninth && v.size() < 5 && has (I.H.keyPcs, mod12 (h.root + 2)) && ! has (h.pcs, mod12 (h.root + 2)) && th != 2) v.push_back (r + 14);
        std::sort (v.begin(), v.end()); v.erase (std::unique (v.begin(), v.end()), v.end());
        h.voicing = v;
    }
}
namespace
{
struct ECtx { const Idea& I; int total; std::vector<int> pool; double tps; };
ECtx emoCtx (const Idea& I)
{
    ECtx c { I, std::max (BAR, I.total), {}, std::max (40, I.g.bpm) / 60.0 * PPQ };
    for (int p : I.H.keyPcs) c.pool.push_back (p);
    if (c.pool.empty()) for (int s : scaleOf ("minor")) c.pool.push_back (mod12 (I.H.key + s));
    return c;
}
bool eIn (const ECtx& c, int p, int t) { return has (c.pool, mod12 (p)) || c.I.H.chordTone (p, t); }
int eStep (const ECtx& c, int p, int dir, int t)
{   // one scale step (the key + the chord under t)
    int q = p + dir; for (int g = 0; g < 4 && ! eIn (c, q, t); ++g) q += dir;
    return q;
}
int eNear (int p, int lo, int hi, const std::function<bool (int)>& ok) { return nearestWhere (p, lo, hi, ok); }
} // namespace

std::vector<Note> emoMelody (const Idea& I, uint32_t seed, int* archOut)
{
    const ECtx c = emoCtx (I);
    Rng R = emoRng (I, seed, 0x3E10u);
    const auto& C = emoCells(); const auto& E = emoEnds();
    const int ia = irand (R, (int) C.size()); int ib = irand (R, (int) C.size() - 1); if (ib >= ia) ++ib;
    const int ie = irand (R, (int) E.size()), form = irand (R, 3);
    if (archOut) *archOut = ia;
    const int pickups = R() < 0.85 ? 1 + irand (R, 3) : 0;   // 1-3 felt 8ths before the downbeat start most phrases
    // register: a 9th .. an 11th, centred around A3-E4 (DARKNESS: brighter = a few semitones up, darker = down)
    const double dk = darknessOf (I, Trap) / 100.0;
    const int range = 14 + irand (R, 4);
    int lo = 55 + (int) std::lround (4.0 - 7.0 * dk) + irand (R, 3) - 1 + 12 * clampv (I.reg, -1, 1); lo = clampv (lo, 45, 72);
    const int hi = lo + range;
    const int phraseT = 4 * EBAR;
    const double up = 0.42 + 0.2 * R();                      // the peak: bar 2-3
    const int peakTarget = lo + (R() < 0.55 ? 12 : 14), startT = lo + 5 + irand (R, 3), endT = lo + 2 + irand (R, 2);   // the peak an octave / a 9th over the floor
    auto target = [&] (int t) {
        const double u = (double) (t % phraseT) / phraseT; auto sm = [] (double x) { x = clampv (x, 0.0, 1.0); return x * x * (3 - 2 * x); };
        return u < up ? startT + (peakTarget - startT) * sm (u / up) : peakTarget + (endT - peakTarget) * sm ((u - up) / (1 - up)); };
    struct H { int t, len; bool strong, last, pickup; };
    std::vector<H> hits;
    const int bars[4] { ia, form == 2 ? ib : ia, form == 1 ? ib : ia, -1 };
    for (int b = 0; b < 4; ++b)
    {
        std::string g = bars[b] >= 0 ? C[(size_t) bars[b]].grid : E[(size_t) ie].grid;
        if (b == 3 && pickups > 0) for (int k = 16 - 2 * pickups; k < 16; ++k) g[(size_t) k] = '.';   // room for the next phrase's pickups
        for (auto& h : parseGrid (g)) hits.push_back ({ b * EBAR + h.step * EU, h.len * EU, h.step % 8 == 0 || h.len >= 4, false, false });
    }
    if (! hits.empty()) hits.back().last = true;
    for (int k = 0; k < pickups; ++k) hits.push_back ({ 3 * EBAR + (16 - 2 * (pickups - k)) * EU, 2 * EU, false, false, true });
    std::sort (hits.begin(), hits.end(), [] (const H& a, const H& b) { return a.t < b.t; });
    // the pitches follow the contour (mid start -> a peak in bar 2-3 -> down): each attack takes the key / chord note nearest its
    // target (chord tones on the strong beats, the 9th / 4th as the ache note on a downbeat now and then), then moves by a step from
    // the note before (70-80 % steps), except one leap up (a 4th .. a 6th) toward the peak that steps back down; nothing passes the
    // peak note; the phrase ends on the chord's 3rd or 5th
    std::vector<int> ps (hits.size(), 0);
    int peakIdx = -1; { double bd = 1e9; const int tp = (int) (up * phraseT); for (size_t i = 0; i < hits.size(); ++i) if (! hits[i].pickup && ! hits[i].last && hits[i].t >= EBAR && hits[i].t < 3 * EBAR && std::abs (hits[i].t - tp) < bd) { bd = std::abs (hits[i].t - tp); peakIdx = (int) i; } }
    int first = -1;
    for (size_t i = 0; i < hits.size(); ++i) if (! hits[i].pickup) { first = (int) i; break; }
    if (first < 0 || peakIdx < 0) return {};
    const int tPk = hits[(size_t) peakIdx].t;
    const int pk = eNear (peakTarget, lo + 7, hi, [&] (int q) { return c.I.H.chordTone (q, tPk); });
    int prev = -1; bool leapUsed = R() < 0.15, afterLeap = false;
    for (size_t i = (size_t) first; i < hits.size(); ++i)
    {
        const auto& h = hits[i]; if (h.pickup) continue;
        if ((int) i == peakIdx) { afterLeap = prev >= 0 && pk - prev >= 5; ps[i] = prev = pk; continue; }
        const double want = std::min ((double) pk - 1, target (h.t) + (R() < 0.3 ? (R() < 0.5 ? 1.5 : -1.5) : 0.0));
        const bool ache = h.strong && (h.t % EBAR) == 0 && R() < 0.25;
        const HChord& ch = c.I.H.at (h.t); const int nine = mod12 (ch.root + 2), four = mod12 (ch.root + 5);
        auto okStrong = [&] (int q) { return c.I.H.chordTone (q, h.t) || (ache && (mod12 (q) == nine || mod12 (q) == four) && eIn (c, q, h.t)); };
        int p = eNear ((int) std::lround (want), lo, pk - 1, [&] (int q) { return h.strong ? okStrong (q) : eIn (c, q, h.t); });
        if (prev >= 0)
        {
            const int jump = p - prev;
            const bool leapNow = ! leapUsed && jump >= 4 && (int) i < peakIdx;
            if (afterLeap) { p = eStep (c, prev, -1, h.t); afterLeap = false; }   // after the leap: a step back down
            else if (leapNow) { p = eNear (prev + pickV (R, std::vector<int> { 5, 7, 9 }), prev + 4, pk - 1, [&] (int q) { return okStrong (q) || (! h.strong && eIn (c, q, h.t)); }); leapUsed = true; afterLeap = p - prev >= 5; }
            else if (std::abs (jump) > 2)
            {   // too far for a step: walk toward it (one scale step, two on a strong beat when a chord tone sits there)
                int q = eStep (c, prev, jump > 0 ? 1 : -1, h.t);
                if (h.strong && ! okStrong (q)) { const int q2 = eStep (c, q, jump > 0 ? 1 : -1, h.t); if (okStrong (q2) && std::abs (q2 - prev) <= 4) q = q2; }
                p = q;
            }
            else if (jump == 0 && R() < 0.7) p = eStep (c, prev, target (h.t) > prev ? 1 : -1, h.t);   // fewer repeated notes
        }
        p = clampv (p, lo, pk - 1);
        if (! eIn (c, p, h.t)) p = eNear (p, lo, pk - 1, [&] (int q) { return eIn (c, q, h.t); });
        ps[i] = prev = p;
    }
    // the open ending: the last held note on the chord's 3rd or 5th (never the root)
    for (size_t i = hits.size(); i-- > 0;)
        if (hits[i].last)
        {
            const HChord& ch = c.I.H.at (hits[i].t); const int th = mod12 (ch.root + chordThird (ch)), fi = mod12 (ch.root + chordFifth (ch));
            const int top = pk - 1;   // (an octave below the peak always holds a 3rd / 5th)
            ps[i] = eNear (ps[i], top - 11, top, [&] (int q) { return (mod12 (q) == th && th != ch.root) || mod12 (q) == fi; });
            break;
        }
    // the pickups: felt 8ths stepping up into the phrase's first note (they sit at the end of the phrase before it)
    {
        int q = ps[(size_t) first];
        for (size_t i = hits.size(); i-- > 0;) if (hits[i].pickup) { q = eStep (c, q, -1, hits[i].t); if (q < lo) q = eNear (lo, lo, hi, [&] (int x) { return eIn (c, x, hits[i].t); }); ps[i] = q; }
    }
    std::vector<Note> phrase;
    for (size_t i = 0; i < hits.size(); ++i)
    {
        Note n; n.t = hits[i].t; n.p = ps[i]; n.d = std::max (8, hits[i].len - (hits[i].len >= 4 * EU ? 12 : 8));
        n.v = hits[i].strong ? 0.74f : hits[i].pickup ? 0.6f : 0.66f; n.acc = n.t % EBAR == 0;
        if (! hits[i].pickup || i >= (size_t) first) phrase.push_back (n);
    }
    sortNotes (phrase);
    for (size_t i = 0; i + 1 < phrase.size(); ++i) if (phrase[i].t + phrase[i].d > phrase[i + 1].t) phrase[i].d = std::max (6, phrase[i + 1].t - phrase[i].t - 4);
    // over the whole idea: the phrase again, refit to the chords under it (the 8-bar loop's answer), the ending kept open
    std::vector<Note> out;
    for (int t = 0, k = 0; t < c.total; t += phraseT, ++k)
    {
        std::vector<Note> rep;
        for (auto n : phrase)
        {
            n.t += t; if (n.t >= c.total) continue; n.d = std::min (n.d, c.total - n.t);
            if (k > 0 && (c.I.H.clash (n.p, n.t) || ! eIn (c, n.p, n.t))) n.p = eNear (n.p, lo, hi, [&] (int q) { return c.I.H.chordTone (q, n.t); });
            rep.push_back (n);
        }
        if (k > 0 && ! rep.empty())
        {   // the answer's last held note: open again
            for (size_t i = rep.size(); i-- > 0;)
                if (rep[i].d >= 3 * EU && rep[i].t % phraseT >= 3 * EBAR)
                {
                    const HChord& ch = c.I.H.at (rep[i].t); const int th = mod12 (ch.root + chordThird (ch)), fi = mod12 (ch.root + chordFifth (ch));
                    int top = -1; for (size_t j = 0; j < rep.size(); ++j) if (j != i) top = std::max (top, rep[j].p);
                    top = std::min (top - 1, hi);
                    rep[i].p = eNear (std::min (rep[i].p, top), top - 11, top, [&] (int q) { return (mod12 (q) == th && th != ch.root) || mod12 (q) == fi; });
                    break;
                }
        }
        out.insert (out.end(), rep.begin(), rep.end());
    }
    sortNotes (out);
    return out;
}

std::vector<Note> emoArp (const Idea& I, uint32_t seed)
{
    const ECtx c = emoCtx (I); (void) seed;
    const auto& A = emoArps()[(size_t) emoArchFor (I)];
    const std::string grid = A.grid; const auto toks = split (A.pitches, ' ');
    const auto hits = parseGrid (grid);
    const int span = (int) grid.size() * EU;
    std::vector<Note> out; int tokI = 0;
    auto rootAt = [&] (const HChord& ch) { return 50 + mod12 (ch.root - 50); };   // D3..C#4
    for (int base = 0; base < c.total; base += span)
        for (size_t i = 0; i < hits.size(); ++i)
        {
            const auto& h = hits[i]; const int t = base + h.step * EU; if (t >= c.total) break;
            if (h.step == 0 && base % EBAR == 0) tokI = 0;   // the figure starts again each felt bar
            const HChord& ch = I.H.at (t);
            const int r = rootAt (ch) + 12 * A.reg, th = chordThird (ch), fi = chordFifth (ch);
            const std::string tk = toks[(size_t) (tokI++ % (int) toks.size())];
            auto deg = [&] (const std::string& d) {
                if (d == "1") return r; if (d == "3") return r + th; if (d == "5") return r + fi; if (d == "8") return r + 12;
                if (d == "9") return has (c.pool, mod12 (ch.root + 2)) || has (ch.pcs, mod12 (ch.root + 2)) ? r + 14 : r + 12;
                if (d == "10") return r + 12 + th; return r; };
            const bool accent = t % EBAR == 0;
            const float v = std::string (A.name).rfind ("GA4", 0) == 0 ? ((h.step % 2) ? 0.42f : 0.6f) : accent ? 0.64f : (t % (EBAR / 4) == 0 ? 0.56f : 0.5f);
            std::vector<int> ps;
            if (tk == "S") ps = { r, r + fi, r + 12, r + 12 + th };   // root, 5th, octave, the 3rd on top (strummed by the sound)
            else if (tk == "P") ps = { r, r + 7 };
            else ps = { deg (tk) };
            for (int p : ps)
            {
                while (p > 79) p -= 12;
                Note n; n.t = t; n.p = p; n.v = tk == "P" ? 0.55f : v; n.acc = accent;
                n.d = tk == "P" ? EU - 10 : std::max (EU, std::min (h.len * EU + 3 * EU, 8 * EU));   // fingerpicked strings ring on; palm-muted ones stop
                out.push_back (n);
            }
        }
    sortNotes (out);
    // a string rings until it is picked again, never over the chord change
    for (size_t i = 0; i < out.size(); ++i)
    {
        auto& n = out[i]; const int L = std::max (1, I.H.loopTicks), chEnd = n.t - n.t % L + I.H.at (n.t).tEnd;
        n.d = std::min (n.d, std::max (8, chEnd - n.t - 4));
        for (size_t j = i + 1; j < out.size(); ++j) if (out[j].p == n.p) { n.d = std::min (n.d, std::max (6, out[j].t - n.t - 2)); break; }
        n.d = std::min (n.d, c.total - n.t);
    }
    return out;
}

std::vector<Note> emoBass (const Idea& I, uint32_t seed, int* archOut)
{
    const ECtx c = emoCtx (I); const auto& B = emoBasses();
    Rng R = emoRng (I, seed, 0xEB5u);
    int ai = irand (R, (int) B.size());
    if (I.urbanProg == 2 && R() < 0.5) ai = 5;                      // the descending-bass loop walks down
    if (archOut) *archOut = ai;
    const auto& A = B[(size_t) ai];
    const auto hits = parseGrid (A.grid); const auto toks = split (A.pitches, ' ');
    const int keyHome = 24 + mod12 (I.H.key - 24);
    auto home = [&] (int pc) { int p = 24 + mod12 (pc - 24); if (p - keyHome > 6) p -= 12; if (keyHome - p > 5) p += 12; while (p < 24) p += 12; return p; };
    std::vector<Note> out;
    const int bars = std::max (1, c.total / EBAR);
    // the slide bar of each 4-bar window: the 4th (into the next loop) when another bar follows in the take, else the 2nd (into the
    // 3rd: the take's last bar has no note after it to slide into)
    auto slideBarOf = [&] (int b) { const int w0 = b / 4 * 4; return w0 + 4 < bars ? w0 + 3 : w0 + 1; };
    for (int b = 0; b < bars; ++b)
    {
        const int t0 = b * EBAR; const bool turn = b % 4 == 3, slideBar = b == slideBarOf (b);
        // EB4 lifts in the loop's 4th bar, EB7 sighs into the turnaround (else the whole-note root); EB10 drops in on the odd bars only
        const EmoBass& use = (ai == 3 && ! turn) || (ai == 6 && ! slideBar) || (ai == 9 && b % 2 == 1) ? B[0] : A;
        const auto hs = &use == &A ? hits : parseGrid (use.grid); const auto ts = &use == &A ? toks : split (use.pitches, ' ');
        for (size_t i = 0; i < hs.size(); ++i)
        {
            const int t = t0 + hs[i].step * EU; if (t >= c.total) break;
            const HChord& ch = I.H.at (t);
            const std::string tk = i < ts.size() ? ts[i] : "1";
            int p = home (bassPc (ch));
            if (tk == "8") p += 12;
            else if (tk == "5") p = home (mod12 (bassPc (ch) + 7));
            else if (tk == "N") p = home (bassPc (I.H.next (t)));
            else if (tk == "w1" || tk == "w2") { int q = p; for (int k = 0; k < (tk == "w1" ? 1 : 2); ++k) { q = eStep (c, q, -1, t); } p = q < 24 ? q + 12 : q; }
            while (p > 47) p -= 12; while (p < 24) p += 12;
            Note n; n.t = t; n.p = p; n.d = std::max (8, hs[i].len * EU - 4); n.v = t % EBAR == 0 ? 0.74f : 0.66f; n.acc = t % EBAR == 0;
            if (hs[i].slide) { n.l.type = 1; n.l.w = 0.85f; }
            out.push_back (n);
        }
    }
    sortNotes (out);
    // EB3: the anticipation ties over (the next bar's downbeat on the same note is not struck again)
    for (size_t i = 0; i + 1 < out.size(); ++i)
        if (out[i + 1].t % EBAR == 0 && out[i + 1].p == out[i].p && out[i].t % EBAR != 0 && (out[i].t + out[i].d + 8 >= out[i + 1].t))
        { out[i].d = out[i + 1].t + out[i + 1].d - out[i].t; out.erase (out.begin() + (long) i + 1); }
    // the turnaround slide (the loop's 4th bar into the next root): EB7 always, the others now and then; at most one per 4 felt bars
    Rng Rs = emoRng (I, seed, 0x51DEu);
    const bool wantSlide = ai == 6 || Rs() < 0.35;
    for (size_t i = 0; i < out.size(); ++i) { if (out[i].l.type == 1 && ! wantSlide) out[i].l = Link {}; }
    if (wantSlide && ai != 6)
        for (int w = 0; w < bars; w += 4)
        {
            const int b = slideBarOf (w); if (b >= bars) break;
            const int tEnd = (b + 1) * EBAR; int li = -1;
            for (size_t i = 0; i < out.size(); ++i) if (out[i].t < tEnd && out[i].t >= b * EBAR) li = (int) i;
            if (li >= 0 && (size_t) li + 1 < out.size() && out[(size_t) li + 1].p != out[(size_t) li].p) { out[(size_t) li].l.type = 1; out[(size_t) li].l.w = 0.85f; }
        }
    {   // the cap, whatever the archetype: one slide per 4 felt bars
        int lastWin = -1;
        for (auto& n : out) if (n.l.type == 1) { const int w = n.t / (4 * EBAR); if (w == lastWin) n.l = Link {}; else lastWin = w; }
    }
    for (size_t i = 0; i < out.size(); ++i)
    {
        auto& n = out[i];
        if (n.l.type != 1) continue;
        if (i + 1 >= out.size() || out[i + 1].p == n.p) { n.l = Link {}; continue; }
        const int gap = out[i + 1].t - n.t; const double g = 0.075 * c.tps;   // a short 60-90 ms slide into the next root
        n.l.w = (float) clampv (1.0 - g / std::max (1, gap), 0.3, 0.97); n.d = std::max (n.d, gap - 2);
    }
    for (size_t i = 0; i + 1 < out.size(); ++i) if (out[i].l.type != 1 && out[i].t + out[i].d > out[i + 1].t) out[i].d = std::max (4, out[i + 1].t - out[i].t - 4);   // mono
    for (auto& n : out) n.d = std::min (n.d, c.total - n.t);
    return out;
}
} // namespace arp
