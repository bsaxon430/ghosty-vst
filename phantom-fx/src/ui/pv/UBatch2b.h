// SPDX-License-Identifier: AGPL-3.0-or-later
// SPACE, SHIMMER and HALLS (port of phantom-anime/visuals/src/u_space.js, u_shimmer.js, u_halls.js).
#pragma once
#include "Hands.h"

namespace phantom::pv
{
// SPACE (SPECTRAL SPACE): night comes and the ghost stands under an open sky; every hit sends a ring of light out into it
class SpaceScene final : public Scene
{
public:
    std::vector<std::string> controls() const override { return { "size", "blend", "decay", "tone", "mod", "glimmer", "width", "predelay", "duck", "damp", "mix" }; }
    std::vector<float> defaults() const override { return { 0.5f, 0.6f, 0.5f, 0.5f, 0.4f, 0.2f, 1, 0.08f, 0, 0.4f, 1 }; }
    void scene (G& G, const Feed& F, State& S, const Clock& T) override
    {
        const Look look = G.look; const auto m = gmo (F, S, T); const bool on = m.on; const float lvl = m.lvl; const int k = m.k; const float mix = mixA (F.v ("mix")), nt = F.v ("tone") * 2 - 1, size = F.v ("size"), decay = F.v ("decay"), blend = F.v ("blend"), md = F.v ("mod"), damp = F.v ("damp");
        const float cx = 140, cy = 228, r0 = 34 + 56 * F.v ("predelay"), Rm = r0 + 62 + 150 * size, wx = 0.38f + 0.62f * F.v ("width"), duck = 1 - F.v ("duck") * juce::jmin (1.0f, lvl * 1.4f) * 0.8f;
        const HSL sky = nt < 0 ? mixrgb ({ 236, 60, 10 }, { 206, 70, 12 }, -nt) : mixrgb ({ 236, 60, 10 }, { 300, 50, 12 }, nt), near = nt < 0 ? mixrgb ({ 247, 100, 82 }, { 196, 100, 84 }, -nt) : mixrgb ({ 247, 100, 82 }, { 30, 100, 80 }, nt), far = mixrgb (near, { 24, 90, 60 }, damp * 0.8f);
        stageUnder (G);
        G.rect (0, 0, 280, 300, K (sky, (look == Look::neon ? 0.35f : 0.55f) * mix * (0.75f + 0.25f * duck))); G.rect (0, 0, 280, 120, K (sky, (look == Look::neon ? 0.2f : 0.3f) * mix));
        const V2 pole { 140, 96 }; const float rot = on ? (float) T.t * md * 0.22f : 0.0f, tr = 0.3f * decay * md; std::vector<Poly> st; const int gl = juce::roundToInt (F.v ("glimmer") * 9);
        for (int i = 0; i < 26; ++i) { const float a = hsh (i, 1) * TAU + rot, d = 30 + hsh (i, 2) * 150, x = pole.x + std::cos (a) * d * 1.15f, y = pole.y + std::sin (a) * d * 0.8f; if (y > 206 || x < 6 || x > 274 || y < 6) continue; st.push_back ({ { x, y }, { pole.x + std::cos (a - tr) * d * 1.15f, pole.y + std::sin (a - tr) * d * 0.8f } }); }
        if (tr > 0.02f) G.strokeMany (st, 1, K ({ 210, 60, 92 }, 0.4f * mix * duck));
        { std::vector<Poly> ss; for (size_t i = 0; i < st.size(); ++i) ss.push_back (star4 (st[i][0].x, st[i][0].y, 1.3f + 1.6f * hsh ((double) i, 5), 0.6f)); G.fillMany (ss, K ({ 210, 50, 95 }, 0.9f * mix * duck)); }
        if (! st.empty()) for (int i = 0; i < gl; ++i) { const auto& q = st[(size_t) std::floor (hsh (i, std::floor ((float) T.tick / 4) + 7) * (float) st.size())]; if (hsh (i, T.tick >> 1) > 0.3f) glint (G, q[0].x, q[0].y, hsh (i, 3) > 0.6f ? 0.8f : 0.5f, { 200, 60, 96 }, 0.9f * mix * duck); }
        if (blend > 0.05f) for (int i = 0; i < 4; ++i) { const float a = PI * (1.15f + 0.7f * (float) i / 3), r = Rm * 0.8f; glow (G, cx + std::cos (a) * r * wx, cy + std::sin (a) * r * 0.82f, 2.6f, far, blend * 0.3f * mix * duck * (0.6f + 0.4f * lvl)); }
        const int nR = 3 + juce::roundToInt (5 * decay); const float lw = lerp (1.5f, 2.6f, blend);
        auto arc = [&] (float r, float a, float w) { if (a < 0.03f) return; const float q = (r - r0) / (Rm - r0); lightLine (G, arcp (cx, cy, r * wx, r * 0.86f, PI * 1.02f, PI * 1.98f, 22), w, mixrgb (near, far, clampf (q, 0, 1)), a); };
        for (int i = 0; i < nR; ++i) { const float q = ((float) i + 0.5f) / (float) nR, r = r0 + (Rm - r0) * q * (0.55f + 0.45f * decay); arc (r, std::pow (1 - q, 1.6f - decay) * (0.2f + 0.25f * (1 - blend)) * mix * duck * (1 - damp * q * 0.6f), lw); }
        if (on) for (double t0 : S.hits) { const float age = (float) (T.t - t0), r = r0 + age * (90 + 120 * size); if (age < 0 || r > Rm * 1.05f) continue; arc (r, std::exp (-age / (0.3f + 2.4f * decay)) * 0.95f * mix * duck, lw + 1); }
        RigP P = m.P; P.s = 40; P.x = 140; P.y = 256 - lvl * 2; P.tail = 0.25f; const Rig R = ghostRig (P);
        glow (G, 140, 258, 2.1f, near, 0.3f + 0.2f * lvl, true); glow (G, R.mid.x, R.mid.y, 1.5f, near, (0.3f + 0.25f * lvl) * (on ? 1.0f : 0.4f));
        GL L; L.slime = HSL { 188, 78, 62 }; L.eyes = ! on ? "closed" : m.hp.on && ! m.hp.eyes.empty() ? m.hp.eyes : lvl > 0.3f ? "open" : "closed"; L.mouth = ! on ? "flat" : k >= 0 && k < 5 ? "o" : "smile"; L.blush = 0.55f; L.glow = 0;
        if (look == Look::neon) { L.tube = HSL { 247, 100, 74 }; L.slime = HSL { 188, 100, 60 }; L.k = 0.9f + 0.1f * hsh (T.tick, 5); } else { L.tint = sky; L.tintA = 0.16f * mix; }
        ghost (G, R, L, T); wear (G, R, "halo", T);
        if (! on) offVeil (G);
        stageOver (G);
    }
};

// SHIMMER: halos leave the ghost and climb, each a step above the last: smaller, brighter, higher
class ShimmerScene final : public Scene
{
public:
    std::vector<std::string> controls() const override { return { "size", "decay", "damp", "shimmer", "predelay", "mod", "width", "freeze", "interval", "hold", "mix" }; }
    std::vector<float> defaults() const override { return { 0.7f, 0.5f, 0.4f, 0.5f, 0.12f, 0.5f, 1, 0, 0, 0, 1 }; }
    void scene (G& G, const Feed& F, State& S, const Clock& T) override
    {
        const Look look = G.look; const auto m = gmo (F, S, T); const bool on = m.on; const float lvl = m.lvl; const int k = m.k; const float mix = mixA (F.v ("mix"));
        const int iv = juce::jlimit (0, 3, juce::roundToInt (F.v ("interval"))); const bool down = iv == 3, frz = F.v ("freeze") > 0.5f, hold = F.v ("hold") > 0.5f;
        const float sh = F.v ("shimmer") * mix, r0 = 24 + 38 * F.v ("size"), gy = down ? 196.0f : 254.0f, width = F.v ("width"), damp = F.v ("damp"), md = F.v ("mod"); const int n = 1 + juce::roundToInt (sh * 7);
        RigP P = m.P; P.s = 40; P.x = 140; P.y = gy - lvl * 2; P.tail = 0.25f; const Rig R = ghostRig (P);
        auto ratio = [&] (int j) { return iv == 0 ? 0.66f : iv == 1 ? 0.82f : iv == 2 ? (j % 2 ? 0.8f : 0.72f) : 1.36f; };
        const float reach = down ? 70.0f : 64 + 150 * F.v ("decay"), gap0 = 10 + 34 * F.v ("predelay"), ph = on && ! frz ? std::fmod ((float) T.tick / 12 * (0.35f + 0.25f * sh), 1.0f) : 0.0f;
        const HSL gold { 46, 100, 76 }, vio { 276, 100, 84 }, hot { 50, 80, 96 };
        stageUnder (G);
        G.rect (0, 0, 280, 300, K (L3<HSL> (look, { 262, 50, 10 }, { 268, 50, 10 }, { 262, 60, 5 }), (look == Look::neon ? 0.2f : 0.36f) * mix));
        glow (G, 140, gy + 2, 2.1f, vio, 0.3f + 0.2f * lvl, true);
        if (! down) for (int j = 0; j < 3; ++j) glow (G, 140, R.top.y - 26 - (float) j * (reach / 3.2f), 2.6f - (float) j * 0.5f, vio, (0.16f + 0.16f * sh) * (1 - (float) j * 0.25f) * mix * (0.6f + 0.4f * lvl));
        auto ring = [&] (float pos, float alive)
        {
            float r = r0, y = 0; for (int j = 0; j < (int) std::floor (pos); ++j) { y += r * (0.5f + 0.25f * width) + 6; r *= ratio (j); }
            const float f = pos - std::floor (pos), rn = r * ratio ((int) std::floor (pos)); y += (r * (0.5f + 0.25f * width) + 6) * f; r = lerp (r, rn, f);
            const float q = clampf (y / reach, 0, 1), a = (hold ? 1.0f : std::pow (1 - q, 0.8f)) * alive * (0.4f + 0.6f * sh) * mix * (on ? 0.75f + 0.25f * lvl : 0.55f); if (a < 0.03f || q >= 1) return;
            const HSL c = mixrgb (iv == 2 && (int) std::floor (pos) % 2 ? gold : vio, { 26, 90, 62 }, damp * q * 0.9f);
            const float sway = std::sin ((float) T.tick / 12 * 1.3f + pos * 1.7f) * md * 9 * q * (frz ? 0.0f : 1.0f), rx = r * (0.55f + 0.45f * width), ry = r * (down ? 0.3f : 0.2f + 0.12f * width);
            const float yy = down ? gy + 8 : R.top.y - gap0 - y, xx = 140 + sway; const auto pts = circ (xx, yy, down ? rx * 1.5f : rx, down ? ry * 1.3f : ry, 24);
            LightOpt lo; lo.closed = true; lo.halo = pos < 2 ? 1.0f : 0.0f; lightLine (G, pts, lerp (3.3f, 1.5f, q), mixrgb (c, hot, (1 - q) * 0.2f + q * 0.5f * (1 - damp)), a, lo);
            if ((frz || hold) && hsh (std::floor (pos * 3), T.tick >> 1) > 0.5f) glint (G, xx + std::cos (pos * 5) * rx, yy + std::sin (pos * 5) * ry, 0.5f, hot, a);
        };
        for (int j = n - 1; j >= 0; --j) ring ((float) j + ph, j == 0 ? ph : 1 - (j == n - 1 ? ph * 0.6f : 0.0f));
        glow (G, R.mid.x, R.mid.y, 1.5f, vio, (0.3f + 0.25f * lvl) * (on ? 1.0f : 0.4f));
        GL L; L.slime = HSL { 262, 68, 68 }; L.eyes = ! on ? "closed" : m.hp.on && ! m.hp.eyes.empty() ? m.hp.eyes : "happy"; L.mouth = ! on ? "flat" : k >= 0 && k < 5 ? "o" : "smile"; L.blush = 0.6f; L.glow = 0;
        if (look == Look::neon) { L.tube = HSL { 276, 100, 78 }; L.slime = HSL { 262, 100, 66 }; L.k = 0.9f + 0.1f * hsh (T.tick, 5); }
        ghost (G, R, L, T); wear (G, R, "halo", T);
        if (on && k >= 0 && k < 4 && ! down) { LightOpt lo; lo.closed = true; lo.halo = 1; lightLine (G, circ (140, R.top.y - gap0 + 6 - (float) k * 5, r0 * (0.55f + 0.45f * width) * (1 + (float) k * 0.06f), r0 * 0.22f, 22), 2.4f, hot, (1 - (float) k / 4) * mix, lo); }
        if (sh > 0.05f) for (int i = 0; i < juce::roundToInt (4 + sh * 8); ++i)
        {
            const float c = std::fmod ((frz ? 0.0f : (float) T.f / 24 * (0.12f + 0.1f * hsh (i, 2))) + hsh (i, 1), 1.0f), x = 140 + (hsh (i, 4) - 0.5f) * r0 * 1.6f * (1 - c * 0.5f), y = down ? gy + 4 + (hsh (i, 6) - 0.5f) * 14 : R.top.y - gap0 - c * reach;
            G.ell (x, y, 1.2f, 1.2f, K (hot, (hold ? 1.0f : 1 - c) * 0.85f * mix * (on ? 1.0f : 0.5f)));
        }
        if (frz && look != Look::neon) G.rect (0, 0, 280, 300, K ({ 200, 80, 90 }, 0.07f));
        if (! on) offVeil (G);
        stageOver (G);
    }
};

// HALLS (HAUNTED HALLS): a hall of light stands round the ghost, its arches going away down the stage
class HallsScene final : public Scene
{
public:
    std::vector<std::string> controls() const override { return { "mode", "size", "decay", "predelay", "diffusion", "damp", "lowcut", "mod", "width", "early", "shimmer", "freeze", "mix" }; }
    std::vector<float> defaults() const override { return { 1, 0.6f, 0.45f, 0.04f, 0.7f, 0.4f, 0.3f, 0.4f, 1, 0.5f, 0, 0, 1 }; }
    static Poly arch (int kind, float cx, float fy, float w, float h)
    {
        Poly o { { cx - w, fy } };
        if (kind == 0) { o.push_back ({ cx - w, fy - h }); o.push_back ({ cx + w, fy - h }); }
        else if (kind == 3)
        {
            const float sp = h * 0.5f, ky = (h - sp) / (1.732f * w);
            for (int i = 0; i <= 8; ++i) { const float f = PI - (float) i / 8 * PI / 3; o.push_back ({ cx + w + 2 * w * std::cos (f), fy - sp - 2 * w * std::sin (f) * ky }); }
            for (int i = 7; i >= 0; --i) { const float f = PI - (float) i / 8 * PI / 3; o.push_back ({ cx - w - 2 * w * std::cos (f), fy - sp - 2 * w * std::sin (f) * ky }); }
        }
        else { const float rise = kind == 2 ? h * 0.34f : juce::jmin (w, h * 0.6f), sp = h - rise; for (int i = 0; i <= 12; ++i) { const float a = PI + (float) i / 12 * PI; o.push_back ({ cx + std::cos (a) * w, fy - sp + std::sin (a) * rise }); } }
        o.push_back ({ cx + w, fy }); return o;
    }
    void scene (G& G, const Feed& F, State& S, const Clock& T) override
    {
        const Look look = G.look; const auto m = gmo (F, S, T); const bool on = m.on; const float lvl = m.lvl; const int k = m.k; const float mix = mixA (F.v ("mix"));
        const int md = juce::jlimit (0, 10, juce::roundToInt (F.v ("mode"))); const bool frz = F.v ("freeze") > 0.5f; const float size = F.v ("size"), decay = F.v ("decay"), pre = F.v ("predelay"), mod = F.v ("mod");
        const int kind = md == 0 ? 0 : md == 3 ? 2 : md == 4 ? 3 : 1; const V2 vp { 140, 132 }; const float fy0 = 262, W0 = (md == 3 ? 118.0f : 106.0f) * (0.5f + 0.5f * F.v ("width")), H0 = md == 0 ? 150.0f : md == 3 ? 170.0f : md == 4 ? 236.0f : 200.0f;
        const int n = 3 + juce::roundToInt (6 * size); const float g = 0.34f + 0.5f * size, dt = 0.09f + 0.2f * size;
        const HSL nearC = md == 10 ? HSL { 36, 50, 74 } : HSL { 227, 100, 84 }, farC = mixrgb (nearC, { 24, 80, 56 }, F.v ("damp") * 0.85f), veilC = L3<HSL> (look, { 226, 50, 8 }, { 262, 50, 8 }, { 250, 60, 4 });
        stageUnder (G);
        G.rect (0, 0, 280, 300, K (veilC, (look == Look::neon ? 0.25f : 0.5f) * mix * (md == 10 ? 1.25f : 1.0f)));
        const float sway = md == 10 ? std::sin ((float) T.tick / 12 * 0.9f) * 5 : std::sin ((float) T.tick / 12 * 0.6f) * mod * 3;
        auto lit = [&] (int j) { float a = 0; if (on) for (double t0 : S.hits) { const float age = (float) (T.t - t0) - (float) (md == 7 ? (n - 1 - j) : j) * dt - pre * 0.25f; if (age < 0) continue; if (md == 8 && age > 0.28f) continue; a = juce::jmax (a, frz ? 1.0f : std::exp (-age / (0.15f + 2.6f * decay))); } return a; };
        if (md == 2)
        {
            Poly pl; int i = 0; for (auto q : Poly { { 40, 70 }, { 240, 62 }, { 236, 214 }, { 44, 222 } }) { pl.push_back ({ q.x + sway * (i < 2 ? 1.0f : -0.5f), q.y }); ++i; }
            const float a = (0.3f + 0.5f * juce::jmax (lit (0), 0.2f)) * mix; G.fill (pl, K (nearC, 0.1f * a + 0.05f)); LightOpt lo; lo.closed = true; lo.halo = 1; lightLine (G, pl, 2, nearC, a, lo);
            for (int j = 0; j < 5; ++j) { const float y = 86 + (float) j * 26 + std::sin ((float) T.tick / 12 * 2 + (float) j) * 3 * (on ? 1.0f : 0.0f); lightLine (G, { { 48, y }, { 96, y + std::sin ((float) T.tick / 3 + (float) j) * 4 * lvl }, { 150, y - 3 }, { 232, y + 2 } }, 1.1f, nearC, a * (0.3f + 0.6f * lit (j))); }
        }
        else if (md == 5 || md == 6)
        {
            for (int j = n - 1; j >= 0; --j)
            {
                const float q = (float) j / (float) (n - 1 > 0 ? n - 1 : 1), sc = 1 / (1 + (float) j * g * 0.7f), a = (0.16f + 0.5f * lit (j)) * (1 - q * 0.5f) * mix;
                if (md == 5) { for (float sd : { -1.0f, 1.0f }) glow (G, vp.x + sd * W0 * 1.1f * sc + sway, vp.y + 60 * sc - 10, clampf (3.4f * sc, 0.5f, 3.4f), mixrgb (nearC, farC, q), a * 1.2f); }
                else { std::vector<Poly> pts; for (int i = 0; i < 9; ++i) { const float an = (float) i / 9 * TAU + (float) j * 0.7f + (float) T.tick / 12 * 0.12f * mod, r = (30 + (float) j * 22) * (0.6f + 0.4f * F.v ("width")); pts.push_back (star4 (vp.x + std::cos (an) * r * 1.3f, vp.y + 10 + std::sin (an) * r * 0.6f, 1.2f + 2 * hsh (i, j), 0.6f)); } G.fillMany (pts, K (mixrgb (nearC, farC, q), juce::jmin (1.0f, a * 2.4f))); }
            }
        }
        else
            for (int j = n - 1; j >= 0; --j)
            {
                const float q = (float) j / (float) (n - 1 > 0 ? n - 1 : 1), z = pre * 1.6f + (float) j * g, sc = 1 / (1 + z), nl0 = md == 9 ? (hsh (j, 3) - 0.5f) * 26 : 0.0f, nl1 = md == 9 ? (hsh (j, 5) - 0.5f) * 0.3f : 0.0f;
                const float cx = vp.x + (140 - vp.x) * sc + sway * (1 - sc) + nl0 * sc, fy = vp.y + (fy0 - vp.y) * sc;
                Poly p = arch (kind, cx, fy, W0 * sc, H0 * sc);
                if (md == 9) p = xf (p, [&] (float x, float y) { return V2 { cx + (x - cx) * std::cos (nl1) - (y - fy) * std::sin (nl1), fy + (x - cx) * std::sin (nl1) + (y - fy) * std::cos (nl1) }; });
                const float base = (j == 0 ? 0.25f + 0.6f * F.v ("early") : 0.3f) * std::exp (-q * (1.6f - 1.2f * decay)), a = clampf (base * 0.7f + lit (j) * 0.85f, 0, 1) * mix; const HSL c = mixrgb (nearC, farC, q);
                G.fill (p, K (mixrgb (veilC, c, 0.3f), (0.05f + 0.13f * F.v ("diffusion")) * mix));
                LightOpt lo; lo.halo = j < 2 ? 1.0f : 0.0f; lightLine (G, p, lerp (2.6f, 1.1f, q) * (md == 3 ? 1.5f : 1.0f), c, a, lo);
                if (md == 8 && j == 2 && on && ! S.hits.empty())
                {
                    const float age = (float) (T.t - S.hits.back());
                    if (age > 0.28f) { const float gy = juce::jmin (1.0f, (age - 0.28f) * 6); std::vector<Poly> bars; for (int i = 0; i < 7; ++i) { const float x = cx - W0 * sc + 2 * W0 * sc * ((float) i + 0.5f) / 7; bars.push_back ({ { x, fy - H0 * sc }, { x, fy - H0 * sc * (1 - gy) } }); } G.strokeMany (bars, 1.6f, K (L3<HSL> (look, { 262, 30, 20 }, INKC, { 262, 60, 60 }), 0.9f)); }
                }
            }
        if (md == 4) { const float q = 1 / (1 + pre * 1.6f + (float) (n - 1) * g); const V2 rc { vp.x, vp.y + (fy0 - vp.y) * q - H0 * q * 0.62f }; glow (G, rc.x, rc.y, 1.5f, { 46, 100, 80 }, (0.4f + 0.4f * lit (n - 1)) * mix); LightOpt lo; lo.closed = true; lightLine (G, circ (rc.x, rc.y, 9, 9, 12), 1.2f, { 46, 100, 80 }, 0.9f * mix, lo); }
        const float fog = (1 - F.v ("lowcut")) * mix; if (fog > 0.05f) for (int i = 0; i < 3; ++i) glow (G, 50 + (float) i * 90 + sway * 2, 262, 2.1f, mixrgb (nearC, { 226, 30, 80 }, 0.5f), fog * 0.4f, true);
        RigP P = m.P; P.s = 38; P.x = 140; P.y = 262 - lvl * 2; P.tail = 0.25f; const Rig R = ghostRig (P);
        glow (G, R.mid.x, R.mid.y, 1.5f, nearC, (0.3f + 0.25f * lvl) * (on ? 1.0f : 0.4f));
        GL L; L.slime = HSL { 188, 78, 62 }; L.eyes = ! on ? "closed" : m.hp.on && ! m.hp.eyes.empty() ? m.hp.eyes : "open"; L.mouth = ! on ? "flat" : k >= 0 && k < 5 ? "o" : "smile"; L.blush = 0.55f; L.glow = 0; L.tint = veilC; L.tintA = 0.14f * mix;
        if (look == Look::neon) { L.tube = HSL { 227, 100, 74 }; L.slime = HSL { 188, 100, 60 }; L.tint.reset(); L.k = 0.9f + 0.1f * hsh (T.tick, 5); }
        ghost (G, R, L, T);
        if (F.v ("shimmer") > 0.03f) for (int i = 0; i < juce::roundToInt (F.v ("shimmer") * 10); ++i) { const float c = std::fmod (((float) T.f / 24) * (0.14f + 0.12f * hsh (i, 2)) + hsh (i, 1), 1.0f); glint (G, 140 + (hsh (i, 4) - 0.5f) * W0 * 1.5f * (1 - c * 0.4f), 230 - c * 170, hsh (i, 5) > 0.6f ? 0.8f : 0.5f, { 50, 70, 96 }, (1 - c) * 0.9f * mix); }
        if (! on) offVeil (G);
        stageOver (G);
    }
};
} // namespace phantom::pv
