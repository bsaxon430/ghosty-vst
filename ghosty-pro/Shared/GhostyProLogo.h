#pragma once
// The Ghosty PRO logo (no crown): a sleek white ghost with a swept, pointed hook, narrowed angry green eyes and a glowing neon-green slime
// tail that thins into a wisp; wordmark "GHOSTY PRO" in bold italic with a green-boxed VST tag and a PROFESSIONAL AUDIO PLUGIN line.
// Procedural (juce::Path + gradients + glow), no bitmaps. Design direction only from docs/design/brand/ghosty-pro-logo-ref.jpg.
//   drawMascot (g, bounds, backdrop, animPhase)  the ghost, 24 px up to 1000+ px (idle: the tail wisp sways, drips swell)
//   drawWordmark (g, bounds, backdrop)           GHOSTY PRO / VST, fitted inside bounds
//   drawIcon / drawHero / drawSocial             app icon, 1600 x 900 hero, 1200 x 630 social card
#include "GhostyLogo.h"

namespace ghosty::prologo
{
namespace pc
{
    const juce::Colour green { 0xff6CFF2A }, deep { 0xff1F7A0C }, glow { 0xff8DFF4D }, ink { 0xff061004 };
}

namespace detail
{
    // a slime drip: wide where it leaves the edge, a thin neck, a round bulb at the end
    inline void drip (juce::Path& d, float x, float y0, float len, float w)
    {
        d.startNewSubPath (x - w * 0.55f, y0);
        d.cubicTo (x - w * 0.3f, y0 + len * 0.2f, x - w * 0.2f, y0 + len * 0.45f, x - w * 0.2f, y0 + len * 0.7f);
        d.cubicTo (x - w * 0.8f, y0 + len * 0.76f, x - w * 0.8f, y0 + len * 1.1f, x, y0 + len * 1.1f);
        d.cubicTo (x + w * 0.8f, y0 + len * 1.1f, x + w * 0.8f, y0 + len * 0.76f, x + w * 0.2f, y0 + len * 0.7f);
        d.cubicTo (x + w * 0.2f, y0 + len * 0.45f, x + w * 0.3f, y0 + len * 0.2f, x + w * 0.55f, y0);
        d.closeSubPath();
    }
    inline juce::Path body (float t)
    {
        const float sw = 0.05f * std::sin (t * 1.6f);              // the wisp sways a little
        juce::Path p;
        p.startNewSubPath (-0.34f, -1.04f);                        // the hook tip, top left
        p.cubicTo (0.10f, -1.0f, 0.50f, -0.70f, 0.52f, -0.20f);
        p.cubicTo (0.54f, 0.20f, 0.34f, 0.42f, 0.22f + sw * 0.5f, 0.62f);
        p.cubicTo (0.12f + sw, 0.80f, 0.14f + sw, 0.96f, 0.02f + sw * 1.8f, 1.18f);   // tail tip
        p.cubicTo (-0.10f + sw, 0.96f, -0.30f, 0.84f, -0.38f, 0.56f);
        p.cubicTo (-0.52f, 0.24f, -0.52f, -0.12f, -0.50f, -0.38f);
        p.cubicTo (-0.48f, -0.68f, -0.42f, -0.88f, -0.34f, -1.04f);
        p.closeSubPath();
        return p;
    }
}

inline void drawAngry (juce::Graphics& g, juce::Rectangle<float> b, juce::Colour backdrop, float animPhase, float R)
{
    using namespace ghosty::logo;
    ghosty::logo::detail::U S (b.getCentreX(), b.getCentreY() - 0.03f * R, R);
    const bool light = isLight (backdrop), tiny = R < 14.0f;
    const float ow = juce::jlimit (0.9f, 4.0f, 0.028f * R);
    const auto path = S.map (detail::body (animPhase));
    const auto rim = light ? pc::deep : pc::green;

    if (! light)   // neon glow: a few wide soft strokes
        for (int i = 12; i >= 1; --i)
        { g.setColour (pc::green.withAlpha (0.018f + 0.004f * (float) (13 - i))); g.strokePath (path, juce::PathStrokeType ((float) i * 0.035f * R, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); }

    {   // the body: white, going to glowing green toward the tail
        juce::Graphics::ScopedSaveState ss (g); g.reduceClipRegion (path.getBounds().toNearestInt()); juce::Path clip (path); g.saveState();
        g.fillPath (path);
        g.setGradientFill (S.lin (juce::Colours::white, -0.1f, -0.9f, juce::Colour (0xffE9F7E1), 0.1f, 0.35f)); g.fillPath (path);
        g.restoreState();
        juce::Graphics::ScopedSaveState s2 (g); g.reduceClipRegion (path);
        g.setGradientFill (S.lin (pc::green.withAlpha (0.0f), 0.0f, 0.38f, pc::green.withAlpha (light ? 0.75f : 0.95f), 0.0f, 1.16f)); g.fillPath (path);
        g.setGradientFill (S.rad (pc::glow.withAlpha (0.35f), -0.45f, 0.1f, pc::glow.withAlpha (0.0f), 0.5f)); g.fillPath (path);   // left rim light
    }
    g.setColour (rim); g.strokePath (path, juce::PathStrokeType (ow, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // slime drips off the lower edges
    if (! tiny)
    {
        juce::Path d;
        const float sw = 0.04f * std::sin (animPhase * 2.1f);
        detail::drip (d, -0.44f, 0.42f, 0.36f + sw, 0.075f);
        detail::drip (d, -0.22f, 0.76f, 0.22f - sw, 0.055f);
        detail::drip (d, 0.30f, 0.50f, 0.30f + sw, 0.07f);
        detail::drip (d, 0.48f, 0.12f, 0.18f, 0.045f);
        ghosty::logo::detail::addDrop (d, -0.56f, 0.72f + 0.03f * std::sin (animPhase * 1.4f), 0.025f);
        ghosty::logo::detail::addDrop (d, 0.20f, 1.12f, 0.03f);
        const auto dp = S.map (d);
        g.setColour (pc::ink); g.strokePath (dp, juce::PathStrokeType (ow * 1.6f));
        g.setGradientFill (S.lin (pc::glow, 0.0f, 0.4f, pc::green, 0.0f, 1.1f)); g.fillPath (dp);
    }

    // the face: two narrowed angry eyes, glowing green
    const struct { float x0, y0, x1, y1, x2, y2; } eye[2] { { -0.42f, -0.40f, -0.04f, -0.20f, -0.20f, 0.03f }, { 0.04f, -0.20f, 0.42f, -0.40f, 0.20f, 0.03f } };
    for (int i = 0; i < 2; ++i)
    {
        juce::Path e;
        const auto& q = eye[i];
        e.startNewSubPath (S.p (q.x0, q.y0)); e.quadraticTo (S.p ((q.x0 + q.x1) * 0.5f, (q.y0 + q.y1) * 0.5f - 0.05f), S.p (q.x1, q.y1));
        e.quadraticTo (S.p (q.x2 + (i == 0 ? 0.10f : -0.10f), q.y2 + 0.02f), S.p (q.x2, q.y2)); e.quadraticTo (S.p (q.x0 + (i == 0 ? 0.05f : -0.05f), q.y0 + 0.14f), S.p (q.x0, q.y0));
        e.closeSubPath();
        g.setColour (pc::ink); g.fillPath (e);
        const float cx = S.p (i == 0 ? -0.20f : 0.20f, -0.17f).x, cy = S.p (0.0f, -0.15f).y;
        if (! tiny)
        {
            juce::Graphics::ScopedSaveState s3 (g); g.reduceClipRegion (e.getBounds().toNearestInt()); juce::Path c; c.addPath (e); g.reduceClipRegion (e.createPathWithRoundedCorners (0));
            g.setGradientFill (juce::ColourGradient (juce::Colours::white, cx, cy, pc::green, cx + 0.16f * R, cy, true)); g.fillPath (e);
        }
        else { g.setColour (pc::green); g.fillPath (e); }
        g.setColour (pc::ink); g.strokePath (e, juce::PathStrokeType (juce::jmax (0.8f, 0.02f * R), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        if (! tiny && ! light) { g.setColour (pc::green.withAlpha (0.35f)); g.strokePath (e, juce::PathStrokeType (0.045f * R)); }
    }
}

namespace gold { const juce::Colour hi { 0xffFFE08A }, mid { 0xffF2B531 }, lo { 0xffB87410 }, ink { 0xff3A2406 }; }

// the Ghosty Pro mark: the sleek angry ghost (no crown)
inline void drawMascot (juce::Graphics& g, juce::Rectangle<float> b, juce::Colour backdrop, float animPhase)
{
    const float R = juce::jmin (b.getWidth() / 1.25f, b.getHeight() / 2.3f);
    drawAngry (g, b, backdrop, animPhase, R);
}

inline void drawBadge (juce::Graphics& g, juce::Rectangle<float> r)
{
    const float cr = r.getHeight() * 0.28f;
    g.setColour (gold::ink); g.fillRoundedRectangle (r.expanded (r.getHeight() * 0.04f), cr);
    g.setGradientFill (juce::ColourGradient (gold::hi, r.getX(), r.getY(), gold::lo, r.getX(), r.getBottom(), false)); g.fillRoundedRectangle (r, cr);
    g.setColour (gold::ink); g.setFont (juce::Font (juce::FontOptions (r.getHeight() * 0.66f, juce::Font::bold)));
    g.drawText ("PRO", r, juce::Justification::centred);
}

inline void drawWordmark (juce::Graphics& g, juce::Rectangle<float> b, juce::Colour backdrop)
{
    const float badgeW = b.getHeight() * 1.0f, gap = b.getHeight() * 0.1f;
    ghosty::logo::drawWordmark (g, b.withTrimmedRight (badgeW + gap), backdrop);
    drawBadge (g, juce::Rectangle<float> (b.getRight() - badgeW, b.getCentreY() - b.getHeight() * 0.25f, badgeW, b.getHeight() * 0.5f));
}

inline void drawIcon (juce::Graphics& g, juce::Rectangle<float> b)
{
    using namespace ghosty::logo;
    const float s = juce::jmin (b.getWidth(), b.getHeight());
    const auto sq = b.withSizeKeepingCentre (s, s).reduced (s * 0.02f);
    const float corner = s * 0.225f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff10150E), sq.getCentreX(), sq.getY(), juce::Colour (0xff030603), sq.getCentreX(), sq.getBottom(), false));
    g.fillRoundedRectangle (sq, corner);
    g.setGradientFill (juce::ColourGradient (pc::green.withAlpha (0.22f), sq.getCentreX(), sq.getBottom() - s * 0.2f, pc::green.withAlpha (0.0f), sq.getCentreX() + s * 0.6f, sq.getBottom() - s * 0.2f, true));
    g.fillRoundedRectangle (sq, corner);
    g.setColour (gold::mid.withAlpha (0.85f)); g.drawRoundedRectangle (sq.reduced (juce::jmax (0.5f, s * 0.008f)), corner, juce::jmax (1.0f, s * 0.016f));
    juce::Graphics::ScopedSaveState ss (g);
    juce::Path clip; clip.addRoundedRectangle (sq, corner); g.reduceClipRegion (clip);
    drawMascot (g, sq.reduced (s * (s <= 32.0f ? 0.06f : 0.1f)), juce::Colour (0xff060A05), 0.6f);
}

inline void drawHero (juce::Graphics& g, juce::Rectangle<float> b, float animPhase)
{
    using namespace ghosty::logo;
    ghosty::logo::detail::backdrop (g, b);
    const float W = b.getWidth(), H = b.getHeight();
    const auto c = juce::Point<float> (b.getX() + W * 0.2f, b.getY() + H * 0.5f);
    g.setGradientFill (juce::ColourGradient (pc::green.withAlpha (0.16f), c.x, c.y, pc::green.withAlpha (0.0f), c.x + W * 0.3f, c.y, true)); g.fillRect (b);
    drawMascot (g, { b.getX() + W * 0.04f, b.getY() + H * 0.04f, W * 0.28f, H * 0.92f }, juce::Colour (0xff160C2A), animPhase);
    drawWordmark (g, { b.getX() + W * 0.42f, b.getY() + H * 0.28f, W * 0.5f, H * 0.34f }, juce::Colour (0xff120A20));
    ghosty::logo::detail::caption (g, { b.getX() + W * 0.45f, b.getY() + H * 0.66f, W * 0.5f, H * 0.07f }, "the full synth workstation", lc::slime, H * 0.05f);
    ghosty::logo::detail::caption (g, { b.getX() + W * 0.45f, b.getY() + H * 0.73f, W * 0.5f, H * 0.05f }, "by GhostBlock", juce::Colour (0xffC9C0DE), H * 0.032f);
}
} // namespace ghosty::prologo
