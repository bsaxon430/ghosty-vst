// Ghost Graph -- the window: a dark crumpled-paper page, amber graphs drawn as they play (glowing dots on the
// playheads), the scene's equations underneath (type in them: the sound changes as you type) and a slim control strip.
#pragma once
#include "BinaryData.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "GraphProcessor.h"
#include "GraphMath.h"
#include "GraphEdit.h"
#include <map>

namespace ggui
{
    // LOOK: PAPER (the videos), CHROMA (rainbow watercolour), INK (indigo / teal / orange watercolour with binary rain)
    struct Theme
    {
        const char* name;
        juce::Colour amber;     // text, buttons, knobs
        juce::Colour lineCol;   // the graph lines (when perVoice is off)
        juce::Colour amberHi;   // dot fill, knob tip
        juce::Colour dotRing, paper, coral, grid, axis, label, onText, halo;
        bool perVoice;
        juce::Colour voice[6];
        bool lcd = false;       // TI 84: a green calculator LCD, monospaced type, dotted grid, hollow dots
    };
    inline const Theme& theme (int i)
    {
        static const Theme t[5] = {
            { "PAPER",  juce::Colour (0xffe4b038), juce::Colour (0xffec941a), juce::Colour (0xfffef2a7), juce::Colour (0xccec941a),
              juce::Colour (0xff3d3733), juce::Colour (0xffff7a5c), juce::Colour (0x8c6a4520), juce::Colour (0xff8a5424), juce::Colour (0xffb2702c),
              juce::Colour (0xff1e1b19), juce::Colour (0x00000000), false, {} },
            { "CHROMA", juce::Colour (0xff1c1a33), juce::Colour (0xff1c1a33), juce::Colour (0xffffffff), juce::Colour (0xff1c1a33),
              juce::Colour (0xfff7f3ec), juce::Colour (0xffc0392b), juce::Colour (0x2a1c1a33), juce::Colour (0xaa1c1a33), juce::Colour (0xcc1c1a33),
              juce::Colour (0xfff7f3ec), juce::Colour (0xd8ffffff), true,
              { juce::Colour (0xff1c1a33), juce::Colour (0xff2c4fb8), juce::Colour (0xffb0206e), juce::Colour (0xff0f7f78), juce::Colour (0xffc45a12), juce::Colour (0xff5b2a9a) } },
            { "INK",    juce::Colour (0xff1f2436), juce::Colour (0xff1d2747), juce::Colour (0xffd77b3d), juce::Colour (0xff1d2747),
              juce::Colour (0xfff3eee4), juce::Colour (0xffb03a2e), juce::Colour (0x261d2747), juce::Colour (0x991d2747), juce::Colour (0xbb1d2747),
              juce::Colour (0xfff3eee4), juce::Colour (0xccf5f0e6), true,
              { juce::Colour (0xff1d2747), juce::Colour (0xff23707c), juce::Colour (0xffc4622d), juce::Colour (0xff5e3a8c), juce::Colour (0xff15161c), juce::Colour (0xffa87a22) } },
            // WINE: the burgundy video (Track 6) -- cream lines, orange dots on wine-red crumpled paper
            { "WINE",   juce::Colour (0xfff4cbaf), juce::Colour (0xfffed5b4), juce::Colour (0xfff25a10), juce::Colour (0xffffd5b4),
              juce::Colour (0xff522829), juce::Colour (0xffffb070), juce::Colour (0x50a0604a), juce::Colour (0xffc8907a), juce::Colour (0xffe0a890),
              juce::Colour (0xff3a1a1c), juce::Colour (0x00000000), false, {} },
            // TI 84: a graphing calculator -- the graph on a light LCD in a black body, Y1..Y6 in the calculator's colours
            // TI 84: a graphing calculator -- black ink on a green LCD in a worn dark body, all type monospaced
            { "TI 84",  juce::Colour (0xff17210f), juce::Colour (0xff17210f), juce::Colour (0xffc2d8a4), juce::Colour (0xff17210f),
              juce::Colour (0xffc2d8a4), juce::Colour (0xff7a1d10), juce::Colour (0x5917210f), juce::Colour (0xff17210f), juce::Colour (0xff17210f),
              juce::Colour (0xffc2d8a4), juce::Colour (0x00000000), false, {}, true },
        };
        return t[juce::jlimit (0, 4, i)];
    }
    inline const Theme*& curTheme() { static const Theme* c = &theme (0); return c; }
    inline const Theme& th() { return *curTheme(); }

    inline juce::Font lcdFont (float size)
    {
        static juce::Typeface::Ptr tf = juce::Typeface::createSystemTypefaceFor (GhostGraphArt::ShareTechMonoRegular_ttf,
                                                                                 (size_t) GhostGraphArt::ShareTechMonoRegular_ttfSize);
        return juce::Font (juce::FontOptions (tf).withHeight (size).withHorizontalScale (0.84f));
    }
    inline juce::Font serif (float size, bool italic = false)
    {
        if (th().lcd) return lcdFont (size * 1.08f);
        juce::FontOptions o (juce::Font::getDefaultSerifFontName(), size, italic ? juce::Font::italic : juce::Font::plain);
       #if JUCE_MAC
        o = o.withName ("Times New Roman");
       #endif
        return juce::Font (o);
    }

    // a dark sheet of crumpled paper like the videos': flat facets with straight creases, each lit a little differently,
    // a darker fold across the middle, and a fine grain. Same every time (fixed seed).
    inline juce::Image makePaper (int w, int h, juce::Colour base = juce::Colour (0xff3d3733))
    {
        w = juce::jmax (8, w); h = juce::jmax (8, h);
        juce::Random rnd (20261118);
        struct Facet { float x, y, base, gx, gy; };
        std::vector<Facet> fs;
        const int n = 70;
        for (int i = 0; i < n; ++i)
            fs.push_back ({ rnd.nextFloat() * w, rnd.nextFloat() * h, (rnd.nextFloat() - 0.5f) * 0.20f,
                            (rnd.nextFloat() - 0.5f) * 0.12f / 300.f, (rnd.nextFloat() - 0.5f) * 0.12f / 300.f });
        // shade at a low resolution (fast), then smooth it up
        const int sw = juce::jmax (2, w / 3), sh = juce::jmax (2, h / 3);
        std::vector<float> lo ((size_t) (sw * sh));
        for (int y = 0; y < sh; ++y)
            for (int x = 0; x < sw; ++x)
            {
                const float px = (x + 0.5f) * w / sw, py = (y + 0.5f) * h / sh;
                int best = 0; float bd = 1e30f;
                for (int i = 0; i < n; ++i)
                {
                    const float dx = px - fs[(size_t) i].x, dy = py - fs[(size_t) i].y, d = dx * dx + dy * dy;
                    if (d < bd) { bd = d; best = i; }
                }
                const auto& f = fs[(size_t) best];
                lo[(size_t) (y * sw + x)] = f.base + f.gx * (px - f.x) + f.gy * (py - f.y);
            }
        for (int pass = 0; pass < 2; ++pass)   // soften the creases a touch
        {
            std::vector<float> t = lo;
            for (int y = 1; y + 1 < sh; ++y)
                for (int x = 1; x + 1 < sw; ++x)
                    lo[(size_t) (y * sw + x)] = (t[(size_t) (y * sw + x)] * 4 + t[(size_t) (y * sw + x - 1)] + t[(size_t) (y * sw + x + 1)]
                                                 + t[(size_t) ((y - 1) * sw + x)] + t[(size_t) ((y + 1) * sw + x)]) / 8.0f;
        }
        juce::Image img (juce::Image::RGB, w, h, false);
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
        juce::Random grain (7);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const float fx = juce::jlimit (0.0f, (float) sw - 1.001f, (x + 0.5f) * sw / w - 0.5f);
                const float fy = juce::jlimit (0.0f, (float) sh - 1.001f, (y + 0.5f) * sh / h - 0.5f);
                const int ix = (int) fx, iy = (int) fy;
                const float tx = fx - ix, ty = fy - iy;
                auto L = [&] (int xx, int yy) { return lo[(size_t) (juce::jmin (sh - 1, yy) * sw + juce::jmin (sw - 1, xx))]; };
                const float a0 = L (ix, iy) + (L (ix + 1, iy) - L (ix, iy)) * tx;
                const float a1 = L (ix, iy + 1) + (L (ix + 1, iy + 1) - L (ix, iy + 1)) * tx;
                const float v = (float) y / h;
                float shade = a0 + (a1 - a0) * ty;
                const float fold = juce::jlimit (0.0f, 1.0f, (v - 0.31f) / 0.06f);
                shade += 0.05f - 0.13f * fold * fold * (3 - 2 * fold);                // lighter above the fold, darker below it
                shade -= 0.07f * std::exp (-std::pow ((v - 0.335f) / 0.02f, 2.0f));   // the fold itself
                const float k = juce::jlimit (0.5f, 1.5f, 1.0f + shade + (grain.nextFloat() - 0.5f) * 0.18f);
                bd.setPixelColour (x, y, juce::Colour::fromFloatRGBA (juce::jmin (1.f, base.getFloatRed() * k), juce::jmin (1.f, base.getFloatGreen() * k), juce::jmin (1.f, base.getFloatBlue() * k), 1.f));
            }
        return img;
    }

    // ---- watercolour tools for the CHROMA and INK looks (procedural, fixed seeds: the same page every time)
    inline void wash (juce::Graphics& g, juce::Random& r, float cx, float cy, float rad, juce::Colour c, float alpha, float stretch = 1.25f)
    {
        float ph[4]; for (auto& q : ph) q = r.nextFloat() * 6.2831853f;
        for (int layer = 0; layer < 3; ++layer)
        {
            juce::Path p;
            const int n = 30;
            const float rr = rad * (1.0f - 0.2f * layer);
            const float ox = (r.nextFloat() - 0.5f) * rad * 0.25f, oy = (r.nextFloat() - 0.5f) * rad * 0.25f;
            for (int i = 0; i <= n; ++i)
            {
                const float a = i * 6.2831853f / n;
                const float k = 1.0f + 0.24f * std::sin (3 * a + ph[0]) + 0.15f * std::sin (5 * a + ph[1] + layer) + 0.09f * std::sin (9 * a + ph[2]) + 0.05f * std::sin (14 * a + ph[3]);
                const float x = cx + ox + std::cos (a) * rr * k * stretch, y = cy + oy + std::sin (a) * rr * k;
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            p.closeSubPath();
            p = p.createPathWithRoundedCorners (rr * 0.25f);
            g.setColour (c.withAlpha (alpha * (layer == 0 ? 0.42f : 0.30f)));
            g.fillPath (p);
            if (layer == 0)   // the darker rim a watercolour wash dries with
            {
                g.setColour (c.darker (0.35f).withAlpha (alpha * 0.30f));
                g.strokePath (p, juce::PathStrokeType (1.4f));
            }
        }
    }
    inline void splatter (juce::Graphics& g, juce::Random& r, float cx, float cy, float spreadX, float spreadY, juce::Colour c, int n, float scale)
    {
        for (int i = 0; i < n; ++i)
        {
            const float x = cx + (r.nextFloat() - 0.5f) * spreadX * 2, y = cy + (r.nextFloat() - 0.5f) * spreadY * 2;
            const float d = (0.8f + std::pow (r.nextFloat(), 3.0f) * 9.0f) * scale;
            g.setColour (c.withAlpha (0.35f + 0.5f * r.nextFloat()));
            g.fillEllipse (x - d / 2, y - d / 2, d, d);
        }
    }
    inline void paperGrain (juce::Image& img, juce::Colour base, uint32_t seed, float amount)
    {
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::readWrite);
        juce::Random gr ((juce::int64) seed);
        for (int y = 0; y < img.getHeight(); ++y)
            for (int x = 0; x < img.getWidth(); ++x)
            {
                const auto c = bd.getPixelColour (x, y);
                const float k = 1.0f + (gr.nextFloat() - 0.5f) * amount;
                bd.setPixelColour (x, y, juce::Colour::fromFloatRGBA (juce::jlimit (0.f, 1.f, c.getFloatRed() * k), juce::jlimit (0.f, 1.f, c.getFloatGreen() * k),
                                                                      juce::jlimit (0.f, 1.f, c.getFloatBlue() * k), 1.f));
            }
        juce::ignoreUnused (base);
    }

    // CHROMA: a rainbow of watercolour across the middle of a white page, with ink doodles: ripples, an atom, stars,
    // a sound wave and a few math signs (from Brandon's first reference picture)
    inline juce::Image makeChroma (int w, int h)
    {
        w = juce::jmax (8, w); h = juce::jmax (8, h);
        juce::Image img (juce::Image::RGB, w, h, false);
        juce::Graphics g (img);
        g.fillAll (juce::Colour (0xfff8f5ef));
        juce::Random r (4987);
        const float k = w / 1054.0f;
        // a pale wash first, then the colours, left to right like the picture: blue, violet, magenta, pink, yellow, orange, teal
        const juce::Colour hues[] = { juce::Colour (0xff2f6fd6), juce::Colour (0xff3d8de0), juce::Colour (0xff7a3fc0), juce::Colour (0xffb03aa8),
                                      juce::Colour (0xffe0368f), juce::Colour (0xfff06aa0), juce::Colour (0xfff4c63a), juce::Colour (0xfff39a2a),
                                      juce::Colour (0xff1fa6a0), juce::Colour (0xff4fb84a), juce::Colour (0xff3b6fd8), juce::Colour (0xffd23b7a) };
        for (int i = 0; i < 26; ++i)
            wash (g, r, r.nextFloat() * w, h * (0.25f + 0.5f * r.nextFloat()), (70 + 90 * r.nextFloat()) * k, hues[r.nextInt (12)].withSaturation (0.35f).brighter (0.3f), 0.45f, 1.6f);
        for (int i = 0; i < 95; ++i)
        {
            const float u = r.nextFloat();
            const float cx = u * w, cy = h * (0.46f + 0.10f * std::sin (u * 7.0f + 1.3f)) + (r.nextFloat() - 0.5f) * h * 0.34f;
            const int hi = juce::jlimit (0, 11, (int) (u * 11.0f + (r.nextFloat() - 0.5f) * 3.0f));
            wash (g, r, cx, cy, (22 + 85 * r.nextFloat()) * k, hues[hi], 0.75f);
        }
        const juce::Colour deep[] = { juce::Colour (0xff2a0f45), juce::Colour (0xff1c1a4a), juce::Colour (0xff5a0f3a), juce::Colour (0xff0e2a52) };
        for (int i = 0; i < 16; ++i)   // dark pools of ink in the middle
            wash (g, r, w * (0.25f + 0.5f * r.nextFloat()), h * (0.36f + 0.24f * r.nextFloat()), (16 + 40 * r.nextFloat()) * k, deep[r.nextInt (4)], 0.8f);
        for (int i = 0; i < 12; ++i)
            splatter (g, r, r.nextFloat() * w, h * (0.25f + 0.5f * r.nextFloat()), 70 * k, 50 * k, hues[r.nextInt (12)], 14, k);

        // ink doodles
        const juce::Colour ink (0xff1c1a3a);
        auto ripples = [&] (float cx, float cy, float rx, int n)
        {
            for (int i = 0; i < n; ++i) { g.setColour (ink.withAlpha (0.55f - 0.04f * i)); const float a = rx * (0.25f + 0.13f * i); g.drawEllipse (cx - a, cy - a * 0.32f, a * 2, a * 0.64f, 1.3f * k); }
        };
        ripples (w * 0.87f, h * 0.30f, 90 * k, 7);
        ripples (w * 0.82f, h * 0.86f, 110 * k, 8);
        {   // an atom
            const float cx = w * 0.66f, cy = h * 0.11f, a = 46 * k;
            g.setColour (ink.withAlpha (0.6f));
            for (int i = 0; i < 3; ++i)
            {
                juce::Path e; e.addEllipse (-a, -a * 0.36f, a * 2, a * 0.72f);
                e.applyTransform (juce::AffineTransform::rotation (i * juce::MathConstants<float>::pi / 3).translated (cx, cy));
                g.strokePath (e, juce::PathStrokeType (1.3f * k));
            }
            g.fillEllipse (cx - 6 * k, cy - 6 * k, 12 * k, 12 * k);
        }
        auto star = [&] (float cx, float cy, float a)
        {
            g.setColour (ink.withAlpha (0.6f));
            for (int i = 0; i < 4; ++i) { const float t = i * juce::MathConstants<float>::pi / 4; g.drawLine (cx - a * std::cos (t), cy - a * std::sin (t), cx + a * std::cos (t), cy + a * std::sin (t), 1.2f * k); }
        };
        star (w * 0.25f, h * 0.07f, 14 * k); star (w * 0.32f, h * 0.05f, 10 * k);
        {   // a sound wave across the middle
            juce::Path wv;
            const float y0 = h * 0.50f;
            for (int i = 0; i <= 400; ++i)
            {
                const float x = w * (0.28f + 0.36f * i / 400.0f);
                const float env = std::sin (juce::MathConstants<float>::pi * i / 400.0f);
                const float y = y0 + env * 16 * k * std::sin (i * 0.9f) * (0.4f + 0.6f * std::abs (std::sin (i * 0.073f)));
                if (i == 0) wv.startNewSubPath (x, y); else wv.lineTo (x, y);
            }
            g.setColour (juce::Colours::white.withAlpha (0.8f)); g.strokePath (wv, juce::PathStrokeType (1.1f * k));
        }
        g.setColour (ink.withAlpha (0.45f));
        const char* glyphs[] = { "\xe2\x88\xab", "\xce\xa3", "\xe2\x88\x9a", "E", "\xcf\x80", "\xce\xbb", "\xe2\x88\x82", "\xe2\x88\x9e" };
        for (int i = 0; i < 9; ++i)
        {
            g.setFont (serif ((18 + 16 * r.nextFloat()) * k, true));
            g.drawText (juce::String::fromUTF8 (glyphs[r.nextInt (8)]), juce::Rectangle<float> (r.nextFloat() * (w - 60), h * (0.05f + 0.9f * r.nextFloat()), 50 * k, 40 * k), juce::Justification::centred);
        }
        paperGrain (img, {}, 11, 0.06f);
        return img;
    }

    // INK: indigo, teal, purple and burnt-orange watercolour pouring down the middle of cream paper, columns of falling
    // binary, graph-paper grids, dials and a bundle of waves (from Brandon's second reference picture)
    inline juce::Image makeInk (int w, int h)
    {
        w = juce::jmax (8, w); h = juce::jmax (8, h);
        juce::Image img (juce::Image::RGB, w, h, false);
        juce::Graphics g (img);
        g.fillAll (juce::Colour (0xfff4efe6));
        juce::Random r (4986);
        const float k = w / 1054.0f;
        const juce::Colour indigo (0xff283b7c), navy (0xff1b2238), teal (0xff2d7a8c), purple (0xff5b3f87), orange (0xffd77b3d), ochre (0xffd9a066), blue (0xff3d5a9a);
        // the watercolour is a GHOST: a round head, a flowing body leaning a little, wispy arms, a wavy hem that drips;
        // warm ochre and teal at the head, deep indigo through the body, blue-teal at the hem
        const float gx = w * 0.66f, headR = juce::jmin (w * 0.22f, h * 0.29f), headY = headR + h * 0.03f, hemY = h * 0.90f;   // the whole head on the page, up on the right
        juce::Path ghost;
        {
            // the head, then a body that bellies out, pinches in and flares into a swaying hem with a tail curling left
            ghost.startNewSubPath (gx - headR, headY);
            ghost.addCentredArc (gx, headY, headR, headR, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi);
            ghost.cubicTo (gx + headR * 1.18f, h * 0.46f, gx + headR * 0.80f, h * 0.66f, gx + headR * 1.22f, hemY - h * 0.02f);
            const float xr = gx + headR * 1.22f, xl = gx - headR * 1.05f;
            const int waves = 4;
            for (int i = 0; i < waves; ++i)   // the wavy hem, right to left, each scallop a little lower
            {
                const float x0 = xr - (xr - xl) * (float) i / waves, x1 = xr - (xr - xl) * (float) (i + 1) / waves;
                ghost.quadraticTo ((x0 + x1) * 0.5f, hemY + h * (0.05f + 0.012f * i), x1, hemY - h * 0.01f * (i % 2) + h * 0.008f * i);
            }
            ghost.cubicTo (xl - headR * 0.55f, hemY + h * 0.06f, xl - headR * 0.60f, hemY - h * 0.10f, xl - headR * 0.18f, hemY - h * 0.13f);   // the tail
            ghost.cubicTo (gx - headR * 1.10f, h * 0.62f, gx - headR * 1.20f, h * 0.44f, gx - headR, headY);
            ghost.closeSubPath();
            ghost.applyTransform (juce::AffineTransform::rotation (0.16f, gx, headY));   // drifting down and to the left, as if floating
        }
        auto inGhost = [&] (float x, float y) { return ghost.contains (x, y); };
        // the body, painted from the inside out
        for (int i = 0; i < 170; ++i)
        {
            float cx = 0, cy = 0;
            for (int t = 0; t < 40; ++t) { cx = gx + (r.nextFloat() - 0.5f) * headR * 2.5f; cy = headY - headR + r.nextFloat() * (hemY - headY + headR); if (inGhost (cx, cy)) break; }
            const float v = juce::jlimit (0.0f, 1.0f, (cy - (headY - headR)) / (hemY - headY + headR));
            juce::Colour c = v < 0.34f ? (cx > gx - headR * 0.3f ? (r.nextInt (4) == 0 ? teal : (r.nextInt (3) == 0 ? ochre : orange)) : (r.nextInt (3) == 0 ? orange : teal))
                                       : (v < 0.7f ? (r.nextInt (3) == 0 ? purple : (r.nextBool() ? indigo : navy)) : (r.nextBool() ? teal : blue));
            wash (g, r, cx, cy, (18 + 52 * r.nextFloat()) * k, c, 0.75f, 1.05f);
        }
        {   // keep the outline crisp: paint that strayed outside goes faint, and the silhouette gets a dried watercolour rim
            juce::Graphics::ScopedSaveState ss (g);
            juce::RectangleList<int> outside (juce::Rectangle<int> (0, 0, w, h));
            juce::Path inv; inv.addRectangle (0, 0, (float) w, (float) h); inv.addPath (ghost); inv.setUsingNonZeroWinding (false);
            g.reduceClipRegion (inv);
            g.setColour (juce::Colour (0xfff4efe6).withAlpha (0.55f));
            g.fillAll();
        }
        g.setColour (navy.withAlpha (0.16f));
        g.strokePath (ghost, juce::PathStrokeType (1.4f * k, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (indigo.withAlpha (0.09f));
        g.strokePath (ghost, juce::PathStrokeType (9.0f * k, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        // the face: two hollow eyes and a little "o" mouth, left as bare paper, each eye holding a dial
        const float eyeY = headY - headR * 0.05f, eyeDx = headR * 0.38f, eyeRx = headR * 0.17f, eyeRy = headR * 0.26f;
        for (int e = -1; e <= 1; e += 2)
        {
            juce::Path eye; eye.addEllipse (gx + e * eyeDx - eyeRx, eyeY - eyeRy, eyeRx * 2, eyeRy * 2);
            eye.applyTransform (juce::AffineTransform::rotation (e * 0.12f, gx + e * eyeDx, eyeY));
            g.setColour (juce::Colour (0xfff4efe6).withAlpha (0.96f)); g.fillPath (eye);
            g.setColour (navy.withAlpha (0.55f)); g.strokePath (eye, juce::PathStrokeType (1.6f * k));
        }
        {
            juce::Path mouth; mouth.addEllipse (gx - headR * 0.10f, headY + headR * 0.36f, headR * 0.20f, headR * 0.26f);
            g.setColour (juce::Colour (0xfff4efe6).withAlpha (0.94f)); g.fillPath (mouth);
            g.setColour (navy.withAlpha (0.5f)); g.strokePath (mouth, juce::PathStrokeType (1.4f * k));
        }
        for (int i = 0; i < 10; ++i) splatter (g, r, w * (0.3f + 0.5f * r.nextFloat()), h * r.nextFloat(), 80 * k, 60 * k, r.nextBool() ? indigo : orange, 12, k);
        for (int i = 0; i < 26; ++i)   // drips running off the bottom
        {
            const float x = gx - headR * 0.45f + (r.nextFloat() - 0.5f) * headR * 2.0f, y0 = hemY - h * 0.05f + h * 0.04f * r.nextFloat(), len = h * (0.06f + 0.14f * r.nextFloat());
            juce::ColourGradient cg ((r.nextBool() ? blue : teal).withAlpha (0.55f), x, y0, juce::Colours::transparentWhite, x, y0 + len, false);
            g.setGradientFill (cg);
            const float wd = (2 + 5 * r.nextFloat()) * k;
            g.fillRoundedRectangle (x - wd / 2, y0, wd, len, wd / 2);
        }
        // binary rain
        auto column = [&] (float x, float y0, float y1, float size, juce::Colour c)
        {
            g.setFont (serif (size));
            for (float y = y0; y < y1; y += size * 1.05f)
            {
                g.setColour (c.withAlpha (0.35f + 0.5f * r.nextFloat()));
                g.drawText (r.nextBool() ? "1" : "0", juce::Rectangle<float> (x - size, y, size * 2, size), juce::Justification::centred);
            }
        };
        const juce::Colour cols[] = { ochre, indigo, navy, purple, orange };
        for (int i = 0; i < 6; ++i) column (w * (0.06f + 0.035f * i), h * (0.03f + 0.1f * r.nextFloat()), h * (0.35f + 0.25f * r.nextFloat()), (13 + 7 * r.nextFloat()) * k, cols[r.nextInt (5)]);
        for (int i = 0; i < 5; ++i) column (w * (0.58f + 0.03f * i), h * (0.60f + 0.05f * r.nextFloat()), h * (0.74f + 0.06f * r.nextFloat()), (10 + 4 * r.nextFloat()) * k, navy);
        // graph-paper grids
        auto grid = [&] (float x, float y, float gw, float gh, float step)
        {
            g.setColour (navy.withAlpha (0.30f));
            for (float gx = x; gx <= x + gw; gx += step) g.drawLine (gx, y, gx, y + gh, 0.7f);
            for (float gy = y; gy <= y + gh; gy += step) g.drawLine (x, gy, x + gw, gy, 0.7f);
        };
        grid (w * 0.30f, h * 0.10f, 170 * k, 110 * k, 11 * k);
        grid (w * 0.02f, h * 0.22f, 120 * k, 60 * k, 9 * k);
        // dials down the middle
        auto dial = [&] (float cx, float cy, float rad, juce::Colour c)
        {
            g.setColour (c.withAlpha (0.55f));
            for (int i = 1; i <= 4; ++i) g.drawEllipse (cx - rad * i / 4, cy - rad * i / 4, rad * i / 2, rad * i / 2, 0.8f);
            for (int t = 0; t < 48; ++t)
            {
                const float a = t * juce::MathConstants<float>::twoPi / 48;
                g.drawLine (cx + std::cos (a) * rad * 0.8f, cy + std::sin (a) * rad * 0.8f, cx + std::cos (a) * rad, cy + std::sin (a) * rad, 0.7f);
            }
            g.fillEllipse (cx - 3 * k, cy - 3 * k, 6 * k, 6 * k);
        };
        dial (gx - eyeDx, eyeY, eyeRx * 1.55f, navy);   // the eyes look out through dials
        dial (gx + eyeDx, eyeY, eyeRx * 1.55f, navy);
        dial (w * 0.80f, h * 0.42f, 46 * k, orange);
        dial (w * 0.30f, h * 0.66f, 40 * k, orange);
        // a bundle of waves along the bottom
        for (int i = 0; i < 34; ++i)
        {
            juce::Path wv;
            const float ph = r.nextFloat() * 6.28f, amp = (10 + 30 * r.nextFloat()) * k, y0 = h * (0.70f + 0.08f * r.nextFloat()), fr = 1.5f + 2.0f * r.nextFloat();
            for (int x = 0; x <= w; x += 6)
            {
                const float y = y0 + amp * std::sin (fr * x / (float) w * 6.28f + ph) + 0.4f * amp * std::sin (3.1f * x / (float) w * 6.28f + ph * 2);
                if (x == 0) wv.startNewSubPath ((float) x, y); else wv.lineTo ((float) x, y);
            }
            g.setColour (navy.withAlpha (0.14f + 0.14f * r.nextFloat()));
            g.strokePath (wv, juce::PathStrokeType (0.8f * k));
        }
        paperGrain (img, {}, 12, 0.07f);
        return img;
    }

    // TI 84: a black calculator body, the graph on a light LCD behind a bezel, the equations on a dark panel,
    // and a row of the calculator's top keys along the bottom (no maker's logo: it says GHOST-84)
    // TI 84 layout, shared by the background and the editor (fractions of the window height)
    namespace ti { constexpr float lcdTop = 0.066f, lcdBottom = 0.862f, keysTop = 0.878f, keysBottom = 0.952f; }
    inline juce::Rectangle<float> tiKey (int i, float w, float h)   // the six keys under the screen; the sixth is EDIT
    {
        const float x0 = w * 0.04f, span = w * 0.92f, gap = w * 0.012f, kw = (span - 5 * gap) / 6.f;
        return { x0 + i * (kw + gap), h * (ti::keysTop + 0.012f), kw, h * (ti::keysBottom - ti::keysTop - 0.024f) };
    }

    inline juce::Image makeTI (int w, int h)
    {
        w = juce::jmax (8, w); h = juce::jmax (8, h);
        juce::Image img (juce::Image::RGB, w, h, false);
        juce::Graphics g (img);
        const float k = w / 1054.0f, fw = (float) w, fh = (float) h;
        juce::Random rnd (84);
        // the worn charcoal body
        g.fillAll (juce::Colour (0xff0d0e10));
        auto body = juce::Rectangle<float> (0, 0, fw, fh).reduced (2.f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2c2f33), 0, 0, juce::Colour (0xff17191c), 0, fh, false));
        g.fillRoundedRectangle (body, 26 * k);
        for (int i = 0; i < w * h / 90; ++i)   // scuffs
        {
            g.setColour ((rnd.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (0.03f + 0.05f * rnd.nextFloat()));
            g.fillRect (rnd.nextFloat() * fw, rnd.nextFloat() * fh, 1.0f + rnd.nextFloat() * 1.5f, 1.0f + rnd.nextFloat() * 1.5f);
        }
        g.setColour (juce::Colour (0xff5a5d62).withAlpha (0.6f)); g.drawRoundedRectangle (body, 26 * k, 1.5f);
        // the name plate along the top (the Ghost Graph lettering is drawn on the right by the editor)
        g.setColour (juce::Colour (0xffd8d6cf).withAlpha (0.88f));
        g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultSansSerifFontName(), 15.f * k, juce::Font::plain)));
        g.drawText ("GHOSTBLOCK INSTRUMENTS", juce::Rectangle<float> (fw * 0.05f, 0, fw * 0.3f, fh * ti::lcdTop), juce::Justification::centredLeft);
        g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultSansSerifFontName(), 25.f * k, juce::Font::bold | juce::Font::italic)));
        g.drawText ("GHOST-84 Plus CE", juce::Rectangle<float> (0, 0, fw, fh * ti::lcdTop), juce::Justification::centred);
        // the dark bezel holding the screen and the key strip
        auto bez = juce::Rectangle<float> (fw * 0.018f, fh * ti::lcdTop - 6 * k, fw * 0.964f, fh * (ti::keysBottom - ti::lcdTop) + 12 * k);
        g.setColour (juce::Colour (0xff0b0c0d)); g.fillRoundedRectangle (bez, 18 * k);
        g.setColour (juce::Colour (0xff3e4146)); g.drawRoundedRectangle (bez, 18 * k, 1.2f);
        auto paintLcd = [&] (juce::Rectangle<float> r, float corner)
        {
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffc9ddae), 0, r.getY(), juce::Colour (0xffb7cf98), 0, r.getBottom(), false));
            g.fillRoundedRectangle (r, corner);
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (r.toNearestInt());
            g.setColour (juce::Colour (0xff17210f).withAlpha (0.035f));   // the pixel matrix
            const float step = juce::jmax (3.f, 3.f * k);
            for (float y = r.getY(); y < r.getBottom(); y += step) g.fillRect (r.getX(), y, r.getWidth(), 0.8f);
            for (float x = r.getX(); x < r.getRight(); x += step) g.fillRect (x, r.getY(), 0.8f, r.getHeight());
            for (int i = 0; i < (int) (r.getWidth() * r.getHeight() / 60); ++i)   // grain
            {
                g.setColour (juce::Colour (0xff17210f).withAlpha (0.02f + 0.03f * rnd.nextFloat()));
                g.fillRect (r.getX() + rnd.nextFloat() * r.getWidth(), r.getY() + rnd.nextFloat() * r.getHeight(), 1.2f, 1.2f);
            }
            g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.16f), 0, r.getY(), juce::Colours::transparentBlack, 0, r.getY() + 14 * k, false));
            g.fillRect (r.withHeight (14 * k));   // the screen sits a little behind the glass
        };
        paintLcd ({ fw * 0.026f, fh * ti::lcdTop, fw * 0.948f, fh * (ti::lcdBottom - ti::lcdTop) }, 12 * k);
        paintLcd ({ fw * 0.026f, fh * ti::keysTop, fw * 0.948f, fh * (ti::keysBottom - ti::keysTop) }, 10 * k);
        // the five fixed keys in the strip (the sixth, EDIT, is a real button)
        const char* keys[] = { "Y=", "WINDOW", "ZOOM", "TRACE", "GRAPH" };
        for (int i = 0; i < 5; ++i)
        {
            auto key = tiKey (i, fw, fh);
            g.setColour (juce::Colour (0xff17210f));
            if (i == 0) g.fillRoundedRectangle (key, 6 * k);
            else        g.drawRoundedRectangle (key.reduced (1.f), 6 * k, 2.2f * k);
            g.setColour (i == 0 ? juce::Colour (0xffc2d8a4) : juce::Colour (0xff17210f));
            g.setFont (lcdFont (key.getHeight() * 0.62f));
            g.drawText (keys[i], key, juce::Justification::centred);
        }
        return img;
    }

    inline juce::Image makeBackground (int look, int w, int h)
    {
        if (look == 0) return makePaper (w, h);
        if (look == 3) return makePaper (w, h, theme (3).paper);
        if (look == 4) return makeTI (w, h);
        auto img = look == 1 ? makeChroma (w, h) : makeInk (w, h);
        // where the equations are, the paint thins out a little so the math reads
        juce::Graphics g (img);
        const juce::Colour pc = theme (look).paper;
        juce::ColourGradient cg (pc.withAlpha (0.0f), 0, h * 0.53f, pc.withAlpha (0.5f), 0, h * 0.62f, false);
        g.setGradientFill (cg);
        g.fillRect (0, (int) (h * 0.53f), w, h - (int) (h * 0.53f));
        return img;
    }

    struct Look : juce::LookAndFeel_V4
    {
        const Theme* theme = &ggui::theme (0);
        Look() { applyColours (0); }
        void applyColours (int idx)
        {
            theme = &ggui::theme (idx);
            curTheme() = theme;
            setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
            setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
            setColour (juce::TextEditor::focusedOutlineColourId, th().amber.withAlpha (0.25f));
            setColour (juce::TextEditor::textColourId, th().amber);
            setColour (juce::TextEditor::highlightColourId, th().amber.withAlpha (0.25f));
            setColour (juce::TextEditor::highlightedTextColourId, th().amberHi);
            // the typing cursor: black on the light pages (CHROMA, INK, TI 84), the text colour on the dark ones
            setColour (juce::CaretComponent::caretColourId, th().paper.getPerceivedBrightness() > 0.5f ? juce::Colours::black : th().amber);
            setColour (juce::BubbleComponent::backgroundColourId, juce::Colour (0xf01e1b19));
            setColour (juce::BubbleComponent::outlineColourId, th().amber.withAlpha (0.5f));
            setColour (juce::TooltipWindow::textColourId, th().amber);
            setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1e1b19));
            setColour (juce::PopupMenu::textColourId, th().amber);
            setColour (juce::PopupMenu::highlightedBackgroundColourId, th().amber.withAlpha (0.85f));
            setColour (juce::PopupMenu::highlightedTextColourId, th().onText);
            setColour (juce::PopupMenu::backgroundColourId, th().paper.darker (0.35f));
            setColour (juce::BubbleComponent::backgroundColourId, th().paper.darker (0.35f).withAlpha (0.95f));
        }

        void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) override
        {
            curTheme() = theme;
            const float alpha = s.isEnabled() ? 1.0f : 0.28f;
            auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.f);
            const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
            const auto c = b.getCentre();
            juce::Path track, val;
            track.addCentredArc (c.x, c.y, r, r, 0, a0, a1, true);
            g.setColour (th().amber.withAlpha (0.18f * alpha));
            g.strokePath (track, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            const float ang = a0 + pos * (a1 - a0);
            val.addCentredArc (c.x, c.y, r, r, 0, a0, ang, true);
            g.setColour (th().amber.withAlpha (0.9f * alpha));
            g.strokePath (val, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            const juce::Point<float> tip (c.x + (r - 1) * std::sin (ang), c.y - (r - 1) * std::cos (ang));
            if (alpha > 0.5f)
            {
                g.setColour (th().amber.withAlpha (0.25f));
                g.fillEllipse (juce::Rectangle<float> (11.f, 11.f).withCentre (tip));
            }
            g.setColour (th().amberHi.withAlpha (alpha));
            g.fillEllipse (juce::Rectangle<float> (5.f, 5.f).withCentre (tip));
        }

        void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
        {
            curTheme() = theme;
            const float alpha = b.isEnabled() ? 1.0f : 0.28f;
            auto r = b.getLocalBounds().toFloat().reduced (1.0f);
            const bool on = b.getToggleState();
            if (th().lcd)
            {
                const float rr = juce::jmin (6.f, r.getHeight() * 0.25f);
                g.setColour (th().amber.withAlpha (alpha));
                if (on) g.fillRoundedRectangle (r, rr);
                else { if (over || down) { g.setColour (th().amber.withAlpha (0.12f * alpha)); g.fillRoundedRectangle (r, rr); g.setColour (th().amber.withAlpha (alpha)); }
                       g.drawRoundedRectangle (r.reduced (0.5f), rr, 2.0f); }
                return;
            }
            if (on) { g.setColour (th().amber.withAlpha (0.88f * alpha)); g.fillRoundedRectangle (r, 3.f); }
            else if (over || down) { g.setColour (th().amber.withAlpha (0.10f * alpha)); g.fillRoundedRectangle (r, 3.f); }
            g.setColour (th().amber.withAlpha ((on ? 0.9f : 0.45f) * alpha));
            g.drawRoundedRectangle (r, 3.f, 1.0f);
        }
        // hover tips in the look: the page colour, the look's ink, its type (monospaced on the TI 84)
        juce::TextLayout tipLayout (const juce::String& tip) const
        {
            juce::AttributedString a;
            a.append (tip, serif (th().lcd ? 16.f : 15.f), th().amber);
            a.setJustification (juce::Justification::topLeft);
            a.setWordWrap (juce::AttributedString::byWord);
            juce::TextLayout tl; tl.createLayout (a, 320.f);
            return tl;
        }
        juce::Rectangle<int> getTooltipBounds (const juce::String& tip, juce::Point<int> pos, juce::Rectangle<int> area) override
        {
            const auto tl = tipLayout (tip);
            const int w = (int) std::ceil (tl.getWidth()) + 22, h = (int) std::ceil (tl.getHeight()) + 14;
            int x = pos.x - w / 2, y = pos.y + 18;
            if (y + h > area.getBottom()) y = pos.y - h - 10;   // near the bottom: above the mouse
            return juce::Rectangle<int> (x, y, w, h).constrainedWithin (area.reduced (4));
        }
        void drawTooltip (juce::Graphics& g, const juce::String& tip, int w, int h) override
        {
            auto r = juce::Rectangle<float> ((float) w, (float) h);
            const float rr = th().lcd ? 5.f : 7.f;
            g.setColour (th().paper.withAlpha (0.97f));
            g.fillRoundedRectangle (r.reduced (0.5f), rr);
            g.setColour (th().amber.withAlpha (th().lcd ? 1.f : 0.6f));
            g.drawRoundedRectangle (r.reduced (th().lcd ? 1.f : 0.5f), rr, th().lcd ? 2.f : 1.f);
            tipLayout (tip).draw (g, r.reduced (11.f, 7.f));
        }
        juce::Font getTextButtonFont (juce::TextButton&, int h) override { return th().lcd && h > 30 ? serif (h * 0.58f) : serif (juce::jmin (16.f, h * 0.62f)); }
        void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
        {
            curTheme() = theme;
            const float alpha = b.isEnabled() ? 1.0f : 0.28f;
            g.setFont (getTextButtonFont (b, b.getHeight()));
            g.setColour ((b.getToggleState() ? th().onText : th().amber).withAlpha (alpha));
            g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (3, 0), juce::Justification::centred, 1);
        }
    };
}

// ------------------------------------------------------------------------------------------------ the graph
class GraphView : public juce::Component
{
public:
    const ggui::Theme* theme = &ggui::theme (0);
    static constexpr int kSamples = 1280;

    void setProgram (gg::Program* p)
    {
        prog = p;
        resample();
    }
    void setX (double x) { xNow = x; }
    std::function<void()> onClick;

    // ---- editing notes by dragging (EDIT mode)
    bool editMode = false;
    std::function<void (int voice, double x0, double x1)> onDragStart;
    std::function<void (int part, double dx, double dy, bool final)> onDrag;   // part: 0 the whole note, 1 its start, 2 its end

    // the selected notes: one click picks one; Cmd+click adds or takes one away; Cmd+drag on the paper draws a box
    // round several. Grab any selected note and they all move together.
    struct Sel { int v = -1; double a = 0, b = 0; };
    void setHighlight (int v, double a, double b) { sels.assign (1, Sel { v, a, b }); repaint(); }
    void clearHighlight() { sels.clear(); repaint(); }
    void setSelection (std::vector<Sel> s) { sels = std::move (s); repaint(); }
    const std::vector<Sel>& selection() const { return sels; }
    int selVoice() const { return sels.empty() ? -1 : sels.front().v; }
    double selStart() const { return sels.empty() ? 0.0 : sels.front().a; }
    double selEnd() const { return sels.empty() ? 0.0 : sels.front().b; }
    bool hasSelection() const { return ! sels.empty(); }
    std::function<void()> onSelect;                                       // the selection changed: show it in the math
    std::function<void (const std::vector<Sel>&)> onGroupStart;          // grabbing one of several selected notes
    std::function<void (double dx, double dy, bool final)> onGroupDrag;
    // right-click: Copy the note(s), Paste anywhere on the paper (the copy lands where you clicked), Delete
    std::function<void (const std::vector<Sel>&)> onCopy;
    std::function<bool()> canPaste;
    std::function<void (double x, double y, bool fine)> onPaste;
    std::function<void (int, double, double)> onDelete;
    std::function<void (const std::vector<Sel>&)> onDeleteMany;
    double pxToX (float px) const { const auto r = plot(); return -8.0 + 16.0 * (px - r.getX()) / r.getWidth(); }
    double pxToY (float py) const { const auto r = plot(); return (r.getCentreY() - py) / (r.getHeight() * 0.5) * yRange; }

    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (getWidth() * 0.085f, 8.f); }
    float xToPx (double x) const { const auto r = plot(); return r.getX() + (float) ((x + 8.0) / 16.0) * r.getWidth(); }
    float yToPx (double y) const { const auto r = plot(); return r.getCentreY() - (float) (y / yRange) * r.getHeight() * 0.5f; }
    double pxPerBeat() const { return plot().getWidth() / 16.0; }
    double pxPerOct() const { return plot().getHeight() * 0.5 / yRange; }

    struct Hit { int v = -1, k = -1, part = 0; double x0 = 0, x1 = 0; };
    Hit hitTest (juce::Point<float> m) const
    {
        Hit best; float bd = 9.0f;
        if (nv == 0) return best;
        const double xm = -8.0 + 16.0 * (m.x - plot().getX()) / plot().getWidth();
        const int kc = (int) std::round ((xm + 8.0) / 16.0 * (kSamples - 1));
        const int span = (int) std::ceil (12.0 / (plot().getWidth() / (kSamples - 1))) + 2;
        for (int v = 0; v < nv; ++v)
            for (int k = juce::jmax (1, kc - span); k <= juce::jmin (kSamples - 1, kc + span); ++k)
            {
                const double ya = ys[(size_t) (v * kSamples + k - 1)], yb = ys[(size_t) (v * kSamples + k)];
                if (! std::isfinite (ya) || ! std::isfinite (yb) || breaks[(size_t) (v * kSamples + k)]) continue;
                const juce::Line<float> seg (xToPx (-8.0 + 16.0 * (k - 1) / (kSamples - 1)), yToPx (ya), xToPx (-8.0 + 16.0 * k / (kSamples - 1)), yToPx (yb));
                juce::Point<float> on; const float d = seg.getDistanceFromPoint (m, on);
                if (d < bd) { bd = d; best.v = v; best.k = k; }
            }
        if (best.v < 0) return best;
        pieceAround (best, best.k - 1, best.k);
        const float px0 = xToPx (best.x0), px1 = xToPx (best.x1), grab = juce::jmin (12.0f, (px1 - px0) * 0.3f);
        best.part = (m.x - px0 < grab) ? 1 : ((px1 - m.x < grab) ? 2 : 0);
        return best;
    }

    // every note with a bit of its line inside the box (in pixels)
    std::vector<Sel> notesIn (juce::Rectangle<float> box) const
    {
        std::vector<Sel> out;
        if (nv == 0 || box.getWidth() < 2.f || box.getHeight() < 2.f) return out;
        const int kA = juce::jlimit (0, kSamples - 1, (int) std::floor ((pxToX (box.getX()) + 8.0) / 16.0 * (kSamples - 1)));
        const int kB = juce::jlimit (0, kSamples - 1, (int) std::ceil ((pxToX (box.getRight()) + 8.0) / 16.0 * (kSamples - 1)));
        for (int v = 0; v < nv; ++v)
        {
            double doneTo = -1e9;
            for (int k = kA; k <= kB; ++k)
            {
                const double x = -8.0 + 16.0 * k / (kSamples - 1);
                if (x <= doneTo) continue;
                const double y = ys[(size_t) (v * kSamples + k)];
                if (! std::isfinite (y) || ! box.contains (xToPx (x), yToPx (y))) continue;
                Hit h; h.v = v;
                pieceAround (h, k, k);
                out.push_back ({ v, h.x0, h.x1 });
                doneTo = h.x1;
            }
        }
        return out;
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        if (! editMode) { setMouseCursor (juce::MouseCursor::NormalCursor); return; }
        const auto h = hitTest (e.position);
        if (h.v != hov.v || h.x0 != hov.x0 || h.part != hov.part) { hov = h; repaint(); }
        const bool inGroup = sels.size() > 1 && findSel (h) >= 0;
        setMouseCursor (e.mods.isCommandDown() ? juce::MouseCursor::CrosshairCursor
                        : h.v < 0 ? juce::MouseCursor::NormalCursor
                        : (h.part == 0 || inGroup ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::LeftRightResizeCursor));
    }
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (! editMode) return false;
        if (k == juce::KeyPress ('v', juce::ModifierKeys::commandModifier, 0))
        {
            const auto m = getMouseXYRelative().toFloat();   // Cmd+V: the copy lands under the mouse
            if (plot().contains (m) && canPaste && canPaste() && onPaste) onPaste (pxToX (m.x), pxToY (m.y), false);
            return true;
        }
        if (k == juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0))   // Cmd+A: every note on the page
        {
            sels = notesIn (plot().expanded (2.f));
            repaint();
            if (onSelect) onSelect();
            return true;
        }
        if (sels.empty()) return false;
        if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey)
        {
            deleteSelected();
            return true;
        }
        if (k == juce::KeyPress::escapeKey) { clearHighlight(); if (onSelect) onSelect(); return true; }
        if (k == juce::KeyPress ('c', juce::ModifierKeys::commandModifier, 0))
        {
            if (onCopy) onCopy (sels);
            return true;
        }
        return false;
    }
    void mouseExit (const juce::MouseEvent&) override { if (hov.v >= 0 && ! dragging) { hov = Hit(); repaint(); } }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! editMode) { if (onClick) onClick(); return; }
        const auto h = hitTest (e.position);
        if (e.mods.isPopupMenu()) { showNoteMenu (h, e.position, e.mods.isAltDown()); return; }
        grabKeyboardFocus();
        dragging = false; group = false; axis = 0; grabbed = h; downPos = e.position;
        if (e.mods.isCommandDown())
        {
            if (h.v >= 0)   // Cmd+click a note: add it to the selection, or take it out
            {
                const int i = findSel (h);
                if (i >= 0) sels.erase (sels.begin() + i); else sels.push_back ({ h.v, h.x0, h.x1 });
                repaint();
                if (onSelect) onSelect();
                return;
            }
            marquee = true; mqA = mqB = e.position; mqBase = sels;   // Cmd+drag on the paper: draw a box round notes
            repaint();
            return;
        }
        if (h.v < 0) { clearHighlight(); if (onSelect) onSelect(); return; }   // a click on empty paper: nothing selected
        if (sels.size() > 1 && findSel (h) >= 0)   // grab one of several selected notes: they all move
        {
            dragging = group = true;
            grabbed.part = 0;
            if (onGroupStart) onGroupStart (sels);
            return;
        }
        dragging = true;
        setHighlight (h.v, h.x0, h.x1);                  // the clicked note is selected (Delete removes it)
        if (onSelect) onSelect();                        // ...and its place in the math lights up
        if (onDragStart) onDragStart (h.v, h.x0, h.x1);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (marquee)
        {
            mqB = e.position;
            sels = mqBase;
            for (auto& n : notesIn (juce::Rectangle<float> (mqA, mqB)))
                if (findSel (n) < 0) sels.push_back (n);
            repaint();
            return;
        }
        if (dragging) sendDrag (e, false);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (marquee) { marquee = false; repaint(); if (onSelect) onSelect(); return; }
        if (! dragging) return;
        sendDrag (e, true);
        dragging = group = false; hov = Hit();   // the notes stay selected (highlighted) after the drag
    }

    void resample()
    {
        nv = (prog != nullptr && prog->ok()) ? prog->numVoices() : 0;
        ys.assign ((size_t) nv * kSamples, std::numeric_limits<double>::quiet_NaN());
        if (nv == 0) { repaint(); return; }
        double m = 0;
        for (int k = 0; k < kSamples; ++k)
        {
            prog->setX (-8.0 + 16.0 * k / (kSamples - 1));
            for (int v = 0; v < nv; ++v)
            {
                const double y = prog->voice (v);
                ys[(size_t) (v * kSamples + k)] = y;
                if (std::isfinite (y)) m = std::max (m, std::abs (y));
            }
        }
        yRange = 2.6; juce::ignoreUnused (m);   // fixed, like every video: about -2.5 .. 2.5, anything further off the page is cut
        // where the line really breaks: a big step between two samples is a jump only if it stays a jump when we look
        // closer (halve the gap a few times); a steep drum hit gets smaller steps and is drawn as one stroke, like Desmos
        breaks.assign ((size_t) nv * kSamples, 0);
        const double dx = 16.0 / (kSamples - 1);
        for (int v = 0; v < nv; ++v)
            for (int k = 1; k < kSamples; ++k)
            {
                const double a = ys[(size_t) (v * kSamples + k - 1)], b = ys[(size_t) (v * kSamples + k)];
                if (! std::isfinite (a) || ! std::isfinite (b)) continue;
                double lo = -8.0 + dx * (k - 1), hi = lo + dx, ya = a, yb = b;
                bool jump = std::abs (b - a) > 0.02;
                for (int it = 0; it < 10 && jump; ++it)
                {
                    const double mid = 0.5 * (lo + hi);
                    prog->setX (mid);
                    const double ym = prog->voice (v);
                    if (! std::isfinite (ym)) { ya = 0; yb = 1; break; }   // a gap in between: a break
                    // keep the half that holds most of the step
                    if (std::abs (ym - ya) > std::abs (yb - ym)) { hi = mid; yb = ym; } else { lo = mid; ya = ym; }
                }
                if (jump && std::abs (yb - ya) > 0.01) breaks[(size_t) (v * kSamples + k)] = 1;
            }
        repaint();
    }


    void paint (juce::Graphics& g) override
    {
        using namespace ggui;
        curTheme() = theme;
        const auto r = getLocalBounds().toFloat().reduced (getWidth() * 0.085f, 8.f);
        auto X = [&] (double x) { return r.getX() + (float) ((x + 8.0) / 16.0) * r.getWidth(); };
        auto Y = [&] (double y) { return r.getCentreY() - (float) (y / yRange) * r.getHeight() * 0.5f; };

        // grid + axes, as in the videos: a line every 2 along x, every 1 along y, brown-orange and faint
        const juce::Colour gridC = th().grid, axisC = th().axis, labelC = th().label;
        const bool lcd = th().lcd;
        const float dotStep = juce::jmax (4.f, getWidth() / 230.f);
        for (int i = -8; i <= 8; i += 2)
        {
            g.setColour (gridC);
            if (lcd) for (float y = r.getY(); y <= r.getBottom(); y += dotStep) g.fillRect (X (i) - 0.8f, y, 1.6f, 1.6f);
            else g.fillRect (X (i) - 0.5f, r.getY(), 1.0f, r.getHeight());
        }
        for (int j = (int) std::ceil (-yRange); j <= (int) std::floor (yRange); ++j)
        {
            g.setColour (gridC);
            if (lcd) for (float x = r.getX(); x <= r.getRight(); x += dotStep) g.fillRect (x, Y (j) - 0.8f, 1.6f, 1.6f);
            else g.fillRect (r.getX(), Y (j) - 0.5f, r.getWidth(), 1.0f);
        }
        g.setColour (axisC);
        const float aw = lcd ? 2.6f : 1.5f;
        g.fillRect (r.getX() - 6.f, Y (0) - aw * 0.5f, r.getWidth() + 12.f, aw);
        g.fillRect (X (0) - aw * 0.5f, r.getY() - 6.f, aw, r.getHeight() + 12.f);
        if (lcd)   // arrowheads, as on the calculator
        {
            juce::Path ah;
            const float s = 7.f;
            ah.addTriangle (r.getRight() + 12.f, Y (0), r.getRight() + 12.f - s * 1.5f, Y (0) - s * 0.7f, r.getRight() + 12.f - s * 1.5f, Y (0) + s * 0.7f);
            ah.addTriangle (X (0), r.getY() - 12.f, X (0) - s * 0.7f, r.getY() - 12.f + s * 1.5f, X (0) + s * 0.7f, r.getY() - 12.f + s * 1.5f);
            g.fillPath (ah);
            for (int i = -8; i <= 8; i += 2) g.fillRect (X (i) - 1.f, Y (0) - 4.f, 2.f, 8.f);   // ticks
            for (int j = (int) std::ceil (-yRange); j <= (int) std::floor (yRange); ++j) g.fillRect (X (0) - 4.f, Y (j) - 1.f, 8.f, 2.f);
        }
        const float ls = lcd ? juce::jmax (12.f, getWidth() / 62.f) : 10.5f;
        const juce::String minus = lcd ? juce::String ("-") : juce::String::fromUTF8 ("\xe2\x88\x92");
        g.setFont (serif (ls, true));
        g.setColour (labelC);
        for (int i = -8; i <= 8; i += 2)
            if (i != 0) g.drawText ((i < 0 ? minus : juce::String()) + juce::String (std::abs (i)), juce::Rectangle<float> (X (i) - 2 * ls, Y (0) + 4, 4 * ls, ls + 2), juce::Justification::centred);
        for (int j = (int) std::ceil (-yRange); j <= (int) std::floor (yRange); ++j)
            if (j != 0) g.drawText ((j < 0 ? minus : juce::String()) + juce::String (std::abs (j)), juce::Rectangle<float> (X (0) - 6 - 3 * ls, Y (j) - ls * 0.6f, 3 * ls, ls + 2), juce::Justification::centredRight);
        const float xs = lcd ? ls * 1.3f : 12.f;
        if (lcd) g.setFont (serif (xs));
        g.drawText ("x", juce::Rectangle<float> (r.getRight() + (lcd ? 14.f : 2.f), Y (0) - xs * 0.6f, xs, xs + 2), juce::Justification::centredLeft);
        g.drawText ("y", juce::Rectangle<float> (X (0) + (lcd ? 10.f : 4.f), r.getY() - (lcd ? 6.f : 10.f), xs, xs + 2), juce::Justification::centredLeft);

        if (nv == 0) return;
        g.reduceClipRegion (r.expanded (10.f, 6.f).toNearestInt());   // lines that leave the window are cut at its edge
        const int kNow = juce::jlimit (0, kSamples - 1, (int) std::floor ((xNow + 8.0) / 16.0 * (kSamples - 1)));

        // EDIT: the whole of every line shows (faint ahead of the playhead), so any note can be grabbed
        if (editMode)
            for (int v = 0; v < nv; ++v)
            {
                juce::Path all;
                build (all, v, 0, kSamples - 1, X, Y, false);
                const juce::Colour lc = th().perVoice ? th().voice[v % 6] : th().lineCol;
                g.setColour (lc.withAlpha (0.45f));
                g.strokePath (all, juce::PathStrokeType (juce::jmax (1.6f, getWidth() / 480.0f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        // the lines: solid th().amber, drawn only up to the playhead (nothing of the future shows, like the videos)
        for (int v = 0; v < nv; ++v)
        {
            juce::Path trail;
            build (trail, v, 0, kNow, X, Y, true);
            const float lw = juce::jmax (2.0f, getWidth() / 380.0f) * (th().lcd ? 1.55f : 1.0f);
            const juce::Colour lc = th().perVoice ? th().voice[v % 6] : th().lineCol;
            if (! th().halo.isTransparent())
            {
                g.setColour (th().halo);
                g.strokePath (trail, juce::PathStrokeType (lw * 3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            if (! th().lcd) g.setColour (lc.withAlpha (0.20f)); else g.setColour (juce::Colours::transparentBlack);
            g.strokePath (trail, juce::PathStrokeType (lw * 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (lc);
            g.strokePath (trail, juce::PathStrokeType (lw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        // the selected notes and the note under the mouse: thicker, with handles at the ends of a single one
        if (editMode)
        {
            auto drawNote = [&] (int v, double a, double b, bool handles)
            {
                if (v < 0 || v >= nv) return;
                juce::Path seg;
                const int k0 = juce::jlimit (0, kSamples - 1, (int) std::ceil ((a + 8.0) / 16.0 * (kSamples - 1)));
                const int k1 = juce::jlimit (0, kSamples - 1, (int) std::floor ((b + 8.0) / 16.0 * (kSamples - 1)));
                if (k1 > k0) build (seg, v, k0, k1, X, Y, false);
                const juce::Colour lc = th().perVoice ? th().voice[v % 6] : th().lineCol;
                g.setColour (th().amberHi.withAlpha (0.35f));
                g.strokePath (seg, juce::PathStrokeType (juce::jmax (6.0f, getWidth() / 160.0f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setColour (lc);
                g.strokePath (seg, juce::PathStrokeType (juce::jmax (3.0f, getWidth() / 300.0f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                for (int end = 0; end < 2 && k1 > k0 && handles; ++end)
                {
                    const int k = end == 0 ? k0 : k1;
                    const double y = ys[(size_t) (v * kSamples + k)];
                    if (! std::isfinite (y)) continue;
                    const auto c = juce::Point<float> (X (-8.0 + 16.0 * k / (kSamples - 1)), Y (y));
                    const bool on = ! dragging && hov.part == end + 1 && hov.v == v && std::abs (hov.x0 - a) < 1e-6;
                    g.setColour (th().amberHi); g.fillRect (juce::Rectangle<float> (on ? 11.f : 8.f, on ? 11.f : 8.f).withCentre (c));
                    g.setColour (lc);           g.drawRect (juce::Rectangle<float> (on ? 11.f : 8.f, on ? 11.f : 8.f).withCentre (c), 1.5f);
                }
            };
            const bool single = sels.size() <= 1;
            for (auto& sl : sels) drawNote (sl.v, sl.a, sl.b, single);
            if (! dragging && ! marquee && hov.v >= 0 && findSel (hov) < 0) drawNote (hov.v, hov.x0, hov.x1, single || sels.empty());
            if (marquee)   // the box you are drawing round notes
            {
                const auto box = juce::Rectangle<float> (mqA, mqB);
                g.setColour (th().amberHi.withAlpha (0.10f)); g.fillRect (box);
                g.setColour (th().amber.withAlpha (0.8f));
                const float dash[] = { 5.f, 4.f };
                juce::Path outline; outline.addRectangle (box);
                juce::Path dashed; juce::PathStrokeType (1.2f).createDashedStroke (dashed, outline, dash, 2);
                g.fillPath (dashed);
            }
        }
        // the playheads: pale cream dots with a thin th().amber rim
        prog->setX (xNow);
        for (int v = 0; v < nv; ++v)
        {
            const double y = prog->voice (v);
            if (! std::isfinite (y) || std::abs (y) > yRange * 1.05) continue;
            const juce::Point<float> p (X (xNow), Y (y));
            const float d = juce::jmax (11.0f, getWidth() / 75.0f);
            if (th().lcd)
            {
                g.setColour (th().paper);   g.fillEllipse (juce::Rectangle<float> (d * 1.15f, d * 1.15f).withCentre (p));
                g.setColour (th().lineCol); g.drawEllipse (juce::Rectangle<float> (d * 1.15f, d * 1.15f).withCentre (p), juce::jmax (2.2f, d * 0.2f));
                continue;
            }
            g.setColour (th().amberHi.withAlpha (0.22f)); g.fillEllipse (juce::Rectangle<float> (d * 1.4f, d * 1.4f).withCentre (p));
            g.setColour (th().amberHi);                   g.fillEllipse (juce::Rectangle<float> (d, d).withCentre (p));
            g.setColour (th().perVoice ? th().voice[v % 6] : th().dotRing);
            g.drawEllipse (juce::Rectangle<float> (d, d).withCentre (p), th().perVoice ? 2.0f : 1.0f);
        }
    }

    double getYRange() const { return yRange; }
    bool isDragging() const { return dragging; }
    int numVoices() const { return nv; }

private:
    gg::Program* prog = nullptr;
    Hit hov, grabbed;
    bool dragging = false;
    int axis = 0;                  // the ends follow one direction at a time: 1 longer/shorter, 2 up/down
    juce::Point<float> downPos;

    void showNoteMenu (const Hit& h, juce::Point<float> at, bool fine)
    {
        juce::PopupMenu m;
        const bool onNote = h.v >= 0;
        if (onNote)
        {
            if (findSel (h) < 0 || sels.size() < 2) { setHighlight (h.v, h.x0, h.x1); if (onSelect) onSelect(); }
            const juce::String what = sels.size() > 1 ? juce::String (" ") + juce::String ((int) sels.size()) + " notes" : juce::String();
            m.addItem (1, "Copy" + what);
            m.addItem (3, "Delete" + what);
        }
        else m.addItem (2, "Paste", canPaste && canPaste());   // greyed out until a note is copied
        const double px = pxToX (at.x), py = pxToY (at.y);
        juce::Component::SafePointer<GraphView> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                                   .withTargetScreenArea (juce::Rectangle<int> (1, 1).withPosition (localPointToGlobal (at.toInt()))),
                         [safe, px, py, fine] (int r)
        {
            if (safe == nullptr) return;
            if (r == 1 && safe->onCopy) safe->onCopy (safe->sels);
            if (r == 2 && safe->onPaste) safe->onPaste (px, py, fine);
            if (r == 3) safe->deleteSelected();
        });
    }

    std::vector<Sel> sels;
    bool group = false, marquee = false;
    juce::Point<float> mqA, mqB;
    std::vector<Sel> mqBase;

    int findSel (const Hit& h) const { return findSel (Sel { h.v, h.x0, h.x1 }); }
    int findSel (const Sel& n) const
    {
        if (n.v < 0) return -1;
        for (size_t i = 0; i < sels.size(); ++i)
        {
            const auto& s = sels[i];
            if (s.v == n.v && n.a < s.b - 1e-6 && n.b > s.a + 1e-6) return (int) i;   // the same piece of the same voice
        }
        return -1;
    }
    void deleteSelected()
    {
        const auto gone = sels;
        clearHighlight();
        if (gone.size() == 1) { if (onDelete) onDelete (gone[0].v, gone[0].a, gone[0].b); }
        else if (onDeleteMany) onDeleteMany (gone);
        if (onSelect) onSelect();
    }
    // the whole unbroken piece of voice h.v around samples k0..k1
    void pieceAround (Hit& h, int k0, int k1) const
    {
        auto ok = [&] (int k) { return std::isfinite (ys[(size_t) (h.v * kSamples + k)]); };
        while (k0 > 0 && ok (k0 - 1) && ! breaks[(size_t) (h.v * kSamples + k0)]) --k0;
        while (k1 < kSamples - 1 && ok (k1 + 1) && ! breaks[(size_t) (h.v * kSamples + k1 + 1)]) ++k1;
        h.x0 = refine (h.v, k0, -1);
        h.x1 = refine (h.v, k1, +1);
    }

    void sendDrag (const juce::MouseEvent& e, bool final)
    {
        const auto d = e.position - downPos;
        const bool fine = e.mods.isAltDown();
        if (grabbed.part != 0 && axis == 0 && d.getDistanceFromOrigin() > 6.0f) axis = std::abs (d.x) >= std::abs (d.y) ? 1 : 2;
        double dx = d.x / pxPerBeat(), dy = -d.y / pxPerOct();
        dx = fine ? std::round (dx * 128.0) / 128.0 : std::round (dx * 16.0) / 16.0;      // 1/16 beat (Alt: finer)
        dy = fine ? std::round (dy * 1200.0) / 1200.0 : std::round (dy * 12.0) / 12.0;    // a semitone (Alt: cents)
        if (group) { if (onGroupDrag) onGroupDrag (dx, dy, final); return; }
        if (grabbed.part != 0) { if (axis != 1) dx = 0; if (axis != 2) dy = 0; }
        if (onDrag) onDrag (grabbed.part, dx, dy, final);
    }

    // where a note really starts or ends, between sample k and its neighbour on side dir (-1 left, +1 right)
    double refine (int v, int k, int dir) const
    {
        const double xin = -8.0 + 16.0 * k / (kSamples - 1);
        if ((dir < 0 && k == 0) || (dir > 0 && k == kSamples - 1)) return xin;
        double lo = xin, hi = -8.0 + 16.0 * (k + dir) / (kSamples - 1);   // lo inside, hi outside
        prog->setX (lo); const double yin = prog->voice (v);
        prog->setX (hi); const double yout = prog->voice (v);
        for (int it = 0; it < 30; ++it)
        {
            const double mid = 0.5 * (lo + hi);
            prog->setX (mid);
            const double ym = prog->voice (v);
            const bool inside = std::isfinite (ym) && (! std::isfinite (yout) || std::abs (ym - yin) < std::abs (ym - yout));
            (inside ? lo : hi) = mid;
        }
        double x = 0.5 * (lo + hi);
        const double r64 = std::round (x * 64.0) / 64.0;
        if (std::abs (x - r64) < 0.004) x = r64;   // clean numbers: 2 rather than 1.9998
        return x;
    }

    std::vector<double> ys;
    std::vector<uint8_t> breaks;
    int nv = 0;
    double xNow = -8.0, yRange = 2.5;

    template <typename FX, typename FY>
    void build (juce::Path& p, int v, int k0, int k1, FX X, FY Y, bool toNow)
    {
        bool pen = false;
        double prev = 0;
        for (int k = k0; k <= k1; ++k)
        {
            const double y = ys[(size_t) (v * kSamples + k)];
            if (! std::isfinite (y) || std::abs (y) > yRange * 3.0) { pen = false; continue; }
            const double x = -8.0 + 16.0 * k / (kSamples - 1);
            if (pen && breaks[(size_t) (v * kSamples + k)]) pen = false;
            if (! pen) { p.startNewSubPath (X (x), Y (y)); pen = true; }
            else p.lineTo (X (x), Y (y));
            prev = y;
        }
        if (toNow && pen && prog != nullptr)
        {
            prog->setX (xNow);
            const double y = prog->voice (v);
            if (std::isfinite (y) && std::abs (y - prev) <= 0.5) p.lineTo (X (xNow), Y (y));
        }
    }
};

// the control strip floats over the bottom of the page; it hides while you just watch, like the videos
class ControlStrip : public juce::Component
{
public:
    std::function<void (juce::Graphics&)> paintLabels;
    juce::Rectangle<int> panel;
    const ggui::Theme* theme = &ggui::theme (0);
    ControlStrip() { setInterceptsMouseClicks (false, true); }
    void paint (juce::Graphics& g) override
    {
        ggui::curTheme() = theme;
        g.setColour ((ggui::th().perVoice ? ggui::th().paper : ggui::th().paper.darker (0.25f)).withAlpha (0.94f));
        g.fillRect (panel);
        g.setColour (ggui::th().amber.withAlpha (0.25f));
        g.fillRect (panel.getX(), panel.getY(), panel.getWidth(), 1);
        if (paintLabels) paintLabels (g);
    }
};

// ------------------------------------------------------------------------------------------------ the editor
// Over the typed equations: where the selected note is written. A box round the exact piece that plays it, an arrow
// in the margin pointing at it, and fainter boxes round the voice's entry in y = [...] and the lines it plays from.
class FormulaMarks : public juce::Component
{
public:
    explicit FormulaMarks (juce::TextEditor& e) : ed (e) { setInterceptsMouseClicks (false, false); }
    const ggui::Theme* theme = &ggui::theme (0);
    juce::Array<juce::Range<int>> mains;      // characters in the text box: where the selected notes are written
    juce::Array<juce::Range<int>> others;
    bool active() const { return ! mains.isEmpty(); }
    void clear() { mains.clear(); others.clear(); repaint(); }

    void paint (juce::Graphics& g) override
    {
        if (! active()) return;
        const juce::Colour hi = theme->lcd ? theme->lineCol : theme->amberHi;
        const juce::Colour ink = theme->amber;
        g.reduceClipRegion (getLocalBounds());
        for (auto& r : others)
            for (auto& b : rows (r))
            {
                g.setColour (ink.withAlpha (0.10f)); g.fillRoundedRectangle (b.toFloat().expanded (2.f, 1.f), 3.f);
                g.setColour (ink.withAlpha (0.55f)); g.drawRoundedRectangle (b.toFloat().expanded (2.f, 1.f), 3.f, 1.0f);
            }
        juce::Array<juce::Rectangle<int>> rl;
        for (int m = 0; m < mains.size(); ++m)
        {
            const auto rs = rows (mains[m]);
            if (m == 0) rl = rs;
            for (auto& b : rs)
            {
                g.setColour (hi.withAlpha (0.18f)); g.fillRoundedRectangle (b.toFloat().expanded (3.f, 2.f), 4.f);
                g.setColour (hi);                   g.drawRoundedRectangle (b.toFloat().expanded (3.f, 2.f), 4.f, 2.0f);
            }
        }
        if (! rl.isEmpty())   // the arrow, in the left margin, at the first line
        {
            const auto b = rl.getFirst().toFloat();
            const float cy = b.getCentreY(), s = juce::jlimit (4.f, 7.f, b.getHeight() * 0.3f);
            const float tip = (float) margin - 3.f;
            juce::Path a; a.addTriangle (tip - s * 1.6f, cy - s, tip - s * 1.6f, cy + s, tip, cy);
            g.setColour (hi); g.fillPath (a);
        }
    }
    static constexpr int margin = 16;   // the text box's left indent: room for the arrow
private:
    // the text's boxes, one per line (the text box gives one per word)
    juce::Array<juce::Rectangle<int>> rows (juce::Range<int> r) const
    {
        juce::Array<juce::Rectangle<int>> out;
        for (auto& b : ed.getTextBounds (r.getIntersectionWith ({ 0, ed.getTotalNumChars() })))
        {
            bool merged = false;
            for (auto& o : out)
                if (b.getY() < o.getBottom() && b.getBottom() > o.getY()) { o = o.getUnion (b); merged = true; break; }
            if (! merged) out.add (b);
        }
        return out;
    }
    juce::TextEditor& ed;
};

class GraphEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit GraphEditor (GraphProcessor& p) : juce::AudioProcessorEditor (p), proc (p)
    {
        setLookAndFeel (&look);
        addAndMakeVisible (graph);
        graph.onClick = [this] { unfocusAllComponents(); };
        graph.onDragStart = [this] (int v, double x0, double x1)
        {
            flushEdit();
            dragBase = proc.getScene (viewed).text;
            dragPlan = gg::planNote (dragBase, v, x0, x1);
            dragVoice = v;
            grabX0 = x0; grabX1 = x1;   // the ends you can see (a moved note can run off the page)
        };
        graph.onDrag = [this] (int part, double dx, double dy, bool final) { applyDrag (part, dx, dy, final); };
        graph.setWantsKeyboardFocus (true);
        graph.onDelete = [this] (int v, double a, double b) { deleteNote (v, a, b); };
        graph.onSelect = [this] { markFormula(); };
        graph.onCopy = [this] (const std::vector<GraphView::Sel>& n) { copyNotes (n); };
        graph.canPaste = [this] { return canPaste(); };
        graph.onPaste = [this] (double x, double y, bool fine) { pasteNotes (x, y, fine); };
        graph.onDeleteMany = [this] (const std::vector<GraphView::Sel>& n) { deleteNotes (n); };
        graph.onGroupStart = [this] (const std::vector<GraphView::Sel>& n)
        {
            flushEdit();
            dragBase = proc.getScene (viewed).text;
            groupNotes = n;
        };
        graph.onGroupDrag = [this] (double dx, double dy, bool final) { applyGroupDrag (dx, dy, final); };

        // EDIT: type in the equations and drag the notes on the graph; DONE puts the math back on the page
        editBtn.setButtonText ("EDIT");
        editBtn.setTooltip ("Edit this scene: type in the equations, or drag notes on the graph (Alt = finer steps). Click a note to see where it is in the math; Cmd+click or Cmd+drag a box to pick several and move them together; right-click to copy, paste or delete");
        editBtn.onClick = [this] { if (editing) stopEditing(); else startEditing(); };
        addAndMakeVisible (editBtn);
        undoBtn.setButtonText ("UNDO");
        undoBtn.setTooltip ("Undo the last note drag");
        undoBtn.onClick = [this] { undoDrag(); };
        addChildComponent (undoBtn);
        redoBtn.setButtonText ("REDO");
        redoBtn.setTooltip ("Redo the note drag you just undid");
        redoBtn.onClick = [this] { redoDrag(); };
        addChildComponent (redoBtn);

        eq.setMultiLine (true, false);
        eq.setReturnKeyStartsNewLine (true);
        eq.setTabKeyUsedAsCharacter (false);
        eq.setScrollbarsShown (true);
        eq.setFont (ggui::serif (20.f, true));
        eq.setIndents (FormulaMarks::margin, 4);
        eq.onTextChange = [this] { pendingEdit = true; editCountdown = 8; marks.clear(); };
        addChildComponent (eq);
        eq.addAndMakeVisible (marks);
        math.font = [] (float sz, bool it) { return ggui::serif (sz, it); };
        math.onClick = [this] { startEditing(); };
        math.setMouseCursor (juce::MouseCursor::IBeamCursor);
        addAndMakeVisible (math);

        for (int i = 0; i < GraphProcessor::kMaxScenes; ++i)
        {
            auto* b = sceneTabs.add (new juce::TextButton());
            b->onClick = [this, i] { pickScene (i); };
            addChildComponent (b);
        }
        plus.setButtonText ("+");
        plus.setTooltip ("Add a scene");
        plus.onClick = [this] { const int i = proc.addScene(); if (i >= 0) pickScene (i); };
        addAndMakeVisible (plus);
        helpBtn.setButtonText ("?");
        helpBtn.setTooltip ("How the equations work");
        helpBtn.setClickingTogglesState (true);
        helpBtn.onClick = [this] { resized(); repaint(); };
        addAndMakeVisible (helpBtn);
        lookBtn.setTooltip ("Change the look: PAPER (the videos), CHROMA (rainbow watercolour), INK (the watercolour ghost), WINE (the burgundy video), TI 84 (a graphing calculator)");
        lookBtn.onClick = [this] { cycle ("look", 5); };
        tracksBtn.setButtonText ("TRACKS");
        tracksBtn.setTooltip ("Load one of the reference videos: every scene, at its own tempo");
        tracksBtn.onClick = [this] { showTracks(); };
        addAndMakeVisible (tracksBtn);
        genBtn.setButtonText ("GENERATE");
        genBtn.setTooltip ("Write a brand-new song in equations, the way the videos are written (right-click: pick the style)");
        genBtn.onClick = [this]
        {
            if (juce::ModifierKeys::currentModifiers.isPopupMenu()) { showGenerateStyles(); return; }
            generate (-1);
        };
        addAndMakeVisible (genBtn);
        more.setButtonText ("...");
        more.onClick = [this] { showMore(); };
        addAndMakeVisible (more);

        auto toggle = [this] (juce::TextButton& b, const char* text, const char* id, const char* tip)
        {
            b.setButtonText (text);
            b.setClickingTogglesState (true);
            b.setTooltip (tip);
            addAndMakeVisible (b);
            attachments.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (proc.apvts, id, b));
        };
        toggle (playBtn,  "PLAY",  "play",  "Play on its own clock (when SYNC is off)");
        toggle (syncBtn,  "SYNC",  "sync",  "Follow the DAW: plays when the DAW plays, x follows the song position");
        toggle (chainBtn, "CHAIN", "chain", "Play through every scene, one per 16 beats (off: loop the chosen scene)");
        toggle (keysBtn,  "KEYS",  "keys",  "Sound only while you hold a key; the key transposes (A3 = as written)");
        toggle (kitBtn,   "KIT",   "kit",   "Voices named hats, snare, clap or rim sound like drums, as in the videos (off: every voice is a tone)");

        rateBtn.onClick = [this] { cycle ("rate", 3); };
        rateBtn.setTooltip ("How fast x moves: 1/2x, 1x (one unit per beat), 2x");
        addAndMakeVisible (rateBtn);
        waveBtn.onClick = [this] { cycle ("wave", 6); };
        waveBtn.setTooltip ("The tone of every voice");
        addAndMakeVisible (waveBtn);

        auto knob = [this] (juce::Slider& s, const char* id, const char* label)
        {
            s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            s.setPopupDisplayEnabled (true, true, this);
            s.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
            addAndMakeVisible (s);
            sliderAttachments.add (new juce::AudioProcessorValueTreeState::SliderAttachment (proc.apvts, id, s));
            knobLabels.add (label);
        };
        knob (tempoK, "tempo", "TEMPO");
        knob (tuneK, "tune", "TUNE");
        knob (glideK, "glide", "GLIDE");
        knob (toneK, "tone", "TONE");
        knob (spaceK, "space", "SPACE");
        knob (widthK, "width", "WIDTH");
        knob (volK, "volume", "VOLUME");

        for (auto* c : stripComps()) strip.addAndMakeVisible (c);
        for (int i = 0; i < sceneTabs.size(); ++i) strip.addChildComponent (sceneTabs[i]);
        strip.paintLabels = [this] (juce::Graphics& g) { paintStripLabels (g); };
        addAndMakeVisible (strip);
        setResizable (true, true);
        setResizeLimits (860, 620, 2400, 1700);
        setSize (1054, 760);
        rebuildPrograms();
        showScene (proc.displayScene.load(), true);
        applyLook ((int) proc.apvts.getRawParameterValue ("look")->load());
        startTimerHz (30);
    }

    ~GraphEditor() override
    {
        stopTimer();
        flushEdit();
        setLookAndFeel (nullptr);
    }

    void paint (juce::Graphics& g) override
    {
        using namespace ggui;
        curTheme() = &ggui::theme (lookIdx);
        if (paperImg.getWidth() != getWidth() || paperImg.getHeight() != getHeight())
            paperImg = makeBackground (lookIdx, getWidth(), getHeight());
        g.drawImageAt (paperImg, 0, 0);

        const int count = proc.getNumScenes();
        g.setColour (th().amber);
        const float k = getWidth() / 1054.0f;   // everything on the page scales with the window, like a video
        // the Ghost Graph lettering for this look, centred in the top band
        {
            juce::Image art; float ah = 0, ay = 0;
            switch (lookIdx)
            {
                case 0: art = juce::ImageCache::getFromMemory (GhostGraphArt::paper_png,  GhostGraphArt::paper_pngSize);  ah = 37; ay = 6; break;
                case 1: art = juce::ImageCache::getFromMemory (GhostGraphArt::chroma_png, GhostGraphArt::chroma_pngSize); ah = 48; ay = 3; break;
                case 2: art = juce::ImageCache::getFromMemory (GhostGraphArt::ink_png,    GhostGraphArt::ink_pngSize);    ah = 41; ay = 5; break;
                case 3: art = juce::ImageCache::getFromMemory (GhostGraphArt::wine_png,   GhostGraphArt::wine_pngSize);   ah = 37; ay = 6; break;
                case 4: art = juce::ImageCache::getFromMemory (GhostGraphArt::ti_png,     GhostGraphArt::ti_pngSize);     ah = 30; ay = 8; break;
                default: break;
            }
            if (art.isValid())
            {
                const float hh = ah * k, ww = hh * art.getWidth() / (float) art.getHeight();
                const float ax = lookIdx == 4 ? getWidth() * 0.95f - ww : (getWidth() - ww) * 0.5f;   // TI: on the name plate's right
                g.setOpacity (1.0f);
                g.drawImage (art, juce::Rectangle<float> (ax, ay * k, ww, hh), juce::RectanglePlacement::stretchToFit);
            }
        }
        g.setFont (serif (23.f * k));
        const bool lcd = th().lcd;
        if (lcd) g.drawText (juce::String (proc.getScene (viewed).name), (int) (getWidth() * 0.05f), (int) (getHeight() * (ggui::ti::lcdTop + 0.008f)), 300, (int) (34 * k), juce::Justification::centredLeft);
        else g.drawText (juce::String (proc.getScene (viewed).name), (int) (60 * k), (int) (8 * k), 300, (int) (32 * k), juce::Justification::centredLeft);

        // progress: through the whole chain, or through this page when looping
        const double page = (lastX + 8.0) / 16.0;
        const bool chain = proc.apvts.getRawParameterValue ("chain")->load() > 0.5f;
        const double prog = chain ? (viewed + page) / juce::jmax (1, count) : page;
        auto bar = lcd ? juce::Rectangle<float> ((float) getWidth() * 0.95f - 130 * k, getHeight() * (ggui::ti::lcdTop + 0.02f), 130 * k, 13 * k)
                       : juce::Rectangle<float> ((float) getWidth() - 92 * k - 112 * k, 15 * k, 112 * k, 10 * k);
        g.setColour (th().amber.withAlpha (0.7f));
        g.drawRect (bar, 1.0f);
        g.setColour (th().amber);
        g.fillRect (bar.reduced (2.f).withWidth ((bar.getWidth() - 4) * (float) juce::jlimit (0.0, 1.0, prog)));

        // a mistake in the equations always shows, strip or not
        if (! errorText.isEmpty())
        {
            g.setFont (serif (15.f, true));
            g.setColour (th().coral);
            g.drawFittedText (errorText, juce::Rectangle<int> (22, getHeight() - 30, getWidth() - 140, 26), juce::Justification::centredLeft, 1);
        }

        // cheat sheet
        g.setColour (th().amber.withAlpha (0.42f));
        g.setFont (serif (14.f));
        const char* lines[] = {
            "x   time, in beats (-8 to 8 on each page)",
            "y = [ ... ]   one voice for each entry",
            "y = 0 is A3,  1 = an octave,  1/12 = a semitone",
            "{cond: value, cond: value, otherwise}",
            "   no match = that voice is silent",
            "F(a, b) = ...   your own functions",
            "frac  floor  ceil  round  mod  clamp",
            "min  max  abs  sqrt  sin  cos  exp  ln",
            "tri  saw  sq  e  pi      # comments",
        };
        auto h = helpArea;
        if (! helpArea.isEmpty())
        for (auto* l : lines) { g.drawText (l, h.removeFromTop (19), juce::Justification::centredLeft); }

        g.setColour (th().amber.withAlpha (0.8f));
        g.setFont (serif (19.f * k));
        g.drawText (juce::String (viewed + 1) + "/" + juce::String (count), pageArea, juce::Justification::centredRight);
    }

    void resized() override
    {
        const int w = getWidth(), h = getHeight();
        const bool lcd = lookIdx == 4;
        const int graphBottom = (int) (h * (lcd ? 0.53f : 0.565f));   // the calculator's screen also holds the equations
        const int graphTop = (int) (h * (lcd ? 0.12f : 0.075f));
        graph.setBounds (0, graphTop, w, graphBottom - graphTop);
        math.maxSize = 31.0f * w / 1054.0f;

        const int strip = 100;
        const int midBottom = lcd ? (int) (h * ggui::ti::lcdBottom) - 34 : h - 40;
        auto mid = juce::Rectangle<int> ((int) (w * (lcd ? 0.045f : 0.02f)), graphBottom + 6, (int) (w * (lcd ? 0.91f : 0.96f)), midBottom - graphBottom - 6);
        pageArea = lcd ? juce::Rectangle<int> ((int) (w * 0.95f) - 100, midBottom, 100, 30)
                       : juce::Rectangle<int> (w - (int) (w * 0.075f) - 100, h - 44, 100, 30);
        helpArea = {};
        if (helpBtn.getToggleState())
            helpArea = mid.removeFromRight (juce::jmin (380, (int) (w * 0.34f))).reduced (14, 6);   // the cheat sheet, when asked for
        statusArea = juce::Rectangle<int> (22, h - strip - 4, w - 44, 0);
        eq.setBounds (mid);
        math.setBounds (mid);
        editBtn.setBounds (mid.getRight() - 66, mid.getY() + 2, 64, 24);
        if (lcd) editBtn.setBounds (ggui::tiKey (5, (float) w, (float) h).toNearestInt());   // the sixth calculator key
        undoBtn.setBounds (mid.getRight() - (lcd ? 134 : 202), mid.getY() + 2, 64, 24);
        redoBtn.setBounds (mid.getRight() - (lcd ? 66 : 134), mid.getY() + 2, 64, 24);
        eq.setBounds (mid.withTrimmedRight (140));
        marks.setBounds (eq.getLocalBounds());

        this->strip.setBounds (getLocalBounds());
        this->strip.panel = juce::Rectangle<int> (0, h - strip - 30, w, strip + 30);
        statusArea = juce::Rectangle<int> (22, h - strip - 28, w - 44, 24);
        auto bottom = juce::Rectangle<int> (16, h - strip, w - 32, strip - 8);
        // row 1: the scenes on the left, TRACKS / LOOK / GENERATE on the right
        auto row1 = bottom.removeFromTop (30);
        genBtn.setBounds (row1.removeFromRight (104).reduced (2, 2));
        lookBtn.setBounds (row1.removeFromRight (118).reduced (2, 2));
        tracksBtn.setBounds (row1.removeFromRight (80).reduced (2, 2));
        helpBtn.setBounds (row1.removeFromRight (32).reduced (2, 2));
        more.setBounds (row1.removeFromRight (36).reduced (2, 2));
        plus.setBounds (row1.removeFromRight (32).reduced (2, 2));
        const int count = proc.getNumScenes();
        const int tabW = juce::jlimit (30, 58, count > 0 ? row1.getWidth() / count : 58);
        for (int i = 0; i < sceneTabs.size(); ++i)
        {
            sceneTabs[i]->setVisible (i < count);
            if (i < count) sceneTabs[i]->setBounds (row1.removeFromLeft (tabW).reduced (2, 2));
        }
        // row 2: the switches on the left, the knobs on the right
        bottom.removeFromTop (4);
        auto row2 = bottom;
        auto sw = row2.removeFromLeft (juce::jmin (470, row2.getWidth() / 2)).withHeight (28).translated (0, 8);
        for (auto* b : { &playBtn, &syncBtn, &chainBtn, &keysBtn, &kitBtn, &rateBtn, &waveBtn })
            b->setBounds (sw.removeFromLeft (b == &waveBtn ? 96 : (b == &kitBtn || b == &rateBtn ? 50 : 64)).reduced (2, 2));
        juce::Slider* ks[] = { &tempoK, &tuneK, &glideK, &toneK, &spaceK, &widthK, &volK };
        const int kw = juce::jmin (76, row2.getWidth() / 7);
        auto knobRow = row2.withSizeKeepingCentre (kw * 7, row2.getHeight());
        for (auto* k : ks) k->setBounds (knobRow.removeFromLeft (kw).withTrimmedBottom (14).withSizeKeepingCentre (40, 40));
    }

    void paintStripLabels (juce::Graphics& g)
    {
        using namespace ggui;
        g.setFont (serif (14.f, true));
        g.setColour (th().amber.withAlpha (0.5f));
        if (errorText.isEmpty()) g.drawText (okFlash.isNotEmpty() ? okFlash : okText, statusArea.withTrimmedRight (170), juce::Justification::centredLeft);
        g.setColour (th().amber.withAlpha (0.65f));
        g.drawText (juce::String::fromUTF8 ("Ghost Graph \xc2\xb7 v1 beta"), statusArea, juce::Justification::centredRight);
        g.setFont (serif (12.f));
        juce::Slider* ks[] = { &tempoK, &tuneK, &glideK, &toneK, &spaceK, &widthK, &volK };
        for (int i = 0; i < 7; ++i)
        {
            g.setColour (th().amber.withAlpha (ks[i]->isEnabled() ? 0.75f : 0.22f));
            g.drawText (knobLabels[i], ks[i]->getBounds().translated (0, ks[i]->getHeight() - 2).withHeight (14).expanded (12, 0),
                        juce::Justification::centred);
        }
    }

    std::vector<juce::Component*> stripComps()
    {
        return { &plus, &more, &helpBtn, &tracksBtn, &lookBtn, &genBtn, &playBtn, &syncBtn, &chainBtn, &keysBtn, &kitBtn, &rateBtn, &waveBtn,
                 &tempoK, &tuneK, &glideK, &toneK, &spaceK, &widthK, &volK };
    }

    void updateStripVisibility()
    {
        const auto mp = getMouseXYRelative();
        const bool near = isMouseOver (true) && mp.y > getHeight() - 170;
        if (near || editing || ! errorText.isEmpty() || forceStrip) stripHold = 75;   // stays 2.5 s after the mouse leaves
        else if (stripHold > 0) --stripHold;
        const float target = stripHold > 0 ? 1.0f : 0.0f;
        stripAlpha += (target - stripAlpha) * 0.35f;
        if (std::abs (stripAlpha - target) < 0.02f) stripAlpha = target;
        strip.setAlpha (stripAlpha);
        strip.setVisible (stripAlpha > 0.01f);
    }

    void applyLook (int idx)
    {
        lookIdx = juce::jlimit (0, 4, idx);
        look.applyColours (lookIdx);
        graph.theme = strip.theme = &ggui::theme (lookIdx);
        math.colour = ggui::theme (lookIdx).amber;
        math.halo = (lookIdx != 1 && lookIdx != 2) ? juce::Colours::transparentBlack : ggui::theme (lookIdx).paper.withAlpha (0.55f);
        paperImg = {};
        sendLookAndFeelChange();
        eq.applyColourToAllText (ggui::theme (lookIdx).amber);
        marks.theme = &ggui::theme (lookIdx);
        // over watercolour, the typing gets a soft paper card behind it so it reads
        eq.setColour (juce::TextEditor::backgroundColourId, (lookIdx == 1 || lookIdx == 2) ? ggui::theme (lookIdx).paper.withAlpha (0.78f) : juce::Colours::transparentBlack);
        lookBtn.setButtonText (juce::String ("LOOK: ") + ggui::theme (lookIdx).name);
        resized();   // the TI 84 lays the page out like the calculator
        repaint();
    }

    int getViewedScene() const { return viewed; }
    void setEditing (bool on) { if (on != editing) { if (on) startEditing(); else stopEditing(); } }
    bool isEditing() const { return editing; }
    size_t undoCount() const { return undo.size(); }
    void pressUndo() { undoDrag(); }
    std::string equationText() const { return eq.getText().toStdString(); }
    void pressDelete (int v, double a, double b) { deleteNote (v, a, b); }
    void pressRedo() { redoDrag(); }
    void showStrip (bool on) { forceStrip = on; stripHold = on ? 75 : 0; stripAlpha = on ? 1.0f : 0.0f; updateStripVisibility(); }
    MathView& getMath() { return math; }
    const FormulaMarks& getMarks() const { return marks; }
    GraphView& getGraph() { return graph; }

private:
    GraphProcessor& proc;
    ggui::Look look;
    GraphView graph;
    juce::TextEditor eq;
    FormulaMarks marks { eq };
    ControlStrip strip;
    float stripAlpha = 1.0f;
    int lookIdx = 0;
    int stripHold = 120;
    bool forceStrip = false;
    MathView math;
    bool editing = false;
    juce::OwnedArray<juce::TextButton> sceneTabs;
    juce::TextButton editBtn, undoBtn, redoBtn, plus, more, helpBtn, tracksBtn, lookBtn, genBtn, playBtn, syncBtn, chainBtn, keysBtn, kitBtn, rateBtn, waveBtn;
    juce::Slider tempoK, tuneK, glideK, toneK, spaceK, widthK, volK;
    juce::StringArray knobLabels;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> attachments;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachments;
    juce::TooltipWindow tips { this, 600 };

    std::vector<std::unique_ptr<gg::Program>> programs;   // the window's own copies (the sound has its own)
    int viewed = 0, seenVersion = -1, editCountdown = 0;
    bool pendingEdit = false;
    double lastX = -8.0;
    juce::String errorText, okText, okFlash;
    int flashCountdown = 0;
    juce::Rectangle<int> helpArea, statusArea, pageArea;
    juce::Image paperImg;

    void rebuildPrograms()
    {
        programs.clear();
        for (int i = 0; i < proc.getNumScenes(); ++i)
            programs.push_back (gg::Program::compile (proc.getScene (i).text));
        seenVersion = proc.scenesVersion.load();
        resized();
    }

    void showScene (int i, bool force)
    {
        i = juce::jlimit (0, proc.getNumScenes() - 1, i);
        if (i == viewed && ! force) return;
        flushEdit();
        graph.clearHighlight(); marks.clear();   // another scene (or new math): nothing selected
        viewed = i;
        eq.setText (proc.getScene (i).text, juce::dontSendNotification);
        graph.setProgram (programs[(size_t) i].get());
        math.setProgram (programs[(size_t) i].get());
        updateStatus();
        repaint();
    }

    void pickScene (int i)
    {
        if (auto* p = proc.apvts.getParameter ("chain")) p->setValueNotifyingHost (0.0f);
        if (auto* p = proc.apvts.getParameter ("scene")) p->setValueNotifyingHost (p->convertTo0to1 ((float) (i + 1)));
        proc.displayScene = i;
        showScene (i, true);
    }

    void cycle (const char* id, int n)
    {
        if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (id)))
            p->setValueNotifyingHost (p->convertTo0to1 ((float) ((p->getIndex() + 1) % n)));
    }

    void showMore()
    {
        juce::PopupMenu m;
        m.addItem (1, "Delete this scene", proc.getNumScenes() > 1);
        m.addItem (2, "Restore the six original scenes");
        m.addSeparator();
        m.addItem (3, "Generate just this scene (same key and chords)");
        juce::PopupMenu styles;
        for (int i = 0; i < gg::Generator::kStyles; ++i) styles.addItem (11 + i, gg::Generator::styleName (i));
        m.addSubMenu ("GENERATE in the style of...", styles);
        m.addItem (4, "Undo GENERATE", proc.canUndoGenerate());
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&more), [this] (int r)
        {
            if (r == 1) { proc.removeScene (viewed); rebuildPrograms(); showScene (juce::jmin (viewed, proc.getNumScenes() - 1), true); }
            if (r == 2) { proc.resetScenes(); rebuildPrograms(); showScene (0, true); }
            if (r == 3) { pendingEdit = false; proc.generateSceneHere (viewed); rebuildPrograms(); showScene (viewed, true); }
            if (r == 4) { pendingEdit = false; proc.undoGenerate(); rebuildPrograms(); showScene (0, true); }
            if (r >= 11 && r < 11 + gg::Generator::kStyles) generate (r - 11);
        });
    }

    void showTracks()
    {
        juce::PopupMenu m;
        const auto tracks = gg::factoryTracks();
        for (size_t i = 0; i < tracks.size(); ++i)
            m.addItem ((int) i + 1, juce::String (tracks[i].name) + "   (" + juce::String ((int) tracks[i].scenes.size()) + " scenes, "
                                    + juce::String ((int) tracks[i].bpm) + " BPM)");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&tracksBtn), [this] (int r)
        {
            if (r <= 0) return;
            pendingEdit = false;
            proc.loadTrack (r - 1);
            rebuildPrograms();
            showScene (0, true);
        });
    }

    void generate (int style)
    {
        pendingEdit = false;
        proc.generateTrack ((juce::uint32) juce::Random::getSystemRandom().nextInt(), style);
        rebuildPrograms();
        showScene (0, true);
    }

    void showGenerateStyles()
    {
        juce::PopupMenu m;
        m.addItem (100, "Any style");
        for (int i = 0; i < gg::Generator::kStyles; ++i) m.addItem (101 + i, gg::Generator::styleName (i));
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&genBtn), [this] (int r)
        {
            if (r == 100) generate (-1);
            else if (r > 100) generate (r - 101);
        });
    }

    void flushEdit()
    {
        if (! pendingEdit) return;
        pendingEdit = false;
        const std::string t = eq.getText().toStdString();
        proc.setSceneText (viewed, t);
        if (viewed < (int) programs.size())
        {
            programs[(size_t) viewed] = gg::Program::compile (t);
            graph.setProgram (programs[(size_t) viewed].get());
            math.setProgram (programs[(size_t) viewed].get());
        }
        updateStatus();
    }

    void updateStatus()
    {
        errorText.clear();
        okText.clear();
        if (viewed >= (int) programs.size()) return;
        auto& p = *programs[(size_t) viewed];
        if (! p.ok())
        {
            const auto& e = p.getErrors().front();
            errorText = "line " + juce::String (e.line) + ": " + juce::String (e.message)
                      + (p.getErrors().size() > 1 ? "   (+" + juce::String ((int) p.getErrors().size() - 1) + " more)" : juce::String());
        }
        else okText = juce::String (p.numVoices()) + (p.numVoices() == 1 ? " voice" : " voices")
                    + (editing ? juce::String::fromUTF8 ("   \xc2\xb7   drag a note to move it (its ends stretch or tilt it), Cmd+drag a box to pick several, right-click to copy, paste or delete   \xc2\xb7   DONE when finished") : juce::String::fromUTF8 ("   \xc2\xb7   EDIT to change the equations or drag notes   \xc2\xb7   GENERATE writes a new track"));
        const bool showText = editing || ! p.ok();
        graph.editMode = editing;
        editBtn.setButtonText (editing ? "DONE" : "EDIT");
        editBtn.setToggleState (editing, juce::dontSendNotification);
        undoBtn.setVisible (editing);
        undoBtn.setEnabled (! undo.empty());
        redoBtn.setVisible (editing);
        redoBtn.setEnabled (! redo.empty());
        graph.repaint();
        eq.setVisible (showText);
        math.setVisible (! showText);
        if (! showText && mathStale && viewed < (int) programs.size()) { mathStale = false; math.setProgram (programs[(size_t) viewed].get()); }
        repaint (statusArea);
    }

    void startEditing()
    {
        editing = true;
        pickScene (viewed);   // the scene you edit keeps looping, so you hear every change
        updateStatus();
        eq.grabKeyboardFocus();
        eq.moveCaretToEnd();
    }
    void stopEditing()
    {
        editing = false;
        flushEdit();
        graph.clearHighlight();
        marks.clear();
        unfocusAllComponents();
        updateStatus();
    }

    // ---- note dragging
    std::string dragBase;
    gg::NotePlan dragPlan;
    std::vector<GraphView::Sel> groupNotes;
    int dragVoice = -1;
    double grabX0 = 0, grabX1 = 0;
    std::vector<std::pair<int, std::string>> undo;   // (scene, text before the drag)
    std::vector<std::pair<int, std::string>> redo;   // (scene, text an UNDO took back)
    bool mathStale = false;

    void setViewedText (const std::string& t, bool live = false)
    {
        proc.setSceneText (viewed, t);
        if (viewed < (int) programs.size())
        {
            programs[(size_t) viewed] = gg::Program::compile (t);
            graph.setProgram (programs[(size_t) viewed].get());
            if (math.isVisible()) math.setProgram (programs[(size_t) viewed].get());
            else mathStale = true;   // typesetting is slow: done once the math shows again
        }
        if (! live) eq.setText (t, juce::dontSendNotification);   // mid-drag the text box catches up when you let go
        pendingEdit = false;
    }

    void applyDrag (int part, double dx, double dy, bool final)
    {
        if (! dragPlan.ok || dragVoice < 0) return;
        const auto& s0 = dragPlan.start;
        gg::NoteEdit n = s0;
        const double minLen = 1.0 / 32.0;
        if (part == 0) { n.A = s0.A + dx; n.B = s0.B + dx; n.D0 = s0.D0 + dy; n.D1 = s0.D1 + dy; }
        else if (part == 1) { n.A = std::min ((dx != 0 ? grabX0 : s0.A) + dx, s0.B - minLen); n.D0 = s0.D0 + dy; }
        else { n.B = std::max ((dx != 0 ? grabX1 : s0.B) + dx, s0.A + minLen); n.D1 = s0.D1 + dy; }
        const bool moved = std::abs (n.A - s0.A) > 1e-9 || std::abs (n.B - s0.B) > 1e-9 || std::abs (n.D0 - s0.D0) > 1e-9 || std::abs (n.D1 - s0.D1) > 1e-9;
        const std::string t = moved ? gg::applyNote (dragBase, dragPlan, n, gg::NoteOp::Move) : dragBase;
        if (t != proc.getScene (viewed).text) setViewedText (t, ! final);
        else if (final) eq.setText (t, juce::dontSendNotification);
        graph.setHighlight (dragVoice, n.A, n.B);
        if (final)
        {
            if (t != dragBase) { undo.push_back ({ viewed, dragBase }); redo.clear(); }
            if (undo.size() > 100) undo.erase (undo.begin());
            dragVoice = -1;
            updateStatus();
            markFormula();
        }
    }

    void pushUndo (const std::string& before)
    {
        undo.push_back ({ viewed, before }); redo.clear();
        if (undo.size() > 100) undo.erase (undo.begin());
    }

    // Delete on a selected note: it goes quiet in its formula, and UNDO brings it back
    void deleteNote (int v, double a, double b) { deleteNotes ({ GraphView::Sel { v, a, b } }); }
    void deleteNotes (const std::vector<GraphView::Sel>& notes)
    {
        flushEdit();
        const std::string base = proc.getScene (viewed).text;
        std::string t = base;
        for (auto& n : notes)
        {
            const auto plan = gg::planNote (t, n.v, n.a, n.b);
            if (! plan.ok) continue;
            const std::string r = gg::applyNote (t, plan, plan.start, gg::NoteOp::Delete);
            if (gg::Program::compile (r)->ok()) t = r;
        }
        if (t == base) return;
        setViewedText (t);
        pushUndo (base);
        updateStatus();
    }

    // several selected notes dragged together: each one moved in its own formula, one after another (the one furthest
    // along the way they go first, so none lands on another before that one has moved)
    std::string moveNotes (const std::string& base, std::vector<GraphView::Sel> notes, double dx, double dy, std::vector<GraphView::Sel>* landed)
    {
        std::sort (notes.begin(), notes.end(), [dx] (const GraphView::Sel& p, const GraphView::Sel& q) { return dx > 0 ? p.a > q.a : p.a < q.a; });
        std::string t = base;
        if (landed) landed->clear();
        for (auto& sel : notes)
        {
            const auto plan = gg::planNote (t, sel.v, sel.a, sel.b);
            if (! plan.ok) continue;
            gg::NoteEdit n = plan.start;
            n.A += dx; n.B += dx; n.D0 += dy; n.D1 += dy;
            if (dx != 0 || dy != 0)
            {
                const std::string r = gg::applyNote (t, plan, n, gg::NoteOp::Move);
                if (gg::Program::compile (r)->ok()) t = r;
            }
            if (landed) landed->push_back ({ sel.v, sel.a + dx, sel.b + dx });
        }
        return t;
    }
    void applyGroupDrag (double dx, double dy, bool final)
    {
        if (groupNotes.empty()) return;
        std::vector<GraphView::Sel> landed;
        const std::string t = moveNotes (dragBase, groupNotes, dx, dy, &landed);
        if (t != proc.getScene (viewed).text) setViewedText (t, ! final);
        else if (final) eq.setText (t, juce::dontSendNotification);
        graph.setSelection (landed);
        if (final)
        {
            if (t != dragBase) pushUndo (dragBase);
            groupNotes.clear();
            updateStatus();
            markFormula();
        }
    }
    void undoDrag()
    {
        if (undo.empty()) return;
        const auto u = undo.back();
        undo.pop_back();
        if (u.first != viewed) showScene (u.first, true);
        redo.push_back ({ u.first, proc.getScene (u.first).text });
        setViewedText (u.second);
        updateStatus();
        markFormula();
    }
    void redoDrag()
    {
        if (redo.empty()) return;
        const auto r = redo.back();
        redo.pop_back();
        if (r.first != viewed) showScene (r.first, true);
        undo.push_back ({ r.first, proc.getScene (r.first).text });
        setViewedText (r.second);
        updateStatus();
        markFormula();
    }

    // ---- the selected note in the typed math
    static int charIndex (const std::string& t, size_t byte) { return juce::String::fromUTF8 (t.data(), (int) std::min (byte, t.size())).length(); }
    juce::Range<int> chars (const std::string& t, gg::Span sp) const { return { charIndex (t, sp.b), charIndex (t, sp.e) }; }

    void markFormula()
    {
        marks.clear();
        const auto& sel = graph.selection();
        if (! editing || sel.empty() || pendingEdit) return;
        const std::string t = eq.getText().toStdString();
        auto p = gg::Program::compile (t);
        if (! p->ok()) return;
        for (size_t s = 0; s < sel.size() && s < 32; ++s)
        {
            const int v = sel[s].v;
            if (v < 0 || v >= p->numVoices()) continue;
            const auto spots = p->locate (v, 0.5 * (sel[s].a + sel[s].b));
            const gg::Span entry = gg::voiceEntrySpan (t, v);
            gg::Span mainSpan;
            for (size_t i = 0; i < spots.size(); ++i)
            {
                gg::Span whole, piece;
                if (spots[i].def < 0) { whole = entry; const auto it = gg::pieceItems (t, entry); if (spots[i].branch >= 0 && spots[i].branch < (int) it.size()) piece = it[(size_t) spots[i].branch]; }
                else { whole = gg::lineSpan (t, p->getDefs()[(size_t) spots[i].def].line); piece = gg::branchSpan (t, whole, spots[i].branch); }
                const gg::Span here = piece.ok() ? piece : whole;
                if (! here.ok()) continue;
                if (! mainSpan.ok()) mainSpan = here;                   // where this note is written
                else if (s == 0) marks.others.add (chars (t, here));    // what it plays from
            }
            if (! mainSpan.ok()) mainSpan = entry;                      // a voice written straight into y = [...]
            else if (s == 0 && entry.ok() && entry.b != mainSpan.b && ! (mainSpan.b >= entry.b && mainSpan.e <= entry.e)) marks.others.add (chars (t, entry));
            if (mainSpan.ok()) marks.mains.addIfNotAlreadyThere (chars (t, mainSpan));
        }
        if (marks.mains.isEmpty()) return;
        // scroll the text box so it shows (moving the caret there; the graph keeps the keyboard for Delete / Cmd+C)
        eq.setCaretPosition (marks.mains.getFirst().getEnd());
        eq.setCaretPosition (marks.mains.getFirst().getStart());
        marks.repaint();
    }

    // ---- copy and paste notes (right-click on the graph, or Cmd+C / Cmd+V)
    struct ClipNote { int voice = -1; std::string name; gg::NoteEdit n; };
    std::vector<ClipNote> clip;
    double clipX = 0, clipY = 0;   // the first note's start: a paste puts that point where you click

    void copyNotes (const std::vector<GraphView::Sel>& notes)
    {
        flushEdit();
        if (notes.empty() || viewed >= (int) programs.size()) return;
        const std::string t = proc.getScene (viewed).text;
        auto p = gg::Program::compile (t);
        if (! p->ok()) return;
        std::vector<ClipNote> c;
        double x0 = 1e9, y0 = std::numeric_limits<double>::quiet_NaN();
        for (auto& sel : notes)
        {
            const auto plan = gg::planNote (t, sel.v, sel.a, sel.b);
            if (! plan.ok) continue;
            c.push_back ({ sel.v, p->voiceLabel (sel.v), plan.start });
            if (plan.start.A < x0)
            {
                x0 = plan.start.A;
                y0 = std::numeric_limits<double>::quiet_NaN();
                for (double f : { 0.02, 0.1, 0.25, 0.5 }) { p->setX (sel.a + (sel.b - sel.a) * f); y0 = p->voice (sel.v); if (std::isfinite (y0)) break; }
            }
        }
        if (c.empty() || ! std::isfinite (y0)) return;
        clip = c; clipX = x0; clipY = y0;
        okFlash = juce::String::fromUTF8 ("copied   \xc2\xb7   right-click the paper where it should go and Paste (or Cmd+V)");
        flashCountdown = 90;
        repaint (statusArea);
    }
    bool canPaste() const
    {
        if (clip.empty() || viewed >= (int) programs.size()) return false;
        std::string t = proc.getScene (viewed).text;
        while (! t.empty() && t.back() == '\n') t.pop_back();
        for (auto& c : clip) t += "\nggpasteprobe" + std::to_string (&c - clip.data()) + " = " + c.n.V;   // what it plays still reads here
        return gg::Program::compile (t)->ok();
    }
    static bool busy (gg::Program& p, int v, double A, double B)
    {
        const double step = std::min (1.0 / 64.0, (B - A) / 8.0);
        for (double t = A + step * 0.5; t < B; t += step) { p.setX (t); if (std::isfinite (p.voice (v))) return true; }
        return false;
    }
    void pasteNotes (double x, double y, bool fine)
    {
        flushEdit();
        if (! canPaste()) return;
        const std::string base = proc.getScene (viewed).text;
        const double A0 = fine ? std::round (x * 16.0) / 16.0 : std::round (x * 4.0) / 4.0;                  // a quarter beat (Alt: 1/16)
        const double dx = A0 - clipX;
        const double dy = fine ? std::round ((y - clipY) * 1200.0) / 1200.0 : std::round ((y - clipY) * 12.0) / 12.0;   // semitones (Alt: cents)
        std::string t = base;
        std::map<int, int> target;   // where each copied voice's notes go
        std::vector<GraphView::Sel> landed;
        for (auto& c : clip)
        {
            auto p = gg::Program::compile (t);
            if (! p->ok()) break;
            gg::NoteEdit n = c.n;
            n.A += dx; n.B += dx; n.D0 += dy; n.D1 += dy;
            // on the voice it came from where that voice is quiet, else a voice of its own (a chord)
            int v = target.count (c.voice) ? target[c.voice] : (c.voice < p->numVoices() ? c.voice : -1);
            if (v >= 0 && busy (*p, v, n.A, n.B)) v = -1;
            std::string r;
            if (v < 0 && p->numVoices() < gg::Program::kMaxVoices) r = gg::addVoice (t, c.name, n, &v);
            else
            {
                if (v < 0) v = c.voice < p->numVoices() ? c.voice : 0;   // 16 voices: over what that voice had
                const auto plan = gg::planNote (t, v, n.A, n.B);
                r = gg::applyNote (t, plan, n, gg::NoteOp::Copy);
            }
            if (r == t || ! gg::Program::compile (r)->ok()) continue;
            t = r;
            target[c.voice] = v;
            landed.push_back ({ v, n.A, n.B });
        }
        if (t == base) return;
        setViewedText (t);
        pushUndo (base);
        graph.setSelection (landed);   // the copies are selected: drag them, Delete them, or paste again
        updateStatus();
        markFormula();
    }

    void timerCallback() override
    {
        if (proc.scenesVersion.load() != seenVersion) { rebuildPrograms(); showScene (viewed, true); }
        if (pendingEdit && --editCountdown <= 0) flushEdit();
        if (marks.active()) marks.repaint();   // follows the text box when it scrolls
        if (flashCountdown > 0 && --flashCountdown == 0) { okFlash.clear(); repaint (statusArea); }
        // (editing stays on until DONE: clicking the graph to drag a note doesn't end it)

        // follow the scene that plays, unless you are typing in this one
        if (! editing && ! eq.hasKeyboardFocus (true)) showScene (proc.displayScene.load(), false);

        const double x = proc.displayX.load();
        if (x != lastX || proc.displayScene.load() != viewed)
        {
            lastX = proc.displayScene.load() == viewed ? x : lastX;
            graph.setX (lastX);
            graph.repaint();
            repaint (getWidth() - 160, 10, 150, 22);
        }

        updateStripVisibility();
        if (const int lk = (int) proc.apvts.getRawParameterValue ("look")->load(); lk != lookIdx) applyLook (lk);

        // what does nothing right now is greyed out
        const bool standalone = proc.isStandalone();
        const bool sync = proc.apvts.getRawParameterValue ("sync")->load() > 0.5f && ! standalone;
        syncBtn.setEnabled (! standalone);
        playBtn.setEnabled (! sync);
        tempoK.setEnabled (! sync);
        glideK.setEnabled (true);
        plus.setEnabled (proc.getNumScenes() < GraphProcessor::kMaxScenes);
        for (int i = 0; i < sceneTabs.size(); ++i)
        {
            auto* b = sceneTabs[i];
            if (i < proc.getNumScenes()) b->setButtonText (proc.getScene (i).name);
            b->setToggleState (i == viewed, juce::dontSendNotification);
        }
        static const char* rates[] = { "1/2x", "1x", "2x" };
        static const char* waves[] = { "SINE", "TRIANGLE", "SAW", "SQUARE", "SOFT SAW", "ROUND" };
        const int ri = juce::jlimit (0, 2, (int) proc.apvts.getRawParameterValue ("rate")->load());
        const int wi = juce::jlimit (0, 5, (int) proc.apvts.getRawParameterValue ("wave")->load());
        rateBtn.setButtonText (rates[ri]);
        waveBtn.setButtonText (waves[wi]);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GraphEditor)
};
