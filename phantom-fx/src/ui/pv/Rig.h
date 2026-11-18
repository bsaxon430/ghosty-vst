// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX painted unit visuals: the ghost as a rig (port of phantom-anime/visuals/src/rig.js). Numbers in, polygons out:
// dome head, scalloped hem, tail flick to the left, slime hem with drips, eyes set to the right; redrawn fat, pressed flat,
// pinched, torn or snapped to a grid. Local units = s; origin = where the hem touches the floor, y up is negative.
#pragma once
#include "Kit.h"

namespace phantom::pv
{
struct RigP
{
    float s = 44, fat = 0, sx = 1, sy = 1, lean = 0, lift = 0, tail = 0, hem = 0, belly = 0, sd = 3, x = 140, y = 232;
    bool hasStrings = false; std::vector<float> strings;
    float flat = 0, pinch = 0; int seg = 5; float rough = 0, rsd = 0, bite = 0, block = 0;
    bool hasDrips = false; std::vector<std::pair<float, float>> drips; float dripK = 1;
    bool hasLook = false; V2 look;
};
struct Drip { float x = 0, len = 0; Poly p, hi; };
struct Eye { float x, y, rx, ry; };

struct Rig
{
    RigP P; float s = 44, f = 0, g = 0, H = 2, sx = 1, sy = 1, um = 0.39f, xr = 0, xl = 0, halfW = 0;
    Poly body, slime, band, loc, under; std::vector<Drip> drips; std::vector<Eye> eyes, cheeks; V2 mouth; std::vector<Poly> rolls;
    V2 mid, top, gloss; std::vector<float> downs;
    // the body's shape functions
    float wmax = 0.79f, qt = 2, kb = 0.07f, hemH = 0.2f;
    bool flipped = false; float flipCx = 0;   // seen from the other side (flipRig): mirrored about flipCx
    V2 X (float x, float y) const { const V2 v { P.x + (x * P.sx - P.lean * y) * P.s, P.y + (y * P.sy - P.lift) * P.s }; return flipped ? V2 { 2 * flipCx - v.x, v.y } : v; }
    float wBase (float u) const { return u <= um ? wmax * std::pow (juce::jmax (0.0f, 1 - std::pow (1 - u / um, 2.0f)), 1 / qt) : wmax * (1 - kb * std::pow ((u - um) / (1 - um), 2.2f)); }
    float tie (float u) const { float k = 1; for (float us : P.strings) k -= 0.15f * std::exp (-std::pow ((u - us) / 0.028f, 2.0f)); return k + (P.strings.empty() ? 0.0f : 0.05f); }
    float wAt (float u, float side) const { return wBase (u) * (side > 0 ? 1 + 0.06f * g : 1 - 0.04f * g) * (P.strings.empty() ? 1.0f : tie (u)); }
    float bell (float u) const { return std::exp (-std::pow ((u - 0.7f) / 0.26f, 2.0f)); }
    float cAt (float u) const { return (0.10f * g + P.belly) * bell (u); }
    float yAt (float u) const { return -H + u * (H - hemH); }
    // a line across the front of the body at height u (it sags like a belt)
    Poly across (float u, float sag = 0.06f, int n = 12, float x0 = 0, float x1 = 1) const
    {
        const float y = yAt (u), wl = wAt (u, -1), wr = wAt (u, 1), c = cAt (u); Poly p;
        for (int i = 0; i <= n; ++i) { const float t = x0 + (x1 - x0) * (float) i / (float) n; p.push_back (X (c - wl + (wl + wr) * t, y + sag * std::sin (t * PI))); }
        return p;
    }
    Poly roll (float u, float sag, float x0, float x1) const
    {
        const float y = yAt (u), wl = wAt (u, -1), wr = wAt (u, 1), c = cAt (u); Poly p;
        for (int i = 0; i <= 10; ++i) { const float t = (float) i / 10, x = c - wl + (wl + wr) * (x0 + (x1 - x0) * t); p.push_back (X (x, y + sag * std::sin (t * PI) - sag * 0.5f)); }
        return p;
    }
};

inline Rig ghostRig (const RigP& Pin)
{
    Rig R; R.P = Pin; const auto& P = R.P;
    const float s = P.s, f = clampf (P.fat, 0, 1), g = std::pow (f, 0.9f), H = 2.0f + 1.4f * std::pow (f, 1.1f), tl = P.tail, hemF = P.hem, sd = P.sd;
    R.s = s; R.f = f; R.g = g; R.H = H; R.sx = P.sx; R.sy = P.sy;
    R.um = lerp (0.39f, 0.70f, g); R.wmax = lerp (0.79f, 2.2f, g); R.qt = lerp (2, 1.5f, g); R.kb = lerp (0.07f, 0.24f, g); R.hemH = 0.20f - 0.08f * g;
    const int N = P.hasStrings ? 34 : 20; Poly raw;
    for (int i = 0; i <= N; ++i) { const float u = std::pow ((float) i / (float) N, 0.9f); raw.push_back ({ R.cAt (u) + R.wAt (u, 1), R.yAt (u) }); }
    const float xr = R.cAt (1) + R.wAt (1, 1), xl = R.cAt (1) - R.wAt (1, -1), span = xr - xl - 0.14f, amp = 0.21f - 0.11f * g; const int nl = 3 + juce::roundToInt (2 * g);
    R.xr = xr; R.xl = xl;
    for (int i = 0; i < nl; ++i)
    {
        const float xd = xr - 0.05f - ((float) i + 0.42f) / (float) nl * span, xu = xr - 0.05f - ((float) i + 0.86f) / (float) nl * span, j = (i % 2) ? -1.0f : 1.0f;
        raw.push_back ({ xd, 0 + hemF * 0.05f * j + (i == 1 ? 0.01f : -0.02f) }); R.downs.push_back (xd);
        if (i < nl - 1) raw.push_back ({ xu, -amp + 0.02f * j - hemF * 0.03f });
    }
    raw.push_back ({ xl + 0.2f, -amp - 0.01f });
    raw.push_back ({ xl - 0.06f, -0.13f + tl * 0.12f }); raw.push_back ({ xl - 0.36f - tl * 0.12f, -0.26f - tl * 0.3f }); raw.push_back ({ xl - 0.12f, -0.40f - tl * 0.06f });
    const float uTail = clampf (1 - (0.70f - R.hemH) / (H - R.hemH), 0.5f, 0.95f);
    for (int i = N; i >= 1; --i) { const float u = std::pow ((float) i / (float) N, 0.9f); if (u > uTail) continue; raw.push_back ({ R.cAt (u) - R.wAt (u, -1), R.yAt (u) }); }
    const float flat = clampf (P.flat, 0, 1);
    if (flat > 0) { const float yc = -H * (1 - 0.42f * flat); for (auto& q : raw) { if (q.y < yc) q.y = yc + (q.y - yc) * 0.10f; const float u = clampf ((q.y + H) / H, 0, 1); q.x *= 1 + flat * 0.26f * (1 - std::abs (u - 0.45f)); } }
    const float pinch = clampf (P.pinch, 0, 1);
    if (pinch > 0) for (auto& q : raw) { q.x *= 1 - pinch * 0.45f; q.y *= 1 + pinch * 0.22f; }
    Poly loc = smooth (raw, true, P.seg);
    const float rough = clampf (P.rough, 0, 1), bite = P.bite;
    if (rough > 0.004f)
    {
        const int n = (int) loc.size(); const float rs = P.rsd; Poly o; const auto Nn = normals (loc); int nxt = 1, j = 0;
        for (int i = 0; i < n; ++i)
        {
            const V2 q = loc[(size_t) i]; float am = 0;
            if (i == nxt)
            {
                const float h = hsh (j, rs), h2_ = hsh (j, rs + 9); am = rough * 0.13f * (0.2f + 0.8f * h * h); if (h2_ < 0.22f) am = -rough * 0.05f;
                if (bite > 0 && hsh (j, rs + 33) < bite * 0.2f) am = -0.12f - 0.08f * h;
                nxt = i + 2 + (int) std::floor (hsh (j, rs + 17) * 4.2f); ++j;
            }
            o.push_back ({ q.x + Nn[(size_t) i].x * am, q.y + Nn[(size_t) i].y * am });
        }
        loc = o;
    }
    const float block = clampf (P.block, 0, 1);
    if (block > 0.02f)
    {
        const float b = 0.06f + block * 0.26f; Poly o; bool has = false; V2 last;
        for (auto& q : loc)
        {
            const float x = std::round (q.x / b) * b, y = std::round (q.y / b) * b;
            if (has && last.x == x && last.y == y) continue;
            if (has && last.x != x && last.y != y) o.push_back ({ x, last.y });
            last = { x, y }; has = true; o.push_back (last);
        }
        loc = o;
    }
    R.loc = loc; for (auto& q : loc) R.body.push_back (R.X (q.x, q.y));
    // slime band: everything under a wavy line near the hem
    const float bandH = 0.53f - 0.20f * g;
    auto bandY = [&] (float x) { return -bandH + 0.07f * (1 - 0.3f * g) * std::sin (x * (8 - 3.4f * g) + sd) + (x < xl + 0.12f ? 0.10f : 0.0f); };
    const int n = (int) loc.size(); std::vector<bool> below ((size_t) n); for (int i = 0; i < n; ++i) below[(size_t) i] = loc[(size_t) i].y > bandY (loc[(size_t) i].x);
    int st = -1; for (int i = 0; i < n; ++i) if (below[(size_t) i] && ! below[(size_t) ((i - 1 + n) % n)]) { st = i; break; }
    if (st >= 0)
    {
        Poly run; for (int kk = 0; kk < n; ++kk) { const int i = (st + kk) % n; if (! below[(size_t) i]) break; run.push_back (loc[(size_t) i]); }
        if (run.size() > 3)
        {
            const float xa = run[0].x, xb = run.back().x; Poly bl; for (int i = 0; i <= 14; ++i) { const float x = xb + (xa - xb) * (float) i / 14; bl.push_back ({ x, bandY (x) }); }
            for (auto& q : run) R.slime.push_back (R.X (q.x, q.y)); for (auto& q : bl) { R.slime.push_back (R.X (q.x, q.y)); R.band.push_back (R.X (q.x, q.y)); }
        }
    }
    // drips hang from the hem's low points
    std::vector<std::pair<float, float>> dripDef;
    if (P.hasDrips) dripDef = P.drips;
    else if (nl == 3) dripDef = { { R.downs[2], 0.34f }, { R.downs[1], 0.5f }, { R.downs[0], 0.26f } };
    else { static const float L5[5] { 0.3f, 0.46f, 0.24f, 0.4f, 0.2f }; for (size_t i = 0; i < R.downs.size(); ++i) dripDef.push_back ({ R.downs[i], L5[i % 5] }); }
    const float dw = 0.085f * (1 + 0.6f * g), dk = 1 - 0.45f * g;
    for (auto [dx, dl] : dripDef)
    {
        dl *= dk * P.dripK; Drip d; d.x = dx; d.len = dl;
        for (auto& q : smooth ({ { dx - dw, -0.2f }, { dx - dw * 0.9f, -0.1f + dl * 0.6f }, { dx - dw * 1.15f, -0.1f + dl }, { dx, -0.02f + dl + 0.08f }, { dx + dw * 1.15f, -0.1f + dl }, { dx + dw * 0.9f, -0.1f + dl * 0.6f }, { dx + dw, -0.2f } }, true, 4)) d.p.push_back (R.X (q.x, q.y));
        for (auto& q : smooth ({ { dx + 0.01f, -0.15f }, { dx + dw, -0.1f + dl * 0.6f }, { dx + dw * 1.1f, -0.1f + dl }, { dx, -0.02f + dl + 0.07f }, { dx + 0.03f, -0.1f + dl * 0.7f } }, true, 4)) d.hi.push_back (R.X (q.x, q.y));
        R.drips.push_back (std::move (d));
    }
    // face: eyes set to the right, cheeks, mouth
    const float fx = 0.30f + 0.10f * g + R.cAt (0.2f) * 0.5f + (P.hasLook ? P.look.x : 0.0f), fy = -H + 0.73f + 0.06f * g + (P.hasLook ? P.look.y : 0.0f), es = 1 - 0.12f * f, ed = 0.2f * (1 + 0.25f * g);
    R.eyes = { { fx - ed, fy, 0.115f * es, 0.18f * es }, { fx + ed, fy, 0.10f * es, 0.165f * es } };
    R.cheeks = { { fx - 0.40f * (1 + 0.5f * g), fy + 0.23f + 0.25f * g, 0.085f * (1 + g), 0.045f * (1 + 0.8f * g) }, { fx + 0.38f * (1 + 0.5f * g), fy + 0.23f + 0.25f * g, 0.085f * (1 + g), 0.045f * (1 + 0.8f * g) } };
    R.mouth = { fx, fy + 0.27f + 0.06f * g };
    // belly rolls
    const float r1 = sstep (0.32f, 0.6f, f), r2 = sstep (0.68f, 0.92f, f);
    if (r1 > 0.3f) { auto up = R.roll (0.83f, 0.07f + 0.07f * g, 0.5f - 0.2f * r1, 0.5f + 0.27f * r1); R.rolls.push_back (up); R.under = concat (up, reversed (R.roll (0.955f, 0.0f, 0.5f - 0.26f * r1, 0.5f + 0.34f * r1))); }
    if (r2 > 0.3f) R.rolls.push_back (R.roll (0.925f, 0.05f, 0.2f, 0.2f + 0.3f * r2));
    if (f > 0.5f)
    {
        const float k = sstep (0.5f, 0.8f, f);
        for (float sd_ : { -1.0f, 1.0f }) { const float u = 0.9f, w = R.wAt (u, sd_), c = R.cAt (u), y = R.yAt (u); R.rolls.push_back ({ R.X (c + sd_ * w * 0.995f, y - 0.2f * k), R.X (c + sd_ * (w - 0.16f * k), y - 0.04f), R.X (c + sd_ * (w - 0.34f * k), y - 0.03f * k) }); }
    }
    float ty = 1e9f, tx = 0; for (auto& q : R.body) if (q.y < ty) { ty = q.y; tx = q.x; }
    const float umid = 0.42f + 0.2f * g; R.mid = R.X (R.cAt (umid), R.yAt (umid)); R.halfW = R.wAt (R.um, 1) * s * P.sx;
    R.top = { lerp (R.X (R.cAt (0), -H).x, tx, 0.5f), ty }; R.gloss = R.X (R.cAt (0.6f) - R.wAt (0.6f, -1) * 0.45f, R.yAt (0.52f));
    return R;
}

// the mouth: kind smile / grin / o / flat, or a vowel blend 0..4 (A E I O U) when vowel >= 0
struct Mouth { Poly line, fill; bool hasTongue = false; float tx = 0, ty = 0, trx = 0, try_ = 0; bool hasTeeth = false; Poly teeth; };
inline Mouth mouthShape (const Rig& R, const std::string& kind, float size = 1, float vowel = -1)
{
    const V2 m = R.mouth; const float k = size * (1 + 0.25f * R.g); Mouth o;
    auto X = [&] (float x, float y) { return R.X (x, y); };
    if (vowel < 0)
    {
        if (kind == "smile") { Poly p; for (int i = 0; i <= 6; ++i) { const float a = 0.25f + (float) i / 6 * (PI - 0.5f); p.push_back (X (m.x + std::cos (a) * 0.1f * k, m.y - 0.04f + std::sin (a) * 0.07f * k)); } o.line = smooth (p, false, 3); return o; }
        if (kind == "flat") { o.line = { X (m.x - 0.09f * k, m.y), X (m.x + 0.09f * k, m.y + 0.01f) }; return o; }
        if (kind == "grin")
        {
            o.fill = smooth ({ X (m.x - 0.16f * k, m.y - 0.04f), X (m.x, m.y - 0.02f), X (m.x + 0.16f * k, m.y - 0.05f), X (m.x + 0.1f * k, m.y + 0.12f * k), X (m.x, m.y + 0.17f * k), X (m.x - 0.1f * k, m.y + 0.12f * k) }, true, 4);
            o.hasTongue = true; o.tx = m.x + 0.01f; o.ty = m.y + 0.11f * k; o.trx = 0.07f * k; o.try_ = 0.045f * k; return o;
        }
        if (kind == "o") { const auto c = X (m.x, m.y + 0.03f); o.fill = circ (c.x, c.y, 0.07f * k * R.s * R.sx, 0.10f * k * R.s * R.sy, 12); o.hasTongue = true; o.tx = m.x; o.ty = m.y + 0.08f * k; o.trx = 0.04f * k; o.try_ = 0.04f * k; return o; }
        vowel = 0;
    }
    static const float V[5][3] { { 0.17f, 0.19f, 0 }, { 0.20f, 0.075f, 0.03f }, { 0.13f, 0.05f, 0.05f }, { 0.115f, 0.15f, 0 }, { 0.065f, 0.085f, 0 } };
    const float v = clampf (vowel, 0, 4); const int i0 = (int) std::floor (juce::jmin (v, 3.999f)); const float t = v - (float) i0;
    const float* a = V[i0]; const float* b = V[i0 + 1];
    const float hw = lerp (a[0], b[0], t) * k, hh = lerp (a[1], b[1], t) * k, cl = lerp (a[2], b[2], t) * k; Poly p;
    for (int i = 0; i < 14; ++i) { const float an = (float) i / 14 * TAU, cx_ = std::cos (an), sy_ = std::sin (an); p.push_back (X (m.x + cx_ * hw, m.y + 0.04f + sy_ * hh - std::abs (cx_) * cl * (sy_ < 0 ? 1.6f : 0.4f))); }
    o.fill = smooth (p, true, 3);
    if (hh > 0.09f * k) { o.hasTongue = true; o.tx = m.x; o.ty = m.y + 0.04f + hh * 0.55f; o.trx = hw * 0.6f; o.try_ = hh * 0.4f; }
    if (hh < 0.09f * k && hw > 0.11f * k) { o.hasTeeth = true; o.teeth = { X (m.x - hw * 0.8f, m.y + 0.04f - hh * 0.25f), X (m.x + hw * 0.8f, m.y + 0.04f - hh * 0.25f) }; }
    return o;
}
// hit poses on the 12-a-second clock: k = ticks since the hit (-1 = none)
// a rig seen from the other side (REVERSE in GRID and HAUNT; ukit.js flipRig): every outline mirrored about the ghost's own x; the face follows through X
inline Rig flipRig (const Rig& R0)
{
    Rig R = R0; const float cx = R0.P.x; auto f = [cx] (V2 q) { return V2 { 2 * cx - q.x, q.y }; }; auto fa = [&] (Poly& p) { for (auto& q : p) q = f (q); };
    fa (R.body); fa (R.slime); fa (R.band); for (auto& d : R.drips) { fa (d.p); fa (d.hi); } for (auto& r : R.rolls) fa (r); fa (R.under);
    R.mid = f (R.mid); R.top = f (R.top); R.gloss = f (R.gloss);
    if (R0.flipped && std::abs (R0.flipCx - cx) < 1e-6f) R.flipped = false; else { R.flipped = true; R.flipCx = cx; }
    return R;
}
struct HitPose { bool on = false; float sx = 1, sy = 1, lift = 0, hem = 0, belly = 0; std::string eyes, ph; };
inline HitPose hitPose (int k, float fat = 0, bool punch = false)
{
    HitPose h; if (k < 0) return h; const float d = fat; h.on = true;
    if (k < 2) { h.sx = 1.10f + 0.10f * d; h.sy = 0.86f - 0.08f * d; h.hem = 1; h.eyes = "squeeze"; h.ph = "squat"; return h; }
    if (k < 5) { h.sx = 0.93f - 0.02f * d; h.sy = (punch ? 1.16f : 1.10f) + 0.03f * d; h.lift = punch ? 0.16f : 0.10f; h.hem = -1; h.eyes = "wide"; h.ph = "up"; return h; }
    if (k < 7) { h.sx = 1.03f + 0.05f * d; h.sy = 0.97f - 0.03f * d; h.hem = 0.5f; h.ph = "settle"; return h; }
    const int n = juce::roundToInt (4 + 8 * d), j = k - 7;
    if (j < n) { const float a = (0.05f + 0.07f * d) * std::pow (0.5f, std::floor ((float) j / 2)), sg = ((j / 2) % 2) ? -1.0f : 1.0f; h.sx = 1 + a * sg * 0.5f; h.sy = 1 - a * sg * 0.4f; h.belly = a * sg * 1.6f; h.ph = "wobble"; return h; }
    h.on = false; return h;
}
} // namespace phantom::pv
