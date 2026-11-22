// Ghost Graph -- dragging, copying and deleting notes on the graph rewrites the voice's own formula, in place.
//
// The voice's formula (its own line, kick = ..., or its entry in y = [...] when it has no line of its own) becomes
// a piece {...} whose first pieces say what changed:
//
//   kick = {1 < x < 1.6: at(1.5e^(-14h) - 2.1, x - 1) + 7/12, 0 < x < 0.6: none, h < 0.3: 1.5e^(-14h) - 2.1}
//
// "1 < x < 1.6: ..." plays the note there (at(f, t) is f at the time t: what it played where it came from), "0 < x <
// 0.6: none" makes its old place quiet, and the rest is the formula as it was. Dragging that note again just changes
// its piece. A pasted copy is the same without the "none". A note on a held line (y = [-1]) reads {-6 < x < 10: -5/12,
// -8 < x < 8: none, -1}.
#pragma once
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <cctype>
#include <algorithm>
#include "GraphExpr.h"

namespace gg {

// A note: what it plays (V, as written at the times X0..X1), and where it plays now (A..B), lifted by D0 at its
// start to D1 at its end.
struct NoteEdit { std::string V; double X0 = 0, X1 = 1, A = 0, B = 1, D0 = 0, D1 = 0; };

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

// ---- text helpers
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
inline std::string withoutComment (const std::string& l)
{
    size_t k = l.find ('#');
    const size_t k2 = l.find ("//");
    if (k2 != std::string::npos && (k == std::string::npos || k2 < k)) k = k2;
    return k == std::string::npos ? l : l.substr (0, k);
}
inline bool isNameChar (char c) { return std::isalnum ((unsigned char) c) || c == '_'; }
inline int countWord (const std::string& s, const std::string& w)
{
    int n = 0;
    for (size_t p = s.find (w); p != std::string::npos; p = s.find (w, p + 1))
        if ((p == 0 || ! isNameChar (s[p - 1])) && (p + w.size() >= s.size() || ! isNameChar (s[p + w.size()]))) ++n;
    return n;
}
inline std::string trimStr (std::string t)
{
    while (! t.empty() && (t.front() == ' ' || t.front() == '\t')) t.erase (0, 1);
    while (! t.empty() && (t.back() == ' ' || t.back() == '\t' || t.back() == '\r')) t.pop_back();
    return t;
}
inline bool isIdentifier (const std::string& s)
{
    if (s.empty() || ! (std::isalpha ((unsigned char) s[0]) || s[0] == '_')) return false;
    for (char c : s) if (! isNameChar (c)) return false;
    return true;
}
inline bool isConstantText (const std::string& s)   // a plain number: -1, 7/12, 0.5
{
    for (char c : s) if (std::isalpha ((unsigned char) c)) return false;
    return ! s.empty();
}
// the value of a small formula of x alone (NaN if it isn't one)
inline double evalAt (const std::string& f, double x)
{
    auto p = Program::compile ("y = [" + f + "]");
    if (! p->ok() || p->numVoices() != 1) return std::numeric_limits<double>::quiet_NaN();
    p->setX (x);
    return p->voice (0);
}

// ---- byte ranges in the scene text
struct Span { size_t b = 0, e = 0; bool ok() const { return e > b; } };

inline Span trimSpan (const std::string& t, Span s)
{
    while (s.b < s.e && (t[s.b] == ' ' || t[s.b] == '\t')) ++s.b;
    while (s.e > s.b && (t[s.e - 1] == ' ' || t[s.e - 1] == '\t' || t[s.e - 1] == '\r')) --s.e;
    return s;
}
inline std::string spanText (const std::string& t, Span s) { return s.ok() ? t.substr (s.b, s.e - s.b) : std::string(); }
// line L (counting from 1), without its comment
inline Span lineSpan (const std::string& t, int L)
{
    size_t a = 0;
    for (int i = 1; i < L; ++i) { const size_t nl = t.find ('\n', a); if (nl == std::string::npos) return {}; a = nl + 1; }
    size_t e = t.find ('\n', a); if (e == std::string::npos) e = t.size();
    return trimSpan (t, { a, a + withoutComment (t.substr (a, e - a)).size() });
}
// a definition line's right-hand side
inline Span rhsSpan (const std::string& t, Span line)
{
    if (! line.ok()) return {};
    const size_t eq = t.find ('=', line.b);
    if (eq == std::string::npos || eq >= line.e) return {};
    return trimSpan (t, { eq + 1, line.e });
}
// the top-level pieces of a bracketed list starting at open, split at commas (empty if it never closes before end)
inline std::vector<Span> topLevelItems (const std::string& t, size_t open, size_t end, size_t* close = nullptr)
{
    std::vector<Span> items;
    int depth = 0; size_t start = open + 1;
    for (size_t i = open; i < end; ++i)
    {
        const char c = t[i];
        if (c == '(' || c == '[' || c == '{') ++depth;
        else if (c == ')' || c == ']' || c == '}')
        {
            if (--depth == 0) { items.push_back (trimSpan (t, { start, i })); if (close) *close = i; return items; }
        }
        else if (c == ',' && depth == 1) { items.push_back (trimSpan (t, { start, i })); start = i + 1; }
    }
    return {};
}
// a formula that is one whole piece {a: b, c: d, e}: its items (else empty)
inline std::vector<Span> pieceItems (const std::string& t, Span f)
{
    if (! f.ok() || t[f.b] != '{') return {};
    size_t close = 0;
    auto items = topLevelItems (t, f.b, f.e, &close);
    if (items.empty() || trimSpan (t, { close + 1, f.e }).ok()) return {};
    return items;
}
// in a definition line, the piece number branch of its right-hand side
inline Span branchSpan (const std::string& t, Span line, int branch)
{
    const auto items = pieceItems (t, rhsSpan (t, line));
    return branch >= 0 && branch < (int) items.size() ? items[(size_t) branch] : Span();
}
// voice v's entry in the y list(s)
inline Span voiceEntrySpan (const std::string& t, int v)
{
    int seen = 0, lineNo = 0;
    size_t a = 0;
    while (a <= t.size())
    {
        size_t e = t.find ('\n', a); if (e == std::string::npos) e = t.size();
        ++lineNo;
        if (isYLine (t.substr (a, e - a)))
        {
            const Span r = rhsSpan (t, lineSpan (t, lineNo));
            std::vector<Span> items;
            if (r.ok() && t.compare (r.b, 7, "outline") == 0) { const size_t o = t.find ('(', r.b); if (o != std::string::npos && o < r.e) items = topLevelItems (t, o, r.e); }
            else if (r.ok() && t[r.b] == '[') items = topLevelItems (t, r.b, r.e);
            else if (r.ok()) items.push_back (r);
            for (auto& it : items) { if (seen == v) return it; ++seen; }
        }
        if (e >= t.size()) break;
        a = e + 1;
    }
    return {};
}
inline std::string replaceSpan (const std::string& t, Span s, const std::string& with) { return t.substr (0, s.b) + with + t.substr (s.e); }

// ---- where a voice's formula is written
struct Host
{
    bool ok = false;
    int def = -1;                      // its own line (the definition's index), or -1: its entry in y = [...]
    Span span;                         // the formula's text
    std::vector<std::string> items;    // the pieces of {...}; a formula that isn't a piece is one "otherwise" item
    bool piece = false;
};
inline Host findHost (const std::string& text, const Program& p, int v)
{
    Host h;
    if (! p.ok() || v < 0 || v >= p.numVoices()) return h;
    const Span entry = voiceEntrySpan (text, v);
    if (! entry.ok()) return h;
    const std::string name = spanText (text, entry);
    h.span = entry;
    if (isIdentifier (name))
    {
        int uses = 0;   // its own line is the voice's formula only when nothing else uses it
        for (auto& l : splitLines (text))
        {
            const std::string m = withoutComment (l);
            if (isYLine (m)) { uses += countWord (m.substr (m.find ('=') + 1), name); continue; }
            const size_t eq = m.find ('=');
            if (eq != std::string::npos && trimStr (m.substr (0, eq)) != name) uses += countWord (m.substr (eq + 1), name);
        }
        for (size_t d = 0; d < p.getDefs().size(); ++d)
            if (p.getDefs()[d].name == name && p.getDefs()[d].params.empty() && uses == 1)
            {
                const Span r = rhsSpan (text, lineSpan (text, p.getDefs()[d].line));
                if (r.ok()) { h.def = (int) d; h.span = r; }
            }
    }
    for (auto& it : pieceItems (text, h.span)) h.items.push_back (spanText (text, it));
    h.piece = ! h.items.empty();
    if (! h.piece) h.items.push_back (spanText (text, h.span));
    h.ok = true;
    return h;
}
inline std::string hostText (const Host& h)
{
    if (h.items.size() == 1 && h.items[0].find (':') == std::string::npos && h.items[0].find ('{') != 0 && ! h.piece) return h.items[0];
    std::string s = "{";
    for (size_t i = 0; i < h.items.size(); ++i) s += (i ? ", " : "") + h.items[i];
    return s + "}";
}
// "cond: value" split at its top-level colon (no colon: value only)
inline bool splitBranch (const std::string& item, std::string& cond, std::string& value)
{
    int depth = 0;
    for (size_t i = 0; i < item.size(); ++i)
    {
        const char c = item[i];
        if (c == '(' || c == '[' || c == '{') ++depth;
        else if (c == ')' || c == ']' || c == '}') --depth;
        else if (c == ':' && depth == 0) { cond = trimStr (item.substr (0, i)); value = trimStr (item.substr (i + 1)); return true; }
    }
    cond.clear(); value = trimStr (item);
    return false;
}
// "A < x < B" (A, B numbers)
inline bool xRange (const std::string& cond, double& A, double& B)
{
    const size_t l1 = cond.find ('<'); if (l1 == std::string::npos) return false;
    const size_t l2 = cond.find ('<', l1 + 1); if (l2 == std::string::npos || cond.find ('<', l2 + 1) != std::string::npos) return false;
    if (cond.find ('=') != std::string::npos) return false;
    const std::string a = trimStr (cond.substr (0, l1)), m = trimStr (cond.substr (l1 + 1, l2 - l1 - 1)), b = trimStr (cond.substr (l2 + 1));
    if (m != "x" || ! isConstantText (a) || ! isConstantText (b)) return false;
    A = evalAt (a, 0); B = evalAt (b, 0);
    return std::isfinite (A) && std::isfinite (B) && B >= A;
}
// a piece that plays one note over A < x < B: read what it plays back into a NoteEdit
inline bool readNote (const std::string& item, NoteEdit& n)
{
    std::string cond, value;
    if (! splitBranch (item, cond, value) || ! xRange (cond, n.A, n.B) || value == "none") return false;
    if (value.compare (0, 3, "at(") == 0)
    {
        int depth = 0; size_t close = std::string::npos;
        for (size_t i = 2; i < value.size(); ++i)
        {
            if (value[i] == '(' || value[i] == '[' || value[i] == '{') ++depth;
            else if (value[i] == ')' || value[i] == ']' || value[i] == '}') { if (--depth == 0) { close = i; break; } }
        }
        if (close == std::string::npos) return false;
        std::vector<Span> parts = topLevelItems (value, 2, close + 1);
        if (parts.size() != 2) return false;
        const std::string rest = trimStr (value.substr (close + 1));
        if (! rest.empty() && rest[0] != '+' && rest[0] != '-') return false;
        n.V = spanText (value, parts[0]);
        const std::string T = spanText (value, parts[1]);
        n.X0 = evalAt (T, n.A); n.X1 = evalAt (T, n.B);
        n.D0 = rest.empty() ? 0.0 : evalAt ("0 " + rest, n.A);
        n.D1 = rest.empty() ? 0.0 : evalAt ("0 " + rest, n.B);
        return ! n.V.empty() && std::isfinite (n.X0) && std::isfinite (n.X1) && std::isfinite (n.D0) && std::isfinite (n.D1) && n.X1 >= n.X0;
    }
    n.V = value; n.X0 = n.A; n.X1 = n.B; n.D0 = n.D1 = 0.0;   // written straight (a held note, or one typed in)
    return true;
}

// the piece that plays note n
inline std::string noteBranch (const NoteEdit& n)
{
    const double L = n.X1 - n.X0, dt = n.B - n.A;
    const bool sameTime = std::abs (dt - L) < 1e-9 && std::abs (n.A - n.X0) < 1e-9;
    const bool constant = isConstantText (n.V);
    std::string body;
    double D0 = n.D0, D1 = n.D1;
    if (constant)   // a held pitch: just the new pitch
    {
        const double c = evalAt (n.V, 0);
        if (std::isfinite (c)) { body = fmtPitch (c + D0); D1 -= D0; D0 = 0; }
        else body = n.V;
    }
    else if (sameTime) body = n.V;
    else
    {
        std::string tmap;
        if (std::abs (dt - L) < 1e-9) tmap = "x" + plusTerm (fmtNum (n.X0 - n.A));
        else tmap = fmtNum (n.X0) == "0" ? (fmtNum (L) + "(x" + plusTerm (fmtNum (-n.A)) + ")/" + fmtNum (dt))
                                         : (fmtNum (n.X0) + " + " + fmtNum (L) + "(x" + plusTerm (fmtNum (-n.A)) + ")/" + fmtNum (dt));
        body = "at(" + n.V + ", " + tmap + ")";
    }
    if (std::abs (D0) > 1e-9) body += plusTerm (fmtPitch (D0));
    if (std::abs (D1 - D0) > 1e-9 && dt > 1e-9)   // a tilt: (D1 - D0)(x - A)/dt, in twelfths when whole semitones
    {
        const double semis = (D1 - D0) * 12.0;
        const std::string xa = "(x" + plusTerm (fmtNum (-n.A)) + ")";
        if (std::abs (semis - std::round (semis)) < 1e-6)
        {
            const long long k = std::llround (semis);
            const std::string kk = std::to_string (std::llabs (k));
            body += std::string (k < 0 ? " - " : " + ") + (kk == "1" ? "" : kk) + xa + "/" + fmtNum (12.0 * dt);
        }
        else body += plusTerm (fmtNum ((D1 - D0) / dt)) + xa;
    }
    return fmtNum (n.A) + " < x < " + fmtNum (n.B) + ": " + body;
}
inline std::string noneBranch (double X0, double X1) { return fmtNum (X0) + " < x < " + fmtNum (X1) + ": none"; }

// ---- a note picked on the graph
struct NotePlan
{
    bool ok = false;
    Host host;
    int branch = -1;     // the piece of the formula that plays it
    bool own = false;    // that piece is the note's own (A < x < B: ...): changing the note changes the piece
    NoteEdit start;      // the note as it is
};
inline NotePlan planNote (const std::string& text, int v, double X0, double X1)
{
    NotePlan plan;
    auto p = Program::compile (text);
    plan.host = findHost (text, *p, v);
    if (! plan.host.ok) return plan;
    const auto spots = p->locate (v, 0.5 * (X0 + X1));
    if (plan.host.piece && ! spots.empty() && spots[0].def == plan.host.def) plan.branch = spots[0].branch;
    plan.start.V = spanText (text, plan.host.span);   // what plays there: the whole formula...
    if (plan.branch >= 0 && plan.branch < (int) plan.host.items.size())
    {
        NoteEdit n;
        if (readNote (plan.host.items[(size_t) plan.branch], n) && X0 >= n.A - 0.02 && X1 <= n.B + 0.02) { plan.own = true; plan.start = n; }
        else
        {
            std::string cond, value;   // ...or just the piece of it that plays there
            if (splitBranch (plan.host.items[(size_t) plan.branch], cond, value) || plan.host.items.size() > 1) plan.start.V = value;
        }
    }
    if (! plan.own) { plan.start.X0 = plan.start.A = X0; plan.start.X1 = plan.start.B = X1; plan.start.D0 = plan.start.D1 = 0; }
    plan.ok = true;
    return plan;
}

enum class NoteOp { Move, Copy, Delete };
// The text with the note changed: Move puts it at n (its old place goes quiet), Copy adds n and leaves the old place,
// Delete makes it quiet.
inline std::string applyNote (const std::string& text, const NotePlan& plan, const NoteEdit& n, NoteOp op)
{
    if (! plan.ok) return text;
    Host h = plan.host;
    if (! h.piece) h.items.assign (1, spanText (text, h.span));
    auto& it = h.items;
    const auto& s = plan.start;
    if (op == NoteOp::Copy) it.insert (it.begin(), noteBranch (n));
    else if (op == NoteOp::Delete)
    {
        if (plan.own) it.erase (it.begin() + plan.branch);
        else it.insert (it.begin(), noneBranch (s.X0, s.X1));
    }
    else if (plan.own) it[(size_t) plan.branch] = noteBranch (n);
    else
    {
        const bool covers = n.A <= s.X0 + 1e-9 && n.B >= s.X1 - 1e-9;   // the note still covers its old place
        if (! covers) it.insert (it.begin(), noneBranch (s.X0, s.X1));
        it.insert (it.begin(), noteBranch (n));
    }
    h.piece = h.piece || it.size() > 1 || it[0].find (':') != std::string::npos;
    return replaceSpan (text, h.span, hostText (h));
}

// A copied note on a voice of its own: a new line named after the voice it came from (hats2, so a hi-hat stays a
// hi-hat), or a new entry in y = [...] when that voice had no name.
inline std::string addVoice (const std::string& text, const std::string& fromName, const NoteEdit& n, int* newIndex = nullptr)
{
    auto p = Program::compile (text);
    if (! p->ok() || p->numVoices() >= Program::kMaxVoices) return text;
    std::string entry = "{" + noteBranch (n) + "}", def;
    if (isIdentifier (fromName))
    {
        std::string base = fromName;
        while (! base.empty() && std::isdigit ((unsigned char) base.back())) base.pop_back();
        if (base.empty()) base = "note";
        for (int k = 2; k < 1000; ++k)
        {
            const std::string nm = base + std::to_string (k);
            bool used = false;
            for (auto& d : p->getDefs()) if (d.name == nm) used = true;
            if (countWord (text, nm) > 0) used = true;
            if (! used) { def = nm + " = " + entry; entry = nm; break; }
        }
    }
    std::string t = text;
    const int nv = p->numVoices();
    const Span last = nv > 0 ? voiceEntrySpan (t, nv - 1) : Span();
    size_t after = last.e;
    while (after < t.size() && t[after] == ' ') ++after;
    if (last.ok() && after < t.size() && (t[after] == ']' || t[after] == ')' || t[after] == ','))
        t = t.substr (0, last.e) + ", " + entry + t.substr (last.e);
    else
    {
        while (! t.empty() && t.back() == '\n') t.pop_back();
        t += "\ny = [" + entry + "]\n";
    }
    if (! def.empty())
    {
        while (! t.empty() && t.back() == '\n') t.pop_back();
        t += "\n" + def + "\n";
    }
    if (newIndex) *newIndex = nv;
    return t;
}

} // namespace gg
