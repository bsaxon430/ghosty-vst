// SPDX-License-Identifier: AGPL-3.0-or-later
// FATTEN (port of phantom-anime/visuals/src/u_fatten.js): the ghost sits on something that has to carry it and is redrawn fatter.
#pragma once
#include "Hands.h"

namespace phantom::pv
{
class FattenScene final : public Scene
{
public:
    std::vector<std::string> controls() const override { return { "fat", "mode", "color", "crunch", "drive", "punch", "lowclean", "out", "mix" }; }
    std::vector<float> defaults() const override { return { 0.4f, 0, 0.5f, 0.2f, 0, 0, 0, 0.5f, 1 }; }
    // what it sits on, per look. Y = the seat's top, f = fat, q = how hard it is pressed now
    static void seat (G& G, float Y, float f, float q, const Clock& T)
    {
        const float cx = 140;
        if (G.isDawn())
        {
            const float rx = 58 + 18 * f + 5 * q, hc = 21 - 6 * f - 2 * q, dr = 4 + 9 * f + 3 * q, sd = 7 + (float) boilOf (T, 4) * 3.3f, fl = 237, sw = 15 + 5 * f;
            Poly st { { cx - sw, Y + hc - 2 }, { cx - sw - 2 - 2 * f, (Y + hc + fl) / 2 }, { cx - sw - 4, fl }, { cx + sw + 4, fl }, { cx + sw + 2 + 2 * f, (Y + hc + fl) / 2 }, { cx + sw, Y + hc - 2 } };
            { SolidOpt so; so.shw = 7; so.sd = sd; solid (G, smooth (st, true, 4), { { 44, 42, 86 }, { 32, 26, 60 }, {}, HSL { 50, 80, 92 } }, so); }
            Poly gil; for (int i = 0; i <= 12; ++i) { const float t = (float) i / 12, x = cx - rx * 0.92f + rx * 1.84f * t; gil.push_back ({ x, Y + hc + dr * std::pow (std::abs (t - 0.5f) * 2, 2.0f) - 1 }); }
            for (int i = 12; i >= 0; --i) { const float t = (float) i / 12, x = cx - rx * 0.8f + rx * 1.6f * t; gil.push_back ({ x, Y + hc + 7 + dr * 0.5f * std::pow (std::abs (t - 0.5f) * 2, 2.0f) }); }
            G.fill (wob (gil, 0.5f, 0.05f, sd), K ({ 34, 34, 52 }));
            std::vector<Poly> gl; for (int i = 0; i < 9; ++i) { const float t = ((float) i + 0.5f) / 9, x0 = cx - rx * 0.86f + rx * 1.72f * t, x1 = cx - rx * 0.7f + rx * 1.4f * t; gl.push_back ({ { x0, Y + hc + dr * std::pow (std::abs (t - 0.5f) * 2, 2.0f) }, { x1, Y + hc + 6 + dr * 0.5f * std::pow (std::abs (t - 0.5f) * 2, 2.0f) } }); }
            G.strokeMany (gl, 0.8f, K ({ 30, 30, 38 }, 0.6f));
            Poly cap; for (int i = 0; i <= 16; ++i) { const float a = PI + (float) i / 16 * PI, c = std::cos (a), s = std::sin (a); cap.push_back ({ cx + c * rx, Y + hc + s * hc * 1.05f + dr * std::pow (std::abs (c), 3.0f) }); }
            for (int i = 10; i >= 0; --i) { const float t = (float) i / 10; cap.push_back ({ cx - rx + rx * 2 * t, Y + hc + 2.5f + dr * std::pow (std::abs (t - 0.5f) * 2, 2.4f) - 1.5f * std::sin (t * PI) }); }
            const auto cp = wob (smooth (cap, true, 3), 0.6f, 0.05f, sd);
            { SolidOpt so; so.shw = 5; so.sd = sd; so.rimw = 1.8f; solid (G, cp, { { 10, 66, 46 }, { 6, 58, 30 }, {}, HSL { 28, 90, 66 } }, so); }
            { LitOpt lo; lo.bias = 0.5f; lo.pw = 1.5f; G.fill (litShape (cp, 11, lo), K ({ 16, 74, 56 }, 0.85f)); }
            static const float dots[7][3] { { -0.62f, 0.62f, 0.11f }, { -0.3f, 0.3f, 0.14f }, { 0.08f, 0.2f, 0.12f }, { 0.42f, 0.44f, 0.13f }, { 0.72f, 0.72f, 0.09f }, { -0.05f, 0.7f, 0.08f }, { -0.84f, 0.86f, 0.06f } };
            for (auto& d : dots) G.fill (blobp (cx + d[0] * rx, Y + hc - hc * (1 - d[1]) * 0.92f + dr * std::pow (std::abs (d[0]), 3.0f), d[2] * rx * 0.5f, d[2] * hc * 0.9f, 0.18f, 8, d[0] * 9), K ({ 46, 64, 88 }, 0.95f));
            G.sprite ("fatten/tuft", cx, fl + 1);
        }
        else if (G.isCel())
        {
            const float fl = 238, sd = 4 + (float) boilOf (T, 3) * 5.1f, bt = 216; const Mat metal { { 188, 22, 52 }, { 204, 26, 33 } }, brass { { 42, 62, 58 }, { 28, 54, 36 } };
            Poly sp; const int n = 9; for (int i = 0; i <= n; ++i) sp.push_back ({ cx + (i == 0 || i == n ? 0.0f : (i % 2 ? -11.0f : 11.0f)), lerp (Y + 6, bt + 1, (float) i / (float) n) });
            G.stroke (sp, 3.2f, K (INKC)); G.stroke (sp, 1.5f, K ({ 44, 30, 74 }));
            { SolidOpt so; so.iw = 1.6f; so.sd = sd; solid (G, wob ({ { cx - 52, fl }, { cx - 42, bt }, { cx + 42, bt }, { cx + 52, fl } }, 0.5f, 0.05f, sd), metal, so); }
            { SolidOpt so; so.iw = 1.3f; so.sd = sd + 1; so.shw = 1.4f; solid (G, wob ({ { cx - 46, bt + 1.5f }, { cx - 44, bt - 3 }, { cx + 44, bt - 3 }, { cx + 46, bt + 1.5f } }, 0.4f, 0.05f, sd + 1), brass, so); }
            G.fill (circ (cx, bt + 12, 10.5f, 9.5f, 16), K (INKC)); G.fill (circ (cx, bt + 12, 9, 8, 16), K ({ 46, 50, 90 }));
            std::vector<Poly> ticks; for (int i = 0; i < 7; ++i) { const float a = PI * 1.15f + (float) i / 6 * PI * 0.7f, c = std::cos (a), s = std::sin (a); ticks.push_back ({ { cx + c * 7.6f, bt + 12 + s * 6.8f }, { cx + c * 5.6f, bt + 12 + s * 5 } }); }
            G.strokeMany (ticks, 0.9f, K (INKC));
            const float na = PI * 1.15f + clampf (f + q * 0.12f, 0, 1.08f) * PI * 0.7f; G.stroke ({ { cx, bt + 12 }, { cx + std::cos (na) * 7.4f, bt + 12 + std::sin (na) * 6.6f } }, 1.5f, K ({ 356, 80, 52 })); G.ell (cx, bt + 12, 1.5f, 1.5f, K (INKC));
            Poly pl; for (int i = 0; i <= 12; ++i) { const float a = PI + (float) i / 12 * PI; pl.push_back ({ cx + std::cos (a) * 56, Y + 2 + std::sin (a) * 4.5f }); }
            pl.push_back ({ cx + 56, Y + 6 }); pl.push_back ({ cx + 48, Y + 9.5f }); pl.push_back ({ cx - 48, Y + 9.5f }); pl.push_back ({ cx - 56, Y + 6 });
            { SolidOpt so; so.iw = 1.6f; so.sd = sd + 2; so.shw = 2; solid (G, wob (smooth (pl, true, 2), 0.4f, 0.05f, sd + 2), brass, so); }
            G.fill (circ (cx, Y + 2.5f, 50, 3, 14), K ({ 48, 72, 72 }));
        }
        else
        {
            const float fl = 238, kk = 0.9f + 0.1f * hsh (T.tick, 3); const HSL cy { 186, 100, 56 };
            TubeOpt c; c.closed = true; c.k = kk; tubeLine (G, circ (cx, fl, 72, 13, 26), 1.5f, cy, c);
            for (int i = 0; i < 4; ++i) { const float t = ((float) i + 0.5f) / 4, y = lerp (fl - 3, Y + 9, t), r = lerp (44, 34, t); TubeOpt o = c; o.halo = 0; tubeLine (G, circ (cx, y, r, r * 0.2f, 20), 1.1f, { 200.0f + (float) i * 18, 100, 62 }, o); }
            G.fill (circ (cx, Y + 3, 54, 7.5f, 22), K (cy, 0.16f)); tubeLine (G, circ (cx, Y + 3, 54, 7.5f, 22), 1.3f, cy, c);
            const int lit = juce::roundToInt (clampf (f + q * 0.1f, 0, 1) * 10);
            for (int i = 0; i < 10; ++i) { const float y = fl - 6 - (float) i * 7.2f; const bool on = i < lit; const HSL cc = i < 5 ? HSL { 140, 100, 56 } : i < 8 ? HSL { 38, 100, 58 } : HSL { 322, 100, 60 }; G.rect (22, y, 9, 4.6f, on ? K (cc, 0.95f) : K ({ 240, 40, 22 }, 0.9f)); if (on) G.rect (19.5f, y - 1.6f, 14, 7.8f, K (cc, 0.16f)); }
        }
    }
    void scene (G& G, const Feed& F, State& S, const Clock& T) override
    {
        const bool on = F.on; const float fat = F.v ("fat"), n = F.v ("color") * 2 - 1, crunch = F.v ("crunch"), drive = F.v ("drive"), out = F.v ("out"), mix = F.v ("mix");
        const int mode = juce::roundToInt (F.v ("mode")); const bool punch = F.v ("punch") > 0.5f || mode == 6, clean = F.v ("lowclean") > 0.5f;
        const float lvl = on ? F.level : 0, low = on ? F.lowOr() : 0; const int k = on ? T.tick - S.hitTick : -1; const auto hp = hitPose (k, fat, punch);
        const int idleB = on ? (int) std::floor ((float) T.tick / 9) % 2 : 0; const float wf = 3.2f - 2.0f * fat, jel = std::sin ((float) T.tick / 12 * wf * TAU) * (0.012f + 0.05f * fat) * juce::jmin (1.0f, lvl * 1.3f);
        float sx = 1 + jel + (idleB ? 0.012f : 0.0f), sy = 1 - jel * 0.8f - (idleB ? 0.014f : 0.0f), lift = 0, hem = idleB ? 0.5f : 0.0f, belly = jel * 1.5f, press = fat * 0.25f + std::abs (jel) * 2;
        if (hp.on) { sx = hp.sx; sy = hp.sy; lift = hp.lift; hem = hp.hem; belly = hp.belly; press = hp.ph == "squat" ? 1.0f : hp.ph == "up" ? 0.0f : 0.5f; }
        const float Y = 194 + 14 * fat + 3 * press * (0.4f + fat), hover = 9 * (1 - sstep (0.05f, 0.3f, fat)) * (hp.on && hp.ph == "squat" ? 0.2f : 1.0f) + (fat < 0.3f && idleB ? 1.5f : 0.0f);
        std::string eyes = fat >= 0.6f ? "happy" : "open", mouth = fat >= 0.85f ? "o" : fat >= 0.3f ? "grin" : "smile";
        if (hp.on && ! hp.eyes.empty() && ! (hp.eyes == "wide" && fat >= 0.6f)) eyes = hp.eyes;
        if (! on) { eyes = "closed"; mouth = "flat"; }
        RigP P; P.s = 42; P.x = 140; P.y = Y - hover; P.fat = fat; P.sx = sx; P.sy = sy; P.lift = lift; P.hem = hem; P.belly = belly; P.tail = 0.25f; P.rough = crunch;
        P.rsd = lvl > 0.04f ? std::floor ((float) T.tick / 2) : 0.0f; P.bite = crunch > 0.3f && hp.on && hp.ph == "squat" ? 1.0f : 0.0f; P.flat = mode == 3 ? 0.35f : 0.0f;
        if (mode == 7) { P.hasStrings = true; P.strings = { 0.44f, 0.63f, 0.82f }; }
        const Rig R = ghostRig (P);
        const HSL cool { 198, 90, 72 }, warm { 28, 96, 60 }, tint = n < 0 ? cool : warm; const float am = std::abs (n); const HSL halo = mixrgb ({ 50, 64, 88 }, tint, am * 0.92f); const float reach = 0.75f + 0.6f * out;
        GL L; L.slime = HSL { 318, 78, 68 }; L.eyes = eyes; L.mouth = mouth; L.gloss = drive > 0.02f ? drive : 0.0f; L.blush = 0.5f + 0.4f * drive;
        if (G.isCel()) L.shade = mixrgb (pal::celShade, n < 0 ? HSL { 214, 52, 74 } : HSL { 14, 58, 77 }, am * 0.9f);
        if (G.isDawn()) { L.shade = mixrgb (pal::dawnShade, n < 0 ? HSL { 212, 46, 64 } : HSL { 16, 52, 66 }, am * 0.9f); L.glow = 0; L.rim = mixrgb (pal::dawnRim, tint, am * 0.6f); }
        if (G.isNeon()) { L.tube = mixrgb ({ 322, 100, 60 }, n < 0 ? HSL { 194, 100, 62 } : HSL { 26, 100, 56 }, am); L.heat = drive; L.k = 0.9f + 0.1f * hsh (T.tick, 5); L.glow = 0; L.slime = HSL { 140, 100, 56 }; }
        if (mode == 4 || mode == 7) { L.tint = HSL { 14, 92, 60 }; L.tintA = 0.14f + 0.2f * fat; if (G.isNeon()) L.heat = juce::jmax (L.heat, 0.5f); }
        stageUnder (G);
        glow (G, 140, 238, 1.5f + 0.5f * out, mixrgb ({ 48, 70, 70 }, tint, am), 0.3f + 0.45f * out, true);
        if (mode == 1 || mode == 7)
        {
            const float r = 1 + 0.28f * low; const HSL c = G.isNeon() ? HSL { 268, 100, 62 } : G.isDawn() ? HSL { 262, 40, 22 } : HSL { 268, 40, 24 };
            if (! G.isNeon()) G.ell (140, 247, 62 * r, 9 * r, K ({ 268, 44, 10 }, 0.55f));
            for (float rr_ : { 1.0f, 1.32f }) { LineOpt lo; lo.closed = true; lo.a = 0.9f / rr_; lo.sd = 3; lo.ta = 0; lo.tb = 0; lo.halo = 1; line (G, circ (140, 247, 62 * r * rr_, 9 * r * rr_, 22), 2, c, lo); }
        }
        if (clean) { glow (G, 140, 239, 2.1f, { 190, 80, 84 }, 0.6f, true); LineOpt lo; lo.closed = true; lo.a = 0.85f; lo.sd = 5; lo.ta = 0; lo.tb = 0; lo.halo = 1; line (G, circ (140, 239, 88, 12.5f, 26), 1.3f, { 188, 90, 84 }, lo); }
        glow (G, R.mid.x, R.mid.y, clampf (R.halfW / 21 * reach, 0.75f, 4.4f), halo, (0.42f + 0.22f * out) * (1 + 0.25f * lvl) * (on ? 1.0f : 0.4f));
        if (mode == 5 || mode == 7)
            for (float sd_ : { -1.0f, 1.0f })
            {
                RigP P2 = P; P2.x = 140 + sd_ * (R.halfW * 0.62f + 9); P2.y = P.y - 2; P2.s = 38; P2.rough = 0; P2.hasStrings = false; P2.strings.clear(); P2.sd = 9 + sd_;
                const Rig R2 = ghostRig (P2); GL L2 = L; L2.simple = G.isNeon(); L2.eyes = "closed"; L2.mouth = "flat"; L2.gloss = 0; L2.blush = 0.3f;
                if (G.isNeon()) L2.tint.reset(); else L2.tint = HSL { 262, 36, 34 };
                L2.tintA = 0.2f; L2.glow = 0; L2.k = 0.5f; L2.iw = 0.85f; L2.sd = 11 + sd_; ghost (G, R2, L2, T);
            }
        seat (G, Y, fat, clampf (press, 0, 1), T);
        ghost (G, R, L, T);
        if (mode == 2)
        {
            const auto t0 = R.across (0.585f, 0.045f, 14, 0, 1), t1 = R.across (0.665f, 0.075f, 14, 0, 1); Poly top, bot;
            for (int i = 0; i <= 14; ++i) { const float e = std::pow (std::abs ((float) i / 14 - 0.5f) * 2, 3.0f) * 0.42f; top.push_back ({ lerp (t0[(size_t) i].x, t1[(size_t) i].x, e), lerp (t0[(size_t) i].y, t1[(size_t) i].y, e) }); bot.push_back ({ lerp (t1[(size_t) i].x, t0[(size_t) i].x, e), lerp (t1[(size_t) i].y, t0[(size_t) i].y, e) }); }
            const HSL bc = n < 0 ? HSL { 196, 80, 56 } : HSL { 334, 82, 60 };
            { SolidOpt so; so.iw = 1.3f; so.sd = 6; so.shw = 2.2f; so.k = 1; solid (G, concat (top, reversed (bot)), { bc, { bc.h, bc.s, bc.l - 20 }, {}, HSL { bc.h, 90, 80 }, bc }, so); }
            const auto b0 = R.across (0.57f, 0.045f, 12, 0.52f, 0.63f), b1 = reversed (R.across (0.68f, 0.075f, 12, 0.52f, 0.63f));
            { SolidOpt so; so.iw = 1.1f; so.sd = 8; so.shw = 1.6f; solid (G, concat (b0, b1), { { 46, 90, 66 }, { 34, 80, 44 }, {}, {}, HSL { 46, 100, 62 } }, so); }
            glow (G, (b0[6].x + b1[6].x) / 2, (b0[6].y + b1[6].y) / 2, 0.75f, { 46, 100, 70 }, 0.25f + 0.5f * lvl);
        }
        if (mode == 7)
            for (float u : P.strings)
            {
                const auto ln = R.across (u, 0.03f, 12, 0.01f, 0.99f); const HSL c { 36, 56, G.isNeon() ? 62.0f : 72.0f };
                { LineOpt lo; lo.sd = u * 9; lo.ta = 0.02f; lo.tb = 0.02f; line (G, ln, 1.7f, c, lo); }
                const auto q = ln[8]; LineOpt lo; lo.sd = 1 + u; lo.ta = 0.1f; lo.tb = 0.1f; line (G, { { q.x - 3.4f, q.y - 3.2f }, q, { q.x + 3.6f, q.y - 3 } }, 1.3f, c, lo);
            }
        if (mode == 3)
        {
            const float tx = R.top.x, ty = R.top.y + 3; Poly w { { tx - 25, ty }, { tx - 19, ty - 21 }, { tx + 19, ty - 21 }, { tx + 25, ty } };
            { LineOpt lo; lo.sd = 2; lo.ta = 0.02f; lo.tb = 0.02f; line (G, arcp (tx, ty - 21, 8, 8, PI, TAU, 8), 2.2f, { 250, 16, G.isNeon() ? 60.0f : 30.0f }, lo); }
            SolidOpt so; so.iw = 1.5f; so.sd = 3; so.shw = 3.2f; solid (G, smooth (w, true, 2), { { 250, 14, 40 }, { 252, 20, 22 }, {}, HSL { 240, 20, 62 }, HSL { 250, 60, 70 } }, so);
        }
        const int nh = juce::roundToInt (drive * 5);
        for (int i = 0; i < nh; ++i)
        {
            const float x0 = R.top.x + ((float) i - (float) (nh - 1) / 2) * 13, y0 = R.top.y - 7; Poly p;
            for (int j = 0; j <= 6; ++j) p.push_back ({ x0 + std::sin ((float) j * 1.15f + (float) T.f * 0.3f + (float) i * 1.7f) * 2.6f * ((float) j / 6), y0 - (float) j * 4.2f });
            LineOpt lo; lo.a = 0.8f; lo.sd = (float) i; lo.ta = 0.15f; lo.tb = 0.5f; line (G, p, 1.5f, { 22, 95, G.isDawn() ? 72.0f : 64.0f }, lo);
        }
        if (drive > 0.3f && k >= 0 && k < 5) for (float sd_ : { -1.0f, 1.0f }) puff (G, R.top.x + sd_ * (R.halfW * 0.55f + 6), R.top.y + 10 - (float) k * 2.6f, k, drive > 0.7f ? 1.5f : 1.0f, K (G.isNeon() ? HSL { 190, 80, 84 } : HSL { 48, 50, 95 }), 0.9f);
        const int nc = crunch < 0.25f ? 0 : 2 + juce::roundToInt (crunch * 9 * juce::jmin (1.0f, lvl * 1.6f)), ph = (int) std::floor ((float) T.tick / 3), age = T.tick % 3;
        for (int i = 0; i < nc; ++i)
        {
            const float a = hsh (i, ph) * TAU, d = 1.02f + (float) age * 0.14f + hsh (i, ph + 7) * 0.1f, x = R.mid.x + std::cos (a) * R.halfW * d, y = R.mid.y + std::sin (a) * R.H * R.s * 0.48f * d, r = 1.5f + hsh (i, ph + 3) * 1.8f, rot = hsh (i, ph + 5) * 6;
            G.fill ({ { x + std::cos (rot) * r, y + std::sin (rot) * r }, { x + std::cos (rot + 2.2f) * r, y + std::sin (rot + 2.2f) * r }, { x + std::cos (rot + 4.3f) * r * 0.8f, y + std::sin (rot + 4.3f) * r * 0.8f } },
                    G.isNeon() ? K (*L.tube, 0.9f) : K (i % 3 == 0 ? HSL { 318, 78, 68 } : HSL { 46, 40, 95 }, 1));
        }
        if (punch && k >= 2 && k < 5)
        {
            static const float kks[3] { 1, 1.25f, 0.8f }; const float kk = kks[k - 2] * (mode == 6 ? 1.45f : 1.0f), hh = R.H * R.s * 0.5f;
            for (int i = 0; i < 6; ++i)
            {
                const float sd_ = i < 3 ? -1.0f : 1.0f, j = (float) (i % 3) - 1, yy = R.mid.y + j * hh * 0.5f, x0 = R.mid.x + sd_ * (R.halfW * sx + 7), len = (11 + 7 * (1 - std::abs (j))) * kk;
                const Poly wdg { { x0, yy - 2.6f }, { x0 + sd_ * len, yy + j * len * 0.4f }, { x0, yy + 2.6f } };
                if (G.isNeon()) tubeLine (G, wdg, 1.1f, { 52, 100, 70 });
                else { SolidOpt so; so.iw = 1.1f; so.sd = (float) i; so.shw = 1; solid (G, wdg, { { 50, 100, 78 }, { 40, 100, 62 }, {}, HSL { 52, 100, 92 } }, so); }
            }
        }
        if (! clean && k >= 0 && k < 5) for (float sd_ : { -1.0f, 1.0f }) puff (G, 140 + sd_ * (52 + R.halfW * 0.25f + (float) k * 3), 236, k, 1, K (G.isNeon() ? HSL { 268, 80, 70 } : G.isDawn() ? HSL { 70, 30, 70 } : HSL { 272, 20, 62 }), 0.75f);
        if (mix < 0.98f)
        {
            RigP P0; P0.s = 42; P0.x = 140; P0.y = P.y; P0.sx = sx; P0.sy = sy; P0.lift = lift; const Rig R0 = ghostRig (P0); StrokeOpt so; so.closed = true; so.dash = { 4, 3.2f };
            G.stroke (R0.body, 1.3f, G.isNeon() ? K ({ 200, 90, 80 }, (1 - mix) * 0.9f) : K (G.isDawn() ? HSL { 262, 30, 34 } : INKC, (1 - mix) * 0.75f), so);
        }
        if (! on) offVeil (G);
        stageOver (G);
    }
};
} // namespace phantom::pv
