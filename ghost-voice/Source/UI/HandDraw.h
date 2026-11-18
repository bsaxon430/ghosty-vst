// Ghost Voice - AGPL-3.0-only (see LICENSE, NOTICE.md).
// HAND-DRAWN (build 10, Brandon 2026-11-13: "make everything hand drawn, even the buttons, and draw the ghosts"): the pen strokes
// every part of the window is drawn with. No palette here (SplashArt.h has it), so the ghost art can use it too.
#pragma once
#include <juce_graphics/juce_graphics.h>
#include <cmath>
#include <map>

namespace gv::splash
{
// ================================================================================================ HAND-DRAWN (build 10)
// A path redrawn as if by hand: walked every few pixels, each point nudged by smooth seeded noise, so a line wobbles a little
// the way a pen does. Seeded from where it is: the same thing is drawn the same way every time (no flicker).
inline juce::Path wobble (const juce::Path& src, juce::int64 seed, float amp = 1.1f, float step = 5.0f)
{
    juce::Path out; juce::Random rng (seed);
    const float f1 = 0.035f + rng.nextFloat() * 0.04f, f2 = 0.09f + rng.nextFloat() * 0.08f;
    const float p1 = rng.nextFloat() * 6.28f, p2 = rng.nextFloat() * 6.28f, p3 = rng.nextFloat() * 6.28f, p4 = rng.nextFloat() * 6.28f;
    auto nudge = [&] (float x, float y, float d)
    {
        return juce::Point<float> (x + amp * (0.65f * std::sin (d * f1 + p1) + 0.35f * std::sin (d * f2 + p2)),
                                   y + amp * (0.65f * std::sin (d * f1 + p3) + 0.35f * std::sin (d * f2 + p4)));
    };
    juce::PathFlatteningIterator it (src, {}, 0.6f);
    float d = 0;
    while (it.next())
    {
        if (it.subPathIndex == 0) out.startNewSubPath (nudge (it.x1, it.y1, d));   // (subPathIndex = the segment's index in its sub-path)
        const float len = juce::Point<float> (it.x1, it.y1).getDistanceFrom ({ it.x2, it.y2 });
        const int n = juce::jmax (1, (int) (len / step));
        for (int k = 1; k <= n; ++k)
        {
            const float t = (float) k / (float) n;
            d += len / (float) n;
            out.lineTo (nudge (it.x1 + (it.x2 - it.x1) * t, it.y1 + (it.y2 - it.y1) * t, d));
        }
        if (it.isLastInSubpath() && it.closesSubPath) out.closeSubPath();
    }
    return out;
}

// a rounded box drawn by hand (cached: the same box is wobbled once)
inline const juce::Path& handBox (juce::Rectangle<float> r, float rad, juce::int64 seed, float amp = 1.0f)
{
    static std::map<juce::String, juce::Path> cache;
    const auto key = r.toString() + "|" + juce::String (rad, 1) + "|" + juce::String (seed) + "|" + juce::String (amp, 2);
    auto it = cache.find (key);
    if (it != cache.end()) return it->second;
    if (cache.size() > 1500) cache.clear();
    juce::Path p; p.addRoundedRectangle (r, juce::jmin (rad, r.getHeight() * 0.5f - 0.75f, r.getWidth() * 0.5f - 0.75f));   // (a full pill seams)
    return cache.emplace (key, wobble (p, seed, amp)).first->second;
}

// the ink line: a firm hand-drawn stroke and a lighter second pass a hair off it
inline void inkLine (juce::Graphics& g, const juce::Path& p, juce::int64 seed, juce::Colour ink, float width = 1.3f, float amp = 0.9f)
{
    const juce::PathStrokeType st (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.setColour (ink.withMultipliedAlpha (0.85f)); g.strokePath (wobble (p, seed, amp), st);
    g.setColour (ink.withMultipliedAlpha (0.35f));
    g.strokePath (wobble (p, seed + 17, amp * 1.4f), juce::PathStrokeType (width * 0.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// hatching: parallel pen lines inside a shape, each a slightly different length and angle
inline void hatch (juce::Graphics& g, const juce::Path& shape, juce::int64 seed, juce::Colour c, float spacing = 5.0f, float angle = -0.95f, float width = 0.7f)
{
    juce::Graphics::ScopedSaveState s (g); g.reduceClipRegion (shape);
    const auto b = shape.getBounds().expanded (6);
    juce::Random rng (seed);
    const float dx = std::cos (angle), dy = std::sin (angle), span = b.getWidth() + b.getHeight();
    for (float o = -span; o < span; o += spacing * (0.8f + 0.4f * rng.nextFloat()))
    {
        const float cx = b.getCentreX() - dy * o, cy = b.getCentreY() + dx * o;
        const float a = 0.02f * (rng.nextFloat() - 0.5f);
        juce::Path l; l.startNewSubPath (cx - (dx - a) * span, cy - (dy + a) * span); l.lineTo (cx + (dx + a) * span, cy + (dy - a) * span);
        g.setColour (c.withMultipliedAlpha (0.6f + 0.4f * rng.nextFloat())); g.strokePath (l, juce::PathStrokeType (width * (0.7f + 0.6f * rng.nextFloat())));
    }
}

// watercolour: the colour laid in twice, the second wash bleeding a little past the line
inline void watercolour (juce::Graphics& g, const juce::Path& p, juce::Colour c, juce::int64 seed, float alpha = 1.0f)
{
    juce::Random rng (seed);
    g.setColour (c.withMultipliedAlpha (0.35f * alpha)); g.fillPath (p, juce::AffineTransform::translation (rng.nextFloat() * 2.0f - 0.6f, rng.nextFloat() * 2.0f - 0.4f));
    g.setColour (c.withMultipliedAlpha (0.78f * alpha)); g.fillPath (p);
}

}
