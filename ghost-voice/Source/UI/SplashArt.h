// Ghost Voice - AGPL-3.0-only (see LICENSE, NOTICE.md).
//
// SPLASH (build 9, Brandon 2026-11-13): the whole window in the palette of Brandon's painting: a pale mint ground with vermilion,
// amber, gold, magenta, pink, orchid, cyan and sky washes, splatter and drips. Everything is gooey and rounded and melts a little;
// outlines are sketched: thin lines drawn over and over, each a hair off the last. The logo is written in thousands of tiny hairs,
// every one a different colour of the palette. Text always sits on milky panels in dark plum ink, so it stays easy to read.
// Every random choice is seeded from where a thing is, so nothing flickers; the big art is drawn once per window size and cached.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "GhostThemes.h"
#include "GhostArt.h"
#include "HandDraw.h"
#include <cmath>
#include <map>

namespace gv::splash
{
// DARK (build 12, Brandon 2026-11-13): the same hand-drawn window in the colours of his dark reference: a deep plum-indigo night with
// soft purple and rose-peach glows, dark glassy cards, white text, a lime highlight. One switch (Settings: LIGHT / DARK) picks it.
inline bool& darkFlag() { static bool d = false; return d; }   // set by the window before it paints (message thread)
inline bool isDark() { return darkFlag(); }

// everything the art draws with, for each look
struct Art
{
    juce::Colour paints[8];          // washes, splatter, tints (art only)
    juce::Colour inks[8];            // thin lines: readable on the ground
    juce::Colour on0, on1, onHatch, onInk;   // an ON button: its wash, its hatching, its ink line
    juce::Colour wave0, wave1, wave2;        // the waveform's gradient
    juce::Colour play, select;               // the playhead / loop, the selection / tools
    juce::Colour mist;                       // what keeps the middle of the window calm
};
inline const Art& art()
{
    using C = juce::Colour;
    static const Art light {
        { C (0xffe04a26), C (0xfff2a93b), C (0xfff7d36b), C (0xffe45bc9), C (0xfff6a3d8), C (0xffb178ec), C (0xff5fd6e6), C (0xff9fe3ee) },
        { C (0xffc93a17), C (0xffd4801a), C (0xffc99a14), C (0xffc93aa8), C (0xffd2589f), C (0xff8f4fd6), C (0xff1aa5b8), C (0xff3b9fc4) },
        C (0xffc93aa8), C (0xff8f4fd6), C (0xff5a1f8a), C (0xff2a1440),
        C (0xff1aa5b8), C (0xff8f4fd6), C (0xffc93aa8),
        C (0xff1aa5b8), C (0xff8f4fd6), C (0x73ffffff) };
    static const Art dark {
        { C (0xff5b4a8a), C (0xff7d5fc0), C (0xffc47a86), C (0xffd9967e), C (0xff3b3470), C (0xff6a3f6e), C (0xffa996e6), C (0xffe8f27c) },
        { C (0xffc9b8f5), C (0xffe7a7b6), C (0xfff0c09e), C (0xffb3a2f0), C (0xff8f86d6), C (0xffd6a6e0), C (0xffe8f27c), C (0xffa8c8f0) },
        C (0xff7a5cc4), C (0xffc07a8e), C (0xff1b1528), C (0xfff4f1fa),
        C (0xff9f8ae0), C (0xffc98aa0), C (0xffe9b39a),
        C (0xffe8f27c), C (0xffb3a2f0), C (0x66161320) };
    return darkFlag() ? dark : light;
}
constexpr int kPaints = 8;

inline ghosty::themes::Palette makePalette (bool dark)
{
    using C = juce::Colour; ghosty::themes::Palette p;
    if (dark)
    {
        p.theme = ghosty::themes::Theme::Dark; p.isLight = false;
        p.window = C (0xff1b1828); p.windowTop = C (0xff221c33); p.windowBottom = C (0xff15121f);
        p.panel = C (0xff272135); p.card = C (0xff2f283f); p.cell = C (0xff3a3150); p.line = C (0xff4d4366);
        p.ink = C (0xfff4f1fa); p.dim = C (0xffbdb4d0); p.onAccent = C (0xff16131f); p.overlay = C (0xffffffff);
        p.accent1 = C (0xffd79aa5); p.accent2 = C (0xffa996e6); p.accent3 = C (0xffe8f27c); p.violet = C (0xffa58cf0);
        p.good = C (0xff5fd38d); p.warn = C (0xffe8c35a); p.bad = C (0xffec6a62);
        p.keyWhite = C (0xffe9e4f2); p.keyBlack = C (0xff14111c);
        p.waveBg = C (0xff201b2e); p.waveLo = C (0xff9f8ae0); p.waveHi = C (0xffd79aa5); p.shadow = C (0x66000000);
        return p;
    }
    p.theme = ghosty::themes::Theme::Light; p.isLight = true;
    p.window = C (0xffe2efed); p.windowTop = C (0xffecf5f3); p.windowBottom = C (0xffd8e9e6);
    p.panel = C (0xfff6faf9); p.card = C (0xfffbfdfc); p.cell = C (0xffe6eff0); p.line = C (0xffa9c3c4);
    p.ink = C (0xff1f1a2e); p.dim = C (0xff4d4661); p.onAccent = C (0xffffffff); p.overlay = C (0xff1f1a2e);
    p.accent1 = C (0xffbf2d8c); p.accent2 = C (0xff0e8396); p.accent3 = C (0xffad6008); p.violet = C (0xff8a3fd1);
    p.good = C (0xff12805a); p.warn = C (0xffa86505); p.bad = C (0xffc0331a);
    p.keyWhite = C (0xfffbfdfc); p.keyBlack = C (0xff2a2238);
    p.waveBg = C (0xfff2f8f7); p.waveLo = C (0xff1aa5b8); p.waveHi = C (0xffc93aa8); p.shadow = C (0x2a1f1a2e);
    return p;
}
inline const ghosty::themes::Palette& palette (bool dark) { static const auto l = makePalette (false), d = makePalette (true); return dark ? d : l; }
inline const ghosty::themes::Palette& palette() { return palette (darkFlag()); }

inline juce::int64 seedOf (juce::Rectangle<float> r) { return (juce::int64) (r.getX() * 7919.0f + r.getY() * 104729.0f + r.getWidth() * 31.0f + r.getHeight() * 131.0f) + 4922; }

// a path drawn over and over, each pass a hair off the last: the sketched outline
inline void sketch (juce::Graphics& g, const juce::Path& p, juce::int64 seed, int passes = 6, float width = 0.7f, float alpha = 0.45f, juce::Colour only = {})
{
    juce::Random rng (seed);
    for (int k = 0; k < passes; ++k)
    {
        const float dx = (rng.nextFloat() - 0.5f) * 2.2f, dy = (rng.nextFloat() - 0.5f) * 2.2f;
        const auto c = only.isTransparent() ? art().inks[rng.nextInt (kPaints)] : only;
        g.setColour (c.withAlpha (alpha * (0.55f + 0.45f * rng.nextFloat())));
        g.strokePath (p, juce::PathStrokeType (width * (0.7f + 0.6f * rng.nextFloat()), juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                      juce::AffineTransform::translation (dx, dy));
    }
}

// drips hanging under a shape's bottom edge (never longer than `maxLen`, so they stay in the gap below it)
inline juce::Path drips (juce::Rectangle<float> r, float rad, juce::int64 seed, float maxLen = 8.0f)
{
    juce::Path d; juce::Random rng (seed * 31 + 7);
    if (r.getHeight() < 36 || r.getWidth() < 60) return d;
    const int n = juce::jlimit (1, 4, (int) (r.getWidth() / 170.0f) + rng.nextInt (2));
    for (int i = 0; i < n; ++i)
    {
        const float w = 5.0f + rng.nextFloat() * 6.0f, len = maxLen * (0.45f + 0.55f * rng.nextFloat());
        const float x = r.getX() + rad + 4 + rng.nextFloat() * juce::jmax (1.0f, r.getWidth() - 2 * rad - 8 - w);
        const float y = r.getBottom() - 3.0f;
        juce::Path one;
        one.startNewSubPath (x - 3.0f, y);
        one.cubicTo (x + w * 0.15f, y + 1.5f, x + w * 0.1f, y + len * 0.6f, x + w * 0.35f, y + len);
        one.addEllipse (x + w * 0.5f - w * 0.42f, y + len - w * 0.35f, w * 0.84f, w * 0.84f);
        one.startNewSubPath (x + w * 0.65f, y + len);
        one.cubicTo (x + w * 0.9f, y + len * 0.6f, x + w * 0.85f, y + 1.5f, x + w + 3.0f, y);
        one.closeSubPath();
        d.addPath (one);
    }
    return d;
}


// a hand-drawn button: paper with a few pale hatch lines (OFF) or a painted wash (ON), and an ink line round it
inline void handButton (juce::Graphics& g, const ghosty::themes::Palette& P, juce::Rectangle<float> r, bool on, bool enabled, bool over, juce::int64 seed)
{
    const float rad = juce::jmin (r.getHeight() * 0.5f, 14.0f);
    const auto& box = handBox (r.reduced (1.5f), rad, seed);
    const float a = enabled ? 1.0f : 0.42f;
    const auto tint = art().paints[(int) ((seed & 0x7fffffff) % kPaints)];
    if (on)
    {
        juce::ColourGradient gr (art().on0.withMultipliedAlpha (a), r.getX(), r.getY(), art().on1.withMultipliedAlpha (a), r.getRight(), r.getBottom(), false);
        g.setGradientFill (gr); g.fillPath (box);
        hatch (g, box, seed, art().onHatch.withAlpha (0.28f * a), 4.5f);
    }
    else
    {
        g.setColour ((over && enabled ? P.card.interpolatedWith (tint, 0.22f) : P.card).withMultipliedAlpha (a)); g.fillPath (box);
        if (enabled) hatch (g, box, seed, tint.withAlpha (over ? 0.32f : 0.2f), 6.0f);
    }
    if (enabled) inkLine (g, box, seed + 3, on ? art().onInk : P.ink, 1.2f, 0.7f);
    else { g.setColour (P.line.withAlpha (0.6f)); g.strokePath (box, juce::PathStrokeType (0.9f)); }
}

// a gooey milky panel: rounded, melting a little at the bottom, a soft wash of one paint inside, a sketched outline
inline void gooPanel (juce::Graphics& g, const ghosty::themes::Palette& P, juce::Rectangle<float> r, float rad, juce::Colour glow = {})
{
    const auto seed = seedOf (r);
    juce::Random rng (seed);
    rad = juce::jmax (rad, juce::jmin (16.0f, r.getHeight() * 0.3f));
    juce::Path body; body.addRoundedRectangle (r, rad);
    const auto d = drips (r, rad, seed);
    g.setColour (P.shadow); g.fillPath (body, juce::AffineTransform::translation (0, 3));
    const auto tint = art().paints[rng.nextInt (kPaints)];
    juce::ColourGradient gr (P.panel, r.getX(), r.getY(), P.panel.interpolatedWith (tint, 0.10f), r.getX(), r.getBottom(), false);
    g.setGradientFill (gr); g.fillPath (body); g.fillPath (d);
    {   // a wash of paint in one corner
        juce::Graphics::ScopedSaveState s (g); g.reduceClipRegion (body);
        const float cx = rng.nextBool() ? r.getRight() : r.getX(), cy = rng.nextBool() ? r.getY() : r.getBottom();
        juce::ColourGradient w (tint.withAlpha (0.16f), cx, cy, tint.withAlpha (0.0f), cx + juce::jmin (r.getWidth(), 220.0f) * (cx > r.getX() ? -1.0f : 1.0f), cy, true);
        g.setGradientFill (w); g.fillRect (r);
    }
    sketch (g, wobble (body, seed, 1.2f), seed, 4, 0.7f, 0.42f);       // coloured pencil passes
    inkLine (g, body, seed + 1, P.ink.withAlpha (0.75f), 1.15f, 1.0f);  // and the ink line over them
    if (! d.isEmpty()) inkLine (g, d, seed + 3, P.ink.withAlpha (0.6f), 0.9f, 0.5f);
    if (! glow.isTransparent()) { const auto w = wobble (body, seed + 9, 1.4f); g.setColour (glow.withAlpha (0.85f)); g.strokePath (w, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); }
}

// the window's painting: washes, splatter, drips, bokeh and thousands of thin sketch lines, kept light in the middle
inline juce::Image renderBackdrop (int w, int h)
{
    const auto& P = palette();
    const bool dk = isDark();
    juce::Image img (juce::Image::RGB, juce::jmax (1, w), juce::jmax (1, h), true);
    juce::Graphics g (img);
    juce::ColourGradient base (P.windowTop, 0, 0, P.windowBottom, 0, (float) h, false);
    g.setGradientFill (base); g.fillAll();
    juce::Random rng (4922);
    struct Wash { float x, y, r; juce::Colour c; };
    std::vector<Wash> washes;
    // washes cluster at the edges (top right like the painting, the bottom, the left), the middle stays light for reading
    for (int i = 0; i < 18; ++i)
    {
        const int side = i % 4;
        float x = side == 0 ? (float) w * (0.55f + 0.45f * rng.nextFloat()) : side == 1 ? (float) w * rng.nextFloat() : side == 2 ? (float) w * 0.12f * rng.nextFloat() : (float) w * (0.8f + 0.2f * rng.nextFloat());
        float y = side == 0 ? (float) h * 0.25f * rng.nextFloat() : side == 1 ? (float) h * (0.82f + 0.18f * rng.nextFloat()) : (float) h * rng.nextFloat();
        washes.push_back ({ x, y, 120.0f + rng.nextFloat() * 260.0f, art().paints[rng.nextInt (dk ? 7 : kPaints)] });   // (no lime washes at night)
    }
    for (auto& s : washes)
    {
        juce::ColourGradient gr (s.c.withAlpha (dk ? 0.42f : 0.30f), s.x, s.y, s.c.withAlpha (0.0f), s.x + s.r, s.y, true);
        g.setGradientFill (gr); g.fillEllipse (s.x - s.r, s.y - s.r, 2 * s.r, 2 * s.r);
    }
    // the sketch: thin flowing strokes layered over and over
    for (int i = 0; i < 2200; ++i)
    {
        const auto& s = washes[(size_t) rng.nextInt ((int) washes.size())];
        float x = s.x + (rng.nextFloat() - 0.5f) * s.r * 1.6f, y = s.y + (rng.nextFloat() - 0.5f) * s.r * 1.6f;
        juce::Path p; p.startNewSubPath (x, y);
        const int segs = 6 + rng.nextInt (10);
        for (int k = 0; k < segs; ++k)
        {
            const float a = std::sin (x * 0.006f + (float) i * 0.01f) * 2.2f + std::cos (y * 0.008f) * 1.4f + 0.9f;
            x += std::cos (a) * 14.0f; y += std::sin (a) * 14.0f; p.lineTo (x, y);
        }
        g.setColour (art().inks[rng.nextInt (kPaints)].withAlpha (0.05f + 0.09f * rng.nextFloat()));
        g.strokePath (p, juce::PathStrokeType (0.4f + 0.6f * rng.nextFloat(), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    // splatter + drips
    for (int i = 0; i < 520; ++i)
    {
        const auto& s = washes[(size_t) rng.nextInt ((int) washes.size())];
        const float ang = rng.nextFloat() * juce::MathConstants<float>::twoPi, dist = s.r * std::sqrt (rng.nextFloat());
        const float x = s.x + std::cos (ang) * dist, y = s.y + std::sin (ang) * dist, r = 0.5f + 3.2f * rng.nextFloat() * rng.nextFloat();
        g.setColour (art().paints[rng.nextInt (kPaints)].withAlpha (0.25f + 0.5f * rng.nextFloat())); g.fillEllipse (x - r, y - r, 2 * r, 2 * r);
    }
    for (int i = 0; i < 26; ++i)
    {
        const auto& s = washes[(size_t) rng.nextInt ((int) washes.size())];
        const float x = s.x + (rng.nextFloat() - 0.5f) * s.r, y = s.y, len = 30.0f + rng.nextFloat() * 150.0f, wd = 0.8f + 1.8f * rng.nextFloat();
        const auto c = art().inks[rng.nextInt (kPaints)].withAlpha (0.35f + 0.3f * rng.nextFloat());
        juce::Path p; p.startNewSubPath (x, y); p.cubicTo (x + 3, y + len * 0.3f, x - 3, y + len * 0.7f, x + 1, y + len);
        g.setColour (c); g.strokePath (p, juce::PathStrokeType (wd, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.fillEllipse (x + 1 - wd * 1.6f, y + len - wd * 1.2f, wd * 3.2f, wd * 3.6f);
    }
    for (int i = 0; i < 16; ++i)   // bokeh
    {
        const float x = rng.nextFloat() * (float) w, y = rng.nextFloat() * (float) h, r = 14.0f + rng.nextFloat() * 34.0f;
        g.setColour ((rng.nextBool() ? (dk ? art().paints[6] : juce::Colours::white) : art().paints[4]).withAlpha ((dk ? 0.06f : 0.10f) + 0.08f * rng.nextFloat())); g.fillEllipse (x - r, y - r, 2 * r, 2 * r);
    }
    {   // a milky mist over the middle, where the pages are
        juce::ColourGradient mist (art().mist, (float) w * 0.45f, (float) h * 0.5f, art().mist.withAlpha (0.0f), (float) w * 0.95f, (float) h * 0.5f, true);
        g.setGradientFill (mist); g.fillAll();
    }
    return img;
}

inline void backdrop (juce::Graphics& g, juce::Rectangle<int> area, juce::Point<int> offset = {})
{   // drawn once per size; `offset` = where `area` sits in the window, so a page shows its part of the same painting
    static std::map<std::pair<int, int>, juce::Image> cache;
    const auto key = std::make_pair ((area.getWidth() + offset.x) * (isDark() ? -1 : 1), area.getHeight() + offset.y);
    auto it = cache.find (key);
    if (it == cache.end()) { if (cache.size() > 6) cache.clear(); it = cache.emplace (key, renderBackdrop (std::abs (key.first), key.second)).first; }
    g.drawImage (it->second, area.getX(), area.getY(), area.getWidth(), area.getHeight(), offset.x, offset.y, area.getWidth(), area.getHeight());
}

// the logo: a word written in thousands of little hairs, every one a different colour of the palette
inline juce::Image renderHairWord (const juce::String& word, int w, int h, float scale)
{
    juce::Image img (juce::Image::ARGB, juce::jmax (1, (int) (w * scale)), juce::jmax (1, (int) (h * scale)), true);
    juce::Graphics g (img);
    g.addTransform (juce::AffineTransform::scale (scale));
    juce::GlyphArrangement ga;
    auto f = ghosty::font ((float) h * 0.86f, true);
    ga.addFittedText (f, word, 2.0f, 0.0f, (float) w - 4.0f, (float) h, juce::Justification::centredLeft, 1, 1.0f);
    juce::Path glyphs; ga.createPath (glyphs);
    const auto b = glyphs.getBounds();
    juce::Random rng (8);
    const int want = juce::jlimit (2500, 12000, (int) (b.getWidth() * b.getHeight() * 0.9f));
    int made = 0;
    for (int tries = 0; made < want && tries < want * 12; ++tries)
    {
        const float x = b.getX() + rng.nextFloat() * b.getWidth(), y = b.getY() + rng.nextFloat() * b.getHeight();
        const bool in = glyphs.contains (x, y);
        if (! in && rng.nextFloat() > 0.025f) continue;            // a few stray hairs around the letters
        // the hairs comb along a gentle flow, with a little of their own mind
        const float ang = 1.25f + std::sin (x * 0.07f) * 0.5f + std::cos (y * 0.11f) * 0.35f + (rng.nextFloat() - 0.5f) * 0.9f;
        const float len = (in ? 3.0f : 2.5f) + rng.nextFloat() * (in ? 7.0f : 5.0f);
        // the colour drifts along the word (vermilion -> amber -> magenta -> orchid -> cyan) and every hair picks its own neighbour
        const float t = (x - b.getX()) / juce::jmax (1.0f, b.getWidth());
        const int base = juce::jlimit (0, kPaints - 1, (int) (t * 5.0f + (rng.nextFloat() - 0.5f) * 2.4f));
        static const int order[] { 0, 1, 3, 5, 6 };
        auto c = art().inks[order[juce::jlimit (0, 4, base)]];
        if (rng.nextFloat() < 0.35f) c = art().inks[rng.nextInt (kPaints)];
        c = c.withMultipliedBrightness (0.85f + 0.3f * rng.nextFloat());
        g.setColour (c.withAlpha (in ? 0.62f + 0.38f * rng.nextFloat() : 0.35f));
        g.drawLine (x - std::cos (ang) * len * 0.5f, y - std::sin (ang) * len * 0.5f, x + std::cos (ang) * len * 0.5f, y + std::sin (ang) * len * 0.5f,
                    0.45f + 0.55f * rng.nextFloat());
        ++made;
    }
    return img;
}

inline void hairWord (juce::Graphics& g, const juce::String& word, juce::Rectangle<float> r)
{
    static std::map<juce::String, juce::Image> cache;
    const float scale = juce::jmax (1.0f, (float) g.getInternalContext().getPhysicalPixelScaleFactor());
    const auto key = word + (isDark() ? "|D|" : "|L|") + juce::String ((int) r.getWidth()) + "x" + juce::String ((int) r.getHeight()) + "@" + juce::String (scale, 1);
    auto it = cache.find (key);
    if (it == cache.end()) { if (cache.size() > 8) cache.clear(); it = cache.emplace (key, renderHairWord (word, (int) r.getWidth(), (int) r.getHeight(), scale)).first; }
    g.drawImage (it->second, r);
}
}
