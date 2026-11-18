// SPDX-License-Identifier: AGPL-3.0-or-later
// HAUNT (HAUNT EDIT; port of phantom-anime/visuals/src/u_haunt.js): sixteen pads lie on the floor like a summoning board; hold one and
// the ghost over the board acts out that pad's gesture for as long as it is held. With nothing held the ghost dozes and shows, dimly,
// the gesture of the pad being edited.
#pragma once
#include "UGrid.h"

namespace phantom::pv
{
namespace hauntpv
{
    inline const HSL COL[10] { { 313, 100, 65 }, { 38, 100, 68 }, { 146, 100, 62 }, { 188, 100, 64 }, { 261, 100, 71 }, { 353, 100, 68 }, { 198, 100, 75 }, { 79, 100, 65 }, { 25, 100, 68 }, { 227, 100, 78 } };
    inline const float BEATS[11] { 1.0f / 16, 1.0f / 8, 1.0f / 6, 1.0f / 4, 1.0f / 3, 1.0f / 2, 2.0f / 3, 1, 2, 4, 8 };
    inline const float PADS[16][3] { { 0, 7, 0 }, { 0, 5, 0 }, { 0, 3, 0 }, { 0, 1, 0 }, { 1, 7, 0 }, { 2, 5, 0.5f }, { 2, 3, 0.5f }, { 2, 2, 0.4f }, { 3, 3, 0.6f }, { 3, 1, 0.75f }, { 4, 7, 0 }, { 9, 5, 0.5f }, { 5, 8, 0.3f }, { 6, 9, 0.5f }, { 7, 9, 0.5f }, { 8, 8, 0.5f } };
    inline V2 lerp2 (V2 a, V2 b, float t) { return { lerp (a.x, b.x, t), lerp (a.y, b.y, t) }; }
}

class HauntScene final : public Scene
{
public:
    std::vector<std::string> controls() const override { return { "pad", "sync", "latch", "motion", "ptype", "psize", "mix" }; }
    std::vector<float> defaults() const override { return { 0, 1, 0, 0.5f, 0, 7, 1 }; }
    // (the plugin's PAD TYPE / PAD SIZE controls are not used by the sound: the pad editor sets each pad. With the editor's pads fed in,
    //  the edited pad is drawn as the editor has it; without them, the painter's stand-in takes the two controls for the edited pad)
    struct Now { bool on; int sel; std::array<std::array<float, 3>, 16> pads; int held; bool play; int type, size; float mo, beats, Ls, o; int k; float prog, u, env; };
    static Now now (const Feed& F, State& S, const Clock& T)
    {
        Now n; n.on = F.on; n.sel = F.sel >= 0 ? F.sel : 0;
        for (int i = 0; i < 16; ++i) { n.pads[(size_t) i] = F.pads.size() == 16 ? F.pads[(size_t) i] : std::array<float, 3> { hauntpv::PADS[i][0], hauntpv::PADS[i][1], hauntpv::PADS[i][2] }; if (i == n.sel && F.pads.size() != 16) { n.pads[(size_t) i][0] = (float) juce::jlimit (0, 9, juce::roundToInt (F.v ("ptype"))); n.pads[(size_t) i][1] = (float) juce::jlimit (0, 10, juce::roundToInt (F.v ("psize"))); } }
        n.held = F.hasHeld() ? F.held : juce::roundToInt (F.v ("pad")) - 1; n.play = n.on && n.held >= 0 && n.held < 16;
        if (S.dv.find ("hPad") == S.dv.end() || (int) S.dv["hPad"] != n.held) { S.dv["hT0"] = S.dv.count ("hSeen") ? T.t : T.t - 1.3; S.dv["hPad"] = n.held; } S.dv["hSeen"] = 1;
        const auto& pd = n.pads[(size_t) (n.play ? n.held : n.sel)]; n.type = juce::jlimit (0, 9, (int) pd[0]); n.size = juce::jlimit (0, 10, (int) pd[1]); n.mo = clampf (pd[2] * 2 * F.v ("motion"), 0, 1);
        n.beats = hauntpv::BEATS[n.size]; n.Ls = n.beats * 60 / 120; n.o = n.play ? (float) (T.t - S.dv["hT0"]) : 0.0f;
        n.k = n.play ? (int) std::floor (n.o / n.Ls) : 2; n.prog = n.play ? (F.prog >= 0 ? F.prog : std::fmod (n.o / n.Ls, 1.0f)) : 0.62f; n.u = n.play ? juce::jmin (1.0f, n.o / n.Ls) : 0.6f;
        n.env = F.env >= 0 ? F.env : S.follow ("henv", n.play ? 1.0f : 0.0f, 0.5f, 0.15f); return n;
    }
    static V2 bp (float u, float w) { const float d = std::pow (w, 1.15f), hw = lerp (66, 120, d); return { 140 + (u - 0.5f) * 2 * hw, 208 + 82 * d }; }
    struct GO { float gx, gy, z, ga, lvl; float box[4]; bool lite = false; };
    // the ghost and its gesture, at any place and size (z = 1 is the stage's s = 40)
    static Rig gest (G& G, const Now& N, const Clock& T, const GO& O)
    {
        const Look look = G.look; const int type = N.type, size = N.size; const float mo = N.mo, Ls = N.Ls, o = N.o, prog = N.prog, u = N.u; const int k = N.k; const bool play = N.play, pv = ! play;
        const HSL col = hauntpv::COL[type], veilC = L3<HSL> (look, { 200, 60, 7 }, { 262, 50, 8 }, { 250, 60, 4 }); const float gx = O.gx, gy = O.gy, z = O.z, ga = O.ga, s = 40 * z;
        RigP base; base.s = s; base.x = gx; base.y = gy - (play ? O.lvl * 2 * z : 0.0f); base.tail = 0.25f;
        GL Lb; Lb.slime = HSL { 318, 78, 68 }; Lb.eyes = pv ? "closed" : "open"; Lb.mouth = pv ? "flat" : "smile"; Lb.blush = 0.5f; Lb.glow = 0; Lb.a = ga;
        if (look == Look::neon) { Lb.tube = HSL { 313, 100, 66 }; Lb.slime = HSL { 322, 100, 62 }; Lb.k = (pv ? 0.6f : 1.0f) * (0.9f + 0.1f * hsh (T.tick, 5)); } if (O.lite) Lb.lite = true;
        auto pose = [] (RigP& P, float q) { const auto hp = hitPose ((int) std::floor (clampf (q, 0, 0.999f) * 7)); if (hp.on) { P.sx = hp.sx; P.sy = hp.sy; P.lift = hp.lift; P.hem = hp.hem; } };
        auto rigOf = [&] (const RigP& P, bool flip = false) { const Rig R = ghostRig (P); return flip ? flipRig (R) : R; };
        auto draw = [&] (const RigP& P, const std::function<void (GL&)>& lx, bool flip = false) { const Rig R = rigOf (P, flip); GL L = Lb; if (lx) lx (L); ghost (G, R, L, T); if (L.a > 0.4f) { WearOpt wo; wo.a = L.a; wear (G, R, "crown", T, wo); } return R; };
        const float ew = juce::jmax (1.0f, 1.3f * z); Rig R0; float veil = -1;
        if (type == 0)
        {
            static const int ns[11] { 6, 6, 5, 5, 4, 4, 3, 3, 2, 1, 1 }; const int n = ns[size]; const float d = juce::jmin (12.0f + 7 * (float) size, 104.0f / (float) n) * z, duty = 1 - 0.8f * mo, xm = gx + (float) n * d * 0.36f;
            RigP P = base; pose (P, prog);
            for (int j = n; j >= 1; --j) { RigP q = P; q.x = xm - (float) j * d; q.sx = P.sx * (0.5f + 0.5f * duty); ghostEcho (G, rigOf (q), col, (1 - (float) j / ((float) n + 1.5f)) * 0.8f * ga, ew); }
            RigP q = P; q.x = xm; R0 = draw (q, [&] (GL& L) { L.a = ga * (prog < duty ? 1.0f : 0.3f); });
        }
        else if (type == 1)
        {
            const float segL = 1 - 0.5f * mo; const int seg = pv ? 2 : juce::jmin (4, (int) std::floor ((o / Ls) / segL)); auto fr = [] (int j) { return (1 - std::pow (0.5f, (float) j)) / (1 - std::pow (0.5f, 4.0f)); };
            auto at = [&] (int j) { RigP P = base; P.x = gx + (-78 + 156 * fr (j)) * z; P.y = gy + (12 - (float) j * (6 + 7 * mo)) * z; P.s = s * (1 - 0.08f * (float) j); return P; };
            for (int j = 0; j < 5; ++j) if (j != seg) ghostEcho (G, rigOf (at (j)), col, (j < seg ? 0.3f : 0.55f) * ga, ew);
            const float oo = o - (float) seg * segL * Ls, Lk = Ls / std::pow (2.0f, (float) seg); RigP P = at (seg); pose (P, pv ? 0.5f : std::fmod (oo / Lk, 1.0f)); R0 = draw (P, [&] (GL& L) { L.eyes = pv ? "closed" : "wide"; });
        }
        else if (type == 2)
        {
            const bool open = prog < 0.15f + 0.7f * mo; RigP P = base; pose (P, prog / (0.15f + 0.7f * mo)); R0 = draw (P, [&] (GL& L) { L.a = ga * (open ? 1.0f : 0.12f); });
            if (! open) { StrokeOpt so; so.closed = true; so.dash = { 3, 3 }; G.stroke (R0.body, ew, K (col, 0.7f * ga), so); }
            // a small shutter in front of it: four slats, edge-on while the gate is open, shut when it shuts; two rails
            std::vector<Poly> sl, edge; for (int j = 0; j < 4; ++j) sl.push_back ({ { gx - 36 * z, gy - (92 - (float) j * 25) * z }, { gx + 36 * z, gy - (92 - (float) j * 25) * z } });
            if (! open) G.strokeMany (sl, 17 * z, K (veilC, 0.62f * ga), false);
            for (auto l : sl) { for (auto& q : l) q.y -= open ? 0.0f : 8.5f * z; edge.push_back (l); } G.strokeMany (edge, open ? 1.0f : 1.3f, K (col, (open ? 0.4f : 0.8f) * ga));
            G.strokeMany ({ { { gx - 37 * z, gy - 104 * z }, { gx - 37 * z, gy - 6 * z } }, { { gx + 37 * z, gy - 104 * z }, { gx + 37 * z, gy - 6 * z } } }, 1.6f * z, K (col, 0.6f * ga));
        }
        else if (type == 3)
        {
            RigP pl; pl.s = s; pl.x = 0; pl.y = 0; pl.tail = 0.25f; const Rig Rl = ghostRig (pl); GL Lg = Lb; Lg.glow = 0; Lg.tex = 0; Lg.a = 1; Lg.eyes = "wide"; Lg.mouth = "o";
            const auto lay = G.layer ("hg" + juce::String (z, 3) + (look == Look::neon ? juce::String (Lg.k, 2) : juce::String()), 108 * z, 124 * z, 54 * z, 108 * z, [&] (pv::G& g) { ghost (g, Rl, Lg, Clock {}); wear (g, Rl, "crown", Clock {}); });
            const float y0 = gy - 104 * z, bh = 30 * z; std::vector<Poly> frA, frB;
            for (int j = 0; j < 4; ++j)
            {
                const bool hit = hsh (j, k * 7 + 3) < mo + (pv ? 0.3f : 0.0f); const int act = hit ? 1 + (int) std::floor (hsh (j, k * 13 + 5) * 4) : 0; const float dx = act ? (hsh (j, k * 11 + 1) - 0.5f) * 2 * (10 + 26 * mo) * z : 0.0f;
                if (act == 4 && play) continue;
                G.clip ({ { gx - 70 * z, y0 + (float) j * bh }, { gx + 70 * z, y0 + (float) j * bh }, { gx + 70 * z, y0 + (float) (j + 1) * bh }, { gx - 70 * z, y0 + (float) (j + 1) * bh } }, [&] { G.blit (lay, gx + dx, base.y, ga); });
                if (act) { frA.push_back ({ { gx + dx - 34 * z, y0 + (float) j * bh + 0.6f }, { gx + dx + 34 * z, y0 + (float) j * bh + 0.6f } }); frB.push_back ({ { gx + dx - 34 * z, y0 + (float) (j + 1) * bh - 0.6f }, { gx + dx + 34 * z, y0 + (float) (j + 1) * bh - 0.6f } }); }
            }
            if (! frA.empty()) { G.strokeMany (frA, 1, K ({ 186, 100, 64 }, 0.8f * ga)); G.strokeMany (frB, 1, K ({ 322, 100, 64 }, 0.8f * ga)); }
            R0 = rigOf (base);
        }
        else if (type == 4)
        {
            RigP P = base; pose (P, 1 - prog);
            for (int j = 3; j >= 1; --j) { RigP q = P; q.x = gx + (float) j * 22 * z * (0.4f + 0.6f * (1 - prog)); ghostEcho (G, rigOf (q, true), col, 0.5f * ga / (float) j, ew); }
            R0 = draw (P, {}, true);
            const auto ar = arcp (gx, gy - 104 * z, 17 * z, 9 * z, PI * 0.15f, PI * 1.75f, 12); const V2 e = ar.back(); lightLine (G, ar, 1.5f * z, col, 0.9f * ga);
            G.fill ({ { e.x - 5 * z, e.y - 5 * z }, { e.x + 5 * z, e.y - 1 * z }, { e.x - 2 * z, e.y + 5 * z } }, K (col, 0.95f * ga));
        }
        else if (type == 5)
        {
            const float q = 1 - std::pow (1 - u, 1 + 2 * mo); G.ell (gx, gy + 3 * z, (12 + 30 * q) * z, (2 + 3 * q) * z, K (look == Look::neon ? HSL { 322, 100, 62 } : HSL { 318, 78, 62 }, 0.5f * q * ga));
            RigP P = base; P.sy = 1 - 0.45f * q; P.sx = 1 + 0.25f * q; P.dripK = 1 + 1.7f * q; R0 = draw (P, [&] (GL& L) { L.eyes = pv || q > 0.55f ? "closed" : q > 0.25f ? "happy" : "open"; L.mouth = q > 0.4f ? "flat" : "smile"; });
        }
        else if (type == 6 || type == 7) { R0 = draw (base, [&] (GL& L) { L.eyes = pv ? "closed" : "wide"; L.mouth = "o"; }); veil = u; }
        else if (type == 8)
        {
            const float b = 0.06f + u * u * (0.5f + 0.4f * mo); RigP P = base; P.block = b; P.seg = 3; R0 = draw (P, [&] (GL& L) { L.eyes = b > 0.4f ? "x" : pv ? "closed" : "open"; });
            if (play) { CrumbOpt co; co.sp = 7 * z; co.g = 1.2f * z; co.r = 1.9f * z; crumbs (G, gx, gy - 40 * z, juce::roundToInt (u * 9), (float) (T.tick >> 1), (float) (T.tick % 2), [&] (int i) { return K (i % 2 ? col : HSL { 46, 40, 94 }, 0.9f); }, co); }
        }
        else
        {
            const int kk = pv ? 2 : juce::jmin (4, k); auto at = [&] (int j) { RigP P = base; P.x = gx + (-52 + 26 * (float) j) * z; P.y = gy + (-40 + 15 * (float) j) * z; P.fat = 0.07f * (float) j * (0.5f + mo); P.s = s * (0.94f + 0.03f * (float) j); return P; };
            Poly st; for (int j = 0; j < 5; ++j) { st.push_back ({ gx + (-70 + 26 * (float) j) * z, gy + (-36 + 15 * (float) j) * z }); st.push_back ({ gx + (-44 + 26 * (float) j) * z, gy + (-36 + 15 * (float) j) * z }); }
            LineOpt lo; lo.a = 0.8f; lo.ta = 0.02f; lo.tb = 0.02f; lo.sd = 5; LN (G, st, 1.6f * z, look == Look::neon ? col : L3<HSL> (look, { 50, 50, 84 }, { 46, 40, 80 }, {}), lo);
            for (int j = 0; j < kk; ++j) ghostEcho (G, rigOf (at (j)), col, (0.25f + 0.12f * (float) j) * ga, ew);
            RigP P = at (kk); pose (P, prog); R0 = draw (P, [&] (GL& L) { L.eyes = pv ? "closed" : kk > 2 ? "squeeze" : "open"; L.mouth = kk > 2 ? "flat" : "smile"; });
        }
        // LP SWEEP: the dark comes down from the top as far as the sweep has gone; HP SWEEP: it rises from the floor
        if (veil >= 0)
        {
            const float bx0 = O.box[0], yT = O.box[1], bx1 = O.box[2], yB = O.box[3], ye = type == 6 ? lerp (yT, yB, veil) : lerp (yB, yT, veil);
            G.rect (bx0, type == 6 ? yT : ye, bx1 - bx0, type == 6 ? ye - yT : yB - ye, K (veilC, 0.8f * ga));
            std::vector<Poly> ls; for (int i = 0; i < 5; ++i) { const float y = type == 6 ? ye - 6 - (float) i * (ye - yT) / 6 : ye + 6 + (float) i * (yB - ye) / 6; if (y > yT && y < yB) ls.push_back ({ { bx0 + 8, y }, { bx1 - 8, y } }); } G.strokeMany (ls, 0.8f, K (col, 0.25f * ga));
            lightLine (G, { { bx0, ye }, { bx1, ye } }, (1.3f + 3 * mo) * juce::jmax (0.6f, z), col, (0.6f + 0.4f * mo) * ga); if (mo > 0.3f) glow (G, (bx0 + bx1) / 2, ye, z < 1 ? 1.5f : 2.6f, col, 0.3f * mo * ga);
        }
        if (pv) G.strokeMany ({ { { gx + 30 * z, gy - 96 * z }, { gx + 37 * z, gy - 96 * z }, { gx + 30 * z, gy - 88 * z }, { gx + 37 * z, gy - 88 * z } }, { { gx + 42 * z, gy - 108 * z }, { gx + 47 * z, gy - 108 * z }, { gx + 42 * z, gy - 102 * z }, { gx + 47 * z, gy - 102 * z } } }, 1.1f, K (look == Look::neon ? HSL { 313, 80, 80 } : HSL { 50, 40, 92 }, 0.7f));
        return R0;
    }
    void scene (G& G, const Feed& F, State& S, const Clock& T) override
    {
        const Look look = G.look; const auto N = now (F, S, T); const bool on = N.on, play = N.play, pv = ! play; const int sel = N.sel, held = N.held, type = N.type; const float mixk = mixA (F.v ("mix")), lvl = on ? F.level : 0, env = N.env;
        const HSL col = hauntpv::COL[type], dk = L3<HSL> (look, { 262, 30, 14 }, INKC, { 262, 60, 30 }), veilC = L3<HSL> (look, { 200, 60, 7 }, { 262, 50, 8 }, { 250, 60, 4 }); const float bs = 3 + (float) boilOf (T, look == Look::dawn ? 4 : 3) * 1.7f, gx = 140, gy = 178;
        stageUnder (G);
        G.rect (0, 0, 280, 300, K (veilC, L3 (look, 0.54f, 0.5f, 0.26f) * mixk));
        glow (G, gx, gy - 44, 3.4f, col, (0.08f + 0.3f * env) * mixk);
        auto tile = [&] (int i, float in = 0.07f) { const int c = i % 4, r = i / 4; const float a = in, b = 1 - in; Poly p { bp (((float) c + a) / 4, ((float) r + a) / 4), bp (((float) c + b) / 4, ((float) r + a) / 4), bp (((float) c + b) / 4, ((float) r + b) / 4), bp (((float) c + a) / 4, ((float) r + b) / 4) }; return look == Look::neon ? p : wob (p, 0.7f, 0.09f, (float) (i * 3 + 1)); };
        const Poly brd { bp (-0.03f, -0.04f), bp (1.03f, -0.04f), bp (1.03f, 1.04f), bp (-0.03f, 1.04f) };
        if (look == Look::neon) { G.fill (brd, K (pal::neonGlass, 0.8f)); TubeOpt to; to.closed = true; to.halo = 0; to.k = 0.7f; tubeLine (G, brd, 1.1f, { 262, 90, 62 }, to); }
        else { SolidOpt so; so.iw = 1.7f; so.sd = bs; so.shw = 3; solid (G, wob (brd, 0.6f, 0.05f, bs), look == Look::dawn ? Mat { { 150, 16, 24 }, { 190, 24, 13 }, {}, HSL { 60, 50, 60 } } : MAT ("dark"), so); }
        std::vector<Poly> byT[10]; for (int i = 0; i < 16; ++i) byT[juce::jlimit (0, 9, (int) N.pads[(size_t) i][0])].push_back (tile (i));
        for (int t = 0; t < 10; ++t)
        {
            if (byT[t].empty()) continue; const HSL c = hauntpv::COL[t];
            if (look == Look::neon) { G.fillMany (byT[t], K (c, 0.2f)); std::vector<Poly> cl; for (auto p : byT[t]) { p.push_back (p[0]); cl.push_back (p); } G.strokeMany (cl, 0.8f, K (c, 0.75f)); }
            else G.fillMany (byT[t], K (look == Look::dawn ? mixrgb ({ 190, 14, 32 }, c, 0.34f) : mixrgb ({ 262, 18, 36 }, c, 0.4f)));
        }
        if (look != Look::neon)
        {
            std::vector<Poly> hi; for (int i = 0; i < 16; ++i) { const auto p = tile (i); hi.push_back ({ p[0], p[1], hauntpv::lerp2 (p[1], p[2], 0.16f), hauntpv::lerp2 (p[0], p[3], 0.16f) }); } G.fillMany (hi, K ({ 50, 60, 92 }, 0.14f));
            if (look == Look::cel) { std::vector<Poly> rb; OutlineOpt oo; oo.gap = 0.1f; for (int i = 0; i < 16; ++i) for (auto& p : outlineRibbons (tile (i), 1, bs + (float) i, oo)) rb.push_back (p); G.fillMany (rb, K (INKC)); }
        }
        { const auto es = tile (sel, 0.02f); if (look == Look::neon) { TubeOpt to; to.closed = true; to.halo = 0; tubeLine (G, es, 1, { 50, 100, 80 }, to); } else G.stroke (es, 1.3f, K ({ 50, 90, 88 }, 0.9f), { true }); }
        if (play)
        {
            const auto p = tile (held); const HSL c = hauntpv::COL[juce::jlimit (0, 9, (int) N.pads[(size_t) held][0])]; const V2 mid = hauntpv::lerp2 (p[0], p[2], 0.5f); const float fl = 0.75f + 0.25f * hsh (T.tick, 3);
            G.fill (p, K ({ c.h, c.s, look == Look::neon ? 66.0f : 70.0f }, (0.5f + 0.4f * env) * fl)); G.fill (tile (held, 0.26f), K ({ 50, 80, 95 }, 0.5f * env * fl)); glow (G, mid.x, mid.y, 1.5f, c, 0.8f * env * mixk, true); glow (G, mid.x, mid.y - 8, 1, c, 0.5f * env * mixk);
            if (F.v ("latch") > 0.5f) { LightOpt lo; lo.closed = true; lightLine (G, circ (mid.x, mid.y, (p[1].x - p[0].x) * 0.62f, (p[3].y - p[0].y) * 0.7f, 14), 1.3f, { 50, 100, 84 }, 0.95f, lo); }
        }
        {   // LATCH: a padlock at the board's corner (shut when on)
            const float lx = 252, ly = 212; const bool on_ = F.v ("latch") > 0.5f; const HSL c = look == Look::neon ? HSL { 50, 100, 74 } : L3<HSL> (look, { 46, 80, 70 }, { 46, 84, 66 }, {}); const float a = on_ ? 1.0f : 0.4f;
            G.fill (rbox (lx - 7, ly, 14, 11, 2, 2), K (c, a)); G.stroke (arcp (lx + (on_ ? 0.0f : 4.6f), ly, 4.4f, 6, PI, TAU, 8), 1.8f, K (c, a));
            if (on_) { G.ell (lx, ly + 5, 1.6f, 1.6f, K (dk)); if (! play) { const auto p = tile (sel, 0.02f); LightOpt lo; lo.closed = true; lightLine (G, circ ((p[0].x + p[2].x) / 2, (p[0].y + p[2].y) / 2, (p[1].x - p[0].x) * 0.62f, (p[3].y - p[0].y) * 0.7f, 14), 1.2f, { 50, 100, 84 }, 0.6f, lo); } }
        }
        {   // the slice ruler under the ghost (PAD SIZE), SYNC START: full ticks on the grid, or short ticks and a drop mark
            const float x0 = 58, w = 164, y = 197, beats = N.beats; const bool sync = F.v ("sync") > 0.5f, fit = beats <= 1; const int nt = fit ? juce::jmax (1, (int) std::floor (1 / beats + 1e-6f)) : juce::roundToInt (beats);
            const float tw = fit ? w * beats : w / (float) nt; const HSL rc = look == Look::neon ? col : L3<HSL> (look, { 50, 60, 88 }, { 46, 50, 86 }, {}); const float off = sync ? 0.0f : tw * 0.37f;
            G.stroke ({ { x0, y }, { x0 + w, y } }, 1.1f, K (rc, 0.7f)); std::vector<Poly> ts; for (int i = 0; i <= nt; ++i) { const float x = juce::jmin (x0 + w, x0 + off + (float) i * tw); ts.push_back ({ { x, y - (sync ? 4.4f : 2.4f) }, { x, y + (sync ? 4.4f : 2.4f) } }); } G.strokeMany (ts, 1.2f, K (rc, 0.85f));
            if (! sync) G.fill ({ { x0 + off - 3.4f, y - 10 }, { x0 + off + 3.4f, y - 10 }, { x0 + off, y - 4.4f } }, K ({ 353, 90, 64 }));
            {
                const float px = 30, py = y - 10, pw = 20; const Poly fr { { px, py }, { px + pw, py }, { px + pw, py + pw }, { px, py + pw }, { px, py } };
                if (sync) { G.stroke (fr, 1.5f, K (rc, 0.95f)); std::vector<Poly> gl; for (int i = 1; i <= 2; ++i) { gl.push_back ({ { px + (float) i * pw / 3, py }, { px + (float) i * pw / 3, py + pw } }); gl.push_back ({ { px, py + (float) i * pw / 3 }, { px + pw, py + (float) i * pw / 3 } }); } G.strokeMany (gl, 1.3f, K (rc, 0.9f)); }
                else { G.stroke (fr, 1.1f, K (rc, 0.3f)); G.fill ({ { px + 4, py + 3 }, { px + pw - 4, py + 3 }, { px + pw / 2, py + pw - 3 } }, K ({ 353, 90, 62 }, 0.9f)); }
            }
            if (type == 2) { const float duty = 0.15f + 0.7f * N.mo; std::vector<Poly> ds; for (int i = 0; i < (fit ? nt : 1); ++i) ds.push_back ({ { x0 + off + (float) i * tw, y }, { juce::jmin (x0 + w, x0 + off + ((float) i + duty) * tw), y } }); G.strokeMany (ds, 3, K (col, 0.9f), false); }
            const float dx = x0 + off + (fit ? ((float) (((N.k % nt) + nt) % nt) + N.prog) * tw : N.prog * w); if (dx <= x0 + w + 1) { G.ell (dx, y, 3, 3, K (col)); if (play) glow (G, dx, y, 0.5f, col, 0.8f); }
        }
        {   // MOTION (the knob itself): streaks fan out either side of the ghost, longer with the knob
            const float mv = F.v ("motion"), ml = 5 + 30 * mv; std::vector<Poly> ls; for (float d : { -1.0f, 1.0f }) for (int j = 0; j < 4; ++j) { const float y = gy - 84 + (float) j * 20, x = gx + d * (52 + (float) (j % 2) * 5); ls.push_back ({ { x, y }, { x + d * ml, y - (1.5f - (float) j) * ml * 0.14f } }); }
            G.strokeMany (ls, 1.4f, K (col, (0.25f + 0.4f * mv) * (pv ? 0.6f : 1.0f)));
        }
        GO o { gx, gy, 1, (pv ? 0.6f : 1.0f) * (0.55f + 0.45f * juce::jmax (env, pv ? 1.0f : 0.0f)), lvl, { 0, 22, 280, gy + 24 } }; gest (G, N, T, o);
        if (! on) offVeil (G);
        stageOver (G);
    }
    // DAWN: light behind the pads (22,330,632,300; 1 su = 1 px), and the ghost with its gesture, small, in the corner under the pad settings
    bool hasDawn() const override { return true; }
    juce::Rectangle<float> dawnRect() const override { return { 22, 330, 632, 300 }; }
    std::vector<std::string> dawnControls() const override { return { "pad", "motion", "ptype", "psize", "mix" }; }
    // the editor's own pad centres (design px, relative to the picture's rectangle), set by the plugin; else the painter's measured ones
    std::array<V2, 16> padAt { [] { std::array<V2, 16> a {}; for (int i = 0; i < 16; ++i) a[(size_t) i] = { 4 + (float) (i % 4) * 108.5f + 50.5f, 3 + (float) (i / 4) * 75.5f + 34.5f }; return a; }() };
    void dawnScene (G& G, const Feed& F, State& S, const Clock& T, float W, float H) override
    {
        const auto N = now (F, S, T); const bool on = N.on, play = N.play; const float mixk = mixA (F.v ("mix")), lvl = on ? F.level : 0, env = N.env;
        for (int i = 0; i < 16; ++i) { const V2 q = padAt[(size_t) i]; glow (G, q.x, q.y + 30, 1, hauntpv::COL[juce::jlimit (0, 9, (int) N.pads[(size_t) i][0])], 0.16f * mixk, true); }
        if (play)
        {
            const V2 q = padAt[(size_t) N.held]; const HSL c = hauntpv::COL[juce::jlimit (0, 9, (int) N.pads[(size_t) N.held][0])]; const float fl = 0.75f + 0.25f * hsh (T.tick, 3);
            glow (G, q.x, q.y, 2, c, 0.5f * env * fl * mixk); glow (G, q.x, q.y + 34, 1.5f, c, 0.7f * env * fl * mixk, true);
            for (int i = 0; i < 6; ++i) { const float c2 = std::fmod (((float) T.f / 24) * (0.3f + 0.3f * hsh (i, 5)) + hsh (i, 7), 1.0f); G.ell (q.x + (hsh (i, 9) - 0.5f) * 90, q.y + 30 - c2 * 70, 1.3f, 1.3f, K ({ c.h, 80, 86 }, 0.7f * std::sin (c2 * PI) * env * mixk)); }
        }
        const float gx = W - 98, gy = H - 12; glow (G, gx, gy - 34, 2, hauntpv::COL[N.type], (0.12f + 0.3f * env) * mixk); glow (G, gx, gy + 2, 1.5f, hauntpv::COL[N.type], (0.2f + 0.3f * env) * mixk, true);
        GO o { gx, gy, 0.74f, (play ? 1.0f : 0.55f) * (0.5f + 0.4f * juce::jmax (env, play ? 0.0f : 1.0f)), lvl, { W - 190, H - 100, W - 4, H - 2 }, true }; gest (G, N, T, o);
    }
};
} // namespace phantom::pv
