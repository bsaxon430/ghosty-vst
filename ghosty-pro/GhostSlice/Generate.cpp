#include "Generate.h"
#include "GenerateEngine.h"
#include "Hold.h"
#include "Syllables.h"
#include "Psola.h"
#include <map>
#include <thread>
#include <atomic>
#include <numeric>

namespace gslice::gen
{
using namespace dsp;

// ---------------------------------------------------------------- names
const char* typeName (Type t) { switch (t) { case Type::Auto: return "AUTO"; case Type::Instrument: return "INSTRUMENT"; case Type::Pad: return "PAD"; case Type::Chops: return "CHOPS"; case Type::Texture: return "TEXTURE"; } return "AUTO"; }
Type typeFromName (const juce::String& s) { for (int i = 0; i < kTypes; ++i) if (s.equalsIgnoreCase (typeName ((Type) i))) return (Type) i; return Type::Auto; }
const char* characterName (Character c) { switch (c) { case Character::Natural: return "NATURAL"; case Character::Chipmunk: return "CHIPMUNK"; case Character::Deep: return "DEEP"; } return "NATURAL"; }
Character characterFromName (const juce::String& s) { for (int i = 0; i < 3; ++i) if (s.equalsIgnoreCase (characterName ((Character) i))) return (Character) i; return Character::Natural; }
juce::String Take::keyName() const { return analysis::pitchClassName (keyTonic) + (keyMinor ? " MIN" : " MAJ"); }
juce::String takeName (int index, Type t, const Key& k, int variation)
{
    juce::String n = "Take " + juce::String (index);
    if (variation > 0) n << juce::String::charToString ((juce::juce_wchar) ('a' + juce::jlimit (0, 25, variation)));
    return n + " - " + typeName (t) + " in " + analysis::pitchClassName (k.tonic) + (k.minor ? " MIN" : " MAJ");
}
size_t Take::bytes() const
{
    size_t b = (size_t) preview.getNumSamples() * 2 * sizeof (float);
    for (auto& z : zones) b += (size_t) z.audio.getNumSamples() * (size_t) z.audio.getNumChannels() * sizeof (float);
    for (auto& c : chops) b += (size_t) c.getNumSamples() * (size_t) c.getNumChannels() * sizeof (float);
    return b;
}

Type resolveType (const Recipe& r)
{
    if (r.type != Type::Auto) return r.type;
    static const Type order[] { Type::Instrument, Type::Pad, Type::Chops, Type::Texture };
    return order[((r.rotation % 4) + 4) % 4];
}

juce::var Recipe::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("type", typeName (type)); o->setProperty ("musical", musical); o->setProperty ("keyTonic", keyTonic); o->setProperty ("keyMinor", keyMinor);
    o->setProperty ("character", characterName (character)); o->setProperty ("bpm", bpm); o->setProperty ("seed", (juce::int64) seed); o->setProperty ("detail", (juce::int64) detail);
    o->setProperty ("rotation", rotation); o->setProperty ("variation", variation); o->setProperty ("name", name); o->setProperty ("favourite", favourite);
    o->setProperty ("clear", clear);
    if (! parts.empty()) { juce::Array<juce::var> ps; for (auto& q : parts) { juce::Array<juce::var> one; one.add (q.first); one.add (q.second); ps.add (one); } o->setProperty ("parts", ps); }
    o->setProperty ("engine", engine);
    if (engine >= 6)
    {
        o->setProperty ("knob", knob); o->setProperty ("style", styleName (style)); o->setProperty ("bars", bars);
        juce::Array<juce::var> d; for (double x : deep) d.add (x); o->setProperty ("deep", d);
        if (edit != NoteEdit()) o->setProperty ("edit", edit.toVar());   // (build 8; only when there is one)
    }
    if (terrain != ghost::terrain::Terrain()) o->setProperty ("terrain", ghost::terrain::toVar (terrain));   // (build 12; only when there is one)
    return juce::var (o);
}
Recipe Recipe::fromVar (const juce::var& v)
{
    Recipe r;
    if (! v.isObject()) return r;
    r.type = typeFromName (v["type"].toString()); r.musical = juce::jlimit (0.0, 1.0, v.hasProperty ("musical") ? (double) v["musical"] : 0.7);
    r.keyTonic = v.hasProperty ("keyTonic") ? juce::jlimit (-1, 11, (int) v["keyTonic"]) : -1; r.keyMinor = (bool) v["keyMinor"];
    r.character = characterFromName (v["character"].toString()); r.bpm = juce::jlimit (20.0, 480.0, v.hasProperty ("bpm") ? (double) v["bpm"] : 120.0);
    r.seed = (uint32_t) (juce::int64) v["seed"]; r.detail = (uint32_t) (juce::int64) v["detail"]; if (r.seed == 0) r.seed = 1; if (r.detail == 0) r.detail = 1;
    r.rotation = (int) v["rotation"]; r.variation = juce::jmax (0, (int) v["variation"]); r.name = v["name"].toString(); r.favourite = (bool) v["favourite"];
    r.clear = v.hasProperty ("clear") && (bool) v["clear"];   // (older takes: off, so they sound as they did)
    if (auto* ps = v["parts"].getArray())
        for (auto& q : *ps) if (q.isArray() && q.size() >= 2) { const double a = q[0], b = q[1]; if (b > a && a >= 0.0) r.parts.push_back ({ a, b }); }
    std::sort (r.parts.begin(), r.parts.end());
    // build 6: a recipe without "engine" was saved before build 6: engine 5, it renders exactly as it did
    r.engine = v.hasProperty ("engine") ? juce::jlimit (5, 9, (int) v["engine"]) : 5;
    r.knob = v.hasProperty ("knob") ? juce::jlimit (-1.0, 1.0, (double) v["knob"]) : -1.0;
    r.style = styleFromName (v["style"].toString());
    r.bars = v.hasProperty ("bars") ? juce::jlimit (1, 16, (int) v["bars"]) : 4;
    if (auto* d = v["deep"].getArray()) for (int i = 0; i < juce::jmin (kDeep, d->size()); ++i) r.deep[(size_t) i] = juce::jlimit (-1.0, 1.0, (double) (*d)[i]);
    if (r.engine >= 6 && v.hasProperty ("edit")) r.edit = NoteEdit::fromVar (v["edit"]);
    if (v.hasProperty ("terrain")) ghost::terrain::fromVar (v["terrain"], r.terrain);   // (build 12)
    if (r.engine < 6 && (int) r.parts.size() > 1 && resolveType (r) == Type::Instrument) r.parts.resize (1);
    if ((int) r.parts.size() > maxParts (resolveType (r))) r.parts.resize ((size_t) maxParts (resolveType (r)));
    return r;
}
int Recipe::maxParts (Type t)
{
    switch (t) { case Type::Instrument: return 8; case Type::Pad: return 8; case Type::Chops: return 16; case Type::Texture: return 5; case Type::Auto: return 16; }
    return 16;
}
// the third knob and the UNDER THE HOOD knobs, per type
double Recipe::knobDefault (Type t) { switch (t) { case Type::Instrument: return 0.35; case Type::Pad: return 0.5; case Type::Chops: return 0.15; case Type::Texture: return 0.5; default: return 0.5; } }
double Recipe::knobOr (Type t) const { return knob >= 0.0 ? juce::jlimit (0.0, 1.0, knob) : knobDefault (t); }
double Recipe::deepDefault (Type t, int i, bool cl)
{   // (the order: what each type's knobs were in the first build 6 sessions stays at 0..3; the rest appended)
    static const double d[5][kDeep] {
        { 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5 },
        { 0.5, 0.2, 0.35, 0.3, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5 },     // INSTRUMENT: FORMANT BREATH VIBRATO GLIDE OWN PITCH QUALITY
        { 0.5, 0.5, 0.5, 0.6, 0.5, 0.35, 0.0, 0.0, 0.0, 0.5 },     // PAD: SHIMMER FREEZE GRAINS COLOUR SIZE SPRAY WINDOW BLUR ROUTE
        { 0.3, 0.4, 0.4, 0.85, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5 },     // CHOPS: REVERSE HUMAN REPEAT GATE TRANSIENT KEEP
        { 0.5, 0.5, 0.5, 0.5, 0.0, 0.0, 0.0, 0.5, 0.5, 0.5 } };    // TEXTURE: GRAIN SIZE DENSITY SPRAY FREEZE WINDOW BLUR SHIMMER ROUTE
    if (t == Type::Instrument && i == 2 && cl) return 0.0;   // CLEAR: no vibrato unless you ask for it
    if (t == Type::Chops && i == 4) return 0.3;
    if (t == Type::Texture && i == 7) return 0.0;
    return d[juce::jlimit (0, 4, (int) t)][juce::jlimit (0, kDeep - 1, i)];
}
double Recipe::deepOr (Type t, int i) const { const double x = deep[(size_t) juce::jlimit (0, kDeep - 1, i)]; return x >= 0.0 ? juce::jlimit (0.0, 1.0, x) : deepDefault (t, i, clear); }
int Recipe::deepCount (Type t) { switch (t) { case Type::Instrument: return 6; case Type::Pad: return 9; case Type::Chops: return 5; case Type::Texture: return 8; default: return 0; } }
const char* Recipe::knobName (Type t) { switch (t) { case Type::Instrument: return "VOICE"; case Type::Pad: return "DREAM"; case Type::Chops: return "STUTTER"; case Type::Texture: return "DRIFT"; default: return "VOICE"; } }
const char* Recipe::knobLeft (Type t) { switch (t) { case Type::Instrument: return "VOICE"; case Type::Pad: return "CLEAR"; case Type::Chops: return "HUMAN"; case Type::Texture: return "STILL"; default: return "VOICE"; } }
const char* Recipe::knobRight (Type t) { switch (t) { case Type::Instrument: return "INSTRUMENT"; case Type::Pad: return "DREAMY"; case Type::Chops: return "STUTTER"; case Type::Texture: return "DRIFTING"; default: return "INSTRUMENT"; } }
const char* Recipe::knobTip (Type t)
{
    switch (t)
    {
        case Type::Instrument: return "VOICE: left = the take sings your actual words, syllable by syllable, on the melody notes (each key plays the next syllable). Right = the pure, smooth instrument made from your vowel. In between: a voice turning into an instrument.";
        case Type::Pad: return "DREAM: how dreamy the pad is. Right = longer, smoother freeze, an octave shimmer, wider 9th / 11th chords that slowly move, more space.";
        case Type::Chops: return "STUTTER: left = clean chops played like a person would. Right = more ratchets, repeats and reversed chops.";
        case Type::Texture: return "DRIFT: how much the drone wanders. Right = longer freezes, wider grain spray, slow movement and more space.";
        default: return "Pick a take type first.";
    }
}
const char* Recipe::deepName (Type t, int i)
{
    static const char* n[5][kDeep] { { "", "", "", "", "", "", "", "", "", "" },
        { "FORMANT", "BREATH", "VIBRATO", "GLIDE", "OWN PITCH", "QUALITY", "", "", "", "" },
        { "SHIMMER", "FREEZE", "DENSITY", "COLOUR", "SIZE", "SPRAY", "WINDOW", "BLUR", "ROUTE", "" },
        { "REVERSE", "HUMAN", "REPEAT", "GATE", "TRANSIENTS", "", "", "", "", "" },
        { "SIZE", "DENSITY", "SPRAY", "FREEZE", "WINDOW", "BLUR", "SHIMMER", "ROUTE", "", "" } };
    return n[juce::jlimit (0, 4, (int) t)][juce::jlimit (0, kDeep - 1, i)];
}
const char* Recipe::deepTip (Type t, int i)
{
    static const char* n[5][kDeep] {
        { "", "", "", "", "", "", "", "", "", "" },
        { "FORMANT: the size of the throat. Left = bigger and darker, right = smaller and brighter; the middle keeps your own.",
          "BREATH: how much of the voice's own air stays in the tone. Left = clean harmonics, right = breathy.",
          "VIBRATO: a gentle, delayed vibrato on held notes (CLEAR starts at none).",
          "GLIDE: how much one note slides into the next when they join.",
          "OWN PITCH: how much of your voice's own scoops and wobbles stay in the sung notes (left = exactly on the notes).",
          "QUALITY: how finely your voice is measured (left = quicker, follows fast changes; right = the finest detail, for long notes). Like an FFT of 2048 / 4096 / 8192.",
          "", "", "", "" },
        { "SHIMMER: the voice an octave up, softly, above the pad.",
          "FREEZE: how much of the long frozen part (the glassy, endless sound) there is and how slowly it moves.",
          "DENSITY: how many little grains of your sound sparkle in the pad.",
          "COLOUR: plain chords (left) to rich 7th / 9th / 11th chords (right).",
          "SIZE: short sparkly grains (left) to long smooth ones (right).",
          "SPRAY: how far the grains wander through your sound.",
          "WINDOW: the shape of each grain: soft-edged (left, Tukey), round (middle, Hann), very soft (right, Gaussian).",
          "BLUR: how much the frozen part smears into a dreamy wash (random phases). Left = clear and glassy.",
          "ROUTE: left = the grains go into the freeze (a frozen cloud); right = the grains are made from the frozen sound.",
          "" },
        { "REVERSE: how often a chop plays backwards.", "HUMAN: how loose the timing feels (a little early, a little late).",
          "REPEAT: how often the same chop comes back twice in a row.", "GATE: how long each chop rings (short and choppy .. full).",
          "TRANSIENTS: how much of each chop's attack stays exactly as recorded when it is re-pitched (right = crisp, untouched starts).",
          "", "", "", "", "" },
        { "SIZE: short sparkly grains (left) to long smooth ones (right).", "DENSITY: how many grains.",
          "SPRAY: how far the grains wander through your sound.", "FREEZE: how long and smooth the frozen drone is.",
          "WINDOW: the shape of each grain: soft-edged (left, Tukey), round (middle, Hann), very soft (right, Gaussian).",
          "BLUR: how much the drone smears into a dreamy wash (random phases). Left = clear.",
          "SHIMMER: the drone an octave up, softly, on top.",
          "ROUTE: left = the grains go into the freeze; right = the grains are made from the frozen drone.",
          "", "" } };
    return n[juce::jlimit (0, 4, (int) t)][juce::jlimit (0, kDeep - 1, i)];
}

// ---------------------------------------------------------------- material
namespace
{
struct FrameStats { double voiced = 0, f0 = 0, cents = 0, rmsDb = -100; int frames = 0; };
FrameStats statsOf (const analysis::PitchTrack& t, int64 s0, int64 s1)
{
    FrameStats r;
    if (t.frames() == 0 || s1 <= s0) return r;
    const int i0 = juce::jlimit (0, t.frames() - 1, (int) (s0 / t.hop)), i1 = juce::jlimit (i0 + 1, t.frames(), (int) ((s1 + t.hop - 1) / t.hop));
    std::vector<double> m; double v = 0, e = 0;
    for (int i = i0; i < i1; ++i)
    {
        const bool vo = t.f0[(size_t) i] > 0 && t.voicedProb[(size_t) i] >= 0.5;
        v += vo ? 1 : 0; if (vo) m.push_back (hzToMidi (t.f0[(size_t) i]));
        e += std::pow (10.0, t.rmsDb[(size_t) i] / 10.0);
    }
    r.frames = i1 - i0; r.voiced = v / r.frames; r.rmsDb = 10.0 * std::log10 (e / r.frames + 1e-12);
    if (! m.empty())
    {
        auto s = m; std::sort (s.begin(), s.end()); const double med = s[s.size() / 2];
        double d = 0; for (double x : m) { const double c = juce::jlimit (-600.0, 600.0, (x - med) * 100.0); d += c * c; }
        r.cents = std::sqrt (d / (double) m.size()); r.f0 = midiToHz (med);
    }
    return r;
}
double flatnessAt (const SourceAudio& src, int64 centre)
{
    const int N = 2048; std::vector<double> re ((size_t) N, 0.0), im ((size_t) N, 0.0);
    const int64 a = juce::jlimit<int64> (0, juce::jmax<int64> (0, src.frames - N), centre - N / 2);
    for (int i = 0; i < N && a + i < src.frames; ++i)
    {
        double v = 0; for (int c = 0; c < src.channels; ++c) v += src.audio.getReadPointer (c)[a + i]; v /= juce::jmax (1, src.channels);
        re[(size_t) i] = v * (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / N));
    }
    analysis::FFT fft (N); fft.transform (re.data(), im.data(), false);
    const int k0 = juce::jmax (1, (int) (200.0 * N / src.sampleRate)), k1 = juce::jmin (N / 2, (int) (8000.0 * N / src.sampleRate));
    double lg = 0, ar = 0; int n = 0;
    for (int k = k0; k < k1; ++k) { const double p = re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k] + 1e-18; lg += std::log (p); ar += p; ++n; }
    if (n == 0) return 1.0;
    return juce::jlimit (0.0, 1.0, std::exp (lg / n) / (ar / n));
}
} // namespace

namespace
{
// the piece [s0, s1) with its vowel [v0, v1): pitch, clarity, loudness, flatness and the scores. force (a user's part): never rejected
bool scoreSegment (const SourceAudio& src, const analysis::Result& a, double loudest, int64 s0, int64 s1, int64 v0, int64 v1, int kind, bool force, Segment& g)
{
    const double sr = src.sampleRate;
    const int64 minLen = (int64) (0.05 * sr);
    s0 = juce::jlimit<int64> (0, src.frames, s0); s1 = juce::jlimit<int64> (s0, src.frames, s1);
    if (s1 - s0 < (force ? 1 : minLen)) return false;
    g = Segment(); g.s0 = s0; g.s1 = s1; g.v0 = juce::jlimit (s0, s1, v0); g.v1 = juce::jlimit (g.v0, s1, v1); g.kind = kind;
    const auto core = g.v1 > g.v0 + minLen ? statsOf (a.pitch, g.v0, g.v1) : statsOf (a.pitch, s0, s1);
    const auto whole = statsOf (a.pitch, s0, s1);
    g.f0 = core.f0; g.midi = core.f0 > 0 ? hzToMidi (core.f0) : 0; g.voiced = core.voiced; g.stabilityCents = core.f0 > 0 ? core.cents : 600;
    g.rmsDb = whole.rmsDb; g.flatness = flatnessAt (src, g.v1 > g.v0 ? (g.v0 + g.v1) / 2 : (s0 + s1) / 2);
    if (! force && (whole.rmsDb < -55 || whole.rmsDb < loudest - 40)) return false;   // silence / room noise
    g.breath = g.voiced < 0.25 && (g.flatness > 0.2 || whole.rmsDb < loudest - 24);
    const double len = g.seconds (sr), coreLen = (double) (g.v1 - g.v0) / sr;
    const double loud = juce::jlimit (0.0, 1.0, (whole.rmsDb - (loudest - 30.0)) / 24.0);
    const double steady = 1.0 / (1.0 + g.stabilityCents / 45.0);
    const double longEnough = juce::jlimit (0.0, 1.0, (coreLen > 0 ? coreLen : len) / 0.22);
    const double clean = 1.0 - juce::jlimit (0.0, 1.0, g.flatness * 3.0);
    const double pitched = g.breath || g.f0 <= 0 ? 0.0 : 1.0;
    g.instrument = pitched * std::pow (g.voiced, 1.5) * steady * (0.3 + 0.7 * longEnough) * (0.4 + 0.6 * loud) * (0.3 + 0.7 * clean);
    g.pad = pitched * g.voiced * std::sqrt (steady) * (0.5 + 0.5 * longEnough) * (0.5 + 0.5 * loud) * (0.5 + 0.5 * clean);
    g.chop = (0.4 + 0.6 * loud) * juce::jlimit (0.0, 1.0, len / 0.12) * (len > 1.4 ? 0.5 : 1.0) * (g.breath ? 0.15 : 1.0);
    return true;
}
double loudestOf (const analysis::Result& a) { double l = -100; for (float v : a.pitch.rmsDb) l = std::max (l, (double) v); return l; }
// the vowel of [a0, a1): the voiced frames inside, 20 ms in from their edges (none: the middle two thirds)
void vowelIn (const analysis::Result& a, double sr, int64 a0, int64 a1, int64& v0, int64& v1, int& kind)
{
    const auto& T = a.pitch;
    auto voicedAt = [&] (int64 s) { if (T.frames() == 0) return false; const int i = juce::jlimit (0, T.frames() - 1, (int) (s / T.hop)); return T.f0[(size_t) i] > 0 && T.voicedProb[(size_t) i] >= 0.5; };
    v0 = -1; v1 = -1; const int64 step = juce::jmax<int64> (1, T.hop);
    for (int64 s = a0; s < a1; s += step) if (voicedAt (s)) { if (v0 < 0) v0 = s; v1 = s; }
    const int64 in = (int64) (0.02 * sr);
    if (v0 >= 0 && v1 - v0 > 3 * in) { v0 += in; v1 -= in; kind = 0; }
    else { v0 = a0 + (a1 - a0) / 6; v1 = a1 - (a1 - a0) / 6; kind = 2; }
}
} // namespace

Segment segmentAt (const SourceAudio& src, const analysis::Result& a, int64 s0, int64 s1)
{
    int64 v0, v1; int kind; vowelIn (a, src.sampleRate, s0, s1, v0, v1, kind);
    Segment g; g.s0 = s0; g.s1 = s1;
    scoreSegment (src, a, loudestOf (a), s0, s1, v0, v1, kind, true, g);
    return g;
}

std::vector<Segment> findMaterial (const SourceAudio& src, const analysis::Result& a)
{
    std::vector<Segment> out;
    const double sr = src.sampleRate;
    const double loudest = loudestOf (a);
    auto push = [&] (int64 s0, int64 s1, int64 v0, int64 v1, int kind) { Segment g; if (scoreSegment (src, a, loudest, s0, s1, v0, v1, kind, false, g)) out.push_back (g); };
    // the pieces: between every boundary the analysis knows - syllable edges, onsets, and note changes (a pitch jump or a gap) -
    // so a phrase of 10 syllables gives about 10 pieces even when the syllable detector merged some of them
    std::vector<int64> B;
    for (auto& s : a.syllables) { B.push_back (s.start); B.push_back (s.end); }
    for (auto& e : a.onsets.events) if (e.strength >= 0.3f) B.push_back (e.pos);
    for (size_t i = 0; i < a.notes.size(); ++i)
    {
        const auto& n = a.notes[i];
        if (i == 0 || n.s0 - a.notes[i - 1].s1 > (int64) (0.04 * sr) || std::abs (n.pitch - a.notes[i - 1].pitch) >= 1.0) B.push_back (n.s0);
        if (i + 1 == a.notes.size() || a.notes[i + 1].s0 - n.s1 > (int64) (0.04 * sr)) B.push_back (n.s1);
    }
    B.push_back (0); B.push_back (src.frames);
    std::sort (B.begin(), B.end());
    std::vector<int64> M;
    for (auto x : B) if (M.empty() || x - M.back() >= (int64) (0.08 * sr)) M.push_back (x); else M.back() = (M.back() + x) / 2;
    const auto& T = a.pitch;
    auto voicedAt = [&] (int64 s) { if (T.frames() == 0) return false; const int i = juce::jlimit (0, T.frames() - 1, (int) (s / T.hop)); return T.f0[(size_t) i] > 0 && T.voicedProb[(size_t) i] >= 0.5; };
    for (size_t i = 0; i + 1 < M.size(); ++i)
    {
        int64 s0 = M[i], s1 = M[i + 1];
        const int parts = juce::jmax (1, (int) std::ceil ((double) (s1 - s0) / (1.4 * sr)));   // (long notes: pieces of at most 1.4 s)
        for (int p = 0; p < parts; ++p)
        {
            const int64 a0 = s0 + (s1 - s0) * p / parts, a1 = s0 + (s1 - s0) * (p + 1) / parts;
            // the vowel: the voiced frames inside, 20 ms in from their edges
            int64 v0 = -1, v1 = -1; const int64 step = juce::jmax<int64> (1, T.hop);
            for (int64 s = a0; s < a1; s += step) if (voicedAt (s)) { if (v0 < 0) v0 = s; v1 = s; }
            const int64 in = (int64) (0.02 * sr);
            if (v0 >= 0 && v1 - v0 > 3 * in) push (a0, a1, v0 + in, v1 - in, 0);
            else push (a0, a1, a0 + (a1 - a0) / 6, a1 - (a1 - a0) / 6, 2);
        }
    }
    if (out.size() < 2)
    {
        out.clear();
        const int64 step = (int64) (0.4 * sr);
        for (int64 s0 = 0; s0 < src.frames; s0 += step) push (s0, juce::jmin (src.frames, s0 + step), s0 + step / 6, juce::jmin (src.frames, s0 + step - step / 6), 3);
    }
    return out;
}

// ---------------------------------------------------------------- music
const std::vector<int>& scaleOf (bool minor) { static const std::vector<int> maj { 0, 2, 4, 5, 7, 9, 11 }, mn { 0, 2, 3, 5, 7, 8, 10 }; return minor ? mn : maj; }
const std::vector<int>& pentatonicOf (bool minor) { static const std::vector<int> maj { 0, 2, 4, 7, 9 }, mn { 0, 3, 5, 7, 10 }; return minor ? mn : maj; }
bool inScale (int note, const Key& k) { const auto& sc = scaleOf (k.minor); const int pc = ((note - k.tonic) % 12 + 12) % 12; return std::find (sc.begin(), sc.end(), pc) != sc.end(); }
int snapToScale (int note, const Key& k)
{
    for (int d = 0; d <= 6; ++d) { if (inScale (note - d, k)) return note - d; if (inScale (note + d, k)) return note + d; }
    return note;
}
double snapToScale (double note, const Key& k)
{
    const int lo = snapToScale ((int) std::floor (note), k), hi = snapToScale ((int) std::ceil (note), k);
    return std::abs (note - lo) <= std::abs (hi - note) ? lo : hi;
}

namespace
{
// 4-chord progressions by scale degree (0-based) for the mood: pop / ambient / cinematic classics
const std::vector<std::vector<int>>& bank (bool minor)
{
    static const std::vector<std::vector<int>> maj { { 0, 4, 5, 3 }, { 0, 5, 3, 4 }, { 3, 0, 4, 5 }, { 0, 2, 3, 4 }, { 5, 3, 0, 4 }, { 0, 3, 5, 4 }, { 3, 4, 2, 5 }, { 0, 4, 3, 3 } };
    static const std::vector<std::vector<int>> mn  { { 0, 5, 2, 6 }, { 0, 3, 6, 2 }, { 0, 6, 5, 6 }, { 0, 5, 3, 4 }, { 5, 6, 0, 0 }, { 0, 3, 5, 6 }, { 0, 2, 5, 6 }, { 5, 3, 0, 6 } };
    return minor ? mn : maj;
}
juce::String chordName (const Key& k, int degree, const std::vector<int>& pcsRel)
{
    const auto& sc = scaleOf (k.minor);
    const int root = (k.tonic + sc[(size_t) degree]) % 12;
    const int third = pcsRel.size() > 1 ? pcsRel[1] : 4, fifth = pcsRel.size() > 2 ? pcsRel[2] : 7;
    juce::String q = fifth == 6 ? "dim" : third == 3 ? "m" : third == 2 ? "sus2" : "";
    if (pcsRel.size() > 3) q << (pcsRel[3] == 11 ? "maj7" : pcsRel[3] == 10 ? "7" : pcsRel[3] == 2 || pcsRel[3] == 14 ? "add9" : "");
    return analysis::pitchClassName (root) + q;
}
}

std::vector<Chord> progression (const Key& k, int centreNote, bool rich, uint32_t seed, uint32_t detail)
{
    Rng rng ((uint64) seed * 31 + 7), rd ((uint64) detail * 53 + 5);
    const auto& B = bank (k.minor);
    const auto& degs = B[(size_t) rng.pick ((int) B.size())];
    const auto& sc = scaleOf (k.minor);
    std::vector<Chord> out;
    std::vector<int> prev;
    const int lo = centreNote - 8, hi = centreNote + 10;
    for (int d : degs)
    {
        Chord c; c.degree = d;
        std::vector<int> rel { 0, (sc[(size_t) ((d + 2) % 7)] - sc[(size_t) d] + 12) % 12, (sc[(size_t) ((d + 4) % 7)] - sc[(size_t) d] + 12) % 12 };
        if (rich) { if (rd.chance (0.6)) rel.push_back ((sc[(size_t) ((d + 6) % 7)] - sc[(size_t) d] + 12) % 12); else rel.push_back (2); }   // 7th or add9
        c.name = chordName (k, d, rel);
        const int rootPc = (k.tonic + sc[(size_t) d]) % 12;
        // voice every pitch class to the octave nearest its predecessor (the first chord: spread around the centre), no doubles
        std::vector<int> notes;
        for (size_t i = 0; i < rel.size(); ++i)
        {
            const int pc = (rootPc + rel[i]) % 12;
            const int target = prev.empty() ? centreNote - 5 + (int) i * 4 : prev[juce::jmin (i, prev.size() - 1)];
            int best = -1, bd = 999;
            for (int n2 = lo; n2 <= hi; ++n2)
            {
                if ((n2 % 12 + 12) % 12 != pc || std::find (notes.begin(), notes.end(), n2) != notes.end()) continue;
                const int dist = std::abs (n2 - target);
                if (dist < bd) { bd = dist; best = n2; }
            }
            if (best >= 0) notes.push_back (best);
        }
        std::sort (notes.begin(), notes.end());
        // no two voices a semitone apart at the bottom (mud): lift the lower one an octave when there is room
        if (notes.size() >= 2 && notes[1] - notes[0] <= 2 && notes[0] + 12 <= hi + 2) { notes[0] += 12; std::sort (notes.begin(), notes.end()); }
        c.notes = notes; prev = notes;
        out.push_back (c);
    }
    return out;
}

std::vector<NoteEvent> melody (const Key& k, int centreNote, int bars, uint32_t seed, uint32_t detail)
{
    // a one-bar motif (rhythm + contour on the pentatonic), repeated with variation: A A' A B-cadence onto the tonic
    Rng rng ((uint64) seed * 17 + 3), rd ((uint64) detail * 29 + 1);
    const auto& pent = pentatonicOf (k.minor);
    std::vector<int> pool; for (int n = centreNote - 9; n <= centreNote + 9; ++n) if (std::find (pent.begin(), pent.end(), ((n - k.tonic) % 12 + 12) % 12) != pent.end()) pool.push_back (n);
    if (pool.empty()) pool.push_back (centreNote);
    auto nearestIdx = [&] (int note) { int b = 0; for (size_t i = 0; i < pool.size(); ++i) if (std::abs (pool[i] - note) < std::abs (pool[(size_t) b] - note)) b = (int) i; return b; };
    static const char* rhythms[] { "x.x.x...", "x..x..x.", "x...x.x.", "x.xx..x.", "x..x.x..", "xx..x...", "x.x..x..", "x...x..." };   // 8ths
    const char* rA = rhythms[rng.pick (8)];
    const char* rB = rhythms[rng.pick (8)];
    int tonicNear = centreNote; for (int d = 0; d <= 6; ++d) { if (((centreNote - d - k.tonic) % 12 + 12) % 12 == 0) { tonicNear = centreNote - d; break; } if (((centreNote + d - k.tonic) % 12 + 12) % 12 == 0) { tonicNear = centreNote + d; break; } }
    const int tonicIdx = nearestIdx (tonicNear);
    std::vector<int> contour; { int c = rng.pick (3) - 1; for (int i = 0; i < 8; ++i) { contour.push_back (c); c += rng.chance (0.65) ? (rng.chance (0.5) ? 1 : -1) : (rng.chance (0.5) ? 2 : -2); c = juce::jlimit (-4, 4, c); } }
    std::vector<NoteEvent> ev;
    bars = juce::jmax (2, bars);
    for (int bar = 0; bar < bars; ++bar)
    {
        const bool last = bar == bars - 1;
        const char* r = (bar % 4 == 3 || (bars == 2 && last)) ? rB : rA;
        const int lift = bar % 4 == 1 ? (rd.chance (0.5) ? 1 : -1) : bar % 4 == 2 ? (rd.chance (0.3) ? 2 : 0) : 0;
        int hit = 0;
        for (int s = 0; s < 8; ++s)
        {
            if (r[s] != 'x') continue;
            int nx = 8; for (int t = s + 1; t < 8; ++t) if (r[t] == 'x') { nx = t; break; }
            NoteEvent e; e.beat = bar * 4.0 + s * 0.5; e.length = (nx - s) * 0.5 * 0.92; e.velocity = 0.66f + 0.22f * (float) rd.uni() + (s == 0 ? 0.08f : 0.0f);
            int idx = tonicIdx + contour[(size_t) (hit % 8)] + lift;
            if (rd.chance (0.15)) idx += rd.chance (0.5) ? 1 : -1;
            e.note = pool[(size_t) juce::jlimit (0, (int) pool.size() - 1, idx)];
            ++hit;
            if (last && s >= 4) break;   // the cadence bar leaves room for the long tonic
            ev.push_back (e);
        }
        if (last) { NoteEvent e; e.note = pool[(size_t) tonicIdx]; e.beat = bar * 4.0 + 2.0; e.length = 2.0; e.velocity = 0.8f; ev.erase (std::remove_if (ev.begin(), ev.end(), [&] (const NoteEvent& x) { return x.beat >= e.beat; }), ev.end()); ev.push_back (e); }
    }
    return ev;
}

std::vector<NoteEvent> phrase (const Key& k, int centreNote, int nChops, const std::vector<bool>& isLong, int bars, uint32_t seed, uint32_t detail)
{
    Rng rng ((uint64) seed * 23 + 11), rd ((uint64) detail * 41 + 13);
    // 16-step patterns (vocal-chop style: syncopated, space on the back half); a = call, b = answer
    static const char* pats[] { "x..x..x...x.x...", "x...x..x..x...x.", "x.x...x...x.x...", "x..x....x..x..x.", "x...x...x.x...x.", "x.x.x...x...x.x.", "x..x..x.x...x...", "x.....x...x.x...", "x..x.x...x..x...", "..x...x.x..x...x" };
    const int pA = rng.pick (10); int pB = rng.pick (10); if (pB == pA) pB = (pB + 3) % 10;
    const auto& pent = pentatonicOf (k.minor);
    std::vector<int> pool; for (int n = centreNote - 7; n <= centreNote + 7; ++n) if (std::find (pent.begin(), pent.end(), ((n - k.tonic) % 12 + 12) % 12) != pent.end()) pool.push_back (n);
    if (pool.empty()) pool.push_back (centreNote);
    std::vector<int> longs, shorts; for (int i = 0; i < nChops; ++i) ((size_t) i < isLong.size() && isLong[(size_t) i] ? longs : shorts).push_back (i);
    if (longs.empty()) longs.push_back (0); if (shorts.empty()) shorts = longs;
    std::vector<NoteEvent> ev;
    std::vector<NoteEvent> callBar;
    int idx = (int) pool.size() / 2;
    bars = juce::jmax (2, bars);
    for (int bar = 0; bar < bars; ++bar)
    {
        const bool answer = bar >= bars / 2;
        const char* pat = pats[(bar % 2 == 0) ? pA : pB];
        if (answer && bar == bars / 2 && ! callBar.empty())
        {   // the answer opens with the call's rhythm, moved in pitch (a classic call / answer)
            const int mv = rd.chance (0.5) ? 2 : -2;
            for (auto e : callBar) { e.beat += (double) bar * 4.0; const int i2 = juce::jlimit (0, (int) pool.size() - 1, (int) (std::find (pool.begin(), pool.end(), e.note) - pool.begin()) + mv); e.note = pool[(size_t) i2]; e.reverse = e.reverse || (e.length >= 0.75 && rd.chance (0.2)); ev.push_back (e); }
            continue;
        }
        int lastChop = -1;
        for (int st = 0; st < 16; ++st)
        {
            if (pat[st] != 'x') continue;
            int next = 16; for (int t = st + 1; t < 16; ++t) if (pat[t] == 'x') { next = t; break; }
            NoteEvent e; e.beat = bar * 4.0 + st * 0.25; e.length = (next - st) * 0.25;
            e.note = pool[(size_t) idx]; e.velocity = 0.72f + 0.2f * (float) rd.uni() + (st % 4 == 0 ? 0.06f : 0.0f);
            const bool longNote = e.length >= 0.75;
            int chop = longNote ? longs[(size_t) rng.pick ((int) longs.size())] : shorts[(size_t) rng.pick ((int) shorts.size())];
            if (lastChop >= 0 && rd.chance (0.3)) chop = lastChop;   // the same chop twice: the classic repeat
            e.chop = juce::jlimit (0, juce::jmax (0, nChops - 1), chop); lastChop = e.chop;
            e.reverse = longNote && rd.chance (0.14);
            ev.push_back (e);
            int step = rng.chance (0.55) ? (rng.chance (0.5) ? 1 : -1) : rng.chance (0.5) ? 0 : (rng.chance (0.5) ? 2 : -2);
            if (idx + step < 0 || idx + step >= (int) pool.size()) step = -step;
            idx = juce::jlimit (0, (int) pool.size() - 1, idx + step);
        }
        if (bar == 0) { callBar.clear(); for (auto& e : ev) if (e.beat < 4.0) callBar.push_back (e); }
    }
    // the answer lands on the tonic
    if (! ev.empty()) { auto& e = ev.back(); e.note = snapToScale (centreNote, k); for (int n : pool) if (((n - k.tonic) % 12 + 12) % 12 == 0 && std::abs (n - centreNote) <= 6) e.note = n; }
    // one stutter (a 1/32 ratchet) on a long hit of the last bar, sometimes one in the call too
    for (int i = (int) ev.size() - 1; i >= 0; --i)
        if (ev[(size_t) i].beat >= (bars - 1) * 4.0 && ev[(size_t) i].length >= 0.5 && ! ev[(size_t) i].reverse) { ev[(size_t) i].stutter = 3; break; }
    if (rd.chance (0.5)) for (auto& e : ev) if (e.beat >= 1.0 && e.beat < 4.0 && e.length >= 0.5 && ! e.reverse) { e.stutter = 2; break; }
    std::sort (ev.begin(), ev.end(), [] (const NoteEvent& a, const NoteEvent& b) { return a.beat < b.beat; });
    return ev;
}

std::vector<NoteEvent> chordNotes (const std::vector<Chord>& chords, double bpc, double lengthBeats)
{
    std::vector<NoteEvent> ev; if (chords.empty() || bpc <= 0) return ev;
    int i = 0;
    for (double b = 0; b < lengthBeats - 1e-9; b += bpc, ++i)
        for (int n : chords[(size_t) i % chords.size()].notes) { NoteEvent e; e.note = n; e.beat = b; e.length = juce::jmin (bpc, lengthBeats - b); e.velocity = 0.6f; ev.push_back (e); }
    return ev;
}
void addChordTrack (juce::MidiFile& f, const std::vector<NoteEvent>& ev, int channel)
{
    if (ev.empty()) return;
    const double ppq = f.getTimeFormat() > 0 ? (double) f.getTimeFormat() : 960.0;
    juce::MidiMessageSequence seq;
    seq.addEvent (juce::MidiMessage::textMetaEvent (3, "CHORDS"), 0.0);   // (the track's name)
    for (auto& e : ev)
    {
        const double t0 = std::round (e.beat * ppq), t1 = std::max (t0 + 1, std::round ((e.beat + juce::jmax (0.05, e.length)) * ppq) - 1);
        seq.addEvent (juce::MidiMessage::noteOn (channel, juce::jlimit (0, 127, e.note), (juce::uint8) juce::jlimit (1, 127, (int) std::lround (e.velocity * 127))), t0);
        seq.addEvent (juce::MidiMessage::noteOff (channel, juce::jlimit (0, 127, e.note)), t1);
    }
    seq.updateMatchedPairs(); seq.sort();
    seq.addEvent (juce::MidiMessage::endOfTrack(), seq.getEndTime() + 1);
    f.addTrack (seq);
}

std::vector<NoteEvent> notesOf (const Take& t)
{
    if (! t.notes.empty()) return t.notes;
    std::vector<NoteEvent> out;
    const double tpq = t.midi.getTimeFormat() > 0 ? (double) t.midi.getTimeFormat() : 960.0;
    for (int tr = 0; tr < t.midi.getNumTracks(); ++tr)
        if (auto* seq = t.midi.getTrack (tr))
        {
            juce::MidiMessageSequence sq (*seq); sq.updateMatchedPairs();
            for (int i = 0; i < sq.getNumEvents(); ++i)
            {
                auto* e = sq.getEventPointer (i);
                if (! e->message.isNoteOn()) continue;
                NoteEvent n; n.note = e->message.getNoteNumber(); n.beat = e->message.getTimeStamp() / tpq; n.velocity = e->message.getFloatVelocity();
                n.length = e->noteOffObject != nullptr ? juce::jmax (0.05, (e->noteOffObject->message.getTimeStamp() - e->message.getTimeStamp()) / tpq) : 0.5;
                out.push_back (n);
            }
        }
    std::stable_sort (out.begin(), out.end(), [] (const NoteEvent& a, const NoteEvent& b) { return a.beat != b.beat ? a.beat < b.beat : a.note < b.note; });
    return out;
}

juce::MidiFile toMidi (const std::vector<NoteEvent>& ev, double bpm, int channel)
{
    juce::MidiMessageSequence seq;
    const double ppq = 960.0;
    seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::lround (60.0e6 / juce::jmax (20.0, bpm))), 0.0);
    seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);
    for (auto& e : ev)
    {
        const double t0 = std::round (e.beat * ppq), t1 = std::max (t0 + 1, std::round ((e.beat + juce::jmax (0.05, e.length)) * ppq) - 1);
        seq.addEvent (juce::MidiMessage::noteOn (channel, juce::jlimit (0, 127, e.note), (juce::uint8) juce::jlimit (1, 127, (int) std::lround (e.velocity * 127))), t0);
        seq.addEvent (juce::MidiMessage::noteOff (channel, juce::jlimit (0, 127, e.note)), t1);
    }
    seq.updateMatchedPairs(); seq.sort();
    seq.addEvent (juce::MidiMessage::endOfTrack(), seq.getEndTime() + 1);
    juce::MidiFile f; f.setTicksPerQuarterNote ((int) ppq); f.addTrack (seq);
    return f;
}

// ---------------------------------------------------------------- prepare
double Prepared::f0AtWork (double w) const
{
    if (analysis == nullptr) return 0.0;
    const auto& t = analysis->pitch;
    if (t.frames() == 0) return 0.0;
    const int i = juce::jlimit (0, t.frames() - 1, (int) std::lround (w / kS / t.hop));
    return t.f0[(size_t) i] > 0 && t.voicedProb[(size_t) i] >= 0.4 ? (double) t.f0[(size_t) i] : 0.0;
}

std::shared_ptr<const voc::Frames> Prepared::vocoder() const
{
    std::lock_guard<std::mutex> l (vocMutex);
    if (vocFrames == nullptr && work != nullptr)
    {
        const auto& wb = work->buffer();
        std::vector<float> mono ((size_t) wb.getNumSamples(), 0.0f);
        for (int ch = 0; ch < wb.getNumChannels(); ++ch) { const float* x = wb.getReadPointer (ch); for (int i = 0; i < wb.getNumSamples(); ++i) mono[(size_t) i] += x[i] / (float) wb.getNumChannels(); }
        vocFrames = voc::analyse (mono.data(), (int) mono.size(), kRate, [this] (double w) { return f0AtWork (w); });
    }
    return vocFrames;
}

std::shared_ptr<const Prepared> prepare (std::shared_ptr<const SourceAudio> src, std::shared_ptr<const analysis::Result> a, const Progress& p)
{
    if (src == nullptr || src->frames < (int64) (0.15 * src->sampleRate)) return nullptr;
    juce::ScopedNoDenormals nd;
    auto P = std::make_shared<Prepared>();
    // a long file: GENERATE works on its first 40 s (a minute of song is plenty of material)
    const int64 cap = (int64) (40.0 * src->sampleRate);
    if (src->frames > cap)
    {
        auto s = std::make_shared<SourceAudio> (*src);
        s->audio.setSize (s->audio.getNumChannels(), (int) cap, true, false, false); s->frames = cap; s->peaks.build (s->audio);
        s->fingerprint = src->fingerprint + ":40s";
        src = s; a = nullptr;
    }
    P->src = src;
    if (p.report) p.report (0.02f);
    if (a == nullptr || a->fingerprint != src->fingerprint) a = analysis::analyse (*src, analysis::Material::Auto, {});
    if (a == nullptr || (p.cancelled && p.cancelled())) return nullptr;
    P->analysis = a;
    if (p.report) p.report (0.35f);
    P->work = source::makeWorking (src, kRate);
    P->kS = P->work->ratio;
    P->material = findMaterial (*src, *a);
    if (P->material.empty())
    {
        Segment g; g.s0 = 0; g.s1 = src->frames; g.v0 = src->frames / 4; g.v1 = src->frames * 3 / 4; g.chop = 1; g.pad = 0.1; P->material.push_back (g);
    }
    // the key: the analysis' when it is sure enough, else the sound's own median pitch (minor: it sits under anything)
    if (a->key.valid() && a->key.confidence >= 0.25) { P->detected.tonic = a->key.tonic; P->detected.minor = a->key.minor; P->keyFromAnalysis = true; }
    else
    {
        double f = a->medianF0; if (f <= 0) for (auto& s : P->material) if (s.f0 > 0) { f = s.f0; break; }
        P->detected.tonic = f > 0 ? ((int) std::lround (hzToMidi (f)) % 12 + 12) % 12 : 9; P->detected.minor = true;
    }
    // band-limited copies (mono) for the grain clouds
    const auto& wb = P->work->buffer();
    juce::AudioBuffer<float> mono (1, wb.getNumSamples()); mono.clear();
    for (int c = 0; c < wb.getNumChannels(); ++c) mono.addFrom (0, 0, wb, c, 0, wb.getNumSamples(), 1.0f / (float) wb.getNumChannels());
    P->bank.build (mono, kRate);
    if (p.report) p.report (0.45f);
    return P;
}

Key keyFor (const Prepared& P, const Recipe& r)
{
    if (r.keyTonic >= 0) { Key k; k.tonic = r.keyTonic; k.minor = r.keyMinor; return k; }
    return P.detected;
}

// ---------------------------------------------------------------- the generator
namespace
{
struct Ctx
{
    const Prepared& P; const Recipe& r; const Progress& p;
    Key key; double sr = kRate;
    Rng rs, rd;   // structure (seed) / details (detail)
    Ctx (const Prepared& pp, const Recipe& rr, const Progress& pr) : P (pp), r (rr), p (pr), rs ((uint64) rr.seed * 1000003u + 1), rd ((uint64) rr.detail * 998244353u + 7) {}
    const juce::AudioBuffer<float>& W() const { return P.work->buffer(); }
    bool musical() const { return r.musical >= 0.5; }
    bool clear() const { return r.clear; }
    double m() const { return juce::jlimit (0.0, 1.0, r.musical); }
    int octave() const { return r.character == Character::Chipmunk ? 12 : r.character == Character::Deep ? -12 : 0; }
    double formantFor (double semisFromVoice) const { return r.character == Character::Chipmunk ? semisFromVoice : r.character == Character::Deep ? -4.0 : 0.0; }
    void report (float f) const { if (p.report) p.report (juce::jlimit (0.0f, 1.0f, f)); }
    bool cancelled() const { return p.cancelled && p.cancelled(); }
    double wf (int64 sf) const { return P.wf (sf); }
    uint32_t sub (uint32_t salt) const { return (uint32_t) ((uint64) r.detail * 2654435761u + salt * 40503u + r.seed); }
    std::vector<Segment> manual;   // the user's parts (Recipe::parts) as material, in time order (empty: the generator picks)
    std::shared_ptr<const Take> reuse;   // build 8: the same take before an edit (its zones / models are reused), or null
};

// a candidate ranked by a score: one of the best few, picked by the take's seed (good every time, different across takes)
const Segment& pickBy (Ctx& c, double Segment::* score, int topN)
{
    std::vector<const Segment*> v; for (auto& s : c.P.material) if (s.*score > 0) v.push_back (&s);
    if (v.empty()) { for (auto& s : c.P.material) v.push_back (&s); std::sort (v.begin(), v.end(), [] (auto* a, auto* b) { return a->rmsDb > b->rmsDb; }); return *v.front(); }
    std::stable_sort (v.begin(), v.end(), [score] (const Segment* a, const Segment* b) { return a->*score > b->*score; });
    const double best = v.front()->*score;
    std::vector<double> w; for (int i = 0; i < juce::jmin (topN, (int) v.size()); ++i) if (v[(size_t) i]->*score >= 0.55 * best) w.push_back (std::pow (v[(size_t) i]->*score, 2.0));
    return *v[(size_t) c.rs.weighted (w)];
}

// the main piece: one of the best few (seeded), or, with the user's parts, the best of those
const Segment& mainPiece (Ctx& c, double Segment::* score, int topN)
{
    if (c.manual.empty()) return pickBy (c, score, topN);
    const Segment* b = &c.manual.front(); for (auto& m : c.manual) if (m.*score > b->*score) b = &m;
    return *b;
}

// the stable core of a segment (WF) - the HOLD picker inside the vowel
void coreOf (const Ctx& c, const Segment& s, double& a, double& b)
{
    const int64 v0 = s.v1 > s.v0 ? s.v0 : s.s0, v1 = s.v1 > s.v0 ? s.v1 : s.s1;
    auto st = hold::pickStable (*c.P.src, v0, v1, c.P.analysis != nullptr ? &c.P.analysis->syllables : nullptr);
    const int64 minLen = (int64) (0.08 * c.P.src->sampleRate);
    if (st.b - st.a < minLen) { const int64 l = v1 - v0; st.a = v0 + l / 8; st.b = v1 - l / 8; }
    if (st.b - st.a < minLen) { st.a = v0; st.b = v1; }
    a = c.wf (st.a); b = c.wf (st.b);
}

Voice voiceOf (const Ctx& c, const Segment& s, double c0, double c1)
{
    const double from = c.wf (s.v1 > s.v0 ? s.v0 : s.s0), to = c.wf (s.v1 > s.v0 ? s.v1 : s.s1);
    return analyseVoice (c.W(), c.sr, from, c0, c1, to, [&c] (double w) { return c.P.f0AtWork (w); });
}

void levelTo (juce::AudioBuffer<float>& b, int from, int to, double db)
{
    const double r = rmsOf (b, from, to);
    if (r > 1e-7) b.applyGain ((float) juce::jlimit (0.02, 40.0, dbToGain (db) / r));
}

// CLEAR renders many zones: they are independent, so they run on the cores (each zone's random draws are made first, in order, so
// the take is the same bit for bit whatever the core count)
template <typename F> void forEachZone (int n, bool parallel, F&& f)
{
    if (! parallel || n < 2) { for (int i = 0; i < n; ++i) f (i); return; }
    const int T = juce::jlimit (1, 8, (int) std::thread::hardware_concurrency());
    std::atomic<int> next { 0 }; std::vector<std::thread> th;
    for (int k = 0; k < T; ++k) th.emplace_back ([&] { juce::ScopedNoDenormals nd; for (int i; (i = next.fetch_add (1)) < n;) f (i); });
    for (auto& x : th) x.join();
}

// a steady texture (a freeze) laid end to end to n frames: copies of `piece` joined by equal-power crossfades of xf frames
juce::AudioBuffer<float> tileTo (const juce::AudioBuffer<float>& piece, int n, int xf)
{
    const int pn = piece.getNumSamples(), chs = piece.getNumChannels();
    juce::AudioBuffer<float> o (chs, n); o.clear();
    if (pn <= 2 * xf) { for (int ch = 0; ch < chs; ++ch) o.copyFrom (ch, 0, piece, ch, 0, juce::jmin (n, pn)); return o; }
    const int stepN = pn - xf;
    for (int at = 0, first = 1; at < n; at += stepN, first = 0)
        for (int ch = 0; ch < chs; ++ch)
        {
            const float* x = piece.getReadPointer (ch); float* y = o.getWritePointer (ch);
            for (int i = 0; i < pn && at + i < n; ++i)
            {
                float w = 1.0f;
                if (! first && i < xf) w = (float) std::sin (juce::MathConstants<double>::halfPi * (i + 0.5) / xf);
                if (i >= pn - xf && at + stepN < n) w *= (float) std::cos (juce::MathConstants<double>::halfPi * (i - (pn - xf) + 0.5) / xf);
                y[at + i] += w * x[i];
            }
        }
    return o;
}

void swell (juce::AudioBuffer<float>& b, int len)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch) { float* y = b.getWritePointer (ch); for (int i = 0; i < juce::jmin (len, b.getNumSamples()); ++i) y[i] *= fadeShape ((double) i / len); }
}

// render events through the real voice engine (offline, dry): what the keys play is what the preview plays
juce::AudioBuffer<float> renderEvents (const Take& t, const std::vector<NoteEvent>& ev, double seconds)
{
    const double sr = t.rate; const int n = (int) (seconds * sr), block = 256;
    juce::AudioBuffer<float> out (2, n + block); out.clear();
    Engine eng; eng.prepare (sr, block);
    eng.publish (std::shared_ptr<const Take> (std::shared_ptr<Take>(), &t));
    const double spb = 60.0 / juce::jmax (20.0, t.bpm);
    std::vector<std::pair<int64, juce::MidiMessage>> msgs;
    for (auto& e : ev)
    {
        const int64 on = (int64) (e.beat * spb * sr), off = juce::jmax (on + 1, (int64) ((e.beat + e.length) * spb * sr) - 1);
        msgs.push_back ({ on, juce::MidiMessage::noteOn (1, e.note, e.velocity) });
        msgs.push_back ({ off, juce::MidiMessage::noteOff (1, e.note) });
    }
    std::stable_sort (msgs.begin(), msgs.end(), [] (auto& a, auto& b) { return a.first < b.first; });
    size_t k = 0; juce::MidiBuffer midi;
    for (int64 pos = 0; pos < n; pos += block)
    {
        midi.clear();
        while (k < msgs.size() && msgs[k].first < pos + block) { midi.addEvent (msgs[k].second, (int) juce::jmax<int64> (0, msgs[k].first - pos)); ++k; }
        eng.process (out.getWritePointer (0) + pos, out.getWritePointer (1) + pos, block, midi);
    }
    out.setSize (2, n, true, true, true);
    return out;
}

void finishPreview (const Ctx& c, Take& t)
{
    fadeEdges (t.preview, (int) (0.002 * c.sr), (int) (0.08 * c.sr));
    if (! (c.r.engine >= 6 && c.clear())) highpassInPlace (t.preview, c.sr, 28.0);   // (build 6 CLEAR: no filter at all, the recording stays the recording)
    finish (t.preview, c.sr, -16.0, -1.5, ! c.clear());   // (the FX chain adds the room and warmth: the export lands at -14 LUFS)
    juce::MemoryOutputStream mo;
    for (int ch = 0; ch < 2; ++ch) mo.write (t.preview.getReadPointer (ch), sizeof (float) * (size_t) t.preview.getNumSamples());
    t.digest = juce::SHA256 (mo.getData(), mo.getDataSize()).toHexString();
}

int centreFor (const Ctx& c, int root)
{   // the preview's centre: the root, an octave up / down for CHIPMUNK / DEEP, kept in a singable range, on a scale note
    int n = root + c.octave();
    while (n > 84) n -= 12; while (n < 36) n += 12;
    return snapToScale (n, c.key);
}

// ---------------------------------------------------------------- INSTRUMENT
void makeInstrument (Ctx& c, Take& t)
{
    const Segment& s = mainPiece (c, &Segment::instrument, 3);
    double c0, c1; coreOf (c, s, c0, c1);
    const Voice v = voiceOf (c, s, c0, c1);
    const bool pitched = v.ok();
    const double own = pitched ? hzToMidi (v.f0) : (s.midi > 0 ? s.midi : 60.0);
    const int root = c.musical() ? snapToScale ((int) std::lround (own), c.key) : (int) std::lround (own);
    const double corr = (root - own) * (c.musical() ? 1.0 : c.m() * 2.0);   // NATURAL keeps some of the voice's own tuning
    t.rootNote = root;
    c.report (0.5f);
    const double secs = 3.6; const int ls = (int) (1.0 * c.sr), le = (int) (3.5 * c.sr);
    const bool cl = c.clear();
    const int step = cl ? 1 : 3, zonesN = 36 / step + 1;   // CLEAR: a zone on every semitone (no key is resampled from a neighbour)
    std::vector<int> offs; std::vector<double> rdv; for (int off = -18; off <= 18; off += step) { offs.push_back (off); rdv.push_back (c.rd.uni()); }
    std::vector<Zone> zs (offs.size()); std::atomic<int> done { 0 };
    forEachZone ((int) offs.size(), cl, [&] (int zi)
    {
        if (c.cancelled()) return;
        const int off = offs[(size_t) zi];
        ToneSpec ts; ts.seconds = secs; ts.semis = off + corr; ts.formant = c.formantFor (ts.semis);
        ts.natural = 1.0 - c.m(); ts.vibratoCents = 11.0 * c.m(); ts.vibratoHz = 5.0 + 0.6 * rdv[(size_t) zi]; ts.vibratoDelayS = 0.45;
        ts.driftCents = 3.0 + 3.0 * (1.0 - c.m()); ts.width = 0.32; ts.walk = 1.0; ts.onset = true; ts.seed = c.sub ((uint32_t) (off + 40));
        if (cl) { ts.vibratoCents = 0; ts.driftCents = 0; ts.width = 0; ts.walk = 0.7; ts.clean = true; }   // steady, one voice
        Zone z; z.rootNote = root + off;
        z.audio = pitched ? psolaTone (v, ts) : grainTone (c.W(), c.P.bank, c.sr, c0, c1, ts);
        int zle = le, xf = (int) (0.3 * c.sr);
        if (cl)
        {   // mono (the voice is one voice), the loop end where the waveform is in phase with the loop start (a seamless join)
            z.audio = monoOf (z.audio);
            const double hz = pitched ? v.f0 * std::exp2 (ts.semis / 12.0) : 220.0;
            const int per = (int) std::ceil (c.sr / juce::jmax (40.0, hz));
            zle = phaseMatchedLoopEnd (z.audio, ls, le - per, per, juce::jmax (2 * per, (int) (0.02 * c.sr)));
            xf = (int) (0.12 * c.sr);
        }
        levelTo (z.audio, ls, zle, -18.0);
        bakeLoop (z.audio, ls, zle, xf);
        z.loopStart = ls; z.loopEnd = zle;
        zs[(size_t) zi] = std::move (z);
        c.report (0.5f + 0.35f * (float) (++done) / (float) zonesN);
    });
    if (c.cancelled()) return;
    for (auto& z : zs) t.zones.push_back (std::move (z));
    t.attackS = 0.006; t.releaseS = 0.32 + 0.25 * (1.0 - c.m()); t.pairDetune = false; t.ensemble = cl ? 0.0f : 0.25f;
    const int centre = centreFor (c, root);
    const auto ev = melody (c.key, centre, 4, c.r.seed, c.r.detail);
    t.midi = toMidi (ev, t.bpm);
    t.used = { { (double) s.s0 / c.P.src->sampleRate, (double) s.s1 / c.P.src->sampleRate } };
    t.info = "The '" + juce::String (s.kind == 0 ? "vowel" : "note") + "' at " + juce::String ((double) s.s0 / c.P.src->sampleRate, 1) + " s, held by the voice engine. Play it on your keys (its own note is "
             + juce::MidiMessage::getMidiNoteName (root, true, true, 5) + ").";
    t.preview = renderEvents (t, ev, 16.0 * 60.0 / t.bpm + t.releaseS + 0.6);
    finishPreview (c, t);
}

// ---------------------------------------------------------------- PAD
std::vector<GrainSource> grainSources (const Ctx& c, int maxN, bool pitchedOnly)
{
    if (! c.manual.empty())
    {   // the user's parts, every one of them (its vowel when it has one)
        std::vector<GrainSource> out;
        for (auto& s : c.manual)
        {
            GrainSource g; g.a = c.wf (s.v1 > s.v0 ? s.v0 : s.s0); g.b = c.wf (s.v1 > s.v0 ? s.v1 : s.s1); g.midi = s.midi; g.weight = 0.2 + s.pad;
            if (g.b - g.a < 0.06 * c.sr) { g.a = c.wf (s.s0); g.b = c.wf (s.s1); }
            if ((int) out.size() < maxN) out.push_back (g);
        }
        return out;
    }
    std::vector<const Segment*> v; for (auto& s : c.P.material) if (! s.breath && (! pitchedOnly || (s.midi > 0 && s.voiced >= 0.5))) v.push_back (&s);
    std::stable_sort (v.begin(), v.end(), [] (auto* a, auto* b) { return a->pad + 0.3 * a->chop > b->pad + 0.3 * b->chop; });
    std::vector<GrainSource> out;
    for (auto* s : v)
    {
        if ((int) out.size() >= maxN) break;
        GrainSource g; g.a = c.wf (s->v1 > s->v0 ? s->v0 : s->s0); g.b = c.wf (s->v1 > s->v0 ? s->v1 : s->s1); g.midi = s->midi; g.weight = 0.2 + s->pad;
        if (g.b - g.a < 0.06 * c.sr) { g.a = c.wf (s->s0); g.b = c.wf (s->s1); }
        if (g.b - g.a >= 0.05 * c.sr) out.push_back (g);
    }
    if (out.empty() && pitchedOnly) return grainSources (c, maxN, false);
    return out;
}

void makePad (Ctx& c, Take& t)
{
    const Segment& s = mainPiece (c, &Segment::pad, 3);
    double c0, c1; coreOf (c, s, c0, c1);
    const Voice v = voiceOf (c, s, c0, c1);
    const bool pitched = v.ok();
    const double own = pitched ? hzToMidi (v.f0) : (s.midi > 0 ? s.midi : 57.0);
    const int root = c.musical() ? snapToScale ((int) std::lround (own), c.key) : (int) std::lround (own);
    const double corr = (root - own) * (c.musical() ? 1.0 : c.m() * 2.0);
    t.rootNote = root;
    const auto sources = grainSources (c, 8, true);
    c.report (0.5f);
    const double secs = 6.4; const int ls = (int) (1.6 * c.sr), le = (int) (6.2 * c.sr);
    const bool cl = c.clear();
    const int step = cl ? 1 : 4, zonesN = 32 / step + 1;   // CLEAR: a zone on every semitone
    std::vector<int> offs; std::vector<double> rdv; for (int off = -16; off <= 16; off += step) { offs.push_back (off); rdv.push_back (c.rd.uni()); }
    std::vector<Zone> zs (offs.size()); std::atomic<int> done { 0 };
    forEachZone ((int) offs.size(), cl, [&] (int zi)
    {
        if (c.cancelled()) return;
        const int off = offs[(size_t) zi];
        const double zn = root + off;
        ToneSpec ts; ts.seconds = secs; ts.semis = off + corr; ts.formant = c.formantFor (ts.semis);
        ts.natural = 0.5 * (1.0 - c.m()); ts.vibratoCents = 0; ts.driftCents = 7.0; ts.width = 0.9; ts.walk = 1.6; ts.onset = false; ts.seed = c.sub ((uint32_t) (off + 100));
        if (cl) { ts.driftCents = 0.0; ts.width = 0.0; ts.walk = 0.8; ts.clean = true; }   // the direct voice: steady, defined
        auto body = pitched ? psolaTone (v, ts) : grainTone (c.W(), c.P.bank, c.sr, c0, c1, ts);
        if (cl) body = monoOf (body);
        levelTo (body, 0, body.getNumSamples(), -20.0);
        juce::AudioBuffer<float> glass;
        if (cl)
        {   // the glass is a steady texture: 2.4 s of it, laid end to end with equal-power joins (a third of the work for 33 zones)
            auto piece = monoOf (c.m() >= 0.35 ? freeze (body, c.sr, 0.25 * c.sr, (secs - 0.1) * c.sr, 2.4, ts.seed + 1)
                                               : blur (body, c.sr, 0.25 * c.sr, (secs - 0.1) * c.sr, 2.4, 0.55, ts.seed + 1));
            glass = tileTo (piece, (int) (secs * c.sr), (int) (0.4 * c.sr));
        }
        else glass = c.m() >= 0.35 ? freeze (body, c.sr, 0.25 * c.sr, (secs - 0.1) * c.sr, secs, ts.seed + 1)
                                   : blur (body, c.sr, 0.25 * c.sr, (secs - 0.1) * c.sr, secs, 0.55, ts.seed + 1);
        levelTo (glass, 0, glass.getNumSamples(), -20.0);
        CloudSpec cs; cs.seconds = secs; cs.density = 16.0 + 14.0 * c.m(); cs.grainS = 0.11; cs.grainJitter = 0.45; cs.taper = 0.6; cs.spray = 0.35; cs.headSpeed = 0.2;
        cs.notes = { zn, zn + 12, zn + 7, zn - 12 }; cs.noteWeights = { 1.0, 0.25, c.musical() ? 0.22 : 0.0, 0.2 }; cs.foldOctaves = false; cs.natural = 1.0 - c.m();
        cs.scatterCents = 8; cs.spread = 0.95; cs.reverse = 0.15; cs.gainJitterDb = 4.0; cs.seed = ts.seed + 2;
        if (cl)
        {   // longer, calmer grains: less spray / jitter / detune, a slow head, no reversed grains, one channel
            cs.density = 10.0 + 8.0 * c.m(); cs.grainS = 0.26; cs.grainJitter = 0.15; cs.taper = 0.5; cs.spray = 0.1; cs.headSpeed = 0.12;
            cs.scatterCents = 2; cs.spread = 0; cs.reverse = 0; cs.gainJitterDb = 1.5; cs.clean = true; cs.channels = 1;
        }
        const double ph = rdv[(size_t) zi] * 6.28;
        cs.densityAt = [ph] (double tt) { return 0.7 + 0.3 * std::sin (juce::MathConstants<double>::twoPi * tt / 5.3 + ph); };
        auto grains = cloud (c.W(), c.P.bank, c.sr, sources, cs);
        levelTo (grains, 0, grains.getNumSamples(), -20.0);
        Zone z; z.rootNote = (int) zn; z.audio.setSize (cl ? 1 : 2, (int) (secs * c.sr)); z.audio.clear();
        if (cl) { addInto (z.audio, body, 0, 0.8f); addInto (z.audio, glass, 0, 0.22f); addInto (z.audio, grains, 0, 0.3f); }   // the voice in front
        else { addInto (z.audio, glass, 0, (float) (0.5 + 0.2 * c.m())); addInto (z.audio, body, 0, (float) (0.3 + 0.25 * (1.0 - c.m()))); addInto (z.audio, grains, 0, 0.45f); }
        swell (z.audio, (int) (0.35 * c.sr));
        int zle = le;
        if (cl) { const int per = (int) std::ceil (c.sr / juce::jmax (40.0, midiToHz (zn))); zle = phaseMatchedLoopEnd (z.audio, ls, le - per, per, juce::jmax (2 * per, (int) (0.03 * c.sr))); }
        levelTo (z.audio, ls, zle, -19.0);
        bakeLoop (z.audio, ls, zle, (int) ((cl ? 0.5 : 1.1) * c.sr));
        z.loopStart = ls; z.loopEnd = zle;
        zs[(size_t) zi] = std::move (z);
        c.report (0.5f + 0.3f * (float) (++done) / (float) zonesN);
    });
    if (c.cancelled()) return;
    for (auto& z : zs) t.zones.push_back (std::move (z));
    t.attackS = 0.45 + 0.6 * c.m(); t.releaseS = 1.8; t.pairDetune = ! cl; t.ensemble = cl ? 0.0f : 0.85f;
    // the progression: 4 chords, 2 bars each, voiced around the centre with smooth voice leading
    const int centre = centreFor (c, root);
    const auto chords = progression (c.key, centre, c.m() >= 0.55, c.r.seed, c.r.detail);
    std::vector<NoteEvent> ev; juce::String names;
    for (size_t i = 0; i < chords.size(); ++i)
    {
        names << (i ? " - " : "") << chords[i].name;
        for (int n : chords[i].notes) { NoteEvent e; e.note = n; e.beat = (double) i * 8.0; e.length = 8.0; e.velocity = 0.7f; ev.push_back (e); }
        if (c.musical() && ! chords[i].notes.empty())
        {   // the chord's root under it
            const int rootPc = ((c.key.tonic + scaleOf (c.key.minor)[(size_t) chords[i].degree]) % 12);
            int bass = chords[i].notes.front() - 1; while (((bass % 12) + 12) % 12 != rootPc) --bass;
            if (bass >= root - 18) { NoteEvent e; e.note = bass; e.beat = (double) i * 8.0; e.length = 8.0; e.velocity = 0.5f; ev.push_back (e); }
        }
    }
    t.midi = toMidi (ev, t.bpm);
    const double spb = 60.0 / t.bpm, total = 32.0 * spb + t.releaseS + 0.4;
    t.preview = renderEvents (t, ev, total);
    c.report (0.86f);
    // the chord-following grain layer: small chops from the whole sound, pitched onto each chord's tones
    juce::AudioBuffer<float> layer (2, t.preview.getNumSamples()); layer.clear();
    const auto all = grainSources (c, 12, true);
    for (size_t i = 0; i < chords.size(); ++i)
    {
        if (c.cancelled()) return;
        CloudSpec cs; cs.seconds = 8.0 * spb + 1.0; cs.density = 9.0 + 6.0 * c.m(); cs.grainS = 0.16; cs.grainJitter = 0.5; cs.taper = 0.7; cs.spray = 0.5; cs.headSpeed = 0.35;
        for (int n : chords[i].notes) { cs.notes.push_back (n); cs.noteWeights.push_back (1.0); }
        cs.foldOctaves = true; cs.natural = 0.3 * (1.0 - c.m()); cs.scatterCents = 6; cs.spread = 1.0; cs.reverse = 0.2; cs.fadeS = 0.5; cs.seed = c.sub ((uint32_t) (500 + i));
        if (c.clear()) { cs.density = 6.0 + 4.0 * c.m(); cs.grainS = 0.28; cs.grainJitter = 0.15; cs.spray = 0.15; cs.headSpeed = 0.2; cs.scatterCents = 2; cs.spread = 0; cs.reverse = 0; cs.gainJitterDb = 1.5; cs.clean = true; }
        auto g = cloud (c.W(), c.P.bank, c.sr, all, cs);
        addInto (layer, g, (int) ((double) i * 8.0 * spb * c.sr));
    }
    levelTo (layer, 0, layer.getNumSamples(), dsp::gainToDb (rmsOf (t.preview)) - (c.clear() ? 11.0 : 7.0));
    addInto (t.preview, layer, 0);
    t.used.clear(); t.used.push_back ({ (double) s.s0 / c.P.src->sampleRate, (double) s.s1 / c.P.src->sampleRate });
    for (auto& g : sources) t.used.push_back ({ g.a / c.sr, g.b / c.sr });
    t.info = names + ". Grains of " + juce::String ((int) sources.size()) + " parts of your sound over a frozen '" + juce::String (s.kind == 0 ? "vowel" : "tone") + "'. Play chords on your keys.";
    finishPreview (c, t);
}

// ---------------------------------------------------------------- CHOPS
std::vector<NoteEvent> phrase6 (Ctx& c, Take& t, int centre, int nChops, const std::vector<bool>& isLong, int bars);   // (GenerateV6.inl)
juce::AudioBuffer<float> grainsOf (const juce::AudioBuffer<float>& src, double sr, double seconds, double density, double grainS, double spray, int window, uint32_t seed);   // (GenerateV6.inl)
void makeChops (Ctx& c, Take& t)
{
    const bool v6 = c.r.engine >= 6;
    const double ssr = c.P.src->sampleRate;
    std::vector<const Segment*> cand;
    for (auto& s : c.P.material) { const double l = s.seconds (ssr); if (s.chop > 0.15 && l >= 0.07 && l <= 1.6) cand.push_back (&s); }
    if (cand.empty()) for (auto& s : c.P.material) cand.push_back (&s);
    std::stable_sort (cand.begin(), cand.end(), [] (const Segment* x, const Segment* y) { return x->instrument * 0.6 + x->chop * 0.4 > y->instrument * 0.6 + y->chop * 0.4; });
    // the take's chops: the best few, the rest of the slots drawn from the next best by the seed (so takes use different words)
    std::vector<const Segment*> pick;
    if (! c.manual.empty()) { cand.clear(); for (auto& m : c.manual) pick.push_back (&m); }   // the user's parts: one chop each, in order
    for (size_t i = 0; i < cand.size() && pick.size() < 3; ++i) pick.push_back (cand[i]);
    std::vector<const Segment*> rest (cand.begin() + (long) juce::jmin<size_t> (3, cand.size()), cand.begin() + (long) juce::jmin<size_t> (14, cand.size()));
    while (pick.size() < 8 && ! rest.empty()) { const int i = (v6 && c.r.variation > 0 ? c.rd : c.rs).pick ((int) rest.size()); pick.push_back (rest[(size_t) i]); rest.erase (rest.begin() + i); }   // (VARY: other words)
    std::stable_sort (pick.begin(), pick.end(), [] (auto* a, auto* b) { return a->s0 < b->s0; });   // in the order they were said
    const int n = (int) pick.size();
    std::vector<double> own; std::vector<bool> isLong;
    for (auto* s : pick)
    {
        auto b = slice (c.W(), c.wf (s->s0) - (c.manual.empty() ? 0.004 * c.sr : 0.0), c.wf (s->s1));   // (a user's part: exactly the box)
        fadeEdges (b, (int) (0.003 * c.sr), (int) (0.012 * c.sr));
        levelTo (b, 0, b.getNumSamples(), -17.0);
        if (peakOf (b) > 0.9f) b.applyGain (0.9f / peakOf (b));
        t.chops.push_back (std::move (b)); own.push_back (s->midi);
        isLong.push_back (s->voiced >= 0.5 && s->seconds (ssr) >= 0.22);
    }
    c.report (0.55f);
    double med = 0; { std::vector<double> m; for (double p : own) if (p > 0) m.push_back (p); std::sort (m.begin(), m.end()); med = m.empty() ? 60.0 : m[m.size() / 2]; }
    const int root = snapToScale ((int) std::lround (med), c.key);
    const bool cl = c.clear();
    t.rootNote = root; t.attackS = 0.002; t.releaseS = 0.06; t.ensemble = cl ? 0.0f : 0.15f;
    const bool keepF = c.r.character != Character::Chipmunk; const double fSemis = c.r.character == Character::Deep ? -4.0 : 0.0;
    // CLEAR: a chop that needs (almost) no repitching stays the original audio, sample for sample; a small correction (half a
    // semitone or less, NATURAL character) is a varispeed (r8brain: every transient intact, the formants move 3 % at most, the
    // length the same 3 %); only real moves go through the phase vocoder (formants kept)
    auto repitch = [&] (const juce::AudioBuffer<float>& b, double semis, double f0Hz) -> juce::AudioBuffer<float>
    {
        if (cl && keepF && fSemis == 0.0)
        {
            if (std::abs (semis) < 0.15) { juce::AudioBuffer<float> o; o.makeCopyOf (b); return o; }
            if (std::abs (semis) <= 0.5) return varispeed (b, c.sr, std::exp2 (semis / 12.0));
        }
        return shift (b, c.sr, semis, keepF, fSemis, f0Hz);
    };
    // the chops on the keys: in tune (MUSICAL), as said (NATURAL)
    for (int i = 0; i < n; ++i)
    {
        if (own[(size_t) i] <= 0 || c.cancelled()) continue;
        const double semis = (snapToScale (own[(size_t) i], c.key) - own[(size_t) i]) * (c.musical() ? 1.0 : c.m() * 2.0) + c.octave();
        if (std::abs (semis) > 0.04 || ! keepF || fSemis != 0.0) t.chops[(size_t) i] = cl ? repitch (t.chops[(size_t) i], semis, midiToHz (own[(size_t) i])) : shift (t.chops[(size_t) i], c.sr, semis, keepF, fSemis, midiToHz (own[(size_t) i]));
    }
    const int bars = v6 ? juce::jlimit (1, 16, c.r.bars) : (c.rs.chance (0.3) ? 2 : 4);
    const int centre = centreFor (c, root);
    auto ev = v6 ? phrase6 (c, t, centre, n, isLong, bars) : phrase (c.key, centre, n, isLong, bars, c.r.seed, c.r.detail);
    if (v6) ev = edit::editLine (ev, c.r.edit, c.key, bars, true);   // (build 8: your notes on the roll; nothing when there are none)
    std::vector<float> fatV; if (v6) for (auto& x : edit::playLine (ev)) fatV.push_back (x.velocity);   // (FATTEN: the next chop louder)
    const double spb = 60.0 / t.bpm;
    juce::AudioBuffer<float> out (2, (int) ((bars * 4.0 * spb + 1.0) * c.sr)); out.clear();
    std::map<std::pair<int, int>, juce::AudioBuffer<float>> cache;
    for (auto& e : ev)
    {
        if (c.cancelled()) return;
        const float vel = fatV.empty() ? e.velocity : fatV[(size_t) (&e - ev.data())];
        const int ci = juce::jlimit (0, n - 1, e.chop);
        const double o = own[(size_t) ci];
        double semis = 0;
        if (o > 0)
        {
            if (c.r.engine >= 7 && e.mine) { semis = juce::jlimit (-19.0, 19.0, (double) e.note - (o + c.octave())); e.note = (int) std::lround (o + c.octave() + semis); }   // (build 9: your note's octave too)
            else if (c.musical()) { double full = (double) e.note - (o + c.octave()); full -= 12.0 * std::round (full / 12.0); if (full > 7) full -= 12; if (full < -7) full += 12; semis = full; e.note = (int) std::lround (o + c.octave() + semis); }
            else { semis = (snapToScale (o, c.key) - o) * c.m() * 2.0; e.note = (int) std::lround (o + c.octave() + semis); }
        }
        // (the chops on the keys are already moved by the octave / tuning; the phrase moves the raw take of them)
        const double fromKeys = o > 0 ? (snapToScale (o, c.key) - o) * (c.musical() ? 1.0 : c.m() * 2.0) : 0.0;
        const double qs = cl ? 100.0 : 4.0;   // (CLEAR: the move to the cent, not the quarter tone)
        const int q = (int) std::lround ((semis - fromKeys) * qs);
        const auto key = std::make_pair (ci, q);
        if (cache.find (key) == cache.end())
        {
            const double f0 = o > 0 ? midiToHz (o + fromKeys + c.octave()) : 0.0;
            if (cl) { const bool tiny = std::abs (q / qs) < 0.15; cache[key] = tiny ? t.chops[(size_t) ci] : (keepF ? (std::abs (q / qs) <= 0.5 ? varispeed (t.chops[(size_t) ci], c.sr, std::exp2 (q / qs / 12.0)) : shift (t.chops[(size_t) ci], c.sr, q / qs, true, 0.0, f0)) : shift (t.chops[(size_t) ci], c.sr, q / qs, false, 0.0, f0)); }
            else cache[key] = q == 0 ? t.chops[(size_t) ci] : shift (t.chops[(size_t) ci], c.sr, q / 4.0, keepF, 0.0, f0);
        }
        juce::AudioBuffer<float> piece; piece.makeCopyOf (cache[key]);
        const bool bigMove = c.r.engine >= 7 && e.mine && std::abs (semis) > 4.0;   // (build 9: an octave move keeps no attack at the old pitch)
        if (v6 && q != 0 && ! e.reverse && ! bigMove)
        {   // TRANSIENTS: the chop's attack exactly as recorded (a re-pitch smears it), then a 3 ms crossfade into the re-pitched rest
            const double tk = c.r.deepOr (Type::Chops, 4);
            const auto& base = t.chops[(size_t) ci];
            const int att = (int) ((0.004 + 0.022 * tk) * c.sr), xf = (int) (0.003 * c.sr);
            if (tk > 0.02 && att + xf < juce::jmin (piece.getNumSamples(), base.getNumSamples()))
                for (int ch = 0; ch < piece.getNumChannels(); ++ch)
                {
                    const float* o = base.getReadPointer (juce::jmin (ch, base.getNumChannels() - 1)); float* d = piece.getWritePointer (ch);
                    for (int i = 0; i < att + xf; ++i) { const float w = i < att ? 1.0f : 1.0f - (float) (i - att) / (float) xf; d[i] = w * o[i] + (1.0f - w) * d[i]; }
                }
        }
        const int maxLen = (int) ((e.length * spb + 0.05) * c.sr);
        if (piece.getNumSamples() > maxLen && maxLen > 256) piece.setSize (2, maxLen, true, true, true);
        if (e.reverse) piece.reverse (0, piece.getNumSamples());
        fadeEdges (piece, (int) (0.003 * c.sr), (int) juce::jmin (0.025 * c.sr, piece.getNumSamples() / 3.0));
        const int at = (int) (e.beat * spb * c.sr);
        if (e.stutter > 0)
        {   // a ratchet: the chop's start, re-hit on 1/32 notes, falling a little
            const int step = (int) (spb * c.sr / 8.0);
            for (int r = 0; r < e.stutter; ++r)
            {
                juce::AudioBuffer<float> st; st.makeCopyOf (piece); if (st.getNumSamples() > step) st.setSize (2, step, true, true, true);
                fadeEdges (st, (int) (0.002 * c.sr), (int) juce::jmin (0.012 * c.sr, st.getNumSamples() / 3.0));
                addInto (out, st, at + r * step, vel * (1.0f - 0.12f * (float) r));
            }
            addInto (out, piece, at + e.stutter * step, vel * 0.85f);
        }
        else addInto (out, piece, at, vel);
    }
    t.preview = std::move (out);
    t.midi = toMidi (v6 ? edit::midiLine (ev) : ev, t.bpm);
    for (auto* s : pick) t.used.push_back ({ (double) s->s0 / ssr, (double) s->s1 / ssr });
    t.info = juce::String (n) + " chops in a " + juce::String (bars) + "-bar phrase. Keys: C3 and up play the chops, C5 (middle C) plays the whole phrase.";
    t.notes = ev;   // (build 10: also an older take's, with their chop numbers: an edit upgrades it keeping its phrase; no sound changes)
    if (v6) { t.chain = { "CHOPS", "PITCH (STRETCH)", "PHRASE", "STUTTER", "SPACE" }; t.info = juce::String (styleName (t.style)) + " phrase: " + t.info; }
    finishPreview (c, t);
}

// ---------------------------------------------------------------- TEXTURE
void makeTexture (Ctx& c, Take& t)
{
    const bool v6 = c.r.engine >= 6;
    const double drift = v6 ? c.r.knobOr (Type::Texture) : 0.0;
    const double gSize = v6 ? c.r.deepOr (Type::Texture, 0) : 0.5, gDens = v6 ? c.r.deepOr (Type::Texture, 1) : 0.5, gSpray = v6 ? c.r.deepOr (Type::Texture, 2) : 0.5, gFrz = v6 ? c.r.deepOr (Type::Texture, 3) : 0.5;
    const Segment& s = mainPiece (c, &Segment::pad, 3);
    const double ssr = c.P.src->sampleRate;
    // the drone's material: the voiced cores of the best vowels (this one first), joined with short crossfades, about 1.5 s. Only
    // voiced parts: a frozen "s" or breath is hiss, not a tone.
    std::vector<const Segment*> parts { &s };
    if (! c.manual.empty()) { for (auto& o : c.manual) if (&o != &s) parts.push_back (&o); }   // (the user's parts: all of them)
    else { std::vector<const Segment*> v; for (auto& o : c.P.material) if (&o != &s && o.pad > 0.25 && o.midi > 0 && o.voiced >= 0.6) v.push_back (&o);
      std::stable_sort (v.begin(), v.end(), [] (auto* x, auto* y) { return x->pad > y->pad; });
      double tot = (double) (s.v1 - s.v0) / ssr; for (auto* o : v) { if (tot >= 1.5 || parts.size() >= 5) break; parts.push_back (o); tot += (double) (o->v1 - o->v0) / ssr; } }
    juce::AudioBuffer<float> span (2, 0);
    {
        const int xf = (int) (0.03 * c.sr);
        for (auto* o : parts)
        {
            const int64 v0 = o->v1 > o->v0 ? o->v0 : o->s0, v1 = o->v1 > o->v0 ? o->v1 : o->s1;
            auto piece = slice (c.W(), c.wf (v0), c.wf (v1));
            if (piece.getNumSamples() < 4 * xf) continue;
            if (o != &s && o->midi > 0 && s.midi > 0)
            {   // every piece onto the drone's note (the octave nearest it): one tone, not a smear of speech pitches. NATURAL: partly.
                double d = s.midi - o->midi; d -= 12.0 * std::round (d / 12.0);
                d *= c.musical() ? 1.0 : c.m() * 2.0;
                if (std::abs (d) > 0.05) piece = shift (piece, c.sr, d, true, 0.0, midiToHz (o->midi));
            }
            levelTo (piece, 0, piece.getNumSamples(), -20.0);
            fadeEdges (piece, xf, xf);
            const int at = juce::jmax (0, span.getNumSamples() - xf);
            span.setSize (2, at + piece.getNumSamples(), true, true);
            addInto (span, piece, at);
        }
        if (span.getNumSamples() < (int) (0.2 * c.sr)) span = slice (c.W(), c.wf (s.s0), c.wf (s.s1));
    }
    const double own = s.midi > 0 ? s.midi : 57.0;
    const int root = c.musical() ? snapToScale ((int) std::lround (own), c.key) : (int) std::lround (own);
    t.rootNote = root;
    const double secs = 9.5; const int ls = (int) (2.0 * c.sr), le = (int) (9.2 * c.sr);
    const bool cl = c.clear();
    // build 6: the phase-locked freeze (PhaseLock.h): clear at DRIFT 0, the random-phase smear grows with DRIFT and FREEZE
    // build 9: an edit on the roll re-renders only the preview: the drone zones are the take's before the edit (generate() checked that
    // nothing but the edit changed), so the freeze, the drones and the grains are not made again
    const bool reuseZ = v6 && c.reuse != nullptr && c.reuse->type == Type::Texture && ! c.reuse->zones.empty();
    auto base = reuseZ ? juce::AudioBuffer<float> (1, 1) : ! v6 ? freeze (span, c.sr, 0, span.getNumSamples(), secs, c.sub (7))
                     : [&] { pl::Spec ps; ps.fftOrder = 13; ps.transients = false; ps.seed = c.sub (7); ps.random = juce::jlimit (0.0, 1.0, juce::jmax (c.r.deepOr (Type::Texture, 5), 0.15 + 0.6 * drift * (0.5 + gFrz)));
                             return stereoOf (pl::freeze (span, c.sr, 0, span.getNumSamples(), secs, 1.5 + 5.0 * gFrz, ps)); }();
    if (! reuseZ)
    {
        if (cl) base = monoOf (base);
        levelTo (base, 0, base.getNumSamples(), -20.0);
    }
    c.report (0.55f);
    if (! reuseZ) base = lowpass (base, c.sr, cl ? 14000.0 : 9000.0);   // (a drone is air, not fizz)
    const auto air = grainSources (c, 16, true);
    const bool keepF = c.r.character != Character::Chipmunk; const double fSemis = c.r.character == Character::Deep ? -4.0 : 0.0;
    // CLEAR: the vowel itself, held steady by the voice engine, under the drone (the voice stays a voice)
    Voice vd; double vc0 = 0, vc1 = 0;
    if (cl) { coreOf (c, s, vc0, vc1); if (! reuseZ) vd = voiceOf (c, s, vc0, vc1); }
    const int step = cl ? 1 : 6, zonesN = 24 / step + 1;   // CLEAR: a zone on every semitone
    std::vector<int> offs; std::vector<double> rdv; for (int off = -12; off <= 12; off += step) { offs.push_back (off); rdv.push_back (c.rd.uni()); }
    const int nz = (int) offs.size();
    auto droneFor = [&] (double semis, bool kf, double fs) -> juce::AudioBuffer<float>
    {
        if (! cl) return shift (base, c.sr, semis, kf, fs, midiToHz (own), false);
        // CLEAR: the frozen drone is steady: 5 s of it shifted, laid end to end (half the phase-vocoder work)
        juce::AudioBuffer<float> piece (1, (int) (5.0 * c.sr)); piece.copyFrom (0, 0, base, 0, (int) (1.0 * c.sr), piece.getNumSamples());
        return tileTo (shift (piece, c.sr, semis, kf, fs, midiToHz (own), true), (int) (secs * c.sr), (int) (0.5 * c.sr));
    };
    // the drones first (a NATURAL sub an octave down is the drone of the zone 12 below: made once)
    std::vector<juce::AudioBuffer<float>> drones ((size_t) nz);
    if (! reuseZ) forEachZone (nz, cl, [&] (int zi) { if (! c.cancelled()) drones[(size_t) zi] = droneFor (root + offs[(size_t) zi] - own, keepF, fSemis); });
    if (c.cancelled()) return;
    std::vector<Zone> zs ((size_t) nz); std::atomic<int> done { 0 };
    if (! reuseZ) forEachZone (nz, cl, [&] (int zi)
    {
        if (c.cancelled()) return;
        const int off = offs[(size_t) zi];
        const double zn = root + off, semis = zn - own;
        const auto& drone = drones[(size_t) zi];
        Zone z; z.rootNote = (int) zn; z.audio.setSize (cl ? 1 : 2, (int) (secs * c.sr)); z.audio.clear();
        addInto (z.audio, drone, 0, (cl ? 0.55f : 0.7f) * (v6 ? (float) (0.6 + 0.8 * gFrz) : 1.0f));
        if (c.musical() && off >= -6)
        {
            juce::AudioBuffer<float> sub;
            int below = -1; for (int j = 0; j < nz; ++j) if (offs[(size_t) j] == off - 12) below = j;
            if (cl && keepF && fSemis == 0.0 && below >= 0) sub.makeCopyOf (drones[(size_t) below]);
            else sub = droneFor (semis - 12.0, true, 0.0);
            levelTo (sub, 0, sub.getNumSamples(), -24.0); highpassInPlace (sub, c.sr, 40.0); addInto (z.audio, sub, 0, 0.6f);
        }
        if (cl && vd.ok())
        {
            ToneSpec ts; ts.seconds = secs; ts.semis = zn - hzToMidi (vd.f0); ts.formant = c.formantFor (ts.semis); ts.natural = 0.5 * (1.0 - c.m());
            ts.vibratoCents = 0; ts.driftCents = 0; ts.width = 0; ts.walk = 0.8; ts.onset = false; ts.clean = true; ts.seed = c.sub ((uint32_t) (off + 350));
            auto direct = monoOf (psolaTone (vd, ts));
            levelTo (direct, 0, direct.getNumSamples(), -22.0);
            addInto (z.audio, direct, 0, 0.5f);
        }
        CloudSpec cs; cs.seconds = secs; cs.density = 5.0 + 4.0 * c.m(); cs.grainS = 0.38; cs.grainJitter = 0.5; cs.taper = 1.0; cs.spray = 1.0; cs.headSpeed = 0.5;
        cs.notes = { zn, zn + 7, zn + 12, zn + (c.key.minor ? 15 : 14), zn - 12 }; cs.noteWeights = { 1.0, 0.5 * c.m(), 0.5, 0.25 * c.m(), 0.25 };
        cs.foldOctaves = false; cs.natural = 1.0 - c.m(); cs.scatterCents = 12; cs.spread = 1.0; cs.reverse = 0.3; cs.gainJitterDb = 6.0; cs.fadeS = 0.4; cs.seed = c.sub ((uint32_t) (off + 300));
        if (cl) { cs.grainS = 0.45; cs.grainJitter = 0.2; cs.taper = 0.8; cs.spray = 0.4; cs.headSpeed = 0.25; cs.scatterCents = 3; cs.spread = 0; cs.reverse = 0.1; cs.gainJitterDb = 2.0; cs.clean = true; cs.channels = 1; }
        if (v6)
        {   // DRIFT and the deeper knobs: grain size, density, spray (and the wander that comes with drifting)
            cs.grainS *= 0.5 + gSize; cs.density *= (0.4 + 1.2 * gDens) * (1.0 + 0.5 * drift);
            cs.spray = juce::jlimit (0.05, 1.0, cs.spray * (0.5 + gSpray) + 0.3 * drift); cs.reverse = juce::jlimit (0.0, 0.6, cs.reverse + 0.2 * drift);
            cs.scatterCents += 10.0 * drift; cs.headSpeed *= 0.6 + 0.8 * drift;
            cs.normalise = true; const double gw = c.r.deepOr (Type::Texture, 4); cs.window = gw < 1.0 / 3.0 ? 0 : gw < 2.0 / 3.0 ? 1 : 2;
        }
        const double ph = rdv[(size_t) zi] * 6.28;
        cs.densityAt = [ph] (double tt) { return 0.6 + 0.4 * std::sin (juce::MathConstants<double>::twoPi * tt / 7.1 + ph); };
        const bool fromDrone = v6 && c.r.deepOr (Type::Texture, 7) >= 0.5;   // ROUTE right: the grains made from the frozen drone
        auto g = fromDrone ? grainsOf (monoOf (z.audio), c.sr, secs, cs.density, cs.grainS, cs.spray, cs.window, cs.seed) : cloud (c.W(), c.P.bank, c.sr, air, cs);
        if (v6 && ! fromDrone)
        {   // ROUTE left: the grains go into a freeze of their own (a frozen cloud), clear or blurred with BLUR / DRIFT
            pl::Spec ps; ps.fftOrder = 12; ps.transients = false; ps.seed = c.sub ((uint32_t) (zi + 900)); ps.random = juce::jlimit (0.0, 1.0, c.r.deepOr (Type::Texture, 5) + 0.3 * drift);
            g = pl::freeze (g, c.sr, 0, g.getNumSamples(), secs, 2.0 + 4.0 * gFrz, ps);
        }
        levelTo (g, 0, g.getNumSamples(), -21.0);
        addInto (z.audio, g, 0, cl ? 0.4f : 0.5f);
        if (v6 && c.r.deepOr (Type::Texture, 6) > 0.02 && zn + 12 <= 108)
        {   // SHIMMER: the drone an octave up (formants kept), softly
            auto up = shift (drone, c.sr, 12.0, true, 0.0, midiToHz (zn), true);
            highpassInPlace (up, c.sr, 700.0); levelTo (up, 0, up.getNumSamples(), -21.0);
            addInto (z.audio, up, 0, (float) (0.4 * c.r.deepOr (Type::Texture, 6)));
        }
        z.audio = lowpass (z.audio, c.sr, cl ? 12000.0 : 7500.0);   // (soft on top: breath and hiss stay out of the drone; TONE brightens it)
        swell (z.audio, (int) (1.2 * c.sr));
        levelTo (z.audio, ls, le, -19.0);
        bakeLoop (z.audio, ls, le, (int) (1.6 * c.sr));
        z.loopStart = ls; z.loopEnd = le;
        zs[(size_t) zi] = std::move (z);
        c.report (0.55f + 0.3f * (float) (++done) / (float) zonesN);
    });
    if (c.cancelled()) return;
    if (reuseZ) zs = c.reuse->zones;
    for (auto& z : zs) t.zones.push_back (std::move (z));
    if (v6) t.cache = std::make_shared<int> (2);
    t.attackS = 1.3; t.releaseS = 2.8; t.pairDetune = ! cl; t.ensemble = cl ? 0.0f : 1.0f;
    if (v6) { t.dream = (float) (0.8 * drift); t.releaseS += 1.5 * drift; t.style = Style::Ambient; t.chain = { "VOWELS", "FREEZE", "DRONE", "SUB", "GRAINS", "SPACE" }; }
    // the preview: the tonic drone, the fifth joins, then the octave (about 12 s)
    const int tonic = centreFor (c, root);
    const double spb = 60.0 / t.bpm, bps = 1.0 / spb;
    std::vector<NoteEvent> ev;
    { NoteEvent e; e.note = tonic; e.beat = 0; e.length = 12.0 * bps; e.velocity = 0.72f; ev.push_back (e); }
    { NoteEvent e; e.note = tonic + 7; e.beat = 3.0 * bps; e.length = 9.0 * bps; e.velocity = 0.55f; ev.push_back (e); }
    { NoteEvent e; e.note = tonic + 12; e.beat = 6.0 * bps; e.length = 6.0 * bps; e.velocity = 0.45f; ev.push_back (e); }
    if (! c.musical()) ev[1].note = tonic + (c.rs.chance (0.5) ? 5 : 7);
    if (v6) ev = edit::editLine (ev, c.r.edit, c.key, juce::jmax (1, (int) std::ceil (12.0 * bps / 4.0 - 1e-9)), false);   // (build 8: your notes on the roll; none = as before)
    t.midi = toMidi (ev, t.bpm);
    if (v6) t.notes = ev;
    t.preview = renderEvents (t, ev, 12.0 + t.releaseS + 0.3);
    for (auto* o : parts) t.used.push_back ({ (double) o->s0 / ssr, (double) o->s1 / ssr });
    t.info = "A frozen drone of " + juce::String ((int) parts.size()) + " vowels of your sound, with a cloud of grains from all of it. Hold notes on your keys.";
    finishPreview (c, t);
}
#include "GenerateV6.inl"
} // namespace

bool sameSound (const Recipe& a, const Recipe& b)
{
    auto x = a, y = b; x.edit = {}; y.edit = {}; x.terrain = {}; y.terrain = {}; x.name = y.name = {}; x.favourite = y.favourite = false;
    return juce::JSON::toString (x.toVar(), true) == juce::JSON::toString (y.toVar(), true);
}

bool sameButTerrain (const Recipe& a, const Recipe& b)
{
    auto x = a, y = b; x.terrain = {}; y.terrain = {}; x.name = y.name = {}; x.favourite = y.favourite = false;
    return x.edit == y.edit && juce::JSON::toString (x.toVar(), true) == juce::JSON::toString (y.toVar(), true);
}

std::shared_ptr<Take> generate (const Prepared& P, const Recipe& r, const Progress& p, std::shared_ptr<const Take> reuse)
{
    if (P.src == nullptr || P.work == nullptr) return nullptr;
    juce::ScopedNoDenormals nd;   // (no subnormal tails in the takes: the same flush-to-zero the audio thread has)
    auto t = std::make_shared<Take>();
    t->recipe = r; t->type = resolveType (r); t->bpm = juce::jlimit (20.0, 480.0, r.bpm); t->rate = kRate;
    Ctx c (P, r, p);
    c.key = keyFor (P, r);
    if (reuse != nullptr && reuse->prepTag == &P && reuse->cache != nullptr && r.engine >= 6 && reuse->recipe.engine >= 6 && sameSound (reuse->recipe, r)) c.reuse = reuse;
    if (! r.parts.empty() && P.analysis != nullptr)
    {
        const double ssr = P.src->sampleRate;
        auto ps = r.parts; std::sort (ps.begin(), ps.end());
        for (auto& q : ps)
        {
            const int64 s0 = juce::jlimit<int64> (0, P.src->frames, (int64) std::llround (q.first * ssr)), s1 = juce::jlimit<int64> (s0, P.src->frames, (int64) std::llround (q.second * ssr));
            if (s1 - s0 >= (int64) (0.02 * ssr) && (int) c.manual.size() < Recipe::maxParts (t->type)) c.manual.push_back (segmentAt (*P.src, *P.analysis, s0, s1));
        }
    }
    t->keyTonic = c.key.tonic; t->keyMinor = c.key.minor;
    c.report (0.48f);
    const bool v6 = r.engine >= 6;
    switch (t->type)
    {
        case Type::Pad: if (v6) makePad6 (c, *t); else makePad (c, *t); break;
        case Type::Chops: makeChops (c, *t); break;
        case Type::Texture: makeTexture (c, *t); break;
        default: if (v6) makeInstrument6 (c, *t); else makeInstrument (c, *t); break;
    }
    if (c.cancelled() || t->preview.getNumSamples() == 0) return nullptr;
    if (! c.manual.empty()) { t->used.clear(); for (auto& m : c.manual) t->used.push_back ({ (double) m.s0 / P.src->sampleRate, (double) m.s1 / P.src->sampleRate }); }   // (exactly the user's boxes)
    if (t->recipe.name.isEmpty()) t->recipe.name = takeName (1, t->type, c.key, r.variation);
    t->prepTag = &P;
    c.report (1.0f);
    return t;
}
} // namespace gslice::gen
