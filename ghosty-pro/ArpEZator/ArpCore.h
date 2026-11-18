#pragma once
// ArpEZator chord core: a faithful C++ port of the web app's theory.js chord side (seeded RNG, prompt parser,
// progression generator, STYLE profile "Professor B5", chord alternatives, voice leading, names, mood analysis)
// plus the SMF writer. Same prompt + seed gives the same chords as the web version (checked by tests/ArpTests.cpp
// against outputs dumped from the JS). The melody / arp side lives in Composer.h (its own engine).
// No JUCE dependency: plain C++17, deterministic, no allocation-sensitive code (message thread only).
#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <utility>

namespace arp
{
constexpr int BAR = 384, PPQ = 96;   // ticks: 96 per quarter, 384 per 4/4 bar

// ---------------------------------------------------------------- JS-compatible helpers
uint32_t hashStr (const std::u16string& s);
uint32_t hashStr (const std::string& asciiOrUtf8);   // UTF-8 -> UTF-16 code units first (same as JS charCodeAt)
struct Rng   // mulberry32, exactly as theory.js rng()
{
    uint32_t s;
    explicit Rng (uint32_t seed) : s (seed) {}
    double operator()();
};
double jsRound (double x);   // Math.round (half up)
inline int mod12 (int x) { return ((x % 12) + 12) % 12; }
template <typename T> T clampv (T x, T a, T b) { return x < a ? a : (x > b ? b : x); }

// an object with JS property order: array-index keys first (ascending), then the rest in insertion order
struct JMap
{
    std::vector<std::pair<std::string, double>> kv;
    JMap() = default;
    JMap (std::initializer_list<std::pair<std::string, double>> l) { for (auto& p : l) set (p.first, p.second); }
    static bool isIndex (const std::string& k);
    double* find (const std::string& k);
    const double* find (const std::string& k) const;
    void set (const std::string& k, double v);
    void inc (const std::string& k, double w = 1) { if (auto* p = find (k)) *p += w; else set (k, w); }
    double get (const std::string& k, double d = 0) const { auto* p = find (k); return p ? *p : d; }
    bool empty() const { return kv.empty(); }
};
std::string pickW (Rng& R, const JMap& table);   // "" when the table is empty (JS: undefined)
JMap normH (const JMap& h);

// ---------------------------------------------------------------- scales
const std::vector<int>& scaleOf (const std::string& name);   // minor major dorian phrygian lydian mixolydian harmonic
const std::vector<std::string>& scaleNames();
std::string scaleLabel (const std::string& s);
std::string scaleFeel (const std::string& s);
bool minorish (const std::string& s);
std::string noteName (int pc, int key, const std::string& scale);
std::string keyName (int key, const std::string& scale);
std::string chordSuffix (const std::vector<int>& iv);
std::string triadQuality (const std::vector<int>& iv);

// ---------------------------------------------------------------- progression data
struct FixedChord { int root = 0; std::vector<int> iv; std::optional<int> bass; bool chrom = false; int deg = -1; std::string roman; };
struct Slot
{
    std::string tok; double r = 0.5, r2 = 0.9; int alt = 0, len = 4;
    std::string q, sl;            // "" = undefined
    std::optional<FixedChord> fixed;   // chord heard by LISTEN / imported (as-is)
};
struct ChordBase { int root = 0; std::vector<int> iv; std::optional<int> bass; int deg = -1; bool chrom = false; std::vector<int> cs; std::string roman; };
struct Resolved : ChordBase
{
    std::string name; std::vector<int> voicing; int bassNote = 36; std::vector<int> pcs; int t = 0, len = 4, tEnd = 0;
};
struct TokBase { std::vector<int> cs; int dd = 0, root = 0; bool chrom = false; std::string tok; bool ok = false; };
TokBase resolveToken (const std::string& tok, const std::string& scale);
int csTone (const TokBase& b, int k);
const std::vector<std::string>& altNames();
ChordBase altChord (const Slot& slot, const std::string& scale, double colour);
std::string romanOf (const ChordBase& ch, const std::string& scale);
std::vector<int> voiceChord (const std::vector<int>& pcsIn, const std::vector<int>* prev, int rootPc);
double vlCost (const std::vector<int>& a, const std::vector<int>& b);

struct ProgState   // what resolveProg needs (a subset of the web app's S)
{
    int key = 9; std::string scale = "minor"; double colour = 0.4; bool pedal = false; std::vector<Slot> prog;
    bool simple = false; int mood = 2;   // SIMPLE chords: plain major / minor triads, bass = root (mood 0 SUNRISE 1 DUSK 2 MIDNIGHT)
};
std::vector<Resolved> resolveProg (const ProgState& S);
int progTicks (const std::vector<Slot>& prog);

// ---------------------------------------------------------------- SIMPLE chords (classic progressive-house triads)
std::string simpleScale (const std::string& scale);   // minor-ish -> "minor", else "major"
ChordBase simplifyChord (const ChordBase& c, const std::string& famScale, int mood);   // plain triad, bass = root, no colour
const std::vector<std::pair<std::string, double>>& classicLoops (int mood, const std::string& scale);   // 4-chord loops + weights
std::vector<Slot> classicProg (int mood, const std::string& scale, uint32_t seed, int nBars = 4, const std::vector<Slot>* avoid = nullptr);

// ---------------------------------------------------------------- prompt parser
struct Parsed
{
    std::vector<std::string> tags;   // moods in LEX order
    std::optional<std::string> genre, scale, rate, preset, style;
    std::optional<int> key, bpm, oct, reg, bars;
    std::optional<double> master, colour;
    int speed = 0; double groove = 0;
    bool rolling = false, glide = false, ratchet = false, sus = false;
    bool hasTag (const std::string& t) const;
};
Parsed parsePrompt (const std::string& utf8Prompt);

// ---------------------------------------------------------------- STYLE profile (aggregate statistics only)
struct FamProfile
{
    JMap start;
    std::vector<std::pair<std::string, JMap>> tr, tri;
    const JMap* trOf (const std::string& a) const;
    const JMap* triOf (const std::string& ab) const;
};
struct ArpStats { JMap rate, step, oct, motif; double rest = 0, accent = 0, ratchet = 0; bool has = false; };
struct Profile
{
    std::string name; bool builtin = false;
    FamProfile minorF, majorF; bool hasMinor = false, hasMajor = false;
    JMap colour, len, bars; double slash = 0, ped = 0; std::optional<std::pair<int, int>> bpm;
    ArpStats arp;
    const FamProfile* fam (const std::string& f) const { return f == "minor" ? (hasMinor ? &minorF : nullptr) : (hasMajor ? &majorF : nullptr); }
};
const Profile& builtinProfile();   // "Professor B5": built from this app's own templates + hand-set numbers
struct StyleArg { double amt = 0; const Profile* prof = nullptr; bool active() const { return amt > 0 && prof != nullptr; } };

// ---------------------------------------------------------------- the generator
struct Gen
{
    std::string prompt; uint32_t seed = 0; std::string genre; std::vector<std::string> moods;
    int key = 0; std::string scale; int bars = 4; double colour = 0.4; std::vector<Slot> prog;
    std::string style, preset, rate; int bpm = 120; double master = 0.5; int oct = 2, reg = 0;
    bool glide = false, ratchet = false, rolling = false, pedal = false; double gate = 0.5, swing = 0, groove = 0;
    double fxTone = 0.72, fxDelay = 0.32, fxReverb = 0.3, fxPump = 0.45;
    uint32_t arpSeed = 0, progSeed = 0;
    bool styled = false; double styAmt = 0; std::string styName;   // g.sty
};
Gen generate (const std::string& utf8Prompt, uint32_t seed, StyleArg sty = {});
std::vector<Slot> newProg (const Gen& S, uint32_t seed, StyleArg sty = {});
std::string famOf (const std::string& scale);
std::optional<std::string> degToken (int rel, const std::vector<int>& iv, const std::string& fam);

// ---------------------------------------------------------------- mood (drives the ghost's face)
struct MoodResult { std::string mood; double valence = 0, tension = 0, lush = 0, energy = 0; };
MoodResult analyzeMood (const std::vector<Resolved>& chords, const std::string& scale, int bpm, double master, const std::string& preset, const std::string& rate);

// ---------------------------------------------------------------- MIDI writer (SMF type 1, 96 PPQ)
struct MidiNote { int t, d, p; double v; };
struct MidiCtl { int t = 0; int kind = 0; /*0 pitch bend (val 0..16383), 1 CC*/ int num = 0, val = 0; };
struct MidiTrack { std::string name; int ch = 0; std::vector<MidiNote> notes; std::vector<MidiCtl> ctl; };
std::vector<uint8_t> writeMidi (double bpm, const std::vector<MidiTrack>& tracks);
} // namespace arp
