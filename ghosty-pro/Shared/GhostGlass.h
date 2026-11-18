#pragma once
// GhostBlock "liquid glass" primitives (the GHOST look; first used by Ghost Slice, any plugin may adopt it).
//
// A pale aurora backdrop (lavender -> mint with five soft colour orbs) under translucent frosted panels: a soft layered drop shadow,
// a white vertical body gradient, an inner edge glow, a specular rim (bright top-left, soft bottom-right), a faint violet fringe just
// inside the lower edge and a top gloss highlight. Pill buttons, glossy capsules, glass slider tracks with a violet -> teal fill and
// round glossy thumbs, recessed value boxes.
//
// Own drawing code (juce::Graphics only): no third-party code, no bitmaps. Message thread only, never the audio thread.
// PERFORMANCE: no blur filters. The backdrop is rendered once per size into a cached image (Backdrop), big panels into a cached image
// per size (PanelCache); small controls draw a handful of fills each.
#include <juce_graphics/juce_graphics.h>
#include <map>
#include <tuple>

namespace ghosty::glass {

// ---- tokens
inline juce::Colour ink()         { return juce::Colour (0xff2a2250); }   // deep indigo text / ink
inline juce::Colour ink2()        { return juce::Colour (0xff463e6c); }   // secondary text (>= 4.5:1 on the glass at its darkest)
inline juce::Colour violet()      { return juce::Colour (0xff8b5cf6); }
inline juce::Colour violetDeep()  { return juce::Colour (0xff5530c8); }   // active pill tint: white text reads on it
inline juce::Colour teal()        { return juce::Colour (0xff22d3b6); }
inline juce::Colour pink()        { return juce::Colour (0xffff4da6); }
inline juce::Colour shadowTint()  { return juce::Colour (0xff3a2e6b); }
inline juce::Colour fringe()      { return juce::Colour (0x40a06bff); }
inline juce::Colour auroraTop()   { return juce::Colour (0xffefe9ff); }   // lavender
inline juce::Colour auroraBottom(){ return juce::Colour (0xffe3f6f1); }   // mint

// ---- the backdrop
inline void paintAurora (juce::Graphics& g, float w, float h)
{
    using C = juce::Colour;
    g.setGradientFill (juce::ColourGradient (auroraTop(), 0, 0, auroraBottom(), w, h, false)); g.fillRect (0.0f, 0.0f, w, h);
    const float sx = w / 1280.0f, sy = h / 780.0f, sr = std::sqrt (sx * sy);
    struct Orb { float x, y, r; juce::uint32 c; };
    const Orb orbs[] { { 140, 90, 330, 0xffc9b2ff }, { 1120, 130, 300, 0xff9fe8d6 }, { 820, 640, 380, 0xffffc9e6 }, { 260, 700, 300, 0xffb6d4ff }, { 640, 360, 240, 0xffd9c8ff } };
    for (auto& o : orbs)
    {
        const float x = o.x * sx, y = o.y * sy, r = o.r * sr;
        g.setGradientFill (juce::ColourGradient (C (o.c).withAlpha (0.85f), x, y, C (o.c).withAlpha (0.0f), x + r, y, true));
        g.fillRect (0.0f, 0.0f, w, h);
    }
}

// the aurora rendered once per (size, pixel scale); draw() blits it
class Backdrop
{
public:
    const juce::Image& get (int w, int h, float scale)
    {
        const int pw = juce::jmax (1, juce::roundToInt ((float) w * scale)), ph = juce::jmax (1, juce::roundToInt ((float) h * scale));
        if (img.isNull() || img.getWidth() != pw || img.getHeight() != ph)
        {
            img = juce::Image (juce::Image::RGB, pw, ph, false, juce::SoftwareImageType());
            juce::Graphics ig (img); ig.addTransform (juce::AffineTransform::scale ((float) pw / (float) w, (float) ph / (float) h));
            paintAurora (ig, (float) w, (float) h);
            ++builds;
        }
        return img;
    }
    void draw (juce::Graphics& g, int w, int h)
    {
        const float scale = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        const auto& i = get (w, h, scale);
        g.drawImageTransformed (i, juce::AffineTransform::scale ((float) w / (float) i.getWidth(), (float) h / (float) i.getHeight()));
    }
    int builds = 0;     // (tests: rebuilt only on resize)
private:
    juce::Image img;
};

// ---- glass primitives
// a soft drop shadow: `layers` translucent rounded rects growing outwards, offset down a little
inline void shadow (juce::Graphics& g, juce::Rectangle<float> r, float rad, float spread, float alpha = 1.0f, int layers = 8)
{
    for (int i = layers; i >= 1; --i)
    {
        const float e = spread * (float) i / (float) layers;
        g.setColour (shadowTint().withAlpha (alpha * 0.035f * 8.0f / (float) layers));
        g.fillRoundedRectangle (r.expanded (e).translated (0, spread * 0.45f), rad + e);
    }
}

// the glass body without its shadow (body = the tint's mean alpha; gloss scales the inner glow + top highlight: < 1 where text sits on tinted glass)
inline void body (juce::Graphics& g, juce::Rectangle<float> r, float rad, juce::Colour tint = juce::Colours::white, float bodyAlpha = 0.38f, float gloss = 1.0f)
{
    rad = juce::jmin (rad, r.getHeight() * 0.5f, r.getWidth() * 0.5f);
    g.setGradientFill (juce::ColourGradient (tint.withAlpha (juce::jlimit (0.0f, 1.0f, bodyAlpha + 0.22f)), r.getX(), r.getY(),
                                             tint.withAlpha (juce::jlimit (0.0f, 1.0f, bodyAlpha - 0.10f)), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, rad);
    // inner edge glow (brighter towards the edges)
    g.setGradientFill (juce::ColourGradient (juce::Colour (0x00ffffff), r.getCentreX(), r.getCentreY(), juce::Colours::white.withAlpha (0.333f * gloss), r.getX(), r.getY(), true));
    g.fillRoundedRectangle (r, rad);
    // specular rim: bright top-left, soft bottom-right; the violet fringe just inside the lower edge
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xe6ffffff), r.getX(), r.getY(), juce::Colour (0x30ffffff), r.getRight(), r.getBottom(), false));
    g.drawRoundedRectangle (r.reduced (0.75f), juce::jmax (0.0f, rad - 0.75f), 1.5f);
    if (r.getHeight() > 10.0f && r.getWidth() > 10.0f)
    {
        g.setColour (fringe()); g.drawRoundedRectangle (r.reduced (2.2f).translated (0, 0.8f), juce::jmax (0.0f, rad - 2.0f), 0.8f);
        // top gloss
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.565f * gloss), r.getX() + rad, r.getY() + 2, juce::Colour (0x00ffffff), r.getX() + rad, r.getY() + r.getHeight() * 0.45f, false));
        juce::Path hl; const float hr = rad * 0.8f;
        hl.addRoundedRectangle (r.getX() + 3, r.getY() + 2.5f, r.getWidth() - 6, r.getHeight() * 0.42f, hr, hr, true, true, false, false);
        g.fillPath (hl);
    }
}

// the full recipe: shadow + body (pressed = a tighter shadow)
inline void panel (juce::Graphics& g, juce::Rectangle<float> r, float rad, juce::Colour tint = juce::Colours::white, float bodyAlpha = 0.38f,
                   bool pressed = false, float spread = -1.0f)
{
    if (spread < 0) spread = pressed ? 6.0f : 14.0f;
    if (spread > 0) shadow (g, r, juce::jmin (rad, r.getHeight() * 0.5f), spread, 1.0f, spread > 8.0f ? 8 : 4);
    body (g, r, rad, tint, bodyAlpha);
}

// big panels: the recipe rendered into an image per (size, radius, body, spread, scale), blitted afterwards
class PanelCache
{
public:
    void draw (juce::Graphics& g, juce::Rectangle<float> r, float rad, float bodyAlpha = 0.38f, float spread = 14.0f)
    {
        const float scale = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        const int m = (int) std::ceil (spread * 1.6f) + 2;
        const int w = juce::roundToInt (r.getWidth()), h = juce::roundToInt (r.getHeight());
        if (w <= 0 || h <= 0) return;
        const Key k { w, h, juce::roundToInt (rad * 10), juce::roundToInt (bodyAlpha * 1000), juce::roundToInt (spread * 10), juce::roundToInt (scale * 100) };
        auto it = map.find (k);
        if (it == map.end())
        {
            if (map.size() > 24) map.clear();
            const int pw = juce::roundToInt ((float) (w + 2 * m) * scale), ph = juce::roundToInt ((float) (h + 2 * m) * scale);
            juce::Image img (juce::Image::ARGB, juce::jmax (1, pw), juce::jmax (1, ph), true, juce::SoftwareImageType());
            {
                juce::Graphics ig (img); ig.addTransform (juce::AffineTransform::scale (scale));
                panel (ig, juce::Rectangle<float> ((float) m, (float) m, (float) w, (float) h), rad, juce::Colours::white, bodyAlpha, false, spread);
            }
            it = map.emplace (k, img).first; ++builds;
        }
        g.drawImageTransformed (it->second, juce::AffineTransform::scale (1.0f / scale).translated (std::round (r.getX()) - (float) m, std::round (r.getY()) - (float) m));
    }
    int builds = 0;
private:
    using Key = std::tuple<int, int, int, int, int, int>;
    std::map<Key, juce::Image> map;
};

// a pill button inside its own bounds (the shadow stays inside: buttons sit close together). on = violet glass, white text
inline juce::Rectangle<float> pillBody (juce::Rectangle<float> bounds) { return bounds.reduced (1.5f, 1.0f).withTrimmedBottom (2.0f); }
inline void pill (juce::Graphics& g, juce::Rectangle<float> bounds, bool on, bool over, bool down, bool enabled = true)
{
    auto r = pillBody (bounds);
    if (down) r = r.translated (0, 0.6f);
    const float rad = r.getHeight() * 0.5f;
    shadow (g, r, rad, down ? 1.5f : 2.6f, enabled ? 1.3f : 0.6f, 3);
    if (on) body (g, r, rad, violetDeep(), over ? 0.94f : 0.90f, 0.3f);
    else    body (g, r, rad, juce::Colours::white, over ? 0.52f : 0.40f);
    if (! enabled) { g.setColour (juce::Colours::white.withAlpha (0.28f)); g.fillRoundedRectangle (r, rad); }
}
// a glossy note capsule (violet; selected = teal)
inline void capsule (juce::Graphics& g, juce::Rectangle<float> r, bool selected, float confidence = 1.0f)
{
    const float rad = r.getHeight() * 0.5f;
    if (r.getHeight() >= 8.0f) shadow (g, r, rad, 3.0f, 0.9f, 3);
    body (g, r, rad, selected ? teal() : violet(), 0.40f + 0.22f * juce::jlimit (0.0f, 1.0f, confidence));
}
// the slider: a glass track, the violet -> teal fill between from and to (x), a glossy round thumb
inline void track (juce::Graphics& g, juce::Rectangle<float> tr, float from, float to, bool enabled = true)
{
    const float rad = tr.getHeight() * 0.5f;
    g.setColour (juce::Colour (0x30443a7a)); g.fillRoundedRectangle (tr, rad);
    g.setColour (juce::Colour (0x55ffffff)); g.drawRoundedRectangle (tr, rad, 1.0f);
    const float a = juce::jmin (from, to), b = juce::jmax (from, to);
    if (b - a > 0.5f)
    {
        if (enabled) g.setGradientFill (juce::ColourGradient (violet(), tr.getX(), 0, teal(), tr.getRight(), 0, false));
        else g.setColour (juce::Colour (0x60443a7a));
        g.fillRoundedRectangle (juce::Rectangle<float>::leftTopRightBottom (a, tr.getY(), b, tr.getBottom()), rad);
    }
}
inline void thumb (juce::Graphics& g, juce::Point<float> c, float d)
{
    const juce::Rectangle<float> th (c.x - d * 0.5f, c.y - d * 0.5f, d, d);
    shadow (g, th, d * 0.5f, juce::jmax (2.0f, d * 0.2f), 1.0f, 4);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white, c.x - d * 0.2f, th.getY() + 2, juce::Colour (0xffdcd4f5), c.x, th.getBottom(), false)); g.fillEllipse (th);
    g.setColour (juce::Colour (0xa0ffffff)); g.drawEllipse (th.reduced (0.7f), 1.4f);
    g.setColour (juce::Colour (0x60a06bff)); g.drawEllipse (th.reduced (d * 0.15f), 0.8f);
}
// a recessed value box / readout well
inline void recessed (juce::Graphics& g, juce::Rectangle<float> r, float rad = -1.0f)
{
    if (rad < 0) rad = r.getHeight() * 0.5f;
    rad = juce::jmin (rad, r.getHeight() * 0.5f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0x1e3a2e6b), r.getX(), r.getY(), juce::Colour (0x00ffffff), r.getX(), r.getY() + r.getHeight() * 0.5f, false));
    g.fillRoundedRectangle (r, rad);
    g.setColour (juce::Colours::white.withAlpha (0.42f)); g.fillRoundedRectangle (r.reduced (1.0f).withTrimmedTop (1.0f), juce::jmax (0.0f, rad - 1.0f));
    g.setColour (juce::Colour (0x40463e6c)); g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.0f);
    g.setColour (juce::Colour (0xb0ffffff)); g.drawLine (r.getX() + rad, r.getBottom() - 0.5f, r.getRight() - rad, r.getBottom() - 0.5f, 1.0f);
}
// a glass card for popups / tooltips / overlays (opaque enough to read on anything)
inline void card (juce::Graphics& g, juce::Rectangle<float> r, float rad)
{
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff8f6ff), r.getX(), r.getY(), juce::Colour (0xffeef7f4), r.getRight(), r.getBottom(), false));
    g.fillRoundedRectangle (r, rad);
    body (g, r, rad, juce::Colours::white, 0.30f);
}

} // namespace ghosty::glass
