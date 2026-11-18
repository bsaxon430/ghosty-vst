// GHOST SLICE build 8 - PLAY IT (see PlayEdit.h). ArpEZator's engine (ArpEZator/*.cpp, linked, our own code) does the arp, the
// SIMPLER / BUSIER note tools and the chord alternatives; this file turns Ghost Slice's chords / notes into its terms and back.
#include "PlayEdit.h"
#include "Brain.h"
#include "Generate.h"
#include "ArpCore.h"
#include "Composer.h"
#include "HookLab.h"
#include <algorithm>
#include <cmath>

namespace gslice::gen
{
namespace
{
int pcOf (int n) { return ((n % 12) + 12) % 12; }
bool sameNote (const NoteEvent& a, const NoteEvent& b)
{
    return a.note == b.note && a.beat == b.beat && a.length == b.length && a.velocity == b.velocity && a.chop == b.chop && a.reverse == b.reverse
        && a.stutter == b.stutter && a.link == b.link && a.linkAmt == b.linkAmt && a.mine == b.mine;
}
std::string scaleOfKey (const Key& k) { return k.minor ? "minor" : "major"; }
const char* degreeToken (const Key& k, int d)
{
    static const char* tm[] { "i", "ii", "III", "iv", "v", "VI", "VII" }, * tM[] { "I", "ii", "iii", "IV", "V", "vi", "vii" };
    return (k.minor ? tm : tM)[((d % 7) + 7) % 7];
}
double colourFor (int kind) { return kind == 0 ? 0.0 : kind == 1 ? 0.85 : 0.45; }

// an ArpEZator chord (root / bass relative to the key) as a Ghost Slice chord, voiced where the written chord sits
Chord fromBase (const Key& k, const arp::ChordBase& cb, const Chord& written)
{
    const int rootPc = pcOf (k.tonic + cb.root);
    const int bassPc = cb.bass ? pcOf (k.tonic + *cb.bass) : rootPc;
    const int wBass = written.notes.empty() ? 36 + pcOf (rootPc - 36) : written.notes.front();
    int bass = wBass - 6; while (pcOf (bass) != bassPc) ++bass;   // (the nearest one at or above the written bass - 6)
    double centre = 0; int nu = 0; for (size_t i = 1; i < written.notes.size(); ++i) { centre += written.notes[i]; ++nu; }
    centre = nu > 0 ? centre / nu : (double) wBass + 16.0;
    std::vector<int> upper;
    for (int x : cb.iv)
    {
        const int pc = pcOf (rootPc + x); int best = -1;
        for (int m = (int) centre - 7; m <= (int) centre + 7; ++m) if (pcOf (m) == pc && (best < 0 || std::abs (m - centre) < std::abs (best - centre))) best = m;
        while (best <= bass + 2) best += 12;
        if (std::find (upper.begin(), upper.end(), best) == upper.end()) upper.push_back (best);
    }
    std::sort (upper.begin(), upper.end());
    std::vector<int> v { bass }; for (int x : upper) v.push_back (x);
    return brain::chordOf (k, rootPc, cb.iv, v);
}
arp::ChordBase baseOf (const Key& k, int degree, int alt, int kind)
{
    arp::Slot s; s.tok = degreeToken (k, degree); s.alt = alt;
    return arp::altChord (s, scaleOfKey (k), colourFor (kind));
}
bool baseInKey (const Key& k, const arp::ChordBase& cb)
{
    if (cb.chrom) return false;
    for (int x : cb.iv) if (! inScale (pcOf (k.tonic + cb.root + x) + 60, k)) return false;
    return ! cb.bass || inScale (pcOf (k.tonic + *cb.bass) + 60, k);
}

// Ghost Slice chords -> ArpEZator's harmony (each chord from its beat to the next, repeated to fill the length)
arp::Harmony harmonyOf (const std::vector<Chord>& chords, double bpc, const Key& k, int bars)
{
    std::vector<arp::Resolved> rs;
    const int total = bars * arp::BAR, per = juce::jmax (arp::PPQ, (int) std::lround (bpc * arp::PPQ));
    for (int j = 0, t = 0; t < total && ! chords.empty(); ++j, t += per)
    {
        const auto& ch = chords[(size_t) j % chords.size()];
        arp::Resolved r;
        const int rootPc = ch.pcs.empty() ? k.tonic : ch.pcs.front();
        r.root = pcOf (rootPc - k.tonic);
        for (int pc : ch.pcs) r.iv.push_back (pcOf (pc - rootPc));
        r.pcs = ch.pcs; r.name = ch.name.toStdString();
        r.bassNote = ch.notes.empty() ? 36 + pcOf (rootPc - 36) : ch.notes.front();
        for (size_t i = 1; i < ch.notes.size(); ++i) r.voicing.push_back (ch.notes[i]);
        if (r.voicing.empty()) for (int x : r.iv) r.voicing.push_back (60 + pcOf (rootPc + x - 60));
        r.t = t; r.tEnd = juce::jmin (total, t + per); r.len = (r.tEnd - r.t) / arp::PPQ;
        rs.push_back (r);
    }
    return arp::makeHarmony (k.tonic, scaleOfKey (k), rs, 3);
}
std::vector<arp::Note> toArp (const std::vector<NoteEvent>& ev)
{
    std::vector<arp::Note> n;
    for (auto& e : ev) { arp::Note x; x.t = (int) std::lround (e.beat * arp::PPQ); x.d = juce::jmax (1, (int) std::lround (e.length * arp::PPQ)); x.p = e.note; x.v = e.velocity; x.lock = e.mine; n.push_back (x); }
    arp::sortNotes (n);
    return n;
}
std::vector<NoteEvent> fromArp (const std::vector<arp::Note>& n, int bars)
{
    std::vector<NoteEvent> ev; const double end = bars * 4.0;
    for (auto& x : n)
    {
        NoteEvent e; e.note = x.p; e.beat = (double) x.t / arp::PPQ; e.length = juce::jmax (1.0 / 32.0, (double) x.d / arp::PPQ); e.velocity = juce::jlimit (0.2f, 1.0f, x.v);
        if (e.beat >= end - 1e-9) continue;
        e.length = juce::jmin (e.length, end - e.beat);
        ev.push_back (e);
    }
    return ev;
}
void toKey (std::vector<NoteEvent>& ev, const Key& k) { for (auto& e : ev) if (! inScale (e.note, k)) e.note = snapToScale (e.note, k); }
void sortLine (std::vector<NoteEvent>& ev) { std::stable_sort (ev.begin(), ev.end(), [] (const NoteEvent& a, const NoteEvent& b) { return a.beat != b.beat ? a.beat < b.beat : a.note < b.note; }); }
} // namespace

// ---------------------------------------------------------------- the edit (saved with the take's recipe)
bool NoteEdit::music() const
{
    if (notesEdited() || chords >= 0 || arp > 0 || std::abs (master - 0.5) > 1e-9 || bed > 0.0) return true;
    for (int a : chordAlt) if (a >= 0) return true;
    return false;
}
bool NoteEdit::operator== (const NoteEdit& o) const
{
    if (locked.size() != o.locked.size() || removed != o.removed || chordAlt != o.chordAlt || chords != o.chords || master != o.master || arp != o.arp
        || arpRate != o.arpRate || arpOct != o.arpOct || grid != o.grid || rests != o.rests || onlyMine != o.onlyMine || bed != o.bed) return false;
    for (size_t i = 0; i < locked.size(); ++i) if (! sameNote (locked[i], o.locked[i])) return false;
    return true;
}
juce::var NoteEdit::toVar() const
{
    auto* o = new juce::DynamicObject();
    juce::Array<juce::var> L;
    for (auto& e : locked) { juce::Array<juce::var> a; a.add (e.beat); a.add (e.length); a.add (e.note); a.add (e.velocity); a.add (e.chop); a.add (e.link); a.add (e.linkAmt); a.add (e.reverse); a.add (e.stutter); L.add (a); }
    o->setProperty ("locked", L);
    juce::Array<juce::var> R; for (auto& r : removed) { juce::Array<juce::var> a; a.add (r.first); a.add (r.second); R.add (a); } o->setProperty ("removed", R);
    juce::Array<juce::var> A; for (int x : chordAlt) A.add (x); o->setProperty ("chordAlt", A);
    if (! rests.empty()) { juce::Array<juce::var> Z; for (auto& r : rests) { juce::Array<juce::var> a; a.add (r.first); a.add (r.second); Z.add (a); } o->setProperty ("rests", Z); }   // (build 9: only when used)
    if (onlyMine) o->setProperty ("onlyMine", true);
    if (bed > 0.0) o->setProperty ("bed", bed);
    o->setProperty ("chords", chords); o->setProperty ("master", master); o->setProperty ("arp", arp); o->setProperty ("arpRate", arpRate); o->setProperty ("arpOct", arpOct); o->setProperty ("grid", grid);
    return juce::var (o);
}
NoteEdit NoteEdit::fromVar (const juce::var& v)
{
    NoteEdit e;
    if (! v.isObject()) return e;
    if (auto* L = v["locked"].getArray())
        for (auto& a : *L)
            if (a.isArray() && a.size() >= 9)
            {
                NoteEvent n; n.beat = juce::jmax (0.0, (double) a[0]); n.length = juce::jmax (1.0 / 64.0, (double) a[1]); n.note = juce::jlimit (0, 127, (int) a[2]);
                n.velocity = juce::jlimit (0.05f, 1.0f, (float) (double) a[3]); n.chop = (int) a[4]; n.link = juce::jlimit (0, 2, (int) a[5]); n.linkAmt = juce::jlimit (0.0f, 1.0f, (float) (double) a[6]);
                n.reverse = (bool) a[7]; n.stutter = juce::jlimit (0, 8, (int) a[8]); n.mine = true;
                e.locked.push_back (n);
            }
    if (auto* R = v["removed"].getArray()) for (auto& a : *R) if (a.isArray() && a.size() >= 2) e.removed.push_back ({ (double) a[0], (int) a[1] });
    if (auto* A = v["chordAlt"].getArray()) for (auto& a : *A) e.chordAlt.push_back (juce::jlimit (-1, 55, (int) a));
    if (auto* Z = v["rests"].getArray()) for (auto& a : *Z) if (a.isArray() && a.size() >= 2 && (double) a[1] > 0) e.rests.push_back ({ (double) a[0], (double) a[1] });
    e.onlyMine = (bool) v["onlyMine"];
    e.bed = v.hasProperty ("bed") ? juce::jlimit (0.0, 1.0, (double) v["bed"]) : 0.0;
    e.chords = v.hasProperty ("chords") ? juce::jlimit (-1, 1, (int) v["chords"]) : -1;
    e.master = v.hasProperty ("master") ? juce::jlimit (0.0, 1.0, (double) v["master"]) : 0.5;
    e.arp = juce::jlimit (0, edit::kArps - 1, (int) v["arp"]);
    e.arpRate = v.hasProperty ("arpRate") ? juce::jlimit (0, 1, (int) v["arpRate"]) : 1;
    e.arpOct = v.hasProperty ("arpOct") ? juce::jlimit (1, 3, (int) v["arpOct"]) : 2;
    e.grid = v.hasProperty ("grid") ? juce::jlimit (0, 3, (int) v["grid"]) : 2;
    return e;
}

namespace edit
{
const char* arpName (int a) { static const char* n[] { "OFF", "FIGURE", "UP", "DOWN", "UP-DOWN", "RANDOM", "PENDULUM", "CONVERGE" }; return n[juce::jlimit (0, kArps - 1, a)]; }
const char* chordsName (int c) { return c == 0 ? "SIMPLE" : c == 1 ? "RICH" : "AS WRITTEN"; }
double gridBeats (int grid) { static const double g[] { 1.0, 0.5, 0.25, 0.125 }; return g[juce::jlimit (0, 3, grid)]; }
const char* gridName (int grid) { static const char* n[] { "1/4", "1/8", "1/16", "1/32" }; return n[juce::jlimit (0, 3, grid)]; }
double snapTime (double beat, int grid) { const double g = gridBeats (grid); return juce::jmax (0.0, std::round (beat / g) * g); }
int snapPitch (int note, const Key& k) { return juce::jlimit (0, 127, inScale (note, k) ? note : snapToScale (note, k)); }

std::vector<ChordOption> chordOptions (const Key& k, const Chord& written, int kind)
{
    std::vector<ChordOption> out;
    const int d = juce::jlimit (0, 6, written.degree);
    auto add = [&] (int degree, int alt, const juce::String& why)
    {
        const auto cb = baseOf (k, degree, alt, kind);
        if (! baseInKey (k, cb)) return;   // (only chords inside the key: an out-of-key chord cannot be picked)
        auto c = fromBase (k, cb, written);
        for (auto& o : out) if (o.chord.name == c.name && o.chord.notes == c.notes) return;
        if (c.name == written.name && c.notes == written.notes) return;
        out.push_back ({ c, why.isEmpty() ? c.name : c.name + "  (" + why + ")", degree * 8 + alt });
    };
    // ArpEZator's alternatives of this chord (tap-a-chord): plain / its colour, sus, 7th, lush, over the 3rd, swap down / up
    add (d, 0, kind == 0 ? "plain" : kind == 1 ? "rich" : "as the key has it");
    static const char* why[] { "", "sus", "7th", "lush", "over the 3rd", "swap down", "swap up" };
    for (int a = 1; a <= 6; ++a) add (d, a, why[a]);
    // then every other chord of the key
    for (int j = 1; j < 7; ++j) add ((d + j) % 7, 0, {});
    return out;
}

Chord chordFor (const Key& k, const Chord& written, int code, int kind)
{
    if (code < 0) return written;
    const auto cb = baseOf (k, juce::jlimit (0, 6, code / 8), juce::jlimit (0, 6, code % 8), kind);
    return baseInKey (k, cb) ? fromBase (k, cb, written) : written;
}

std::vector<Chord> editChords (const std::vector<Chord>& written, const Key& k, const NoteEdit& e, std::vector<bool>* changed)
{
    std::vector<Chord> out = written;
    if (changed != nullptr) changed->assign (written.size(), false);
    for (size_t i = 0; i < written.size(); ++i)
    {
        const int alt = i < e.chordAlt.size() ? e.chordAlt[i] : -1;
        if (alt >= 0) out[i] = chordFor (k, written[i], alt, e.chords);
        else if (e.chords >= 0)
        {
            const auto cb = baseOf (k, written[i].degree, 0, e.chords);
            if (baseInKey (k, cb)) out[i] = fromBase (k, cb, written[i]);
        }
        if (changed != nullptr) (*changed)[i] = ! (out[i].name == written[i].name && out[i].notes == written[i].notes);
    }
    return out;
}

void refit (std::vector<NoteEvent>& line, const std::vector<Chord>& chords, double bpc, const Key& k, const std::vector<bool>& changed)
{
    if (chords.empty()) return;
    for (auto& e : line)
    {
        const int ci = juce::jmax (0, (int) std::floor (e.beat / juce::jmax (0.25, bpc) + 1e-9)) % (int) chords.size();
        if (ci >= (int) changed.size() || ! changed[(size_t) ci]) continue;
        const auto& ch = chords[(size_t) ci];
        const bool strong = std::abs (e.beat - std::round (e.beat)) < 1e-6 || e.length >= 1.0;
        if (strong && ! isChordTone (e.note, ch))
        {   // onto the nearest chord tone (down first on a tie: a falling resolution)
            for (int dd = 1; dd <= 6; ++dd) { if (isChordTone (e.note - dd, ch)) { e.note -= dd; break; } if (isChordTone (e.note + dd, ch)) { e.note += dd; break; } }
        }
        e.note = snapPitch (e.note, k);
    }
}

std::vector<NoteEvent> arpOver (const std::vector<Chord>& chords, double bpc, const Key& k, int bars, int centre, const NoteEdit& e, uint32_t seed)
{
    if (chords.empty() || e.arp <= 0) return {};
    std::vector<arp::Note> notes;
    {
        auto lock = brain::arpEngine();
        const auto H = harmonyOf (chords, bpc, k, bars);
        arp::ArpParams P; P.seed = seed * 2654435761u + 977u; P.mood = k.minor ? arp::Midnight : arp::Sunrise; P.spb = e.arpRate == 0 ? 8 : 16;
        P.range = juce::jlimit (1, 3, e.arpOct); P.master = e.master; P.evolveBars = 0;
        auto r = arp::composeArp (H, P, bars * arp::BAR);
        notes = r.notes;
        if (e.arp >= 2) notes = arp::shapeArp (notes, H, e.arp - 1, 0, 0, r.fig.len, juce::jmax (arp::BAR, H.loopTicks), seed * 31u + 7u);
        // (the figure loops the chord cycle; a take longer than it repeats it)
        const int loop = juce::jmax (arp::BAR, H.loopTicks), total = bars * arp::BAR;
        std::vector<arp::Note> all;
        for (int t0 = 0; t0 < total; t0 += loop) for (auto x : notes) { x.t += t0; if (x.t < total) all.push_back (x); }
        notes = all;
    }
    auto ev = fromArp (notes, bars);
    if (! ev.empty())
    {   // where the take's line sits (the voice's register): the arp's middle on the centre, in octaves
        std::vector<int> ps; for (auto& x : ev) ps.push_back (x.note); std::nth_element (ps.begin(), ps.begin() + (long) ps.size() / 2, ps.end());
        const int med = ps[ps.size() / 2]; int sh = 0; while (med + sh > centre + 6) sh -= 12; while (med + sh < centre - 6) sh += 12;
        for (auto& x : ev) x.note = juce::jlimit (0, 127, x.note + sh);
    }
    toKey (ev, k);
    return ev;
}

std::vector<NoteEvent> master (const std::vector<NoteEvent>& line, const std::vector<Chord>& chords, double bpc, const Key& k, int bars, double amount, uint32_t seed)
{
    const int n = juce::jlimit (0, 2, (int) std::lround (std::abs (amount - 0.5) * 4.0));
    if (n == 0 || line.empty() || chords.empty()) return line;
    std::vector<arp::Note> notes = toArp (line);
    {
        auto lock = brain::arpEngine();
        const auto H = harmonyOf (chords, bpc, k, bars);
        arp::EditCtx c; c.H = &H; c.totalTicks = bars * arp::BAR; c.spb = 16; c.mood = k.minor ? arp::Midnight : arp::Sunrise;
        for (int i = 0; i < n; ++i) notes = amount < 0.5 ? arp::toolSimpler (notes, c) : arp::toolBusier (notes, c, seed * 7919u + (uint32_t) i * 104729u + 11u);
    }
    auto ev = fromArp (notes, bars);
    toKey (ev, k);
    return ev;
}

bool editSong (Song& s, const Key& k, int centre, int bars, uint32_t seed, const NoteEdit& e, bool arpOk, bool masterOk, std::vector<Chord>* written)
{
    if (written != nullptr) *written = s.chords;
    bool any = false;
    bool swaps = e.chords >= 0; for (int a : e.chordAlt) swaps = swaps || a >= 0;
    if (swaps && ! s.chords.empty())
    {
        std::vector<bool> changed;
        s.chords = editChords (s.chords, k, e, &changed);
        refit (s.melody, s.chords, s.beatsPerChord, k, changed);
        refit (s.accomp, s.chords, s.beatsPerChord, k, changed);
        s.progName.clear(); for (size_t i = 0; i < juce::jmin<size_t> (4, s.chords.size()); ++i) s.progName << (i ? " - " : "") << s.chords[i].name;
        any = true;
    }
    if (arpOk && e.arp > 0 && ! s.chords.empty())
    {
        auto a = arpOver (s.chords, s.beatsPerChord, k, bars, centre, e, seed);
        if (! a.empty()) { s.melody = a; s.form = juce::String ("ARP ") + arpName (e.arp) + (e.arpRate == 0 ? " 1/8" : " 1/16") + ", " + juce::String (e.arpOct) + (e.arpOct == 1 ? " octave" : " octaves"); any = true; }
    }
    else if (masterOk && std::abs (e.master - 0.5) > 0.01)
    {
        s.melody = master (s.melody, s.chords, s.beatsPerChord, k, bars, e.master, seed);
        any = true;
    }
    if (any) { toKey (s.melody, k); toKey (s.accomp, k); }
    return any;
}

std::vector<NoteEvent> editLine (std::vector<NoteEvent> gen, const NoteEdit& e, const Key& k, int bars, bool mono)
{
    if (! e.notesEdited()) return gen;
    const double end = bars * 4.0;
    gen.erase (std::remove_if (gen.begin(), gen.end(), [&] (const NoteEvent& g)
    {
        for (auto& r : e.removed) if (std::abs (g.beat - r.first) < 1e-4 && (mono || g.note == r.second)) return true;   // (one line: the beat says which note)
        return false;
    }), gen.end());
    // build 9: an upgraded older take plays only your notes; the spots you emptied stay empty (also after VARY: they are times, not notes)
    if (e.onlyMine) gen.clear();
    if (mono && ! e.rests.empty())
        gen.erase (std::remove_if (gen.begin(), gen.end(), [&] (const NoteEvent& g) { for (auto& r : e.rests) if (g.beat >= r.first - 1e-4 && g.beat < r.first + r.second - 1e-4) return true; return false; }), gen.end());
    std::vector<NoteEvent> mine;
    for (auto n : e.locked)
    {
        n.note = snapPitch (n.note, k); n.mine = true;
        if (n.beat >= end - 1e-9 || n.beat < 0) continue;
        n.length = juce::jmin (end - n.beat, juce::jmax (1.0 / 32.0, n.length));
        mine.push_back (n);
    }
    if (mono)
    {   // one line: what your notes overlap is cut (the take's note before one of yours ends where yours starts)
        std::vector<NoteEvent> keep;
        for (auto g : gen)
        {
            bool drop = false;
            for (auto& m : mine)
            {
                if (g.beat >= m.beat - 1e-6 && g.beat < m.beat + m.length - 1e-6) { drop = true; break; }
                if (g.beat < m.beat && g.beat + g.length > m.beat) g.length = m.beat - g.beat;
            }
            if (! drop && g.length >= 1.0 / 32.0 - 1e-9) keep.push_back (g);
        }
        gen = keep;
    }
    for (auto& m : mine) gen.push_back (m);
    std::stable_sort (gen.begin(), gen.end(), [] (const NoteEvent& a, const NoteEvent& b) { return a.beat != b.beat ? a.beat < b.beat : (a.mine != b.mine ? ! a.mine : a.note < b.note); });
    if (mono)
    {
        std::vector<NoteEvent> line;
        for (auto& x : gen)
        {
            if (! line.empty() && x.beat - line.back().beat < 1.0 / 32.0 - 1e-9)
            {   // (two notes at (almost) the same start: one line keeps one - yours wins)
                if (x.mine || ! line.back().mine) line.back() = x;
                continue;
            }
            if (! line.empty() && line.back().beat + line.back().length > x.beat) line.back().length = x.beat - line.back().beat;
            line.push_back (x);
        }
        gen = line;
    }
    toKey (gen, k);
    return gen;
}

std::vector<NoteEvent> playLine (const std::vector<NoteEvent>& line)
{
    auto out = line;
    for (size_t i = 0; i + 1 < out.size(); ++i)
        if (line[i].link == 2) out[i + 1].velocity = juce::jlimit (0.05f, 1.0f, out[i + 1].velocity * (1.0f + 0.7f * line[i].linkAmt));
    return out;
}
std::vector<NoteEvent> midiLine (const std::vector<NoteEvent>& line)
{
    auto out = playLine (line);
    for (size_t i = 0; i + 1 < out.size(); ++i)
        if (line[i].link == 1 && out[i + 1].beat > out[i].beat) out[i].length = (out[i + 1].beat - out[i].beat) + 1.0 / 16.0;   // (overlap: legato / glide in the DAW)
    return out;
}
} // namespace edit
} // namespace gslice::gen
