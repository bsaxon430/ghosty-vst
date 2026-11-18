// SPDX-License-Identifier: AGPL-3.0-or-later
// WRECK and BASS (port of phantom-anime/visuals/src/u_wreck.js, u_bass.js).
#pragma once
#include "Hands.h"

namespace phantom::pv
{
// WRECK: the ghost stands in front of a furnace mouth. DRIVE is the heat: the fire climbs, cracks spread through the furnace and the ghost
class WreckScene final : public Scene
{
public:
    std::vector<std::string> controls() const override { return { "style", "drive", "tone", "lowcut", "highcut", "thump", "steep", "punish", "bias", "sag", "out", "mix" }; }
    std::vector<float> defaults() const override { return { 0, 0.3f, 0.5f, 0, 1, 0, 0, 0, 0.5f, 0, 0.667f, 1 }; }
    static Poly arch (float cx, float y, float w, float h, int n = 14)
    { Poly o { { cx - w, y } }; const float r = w, yc = y - h + r; for (int i = 0; i <= n; ++i) { const float a = PI + (float) i / (float) n * PI; o.push_back ({ cx + std::cos (a) * r, yc + std::sin (a) * r }); } o.push_back ({ cx + w, y }); return o; }
    void scene (G& G, const Feed& F, State& S, const Clock& T) override
    {
        const Look look = G.look; const auto m = gmo (F, S, T); const bool on = m.on; const float lvl = m.lvl; const int k = m.k; const float mix = mixA (F.v ("mix"));
        const int st = juce::jlimit (0, 8, juce::roundToInt (F.v ("style"))); const float drive = F.v ("drive") * mix, nt = F.v ("tone") * 2 - 1, nb = F.v ("bias") * 2 - 1, sag = F.v ("sag"); const bool pun = F.v ("punish") > 0.5f;
        const float sagE = on && sag > 0.02f && ! S.hits.empty() ? std::exp (-(float) (T.t - S.hits.back()) / 1.1f) * sag : 0.0f;
        const float shx = on && pun && lvl > 0.08f ? (hsh (T.tick, 1) - 0.5f) * 4 : 0.0f, shy = on && pun && lvl > 0.08f ? (hsh (T.tick, 2) - 0.5f) * 3 : 0.0f, sx0 = 140 + shx, sy0 = shy;
        const bool soft = st == 0, viol = st == 4; const int ck = st == 2 ? 1 : st == 3 ? 2 : 0;
        const float fl = (on ? (0.8f + 0.3f * lvl) * (k >= 0 && k < 3 ? 1.2f : 1.0f) : 0.45f) * (1 - 0.45f * sagE) * (pun ? 1.2f : 1.0f);
        auto tint = [&] (HSL c) { if (viol) c = mixrgb (c, { 286, 90, 64 }, 0.6f); if (st == 1) c = mixrgb (c, { 28, 100, 58 }, 0.4f); return nt < 0 ? mixrgb (c, { 200, 80, 84 }, -nt * 0.5f) : mixrgb (c, { 6, 100, 46 }, nt * 0.4f); };
        const HSL hc = tint (heatC (drive)), hot = mixrgb (hc, { 52, 100, 92 }, 0.55f), dk = L3<HSL> (look, { 262, 30, 14 }, INKC, { 262, 60, 30 });
        const float bs = 3 + (float) boilOf (T, look == Look::dawn ? 4 : 3) * 1.7f, fy = 238 + sy0;
        stageUnder (G);
        glow (G, sx0, 150 + sy0, 4.4f, hc, (0.1f + 0.3f * F.v ("out")) * (0.25f + 0.9f * drive) * fl);
        const auto body = smooth ({ { sx0 - 104, fy + 4 }, { sx0 - 106, 120 + sy0 }, { sx0 - 84, 74 + sy0 }, { sx0 - 40, 54 + sy0 }, { sx0 + 40, 54 + sy0 }, { sx0 + 84, 74 + sy0 }, { sx0 + 106, 120 + sy0 }, { sx0 + 104, fy + 4 } }, true, 2);
        const auto mouth = arch (sx0, fy, 62, 150);
        if (look == Look::neon) { G.fill (body, K (pal::neonGlass, 0.8f)); TubeOpt to; to.closed = true; to.halo = 0; to.k = 0.7f; tubeLine (G, body, 1.2f, { 262, 80, 56 }, to); }
        else
        {
            SolidOpt so; so.iw = 1.9f; so.sd = bs; so.shw = 7; so.rimw = 2.2f; solid (G, wob (body, 0.7f, 0.04f, bs), look == Look::dawn ? Mat { { 38, 14, 52 }, { 252, 18, 26 }, {}, HSL { 46, 70, 80 } } : Mat { { 250, 16, 44 }, { 256, 26, 24 } }, so);
            G.strokeMany ({ { { sx0 - 104, 150 + sy0 }, { sx0 - 78, 152 + sy0 } }, { { sx0 + 78, 150 + sy0 }, { sx0 + 105, 148 + sy0 } }, { { sx0 - 92, 196 + sy0 }, { sx0 - 77, 196 + sy0 } }, { { sx0 + 77, 198 + sy0 }, { sx0 + 96, 197 + sy0 } }, { { sx0 - 60, 62 + sy0 }, { sx0 - 52, 82 + sy0 } }, { { sx0 + 56, 60 + sy0 }, { sx0 + 50, 80 + sy0 } }, { { sx0 - 2, 55 + sy0 }, { sx0, 70 + sy0 } } }, 1.3f, K (dk, 0.5f));
            if (look == Look::dawn) { const float ms[4][5] { { -62, 22, 110, 44, 32 }, { -10, 26, 124, 42, 28 }, { 48, 20, 96, 50, 38 }, { 84, 12, 112, 46, 34 } }; for (auto& q : ms) G.fill (blobp (sx0 + q[0], (std::abs (q[0]) > 70 ? 80.0f : 58.0f) + sy0, q[1], 5.5f, 0.25f, 10, q[0]), K ({ q[2], q[3], q[4] })); }
            else { std::vector<Poly> rv; for (auto q : Poly { { -92, 112 }, { -96, 176 }, { -92, 226 }, { 92, 112 }, { 96, 176 }, { 92, 226 }, { -64, 70 }, { 64, 70 } }) rv.push_back (circ (sx0 + q.x, q.y + sy0, 2.3f, 2.3f, 7)); G.fillMany (rv, K (dk)); }
        }
        const float chx = sx0 + 62, chy = 58 + sy0; const Poly chim { { chx - 11, chy + 6 }, { chx - 9, chy - 26 }, { chx + 9, chy - 26 }, { chx + 11, chy + 4 } };
        if (look == Look::neon) { TubeOpt to; to.halo = 0; to.k = 0.7f; tubeLine (G, chim, 1, { 262, 80, 56 }, to); }
        else { SolidOpt so; so.iw = 1.5f; so.sd = bs + 3; so.shw = 3; solid (G, chim, look == Look::dawn ? Mat { { 30, 16, 46 }, { 250, 18, 24 }, {}, HSL { 46, 70, 80 } } : MAT ("iron"), so); }
        if (on && drive > 0.25f) for (int j = 0; j < 2; ++j) { const float c = std::fmod (((float) T.tick / 12) * 0.55f + (float) j * 0.5f, 1.0f); puff (G, chx + 3 + c * 10, chy - 32 - c * 30, juce::jmin (4, (int) std::floor (c * 5)), drive > 0.6f ? 1.5f : 1.0f, K (L3<HSL> (look, { 60, 20, 88 }, { 268, 16, 74 }, { 262, 60, 70 })), 0.8f * (1 - c * 0.5f)); }
        G.fill (mouth, K (mixrgb ({ 258, 60, 6 }, hc, 0.12f + 0.2f * drive)));
        const float fh = (40 + 122 * drive) * fl, ph = on ? std::floor ((float) T.tick / 2) : 0.0f;
        auto tong = [&] (int n, float hk, float wk, float salt)
        {
            Poly p { { sx0 - 56 * wk, fy } };
            for (int i = 0; i < n; ++i)
            {
                const float x = sx0 - 56 * wk + 112 * wk * ((float) i + 0.5f) / (float) n + nb * 14 * hk, hh = fh * hk * (0.55f + 0.45f * hsh (i, ph + salt)) * (1 - 0.22f * std::pow (std::abs (((float) i + 0.5f) / (float) n - 0.5f) * 2, 2.0f));
                const float xl = sx0 - 56 * wk + 112 * wk * (float) i / (float) n, xr = sx0 - 56 * wk + 112 * wk * (float) (i + 1) / (float) n;
                if (st == 7) { for (auto q : Poly { { xl, fy - hh * 0.55f }, { xl + (xr - xl) * 0.3f, fy - hh * 0.55f }, { xl + (xr - xl) * 0.3f, fy - hh }, { xl + (xr - xl) * 0.7f, fy - hh }, { xl + (xr - xl) * 0.7f, fy - hh * 0.5f }, { xr, fy - hh * 0.5f } }) p.push_back (q); }
                else if (st == 6 || st == 3) { for (auto q : Poly { { xl, fy - hh * 0.3f }, { x - 3, fy - hh * 0.7f }, { x, fy - hh }, { x + 3, fy - hh * 0.62f }, { xr, fy - hh * 0.3f } }) p.push_back (q); }
                else for (auto q : Poly { { xl, fy - hh * 0.28f }, { x - (xr - xl) * 0.2f, fy - hh * 0.74f }, { x + nb * 5, fy - hh }, { x + (xr - xl) * 0.24f, fy - hh * 0.6f } }) p.push_back (q);
            }
            p.push_back ({ sx0 + 56 * wk, fy }); return st == 7 || st == 6 || st == 3 ? p : smooth (p, true, 2);
        };
        if (drive > 0.02f)
        {
            glow (G, sx0, fy - fh * 0.35f, clampf (0.75f + 2 * drive, 0.75f, 2.6f), hc, (0.15f + 0.15f * drive) * juce::jmin (1.1f, fl));
            G.fill (tong (5, 1, 1, 3), K (tint (heatC (drive * 0.42f)), 0.96f)); G.fill (tong (4, 0.74f, 0.82f, 5), K (tint (heatC (0.2f + drive * 0.6f)))); G.fill (tong (3, 0.46f, 0.6f, 7), K (tint (heatC (juce::jmin (1.0f, 0.45f + drive * 0.6f)))));
        }
        if (st == 1 && drive > 0.05f) for (float dx : { -22.0f, 22.0f }) lightLine (G, arcp (sx0 + dx, fy - 22, 9, 16, PI * 0.9f, PI * 2.1f, 10), 1.6f, { 30, 100, 62 }, 0.9f * juce::jmin (1.0f, fl));
        const auto ro = arch (sx0, fy, 76, 166); const auto ring = concat (ro, reversed (mouth));
        if (look == Look::neon) { G.fill (ring, K (hc, 0.1f + 0.2f * drive)); TubeOpt to; to.k = 0.5f + 0.5f * drive; to.heat = drive * 0.5f; tubeLine (G, ro, 1.5f, hc, to); TubeOpt t2; t2.halo = 0; tubeLine (G, mouth, 1.1f, hot, t2); }
        else
        {
            SolidOpt so; so.iw = 1.7f; so.sd = bs + 1; so.shw = 4; so.rimw = 1.8f; solid (G, ring, look == Look::dawn ? Mat { { 36, 14, 58 }, { 258, 18, 30 }, {}, HSL { 46, 90, 84 } } : MAT ("brass"), so);
            std::vector<Poly> jn; for (int i = 0; i < 9; ++i) { const float a = PI + ((float) i + 0.5f) / 9 * PI, c = std::cos (a), s_ = std::sin (a), yc = fy - 166 + 76; jn.push_back ({ { sx0 + c * 62, (fy - 150 + 62) + s_ * 62 }, { sx0 + c * 76, yc + s_ * 76 } }); } G.strokeMany (jn, 1.2f, K (dk, 0.8f));
        }
        const int nc = soft || st == 1 || viol ? juce::roundToInt (drive * 3) : juce::roundToInt (drive * 10); std::vector<Poly> cr;
        for (int i = 0; i < nc; ++i) { const float a = PI * (1.02f + 0.96f * hsh (i, 11)) + nb * 0.25f, r0 = 78, x = sx0 + std::cos (a) * r0, y = (fy - 166 + 76) + std::sin (a) * r0; cr.push_back (crackP (x, y, a + (hsh (i, 5) - 0.5f) * 0.5f, (14 + 34 * hsh (i, 7)) * (0.5f + drive), (float) (i * 3 + st), ck)); }
        if (! cr.empty()) { G.strokeMany (cr, st == 2 ? 1.5f : 2.6f, K (dk, 0.92f)); if (drive > 0.4f) G.strokeMany (cr, st == 2 ? 0.7f : 1.1f, K (hot, juce::jmin (1.0f, (drive - 0.4f) * 2.2f) * juce::jmin (1.0f, fl))); }
        if (st == 8) for (int i = 0; i < juce::roundToInt (drive * 7); ++i) { const float a = PI * (1.05f + 0.9f * hsh (i, 21)), r = 92 + hsh (i, 23) * 10, rr = 2.5f + 3 * hsh (i, 25); G.ell (sx0 + std::cos (a) * r, (fy - 90) + std::sin (a) * r * 0.9f, rr, rr, K (mixrgb ({ 258, 60, 6 }, hc, 0.2f))); }
        auto dkC = [&] (float a) { return K (L3<HSL> (look, { 200, 60, 6 }, { 262, 50, 7 }, { 250, 60, 4 }), a); };
        const bool stp = F.v ("steep") > 0.5f; const float lo = F.v ("lowcut"), hi = 1 - F.v ("highcut");
        if (lo > 0.02f) { const float y = 300 - lo * 120; if (stp) G.rect (0, y, 280, 300 - y, dkC (0.72f)); else for (int j = 0; j < 3; ++j) G.rect (0, y + (float) j * 14, 280, 300 - y - (float) j * 14, dkC (0.3f)); }
        if (hi > 0.02f) { const float y = hi * 130; if (stp) G.rect (0, 0, 280, y, dkC (0.72f)); else for (int j = 0; j < 3; ++j) G.rect (0, 0, 280, y - (float) j * 14, dkC (0.3f)); }
        if (F.v ("thump") > 0.5f && on && k >= 0 && k < 5) { LightOpt lo2; lo2.closed = true; lightLine (G, circ (sx0, 254, 46 + (float) k * 17, 8 + (float) k * 3, 20), 2, hc, (1 - (float) k / 5) * 0.9f, lo2); }
        RigP P = m.P; P.s = 44; P.x = 140 + shx + nb * 4; P.y = 252 + sy0 - lvl * 2; P.tail = 0.25f; P.lean = nb * 0.06f; P.rough = clampf (drive * (st == 5 ? 1.15f : 0.8f), 0, 1); P.rsd = lvl > 0.04f ? std::floor ((float) T.tick / 2) : 0.0f; P.block = st == 7 ? drive * 0.6f : 0.0f; P.seg = st == 7 ? 3 : 5; P.sy *= 1 - 0.13f * sagE;
        const Rig R = ghostRig (P);
        GL L; L.slime = HSL { 84, 80, 56 }; L.eyes = ! on ? "closed" : drive > 0.82f || pun ? "x" : m.hp.on && ! m.hp.eyes.empty() ? m.hp.eyes : drive < 0.25f ? "happy" : "open"; L.mouth = ! on ? "flat" : drive > 0.6f ? "o" : "smile"; L.blush = 0.5f + 0.4f * drive; L.glow = 0; L.tint = hc; L.tintA = 0.06f + 0.2f * drive;
        if (look == Look::neon) { L.tube = mixrgb ({ 38, 100, 60 }, hc, 0.5f); L.slime = HSL { 84, 100, 56 }; L.heat = drive * 0.6f; L.tint.reset(); L.k = 0.9f + 0.1f * hsh (T.tick, 5); }
        glow (G, R.mid.x, R.mid.y, 2, hc, (0.2f + 0.4f * drive) * fl); ghost (G, R, L, T); wear (G, R, "horns", T);
        const HSL lineC = look == Look::neon ? *L.tube : dk;
        if (st == 5 && drive > 0.05f) { std::vector<Poly> hp; for (size_t i = 0; i < R.body.size(); i += 2) { const auto q = R.body[i]; const float dx = q.x - R.mid.x, dy = q.y - R.mid.y, l = hyp (dx, dy) != 0 ? hyp (dx, dy) : 1, len = (2 + 7 * drive) * (0.4f + hsh ((double) i, ph)); hp.push_back ({ q, { q.x + dx / l * len + (hsh ((double) i, 3) - 0.5f) * 3, q.y + dy / l * len } }); } G.strokeMany (hp, 1, K (lineC, 0.85f)); }
        if (st == 6 && drive > 0.05f) { const float us[3] { 0.4f, 0.62f, 0.8f }; const int nu = 1 + juce::roundToInt (drive * 2); for (int ui = 0; ui < nu && ui < 3; ++ui) { auto ln = R.across (us[ui], 0.02f, 10, 0.06f, 0.94f); for (size_t i = 0; i < ln.size(); ++i) ln[i].y += (i % 2 ? -1.0f : 1.0f) * (1.5f + 3 * drive); G.stroke (ln, 1.2f, K (lineC, 0.7f)); } }
        if (st == 8) for (int i = 0; i < juce::roundToInt (drive * 6); ++i) { const auto q = R.X ((hsh (i, 31) - 0.5f) * 1.1f, -0.5f - hsh (i, 33) * 1.2f); const float rr = 2 + 2.5f * hsh (i, 35); G.ell (q.x, q.y, rr, rr, K (mixrgb ({ 258, 60, 8 }, hc, 0.25f), 0.9f)); }
        if (drive > 0.55f && st != 0) { const auto c1 = crackP (R.top.x - 6, R.top.y + 10, 1.2f, 16 + 20 * drive, 5, ck), c2 = crackP (R.mid.x + R.halfW * 0.5f, R.mid.y + 4, 2.6f, 10 + 16 * drive, 9, ck); G.strokeMany ({ c1, c2 }, 1.3f, K (look == Look::neon ? hot : dk, 0.85f)); }
        if (on && drive > 0.35f) for (int i = 0; i < juce::roundToInt (drive * 8); ++i) { const float c = std::fmod (((float) T.f / 24) * (0.4f + 0.4f * hsh (i, 2)) + hsh (i, 1), 1.0f), x = sx0 + (hsh (i, 4) - 0.5f) * 110 + std::sin (c * 7 + (float) i) * 5, y = fy - 10 - c * (70 + 110 * drive); G.ell (x, y, 1.3f, 1.3f, K (hot, (1 - c) * 0.9f)); }
        if (on && pun && k >= 0 && k < 5) { CrumbOpt co; co.a0 = -2.6f; co.a1 = -0.5f; co.sp = 9; co.g = 0.5f; co.r = 1.9f; crumbs (G, R.top.x, R.top.y + 12, 9, (float) S.hitTick, (float) k, [&] (int i) { return K (i % 2 ? hot : hc); }, co); }
        if (! on) offVeil (G);
        stageOver (G);
    }
};

// BASS (BASS LAB): a cut through the floor. Under it the sub, a big cone that thumps; on the floor the ghost (the mid: it growls);
// over its head the edge, a crown of spikes
class BassScene final : public Scene
{
public:
    std::vector<std::string> controls() const override { return { "xlo", "xhi", "lowwidth", "sub", "growl", "gfreq", "gmove", "gdiv", "edge", "translate", "low", "mid", "high", "lowsat", "mix" }; }   // (OUTPUT: "not shown", SPEC.md)
    std::vector<float> defaults() const override { return { 0.5f, 0.5f, 1, 0, 0, 0.5f, 0, 6, 0, 0, 0.6f, 0.6f, 0.6f, 0, 1 }; }
    void scene (G& G, const Feed& F, State& S, const Clock& T) override
    {
        const Look look = G.look; const auto m = gmo (F, S, T); const bool on = m.on; const float lvl = m.lvl, mix = mixA (F.v ("mix")), low = m.low, mid = m.mid, high = m.high;
        const float yF = 204 - (F.v ("xlo") - 0.5f) * 26, lw = F.v ("lowwidth"), sub = F.v ("sub") * mix, growl = F.v ("growl") * mix, edge = F.v ("edge") * mix, lL = F.v ("low") / 0.6f, lM = F.v ("mid") / 0.6f, lH = F.v ("high") / 0.6f, lowsat = F.v ("lowsat");
        const float bs = 3 + (float) boilOf (T, look == Look::dawn ? 4 : 3) * 1.7f; const HSL dk = L3<HSL> (look, { 262, 30, 14 }, INKC, { 262, 60, 30 }), oc { 19, 100, 60 }; const float thump = 1 + 0.16f * low * lL;
        stageUnder (G);
        const Poly cut { { 0, yF }, { 280, yF }, { 280, 300 }, { 0, 300 } };
        G.fill (cut, K (L3<HSL> (look, { 24, 30, 13 }, { 266, 34, 11 }, { 250, 60, 5 }), look == Look::neon ? 0.8f : 1.0f));
        if (look == Look::dawn)
        {
            G.fillMany ({ blobp (30, yF + 26, 9, 5, 0.2f, 8, 30), blobp (226, yF + 40, 12, 6, 0.2f, 8, 226), blobp (64, yF + 70, 7, 4, 0.2f, 8, 64), blobp (250, yF + 80, 8, 5, 0.2f, 8, 250) }, K ({ 30, 16, 30 }));
            G.strokeMany ({ { { 14, yF }, { 22, yF + 18 }, { 18, yF + 40 } }, { { 262, yF }, { 254, yF + 22 }, { 262, yF + 50 }, { 256, yF + 66 } }, { { 96, yF }, { 92, yF + 12 } }, { { 190, yF }, { 196, yF + 16 }, { 190, yF + 26 } } }, 1.6f, K ({ 30, 30, 26 }));
            G.fill (wob ({ { 0, yF - 3 }, { 280, yF - 3 }, { 280, yF + 5 }, { 0, yF + 5 } }, 1.2f, 0.06f, bs), K ({ 104, 46, 30 }));
        }
        else if (look == Look::cel)
        {
            std::vector<Poly> js; for (float x : { 40.0f, 100.0f, 180.0f, 240.0f }) js.push_back ({ { x, yF + 6 }, { x, 300 } }); G.strokeMany (js, 5, K ({ 268, 26, 18 }));
            const Poly bd { { 0, yF - 2 }, { 280, yF - 2 }, { 280, yF + 6 }, { 0, yF + 6 } }; G.fill (bd, K ({ 30, 40, 40 })); G.fillMany (outlineRibbons (bd, 1.5f, bs), K (INKC));
        }
        else { TubeOpt to; to.k = 0.9f; tubeLine (G, { { 0, yF }, { 280, yF } }, 1.3f, { 186, 100, 56 }, to); std::vector<Poly> ls; for (float x : { 56.0f, 112.0f, 168.0f, 224.0f }) ls.push_back ({ { x, yF }, { x + (x - 140) * 0.5f, 300 } }); G.strokeMany (ls, 0.7f, K ({ 262, 80, 50 }, 0.5f)); }
        const float cy = yF + 44, rx = (22 + 92 * lw) * thump, ry = (9 + 13 * lw) * thump;
        auto cone = [&] (float yy, float rxx, float ryy, float a, bool hotE)
        {
            const auto rim = circ (140, yy, rxx, ryy, 26), inner = circ (140, yy + ryy * 0.3f, rxx * 0.5f, ryy * 0.52f, 18), cap = circ (140, yy + ryy * 0.42f, rxx * 0.17f, ryy * 0.2f, 10);
            if (look == Look::neon)
            { G.fill (rim, K (oc, 0.1f * a)); TubeOpt to; to.closed = true; to.a = a; to.heat = low * 0.5f; tubeLine (G, rim, 1.5f, hotE ? HSL { 8, 100, 58 } : oc, to); TubeOpt t2; t2.closed = true; t2.a = a; t2.halo = 0; tubeLine (G, inner, 1, { 30, 100, 58 }, t2); tubeLine (G, cap, 0.9f, { 40, 100, 66 }, t2); }
            else
            {
                SolidOpt so; so.iw = 1.7f; so.sd = bs + yy; so.shw = 3.5f; so.a = a; solid (G, rim, { L3<HSL> (look, { 24, 26, 36 }, { 258, 20, 36 }, {}), L3<HSL> (look, { 20, 30, 16 }, { 262, 28, 16 }, {}), {}, HSL { 30, 90, 70 } }, so);
                G.fill (inner, K (L3<HSL> (look, { 20, 30, 10 }, { 264, 34, 9 }, {}), a)); G.fill (cap, K (mixrgb (L3<HSL> (look, { 28, 40, 34 }, { 30, 40, 40 }, {}), { 24, 100, 60 }, 0.3f + 0.5f * low * lL), a));
            }
            std::vector<Poly> ribs; for (int i = 0; i < 10; ++i) { const float an = (float) i / 10 * TAU + 0.3f; ribs.push_back ({ { 140 + std::cos (an) * rxx * 0.9f, yy + std::sin (an) * ryy * 0.9f }, { 140 + std::cos (an) * rxx * 0.52f, yy + ryy * 0.3f + std::sin (an) * ryy * 0.54f } }); }
            if (look == Look::neon) G.strokeMany (ribs, 0.7f, K (oc, 0.5f * a)); else { G.strokeMany (ribs, 1, K (dk, 0.45f * a)); G.ring (140, yy, rxx * 0.9f, ryy * 0.9f, 1.2f, K (L3<HSL> (look, { 30, 40, 50 }, { 262, 20, 52 }, {}), 0.8f * a)); }
            if (hotE) { auto rg = circ (140, yy, rxx * 1.04f, ryy * 1.08f, 26); for (size_t i = 0; i < rg.size(); ++i) { rg[i].x += (hsh ((double) i, T.tick >> 1) - 0.5f) * 5 * lowsat; rg[i].y += (hsh ((double) i + 40, T.tick >> 1) - 0.5f) * 3 * lowsat; } LightOpt lo; lo.closed = true; lightLine (G, rg, 1.3f + lowsat, { 10, 100, 58 }, lowsat * a, lo); }
        };
        glow (G, 140, cy, 2.1f, { 19, 100, 56 }, (0.2f + 0.5f * low) * lL * mix, true);
        if (sub > 0.03f) { cone (cy + 34, rx * (0.55f + 0.5f * sub), ry * (0.55f + 0.5f * sub), 0.5f + 0.5f * sub, false); glow (G, 140, cy + 34, 1.5f, { 2, 100, 50 }, sub * (0.25f + 0.5f * low), true); }
        cone (cy, rx, ry, 1, lowsat > 0.03f);
        if (on) for (int j = 0; j < 3; ++j) { const float c = std::fmod (((float) T.tick / 12) * 1.1f + (float) j / 3, 1.0f); LightOpt lo; lo.closed = true; lightLine (G, circ (140, cy, rx * (1.05f + c * 0.5f), ry * (1.05f + c * 0.7f), 22), 1.1f, oc, (1 - c) * 0.5f * low * lL * mix, lo); }
        const float gm = on && F.v ("gmove") > 0.02f ? 0.5f + 0.5f * std::sin ((float) T.tick / 12 * 2 / DIVB[juce::jlimit (0, 15, juce::roundToInt (F.v ("gdiv")))] * TAU) : 1.0f, gk = growl * (1 - F.v ("gmove") * (1 - gm));
        RigP P = m.P; P.s = 42; P.x = 140; P.y = yF + 2 - lvl * 2; P.tail = 0.25f; P.fat = 0.1f; const Rig R = ghostRig (P);
        glow (G, R.mid.x, R.mid.y, 1.5f, { 24, 100, 70 }, (0.14f + 0.3f * mid) * lM * (on ? 1.0f : 0.4f));
        GL L; L.slime = HSL { 146, 72, 56 }; L.eyes = ! on ? "closed" : gk > 0.5f ? "squeeze" : m.hp.on && ! m.hp.eyes.empty() ? m.hp.eyes : "open";
        if (! on) L.mouth = "flat"; else if (gk > 0.06f) L.vowel = lerp (1.0001f, 0.2f, F.v ("gfreq") * gk); else L.mouth = "smile";
        L.mouthK = 1 + 1.5f * gk * (0.7f + 0.3f * juce::jmin (1.0f, mid * 2)); L.blush = 0.5f; L.glow = 0;
        if (look == Look::neon) { L.tube = HSL { 19, 100, 60 }; L.slime = HSL { 146, 100, 56 }; L.k = 0.9f + 0.1f * hsh (T.tick, 5); }
        ghost (G, R, L, T);
        if (gk > 0.25f && on)
        {
            const auto mb = bbox (mouthShape (R, L.mouth, L.mouthK, L.vowel).fill); const float mw = mb[2] - mb[0], mh = mb[3] - mb[1]; std::vector<Poly> tt;
            for (int i = 0; i < 5; ++i) { const float x = mb[0] + mw * (0.14f + 0.72f * (float) i / 5), w = mw * 0.72f / 5; tt.push_back ({ { x, mb[1] + mh * 0.1f }, { x + w, mb[1] + mh * 0.1f }, { x + w / 2, mb[1] + mh * 0.1f + mh * 0.3f } }); }
            for (int i = 0; i < 4; ++i) { const float x = mb[0] + mw * (0.2f + 0.6f * (float) i / 4), w = mw * 0.6f / 4; tt.push_back ({ { x, mb[3] - mh * 0.08f }, { x + w, mb[3] - mh * 0.08f }, { x + w / 2, mb[3] - mh * 0.08f - mh * 0.24f } }); }
            if (look == Look::neon) { std::vector<Poly> ps; for (auto& q : tt) ps.push_back ({ q[0], q[2], q[1] }); G.strokeMany (ps, 1, K ({ 50, 60, 92 }, 0.95f)); } else G.fillMany (tt, K ({ 50, 60, 97 }));
        }
        if (gk > 0.3f && on) for (float sd : { -1.0f, 1.0f }) { const auto q = R.X (R.mouth.x + sd * 0.5f, R.mouth.y + 0.05f); for (int j = 0; j < 2; ++j) { LineOpt lo; lo.a = gk * (0.8f - 0.3f * (float) j) * (0.5f + 0.5f * mid); lo.ta = 0.2f; lo.tb = 0.2f; lo.halo = 0; LN (G, arcp (q.x + sd * (4 + (float) j * 6), q.y, 4 + (float) j * 3, 7 + (float) j * 4, sd > 0 ? -0.7f : PI - 0.7f, sd > 0 ? 0.7f : PI + 0.7f, 6), 1.4f, oc, lo); } }
        const float yH = R.top.y - 4 - (F.v ("xhi") - 0.5f) * 22; { std::vector<Poly> ds; for (int i = 0; i < 12; ++i) ds.push_back ({ { 22 + (float) i * 20, yH }, { 32 + (float) i * 20, yH } }); G.strokeMany (ds, 1, K (L3<HSL> (look, { 50, 40, 90 }, { 45, 40, 92 }, { 186, 100, 70 }), 0.4f)); }
        if (edge > 0.03f)
        {
            const int n = 5 + juce::roundToInt (edge * 6); std::vector<Poly> sp;
            for (int i = 0; i < n; ++i) { const float a = PI * (1.12f + 0.76f * (float) i / (float) (n - 1)), len = (7 + 24 * edge) * (0.7f + 0.5f * hsh (i, 3)) * (1 + 0.5f * high * lH), bx = R.top.x + std::cos (a) * R.halfW * 0.9f, by = R.top.y + 12 + std::sin (a) * 14, wd = 2.6f + 2 * edge; sp.push_back ({ { bx - std::sin (a) * wd, by + std::cos (a) * wd }, { bx + std::cos (a) * len, by + std::sin (a) * len }, { bx + std::sin (a) * wd, by - std::cos (a) * wd } }); }
            if (look == Look::neon) { for (auto& q : sp) { TubeOpt to; to.halo = 0; to.k = 0.6f + 0.4f * high; tubeLine (G, q, 1, { 46, 100, 64 }, to); } }
            else { G.fillMany (sp, K (mixrgb ({ 40, 100, 58 }, { 50, 100, 88 }, high * lH))); std::vector<Poly> rs; for (auto& q : sp) rs.push_back (ribbon (concat (q, { q[0] }), 1.3f, 5, 0.05f, 0.05f)); G.fillMany (rs, K (dk, look == Look::dawn ? 0.6f : 1.0f)); }
            if (on && high > 0.25f) for (int i = 0; i < n; i += 2) glint (G, sp[(size_t) i][1].x, sp[(size_t) i][1].y, 0.5f, { 50, 80, 96 }, high * lH);
        }
        if (F.v ("translate") > 0.03f)
        {
            const float tx = 238, ty = 44, a = F.v ("translate"); const auto box = rbox (tx - 16, ty - 13, 32, 26, 4, 2);
            if (look == Look::neon) { TubeOpt to; to.closed = true; to.halo = 0; to.a = 0.4f + 0.6f * a; tubeLine (G, box, 1, { 186, 100, 60 }, to); to.a = a; tubeLine (G, circ (tx, ty, 7, 7, 12), 1, oc, to); }
            else { SolidOpt so; so.iw = 1.4f; so.sd = 5; so.shw = 2; so.a = 0.5f + 0.5f * a; solid (G, box, MAT ("wood"), so); G.ell (tx, ty, 8, 8, K (dk, 0.5f + 0.5f * a)); G.ell (tx, ty, 3, 3, K ({ 24, 100, 60 }, a)); }
            if (on) for (int j = 0; j < 2; ++j) { LineOpt lo; lo.a = a * (0.3f + 0.7f * low) * (1 - 0.3f * (float) j); lo.ta = 0.2f; lo.tb = 0.2f; lo.halo = 0; LN (G, arcp (tx - 20 - (float) j * 6, ty, 5 + (float) j * 4, 9 + (float) j * 5, PI - 0.8f, PI + 0.8f, 6), 1.2f, oc, lo); }
        }
        if (! on) offVeil (G);
        stageOver (G);
    }
};
} // namespace phantom::pv
