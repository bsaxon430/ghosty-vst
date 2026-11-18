#pragma once
// NEON look (Ghosty Beats, owner 2026-11-11): the backdrop. A rainy cyberpunk city at dusk painted like a 1995 hand-painted cel background:
// smog-lit sky, three layers of weathered towers fading into the haze (atmospheric perspective), sparse lit windows, a few vertical neon
// signs whose light bleeds into the wet air, sagging power lines, rain, lifted blacks, a soft vignette and film grain.
// Rendered ONCE per size (and pixel scale) into an image and blitted; the header's moving rain + sign flicker are drawn live and cheap
// (overlay()). Deterministic: the same city every time. Message thread only. Nothing here touches the audio thread.
#include <juce_graphics/juce_graphics.h>
#include <cmath>

namespace bx::neoncity {

using C = juce::Colour;
inline C mixC(C a, C b, float t) { return a.interpolatedWith(b, juce::jlimit(0.0f, 1.0f, t)); }

struct Sign { juce::Rectangle<float> r; C col; bool vertical; int glyphSeed; float flickerPh; };

struct City {
    int builds = 0;
    std::vector<Sign> signs;   // (content coordinates; overlay() re-lights them)

    void draw(juce::Graphics& g, int w, int h) {
        const float scale = juce::jlimit(1.0f, 2.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        if (w != cw || h != ch || std::abs(scale - cs) > 0.01f || !img.isValid()) { render(w, h, scale); cw = w; ch = h; cs = scale; ++builds; }
        g.setOpacity(1.0f);
        g.drawImageTransformed(img, juce::AffineTransform::scale(1.0f / scale));
    }

    // the live layer, header only (y < maxY): falling rain and the signs' slow breathing / an occasional tired flicker
    void overlay(juce::Graphics& g, int w, float maxY, float t) {
        juce::Graphics::ScopedSaveState ss(g); g.reduceClipRegion(0, 0, w, (int) maxY);
        for (auto& s : signs) {
            if (s.r.getY() > maxY) continue;
            const float breathe = 0.5f + 0.5f * std::sin(t * 0.7f + s.flickerPh);
            const float fl = std::fmod(t * 0.37f + s.flickerPh, 9.0f) < 0.18f ? 0.35f : 1.0f;   // a tube stutters now and then
            const float a = (0.05f + 0.05f * breathe) * fl;
            g.setColour(s.col.withAlpha(a)); g.fillRoundedRectangle(s.r.expanded(10.0f), 10.0f);
            g.setColour(s.col.withAlpha(a * 1.6f)); g.fillRoundedRectangle(s.r.expanded(4.0f), 5.0f);
            if (fl < 1.0f) { g.setColour(C(0xff0a1112).withAlpha(0.45f)); g.fillRoundedRectangle(s.r, 2.0f); }
        }
        // rain: slanted pale streaks, three speeds (near ones longer, brighter)
        juce::Random r(77);
        for (int i = 0; i < 140; ++i) {
            const int layer = i % 3; const float speed = 260.0f + 180.0f * (float) layer, len = 7.0f + 6.0f * (float) layer;
            const float x0 = r.nextFloat() * (float) (w + 60) - 30.0f, y0 = r.nextFloat() * (maxY + 40.0f);
            float y = std::fmod(y0 + t * speed, maxY + 40.0f) - 20.0f; const float x = x0 - (y - y0) * 0.16f;
            g.setColour(C(0xffc9d6cf).withAlpha(0.06f + 0.05f * (float) layer));
            g.drawLine(x, y, x - len * 0.16f, y + len, 0.7f + 0.3f * (float) layer);
        }
    }

    // the street seen through the bottom strip: the signs' colours lying in the wet ground, broken into ripples by the rain (t moves the ripples)
    static void reflections(juce::Graphics& g, juce::Rectangle<float> r, float t) {
        const C cols[] { C(0xffc6587e), C(0xff4fae9f), C(0xffd29440), C(0xffc6587e), C(0xff6c93a8) };
        juce::Random rnd(4242);
        for (int k = 0; k < 22; ++k) {
            const C col = cols[k % 5]; const float rx = r.getX() + r.getWidth() * rnd.nextFloat(), rw = 6.0f + rnd.nextFloat() * 30.0f, rl = r.getHeight() * (0.45f + 0.55f * rnd.nextFloat());
            const float a0 = 0.10f + 0.10f * rnd.nextFloat(); int row = 0;
            for (float yy = r.getBottom(); yy > r.getBottom() - rl; yy -= 3.0f, ++row) {
                juce::Random rr(k * 977 + row + (int) (t * 6.0f) * 131);   // the ripples shift a few times a second
                if (rr.nextFloat() < 0.22f) continue;
                const float a = a0 * (1.0f - (r.getBottom() - yy) / rl), wob = (rr.nextFloat() - 0.5f) * 5.0f, ww = rw * (0.5f + 0.6f * rr.nextFloat());
                g.setColour(col.withAlpha(a)); g.fillRect(rx + wob + (rw - ww) * 0.5f, yy - 2.0f, ww, 1.6f);
            }
        }
    }
private:
    juce::Image img; int cw = 0, ch = 0; float cs = 0;

    static void glow(juce::Graphics& g, juce::Rectangle<float> r, C col, float strength) {   // light bleeding into wet, smoggy air
        for (int k = 6; k >= 1; --k) { const float e = (float) k * 4.5f; g.setColour(col.withAlpha(strength * 0.035f * (7 - k) / 6.0f + strength * 0.012f)); g.fillRoundedRectangle(r.expanded(e), e + 2.0f); }
    }

    // a sign's light running down through the rain and over the wet facade below it: a soft column of its colour, broken by rain streaks
    static void bleed(juce::Graphics& g, juce::Rectangle<float> s, C col, float fade, float H, juce::Random& rnd) {
        const float len = 60.0f + rnd.nextFloat() * 90.0f, x0 = s.getX() - 4.0f, w0 = s.getWidth() + 8.0f;
        g.setGradientFill(juce::ColourGradient(col.withAlpha(0.20f * fade), 0, s.getBottom(), col.withAlpha(0.0f), 0, juce::jmin(H, s.getBottom() + len), false));
        g.fillRect(x0, s.getBottom(), w0, len);
        juce::Random r(rnd.nextInt());
        for (int k = 0; k < (int) (w0 / 2.5f); ++k) { const float xx = x0 + r.nextFloat() * w0, l = 10.0f + r.nextFloat() * len * 0.8f;
            g.setGradientFill(juce::ColourGradient(mixC(col, C(0xfff0e6d0), 0.3f).withAlpha(0.22f * fade), 0, s.getBottom(), col.withAlpha(0.0f), 0, s.getBottom() + l, false));
            g.fillRect(xx, s.getBottom(), 0.8f, l); }
    }

    void render(int w, int h, float scale) {
        img = juce::Image(juce::Image::ARGB, juce::jmax(1, juce::roundToInt(w * scale)), juce::jmax(1, juce::roundToInt(h * scale)), true, juce::SoftwareImageType());
        juce::Graphics g(img); g.addTransform(juce::AffineTransform::scale(scale));
        const float W = (float) w, H = (float) h; juce::Random rnd(1995);
        signs.clear();
        const C night(0xff0a1112), smog(0xff2b3b38), smogHi(0xff3a4842), amber(0xffd29440), teal(0xff4fae9f), magenta(0xffc6587e), olive(0xff8e9a5c);

        // ---- sky: dark smog above, the city's own light caught in the haze low down, ground in shadow
        { juce::ColourGradient sky(C(0xff141d1c), 0, 0, night, 0, H, false);
          sky.addColour(0.06, C(0xff1d2927)); sky.addColour(0.13, smogHi); sky.addColour(0.20, C(0xff1a2524)); sky.addColour(0.45, C(0xff0e1617));
          g.setGradientFill(sky); g.fillAll(); }
        // dirty amber city-glow low on the right, a cold teal one on the left (soft, wide)
        g.setGradientFill(juce::ColourGradient(amber.withAlpha(0.20f), W * 0.78f, 120.0f, amber.withAlpha(0.0f), W * 0.78f + 420.0f, 120.0f, true)); g.fillAll();
        g.setGradientFill(juce::ColourGradient(teal.withAlpha(0.13f), W * 0.18f, 100.0f, teal.withAlpha(0.0f), W * 0.18f + 360.0f, 100.0f, true)); g.fillAll();

        // ---- three layers of towers (far = pale and hazy, near = almost black); lit windows sparse and dim
        struct Layer { float topMin, topMax, wMin, wMax; float haze; float windows; };
        const Layer layers[] { { 6, 46, 26, 70, 0.72f, 0.05f }, { 26, 78, 34, 96, 0.42f, 0.09f }, { 50, 112, 40, 120, 0.12f, 0.06f } };
        for (int li = 0; li < 3; ++li) {
            const auto& L = layers[li];
            const C body = mixC(C(0xff0d1415), smog, L.haze), edge = mixC(body, smogHi, 0.35f);
            float x = -rnd.nextFloat() * 40.0f;
            while (x < W + 10.0f) {
                const float bw = L.wMin + rnd.nextFloat() * (L.wMax - L.wMin), top = L.topMin + rnd.nextFloat() * (L.topMax - L.topMin);
                juce::Rectangle<float> b(x, top, bw, H - top);
                // body with a faint vertical wash (a painted, slightly uneven face)
                g.setGradientFill(juce::ColourGradient(edge, b.getX(), top, body, b.getX(), top + 140.0f, false)); g.fillRect(b);
                g.setColour(body.darker(0.25f)); g.fillRect(b.withX(b.getRight() - bw * 0.22f).withWidth(bw * 0.22f));   // the shaded side
                // roof clutter: setbacks, water tanks, antennas
                if (rnd.nextFloat() < 0.55f) { const float sw = bw * (0.3f + rnd.nextFloat() * 0.35f), sh = 4.0f + rnd.nextFloat() * 10.0f; g.setColour(edge); g.fillRect(b.getX() + rnd.nextFloat() * (bw - sw), top - sh, sw, sh); }
                if (rnd.nextFloat() < 0.45f) { const float ax = b.getX() + 4.0f + rnd.nextFloat() * (bw - 8.0f), ah = 8.0f + rnd.nextFloat() * 22.0f; g.setColour(edge); g.drawLine(ax, top, ax, top - ah, 1.0f);
                    if (li > 0 && rnd.nextFloat() < 0.5f) { g.setColour(C(0xffc85a44).withAlpha(0.55f)); g.fillEllipse(ax - 1.2f, top - ah - 1.2f, 2.4f, 2.4f); } }   // aircraft warning lamp
                // floors: faint horizontal grime lines + water stains running down
                g.setColour(C(0xff000000).withAlpha(0.10f + 0.08f * (float) li)); for (float fy = top + 6.0f; fy < H; fy += 7.0f) g.drawHorizontalLine((int) fy, b.getX(), b.getRight());
                for (int s = 0; s < 2; ++s) { const float sx = b.getX() + rnd.nextFloat() * bw; g.setColour(C(0xff000000).withAlpha(0.12f)); g.fillRect(sx, top, 1.5f + rnd.nextFloat() * 2.0f, 30.0f + rnd.nextFloat() * 90.0f); }
                // lit windows: small, warm or cold, never bright
                for (float wy = top + 5.0f; wy < juce::jmin(H, top + 320.0f); wy += 7.0f)
                    for (float wx = b.getX() + 3.0f; wx < b.getRight() - 4.0f; wx += 5.0f)
                        if (rnd.nextFloat() < L.windows) { const C wc = rnd.nextFloat() < 0.7f ? C(0xffe0b070) : C(0xff9fd0c8);
                            g.setColour(mixC(wc, smog, L.haze * 0.8f).withAlpha(0.35f + 0.35f * rnd.nextFloat())); g.fillRect(wx, wy, 2.0f, 3.0f); }
                // neon: on the middle and near layers, some towers carry a sign
                if (li >= 1 && rnd.nextFloat() < (li == 2 ? 0.72f : 0.45f)) {
                    const C cols[] { magenta, teal, amber, olive.brighter(0.4f), magenta };
                    const C col = cols[rnd.nextInt(5)]; const bool vert = rnd.nextFloat() < 0.6f;
                    juce::Rectangle<float> s = vert ? juce::Rectangle<float>(b.getX() + 3.0f + rnd.nextFloat() * juce::jmax(1.0f, bw - 18.0f), top + 8.0f + rnd.nextFloat() * 20.0f, 9.0f + rnd.nextFloat() * 4.0f, 36.0f + rnd.nextFloat() * 44.0f)
                                                    : juce::Rectangle<float>(b.getX() + 4.0f, top + 10.0f + rnd.nextFloat() * 24.0f, juce::jmax(16.0f, bw - 10.0f), 9.0f + rnd.nextFloat() * 3.0f);
                    const float fade = li == 1 ? 0.55f : 1.0f;
                    bleed(g, s, col, fade, H, rnd);
                    glow(g, s, col, 0.9f * fade);
                    g.setColour(mixC(C(0xff10181a), col, 0.25f)); g.fillRoundedRectangle(s, 1.5f);   // the sign box
                    // abstract glyph strokes in the tube colour (no real lettering)
                    juce::Random gr(rnd.nextInt()); g.setColour(mixC(col, C(0xfff0e6d0), 0.25f).withAlpha(0.85f * fade));
                    if (vert) for (float gy = s.getY() + 3.0f; gy < s.getBottom() - 5.0f; gy += 7.0f) { const float gw2 = s.getWidth() - 4.0f;
                        g.fillRect(s.getX() + 2.0f, gy, gw2 * (0.4f + 0.6f * gr.nextFloat()), 1.2f); if (gr.nextBool()) g.fillRect(s.getX() + 2.0f + gw2 * gr.nextFloat() * 0.6f, gy, 1.2f, 4.0f); }
                    else for (float gx = s.getX() + 3.0f; gx < s.getRight() - 6.0f; gx += 7.0f) { g.fillRect(gx, s.getY() + 2.5f, 1.2f, s.getHeight() - 5.0f); if (gr.nextBool()) g.fillRect(gx, s.getCentreY() - 0.6f, 4.0f, 1.2f); }
                    g.setColour(col.withAlpha(0.55f * fade)); g.drawRoundedRectangle(s, 1.5f, 0.8f);
                    if (li == 2) signs.push_back({ s, col, vert, 0, rnd.nextFloat() * 6.28f });
                }
                // big lit advertisement boards on the near towers: a pale-lit panel with abstract ad shapes, its colour pouring down the wet facade
                if (li == 2 && bw > 52.0f && rnd.nextFloat() < 0.38f) {
                    const C cols[] { magenta, teal, amber, C(0xff6c93a8) }; const C col = cols[rnd.nextInt(4)];
                    juce::Rectangle<float> s(b.getX() + 5.0f, top + 14.0f + rnd.nextFloat() * 30.0f, juce::jmin(70.0f, bw - 12.0f), 18.0f + rnd.nextFloat() * 14.0f);
                    bleed(g, s, col, 1.0f, H, rnd); glow(g, s, col, 0.8f);
                    g.setGradientFill(juce::ColourGradient(mixC(col, C(0xfff0e6d0), 0.30f).withAlpha(0.80f), s.getX(), s.getY(), mixC(col, C(0xff10181a), 0.35f).withAlpha(0.85f), s.getX(), s.getBottom(), false)); g.fillRect(s);
                    juce::Random ar(rnd.nextInt()); g.setColour(mixC(col, C(0xff10181a), 0.65f).withAlpha(0.75f));
                    g.fillEllipse(s.getX() + 3.0f, s.getY() + 3.0f, s.getHeight() - 6.0f, s.getHeight() - 6.0f);   // a 'logo' disc
                    for (float ly = s.getY() + 4.0f; ly < s.getBottom() - 3.0f; ly += 4.0f) g.fillRect(s.getX() + s.getHeight() + 1.0f, ly, (s.getWidth() - s.getHeight() - 5.0f) * (0.4f + 0.6f * ar.nextFloat()), 1.6f);   // 'copy' lines
                    g.setColour(C(0xff000000).withAlpha(0.18f)); for (float sy = s.getY(); sy < s.getBottom(); sy += 2.0f) g.drawHorizontalLine((int) sy, s.getX(), s.getRight());   // the panel's scan lines
                    g.setColour(col.withAlpha(0.7f)); g.drawRect(s, 0.8f);
                    signs.push_back({ s, col, false, 1, rnd.nextFloat() * 6.28f });
                }
                x += bw + (li == 0 ? -4.0f : rnd.nextFloat() * 6.0f);
            }
            // haze between the layers: the bases of each layer sink into lit smog
            g.setGradientFill(juce::ColourGradient(smog.withAlpha(0.0f), 0, 70.0f + 14.0f * (float) li, smog.withAlpha(0.30f - 0.07f * (float) li), 0, 128.0f + 10.0f * (float) li, false));
            g.fillRect(0.0f, 60.0f, W, 90.0f);
        }

        // ---- power lines sagging across the top (thin, dark, a little uneven)
        for (int k = 0; k < 4; ++k) {
            juce::Path p; const float y0 = 6.0f + 9.0f * (float) k + rnd.nextFloat() * 6.0f, y1 = 2.0f + 11.0f * (float) k + rnd.nextFloat() * 8.0f, sag = 18.0f + rnd.nextFloat() * 22.0f;
            const float xa = -10.0f, xb = W * (0.45f + 0.25f * rnd.nextFloat()); p.startNewSubPath(xa, y0); p.quadraticTo((xa + xb) * 0.5f, juce::jmax(y0, y1) + sag, xb, y1);
            p.quadraticTo((xb + W + 10.0f) * 0.5f, y1 + sag * 0.8f, W + 10.0f, y0 + 4.0f);
            g.setColour(C(0xff05090a).withAlpha(0.85f)); g.strokePath(p, juce::PathStrokeType(k % 2 ? 1.0f : 1.4f));
        }

        // ---- still rain over everything (the live layer adds the moving drops in the header)
        for (int i = 0; i < (int) (W * H / 900.0f); ++i) {
            const float x = rnd.nextFloat() * (W + 40.0f) - 20.0f, y = rnd.nextFloat() * H, len = 5.0f + rnd.nextFloat() * 12.0f;
            g.setColour(C(0xffbfcdc6).withAlpha(0.025f + 0.04f * rnd.nextFloat())); g.drawLine(x, y, x - len * 0.16f, y + len, 0.7f);
        }

        // ---- vignette
        g.setGradientFill(juce::ColourGradient(C(0x00000000), W * 0.5f, H * 0.42f, C(0x8a000000), 0.0f, -H * 0.15f, true)); g.fillAll();

        // ---- wet street glow at the very bottom (under the bottom card's edge): neon smeared into puddles
        g.setGradientFill(juce::ColourGradient(C(0xff0b1314).withAlpha(0.0f), 0, H - 150.0f, C(0xff131d1d), 0, H, false)); g.fillRect(0.0f, H - 150.0f, W, 150.0f);   // wet asphalt
        for (int k = 0; k < 26; ++k) { const C cols[] { magenta, teal, amber, magenta, C(0xff6c93a8) };
            const C col = cols[k % 5]; const float rx = W * rnd.nextFloat(), rw = 5.0f + rnd.nextFloat() * 26.0f, rl = 50.0f + rnd.nextFloat() * 110.0f;
            // a reflection: a vertical smear of the sign's colour, broken into ripples by the rain
            for (float yy = H; yy > H - rl; yy -= 3.0f) {
                const float a = 0.34f * (1.0f - (H - yy) / rl), wob = (rnd.nextFloat() - 0.5f) * 4.0f, ww = rw * (0.6f + 0.5f * rnd.nextFloat());
                if (rnd.nextFloat() < 0.25f) continue;
                g.setColour(col.withAlpha(a)); g.fillRect(rx + wob + (rw - ww) * 0.5f, yy - 2.0f, ww, 2.0f);
            }
            g.setGradientFill(juce::ColourGradient(col.withAlpha(0.16f), rx + rw * 0.5f, H, col.withAlpha(0.0f), rx + rw * 0.5f + 60.0f, H, true)); g.fillRect(rx - 60.0f, H - 60.0f, rw + 120.0f, 60.0f);   // the puddle's glow
        }

        // ---- the faded print: lifted blacks, a slight teal-olive cast, film grain
        juce::Image::BitmapData bd(img, juce::Image::BitmapData::readWrite);
        juce::Random grain(31);
        for (int y = 0; y < bd.height; ++y) {
            auto* row = bd.getLinePointer(y);
            for (int x = 0; x < bd.width; ++x) {
                auto* px = reinterpret_cast<juce::PixelARGB*>(row + x * bd.pixelStride);
                const int n = grain.nextInt(13) - 6;
                auto fade = [&](juce::uint8 c, int lift) { return (juce::uint8) juce::jlimit(0, 255, lift + (int) (c * 0.93f) + n); };
                px->setARGB(255, fade(px->getRed(), 10), fade(px->getGreen(), 13), fade(px->getBlue(), 12));
            }
        }
    }
};


// ---- the NEON wordmark: GHOSTY BEATS as an old neon sign bolted to the wall. Outline tubes (pale core, coloured tube, light bleeding into the
// rain), GHOSTY in pink, BEATS in teal, the headphones ghost as an amber tube, on a weathered dark plate with rivets and rust streaks.
// Cached per size; overlay flicker: now and then the Y of GHOSTY stutters (a tired tube).
struct Logo {
    int builds = 0;
    void draw(juce::Graphics& g, juce::Rectangle<float> r, const juce::String& version, float t) {
        const float scale = juce::jlimit(1.0f, 3.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        if (r != lastR || version != lastV || std::abs(scale - ls) > 0.01f || !lit.isValid()) {
            lit = render(r, version, scale, false); dim = render(r, version, scale, true); lastR = r; lastV = version; ls = scale; ++builds; }
        const float ph = std::fmod(t, 7.3f); const bool stutter = (ph > 5.0f && ph < 5.08f) || (ph > 5.2f && ph < 5.26f);
        g.setOpacity(1.0f);
        g.drawImageTransformed(stutter ? dim : lit, juce::AffineTransform::scale(1.0f / scale).translated(r.getX() - pad, r.getY() - pad));
    }
private:
    static constexpr float pad = 10.0f;
    juce::Image lit, dim; juce::Rectangle<float> lastR; juce::String lastV; float ls = 0;

    static void tube(juce::Graphics& g, const juce::Path& p, C col, float w, float alpha = 1.0f) {
        const juce::PathStrokeType st(1.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        auto stroke = [&](float width, C c) { juce::PathStrokeType s2(width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded); g.setColour(c); g.strokePath(p, s2); };
        stroke(w * 7.0f, col.withAlpha(0.10f * alpha)); stroke(w * 3.8f, col.withAlpha(0.26f * alpha));
        stroke(w * 1.6f, col.withAlpha(0.85f * alpha)); stroke(w * 0.6f, mixC(col, C(0xfff4ece0), 0.7f).withAlpha(0.95f * alpha)); (void) st;
    }
    static juce::Path textPath(const juce::String& text, juce::Rectangle<float> box, float h) {
        juce::Font f(juce::FontOptions(h, juce::Font::bold)); f.setHorizontalScale(0.9f); f.setExtraKerningFactor(0.06f);
        juce::GlyphArrangement ga; ga.addFittedText(f, text, box.getX(), box.getY(), box.getWidth(), box.getHeight(), juce::Justification::centredLeft, 1, 1.0f);
        juce::Path p; ga.createPath(p); return p;
    }
    static juce::Image render(juce::Rectangle<float> r, const juce::String& version, float scale, bool flick) {
        juce::Image im(juce::Image::ARGB, juce::roundToInt((r.getWidth() + 2 * pad) * scale), juce::roundToInt((r.getHeight() + 2 * pad) * scale), true, juce::SoftwareImageType());
        juce::Graphics g(im); g.addTransform(juce::AffineTransform::scale(scale)); g.addTransform(juce::AffineTransform::translation(pad, pad));
        const juce::Rectangle<float> b(0, 0, r.getWidth(), r.getHeight());
        const C pink(0xffe0648f), teal(0xff5cc7b6), amber(0xffe0a24a);
        // the plate: dark weathered metal, a cold edge, rust running from the rivets, grime
        auto plate = b.reduced(1.0f);
        g.setColour(C(0x88000000)); g.fillRoundedRectangle(plate.translated(0, 2.0f), 5.0f);
        g.setGradientFill(juce::ColourGradient(C(0xff1b2526), 0, plate.getY(), C(0xff0d1415), 0, plate.getBottom(), false)); g.fillRoundedRectangle(plate, 5.0f);
        juce::Random rr(55);
        for (int i = 0; i < 4; ++i) { const float rx = i % 2 ? plate.getRight() - 6.0f : plate.getX() + 6.0f, ry = i < 2 ? plate.getY() + 5.0f : plate.getBottom() - 5.0f;
            if (i < 2) { g.setGradientFill(juce::ColourGradient(C(0xff6b4428).withAlpha(0.45f), rx, ry, C(0xff6b4428).withAlpha(0.0f), rx, ry + 30.0f, false)); g.fillRect(rx - 1.0f, ry, 2.0f, 30.0f); }
            g.setColour(C(0xff3a4644)); g.fillEllipse(rx - 2.0f, ry - 2.0f, 4.0f, 4.0f); g.setColour(C(0xff0a0f10)); g.fillEllipse(rx - 0.8f, ry - 0.8f, 1.6f, 1.6f); }
        for (int i = 0; i < 40; ++i) { g.setColour(C(0xff000000).withAlpha(0.10f + 0.15f * rr.nextFloat())); g.fillEllipse(plate.getX() + rr.nextFloat() * plate.getWidth(), plate.getY() + rr.nextFloat() * plate.getHeight(), 1.0f + rr.nextFloat() * 3.0f, 1.0f + rr.nextFloat() * 2.0f); }
        g.setColour(C(0xff3b4a47)); g.drawRoundedRectangle(plate, 5.0f, 1.0f);
        g.setColour(teal.withAlpha(0.10f)); g.drawRoundedRectangle(plate.reduced(2.0f), 4.0f, 1.0f);
        // the tubes: GHOSTY (pink) over BEATS (teal); the ghost (amber) on the right
        const float gx = plate.getRight() - 46.0f;
        auto words = plate.reduced(8.0f, 6.0f).withRight(gx - 6.0f);
        const float lh = words.getHeight() * 0.5f;
        juce::Path ghosty = textPath("GHOST", { words.getX(), words.getY(), words.getWidth(), lh }, lh * 1.05f);
        // the Y apart (it is the one that flickers)
        const float yX = ghosty.getBounds().getRight() + 1.5f;
        juce::Path yPath = textPath("Y", { yX, words.getY(), 40.0f, lh }, lh * 1.05f);
        juce::Path beats = textPath("BEATS", { words.getX(), words.getY() + lh, words.getWidth(), lh }, lh * 1.05f);
        // fit both lines to the word box
        { auto bb = ghosty.getBounds().getUnion(yPath.getBounds()).getUnion(beats.getBounds());
          const float kx = words.getWidth() / juce::jmax(1.0f, bb.getWidth()), ky = words.getHeight() / juce::jmax(1.0f, bb.getHeight());
          const auto tf = juce::AffineTransform::translation(-bb.getX(), -bb.getY()).scaled(juce::jmin(kx, ky * 1.25f), ky)
                              .sheared(-0.10f, 0.0f).translated(words.getX() + words.getHeight() * 0.10f, words.getY());   // fills the plate, a slight forward lean
          ghosty.applyTransform(tf); yPath.applyTransform(tf); beats.applyTransform(tf); }
        tube(g, ghosty, pink, 1.0f); tube(g, yPath, pink, 1.0f, flick ? 0.18f : 1.0f); tube(g, beats, teal, 1.0f);
        // the headphones ghost: one outline tube
        { const float s = 30.0f; const float x0 = gx + 6.0f, y0 = plate.getCentreY() - s * 0.62f;
          juce::Path gp; gp.startNewSubPath(x0, y0 + s * 1.05f); gp.lineTo(x0, y0 + s * 0.45f);
          gp.addCentredArc(x0 + s * 0.5f, y0 + s * 0.45f, s * 0.5f, s * 0.48f, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi);
          gp.lineTo(x0 + s, y0 + s * 1.05f);
          for (int k = 0; k < 4; ++k) { const float xa = x0 + s - s * 0.25f * (float) k; gp.quadraticTo(xa - s * 0.125f, y0 + s * (k % 2 ? 1.05f : 0.88f), xa - s * 0.25f, y0 + s * 1.05f); }
          tube(g, gp, amber, 0.9f);
          juce::Path eyes; eyes.addEllipse(x0 + s * 0.28f, y0 + s * 0.40f, s * 0.12f, s * 0.16f); eyes.addEllipse(x0 + s * 0.60f, y0 + s * 0.40f, s * 0.12f, s * 0.16f); tube(g, eyes, amber, 0.7f);
          juce::Path phones; phones.addCentredArc(x0 + s * 0.5f, y0 + s * 0.45f, s * 0.64f, s * 0.62f, 0.0f, -juce::MathConstants<float>::halfPi - 0.1f, juce::MathConstants<float>::halfPi + 0.1f, true);
          phones.addRoundedRectangle(x0 - s * 0.24f, y0 + s * 0.30f, s * 0.18f, s * 0.34f, 2.0f); phones.addRoundedRectangle(x0 + s * 1.06f, y0 + s * 0.30f, s * 0.18f, s * 0.34f, 2.0f);
          tube(g, phones, teal, 0.75f); }
        // the version, stencilled small in faded paint
        g.setColour(C(0xffd9dccb).withAlpha(0.55f)); g.setFont(juce::Font(juce::FontOptions(8.0f, juce::Font::bold)));
        g.drawText("v" + version, plate.withTrimmedRight(4.0f).withTrimmedBottom(2.0f), juce::Justification::bottomRight);
        // the rain over the sign + the same faded print as the city
        juce::Random rn(9); for (int i = 0; i < 30; ++i) { const float x = rn.nextFloat() * b.getWidth(), y = rn.nextFloat() * b.getHeight(), l = 4.0f + rn.nextFloat() * 6.0f;
            g.setColour(C(0xffc9d6cf).withAlpha(0.10f)); g.drawLine(x, y, x - l * 0.16f, y + l, 0.6f); }
        return im;
    }
};

} // namespace bx::neoncity
