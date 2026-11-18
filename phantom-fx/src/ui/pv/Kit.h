// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX painted unit visuals (0.1.10): the drawing kit. A line-by-line port of the art designer's live painter
// (phantom-anime/visuals/src/kit.js, engine.js helpers): the same maths in, the same polygons out, drawn through one class G
// (polygon fills, round strokes, ellipses, a few small gradients, sprites blitted 1:1, tinted masks, clips, cached layers).
// Scenes are laid out in stage units (su), 280 x 300; G maps them onto the look's stage rectangle.
// UI thread only. Nothing here touches the audio thread or the sound.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include <cmath>

namespace phantom::pv
{
constexpr float TAU = 6.28318530718f, PI = 3.14159265359f;
constexpr float SU_W = 280.0f, SU_H = 300.0f;

struct V2
{
    float x = 0, y = 0;
    V2() = default;
    V2 (float a, float b) : x (a), y (b) {}
    float& operator[] (int i) { return i == 0 ? x : y; }
    float operator[] (int i) const { return i == 0 ? x : y; }
};
using Poly = std::vector<V2>;

inline float clampf (float v, float a, float b) { return v < a ? a : v > b ? b : v; }
inline float lerp (float a, float b, float t) { return a + (b - a) * t; }
inline float sstep (float a, float b, float x) { const float t = clampf ((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); }
inline float hyp (float a, float b) { return std::sqrt (a * a + b * b); }

// ---- the JS hash / noise, bit for bit (Math.imul on int32 = uint32 arithmetic)
inline float h2 (double x, double y)
{
    const juce::uint32 xi = (juce::uint32) (juce::int32) (std::trunc (x)), yi = (juce::uint32) (juce::int32) (std::trunc (y));
    juce::uint32 n = xi * 374761393u + yi * 668265263u;
    n = (n ^ (n >> 13)) * 1274126177u;
    return (float) ((double) (n ^ (n >> 16)) / 4294967296.0);
}
inline float noise (float x, float y)
{
    const float xi = std::floor (x), yi = std::floor (y), xf = x - xi, yf = y - yi, u = xf * xf * (3 - 2 * xf), v = yf * yf * (3 - 2 * yf);
    return lerp (lerp (h2 (xi, yi), h2 (xi + 1, yi), u), lerp (h2 (xi, yi + 1), h2 (xi + 1, yi + 1), u), v);
}
inline float fbm (float x, float y, int o = 4)
{
    float s = 0, a = 0.5f, f = 1; for (int i = 0; i < o; ++i) { s += a * noise (x * f, y * f); a *= 0.5f; f *= 2; }
    return s / (1.0f - std::pow (0.5f, (float) o));
}
inline float hsh (double i, double salt = 0) { return h2 (i * 7 + 3, salt * 13 + 5); }
inline float hsg (double i, double salt = 0) { return (hsh (i, salt) + hsh (i, salt + 101) + hsh (i, salt + 202) - 1.5f) * 1.15f; }

// ---- colours: [h, s, l] as in CSS hsl(); K() makes the colour
struct HSL
{
    float h = 0, s = 0, l = 0;
    HSL() = default;
    HSL (float a, float b, float c) : h (a), s (b), l (c) {}
    float operator[] (int i) const { return i == 0 ? h : i == 1 ? s : l; }
};
inline std::array<float, 3> hsl2rgb (HSL c)
{
    const float h = std::fmod (std::fmod (c.h, 360.0f) + 360.0f, 360.0f) / 60.0f, s = clampf (c.s, 0, 100) / 100.0f, l = clampf (c.l, 0, 100) / 100.0f;
    const float q = (1 - std::abs (2 * l - 1)) * s, x = q * (1 - std::abs (std::fmod (h, 2.0f) - 1)), m = l - q / 2;
    float r, g, b;
    if (h < 1) { r = q; g = x; b = 0; } else if (h < 2) { r = x; g = q; b = 0; } else if (h < 3) { r = 0; g = q; b = x; }
    else if (h < 4) { r = 0; g = x; b = q; } else if (h < 5) { r = x; g = 0; b = q; } else { r = q; g = 0; b = x; }
    return { r + m, g + m, b + m };
}
inline HSL rgb2hsl (std::array<float, 3> c)
{
    const float r = c[0], g = c[1], b = c[2], mx = juce::jmax (r, g, b), mn = juce::jmin (r, g, b), l = (mx + mn) / 2, d = mx - mn;
    if (d < 1e-5f) return { 0, 0, l * 100 };
    const float s = d / (1 - std::abs (2 * l - 1) + 1e-9f);
    float h = mx == r ? std::fmod ((g - b) / d, 6.0f) : mx == g ? (b - r) / d + 2 : (r - g) / d + 4;
    return { std::fmod (h * 60 + 360, 360.0f), clampf (s * 100, 0, 100), l * 100 };
}
inline HSL mixrgb (HSL a, HSL b, float t)
{
    const auto A = hsl2rgb (a), B = hsl2rgb (b);
    return rgb2hsl ({ A[0] + (B[0] - A[0]) * t, A[1] + (B[1] - A[1]) * t, A[2] + (B[2] - A[2]) * t });
}
inline HSL mixc (HSL a, HSL b, float t) { return { a.h + (b.h - a.h) * t, a.s + (b.s - a.s) * t, a.l + (b.l - a.l) * t }; }
inline juce::Colour K (HSL c, float a = 1.0f)
{
    const auto rgb = hsl2rgb (c);
    return juce::Colour::fromFloatRGBA (rgb[0], rgb[1], rgb[2], clampf (a, 0, 1));
}
inline juce::Colour rgba (int r, int g, int b, float a) { return juce::Colour ((juce::uint8) r, (juce::uint8) g, (juce::uint8) b, clampf (a, 0, 1)); }

// ---- LIVE geometry (pure maths: polygons in, polygons out)
inline Poly smooth (const Poly& p, bool closed = false, int seg = 10)
{
    const int n = (int) p.size(); Poly o; if (n == 0) return o; o.reserve ((size_t) (n * seg + 1));
    auto g = [&] (int i) -> const V2& { return closed ? p[(size_t) (((i % n) + n) % n)] : p[(size_t) juce::jlimit (0, n - 1, i)]; };
    const int m = closed ? n : n - 1;
    for (int i = 0; i < m; ++i)
    {
        const V2 &a = g (i - 1), &b = g (i), &c = g (i + 1), &d = g (i + 2);
        for (int j = 0; j < seg; ++j)
        {
            const float t = (float) j / (float) seg, t2 = t * t, t3 = t2 * t;
            o.push_back ({ 0.5f * (2 * b.x + (-a.x + c.x) * t + (2 * a.x - 5 * b.x + 4 * c.x - d.x) * t2 + (-a.x + 3 * b.x - 3 * c.x + d.x) * t3),
                           0.5f * (2 * b.y + (-a.y + c.y) * t + (2 * a.y - 5 * b.y + 4 * c.y - d.y) * t2 + (-a.y + 3 * b.y - 3 * c.y + d.y) * t3) });
        }
    }
    if (! closed) o.push_back (p[(size_t) n - 1]);
    return o;
}
inline Poly wob (const Poly& p, float amp = 1, float sc = 0.03f, float sd = 0)
{
    Poly o; o.reserve (p.size());
    for (auto& q : p) o.push_back ({ q.x + (fbm (q.x * sc + sd, q.y * sc + sd * 1.7f, 3) - 0.5f) * 2.4f * amp, q.y + (fbm (q.x * sc + sd + 31, q.y * sc + sd + 57, 3) - 0.5f) * 2.4f * amp });
    return o;
}
// pressure line: an open polyline becomes a ribbon polygon whose width swells and thins
inline Poly ribbon (const Poly& p, float w, float sd = 1, float ta = 0.18f, float tb = 0.3f)
{
    const int n = (int) p.size(); if (n < 2) return {};
    std::vector<float> L (1, 0.0f); for (int i = 1; i < n; ++i) L.push_back (L[(size_t) i - 1] + hyp (p[(size_t) i].x - p[(size_t) i - 1].x, p[(size_t) i].y - p[(size_t) i - 1].y));
    const float T = L[(size_t) n - 1] != 0 ? L[(size_t) n - 1] : 1; Poly l, r;
    for (int i = 0; i < n; ++i)
    {
        const float t = L[(size_t) i] / T; const V2 &a = p[(size_t) juce::jmax (0, i - 1)], &b = p[(size_t) juce::jmin (n - 1, i + 1)];
        float dx = b.x - a.x, dy = b.y - a.y; const float d = hyp (dx, dy) != 0 ? hyp (dx, dy) : 1; dx /= d; dy /= d;
        float tp = juce::jmin (1.0f, ta > 0 ? t / ta : 1.0f, tb > 0 ? (1 - t) / tb : 1.0f); tp = std::pow (juce::jmax (tp, 0.0f), 0.7f);
        const float pr = 0.55f + 0.9f * fbm (L[(size_t) i] * 0.035f + sd, sd * 0.3f, 2), hw = juce::jmax (0.1f, w * 0.5f * tp * pr);
        l.push_back ({ p[(size_t) i].x - dy * hw, p[(size_t) i].y + dx * hw }); r.push_back ({ p[(size_t) i].x + dy * hw, p[(size_t) i].y - dx * hw });
    }
    for (int i = n - 1; i >= 0; --i) l.push_back (r[(size_t) i]);
    return l;
}
struct OutlineOpt { float lx = -0.6f, ly = -0.8f, gap = 0, wmin = 0.5f, wmax = 1.25f; std::optional<float> only; };
// outline of a closed shape as ribbons: heavier where it faces away from the light, may break on the lit side
inline std::vector<Poly> outlineRibbons (const Poly& p, float w, float sd = 1, OutlineOpt o = {})
{
    const int n = (int) p.size(); std::vector<Poly> out; if (n < 3) return out;
    float cx = 0, cy = 0; for (auto& q : p) { cx += q.x; cy += q.y; } cx /= (float) n; cy /= (float) n;
    std::vector<std::optional<V2>> l, r;
    for (int i = 0; i <= n; ++i)
    {
        const V2 &a = p[(size_t) ((i - 1 + n) % n)], &q = p[(size_t) (i % n)], &b = p[(size_t) ((i + 1) % n)];
        float dx = b.x - a.x, dy = b.y - a.y; const float d = hyp (dx, dy) != 0 ? hyp (dx, dy) : 1; dx /= d; dy /= d;
        float nx = dy, ny = -dx; if ((q.x - cx) * nx + (q.y - cy) * ny < 0) { nx = -nx; ny = -ny; }
        const float face = -(nx * o.lx + ny * o.ly), wt = o.wmin + (o.wmax - o.wmin) * clampf (face * 0.5f + 0.5f, 0, 1), pr = 0.7f + 0.6f * fbm ((float) i * 0.11f + sd, sd, 2), hw = juce::jmax (0.12f, w * 0.5f * wt * pr);
        if (o.gap > 0 && face < -0.5f && fbm ((float) i * 0.07f + sd * 2, 3, 2) < o.gap) { l.push_back ({}); r.push_back ({}); continue; }
        if (o.only.has_value() && face < *o.only) { l.push_back ({}); r.push_back ({}); continue; }
        l.push_back (V2 { q.x - dy * hw, q.y + dx * hw }); r.push_back (V2 { q.x + dy * hw, q.y - dx * hw });
    }
    size_t i = 0;
    while (i < l.size())
    {
        while (i < l.size() && ! l[i]) ++i;
        size_t j = i; while (j < l.size() && l[j]) ++j;
        if (j - i > 1) { Poly s; for (size_t k = i; k < j; ++k) s.push_back (*l[k]); for (size_t k = j; k-- > i;) s.push_back (*r[k]); out.push_back (std::move (s)); }
        i = j;
    }
    return out;
}
inline bool pip (const Poly& p, float x, float y)
{
    bool c = false; const size_t n = p.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++) { const V2 &a = p[i], &b = p[j]; if ((a.y > y) != (b.y > y) && x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x) c = ! c; }
    return c;
}
inline Poly normals (const Poly& p)
{
    const int n = (int) p.size(); Poly o; o.reserve ((size_t) n);
    float area = 0; for (int i = 0; i < n; ++i) { const V2 &a = p[(size_t) i], &b = p[(size_t) ((i + 1) % n)]; area += a.x * b.y - b.x * a.y; }
    const float sg = area > 0 ? 1.0f : -1.0f;
    for (int i = 0; i < n; ++i) { const V2 &a = p[(size_t) ((i - 1 + n) % n)], &b = p[(size_t) ((i + 1) % n)]; const float dx = b.x - a.x, dy = b.y - a.y, d = hyp (dx, dy) != 0 ? hyp (dx, dy) : 1; o.push_back ({ dy / d * sg, -dx / d * sg }); }
    return o;
}
inline V2 inset (const Poly& p, V2 q, float nx, float ny, float d)
{
    if (d <= 0) return q;
    for (int k = 0; k < 4; ++k) { const float x = q.x - nx * d, y = q.y - ny * d; if (pip (p, x - nx * d * 0.35f, y - ny * d * 0.35f)) return { x, y }; d *= 0.45f; }
    return { q.x - nx * d * 0.3f, q.y - ny * d * 0.3f };
}
struct LitOpt { float sx = 0.75f, sy = 0.66f, pw = 1, bias = 0; };
inline Poly litShape (const Poly& p, float ws, LitOpt o = {})
{
    const int n = (int) p.size(); Poly out; out.reserve ((size_t) n); const auto N = normals (p);
    for (int i = 0; i < n; ++i) { const float f = std::pow (clampf (N[(size_t) i].x * o.sx + N[(size_t) i].y * o.sy + o.bias, 0, 1), o.pw); out.push_back (inset (p, p[(size_t) i], N[(size_t) i].x, N[(size_t) i].y, ws * f)); }
    return out;
}
inline Poly rimShape (const Poly& p, float wd, float fx, float fy, float th = 0.3f)
{
    const int n = (int) p.size(); const auto N = normals (p); const float fl = hyp (fx, fy); fx /= fl; fy /= fl;
    std::vector<std::pair<V2, V2>> best, cur;
    for (int i = 0; i < n * 2; ++i)
    {
        const V2& q = p[(size_t) (i % n)]; const V2& nn = N[(size_t) (i % n)]; const float f = nn.x * fx + nn.y * fy;
        if (f > th) cur.push_back ({ q, inset (p, q, nn.x, nn.y, wd * juce::jmin (1.0f, (f - th) / (1 - th) * 1.6f)) });
        else { if (cur.size() > best.size()) best = cur; cur.clear(); if (i >= n) break; }
    }
    if (cur.size() > best.size()) best = cur;
    if (best.size() < 3) return {};
    if ((int) best.size() > n) best.resize ((size_t) n);
    Poly o; for (auto& e : best) o.push_back (e.first); for (size_t k = best.size(); k-- > 0;) o.push_back (best[k].second);
    return o;
}
inline Poly circ (float cx, float cy, float rx, float ry, int n = 18, float rot = 0)
{
    Poly o; o.reserve ((size_t) n); const float c = std::cos (rot), s = std::sin (rot);
    for (int i = 0; i < n; ++i) { const float a = (float) i / (float) n * TAU, x = std::cos (a) * rx, y = std::sin (a) * ry; o.push_back ({ cx + x * c - y * s, cy + x * s + y * c }); }
    return o;
}
inline Poly blobp (float cx, float cy, float rx, float ry, float irr, int n, float sd)
{
    Poly o; for (int i = 0; i < n; ++i) { const float a = (float) i / (float) n * TAU, k = 1 + (noise (std::cos (a) * 1.7f + sd, std::sin (a) * 1.7f + sd) - 0.5f) * 2 * irr; o.push_back ({ cx + std::cos (a) * rx * k, cy + std::sin (a) * ry * k }); }
    return smooth (o, true, 4);
}
inline Poly arcp (float cx, float cy, float rx, float ry, float a0, float a1, int n = 10)
{ Poly o; for (int i = 0; i <= n; ++i) { const float a = a0 + (a1 - a0) * (float) i / (float) n; o.push_back ({ cx + std::cos (a) * rx, cy + std::sin (a) * ry }); } return o; }
inline Poly linep (float x0, float y0, float x1, float y1, int n = 6)
{ Poly o; for (int i = 0; i <= n; ++i) o.push_back ({ x0 + (x1 - x0) * (float) i / (float) n, y0 + (y1 - y0) * (float) i / (float) n }); return o; }
template <typename F> Poly xf (const Poly& p, F fn) { Poly o; o.reserve (p.size()); for (auto& q : p) o.push_back (fn (q.x, q.y)); return o; }
inline Poly star4 (float x, float y, float r, float r2 = -1, float rot = 0)
{ if (r2 < 0) r2 = r * 0.22f; Poly o; for (int i = 0; i < 8; ++i) { const float a = rot + (float) i / 8 * TAU, d = (i % 2) ? r2 : r; o.push_back ({ x + std::cos (a) * d, y + std::sin (a) * d }); } return o; }
inline std::array<float, 4> bbox (const Poly& p)
{ float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f; for (auto& q : p) { x0 = juce::jmin (x0, q.x); x1 = juce::jmax (x1, q.x); y0 = juce::jmin (y0, q.y); y1 = juce::jmax (y1, q.y); } return { x0, y0, x1, y1 }; }
inline Poly rbox (float x, float y, float w, float h, float r = 3, int n = 3)
{
    Poly o; const float c[4][3] { { x + w - r, y + r, -PI / 2 }, { x + w - r, y + h - r, 0 }, { x + r, y + h - r, PI / 2 }, { x + r, y + r, PI } };
    for (auto& e : c) for (int i = 0; i <= n; ++i) { const float a = e[2] + (float) i / (float) n * PI / 2; o.push_back ({ e[0] + std::cos (a) * r, e[1] + std::sin (a) * r }); }
    return o;
}
inline Poly zig (float x0, float y0, float x1, float y1, int n, float amp)
{
    Poly o { { x0, y0 } }; const float dx = x1 - x0, dy = y1 - y0, l = hyp (dx, dy) != 0 ? hyp (dx, dy) : 1, nx = -dy / l, ny = dx / l;
    for (int i = 1; i < n; ++i) { const float t = (float) i / (float) n, s = (i % 2 ? 1.0f : -1.0f) * amp; o.push_back ({ x0 + dx * t + nx * s, y0 + dy * t + ny * s }); }
    o.push_back ({ x1, y1 }); return o;
}
inline Poly crackP (float x, float y, float an, float len, float sd, int kind = 0)
{
    const int n = kind == 1 ? 2 : 6; Poly o { { x, y } };
    for (int i = 1; i <= n; ++i)
    {
        const float j = kind == 1 ? 0.0f : (hsh (i, sd) - 0.5f) * (kind == 2 ? 1.5f : 0.8f) * (i % 2 ? 1.0f : -1.0f) * (kind == 2 ? 2.0f : 1.0f), a = an + j;
        o.push_back ({ o[(size_t) i - 1].x + std::cos (a) * len / (float) n, o[(size_t) i - 1].y + std::sin (a) * len / (float) n });
    }
    return o;
}
inline Poly concat (Poly a, const Poly& b) { a.insert (a.end(), b.begin(), b.end()); return a; }
inline Poly reversed (Poly a) { std::reverse (a.begin(), a.end()); return a; }

// ---------------------------------------------------------------- sprites: the designer's exported parts of a look
struct Sprite
{
    juce::Image im;               // the @2x strip as exported (frames side by side)
    float w = 0, h = 0, ax = 0, ay = 0;   // size and anchor in su
    int frames = 1, pw = 0, ph = 0;      // one frame in pixels
    float pxPerSu = 2, drawScale = 1; bool mask = false; std::vector<float> sizes;
};
// the parts of one look (owned by the look's registry entry, decoded on first use)
struct SpriteBank
{
    std::map<std::string, Sprite> sprites;
    const Sprite* get (const std::string& n) const { auto it = sprites.find (n); return it == sprites.end() ? nullptr : &it->second; }
};

// ---------------------------------------------------------------- G: the live drawing surface
enum class Look { dawn, cel, neon };
struct SpriteOpt { std::optional<float> size; int frame = 0; std::optional<juce::Colour> tint; float a = 1; };
struct StrokeOpt { bool closed = false; bool roundCap = true; std::vector<float> dash; };

class G
{
public:
    // k = device pixels per su; (ox, oy) = where su (0,0) lands, in the Graphics' own units times 'phys' (device px); phys = device px per unit
    G (juce::Graphics& gr, Look lk, const SpriteBank* bank, float kDev, float ox, float oy, float phys, std::map<std::string, juce::Image>* cache)
        : g (gr), look (lk), sprites (bank), k (kDev), ox_ (ox), oy_ (oy), ph (phys), imgCache (cache) {}
    juce::Graphics& g; Look look; const SpriteBank* sprites; float k, ox_, oy_, ph;
    bool isDawn() const { return look == Look::dawn; } bool isCel() const { return look == Look::cel; } bool isNeon() const { return look == Look::neon; }

    // su -> the Graphics' units
    float X (float x) const { return (x * k + ox_) / ph; }
    float Y (float y) const { return (y * k + oy_) / ph; }
    float S (float w) const { return w * k / ph; }
    juce::Path path (const Poly& p, bool closed) const
    {
        juce::Path pa; if (p.empty()) return pa; pa.preallocateSpace ((int) p.size() * 3 + 4);
        pa.startNewSubPath (X (p[0].x), Y (p[0].y)); for (size_t i = 1; i < p.size(); ++i) pa.lineTo (X (p[i].x), Y (p[i].y)); if (closed) pa.closeSubPath();
        return pa;
    }
    void fill (const Poly& p, juce::Colour c) { if (p.size() < 3 || c.isTransparent()) return; ++nFill; g.setColour (c); g.fillPath (path (p, true)); }
    void fillMany (const std::vector<Poly>& ps, juce::Colour c)
    {
        if (ps.empty() || c.isTransparent()) return; ++nFill; juce::Path pa;
        for (auto& p : ps) { if (p.size() < 3) continue; pa.startNewSubPath (X (p[0].x), Y (p[0].y)); for (size_t i = 1; i < p.size(); ++i) pa.lineTo (X (p[i].x), Y (p[i].y)); pa.closeSubPath(); }
        g.setColour (c); g.fillPath (pa);
    }
    void stroke (const Poly& p, float w, juce::Colour c, StrokeOpt o = {})
    {
        if (p.size() < 2 || c.isTransparent()) return; ++nStroke; g.setColour (c);
        // (a faint wide stroke - a neon halo - is joined with bevels: the same picture at a fraction of the cost of round joins)
        const bool faint = c.getFloatAlpha() < 0.3f && S (w) > 2.5f;
        juce::PathStrokeType st (juce::jmax (0.01f, S (w)), faint ? juce::PathStrokeType::beveled : juce::PathStrokeType::curved, o.roundCap ? juce::PathStrokeType::rounded : juce::PathStrokeType::butt);
        auto pa = path (p, o.closed);
        if (! o.dash.empty()) { std::vector<float> d; for (float v : o.dash) d.push_back (S (v)); juce::Path dashed; st.createDashedStroke (dashed, pa, d.data(), (int) d.size()); g.fillPath (dashed); return; }
        g.strokePath (pa, st);
    }
    void strokeMany (const std::vector<Poly>& ps, float w, juce::Colour c, bool roundCap = true)
    {
        if (ps.empty() || c.isTransparent()) return; ++nStroke; juce::Path pa;
        for (auto& p : ps) { if (p.size() < 2) continue; pa.startNewSubPath (X (p[0].x), Y (p[0].y)); for (size_t i = 1; i < p.size(); ++i) pa.lineTo (X (p[i].x), Y (p[i].y)); }
        g.setColour (c); g.strokePath (pa, juce::PathStrokeType (juce::jmax (0.01f, S (w)), juce::PathStrokeType::curved, roundCap ? juce::PathStrokeType::rounded : juce::PathStrokeType::butt));
    }
    void ell (float cx, float cy, float rx, float ry, juce::Colour c, float rot = 0)
    {
        if (c.isTransparent()) return; ++nFill; g.setColour (c); rx = juce::jmax (0.01f, rx); ry = juce::jmax (0.01f, ry);
        if (rot == 0) { g.fillEllipse (X (cx - rx), Y (cy - ry), S (rx * 2), S (ry * 2)); return; }
        juce::Path pa; pa.addEllipse (X (cx - rx), Y (cy - ry), S (rx * 2), S (ry * 2)); g.fillPath (pa, juce::AffineTransform::rotation (rot, X (cx), Y (cy)));
    }
    void ring (float cx, float cy, float rx, float ry, float w, juce::Colour c, float rot = 0)
    {
        if (c.isTransparent()) return; ++nStroke; g.setColour (c); juce::Path pa; pa.addEllipse (X (cx - juce::jmax (0.01f, rx)), Y (cy - juce::jmax (0.01f, ry)), S (juce::jmax (0.01f, rx) * 2), S (juce::jmax (0.01f, ry) * 2));
        g.strokePath (pa, juce::PathStrokeType (S (w)), rot == 0 ? juce::AffineTransform() : juce::AffineTransform::rotation (rot, X (cx), Y (cy)));
    }
    void arc (float cx, float cy, float rx, float ry, float a0, float a1, float w, juce::Colour c)
    { const int n = juce::jlimit (6, 48, (int) (std::abs (a1 - a0) / TAU * 48.0f)); stroke (arcp (cx, cy, juce::jmax (0.01f, rx), juce::jmax (0.01f, ry), a0, a1, n), w, c); }
    void rect (float x, float y, float w, float h, juce::Colour c) { if (c.isTransparent()) return; ++nFill; g.setColour (c); g.fillRect (juce::Rectangle<float> (X (x), Y (y), S (w), S (h))); }
    // small radial gradient blob: stops = { t, colour }; ry > 0 squashes it vertically
    void rgrad (float cx, float cy, float r, std::initializer_list<std::pair<float, juce::Colour>> stops, float ry = 0)
    {
        ++nGrad; juce::Graphics::ScopedSaveState ss (g); if (ry > 0) g.addTransform (juce::AffineTransform::scale (1.0f, ry / r, X (cx), Y (cy)));
        auto it = stops.begin(); const auto c0 = it->second; auto last = c0; float lt = it->first;
        juce::ColourGradient gr (c0, X (cx), Y (cy), (stops.end() - 1)->second, X (cx) + S (r), Y (cy), true);
        for (auto& s : stops) { if (s.first > 0 && s.first < 1) gr.addColour (s.first, s.second); last = s.second; lt = s.first; }
        if (lt < 1) gr.addColour (1.0, last);
        g.setGradientFill (gr); g.fillRect (juce::Rectangle<float> (X (cx - r), Y (cy - r), S (2 * r), S (2 * r)));
    }
    void lgrad (const Poly& p, float x0, float y0, float x1, float y1, std::initializer_list<std::pair<float, juce::Colour>> stops)
    {
        if (p.size() < 3) return; ++nGrad; juce::ColourGradient gr (stops.begin()->second, X (x0), Y (y0), (stops.end() - 1)->second, X (x1), Y (y1), false);
        for (auto& s : stops) if (s.first > 0 && s.first < 1) gr.addColour (s.first, s.second);
        g.setGradientFill (gr); g.fillPath (path (p, true));
    }
    template <typename Fn> void clip (const Poly& p, Fn fn) { ++nClip; juce::Graphics::ScopedSaveState ss (g); g.reduceClipRegion (path (p, true)); if (! g.isClipEmpty()) fn(); }

    // a sprite 1:1: its anchor on (x, y) su, snapped to whole device pixels; size = the nearest of its size steps; tint = a mask filled with a colour
    void sprite (const std::string& name, float x, float y, SpriteOpt o = {})
    {
        if (sprites == nullptr) return; const Sprite* s = sprites->get (name); if (s == nullptr || ! s->im.isValid()) return;
        float sc = 1; if (o.size.has_value() && ! s->sizes.empty()) { float bd = 1e9f; for (float z : s->sizes) if (std::abs (z - *o.size) < bd) { bd = std::abs (z - *o.size); sc = z; } }
        const float scale = k / s->pxPerSu * s->drawScale * sc;   // source px -> device px
        const int dw = juce::jmax (1, juce::roundToInt ((float) s->pw * scale)), dh = juce::jmax (1, juce::roundToInt ((float) s->ph * scale));
        const int fr = juce::jlimit (0, s->frames - 1, o.frame);
        const juce::String key = juce::String (name) + "#" + juce::String (fr) + "|" + juce::String (dw) + "x" + juce::String (dh);
        auto& img = sized (key, s->im, { fr * s->pw, 0, s->pw, s->ph }, dw, dh);
        const float px = std::round ((x - s->ax * sc) * k + ox_), py = std::round ((y - s->ay * sc) * k + oy_);
        ++nSprite; const juce::Rectangle<float> dst (px / ph, py / ph, (float) dw / ph, (float) dh / ph);
        if (o.tint.has_value())
        {   // a tinted copy per colour step (32 steps a channel), kept: drawing it is a plain copy with alpha
            const auto c = *o.tint; const juce::uint32 q = ((juce::uint32) (c.getRed() >> 3) << 10) | ((juce::uint32) (c.getGreen() >> 3) << 5) | (juce::uint32) (c.getBlue() >> 3);
            const juce::String tk = key + "~" + juce::String ((int) q);
            auto it = imgCache->find (tk.toStdString());
            if (it == imgCache->end())
            {
                if (imgCache->size() > 400) imgCache->clear();
                const auto tc = juce::Colour ((juce::uint8) (((q >> 10) & 31) * 255 / 31), (juce::uint8) (((q >> 5) & 31) * 255 / 31), (juce::uint8) ((q & 31) * 255 / 31));
                juce::Image t (juce::Image::ARGB, dw, dh, true, juce::SoftwareImageType());
                { const juce::Image::BitmapData src (sized (key, s->im, { fr * s->pw, 0, s->pw, s->ph }, dw, dh), juce::Image::BitmapData::readOnly); juce::Image::BitmapData d (t, juce::Image::BitmapData::writeOnly);
                  for (int y = 0; y < dh; ++y) for (int x = 0; x < dw; ++x) { const auto sp = src.getPixelColour (x, y); juce::PixelARGB px; px.setARGB (sp.getAlpha(), tc.getRed(), tc.getGreen(), tc.getBlue()); px.premultiply(); *(juce::PixelARGB*) d.getPixelPointer (x, y) = px; } }
                it = imgCache->emplace (tk.toStdString(), juce::NativeImageType().convert (t)).first;
            }
            g.setOpacity (clampf (c.getFloatAlpha() * o.a, 0, 1)); g.drawImageTransformed (it->second, juce::AffineTransform::scale (1.0f / ph).translated (dst.getX(), dst.getY())); g.setOpacity (1.0f);
        }
        else { g.setOpacity (clampf (o.a, 0, 1)); g.drawImageTransformed (img, juce::AffineTransform::scale (1.0f / ph).translated (dst.getX(), dst.getY())); g.setOpacity (1.0f); }
    }
    bool hasSprite (const std::string& name) const { return sprites != nullptr && sprites->get (name) != nullptr; }
    // a picture drawn once with live code (per key) and then blitted with alpha: the offscreen layer of a knob state
    struct Layer { juce::Image im; float ax = 0, ay = 0; };
    template <typename Fn> Layer layer (const juce::String& key, float w, float h, float ax, float ay, Fn fn)
    {
        const juce::String id = "L|" + key + "|" + juce::String (k, 3); auto it = imgCache->find (id.toStdString());
        if (it == imgCache->end())
        {
            if (imgCache->size() > 220) imgCache->clear();
            juce::Image im (juce::Image::ARGB, juce::jmax (1, (int) std::ceil (w * k)), juce::jmax (1, (int) std::ceil (h * k)), true, juce::SoftwareImageType());
            { juce::Graphics lg (im); G sub (lg, look, sprites, k, ax * k, ay * k, 1.0f, imgCache); fn (sub); }
            it = imgCache->emplace (id.toStdString(), juce::NativeImageType().convert (im)).first;
        }
        return { it->second, ax, ay };
    }
    void blit (const Layer& L, float x, float y, float a = 1)
    {
        ++nSprite; const float px = std::round ((x - L.ax) * k + ox_), py = std::round ((y - L.ay) * k + oy_);
        g.setOpacity (clampf (a, 0, 1)); g.drawImage (L.im, { px / ph, py / ph, (float) L.im.getWidth() / ph, (float) L.im.getHeight() / ph }, juce::RectanglePlacement::stretchToFit); g.setOpacity (1.0f);
    }
    int nFill = 0, nStroke = 0, nSprite = 0, nGrad = 0, nClip = 0;
    std::map<std::string, juce::Image>* imgCache;
private:
    const juce::Image& sized (const juce::String& key, const juce::Image& src, juce::Rectangle<int> from, int dw, int dh)
    {
        auto it = imgCache->find (key.toStdString()); if (it != imgCache->end()) return it->second;
        if (imgCache->size() > 400) imgCache->clear();
        juce::Image out (src.getFormat() == juce::Image::RGB ? juce::Image::RGB : juce::Image::ARGB, dw, dh, true, juce::SoftwareImageType());
        { juce::Graphics gg (out); gg.setImageResamplingQuality (juce::Graphics::highResamplingQuality); gg.drawImage (src, 0, 0, dw, dh, from.getX(), from.getY(), from.getWidth(), from.getHeight()); }
        return imgCache->emplace (key.toStdString(), juce::NativeImageType().convert (out)).first->second;
    }
};
} // namespace phantom::pv
