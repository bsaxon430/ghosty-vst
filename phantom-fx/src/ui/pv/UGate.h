// SPDX-License-Identifier: AGPL-3.0-or-later
// GATE (GHOST GATE; port of phantom-anime/visuals/src/u_gate.js): a shuttered window with the morning behind it. One slat per 1/32
// of the pattern, as open as the curve is there; a light runs with the playhead; the PUMP is a blind that drops from the top.
#pragma once
#include "Hands.h"

namespace phantom::pv
{
namespace gatepv
{
    inline float gPul (int n, float duty, const char* mask, float x)
    {
        const float p = x * (float) n;
        for (int k = (int) std::floor (p); k >= juce::jmax (0, (int) std::floor (p) - 2); --k) { if (mask && mask[k] == '0') continue; if (p - (float) k < duty) return mask && mask[k] == '2' ? 0.6f : 1.0f; }
        return 0;
    }
    // the 16 factory patterns (the painter's stand-in when the plugin sends no curve)
    inline float pat (int i, float x)
    {
        switch (i)
        {
            case 1: return gPul (8, 0.5f, nullptr, x); case 2: return gPul (16, 0.5f, nullptr, x); case 3: return gPul (16, 0.62f, "1011011010110111", x);
            case 4: { const float f = std::fmod (x * 4, 1.0f); return f < 0.68f ? 0.05f + 0.95f * std::pow (f / 0.68f, 0.8f) : 1.0f; }
            case 8: return x < 0.75f ? 1.0f : 1 - 0.8f * std::pow ((x - 0.75f) / 0.25f, 1.3f);
            case 10: return gPul (16, 1.6f, "1001001001001000", x);
            case 11: { const int k = (int) std::floor (x * 8); const float f = x * 8 - (float) k; const int o = k % 2; const float a = o ? 0.36f : 0.004f, l = o ? 0.36f : 0.56f; return f >= a && f < a + l ? (o ? 0.75f : 1.0f) : 0.0f; }
            case 12: return std::pow (std::fmod (x * 2, 1.0f), 1.25f);
            case 14: return gPul (16, 0.7f, "1101101111011010", x);
            case 15: return 0.5f + 0.5f * std::cos (x * TAU * 4);
            default: return 1;
        }
    }
    inline const char* steps (int i)
    {
        switch (i) { case 5: return "3333333333333333"; case 6: return "0000000000004444"; case 7: return "0000000000001122"; case 8: return "0000000000006666"; case 9: return "0005000500050055"; case 13: return "3333333300004444"; case 14: return "0020401103305246"; default: return "0000000000000000"; }
    }
    inline float swing (float x, float sw) { const float a = 0.5f * sw, p = x * 16, pair = std::floor (p / 2), f = p - 2 * pair, fs = f < 1 + a ? f / (1 + a) : 1 + (f - (1 + a)) / (1 - a); return (2 * pair + fs) / 16; }
    inline float swingInv (float u, float sw) { const float a = 0.5f * sw, p = u * 16, pair = std::floor (p / 2), fs = p - 2 * pair, f = fs < 1 ? fs * (1 + a) : (1 + a) + (fs - 1) * (1 - a); return (2 * pair + f) / 16; }
}

class GateScene final : public Scene
{
public:
    std::vector<std::string> controls() const override { return { "pattern", "length", "smooth", "time", "pmode", "pdepth", "pshape", "plen", "prel", "patk", "phold", "pthr", "ponly", "swing", "trig", "tthr", "tsens", "mix" }; }
    std::vector<float> defaults() const override { return { 1, 1, 0.2f, 1, 0, 0.7f, 0.5f, 1, 0.3f, 0.2f, 0, 0.6f, 1, 0, 0, 0.5f, 0.5f, 1 }; }
    static constexpr float BPM = 120;
    static float level (const Feed& F, int pat, float u) { const auto& c = F.curve; u = clampf (u, 0, 0.99999f); return c.empty() ? gatepv::pat (pat, u) : c[(size_t) juce::jmin ((int) c.size() - 1, (int) std::floor (u * (float) c.size()))]; }
    static float pumpAt (const Feed& F, float ph, int pm)
    {
        const float depth = F.v ("pdepth"), ex = std::pow (2.0f, (F.v ("pshape") - 0.5f) * 3);
        if (pm == 4) return 1 - depth * clampf (1 - std::pow (clampf (ph / 0.6f, 0, 1), 2.83f), 0, 1);
        const float period = pm == 1 ? 1.0f : pm == 2 ? 2.0f : pm == 3 ? 0.5f : 1.0f, hold = clampf (0.2f * F.v ("phold") / (period * 60 / BPM), 0, 0.6f), rec = clampf ((ph - hold) / juce::jmax (0.05f, 0.75f - hold), 0, 1);
        return 1 - depth * (1 - std::pow (rec, ex));
    }
    static float pumpNow (const Feed& F, float bt, float lvl, State& S, bool on, int pm)
    {
        if (pm == 0 || ! on) return 1;
        if (pm == 5)
        {
            const float db = 20 * std::log10 (juce::jmax (1e-4f, lvl)), over = clampf ((db - (-60 + 60 * F.v ("pthr"))) / 18, 0, 1), tg = 1 - F.v ("pdepth") * std::pow (over, 1 / std::pow (2.0f, (F.v ("pshape") - 0.5f) * 3));
            return S.follow ("gp", tg, lerp (0.9f, 0.25f, F.v ("patk")), 1 - std::exp (-1 / (24 * (0.01f + 0.99f * F.v ("prel")))));
        }
        static const float lens[4] { 0.5f, 1, 2, 4 }; const float period = pm == 1 ? 1.0f : pm == 2 ? 2.0f : pm == 3 ? 0.5f : lens[juce::jlimit (0, 3, juce::roundToInt (F.v ("plen")))];
        return pumpAt (F, std::fmod (std::fmod (bt, period) + period, period) / period, pm);
    }
    struct Cell { float xa, xb, a; };
    void scene (G& G, const Feed& F, State& S, const Clock& T) override
    {
        const Look look = G.look; const bool on = F.on; const float mixk = mixA (F.v ("mix")); const int pat = juce::jlimit (0, 15, juce::roundToInt (F.v ("pattern")) - 1);
        static const int lens[3] { 2, 4, 8 }; const int len = lens[juce::jlimit (0, 2, juce::roundToInt (F.v ("length")))], pm = juce::jlimit (0, 5, juce::roundToInt (F.v ("pmode")));
        const bool patOn = pm == 0 || F.v ("ponly") < 0.5f, audio = F.v ("trig") > 0.5f; const float sw = F.v ("swing");
        const float bt = (float) T.t * BPM / 60, lastHit = S.hits.empty() ? -9.0f : (float) S.hits.back(), spb = 60 / BPM, lvl = on ? F.level : 0;
        auto posAt = [&] (float t) { return ! patOn ? -1.0f : audio ? clampf ((t - lastHit) / ((float) len * spb), 0, 1) : std::fmod ((t / spb) / (float) len, 1.0f); };
        const float pos = F.hasPos() ? F.pos : posAt ((float) T.t); auto lv = [&] (float u) { return level (F, pat, u); };
        const float aNow = pos < 0 ? 1.0f : lv (gatepv::swing (juce::jmin (pos, 0.99999f), sw)), gp = F.hasPump() ? F.pump : pumpNow (F, bt, F.level, S, on, pm), lit = on ? aNow * gp : 0.0f, face = S.follow ("gf", lit, 0.7f, 0.3f);
        const float X0 = 24, X1 = 256, Y0 = 46, Y1 = 166, W = X1 - X0, Hw = Y1 - Y0; const int N = 32; std::vector<Cell> cells;
        for (int k = 0; k < N; ++k) cells.push_back ({ X0 + W * gatepv::swingInv ((float) k / N, sw), X0 + W * gatepv::swingInv ((float) (k + 1) / N, sw), patOn ? lv (((float) k + 0.5f) / N) : 1.0f });
        const HSL LC = L3<HSL> (look, { 48, 100, 80 }, { 46, 94, 78 }, { 146, 100, 62 }), veilC = L3<HSL> (look, { 200, 60, 7 }, { 262, 50, 8 }, { 250, 60, 4 }), dk = L3<HSL> (look, { 262, 30, 14 }, INKC, { 262, 60, 30 });
        const float bs = 3 + (float) boilOf (T, look == Look::dawn ? 4 : 3) * 1.7f, sndK = 0.62f + 0.38f * lvl;
        stageUnder (G);
        G.rect (0, 0, 280, 300, K (veilC, L3 (look, 0.52f, 0.48f, 0.24f) * mixk));
        glow (G, 140, 106, 4.4f, LC, (0.14f + 0.22f * lit) * mixk);
        if (look == Look::neon)
        {
            G.rect (X0, Y0, W, Hw, K (pal::neonGlass, 0.6f)); G.rect (X0, Y0, W, Hw, K (LC, 0.34f)); G.rect (X0, Y0 + Hw * 0.56f, W, Hw * 0.44f, K ({ 160, 100, 70 }, 0.2f));
            std::vector<Poly> ls; for (int i = 0; i < 7; ++i) ls.push_back ({ { X0, Y0 + 8 + (float) i * 17 }, { X1, Y0 + 8 + (float) i * 17 } }); G.strokeMany (ls, 0.7f, K ({ 146, 80, 84 }, 0.3f));
            G.ell (182, 98, 22, 22, K ({ 120, 90, 88 }, 0.5f)); G.ring (182, 98, 22, 22, 1, K ({ 110, 100, 88 }, 0.85f));
        }
        else
        {
            G.rect (X0, Y0, W, Hw, K (L3<HSL> (look, { 46, 96, 80 }, { 44, 92, 80 }, {}))); G.rect (X0, Y0, W, Hw * 0.46f, K ({ 52, 100, 91 }, 0.8f)); G.ell (182, 98, 24, 24, K ({ 54, 100, 97 }, 0.95f));
            G.fill (smooth ({ { X0, Y1 }, { X0, 132 }, { 52, 120 }, { 84, 134 }, { 118, 122 }, { 150, 138 }, { 196, 126 }, { 230, 136 }, { X1, 124 }, { X1, Y1 } }, true, 3), K (L3<HSL> (look, { 70, 56, 62 }, { 84, 50, 60 }, {}), 0.9f));
            G.fill (smooth ({ { X0, Y1 }, { X0, 150 }, { 60, 142 }, { 104, 152 }, { 160, 146 }, { 214, 154 }, { X1, 146 }, { X1, Y1 } }, true, 3), K (L3<HSL> (look, { 110, 44, 46 }, { 120, 44, 44 }, {}), 0.9f));
        }
        std::vector<Poly> faces, thin, shade, edge;
        for (auto& c : cells)
        {
            const float w = c.xb - c.xa, fw = juce::jmax (1.3f, w * (1 - c.a)), xc = (c.xa + c.xb) / 2; Poly f { { xc - fw / 2, Y0 }, { xc + fw / 2, Y0 }, { xc + fw / 2, Y1 }, { xc - fw / 2, Y1 } };
            (fw < 2.2f ? thin : faces).push_back (f);
            if (fw > 3.4f) shade.push_back ({ { xc + fw * 0.12f, Y0 }, { xc + fw / 2, Y0 }, { xc + fw / 2, Y1 }, { xc + fw * 0.12f, Y1 } });
            if (fw >= 2.2f) { edge.push_back ({ { xc - fw / 2, Y0 }, { xc - fw / 2, Y1 } }); if (c.a > 0.06f) edge.push_back ({ { xc + fw / 2, Y0 }, { xc + fw / 2, Y1 } }); }
        }
        const float sA = patOn ? 1.0f : 0.5f;
        if (look == Look::neon) { G.fillMany (faces, K (pal::neonGlass, 0.94f)); G.fillMany (faces, K ({ 262, 80, 40 }, 0.16f)); G.fillMany (thin, K (LC, 0.5f * sA)); G.strokeMany (edge, 1.3f, K (LC, 0.85f)); G.strokeMany (edge, 0.5f, K ({ 146, 60, 92 }, 0.9f)); }
        else
        {
            G.fillMany (thin, K (L3<HSL> (look, { 40, 56, 56 }, { 42, 50, 60 }, {}), 0.85f * sA)); G.fillMany (faces, K (L3<HSL> (look, { 28, 26, 24 }, { 252, 18, 32 }, {}))); G.fillMany (shade, K (L3<HSL> (look, { 262, 26, 13 }, { 258, 26, 19 }, {})));
            if (look == Look::cel) G.strokeMany (edge, 1, K (INKC)); else G.strokeMany (edge, 1, K (LC, 0.5f));
        }
        if (pm > 0)
        {
            const float yb = Y0 + (1 - gp) * (Hw - 4) + 2; const Poly cloth { { X0, Y0 }, { X1, Y0 }, { X1, yb }, { X0, yb } }; const auto bar = rbox (X0 - 7, yb - 3, W + 14, 8, 2.4f, 2); const HSL cc = L3<HSL> (look, { 150, 26, 20 }, { 262, 22, 28 }, { 322, 100, 58 });
            std::vector<Poly> lines; for (int i = 0; i < (int) std::floor ((yb - Y0) / 8); ++i) lines.push_back ({ { X0 + (look == Look::neon ? 2.0f : 1.0f), Y0 + (look == Look::neon ? 4.0f : 5.0f) + (float) i * 8 }, { X1 - (look == Look::neon ? 2.0f : 1.0f), Y0 + (look == Look::neon ? 4.0f : 5.0f) + (float) i * 8 } });
            if (look == Look::neon) { G.fill (cloth, K (pal::neonGlass, 0.82f)); G.fill (cloth, K (cc, 0.2f)); if (yb - Y0 > 8) G.strokeMany (lines, 0.7f, K (cc, 0.45f)); tubeLine (G, { { X0 - 7, yb }, { X1 + 7, yb } }, 2, cc); }
            else
            {
                G.fill (cloth, K (cc)); if (yb - Y0 > 8) G.strokeMany (lines, 1, K (L3<HSL> (look, { 150, 30, 13 }, { 264, 28, 18 }, {}), 0.85f));
                if (yb - Y0 > 6) G.fill ({ { X0, Y0 }, { X0 + W * 0.42f, Y0 }, { X0 + W * 0.3f, yb }, { X0, yb } }, K (L3<HSL> (look, { 140, 30, 30 }, { 262, 20, 38 }, {}), 0.55f));
                SolidOpt so; so.iw = 1.5f; so.sd = bs; so.shw = 2; solid (G, wob (bar, 0.4f, 0.05f, bs), MAT (look == Look::dawn ? "wood" : "brass"), so);
            }
            {   // PUMP ATTACK: a fringe under the bar, longer the slower the blind comes down
                const float fl = 2 + 11 * F.v ("patk"); std::vector<Poly> fr; for (int i = 0; i < 15; ++i) { const float x = X0 + 9 + (float) i * (W - 18) / 14; fr.push_back ({ { x - 5.4f, yb + 4.4f }, { x + 5.4f, yb + 4.4f }, { x, yb + 4.4f + fl } }); }
                if (look == Look::neon) { std::vector<Poly> o; for (auto& t : fr) o.push_back ({ t[0], t[2], t[1] }); G.strokeMany (o, 1, K (cc, 0.85f)); } else G.fillMany (fr, K (L3<HSL> (look, { 140, 30, 30 }, { 262, 22, 40 }, {}), 0.95f));
            }
            const auto mc = look == Look::neon ? K ({ 50, 100, 80 }) : K (L3<HSL> (look, { 50, 90, 86 }, { 262, 40, 16 }, {})); static const int nds[6] { 0, 4, 2, 8, 0, 0 }; const int nd = nds[pm];
            if (nd) { std::vector<Poly> ds; for (int i = 0; i < nd; ++i) ds.push_back (circ (140 + ((float) i - (float) (nd - 1) / 2) * (nd > 4 ? 7.0f : 11.0f), yb + 1, nd > 4 ? 1.3f : 1.9f, nd > 4 ? 1.3f : 1.9f, 6)); G.fillMany (ds, mc); }
            if (pm == 4) { Poly w; for (auto q : Poly { { -16, 3 }, { -9, -2 }, { -2, 3 }, { 5, -1 }, { 12, 2 }, { 17, 0 } }) w.push_back ({ 140 + q.x, yb + 1 + q.y * 0.9f }); G.stroke (smooth (w, false, 3), 1.3f, mc); }
            if (pm == 5) { G.stroke (arcp (140, yb + 1, 5, 3.4f, -2.3f, 2.3f, 8), 1.3f, mc); G.stroke ({ { 150, yb - 1.5f }, { 154, yb - 3 } }, 1.1f, mc); G.stroke ({ { 150, yb + 1 }, { 155, yb + 1 } }, 1.1f, mc); G.stroke ({ { 150, yb + 3.5f }, { 154, yb + 5 } }, 1.1f, mc); }
            std::vector<float> mk; if (pm == 5) mk = { 1 - F.v ("pdepth"), 1 }; else for (float q : { 0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f }) mk.push_back (pumpAt (F, q * 0.75f, pm));
            std::vector<float> my; for (float gg : mk) my.push_back (Y0 + (1 - gg) * (Hw - 4) + 2);
            const auto pc = K (look == Look::neon ? HSL { 50, 100, 74 } : L3<HSL> (look, { 50, 90, 84 }, { 46, 80, 76 }, {}), 0.9f);
            std::vector<Poly> ticks; for (size_t i = 0; i < my.size(); ++i) ticks.push_back ({ { X1 + 11, my[i] }, { X1 + (i == 0 ? 19.0f : 16.0f), my[i] } }); G.strokeMany (ticks, 1.2f, pc);
            if (F.v ("phold") > 0.03f && pm < 4) G.stroke ({ { X1 + 19, my[0] }, { X1 + 19, my[0] - 3 - 9 * F.v ("phold") }, { X1 + 15, my[0] - 3 - 9 * F.v ("phold") } }, 1.3f, pc);
        }
        if (look == Look::dawn)
        {
            const Mat stn { { 44, 12, 43 }, { 252, 18, 22 }, {}, HSL { 46, 60, 72 } };
            const float blocks[11][4] { { X0 - 27, Y0 - 16, 17, 42 }, { X0 - 25, Y0 + 27, 15, 38 }, { X0 - 28, Y0 + 66, 18, 40 }, { X0 - 26, Y0 + 107, 16, 29 }, { X1 + 10, Y0 - 14, 16, 38 }, { X1 + 9, Y0 + 25, 19, 44 }, { X1 + 11, Y0 + 70, 15, 36 }, { X1 + 9, Y0 + 107, 18, 29 }, { X0 - 31, Y0 - 33, 84, 18 }, { X0 + 55, Y0 - 31, 92, 16 }, { X0 + 149, Y0 - 34, W - 149 + 32, 19 } };
            for (int i = 0; i < 11; ++i) { SolidOpt so; so.iw = 1.5f; so.sd = bs + (float) i; so.shw = 3.4f; so.rimw = 1.5f; solid (G, wob (rbox (blocks[i][0], blocks[i][1], blocks[i][2], blocks[i][3], 3.4f, 2), 1, 0.06f, bs + (float) i * 3), stn, so); }
            G.fill (blobp (58, Y0 - 34, 30, 5, 0.3f, 11, 5), K ({ 112, 44, 30 })); G.fill (blobp (150, Y0 - 33, 24, 4, 0.3f, 10, 6), K ({ 98, 48, 34 })); G.fill (blobp (232, Y0 - 35, 26, 5, 0.3f, 10, 8), K ({ 118, 42, 28 })); G.fill (blobp (X0 - 20, Y0 + 64, 8, 4, 0.3f, 8, 3), K ({ 104, 46, 32 }));
        }
        if (look == Look::cel)
        {
            const float a = X0 - 22, b = X1 + 22, t = Y0 - 26, u = Y1 + 16;
            G.fillMany ({ { { a, t }, { b, t }, { b, Y0 }, { a, Y0 } }, { { a, Y1 }, { b, Y1 }, { b, u }, { a, u } }, { { a, Y0 }, { X0, Y0 }, { X0, Y1 }, { a, Y1 } }, { { X1, Y0 }, { b, Y0 }, { b, Y1 }, { X1, Y1 } } }, K ({ 252, 18, 24 }));
            G.fillMany ({ { { a, t }, { b, t }, { b, t + 4 }, { a, t + 4 } }, { { a, t }, { a + 4, t }, { a + 4, u }, { a, u } } }, K ({ 250, 16, 37 }));
            { OutlineOpt oo; oo.gap = 0; G.fillMany (outlineRibbons ({ { a, t }, { b, t }, { b, u }, { a, u } }, 1.6f, bs + 7, oo), K (INKC)); }
            std::vector<Poly> rv; for (int i = 0; i < 8; ++i) { rv.push_back (circ (a + 5.5f, t + 8 + (float) i * 21, 1.6f, 1.6f, 6)); rv.push_back (circ (b - 5.5f, t + 8 + (float) i * 21, 1.6f, 1.6f, 6)); } G.fillMany (rv, K (INKC));
        }
        const std::vector<Poly> posts { rbox (X0 - 11, Y0 - 12, 11, Hw + 24, 2, 2), rbox (X1, Y0 - 12, 11, Hw + 24, 2, 2) }; const auto roll = rbox (X0 - 14, Y0 - 13, W + 28, 13, 5, 3), sill = rbox (X0 - 17, Y1, W + 34, 12, 2.5f, 2);
        if (look == Look::neon)
        {
            for (auto& p : posts) G.fill (p, K (pal::neonGlass, 0.85f)); G.fill (sill, K (pal::neonGlass, 0.85f));
            { TubeOpt to; to.k = 0.8f; tubeLine (G, { { X0 - 11, Y1 }, { X0 - 11, Y0 - 12 }, { X1 + 11, Y0 - 12 }, { X1 + 11, Y1 } }, 1.4f, { 262, 90, 66 }, to); }
            { TubeOpt to; to.halo = 0; to.k = 0.7f; tubeLine (G, { { X0, Y1 }, { X0, Y0 }, { X1, Y0 }, { X1, Y1 } }, 1, LC, to); }
            { TubeOpt to; to.k = 0.5f + 0.5f * lit; tubeLine (G, { { X0 - 17, Y1 + 1 }, { X1 + 17, Y1 + 1 } }, 1.6f, LC, to); }
            { TubeOpt to; to.halo = 0; to.k = 0.7f; tubeLine (G, { { X0 - 17, Y1 + 11 }, { X1 + 17, Y1 + 11 } }, 1, { 262, 90, 66 }, to); }
        }
        else
        {
            const Mat wd = look == Look::dawn ? Mat { { 30, 34, 40 }, { 18, 36, 20 }, {}, HSL { 46, 80, 78 } } : MAT ("steel"), tr = look == Look::dawn ? Mat { { 34, 38, 46 }, { 20, 38, 24 }, {}, HSL { 48, 90, 82 } } : MAT ("brass");
            for (int i = 0; i < 2; ++i) { SolidOpt so; so.iw = 1.7f; so.sd = bs + (float) i; so.shw = 2.6f; so.rimw = 1.6f; solid (G, wob (posts[(size_t) i], 0.5f, 0.05f, bs + (float) i), wd, so); }
            { SolidOpt so; so.iw = 1.7f; so.sd = bs + 2; so.shw = 3; so.rimw = 1.8f; solid (G, wob (roll, 0.5f, 0.05f, bs + 2), tr, so); } G.stroke ({ { X0 - 8, Y0 - 9.4f }, { X1 + 8, Y0 - 9.4f } }, 1.1f, K (*tr.rim, 0.7f));
            { SolidOpt so; so.iw = 1.8f; so.sd = bs + 3; so.shw = 3; so.rimw = 1.8f; solid (G, wob (sill, 0.5f, 0.05f, bs + 3), tr, so); }
            if (look == Look::dawn) { G.fill (blobp (X0 - 12, Y1 + 2, 9, 5, 0.25f, 8, 7), K ({ 104, 46, 34 })); G.strokeMany ({ smooth ({ { X1 + 6, Y0 - 8 }, { X1 + 9, Y0 + 16 }, { X1 + 4, Y0 + 38 }, { X1 + 8, Y0 + 58 } }, false, 4), smooth ({ { X0 - 4, Y0 - 16 }, { X0 - 7, Y0 + 4 }, { X0 - 3, Y0 + 22 } }, false, 4), smooth ({ { 96, Y0 - 30 }, { 99, Y0 - 20 }, { 95, Y0 - 13 } }, false, 3) }, 1.3f, K ({ 110, 44, 30 }, 0.9f)); }
            else { std::vector<Poly> rv; for (auto q : Poly { { X0 - 5.5f, Y0 - 4 }, { X0 - 5.5f, Y1 - 6 }, { X1 + 5.5f, Y0 - 4 }, { X1 + 5.5f, Y1 - 6 }, { X0 - 8, Y0 - 6.5f }, { X1 + 8, Y0 - 6.5f } }) rv.push_back (circ (q.x, q.y, 1.9f, 1.9f, 6)); G.fillMany (rv, K (dk)); }
        }
        // on the sill: the 16 time-lane steps and the beats of the LENGTH
        const char* st0 = gatepv::steps (pat); const float ta = F.v ("time"); std::vector<Poly> dots, mk2; const HSL mcol = look == Look::neon ? HSL { 50, 100, 74 } : L3<HSL> (look, { 262, 34, 16 }, { 262, 36, 14 }, {});
        for (int i = 0; i < 16; ++i)
        {
            const float x = X0 + W * gatepv::swingInv (((float) i + 0.5f) / 16, sw), y = Y1 + 5.4f; const int md = F.steps.size() == 16 ? F.steps[(size_t) i] : st0[i] - '0';
            if (! md) { dots.push_back (circ (x, y, 0.9f, 0.9f, 5)); continue; }
            if (md == 1) { mk2.push_back ({ { x - 3, y - 1.6f }, { x + 3, y - 1.6f } }); mk2.push_back ({ { x - 3, y + 1.6f }, { x + 3, y + 1.6f } }); }
            else if (md == 2) { mk2.push_back ({ { x - 3, y - 2 }, { x - 3, y + 2 } }); mk2.push_back ({ { x, y - 2 }, { x, y + 2 } }); mk2.push_back ({ { x + 3, y - 2 }, { x + 3, y + 2 } }); }
            else if (md == 3) mk2.push_back ({ { x - 4.4f, y }, { x + 4.4f, y } });
            else if (md == 4) mk2.push_back ({ { x + 2.4f, y - 2.6f }, { x - 2.4f, y }, { x + 2.4f, y + 2.6f } });
            else if (md == 5) mk2.push_back ({ { x - 4, y + 1.6f }, { x - 1.4f, y - 2 }, { x + 1.4f, y + 2 }, { x + 4, y - 1.6f } });
            else mk2.push_back ({ { x - 3, y - 2 }, { x + 3, y - 2 }, { x, y + 2.6f }, { x - 3, y - 2 } });
        }
        G.fillMany (dots, K (mcol, 0.45f)); if (! mk2.empty()) G.strokeMany (mk2, 1.25f, K (mcol, 0.3f + 0.7f * ta));
        { std::vector<Poly> bars; for (int i = 0; i <= len; ++i) { const float x = X0 + W * (float) i / (float) len; bars.push_back ({ { x, Y1 + 12.5f }, { x, Y1 + (i % 4 == 0 ? 19.0f : 16.0f) } }); } G.strokeMany (bars, 1.2f, K (look == Look::neon ? LC : L3<HSL> (look, { 50, 60, 84 }, { 46, 60, 80 }, {}), 0.75f)); }
        if (on && pos >= 0)
        {
            const float xp = X0 + W * pos; lightLine (G, { { xp, Y0 + 1 }, { xp, Y1 - 1 } }, 1.5f, { 52, 100, 92 }, 0.55f + 0.45f * aNow);
            const Poly car { { xp - 5, Y0 - 15 }, { xp + 5, Y0 - 15 }, { xp + 5, Y0 - 9 }, { xp, Y0 - 3 }, { xp - 5, Y0 - 9 } };
            if (look == Look::neon) { TubeOpt to; to.closed = true; tubeLine (G, car, 1.1f, { 50, 100, 70 }, to); }
            else { SolidOpt so; so.iw = 1.2f; so.sd = 4; so.shw = 1.4f; solid (G, car, { { 50, 96, 70 }, { 36, 84, 46 }, {}, HSL { 54, 100, 92 } }, so); glow (G, xp, Y0 - 9, 0.5f, { 50, 100, 80 }, 0.7f); }
            if (audio)
            {
                G.stroke ({ { xp, Y0 - 15 }, { xp, Y0 - 25 } }, 1.1f, K (look == Look::neon ? HSL { 50, 100, 74 } : dk)); G.fill ({ { xp, Y0 - 25 }, { xp + 8, Y0 - 22 }, { xp, Y0 - 19 } }, K ({ 353, 90, 60 }));
                const float ag = (float) T.t - lastHit; if (ag >= 0 && ag < 0.17f) { std::vector<Poly> sp; for (int i = 0; i < 3; ++i) sp.push_back ({ { X0 + 6 + (float) i * 9, Y0 - 20 - (float) i * 2.5f }, { X0 + 30 + (float) i * 14, Y0 - 20 - (float) i * 2.5f } }); G.strokeMany (sp, 1.2f, K ({ 52, 100, 90 }, 0.9f * (1 - ag / 0.17f))); }
            }
        }
        if (audio)
        {
            const float mx = 7, my0 = 64, my1 = 150, ty = lerp (my1, my0, F.v ("tthr")), ly = lerp (my1, my0, clampf (1 + 20 * std::log10 (juce::jmax (1e-3f, lvl)) / 60, 0, 1));
            G.stroke ({ { mx, my0 }, { mx, my1 } }, 3.6f, K (look == Look::neon ? HSL { 262, 50, 26 } : dk, 0.9f), { false, false }); if (lvl > 0.01f) G.stroke ({ { mx, my1 }, { mx, ly } }, 2, K (LC), { false, false });
            const float bh = 2 + 12 * F.v ("tsens"); G.rect (mx - 7, ty - bh, 14, 2 * bh, K ({ 353, 90, 60 }, 0.34f)); G.stroke ({ { mx - 8, ty }, { mx + 8, ty } }, 1.6f, K ({ 353, 90, 64 }));   // TRIGGER SENSE: the band in which a hit counts
        }
        std::vector<std::pair<float, float>> gaps;
        for (int j = 0; j <= N; ++j) { const Cell* a = j > 0 ? &cells[(size_t) j - 1] : nullptr; const Cell* b = j < N ? &cells[(size_t) j] : nullptr; const float g = ((a ? (a->xb - a->xa) * a->a : 0.0f) + (b ? (b->xb - b->xa) * b->a : 0.0f)) / 2, x = b ? b->xa : a->xb; if (g > 0.5f) gaps.push_back ({ x - g / 2, x + g / 2 }); }
        struct Run { float a, b, sum; }; std::vector<Run> runs; for (auto& gg : gaps) { if (! runs.empty() && gg.first - runs.back().b < 2.4f) { runs.back().sum += gg.second - gg.first; runs.back().b = gg.second; } else runs.push_back ({ gg.first, gg.second, gg.second - gg.first }); }
        const float YS = Y1 + 12, sm = F.v ("smooth"), bA = (on ? 1.0f : 0.55f) * mixk * gp * sndK; auto fx = [] (float x) { return 140 + (x - 140) * 1.72f; };
        std::vector<Poly> grp[3], soft;
        for (auto& r : runs)
        {
            const float s = r.sum / juce::jmax (1.0f, r.b - r.a), xa = juce::jmax (X0, r.a), xb = juce::jmin (X1, r.b); grp[s > 0.8f ? 0 : s > 0.45f ? 1 : 2].push_back ({ { xa, YS }, { xb, YS }, { fx (xb), 300 }, { fx (xa), 300 } });
            if (sm > 0.08f) { const float e = 2 + 9 * sm; soft.push_back ({ { xa - e * 0.4f, YS }, { xb + e * 0.4f, YS }, { fx (xb) + e, 300 }, { fx (xa) - e, 300 } }); }
        }
        if (! soft.empty()) G.fillMany (soft, K (LC, 0.1f * bA));
        { const float ga[3] { 0.2f, 0.13f, 0.07f }; for (int i = 0; i < 3; ++i) if (! grp[i].empty()) G.fillMany (grp[i], K (LC, ga[i] * bA * (1 - 0.3f * sm))); }
        { auto sorted = runs; std::stable_sort (sorted.begin(), sorted.end(), [] (const Run& p, const Run& q) { return (q.b - q.a) < (p.b - p.a); });
          for (size_t i = 0; i < sorted.size() && i < 2; ++i) { const auto& r = sorted[i]; const float w = r.b - r.a; if (w < 14) continue; const float xa = r.a + w * 0.2f, xb = r.b - w * 0.2f; G.fill ({ { xa, YS }, { xb, YS }, { fx (xb), 300 }, { fx (xa), 300 } }, K (mixrgb (LC, { 54, 100, 96 }, 0.4f), 0.13f * bA * r.sum / w)); glow (G, (r.a + r.b) / 2, (Y0 + Y1) / 2, clampf (w / 50, 0.75f, 2.6f), LC, 0.22f * bA * r.sum / w); } }
        if (on && pos >= 0 && aNow > 0.05f) { const float xp = X0 + W * pos, hw = 3 + 5 * aNow; G.fill ({ { xp - hw, YS }, { xp + hw, YS }, { fx (xp) + hw * 2.2f, 300 }, { fx (xp) - hw * 2.2f, 300 } }, K ({ 52, 100, 90 }, (0.14f + 0.36f * lvl) * aNow * gp * mixk)); }
        glow (G, 140, 254, 2.1f, LC, (0.1f + 0.42f * lit * sndK) * mixk, true);
        if (on && ! runs.empty())
            for (int i = 0; i < 9; ++i)
            {
                const auto& r = runs[(size_t) std::floor (hsh (i, 3) * (float) runs.size())]; const float c = std::fmod (((float) T.f / 24) * (0.05f + 0.06f * hsh (i, 5)) + hsh (i, 7), 1.0f), x0 = lerp (r.a, r.b, hsh (i, 9)), y = YS + 4 + c * 110;
                G.ell (140 + (x0 - 140) * (1 + 0.72f * (y - YS) / (300 - YS)) + std::sin (c * 9 + (float) i) * 3, y, 1.1f, 1.1f, K (look == Look::neon ? HSL { 146, 80, 84 } : HSL { 52, 100, 94 }, 0.75f * gp * std::sin (c * PI) * mixk));
            }
        const float litAtHit = ! patOn ? 1.0f : lv (gatepv::swing (juce::jmin (juce::jmax (0.0f, posAt (lastHit + 0.01f)), 0.99999f), sw)), gpHit = pm > 0 && pm < 5 ? pumpNow (F, (lastHit + 0.02f) / spb, 0, S, on, pm) : gp;
        MoOpt mo; mo.noHit = litAtHit * gpHit < 0.4f; const auto m = gmo (F, S, T, mo); RigP P = m.P; P.s = 40; P.x = 140; P.y = 266 - lvl * lit * 2; P.tail = 0.25f; const Rig R = ghostRig (P);
        if (lit > 0.05f) G.fill ({ { R.mid.x - R.halfW * 0.8f, 270 }, { R.mid.x + R.halfW * 0.8f, 270 }, { R.mid.x + R.halfW * 1.7f, 300 }, { R.mid.x - R.halfW * 1.7f, 300 } }, K (veilC, 0.34f * lit * sndK * mixk));
        glow (G, R.mid.x, R.mid.y, 1.5f, LC, (0.12f + 0.4f * lit) * mixk * (on ? 1.0f : 0.4f));
        const float dkA = (1 - lit) * 0.5f * mixk;
        GL L; L.slime = HSL { 84, 80, 56 }; L.eyes = ! on ? "closed" : face < 0.3f ? "closed" : m.hp.on && ! m.hp.eyes.empty() ? m.hp.eyes : face > 0.8f && lvl > 0.3f ? "happy" : "open"; L.mouth = ! on || face < 0.3f ? "flat" : lvl * lit > 0.5f ? "grin" : "smile"; L.blush = 0.5f; L.glow = 0; L.rim = LC;
        if (look == Look::neon) { L.tube = HSL { 146, 100, 62 }; L.slime = HSL { 84, 100, 56 }; L.k = (0.3f + 0.7f * juce::jmax (lit, 1 - mixk)) * (0.9f + 0.1f * hsh (T.tick, 5)); } else if (dkA > 0.02f) { L.tint = veilC; L.tintA = dkA; }
        ghost (G, R, L, T);
        if (! on) offVeil (G);
        stageOver (G);
    }
    // DAWN: behind the curve canvas of the editor (22,358,632,244; 1 su = 1 px)
    bool hasDawn() const override { return true; }
    std::vector<std::string> dawnControls() const override { return { "pattern", "length", "pmode", "pdepth", "pshape", "prel", "phold", "pthr", "ponly", "swing", "trig", "mix" }; }
    juce::Rectangle<float> dawnRect() const override { return { 22, 358, 632, 244 }; }
    void dawnScene (G& G, const Feed& F, State& S, const Clock& T, float W, float H) override
    {
        const bool on = F.on; const float mixk = mixA (F.v ("mix")); const int pat = juce::jlimit (0, 15, juce::roundToInt (F.v ("pattern")) - 1);
        static const int lens[3] { 2, 4, 8 }; const int len = lens[juce::jlimit (0, 2, juce::roundToInt (F.v ("length")))], pm = juce::jlimit (0, 5, juce::roundToInt (F.v ("pmode")));
        const bool patOn = pm == 0 || F.v ("ponly") < 0.5f, audio = F.v ("trig") > 0.5f; const float sw = F.v ("swing");
        const float bt = (float) T.t * BPM / 60, lastHit = S.hits.empty() ? -9.0f : (float) S.hits.back(), spb = 60 / BPM, lvl = on ? F.level : 0; auto lv = [&] (float u) { return level (F, pat, u); };
        const float pos = F.hasPos() ? F.pos : ! patOn ? -1.0f : audio ? clampf (((float) T.t - lastHit) / ((float) len * spb), 0, 1) : std::fmod (((float) T.t / spb) / (float) len, 1.0f);
        const float aNow = pos < 0 ? 1.0f : lv (gatepv::swing (juce::jmin (pos, 0.99999f), sw)), gp = F.hasPump() ? F.pump : pumpNow (F, bt, F.level, S, on, pm), lit = on ? aNow * gp : 0.0f, face = S.follow ("gf", lit, 0.7f, 0.3f);
        const int N = 32; std::vector<Cell> cells; for (int k = 0; k < N; ++k) cells.push_back ({ W * gatepv::swingInv ((float) k / N, sw), W * gatepv::swingInv ((float) (k + 1) / N, sw), patOn ? lv (((float) k + 0.5f) / N) : 1.0f });
        G.rect (0, 0, W, H, K ({ 204, 50, 9 }, 0.3f * mixk)); for (float q : { 0.14f, 0.44f, 0.74f }) glow (G, W * q + 40, H * 0.7f, 2.1f, { 44, 90, 60 }, 0.5f * mixk, true);
        glow (G, W * 0.8f, H * 0.6f, 2.6f, { 48, 100, 72 }, 0.45f * mixk); G.ell (W * 0.8f, H * 0.62f, 16, 16, K ({ 52, 100, 82 }, 0.4f * mixk));
        G.fill (smooth ({ { 0, H }, { 0, H * 0.72f }, { 70, H * 0.64f }, { 150, H * 0.74f }, { 240, H * 0.66f }, { 330, H * 0.76f }, { 420, H * 0.68f }, { 520, H * 0.75f }, { W, H * 0.66f }, { W, H } }, true, 3), K ({ 120, 34, 24 }, 0.62f * mixk));
        G.fill (smooth ({ { 0, H }, { 0, H * 0.84f }, { 110, H * 0.79f }, { 230, H * 0.87f }, { 360, H * 0.81f }, { 480, H * 0.88f }, { W, H * 0.82f }, { W, H } }, true, 3), K ({ 150, 36, 15 }, 0.7f * mixk));
        std::vector<Poly> faces, thin, edge;
        for (auto& c : cells) { const float w = c.xb - c.xa, fw = juce::jmax (1.4f, w * (1 - c.a)), xc = (c.xa + c.xb) / 2; Poly f { { xc - fw / 2, 0 }, { xc + fw / 2, 0 }, { xc + fw / 2, H }, { xc - fw / 2, H } }; (fw < 2.4f ? thin : faces).push_back (f); if (fw >= 2.4f && c.a > 0.06f) { edge.push_back ({ { xc - fw / 2, 0 }, { xc - fw / 2, H } }); edge.push_back ({ { xc + fw / 2, 0 }, { xc + fw / 2, H } }); } }
        G.fillMany (thin, K ({ 44, 40, 44 }, 0.34f * (patOn ? 1.0f : 0.5f))); G.fillMany (faces, K ({ 206, 46, 8 }, 0.66f)); G.strokeMany (edge, 1, K ({ 46, 90, 66 }, 0.28f));
        if (pm > 0)
        {
            const float yb = (1 - gp) * (H - 8);
            if (yb > 2) { G.rect (0, 0, W, yb, K ({ 170, 34, 9 }, 0.6f)); std::vector<Poly> ls; for (int i = 0; i < (int) std::floor (yb / 12); ++i) ls.push_back ({ { 0, 6 + (float) i * 12 }, { W, 6 + (float) i * 12 } }); G.strokeMany (ls, 1, K ({ 160, 30, 20 }, 0.5f)); }
            G.stroke ({ { 0, yb + 2 }, { W, yb + 2 } }, 4, K ({ 30, 36, 34 }, 0.8f), { false, false }); G.stroke ({ { 0, yb + 0.6f }, { W, yb + 0.6f } }, 1, K ({ 46, 70, 62 }, 0.6f), { false, false });
        }
        if (on && pos >= 0) { const float xp = W * pos; glow (G, xp, H * 0.5f, 2.6f, { 50, 100, 80 }, 0.2f * (0.3f + 0.7f * aNow) * gp * mixk); G.stroke ({ { xp, 0 }, { xp, H } }, 5, K ({ 50, 100, 80 }, 0.1f * (0.3f + 0.7f * aNow) * mixk), { false, false }); }
        if (on) for (int i = 0; i < 12; ++i) { const auto& c = cells[(size_t) std::floor (hsh (i, 3) * N)]; const float cy = std::fmod (((float) T.f / 24) * (0.03f + 0.04f * hsh (i, 5)) + hsh (i, 7), 1.0f); if (c.a > 0.3f) G.ell (lerp (c.xa, c.xb, 0.5f) + std::sin (cy * 9 + (float) i) * 4, H - 10 - cy * (H - 30), 1.2f, 1.2f, K ({ 52, 100, 88 }, 0.5f * c.a * gp * std::sin (cy * PI) * mixk)); }
        MoOpt mo; mo.noHit = lit < 0.3f; const auto m = gmo (F, S, T, mo); RigP P = m.P; P.s = 21; P.x = W - 34; P.y = H - 13 - lvl * lit * 1.5f; P.tail = 0.25f; const Rig R = ghostRig (P);
        glow (G, R.mid.x, R.mid.y, 1, { 48, 100, 78 }, (0.1f + 0.3f * lit) * mixk * (on ? 1.0f : 0.4f));
        GL L; L.slime = HSL { 84, 80, 56 }; L.eyes = ! on || face < 0.3f ? "closed" : m.hp.on && ! m.hp.eyes.empty() ? m.hp.eyes : "open"; L.mouth = ! on || face < 0.3f ? "flat" : "smile"; L.blush = 0.4f; L.glow = 0; L.lite = true; L.a = 0.45f + 0.3f * face;
        if (lit < 0.98f) { L.tint = HSL { 204, 50, 9 }; L.tintA = (1 - lit) * 0.45f; }
        ghost (G, R, L, T);
    }
};
} // namespace phantom::pv
