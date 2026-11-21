// Ghost Graph -- typesets a scene's equations like a math page: real fractions, raised powers, braces for the
// {cond: value} pieces, ⌊floor⌋ brackets, the y list as a column block. Click it to type in the plain text.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "GraphExpr.h"

namespace gmath
{
struct Box
{
    enum K { Text, Row, Frac, Sup, Fence, Brace, Grid } k = Row;
    juce::String s;
    bool italic = false;
    float size = 20.f;
    std::vector<Box> kids;
    int fence = 0;       // 0 ( ), 1 floor, 2 ceil, 3 | |
    int cols = 1;        // Grid: number of columns (cells are row-major)
    float w = 0, asc = 0, desc = 0;
    // grid layout
    std::vector<float> colW, rowAsc, rowDesc;
};

struct Typesetter
{
    std::function<juce::Font (float, bool)> font;
    const gg::Program* p = nullptr;
    const gg::Program::Def* def = nullptr;

    static Box txt (const juce::String& s, float size, bool italic = false) { Box b; b.k = Box::Text; b.s = s; b.size = size; b.italic = italic; return b; }
    static Box row (std::vector<Box> k, float size) { Box b; b.k = Box::Row; b.kids = std::move (k); b.size = size; return b; }
    static Box fenceOf (Box inner, int type, float size) { Box b; b.k = Box::Fence; b.fence = type; b.size = size; b.kids.push_back (std::move (inner)); return b; }

    static juce::String numText (double v)
    {
        if (std::abs (v - std::round (v)) < 1e-9) return juce::String ((long long) std::llround (v));
        juce::String s (v, 4);
        while (s.endsWithChar ('0')) s = s.dropLastCharacters (1);
        if (s.endsWithChar ('.')) s = s.dropLastCharacters (1);
        if (s.startsWith ("0.")) s = s.substring (0);
        return s;
    }
    static const char* greek (const std::string& n)   // theta -> θ and friends, as the famous equations write them
    {
        static const char* g[][2] = { { "theta", "\xce\xb8" }, { "phi", "\xcf\x86" }, { "omega", "\xcf\x89" }, { "gamma", "\xce\xb3" },
                                      { "sigma", "\xcf\x83" }, { "alpha", "\xce\xb1" }, { "beta", "\xce\xb2" }, { "delta", "\xce\xb4" },
                                      { "lambda", "\xce\xbb" }, { "mu", "\xce\xbc" } };
        for (auto& e : g) if (n == e[0]) return e[1];
        return nullptr;
    }
    static juce::String nameText (const std::string& n) { if (auto* gk = greek (n)) return juce::String::fromUTF8 (gk); return juce::String (n); }
    static bool italicName (const std::string& n) { return n.size() == 1 || greek (n) != nullptr; }

    using Op = gg::Program::Op;
    const gg::Program::Node& N (int i) const { return p->getNodes()[(size_t) i]; }
    bool isSum (int i) const { const auto op = N (i).op; return op == Op::Add || op == Op::Sub || isLogic (i); }
    bool isLogic (int i) const
    {
        const auto op = N (i).op;
        return op == Op::Lt || op == Op::Gt || op == Op::Le || op == Op::Ge || op == Op::Eq || op == Op::Ne || op == Op::And || op == Op::Or;
    }
    bool isAtom (int i) const
    {
        const auto& n = N (i);
        return (n.op == Op::Num && n.num >= 0) || n.op == Op::X || n.op == Op::Var || n.op == Op::Param
            || n.op == Op::Call || n.op == Op::Builtin;
    }
    bool startsWithNumber (int i) const
    {
        const auto& n = N (i);
        if (n.op == Op::Num) return n.idx == 0;
        if (n.op == Op::Neg) return true;
        if (n.op == Op::Mul || n.op == Op::Pow) return startsWithNumber (n.a);
        return false;
    }
    bool isName (int i) const { const auto op = N (i).op; return op == Op::X || op == Op::Var || op == Op::Param || (op == Op::Num && N (i).idx != 0); }

    Box paren (int i, float size, bool need) { return need ? fenceOf (make (i, size), 0, size) : make (i, size); }

    Box args (const std::vector<int>& kids, float size)
    {
        std::vector<Box> r;
        for (size_t i = 0; i < kids.size(); ++i)
        {
            if (i > 0) r.push_back (txt (", ", size));
            r.push_back (make (kids[i], size));
        }
        return fenceOf (row (std::move (r), size), 0, size);
    }

    Box make (int i, float size)
    {
        const auto& n = N (i);
        const float sub = juce::jmax (11.f, size * 0.9f);
        switch (n.op)
        {
            case Op::Num:
                if (n.idx == 1) return txt ("e", size, true);
                if (n.idx == 2) return txt (juce::String::fromUTF8 ("\xcf\x80"), size, true);
                if (n.idx == 3) return txt (juce::String::fromUTF8 ("\xcf\x84"), size, true);
                if (n.idx == 4) return txt ("none", size);
                return txt (numText (n.num), size);
            case Op::X: return txt ("x", size, true);
            case Op::Var: { const auto& nm = p->getDefs()[(size_t) n.idx].name; return txt (nameText (nm), size, italicName (nm)); }
            case Op::Param:
            {
                const std::string nm = (def != nullptr && n.idx < (int) def->params.size()) ? def->params[(size_t) n.idx] : "?";
                return txt (nameText (nm), size, italicName (nm));
            }
            case Op::Call:
            {
                const auto& nm = p->getDefs()[(size_t) n.idx].name;
                return row ({ txt (nameText (nm), size, italicName (nm)), args (n.kids, size) }, size);
            }
            case Op::Builtin:
            {
                const int f = n.idx;
                if (f == gg::Program::fFloor) return fenceOf (make (n.kids[0], size), 1, size);
                if (f == gg::Program::fCeil)  return fenceOf (make (n.kids[0], size), 2, size);
                if (f == gg::Program::fAbs)   return fenceOf (make (n.kids[0], size), 3, size);
                if (f == gg::Program::fMod)
                    return row ({ paren (n.kids[0], size, isSum (n.kids[0]) || N (n.kids[0]).op == Op::Div), txt (" mod ", size), paren (n.kids[1], size, ! isAtom (n.kids[1])) }, size);
                if (f == gg::Program::fZre || f == gg::Program::fZim)   // Re zeta(1/2 + iu), as the video writes it
                {
                    Box half; half.k = Box::Frac; half.size = size;
                    half.kids.push_back (txt ("1", sub));
                    half.kids.push_back (txt ("2", sub));
                    const int u = n.kids[0];
                    Box iu = row ({ txt ("i", size, true), paren (u, size, ! isAtom (u) || N (u).op == Op::Num) }, size);
                    return row ({ txt (f == gg::Program::fZre ? "Re " : "Im ", size), txt (juce::String::fromUTF8 ("\xce\xb6"), size, true),
                                  fenceOf (row ({ std::move (half), txt (" + ", size), std::move (iu) }, size), 0, size) }, size);
                }
                if (f == gg::Program::fSqrt)
                    return row ({ txt (juce::String::fromUTF8 ("\xe2\x88\x9a"), size), fenceOf (make (n.kids[0], size), 0, size) }, size);
                return row ({ txt (gg::Program::builtinName (f), size), args (n.kids, size) }, size);
            }
            case Op::Neg: return row ({ txt (juce::String::fromUTF8 ("\xe2\x88\x92"), size), paren (n.a, size, isSum (n.a)) }, size);
            case Op::Add: return row ({ paren (n.a, size, isLogic (n.a)), txt (" + ", size), paren (n.b, size, isLogic (n.b)) }, size);
            case Op::Sub: return row ({ paren (n.a, size, isLogic (n.a)), txt (juce::String::fromUTF8 (" \xe2\x88\x92 "), size), paren (n.b, size, isSum (n.b)) }, size);
            case Op::Mul:
            {
                std::vector<Box> r;
                r.push_back (paren (n.a, size, isSum (n.a)));
                if (startsWithNumber (n.b)) r.push_back (txt (juce::String::fromUTF8 (" \xc2\xb7 "), size));
                else if (isName (n.a) && isName (n.b)) r.push_back (txt (" ", size * 0.6f));
                r.push_back (paren (n.b, size, isSum (n.b) || N (n.b).op == Op::Neg));
                return row (std::move (r), size);
            }
            case Op::Div:
            {
                Box b; b.k = Box::Frac; b.size = size;
                b.kids.push_back (make (n.a, sub));
                b.kids.push_back (make (n.b, sub));
                return b;
            }
            case Op::Pow:
            {
                Box b; b.k = Box::Sup; b.size = size;
                b.kids.push_back (paren (n.a, size, ! isAtom (n.a)));
                b.kids.push_back (make (n.b, juce::jmax (10.f, size * 0.66f)));
                return b;
            }
            case Op::Lt: return row ({ make (n.a, size), txt (" < ", size), make (n.b, size) }, size);
            case Op::Gt: return row ({ make (n.a, size), txt (" > ", size), make (n.b, size) }, size);
            case Op::Le: return row ({ make (n.a, size), txt (juce::String::fromUTF8 (" \xe2\x89\xa4 "), size), make (n.b, size) }, size);
            case Op::Ge: return row ({ make (n.a, size), txt (juce::String::fromUTF8 (" \xe2\x89\xa5 "), size), make (n.b, size) }, size);
            case Op::Eq: return row ({ make (n.a, size), txt (" = ", size), make (n.b, size) }, size);
            case Op::Ne: return row ({ make (n.a, size), txt (juce::String::fromUTF8 (" \xe2\x89\xa0 "), size), make (n.b, size) }, size);
            case Op::And: return row ({ paren (n.a, size, N (n.a).op == Op::Or), txt (" and ", size), paren (n.b, size, N (n.b).op == Op::Or) }, size);
            case Op::Or:  return row ({ make (n.a, size), txt (" or ", size), make (n.b, size) }, size);
            case Op::Piece:
            {
                if (n.b == 1) return make (n.kids[0], size);   // a bare {condition}, written like the reference
                Box g; g.k = Box::Grid; g.cols = 2; g.size = size;
                const size_t k = n.kids.size();
                size_t j = 0;
                for (; j + 1 < k; j += 2)
                {
                    g.kids.push_back (make (n.kids[j + 1], size));
                    g.kids.push_back (row ({ txt ("     if ", size), make (n.kids[j], size) }, size));
                }
                if (j < k) { g.kids.push_back (make (n.kids[j], size)); g.kids.push_back (txt ("     otherwise", size)); }
                Box b; b.k = Box::Brace; b.size = size; b.kids.push_back (std::move (g));
                return b;
            }
        }
        return txt ("?", size);
    }

    Box defBox (const gg::Program::Def& d, float size)
    {
        def = &d;
        std::vector<Box> r;
        r.push_back (txt (nameText (d.name), size, italicName (d.name)));
        if (! d.params.empty())
        {
            std::vector<Box> ps;
            for (size_t i = 0; i < d.params.size(); ++i)
            {
                if (i > 0) ps.push_back (txt (", ", size));
                ps.push_back (txt (nameText (d.params[i]), size, italicName (d.params[i])));
            }
            r.push_back (fenceOf (row (std::move (ps), size), 0, size));
        }
        r.push_back (txt (" = ", size));
        r.push_back (make (d.root, size));
        Box b = row (std::move (r), size);
        def = nullptr;
        return b;
    }

    Box yBox (float size)
    {
        const auto& v = p->getVoiceRoots();
        const int n = (int) v.size();
        std::vector<Box> r;
        r.push_back (row ({ txt ("y", size, true), txt (" = ", size) }, size));
        if (p->isOutline())
        {
            std::vector<Box> a;
            for (int i = 0; i < n; ++i) { if (i > 0) a.push_back (txt (", ", size)); a.push_back (make (v[(size_t) i], size)); }
            r.push_back (txt ("outline", size));
            r.push_back (fenceOf (row (std::move (a), size), 0, size));
            return row (std::move (r), size);
        }
        if (n == 1) { r.push_back (make (v[0], size)); return row (std::move (r), size); }
        const int cols = n <= 10 ? 2 : 3;
        const int rows = (n + cols - 1) / cols;
        Box g; g.k = Box::Grid; g.cols = cols; g.size = size;
        for (int rr = 0; rr < rows; ++rr)
            for (int c = 0; c < cols; ++c)
            {
                const int idx = rr * cols + c;   // row by row, two to a row, like the reference
                if (idx < n) g.kids.push_back (row ({ make (v[(size_t) idx], size), txt ("      ", size) }, size));
                else g.kids.push_back (txt ("", size));
            }
        Box b; b.k = Box::Brace; b.size = size; b.kids.push_back (std::move (g));
        r.push_back (std::move (b));
        return row (std::move (r), size);
    }

    // ---------------------------------------------------------------- measuring
    float axis (float size) const { return size * 0.27f; }

    void measure (Box& b)
    {
        switch (b.k)
        {
            case Box::Text:
            {
                const auto f = font (b.size, b.italic);
                b.w = juce::GlyphArrangement::getStringWidth (f, b.s) + (b.italic ? b.size * 0.04f : 0.f);
                b.asc = b.size * 0.70f; b.desc = b.size * 0.22f;
                break;
            }
            case Box::Row:
                b.w = 0; b.asc = b.size * 0.70f; b.desc = b.size * 0.22f;
                for (auto& k : b.kids) { measure (k); b.w += k.w; b.asc = std::max (b.asc, k.asc); b.desc = std::max (b.desc, k.desc); }
                break;
            case Box::Frac:
            {
                auto& nu = b.kids[0]; auto& de = b.kids[1];
                measure (nu); measure (de);
                const float gap = b.size * 0.12f, ax = axis (b.size);
                b.w = std::max (nu.w, de.w) + b.size * 0.3f;
                b.asc = ax + gap + nu.desc + nu.asc;
                b.desc = gap + de.asc + de.desc - ax;
                break;
            }
            case Box::Sup:
            {
                auto& ba = b.kids[0]; auto& ex = b.kids[1];
                measure (ba); measure (ex);
                const float raise = ba.asc * 0.62f;
                b.w = ba.w + ex.w + b.size * 0.05f;
                b.asc = std::max (ba.asc, raise + ex.asc);
                b.desc = ba.desc;
                break;
            }
            case Box::Fence:
            {
                auto& in = b.kids[0];
                measure (in);
                const float side = b.fence == 3 ? b.size * 0.22f : b.size * 0.34f;
                b.w = in.w + 2 * side;
                b.asc = std::max (in.asc + b.size * 0.06f, b.size * 0.74f);
                b.desc = std::max (in.desc + b.size * 0.06f, b.size * 0.26f);
                break;
            }
            case Box::Grid:
            {
                const int cols = std::max (1, b.cols);
                const int rows = (int) (b.kids.size() + (size_t) cols - 1) / cols;
                b.colW.assign ((size_t) cols, 0.f);
                b.rowAsc.assign ((size_t) rows, b.size * 0.70f);
                b.rowDesc.assign ((size_t) rows, b.size * 0.22f);
                for (size_t i = 0; i < b.kids.size(); ++i)
                {
                    auto& k = b.kids[i];
                    measure (k);
                    const size_t r = i / (size_t) cols, c = i % (size_t) cols;
                    b.colW[c] = std::max (b.colW[c], k.w);
                    b.rowAsc[r] = std::max (b.rowAsc[r], k.asc);
                    b.rowDesc[r] = std::max (b.rowDesc[r], k.desc);
                }
                b.w = 0; for (auto c : b.colW) b.w += c;
                float h = 0; for (size_t r = 0; r < b.rowAsc.size(); ++r) h += b.rowAsc[r] + b.rowDesc[r] + (r > 0 ? b.size * 0.18f : 0.f);
                b.asc = h * 0.5f + axis (b.size);
                b.desc = h * 0.5f - axis (b.size);
                break;
            }
            case Box::Brace:
            {
                auto& g = b.kids[0];
                measure (g);
                b.w = g.w + b.size * 0.62f;
                b.asc = g.asc + b.size * 0.08f;
                b.desc = g.desc + b.size * 0.08f;
                break;
            }
        }
    }

    // ---------------------------------------------------------------- drawing
    void draw (juce::Graphics& g, const Box& b, float x, float base) const
    {
        switch (b.k)
        {
            case Box::Text:
                g.setFont (font (b.size, b.italic));
                g.drawSingleLineText (b.s, juce::roundToInt (x), juce::roundToInt (base));
                break;
            case Box::Row:
                for (auto& k : b.kids) { draw (g, k, x, base); x += k.w; }
                break;
            case Box::Frac:
            {
                const auto& nu = b.kids[0]; const auto& de = b.kids[1];
                const float gap = b.size * 0.12f, ax = axis (b.size);
                const float lineY = base - ax;
                draw (g, nu, x + (b.w - nu.w) * 0.5f, lineY - gap - nu.desc);
                draw (g, de, x + (b.w - de.w) * 0.5f, lineY + gap + de.asc);
                g.fillRect (x + b.size * 0.1f, lineY - 0.5f, b.w - b.size * 0.2f, std::max (1.0f, b.size * 0.05f));
                break;
            }
            case Box::Sup:
            {
                const auto& ba = b.kids[0]; const auto& ex = b.kids[1];
                draw (g, ba, x, base);
                draw (g, ex, x + ba.w + b.size * 0.04f, base - ba.asc * 0.62f);
                break;
            }
            case Box::Fence:
            {
                const auto& in = b.kids[0];
                const float side = b.fence == 3 ? b.size * 0.22f : b.size * 0.34f;
                const float top = base - b.asc + 1, bot = base + b.desc - 1;
                const float th = std::max (1.0f, b.size * 0.055f);
                juce::Path p;
                auto bracket = [&] (float sx, bool left)
                {
                    const float dir = left ? 1.f : -1.f;
                    if (b.fence == 0)
                    {
                        const float bulge = side * 0.55f;
                        const float x0 = left ? sx + side * 0.75f : sx + side * 0.25f;
                        p.startNewSubPath (x0, top);
                        p.quadraticTo (x0 - dir * bulge * 1.3f, (top + bot) * 0.5f, x0, bot);
                    }
                    else if (b.fence == 3)
                    {
                        const float xm = sx + side * 0.5f;
                        p.startNewSubPath (xm, top); p.lineTo (xm, bot);
                    }
                    else
                    {
                        const float xa = left ? sx + side * 0.35f : sx + side * 0.65f;
                        const float xb = xa + dir * side * 0.45f;
                        p.startNewSubPath (xa, top); p.lineTo (xa, bot);
                        if (b.fence == 1) { p.startNewSubPath (xa, bot); p.lineTo (xb, bot); }   // floor: feet at the bottom
                        else              { p.startNewSubPath (xa, top); p.lineTo (xb, top); }   // ceil: at the top
                    }
                };
                bracket (x, true);
                bracket (x + side + in.w, false);
                g.strokePath (p, juce::PathStrokeType (th, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                draw (g, in, x + side, base);
                break;
            }
            case Box::Grid:
            {
                float y = base - b.asc;
                const int cols = std::max (1, b.cols);
                for (size_t r = 0; r < b.rowAsc.size(); ++r)
                {
                    if (r > 0) y += b.size * 0.18f;
                    const float rb = y + b.rowAsc[r];
                    float cx = x;
                    for (int c = 0; c < cols; ++c)
                    {
                        const size_t i = r * (size_t) cols + (size_t) c;
                        if (i < b.kids.size()) draw (g, b.kids[i], cx, rb);
                        cx += b.colW[(size_t) c];
                    }
                    y = rb + b.rowDesc[r];
                }
                break;
            }
            case Box::Brace:
            {
                const auto& gr = b.kids[0];
                const float top = base - b.asc, bot = base + b.desc, mid = base - axis (b.size);
                const float bw = b.size * 0.42f, x0 = x + bw * 0.75f, xt = x + bw * 0.15f;
                juce::Path p;
                const float q = std::min (b.size * 0.35f, (bot - top) * 0.2f);
                p.startNewSubPath (x0 + bw * 0.25f, top);
                p.quadraticTo (x0 - bw * 0.2f, top, x0 - bw * 0.2f, top + q);
                p.lineTo (x0 - bw * 0.2f, mid - q);
                p.quadraticTo (x0 - bw * 0.2f, mid, xt, mid);
                p.quadraticTo (x0 - bw * 0.2f, mid, x0 - bw * 0.2f, mid + q);
                p.lineTo (x0 - bw * 0.2f, bot - q);
                p.quadraticTo (x0 - bw * 0.2f, bot, x0 + bw * 0.25f, bot);
                g.strokePath (p, juce::PathStrokeType (std::max (1.0f, b.size * 0.06f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                draw (g, gr, x + b.size * 0.62f, base);
                break;
            }
        }
    }
};
}

// The typeset page: the y block first, then every definition flowing left to right, wrapping like the reference.
class MathView : public juce::Component
{
public:
    std::function<void()> onClick;
    std::function<juce::Font (float, bool)> font;
    juce::Colour colour { 0xffeda43c };
    juce::Colour halo { 0x00000000 };   // a glow behind the math so it reads over watercolour
    float maxSize = 31.f;   // the videos write the math big: about 3% of the page width

    void setProgram (const gg::Program* prog) { p = prog; layout(); repaint(); }
    void resized() override { layout(); }
    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }
    bool hasContent() const { return ! items.empty(); }

    void paint (juce::Graphics& g) override
    {
        if (! halo.isTransparent())
        {
            g.setColour (halo);
            for (auto off : { juce::Point<int> (-1, 0), juce::Point<int> (1, 0), juce::Point<int> (0, -1), juce::Point<int> (0, 1),
                              juce::Point<int> (-2, 0), juce::Point<int> (2, 0), juce::Point<int> (0, -2), juce::Point<int> (0, 2) })
            {
                juce::Graphics::ScopedSaveState ss (g);
                g.setOrigin (off);
                for (auto& it : items) ts.draw (g, it.box, it.x, it.base);
            }
        }
        g.setColour (colour);
        for (auto& it : items) ts.draw (g, it.box, it.x, it.base);
    }

private:
    struct Item { gmath::Box box; float x = 0, base = 0; };
    const gg::Program* p = nullptr;
    gmath::Typesetter ts;
    std::vector<Item> items;

    void layout()
    {
        items.clear();
        if (p == nullptr || ! p->ok() || ! font || getWidth() < 40) return;
        ts.font = font;
        ts.p = p;
        for (float size = std::floor (maxSize); size >= 8.f; size -= 1.f)
        {
            items.clear();
            std::vector<gmath::Box> boxes;
            boxes.push_back (ts.yBox (size));
            for (auto& d : p->getDefs()) boxes.push_back (ts.defBox (d, size));
            for (auto& b : boxes) ts.measure (b);

            const float W = (float) getWidth() - 8.f, gap = size * 1.2f, lineGap = size * 0.35f;
            float x = 4.f, top = 4.f, lineAsc = 0, lineDesc = 0;
            std::vector<size_t> lineStart { 0 };
            std::vector<Item> placed;
            auto finishLine = [&] (size_t from)
            {
                for (size_t i = from; i < placed.size(); ++i) placed[i].base = top + lineAsc;
                top += lineAsc + lineDesc + lineGap;
                lineAsc = lineDesc = 0; x = 4.f;
            };
            size_t from = 0;
            for (size_t i = 0; i < boxes.size(); ++i)
            {
                auto& b = boxes[i];
                const bool newLine = i == 1 || (x > 4.f && x + b.w > W);   // the y block gets its own line
                if (newLine && placed.size() > from) { finishLine (from); from = placed.size(); }
                placed.push_back ({ b, x, 0 });
                x += b.w + gap;
                lineAsc = std::max (lineAsc, b.asc);
                lineDesc = std::max (lineDesc, b.desc);
            }
            if (placed.size() > from) finishLine (from);
            bool fits = top <= (float) getHeight();
            for (auto& it : placed) if (it.x + it.box.w > W + 4.f) fits = false;
            items = std::move (placed);
            if (fits) break;
        }
    }
};
