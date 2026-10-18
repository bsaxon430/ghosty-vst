#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// GhostBlock ghost art + palette, shared by both plugins.
namespace ghosty
{
namespace col
{
    const juce::Colour bg     { 0xff0B0C10 };
    const juce::Colour card   { 0xff141621 };
    const juce::Colour card2  { 0xff1b1e2a };
    const juce::Colour white  { 0xffF2F4F5 };
    const juce::Colour slime  { 0xff3CFF8F };
    const juce::Colour ecto   { 0xffA06BFF };
    const juce::Colour pink   { 0xffFF4FD8 };
    const juce::Colour ice    { 0xff45E6FF };
    const juce::Colour gold   { 0xffFFC35A };
    const juce::Colour dim    { 0xff7f8496 };
}

inline juce::Font font (float h, bool bold = true)
{
    return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
}

struct GhostLook
{
    juce::Colour body  { col::white };
    juce::Colour slime { col::slime };
    juce::Colour pupil { col::ecto };
    int eyes  = 0;   // 0 oval, 1 reticle, 2 x, 3 happy, 4 heart, 5 sleepy, 6 big
    int mouth = 0;   // 0 none, 1 smile, 2 grin, 3 o, 4 fang, 5 tongue
    int acc   = 0;   // 0 none, 1 halo, 2 horns, 3 headphones, 4 crown
    bool lines = true;
};

inline const juce::Path& cuteBody()   { static juce::Path p = juce::Drawable::parseSVGPath ("M92 80 A48 48 0 0 1 188 80 V132 Q188 150 174 146 Q164 162 150 150 Q138 166 122 152 Q106 162 96 146 Q80 150 70 138 Q88 128 92 110 Z"); return p; }
inline const juce::Path& cuteSlime()  { static juce::Path p = juce::Drawable::parseSVGPath ("M96 146 Q106 162 122 152 Q138 166 150 150 Q164 162 174 146 Q188 150 188 132 V124 Q176 136 164 128 Q150 140 136 130 Q120 142 106 132 Q96 140 88 130 Q84 142 96 146 Z"); return p; }
inline const juce::Path& cuteDrips()  { static juce::Path p = juce::Drawable::parseSVGPath ("M123 144 Q123 164 124 178 A5 5 0 0 0 132 178 Q133 164 133 144 Z M147 146 Q147 159 148 168 A5 5 0 0 0 156 168 Q157 159 157 146 Z M168 138 Q168 146 169 152 A4 4 0 0 0 175 152 Q176 146 176 138 Z M104 142 Q104 152 105 158 A4 4 0 0 0 111 158 Q112 152 112 142 Z"); return p; }
inline const juce::Path& snipeBody()  { static juce::Path p = juce::Drawable::parseSVGPath ("M90 120 L94 74 L112 46 L146 34 L178 48 L194 78 L194 138 L182 130 L172 150 L158 134 L146 152 L132 136 L118 150 L106 134 L92 144 L76 134 Z"); return p; }
inline const juce::Path& snipeSlime() { static juce::Path p = juce::Drawable::parseSVGPath ("M76 134 L92 144 L106 134 L118 150 L132 136 L146 152 L158 134 L172 150 L182 130 L194 138 L194 124 L182 118 L170 134 L158 120 L146 136 L132 122 L118 136 L106 122 L92 130 L84 124 Z"); return p; }
inline const juce::Path& snipeDrips() { static juce::Path p = juce::Drawable::parseSVGPath ("M126 140 L136 140 L131 178 Z M150 140 L158 140 L154 164 Z M172 132 L180 132 L176 150 Z M104 140 L112 140 L108 158 Z"); return p; }
inline const juce::Path& snipeEyes()  { static juce::Path p = juce::Drawable::parseSVGPath ("M140 76 L160 71 L158 79 L141 82 Z M168 69 L186 64 L185 72 L169 76 Z"); return p; }

// 35 different ghosts, same seeds every time.
inline GhostLook lookFor (int index)
{
    juce::Random r ((juce::int64) (index * 977 + 13));
    static const juce::Colour bodies[]  { col::white, col::white, juce::Colour (0xffE4FFF0), juce::Colour (0xffEEE6FF), juce::Colour (0xffFFE3F5), juce::Colour (0xffE2FAFF), juce::Colour (0xffFFF4D2), juce::Colour (0xff16181f) };
    static const juce::Colour slimes[]  { col::slime, col::slime, col::ecto, col::pink, col::ice, juce::Colour (0xffFFB547), juce::Colour (0xffB6FF3C) };
    static const juce::Colour pupils[]  { col::ecto, col::slime, col::pink, col::ice };
    GhostLook L;
    if (index % 35 == 0) return L;
    L.body  = bodies[r.nextInt (8)];
    L.slime = slimes[r.nextInt (7)];
    L.pupil = pupils[r.nextInt (4)];
    L.eyes  = r.nextInt (7);
    L.mouth = r.nextInt (6);
    L.acc   = r.nextInt (8); if (L.acc > 4) L.acc = 0;
    L.lines = r.nextFloat() < 0.6f;
    return L;
}

inline void drawGhostRaw (juce::Graphics& g, const GhostLook& L, const juce::AffineTransform& t, bool locked, float alpha)
{
    const bool dark = L.body.getBrightness() < 0.3f;
    const auto ink = dark ? col::white : col::bg;
    auto P = [&] (const juce::Path& p) { auto q = p; q.applyTransform (t); return q; };

    if (L.lines || locked)
    {
        const float ys[] { 72.f, 94.f, 116.f };
        const float x1[] { 14.f, 28.f, 8.f }, x2[] { 62.f, 70.f, 58.f };
        for (int i = 0; i < 3; ++i)
        {
            juce::Path ln; ln.startNewSubPath (x1[i], ys[i]); ln.lineTo (x2[i], ys[i]); ln.applyTransform (t);
            g.setColour ((i % 2 ? L.slime : col::ecto).withMultipliedAlpha (alpha));
            g.strokePath (ln, juce::PathStrokeType (locked ? 1.5f : 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }
    g.setColour (L.slime.withMultipliedAlpha (alpha)); g.fillPath (P (locked ? snipeDrips() : cuteDrips()));
    g.setColour (L.body.withMultipliedAlpha (alpha));  g.fillPath (P (locked ? snipeBody() : cuteBody()));
    if (dark) { g.setColour (L.slime.withMultipliedAlpha (alpha)); g.strokePath (P (cuteBody()), juce::PathStrokeType (2.0f)); }
    g.setColour (L.slime.withMultipliedAlpha (alpha)); g.fillPath (P (locked ? snipeSlime() : cuteSlime()));

    g.setColour (ink.withMultipliedAlpha (alpha));
    if (locked)
    {
        g.fillPath (P (snipeEyes()));
        juce::Path ret; ret.addEllipse (148, 58, 32, 32);
        ret.startNewSubPath (164, 50); ret.lineTo (164, 62); ret.startNewSubPath (164, 86); ret.lineTo (164, 98);
        ret.startNewSubPath (140, 74); ret.lineTo (152, 74); ret.startNewSubPath (176, 74); ret.lineTo (188, 74);
        ret.applyTransform (t);
        g.setColour (L.slime.withMultipliedAlpha (alpha)); g.strokePath (ret, juce::PathStrokeType (1.6f));
        return;
    }
    const float e1x = 150, e1y = 80, e2x = 174, e2y = 80;
    juce::Path eyes, strokes;
    switch (L.eyes)
    {
        case 1: eyes.addEllipse (e1x - 8, e1y - 11, 16, 22);
                strokes.addEllipse (e2x - 11, e2y - 11, 22, 22); break;
        case 2: for (auto ex : { e1x, e2x }) { strokes.startNewSubPath (ex - 7, e1y - 7); strokes.lineTo (ex + 7, e1y + 7); strokes.startNewSubPath (ex + 7, e1y - 7); strokes.lineTo (ex - 7, e1y + 7); } break;
        case 3: for (auto ex : { e1x, e2x }) strokes.addCentredArc (ex, e1y + 4, 7, 7, 0, -juce::MathConstants<float>::halfPi * 0.8f, juce::MathConstants<float>::halfPi * 0.8f, true); break;
        case 4: break;
        case 5: for (auto ex : { e1x, e2x }) strokes.addCentredArc (ex, e1y - 2, 7, 7, 0, juce::MathConstants<float>::pi * 0.65f, juce::MathConstants<float>::pi * 1.35f, true); break;
        case 6: eyes.addEllipse (e1x - 12, e1y - 16, 24, 32); eyes.addEllipse (e2x - 10, e2y - 15, 21, 30); break;
        default: eyes.addEllipse (e1x - 8, e1y - 11, 16, 22); eyes.addEllipse (e2x - 7, e2y - 10, 14, 20); break;
    }
    if (! eyes.isEmpty()) { eyes.applyTransform (t); g.fillPath (eyes); }
    if (! strokes.isEmpty()) { strokes.applyTransform (t); g.setColour ((L.eyes == 1 ? L.slime : ink).withMultipliedAlpha (alpha)); g.strokePath (strokes, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); }
    if (L.eyes == 4)
    {
        juce::Path h; h.addEllipse (e1x - 8, e1y - 6, 9, 9); h.addEllipse (e1x - 1, e1y - 6, 9, 9); h.addTriangle (e1x - 8, e1y, e1x + 8, e1y, e1x, e1y + 10);
        juce::Path h2 = h; h2.applyTransform (juce::AffineTransform::translation (24, 0));
        h.addPath (h2); h.applyTransform (t); g.setColour (col::pink.withMultipliedAlpha (alpha)); g.fillPath (h);
    }
    if (L.eyes == 0 || L.eyes == 6)
    {
        juce::Path pu; pu.addEllipse (e1x + 0.5f, e1y - 7, 5, 5); pu.addEllipse (e2x, e2y - 7, 4.4f, 4.4f); pu.applyTransform (t);
        g.setColour (L.pupil.withMultipliedAlpha (alpha)); g.fillPath (pu);
    }
    const float mx = 162, my = 106;
    juce::Path mouth;
    switch (L.mouth)
    {
        case 1: case 4: case 5: mouth.addCentredArc (mx, my - 6, 11, 11, 0, juce::MathConstants<float>::pi * 0.65f, juce::MathConstants<float>::pi * 1.35f, true); break;
        case 2: mouth.addPieSegment (mx - 13, my - 19, 26, 26, juce::MathConstants<float>::halfPi, juce::MathConstants<float>::pi * 1.5f, 0); break;
        case 3: mouth.addEllipse (mx - 5, my - 7, 10, 14); break;
        default: break;
    }
    if (! mouth.isEmpty())
    {
        mouth.applyTransform (t); g.setColour (ink.withMultipliedAlpha (alpha));
        if (L.mouth == 2 || L.mouth == 3) g.fillPath (mouth); else g.strokePath (mouth, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        if (L.mouth == 5) { juce::Path tg; tg.addEllipse (mx - 2, my + 1, 10, 12); tg.applyTransform (t); g.setColour (col::pink.withMultipliedAlpha (alpha)); g.fillPath (tg); }
        if (L.mouth == 4) { juce::Path f; f.addTriangle (mx - 6, my + 3, mx, my + 4, mx - 3, my + 11); f.applyTransform (t); g.setColour (col::white.withMultipliedAlpha (alpha)); g.fillPath (f); }
    }
    juce::Path acc;
    switch (L.acc)
    {
        case 1: acc.addEllipse (134, 14, 56, 14); acc.applyTransform (t); g.setColour (col::gold.withMultipliedAlpha (alpha)); g.strokePath (acc, juce::PathStrokeType (3.0f)); break;
        case 2: acc.addTriangle (128, 44, 140, 38, 128, 18); acc.addTriangle (180, 38, 192, 44, 192, 18); acc.applyTransform (t); g.setColour (col::pink.withMultipliedAlpha (alpha)); g.fillPath (acc); break;
        case 3: { juce::Path band; band.addCentredArc (140, 80, 54, 54, 0, -1.4f, 1.4f, true); band.applyTransform (t); g.setColour (col::ecto.withMultipliedAlpha (alpha)); g.strokePath (band, juce::PathStrokeType (4.0f));
                  acc.addEllipse (80, 66, 14, 28); acc.addEllipse (186, 66, 14, 28); acc.applyTransform (t); g.fillPath (acc); } break;
        case 4: acc.startNewSubPath (140, 40); acc.lineTo (140, 20); acc.lineTo (151, 30); acc.lineTo (162, 16); acc.lineTo (173, 30); acc.lineTo (184, 20); acc.lineTo (184, 40); acc.closeSubPath();
                acc.applyTransform (t); g.setColour (col::gold.withMultipliedAlpha (alpha)); g.fillPath (acc); break;
        default: break;
    }
}

// Draw a ghost centred at (cx, cy). glitch 0..1 adds the GhostBlock RGB-split + slice tearing.
inline void drawGhost (juce::Graphics& g, const GhostLook& L, float cx, float cy, float size, float glitch = 0.0f,
                       bool locked = false, float alpha = 1.0f, bool flip = false)
{
    const float s = size / 118.0f;
    auto base = juce::AffineTransform::translation (-130.0f, -106.0f).scaled (flip ? -s : s, s).translated (cx, cy);
    if (glitch < 0.05f) { drawGhostRaw (g, L, base, locked, alpha); return; }

    auto& rnd = juce::Random::getSystemRandom();
    const float d = glitch * size * 0.12f;
    GhostLook gl = L; gl.body = col::slime; gl.slime = col::slime; gl.pupil = col::slime;
    GhostLook pl = L; pl.body = col::ecto;  pl.slime = col::ecto;  pl.pupil = col::ecto;
    drawGhostRaw (g, gl, base.translated (-d, 0), locked, alpha * 0.55f);
    drawGhostRaw (g, pl, base.translated (d, (rnd.nextFloat() - 0.5f) * d), locked, alpha * 0.55f);

    const int slices = 2 + (int) (glitch * 6.0f);
    const float top = cy - size * 0.75f, h = size * 1.5f / (float) slices;
    for (int k = 0; k < slices; ++k)
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (juce::Rectangle<float> (cx - size * 1.2f, top + (float) k * h, size * 2.4f, h + 1.0f).toNearestInt());
        const float shift = (rnd.nextFloat() - 0.5f) * glitch * size * 0.5f;
        drawGhostRaw (g, L, base.translated (shift, 0), locked, alpha);
    }
}

// Liquid neon drips that rain down a component while you play.
struct NeonRain
{
    struct Drop { float x, y, vy, len, w; juce::Colour c; };
    std::vector<Drop> drops;
    void spawn (juce::Rectangle<float> area, int n)
    {
        auto& r = juce::Random::getSystemRandom();
        static const juce::Colour cs[] { col::slime, col::ecto, col::pink, col::ice };
        for (int i = 0; i < n; ++i)
            drops.push_back ({ area.getX() + r.nextFloat() * area.getWidth(), area.getY() - 10.0f, 2.0f + r.nextFloat() * 4.0f,
                               20.0f + r.nextFloat() * 60.0f, 1.5f + r.nextFloat() * 2.0f, cs[r.nextInt (4)] });
        if (drops.size() > 220) drops.erase (drops.begin(), drops.begin() + (long) (drops.size() - 220));
    }
    void step (float bottom)
    {
        for (auto& d : drops) { d.vy += 0.15f; d.y += d.vy; }
        drops.erase (std::remove_if (drops.begin(), drops.end(), [bottom] (const Drop& d) { return d.y - d.len > bottom; }), drops.end());
    }
    void paint (juce::Graphics& g) const
    {
        for (auto& d : drops)
        {
            juce::ColourGradient gr (d.c.withAlpha (0.0f), d.x, d.y - d.len, d.c.withAlpha (0.8f), d.x, d.y, false);
            g.setGradientFill (gr);
            g.fillRoundedRectangle (d.x - d.w * 0.5f, d.y - d.len, d.w, d.len, d.w * 0.5f);
            g.setColour (d.c.withAlpha (0.95f)); g.fillEllipse (d.x - d.w, d.y - d.w, d.w * 2, d.w * 2);
        }
    }
};

inline float textWidth (const juce::Font& f, const juce::String& s)
{
    juce::GlyphArrangement ga; ga.addLineOfText (f, s, 0.0f, 0.0f);
    return ga.getBoundingBox (0, -1, true).getWidth();
}

// The glitchy "ghosty" wordmark.
inline void drawWordmark (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& tag, float glitch)
{
    const auto f = font (r.getHeight() * 0.8f);
    g.setFont (f);
    auto drawText = [&] (float dx, juce::Colour a, juce::Colour b)
    {
        auto rr = r.translated (dx, 0);
        g.setColour (a); g.drawText ("ghost", rr, juce::Justification::centredLeft);
        const float w = textWidth (f, "ghost");
        g.setColour (b); g.drawText ("y", rr.withTrimmedLeft (w), juce::Justification::centredLeft);
    };
    if (glitch > 0.05f) { drawText (-3.0f * glitch, col::slime.withAlpha (0.7f), col::slime.withAlpha (0.7f)); drawText (3.0f * glitch, col::ecto.withAlpha (0.7f), col::ecto.withAlpha (0.7f)); }
    drawText (0, col::white, col::slime);
    if (tag.isNotEmpty())
    {
        const float w = textWidth (f, "ghosty") + 10.0f;
        auto tr = juce::Rectangle<float> (r.getX() + w, r.getCentreY() - 9, 70, 18);
        g.setColour (col::ecto); g.drawRoundedRectangle (tr, 5, 1.2f);
        g.setFont (font (10.0f)); g.drawText (tag, tr, juce::Justification::centred);
    }
}
} // namespace ghosty
