#pragma once
// RECIPE <-> JSON. The engines read every table / weight from arp::recipe(); the plugin fills it from the embedded
// data/recipe.json at startup (missing keys keep their built-in defaults, unknown keys are ignored), so a new recipe
// can be dropped in without touching the code.
#include <juce_core/juce_core.h>
#include "Composer.h"
#include <algorithm>

namespace arprecipe
{
inline juce::var intsToVar (const std::vector<int>& v) { juce::Array<juce::var> a; for (int x : v) a.add (x); return a; }
inline juce::var dblsToVar (const std::vector<double>& v) { juce::Array<juce::var> a; for (double x : v) a.add (x); return a; }
inline juce::var strsToVar (const std::vector<std::string>& v) { juce::Array<juce::var> a; for (auto& x : v) a.add (juce::String (x)); return a; }
inline juce::var gridToVar (const std::vector<std::vector<int>>& g) { juce::Array<juce::var> a; for (auto& r : g) a.add (intsToVar (r)); return a; }
inline void readInts (const juce::var& v, std::vector<int>& out) { if (auto* a = v.getArray()) { out.clear(); for (auto& x : *a) out.push_back ((int) x); } }
inline void readDbls (const juce::var& v, std::vector<double>& out) { if (auto* a = v.getArray()) { out.clear(); for (auto& x : *a) out.push_back ((double) x); } }
inline void readStrs (const juce::var& v, std::vector<std::string>& out) { if (auto* a = v.getArray()) { out.clear(); for (auto& x : *a) out.push_back (x.toString().toStdString()); } }
inline void readGrid (const juce::var& v, std::vector<std::vector<int>>& out)
{
    if (auto* a = v.getArray()) { out.clear(); for (auto& r : *a) { std::vector<int> row; readInts (r, row); int sum = 0; for (int x : row) sum += x; if (! row.empty() && sum > 0) out.push_back (row); } }
}
template <typename T> void readNum (const juce::var& o, const char* k, T& dst) { if (o.hasProperty (k)) dst = (T) (double) o[k]; }

inline juce::var toVar (const arp::Recipe& R)
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("schema", "arpezator.recipe.v1");
    juce::Array<juce::var> modes;
    for (int m = 0; m < 3; ++m)
    {
        const auto& M = R.mode[m]; auto* o = new juce::DynamicObject();
        o->setProperty ("label", juce::String (M.label)); o->setProperty ("words", juce::String (M.words)); o->setProperty ("scales", strsToVar (M.scales));
        o->setProperty ("progs", strsToVar (M.progs)); o->setProperty ("progBias", M.progBias);
        o->setProperty ("chordBars", M.chordBars); o->setProperty ("voicing", M.voicing); o->setProperty ("chordRhythm", M.chordRhythm);
        o->setProperty ("phraseBars", M.phraseBars); o->setProperty ("form", M.form); o->setProperty ("staggerTicks", M.staggerTicks); o->setProperty ("stutter", M.stutter);
        o->setProperty ("arpRange", M.arpRange); o->setProperty ("melRange", M.melRange); o->setProperty ("forceArp16", M.forceArp16);
        juce::Array<juce::var> mute; for (bool b : M.mute) mute.add (b); o->setProperty ("mute", mute);
        o->setProperty ("sound", juce::String (M.sound)); o->setProperty ("stabs", M.stabs);
        o->setProperty ("cycleW", dblsToVar (M.cycleW)); o->setProperty ("ruleW", dblsToVar (M.ruleW)); o->setProperty ("groupW", dblsToVar (M.groupW));
        o->setProperty ("stepTarget", M.stepTarget); o->setProperty ("antP", M.antP); o->setProperty ("susP", M.susP);
        modes.add (juce::var (o));
    }
    root->setProperty ("modes", modes);
    root->setProperty ("groupings16", gridToVar (R.groupings16)); root->setProperty ("groupings8", gridToVar (R.groupings8));
    root->setProperty ("groupings24", gridToVar (R.groupings24)); root->setProperty ("sunriseHalves", gridToVar (R.sunriseHalves));
    auto* w = new juce::DynamicObject();
    w->setProperty ("bigLeap", R.wBigLeap); w->setProperty ("octave", R.wOct); w->setProperty ("clash", R.wClash); w->setProperty ("nonChordStrong", R.wWeakStrong);
    w->setProperty ("narrow", R.wNarrow); w->setProperty ("topShare", R.wTopShare); w->setProperty ("repeat", R.wRep); w->setProperty ("step", R.wStep);
    w->setProperty ("direction", R.wDir); w->setProperty ("flatBar", R.wFlatBar); w->setProperty ("loop", R.wLoop); w->setProperty ("static", R.wStatic); w->setProperty ("random", R.wRandom);
    root->setProperty ("taste", juce::var (w));
    root->setProperty ("arpCandidates", R.arpCandidates); root->setProperty ("melodyCandidates", R.melodyCandidates);
    return juce::var (root);
}

// the composer's numerals are relative to the MAJOR scale of the key (i, bVI, bVII, IV, vi, bIIIadd9, bVIIsus2 ...);
// the chord generator's tokens are relative to the mode's own scale (minor: i VI III VII, major: I V vi IV, bVII ...)
inline std::string translateProg (const std::vector<std::string>& toks)
{
    static const int MAJ[7] { 0, 2, 4, 5, 7, 9, 11 }, MIN[7] { 0, 2, 3, 5, 7, 8, 10 };
    static const char* ROM[7] { "I", "II", "III", "IV", "V", "VI", "VII" };
    struct P { int root; bool upper; std::string raw; bool ok; };
    auto parse = [] (const std::string& t) {
        P p { 0, true, t, false }; size_t i = 0; int acc = 0;
        if (i < t.size() && (t[i] == 'b' || t[i] == '#')) { acc = t[i] == 'b' ? -1 : 1; ++i; }
        static const char* NUM[] { "VII", "VI", "IV", "V", "III", "II", "I", "vii", "vi", "iv", "v", "iii", "ii", "i" };
        for (auto* n : NUM) { const std::string s (n); if (t.compare (i, s.size(), s) == 0) {
            std::string up = s; for (auto& ch : up) ch = (char) std::toupper ((unsigned char) ch);
            int d = 0; for (int k = 0; k < 7; ++k) if (up == ROM[k]) d = k;
            p.root = ((MAJ[d] + acc) % 12 + 12) % 12; p.upper = s == up; p.ok = true; break; } }
        return p; };
    std::vector<P> ps; for (auto& t : toks) ps.push_back (parse (t));
    const bool minorFam = ! ps.empty() && ps[0].ok && ps[0].root == 0 && ! ps[0].upper;
    std::string out;
    for (size_t k = 0; k < ps.size(); ++k)
    {
        const P& p = ps[k]; std::string tok;
        const int* S = minorFam ? MIN : MAJ; int d = -1; for (int i = 0; i < 7; ++i) if (S[i] == p.root) d = i;
        if (d >= 0) { tok = ROM[d]; if (! p.upper) for (auto& ch : tok) ch = (char) std::tolower ((unsigned char) ch); }
        else if (minorFam && p.root == 1) tok = "bII";
        else { int d2 = -1; for (int i = 0; i < 7; ++i) if (MAJ[i] == (p.root + 1) % 12) d2 = i; tok = d2 >= 0 ? std::string ("b") + ROM[d2] : p.raw; if (d2 >= 0 && ! p.upper) for (size_t c = 1; c < tok.size(); ++c) tok[c] = (char) std::tolower ((unsigned char) tok[c]); }
        out += (k ? " " : "") + tok;
    }
    return out;
}
inline std::string firstWord (const juce::String& s) { return s.upToFirstOccurrenceOf (" ", false, false).trim().toStdString(); }

// the composer's recipe (recipe.json from the composer study: "modes": { "MIDNIGHT": ..., "SUNRISE": ..., "DUSK": ... })
inline bool fromComposerVar (const juce::var& v, arp::Recipe& R)
{
    const auto modes = v["modes"];
    if (! modes.isObject()) return false;
    const char* names[] { "SUNRISE", "DUSK", "MIDNIGHT" };
    for (int m = 0; m < 3; ++m)
    {
        const auto o = modes[names[m]]; if (! o.isObject()) continue;
        auto& M = R.mode[m]; const auto d = o["defaults"];
        if (auto* sc = d["scales"].getDynamicObject())
        { std::vector<std::string> s; for (auto& p : sc->getProperties()) if ((double) p.value > 0) { auto w = firstWord (p.name.toString()); if (std::find (s.begin(), s.end(), w) == s.end()) s.push_back (w); } if (! s.empty()) M.scales = s; }
        if (auto* hr = d["harmonic_rhythm_bars"].getDynamicObject()) { double bw = -1; for (auto& p : hr->getProperties()) if ((double) p.value > bw) { bw = p.value; M.chordBars = juce::jlimit (1, 2, p.name.toString().getIntValue()); } }
        if (auto* fl = d["figure_len_weights"].getDynamicObject()) { std::vector<double> w (6, 0.0); for (auto& p : fl->getProperties()) { const int k = p.name.toString().getIntValue(); if (k >= 3 && k <= 8) w[(size_t) (k - 3)] = p.value; } M.cycleW = w; }
        if (auto* pd = d["pedal"]["degree_weights"].getDynamicObject()) { M.pedalDeg.clear(); for (auto& p : pd->getProperties()) M.pedalDeg.push_back ({ firstWord (p.name.toString()), (double) p.value }); }
        if (auto* g = d["gate"].getArray(); g != nullptr && g->size() >= 2) { M.gateLo = g->getReference (0); M.gateHi = g->getReference (1); }
        if (d["stutter"].isObject() && d["stutter"].hasProperty ("max_per_phrase")) M.maxStutterPerPhrase = (int) d["stutter"]["max_per_phrase"];
        if (d["tie_across_barline"].isObject()) M.tieInP = (double) d["tie_across_barline"]["per_phrase_probability"];
        if (auto* rs = d["range_semitones"].getArray(); rs != nullptr && rs->size() >= 2) { M.rangeLo = rs->getReference (0); M.rangeHi = rs->getReference (1); }
        if (auto* st = d["stagger"].getDynamicObject()) for (auto& p : st->getProperties()) if (p.name.toString() != "0") M.staggerP = p.value;
        auto readForms = [&M] (const juce::var& f) { if (auto* fo = f.getDynamicObject()) { M.formW.clear(); for (auto& p : fo->getProperties()) { const auto s = p.name.toString().removeCharacters (" "); const int idx = s == "AAAB" ? 0 : s == "ABAB" ? 1 : s == "ABAC" ? 2 : s == "AAAA" ? 3 : -1; if (idx >= 0) M.formW.push_back ({ idx, (double) p.value }); } } };
        readForms (o["forms"]); if (d.hasProperty ("form")) readForms (d["form"]);
        if (o["evolve"].isObject())
        {
            if (auto* ops = o["evolve"]["ops"].getDynamicObject()) { M.evolveOps.clear(); for (auto& p : ops->getProperties()) M.evolveOps.push_back ({ p.name.toString().toStdString(), (double) p.value }); }
            if (o["evolve"]["lift"].isObject()) M.liftP = (double) o["evolve"]["lift"]["probability"];
        }
        if (auto* figs = o["figures"].getArray())
        {
            M.figures.clear();
            for (auto& f : *figs)
            {
                arp::ModeRecipe::Fig F; readStrs (f["tokens"], F.tokens); F.resync = (int) f["resync_bars"]; F.pedal = f["pedal_degree"].toString().toStdString(); F.weight = f["weight"];
                if (auto* ev = f["evolve"].getArray()) for (auto& e : *ev) { arp::ModeRecipe::Event E; E.fromBar = (int) e["from_bar"]; readStrs (e["pattern"], E.pattern); E.pedal = e["pedal"].toString().toStdString(); F.evolve.push_back (E); }
                if (! F.tokens.empty() && F.tokens.size() <= 8) M.figures.push_back (F);
            }
        }
        auto readProgs = [&M] (const juce::var& a, double scale) { if (auto* pr = a.getArray()) for (auto& p : *pr) { std::vector<std::string> t; readStrs (p["prog"], t); if (t.empty()) continue; M.progs.push_back (translateProg (t)); M.progW.push_back ((double) p["weight"] * scale); } };
        if (o["progressions"].isArray()) { M.progs.clear(); M.progW.clear(); readProgs (o["progressions"], 1.0); readProgs (o["progressions_also_ok"], 0.3); M.progBias = std::max (M.progBias, 0.6); }
        auto readCells = [] (const juce::var& a, std::vector<std::pair<std::string, double>>& out) { if (auto* cs = a.getArray()) { out.clear(); for (auto& c : *cs) out.push_back ({ c["cell"].toString().toStdString(), (double) c["weight"] }); } };
        readCells (o["rhythm_cells_beat"], M.cellsBeat); readCells (o["rhythm_cells_two_beat"], M.cellsTwo);
        if (auto* os = o["motif_openings_scale_steps"].getArray()) { M.openSteps.clear(); for (auto& e : *os) { std::vector<int> iv; readInts (e["iv"], iv); M.openSteps.push_back ({ iv, (double) e["weight"] }); } }
        if (auto* od = o["motif_opening_degrees"].getArray()) { M.openDeg.clear(); for (auto& e : *od) { std::vector<int> dg; readInts (e["deg"], dg); M.openDeg.push_back ({ dg, (double) e["weight"] }); } }
        if (auto* cw = o["contour_weights"].getDynamicObject()) { M.contour[0] = cw->getProperty ("arch"); M.contour[1] = cw->getProperty ("descending"); M.contour[2] = cw->getProperty ("ascending"); M.contour[3] = cw->getProperty ("level"); }
        if (auto* tg = o["targets"].getDynamicObject()) { M.targets.clear(); for (auto& p : tg->getProperties()) { arp::ModeRecipe::Target T; T.lo = p.value["lo"]; T.hi = p.value["hi"]; T.w = p.value["weight"]; M.targets.push_back ({ p.name.toString().toStdString(), T }); } }
    }
    // DUSK shares SUNRISE's cells / openings / contour where it has none of its own
    auto& S = R.mode[arp::Sunrise]; auto& D = R.mode[arp::Dusk];
    if (D.cellsTwo.empty()) D.cellsTwo = S.cellsTwo;
    if (D.openSteps.empty()) D.openSteps = S.openSteps;
    if (D.openDeg.empty()) D.openDeg = S.openDeg;
    if (D.formW.empty()) D.formW = { { 1, 0.8 }, { 2, 0.2 } };
    for (int k = 0; k < 4; ++k) if (D.contour[k] <= 0.0) D.contour[k] = S.contour[k];
    return true;
}

inline bool fromVar (const juce::var& v, arp::Recipe& R)
{
    if (! v.isObject()) return false;
    if (v["modes"].isObject()) return fromComposerVar (v, R);
    if (auto* modes = v["modes"].getArray())
        for (int m = 0; m < 3 && m < modes->size(); ++m)
        {
            const auto& o = modes->getReference (m); auto& M = R.mode[m];
            if (o.hasProperty ("label")) M.label = o["label"].toString().toStdString();
            if (o.hasProperty ("words")) M.words = o["words"].toString().toStdString();
            readStrs (o["scales"], M.scales); readStrs (o["progs"], M.progs); readNum (o, "progBias", M.progBias);
            readNum (o, "chordBars", M.chordBars); readNum (o, "voicing", M.voicing); readNum (o, "chordRhythm", M.chordRhythm);
            readNum (o, "phraseBars", M.phraseBars); readNum (o, "form", M.form); readNum (o, "staggerTicks", M.staggerTicks); readNum (o, "stutter", M.stutter);
            readNum (o, "arpRange", M.arpRange); readNum (o, "melRange", M.melRange);
            if (o.hasProperty ("forceArp16")) M.forceArp16 = (bool) o["forceArp16"];
            if (auto* mu = o["mute"].getArray()) for (int i = 0; i < 4 && i < mu->size(); ++i) M.mute[i] = (bool) mu->getReference (i);
            if (o.hasProperty ("sound")) M.sound = o["sound"].toString().toStdString();
            if (o.hasProperty ("stabs")) M.stabs = (bool) o["stabs"];
            readDbls (o["cycleW"], M.cycleW); readDbls (o["ruleW"], M.ruleW); readDbls (o["groupW"], M.groupW);
            readNum (o, "stepTarget", M.stepTarget); readNum (o, "antP", M.antP); readNum (o, "susP", M.susP);
            M.chordBars = juce::jlimit (1, 2, M.chordBars); M.voicing = juce::jlimit (0, 3, M.voicing); M.chordRhythm = juce::jlimit (0, 2, M.chordRhythm);
            M.form = juce::jlimit (0, 3, M.form); M.arpRange = juce::jlimit (1, 3, M.arpRange); M.melRange = juce::jlimit (1, 3, M.melRange);
        }
    readGrid (v["groupings16"], R.groupings16); readGrid (v["groupings8"], R.groupings8); readGrid (v["groupings24"], R.groupings24); readGrid (v["sunriseHalves"], R.sunriseHalves);
    const auto w = v["taste"];
    if (w.isObject())
    {
        readNum (w, "bigLeap", R.wBigLeap); readNum (w, "octave", R.wOct); readNum (w, "clash", R.wClash); readNum (w, "nonChordStrong", R.wWeakStrong);
        readNum (w, "narrow", R.wNarrow); readNum (w, "topShare", R.wTopShare); readNum (w, "repeat", R.wRep); readNum (w, "step", R.wStep);
        readNum (w, "direction", R.wDir); readNum (w, "flatBar", R.wFlatBar); readNum (w, "loop", R.wLoop); readNum (w, "static", R.wStatic); readNum (w, "random", R.wRandom);
    }
    readNum (v, "arpCandidates", R.arpCandidates); readNum (v, "melodyCandidates", R.melodyCandidates);
    R.arpCandidates = juce::jlimit (1, 400, R.arpCandidates); R.melodyCandidates = juce::jlimit (1, 400, R.melodyCandidates);
    return true;
}
} // namespace arprecipe
