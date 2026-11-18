// SPDX-License-Identifier: AGPL-3.0-or-later
// SPECTRA and DOOR (port of phantom-anime/visuals/src/u_spectra.js, u_door.js).
#pragma once
#include "Hands.h"

namespace phantom::pv
{
// SPECTRA (GHOST SPECTRA): a northern light made of the sound's spectrum hangs over the stage, lows left, highs right
class SpectraScene final : public Scene
{
public:
    std::vector<std::string> controls() const override { return { "freeze", "smear", "scatter", "shift", "ghost", "motion", "mdiv", "tilt", "hq", "fsync", "thresh", "pitch", "phase", "binshift", "mix" }; }
    std::vector<float> defaults() const override { return { 0, 0.3f, 0, 0.5f, 0.5f, 0, 14, 0.5f, 0, 0, 0, 0.5f, 0, 0, 1 }; }
    static HSL col (float q) { return q < 0.5f ? mixrgb ({ 300, 90, 66 }, { 176, 100, 62 }, q * 2) : mixrgb ({ 176, 100, 62 }, { 110, 90, 76 }, (q - 0.5f) * 2); }
    void scene (G& G, const Feed& F, State& S, const Clock& T) override
    {
        const Look look = G.look; const auto m = gmo (F, S, T); const bool on = m.on; const float lvl = m.lvl; const int k = m.k; const float mix = mixA (F.v ("mix"));
        const bool frz = F.v ("freeze") > 0.5f, hq = F.v ("hq") > 0.5f; const int N = hq ? 26 : 16; const float nt = F.v ("tilt") * 2 - 1;
        static const float grids[7] { 0, 0.25f, 0.5f, 1, 2, 4, 8 }; const float grid = grids[juce::jlimit (0, 6, juce::roundToInt (F.v ("fsync")))] * 6;
        if (frz) { const bool none = S.keys.find ("spF") == S.keys.end(); const float gnow = grid > 0 ? std::floor ((float) T.tick / grid) : 0.0f; if (none || (grid > 0 && gnow != S.keys["spG"])) { S.keys["spF"] = (float) T.tick; S.keys["spG"] = gnow; } }
        else S.keys.erase ("spF");
        const int tk = frz ? (int) S.keys["spF"] : T.tick; const float deal = std::floor ((float) tk / 2), sc = std::floor ((float) tk / 8), X0 = 18, XW = 244, yb = 154, HM = 118;
        std::vector<float> lev;
        for (int i = 0; i < N; ++i)
        {
            const float q = (float) i / (float) (N - 1), e = on ? (m.low * std::exp (-std::pow (q / 0.3f, 2.0f)) + m.mid * std::exp (-std::pow ((q - 0.45f) / 0.25f, 2.0f)) + m.high * std::exp (-std::pow ((q - 0.85f) / 0.3f, 2.0f))) * (0.55f + 0.45f * hsh (i, deal)) : 0.0f;
            lev.push_back (clampf ((0.22f + 0.9f * e) * (1 + nt * (q - 0.5f) * 1.6f), 0, 1.15f));
        }
        const float sm = F.v ("smear"), scat = F.v ("scatter"); std::vector<float> ls; std::vector<int> ord;
        for (int i = 0; i < N; ++i) { ls.push_back (lerp (lev[(size_t) i], (lev[(size_t) juce::jmax (0, i - 1)] + lev[(size_t) i] + lev[(size_t) juce::jmin (N - 1, i + 1)]) / 3, sm) * (1 + 0.45f * sm)); ord.push_back (i); }
        if (scat > 0.02f) for (int i = 0; i < N - 1; ++i) if (hsh (i, sc + 9) < scat * 0.7f) { const int j = juce::jmin (N - 1, i + 1 + (int) std::floor (hsh (i, sc + 3) * scat * 5)); std::swap (ord[(size_t) i], ord[(size_t) j]); }
        const float shf = (F.v ("shift") * 2 - 1) * 0.28f, pk = std::pow (2.0f, (F.v ("pitch") * 2 - 1) * 0.8f), bsh = F.v ("binshift"), th = F.v ("thresh");
        auto xq = [&] (float q) { return X0 + XW * clampf (q * pk + shf + bsh * 0.3f, -0.2f, 1.2f); };
        const HSL veilC = L3<HSL> (look, { 200, 60, 7 }, { 262, 50, 8 }, { 250, 60, 4 });
        stageUnder (G);
        G.rect (0, 0, 280, 300, K (veilC, (look == Look::neon ? 0.25f : 0.52f) * mix));
        const float motion = F.v ("motion"), sweepX = motion > 0.02f && on ? std::fmod ((float) T.t * 2 / DIVB[juce::jlimit (0, 15, juce::roundToInt (F.v ("mdiv")))], 1.0f) : -1.0f;
        auto wave = [&] (float x) { return std::sin (x * 0.035f + (frz ? 0.0f : (float) T.tick / 12 * 0.5f)) * 9 + std::sin (x * 0.09f + 1) * 4; };
        struct Foot { float x, y; HSL c; float l; }; std::vector<Foot> feet;
        for (int i = 0; i < N; ++i)
        {
            const float q = (float) i / (float) (N - 1), l = ls[(size_t) ord[(size_t) i]], xa = xq (q - 0.5f / (float) N), xz = xq (q + 0.5f / (float) N); if (l < th || xz < 4 || xa > 276) continue;
            const HSL c = col ((float) ord[(size_t) i] / (float) (N - 1)); const float h = HM * juce::jmin (1.0f, l) * (0.8f + 0.2f * hsh (i, 7)), y0 = yb + wave ((xa + xz) / 2);
            const float bright = sweepX >= 0 ? juce::jmax (0.0f, 1 - std::abs (q - sweepX) * 5) * motion : 0.0f, a = (0.2f + 0.3f * juce::jmin (1.0f, l) + 0.4f * bright) * mix; const HSL cc = frz ? mixrgb (c, { 196, 60, 90 }, 0.45f) : c; const float lean = (hsh (i, 2) - 0.5f) * 8;
            const float xm = (xa + xz) / 2, hw = (xz - xa) * 0.62f, xt = xm - 9 + lean;
            auto ray = [&] (float wk, float hk) { return Poly { { xm - hw * wk, y0 }, { xm + hw * wk, y0 }, { xt * hk + xm * (1 - hk) + hw * wk * 0.22f, y0 - h * hk }, { xt * hk + xm * (1 - hk) - hw * wk * 0.22f, y0 - h * hk } }; };
            if (look == Look::neon) { G.fill (ray (1, 1), K (cc, a * 0.4f)); TubeOpt to; to.halo = 0; to.a = juce::jmin (1.0f, a * 2); tubeLine (G, { { xm, y0 }, { xt, y0 - h } }, hq ? 0.9f : 1.2f, cc, to); }
            else { G.fill (ray (1, 1), K (cc, a * 0.6f)); G.fill (ray (0.55f, 0.5f), K (mixrgb (cc, { 60, 60, 95 }, 0.4f), a * 0.85f)); }
            if (sm > 0.25f) G.fill ({ { xt - hw * 0.3f, y0 - h * 0.9f }, { xt + hw * 0.3f, y0 - h * 0.9f }, { xt - 4 + hw * 0.12f, y0 - h * (1.1f + 0.5f * sm) }, { xt - 4 - hw * 0.12f, y0 - h * (1.1f + 0.5f * sm) } }, K (cc, a * 0.3f * sm));
            feet.push_back ({ (xa + xz) / 2, y0, cc, l });
        }
        if (feet.size() > 1)
        {
            Poly fp0; for (auto& f : feet) fp0.push_back ({ f.x, f.y }); const auto fp = smooth (fp0, false, 3);
            if (look == Look::neon) { TubeOpt to; to.halo = 1; to.a = 0.9f * mix; tubeLine (G, fp, 1.2f, { 170, 100, 70 }, to); }
            else { G.stroke (fp, 7, K ({ 170, 80, 84 }, 0.22f * mix)); G.stroke (fp, 3, K ({ 170, 80, 90 }, 0.4f * mix)); G.stroke (fp, 1.2f, K ({ 150, 60, 97 }, 0.9f * mix)); }
        }
        for (int i = 0; i < 3 && ! feet.empty(); ++i) { const auto& f = feet[(size_t) std::floor (((float) i + 0.5f) / 3 * (float) feet.size())]; glow (G, f.x, 250, 2.1f, f.c, (0.2f + 0.3f * f.l) * mix, true); }
        if (th > 0.02f) { const float y = yb - HM * th; std::vector<Poly> ds; for (int i = 0; i < 14; ++i) ds.push_back ({ { 14 + (float) i * 18.5f, y }, { 24 + (float) i * 18.5f, y } }); G.strokeMany (ds, 1, K ({ 50, 80, 90 }, 0.7f)); }
        if (F.v ("phase") > 0.03f && ! feet.empty()) for (int i = 0; i < juce::roundToInt (F.v ("phase") * 14); ++i) { const auto& f = feet[(size_t) std::floor (hsh (i, T.tick) * (float) feet.size())]; G.ell (f.x + (hsh (i, T.tick + 3) - 0.5f) * 14, f.y - hsh (i, T.tick + 5) * HM * f.l, 1.2f, 1.2f, K ({ 60, 60, 97 }, 0.9f * mix)); }
        if (frz && ! feet.empty()) for (int i = 0; i < 5; ++i) { const auto& f = feet[(size_t) std::floor (hsh (i, S.keys["spF"]) * (float) feet.size())]; if (hsh (i, T.tick >> 2) > 0.4f) glint (G, f.x, f.y - hsh (i, 3) * HM * f.l, 0.8f, { 196, 60, 96 }, 0.9f * mix); }
        const float gs_ = F.v ("ghost"), ga = lerp (0.3f, 1, gs_); RigP P = m.P; P.s = 40; P.x = 140; P.y = 258 - lvl * 2; P.tail = 0.25f; const Rig R = ghostRig (P);
        glow (G, R.mid.x, R.mid.y, 1.5f, { 169, 100, 74 }, (0.25f + 0.3f * lvl) * (on ? 1.0f : 0.4f));
        GL L; L.slime = HSL { 262, 68, 68 }; L.eyes = ! on ? "closed" : m.hp.on && ! m.hp.eyes.empty() ? m.hp.eyes : "open"; L.mouth = ! on ? "flat" : "smile"; L.blush = 0.55f * ga; L.glow = 0; L.a = ga; L.tint = HSL { 169, 90, 60 }; L.tintA = (1 - gs_) * 0.4f;
        if (look == Look::neon) { L.tube = HSL { 169, 100, 66 }; L.slime = HSL { 262, 100, 68 }; L.tint.reset(); L.k = (0.4f + 0.6f * gs_) * (0.9f + 0.1f * hsh (T.tick, 5)); }
        ghost (G, R, L, T); if (ga > 0.6f) { WearOpt wo; wo.a = ga; wear (G, R, "horns", T, wo); }
        if (on && gs_ > 0.5f && k >= 0 && k < 3) { LightOpt lo; lo.closed = true; lo.halo = 1; lightLine (G, R.body, 2, { 169, 100, 84 }, (gs_ - 0.5f) * 2 * (1 - (float) k / 3), lo); }
        if (! on) offVeil (G);
        stageOver (G);
    }
};

// DOOR (DOORWAY): a door stands on the stage with another space behind it; its light falls into our room, toward the ghost
class DoorScene final : public Scene
{
public:
    std::vector<std::string> controls() const override { return { "size", "shape", "move", "density", "decay", "transform", "freeze", "predelay", "tone", "early", "morph", "stretch", "mix" }; }
    std::vector<float> defaults() const override { return { 0.6f, 1, 1, 0.7f, 0.45f, 0, 0, 0.08f, 0.5f, 0.5f, 0, 0.333f, 1 }; }
    static HSL lightOf (int i) { static const HSL l[5] { { 200, 50, 88 }, { 70, 80, 76 }, { 262, 50, 66 }, { 210, 30, 94 }, { 320, 90, 82 } }; return l[i]; }
    template <typename Fn> static void space (G& G, int i, V2 c, float w, float h, float a, Fn f)
    {
        auto B = [&] (HSL col, float al) { return K (col, al * a); };
        auto R_ = [&] (float sx, float sy) { return Poly { f (c.x - sx, c.y - sy), f (c.x + sx, c.y - sy), f (c.x + sx, c.y + sy), f (c.x - sx, c.y + sy) }; };
        static const HSL bgs[5] { { 214, 26, 20 }, { 150, 36, 12 }, { 262, 44, 6 }, { 218, 22, 58 }, { 300, 44, 15 } };
        G.fill ({ { c.x - w, c.y - h }, { c.x + w, c.y - h }, { c.x + w, c.y + h }, { c.x - w, c.y + h } }, B (bgs[i], 1));
        if (i == 0)
        {
            for (int j = 0; j < 4; ++j) { const float s_ = std::pow (0.66f, (float) j); StrokeOpt so; so.closed = true; G.stroke (R_ (w * 0.92f * s_, h * 0.92f * s_), 1.6f - (float) j * 0.3f, B ({ 204, 40, 76 }, 0.9f - (float) j * 0.16f), so); }
            std::vector<Poly> ls; for (auto q : Poly { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } }) ls.push_back ({ f (c.x + q.x * w * 0.92f, c.y + q.y * h * 0.92f), f (c.x + q.x * w * 0.26f, c.y + q.y * h * 0.26f) }); G.strokeMany (ls, 0.9f, B ({ 204, 40, 66 }, 0.7f));
            const auto e = f (c.x, c.y); G.ell (e.x, e.y, w * 0.2f, h * 0.2f, B ({ 190, 70, 92 }, 0.95f));
        }
        if (i == 1)
        {
            const auto e = f (c.x, c.y + h * 0.1f); G.ell (e.x, e.y, w * 0.5f, h * 0.42f, B ({ 78, 70, 62 }, 0.55f)); G.ell (e.x, e.y, w * 0.25f, h * 0.22f, B ({ 64, 90, 84 }, 0.8f));
            for (int j = 0; j < 6; ++j) { const float sd = j % 2 ? 1.0f : -1.0f, y0 = c.y - h + (float) j * h * 0.36f; const auto p = smooth ({ f (c.x + sd * w * 1.05f, y0), f (c.x + sd * w * 0.55f, y0 + h * 0.12f), f (c.x + sd * w * (0.3f + 0.2f * hsh (j, 3)), y0 + h * 0.34f), f (c.x + sd * w * 0.5f, y0 + h * 0.5f) }, false, 3); G.stroke (p, 5 - (float) j * 0.4f, B ({ 28, 34, 20 }, 1)); G.stroke (p, 1.4f, B ({ 96, 50, 40 }, 0.8f)); }
        }
        if (i == 2) for (int j = 0; j < 5; ++j) { const float s_ = 1 - (float) j * 0.19f; const auto e = f (c.x, c.y); G.ell (e.x, e.y, w * 1.25f * s_, h * 0.95f * s_, B ({ 262, 40, 7 + (float) j * 5 }, 1)); G.ring (e.x, e.y, w * 1.25f * s_, h * 0.95f * s_, 1.1f, B ({ 262, 50, 52 }, 0.55f - (float) j * 0.07f)); }
        if (i == 3) for (int j = 0; j < 3; ++j) { const auto e = f (c.x + (float) (j - 1) * w * 0.5f, c.y + (j % 2 ? -1.0f : 1.0f) * h * 0.25f); SpriteOpt so; so.tint = K ({ 210, 30, 97 }); so.a = 0.7f * a; so.size = 1.5f; G.sprite ("glow-soft", e.x, e.y, so); }
        if (i == 4)
        {
            static const HSL cols[6] { { 320, 80, 76 }, { 276, 80, 72 }, { 196, 80, 82 }, { 330, 90, 88 }, { 262, 70, 60 }, { 300, 70, 66 } };
            for (int j = 0; j < 6; ++j)
            {
                const float an = (float) j / 6 * TAU + 0.4f, r = w * (0.5f + 0.5f * hsh (j, 5));
                const auto p0 = f (c.x + std::cos (an) * w * 1.1f, c.y + std::sin (an) * h * 1.05f), p1 = f (c.x + std::cos (an + 0.5f) * w * 1.1f, c.y + std::sin (an + 0.5f) * h * 1.05f), p2 = f (c.x + std::cos (an + 0.25f) * (w * 1.1f - r * 1.2f), c.y + std::sin (an + 0.25f) * (h * 1.05f - r * 1.6f));
                G.fill ({ p0, p1, p2 }, B (cols[j], 0.92f)); G.stroke ({ p0, p2, p1 }, 0.8f, B ({ 330, 60, 96 }, 0.7f));
            }
            const auto e = f (c.x, c.y); SpriteOpt so; so.tint = K ({ 330, 60, 97 }); so.a = 0.9f * a; so.size = 1.25f; G.sprite ("glint", e.x, e.y, so);
        }
    }
    void scene (G& G, const Feed& F, State& S, const Clock& T) override
    {
        const Look look = G.look; const auto m = gmo (F, S, T); const bool on = m.on; const float lvl = m.lvl; const float mix = mixA (F.v ("mix"));
        const int sh = juce::jlimit (0, 4, juce::roundToInt (F.v ("shape"))), nx = (sh + 1) % 5, mv = juce::roundToInt (F.v ("move")), mo = juce::roundToInt (F.v ("morph")); const float tr = F.v ("transform"), nt = F.v ("tone") * 2 - 1, pre = F.v ("predelay"), decay = F.v ("decay"), size = F.v ("size"), early = F.v ("early"), dens = F.v ("density"); const bool frz = F.v ("freeze") > 0.5f;
        const float scl = 1 - 0.3f * pre, yT = 242 - 36 * pre, W = 96 * scl, Hh = juce::jmin (yT - 14, 178 * lerp (0.6f, 1.45f, F.v ("stretch")) * scl), xl = 172 - W / 2, xr = 172 + W / 2, yt = yT - Hh, th = (8 + 100 * size) * PI / 180, xfo = xr - W * std::cos (th), open = clampf ((xfo - xl) / W, 0, 1), ft = 9 * scl;
        HSL lc = mixrgb (lightOf (sh), lightOf (nx), tr); lc = nt < 0 ? mixrgb (lc, { 200, 90, 86 }, -nt * 0.6f) : mixrgb (lc, { 34, 100, 74 }, nt * 0.6f);
        const HSL dk = L3<HSL> (look, { 262, 30, 14 }, INKC, { 262, 60, 30 }); const float bs = 3 + (float) boilOf (T, look == Look::dawn ? 4 : 3) * 1.7f;
        const float pulse = on && ! S.hits.empty() ? (frz ? 1.0f : std::exp (-(float) (T.t - S.hits.back()) / (0.2f + 2.2f * decay))) : 0.0f, la = (0.5f + 0.3f * lvl + 0.3f * pulse) * mix * (on ? 1.0f : 0.5f);
        stageUnder (G);
        G.rect (0, 0, 280, 300, K (L3<HSL> (look, { 220, 50, 7 }, { 262, 50, 8 }, { 250, 60, 4 }), (look == Look::neon ? 0.22f : 0.42f) * mix));
        glow (G, 172, yT - Hh * 0.5f, clampf (3 * scl * (Hh / 132), 1.5f, 3.4f), lc, (0.12f + 0.5f * early) * la);
        const V2 c { (xl + xr) / 2, (yt + yT) / 2 }; const float tt = on && ! frz ? (float) T.tick / 12 : 0.0f, dx = mv == 1 ? std::sin (tt * 0.5f) * W * 0.22f : 0.0f, rot = mv == 2 ? tt * 0.35f : 0.0f, ex = mv == 3 ? std::fmod (tt * 0.3f, 1.0f) * 0.35f : 0.0f;
        auto f = [&] (float x, float y) { const float ux = (x - c.x) * (1 + ex), uy = (y - c.y) * (1 + ex), cs = std::cos (rot), sn = std::sin (rot); return V2 { c.x + ux * cs - uy * sn + dx, c.y + ux * sn + uy * cs }; };
        const Poly op { { xl, yt }, { xr, yt }, { xr, yT }, { xl, yT } };
        G.clip (op, [&]
        {
            space (G, sh, c, W / 2, Hh / 2, 1, f); if (tr > 0.02f && mo != 1) space (G, nx, c, W / 2, Hh / 2, tr, f);
            if (tr > 0.02f && mo == 2) { G.stroke ({ { xl + W * tr - 3, yt }, { xl + W * tr - 3, yT } }, 1.6f, K ({ 350, 100, 60 }, 0.7f)); G.stroke ({ { xl + W * tr + 3, yt }, { xl + W * tr + 3, yT } }, 1.6f, K ({ 186, 100, 60 }, 0.7f)); }
            G.rect (xl, yt, W, Hh, K ({ 250, 50, 5 }, (0.5f - 0.42f * size) * (1 - 0.5f * pulse)));
        });
        if (tr > 0.02f && mo == 1) G.clip ({ { xl, yt }, { xl + W * tr, yt }, { xl + W * tr, yT }, { xl, yT } }, [&] { space (G, nx, c, W / 2, Hh / 2, 1, f); });
        const float Ld = (26 + 78 * decay) * scl, xo = juce::jmax (xl, juce::jmin (xr, xfo)); const int nb = 1 + juce::roundToInt (dens * 6);
        if (open > 0.03f) for (int j = 0; j < nb; ++j)
        {
            const float a0 = (float) j / (float) nb, a1 = ((float) j + (0.55f + 0.4f * dens)) / (float) nb, x0 = lerp (xl, xo, a0), x1 = lerp (xl, xo, juce::jmin (1.0f, a1)), bx0 = lerp (xl - Ld * 1.15f, xo - Ld * 0.2f, a0), bx1 = lerp (xl - Ld * 1.15f, xo - Ld * 0.2f, juce::jmin (1.0f, a1));
            G.fill ({ { x0, yT }, { x1, yT }, { bx1, yT + Ld * 0.62f }, { bx0, yT + Ld * 0.62f } }, K (lc, (0.3f + 0.16f * pulse) * la));
        }
        if (open > 0.03f)
        {
            glow (G, lerp (xl, xo, 0.5f) - Ld * 0.45f, yT + Ld * 0.34f, clampf (1 + Ld / 50, 1, 2.1f), lc, 0.5f * la, true);
            if (on) for (double t0 : S.hits) { const float age = (float) (T.t - t0), q = age * 1.6f; if (age < 0 || q > 1) continue; const float y = yT + Ld * 0.62f * q; G.stroke ({ { lerp (xl, xl - Ld * 1.15f, q), y }, { lerp (xo, xo - Ld * 0.2f, q), y } }, 2, K (mixrgb (lc, { 50, 60, 97 }, 0.5f), (1 - q) * 0.8f * mix)); }
        }
        const Poly fr { { xl - ft, yT }, { xl - ft, yt - ft }, { xr + ft, yt - ft }, { xr + ft, yT }, { xr, yT }, { xr, yt }, { xl, yt }, { xl, yT } }; const float sn = std::sin (th);
        const Poly leaf { { xr, yt }, { xfo, yt - 7 * sn * scl }, { xfo, yT + 10 * sn * scl }, { xr, yT } };
        if (look == Look::neon)
        {
            G.fill (fr, K (pal::neonGlass, 0.8f)); { TubeOpt to; to.k = 0.6f + 0.4f * early; tubeLine (G, { { xl - ft, yT }, { xl - ft, yt - ft }, { xr + ft, yt - ft }, { xr + ft, yT } }, 1.5f, { 268, 100, 70 }, to); } { TubeOpt to; to.halo = 0; tubeLine (G, { { xl, yT }, { xl, yt }, { xr, yt }, { xr, yT } }, 1, lc, to); }
            G.fill (leaf, K (pal::neonGlass, 0.6f)); G.fill (leaf, K ({ 268, 100, 60 }, 0.12f)); { TubeOpt to; to.closed = true; to.halo = 0; tubeLine (G, leaf, 1.2f, { 268, 100, 70 }, to); } G.ell (lerp (xr, xfo, 0.82f), (yt + yT) / 2 + 6, 2, 2, K ({ 46, 100, 70 }));
        }
        else
        {
            { SolidOpt so; so.iw = 1.8f; so.sd = bs; so.shw = 3.2f; so.rimw = 1.8f; solid (G, wob (fr, 0.5f, 0.04f, bs), look == Look::dawn ? Mat { { 30, 34, 40 }, { 18, 36, 20 }, {}, HSL { 46, 80, 78 } } : MAT ("steel"), so); }
            if (look == Look::dawn) { G.fill (blobp (172, yt - ft - 1, W * 0.62f, 4.6f, 0.25f, 10, 5), K ({ 112, 44, 32 })); G.fill (blobp (xl - ft + 2, yT - 3, 9, 6, 0.25f, 8, 7), K ({ 96, 50, 36 })); G.strokeMany ({ { { xl - ft * 0.5f, yt + 12 }, { xl - ft * 0.5f, yT - 14 } }, { { xr + ft * 0.5f, yt + 20 }, { xr + ft * 0.5f, yT - 8 } } }, 1, K (dk, 0.45f)); }
            else { std::vector<Poly> rv; for (auto q : Poly { { xl - ft * 0.5f, yt + 10 }, { xl - ft * 0.5f, yT - 12 }, { xr + ft * 0.5f, yt + 10 }, { xr + ft * 0.5f, yT - 12 }, { 172, yt - ft * 0.5f } }) rv.push_back (circ (q.x, q.y, 1.8f, 1.8f, 6)); G.fillMany (rv, K (dk)); }
            if (std::abs (xfo - xr) > 2)
            {
                { SolidOpt so; so.iw = 1.7f; so.sd = bs + 2; so.shw = 3; so.rimw = 1.6f; solid (G, leaf, look == Look::dawn ? Mat { { 28, 40, 46 }, { 16, 40, 24 }, {}, HSL { 46, 80, 80 } } : Mat { { 214, 16, 60 }, { 226, 22, 36 } }, so); }
                auto lx = [&] (float t) { return lerp (xr, xfo, t); }; auto ly = [&] (float t, float u) { return lerp (lerp (yt, yt - 7 * sn * scl, t), lerp (yT, yT + 10 * sn * scl, t), u); };
                const float cth = juce::jmax (0.25f, std::abs (std::cos (th)));
                if (look == Look::dawn)
                {
                    G.strokeMany ({ { { lx (0.33f), ly (0.33f, 0.03f) }, { lx (0.33f), ly (0.33f, 0.97f) } }, { { lx (0.66f), ly (0.66f, 0.03f) }, { lx (0.66f), ly (0.66f, 0.97f) } } }, 1, K (dk, 0.5f));
                    G.strokeMany ({ { { lx (0.04f), ly (0.04f, 0.2f) }, { lx (0.96f), ly (0.96f, 0.2f) } }, { { lx (0.04f), ly (0.04f, 0.78f) }, { lx (0.96f), ly (0.96f, 0.78f) } } }, 2.4f, K ({ 240, 14, 26 }));
                    G.ring (lx (0.8f), ly (0.8f, 0.52f), 3.4f * juce::jmax (0.3f, std::abs (std::cos (th))), 4, 1.4f, K ({ 42, 60, 56 }));
                }
                else
                {
                    const V2 wc { lx (0.5f), ly (0.5f, 0.5f) }; const float wr = 11 * scl; G.ring (wc.x, wc.y, wr * cth, wr, 2, K (INKC)); G.ring (wc.x, wc.y, wr * cth, wr, 1, K ({ 42, 62, 58 }));
                    std::vector<Poly> sp; for (int i = 0; i < 3; ++i) { const float a = (float) i / 3 * PI; sp.push_back ({ { wc.x + std::cos (a) * wr * cth, wc.y + std::sin (a) * wr }, { wc.x - std::cos (a) * wr * cth, wc.y - std::sin (a) * wr } }); } G.strokeMany (sp, 1.2f, K (INKC));
                }
            }
        }
        if (open > 0.03f) lightLine (G, { { xfo, yt - 7 * sn * scl }, { xfo, yT + 10 * sn * scl } }, 1.3f, lc, 0.8f * la);
        if (frz) for (int i = 0; i < 4; ++i) if (hsh (i, T.tick >> 2) > 0.4f) glint (G, lerp (xl - ft, xr + ft, hsh (i, 3)), lerp (yt - ft, yT, hsh (i, 5)), 0.8f, { 196, 60, 96 }, 0.9f);
        RigP P = m.P; P.s = 38; P.x = 54; P.y = 264 - lvl * 2; P.tail = 0.25f; const Rig R = ghostRig (P);
        glow (G, R.mid.x, R.mid.y, 1.5f, lc, (0.2f + 0.3f * open) * la);
        GL L; L.slime = HSL { 318, 78, 68 }; L.eyes = ! on ? "closed" : m.hp.on && ! m.hp.eyes.empty() ? m.hp.eyes : open > 0.5f || pulse > 0.4f ? "open" : "closed"; L.mouth = ! on ? "flat" : pulse > 0.6f ? "o" : "smile"; L.blush = 0.55f; L.glow = 0; L.tint = lc; L.tintA = 0.1f + 0.14f * open * la; L.rim = lc;
        if (look == Look::neon) { L.tube = HSL { 268, 100, 72 }; L.slime = HSL { 322, 100, 62 }; L.tint.reset(); L.k = 0.9f + 0.1f * hsh (T.tick, 5); }
        ghost (G, R, L, T); wear (G, R, "horns", T);
        if (! on) offVeil (G);
        stageOver (G);
    }
};
} // namespace phantom::pv
