// SPDX-License-Identifier: AGPL-3.0-or-later
// SQUEEZE and LIMIT (port of phantom-anime/visuals/src/u_squeeze.js, u_limit.js).
#pragma once
#include "Hands.h"

namespace phantom::pv
{
// SQUEEZE: three ghosts (low, mid, high). Under each a plate that lifts the quiet, over each a plate that presses the loud
class SqueezeScene final : public Scene
{
public:
    std::vector<std::string> controls() const override { return { "time", "up", "down", "in", "out", "lowgain", "midgain", "highgain", "xlo", "xhi", "mix" }; }
    std::vector<float> defaults() const override { return { 0.5f, 0.6f, 0.7f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 1 }; }
    void scene (G& G, const Feed& F, State& S, const Clock& T) override
    {
        const Look look = G.look; MoOpt mo; mo.noHit = true; const auto m = gmo (F, S, T, mo); const bool on = m.on; const float depth = F.v ("mix"); const HSL acc { 313, 100, 65 };
        const float X1 = 108 + (F.v ("xlo") - 0.5f) * 26, X2 = 194 + (F.v ("xhi") - 0.5f) * 26, walls[4] { 14, X1, X2, 266 }, inK = 0.35f + 1.3f * F.v ("in"), spd = lerp (0.9f, 0.1f, F.v ("time")), lv[3] { m.low, m.mid, m.high };
        const HSL slime[3] { { 20, 86, 62 }, { 318, 78, 68 }, { 188, 78, 62 } }, tubes[3] { { 24, 100, 58 }, { 322, 100, 62 }, { 190, 100, 60 } };
        const float gains[3] { F.v ("lowgain"), F.v ("midgain"), F.v ("highgain") }, floor = 260, rest = 36, up = F.v ("up"), down = F.v ("down"), out = F.v ("out");
        const Mat brass = MAT ("brass"), iron = MAT ("iron"); const HSL ink = look == Look::neon ? HSL { 262, 60, 62 } : look == Look::dawn ? HSL { 262, 30, 24 } : INKC;
        struct B { int b; float cx, base, fat, L_, quiet, press, Hs, pw, yF, yC, flat, yUp, yDn; }; std::vector<B> Bs;
        static const float bases[3] { 36, 36, 31 }, fats[3] { 0.15f, 0.04f, 0 };
        for (int b = 0; b < 3; ++b)
        {
            const float xa = walls[b], xb = walls[b + 1], cx = (xa + xb) / 2, base = bases[b] * (0.8f + 0.4f * gains[b]), fat = fats[b];
            const float L_ = on ? S.follow ("sq" + std::to_string (b), clampf (lv[b] * inK, 0, 1), juce::jmin (1.0f, spd * 1.6f), spd * 0.55f) : 0.0f, quiet = 1 - sstep (0.05f, 0.5f, L_), press = sstep (0.3f, 0.95f, L_);
            const float Hs = (2 + 1.4f * std::pow (fat, 1.1f)) * base, pw = juce::jmin (xb - xa - 8, base * 2.3f), dnF = down * depth, dn = dnF * press, lift = on ? up * quiet * depth * 50 : 0.0f;
            const float yF = floor - lift, top0 = yF - Hs, flat = sstep (0.18f, 1, dn) * 0.95f, yC = dn <= 0.001f ? rest : lerp (rest, top0, sstep (0, 0.18f, dn)) + 0.378f * flat * Hs;
            Bs.push_back ({ b, cx, base, fat, L_, quiet, press, Hs, pw, yF, yC, flat, floor - up * depth * 50, lerp (rest, floor - Hs, sstep (0, 0.18f, dnF)) + 0.378f * sstep (0.18f, 1, dnF) * 0.95f * Hs });
        }
        stageUnder (G);
        glow (G, 140, 264, 2.1f, { 313, 70, 70 }, 0.2f + 0.3f * out, true);
        for (float x : { X1, X2 }) { LineOpt lo; lo.a = look == Look::neon ? 0.8f : 0.55f; lo.ta = 0.02f; lo.tb = 0.02f; lo.sd = x; lo.halo = 0; LN (G, { { x, 14 }, { x, 292 } }, 1.6f, ink, lo); }
        for (int b = 0; b < 3; ++b) glyph (G, "LMH"[b], walls[b] + 9, 289, 8, look == Look::neon ? tubes[b] : HSL { 50, 30, 88 }, 1.2f, 0.8f);
        auto dash = [] (const B& q, float y, std::vector<Poly>& out) { for (int i = 0; i < 6; ++i) { const float x = q.cx - q.pw / 2 + q.pw * (float) i / 6; out.push_back ({ { x, y }, { x + q.pw / 11, y } }); } };
        if (up * depth > 0.03f) { std::vector<Poly> d; for (auto& q : Bs) dash (q, q.yUp, d); G.strokeMany (d, 1, K ({ 42, 100, 66 }, 0.55f)); }
        if (down * depth > 0.03f) { std::vector<Poly> d; for (auto& q : Bs) dash (q, q.yDn, d); G.strokeMany (d, 1, K (acc, 0.6f)); }
        { std::vector<Poly> rods; for (auto& q : Bs) { rods.push_back ({ { q.cx, 0 }, { q.cx, q.yC - 6 } }); rods.push_back ({ { q.cx, q.yF + 7 }, { q.cx, 300 } }); } G.strokeMany (rods, 3.4f, look == Look::neon ? K ({ 262, 60, 56 }, 0.9f) : K (look == Look::dawn ? HSL { 240, 16, 34 } : HSL { 244, 14, 30 }), false); }
        for (auto& q : Bs)
        {
            const int b = q.b; const auto cp = rbox (q.cx - q.pw / 2, q.yC - 7, q.pw, 7, 2, 2), fp = rbox (q.cx - q.pw / 2, q.yF, q.pw, 7, 2, 2);
            if (look == Look::neon) { StrokeOpt c; c.closed = true; G.fill (cp, K ({ 262, 60, 50 }, 0.45f + 0.3f * q.press)); G.stroke (cp, 1, K ({ 262, 80, 74 }), c); G.fill (fp, K ({ 42, 100, 56 }, 0.45f + 0.3f * q.quiet * up)); G.stroke (fp, 1, K ({ 44, 100, 68 }), c); }
            else { SolidOpt so; so.iw = 1.3f; so.sd = 3.0f + (float) b; so.shw = 1.6f; solid (G, cp, iron, so); so.sd = 6.0f + (float) b; solid (G, fp, brass, so); }
            G.fill ({ { q.cx - 4, q.yC - 5.4f }, { q.cx + 4, q.yC - 5.4f }, { q.cx, q.yC - 1.4f } }, K (acc, 0.25f + 0.75f * q.press * juce::jmin (1.0f, down * 2)));
            G.fill ({ { q.cx - 4, q.yF + 5.4f }, { q.cx + 4, q.yF + 5.4f }, { q.cx, q.yF + 1.4f } }, K ({ 50, 100, 90 }, 0.25f + 0.75f * q.quiet * juce::jmin (1.0f, up * 2)));
            MoOpt mo2; mo2.noHit = true; mo2.ph = b * 3; RigP P = gmo (F, S, T, mo2).P; P.s = q.base; P.x = q.cx; P.y = q.yF; P.fat = q.fat; P.flat = q.flat; P.tail = 0.2f; P.sd = 3.0f + (float) b * 2; P.hasDrips = true; P.drips = { { 0.05f, 0.4f } };
            const Rig R = ghostRig (P);
            glow (G, R.mid.x, R.mid.y, clampf (q.base / 30, 0.75f, 1.5f), look == Look::neon ? tubes[b] : HSL { 313, 70, 84 }, (0.16f + 0.26f * out) * (0.6f + 0.6f * q.L_) * (on ? 1.0f : 0.4f));
            GL Lg; Lg.slime = slime[b]; Lg.eyes = ! on ? "closed" : q.flat > 0.45f ? "squeeze" : q.quiet > 0.7f && up * depth > 0.25f ? "happy" : "open"; Lg.mouth = ! on || q.flat > 0.45f ? "flat" : q.L_ > 0.5f ? "o" : "smile"; Lg.blush = 0.5f; Lg.glow = 0; Lg.tex = 0; Lg.lite = true;
            if (look == Look::neon) { Lg.tube = tubes[b]; Lg.slime = tubes[(b + 1) % 3]; Lg.k = 0.9f + 0.1f * hsh (T.tick, 5 + b); }
            ghost (G, R, Lg, T); if (q.flat < 0.08f) { WearOpt wo; wo.lite = true; wear (G, R, "horns", T, wo); }
        }
        if (! on) offVeil (G);
        stageOver (G);
    }
};

// LIMIT: one hard ceiling. The ghost jumps with the sound and is flattened where it would go through
class LimitScene final : public Scene
{
public:
    std::vector<std::string> controls() const override { return { "ceiling", "gain", "release", "adaptive", "lookahead", "truepeak", "quality" }; }
    std::vector<float> defaults() const override { return { 0.975f, 0, 0.4f, 1, 0.16f, 1, 1 }; }
    static void bar (G& G, float yb, const Clock& T, bool hot)
    {
        const Look look = G.look; const float sd = 3 + (float) boilOf (T, look == Look::dawn ? 4 : 3) * 2.3f;
        if (look == Look::cel)
        {
            StrokeOpt d; d.dash = { 5, 3 }; G.stroke ({ { 40, 0 }, { 40, yb - 13 } }, 2.4f, K (INKC), d); G.stroke ({ { 240, 0 }, { 240, yb - 13 } }, 2.4f, K (INKC), d);
            SolidOpt so; so.iw = 1.7f; so.sd = sd; so.shw = 2.4f; solid (G, wob (rbox (8, yb - 14, 264, 14, 2, 2), 0.35f, 0.05f, sd), MAT ("steel"), so);
            std::vector<Poly> st; for (int i = 0; i < 11; ++i) { const float x = 20 + (float) i * 23; st.push_back ({ { x, yb - 1.5f }, { x + 8, yb - 1.5f }, { x + 14, yb - 7 }, { x + 6, yb - 7 } }); } G.fillMany (st, K ({ 46, 95, 58 }));
            std::vector<Poly> rv; for (int i = 0; i < 6; ++i) rv.push_back (circ (22 + (float) i * 47, yb - 10.4f, 1.3f, 1.3f, 6)); G.fillMany (rv, K (INKC));
        }
        else if (look == Look::dawn)
        {
            const auto p = wob (smooth ({ { 4, yb - 15 }, { 60, yb - 18 }, { 150, yb - 16 }, { 230, yb - 19 }, { 276, yb - 15 }, { 276, yb - 1 }, { 200, yb }, { 90, yb + 0.6f }, { 4, yb - 1 } }, true, 3), 0.8f, 0.04f, sd);
            SolidOpt so; so.iw = 1.6f; so.sd = sd; so.shw = 4.5f; so.rimw = 1.8f; solid (G, p, { { 204, 12, 58 }, { 232, 20, 30 }, {}, HSL { 50, 80, 86 } }, so);
            const float ms[4][4] { { 38, 13, 110, 46 }, { 96, 9, 86, 52 }, { 176, 15, 124, 44 }, { 236, 10, 96, 50 } }; const float ml[4] { 34, 42, 30, 40 };
            for (int i = 0; i < 4; ++i) G.fill (blobp (ms[i][0], yb - 17, ms[i][1], 4.6f, 0.25f, 9, ms[i][0]), K ({ ms[i][2], ms[i][3], ml[i] }));
            G.strokeMany ({ { { 64, yb - 1 }, { 63, yb + 9 } }, { { 212, yb - 1 }, { 214, yb + 12 } }, { { 150, yb }, { 150, yb + 6 } } }, 1.2f, K ({ 110, 40, 30 }, 0.8f));
        }
        else
        {
            G.fill (rbox (8, yb - 11, 264, 8, 2, 2), K (pal::neonGlass, 0.8f)); G.fillMany ({ rbox (4, yb - 13, 12, 14, 2, 2), rbox (264, yb - 13, 12, 14, 2, 2) }, K ({ 200, 40, 24 }));
            TubeOpt to; to.heat = hot ? 0.6f : 0.0f; tubeLine (G, { { 14, yb - 1.2f }, { 266, yb - 1.2f } }, 2, { 190, 60, 88 }, to);
        }
    }
    void scene (G& G, const Feed& F, State& S, const Clock& T) override
    {
        const Look look = G.look; const auto m = gmo (F, S, T); const bool on = m.on; const float lvl = m.lvl; const int k = m.k; const float gain = F.v ("gain"); const bool tp = F.v ("truepeak") > 0.5f; const int q = juce::jlimit (0, 5, juce::roundToInt (F.v ("quality")));
        const HSL acc { 200, 14, 95 }, red { 352, 100, 62 }; const float s = 42, floorY = 250, Hs = 2 * s, yb = 62 + (1 - F.v ("ceiling")) * 72, yLim = yb + (tp ? 5.4f : 4.6f), reach = 44 + 104 * gain;
        const float la = F.v ("lookahead"), padRest = yb + 6 + 30 * la + (on && k >= 0 && k < 2 ? 3 + 7 * la : 0.0f);
        static const float Js[11] { 0, 0, 0.78f, 1, 0.92f, 0.72f, 0.5f, 0.34f, 0.2f, 0.1f, 0.04f };
        const float sy = m.P.sy, J = k >= 0 && k < 11 ? Js[k] : 0.0f, want = on ? reach * juce::jmax (J, lvl * 0.6f) : 0.0f, over = juce::jmax (0.0f, yLim - (floorY - Hs * sy - want)), flatT = clampf (over / (0.378f * Hs), 0, 1);
        const bool adaptive = F.v ("adaptive") > 0.5f; const float relK = lerp (0.75f, 0.07f, F.v ("release")) * (adaptive ? 0.8f : 1.0f);
        const float flat = on ? S.follow ("lf", flatT, 1, relK) : 0.0f, lift = want - juce::jmax (0.0f, over - 0.378f * Hs * flatT), wob2 = adaptive && on && k >= 5 && k < 12 ? std::sin ((float) (k - 5) * 1.6f) * 0.03f * std::pow (0.7f, (float) (k - 5)) : 0.0f;
        stageUnder (G);
        glow (G, 140, 254, 2.1f, { 262, 50, 74 }, 0.3f, true);
        RigP P1; P1.s = s; P1.x = 140; P1.y = floorY - reach; const Rig R1 = ghostRig (P1); Poly dome;
        for (int i = 10; i >= 0; --i) { const float u = (float) i / 10 * 0.46f; dome.push_back (R1.X (R1.cAt (u) - R1.wAt (u, -1), R1.yAt (u))); }
        for (int i = 1; i <= 10; ++i) { const float u = (float) i / 10 * 0.46f; dome.push_back (R1.X (R1.cAt (u) + R1.wAt (u, 1), R1.yAt (u))); }
        Poly ov; for (auto& p : dome) if (p.y < yLim) ov.push_back (p);
        if (ov.size() > 2) G.fill (concat (concat ({ { ov[0].x, yLim } }, ov), { { ov.back().x, yLim } }), K (red, 0.4f));
        if (reach > 50) { StrokeOpt d; d.dash = { 4, 3 }; G.stroke (dome, 1.2f, K (acc, 0.55f), d); }
        G.ell (140, floorY + 3, 34 - lift * 0.12f, 6 - lift * 0.02f, K ({ 262, 50, 8 }, 0.4f - lift * 0.0018f));
        RigP P = m.P; P.s = s; P.x = 140; P.y = floorY - lift; P.tail = 0.25f + lift * 0.004f; P.flat = flat; P.lift = 0; P.sx = m.P.sx + wob2; P.sy = m.P.sy - wob2; const Rig R = ghostRig (P);
        glow (G, R.mid.x, R.mid.y, 1.5f, { 262, 60, 84 }, (0.3f + 0.25f * lvl) * (on ? 1.0f : 0.4f));
        GL L; L.slime = HSL { 262, 68, 68 }; L.eyes = ! on ? "closed" : flat > 0.35f ? "squeeze" : lift > 14 ? "wide" : m.hp.on && ! m.hp.eyes.empty() ? m.hp.eyes : "open"; L.mouth = ! on ? "flat" : flat > 0.35f ? "flat" : lift > 14 ? "o" : "smile"; L.blush = 0.55f; L.glow = 0;
        if (look == Look::neon) { L.tube = HSL { 262, 100, 72 }; L.slime = HSL { 262, 100, 62 }; L.face = HSL { 200, 40, 94 }; L.k = 0.9f + 0.1f * hsh (T.tick, 5); }
        ghost (G, R, L, T); wear (G, R, "phones", T);
        const bool hit = flat > 0.05f && on; bar (G, yb, T, hit);
        const float padY = juce::jmax (yb + 0.6f, juce::jmin (padRest, R.top.y - 4.4f)); const std::vector<Poly> sp { zig (100, yb, 100, padY, 7, 3.4f), zig (180, yb, 180, padY, 7, 3.4f) }; const auto pad = rbox (86, padY, 108, 4, 2, 2);
        if (look == Look::neon) { G.strokeMany (sp, 0.9f, K ({ 190, 80, 70 }, 0.8f)); G.fill (pad, K ({ 190, 80, 60 }, 0.25f)); TubeOpt to; to.halo = hit ? 1.0f : 0.0f; tubeLine (G, { { 86, padY + 2 }, { 194, padY + 2 } }, 1.3f, { 190, 100, 70 }, to); }
        else { G.strokeMany (sp, 1.3f, K (look == Look::dawn ? HSL { 96, 40, 34 } : INKC)); SolidOpt so; so.iw = 1.2f; so.sd = 9; so.shw = 1.3f; solid (G, pad, look == Look::dawn ? MAT ("wood") : MAT ("brass"), so); }
        if (q < 4) { const int n = 6 * (1 << q); const float pitch = 256.0f / (float) n; std::vector<Poly> ds; for (int i = 0; i < n; ++i) ds.push_back ({ { 12 + (float) i * pitch, yb + 1.6f }, { 12 + (float) i * pitch + pitch * 0.55f, yb + 1.6f } }); G.strokeMany (ds, 1.5f, K (acc, 0.9f), false); }
        else G.stroke ({ { 12, yb + 1.6f }, { 268, yb + 1.6f } }, q == 5 ? 2.0f : 1.3f, K (acc, 0.95f));
        if (tp) G.stroke ({ { 12, yb + 5.4f }, { 268, yb + 5.4f } }, 0.8f, K ({ 186, 100, 74 }, 0.9f));
        if (hit && k >= 2 && k < 6)
        {
            glow (G, R.top.x, yLim + 2, 1.5f, { 40, 100, 80 }, 0.7f * (1 - (float) (k - 2) / 4));
            for (float sd : { -1.0f, 1.0f }) { CrumbOpt co; co.a0 = sd > 0 ? -0.3f : PI - 0.5f; co.a1 = sd > 0 ? 0.5f : PI + 0.3f; co.sp = 7; co.g = 0.4f; co.r = 1.6f; crumbs (G, R.top.x + sd * R.halfW * 0.7f, yLim + 2, 4, (float) T.tick, (float) (k - 2), [] (int) { return K ({ 44, 100, 70 }); }, co); }
        }
        const float gr = clampf ((over + flat * 8) / 70, 0, 1); G.rect (258, yb + 12, 6, 46, K ({ 262, 30, 14 }, 0.7f)); if (gr > 0.01f) G.rect (258, yb + 12, 6, 46 * gr, K (red, 0.95f));
        if (! on) offVeil (G);
        stageOver (G);
    }
};
} // namespace phantom::pv
