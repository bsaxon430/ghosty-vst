#pragma once
// ArpEZator FACES (2026-11-11): picture-backed looks, next to BLACK / PURPLE / LIGHT / GHOST.
//   HOLO       the rainy bus-stop city with the holographic raincoat (pearl / lilac / mint / pink foil edges)
//   NEON RAIN  the neon sign canyon (SUNRISE / DUSK / MIDNIGHT) or the wet street with the tail lights (TRAP / HIP HOP)
//   MASK       the red / mustard / teal stencil face (print-misregistration edges)
//   DREAM      an original watercolour dawn (pale sun, washed hills, drifting paper shards; pencil + wash edges)
//   AUTO       a face per mood (ArpProcessor::faceOf): SUNRISE DREAM, DUSK HOLO, MIDNIGHT + TRAP NEON RAIN, HIP HOP MASK
// The pictures live in data/faces (binary data, 1240 x 820 = the editor's own canvas) with a pre-blurred copy each: a card is the
// blurred picture under a tinted wash (frosted), so there are no blur filters at run time. Each picture is rescaled ONCE per pixel scale
// and blitted afterwards. Message thread only.
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include "ArpArt.h"
#if ARPEZATOR_HAS_DATA
 #include "BinaryData.h"
#endif

namespace arpui::faces
{
using juce::Colour;
enum { None = 0, Holo = 1, Neon = 2, Mask = 3, Dream = 4, Kitsune = 5 };

inline const char* fileFor (int face, int mode, bool soft)
{
    const bool street = mode == 3 || mode == 4;   // TRAP / HIP HOP: the street; the moods: the sign canyon
    switch (face)
    {
        case Holo:  return soft ? "face_holo_soft.jpg" : "face_holo.jpg";
        case Neon:  return street ? (soft ? "face_neon_street_soft.jpg" : "face_neon_street.jpg") : (soft ? "face_neon_sky_soft.jpg" : "face_neon_sky.jpg");
        case Mask:  return soft ? "face_mask_soft.jpg" : "face_mask.jpg";
        case Dream: return soft ? "face_dream_soft.jpg" : "face_dream.jpg";
        case Kitsune: return soft ? "face_kitsune_soft.jpg" : "face_kitsune.jpg";
        default:    return nullptr;
    }
}

// the frosted wash on a card: the tint and how much of it covers the blurred picture
struct Wash { Colour tint; float card, roll; };
inline Wash washFor (int face)
{
    switch (face)
    {
        case Holo:  return { Colour (0xff1a1230), 0.62f, 0.56f };
        case Neon:  return { Colour (0xff060b13), 0.62f, 0.56f };
        case Mask:  return { Colour (0xff110f0f), 0.58f, 0.52f };
        case Dream: return { Colour (0xfffffaf4), 0.64f, 0.58f };
        case Kitsune: return { Colour (0xff0a0506), 0.6f, 0.5f };
        default:    return { Colour (0xff000000), 1.0f, 1.0f };
    }
}

class Art
{
public:
    // the picture at w x h logical units, rescaled once for this pixel scale (null image = no data: the caller paints the plain backdrop)
    const juce::Image& get (int face, int mode, bool soft, int w, int h, float scale)
    {
        static const juce::Image none;
        const char* name = fileFor (face, mode, soft);
        if (name == nullptr) return none;
        const int pw = juce::jmax (1, juce::roundToInt ((float) w * scale)), ph = juce::jmax (1, juce::roundToInt ((float) h * scale));
        const Key k { juce::String (name), pw, ph };
        auto it = cache.find (k);
        if (it != cache.end()) return it->second;
        if (cache.size() > 8) cache.clear();
        juce::Image src = load (name);
        juce::Image out;
        if (src.isValid()) out = src.rescaled (pw, ph, juce::Graphics::highResamplingQuality).convertedToFormat (juce::Image::RGB);
        ++builds;
        return cache.emplace (k, out).first->second;
    }
    // the full-window picture (content coordinates, 0, 0, w, h); false = no picture
    bool drawBackdrop (juce::Graphics& g, int face, int mode, int w, int h)
    {
        const float scale = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        const auto& img = get (face, mode, false, w, h, scale);
        if (! img.isValid()) return false;
        g.setOpacity (1.0f);
        g.drawImageTransformed (img, juce::AffineTransform::scale ((float) w / (float) img.getWidth(), (float) h / (float) img.getHeight()));
        return true;
    }
    // a frosted card: the blurred picture under the face's wash, clipped to r (r in the caller's coordinates; `origin` = where the caller's
    // (0, 0) sits on the w x h canvas). roll = the piano roll's denser wash.
    void frost (juce::Graphics& g, juce::Rectangle<float> r, float rad, juce::Point<float> origin, int face, int mode, int w, int h, bool roll = false)
    {
        const auto ws = washFor (face);
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip; clip.addRoundedRectangle (r, rad); g.reduceClipRegion (clip);
        const float scale = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        const auto& img = get (face, mode, true, w, h, scale);
        if (img.isValid())
        {
            g.setOpacity (1.0f);
            g.drawImageTransformed (img, juce::AffineTransform::scale ((float) w / (float) img.getWidth(), (float) h / (float) img.getHeight()).translated (-origin.x, -origin.y));
            g.setColour (ws.tint.withAlpha (roll ? ws.roll : ws.card));
        }
        else g.setColour (ws.tint);
        g.fillRect (r);
        if (face == Holo)
        {   // a faint iridescent sheen across the card, like the raincoat catching the signs
            juce::ColourGradient sh (Colour (0x16b8a6ff), r.getX(), r.getY(), Colour (0x10ff8fd0), r.getRight(), r.getBottom(), false);
            sh.addColour (0.35, Colour (0x0e8ff7d8)); sh.addColour (0.7, Colour (0x0cffe08a));
            g.setGradientFill (sh); g.fillRect (r);
        }
        if (face == Dream)
        {   // a wash pooled along the top edge, like pigment drying
            g.setGradientFill (juce::ColourGradient (Colour (0x26f0b0bd), r.getX(), r.getY(), Colour (0x00f0b0bd), r.getX(), r.getY() + juce::jmin (60.0f, r.getHeight() * 0.4f), false));
            g.fillRect (r);
        }
    }
    int builds = 0;   // (tests: built once per picture and pixel scale)
private:
    static juce::Image load (const char* name)
    {
       #if ARPEZATOR_HAS_DATA
        for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
            if (juce::String (BinaryData::originalFilenames[i]) == name)
            {
                int size = 0;
                if (auto* d = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size))
                    return juce::ImageFileFormat::loadFrom (d, (size_t) size);
            }
       #else
        juce::ignoreUnused (name);
       #endif
        return {};
    }
    using Key = std::tuple<juce::String, int, int>;
    std::map<Key, juce::Image> cache;
};

// the HOLO foil: the raincoat's colours around a card
inline juce::ColourGradient foil (juce::Rectangle<float> r, float alpha = 1.0f)
{
    juce::ColourGradient gr (Colour (0xffc9b8ff).withMultipliedAlpha (alpha), r.getX(), r.getY(), Colour (0xffffd59a).withMultipliedAlpha (alpha), r.getRight(), r.getBottom(), false);
    gr.addColour (0.22, Colour (0xff9ff5e1).withMultipliedAlpha (alpha));
    gr.addColour (0.45, Colour (0xffff9fd6).withMultipliedAlpha (alpha));
    gr.addColour (0.68, Colour (0xff8fd8ff).withMultipliedAlpha (alpha));
    return gr;
}

// a card's edge in the face's style; edge = the lane / line colour, sel = the selected lane (thicker)
inline void edge (juce::Graphics& g, juce::Rectangle<float> r, float rad, int face, Colour edgeC, bool sel = false)
{
    switch (face)
    {
        case Holo:
            g.setGradientFill (foil (r, sel ? 1.0f : 0.85f)); g.drawRoundedRectangle (r.reduced (0.8f), rad, sel ? 2.6f : 1.8f);
            g.setColour (Colour (0x30ffffff)); g.drawRoundedRectangle (r.reduced (2.4f), juce::jmax (0.0f, rad - 2.0f), 0.8f);
            if (sel) { g.setColour (edgeC.withAlpha (0.9f)); g.drawRoundedRectangle (r.reduced (3.4f), juce::jmax (0.0f, rad - 3.0f), 1.2f); }
            break;
        case Neon: case Kitsune:
        {   // a neon tube: a wide soft glow under a bright core
            const Colour c = sel ? edgeC : edgeC.withMultipliedAlpha (0.85f);
            for (auto [w, a] : { std::pair<float, float> (7.0f, 0.07f), std::pair<float, float> (4.0f, 0.14f), std::pair<float, float> (2.2f, 0.32f) })
            { g.setColour (c.withAlpha (a)); g.drawRoundedRectangle (r.reduced (1.0f), rad, w); }
            g.setColour (c.interpolatedWith (juce::Colours::white, 0.35f)); g.drawRoundedRectangle (r.reduced (1.0f), rad, sel ? 1.6f : 1.0f);
            break;
        }
        case Mask:
            // print misregistration: a red plate and a teal plate off by a hair, the black key on top, mustard when selected
            g.setColour (Colour (0xb0d23c3c)); g.drawRoundedRectangle (r.reduced (1.0f).translated (2.0f, 1.5f), rad, 1.6f);
            g.setColour (Colour (0x9049b6c9)); g.drawRoundedRectangle (r.reduced (1.0f).translated (-1.5f, -1.0f), rad, 1.2f);
            g.setColour (sel ? edgeC : Colour (0xffe0b23f).withAlpha (0.8f)); g.drawRoundedRectangle (r.reduced (1.0f), rad, sel ? 2.6f : 1.4f);
            break;
        case Dream:
            // a soft watercolour bleed, then two light pencil lines that do not quite agree
            g.setColour (edgeC.withAlpha (sel ? 0.30f : 0.16f)); g.drawRoundedRectangle (r.reduced (1.0f), rad, 5.0f);
            g.setColour (Colour (0x8c5a3e4c)); g.drawRoundedRectangle (r.reduced (1.2f), rad, 0.9f);
            g.setColour (Colour (0x405a3e4c)); g.drawRoundedRectangle (r.reduced (1.2f).translated (0.8f, -0.6f), rad + 1.0f, 0.7f);
            if (sel) { g.setColour (edgeC.withAlpha (0.85f)); g.drawRoundedRectangle (r.reduced (2.6f), juce::jmax (0.0f, rad - 2.0f), 1.6f); }
            break;
        default: break;
    }
}

// ---------------------------------------------------------------- neon type (OFL fonts in data/faces/fonts, licences beside them)
// 0 TILT NEON (a bent-tube sign face), 1 MONOTON (multi-line retro neon), 2 AUDIOWIDE (wide future), 3 STARDOS STENCIL (bold stencil)
inline juce::Typeface::Ptr typeface (int which)
{
    // kept on the heap and never freed on purpose: a static array of typefaces gets destroyed after JUCE's own font
    // statics when the host quits, and that crashed FL Studio on close (std::terminate in ~CoreTextTypeface)
    static juce::Typeface::Ptr* const tf = new juce::Typeface::Ptr[4];
    static bool tried[4] {};
    which = juce::jlimit (0, 3, which);
    if (! tried[which])
    {
        tried[which] = true;
       #if ARPEZATOR_HAS_DATA
        static const char* names[4] { "face_tiltneon.ttf", "face_monoton.ttf", "face_audiowide.ttf", "face_stencil.ttf" };
        for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
            if (juce::String (BinaryData::originalFilenames[i]) == names[which])
            {
                int size = 0;
                if (auto* d = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size)) tf[which] = juce::Typeface::createSystemTypefaceFor (d, (size_t) size);
            }
       #endif
    }
    return tf[which];
}
// the text as a path, left-aligned at box's x with its baseline near the box bottom, squeezed to the box width if wider
// squeeze = how far the letters may be narrowed (1 = never; 0.75 = down to 3/4 width) so a wide face (MONOTON, AUDIOWIDE) still fills the height
inline juce::Path textPath (const juce::String& s, int fontId, float h, juce::Rectangle<float> box, float squeeze = 1.0f)
{
    juce::Font f = typeface (fontId) != nullptr ? juce::Font (juce::FontOptions (typeface (fontId)).withHeight (h)) : juce::Font (juce::FontOptions (h, juce::Font::bold));
    juce::GlyphArrangement ga; ga.addLineOfText (f, s, 0, 0);
    juce::Path p; ga.createPath (p);
    const auto b = p.getBounds(); if (b.isEmpty()) return p;
    float ky = juce::jmin (1.0f, box.getHeight() / b.getHeight());
    const float kx = juce::jmin (ky, box.getWidth() / b.getWidth());
    if (kx < ky * squeeze) ky = kx / juce::jmax (0.1f, squeeze);
    p.applyTransform (juce::AffineTransform::translation (-b.getX(), -b.getY()).scaled (kx, ky).translated (box.getX(), box.getY() + (box.getHeight() - b.getHeight() * ky) * 0.5f));
    return p;
}
// a lit neon tube: wide soft glow, then the glass, then the hot white-ish core
inline void neon (juce::Graphics& g, const juce::Path& p, Colour core, Colour glow, float spread = 1.0f, bool fill = true)
{
    for (auto [w, a] : { std::pair<float, float> (9.0f, 0.05f), std::pair<float, float> (6.0f, 0.09f), std::pair<float, float> (3.6f, 0.18f), std::pair<float, float> (2.0f, 0.35f) })
    { g.setColour (glow.withAlpha (a)); g.strokePath (p, juce::PathStrokeType (w * spread, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); }
    if (fill) { g.setColour (core); g.fillPath (p); }
    g.setColour (core.interpolatedWith (juce::Colours::white, 0.6f).withAlpha (0.85f)); g.strokePath (p, juce::PathStrokeType (0.7f * spread));
}
// the header names (PROFESSOR BERNSTEIN'S + ArpEZator) in the face's neon type
inline void names (juce::Graphics& g, juce::Rectangle<float> sub, juce::Rectangle<float> tr, const Palette& P)
{
    // 2026-11-13: the name bigger on every face (a taller box, and the wide types may narrow a little to fill it)
    const auto title = textPath ("ArpEZator", P.face == Neon || P.face == Kitsune ? 0 : P.face == Holo ? 2 : P.face == Mask ? 3 : 1, 48.0f, tr, P.face == Dream ? 0.72f : P.face == Holo ? 0.84f : 1.0f);
    const auto subP = textPath ("PROFESSOR BERNSTEIN'S", P.face == Neon || P.face == Kitsune ? 0 : P.face == Mask ? 3 : 2, 9.5f, sub);
    switch (P.face)
    {
        case Holo:
        {   // AUDIOWIDE in the raincoat's foil with a lilac neon glow and a white-hot rim; the small line mint
            for (auto [w, a] : { std::pair<float, float> (8.0f, 0.06f), std::pair<float, float> (4.5f, 0.14f), std::pair<float, float> (2.4f, 0.3f) })
            { g.setColour (Colour (0xffb59bff).withAlpha (a)); g.strokePath (title, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); }
            g.setGradientFill (foil (title.getBounds())); g.fillPath (title);
            g.setColour (Colour (0xd0ffffff)); g.strokePath (title, juce::PathStrokeType (0.8f));
            neon (g, subP, Colour (0xffc8fff0), Colour (0xff8ff2d9), 0.5f);
            break;
        }
        case Neon:
            neon (g, title, Colour (0xffff5cc0), Colour (0xffff2d95), 1.0f);
            neon (g, subP, Colour (0xff9ff4ff), Colour (0xff3de3ff), 0.45f);
            break;
        case Kitsune:
            neon (g, title, Colour (0xffff6a60), Colour (0xffff2020), 1.0f);
            neon (g, subP, Colour (0xffe6f6ff), Colour (0xff8fd0ff), 0.45f);
            break;
        case Mask:
            g.setColour (Colour (0xcc49b6c9)); g.fillPath (title, juce::AffineTransform::translation (-1.6f, -1.0f));
            neon (g, title, Colour (0xfff7c64e), Colour (0xffff3b3b), 1.0f);
            g.setColour (Colour (0xff0e0c0c)); g.strokePath (title, juce::PathStrokeType (0.6f));
            neon (g, subP, Colour (0xfffcf4e2), Colour (0xffff3b3b), 0.45f);
            break;
        case Dream:
            // MONOTON's retro tubes in blush pink on a teal glow, a wine shadow so it reads on the paper
            g.setColour (Colour (0x804a2f3c)); g.fillPath (title, juce::AffineTransform::translation (0.8f, 1.2f));
            neon (g, title, Colour (0xffe8508a), Colour (0xff5fc8d8), 0.8f);
            g.setColour (Colour (0xff6a4050)); g.fillPath (subP);
            break;
        default: break;
    }
}


// ---------------------------------------------------------------- the professor logo, redrawn per face AND per mood
// The same scene as drawProfessor (ArpArt.h): two posts, the striped hammock, the professor lying back with his round fluffy white hair
// and round glasses, one arm up holding the mood's prop. SUNRISE: the sun + rays, sunglasses. DUSK: a low banded sun, warm sky, birds.
// MIDNIGHT: moon + stars, glowing glasses, flask bubbles. TRAP: the stacked double foam cup + straw. HIP HOP: the 40 in a paper bag
// (both props are ArpArt's own propPaths(), so they are the same objects). Each face has its own way of drawing it:
//   HOLO      foil-gradient tubes on pearl glass          NEON RAIN  hot pink + cyan + yellow tubes, rain streaks
//   MASK      a posterised stencil print: flat plates, a black key line, off-register colour, halftone dots
//   DREAM     wine ink + watercolour washes on paper       KITSUNE    red lantern tubes + ice white, a fox mask on the post
// The still layer is cached per (face, mood, pixel size); a frame only adds bubbles, twinkles, a pulse and a flicker.
inline void tube (juce::Graphics& g, const juce::Path& p, Colour c, float w, bool lightBg)
{
    if (lightBg)
    {   // DREAM: a soft wash bleed, then the ink line
        g.setColour (c.withAlpha (0.25f)); g.strokePath (p, stroke (w * 2.6f));
        g.setColour (c.darker (0.6f)); g.strokePath (p, stroke (w * 0.9f));
        return;
    }
    for (auto [k, a] : { std::pair<float, float> (4.2f, 0.07f), std::pair<float, float> (2.8f, 0.14f), std::pair<float, float> (1.8f, 0.3f) })
    { g.setColour (c.withAlpha (a)); g.strokePath (p, stroke (w * k)); }
    g.setColour (c); g.strokePath (p, stroke (w));
    g.setColour (c.interpolatedWith (juce::Colours::white, 0.65f)); g.strokePath (p, stroke (w * 0.38f));
}
struct SignColours { Colour frame, prof, flask, accent; bool light; };
inline SignColours signColours (int face)
{
    switch (face)
    {
        case Holo:    return { Colour (0xffc9b8ff), Colour (0xff9ff5e1), Colour (0xffff9fd6), Colour (0xffffd59a), false };
        case Neon:    return { Colour (0xff3de3ff), Colour (0xffff4fb4), Colour (0xfffff07a), Colour (0xffb06bff), false };
        case Mask:    return { Colour (0xffff3b3b), Colour (0xfff7c64e), Colour (0xff49d3e6), Colour (0xfffcf4e2), false };
        case Dream:   return { Colour (0xffd9667f), Colour (0xff5a3e4c), Colour (0xff4f9fb3), Colour (0xffe6a04f), true };
        case Kitsune: return { Colour (0xffff2e2e), Colour (0xffe8f6ff), Colour (0xffff9a5c), Colour (0xffbfe8ff), false };
        default:      return { Colours::white, Colours::white, Colours::white, Colours::white, false };
    }
}

namespace sign
{
// what a line or a plate is for; each face gives every role a colour (c) and its glow (MASK: the off-register plate)
enum Role { Frame, Prof, Hair, Glass, Prop, Accent, Drink, Sky, Stripe, NumRoles };
struct Ink { Colour c, glow; };
struct Style { int face; bool light; Colour plate, key, blush; Ink ink[NumRoles]; };
inline const Style& styleFor (int face)
{
    auto C = [] (juce::uint32 v) { return Colour (0xff000000u | v); };
    static const Style holo { Holo, false, C (0x130e26), C (0x130e26), C (0xff9fd6),
        { { C (0xc9b8ff), C (0xb59bff) }, { C (0xf1eaff), C (0xc9b8ff) }, { C (0xffffff), C (0x9ff5e1) }, { C (0xff9fd6), C (0xff7cc8) }, { C (0x9ff5e1), C (0x5fe8c8) },
          { C (0xffd59a), C (0xffb870) }, { C (0xb79cff), C (0x9b7bff) }, { C (0x8fd8ff), C (0x5fc0ff) }, { C (0xff9fd6), C (0xff7cc8) } } };
    static const Style neon { Neon, false, C (0x08060f), C (0x08060f), C (0xff3fa8),
        { { C (0x3de3ff), C (0x22c8ff) }, { C (0xff5cb8), C (0xff3fa8) }, { C (0xfff4fb), C (0xff3fa8) }, { C (0x3de3ff), C (0x22c8ff) }, { C (0xfff07a), C (0xffd83d) },
          { C (0xfff07a), C (0xffc83d) }, { C (0xb06bff), C (0x9b4cff) }, { C (0x3de3ff), C (0x22c8ff) }, { C (0xff5cb8), C (0xff3fa8) } } };
    static const Style mask { Mask, false, C (0x0d0b0b), C (0x0d0b0b), C (0xd23c3c),
        { { C (0x49b6c9), C (0xd23c3c) }, { C (0xfcf4e2), C (0xd23c3c) }, { C (0xfcf4e2), C (0x49b6c9) }, { C (0xd23c3c), C (0x49b6c9) }, { C (0xd23c3c), C (0x49b6c9) },
          { C (0xf2c14e), C (0xd23c3c) }, { C (0xd23c3c), C (0x49b6c9) }, { C (0x49b6c9), C (0xd23c3c) }, { C (0xf2c14e), C (0xd23c3c) } } };
    static const Style dream { Dream, true, C (0xfbf3ec), C (0x3a2230), C (0xf08fa8),
        { { C (0xc9647c), C (0xc9647c) }, { C (0xf6c9d2), C (0xf0b0bd) }, { C (0xdfe9ee), C (0xb9d3dc) }, { C (0x3a2230), C (0x3a2230) }, { C (0x4f9fb3), C (0x4f9fb3) },
          { C (0xe6a04f), C (0xe6a04f) }, { C (0xa77bc4), C (0xa77bc4) }, { C (0x6fb7c6), C (0x6fb7c6) }, { C (0x6fb7c6), C (0x6fb7c6) } } };
    static const Style kitsune { Kitsune, false, C (0x0c0506), C (0x0c0506), C (0xff2e2e),
        { { C (0xff2e2e), C (0xff1a1a) }, { C (0xd9f1ff), C (0x8fd0ff) }, { C (0xffffff), C (0xbfe8ff) }, { C (0xffb35c), C (0xff8a2e) }, { C (0xffb35c), C (0xff8a2e) },
          { C (0xffb35c), C (0xff7a2e) }, { C (0xff4a6a), C (0xff2e2e) }, { C (0xbfe8ff), C (0x8fd0ff) }, { C (0xf4fbff), C (0xbfe8ff) } } };
    switch (face) { case Holo: return holo; case Mask: return mask; case Dream: return dream; case Kitsune: return kitsune; default: return neon; }
}
// a line: a lit tube (glow, glass, hot core); MASK a flat printed line with its off-register twin; DREAM a wash bleed under an ink line
inline void line (juce::Graphics& g, const Style& S, const juce::Path& p, int role, float w, float a = 1.0f)
{
    const Ink k = S.ink[role];
    switch (S.face)
    {
        case Mask:
            g.setColour (k.glow.withAlpha (0.8f * a)); g.strokePath (p, stroke (w), juce::AffineTransform::translation (0.9f, 0.7f));
            g.setColour (k.c.withAlpha (a)); g.strokePath (p, stroke (w * 1.05f));
            break;
        case Dream:
        {
            const float mix = role == Accent || role == Sky || role == Drink || role == Stripe ? 0.8f : role == Frame || role == Prop ? 0.5f : 0.0f;
            g.setColour (k.c.withAlpha (0.24f * a)); g.strokePath (p, stroke (w * 2.4f));
            g.setColour (S.key.interpolatedWith (k.c.darker (0.25f), mix).withAlpha (0.92f * a)); g.strokePath (p, stroke (juce::jmax (0.6f, w * 0.62f)));
            break;
        }
        default:
            for (auto [m, al] : { std::pair<float, float> (4.4f, 0.06f), std::pair<float, float> (2.9f, 0.13f), std::pair<float, float> (1.8f, 0.3f) })
            { g.setColour (k.glow.withAlpha (al * a)); g.strokePath (p, stroke (w * m)); }
            if (S.face == Holo && (role == Frame || role == Prof)) g.setGradientFill (foil ({ 0, 0, 100, 100 }, a)); else g.setColour (k.c.withAlpha (a));
            g.strokePath (p, stroke (w));
            g.setColour (k.c.interpolatedWith (juce::Colours::white, 0.75f).withAlpha (0.9f * a)); g.strokePath (p, stroke (w * 0.36f));
            break;
    }
}
// a plate's colour: lit glass (translucent) on the tube faces, flat ink on MASK, a wash on DREAM. k = how full (0..1)
inline void fill (juce::Graphics& g, const Style& S, const juce::Path& p, int role, float k)
{
    const Colour c = S.ink[role].c;
    if (S.face == Mask) g.setColour (k >= 0.45f ? c : c.withAlpha (k * 2.0f));
    else if (S.face == Dream) g.setColour (c.withAlpha (juce::jmin (1.0f, k * 0.8f)));
    else g.setColour (c.withAlpha (juce::jmin (1.0f, k * 0.5f)));
    g.fillPath (p);
}
// a solid shape: it hides what is behind it (so tubes never tangle), takes its colour, then its outline
inline void shape (juce::Graphics& g, const Style& S, const juce::Path& p, int role, float w, float k)
{
    if (S.face == Mask)
    {
        g.setColour (S.ink[role].glow); g.fillPath (p, juce::AffineTransform::translation (1.2f, 0.9f));
        g.setColour (S.plate); g.fillPath (p);
        fill (g, S, p, role, k);
        if (w > 0) { g.setColour (S.key); g.strokePath (p, stroke (w * 0.62f)); }
        return;
    }
    g.setColour (S.plate); g.fillPath (p);
    if (S.face == Dream) { g.setColour (S.ink[role].c.withAlpha (juce::jmin (1.0f, k * 0.4f))); g.fillPath (p, juce::AffineTransform::translation (0.7f, 0.9f)); }
    fill (g, S, p, role, k);
    if (w > 0) line (g, S, p, role, w);
}
// a face feature (eyes, smile): the key ink on the print / paper faces, a white-hot hairline on the tube faces
inline void feature (juce::Graphics& g, const Style& S, const juce::Path& p, float w)
{
    if (S.face == Mask || S.face == Dream) { g.setColour (S.key); g.strokePath (p, stroke (w * 0.85f)); }
    else line (g, S, p, Hair, w);
}
inline juce::Path star4 (float x, float y, float z)
{
    juce::Path st; st.startNewSubPath (x, y - z); st.quadraticTo (x, y, x + z, y); st.quadraticTo (x, y, x, y + z); st.quadraticTo (x, y, x - z, y); st.quadraticTo (x, y, x, y - z); st.closeSubPath();
    return st;
}
// where things are (100 x 100 units): shared by the still layer and the animated bits
constexpr float headX = 36.5f, headY = 44.5f, headRot = -0.2f, handX = 61.0f, handY = 38.5f, lensX = 5.3f, lensY = -0.3f, lensR = 4.5f;
constexpr float sunX = 79.0f, sunY = 20.0f, duskX = 70.0f, duskY = 77.0f, flaskRot = 0.08f, flaskK = 1.15f;
inline juce::AffineTransform group() { return juce::AffineTransform::scale (1.1f, 1.1f, 50.0f, 50.0f).translated (0.0f, 5.0f); }
inline juce::AffineTransform headT() { return juce::AffineTransform::scale (1.04f).rotated (headRot).translated (headX, headY); }
inline juce::Point<float> headPoint (float x, float y) { return juce::Point<float> (x, y).transformedBy (headT()); }
struct Twinkle { float x, y, z; };
inline const Twinkle* twinkles (int mode, int& n)
{
    static const Twinkle sunrise[] { { 24, 30, 1.6f } }, dusk[] { { 85, 14, 2.4f }, { 20, 13, 1.6f } };
    static const Twinkle night[] { { 46, 11, 2.6f }, { 84, 13, 3.0f }, { 77, 31, 2.0f }, { 23, 32, 1.5f } }, street[] { { 21, 14, 2.4f }, { 82, 14, 2.8f }, { 78, 33, 1.8f }, { 43, 12, 1.6f } };
    switch (mode) { case 0: n = 1; return sunrise; case 1: n = 2; return dusk; case 2: n = 4; return night; default: n = 4; return street; }
}
} // namespace sign

// the still part of the sign, in 100 x 100 units
inline void professorSignStatic (juce::Graphics& g, int face, int mode)
{
    using namespace sign;
    const Style& S = styleFor (face); const bool light = S.light, print = face == Mask, tubes = ! light && ! print;
    const float pi = juce::MathConstants<float>::pi;
    mode = juce::jlimit (0, 4, mode);
    const auto moved = [] (juce::Path p, const juce::AffineTransform& t) { p.applyTransform (t); return p; };
    const juce::Rectangle<float> plate (2, 2, 96, 96);
    juce::Path plateP; plateP.addRoundedRectangle (plate, 18);
    g.setColour (S.plate); g.fillPath (plateP);
    {
        juce::Graphics::ScopedSaveState clipState (g); g.reduceClipRegion (plateP);
        // ---- the sky behind the sign
        if (print)
        {   // glossy black with Ben-Day dots fading out of one corner in the mood's colour
            const Colour dot = S.ink[mode == 0 ? Accent : mode == 1 ? Drink : Sky].c;
            const float ox = mode == 1 ? 96.0f : 98.0f, oy = mode == 1 ? 98.0f : 0.0f;
            for (int iy = 0; iy < 20; ++iy) for (int ix = 0; ix < 20; ++ix)
            {
                const float x = 3.0f + (float) ix * 5.0f + (iy % 2 ? 2.5f : 0.0f), y = 3.0f + (float) iy * 5.0f;
                const float d = std::sqrt ((x - ox) * (x - ox) + (y - oy) * (y - oy)), rr = 1.9f * (1.0f - d / (mode == 1 ? 70.0f : 62.0f));
                if (rr > 0.25f) { g.setColour (dot.withAlpha (0.8f)); g.fillEllipse (x - rr, y - rr, 2 * rr, 2 * rr); }
            }
            juce::Path gloss; gloss.startNewSubPath (2, 30); gloss.lineTo (30, 2); gloss.lineTo (48, 2); gloss.lineTo (2, 48); gloss.closeSubPath();
            g.setColour (Colour (0x0dffffff)); g.fillPath (gloss);
        }
        else if (light)
        {   // paper: the sky is a wash that pools and thins
            const Colour top = mode == 0 ? Colour (0xff8fcbd6) : mode == 1 ? Colour (0xffb695d0) : Colour (0xff7d86bd);
            const Colour low = mode == 0 ? Colour (0xfff6c98f) : mode == 1 ? Colour (0xfff2a37c) : Colour (0xffc7a6d8);
            juce::ColourGradient sk (top.withAlpha (mode >= 2 ? 0.62f : 0.42f), 0, 2, low.withAlpha (mode == 1 ? 0.55f : 0.3f), 0, 98, false);
            sk.addColour (0.5, (mode == 1 ? Colour (0xfff0a0b8) : top.interpolatedWith (low, 0.5f)).withAlpha (mode == 1 ? 0.4f : mode >= 2 ? 0.3f : 0.12f));
            g.setGradientFill (sk); g.fillRect (plate);
            for (int i = 0; i < 5; ++i)
            {   // blooms where the pigment dried
                const float x = 12.0f + seeded (i * 7 + mode) * 76.0f, y = 10.0f + seeded (i * 7 + 3 + mode) * 30.0f, rr = 9.0f + seeded (i * 7 + 5) * 9.0f;
                g.setGradientFill (juce::ColourGradient (top.withAlpha (0.16f), x, y, top.withAlpha (0.0f), x + rr, y, true)); g.fillEllipse (x - rr, y - rr, 2 * rr, 2 * rr);
            }
        }
        else
        {
            const Colour top = S.ink[mode == 0 ? Sky : Drink].c, low = S.ink[mode <= 1 ? Accent : Frame].c;
            juce::ColourGradient sk (top.withAlpha (mode == 0 ? 0.26f : mode == 1 ? 0.32f : 0.24f), 0, 2, low.withAlpha (mode == 0 ? 0.28f : mode == 1 ? 0.42f : 0.05f), 0, 98, false);
            if (mode == 1) sk.addColour (0.55, S.ink[Glass].c.interpolatedWith (S.ink[Prof].glow, 0.5f).withAlpha (0.24f));
            g.setGradientFill (sk); g.fillRect (plate);
            if (face == Holo) { g.setGradientFill (foil (plate, 0.10f)); g.fillRect (plate);
                                juce::Path sh; sh.startNewSubPath (2, 62); sh.lineTo (62, 2); sh.lineTo (74, 2); sh.lineTo (2, 74); sh.closeSubPath(); g.setColour (Colour (0x0cffffff)); g.fillPath (sh); }
            if (face == Neon)
            {   // the rain, behind everything
                g.setColour (S.ink[Sky].c.withAlpha (0.16f));
                for (int i = 0; i < 16; ++i) { const float x = 6.0f + seeded (i * 5 + 1) * 90.0f, y = 6.0f + seeded (i * 5 + 2) * 84.0f, len = 4.0f + seeded (i * 5 + 3) * 5.0f; g.drawLine (x, y, x - len * 0.32f, y + len, 0.55f); }
            }
        }
        // ---- the mood's sky things
        if (mode == 0)
        {
            if (tubes) { g.setGradientFill (juce::ColourGradient (S.ink[Accent].glow.withAlpha (0.38f), sunX, sunY, S.ink[Accent].glow.withAlpha (0.0f), sunX + 30, sunY, true)); g.fillRect (plate); }
            if (light) { g.setGradientFill (juce::ColourGradient (Colour (0x70ffd98a), sunX, sunY, Colour (0x00ffd98a), sunX + 26, sunY, true)); g.fillRect (plate); }
            juce::Path rays;
            for (int i = 0; i < 12; ++i) { const float a = (float) i * pi / 6.0f + 0.26f, r0 = 10.0f, r1 = i % 2 ? 12.0f : 13.6f; rays.startNewSubPath (sunX + std::cos (a) * r0, sunY + std::sin (a) * r0); rays.lineTo (sunX + std::cos (a) * r1, sunY + std::sin (a) * r1); }
            line (g, S, rays, Accent, 1.25f);
            shape (g, S, ell (sunX, sunY, 7.4f, 7.4f), Accent, 2.0f, print ? 1.0f : 0.75f);
            { juce::Path hl; arcTo (hl, sunX, sunY, 4.4f, pi * 1.05f, pi * 1.55f, true); if (print || light) { g.setColour (Colour (0xfffff6dc).withAlpha (0.9f)); g.strokePath (hl, stroke (0.9f)); } else line (g, S, hl, Hair, 0.7f, 0.8f); }
            // a small cloud up left, a second sliver under the sun
            juce::Path cloud; cloud.startNewSubPath (23, 19.5f); arcTo (cloud, 26.5f, 16.5f, 3.0f, pi * 0.6f, pi * 1.5f, false); arcTo (cloud, 31.5f, 13.5f, 4.2f, pi * 1.05f, pi * 1.95f, false);
            arcTo (cloud, 37.0f, 16.0f, 3.4f, pi * 1.4f, pi * 2.4f, false); cloud.closeSubPath();
            shape (g, S, cloud, Sky, 1.1f, 0.35f);
            juce::Path sliver; sliver.startNewSubPath (76, 35.0f); sliver.lineTo (86, 35.0f); sliver.startNewSubPath (79, 38.0f); sliver.lineTo (84.5f, 38.0f); line (g, S, sliver, Sky, 0.8f, 0.7f);
        }
        else if (mode == 1)
        {   // streak clouds, two birds, the evening's first star (the sun itself sits low, behind the hammock)
            juce::Path streaks; streaks.startNewSubPath (66, 24); streaks.lineTo (88, 24); streaks.startNewSubPath (72, 28); streaks.lineTo (84, 28);
            streaks.startNewSubPath (15, 21); streaks.lineTo (29, 21); streaks.startNewSubPath (19, 17.5f); streaks.lineTo (26, 17.5f);
            line (g, S, streaks, Glass, 0.9f, 0.75f);
            juce::Path birds; for (auto b : { juce::Point<float> (40.0f, 13.0f), juce::Point<float> (47.5f, 17.5f) })
            { birds.startNewSubPath (b.x - 3.0f, b.y - 0.6f); birds.quadraticTo (b.x - 1.4f, b.y - 2.4f, b.x, b.y); birds.quadraticTo (b.x + 1.4f, b.y - 2.4f, b.x + 3.0f, b.y - 0.6f); }
            line (g, S, birds, Hair, 0.8f, 0.9f);
        }
        else if (mode == 2)
        {   // a crescent moon up left
            juce::Path moon; moon.addCentredArc (22.5f, 16.5f, 6.0f, 6.0f, 0, 0.5f, 4.6f, true); moon.addCentredArc (25.6f, 14.6f, 4.9f, 4.9f, 0, 4.25f - 2 * pi, -1.72f, false); moon.closeSubPath();
            if (tubes) { g.setGradientFill (juce::ColourGradient (S.ink[Accent].glow.withAlpha (0.28f), 22, 16, S.ink[Accent].glow.withAlpha (0.0f), 40, 16, true)); g.fillRect (plate); }
            shape (g, S, moon, Accent, 1.5f, 0.7f);
        }
        if (mode >= 1)
        {   // stars: four-point ones (they twinkle in the animated layer) and a dusting of dots
            int n = 0; const auto* tw = twinkles (mode, n);
            for (int i = 0; i < n; ++i)
            {
                const auto st = star4 (tw[i].x, tw[i].y, tw[i].z);
                if (print || light) { g.setColour (S.ink[mode == 1 ? Hair : Accent].c.withAlpha (light ? 0.0f : 1.0f)); g.fillPath (st); if (light) { g.setColour (Colour (0xfffffdf6)); g.fillPath (st); g.setColour (S.key.withAlpha (0.55f)); g.strokePath (st, stroke (0.5f)); } }
                else { g.setColour (S.ink[Sky].glow.withAlpha (0.35f)); g.strokePath (st, stroke (1.8f)); g.setColour (Colour (0xffffffff)); g.fillPath (st); }
            }
            if (mode >= 2)
                for (int i = 0; i < 12; ++i)
                {
                    const float x = 8.0f + seeded (i * 2 + 11 + mode * 31) * 84.0f, y = 6.0f + seeded (i * 2 + 12 + mode * 31) * 30.0f, rr = 0.45f + 0.4f * seeded (i * 9 + 4);
                    if (std::abs (x - (headX + 1)) < 21 && y > 22) continue;   // not in his hair
                    g.setColour ((light ? Colour (0xfffffdf6) : print ? S.ink[Prof].c : S.ink[Hair].c).withAlpha (light ? 0.95f : 0.75f)); g.fillEllipse (x - rr, y - rr, 2 * rr, 2 * rr);
                }
        }
        // ---- the ground and the low sun (DUSK)
        if (mode == 1)
        {
            const auto sun = ell (duskX, duskY, 14.0f, 14.0f);
            if (tubes) { g.setGradientFill (juce::ColourGradient (S.ink[Accent].glow.withAlpha (0.42f), duskX, duskY, S.ink[Accent].glow.withAlpha (0.0f), duskX + 40, duskY, true)); g.fillRect (plate); }
            if (light) { g.setGradientFill (juce::ColourGradient (Colour (0x80ffb070), duskX, duskY, Colour (0x00ffb070), duskX + 34, duskY, true)); g.fillRect (plate); }
            {
                juce::Graphics::ScopedSaveState sunState (g); g.reduceClipRegion (juce::Rectangle<int> (0, 0, 100, 91));
                shape (g, S, sun, Accent, 2.0f, print ? 1.0f : 0.8f);
                juce::Graphics::ScopedSaveState bandState (g); g.reduceClipRegion (sun);   // the bands a setting sun breaks into
                g.setColour (light ? Colour (0xfffbe9dc) : S.plate);
                for (int i = 0; i < 4; ++i) { const float y = 78.0f + (float) i * 3.4f, h = 0.9f + (float) i * 0.45f; g.fillRect (duskX - 15.0f, y, 30.0f, h); }
            }
            juce::Path hz; hz.startNewSubPath (14, 91); hz.lineTo (86, 91); line (g, S, hz, Glass, 1.0f, 0.85f);
            juce::Path sh; sh.startNewSubPath (62, 93.6f); sh.lineTo (78, 93.6f); line (g, S, sh, Accent, 0.8f, 0.7f);
        }
        else
        {
            g.setColour (S.ink[mode == 0 ? Accent : Drink].c.withAlpha (light ? 0.3f : print ? 0.0f : 0.2f)); g.fillPath (ell (50, 93.5f, 30, 2.6f));   // his shadow on the ground
            if (print) { juce::Path gr; gr.startNewSubPath (30, 93.5f); gr.lineTo (70, 93.5f); gr.startNewSubPath (38, 95.6f); gr.lineTo (62, 95.6f); line (g, S, gr, Frame, 0.8f, 0.9f); }
        }
        // ---- the posts (a ball cap each; KITSUNE: the right one wears a fox mask) and the ropes
        for (float x : { 10.5f, 89.5f })
        {
            juce::Path post; post.addRoundedRectangle (x - 2.0f, 27.0f, 4.0f, 72.0f, 2.0f);
            shape (g, S, post, Frame, 1.5f, 0.42f);
            juce::Path rings; rings.startNewSubPath (x - 2.0f, 33.0f); rings.lineTo (x + 2.0f, 33.0f); rings.startNewSubPath (x - 2.0f, 80.0f); rings.lineTo (x + 2.0f, 80.0f);
            line (g, S, rings, Frame, 0.7f, 0.8f);
            if (! (face == Kitsune && (x > 50) == (mode != 0))) shape (g, S, ell (x, 24.0f, 3.3f, 3.3f), Accent, 1.4f, 0.8f);
        }
        {
            juce::Path ropes;
            const float pts[4][4] { { 10.5f, 28.5f, 19, 52 }, { 10.5f, 28.5f, 22.5f, 54.6f }, { 89.5f, 28.5f, 81, 52 }, { 89.5f, 28.5f, 77.5f, 54.6f } };
            for (auto& q : pts) { const auto e = juce::Point<float> (q[2], q[3]).transformedBy (group()); ropes.startNewSubPath (q[0], q[1]); ropes.lineTo (e.x, e.y); }
            line (g, S, ropes, light || print ? Frame : Hair, 0.8f, 0.85f);
        }
        juce::Graphics::ScopedSaveState groupState (g); g.addTransform (group());   // the hammock + the professor, a little larger than the original's
        // ---- the professor's head: round fluffy hair (a cloud of puffs), the face, big round glasses
        {
            const auto H = headT();
            const int nPuff = 9; const float a0 = 2.76f, a1 = 6.68f, ring = 12.6f;
            const float pr[9] { 4.3f, 5.0f, 4.7f, 5.3f, 5.0f, 5.4f, 4.8f, 5.1f, 4.4f };
            juce::Path mass, cloud, curls; mass.addEllipse (-13.0f, -12.5f, 26.0f, 25.0f);
            for (int i = 0; i < nPuff; ++i)
            {
                const float a = a0 + (a1 - a0) * (float) i / (float) (nPuff - 1), cx = std::cos (a) * ring, cy = std::sin (a) * ring;
                mass.addEllipse (cx - pr[i], cy - pr[i], 2 * pr[i], 2 * pr[i]);
                cloud.addCentredArc (cx, cy, pr[i], pr[i], 0, a + pi * 0.5f - 1.12f, a + pi * 0.5f + 1.12f, i == 0);
                if (i % 2 == 1) curls.addCentredArc (cx * 0.93f, cy * 0.93f, pr[i] * 0.45f, pr[i] * 0.45f, 0, a + pi * 0.5f - 1.5f, a + pi * 0.5f + 0.7f, true);
            }
            mass.applyTransform (H); cloud.applyTransform (H); curls.applyTransform (H);
            g.setColour (S.plate); g.fillPath (mass);
            if (print) { g.setColour (S.ink[Hair].glow); g.fillPath (mass, juce::AffineTransform::translation (1.3f, 1.0f)); g.setColour (S.ink[Hair].c); g.fillPath (mass); }
            else if (light) { g.setColour (Colour (0xfffffdfa)); g.fillPath (mass); g.setColour (S.ink[Hair].glow.withAlpha (0.55f)); g.fillPath (mass, juce::AffineTransform::translation (0, 0)); g.setColour (Colour (0xfffffdfa)); g.fillPath (mass, juce::AffineTransform::translation (-1.2f, -1.6f)); }
            else { g.setGradientFill (juce::ColourGradient (S.ink[Hair].c.withAlpha (0.86f), headX - 6, headY - 14, S.ink[Hair].c.interpolatedWith (S.ink[Hair].glow, 0.6f).withAlpha (0.5f), headX + 9, headY + 12, false)); g.fillPath (mass); }
            if (print) { g.setColour (S.ink[Hair].glow); g.strokePath (curls, stroke (1.0f)); g.setColour (S.key); g.strokePath (cloud, stroke (1.1f)); }
            else if (light) { line (g, S, curls, Hair, 0.7f, 0.75f); line (g, S, cloud, Hair, 2.1f); }
            else { g.setColour (S.ink[Hair].glow.interpolatedWith (S.plate, 0.25f)); g.strokePath (curls, stroke (0.75f)); line (g, S, cloud, Hair, 1.9f); }
            // the face, over the hair
            const auto faceP = moved (ell (0, 1.3f, 10.9f, 10.5f), H);
            g.setColour (S.plate); g.fillPath (faceP);
            if (print) { g.setColour (S.ink[Prof].c); g.fillPath (faceP);
                         juce::Graphics::ScopedSaveState shade (g); g.reduceClipRegion (faceP); g.setColour (S.ink[Frame].c.withAlpha (0.55f)); g.fillPath (moved (ell (12.5f, 7.0f, 9.0f, 12.0f), H)); }   // a posterised shadow
            else if (light) { g.setColour (Colour (0xfffff8f4)); g.fillPath (faceP); g.setColour (S.ink[Prof].c.withAlpha (0.35f)); g.fillPath (faceP); }
            else { g.setGradientFill (juce::ColourGradient (S.ink[Prof].c.withAlpha (0.4f), headX - 4, headY - 6, S.ink[Prof].glow.withAlpha (0.16f), headX + 8, headY + 10, false)); g.fillPath (faceP); }
            { juce::Path jaw; jaw.addCentredArc (0, 1.3f, 10.9f, 10.5f, 0, pi * 0.5f - 0.5f, pi * 1.5f + 0.5f, true); jaw.applyTransform (H);
              if (print) { g.setColour (S.key); g.strokePath (jaw, stroke (1.1f)); } else line (g, S, jaw, Prof, 2.0f); }
            {   // the fringe: three soft puffs over the forehead
                juce::Path fr, frLine;
                for (int i = 0; i < 3; ++i) { const float a = pi * (1.26f + 0.24f * (float) i), cx = std::cos (a) * 9.6f, cy = std::sin (a) * 9.6f + 0.6f; fr.addEllipse (cx - 3.6f, cy - 3.6f, 7.2f, 7.2f);
                                              frLine.addCentredArc (cx, cy, 3.6f, 3.6f, 0, a + pi * 1.5f - 1.05f, a + pi * 1.5f + 1.05f, true); }
                fr.applyTransform (H); frLine.applyTransform (H);
                juce::Graphics::ScopedSaveState fs (g); g.reduceClipRegion (mass);
                g.setColour (S.plate); g.fillPath (fr);
                if (print) { g.setColour (S.ink[Hair].c); g.fillPath (fr); g.setColour (S.key); g.strokePath (frLine, stroke (0.9f)); }
                else if (light) { g.setColour (Colour (0xfffffdfa)); g.fillPath (fr); line (g, S, frLine, Hair, 1.0f); }
                else { g.setColour (S.ink[Hair].c.withAlpha (0.93f)); g.fillPath (fr); g.setColour (S.ink[Hair].glow.withAlpha (0.9f)); g.strokePath (frLine, stroke (0.7f)); }
            }
            // cheeks
            for (float sx : { -1.0f, 1.0f }) { g.setColour (S.blush.withAlpha (print ? 1.0f : light ? 0.6f : 0.75f)); g.fillPath (moved (ell (sx * 8.0f, 5.6f, 2.2f, 1.4f), H)); }
            // the glasses: round frames, a bridge, the arms into the hair; SUNRISE they are shades, MIDNIGHT the lenses glow
            juce::Path lenses, extra; lenses.addEllipse (-lensX - lensR, lensY - lensR, 2 * lensR, 2 * lensR); lenses.addEllipse (lensX - lensR, lensY - lensR, 2 * lensR, 2 * lensR);
            extra.startNewSubPath (-lensX + lensR - 0.3f, lensY - 0.6f); extra.quadraticTo (0, lensY - 2.0f, lensX - lensR + 0.3f, lensY - 0.6f);
            extra.startNewSubPath (-lensX - lensR, lensY - 0.4f); extra.lineTo (-11.6f, lensY - 1.6f); extra.startNewSubPath (lensX + lensR, lensY - 0.4f); extra.lineTo (11.6f, lensY - 1.6f);
            lenses.applyTransform (H); extra.applyTransform (H);
            if (mode == 0) { g.setColour (S.plate); g.fillPath (lenses); g.setColour (light ? S.key.withAlpha (0.88f) : print ? S.key : S.ink[Glass].c.withAlpha (0.5f)); g.fillPath (lenses); }
            else if (mode == 2) { g.setColour (S.plate); g.fillPath (lenses); g.setColour (S.ink[Sky].c.withAlpha (light ? 0.75f : print ? 1.0f : 0.6f)); g.fillPath (lenses); }
            else { g.setColour ((light || print ? Colour (0xffffffff) : S.ink[Glass].c).withAlpha (light ? 0.7f : print ? 0.0f : 0.14f)); g.fillPath (lenses); }
            line (g, S, extra, Glass, 1.0f);
            line (g, S, lenses, Glass, mode == 0 ? 1.7f : 1.4f);
            if (mode == 0)
            {   // glints across the shades
                juce::Path gl; for (float sx : { -lensX, lensX }) { gl.startNewSubPath (sx - 2.4f, lensY + 0.6f); gl.lineTo (sx - 0.2f, lensY - 2.4f); gl.startNewSubPath (sx - 0.4f, lensY + 1.6f); gl.lineTo (sx + 1.0f, lensY - 0.4f); }
                gl.applyTransform (H); g.setColour (Colour (0xffffffff).withAlpha (0.9f)); g.strokePath (gl, stroke (0.7f));
            }
            else if (mode == 2)
            {   // bright pupils in the lit lenses
                for (float sx : { -lensX, lensX }) { const auto c = headPoint (sx, lensY + 0.3f); g.setColour (light ? S.key : Colour (0xffffffff)); g.fillEllipse (c.x - 1.35f, c.y - 1.35f, 2.7f, 2.7f);
                                                     if (light) { g.setColour (Colour (0xffffffff)); g.fillEllipse (c.x - 0.1f, c.y - 0.9f, 0.9f, 0.9f); } }
            }
            else
            {   // happy closed eyes
                juce::Path e; for (float sx : { -lensX, lensX }) arcTo (e, sx, lensY + 1.3f, 2.3f, pi * 1.12f, pi * 1.88f, true);
                e.applyTransform (H); feature (g, S, e, 1.15f);
            }
            { juce::Path sm; arcTo (sm, 0, 5.0f, 3.0f, 0.25f, pi - 0.25f, true); sm.applyTransform (H); feature (g, S, sm, 1.2f); }
        }
        // ---- the raised arm (a lab-coat sleeve with its cuff), the prop, the hand
        {
            juce::Path mid, arm; mid.startNewSubPath (48.5f, 60.0f); mid.quadraticTo (57.0f, 54.0f, handX, handY + 3.0f);
            juce::PathStrokeType (6.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath (arm, mid);
            shape (g, S, arm, Prof, 1.5f, print ? 1.0f : 0.5f);
            juce::Path cuff; cuff.startNewSubPath (handX - 3.6f, handY + 5.4f); cuff.lineTo (handX + 2.6f, handY + 7.0f); line (g, S, cuff, Accent, 0.9f);
        }
        if (mode <= 2)
        {   // the flask: neck + lip, the cone, the potion with its level line, bubbles inside, a glint on the glass
            const auto T = juce::AffineTransform::scale (flaskK).rotated (flaskRot).translated (handX, handY);
            juce::Path flask; flask.startNewSubPath (-1.8f, -16); flask.lineTo (-1.8f, -10); flask.lineTo (-7, -1); flask.quadraticTo (-8, 1.5f, -5, 1.5f); flask.lineTo (5, 1.5f); flask.quadraticTo (8, 1.5f, 7, -1); flask.lineTo (1.8f, -10); flask.lineTo (1.8f, -16); flask.closeSubPath();
            juce::Path liq; liq.startNewSubPath (-4.1f, -6.0f); liq.quadraticTo (-2.0f, -7.0f, 0, -6.0f); liq.quadraticTo (2.0f, -5.0f, 4.1f, -6.0f); liq.lineTo (7, -1); liq.quadraticTo (8, 1.5f, 5, 1.5f); liq.lineTo (-5, 1.5f); liq.quadraticTo (-8, 1.5f, -7, -1); liq.closeSubPath();
            flask.applyTransform (T); liq.applyTransform (T);
            if (tubes) { g.setGradientFill (juce::ColourGradient (S.ink[Prop].glow.withAlpha (0.34f), handX, handY - 6, S.ink[Prop].glow.withAlpha (0.0f), handX + 19, handY - 6, true)); g.fillRect (plate); }
            g.setColour (S.plate); g.fillPath (flask);
            g.setColour (light ? Colour (0xb0ffffff) : print ? S.ink[Prof].c : S.ink[Hair].c.withAlpha (0.12f)); g.fillPath (flask);
            if (light) { g.setColour (S.ink[Prop].c.withAlpha (0.75f)); g.fillPath (liq); g.setColour (S.ink[Prop].c.withAlpha (0.3f)); g.fillPath (liq, juce::AffineTransform::translation (0.6f, 0.8f)); }
            else { g.setColour (print ? S.ink[Prop].c : S.ink[Prop].c.withAlpha (0.78f)); g.fillPath (liq); }
            g.setColour ((light ? Colour (0xffffffff) : print ? S.ink[Prof].c : Colour (0xffffffff)).withAlpha (0.9f));
            for (auto b : { juce::Point<float> (-2.4f, -2.2f), juce::Point<float> (1.6f, -3.6f), juce::Point<float> (3.2f, -0.8f) }) { const auto c = b.transformedBy (T); g.fillEllipse (c.x - 0.8f, c.y - 0.8f, 1.6f, 1.6f); }
            if (print) { g.setColour (S.key); g.strokePath (flask, stroke (1.0f)); g.setColour (S.ink[Prop].glow); g.strokePath (flask, stroke (0.9f), juce::AffineTransform::translation (1.1f, 0.8f)); g.setColour (S.ink[Prof].c); g.strokePath (flask, stroke (1.1f)); }
            else line (g, S, flask, light ? Glass : Prop, 1.5f);
            juce::Path lip; lip.startNewSubPath (-3.0f, -16); lip.lineTo (3.0f, -16); lip.applyTransform (T); line (g, S, lip, light ? Glass : print ? Hair : Prop, 1.3f);
            juce::Path glint; glint.startNewSubPath (-4.4f, -1.2f); glint.lineTo (-2.6f, -4.4f); glint.applyTransform (T); g.setColour (Colour (0xffffffff).withAlpha (print ? 0.0f : 0.85f)); g.strokePath (glint, stroke (0.7f));
        }
        else if (mode == 3)
        {   // TRAP: the stacked double foam cup, ribbed rims, the straw, the purple top with two ice cubes
            const auto& Q = propPaths();
            const auto T = juce::AffineTransform::scale (1.22f).rotated (0.06f).translated (handX, handY);
            if (tubes) { g.setGradientFill (juce::ColourGradient (S.ink[Drink].glow.withAlpha (0.36f), handX, handY - 9, S.ink[Drink].glow.withAlpha (0.0f), handX + 21, handY - 9, true)); g.fillRect (plate); }
            const Colour foam = light ? Colour (0xfffffefb) : print ? S.ink[Prof].c : S.ink[Hair].c;
            auto body = [&] (const juce::Path& src, float lw)
            {
                const auto p = moved (src, T);
                g.setColour (S.plate); g.fillPath (p);
                if (print) { g.setColour (S.ink[Sky].c); g.fillPath (p, juce::AffineTransform::translation (1.2f, 0.9f)); }
                g.setColour (foam.withAlpha (tubes ? 0.42f : 1.0f)); g.fillPath (p);
                if (print) { g.setColour (S.key); g.strokePath (p, stroke (lw * 0.7f)); } else line (g, S, p, light ? Glass : Hair, lw);
            };
            { const auto st = moved (Q.straw, T); shape (g, S, st, light ? Frame : Glass, 0.9f, print ? 1.0f : 1.3f); }
            body (Q.cupOuter, 1.3f);
            { g.setColour ((light ? S.ink[Hair].glow : print ? S.ink[Frame].c : S.ink[Hair].glow).withAlpha (light ? 0.7f : print ? 0.8f : 0.35f)); g.fillPath (moved (Q.cupShade, T));
              juce::Path flutes; for (float fx : { -3.4f, -1.2f, 1.0f }) { flutes.startNewSubPath (fx * 1.25f, -9.0f); flutes.lineTo (fx, 1.2f); }
              flutes.applyTransform (T); if (print || light) { g.setColour (S.key.withAlpha (0.5f)); g.strokePath (flutes, stroke (0.45f)); } else line (g, S, flutes, Hair, 0.45f, 0.6f); }
            body (Q.cupInner, 1.0f);
            body (Q.cupLipLow, 0.9f);
            body (Q.cupRing, 0.9f);
            { const auto mouth = moved (Q.cupMouth, T);
              g.setColour (S.plate); g.fillPath (mouth); g.setColour (S.ink[Drink].c.withAlpha (light ? 0.85f : print ? 1.0f : 0.9f)); g.fillPath (mouth);
              for (int i = 0; i < 2; ++i) { const auto ice = moved (Q.ice, juce::AffineTransform::rotation (0.5f * (float) i - 0.2f).translated (-2.6f + (float) i * 4.4f, -14.0f).followedBy (T));
                                            g.setColour (Colour (0xffffffff).withAlpha (0.95f)); g.fillPath (ice); }
              if (print || light) { g.setColour (S.key); g.strokePath (mouth, stroke (0.6f)); } else line (g, S, mouth, Drink, 0.8f); }
            { const auto ribs = moved (Q.cupRibs, T); g.setColour (light || print ? S.key.withAlpha (0.6f) : S.ink[Hair].glow.withAlpha (0.9f)); g.fillPath (ribs); }
            { const auto st = moved (Q.straw, T); juce::Graphics::ScopedSaveState ss2 (g); g.reduceClipRegion (juce::Rectangle<int> (0, 0, 100, juce::roundToInt (handY - 16.6f * 1.22f)));
              shape (g, S, st, light ? Frame : Glass, 0.9f, print ? 1.0f : 1.3f); }   // the straw above the rim, in front
        }
        else
        {   // HIP HOP: the 40 in its crumpled paper bag: neck, lip ring and crown cap out of the top
            const auto& Q = propPaths();
            const auto T = juce::AffineTransform::scale (1.18f).rotated (-0.06f).translated (handX, handY);
            if (tubes) { g.setGradientFill (juce::ColourGradient (S.ink[Accent].glow.withAlpha (0.34f), handX, handY - 8, S.ink[Accent].glow.withAlpha (0.0f), handX + 21, handY - 8, true)); g.fillRect (plate); }
            const int glassRole = print ? Sky : light ? Prop : face == Kitsune ? Stripe : Sky;
            shape (g, S, moved (Q.neck, T), glassRole, 1.1f, print ? 1.0f : 0.9f);
            { g.setColour (Colour (0xffffffff).withAlpha (print ? 0.0f : 0.8f)); g.fillPath (moved (Q.glint, T)); }
            shape (g, S, moved (Q.ring, T), glassRole, 0.8f, 1.0f);
            shape (g, S, moved (Q.cap, T), light ? Accent : Hair, 0.9f, print ? 1.0f : 1.4f);
            { g.setColour (S.key.withAlpha (light || print ? 0.7f : 0.5f)); g.fillPath (moved (Q.capTeeth, T)); }
            const auto bag = moved (Q.bag, T);
            shape (g, S, bag, Accent, 1.5f, print ? 1.0f : light ? 0.9f : 0.7f);
            { g.setColour ((light ? Colour (0xffffffff) : S.ink[Accent].c.interpolatedWith (Colour (0xffffffff), 0.6f)).withAlpha (light ? 0.6f : print ? 0.0f : 0.8f)); g.fillPath (moved (Q.folds, T));
              g.setColour ((light || print ? S.key : S.plate).withAlpha (light ? 0.5f : print ? 0.8f : 0.5f)); g.fillPath (moved (Q.creases, T)); }
        }
        {   // the hand round it
            const auto hand = ell (handX, handY + 1.4f, 3.3f, 2.8f);
            g.setColour (S.plate); g.fillPath (hand);
            g.setColour (light ? Colour (0xfffffaf6) : print ? S.ink[Prof].c : S.ink[Hair].c.withAlpha (0.55f)); g.fillPath (hand);
            if (print) { g.setColour (S.key); g.strokePath (hand, stroke (0.8f)); } else line (g, S, hand, Prof, 1.1f);
            juce::Path fingers; fingers.startNewSubPath (handX - 1.0f, handY + 0.2f); fingers.lineTo (handX - 1.0f, handY + 2.6f); fingers.startNewSubPath (handX + 0.9f, handY + 0.2f); fingers.lineTo (handX + 0.9f, handY + 2.6f);
            g.setColour ((light || print ? S.key : S.ink[Prof].glow).withAlpha (0.75f)); g.strokePath (fingers, stroke (0.45f));
        }
        // ---- the hammock: a striped sling with a bright top edge, knots at the ends, a tassel fringe
        {
            juce::Path bed; bed.startNewSubPath (19, 52); bed.quadraticTo (50, 72, 81, 52); bed.quadraticTo (50, 97, 19, 52); bed.closeSubPath();
            g.setColour (S.plate); g.fillPath (bed);
            if (print) { g.setColour (S.ink[Frame].glow); g.fillPath (bed, juce::AffineTransform::translation (1.3f, 1.0f)); g.setColour (S.plate); g.fillPath (bed); }
            if (light) { g.setColour (Colour (0xfffffaf6)); g.fillPath (bed); }
            {
                juce::Graphics::ScopedSaveState bs (g); g.reduceClipRegion (bed);
                for (int i = -4; i < 16; ++i)
                {
                    juce::Path st; const float x = (float) i * 6.4f; st.startNewSubPath (x, 46); st.lineTo (x + 6.4f, 46); st.lineTo (x + 17.4f, 98); st.lineTo (x + 11.0f, 98); st.closeSubPath();
                    const bool odd = (i & 1) != 0;
                    if (print) g.setColour (S.ink[odd ? Frame : Stripe].c);
                    else if (light) g.setColour (S.ink[odd ? Frame : Stripe].c.withAlpha (odd ? 0.55f : 0.5f));
                    else g.setColour (S.ink[odd ? Frame : Stripe].c.withAlpha (odd ? 0.5f : 0.3f));
                    g.fillPath (st);
                    if (print) { g.setColour (S.key); g.strokePath (st, stroke (0.5f)); }
                }
                g.setGradientFill (juce::ColourGradient ((light ? Colour (0xff6a4050) : S.plate).withAlpha (0.0f), 0, 62, (light ? Colour (0xff6a4050) : S.plate).withAlpha (light ? 0.22f : 0.5f), 0, 76, false)); g.fillRect (0, 50, 100, 50);
            }
            if (print) { juce::Path top; top.startNewSubPath (19, 52); top.quadraticTo (50, 72, 81, 52); g.setColour (S.ink[Prof].c); g.strokePath (bed, stroke (1.5f)); g.setColour (S.key); g.strokePath (bed, stroke (0.5f)); juce::ignoreUnused (top); }
            else line (g, S, bed, Frame, 2.2f);
            juce::Path fringe;
            for (int i = 1; i <= 9; ++i) { const float u = 0.12f + 0.76f * (float) (i - 1) / 8.0f, x = (1 - u) * (1 - u) * 19 + 2 * u * (1 - u) * 50 + u * u * 81, y = (1 - u) * (1 - u) * 52 + 2 * u * (1 - u) * 97 + u * u * 52;
                                           fringe.startNewSubPath (x, y + 1.2f); fringe.lineTo (x + (u - 0.5f) * 1.6f, y + 4.6f); }
            line (g, S, fringe, Stripe, 0.9f, 0.9f);
            for (float x : { 19.0f, 81.0f }) shape (g, S, ell (x, 52.0f, 2.0f, 2.0f), Accent, 1.0f, 1.0f);
        }
        // ---- on top of the sling: his lab-coat collar + bow tie, the tail tip out of the far end
        {
            juce::Path col; col.startNewSubPath (26.5f, 54.6f); col.lineTo (34.5f, 52.6f); col.lineTo (38.5f, 58.2f); col.lineTo (43.5f, 52.6f); col.lineTo (49.8f, 55.2f); col.quadraticTo (38.5f, 62.4f, 26.5f, 54.6f); col.closeSubPath();
            g.setColour (S.plate); g.fillPath (col);
            if (print) { g.setColour (S.ink[Hair].glow); g.fillPath (col, juce::AffineTransform::translation (1.1f, 0.9f)); }
            g.setColour (light ? Colour (0xfffffdfa) : print ? S.ink[Hair].c : S.ink[Hair].c.withAlpha (0.5f)); g.fillPath (col);
            if (print) { g.setColour (S.key); g.strokePath (col, stroke (0.8f)); } else line (g, S, col, Hair, 1.2f);
            juce::Path bow; bow.startNewSubPath (38.5f, 56.4f); bow.lineTo (35.0f, 54.2f); bow.lineTo (35.0f, 59.0f); bow.closeSubPath(); bow.startNewSubPath (38.5f, 56.4f); bow.lineTo (42.0f, 54.2f); bow.lineTo (42.0f, 59.0f); bow.closeSubPath();
            shape (g, S, bow, Drink, 0.8f, print ? 1.0f : 1.6f);
            juce::Path tail; tail.startNewSubPath (69, 56.5f); tail.quadraticTo (79, 50, 84.5f, 54); tail.quadraticTo (88.5f, 58, 91.5f, 57); tail.quadraticTo (89.5f, 62, 85.5f, 61); tail.quadraticTo (83.5f, 64, 79.5f, 61.5f); tail.quadraticTo (75.5f, 62, 72.5f, 60.5f); tail.closeSubPath();
            g.setColour (S.plate); g.fillPath (tail);
            if (print) { g.setColour (S.ink[Prof].glow); g.fillPath (tail, juce::AffineTransform::translation (1.1f, 0.9f)); }
            g.setColour (light ? Colour (0xfffffaf6) : print ? S.ink[Prof].c : S.ink[Hair].c.withAlpha (0.8f)); g.fillPath (tail);
            if (print) { g.setColour (S.key); g.strokePath (tail, stroke (0.8f)); } else line (g, S, tail, Prof, 1.5f);
        }
        if (face == Kitsune)
        {   // the fox mask on the right post: ice white, red ears, eye slits and whiskers, an amber brow mark
            const bool left = mode == 0;   // (SUNRISE: the sun is by the right post)
            const auto T = juce::AffineTransform::scale (0.95f).rotated (left ? -0.12f : 0.12f).translated (left ? 10.9f : 89.1f, 23.0f).followedBy (group().inverted());
            juce::Path fx; fx.startNewSubPath (-5.6f, -0.6f); fx.lineTo (-6.4f, -7.4f); fx.lineTo (-2.4f, -4.2f); fx.lineTo (2.4f, -4.2f); fx.lineTo (6.4f, -7.4f); fx.lineTo (5.6f, -0.6f);
            fx.quadraticTo (4.6f, 3.6f, 0, 6.2f); fx.quadraticTo (-4.6f, 3.6f, -5.6f, -0.6f); fx.closeSubPath(); fx.applyTransform (T);
            g.setColour (S.plate); g.fillPath (fx); g.setColour (Colour (0xfff4fbff)); g.fillPath (fx);
            juce::Path marks; marks.startNewSubPath (-4.2f, -0.6f); marks.quadraticTo (-2.8f, -1.8f, -1.3f, -0.2f); marks.startNewSubPath (4.2f, -0.6f); marks.quadraticTo (2.8f, -1.8f, 1.3f, -0.2f);
            marks.startNewSubPath (-5.3f, -5.6f); marks.lineTo (-3.6f, -4.0f); marks.startNewSubPath (5.3f, -5.6f); marks.lineTo (3.6f, -4.0f);
            marks.startNewSubPath (-3.6f, 2.0f); marks.lineTo (-1.8f, 2.6f); marks.startNewSubPath (3.6f, 2.0f); marks.lineTo (1.8f, 2.6f);
            marks.applyTransform (T); g.setColour (Colour (0xffe01818)); g.strokePath (marks, stroke (0.9f));
            const auto nose = juce::Point<float> (0, 4.4f).transformedBy (T), brow = juce::Point<float> (0, -2.2f).transformedBy (T);
            g.fillEllipse (nose.x - 0.8f, nose.y - 0.8f, 1.6f, 1.6f); g.setColour (Colour (0xffffb35c)); g.fillEllipse (brow.x - 0.8f, brow.y - 0.8f, 1.6f, 1.6f);
            for (auto [m, al] : { std::pair<float, float> (3.4f, 0.14f), std::pair<float, float> (2.0f, 0.3f) }) { g.setColour (Colour (0xff8fd0ff).withAlpha (al)); g.strokePath (fx, stroke (m)); }
            g.setColour (Colour (0xffff2e2e)); g.strokePath (fx, stroke (0.9f));
        }
    }
    // ---- the sign's own rim
    {
        juce::Path fr; fr.addRoundedRectangle (plate.reduced (1.6f), 16.6f);
        if (print) { g.setColour (S.ink[Frame].glow); g.strokePath (fr, stroke (1.6f), juce::AffineTransform::translation (1.0f, 0.8f)); g.setColour (S.ink[Hair].c); g.strokePath (fr, stroke (1.9f)); }
        else if (light) { g.setColour (S.ink[Frame].c.withAlpha (0.28f)); g.strokePath (fr, stroke (4.0f)); g.setColour (S.key.withAlpha (0.8f)); g.strokePath (fr, stroke (0.9f));
                          g.setColour (S.key.withAlpha (0.3f)); g.strokePath (fr, stroke (0.6f), juce::AffineTransform::translation (0.7f, -0.5f)); }
        else line (g, S, fr, Frame, 1.7f);
    }
}
// the logo at r (the editor draws it 62 x 62): the cached still + this frame's bubbles, twinkles, pulses and the odd flicker
inline void professorSign (juce::Graphics& g, juce::Rectangle<float> r, int face, int mode, double t)
{
    using namespace sign;
    const Style& S = styleFor (face); const bool light = S.light, print = face == Mask; const float tf = (float) t;
    mode = juce::jlimit (0, 4, mode);
    {   // the still sign, rendered once per face, mood and pixel size
        static std::map<std::tuple<int, int, int>, juce::Image> cache;
        const float scale = juce::jlimit (1.0f, 4.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        const int px = juce::jmax (8, juce::roundToInt (r.getWidth() * scale));
        const std::tuple<int, int, int> key { face, mode, px };
        auto it = cache.find (key);
        if (it == cache.end())
        {
            if (cache.size() > 60) cache.clear();
            juce::Image img (juce::Image::ARGB, px, px, true, juce::SoftwareImageType());
            { juce::Graphics ig (img); ig.addTransform (juce::AffineTransform::scale ((float) px / 100.0f)); professorSignStatic (ig, face, mode); }
            it = cache.emplace (key, img).first;
        }
        g.setOpacity (1.0f);
        g.drawImageTransformed (it->second, juce::AffineTransform::scale (r.getWidth() / (float) px).translated (r.getX(), r.getY()));
    }
    juce::Graphics::ScopedSaveState ss (g);
    g.addTransform (juce::AffineTransform::scale (r.getWidth() / 100.0f).translated (r.getX(), r.getY()));
    const Colour hot = light ? S.key : print ? S.ink[Accent].c : Colour (0xffffffff);
    // twinkling stars: a cross that swells and fades over each still star
    {
        int n = 0; const auto* tw = twinkles (mode, n);
        for (int i = 0; i < n; ++i)
        {
            const float k = 0.5f + 0.5f * std::sin (tf * (1.7f + 0.4f * (float) i) + (float) i * 2.1f), z = tw[i].z * (0.9f + 0.9f * k);
            if (! light && ! print) { g.setColour (S.ink[Sky].glow.withAlpha (0.35f * k)); g.drawLine (tw[i].x - z, tw[i].y, tw[i].x + z, tw[i].y, 1.5f); g.drawLine (tw[i].x, tw[i].y - z, tw[i].x, tw[i].y + z, 1.5f); }
            g.setColour (hot.withAlpha ((light ? 0.5f : 0.95f) * k)); g.drawLine (tw[i].x - z, tw[i].y, tw[i].x + z, tw[i].y, 0.55f); g.drawLine (tw[i].x, tw[i].y - z, tw[i].x, tw[i].y + z, 0.55f);
        }
    }
    if (mode == 0)
    {   // the sun breathes
        const float k = 0.5f + 0.5f * std::sin (tf * 1.6f);
        if (! light && ! print) { g.setGradientFill (juce::ColourGradient (S.ink[Accent].c.withAlpha (0.10f + 0.16f * k), sunX, sunY, S.ink[Accent].c.withAlpha (0.0f), sunX + 15, sunY, true)); g.fillEllipse (sunX - 15, sunY - 15, 30, 30); }
        else { g.setColour ((print ? S.ink[Prof].c : Colour (0xffffffff)).withAlpha (0.25f + 0.45f * k)); g.drawEllipse (sunX - 4.6f, sunY - 4.6f, 9.2f, 9.2f, 0.7f); }
    }
    else if (mode == 1)
    {   // the low sun's glow breathes, its reflection shimmers
        const float k = 0.5f + 0.5f * std::sin (tf * 1.3f);
        if (! light && ! print) { g.setGradientFill (juce::ColourGradient (S.ink[Accent].c.withAlpha (0.08f + 0.12f * k), duskX, duskY, S.ink[Accent].c.withAlpha (0.0f), duskX + 20, duskY, true)); g.fillEllipse (duskX - 20, duskY - 20, 40, 40); }
        const float w = 5.0f + 3.0f * std::sin (tf * 2.2f);
        g.setColour ((light ? S.ink[Accent].c.darker (0.2f) : print ? S.ink[Prof].c : S.ink[Accent].c.interpolatedWith (Colour (0xffffffff), 0.5f)).withAlpha (0.55f + 0.35f * k)); g.drawLine (duskX - w, 95.4f, duskX + w, 95.4f, 0.6f);
    }
    if (! light && ! print)
    {   // an old tube: the left post's cap light stutters every few seconds
        const float ph = std::fmod (tf * 0.19f, 1.0f);
        if (ph > 0.94f && std::sin (tf * 80.0f) > 0.0f && ! (face == Kitsune && mode == 0)) { g.setColour (S.plate.withAlpha (0.7f)); g.fillEllipse (10.5f - 4.2f, 24.0f - 4.2f, 8.4f, 8.4f); }
    }
    g.addTransform (group());   // from here: things he wears and holds
    if (mode <= 2)
    {   // bubbles out of the flask's mouth (MIDNIGHT: a busier brew)
        const auto mouth = juce::Point<float> (0, -17.0f).transformedBy (juce::AffineTransform::scale (flaskK).rotated (flaskRot).translated (handX, handY));
        const int n = mode == 2 ? 5 : 3;
        for (int i = 0; i < n; ++i)
        {
            const float ph = std::fmod (tf * 0.5f + (float) i / (float) n, 1.0f), x = mouth.x + std::sin (tf * 2.0f + (float) i * 2.1f) * (1.0f + ph * 2.6f), y = mouth.y - 1.0f - ph * 11.5f, rr = 0.9f + ph * 1.5f, a = 1.0f - ph * ph;
            if (light) { g.setColour (S.ink[Prop].c.withAlpha (0.3f * a)); g.fillEllipse (x - rr, y - rr, 2 * rr, 2 * rr); g.setColour (S.key.withAlpha (0.8f * a)); g.drawEllipse (x - rr, y - rr, 2 * rr, 2 * rr, 0.5f); }
            else if (print) { g.setColour (S.ink[Prop].glow.withAlpha (a)); g.drawEllipse (x - rr + 0.7f, y - rr + 0.5f, 2 * rr, 2 * rr, 0.7f); g.setColour (S.ink[Prof].c.withAlpha (a)); g.drawEllipse (x - rr, y - rr, 2 * rr, 2 * rr, 0.8f); }
            else { g.setColour (S.ink[Prop].glow.withAlpha (0.3f * a)); g.drawEllipse (x - rr, y - rr, 2 * rr, 2 * rr, 2.0f); g.setColour (S.ink[Prop].c.interpolatedWith (Colour (0xffffffff), 0.4f).withAlpha (a)); g.drawEllipse (x - rr, y - rr, 2 * rr, 2 * rr, 0.75f); }
        }
    }
    if (mode == 0)
    {   // a glint slides across the shades now and then
        const float ph = std::fmod (tf * 0.22f, 1.0f);
        if (ph < 0.18f)
            for (float sx : { -lensX, lensX })
            {
                const float u = ph / 0.18f, dx = -3.0f + 6.0f * u; const auto p0 = headPoint (sx + dx - 0.9f, lensY + 2.0f), p1 = headPoint (sx + dx + 0.9f, lensY - 2.0f);
                g.setColour (Colour (0xffffffff).withAlpha (std::sin (u * juce::MathConstants<float>::pi))); g.drawLine (p0.x, p0.y, p1.x, p1.y, 0.8f);
            }
    }
    else if (mode == 2)
    {   // the glasses glow and dim
        const float k = 0.5f + 0.5f * std::sin (tf * 3.0f);
        for (float sx : { -lensX, lensX })
        {
            const auto c = headPoint (sx, lensY);
            if (light || print) { g.setColour ((print ? S.ink[Prof].c : Colour (0xffffffff)).withAlpha (0.2f + 0.5f * k)); g.drawEllipse (c.x - 2.6f, c.y - 2.6f, 5.2f, 5.2f, 0.6f); }
            else { g.setGradientFill (juce::ColourGradient (S.ink[Sky].c.interpolatedWith (Colour (0xffffffff), 0.3f).withAlpha (0.25f + 0.5f * k), c.x, c.y, S.ink[Sky].c.withAlpha (0.0f), c.x + 7.5f, c.y, true)); g.fillEllipse (c.x - 7.5f, c.y - 7.5f, 15, 15); }
        }
    }
    else if (mode >= 3)
    {   // TRAP: the ice glints in the cup; HIP HOP: the bottle cap catches the light
        const auto at = mode == 3 ? juce::Point<float> (-1.6f, -14.4f).transformedBy (juce::AffineTransform::scale (1.22f).rotated (0.06f).translated (handX, handY))
                                  : juce::Point<float> (1.4f, -21.6f).transformedBy (juce::AffineTransform::scale (1.18f).rotated (-0.06f).translated (handX, handY));
        const float k = std::pow (0.5f + 0.5f * std::sin (tf * 2.4f), 3.0f), z = 1.0f + 2.6f * k;
        if (! light && ! print) { g.setColour (S.ink[mode == 3 ? Drink : Accent].glow.withAlpha (0.4f * k)); g.drawLine (at.x - z, at.y, at.x + z, at.y, 1.4f); g.drawLine (at.x, at.y - z, at.x, at.y + z, 1.4f); }
        g.setColour ((light ? Colour (0xffffffff) : print ? S.ink[Prof].c : Colour (0xffffffff)).withAlpha (0.95f * k)); g.drawLine (at.x - z, at.y, at.x + z, at.y, 0.6f); g.drawLine (at.x, at.y - z, at.x, at.y + z, 0.6f);
    }
}

// ---------------------------------------------------------------- the mood ghost, anime neon, per face
// The same ghost (dome head, wavy hem, the bob) and the same Face expression as drawMoodGhost, drawn the way an anime character stands
// under neon: a bright cel-shaded body with coloured rim lights, a lit tube round him, big glossy eyes in the mood's colour (iris,
// pupil, lid shadow, two highlights, lashes), hatched blush, a cat mouth or an open one with a tongue and a fang, tears / sweat /
// twinkles. HOLO: a clear foil raincoat hood. NEON RAIN: pink + cyan rim lights, rain round him. MASK: the print's
// off-register plates and the glossy black zip mask (the zip is his mouth). DREAM: wine ink + pastel washes on paper. KITSUNE: a fox
// mask worn on his head.
inline void neonGhost (juce::Graphics& g, juce::Rectangle<float> r, const Face& F, double t, int face, float ear = 0)
{
    juce::Graphics::ScopedSaveState ss (g);
    const float s = r.getWidth() / 100.0f, tf = (float) t, pi = juce::MathConstants<float>::pi;
    g.addTransform (juce::AffineTransform::scale (s).translated (r.getX(), r.getY()));
    const Colour mood = rgba ((int) F.hue[0], (int) F.hue[1], (int) F.hue[2], 1.0f), white (0xffffffff);
    const bool light = face == Dream, print = face == Mask;
    const Colour rimA = face == Kitsune ? Colour (0xffff2e2e) : face == Neon ? Colour (0xffff3fa8) : print ? Colour (0xffff3b3b) : light ? Colour (0xfff29cb8) : Colour (0xffc9b8ff);
    const Colour rimB = face == Kitsune ? Colour (0xffbfe8ff) : face == Neon ? Colour (0xff3de3ff) : print ? Colour (0xfff2c14e) : light ? Colour (0xff5fc8d8) : Colour (0xff9ff5e1);
    const Colour bodyHi = print ? Colour (0xfffcf4e2) : light ? Colour (0xfffffefc) : white;
    const Colour bodyLo = face == Kitsune ? Colour (0xffe9f6ff) : face == Neon ? Colour (0xfff8eeff) : print ? Colour (0xfffcf4e2) : light ? Colour (0xfffff0f3) : Colour (0xfff3ecff);
    const Colour shade = face == Kitsune ? Colour (0xffb9dcf5) : face == Neon ? Colour (0xffd3c2f6) : print ? Colour (0xff49b6c9) : light ? Colour (0xfff6cdd8) : Colour (0xffd2c4ff);
    const Colour ink = face == Kitsune ? Colour (0xff2a0a10) : face == Neon ? Colour (0xff1c0c30) : print ? Colour (0xff0b0909) : light ? Colour (0xff3a2230) : Colour (0xff2a1b52);
    const float bob = std::sin (tf * 2.1f) * 2.5f * (1 - F.sweat * 0.5f) + std::sin (tf * 23) * F.sweat * 0.5f;
    // behind him: a pool of dark so he reads on the picture (DREAM: a pool of paper), then his glow
    if (light) { g.setGradientFill (juce::ColourGradient (Colour (0xd0fffaf6), 50, 50, Colour (0x00fffaf6), 104, 50, true)); g.fillEllipse (-6, -6, 112, 112); }
    else
    {
        g.setGradientFill (juce::ColourGradient (Colour (0x9a05030a), 50, 50, Colour (0x0005030a), 106, 50, true)); g.fillEllipse (-8, -8, 116, 116);
        juce::ColourGradient gl (rimA.withAlpha (0.34f), 42, 50, rimA.withAlpha (0.0f), 98, 50, true); g.setGradientFill (gl); g.fillEllipse (-8, -8, 116, 116);
    }
    if (face == Neon)
    {   // the rain round him
        g.setColour (rimB.withAlpha (0.5f));
        for (int i = 0; i < 6; ++i) { const float ph = std::fmod (tf * (0.9f + 0.13f * (float) i) + seeded (i * 3 + 1), 1.0f), x = (i % 2 ? 84.0f : 2.0f) + seeded (i * 3 + 2) * 14.0f, y = 4.0f + ph * 84.0f; g.drawLine (x, y, x - 2.2f, y + 7.0f, 0.9f); }
    }
    g.addTransform (juce::AffineTransform::translation (0, bob));
    // the silhouette: dome head, two stubby arms, the wavy hem
    juce::Path body; body.startNewSubPath (17, 50); body.cubicTo (17, 8, 83, 8, 83, 50);
    const float wig = std::sin (tf * 2.6f) * 1.2f;
    body.lineTo (83, 57); body.quadraticTo (91.5f, 55.5f + wig, 91.5f, 62 + wig); body.quadraticTo (91, 68.5f + wig, 83, 67.5f); body.lineTo (83, 80);
    for (int i = 1; i <= 6; ++i) { const float x = 83 - (float) i * 11, y = 83 + (i % 2 ? -5.0f : 2.0f) + std::sin (tf * 4 + (float) i) * 1.6f; body.quadraticTo (x + 5.5f, y + 4, x, y); }
    body.lineTo (17, 67.5f); body.quadraticTo (9, 68.5f - wig, 8.5f, 62 - wig); body.quadraticTo (8.5f, 55.5f - wig, 17, 57); body.closeSubPath();
    juce::ColourGradient rimGrad = face == Holo ? foil ({ 8, 8, 84, 84 }) : juce::ColourGradient (rimA, 12, 20, rimB, 88, 80, false);
    auto rimStroke = [&] (float w, float a) { auto gr = rimGrad; gr.multiplyOpacity (a); g.setGradientFill (gr); g.strokePath (body, stroke (w)); };
    if (face == Holo)
    {   // the clear raincoat hood, behind his head: a foil film round his head
        juce::Path hood; hood.startNewSubPath (11.5f, 56); hood.cubicTo (5, 0, 95, 0, 88.5f, 56); hood.quadraticTo (86, 60, 83, 57); hood.cubicTo (86, 10, 14, 10, 17, 57); hood.quadraticTo (14, 60, 11.5f, 56); hood.closeSubPath();
        g.setGradientFill (foil ({ 6, 2, 88, 58 }, 0.38f)); g.fillPath (hood);
        for (auto [w, a] : { std::pair<float, float> (5.0f, 0.12f), std::pair<float, float> (2.8f, 0.25f) }) { g.setColour (Colour (0xffb59bff).withAlpha (a)); g.strokePath (hood, stroke (w)); }
        g.setGradientFill (foil ({ 6, 2, 88, 58 })); g.strokePath (hood, stroke (1.3f));
    }
    if (print)
    {   // the print: a soft red glow, then the red and teal plates off register under the cream
        for (auto [w, a] : { std::pair<float, float> (13.0f, 0.08f), std::pair<float, float> (8.0f, 0.16f) }) { g.setColour (rimA.withAlpha (a)); g.strokePath (body, stroke (w)); }
        g.setColour (Colour (0xffd23c3c)); g.fillPath (body, juce::AffineTransform::translation (3.0f, 2.2f));
        g.setColour (Colour (0xff49b6c9)); g.fillPath (body, juce::AffineTransform::translation (-2.4f, -1.6f));
        g.setColour (bodyHi); g.fillPath (body);
        {
            juce::Graphics::ScopedSaveState cs (g); g.reduceClipRegion (body);
            g.setColour (shade); g.fillPath (body); g.setColour (bodyHi); g.fillPath (body, juce::AffineTransform::translation (-7.0f, -4.0f));
            g.setColour (Colour (0xfff2c14e));   // Ben-Day dots up the lit side
            for (int iy = 0; iy < 9; ++iy) for (int ix = 0; ix < 5; ++ix) { const float x = 21.0f + (float) ix * 4.6f + (iy % 2 ? 2.3f : 0.0f), y = 14.0f + (float) iy * 4.6f, rr = 1.5f - (float) ix * 0.28f - (float) iy * 0.1f; if (rr > 0.3f) g.fillEllipse (x - rr, y - rr, 2 * rr, 2 * rr); }
        }
        g.setColour (ink); g.strokePath (body, stroke (2.6f));
    }
    else if (light)
    {   // paper: a wash halo, the body, a blush wash pooled at the hem, a teal shadow, ink lines that do not quite agree
        g.setColour (rimA.withAlpha (0.22f)); g.strokePath (body, stroke (9.0f)); g.setColour (rimB.withAlpha (0.16f)); g.strokePath (body, stroke (5.0f), juce::AffineTransform::translation (1.5f, 1.5f));
        g.setColour (bodyHi); g.fillPath (body);
        {
            juce::Graphics::ScopedSaveState cs (g); g.reduceClipRegion (body);
            g.setColour (rimB.withAlpha (0.28f)); g.fillPath (body); g.setColour (bodyHi.withAlpha (0.93f)); g.fillPath (body, juce::AffineTransform::translation (-6.0f, -3.5f));
            g.setGradientFill (juce::ColourGradient (shade.withAlpha (0.0f), 0, 58, shade.withAlpha (0.8f), 0, 92, false)); g.fillRect (0, 0, 100, 100);
        }
        g.setColour (ink.withAlpha (0.3f)); g.strokePath (body, stroke (0.9f), juce::AffineTransform::translation (1.1f, -0.8f));
        g.setColour (ink); g.strokePath (body, stroke (1.9f));
    }
    else
    {   // neon: the tube's glow, the bright body, a cel shadow, rim lights inside the edge, a gloss on the head, then the tube itself
        rimStroke (14.0f, 0.08f); rimStroke (9.0f, 0.15f); rimStroke (5.5f, 0.3f);
        g.setGradientFill (juce::ColourGradient (bodyHi, 30, 16, bodyLo, 70, 90, false)); g.fillPath (body);
        {
            juce::Graphics::ScopedSaveState cs (g); g.reduceClipRegion (body);
            g.setColour (shade); g.fillPath (body);
            g.setGradientFill (juce::ColourGradient (bodyHi, 30, 16, bodyLo, 70, 90, false)); g.fillPath (body, juce::AffineTransform::translation (-6.0f, -3.5f));
            if (face == Holo) { g.setGradientFill (foil ({ 10, 10, 80, 80 }, 0.3f)); g.fillPath (body);
                                juce::Path sheen; sheen.startNewSubPath (10, 74); sheen.lineTo (58, 10); sheen.lineTo (66, 10); sheen.lineTo (18, 74); sheen.closeSubPath(); g.setColour (white.withAlpha (0.5f)); g.fillPath (sheen); }
            rimStroke (7.0f, 0.5f);
            g.setColour (white.withAlpha (0.95f)); g.fillPath (ell (32, 21, 8.5f, 3.6f, -0.62f)); g.fillEllipse (22.2f, 30.4f, 3.2f, 3.2f);
        }
        { auto gr = rimGrad; g.setGradientFill (gr); g.strokePath (body, stroke (2.8f)); }
        g.setColour (white.withAlpha (0.85f)); g.strokePath (body, stroke (0.9f));
    }
    // blush: a soft spot + anime hatching
    if (F.blush > 0.02f)
        for (float bx : { 23.5f, 76.5f })
        {
            g.setColour (Colour (0xffff7fb4).withAlpha ((print ? 0.9f : 0.5f) * F.blush)); g.fillPath (ell (bx, 58.5f, 6.4f, 3.3f));
            g.setColour ((print ? ink : Colour (0xffe8457f)).withAlpha (0.9f * F.blush));
            for (int k = 0; k < 3; ++k) g.drawLine (bx - 3.9f + (float) k * 3.0f, 60.6f, bx - 2.1f + (float) k * 3.0f, 56.4f, 1.0f);
        }
    // eyes
    const float eyeOpen = juce::jlimit (0.12f, 1.25f, F.eye), hh = 10.5f * eyeOpen, ww = 8.0f, ey = 46.0f, isc = F.eye > 1.05f ? 0.66f : 1.0f;
    for (float ex : { 35.5f, 64.5f })
    {
        const float outer = ex < 50 ? -1.0f : 1.0f, al = 1 - F.smileEye;
        if (al > 0.02f)
        {
            const auto eye = ell (ex, ey, ww, hh);
            g.setColour (mood.withAlpha ((light ? 0.2f : 0.34f) * al * (1.0f - 0.8f * F.dream))); g.strokePath (eye, stroke (3.8f));   // the eye's own glow
            {
                juce::Graphics::ScopedSaveState es (g);
                g.setColour (white.withAlpha (al)); g.fillPath (eye);
                g.reduceClipRegion (eye);
                juce::ColourGradient ir (mood.darker (1.3f).withAlpha (al), ex, ey - hh, mood.brighter (0.7f).withAlpha (al), ex, ey + hh, false); ir.addColour (0.5, mood.darker (0.15f).withAlpha (al));
                g.setGradientFill (ir); g.fillPath (ell (ex, ey + hh * 0.1f, ww * 0.9f * isc, hh * 0.95f * isc));
                g.setColour (ink.withAlpha (al)); g.fillPath (ell (ex, ey + hh * 0.1f, ww * 0.42f * isc, hh * 0.5f * isc));
                g.setColour (mood.interpolatedWith (white, 0.7f).withAlpha (0.85f * al)); g.fillPath (ell (ex, ey + hh * (0.1f + 0.62f * isc), ww * 0.5f * isc, hh * 0.17f * isc));
                g.setColour (ink.withAlpha (0.32f * al)); g.fillRect (ex - ww, ey - hh, 2 * ww, hh * 0.5f);   // the lid's shadow
                g.setColour (white.withAlpha (al)); g.fillPath (ell (ex - 2.8f, ey - hh * 0.4f, 2.9f, juce::jmin (2.9f, hh * 0.3f))); g.fillPath (ell (ex + 3.3f, ey + hh * 0.42f, 1.5f, juce::jmin (1.5f, hh * 0.17f)));
                if (F.spark > 0.3f) { g.setColour (white.withAlpha (F.spark * al)); g.fillPath (sign::star4 (ex + 2.6f, ey - hh * 0.05f, 2.8f * F.spark)); }
                if (F.tear > 0.05f) { g.setColour (Colour (0xffbfeaff).withAlpha (0.85f * F.tear * al)); g.fillPath (ell (ex, ey + hh * 0.86f, ww * 0.8f, hh * 0.2f)); }   // welling up
                if (F.dream > 0.3f) { g.setColour ((print || light ? bodyHi : bodyLo).withAlpha (F.dream * al)); g.fillRect (ex - ww - 1, ey - hh - 1, 2 * ww + 2, hh * 0.98f); }   // sleepy lids
            }
            g.setColour (ink.withAlpha (0.55f * al)); g.strokePath (eye, stroke (0.8f));
            juce::Path lash;   // the upper lid: thick, a flick at the outer corner
            if (F.dream > 0.3f)
            {
                const float ly = ey - 0.4f;
                lash.startNewSubPath (ex - (ww + 0.6f), ly - 0.6f); lash.quadraticTo (ex, ly + 2.0f, ex + (ww + 0.6f), ly - 0.6f);
                lash.startNewSubPath (ex + outer * (ww + 0.2f), ly - 0.4f); lash.lineTo (ex + outer * (ww + 3.2f), ly + 1.6f);
                lash.startNewSubPath (ex + outer * (ww - 2.6f), ly + 0.6f); lash.lineTo (ex + outer * (ww - 1.2f), ly + 3.0f);
            }
            else
            {
                lash.addCentredArc (ex, ey, ww + 0.4f, hh + 0.4f, 0, -1.3f, 1.3f, true);
                const float cx = ex + outer * (ww + 0.4f) * std::sin (1.3f), cy = ey - (hh + 0.4f) * std::cos (1.3f);
                lash.startNewSubPath (cx, cy); lash.lineTo (cx + outer * 3.4f, cy - 2.4f);
                lash.startNewSubPath (ex + outer * (ww + 0.4f) * std::sin (0.85f), ey - (hh + 0.4f) * std::cos (0.85f)); lash.lineTo (ex + outer * (ww + 0.4f) * std::sin (0.85f) + outer * 2.2f, ey - (hh + 0.4f) * std::cos (0.85f) - 2.6f);
            }
            g.setColour (ink.withAlpha (al)); g.strokePath (lash, stroke (2.3f));
        }
        if (F.smileEye > 0.02f)
        {   // happy ^ ^ eyes: a thick lit arc, a lash tick at the outer end
            juce::Path a; a.startNewSubPath (ex - 7.6f, ey + 4.0f); a.quadraticTo (ex, ey - 9.0f, ex + 7.6f, ey + 4.0f);
            a.startNewSubPath (ex + outer * 7.6f, ey + 4.0f); a.lineTo (ex + outer * 10.2f, ey + 2.6f);
            g.setColour (mood.withAlpha ((light ? 0.3f : 0.5f) * F.smileEye)); g.strokePath (a, stroke (6.0f));
            g.setColour (ink.withAlpha (F.smileEye)); g.strokePath (a, stroke (3.0f));
        }
        const float inner = ex < 50 ? 1.0f : -1.0f, dy = F.brow * 3.4f, by = 30.5f - (F.eye - 1) * 4.0f;
        juce::Path brow; brow.startNewSubPath (ex - 5.6f * inner, by - dy * 0.2f); brow.quadraticTo (ex, by + dy * 0.2f - 1.0f, ex + 5.6f * inner, by + dy);
        g.setColour (ink); g.strokePath (brow, stroke (2.0f));
    }
    const float my = print ? 65.0f : 66.0f;
    if (print)
    {   // the glossy black zip mask over the lower face; the zip is his mouth: it curves, opens and wobbles with the mood
        juce::Path mk; mk.startNewSubPath (17.6f, 60); mk.quadraticTo (50, 52.5f, 82.4f, 60); mk.lineTo (82.4f, 73.5f); mk.quadraticTo (50, 84.5f, 17.6f, 73.5f); mk.closeSubPath();
        juce::Graphics::ScopedSaveState ms (g); g.reduceClipRegion (body);
        g.setColour (Colour (0xffd23c3c)); g.fillPath (mk, juce::AffineTransform::translation (0, -1.6f));
        g.setColour (Colour (0xff0b0909)); g.fillPath (mk);
        g.setColour (white.withAlpha (0.85f)); g.fillPath (ell (31, 64, 7.5f, 1.9f, -0.22f)); g.setColour (white.withAlpha (0.5f)); g.fillPath (ell (72, 68.5f, 2.0f, 3.6f, 0.2f));
        const float cu = F.mouth * 5.0f, gap = F.open * 3.4f;
        auto zipY = [&] (int i, float side) { const float u = (float) i / 14.0f, bend = 4.0f * u * (1.0f - u), wob = F.wave > 0.5f ? std::sin ((float) i * 1.5f + tf * 6) * 1.5f * F.wave : 0.0f;
                                             return my + 2.0f - cu * 0.5f + cu * bend + wob + side * gap * bend; };
        juce::Path top, low, mo;
        for (int i = 0; i <= 14; ++i)
        {
            const float x = 32.0f + (float) i * 36.0f / 14.0f;
            if (i) { top.lineTo (x, zipY (i, -1)); low.lineTo (x, zipY (i, 1)); mo.lineTo (x, zipY (i, -1)); } else { top.startNewSubPath (x, zipY (i, -1)); low.startNewSubPath (x, zipY (i, 1)); mo.startNewSubPath (x, zipY (i, -1)); }
        }
        if (gap > 0.4f) { for (int i = 14; i >= 0; --i) mo.lineTo (32.0f + (float) i * 36.0f / 14.0f, zipY (i, 1)); mo.closeSubPath(); g.setColour (Colour (0xffd23c3c)); g.fillPath (mo); }
        g.setColour (Colour (0xfff2c14e)); g.strokePath (top, juce::PathStrokeType (1.5f)); g.strokePath (low, juce::PathStrokeType (1.5f));
        g.setColour (Colour (0xff0b0909));   // the teeth
        for (int i = 1; i < 14; ++i) { const float x = 32.0f + (float) i * 36.0f / 14.0f; g.drawLine (x, zipY (i, -1) - 1.0f, x, zipY (i, -1) + 1.0f, 0.7f); if (gap > 0.4f) g.drawLine (x, zipY (i, 1) - 1.0f, x, zipY (i, 1) + 1.0f, 0.7f); }
        g.setColour (Colour (0xfff2c14e)); g.fillRoundedRectangle (27.6f, zipY (0, 0) - 1.6f, 4.6f, 3.2f, 1.0f); g.setColour (Colour (0xff0b0909)); g.fillEllipse (28.6f, zipY (0, 0) - 0.7f, 1.4f, 1.4f);   // the pull
    }
    else if (F.wave > 0.5f)
    {
        juce::Path w; for (int i = 0; i <= 12; ++i) { const float x = 41.6f + (float) i * 1.4f, y = my + std::sin ((float) i * 1.6f + tf * 6) * 1.7f * F.wave; if (i) w.lineTo (x, y); else w.startNewSubPath (x, y); }
        g.setColour (ink); g.strokePath (w, stroke (2.0f));
    }
    else if (F.open > 0.15f)
    {   // an open anime mouth: a soft D, the tongue, a little fang when he is happy
        const float w = 5.6f + F.open * 2.6f, dir = F.mouth < 0 ? -1.0f : 1.0f, drop = 3.0f + F.open * 8.5f, top = my - 1.5f + (dir < 0 ? 3.0f : 0.0f);
        juce::Path m; m.startNewSubPath (50 - w, top); m.quadraticTo (50, top + dir * 1.2f, 50 + w, top); m.quadraticTo (50, top + dir * drop * 1.9f, 50 - w, top); m.closeSubPath();
        g.setColour (Colour (0xff4a1028)); g.fillPath (m);
        { juce::Graphics::ScopedSaveState s2 (g); g.reduceClipRegion (m); g.setColour (Colour (0xffff7aa8)); g.fillPath (ell (50, top + dir * drop * 0.9f, w * 0.62f, drop * 0.36f));
          if (F.mouth > 0.5f) { juce::Path fang; fang.startNewSubPath (50 - w * 0.62f, top - 0.5f); fang.lineTo (50 - w * 0.2f, top - 0.5f); fang.lineTo (50 - w * 0.42f, top + 3.4f); fang.closeSubPath(); g.setColour (white); g.fillPath (fang); } }
        g.setColour (ink); g.strokePath (m, stroke (1.7f));
    }
    else
    {   // a small cat mouth when smiling, a little line otherwise
        juce::Path m; const float cu = F.mouth * 3.6f;
        if (F.mouth > 0.3f) { m.startNewSubPath (43.6f, my - 1); m.quadraticTo (46.8f, my + cu, 50, my - 0.5f); m.quadraticTo (53.2f, my + cu, 56.4f, my - 1); }
        else { m.startNewSubPath (45.4f, my + 0.5f); m.quadraticTo (50, my + 0.5f + cu, 54.6f, my + 0.5f); }
        g.setColour (ink); g.strokePath (m, stroke (2.0f));
    }
    if (face == Kitsune)
    {   // a little fox mask worn on the side of his head: ice white, red ears, slit eyes, whiskers, an amber brow mark
        juce::Graphics::ScopedSaveState fs (g); g.addTransform (juce::AffineTransform::rotation (-0.44f).translated (25.0f, 16.5f));
        juce::Path fx; fx.startNewSubPath (-10.5f, -1.5f); fx.lineTo (-12.5f, -13.5f); fx.lineTo (-4.5f, -8.0f); fx.lineTo (4.5f, -8.0f); fx.lineTo (12.5f, -13.5f); fx.lineTo (10.5f, -1.5f);
        fx.quadraticTo (8.5f, 6.5f, 0, 11.0f); fx.quadraticTo (-8.5f, 6.5f, -10.5f, -1.5f); fx.closeSubPath();
        for (auto [w, a] : { std::pair<float, float> (6.0f, 0.14f), std::pair<float, float> (3.6f, 0.3f) }) { g.setColour (Colour (0xff8fd0ff).withAlpha (a)); g.strokePath (fx, stroke (w)); }
        g.setGradientFill (juce::ColourGradient (white, -6, -8, Colour (0xffd6ecfb), 8, 10, false)); g.fillPath (fx);
        juce::Path ears; ears.startNewSubPath (-11.2f, -11.4f); ears.lineTo (-6.4f, -8.0f); ears.lineTo (-10.2f, -5.6f); ears.closeSubPath(); ears.startNewSubPath (11.2f, -11.4f); ears.lineTo (6.4f, -8.0f); ears.lineTo (10.2f, -5.6f); ears.closeSubPath();
        g.setColour (Colour (0xffff3a3a)); g.fillPath (ears);
        juce::Path marks; marks.startNewSubPath (-8.2f, -1.2f); marks.quadraticTo (-5.4f, -4.4f, -2.2f, -0.6f); marks.startNewSubPath (8.2f, -1.2f); marks.quadraticTo (5.4f, -4.4f, 2.2f, -0.6f);
        marks.startNewSubPath (-7.6f, 3.4f); marks.lineTo (-3.8f, 4.4f); marks.startNewSubPath (-6.0f, 6.0f); marks.lineTo (-3.2f, 6.0f); marks.startNewSubPath (7.6f, 3.4f); marks.lineTo (3.8f, 4.4f); marks.startNewSubPath (6.0f, 6.0f); marks.lineTo (3.2f, 6.0f);
        g.setColour (Colour (0xffe01818)); g.strokePath (marks, stroke (1.3f));
        juce::Path nose; nose.startNewSubPath (-1.6f, 7.4f); nose.lineTo (1.6f, 7.4f); nose.lineTo (0, 9.6f); nose.closeSubPath(); g.fillPath (nose);
        juce::Path gem; gem.startNewSubPath (0, -6.4f); gem.lineTo (1.7f, -4.0f); gem.lineTo (0, -1.6f); gem.lineTo (-1.7f, -4.0f); gem.closeSubPath(); g.setColour (Colour (0xffffb35c)); g.fillPath (gem);
        g.setColour (Colour (0xffff2e2e)); g.strokePath (fx, stroke (1.4f)); g.setColour (Colour (0xffffd0d0).withAlpha (0.9f)); g.strokePath (fx, stroke (0.45f));
    }
    if (F.tear > 0.05f)
    {
        const float ph = std::fmod (tf * 0.6f, 1.0f), x = 30.5f, y = 59 + ph * 16; juce::Path d; d.startNewSubPath (x, y - 4.4f); d.quadraticTo (x + 3.4f, y + 1, x, y + 2.8f); d.quadraticTo (x - 3.4f, y + 1, x, y - 4.4f);
        const float a = F.tear * (1 - ph * 0.6f);
        g.setColour (Colour (0xff3fa8ff).withAlpha (0.45f * a)); g.strokePath (d, stroke (2.6f)); g.setColour (Colour (0xffc8f0ff).withAlpha (a)); g.fillPath (d);
        g.setColour (white.withAlpha (a)); g.fillEllipse (x - 1.3f, y - 0.6f, 1.3f, 1.6f);
    }
    if (F.sweat > 0.05f)
    {
        const float x = 80, y = 22 + std::sin (tf * 3) * 1.5f; juce::Path d; d.startNewSubPath (x, y - 6); d.quadraticTo (x + 4.6f, y + 1, x, y + 4); d.quadraticTo (x - 4.6f, y + 1, x, y - 6);
        g.setColour (Colour (0xff3fa8ff).withAlpha (0.4f * F.sweat)); g.strokePath (d, stroke (3.0f));
        g.setColour (Colour (0xffbfeaff).withAlpha (F.sweat)); g.fillPath (d); g.setColour (ink.withAlpha (0.85f * F.sweat)); g.strokePath (d, stroke (1.0f));
        g.setColour (white.withAlpha (F.sweat)); g.fillEllipse (x - 1.8f, y - 0.6f, 1.5f, 2.2f);
    }
    if (F.spark > 0.05f)
        for (int i = 0; i < 4; ++i)
        {   // four-point anime twinkles
            const float ph = std::fmod (tf * 0.8f + (float) i * 0.25f, 1.0f), x = i % 2 ? 91.0f : 9.0f, y = 14.0f + (float) i * 11.0f + (i > 1 ? 18.0f : 0.0f), sz = 2 + 4.5f * std::sin (ph * pi);
            const auto st = sign::star4 (x, y, sz);
            g.setColour ((i % 2 ? rimB : rimA).withAlpha ((light ? 0.9f : 0.5f) * F.spark)); g.strokePath (st, stroke (light ? 1.0f : 2.6f));
            g.setColour ((light ? Colour (0xfffffdf6) : white).withAlpha (F.spark)); g.fillPath (st);
        }
    if (F.dream > 0.05f)
        for (int i = 0; i < 3; ++i)
        {   // dream bubbles drifting up from his head
            const float ph = std::fmod (tf * 0.3f + (float) i / 3.0f, 1.0f), x = 80 + ph * 10 + std::sin (tf * 1.4f + (float) i) * 1.5f, y = 20 - ph * 18, rr = 1.6f + ph * 3.2f, a = F.dream * (1 - ph);
            if (! light) { g.setColour (rimB.withAlpha (0.35f * a)); g.drawEllipse (x - rr, y - rr, 2 * rr, 2 * rr, 2.4f); }
            g.setColour ((light ? ink : white).withAlpha (a)); g.drawEllipse (x - rr, y - rr, 2 * rr, 2 * rr, light ? 0.8f : 1.0f);
            g.setColour (white.withAlpha (0.8f * a)); g.fillEllipse (x - rr * 0.55f, y - rr * 0.55f, rr * 0.5f, rr * 0.5f);
        }
    if (ear > 0.02f)
    {   // listening: a hand cupped to his ear, sound rings coming in
        const float a = juce::jmin (1.0f, ear);
        const auto hand = ell (89.5f, 45.0f, 4.6f, 7.0f, 0.35f);
        g.setColour (bodyHi.withAlpha (a)); g.fillPath (hand);
        if (print || light) { g.setColour (ink.withAlpha (a)); g.strokePath (hand, stroke (print ? 2.2f : 1.6f)); }
        else { g.setColour (rimB.withAlpha (0.35f * a)); g.strokePath (hand, stroke (4.5f)); g.setColour (rimB.withAlpha (a)); g.strokePath (hand, stroke (2.0f)); g.setColour (white.withAlpha (0.8f * a)); g.strokePath (hand, stroke (0.7f)); }
        for (int i = 0; i < 3; ++i)
        {
            const float ph = std::fmod (tf * 0.9f + (float) i / 3, 1.0f), rr = 18 - ph * 11; juce::Path w; arcTo (w, 88, 44, rr, -0.7f, 0.7f, true);
            if (! light) { g.setColour (rimB.withAlpha (0.35f * a * ph)); g.strokePath (w, stroke (4.0f)); }
            g.setColour ((light ? rimB.darker (0.3f) : rimB.interpolatedWith (white, 0.4f)).withAlpha (0.95f * a * ph)); g.strokePath (w, stroke (1.6f));
        }
    }
}
} // namespace arpui::faces
