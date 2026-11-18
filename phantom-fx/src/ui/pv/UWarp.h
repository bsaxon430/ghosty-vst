// SPDX-License-Identifier: AGPL-3.0-or-later
// WARP (port of phantom-anime/visuals/src/u_warp.js): the drawn bend lane is a ribbon stretched across the stage, "now" over the
// middle, where the ghost stands with the top of its head caught in it: the ribbon up pulls it tall and thin like taffy, down presses
// it squat. TAPE mode: the ribbon is tape between two reels and the beat marks on the floor bunch up and spread with the speed.
#pragma once
#include "UHaunt.h"

namespace phantom::pv
{
namespace warppv
{
    struct Pt { float x, y, ten = 0; bool step = false; };
    // the plugin's own lane maths (BendLane.h: ease, valueAt), for the stand-in shape (UP & BACK)
    inline float ease (float t, float ten, bool rising) { if (std::abs (ten) < 1e-4f) return t; const float e = std::pow (2.0f, 3 * std::abs (ten)); return (ten > 0) == rising ? 1 - std::pow (1 - t, e) : std::pow (t, e); }
    inline float bendAt (const std::vector<Pt>& pts, float ph)
    {
        const int n = (int) pts.size(); if (n == 0) return 0; if (n == 1) return pts[0].y; ph -= std::floor (ph); int i = 0;
        if (ph < pts[0].x) i = n - 1; else while (i + 1 < n && pts[(size_t) i + 1].x <= ph) ++i;
        const auto& a = pts[(size_t) i]; const bool wrap = i == n - 1; const auto& b = pts[wrap ? 0 : (size_t) i + 1]; const float ax = a.x, bx = b.x + (wrap ? 1.0f : 0.0f); float x = ph; if (wrap && x < ax) x += 1;
        if (a.step) return a.y; const float span = bx - ax; if (span <= 1e-7f) return b.y; return a.y + (b.y - a.y) * ease (clampf ((x - ax) / span, 0, 1), a.ten, b.y > a.y);
    }
    inline const std::vector<Pt>& shape0() { static const std::vector<Pt> s { { 0, 0, 0.4f }, { 0.0625f, 1 }, { 0.1875f, 1, 0.4f }, { 0.25f, 0 } }; return s; }
}

class WarpScene final : public Scene
{
public:
    std::vector<std::string> controls() const override { return { "mode", "amount", "length", "range", "formant", "mix" }; }
    std::vector<float> defaults() const override { return { 0, 1, 1, 2, 0, 1 }; }
    static float lane (const Feed& F, float ph) { const auto& l = F.lane; if (! l.empty()) { const int n = (int) l.size(); return l[(size_t) (((int) std::floor ((std::fmod (std::fmod (ph, 1.0f) + 1, 1.0f)) * (float) n)) % n)]; } return warppv::bendAt (warppv::shape0(), ph); }
    struct Now { bool on; float bars; int rg; float st, Fk, pos, val; bool tape; };
    static Now now (const Feed& F, const Clock& T)
    {
        Now n; n.on = F.on; static const float bs[4] { 0.5f, 1, 2, 4 }, sts[4] { 2, 7, 12, 24 }, fks[4] { 1.15f, 1.4f, 1.8f, 2.4f };
        n.bars = bs[juce::jlimit (0, 3, juce::roundToInt (F.v ("length")))]; n.rg = juce::jlimit (0, 3, juce::roundToInt (F.v ("range"))); n.st = sts[n.rg]; n.Fk = fks[n.rg];
        n.pos = F.hasPos() && F.pos >= 0 ? F.pos : (float) std::fmod ((T.t * 120 / 60) / (4 * n.bars), 1.0);
        n.val = n.on ? clampf (F.semis > -98 ? F.semis / n.st : lane (F, n.pos) * F.v ("amount"), -1, 1) : 0.0f; n.tape = F.v ("mode") > 0.5f; return n;
    }
    void scene (G& G, const Feed& F, State& S, const Clock& T) override
    {
        const Look look = G.look; const auto N = now (F, T); const bool on = N.on, tape = N.tape; const float bars = N.bars, st = N.st, Fk = N.Fk, pos = N.pos, val = N.val; const int rg = N.rg;
        const float mixk = mixA (F.v ("mix")), amount = F.v ("amount"); const auto m = gmo (F, S, T); const float lvl = m.lvl;
        const HSL acc { 79, 100, 65 }, dk = L3<HSL> (look, { 262, 30, 14 }, INKC, { 262, 60, 30 }); const float bs = 3 + (float) boilOf (T, look == Look::dawn ? 4 : 3) * 1.7f, XL = 18, XR = 262, SP = XR - XL, gy = 262, s = 33;
        auto yOf = [&] (float q) { return gy - 2 * s * std::pow (Fk, q) - 3; }; auto lv = [&] (float ph) { return lane (F, ph) * amount; };
        const HSL RC = tape ? L3<HSL> (look, { 26, 50, 34 }, { 24, 46, 30 }, { 28, 100, 58 }) : L3<HSL> (look, { 70, 80, 62 }, { 76, 84, 58 }, acc);
        stageUnder (G);
        G.rect (0, 0, 280, 300, K (L3<HSL> (look, { 200, 60, 7 }, { 262, 50, 8 }, { 250, 60, 4 }), L3 (look, 0.36f, 0.34f, 0.16f) * mixk));
        // RANGE: three thin lines mark the lane's top, middle and bottom; dots = which range
        float gl[3]; { int j = 0; for (float q : { 1.0f, 0.0f, -1.0f }) gl[j++] = yOf (q * mixk); }
        { std::vector<Poly> ds; for (float y : gl) for (int i = 0; i < 12; ++i) ds.push_back ({ { XL + 10 + (float) i * 20, y }, { XL + 18 + (float) i * 20, y } }); G.strokeMany (ds, 0.9f, K (look == Look::neon ? acc : L3<HSL> (look, { 60, 50, 84 }, { 50, 40, 84 }, {}), 0.3f)); }
        { std::vector<Poly> dots; for (int i = 0; i <= rg; ++i) dots.push_back (circ (XR - 12 - (float) i * 7, gl[0] - 6, 1.7f, 1.7f, 6)); G.fillMany (dots, K (look == Look::neon ? acc : L3<HSL> (look, { 60, 60, 86 }, { 50, 50, 86 }, {}), 0.8f)); }
        // LENGTH: a chequered beat rule on the floor that runs with the ribbon; TAPE: the beats sit where the tape's own time puts them
        const int nb = juce::roundToInt (4 * bars); std::array<float, 65> cum {}; for (int j = 1; j <= 64; ++j) cum[(size_t) j] = cum[(size_t) j - 1] + (tape ? std::pow (2.0f, lv (((float) j - 0.5f) / 64) * st / 12) : 1.0f);
        auto phOf = [&] (float c) { const float t = c * cum[64]; int j = 1; while (j < 64 && cum[(size_t) j] < t) ++j; return ((float) (j - 1) + (t - cum[(size_t) j - 1]) / juce::jmax (1e-6f, cum[(size_t) j] - cum[(size_t) j - 1])) / 64; };
        auto xOf = [&] (float ph) { return 140 + (std::fmod (std::fmod (ph - pos + 0.5f, 1.0f) + 1, 1.0f) - 0.5f) * SP; };
        std::vector<Poly> tk, chk;
        for (int i = 0; i < nb; ++i)
        {
            const float x = xOf (phOf ((float) i / (float) nb)), x1 = xOf (phOf ((float) (i + 1) / (float) nb - 1e-4f)); tk.push_back ({ { x, gy + 7 }, { x, gy + (i % 4 == 0 ? 21.0f : 15.0f) } });
            if (i % 2 == 0) { if (x1 > x) chk.push_back ({ { x, gy + 11.5f }, { x1, gy + 11.5f } }); else { chk.push_back ({ { x, gy + 11.5f }, { XR, gy + 11.5f } }); chk.push_back ({ { XL, gy + 11.5f }, { x1, gy + 11.5f } }); } }
        }
        { StrokeOpt bt; bt.roundCap = false; G.stroke ({ { XL, gy + 11.5f }, { XR, gy + 11.5f } }, 5.4f, K (look == Look::neon ? HSL { 262, 60, 24 } : L3<HSL> (look, { 200, 40, 14 }, { 262, 34, 16 }, {}), 0.8f), bt);
          G.strokeMany (chk, 4.2f, K (look == Look::neon ? acc : L3<HSL> (look, { 60, 60, 80 }, { 50, 50, 82 }, {}), look == Look::neon ? 0.5f : 0.7f), false); }
        G.strokeMany (tk, 1.3f, K (look == Look::neon ? acc : L3<HSL> (look, { 60, 60, 88 }, { 50, 50, 88 }, {}), 0.9f));
        glow (G, 140, gy + 2, 2.1f, acc, (0.14f + 0.3f * lvl) * mixk, true);
        // the posts (PITCH) or the reels on them (TAPE)
        if (tape && S.dv.find ("wA") == S.dv.end()) S.dv["wA"] = 0; if (tape && on) S.dv["wA"] += 0.16 * std::pow (2.0, (double) (val * st / 12));
        for (float sd : { -1.0f, 1.0f })
        {
            const float px = sd < 0 ? XL - 6 : XR + 6; const auto post = rbox (px - 4, tape ? 64.0f : 30.0f, 8, tape ? gy + 22 - 64 : gy + 22 - 30, 2, 2);
            if (look == Look::neon) { TubeOpt to; to.halo = 0; to.k = 0.7f; tubeLine (G, { { px, tape ? 64.0f : 30.0f }, { px, gy + 22 } }, 1.2f, { 262, 90, 66 }, to); }
            else { SolidOpt so; so.iw = 1.5f; so.sd = bs + sd; so.shw = 2.2f; solid (G, wob (post, 0.5f, 0.05f, bs + sd), MAT (look == Look::dawn ? "wood" : "steel"), so); }
            if (tape)
            {
                const float rx = sd < 0 ? XL + 18 : XR - 18, ry = 46, rr = 24, a0 = (float) (S.dv.count ("wA") ? S.dv["wA"] : 0.0) * (sd < 0 ? 1.0f : 1.13f); const Poly rim = circ (rx, ry, rr, rr, 18);
                if (look == Look::neon) { G.fill (rim, K (pal::neonGlass, 0.8f)); TubeOpt to; to.closed = true; to.halo = 0; tubeLine (G, rim, 1.2f, { 28, 100, 58 }, to); to.k = 0.6f; tubeLine (G, circ (rx, ry, rr * 0.55f, rr * 0.55f, 12), 0.8f, { 28, 100, 58 }, to); }
                else { SolidOpt so; so.iw = 1.5f; so.sd = bs + sd + 4; so.shw = 3; solid (G, wob (rim, 0.4f, 0.05f, bs + sd + 4), MAT (look == Look::dawn ? "brass" : "steel"), so); G.ell (rx, ry, rr * 0.62f, rr * 0.62f, K (L3<HSL> (look, { 26, 44, 26 }, { 24, 40, 24 }, {}))); }
                std::vector<Poly> sp; for (int i = 0; i < 3; ++i) { const float a = a0 + (float) i * TAU / 3; sp.push_back ({ { rx + std::cos (a) * 5, ry + std::sin (a) * 5 }, { rx + std::cos (a) * (rr - 3), ry + std::sin (a) * (rr - 3) } }); }
                G.strokeMany (sp, look == Look::neon ? 1.0f : 2.0f, K (look == Look::neon ? HSL { 28, 100, 70 } : L3<HSL> (look, { 46, 70, 70 }, { 214, 16, 74 }, {}))); G.ell (rx, ry, 3.4f, 3.4f, K (look == Look::neon ? HSL { 28, 100, 70 } : dk));
            }
        }
        // the ribbon: the lane itself, one cycle across the stage, "now" in the middle
        Poly rib; for (float x = XL; x <= XR + 0.01f; x += 4) rib.push_back ({ x, yOf (lv (pos + (x - 140) / SP) * mixk) });
        if (tape) { rib.insert (rib.begin(), { XL + 18 + 17, 62 }); rib.push_back ({ XR - 18 - 17, 62 }); }
        const float ra = on ? 1.0f : 0.6f;
        if (look == Look::neon) { TubeOpt to; to.halo = 1; to.a = ra; to.k = 0.75f + 0.25f * lvl; tubeLine (G, rib, tape ? 2.0f : 1.5f, RC, to); }
        else
        {
            G.stroke (rib, tape ? 6.6f : 7.4f, K (dk, 0.9f * ra)); G.stroke (rib, tape ? 4.6f : 5.2f, K (RC, ra));
            if (! tape) { StrokeOpt so; so.dash = { 4.4f, 6.6f }; so.roundCap = false; G.stroke (rib, 5.2f, K (L3<HSL> (look, { 56, 90, 90 }, { 60, 90, 92 }, {}), 0.75f * ra), so); }
            Poly hi; for (auto q : rib) hi.push_back ({ q.x, q.y - (tape ? 1.3f : 1.7f) }); G.stroke (hi, 1, K (L3<HSL> (look, { 60, 90, 90 }, { 70, 90, 90 }, {}), 0.8f * ra));
        }
        if (on && look != Look::neon) glow (G, 140, yOf (val * mixk), 1, acc, (0.3f + 0.3f * lvl) * mixk);
        if (! tape) for (const V2 q : { rib.front(), rib.back() }) { if (look == Look::neon) { TubeOpt to; to.closed = true; to.halo = 0; tubeLine (G, circ (q.x, q.y, 3.4f, 3.4f, 8), 0.9f, acc, to); } else { G.ell (q.x, q.y, 4.4f, 4.4f, K (dk)); G.ell (q.x, q.y, 2.8f, 2.8f, K (L3<HSL> (look, { 46, 80, 70 }, { 46, 84, 66 }, {}))); } }
        // the ghost: caught by the top of its head. Height x Fk^bend, width the other way. KEEP FORMANTS: the face keeps its size
        const float sy = std::pow (Fk, val * mixk), sx = std::pow (Fk, -val * mixk * 0.45f); RigP P = m.P; P.s = s; P.x = 140; P.y = gy; P.tail = 0.22f; P.sx *= sx; P.sy *= sy; P.lift *= 0.5f;
        const Rig R = ghostRig (P); const float ty = R.top.y, up = val * mixk;
        glow (G, R.mid.x, R.mid.y, clampf (1.5f * juce::jmax (sx, sy), 1, 2.6f), acc, (0.16f + 0.3f * lvl) * mixk * (on ? 1.0f : 0.4f));
        GL L; L.slime = HSL { 188, 78, 62 }; L.eyes = ! on ? "closed" : up > 0.55f ? "wide" : up < -0.45f ? "squeeze" : m.hp.on && ! m.hp.eyes.empty() ? m.hp.eyes : "open"; L.mouth = ! on ? "flat" : up > 0.55f ? "o" : up < -0.45f ? "flat" : "smile"; L.blush = 0.5f; L.glow = 0;
        // KEEP FORMANTS: the face of an unstretched rig, laid so that its middle sits where the stretched face's middle is
        if (F.v ("formant") > 0.5f) L.faceR = keptFace (R, s, sx);
        if (look == Look::neon) { L.tube = acc; L.slime = HSL { 188, 100, 60 }; L.k = 0.9f + 0.1f * hsh (T.tick, 5); }
        const float ry_ = yOf (up) + 1;
        if (look == Look::neon) { TubeOpt to; to.halo = 0; tubeLine (G, { { 136, ty + 4 }, { 140, ry_ }, { 144, ty + 4 } }, 1, acc, to); } else G.fill ({ { 134.5f, ty + 5 }, { 140, ry_ - 1 }, { 145.5f, ty + 5 } }, K (look == Look::dawn ? pal::dawnBody : pal::celBody));
        G.ell (140, gy + 6, R.halfW * 1.08f, 3.6f, K (L3<HSL> (look, { 200, 60, 6 }, { 262, 50, 7 }, { 250, 60, 3 }), 0.34f));
        ghost (G, R, L, T);
        if (on)
        {
            const float nx = 140 - R.halfW - 20, ny = clampf (ry_ + 12, 30, gy - 8); if (look != Look::neon) noteMark (G, nx + 0.9f, ny + 0.9f, 9, dk, 0.5f); noteMark (G, nx, ny, 9, look == Look::neon ? HSL { 50, 100, 78 } : L3<HSL> (look, { 56, 96, 80 }, { 52, 96, 76 }, {}), 0.95f);
            for (int i = 0; i < 3; ++i) { const int j = 4 + (int) std::floor (hsh (i, T.tick / 3 + 3) * (float) (rib.size() - 10)); const V2 a = rib[(size_t) j], b = rib[(size_t) j + 1]; if (std::abs (b.y - a.y) > 5) glint (G, a.x, a.y, 0.5f, { 60, 90, 92 }, 0.85f); }
        }
        if (L.faceR) { const auto& Rf = *L.faceR; const V2 fc = Rf.X (Rf.mouth.x, (Rf.eyes[0].y + Rf.mouth.y) / 2); StrokeOpt so; so.closed = true; so.dash = { 2.4f, 2.6f }; G.stroke (circ (fc.x, fc.y, s * 0.5f * Rf.sx, s * 0.4f, 14), 1.3f, K (look == Look::neon ? HSL { 50, 100, 80 } : HSL { 46, 90, 70 }, 0.9f), so); }
        if (std::abs (up) > 0.2f)
        {
            const float a = juce::jmin (1.0f, (std::abs (up) - 0.2f) * 1.6f); const auto c = look == Look::neon ? K (acc, 0.6f * a) : K (L3<HSL> (look, { 262, 24, 60 }, { 255, 28, 70 }, {}), 0.6f * a);
            if (up > 0) { std::vector<Poly> ls; for (float q : { -0.42f, 0.0f, 0.4f }) { const float x = R.mid.x + q * R.halfW; ls.push_back ({ { x, ty + 16 + std::abs (q) * 18 }, { x - 1.5f, lerp (ty, gy, 0.45f) }, { x, gy - 22 } }); } G.strokeMany (ls, 1.1f, c); }
            else G.strokeMany ({ R.across (0.5f, 0.04f, 10, 0.12f, 0.88f), R.across (0.68f, 0.04f, 10, 0.12f, 0.88f) }, 1.1f, c);
        }
        if (look == Look::neon) { TubeOpt to; to.closed = true; to.halo = 1; tubeLine (G, circ (140, ry_, 3, 3, 8), 0.9f, { 50, 100, 80 }, to); }
        else { G.ell (140, ry_, 4.2f, 4.2f, K (dk)); G.ell (140, ry_, 2.7f, 2.7f, K ({ 50, 96, 74 })); glow (G, 140, ry_, 0.5f, { 50, 100, 80 }, 0.6f + 0.4f * lvl); }
        if (! on) offVeil (G);
        stageOver (G);
    }
    // DAWN: behind the bend-lane canvas (22,358,632,244; 1 su = 1 px)
    bool hasDawn() const override { return true; }
    juce::Rectangle<float> dawnRect() const override { return { 22, 358, 632, 244 }; }
    std::vector<std::string> dawnControls() const override { return { "amount", "length", "range", "formant", "mix" }; }
    float laneY0 = 127, laneYa = 97;   // the editor's own mapping (design px in the picture): the lane's 0 and how far its range's end is
    void dawnScene (G& G, const Feed& F, State& S, const Clock& T, float W, float H) override
    {
        const auto N = now (F, T); const bool on = N.on; const float bars = N.bars, Fk = N.Fk, pos = N.pos, val = N.val, mixk = mixA (F.v ("mix")), amount = F.v ("amount"); const auto m = gmo (F, S, T); const float lvl = m.lvl;
        const HSL acc { 79, 100, 65 }; const float y0 = laneY0, ya = laneYa; auto lv = [&] (float ph) { return lane (F, ph) * amount; };
        G.rect (0, 0, W, H, K ({ 204, 50, 9 }, 0.22f * mixk));
        if (on)
        {
            const float xp = pos * W; glow (G, xp, y0 - val * ya, 2.6f, acc, (0.16f + 0.16f * lvl) * mixk); StrokeOpt bt; bt.roundCap = false; G.stroke ({ { xp, 4 }, { xp, H - 4 } }, 6, K (acc, 0.07f * mixk), bt);
            const int n = 14; const float dph = 0.012f / bars;
            for (int j = 0; j < n; ++j) { const float ph = pos - (float) j * dph; if (ph < 0) break; const float x = ph * W, q = lv (ph); if (std::abs (q) < 0.03f) continue; G.stroke ({ { x, y0 }, { x, y0 - q * ya } }, 2.2f, K (q > 0 ? HSL { 70, 90, 70 } : HSL { 188, 70, 66 }, 0.3f * (1 - (float) j / (float) n) * mixk), bt); }
            for (int i = 0; i < 8; ++i) { const float c = std::fmod (((float) T.f / 24) * (0.1f + 0.1f * hsh (i, 5)) + hsh (i, 7), 1.0f); G.ell (xp + (hsh (i, 9) - 0.5f) * 40, y0 - val * ya * c + (hsh (i, 3) - 0.5f) * 14, 1.2f, 1.2f, K ({ 70, 90, 84 }, 0.5f * std::sin (c * PI) * mixk)); }
        }
        const float s = 20, gy = H - 12, gx = W - 36, sy = std::pow (Fk, val * mixk), sx = std::pow (Fk, -val * mixk * 0.45f); RigP P = m.P; P.s = s; P.x = gx; P.y = gy; P.tail = 0.22f; P.sx *= sx; P.sy *= sy; P.lift = 0; const Rig R = ghostRig (P);
        glow (G, gx, gy - s, 1, acc, (0.14f + 0.2f * lvl) * mixk); G.stroke ({ { gx, R.top.y + 3 }, { gx, R.top.y - 10 } }, 1.2f, K ({ 70, 80, 66 }, 0.5f)); G.ell (gx, R.top.y - 11, 2.4f, 2.4f, K ({ 50, 96, 74 }, 0.7f));
        GL L; L.slime = HSL { 188, 78, 62 }; L.eyes = ! on ? "closed" : val > 0.55f ? "wide" : val < -0.45f ? "squeeze" : "open"; L.mouth = val > 0.55f ? "o" : "smile"; L.blush = 0.4f; L.glow = 0; L.lite = true; L.a = 0.7f;
        if (F.v ("formant") > 0.5f) L.faceR = keptFace (R, s, sx);
        ghost (G, R, L, T);
    }
    // KEEP FORMANTS: an unstretched rig (width up to 1.15 x the stretched one's, never wider than 1) whose face's middle (between the
    // eyes and the mouth) lands on the stretched face's middle
    static std::shared_ptr<Rig> keptFace (const Rig& R, float s, float sx)
    {
        const float fm = (R.eyes[0].y + R.mouth.y) / 2, kx = juce::jmin (1.0f, sx * 1.15f); const V2 fc = R.X (R.mouth.x, fm);
        RigP fp; fp.s = s; fp.x = fc.x - R.mouth.x * s * kx; fp.y = fc.y - fm * s; fp.tail = 0.22f; fp.sx = kx; return std::make_shared<Rig> (ghostRig (fp));
    }
};
} // namespace phantom::pv
