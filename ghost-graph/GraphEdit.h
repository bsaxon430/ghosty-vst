// Ghost Graph -- dragging notes on the graph rewrites the scene's math.
//
// A note is one unbroken piece of one voice's line. A drag can move it (in time and pitch), stretch or shorten it
// from either end, and tilt either end up or down. The voice's entry in y = [...] is then replaced by a new line:
//
//   edit1 = {A < x < B: at(E, ...) + pitch, X0 < x < X1: none, E}      # note: X0 X1 | A B | D0 D1 | E
//
// E is what the voice was. Between A and B it plays the note, time-mapped from where it was (X0..X1) and lifted by
// D0 at its start to D1 at its end; where the note used to be is now silent ("none"); everywhere else it is E.
// Dragging the same note again updates edit1 instead of adding another line (the "# note:" comment remembers it).
#pragma once
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <sstream>
#include "GraphExpr.h"

namespace gg {

struct NoteEdit { std::string E; double X0 = 0, X1 = 1, A = 0, B = 1, D0 = 0, D1 = 0; };

// numbers as people write them: 2.5, -0.125, 7/12
inline std::string fmtNum (double v)
{
    if (std::abs (v) < 1e-9) return "0";
    for (int q : { 1, 2, 4, 8, 16, 32, 64 })
        if (std::abs (v * q - std::round (v * q)) < 1e-7)
        {
            char b[48];
            if (q == 1) std::snprintf (b, sizeof b, "%lld", (long long) std::llround (v));
            else std::snprintf (b, sizeof b, "%.6g", v);
            return b;
        }
    char b[48]; std::snprintf (b, sizeof b, "%.5g", v);
    return b;
}
inline std::string fmtPitch (double d)   // in twelfths when it is whole semitones: 7/12, -1/12, 1
{
    const double s = d * 12.0;
    if (std::abs (s - std::round (s)) < 1e-6)
    {
        const long long k = std::llround (s);
        if (k % 12 == 0) return std::to_string (k / 12);
        return std::to_string (k) + "/12";
    }
    return fmtNum (d);
}
inline std::string plusTerm (const std::string& t)   // " + t" or " - |t|"
{
    if (! t.empty() && t[0] == '-') return " - " + t.substr (1);
    return " + " + t;
}

inline std::string formatEdit (const std::string& name, const NoteEdit& n)
{
    const double L = n.X1 - n.X0, dt = n.B - n.A;
    std::string tmap;
    if (std::abs (dt - L) < 1e-9)
    {
        const double S = n.A - n.X0;
        tmap = std::abs (S) < 1e-9 ? "x" : ("x" + plusTerm (fmtNum (-S)));
    }
    else
        tmap = fmtNum (n.X0) == "0" ? (fmtNum (L) + "(x" + plusTerm (fmtNum (-n.A)) + ")/" + fmtNum (dt))
                                    : (fmtNum (n.X0) + " + " + fmtNum (L) + "(x" + plusTerm (fmtNum (-n.A)) + ")/" + fmtNum (dt));
    std::string body = std::abs (dt - L) < 1e-9 && std::abs (n.A - n.X0) < 1e-9 ? n.E : ("at(" + n.E + ", " + tmap + ")");
    if (std::abs (n.D0 - n.D1) < 1e-9)
    {
        if (std::abs (n.D0) > 1e-9) body += plusTerm (fmtPitch (n.D0));
    }
    else
    {
        if (std::abs (n.D0) > 1e-9) body += plusTerm (fmtPitch (n.D0));
        const double slope = (n.D1 - n.D0) / dt;   // per beat
        // (D1 - D0)(x - A)/dt, written in twelfths when the tilt is whole semitones
        const double semis = (n.D1 - n.D0) * 12.0;
        const std::string xa = "(x" + plusTerm (fmtNum (-n.A)) + ")";
        if (std::abs (semis - std::round (semis)) < 1e-6)
        {
            const long long k = std::llround (semis);
            const std::string den = fmtNum (12.0 * dt);
            const std::string kk = std::to_string (std::llabs (k));
            body += std::string (k < 0 ? " - " : " + ") + (kk == "1" ? "" : kk) + xa + "/" + den;
        }
        else body += plusTerm (fmtNum (slope)) + xa;
    }
    std::string line = name + " = {" + fmtNum (n.A) + " < x < " + fmtNum (n.B) + ": " + body;
    if (n.X1 > n.X0 + 1e-9 && ! (n.A <= n.X0 + 1e-9 && n.B >= n.X1 - 1e-9))   // the old place goes quiet (unless the note still covers it)
        line += ", " + fmtNum (n.X0) + " < x < " + fmtNum (n.X1) + ": none";
    line += ", " + n.E + "}";
    char meta[160];
    std::snprintf (meta, sizeof meta, "   # note: %.9g %.9g | %.9g %.9g | %.9g %.9g | ", n.X0, n.X1, n.A, n.B, n.D0, n.D1);
    return line + meta + n.E;
}

// read back an edit line we wrote; false if it isn't one (or was changed by hand so the comment can't be trusted)
inline bool parseEdit (const std::string& line, std::string& name, NoteEdit& n)
{
    const size_t k = line.find ("# note:");
    const size_t eq = line.find ('=');
    if (k == std::string::npos || eq == std::string::npos || eq > k) return false;
    name = line.substr (0, eq);
    while (! name.empty() && name.back() == ' ') name.pop_back();
    while (! name.empty() && name.front() == ' ') name.erase (0, 1);
    const size_t bar = line.rfind ('|');
    if (bar == std::string::npos || bar < k) return false;
    std::string nums = line.substr (k + 7, bar - (k + 7));
    for (auto& c : nums) if (c == '|') c = ' ';
    std::istringstream is (nums);
    if (! (is >> n.X0 >> n.X1 >> n.A >> n.B >> n.D0 >> n.D1)) return false;
    n.E = line.substr (bar + 1);
    while (! n.E.empty() && n.E.front() == ' ') n.E.erase (0, 1);
    while (! n.E.empty() && (n.E.back() == ' ' || n.E.back() == '\r')) n.E.pop_back();
    return ! n.E.empty() && ! name.empty();
}

struct EditPlan { std::string name; NoteEdit start; bool reuse = false; };

inline std::vector<std::string> splitLines (const std::string& t)
{
    std::vector<std::string> v; size_t a = 0;
    while (true) { const size_t b = t.find ('\n', a); v.push_back (t.substr (a, b == std::string::npos ? std::string::npos : b - a)); if (b == std::string::npos) break; a = b + 1; }
    return v;
}
inline std::string joinLines (const std::vector<std::string>& v)
{
    std::string s; for (size_t i = 0; i < v.size(); ++i) { if (i) s += '\n'; s += v[i]; } return s;
}
inline bool isYLine (const std::string& l)
{
    size_t i = 0; while (i < l.size() && l[i] == ' ') ++i;
    if (i >= l.size() || l[i] != 'y') return false;
    ++i; while (i < l.size() && l[i] == ' ') ++i;
    return i < l.size() && l[i] == '=';
}

// Start a drag on note [X0, X1] of voice v: reuse the voice's edit line when this is the note it already moved.
inline EditPlan planEdit (const std::string& text, int v, double X0, double X1)
{
    EditPlan plan;
    auto p = Program::compile (text);
    if (! p->ok() || v < 0 || v >= p->numVoices()) return plan;
    const std::string src = p->voiceSource (v);
    for (auto& l : splitLines (text))
    {
        std::string nm; NoteEdit ne;
        if (parseEdit (l, nm, ne) && nm == src && X0 >= ne.A - 0.02 && X1 <= ne.B + 0.02)
        { plan.name = nm; plan.start = ne; plan.reuse = true; return plan; }
    }
    // a new edit line: the first free editK
    for (int k = 1; k < 1000; ++k)
    {
        const std::string nm = "edit" + std::to_string (k);
        bool used = false;
        for (auto& d : p->getDefs()) if (d.name == nm) used = true;
        if (text.find (nm + " =") != std::string::npos) used = true;
        if (! used) { plan.name = nm; break; }
    }
    plan.start.E = src;
    plan.start.X0 = plan.start.A = X0;
    plan.start.X1 = plan.start.B = X1;
    return plan;
}

// The scene's text with the edit applied.
inline std::string applyEdit (const std::string& text, int v, const EditPlan& plan, const NoteEdit& n)
{
    auto p = Program::compile (text);
    if (! p->ok() || v < 0 || v >= p->numVoices() || plan.name.empty()) return text;
    auto lines = splitLines (text);
    const std::string def = formatEdit (plan.name, n);
    if (plan.reuse)
    {
        for (auto& l : lines)
        {
            std::string nm; NoteEdit ne;
            if (parseEdit (l, nm, ne) && nm == plan.name) { l = def; return joinLines (lines); }
        }
        return text;
    }
    // the y list, with this voice renamed
    std::string ys = p->isOutline() ? "y = outline(" : "y = [";
    for (int i = 0; i < p->numVoices(); ++i) ys += (i ? ", " : "") + (i == v ? plan.name : p->voiceSource (i));
    ys += p->isOutline() ? ")" : "]";
    std::vector<std::string> out;
    bool placed = false;
    for (auto& l : lines)
    {
        if (isYLine (l)) { if (! placed) { out.push_back (ys); placed = true; } continue; }
        out.push_back (l);
    }
    while (! out.empty() && out.back().empty()) out.pop_back();
    out.push_back (def);
    return joinLines (out) + "\n";
}

} // namespace gg
