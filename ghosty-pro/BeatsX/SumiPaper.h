#pragma once
// LIGHT look (Ghosty Beats, owner 2026-11-13): sumi-e ink + watercolour on washi paper, in the spirit of a hand-drawn Japanese scroll.
// Warm cream rice paper with fibres and mottling; rough, expressive charcoal / black-ink brush strokes; muted vermilion, indigo, moss green and
// pale cherry-blossom pink bleeding softly like watercolour on dry paper. Serene, but raw: on every drum hit the header's ink trembles and the
// falling petals swirl harder (a nod to the running scene, where calm lines turn into frantic scribbles).
// Everything is painted (no images, no text). The paper is rendered once per window size and cached; the petals are drawn live in the header.
#include <juce_graphics/juce_graphics.h>
#include <cmath>
#include <vector>

namespace bx::sumi {
using C = juce::Colour; using P = juce::Point<float>;

inline C paper()     { return C(0xffD9CFBB); }   // warm washi parchment, toned down (never bright white)
inline C paperHi()   { return C(0xffE4DCCB); }
inline C ink()       { return C(0xff1C1B1A); }   // charcoal ink
inline C vermilion() { return C(0xffC84B31); }
inline C indigo()    { return C(0xff34507F); }
inline C moss()      { return C(0xff6E8B4E); }
inline C blossom()   { return C(0xffE8B4B8); }
inline C ochre()     { return C(0xffD9B26A); }

// smooth 1-D noise from a few sines with random phases (stable for a given Random seed)
struct Wobble {
    float ph[4], fr[4];
    explicit Wobble(juce::Random& r) { for (int i = 0; i < 4; ++i) { ph[i] = r.nextFloat() * 6.283f; fr[i] = 1.0f + (float) i * 1.7f + r.nextFloat(); } }
    float at(float t) const { float s = 0; for (int i = 0; i < 4; ++i) s += std::sin(t * fr[i] * 3.1f + ph[i]) / (float) (i + 1); return s * 0.48f; }   // ~ -1..1
};

// a bent line a -> b (quadratic bow + hand tremble), n points
inline std::vector<P> stroke(P a, P b, float bow, int n, float tremble, juce::Random& r) {
    std::vector<P> v; v.reserve((size_t) n); Wobble w(r);
    const P d = b - a; const float len = juce::jmax(1.0f, d.getDistanceFromOrigin()); const P nrm(-d.y / len, d.x / len);
    for (int i = 0; i < n; ++i) { const float t = (float) i / (float) (n - 1); const float off = bow * 4.0f * t * (1 - t) + tremble * w.at(t * len * 0.02f);
        v.push_back(a + d * t + nrm * off); }
    return v;
}

// a brush stroke along a polyline: pressure rises, wavers and lifts off (filled outline, both edges ragged)
inline juce::Path brush(const std::vector<P>& pts, float width, juce::Random& r, float taperIn = 0.25f, float taperOut = 0.4f) {
    juce::Path p; const int n = (int) pts.size(); if (n < 2) return p;
    Wobble wl(r), wr(r); std::vector<P> L, R; L.reserve((size_t) n); R.reserve((size_t) n);
    for (int i = 0; i < n; ++i) {
        const float t = (float) i / (float) (n - 1);
        const P a = pts[(size_t) juce::jmax(0, i - 1)], b = pts[(size_t) juce::jmin(n - 1, i + 1)]; P d = b - a; const float l = juce::jmax(0.001f, d.getDistanceFromOrigin()); d = d / l;
        const P nrm(-d.y, d.x);
        const float in = taperIn > 0 ? juce::jmin(1.0f, t / taperIn) : 1.0f, out = taperOut > 0 ? juce::jmin(1.0f, (1 - t) / taperOut) : 1.0f;
        const float env = 0.12f + 0.88f * std::sqrt(juce::jmax(0.0f, in)) * std::sqrt(juce::jmax(0.0f, out));
        const float hw = width * 0.5f * env;
        L.push_back(pts[(size_t) i] + nrm * hw * (1.0f + 0.22f * wl.at(t * 9.0f)));
        R.push_back(pts[(size_t) i] - nrm * hw * (1.0f + 0.22f * wr.at(t * 9.0f)));
    }
    p.startNewSubPath(L[0]); for (int i = 1; i < n; ++i) p.lineTo(L[(size_t) i]);
    for (int i = n - 1; i >= 0; --i) p.lineTo(R[(size_t) i]);
    p.closeSubPath(); return p;
}

// dry-brush: paper-coloured streaks dragged through a wide stroke (the bristles running out of ink)
inline void dryStreaks(juce::Graphics& g, const std::vector<P>& pts, float width, juce::Random& r, C paperCol, int streaks = 4) {
    const int n = (int) pts.size(); if (n < 3) return;
    for (int k = 0; k < streaks; ++k) {
        const float off = (r.nextFloat() - 0.5f) * width * 0.75f; const float th = 0.5f + r.nextFloat() * 1.3f; Wobble gap(r);
        for (int i = 1; i < n; ++i) {
            const float t = (float) i / (float) (n - 1); if (gap.at(t * 6.0f) < 0.05f + 0.35f * (1.0f - t)) continue;   // more gaps where the brush lifts
            const P a = pts[(size_t) i - 1], b = pts[(size_t) i]; P d = b - a; const float l = juce::jmax(0.001f, d.getDistanceFromOrigin()); const P nrm(-d.y / l, d.x / l);
            g.setColour(paperCol.withAlpha(0.35f + 0.4f * t)); g.drawLine({ a + nrm * off, b + nrm * off }, th);
        }
    }
}

// a watercolour wash: an irregular puddle, pale inside, pigment pooled at its dried edge, a second layer bleeding past it
inline void wash(juce::Graphics& g, P c, float rad, C col, float alpha, juce::Random& r) {
    for (int layer = 0; layer < 2; ++layer) {
        Wobble w(r); juce::Path p; const int N = 56; const float rr = rad * (layer ? 0.78f + r.nextFloat() * 0.3f : 1.0f);
        const P cc = c + P((r.nextFloat() - 0.5f) * rad * 0.35f * (float) layer, (r.nextFloat() - 0.5f) * rad * 0.3f * (float) layer);
        std::vector<P> v; for (int i = 0; i < N; ++i) { const float a = (float) i / N * 6.2832f; const float k = 1.0f + 0.22f * w.at(a * 1.3f) + 0.06f * std::sin(a * 9.0f + w.ph[0]);
            v.push_back(cc + P(std::cos(a), std::sin(a) * 0.82f) * rr * k); }
        p.startNewSubPath((v[0] + v[1]) * 0.5f); for (int i = 1; i <= N; ++i) { const P a = v[(size_t) (i % N)], b = v[(size_t) ((i + 1) % N)]; p.quadraticTo(a, (a + b) * 0.5f); } p.closeSubPath();
        const float al = alpha * (layer ? 0.7f : 1.0f);
        g.setGradientFill(juce::ColourGradient(col.withAlpha(al * 0.45f), cc.x, cc.y, col.withAlpha(al * 0.95f), cc.x + rr, cc.y, true)); g.fillPath(p);
        g.setColour(col.withAlpha(juce::jmin(1.0f, al * 1.2f))); g.strokePath(p, juce::PathStrokeType(1.0f + r.nextFloat()));   // the dried edge
    }
}

// a hand-inked frame: four separate strokes, each overshooting its corner a little, never quite closed
inline void inkFrame(juce::Graphics& g, juce::Rectangle<float> f, juce::Random& r, C col, float width, float tremble) {
    const P tl = f.getTopLeft(), tr = f.getTopRight(), br = f.getBottomRight(), bl = f.getBottomLeft();
    auto side = [&](P a, P b) {
        const P d = (b - a); const float len = d.getDistanceFromOrigin(); const P u = d / juce::jmax(1.0f, len);
        const float o0 = 2.0f + r.nextFloat() * 5.0f, o1 = 1.0f + r.nextFloat() * 6.0f;
        const auto pts = stroke(a - u * o0 + P(r.nextFloat() - 0.5f, r.nextFloat() - 0.5f) * 2.0f, b + u * o1, (r.nextFloat() - 0.5f) * juce::jmin(6.0f, len * 0.012f), juce::jlimit(8, 90, (int) (len / 8)), tremble, r);
        g.setColour(col); g.fillPath(brush(pts, width * (0.8f + r.nextFloat() * 0.5f), r, 0.08f, 0.18f));
    };
    side(tl, tr); side(tr, br); side(br, bl); side(bl, tl);
}

// the paper backdrop, rendered once per size
struct Paper {
    juce::Image img; int w = 0, h = 0;
    void draw(juce::Graphics& g, int W, int H) {
        if (! img.isValid() || W != w || H != h) render(W, H);
        g.drawImageAt(img, 0, 0);
    }
    void render(int W, int H) {
        w = W; h = H; img = juce::Image(juce::Image::ARGB, juce::jmax(1, W), juce::jmax(1, H), true);
        juce::Random r(19951995);
        {   // washi: base cream, low-frequency mottling + fine grain (pixel pass)
            const int gs = 48, gw = W / gs + 3, gh = H / gs + 3; std::vector<float> grid((size_t) (gw * gh)); for (auto& v : grid) v = r.nextFloat() - 0.5f;
            juce::Image::BitmapData bd(img, juce::Image::BitmapData::writeOnly); const C base = paper();
            for (int y = 0; y < H; ++y) {
                const float gy = (float) y / gs; const int y0 = (int) gy; const float fy = gy - (float) y0;
                for (int x = 0; x < W; ++x) {
                    const float gx = (float) x / gs; const int x0 = (int) gx; const float fx = gx - (float) x0;
                    auto G = [&](int i, int j) { return grid[(size_t) (j * gw + i)]; };
                    const float m = (G(x0, y0) * (1 - fx) + G(x0 + 1, y0) * fx) * (1 - fy) + (G(x0, y0 + 1) * (1 - fx) + G(x0 + 1, y0 + 1) * fx) * fy;
                    const float vx = ((float) x / (float) W - 0.5f), vy = ((float) y / (float) H - 0.5f); const float vig = (vx * vx + vy * vy) * 0.10f;   // warmer, darker toward the edges
                    const float k = 1.0f + m * 0.045f - vig + (r.nextFloat() - 0.5f) * 0.03f;
                    bd.setPixelColour(x, y, C::fromFloatRGBA(juce::jlimit(0.0f, 1.0f, base.getFloatRed() * k), juce::jlimit(0.0f, 1.0f, base.getFloatGreen() * k), juce::jlimit(0.0f, 1.0f, base.getFloatBlue() * (k - vig * 0.4f)), 1.0f));
                }
            }
        }
        juce::Graphics g(img); const float fw = (float) W, fh = (float) H;
        // fibres: short curling hairs, some darker, some paler than the sheet
        const int nf = W * H / 900;
        for (int i = 0; i < nf; ++i) {
            const P a(r.nextFloat() * fw, r.nextFloat() * fh); const float ang = r.nextFloat() * 6.283f, len = 4.0f + r.nextFloat() * r.nextFloat() * 34.0f;
            juce::Path p; p.startNewSubPath(a); const P b = a + P(std::cos(ang), std::sin(ang)) * len; const P c = (a + b) * 0.5f + P(r.nextFloat() - 0.5f, r.nextFloat() - 0.5f) * len * 0.6f; p.quadraticTo(c, b);
            const bool dark = r.nextFloat() < 0.55f; g.setColour(dark ? C(0xff9C8E73).withAlpha(0.10f + r.nextFloat() * 0.14f) : paperHi().withAlpha(0.35f + r.nextFloat() * 0.4f));
            g.strokePath(p, juce::PathStrokeType(0.4f + r.nextFloat() * 0.7f));
        }
        // faint pigment drifting through the whole sheet (it tints the panels very lightly from below)
        const C tints[] { vermilion(), indigo(), moss(), blossom(), ochre() };
        for (int i = 0; i < 9; ++i) wash(g, { r.nextFloat() * fw, 150.0f + r.nextFloat() * juce::jmax(10.0f, fh - 200.0f) }, 90.0f + r.nextFloat() * 160.0f, tints[i % 5], 0.045f, r);
        // ---- the header: a pale moon, distant ink-wash mountains, bamboo at the right edge, a blossom branch, an ink frame on the edges
        {   // the moon (top right), a pale ochre wash with a quick, unfinished ink circle
            const P mc(fw - 190.0f, 30.0f); const float mr = 44.0f;
            wash(g, mc, mr, ochre(), 0.30f, r); wash(g, mc + P(-6, 4), mr * 0.7f, C(0xffF3E2B8), 0.25f, r);
            juce::Path arc; arc.addCentredArc(mc.x, mc.y, mr * 1.02f, mr * 0.86f, 0.0f, 0.6f, 5.4f, true);
            std::vector<P> pts; for (float t = 0; t <= 1.0f; t += 0.02f) pts.push_back(arc.getPointAlongPath(arc.getLength() * t));
            g.setColour(ink().withAlpha(0.45f)); g.fillPath(brush(pts, 2.2f, r, 0.1f, 0.5f));
        }
        for (int layer = 0; layer < 2; ++layer) {   // two ridgelines of grey-indigo ink wash along the header's foot
            juce::Path m; Wobble w(r); const float base = 128.0f, top = layer ? 92.0f : 76.0f, amp = layer ? 14.0f : 22.0f;
            m.startNewSubPath(0, base + 4); for (float x = 0; x <= fw; x += 6.0f) m.lineTo(x, top + amp * (0.5f + 0.5f * w.at(x * 0.004f + (float) layer)) + 4.0f * std::sin(x * 0.05f + (float) layer)); m.lineTo(fw, base + 4); m.closeSubPath();
            const C col = layer ? C(0xff4A5568) : C(0xff7E8796);
            g.setGradientFill(juce::ColourGradient(col.withAlpha(layer ? 0.20f : 0.13f), 0, top, col.withAlpha(0.02f), 0, base, false)); g.fillPath(m);
        }
        for (int s = 0; s < 3; ++s) {   // bamboo: moss-wash stalks with ink joints and a few leaves
            const float x = fw - 18.0f - (float) s * 15.0f - r.nextFloat() * 4.0f, wd = 6.0f + r.nextFloat() * 3.0f;
            const auto pts = stroke({ x + r.nextFloat() * 3.0f, -4.0f }, { x - 2.0f + r.nextFloat() * 4.0f, 132.0f }, (r.nextFloat() - 0.5f) * 6.0f, 40, 0.6f, r);
            g.setColour(moss().withAlpha(0.34f - 0.06f * (float) s)); g.fillPath(brush(pts, wd, r, 0.0f, 0.0f));
            for (float y = 14.0f + r.nextFloat() * 18.0f; y < 126.0f; y += 26.0f + r.nextFloat() * 12.0f) {
                const auto j = stroke({ x - wd * 0.7f, y }, { x + wd * 0.7f, y + 1.5f }, 1.5f, 8, 0.2f, r); g.setColour(ink().withAlpha(0.55f)); g.fillPath(brush(j, 1.8f, r, 0.3f, 0.3f)); }
        }
        for (int l = 0; l < 5; ++l) {   // leaves
            const P a(fw - 30.0f - r.nextFloat() * 30.0f, 10.0f + r.nextFloat() * 90.0f); const float ang = 2.3f + r.nextFloat() * 1.4f, len = 22.0f + r.nextFloat() * 18.0f;
            const auto pts = stroke(a, a + P(std::cos(ang), std::sin(ang)) * len, (r.nextFloat() - 0.5f) * 8.0f, 16, 0.3f, r);
            g.setColour((l % 2 ? moss().darker(0.4f) : ink()).withAlpha(0.42f)); g.fillPath(brush(pts, 6.0f, r, 0.15f, 0.85f));
        }
        {   // a blossom branch hanging in from the top edge
            const float bx0 = fw * 0.52f; Wobble w(r);
            const auto main = stroke({ bx0 - 120.0f, -6.0f }, { bx0 + 40.0f, 30.0f }, 14.0f, 40, 1.2f, r);
            g.setColour(ink().withAlpha(0.60f)); g.fillPath(brush(main, 4.5f, r, 0.0f, 0.7f));
            std::vector<P> buds;
            for (int b = 0; b < 3; ++b) { const P a = main[(size_t) (10 + b * 9)]; const P e = a + P(10.0f + r.nextFloat() * 22.0f, 8.0f + r.nextFloat() * 16.0f);
                const auto tw = stroke(a, e, 4.0f, 12, 0.4f, r); g.setColour(ink().withAlpha(0.55f)); g.fillPath(brush(tw, 2.2f, r, 0.0f, 0.8f)); buds.push_back(e); buds.push_back((a + e) * 0.5f); }
            buds.push_back(main.back()); buds.push_back(main[30]);
            for (auto& b : buds) { wash(g, b, 5.0f + r.nextFloat() * 3.0f, blossom(), 0.55f, r); g.setColour(vermilion().withAlpha(0.55f)); g.fillEllipse(b.x - 1.2f, b.y - 1.2f, 2.4f, 2.4f); }
        }
        // the ink frame: heavy dry-brush strokes catching the window's edges (seen in the gaps around the panels)
        auto edge = [&](P a, P b, float wd, float bow) { const auto pts = stroke(a, b, bow, 60, 1.6f, r); g.setColour(ink().withAlpha(0.82f)); g.fillPath(brush(pts, wd, r, 0.12f, 0.45f)); dryStreaks(g, pts, wd, r, paper(), 5); };
        edge({ fw * 0.62f, 3.0f }, { fw + 6.0f, 2.0f }, 7.0f, 2.0f);
        edge({ 2.5f, fh * 0.45f }, { 3.5f, fh + 6.0f }, 6.0f, -3.0f);
        edge({ -6.0f, fh - 3.0f }, { fw * 0.34f, fh - 2.5f }, 6.5f, 2.0f);
        edge({ fw - 3.0f, fh * 0.70f }, { fw - 2.5f, fh + 4.0f }, 5.0f, 2.0f);
        // a vermilion seal stamp, bottom right, in the gap under the panels
        { const juce::Rectangle<float> s(fw - 30.0f, fh - 7.5f, 14.0f, 7.0f); g.setColour(vermilion().withAlpha(0.75f)); g.fillRect(s); }
    }
    // live, over the header: cherry petals drifting down; energy (0..1, a drum hit) makes them swirl
    static void overlay(juce::Graphics& g, int W, float maxY, float t, float energy) {
        for (int i = 0; i < 16; ++i) {
            const float seed = (float) i * 12.9898f; const float rx = std::fmod(std::sin(seed) * 43758.5453f, 1.0f), ry = std::fmod(std::sin(seed * 1.7f) * 24634.6345f, 1.0f);
            const float sp = 9.0f + 10.0f * std::abs(ry);   // px per second
            const float y = std::fmod(t * sp + std::abs(ry) * (maxY + 30.0f), maxY + 30.0f) - 15.0f;
            const float sway = (8.0f + 22.0f * energy) * std::sin(t * (0.6f + 0.3f * std::abs(rx)) + seed);
            const float x = std::abs(rx) * (float) W + sway;
            const float rot = t * (0.8f + std::abs(rx)) + seed + energy * 2.0f, sz = 3.0f + 2.5f * std::abs(ry);
            juce::Path p; p.addEllipse(-sz, -sz * 0.55f, sz * 2.0f, sz * 1.1f); p.applyTransform(juce::AffineTransform::rotation(rot).translated(x, y));
            g.setColour(blossom().withAlpha(0.75f)); g.fillPath(p); g.setColour(C(0xffC98C95).withAlpha(0.6f)); g.strokePath(p, juce::PathStrokeType(0.6f));
        }
    }
};

// the GHOSTY BEATS artwork sits on a sweep of black ink (it is drawn for a dark backdrop)
struct LogoSwash {
    juce::Path body; std::vector<P> spine; juce::Rectangle<float> at; float wd = 0;
    void draw(juce::Graphics& g, juce::Rectangle<float> r) {
        if (r != at) { at = r; juce::Random rnd(1013); wd = r.getHeight() * 1.04f;
            spine = stroke({ r.getX() - 4.0f, r.getCentreY() + 1.0f }, { r.getRight() + 14.0f, r.getCentreY() - 2.0f }, 3.0f, 70, 1.2f, rnd);
            body = brush(spine, wd, rnd, 0.05f, 0.22f); }
        g.setColour(ink().withAlpha(0.95f)); g.fillPath(body);
        juce::Random rnd(2026); dryStreaks(g, { spine.begin() + (long) spine.size() * 3 / 4, spine.end() }, wd, rnd, paper(), 6);   // the brush runs dry at its tail
    }
};

} // namespace bx::sumi
