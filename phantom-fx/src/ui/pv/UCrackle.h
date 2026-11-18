// SPDX-License-Identifier: AGPL-3.0-or-later
// CRACKLE (port of phantom-anime/visuals/src/u_crackle.js): a speaker cabinet fills the stage. One driver for every band, standing
// along a frequency rule where the crossovers put them; each cone is broken the way its band's curve breaks the sound, as far as its
// DRIVE; the cabinet itself is the BOX (ten of them); the cuts are shades drawn in from the low and the high side, behind the cones
// (pre) or in front of them (post). The ghost stands in front and takes the blast.
#pragma once
#include "UWarp.h"

namespace phantom::pv
{
namespace crkpv
{
    struct Geo { float a0, a1, cy, rm; };
    inline const Geo GEO[10] { { 34, 246, 118, 66 }, { 60, 220, 120, 52 }, { 30, 250, 116, 66 }, { 62, 218, 127, 50 }, { 36, 244, 112, 56 }, { 56, 224, 122, 54 }, { 40, 240, 134, 56 }, { 74, 242, 116, 56 }, { 42, 238, 100, 54 }, { 80, 200, 128, 44 } };
    inline Poly body (int b)
    {
        switch (b)
        {
            case 0: return rbox (18, 28, 244, 180, 9, 3); case 1: return rbox (46, 38, 188, 164, 18, 4); case 2: return rbox (12, 20, 256, 192, 6, 2); case 3: return rbox (34, 52, 212, 150, 58, 6);
            case 4: { Poly p { { 18, 208 }, { 18, 104 } }; for (auto q : arcp (140, 104, 122, 82, PI, TAU, 14)) p.push_back (q); p.push_back ({ 262, 104 }); p.push_back ({ 262, 208 }); return p; }
            case 5: return concat (arcp (140, 48, 96, 15, PI, TAU, 12), arcp (140, 194, 96, 15, 0, PI, 12));
            case 6: return { { 26, 58 }, { 254, 58 }, { 256, 208 }, { 24, 208 } }; case 7: return { { 32, 92 }, { 248, 26 }, { 256, 206 }, { 32, 146 } };
            case 8: return rbox (26, 24, 228, 152, 7, 3); default: return rbox (66, 40, 148, 168, 13, 3);
        }
    }
    inline Mat mat (int b, Look look)
    {
        const bool d = look == Look::dawn;
        switch (b)
        {
            case 0: return d ? Mat { { 30, 22, 30 }, { 262, 24, 15 }, {}, HSL { 46, 60, 70 } } : MAT ("dark");
            case 1: return d ? Mat { { 28, 30, 34 }, { 18, 34, 18 }, {}, HSL { 46, 70, 74 } } : MAT ("iron");
            case 2: return d ? Mat { { 262, 12, 24 }, { 262, 24, 11 }, {}, HSL { 46, 60, 70 } } : Mat { { 262, 14, 24 }, { 262, 26, 12 } };
            case 3: return d ? Mat { { 150, 20, 26 }, { 170, 26, 13 }, {}, HSL { 60, 60, 70 } } : Mat { { 162, 18, 30 }, { 180, 24, 16 } };
            case 4: return d ? Mat { { 24, 44, 36 }, { 14, 44, 20 }, {}, HSL { 46, 80, 76 } } : MAT ("wood");
            case 5: return d ? Mat { { 210, 10, 58 }, { 226, 16, 34 }, {}, HSL { 48, 60, 86 } } : MAT ("steel");
            case 6: return Mat { { 30, 44, 50 }, { 22, 44, 32 }, {}, HSL { 44, 70, 74 } };
            case 7: return d ? Mat { { 8, 56, 44 }, { 356, 54, 26 }, {}, HSL { 30, 90, 76 } } : Mat { { 4, 66, 50 }, { 352, 60, 30 }, {}, HSL { 30, 90, 78 } };
            case 8: return d ? Mat { { 230, 10, 40 }, { 240, 16, 22 }, {}, HSL { 200, 40, 80 } } : MAT ("iron");
            default: return d ? Mat { { 80, 16, 32 }, { 100, 20, 16 }, {}, HSL { 60, 60, 70 } } : Mat { { 84, 16, 34 }, { 110, 22, 18 } };
        }
    }
}

class CrackleScene final : public Scene
{
public:
    std::vector<std::string> controls() const override { return { "bands", "xlo", "xhi", "b1_curve", "b1_drive", "b1_mix", "b1_level", "b2_curve", "b2_drive", "b2_mix", "b2_level", "b3_curve", "b3_drive", "b3_mix", "b3_level", "prehp", "prelp", "posthp", "postlp", "box", "boxmix", "out", "mix" }; }
    std::vector<float> defaults() const override { return { 0, 0.436f, 0.572f, 0, 0.4f, 1, 0.667f, 4, 0.4f, 1, 0.667f, 12, 0.4f, 1, 0.667f, 0, 1, 0, 1, 0, 1, 0.667f, 1 }; }
    // one ring of a cone at radius q, bent by the curve c as far as d: the same rule for the rim, the ribs and the cap
    static Poly ring (float cx, float cy, float r, float q, int c, float d, float rot = 0)
    {
        const int n = c == 10 ? juce::jmax (4, juce::roundToInt (lerp (20, 4, d))) : 30; Poly o; const float st = 2 + 8 * d, cl = r * (1 - 0.55f * d);
        for (int i = 0; i < n; ++i)
        {
            const float a = rot + (float) i / (float) n * TAU; float rr = r * q;
            if (c == 7) rr *= 1 + 0.15f * d * (2 * std::abs (std::fmod (std::fmod (a * 8 / TAU, 1.0f) + 1, 1.0f) - 0.5f) * 2 - 1); if (c == 8) rr *= 1 + 0.13f * d * std::sin (a * 6);
            float x = cx + std::cos (a) * rr, y = cy + std::sin (a) * rr;
            if (c == 2) y = juce::jmax (y, cy - cl); if (c == 3) y = clampf (y, cy - cl, cy + cl); if (c == 5 || c == 6) y = juce::jmin (y, cy); if (c == 9) { x = std::round (x / st) * st; y = std::round (y / st) * st; } if (c == 13) y = cy + (y - cy) * (1 - 0.28f * d) + r * 0.14f * d;
            o.push_back ({ x, y });
        }
        return o;
    }
    static void cab (G& G, int b, Look look, float bm, float bs)
    {
        const Poly body = crkpv::body (b); const HSL dk = L3<HSL> (look, { 262, 30, 14 }, INKC, { 262, 60, 30 }); const bool neon = look == Look::neon; const Mat mt = crkpv::mat (b, look); const HSL tc { 14, 100, 62 }; const float a = 0.35f + 0.65f * bm;
        auto line = [&] (const Poly& p, float w, std::optional<HSL> col, float al = 1, bool cl = false) { if (neon) { TubeOpt to; to.halo = 0; to.a = al * a; to.closed = cl; to.k = 0.8f; tubeLine (G, p, juce::jmax (0.8f, w * 0.6f), col.value_or (tc), to); } else G.stroke (p, w, K (col.value_or (dk), al * a), { cl }); };
        auto tl = [&] (const Poly& p, float w, HSL col, bool cl = true) { TubeOpt to; to.closed = cl; to.halo = 0; to.a = a; tubeLine (G, p, w, col, to); };
        auto sol = [&] (const Poly& p, const Mat& m, float iw, float sd, float shw) { SolidOpt so; so.iw = iw; so.sd = sd; so.shw = shw; solid (G, p, m, so); };
        const HSL rim = mt.rim.value_or (dk);
        if (b == 3) { const auto hs = arcp (140, 58, 104, 40, PI * 1.03f, PI * 1.97f, 16); if (neon) tl (hs, 2.2f, tc, false); else { G.stroke (hs, 13, K (dk)); G.stroke (hs, 9.4f, K (mt.lit)); } for (float sx : { 36.0f, 244.0f }) { if (neon) tl (circ (sx, 62, 13, 10, 10), 1, tc); else sol (circ (sx, 62, 14, 11, 12), mt, 1.5f, bs + sx, 2.4f); } }
        if (b == 7) { const auto hd = rbox (64, 148, 22, 58, 5, 2); if (neon) tl (hd, 1, tc); else sol (hd, mt, 1.5f, bs + 4, 2.4f); }
        if (b == 9) line ({ { 88, 40 }, { 82, 2 } }, neon ? 2.0f : 5.0f, neon ? tc : mt.lit);
        if (b == 8) { const Poly base { { 10, 180 }, { 270, 180 }, { 258, 206 }, { 22, 206 } }; if (neon) { G.fill (base, K (pal::neonGlass, 0.8f)); tl (base, 1, tc); } else sol (base, mt, 1.5f, bs + 6, 2.4f); }
        if (neon) { G.fill (body, K (pal::neonGlass, 0.82f)); G.fill (body, K (tc, 0.05f)); TubeOpt to; to.closed = true; to.halo = 1; to.k = 0.5f + 0.4f * bm; tubeLine (G, body, 1.5f, tc, to); }
        else { SolidOpt so; so.iw = 2; so.sd = bs; so.shw = b == 5 ? 9.0f : 5.0f; so.rimw = 2; solid (G, wob (body, 0.6f, 0.04f, bs), mt, so); }
        if (b == 0) line (rbox (26, 36, 228, 164, 5, 2), 1, neon ? std::optional<HSL>() : rim, 0.35f, true);
        if (b == 2)
        {
            line (rbox (22, 30, 236, 172, 4, 2), 1.6f, neon ? std::optional<HSL>() : L3<HSL> (look, { 46, 60, 70 }, { 46, 70, 70 }, {}), 0.8f, true);
            for (auto c4 : std::vector<std::array<float, 4>> { { 12, 20, 1, 1 }, { 268, 20, -1, 1 }, { 12, 212, 1, -1 }, { 268, 212, -1, -1 } }) { const Poly c { { c4[0], c4[1] }, { c4[0] + c4[2] * 20, c4[1] }, { c4[0], c4[1] + c4[3] * 20 } }; if (neon) tl (c, 1, { 46, 100, 66 }); else sol (c, MAT ("steel"), 1.2f, bs + c4[0], 1.6f); }
        }
        if (b == 4)
        {
            const Poly d0 { { 44, 186 }, { 236, 186 }, { 236, 200 }, { 44, 200 } }; if (neon) { TubeOpt to; to.closed = true; to.halo = 0; to.a = a; tubeLine (G, d0, 0.9f, { 46, 100, 66 }, to); } else G.fill (d0, K ({ 46, 60, 84 }, a));
            std::vector<Poly> ts; for (int i = 0; i < 13; ++i) ts.push_back ({ { 52 + (float) i * 14.7f, 188 }, { 52 + (float) i * 14.7f, i % 3 ? 193.0f : 197.0f } }); G.strokeMany (ts, 1, K (neon ? HSL { 46, 100, 70 } : dk, 0.8f * a)); line ({ { 150, 184 }, { 150, 202 } }, 1.6f, HSL { 353, 90, 58 });
            for (float kx : { 28.0f, 252.0f }) { if (neon) tl (circ (kx, 194, 6, 6, 8), 0.9f, tc); else sol (circ (kx, 194, 6.4f, 6.4f, 10), MAT ("brass"), 1.2f, bs + kx, 1.6f); }
        }
        if (b == 5)
        {
            std::vector<Poly> ls; for (float y : { 76.0f, 104.0f, 140.0f, 168.0f }) ls.push_back (arcp (140, y, 96, 15, 0.06f * PI, 0.94f * PI, 12)); G.strokeMany (ls, 1.2f, K (neon ? tc : mt.shd, 0.8f * a));
            const auto rm = circ (140, 48, 96, 15, 24); if (neon) tl (rm, 1.2f, tc); else { G.fill (rm, K (mt.shd)); G.stroke (rm, 1.6f, K (rim, 0.9f), { true }); }
            line (smooth ({ { 236, 124 }, { 254, 112 }, { 266, 118 }, { 282, 100 } }, false, 4), 1.2f, neon ? HSL { 46, 100, 70 } : HSL { 46, 40, 84 });
        }
        if (b == 6)
        {
            for (const Poly f : { Poly { { 26, 58 }, { 6, 26 }, { 108, 30 }, { 124, 58 } }, Poly { { 254, 58 }, { 274, 28 }, { 172, 30 }, { 156, 58 } } }) { if (neon) tl (f, 1, tc); else { SolidOpt so; so.iw = 1.7f; so.sd = bs + f[0].x; so.shw = 3; solid (G, wob (f, 0.6f, 0.05f, bs + f[0].x), mt, so); } }
            if (neon) line ({ { 140, 58 }, { 140, 208 } }, 1, HSL { 46, 100, 66 }, 0.6f); else G.fill ({ { 130, 58 }, { 150, 58 }, { 151, 208 }, { 129, 208 } }, K ({ 44, 50, 70 }, 0.55f * a));
        }
        if (b == 7) { const auto mo = circ (252, 116, 9, 90, 16); if (neon) tl (mo, 1.2f, tc); else { G.fill (mo, K (mt.shd)); G.stroke (mo, 1.5f, K (rim, 0.9f), { true }); } }
        if (b == 8)
        {
            line (rbox (33, 31, 214, 138, 4, 2), 1, neon ? std::optional<HSL>() : rim, 0.5f, true); std::vector<Poly> ds; for (int i = 0; i < 11; ++i) ds.push_back (circ (90 + (float) i * 10, 188, 1.2f, 1.2f, 5)); G.fillMany (ds, K (neon ? tc : dk, 0.8f * a));
            G.strokeMany ({ { { 40, 194 }, { 240, 194 } }, { { 40, 200 }, { 240, 200 } } }, 1, K (neon ? tc : dk, 0.4f * a));
        }
        if (b == 9) { const auto bt = rbox (214, 84, 9, 30, 3, 2); if (neon) tl (bt, 0.9f, { 46, 100, 66 }); else sol (bt, MAT ("brass"), 1.2f, bs + 9, 1.6f); G.strokeMany ({ { { 84, 176 }, { 196, 176 } }, { { 84, 184 }, { 196, 184 } }, { { 84, 192 }, { 196, 192 } } }, 1.4f, K (neon ? tc : dk, 0.7f * a)); }
    }
    static void grille (G& G, int b, Look look, float bm)
    {
        if (bm < 0.03f) return; const auto c = look == Look::neon ? K ({ 14, 100, 70 }, 0.3f * bm) : K (look == Look::dawn ? HSL { 262, 30, 10 } : INKC, 0.3f * bm); const auto& g = crkpv::GEO[b];
        if (b == 1 || b == 3) { std::vector<Poly> ps; for (int j = 0; j < 7; ++j) for (int i = 0; i < 9; ++i) ps.push_back (circ (g.a0 + 8 + ((float) i + (float) (j % 2) * 0.5f) * (g.a1 - g.a0 - 16) / 9, g.cy - 48 + (float) j * 16, 1.5f, 1.5f, 5)); G.fillMany (ps, c); }
        if (b == 2) { std::vector<Poly> ls; for (int i = 0; i < 15; ++i) ls.push_back ({ { 28 + (float) i * 16, 34 }, { 28 + (float) i * 16, 198 } }); for (int i = 0; i < 10; ++i) ls.push_back ({ { 24, 40 + (float) i * 17 }, { 256, 40 + (float) i * 17 } }); G.strokeMany (ls, 0.8f, c); }
        if (b == 4 || b == 9) { std::vector<Poly> ls; for (int i = 0; i < 9; ++i) ls.push_back ({ { g.a0 + 2, g.cy - 48 + (float) i * 12 }, { g.a1 - 2, g.cy - 48 + (float) i * 12 } }); G.strokeMany (ls, 2, c); }
        if (b == 8) { std::vector<Poly> ls; for (int i = 0; i < 4; ++i) ls.push_back ({ { 40 + (float) i * 56, 34 }, { 62 + (float) i * 56, 166 } }); G.strokeMany (ls, 6, look == Look::neon ? K ({ 200, 60, 80 }, 0.06f * bm) : K ({ 200, 40, 90 }, 0.07f * bm)); }
    }
    struct Drv { int i; float cx, cy, r; int c; float d, mx, lev, L; HSL col; };
    void scene (G& G, const Feed& F, State& S, const Clock& T) override
    {
        const Look look = G.look; const auto m = gmo (F, S, T); const bool on = m.on; const float lvl = m.lvl; const int k = m.k; const float mixk = mixA (F.v ("mix"));
        const int nb = juce::jlimit (0, 2, juce::roundToInt (F.v ("bands"))) + 1, box = juce::jlimit (0, 9, juce::roundToInt (F.v ("box"))); const float bm = F.v ("boxmix");
        const HSL dk = L3<HSL> (look, { 262, 30, 14 }, INKC, { 262, 60, 30 }); const float bs = 3 + (float) boilOf (T, look == Look::dawn ? 4 : 3) * 1.7f; const auto& geo = crkpv::GEO[box];
        auto fx = [&] (float hz) { return geo.a0 + (geo.a1 - geo.a0) * clampf (std::log (hz / 20) / std::log (1000.0f), 0, 1); };
        const float X1 = fx (40 * std::pow (50.0f, F.v ("xlo"))), X2 = juce::jmax (X1 + 20, fx (400 * std::pow (30.0f, F.v ("xhi"))));
        std::vector<float> edges = nb == 1 ? std::vector<float> { geo.a0, geo.a1 } : nb == 2 ? std::vector<float> { geo.a0, X1, geo.a1 } : std::vector<float> { geo.a0, X1, X2, geo.a1 };
        const float lv[3] { m.low, m.mid, m.high }; const HSL bc[3] { { 8, 92, 58 }, { 26, 100, 60 }, { 46, 100, 66 } }; const float out = 0.3f + 0.9f * F.v ("out"); const int ph = on ? (int) std::floor ((float) T.tick / 2) : 0;
        std::vector<Drv> B; static const char* names[3][4] { { "b1_curve", "b1_drive", "b1_mix", "b1_level" }, { "b2_curve", "b2_drive", "b2_mix", "b2_level" }, { "b3_curve", "b3_drive", "b3_mix", "b3_level" } };
        for (int i = 0; i < nb; ++i)
        {
            const float xa = edges[(size_t) i], xb = edges[(size_t) i + 1]; const int c = juce::jlimit (0, 13, juce::roundToInt (F.v (names[i][0]))); const float d = F.v (names[i][1]) * mixk, L_ = nb == 1 ? lvl : nb == 2 ? (i == 0 ? lv[0] : juce::jmax (lv[1], lv[2])) : lv[i];
            static const float o3[3] { 8, -6, 10 }, o2[2] { 6, -4 }, rk[3] { 1, 0.8f, 0.62f };
            B.push_back ({ i, (xa + xb) / 2, geo.cy + (nb == 3 ? o3[i] : nb == 2 ? o2[i] : 0.0f), juce::jmax (9.0f, juce::jmin ((xb - xa) / 2 - 4, geo.rm * rk[nb == 1 ? 0 : i])), c, d, F.v (names[i][2]), 0.3f + 1.05f * F.v (names[i][3]), L_, bc[nb == 1 ? 1 : nb == 2 ? i * 2 : i] });
        }
        float avgD = 0; for (auto& q : B) avgD += q.d; avgD /= (float) nb; const HSL hot = mixrgb ({ 14, 100, 60 }, { 50, 100, 80 }, avgD);
        stageUnder (G);
        G.rect (0, 0, 280, 300, K (L3<HSL> (look, { 200, 60, 7 }, { 262, 50, 8 }, { 250, 60, 4 }), L3 (look, 0.34f, 0.3f, 0.14f)));
        glow (G, 140, geo.cy, 4.4f, { 14, 100, 60 }, (0.1f + 0.22f * avgD) * out * (0.6f + 0.5f * lvl) * mixk);
        G.ell (140, 214, geo.a1 - geo.a0 > 180 ? 118.0f : 86.0f, 9, K (L3<HSL> (look, { 200, 60, 6 }, { 262, 50, 7 }, { 250, 60, 3 }), 0.5f));
        cab (G, box, look, bm, bs);
        auto shade = [&] (float x0, float x1, float a, bool front)
        {
            if (x1 - x0 < 2) return; const Poly p { { x0, geo.cy - geo.rm - 8 }, { x1, geo.cy - geo.rm - 8 }, { x1, geo.cy + geo.rm + 8 }, { x0, geo.cy + geo.rm + 8 } }; G.fill (p, K (L3<HSL> (look, { 200, 60, 5 }, { 262, 50, 6 }, { 250, 60, 3 }), a));
            if (front) { const float xe = x0 < geo.a0 + 1 ? x1 : x0; G.strokeMany ({ { { xe, p[0].y }, { xe, p[2].y } } }, 1.2f, K (look == Look::neon ? HSL { 14, 100, 66 } : HSL { 46, 70, 80 }, 0.7f)); }
        };
        shade (geo.a0 - 4, fx (10 * std::pow (200.0f, F.v ("prehp"))), 0.5f, false); shade (fx (1000 * std::pow (22.0f, F.v ("prelp"))), geo.a1 + 4, 0.5f, false);
        for (auto& q : B)
        {
            const float cx = q.cx, cy = q.cy, d = q.d; const int c = q.c; const HSL col = q.col; const float pump = 1 + 0.07f * q.L + (on && k >= 0 && k < 2 ? 0.04f : 0.0f), r = q.r * pump, la = q.lev * out;
            const float dim = c == 13 ? 1 - 0.5f * d * (0.4f + 0.6f * clampf (q.L * 1.6f, 0, 1)) * (0.7f + 0.3f * hsh (T.tick, q.i)) : 1.0f, rot = c == 10 ? 0.4f : 0.0f;
            auto R_ = [&] (float qq) { return ring (cx, cy, r, qq, c, d, rot); }; const Poly rim = R_ (1), cone = R_ (0.86f), cap = R_ (0.25f + (c == 0 ? 0.08f * d : 0.0f)); const std::vector<Poly> ribs { R_ (0.66f), R_ (0.47f) };
            const HSL warm = c == 0 ? mixrgb (col, { 30, 100, 60 }, 0.5f) : c == 1 ? HSL { 28, 50, 44 } : col;
            auto closed = [] (std::vector<Poly> ps) { for (auto& p : ps) p.push_back (p[0]); return ps; };
            glow (G, cx, cy, clampf (r / 26, 0.5f, 2.6f), warm, (0.12f + 0.5f * d * (c == 0 ? 1.2f : 0.7f)) * la * dim * (0.5f + 0.6f * q.L) * mixk);
            if (look == Look::neon)
            {
                G.fill (rim, K (pal::neonGlass, 0.9f)); G.fill (cone, K (col, (0.08f + 0.14f * d) * dim)); { TubeOpt to; to.closed = true; to.halo = 1; to.k = (0.5f + 0.5f * la * 0.8f) * dim; to.heat = d * 0.5f; tubeLine (G, rim, 1.6f, col, to); }
                G.strokeMany (closed (ribs), 1, K (col, 0.75f * dim)); G.strokeMany (closed (ribs), 0.4f, K ({ col.h, 60, 92 }, 0.8f * dim)); { TubeOpt to; to.closed = true; to.halo = 0; to.k = dim; tubeLine (G, cap, 1.1f, { 50, 100, 80 }, to); }
            }
            else
            {
                G.fill (rim, K (dk)); G.fill (R_ (0.95f), K (L3<HSL> (look, { 30, 10, 22 }, { 214, 16, 58 }, {}))); G.fill (cone, K (mixrgb (L3<HSL> (look, { 30, 24, 30 }, { 258, 16, 28 }, {}), warm, (0.1f + 0.3f * d) * dim)));
                G.fill (ring (cx - r * 0.12f, cy - r * 0.14f, r, 0.6f, c, d, rot), K (mixrgb (L3<HSL> (look, { 34, 26, 40 }, { 256, 16, 38 }, {}), warm, (0.12f + 0.34f * d) * dim)));
                if (look == Look::cel) { std::vector<Poly> rb; OutlineOpt oo; oo.gap = 0.12f; auto ps = ribs; ps.push_back (cone); for (size_t j = 0; j < ps.size(); ++j) for (auto& p : outlineRibbons (ps[j], 1.1f, bs + (float) j + (float) q.i * 5, oo)) rb.push_back (p); G.fillMany (rb, K (INKC)); }
                else G.strokeMany (closed (ribs), 1.3f, K ({ 262, 26, 13 }, 0.7f));
                G.fill (cap, K (L3<HSL> (look, { 262, 22, 15 }, { 262, 26, 13 }, {}))); G.fill (ring (cx - r * 0.05f, cy - r * 0.06f, r, 0.13f, c, d, rot), K (mixrgb ({ 250, 16, 42 }, warm, 0.3f + 0.4f * d), 0.9f));
            }
            if (c == 2 || c == 3) { const float cl = r * (1 - 0.55f * d), hw = std::sqrt (juce::jmax (0.0f, r * r - cl * cl)); if (d > 0.05f) { lightLine (G, { { cx - hw, cy - cl }, { cx + hw, cy - cl } }, 1.6f, { 50, 100, 80 }, 0.9f * dim); if (c == 3) lightLine (G, { { cx - hw, cy + cl }, { cx + hw, cy + cl } }, 1.6f, { 50, 100, 80 }, 0.9f * dim); } }
            if (c == 4 && d > 0.03f) { std::vector<Poly> hp; for (int j = 0; j < 26; ++j) { const float a = (float) j / 26 * TAU, l = (2 + 11 * d) * (0.35f + 0.65f * hsh (j, ph + q.i * 7)), a2 = a + (hsh (j, 3) - 0.5f) * 0.5f; hp.push_back ({ { cx + std::cos (a) * r, cy + std::sin (a) * r }, { cx + std::cos (a2) * (r + l), cy + std::sin (a2) * (r + l) } }); } G.strokeMany (hp, 1.1f, K (look == Look::neon ? col : dk, 0.9f)); }
            if (c == 5) { std::vector<Poly> hs; for (float qq : { 0.76f, 0.56f, 0.37f }) { const auto p = R_ (qq); Poly h (p.begin() + (long) (p.size() / 2), p.end()); h.push_back (p[0]); hs.push_back (h); } G.strokeMany (hs, 1, K (look == Look::neon ? col : L3<HSL> (look, { 46, 70, 74 }, { 46, 60, 80 }, {}), 0.85f)); lightLine (G, { { cx - r, cy }, { cx + r, cy } }, 1.8f, { 50, 100, 80 }, 0.9f); }
            if (c == 6) { StrokeOpt so; so.dash = { 3, 3.4f }; G.stroke (arcp (cx, cy, r, r, 0, PI, 12), 1.1f, K (look == Look::neon ? HSL { 200, 80, 80 } : HSL { 50, 40, 92 }, 0.55f), so); lightLine (G, { { cx - r, cy }, { cx + r, cy } }, 1.3f, { 200, 60, 80 }, 0.8f); }
            if (c == 11 && d > 0.03f)
            {
                const float a0 = 0.5f + (float) q.i, w = 0.28f + 0.5f * d; const Poly fl { { cx, cy }, { cx + std::cos (a0) * r * 1.02f, cy + std::sin (a0) * r * 1.02f }, { cx + std::cos (a0 + w) * r * 1.02f, cy + std::sin (a0 + w) * r * 1.02f } };
                const HSL hole = L3<HSL> (look, { 262, 40, 5 }, { 262, 40, 6 }, { 250, 60, 3 }); G.fill (fl, K (hole, 0.94f)); G.stroke ({ fl[1], fl[0], fl[2] }, 1.2f, K (look == Look::neon ? col : HSL { 46, 60, 80 }, 0.8f));
                G.fill ({ { cx + std::cos (a0 + w) * r, cy + std::sin (a0 + w) * r }, { cx + std::cos (a0 + w * 0.6f) * r * 0.9f + 4, cy + std::sin (a0 + w * 0.6f) * r * 0.9f + 9 * d + 4 }, { cx + std::cos (a0 + w) * r * 0.5f, cy + std::sin (a0 + w) * r * 0.5f } }, K (L3<HSL> (look, { 30, 24, 36 }, { 258, 16, 34 }, col), look == Look::neon ? 0.4f : 1.0f));
                for (int j = 0; j < juce::roundToInt (d * 4); ++j) { const float a = 2.4f + (float) j * 1.3f + (float) q.i, rr = r * (0.4f + 0.3f * hsh (j, 5)); G.ell (cx + std::cos (a) * rr, cy + std::sin (a) * rr, r * 0.06f * (1 + hsh (j, 7)), r * 0.06f * (1 + hsh (j, 9)), K (hole, 0.94f)); }
            }
            if (c == 12 && d > 0.03f)
            {
                std::vector<Poly> cr; for (int j = 0; j < 2 + juce::roundToInt (d * 5); ++j) { const float a = hsh (j, q.i * 5 + 3) * TAU; cr.push_back (crackP (cx + std::cos (a) * r * 0.2f, cy + std::sin (a) * r * 0.2f, a + (hsh (j, 9) - 0.5f) * 0.6f, r * (0.5f + 0.45f * hsh (j, 11)) * (0.6f + 0.5f * d), j * 3 + q.i, 0)); }
                G.strokeMany (cr, look == Look::neon ? 1.2f : 2.0f, K (look == Look::neon ? HSL { 50, 100, 80 } : dk, 0.95f)); if (d > 0.4f) G.strokeMany (cr, 0.8f, K ({ 46, 100, 74 }, juce::jmin (1.0f, (d - 0.4f) * 2) * (0.5f + 0.5f * q.L)));
            }
            if (c == 13)
            {
                const float bx = cx - 9, by = cy - r - 15, fill = 1 - d; const HSL bc2 = look == Look::neon ? HSL { 50, 100, 74 } : HSL { 46, 40, 88 };
                G.stroke (rbox (bx, by, 18, 9, 2, 2), 1.2f, K (bc2, 0.9f), { true }); G.rect (bx + 18.6f, by + 2.6f, 2, 3.8f, K (bc2, 0.9f)); G.rect (bx + 2, by + 2, juce::jmax (1.5f, 14 * fill), 5, K (fill < 0.4f - 1e-5f ? HSL { 353, 90, 58 } : HSL { 110, 70, 56 }));
            }
            if ((c == 9 || c == 10) && d > 0.4f && look != Look::neon) { Poly p = rim; p.push_back (rim[0]); G.strokeMany ({ p }, 1, K ({ 46, 60, 80 }, 0.4f)); }
            if (q.mx < 0.98f) { StrokeOpt so; so.closed = true; so.dash = { 4, 3.4f }; G.stroke (circ (cx, cy, q.r, q.r, 24), 1.2f, K (look == Look::neon ? HSL { 200, 90, 84 } : HSL { 50, 50, 92 }, (1 - q.mx) * 0.8f + 0.1f), so); }
            if (on && d > 0.25f && q.L > 0.1f) { const int ns = juce::roundToInt (d * 3 * q.L) + (k >= 0 && k < 3 ? 3 : 0); for (int j = 0; j < ns; ++j) { const float a = hsh (j, ph * 3 + q.i) * TAU, rr = r * (0.5f + 0.6f * hsh (j, ph + 5)); glint (G, cx + std::cos (a) * rr, cy + std::sin (a) * rr, hsh (j, ph + 9) > 0.6f ? 0.8f : 0.5f, { 50, 100, 90 }, 0.9f); } }
            if (on && d > 0.3f && k >= 0 && k < 4) { CrumbOpt co; co.d0 = r * 0.9f; co.sp = 6; co.g = 0.5f; co.r = 1.7f; const HSL qc = q.col; crumbs (G, cx, cy, juce::roundToInt (3 + 5 * d), (float) (S.hitTick + q.i), (float) k, [&] (int j) { return K (j % 2 ? hot : qc, 0.95f); }, co); }
        }
        grille (G, box, look, bm);
        shade (geo.a0 - 4, fx (10 * std::pow (200.0f, F.v ("posthp"))), 0.56f, true); shade (fx (1000 * std::pow (22.0f, F.v ("postlp"))), geo.a1 + 4, 0.56f, true);
        // the walls between the bands (always marked; broken and dim while unused)
        { int i = 0; for (float x : { X1, X2 }) { const bool used = nb > i + 1; const HSL c = look == Look::neon ? HSL { 14, 100, 66 } : L3<HSL> (look, { 46, 60, 80 }, { 46, 50, 84 }, {}); StrokeOpt so; if (! used) so.dash = { 3, 4 };
            G.stroke ({ { x, geo.cy - geo.rm - 6 }, { x, geo.cy + geo.rm + 6 } }, used ? 1.4f : 1.2f, K (c, used ? 0.6f : 0.3f), so); G.fill ({ { x - 4.4f, 213 }, { x + 4.4f, 213 }, { x, 221 } }, K (look == Look::neon ? HSL { 14, 100, 66 } : HSL { 46, 80, 78 }, used ? 0.95f : 0.5f)); ++i; } }
        // the ghost in front: blown back by the blast, its edge torn as the drive goes up
        const float blast = on ? clampf (lvl * avgD * 1.6f, 0, 1) : 0.0f; RigP P = m.P; P.s = 27; P.x = 140 - blast * 3; P.y = 278 - lvl * 1.5f; P.tail = 0.25f; P.lean = -0.1f * blast; P.rough = clampf (avgD * 0.7f, 0, 1); P.rsd = lvl > 0.04f ? (float) ph : 0.0f; const Rig R = ghostRig (P);
        glow (G, R.mid.x, R.mid.y, 1, hot, (0.2f + 0.4f * avgD) * (0.5f + 0.5f * lvl) * mixk);
        GL L; L.slime = HSL { 20, 86, 62 }; L.eyes = ! on ? "closed" : avgD > 0.8f ? "x" : m.hp.on && ! m.hp.eyes.empty() ? m.hp.eyes : avgD > 0.45f ? "wide" : "open"; L.mouth = ! on ? "flat" : avgD > 0.45f && lvl > 0.3f ? "o" : "smile"; L.blush = 0.5f + 0.3f * avgD; L.glow = 0; L.tint = hot; L.tintA = 0.05f + 0.16f * avgD;
        if (look == Look::neon) { L.tube = HSL { 14, 100, 62 }; L.slime = HSL { 24, 100, 58 }; L.tint.reset(); L.heat = avgD * 0.5f; L.k = 0.9f + 0.1f * hsh (T.tick, 5); }
        ghost (G, R, L, T);
        if (on && blast > 0.2f) { std::vector<Poly> ls; for (int j = 0; j < 3; ++j) ls.push_back ({ { R.mid.x + 16 + (float) j * 5, R.mid.y - 14 + (float) j * 12 }, { R.mid.x + 24 + (float) j * 5 + 10 * blast, R.mid.y - 14 + (float) j * 12 } }); G.strokeMany (ls, 1.1f, K (look == Look::neon ? HSL { 14, 100, 70 } : L3<HSL> (look, { 50, 40, 90 }, { 46, 30, 90 }, {}), 0.7f * blast)); }
        if (! on) offVeil (G);
        stageOver (G);
    }
};
} // namespace phantom::pv
