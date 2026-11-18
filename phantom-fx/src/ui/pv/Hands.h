// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX painted unit visuals: the three hands (DAWN gouache, CEL ink, NEON tubes) and what every unit shares. Port of
// phantom-anime/visuals/src/styles.js and ukit.js. Scenes ask a hand for a ghost, a solid form, a line, a light.
#pragma once
#include "Rig.h"

namespace phantom::pv
{
// ---------------------------------------------------------------- what a scene is fed, its clock and its state
struct Feed
{
    const std::vector<std::string>* names = nullptr; std::array<float, 24> vals {};
    float level = 0, hit = 0, low = -1, mid = -1, high = -1; bool on = true; double t = 0;
    // what a wide-page unit's own editor knows (batch 3; empty / negative = not sent: the scene uses its own stand-in)
    float pos = -2, pump = -1;            // the playhead along the pattern 0..1 (-1 = the pattern is bypassed), the pump's gain now
    std::vector<float> curve;             // GATE: the shown pattern's level at even places, 0..1
    std::vector<int> steps;               // GATE: its 16 time-lane modes
    std::vector<int> rows;                // GRID: the shown pattern, one number per row (bit k = step k painted)
    int held = -2, sel = -1; float env = -1, prog = -1;   // HAUNT: the pad that plays (-1 none), the pad being edited, its envelope, how far through its slice
    std::vector<std::array<float, 3>> pads;               // HAUNT: 16 x { type, size, motion }
    float semis = -99; std::vector<float> lane;           // WARP: the pitch now (semitones), the drawn lane at even places (-1..1)
    std::vector<float> spec; std::vector<std::array<float, 3>> bands;   // EQ: 16 levels; per band { x, on, dyn }
    bool hasHeld() const { return held > -2; }
    bool hasPos() const { return pos > -1.5f; } bool hasPump() const { return pump >= 0; }
    float v (const char* n) const { if (names) for (size_t i = 0; i < names->size(); ++i) if ((*names)[i] == n) return vals[i]; return 0.0f; }
    float lowOr() const { return low >= 0 ? low : level; } float midOr() const { return mid >= 0 ? mid : level; } float highOr() const { return high >= 0 ? high : level; }
};
struct Clock { double t = 0; int tick = 0, f = 0; static Clock at (double t) { return { t, (int) std::floor (t * 12 + 1e-6), (int) std::floor (t * 24 + 1e-6) }; } };
struct State
{
    bool started = false; float prevHit = 0; int hitTick = -99; std::vector<double> hits; std::map<std::string, float> keys; std::map<std::string, Clock> clocks; std::map<std::string, double> dv;
    void step (const Feed& F, const Clock& T)
    {
        if (! started) { started = true; prevHit = 0; hitTick = -99; hits.clear(); }
        if (F.on && F.hit > 0.5f && prevHit <= 0.5f) { hitTick = T.tick; hits.push_back (T.t); if (hits.size() > 8) hits.erase (hits.begin()); }
        prevHit = F.hit;
    }
    // a value that follows a target at its own speeds (a still starts on the target)
    float follow (const std::string& key, float target, float up = 1, float down = 0.2f)
    {
        auto it = keys.find (key); if (it == keys.end()) { keys[key] = target; return target; }
        const float p = it->second, v = target > p ? p + (target - p) * up : p + (target - p) * down; it->second = v; return v;
    }
};

// ---------------------------------------------------------------- palettes
inline const HSL INKC { 268, 30, 14 };
namespace pal
{
    inline const HSL celInk { 268, 30, 14 }, celBody { 45, 40, 96 }, celShade { 255, 28, 79 };
    inline const HSL dawnInk { 268, 32, 22 }, dawnBody { 46, 46, 93 }, dawnShade { 262, 24, 70 }, dawnRim { 48, 100, 88 }, dawnBounce { 150, 40, 66 };
    inline const HSL neonInk { 250, 45, 7 }, neonGlass { 240, 60, 8 }, neonWhite { 210, 60, 92 };
}
inline int boilOf (const Clock& T, int every = 3) { return (int) std::floor ((float) T.tick / (float) every); }
template <typename T> T L3 (Look l, T d, T c, T n) { return l == Look::dawn ? d : l == Look::cel ? c : n; }

// ---------------------------------------------------------------- NEON tube: three halo strokes, the colour body, the white-hot core
struct TubeOpt { float k = 1, heat = 0, a = 1; bool closed = false; float halo = 1; };
inline void tubeLine (G& g, const Poly& p, float w, HSL col, TubeOpt o = {})
{
    StrokeOpt so; so.closed = o.closed;
    if (o.k <= 0.03f)
    {
        g.stroke (p, w, K ({ col.h, 22, 27 }, o.a), so);
        g.stroke (xf (p, [&] (float x, float y) { return V2 { x - w * 0.18f, y - w * 0.2f }; }), juce::jmax (0.3f, w * 0.26f), K ({ col.h, 30, 56 }, 0.7f), so);
        return;
    }
    const float a = o.a, L = lerp (30, col.l, o.k) + o.heat * 14; const HSL c { col.h, col.s - o.heat * 10, L };
    if (o.halo != 0) { g.stroke (p, w * 8, K (c, 0.05f * o.k * a), so); g.stroke (p, w * 4.6f, K (c, 0.12f * o.k * a), so); g.stroke (p, w * 2.4f, K (c, 0.26f * o.k * a), so); }
    g.stroke (p, w, K (c, a), so);
    g.stroke (p, w * (0.46f + 0.2f * o.heat), K ({ col.h, lerp (60, 95, o.k) - o.heat * 40, lerp (52, 91, o.k) + o.heat * 6 }, a), so);
}

// ---------------------------------------------------------------- ghost options (the JS "L")
struct GL
{
    float a = 1, iw = 1; std::optional<float> sd;
    std::optional<HSL> ink, body, shade, slime, pupil, rim, bounce, tint, glowC, blushC, faceInk, tube, face;
    float tintA = 0.3f; bool simple = false, lite = false, holo = false; float gloss = 0, gap = 0;
    std::optional<float> shw, glow, tex, blush, glass; float glowK = 1;
    std::string eyes = "open", mouth = "smile"; float vowel = -1, mouthK = 1;
    float heat = 0, k = 1, bulge = 1, faceHalo = 0;
    std::shared_ptr<const Rig> faceR;   // the face taken from a second rig (WARP: KEEP FORMANTS)
};

inline void celFace (G& G, const Rig& R, const GL& L, const Clock&, juce::Colour ink, HSL pupil, float iw, float sd, float a, HSL blushDefault)
{
    const float s = R.s; const std::string& em = L.eyes;
    const float blush = L.blush.value_or (0.6f);
    if (blush > 0) for (auto& c : R.cheeks) { const auto q = R.X (c.x, c.y); G.ell (q.x, q.y, c.rx * s * R.sx, c.ry * s * R.sy, K (L.blushC.value_or (blushDefault), blush * a)); }
    for (auto& e : R.eyes)
    {
        const auto q = R.X (e.x, e.y); const float rx = e.rx * s * R.sx, ry = e.ry * s * R.sy;
        if (em == "open" || em == "wide")
        {
            const float k = em == "wide" ? 1.28f : 1.0f;
            G.fill (blobp (q.x, q.y, rx * k, ry * k, 0.05f, 10, sd + e.x * 9), ink);
            G.ell (q.x + rx * 0.25f * k, q.y + ry * 0.4f * k, rx * 0.5f * k, ry * 0.34f * k, K (pupil, a));
            G.ell (q.x - rx * 0.25f * k, q.y - ry * 0.42f * k, rx * 0.42f * k, ry * 0.3f * k, K ({ 50, 100, 98 }, a));
        }
        else if (em == "happy" || em == "closed")
        {
            const float dir = em == "happy" ? -1.0f : 1.0f, r = 0.11f * s * (1 + 0.2f * R.g); Poly p;
            for (int i = 0; i <= 8; ++i) { const float an = PI + (float) i / 8 * PI; p.push_back ({ q.x + std::cos (an) * r, q.y + 0.03f * s - dir * std::sin (an) * r * 0.75f - (dir > 0 ? 0.0f : r * 0.5f) }); }
            G.fill (ribbon (p, iw * 1.15f, sd + e.x, 0.15f, 0.15f), ink);
        }
        else if (em == "squeeze") { const float d = e.x < R.mouth.x ? 1.0f : -1.0f, r = 0.11f * s; G.fill (ribbon ({ { q.x - r * d, q.y - r * 0.7f }, { q.x + r * 0.6f * d, q.y }, { q.x - r * d, q.y + r * 0.7f } }, iw * 1.1f, sd, 0.1f, 0.1f), ink); }
        else if (em == "x") { const float r = 0.09f * s; G.fill (ribbon ({ { q.x - r, q.y - r * 1.1f }, { q.x + r, q.y + r * 1.1f } }, iw * 1.1f, sd, 0.1f, 0.1f), ink); G.fill (ribbon ({ { q.x + r, q.y - r * 1.1f }, { q.x - r, q.y + r * 1.1f } }, iw * 1.1f, sd + 1, 0.1f, 0.1f), ink); }
    }
    const auto m = mouthShape (R, L.mouth, L.mouthK, L.vowel);
    if (! m.line.empty()) G.fill (ribbon (m.line, iw, sd + 5, 0.2f, 0.2f), ink);
    if (! m.fill.empty())
    {
        G.fill (m.fill, ink);
        if (m.hasTongue) { const auto q = R.X (m.tx, m.ty); G.ell (q.x, q.y, m.trx * s, m.try_ * s, K ({ 350, 75, 66 }, a)); }
        if (m.hasTeeth) G.stroke (m.teeth, s * 0.035f, K ({ 50, 60, 97 }, a));
    }
}

inline Poly ghostCel (G& G, const Rig& R, const GL& L, const Clock& T)
{
    const float s = R.s, iw = juce::jmax (0.9f, s * 0.075f) * L.iw, sd = L.sd.value_or (3) + (float) boilOf (T, 3) * 17.3f, a = L.a;
    const auto ink = K (L.ink.value_or (INKC), a); const HSL body = L.body.value_or (pal::celBody), shade = L.shade.value_or (pal::celShade), slime = L.slime.value_or (HSL { 146, 72, 56 }), pupil = L.pupil.value_or (HSL { 262, 68, 68 });
    auto W_ = [&] (const Poly& p) { return wob (p, s * 0.014f, 0.9f / s, sd); };
    const Poly shp = W_ (R.body);
    if (! L.simple) for (auto& d : R.drips) { const auto p = W_ (d.p); G.fill (p, K (slime, a)); G.fill (W_ (d.hi), K ({ slime.h + 8, slime.s, slime.l - 16 }, a)); G.fillMany (outlineRibbons (p, iw * 0.8f, sd + d.x * 9), ink); }
    G.fill (shp, K (shade, a)); G.fill (litShape (shp, s * L.shw.value_or (0.17f + 0.24f * R.g)), K (body, a));
    if (! R.under.empty() && ! L.simple) G.fill (W_ (R.under), K (shade, 0.85f * a));
    if (L.tint) G.fill (shp, K (*L.tint, L.tintA));
    if (! R.slime.empty())
    {
        const auto sp = W_ (R.slime); G.fill (sp, K ({ slime.h + 8, slime.s, slime.l - 16 }, a)); G.fill (litShape (sp, s * 0.15f), K (slime, a));
        const auto hl = K ({ slime.h - 10, slime.s, juce::jmin (92.0f, slime.l + 22) }, a); const auto& b = R.band; const int nb = (int) b.size();
        if (! L.simple && ! L.lite) for (float t : { 0.22f, 0.5f, 0.76f }) { const auto q = b[(size_t) std::floor (t * (float) (nb - 1))]; G.ell (q.x, q.y + s * 0.15f, s * 0.07f, s * 0.035f, hl); }
        G.fill (ribbon (W_ (R.band), iw * 0.7f, sd, 0.05f, 0.05f), ink);
    }
    if (L.gloss > 0) { const auto q = R.gloss; const float r = s * (0.10f + 0.16f * L.gloss) * (1 + 0.5f * R.g); G.fill (blobp (q.x, q.y, r, r * 0.62f, 0.12f, 9, sd), K ({ 50, 100, 99 }, 0.9f * a)); G.ell (q.x + r * 1.25f, q.y + r * 0.75f, r * 0.22f, r * 0.16f, K ({ 50, 100, 99 }, 0.9f * a)); }
    { OutlineOpt oo; oo.gap = L.gap; G.fillMany (outlineRibbons (shp, iw, sd, oo), ink); }
    if (L.simple) return shp;
    for (auto& r : R.rolls) G.fill (ribbon (W_ (r), iw * 0.85f, sd + 2, 0.25f, 0.25f), ink);
    celFace (G, L.faceR ? *L.faceR : R, L, T, ink, pupil, iw, sd, a, { 350, 85, 74 });
    return shp;
}

inline Poly ghostDawn (G& G, const Rig& R, const GL& L, const Clock& T)
{
    const float s = R.s, iw = juce::jmax (0.8f, s * 0.06f) * L.iw, sd = L.sd.value_or (3) + (float) boilOf (T, 4) * 9.1f, a = L.a;
    const HSL body = L.body.value_or (pal::dawnBody), shade = L.shade.value_or (pal::dawnShade), slime = L.slime.value_or (HSL { 140, 60, 50 }), pupil = L.pupil.value_or (HSL { 262, 60, 62 }), dk = L.ink.value_or (pal::dawnInk);
    auto W_ = [&] (const Poly& p) { return wob (p, s * 0.024f, 0.55f / s, sd); };
    const Poly shp = W_ (R.body);
    const float glow = L.glow.value_or (0.55f);
    if (glow > 0) { SpriteOpt so; so.tint = K (L.glowC.value_or (HSL { 48, 95, 84 })); so.a = glow * a; so.size = clampf (R.halfW / 34 * L.glowK, 0.5f, 3); G.sprite ("glow-soft", R.mid.x, R.mid.y, so); }
    if (! L.simple) for (auto& d : R.drips) { const auto p = W_ (d.p); G.fill (p, K ({ slime.h + 10, slime.s, slime.l - 18 }, a)); G.fill (litShape (p, s * 0.05f), K (slime, a)); }
    const float shw = s * L.shw.value_or (0.14f + 0.16f * R.g);
    G.fill (shp, K (shade, a)); { LitOpt lo; lo.pw = 0.8f; G.fill (litShape (shp, shw, lo), K (mixrgb (shade, body, 0.55f), a)); } { LitOpt lo; lo.pw = 0.9f; G.fill (litShape (shp, shw * 2.5f, lo), K (body, a)); }
    if (! R.under.empty() && ! L.simple) G.fill (W_ (R.under), K (mixrgb (shade, body, 0.4f), 0.8f * a));
    { LitOpt lo; lo.bias = 0.55f; lo.pw = 1.4f; G.fill (litShape (shp, s * 0.5f, lo), K ({ body.h + 2, body.s + 20, juce::jmin (99.0f, body.l + 3.5f) }, 0.9f * a)); }
    { const auto rim = rimShape (shp, s * 0.07f, -0.55f, -0.83f, 0.25f); if (! rim.empty()) G.fill (rim, K (L.rim.value_or (pal::dawnRim), 0.95f * a)); }
    if (! L.lite) { const auto bnc = rimShape (shp, s * 0.085f, 0.75f, 0.5f, 0.45f); if (! bnc.empty()) G.fill (bnc, K (L.bounce.value_or (pal::dawnBounce), 0.5f * a)); }
    if (L.tint) G.fill (shp, K (*L.tint, L.tintA));
    if (! R.slime.empty())
    {
        const auto sp = W_ (R.slime); G.fill (sp, K ({ slime.h + 12, slime.s, slime.l - 20 }, a)); G.fill (litShape (sp, s * 0.16f), K (slime, a));
        const auto hl = K ({ slime.h - 14, slime.s + 6, juce::jmin (92.0f, slime.l + 24) }, 0.9f * a); const auto& b = R.band; const int nb = (int) b.size();
        if (! L.lite) for (float t : { 0.2f, 0.48f, 0.74f }) { const auto q = b[(size_t) std::floor (t * (float) (nb - 1))]; G.fill (blobp (q.x, q.y + s * 0.14f, s * 0.085f, s * 0.04f, 0.25f, 7, sd + t * 9), hl); }
    }
    if (L.gloss > 0) { const auto q = R.gloss; const float r = s * (0.10f + 0.16f * L.gloss) * (1 + 0.5f * R.g); G.fill (blobp (q.x, q.y, r, r * 0.62f, 0.2f, 9, sd), K ({ 52, 100, 97 }, 0.85f * a)); }
    const float tex = L.tex.value_or (0.5f);
    if (tex != 0)
    {
        const auto b = bbox (shp); const int o = boilOf (T, 4) % 3;
        G.clip (shp, [&]
        {
            int row = 0;
            for (float y = b[1] - (float) ((o * 31) % 40); y < b[3]; y += 120, ++row)
            { int col = 0; for (float x = b[0] - (float) ((o * 47) % 40) - (row % 2 ? 60.0f : 0.0f); x < b[2]; x += 120, ++col) { SpriteOpt so; so.a = tex * a; G.sprite ((row + col + o) % 2 ? "tex-bristle-b" : "tex-bristle", x, y, so); } }
        });
    }
    { OutlineOpt oo; oo.only = 0.05f; oo.wmin = 0.2f; oo.wmax = 1.3f; G.fillMany (outlineRibbons (shp, iw * 1.1f, sd, oo), K (dk, 0.62f * a)); }
    if (! L.lite) { OutlineOpt oo; oo.gap = 0.55f; G.fillMany (outlineRibbons (shp, iw * 0.45f, sd + 3, oo), K (dk, 0.3f * a)); }
    if (L.simple) return shp;
    for (auto& r : R.rolls) G.fill (ribbon (W_ (r), iw * 0.9f, sd + 2, 0.3f, 0.3f), K ({ shade.h, shade.s + 8, shade.l - 22 }, 0.75f * a));
    celFace (G, L.faceR ? *L.faceR : R, L, T, K (L.faceInk.value_or (HSL { 268, 35, 17 }), a), pupil, iw * 1.15f, sd, a, { 355, 80, 72 });
    return shp;
}

inline Poly ghostNeon (G& G, const Rig& R, const GL& L, const Clock&)
{
    const float s = R.s, a = L.a, heat = L.heat, k = L.k; const HSL col = L.tube.value_or (HSL { 322, 100, 60 }), col2 = L.slime.value_or (HSL { 140, 100, 56 }), col3 = L.face.value_or (HSL { 200, 90, 86 });
    const float w = juce::jmax (1.0f, s * 0.075f) * (1 + 0.9f * R.f * L.bulge) * L.iw; const Poly shp = wob (R.body, s * 0.012f, 0.9f / s, L.sd.value_or (5));
    const float glow = L.glow.value_or (0.6f);
    if (glow > 0) { SpriteOpt so; so.tint = K ({ col.h, col.s, lerp (col.l, 80, heat) }); so.a = glow * (0.6f + 0.4f * heat) * k * a; so.size = clampf (R.halfW / 30 * L.glowK, 0.5f, 3); G.sprite ("glow-soft", R.mid.x, R.mid.y, so); }
    G.fill (shp, K (pal::neonGlass, L.glass.value_or (0.5f) * a));
    if (L.holo) G.fill (shp, K (col, 0.16f * a));
    size_t ti = 0; for (size_t i = 1; i < shp.size(); ++i) if (shp[i].x < shp[ti].x) ti = i;
    const size_t n = shp.size(); Poly open; for (size_t j = 3; j + 2 < n; ++j) open.push_back (shp[(ti + j) % n]);
    if (! L.simple) for (auto& d : R.drips) { Poly p { R.X (d.x - 0.07f, -0.1f), R.X (d.x - 0.08f, -0.08f + d.len), R.X (d.x, d.len + 0.06f), R.X (d.x + 0.08f, -0.08f + d.len), R.X (d.x + 0.07f, -0.1f) }; TubeOpt to; to.k = k; to.a = a; to.heat = heat * 0.5f; tubeLine (G, smooth (p, false, 3), w * 0.8f, col2, to); }
    if (! R.band.empty() && ! L.simple) { TubeOpt to; to.k = k * 0.9f; to.a = a; to.halo = 0; tubeLine (G, R.band, w * 0.6f, col2, to); }
    { TubeOpt to; to.k = k; to.a = a; to.heat = heat; to.halo = L.simple ? 0.0f : 1.0f; tubeLine (G, open, w, col, to); }
    if (L.simple) return shp;
    for (auto& r : R.rolls) { TubeOpt to; to.k = k * 0.85f; to.a = a; to.halo = 0; tubeLine (G, r, w * 0.5f, { col.h + 30, col.s, col.l }, to); }
    auto face = [&] (const Rig& F) {
    const Rig& R = F; const std::string& em = L.eyes; const float fw = juce::jmax (0.9f, s * 0.06f);
    TubeOpt fo; fo.k = k; fo.a = a; fo.halo = 0;
    for (auto& e : R.eyes)
    {
        const auto q = R.X (e.x, e.y); const float rx = e.rx * s * 0.9f, ry = e.ry * s * 0.9f;
        if (em == "open" || em == "wide") { const float kk = em == "wide" ? 1.25f : 1.0f; TubeOpt to = fo; to.closed = true; to.halo = L.faceHalo; tubeLine (G, circ (q.x, q.y, rx * kk, ry * kk, 12), fw, col3, to); }
        else if (em == "x") { tubeLine (G, { { q.x - rx, q.y - ry * 0.7f }, { q.x + rx, q.y + ry * 0.7f } }, fw, col3, fo); tubeLine (G, { { q.x + rx, q.y - ry * 0.7f }, { q.x - rx, q.y + ry * 0.7f } }, fw, col3, fo); }
        else if (em == "squeeze") { const float d = e.x < R.mouth.x ? 1.0f : -1.0f; tubeLine (G, { { q.x - rx * d, q.y - ry * 0.6f }, { q.x + rx * 0.6f * d, q.y }, { q.x - rx * d, q.y + ry * 0.6f } }, fw, col3, fo); }
        else { const float dir = em == "happy" ? -1.0f : 1.0f; tubeLine (G, arcp (q.x, q.y + (dir < 0 ? ry * 0.3f : -ry * 0.1f), rx * 1.1f, ry * 0.6f, dir < 0 ? PI : 0.0f, dir < 0 ? TAU : PI, 8), fw, col3, fo); }
    }
    const auto m = mouthShape (R, L.mouth, L.mouthK, L.vowel);
    if (! m.line.empty()) tubeLine (G, m.line, fw, col3, fo);
    if (! m.fill.empty()) { TubeOpt to = fo; to.closed = true; tubeLine (G, m.fill, fw * 0.9f, col3, to); }
    const float blush = L.blush.value_or (0.5f);
    if (blush > 0) for (auto& c : R.cheeks) { const auto q = R.X (c.x, c.y); G.ell (q.x, q.y, c.rx * s * 1.1f, c.ry * s * 1.1f, K ({ 335, 100, 66 }, blush * 0.7f * a)); } };
    face (L.faceR ? *L.faceR : R);
    return shp;
}
inline Poly ghost (G& G, const Rig& R, const GL& L, const Clock& T)
{ return G.isCel() ? ghostCel (G, R, L, T) : G.isDawn() ? ghostDawn (G, R, L, T) : ghostNeon (G, R, L, T); }

// ---------------------------------------------------------------- a line, a solid form, a light
struct LineOpt { float a = 1, sd = 1; std::optional<float> ta, tb; float k = 1, halo = 0, heat = 0; bool closed = false; };
inline void line (G& G, const Poly& p, float w, HSL col, LineOpt o = {})
{
    if (G.isCel()) G.fill (ribbon (p, w, o.sd, o.ta.value_or (0.2f), o.tb.value_or (0.25f)), K (col, o.a));
    else if (G.isDawn()) G.fill (ribbon (p, w * 1.25f, o.sd, o.ta.value_or (0.3f), o.tb.value_or (0.35f)), K (col, o.a * 0.9f));
    else { TubeOpt to; to.k = o.k; to.a = o.a; to.halo = o.halo; to.heat = o.heat; to.closed = o.closed; tubeLine (G, p, juce::jmax (0.8f, w * 0.62f), col, to); }
}
struct Mat { HSL lit, shd; std::optional<HSL> ink, rim, tube; float glassA = 0.55f, fillA = 0.14f; };
struct SolidOpt { float a = 1; std::optional<float> iw, sd, shw, rimw; float lx = 0.75f, ly = 0.66f, gap = 0.08f; bool ink = true; float k = 1, halo = 0; };
inline void solid (G& G, const Poly& p, const Mat& m, SolidOpt o = {})
{
    const float a = o.a;
    if (G.isCel())
    {
        G.fill (p, K (m.shd, a)); LitOpt lo; lo.sx = o.lx; lo.sy = o.ly; G.fill (litShape (p, o.shw.value_or (3.2f), lo), K (m.lit, a));
        if (o.ink) { OutlineOpt oo; oo.gap = o.gap; G.fillMany (outlineRibbons (p, o.iw.value_or (1.5f), o.sd.value_or (2), oo), K (m.ink.value_or (INKC), a)); }
    }
    else if (G.isDawn())
    {
        G.fill (p, K (m.shd, a)); LitOpt lo; lo.pw = 0.8f; G.fill (litShape (p, o.shw.value_or (4), lo), K (m.lit, a));
        if (m.rim) { const auto r = rimShape (p, o.rimw.value_or (1.6f), -0.55f, -0.83f, 0.3f); if (! r.empty()) G.fill (r, K (*m.rim, 0.9f * a)); }
        if (o.ink) { OutlineOpt oo; oo.only = 0.1f; oo.wmin = 0.2f; oo.wmax = 1.2f; G.fillMany (outlineRibbons (p, o.iw.value_or (1.5f) * 0.9f, o.sd.value_or (2), oo), K (m.ink.value_or (pal::dawnInk), 0.5f * a)); }
    }
    else
    {
        const HSL c = m.tube.value_or (m.lit); G.fill (p, K (pal::neonGlass, m.glassA * a)); G.fill (p, K (c, m.fillA * a));
        TubeOpt to; to.k = o.k; to.a = a; to.closed = true; to.halo = o.halo; tubeLine (G, p, juce::jmax (0.8f, o.iw.value_or (1.5f) * 0.6f), c, to);
    }
}
inline void glint (G& G, float x, float y, float size, HSL col, float a = 1) { SpriteOpt so; so.size = size; so.tint = K (col); so.a = a; G.sprite ("glint", x, y, so); }
inline void glow (G& G, float x, float y, float size, HSL col, float a = 1, bool floor = false) { SpriteOpt so; so.size = size; so.tint = K (col); so.a = a; G.sprite (floor ? "glow-floor" : "glow-soft", x, y, so); }
inline void puff (G& G, float x, float y, int frame, float size, juce::Colour tint, float a) { SpriteOpt so; so.frame = frame; so.size = size; so.tint = tint; so.a = a; G.sprite ("puff", x, y, so); }

// ---------------------------------------------------------------- the stage under and over a scene
inline void stageUnder (G& G)
{
    if (G.isNeon()) { G.sprite ("_glass", 0, 0); G.sprite ("stage-shade", 0, 0); G.sprite ("stage-back", 0, 0); }
    else G.sprite ("backdrop", 0, 0);
}
inline void stageOver (G& G) { if (G.isNeon()) G.sprite ("stage-front", 0, 0); }

// ---------------------------------------------------------------- ukit.js: what the later units share
inline float mixA (float m) { return sstep (0, 0.12f, m) * (0.55f + 0.45f * m); }
struct Motion { bool on = true; int k = -1; HitPose hp; int idleB = 0; RigP P; float lvl = 0, low = 0, mid = 0, high = 0; };
struct MoOpt { float fat = 0; bool punch = false, noHit = false; int ph = 0; };
inline Motion gmo (const Feed& F, const State& S, const Clock& T, MoOpt o = {})
{
    Motion m; m.on = F.on; m.k = m.on ? T.tick - S.hitTick : -1; if (! o.noHit) m.hp = hitPose (m.k, o.fat, o.punch);
    m.idleB = m.on ? (int) std::floor ((float) (T.tick + o.ph) / 9.0f) % 2 : 0;
    m.P.sx = 1 + (m.idleB ? 0.012f : 0.0f); m.P.sy = 1 - (m.idleB ? 0.014f : 0.0f); m.P.hem = m.idleB ? 0.5f : 0.0f; m.P.lift = 0;
    if (m.hp.on) { m.P.sx = m.hp.sx; m.P.sy = m.hp.sy; m.P.lift = m.hp.lift; m.P.hem = m.hp.hem; if (m.hp.belly != 0) m.P.belly = m.hp.belly; }
    m.lvl = m.on ? F.level : 0; m.low = m.on ? F.lowOr() : 0; m.mid = m.on ? F.midOr() : 0; m.high = m.on ? F.highOr() : 0;
    return m;
}
inline void offVeil (G& G) { G.rect (0, 0, 280, 300, rgba (10, 8, 24, 0.45f)); }
inline void LN (G& G, const Poly& p, float w, HSL col, LineOpt o = {}) { line (G, p, w, col, o); }
inline Mat MAT (const std::string& n)
{
    if (n == "iron") return { { 244, 14, 42 }, { 252, 22, 22 }, {}, HSL { 226, 24, 66 }, HSL { 236, 60, 72 } };
    if (n == "steel") return { { 214, 16, 66 }, { 226, 22, 40 }, {}, HSL { 200, 40, 88 }, HSL { 200, 70, 76 } };
    if (n == "brass") return { { 42, 64, 58 }, { 26, 56, 34 }, {}, HSL { 50, 92, 82 }, HSL { 40, 100, 60 } };
    if (n == "wood") return { { 28, 42, 46 }, { 16, 40, 26 }, {}, HSL { 44, 70, 70 }, HSL { 28, 100, 58 } };
    if (n == "stone") return { { 210, 12, 60 }, { 236, 18, 36 }, {}, HSL { 48, 60, 84 }, HSL { 212, 50, 72 } };
    return { { 258, 24, 26 }, { 262, 30, 13 }, {}, HSL { 250, 30, 48 }, HSL { 262, 60, 60 } };   // dark
}
// stroke letters on a 4 x 6 grid
inline void glyph (G& G, char ch, float cx, float cy, float h, HSL col, float w = 1.4f, float a = 1, float halo = 0)
{
    static const std::map<char, std::vector<std::vector<V2>>> SF {
        { 'A', { { { 0, 6 }, { 2, 0 }, { 4, 6 } }, { { 0.95f, 3.9f }, { 3.05f, 3.9f } } } }, { 'E', { { { 4, 0 }, { 0, 0 }, { 0, 6 }, { 4, 6 } }, { { 0, 3 }, { 3, 3 } } } },
        { 'I', { { { 2, 0 }, { 2, 6 } }, { { 0.9f, 0 }, { 3.1f, 0 } }, { { 0.9f, 6 }, { 3.1f, 6 } } } },
        { 'O', { { { 1, 0 }, { 3, 0 }, { 4, 1 }, { 4, 5 }, { 3, 6 }, { 1, 6 }, { 0, 5 }, { 0, 1 }, { 1, 0 } } } }, { 'U', { { { 0, 0 }, { 0, 5 }, { 1, 6 }, { 3, 6 }, { 4, 5 }, { 4, 0 } } } },
        { 'L', { { { 0, 0 }, { 0, 6 }, { 4, 6 } } } }, { 'M', { { { 0, 6 }, { 0, 0 }, { 2, 3.6f }, { 4, 0 }, { 4, 6 } } } }, { 'H', { { { 0, 0 }, { 0, 6 } }, { { 4, 0 }, { 4, 6 } }, { { 0, 3 }, { 4, 3 } } } } };
    auto it = SF.find (ch); if (it == SF.end()) return; const float wd = h * 0.62f; std::vector<Poly> ps;
    for (auto& st : it->second) { Poly p; for (auto& q : st) p.push_back ({ cx + (q.x / 4 - 0.5f) * wd, cy + (q.y / 6 - 0.5f) * h }); ps.push_back (p); }
    if (G.isNeon()) { for (auto& p : ps) { TubeOpt to; to.a = a; to.halo = halo; tubeLine (G, p, juce::jmax (0.8f, w * 0.62f), col, to); } return; }
    G.strokeMany (ps, w, K (col, a));
}
// what the tile ghosts wear: crown, halo, horns, headphones
struct WearOpt { float dx = 0, a = 1; bool lite = false; std::optional<HSL> col; };
inline void wear (G& G, const Rig& R, const std::string& what, const Clock&, WearOpt o = {})
{
    const float s = R.s, tx = R.top.x + o.dx, ty = R.top.y, a = o.a;
    if (what == "crown")
    {
        const float w = s * 0.36f, h = s * 0.3f, y = ty + s * 0.05f; Poly p { { tx - w, y }, { tx - w * 1.08f, y - h * 0.9f }, { tx - w * 0.5f, y - h * 0.45f }, { tx, y - h * 1.15f }, { tx + w * 0.5f, y - h * 0.45f }, { tx + w * 1.08f, y - h * 0.9f }, { tx + w, y } };
        if (G.isNeon()) { TubeOpt to; to.a = a; to.halo = 0; tubeLine (G, concat (p, { p[0] }), juce::jmax (0.8f, s * 0.04f), { 46, 100, 62 }, to); }
        else { SolidOpt so; so.iw = juce::jmax (0.8f, s * 0.045f); so.sd = 4; so.shw = s * 0.05f; so.a = a; solid (G, p, { { 46, 92, 64 }, { 34, 84, 44 }, {}, HSL { 52, 100, 88 } }, so); for (int i : { 1, 3, 5 }) G.ell (p[(size_t) i].x, p[(size_t) i].y, s * 0.035f, s * 0.035f, K ({ 350, 80, 66 }, a)); }
    }
    if (what == "halo")
    {
        const float y = ty - s * 0.2f, rx = s * 0.4f, ry = s * 0.11f; const auto p = circ (tx, y, rx, ry, 14);
        if (G.isNeon()) { TubeOpt to; to.a = a; to.closed = true; to.halo = 1; tubeLine (G, p, juce::jmax (0.8f, s * 0.04f), { 48, 100, 66 }, to); }
        else
        {
            if (G.isDawn()) { SpriteOpt so; so.tint = K ({ 48, 100, 80 }); so.a = 0.5f * a; so.size = clampf (s / 60, 0.5f, 1); G.sprite ("glow-soft", tx, y, so); }
            StrokeOpt c; c.closed = true; G.stroke (p, juce::jmax (1.4f, s * 0.075f), K (G.isDawn() ? HSL { 38, 70, 40 } : INKC, a), c); G.stroke (p, juce::jmax (0.8f, s * 0.04f), K ({ 50, 100, 74 }, a), c);
        }
    }
    if (what == "horns" && o.lite)
    {
        std::vector<Poly> hp; for (float sd : { -1.0f, 1.0f }) { const float bx = tx + sd * s * 0.36f, by = ty + s * 0.2f; hp.push_back ({ { bx - sd * s * 0.1f, by }, { bx + sd * s * 0.1f, by - s * 0.3f }, { bx + sd * s * 0.13f, by + s * 0.02f } }); }
        if (G.isNeon()) { G.strokeMany (hp, juce::jmax (1.0f, s * 0.05f), K ({ 350, 100, 62 }, a)); G.strokeMany (hp, juce::jmax (0.5f, s * 0.022f), K ({ 350, 80, 90 }, a)); }
        else { G.fillMany (hp, K ({ 352, 74, 54 }, a)); std::vector<Poly> rs; for (auto& q : hp) rs.push_back (ribbon (concat (q, { q[0] }), juce::jmax (0.8f, s * 0.05f), 7, 0.05f, 0.05f)); G.fillMany (rs, K (G.isDawn() ? HSL { 340, 60, 26 } : INKC, a)); }
    }
    if (what == "horns" && ! o.lite)
        for (float sd : { -1.0f, 1.0f })
        {
            const float bx = tx + sd * s * 0.36f, by = ty + s * 0.2f; Poly p { { bx - sd * s * 0.1f, by }, { bx + sd * s * 0.1f, by - s * 0.3f }, { bx + sd * s * 0.13f, by + s * 0.02f } };
            if (G.isNeon()) { TubeOpt to; to.a = a; to.halo = 0; tubeLine (G, p, juce::jmax (0.8f, s * 0.04f), { 350, 100, 62 }, to); }
            else { SolidOpt so; so.iw = juce::jmax (0.8f, s * 0.045f); so.sd = 5 + sd; so.shw = s * 0.04f; so.a = a; solid (G, p, { { 352, 74, 56 }, { 340, 70, 36 }, {}, HSL { 20, 90, 78 } }, so); }
        }
    if (what == "phones")
    {
        const float ey = R.X (R.eyes[0].x, R.eyes[0].y).y + s * 0.05f, cx = R.X (R.cAt (0.32f), R.yAt (0.32f)).x, hw = R.wAt (0.32f, 1) * s * R.sx * 1.03f, ry = juce::jmax (s * 0.3f, ey - (ty - s * 0.07f));
        const auto band = arcp (cx, ey, hw, ry, PI, TAU, 10); const HSL col = o.col.value_or (HSL { 262, 60, 58 });
        if (G.isNeon()) { TubeOpt to; to.a = a; to.halo = 0; tubeLine (G, band, juce::jmax (0.8f, s * 0.045f), col, to); }
        else { G.stroke (band, juce::jmax (1.6f, s * 0.09f), K (G.isDawn() ? HSL { 262, 32, 22 } : INKC, a)); G.stroke (band, juce::jmax (0.8f, s * 0.045f), K ({ 250, 16, 46 }, a)); }
        for (float sd : { -1.0f, 1.0f })
        {
            const auto cup = circ (cx + sd * hw, ey, s * 0.12f, s * 0.19f, 10);
            if (G.isNeon()) { TubeOpt to; to.a = a; to.closed = true; to.halo = 0; tubeLine (G, cup, juce::jmax (0.8f, s * 0.045f), col, to); }
            else { SolidOpt so; so.iw = juce::jmax (0.8f, s * 0.045f); so.sd = 6 + sd; so.shw = s * 0.04f; so.a = a; solid (G, cup, { col, { col.h, col.s, col.l - 24 }, {}, HSL { col.h, 80, 84 } }, so); }
        }
    }
}
inline float lfoShape (int i, float ph, float salt = 0)
{
    const float p = std::fmod (std::fmod (ph, 1.0f) + 1.0f, 1.0f);
    return i == 1 ? 1 - 4 * std::abs (p - 0.5f) : i == 2 ? 2 * p - 1 : i == 3 ? (p < 0.5f ? 1.0f : -1.0f) : i == 4 ? hsh (std::floor (ph), salt) * 2 - 1 : i == 5 ? 1 - 2 * std::pow (p, 0.5f) : std::sin (p * TAU);
}
inline const float DIVB[16] { 0.0625f, 0.125f, 1.0f / 6, 0.25f, 0.375f, 1.0f / 3, 0.5f, 0.75f, 2.0f / 3, 1, 1.5f, 4.0f / 3, 2, 3, 4, 8 };
struct CrumbOpt { float sp = 6, r = 1.8f, a0 = 0, a1 = TAU, d0 = 4, ky = 1, g = 0.5f; };
template <typename ColFn> void crumbs (G& G, float x, float y, int n, float salt, float age, ColFn col, CrumbOpt o = {})
{
    for (int i = 0; i < n; ++i)
    {
        const float a = o.a0 + hsh (i, salt) * (o.a1 - o.a0), d = o.d0 + age * o.sp * (0.5f + hsh (i, salt + 7)), px = x + std::cos (a) * d, py = y + std::sin (a) * d * o.ky + age * age * o.g, r = o.r * (0.6f + hsh (i, salt + 3)), rot = hsh (i, salt + 5) * 6;
        G.fill ({ { px + std::cos (rot) * r, py + std::sin (rot) * r }, { px + std::cos (rot + 2.2f) * r, py + std::sin (rot + 2.2f) * r }, { px + std::cos (rot + 4.3f) * r * 0.8f, py + std::sin (rot + 4.3f) * r * 0.8f } }, col (i));
    }
}
// a ring / arc made of light: a wide faint stroke under a bright one (a tube in NEON)
struct LightOpt { bool closed = false; float halo = 0, k = 1; };
inline void lightLine (G& G, const Poly& pts, float w, HSL col, float a = 1, LightOpt o = {})
{
    if (a < 0.02f) return;
    if (G.isNeon()) { TubeOpt to; to.a = a; to.closed = o.closed; to.halo = o.halo; to.k = o.k; tubeLine (G, pts, juce::jmax (0.7f, w * 0.6f), col, to); return; }
    StrokeOpt so; so.closed = o.closed; G.stroke (pts, w * 3.2f, K (col, a * 0.22f), so); G.stroke (pts, w, K ({ col.h, col.s, juce::jmin (97.0f, col.l + 10) }, a), so);
}
inline HSL heatC (float d) { return d < 0.5f ? mixrgb ({ 4, 84, 36 }, { 22, 100, 54 }, d * 2) : mixrgb ({ 22, 100, 54 }, { 48, 100, 78 }, (d - 0.5f) * 2); }

// ---------------------------------------------------------------- a unit's painted scene
class Scene
{
public:
    virtual ~Scene() = default;
    virtual std::vector<std::string> controls() const = 0;          // the unit's control ids the picture reads (menus as their index, switches 0 / 1)
    virtual std::vector<float> defaults() const = 0;                // the designer's defaults (for checks and comparisons)
    // a knob whose scene value is not its 0..1 position (FORMANT's vowel is 0..4 = A..U): the factor the position is multiplied by
    virtual float scaleOf (const std::string&) const { return 1.0f; }
    virtual void scene (G&, const Feed&, State&, const Clock&) = 0;
    // DAWN's wide pages have no room for the stage: a unit may bring a second picture there, in its own rectangle (design px,
    // 1 su = 1 px from its top-left): drawn BEHIND its editor ("under", light only) or as a small pane of its own
    virtual std::vector<std::string> dawnControls() const { return controls(); }   // what the DAWN picture shows (the editor in front shows the rest)
    virtual bool hasDawn() const { return false; }
    virtual juce::Rectangle<float> dawnRect() const { return {}; }
    virtual bool dawnUnder() const { return true; }
    virtual bool dawnOver() const { return false; }   // (an "under" picture laid over the editor instead: light only, takes no clicks; GRID, whose cells are solid)
    virtual void dawnScene (G&, const Feed&, State&, const Clock&, float /*W*/, float /*H*/) {}
};
} // namespace phantom::pv
