// SPDX-License-Identifier: AGPL-3.0-or-later
// FILTER (port of phantom-anime/visuals/src/u_filter.js): a dark room and a wall of light at the back. The top edge of the light IS
// the filter's curve (lows left, highs right); the ghost in the middle is lit or left in the dark.
#pragma once
#include "Hands.h"

namespace phantom::pv
{
class FilterScene final : public Scene
{
public:
    std::vector<std::string> controls() const override { return { "type", "poles", "routing", "cutoff", "reso", "spread", "drive", "lfoamt", "lfoshape", "lfosync", "lfodiv", "lforate", "envamt", "envrel", "envatk", "mix" }; }
    std::vector<float> defaults() const override { return { 0, 1, 0, 0.593f, 0.4f, 0.625f, 0.2f, 0.5f, 0, 1, 9, 0.3f, 0.5f, 0.4f, 0.2f, 1 }; }
    enum Kind { lp, bp, hp, no, cp, cm, ap, shk };
    static Kind kindOf (int ti) { static const Kind k[44] { lp, bp, hp, no, lp, hp, bp, lp, hp, bp, no, lp, hp, lp, cp, cm, lp, lp, hp, bp, lp, hp, bp, no, lp, hp, bp, no, ap, shk, no,
                                                              lp, hp, bp, no, lp, lp, lp, bp, hp, lp, bp, hp, no }; return k[ti]; }   // 31..43: the 0.1.16 models (dsp/FilterModels.h)
    static int famOf (int ti) { static const int f[44] { 0, 0, 0, 0, 1, 1, 1, 2, 2, 2, 2, 3, 3, 4, 0, 0, 5, 6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 8, 0, 0, 0, 0, 0, 0, 0, 7, 1, 1, 0, 0, 3, 3, 3, 3 }; return f[ti]; }
    static float dB (Kind kind, int fam, float u, float n, float reso)
    {
        const float R0 = 26 * std::pow (reso, 1.5f) * (fam == 3 ? 1.15f : fam == 6 ? 0.7f : 1.0f), w = fam == 2 ? 0.5f : fam == 3 ? 0.22f : fam == 6 ? 0.45f : 0.32f;
        auto pk = [&] (float r, float ww) { return r * std::exp (-std::pow (u / ww, 2.0f)); };
        if (kind == lp || kind == hp || kind == shk)
        {
            const float uu = kind == hp ? -u : u, nn = fam == 5 ? 3 : fam == 4 ? n * 0.85f : n; float a = -10 * std::log10 (1 + std::pow (2.0f, 2 * nn * uu)) + pk (R0, w);
            if (fam == 1 && uu < 0) a -= 6 * reso * (1 - std::exp (uu)); if (fam == 7) a += 1.7f * std::sin (u * 5); if (fam == 8) a += pk (R0 * 0.5f, w * 1.6f) * std::sin (u * 14) * 0.5f; return a;
        }
        const float Q = (0.7f + 7 * reso * reso) * (n > 2 ? 1.6f : 1.0f), x = Q * (std::pow (2.0f, u) - std::pow (2.0f, -u));
        if (kind == bp) return -10 * std::log10 (1 + x * x) + (fam == 7 ? 1.7f * std::sin (u * 5) : 0.0f) + (fam == 8 ? 2 * std::sin (u * 14) * std::exp (-u * u) : 0.0f);
        if (kind == no) return juce::jmax (-60.0f, 10 * std::log10 (x * x / (1 + x * x) + 1e-6f));
        if (kind == cp || kind == cm) { const float f = std::pow (2.0f, juce::jmin (u, 2.6f)), c = kind == cp ? std::abs (std::cos (f * PI * 0.5f)) : std::abs (std::sin (f * PI * 0.5f)), d = 0.4f + 0.58f * reso; return 20 * std::log10 (juce::jmax (0.004f, 1 - d + d * c)); }
        return 0;
    }
    struct Now { float c, lfo, env, mvL, mvE; };
    static Now now (const Feed& F, const State& S, const Clock& T, bool on, Kind kind)
    {
        const float mvL = F.v ("lfoamt") * 2 - 1, mvE = F.v ("envamt") * 2 - 1; float c = F.v ("cutoff"), lfo = 0, env = 0;
        if (on && (std::abs (mvL) > 0.02f || kind == shk))
        {
            const float ph = F.v ("lfosync") > 0.5f ? (float) T.t * 2 / DIVB[juce::jlimit (0, 15, juce::roundToInt (F.v ("lfodiv")))] : (float) T.t * (0.05f + 19.95f * std::pow (F.v ("lforate"), 2.2f));
            lfo = lfoShape (kind == shk ? 4 : juce::roundToInt (F.v ("lfoshape")), ph, 5); c += (kind == shk && std::abs (mvL) <= 0.02f ? 0.6f : mvL) * 0.25f * lfo;
        }
        if (on && std::abs (mvE) > 0.02f && ! S.hits.empty())
        {
            const float dt = (float) (T.t - S.hits.back()), atk = 0.0005f + 0.2f * F.v ("envatk") * F.v ("envatk"), rel = 0.01f + 0.99f * F.v ("envrel") * F.v ("envrel");
            env = dt < 0 ? 0.0f : dt < atk ? dt / atk : std::exp (-(dt - atk) / rel); c += mvE * 0.3f * env;
        }
        return { clampf (c, 0, 1), lfo, env, mvL, mvE };
    }
    void scene (G& G, const Feed& F, State& S, const Clock& T) override
    {
        const Look look = G.look; const auto m = gmo (F, S, T); const bool on = m.on; const float lvl = m.lvl; const int k = m.k; const float mix = mixA (F.v ("mix")); const HSL acc { 261, 100, 71 };
        const int ti = juce::jlimit (0, 43, juce::roundToInt (F.v ("type"))); const Kind kind = kindOf (ti); const int fam = famOf (ti); const float n = F.v ("poles") > 0.5f ? 4.0f : 2.0f, reso = F.v ("reso"), drive = F.v ("drive"); const int route = juce::roundToInt (F.v ("routing"));
        const auto nw = now (F, S, T, on, kind); const float cut = nw.c, X0 = 22, XW = 236, OCT = XW / 9.966f, xc = X0 + XW * cut, xc2 = xc + (F.v ("spread") * 8 - 4) * OCT, Y0 = 180, HS = 62;
        const int N = (kind == cp || kind == cm) ? 96 : 56; Poly pts; std::vector<float> hs; int ip = 0;
        for (int i = 0; i <= N; ++i)
        {
            const float x = 10 + 260 * (float) i / (float) N; float a = dB (kind, fam, (x - xc) / OCT, n, reso);
            if (route) { const float a2 = dB (kind, fam, (x - xc2) / OCT, n, reso); a = route == 1 ? a + a2 : juce::jmax (a, a2); }
            const float h = clampf (1 + a / 42, 0, 1.6f); hs.push_back (h); pts.push_back ({ x, Y0 - HS * h }); if (h > hs[(size_t) ip]) ip = i;
        }
        auto hAt = [&] (float x) { return hs[(size_t) juce::jlimit (0, N, juce::roundToInt ((x - 10) / 260 * (float) N))]; };
        const float lit = clampf ((hAt (124) + hAt (140) * 2 + hAt (156)) / 4, 0, 1), b = lerp (1, lit, mix);
        const HSL lc = mixrgb (look == Look::neon ? HSL { 262, 100, 72 } : look == Look::dawn ? HSL { 52, 90, 90 } : HSL { 262, 74, 84 }, look == Look::neon ? HSL { 28, 100, 60 } : look == Look::dawn ? HSL { 24, 100, 62 } : HSL { 34, 100, 70 }, drive * 0.85f);
        const float fl = (k >= 0 && k < 2 ? 1.3f : 1.0f) * (0.72f + 0.28f * lvl) * mix * (on ? 1.0f : 0.5f), dense = 1 + 0.5f * drive;
        stageUnder (G);
        const bool painted = look != Look::neon;
        auto vc = [&] (float a) { return look == Look::dawn ? K ({ 200, 60, 5 }, a * 1.14f) : K ({ 262, 50, 7 }, a); };
        auto fan = [&] (float x, float y) { return 140 + (x - 140) * (1 + 0.32f * (y - Y0) / (256 - Y0)); };
        if (look == Look::neon) G.rect (0, 0, 280, 300, K ({ 250, 60, 4 }, 0.3f * mix));
        if (painted)
        {
            Poly d { { 0, 0 }, { 280, 0 }, { 280, pts[(size_t) N].y } }; for (int i = N; i >= 0; --i) d.push_back (pts[(size_t) i]); d.push_back ({ 0, pts[0].y }); G.fill (d, vc (0.56f * mix));
            G.fillMany ({ { { 0, Y0 }, { 10, Y0 }, { fan (10, 300), 300 }, { 0, 300 } }, { { 280, Y0 }, { 270, Y0 }, { fan (270, 300), 300 }, { 280, 300 } } }, vc (0.56f * mix));
        }
        { std::vector<Poly> mk; for (float q : { 0.233f, 0.566f, 0.9f }) mk.push_back ({ { X0 + XW * q, Y0 }, { X0 + XW * q, Y0 + 5 } }); G.strokeMany (mk, 1, K ({ 262, 30, 70 }, 0.5f)); }
        G.fill (concat (pts, { { 270, Y0 }, { 10, Y0 } }), K (lc, (painted ? 0.1f + 0.12f * drive : 0.2f) * fl * dense));
        { Poly band = pts; for (int i = N; i >= 0; --i) band.push_back ({ pts[(size_t) i].x, juce::jmin (Y0, pts[(size_t) i].y + 13) }); G.fill (band, K (lc, (painted ? 0.3f : 0.22f) * fl * dense)); }
        { std::vector<Poly> st; int j = 0; for (int i = 0; i <= N; ++i) if (i % 2 == 0 && hs[(size_t) i] > 0.06f && hsh (i, 3) > 0.25f) { st.push_back ({ { pts[(size_t) i].x, Y0 }, { pts[(size_t) i].x, pts[(size_t) i].y + 2 + hsh (j, 5) * 8 } }); ++j; } G.strokeMany (st, 1.1f, K (lc, (painted ? 0.3f : 0.14f) * fl)); }
        for (int j = 0; j < 8; ++j)
        {
            const float xa = 10 + 260 * (float) j / 8, xb = xa + 32.5f, yf = painted ? 300.0f : 256.0f; float av = 0; for (int i = 0; i <= 6; ++i) av += juce::jmin (1.0f, hAt (xa + 32.5f * (float) i / 6)); av /= 7;
            const Poly q { { xa, Y0 }, { xb, Y0 }, { fan (xb, yf), yf }, { fan (xa, yf), yf } };
            if (painted && av < 0.97f) G.fill (q, vc (0.56f * (1 - av) * mix)); if (av >= 0.04f) G.fill (q, K (lc, (painted ? 0.07f + 0.14f * drive : 0.26f) * av * fl * dense));
        }
        if (painted && mix > 0.05f)
        {
            std::vector<Poly> bm; for (int j = 0; j < 7; ++j) { const float x = 26 + (float) j * 36 + hsh (j, 9) * 10, h = juce::jmin (1.0f, hAt (x)); if (h < 0.25f) continue; const float w = 7 + hsh (j, 4) * 10, yt = Y0 - HS * h + 3; bm.push_back ({ { x, yt }, { x + w, yt }, { x + w - 17, 256 }, { x - 23, 256 } }); }
            G.fillMany (bm, K (lc, (look == Look::dawn ? 0.24f : 0.14f) * fl));
            for (int j = 0; j < 4; ++j) { const float x = 42 + (float) j * 65, h = juce::jmin (1.0f, hAt (x)); if (h > 0.4f) glow (G, x, Y0 - HS * h * 0.4f, 2, look == Look::dawn ? mixrgb ({ 52, 90, 86 }, { 24, 100, 62 }, drive * 0.85f) : lc, (look == Look::dawn ? 0.26f : 0.14f) * h * fl); }
            for (int j = 0; j < 3; ++j) { const float x = 60 + (float) j * 80, h = juce::jmin (1.0f, hAt (x)); if (h > 0.4f) glow (G, fan (x, 222), 222, 1.5f, lc, (look == Look::dawn ? 0.4f : 0.22f) * h * fl, true); }
        }
        if (painted) G.stroke (pts, 5.5f + 2 * drive, K (lc, 0.3f * fl));
        { LineOpt lo; lo.a = juce::jmin (1.0f, 0.95f * fl + 0.1f); lo.halo = 1; lo.ta = 0.02f; lo.tb = 0.02f; lo.sd = 2; LN (G, pts, 2.2f + 1.7f * drive, lc, lo); }
        if (painted) G.stroke (pts, 1, K ({ 50, 70, 98 }, 0.85f * juce::jmin (1.0f, fl)));
        struct Sl { int i; V2 p, t, n; }; std::vector<Sl> sl;
        for (int i = 1; i < N; ++i) if (hs[(size_t) i] > 0.1f && hs[(size_t) i] < 0.94f && std::abs (hs[(size_t) i + 1] - hs[(size_t) i - 1]) > 0.012f)
        { const V2 p = pts[(size_t) i], d { pts[(size_t) i + 1].x - pts[(size_t) i - 1].x, pts[(size_t) i + 1].y - pts[(size_t) i - 1].y }; const float l = hyp (d.x, d.y) != 0 ? hyp (d.x, d.y) : 1, up = d.x > 0 ? 1.0f : -1.0f; sl.push_back ({ i, p, { d.x / l, d.y / l }, { d.y / l * up, -d.x / l * up } }); }
        if (fam && ! sl.empty() && mix > 0.05f)
        {
            std::vector<Sl> mk; for (size_t j = 0; j < sl.size(); ++j) if (j % 3 == 1) mk.push_back (sl[j]); const auto mc = K (lc, 0.9f * mix);
            if (fam == 1) { std::vector<Poly> rail; for (size_t j = 0; j + 1 < sl.size(); ++j) if (sl[j + 1].i == sl[j].i + 1) rail.push_back ({ { sl[j].p.x + sl[j].n.x * 6, sl[j].p.y + sl[j].n.y * 6 }, { sl[j + 1].p.x + sl[j + 1].n.x * 6, sl[j + 1].p.y + sl[j + 1].n.y * 6 } }); for (auto& e : mk) rail.push_back ({ e.p, { e.p.x + e.n.x * 6, e.p.y + e.n.y * 6 } }); G.strokeMany (rail, 1.1f, mc); }
            if (fam == 2) for (auto& e : mk) G.ell (e.p.x, e.p.y, 2.3f, 2.3f, mc);
            if (fam == 3) { std::vector<Poly> ps; for (auto& e : mk) ps.push_back ({ { e.p.x - 3, e.p.y }, { e.p.x, e.p.y - 3.6f }, { e.p.x + 3, e.p.y }, { e.p.x, e.p.y + 3.6f } }); G.fillMany (ps, mc); }
            if (fam == 4) { std::vector<Poly> ps; for (auto& e : mk) ps.push_back ({ { e.p.x + e.t.x * 3.6f, e.p.y + e.t.y * 3.6f }, { e.p.x - e.t.x * 2.4f + e.n.x * 3, e.p.y - e.t.y * 2.4f + e.n.y * 3 }, { e.p.x - e.t.x * 2.4f - e.n.x * 3, e.p.y - e.t.y * 2.4f - e.n.y * 3 } }); G.fillMany (ps, mc); }
            if (fam == 5) for (size_t j = 0; j < mk.size() && j < 3; ++j) G.ell (mk[j].p.x, mk[j].p.y, 2, 2, mc);
            if (fam == 6) { std::vector<Poly> ps; for (auto& e : mk) ps.push_back ({ { e.p.x - e.n.x * 3.4f, e.p.y - e.n.y * 3.4f }, { e.p.x + e.n.x * 3.4f, e.p.y + e.n.y * 3.4f } }); G.strokeMany (ps, 1.2f, mc); }
        }
        if (kind == ap) { auto c = arcp (xc, Y0 - HS - 7, 6, 7, PI * 0.5f, PI * 2.5f, 14); for (size_t i = 0; i < c.size(); ++i) c[i].x += ((float) i - 7) * 0.9f; LineOpt lo; lo.a = 0.95f * mix; lo.halo = 1; lo.ta = 0.05f; lo.tb = 0.05f; LN (G, c, 1.6f, lc, lo); }
        if (route && mix > 0.05f) { Poly p2; for (int i = 0; i <= 28; ++i) { const float x = 10 + 260 * (float) i / 28; p2.push_back ({ x, Y0 - HS * clampf (1 + dB (kind, fam, (x - xc2) / OCT, n, reso) / 42, 0, 1.6f) }); } StrokeOpt so; so.dash = { 3, 3 }; G.stroke (p2, 1, K ({ 186, 90, 70 }, 0.75f * mix), so); }
        if (reso > 0.2f && (kind == lp || kind == hp || kind == bp || kind == shk) && mix > 0.05f)
        {
            const auto q = pts[(size_t) ip]; const HSL hot = mixrgb ({ 262, 100, 88 }, { 40, 100, 86 }, drive);
            glow (G, q.x, q.y, 0.5f + 1.3f * reso, look == Look::neon ? HSL { 262, 100, 74 } : HSL { 262, 90, 88 }, 0.75f * reso * fl);
            LineOpt lo; lo.a = (0.45f + 0.5f * reso) * juce::jmin (1.0f, fl + 0.2f); lo.ta = 0.3f; lo.tb = 0.05f; lo.halo = 1; LN (G, { { q.x, q.y - 6 - 34 * reso }, { q.x, Y0 + 2 } }, 1.3f + reso, hot, lo);
            if (reso > 0.7f) glint (G, q.x, q.y, reso > 0.9f ? 1.25f : 0.8f, { 50, 60, 97 }, 0.95f * mix);
        }
        const float yb = 268, xb0 = X0 + XW * F.v ("cutoff");
        G.fill ({ { xc, yb - 7 }, { xc - 4.6f, yb }, { xc + 4.6f, yb } }, K (acc, 1));
        LineOpt l0; l0.a = 0.9f; l0.ta = 0; l0.tb = 0; l0.halo = 0;
        if (std::abs (nw.mvL) > 0.02f)
        {
            const float xa = X0 + XW * clampf (F.v ("cutoff") - std::abs (nw.mvL) * 0.25f, 0, 1), xz = X0 + XW * clampf (F.v ("cutoff") + std::abs (nw.mvL) * 0.25f, 0, 1);
            { LineOpt lo = l0; lo.a = 0.85f; LN (G, { { xa, yb - 2 }, { xa, yb + 4 }, { xz, yb + 4 }, { xz, yb - 2 } }, 1.2f, acc, lo); }
            const V2 pv { 34, 22 }; const float an = nw.lfo * 0.85f * (nw.mvL > 0 ? 1.0f : -1.0f), len = 14 + 22 * std::abs (nw.mvL); const V2 bob { pv.x + std::sin (an) * len, pv.y + std::cos (an) * len };
            LN (G, { pv, bob }, 1.2f, acc, l0); G.ell (bob.x, bob.y, 4.4f, 4.4f, K (acc, 1)); G.ell (pv.x, pv.y, 1.8f, 1.8f, K (acc, 0.9f)); G.arc (pv.x, pv.y, len, len, PI * 0.5f - 0.85f, PI * 0.5f + 0.85f, 0.8f, K (acc, 0.35f));
        }
        if (std::abs (nw.mvE) > 0.02f) { const float xe = clampf (xb0 + nw.mvE * 0.3f * XW, 14, 266), d = nw.mvE > 0 ? 1.0f : -1.0f; LN (G, { { xb0, yb + 10 }, { xe, yb + 10 } }, 1.2f, { 40, 100, 66 }, l0); LN (G, { { xe - d * 5, yb + 6.5f }, { xe, yb + 10 }, { xe - d * 5, yb + 13.5f } }, 1.2f, { 40, 100, 66 }, l0); }
        RigP P = m.P; P.s = 42; P.x = 140; P.y = 247 - lvl * 2 * b; P.tail = 0.25f; const Rig R = ghostRig (P);
        if (b > 0.2f) glow (G, R.mid.x, R.mid.y, 1.5f, look == Look::neon ? HSL { 262, 100, 74 } : HSL { 262, 70, 88 }, 0.34f * b * fl);
        const bool near = std::abs (xc - 140) < 28 && reso > 0.6f;
        GL L; L.slime = HSL { 20, 86, 62 }; L.eyes = ! on ? "closed" : m.hp.on && ! m.hp.eyes.empty() ? m.hp.eyes : b < 0.3f ? "closed" : near ? "wide" : "open"; L.mouth = ! on || b < 0.3f ? "flat" : near ? "o" : "smile"; L.blush = 0.6f * b; L.glow = 0;
        if (look == Look::neon) { L.tube = HSL { 261, 100, 70 }; L.slime = HSL { 24, 100, 58 }; L.k = (0.3f + 0.7f * b) * (0.9f + 0.1f * hsh (T.tick, 5)); }
        else if (b < 0.98f) { L.tint = HSL { 258, 60, 9 }; L.tintA = (1 - b) * 0.6f; }
        ghost (G, R, L, T);
        if (on && mix > 0.05f) for (int i = 0; i < 7; ++i) { const float x = 20 + hsh (i, 1) * 240 + std::sin ((float) T.f * 0.05f + (float) i) * 6, h = juce::jmin (1.0f, hAt (x)); if (h < 0.3f) continue; const float y = Y0 - std::fmod (hsh (i, 2) + (float) T.f / 24 * 0.08f * (0.5f + hsh (i, 3)), 1.0f) * HS * h; G.ell (x, y, 1, 1, K ({ 50, 60, 96 }, 0.7f * mix)); }
        if (! on) offVeil (G);
        stageOver (G);
    }
};
} // namespace phantom::pv
