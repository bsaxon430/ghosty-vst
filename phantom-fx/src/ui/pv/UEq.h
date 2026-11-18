// SPDX-License-Identifier: AGPL-3.0-or-later
// EQ (SPIRIT EQ; port of phantom-anime/visuals/src/u_eq.js): the EQ's curve is a ridge of hills at dawn, lows at the left, highs at
// the right; two nearer ranges follow it more gently. The sound is glowing mist along the ridge where the spectrum is loud. A lantern
// stands on every band that is on; the ghost sits on the hill of the band being edited. 24 bands x 17 controls is far more than a
// visual can map, so this one is FED (the plugin's own curve, bands and edited band) and maps only OUTPUT, MODE and MIX.
#pragma once
#include "URetro.h"

namespace phantom::pv
{
class EqScene final : public Scene
{
public:
    std::vector<std::string> controls() const override { return { "out", "mode", "mix" }; }
    std::vector<float> defaults() const override { return { 0.5f, 0, 1 }; }
    struct D { std::vector<float> curve; std::vector<std::array<float, 3>> bands; int sel; std::vector<float> spec; float at (float x) const { const float p = clampf (x, 0, 1) * (float) (curve.size() - 1); const int i = juce::jmin ((int) curve.size() - 2, (int) std::floor (p)); return lerp (curve[(size_t) i], curve[(size_t) i + 1], p - (float) i); } };
    // what the plugin feeds, or the painter's stand-in (four bands, all off: a flat curve)
    static D feed (const Feed& F, const Motion& m, int n = 64)
    {
        D d; d.curve = F.curve; d.bands = F.bands; d.sel = F.sel; d.spec = F.spec;
        if (d.curve.size() < 2) { d.curve.assign ((size_t) n, 0.0f); d.bands = { { 0.1f, 0, 0 }, { 0.37f, 0, 0 }, { 0.62f, 0, 0 }, { 0.87f, 0, 0 } }; d.sel = 0; }
        if (d.spec.empty()) for (int i = 0; i < 16; ++i) { const float q = (float) i / 15; d.spec.push_back (m.on ? clampf ((m.low * std::exp (-std::pow (q / 0.3f, 2.0f)) + m.mid * std::exp (-std::pow ((q - 0.45f) / 0.25f, 2.0f)) + m.high * std::exp (-std::pow ((q - 0.85f) / 0.3f, 2.0f))) * (0.6f + 0.4f * hsh (i, (int) std::floor (F.t * 6))), 0, 1) : 0.0f); }
        return d;
    }
    struct Z { float X0, X1, W, H, y0, top, k, yb, z, gs; bool own = false; };
    static void land (G& G, const Feed& F, State& S, const Clock& T, const Z& Z)
    {
        const Look look = G.look; const auto m = gmo (F, S, T); const bool on = m.on; const float lvl = m.lvl, mixk = mixA (F.v ("mix")); const D Dd = feed (F, m); const bool neon = look == Look::neon;
        const HSL dk = L3<HSL> (look, { 262, 30, 14 }, INKC, { 262, 60, 30 }); const float bs = 3 + (float) boilOf (T, look == Look::dawn ? 4 : 3) * 1.7f, X0 = Z.X0, X1 = Z.X1, W = Z.W, H = Z.H, k = Z.k, yb = Z.yb, z = Z.z;
        const float y0 = Z.y0 - (F.v ("out") * 48 - 24) * k * 0.42f; auto yOf = [&] (float x, float am = 1, float dy = 0) { return clampf (y0 + dy - Dd.at ((x - X0) / (X1 - X0)) * k * am * mixk, Z.top, yb + 6 * z); };
        std::vector<float> xs; for (int i = 0; i < 49; ++i) xs.push_back (-2 + (W + 4) * (float) i / 48);
        auto ridge = [&] (float am, float dy, float wv = 0) { Poly r; for (size_t i = 0; i < xs.size(); ++i) r.push_back ({ xs[i], yOf (xs[i], am, dy) + (wv != 0 ? std::sin ((float) i * 1.7f + dy) * wv + std::sin ((float) i * 0.6f + 2) * wv * 1.4f : 0.0f) }); return r; };
        auto poly = [&] (Poly r) { r.push_back ({ W + 2, H + 2 }); r.push_back ({ -2, H + 2 }); return r; };
        const Poly R1 = ridge (1, 0), R2 = ridge (0.55f, 22 * z, 1.6f * z), R3 = ridge (0.25f, 44 * z, 2.2f * z); const float sunX = X0 + (X1 - X0) * 0.72f, sunY = Z.y0 - 2 * z;
        if (neon)
        {
            if (Z.own) G.rect (0, 0, W, H, K ({ 250, 60, 5 })); G.rect (0, 0, W, H, K ({ 262, 70, 8 }, 0.5f)); glow (G, sunX, sunY, z < 1 ? 1.0f : 2.6f, { 322, 100, 60 }, 0.5f);
            const float sr = 26 * z; G.fill (arcp (sunX, sunY, sr, sr, PI, TAU, 14), K ({ 38, 100, 58 }, 0.9f)); { std::vector<Poly> ls; for (int i = 0; i < 4; ++i) ls.push_back ({ { sunX - sr, sunY - 3 * z - (float) i * 5 * z }, { sunX + sr, sunY - 3 * z - (float) i * 5 * z } }); G.strokeMany (ls, 1.6f * z, K ({ 262, 70, 8 }, 0.9f), false); }
            G.fill (poly (R1), K ({ 250, 60, 6 }, 0.94f)); G.fill (poly (R1), K ({ 188, 100, 60 }, 0.1f));
            { std::vector<Poly> ls; for (int i = 0; i < 25; ++i) { const float x = xs[(size_t) i * 2]; ls.push_back ({ { x, R1[(size_t) i * 2].y }, { x + (x - W / 2) * 0.12f, R2[(size_t) i * 2].y } }); } G.strokeMany (ls, 0.7f, K ({ 188, 100, 64 }, 0.4f)); }
            { TubeOpt to; to.halo = Z.own ? 0.0f : 1.0f; tubeLine (G, R1, 1.4f * juce::jmax (0.7f, z), { 188, 100, 64 }, to); }
            G.fill (poly (R2), K ({ 250, 60, 5 }, 0.92f)); { TubeOpt to; to.halo = 0; to.k = 0.8f; tubeLine (G, R2, 1 * juce::jmax (0.7f, z), { 322, 100, 62 }, to); }
            G.fill (poly (R3), K ({ 250, 60, 4 }, 0.94f)); { TubeOpt to; to.halo = 0; to.k = 0.7f; tubeLine (G, R3, 0.9f * juce::jmax (0.7f, z), { 262, 90, 66 }, to); }
        }
        else
        {
            const bool dw = look == Look::dawn; const HSL sky[3] { dw ? HSL { 204, 44, 30 } : HSL { 250, 36, 34 }, dw ? HSL { 30, 60, 62 } : HSL { 330, 44, 60 }, dw ? HSL { 42, 90, 76 } : HSL { 40, 90, 74 } };
            G.rect (0, 0, W, Z.y0 - 62 * z + 1, K (sky[0]));
            const float bands[4][2] { { 62, 48 }, { 48, 34 }, { 34, 23 }, { 23, 13 } }; const HSL bc[4] { mixrgb (sky[0], sky[1], 0.55f), mixrgb (sky[0], sky[1], 0.8f), sky[1], mixrgb (sky[1], sky[2], 0.5f) };
            for (int i = 0; i < 4; ++i) G.rect (0, Z.y0 - bands[i][0] * z, W, (bands[i][0] - bands[i][1]) * z + 1, K (bc[i]));
            G.rect (0, Z.y0 - 13 * z, W, yb - Z.y0 + 13 * z + 1, K (sky[2]));
            if (dw) for (float q : { 0.2f, 0.5f, 0.8f }) glow (G, W * q, Z.y0 - 34 * z, z < 1 ? 1.0f : 2.1f, sky[1], 0.6f, true);
            glow (G, sunX, sunY, z < 1 ? 1.0f : 3.4f, { 48, 100, 84 }, 0.75f); G.ell (sunX, sunY, 15 * z, 15 * z, K ({ 52, 100, 95 }));
            if (! Z.own) { const float cl[4][5] { { 52, Z.y0 - 66, 44, 7, 3 }, { 150, Z.y0 - 84, 56, 8, 5 }, { 236, Z.y0 - 58, 38, 6, 8 }, { 108, Z.y0 - 44, 30, 4.4f, 11 } };
                for (auto& c : cl) { G.fill (blobp (c[0], c[1], c[2], c[3], 0.3f, 12, c[4]), K (dw ? HSL { 214, 30, 40 } : HSL { 262, 30, 44 }, 0.85f)); G.fill (blobp (c[0] + 3, c[1] + c[3] * 0.45f, c[2] * 0.86f, c[3] * 0.5f, 0.3f, 10, c[4] + 1), K (dw ? HSL { 24, 80, 70 } : HSL { 340, 60, 70 }, 0.8f)); } }
            const HSL c1 = dw ? HSL { 214, 30, 44 } : HSL { 252, 28, 46 }, c2 = dw ? HSL { 176, 34, 30 } : HSL { 262, 30, 32 }, c3 = dw ? HSL { 150, 38, 17 } : HSL { 266, 34, 20 };
            G.fill (poly (R1), K (c1)); { Poly p; for (auto q : R1) p.push_back ({ q.x + 5 * z, q.y + 7 * z }); for (auto it = R1.rbegin(); it != R1.rend(); ++it) p.push_back ({ it->x + 5 * z, it->y + 34 * z }); G.fill (p, K (mixrgb (c1, c2, 0.45f))); }
            if (look == Look::cel) G.fillMany ({ ribbon (R1, 1.5f * juce::jmax (0.7f, z), bs, 0.02f, 0.02f) }, K (INKC)); else { Poly p; for (auto q : R1) p.push_back ({ q.x, q.y + 0.8f }); G.stroke (p, 1.3f * juce::jmax (0.7f, z), K ({ 44, 90, 84 }, 0.7f)); }
            G.fill (poly (R2), K (c2)); if (look == Look::cel) G.fillMany ({ ribbon (R2, 1.3f * juce::jmax (0.7f, z), bs + 3, 0.02f, 0.02f) }, K (INKC)); else { Poly p; for (auto q : R2) p.push_back ({ q.x, q.y + 0.8f }); G.stroke (p, 1, K ({ 60, 60, 60 }, 0.4f)); }
            { std::vector<Poly> firs; for (int i = 0; i < (Z.own ? 6 : 11); ++i) { const int j = 3 + (int) std::floor (hsh (i, 5) * 42); const V2 q = R2[(size_t) j]; const float h = (6 + 7 * hsh (i, 7)) * z; firs.push_back ({ { q.x - 2.6f * z, q.y + 1 }, { q.x, q.y - h }, { q.x + 2.6f * z, q.y + 1 } }); } G.fillMany (firs, K (mixrgb (c2, c3, 0.6f))); }
            G.fill (poly (R3), K (c3)); if (look == Look::cel) G.fillMany ({ ribbon (R3, 1.3f * juce::jmax (0.7f, z), bs + 5, 0.02f, 0.02f) }, K (INKC));
            if (dw && ! Z.own) G.clip (poly (R1), [&] { for (float y = Z.y0 - 10; y < 300; y += 120) for (float x = 0; x < 280; x += 120) { SpriteOpt so; so.a = 0.5f; G.sprite (((int) ((x + y) / 120)) % 2 ? "tex-bristle-b" : "tex-bristle", x, y, so); } });
        }
        // the lake: MODE = how still it lies (Zero Latency ripples, the linear modes smooth it to a mirror)
        const int md = juce::jlimit (0, 3, juce::roundToInt (F.v ("mode")));
        G.fill ({ { -2, yb }, { W + 2, yb }, { W + 2, H + 2 }, { -2, H + 2 } }, K (neon ? HSL { 250, 60, 7 } : L3<HSL> (look, { 204, 40, 34 }, { 256, 34, 38 }, {}), neon ? 0.9f : 0.94f));
        if (! neon) { std::vector<Poly> ls; for (int i = 0; i < (Z.own ? 3 : 6); ++i) { const float y = yb + (3 + (float) i * 6.4f) * z, hw = (20 - 2.6f * (float) i) * z * (0.8f + 0.4f * hsh (i, 7)); ls.push_back ({ { sunX - hw, y }, { sunX + hw, y } }); } G.strokeMany (ls, juce::jmax (1.0f, 2.2f * z), K ({ 48, 95, 82 }, 0.6f)); }
        {
            Poly mir; for (auto q : R1) if (q.y < yb) mir.push_back ({ q.x, yb + (yb - q.y) * 0.45f });
            if (mir.size() > 2)
            {
                if (neon) { G.stroke (mir, 1, K ({ 188, 100, 64 }, 0.3f)); std::vector<Poly> ls; for (int i = 0; i < (Z.own ? 3 : 5); ++i) { const float y = yb + (4 + (float) i * 7) * z, hw = (22 - 3 * (float) i) * z; ls.push_back ({ { sunX - hw, y }, { sunX + hw, y } }); } G.strokeMany (ls, juce::jmax (1.0f, 2 * z), K ({ 38, 100, 58 }, 0.5f), false); }
                else { Poly p { { mir.front().x, yb } }; for (auto q : mir) p.push_back (q); p.push_back ({ mir.back().x, yb }); G.fill (p, K (L3<HSL> (look, { 214, 30, 44 }, { 252, 28, 46 }, {}), 0.3f)); }
            }
        }
        G.stroke ({ { -2, yb }, { W + 2, yb } }, 1, K (neon ? HSL { 188, 100, 64 } : L3<HSL> (look, { 50, 70, 84 }, { 46, 60, 84 }, {}), 0.7f));
        { static const int nr[4] { 5, 3, 2, 0 }; std::vector<Poly> ls; for (int i = 0; i < nr[md]; ++i) { const float y = yb + (5 + (float) i * 7) * z, x = W * (0.14f + 0.62f * hsh (i, 3)) + std::sin ((float) T.tick / 12 * 1.2f + (float) i) * 4 * z; ls.push_back ({ { x, y }, { x + (30 + 30 * hsh (i, 5)) * z, y } }); } G.strokeMany (ls, 1, K (neon ? HSL { 188, 100, 70 } : HSL { 50, 60, 92 }, 0.5f)); }
        // the sound: glowing mist along the ridge where the spectrum is loud; motes rise from the loudest places
        if (on) for (size_t i = 0; i < Dd.spec.size(); ++i) { const float s = Dd.spec[i]; if (s < 0.06f) continue; const float x = X0 + (X1 - X0) * ((float) i + 0.5f) / (float) Dd.spec.size(); glow (G, x, yOf (x) - 5 * z, (s > 0.6f ? 1.5f : s > 0.3f ? 1.0f : 0.75f) * (z < 1 ? 0.5f : 1.0f), neon ? HSL { 188, 100, 70 } : HSL { 46, 100, 86 }, (0.14f + 0.5f * s) * mixk); }
        if (on && ! Z.own) for (int i = 0; i < 7; ++i) { const int j = (int) std::floor (hsh (i, 3) * (float) Dd.spec.size()); const float s = Dd.spec[(size_t) j], c = std::fmod (((float) T.f / 24) * (0.25f + 0.2f * hsh (i, 5)) + hsh (i, 7), 1.0f), x = X0 + (X1 - X0) * ((float) j + 0.5f) / (float) Dd.spec.size() + std::sin (c * 8 + (float) i) * 4; if (s > 0.25f) G.ell (x, yOf (x) - 6 - c * 46 * s, 1.2f, 1.2f, K (neon ? HSL { 188, 90, 84 } : HSL { 50, 100, 92 }, 0.8f * std::sin (c * PI))); }
        // a lantern on every band that is on (a ring round a dynamic one); the ghost on the band being edited
        for (size_t i = 0; i < Dd.bands.size(); ++i)
        {
            const auto& b = Dd.bands[i]; if (b[1] < 0.5f || (int) i == Dd.sel) continue; const float x = X0 + (X1 - X0) * b[0], y = yOf (x); const HSL c { (float) ((188 + (int) i * 47) % 360), 90, 70 };
            G.stroke ({ { x, y + 1 }, { x, y - 9 * z } }, juce::jmax (1.0f, 1.3f * z), K (neon ? c : dk)); G.ell (x, y - 11 * z, 2.8f * z + 0.4f, 2.8f * z + 0.4f, K (c)); glow (G, x, y - 11 * z, 0.5f, c, 0.7f); if (b[2] > 0.5f) G.ring (x, y - 11 * z, (5 + 2 * lvl) * z, (5 + 2 * lvl) * z, 0.9f, K (c, 0.8f));
        }
        if (Dd.sel >= 0 && Dd.sel < (int) Dd.bands.size())
        {
            const auto& b = Dd.bands[(size_t) Dd.sel]; const float x = X0 + (X1 - X0) * b[0], y = yOf (x), s = Z.gs; RigP P = m.P; P.s = s; P.x = x; P.y = y + s * 0.12f; P.tail = 0.25f; const Rig R = ghostRig (P); const bool bon = b[1] > 0.5f;
            glow (G, x, y - s, clampf (s / 20, 0.5f, 1.5f), { 188, 100, 74 }, (0.4f + 0.3f * lvl) * (bon ? 1.0f : 0.5f));
            GL L; L.slime = HSL { 188, 78, 62 }; L.eyes = ! on ? "closed" : m.hp.on && ! m.hp.eyes.empty() ? m.hp.eyes : "open"; L.mouth = ! on ? "flat" : "smile"; L.blush = 0.5f; L.glow = 0; L.lite = Z.own || s < 24; L.a = bon ? 1.0f : 0.6f;
            if (neon) { L.tube = HSL { 188, 100, 66 }; L.slime = HSL { 188, 100, 60 }; L.k = 0.9f + 0.1f * hsh (T.tick, 5); }
            ghost (G, R, L, T); { WearOpt wo; wo.a = L.a; wear (G, R, "halo", T, wo); } if (b[2] > 0.5f) G.ring (x, y - s, s * 1.15f + 3 * lvl, (s * 1.15f + 3 * lvl) * 0.9f, 1, K ({ 188, 90, 80 }, 0.6f));
        }
    }
    void scene (G& G, const Feed& F, State& S, const Clock& T) override
    {
        if (G.look == Look::neon) stageUnder (G); Z z { 14, 266, 280, 300, 168, 30, 3, 254, 1, 22 }; land (G, F, S, T, z); if (! F.on) offVeil (G); stageOver (G);
    }
    // DAWN: the ridge as a small strip in the page's header, between the blurb and TRIM (250,282,220,40; its own opaque pane)
    bool hasDawn() const override { return true; }
    bool dawnUnder() const override { return false; }
    juce::Rectangle<float> dawnRect() const override { return { 250, 282, 220, 40 }; }
    void dawnScene (G& G, const Feed& F, State& S, const Clock& T, float W, float H) override
    {
        Z z { 6, 214, W, H, 23, 3, 0.62f, 34, 0.34f, 7.5f, true }; G.clip (rbox (0, 0, W, H, 6, 3), [&] { land (G, F, S, T, z); });
        G.stroke (rbox (0.5f, 0.5f, W - 1, H - 1, 6, 3), 1, K ({ 40, 50, 60 }, 0.8f), { true }); if (! F.on) G.fill (rbox (0, 0, W, H, 6, 3), rgba (10, 8, 24, 0.45f));
    }
};
} // namespace phantom::pv
