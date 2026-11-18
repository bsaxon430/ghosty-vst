// SPDX-License-Identifier: AGPL-3.0-or-later
// CHORUS and FORMANT (port of phantom-anime/visuals/src/u_chorus.js, u_formant.js).
#pragma once
#include "Hands.h"

namespace phantom::pv
{
// CHORUS: one ghost sings on a small riser; its voices are see-through copies that circle it
class ChorusScene final : public Scene
{
public:
    std::vector<std::string> controls() const override { return { "rate", "depth", "delay", "fb", "width", "voices", "mode", "mix" }; }
    std::vector<float> defaults() const override { return { 0.2f, 0.5f, 0.36f, 0, 1, 3, 0, 1 }; }
    static G::Layer voice (G& G, int si, const Clock& T)
    {
        static const float ss[4] { 25, 29, 33, 37 }; const float s = ss[si], pad = s * 1.4f, w = s * 2.6f + pad, h = s * 3 + pad; const Look look = G.look;
        return G.layer ("chorus-v" + juce::String (si) + "|" + juce::String (boilOf (T, look == Look::dawn ? 4 : 3)), w, h, w * 0.5f, h - pad * 0.5f, [&] (pv::G& g)
        {
            RigP P; P.s = s; P.x = 0; P.y = 0; P.tail = 0.2f; P.sd = 5.0f + (float) si; const Rig R = ghostRig (P);
            GL L; L.eyes = "closed"; L.mouth = "o"; L.mouthK = 0.85f; L.blush = 0; L.glow = 0; L.gloss = 0; L.tex = 0.4f; L.slime = HSL { 190, 76, 66 };
            if (look == Look::cel) { L.body = HSL { 196, 70, 92 }; L.shade = HSL { 214, 46, 78 }; L.ink = HSL { 206, 60, 34 }; }
            if (look == Look::dawn) { L.body = HSL { 196, 56, 91 }; L.shade = HSL { 214, 34, 71 }; L.ink = HSL { 206, 45, 30 }; L.faceInk = HSL { 206, 45, 26 }; L.rim = HSL { 190, 90, 93 }; L.bounce = HSL { 170, 50, 74 }; }
            if (look == Look::neon) { L.tube = HSL { 199, 100, 70 }; L.slime = HSL { 186, 100, 62 }; L.glass = 0.25f; L.k = 0.9f; }
            ghost (g, R, L, T);
        });
    }
    void scene (G& G, const Feed& F, State& S, const Clock& T) override
    {
        const Look look = G.look; const auto m = gmo (F, S, T); const bool on = m.on; const float lvl = m.lvl; const int k = m.k; const float mix = mixA (F.v ("mix"));
        const int n = juce::jlimit (1, 8, juce::roundToInt (F.v ("voices"))); const bool ens = juce::roundToInt (F.v ("mode")) == 1; const float depth = F.v ("depth"), width = F.v ("width"), fb = F.v ("fb"), dl = F.v ("delay");
        const HSL acc { 199, 100, 75 };
        const float RX = (28 + 84 * depth) * (0.16f + 0.84f * width) * (1 + 0.06f * lvl), RY = 10 + 18 * depth, cx = 140, cy = 238 - RY - 24 * dl, base = 0.9f - 0.16f * dl;
        const float rps = 0.06f + 1.1f * std::pow (F.v ("rate"), 1.5f), tt = on ? (float) T.tick / 12 : 0.0f; const int nt = juce::roundToInt (fb * 4);
        static const float hops[7] { 0, 0, 5, 7, 4, 1, 0 }; const float hop = k >= 0 && k < 7 ? hops[k] : 0.0f;
        struct Vq { float sn, x, y, sc; int j, i; float a; }; std::vector<Vq> vs;
        static const float spdA[3] { 1, 1.21f, 0.83f }, tiltA[3] { 0, 0.22f, -0.22f }, rkA[3] { 1, 0.8f, 1.12f };
        for (int i = 0; i < n; ++i)
        {
            const int ring = ens ? i % 3 : 0; const float spd = spdA[ring], tilt = ens ? tiltA[ring] : 0.0f, rk = ens ? rkA[ring] : 1.0f;
            for (int j = nt; j >= 0; --j)
            {
                const float ph = ((float) i / (float) n + (ens ? (float) ring / 9 : 0.0f) + tt * rps * spd) * TAU - (float) j * 0.21f, c = std::cos (ph), sn = std::sin (ph), ex = c * RX * rk, ey = sn * RY * rk, sc = base * (1 + 0.16f * sn);
                vs.push_back ({ sn - (float) j * 0.001f, cx + ex * std::cos (tilt) - ey * std::sin (tilt), cy + ex * std::sin (tilt) + ey * std::cos (tilt) - std::sin (ph * 2 + (float) i * 1.7f) * depth * 4 - (j ? 0.0f : hop), sc, j, i,
                               (j ? std::pow (0.5f, (float) j) * 0.7f : 1.0f) * (0.44f + 0.2f * (sn * 0.5f + 0.5f)) * mix * (0.8f + 0.2f * lvl) * (k >= 0 && k < 3 && ! j ? 1.3f : 1.0f) });
            }
        }
        std::stable_sort (vs.begin(), vs.end(), [] (const Vq& a, const Vq& b) { return a.sn < b.sn; });
        stageUnder (G);
        glow (G, 140, 250, 2.1f, { 199, 80, 76 }, 0.3f + 0.2f * lvl, true);
        for (int j = 0; j < 2; ++j) { const float r = 62 + (float) j * 30 + lvl * 10; G.ring (140, 196, r, r * 0.92f, 1.2f, K (acc, (0.16f - 0.05f * (float) j) * (0.5f + lvl) * (on ? 1.0f : 0.4f))); }
        if (mix > 0.02f)
            for (int r = 0; r < (ens ? 3 : 1); ++r)
            {
                const auto p = circ (cx, cy, RX * (ens ? rkA[r] : 1.0f), RY * (ens ? rkA[r] : 1.0f), 30, ens ? tiltA[r] : 0.0f);
                if (look == Look::neon) { TubeOpt to; to.closed = true; to.a = 0.5f * mix; to.halo = 0; tubeLine (G, p, 0.7f, { 199, 100, 66 }, to); }
                else { StrokeOpt so; so.closed = true; so.dash = { 3, 4 }; G.stroke (p, 1, K (acc, 0.42f * mix), so); }
            }
        for (auto& q : vs) { const int si = juce::jlimit (0, 3, juce::roundToInt ((37 * q.sc - 25) / 4)); G.blit (voice (G, si, T), q.x, q.y, clampf (q.a, 0, 1)); }
        const auto top = circ (140, 247, 41, 9, 22); const auto side = concat (arcp (140, 247, 41, 9, 0, PI, 12), arcp (140, 254, 41, 9, PI, 0, 12));
        if (look == Look::neon) { TubeOpt to; to.closed = true; to.halo = 0; to.a = 0.8f; tubeLine (G, circ (140, 254, 41, 9, 22), 1, { 199, 100, 62 }, to); G.fill (top, K ({ 199, 100, 60 }, 0.14f)); TubeOpt t2; t2.closed = true; tubeLine (G, top, 1.3f, { 199, 100, 66 }, t2); }
        else
        {
            const Mat mt = look == Look::dawn ? Mat { { 200, 14, 62 }, { 232, 20, 36 }, {}, HSL { 50, 80, 86 } } : Mat { { 30, 44, 50 }, { 16, 42, 28 } };
            { SolidOpt so; so.iw = 1.4f; so.sd = 4; so.shw = 2; Mat m2 { mt.shd, { mt.shd.h, mt.shd.s, mt.shd.l - 9 } }; m2.rim = mt.rim; solid (G, side, m2, so); }
            { SolidOpt so; so.iw = 1.3f; so.sd = 5; so.shw = 2.6f; solid (G, top, mt, so); }
        }
        RigP P = m.P; P.s = 47; P.x = 140; P.y = 246 - lvl * 2; P.tail = 0.25f; const Rig R = ghostRig (P);
        glow (G, R.mid.x, R.mid.y, 1.5f, { 199, 90, 82 }, (0.3f + 0.3f * lvl) * (on ? 1.0f : 0.4f));
        GL L; L.slime = HSL { 188, 78, 62 }; L.eyes = ! on ? "closed" : m.hp.on && ! m.hp.eyes.empty() ? m.hp.eyes : lvl > 0.4f ? "happy" : "open"; L.mouth = ! on ? "flat" : lvl > 0.1f ? "o" : "smile"; L.mouthK = 1 + 0.5f * lvl; L.blush = 0.6f; L.glow = 0;
        if (look == Look::neon) { L.tube = HSL { 199, 100, 66 }; L.slime = HSL { 176, 100, 58 }; L.k = 0.9f + 0.1f * hsh (T.tick, 5); }
        ghost (G, R, L, T);
        if (ens && on && mix > 0.05f) for (int i = 0; i < 3; ++i) { const auto& q = vs[(size_t) std::floor (hsh (i, std::floor ((float) T.tick / 2)) * (float) vs.size())]; if (! q.j) glint (G, q.x + (hsh (i, T.tick) - 0.5f) * 16, q.y - 34 * q.sc * (1 + hsh (i, 7)), 0.8f, { 190, 60, 96 }, 0.9f * mix); }
        if (on && lvl > 0.06f)
            for (int i = 0; i < 3; ++i)
            {
                const float c = std::fmod (((float) T.f / 24) * 0.7f + (float) i / 3, 1.0f), x = 140 + (float) (i - 1) * 24 + std::sin (c * 5 + (float) i) * 5, y = R.top.y - 8 - c * 86, a = (1 - c) * juce::jmin (1.0f, lvl * 2.5f);
                G.ell (x, y, 4.2f, 3, K (acc, a), -0.4f); LineOpt lo; lo.a = a; lo.ta = 0.02f; lo.tb = 0.3f; lo.halo = 0; LN (G, { { x + 3.7f, y - 0.5f }, { x + 3.7f, y - 13.5f }, { x + 8.2f, y - 10 } }, 1.5f, acc, lo);
            }
        if (! on) offVeil (G);
        stageOver (G);
    }
};

// FORMANT: a close portrait. The mouth forms the vowel, rings of sound leave it, a rail of A E I O U shows where it is
class FormantScene final : public Scene
{
public:
    std::vector<std::string> controls() const override { return { "vowel", "mouth", "reso", "shift", "drive", "move", "mdiv", "mdepth", "mlen", "body", "mshape", "offset", "mix" }; }
    std::vector<float> defaults() const override { return { 0, 0.5f, 0.5f, 0.5f, 0.2f, 0, 9, 0.5f, 1, 0.3f, 0, 0, 1 }; }
    float scaleOf (const std::string& n) const override { return n == "vowel" ? 4.0f : 1.0f; }
    static float cur (const Feed& F, const Clock& T, bool on)
    {
        const int mv = juce::roundToInt (F.v ("move")); const float vowel = F.v ("vowel"), md = F.v ("mdepth"); if (! mv || ! on) return vowel;
        const float beats = (float) T.tick / 12 * 2;
        if (mv == 1) return clampf (vowel + md * 2 * lfoShape (juce::roundToInt (F.v ("mshape")), beats / DIVB[juce::jlimit (0, 15, juce::roundToInt (F.v ("mdiv")))], 3), 0, 4);
        static const float lens[4] { 0.5f, 1, 2, 4 }; const float p = beats / (4 * lens[juce::jlimit (0, 3, juce::roundToInt (F.v ("mlen")))]);
        return clampf (vowel + md * 2 * (0.7f * std::sin (p * TAU) + 0.3f * std::sin (p * 3 * TAU + 1)), 0, 4);
    }
    void scene (G& G, const Feed& F, State& S, const Clock& T) override
    {
        const Look look = G.look; const auto m = gmo (F, S, T); const bool on = m.on; const float lvl = m.lvl; const int k = m.k; const float mix = mixA (F.v ("mix")); const HSL acc { 320, 100, 68 };
        const float cu = cur (F, T, on), s = lerp (86, 56, F.v ("shift")), reso = F.v ("reso"), drive = F.v ("drive"); const int mv = juce::roundToInt (F.v ("move"));
        const float mk = (1.25f + 1.15f * F.v ("mouth")) * (on ? (0.86f + 0.2f * juce::jmin (1.0f, lvl * 1.4f)) : 0.8f) * (k >= 0 && k < 2 ? 1.12f : 1.0f);
        RigP P = m.P; P.s = s; P.x = 112; P.y = 252 - lvl * 2; P.tail = 0.25f; const Rig R = ghostRig (P);
        const auto ms = mouthShape (R, "", mk, cu); const auto bb = bbox (ms.fill); const float mx = (bb[0] + bb[2]) / 2, my = (bb[1] + bb[3]) / 2, mhw = (bb[2] - bb[0]) / 2, mhh = (bb[3] - bb[1]) / 2;
        const HSL ink = look == Look::neon ? HSL { 320, 100, 70 } : look == Look::dawn ? HSL { 268, 35, 20 } : INKC;
        stageUnder (G);
        glow (G, 140, 256, 2.1f, { 320, 70, 74 }, 0.3f + 0.2f * lvl, true);
        glow (G, R.mid.x, R.mid.y - s * 0.3f, clampf (s / 24, 1.5f, 3.4f), { 324, 80, 84 }, (0.3f + 0.25f * lvl) * (on ? 1.0f : 0.4f));
        auto rx = [] (float i) { return 46 + i * 47; }; const int near = juce::roundToInt (cu); const char* AEIOU = "AEIOU";
        if (look != Look::neon) G.fill (rbox (24, 9, 232, 32, 9), look == Look::dawn ? K ({ 200, 50, 10 }, 0.42f) : K ({ 268, 40, 10 }, 0.45f));
        for (int i = 0; i < 5; ++i) { const float d = std::abs ((float) i - cu), hot = juce::jmax (0.0f, 1 - d); glyph (G, AEIOU[i], rx ((float) i), 23, 11 + 4 * hot, look == Look::neon ? (hot > 0.5f ? HSL { 320, 100, 70 } : HSL { 262, 50, 60 }) : mixrgb ({ 50, 30, 88 }, { 322, 100, 76 }, hot), 1.5f + 0.9f * hot, 0.7f + 0.3f * hot, hot > 0.5f ? 1.0f : 0.0f); }
        G.fill ({ { rx (cu), 33.5f }, { rx (cu) - 4.4f, 40 }, { rx (cu) + 4.4f, 40 } }, K (acc, 1));
        if (mv) { const float lo = clampf (F.v ("vowel") - F.v ("mdepth") * 2, 0, 4), hi = clampf (F.v ("vowel") + F.v ("mdepth") * 2, 0, 4); LineOpt l; l.a = 0.9f; l.ta = 0; l.tb = 0; l.halo = 0; LN (G, { { rx (lo), 13.5f }, { rx (lo), 10.5f }, { rx (hi), 10.5f }, { rx (hi), 13.5f } }, 1.2f, acc, l); G.ell (rx (F.v ("vowel")), 10.5f, 2, 2, K (acc, 1)); }
        const int nr = 2 + juce::roundToInt (5 * reso); const float gap = lerp (22, 12, reso), span = lerp (1.0f, 0.55f, reso), ph = on && lvl > 0.03f ? std::fmod ((float) T.f / 24 * 1.8f, 1.0f) : 0.0f, rw = lerp (2.8f, 1.25f, reso), ra = (on ? (0.3f + 0.7f * juce::jmin (1.0f, lvl * 1.6f)) : 0.3f) * mix, off = F.v ("offset");
        const HSL ringCol = mixrgb ({ 320, 100, 68 }, { 330, 100, 90 }, reso * 0.7f);
        for (int j = 0; j < nr; ++j) { const float r = mhw + 14 + ((float) j + ph) * gap * (1 + off * 0.4f), a = (1 - ((float) j + ph) / (float) nr) * ra; if (mx + r > 292 || a < 0.03f) continue; LineOpt l; l.a = a; l.ta = 0.22f; l.tb = 0.22f; l.sd = (float) j; l.halo = reso > 0.6f && j < 2 ? 1.0f : 0.0f; LN (G, arcp (mx, my, r, r * 1.06f, -span, span, 10), rw, ringCol, l); }
        if (off > 0.03f) for (int j = 0; j < nr; ++j) { const float r = s * 1.2f + ((float) j + std::fmod (ph + 0.5f, 1.0f)) * gap * (1 - off * 0.5f), a = (1 - ((float) j + ph) / (float) nr) * ra * juce::jmin (1.0f, off * 3); if (mx - r < -10 || a < 0.03f) continue; LineOpt l; l.a = a; l.ta = 0.22f; l.tb = 0.22f; l.sd = (float) j + 9; l.halo = 0; LN (G, arcp (mx, my, r, r * 1.06f, PI - span * 0.8f, PI + span * 0.8f, 10), rw, { 190, 90, 72 }, l); }
        if (on && k >= 0 && k < 5) { const float r = mhw + 10 + (float) k * 13; LineOpt l; l.a = (1 - (float) k / 5) * mix; l.ta = 0.2f; l.tb = 0.2f; l.sd = 3; l.halo = 1; LN (G, arcp (mx, my, r, r * 1.06f, -span * 1.15f, span * 1.15f, 12), rw * 1.5f, { 330, 100, 92 }, l); }
        GL L; L.slime = HSL { 42, 92, 62 }; L.eyes = ! on ? "closed" : m.hp.on && ! m.hp.eyes.empty() ? m.hp.eyes : lvl > 0.55f ? "happy" : "open";
        if (! on) L.mouth = "flat"; else L.vowel = cu;
        L.mouthK = mk; L.blush = 0.55f + 0.35f * drive; L.glow = 0; L.gloss = 0;
        if (look == Look::neon) { L.tube = HSL { 320, 100, 64 }; L.slime = HSL { 42, 100, 58 }; L.heat = drive * 0.6f; L.k = 0.9f + 0.1f * hsh (T.tick, 5); }
        ghost (G, R, L, T); wear (G, R, "halo", T);
        if (on && drive > 0.03f && mhh > 3) { if (look == Look::neon) glow (G, mx, my, 0.5f, { 14, 100, 60 }, drive * 0.9f); else G.ell (mx, my + mhh * 0.12f, mhw * 0.62f, mhh * 0.6f, K ({ 6, 92, 50 }, drive * 0.9f)); }
        const int nh = juce::roundToInt (drive * 4);
        for (int i = 0; i < nh; ++i) { const float x0 = R.top.x + ((float) i - (float) (nh - 1) / 2) * 15, y0 = R.top.y - s * 0.34f; Poly p; for (int j = 0; j <= 6; ++j) p.push_back ({ x0 + std::sin ((float) j * 1.15f + (float) T.f * 0.3f + (float) i * 1.7f) * 2.8f * ((float) j / 6), y0 - (float) j * 4.4f }); LineOpt l; l.a = 0.8f; l.sd = (float) i; l.ta = 0.15f; l.tb = 0.5f; LN (G, p, 1.5f, { 22, 95, look == Look::dawn ? 72.0f : 64.0f }, l); }
        if (F.v ("body") > 0.02f) { const auto q = R.X (R.mouth.x - 0.12f, -0.72f); glow (G, q.x, q.y, clampf (s / 38, 1, 2), { 18, 95, 66 }, F.v ("body") * (0.45f + 0.3f * lvl) * (on ? 1.0f : 0.4f)); }
        const float bx = 228, by = 78; const auto bub = circ (bx, by, 25, 22, 18); const Poly tail { { bx - 21, by + 9 }, { bx - 36, by + 27 }, { bx - 9, by + 19 } };
        if (look == Look::neon) { G.fill (bub, K (pal::neonGlass, 0.6f)); G.fill (bub, K ({ 320, 100, 60 }, 0.12f)); TubeOpt to; to.closed = true; tubeLine (G, bub, 1.2f, { 320, 100, 64 }, to); TubeOpt t2; t2.halo = 0; tubeLine (G, tail, 1, { 320, 100, 64 }, t2); }
        else
        {
            const Mat pm { look == Look::dawn ? HSL { 48, 50, 95 } : HSL { 45, 40, 96 }, look == Look::dawn ? HSL { 300, 26, 80 } : HSL { 290, 30, 84 }, {}, HSL { 50, 100, 97 } };
            { SolidOpt so; so.iw = 1.3f; so.sd = 4; so.shw = 1.5f; solid (G, tail, pm, so); }
            { SolidOpt so; so.iw = 1.5f; so.sd = 5; so.shw = 3; solid (G, wob (bub, 0.5f, 0.05f, 3 + (float) boilOf (T, look == Look::dawn ? 4 : 3)), pm, so); }
            G.fill ({ { bx - 19, by + 8 }, { bx - 30, by + 22 }, { bx - 10, by + 17 } }, K (pm.lit));
        }
        glyph (G, AEIOU[juce::jlimit (0, 4, near)], bx, by, 24, ink, look == Look::neon ? 2.2f : 3.2f, 1, 1);
        if (! on) offVeil (G);
        stageOver (G);
    }
};
} // namespace phantom::pv
