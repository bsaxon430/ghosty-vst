#pragma once
// GHOSTY BEATS brand art (the name again since 2026-10-30; "GHOST KIT" now names its own drum sounds): the headphones ghost on the pads + the dripping "GHOST KIT"
// wordmark, drawn procedurally in the house style of the Ghosty Pro pad ghosts (juce::Path + gradients + glow layers, no bitmaps,
// no font: the letters are our own block letterforms built from strokes, with drips added).
//
//   drawMascot (g, bounds, theme, animPhase)   the full mascot: white ghost, purple hem, narrowed eyes with green pupils, black over-ear
//                                              headphones (ghost badge on the cups), neon-green slime, hands on a pad controller.
//                                              24..32 px (header) up to 1000+ px; small sizes drop the fine detail.
//                                              Idle animation (cheap: a few paths): a slime drip grows and lets go of a drop, the eye
//                                              glint flashes, one pad lights in time. animPhase = seconds of any running clock (loops 4 s).
//   drawBust   (g, bounds, theme, animPhase)   the ghost without the pad controller (whole hem visible): small icons, avatars
//   drawWordmark (g, bounds, theme)            "GHOSTY BEATS" in our own pixel-glitch letters; one line when the box is wide (>= 4.2 : 1),
//                                              stacked (GHOSTY over BEATS) otherwise. Fits inside bounds (drips + glitch dashes included).
//   drawIcon   (g, bounds)                     app icon: dark squircle + the ghost (bust under 128 px, full mascot above)
//   drawSplash (g, bounds, theme, animPhase, formerly)  the about / splash card: studio backdrop, mascot, stacked wordmark
//                                              (formerly = the old "(FORMERLY GHOSTY BEATS)" line; Ghosty Beats is the name again)
//   outlineColour (theme) / backdropFor (theme)   the mascot's outline colour and the window colour it is tuned for
//                                              (contrast(outline, backdrop) >= 3:1 in all three looks, see the logo renders)
//
// Message thread only, paint-time only; nothing is cached, nothing allocates beyond the juce::Path objects of one paint.
#include <juce_gui_basics/juce_gui_basics.h>
#include "GhostThemes.h"
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

namespace ghostkit
{
namespace th = ghosty::themes;

namespace kcol
{
    const juce::Colour slime     { 0xff3CFF8F };
    const juce::Colour slimeDeep { 0xff12C060 };
    const juce::Colour ecto      { 0xffA06BFF };
    const juce::Colour hem       { 0xff7C3AED };
    const juce::Colour ink       { 0xff0D0A12 };
    const juce::Colour phone     { 0xff141118 };
    const juce::Colour white     { 0xffFFFFFF };
}

inline juce::Colour backdropFor (th::Theme t) { return th::get (t).window; }
// the neon rim around the ghost: slime green on the dark looks, deepened until it reads on LIGHT
inline juce::Colour outlineColour (th::Theme t) { return th::readableOn (kcol::slime, backdropFor (t), 4.5); }

namespace detail
{
    inline float frac (float v) { return v - std::floor (v); }
    inline float smooth (float e0, float e1, float x) { const float u = juce::jlimit (0.0f, 1.0f, (x - e0) / (e1 - e0)); return u * u * (3.0f - 2.0f * u); }
    inline juce::PathStrokeType stroke (float w) { return juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded); }

    // unit space: the ghost's head centre is (0, -0.2), head radius ~0.74; R = pixels per unit
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
    };

    // a hanging drip: attached at (x, y0) (a little above, so it melts out of the shape), length len, stem width w, ball at the end
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
    inline void addDrop (juce::Path& d, float x, float y, float r)   // a falling drop (pointed top)
    {
        d.startNewSubPath (x, y - r * 1.9f);
        d.cubicTo (x + r * 0.35f, y - r * 1.1f, x + r, y - r * 0.6f, x + r, y);
        d.cubicTo (x + r, y + r * 0.6f, x + r * 0.55f, y + r, x, y + r);
        d.cubicTo (x - r * 0.55f, y + r, x - r, y + r * 0.6f, x - r, y);
        d.cubicTo (x - r, y - r * 0.6f, x - r * 0.35f, y - r * 1.1f, x, y - r * 1.9f);
        d.closeSubPath();
    }

    inline juce::Path body()
    {
        juce::Path p;
        p.startNewSubPath (-0.74f, -0.16f);
        p.cubicTo (-0.74f, -0.63f, -0.41f, -0.95f, 0.0f, -0.95f);
        p.cubicTo (0.41f, -0.95f, 0.74f, -0.63f, 0.74f, -0.16f);
        p.cubicTo (0.75f, 0.16f, 0.82f, 0.4f, 0.9f, 0.56f);
        const float top = 0.56f;
        for (int i = 0; i < 4; ++i)
        {
            const float x0 = 0.9f - (float) i * 0.45f, x1 = x0 - 0.45f, d = 0.24f + (i == 1 ? 0.04f : 0.0f);
            p.cubicTo (x0 - 0.03f, top + d, x1 + 0.03f, top + d, x1, top);
        }
        p.cubicTo (-0.82f, 0.4f, -0.75f, 0.16f, -0.74f, -0.16f);
        p.closeSubPath();
        return p;
    }
    // the narrowed (angry-ish) eye: flat top slanting down towards the nose, round bottom. s = +1 left eye, -1 right eye
    inline void addEye (juce::Path& e, float cx, float cy, float s, float k = 1.0f)
    {
        e.startNewSubPath (cx - 0.15f * s * k, cy - 0.105f * k);
        e.lineTo (cx + 0.15f * s * k, cy + 0.0f);
        e.cubicTo (cx + 0.17f * s * k, cy + 0.17f * k, cx + 0.03f * s * k, cy + 0.215f * k, cx - 0.06f * s * k, cy + 0.2f * k);
        e.cubicTo (cx - 0.17f * s * k, cy + 0.17f * k, cx - 0.2f * s * k, cy + 0.03f * k, cx - 0.15f * s * k, cy - 0.105f * k);
        e.closeSubPath();
    }
    inline void addBadgeGhost (juce::Path& p, float cx, float cy, float w)   // the little ghost on the ear cups
    {
        const float h = w * 1.15f, x0 = cx - w * 0.5f, y0 = cy - h * 0.5f;
        p.startNewSubPath (x0, y0 + h * 0.45f);
        p.cubicTo (x0, y0 - h * 0.05f, x0 + w, y0 - h * 0.05f, x0 + w, y0 + h * 0.45f);
        p.lineTo (x0 + w, y0 + h);
        p.lineTo (x0 + w * 0.75f, y0 + h * 0.86f); p.lineTo (x0 + w * 0.5f, y0 + h); p.lineTo (x0 + w * 0.25f, y0 + h * 0.86f); p.lineTo (x0, y0 + h);
        p.closeSubPath();
    }
    inline juce::Point<float> lerp (juce::Point<float> a, juce::Point<float> b, float t) { return a + (b - a) * t; }

    // ---------------------------------------------------------------------------------------------------- the mascot
    inline void drawGhostKit (juce::Graphics& g, const U& S, th::Theme theme, float phase, bool pads)
    {
        const float R = S.R;
        const auto& P = th::get (theme);
        const bool light = P.isLight;
        const auto rim = outlineColour (theme);
        const bool tiny = R < 14.0f, big = R >= 40.0f;
        const float t = phase;
        const float ow = juce::jlimit (0.9f, 4.5f, 0.03f * R);   // outline half-width in px (thin crisp rim on big renders)

        // glow behind the ghost (a radial fill; the rim colour, faint)
        {
            g.setGradientFill (S.rad (rim.withAlpha (light ? 0.16f : 0.30f), 0.0f, -0.15f, rim.withAlpha (0.0f), 1.45f));
            g.fillEllipse (juce::Rectangle<float> (S.p (-1.45f, -1.6f), S.p (1.45f, 1.3f)));
        }
        const auto bodyPx = S.map (body());

        // headphone band (behind the dome's top edge it rests on)
        juce::Path band; band.addCentredArc (0.0f, -0.2f, 0.86f, 0.84f, 0.0f, -1.32f, 1.32f, true);
        const auto bandPx = S.map (band);
        g.setColour (rim.withAlpha (0.9f)); g.strokePath (bandPx, juce::PathStrokeType (0.15f * R + 2.0f * ow));

        // body: crisp rim under the fill, white -> lavender -> purple hem
        g.setColour (rim); g.strokePath (bodyPx, juce::PathStrokeType (2.0f * ow, juce::PathStrokeType::curved));
        {
            auto gb = S.lin (kcol::white, 0.0f, -0.95f, kcol::hem, 0.0f, 0.8f);
            gb.addColour (0.42, juce::Colour (0xffF7F4FC)); gb.addColour (0.62, juce::Colour (0xffE2D6F8)); gb.addColour (0.8, juce::Colour (0xffA77BFF));
            g.setGradientFill (gb); g.fillPath (bodyPx);
        }
        {
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (bodyPx);
            // the purple hem band (a wavy top edge)
            juce::Path hemP; hemP.startNewSubPath (-1.0f, 0.34f);
            for (int i = 0; i <= 8; ++i) { const float x = -1.0f + (float) i * 0.25f; hemP.quadraticTo (x - 0.125f, 0.34f + (i % 2 ? 0.06f : -0.04f), x, 0.33f); }
            hemP.lineTo (1.0f, 1.2f); hemP.lineTo (-1.0f, 1.2f); hemP.closeSubPath();
            auto hg = S.lin (juce::Colour (0xffB794FF), 0.0f, 0.3f, juce::Colour (0xff5B21B6), 0.0f, 0.85f);
            g.setGradientFill (hg); g.fillPath (S.map (hemP));
            // soft shading on the right, a highlight on the dome
            g.setGradientFill (S.rad (juce::Colour (0xff6D4AA8).withAlpha (0.22f), 0.95f, 0.1f, juce::Colour (0xff6D4AA8).withAlpha (0.0f), 0.9f));
            g.fillPath (bodyPx);
            if (! tiny) { g.setGradientFill (S.rad (kcol::white.withAlpha (0.9f), -0.32f, -0.62f, kcol::white.withAlpha (0.0f), 0.36f)); g.fillPath (bodyPx); }
            // the slime cap on the crown, dripping onto the face (one drip grows and lets go of a drop: the idle animation)
            juce::Path cap;
            cap.startNewSubPath (-0.62f, -0.62f);
            cap.cubicTo (-0.6f, -1.05f, 0.05f, -1.12f, 0.3f, -0.9f);
            cap.cubicTo (0.24f, -0.82f, 0.16f, -0.84f, 0.1f, -0.78f);
            cap.cubicTo (0.04f, -0.8f, -0.02f, -0.74f, -0.08f, -0.77f);
            cap.cubicTo (-0.2f, -0.72f, -0.28f, -0.8f, -0.38f, -0.72f);
            cap.cubicTo (-0.48f, -0.66f, -0.54f, -0.7f, -0.62f, -0.62f);
            cap.closeSubPath();
            const float cyc = frac (t / 4.0f), grow = smooth (0.0f, 0.7f, cyc) * (1.0f - smooth (0.72f, 0.8f, cyc));
            juce::Path drips;
            addDrip (drips, -0.38f, -0.73f, 0.12f + 0.05f * grow, 0.075f);
            addDrip (drips, 0.12f, -0.79f, 0.07f, 0.06f);
            addDrip (drips, -0.02f, -0.77f, 0.2f + 0.15f * grow, 0.07f);       // the animated one
            if (! tiny) addDrip (drips, 0.6f, -0.1f, 0.22f, 0.07f);           // down the right side
            if (! tiny) addDrip (drips, -0.66f, 0.05f, 0.3f, 0.08f);          // down the left side
            auto capPx = S.map (cap), dripPx = S.map (drips);
            if (! tiny) { g.setColour (kcol::slimeDeep); g.strokePath (capPx, juce::PathStrokeType (juce::jmax (1.0f, 0.03f * R))); g.strokePath (dripPx, juce::PathStrokeType (juce::jmax (1.0f, 0.03f * R))); }
            g.setGradientFill (S.lin (juce::Colour (0xffA8FFC9), -0.2f, -1.0f, kcol::slime, 0.0f, -0.6f));
            g.fillPath (capPx); g.setColour (kcol::slime); g.fillPath (dripPx);
            if (! tiny)
            {
                // side blobs the drips come from + speckles on the body
                juce::Path sp;
                const float spk[][3] { { 0.52f, -0.16f, 0.07f }, { -0.62f, 0.02f, 0.08f }, { 0.36f, 0.18f, 0.035f }, { -0.42f, 0.28f, 0.03f }, { 0.58f, 0.4f, 0.04f }, { -0.2f, 0.08f, 0.022f }, { 0.12f, 0.3f, 0.025f } };
                for (auto& s : spk) sp.addEllipse (s[0] - s[2], s[1] - s[2] * 0.8f, s[2] * 2.0f, s[2] * 1.6f);
                g.fillPath (S.map (sp));
                juce::Path gl; gl.addEllipse (-0.42f, -0.95f, 0.22f, 0.07f); gl.addEllipse (0.04f, -0.98f, 0.08f, 0.04f);
                g.setColour (kcol::white.withAlpha (0.75f)); g.fillPath (S.map (gl));
            }
        }
        // the drop that let go (falls in front of the face)
        {
            const float cyc = frac (t / 4.0f);
            if (cyc > 0.72f)
            {
                const float f = (cyc - 0.72f) / 0.28f;
                juce::Path d; addDrop (d, -0.02f, -0.4f + f * f * 1.1f, 0.045f);
                g.setColour (kcol::slime.withAlpha (1.0f - smooth (0.75f, 1.0f, f))); g.fillPath (S.map (d));
            }
        }

        // eyes: narrowed, with heavy brows; glowing green pupils; a glint that flashes
        {
            const float ey = -0.24f;
            juce::Path eyes; addEye (eyes, -0.27f, ey, 1.0f); addEye (eyes, 0.27f, ey, -1.0f);
            const auto eyesPx = S.map (eyes);
            g.setColour (kcol::ink); g.fillPath (eyesPx);
            if (! tiny)
            {
                juce::Path brow;   // a wedge over each eye along the slant
                for (float s : { 1.0f, -1.0f })
                {
                    const float cx = -0.27f * s;
                    brow.startNewSubPath (cx - 0.19f * s, ey - 0.17f);
                    brow.lineTo (cx + 0.18f * s, ey - 0.03f);
                    brow.lineTo (cx + 0.15f * s, ey + 0.02f);
                    brow.lineTo (cx - 0.17f * s, ey - 0.11f);
                    brow.closeSubPath();
                }
                g.fillPath (S.map (brow));
            }
            juce::Path pu;
            for (float s : { 1.0f, -1.0f }) { const float px = -0.27f * s + 0.035f * s; pu.addEllipse (px - 0.06f, ey + 0.02f, 0.12f, 0.14f); }
            {
                juce::Graphics::ScopedSaveState ss (g);
                g.reduceClipRegion (eyesPx);
                if (big) for (float s : { 1.0f, -1.0f }) { const float px = -0.27f * s + 0.035f * s;
                    g.setGradientFill (S.rad (kcol::slime.withAlpha (0.55f), px, ey + 0.09f, kcol::slime.withAlpha (0.0f), 0.16f));
                    g.fillEllipse (juce::Rectangle<float> (S.p (px - 0.16f, ey - 0.07f), S.p (px + 0.16f, ey + 0.25f))); }
                g.setGradientFill (S.lin (juce::Colour (0xffC8FFDC), 0.0f, ey + 0.02f, kcol::slime, 0.0f, ey + 0.16f));
                g.fillPath (S.map (pu));
            }
            // glint: always a small dot, flashing to a sparkle every 4 s
            const float fl = std::exp (-std::pow ((frac (t / 4.0f + 0.35f) - 0.5f) * 14.0f, 2.0f));
            juce::Path gl;
            for (float s : { 1.0f, -1.0f })
            {
                const float px = -0.27f * s + 0.035f * s - 0.025f, py = ey + 0.055f, r = 0.026f + 0.012f * fl;
                gl.addEllipse (px - r, py - r, r * 2.0f, r * 2.0f);
                if (fl > 0.05f && ! tiny)
                {
                    const float q = 0.09f * fl, w = 0.012f;
                    gl.startNewSubPath (px, py - q); gl.lineTo (px + w, py - w); gl.lineTo (px + q, py); gl.lineTo (px + w, py + w);
                    gl.lineTo (px, py + q); gl.lineTo (px - w, py + w); gl.lineTo (px - q, py); gl.lineTo (px - w, py - w); gl.closeSubPath();
                }
            }
            g.setColour (kcol::white.withAlpha (tiny ? 0.85f + 0.15f * fl : 1.0f)); g.fillPath (S.map (gl));
            // a slime tear running from the right eye
            if (! tiny) { juce::Path tr; addDrip (tr, 0.22f, -0.07f, 0.12f + 0.03f * std::sin (t * 1.6f), 0.05f); g.setColour (kcol::slime); g.fillPath (S.map (tr)); }
        }

        // headphones: the band on top, the over-ear cups with a green ring + the ghost badge
        {
            g.setColour (kcol::phone); g.strokePath (bandPx, juce::PathStrokeType (0.15f * R, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
            if (! tiny)
            {
                juce::Path hl; hl.addCentredArc (0.0f, -0.2f, 0.86f, 0.84f, 0.0f, -0.9f, 0.5f, true);
                g.setColour (juce::Colour (0xff4A4258)); g.strokePath (S.map (hl), juce::PathStrokeType (0.035f * R, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            for (float s : { -1.0f, 1.0f })
            {
                const float cx = 0.83f * s, cy = -0.2f;
                juce::Path yoke; yoke.addRoundedRectangle (cx - 0.07f, cy - 0.44f, 0.14f, 0.2f, 0.05f);
                juce::Path cup; cup.addRoundedRectangle (cx - 0.15f, cy - 0.29f, 0.3f, 0.58f, 0.13f);
                juce::Path face; face.addRoundedRectangle (cx - 0.1f + 0.02f * s, cy - 0.23f, 0.2f, 0.46f, 0.09f);
                auto cupPx = S.map (cup);
                g.setColour (rim); g.strokePath (cupPx, juce::PathStrokeType (2.0f * ow));
                g.setColour (kcol::phone); g.fillPath (S.map (yoke));
                g.setGradientFill (S.lin (juce::Colour (0xff2C2733), cx, cy - 0.3f, juce::Colour (0xff0A090C), cx, cy + 0.3f)); g.fillPath (cupPx);
                g.setColour (juce::Colour (0xff050407)); g.fillPath (S.map (face));
                g.setColour (kcol::slime.withAlpha (light ? 1.0f : 0.95f));
                g.strokePath (S.map (face), juce::PathStrokeType (juce::jmax (0.8f, 0.022f * R)));
                juce::Path badge;
                if (R >= 22.0f) addBadgeGhost (badge, cx + 0.02f * s, cy + 0.01f, 0.12f);
                else badge.addEllipse (cx - 0.045f + 0.02f * s, cy - 0.045f, 0.09f, 0.09f);
                g.setColour (kcol::slime); g.fillPath (S.map (badge));
                if (R >= 22.0f) { juce::Path be; be.addEllipse (cx + 0.02f * s - 0.035f, cy - 0.025f, 0.025f, 0.03f); be.addEllipse (cx + 0.02f * s + 0.01f, cy - 0.025f, 0.025f, 0.03f);
                                  g.setColour (juce::Colour (0xff050407)); g.fillPath (S.map (be)); }
            }
        }

        if (! pads) return;
        // the pad controller (in front of the hem) + the two hands on it
        {
            const auto TL = S.p (-0.92f, 0.6f), TR = S.p (0.92f, 0.6f), BR = S.p (1.08f, 0.86f), BL = S.p (-1.08f, 0.86f);
            const auto FBR = S.p (1.08f, 1.0f), FBL = S.p (-1.08f, 1.0f);
            juce::Path top; top.startNewSubPath (TL); top.lineTo (TR); top.lineTo (BR); top.lineTo (BL); top.closeSubPath();
            juce::Path front; front.startNewSubPath (BL); front.lineTo (BR); front.lineTo (FBR); front.lineTo (FBL); front.closeSubPath();
            juce::Path slab = top; slab.addPath (front);
            g.setColour (rim); g.strokePath (slab, juce::PathStrokeType (2.0f * ow, juce::PathStrokeType::beveled));
            g.setGradientFill (S.lin (juce::Colour (0xff2A2433), 0.0f, 0.6f, juce::Colour (0xff17131C), 0.0f, 0.86f)); g.fillPath (top);
            g.setColour (juce::Colour (0xff0B090E)); g.fillPath (front);
            auto quad = [&] (float u0, float v0, float u1, float v1)
            {
                auto at = [&] (float u, float v) { return lerp (lerp (TL, TR, u), lerp (BL, BR, u), v); };
                juce::Path q; q.startNewSubPath (at (u0, v0)); q.lineTo (at (u1, v0)); q.lineTo (at (u1, v1)); q.lineTo (at (u0, v1)); q.closeSubPath(); return q;
            };
            const int cols = 4, rows = tiny ? 1 : 2;
            const int beat = (int) std::floor (t * 2.0f) % (cols * rows);
            for (int r = 0; r < rows; ++r)
                for (int c = 0; c < cols; ++c)
                {
                    const int i = r * cols + c;
                    const float u0 = 0.07f + (float) c * 0.225f, v0 = rows == 1 ? 0.3f : 0.16f + (float) r * 0.42f;
                    auto q = quad (u0, v0, u0 + 0.16f, v0 + (rows == 1 ? 0.42f : 0.28f));
                    const bool lit = i == beat, purple = i == 6 || (rows == 1 && i == 2);
                    juce::Colour c0 = purple ? kcol::ecto : (i % 3 == 0 ? kcol::slime : juce::Colour (0xff1F8F52));
                    if (lit) c0 = c0.brighter (0.5f);
                    g.setColour (c0); g.fillPath (q);
                    if (lit && ! tiny) { g.setColour (c0.withAlpha (0.45f)); g.strokePath (q, juce::PathStrokeType (0.05f * R)); }
                }
            if (! tiny)
            {
                juce::Path knobs; for (int k = 0; k < 6; ++k) { const auto c = lerp (FBL, FBR, 0.1f + (float) k * 0.16f) + juce::Point<float> (0.0f, -0.07f * R); knobs.addEllipse (c.x - 0.022f * R, c.y - 0.022f * R, 0.044f * R, 0.044f * R); }
                g.setColour (juce::Colour (0xff3A3344)); g.fillPath (knobs);
            }
            // hands (with knuckles), resting on the pads; a slime drip running off the right one
            for (float s : { -1.0f, 1.0f })
            {
                const float hx = 0.44f * s, hy = 0.64f + (s > 0 ? -0.01f : 0.01f);
                juce::Path hand; hand.addEllipse (hx - 0.2f, hy - 0.11f, 0.4f, 0.21f);
                if (! tiny) for (int k = 0; k < 3; ++k) hand.addEllipse (hx - 0.17f + (float) k * 0.115f, hy + 0.0f, 0.11f, 0.12f);
                auto hp = S.map (hand);
                g.setColour (rim); g.strokePath (hp, juce::PathStrokeType (2.0f * ow));
                g.setGradientFill (S.lin (kcol::white, hx, hy - 0.11f, juce::Colour (0xffD9CCF4), hx, hy + 0.12f)); g.fillPath (hp);
                if (! tiny)
                {
                    juce::Path kn; for (int k = 0; k < 2; ++k) { const float x = hx - 0.06f + (float) k * 0.115f; kn.startNewSubPath (x, hy + 0.03f); kn.lineTo (x, hy + 0.09f); }
                    g.setColour (juce::Colour (0xffB8A6DC)); g.strokePath (S.map (kn), stroke (juce::jmax (0.8f, 0.018f * R)));
                }
            }
            if (! tiny)
            {
                juce::Path hd; addDrip (hd, 0.52f, 0.7f, 0.1f + 0.04f * std::sin (t * 1.3f), 0.05f);
                hd.addEllipse (0.38f, 0.58f, 0.08f, 0.05f);
                g.setColour (kcol::slime); g.fillPath (S.map (hd));
            }
        }
    }
} // namespace detail

inline void drawMascot (juce::Graphics& g, juce::Rectangle<float> b, th::Theme theme, float animPhase)
{
    const float R = juce::jmin (b.getWidth() / 2.3f, b.getHeight() / 2.2f);
    detail::drawGhostKit (g, detail::U (b.getCentreX(), b.getCentreY() + 0.07f * R, R), theme, animPhase, true);
}
inline void drawBust (juce::Graphics& g, juce::Rectangle<float> b, th::Theme theme, float animPhase)
{
    const float R = juce::jmin (b.getWidth() / 2.1f, b.getHeight() / 1.95f);
    detail::drawGhostKit (g, detail::U (b.getCentreX(), b.getCentreY() + 0.12f * R, R), theme, animPhase, false);
}

// ---------------------------------------------------------------------------------------------------- the wordmark
// GHOSTY BEATS (2026-10-30, owner: "make Ghosty Beats Ghosty Beats again"): our own chunky pixel letters (5 x 7 bitmaps made bold
// by doubling every stem one pixel right, so 6 x 7), white -> mint, a dark outline, a purple drop shadow, a green RGB split to the
// left, a glitch slice (one row band nudged right), slime drips under some stems and horizontal glitch dashes beside each line.
// No font and no bitmap: every pixel is a rectangle in a juce::Path, so it is crisp at any size.
namespace detail
{
    inline const char* const* pixelGlyph (char c)
    {
        static const char* G[] { ".####", "#....", "#....", "#..##", "#...#", "#...#", ".###." };
        static const char* H[] { "#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#" };
        static const char* O[] { ".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###." };
        static const char* S[] { ".####", "#....", "#....", ".###.", "....#", "....#", "####." };
        static const char* T[] { "#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.." };
        static const char* Y[] { "#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.." };
        static const char* B[] { "####.", "#...#", "#...#", "####.", "#...#", "#...#", "####." };
        static const char* E[] { "#####", "#....", "#....", "####.", "#....", "#....", "#####" };
        static const char* A[] { ".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#" };
        static const char* K[] { "#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#" };
        static const char* I[] { "#####", "..#..", "..#..", "..#..", "..#..", "..#..", "#####" };
        switch (c) { case 'G': return G; case 'H': return H; case 'O': return O; case 'S': return S; case 'T': return T; case 'Y': return Y;
                     case 'B': return B; case 'E': return E; case 'A': return A; case 'K': return K; default: return I; }
    }
    constexpr float kPixAdv = 7.0f;   // 6 columns (bold) + 1 column of space

    // one word in pixel units (a letter is 7 tall), from (x0, y0); sliceRow0..1 = the glitch band (those rows sit +slice to the right)
    inline void buildPixelWord (const char* word, float x0, float y0, int slice0, int slice1, float slice, juce::Path& body, std::vector<juce::Point<float>>& dripAt)
    {
        float x = x0; int li = 0;
        for (const char* c = word; *c; ++c, ++li)
        {
            const auto rows = pixelGlyph (*c);
            for (int r = 0; r < 7; ++r)
            {
                const float dx = (r >= slice0 && r <= slice1) ? slice : 0.0f;
                int run0 = -1;   // horizontal runs of the bold row (OR with itself shifted one right) -> one rect each (fewer path pieces)
                auto on = [&] (int col) { return (col < 5 && rows[r][col] == '#') || (col >= 1 && col - 1 < 5 && rows[r][col - 1] == '#'); };
                for (int col = 0; col <= 6; ++col)
                {
                    const bool o = col < 6 && on (col);
                    if (o && run0 < 0) run0 = col;
                    if (! o && run0 >= 0) { body.addRectangle (x + (float) run0 + dx, y0 + (float) r, (float) (col - run0), 1.0f); run0 = -1; }
                }
            }
            // drips: under the first stem of every other letter, and under T / Y's stem
            const auto last = rows[6];
            if (*c == 'T' || *c == 'Y') dripAt.push_back ({ x + 3.0f, y0 + 7.0f });
            else if (li % 2 == 0 && last[0] == '#') dripAt.push_back ({ x + 1.0f, y0 + 7.0f });
            else if (li % 2 == 1 && last[4] == '#') dripAt.push_back ({ x + 5.0f, y0 + 7.0f });
            x += kPixAdv;
        }
    }
    inline float pixelWordWidth (const char* word) { return (float) std::strlen (word) * kPixAdv - 1.0f; }
} // namespace detail

inline void drawWordmark (juce::Graphics& g, juce::Rectangle<float> b, th::Theme theme)
{
    using namespace detail;
    const auto& P = th::get (theme);
    const bool light = P.isLight;
    const char* w1 = "GHOSTY"; const char* w2 = "BEATS";
    const float W1 = pixelWordWidth (w1), W2 = pixelWordWidth (w2);
    const bool stacked = b.getWidth() / b.getHeight() < 4.2f;
    // extents in pixel units: stacked = W1 x (7 + 2 gap + 7 + drips 3); one line = W1 + 4 + W2 x (7 + drips 3); dashes take 3 a side
    const float dash = 3.5f, extW = (stacked ? W1 : W1 + 4.0f + W2) + 2.0f * dash, extH = (stacked ? 16.0f : 7.0f) + 3.4f;
    const float px = juce::jmin (b.getWidth() / extW, b.getHeight() / extH);
    const float ox = b.getCentreX() - (stacked ? W1 : W1 + 4.0f + W2) * 0.5f * px, oy = b.getY() + (b.getHeight() - extH * px) * 0.5f + 0.2f * px;
    const auto T = juce::AffineTransform::scale (px).translated (ox, oy);
    juce::Path body; std::vector<juce::Point<float>> drips;
    buildPixelWord (w1, 0.0f, 0.0f, 2, 3, 0.35f, body, drips);
    const float x2 = stacked ? (W1 - W2) * 0.5f : W1 + 4.0f, y2 = stacked ? 9.0f : 0.0f;
    buildPixelWord (w2, x2, y2, 4, 4, -0.3f, body, drips);
    juce::Path dp;
    int k = 0;
    for (auto d : drips)
    {
        const float len = 0.9f + (float) ((k * 7 + 3) % 5) * 0.45f; ++k;
        if (stacked && d.y < 8.0f && d.x > x2 - 0.5f && d.x < x2 + W2 + 0.5f) continue;   // (no drip from GHOSTY into BEATS)
        addDrip (dp, d.x, d.y - 0.15f, len, 0.85f);
    }
    auto all = body; all.addPath (dp);
    const float ow = juce::jmax (1.0f, 0.22f * px);
    const auto outline = light ? juce::Colour (0xffFFFFFF) : juce::Colour (0xff07040C);
    const auto purple  = light ? juce::Colour (0xff7C3AED) : juce::Colour (0xffA12CFF);
    const auto green   = light ? juce::Colour (0xff0A9A55) : juce::Colour (0xff39FF88);
    // glitch dashes beside each line (seeded: the same every paint)
    if (px >= 2.0f)
    {
        juce::Path dpur, dgrn;
        auto dashes = [&] (float lx, float rx, float y, int seed)
        {
            juce::Random r (seed);
            for (int i = 0; i < 3; ++i)
            {
                const float yy = y + 0.8f + (float) i * 2.2f + r.nextFloat() * 0.6f, len = 0.9f + r.nextFloat() * 1.8f, h = juce::jmax (0.18f, 1.2f / px);
                (i % 2 == 0 ? dpur : dgrn).addRectangle (lx - 0.8f - len - r.nextFloat() * 0.8f, yy, len, h);
                (i % 2 == 1 ? dpur : dgrn).addRectangle (rx + 0.8f + r.nextFloat() * 0.8f, yy + 0.7f, len * 0.8f, h);
            }
        };
        dashes (0.0f, W1, 0.0f, 31);
        if (stacked) dashes (x2, x2 + W2, y2, 77);
        g.setColour (purple.withAlpha (0.85f)); g.fillPath (dpur, T);
        g.setColour (green.withAlpha (0.8f)); g.fillPath (dgrn, T);
    }
    // purple drop shadow, then the green split, then the outline, then the white -> mint fill
    g.setColour (purple.withAlpha (light ? 0.55f : 0.9f));
    g.fillPath (all, T.translated (0.45f * px, 0.45f * px)); g.strokePath (all, juce::PathStrokeType (ow), T.translated (0.45f * px, 0.45f * px));
    if (! light) { g.setColour (green.withAlpha (0.55f)); g.fillPath (all, T.translated (-0.3f * px, 0.0f)); }
    g.setColour (outline); g.strokePath (all, juce::PathStrokeType (ow * 1.6f, juce::PathStrokeType::mitered), T);
    const auto top = light ? juce::Colour (0xff2A1F45) : juce::Colour (0xffF5F0FF), bottom = light ? juce::Colour (0xff6D28D9) : juce::Colour (0xffB8FFD8);
    for (auto [y0, y1] : { std::pair<float, float> { 0.0f, 7.0f }, std::pair<float, float> { y2, y2 + 7.0f } })
    {
        juce::Point<float> a (0.0f, y0), c (0.0f, y1 + 2.5f); T.transformPoint (a.x, a.y); T.transformPoint (c.x, c.y);
        juce::Graphics::ScopedSaveState ss (g);
        juce::Point<float> ca (-50.0f, y0 - 0.5f), cb (200.0f, y1 + (y1 > 8.0f || ! stacked ? 4.0f : 1.5f)); T.transformPoint (ca.x, ca.y); T.transformPoint (cb.x, cb.y);
        g.reduceClipRegion (juce::Rectangle<float> (ca, cb).toNearestInt());
        g.setGradientFill (juce::ColourGradient (top, a.x, a.y, bottom, c.x, c.y, false)); g.fillPath (all, T);
        if (! stacked) break;
    }
    // CRT scanlines across the letters (big sizes only)
    if (px >= 6.0f)
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (all, T);
        g.setColour (juce::Colour (0xff2A1640).withAlpha (0.13f));
        const auto bb = all.getBoundsTransformed (T);
        for (float y = bb.getY(); y < bb.getBottom(); y += juce::jmax (3.0f, px * 0.6f)) g.fillRect (bb.getX(), y, bb.getWidth(), juce::jmax (1.0f, px * 0.08f));
    }
}

// ---------------------------------------------------------------------------------------------------- icon + splash
inline void drawIcon (juce::Graphics& g, juce::Rectangle<float> b)
{
    const float s = juce::jmin (b.getWidth(), b.getHeight());
    auto r = b.withSizeKeepingCentre (s, s);
    const float corner = s * 0.225f;
    const auto sq = r.reduced (s * 0.02f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff22123A), sq.getCentreX(), sq.getY(), juce::Colour (0xff07040D), sq.getCentreX(), sq.getBottom(), false));
    g.fillRoundedRectangle (sq, corner);
    g.setGradientFill (juce::ColourGradient (kcol::ecto.withAlpha (0.35f), sq.getCentreX(), sq.getY() + s * 0.35f, kcol::ecto.withAlpha (0.0f), sq.getCentreX() + s * 0.55f, sq.getY() + s * 0.35f, true));
    g.fillRoundedRectangle (sq, corner);
    g.setColour (kcol::slime.withAlpha (s < 40 ? 0.9f : 0.7f)); g.drawRoundedRectangle (sq.reduced (juce::jmax (0.5f, s * 0.008f)), corner, juce::jmax (1.0f, s * 0.016f));
    juce::Graphics::ScopedSaveState ss (g);
    juce::Path clip; clip.addRoundedRectangle (sq, corner); g.reduceClipRegion (clip);
    if (s >= 128.0f) drawMascot (g, sq.reduced (s * 0.07f).translated (0.0f, s * 0.01f), th::Theme::Dark, 0.0f);
    else drawBust (g, sq.reduced (s * (s <= 32.0f ? 0.03f : 0.06f)).translated (0.0f, s * 0.03f), th::Theme::Dark, 0.0f);
}

inline void drawSplash (juce::Graphics& g, juce::Rectangle<float> b, th::Theme theme, float animPhase, bool formerly = true)
{
    const auto& P = th::get (theme);
    const bool light = P.isLight;
    // studio backdrop: the theme's window colours, a spotlight behind the ghost, speaker shapes in the dark, a floor line
    const auto c0 = light ? P.card : theme == th::Theme::Purple ? P.windowTop : juce::Colour (0xff120A1E);
    const auto c1 = light ? P.cell : theme == th::Theme::Purple ? P.windowBottom : juce::Colour (0xff030205);
    g.setGradientFill (juce::ColourGradient (c0, b.getX(), b.getY(), c1, b.getX(), b.getBottom(), false)); g.fillRect (b);
    const float W = b.getWidth(), H = b.getHeight();
    auto at = [&] (float fx, float fy) { return juce::Point<float> (b.getX() + fx * W, b.getY() + fy * H); };
    g.setGradientFill (juce::ColourGradient (kcol::ecto.withAlpha (light ? 0.25f : 0.32f), at (0.27f, 0.42f).x, at (0.27f, 0.42f).y, kcol::ecto.withAlpha (0.0f), at (0.27f, 0.42f).x + W * 0.36f, at (0.27f, 0.42f).y, true));
    g.fillRect (b);
    g.setGradientFill (juce::ColourGradient (kcol::slime.withAlpha (light ? 0.12f : 0.14f), at (0.68f, 0.4f).x, at (0.68f, 0.4f).y, kcol::slime.withAlpha (0.0f), at (0.68f, 0.4f).x + W * 0.3f, at (0.68f, 0.4f).y, true));
    g.fillRect (b);
    {
        const auto spk = light ? P.line.withAlpha (0.55f) : juce::Colour (0xff1E1530).withAlpha (0.9f);
        const auto cone = light ? P.cell.darker (0.15f) : juce::Colour (0xff0A0711);
        for (float fx : { 0.015f, 0.905f })
        {
            auto box = juce::Rectangle<float> (at (fx, 0.12f), at (fx + 0.08f, 0.62f));
            g.setColour (spk); g.fillRoundedRectangle (box, W * 0.006f);
            g.setColour (cone);
            g.fillEllipse (box.withSizeKeepingCentre (box.getWidth() * 0.7f, box.getWidth() * 0.7f).withY (box.getY() + box.getHeight() * 0.12f));
            g.fillEllipse (box.withSizeKeepingCentre (box.getWidth() * 0.82f, box.getWidth() * 0.82f).withY (box.getY() + box.getHeight() * 0.5f));
        }
        g.setColour ((light ? P.line : kcol::ecto).withAlpha (0.35f)); g.fillRect (juce::Rectangle<float> (at (0.0f, 0.93f), at (1.0f, 0.932f)));
    }
    const float mh = H * 0.8f;
    drawMascot (g, juce::Rectangle<float> (at (0.27f, 0.47f).x - mh * 0.55f, at (0.27f, 0.47f).y - mh * 0.5f, mh * 1.1f, mh), theme, animPhase);
    drawWordmark (g, juce::Rectangle<float> (at (0.53f, 0.08f), at (0.95f, formerly ? 0.8f : 0.88f)), theme);
    if (formerly)
    {
        const float fh = juce::jmax (9.0f, H * 0.038f);
        auto row = juce::Rectangle<float> (at (0.53f, 0.82f), at (0.95f, 0.82f)).withHeight (fh * 1.4f);
        const juce::Font f (juce::FontOptions (fh, juce::Font::bold));
        const juce::String a ("(FORMERLY "), gb ("GHOSTY BEATS"), z (")");
        auto width = [&] (const juce::String& s) { juce::GlyphArrangement ga; ga.addLineOfText (f, s, 0, 0); return ga.getBoundingBox (0, -1, true).getWidth() + 0.12f * fh * (float) s.length(); };
        const float total = width (a) + width (gb) + width (z);
        float x = row.getCentreX() - total * 0.5f;
        auto put = [&] (const juce::String& s, juce::Colour c)
        {
            juce::GlyphArrangement ga; ga.addLineOfText (f, s, 0, 0);   // letter-spaced: one glyph at a time
            float gx = x;
            for (int i = 0; i < s.length(); ++i)
            {
                juce::GlyphArrangement one; one.addLineOfText (f, s.substring (i, i + 1), gx, row.getCentreY() + fh * 0.35f);
                g.setColour (c); one.draw (g);
                gx += one.getBoundingBox (0, -1, true).getWidth() + (s[i] == ' ' ? fh * 0.3f : 0.0f) + 0.12f * fh;
            }
            x = gx;
        };
        const auto ink = light ? P.ink : juce::Colour (0xffE9E3F5);
        put (a, ink); put (gb, light ? th::readableOn (kcol::slime, P.cell, 4.5) : kcol::slime); put (z, ink);
    }
}
} // namespace ghostkit
