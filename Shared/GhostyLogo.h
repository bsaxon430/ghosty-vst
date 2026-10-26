#pragma once
// The Ghosty logo: the classic friendly Ghosty (white, purple dripping hem, big friendly eyes, a small smile) with one neon-green
// accent (a slime drip that grows and lets go of a drop), and the lowercase "ghosty" wordmark ("ghost" white, "y" green).
// Drawn procedurally (juce::Path + gradients + glow), no bitmaps and no font: the letters are our own rounded letterforms.
//
//   drawMascot (g, bounds, backdrop, animPhase)   the ghost, 20 px (header) up to 1000+ px; small sizes drop the fine detail.
//                                                 backdrop = the colour behind it (the rim deepens on light backdrops so the outline
//                                                 keeps >= 3:1). animPhase = seconds of any running clock (the idle loop is 4 s:
//                                                 the green drip grows and drops, the eyes blink, the eye glint flashes).
//   drawWordmark (g, bounds, backdrop)            "ghosty", fitted inside bounds (descenders included)
//   drawIcon (g, bounds)                          app icon: dark squircle + the ghost
//   drawHero (g, bounds, animPhase)               1600 x 900 style hero: the ghost, the wordmark, "free synth"
//   drawSocial (g, bounds)                        1200 x 630 social / og card
//   outlineColour (backdrop)                      the mascot's rim colour on that backdrop
//
// Message thread only, paint-time only; nothing is cached.
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <vector>

namespace ghosty::logo
{
namespace lc
{
    const juce::Colour bg     { 0xff0B0C10 };
    const juce::Colour white  { 0xffFFFFFF };
    const juce::Colour slime  { 0xff3CFF8F };
    const juce::Colour ecto   { 0xffA06BFF };
    const juce::Colour hem    { 0xff7C3AED };
    const juce::Colour ink    { 0xff1A1430 };
    const juce::Colour dim    { 0xff9A93B0 };
}

inline double relLum (juce::Colour c)
{
    auto ch = [] (float v) { return v <= 0.03928f ? v / 12.92 : std::pow ((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * ch (c.getFloatRed()) + 0.7152 * ch (c.getFloatGreen()) + 0.0722 * ch (c.getFloatBlue());
}
inline double contrast (juce::Colour a, juce::Colour b)
{
    const double x = relLum (a), y = relLum (b);
    return (juce::jmax (x, y) + 0.05) / (juce::jmin (x, y) + 0.05);
}
// brightens (dark backdrop) or deepens (light backdrop) c until it reads on bg
inline juce::Colour readableOn (juce::Colour c, juce::Colour bg, double minRatio)
{
    const bool darken = relLum (bg) > 0.18; auto o = c.withAlpha (1.0f);
    for (int k = 0; k < 40 && contrast (o, bg) < minRatio; ++k)
        o = darken ? juce::Colour::fromHSV (o.getHue(), juce::jmin (1.0f, o.getSaturation() * 1.03f + 0.02f), o.getBrightness() * 0.93f, 1.0f)
                   : juce::Colour::fromHSV (o.getHue(), o.getSaturation() * 0.93f, juce::jmin (1.0f, o.getBrightness() * 1.07f + 0.02f), 1.0f);
    return o;
}
inline bool isLight (juce::Colour backdrop) { return relLum (backdrop) > 0.25; }
inline juce::Colour outlineColour (juce::Colour backdrop) { return readableOn (lc::ecto, backdrop, 4.5); }

namespace detail
{
    inline float frac (float v) { return v - std::floor (v); }
    inline float smooth (float e0, float e1, float x) { const float u = juce::jlimit (0.0f, 1.0f, (x - e0) / (e1 - e0)); return u * u * (3.0f - 2.0f * u); }
    inline juce::PathStrokeType roundStroke (float w) { return juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded); }

    struct U
    {
        float cx, cy, R; juce::AffineTransform T;
        U (float x, float y, float r) : cx (x), cy (y), R (r), T (juce::AffineTransform::scale (r).translated (x, y)) {}
        juce::Point<float> p (float x, float y) const { T.transformPoint (x, y); return { x, y }; }
        juce::Path map (juce::Path path) const { path.applyTransform (T); return path; }
        juce::ColourGradient lin (juce::Colour a, float x1, float y1, juce::Colour b, float x2, float y2) const
        { auto A = p (x1, y1), B = p (x2, y2); return juce::ColourGradient (a, A.x, A.y, b, B.x, B.y, false); }
        juce::ColourGradient rad (juce::Colour a, float x, float y, juce::Colour b, float r) const
        { auto A = p (x, y); return juce::ColourGradient (a, A.x, A.y, b, A.x + r * R, A.y, true); }
        void ellipse (juce::Path& path, float x, float y, float w, float h) const
        { const auto c = p (x, y); path.addEllipse (c.x - w * R * 0.5f, c.y - h * R * 0.5f, w * R, h * R); }
    };

    inline void addDrip (juce::Path& d, float x, float y0, float len, float w)
    {
        const float hw = w * 0.5f, e = y0 + len;
        d.startNewSubPath (x - hw * 1.7f, y0 - hw * 0.9f);
        d.quadraticTo (x - hw, y0, x - hw * 0.8f, y0 + len * 0.35f);
        d.lineTo (x - hw * 0.72f, e - hw * 1.1f);
        d.cubicTo (x - hw * 1.9f, e - hw * 0.1f, x - hw * 1.25f, e + hw * 1.45f, x, e + hw * 1.3f);
        d.cubicTo (x + hw * 1.25f, e + hw * 1.45f, x + hw * 1.9f, e - hw * 0.1f, x + hw * 0.72f, e - hw * 1.1f);
        d.lineTo (x + hw * 0.8f, y0 + len * 0.35f);
        d.quadraticTo (x + hw, y0, x + hw * 1.7f, y0 - hw * 0.9f);
        d.closeSubPath();
    }
    inline void addDrop (juce::Path& d, float x, float y, float r)
    {
        d.startNewSubPath (x, y - r * 1.9f);
        d.cubicTo (x + r * 0.35f, y - r * 1.1f, x + r, y - r * 0.6f, x + r, y);
        d.cubicTo (x + r, y + r * 0.6f, x + r * 0.55f, y + r, x, y + r);
        d.cubicTo (x - r * 0.55f, y + r, x - r, y + r * 0.6f, x - r, y);
        d.cubicTo (x - r, y - r * 0.6f, x - r * 0.35f, y - r * 1.1f, x, y - r * 1.9f);
        d.closeSubPath();
    }

    // the classic silhouette: round head, soft skirt, four scallops (unit space: head centre (0, -0.2), radius ~0.74)
    inline juce::Path body (float t)
    {
        juce::Path p;
        p.startNewSubPath (-0.74f, -0.18f);
        p.cubicTo (-0.74f, -0.64f, -0.41f, -0.96f, 0.0f, -0.96f);
        p.cubicTo (0.41f, -0.96f, 0.74f, -0.64f, 0.74f, -0.18f);
        p.cubicTo (0.74f, 0.14f, 0.8f, 0.4f, 0.86f, 0.58f);
        const float top = 0.58f;
        for (int i = 0; i < 4; ++i)
        {
            const float x0 = 0.86f - (float) i * 0.43f, x1 = x0 - 0.43f, d = 0.25f + 0.02f * std::sin (t * 2.2f + (float) i * 1.5f);
            p.cubicTo (x0 - 0.03f, top + d, x1 + 0.03f, top + d, x1, top);
        }
        p.cubicTo (-0.8f, 0.4f, -0.74f, 0.14f, -0.74f, -0.18f);
        p.closeSubPath();
        return p;
    }

    inline void drawGhosty (juce::Graphics& g, const U& S, juce::Colour backdrop, float t)
    {
        const float R = S.R;
        const bool light = isLight (backdrop);
        const auto rim = outlineColour (backdrop);
        const bool tiny = R < 14.0f, big = R >= 40.0f;
        const float ow = juce::jlimit (0.9f, 4.5f, 0.03f * R);

        g.setGradientFill (S.rad (rim.withAlpha (light ? 0.16f : 0.32f), 0.0f, -0.1f, rim.withAlpha (0.0f), 1.4f));
        g.fillEllipse (juce::Rectangle<float> (S.p (-1.4f, -1.5f), S.p (1.4f, 1.3f)));

        const auto bodyPx = S.map (body (t));
        // the hem drips (purple) + the one green drip, behind the body so they melt out of the scallops
        const float cyc = frac (t / 4.0f), grow = smooth (0.0f, 0.7f, cyc) * (1.0f - smooth (0.72f, 0.8f, cyc));
        juce::Path purpleDrips, greenDrip;
        addDrip (purpleDrips, -0.645f, 0.74f, 0.16f, 0.075f);
        addDrip (purpleDrips, 0.215f, 0.76f, 0.1f, 0.07f);
        if (! tiny) addDrip (purpleDrips, 0.645f, 0.74f, 0.07f, 0.065f);
        addDrip (greenDrip, -0.215f, 0.76f, 0.16f + 0.14f * grow, 0.075f);
        const auto pdPx = S.map (purpleDrips), gdPx = S.map (greenDrip);
        g.setColour (rim);
        g.strokePath (bodyPx, juce::PathStrokeType (2.0f * ow, juce::PathStrokeType::curved));
        g.strokePath (pdPx, juce::PathStrokeType (2.0f * ow)); g.strokePath (gdPx, juce::PathStrokeType (2.0f * ow));
        g.setColour (juce::Colour (0xff6A2FE0)); g.fillPath (pdPx);
        g.setColour (lc::slime); g.fillPath (gdPx);
        {
            auto gb = S.lin (lc::white, 0.0f, -0.96f, juce::Colour (0xffE9E1F8), 0.0f, 0.5f);
            g.setGradientFill (gb); g.fillPath (bodyPx);
        }
        {
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (bodyPx);
            // the purple hem: a dripping band along the bottom
            juce::Path hemP; hemP.startNewSubPath (-1.0f, 0.42f);
            const float dx[] { -0.64f, -0.22f, 0.0f, 0.42f };
            float x = -1.0f;
            for (float d : dx)
            {
                hemP.lineTo (d - 0.09f, 0.42f + 0.02f * std::sin (d * 9.0f));
                hemP.quadraticTo (d - 0.06f, 0.42f, d - 0.05f, 0.52f);
                hemP.quadraticTo (d, 0.6f + (d == 0.0f ? 0.04f : 0.0f), d + 0.05f, 0.52f);
                hemP.quadraticTo (d + 0.06f, 0.42f, d + 0.09f, 0.42f);
                x = d;
            }
            juce::ignoreUnused (x);
            hemP.lineTo (1.0f, 0.4f); hemP.lineTo (1.0f, 1.2f); hemP.lineTo (-1.0f, 1.2f); hemP.closeSubPath();
            g.setGradientFill (S.lin (juce::Colour (0xffB794FF), 0.0f, 0.4f, juce::Colour (0xff5B21B6), 0.0f, 0.86f));
            g.fillPath (S.map (hemP));
            // the green drip starts in the hem as a green blob
            juce::Path gb; gb.addEllipse (-0.31f, 0.72f, 0.18f, 0.14f);
            g.setColour (lc::slime); g.fillPath (S.map (gb));
            g.setGradientFill (S.rad (juce::Colour (0xff6D4AA8).withAlpha (0.2f), 0.95f, 0.05f, juce::Colour (0xff6D4AA8).withAlpha (0.0f), 0.9f));
            g.fillPath (bodyPx);
            if (! tiny) { g.setGradientFill (S.rad (lc::white.withAlpha (0.95f), -0.32f, -0.62f, lc::white.withAlpha (0.0f), 0.36f)); g.fillPath (bodyPx); }
        }
        // the drop that let go
        if (cyc > 0.72f)
        {
            const float f = (cyc - 0.72f) / 0.28f;
            juce::Path d; addDrop (d, -0.22f, 1.04f + f * f * 0.5f, 0.05f);
            g.setColour (lc::slime.withAlpha (1.0f - smooth (0.7f, 1.0f, f))); g.fillPath (S.map (d));
        }

        // the face: big friendly eyes (blink every few seconds), highlights, cheeks, a small smile
        const float ex = 0.27f, ey = -0.22f, ew = 0.24f, eh = 0.32f;
        const bool blink = frac ((t + 1.1f) / 4.3f) < 0.035f;
        if (blink)
        {
            juce::Path b;
            for (float s : { -1.0f, 1.0f }) { b.startNewSubPath (S.p (s * ex - ew * 0.5f, ey + 0.02f)); b.quadraticTo (S.p (s * ex, ey + 0.08f), S.p (s * ex + ew * 0.5f, ey + 0.02f)); }
            g.setColour (lc::ink); g.strokePath (b, roundStroke (juce::jmax (1.2f, 0.05f * R)));
        }
        else
        {
            juce::Path e; for (float s : { -1.0f, 1.0f }) S.ellipse (e, s * ex, ey, ew, eh);
            g.setColour (lc::ink); g.fillPath (e);
            if (! tiny)
            {
                juce::Path ir; for (float s : { -1.0f, 1.0f }) S.ellipse (ir, s * ex, ey + 0.07f, ew * 0.66f, eh * 0.42f);
                g.setColour (juce::Colour (0xff7C5BFF)); g.fillPath (ir);
            }
            const float fl = std::exp (-std::pow ((frac (t / 4.0f + 0.4f) - 0.5f) * 14.0f, 2.0f));
            juce::Path hl;
            for (float s : { -1.0f, 1.0f })
            {
                S.ellipse (hl, s * ex + 0.05f, ey - 0.07f, 0.09f + 0.02f * fl, 0.09f + 0.02f * fl);
                if (R >= 24.0f) S.ellipse (hl, s * ex - 0.05f, ey + 0.08f, 0.04f, 0.04f);
            }
            g.setColour (lc::white); g.fillPath (hl);
        }
        if (! tiny)
        {
            juce::Path c; for (float s : { -1.0f, 1.0f }) S.ellipse (c, s * 0.45f, 0.0f, 0.17f, 0.095f);
            g.setColour (juce::Colour (0xffFF8AD0).withAlpha (0.5f)); g.fillPath (c);
        }
        juce::Path m; m.startNewSubPath (S.p (-0.1f, 0.02f)); m.quadraticTo (S.p (0.0f, 0.13f), S.p (0.1f, 0.02f));
        g.setColour (lc::ink); g.strokePath (m, roundStroke (juce::jmax (1.1f, 0.045f * R)));
        if (big)
        {
            juce::Path gl; S.ellipse (gl, -0.36f, -0.7f, 0.2f, 0.07f);
            g.setColour (lc::white.withAlpha (0.8f)); g.fillPath (gl);
        }
    }
} // namespace detail

inline void drawMascot (juce::Graphics& g, juce::Rectangle<float> b, juce::Colour backdrop, float animPhase)
{
    const float R = juce::jmin (b.getWidth() / 2.0f, b.getHeight() / 2.25f);
    detail::drawGhosty (g, detail::U (b.getCentreX(), b.getCentreY() + 0.08f * R, R), backdrop, animPhase);
}

// ---------------------------------------------------------------------------------------------------- the wordmark
namespace detail
{
    // our own rounded lowercase letters: centrelines in x-height units (x-height 1, baseline y = 1, ascender to -0.5, descender to 1.45)
    inline std::vector<juce::Path> letter (char c, float& adv)
    {
        std::vector<juce::Path> v (2);
        auto& p = v[0]; auto& q = v[1];
        constexpr float r = 0.37f;
        switch (c)
        {
            case 'g': adv = 0.98f; p.addCentredArc (0.43f, 0.5f, r, r, 0.0f, 0.0f, juce::MathConstants<float>::twoPi, true); p.closeSubPath();
                      q.startNewSubPath (0.8f, 0.13f); q.lineTo (0.8f, 1.12f); q.cubicTo (0.8f, 1.5f, 0.24f, 1.56f, 0.1f, 1.26f); break;
            case 'h': adv = 0.94f; p.startNewSubPath (0.13f, -0.5f); p.lineTo (0.13f, 1.0f);
                      q.startNewSubPath (0.13f, 0.52f); q.cubicTo (0.13f, 0.02f, 0.79f, 0.02f, 0.79f, 0.52f); q.lineTo (0.79f, 1.0f); break;
            case 'o': adv = 0.92f; p.addCentredArc (0.46f, 0.5f, r, r, 0.0f, 0.0f, juce::MathConstants<float>::twoPi, true); p.closeSubPath(); break;
            case 's': adv = 0.84f; p.startNewSubPath (0.7f, 0.24f); p.cubicTo (0.58f, 0.06f, 0.13f, 0.06f, 0.14f, 0.3f);
                      p.cubicTo (0.15f, 0.52f, 0.72f, 0.44f, 0.72f, 0.72f); p.cubicTo (0.72f, 0.98f, 0.24f, 1.0f, 0.1f, 0.8f); break;
            case 't': adv = 0.7f; p.startNewSubPath (0.3f, -0.32f); p.lineTo (0.3f, 0.76f); p.cubicTo (0.3f, 0.96f, 0.46f, 1.0f, 0.62f, 0.94f);
                      q.startNewSubPath (0.04f, 0.13f); q.lineTo (0.6f, 0.13f); break;
            default:  adv = 0.92f; p.startNewSubPath (0.1f, 0.13f); p.lineTo (0.45f, 0.94f);
                      q.startNewSubPath (0.8f, 0.13f); q.lineTo (0.42f, 1.14f); q.cubicTo (0.34f, 1.36f, 0.22f, 1.44f, 0.06f, 1.4f); break;
        }
        if (q.isEmpty()) v.pop_back();
        return v;
    }
}

inline void drawWordmark (juce::Graphics& g, juce::Rectangle<float> b, juce::Colour backdrop)
{
    using namespace detail;
    constexpr float sw = 0.27f, track = 0.06f;
    // layout in x-height units; every stroke is its own piece (filled one by one: no winding holes where strokes cross)
    std::vector<juce::Path> ghostP, yP; juce::Path drip;
    float x = 0.0f;
    for (char c : { 'g', 'h', 'o', 's', 't', 'y' })
    {
        float adv = 0;
        for (auto& l : letter (c, adv))
        {
            juce::Path outl; juce::PathStrokeType (sw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath (outl, l);
            outl.applyTransform (juce::AffineTransform::translation (x, 0.0f));
            (c == 'y' ? yP : ghostP).push_back (outl);
        }
        if (c == 'o') addDrip (drip, x + 0.46f + 0.05f, 0.92f, 0.34f, 0.13f);
        x += adv + track;
    }
    const float width = x - track, top = -0.5f - sw * 0.5f, bottom = 1.45f + sw * 0.5f;
    const float u = juce::jmin (b.getWidth() / (width + 0.2f), b.getHeight() / (bottom - top + 0.15f));
    const auto T = juce::AffineTransform::translation (-width * 0.5f, -(top + bottom) * 0.5f).scaled (u).translated (b.getCentreX(), b.getCentreY());
    for (auto& p : ghostP) p.applyTransform (T);
    for (auto& p : yP) p.applyTransform (T);
    drip.applyTransform (T);
    const bool light = isLight (backdrop);
    const auto outline = light ? lc::ink : juce::Colour (0xff0A0610);
    const float ow = juce::jmax (0.8f, 0.05f * u);
    if (! light)
    {
        const auto off = juce::AffineTransform::translation (0.04f * u, 0.06f * u);
        g.setColour (lc::hem.withAlpha (0.55f)); for (auto* v : { &ghostP, &yP }) for (auto& p : *v) g.fillPath (p, off); g.fillPath (drip, off);
    }
    g.setColour (outline);
    g.strokePath (drip, juce::PathStrokeType (2.0f * ow));
    for (auto* v : { &ghostP, &yP }) for (auto& p : *v) g.strokePath (p, juce::PathStrokeType (2.0f * ow));
    g.setColour (juce::Colour (0xff8B5CF6)); g.fillPath (drip);
    juce::Point<float> a (0.0f, -0.6f), z (0.0f, 1.2f); T.transformPoint (a.x, a.y); T.transformPoint (z.x, z.y);
    g.setGradientFill (juce::ColourGradient (lc::white, a.x, a.y, juce::Colour (0xffE4DCF2), z.x, z.y, false)); for (auto& p : ghostP) g.fillPath (p);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff8DFFBE), a.x, a.y, lc::slime.darker (0.08f), z.x, z.y, false)); for (auto& p : yP) g.fillPath (p);
}

// ---------------------------------------------------------------------------------------------------- icon, hero, social card
inline void drawIcon (juce::Graphics& g, juce::Rectangle<float> b)
{
    const float s = juce::jmin (b.getWidth(), b.getHeight());
    const auto sq = b.withSizeKeepingCentre (s, s).reduced (s * 0.02f);
    const float corner = s * 0.225f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2A1650), sq.getCentreX(), sq.getY(), juce::Colour (0xff0B0714), sq.getCentreX(), sq.getBottom(), false));
    g.fillRoundedRectangle (sq, corner);
    g.setGradientFill (juce::ColourGradient (lc::ecto.withAlpha (0.4f), sq.getCentreX(), sq.getY() + s * 0.4f, lc::ecto.withAlpha (0.0f), sq.getCentreX() + s * 0.55f, sq.getY() + s * 0.4f, true));
    g.fillRoundedRectangle (sq, corner);
    g.setColour (lc::ecto.withAlpha (0.8f)); g.drawRoundedRectangle (sq.reduced (juce::jmax (0.5f, s * 0.008f)), corner, juce::jmax (1.0f, s * 0.016f));
    juce::Graphics::ScopedSaveState ss (g);
    juce::Path clip; clip.addRoundedRectangle (sq, corner); g.reduceClipRegion (clip);
    drawMascot (g, sq.reduced (s * (s <= 32.0f ? 0.05f : 0.1f)).translated (0.0f, s * 0.02f), juce::Colour (0xff160C2A), 0.0f);
}

namespace detail
{
    inline void backdrop (juce::Graphics& g, juce::Rectangle<float> b)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1A0F2E), b.getX(), b.getY(), juce::Colour (0xff07050C), b.getX(), b.getBottom(), false));
        g.fillRect (b);
        juce::Random r (99);
        juce::Path stars;
        for (int i = 0; i < 70; ++i) { const float x = b.getX() + r.nextFloat() * b.getWidth(), y = b.getY() + r.nextFloat() * b.getHeight(), s = 0.6f + r.nextFloat() * 1.6f; stars.addEllipse (x - s, y - s, s * 2, s * 2); }
        g.setColour (juce::Colours::white.withAlpha (0.25f)); g.fillPath (stars);
    }
    inline void caption (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, juce::Colour c, float h)
    {
        g.setColour (c); g.setFont (juce::Font (juce::FontOptions (h, juce::Font::bold)));
        g.drawText (text, r, juce::Justification::centredLeft);
    }
}

inline void drawHero (juce::Graphics& g, juce::Rectangle<float> b, float animPhase)
{
    detail::backdrop (g, b);
    const float W = b.getWidth(), H = b.getHeight();
    const auto c = juce::Point<float> (b.getX() + W * 0.3f, b.getY() + H * 0.5f);
    g.setGradientFill (juce::ColourGradient (lc::ecto.withAlpha (0.35f), c.x, c.y, lc::ecto.withAlpha (0.0f), c.x + W * 0.33f, c.y, true)); g.fillRect (b);
    const float mh = H * 0.74f;
    drawMascot (g, juce::Rectangle<float> (c.x - mh * 0.5f, c.y - mh * 0.5f, mh, mh), juce::Colour (0xff160C2A), animPhase);
    drawWordmark (g, juce::Rectangle<float> (b.getX() + W * 0.53f, b.getY() + H * 0.24f, W * 0.42f, H * 0.4f), juce::Colour (0xff120A20));
    detail::caption (g, juce::Rectangle<float> (b.getX() + W * 0.56f, b.getY() + H * 0.66f, W * 0.4f, H * 0.07f), "free synth", lc::slime, H * 0.05f);
    detail::caption (g, juce::Rectangle<float> (b.getX() + W * 0.56f, b.getY() + H * 0.73f, W * 0.4f, H * 0.05f), "by GhostBlock", juce::Colour (0xffC9C0DE), H * 0.032f);
}

inline void drawSocial (juce::Graphics& g, juce::Rectangle<float> b)
{
    detail::backdrop (g, b);
    const float W = b.getWidth(), H = b.getHeight();
    const auto c = juce::Point<float> (b.getX() + W * 0.26f, b.getY() + H * 0.5f);
    g.setGradientFill (juce::ColourGradient (lc::ecto.withAlpha (0.38f), c.x, c.y, lc::ecto.withAlpha (0.0f), c.x + W * 0.3f, c.y, true)); g.fillRect (b);
    const float mh = H * 0.72f;
    drawMascot (g, juce::Rectangle<float> (c.x - mh * 0.5f, c.y - mh * 0.5f, mh, mh), juce::Colour (0xff160C2A), 0.0f);
    drawWordmark (g, juce::Rectangle<float> (b.getX() + W * 0.5f, b.getY() + H * 0.16f, W * 0.44f, H * 0.42f), juce::Colour (0xff120A20));
    detail::caption (g, juce::Rectangle<float> (b.getX() + W * 0.53f, b.getY() + H * 0.62f, W * 0.44f, H * 0.08f), "free wavetable synth", lc::slime, H * 0.058f);
    detail::caption (g, juce::Rectangle<float> (b.getX() + W * 0.53f, b.getY() + H * 0.71f, W * 0.44f, H * 0.06f), "VST3 / Standalone  -  open source", juce::Colour (0xffC9C0DE), H * 0.038f);
}
} // namespace ghosty::logo
