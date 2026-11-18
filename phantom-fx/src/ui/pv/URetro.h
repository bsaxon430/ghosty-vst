// SPDX-License-Identifier: AGPL-3.0-or-later
// RETRO (port of phantom-anime/visuals/src/u_retro.js): an old tape machine. Two reels turn at the top, the tape runs between them
// past the heads, and under them a small screen shows the ghost the way the machine plays it back. Six lamps under the reels are the
// six section switches. DAWN: the same machine, small, in its own pane on the RETRO page.
#pragma once
#include "UCrackle.h"

namespace phantom::pv
{
namespace retropv
{
    inline const HSL DC[5] { { 28, 90, 52 }, { 42, 100, 56 }, { 200, 50, 72 }, { 120, 60, 66 }, { 320, 90, 62 } };
    inline const HSL LC[6] { { 50, 60, 90 }, { 196, 90, 66 }, { 28, 100, 58 }, { 146, 100, 60 }, { 279, 90, 78 }, { 353, 100, 64 } };
    struct Lay { float bx, by, bw, bh; V2 rl, rr; float r, ly, sx, sy, sw, sh, gs, gy, k; };
    inline const Lay STAGE { 16, 10, 248, 280, { 86, 68 }, { 194, 68 }, 38, 122, 44, 138, 192, 134, 34, 263, 1 };
    inline const Lay MINI { 1, 1, 92, 162, { 25, 26 }, { 69, 26 }, 18.5f, 51, 5, 59, 84, 99, 21, 152, 0.55f };
}

class RetroScene final : public Scene
{
public:
    std::vector<std::string> controls() const override { return { "magic", "noise_on", "noise", "ntype", "ntone", "nfollow", "wobble_on", "wobble", "flutter", "dist_on", "dist", "dtype", "digi_on", "digi", "space_on", "space", "mag_on", "mag", "bias", "speed", "degrade", "tone", "out", "mix" }; }
    std::vector<float> defaults() const override { return { 0.5f, 1, 0.3f, 0, 0.5f, 0, 1, 0.3f, 0.3f, 1, 0.3f, 0, 0, 0.3f, 0, 0.3f, 0, 0.3f, 0.5f, 1, 0, 0.5f, 0.667f, 1 }; }
    static void machine (G& G, const Feed& F, State& S, const Clock& T, const retropv::Lay& Z, bool mini)
    {
        const Look look = G.look; const auto m = gmo (F, S, T); const bool on = m.on; const float lvl = m.lvl, mixk = mixA (F.v ("mix")), k = Z.k; const bool neon = look == Look::neon;
        const HSL dk = L3<HSL> (look, { 262, 30, 14 }, INKC, { 262, 60, 30 }); const float bs = 3 + (float) boilOf (T, look == Look::dawn ? 4 : 3) * 1.7f;
        const float mg = clampf (0.3f + 1.4f * F.v ("magic"), 0, 1.6f) * mixk; auto am = [&] (const char* sw, const char* a) { return F.v (sw) > 0.5f ? clampf (F.v (a) * mg, 0, 1) : 0.0f; };
        const float nz = am ("noise_on", "noise"), wb = am ("wobble_on", "wobble"), fl = am ("wobble_on", "flutter"), ds = am ("dist_on", "dist"), dg = am ("digi_on", "digi"), sp = am ("space_on", "space"), mgn = am ("mag_on", "mag"), deg = clampf (F.v ("degrade") * mixk, 0, 1);
        const int nt = juce::jlimit (0, 7, juce::roundToInt (F.v ("ntype"))), dt = juce::jlimit (0, 4, juce::roundToInt (F.v ("dtype"))), si = juce::jlimit (0, 2, juce::roundToInt (F.v ("speed")));
        static const float spds[3] { 0.5f, 1, 2 }; const float spd = spds[si], tn = F.v ("tone") * 2 - 1, out = 0.35f + 0.9f * F.v ("out"); const HSL acc { 38, 71, 69 }; const float sx = Z.sx, sy = Z.sy, sw = Z.sw, sh = Z.sh;
        // the case
        const auto body = rbox (Z.bx, Z.by, Z.bw, Z.bh, 9 * k + 3, 3); const Mat caseM = look == Look::dawn ? Mat { { 28, 36, 38 }, { 16, 36, 20 }, {}, HSL { 46, 70, 74 } } : Mat { { 250, 14, 40 }, { 256, 24, 22 } };
        if (neon) { G.fill (body, K (pal::neonGlass, 0.86f)); TubeOpt to; to.closed = true; to.halo = mini ? 0.0f : 1.0f; to.k = 0.7f; tubeLine (G, body, 1.4f * juce::jmax (0.7f, k), acc, to); }
        else
        {
            SolidOpt so; so.iw = mini ? 1.3f : 2.0f; so.sd = bs; so.shw = mini ? 2.4f : 6.0f; so.rimw = 2 * k + 0.6f; solid (G, mini ? body : wob (body, 0.6f, 0.04f, bs), caseM, so);
            const auto pl = rbox (Z.bx + 7 * k, Z.by + 7 * k, Z.bw - 14 * k, Z.ly + 9 * k - Z.by, 5 * k + 1, 2); G.fill (pl, K (L3<HSL> (look, { 36, 26, 56 }, { 214, 14, 58 }, {})));
            Poly pt; for (auto q : pl) pt.push_back ({ q.x, juce::jmin (q.y, Z.by + 7 * k + (Z.ly + 9 * k - Z.by) * 0.5f) }); G.fill (pt, K (L3<HSL> (look, { 40, 30, 62 }, { 214, 16, 66 }, {}), 0.7f));
            if (look == Look::dawn && ! mini) { G.fill (blobp (60, Z.by - 1, 30, 5, 0.3f, 10, 5), K ({ 112, 44, 32 })); G.fill (blobp (214, Z.by, 22, 4.4f, 0.3f, 9, 8), K ({ 96, 50, 38 })); }
        }
        if (! mini)
        {
            const V2 sc_[4] { { Z.bx + 9, Z.by + 9 }, { Z.bx + Z.bw - 9, Z.by + 9 }, { Z.bx + 9, Z.by + Z.bh - 9 }, { Z.bx + Z.bw - 9, Z.by + Z.bh - 9 } }; std::vector<Poly> hs, sl;
            for (auto q : sc_) { hs.push_back (circ (q.x, q.y, 2.7f, 2.7f, 7)); sl.push_back ({ { q.x - 1.7f, q.y - 1.2f }, { q.x + 1.7f, q.y + 1.2f } }); } G.fillMany (hs, K (neon ? HSL { 38, 50, 40 } : dk)); G.strokeMany (sl, 0.8f, K (neon ? HSL { 38, 80, 80 } : HSL { 46, 60, 74 }));
            std::vector<Poly> gr; for (int i = 0; i < 15; ++i) gr.push_back (circ (Z.sx + 22 + (float) i * (Z.sw - 44) / 14, Z.by + Z.bh - 7.5f, 1.3f, 1.3f, 5)); G.fillMany (gr, K (neon ? HSL { 38, 71, 60 } : dk, 0.7f));
        }
        // the tape: its colour is the TAPE BIAS; FLUTTER makes it shiver, DEGRADE frays it, MAGNETIC drops bits out
        const HSL tc = mixrgb (L3<HSL> (look, { 30, 34, 60 }, { 30, 30, 62 }, { 38, 100, 66 }), L3<HSL> (look, { 18, 50, 17 }, { 16, 46, 16 }, { 20, 100, 40 }), F.v ("bias")); const float hx = (Z.rl.x + Z.rr.x) / 2, hy = Z.ly - 8 * k, sh_ = on ? (hsh (T.f, 3) - 0.5f) * 3 * fl * k : 0.0f;
        const Poly tp { { Z.rl.x + Z.r * 0.5f, Z.rl.y + Z.r * 0.84f }, { hx - 14 * k, hy + sh_ }, { hx + 14 * k, hy - sh_ }, { Z.rr.x - Z.r * 0.5f, Z.rr.y + Z.r * 0.84f } };
        // the reels: TAPE SPEED turns them, WOBBLE swings their speed
        if (S.dv.find ("rA") == S.dv.end()) S.dv["rA"] = 0; if (on) S.dv["rA"] += 0.12 * spd * (1 + 0.7 * wb * std::sin (T.t * 3.8));
        for (int i = 0; i < 2; ++i)
        {
            const V2 c = i ? Z.rr : Z.rl; const float r = Z.r, a0 = (float) S.dv["rA"] * (i ? 1.1f : 1.0f) + (float) i, pack = r * (i ? 0.56f : 0.74f); const Poly rim = circ (c.x, c.y, r, r, 22);
            if (neon) { G.fill (rim, K (pal::neonGlass, 0.7f)); G.ell (c.x, c.y, pack, pack, K (tc, 0.3f)); TubeOpt to; to.closed = true; to.halo = 0; tubeLine (G, rim, 1.2f * juce::jmax (0.7f, k), acc, to); tubeLine (G, circ (c.x, c.y, pack, pack, 16), 0.8f, tc, to); }
            else
            {
                Poly sd; for (auto q : rim) sd.push_back ({ q.x + 1.5f * k, q.y + 2 * k }); G.fill (sd, K (dk, 0.5f));
                SolidOpt so; so.iw = mini ? 1.1f : 1.6f; so.sd = bs + (float) i; so.shw = 3 * k + 1; solid (G, mini ? rim : wob (rim, 0.4f, 0.05f, bs + (float) i), MAT ("brass"), so);
                G.ell (c.x, c.y, pack, pack, K (tc)); G.ring (c.x, c.y, pack * 0.8f, pack * 0.8f, 0.8f, K ({ tc.h, tc.s, tc.l + 10 }, 0.6f));
            }
            std::vector<Poly> spk; for (int j = 0; j < 3; ++j) { const float a = a0 + (float) j * TAU / 3; spk.push_back ({ { c.x + std::cos (a) * r * 0.2f, c.y + std::sin (a) * r * 0.2f }, { c.x + std::cos (a) * r * 0.9f, c.y + std::sin (a) * r * 0.9f } }); }
            G.strokeMany (spk, neon ? 1.0f : juce::jmax (1.4f, 4 * k), K (neon ? acc : L3<HSL> (look, { 44, 60, 62 }, { 44, 60, 60 }, {})));
            G.ell (c.x, c.y, r * 0.2f, r * 0.2f, K (neon ? HSL { 38, 80, 80 } : dk)); G.ell (c.x, c.y, r * 0.08f, r * 0.08f, K (neon ? HSL { 50, 100, 90 } : HSL { 46, 70, 74 }));
        }
        if (neon) { TubeOpt to; to.halo = 0; tubeLine (G, tp, 1 * juce::jmax (0.7f, k), tc, to); }
        else { G.stroke (tp, juce::jmax (1.6f, 3.4f * k), K (dk)); StrokeOpt so; if (deg > 0.15f) so.dash = { 9 - 6 * deg, 1 + 3 * deg }; G.stroke (tp, juce::jmax (1.0f, 2.2f * k), K (tc), so); }
        if (mgn > 0.03f && on) for (int j = 0; j < 2; ++j) { const float q = hsh (j, T.tick / 3 + 5); const V2 p = hauntpv::lerp2 (tp[1], tp[2], q); G.ell (p.x, p.y, 3 * k * (1 + mgn), 1.6f * k, K (neon ? HSL { 262, 60, 20 } : L3<HSL> (look, { 36, 26, 56 }, { 214, 14, 58 }, {}), 0.95f)); }
        { const auto head = rbox (hx - 17 * k, hy - 2 * k, 34 * k, 10 * k, 2, 2); if (neon) { TubeOpt to; to.closed = true; to.halo = 0; tubeLine (G, head, 0.9f, { 262, 90, 66 }, to); } else { SolidOpt so; so.iw = mini ? 1.0f : 1.3f; so.sd = bs + 5; so.shw = 1.6f; solid (G, head, MAT ("steel"), so); } }
        {   // speed: three small marks over the heads, one lit
            const float sy_ = Z.rl.y - Z.r * 0.62f, sr = 2.4f * k + 0.5f; std::vector<Poly> ds3; for (int j = 0; j < 3; ++j) ds3.push_back (circ (hx + (float) (j - 1) * 9 * k, sy_, sr + 0.8f, sr + 0.8f, 8)); G.fillMany (ds3, K (neon ? HSL { 262, 50, 40 } : dk, 0.85f));
            G.ell (hx + (float) (si - 1) * 9 * k, sy_, sr, sr, K ({ 50, 100, 74 })); if (! mini) glow (G, hx + (float) (si - 1) * 9 * k, sy_, 0.5f, { 50, 100, 76 }, 0.75f);
        }
        {   // six lamps: NOISE, WOBBLE, DISTORT, DIGITAL, SPACE, MAGNETIC
            const float sws[6] { F.v ("noise_on"), F.v ("wobble_on"), F.v ("dist_on"), F.v ("digi_on"), F.v ("space_on"), F.v ("mag_on") }, as[6] { nz, juce::jmax (wb, fl), ds, dg, sp, mgn };
            for (int j = 0; j < 6; ++j) { const float lx = sx + sw * ((float) j + 0.5f) / 6, ly = Z.ly + 3 * k, r = 2.6f * k + 0.8f; const HSL c = j == 2 ? retropv::DC[dt] : retropv::LC[j]; G.ell (lx, ly, r + 1, r + 1, K (neon ? HSL { 262, 50, 30 } : dk)); G.ell (lx, ly, r, r, K (sws[j] > 0.5f ? c : HSL { c.h, 30, 22 })); if (sws[j] > 0.5f && ! mini) glow (G, lx, ly, 0.5f, c, 0.3f + 0.5f * as[j]); }
        }
        // the screen
        const auto scr = rbox (sx, sy, sw, sh, 12 * k + 2, 3), bez = rbox (sx - 5 * k, sy - 5 * k, sw + 10 * k, sh + 10 * k, 14 * k + 3, 3); const HSL sc = mixrgb (mixrgb (L3<HSL> (look, { 168, 34, 13 }, { 262, 34, 13 }, { 250, 50, 7 }), { 36, 60, 16 }, clampf (-tn, 0, 1) * 0.7f), { 204, 60, 26 }, clampf (tn, 0, 1) * 0.6f);
        if (neon) { G.fill (scr, K (sc, 0.92f)); TubeOpt to; to.closed = true; to.halo = 0; to.k = 0.7f; tubeLine (G, bez, 1.2f * juce::jmax (0.7f, k), { 262, 90, 66 }, to); }
        else { SolidOpt so; so.iw = mini ? 1.2f : 1.7f; so.sd = bs + 7; so.shw = 2.4f * k + 1; solid (G, bez, look == Look::dawn ? Mat { { 30, 26, 26 }, { 262, 24, 12 }, {}, HSL { 46, 60, 70 } } : MAT ("dark"), so); G.fill (scr, K (sc)); }
        glow (G, sx + sw / 2, sy + sh * 0.55f, mini ? 1.0f : 2.6f, mixrgb ({ 150, 60, 60 }, { 46, 90, 70 }, clampf (0.5f - tn * 0.5f, 0, 1)), (0.16f + 0.2f * lvl) * out * mixk);
        { std::vector<Poly> sl; for (int i = 0; i < (int) std::floor (sh / (7 * k + 3)); ++i) sl.push_back ({ { sx + 5 * k, sy + 6 * k + (float) i * (7 * k + 3) }, { sx + sw - 5 * k, sy + 6 * k + (float) i * (7 * k + 3) } }); G.strokeMany (sl, 0.7f, K ({ 150, 40, 70 }, 0.1f)); }
        const float sway = on ? (std::sin ((float) T.t * 3.8f) * 8 * wb + (hsh (T.f, 7) - 0.5f) * 5 * fl) * k : 0.0f, gx = sx + sw / 2 + sway;
        const float nA = nz * (F.v ("nfollow") > 0.5f ? clampf (0.15f + lvl * 1.3f, 0, 1) : 1.0f); const HSL ntc = mixrgb ({ 36, 22, 48 }, { 50, 50, 96 }, F.v ("ntone"));
        if (nA > 0.02f && (nt == 5 || nt == 6))
        {
            const int nbr = nt == 5 ? 2 : 3; const float ph = ((float) T.f / 24) * (nt == 5 ? 0.35f : 0.6f);
            for (int j = 0; j < nbr; ++j) { const float hh = sh * (nt == 5 ? 0.16f : 0.1f), y0 = juce::jmax (sy + 2, sy + sh * std::fmod (std::fmod ((float) j / (float) nbr - ph, 1.0f) + 1, 1.0f)), y1 = juce::jmin (sy + sh - 2, y0 + hh); if (y1 > y0) G.rect (sx + 3 * k, y0, sw - 6 * k, y1 - y0, K (ntc, 0.3f * nA)); }
        }
        if (nA > 0.02f && nt == 7)
        {
            for (int j = 0; j < 4; ++j) { const float x = sx + sw * (0.1f + 0.8f * hsh (j, T.tick / 4 + 3)), w = sw * (0.03f + 0.06f * hsh (j, 5)); G.rect (x, sy + 3 * k, w, sh - 6 * k, K (ntc, 0.26f * nA)); }
            const float wy = sy + sh * 0.3f; Poly wl; for (int i = 0; i < 17; ++i) wl.push_back ({ sx + 6 * k + (float) i * (sw - 12 * k) / 16, wy + std::sin ((float) i * 1.3f + (float) T.f * 0.9f) * 5 * k * nA }); G.stroke (wl, 1, K (ntc, 0.7f * nA));
        }
        if (ds > 0.02f)
        {
            const HSL c = retropv::DC[dt]; auto fr = [&] (float e) { return std::vector<Poly> { { { sx, sy }, { sx + sw, sy }, { sx + sw - e, sy + e }, { sx + e, sy + e } }, { { sx, sy + sh }, { sx + sw, sy + sh }, { sx + sw - e, sy + sh - e }, { sx + e, sy + sh - e } }, { { sx, sy }, { sx + e, sy + e }, { sx + e, sy + sh - e }, { sx, sy + sh } }, { { sx + sw, sy }, { sx + sw - e, sy + e }, { sx + sw - e, sy + sh - e }, { sx + sw, sy + sh } } }; };
            G.fillMany (fr ((14 + 26 * ds) * k), K (c, 0.06f + 0.12f * ds)); G.fillMany (fr ((7 + 13 * ds) * k), K (c, 0.08f + 0.16f * ds)); G.fillMany (fr ((2 + 5 * ds) * k), K (mixrgb (c, { 50, 100, 84 }, 0.4f), 0.1f + 0.2f * ds)); if (! mini) glow (G, sx + sw / 2, sy + sh, 2.1f, c, 0.4f * ds, true);
        }
        if (dg > 0.02f)
        {
            const float b = (7 + 9 * dg) * k; std::vector<Poly> sq;
            for (int j = 0; j < juce::roundToInt (4 + 10 * dg); ++j) { const float x = sx + 6 * k + std::floor (hsh (j, T.tick / 3 + 2) * (sw - 12 * k - b) / b) * b, y = sy + 6 * k + std::floor (hsh (j, T.tick / 3 + 9) * (sh - 12 * k - b) / b) * b; sq.push_back ({ { x, y }, { x + b, y }, { x + b, y + b }, { x, y + b } }); }
            G.fillMany (sq, K ({ 146, 90, 60 }, 0.16f + 0.14f * dg));
        }
        // the ghost on the screen
        RigP P = m.P; P.s = Z.gs; P.x = gx; P.y = Z.gy - lvl * 2 * k; P.tail = 0.25f; P.block = dg > 0.02f ? 0.14f + 0.6f * dg : 0.0f; P.seg = dg > 0.02f ? 3 : 5; P.fat = dt == 1 ? 0.22f * ds : 0.0f; P.rough = dt == 4 ? 0.6f * ds : 0.0f; P.rsd = on ? (float) (T.tick >> 1) : 0.0f; const Rig R = ghostRig (P);
        if (sp > 0.02f) { glow (G, R.mid.x, R.mid.y, mini ? 1.0f : 2.0f, retropv::LC[4], 0.5f * sp); for (float d : { -1.0f, 1.0f }) ghostEcho (G, R, retropv::LC[4], 0.3f + 0.4f * sp, juce::jmax (1.0f, 1.3f * k), d * (5 + 12 * sp) * k, -(2 + 4 * sp) * k); }
        GL L; L.slime = HSL { 262, 68, 68 }; L.eyes = ! on ? "closed" : dg > 0.6f ? "x" : m.hp.on && ! m.hp.eyes.empty() ? m.hp.eyes : "open"; L.mouth = ! on ? "flat" : "smile"; L.blush = 0.5f; L.glow = 0; L.lite = mini;
        L.tint = ds > 0.02f ? retropv::DC[dt] : tn < 0 ? HSL { 36, 70, 40 } : HSL { 204, 80, 80 }; L.tintA = ds > 0.02f ? 0.08f + 0.22f * ds : std::abs (tn) * 0.18f;
        if (neon) { L.tube = mixrgb ({ 262, 100, 72 }, ds > 0.02f ? retropv::DC[dt] : HSL { 262, 100, 72 }, ds * 0.6f); L.slime = HSL { 262, 100, 68 }; L.tint.reset(); L.k = 0.9f + 0.1f * hsh (T.tick, 5); L.glass = 0.3f; }
        ghost (G, R, L, T);
        // NOISE over the picture: the shape of the specks is the type, their brightness the NOISE TONE
        if (nA > 0.02f && nt < 5)
        {
            static const float ns[5] { 10, 30, 90, 70, 16 }; const int n = juce::roundToInt (ns[nt] * nA * (mini ? 0.5f : 1.0f)); const int salts[5] { T.tick >> 1, T.tick >> 3, T.tick, T.tick, T.tick >> 1 }; const int salt = salts[nt];
            auto px = [&] (int i) { return sx + 5 * k + hsh (i, salt + 1) * (sw - 10 * k); }; auto py = [&] (int i) { return sy + 5 * k + hsh (i, salt + 7) * (sh - 10 * k); }; std::vector<Poly> spk;
            if (nt == 0)
            {
                std::vector<Poly> arcs; for (int i = 0; i < juce::roundToInt (2 + 5 * nA); ++i) { const float cy_ = sy + sh * hsh (i, salt + 3), a0 = -0.5f + hsh (i, salt + 5); arcs.push_back (arcp (sx + sw / 2, cy_ + sw * 1.4f, sw * 1.4f, sw * 1.4f, -PI / 2 + a0 * 0.3f - 0.12f, -PI / 2 + a0 * 0.3f + 0.12f, 5)); }
                G.strokeMany (arcs, 0.8f, K (ntc, 0.7f * nA + 0.2f)); for (int i = 0; i < n; ++i) { const float rr = (1 + 1.4f * hsh (i, 3)) * k; spk.push_back (circ (px (i), py (i), rr, rr, 5)); }
            }
            else if (nt == 1) for (int i = 0; i < n; ++i) { const float x = px (i), y = py (i) + std::fmod ((float) T.f * 0.15f * (1 + hsh (i, 2)), 8.0f) * k, r = (1 + 1.6f * hsh (i, 3)) * k; spk.push_back ({ { x - r, y }, { x + r * 0.6f, y - r * 0.8f }, { x + r, y + r * 0.5f } }); }
            else if (nt == 4) for (int i = 0; i < n; ++i) spk.push_back (circ (px (i), py (i), (3 + 4 * hsh (i, 3)) * k, (2 + 3 * hsh (i, 5)) * k, 7));
            else for (int i = 0; i < n; ++i) { const float x = px (i), y = py (i), w = nt == 3 ? (1.4f + 2.4f * hsh (i, 3)) * k : 0.9f * k; spk.push_back ({ { x, y }, { x + w, y }, { x + w, y + 0.9f * k + 0.3f }, { x, y + 0.9f * k + 0.3f } }); }
            G.fillMany (spk, K (ntc, (nt == 4 ? 0.22f : nt == 2 ? 0.5f : 0.75f) * juce::jmin (1.0f, nA + 0.25f)));
        }
        if (mgn > 0.03f && on) for (int j = 0; j < juce::roundToInt (1 + 3 * mgn); ++j) { const float y = sy + 8 * k + hsh (j, T.tick / 3 + 11) * (sh - 16 * k), w = sw * (0.3f + 0.6f * hsh (j, T.tick / 3 + 13)), x = sx + 4 * k + hsh (j, T.tick / 3 + 17) * (sw - 8 * k - w); G.rect (x, y, w, (1.5f + 2.5f * mgn) * k, K (j % 2 ? sc : HSL { 50, 40, 92 }, j % 2 ? 0.9f : 0.5f)); }
        for (int j = 0; j < juce::roundToInt (deg * 5); ++j) { const float x = sx + sw * (0.14f + 0.72f * hsh (j, 41)), y = sy + sh * (0.14f + 0.72f * hsh (j, 43)), r = (4 + 7 * hsh (j, 45)) * (0.5f + 0.5f * deg) * k; G.fill (blobp (x, y, r + 1.6f * k, r * 0.8f + 1.6f * k, 0.3f, 9, (float) (j * 3 + 1)), K ({ 24, 90, 48 }, 0.85f)); G.fill (blobp (x, y, r, r * 0.8f, 0.3f, 9, (float) (j * 3 + 1)), K (L3<HSL> (look, { 20, 40, 6 }, { 262, 40, 6 }, { 250, 60, 3 }))); }
        if (std::abs (tn) > 0.04f) G.fill (scr, K (tn < 0 ? HSL { 30, 70, 12 } : HSL { 200, 80, 84 }, std::abs (tn) * (tn < 0 ? 0.34f : 0.14f)));
        G.fill ({ { sx + sw * 0.08f, sy + sh * 0.06f }, { sx + sw * 0.3f, sy + sh * 0.06f }, { sx + sw * 0.2f, sy + sh * 0.26f }, { sx + sw * 0.05f, sy + sh * 0.3f } }, K ({ 200, 40, 96 }, 0.07f));
        if (! neon && ! mini) { OutlineOpt oo; oo.gap = 0; G.fillMany (outlineRibbons (scr, 1.2f, bs + 9, oo), K (dk, 0.8f)); }
    }
    void scene (G& G, const Feed& F, State& S, const Clock& T) override
    {
        stageUnder (G); G.rect (0, 0, 280, 300, K (L3<HSL> (G.look, { 200, 60, 7 }, { 262, 50, 8 }, { 250, 60, 4 }), L3 (G.look, 0.3f, 0.26f, 0.12f))); G.ell (140, 290, 128, 8, K (L3<HSL> (G.look, { 200, 60, 6 }, { 262, 50, 7 }, { 250, 60, 3 }), 0.5f));
        machine (G, F, S, T, retropv::STAGE, false); if (! F.on) offVeil (G); stageOver (G);
    }
    // DAWN: the same machine, small, in its own opaque pane between the OUTPUT dial and the menus (576,466,94,164; 1 su = 1 px)
    bool hasDawn() const override { return true; }
    bool dawnUnder() const override { return false; }
    juce::Rectangle<float> dawnRect() const override { return { 576, 466, 94, 164 }; }
    void dawnScene (G& G, const Feed& F, State& S, const Clock& T, float W, float H) override
    {
        G.fill (rbox (0, 0, W, H, 7, 3), K ({ 196, 40, 10 })); machine (G, F, S, T, retropv::MINI, true); if (! F.on) G.fill (rbox (0, 0, W, H, 7, 3), rgba (10, 8, 24, 0.45f));
    }
};
} // namespace phantom::pv
