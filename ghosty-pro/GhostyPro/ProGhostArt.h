#pragma once
// Ghosty Pro round 3: the four XY-pad ghost characters, drawn procedurally (juce::Path + gradients + glow layers, no bitmaps).
//
//   HEAT   dark soft body, flame crown + flame-tongue hem, amber eyes, embers   (drive / saturation)   X = DRIVE, Y = SUB
//   SPACE  pale lavender body, wispy trailing tail, nebula, stars around it     (reverb / delay)       X = ECHO,  Y = REVERB
//   MORPH  split yellow / hot-pink liquid body, dripping crown, falling drips    (morphing)             X = POS,   Y = DETUNE
//   TONE   clean white classic ghost, cyan neon rim, soundwave lines            (filter / brightness)  X = CUTOFF, Y = RES
//
// Drawn in the style of the piano GHOST CURSOR (ProKeysExtras.h): its body, hem, drips and face (two tall oval eyes with pupils), flat
// fills and a thin rim, in each character's own colours with its own extras. Like the cursor they look where they move: the pad
// (Shared/GhostUI.h) gives each ghost a smoothed gaze from its motion and a drag glitch from the drag speed (Pose::gaze / dragGlitch).
// Parameters drive the art: HEAT more flame + embers with drive, SPACE more stars with reverb (echo copies with echo),
// MORPH the split line and drip lengths follow POS, TONE the wave amplitude follows the cutoff. The glow grows while the ghost
// is dragged and when its parameters are high. Idle animation (flicker / twinkle / drip / wave / blink) advances in 24 steps per
// second from the pad's clock, no extra timer. The art adapts to the backdrop: on a light backdrop the rims and glows deepen
// so they still read (>= 3:1 rim contrast, see ProTests ghosts).
//
// Message thread only, paint-time only, no allocation beyond the juce::Path objects of one paint.
// The XY pad opts in through XYGhostPad::ghostPainter (Shared/GhostUI.h); other plugins keep the classic Shared/GhostArt look.
#include <juce_gui_basics/juce_gui_basics.h>
#include "GhostUI.h"
#include "GhostThemes.h"
#include <cmath>
#include <map>
#include <tuple>

namespace proghost
{
enum class Kind { Heat, Space, Morph, Tone, Classic };

inline Kind kindFor (const juce::String& name)
{
    if (name == "HEAT")  return Kind::Heat;
    if (name == "SPACE") return Kind::Space;
    if (name.startsWith ("MORPH")) return Kind::Morph;   // STUDIO's pad: "MORPH 2" / "MORPH 1+2" (the OSC switch) are MORPH too
    if (name == "TONE")  return Kind::Tone;
    return Kind::Classic;
}

struct Pose
{
    float t = 0;            // seconds (the pad's clock)
    float x = 0, y = 0;     // the ghost's two parameters, normalised 0..1
    float level = 0;        // output level 0..1
    float glitch = 0;       // the pad's glitch 0..1 (RGB split)
    bool held = false, flip = false;
    bool title = true;      // write the ghost's name on its body
    juce::String label;     // the name written on it (empty = the character's own: HEAT / SPACE / MORPH / TONE); a second word
                            // ("MORPH 2", "MORPH 1+2") goes on a smaller second line, on the hem
    juce::Point<float> gaze;   // where it looks, screen space, length 0..1 (0 = the resting look): eyes turn, the head leans
    float dragGlitch = 0;      // the drag glitch 0..1 (slices + scanlines, see drawGhost)
    juce::Colour backdrop { 0xff06070a };
};

inline bool isLightBackdrop (juce::Colour bg) { return ghosty::themes::relLum (bg) > 0.25; }

// the colour of the neon rim / glow of each character (dark backdrop vs light backdrop)
inline juce::Colour rimColour (Kind k, bool light)
{
    switch (k)
    {
        case Kind::Heat:  return light ? juce::Colour (0xffB83A00) : juce::Colour (0xffFF9A2E);
        case Kind::Space: return light ? juce::Colour (0xff5B2FD6) : juce::Colour (0xffC7A6FF);
        case Kind::Morph: return light ? juce::Colour (0xffB0127A) : juce::Colour (0xffFF6AD0);
        case Kind::Tone:  return light ? juce::Colour (0xff06707F) : juce::Colour (0xff3FE6FF);
        case Kind::Classic:
        default:          return light ? juce::Colour (0xff5B2FD6) : juce::Colour (0xffA06BFF);
    }
}
inline juce::Colour glowColour (Kind k, bool light)
{
    switch (k)
    {
        case Kind::Heat:  return light ? juce::Colour (0xffE05A00) : juce::Colour (0xffFF7A1A);
        case Kind::Space: return light ? juce::Colour (0xff5226D8) : juce::Colour (0xffA070FF);
        case Kind::Morph: return light ? juce::Colour (0xffD0208F) : juce::Colour (0xffFF4FC0);
        case Kind::Tone:  return light ? juce::Colour (0xff0A8FA3) : juce::Colour (0xff22E6FF);
        case Kind::Classic:
        default:          return light ? juce::Colour (0xff6A3AF0) : juce::Colour (0xffA06BFF);
    }
}

// how hard the ghost glows, 0..1: its "main" parameter, dragging and the output level
inline float intensity (Kind k, const Pose& p)
{
    const float main = k == Kind::Space ? juce::jmax (p.y, p.x * 0.7f) : p.x;
    return juce::jlimit (0.0f, 1.0f, 0.18f + 0.42f * main + (p.held ? 0.32f : 0.0f) + 0.2f * p.level);
}

namespace detail
{


    inline float frac (float v) { return v - std::floor (v); }
    inline float wob (float t, float seed) { return 0.6f * std::sin (t * 1.7f + seed) + 0.4f * std::sin (t * 2.9f + seed * 2.3f); }

    struct Space
    {
        juce::AffineTransform T; float R;
        Space (float cx, float cy, float r, bool flip) : T (juce::AffineTransform::scale (flip ? -r : r, r).translated (cx, cy)), R (r) {}
        juce::Point<float> p (float x, float y) const { T.transformPoint (x, y); return { x, y }; }
        juce::Path& map (juce::Path& path) const { path.applyTransform (T); return path; }
        juce::ColourGradient lin (juce::Colour a, float x1, float y1, juce::Colour b, float x2, float y2) const
        { auto A = p (x1, y1), B = p (x2, y2); return juce::ColourGradient (a, A.x, A.y, b, B.x, B.y, false); }
        juce::ColourGradient rad (juce::Colour a, float x, float y, juce::Colour b, float r) const
        { auto A = p (x, y); return juce::ColourGradient (a, A.x, A.y, b, A.x + r * R, A.y, true); }
        void ellipse (juce::Path& path, float x, float y, float w, float h) const
        { const auto c = p (x, y); path.addEllipse (c.x - w * R * 0.5f, c.y - h * R * 0.5f, w * R, h * R); }
    };

    // a soft round glow of radius r px (colour c1 on the left blending into c2 on the right) at opacity level q / 24, pre-rendered once
    // (ARGB, premultiplied) and cached per (colours, radius, level); drawn with a plain drawImageAt (no opacity: the cheapest blend
    // the software renderer has), much cheaper than a radial gradient fill per ghost per frame. The faint outer 15 % of the falloff is
    // cropped. Message thread only.
    inline const juce::Image& glowSprite (juce::Colour c1, juce::Colour c2, int r, int q)
    {
        static std::map<std::tuple<juce::uint32, juce::uint32, int, int>, juce::Image> cache;
        r = juce::jlimit (2, 400, r); q = juce::jlimit (1, 24, q);
        const auto key = std::make_tuple (c1.getARGB() | 0xff000000u, c2.getARGB() | 0xff000000u, r, q);
        if (auto it = cache.find (key); it != cache.end()) return it->second;
        if (cache.size() > 160) cache.clear();
        juce::Image img (juce::Image::ARGB, 2 * r + 1, 2 * r + 1, true, juce::SoftwareImageType());
        {
            juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
            const float edge = 0.15f, peak = (float) q / 24.0f;
            for (int y = 0; y <= 2 * r; ++y)
                for (int x = 0; x <= 2 * r; ++x)
                {
                    const float d = std::hypot ((float) (x - r), (float) (y - r)) / (float) r * 0.85f;
                    const float a = d >= 0.85f ? 0.0f : std::pow ((1.0f - d - edge) / (1.0f - edge), 1.05f);
                    const float mix = juce::jlimit (0.0f, 1.0f, 0.5f + (float) (x - r) / (float) r * 0.8f);
                    bd.setPixelColour (x, y, c1.interpolatedWith (c2, mix).withAlpha (juce::jlimit (0.0f, 1.0f, a * peak)));
                }
        }
        return cache.emplace (key, img).first->second;
    }
    // radius = the visible radius of the glow, alpha = its opacity at the centre
    inline void glowAt (juce::Graphics& g, juce::Colour c1, juce::Colour c2, float alpha, float x, float y, float radius)
    {
        const int r = juce::roundToInt (radius * 0.85f);
        g.drawImageAt (glowSprite (c1, c2, r, juce::roundToInt (alpha * 24.0f)), juce::roundToInt (x) - r, juce::roundToInt (y) - r);
    }
    inline void glowAt (juce::Graphics& g, juce::Colour c, float x, float y, float radius) { glowAt (g, c, c, c.getFloatAlpha(), x, y, radius); }

    // a curved path as straight lines (flattened once; it is then filled several times: halo, rim, body, RGB split)
    inline juce::Path flattened (const juce::Path& src, float tolerance = 0.6f)
    {
        juce::Path out;
        juce::PathFlatteningIterator it (src, {}, tolerance);
        bool open = false; float lx = 0, ly = 0;
        while (it.next())
        {
            if (! open || std::abs (it.x1 - lx) > 1.0e-4f || std::abs (it.y1 - ly) > 1.0e-4f) { out.startNewSubPath (it.x1, it.y1); open = true; }
            out.lineTo (it.x2, it.y2); lx = it.x2; ly = it.y2;
            if (it.closesSubPath) { out.closeSubPath(); open = false; }
        }
        return out;
    }

    inline juce::PathStrokeType stroke (float w) { return juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded); }

    // a four-point sparkle star
    inline void addSparkle (juce::Path& path, juce::Point<float> c, float r)
    {
        const float q = r * 0.28f;
        path.startNewSubPath (c.x, c.y - r);
        path.quadraticTo (c.x + q, c.y - q, c.x + r, c.y);
        path.quadraticTo (c.x + q, c.y + q, c.x, c.y + r);
        path.quadraticTo (c.x - q, c.y + q, c.x - r, c.y);
        path.quadraticTo (c.x - q, c.y - q, c.x, c.y - r);
        path.closeSubPath();
    }

    // ------------------------------------------------------------ the piano GHOST CURSOR's shape and face (Shared/GhostArt.h cuteBody /
    // cuteSlime / cuteDrips, ProKeysExtras.h eyes / pupils), mapped from its 280-unit drawing space into the unit space used here
    // (head centre (140, 80) radius 48 -> (0, -0.2) radius 0.7). The paths are flattened once (static), every paint only transforms them.
    namespace cute
    {
        constexpr float k = 0.7f / 48.0f;
        inline juce::Point<float> u (float x, float y) { return { (x - 140.0f) * k, (y - 80.0f) * k - 0.2f }; }
        inline juce::Path unit (const juce::Path& src)
        {
            auto p = src; p.applyTransform (juce::AffineTransform::translation (-140.0f, -80.0f).scaled (k).translated (0.0f, -0.2f));
            return flattened (p, 0.004f);
        }
        inline const juce::Path& body()  { static const juce::Path p = unit (ghosty::cuteBody());  return p; }
        inline const juce::Path& hem()   { static const juce::Path p = unit (ghosty::cuteSlime()); return p; }
        inline const juce::Path& drips() { static const juce::Path p = unit (ghosty::cuteDrips()); return p; }
        constexpr float dripTop = 0.70f;     // where the drips leave the hem (unit y)
    }

    struct Face
    {
        juce::Colour eye, pupil;     // the cursor's dark oval eyes + coloured pupils (HEAT: amber eyes, dark pupils)
        float blinkSeed = 0;
    };

    // the cursor ghost's face: two tall oval eyes with a pupil each. gaze (-1..1 each way, screen space) moves the pupils to the edge of the
    // eye the ghost looks to (and the eyes a little), smoothly; gaze 0 = the cursor's own resting look (up-right). flip = the drawing is mirrored.
    inline void drawFace (juce::Graphics& g, const Space& S, const Face& F, float t, juce::Point<float> gaze, bool flip)
    {
        const bool blink = frac ((t + F.blinkSeed) / 4.3f) < 0.035f;
        const float gm = juce::jmin (1.0f, gaze.getDistanceFromOrigin());
        // in unit space (the drawing may be mirrored: the screen direction flips with it)
        const juce::Point<float> dir = gm > 1.0e-4f ? juce::Point<float> ((flip ? -gaze.x : gaze.x) / gm, gaze.y / gm) : juce::Point<float>();
        struct E { float cx, cy, rx, ry, px, py, pr; };
        static const E eyes[] { { 150.0f, 80.0f, 8.0f, 11.0f, 153.0f, 75.5f, 3.0f }, { 174.0f, 80.0f, 7.0f, 10.0f, 176.2f, 75.2f, 2.7f } };
        juce::Path e, pu;
        for (auto& E : eyes)
        {
            const float ex = E.cx + dir.x * gm * 2.4f, ey = E.cy + dir.y * gm * 1.6f;
            if (blink)
            {
                auto a = S.p (cute::u (ex - E.rx, ey).x, cute::u (ex - E.rx, ey).y), c = S.p (cute::u (ex, ey + 4.0f).x, cute::u (ex, ey + 4.0f).y), d = S.p (cute::u (ex + E.rx, ey).x, cute::u (ex + E.rx, ey).y);
                e.startNewSubPath (a); e.quadraticTo (c, d);
                continue;
            }
            const auto c = cute::u (ex, ey);
            S.ellipse (e, c.x, c.y, 2.0f * E.rx * cute::k, 2.0f * E.ry * cute::k);
            // the pupil: from its resting spot towards the rim of the eye in the gaze direction
            // (about as far sideways as up / down, so the diagonals read as diagonals in the tall eyes)
            const float tx = ex + dir.x * (E.rx - E.pr), ty = ey + dir.y * (E.ry - E.pr - 2.0f);
            const float rx0 = E.px + dir.x * gm * 2.4f, ry0 = E.py + dir.y * gm * 1.6f;
            const float px = rx0 + (tx - rx0) * gm, py = ry0 + (ty - ry0) * gm;
            const auto pc = cute::u (px, py);
            S.ellipse (pu, pc.x, pc.y, 2.0f * E.pr * cute::k, 2.0f * E.pr * cute::k);
        }
        g.setColour (F.eye);
        if (blink) { g.strokePath (e, stroke (juce::jmax (1.1f, 0.05f * S.R))); return; }
        g.fillPath (e);
        g.setColour (F.pupil); g.fillPath (pu);
    }

    // flame tongues rising from the head arc (centre (0, -0.18), radius r0); scale = tongue height
    inline juce::Path flameCrown (float t, float scale, float seed)
    {
        juce::Path p;
        const float r0 = 0.66f, cy = -0.18f;
        const int n = 7;
        const float a0 = juce::MathConstants<float>::pi * 0.97f, a1 = juce::MathConstants<float>::pi * 2.03f;   // left .. right over the top, down the sides
        auto at = [&] (float a, float r) { return juce::Point<float> (std::cos (a) * r, cy + std::sin (a) * r); };
        p.startNewSubPath (at (a0, r0 - 0.05f));
        const float span = (a1 - a0) / (float) n;
        for (int i = 0; i < n; ++i)
        {
            const float aL = a0 + span * (float) i, aR = aL + span;
            const float centre = std::abs ((float) i - (float) (n - 1) * 0.5f) / ((float) (n - 1) * 0.5f);   // 0 middle .. 1 ends
            const float flick = 1.0f + 0.22f * std::sin (t * 9.0f + (float) i * 1.7f + seed) + 0.12f * std::sin (t * 15.3f + (float) i * 2.9f + seed);
            const float h = scale * (1.0f - 0.45f * centre) * flick;
            const float sway = 0.12f * span * std::sin (t * 4.3f + (float) i + seed);
            const auto tip = at ((aL + aR) * 0.5f + sway + span * 0.15f, r0 + h);
            p.quadraticTo (at (aL + span * 0.15f, r0 + h * 0.55f), tip);
            p.quadraticTo (at (aR - span * 0.25f, r0 + h * 0.35f), at (aR, r0 + 0.03f));
        }
        p.lineTo (0.0f, cy + 0.2f);
        p.closeSubPath();
        return p;
    }


    // the dripping crown: five liquid prongs with a ball on top (same winding as the body, so a union fill has no holes)
    inline juce::Path morphCrown (float t, float morph)
    {
        juce::Path p;
        const float cy = -0.16f, r0 = 0.66f;
        for (int i = 0; i < 5; ++i)
        {
            const float a = juce::MathConstants<float>::pi * (1.17f + 0.165f * (float) i);
            const float len = 0.17f + 0.07f * (float) (i % 2 == 0) + 0.035f * std::sin (t * 2.4f + (float) i * 1.1f) + 0.05f * morph;
            const float ca = std::cos (a), sa = std::sin (a), nx = sa, ny = -ca;
            const float bx = ca * (r0 - 0.1f), by = cy + sa * (r0 - 0.1f), tx = ca * (r0 + len), ty = cy + sa * (r0 + len);
            p.startNewSubPath (bx + nx * 0.1f, by + ny * 0.1f);
            p.quadraticTo (tx + nx * 0.02f - ca * 0.06f, ty + ny * 0.02f - sa * 0.06f, tx + nx * 0.028f, ty + ny * 0.028f);
            p.lineTo (tx - nx * 0.028f, ty - ny * 0.028f);
            p.quadraticTo (tx - nx * 0.02f - ca * 0.06f, ty - ny * 0.02f - sa * 0.06f, bx - nx * 0.1f, by - ny * 0.1f);
            p.closeSubPath();
            const float br = 0.07f + 0.012f * (float) (i % 2);
            p.addEllipse (tx - br, ty - br, br * 2.0f, br * 2.0f);
        }
        return p;
    }
} // namespace detail

inline const char* titleOf (Kind k) { return k == Kind::Heat ? "HEAT" : k == Kind::Space ? "SPACE" : k == Kind::Morph ? "MORPH" : k == Kind::Tone ? "TONE" : ""; }

// the body silhouette in pad coordinates, exactly as drawGhost fills it (the tests check the title sits inside it)
// (the cursor ghost's body, leaning a few degrees with the gaze)
inline detail::Space spaceFor (const Pose& pose, float cx, float cy, float R)
{
    const auto lean = juce::Point<float> (pose.gaze.x * 0.05f * R, pose.gaze.y * 0.04f * R);
    detail::Space S (cx + lean.x, cy + lean.y, R, pose.flip);
    if (pose.gaze.x != 0.0f) S.T = S.T.rotated (pose.gaze.x * 0.12f, cx + lean.x, cy + lean.y + 0.28f * R);   // ~7 degrees at full gaze
    return S;
}
inline juce::Path bodyPathFor (Kind, const Pose& pose, float cx, float cy, float R)
{
    juce::Path body (detail::cute::body());
    body.applyTransform (spaceFor (pose, cx, cy, R).T);
    return body;
}

// ---- the title written across the lower body: small bold letter-spaced caps in the character's own (deepened) glow colour with a
// thin outline, readable on its body (>= 3:1, ProTests ghosts). Pre-rendered once per (character, size) and cached (message thread only).
inline juce::Colour titleColour (Kind k)
{
    switch (k)
    {
        case Kind::Heat:  return juce::Colour (0xffFFB347);   // amber flame on the dark body
        case Kind::Space: return juce::Colour (0xff4A22C8);   // deep violet on the pale body
        case Kind::Morph: return juce::Colour (0xff3A0A2E);   // deep plum on yellow / pink
        case Kind::Tone:  return juce::Colour (0xff05606E);   // deep teal on white
        case Kind::Classic:
        default:          return juce::Colours::white;
    }
}
inline juce::Colour titleOutline (Kind k) { return k == Kind::Heat ? juce::Colour (0xff120C10) : juce::Colours::white.withAlpha (0.8f); }
inline float titleCentreY (Kind) { return 0.28f; }    // between the eyes and the hem of the cursor-style body
// the first line of a label (the whole character name when the label is empty), and its second line ("2", "1+2"; may be empty)
inline juce::String titleLine1 (Kind k, const juce::String& label) { return label.isEmpty() ? juce::String (titleOf (k)) : label.upToFirstOccurrenceOf (" ", false, false); }
inline juce::String titleLine2 (const juce::String& label) { return label.fromFirstOccurrenceOf (" ", false, false).trim(); }
inline float titlePx (Kind k, float R, const juce::String& text = {})
{
    static std::map<juce::String, float> widths;                             // width at 100 px, measured once per text (layout is slow)
    const auto str = text.isEmpty() ? juce::String (titleOf (k)) : text;
    float& w100 = widths[str];
    if (w100 <= 0.0f) w100 = juce::jmax (1.0f, ghosty::textWidth (ghosty::font (100.0f).withExtraKerningFactor (0.1f), str));
    const float maxW = 1.2f;                                              // fits the body's width at the title's height
    return juce::jmin (0.36f * R, maxW * R * 100.0f / w100);              // fits the body, 0.36 R tall at most
}
struct TitleSprite { juce::Image img; juce::Rectangle<float> ink; };    // ink = the glyph box inside the image
inline const TitleSprite& titleSprite (Kind k, float px, const juce::String& text = {})
{
    static std::map<std::tuple<int, int, juce::String>, TitleSprite> cache;
    const auto str = text.isEmpty() ? juce::String (titleOf (k)) : text;
    const int q = juce::roundToInt (px * 4.0f);
    const auto key = std::make_tuple ((int) k, q, str);
    if (auto it = cache.find (key); it != cache.end()) return it->second;
    if (cache.size() > 96) cache.clear();
    px = (float) q / 4.0f;
    const auto f = ghosty::font (px).withExtraKerningFactor (0.1f);
    juce::GlyphArrangement ga; ga.addLineOfText (f, str, 0.0f, 0.0f);
    juce::Path glyphs; ga.createPath (glyphs);
    const float ow = juce::jmax (1.1f, px * 0.17f);
    auto b = glyphs.getBounds();
    const int pad = (int) std::ceil (ow) + 1;
    TitleSprite ts;
    ts.img = juce::Image (juce::Image::ARGB, (int) std::ceil (b.getWidth()) + 2 * pad, (int) std::ceil (b.getHeight()) + 2 * pad, true, juce::SoftwareImageType());
    glyphs.applyTransform (juce::AffineTransform::translation ((float) pad - b.getX(), (float) pad - b.getY()));
    {
        juce::Graphics g (ts.img);
        g.setColour (titleOutline (k)); g.strokePath (glyphs, juce::PathStrokeType (2.0f * ow, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (titleColour (k)); g.fillPath (glyphs);
    }
    ts.ink = glyphs.getBounds();
    return cache.emplace (key, ts).first->second;
}
// where the title's (first line's) glyphs land on screen for a ghost at (cx, cy), unit radius R
inline juce::Rectangle<float> titleBounds (Kind k, float cx, float cy, float R, const juce::String& label = {})
{
    const auto l1 = titleLine1 (k, label);
    const auto& ts = titleSprite (k, titlePx (k, R, l1), l1);
    const int x = juce::roundToInt (cx - (float) ts.img.getWidth() * 0.5f), y = juce::roundToInt (cy + titleCentreY (k) * R - (float) ts.img.getHeight() * 0.5f);
    return ts.ink.translated ((float) x, (float) y);
}
inline void drawTitle (juce::Graphics& g, Kind k, float cx, float cy, float R, const juce::String& label = {})
{
    const auto l1 = titleLine1 (k, label);
    const float px = titlePx (k, R, l1);
    const auto& ts = titleSprite (k, px, l1);
    g.drawImageAt (ts.img, juce::roundToInt (cx - (float) ts.img.getWidth() * 0.5f), juce::roundToInt (cy + titleCentreY (k) * R - (float) ts.img.getHeight() * 0.5f));
    const auto l2 = titleLine2 (label);
    if (l2.isNotEmpty())
    {   // the second word, a little smaller, on the hem under the name
        const auto& t2 = titleSprite (k, juce::jmin (px * 0.85f, titlePx (k, R, l2)), l2);
        g.drawImageAt (t2.img, juce::roundToInt (cx - (float) t2.img.getWidth() * 0.5f), juce::roundToInt (cy + 0.56f * R - (float) t2.img.getHeight() * 0.5f));
    }
}

// ------------------------------------------------------------ the glow under a ghost (draw all glows first, then the ghosts)
inline void drawGlow (juce::Graphics& g, Kind k, float cx, float cy, float R, const Pose& p)
{
    if (k == Kind::Classic) return;
    const bool light = isLightBackdrop (p.backdrop);
    const float I = intensity (k, p);
    const float gr = R * (1.7f + 0.25f * I);    // (alpha carries most of the intensity: a bigger glow costs pixels, a brighter one nothing)
    // a mid-tone backdrop (the PURPLE window) needs a stronger glow than near-black to tint it as much
    const float mid = light ? 0.0f : juce::jlimit (0.0f, 1.0f, (float) ghosty::themes::relLum (p.backdrop) * 25.0f);
    const float a = juce::jmin (1.0f, light ? 0.85f + 0.4f * I : 0.55f + 0.45f * I + 0.45f * mid);
    auto blob = [&] (juce::Colour c, float x, float y, float rr, float alpha) { detail::glowAt (g, c.withAlpha (alpha), x, y, rr); };
    if (k == Kind::Morph)
    {
        const auto yel = light ? juce::Colour (0xffC08A00) : juce::Colour (0xffFFD21A), pink = glowColour (k, light);
        detail::glowAt (g, p.flip ? pink : yel, p.flip ? yel : pink, a, cx, cy, gr * 1.05f);
    }
    else if (k == Kind::Heat)
        blob (glowColour (k, light), cx, cy - R * 0.12f, gr, a);
    else
        blob (glowColour (k, light), cx, cy, gr, a);
}

// ------------------------------------------------------------ one ghost centred at (cx, cy), unit radius R (pad: ~20 idle, ~23 dragged)
// Drawn in the piano GHOST CURSOR's style (its body, hem, drips and face, flat fills, a thin rim), in each character's own colours and
// with its own extras: HEAT a flickering flame crown + embers, SPACE echo copies + twinkling sparkles, MORPH a wobbling dripping crown +
// falling drops, TONE rippling sound waves. pose.gaze turns the eyes / leans the head; pose.dragGlitch adds slices + scanlines (drawGhost).
namespace detail
{
inline void drawGhostCore (juce::Graphics& g, Kind k, float cx, float cy, float R, const Pose& pose)
{
    const float t = std::floor (pose.t * 24.0f) / 24.0f;      // idle animation in 24 steps per second
    const bool light = isLightBackdrop (pose.backdrop);
    const float I = intensity (k, pose);
    const Space S = spaceFor (pose, cx, cy, R);
    const auto rim = rimColour (k, light), glow = glowColour (k, light);
    const float split = juce::jmax (pose.glitch, pose.dragGlitch);
    // clip to the ghost's own box: JUCE sizes every fill's edge table to the clip, so this keeps each fill small (the pad is big)
    juce::Graphics::ScopedSaveState clipState (g);
    g.reduceClipRegion (juce::Rectangle<float> (cx - 1.8f * R, cy - 1.75f * R, 3.6f * R, 3.55f * R).expanded (split * R * 0.25f + 0.08f * R + 2.0f).getSmallestIntegerContainer());

    juce::Path body (cute::body());  body.applyTransform (S.T);
    juce::Path hem (cute::hem());    hem.applyTransform (S.T);
    // the drips: MORPH's lengthen with POS and wobble; HEAT's flicker like flame tongues
    float dripLen = 1.0f;
    if (k == Kind::Morph) dripLen = 0.75f + 0.6f * pose.x + 0.08f * std::sin (t * 2.0f);
    else if (k == Kind::Heat) dripLen = 0.85f + 0.3f * pose.x + 0.1f * std::sin (t * 9.0f);
    juce::Path drips (cute::drips());
    drips.applyTransform (juce::AffineTransform::scale (1.0f, dripLen, 0.0f, cute::dripTop).followedBy (S.T));

    // the GhostBlock RGB split while glitching / dragging (two tinted silhouettes: cheap)
    if (split > 0.08f)
    {
        const float d = split * R * 0.22f, j = std::sin (pose.t * 37.0f) * d * 0.5f;
        // a slow drag shows one tinted copy (slime / ecto alternating, glitchy); a fast drag both
        const bool both = split > 0.6f, slimeSide = both || std::sin (pose.t * 23.0f) > 0.0f;
        if (slimeSide) { g.setColour (ghosty::col::slime.withAlpha (0.42f * split)); g.fillPath (body, juce::AffineTransform::translation (-d, 0)); }
        if (both || ! slimeSide) { g.setColour (ghosty::col::ecto.withAlpha (0.48f * split)); g.fillPath (body, juce::AffineTransform::translation (d, j)); }
    }

    // the rim: like the cursor ghost, none on the dark pad at pad size (the glow under it frames it); big ghosts and light backdrops
    // get a thin crisp outline in the character's rim colour (a stroke under the fills: only its outer half shows)
    const bool crisp = light || R >= 30.0f;
    auto rimOf = [&] (const juce::Path& path, juce::Colour c)
    {
        if (! crisp) return;
        g.setColour (c); g.strokePath (path, juce::PathStrokeType (2.0f * juce::jmax (1.0f, 0.04f * R), juce::PathStrokeType::curved));
    };
    auto shape = [&] (juce::Colour bodyC, const juce::FillType* bodyFill, juce::Colour hemC, juce::Colour dripC)
    {
        g.setColour (dripC); g.fillPath (drips);
        if (bodyFill != nullptr) g.setFillType (*bodyFill); else g.setColour (bodyC);
        g.fillPath (body);
        g.setColour (hemC); g.fillPath (hem);
    };

    Face F;
    switch (k)
    {
        case Kind::Heat:
        {
            // flame crown behind the head: outer orange-red, inner yellow; taller with drive, flickering
            const float h = 0.2f + 0.42f * pose.x + (pose.held ? 0.08f : 0.0f);
            auto outer = flameCrown (t, h, 0.0f); S.map (outer);
            auto inner = flameCrown (t, h * 0.62f, 1.3f); S.map (inner);
            g.setColour (juce::Colour (0xffF0581A)); g.fillPath (outer);
            g.setColour (juce::Colour (0xffFFC94A)); g.fillPath (inner);
            if (crisp) { juce::Path sil (body); sil.addPath (drips); rimOf (sil, rim.withAlpha (0.55f + 0.45f * I)); }
            // dark soft body with an orange flame edge (the hem + the drips)
            // (the edge burns brighter with drive: deep ember red at 0 -> bright orange / yellow at full)
            const auto hemTop = S.p (0.0f, 0.42f), hemBot = S.p (0.0f, 0.95f);
            g.setColour (juce::Colour (0xffA8240A).interpolatedWith (juce::Colour (0xffFF5A12), pose.x)); g.fillPath (drips);
            g.setColour (juce::Colour (0xff2E2733)); g.fillPath (body);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffD8601E).interpolatedWith (juce::Colour (0xffFFD24A), pose.x), hemTop.x, hemTop.y,
                                                     juce::Colour (0xff9A2408).interpolatedWith (juce::Colour (0xffFF6A14), pose.x), hemBot.x, hemBot.y, false));
            g.fillPath (hem);
            // embers rising around it: more with drive
            const int n = 3 + (int) std::round (6.0f * pose.x);
            juce::Path em;
            for (int i = 0; i < n; ++i)
            {
                const float ph = frac (t * 0.55f + (float) i * 0.37f);
                const float side = (i % 2 ? 1.0f : -1.0f), x = side * (0.84f + 0.32f * frac ((float) i * 0.618f)) + 0.08f * std::sin (t * 3.0f + (float) i);
                const float y = 0.7f - ph * 1.7f, r = (0.035f + 0.025f * frac ((float) i * 0.43f)) * (1.0f - ph * 0.5f);
                S.ellipse (em, x, y, r * 2.0f, r * 2.0f);
            }
            g.setColour (juce::Colour (light ? 0xffD04A00 : 0xffFFB040).withAlpha (0.9f)); g.fillPath (em);
            F.eye = juce::Colour (0xffFFB347); F.pupil = juce::Colour (0xff140E12); F.blinkSeed = 0.4f;
            break;
        }
        case Kind::Space:
        {
            // echo copies trailing behind (more with ECHO): the wispy tail
            if (pose.x > 0.05f)
                for (int e = 1; e <= (R >= 30.0f ? 2 : 1); ++e)
                {
                    g.setColour (glow.withAlpha (0.18f * pose.x / (float) e));
                    g.fillPath (body, juce::AffineTransform::translation ((pose.flip ? 1.0f : -1.0f) * R * 0.28f * (float) e, R * 0.1f * (float) e));
                }
            if (crisp) { juce::Path sil (body); sil.addPath (drips); rimOf (sil, rim); }
            shape (juce::Colour (0xffEFE6FF), nullptr, juce::Colour (0xff9C74FF), juce::Colour (0xff8A5CFF));
            // sparkles inside the sheet (on the head, clear of the name) and stars around it: more with REVERB, twinkling
            juce::Path st, dots;
            static const float inner[][2] { { -0.5f, -0.52f }, { 0.52f, -0.42f }, { -0.42f, -0.08f } };
            for (int i = 0; i < 3; ++i)
            {
                const float tw = 0.5f + 0.5f * std::sin (t * 2.6f + (float) i * 2.1f);
                addSparkle (dots, S.p (inner[i][0], inner[i][1]), (0.045f + 0.04f * tw) * R);
            }
            const int n = 2 + (int) std::round (7.0f * pose.y);
            for (int i = 0; i < n; ++i)
            {
                const float a = (float) i * 2.39996f + 0.6f, rr = 1.12f + 0.3f * frac ((float) i * 0.618f + 0.2f);
                const float x = std::cos (a) * rr * 1.08f, y = std::sin (a) * rr * 0.86f - 0.05f;
                const float tw = std::pow (0.5f + 0.5f * std::sin (t * 3.0f + (float) i * 1.9f), 2.0f);
                const float size = (0.07f + 0.07f * frac ((float) i * 0.37f)) * (0.45f + 0.75f * tw) * (0.85f + 0.3f * pose.y);
                addSparkle (st, S.p (x, y), size * R);
            }
            g.setColour (juce::Colour (0xffA070FF)); g.fillPath (dots);
            g.setColour (light ? juce::Colour (0xff5B2FD6) : juce::Colour (0xffEADFFF)); g.fillPath (st);
            F.eye = juce::Colour (0xff1E1546); F.pupil = juce::Colour (0xffB49BFF); F.blinkSeed = 1.7f;
            break;
        }
        case Kind::Morph:
        {
            // yellow -> hot pink across a soft liquid split that follows POS; a wobbling dripping crown of the same liquid
            const float sp = (pose.x - 0.5f) * 0.7f + 0.06f * std::sin (t * 1.5f), tilt = 0.25f * std::sin (t * 0.9f);
            auto fill = S.lin (juce::Colour (0xffFFE53A), sp - 0.5f, -tilt, juce::Colour (0xffF0189A), sp + 0.5f, tilt);
            fill.addColour (0.40, juce::Colour (0xffFFC21A)); fill.addColour (0.5, juce::Colour (0xffFF6A70)); fill.addColour (0.60, juce::Colour (0xffFF4FC0));
            auto crown = morphCrown (t, pose.x); S.map (crown);
            crown = flattened (crown);
            if (crisp) { juce::Path sil (crown); sil.addPath (body); sil.addPath (drips); rimOf (sil, rim); }
            g.setColour (juce::Colour (0xffFF4FC0)); g.fillPath (drips);
            g.setGradientFill (fill); g.fillPath (crown); g.fillPath (body);
            g.setColour (juce::Colour (0xffF0189A)); g.fillPath (hem);
            // falling drops under the drips
            juce::Path dr;
            static const float tips[][2] { { -0.175f, 1.23f }, { 0.175f, 1.08f }, { -0.467f, 0.94f } };
            for (int i = 0; i < 3; ++i)
            {
                const float ph = frac (t * 0.45f + (float) i * 0.33f);
                const float y = cute::dripTop + (tips[i][1] - cute::dripTop) * dripLen + 0.05f + ph * 0.22f;
                const float r = 0.05f * (1.0f - ph * 0.6f);
                S.ellipse (dr, tips[i][0], y, r * 2.0f, r * 2.3f);
            }
            g.setColour (juce::Colour (0xffFF9AD8).withAlpha (0.85f)); g.fillPath (dr);
            // gloss + bubbles
            juce::Path gl; S.ellipse (gl, -0.4f, -0.6f, 0.26f, 0.11f); S.ellipse (gl, 0.5f, -0.02f, 0.07f, 0.07f); S.ellipse (gl, -0.55f, 0.08f, 0.05f, 0.05f);
            g.setColour (juce::Colours::white.withAlpha (0.6f)); g.fillPath (gl);
            F.eye = juce::Colour (0xff2A1020); F.pupil = juce::Colour (0xffFFE53A); F.blinkSeed = 2.9f;
            break;
        }
        case Kind::Tone:
        {
            // soundwave lines on both sides: amplitude follows the cutoff (brightness), sharper with resonance, rippling
            const float amp = 0.04f + 0.2f * pose.x;
            for (float side : { -1.0f, 1.0f })
            {
                juce::Path w;
                for (int l = 0; l < 2; ++l)
                {
                    const float y0 = -0.08f + (float) l * 0.2f, a = amp * (l == 0 ? 1.0f : 0.6f), freq = 15.0f + 10.0f * pose.y;
                    for (int i = 0; i <= 12; ++i)
                    {
                        const float u = (float) i / 12.0f, x = side * (0.84f + 0.5f * u);
                        const float y = y0 + a * std::sin (u * freq * 0.45f - t * 7.0f * side + (float) l) * std::sin (u * juce::MathConstants<float>::pi);
                        if (i == 0) w.startNewSubPath (S.p (x, y)); else w.lineTo (S.p (x, y));
                    }
                }
                const auto A = S.p (side * 0.84f, 0.0f), B = S.p (side * 1.34f, 0.0f);
                g.setGradientFill (juce::ColourGradient (rim.withAlpha (0.95f), A.x, A.y, rim.withAlpha (0.0f), B.x, B.y, false));
                g.strokePath (w, juce::PathStrokeType (juce::jmax (1.0f, 0.05f * R)));
            }
            // a little level meter under the drips on big ghosts
            if (R >= 40.0f)
            {
                juce::Path bars;
                for (int i = 0; i < 11; ++i)
                {
                    const float x = -0.6f + (float) i * 0.12f, env = std::sin ((float) (i + 1) / 12.0f * juce::MathConstants<float>::pi);
                    const float h = (0.03f + 0.12f * pose.x) * env * (0.35f + 0.65f * std::abs (std::sin ((float) i * 1.3f + t * 5.0f)));
                    auto a = S.p (x - 0.018f, 1.42f - h), b = S.p (x + 0.018f, 1.42f + h);
                    bars.addRoundedRectangle (juce::Rectangle<float> (a, b), 0.018f * R);
                }
                g.setColour (rim.withAlpha (0.85f)); g.fillPath (bars);
            }
            if (crisp) { juce::Path sil (body); sil.addPath (drips); rimOf (sil, rim); }
            // the hem + drips glow brighter with the cutoff (dull teal when dark -> bright cyan when open)
            const auto cyan = light ? juce::Colour (0xff3A7F8A).interpolatedWith (juce::Colour (0xff0AA6BE), pose.x)
                                    : juce::Colour (0xff1C6E7C).interpolatedWith (juce::Colour (0xff3FF0FF), pose.x);
            shape (light ? juce::Colour (0xffF6FCFD) : juce::Colour (0xffF4FCFE), nullptr, cyan, cyan);
            F.eye = juce::Colour (0xff0E1E26); F.pupil = juce::Colour (0xff3FE6FF); F.blinkSeed = 3.6f;
            break;
        }
        case Kind::Classic:
        default: break;
    }
    drawFace (g, S, F, t, pose.gaze, pose.flip);
    if (pose.title)
    {
        const auto lean = juce::Point<float> (pose.gaze.x * 0.05f * R, pose.gaze.y * 0.04f * R);
        drawTitle (g, k, cx + lean.x, cy + lean.y, R, pose.label);
    }
}
} // namespace detail

// the ghost, plus the drag glitch: pose.dragGlitch 0..1 (from the drag speed) tears it into a few horizontally shifted slices and lays
// flickering scanlines over its body. Deterministic per animation step (no random generator), a few clipped redraws, no allocation
// beyond the paths of one paint; only the ghost being dragged (or just let go) ever pays for it.
inline float sliceShift (const Pose& pose, int i)
{
    const int step = (int) std::floor (pose.t * 24.0f);
    return detail::frac (std::sin ((float) (step * 7 + i * 13) * 12.9898f) * 43758.5453f);
}
inline void drawGhost (juce::Graphics& g, Kind k, float cx, float cy, float R, const Pose& pose)
{
    if (k == Kind::Classic) return;
    const float dg = juce::jlimit (0.0f, 1.0f, pose.dragGlitch);
    if (dg < 0.04f) { detail::drawGhostCore (g, k, cx, cy, R, pose); return; }
    const float top = cy - 1.9f * R, bottom = cy + 1.9f * R;
    const int n = 3 + (int) (dg * 2.5f);
    float y = top;
    for (int i = 0; i < n; ++i)
    {
        const float h = i == n - 1 ? bottom - y : (bottom - top) / (float) n * (0.6f + 0.8f * sliceShift (pose, i + 31));
        const float r = sliceShift (pose, i);
        const float shift = r < 0.3f ? 0.0f : (r - 0.65f) * 2.0f * dg * R * 0.32f;
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (juce::Rectangle<float> (cx - 2.2f * R, y, 4.4f * R, h + 0.5f).getSmallestIntegerContainer());
        detail::drawGhostCore (g, k, cx + shift, cy, R, pose);
        y += h;
    }
    {   // scanlines over the body, flickering
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (bodyPathFor (k, pose, cx, cy, R));
        const float flick = 0.55f + 0.45f * sliceShift (pose, 97);
        g.setColour (juce::Colours::black.withAlpha (0.22f * dg * flick));
        const float gap = juce::jmax (2.0f, R * 0.11f), lh = juce::jmax (1.0f, R * 0.035f);
        for (float sy = top + gap * sliceShift (pose, 5); sy < bottom; sy += gap) g.fillRect (cx - 2.0f * R, sy, 4.0f * R, lh);
    }
}

// the unit radius the pad uses for a ghost of the classic "size" (30 idle, 40 dragged, + level)
// (owner, round 3: the name is written on the body, so the idle ghost stays big enough to read it: 20 px idle, 23 px dragged)
inline float& padIdleRadius() { static float r = 20.0f; return r; }     // 16.5 = the first round-3 size (tests show both)
inline float radiusForPadSize (float size) { return padIdleRadius() + (size - 30.0f) * (padIdleRadius() >= 20.0f ? 0.3f : 0.55f); }

// the painter Ghosty Pro installs on its XY pad (see ProEditor.cpp, "GHOST ART (round 3)")
// (returns false for a ghost it does not know: the pad then draws its classic look)
inline std::function<bool (juce::Graphics&, const ghosty::XYGhostPad::GhostPaint&)> padPainter()
{
    return [] (juce::Graphics& g, const ghosty::XYGhostPad::GhostPaint& gp)
    {
        const auto k = kindFor (gp.ghost.name);
        if (k == Kind::Classic) return false;
        Pose p;
        p.t = gp.time; p.x = gp.ghost.px != nullptr ? gp.ghost.px->getValue() : 0.0f; p.y = gp.ghost.py != nullptr ? gp.ghost.py->getValue() : 0.0f;
        p.level = gp.level; p.glitch = gp.glitch; p.held = gp.ghost.held; p.flip = gp.ghost.flip; p.backdrop = gp.backdrop;
        p.gaze = gp.ghost.gaze; p.dragGlitch = gp.ghost.dragGlitch; p.label = gp.ghost.name;
        const float R = radiusForPadSize (gp.size);
        if (gp.glowPass) drawGlow (g, k, gp.centre.x, gp.centre.y, R, p);
        else drawGhost (g, k, gp.centre.x, gp.centre.y, R, p);
        return true;
    };
}
} // namespace proghost
