// SPDX-License-Identifier: AGPL-3.0-or-later
// GRID (GHOST GRID; port of phantom-anime/visuals/src/u_grid.js): a turning hoop of lanterns hangs over the ghost, one lantern for
// every step of the bar, lit in the colours of the rows painted on that step. The lantern of the step that is playing hangs at the
// front, over the ghost's head; the ghost does the tricks of the rows in it. Twelve gems on the rim of its stand are the row knobs.
#pragma once
#include "Hands.h"

namespace phantom::pv
{
namespace gridpv
{
    inline const HSL COL[12] { { 188, 100, 64 }, { 313, 100, 65 }, { 146, 100, 62 }, { 38, 100, 68 }, { 353, 100, 68 }, { 261, 100, 71 }, { 25, 100, 68 }, { 198, 100, 75 }, { 279, 100, 86 }, { 82, 100, 62 }, { 146, 100, 62 }, { 227, 100, 78 } };
    inline const char* KEYS[12] { "loop", "stutter", "scratch", "reverse", "tape", "filter", "crush", "delay", "verb", "tonal", "gate", "freeze" };
    // the painter's stand-in patterns (row -> steps), when the plugin sends no rows
    inline std::array<juce::uint32, 12> pat (int i)
    {
        std::array<juce::uint32, 12> m {}; auto set = [&m] (int r, std::initializer_list<int> st) { for (int s : st) m[(size_t) r] |= 1u << s; };
        switch (i)
        {
            case 0: set (0, { 12, 13, 14, 15 }); set (5, { 8, 9, 10, 11, 12, 13, 14, 15 }); set (7, { 14 }); break;
            case 1: set (1, { 12, 13, 14, 15 }); set (3, { 7 }); set (8, { 15 }); break;
            case 2: set (2, { 2, 3, 10, 11 }); set (6, { 6, 14 }); set (10, { 4, 5 }); break;
            case 3: set (4, { 12, 13, 14, 15 }); set (7, { 15 }); break;
            case 4: set (3, { 4, 5, 6, 7, 12, 13, 14, 15 }); set (8, { 7 }); break;
            case 5: set (7, { 3, 7, 11, 15 }); set (8, { 15 }); set (5, { 0, 1, 2, 3 }); break;
            case 6: set (9, { 0, 1, 8, 9 }); set (6, { 4, 5, 12, 13 }); set (11, { 14, 15 }); break;
            default: set (0, { 6, 7 }); set (1, { 14, 15 }); set (2, { 3 }); set (3, { 10, 11 }); set (6, { 1, 5, 9 }); set (7, { 7 }); set (9, { 12, 13 }); set (5, { 8, 9, 10, 11 }); set (10, { 2 }); break;
        }
        return m;
    }
}
// an after-image of the ghost: its outline only, in a colour (a tube in NEON)
inline void ghostEcho (G& G, const Rig& R, HSL col, float a = 1, float w = 1.4f, float dx = 0, float dy = 0)
{
    Poly p = R.body; if (dx != 0 || dy != 0) for (auto& q : p) { q.x += dx; q.y += dy; }
    if (G.isNeon()) { G.stroke (p, w * 1.1f, K (col, a * 0.8f), { true }); G.stroke (p, w * 0.45f, K ({ col.h, 60, 92 }, a), { true }); }
    else { G.fill (p, K ({ col.h, col.s, juce::jmin (96.0f, col.l + 16) }, a * 0.3f)); G.stroke (p, w, K (col, a), { true }); }
}
// a small eighth note at (x, y)
inline void noteMark (G& G, float x, float y, float s, HSL col, float a = 1) { const auto c = K (col, a); G.ell (x, y, s * 0.5f, s * 0.36f, c, -0.4f); G.stroke ({ { x + s * 0.42f, y - s * 0.1f }, { x + s * 0.42f, y - s * 1.5f }, { x + s * 1.05f, y - s * 1.05f } }, juce::jmax (1.0f, s * 0.2f), c); }

class GridScene final : public Scene
{
public:
    std::vector<std::string> controls() const override { return { "pattern", "steps", "loop", "stutter", "scratch", "reverse", "tape", "filter", "crush", "delay", "verb", "tonal", "gate", "freeze", "seed", "mix" }; }
    std::vector<float> defaults() const override { return { 1, 0, 0.35f, 0.5f, 0.5f, 1, 0.4f, 0.6f, 0.55f, 0.5f, 0.6f, 0.5f, 0.5f, 1, 0, 1 }; }
    struct Now { bool on; int NS; std::array<juce::uint32, 12> rows; float pos; int step; std::array<bool, 12> act; std::array<float, 12> kv; float prog; bool bit (int r, int s) const { return s >= 0 && s < NS && ((rows[(size_t) r] >> s) & 1u) == 1u; } };
    static Now now (const Feed& F, const Clock& T)
    {
        Now n; n.on = F.on; const int pat = juce::jlimit (0, 7, juce::roundToInt (F.v ("pattern")) - 1); n.NS = F.v ("steps") > 0.5f ? 32 : 16;
        if (F.rows.size() == 12) for (int r = 0; r < 12; ++r) n.rows[(size_t) r] = (juce::uint32) F.rows[(size_t) r]; else n.rows = gridpv::pat (pat);
        n.pos = F.hasPos() && F.pos >= 0 ? F.pos : (float) std::fmod ((T.t * 120 / 60) / 4, 1.0); const float ps = n.pos * (float) n.NS; n.step = (int) std::floor (ps) % n.NS;
        for (int r = 0; r < 12; ++r) { n.act[(size_t) r] = n.on && n.bit (r, n.step); n.kv[(size_t) r] = F.v (gridpv::KEYS[r]); }
        int s0 = n.step, s1 = n.step; while (n.bit (4, s0 - 1)) --s0; while (n.bit (4, s1 + 1)) ++s1;
        n.prog = ((float) (n.step - s0) + ps - std::floor (ps)) / (float) (s1 - s0 + 1); return n;
    }
    static HSL lightOf (const Now& N, HSL plain, bool clampIt)
    {
        std::vector<int> rs; for (int r = 0; r < 12; ++r) if (N.act[(size_t) r]) rs.push_back (r); if (rs.empty()) return plain;
        HSL c = gridpv::COL[rs[0]]; for (size_t j = 1; j < rs.size(); ++j) c = mixrgb (c, gridpv::COL[rs[j]], 1.0f / (float) (j + 1));
        return clampIt ? HSL { c.h, juce::jmin (100.0f, c.s), juce::jmin (80.0f, c.l + 4) } : c;
    }
    struct Lz { int i; float a, d, sc, x, y; std::vector<int> list; bool cur, back; };
    void scene (G& G, const Feed& F, State& S, const Clock& T) override
    {
        const Look look = G.look; const auto N = now (F, T); const bool on = N.on; const int NS = N.NS, step = N.step; const auto& A = N.act; const auto& kv = N.kv;
        const float mixk = mixA (F.v ("mix")); const int sd = juce::roundToInt (F.v ("seed"));
        const float cx = 140, cy = 84, rx = 112, ry = 24; const HSL dk = L3<HSL> (look, { 262, 30, 14 }, INKC, { 262, 60, 30 }); const float bs = 3 + (float) boilOf (T, look == Look::dawn ? 4 : 3) * 1.7f; const bool frz = A[11];
        if (frz) { if (S.clocks.find ("gfz") == S.clocks.end()) S.clocks["gfz"] = T; } else S.clocks.erase ("gfz");
        const Clock Tp = frz ? S.clocks["gfz"] : T; const auto m = gmo (F, S, Tp); const float lvl = m.lvl;
        bool any = false; for (bool b : A) any = any || b; const HSL LC = lightOf (N, { 46, 90, 82 }, true);
        stageUnder (G);
        G.rect (0, 0, 280, 300, K (L3<HSL> (look, { 200, 60, 7 }, { 262, 50, 8 }, { 250, 60, 4 }), L3 (look, 0.44f, 0.4f, 0.2f) * mixk));
        glow (G, cx, cy + 26, 4.4f, LC, (0.1f + 0.14f * lvl) * mixk);
        const float ang0 = -(float) step / (float) NS * TAU; std::vector<Lz> lz;
        for (int i = 0; i < NS; ++i)
        {
            const float a = PI / 2 + (float) i / (float) NS * TAU + ang0, d = (std::sin (a) + 1) / 2; const bool cur = i == step && on;
            Lz q { i, a, d, lerp (0.56f, 1, d) * (NS == 32 ? 0.7f : 1.0f) * (cur ? 1.3f : 1.0f), cx + std::cos (a) * rx, cy + std::sin (a) * ry, {}, cur, std::sin (a) < -0.02f };
            for (int r = 0; r < 12; ++r) if (N.bit (r, i)) q.list.push_back (r); lz.push_back (q);
        }
        const HSL hoopC = L3<HSL> (look, { 30, 36, 40 }, { 42, 64, 56 }, { 188, 100, 64 }); std::vector<Poly> spokes;
        for (int k = 0; k < 4; ++k) { const float a = ang0 + (float) k * TAU / 4 + 0.4f; spokes.push_back ({ { cx, 34 }, { cx + std::cos (a) * rx, cy + std::sin (a) * ry } }); }
        auto hoop = [&] (float a0, float a1)
        {
            if (look == Look::neon) { TubeOpt to; to.halo = 0; to.k = 0.75f; tubeLine (G, arcp (cx, cy, rx, ry, a0, a1, 22), 1.3f, hoopC, to); }
            else { G.arc (cx, cy, rx, ry, a0, a1, 4.4f, K (dk)); G.arc (cx, cy, rx, ry, a0, a1, 2.6f, K (hoopC)); G.arc (cx, cy - 0.9f, rx, ry, a0, a1, 0.9f, K (L3<HSL> (look, { 46, 80, 74 }, { 50, 90, 82 }, {}), 0.8f)); }
        };
        G.stroke ({ { cx, 0 }, { cx, 34 } }, look == Look::neon ? 1.0f : 1.6f, K (look == Look::neon ? HSL { 188, 60, 50 } : dk)); G.strokeMany (spokes, look == Look::neon ? 0.8f : 1.1f, K (look == Look::neon ? HSL { 188, 70, 56 } : dk, 0.75f));
        auto lanterns = [&] (const std::vector<Lz>& set, bool back)
        {
            std::vector<Poly> cords, dim, caps, outl, band[12], hi;
            for (auto& q : set)
            {
                const float w = 15 * q.sc, h = 19 * q.sc, x = q.x, y0 = q.y + 5 * q.sc; auto hw = [w] (float u) { return w / 2 * (1 - 0.42f * clampf ((std::abs (u - 0.5f) - 0.3f) / 0.2f, 0, 1)); };
                const Poly body { { x - hw (0), y0 }, { x + hw (0), y0 }, { x + w / 2, y0 + h * 0.2f }, { x + w / 2, y0 + h * 0.8f }, { x + hw (1), y0 + h }, { x - hw (1), y0 + h }, { x - w / 2, y0 + h * 0.8f }, { x - w / 2, y0 + h * 0.2f } };
                cords.push_back ({ { x, q.y }, { x, y0 } }); caps.push_back ({ { x - w * 0.3f, y0 - 1.6f * q.sc }, { x + w * 0.3f, y0 - 1.6f * q.sc }, { x + w * 0.3f, y0 + 0.6f }, { x - w * 0.3f, y0 + 0.6f } });
                caps.push_back ({ { x - w * 0.26f, y0 + h - 0.6f }, { x + w * 0.26f, y0 + h - 0.6f }, { x + w * 0.2f, y0 + h + 2 * q.sc }, { x - w * 0.2f, y0 + h + 2 * q.sc } }); outl.push_back (body);
                if (q.list.empty()) { dim.push_back (body); continue; }
                const int n = (int) q.list.size();
                for (int j = 0; j < n; ++j) { const float u0 = (float) j / (float) n, u1 = (float) (j + 1) / (float) n, um = (u0 + u1) / 2; band[q.list[(size_t) j]].push_back ({ { x - hw (u0), y0 + h * u0 }, { x + hw (u0), y0 + h * u0 }, { x + hw (um), y0 + h * um }, { x + hw (u1), y0 + h * u1 }, { x - hw (u1), y0 + h * u1 }, { x - hw (um), y0 + h * um } }); }
                hi.push_back ({ { q.x - w * 0.3f, y0 + h * 0.16f }, { q.x - w * 0.12f, y0 + h * 0.12f }, { q.x - w * 0.12f, y0 + h * 0.86f }, { q.x - w * 0.3f, y0 + h * 0.8f } });
            }
            const float fa = back ? 0.62f : 1.0f; G.strokeMany (cords, look == Look::neon ? 0.7f : 1.0f, K (look == Look::neon ? HSL { 188, 60, 56 } : dk, 0.85f * fa));
            auto closedOf = [] (const std::vector<Poly>& ps) { std::vector<Poly> o; for (auto p : ps) { p.push_back (p[0]); o.push_back (p); } return o; };
            if (look == Look::neon)
            {
                G.fillMany (outl, K (pal::neonGlass, 0.8f));
                for (int r = 0; r < 12; ++r) if (! band[r].empty()) { G.fillMany (band[r], K (gridpv::COL[r], 0.5f * fa)); G.strokeMany (closedOf (band[r]), 0.9f, K ({ gridpv::COL[r].h, 90, 80 }, fa)); }
                G.strokeMany (closedOf (dim), 0.8f, K ({ 240, 40, 46 }, 0.8f * fa));
            }
            else
            {
                G.fillMany (dim, K (L3<HSL> (look, { 40, 22, 30 }, { 262, 18, 34 }, {}), 0.95f));
                for (int r = 0; r < 12; ++r) if (! band[r].empty()) G.fillMany (band[r], K (L3<HSL> (look, { gridpv::COL[r].h, 78, back ? 52.0f : 64.0f }, { gridpv::COL[r].h, 86, back ? 54.0f : 66.0f }, {})));
                if (! hi.empty()) G.fillMany (hi, K ({ 50, 90, 95 }, back ? 0.25f : 0.45f)); G.fillMany (caps, K (L3<HSL> (look, { 26, 34, 22 }, { 262, 26, 18 }, {})));
                if (look == Look::cel) { std::vector<Poly> rb; OutlineOpt oo; oo.gap = 0; for (size_t j = 0; j < outl.size(); ++j) for (auto& p : outlineRibbons (outl[j], 1.1f, bs + (float) j, oo)) rb.push_back (p); G.fillMany (rb, K (INKC)); }
                else { std::vector<Poly> sides; for (auto& p : outl) sides.push_back ({ p[1], p[2], p[3], p[4] }); G.strokeMany (sides, 1, K (dk, 0.5f * fa)); }
            }
        };
        hoop (PI, TAU);
        { std::vector<Lz> bk; for (auto& q : lz) if (q.back) bk.push_back (q); std::stable_sort (bk.begin(), bk.end(), [] (const Lz& p, const Lz& q) { return p.d < q.d; }); lanterns (bk, true); }
        hoop (0, PI);
        std::vector<Lz> front; for (auto& q : lz) if (! q.back) front.push_back (q); std::stable_sort (front.begin(), front.end(), [] (const Lz& p, const Lz& q) { return p.d < q.d; });
        for (size_t j = front.size() > 5 ? front.size() - 5 : 0; j < front.size(); ++j) { const auto& q = front[j]; if (! q.list.empty() && ! q.cur) glow (G, q.x, q.y + 15 * q.sc, 0.75f, gridpv::COL[q.list[0]], (0.3f + 0.2f * hsh (q.i, sd * 7 + (T.tick >> 2))) * mixk); }
        { std::vector<Lz> fr; for (auto& q : front) if (! q.cur) fr.push_back (q); lanterns (fr, false); }
        const auto& cur = lz[(size_t) step]; const float yb = cur.y + 5 * cur.sc + 19 * cur.sc + 2;
        if (on)
        {
            G.fill ({ { cx - 7, yb }, { cx + 7, yb }, { cx + 64, 262 }, { cx - 64, 262 } }, K (LC, (any ? 0.13f : 0.05f) * (0.6f + 0.6f * lvl) * mixk));
            G.fill ({ { cx - 4, yb }, { cx + 4, yb }, { cx + 34, 262 }, { cx - 34, 262 } }, K (mixrgb (LC, { 50, 60, 96 }, 0.4f), (any ? 0.1f : 0.04f) * (0.6f + 0.6f * lvl) * mixk));
            glow (G, cx, yb - 9, 1.5f, LC, (any ? 0.75f : 0.3f) * mixk); lanterns ({ cur }, false);
        }
        // the stand, and on its rim the twelve row knobs as gems
        const Poly top = circ (cx, 262, 62, 11, 22); const Poly wall = concat (arcp (cx, 262, 62, 11, 0, PI, 12), arcp (cx, 270, 62, 11, PI, 0, 12));
        if (look == Look::neon)
        {
            G.fill (wall, K (pal::neonGlass, 0.85f)); G.fill (top, K (pal::neonGlass, 0.7f)); G.fill (top, K (LC, 0.1f + 0.14f * lvl));
            { TubeOpt to; to.closed = true; to.halo = 0; tubeLine (G, top, 1.2f, { 188, 100, 64 }, to); } { TubeOpt to; to.halo = 0; tubeLine (G, arcp (cx, 270, 62, 11, 0, PI, 14), 0.9f, { 262, 90, 66 }, to); }
        }
        else
        {
            { SolidOpt so; so.iw = 1.5f; so.sd = bs + 5; so.shw = 2.4f; so.ink = false; solid (G, wall, MAT (look == Look::dawn ? "stone" : "iron"), so); }
            { SolidOpt so; so.iw = 1.5f; so.sd = bs + 6; so.shw = 3; solid (G, wob (top, 0.5f, 0.05f, bs), look == Look::dawn ? Mat { { 110, 30, 40 }, { 150, 30, 22 }, {}, HSL { 60, 70, 70 } } : MAT ("brass"), so); }
            glow (G, cx, 262, 1.5f, LC, (0.2f + 0.3f * lvl) * mixk, true);
        }
        // (the row knobs are crystals along the front of the stage, drawn after the ghost)
        // the ghost and the tricks of the rows painted on this step
        RigP P = m.P; P.s = 40; P.x = cx; P.y = 258 - lvl * 2; P.tail = 0.25f; float ga = 1, sl = 0;
        if (A[6]) { P.block = 0.22f + 0.6f * kv[6]; P.seg = 3; }
        if (A[4]) { const float q = clampf (N.prog * (0.5f + 1.6f * kv[4]), 0, 1); P.sy *= 1 - 0.42f * q; P.sx *= 1 + 0.22f * q; P.dripK = 1 + 1.5f * q; P.lift = 0; sl = q; }
        if (A[9]) { const float n = kv[9] * 2 - 1; P.sy *= 1 + 0.24f * n; P.sx *= 1 - 0.1f * n; }
        if (A[2]) { const float sg = T.tick % 2 ? 1.0f : -1.0f; P.lean = sg * (0.1f + 0.3f * kv[2]); P.x += sg * (3 + 9 * kv[2]); }
        if (A[1]) P.x += (T.f % 2 ? 1.0f : -1.0f) * (2 + 4 * kv[1]);
        if (A[10] && (int) std::floor ((float) T.f * (0.5f + 1.5f * kv[10])) % 2) ga = 0.22f;
        Rig R = ghostRig (P); if (A[3] && kv[3] > 0.02f) R = flipRig (R); const float gx = R.mid.x, gy = R.mid.y;
        if (A[8]) { glow (G, gx, gy, 2 + 1.4f * kv[8] > 2.6f ? 3.4f : 2.6f, gridpv::COL[8], (0.3f + 0.3f * kv[8]) * mixk); LightOpt lo; lo.closed = true; lightLine (G, circ (cx, 262, 40 + 50 * kv[8], 8 + 9 * kv[8], 20), 1.4f, gridpv::COL[8], 0.5f * mixk, lo); }
        if (A[7]) for (int j = 3; j >= 1; --j) ghostEcho (G, R, gridpv::COL[7], std::pow (0.3f + 0.6f * kv[7], (float) j) * mixk, 1.3f, 24.0f * (float) j, -9.0f * (float) j);
        if (A[0]) { const int n = 1 + juce::roundToInt (kv[0] * 3); for (int j = n; j >= 1; --j) ghostEcho (G, R, gridpv::COL[0], (0.75f - 0.15f * (float) j) * mixk, 1.3f, -5.0f * (float) j, -4.0f * (float) j); lightLine (G, arcp (cx, 262, 50, 9, (float) T.tick * 0.5f, (float) T.tick * 0.5f + 4.4f, 14), 1.5f, gridpv::COL[0], 0.8f * mixk); }
        if (A[1]) for (float d : { -1.0f, 1.0f }) ghostEcho (G, R, gridpv::COL[1], 0.55f * mixk, 1.2f, d * (8 + 10 * kv[1]), 0);
        glow (G, gx, gy, 1.5f, LC, (0.2f + 0.3f * lvl) * mixk * (on ? 1.0f : 0.4f));
        GL L; L.slime = HSL { 262, 68, 68 }; L.eyes = ! on ? "closed" : frz ? "wide" : sl > 0.55f ? "closed" : A[6] && kv[6] > 0.7f ? "x" : m.hp.on && ! m.hp.eyes.empty() ? m.hp.eyes : "open";
        L.mouth = ! on || sl > 0.3f ? "flat" : A[9] || A[8] ? "o" : "smile"; L.blush = 0.5f; L.glow = 0; L.a = ga; if (any) { L.tint = LC; L.tintA = 0.12f; }
        if (look == Look::neon) { L.tube = HSL { 188, 100, 64 }; L.slime = HSL { 262, 100, 68 }; L.tint.reset(); L.k = 0.9f + 0.1f * hsh (T.tick, 5); }
        ghost (G, R, L, Tp); if (ga > 0.5f) { WearOpt wo; wo.col = HSL { 262, 60, 58 }; wear (G, R, "phones", Tp, wo); }
        if (A[5])
        {
            const float y0 = R.top.y - 12, y1 = y0 + (18 + 86 * kv[5]) * (1 - std::abs (P.sy - 1)); const HSL c = gridpv::COL[5]; const float hw = R.halfW + 9;
            Poly pn { { gx - hw, y1 }, { gx - hw, y0 + 14 } }; for (auto q : arcp (gx, y0 + 14, hw, 14, PI, TAU, 10)) pn.push_back (q); pn.push_back ({ gx + hw, y0 + 14 }); pn.push_back ({ gx + hw, y1 });
            G.fill (pn, K (c, look == Look::neon ? 0.2f : 0.3f)); G.fill ({ { gx - hw * 0.78f, y0 + 12 }, { gx - hw * 0.5f, y0 + 5 }, { gx - hw * 0.62f, y1 - 2 }, { gx - hw * 0.86f, y1 - 2 } }, K ({ c.h, 60, 92 }, 0.22f));
            if (look == Look::neon) { TubeOpt to; to.halo = 0; tubeLine (G, pn, 1, c, to); } else G.stroke (pn, 1.2f, K ({ c.h, 70, 84 }, 0.85f));
            lightLine (G, { { gx - hw, y1 }, { gx + hw, y1 } }, 1.5f, c, 0.9f); G.ell (gx, y0 - 1, 3, 2.2f, K ({ c.h, 70, 84 }, 0.9f));
        }
        if (frz)
        {
            const Poly ice { { gx - 34, gy - 52 }, { gx + 12, gy - 58 }, { gx + 38, gy - 40 }, { gx + 42, gy + 38 }, { gx + 8, gy + 50 }, { gx - 38, gy + 42 }, { gx - 44, gy - 10 } }; const HSL c = gridpv::COL[11];
            G.fill (ice, K ({ c.h, 70, 88 }, 0.2f + 0.2f * kv[11])); G.fill ({ { gx - 34, gy - 52 }, { gx + 12, gy - 58 }, { gx - 2, gy - 30 }, { gx - 44, gy - 10 } }, K ({ 200, 60, 97 }, 0.2f));
            if (look == Look::neon) { TubeOpt to; to.closed = true; to.halo = 0; tubeLine (G, ice, 1, c, to); } else G.stroke (ice, 1.3f, K ({ c.h, 60, 92 }, 0.9f), { true });
            G.stroke ({ { gx + 12, gy - 58 }, { gx - 2, gy - 30 }, { gx + 8, gy + 50 } }, 0.9f, K ({ 200, 60, 96 }, 0.6f));
            static const float ix[3] { -30, 34, 2 }, iy[3] { -44, -30, 44 }; for (int i = 0; i < 3; ++i) if (hsh (i, T.tick >> 2) > 0.35f) glint (G, gx + ix[i], gy + iy[i], 0.8f, { 200, 60, 97 }, 0.9f);
        }
        if (A[9]) { const float n = kv[9] * 2 - 1; noteMark (G, gx + 24, R.top.y - 12 - n * 16, 6, gridpv::COL[9], 0.95f); if (look != Look::neon) noteMark (G, gx + 24.6f, R.top.y - 11.4f - n * 16, 6, dk, 0.3f); }
        if (A[2]) { std::vector<Poly> ls; for (float d : { -1.0f, 1.0f }) for (int j = 0; j < 3; ++j) ls.push_back ({ { gx + d * (34 + (float) j * 4), gy - 18 + (float) j * 16 }, { gx + d * (46 + 8 * kv[2] + (float) j * 4), gy - 18 + (float) j * 16 } }); G.strokeMany (ls, 1.3f, K (gridpv::COL[2], 0.85f)); }
        if (A[3] && kv[3] > 0.02f) { std::vector<Poly> ls; for (int j = 0; j < 3; ++j) { const float x = gx + 44 + (float) j * 9 - (float) (T.tick % 3) * 3; ls.push_back ({ { x + 4, gy - 7 }, { x - 2, gy }, { x + 4, gy + 7 } }); } G.strokeMany (ls, 1.5f, K (gridpv::COL[3], 0.9f)); }
        if (A[4] && sl > 0.2f) { std::vector<Poly> ls; for (int j = 0; j < 2; ++j) ls.push_back ({ { gx - 14 + (float) j * 28, R.top.y - 6 }, { gx - 14 + (float) j * 28, R.top.y - 6 - 10 * sl } }); G.strokeMany (ls, 1.2f, K (gridpv::COL[4], 0.8f)); }
        // the twelve row knobs: a row of crystals along the front of the stage, each as tall as its knob; the crystal of a row that plays glows and glints
        for (int r = 0; r < 12; ++r)
        {
            const float x = 27.5f + (float) r * 20.45f, k = kv[(size_t) r], h = 6 + 22 * k, yb = 295; const HSL c = gridpv::COL[r]; const bool lit = A[(size_t) r];
            const Poly sh { { x - 5.6f, yb }, { x - 5.6f, yb - h + 4.4f }, { x, yb - h }, { x + 5.6f, yb - h + 4.4f }, { x + 5.6f, yb } };
            if (look == Look::neon) { G.fill (sh, K (pal::neonGlass, 0.8f)); G.fill (sh, K (c, 0.2f + 0.3f * k + (lit ? 0.3f : 0.0f))); TubeOpt to; to.halo = 0; to.k = 0.5f + 0.5f * k; tubeLine (G, sh, 1, c, to); }
            else
            {
                G.fill (sh, K ({ c.h, c.s * (0.5f + 0.5f * k), L3 (look, 34.0f, 36.0f, 0.0f) + 22 * k + (lit ? 10.0f : 0.0f) })); G.fill ({ { x - 5.6f, yb }, { x - 5.6f, yb - h + 4.4f }, { x, yb - h }, { x - 1, yb } }, K ({ c.h, c.s * 0.7f, L3 (look, 24.0f, 26.0f, 0.0f) + 14 * k }, 0.9f));
                G.stroke ({ { x + 2.4f, yb - 3 }, { x + 2.4f, yb - h + 6 } }, 1, K ({ 50, 80, 95 }, 0.3f + 0.4f * k));
                if (look == Look::cel) { OutlineOpt oo; oo.gap = 0; G.fillMany (outlineRibbons (sh, 1.1f, bs + (float) r, oo), K (INKC)); } else G.stroke ({ sh[1], sh[2], sh[3] }, 1, K ({ c.h, 60, 88 }, 0.7f));
            }
            if (lit) { glow (G, x, yb - h, 0.5f, c, 0.85f * mixk); if (T.tick % 2 == 0) glint (G, x, yb - h, 0.5f, { c.h, 60, 94 }, 0.9f); }
        }
        if (! on) offVeil (G);
        stageOver (G);
    }
    // DAWN: light behind the step grid itself (22,330,1156,300; 1 su = 1 px)
    bool hasDawn() const override { return true; }
    bool dawnOver() const override { return true; }
    std::vector<std::string> dawnControls() const override { return { "pattern", "steps", "mix" }; }   // (the row knobs brighten only rows with painted cells)
    juce::Rectangle<float> dawnRect() const override { return { 22, 330, 1156, 300 }; }
    // the editor's own cells (design px, relative to the picture's rectangle), set by the plugin; else the painter's measured ones
    float cellX0 = 75, cellW = 930, cellY0 = 31, rowH = 21.2f;
    void dawnScene (G& G, const Feed& F, State&, const Clock& T, float, float) override
    {
        const auto N = now (F, T); const bool on = N.on; const int NS = N.NS, step = N.step; const float mixk = mixA (F.v ("mix")), x0 = cellX0, cw = cellW / (float) NS, y0 = cellY0, rh = rowH, lvl = on ? F.level : 0;
        const HSL LC = lightOf (N, { 46, 90, 74 }, false);
        if (on)
        {
            const float xc = x0 + ((float) step + 0.5f) * cw; G.rect (xc - cw / 2, y0 - 4, cw, rh * 12 + 8, K (LC, 0.1f * mixk));
            for (int j = 1; j <= 2; ++j) if (step - j >= 0) G.rect (xc - cw / 2 - (float) j * cw, y0 - 4, cw, rh * 12 + 8, K (LC, (0.05f - 0.02f * (float) j) * mixk));
            glow (G, xc, y0 + rh * 6, 3.4f, LC, (0.16f + 0.14f * lvl) * mixk); glow (G, xc, y0 + rh * 12, 1.5f, LC, (0.2f + 0.2f * lvl) * mixk, true);
            for (int i = 0; i < 9; ++i) { const float c = std::fmod (((float) T.f / 24) * (0.12f + 0.1f * hsh (i, 5)) + hsh (i, 7), 1.0f); G.ell (xc + (hsh (i, 9) - 0.5f) * cw * 0.8f + std::sin (c * 8 + (float) i) * 3, y0 + rh * 12 - c * rh * 12, 1.3f, 1.3f, K ({ 52, 100, 90 }, 0.6f * std::sin (c * PI) * mixk)); }
        }
        for (int r = 0; r < 12; ++r) for (int i = 0; i < NS; ++i) if (N.bit (r, i)) { const bool cu = on && i == step; glow (G, x0 + ((float) i + 0.5f) * cw, y0 + ((float) r + 0.5f) * rh, cu ? 1.0f : N.kv[(size_t) r] > 0.5f ? 0.75f : 0.5f, gridpv::COL[r], (cu ? 0.5f : 0.14f + 0.2f * N.kv[(size_t) r]) * mixk); }
    }
};
} // namespace phantom::pv
