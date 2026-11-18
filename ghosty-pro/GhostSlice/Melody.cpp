#include "Generate.h"
#include "Analysis.h"
#include "Brain.h"

namespace gslice::gen
{
using dsp::Rng;
namespace
{
inline int pcOf (int n) { return ((n % 12) + 12) % 12; }

// ---------------------------------------------------------------- styles
struct StyleDef
{
    const char* name; const char* tip;
    std::vector<std::vector<const char*>> maj, mn;   // progressions: tokens "<degree 1-7>[7 9 b(11) s(sus2) S(sus4) m(borrowed minor) D(dominant 7)]"
    double beatsPerChord;
    std::vector<std::vector<int>> cells;              // one bar of onsets (16 steps)
    double legato;                                    // note length share of the gap to the next onset
    double hum;                                       // timing jitter (beats)
    int range;                                        // semitones either side of the centre
    double leap;                                      // chance of a leap
};
const StyleDef& def (Style s)
{
    static const StyleDef D[kStyles] = {
        { "AUTO", "Picks a style that suits the take type, a new one with every GENERATE.", {}, {}, 4, {}, 0.9, 0.01, 8, 0.2 },
        { "DREAMY", "Long, floating notes over soft 9th and major 7th chords.",
          { { "17", "47", "679", "49" }, { "19", "5s", "679", "47" }, { "47", "19", "67", "5S" }, { "17", "37", "47", "19" } },
          { { "179", "67", "37", "79" }, { "179", "479", "67", "57" }, { "67", "37", "79", "179" } }, 4,
          { { 0, 6, 8, 12 }, { 0, 8, 10 }, { 0, 4, 8, 14 }, { 2, 8, 12 }, { 0, 10 } }, 0.97, 0.015, 7, 0.18 },
        { "MUSIC BOX", "A gentle, simple tune: a few notes, like a music box.",
          { { "1", "3", "4", "1" }, { "17", "47", "1", "5" }, { "4", "1", "2", "1" }, { "1", "6", "4", "4m" } },
          { { "1", "6", "4", "1" }, { "1", "3", "6", "5" }, { "19", "67", "4", "1" } }, 4,
          { { 0, 4, 8 }, { 0, 8, 12 }, { 4, 8 }, { 0, 6, 8, 12 }, { 0, 4, 8, 12 } }, 0.9, 0.04, 7, 0.15 },
        { "HOOK", "A catchy repeating hook with a call and an answer.",
          { { "1", "5", "6", "4" }, { "6", "4", "1", "5" }, { "1", "6", "4", "5" }, { "4", "5", "6", "6" } },
          { { "1", "6", "3", "7" }, { "1", "7", "6", "7" }, { "6", "7", "1", "1" }, { "1", "4", "6", "5" } }, 4,
          { { 0, 3, 6, 8, 10 }, { 0, 2, 3, 6, 10, 12 }, { 2, 4, 6, 8, 11 }, { 0, 3, 6, 10, 12, 14 }, { 0, 2, 4, 8, 11 } }, 0.82, 0.012, 7, 0.22 },
        { "SOUL", "Syncopated, warm 7th chords, notes that push ahead of the beat.",
          { { "27", "5D", "17", "67" }, { "17", "67", "27", "5D" }, { "47", "37", "27", "17" } },
          { { "17", "47", "7D", "37" }, { "179", "479", "179", "57" }, { "17", "67", "47", "5D" } }, 4,
          { { 0, 3, 7, 10, 14 }, { 2, 6, 7, 11, 14 }, { 0, 4, 7, 10, 12 }, { 3, 6, 10, 14 }, { 0, 3, 6, 11, 14 } }, 0.78, 0.02, 7, 0.25 },
        { "ARP", "Broken chords: the notes of each chord rolled up and down in a pattern.",
          { { "1", "5", "6", "4" }, { "6", "4", "1", "5" }, { "1", "3", "4", "5" } },
          { { "1", "6", "3", "7" }, { "1", "6", "4", "5" }, { "1", "4", "6", "7" } }, 4,
          {}, 0.62, 0.008, 9, 0.0 },
        { "ACID", "A hypnotic 16th-note motif with octave jumps that keeps mutating (Aphex acid).",
          { { "1", "1", "4", "5" }, { "1", "4", "1", "5" }, { "1", "1", "6", "5" } },
          { { "1", "1", "6", "7" }, { "1", "7", "1", "6" }, { "1", "4", "1", "7" } }, 4,
          {}, 0.5, 0.006, 12, 0.0 },
        { "AMBIENT", "Very few, very long notes over slow, open chords.",
          { { "19", "47" }, { "19", "67", "47", "19" } }, { { "179", "67" }, { "19", "67", "37", "67" } }, 8,
          { { 0 }, { 0, 8 }, { 4 }, { 0, 12 } }, 1.0, 0.02, 7, 0.12 },
        { "VOICE", "Your sound's own melody becomes the motif, developed over chords.",
          { { "1", "5", "6", "4" }, { "1", "6", "4", "5" }, { "4", "1", "5", "6" } },
          { { "1", "6", "3", "7" }, { "1", "4", "6", "5" }, { "6", "7", "1", "1" } }, 4,
          { { 0, 4, 8, 12 }, { 0, 3, 6, 10 }, { 0, 6, 8, 12 } }, 0.88, 0.015, 8, 0.2 },
    };
    return D[juce::jlimit (0, kStyles - 1, (int) s)];
}

// a chord token -> its pitch classes (relative to the root first) + a name
Chord chordOf (const Key& k, const char* tok)
{
    const auto& sc = scaleOf (k.minor);
    const int d = juce::jlimit (0, 6, tok[0] - '1');
    const int root = (k.tonic + sc[(size_t) d]) % 12;
    auto rel = [&] (int step) { return pcOf (sc[(size_t) ((d + step) % 7)] - sc[(size_t) d]); };
    int third = rel (2), fifth = rel (4); int seventh = -1, ninth = -1, eleventh = -1;
    juce::String q;
    for (const char* p = tok + 1; *p; ++p)
    {
        switch (*p)
        {
            case '7': seventh = rel (6); break;
            case '9': ninth = rel (1); break;
            case 'b': eleventh = rel (3); break;
            case 's': third = rel (1); q = "sus2"; break;
            case 'S': third = rel (3); q = "sus4"; break;
            case 'm': break;                  // (borrowed minor iv / dominant V7: kept in the key, every note diatonic -
            case 'D': seventh = rel (6); break;  //  the take's notes are always the scale's; the minor v7 cadence instead)
            default: break;
        }
    }
    Chord c; c.degree = d;
    c.pcs = { root, (root + third) % 12, (root + fifth) % 12 };
    if (seventh >= 0) c.pcs.push_back ((root + seventh) % 12);
    if (ninth >= 0) c.pcs.push_back ((root + ninth) % 12);
    if (eleventh >= 0) c.pcs.push_back ((root + eleventh) % 12);
    juce::String nm = analysis::pitchClassName (root);
    if (q.isEmpty()) { if (fifth == 6) nm << "dim"; else if (third == 3) nm << "m"; }
    if (seventh >= 0) nm << (seventh == 11 ? "maj7" : "7");
    if (ninth >= 0) nm << (seventh >= 0 ? "(9)" : "add9");
    if (eleventh >= 0) nm << "(11)";
    nm << q;
    c.name = nm;
    return c;
}

// open, voice-led voicing: the root low, the rest near the previous chord's voices, no mud under middle C
std::vector<int> voiceChord (const Chord& c, const std::vector<int>& prev, int centre, double spread)
{
    std::vector<int> upper (c.pcs.begin() + (c.pcs.size() > 3 ? 1 : 0), c.pcs.end());   // (rich chords: the root lives in the bass)
    const int lo = centre - 8, hi = centre + 10 + (int) std::lround (6 * spread);
    std::vector<int> out;
    for (size_t i = 0; i < upper.size(); ++i)
    {
        const int pc = upper[i];
        const int target = prev.size() > i + 1 ? prev[i + 1] : centre - 4 + (int) i * (int) std::lround (3 + 3 * spread);
        int best = -1, bd = 999;
        for (int n = lo; n <= hi; ++n) if (pcOf (n) == pc && std::find (out.begin(), out.end(), n) == out.end() && std::abs (n - target) < bd) { bd = std::abs (n - target); best = n; }
        if (best >= 0) out.push_back (best);
    }
    std::sort (out.begin(), out.end());
    // no mud: under middle C, neighbouring voices at least a 4th apart; anywhere, no seconds at the bottom of the voicing
    for (int pass = 0; pass < 3; ++pass)
        for (size_t i = 0; i + 1 < out.size(); ++i)
        {
            const int gap = out[i + 1] - out[i];
            if ((out[i] < 60 && gap < 5) || (i == 0 && gap <= 2)) { out[i] += 12; std::sort (out.begin(), out.end()); }
        }
    // the bass: the root an octave or so under the voicing
    int bass = out.empty() ? centre - 12 : out.front() - 1; while (pcOf (bass) != c.pcs.front()) --bass;
    if (spread >= 0.25 && out.front() - bass < 7) bass -= 12;
    while (bass < 33) bass += 12;
    out.insert (out.begin(), bass);
    return out;
}

std::vector<int> scalePool (const Key& k, int lo, int hi) { std::vector<int> p; for (int n = lo; n <= hi; ++n) if (inScale (n, k)) p.push_back (n); return p; }
int idxNear (const std::vector<int>& pool, int n) { int b = 0; for (size_t i = 0; i < pool.size(); ++i) if (std::abs (pool[i] - n) < std::abs (pool[(size_t) b] - n)) b = (int) i; return b; }
int chordToneNear (int n, const Chord& c, int lo, int hi)
{
    int best = n, bd = 99;
    for (int m = n - 6; m <= n + 6; ++m) if (m >= lo && m <= hi && std::find (c.pcs.begin(), c.pcs.end(), pcOf (m)) != c.pcs.end() && std::abs (m - n) < bd) { bd = std::abs (m - n); best = m; }
    return best;
}
// a contour: scale steps, mostly by step, a leap answered by a step the other way (gap fill)
std::vector<int> contourOf (Rng& r, int n, double leapP)
{
    std::vector<int> c; int lastLeap = 0;
    for (int i = 0; i < n; ++i)
    {
        int s;
        if (lastLeap != 0) { s = lastLeap > 0 ? -1 : 1; lastLeap = 0; }
        else if (r.chance (leapP)) { s = (r.chance (0.5) ? 1 : -1) * (2 + r.pick (3)); lastLeap = s; }
        else s = r.chance (0.14) ? 0 : (r.chance (0.5) ? 1 : -1);
        c.push_back (s);
    }
    return c;
}
} // namespace

int styleCount() { return brain::kFirst + brain::count(); }
const char* styleName (Style s)
{
    if (s == Style::Acid) return "ACID";
    if (brain::isBrain (s)) return brain::info ((int) s - brain::kFirst).name;
    return def ((Style) juce::jlimit (0, kStyles - 1, (int) s)).name;
}
const char* styleTip (Style s)
{
    if (s == Style::Acid) return "A classic 303 line: slides, accents, octave jumps; a hook, its variations and an answer.";
    if (brain::isBrain (s)) return brain::info ((int) s - brain::kFirst).tip;
    return def ((Style) juce::jlimit (0, kStyles - 1, (int) s)).tip;
}
const char* styleGroup (Style s)
{
    if (s == Style::Auto) return "AUTO";
    if (s == Style::Acid) return "SYNTH";
    if (brain::isBrain (s)) return brain::info ((int) s - brain::kFirst).group;
    return "GHOST SLICE";
}
int styleGroupIndex (Style s)
{
    static const char* G[] { "AUTO", "GHOST SLICE", "PIANO", "REVERIE", "SYNTH", "LEARNED", "ARPEZATOR", "HOOK LAB", "TRAP", "HIP HOP" };
    const juce::String g = styleGroup (s);
    for (int i = 0; i < (int) std::size (G); ++i) if (g == G[i]) return i;
    return 99;
}
Style styleFromName (const juce::String& s)
{
    if (s.equalsIgnoreCase ("LULLABY")) { for (int i = brain::kFirst; i < styleCount(); ++i) if (s.equalsIgnoreCase (styleName ((Style) i))) return (Style) i; }
    for (int i = 0; i < styleCount(); ++i) if (s.equalsIgnoreCase (styleName ((Style) i))) return (Style) i;
    return Style::Auto;
}
namespace { Style byName (const char* n) { for (int i = 0; i < styleCount(); ++i) if (juce::String (n) == styleName ((Style) i)) return (Style) i; return Style::Hook; } }
Style resolveStyle (Style s, int type, uint32_t seed)
{
    if (s != Style::Auto) return s;
    // AUTO: a style that suits the type (a singable line for INSTRUMENT, slow harmony for PAD, rhythm for CHOPS), from every group
    static const char* inst[] { "HOOK", "DREAMY", "SOUL", "LULLABY", "DEEP", "MELANCHOLIC", "HOPEFUL", "NOSTALGIC", "SPARSE MELODY", "POP BRAIN", "SUNRISE HOOK", "DUSK HOOK", "MIDNIGHT HOOK", "EMO", "JAZZ SOUL", "ANTHEMIC", "EMOTIONAL", "SUPERSAW" };
    static const char* pad[] { "DREAMY", "AMBIENT", "DRIFT", "DEEP", "PEACEFUL", "HAUNTING", "MELANCHOLIC", "NOSTALGIC", "RAIN", "BRIDGE" };
    // (chops are words: AUTO keeps to phrases with room for them; the 16th-note streams are there to pick by hand)
    static const char* chops[] { "HOOK", "SOUL", "PIANO HOUSE", "BOOM BAP", "DRILL", "PLAYFUL", "EMO", "MIDNIGHT HOOK", "ANTHEM", "JAZZ SOUL" };
    Rng r ((uint64) seed * 977u + 13u);
    switch (type)
    {
        case 2: return byName (pad[r.pick ((int) std::size (pad))]);
        case 3: return byName (chops[r.pick ((int) std::size (chops))]);
        case 4: return Style::Ambient;
        default: return byName (inst[r.pick ((int) std::size (inst))]);
    }
}
bool isChordTone (int note, const Chord& c) { return std::find (c.pcs.begin(), c.pcs.end(), pcOf (note)) != c.pcs.end(); }

std::vector<Chord> harmony (const Key& k, int centre, Style st, uint32_t seed, uint32_t detail, int variation, double colour, double spread, double& bpc, juce::String& name)
{
    const auto& D = def (st == Style::Auto ? Style::Hook : st);
    Rng r ((uint64) seed * 31u + 7u), rv ((uint64) detail * 53u + (uint64) variation * 7919u + 5u);
    const auto& tbl = k.minor ? D.mn : D.maj;
    std::vector<const char*> toks = tbl[(size_t) r.pick ((int) tbl.size())];
    bpc = D.beatsPerChord;
    std::vector<Chord> out; std::vector<int> prev;
    // VARY: one chord swapped for a related one (the relative: I <-> vi, IV <-> ii, V <-> iii) and new inversions
    const int swap = variation > 0 ? 1 + rv.pick ((int) toks.size() - 1) : -1;
    for (size_t i = 0; i < toks.size(); ++i)
    {
        std::string t = toks[i];
        if ((int) i == swap) { const int d = t[0] - '1'; static const int rel[7] { 5, 3, 4, 1, 2, 0, 4 }; t[0] = (char) ('1' + rel[d]); }
        // colour: plain styles take a 7th / add9 when colour is high; rich styles shed them when it is low
        std::string tt; tt += t[0];
        for (size_t j = 1; j < t.size(); ++j) { const char ch = t[j]; if ((ch == '7' || ch == '9' || ch == 'b') && colour < 0.25) continue; tt += ch; }
        if (colour > 0.6 && tt.find ('7') == std::string::npos && tt.find ('9') == std::string::npos && tt.find ('s') == std::string::npos && tt.find ('S') == std::string::npos) tt += r.chance (0.5) ? "9" : "7";
        if (colour > 0.85 && tt.find ('b') == std::string::npos && (tt[0] == '2' || tt[0] == '6' || (k.minor && tt[0] == '1'))) tt += "b";   // an 11th on the minor chords
        Chord c = chordOf (k, tt.c_str());
        int ctr = centre; if (variation > 0) ctr += (rv.pick (3) - 1) * 3;   // (a new inversion: the voicing centre moves a third)
        c.notes = voiceChord (c, prev, ctr, spread);
        prev = c.notes; out.push_back (c);
        name << (i ? " - " : "") << c.name;
    }
    return out;
}

Song compose (const Key& k, int centre, Style st, int bars, uint32_t seed, uint32_t detail, int variation, double colour, double spread, const std::vector<SeedNote>& voice, double bpm)
{
    const int want = juce::jlimit (1, 16, bars);
    if (st == Style::Acid || brain::isBrain (st))
    {   // the melody brain (Block Step / Ghost House / REVERIE / the learned models / ArpEZator)
        const int bi = st == Style::Acid ? 12 : (int) st - brain::kFirst;   // (ACID = the brain's SYNTH ACID)
        auto o = brain::make (bi, k, centre, want, seed, detail, variation, bpm);
        Song S; S.style = st; S.bars = want; S.chords = o.chords; S.beatsPerChord = o.beatsPerChord; S.melody = o.lead; S.accomp = o.accomp; S.progName = o.progName; S.form = o.form;
        if (S.melody.empty()) { NoteEvent e; e.note = centre; e.beat = 0; e.length = 4.0 * want - 0.05; S.melody.push_back (e); }
        return S;
    }
    Song S; S.style = st == Style::Auto ? Style::Hook : st; S.bars = bars = juce::jmax (2, want + want % 2);
    if (S.style == Style::Voice && voice.size() < 3) S.style = Style::Hook;
    const auto& D = def (S.style);
    S.chords = harmony (k, centre, S.style, seed, detail, variation, colour, spread, S.beatsPerChord, S.progName);
    auto chordAt = [&] (double beat) -> const Chord& { const int i = (int) std::floor (beat / S.beatsPerChord); return S.chords[(size_t) (((i % (int) S.chords.size()) + (int) S.chords.size()) % (int) S.chords.size())]; };
    Rng rm ((uint64) seed * 17u + 3u);                                               // the idea (the same for every VARY)
    Rng rv ((uint64) detail * 29u + (uint64) variation * 104729u + 1u);              // the development (VARY changes it)
    Rng rh ((uint64) detail * 61u + (uint64) variation * 7u + 9u);                   // the feel
    const int lo = centre - D.range, hi = centre + D.range + 1;
    const auto pool = scalePool (k, lo - 12, hi + 12);
    std::vector<NoteEvent> ev;
    auto put = [&] (double beat, int note, double len, float vel) { NoteEvent e; e.beat = beat; e.note = note; e.length = len; e.velocity = vel; ev.push_back (e); };
    const int tonicNear = [&] { int t = centre; for (int d = 0; d <= 6; ++d) { if (pcOf (centre - d - k.tonic) == 0) return centre - d; if (pcOf (centre + d - k.tonic) == 0) return centre + d; } return t; }();

    if (S.style == Style::Arp)
    {   // broken chords: a pattern over each chord's tones (up / up-down / 1-5-8-10 / a rolling 3 against 4), 8ths or 16ths
        static const std::vector<std::vector<int>> pats { { 0, 1, 2, 3 }, { 0, 1, 2, 3, 2, 1 }, { 0, 2, 3, 4 }, { 0, 1, 2 }, { 0, 2, 1, 3 } };
        const auto& pA = pats[(size_t) rm.pick ((int) pats.size())]; const auto& pB = variation > 0 ? pats[(size_t) rv.pick ((int) pats.size())] : pA;
        const double step = rm.chance (0.5) ? 0.5 : 0.25; const int octA = rm.pick (2), octB = variation > 0 ? rv.pick (2) : octA;
        for (double b = 0; b < bars * 4.0 - 1e-9; b += step)
        {
            const auto& c = chordAt (b); const bool second = std::fmod (b, 8.0) >= 4.0;
            const auto& p = second ? pB : pA;
            std::vector<int> tones; for (int n : c.notes) if (n >= centre - 7) tones.push_back (n); if (tones.size() < 3) tones = c.notes;
            const int idx = (int) std::lround (b / step); const int j = p[(size_t) (idx % (int) p.size())];
            int n = tones[(size_t) (j % (int) tones.size())] + 12 * (j / (int) tones.size()) + 12 * (second ? octB : octA) * (j % 2 == 1 && step > 0.3 ? 1 : 0);
            while (n > hi + 5) n -= 12; while (n < lo) n += 12;
            put (b, n, step * D.legato, (float) (0.62 + (std::fmod (b, 1.0) < 1e-6 ? 0.16 : 0.0) + 0.06 * rh.uni()));
        }
        S.form = "broken chords";
    }
    else if (S.style == Style::Acid)
    {   // the acid form: motif A1 motif B motif A2 A1 B2 (16ths, accents, octave jumps, each copy mutated a little)
        struct St { bool on; int deg; int oct; bool acc; };
        std::vector<St> motif (16);
        for (int s = 0; s < 16; ++s) motif[(size_t) s] = { rm.chance (0.7) || s == 0, rm.pick (5) == 0 ? 0 : rm.pick (7) - 2, rm.chance (0.22) ? 1 : 0, rm.chance (0.3) };
        auto mut = [&] (const std::vector<St>& m, double p) { auto o = m; for (auto& x : o) if (rv.chance (p)) { x.on = rv.chance (0.7); x.deg = rv.pick (7) - 2; x.oct = rv.chance (0.25) ? 1 : 0; } return o; };
        const auto A1 = mut (motif, 0.22), A2 = mut (motif, 0.3), B = mut (motif, 0.65), B2 = mut (B, 0.3);
        const std::vector<const std::vector<St>*> seq { &motif, &A1, &motif, &B, &motif, &A2, &A1, &B2 };
        const int t0 = idxNear (pool, tonicNear - 5);
        for (int bar = 0; bar < bars; ++bar)
        {
            const auto& m = *seq[(size_t) (bar % 8)];
            for (int s = 0; s < 16; ++s)
            {
                if (! m[(size_t) s].on) continue;
                int nx = 16; for (int t = s + 1; t < 16; ++t) if (m[(size_t) t].on) { nx = t; break; }
                int n = pool[(size_t) juce::jlimit (0, (int) pool.size() - 1, t0 + m[(size_t) s].deg)] + 12 * m[(size_t) s].oct;
                if (s % 4 == 0) n = chordToneNear (n, chordAt (bar * 4.0 + s * 0.25), lo - 12, hi + 12);
                put (bar * 4.0 + s * 0.25, n, (nx - s) * 0.25 * (m[(size_t) s].acc ? 0.9 : D.legato), m[(size_t) s].acc ? 0.92f : 0.66f);
            }
        }
        S.form = "motif A1 motif B motif A2 A1 B2";
    }
    else
    {
        // ---- the motif A (2 bars): rhythm cells + a contour. VOICE: the sound's own notes are the motif.
        const auto& cells = D.cells;
        std::vector<std::vector<int>> rA { cells[(size_t) rm.pick ((int) cells.size())], cells[(size_t) rm.pick ((int) cells.size())] };
        std::vector<int> cA = contourOf (rm, 16, D.leap);
        int startNote = chordToneNear (centre + (rm.pick (3) - 1) * 3, chordAt (0), lo, hi);
        if (S.style == Style::Voice)
        {   // the voice's own melody: its rhythm (relative lengths on the 16th grid over 2 bars) and its intervals, on the scale
            const int nv = juce::jmin ((int) voice.size(), 10);
            double tot = 0; for (int i = 0; i < nv; ++i) tot += juce::jmax (0.05, voice[(size_t) i].seconds);
            std::vector<int> on; double acc = 0;
            for (int i = 0; i < nv; ++i) { const int st = (int) std::lround (acc / tot * 32.0); if (on.empty() || st > on.back()) on.push_back (juce::jmin (31, st)); acc += juce::jmax (0.05, voice[(size_t) i].seconds); }
            rA = { {}, {} }; for (int st : on) rA[(size_t) (st / 16)].push_back (st % 16);
            if (rA[0].empty()) rA[0] = { 0 }; if (rA[1].empty()) rA[1] = { 0, 8 };
            cA.clear(); for (int i = 1; i < nv; ++i) { const int d = (int) std::lround ((voice[(size_t) i].midi - voice[(size_t) i - 1].midi) / 1.75); cA.push_back (juce::jlimit (-4, 4, d)); }
            while (cA.size() < 16) cA.push_back (cA.empty() ? 1 : -cA[cA.size() % juce::jmax<size_t> (1, cA.size())]);
            startNote = chordToneNear (snapToScale ((int) std::lround (voice[0].midi + 12.0 * std::round ((centre - voice[0].midi) / 12.0)), k), chordAt (0), lo, hi);
        }
        // the answer (bar 2 of A'): the call's contour mirrored; B: new cells + contour; VARY develops A' and B anew
        Rng& rd = variation > 0 ? rv : rm;
        std::vector<std::vector<int>> rB { cells[(size_t) rd.pick ((int) cells.size())], cells[(size_t) rd.pick ((int) cells.size())] };
        std::vector<int> cB = contourOf (rd, 16, D.leap + 0.05);
        if (variation > 0 && rB == rA) rB[0] = cells[(size_t) ((rv.pick ((int) cells.size()) + 1) % (int) cells.size())];
        const int devOp = variation > 0 ? 1 + rv.pick (3) : 0;   // 1 inversion, 2 sequence, 3 new answer rhythm
        std::vector<int> cAns; for (int x : cA) cAns.push_back (-x);
        auto rAns = rA; if (devOp == 3) rAns[1] = cells[(size_t) rv.pick ((int) cells.size())];
        const bool sequence = (S.style == Style::Hook || S.style == Style::Soul || S.style == Style::Voice) ? rm.chance (0.5) : false;
        int cur = startNote;
        for (int ph = 0; ph < bars / 2; ++ph)
        {
            const int kind = ph % 4;   // 0 A, 1 A', 2 A (or A sequenced), 3 B
            const int round = ph / 4; // LENGTH > 8 bars: the second time round the theme is lifted and B is answered anew
            const int nph = juce::jmax (1, bars / 2);
            const int arch = (int) std::lround (2.0 * std::sin (juce::MathConstants<double>::pi * (ph + 0.5) / nph) * (nph >= 4 ? 1.0 : 0.0));   // the phrase-level arch (scale steps)
            const auto& rr = kind == 3 ? rB : kind == 1 ? rAns : rA;
            std::vector<int> ct = kind == 3 ? cB : kind == 1 ? (devOp == 1 ? cAns : (variation > 0 ? contourOf (rv, 16, D.leap) : cAns)) : cA;
            if (round > 0 && kind == 3) ct = contourOf (rd, 16, D.leap + 0.05);                       // B' (a new answer)
            if (round > 0 && kind == 1) for (auto& x : ct) x = -x;                                    // A' inverted the second time
            if (kind == 0 || kind == 2) cur = startNote + (kind == 2 && (sequence || devOp == 2) ? (pool[(size_t) juce::jmin ((int) pool.size() - 1, idxNear (pool, startNote) + 1)] - startNote) : 0);
            if (kind == 2 && variation > 0 && devOp != 2) cur = chordToneNear (startNote + (rv.chance (0.5) ? 2 : -2), chordAt (ph * 8.0), lo, hi);
            if ((kind == 0 || kind == 2) && arch != 0) cur = chordToneNear (pool[(size_t) juce::jlimit (0, (int) pool.size() - 1, idxNear (pool, cur) + arch + (round > 0 ? 1 : 0))], chordAt (ph * 8.0), lo, hi);
            int ci = 0;
            for (int bi = 0; bi < 2; ++bi)
            {
                const int bar = ph * 2 + bi; if (bar >= bars) break;
                const auto& onsets = rr[(size_t) bi];
                for (size_t oi = 0; oi < onsets.size(); ++oi)
                {
                    const int st = onsets[oi]; const double beat = bar * 4.0 + st * 0.25;
                    const auto& ch = chordAt (beat);
                    int n;
                    if (ci == 0 && bi == 0) n = cur;
                    else { int i = idxNear (pool, cur) + ct[(size_t) (ci % (int) ct.size())]; i = juce::jlimit (0, (int) pool.size() - 1, i); n = pool[(size_t) i]; }
                    while (n > hi) n -= 12; while (n < lo) n += 12;
                    if (st % 4 == 0) n = chordToneNear (n, ch, lo, hi);   // strong beats on chord tones
                    cur = n; ++ci;
                    const int nxStep = oi + 1 < onsets.size() ? onsets[oi + 1] : 16;
                    put (beat, n, (nxStep - st) * 0.25 * D.legato, 0.7f);
                }
            }
            // the phrase end lands on a chord tone; the last phrase cadences long on the tonic chord
            if (! ev.empty())
            {
                auto& e = ev.back();
                const bool last = ph == bars / 2 - 1;
                if (last)
                {
                    const double endBeat = bars * 4.0;
                    Chord tonic = chordOf (k, "1");
                    e.note = chordToneNear (rd.chance (0.6) ? tonicNear : e.note, tonic, lo, hi);
                    if (e.beat > endBeat - 2.0) e.beat = juce::jmax (ev.size() > 1 ? ev[ev.size() - 2].beat + 0.5 : 0.0, endBeat - 2.0);
                    e.length = endBeat - e.beat - 0.05;
                }
                else e.note = chordToneNear (e.note, chordAt (e.beat), lo, hi);
            }
        }
        // the last chord resolves to the tonic under the cadence
        if (! S.chords.empty() && (int) S.chords.size() * S.beatsPerChord >= bars * 4.0)
        {
            // (progressions are 4 chords; the cadence note is already a tonic-chord tone)
        }
        S.form = devOp == 0 ? (sequence ? "A A' A(seq) B" : "A A' A B") : devOp == 1 ? "A A'(inverted) A B'" : devOp == 2 ? "A A' A(seq) B'" : "A A'(new answer) A B'";
        if (bars > 8) S.form << " | A^ A'(inv) A^ B'";
        if (S.style == Style::Voice) S.form = "your melody: " + S.form;
    }
    // no two notes on top of each other; lengths never past the next onset
    std::stable_sort (ev.begin(), ev.end(), [] (const NoteEvent& a, const NoteEvent& b) { return a.beat < b.beat; });
    for (size_t i = 0; i + 1 < ev.size(); ++i) ev[i].length = juce::jmin (ev[i].length, juce::jmax (0.05, ev[i + 1].beat - ev[i].beat - 0.02));
    // the feel: velocity accents on 1 and 3, a swell to the middle of each 2-bar phrase, human timing (never before beat 0)
    for (auto& e : ev)
    {
        const double inBar = std::fmod (e.beat, 4.0), inPh = std::fmod (e.beat, 8.0) / 8.0;
        float v = e.velocity + (inBar < 1e-6 ? 0.1f : std::abs (inBar - 2.0) < 1e-6 ? 0.05f : 0.0f) + 0.08f * (float) std::sin (inPh * juce::MathConstants<double>::pi);
        v += (float) (0.08 * rh.sym());
        e.velocity = juce::jlimit (0.3f, 1.0f, v);
        if (e.beat > 0.0) e.beat = juce::jmax (0.0, e.beat + D.hum * rh.sym());
    }
    if (want < bars)
    {   // LENGTH 1: one bar (the motif's first bar, ending on the tonic chord)
        const double end = want * 4.0;
        ev.erase (std::remove_if (ev.begin(), ev.end(), [&] (const NoteEvent& e) { return e.beat >= end - 0.125; }), ev.end());
        for (auto& e : ev) e.length = juce::jmax (0.05, juce::jmin (e.length, end - e.beat - 0.02));
        if (! ev.empty()) ev.back().note = chordToneNear (ev.back().note, S.chords.front(), lo - 12, hi + 12);
        S.bars = want;
    }
    S.melody = ev;
    return S;
}
} // namespace gslice::gen
