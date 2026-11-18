// SPDX-License-Identifier: AGPL-3.0-or-later
// CRUSH and TUBE (port of phantom-anime/visuals/src/u_crush.js, u_tube.js).
#pragma once
#include "Hands.h"

namespace phantom::pv
{
// CRUSH (SOUL CRUSH is a compressor with grit and darkness): a vise; two jaws close on the ghost on every loud moment
class CrushScene final : public Scene
{
public:
    std::vector<std::string> controls() const override { return { "crush", "dark", "release", "prog", "blend", "out", "mix" }; }
    std::vector<float> defaults() const override { return { 0.5f, 0.3f, 0.35f, 1, 1, 0.667f, 1 }; }
    static void jaw (G& G, float f, float sd, const Clock& T, bool prog, float bite)
    {
        const Look look = G.look; const float bs = 3 + sd + (float) boilOf (T, look == Look::dawn ? 4 : 3) * 1.7f, e = sd < 0 ? 0.0f : 280.0f, W = 60, y0 = 118, y1 = 262, ym = 196;
        auto X = [&] (float d) { return f + sd * d; };
        const Poly blk { { X (W), y0 + 7 }, { X (W - 7), y0 }, { X (7), y0 }, { X (-2), y0 + 13 }, { X (2), y0 + 30 }, { X (2), y1 - 34 }, { X (-2), y1 - 24 }, { X (5), y1 }, { X (W), y1 } };
        std::vector<Poly> teeth; for (int i = 0; i < 4; ++i) { const float y = y0 + 34 + (float) i * 19.5f; teeth.push_back ({ { X (3), y }, { X (-7), y + 9.7f }, { X (3), y + 19.5f } }); }
        const Poly rod { { e, ym }, { X (W), ym } }, rod2 { { e, ym + 42 }, { X (W), ym + 42 } };
        std::vector<Poly> th; for (int i = 0; i < 7; ++i) { const float x = lerp (e + sd * 5, X (W + 6), ((float) i + 0.5f) / 7); th.push_back ({ { x - 2.4f, ym - 5.4f }, { x + 2.4f, ym + 5.4f } }); }
        const float ex = X (27), ey = y0 + 25, er = 8.5f; const Poly brow { { X (42), y0 + 10 - 2 * bite }, { X (13), y0 + 13 + 7 * bite } }; const V2 pup { X (27 - 3.4f), ey + 1 + 1.5f * bite };
        const auto spring = zig (e, ym - 19, X (W), ym - 19, 13, 4.2f); const HSL eyeC = mixrgb ({ 48, 100, 62 }, { 358, 100, 56 }, bite);
        if (look == Look::neon)
        {
            const HSL c { 353, 100, 64 }; G.strokeMany ({ rod, rod2 }, 4, K ({ 262, 50, 30 }, 0.9f), false); G.strokeMany (th, 1, K (c, 0.7f));
            if (prog) { TubeOpt to; to.halo = 0; tubeLine (G, spring, 0.8f, { 40, 100, 60 }, to); }
            G.fill (blk, K (pal::neonGlass, 0.75f)); G.fill (blk, K (c, 0.1f + 0.22f * bite)); { TubeOpt to; to.closed = true; to.heat = bite * 0.6f; tubeLine (G, blk, 1.5f, c, to); }
            { Poly tz; for (auto& t : teeth) { tz.push_back (t[0]); tz.push_back (t[1]); } tz.push_back (teeth[3][2]); TubeOpt to; to.halo = 0; to.k = 0.6f + 0.4f * bite; tubeLine (G, tz, 1.1f, { 46, 100, 62 }, to); }
            std::vector<Poly> ch; for (int i = 0; i < 3; ++i) ch.push_back ({ { X (50 - (float) i * 11), ym + 14 }, { X (40 - (float) i * 11), ym + 30 }, { X (50 - (float) i * 11), ym + 46 } }); G.strokeMany (ch, 1.8f, K (c, 0.3f + 0.7f * bite));
            { TubeOpt to; to.closed = true; to.halo = bite > 0.4f ? 1.0f : 0.0f; tubeLine (G, circ (ex, ey, er, er, 12), 1, eyeC, to); } G.ell (pup.x, pup.y, 3, 3, K (eyeC)); { TubeOpt to; to.halo = 0; tubeLine (G, brow, 1.4f, c, to); }
            return;
        }
        const bool dawn = look == Look::dawn; const Mat mt = dawn ? Mat { { 204, 12, 56 }, { 236, 20, 28 }, {}, HSL { 48, 70, 84 } } : MAT ("steel"), rm = dawn ? MAT ("wood") : MAT ("iron"); const HSL dk = dawn ? HSL { 262, 30, 16 } : INKC;
        G.strokeMany ({ rod, rod2 }, 8, K (dk), false); G.strokeMany ({ rod, rod2 }, 5, K (rm.lit), false); { StrokeOpt so; so.roundCap = false; G.stroke ({ { e, ym - 1.6f }, { X (W), ym - 1.6f } }, 1.2f, K (*rm.rim, 0.8f), so); } G.strokeMany (th, 1.2f, K (rm.shd));
        if (prog) G.stroke (spring, 1.7f, K (dawn ? HSL { 40, 60, 56 } : HSL { 42, 62, 58 }));
        G.fillMany (teeth, K (dawn ? HSL { 48, 30, 84 } : HSL { 210, 16, 84 })); { std::vector<Poly> tr; for (auto& t : teeth) for (auto& r : outlineRibbons (t, 1.2f, bs)) tr.push_back (r); G.fillMany (tr, K (dk)); }
        { SolidOpt so; so.iw = 1.9f; so.sd = bs; so.shw = 5.5f; so.rimw = 2; solid (G, wob (blk, 0.5f, 0.05f, bs), mt, so); }
        { SolidOpt so; so.iw = 1.2f; so.sd = bs + 2; so.shw = 1.6f; solid (G, rbox (juce::jmin (X (W - 3), X (W + 5)), ym - 11, 8, 22, 2, 2), dawn ? MAT ("wood") : MAT ("brass"), so); }
        G.fill ({ { X (W - 2), y1 - 16 }, { X (6), y1 - 16 }, { X (9), y1 - 3 }, { X (W - 2), y1 - 3 } }, K (mt.shd, 0.55f));
        if (! dawn) { std::vector<Poly> st; for (int i = 0; i < 4; ++i) { const float x = X (12 + (float) i * 11); st.push_back ({ { x, y1 - 5 }, { x + sd * 5, y1 - 5 }, { x + sd * 9, y1 - 14 }, { x + sd * 4, y1 - 14 } }); } G.fillMany (st, K ({ 46, 95, 58 })); }
        G.fillMany ({ circ (X (W - 8), y0 + 12, 2.3f, 2.3f, 7), circ (X (W - 8), ym + 22, 2.3f, 2.3f, 7), circ (X (W - 8), y1 - 24, 2.3f, 2.3f, 7) }, K (dk));
        if (dawn) { G.fill (blobp (X (30), y0 - 1, 24, 5, 0.25f, 10, sd * 7 + 3), K ({ 112, 44, 34 })); G.fill (blobp (X (44), y0 + 1, 10, 3.4f, 0.25f, 8, sd * 5 + 1), K ({ 92, 50, 44 })); G.stroke ({ { X (20), ym + 18 }, { X (30), ym + 30 }, { X (26), ym + 44 } }, 1, K (dk, 0.5f)); }
        G.fill (circ (ex, ey, er + 1.6f, er + 1.6f, 14), K (dk)); G.ell (ex, ey, er, er, K (dawn ? HSL { 46, 70, 90 } : HSL { 50, 60, 94 })); G.ell (pup.x, pup.y, 4, 4.4f, K (eyeC)); G.ell (pup.x - 1, pup.y - 1.4f, 1.3f, 1.3f, K ({ 50, 100, 98 }));
        if (bite > 0.3f) { SpriteOpt so; so.tint = K ({ 358, 100, 60 }); so.a = 0.5f * bite; so.size = 0.5f; G.sprite ("glow-soft", ex, ey, so); }
        G.fill (ribbon (linep (brow[0].x, brow[0].y, brow[1].x, brow[1].y, 6), 5.4f, bs, 0.12f, 0.12f), K (dk));
    }
    void scene (G& G, const Feed& F, State& S, const Clock& T) override
    {
        const Look look = G.look; MoOpt mo; mo.noHit = true; const auto m = gmo (F, S, T, mo); const bool on = m.on; const float lvl = m.lvl; const int k = m.k; const float mix = mixA (F.v ("mix")), cr = F.v ("crush"), dark = F.v ("dark"), rel = F.v ("release"), out = F.v ("out"), blend = F.v ("blend"); const bool prog = F.v ("prog") > 0.5f;
        const float tau = (0.03f + 0.9f * rel * rel) * (prog ? 0.75f : 1.0f), gr = on ? S.follow ("gr", juce::jmin (1.0f, lvl * 1.25f), 1, 1 - std::exp (-1 / (24 * tau))) : 0.0f, slam = on && k >= 0 && k < 2 ? 0.14f * cr : 0.0f;
        const float s = 46, pin = clampf ((cr * (0.2f + 0.8f * gr) + slam) * mix, 0, 1), hw = 0.79f * s * (1 - 0.45f * pin * 0.95f), g0 = lerp (78, 42, cr * mix), gap = juce::jmax (hw + 3, g0 - (g0 - 19) * pin), bite = sstep (0.35f, 0.9f, pin);
        stageUnder (G);
        if (dark > 0.02f) G.rect (0, 0, 280, 300, K ({ 252, 60, 5 }, dark * 0.62f));
        glow (G, 140, 150, 3.4f, { 40, 70, 84 }, (1 - dark) * (0.1f + 0.2f * out)); glow (G, 140, 258, 2.1f, { 353, 70, 66 }, 0.2f + 0.2f * gr, true);
        for (float lx : { 62.0f, 218.0f })
        {
            const float ly = 40, lit = 1 - dark; const Poly sh { { lx - 13, ly + 9 }, { lx - 6, ly - 3 }, { lx + 6, ly - 3 }, { lx + 13, ly + 9 } };
            if (look == Look::neon) { G.stroke ({ { lx, 0 }, { lx, ly - 3 } }, 1, K ({ 262, 50, 40 })); TubeOpt to; to.halo = 0; to.k = 0.3f + 0.7f * lit; tubeLine (G, sh, 1, { 46, 100, 60 }, to); G.ell (lx, ly + 10, 5, 3.2f, K ({ 46, 100, 70 }, 0.2f + 0.8f * lit)); glow (G, lx, ly + 14, 1.5f, { 46, 100, 66 }, 0.55f * lit); }
            else { G.stroke ({ { lx, 0 }, { lx, ly - 3 } }, 1.4f, K (look == Look::dawn ? HSL { 262, 30, 18 } : INKC)); glow (G, lx, ly + 16, 2, { 46, 90, 78 }, 0.6f * lit); SolidOpt so; so.iw = 1.3f; so.sd = lx; so.shw = 1.6f; solid (G, sh, MAT ("brass"), so); G.ell (lx, ly + 10, 5.4f, 3.4f, K (mixrgb ({ 250, 20, 30 }, { 50, 100, 88 }, lit))); }
        }
        const auto bed = rbox (6, 261, 268, 9, 2, 2);
        if (look == Look::neon) { TubeOpt to; to.halo = 0; tubeLine (G, { { 8, 262 }, { 272, 262 } }, 1.1f, { 353, 100, 60 }, to); tubeLine (G, { { 8, 269 }, { 272, 269 } }, 0.8f, { 262, 80, 60 }, to); }
        else { SolidOpt so; so.iw = 1.4f; so.sd = 8; so.shw = 2; solid (G, bed, MAT ("dark"), so); G.stroke ({ { 12, 265.5f }, { 268, 265.5f } }, 1, K ({ 250, 20, 44 }, 0.8f)); }
        const float gx = 140, gy = 66, na = PI * (1.12f + 0.76f * clampf (gr * cr * 1.2f, 0, 1));
        if (look == Look::neon) { TubeOpt to; to.halo = 0; tubeLine (G, arcp (gx, gy, 22, 22, PI * 1.1f, PI * 1.9f, 12), 1, { 353, 100, 64 }, to); to.halo = 1; tubeLine (G, { { gx, gy }, { gx + std::cos (na) * 19, gy + std::sin (na) * 19 } }, 1.1f, { 46, 100, 66 }, to); }
        else
        {
            { SolidOpt so; so.iw = 1.6f; so.sd = 4; so.shw = 3; solid (G, circ (gx, gy - 4, 27, 25, 18), MAT ("brass"), so); } G.fill (circ (gx, gy - 4, 22, 20, 16), K ({ 46, 50, 92 }));
            std::vector<Poly> ticks; for (int i = 0; i < 7; ++i) { const float a = PI * (1.12f + 0.76f * (float) i / 6); ticks.push_back ({ { gx + std::cos (a) * 19, gy + std::sin (a) * 17 }, { gx + std::cos (a) * 15, gy + std::sin (a) * 13.5f } }); } G.strokeMany (ticks, 1, K (INKC));
            G.arc (gx, gy, 19, 17, PI * 1.6f, PI * 1.88f, 2.4f, K ({ 353, 90, 56 })); G.stroke ({ { gx, gy + 2 }, { gx + std::cos (na) * 17, gy + 2 + std::sin (na) * 15.5f } }, 1.6f, K ({ 353, 80, 40 })); G.ell (gx, gy + 2, 2.2f, 2.2f, K (INKC));
        }
        jaw (G, 140 - gap, -1, T, prog, bite); jaw (G, 140 + gap, 1, T, prog, bite);
        const float dx = 140 + gap + 40, dy = 236, da = PI * (0.75f + 1.5f * rel);
        G.ell (dx, dy, 9.5f, 9.5f, look == Look::neon ? K (pal::neonGlass, 0.9f) : K ({ 46, 50, 92 })); G.ring (dx, dy, 9.5f, 9.5f, 1.3f, look == Look::neon ? K ({ 46, 100, 62 }) : K (look == Look::dawn ? HSL { 262, 30, 20 } : INKC));
        G.stroke ({ { dx, dy }, { dx + std::cos (da) * 7, dy + std::sin (da) * 7 } }, 1.5f, K (look == Look::neon ? HSL { 46, 100, 70 } : HSL { 353, 80, 42 }));
        if (blend < 0.98f) { RigP P0; P0.s = s; P0.x = 140; P0.y = 256; const Rig R0 = ghostRig (P0); StrokeOpt so; so.closed = true; so.dash = { 4, 3.2f }; G.stroke (R0.body, 1.3f, look == Look::neon ? K ({ 200, 90, 80 }, (1 - blend) * 0.9f + 0.1f) : K (look == Look::dawn ? HSL { 50, 40, 92 } : HSL { 45, 40, 96 }, (1 - blend) * 0.8f + 0.1f), so); }
        RigP P; if (pin <= 0.2f) P = m.P; P.s = s; P.x = 140; P.y = 256; P.tail = 0.25f * (1 - pin); P.pinch = pin * 0.95f; P.rough = cr * 0.55f * gr * mix; P.rsd = std::floor ((float) T.tick / 2);
        const Rig R = ghostRig (P);
        GL L; L.slime = HSL { 318, 78, 68 }; L.eyes = ! on ? "closed" : pin > 0.72f ? "x" : pin > 0.38f ? "squeeze" : "open"; L.mouth = ! on || pin > 0.38f ? "flat" : "smile"; L.blush = 0.5f + 0.4f * pin; L.glow = 0;
        if (look == Look::neon) { L.tube = HSL { 353, 100, 66 }; L.slime = HSL { 322, 100, 60 }; L.heat = pin * 0.5f; L.k = (1 - dark * 0.4f) * (0.9f + 0.1f * hsh (T.tick, 5)); }
        else if (dark > 0.02f) { L.tint = HSL { 252, 60, 8 }; L.tintA = dark * 0.42f; }
        ghost (G, R, L, T);
        if (on && bite > 0.05f)
        {
            const int n = juce::roundToInt (cr * 4 * gr) + (k >= 0 && k < 3 ? 4 : 0);
            for (float sd : { -1.0f, 1.0f })
            {
                CrumbOpt co; co.a0 = sd > 0 ? -1.3f : PI + 0.3f; co.a1 = sd > 0 ? -0.3f : PI + 1.3f; co.sp = 5; co.g = 0.6f;
                crumbs (G, 140 + sd * (gap - 3), 206, n, std::floor ((float) T.tick / 2) + sd, (float) (T.tick % 2 + (k >= 0 && k < 3 ? k : 0)), [&] (int i) { return look == Look::neon ? K ({ 353, 100, 70 }, 0.9f) : K (i % 3 == 0 ? HSL { 318, 78, 68 } : HSL { 46, 40, 95 }); }, co);
            }
        }
        if (! on) offVeil (G);
        stageOver (G);
    }
};

// TUBE: the ghost lives inside a glass valve; DRIVE lights the filament
class TubeScene final : public Scene
{
public:
    std::vector<std::string> controls() const override { return { "drive", "char", "bass", "treble", "bias", "air", "out", "mix" }; }
    std::vector<float> defaults() const override { return { 0.35f, 0, 0.5f, 0.5f, 0.5f, 0, 0.667f, 1 }; }
    static Poly env (bool mic)
    {
        const Poly h = mic ? Poly { { 30, 238 }, { 37, 224 }, { 58, 198 }, { 67, 160 }, { 64, 124 }, { 52, 96 }, { 32, 76 }, { 12, 66 }, { 5, 58 }, { 0, 54 } } : Poly { { 37, 238 }, { 44, 226 }, { 45, 150 }, { 45, 98 }, { 39, 72 }, { 25, 56 }, { 9, 49 }, { 5, 41 }, { 0, 37 } };
        Poly o; for (auto& q : h) o.push_back ({ 140 + q.x, q.y }); for (int i = (int) h.size() - 2; i >= 0; --i) o.push_back ({ 140 - h[(size_t) i].x, h[(size_t) i].y });
        return smooth (o, true, 3);
    }
    void scene (G& G, const Feed& F, State& S, const Clock& T) override
    {
        const Look look = G.look; const auto m = gmo (F, S, T); const bool on = m.on; const float lvl = m.lvl; const int k = m.k; const float mix = mixA (F.v ("mix")); const bool mic = F.v ("char") > 0.5f; const HSL acc { 25, 100, 68 };
        const float drive = F.v ("drive") * mix, nb = F.v ("bias") * 2 - 1, nBass = F.v ("bass") * 2 - 1, nTre = F.v ("treble") * 2 - 1, air = F.v ("air"), fx = 140 + nb * 16;
        const float flick = on ? (0.86f + 0.14f * hsh (T.tick, 3)) * (1 + 0.25f * lvl) * (k >= 0 && k < 3 ? 1.25f : 1.0f) : 0.35f;
        const auto en = env (mic); const float top = mic ? 54.0f : 37.0f, wHalf = mic ? 66.0f : 45.0f, bs = 3 + (float) boilOf (T, look == Look::dawn ? 4 : 3) * 1.9f;
        const HSL heat = drive < 0.5f ? mixrgb ({ 6, 80, 32 }, { 24, 100, 56 }, drive * 2) : mixrgb ({ 24, 100, 56 }, { 46, 100, 80 }, (drive - 0.5f) * 2);
        stageUnder (G);
        glow (G, 140, 150, 3.4f, { 28, 100, 66 }, (0.06f + 0.3f * F.v ("out")) * (0.3f + 0.9f * drive) * flick); glow (G, 140, 262, 2.1f, { 28, 90, 62 }, (0.2f + 0.5f * drive) * flick, true);
        const auto ch = circ (140, 262, 64, 9, 22);
        if (look == Look::neon) { TubeOpt to; to.closed = true; to.halo = 0; tubeLine (G, ch, 1.1f, { 25, 100, 58 }, to); }
        else { SolidOpt so; so.iw = 1.4f; so.sd = 4; so.shw = 2; solid (G, concat (arcp (140, 262, 64, 9, 0, PI, 12), arcp (140, 270, 64, 9, PI, 0, 12)), look == Look::dawn ? Mat { { 22, 36, 30 }, { 14, 34, 18 } } : MAT ("iron"), so); so.sd = 5; so.shw = 3; solid (G, ch, look == Look::dawn ? MAT ("wood") : MAT ("steel"), so); }
        G.fill (en, look == Look::neon ? K (pal::neonGlass, 0.5f) : K (look == Look::dawn ? HSL { 176, 40, 90 } : HSL { 190, 50, 92 }, 0.13f));
        const auto stc = look == Look::neon ? K ({ 230, 40, 50 }, 0.9f) : K (look == Look::dawn ? HSL { 240, 14, 34 } : HSL { 244, 14, 34 }, 0.9f);
        if (mic)
        {
            G.strokeMany ({ { { 102, 236 }, { 102, 120 } }, { { 178, 236 }, { 178, 120 } } }, 1.6f, stc); G.ring (140, 152, 42, 50, 1.6f, stc);
            std::vector<Poly> mesh; for (int i = 0; i < 6; ++i) { const float y = 112 + (float) i * 16, w = 42 * std::sqrt (juce::jmax (0.0f, 1 - std::pow ((y - 152) / 50, 2.0f))); mesh.push_back ({ { 140 - w, y }, { 140 + w, y } }); } G.strokeMany (mesh, 1, stc);
        }
        else { G.strokeMany ({ { { 107, 236 }, { 107, 92 } }, { { 173, 236 }, { 173, 92 } } }, 1.6f, stc); G.fill (rbox (113, 104, 54, 116, 5, 2), look == Look::neon ? K ({ 230, 40, 30 }, 0.5f) : K (look == Look::dawn ? HSL { 240, 16, 30 } : HSL { 250, 16, 34 }, 0.6f)); G.ell (140, 93, 35, 5.5f, stc); }
        G.ell (140, top + (mic ? 16.0f : 20.0f), mic ? 22.0f : 19.0f, 7, K ({ 220, 14, 82 }, look == Look::neon ? 0.18f : 0.4f));
        G.fill (en, K ({ 26, 100, 60 }, (0.03f + 0.27f * drive) * juce::jmin (1.2f, flick)));
        glow (G, fx, 168, clampf (0.75f + 2.4f * drive, 0.75f, 3.4f), { 26, 100, 62 }, (0.12f + 0.75f * drive) * juce::jmin (1.15f, flick));
        const auto fil = smooth ({ { fx - 13, 232 }, { fx - 13, 130 }, { fx - 9, 121 }, { fx, 117 }, { fx + 9, 121 }, { fx + 13, 130 }, { fx + 13, 232 } }, false, 2);
        if (look == Look::neon) { TubeOpt to; to.k = 0.35f + 0.65f * drive * flick; to.heat = drive * 0.7f; tubeLine (G, fil, 1.6f + drive, heat, to); }
        else { G.stroke (fil, (2.2f + 2 * drive) * 1.9f, K (heat, 0.28f * drive * flick)); G.stroke (fil, 2.2f + 2 * drive, K (heat)); if (drive > 0.45f) G.stroke (fil, 1, K ({ 50, 100, 92 }, (drive - 0.45f) * 1.8f)); }
        if (nBass > 0.03f) { glow (G, 140, 226, 1.5f, { 12, 100, 54 }, nBass * 0.85f * mix, true); G.ell (140, 230, mic ? 32.0f : 38.0f, 7, K ({ 14, 100, 56 }, nBass * 0.4f * mix)); }
        else if (nBass < -0.03f) G.ell (140, 226, mic ? 34.0f : 40.0f, 16, K ({ 226, 50, 14 }, -nBass * 0.5f * mix));
        if (nTre < -0.03f) G.ell (140, top + 26, mic ? 40.0f : 34.0f, 22, K ({ 240, 40, 12 }, -nTre * 0.45f * mix));
        RigP P = m.P; P.s = 30; P.x = 140 + nb * 5; P.y = 229 - lvl * 2; P.fat = 0.26f * drive; P.lean = nb * 0.08f; P.tail = 0.2f; const Rig R = ghostRig (P);
        GL L; L.slime = HSL { 42, 92, 62 }; L.eyes = ! on ? "closed" : m.hp.on && ! m.hp.eyes.empty() ? m.hp.eyes : drive > 0.3f ? "happy" : "open"; L.mouth = ! on ? "flat" : drive > 0.7f ? "grin" : "smile";
        L.blush = 0.5f + 0.45f * drive; L.glow = 0; L.tint = HSL { 22, 100, 60 }; L.tintA = 0.05f + 0.22f * drive; L.tex = 0.4f;
        if (look == Look::neon) { L.tube = mixrgb ({ 25, 100, 62 }, { 46, 100, 74 }, drive); L.slime = HSL { 46, 100, 58 }; L.heat = drive * 0.6f; L.tint.reset(); L.k = 0.9f + 0.1f * hsh (T.tick, 5); }
        ghost (G, R, L, T); wear (G, R, "crown", T);
        if (nTre > 0.03f && on) { const int n = juce::roundToInt (nTre * 7); for (int i = 0; i < n; ++i) { const float ph = std::floor ((float) T.tick / 3) + (float) i * 7; if (hsh (i, ph) < 0.35f) continue; glint (G, 140 + (hsh (i, ph + 1) - 0.5f) * wHalf * 1.2f, top + 16 + hsh (i, ph + 2) * 44, hsh (i, ph + 3) > 0.6f ? 0.8f : 0.5f, { 48, 80, 96 }, 0.9f * mix); } }
        if (on && k >= 0 && k < 6) { const float y = 222 - (float) k * 30, w = (mic ? 44.0f : 38.0f) * (y < top + 40 ? 0.6f : 1.0f); G.ring (140, y, w, w * 0.2f, 1.4f, K ({ 40, 100, 78 }, (1 - (float) k / 6) * 0.7f * mix)); }
        const float kx = mic ? 1.42f : 1.0f; Poly hl, hl2;
        for (auto q : Poly { { -34, 196 }, { -37.5f, 128 }, { -31, 88 }, { -27, 93 }, { -32.5f, 130 }, { -30, 196 } }) hl.push_back ({ 140 + q.x * kx, q.y });
        for (auto q : Poly { { 30, 180 }, { 33, 140 }, { 30, 112 } }) hl2.push_back ({ 140 + q.x * kx, q.y });
        if (look == Look::neon) { G.stroke (hl2, 1, K ({ 190, 60, 92 }, 0.45f)); G.stroke (Poly (hl.begin(), hl.begin() + 3), 1.2f, K ({ 190, 60, 92 }, 0.5f)); TubeOpt to; to.closed = true; to.k = 0.9f; tubeLine (G, en, 1.3f, { 190, 100, 70 }, to); }
        else
        {
            G.fill (smooth (hl, true, 2), K ({ 50, 60, 98 }, look == Look::dawn ? 0.5f : 0.62f)); G.stroke (hl2, 1.6f, K ({ 50, 60, 98 }, 0.4f));
            if (look == Look::cel) G.fillMany (outlineRibbons (wob (en, 0.4f, 0.03f, bs), 1.7f, bs), K (INKC));
            else { const auto rm = rimShape (en, 2.2f, -0.55f, -0.83f, 0.2f); if (! rm.empty()) G.fill (rm, K ({ 48, 100, 92 }, 0.85f)); OutlineOpt oo; oo.only = 0.05f; oo.wmin = 0.2f; oo.wmax = 1.2f; G.fillMany (outlineRibbons (wob (en, 0.6f, 0.03f, bs), 1.5f, bs, oo), K ({ 262, 30, 20 }, 0.6f)); }
        }
        const Poly base { { 101, 236 }, { 179, 236 }, { 175, 259 }, { 105, 259 } };
        { SolidOpt so; so.iw = 1.6f; so.sd = 7; so.shw = 3; Mat bm = look == Look::neon ? Mat { { 25, 100, 58 }, { 25, 100, 58 } } : Mat { { 250, 14, 26 }, { 256, 22, 12 }, {}, HSL { 36, 60, 60 } }; if (look == Look::neon) { bm.tube = HSL { 25, 100, 58 }; bm.glassA = 0.85f; } solid (G, base, bm, so); }
        { SolidOpt so; so.iw = 1.3f; so.sd = 8; so.shw = 1.5f; solid (G, { { 99, 233 }, { 181, 233 }, { 180, 240 }, { 100, 240 } }, MAT ("brass"), so); }
        { std::vector<Poly> pins; for (float d : { -28.0f, -14.0f, 0.0f, 14.0f, 28.0f }) pins.push_back ({ { 140 + d, 259 }, { 140 + d, 265 } }); G.strokeMany (pins, 2, look == Look::neon ? K ({ 40, 100, 60 }) : K ({ 42, 50, 50 }), false); }
        const int na = juce::roundToInt (air * 7);
        for (int i = 0; i < na; ++i)
        {
            const float c = std::fmod (((float) T.f / 24) * (0.5f + 0.3f * hsh (i, 2)) + hsh (i, 1), 1.0f), x = 140 + ((float) i - (float) (na - 1) / 2) * 7.5f + std::sin (c * 6 + (float) i) * 2.5f, y = top - 4 - c * (14 + 34 * air);
            LineOpt lo; lo.a = (1 - c) * 0.85f * mix; lo.ta = 0.1f; lo.tb = 0.5f; lo.halo = 0; LN (G, { { x, y }, { x + std::sin (c * 9 + (float) i) * 1.5f, y - 7 - 6 * air } }, 1.1f, { 46, 90, 88 }, lo);
        }
        if (! on) offVeil (G);
        stageOver (G);
    }
};
} // namespace phantom::pv
