// Ghost Graph -- the equation engine. Plain C++17, no JUCE, no allocation while evaluating.
//
// A scene is a few lines of Desmos-style math. x is the time (in beats); every entry of the y list is one voice,
// and its value is that voice's pitch in octaves (0 = the TUNE note, A4 by default; 1/12 = one semitone up).
// Where a voice is undefined (a {condition: value} with no matching branch) it is silent.
//
//   y = [kick, -1, 7/12 - w/12]        the voices (several y lines just add more)
//   h = frac(x/2)                      a variable (re-computed for every x)
//   kick = {h < 0.3: 1.5e^(-14h) - 2.1} piecewise: {cond: value, cond: value, otherwise}
//   H(i) = i/12 - 1                    a function with parameters
//   # comment                          (also // comments)
//
// Numbers next to names multiply (3w^2, 14h, 2clamp(...)), as do names next to each other with a space (i s) and
// a variable before "(" (w(1-w)). Comparisons: < > <= >= = (chains like 0.5 < w < 0.97 work), and, or.
// Built in: frac floor ceil round abs sign sqrt sin cos tan exp ln log min max clamp mod tri saw sq, e, pi.
#pragma once
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <cctype>
#include <limits>
#include <algorithm>

namespace gg {

struct ExprError { int line = 0; std::string message; };

class Program {
public:
    static constexpr int kMaxVoices = 16;

    bool ok() const { return errors.empty(); }
    const std::vector<ExprError>& getErrors() const { return errors; }
    int numVoices() const { return (int) voices.size(); }
    const std::string& voiceLabel (int i) const { return voiceLabels[(size_t) i]; }
    // like the videos: a voice named ...hat... is a hi-hat, snare / clap / rim are noise; everything else is a tone
    int voiceKind (int i) const { return i >= 0 && i < (int) voiceKinds.size() ? voiceKinds[(size_t) i] : 0; }
    static int kindOf (std::string n)
    {
        for (auto& ch : n) ch = (char) std::tolower ((unsigned char) ch);
        if (n.find ("hat") != std::string::npos) return 1;
        if (n.find ("snare") != std::string::npos || n.find ("clap") != std::string::npos || n == "rim" || n.find ("shaker") != std::string::npos) return 2;
        return 0;
    }

    // Set the time, then ask for voices. Re-setting the same x keeps the cached variables.
    void setX (double newX)
    {
        if (newX == x && cacheStamp != 0) return;
        x = newX;
        if (++cacheStamp == 0) { std::fill (stamps.begin(), stamps.end(), 0u); cacheStamp = 1; }
    }
    double voice (int i)
    {
        if (i < 0 || i >= (int) voices.size()) return nan();
        depth = 0;
        const double v = eval (voices[(size_t) i], nullptr);
        return std::isfinite (v) ? v : nan();
    }

    static std::unique_ptr<Program> compile (const std::string& text);

    // the parsed equations, read-only (the window typesets them)
    enum class Op : uint8_t { Num, X, Var, Param, Call, Builtin, Neg, Add, Sub, Mul, Div, Pow,
                              Lt, Gt, Le, Ge, Eq, And, Or, Piece, Ne };
    enum Fn : uint8_t { fFrac, fFloor, fCeil, fRound, fAbs, fSign, fSqrt, fSin, fCos, fTan, fExp, fLn, fLog,
                        fMin, fMax, fClamp, fMod, fTri, fSaw, fSq, fAt };
    // Num: idx 1 = e, 2 = pi, 3 = tau (as written)
    struct Node { Op op = Op::Num; double num = 0; int a = -1, b = -1, idx = 0; std::vector<int> kids; };
    struct Def  { std::string name; std::vector<std::string> params; int root = -1; int line = 0; std::string rhs; };
    const std::vector<Node>& getNodes() const { return nodes; }
    const std::vector<Def>& getDefs() const { return defs; }
    const std::vector<int>& getVoiceRoots() const { return voices; }
    bool isOutline() const { return outlineY; }
    const std::string& voiceSource (int i) const { return voiceSrc[(size_t) i]; }   // the entry as written in y = [...]
    static const char* builtinName (int f)
    {
        static const char* names[] = { "frac", "floor", "ceil", "round", "abs", "sign", "sqrt", "sin", "cos", "tan",
                                       "exp", "ln", "log", "min", "max", "clamp", "mod", "tri", "saw", "sq", "at" };
        return names[f];
    }

private:

    std::vector<Node> nodes;
    std::vector<Def> defs;
    std::vector<int> voices;
    std::vector<std::string> voiceLabels;
    std::vector<int> voiceKinds;
    std::vector<std::string> voiceSrc;
    std::vector<ExprError> errors;
    bool outlineY = false;
    std::vector<double> cache;
    std::vector<uint32_t> stamps;
    uint32_t cacheStamp = 0;
    double x = 0;
    int depth = 0;

    static double nan() { return std::numeric_limits<double>::quiet_NaN(); }
    static bool truthy (double v) { return v == v && v != 0.0; }

    double eval (int n, const double* args)
    {
        const Node& nd = nodes[(size_t) n];
        switch (nd.op)
        {
            case Op::Num:   return nd.num;
            case Op::X:     return x;
            case Op::Param: return args != nullptr ? args[nd.idx] : nan();
            case Op::Var:
            {
                if (stamps[(size_t) nd.idx] == cacheStamp) return cache[(size_t) nd.idx];
                if (++depth > 48) { --depth; return nan(); }
                stamps[(size_t) nd.idx] = cacheStamp;      // guards a variable that refers to itself
                cache[(size_t) nd.idx] = nan();
                const double v = eval (defs[(size_t) nd.idx].root, nullptr);
                --depth;
                cache[(size_t) nd.idx] = v;
                return v;
            }
            case Op::Call:
            {
                if (++depth > 48) { --depth; return nan(); }
                double a[8];
                const size_t k = nd.kids.size();
                for (size_t i = 0; i < k && i < 8; ++i) a[i] = eval (nd.kids[i], args);
                const double v = eval (defs[(size_t) nd.idx].root, a);
                --depth;
                return v;
            }
            case Op::Builtin: return builtin ((Fn) nd.idx, nd, args);
            case Op::Neg: return -eval (nd.a, args);
            case Op::Add: return eval (nd.a, args) + eval (nd.b, args);
            case Op::Sub: return eval (nd.a, args) - eval (nd.b, args);
            case Op::Mul: return eval (nd.a, args) * eval (nd.b, args);
            case Op::Div: { const double d = eval (nd.b, args); return d == 0 ? nan() : eval (nd.a, args) / d; }
            case Op::Pow: return std::pow (eval (nd.a, args), eval (nd.b, args));
            case Op::Lt:  { const double l = eval (nd.a, args), r = eval (nd.b, args); return (l == l && r == r) ? (l <  r ? 1.0 : 0.0) : nan(); }
            case Op::Gt:  { const double l = eval (nd.a, args), r = eval (nd.b, args); return (l == l && r == r) ? (l >  r ? 1.0 : 0.0) : nan(); }
            case Op::Le:  { const double l = eval (nd.a, args), r = eval (nd.b, args); return (l == l && r == r) ? (l <= r ? 1.0 : 0.0) : nan(); }
            case Op::Ge:  { const double l = eval (nd.a, args), r = eval (nd.b, args); return (l == l && r == r) ? (l >= r ? 1.0 : 0.0) : nan(); }
            case Op::Eq:  { const double l = eval (nd.a, args), r = eval (nd.b, args); return (l == l && r == r) ? (std::abs (l - r) < 1e-9 ? 1.0 : 0.0) : nan(); }
            case Op::Ne:  { const double l = eval (nd.a, args), r = eval (nd.b, args); return (l == l && r == r) ? (std::abs (l - r) < 1e-9 ? 0.0 : 1.0) : nan(); }
            case Op::And: return (truthy (eval (nd.a, args)) && truthy (eval (nd.b, args))) ? 1.0 : 0.0;
            case Op::Or:  return (truthy (eval (nd.a, args)) || truthy (eval (nd.b, args))) ? 1.0 : 0.0;
            case Op::Piece:
            {
                const size_t k = nd.kids.size();
                size_t i = 0;
                for (; i + 1 < k; i += 2)
                    if (truthy (eval (nd.kids[i], args))) return eval (nd.kids[i + 1], args);
                return i < k ? eval (nd.kids[i], args) : nan();
            }
        }
        return nan();
    }

    static double fracOf (double v) { return v - std::floor (v); }

    double builtin (Fn f, const Node& nd, const double* args)
    {
        if (f == fAt)
        {
            // at(f, t): f at the time t (how a moved or stretched note is written)
            const double t = eval (nd.kids[1], args);
            if (! std::isfinite (t)) return nan();
            const double saved = x;
            x = t; if (++cacheStamp == 0) { std::fill (stamps.begin(), stamps.end(), 0u); cacheStamp = 1; }
            const double v = eval (nd.kids[0], args);
            x = saved; if (++cacheStamp == 0) { std::fill (stamps.begin(), stamps.end(), 0u); cacheStamp = 1; }
            return v;
        }
        const double a = eval (nd.kids[0], args);
        switch (f)
        {
            case fFrac:  return fracOf (a);
            case fFloor: return std::floor (a);
            case fCeil:  return std::ceil (a);
            case fRound: return std::round (a);
            case fAbs:   return std::abs (a);
            case fSign:  return a > 0 ? 1.0 : (a < 0 ? -1.0 : 0.0);
            case fSqrt:  return a < 0 ? nan() : std::sqrt (a);
            case fSin:   return std::sin (a);
            case fCos:   return std::cos (a);
            case fTan:   return std::tan (a);
            case fExp:   return std::exp (a);
            case fLn:    return a <= 0 ? nan() : std::log (a);
            case fLog:   return a <= 0 ? nan() : std::log10 (a);
            case fTri:   { const double p = fracOf (a); return p < 0.5 ? 4 * p - 1 : 3 - 4 * p; }
            case fSaw:   return 2 * fracOf (a) - 1;
            case fSq:    return fracOf (a) < 0.5 ? 1.0 : -1.0;
            case fAt:    break;
            case fMin:   { double m = a; for (size_t i = 1; i < nd.kids.size(); ++i) m = std::fmin (m, eval (nd.kids[i], args)); return m; }
            case fMax:   { double m = a; for (size_t i = 1; i < nd.kids.size(); ++i) m = std::fmax (m, eval (nd.kids[i], args)); return m; }
            case fMod:   { const double b = eval (nd.kids[1], args); return b == 0 ? nan() : a - b * std::floor (a / b); }
            case fClamp: { const double lo = eval (nd.kids[1], args), hi = eval (nd.kids[2], args); return a < lo ? lo : (a > hi ? hi : a); }
        }
        return nan();
    }

    // ------------------------------------------------------------------------------------------------ compiling
    struct Tok { enum T { Num, Id, Sym, End } t = End; std::string s; double v = 0; size_t b = 0, e = 0; };   // b, e: where it sits in the line

    struct Parser
    {
        Program& p;
        std::vector<Tok> toks;
        size_t pos = 0;
        const std::vector<std::string>* params = nullptr;
        std::string err;

        Parser (Program& prog, const std::string& src) : p (prog) { lex (src); }

        void lex (const std::string& s)
        {
            size_t i = 0;
            while (i < s.size())
            {
                const unsigned char c = (unsigned char) s[i];
                if (std::isspace (c)) { ++i; continue; }
                if (std::isdigit (c) || (c == '.' && i + 1 < s.size() && std::isdigit ((unsigned char) s[i + 1])))
                {
                    size_t j = i; bool dot = false;
                    while (j < s.size() && (std::isdigit ((unsigned char) s[j]) || (s[j] == '.' && ! dot))) { if (s[j] == '.') dot = true; ++j; }
                    Tok t; t.t = Tok::Num; t.v = std::strtod (s.substr (i, j - i).c_str(), nullptr); t.b = i; t.e = j; toks.push_back (t); i = j; continue;
                }
                if (std::isalpha (c) || c == '_')
                {
                    size_t j = i;
                    while (j < s.size() && (std::isalnum ((unsigned char) s[j]) || s[j] == '_')) ++j;
                    Tok t; t.t = Tok::Id; t.s = s.substr (i, j - i); t.b = i; t.e = j; toks.push_back (t); i = j; continue;
                }
                if (c == '!' && i + 1 < s.size() && s[i + 1] == '=')
                { Tok t; t.t = Tok::Sym; t.s = "!="; t.b = i; t.e = i + 2; toks.push_back (t); i += 2; continue; }
                if ((c == '<' || c == '>') && i + 1 < s.size() && s[i + 1] == '=')
                { Tok t; t.t = Tok::Sym; t.s = s.substr (i, 2); t.b = i; t.e = i + 2; toks.push_back (t); i += 2; continue; }
                if (c == '*' && i + 1 < s.size() && s[i + 1] == '*')   // ** as power
                { Tok t; t.t = Tok::Sym; t.s = "^"; t.b = i; t.e = i + 2; toks.push_back (t); i += 2; continue; }
                if (std::string ("+-*/^()[]{},:<>=").find ((char) c) != std::string::npos)
                { Tok t; t.t = Tok::Sym; t.s = std::string (1, (char) c); t.b = i; t.e = i + 1; toks.push_back (t); ++i; continue; }
                err = std::string ("can't read '") + (char) c + "'";
                ++i;
            }
            toks.push_back (Tok());
        }

        const Tok& peek() const { return toks[pos]; }
        bool isSym (const char* s) const { return peek().t == Tok::Sym && peek().s == s; }
        bool isWord (const char* s) const { return peek().t == Tok::Id && peek().s == s; }
        bool accept (const char* s) { if (isSym (s)) { ++pos; return true; } return false; }
        void expect (const char* s) { if (! accept (s) && err.empty()) err = std::string ("expected '") + s + "'"; }

        int add (Node n) { p.nodes.push_back (std::move (n)); return (int) p.nodes.size() - 1; }
        int bin (Op op, int a, int b) { Node n; n.op = op; n.a = a; n.b = b; return add (n); }
        int num (double v) { Node n; n.op = Op::Num; n.num = v; return add (n); }

        int parseExpr() { return parseOr(); }

        int parseOr()
        {
            int l = parseAnd();
            while (err.empty() && isWord ("or")) { ++pos; l = bin (Op::Or, l, parseAnd()); }
            return l;
        }
        int parseAnd()
        {
            int l = parseCmp();
            while (err.empty() && isWord ("and")) { ++pos; l = bin (Op::And, l, parseCmp()); }
            return l;
        }
        int parseCmp()
        {
            int l = parseAdd();
            int result = -1;
            while (err.empty())
            {
                Op op;
                if      (isSym ("<"))  op = Op::Lt;
                else if (isSym (">"))  op = Op::Gt;
                else if (isSym ("<=")) op = Op::Le;
                else if (isSym (">=")) op = Op::Ge;
                else if (isSym ("="))  op = Op::Eq;
                else if (isSym ("!=")) op = Op::Ne;
                else break;
                ++pos;
                const int r = parseAdd();
                const int c = bin (op, l, r);
                result = result < 0 ? c : bin (Op::And, result, c);   // 0.5 < w < 0.97
                l = r;
            }
            return result < 0 ? l : result;
        }
        int parseAdd()
        {
            int l = parseMul();
            while (err.empty())
            {
                if (accept ("+")) l = bin (Op::Add, l, parseMul());
                else if (accept ("-")) l = bin (Op::Sub, l, parseMul());
                else break;
            }
            return l;
        }
        bool startsPrimary() const
        {
            const Tok& t = peek();
            if (t.t == Tok::Num) return true;
            if (t.t == Tok::Id) return t.s != "and" && t.s != "or";
            return t.t == Tok::Sym && (t.s == "(" || t.s == "{");
        }
        int parseMul()
        {
            int l = parseUnary();
            while (err.empty())
            {
                if (accept ("*")) l = bin (Op::Mul, l, parseUnary());
                else if (accept ("/")) l = bin (Op::Div, l, parseUnary());
                else if (startsPrimary()) l = bin (Op::Mul, l, parsePow());   // implicit: 3w, 2clamp(..), i s
                else break;
            }
            return l;
        }
        int parseUnary()
        {
            if (accept ("-")) { Node n; n.op = Op::Neg; n.a = parseUnary(); return add (n); }
            if (accept ("+")) return parseUnary();
            return parsePow();
        }
        int parsePow()
        {
            const int base = parsePrimary();
            if (err.empty() && accept ("^")) return bin (Op::Pow, base, parseUnary());   // right-assoc, allows 2^-3
            return base;
        }

        static int builtinIndex (const std::string& s, int& arity)
        {
            static const char* names[] = { "frac", "floor", "ceil", "round", "abs", "sign", "sqrt", "sin", "cos", "tan",
                                           "exp", "ln", "log", "min", "max", "clamp", "mod", "tri", "saw", "sq", "at" };
            for (int i = 0; i < 21; ++i)
                if (s == names[i])
                {
                    arity = (i == fMin || i == fMax) ? -2 : (i == fClamp ? 3 : ((i == fMod || i == fAt) ? 2 : 1));
                    return i;
                }
            return -1;
        }

        std::vector<int> parseArgs()
        {
            std::vector<int> args;
            expect ("(");
            if (! isSym (")"))
                do { args.push_back (parseExpr()); } while (err.empty() && accept (","));
            expect (")");
            return args;
        }

        int parsePrimary()
        {
            const Tok t = peek();
            if (t.t == Tok::Num) { ++pos; return num (t.v); }
            if (accept ("("))
            {
                const int e = parseExpr();
                expect (")");
                return e;
            }
            if (accept ("{"))
            {
                Node n; n.op = Op::Piece;
                if (! isSym ("}"))
                {
                    do
                    {
                        const int first = parseExpr();
                        if (accept (":")) { n.kids.push_back (first); n.kids.push_back (parseExpr()); }
                        else { n.kids.push_back (first); break; }   // the "otherwise" value is last
                    } while (err.empty() && accept (","));
                }
                expect ("}");
                if (err.empty() && n.kids.empty()) err = "empty { }";
                if (err.empty() && n.kids.size() == 1)
                {
                    const Op o = p.nodes[(size_t) n.kids[0]].op;
                    if (o == Op::Lt || o == Op::Gt || o == Op::Le || o == Op::Ge || o == Op::Eq || o == Op::Ne || o == Op::And || o == Op::Or)
                    { n.kids.push_back (num (1.0)); n.b = 1; }   // b = 1 marks the bare {condition}
                }
                return add (n);
            }
            if (t.t == Tok::Id)
            {
                ++pos;
                const std::string& s = t.s;
                if (params != nullptr)
                    for (size_t i = 0; i < params->size(); ++i)
                        if ((*params)[i] == s) { Node n; n.op = Op::Param; n.idx = (int) i; return add (n); }
                if (s == "x") { Node n; n.op = Op::X; return add (n); }
                if (s == "pi")  { const int k = num (3.14159265358979323846); p.nodes[(size_t) k].idx = 2; return k; }
                if (s == "e")   { const int k = num (2.71828182845904523536); p.nodes[(size_t) k].idx = 1; return k; }
                if (s == "tau") { const int k = num (6.28318530717958647692); p.nodes[(size_t) k].idx = 3; return k; }
                if (s == "none") { const int k = num (std::numeric_limits<double>::quiet_NaN()); p.nodes[(size_t) k].idx = 4; return k; }   // silence
                for (size_t d = 0; d < p.defs.size(); ++d)
                    if (p.defs[d].name == s)
                    {
                        if (p.defs[d].params.empty()) { Node n; n.op = Op::Var; n.idx = (int) d; return add (n); }
                        if (! isSym ("(")) { err = s + " needs ( )"; return num (0); }
                        Node n; n.op = Op::Call; n.idx = (int) d; n.kids = parseArgs();
                        if (err.empty() && n.kids.size() != p.defs[d].params.size())
                            err = s + " takes " + std::to_string (p.defs[d].params.size()) + " value(s)";
                        return add (n);
                    }
                int arity = 0;
                const int b = builtinIndex (s, arity);
                if (b >= 0)
                {
                    if (! isSym ("(")) { err = s + " needs ( )"; return num (0); }
                    Node n; n.op = Op::Builtin; n.idx = b; n.kids = parseArgs();
                    if (err.empty())
                    {
                        const int k = (int) n.kids.size();
                        if ((arity > 0 && k != arity) || (arity == -2 && k < 1))
                            err = s + " takes " + std::to_string (arity > 0 ? arity : 2) + " value(s)";
                    }
                    return add (n);
                }
                err = "unknown name '" + s + "'";
                return num (0);
            }
            if (err.empty())
                err = t.t == Tok::End ? "the line ends too early" : "unexpected '" + t.s + "'";
            if (t.t != Tok::End) ++pos;
            return num (0);
        }
    };

    static std::string trim (const std::string& s)
    {
        size_t a = 0, b = s.size();
        while (a < b && std::isspace ((unsigned char) s[a])) ++a;
        while (b > a && std::isspace ((unsigned char) s[b - 1])) --b;
        return s.substr (a, b - a);
    }
    static bool validName (const std::string& s)
    {
        if (s.empty() || ! (std::isalpha ((unsigned char) s[0]) || s[0] == '_')) return false;
        for (char c : s) if (! (std::isalnum ((unsigned char) c) || c == '_')) return false;
        return true;
    }
    static bool reserved (const std::string& s)
    {
        int ar = 0;
        return s == "x" || s == "y" || s == "e" || s == "pi" || s == "tau" || s == "and" || s == "or" || s == "none"
            || Parser::builtinIndex (s, ar) >= 0;
    }
};

inline std::unique_ptr<Program> Program::compile (const std::string& text)
{
    auto prog = std::make_unique<Program>();
    Program& p = *prog;
    struct YLine { int line; std::string rhs; };
    std::vector<YLine> ylines;
    std::vector<std::pair<std::string, std::string>> noteOf;   // edit name -> the voice it edits (from "# note:" comments)

    // pass 1: split lines, read each left-hand side
    int lineNo = 0;
    size_t start = 0;
    while (start <= text.size())
    {
        size_t end = text.find ('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr (start, end - start);
        ++lineNo;
        start = end + 1;
        {
            const size_t nk = line.find ("# note:");
            if (nk != std::string::npos)
            {
                const size_t bar = line.rfind ('|');
                const size_t eqp = line.find ('=');
                if (bar != std::string::npos && eqp != std::string::npos && eqp < nk)
                    noteOf.push_back ({ trim (line.substr (0, eqp)), trim (line.substr (bar + 1)) });
            }
        }
        for (const char* c : { "#", "//" })
        {
            const size_t k = line.find (c);
            if (k != std::string::npos) line = line.substr (0, k);
        }
        line = trim (line);
        if (line.empty()) { if (end == text.size()) break; continue; }

        size_t eq = std::string::npos;
        for (size_t i = 0; i < line.size(); ++i)
            if (line[i] == '=' && (i == 0 || (line[i - 1] != '<' && line[i - 1] != '>' && line[i - 1] != '!'))) { eq = i; break; }
        if (eq == std::string::npos) { p.errors.push_back ({ lineNo, "needs a name = ..." }); continue; }

        const std::string lhs = trim (line.substr (0, eq));
        const std::string rhs = trim (line.substr (eq + 1));
        if (rhs.empty()) { p.errors.push_back ({ lineNo, "nothing after =" }); continue; }
        if (lhs == "y") { ylines.push_back ({ lineNo, rhs }); continue; }

        Def d; d.line = lineNo; d.rhs = rhs;
        const size_t paren = lhs.find ('(');
        if (paren == std::string::npos) d.name = lhs;
        else
        {
            d.name = trim (lhs.substr (0, paren));
            const size_t close = lhs.find (')', paren);
            if (close == std::string::npos || trim (lhs.substr (close + 1)) != "")
            { p.errors.push_back ({ lineNo, "write a function as F(a, b) = ..." }); continue; }
            std::string inside = lhs.substr (paren + 1, close - paren - 1) + ",";
            size_t s0 = 0;
            for (size_t c = inside.find (','); c != std::string::npos; s0 = c + 1, c = inside.find (',', s0))
            {
                const std::string pn = trim (inside.substr (s0, c - s0));
                if (! validName (pn) || reserved (pn)) { p.errors.push_back ({ lineNo, "bad value name '" + pn + "'" }); d.name.clear(); break; }
                d.params.push_back (pn);
            }
            if (d.params.size() > 8) { p.errors.push_back ({ lineNo, "at most 8 values" }); continue; }
        }
        if (d.name.empty()) continue;
        if (! validName (d.name) || reserved (d.name)) { p.errors.push_back ({ lineNo, "'" + d.name + "' can't be a name" }); continue; }
        bool dup = false;
        for (auto& o : p.defs) if (o.name == d.name) dup = true;
        if (dup) { p.errors.push_back ({ lineNo, d.name + " is defined twice" }); continue; }
        p.defs.push_back (d);
    }
    if (ylines.empty() && p.errors.empty()) p.errors.push_back ({ 1, "needs a y = [ ... ] line (the voices)" });

    // pass 2: the right-hand sides
    for (auto& d : p.defs)
    {
        Parser ps (p, d.rhs);
        ps.params = &d.params;
        d.root = ps.parseExpr();
        if (ps.err.empty() && ps.peek().t != Tok::End) ps.err = "unexpected '" + ps.peek().s + "'";
        if (! ps.err.empty()) p.errors.push_back ({ d.line, ps.err });
    }
    for (auto& yl : ylines)
    {
        Parser ps (p, yl.rhs);
        bool outline = false;
        if (ps.isWord ("outline") && ps.toks[ps.pos + 1].t == Tok::Sym && ps.toks[ps.pos + 1].s == "(")
        { ps.pos += 2; outline = true; p.outlineY = true; }   // y = outline(a, b, c): a list of voices, as in the reference
        const bool list = outline || ps.accept ("[");
        do
        {
            if (list && ps.isSym (outline ? ")" : "]")) break;
            const size_t before = ps.pos;
            const int r = ps.parseExpr();
            if (! ps.err.empty()) break;
            std::string label;
            if (ps.pos == before + 1 && ps.toks[before].t == Tok::Id) label = ps.toks[before].s;
            if ((int) p.voices.size() >= kMaxVoices) { ps.err = "at most 16 voices"; break; }
            p.voices.push_back (r);
            p.voiceLabels.push_back (label);
            {   // an edited voice keeps the sound of what it was (edit1 of "hats" is still a hi-hat)
                std::string base = label;
                for (int hop = 0; hop < 8; ++hop)
                {
                    bool found = false;
                    for (auto& nt : noteOf) if (nt.first == base) { base = nt.second; found = true; break; }
                    if (! found) break;
                }
                p.voiceKinds.push_back (kindOf (base));
            }
            p.voiceSrc.push_back (ps.toks[before].b < ps.toks[ps.pos - 1].e ? yl.rhs.substr (ps.toks[before].b, ps.toks[ps.pos - 1].e - ps.toks[before].b) : label);
        } while (list && ps.accept (","));
        if (list && ps.err.empty()) ps.expect (outline ? ")" : "]");
        if (ps.err.empty() && ps.peek().t != Tok::End) ps.err = "unexpected '" + ps.peek().s + "'";
        if (! ps.err.empty()) p.errors.push_back ({ yl.line, ps.err });
    }
    std::sort (p.errors.begin(), p.errors.end(), [] (const ExprError& a, const ExprError& b) { return a.line < b.line; });
    if (! p.ok()) { p.voices.clear(); p.voiceLabels.clear(); p.voiceKinds.clear(); p.voiceSrc.clear(); }
    p.cache.assign (p.defs.size(), 0.0);
    p.stamps.assign (p.defs.size(), 0u);
    p.cacheStamp = 0;
    return prog;
}

// ------------------------------------------------------------------------------------------------ the scenes
struct Scene { std::string name, text; double speed = 1.0; };   // speed: how fast x moves in this scene (1 = one unit per beat)

inline std::vector<Scene> factoryScenes()
{
    // the six scenes of the reference video, equation for equation
    return {
        { "one",
          "y = [kick, -1, -1 + 3w^2/12, 7/12 - w/12, high]\n"
          "kick = {h < 0.3: 1.5e^(-14h) - 2.1}\n"
          "high = {w > 0.5: 19/12 - (w - 0.5)/6}\n"
          "h = frac(x/2)\n"
          "w = frac(x/8)\n", 1.0 },
        { "two",
          "y = [kick, V(18141113), V(25212020), V(30282629), V(32303032), V(33333335), V(37373738)]\n"
          "kick = {q < 0.3: 1.2e^(-14q) - 2, {q > 0.875 and frac(8q) < 0.6: 0.9e^(-5frac(8q)) - 2}}\n"
          "V(N) = {q < 0.93: (mod(floor(N/100^(3 - c)), 100) - 30 - 2clamp(1 - q/0.08, 0, 1))/12}\n"
          "q = frac(x/4)\n"
          "c = floor(x/4) + 2\n", 1.058 },
        { "three",
          "y = [kick, motor, H(0), H(7)]\n"
          "kick = {frac(x/4) < 0.5: 1.5e^(-9frac(x/4)) - 2}\n"
          "motor = {frac(2x) < 0.55 + 0.3a: -1 + a/12}\n"
          "a = {m = 0 or m = 3 or m = 6}\n"
          "H(i) = (i + R(c) + (R(c + 1) - R(c))clamp((q - 0.85)/0.15, 0, 1))/12 - 1\n"
          "R(j) = {mod(j, 4) = 0: 0, mod(j, 4) = 1: 1, mod(j, 4) = 2: 3, 4}\n"
          "m = mod(floor(2x) + 16, 8)\n"
          "q = frac(x/4)\n"
          "c = floor(x/4) + 2\n", 1.164 },
        { "four",
          "y = [kick, H(0), H(7), H(12), C(14), C(15)]\n"
          "kick = {w < 0.12: 1.4e^(-30w) - 2}\n"
          "H(i) = {w < 0.55: i/12 - 1 - (w/0.55)^2}\n"
          "C(i) = {w > 0.5 and w < 0.97: (i + 2(w - 0.5))/12}\n"
          "w = frac(x/8)\n", 0.853 },
        { "five",
          "y = [kick, tap, -1, -5/12, tune, tune - 1]\n"
          "kick = {m = 0 or m = 6: 1.2e^(-9f) - 2}\n"
          "tap = {(m = 3 or m = 4 or m = 7) and f < 0.5: 0.5e^(-10f) - 1.1}\n"
          "tune = (R(c) + (R(c + 1) - R(c))clamp((q - 0.8)/0.2, 0, 1) - 0.5clamp(1 - q/0.03, 0, 1)(c = 0))/12 + 1\n"
          "R(j) = {mod(j, 4) = 0: 0, mod(j, 4) = 1: 3, mod(j, 4) = 2: 1, -1}\n"
          "m = mod(floor(2x) + 16, 8)\n"
          "f = frac(2x)\n"
          "q = frac(x/4)\n"
          "c = floor(x/4) + 2\n", 0.914 },
        { "six",
          "y = [V(-12), V(-5), V(1), V(8), sub]\n"
          "V(i) = i s/12\n"
          "sub = {s > 0.5: -2}\n"
          "s = (sin(pi frac(x/8)))^2\n", 1.067 },
    };
}

} // namespace gg
