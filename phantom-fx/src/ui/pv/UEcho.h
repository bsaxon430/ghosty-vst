// SPDX-License-Identifier: AGPL-3.0-or-later
// ECHO (port of phantom-anime/visuals/src/u_echo.js): the ghost calls; its copies stand down a path to the upper right, one per repeat.
#pragma once
#include "Hands.h"

namespace phantom::pv
{
class EchoScene final : public Scene
{
public:
    std::vector<std::string> controls() const override { return { "mode", "sync", "time", "div", "fb", "ping", "width", "lowcut", "highcut", "char", "duck", "nodes", "spread", "sat", "pan", "mix" }; }
    std::vector<float> defaults() const override { return { 0, 1, 0.486f, 7, 0.45f, 1, 1, 0.25f, 0.6f, 0.5f, 0, 1, 0, 0, 0, 1 }; }
    static constexpr float AX = 108, AY = 244, BX = 258, BY = 172;
    struct Cp { int k; float al, x, y, sc, q, side, cut, dull; bool onBeat; };
    struct At { float q, sc, x, y; };
    struct Plan { bool sync; int di; float beats, tv, g, fb; bool ping; int mode, nodes; float ms; bool run; std::vector<Cp> C;
                  At at (float tau) const { const float q = 1 - std::exp (-tau * g * 0.214f), sc = 1 - 0.7f * std::pow (q, 0.9f); return { q, sc, lerp (AX, BX, q), lerp (AY, BY, q) }; } };
    struct V { float mode, sync, time, div, fb, ping, width, lowcut, highcut, chr, duck, nodes, spread, sat, pan, mix; };
    static Plan plan (const V& v)
    {
        Plan p; p.sync = v.sync > 0.5f; p.di = juce::jlimit (0, 15, juce::roundToInt (v.div)); p.beats = DIVB[p.di]; p.tv = p.sync ? clampf (std::log2 (p.beats * 16) / 7, 0, 1) : v.time;
        p.g = 0.25f + 1.1f * p.tv; p.fb = v.fb; p.ping = v.ping > 0.5f; p.mode = juce::roundToInt (v.mode); p.nodes = juce::jlimit (1, 8, juce::roundToInt (v.nodes));
        p.ms = p.sync ? p.beats * 60000 / 120 : 1 + 1999 * std::pow (v.time, 2.325f); p.run = p.fb > 0.95f;
        for (int k = 1; k <= (p.run ? 9 : 10); ++k)
        {
            const float al = p.run ? juce::jmax (0.42f, std::pow (0.9f, (float) k)) : std::pow (0.22f + 0.76f * p.fb, (float) k); if (al < 0.05f) break;
            const auto a = p.at ((float) k); const float side = p.ping ? (k % 2 ? -1.0f : 1.0f) : 0.0f, W = (10 + 46 * v.width) * a.sc * 0.5f, cut = juce::jmin (0.6f, v.lowcut * (float) k * 0.12f), dull = (1 - v.highcut) * juce::jmin (1.0f, (float) k / 3);
            p.C.push_back ({ k, al, a.x + side * W * 0.42f, a.y + side * W * 0.9f * 0.42f - cut * 8 * a.sc, a.sc, a.q, side, cut, dull, p.sync && std::abs ((float) k * p.beats - std::round ((float) k * p.beats)) < 0.01f });
        }
        return p;
    }
    // one copy, drawn once per knob state (and line boil) into its own small picture
    static G::Layer copy (G& G, const Cp& cp, const Plan& pl, const V& v, const Clock& T, int mini, const juce::String& vkey)
    {
        const Look look = G.look; const float s = (mini ? 14.0f : 34.0f) * cp.sc, chr = v.chr, dull = cp.dull, pad = s * 1.6f, w = s * 2.9f + pad, h = s * 3.2f + pad; const int mode = pl.mode;
        const juce::String key = "echo-c" + juce::String (cp.k) + "m" + juce::String (mini) + "|" + vkey + "|" + juce::String (boilOf (T, look == Look::dawn ? 4 : 3));
        return G.layer (key, w, h, w * 0.5f, h - pad * 0.5f, [&] (pv::G& g)
        {
            RigP P; P.s = s; P.x = 0; P.y = 0; P.tail = 0.2f; P.sd = (float) (cp.k * 3 + mini); P.fat = mode == 1 ? juce::jmin (0.35f, 0.035f * (float) cp.k * (0.4f + chr)) : 0.0f;
            P.block = mode == 2 ? clampf (0.14f + 0.07f * (float) cp.k * (0.5f + chr), 0, 0.9f) : 0.0f; P.seg = mode == 2 ? 3 : 5; const Rig R = ghostRig (P);
            GL L; L.slime = look == Look::neon ? HSL { 140, 100, 56 } : HSL { 92, 72, 52 }; L.eyes = dull > 0.5f ? "closed" : "open"; L.mouth = dull > 0.5f ? "flat" : "smile"; L.blush = dull > 0.5f ? 0.0f : 0.5f;
            L.glow = 0; L.gloss = 0; L.simple = mini != 0; L.tex = mini ? 0.0f : 0.5f;
            const float warm = mode == 1 ? clampf (0.09f * (float) cp.k * (0.4f + chr), 0, 0.5f) : 0.0f; const HSL grey { 262, 18, 60 };
            if (look == Look::neon) { L.tube = mixrgb (mixrgb ({ 140, 100, 58 }, { 24, 100, 54 }, warm * 1.4f), { 250, 30, 46 }, dull * 0.8f); L.k = 1 - 0.45f * dull; L.glow = 0; L.face = HSL { 100, 80, 86 }; }
            else
            {
                HSL body = mixrgb (look == Look::dawn ? pal::dawnBody : pal::celBody, mode == 3 ? HSL { 190, 70, 86 } : HSL { 110, 60, 84 }, 0.34f), shd = look == Look::dawn ? HSL { 150, 30, 62 } : HSL { 160, 30, 66 }, ik = look == Look::dawn ? HSL { 150, 40, 26 } : HSL { 150, 44, 24 };
                body = mixrgb (mixrgb (body, { 24, 70, 62 }, warm), grey, dull * 0.75f); shd = mixrgb (mixrgb (shd, { 14, 60, 40 }, warm), { 262, 20, 40 }, dull * 0.75f); ik = mixrgb (mixrgb (ik, { 14, 60, 22 }, warm), { 262, 22, 26 }, dull * 0.7f);
                if (look == Look::dawn) { const float m = cp.q * 0.45f; body = mixrgb (body, { 60, 44, 84 }, m); shd = mixrgb (shd, { 150, 30, 60 }, m); L.rim = mixrgb ({ 48, 100, 88 }, { 60, 44, 84 }, dull); }
                L.body = body; L.shade = shd; L.ink = ik; L.faceInk = ik; L.slime = mixrgb (mixrgb ({ 92, 72, 52 }, { 24, 80, 46 }, warm), grey, dull * 0.7f); L.pupil = mixrgb ({ 262, 60, 62 }, grey, dull);
            }
            const auto bb = bbox (R.body); const float top = bb[1] - 2, cutY = lerp (bb[3] + s * 0.6f, top, cp.cut);
            if (cp.cut > 0.02f)
            {
                g.clip ({ { -w, top - 4 }, { w, top - 4 }, { w, cutY }, { -w, cutY } }, [&] { ghost (g, R, L, T); });
                for (int i = 0; i < 7; ++i) { const float px = lerp (bb[0], bb[2], ((float) i + 0.5f) / 7), py = cutY + 2 + hsh (i, cp.k) * s * 0.35f, r = s * (0.05f + 0.04f * hsh (i, cp.k + 3)); g.ell (px, py, r, r, K (look == Look::neon ? *L.tube : HSL { 60, 30, 92 }, 0.8f)); }
            }
            else ghost (g, R, L, T);
            if (v.sat > 0.02f) { StrokeOpt so; so.closed = true; g.stroke (R.body, juce::jmax (0.8f, s * 0.05f) * (1 + v.sat), K ({ 22, 100, 58 }, 0.35f + 0.6f * v.sat), so); }
            if (! mini && pl.nodes > 1 && v.spread < 0.04f && cp.k <= 3)
                for (int j = 1; j < juce::jmin (pl.nodes, 5); ++j)
                { const float kx = 1 + 0.07f * (float) j; StrokeOpt so; so.closed = true; g.stroke (xf (R.body, [&] (float x, float y) { return V2 { x * kx, (y + s) * kx - s }; }), 0.8f, K (look == Look::neon ? *L.tube : HSL { 60, 30, 92 }, 0.5f / (float) j), so); }
        });
    }
    void scene (G& G, const Feed& F, State& S, const Clock& T) override
    {
        const V v { F.v ("mode"), F.v ("sync"), F.v ("time"), F.v ("div"), F.v ("fb"), F.v ("ping"), F.v ("width"), F.v ("lowcut"), F.v ("highcut"), F.v ("char"), F.v ("duck"), F.v ("nodes"), F.v ("spread"), F.v ("sat"), F.v ("pan"), F.v ("mix") };
        const Look look = G.look; const bool on = F.on; const auto pl = plan (v); const float lvl = on ? F.level : 0, mix = mixA (v.mix), chr = v.chr, Ts = pl.ms / 1000; const int k = on ? T.tick - S.hitTick : -1, mode = pl.mode;
        const HSL acc = look == Look::neon ? HSL { 140, 100, 58 } : look == Look::dawn ? HSL { 84, 70, 72 } : HSL { 110, 60, 70 };
        juce::String vkey; for (float x : { v.mode, v.sync, v.time, v.div, v.fb, v.ping, v.width, v.lowcut, v.highcut, v.chr, v.duck, v.nodes, v.spread, v.sat, v.pan }) vkey << juce::String (juce::roundToInt (x * 2000)) << ",";
        stageUnder (G); G.sprite ("echo/path", 0, 0);
        const auto tr = G.layer ("echo-trace|" + vkey, 280, 300, 0, 0, [&] (pv::G& g)
        {
            Poly pts { { AX - 20, AY + 4 } }; for (auto& c : pl.C) pts.push_back ({ c.x, c.y + c.cut * 8 * c.sc }); if (pts.size() < 2) return;
            auto ln = [&] (const Poly& p, float w, float a) { if (look == Look::neon) { TubeOpt to; to.a = a; to.halo = 0; tubeLine (g, p, w * 0.6f, acc, to); } else g.stroke (p, w, K (acc, a)); };
            const auto Cat = [&] (size_t i) -> const Cp& { return pl.C[juce::jmin (i, pl.C.size() - 1)]; };
            if (mode == 0) { for (float o : { -1.6f, 1.6f }) { Poly q; for (size_t i = 0; i < pts.size(); ++i) q.push_back ({ pts[i].x + o * 0.4f, pts[i].y + o * (1 - 0.5f * (float) i / (float) pts.size()) }); ln (q, 0.9f, 0.75f); } }
            else if (mode == 1) { ln (pts, 1.5f, 0.85f); if (look != Look::neon) g.stroke (pts, 3.6f, K (acc, 0.2f)); }
            else if (mode == 2) { for (size_t i = 0; i + 1 < pts.size(); ++i) for (int j = 0; j < 3; ++j) { const float t = ((float) j + 0.5f) / 3, px = lerp (pts[i].x, pts[i + 1].x, t), py = lerp (pts[i].y, pts[i + 1].y, t), r = 2.6f * Cat (i).sc; g.rect (px - r, py - r * 0.6f, r * 2, r * 1.2f, K (acc, 0.8f)); } }
            else { for (size_t i = 0; i + 1 < pts.size(); ++i) for (int j = 0; j < 6; ++j) { const float t = (float) j / 6, px = lerp (pts[i].x, pts[i + 1].x, t), py = lerp (pts[i].y, pts[i + 1].y, t), hg = 2.4f * Cat (i).sc; g.stroke ({ { px, py - hg }, { px, py + hg } }, 0.8f, K (acc, 0.8f)); } }
            for (auto& c : pl.C)
            {
                const float fy = c.y + c.cut * 8 * c.sc + 1, rx = 13 * c.sc, ry = 4.4f * c.sc; const auto el = circ (c.x, fy, rx, ry, 16); StrokeOpt cl; cl.closed = true;
                if (pl.sync)
                {
                    if (look == Look::neon) { TubeOpt to; to.closed = true; to.halo = 0; to.a = 0.9f; tubeLine (g, el, 0.8f, acc, to); } else g.stroke (el, 1.3f * juce::jmax (0.6f, c.sc), K (acc, 0.85f), cl);
                    if (c.onBeat) g.stroke (circ (c.x, fy, rx * 1.3f, ry * 1.3f, 16), 0.8f, K (acc, 0.6f), cl);
                    if (pl.di == 4 || pl.di == 7 || pl.di == 10 || pl.di == 13) g.ell (c.x + rx * 1.5f, fy, 1.7f * c.sc, 1.2f * c.sc, K (acc, 0.95f));
                    if (pl.di == 2 || pl.di == 5 || pl.di == 8 || pl.di == 11) for (int j = -1; j <= 1; ++j) g.stroke ({ { c.x + (float) j * 3.4f * c.sc, fy + ry + 1.2f * c.sc }, { c.x + (float) j * 3.4f * c.sc, fy + ry + 3.6f * c.sc } }, 0.8f, K (acc, 0.9f));
                }
                else { StrokeOpt d; d.closed = true; d.dash = { 2.4f, 2.4f }; g.stroke (el, 0.9f, K (acc, 0.6f), d); }
            }
            if (pl.run) glow (g, BX, BY - 10, 1.5f, look == Look::neon ? HSL { 140, 100, 60 } : HSL { 60, 90, 80 }, 0.8f);
        });
        G.blit (tr, 0, 0, mix);
        std::map<int, bool> flash; std::vector<float> rings;
        if (on) for (double t0 : S.hits) { const float tau = (float) ((T.t - t0) / Ts); if (tau < 0) continue; const int kk = (int) std::floor (tau + 1e-6f); const float fr = (tau - (float) kk) * Ts; if (kk >= 1 && fr < 0.17f) flash[kk] = true; if (tau < (float) pl.C.size() + 0.5f && tau > 0.05f) rings.push_back (tau); }
        const float duckK = 1 - v.duck * juce::jmin (1.0f, lvl * 1.5f), wow = mode == 0 ? std::sin ((float) T.t * 0.55f * TAU) : 0.0f;
        for (int i = (int) pl.C.size() - 1; i >= 0; --i)
        {
            const auto& c = pl.C[(size_t) i]; const bool fl = flash.count (c.k) > 0; const float a = clampf (c.al * (fl ? 2.2f : 1.0f), 0, 1) * duckK * mix, wy = mode == 0 ? wow * chr * 5 * c.sc * ((c.k % 2) ? 1.0f : -1.0f) : 0.0f, dy = v.duck * lvl * 6 * c.sc + wy;
            if (pl.nodes > 1 && c.k <= 3 && v.spread >= 0.04f)
            {
                const auto nxt = pl.at ((float) c.k + 1);
                for (int j = juce::jmin (pl.nodes - 1, 5); j >= 1; --j)
                {
                    const float t = (float) j / (float) pl.nodes * v.spread * (0.5f + 0.37f * (float) j / (float) pl.nodes) * 1.6f, sg = (j % 2) ? 1.0f : -1.0f;
                    const float mx = lerp (c.x, nxt.x, juce::jmin (0.95f, t)) + sg * v.pan * 12 * c.sc * 0.42f, my = lerp (c.y, nxt.y, juce::jmin (0.95f, t)) + sg * v.pan * 12 * c.sc * 0.38f;
                    G.blit (copy (G, c, pl, v, T, j, vkey), mx, my + dy, a * 0.8f);
                }
            }
            const auto Lc = copy (G, c, pl, v, T, 0, vkey);
            if (look != Look::neon) glow (G, c.x, c.y - 16 * c.sc + dy, 1.5f * c.sc, look == Look::dawn ? HSL { 84, 70, 80 } : HSL { 120, 60, 80 }, a * (look == Look::dawn ? 0.4f : 0.26f));
            if (mode == 0) G.blit (Lc, c.x + 1.6f * c.sc + wow * chr * 1.2f, c.y + dy, a * 0.3f);
            G.blit (Lc, c.x, c.y + dy, a);
            if (fl) { glow (G, c.x, c.y - 16 * c.sc, 1.5f * c.sc + 0.3f, look == Look::neon ? HSL { 140, 100, 62 } : HSL { 70, 90, 82 }, 0.6f * mix); LineOpt lo; lo.closed = true; lo.a = 0.9f; lo.ta = 0; lo.tb = 0; lo.sd = (float) c.k; lo.halo = 1; line (G, circ (c.x, c.y, 15 * c.sc, 5 * c.sc, 14), 1.2f, acc, lo); }
        }
        for (float tau : rings)
        {
            const auto p = pl.at (tau); const float a = clampf (std::pow (0.22f + 0.76f * pl.fb, tau) * 1.6f, 0, 1) * mix; if (a < 0.06f) continue; const float fr = std::fmod (tau, 1.0f);
            LineOpt lo; lo.closed = true; lo.a = a; lo.ta = 0; lo.tb = 0; lo.sd = 3; lo.halo = 0; line (G, circ (p.x, p.y + 1, (13 + 5 * fr) * p.sc, (4.4f + 1.6f * fr) * p.sc, 14), 1.1f, acc, lo);
        }
        const auto hp = hitPose (k, 0, false); const int idleB = on ? (int) std::floor ((float) T.tick / 9) % 2 : 0;
        RigP P; P.s = 36; P.x = 50; P.y = 246 - lvl * 2.5f; P.tail = 0.25f; P.lean = -0.04f; P.sx = 1 + (idleB ? 0.012f : 0.0f); P.sy = 1 - (idleB ? 0.014f : 0.0f); P.hem = idleB ? 0.5f : 0.0f;
        if (hp.on) { P.sx = hp.sx; P.sy = hp.sy; P.lift = hp.lift; P.hem = hp.hem; }
        const Rig R = ghostRig (P); GL L; L.slime = look == Look::neon ? HSL { 140, 100, 56 } : HSL { 92, 72, 52 }; L.eyes = ! on ? "closed" : hp.on && hp.ph == "squat" ? "squeeze" : "open";
        L.mouth = ! on ? "flat" : k >= 0 && k < 5 ? "o" : "smile"; L.mouthK = k >= 0 && k < 5 ? 1.35f : 1.0f; L.blush = 0.6f; L.glow = look == Look::dawn ? 0.5f : 0.6f; L.tube = HSL { 140, 100, 58 }; L.face = HSL { 60, 90, 88 }; L.k = 1;
        ghost (G, R, L, T);
        if (k >= 0 && k < 4 && on) { const auto m = R.X (R.mouth.x + 0.5f, R.mouth.y); for (int j = 0; j < 3; ++j) { const float r = 7 + (float) j * 7 + (float) k * 4.4f; LineOpt lo; lo.a = (1 - (float) k / 5) * (1 - (float) j * 0.22f); lo.ta = 0.25f; lo.tb = 0.25f; lo.sd = (float) j; lo.halo = 0; line (G, arcp (m.x, m.y, r, r * 1.3f, -0.8f, 0.8f, 8), 2.2f - (float) j * 0.4f, acc, lo); } }
        if (k >= 0 && k < 6 && on) { const auto m = R.X (R.mouth.x + 0.5f, R.mouth.y); for (int j = 0; j < 3; ++j) { const float r = 40 + (float) j * 26 + (float) k * 17; if (r > 230) continue; G.stroke (arcp (m.x, m.y, r, r * 0.9f, -1.05f, 0.12f, 16), look == Look::neon ? 1.0f : 1.3f, K (acc, (1 - (float) k / 6) * (0.4f - 0.1f * (float) j))); } }
        if (! on) offVeil (G);
        stageOver (G);
    }
};
} // namespace phantom::pv
