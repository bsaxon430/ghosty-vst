// SPDX-License-Identifier: AGPL-3.0-or-later
// CRYSTAL (port of phantom-anime/visuals/src/u_crystal.js): the ghost breathes frost toward a crystal that every knob recuts.
#pragma once
#include "Hands.h"

namespace phantom::pv
{
// one shard: a six-sided prism seen from the side (u along its length 0..1, v across -1..1)
struct ShardGeo
{
    float bx, by, dx, dy, L, w, tl = 0, round = 0, va = -0.06f, tex; bool rev, flat; int kind = 0;
    Poly out, lit, drk, fac, hi, root; std::vector<Poly> ridges; V2 tip; bool small = false, hasFac = false;
    V2 P (float u, float v) const { return { bx + dx * L * u - dy * w * 0.5f * v, by + dy * L * u + dx * w * 0.5f * v }; }
    float sh (float v) const
    {
        if (rev) return 1 - tl * (1 - v) * 0.5f - 0.02f;
        if (kind == 0) return 1 - tl * (1.15f - v * 0.85f);
        return 1 - tl * (0.84f + 0.16f * std::abs (v - va));
    }
    float tp (float u) const { return rev ? 0.36f + 0.64f * std::pow (u, 0.8f) : 1 - 0.07f * u; }
    V2 Q (float u, float v) const { return P (u, v * tp (u)); }
    V2 S (float v) const { return Q (sh (v), v); }
    float top (float v) const { return juce::jmin (sh (v), 0.98f); }
    Poly cap (float a, float b) const
    {
        if (flat) return { S (a), S (b) };
        if (round > 0) { Poly o; for (int i = 0; i <= 4; ++i) { const float v = lerp (a, b, (float) i / 4), e = std::pow (juce::jmax (0.0f, 1 - std::pow ((v - va) / 1.06f, 2.0f)), 0.5f + 0.5f * (1 - round)); o.push_back (Q (lerp (sh (v), 1, e * 0.92f), v)); } return o; }
        return { S (a), Q (1, va), S (b) };
    }
};
inline ShardGeo shardGeo (float bx, float by, float ang, float L, float w, float tex, bool rev, bool down)
{
    ShardGeo g; g.bx = bx; g.by = by; g.dx = std::sin (ang); g.dy = (down ? 1.0f : -1.0f) * std::cos (ang); g.L = L; g.w = w; g.tex = tex; g.rev = rev;
    const float v1 = -0.38f, v2 = 0.46f;
    if (rev) g.tl = 0.06f;
    else if (tex < 1.0f / 3) { g.kind = 0; g.tl = 0.1f + 0.05f * tex * 3; }
    else if (tex < 2.0f / 3) { g.kind = 1; g.tl = 0.2f + 0.2f * (tex - 1.0f / 3) * 3; }
    else { g.kind = 2; g.tl = 0.3f; g.round = clampf ((tex - 2.0f / 3) * 3, 0, 1); }
    g.flat = rev || tex < 1.0f / 3;
    g.out = concat (concat ({ g.Q (0, -1) }, g.cap (-1, 1)), { g.Q (0, 1) });
    g.lit = concat (concat ({ g.Q (0, -1) }, g.cap (-1, v1)), { g.Q (0, v1) });
    g.drk = concat (concat ({ g.Q (0, v2) }, g.cap (v2, 1)), { g.Q (0, 1) });
    if (g.flat) { g.fac = { g.S (-1), g.S (1), g.Q (g.sh (1) - 0.05f, 1), g.Q (g.sh (-1) - 0.05f, -1) }; g.hasFac = true; }
    else if (g.round <= 0) { g.fac = { g.S (v1), g.Q (1, g.va), g.S (v2) }; g.hasFac = true; }
    const float tipU = g.flat ? g.sh (g.va) : 1.0f; g.tip = g.Q (tipU, g.flat ? 0.2f : g.va);
    Poly r1 { g.Q (0, v1), g.S (v1) }, r2 { g.Q (0, v2), g.S (v2) }; if (! (g.flat || g.round > 0)) { r1.push_back (g.Q (1, g.va)); r2.push_back (g.Q (1, g.va)); }
    g.ridges = { r1, r2 };
    g.hi = { g.Q (0.1f, -0.02f), g.Q (g.sh (0) * 0.8f, -0.02f), g.Q (g.sh (0) * 0.78f, 0.2f), g.Q (0.13f, 0.2f) };
    g.root = { g.Q (0, -1), g.Q (0.3f, -1), g.Q (0.22f, v1), g.Q (0.34f, v2), g.Q (0.2f, 1), g.Q (0, 1) };
    g.small = w < 7;
    return g;
}
inline void drawShard (G& G, const ShardGeo& g, HSL c, float a, float sd, int hot)
{
    if (G.isCel())
    {
        G.fill (g.out, K (c, a)); G.fill (g.lit, K ({ c.h - 10, c.s - 18, juce::jmin (96.0f, c.l + 17) }, a)); G.fill (g.drk, K ({ c.h + 12, c.s + 6, c.l - 21 }, a));
        if (! g.small) { G.fill (g.root, K ({ c.h + 6, juce::jmin (100.0f, c.s + 14), c.l - 12 }, 0.5f * a)); if (g.hasFac) G.fill (g.fac, K ({ c.h - 6, c.s - 24, juce::jmin (98.0f, c.l + 24) }, 0.9f * a)); G.fill (g.hi, K ({ c.h, 30, 98 }, 0.8f * a)); }
        OutlineOpt oo; oo.gap = 0.04f; G.fillMany (outlineRibbons (g.out, juce::jmax (0.8f, juce::jmin (1.7f, g.w * 0.11f)), sd, oo), K (INKC, a)); G.strokeMany (g.ridges, g.small ? 0.6f : 0.85f, K (INKC, 0.72f * a));
    }
    else if (G.isDawn())
    {
        const auto o = wob (g.out, 0.5f, 0.08f, sd); G.fill (o, K (c, a)); G.fill (g.lit, K ({ c.h - 16, c.s - 18, juce::jmin (97.0f, c.l + 19) }, a)); G.fill (g.drk, K ({ c.h + 18, c.s + 2, c.l - 23 }, a));
        if (! g.small) { G.fill (g.root, K ({ c.h + 14, c.s + 8, c.l - 16 }, 0.45f * a)); if (g.hasFac) G.fill (g.fac, K ({ c.h - 10, c.s - 26, juce::jmin (98.0f, c.l + 25) }, 0.85f * a)); G.fill (g.hi, K ({ 52, 70, 97 }, 0.5f * a)); }
        Poly l (g.lit.begin(), g.lit.begin() + (g.lit.size() > 4 ? 3 : 2)); G.stroke (l, juce::jmax (0.8f, g.w * 0.07f), K ({ 48, 100, 90 }, 0.9f * a)); G.strokeMany (g.ridges, 0.7f, K ({ c.h + 14, c.s, c.l - 32 }, 0.45f * a));
    }
    else
    {
        G.fill (g.out, K (pal::neonGlass, 0.55f * a)); G.fill (g.out, K (c, 0.1f * a)); G.fill (g.lit, K (c, 0.17f * a)); if (g.hasFac && ! g.small) G.fill (g.fac, K ({ c.h, 60, 90 }, 0.2f * a));
        const float w = juce::jmax (0.7f, juce::jmin (1.3f, g.w * 0.07f)); StrokeOpt cl; cl.closed = true;
        if (hot) { G.stroke (g.out, w * 4.6f, K (c, 0.11f * a), cl); G.stroke (g.out, w * 2.4f, K (c, 0.24f * a), cl); }
        G.stroke (g.out, w, K (c, a), cl); G.stroke (g.out, w * 0.42f, K ({ c.h, 70, 92 }, a), cl); G.strokeMany (g.ridges, 0.7f, K (c, 0.75f * a));
    }
}

class CrystalScene final : public Scene
{
public:
    std::vector<std::string> controls() const override { return { "pitch", "splice", "delay", "fb", "reverse", "spread", "position", "density", "texture", "freeze", "mix" }; }
    std::vector<float> defaults() const override { return { 0.75f, 0.36f, 0.33f, 0.4f, 0.5f, 0.6f, 0, 0.5f, 0.5f, 0, 1 }; }
    struct Cl { int k; float x, y, sc, al, st, q; };
    struct Plan { float st; bool frozen, reg; int gens; float fan; std::vector<Cl> C; bool pure; };
    struct Sh { int i; bool down; float ang, L, w; bool rev; float bx, by; bool main, hero; float order; };
    struct V { float pitch, splice, delay, fb, reverse, spread, position, density, texture, freeze, mix; };
    static Plan plan (const V& v)
    {
        Plan p; p.st = -24 + 48 * v.pitch; p.frozen = v.freeze > 0.5f; const float d = v.density * 2 - 1; p.reg = d < 0; p.gens = 1 + juce::roundToInt (v.fb * 4);
        p.fan = (8 + 50 * v.spread) * PI / 180; const float g = 0.35f + 0.9f * v.delay; const float P0x = 128 + 38 * v.delay, P0y = 239, farx = 246, fary = 180;
        for (int k = 0; k < p.gens; ++k)
        {
            float q = 1 - 1 / (1 + (float) k * g * 0.6f); q = 1 - (1 - q) * (1 - 0.45f * v.position);
            p.C.push_back ({ k, lerp (P0x, farx, q), lerp (P0y, fary, q), 1 - 0.45f * q, std::pow (0.4f + 0.55f * v.fb, (float) k), clampf (p.st * (float) (k + 1), -48, 48), q });
        }
        p.pure = false; for (float pp : { 0.0f, 7.0f, 12.0f, 19.0f, 24.0f }) if (std::abs (std::abs (p.st) - pp) < 0.3f) p.pure = true;
        return p;
    }
    static HSL col (Look look, float st, bool rev, bool frozen)
    {
        const float u = clampf (st / 24, -2, 2); HSL c;
        c.s = u >= 0 ? 72 - 20 * juce::jmin (1.0f, u) : 56.0f;
        c.h = u >= 0 ? 194 + 4 * u : 214 - 36 * juce::jmax (-1.4f, u); c.l = u >= 0 ? 66 + 11 * juce::jmin (1.7f, u) : 60 + 13 * juce::jmax (-1.6f, u);
        if (look == Look::neon) c = u >= 0 ? HSL { 188, 100, 58 + 8 * juce::jmin (1.5f, u) } : HSL { 250 - 10 * u, 100, 64 };
        if (look == Look::dawn) c = { c.h - 10, c.s - 8, c.l };
        if (rev) c = look == Look::neon ? HSL { 322, 100, 62 } : mixrgb (c, { 334, 70, 76 }, 0.62f);
        if (frozen) c = look == Look::neon ? HSL { 196, 60, 84 } : mixrgb (c, { 200, 34, 92 }, 0.72f);
        return c;
    }
    template <typename Grow> static std::vector<Sh> shards (const V& v, const Plan& pl, const Cl& cl, Grow grow)
    {
        std::vector<Sh> out; const float kk = std::pow (0.66f, (float) cl.k), d = v.density * 2 - 1, sp = v.splice;
        const int Nf = juce::jmax (2, juce::roundToInt ((3 + 9 * std::pow (std::abs (d), 0.8f)) * (1 - 0.35f * sp) * kk)), Nr = juce::roundToInt (5 * v.reverse * (1 - 0.3f * sp) * kk);
        const float af = 0.55f + 0.45f * std::cos (v.reverse * PI / 2), ar = std::sin (v.reverse * PI / 2), up = sstep (-6, 0, cl.st), seedK = (float) cl.k * 31, far = std::pow (0.86f, (float) cl.k);
        auto Lf = [] (float s_) { return 0.5f + 0.5f * std::pow (juce::jmin (1.5f, std::abs (s_) / 24), 0.7f); };
        auto mk = [&] (bool down, float wt)
        {
            if (wt < 0.06f) return; const float st = cl.st, L0 = 170 * Lf (st) * (0.75f + 0.45f * sp) * cl.sc * far * wt;
            const float w0 = (11 + 16 * sp) * (1 - 0.3f * clampf (st / 24, 0, 1)) * (1 + 0.3f * clampf (-st / 24, 0, 1)) * cl.sc * far * (0.55f + 0.45f * wt), bx = cl.x, by = down ? -6.0f : cl.y;
            auto add = [&] (int i, int n, bool rev, float amp, float sd0)
            {
                const float nar = juce::jmin (1.0f, (float) n / 8 + 0.3f);
                float t = n > 1 ? (pl.reg || rev ? ((float) i / (float) (n - 1) * 2 - 1) * (rev ? 0.85f : 1.0f) * nar : clampf (hsg (i, seedK + sd0) * 0.9f, -1, 1) * nar) : (rev ? 0.3f : 0.0f);
                const bool hero = ! rev && i == n / 2; if (hero) t = 0.06f;
                const float tj = t + (rev ? 0.13f : 0.0f), ang = pl.fan * tj, env = 1 - 0.6f * std::pow (juce::jmin (1.0f, std::abs (tj) / nar), 1.2f) * nar;
                const float lk = hero ? 1.0f : env * (pl.reg || rev ? 0.92f : 0.7f + 0.34f * hsh (i, seedK + sd0 + 2)), gk = grow (i + (rev ? 50 : 0));
                const float L = L0 * lk * gk * amp * (rev ? 0.8f : 1.0f), w = juce::jmax (w0 * (hero ? 1.25f : 0.72f + 0.45f * hsh (i, seedK + sd0 + 4)) * (rev ? 1.05f : 1.0f), L / 9);
                Sh sh { i + (rev ? 50 : 0), down, ang, L, w, rev, bx + tj * w0 * (float) juce::jmin (n, 7) * 0.26f, by + (down ? -1.0f : 1.0f) * std::abs (tj) * 3, true, hero, L };
                out.push_back (sh);
                if (cl.k == 0 && ! rev && hsh (i, seedK + 13) < v.fb * 0.8f && L > 30)
                {
                    const float side = hero ? (hsh (i, seedK + 15) < 0.5f ? -1.0f : 1.0f) : (tj < 0 ? -1.0f : 1.0f), u = 0.36f + 0.26f * hsh (i, seedK + 17), ax = std::sin (ang), ay = (down ? 1.0f : -1.0f) * std::cos (ang);
                    out.push_back ({ i + 100, down, ang + side * 0.7f, L * 0.36f, w * 0.6f, false, sh.bx + ax * L * u, sh.by + ay * L * u, false, false, L + 1 });
                }
            };
            for (int i = 0; i < Nf; ++i) add (i, Nf, false, af, 3);
            for (int i = 0; i < Nr; ++i) add (i, Nr, true, ar, 23);
        };
        mk (false, std::sqrt (up)); mk (true, std::sqrt (1 - up));
        std::stable_sort (out.begin(), out.end(), [] (const Sh& a, const Sh& b) { return b.order < a.order; });
        return out;
    }
    void scene (G& G, const Feed& F, State& S, const Clock& T) override
    {
        const V v { F.v ("pitch"), F.v ("splice"), F.v ("delay"), F.v ("fb"), F.v ("reverse"), F.v ("spread"), F.v ("position"), F.v ("density"), F.v ("texture"), F.v ("freeze"), F.v ("mix") };
        const Look look = G.look; const bool on = F.on; const auto pl = plan (v); const bool frozen = pl.frozen; const float lvl = on && ! frozen ? F.level : 0, mix = mixA (v.mix);
        const int k = on && ! frozen ? T.tick - S.hitTick : -1; const float tex = v.texture;
        float tt; if (frozen) { auto it = S.keys.find ("frzT"); if (it == S.keys.end()) { S.keys["frzT"] = (float) T.t; tt = (float) T.t; } else tt = it->second; } else { S.keys.erase ("frzT"); tt = (float) T.t; }
        stageUnder (G);
        auto draw = [&] (pv::G& g, const Sh& sh, const Cl& cl, float a) { auto geo = shardGeo (sh.bx, sh.by, sh.ang, sh.L, sh.w, tex, sh.rev, sh.down); drawShard (g, geo, col (look, cl.st, sh.rev, frozen), a, (float) sh.i * 3.7f + (float) cl.k, cl.k == 0 && sh.main ? 1 : 0); return geo; };
        juce::String vkey; for (float x : { v.pitch, v.splice, v.delay, v.fb, v.reverse, v.spread, v.position, v.density, v.texture, v.freeze, v.mix }) vkey << juce::String (juce::roundToInt (x * 2000)) << ",";
        // far generations and the frost path: one picture per knob state
        const auto far = G.layer ("crystal-far|" + vkey, 280, 300, 0, 0, [&] (pv::G& g)
        {
            const auto& P0 = pl.C[0]; const int n = juce::roundToInt (4 + 10 * v.delay);
            for (int i = 0; i < n; ++i) { const float t = ((float) i + 0.5f) / (float) n, px = lerp (60, P0.x - 8, t), py = lerp (238, P0.y + 2, t) + std::sin ((float) i * 2.1f) * 1.6f, r = 1.2f + 1.2f * hsh (i, 3); g.fill ({ { px - r * 2, py }, { px, py - r }, { px + r * 2, py }, { px, py + r } }, K (look == Look::neon ? HSL { 186, 100, 70 } : HSL { 190, 60, 94 }, 0.9f)); }
            for (int ci = (int) pl.C.size() - 1; ci >= 1; --ci)
            {
                const auto& cl = pl.C[(size_t) ci]; if (cl.al < 0.05f) continue;
                glow (g, cl.x, cl.y, 0.6f, col (look, cl.st, false, frozen), 0.3f * cl.al, true);
                for (auto& sh : shards (v, pl, cl, [] (int) { return 1.0f; })) draw (g, sh, cl, (0.35f + 0.65f * cl.al) * mix);
            }
        });
        G.blit (far, 0, 0);
        // the main cluster re-grows on a hit (a third of its shards: 60 %, 80 %, then full length on three held ticks)
        const auto& cl0 = pl.C[0]; const int hitN = (int) S.hits.size();
        auto grow = [&] (int i) { if (k >= 0 && k < 3 && hsh (i, hitN + 40) < 0.34f) { static const float g3[3] { 0.6f, 0.8f, 1.0f }; return g3[k]; } return 1.0f; };
        const auto list = shards (v, pl, cl0, grow);
        {
            const HSL c = col (look, cl0.st, false, frozen); glow (G, cl0.x, cl0.y - 2, 1.5f, c, (0.32f + 0.4f * lvl) * mix, true);
            const float up = sstep (-6, 0, cl0.st);
            if (up > 0.2f) glow (G, cl0.x, cl0.y - 44 * cl0.sc, 2, c, (0.2f + 0.35f * lvl) * mix * up);
            if (up < 0.8f) glow (G, cl0.x, 30, 2, c, (0.2f + 0.35f * lvl) * mix * (1 - up));
        }
        std::vector<std::pair<Sh, ShardGeo>> geos; for (auto& sh : list) geos.push_back ({ sh, shardGeo (sh.bx, sh.by, sh.ang, sh.L, sh.w, tex, sh.rev, sh.down) });
        const auto main = G.layer ("crystal-main|" + vkey + (k >= 0 && k < 3 ? juce::String (k) + "/" + juce::String (hitN) : juce::String ("-")), 280, 300, 0, 0, [&] (pv::G& g)
        {
            for (auto& [sh, geo] : geos) drawShard (g, geo, col (look, cl0.st, sh.rev, frozen), mix, (float) sh.i * 3.7f, sh.main ? 1 : 0);
            const std::pair<Sh, ShardGeo>* big = nullptr; for (auto& e : geos) if (e.first.main && ! e.first.down && (big == nullptr || e.second.w * e.second.L > big->second.w * big->second.L)) big = &e;
            if (big != nullptr && big->second.w > 12 && mix > 0.5f)
            {
                const auto q = big->second.Q (0.4f, 0.04f); const float r = big->second.w * 0.085f;
                for (float dx : { -1.0f, 1.0f }) { g.ell (q.x + dx * r * 1.5f, q.y, r * 0.75f, r * 1.15f, look == Look::neon ? K ({ 186, 100, 80 }, 0.8f) : K (INKC, 0.55f)); if (look != Look::neon) g.ell (q.x + dx * r * 1.5f - r * 0.2f, q.y - r * 0.4f, r * 0.28f, r * 0.34f, K ({ 50, 90, 97 }, 0.8f)); }
            }
        });
        G.blit (main, 0, 0);
        if (look != Look::neon) G.sprite ("crystal/base", cl0.x, cl0.y);
        else { TubeOpt c; c.closed = true; tubeLine (G, circ (cl0.x, cl0.y, 34, 6, 18), 1.2f, { 186, 100, 58 }, c); c.halo = 0; tubeLine (G, circ (cl0.x, cl0.y, 24, 4, 14), 0.9f, { 268, 100, 68 }, c); }
        if (lvl > 0.02f)
            for (auto& [sh, geo] : geos)
            {
                if (! sh.main) continue; float h = std::fmod (tt * 1.5f + (float) sh.i * 0.37f, 1.0f); if (sh.rev) h = 1 - h;
                const float h1 = juce::jmin (geo.top (-1) * 0.97f, geo.top (1) * 0.97f, h + 0.13f); if (h1 <= h) continue;
                G.fill ({ geo.Q (h, -0.92f), geo.Q (h1, -0.92f), geo.Q (h1, 0.92f), geo.Q (h, 0.92f) }, K ({ 190, 60, 97 }, (0.18f + 0.6f * lvl) * mix));
            }
        for (auto& [sh, geo] : geos) { if (! sh.main) continue; const bool flash = k >= 0 && k < 2 && hsh (sh.i, hitN + 7) < 0.34f; if (pl.pure || flash) glint (G, geo.tip.x, geo.tip.y, flash ? 1.25f : 0.8f, { 190, 50, 97 }, flash ? 1.0f : 0.85f); }
        if (tex > 0.75f) { const int n = juce::roundToInt ((tex - 0.75f) * 40); const float ph = std::floor (tt * 6); for (int i = 0; i < n; ++i) { const float px = cl0.x + hsg (i, 61) * 70, py = cl0.y - 20 - hsh (i, 63) * 130, tw = frozen ? 0.8f : hsh (i, ph + 65); if (tw > 0.35f) glint (G, px, py, 0.5f, { 196, 50, 96 }, tw); } }
        // the ghost and its breath
        const auto hp = hitPose (k, 0, false); const int idleB = on && ! frozen ? (int) std::floor ((float) T.tick / 9) % 2 : 0;
        RigP P; P.s = 23; P.x = 46; P.y = 237; P.lean = 0.10f; P.tail = 0.2f; P.sx = 1 + (idleB ? 0.012f : 0.0f); P.sy = 1 - (idleB ? 0.014f : 0.0f); P.hem = idleB ? 0.5f : 0.0f;
        if (hp.on) { P.sx = hp.sx; P.sy = hp.sy; P.lift = hp.lift; P.hem = hp.hem; }
        const Rig R = ghostRig (P); GL L; L.slime = look == Look::neon ? HSL { 322, 100, 62 } : HSL { 318, 78, 68 };
        if (look != Look::neon) L.body = frozen ? HSL { 200, 40, 92 } : HSL { 330, 50, 96 };
        L.eyes = ! on ? "closed" : frozen ? "wide" : hp.on && hp.ph == "up" ? "wide" : "closed"; L.mouth = on ? "o" : "flat"; L.blush = frozen ? 0.0f : 0.6f; L.glow = look == Look::dawn ? 0.4f : 0.5f; L.tube = frozen ? HSL { 196, 70, 80 } : HSL { 322, 100, 62 }; L.k = 1;
        ghost (G, R, L, T);
        const auto m0 = R.X (R.mouth.x + 0.2f, R.mouth.y); const float tgx = cl0.x - 6, tgy = cl0.y - 30 * cl0.sc; const int nb = on ? 10 : 0;
        for (int i = 0; i < nb; ++i)
        {
            const float ph = frozen ? ((float) i + 0.5f) / (float) nb : std::fmod (tt * 0.9f + (float) i / (float) nb, 1.0f), e = std::pow (ph, 0.8f);
            const float px = lerp (m0.x, tgx, e) + std::sin ((float) i * 2.4f + ph * 6) * 4 * ph, py = lerp (m0.y, tgy, e) + std::cos ((float) i * 1.7f + ph * 5) * 5 * ph - 10 * std::sin (ph * PI), r = (0.8f + 2.6f * ph) * (1 - 0.5f * ph * ph), a = (1 - ph) * 0.9f;
            G.ell (px, py, r * 1.25f, r * 1.25f, K (look == Look::neon ? HSL { 186, 100, 76 } : HSL { 192, 60, 96 }, a)); if (i % 3 == 0 && ph > 0.25f) glint (G, px, py, 0.5f, { 192, 50, 97 }, a);
        }
        if (k >= 0 && k < 5 && on) puff (G, m0.x + 8 + (float) k * 3, m0.y - 2, k, 1, K (look == Look::neon ? HSL { 186, 90, 82 } : HSL { 194, 50, 96 }), 0.9f);
        if (frozen) { SpriteOpt so; so.tint = K (look == Look::neon ? HSL { 190, 80, 88 } : HSL { 196, 46, 96 }); so.a = look == Look::neon ? 0.6f : 0.9f; G.sprite ("crystal/frost", 0, 0, so); }
        if (! on) offVeil (G);
        stageOver (G);
    }
};
} // namespace phantom::pv
