// GHOST SLICE build 6 - the melody brain (Brain.h). Ports of the house's generators (Block Step / Ghost House app.js: genPiano,
// synRows, revCompose, brainPiano / brainSynth; the n-gram models' data) and the link to ArpEZator's own engine.
#include "Brain.h"
#include "Generate.h"
#include "Analysis.h"
#include "Idea.h"
#include "UrbanMoods.h"
#include "HookLab.h"
#include "RecipeJson.h"
#include <juce_core/juce_core.h>
#include <map>
#include <mutex>
#include <unordered_map>

namespace gslice::gen::brain
{
using dsp::Rng;
namespace
{
#include "BrainData.inc"

inline int pcOf (int n) { return ((n % 12) + 12) % 12; }
template <typename T> const T& rp (Rng& r, const std::vector<T>& v) { return v[(size_t) r.pick ((int) v.size())]; }
int inR (int n, int lo, int hi) { while (n < lo) n += 12; while (n > hi) n -= 12; return n; }

// ---------------------------------------------------------------- the registry
enum Kind { KPiano, KSynth, KRevEmo, KRevPat, KPop, KAcidBrain, KArp, KArpHook, KTrap, KHipHop, KHookLab, KParlour };
struct Def { Info info; Kind kind; int a; };
const std::vector<Def>& defs()
{
    static const std::vector<Def> D {
        // ---- PIANO (Block Step / Ghost House piano page)
        { { "ANTHEM", "PIANO", "Big festival chords and a singalong hook." }, KPiano, 0 },
        { { "PROG", "PIANO", "Wide minor chords, plucky 16th arps and a lift at the end." }, KPiano, 1 },
        { { "PIANO HOUSE", "PIANO", "90s piano house: 7th-chord stabs on the M1 rhythm, offbeat octave bass." }, KPiano, 2 },
        { { "DEEP", "PIANO", "Deep house: 9th and major 7th chords that breathe, a few soft notes on top." }, KPiano, 3 },
        { { "EUPHORIC", "PIANO", "Trance-style gated 8th chords and a fast arpeggio lead." }, KPiano, 4 },
        { { "LULLABY", "PIANO", "A gentle, simple melody over flowing broken chords, like a player piano: human but not quite." }, KPiano, 5 },
        { { "RAIN", "PIANO", "Minor, slow and haunting: a 3-note figure that cycles against the 4/4 grid." }, KPiano, 6 },
        { { "PREPARED", "PIANO", "Prepared-piano clusters, odd timing and stutters." }, KPiano, 7 },
        { { "DRIFT", "PIANO", "Ambient: long soft chords, a few notes floating." }, KPiano, 8 },
        { { "SKITTER", "PIANO", "Fast runs up and down the chords, little slips and stutters." }, KPiano, 9 },
        { { "BUILD", "PIANO", "A build-up: the chords gate faster and the tune climbs and gets busier to the end." }, KPiano, 10 },
        { { "BRIDGE", "PIANO", "A breakdown: other chords, long notes, room to breathe before the return." }, KPiano, 11 },
        // ---- SYNTH (Block Step / Ghost House synth page)
        { { "ACID", "SYNTH", "A classic 303 line: slides, accents, octave jumps; a hook, its variations and an answer." }, KSynth, 0 },
        { { "AFX ACID", "SYNTH", "Aphex acid: an odd-length motif against the bar, ratchets, wide jumps." }, KSynth, 1 },
        { { "101 ROLL", "SYNTH", "A rolling SH-101 bass in 16ths: root, octave and fifth, a few slides." }, KSynth, 2 },
        { { "FM BELLS", "SYNTH", "A slow bell arpeggio with rests over a sparse syncopated bass." }, KSynth, 3 },
        { { "SUPERSAW", "SYNTH", "A big 2-bar hook answered in bars 3-4, over wide chords (trance anthem)." }, KSynth, 4 },
        { { "PLUCKS", "SYNTH", "Rolling pluck arpeggios on the chord tones." }, KSynth, 5 },
        { { "STABS", "SYNTH", "House chord stabs on the off-beats and pushes." }, KSynth, 6 },
        // ---- REVERIE (Block Step's original piano composer): emotions (texture picked by the emotion) and textures
        { { "MELANCHOLIC", "REVERIE", "Minor, falling, aching: i VI III VII." }, KRevEmo, 0 },
        { { "PEACEFUL", "REVERIE", "Open, floating, unhurried: added 9ths, the IV that never leaves." }, KRevEmo, 1 },
        { { "UNSETTLING", "REVERIE", "Half steps that rub and a tritone that will not resolve." }, KRevEmo, 2 },
        { { "HOPEFUL", "REVERIE", "Major, rising: I V vi IV and lifts to the IV." }, KRevEmo, 3 },
        { { "HAUNTING", "REVERIE", "Over a held low note, suspensions that hang in the air." }, KRevEmo, 4 },
        { { "NOSTALGIC", "REVERIE", "Major 7ths and the borrowed minor iv: warm, a little sad." }, KRevEmo, 5 },
        { { "ARPEGGIOS", "REVERIE", "Repeating triplet arpeggios over a left hand in twos (3 against 2), slowly changing." }, KRevPat, 0 },
        { { "BROKEN CHORDS", "REVERIE", "A rolling left hand under a singing melody." }, KRevPat, 1 },
        { { "SPARSE MELODY", "REVERIE", "Held chords, a few soft notes, a lot of air." }, KRevPat, 2 },
        { { "CASCADES", "REVERIE", "Runs that fall across the keyboard and land on the chord." }, KRevPat, 3 },
        { { "MINIMAL", "REVERIE", "A small cell that grows note by note and drifts against the left hand." }, KRevPat, 4 },
        // ---- LEARNED (n-gram models)
        { { "LEARNED POP", "LEARNED", "Chords, melody and two-hand rhythms learned from 900 pop songs: every take new, nothing copied." }, KPop, 0 },
        { { "LEARNED ACID", "LEARNED", "A 303 line from a model learned from real acid patterns: what usually comes next, never a copy." }, KAcidBrain, 0 },
        // ---- PARLOUR PIANO (Block Step's learned piano: chords, the left hand's rhythms and the melody learned from old parlour piano scores)
        { { "WISTFUL", "PARLOUR PIANO", "A learned old-fashioned parlour piano: slow, gentle chords, a swaying left hand and a floating, unhurried tune." }, KParlour, 0 },
        { { "STILL NIGHT", "PARLOUR PIANO", "The learned parlour piano, slower and sparser: long held notes, a lot of air." }, KParlour, 1 },
        // ---- ARPEZATOR (Professor Bernstein's engine)
        { { "SUNRISE ARP", "ARPEZATOR", "Happy: an ostinato arp figure that moves one or two notes per chord." }, KArp, 0 },
        { { "DUSK ARP", "ARPEZATOR", "Bittersweet: a rolling figure over a pedal note." }, KArp, 1 },
        { { "MIDNIGHT ARP", "ARPEZATOR", "Sad and dark: a figure that drifts against the bar and re-syncs." }, KArp, 2 },
        { { "SUNRISE HOOK", "ARPEZATOR", "Happy: a phrase-based hook (3-3-2 rhythms, call and response, A A' A B)." }, KArpHook, 0 },
        { { "DUSK HOOK", "ARPEZATOR", "Bittersweet: a hook with suspensions and anticipations." }, KArpHook, 1 },
        { { "MIDNIGHT HOOK", "ARPEZATOR", "Sad: a hook with octave answers and a lift that resolves." }, KArpHook, 2 },
        { { "DARK", "TRAP", "Trap: half-time, dark minor motifs, space between the notes." }, KTrap, 0 },
        { { "TRIUMPHANT", "TRAP", "Trap: big, proud minor lines that climb." }, KTrap, 1 },
        { { "HYPNOTIC", "TRAP", "Trap: a short loop that repeats and hypnotises." }, KTrap, 2 },
        { { "DRILL", "TRAP", "Drill: sliding, syncopated minor lines." }, KTrap, 3 },
        { { "RATCHET", "TRAP", "Trap: bouncy repeated notes and quick rolls." }, KTrap, 4 },
        { { "EMO", "TRAP", "Emo trap: a singable sad melody over major loops heard as minor." }, KTrap, 5 },
        { { "BOOM BAP", "HIP HOP", "Hip hop: a swung, loop-based phrase with a kick-shadow bounce." }, KHipHop, 0 },
        { { "MEMPHIS", "HIP HOP", "Hip hop: dark, eerie, church-key minor lines." }, KHipHop, 1 },
        { { "WEST GLIDE", "HIP HOP", "Hip hop: smooth gliding lead runs, West Coast." }, KHipHop, 2 },
        { { "UNDERWATER", "HIP HOP", "Hip hop: soft, slow, wobbly and far away." }, KHipHop, 3 },
        { { "JAZZ SOUL", "HIP HOP", "Hip hop: jazzy 7th-chord phrases with a dotted lilt." }, KHipHop, 4 },
        { { "HYPNOTIC HOOK", "HOOK LAB", "A hook that repeats almost the same, small changes: hypnotic." }, KHookLab, 1 },
        { { "ANTHEMIC", "HOOK LAB", "A hook that climbs to a big peak: anthemic." }, KHookLab, 2 },
        { { "MINIMAL HOOK", "HOOK LAB", "Few notes, lots of repetition: minimal." }, KHookLab, 3 },
        { { "EMOTIONAL", "HOOK LAB", "Wide leaps that fall back, long notes: emotional." }, KHookLab, 4 },
        { { "PLAYFUL", "HOOK LAB", "Bouncy, syncopated, short notes: playful." }, KHookLab, 5 },
    };
    return D;
}

// ---------------------------------------------------------------- shared pieces
juce::String chordName (int root, const std::vector<int>& iv)
{
    juce::String n = analysis::pitchClassName (pcOf (root));
    auto has = [&] (int x) { for (int v : iv) if (pcOf (v) == x) return true; return false; };
    const bool halfDim = has (3) && ! has (4) && has (6) && ! has (7) && has (10);   // (build 8: B D F A is Bm7b5, not "Bdim7")
    if (halfDim) n << "m7b5";
    else if (has (3) && ! has (4)) n << (has (6) && ! has (7) ? "dim" : "m");
    if (! halfDim) { if (has (11)) n << "maj7"; else if (has (10)) n << "7"; }
    if (has (2) && ! has (3) && ! has (4)) n << "sus2"; else if (has (2)) n << "(9)";
    if (has (5) && ! has (3) && ! has (4)) n << "sus4";
    return n;
}
// every pitch class of a chord into the key (the original's borrowed / chromatic tones move to the nearest scale note)
Chord makeChord (const Key& k, int root, std::vector<int> iv, std::vector<int> voicing)
{
    Chord c; c.degree = 0;
    std::vector<int> pcs;
    for (int x : iv) { int pc = pcOf (root + x); if (! inScale (pc + 60, k)) pc = pcOf (snapToScale (pc + 60, k)); if (std::find (pcs.begin(), pcs.end(), pc) == pcs.end()) pcs.push_back (pc); }
    c.pcs = pcs;
    for (int& n : voicing) if (! inScale (n, k)) n = snapToScale (n, k);
    c.notes = voicing;
    const auto& sc = scaleOf (k.minor);
    for (int d = 0; d < 7; ++d) if (pcOf (k.tonic + sc[(size_t) d]) == pcs.front()) c.degree = d;
    std::vector<int> rel; for (int pc : pcs) rel.push_back (pcOf (pc - pcs.front()));
    c.name = chordName (pcs.front(), rel);
    return c;
}
void inKey (std::vector<NoteEvent>& ev, const Key& k) { for (auto& e : ev) if (! inScale (e.note, k)) e.note = snapToScale (e.note, k); }
// one line out of several: at each onset the highest note (a lead), no overlaps
std::vector<NoteEvent> monophonic (std::vector<NoteEvent> ev, bool highest = true)
{
    std::stable_sort (ev.begin(), ev.end(), [&] (const NoteEvent& a, const NoteEvent& b) { return a.beat < b.beat - 1e-9 || (std::abs (a.beat - b.beat) < 1e-9 && (highest ? a.note > b.note : a.note < b.note)); });
    std::vector<NoteEvent> out;
    for (auto& e : ev) if (out.empty() || e.beat > out.back().beat + 1e-6) out.push_back (e);
    for (size_t i = 0; i + 1 < out.size(); ++i) out[i].length = juce::jmin (out[i].length, juce::jmax (0.05, out[i + 1].beat - out[i].beat - 0.02));
    return out;
}
// the lead moved by octaves so its middle sits near centre, then folded into about an octave and a half around it
void centreLead (std::vector<NoteEvent>& ev, int centre, int span = 12)
{
    if (ev.empty()) return;
    std::vector<int> ns; for (auto& e : ev) ns.push_back (e.note); std::sort (ns.begin(), ns.end());
    const int med = ns[ns.size() / 2], sh = 12 * (int) std::lround ((centre - med) / 12.0);
    for (auto& e : ev) { e.note += sh; while (e.note > centre + span) e.note -= 12; while (e.note < centre - span) e.note += 12; }
}
// rows (16ths or 24ths per bar) -> beats
struct Grid { int perBar = 16; double beat (double row) const { return row * 4.0 / perBar; } };

// ---------------------------------------------------------------- PIANO (Block Step / Ghost House genPiano + pianoRows)
struct PStyle
{
    std::vector<std::vector<const char*>> major, minor; double hr; const char* voice;
    std::vector<const char*> comp, bass; double den; int lo, hi; const char* cells; const char* len; const char* ctr;
    double lift, hum, fx; int v0, v1; double oct, chrom; int shape;   // shape: 0 plain, 1 BUILD (rises), 2 BRIDGE (breaks down)
};
const PStyle& pstyle (int i)
{
    static const PStyle S[12] {
        { { { "vi", "IV", "I", "V" }, { "I", "V", "vi", "IV" }, { "IV", "I", "V", "vi" }, { "I", "vi", "IV", "V" }, { "IV", "V", "vi", "vi" }, { "vi", "V", "IV", "V" } },
          { { "i", "VI", "III", "VII" }, { "VI", "VII", "i", "i" }, { "i", "VII", "VI", "VII" } }, 1, "close", { "synco", "synco2", "push", "stab8" }, { "pump", "octs" }, .5, 67, 84, "hook", "next", "sparkle", 0, 0, .12, 72, 96, 0, 0, 0 },
        { { { "Imaj7", "IVmaj7", "vi7", "V" }, { "IV", "vi", "I", "V" } }, { { "i", "VII", "VI", "IV" }, { "i", "VI", "III", "VII" }, { "VI", "III", "VII", "i" }, { "iadd9", "VImaj7", "IIImaj7", "VII" }, { "i", "iv", "VI", "V" } },
          1, "spread", { "arp16", "arp16b" }, { "sus", "deep" }, .32, 69, 84, "long", "next", "pluck", .45, 0, .3, 62, 88, 0, 0, 0 },
        { { { "I", "vi7", "ii7", "V" }, { "IVmaj7", "iii7", "ii7", "Imaj7" }, { "I", "IV", "vi", "V" } }, { { "i7", "iv7", "i7", "v7" }, { "i7", "VImaj7", "iv7", "v7" }, { "i7", "VII", "VI", "VII" } },
          1, "close", { "m1", "m1b", "stab8" }, { "off" }, .42, 72, 84, "house", "short", "none", 0, 0, .08, 70, 98, 0, 0, 0 },
        { { { "Imaj7", "IVmaj7", "iii7", "vi7" }, { "IVmaj7", "Imaj7" } }, { { "i9", "VImaj7", "iv9", "v7" }, { "i9", "iv9" }, { "ii7", "v7", "i9", "i9" } },
          .5, "close", { "swell", "push" }, { "deep", "sus" }, .2, 67, 81, "long", "long", "none", 0, .04, .3, 52, 78, 0, 0, 0 },
        { { { "IV", "V", "iii", "vi" }, { "I", "V", "vi", "IV" } }, { { "i", "VI", "III", "VII" }, { "VI", "VII", "i", "v" }, { "i", "iv", "VII", "III" } },
          1, "close", { "gate8" }, { "pump" }, .75, 67, 84, "run", "short", "sparkle", 0, 0, .1, 68, 94, 0, 0, 0 },
        { { { "I", "iii", "IV", "I" }, { "Imaj7", "IVmaj7", "I", "V" }, { "IV", "I", "ii", "I" }, { "I", "vi", "IV", "iv" } }, { { "i", "VI", "iv", "i" }, { "i", "III", "VI", "v" } },
          1, "broken", { "broken8" }, { "sus" }, .28, 64, 81, "simple", "long", "third", 0, .1, .05, 42, 74, 0, 0, 0 },
        { { { "vi", "IV", "vi", "IV" }, { "I", "iii", "vi", "IV" } }, { { "i", "VI", "i", "VI" }, { "i", "iv", "VI", "V" }, { "i", "VII", "VI", "VII" } },
          1, "broken", { "waltz3" }, { "sus" }, .18, 67, 84, "simple", "long", "none", 0, .12, .12, 38, 70, 0, 0, 0 },
        { { { "I", "bVII", "IV", "I" }, { "Iadd9", "bVI", "bVII", "I" } }, { { "i", "bII", "i", "VII" }, { "iadd9", "iv", "i", "bVI" }, { "i", "v", "bII", "i" } },
          1, "cluster", { "cluster" }, { "sparse" }, .3, 64, 84, "odd", "mixed", "none", 0, .3, .35, 30, 92, .15, 0, 0 },
        { { { "Imaj7", "IVmaj7" }, { "Iadd9", "vi7", "IVmaj7", "Iadd9" } }, { { "i9", "VImaj7" }, { "iadd9", "VImaj7", "IIImaj7", "VImaj7" } },
          .5, "close", { "swell" }, { "sus" }, .12, 67, 84, "long", "long", "none", 0, .18, .6, 30, 60, 0, 0, 0 },
        { { { "I", "bVII", "IV", "iv" } }, { { "i", "VI", "III", "VII" }, { "i", "bII", "VI", "v" } },
          1, "spread", { "run16" }, { "sus", "sparse" }, .6, 67, 84, "run", "short", "none", 0, .06, .18, 45, 90, 0, .18, 0 },
        // BUILD / BRIDGE: Ghost House's song sections (build: cx .6 sp .45, no fills; bridge: cx .4 sp .55) as piano shapes
        { { { "IV", "V", "vi", "V" }, { "vi", "IV", "I", "V" } }, { { "VI", "VII", "i", "VII" }, { "i", "VI", "VII", "VII" } },
          1, "close", { "gate8" }, { "pump" }, .45, 67, 84, "hook", "short", "sparkle", 0, 0, .1, 60, 98, 0, 0, 1 },
        { { { "vi", "IV", "ii", "V" }, { "IV", "iii", "ii", "V" } }, { { "iv", "VI", "III", "VII" }, { "VI", "iv", "VII", "VII" } },
          .5, "spread", { "swell" }, { "sus" }, .3, 67, 84, "long", "long", "third", 0, .06, .2, 44, 76, 0, 0, 2 },
    };
    return S[juce::jlimit (0, 11, i)];
}
struct RChord { int root; std::vector<int> tones; };
RChord romanChord (const std::string& tok, bool minor, int rootPc)
{
    static const std::map<std::string, int> RN { { "I", 0 }, { "II", 2 }, { "III", 4 }, { "IV", 5 }, { "V", 7 }, { "VI", 9 }, { "VII", 11 } };
    size_t i = 0; const bool flat = ! tok.empty() && tok[0] == 'b'; if (flat) ++i;
    std::string num; while (i < tok.size() && std::string ("ivIV").find (tok[i]) != std::string::npos) num += tok[i++];
    const std::string suf = tok.substr (i);
    std::string up = num; for (auto& ch : up) ch = (char) std::toupper ((unsigned char) ch);
    const bool upper = num == up;
    int iv = RN.count (up) ? RN.at (up) : 0; if (minor && (up == "III" || up == "VI" || up == "VII")) iv -= 1; if (flat) iv -= 1;
    RChord c; c.root = pcOf (rootPc + iv); c.tones = upper ? std::vector<int> { 0, 4, 7 } : std::vector<int> { 0, 3, 7 };
    if (suf.find ("sus2") != std::string::npos) c.tones[1] = 2; if (suf.find ("sus4") != std::string::npos) c.tones[1] = 5;
    if (suf.find ("maj7") != std::string::npos) c.tones.push_back (11); else if (suf.find ('7') != std::string::npos || suf == "9") c.tones.push_back (10);
    if (suf.find ('9') != std::string::npos) c.tones.push_back (14);
    return c;
}
std::vector<int> pvoicing (const RChord& ch, const std::vector<int>* prev, const char* kind, Rng& r)
{
    const auto& T = ch.tones;
    std::vector<int> pcs = T.size() > 3 ? std::vector<int> { T[1], T[3], T[T.size() > 4 ? 4 : 2] } : T;
    if (std::string (kind) == "spread") { const int a = inR (ch.root + T[2], 50, 61), b = inR (ch.root + T[1], a + 7, a + 18), c = inR (ch.root + (T.size() > 3 ? T[3] : 12), b + 3, b + 14); return { a, b, c }; }
    if (std::string (kind) == "broken") { const int a = inR (ch.root + T[2], 50, 61), b = inR (ch.root + T[1] + 12, a + 3, a + 14), c = inR (ch.root + 12, b + 1, b + 12); return { a, b, c }; }
    std::vector<int> best; int bs = 1 << 30;
    for (int inv = 0; inv < 3; ++inv)
        for (int base = 52; base <= 64; ++base)
        {
            int n = inR (ch.root + pcs[(size_t) inv % pcs.size()], base, base + 11); std::vector<int> out { n };
            for (int k = 1; k < 3; ++k) { n = inR (ch.root + pcs[(size_t) (k + inv) % pcs.size()], n + 1, n + 12); out.push_back (n); }
            if (out[2] > 74) continue;
            int sc = 0; if (prev != nullptr && prev->size() == 3) for (int k = 0; k < 3; ++k) sc += std::abs (out[(size_t) k] - (*prev)[(size_t) k]); else sc = std::abs (out[0] - 57) * 2;
            if (sc < bs) { bs = sc; best = out; }
        }
    if (std::string (kind) == "cluster" && r.chance (.6)) best[1] = best[0] + (r.chance (.5) ? 1 : 2);   // a rubbed second
    return best;
}
struct CompHit { int st, lane, len; };
std::vector<CompHit> compPattern (const std::string& c)
{
    if (c == "push") return { { 0, 0, 6 }, { 6, 0, 4 }, { 10, 0, 6 } };
    if (c == "synco") return { { 0, 0, 3 }, { 3, 0, 3 }, { 6, 0, 4 }, { 10, 0, 2 }, { 12, 0, 4 } };
    if (c == "synco2") return { { 0, 0, 3 }, { 3, 0, 5 }, { 8, 0, 3 }, { 11, 0, 3 }, { 14, 0, 2 } };
    if (c == "stab8") return { { 2, 0, 2 }, { 6, 0, 2 }, { 10, 0, 2 }, { 14, 0, 2 } };
    if (c == "m1") return { { 0, 0, 2 }, { 3, 0, 2 }, { 6, 0, 2 }, { 10, 0, 2 }, { 12, 0, 2 } };
    if (c == "m1b") return { { 0, 0, 2 }, { 3, 0, 2 }, { 6, 0, 3 }, { 9, 0, 2 }, { 12, 0, 2 }, { 14, 0, 2 } };
    if (c == "gate8") { std::vector<CompHit> v; for (int s = 0; s < 16; s += 2) v.push_back ({ s, 0, 1 }); return v; }
    if (c == "arp16") { std::vector<CompHit> v; static const int L[] { 1, 2, 3, 2 }; for (int s = 0; s < 16; ++s) v.push_back ({ s, L[s % 4], 2 }); return v; }
    if (c == "arp16b") { std::vector<CompHit> v; static const int L[] { 1, 3, 2, 3, 1, 2, 3, 2 }; for (int s = 0; s < 16; ++s) v.push_back ({ s, L[s % 8], 2 }); return v; }
    if (c == "broken8") return { { 0, 1, 6 }, { 2, 2, 6 }, { 4, 3, 6 }, { 6, 2, 4 }, { 8, 1, 6 }, { 10, 2, 6 }, { 12, 3, 4 }, { 14, 2, 2 } };
    if (c == "waltz3") return { { 0, 1, 6 }, { 2, 2, 6 }, { 4, 3, 6 }, { 6, 1, 6 }, { 8, 2, 6 }, { 10, 3, 6 }, { 12, 1, 4 }, { 14, 2, 2 } };
    if (c == "run16") { std::vector<CompHit> v; static const int L[] { 1, 2, 3, 3, 2, 1 }; for (int s = 0; s < 16; ++s) v.push_back ({ s, L[s % 6], 1 }); return v; }
    return { { 0, 0, 16 } };   // sus / swell
}
std::vector<std::array<int, 3>> bassPattern (const std::string& b)
{
    if (b == "pump") return { { 0, 6, 0 }, { 8, 6, 0 }, { 14, 2, 12 } };
    if (b == "octs") return { { 0, 2, 0 }, { 2, 2, 12 }, { 4, 2, 0 }, { 6, 2, 12 }, { 8, 2, 0 }, { 10, 2, 12 }, { 12, 2, 0 }, { 14, 2, 12 } };
    if (b == "off") return { { 2, 2, 0 }, { 6, 2, 0 }, { 10, 2, 0 }, { 14, 2, 0 } };
    if (b == "deep") return { { 0, 10, 0 }, { 10, 6, 0 } };
    if (b == "sparse") return { { 0, 12, 0 } };
    return { { 0, 16, 0 } };
}
const std::vector<std::vector<int>>& cellsOf (const std::string& c)
{
    static const std::map<std::string, std::vector<std::vector<int>>> C {
        { "hook", { { 0, 3, 6, 8, 10 }, { 0, 2, 3, 6, 10, 12 }, { 2, 4, 6, 8, 11 }, { 0, 3, 6, 10, 12, 14 } } }, { "long", { { 0, 8 }, { 0, 6, 12 }, { 4, 12 }, { 0 } } },
        { "house", { { 2, 6, 10, 14 }, { 0, 3, 6, 10, 12 }, { 2, 3, 6, 11, 14 } } }, { "simple", { { 0, 4, 8 }, { 0, 8, 12 }, { 4, 8 }, { 0, 6, 8, 12 } } },
        { "odd", { { 0, 5, 10, 13 }, { 1, 7, 12 }, { 0, 3, 7, 11, 14 }, { 2, 9 } } },
        { "run", { { 0, 2, 4, 6, 8, 10, 12, 14 }, { 0, 1, 2, 4, 6, 8, 9, 10, 12, 14 }, { 0, 2, 3, 4, 6, 8, 10, 11, 12, 14 } } } };
    return C.at (c);
}
Out piano (int si, const Key& k, int bars, uint32_t seed, uint32_t detail, int variation)
{
    const PStyle& S = pstyle (si);
    Rng r ((uint64) seed * 1013u + 5u), rm ((uint64) seed * 7u + (uint64) variation * 1013u + (uint64) (variation > 0 ? detail : 0) * 31u + 1u);
    const int nRows = bars * 16; Grid G { 16 };
    std::vector<int> sc; for (int x : scaleOf (k.minor)) sc.push_back (pcOf (x + k.tonic));
    auto inSc = [&] (int n) { return std::find (sc.begin(), sc.end(), pcOf (n)) != sc.end(); };
    const auto& all = k.minor ? S.minor : S.major;
    std::vector<std::vector<const char*>> dia;
    for (auto& pg : all) { bool ok = true; for (auto* t : pg) { const auto c = romanChord (t, k.minor, k.tonic); for (int x : c.tones) ok = ok && inSc (c.root + x); } if (ok) dia.push_back (pg); }
    const auto& prog = rp (r, dia.empty() ? all : dia);
    Out o; o.form = "A A' A B";
    auto hv = [&] { return (float) juce::jlimit (0.2, 1.0, (S.v0 + (S.v1 - S.v0) * (.55 + .45 * r.uni())) / 100.0); };
    auto put = [&] (std::vector<NoteEvent>& dst, int row, int midi, int len, float vel, int stutter = 0)
    {
        if (row < 0 || row >= nRows) return;
        midi = inR (midi, 36, 96); if (! inSc (midi)) midi += inSc (midi + 1) ? 1 : -1;   // (in the key: slips and rubs stay inside it)
        NoteEvent e; e.note = midi; e.beat = G.beat (row); e.length = G.beat (juce::jmax (1, juce::jmin (len, nRows - row))); e.velocity = juce::jlimit (0.05f, 1.0f, vel); e.stutter = stutter;
        if (S.hum > 0) { const double off = (r.uni() - .5) * 2 * S.hum; if (std::abs (off) > .02 && row > 0) e.beat += G.beat (off); }
        dst.push_back (e);
    };
    // chords per bar (hr 1) or per two bars (hr .5)
    const int per = S.hr >= 1 ? 16 : 32;
    struct CS { int s; RChord ch; std::vector<int> v; };
    std::vector<CS> chords; std::vector<int> prev;
    for (int s = 0, kk = 0; s < nRows; s += per, ++kk)
    {
        auto ch = romanChord (prog[(size_t) kk % prog.size()], k.minor, k.tonic);
        auto v = pvoicing (ch, prev.empty() ? nullptr : &prev, S.voice, r); prev = v; chords.push_back ({ s, ch, v });
    }
    auto chordAt = [&] (int s) -> const CS& { return chords[(size_t) juce::jlimit (0, (int) chords.size() - 1, s / per)]; };
    const auto comp = std::string (rp (r, S.comp)), bassP = std::string (rp (r, S.bass));
    for (auto& C : chords)
        for (int b = 0; b < per && C.s + b < nRows; b += 16)
        {
            const int o0 = C.s + b;
            for (auto& [st, ln, oc] : bassPattern (bassP)) { if (bassP == "sparse" && b > 0 && r.chance (.5)) continue; put (o.accomp, o0 + st, inR (C.ch.root, 36, 47) + oc, ln, hv() + .04f); }
            if (comp == "swell" && b > 0) continue;
            if (std::string (S.voice) == "cluster" || comp == "cluster")
            {
                const int hits = 3 + r.pick (4), rot = r.pick (16);
                for (int h = 0; h < hits; ++h) { const int st = (h * 16 / hits + rot) % 16; for (int l = 1; l <= 3; ++l) if (r.chance (.55)) put (o.accomp, o0 + st, C.v[(size_t) l - 1] + (r.chance (S.oct) ? 12 : 0), 2 + r.pick (7), hv()); }
                continue;
            }
            // BUILD: the gate doubles in the last quarter (the roll into the drop)
            auto pat = compPattern (comp);
            if (S.shape == 1 && o0 >= nRows * 3 / 4) { pat.clear(); for (int s2 = 0; s2 < 16; ++s2) pat.push_back ({ s2, 0, 1 }); }
            for (auto& h : pat) { const float acc = h.st % 4 == 0 ? 1.0f : .86f; if (h.lane == 0) for (int l = 1; l <= 3; ++l) put (o.accomp, o0 + h.st, C.v[(size_t) l - 1], h.len, hv() * acc); else put (o.accomp, o0 + h.st, C.v[(size_t) h.lane - 1], h.len, hv() * acc); }
        }
    // the melody: a 2-bar motif, then A A' A B (same rhythm, notes re-fitted to each chord, the last phrase new)
    const auto& cells = cellsOf (S.cells);
    auto onsetsFor = [&] (Rng& rr, double den) { std::vector<std::vector<int>> v; for (int b = 0; b < 2; ++b) { std::vector<int> x; for (int s : cells[(size_t) rr.pick ((int) cells.size())]) if (rr.uni() < .45 + den) x.push_back (s); v.push_back (x); } return v; };
    const auto mA = onsetsFor (rm, S.den), mB = onsetsFor (rm, S.den);
    std::vector<int> cA, cB; for (auto& b : mA) for (size_t i = 0; i < b.size(); ++i) cA.push_back (rm.pick (5) - 2); for (auto& b : mB) for (size_t i = 0; i < b.size(); ++i) cB.push_back (rm.pick (5) - 2);
    if (cA.empty()) cA.push_back (0); if (cB.empty()) cB.push_back (0);
    auto scaleNear = [&] (int n, int d) { std::vector<int> pool; for (int m = S.lo - 12; m <= S.hi + 12; ++m) if (inSc (m)) pool.push_back (m); int i = 0; while (i < (int) pool.size() && pool[(size_t) i] < n) ++i; if (i >= (int) pool.size()) i = (int) pool.size() - 1; return pool[(size_t) juce::jlimit (0, (int) pool.size() - 1, i + d)]; };
    auto chordNear = [&] (int n, const RChord& ch) { int best = n, bd = 99; for (int m = n - 7; m <= n + 7; ++m) { bool t = false; for (int x : ch.tones) t = t || pcOf (ch.root + x) == pcOf (m); if (t && std::abs (m - n) < bd) { bd = std::abs (m - n); best = m; } } return best; };
    static const int st0[] { 0, 4, 7 };
    int cur = inR (chordAt (0).ch.root + st0[rm.pick (3)], S.lo + 3, S.hi - 5);
    std::vector<std::pair<int, int>> mel;
    for (int bar = 0; bar < bars; bar += 2)
    {
        const int ph = (bar / 2) % 4; const bool useB = ph == 3;
        // BUILD climbs (a scale step per phrase) and fills in; BRIDGE thins out
        const int climb = S.shape == 1 ? bar / 2 : 0;
        const auto& on = useB ? mB : mA; const auto& ct = useB ? cB : cA; int ci = 0;
        for (int bi = 0; bi < 2; ++bi)
            for (int st : on[(size_t) bi])
            {
                const int s = (bar + bi) * 16 + st; if (s >= nRows) continue;
                if (S.shape == 2 && st % 8 != 0 && rm.chance (.4)) continue;
                const auto& ch = chordAt (s).ch;
                int n = scaleNear (cur, ct[(size_t) ci++ % ct.size()]); if (st % 4 == 0) n = chordNear (n, ch);
                if (S.chrom > 0 && rm.chance (S.chrom)) n += rm.chance (.5) ? 1 : -1;
                if (n > S.hi + climb) n -= 12; if (n < S.lo) n += 12; cur = n; mel.push_back ({ s, n + (climb > 0 ? scaleNear (n, climb) - n : 0) });
            }
        if (! mel.empty() && mel.back().first / 16 >= bar && (ph == 1 || ph == 3)) mel.back().second = chordNear (mel.back().second, chordAt (mel.back().first).ch);   // phrase ends land on the chord
    }
    for (size_t i = 0; i < mel.size(); ++i)
    {
        const int s = mel[i].first, n = mel[i].second, nx = i + 1 < mel.size() ? mel[i + 1].first : nRows, gap = nx - s;
        const std::string L = S.len;
        const int len = L == "short" ? juce::jmin (gap, 2) : L == "long" ? juce::jmin (gap, 12) : L == "mixed" ? juce::jmin (gap, 1 + rm.pick (6)) : juce::jmin (gap, 6);
        const int stut = rm.chance (S.fx) && (si == 7 || si == 9) ? 2 : 0;   // (PREPARED / SKITTER: the STUT echo as a ratchet)
        put (o.lead, s, n, len, hv() + .08f, stut);
        if (std::string (S.ctr) == "third" && len >= 4 && rm.chance (.5)) put (o.accomp, s, scaleNear (n, -2), len, hv() - .1f);
    }
    if (std::string (S.ctr) == "sparkle") for (auto& C : chords) if (rm.chance (.7)) put (o.accomp, C.s, C.v[2] + 12, 4, hv() - .14f);
    if (std::string (S.ctr) == "pluck") for (int s = 0; s < nRows; s += 2) if (rm.chance (.8)) put (o.accomp, s + 1, chordAt (s).v[(size_t) (s / 2) % 3] + 12, 1, hv() - .18f);
    // the chords as the take's harmony
    o.beatsPerChord = per / 4.0;
    for (auto& C : chords) { std::vector<int> iv = C.ch.tones; std::vector<int> v { inR (C.ch.root, 36, 47) }; for (int x : C.v) v.push_back (x); o.chords.push_back (makeChord (k, C.ch.root, iv, v)); }
    for (size_t i = 0; i < juce::jmin<size_t> (4, o.chords.size()); ++i) o.progName << (i ? " - " : "") << o.chords[i].name;
    return o;
}

// ---------------------------------------------------------------- SYNTH (Block Step / Ghost House synRows)
Out synth (int si, const Key& k, int bars, uint32_t seed, uint32_t detail, int variation)
{
    Rng r ((uint64) seed * 104729u + (uint64) variation * 7919u + (uint64) (variation > 0 ? detail : 0) + 3u);
    const int nRows = bars * 16; Grid G { 16 };
    const std::vector<int> sc = scaleOf (k.minor);
    auto deg = [&] (int d) { const int o = (int) std::floor (d / 7.0); return sc[(size_t) (((d % 7) + 7) % 7)] + 12 * o; };
    auto fold = [] (int p) { while (p > 24) p -= 12; while (p < -24) p += 12; return p; };
    const int R = k.tonic + 48;   // (the root; the take moves the lead to its own range afterwards)
    Out o;
    auto put = [&] (std::vector<NoteEvent>& dst, int row, int p, int len, float vel, int stutter = 0)
    {
        if (row < 0 || row >= nRows) return;
        NoteEvent e; e.note = R + fold (p); e.beat = G.beat (row); e.length = G.beat (juce::jmax (1, len)); e.velocity = vel; e.stutter = stutter; dst.push_back (e);
    };
    static const char* romanN[] { "i", "ii", "iii", "iv", "v", "vi", "vii" };
    if (si == 0 || si == 1)
    {   // acid / AFX acid: a note set of 3-5 scale notes (root weighted), a motif, its variations and an answer: A A' A B | A A'' A' B'
        static const int a1[] { 2, 4 }, a2[] { 4, 6 }, a3[] { 1, 3, 5, 6 };
        std::vector<int> set { 0, 0, deg (a1[r.pick (2)]), deg (a2[r.pick (2)]), deg (a3[r.pick (4)]) };
        const bool afx = si == 1; static const int lens[] { 5, 7, 11, 13 }; const int len = afx ? lens[r.pick (4)] : 16;
        struct St { bool on; int n; bool ac, sl; int rt; };
        std::vector<St> motif;
        for (int q = 0; q < len; ++q) motif.push_back ({ r.chance (afx ? .78 : .7) || q == 0, rp (r, set) + (r.chance (afx ? .32 : .22) ? 12 : 0) + (afx && r.chance (.08) ? 24 : 0) - (afx && r.chance (.06) ? 12 : 0),
                                                         r.chance (.3), r.chance (afx ? .26 : .2), afx && r.chance (.12) ? 1 + r.pick (3) : 0 });
        auto mut = [&] (const std::vector<St>& src, double amt) { auto v = src; for (auto& x : v) if (r.chance (amt)) { x.on = r.chance (.72) || (x.on && r.chance (.5)); if (r.chance (.6)) x.n = rp (r, set) + (r.chance (.22) ? 12 : 0); x.ac = r.chance (.3); x.sl = r.chance (afx ? .26 : .2); } return v; };
        const auto A1 = mut (motif, .22), A2 = mut (motif, .3), B = mut (motif, .65), B2 = mut (B, .3);
        const std::vector<const std::vector<St>*> SEQ { &motif, &A1, &motif, &B, &motif, &A2, &A1, &B2 };
        for (int s = 0; s < nRows; ++s)
        {
            const int bar = s / 16; const auto& mm = afx ? (bar % 4 == 3 ? B : motif) : *SEQ[(size_t) bar % 8]; const auto& m = mm[(size_t) ((afx ? s : s % 16) % len)];
            if (! m.on) continue;
            int nx = s + 1; while (nx < nRows) { const int b2 = nx / 16; const auto& m2 = afx ? (b2 % 4 == 3 ? B : motif) : *SEQ[(size_t) b2 % 8]; if (m2[(size_t) ((afx ? nx : nx % 16) % len)].on) break; ++nx; }
            const bool fill = bar % 4 == 3 && s % 16 >= 12 && r.chance (.5); static const int fl[] { 12, 7, -5 };
            put (o.lead, s, m.n + (fill ? fl[r.pick (3)] : 0), m.sl ? nx - s : 1, m.ac ? 1.0f : .78f, m.rt);
        }
        if (r.chance (.7)) { static const int offs[] { 2, 3, 6 }, steps[] { 3, 4, 6, 8 }; for (int s = offs[r.pick (3)]; s < nRows; s += steps[r.pick (4)]) put (o.accomp, s, rp (r, set) + 12, 1, .62f); }
        o.form = afx ? juce::String ("a motif of ") + juce::String (len) + " against 16" : "hook, its variations and an answer (A A' A B | A A'' A' B')";
    }
    else if (si == 2)
    {   // 101 roll: driving 16ths, root / octave / fifth, a few slides
        std::vector<int> pat; std::vector<int> ch { 0, 0, 12, 7, 12, deg (2), 0 }; for (int q = 0; q < 16; ++q) pat.push_back (q % 4 == 0 ? 0 : rp (r, ch));
        for (int s = 0; s < nRows; ++s)
        {
            const int bar = s / 16, q = s % 16; if (q % 2 == 1 && r.chance (.15)) continue;
            const int chord = std::vector<int> { 0, 0, deg (5) - 12, deg (3) - 12 }[(size_t) bar % 4];
            put (o.lead, s, chord + pat[(size_t) q], 1, q % 4 == 0 ? .96f : .72f);
        }
        o.form = "rolling 16ths";
    }
    else if (si == 3)
    {   // FM bells: a slow arpeggio phrase with rests; FM bass: sparse syncopated hits
        std::vector<int> ph; std::vector<int> ch { 0, 2, 4, 7, 9, 11, 14 }; for (int q = 0; q < 8; ++q) ph.push_back (rp (r, ch));
        static const int steps[] { 2, 3, 3, 4 }, lens[] { 2, 4, 6 }; static const float vels[] { .6f, .72f, .85f };
        for (int s = 0; s < nRows; s += steps[r.pick (4)]) put (o.lead, s, deg (ph[(size_t) (s >> 1) % 8]) + 12, lens[r.pick (3)], vels[r.pick (3)]);
        static const int bp[] { 0, 3, 6, 10, 11, 14 };
        for (int s = 0; s < nRows; ++s) for (int b : bp) if (s % 16 == b && r.chance (.8)) put (o.accomp, s, -12 + (r.chance (.25) ? 12 : 0), 1, s % 16 == 0 ? .96f : .76f);
        o.form = "bells over an FM bass";
    }
    // Ghost House's synth: chords from a progression (in key), then the lead / arps / stabs
    const std::vector<std::vector<int>> PM { { 0, 5, 2, 6 }, { 0, 5, 3, 4 }, { 0, 3, 5, 4 }, { 5, 6, 0, 0 }, { 0, 6, 5, 6 } }, PJ { { 0, 4, 5, 3 }, { 0, 5, 3, 4 }, { 3, 4, 0, 5 }, { 0, 3, 4, 3 } };
    const auto prog = rp (r, k.minor ? PM : PJ);
    auto chordAt = [&] (int bar) { const int d = prog[(size_t) bar % prog.size()]; return std::vector<int> { deg (d), deg (d + 2), deg (d + 4) }; };
    juce::String pn; for (size_t i = 0; i < prog.size(); ++i) pn << (i ? "-" : "") << romanN[prog[i]];
    if (si == 4)
    {   // the supersaw anthem: a 2-bar hook (scale degrees), answered in bars 3-4
        struct H { int at, d, l; }; std::vector<H> hook; static const int starts[] { 4, 7, 9 }; int n = starts[r.pick (3)];
        static const int offs[] { 0, 0, 2, 3 }, lens[] { 2, 3, 4 }, mv[] { -2, -1, 1, 2, 0, -3 };
        for (int q = 0; q < 8; ++q) { hook.push_back ({ q * 4 + offs[r.pick (4)], n, lens[r.pick (3)] }); n = juce::jlimit (2, 11, n + mv[r.pick (6)]); }
        for (int bar = 0; bar < bars; ++bar)
        {
            const auto ch = chordAt (bar); const int s0 = bar * 16;
            for (size_t j = 0; j < ch.size(); ++j) put (o.accomp, s0, ch[j] - 12, 15, .58f);
            for (auto& h : hook) if ((h.at / 16) == bar % 2) put (o.lead, s0 + h.at % 16, deg (h.d + (bar % 4 >= 2 && &h == &hook.back() ? -1 : 0)), h.l, .92f);
        }
        o.form = "a 2-bar hook, answered";
    }
    else if (si == 5)
    {   // plucks
        const std::vector<std::vector<int>> shapes { { 0, 1, 2, 1 }, { 0, 2, 1, 2 }, { 0, 1, 2, 3 }, { 2, 1, 0, 1 } }; const auto shape = rp (r, shapes);
        for (int s = 0; s < nRows; ++s)
        {
            auto ch = chordAt (s / 16); ch.push_back (ch[0] + 12);
            if (r.chance (.1) && s % 4) continue;
            put (o.lead, s, ch[(size_t) shape[(size_t) s % 4] % ch.size()] + ((s / 8) % 2 ? 12 : 0), 1, s % 4 == 0 ? .92f : .7f);
        }
        static const int st[] { 0, 0, 2 };
        for (int bar = 0; bar < bars; ++bar) for (int x : chordAt (bar)) put (o.accomp, bar * 16 + st[r.pick (3)], x - 12, 2, .52f);
        o.form = "arps on " + pn;
    }
    else if (si == 6)
    {   // stabs: a rhythm (off-beats, or a 3-3-2 push), the chord + its octave
        const std::vector<std::vector<int>> rhs { { 2, 6, 10, 14 }, { 0, 3, 6, 10, 12 }, { 2, 5, 8, 10, 14 }, { 3, 6, 11, 14 } }; const auto rhy = rp (r, rhs);
        for (int bar = 0; bar < bars; ++bar) { auto ch = chordAt (bar); ch.push_back (ch[0] + 12); for (int q : rhy) { if (r.chance (.08)) continue; for (int x : ch) put (o.accomp, bar * 16 + q, x, 1, q % 4 == 2 ? .9f : .78f); } }
        o.lead = monophonic (o.accomp, true); o.accomp.clear();
        o.form = "stabs on " + pn;
    }
    o.lead = monophonic (o.lead, true);
    // the harmony: Ghost House's progression one chord per bar (the acid / 101 / FM pages had none: their line implies it)
    o.beatsPerChord = 4.0;
    for (int bar = 0; bar < bars; ++bar) { const int d = prog[(size_t) bar % prog.size()]; const auto ch = chordAt (bar); std::vector<int> iv; for (int x : ch) iv.push_back (x - ch[0]); std::vector<int> v { R - 12 + ch[0] }; for (int x : ch) v.push_back (R + x); o.chords.push_back (makeChord (k, R + ch[0], iv, v)); juce::ignoreUnused (d); }
    o.progName = pn;
    return o;
}

// ---------------------------------------------------------------- REVERIE (Block Step revCompose, one section: the take is <= 16 bars)
struct REmo { const char* n; std::vector<std::vector<int>> progs, bprogs; std::vector<std::pair<int, double>> col; int vel, reg; bool pedal; std::vector<const char*> autoPat; };
const REmo& remo (int i)
{
    static const REmo E[6] {
        { "MELANCHOLIC", { { 0, 5, 2, 6 }, { 0, 3, 5, 4 }, { 0, 6, 5, 6 }, { 5, 6, 0, 0 }, { 0, 4, 5, 3 }, { 0, 0, 5, 5 } }, { { 2, 6, 5, 4 }, { 3, 5, 6, 0 }, { 5, 3, 0, 4 } }, { { 6, .5 }, { 1, .4 } }, 0, 0, false, { "arpeggio", "broken", "sparse" } },
        { "PEACEFUL", { { 0, 3, 0, 3 }, { 0, 5, 3, 0 }, { 0, 1, 3, 0 }, { 3, 0, 3, 4 } }, { { 5, 3, 1, 4 }, { 3, 1, 5, 0 } }, { { 1, .7 }, { 5, .4 } }, -6, 5, false, { "sparse", "arpeggio", "cascade" } },
        { "UNSETTLING", { { 0, 1, 0, 6 }, { 0, 5, 1, 0 }, { 0, 4, 5, 1 }, { 0, 3, 6, 0 } }, { { 1, 4, 1, 0 }, { 5, 1, 3, 1 } }, { { 1, .5 }, { 4, .4 } }, -2, -3, false, { "minimal", "sparse", "cascade" } },
        { "HOPEFUL", { { 0, 4, 5, 3 }, { 3, 0, 4, 5 }, { 5, 3, 0, 4 }, { 0, 3, 5, 4 }, { 0, 2, 3, 4 } }, { { 3, 4, 2, 5 }, { 1, 4, 0, 3 } }, { { 1, .5 }, { 6, .3 } }, 4, 3, false, { "broken", "arpeggio", "cascade" } },
        { "HAUNTING", { { 0, 3, 0, 6 }, { 0, 5, 3, 0 }, { 0, 6, 5, 6 }, { 0, 0, 3, 3 } }, { { 3, 6, 5, 4 }, { 6, 3, 6, 0 } }, { { 1, .6 }, { 3, .3 } }, -4, 2, true, { "sparse", "arpeggio", "minimal" } },
        { "NOSTALGIC", { { 0, 2, 3, 5 }, { 0, 5, 1, 4 }, { 3, 5, 0, 0 }, { 0, 4, 5, 2 } }, { { 5, 1, 4, 0 }, { 2, 5, 3, 5 } }, { { 6, .6 }, { 1, .4 } }, -2, 0, false, { "broken", "sparse", "arpeggio" } },
    };
    return E[juce::jlimit (0, 5, i)];
}
Out reverie (int emo, const char* patIn, const Key& k, int bars, uint32_t seed, uint32_t detail, int variation, double bpm)
{
    // the defaults of Block Step's REVERIE window: fragility 45, motion 60, harmony 45, humanity 55, unexpected 25, development 55, evolve 50
    const double f = .45, mo = .6, ha = .45, hu = .55, un = .25;
    Rng R ((uint64) seed * 2654435761u + 17u);
    auto D = [&] (int s) { return Rng ((uint64) (variation > 0 ? detail : seed) * 40503u + (uint64) s * 7919u + (uint64) variation * 104729u + 29u); };
    if (emo < 0) emo = R.pick (6);
    const REmo& E = remo (emo);
    const std::string pat0 = patIn != nullptr ? std::string (patIn) : std::string (E.autoPat[(size_t) R.pick ((int) E.autoPat.size())]);
    const auto& mode = scaleOf (k.minor); const int L = 7, root = k.tonic;
    const bool trip = pat0 == "arpeggio" || pat0 == "minimal"; const int BEAT = trip ? 6 : 4, BAR = BEAT * 4;
    const int tf = bpm > 135 ? 2 : 1; const double cd = (ha < .25 ? 4 : ha < .55 ? 2 : ha < .85 ? 1 : .5) * tf;
    const int n = bars * BAR; Grid G { BAR };
    auto chordOf = [&] (int c, int ext) { const int d = ((c % L) + L) % L, rr = mode[(size_t) d], th = pcOf (mode[(size_t) (d + 2) % L] - rr), fi = pcOf (mode[(size_t) (d + 4) % L] - rr);
                                          std::vector<int> iv { 0, th, fi }; if (ext > 0) iv.push_back (pcOf (mode[(size_t) (d + 6) % L] - rr)); return std::make_pair (rr, iv); };
    const auto& progA = E.progs[(size_t) R.pick ((int) E.progs.size())];
    std::vector<std::pair<int, std::vector<int>>> prog; for (int c : progA) prog.push_back (chordOf (c, ha > .35 ? 1 : 0));
    static const std::vector<std::vector<double>> MR { { 2, 1, 1 }, { 1.5, .5, 2 }, { 3, 1 }, { 1, 1, 2 }, { 2, 2 }, { 1, .5, .5, 2 }, { .5, .5, 1, 2 } };
    static const int MC[] { 1, -1, 2, -2, 0, 1, -1, 3 };
    auto motR = MR[(size_t) R.pick ((int) MR.size())]; std::vector<int> motC; for (size_t i = 0; i < motR.size(); ++i) motC.push_back (i == 0 ? 0 : MC[R.pick (8)]);
    auto scaleUp = [&] (int m, int steps) { int x = m; while (steps > 0) { ++x; if (inScale (x, k)) --steps; } while (steps < 0) { --x; if (inScale (x, k)) ++steps; } return x; };
    auto nearest = [&] (const std::vector<int>& pcs, int target, int lo, int hi) { int best = -1; for (int m = lo; m <= hi; ++m) if (std::find (pcs.begin(), pcs.end(), pcOf (m)) != pcs.end() && (best < 0 || std::abs (m - target) < std::abs (best - target))) best = m; return best < 0 ? target : best; };
    Out o; std::vector<NoteEvent> fig, mel;
    std::vector<std::vector<bool>> used ((size_t) n, std::vector<bool> (6, false));
    auto put = [&] (std::vector<NoteEvent>& dst, int lane, int at, int midi, double vel, double len, double off = 0)
    {
        if (at < 0 || at >= n) return;
        if (used[(size_t) at][(size_t) lane]) { if (lane >= 1 && lane <= 3) { lane = ! used[(size_t) at][1] ? 1 : ! used[(size_t) at][2] ? 2 : ! used[(size_t) at][3] ? 3 : -1; if (lane < 0) return; } else return; }
        used[(size_t) at][(size_t) lane] = true;
        NoteEvent e; e.note = juce::jlimit (21, 108, midi); e.beat = juce::jmax (0.0, G.beat (at + off)); e.length = G.beat (juce::jlimit (1.0, (double) BAR * 4, std::round (len))); e.velocity = (float) juce::jlimit (0.05, 1.0, vel / 100.0);
        dst.push_back (e);
    };
    const double baseV = 70 - 32 * f + E.vel; const int regUp = (int) std::lround (f * 6 + E.reg) >= 6 ? 12 : 0;
    std::vector<int> figCell; int figRot = 0; const int PH = juce::jmax (1, (int) std::lround (4 * cd));
    for (int b = 0; b < bars; ++b)
    {
        Rng r = D (b);
        const int bar0 = b * BAR, cIdx = (int) std::floor ((b % (int) (4 * cd)) / cd) % 4; const auto& ch = prog[(size_t) cIdx]; const bool chordStart = std::fmod (b, cd) == 0 || cd < 1;
        const int cr = pcOf (root + ch.first); std::vector<int> pcs; for (int x : ch.second) pcs.push_back (pcOf (cr + x));
        std::vector<int> tones = pcs; for (auto& [dg, w] : E.col) if (r.chance (w * (.3 + ha * .5))) { const int pc = pcOf (root + mode[(size_t) dg % L]); if (std::find (tones.begin(), tones.end(), pc) == tones.end()) tones.push_back (pc); }
        const double sw = std::sin (juce::MathConstants<double>::pi * juce::jmin (1.0, ((b % PH) + .5) / PH));
        const double vArc = baseV + 10 * sw * (1 - f * .5);
        auto hum = [&] { return juce::jlimit (-.45, .45, (r.uni() - .5) * .3 * hu); };
        auto vh = [&] (double v) { return v + (r.uni() - .5) * 18 * hu; };
        const int bassM = 33 + pcOf ((E.pedal ? root : cr) - 33);
        if (chordStart || b % 2 == 0) put (o.accomp, 0, bar0, bassM, vh (vArc - 6), cd >= 1 ? BAR * juce::jmin (cd, 2.0) : BAR / 2.0, hum());
        std::vector<int> vc; const int center = 60 + regUp + (pat0 == "cascade" ? 12 : 0);
        for (int pc : tones) vc.push_back (nearest ({ pc }, center - 5 + (int) vc.size() * 4, center - 12, center + 14)); std::sort (vc.begin(), vc.end());
        if (pat0 == "arpeggio" || pat0 == "minimal")
        {
            const int step = mo > .75 && bpm < 110 ? 1 : 2;
            static const std::vector<std::vector<int>> F { { 0, 1, 2, 1, 2, 3 }, { 0, 1, 2, 3, 2, 1 }, { 0, 2, 1, 3, 1, 2 }, { 0, 1, 2, 0, 1, 2 } };
            if (figCell.empty() || (chordStart && pat0 == "arpeggio")) figCell = F[(size_t) r.pick (4)];
            auto cell = figCell;
            if (pat0 == "minimal") { const int grow = 2 + (int) std::floor (juce::jmin (1.0, (b + 1.0) / juce::jmax (1, bars)) * 4); cell.resize ((size_t) juce::jmax (2, juce::jmin ((int) figCell.size(), grow))); }
            for (int q = 0, j = figRot; q < BAR; q += step, ++j)
            {
                if (r.chance ((1 - mo) * .55 * (q % BEAT ? 1 : .2))) continue;
                if (r.chance (f * .3 * (q % BEAT ? 1 : 0))) continue;
                const int ci = cell[(size_t) j % cell.size()]; int m = vc[(size_t) ci % vc.size()] + (ci >= (int) vc.size() ? 12 : 0);
                if (r.chance (un * .07)) m += r.chance (.5) ? 12 : -12;
                const int acc = j % (int) cell.size() == 0 ? 8 : 0;
                put (fig, 1 + (j % 3), bar0 + q, m, vh (vArc - 8 + acc - (q % BEAT ? 6 : 0)), step * 3, hum());
            }
            if (pat0 == "minimal") figRot = (figRot + (r.chance (.25) ? 1 : 0)) % 12;   // the phase drifts
            const int lh[] { bassM + 12, bassM + 19, bassM + 12 + (std::find (pcs.begin(), pcs.end(), pcOf (cr + 3)) != pcs.end() ? 3 : 4) };
            for (int q = BEAT / 2, qq = 0; q < BAR; q += BEAT / 2, ++qq) if (r.chance (.35 + mo * .6) && r.uni() > f * .3) put (o.accomp, 5, bar0 + q, lh[qq % 3], vh (vArc - 16), BEAT, hum());
        }
        else if (pat0 == "broken")
        {
            static const int alb[] { 0, 2, 1, 2 };
            const int lhv[] { bassM + 12, nearest ({ pcOf (cr + 7) }, bassM + 19, bassM + 14, bassM + 24), nearest ({ pcs[1] }, bassM + 16, bassM + 12, bassM + 24) };
            for (int q = 0, qq = 0; q < BAR; q += 2, ++qq) { if (q == 0 && chordStart) continue; if (r.chance ((1 - mo) * .5) && q % 4) continue; put (o.accomp, qq % 2 ? 5 : 1, bar0 + q, lhv[alb[qq % 4]], vh (vArc - 14 - (q % 4 ? 4 : 0)), 3, hum()); }
            if (chordStart && r.chance (.6)) for (size_t j = 0; j < 2 && j < vc.size(); ++j) put (o.accomp, 2 + (int) j, bar0, vc[vc.size() - 2 + j] + 12, vh (vArc - 14), BAR * juce::jmax (1.0, cd) * .9, .06 * (j + 1) * (.5 + hu));
        }
        else if (pat0 == "sparse")
        {
            if (chordStart) for (size_t j = 0; j < vc.size() && j < 3; ++j) if (r.uni() > f * .25 * j) put (o.accomp, 1 + (int) j, bar0, vc[j], vh (vArc - 12 - j * 3), BAR * juce::jmax (1.0, cd), juce::jmin (.45, .07 * j * (.4 + hu)));
            static const double bellAt[] { 2, 3, 1.5 };
            if (r.chance (.25 + f * .4)) put (fig, 5, bar0 + (int) (BEAT * bellAt[r.pick (3)]), nearest (tones, 84 + regUp, 79, 100), vh (vArc - 18), BEAT * 3, hum());
        }
        else if (pat0 == "cascade")
        {
            const int runs = mo > .55 ? 2 : 1;
            for (int u = 0; u < runs; ++u)
            {
                if (u && r.chance (f * .3)) continue;
                const int st = u * BAR / 2 + (r.chance (.3) ? BEAT / 2 : 0); int m = nearest (tones, 86 + regUp + (r.chance (.3) ? 7 : 0), 76, 102);
                const int len2 = BEAT + r.pick (BEAT);
                for (int q = 0; q < len2 && st + q < BAR; ++q) { put (fig, 1 + (q % 3), bar0 + st + q, m, vh (vArc - 6 - q * 2), 4, hum()); int s2 = m - 1; while (std::find (tones.begin(), tones.end(), pcOf (s2)) == tones.end() && s2 > m - 6) --s2; m = r.chance (.2) ? scaleUp (m, -1) : s2; }
                put (fig, 1 + (len2 % 3), bar0 + st + len2, vc[0] + 12, vh (vArc - 4), BEAT * 2, hum());
            }
        }
        // the melody (fragments; every second bar)
        const bool melOn = (pat0 == "sparse" || pat0 == "broken") ? true : r.chance (.35 + (1 - mo) * .5);
        if (melOn && b % 2 == 0 && r.uni() > f * .35)
        {
            int t = bar0 + (r.chance (.3) ? BEAT : 0); int m = nearest (pcs, 72 + regUp + (pat0 == "cascade" ? 5 : 0), 64 + regUp, 90);
            if (b >= PH && r.chance (.3)) { motC[(size_t) r.pick ((int) motC.size())] = MC[r.pick (5)]; }   // EVOLVE: the theme's second time changes a note
            for (size_t q = 0; q < motR.size() && t < bar0 + 2 * BAR && t < n; ++q)
            {
                if (q) m = scaleUp (m, motC[q]);
                if (r.chance (un * .1)) { put (mel, 4, t, m + 1, vh (vArc - 2), juce::jmax (1, BEAT / 2), hum()); t += juce::jmax (1, BEAT / 2); }   // a rub that resolves
                const int d = juce::jmax (1, (int) std::lround (motR[q] * BEAT * (1 + f * .6)));
                if (! (q && r.chance (f * .3))) put (mel, 4, t, m, vh (vArc + 4 - (double) q * 2), d, hum());
                t += d;
            }
        }
    }
    // what INSTRUMENT sings: the melody when the texture is a melody's (BROKEN / SPARSE), else the figure itself
    inKey (fig, k); inKey (mel, k); inKey (o.accomp, k);
    if (pat0 == "broken" || pat0 == "sparse")
    {   // the melody, with the high bells / the top of the left hand filling its silences (one singable line)
        std::vector<NoteEvent> all = mel; for (auto& e : fig) all.push_back (e);
        if (all.size() < 8) for (auto& e : o.accomp) if (e.note >= 55) all.push_back (e);
        o.lead = monophonic (all, true);
        for (auto& e : fig) o.accomp.push_back (e);
    }
    else { o.lead = monophonic (fig, true); for (auto& e : mel) o.accomp.push_back (e); }
    o.lead = monophonic (o.lead, true);
    o.beatsPerChord = 4.0 * cd;
    for (int b = 0; b * o.beatsPerChord < bars * 4.0; ++b)
    {
        const auto& ch = prog[(size_t) b % 4]; const int cr = root + ch.first;
        std::vector<int> v { 36 + pcOf (cr - 36) }; for (int x : ch.second) v.push_back (inR (cr + x, 55, 67));
        o.chords.push_back (makeChord (k, cr, ch.second, v));
    }
    for (size_t i = 0; i < juce::jmin<size_t> (4, o.chords.size()); ++i) o.progName << (i ? " - " : "") << o.chords[i].name;
    static const std::map<std::string, const char*> PN { { "arpeggio", "ARPEGGIOS" }, { "broken", "BROKEN CHORDS" }, { "sparse", "SPARSE MELODY" }, { "cascade", "CASCADES" }, { "minimal", "MINIMAL" } };
    o.form = juce::String ("REVERIE ") + E.n + " - " + PN.at (pat0);
    return o;
}

// ---------------------------------------------------------------- the learned brains (n-gram models)
std::string gunzip (const unsigned char* d, size_t n)
{
    juce::MemoryInputStream mi (d, n, false);
    juce::GZIPDecompressorInputStream gz (&mi, false, juce::GZIPDecompressorInputStream::gzipFormat);
    return gz.readEntireStreamAsString().toStdString();
}
struct NGram
{
    std::vector<std::string> v;                                        // tokens
    std::unordered_map<std::string, std::vector<std::pair<int, double>>> t;   // context ("id id") -> (token index, weight)
    int idOf (const std::string& tok) const { for (size_t i = 0; i < v.size(); ++i) if (v[i] == tok) return (int) i; return -1; }
    static std::string b36 (int i) { if (i == 0) return "0"; std::string s; while (i > 0) { const int d = i % 36; s.insert (s.begin(), (char) (d < 10 ? '0' + d : 'a' + d - 10)); i /= 36; } return s; }
    static NGram from (const juce::var& M)
    {
        NGram g;
        if (auto* a = M["v"].getArray()) for (auto& x : *a) g.v.push_back (x.toString().toStdString());
        if (auto* o = M["t"].getDynamicObject())
            for (auto& p : o->getProperties())
            {
                std::vector<std::pair<int, double>> d;
                for (auto& item : juce::StringArray::fromTokens (p.value.toString(), ",", ""))
                {
                    const int j = item.lastIndexOfChar (':'); if (j < 0) continue;
                    d.push_back ({ (int) item.substring (0, j).getHexValue32() * 0 + (int) std::strtol (item.substring (0, j).toRawUTF8(), nullptr, 36), item.substring (j + 1).getDoubleValue() });
                }
                const auto key = p.name.toString(); g.t[key == "__" ? std::string() : key.toStdString()] = std::move (d);
            }
        return g;
    }
    // the next token from the longest known context (backing off), sampled by learned weight
    int next (const std::vector<int>& hist, int order, Rng& r) const
    {
        for (int kk = juce::jmin (order, (int) hist.size()); kk >= 0; --kk)
        {
            std::string key; for (int i = (int) hist.size() - kk; i < (int) hist.size(); ++i) key += (key.empty() ? "" : " ") + b36 (hist[(size_t) i]);
            auto it = t.find (key); if (it == t.end() || it->second.empty()) continue;
            double tot = 0; for (auto& [id, p] : it->second) tot += juce::jmax (p, .5);
            double x = r.uni() * tot; for (auto& [id, p] : it->second) { x -= juce::jmax (p, .5); if (x <= 0) return id; }
            return it->second.back().first;
        }
        return -1;
    }
    std::vector<std::string> seq (int order, int n, Rng& r, const std::function<bool (const std::string&)>& ok = {}) const
    {
        const int START = idOf ("^"), END = idOf ("$");
        std::vector<int> hist ((size_t) order, START); std::vector<std::string> out;
        for (int guard = 0; (int) out.size() < n && guard < n * 20; ++guard)
        {
            const int id = next (hist, order, r);
            if (id < 0 || id == END) { hist.assign ((size_t) order, START); continue; }
            const auto& tok = v[(size_t) id];
            if (ok && ! ok (tok)) { hist.assign ((size_t) order, START); continue; }
            out.push_back (tok); hist.push_back (id); if ((int) hist.size() > order) hist.erase (hist.begin());
        }
        return out;
    }
};
struct Brains
{
    NGram popC[2], popM[2]; int popOC = 4, popOM = 3; std::vector<std::pair<int, double>> compL, compR;
    NGram acid; int acidO = 2;
    NGram parC, parM; int parOC = 3, parOM = 3; std::vector<std::pair<int, double>> parLh;
};
const Brains& brains()
{
    static const Brains B = []
    {
        Brains b;
        // (JUCE's JSON reader refuses an empty property name: the models' empty context, the order-0 fallback, is renamed "__")
        auto parse = [] (std::string js) { juce::String t (juce::String::fromUTF8 (js.data(), (int) js.size())); t = t.replace ("{\"\":", "{\"__\":").replace (",\"\":", ",\"__\":"); return juce::JSON::parse (t); };
        const auto pop = parse (gunzip (k_brain_pop, sizeof (k_brain_pop)));
        b.popOC = (int) pop["orderC"]; b.popOM = (int) pop["orderM"];
        for (int m = 0; m < 2; ++m) { const char* md = m ? "m" : "M"; b.popC[m] = NGram::from (pop["chords"][md]); b.popM[m] = NGram::from (pop["mel"][md]); }
        auto list = [] (const juce::var& v, std::vector<std::pair<int, double>>& out) { if (auto* a = v.getArray()) for (auto& x : *a) if (x.isArray() && x.size() >= 2) out.push_back ({ (int) x[0], (double) x[1] }); };
        list (pop["comp"]["L"], b.compL); list (pop["comp"]["R"], b.compR);
        const auto ac = parse (gunzip (k_brain_acid, sizeof (k_brain_acid)));
        b.acidO = (int) ac["o"]; b.acid = NGram::from (ac["m"]);
        const auto pa = parse (gunzip (k_brain_parlour, sizeof (k_brain_parlour)));
        b.parOC = (int) pa["orderC"]; b.parOM = (int) pa["orderM"]; b.parC = NGram::from (pa["chords"]["x"]); b.parM = NGram::from (pa["mel"]["x"]);
        list (pa["lh"], b.parLh);
        return b;
    }();
    return B;
}
struct BChord { int deg = 0; std::vector<int> iv { 0, 4, 7 }; };
BChord bChord (const std::string& tok)
{
    static const std::map<std::string, std::vector<int>> Q { { "M", { 0, 4, 7 } }, { "m", { 0, 3, 7 } }, { "s2", { 0, 2, 7 } }, { "m7", { 0, 3, 7, 10 } }, { "M7", { 0, 4, 7, 11 } }, { "s4", { 0, 5, 7 } },
        { "7", { 0, 4, 7, 10 } }, { "7s4", { 0, 5, 7, 10 } }, { "M6", { 0, 4, 7, 9 } }, { "d", { 0, 3, 6 } }, { "m7b5", { 0, 3, 6, 10 } }, { "a", { 0, 4, 8 } }, { "m6", { 0, 3, 7, 9 } }, { "d7", { 0, 3, 6, 9 } }, { "mM7", { 0, 3, 7, 11 } } };
    size_t i = 0; while (i < tok.size() && std::isdigit ((unsigned char) tok[i])) ++i;
    BChord c; if (i == 0) return c; c.deg = std::atoi (tok.substr (0, i).c_str()); auto it = Q.find (tok.substr (i)); if (it != Q.end()) c.iv = it->second; return c;
}
std::vector<int> bVoice (int root, const BChord& ch, int center, const std::vector<int>* prev)
{
    std::vector<int> pcs; for (size_t i = 0; i < ch.iv.size() && i < 4; ++i) pcs.push_back (pcOf (root + ch.deg + ch.iv[i]));
    std::vector<int> best; int bs = 1 << 30;
    for (size_t inv = 0; inv < pcs.size(); ++inv)
    {
        std::vector<int> v; int last = center - 7;
        for (int q = 0; q < 3; ++q) { const int pc = pcs[(inv + (size_t) q) % pcs.size()]; int m = last + pcOf (pc - pcOf (last)); if (m <= last) m += 12; v.push_back (m); last = m; }
        int sc = 0; if (prev != nullptr) for (int i = 0; i < 3; ++i) sc += std::abs (v[(size_t) i] - (*prev)[(size_t) i]); else sc = std::abs (v[1] - center);
        if (sc < bs) { bs = sc; best = v; }
    }
    return best;
}
int bPick (Rng& r, const std::vector<std::pair<int, double>>& list) { double tot = 0; for (auto& x : list) tot += x.second; double x = r.uni() * tot; for (auto& it : list) { x -= it.second; if (x <= 0) return it.first; } return list.empty() ? 1 : list.front().first; }
Out popBrain (const Key& k, int bars, uint32_t seed, uint32_t detail, int variation)
{
    const auto& B = brains(); const int md = k.minor ? 1 : 0;
    Rng r ((uint64) seed * 6151u + 11u), rv ((uint64) (variation > 0 ? detail : seed) * 92821u + (uint64) variation * 13u + 7u);
    const int n = bars * 16; Grid G { 16 };
    auto chordsT = B.popC[md].seq (B.popOC, bars, r);
    std::vector<BChord> chords; for (auto& t : chordsT) chords.push_back (bChord (t)); while ((int) chords.size() < bars) chords.push_back (chords.empty() ? BChord() : chords[chords.size() % juce::jmax<size_t> (1, chords.size())]);
    Out o; std::vector<int> prevV;
    auto bMask = [] (int m) { std::vector<int> v; for (int s = 0; s < 16; ++s) if (m & (1 << s)) v.push_back (s); return v; };
    auto put = [&] (std::vector<NoteEvent>& dst, int s, int m, double len, float vel) { if (s < 0 || s >= n) return; NoteEvent e; e.note = m; e.beat = G.beat (s); e.length = G.beat (juce::jmax (1.0, len)); e.velocity = vel; dst.push_back (e); };
    for (int b = 0; b < bars; ++b)
    {
        const auto& ch = chords[(size_t) b]; const int o0 = b * 16, root = k.tonic + ch.deg, bass = 36 + pcOf (root - 36);
        auto v3 = bVoice (k.tonic, ch, 64, prevV.empty() ? nullptr : &prevV); prevV = v3;
        for (int half = 0; half < 2; ++half)
        {   // POP909's two hands, each a learned rhythm (one song bar = two of ours)
            std::vector<int> Lh, Rh; for (int s : bMask (bPick (rv, B.compL))) if (s % 2 == 0) Lh.push_back (s); for (int s : bMask (bPick (rv, B.compR))) if (s % 2 == 0) Rh.push_back (s);
            for (size_t q = 0; q < Lh.size(); ++q) put (o.accomp, o0 + half * 8 + Lh[q] / 2, bass + (q % 2 && rv.chance (.4) ? 12 : 0), ((q + 1 < Lh.size() ? Lh[q + 1] : 16) - Lh[q]) / 2.0, .7f);
            for (size_t q = 0; q < Rh.size(); ++q) for (size_t j = 0; j < v3.size(); ++j) put (o.accomp, o0 + half * 8 + Rh[q] / 2, v3[j], ((q + 1 < Rh.size() ? Rh[q + 1] : 16) - Rh[q]) / 2.0, .56f - .05f * (float) j);
        }
        std::vector<int> v { bass }; for (int x : v3) v.push_back (x);
        o.chords.push_back (makeChord (k, root, ch.iv, v));
    }
    // the melody: tokens "degree:length:gap:direction" in 16ths (x2: one song bar = two of ours), the octave following the learned direction
    const auto toks = B.popM[md].seq (B.popOM, 400, rv);
    int t = 0, prev = 74; const int lo = 64, hi = 84;
    for (auto& tok : toks)
    {
        const auto parts = juce::StringArray::fromTokens (juce::String (tok), ":", ""); if (parts.size() < 4) continue;
        t += juce::jmin (16, parts[2].getIntValue() * 2); if (t >= n) break;
        const int pcW = pcOf (k.tonic + parts[0].getIntValue());
        std::vector<int> cand; for (int m = lo; m <= hi; ++m) if (pcOf (m) == pcW) cand.push_back (m);
        if (cand.empty()) continue;
        std::vector<int> want; for (int m : cand) if ((parts[3] == "u" && m > prev) || (parts[3] == "d" && m < prev) || (parts[3] == "s" && m == prev)) want.push_back (m);
        const auto& pool = want.empty() ? cand : want; int m = pool[0]; for (int x : pool) if (std::abs (x - prev) < std::abs (m - prev)) m = x;
        const auto& ch = chords[(size_t) juce::jlimit (0, bars - 1, t / 16)];
        if (t % 8 == 0)
        {   // a note on the beat that rubs a half step against the chord moves onto the chord tone
            bool tone = false; int nearD = 99, nearPc = 0; for (int x : ch.iv) { const int pc = pcOf (k.tonic + ch.deg + x); if (pc == pcOf (m)) tone = true; const int d = juce::jmin (pcOf (pc - pcOf (m)), pcOf (pcOf (m) - pc)); if (d < nearD) { nearD = d; nearPc = pc; } }
            if (! tone && nearD == 1) m += pcOf (nearPc - pcOf (m) + 6) - 6;
        }
        const int len = juce::jmax (1, juce::jmin (32, parts[1].getIntValue() * 2));
        put (o.lead, t, m, len, .72f); prev = m; t += len;
    }
    inKey (o.lead, k); inKey (o.accomp, k);
    o.lead = monophonic (o.lead, true); o.beatsPerChord = 4.0;
    for (size_t i = 0; i < juce::jmin<size_t> (4, o.chords.size()); ++i) o.progName << (i ? " - " : "") << o.chords[i].name;
    o.form = "learned from 900 pop songs: every bar new";
    return o;
}
// PARLOUR PIANO (Block Step's brainPiano, the learned piano): one learned chord per bar, the left hand a learned bar rhythm (the bass
// on its first hit, the chord on the rest), the melody from the learned melody model (x2 time: long holds and rests). slow = NOCTURNE.
Out parlour (const Key& k, int bars, uint32_t seed, uint32_t detail, int variation, bool slow)
{
    const auto& B = brains();
    Rng r ((uint64) seed * 7727u + 21u), rv ((uint64) (variation > 0 ? detail : seed) * 3613u + (uint64) variation * 17u + 5u);
    const int n = bars * 16; Grid G { 16 };
    const auto chordsT = B.parC.seq (B.parOC, bars, r);
    std::vector<BChord> chords; for (auto& t : chordsT) chords.push_back (bChord (t)); while ((int) chords.size() < bars) chords.push_back (chords.empty() ? BChord() : chords[chords.size() % juce::jmax<size_t> (1, chords.size())]);
    auto bMask = [] (int m) { std::vector<int> v; for (int s2 = 0; s2 < 16; ++s2) if (m & (1 << s2)) v.push_back (s2); return v; };
    auto put = [&] (std::vector<NoteEvent>& dst, int s2, int m, double len, float vel) { if (s2 < 0 || s2 >= n) return; NoteEvent e; e.note = m; e.beat = G.beat (s2); e.length = G.beat (juce::jmax (1.0, len)); e.velocity = vel; dst.push_back (e); };
    Out o; std::vector<int> prevV;
    for (int b = 0; b < bars; ++b)
    {
        const auto& ch = chords[(size_t) b]; const int o0 = b * 16, root = k.tonic + ch.deg, bass = 36 + pcOf (root - 36);
        auto v3 = bVoice (k.tonic, ch, 64, prevV.empty() ? nullptr : &prevV); prevV = v3;
        auto hits = bMask (bPick (rv, B.parLh)); if (hits.empty()) hits = { 0, 6 };
        if (slow) hits = { hits.front() };
        for (size_t q = 0; q < hits.size(); ++q)
        {
            const int s2 = hits[q], nx = q + 1 < hits.size() ? hits[q + 1] : 16, l = juce::jmax (1, nx - s2);
            if (q == 0) put (o.accomp, o0 + s2, bass, slow ? 16 : l + 2, .62f);
            else for (size_t j = 0; j < v3.size(); ++j) put (o.accomp, o0 + s2, v3[j], l, .44f - .04f * (float) j);
        }
        if (slow) for (size_t j = 0; j < v3.size(); ++j) put (o.accomp, o0 + 4, v3[j], 12, .4f - .04f * (float) j);
        std::vector<int> v { bass }; for (int x : v3) v.push_back (x);
        o.chords.push_back (makeChord (k, root, ch.iv, v));
    }
    // the melody: tokens "degree:length:gap:direction" in 16ths (x2, NOCTURNE x3), the octave following the learned direction
    const auto toks = B.parM.seq (B.parOM, 400, rv);
    const int stretch = slow ? 3 : 2, capL = slow ? 16 : 12, capG = slow ? 10 : 6;
    int t = 0, prev = 76; const int lo = 67, hi = 86;
    for (auto& tok : toks)
    {
        const auto parts = juce::StringArray::fromTokens (juce::String (tok), ":", ""); if (parts.size() < 4) continue;
        t += juce::jmin (capG, parts[2].getIntValue() * stretch); if (t >= n) break;
        const int pcW = pcOf (k.tonic + parts[0].getIntValue());
        std::vector<int> cand; for (int m = lo; m <= hi; ++m) if (pcOf (m) == pcW) cand.push_back (m);
        if (cand.empty()) continue;
        std::vector<int> want; for (int m : cand) if ((parts[3] == "u" && m > prev) || (parts[3] == "d" && m < prev) || (parts[3] == "s" && m == prev)) want.push_back (m);
        const auto& pool = want.empty() ? cand : want; int m = pool[0]; for (int x : pool) if (std::abs (x - prev) < std::abs (m - prev)) m = x;
        const auto& ch = chords[(size_t) juce::jlimit (0, bars - 1, t / 16)];
        if (t % (4 * stretch) == 0)
        {   // a note on the beat that rubs a half step against the chord moves onto the chord tone
            bool tone = false; int nearD = 99, nearPc = 0; for (int x : ch.iv) { const int pc = pcOf (k.tonic + ch.deg + x); if (pc == pcOf (m)) tone = true; const int d = juce::jmin (pcOf (pc - pcOf (m)), pcOf (pcOf (m) - pc)); if (d < nearD) { nearD = d; nearPc = pc; } }
            if (! tone && nearD == 1) m += pcOf (nearPc - pcOf (m) + 6) - 6;
        }
        const int len = juce::jmax (1, juce::jmin (capL, parts[1].getIntValue() * stretch));
        put (o.lead, t, m, len, .62f); prev = m; t += len;
    }
    inKey (o.lead, k); inKey (o.accomp, k);
    o.lead = monophonic (o.lead, true); o.beatsPerChord = 4.0;
    for (size_t i = 0; i < juce::jmin<size_t> (4, o.chords.size()); ++i) o.progName << (i ? " - " : "") << o.chords[i].name;
    o.form = slow ? "learned parlour piano, nocturne: one bar at a time, every bar new" : "learned parlour piano: every bar new";
    return o;
}
Out acidBrain (const Key& k, int bars, uint32_t seed, uint32_t detail, int variation)
{
    const auto& B = brains(); const auto& M = B.acid;
    Rng r ((uint64) seed * 31337u + 3u), rv ((uint64) (variation > 0 ? detail : seed) * 4241u + (uint64) variation * 101u + 9u);
    const int START = M.idOf ("~");
    auto line = [&] (Rng& rr, int steps)
    {
        struct S { int note; bool ac, sl, gate; }; std::vector<S> out;
        std::vector<std::string> starts; for (auto& kv : M.t) { auto sp = juce::StringArray::fromTokens (juce::String (kv.first), " ", ""); if (sp.size() == 2 && (int) std::strtol (sp[0].toRawUTF8(), nullptr, 36) == START) starts.push_back (kv.first); }
        std::sort (starts.begin(), starts.end());
        auto toHist = [] (const std::string& s) { std::vector<int> h; for (auto& x : juce::StringArray::fromTokens (juce::String (s), " ", "")) h.push_back ((int) std::strtol (x.toRawUTF8(), nullptr, 36)); return h; };
        std::vector<int> hist = starts.empty() ? std::vector<int> { START, START } : toHist (starts[(size_t) rr.pick ((int) starts.size())]);
        for (int g = 0; (int) out.size() < steps && g < steps * 10; ++g)
        {
            const int id = M.next (hist, B.acidO, rr);
            if (id < 0) { hist = starts.empty() ? std::vector<int> { START, START } : toHist (starts[(size_t) rr.pick ((int) starts.size())]); continue; }
            hist.push_back (id); if (hist.size() > 2) hist.erase (hist.begin());
            const auto& tok = M.v[(size_t) id]; if (tok == "~" || tok.size() < 8) continue;
            // "<n>!<ac>$<sl>#<gate>="
            if (tok[1] != '!' || tok[3] != '$' || tok[5] != '#') continue;
            out.push_back ({ (int) (unsigned char) tok[0] - 82 + 36 + k.tonic, tok[2] == '1', tok[4] == '1', tok[6] == '1' });
        }
        return out;
    };
    const auto p1 = line (r, 16), p2 = line (rv, 16);
    Out o; const int n = bars * 16; Grid G { 16 };
    for (int s = 0; s < n; ++s)
    {
        const auto& pat = (s / 32) % 2 ? p2 : p1; if (pat.empty()) continue; const auto& st = pat[(size_t) s % pat.size()]; if (! st.gate) continue;
        int nx = s + 1; while (nx < n) { const auto& p = (nx / 32) % 2 ? p2 : p1; if (! p.empty() && p[(size_t) nx % p.size()].gate) break; ++nx; }
        NoteEvent e; e.note = st.note; e.beat = G.beat (s); e.length = G.beat (st.sl ? nx - s : 1); e.velocity = st.ac ? 1.0f : .76f; o.lead.push_back (e);
    }
    inKey (o.lead, k); o.lead = monophonic (o.lead, true);
    o.beatsPerChord = 4.0;
    for (int b = 0; b < bars; ++b) { const int root = k.tonic + 48; o.chords.push_back (makeChord (k, root, { 0, k.minor ? 3 : 4, 7 }, { root - 12, root, root + (k.minor ? 3 : 4), root + 7 })); }
    o.progName = o.chords.front().name; o.form = "learned 303 lines: a pattern, then its answer";
    return o;
}

// ---------------------------------------------------------------- ARPEZATOR (its engine, linked)
std::mutex& arpLock() { static std::mutex m; return m; }
void arpRecipeOnce()
{
    static std::once_flag once;
    std::call_once (once, []
    {
        arp::Recipe R;
        if (arprecipe::fromVar (juce::JSON::parse (juce::String (gunzip (k_arp_recipe, sizeof (k_arp_recipe)))), R)) arp::setRecipe (R);
    });
}
Out arpez (Kind kind, int a, const Key& k, int bars, uint32_t seed, uint32_t detail, int variation)
{
    std::lock_guard<std::mutex> l (arpLock());
    arpRecipeOnce();
    arp::Idea I;
    int mood = kind == KTrap ? arp::Trap : kind == KHipHop ? arp::HipHop : kind == KHookLab ? (k.minor ? arp::Midnight : arp::Sunrise) : a;
    I.mode = mood; I.seed = seed;
    arp::applyModeDefaults (I);
    if (kind == KTrap || kind == KHipHop) I.flavour = a;
    if (kind == KHookLab) I.style = a;
    I.evolve = bars > 8; I.evolveBars = 16;
    arp::newIdea (I);
    if (variation > 0)
    {   // VARY: the same chords and figure, the lane composed again from the variation's seed (ArpEZator's own NEW on a lane)
        I.laneVar[(size_t) (kind == KArp ? arp::LaneArp : arp::LaneMelody)] = (int) (detail % 997u) + variation;
        I.seed = seed;   // (the chords stay)
        if (kind == KArp) { I.fig = arp::figureVary (I.fig, arp::arpParams (I), detail + (uint32_t) variation, 2, I.H, I.total, I.pedalPc); arp::rerealizeArp (I); }
        else
        {
            const auto ctx = arp::editCtx (I, arp::LaneMelody);
            I.lanes[arp::LaneMelody].notes = arp::toolNewRhythm (arp::toolVary (I.lanes[arp::LaneMelody].notes, ctx, detail + (uint32_t) variation, 2), ctx, detail * 7u + (uint32_t) variation);
        }
    }
    // the idea's key -> the take's (a major idea over a minor key: its relative, so every note stays in the scale)
    const std::string sc = I.H.scaleName; const bool ideaMinor = sc.find ("minor") != std::string::npos || sc == "dorian" || sc == "phrygian" || sc == "aeolian" || sc == "locrian" || sc == "penta";
    int target = k.tonic; if (k.minor && ! ideaMinor) target = k.tonic + 3; if (! k.minor && ideaMinor) target = k.tonic + 9;
    int sh = pcOf (target - I.H.key); if (sh > 6) sh -= 12;
    const auto& lane = I.lanes[kind == KArp ? arp::LaneArp : arp::LaneMelody].notes;
    const int loopT = juce::jmax (arp::BAR, I.total), want = bars * arp::BAR;
    Out o;
    auto conv = [&] (const arp::Note& x, int t0) { NoteEvent e; e.note = x.p + sh; e.beat = (double) (x.t + t0) / arp::PPQ; e.length = juce::jmax (0.05, (double) x.d / arp::PPQ); e.velocity = juce::jlimit (0.2f, 1.0f, x.v); return e; };
    for (int t0 = 0; t0 < want; t0 += loopT) for (auto& x : lane) if (x.t + t0 < want) o.lead.push_back (conv (x, t0));
    const auto& other = I.lanes[kind == KArp ? arp::LaneMelody : arp::LaneArp].notes;
    for (int t0 = 0; t0 < want; t0 += loopT) { for (auto& x : other) if (x.t + t0 < want) o.accomp.push_back (conv (x, t0)); for (auto& x : I.lanes[arp::LaneBass].notes) if (x.t + t0 < want) o.accomp.push_back (conv (x, t0)); }
    inKey (o.lead, k); inKey (o.accomp, k);
    o.lead = monophonic (o.lead, kind != KArp);
    // the chords, one per bar (what sounds at the bar's start)
    o.beatsPerChord = 4.0;
    for (int b = 0; b < bars; ++b)
    {
        const auto& hc = I.H.at ((b * arp::BAR) % loopT);
        std::vector<int> iv; for (int pc : hc.pcs) iv.push_back (pcOf (pc - hc.root));
        std::vector<int> v { hc.bassNote + sh }; for (int x : hc.voicing) v.push_back (x + sh);
        if (iv.empty()) iv = { 0, 3, 7 };
        o.chords.push_back (makeChord (k, hc.root + sh, iv, v));
    }
    for (size_t i = 0; i < juce::jmin<size_t> (4, o.chords.size()); ++i) o.progName << (i ? " - " : "") << o.chords[i].name;
    o.form = juce::String (arp::modeName (mood)) + (kind == KTrap || kind == KHipHop ? juce::String (" ") + arp::flavourName (mood, I.flavourUsed) : juce::String())
             + (kind == KHookLab ? juce::String (" ") + arp::styleName (a) : juce::String()) + (kind == KArp ? " arp figure" : " hook");
    return o;
}
} // namespace

int count() { return (int) defs().size(); }
const Info& info (int i) { return defs()[(size_t) juce::jlimit (0, count() - 1, i)].info; }

Out make (int style, const Key& k, int centre, int bars, uint32_t seed, uint32_t detail, int variation, double bpm)
{
    const auto& d = defs()[(size_t) juce::jlimit (0, count() - 1, style)];
    bars = juce::jlimit (1, 16, bars);
    seed = seed * 2654435761u + (uint32_t) style * 40503u + 1u;   // (each style its own stream: two styles of one generator differ)
    Out o;
    switch (d.kind)
    {
        case KPiano: o = piano (d.a, k, bars, seed, detail, variation); break;
        case KSynth: o = synth (d.a, k, bars, seed, detail, variation); break;
        case KRevEmo: o = reverie (d.a, nullptr, k, bars, seed, detail, variation, bpm); break;
        case KRevPat: { static const char* P[] { "arpeggio", "broken", "sparse", "cascade", "minimal" }; o = reverie (-1, P[d.a], k, bars, seed, detail, variation, bpm); break; }
        case KPop: o = popBrain (k, bars, seed, detail, variation); break;
        case KAcidBrain: o = acidBrain (k, bars, seed, detail, variation); break;
        case KParlour: o = parlour (k, bars, seed, detail, variation, d.a == 1); break;
        default: o = arpez (d.kind, d.a, k, bars, seed, detail, variation); break;
    }
    // inside the take: nothing past the end, everything in the key, the lead where the voice sings it
    const double end = bars * 4.0;
    auto crop = [&] (std::vector<NoteEvent>& ev) { ev.erase (std::remove_if (ev.begin(), ev.end(), [&] (const NoteEvent& e) { return e.beat >= end - 1e-6; }), ev.end()); for (auto& e : ev) e.length = juce::jmin (e.length, end - e.beat); };
    crop (o.lead); crop (o.accomp); inKey (o.lead, k); inKey (o.accomp, k);
    centreLead (o.lead, centre);
    if (o.chords.empty()) o.chords.push_back (makeChord (k, k.tonic + 48, { 0, k.minor ? 3 : 4, 7 }, { k.tonic + 36, k.tonic + 48, k.tonic + 52, k.tonic + 55 }));
    return o;
}
// build 8: for PLAY IT (PlayEdit.cpp)
Chord chordOf (const Key& k, int root, const std::vector<int>& iv, const std::vector<int>& voicing) { return makeChord (k, root, iv, voicing); }
std::unique_lock<std::mutex> arpEngine() { std::unique_lock<std::mutex> l (arpLock()); arpRecipeOnce(); return l; }
} // namespace gslice::gen::brain
