#pragma once
// Ghost Slice icons: every icon of the window drawn with juce::Path (own code, no images, no icon fonts). Each one fits the
// rectangle it is given (square-ish) and takes the colour / gradient already set on the Graphics, or the colours passed in.
#include <juce_gui_basics/juce_gui_basics.h>

namespace gslice::ui::icons
{
inline juce::PathStrokeType stroke (float w) { return juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded); }
inline juce::Rectangle<float> square (juce::Rectangle<float> r) { const float d = juce::jmin (r.getWidth(), r.getHeight()); return r.withSizeKeepingCentre (d, d); }

// a 4-point sparkle star (concave sides)
inline juce::Path star4 (juce::Point<float> c, float R, float waist = 0.18f)
{
    juce::Path p; const float w = R * waist;
    p.startNewSubPath (c.x, c.y - R);
    p.quadraticTo (c.x + w, c.y - w, c.x + R, c.y); p.quadraticTo (c.x + w, c.y + w, c.x, c.y + R);
    p.quadraticTo (c.x - w, c.y + w, c.x - R, c.y); p.quadraticTo (c.x - w, c.y - w, c.x, c.y - R);
    p.closeSubPath(); return p;
}

// AUTO: one big sparkle and two small ones
inline void sparkle (juce::Graphics& g, juce::Rectangle<float> r)
{
    r = square (r); const float d = r.getWidth();
    juce::Path p = star4 ({ r.getX() + d * 0.44f, r.getY() + d * 0.56f }, d * 0.40f);
    p.addPath (star4 ({ r.getX() + d * 0.82f, r.getY() + d * 0.18f }, d * 0.15f));
    p.addPath (star4 ({ r.getX() + d * 0.86f, r.getY() + d * 0.86f }, d * 0.09f));
    g.fillPath (p);
}

// INSTRUMENT / DRAG WAV: symmetric waveform bars
inline void waveBars (juce::Graphics& g, juce::Rectangle<float> r, int n = 11, float barW = 0.36f)
{
    static const float h[] { 0.22f, 0.42f, 0.70f, 0.48f, 0.92f, 1.0f, 0.86f, 0.52f, 0.74f, 0.40f, 0.20f, 0.5f, 0.3f };
    const float step = r.getWidth() / (float) n, w = juce::jmax (1.2f, step * barW);
    juce::Path p;
    for (int i = 0; i < n; ++i)
    {
        const float bh = r.getHeight() * h[i % 13];
        p.addRoundedRectangle (r.getX() + step * ((float) i + 0.5f) - w * 0.5f, r.getCentreY() - bh * 0.5f, w, bh, w * 0.5f);
    }
    g.fillPath (p);
}

// PAD: a cloud outline
inline void cloud (juce::Graphics& g, juce::Rectangle<float> r, float lw)
{
    r = square (r); const float d = r.getWidth(); const float x = r.getX(), y = r.getY() + d * 0.12f;
    using M = juce::MathConstants<float>;
    juce::Path p;
    p.startNewSubPath (x + d * 0.24f, y + d * 0.66f);
    p.lineTo (x + d * 0.76f, y + d * 0.66f);
    p.addCentredArc (x + d * 0.76f, y + d * 0.50f, d * 0.16f, d * 0.16f, 0.0f, M::pi, 0.25f, false);
    p.addCentredArc (x + d * 0.50f, y + d * 0.42f, d * 0.23f, d * 0.23f, 0.0f, 1.15f, -1.25f, false);
    p.addCentredArc (x + d * 0.25f, y + d * 0.50f, d * 0.16f, d * 0.16f, 0.0f, -0.35f, -M::pi, false);
    p.closeSubPath();
    g.strokePath (p, stroke (lw));
}

// CHOPS: thin bars of different heights with uneven gaps
inline void chopBars (juce::Graphics& g, juce::Rectangle<float> r, float lw)
{
    r = square (r); const float d = r.getWidth();
    const float xs[] { 0.08f, 0.30f, 0.44f, 0.66f, 0.92f }, hs[] { 0.55f, 0.95f, 0.62f, 0.85f, 0.5f };
    juce::Path p;
    for (int i = 0; i < 5; ++i) { const float hh = d * hs[i]; p.startNewSubPath (r.getX() + d * xs[i], r.getCentreY() - hh * 0.5f); p.lineTo (r.getX() + d * xs[i], r.getCentreY() + hh * 0.5f); }
    g.strokePath (p, stroke (lw));
}

// TEXTURE: three wavy lines
inline void waves (juce::Graphics& g, juce::Rectangle<float> r, float lw)
{
    r = square (r).reduced (0.0f, r.getHeight() * 0.12f); const float d = r.getWidth();
    juce::Path p;
    for (int k = 0; k < 3; ++k)
    {
        const float yy = r.getY() + r.getHeight() * (0.2f + 0.3f * (float) k);
        p.startNewSubPath (r.getX(), yy);
        for (int i = 1; i <= 24; ++i) { const float t = (float) i / 24.0f; p.lineTo (r.getX() + d * t, yy - std::sin (t * juce::MathConstants<float>::twoPi) * d * 0.07f); }
    }
    g.strokePath (p, stroke (lw));
}

// the CHARACTER ghosts: 0 NATURAL (white, cool shades), 1 CHIPMUNK (ears, cheeks), 2 DEEP (dark blue, glowing eyes)
inline juce::Path ghostBody (juce::Rectangle<float> r)
{
    const float x = r.getX(), y = r.getY(), w = r.getWidth(), h = r.getHeight();
    juce::Path p;
    p.startNewSubPath (x + w * 0.10f, y + h * 0.92f);
    p.lineTo (x + w * 0.10f, y + h * 0.45f);
    p.cubicTo (x + w * 0.10f, y - h * 0.02f, x + w * 0.90f, y - h * 0.02f, x + w * 0.90f, y + h * 0.45f);
    p.lineTo (x + w * 0.90f, y + h * 0.92f);
    // the wavy hem: four scallops
    const float y1 = y + h * 0.92f, y2 = y + h * 0.80f;
    p.quadraticTo (x + w * 0.80f, y2, x + w * 0.70f, y1);
    p.quadraticTo (x + w * 0.60f, y + h * 1.04f, x + w * 0.50f, y1);
    p.quadraticTo (x + w * 0.40f, y2, x + w * 0.30f, y1);
    p.quadraticTo (x + w * 0.20f, y + h * 1.04f, x + w * 0.10f, y1);
    p.closeSubPath();
    return p;
}

inline void ghost (juce::Graphics& g, juce::Rectangle<float> r, int kind, juce::Colour glowA, juce::Colour glowB)
{
    r = square (r);
    const float d = r.getWidth();
    auto br = r.reduced (d * 0.14f, d * 0.06f).translated (0.0f, d * 0.04f);
    if (kind == 1) br = br.withTrimmedTop (d * 0.06f);
    const auto body = ghostBody (br);
    // a soft glow under it
    for (int i = 3; i >= 1; --i)
    {
        g.setColour ((kind == 2 ? glowB : glowA).withAlpha (0.10f));
        g.strokePath (body, juce::PathStrokeType (d * 0.035f * (float) i));
    }
    if (kind == 1)
    {   // pointed ears
        juce::Path ears;
        const float ex = br.getX(), ey = br.getY(), ew = br.getWidth();
        ears.addTriangle (ex + ew * 0.10f, ey + ew * 0.30f, ex + ew * 0.06f, ey - ew * 0.16f, ex + ew * 0.42f, ey + ew * 0.06f);
        ears.addTriangle (ex + ew * 0.90f, ey + ew * 0.30f, ex + ew * 0.94f, ey - ew * 0.16f, ex + ew * 0.58f, ey + ew * 0.06f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe9e3f7), ex, ey - ew * 0.2f, juce::Colour (0xffa59cc4), ex, ey + ew * 0.4f, false));
        g.fillPath (ears);
        juce::Path inner;
        inner.addTriangle (ex + ew * 0.15f, ey + ew * 0.20f, ex + ew * 0.12f, ey - ew * 0.06f, ex + ew * 0.34f, ey + ew * 0.08f);
        inner.addTriangle (ex + ew * 0.85f, ey + ew * 0.20f, ex + ew * 0.88f, ey - ew * 0.06f, ex + ew * 0.66f, ey + ew * 0.08f);
        g.setColour (juce::Colour (0xffd9a6e8)); g.fillPath (inner);
    }
    juce::Colour top, bot;
    if (kind == 0) { top = juce::Colour (0xffffffff); bot = juce::Colour (0xffc9c2ea); }
    else if (kind == 1) { top = juce::Colour (0xffe6e0f4); bot = juce::Colour (0xff8f86b2); }
    else { top = juce::Colour (0xff3048b8); bot = juce::Colour (0xff0d1550); }
    g.setGradientFill (juce::ColourGradient (top, br.getX(), br.getY(), bot, br.getX(), br.getBottom(), false));
    g.fillPath (body);
    if (kind == 2) { g.setColour (glowB.withAlpha (0.9f)); g.strokePath (body, juce::PathStrokeType (juce::jmax (1.0f, d * 0.025f))); }
    // a highlight on the head
    { juce::Path hl; hl.addEllipse (br.getX() + br.getWidth() * 0.22f, br.getY() + br.getHeight() * 0.10f, br.getWidth() * 0.22f, br.getHeight() * 0.12f);
      g.setColour (juce::Colours::white.withAlpha (kind == 2 ? 0.18f : 0.55f)); g.fillPath (hl); }
    const float ex = br.getCentreX(), ey = br.getY() + br.getHeight() * 0.42f, bw = br.getWidth();
    if (kind == 0)
    {   // cool shades
        juce::Path s; s.addRoundedRectangle (ex - bw * 0.36f, ey - bw * 0.08f, bw * 0.32f, bw * 0.17f, bw * 0.07f);
        s.addRoundedRectangle (ex + bw * 0.04f, ey - bw * 0.08f, bw * 0.32f, bw * 0.17f, bw * 0.07f);
        s.addRectangle (ex - bw * 0.05f, ey - bw * 0.05f, bw * 0.10f, bw * 0.035f);
        g.setColour (juce::Colour (0xff1b1640)); g.fillPath (s);
        g.setColour (glowB.withAlpha (0.8f)); g.fillRect (ex + bw * 0.10f, ey - bw * 0.05f, bw * 0.08f, bw * 0.03f);
        juce::Path m; m.addCentredArc (ex, ey + bw * 0.20f, bw * 0.07f, bw * 0.05f, 0.0f, juce::MathConstants<float>::pi * 0.6f, juce::MathConstants<float>::pi * 1.4f, true);
        g.setColour (juce::Colour (0xff1b1640)); g.strokePath (m, stroke (juce::jmax (0.8f, bw * 0.035f)));
    }
    else if (kind == 1)
    {
        g.setColour (juce::Colour (0xff1b1640));
        g.fillEllipse (ex - bw * 0.24f, ey - bw * 0.06f, bw * 0.13f, bw * 0.15f); g.fillEllipse (ex + bw * 0.11f, ey - bw * 0.06f, bw * 0.13f, bw * 0.15f);
        g.setColour (juce::Colours::white); g.fillEllipse (ex - bw * 0.20f, ey - bw * 0.04f, bw * 0.04f, bw * 0.04f); g.fillEllipse (ex + bw * 0.15f, ey - bw * 0.04f, bw * 0.04f, bw * 0.04f);
        g.setColour (juce::Colour (0x88ff8fb8)); g.fillEllipse (ex - bw * 0.36f, ey + bw * 0.10f, bw * 0.14f, bw * 0.08f); g.fillEllipse (ex + bw * 0.22f, ey + bw * 0.10f, bw * 0.14f, bw * 0.08f);
        juce::Path t; t.addRectangle (ex - bw * 0.05f, ey + bw * 0.13f, bw * 0.045f, bw * 0.07f); t.addRectangle (ex + bw * 0.005f, ey + bw * 0.13f, bw * 0.045f, bw * 0.07f);
        g.setColour (juce::Colours::white); g.fillPath (t);
    }
    else
    {   // deep: glowing slanted eyes
        juce::Path e;
        e.addEllipse (ex - bw * 0.24f, ey - bw * 0.03f, bw * 0.15f, bw * 0.10f); e.addEllipse (ex + bw * 0.09f, ey - bw * 0.03f, bw * 0.15f, bw * 0.10f);
        g.setColour (glowB.withAlpha (0.35f)); g.strokePath (e, juce::PathStrokeType (bw * 0.08f));
        g.setColour (juce::Colour (0xffd8f6ff)); g.fillPath (e);
    }
}

// a die (VARY): an isometric cube with pips
inline void dice (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
{
    r = square (r); const float d = r.getWidth(), cx = r.getCentreX(), y = r.getY();
    const juce::Point<float> T (cx, y + d * 0.04f), L (cx - d * 0.44f, y + d * 0.27f), R (cx + d * 0.44f, y + d * 0.27f), M (cx, y + d * 0.50f),
                             BL (cx - d * 0.44f, y + d * 0.74f), BR (cx + d * 0.44f, y + d * 0.74f), B (cx, y + d * 0.97f);
    juce::Path top; top.startNewSubPath (T); top.lineTo (R); top.lineTo (M); top.lineTo (L); top.closeSubPath();
    juce::Path left; left.startNewSubPath (L); left.lineTo (M); left.lineTo (B); left.lineTo (BL); left.closeSubPath();
    juce::Path right; right.startNewSubPath (M); right.lineTo (R); right.lineTo (BR); right.lineTo (B); right.closeSubPath();
    g.setColour (c.brighter (0.25f)); g.fillPath (top.createPathWithRoundedCorners (d * 0.05f));
    g.setColour (c); g.fillPath (left.createPathWithRoundedCorners (d * 0.05f));
    g.setColour (c.darker (0.35f)); g.fillPath (right.createPathWithRoundedCorners (d * 0.05f));
    const float pr = d * 0.055f;
    g.setColour (juce::Colours::white.withAlpha (0.95f));
    auto pip = [&] (juce::Point<float> p) { g.fillEllipse (p.x - pr, p.y - pr * 0.7f, pr * 2.0f, pr * 1.4f); };
    pip ({ cx, y + d * 0.27f });
    pip (M + (L - M) * 0.30f + (B - M) * 0.30f); pip (M + (L - M) * 0.70f + (B - M) * 0.70f);
    pip (M + (R - M) * 0.25f + (B - M) * 0.25f); pip (M + (R - M) * 0.5f + (B - M) * 0.5f); pip (M + (R - M) * 0.75f + (B - M) * 0.75f);
}

inline void folder (juce::Graphics& g, juce::Rectangle<float> r)
{
    r = square (r).reduced (0.0f, r.getHeight() * 0.12f); const float d = r.getWidth();
    juce::Path p;
    p.addRoundedRectangle (r.getX(), r.getY() + d * 0.16f, d, r.getHeight() - d * 0.16f, d * 0.10f);
    p.addRoundedRectangle (r.getX(), r.getY(), d * 0.42f, d * 0.26f, d * 0.08f);
    g.fillPath (p);
}

inline void gear (juce::Graphics& g, juce::Rectangle<float> r, float lw)
{
    r = square (r); const auto c = r.getCentre(); const float R = r.getWidth() * 0.5f;
    juce::Path p;
    const int teeth = 8;
    for (int i = 0; i < teeth * 4; ++i)
    {
        const float a = (float) i / (float) (teeth * 4) * juce::MathConstants<float>::twoPi;
        const float rr = (i % 4 == 1 || i % 4 == 2) ? R : R * 0.74f;
        const auto pt = c.getPointOnCircumference (rr, a);
        if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
    }
    p.closeSubPath();
    g.strokePath (p.createPathWithRoundedCorners (R * 0.08f), stroke (lw));
    g.drawEllipse (juce::Rectangle<float> (R * 0.62f, R * 0.62f).withCentre (c), lw);
}

inline void magnifier (juce::Graphics& g, juce::Rectangle<float> r, float lw, int sign)   // +1 zoom in, -1 zoom out
{
    r = square (r); const float d = r.getWidth();
    const auto c = juce::Point<float> (r.getX() + d * 0.42f, r.getY() + d * 0.42f); const float R = d * 0.32f;
    g.drawEllipse (juce::Rectangle<float> (R * 2, R * 2).withCentre (c), lw);
    juce::Path h; h.startNewSubPath (c.x + R * 0.72f, c.y + R * 0.72f); h.lineTo (r.getX() + d * 0.94f, r.getY() + d * 0.94f);
    h.startNewSubPath (c.x - R * 0.5f, c.y); h.lineTo (c.x + R * 0.5f, c.y);
    if (sign > 0) { h.startNewSubPath (c.x, c.y - R * 0.5f); h.lineTo (c.x, c.y + R * 0.5f); }
    g.strokePath (h, stroke (lw));
}

// FIT: a marker line with arrows pointing out (show the whole sound)
inline void fit (juce::Graphics& g, juce::Rectangle<float> r, float lw)
{
    r = square (r); const float d = r.getWidth(); const auto c = r.getCentre();
    juce::Path p;
    p.startNewSubPath (c.x, r.getY()); p.lineTo (c.x, r.getBottom());
    p.startNewSubPath (r.getX(), c.y); p.lineTo (c.x - d * 0.14f, c.y); p.startNewSubPath (c.x + d * 0.14f, c.y); p.lineTo (r.getRight(), c.y);
    p.startNewSubPath (r.getX() + d * 0.16f, c.y - d * 0.13f); p.lineTo (r.getX(), c.y); p.lineTo (r.getX() + d * 0.16f, c.y + d * 0.13f);
    p.startNewSubPath (r.getRight() - d * 0.16f, c.y - d * 0.13f); p.lineTo (r.getRight(), c.y); p.lineTo (r.getRight() - d * 0.16f, c.y + d * 0.13f);
    g.strokePath (p, stroke (lw));
}

inline void play (juce::Graphics& g, juce::Rectangle<float> r)
{
    r = square (r); const float d = r.getWidth();
    juce::Path p; p.addTriangle (r.getX() + d * 0.18f, r.getY(), r.getX() + d * 0.18f, r.getBottom(), r.getRight(), r.getCentreY());
    g.fillPath (p.createPathWithRoundedCorners (d * 0.10f));
}
inline void stopSquare (juce::Graphics& g, juce::Rectangle<float> r) { r = square (r).reduced (r.getWidth() * 0.06f); g.fillRoundedRectangle (r, r.getWidth() * 0.16f); }

inline void scissors (juce::Graphics& g, juce::Rectangle<float> r, float lw)
{
    r = square (r); const float d = r.getWidth();
    const float rr = d * 0.17f;
    const juce::Point<float> a (r.getX() + d * 0.26f, r.getBottom() - rr), b (r.getRight() - d * 0.26f, r.getBottom() - rr);
    g.drawEllipse (juce::Rectangle<float> (rr * 2, rr * 2).withCentre (a), lw); g.drawEllipse (juce::Rectangle<float> (rr * 2, rr * 2).withCentre (b), lw);
    juce::Path p;
    p.startNewSubPath (a.x + rr * 0.6f, a.y - rr * 0.8f); p.lineTo (r.getRight() - d * 0.22f, r.getY());
    p.startNewSubPath (b.x - rr * 0.6f, b.y - rr * 0.8f); p.lineTo (r.getX() + d * 0.22f, r.getY());
    g.strokePath (p, stroke (lw));
}

inline void download (juce::Graphics& g, juce::Rectangle<float> r, float lw)
{
    r = square (r); const float d = r.getWidth(); const float cx = r.getCentreX();
    juce::Path p;
    p.startNewSubPath (cx, r.getY()); p.lineTo (cx, r.getY() + d * 0.62f);
    p.startNewSubPath (cx - d * 0.24f, r.getY() + d * 0.40f); p.lineTo (cx, r.getY() + d * 0.64f); p.lineTo (cx + d * 0.24f, r.getY() + d * 0.40f);
    p.startNewSubPath (r.getX() + d * 0.04f, r.getY() + d * 0.66f); p.lineTo (r.getX() + d * 0.04f, r.getBottom()); p.lineTo (r.getRight() - d * 0.04f, r.getBottom()); p.lineTo (r.getRight() - d * 0.04f, r.getY() + d * 0.66f);
    g.strokePath (p, stroke (lw));
}

// DRAG MIDI: two beamed notes
inline void notes (juce::Graphics& g, juce::Rectangle<float> r, float lw)
{
    r = square (r); const float d = r.getWidth();
    const float hw = d * 0.30f, hh = d * 0.22f;
    juce::Path heads;
    heads.addEllipse (r.getX() + d * 0.02f, r.getBottom() - hh - d * 0.02f, hw, hh);
    heads.addEllipse (r.getX() + d * 0.60f, r.getBottom() - hh - d * 0.14f, hw, hh);
    heads.applyTransform (juce::AffineTransform::rotation (-0.35f, r.getCentreX(), r.getCentreY()));
    g.strokePath (heads, stroke (lw));
    juce::Path s;
    const float x1 = r.getX() + d * 0.30f, x2 = r.getX() + d * 0.88f;
    s.startNewSubPath (x1, r.getBottom() - d * 0.22f); s.lineTo (x1, r.getY() + d * 0.16f); s.lineTo (x2, r.getY()); s.lineTo (x2, r.getBottom() - d * 0.36f);
    s.startNewSubPath (x1, r.getY() + d * 0.34f); s.lineTo (x2, r.getY() + d * 0.18f);
    g.strokePath (s, stroke (lw));
}

inline void lock (juce::Graphics& g, juce::Rectangle<float> r, bool closed, float lw)
{
    r = square (r); const float d = r.getWidth();
    g.fillRoundedRectangle (r.getX() + d * 0.14f, r.getY() + d * 0.44f, d * 0.72f, d * 0.56f, d * 0.10f);
    juce::Path s;
    const float x0 = r.getX() + d * 0.30f, x1 = r.getX() + d * 0.70f;
    s.startNewSubPath (x0, r.getY() + d * 0.46f); s.lineTo (x0, r.getY() + d * 0.26f);
    s.addCentredArc (r.getCentreX(), r.getY() + d * 0.26f, d * 0.20f, d * 0.20f, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, false);
    s.lineTo (x1, r.getY() + (closed ? d * 0.46f : d * 0.32f));
    g.strokePath (s, stroke (lw));
}

inline void chevron (juce::Graphics& g, juce::Rectangle<float> r, int dir, float lw)   // 0 down, -1 left, +1 right
{
    const auto c = r.getCentre(); const float s = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f;
    juce::Path p;
    if (dir == 0) { p.startNewSubPath (c.x - s, c.y - s * 0.5f); p.lineTo (c.x, c.y + s * 0.5f); p.lineTo (c.x + s, c.y - s * 0.5f); }
    else { const float k = (float) dir; p.startNewSubPath (c.x - s * 0.5f * k, c.y - s); p.lineTo (c.x + s * 0.5f * k, c.y); p.lineTo (c.x - s * 0.5f * k, c.y + s); }
    g.strokePath (p, stroke (lw));
}

inline void cross (juce::Graphics& g, juce::Rectangle<float> r, float lw)
{
    juce::Path p; p.startNewSubPath (r.getTopLeft()); p.lineTo (r.getBottomRight()); p.startNewSubPath (r.getTopRight()); p.lineTo (r.getBottomLeft());
    g.strokePath (p, stroke (lw));
}
} // namespace gslice::ui::icons
