#pragma once
// GHOST HUM - glass for the notes, the hits and the big ring (build 8, 2026-11-13: "make the MIDI notes look like that too, and this
// ring"). The same material as the living glass sliders (HumGlass.h): thick pale glass with a little pixel-art light inside.
//
//   GlassTiles   a note (a capsule) or a beat hit (a rounded tile) as a piece of glass: inside, a dithered pixel light (4 x 4 Bayer,
//                art pixels of 1.5 points) - bright at the attack and fading along the note like its envelope; a hit glows from
//                its centre. Brightness = velocity; a note or hit the playhead is on lights up. The glass on top: refraction at the
//                curved edge with a hair of R / G / B split, Fresnel edges, a long soft highlight, one sharp glint, a darker band
//                at a note's start, a bubble or two in long notes. Each look is rendered once per size / colour / state at the
//                screen's real resolution and kept (a small cache), so a roll full of notes costs no more than before.
//   GlassRing    the record button's thick ring as a glass tube with a tiny circuit world inside, seen through the glass. Its light
//                is the recording: idle, a slow arc of light orbits; while you record the ring fills with light clockwise from
//                the top as the 90 seconds go by (Bayer-dithered edge, bright rim) and your voice runs around it as a waveform;
//                count-in lights a quarter per beat; while Ghost listens, an arc spins. Bubbles drift round the tube.
// Everything is drawn on the message thread; nothing touches the audio.
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace hum::ui::glass {

struct C3 { float r = 0, g = 0, b = 0; };
inline C3 c3(juce::Colour c, float k = 1.0f) { return { c.getFloatRed() * k, c.getFloatGreen() * k, c.getFloatBlue() * k }; }
inline C3 lerp(C3 a, C3 b, float k) { return { a.r + (b.r - a.r) * k, a.g + (b.g - a.g) * k, a.b + (b.b - a.b) * k }; }
inline float bayer(int x, int y) { static const int m[4][4] { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } }; return ((float) m[y & 3][x & 3] + 0.5f) / 16.0f; }
inline float hash(int n) { unsigned int x = (unsigned int) n * 747796405u + 2891336453u; x = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u; x = (x >> 22u) ^ x; return (float) (x & 0xffffff) / 16777216.0f; }
inline float sstep(float a, float b, float x) { const float t = juce::jlimit(0.0f, 1.0f, (x - a) / (b - a)); return t * t * (3.0f - 2.0f * t); }
inline juce::PixelARGB premul(C3 c, float a) {
    a = juce::jlimit(0.0f, 1.0f, a); auto q = [a](float v) { return (juce::uint8) juce::jlimit(0, 255, (int) (juce::jlimit(0.0f, a, v * a) * 255.0f + 0.5f)); };
    juce::PixelARGB p; p.setARGB((juce::uint8) (a * 255.0f + 0.5f), q(c.r), q(c.g), q(c.b)); return p; }

// the glass on top of whatever light is under it. t1: 0 on the thick middle .. 1 at the edge; (dx, dy): the way out to the nearest edge;
// light comes from the top left.
inline C3 shade(C3 c, float t1, float dx, float dy, float energy, float rimBoost, float glassK = 1.0f) {
    const float nz = std::sqrt(juce::jmax(0.0f, 1.0f - t1 * t1)), path = 1.0f / juce::jmax(0.3f, nz), tint = juce::jmin(1.0f, 0.25f + (path - 1.0f) * 0.4f);
    c.r *= 1.0f - 0.22f * tint; c.b *= 1.0f - 0.07f * tint;                                                 // thick glass: a little aqua where the path is long
    const float body = (0.04f + 0.10f * t1) * glassK; c.r += 0.50f * body; c.g += 0.92f * body; c.b += 0.86f * body;   // the pale-aqua glass itself
    const float F = 0.03f + 0.97f * std::pow(juce::jlimit(0.0f, 1.0f, 1.0f - nz), 3.0f);
    const float lightDir = juce::jmax(0.0f, -0.55f * dx - 0.83f * dy);
    const float fres = F * (0.5f + 0.4f * energy + rimBoost) * (0.7f + 0.5f * lightDir) * (0.4f + 0.6f * glassK);
    c.r += 0.74f * fres; c.g += 1.0f * fres; c.b += 0.90f * fres;
    c.r += 0.08f * F * juce::jmax(0.0f, -dx); c.b += 0.10f * F * juce::jmax(0.0f, dx);                     // colour fringe on the curve
    const float hl = std::exp(-(t1 - 0.6f) * (t1 - 0.6f) * 80.0f) * lightDir * lightDir * (0.45f + 0.2f * energy); c.r += hl; c.g += hl; c.b += hl;
    const float bounce = std::exp(-(t1 - 0.78f) * (t1 - 0.78f) * 30.0f) * juce::jmax(0.0f, 0.55f * dx + 0.83f * dy) * 0.10f; c.g += bounce; c.b += bounce * 0.8f;
    const float edge = 0.62f + 0.38f * sstep(0.0f, 0.12f, 1.0f - t1); c.r *= edge; c.g *= edge; c.b *= edge;
    return c;
}

// ------------------------------------------------------------------------------------------------ notes and hits
struct GlassTiles {
    struct Look { juce::Colour tint; float vel = 1; bool selected = false, lit = false, pill = true, unsure = false; };

    // draw one tile into r (points). Cached per size / look.
    void draw(juce::Graphics& g, juce::Rectangle<float> r, const Look& L) {
        if (r.getWidth() < 1.0f || r.getHeight() < 1.0f) return;
        const float s = juce::jlimit(1.0f, 3.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        // snap to the real pixel grid so the pixel art stays crisp
        const float x0 = std::round(r.getX() * s) / s, y0 = std::round(r.getY() * s) / s; const int W = juce::jmax(1, (int) std::round(r.getWidth() * s)), H = juce::jmax(1, (int) std::round(r.getHeight() * s));
        const int vb = juce::jlimit(0, 8, (int) std::round(L.vel * 8.0f)); const float rad = L.pill ? r.getHeight() * 0.5f : juce::jmin(3.0f, r.getHeight() * 0.5f);
        const juce::uint64 key = (juce::uint64) W | ((juce::uint64) H << 12) | ((juce::uint64) vb << 22) | ((juce::uint64) ((L.selected ? 1 : 0) | (L.lit ? 2 : 0) | (L.pill ? 4 : 0) | (L.unsure ? 8 : 0)) << 26)
                               | ((juce::uint64) (L.tint.getARGB() & 0xffffff) << 30) | ((juce::uint64) juce::jlimit(1, 4, (int) std::round(s * 2.0f)) << 54);
        auto it = cache.find(key);
        if (it == cache.end()) { if (cache.size() > 700) cache.clear(); it = cache.emplace(key, render(W, H, s, rad, L, vb / 8.0f)).first; }
        g.setOpacity(1.0f); g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);   // (an image takes the current colour's alpha: reset it)
        g.drawImage(it->second, juce::Rectangle<float>(x0, y0, (float) W / s, (float) H / s), juce::RectanglePlacement::stretchToFit, false);
    }
    size_t cached() const { return cache.size(); }

private:
    std::unordered_map<juce::uint64, juce::Image> cache;

    static float sdBox(float px, float py, float hx, float hy, float r) { const float qx = std::abs(px) - hx + r, qy = std::abs(py) - hy + r;
        return std::sqrt(juce::jmax(qx, 0.0f) * juce::jmax(qx, 0.0f) + juce::jmax(qy, 0.0f) * juce::jmax(qy, 0.0f)) + juce::jmin(juce::jmax(qx, qy), 0.0f) - r; }

    static juce::Image render(int W, int H, float s, float rad, const Look& L, float vel) {
        juce::Image im(juce::Image::ARGB, W, H, true, juce::SoftwareImageType());
        const float w = (float) W / s, h = (float) H / s, hx = w * 0.5f, hy = h * 0.5f; rad = juce::jmin(rad, juce::jmin(hx, hy));
        const float bevel = juce::jmax(0.8f, juce::jmin(hy, hx) * (L.pill ? 1.0f : 0.8f)), art = 1.5f;
        const C3 tint = c3(L.tint), deep = lerp(c3(juce::Colour(0xff06180f)), tint, 0.38f), mid = tint, hi = lerp(tint, c3(juce::Colours::white), 0.28f), white { 1, 1, 1 };
        const float lv = (L.unsure ? 0.25f : 0.45f + 0.55f * vel) * (L.lit ? 1.35f : 1.0f) + (L.selected ? 0.12f : 0.0f);
        // the light inside (art pixels, Bayer dithered between three levels)
        auto inner = [&](float lx, float ly) -> C3 {   // lx, ly: points from the tile's top left
            const int ax = (int) std::floor(lx / art), ay = (int) std::floor(ly / art);
            float env;
            if (L.pill) { const float u = juce::jlimit(0.0f, 1.0f, lx / juce::jmax(1.0f, w)); env = 0.55f + 0.75f * std::exp(-u * 3.0f) - 0.25f * std::abs(ly / juce::jmax(0.5f, h) - 0.5f) * 2.0f; }
            else { const float dx = (lx - hx) / juce::jmax(1.0f, hx), dy = (ly - hy) / juce::jmax(1.0f, hy); env = 1.0f - 0.55f * juce::jmin(1.0f, std::sqrt(dx * dx + dy * dy)); }
            const float q = lv * env * 1.6f + (bayer(ax, ay) - 0.5f) * 0.45f;   // clean regions, dithered only where they meet
            const int level = q > 1.25f ? 2 : q > 0.55f ? 1 : 0;
            C3 c = level == 0 ? deep : level == 1 ? mid : hi;
            if (L.lit && level == 2 && bayer(ax + 1, ay + 2) < 0.25f) c = lerp(c, white, 0.6f);   // playing: sparkle
            return c; };
        juce::Image::BitmapData bd(im, juce::Image::BitmapData::writeOnly);
        const float bx = rad * 0.75f, by = h * 0.30f, bub = juce::jmax(0.0f, w - 18.0f);   // the glint; bubbles only in long notes
        for (int py = 0; py < H; ++py) for (int px = 0; px < W; ++px) {
            const float lx = ((float) px + 0.5f) / s, ly = ((float) py + 0.5f) / s, cx = lx - hx, cy = ly - hy;
            const float d = sdBox(cx, cy, hx, hy, rad), cov = juce::jlimit(0.0f, 1.0f, 0.5f - d * s);
            if (cov <= 0) { *reinterpret_cast<juce::PixelARGB*>(bd.getPixelPointer(px, py)) = juce::PixelARGB(0, 0, 0, 0); continue; }
            const float e = 0.3f; float gx = sdBox(cx + e, cy, hx, hy, rad) - sdBox(cx - e, cy, hx, hy, rad), gy = sdBox(cx, cy + e, hx, hy, rad) - sdBox(cx, cy - e, hx, hy, rad);
            const float gl = std::sqrt(gx * gx + gy * gy); if (gl > 1e-5f) { gx /= gl; gy /= gl; }
            const float t1 = 1.0f - juce::jlimit(0.0f, 1.0f, -d / bevel), bend = bevel * 0.45f * t1, disp = 0.45f * t1 * t1;
            // refraction: the light inside bends at the curved edge, R and B a hair apart
            const C3 sr = inner(lx - gx * (bend - disp), ly - gy * (bend - disp)), sg = inner(lx - gx * bend, ly - gy * bend), sb = inner(lx - gx * (bend + disp), ly - gy * (bend + disp));
            C3 c { sr.r, sg.g, sb.b };
            if (L.pill && w > 10.0f) { const float band = 1.0f - sstep(2.2f, 3.4f, lx); c.r *= 1.0f - 0.25f * band; c.g *= 1.0f - 0.18f * band; c.b *= 1.0f - 0.16f * band; }   // the darker band at the start
            c = shade(c, t1, gx, gy, lv, L.selected ? 0.55f : 0.0f, 0.45f);
            const float sx = lx - bx, sy = ly - by; const float spec = std::exp(-(sx * sx + sy * sy) * 6.0f) * 1.3f * (h > 6.0f ? 1.0f : 0.5f); c.r += spec; c.g += spec; c.b += spec;
            if (bub > 0) for (int k = 0; k < (w > 60.0f ? 2 : 1); ++k) { const float bxp = 10.0f + bub * (0.35f + 0.4f * (float) k), byp = h * (0.55f + 0.1f * (float) k), br = juce::jmin(1.0f, h * 0.12f);
                const float dd = std::sqrt((lx - bxp) * (lx - bxp) + (ly - byp) * (ly - byp)); const float ring = std::exp(-(dd - br) * (dd - br) * 8.0f) * 0.28f; c.r += 0.75f * ring; c.g += ring; c.b += ring; }
            if (L.selected) { const float rim = std::exp(-(-d) * 2.2f) * 0.55f; c.r += rim; c.g += rim; c.b += rim; }
            *reinterpret_cast<juce::PixelARGB*>(bd.getPixelPointer(px, py)) = premul(c, cov * (L.unsure ? 0.75f : 0.97f));
        }
        return im;
    }
};

// ------------------------------------------------------------------------------------------------ the record ring
struct GlassRing {
    struct State { float phase = 0; float level = 0; bool recording = false, busy = false, countingIn = false; int countLeft = 0, countTotal = 4; float progress = 0; bool animate = true; };
    std::array<float, 96> voice {};   // the recent input level, newest first (the waveform that runs round the ring)
    int voiceAt = 0;

    void push(float level) { voiceAt = (voiceAt + 1) % (int) voice.size(); voice[(size_t) voiceAt] = level; }

    // draw a glass tube round the circle c (points); tube = its thickness
    void draw(juce::Graphics& g, juce::Rectangle<float> c, float tube, juce::Colour acc, juce::Colour accHi, const State& S) {
        const float s = juce::jlimit(1.0f, 3.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        const auto box = c.expanded(2.0f); const int W = juce::jmax(1, (int) std::ceil(box.getWidth() * s)), H = juce::jmax(1, (int) std::ceil(box.getHeight() * s));
        if (img.isNull() || img.getWidth() != W || img.getHeight() != H) img = juce::Image(juce::Image::ARGB, W, H, true, juce::SoftwareImageType());
        const float Rc = c.getWidth() * 0.5f - tube * 0.5f, hw = tube * 0.5f, art = 1.5f, ccx = box.getWidth() * 0.5f, ccy = box.getHeight() * 0.5f;
        const int N = juce::jmax(16, (int) std::round(juce::MathConstants<float>::twoPi * Rc / art)), M = juce::jmax(3, (int) std::ceil(tube / art));
        buildWorld(N, M, acc, accHi, S);
        juce::Image::BitmapData bd(img, juce::Image::BitmapData::writeOnly);
        const float rin2 = (Rc - hw - 1.0f) * (Rc - hw - 1.0f), rout2 = (Rc + hw + 1.0f) * (Rc + hw + 1.0f);
        const float energy = S.recording ? 0.6f + juce::jmin(0.4f, S.level * 3.0f) : 0.35f;
        for (int py = 0; py < H; ++py) { const float ly = ((float) py + 0.5f) / s - ccy;
            for (int px = 0; px < W; ++px) { const float lx = ((float) px + 0.5f) / s - ccx; const float r2 = lx * lx + ly * ly; auto* dst = reinterpret_cast<juce::PixelARGB*>(bd.getPixelPointer(px, py));
                if (r2 < rin2 || r2 > rout2) { *dst = juce::PixelARGB(0, 0, 0, 0); continue; }
                const float rr = std::sqrt(r2), dr = rr - Rc, sd = std::abs(dr) - hw, cov = juce::jlimit(0.0f, 1.0f, 0.5f - sd * s);
                if (cov <= 0) { *dst = juce::PixelARGB(0, 0, 0, 0); continue; }
                const float ux = lx / juce::jmax(1e-3f, rr), uy = ly / juce::jmax(1e-3f, rr), u = juce::jlimit(-1.0f, 1.0f, dr / hw), t1 = std::abs(u);
                const float sg = u >= 0 ? 1.0f : -1.0f, dx = ux * sg, dy = uy * sg;
                float ang = std::atan2(lx, -ly); if (ang < 0) ang += juce::MathConstants<float>::twoPi;   // 0 at the top, clockwise
                // refraction across the tube: magnify towards the middle, R / B a hair apart
                const float mag = dr * 0.72f - u * hw * 0.25f, disp = 0.5f * t1 * t1 * sg;
                auto at = [&](float drr) { const int ai = (int) (ang / juce::MathConstants<float>::twoPi * (float) N) % N, ri = juce::jlimit(0, M - 1, (int) std::floor((drr + hw) / art)); return world[(size_t) (ri * N + ai)]; };
                const C3 a = at(mag + disp), b = at(mag), d = at(mag - disp); C3 col { a.r, b.g, d.b };
                col = shade(col, t1, dx, dy, energy, 0.25f);
                { const float lightDir = juce::jmax(0.0f, -0.55f * dx - 0.83f * dy); const float hl2 = std::exp(-(u * sg - 0.45f) * (u * sg - 0.45f) * 30.0f) * std::pow(lightDir, 3.0f) * 0.35f; col.r += hl2; col.g += hl2; col.b += hl2; }
                // two glints (top left outer, a fainter one bottom right) and the bubbles drifting round
                for (int k = 0; k < 2; ++k) { const float ga = k == 0 ? -0.78f : 2.36f, gu = k == 0 ? 0.42f : -0.4f; const float gx = std::sin(ga) * (Rc + gu * hw) - lx, gy = -std::cos(ga) * (Rc + gu * hw) - ly;
                    const float sp = std::exp(-(gx * gx + gy * gy) * (k == 0 ? 2.0f : 3.0f)) * (k == 0 ? 1.4f : 0.5f); col.r += sp; col.g += sp; col.b += sp; }
                for (int k = 0; k < 9; ++k) { const float ba = std::fmod(hash(k * 7) * 6.2832f + S.phase * (0.10f + 0.08f * hash(k)) * (S.recording ? 2.5f : 1.0f), 6.2832f), bu = (hash(k * 13) - 0.5f) * 1.1f, br = 0.6f + hash(k * 5) * 0.8f;
                    const float bx = std::sin(ba) * (Rc + bu * hw) - lx, by = -std::cos(ba) * (Rc + bu * hw) - ly; const float dd = std::sqrt(bx * bx + by * by);
                    if (dd < br + 1.5f) { const float ring = std::exp(-(dd - br) * (dd - br) * 6.0f) * 0.3f * (1.0f - t1 * 0.6f); col.r += 0.75f * ring; col.g += ring; col.b += ring; } }
                *dst = premul(col, cov);
            } }
        g.setOpacity(1.0f); g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
        g.drawImage(img, juce::Rectangle<float>(box.getX(), box.getY(), (float) W / s, (float) H / s), juce::RectanglePlacement::stretchToFit, false);
    }

private:
    juce::Image img; std::vector<C3> world;

    // the little circuit world inside the tube, in polar art pixels: N round the ring, M across it (0 = inner edge)
    void buildWorld(int N, int M, juce::Colour accC, juce::Colour hiC, const State& S) {
        world.assign((size_t) (N * M), C3 {});
        const C3 acc = c3(accC), hi = c3(hiC), white { 1, 1, 1 }, dark = lerp(c3(juce::Colour(0xff04100c)), acc, 0.20f), darkLit = lerp(c3(juce::Colour(0xff06140f)), acc, 0.62f);
        const float tw = juce::MathConstants<float>::twoPi; const float t = S.phase;
        // where the ring is lit, as a fraction of the way round (start .. end, clockwise from the top)
        auto litAt = [&](int ai, int ri) -> float {   // 0 dark .. 1 lit (dithered at the edge)
            const float f = ((float) ai + 0.5f) / (float) N; float a0 = 0, a1 = 0;
            if (S.recording) { a0 = 0; a1 = juce::jmax(0.02f, S.progress); }
            else if (S.countingIn) { const int done = juce::jlimit(0, S.countTotal, S.countTotal - S.countLeft + 1); a0 = 0; a1 = (float) done / (float) juce::jmax(1, S.countTotal); }
            else if (S.busy) { a0 = std::fmod(t * 0.9f, 1.0f); a1 = a0 + 0.22f; }
            else { a0 = std::fmod(t * (S.animate ? 0.06f : 0.0f), 1.0f); a1 = a0 + 0.30f; }
            float x = f; if (x < a0) x += 1.0f; const float edgeCells = 5.0f / (float) N;
            if (x <= a1 - edgeCells) return 1.0f; if (x >= a1) return 0.0f;
            const float k = (a1 - x) / edgeCells; return k > bayer(ai, ri) ? 1.0f : 0.0f; };
        auto put = [&](int ai, int ri, C3 c) { ai = ((ai % N) + N) % N; if (ri < 0 || ri >= M) return; world[(size_t) (ri * N + ai)] = c; };
        const float live = S.recording ? juce::jmin(1.0f, 0.4f + S.level * 4.0f) : 0.6f;
        for (int ai = 0; ai < N; ++ai) {
            for (int ri = 0; ri < M; ++ri) { const bool on = litAt(ai, ri) > 0.5f; put(ai, ri, on ? darkLit : dark); }
            // the circuit: traces along both edges of the tube, with pads every so often
            for (int ri : { 0, M - 1 }) { const bool on = litAt(ai, ri) > 0.5f; const bool gap = hash(ai / 7 * 3 + ri) < 0.25f && ai % 7 == 6; if (gap) continue;
                put(ai, ri, on ? lerp(acc, hi, 0.35f + 0.4f * live) : lerp(dark, acc, 0.35f)); }
            if (ai % 6 == 3 && hash(ai * 11) < 0.7f) { const bool in = hash(ai * 5) < 0.5f; const int r0 = in ? 1 : M - 2; const bool on = litAt(ai, r0) > 0.5f;
                const bool hot = on && hash(ai * 17 + (int) (t * 4.0f) * (S.animate ? 1 : 0)) < 0.85f;
                put(ai, r0, hot ? lerp(hi, white, 0.6f) : on ? lerp(acc, hi, 0.3f) : lerp(dark, acc, 0.15f) ); }
            // fireflies: single pixels that blink in the lit part
            if (hash(ai * 29) < 0.18f) { const int ri = 1 + (int) (hash(ai * 31) * (float) juce::jmax(1, M - 2)); if (litAt(ai, ri) > 0.5f && std::sin(t * (2.0f + 3.0f * hash(ai)) + (float) ai) > 0.2f) put(ai, ri, lerp(hi, white, 0.4f)); }
        }
        // the waveform round the middle: your voice while recording, a soft idle wave otherwise
        const float mid = (float) (M - 1) * 0.5f;
        for (int ai = 0; ai < N; ++ai) {
            float amp; if (S.recording) { const int k = (int) ((float) ai / (float) N * (float) voice.size()); amp = juce::jmin(1.0f, voice[(size_t) ((voiceAt - k + (int) voice.size() * 4) % (int) voice.size())] * 5.0f); }
            else amp = 0.25f + 0.1f * std::sin(t * 0.7f);
            const float y = mid + std::sin((float) ai * (S.recording ? 0.5f : 0.16f) - t * (S.recording ? 9.0f : 2.0f)) * amp * mid * 0.9f; const int ri = (int) std::round(y);
            const bool on = litAt(ai, ri) > 0.5f; put(ai, ri, on ? lerp(hi, white, 0.25f + 0.5f * amp) : lerp(dark, acc, 0.55f));
        }
        // the bright rim where light meets dark
        for (int ri = 0; ri < M; ++ri) for (int ai = 0; ai < N; ++ai) if (litAt(ai, ri) > 0.5f && litAt((ai + 1) % N, ri) < 0.5f) put(ai, ri, lerp(hi, white, 0.5f));
        juce::ignoreUnused(tw);
    }
};

} // namespace hum::ui::glass
