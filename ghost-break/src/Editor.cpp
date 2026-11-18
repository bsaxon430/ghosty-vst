// SPDX-License-Identifier: AGPL-3.0-or-later
// GHOST BREAK editor. Neon Tokyo night in the snow: frosted glass over the owner's art, glowing signs, a wet street.
// Everything here runs on the message thread: it reads the engine's atomics and edits the pattern through Processor::edit().
// Every control owns its parameter attachment (declared after the control's own state), so the two always die together.
#include "Editor.h"
#include "GhostBreakArt.h"
#include "engines/CurveEngine.h"
#include "effects/ControlUse.h"   // the DRAW engine's points (header-only): varToPoints / pointsToVar / factoryPoints / evalCurve
#include <algorithm>
#include <map>
#include <cmath>

namespace gbk
{
namespace
{
using Col = juce::Colour;
using RF = juce::Rectangle<float>;
using RI = juce::Rectangle<int>;
using Just = juce::Justification;
using Stroke = juce::PathStrokeType;

// ====================================================================================== the window's fixed grid
constexpr int kW = 1180, kH = 760;
constexpr int kM = 8, kLeftW = 150, kRightW = 360, kColGap = 6;
constexpr int kTopY = 4, kTopH = 64, kGap = 5, kBayH = 104, kLaneH = 21, kDrumPad = 4, kBottomH = 90;
constexpr float kPi = juce::MathConstants<float>::pi;

inline RI topRect() { return { kM, kTopY, kW - 2 * kM, kTopH }; }
// each bay's height: dragging the edge under a bay makes it taller and the other three shorter (they always add up to 4 x kBayH)
inline int gBayH[kBays] { kBayH, kBayH, kBayH, kBayH };
constexpr int kBayMinH = 62, kBayMaxH = 190;
inline int gBayTop = 0;   // PAGES: the tab row pushes the bays down
inline RI bayRect (int b) { int y = kTopY + kTopH + kGap + gBayTop; for (int i = 0; i < b; ++i) y += gBayH[i] + kGap; return { kM, y, kW - 2 * kM, gBayH[b] }; }
inline RI drumRect() { return { kM, bayRect (kBays - 1).getBottom() + kGap, kW - 2 * kM, kDrums * kLaneH + 2 * kDrumPad }; }
inline int bottomY() { return drumRect().getBottom() + kGap; }
inline int gridX() { return kM + kLeftW + kColGap; }                                 // the 32 steps: the same columns in every bay and in the drums
inline int gridW() { return kW - 2 * kM - kLeftW - kRightW - 2 * kColGap; }
inline int rightX() { return kW - kM - kRightW; }

// ====================================================================================== colours
// two looks: NEON (the night city) and WATERCOLOR (light: Japanese watercolour, rice paper, sakura, indigo ink). The look is picked
// when the window opens (SETTINGS switches it and rebuilds the window); every part reads these colours as it paints.
inline bool gWater = false;
inline Col cInk (0xff06051a), cGlass (0xff0b0a24), cText (0xfff0f3ff), cDim (0xffa6aed8), cFaint (0xff666d9c);
inline Col cPink (0xffff4fd8), cCyan (0xff45e6ff), cViolet (0xffa06bff), cMint (0xff3cff8f), cAmber (0xffffc35a), cRed (0xffff5a6e), cIce (0xffdfe7ff), cBlue (0xff6ea0ff);
// the watercolour's brightness (SETTINGS > LOOK): 0 bright paper, 1 soft (the default), 2 dusk
inline int gPaper = 1;
inline Col cPaper (0xfffffaf2), cPaper2 (0xfffbf5ea);
inline float sceneShade() { return gWater ? (gPaper == 0 ? 0.0f : gPaper == 1 ? 0.24f : 0.42f) : 0.0f; }   // an indigo wash over the painting
inline void applyLook (bool water, int paper = 1)
{
    gWater = water; gPaper = juce::jlimit (0, 2, paper);
    if (water)
    {   // rice paper, indigo ink, sakura, wisteria, matcha, the torii's vermilion (the paper toned down for SOFT and DUSK)
        static const Col ink[] { Col (0xfff7efe2), Col (0xffd3c8b5), Col (0xffaea390) }, glass[] { Col (0xfffcf6ec), Col (0xffddd2c0), Col (0xffbab09d) },
                         p1[] { Col (0xfffffaf2), Col (0xffe0d6c4), Col (0xffbfb5a2) }, p2[] { Col (0xfffbf5ea), Col (0xffdbd0bd), Col (0xffb9ae9a) };
        cInk = ink[gPaper]; cGlass = glass[gPaper]; cPaper = p1[gPaper]; cPaper2 = p2[gPaper];
        cText = Col (0xff232c58); cDim = gPaper == 2 ? Col (0xff3c4570) : Col (0xff4f5884); cFaint = gPaper == 2 ? Col (0xff575d82) : gPaper == 1 ? Col (0xff737896) : Col (0xff8a8fae);
        cPink = Col (0xffd94f7c); cCyan = Col (0xff3d74bd); cViolet = Col (0xff8559b0); cMint = Col (0xff4a9466); cAmber = Col (0xffd18a22); cRed = Col (0xffd33f2c); cIce = Col (0xff5d6c9f); cBlue = Col (0xff466cb2);
    }
    else
    {
        cInk = Col (0xff06051a); cGlass = Col (0xff0b0a24); cText = Col (0xfff0f3ff); cDim = Col (0xffa6aed8); cFaint = Col (0xff666d9c);
        cPink = Col (0xffff4fd8); cCyan = Col (0xff45e6ff); cViolet = Col (0xffa06bff); cMint = Col (0xff3cff8f); cAmber = Col (0xffffc35a); cRed = Col (0xffff5a6e); cIce = Col (0xffdfe7ff); cBlue = Col (0xff6ea0ff);
    }
}
// "white" and "black" are the light and the dark of the look: on paper the light lines are ink and the dark wells are paper
inline Col white (float a) { return gWater ? Col (0xff232c58).withAlpha (juce::jlimit (0.0f, 1.0f, a * 1.1f)) : juce::Colours::white.withAlpha (a); }
inline Col hi() { return gWater ? Col (0xff232c58) : juce::Colours::white; }   // the brightest thing of the look (on paper: the darkest ink)
inline Col black (float a) { return gWater ? cPaper.withAlpha (juce::jlimit (0.0f, 1.0f, a * 1.6f)) : juce::Colours::black.withAlpha (a); }
inline Col bayCol (int b) { const Col c[] { cPink, cCyan, cViolet, cMint }; return c[juce::jlimit (0, kBays - 1, b)]; }
// a slice's colour: each bay starts on its own colour and sweeps through the neon band, so a cell and its piece of the waveform match
inline Col sliceCol (int bay, int slice, int n)
{
    static const float from[] { 0.87f, 0.52f, 0.72f, 0.40f }, span[] { -0.46f, 0.46f, 0.30f, 0.46f };
    const int b = juce::jlimit (0, kBays - 1, bay); n = juce::jmax (1, n); const int s = ((slice % n) + n) % n;
    const float h = from[b] + span[b] * (float) s / (float) n; const bool beat = s % 4 == 0;
    return Col::fromHSV (h - std::floor (h), beat ? 0.60f : 0.74f, beat ? 1.0f : 0.90f, 1.0f);
}
inline float scaleOf (juce::Graphics& g) { return juce::jlimit (1.0f, 4.0f, g.getInternalContext().getPhysicalPixelScaleFactor()); }

// ====================================================================================== type
// One condensed family for the whole window (the Mac's Avenir Next Condensed; the nearest thing elsewhere). Sizes are given as the
// height of a capital letter, and text is centred on that height, so labels sit the same whatever font the machine has.
struct Type
{
    enum Weight { medium, bold, heavy, italic };
    juce::String family, styles[4]; float cap = 0.72f;

    Type()
    {
       #if JUCE_LINUX
        { const juce::File extra ("/usr/share/texmf/fonts/opentype/public/tex-gyre"); if (extra.isDirectory()) juce::Typeface::scanFolderForFonts (extra); }
       #endif
        const auto all = juce::Font::findAllTypefaceNames();
        for (auto* n : { "Avenir Next Condensed", "TeX Gyre Heros Cn", "Roboto Condensed", "Bahnschrift", "Helvetica Neue" })
            if (all.contains (n)) { family = n; break; }
        if (family.isNotEmpty())
        {
            const auto st = juce::Font::findAllTypefaceStyles (family);
            auto pick = [&st] (std::initializer_list<const char*> names) { for (auto* n : names) if (st.contains (n)) return juce::String (n); return st.isEmpty() ? juce::String ("Regular") : st[0]; };
            styles[medium] = pick ({ "Medium", "Regular" }); styles[bold] = pick ({ "Demi Bold", "DemiBold", "SemiBold", "Semibold", "Bold" });
            styles[heavy] = pick ({ "Heavy", "Black", "ExtraBold", "Bold" }); styles[italic] = pick ({ "Heavy Italic", "Bold Italic", "Demi Bold Italic", "Italic", "Bold" });
        }
        juce::GlyphArrangement ga; ga.addLineOfText (make (100.0f, bold), "H", 0.0f, 0.0f); juce::Path p; ga.createPath (p);
        const float h = p.getBounds().getHeight() / 100.0f; if (h > 0.4f && h < 0.95f) cap = h;
    }
    juce::Typeface::Ptr face;   // the WATERCOLOR look's brush letters
    explicit Type (juce::Typeface::Ptr brush) : face (brush)
    {
        juce::GlyphArrangement ga; ga.addLineOfText (make (100.0f, bold), "H", 0.0f, 0.0f); juce::Path p; ga.createPath (p);
        const float h = p.getBounds().getHeight() / 100.0f; if (h > 0.4f && h < 0.95f) cap = h;
    }
    juce::Font make (float height, Weight w) const
    {
        if (face != nullptr) return juce::Font (juce::FontOptions (face).withHeight (height));
        if (family.isEmpty()) return juce::Font (juce::FontOptions (height, w == medium ? juce::Font::plain : w == italic ? (juce::Font::bold | juce::Font::italic) : juce::Font::bold));
        return juce::Font (juce::FontOptions (family, styles[w], height));
    }
    static const Type& get()
    {
        static const Type t;
        if (! gWater) return t;
        static const Type w ([] { int n = 0; const char* d = GhostBreakArt::getNamedResource ("wc_font_ttf", n); return d != nullptr ? juce::Typeface::createSystemTypefaceFor (d, (size_t) n) : juce::Typeface::Ptr(); }());
        return w.face != nullptr ? w : t;
    }
};
inline juce::Font font (float capPx, Type::Weight w = Type::bold, float track = 0.05f)
{
    const auto& t = Type::get(); if (gWater) track *= 0.4f; auto f = t.make (capPx / t.cap, w);
    if (gWater) f = f.withHorizontalScale (0.84f);   // the brush letters run wide: a little narrower, so every word fits
    return track > 0.0f ? f.withExtraKerningFactor (track) : f;
}
// one line, centred on its capital height
inline void drawText (juce::Graphics& g, const juce::Font& f, const juce::String& s, RF r, Just j = Just::centred)
{
    const float dy = (f.getHeight() * Type::get().cap - f.getAscent() + f.getDescent()) * 0.5f;
    g.setFont (f); g.drawText (s, r.translated (0.0f, dy), j, false);
}
inline float textWidth (const juce::Font& f, const juce::String& s) { return juce::GlyphArrangement::getStringWidth (f, s); }
// the same, squeezed sideways when the words are wider than the space
inline void fitText (juce::Graphics& g, juce::Font f, const juce::String& s, RF r, Just j = Just::centred, float minScale = 0.68f)
{
    const float w = textWidth (f, s);
    if (w > r.getWidth() && w > 1.0f) f = f.withHorizontalScale (juce::jmax (minScale, r.getWidth() / w));
    drawText (g, f, s, r, j);
}
// words wrapped onto up to maxLines lines inside r (squeezed a little, then smaller, until all of it fits: nothing is cut off)
inline void wrapText (juce::Graphics& g, juce::Font f, const juce::String& s, RF r, int maxLines = 2, Just j = Just::centredLeft)
{
    const auto words = juce::StringArray::fromTokens (s, " ", "");
    for (int attempt = 0; attempt < 8; ++attempt, f = f.withHeight (f.getHeight() * 0.92f))
    {
        juce::StringArray lines; juce::String line;
        for (const auto& w : words)
        {
            const juce::String t = line.isEmpty() ? w : line + " " + w;
            if (line.isNotEmpty() && textWidth (f, t) > r.getWidth()) { lines.add (line); line = w; } else line = t;
        }
        if (line.isNotEmpty()) lines.add (line);
        bool fits = lines.size() <= maxLines; for (const auto& l : lines) fits = fits && textWidth (f, l) <= r.getWidth();
        if (! fits && attempt < 7) continue;
        const float lh = f.getHeight() * 1.05f, top = r.getCentreY() - lh * (float) lines.size() * 0.5f;
        for (int i = 0; i < lines.size(); ++i) fitText (g, f, lines[i], RF (r.getX(), top + lh * (float) i, r.getWidth(), lh), j);
        return;
    }
}
// "1200.00 Hz" -> "1200 Hz", "0.50 st" -> "0.5 st", "35 %" -> "35%"
inline juce::String compact (const juce::String& t)
{
    juce::String n = t.upToFirstOccurrenceOf (" ", false, false); const juce::String unit = t.fromFirstOccurrenceOf (" ", false, false).trim();
    if (n.containsChar ('.') && n.containsOnly ("-+.0123456789")) { while (n.endsWithChar ('0')) n = n.dropLastCharacters (1); if (n.endsWithChar ('.')) n = n.dropLastCharacters (1); }
    if (n == "-0") n = "0";
    return unit.isEmpty() ? n : (unit == "%" ? n + "%" : n + " " + unit);
}

// ====================================================================================== images
// box blur, three passes (close to a gaussian), any pixel format, in place
void blur (juce::Image& img, int radius, int passes = 3)
{
    if (radius < 1 || ! img.isValid()) return;
    juce::Image::BitmapData bd (img, juce::Image::BitmapData::readWrite);
    const int w = bd.width, h = bd.height, ps = bd.pixelStride, win = radius * 2 + 1;
    std::vector<int> line ((size_t) juce::jmax (w, h));
    for (int pass = 0; pass < passes; ++pass)
        for (int dir = 0; dir < 2; ++dir)
        {
            const int n = dir == 0 ? w : h, lines = dir == 0 ? h : w; const std::ptrdiff_t step = dir == 0 ? ps : bd.lineStride;
            for (int l = 0; l < lines; ++l)
            {
                juce::uint8* const base = dir == 0 ? bd.getLinePointer (l) : bd.getLinePointer (0) + l * ps;
                for (int c = 0; c < ps; ++c)
                {
                    juce::uint8* const p = base + c;
                    for (int i = 0; i < n; ++i) line[(size_t) i] = p[(std::ptrdiff_t) i * step];
                    int sum = 0; for (int i = -radius; i <= radius; ++i) sum += line[(size_t) juce::jlimit (0, n - 1, i)];
                    for (int i = 0; i < n; ++i)
                    {
                        p[(std::ptrdiff_t) i * step] = (juce::uint8) (sum / win);
                        sum += line[(size_t) juce::jmin (n - 1, i + radius + 1)] - line[(size_t) juce::jmax (0, i - radius)];
                    }
                }
            }
        }
}
// a piece of the art with its night keyed out: the neon stays, the dark goes (alpha from brightness), the edges fade
juce::Image keyed (const juce::Image& src, RI area, float lo, float hi, float feather)
{
    juce::Image out (juce::Image::ARGB, area.getWidth(), area.getHeight(), true);
    { juce::Graphics g (out); g.drawImage (src, 0, 0, area.getWidth(), area.getHeight(), area.getX(), area.getY(), area.getWidth(), area.getHeight()); }
    juce::Image::BitmapData bd (out, juce::Image::BitmapData::readWrite);
    for (int y = 0; y < bd.height; ++y)
    {
        auto* px = (juce::PixelARGB*) (void*) bd.getLinePointer (y);
        for (int x = 0; x < bd.width; ++x)
        {
            const float r = (float) px[x].getRed() / 255.0f, gr = (float) px[x].getGreen() / 255.0f, b = (float) px[x].getBlue() / 255.0f;
            float a = juce::jlimit (0.0f, 1.0f, (juce::jmax (r, gr, b) - lo) / (hi - lo)); a = a * a * (3.0f - 2.0f * a);
            const float edge = (float) juce::jmin (juce::jmin (x, bd.width - 1 - x), juce::jmin (y, bd.height - 1 - y)) / juce::jmax (1.0f, feather);
            a *= juce::jlimit (0.0f, 1.0f, edge);
            px[x].setARGB ((juce::uint8) (a * 255.0f), (juce::uint8) (r * a * 255.0f), (juce::uint8) (gr * a * 255.0f), (juce::uint8) (b * a * 255.0f));
        }
    }
    return out;
}

// the owner's art: the alley as the window's backdrop, a frosted copy for the glass, the lettering and the ghost cut out of it
struct Art
{
    juce::Image full, frost, logo, ghost;
    static constexpr float kSceneTop = 640.0f;   // the row of the art at the window's top edge: the wet street then runs along the bottom

    Art()
    {
        if (gWater)
        {   // Brandon's watercolour: Fuji over the lake in sakura, the brush lettering lifted out as the logo
            auto load = [] (const char* id) { int n = 0; const char* d = GhostBreakArt::getNamedResource (id, n); return d != nullptr ? juce::ImageCache::getFromMemory (d, n) : juce::Image(); };
            full = load ("wc_scene_jpg"); logo = load ("wc_logo_png");
            if (full.isValid()) { frost = juce::Image (juce::Image::RGB, kW / 2, kH / 2, false); { juce::Graphics g (frost); g.addTransform (juce::AffineTransform::scale (0.5f)); drawScene (g); } blur (frost, 5); }
            return;
        }
        full = juce::ImageCache::getFromMemory (GhostBreakArt::art_jpg, GhostBreakArt::art_jpgSize);
        if (! full.isValid()) return;
        frost = juce::Image (juce::Image::RGB, kW / 2, kH / 2, false);
        { juce::Graphics g (frost); g.addTransform (juce::AffineTransform::scale (0.5f)); drawScene (g); }
        blur (frost, 7);
        const float k = (float) full.getWidth() / 1206.0f;   // the crops were picked on the 1206 x 1512 original
        auto at = [k] (int x, int y, int w, int h) { return RI (juce::roundToInt ((float) x * k), juce::roundToInt ((float) y * k), juce::roundToInt ((float) w * k), juce::roundToInt ((float) h * k)); };
        logo = keyed (full, at (186, 366, 838, 366).getIntersection (full.getBounds()), 0.16f, 0.52f, 14.0f * k);
        ghost = keyed (full, at (398, 692, 396, 336).getIntersection (full.getBounds()), 0.10f, 0.42f, 26.0f * k);
    }
    void drawScene (juce::Graphics& g) const
    {
        g.fillAll (cInk);
        if (! full.isValid()) return;
        if (gWater) { g.setImageResamplingQuality (juce::Graphics::highResamplingQuality); g.drawImage (full, RF (0.0f, 0.0f, (float) kW, (float) kH), juce::RectanglePlacement::fillDestination);
                      if (sceneShade() > 0.0f) { g.setColour (Col (0xff2b2c4e).withAlpha (sceneShade())); g.fillAll(); } return; }
        const float s = (float) kW / (float) full.getWidth();
        g.drawImageTransformed (full, juce::AffineTransform::scale (s).translated (0.0f, -kSceneTop * s * (float) full.getWidth() / 1206.0f));
    }
};


// ====================================================================================== the Phantom FX neon pieces
// Phantom FX's hand-painted NEON look (its dials, its glass buttons, its drop-down plate, its umbrella ghost), copied into
// ghost-break/assets as neon_*.png. Each Ghost Break colour picks the nearest of the look's own colours, so a pink bay gets the
// pink dial and the pink lit button.
struct Neon
{
    struct Swatch { const char* name; Col c; };
    static const std::vector<Swatch>& swatches()
    {
        static const std::vector<Swatch> s { { "pink", Col (0xffff5290) }, { "magenta", Col (0xffff33b4) }, { "ice", Col (0xff1fe9ff) }, { "violet", Col (0xffa35cff) },
                                             { "lime", Col (0xff8dff1f) }, { "green", Col (0xff3ee084) }, { "gold", Col (0xffffc93c) }, { "orange", Col (0xffffb129) },
                                             { "red", Col (0xffff2944) } };
        return s;
    }
    // the look's colour nearest to c (by hue; pale colours go to "neutral" when allowed)
    static const Swatch& nearest (Col c, bool allowNeutral, std::initializer_list<const char*> only = {})
    {
        static const Swatch neutral { "neutral", Col (0xffcfd6ff) };
        if (allowNeutral && c.getSaturation() < 0.3f) return neutral;
        const Swatch* best = &swatches().front(); float bd = 9.0f;
        for (auto& w : swatches())
        {
            if (only.size() > 0 && std::find_if (only.begin(), only.end(), [&] (const char* n) { return juce::String (n) == w.name; }) == only.end()) continue;
            float d = std::abs (w.c.getHue() - c.getHue()); d = juce::jmin (d, 1.0f - d); d += 0.15f * std::abs (w.c.getSaturation() - c.getSaturation());
            if (d < bd) { bd = d; best = &w; }
        }
        return *best;
    }
    static juce::Image load (const juce::String& name)
    {
        int size = 0; const juce::String id = name.replaceCharacter ('.', '_').replaceCharacter ('-', '_');
        if (const char* data = GhostBreakArt::getNamedResource (id.toRawUTF8(), size)) return juce::ImageCache::getFromMemory (data, size);
        return {};
    }
    static const juce::Image& img (const juce::String& name)
    {
        static std::map<juce::String, juce::Image> cache;
        auto it = cache.find (name); if (it == cache.end()) it = cache.emplace (name, load (name)).first;
        return it->second;
    }
    // a picture drawn at r, sharp at the screen's scale (the scaled copy is kept)
    static void draw (juce::Graphics& g, const juce::String& name, RF r, float alpha = 1.0f)
    {
        const auto& src = img (name); if (! src.isValid() || r.isEmpty()) return;
        const float sc = scaleOf (g); const int pw = juce::jmax (1, juce::roundToInt (r.getWidth() * sc)), ph = juce::jmax (1, juce::roundToInt (r.getHeight() * sc));
        static std::map<juce::String, juce::Image> sized; if (sized.size() > 600) sized.clear();
        const juce::String key = name + "@" + juce::String (pw) + "x" + juce::String (ph);
        auto it = sized.find (key);
        if (it == sized.end())
        {
            juce::Image out (juce::Image::ARGB, pw, ph, true); juce::Graphics sg (out);
            sg.setImageResamplingQuality (juce::Graphics::highResamplingQuality); sg.drawImage (src, RF (0.0f, 0.0f, (float) pw, (float) ph));
            it = sized.emplace (key, out).first;
        }
        g.setOpacity (alpha); g.drawImage (it->second, r); g.setOpacity (1.0f);
    }
    // a 9-slice picture (corners kept, edges and middle stretched). Margins are in the picture's 1x points; small parts shrink them.
    static void nine (juce::Graphics& g, const juce::String& name, RF r, float l, float t, float rr, float b, float alpha = 1.0f)
    {
        if (gWater)
        {   // paper with an ink edge; a drop-down gets a brushed arrow at its right
            const float rad = juce::jmin (6.0f, r.getHeight() * 0.3f); const bool hov = name.contains ("hover");
            g.setColour (cPaper.withAlpha ((hov ? 0.95f : 0.82f) * alpha)); g.fillRoundedRectangle (r.reduced (0.5f), rad);
            g.setColour (Col (0xff232c58).withAlpha ((hov ? 0.65f : 0.4f) * alpha)); g.drawRoundedRectangle (r.reduced (0.8f), rad, 1.0f);
            if (name.contains ("menu"))
            {
                const float aw = juce::jmin (r.getWidth() * 0.3f, rr * juce::jmin (1.0f, r.getHeight() / 30.0f)), ax = r.getRight() - aw * 0.5f, ay = r.getCentreY();
                juce::Path p; p.startNewSubPath (ax - 4.0f, ay - 2.0f); p.lineTo (ax, ay + 2.5f); p.lineTo (ax + 4.0f, ay - 2.0f);
                g.setColour (Col (0xffd94f7c).withAlpha (alpha)); g.strokePath (p, Stroke (1.8f, Stroke::curved, Stroke::rounded));
            }
            return;
        }
        const auto& src = img (name); if (! src.isValid() || r.isEmpty()) return;
        const float px = 2.0f, k = juce::jmin (1.0f, r.getHeight() / ((float) src.getHeight() / px), r.getWidth() / (l + rr + 2.0f));   // pictures are @2x
        const float L = l * k, T = t * k, R = rr * k, B = b * k;
        const float sx[] { 0.0f, l * px, (float) src.getWidth() - rr * px, (float) src.getWidth() }, sy[] { 0.0f, t * px, (float) src.getHeight() - b * px, (float) src.getHeight() };
        const float dx[] { r.getX(), r.getX() + L, r.getRight() - R, r.getRight() }, dy[] { r.getY(), r.getY() + T, r.getBottom() - B, r.getBottom() };
        g.setOpacity (alpha); g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j)
        {
            const RF d (dx[i], dy[j], dx[i + 1] - dx[i], dy[j + 1] - dy[j]); if (d.getWidth() <= 0.01f || d.getHeight() <= 0.01f) continue;
            const juce::Rectangle<int> sr (juce::roundToInt (sx[i]), juce::roundToInt (sy[j]), juce::jmax (1, juce::roundToInt (sx[i + 1] - sx[i])), juce::jmax (1, juce::roundToInt (sy[j + 1] - sy[j])));
            g.drawImage (src.getClippedImage (sr), d, juce::RectanglePlacement::stretchToFit);
        }
        g.setOpacity (1.0f);
    }
    // a Phantom FX dial: its soft light, its painted body, the value light and the pointer (the NEON look's own measures)
    static void dial (juce::Graphics& g, RF cell, Col accent, float a0, float a1, float from, float at, float t, bool hot, float alpha, bool pointer = true)
    {
        if (gWater) { waterDial (g, cell, accent, a0, a1, from, at, t, hot, alpha, pointer); return; }
        const auto& sw = nearest (accent, true); const Col ac = sw.c;
        const float k = cell.getWidth() / 124.0f, px = cell.getX() + 62.0f * k, py = cell.getY() + 61.0f * k;
        if (alpha > 0.5f && juce::String (sw.name) != "neutral") { const float gd = 150.0f * k; draw (g, juce::String ("neon_glow_") + nearest (accent, false, { "pink", "magenta", "ice", "violet", "lime", "orange", "red" }).name + ".png", { px - gd * 0.5f, py - gd * 0.5f, gd, gd }, (0.25f + 0.45f * t) * alpha); }
        draw (g, juce::String ("neon_knob_") + sw.name + ".png", cell, alpha < 0.9f ? 0.55f : 1.0f);
        const float ar = 45.9f * k, aw = juce::jmax (1.4f, 4.6f * k);
        if (std::abs (at - from) > 0.01f)
        {
            juce::Path val; val.addCentredArc (px, py, ar, ar, 0.0f, juce::jmin (from, at), juce::jmax (from, at), true);
            const Col c = hot ? ac.brighter (0.12f) : ac;
            g.setColour (c.withAlpha (alpha)); g.strokePath (val, Stroke (aw, Stroke::curved, Stroke::rounded));
            g.setColour (c.interpolatedWith (hi(), 0.6f).withAlpha (0.9f * alpha)); g.strokePath (val, Stroke (juce::jmax (0.7f, 2.1f * k), Stroke::curved, Stroke::rounded));
            const float ed = juce::jmax (0.8f, 2.8f * k); g.setColour (white (0.95f * alpha)); g.fillEllipse (px + std::sin (at) * ar - ed, py - std::cos (at) * ar - ed, ed * 2.0f, ed * 2.0f);
        }
        if (pointer)
        {
            const float pr = 35.0f * k, dx = std::sin (at), dy = -std::cos (at);
            auto seg = [&] (float r0, float r1, float wdt, Col c) { g.setColour (c); g.drawLine (juce::Line<float> (px + dx * pr * r0, py + dy * pr * r0, px + dx * pr * r1, py + dy * pr * r1), wdt);
                                                                    g.fillEllipse (px + dx * pr * r0 - wdt * 0.5f, py + dy * pr * r0 - wdt * 0.5f, wdt, wdt); g.fillEllipse (px + dx * pr * r1 - wdt * 0.5f, py + dy * pr * r1 - wdt * 0.5f, wdt, wdt); };
            seg (0.26f, 0.86f, juce::jmax (2.0f, 0.2f * pr), Col (0xff0b0a1c).withAlpha (alpha));
            seg (0.31f, 0.82f, juce::jmax (1.1f, 0.1f * pr), ac.withAlpha (alpha));
            seg (0.31f, 0.82f, juce::jmax (0.6f, 0.046f * pr), Col::fromHSL (ac.getHue(), ac.getSaturation(), 0.91f, 1.0f).withAlpha (alpha));
        }
        if (hot) draw (g, "neon_knob_hover.png", cell, 0.8f);
    }
    // ---- the WATERCOLOR look's own pieces: washes of colour on rice paper, indigo ink lines
    static void wash (juce::Graphics& g, RF r, float rad, Col c, float alpha)
    {   // a watercolour wash: darker at its edge (the pigment pools there), lighter inside
        g.setColour (c.withAlpha (0.55f * alpha)); g.fillRoundedRectangle (r, rad);
        g.setGradientFill (juce::ColourGradient (cPaper.withAlpha (0.45f * alpha), r.getCentreX(), r.getCentreY() - r.getHeight() * 0.2f, cPaper.withAlpha (0.0f), r.getX(), r.getBottom(), true));
        g.fillRoundedRectangle (r, rad);
        g.setColour (c.darker (0.25f).withAlpha (0.65f * alpha)); g.drawRoundedRectangle (r.reduced (0.6f), rad, 1.1f);
    }
    static void waterDial (juce::Graphics& g, RF cell, Col accent, float, float, float from, float at, float t, bool hot, float alpha, bool pointer)
    {
        const float k = cell.getWidth() / 124.0f, px = cell.getCentreX(), py = cell.getCentreY(), ar = 45.9f * k, br = 38.0f * k;
        // the soft round wash, a darker ring of the same pigment, then the ink rim
        g.setGradientFill (juce::ColourGradient (accent.withAlpha ((0.16f + 0.22f * t) * alpha), px - br * 0.3f, py - br * 0.3f, accent.withAlpha (0.42f * alpha), px + br, py + br, true));
        g.fillEllipse (px - br, py - br, br * 2.0f, br * 2.0f);
        g.setColour (cPaper.withAlpha (0.55f * alpha)); g.fillEllipse (px - br * 0.62f, py - br * 0.7f, br * 1.1f, br * 0.9f);
        g.setColour (Col (0xff232c58).withAlpha ((hot ? 0.85f : 0.6f) * alpha)); g.drawEllipse (px - br, py - br, br * 2.0f, br * 2.0f, juce::jmax (0.9f, 2.0f * k));
        juce::Path track; track.addCentredArc (px, py, ar, ar, 0.0f, kPi * 1.25f, kPi * 2.75f, true);
        g.setColour (accent.withAlpha (0.16f * alpha)); g.strokePath (track, Stroke (juce::jmax (1.6f, 5.5f * k), Stroke::curved, Stroke::rounded));
        if (std::abs (at - from) > 0.01f)
        {   // the value: a brush stroke of the colour
            juce::Path val; val.addCentredArc (px, py, ar, ar, 0.0f, juce::jmin (from, at), juce::jmax (from, at), true);
            g.setColour (accent.withAlpha (0.92f * alpha)); g.strokePath (val, Stroke (juce::jmax (1.8f, 6.0f * k), Stroke::curved, Stroke::rounded));
            g.setColour (accent.darker (0.35f).withAlpha (0.5f * alpha)); g.strokePath (val, Stroke (juce::jmax (0.6f, 1.6f * k), Stroke::curved, Stroke::rounded));
        }
        if (pointer)
        {
            const float dx = std::sin (at), dy = -std::cos (at); g.setColour (Col (0xff232c58).withAlpha (alpha));
            g.drawLine (juce::Line<float> (px + dx * br * 0.25f, py + dy * br * 0.25f, px + dx * br * 0.9f, py + dy * br * 0.9f), juce::jmax (1.4f, 4.2f * k));
        }
    }
    // a glass button: dark when off, lit in the nearest colour when on
    static void button (juce::Graphics& g, RF r, Col accent, bool on, bool over, bool down, float alpha)
    {
        if (gWater)
        {
            const float rad = juce::jmin (6.0f, r.getHeight() * 0.35f);
            if (on) wash (g, r.reduced (0.5f), rad, accent, alpha);
            else
            {
                g.setColour (cPaper.withAlpha ((down ? 0.95f : over ? 0.9f : 0.78f) * alpha)); g.fillRoundedRectangle (r.reduced (0.5f), rad);
                g.setColour (Col (0xff232c58).withAlpha ((over ? 0.6f : 0.35f) * alpha)); g.drawRoundedRectangle (r.reduced (0.8f), rad, 1.0f);
            }
            return;
        }
        const juce::String n = on ? juce::String ("neon_btn_") + nearest (accent, false, { "pink", "magenta", "ice", "violet", "lime", "orange", "red" }).name + ".png"
                                  : down ? juce::String ("neon_btn_pressed.png") : over ? juce::String ("neon_btn_hover.png") : juce::String ("neon_btn_off.png");
        nine (g, n, r, 12.0f, 10.0f, 12.0f, 12.0f, alpha);
    }
    static Col lit (Col accent) { return nearest (accent, true).c; }   // the look's own version of a colour (for words and lights)
};

// ====================================================================================== small drawings
enum class Icon { none, lock, sliders, left, right, plus, folder, undo, refresh, cross, speaker };

void drawIcon (juce::Graphics& g, Icon ic, RF r, Col c, bool on)
{
    const float x = r.getX(), y = r.getY(), w = r.getWidth(), h = r.getHeight(), cx = r.getCentreX(), cy = r.getCentreY();
    juce::Path p; g.setColour (c);
    switch (ic)
    {
        case Icon::none: break;
        case Icon::lock:   // a padlock: shut and filled when on, open when off
        {
            const RF body (x + w * 0.14f, y + h * 0.46f, w * 0.72f, h * 0.50f);
            const float sr = w * 0.22f, top = body.getY() - h * 0.14f - (on ? 0.0f : h * 0.12f);
            p.startNewSubPath (cx - sr, body.getY() + 0.5f); p.lineTo (cx - sr, top); p.addCentredArc (cx, top, sr, sr, 0.0f, -kPi * 0.5f, kPi * 0.5f, false);
            if (on) p.lineTo (cx + sr, body.getY() + 0.5f);
            g.strokePath (p, Stroke (juce::jmax (1.3f, w * 0.13f), Stroke::curved, Stroke::rounded));
            if (on) g.fillRoundedRectangle (body, w * 0.12f); else g.drawRoundedRectangle (body.reduced (0.6f), w * 0.12f, 1.2f);
            break;
        }
        case Icon::sliders:   // three little faders: "more controls"
            for (int i = 0; i < 3; ++i)
            {
                const float ly = y + h * (0.2f + 0.3f * (float) i), kx = x + w * (i == 0 ? 0.3f : i == 1 ? 0.7f : 0.45f);
                g.fillRect (RF (x, ly - 0.6f, w, 1.2f)); g.fillEllipse (kx - 2.1f, ly - 2.1f, 4.2f, 4.2f);
            }
            break;
        case Icon::left: p.addTriangle (x + w * 0.22f, cy, x + w * 0.76f, y + h * 0.16f, x + w * 0.76f, y + h * 0.84f); g.fillPath (p); break;
        case Icon::right: p.addTriangle (x + w * 0.78f, cy, x + w * 0.24f, y + h * 0.16f, x + w * 0.24f, y + h * 0.84f); g.fillPath (p); break;
        case Icon::speaker:   // a speaker (MUTE): crossed out while on
        {
            juce::Path sp; sp.startNewSubPath (x + w * 0.12f, y + h * 0.38f); sp.lineTo (x + w * 0.32f, y + h * 0.38f); sp.lineTo (x + w * 0.56f, y + h * 0.14f);
            sp.lineTo (x + w * 0.56f, y + h * 0.86f); sp.lineTo (x + w * 0.32f, y + h * 0.62f); sp.lineTo (x + w * 0.12f, y + h * 0.62f); sp.closeSubPath(); g.fillPath (sp);
            if (on) { p.startNewSubPath (x + w * 0.68f, y + h * 0.36f); p.lineTo (x + w * 0.92f, y + h * 0.64f); p.startNewSubPath (x + w * 0.92f, y + h * 0.36f); p.lineTo (x + w * 0.68f, y + h * 0.64f); }
            else { p.addCentredArc (x + w * 0.56f, cy, w * 0.2f, h * 0.22f, 0.0f, 0.5f, 2.6f, true); p.addCentredArc (x + w * 0.56f, cy, w * 0.36f, h * 0.38f, 0.0f, 0.55f, 2.6f, true); }
            g.strokePath (p, Stroke (1.4f, Stroke::curved, Stroke::rounded)); break;
        }
        case Icon::cross:   // an X: empty it
            p.startNewSubPath (x + w * 0.2f, y + h * 0.2f); p.lineTo (x + w * 0.8f, y + h * 0.8f); p.startNewSubPath (x + w * 0.8f, y + h * 0.2f); p.lineTo (x + w * 0.2f, y + h * 0.8f);
            g.strokePath (p, Stroke (1.6f, Stroke::curved, Stroke::rounded)); break;
        case Icon::plus: g.fillRect (RF (x + w * 0.1f, cy - 0.9f, w * 0.8f, 1.8f)); g.fillRect (RF (cx - 0.9f, y + h * 0.1f, 1.8f, h * 0.8f)); break;
        case Icon::folder:
            p.startNewSubPath (x, y + h * 0.24f); p.lineTo (x + w * 0.38f, y + h * 0.24f); p.lineTo (x + w * 0.5f, y + h * 0.4f); p.lineTo (x + w, y + h * 0.4f);
            p.lineTo (x + w, y + h * 0.86f); p.lineTo (x, y + h * 0.86f); p.closeSubPath();
            g.strokePath (p, Stroke (1.2f, Stroke::curved, Stroke::rounded)); break;
        case Icon::undo:
            p.addCentredArc (cx, cy + h * 0.06f, w * 0.34f, h * 0.34f, 0.0f, -kPi * 0.62f, kPi * 0.72f, true);
            g.strokePath (p, Stroke (1.5f, Stroke::curved, Stroke::rounded));
            { juce::Path a; const float ax = cx - w * 0.34f * std::sin (kPi * 0.62f), ay = cy + h * 0.06f - h * 0.34f * std::cos (kPi * 0.62f);
              a.addTriangle (ax - 3.6f, ay - 1.2f, ax + 2.4f, ay - 3.4f, ax + 1.2f, ay + 3.0f); g.fillPath (a); }
            break;
        case Icon::refresh:
            p.addCentredArc (cx, cy, w * 0.36f, h * 0.36f, 0.0f, kPi * 0.25f, kPi * 1.85f, true);
            g.strokePath (p, Stroke (1.4f, Stroke::curved, Stroke::rounded));
            { juce::Path a; const float ax = cx + w * 0.36f * std::sin (kPi * 1.85f), ay = cy - h * 0.36f * std::cos (kPi * 1.85f);
              a.addTriangle (ax - 1.0f, ay - 3.6f, ax + 3.4f, ay + 0.6f, ax - 2.6f, ay + 1.6f); g.fillPath (a); }
            break;
    }
}
// a neon tube: a soft halo, then the bright line
inline void neonStroke (juce::Graphics& g, const juce::Path& p, Col c, float thick, float glow = 1.0f)
{
    if (gWater) { g.setColour (c.withAlpha (0.18f)); g.strokePath (p, Stroke (thick + 2.5f, Stroke::curved, Stroke::rounded)); g.setColour (c); g.strokePath (p, Stroke (thick, Stroke::curved, Stroke::rounded)); return; }
    g.setColour (c.withAlpha (0.10f * glow)); g.strokePath (p, Stroke (thick + 5.0f, Stroke::curved, Stroke::rounded));
    g.setColour (c.withAlpha (0.24f * glow)); g.strokePath (p, Stroke (thick + 2.2f, Stroke::curved, Stroke::rounded));
    g.setColour (c.interpolatedWith (hi(), 0.3f)); g.strokePath (p, Stroke (thick, Stroke::curved, Stroke::rounded));
}

// ====================================================================================== menus (inside the plugin window, in the window's look)
struct Look final : juce::LookAndFeel_V4
{
    Look()
    {
        setColour (juce::PopupMenu::backgroundColourId, gWater ? cPaper2 : Col (0xff0c0b25)); setColour (juce::PopupMenu::textColourId, cText);
        setColour (juce::PopupMenu::headerTextColourId, cPink); setColour (juce::PopupMenu::highlightedBackgroundColourId, cCyan.withAlpha (0.22f));
        setColour (juce::PopupMenu::highlightedTextColourId, hi());
        setColour (juce::TextEditor::backgroundColourId, gWater ? cPaper : Col (0xff0a0920)); setColour (juce::TextEditor::textColourId, gWater ? cText : hi());
        setColour (juce::TextEditor::highlightColourId, cCyan.withAlpha (0.35f)); setColour (juce::TextEditor::highlightedTextColourId, hi());
        setColour (juce::TextEditor::outlineColourId, cCyan.withAlpha (0.7f)); setColour (juce::TextEditor::focusedOutlineColourId, cCyan);
        setColour (juce::CaretComponent::caretColourId, cCyan);
    }
    juce::Font getPopupMenuFont() override { return font (7.2f, Type::medium, 0.02f); }
    int getPopupMenuBorderSize() override { return 3; }
    void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override
    {
        const RF r (0.0f, 0.0f, (float) w, (float) h);
        g.fillAll (gWater ? cPaper2 : Col (0xff0c0b25));
        g.setGradientFill (juce::ColourGradient (cViolet.withAlpha (0.16f), 0.0f, 0.0f, cViolet.withAlpha (0.0f), 0.0f, (float) h, false)); g.fillRect (r);
        g.setColour (cCyan.withAlpha (0.6f)); g.drawRect (r, 1.0f);
    }
    void drawPopupMenuItem (juce::Graphics& g, const RI& area, bool isSeparator, bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu,
                            const juce::String& text, const juce::String&, const juce::Drawable*, const Col*) override
    {
        const auto r = area.toFloat().reduced (2.0f, 1.0f);
        if (isSeparator) { g.setColour (white (0.14f)); g.fillRect (RF (r.getX() + 6.0f, r.getCentreY() - 0.5f, r.getWidth() - 12.0f, 1.0f)); return; }
        if (isHighlighted && isActive) { g.setColour (cCyan.withAlpha (0.2f)); g.fillRoundedRectangle (r, 3.0f); }
        const Col ink = ! isActive ? cFaint : isTicked ? cCyan : (isHighlighted ? (gWater ? cText.darker (0.3f) : hi()) : cText);
        g.setColour (ink);
        if (isTicked) g.fillEllipse (r.getX() + 6.0f, r.getCentreY() - 2.5f, 5.0f, 5.0f);
        fitText (g, getPopupMenuFont(), text, r.withTrimmedLeft (17.0f).withTrimmedRight (hasSubMenu ? 14.0f : 4.0f), Just::centredLeft);
        if (hasSubMenu) { juce::Path p; const float ax = r.getRight() - 9.0f, ay = r.getCentreY(); p.addTriangle (ax, ay - 3.5f, ax + 5.0f, ay, ax, ay + 3.5f); g.setColour (ink.withAlpha (0.8f)); g.fillPath (p); }
    }
    void drawPopupMenuSectionHeader (juce::Graphics& g, const RI& area, const juce::String& name) override
    {
        g.setColour (cPink); drawText (g, font (5.2f, Type::bold, 0.16f), name.toUpperCase(), area.toFloat().withTrimmedLeft (9.0f), Just::centredLeft);
    }
    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int, int& idealWidth, int& idealHeight) override
    {
        if (isSeparator) { idealWidth = 50; idealHeight = 7; return; }
        idealHeight = 21; idealWidth = juce::roundToInt (textWidth (getPopupMenuFont(), text)) + 42;
    }
};
// shows a menu inside the plugin window (never a separate desktop window over the host), next to `target` (or to a part of it)
void showMenu (juce::PopupMenu& m, juce::Component& target, std::function<void (int)> done, RI partOfTarget = {})
{
    m.setLookAndFeel (&target.getLookAndFeel());
    auto o = juce::PopupMenu::Options().withTargetComponent (target).withDeletionCheck (target).withMinimumWidth (juce::jmax (110, partOfTarget.isEmpty() ? target.getWidth() : 0));
    if (! partOfTarget.isEmpty()) o = o.withTargetScreenArea (target.localAreaToGlobal (partOfTarget));
    if (auto* top = target.findParentComponentOfClass<juce::AudioProcessorEditor>()) o = o.withParentComponent (top);
    m.showMenuAsync (o, std::move (done));
}

// ====================================================================================== controls
// A parameter's knob. swap: the name under it turns into the value while it is touched; inside: its number always shows in the cap;
// value: the value always shows under it (the FX panel writes the name itself); number: a tempo read-out; bar: a small level bar.
class Knob final : public juce::Slider, private juce::Timer
{
public:
    enum Mode { swap, inside, value, number, bar };

    Knob (Processor& p, const juce::String& paramId, const juce::String& labelText, Col accentColour, Mode m = swap)
        : label (labelText), accent (accentColour), mode (m)
    {
        setSliderStyle (m == bar ? juce::Slider::LinearBar : juce::Slider::RotaryVerticalDrag);
        setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        setMouseDragSensitivity (m == number ? 320 : 190);
        setPopupMenuEnabled (false); setWantsKeyboardFocus (false);
        prm = p.apvts.getParameter (paramId);
        if (prm != nullptr) att = std::make_unique<juce::SliderParameterAttachment> (*prm, *this, nullptr);
        else { jassertfalse; setEnabled (false); }
        bipolar = getMinimum() < -1.0e-6 && getMaximum() > 1.0e-6;
    }
    ~Knob() override { stopTimer(); }

    juce::String label; Col accent; bool pan = false, grBar = false; float labelCap = 5.4f;   // grBar: a small bar beside the dial shows setActivity()
    std::function<void (Knob&)> onType;                    // right-click: type a value
    void setHost (const juce::String& hostValue)           // the tempo box while the host plays: shows the host's tempo, not its own
    {
        if (hostValue == hostText) return;
        hostText = hostValue; setEnabled (hostText.isEmpty()); repaint();
    }
    void setDim (bool dm) { if (dm != dimmed) { dimmed = dm; repaint(); } }                              // greyed, but still usable
    void setActivity (float a) { if (std::abs (a - activity) > 1.0e-3f) { activity = a; repaint(); } }   // 0..1: the cap lights up (a DUCK knob while its band is pushed down)
    juce::String fullText() { return pan ? valueText() : getTextFromValue (getValue()); }
    void typeIn (const juce::String& typed)
    {
        const auto t = typed.trim(); if (t.isEmpty() || prm == nullptr || ! isEnabled()) return;
        double v = getValueFromText (t);
        if (pan)
        {
            const auto u = t.toUpperCase(); const double n = u.retainCharacters ("0123456789.").getDoubleValue();
            v = u.startsWithChar ('C') ? 0.0 : u.startsWithChar ('L') ? -n / 100.0 : u.startsWithChar ('R') ? n / 100.0 : (std::abs (t.getDoubleValue()) > 1.0 ? t.getDoubleValue() / 100.0 : t.getDoubleValue());
        }
        prm->beginChangeGesture(); setValue (juce::jlimit (getMinimum(), getMaximum(), v), juce::sendNotificationSync); prm->endChangeGesture();
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        if (mode == bar) { paintBar (g, b); return; }
        if (mode == number) { paintNumber (g, b); return; }
        auto area = b; const auto textR = area.removeFromBottom (mode == value ? 13.0f : 11.0f);
        const float d = juce::jmin (area.getWidth() - 2.0f, area.getHeight() - 1.0f);
        paintDial (g, RF (d, d).withCentre (area.getCentre()));
        const float a = alpha();
        if (grBar)   // how far this band is pushed down right now (grows downwards)
        {
            const RF m (area.getCentreX() + d * 0.5f + 2.5f, area.getCentreY() - d * 0.5f + 2.0f, 3.0f, d - 4.0f);
            g.setColour (black (0.5f)); g.fillRoundedRectangle (m, 1.5f);
            if (activity > 0.0f) { g.setColour (accent.interpolatedWith (hi(), 0.55f)); g.fillRoundedRectangle (m.withHeight (juce::jmax (1.5f, m.getHeight() * activity)), 1.5f); }
        }

        if (mode == value) { g.setColour (cText.withAlpha (a)); fitText (g, font (5.8f, Type::bold, 0.0f), valueText(), textR.reduced (1.0f, 0.0f)); return; }
        const bool live = touched();
        g.setColour ((live ? hi() : cDim).withAlpha (a));
        fitText (g, font (labelCap, Type::bold, live ? 0.0f : 0.05f), live ? valueText() : label, textR.expanded (1.0f, 0.0f));
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        skip = e.mods.isPopupMenu();
        if (skip) { if (onType && isEnabled()) onType (*this); return; }
        hot = true; stopTimer(); repaint(); juce::Slider::mouseDown (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (! skip) juce::Slider::mouseDrag (e); }
    void mouseUp (const juce::MouseEvent& e) override { if (! skip) { juce::Slider::mouseUp (e); startTimer (900); } skip = false; }
    void mouseDoubleClick (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) juce::Slider::mouseDoubleClick (e); }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override { hot = true; startTimer (900); juce::Slider::mouseWheelMove (e, w); }
    void mouseEnter (const juce::MouseEvent& e) override { over = true; repaint(); juce::Slider::mouseEnter (e); }
    void mouseExit (const juce::MouseEvent& e) override { over = false; repaint(); juce::Slider::mouseExit (e); }

private:
    void timerCallback() override { stopTimer(); hot = false; repaint(); }
    bool touched() const { return hot || over || isMouseButtonDown(); }
    float alpha() const { return isEnabled() && ! dimmed ? 1.0f : 0.4f; }
    juce::String valueText()
    {
        if (pan) { const double v = getValue(); if (std::abs (v) < 0.005) return "C"; return (v < 0.0 ? "L " : "R ") + juce::String (juce::roundToInt (std::abs (v) * 100.0)); }
        return compact (getTextFromValue (getValue()));
    }
    void paintDial (juce::Graphics& g, RF k)
    {   // Phantom FX's painted neon dial in this knob's colour
        const float a0 = kPi * 1.25f, a1 = kPi * 2.75f, t = (float) valueToProportionOfLength (getValue()), av = a0 + (a1 - a0) * t, a = alpha();
        const float from = bipolar ? a0 + (a1 - a0) * (float) valueToProportionOfLength (0.0) : a0;
        const RF cell = k.withSizeKeepingCentre (k.getWidth() * 1.16f, k.getHeight() * 1.16f);   // the painted ring fills the dial's old circle
        Neon::dial (g, cell, accent, a0, a1, from, av, t, touched(), a, mode != inside);
        const float c = cell.getWidth() / 124.0f, cx = cell.getX() + 62.0f * c, cy = cell.getY() + 61.0f * c, cr = 27.0f * c;
        if (activity > 0.0f) { g.setColour (Neon::lit (accent).withAlpha (0.55f * activity)); g.fillEllipse (cx - cr, cy - cr, cr * 2.0f, cr * 2.0f); }
        if (mode == inside)
        {
            g.setColour (hi().withAlpha (a)); fitText (g, font (cr * 0.8f, Type::heavy, 0.0f), juce::String (juce::roundToInt (getValue())), RF (cx - cr, cy - cr, cr * 2.0f, cr * 2.0f).reduced (1.5f, 0.0f));
        }
    }
    void paintNumber (juce::Graphics& g, RF b)
    {
        const auto r = b.reduced (0.5f); const bool host = hostText.isNotEmpty();
        Neon::nine (g, "neon_led.png", r, 10.0f, 10.0f, 10.0f, 10.0f);   // Phantom FX's LED window
        g.setColour (host ? cCyan.withAlpha (0.7f) : white (touched() ? 0.3f : 0.0f)); g.drawRoundedRectangle (r, 6.0f, 1.0f);
        const float lh = r.getHeight() < 40.0f ? 8.0f : 10.0f, ly = r.getHeight() < 40.0f ? 2.5f : 5.0f;
        g.setColour (host ? cCyan : cDim); drawText (g, font (4.9f, Type::bold, 0.16f), host ? "HOST" : label, RF (r.getX(), r.getY() + ly, r.getWidth(), lh));
        g.setColour (host ? cCyan.interpolatedWith (hi(), 0.6f) : hi());
        fitText (g, font (11.0f, Type::heavy, 0.0f), host ? hostText : juce::String (getValue(), 1), RF (r.getX() + 3.0f, r.getY() + ly + lh + 1.0f, r.getWidth() - 6.0f, r.getHeight() - ly - lh - 4.0f));
    }
    void paintBar (juce::Graphics& g, RF b)
    {
        const auto r = b.reduced (0.5f, 2.0f); const float rad = 3.5f, a = alpha(), t = (float) valueToProportionOfLength (getValue());
        g.setColour (cInk.withAlpha (0.55f)); g.fillRoundedRectangle (r, rad);
        {
            juce::Graphics::ScopedSaveState ss (g); juce::Path clip; clip.addRoundedRectangle (r, rad); g.reduceClipRegion (clip);
            const float z = bipolar ? (float) valueToProportionOfLength (0.0) : 0.0f, x0 = r.getX() + r.getWidth() * juce::jmin (z, t), x1 = r.getX() + r.getWidth() * juce::jmax (z, t);
            g.setColour (accent.withAlpha (0.36f * a)); g.fillRect (RF (x0, r.getY(), x1 - x0, r.getHeight()));
            g.setColour (accent.withAlpha (a)); g.fillRect (RF (r.getX() + r.getWidth() * t - (t > 0.5f ? 1.6f : 0.0f), r.getY(), 1.6f, r.getHeight()));
        }
        g.setColour (white (touched() ? 0.34f : 0.16f)); g.drawRoundedRectangle (r, rad, 1.0f);
        const bool live = touched() || always;
        if (always) { g.setColour (cDim.withAlpha (a)); fitText (g, font (labelCap, Type::bold, 0.06f), label, r.reduced (6.0f, 0.0f).withTrimmedRight (r.getWidth() * 0.34f), Just::centredLeft);
                      g.setColour (hi().withAlpha (a)); fitText (g, font (labelCap, Type::bold, 0.0f), valueText(), r.reduced (6.0f, 0.0f).withTrimmedLeft (r.getWidth() * 0.6f), Just::centredRight); return; }
        g.setColour ((live ? hi() : cText.withAlpha (0.82f)).withAlpha (a));
        fitText (g, font (labelCap, Type::bold, live ? 0.0f : 0.07f), live ? valueText() : label, r.reduced (4.0f, 0.0f));
    }

public:
    bool always = false;   // a bar that always shows its name and its value
private:
    Mode mode; bool bipolar = false, hot = false, over = false, skip = false, dimmed = false; float activity = 0.0f; juce::String hostText;
    juce::RangedAudioParameter* prm = nullptr;
    std::unique_ptr<juce::SliderParameterAttachment> att;
};

// A button. Plain: an action. attach(): a switch on a bool parameter (lit in its colour while on).
class Btn final : public juce::Button
{
public:
    Btn (const juce::String& words, Col accentColour, Icon ic = Icon::none) : juce::Button (words), label (words), accent (accentColour), icon (ic) {}
    void attach (Processor& p, const juce::String& paramId)
    {
        setClickingTogglesState (true);
        if (auto* q = p.apvts.getParameter (paramId)) att = std::make_unique<juce::ButtonParameterAttachment> (*q, *this, nullptr);
        else { jassertfalse; setEnabled (false); }
    }
    juce::String label, offLabel; Col accent; Icon icon; float cap = 5.4f; bool tab = false;

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f); const bool on = getToggleState();
        const float a = isEnabled() ? 1.0f : 0.38f;
        Neon::button (g, r, accent, on, over, down, a);   // Phantom FX's glass button: lit in its colour while on
        const Col ink = (gWater ? (on ? hi() : over ? cText : accent.darker (0.2f))
                                : (on ? Col (0xff0b0a1c) : over ? cText : accent.interpolatedWith (hi(), 0.55f))).withAlpha (a);
        const juce::String t = (! on && offLabel.isNotEmpty()) ? offLabel : label;
        const auto f = font (cap, Type::bold, 0.06f); const auto inner = r.reduced (3.0f, 1.0f);
        if (icon != Icon::none)
        {
            const float is = juce::jmin (inner.getHeight() - 5.0f, t.isEmpty() ? 13.0f : 10.0f);
            const float tw = t.isEmpty() ? 0.0f : juce::jmin (inner.getWidth() - is - 4.0f, textWidth (f, t));
            const float x0 = inner.getCentreX() - (is + (t.isEmpty() ? 0.0f : 4.0f + tw)) * 0.5f;
            drawIcon (g, icon, RF (x0, inner.getCentreY() - is * 0.5f, is, is), icon == Icon::lock ? (on ? Col (0xff0b0a1c) : cDim.withAlpha (0.75f * a)) : ink, on);
            if (t.isNotEmpty()) { g.setColour (ink); fitText (g, f, t, RF (x0 + is + 4.0f, inner.getY(), tw + 1.0f, inner.getHeight()), Just::centredLeft); }
        }
        else if (t.containsChar ('\n'))
        {
            const auto lines = juce::StringArray::fromLines (t); const float lh = cap + 6.0f, y0 = inner.getCentreY() - lh * (float) lines.size() * 0.5f;
            g.setColour (ink); for (int i = 0; i < lines.size(); ++i) fitText (g, f, lines[i], RF (inner.getX(), y0 + lh * (float) i, inner.getWidth(), lh));
        }
        else { g.setColour (ink); fitText (g, f, t, inner); }
    }
private:
    std::unique_ptr<juce::ButtonParameterAttachment> att;
};

// DRAG WAV: dragging out of the button it is on renders the loop as you hear it and hands FL Studio (or Finder) the file
struct WavDrag final : juce::MouseListener
{
    std::function<juce::File()> make; bool done = false;
    void mouseDown (const juce::MouseEvent&) override { done = false; }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (done || e.getDistanceFromDragStart() < 4 || make == nullptr) return;
        done = true; juce::MouseCursor::showWaitCursor(); const auto f = make(); juce::MouseCursor::hideWaitCursor();
        if (f.existsAsFile()) juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, e.eventComponent);
    }
};

// TEMPO 1/2x | 1x | 2x: the beat at half, the same or double the tempo, locked to FL's bars (as in Ghosty Beats and ArpEZator)
class TempoSeg final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit TempoSeg (Processor& p) : proc (p)
    {
        if (auto* q = p.apvts.getParameter ("tempo_mul"))
        { att = std::make_unique<juce::ParameterAttachment> (*q, [this] (float v) { index = juce::roundToInt (v); repaint(); }, nullptr); att->sendInitialUpdate(); }
    }
    void tick() { const juce::String t = "TEMPO: THE BEAT PLAYS AT " + juce::String (proc.playBpm(), 1) + " BPM"; if (t != getTooltip()) setTooltip (t); }
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f); const float w = r.getWidth() / 3.0f;
        g.setColour (cInk.withAlpha (0.62f)); g.fillRoundedRectangle (r, 4.0f);
        static const char* names[] { "1/2x", "1x", "2x" };
        for (int i = 0; i < 3; ++i)
        {
            const RF c (r.getX() + w * (float) i, r.getY(), w, r.getHeight()); const bool on = i == index;
            if (on) { g.setColour (cIce.withAlpha (gWater ? 0.85f : 0.9f)); g.fillRoundedRectangle (c.reduced (1.2f), 3.0f); }
            g.setColour (on ? (gWater ? hi() : Col (0xff0b0a1c)) : cDim); fitText (g, font (4.6f, Type::bold, 0.02f), names[i], c);
        }
        g.setColour (cIce.withAlpha (0.45f)); g.drawRoundedRectangle (r, 4.0f, 1.0f);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (att == nullptr) return;
        const int i = juce::jlimit (0, 2, (int) (e.position.x / juce::jmax (1.0f, (float) getWidth() / 3.0f)));
        if (i != index) att->setValueAsCompleteGesture ((float) i);
    }
private:
    Processor& proc; int index = 1; std::unique_ptr<juce::ParameterAttachment> att;
};

// A choice parameter: shows what is picked, a click opens the list (the mouse wheel steps through it).
class Choice final : public juce::Button
{
public:
    Choice (Processor& p, const juce::String& paramId, Col accentColour) : juce::Button (paramId), accent (accentColour)
    {
        if (auto* q = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter (paramId)))
        {
            items = q->choices;
            att = std::make_unique<juce::ParameterAttachment> (*q, [this] (float v) { index = juce::roundToInt (v); repaint(); }, nullptr);
            att->sendInitialUpdate();
        }
        else { jassertfalse; setEnabled (false); }
        onClick = [this] { open(); };
    }
    Col accent; float cap = 5.6f; bool grouped = false, big = false;   // grouped: "ENGINE . Voice" entries open as one sub-menu per engine; big: the panel's main list
    juce::String current() const { return items[juce::jlimit (0, juce::jmax (0, items.size() - 1), index)]; }
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f); const float a = isEnabled() ? 1.0f : 0.38f;
        Neon::nine (g, over || down ? "neon_menu_hover.png" : "neon_menu.png", r, 14.0f, 10.0f, 34.0f, 12.0f, a);   // Phantom FX's drop-down plate (its arrow is painted on)
        const float arrow = juce::jmin (r.getWidth() * 0.3f, 34.0f * juce::jmin (1.0f, r.getHeight() / 30.0f));
        auto tr = r.withTrimmedRight (arrow).reduced (big ? 10.0f : 4.0f, 0.0f);
        g.setColour (Neon::lit (accent).interpolatedWith (hi(), big ? 0.7f : 0.5f).withAlpha (a)); fitText (g, font (cap, Type::bold, 0.03f), current(), tr, big ? Just::centredLeft : Just::centred);
    }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        if (att == nullptr || ! isEnabled() || std::abs (w.deltaY) < 1.0e-4f) return;
        const int n = juce::jlimit (0, items.size() - 1, index + (w.deltaY > 0.0f ? -1 : 1)); if (n != index) att->setValueAsCompleteGesture ((float) n);
    }
private:
    void open()
    {
        if (att == nullptr) return;
        juce::PopupMenu m; const juce::String dot (juce::CharPointer_UTF8 (" \xc2\xb7 "));
        for (int i = 0; i < items.size();)
        {
            if (! grouped || ! items[i].contains (dot)) { m.addItem (i + 1, items[i], true, i == index); ++i; continue; }
            const juce::String head = items[i].upToFirstOccurrenceOf (dot, true, false); juce::PopupMenu sub; bool has = false;   // every voice of one engine
            for (; i < items.size() && items[i].startsWith (head); ++i) { sub.addItem (i + 1, items[i].substring (head.length()), true, i == index); has = has || i == index; }
            m.addSubMenu (head.upToFirstOccurrenceOf (dot, false, false), sub, true, std::unique_ptr<juce::Drawable>(), has);
        }
        showMenu (m, *this, [safe = juce::Component::SafePointer<Choice> (this)] (int r) { if (safe != nullptr && r > 0 && safe->att != nullptr) safe->att->setValueAsCompleteGesture ((float) (r - 1)); });
    }
    juce::StringArray items; int index = 0;
    std::unique_ptr<juce::ParameterAttachment> att;
};

// a button that names what is loaded (a drum sound) and opens its list
class MenuBtn final : public juce::Button
{
public:
    explicit MenuBtn (Col accentColour) : juce::Button ("menu"), accent (accentColour) {}
    juce::String shown; Col accent;
    void setText (const juce::String& t) { if (t != shown) { shown = t; repaint(); } }
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        Neon::nine (g, over || down ? "neon_menu_hover.png" : "neon_menu.png", r, 14.0f, 10.0f, 34.0f, 12.0f);
        const float arrow = juce::jmin (r.getWidth() * 0.3f, 34.0f * juce::jmin (1.0f, r.getHeight() / 30.0f));
        g.setColour (cText); fitText (g, font (5.4f, Type::bold, 0.03f), shown, r.withTrimmedRight (arrow).reduced (6.0f, 0.0f), Just::centredLeft, 0.62f);
    }
};

// the big HOLD: down = the breaks stutter, up = back to the beat. Lit whenever the engine stutters (the F2 key holds it too).
class HoldBtn final : public juce::Component
{
public:
    explicit HoldBtn (Processor& p) : proc (p), prm (p.apvts.getParameter ("stutter")) { jassert (prm != nullptr); setComponentID ("hold"); }
    ~HoldBtn() override { release(); }
    void tick() { const bool on = held || proc.engine.uiStutter.load(); if (on != lit) { lit = on; repaint(); } }
    void mouseDown (const juce::MouseEvent&) override { if (prm == nullptr || held) return; held = true; prm->beginChangeGesture(); prm->setValueNotifyingHost (1.0f); tick(); }
    void mouseUp (const juce::MouseEvent&) override { release(); tick(); }
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (3.5f); juce::Path p; p.addRoundedRectangle (r, 9.0f);
        g.setGradientFill (juce::ColourGradient (lit ? cPink.withAlpha (0.62f) : (gWater ? cGlass : Col (0xff1c1438)).withAlpha (0.9f), r.getCentreX(), r.getY(),
                                                 lit ? cPink.withAlpha (0.26f) : cInk.withAlpha (0.9f), r.getCentreX(), r.getBottom(), false));
        g.fillPath (p); neonStroke (g, p, cPink, lit ? 2.2f : 1.5f, lit ? 2.0f : 1.0f);
        g.setColour (lit ? hi() : cPink.interpolatedWith (hi(), 0.6f));
        fitText (g, font (11.5f, Type::italic, 0.03f), "HOLD", RF (r.getX() + 4.0f, r.getY() + 9.0f, r.getWidth() - 8.0f, 24.0f));
        g.setColour (lit ? white (0.95f) : cDim); fitText (g, font (4.7f, Type::bold, 0.14f), "TO STUTTER", RF (r.getX() + 4.0f, r.getBottom() - 19.0f, r.getWidth() - 8.0f, 10.0f));
    }
private:
    void release() { if (prm == nullptr || ! held) return; held = false; prm->setValueNotifyingHost (0.0f); prm->endChangeGesture(); }
    Processor& proc; juce::RangedAudioParameter* prm; bool held = false, lit = false;
};

// RANDOM START / RANDOM COMBO: the two main signs of the street. The tube and its halo are painted once and kept as images;
// the halo breathes a little and flares when the sign is hit.
class HeroBtn final : public juce::Button
{
public:
    HeroBtn (const juce::String& big, const juce::String& small, Col colour, int picture)   // 0 a break, 1 four bays, 2 the drum grid
        : juce::Button (big), line1 (big), line2 (small), col (colour), pic (picture) {}
    void tick (float breathing)
    {
        const float f = juce::jmax (0.0f, flash - 0.07f); const int q = juce::roundToInt (breathing * 12.0f);
        if (std::abs (f - flash) > 1.0e-4f || q != breathQ) { flash = f; breathQ = q; breath = breathing; repaint(); }
    }
    using juce::Button::clicked;
    void clicked() override { flash = 1.0f; repaint(); }
    void resized() override { base = {}; halo = {}; }
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const float sc = scaleOf (g); if (! base.isValid() || std::abs (sc - imgScale) > 0.01f) render (sc);
        const auto b = getLocalBounds().toFloat();
        g.setOpacity (juce::jlimit (0.0f, 1.0f, 0.66f + 0.24f * breath + (over ? 0.1f : 0.0f))); g.drawImage (halo, b);
        if (flash > 0.01f || down) { g.setOpacity (juce::jlimit (0.0f, 1.0f, flash + (down ? 0.6f : 0.0f))); g.drawImage (halo, b); }
        g.setOpacity (1.0f); g.drawImage (base, down ? b.translated (0.0f, 1.0f) : b);
        if (flash > 0.01f || down) { g.setColour (white (juce::jlimit (0.0f, 0.3f, 0.22f * flash + (down ? 0.1f : 0.0f)))); g.fillRoundedRectangle (b.reduced (7.0f), 10.0f); }
    }
private:
    void render (float sc)
    {
        imgScale = sc;
        auto make = [this, sc] (bool glowLayer)
        {
            juce::Image im (juce::Image::ARGB, juce::jmax (1, juce::roundToInt ((float) getWidth() * sc)), juce::jmax (1, juce::roundToInt ((float) getHeight() * sc)), true);
            juce::Graphics g (im); g.addTransform (juce::AffineTransform::scale (sc)); sign (g, glowLayer); return im;
        };
        halo = make (true); blur (halo, juce::jmax (1, juce::roundToInt (3.0f * sc))); base = make (false);
    }
    void sign (juce::Graphics& g, bool glowLayer) const
    {
        const auto r = getLocalBounds().toFloat().reduced (7.0f); const float rad = 10.0f; const Col hot = gWater ? col.darker (0.45f) : col.interpolatedWith (hi(), 0.78f);
        if (gWater && ! glowLayer)
        {   // a painted card: paper, a wash of the colour pooling at the bottom, an ink edge
            g.setColour (cPaper.withAlpha (0.92f)); g.fillRoundedRectangle (r, rad);
            g.setGradientFill (juce::ColourGradient (col.withAlpha (0.05f), r.getCentreX(), r.getY(), col.withAlpha (0.38f), r.getCentreX(), r.getBottom(), false)); g.fillRoundedRectangle (r, rad);
            g.setColour (col.darker (0.2f)); g.drawRoundedRectangle (r, rad, 2.0f);
        }
        else if (! glowLayer)
        {
            g.setGradientFill (juce::ColourGradient (Col (0xff1d133b).interpolatedWith (col, 0.12f), r.getCentreX(), r.getY(), Col (0xff090717), r.getCentreX(), r.getBottom(), false));
            g.fillRoundedRectangle (r, rad);
        }
        if (! gWater)
        {
            g.setColour (glowLayer ? col : col.interpolatedWith (hi(), 0.15f)); g.drawRoundedRectangle (r, rad, glowLayer ? 3.6f : 2.0f);
            if (! glowLayer) { g.setColour (white (0.85f)); g.drawRoundedRectangle (r, rad, 0.7f); }
        }
        else if (glowLayer) { g.setColour (col.withAlpha (0.5f)); g.drawRoundedRectangle (r, rad, 3.0f); }
        const RF ic (r.getX() + (r.getWidth() < 170.0f ? 8.0f : 13.0f), r.getCentreY() - 11.0f, 22.0f, 22.0f);
        if (pic == 2)   // the drum grid: hits on six lanes
            for (int q = 0; q < 12; ++q)
            {
                static const bool hit[] { 1, 0, 1, 1, 0, 1, 0, 1, 1, 1, 0, 1 };
                g.setColour (glowLayer ? col : (hit[q] ? col.interpolatedWith (hi(), 0.3f) : white (0.18f)));
                g.fillRoundedRectangle (RF (ic.getX() + (float) (q % 4) * 6.0f, ic.getY() + 2.0f + (float) (q / 4) * 6.5f, 4.6f, 5.0f), 1.0f);
            }
        else if (pic == 1)   // four bays, one beat
            for (int q = 0; q < 4; ++q)
            {
                g.setColour (glowLayer ? bayCol (q) : bayCol (q).interpolatedWith (hi(), 0.25f));
                g.fillRoundedRectangle (RF (ic.getX() + (float) (q % 2) * 12.0f, ic.getY() + (float) (q / 2) * 12.0f, 10.0f, 10.0f), 2.5f);
            }
        else         // a break, and the marker that says where it starts
        {
            static const float hts[] { 0.45f, 0.9f, 0.6f, 1.0f, 0.5f, 0.75f };
            for (int q = 0; q < 6; ++q)
            {
                const float bh = 15.0f * hts[q]; g.setColour (glowLayer ? col : (q == 3 ? hot : col.interpolatedWith (hi(), 0.2f)));
                g.fillRoundedRectangle (RF (ic.getX() + (float) q * 3.9f, ic.getBottom() - bh, 2.6f, bh), 1.0f);
            }
            juce::Path m; const float mx = ic.getX() + 3.0f * 3.9f + 1.3f; m.addTriangle (mx - 3.6f, ic.getY() - 1.0f, mx + 3.6f, ic.getY() - 1.0f, mx, ic.getY() + 4.2f);
            g.setColour (glowLayer ? col : hi()); g.fillPath (m);
        }
        const float tx = ic.getRight() + (r.getWidth() < 170.0f ? 6.0f : 10.0f), tw = r.getRight() - tx - 6.0f;
        g.setColour (glowLayer ? col : hot); fitText (g, font (gWater ? 7.6f : 10.0f, Type::italic, 0.03f), line1, RF (tx, r.getY() + 5.0f, tw, 19.0f), Just::centredLeft, 0.5f);
        if (! glowLayer) { g.setColour (gWater ? cDim : col.interpolatedWith (cText, 0.5f).withAlpha (0.95f)); fitText (g, font (4.5f, Type::bold, 0.14f), line2, RF (tx, r.getBottom() - 14.5f, tw, 9.0f), Just::centredLeft); }
    }
    juce::String line1, line2; Col col; int pic; juce::Image base, halo; float imgScale = 0.0f, flash = 0.0f, breath = 0.5f; int breathQ = -1;
};

// PLAY: its own clock. While the host plays it follows the host: lit, marked HOST, and not clickable.
class PlayBtn final : public juce::Button
{
public:
    explicit PlayBtn (Processor& p) : juce::Button ("play")
    {
        setClickingTogglesState (true); setComponentID ("play");
        if (auto* q = p.apvts.getParameter ("play")) att = std::make_unique<juce::ButtonParameterAttachment> (*q, *this, nullptr);
        else { jassertfalse; setEnabled (false); }
    }
    void setHost (bool h) { if (h == host) return; host = h; setEnabled (! h); repaint(); }
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f); const bool on = getToggleState() || host; const Col c = host ? cCyan : cMint;
        g.setColour (on ? c.withAlpha (0.2f) : cInk.withAlpha (0.62f)); g.fillRoundedRectangle (r, 6.0f);
        if (on) { g.setGradientFill (juce::ColourGradient (c.withAlpha (0.36f), r.getCentreX(), r.getBottom(), c.withAlpha (0.0f), r.getCentreX(), r.getY(), false)); g.fillRoundedRectangle (r, 6.0f); }
        if (down) { g.setColour (white (0.14f)); g.fillRoundedRectangle (r, 6.0f); }
        g.setColour (on ? c : white (over ? 0.36f : 0.2f)); g.drawRoundedRectangle (r, 6.0f, on ? 1.4f : 1.0f);
        const float cx = r.getCentreX(), iy = r.getY() + 17.5f; juce::Path p; p.addTriangle (cx - 5.0f, iy - 7.5f, cx - 5.0f, iy + 7.5f, cx + 8.0f, iy);
        g.setColour (on ? hi() : cText); g.fillPath (p);
        g.setColour (on ? c.interpolatedWith (hi(), 0.55f) : cDim); drawText (g, font (4.9f, Type::bold, 0.16f), host ? "HOST" : "PLAY", RF (r.getX(), r.getBottom() - 15.0f, r.getWidth(), 10.0f));
    }
private:
    bool host = false;
    std::unique_ptr<juce::ButtonParameterAttachment> att;
};

// the lettering of the owner's art, with a little snow falling past it
class LogoView final : public juce::Component
{
public:
    explicit LogoView (const juce::Image& lettering) : src (lettering)
    {
        setInterceptsMouseClicks (false, false); juce::Random r (7);
        for (auto& f : flakes) f = { r.nextFloat(), r.nextFloat(), 0.004f + r.nextFloat() * 0.007f, 0.8f + r.nextFloat() * 1.3f, r.nextFloat() * 6.28f };
    }
    void tick() { for (auto& f : flakes) { f.y += f.v; f.ph += 0.05f; if (f.y > 1.05f) { f.y = -0.05f; f.x = std::fmod (f.x + 0.37f, 1.0f); } } repaint(); }
    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        if (src.isValid())
        {
            const float sc = scaleOf (g), fit = juce::jmin (b.getWidth() / (float) src.getWidth(), b.getHeight() / (float) src.getHeight());
            const int tw = juce::jmax (1, juce::roundToInt ((float) src.getWidth() * fit * sc)), th = juce::jmax (1, juce::roundToInt ((float) src.getHeight() * fit * sc));
            if (! scaled.isValid() || scaled.getWidth() != tw || scaled.getHeight() != th)
            {
                scaled = juce::Image (juce::Image::ARGB, tw, th, true); juce::Graphics sg (scaled);
                sg.setImageResamplingQuality (juce::Graphics::highResamplingQuality); sg.drawImage (src, RF (0.0f, 0.0f, (float) tw, (float) th));
            }
            g.drawImage (scaled, RF ((float) tw / sc, (float) th / sc).withCentre (b.getCentre()));
        }
        else { g.setColour (cPink); fitText (g, font (11.0f, Type::italic), "GHOST BREAK", b); }
        for (const auto& f : flakes)
        {
            g.setColour (gWater ? cPink.withAlpha (0.35f + 0.3f * f.s / 2.1f) : white (0.25f + 0.3f * f.s / 2.1f));   // snow, or sakura petals
            if (gWater) g.fillEllipse (b.getX() + (f.x + std::sin (f.ph) * 0.03f) * b.getWidth(), b.getY() + f.y * b.getHeight(), f.s * 2.2f, f.s * 1.3f);
            else g.fillEllipse (b.getX() + (f.x + std::sin (f.ph) * 0.02f) * b.getWidth(), b.getY() + f.y * b.getHeight(), f.s, f.s);
        }
    }
private:
    struct Flake { float x, y, v, s, ph; };
    const juce::Image& src; juce::Image scaled; Flake flakes[9] {};
};

// the ghost: floats between the two signs, hops when one of them is hit, nods on the kick
// the edge under a bay: drag it down and the bay grows (the others shrink a little), drag it back up; double-click: all the same size
class BayDivider final : public juce::Component
{
public:
    std::function<void (int, bool)> onDrag; std::function<void()> onReset;
    BayDivider() { setMouseCursor (juce::MouseCursor::UpDownResizeCursor); setRepaintsOnMouseActivity (true); }
    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat(); const bool hot = isMouseOverOrDragging();
        g.setColour (white (hot ? 0.6f : 0.18f)); g.fillRoundedRectangle (RF (b.getCentreX() - 22.0f, b.getCentreY() - 1.2f, 44.0f, 2.4f), 1.2f);
    }
    void mouseDown (const juce::MouseEvent& e) override { startY = e.getScreenY(); if (onDrag) onDrag (0, true); }
    void mouseDrag (const juce::MouseEvent& e) override
    {   // in the window's own (unscaled) points
        const float s = getParentComponent() != nullptr ? juce::jmax (0.1f, (float) getParentComponent()->getWidth() / (float) kW) : 1.0f;
        if (onDrag) onDrag (juce::roundToInt ((float) (e.getScreenY() - startY) / s), false);
    }
    void mouseDoubleClick (const juce::MouseEvent&) override { if (onReset) onReset(); }
private:
    int startY = 0;
};

// the stutter's picture: the sound it works on, scrolling; lit in its colour where the stutter repeats
class StutterScope final : public juce::Component
{
public:
    explicit StutterScope (Engine& e) : eng (e) { setInterceptsMouseClicks (false, false); }
    void tick() { const int at = eng.scopeAt.load (std::memory_order_acquire); if (at != last) { last = at; repaint(); } }
    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat(); const int N = Engine::kScope, at = last;
        g.setColour (black (0.4f)); g.fillRoundedRectangle (b, 3.0f);
        const float w = b.getWidth() / (float) N, mid = b.getCentreY(), hh = b.getHeight() * 0.46f;
        for (int i = 0; i < N; ++i)
        {
            const int k = (at + 1 + i) % N; const float pk = juce::jlimit (0.0f, 1.0f, eng.scopePeak[(size_t) k].load (std::memory_order_relaxed)); const bool on = eng.scopeOn[(size_t) k].load (std::memory_order_relaxed);
            const float h = juce::jmax (0.6f, std::sqrt (pk) * hh);
            g.setColour (on ? cPink : white (0.35f)); g.fillRect (RF (b.getX() + (float) i * w, mid - h, juce::jmax (0.8f, w - 0.3f), h * 2.0f));
        }
        g.setColour (white (0.25f)); g.drawRoundedRectangle (b.reduced (0.5f), 3.0f, 0.8f);
    }
private:
    Engine& eng; int last = -1;
};

class GhostView final : public juce::Component
{   // Brandon's painted ghost with his candle: the flame flickers and glows, drifting through the Ghost Break colours; the ghost
    // bobs, hops on RANDOM, and dips on every kick
public:
    explicit GhostView (const juce::Image&) { setInterceptsMouseClicks (false, false); }
    void hop() { jump = 1.0f; flare = 1.0f; }
    void nod() { squash = 1.0f; flare = juce::jmax (flare, 0.5f); }
    void tick()
    {
        phase += 0.07f; hue += 0.0045f; jump = jump < 0.02f ? 0.0f : jump * 0.9f; squash = squash < 0.02f ? 0.0f : squash * 0.78f; flare *= 0.9f;
        auto& rnd = juce::Random::getSystemRandom();
        flick += (rnd.nextFloat() - 0.5f) * 0.5f - flick * 0.35f; sway += (rnd.nextFloat() - 0.5f) * 0.4f - sway * 0.25f;   // a living flame
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        const auto& img = Neon::img ("ghost_candle.png"); if (! img.isValid()) return;
        const auto b = getLocalBounds().toFloat();
        if (gWater)   // on paper the white ghost needs a night behind it: a soft wash of indigo ink
        {
            g.setGradientFill (juce::ColourGradient (Col (0xff232c58).withAlpha (0.8f), b.getCentreX(), b.getCentreY(), Col (0xff232c58).withAlpha (0.0f), b.getCentreX() + b.getWidth() * 0.62f, b.getCentreY(), true));
            g.fillEllipse (b.expanded (4.0f, 0.0f));
        }
        const float fit = juce::jmin (b.getWidth() / (float) img.getWidth(), (b.getHeight() - 2.0f) / (float) img.getHeight()), w = (float) img.getWidth() * fit, h = (float) img.getHeight() * fit;
        const float dy = std::sin (phase) * 1.2f - std::abs (std::sin (jump * kPi * 3.0f)) * jump * 4.0f + squash * 1.5f;
        const auto t = juce::AffineTransform::scale (fit * (1.0f + 0.05f * squash), fit * (1.0f - 0.05f * squash)).translated (-w * 0.5f, -h * 0.5f)
                           .rotated (std::sin (phase * 0.5f) * 0.025f).translated (b.getCentreX() + 2.0f, b.getCentreY() + dy);
        // the flame's colour: drifting around the palette (pink, ice blue, violet, mint, gold)
        static const Col pal[] { cPink, cCyan, cViolet, cMint, cAmber };
        const float hp = hue - std::floor (hue); const float fi = hp * 5.0f; const int i0 = (int) fi % 5, i1 = (i0 + 1) % 5;
        const Col fc = pal[i0].interpolatedWith (pal[i1], fi - std::floor (fi));
        float fx = 0.132f * (float) img.getWidth(), fy = 0.254f * (float) img.getHeight(); t.transformPoint (fx, fy);
        const float fh = 0.054f * (float) img.getHeight() * fit * (1.0f + 0.16f * flick + 0.35f * flare), fw = fh * 0.42f;
        // the glow first (behind the painting's hand and candle too), then the painting, then the flame on top
        const float gr = fh * (2.3f + 0.4f * flick + 0.8f * flare);
        g.setGradientFill (juce::ColourGradient (fc.withAlpha (0.55f + 0.2f * flare), fx, fy, fc.withAlpha (0.0f), fx + gr, fy, true)); g.fillEllipse (fx - gr, fy - gr, gr * 2.0f, gr * 2.0f);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality); g.drawImageTransformed (img, t);
        g.setGradientFill (juce::ColourGradient (fc.withAlpha (0.28f), fx, fy, fc.withAlpha (0.0f), fx + gr * 0.7f, fy, true)); g.fillEllipse (fx - gr * 0.7f, fy - gr * 0.7f, gr * 1.4f, gr * 1.4f);
        auto drop = [&] (float scale, Col c)
        {   // a teardrop flame, its tip swaying
            const float hh = fh * scale, ww = fw * scale, base = fy + fh * 0.42f, tipX = fx + sway * fw * 0.9f;
            juce::Path p; p.startNewSubPath (tipX, base - hh);
            p.cubicTo (fx + ww * 0.9f, base - hh * 0.45f, fx + ww, base - hh * 0.05f, fx, base);
            p.cubicTo (fx - ww, base - hh * 0.05f, fx - ww * 0.9f, base - hh * 0.45f, tipX, base - hh);
            g.setColour (c); g.fillPath (p);
        };
        drop (1.0f, fc.withAlpha (0.92f));
        drop (0.7f, fc.interpolatedWith (hi(), 0.55f));
        drop (0.38f, juce::Colours::white.withAlpha (0.95f));
    }
private:
    float phase = 0.0f, hue = 0.0f, jump = 0.0f, squash = 0.0f, flare = 0.0f, flick = 0.0f, sway = 0.0f;
};

// the output level: a row of little lights (-42 .. +3 dB), with a peak that hangs
class MeterView final : public juce::Component
{
public:
    explicit MeterView (Processor& p) : proc (p) { setInterceptsMouseClicks (false, false); }
    static constexpr int kSeg = 26;
    void tick()
    {
        const float m = proc.engine.meter.load(); level = juce::jmax (m, level * 0.84f);
        if (m >= hold) { hold = m; wait = 28; } else if (--wait <= 0) hold *= 0.88f;
        const int q = seg (level), hq = seg (hold); if (q != litQ || hq != holdQ) { litQ = q; holdQ = hq; repaint(); }
    }
    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat(); const float sw = b.getWidth() / (float) kSeg;
        for (int i = 0; i < kSeg; ++i)
        {
            const float db = -42.0f + 45.0f * ((float) i + 0.5f) / (float) kSeg; const Col c = db > 0.0f ? cRed : db > -6.0f ? cAmber : cCyan.interpolatedWith (cIce, 0.45f);
            g.setColour ((i < litQ || (holdQ > 0 && i == holdQ - 1)) ? c : white (0.1f)); g.fillRoundedRectangle (RF (b.getX() + (float) i * sw, b.getY(), sw - 1.2f, b.getHeight()), 1.0f);
        }
    }
private:
    static int seg (float v) { return v <= 1.0e-4f ? 0 : juce::jlimit (0, kSeg, juce::roundToInt ((juce::Decibels::gainToDecibels (v) + 42.0f) / 45.0f * (float) kSeg)); }
    Processor& proc; float level = 0.0f, hold = 0.0f; int wait = 0, litQ = 0, holdQ = 0;
};

// a drum lane's name: click to hear the drum; it flashes whenever the drum plays
class PadBtn final : public juce::Component
{
public:
    PadBtn (Processor& p, int drum) : proc (p), lane (drum) { setComponentID ("pad" + juce::String (drum)); }
    void hit() { glow = 1.0f; repaint(); }
    void tick() { if (glow > 0.0f) { glow = glow < 0.06f ? 0.0f : glow * 0.8f; repaint(); } }
    void setDim (bool d) { if (d != dim) { dim = d; repaint(); } }
    // click: hear the drum. Double-click or right-click: type the lane's own name (empty = back to its starting name)
    // click: hear the drum and open its sound window. Right-click: type the lane's own name
    void mouseDown (const juce::MouseEvent& e) override { if (e.mods.isPopupMenu()) { rename(); return; } proc.engine.pushPad (lane, 0.9f); down = true; hit(); }
    void mouseUp (const juce::MouseEvent& e) override { const bool was = down; down = false; repaint(); if (was && e.getNumberOfClicks() == 1 && ! e.mouseWasDraggedSinceMouseDown() && onOpen) onOpen(); }
    std::function<void()> onOpen;
    void mouseDoubleClick (const juce::MouseEvent&) override { rename(); }
    void rename()
    {
        if (typing != nullptr) return;
        typing = std::make_unique<juce::TextEditor>(); auto& t = *typing; addAndMakeVisible (t);
        t.setBounds (getLocalBounds().reduced (1).withTrimmedLeft (17)); t.setFont (font (6.4f, Type::bold, 0.1f)); t.setInputRestrictions (16);
        t.setColour (juce::TextEditor::backgroundColourId, cInk); t.setColour (juce::TextEditor::textColourId, hi());
        t.setColour (juce::TextEditor::outlineColourId, cIce); t.setColour (juce::TextEditor::focusedOutlineColourId, cIce); t.setJustification (Just::centredLeft);
        t.setText (proc.laneName (lane), false); t.selectAll();
        auto done = [safe = juce::Component::SafePointer<PadBtn> (this)] (bool keep) {
            if (safe == nullptr || safe->typing == nullptr) return;
            const juce::String v = safe->typing->getText(); auto* self = safe.getComponent();
            juce::MessageManager::callAsync ([safe, v, keep] { if (safe == nullptr) return; if (keep) safe->proc.setLaneName (safe->lane, v); safe->typing.reset(); safe->repaint(); });
            juce::ignoreUnused (self); };
        t.onReturnKey = [done] { done (true); }; t.onFocusLost = [done] { done (true); }; t.onEscapeKey = [done] { done (false); };
        t.grabKeyboardFocus();
    }
    void mouseEnter (const juce::MouseEvent&) override { over = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { over = false; repaint(); }
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f); const float a = dim ? 0.45f : 1.0f;
        g.setColour (cInk.withAlpha (0.5f)); g.fillRoundedRectangle (r, 4.0f);
        if (glow > 0.0f) { g.setColour (cIce.withAlpha (0.42f * glow)); g.fillRoundedRectangle (r, 4.0f); }
        g.setColour (glow > 0.0f ? cIce.withAlpha (0.25f + 0.75f * glow) : white (over || down ? 0.36f : 0.18f)); g.drawRoundedRectangle (r, 4.0f, 1.0f);
        juce::Path p; const float px = r.getX() + 8.0f, py = r.getCentreY(); p.addTriangle (px, py - 3.6f, px, py + 3.6f, px + 5.6f, py);
        g.setColour (cIce.interpolatedWith (hi(), glow).withAlpha (a * (0.55f + 0.45f * glow))); g.fillPath (p);
        g.setColour (cText.interpolatedWith (hi(), glow).withAlpha (a)); fitText (g, font (5.8f, Type::bold, 0.1f), proc.laneName (lane), r.withTrimmedLeft (19.0f).withTrimmedRight (4.0f), Just::centredLeft);
    }
private:
    Processor& proc; int lane; float glow = 0.0f; bool over = false, down = false, dim = false; std::unique_ptr<juce::TextEditor> typing;
};

// a bay's break: its family on the first line, its variation under it; a click opens the list
class BreakBtn final : public juce::Button
{
public:
    explicit BreakBtn (Col accentColour) : juce::Button ("break"), accent (accentColour) {}
    void show (const juce::String& fullName, const juce::String& where)
    {
        juce::String a = fullName, b = where;
        if (where.isEmpty() && fullName.contains (" - ")) { a = fullName.upToLastOccurrenceOf (" - ", false, false); b = fullName.fromLastOccurrenceOf (" - ", false, false); }
        if (a != top || b != sub) { top = a; sub = b; repaint(); }
    }
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        Neon::nine (g, over || down ? "neon_menu_hover.png" : "neon_menu.png", r, 14.0f, 10.0f, 34.0f, 12.0f);   // Phantom FX's drop-down plate
        g.setColour (Neon::lit (accent).withAlpha (over || down ? 0.9f : 0.55f)); g.drawRoundedRectangle (r.reduced (1.0f), 5.0f, 1.0f);
        auto tr = r.reduced (7.0f, 0.0f); tr.removeFromRight (juce::jmin (26.0f, r.getWidth() * 0.22f));
        const bool two = sub.isNotEmpty();
        g.setColour (cText); fitText (g, font (6.2f, Type::bold, 0.03f), top.toUpperCase(), two ? RF (tr.getX(), r.getY() + 4.0f, tr.getWidth(), 13.0f) : tr, Just::centredLeft, 0.6f);
        if (two) { g.setColour (Neon::lit (accent).interpolatedWith (hi(), 0.35f)); fitText (g, font (4.7f, Type::bold, 0.12f), sub.toUpperCase(), RF (tr.getX(), r.getBottom() - 14.0f, tr.getWidth(), 10.0f), Just::centredLeft); }
    }
private:
    Col accent; juce::String top, sub;
};

// where the 32 steps sit across a strip: a wider gap after every beat, wider again at the bar line (the same in every bay and in the drums)
struct Steps
{
    static constexpr float kBeat = 2.5f, kBar = 3.0f;
    static float pitch (float w) { return (w - kBeat * 7.0f - kBar) / 32.0f; }
    static float x (int s, float w) { return (float) s * pitch (w) + kBeat * (float) (s / 4) + kBar * (float) (s / 16); }
    static float cell (float w) { return pitch (w) - 1.6f; }
    static int at (float px, float w) { int s = 0; for (int i = 1; i < kSteps; ++i) if (px >= x (i, w) - 0.8f) s = i; return s; }
};

// ====================================================================================== a bay's break
// The break as a skyline over a wet street: bars above the water line, their reflection under it. Each slice wears its own colour
// (the same one its cells wear in the sequence under it). Live: the slice that is playing lights up; START shows where NUDGE puts
// slice 1. Click or drag to move START; drop an audio file to load it.
class WaveView final : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    WaveView (Processor& p, int bayIndex) : proc (p), bay (bayIndex)
    {
        const juce::String pre = bayPrefix (bay);
        nudge = p.apvts.getParameter (pre + "nudge"); nudgeRaw = p.apvts.getRawParameterValue (pre + "nudge"); gridRaw = p.apvts.getRawParameterValue (pre + "grid");
        jassert (nudge != nullptr && nudgeRaw != nullptr && gridRaw != nullptr);
        setComponentID ("wave" + juce::String (bay)); refresh();
    }
    std::function<void (const juce::File&)> onFile;

    void refresh() { const auto b = proc.pattern().brk[(size_t) bay]; if (b != brk) { brk = b; img = {}; } repaint(); }
    void say (const juce::String& m) { msg = m; msgTicks = 110; repaint(); }
    void tick (bool running, bool isMuted)
    {
        const int p = running && ! isMuted ? proc.engine.uiPos[bay].load() : -1, nv = nudgeRaw != nullptr ? juce::roundToInt (nudgeRaw->load()) : 0, gv = gridRaw != nullptr ? juce::roundToInt (gridRaw->load()) : 2;
        if (p != pos) { repaintSlice (pos); pos = p; repaintSlice (pos); }
        if (nv != nudgeV || gv != gridV || isMuted != muted) { nudgeV = nv; gridV = gv; muted = isMuted; repaint(); }
        if (msgTicks > 0 && --msgTicks == 0) { msg.clear(); repaint(); }
    }
    void resized() override { img = {}; }
    void paint (juce::Graphics& g) override
    {
        const float sc = scaleOf (g); if (! img.isValid() || std::abs (sc - imgScale) > 0.01f) render (sc);
        const auto b = getLocalBounds().toFloat(); const float w = b.getWidth(), h = b.getHeight(); const Col c = bayCol (bay);
        g.setOpacity (muted ? 0.36f : 1.0f); g.drawImage (img, b); g.setOpacity (1.0f);
        const int total = totalUnits();
        if (pos >= 0 && total > 0)   // the slice that is playing
        {
            const float x0 = w * (float) (pos % total) / (float) total, sw = w * 4.0f / (float) total;
            for (int k = 0; k < 2; ++k)
            {
                const RF r (x0 - (k == 1 ? w : 0.0f), 0.0f, sw, h); if (r.getRight() <= 0.0f) continue;
                g.setColour (white (0.16f)); g.fillRect (r);
                g.setColour (c.withAlpha (0.4f)); g.fillRect (RF (r.getX() - 1.5f, 0.0f, 4.0f, h));
                g.setColour (hi()); g.fillRect (RF (r.getX(), 0.0f, 1.4f, h));
            }
        }
        if (total > 0)               // where NUDGE puts slice 1
        {
            const float x = w * (float) ((nudgeV * gridUnit64 (gridV)) % total) / (float) total, a = muted ? 0.5f : 1.0f;
            juce::Path m; m.addTriangle (x - 4.5f, 0.0f, x + 4.5f, 0.0f, x, 6.5f);
            g.setColour (white (0.5f * a)); for (float y = 7.0f; y < h * 0.64f; y += 4.0f) g.fillRect (RF (x - 0.5f, y, 1.0f, 2.0f));
            g.setColour (c.interpolatedWith (hi(), 0.7f).withAlpha (a)); g.fillPath (m);
            const auto f = font (4.3f, Type::bold, 0.12f); const bool left = x > w - 34.0f;
            drawText (g, f, "START", left ? RF (x - 36.0f, 1.0f, 30.0f, 9.0f) : RF (x + 6.5f, 1.0f, 30.0f, 9.0f), left ? Just::centredRight : Just::centredLeft);
        }
        if (brk != nullptr)
        {
            g.setColour (cDim.withAlpha (muted ? 0.45f : 0.8f));
            drawText (g, font (4.3f, Type::bold, 0.1f), juce::String (juce::roundToInt (brk->bpm)) + " BPM", RF (w - 64.0f, h - 11.0f, 60.0f, 9.0f), Just::centredRight);
        }
        if (dragOver) { g.setColour (c.withAlpha (0.22f)); g.fillRoundedRectangle (b, 4.0f); g.setColour (c); g.drawRoundedRectangle (b.reduced (1.0f), 4.0f, 1.6f); }
        const juce::String t = dragOver ? "DROP THE BREAK HERE" : (brk == nullptr ? "NO BREAK  -  DROP AN AUDIO FILE HERE" : msg);
        if (t.isNotEmpty())
        {
            const auto f = font (6.4f, Type::bold, 0.08f); const RF box = RF (juce::jmin (w - 16.0f, textWidth (f, t) + 22.0f), 20.0f).withCentre (b.getCentre());
            g.setColour (cInk.withAlpha (0.86f)); g.fillRoundedRectangle (box, 5.0f); g.setColour (dragOver || brk == nullptr ? c : cAmber); g.drawRoundedRectangle (box, 5.0f, 1.0f);
            g.setColour (hi()); fitText (g, f, t, box.reduced (8.0f, 0.0f));
        }
    }
    void mouseDown (const juce::MouseEvent& e) override { if (nudge == nullptr || brk == nullptr || e.mods.isPopupMenu()) return; moving = true; nudge->beginChangeGesture(); place (e.position.x); }
    void mouseDrag (const juce::MouseEvent& e) override { if (moving) place (e.position.x); }
    void mouseUp (const juce::MouseEvent&) override { if (moving) { moving = false; nudge->endChangeGesture(); } }
    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        for (const auto& f : files) if (f.endsWithIgnoreCase (".wav") || f.endsWithIgnoreCase (".aif") || f.endsWithIgnoreCase (".aiff") || f.endsWithIgnoreCase (".flac") || f.endsWithIgnoreCase (".mp3")) return true;
        return false;
    }
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override
    {
        dragOver = false; repaint();
        for (const auto& f : files) if (isInterestedInFileDrag (juce::StringArray (f))) { if (onFile) onFile (juce::File (f)); break; }
    }
private:
    int totalUnits() const { return brk != nullptr ? juce::jmax (1, brk->slices) * 4 : 0; }   // the break's length in 64th notes
    void place (float x)
    {
        const int total = totalUnits(), unit = gridUnit64 (gridRaw != nullptr ? juce::roundToInt (gridRaw->load()) : 2); if (total <= 0) return;
        const int top = juce::jlimit (0, 31, total / unit - 1);
        const int v = juce::jlimit (0, top, juce::roundToInt (x / (float) juce::jmax (1, getWidth()) * (float) total / (float) unit));
        nudge->setValueNotifyingHost (nudge->convertTo0to1 ((float) v));
    }
    void repaintSlice (int p)
    {
        const int total = totalUnits(); if (p < 0 || total <= 0) return;
        const float w = (float) getWidth(), x0 = w * (float) (p % total) / (float) total, sw = w * 4.0f / (float) total;
        repaint (RF (x0 - 3.0f, 0.0f, sw + 6.0f, (float) getHeight()).getSmallestIntegerContainer());
        if (x0 + sw > w) repaint (RF (-3.0f, 0.0f, x0 + sw - w + 6.0f, (float) getHeight()).getSmallestIntegerContainer());
    }
    void render (float sc)
    {
        imgScale = sc; const float w = (float) getWidth(), h = (float) getHeight();
        img = juce::Image (juce::Image::ARGB, juce::jmax (1, juce::roundToInt (w * sc)), juce::jmax (1, juce::roundToInt (h * sc)), true);
        if (brk == nullptr || brk->data.empty()) return;
        juce::Graphics g (img); g.addTransform (juce::AffineTransform::scale (sc));
        const int ns = juce::jmax (1, brk->slices); const float base = std::floor (h * 0.66f), up = base - 9.0f, down = h - base - 2.0f;
        for (int k = 1; k < ns; ++k)   // slice lines: bars strongest, then beats, then every slice
        {
            const bool bar = k % 16 == 0, beat = k % 4 == 0; if ((ns > 64 && ! beat) || (ns > 256 && ! bar)) continue;
            g.setColour (white (bar ? 0.34f : beat ? 0.17f : 0.075f)); g.fillRect (RF (std::floor (w * (float) k / (float) ns), bar ? 0.0f : 3.0f, 1.0f, bar ? h : h - 6.0f));
        }
        const auto& d = brk->data; const size_t n = d.size(); const int nb = juce::jmax (1, (int) (w / 3.0f));
        for (int i = 0; i < nb; ++i)
        {
            const size_t i0 = n * (size_t) i / (size_t) nb, i1 = n * (size_t) (i + 1) / (size_t) nb; float m = 0.0f;
            for (size_t k = i0; k < i1; ++k) m = juce::jmax (m, std::abs (d[k]));
            const float v = std::pow (juce::jlimit (0.0f, 1.0f, m / 0.89f), 0.8f), x = (float) i * 3.0f + 0.5f, bh = juce::jmax (1.5f, v * up);
            const Col c = sliceCol (bay, (int) ((float) ns * ((float) i + 0.5f) / (float) nb), ns);
            g.setGradientFill (juce::ColourGradient (c.interpolatedWith (hi(), 0.45f), x, base - bh, c, x, base, false)); g.fillRect (RF (x, base - bh, 2.0f, bh));
            g.setGradientFill (juce::ColourGradient (c.withAlpha (0.42f), x, base + 1.0f, c.withAlpha (0.0f), x, base + 1.0f + down, false)); g.fillRect (RF (x, base + 1.5f, 2.0f, juce::jmin (down, bh * 0.6f)));
        }
        g.setColour (white (0.16f)); g.fillRect (RF (0.0f, base, w, 1.0f));
    }

    Processor& proc; int bay; mk::BreakPtr brk; juce::Image img; float imgScale = 0.0f;
    juce::RangedAudioParameter* nudge = nullptr; std::atomic<float>* nudgeRaw = nullptr, *gridRaw = nullptr;
    int pos = -1, nudgeV = 0, gridV = 2, msgTicks = 0; bool muted = false, dragOver = false, moving = false; juce::String msg;
};

// A bay's 32 steps. Click: PLAY -> TIE -> REST. Drag up / down: another slice. Shift-click: lock the step. Right-click: the step's menu.
class SeqStrip final : public juce::Component
{
public:
    SeqStrip (Processor& p, int bayIndex) : proc (p), bay (bayIndex) { setComponentID ("seq" + juce::String (bay)); }
    void setStep (int s) { if (s == step) return; const int old = step; step = s; repaintStep (old); repaintStep (s); }
    void setState (bool isMuted, bool isLocked) { if (isMuted != muted || isLocked != bayLocked) { muted = isMuted; bayLocked = isLocked; repaint(); } }

    void paint (juce::Graphics& g) override
    {
        const auto& P = proc.pattern(); const auto& seq = P.seq[(size_t) bay]; const uint32_t locks = P.stepLock[(size_t) bay];
        const int ns = slices(); const float h = (float) getHeight(), dim = muted ? 0.4f : 1.0f; const auto clip = g.getClipBounds().toFloat();
        const auto numFont = font (5.9f, Type::bold, 0.0f);
        for (int s = 0; s < kSteps; ++s)
        {
            const auto c = cellRect (s); if (! c.expanded (2.5f, 0.0f).intersects (clip)) continue;
            const Step& e = seq[(size_t) s]; const bool here = s == step, locked = ((locks >> s) & 1u) != 0; const auto r = c.reduced (0.0f, 2.0f);
            if (here) { g.setColour (white (0.11f)); g.fillRect (RF (c.getX() - 0.8f, 0.0f, c.getWidth() + 1.6f, h)); }
            if (e.kind == kPlay)
            {
                const Col col = sliceCol (bay, e.slice, ns); const float a = (0.6f + 0.4f * (float) e.vel / 127.0f) * dim;
                g.setGradientFill (juce::ColourGradient (col.interpolatedWith (hi(), 0.3f).withAlpha (a), r.getX(), r.getY(), col.darker (0.25f).withAlpha (a), r.getX(), r.getBottom(), false));
                g.fillRoundedRectangle (r, 3.0f);
                g.setColour (white (0.4f * dim)); g.fillRect (RF (r.getX() + 2.0f, r.getY() + 1.0f, r.getWidth() - 4.0f, 1.0f));
                g.setColour (cInk.withAlpha (0.93f));
                fitText (g, numFont, juce::String ((int) e.slice + 1), RF (r.getX() + 1.0f, r.getCentreY() - 7.0f, r.getWidth() - 2.0f, 14.0f));
                if (e.rev) { juce::Path t; const float cx = r.getCentreX(), ty = r.getY() + 6.0f; t.addTriangle (cx - 3.6f, ty, cx + 2.6f, ty - 3.1f, cx + 2.6f, ty + 3.1f); g.fillPath (t); }
                if (e.rat > 1)   // repeats inside the step: one tick each
                {
                    const int n = juce::jlimit (2, 4, (int) e.rat); const float tw = 2.2f, gap = 1.7f; float tx = r.getCentreX() - ((float) n * tw + (float) (n - 1) * gap) * 0.5f;
                    for (int k = 0; k < n; ++k) { g.fillRect (RF (tx, r.getBottom() - 8.0f, tw, 4.6f)); tx += tw + gap; }
                }
                if (here) { g.setColour (white (0.5f)); g.fillRoundedRectangle (r, 3.0f); }
            }
            else
            {
                g.setColour (black (0.26f)); g.fillRoundedRectangle (r, 3.0f); g.setColour (white (0.055f)); g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
                if (e.kind == kTie)   // the slice before keeps sounding: a bar in its colour
                {
                    const Col col = holding (s, ns); const bool more = seq[(size_t) ((s + 1) % kSteps)].kind == kTie;
                    const RF bar (c.getX() - 2.2f, r.getCentreY() - 4.5f, c.getWidth() + 2.2f + (more ? 1.0f : -3.0f), 9.0f);
                    g.setColour (col.isTransparent() ? white (0.09f) : col.withAlpha ((here ? 0.95f : 0.6f) * dim)); g.fillRoundedRectangle (bar, more ? 1.0f : 4.0f);
                }
                else { g.setColour (white (0.14f * dim)); g.fillEllipse (r.getCentreX() - 1.2f, r.getCentreY() - 1.2f, 2.4f, 2.4f); }
            }
            if (locked)
            {
                g.setColour (cAmber); g.drawRoundedRectangle (r.reduced (0.6f), 3.0f, 1.5f);
                g.setColour (cInk); g.fillEllipse (r.getRight() - 6.6f, r.getY() + 1.6f, 5.0f, 5.0f); g.setColour (cAmber); g.fillEllipse (r.getRight() - 5.7f, r.getY() + 2.5f, 3.2f, 3.2f);
            }
        }
        if (bayLocked) { g.setColour (cAmber.withAlpha (0.55f)); g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 4.0f, 1.0f); }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        cell = Steps::at (e.position.x, (float) getWidth()); dragging = false;
        if (e.mods.isPopupMenu()) { menu (cell); cell = -1; return; }
        if (e.mods.isShiftDown()) { const int s = cell; proc.edit ([this, s] (Pattern& p) { p.stepLock[(size_t) bay] ^= (1u << s); }); cell = -1; return; }
        startSlice = proc.pattern().seq[(size_t) bay][(size_t) cell].slice;
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (cell < 0) return;
        const int dy = e.getDistanceFromDragStartY(); if (! dragging && std::abs (dy) < 4) return;
        const int n = slices(), v = (((startSlice - juce::roundToInt ((float) dy / 5.0f)) % n) + n) % n; const auto& cur = proc.pattern().seq[(size_t) bay][(size_t) cell];
        if (dragging && cur.slice == v && cur.kind == kPlay) return;
        const int s = cell; proc.edit ([this, s, v] (Pattern& p) { auto& st = p.seq[(size_t) bay][(size_t) s]; st.slice = (int16_t) v; st.kind = kPlay; }, ! dragging);
        dragging = true;
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (cell >= 0 && ! dragging) menu (cell);   // a click: the step's list (play / hold / silent / slice / reverse / repeats ...)
        cell = -1; dragging = false;
    }
private:
    int slices() const { const auto& b = proc.pattern().brk[(size_t) bay]; return b != nullptr ? juce::jmax (1, b->slices) : 32; }
    RF cellRect (int s) const { const float w = (float) getWidth(); return { Steps::x (s, w), 0.0f, Steps::cell (w), (float) getHeight() }; }
    void repaintStep (int s) { if (s >= 0 && s < kSteps) repaint (cellRect (s).expanded (2.0f, 0.0f).getSmallestIntegerContainer()); }
    Col holding (int s, int ns) const   // the colour of the slice a tie holds (transparent: nothing is sounding there)
    {
        const auto& seq = proc.pattern().seq[(size_t) bay];
        for (int k = 1; k < kSteps; ++k) { const Step& e = seq[(size_t) ((s - k + kSteps) % kSteps)]; if (e.kind == kPlay) return sliceCol (bay, e.slice, ns); if (e.kind == kRest) break; }
        return juce::Colours::transparentBlack;
    }
    void menu (int s)
    {
        const auto& P = proc.pattern(); const Step e = P.seq[(size_t) bay][(size_t) s]; const bool locked = ((P.stepLock[(size_t) bay] >> s) & 1u) != 0;
        juce::PopupMenu m;
        m.addSectionHeader ("STEP " + juce::String (s + 1) + "  -  BAY " + juce::String::charToString ((juce::juce_wchar) ('A' + bay)));
        m.addItem (11, "Play", true, e.kind == kPlay); m.addItem (12, "Hold (the slice before keeps playing)", true, e.kind == kTie); m.addItem (13, "Silent", true, e.kind == kRest);
        {   // which piece of the break this step plays
            juce::PopupMenu sl; const int ns = slices();
            for (int i = 0; i < ns; ++i) sl.addItem (100 + i, "Slice " + juce::String (i + 1), true, e.kind == kPlay && e.slice == i);
            m.addSubMenu ("Slice (now " + juce::String (e.slice + 1) + ")", sl);
        }
        m.addSeparator(); m.addItem (2, "Reverse", true, e.rev); m.addItem (1, "Lock this step (RANDOM COMBO leaves it)", true, locked);
        m.addSeparator(); for (int r = 1; r <= 4; ++r) m.addItem (20 + r, r == 1 ? juce::String ("Repeats: x1 (none)") : "Repeats: x" + juce::String (r), true, juce::jlimit (1, 4, (int) e.rat) == r);
        m.addSeparator(); m.addItem (31, "Reset this bay to the plain break"); m.addItem (32, "Unlock all steps", P.stepLock[(size_t) bay] != 0);
        showMenu (m, *this, [safe = juce::Component::SafePointer<SeqStrip> (this), s] (int r)
        {
            if (safe == nullptr || r <= 0) return;
            const int b = safe->bay;
            safe->proc.edit ([b, s, r] (Pattern& p)
            {
                auto& st = p.seq[(size_t) b][(size_t) s];
                if (r == 1) p.stepLock[(size_t) b] ^= (1u << s);
                else if (r == 2) { st.rev = ! st.rev; if (st.kind != kPlay) st.kind = kPlay; }
                else if (r >= 11 && r <= 13) st.kind = (uint8_t) (r - 11);
                else if (r >= 21 && r <= 24) { st.rat = (uint8_t) (r - 20); if (st.kind != kPlay) st.kind = kPlay; }
                else if (r == 31) { p.straight (b); p.stepLock[(size_t) b] = 0; }
                else if (r == 32) p.stepLock[(size_t) b] = 0;
                else if (r >= 100) { st.slice = (int16_t) (r - 100); st.kind = kPlay; }
            });
        }, cellRect (s).getSmallestIntegerContainer());
    }
    Processor& proc; int bay, step = -1, cell = -1, startSlice = 0; bool dragging = false, muted = false, bayLocked = false;
};

// ====================================================================================== the drum machine
// 6 lanes x 32 steps. Click: a hit (or clears one). Shift-click: an accent. Right-click: a soft hit. Drag: paints along the lane.
class DrumGrid final : public juce::Component
{
public:
    explicit DrumGrid (Processor& p) : proc (p) { setComponentID ("grid"); }
    void setStep (int s) { if (s == step) return; const int old = step; step = s; repaintStep (old); repaintStep (s); }
    void setMuted (int l, bool m) { if (muted[l] != m) { muted[l] = m; repaint(); } }
    void paint (juce::Graphics& g) override
    {
        const auto& P = proc.pattern(); const auto clip = g.getClipBounds().toFloat(); const float h = (float) getHeight();
        if (step >= 0) { const auto c = cellRect (0, step); g.setColour (white (0.11f)); g.fillRect (RF (c.getX() - 0.8f, 0.0f, c.getWidth() + 1.6f, h)); }
        for (int l = 0; l < kDrums; ++l)
        {
            const float dim = muted[l] ? 0.36f : 1.0f; const Col tint = cIce.interpolatedWith (duckSource (l) == 0 ? cPink : duckSource (l) == 1 ? cCyan : cViolet, 0.22f);
            for (int s = 0; s < kSteps; ++s)
            {
                const auto c = cellRect (l, s); if (! c.intersects (clip)) continue;
                const int v = P.drum[(size_t) l][(size_t) s];
                g.setColour (s % 4 == 0 ? white (0.085f) : black (0.25f)); g.fillRoundedRectangle (c, 2.5f);
                if (v <= 0) continue;
                const float t = (float) v / 127.0f; const auto r = c.withSizeKeepingCentre (c.getWidth(), c.getHeight() * (0.4f + 0.6f * t));
                g.setColour (tint.withAlpha ((0.25f + 0.75f * t * t) * dim)); g.fillRoundedRectangle (r, 2.5f);
                if (v >= 120) { g.setColour (white (0.95f * dim)); g.fillRoundedRectangle (r.reduced (3.0f, 3.0f), 1.5f); }   // an accent: a hot core
                if (s == step) { g.setColour (white (0.6f * dim)); g.fillRoundedRectangle (r, 2.5f); }
            }
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        lane = juce::jlimit (0, kDrums - 1, (int) (e.position.y / (float) kLaneH)); const int s = Steps::at (e.position.x, (float) getWidth());
        const int cur = proc.pattern().drum[(size_t) lane][(size_t) s];
        if (e.mods.isPopupMenu()) value = (cur > 0 && cur <= 70) ? 0 : 60;      // right-click: a soft hit (or clears a soft hit)
        else if (e.mods.isShiftDown()) value = cur >= 120 ? 0 : 127;           // shift: an accent
        else value = cur > 0 ? 0 : 110;
        painting = true; first = true; last = -1; put (s);
        if (value > 0 && proc.engine.uiStep.load() < 0) proc.engine.pushPad (lane, (float) value / 127.0f);   // stopped: hear what was placed
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (painting) put (Steps::at (e.position.x, (float) getWidth())); }
    void mouseUp (const juce::MouseEvent&) override { painting = false; }
private:
    RF cellRect (int l, int s) const { const float w = (float) getWidth(); return { Steps::x (s, w), (float) (l * kLaneH) + 1.5f, Steps::cell (w), (float) kLaneH - 3.0f }; }
    void repaintStep (int s) { if (s >= 0 && s < kSteps) { const auto c = cellRect (0, s); repaint (RF (c.getX() - 2.0f, 0.0f, c.getWidth() + 4.0f, (float) getHeight()).getSmallestIntegerContainer()); } }
    void put (int s)
    {
        if (s == last) return;
        last = s;
        if (proc.pattern().drum[(size_t) lane][(size_t) s] == value) return;
        const int l = lane; const auto v = (uint8_t) value;
        proc.edit ([l, s, v] (Pattern& p) { p.drum[(size_t) l][(size_t) s] = v; }, first); first = false;
    }
    Processor& proc; int step = -1, lane = 0, value = 0, last = -1; bool painting = false, first = true, muted[kDrums] {};
};

// ====================================================================================== painted lettering, the value box
// a line of neon lettering for the parts that are painted once and kept: the letters with a real halo around them
void neonSign (juce::Graphics& g, const juce::Font& f, const juce::String& s, RF r, Col c, Just j = Just::centredLeft, float glow = 0.85f)
{
    juce::GlyphArrangement ga; ga.addLineOfText (f, s, 0.0f, 0.0f); juce::Path p; ga.createPath (p);
    const auto pb = p.getBounds(); if (pb.isEmpty()) return;
    const float x = j.testFlags (Just::left) ? r.getX() : j.testFlags (Just::right) ? r.getRight() - pb.getWidth() : r.getCentreX() - pb.getWidth() * 0.5f;
    p.applyTransform (juce::AffineTransform::translation (x - pb.getX(), r.getCentreY() + f.getHeight() * Type::get().cap * 0.5f));
    if (gWater) { g.setColour (c.withAlpha (0.22f)); g.fillPath (p, juce::AffineTransform::translation (0.8f, 0.9f)); g.setColour (c); g.fillPath (p); return; }   // ink, with a soft bleed
    juce::DropShadow (c.withAlpha (glow), 5, {}).drawForPath (g, p);
    g.setColour (c.interpolatedWith (hi(), 0.6f)); g.fillPath (p);
}

// right-click a knob: type its value here. Enter applies it, Escape (or a click elsewhere) leaves it as it was.
class ValueBox final : public juce::TextEditor
{
public:
    ValueBox()
    {
        setJustification (Just::centred); setSelectAllWhenFocused (true); setPopupMenuEnabled (false); setComponentID ("valuebox");
        onReturnKey = [this] { if (auto* k = target.getComponent()) k->typeIn (getText()); shut(); };
        onEscapeKey = [this] { shut(); }; onFocusLost = [this] { shut(); };
    }
    void open (Knob& k, juce::Component& parent)
    {
        target = &k; applyFontToAllText (font (7.4f, Type::bold, 0.0f));
        parent.addAndMakeVisible (*this);
        const float s = juce::jmax (0.1f, (float) parent.getWidth() / (float) kW);   // the window may be scaled: sit in its 1180 x 760 layout like everything else
        const auto c = parent.getLocalArea (&k, k.getLocalBounds()).getCentre().toFloat() / s;
        setTransform (juce::AffineTransform::scale (s));
        setBounds (RI (96, 24).withCentre (c.roundToInt()).constrainedWithin (RI (0, 0, kW, kH).reduced (4)));
        setText (k.fullText(), false); toFront (true); grabKeyboardFocus(); selectAll();
    }
    void shut() { if (! isVisible()) return; target = nullptr; setVisible (false); }
private:
    juce::Component::SafePointer<Knob> target;
};

// ====================================================================================== the FX panel
// An overlay over the dimmed window. One page per hosted Phantom FX unit, built from the unit's own list of controls (a knob for a
// number, a list for a choice, a switch for an on / off), with the unit's ENGINE list on top; plus one page for the split sidechain
// (DUCK). Tabs jump between the pages.
constexpr int kDuckPage = kNumSlots;
inline Col pageCol (int s) { return isBaySlot (s) ? bayCol (slotBay (s)) : s <= sStutGate ? cPink : s == sLimiter ? cIce : s == kDuckPage ? cBlue : cViolet; }

// WRECKER's DRAW engine: the curve the sound is bent through. Input runs left to right, output bottom to top; the dotted line is the
// clean sound. Drag a point, double-click for a new one, right-click a point to remove it (the two ends stay at the edges).
class CurveBox final : public juce::Component
{
public:
    CurveBox (Processor& p, int slotIndex, Col accentColour)
        : proc (p), slot (slotIndex), accent (accentColour), resetBtn ("RESET", accentColour), mirrorBtn ("MIRROR", accentColour)
    {
        setComponentID ("curve"); addAndMakeVisible (resetBtn); addAndMakeVisible (mirrorBtn);
        resetBtn.onClick = [this] { pts = CE::factoryPoints(); commit(); };
        mirrorBtn.onClick = [this]   // the left half becomes the upside-down copy of the right
        {
            std::vector<juce::Point<float>> right, out; for (const auto& q : pts) if (q.x > 0.0f) right.push_back (q);
            for (auto it = right.rbegin(); it != right.rend(); ++it) out.push_back ({ -it->x, -it->y });
            out.push_back ({ 0.0f, 0.0f }); for (const auto& q : right) out.push_back (q);
            if (out.size() >= 2) { out.front().x = -1.0f; out.back().x = 1.0f; pts = out; commit(); }
        };
        reload();
    }
    void reload() { if (drag < 0) { pts = CE::varToPoints (proc.slotData (slot)); repaint(); } }   // the project's curve (also after a change from outside)
    void resized() override
    {
        const auto b = getLocalBounds().toFloat();
        plot = RF (b.getX() + 150.0f, b.getY() + 14.0f, 300.0f, b.getHeight() - 36.0f);
        const int tx = juce::roundToInt (plot.getRight()) + 44; resetBtn.setBounds (tx, getHeight() - 44, 84, 26); mirrorBtn.setBounds (tx + 92, getHeight() - 44, 84, 26);
    }
    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        g.setColour (black (0.22f)); g.fillRoundedRectangle (b, 8.0f); g.setColour (white (0.09f)); g.drawRoundedRectangle (b.reduced (0.5f), 8.0f, 1.0f);
        g.setColour (black (0.4f)); g.fillRect (plot);
        for (int k = 1; k < 8; ++k)
        {
            g.setColour (white (k == 4 ? 0.2f : 0.07f));
            g.fillRect (RF (plot.getX() + plot.getWidth() * (float) k / 8.0f, plot.getY(), 1.0f, plot.getHeight())); g.fillRect (RF (plot.getX(), plot.getY() + plot.getHeight() * (float) k / 8.0f, plot.getWidth(), 1.0f));
        }
        g.setColour (white (0.3f)); g.drawRect (plot, 1.0f);
        {
            juce::Graphics::ScopedSaveState ss (g); g.reduceClipRegion (plot.getSmallestIntegerContainer());
            juce::Path clean, dotted; clean.startNewSubPath (toScreen ({ -1.0f, -1.0f })); clean.lineTo (toScreen ({ 1.0f, 1.0f }));
            const float dashes[] { 4.0f, 4.0f }; Stroke (1.2f).createDashedStroke (dotted, clean, dashes, 2); g.setColour (white (0.42f)); g.fillPath (dotted);
            juce::Path c;
            for (int i = 0; i <= 200; ++i)
            {
                const float x = -1.0f + 2.0f * (float) i / 200.0f; const auto sp = toScreen ({ x, juce::jlimit (-1.5f, 1.5f, CE::evalCurve (pts, x)) });
                if (i == 0) c.startNewSubPath (sp); else c.lineTo (sp);
            }
            neonStroke (g, c, accent, 2.2f, 1.4f);
        }
        for (size_t i = 0; i < pts.size(); ++i)
        {
            const auto h = handle (i); const bool hot = (int) i == drag;
            g.setColour (cInk); g.fillEllipse (h.x - 6.5f, h.y - 6.5f, 13.0f, 13.0f);
            g.setColour (hot ? hi() : accent.interpolatedWith (hi(), 0.25f)); g.fillEllipse (h.x - 4.6f, h.y - 4.6f, 9.2f, 9.2f);
        }
        g.setColour (cDim); const auto sf = font (4.6f, Type::bold, 0.1f);
        drawText (g, sf, "IN  >", RF (plot.getRight() - 60.0f, plot.getBottom() + 3.0f, 60.0f, 11.0f), Just::centredRight);
        drawText (g, sf, "OUT", RF (plot.getX() - 40.0f, plot.getY(), 34.0f, 11.0f), Just::centredRight);
        drawText (g, sf, "QUIET", RF (plot.getX(), plot.getBottom() + 3.0f, 60.0f, 11.0f), Just::centredLeft);
        const float tx = plot.getRight() + 44.0f; float y = plot.getY() + 2.0f;
        g.setColour (accent.interpolatedWith (hi(), 0.6f)); drawText (g, font (8.0f, Type::italic, 0.05f), "DRAW YOUR OWN CURVE", RF (tx, y, 300.0f, 18.0f), Just::centredLeft); y += 27.0f;
        static const char* const help[] { "DRAG A POINT: BEND THE CURVE", "DOUBLE-CLICK: A NEW POINT", "RIGHT-CLICK A POINT: REMOVE IT", "THE DOTTED LINE IS THE CLEAN SOUND", "MIRROR: THE LEFT HALF COPIES THE RIGHT, UPSIDE DOWN" };
        g.setColour (cDim); for (auto* line : help) { fitText (g, font (5.0f, Type::bold, 0.08f), line, RF (tx, y, b.getRight() - tx - 12.0f, 13.0f), Just::centredLeft); y += 17.0f; }
        if (drag >= 0 && drag < (int) pts.size())
        {
            g.setColour (hi());
            drawText (g, font (5.4f, Type::bold, 0.06f), "IN " + juce::String (pts[(size_t) drag].x, 2) + "     OUT " + juce::String (pts[(size_t) drag].y, 2), RF (tx, y + 4.0f, 260.0f, 13.0f), Just::centredLeft);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        drag = hit (e.position);
        if (e.mods.isPopupMenu()) { if (drag > 0 && drag < (int) pts.size() - 1) { pts.erase (pts.begin() + drag); drag = -1; commit(); } drag = -1; return; }
        repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drag < 0 || drag >= (int) pts.size()) return;
        auto q = fromScreen (e.position); const auto i = (size_t) drag;
        if (i == 0) q.x = -1.0f; else if (i == pts.size() - 1) q.x = 1.0f;
        else q.x = juce::jlimit (pts[i - 1].x + 0.01f, juce::jmax (pts[i - 1].x + 0.01f, pts[i + 1].x - 0.01f), q.x);   // the points keep their order
        q.y = juce::jlimit (-1.5f, 1.5f, q.y); pts[i] = q; repaint();
        const auto now = juce::Time::getMillisecondCounter(); if (now - sent > 40) { sent = now; send(); }       // heard while it is drawn
    }
    void mouseUp (const juce::MouseEvent&) override { if (drag >= 0) { drag = -1; commit(); } }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() || hit (e.position) >= 0 || ! plot.contains (e.position)) return;
        auto q = fromScreen (e.position); q.x = juce::jlimit (-0.99f, 0.99f, q.x);
        pts.push_back (q); std::sort (pts.begin(), pts.end(), [] (const juce::Point<float>& a, const juce::Point<float>& b) { return a.x < b.x; }); commit();
    }
private:
    using CE = phantom::engines::CurveEngine;
    juce::Point<float> toScreen (juce::Point<float> v) const { return { plot.getX() + (v.x + 1.0f) * 0.5f * plot.getWidth(), plot.getBottom() - (v.y + 1.0f) * 0.5f * plot.getHeight() }; }
    juce::Point<float> fromScreen (juce::Point<float> sp) const { return { (sp.x - plot.getX()) / plot.getWidth() * 2.0f - 1.0f, (plot.getBottom() - sp.y) / plot.getHeight() * 2.0f - 1.0f }; }
    juce::Point<float> handle (size_t i) const { auto h = toScreen (pts[i]); h.y = juce::jlimit (plot.getY(), plot.getBottom(), h.y); return h; }   // a point above the box waits at its edge
    int hit (juce::Point<float> sp) const { for (size_t i = 0; i < pts.size(); ++i) if (handle (i).getDistanceFrom (sp) < 10.0f) return (int) i; return -1; }
    void send() { proc.setSlotData (slot, CE::pointsToVar (pts)); }
    void commit() { send(); pts = CE::varToPoints (proc.slotData (slot)); repaint(); }

    Processor& proc; int slot; Col accent; std::vector<juce::Point<float>> pts; int drag = -1; juce::uint32 sent = 0; RF plot;
    Btn resetBtn, mirrorBtn;
};

class FxPanel final : public juce::Component
{
public:
    FxPanel (Processor& p, const Art& a) : proc (p), art (a), closeBtn ("CLOSE", cIce)
    {
        setWantsKeyboardFocus (true); setComponentID ("fx");
        static const char* const names[] { "STUTTER", "ST. FILTER", "ST. GATE", "CHORUS", "REVERB", "LIMITER", "DUCK" };
        for (int s = 0; s <= kDuckPage; ++s)
        {
            auto* t = tabs.add (new Btn (isCrush (s) ? "CRUSH" : s < kBays * 2 ? ((s & 1) ? "FILTERS" : "CLIP") : s == kDuckPage ? "DUCK" : names[s - kBays * 2], pageCol (s))); t->cap = 5.2f;
            t->onClick = [this, s] { open (s, scope); }; addAndMakeVisible (t);
        }
        closeBtn.onClick = [this] { close(); }; addAndMakeVisible (closeBtn);
        modeRaw = proc.apvts.getRawParameterValue ("duck_mode");
    }
    std::function<void (Knob&)> onType;
    bool isOpen() const { return page >= 0; }
    // a bay's FX button shows only that bay's pages (its CRUNCH and FILTERS); the other FX buttons show the shared pages only
    // a bay's FX button: that bay's CRUNCH and FILTERS, plus the REVERB and LIMITER (shared by every bay); the other FX buttons: the shared pages
    void open (int s, int keepScope = -2)
    {
        page = juce::jlimit (0, kDuckPage, s); scope = keepScope != -2 ? keepScope : isBaySlot (page) ? slotBay (page) : -1;
        build(); tick(); setVisible (true); toFront (false); if (isShowing()) grabKeyboardFocus(); repaint();
        if (onOpened) onOpened();
    }
    std::function<void()> onOpened;   // the window scales the panel up to fill it
    RI panelBounds() const { return panel; }
    bool inScope (int s) const { return scope >= 0 ? ((isBaySlot (s) && slotBay (s) == scope) || s == sReverb || s == sLimiter) : ! isBaySlot (s); }
    void close() { if (page < 0) return; clear(); page = -1; setVisible (false); }
    void dataChanged() { if (curve != nullptr) curve->reload(); }
    void tick()   // 30 Hz while open
    {
        if (page < 0) return;
        if (page != kDuckPage)
        {
            if (wantsCurve() != (curve != nullptr)) { build(); repaint(); }   // the DRAW engine came or went: its curve box with it
            // a control that does nothing at the unit's current settings is greyed out and says why (phantom-fx effects/ControlUse.h)
            const juce::String pre = slotPrefix (page); const int u = slotUnit (page); bool changed = false;
            const phantom::ControlValue val = [this, &pre] (const std::string& id) { auto* a = proc.apvts.getRawParameterValue (pre + juce::String (id)); return a != nullptr ? a->load() : 0.0f; };
            for (auto& c : cells)
            {
                if (c.spec.empty() || c.comp == nullptr || (page < kBays * 2 && (page & 1) == 0) || isCrush (page)) continue;   // CLIP's and CRUSH's knobs always act
                const auto why = phantom::controlIdleReason (u, c.spec, val); const bool idle = ! why.empty();
                if (idle != c.idle) { c.idle = idle; changed = true; c.comp->setAlpha (idle ? 0.3f : 1.0f); }
                if (auto* t = dynamic_cast<juce::SettableTooltipClient*> (c.comp.get())) { const juce::String tip = idle ? juce::String ("GREYED OUT: ") + why : juce::String(); if (t->getTooltip() != tip) t->setTooltip (tip); }
            }
            if (changed) { panelImg = {}; repaint(); }
            return;
        }
        if (smartKnob != nullptr && modeRaw != nullptr) smartKnob->setDim (modeRaw->load() < 0.5f);       // SMART LEVEL only means something in SMART
        for (int k = 0; k < kBands; ++k) for (int b = 0; b < kBays; ++b)
        {
            const int q = juce::jlimit (0, 24, juce::roundToInt (proc.engine.uiDuck[b][k].load() * 1.5f));
            if (q != gr[k][b]) { gr[k][b] = q; repaint (meterRect (k).expanded (2)); }
        }
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (gWater ? Col (0xd8f4ece0) : Col (0xc0030210));
        if (page < 0) return;
        const float sc = scaleOf (g); if (! panelImg.isValid() || std::abs (sc - imgScale) > 0.01f) render (sc);
        g.drawImage (panelImg, panel.expanded (kMargin).toFloat());
        if (page == kDuckPage)
            for (int k = 0; k < kBands; ++k)   // how far each bay's band is pushed down right now: one bar per bay, growing downwards
            {
                const auto m = meterRect (k).toFloat();
                for (int b = 0; b < kBays; ++b)
                {
                    const RF bar (m.getX() + (float) b * 10.0f, m.getY(), 7.0f, m.getHeight()); const float t = (float) gr[k][b] / 24.0f;
                    g.setColour (black (0.45f)); g.fillRoundedRectangle (bar, 2.0f);
                    if (t > 0.0f) { g.setColour (bayCol (b)); g.fillRoundedRectangle (bar.withHeight (juce::jmax (2.0f, bar.getHeight() * t)), 2.0f); }
                }
            }
    }
    bool keyPressed (const juce::KeyPress& k) override { if (k == juce::KeyPress::escapeKey) { close(); return true; } return false; }
    void mouseDown (const juce::MouseEvent& e) override { if (! panel.contains (e.getPosition())) close(); }
    void resized() override { if (page >= 0) build(); }

private:
    static constexpr int kMargin = 28, kCols = 6, kCellW = 112, kCellH = 92, kHead = 98, kEngineH = 46, kCurveH = 232, kPanelW = 740;   // 6 across: the window then scales the panel up more
    struct Cell { juce::String name; int kind = 0; std::unique_ptr<juce::Component> comp; RI r; std::string spec; bool idle = false; };   // kind: 0 knob, 1 list, 2 switch, 3 a switch with its name on it; spec: the unit's own control id (greyed out when it does nothing)

    void clear() { cells.clear(); engine.reset(); curve.reset(); smartKnob = nullptr; panelImg = {}; }
    bool wantsCurve() const { return page < kBays * 2 && (page & 1) == 0 && engine != nullptr && engine->current() == "DRAW"; }
    Cell* add (const juce::String& id, const juce::String& name, int kind)
    {
        if (proc.apvts.getParameter (id) == nullptr) { jassertfalse; return nullptr; }   // a control this build does not have: left out
        const Col ac = pageCol (page); Cell c; c.name = name.toUpperCase(); c.kind = kind;
        if (kind == 0) { auto k = std::make_unique<Knob> (proc, id, c.name, ac, Knob::value); k->onType = onType; c.comp = std::move (k); }
        else if (kind == 1) { auto ch = std::make_unique<Choice> (proc, id, ac); ch->cap = 5.8f; c.comp = std::move (ch); }
        else { auto b = std::make_unique<Btn> (kind == 3 ? c.name : juce::String ("ON"), ac); if (kind == 2) b->offLabel = "OFF"; b->cap = kind == 3 ? 5.0f : 5.8f; b->attach (proc, id); c.comp = std::move (b); }
        addAndMakeVisible (*c.comp); cells.push_back (std::move (c)); return &cells.back();
    }
    static void place (Cell* c, RI r)   // a cell: its name on top (painted with the panel), its control under it
    {
        if (c == nullptr) return;
        c->r = r; const int kh = juce::jmin (68, r.getHeight() - 18);
        if (c->kind == 0) c->comp->setBounds (r.getCentreX() - 31, r.getY() + 18, 62, kh);
        else if (c->kind == 1) c->comp->setBounds (r.getX() + 7, r.getY() + 36, r.getWidth() - 14, 24);
        else if (c->kind == 2) c->comp->setBounds (r.getCentreX() - 28, r.getY() + 36, 56, 24);
        else c->comp->setBounds (r.reduced (2, 0));
    }
    void build()
    {
        clear(); cells.reserve (48);
        for (int i = 0; i < tabs.size(); ++i) tabs[i]->setToggleState (i == page, juce::dontSendNotification);
        const juce::Point<int> mid (getWidth() / 2, getHeight() / 2);
        if (page == kDuckPage)
        {
            panel = RI (8 * kCellW + 44, kHead + 96 + kBands * 84 + 28).withCentre (mid);
            const int x0 = panel.getX() + 22, y0 = panel.getY() + kHead;
            struct G { const char* id; const char* name; int kind; };
            static const G top[] { { "duck_mode", "Mode", 1 }, { "duck_xlow", "Low / Mid Split", 0 }, { "duck_xhigh", "Mid / High Split", 0 }, { "duck_slope", "Split Slope", 1 },
                                   { "duck_look", "Look Ahead", 1 }, { "duck_mix", "Mix", 0 }, { "duck_thr", "Smart Level", 0 } };
            int i = 0;
            for (const auto& t : top)
            {
                auto* c = add (t.id, t.name, t.kind); place (c, RI (x0 + 56 + i * kCellW, y0, kCellW, kCellH)); ++i;
                if (c != nullptr && juce::String (t.id) == "duck_thr") smartKnob = dynamic_cast<Knob*> (c->comp.get());
            }
            static const char* const ids[] { "atk", "hold", "rel", "shape" }; static const char* const nms[] { "Attack", "Hold", "Release", "Shape" };
            static const char* const drum[] { "KICK", "SNARE", "C HAT", "O HAT", "PERC 1", "PERC 2" };
            for (int k = 0; k < kBands; ++k)
            {
                const juce::String pre = bandPrefix (k); const int y = bandY (k);
                place (add (pre + "on", "On / Off", 2), RI (x0 + 64, y - 2, 68, 80));
                for (int q = 0; q < 4; ++q) place (add (pre + ids[q], nms[q], 0), RI (x0 + 136 + q * 84, y - 2, 84, 82));
                for (int d = 0; d < kDrums; ++d) place (add (pre + "from_" + juce::String (drumPrefix (d)).dropLastCharacters (1), drum[d], 3), RI (x0 + 484 + d * 58, y + 32, 58, 24));
            }
        }
        else
        {
            using T = phantom::ParamSpec::Type; const juce::String pre = slotPrefix (page);
            if (slotHasOn (page)) add (pre + "on", "On / Off", 2);
            for (const auto& sp : proc.engine.slot (page).params())
            {
                if (slotParamHidden (page, sp.id)) continue;
                if (sp.id == "engine" && sp.type == T::Choice && proc.apvts.getParameter (pre + "engine") != nullptr)
                {   // the unit's ENGINE list sits on top of the page, one sub-menu per engine
                    engine = std::make_unique<Choice> (proc, pre + "engine", pageCol (page)); engine->grouped = true; engine->big = true; engine->cap = 6.4f; addAndMakeVisible (*engine);
                    continue;
                }
                if (auto* c = add (pre + juce::String (sp.id), juce::String (sp.name), sp.type == T::Choice ? 1 : sp.type == T::Bool ? 2 : 0)) c->spec = sp.id;
            }
            if (slotHasMix (page)) add (pre + "mix", "Mix", 0);
            const bool draw = wantsCurve(); const int n = (int) cells.size(), rows = juce::jmax (1, (n + kCols - 1) / kCols), eh = engine != nullptr ? kEngineH : 0;
            panel = RI (kPanelW, kHead + eh + rows * kCellH + (draw ? kCurveH + 10 : 0) + 22).withCentre (mid);
            const int x0 = panel.getCentreX() - juce::jmin (n, kCols) * kCellW / 2, y0 = panel.getY() + kHead + eh;
            if (engine != nullptr) engine->setBounds (panel.getX() + 104, panel.getY() + kHead + 2, 430, 30);
            for (int i = 0; i < n; ++i) place (&cells[(size_t) i], RI (x0 + (i % kCols) * kCellW, y0 + (i / kCols) * kCellH, kCellW, kCellH));
            if (draw)
            {
                curve = std::make_unique<CurveBox> (proc, page, pageCol (page)); addAndMakeVisible (*curve);
                curve->setBounds (panel.getX() + 22, y0 + rows * kCellH + 6, panel.getWidth() - 44, kCurveH);
            }
        }
        closeBtn.setBounds (panel.getRight() - 96, panel.getY() + 15, 78, 24);
        // the tabs: a bay's own two ([CRUNCH][FILTERS] after its letter), or the shared ones (STUTTER  ST. FILTER  ST. GATE   CHORUS  REVERB  LIMITER   DUCK)
        int x = panel.getX() + 22; const int ty = panel.getY() + 56;
        static const int w[] { 72, 72, 72, 82, 76, 70, 70, 72 };
        if (scope >= 0) x += 22;                                                            // room for the bay's letter
        for (int s = 0; s <= kDuckPage; ++s) tabs[s]->setVisible (inScope (s));
        std::vector<int> order;   // a bay: CRUSH, CLIP, FILTERS, then the shared REVERB and LIMITER
        if (scope >= 0) order = { crushSlot (scope), crunchSlot (scope), filterSlot (scope), sReverb, sLimiter };
        else for (int s = kBays * 2; s <= sLimiter; ++s) order.push_back (s);
        if (scope < 0) order.push_back (kDuckPage);
        for (int s : order)
        {
            if (s == sChorus || s == kDuckPage) x += 8;
            if (scope >= 0 && s == sReverb) x += 18;   // the shared ones, a little apart from the bay's own
            const int tw = isCrush (s) ? 72 : s < kBays * 2 ? w[s & 1] : s == kDuckPage ? 60 : w[2 + s - kBays * 2];
            tabs[s]->setBounds (x, ty, tw, 23); x += tw + 3;
        }
    }
    int bandY (int k) const { return panel.getY() + kHead + 100 + k * 84; }
    RI meterRect (int k) const { return { panel.getRight() - 22 - 40, bandY (k) + 14, 38, 50 }; }

    void render (float sc)   // the glass, the lettering and the names of the controls: painted once per page, then kept
    {
        imgScale = sc; const auto area = panel.expanded (kMargin); const Col ac = pageCol (page);
        panelImg = juce::Image (juce::Image::ARGB, juce::jmax (1, juce::roundToInt ((float) area.getWidth() * sc)), juce::jmax (1, juce::roundToInt ((float) area.getHeight() * sc)), true);
        juce::Graphics g (panelImg); g.addTransform (juce::AffineTransform::scale (sc).translated ((float) -area.getX() * sc, (float) -area.getY() * sc));
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        const auto pr = panel.toFloat(); juce::Path pp; pp.addRoundedRectangle (pr, 12.0f);
        juce::DropShadow (ac.withAlpha (0.5f), 20, {}).drawForPath (g, pp);
        {
            juce::Graphics::ScopedSaveState ss (g); g.reduceClipRegion (pp);
            if (art.frost.isValid()) g.drawImage (art.frost, getLocalBounds().toFloat()); else g.fillAll (cInk);
            g.setColour (cGlass.withAlpha (0.84f)); g.fillRect (pr);
            g.setGradientFill (juce::ColourGradient (ac.withAlpha (0.16f), pr.getX(), pr.getY(), ac.withAlpha (0.0f), pr.getX(), pr.getY() + 110.0f, false)); g.fillRect (pr);
        }
        neonStroke (g, pp, ac, 1.4f);
        // the ghost and the page's name
        float tx = pr.getX() + 22.0f;
        if (art.ghost.isValid()) { g.drawImage (art.ghost, RF (pr.getX() + 14.0f, pr.getY() + 6.0f, 52.0f, 44.0f), juce::RectanglePlacement::centred); tx += 50.0f; }
        const juce::String title = page == kDuckPage ? juce::String ("DUCK") : slotTitle (page);
        const auto tf = font (10.5f, Type::italic, 0.05f); const float after = tx + textWidth (tf, title) + 16.0f;
        neonSign (g, tf, title, RF (tx, pr.getY() + 14.0f, 500.0f, 26.0f), ac);
        const juce::String blurb = page == kDuckPage ? "THE DRUMS PUSH THE BREAKS DOWN: EACH DRUM ONLY ITS OWN PART OF THE SOUND" : isCrush (page) ? juce::String ("TWO PHANTOM FX DIRTS AT ONCE: WRECKER'S TUBE AND CRACKLE'S DOOM COMP PARALLEL CURVE. BLEND: TUBE TO DOOM")
                                 : page < kBays * 2 && (page & 1) == 0 ? juce::String ("FL STUDIO'S FRUITY SOFT CLIPPER: ROUNDS OFF THE LOUD HITS ABOVE THE THRESHOLD. LOWER THRES = MORE CLIP") : juce::String (phantom::unitDef (slotUnit (page)).blurb).toUpperCase();
        g.setColour (cDim); wrapText (g, font (4.8f, Type::bold, 0.1f), blurb, RF (after, pr.getY() + 10.0f, pr.getRight() - 116.0f - after, 36.0f), 2);
        g.setColour (white (0.1f)); g.fillRect (RF (pr.getX() + 18.0f, pr.getY() + 87.0f, pr.getWidth() - 36.0f, 1.0f));
        if (scope >= 0)   // the bay's letter in front of its two tabs
            neonSign (g, font (8.0f, Type::italic, 0.0f), juce::String::charToString ((juce::juce_wchar) ('A' + scope)), RF ((float) tabs[crushSlot (scope)]->getX() - 20.0f, (float) tabs[crushSlot (scope)]->getY(), 14.0f, 23.0f), bayCol (scope), Just::centred, 0.7f);
        if (engine != nullptr)
        {
            const auto er = engine->getBounds().toFloat();
            neonSign (g, font (7.4f, Type::italic, 0.06f), "ENGINE", RF (pr.getX() + 24.0f, er.getY(), 76.0f, er.getHeight()), ac, Just::centredLeft, 0.7f);
            g.setColour (cDim); wrapText (g, font (4.8f, Type::bold, 0.1f), "WHAT MAKES THIS EFFECT'S SOUND.  PHANTOM IS ITS OWN.", RF (er.getRight() + 14.0f, er.getY() - 4.0f, pr.getRight() - er.getRight() - 34.0f, er.getHeight() + 8.0f), 2);
        }
        // the names of the controls
        const auto nf = font (5.2f, Type::bold, 0.08f);
        for (const auto& c : cells)
        {
            if (c.kind == 3) continue;
            g.setColour (c.idle ? cFaint.withAlpha (0.55f) : cDim); fitText (g, nf, c.idle ? c.name + "  (NOT USED)" : c.name, RF ((float) c.r.getX() + 3.0f, (float) c.r.getY() + 3.0f, (float) c.r.getWidth() - 6.0f, 13.0f));
        }
        if (page == kDuckPage)
        {
            const float x0 = pr.getX() + 22.0f;
            for (int k = 0; k < kBands; ++k)
            {
                const float y = (float) bandY (k); const RF row (x0 - 6.0f, y - 6.0f, pr.getWidth() - 32.0f, 80.0f);
                g.setColour (black (0.2f)); g.fillRoundedRectangle (row, 7.0f); g.setColour (white (0.09f)); g.drawRoundedRectangle (row, 7.0f, 1.0f);
                neonSign (g, font (9.0f, Type::italic, 0.04f), bandName (k), RF (x0 + 4.0f, y + 14.0f, 56.0f, 22.0f), ac);
                g.setColour (cDim); fitText (g, font (4.4f, Type::bold, 0.08f), k == 0 ? "THE BASS" : k == 1 ? "THE BODY" : "THE TOP", RF (x0 + 4.0f, y + 42.0f, 56.0f, 10.0f), Just::centredLeft);
                fitText (g, nf, "PUSHED DOWN BY", RF (x0 + 486.0f, y + 12.0f, 200.0f, 13.0f), Just::centredLeft);
                fitText (g, nf, "NOW", RF ((float) meterRect (k).getX() - 4.0f, y - 1.0f, 46.0f, 13.0f));
            }
            g.setColour (cFaint);
            fitText (g, font (4.6f, Type::bold, 0.08f), "PUNCH: EVERY HIT PUSHES.   SMART: A PART GOES DOWN ONLY WHILE ITS DRUMS REALLY FILL IT.   NOW: HOW FAR EACH BAY (A B C D) IS PUSHED DOWN.",
                     RF (x0, pr.getBottom() - 22.0f, pr.getWidth() - 44.0f, 12.0f));
        }
    }

    Processor& proc; const Art& art; int page = -1, scope = -1; RI panel; juce::Image panelImg; float imgScale = 0.0f;
    std::atomic<float>* modeRaw = nullptr; Knob* smartKnob = nullptr; int gr[kBands][kBays] {};
    juce::OwnedArray<Btn> tabs; Btn closeBtn; std::vector<Cell> cells; std::unique_ptr<Choice> engine; std::unique_ptr<CurveBox> curve;
};

} // namespace

// ====================================================================================== the editor
// ================================================================ the drum sound browser: a click on a lane's sound opens it.
// Where from (YOUR MICKENS DRUMS / CLASSIC BREAK KITS / BUILT-IN), what kind (KICK, SNARE, CLOSED HAT, OPEN HAT, PERCS) and, for the
// classic kits, which break it was cut from. A click on a sound puts it on the lane and plays it; a double-click also closes the window.
class DrumBrowser final : public juce::Component
{
public:
    DrumBrowser (Processor& p) : proc (p), closeBtn ("CLOSE", cIce)
    {
        setWantsKeyboardFocus (true); setComponentID ("drumbrowser");
        static const char* const srcNames[] { "YOUR MICKENS DRUMS", "CLASSIC BREAK KITS", "BUILT-IN" };
        static const char* const kindNames[] { "KICK", "SNARE", "CLOSED HAT", "OPEN HAT", "PERCS" };
        for (int i = 0; i < 3; ++i) { auto* b = srcBtn.add (new Btn (srcNames[i], i == 0 ? cPink : i == 1 ? cCyan : cViolet)); b->cap = 5.6f; b->onClick = [this, i] { src = i; kit = 0; refresh(); }; addAndMakeVisible (b); b->setVisible (i != 1); }   // only YOUR MICKENS DRUMS and BUILT-IN
        for (int i = 0; i < 5; ++i) { auto* b = kindBtn.add (new Btn (kindNames[i], cIce)); b->cap = 5.8f; b->onClick = [this, i] { kind = i; refresh(); }; addAndMakeVisible (b); }
        closeBtn.onClick = [this] { close(); }; addAndMakeVisible (closeBtn);
        view.setViewedComponent (&list, false); view.setScrollBarsShown (true, false); view.setScrollBarThickness (8); addAndMakeVisible (view);
        list.owner = this;
    }
    bool isOpen() const { return lane >= 0; }
    void open (int ln)
    {
        lane = juce::jlimit (0, kDrums - 1, ln);
        // start where the lane's sound is
        const auto cur = proc.pattern().snd[(size_t) lane]; src = 0; kind = juce::jlimit (0, 4, Processor::kindTab (drumCategory (lane))); kit = 0;
        bool found = false;
        for (const auto& e : proc.drumCatalog()) if (e.s == cur) { src = sourceIndex (e.source); kind = e.tab; found = true; if (src == 1) curKit = e.kit; break; }
        if (! found && count (0, -1) == 0) src = 2;
        refresh (true); setVisible (true); toFront (false); if (isShowing()) grabKeyboardFocus();
    }
    void close() { lane = -1; setVisible (false); }
    void soundChanged() { if (lane >= 0) list.repaint(); }
    bool keyPressed (const juce::KeyPress& k) override { if (k == juce::KeyPress::escapeKey) { close(); return true; } return false; }
    void mouseDown (const juce::MouseEvent& e) override { if (! panel.contains (e.getPosition())) close(); }
    void resized() override
    {
        panel = getLocalBounds().withSizeKeepingCentre (930, 600);
        auto r = panel.reduced (24, 18);
        auto head = r.removeFromTop (44); closeBtn.setBounds (head.removeFromRight (96).withSizeKeepingCentre (96, 30));
        r.removeFromTop (6); srcRow = r.removeFromTop (34);
        { auto a = srcRow.withTrimmedLeft (90); for (auto* b : srcBtn) if (b->isVisible()) b->setBounds (a.removeFromLeft (220).reduced (3, 2)); }
        r.removeFromTop (8); kindRow = r.removeFromTop (34);
        { auto a = kindRow.withTrimmedLeft (90); for (auto* b : kindBtn) b->setBounds (a.removeFromLeft (150).reduced (3, 2)); }
        r.removeFromTop (8); kitRow = r.removeFromTop (kitButtonsShown() ? 30 : 0); if (kitRow.getHeight() > 0) r.removeFromTop (8);
        layoutKits();
        listArea = r; view.setBounds (r.reduced (2));
        list.setSize (view.getWidth() - 10, juce::jmax (view.getHeight(), list.heightFor (view.getWidth() - 10)));
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (gWater ? Col (0xd8f4ece0) : Col (0xc8030210));
        if (lane < 0) return;
        const auto p = panel.toFloat();
        g.setColour (cGlass.withAlpha (0.97f)); g.fillRoundedRectangle (p, 16.0f);
        g.setGradientFill (juce::ColourGradient (cCyan.withAlpha (0.10f), p.getX(), p.getY(), cPink.withAlpha (0.10f), p.getRight(), p.getBottom(), false)); g.fillRoundedRectangle (p, 16.0f);
        neonStroke (g, [&] { juce::Path q; q.addRoundedRectangle (p.reduced (1.0f), 16.0f); return q; }(), cCyan, 1.4f, 0.8f);
        const auto r = panel.reduced (24, 18);
        g.setColour (cText); drawText (g, font (10.0f, Type::italic, 0.06f), "LANE " + juce::String (lane + 1) + "  " + proc.laneName (lane), RF ((float) r.getX(), (float) r.getY(), 520.0f, 30.0f), Just::centredLeft);
        const auto cur = proc.pattern().snd[(size_t) lane];
        g.setColour (cDim); drawText (g, font (5.6f, Type::bold, 0.08f), "NOW: " + (cur ? cur->name.toUpperCase() : juce::String ("NOTHING")) + "    CLICK A SOUND TO LOAD AND HEAR IT  /  DOUBLE-CLICK TO LOAD AND CLOSE",
                                      RF ((float) r.getX(), (float) r.getY() + 28.0f, 760.0f, 16.0f), Just::centredLeft);
        auto label = [&] (RI row, const char* t) { if (row.getHeight() > 0) { g.setColour (cFaint); drawText (g, font (5.4f, Type::bold, 0.1f), t, RF ((float) row.getX(), (float) row.getY(), 84.0f, (float) row.getHeight()), Just::centredLeft); } };
        label (srcRow, "FROM"); label (kindRow, "KIND"); label (kitRow, "BREAK");
        g.setColour (black (0.35f)); g.fillRoundedRectangle (listArea.toFloat(), 8.0f); g.setColour (white (0.10f)); g.drawRoundedRectangle (listArea.toFloat(), 8.0f, 1.0f);
        if (items.empty()) { g.setColour (cDim); drawText (g, font (6.4f, Type::bold, 0.08f), emptyText(), listArea.toFloat(), Just::centred); }
    }

private:
    struct List final : public juce::Component
    {
        DrumBrowser* owner = nullptr; static constexpr int kRowH = 30, kCols = 4;
        int heightFor (int w) const { juce::ignoreUnused (w); return ((int) owner->items.size() + kCols - 1) / kCols * kRowH + 8; }
        RF cell (int i) const { const float cw = (float) getWidth() / kCols; return RF ((float) (i % kCols) * cw, 4.0f + (float) (i / kCols) * kRowH, cw, (float) kRowH).reduced (3.0f, 2.0f); }
        int at (juce::Point<int> p) const { for (int i = 0; i < (int) owner->items.size(); ++i) if (cell (i).contains (p.toFloat())) return i; return -1; }
        void paint (juce::Graphics& g) override
        {
            const auto cur = owner->proc.pattern().snd[(size_t) juce::jmax (0, owner->lane)];
            for (int i = 0; i < (int) owner->items.size(); ++i)
            {
                const auto& e = owner->items[(size_t) i]; const auto r = cell (i); const bool on = e.s == cur, ov = i == hover;
                const Col acc = owner->src == 0 ? cPink : owner->src == 1 ? cCyan : cViolet;
                g.setColour (on ? acc.withAlpha (0.24f) : ov ? white (0.07f) : cInk.withAlpha (0.55f)); g.fillRoundedRectangle (r, 5.0f);
                g.setColour (on ? acc : white (ov ? 0.30f : 0.12f)); g.drawRoundedRectangle (r, 5.0f, on ? 1.3f : 1.0f);
                juce::Path tri; const float px = r.getX() + 9.0f, py = r.getCentreY(); tri.addTriangle (px, py - 3.5f, px, py + 3.5f, px + 5.5f, py);
                g.setColour (on ? acc : cDim); g.fillPath (tri);
                juce::String t = e.s->name.replaceCharacter ('_', ' ');
                if (owner->src == 1 && owner->kit == 0 && e.kit.isNotEmpty() && ! t.startsWithIgnoreCase (e.kit)) t = e.kit + "  " + t;
                g.setColour (on ? hi() : cText); fitText (g, font (5.6f, Type::bold, 0.04f), t.toUpperCase(), r.withTrimmedLeft (21.0f).withTrimmedRight (6.0f), Just::centredLeft, 0.6f);
            }
        }
        void mouseMove (const juce::MouseEvent& e) override { const int h = at (e.getPosition()); if (h != hover) { hover = h; repaint(); } }
        void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
        void mouseDown (const juce::MouseEvent& e) override { const int i = at (e.getPosition()); if (i >= 0) owner->pick (i, false); }
        void mouseDoubleClick (const juce::MouseEvent& e) override { const int i = at (e.getPosition()); if (i >= 0) owner->pick (i, true); }
        int hover = -1;
    };
    static int sourceIndex (const juce::String& s) { return s == "MICKENS" ? 0 : s == "CLASSIC BREAKS" ? 1 : 2; }
    static const char* sourceName (int i) { static const char* n[] { "MICKENS", "CLASSIC BREAKS", "BUILT-IN" }; return n[juce::jlimit (0, 2, i)]; }
    int count (int s, int k) const { int n = 0; for (const auto& e : proc.drumCatalog()) if (sourceIndex (e.source) == s && (k < 0 || e.tab == k)) ++n; return n; }
    bool kitButtonsShown() const { return src == 1 && kits.size() > 0; }
    juce::String emptyText() const
    {
        if (src == 0 && count (0, -1) == 0) return "PUT YOUR DRUMS IN  MUSIC / GHOSTBLOCK / GHOST BREAK / DRUMS  (KICKS, SNARES, HI-HATS, OPEN HATS, PERCS)";
        if (src == 1 && count (1, -1) == 0) return "PUT THE KITS IN  MUSIC / GHOSTBLOCK / GHOST BREAK / CLASSIC BREAK KITS";
        return "NO SOUNDS OF THIS KIND HERE";
    }
    void layoutKits()
    {
        for (auto* b : kitBtn) b->setVisible (kitButtonsShown());
        if (! kitButtonsShown()) return;
        auto a = kitRow.withTrimmedLeft (90); const int w = juce::jmin (96, (a.getWidth()) / juce::jmax (1, kitBtn.size()));
        for (auto* b : kitBtn) b->setBounds (a.removeFromLeft (w).reduced (2, 1));
    }
    void refresh (bool fromOpen = false)
    {
        // the kits of the classic source, in the order they come
        juce::StringArray k; for (const auto& e : proc.drumCatalog()) if (e.source == "CLASSIC BREAKS" && e.kit.isNotEmpty() && ! k.contains (e.kit)) k.add (e.kit);
        if (k != kits)
        {
            kits = k; kitBtn.clear();
            for (int i = 0; i <= kits.size(); ++i) { auto* b = kitBtn.add (new Btn (i == 0 ? juce::String ("ALL") : kits[i - 1].toUpperCase(), cCyan)); b->cap = 4.8f; b->onClick = [this, i] { kit = i; refresh(); }; addChildComponent (b); }
        }
        if (fromOpen && src == 1 && curKit.isNotEmpty()) kit = kits.indexOf (curKit) + 1;
        curKit = {};
        for (int i = 0; i < srcBtn.size(); ++i) { srcBtn[i]->setToggleState (i == src, juce::dontSendNotification); srcBtn[i]->label = juce::String (i == 0 ? "YOUR MICKENS DRUMS" : i == 1 ? "CLASSIC BREAK KITS" : "BUILT-IN") + "  " + juce::String (count (i, -1)); }
        for (int i = 0; i < kindBtn.size(); ++i) { kindBtn[i]->setToggleState (i == kind, juce::dontSendNotification); static const char* const kn[] { "KICK", "SNARE", "CLOSED HAT", "OPEN HAT", "PERCS" }; kindBtn[i]->label = juce::String (kn[i]) + "  " + juce::String (count (src, i)); kindBtn[i]->repaint(); }
        for (int i = 0; i < kitBtn.size(); ++i) kitBtn[i]->setToggleState (i == kit, juce::dontSendNotification);
        items.clear();
        for (const auto& e : proc.drumCatalog())
            if (sourceIndex (e.source) == src && e.tab == kind && (src != 1 || kit == 0 || (kit - 1 < kits.size() && e.kit == kits[kit - 1]))) items.push_back (e);
        resized(); view.setViewPosition (0, 0);
        // the current sound in view
        const auto cur = proc.pattern().snd[(size_t) juce::jmax (0, lane)];
        for (int i = 0; i < (int) items.size(); ++i) if (items[(size_t) i].s == cur) { view.setViewPosition (0, juce::jmax (0, (i / List::kCols) * List::kRowH - view.getHeight() / 2)); break; }
        list.repaint(); repaint();
    }
    void pick (int i, bool andClose)
    {
        if (lane < 0 || i < 0 || i >= (int) items.size()) return;
        proc.setDrumSound (lane, items[(size_t) i].s); proc.engine.pushPad (lane, 0.9f);
        list.repaint(); repaint (panel);
        if (andClose) close();
    }

    Processor& proc; int lane = -1, src = 0, kind = 0, kit = 0; juce::String curKit;
    std::vector<Processor::DrumEntry> items; juce::StringArray kits;
    juce::OwnedArray<Btn> srcBtn, kindBtn, kitBtn; Btn closeBtn; juce::Viewport view; List list;
    RI panel, srcRow, kindRow, kitRow, listArea;
};

struct Editor::Impl
{
    Impl (Editor& e, Processor& p) : ed (e), proc (p)
    {
        ed.setLookAndFeel (&look);
        // ---- the top bar
        logo = make<LogoView> (art.logo); ghost = make<GhostView> (art.ghost);
        play = make<PlayBtn> (proc); bpm = knob ("bpm", "BPM", cIce, Knob::number); swing = knob ("swing", "SWING", cIce);
        rStart = make<HeroBtn> ("RANDOM START", "NEW START POINTS", cPink, 0); rStart->setComponentID ("rstart");
        rCombo = make<HeroBtn> ("RANDOM BREAKS", "NEW BREAK BEAT", cCyan, 1); rCombo->setComponentID ("rcombo");
        rDrums = make<HeroBtn> ("RANDOM DRUM", "SEQUENCE", cMint, 2); rDrums->setComponentID ("rdrums");
        rDrums->onClick = [this] { proc.randomDrums(); ghost->hop(); };
        {
            static const char* const tabName[] { "BREAKS", "DRUMS", "RULES + STUTTER + SPACE" };
            const Col tabCol[] { cPink, cMint, cCyan };
            for (int i = 0; i < 3; ++i)
            {
                tabs[i] = make<Btn> (tabName[i], tabCol[i]); tabs[i]->cap = 6.0f; tabs[i]->setComponentID ("page" + juce::String (i));
                tabs[i]->onClick = [this, i] { proc.apvts.state.setProperty ("ui_page", i, nullptr); ed.relayout(); };
            }
        }
        rStart->onClick = [this] { proc.randomStart(); ghost->hop(); }; rCombo->onClick = [this] { proc.randomCombo(); ghost->hop(); };
        undo = make<Btn> ("UNDO", cIce, Icon::undo); undo->setComponentID ("undo"); undo->onClick = [this] { proc.undo(); };
        tempo = make<TempoSeg> (proc); tempo->setComponentID ("tempo");
        dragBeat = make<Btn> ("DRAG WAV", cMint); dragBeat->setComponentID ("dragbeat"); dragBeat->cap = 4.8f;
        dragBeat->setTooltip ("DRAG ME INTO FL STUDIO: THE WHOLE BEAT AS YOU HEAR IT, ONE LOOP (32 STEPS)");
        beatDrag.make = [this] { return proc.renderWav (-1); }; dragBeat->addMouseListener (&beatDrag, false);
        backHome = make<Btn> ("BACK TO MY BEAT", cAmber, Icon::undo); backHome->setComponentID ("backhome"); backHome->cap = 5.0f;
        backHome->onClick = [this] { if (proc.canGoHome()) proc.backHome(); else proc.backAway(); ghost->hop(); };   // flips: BACK TO MY BEAT <-> BACK TO THE BREAK
        resetAll = make<Btn> ("RESET ALL", cRed); resetAll->setComponentID ("resetall"); resetAll->cap = 4.6f; resetAll->onClick = [this] { proc.resetBays(); };
        breaks = knob ("breaks", "BREAKS", cIce); drums = knob ("drums", "DRUMS", cIce); volume = knob ("volume", "VOLUME", cIce);
        meter = make<MeterView> (proc);
        // ---- the four bays
        for (int b = 0; b < kBays; ++b)
        {
            auto& B = bay[b]; const Col c = bayCol (b); const juce::String pre = bayPrefix (b), cr = slotPrefix (crunchSlot (b)), fl = slotPrefix (filterSlot (b));
            B.lock = toggle (pre + "lock", "", cAmber, Icon::lock);
            B.name = make<BreakBtn> (c); B.name->onClick = [this, b] { breakMenu (b); };
            B.prev = make<Btn> ("", c, Icon::left); B.prev->onClick = [this, b] { stepBreak (b, -1); };
            B.next = make<Btn> ("", c, Icon::right); B.next->onClick = [this, b] { stepBreak (b, 1); };
            B.add = make<Btn> ("ADD", c, Icon::plus); B.add->onClick = [this, b] { addFile (b); };
            B.add->setVisible (false);   // ADD is in SETTINGS (and in the break list)
            B.reset = make<Btn> ("", cRed, Icon::cross); B.reset->setComponentID ("reset" + juce::String (b)); B.reset->onClick = [this, b] { proc.resetBay (b); };
            B.mute = toggle (pre + "mute", "", cRed, Icon::speaker); B.out = toggle (pre + "out", "OWN OUT", c); B.out->setVisible (false);   // OWN OUT is in SETTINGS
            B.wave = make<WaveView> (proc, b); B.wave->onFile = [this, b] (const juce::File& f) { loadFile (b, f); };
            B.seq = make<SeqStrip> (proc, b);
            { auto* dv = make<BayDivider>(); B.divider = dv; dv->onDrag = [this, b] (int dy, bool st) { dragBay (b, dy, st); }; dv->onReset = [this] { setBayHeights ({ kBayH, kBayH, kBayH, kBayH }); }; }
            // the steps are folded away (the break's wave fills the bay); STEPS opens them under the wave. Kept with the project.
            B.drag = make<Btn> ("DRAG WAV", c); B.drag->cap = 4.6f; B.drag->setComponentID ("drag" + juce::String (b));
            B.drag->setTooltip ("DRAG ME INTO FL STUDIO: THIS BAY'S BREAK AS YOU HEAR IT, ONE LOOP (32 STEPS)");
            bayDrag[b].make = [this, b] { return proc.renderWav (b); }; B.drag->addMouseListener (&bayDrag[b], false);
            B.steps = make<Btn> ("HIDE STEPS", c); B.steps->offLabel = "SHOW STEPS"; B.steps->cap = 4.6f; B.steps->setComponentID ("steps" + juce::String (b));
            B.steps->setToggleState (stepsOpen (b), juce::dontSendNotification);
            B.steps->onClick = [this, b]
            {
                const int m = (int) proc.apvts.state.getProperty ("steps_open", 0) ^ (1 << b); proc.apvts.state.setProperty ("steps_open", m, nullptr);
                bay[b].steps->setToggleState (stepsOpen (b), juce::dontSendNotification); ed.relayout();
            };
            B.nudge = knob (pre + "nudge", "NUDGE", c, Knob::inside); B.grid = make<Choice> (proc, pre + "grid", c);
            B.pitch = knob (pre + "pitch", "PITCH", c); B.decay = knob (pre + "decay", "DECAY", c); B.level = knob (pre + "level", "LEVEL", c);
            B.pan = knob (pre + "pan", "PAN", c); B.pan->pan = true;
            B.cho = knob (pre + "send_cho", "CHORUS", c); B.rev = knob (pre + "send_rev", "REVERB", c);
            static const char* const depth[] { "duck_low", "duck_mid", "duck_high" };
            for (int k = 0; k < kBands; ++k) { B.duck[k] = knob (pre + depth[k], bandName (k), c); B.duck[k]->grBar = true; B.duck[k]->labelCap = 4.9f; }
            B.crunchOn = toggle (cr + "on", "ON", c); B.crunchOn->offLabel = "OFF"; B.drive = knob (cr + "thres", "THRES", c);   // CLIP: FL's Fruity Soft Clipper; lower = more clipping (POST is in FX)
            { const juce::String cu = slotPrefix (crushSlot (b)); B.crushOn = toggle (cu + "on", "ON", c); B.crushOn->offLabel = "OFF"; B.crush = knob (cu + "drive", "DRIVE", c); }   // CRUSH: TUBE + DOOM (BLEND in FX)
            B.filterOn = toggle (fl + "on", "ON", c); B.filterOn->offLabel = "OFF";
            B.filterDrums = toggle (juce::String (bayPrefix (b)) + "filter_drums", "+DRUMS", c); B.filterDrums->cap = 3.4f;
            B.filterDrums->setTooltip ("+DRUMS: THE 6-LANE DRUMS GO THROUGH THIS BAY'S FILTER TOO"); B.cutoff = knob (fl + "cutoff", "CUTOFF", c);
            B.fx = make<Btn> ("FX", c, Icon::sliders); B.fx->cap = 6.2f; B.fx->onClick = [this, b] { ed.showFx (crushSlot (b)); };
            B.muteRaw = proc.apvts.getRawParameterValue (pre + "mute"); B.lockRaw = proc.apvts.getRawParameterValue (pre + "lock");
        }
        // ---- the drum machine
        grid = make<DrumGrid> (proc);
        for (int ln = 0; ln < kDrums; ++ln)
        {
            auto& L = lane[ln]; const juce::String pre = drumPrefix (ln);
            L.lock = toggle (pre + "lock", "", cAmber, Icon::lock); L.pad = make<PadBtn> (proc, ln); L.pad->onOpen = [this, ln] { browser->open (ln); };
            L.sound = make<MenuBtn> (cIce); L.sound->onClick = [this, ln] { browser->open (ln); };
            L.level = knob (pre + "level", "LEVEL", cIce, Knob::bar); L.tune = knob (pre + "tune", "TUNE", cIce, Knob::bar);
            L.pan = knob (pre + "pan", "PAN", cIce, Knob::bar); L.pan->pan = true;
            L.mute = toggle (pre + "mute", "MUTE", cRed); L.mute->cap = 4.9f;
            L.muteRaw = proc.apvts.getRawParameterValue (pre + "mute"); L.hits = proc.engine.drumHits[ln].load();
        }
        // ---- RULES, STUTTER, SPACE + OUTPUT
        static const char* const ruleId[] { "rule_density", "rule_chaos", "rule_ratchet", "rule_reverse", "rule_trade" };
        static const char* const ruleName[] { "DENSITY", "CHAOS", "STUTTER HITS", "REVERSE", "TRADE" };
        for (int i = 0; i < 5; ++i) rule[i] = knob (ruleId[i], ruleName[i], cCyan);
        ruleSlice = make<Choice> (proc, "rule_slice", cCyan); ruleNew = toggle ("rule_newbreaks", "NEW BREAKS", cCyan);
        hold = make<HoldBtn> (proc);
        stType = make<Choice> (proc, "stut_type", cPink); stRate = make<Choice> (proc, "stut_rate", cPink);
        stSync = toggle ("stut_sync", "ON", cPink); stSync->offLabel = "OFF"; stMotion = knob ("stut_motion", "MOTION", cPink);
        stTarget = make<Choice> (proc, "stut_target", cPink); stTarget->setComponentID ("stuttarget");   // what HOLD repeats: the breaks, the drums or both
        stScope = make<StutterScope> (proc.engine);
        stFilter = toggle ("sfilt_on", "FILTER", cPink); stGate = toggle ("sgate_on", "GATE", cPink); stLink = toggle ("stut_link", "ONLY\nON HOLD", cPink);
        stFilterFx = make<Btn> ("", cPink, Icon::sliders); stFilterFx->onClick = [this] { ed.showFx (sStutFilter); };
        stGateFx = make<Btn> ("", cPink, Icon::sliders); stGateFx->onClick = [this] { ed.showFx (sStutGate); };
        chorusFx = make<Btn> ("CHORUS", cViolet, Icon::sliders); chorusFx->onClick = [this] { ed.showFx (sChorus); };
        reverbFx = make<Btn> ("REVERB", cViolet, Icon::sliders); reverbFx->onClick = [this] { ed.showFx (sReverb); };
        limitOn = toggle ("limit_on", "LIMITER", cViolet); limitFx = make<Btn> ("", cViolet, Icon::sliders); limitFx->onClick = [this] { ed.showFx (sLimiter); };
        drumsOut = toggle ("drums_out", "DRUMS OWN OUT", cViolet); drumsOut->setVisible (false);   // in SETTINGS now
        // SETTINGS: the rarely-touched switches, off the main screen (each bay's and the drums' own output)
        settings = make<Btn> ("SETTINGS", cIce, Icon::sliders); settings->setComponentID ("settings");
        settings->onClick = [this]
        {
            juce::PopupMenu m; m.addSectionHeader ("OWN OUTPUTS (for separate mixer tracks in FL)");
            auto on = [this] (const juce::String& id) { return proc.apvts.getRawParameterValue (id)->load() > 0.5f; };
            for (int b = 0; b < kBays; ++b) m.addItem (1 + b, "Bay " + juce::String::charToString ((juce::juce_wchar) ('A' + b)) + " out of its own output", true, on (juce::String (bayPrefix (b)) + "out"));
            m.addItem (10, "Drums out of their own output", true, on ("drums_out"));
            m.addSectionHeader ("BAYS");
            for (int b = 0; b < kBays; ++b) m.addItem (41 + b, "Add a break file to bay " + juce::String::charToString ((juce::juce_wchar) ('A' + b)) + "...");
            m.addItem (45, "All bays back to the same size");
            m.addSectionHeader ("LAYOUT");
            m.addItem (51, "Classic (everything on one screen)", true, ! pagesMode()); m.addItem (52, "Pages (bigger: breaks, drums, rules on their own pages)", true, pagesMode());
            m.addSectionHeader ("LOOK");
            m.addItem (31, "Neon night", true, ! gWater); m.addItem (32, "Watercolor (light)", true, gWater);
            m.addItem (33, "   Watercolor brightness: bright", true, gWater && gPaper == 0); m.addItem (34, "   Watercolor brightness: soft", true, gWater && gPaper == 1); m.addItem (35, "   Watercolor brightness: dusk", true, gWater && gPaper == 2);
            m.addSectionHeader ("FOLDERS");
            m.addItem (21, "Open the breaks folder"); m.addItem (22, "Open the drums folder"); m.addItem (23, "Rescan the folders (after adding files)");
            showMenu (m, *settings, [safe = juce::Component::SafePointer<Editor> (&ed), this] (int r)
            {
                if (safe == nullptr || r <= 0) return;
                if (r >= 41 && r <= 44) { addFile (r - 41); return; }
                if (r == 51 || r == 52) { proc.apvts.state.setProperty ("ui_layout", r == 52 ? "pages" : "classic", nullptr); ed.relayout(); return; }
                if (r == 45) { setBayHeights ({ kBayH, kBayH, kBayH, kBayH }); return; }
                if (r >= 31 && r <= 35)
                {   // the look (and the watercolour's brightness): kept with the project; the window is rebuilt in it (after this menu has closed)
                    if (r >= 33) proc.apvts.state.setProperty ("paper", r - 33, nullptr);
                    proc.apvts.state.setProperty ("look", r == 31 ? "neon" : "water", nullptr);
                    juce::MessageManager::callAsync ([e = juce::Component::SafePointer<Editor> (&ed)] { if (e != nullptr) e->rebuild(); });
                    return;
                }
                if (r >= 21) { auto* b = r == 21 ? breaksDir : r == 22 ? drumsDir : rescan; if (b->onClick) b->onClick(); return; }
                const juce::String id = r == 10 ? juce::String ("drums_out") : juce::String (bayPrefix (r - 1)) + "out";
                if (auto* q = proc.apvts.getParameter (id)) { q->beginChangeGesture(); q->setValueNotifyingHost (q->getValue() > 0.5f ? 0.0f : 1.0f); q->endChangeGesture(); }
            });
        };
        duckFx = make<Btn> ("DUCK SETUP", cBlue, Icon::sliders); duckFx->onClick = [this] { ed.showFx (kDuckPage); };
        breaksDir = make<Btn> ("BREAKS FOLDER", cViolet, Icon::folder); breaksDir->onClick = [this] { reveal (proc.breaksFolder(), false); };
        drumsDir = make<Btn> ("DRUMS FOLDER", cViolet, Icon::folder); drumsDir->onClick = [this] { reveal (proc.drumsFolder(), true); };
        rescan = make<Btn> ("RESCAN", cViolet, Icon::refresh); rescan->onClick = [this] { proc.rescanUserBreaks(); patternChanged(); };
        for (auto* b : { static_cast<Btn*> (chorusFx), reverbFx, limitOn, drumsOut, settings, duckFx, breaksDir, drumsDir, rescan, stFilter, stGate, ruleNew }) b->cap = 5.0f;
        // ---- the split sidechain's routing: the DUCK knobs are named after the drums that push their band down
        for (int k = 0; k < kBands; ++k)
        {
            bandOnRaw[k] = proc.apvts.getRawParameterValue (juce::String (bandPrefix (k)) + "on");
            for (int ln = 0; ln < kDrums; ++ln) routeRaw[k][ln] = proc.apvts.getRawParameterValue (juce::String (bandPrefix (k)) + "from_" + juce::String (drumPrefix (ln)).dropLastCharacters (1));
        }
        // ---- the FX panel: last, so it lies over everything
        fx = make<FxPanel> (proc, art); fx->setVisible (false); fx->onType = [this] (Knob& k) { valueBox.open (k, ed); };
        fx->onOpened = [this]
        {   // the FX panel scaled up to (nearly) fill the window: bigger knobs, bigger words
            const auto pr = fx->panelBounds().toFloat(); if (pr.isEmpty()) return;
            const float sc = juce::jlimit (1.0f, 1.6f, juce::jmin (((float) kW - 16.0f) / pr.getWidth(), ((float) kH - 16.0f) / pr.getHeight()));
            fxTf = juce::AffineTransform::translation (-pr.getCentreX(), -pr.getCentreY()).scaled (sc).translated ((float) kW * 0.5f, (float) kH * 0.5f); extraTf[fx] = fxTf;
            ed.resized();
        };
        browser = make<DrumBrowser> (proc); browser->setVisible (false);
    }
    ~Impl() { chooser.reset(); }

    // ------------------------------------------------------------------ making controls
    template <class T, class... A> T* make (A&&... a) { auto* c = new T (std::forward<A> (a)...); owned.add (c); ed.addAndMakeVisible (c); return c; }
    Knob* knob (const juce::String& id, const juce::String& name, Col c, Knob::Mode m = Knob::swap)
    {
        auto* k = make<Knob> (proc, id, name, c, m); k->onType = [this] (Knob& x) { valueBox.open (x, ed); }; return k;
    }
    Btn* toggle (const juce::String& id, const juce::String& name, Col c, Icon ic = Icon::none) { auto* b = make<Btn> (name, c, ic); b->attach (proc, id); return b; }

    // ------------------------------------------------------------------ where everything sits (and the words painted behind it)
    struct Tag { juce::String text; RF r; float cap; Col col; Just just; Type::Weight weight; float track; };
    struct Frame { RF r; juce::String legend; Col col; };
    struct Sign { juce::String text; RF r; float cap; Col col; Just just; };
    void tag (const juce::String& t, RF r, float cap, Col c, Just j = Just::centred, Type::Weight w = Type::bold, float track = 0.06f) { tags.push_back ({ t, r, cap, c, j, w, track }); }
    static RF rf (int x, int y, int w, int h) { return RI (x, y, w, h).toFloat(); }

    void layout()
    {
        tags.clear(); frames.clear(); signs.clear(); screens.clear(); lines.clear(); paneList.clear();
        for (auto* c : pageHidden) c->setVisible (true);
        pageHidden.clear();
        const bool pages = pagesMode(); const int pg = pageNo();
        const int pageTop = kTopY + kTopH + kGap + 40, pageBottom = kH - kM;   // PAGES: under the tab row
        for (int i = 0; i < 3; ++i) { tabs[i]->setVisible (pages); tabs[i]->setToggleState (pages && pg == i, juce::dontSendNotification); tabs[i]->setBounds (kM + 4 + i * 200, kTopY + kTopH + kGap + 2, i == 2 ? 300 : 194, 32); }
        struct Marks { size_t t, s, f, sc, l; };
        auto mark = [this] { return Marks { tags.size(), signs.size(), frames.size(), screens.size(), lines.size() }; };
        // a section's parts and its chrome (between two marks) moved by T (or hidden: moved far away)
        auto moveSection = [this] (const std::vector<juce::Component*>& comps, const juce::AffineTransform& T, Marks a, Marks b)
        {
            const float sc = std::sqrt (std::abs (T.getDeterminant()));
            for (auto* c : comps) if (c != nullptr) extraTf[c] = extraTf.count (c) ? extraTf[c].followedBy (T) : T;
            for (size_t i = a.t; i < b.t; ++i) { tags[i].r = tags[i].r.transformedBy (T); tags[i].cap *= sc; }
            for (size_t i = a.s; i < b.s; ++i) { signs[i].r = signs[i].r.transformedBy (T); signs[i].cap *= sc; }
            for (size_t i = a.f; i < b.f; ++i) frames[i].r = frames[i].r.transformedBy (T);
            for (size_t i = a.sc; i < b.sc; ++i) screens[i] = screens[i].transformedBy (T);
            for (size_t i = a.l; i < b.l; ++i) lines[i] = lines[i].transformedBy (T);
        };
        auto hideSection = [this, &moveSection] (const std::vector<juce::Component*>& comps, Marks a, Marks b)
        {
            for (auto* c : comps) if (c != nullptr && c->isVisible()) { c->setVisible (false); pageHidden.push_back (c); }
            moveSection ({}, juce::AffineTransform::translation (20000.0f, 0.0f), a, b);
        };
        // ---- the top bar
        const auto top = topRect(); const int ty = top.getY(), cy = top.getCentreY();
        logo->setBounds (top.getX() + 5, ty + 2, 138, kTopH - 4);
        play->setBounds (158, cy - 23, 58, 46); bpm->setBounds (222, cy - 25, 70, 33); tempo->setBounds (222, cy + 10, 70, 15); swing->setBounds (296, cy - 24, 46, 48);
        rStart->setBounds (353, cy - 27, 150, 54); ghost->setBounds (503, ty + 1, 46, kTopH - 2); rCombo->setBounds (549, cy - 27, 138, 54); rDrums->setBounds (685, cy - 27, 138, 54);
        if (pagesMode()) { undo->setBounds (825, cy - 27, 56, 25); resetAll->setBounds (825, cy + 2, 56, 25); backHome->setBounds (kW - kM - 262, kTopY + kTopH + kGap + 2, 258, 32); backHome->cap = 6.0f;
                           dragBeat->setBounds (kW - kM - 262 - 130, kTopY + kTopH + kGap + 2, 124, 32); dragBeat->cap = 5.6f; }
        else { undo->setBounds (825, cy - 28, 56, 17); backHome->setBounds (825, cy - 9, 56, 18); resetAll->setBounds (825, cy + 11, 56, 17); backHome->cap = 3.6f;
               dragBeat->setBounds (1050, cy - 28, 114, 14); dragBeat->cap = 4.4f; }
        breaks->setBounds (894, cy - 24, 46, 48); drums->setBounds (940, cy - 24, 46, 48); volume->setBounds (986, cy - 24, 46, 48);
        const int my = pagesMode() ? 0 : 5;   // the classic layout: DRAG WAV sits on top of the meter
        meter->setBounds (1050, cy - 5 + my, 114, 9);
        tag ("OUTPUT", rf (1050, cy - 20 + my * 2, 60, 10), 4.6f, cDim, Just::centredLeft, Type::bold, 0.14f);
        tag ("v" + proc.build, rf (1104, cy - 20 + my * 2, 60, 10), 4.4f, cFaint, Just::centredRight, Type::medium, 0.04f);
        for (int db : { -36, -24, -12, 0 }) tag (juce::String (db), RF (1050.0f + 114.0f * ((float) db + 42.0f) / 45.0f - 12.0f, (float) (cy + 7 + my), 24.0f, 9.0f), 4.0f, cFaint, Just::centred, Type::medium, 0.0f);
        for (int x : { 349, 888, 1041 }) lines.push_back (RF ((float) x, (float) cy - 20.0f, 1.0f, 40.0f));
        paneList.push_back ({ topRect().toFloat(), cViolet, 10.0f });
        // ---- the four bays. Each is laid out at its usual size, then its left column and its knobs are scaled with its height
        // (a taller bay: bigger knobs and a taller wave; a shorter one: all a little smaller); the wave takes the room between.
        extraTf.clear(); if (fx != nullptr) extraTf[fx] = fxTf;
        if (pages) { gBayTop = 40; const int h = (pageBottom - pageTop - 3 * kGap) / kBays; for (int b = 0; b < kBays; ++b) gBayH[b] = h; }   // PAGES: four tall bays, every knob bigger
        else { gBayTop = 0; readBayHeights(); }
        const auto bayMarks0 = mark();
        for (int b = 0; b < kBays; ++b)
        {
            auto& B = bay[b]; const auto r = bayRect (b); const int x0 = r.getX(), y0 = r.getY(), h = r.getHeight(); const Col c = bayCol (b);
            const float f = (float) h / (float) kBayH;
            const auto TL = juce::AffineTransform::scale (f, f, (float) x0, (float) y0), TR = juce::AffineTransform::scale (f, f, (float) (kW - kM), (float) y0);
            auto L = [&] (juce::Component* k) { if (std::abs (f - 1.0f) > 0.001f) extraTf[k] = TL; };
            auto R = [&] (juce::Component* k) { if (std::abs (f - 1.0f) > 0.001f) extraTf[k] = TR; };
            // the plate: the letter, then MUTE, empty (X) and lock under it
            signs.push_back ({ juce::String::charToString ((juce::juce_wchar) ('A' + b)), rf (x0 + 4, y0 + 6, 26, 24).transformedBy (TL), 12.0f * f, c, Just::centred });
            B.mute->setBounds (x0 + 6, y0 + 32, 22, 20); B.reset->setBounds (x0 + 6, y0 + 55, 22, 20); B.lock->setBounds (x0 + 6, y0 + 78, 22, 20);
            const int cx = x0 + 36;
            B.name->setBounds (cx, y0 + 6, 110, 44);
            B.prev->setBounds (cx, y0 + 55, 34, 43); B.next->setBounds (cx + 37, y0 + 55, 34, 43); B.fx->setBounds (cx + 74, y0 + 55, 36, 43);   // FX lives under the name now
            for (auto* k : { (juce::Component*) B.mute, (juce::Component*) B.reset, (juce::Component*) B.lock, (juce::Component*) B.name, (juce::Component*) B.prev, (juce::Component*) B.next, (juce::Component*) B.fx }) L (k);
            // the wave (and the steps under it, when open) between the left column and the knobs
            const int wl = juce::roundToInt ((float) x0 + (float) (gridX() - x0) * f), wr = juce::roundToInt ((float) (kW - kM) - (float) ((kW - kM) - (gridX() + gridW())) * f), ww = wr - wl;
            const bool open = stepsOpen (b);
            if (open) { const int wh = juce::roundToInt ((float) (h - 11) * 0.58f); B.wave->setBounds (wl, y0 + 4, ww, wh); B.seq->setBounds (wl, y0 + 7 + wh, ww, h - 11 - wh); }
            else B.wave->setBounds (wl, y0 + 4, ww, h - 8);
            B.seq->setVisible (open); B.steps->setBounds (wl + ww - 78, y0 + 7, 74, 18); B.steps->toFront (false);
            B.drag->setBounds (wl + ww - 140, y0 + 7, 58, 18); B.drag->toFront (false);
            screens.push_back (rf (wl - 3, y0 + 2, ww + 6, h - 4));
            B.divider->setBounds (x0 + 40, r.getBottom() - 2, r.getWidth() - 80, kGap + 4); B.divider->toFront (false);
            // two rows of knobs, in named groups
            const int rx = rightX() + 4, y1 = y0 + 7, y2 = y0 + 57, fh = 43;
            auto cell = [&] (juce::Component* k, int x, int y) { k->setBounds (x, y + 3, 42, 40); R (k); };
            auto frame = [&] (RF fr, const char* legend) { frames.push_back ({ fr.transformedBy (TR), legend, c }); };
            frame (rf (rx, y1, 88, fh), "START"); cell (B.nudge, rx + 2, y1); B.grid->setBounds (rx + 46, y1 + 9, 40, 19); R (B.grid);
            tag ("GRID", rf (rx + 44, y1 + 32, 42, 11).transformedBy (TR), 5.4f * f, cDim, Just::centred, Type::bold, 0.05f);
            frame (rf (rx + 90, y1, 172, fh), "SOUND"); cell (B.pitch, rx + 92, y1); cell (B.decay, rx + 134, y1); cell (B.level, rx + 176, y1); cell (B.pan, rx + 218, y1);
            frame (rf (rx + 264, y1, 88, fh), "SEND"); cell (B.cho, rx + 266, y1); cell (B.rev, rx + 308, y1);
            frame (rf (rx, y2, 136, fh), "DUCK"); for (int k = 0; k < kBands; ++k) { B.duck[k]->setBounds (rx + 2 + k * 44, y2 + 3, 44, 40); R (B.duck[k]); }
            frame (rf (rx + 138, y2, 70, fh), "CRUSH"); B.crushOn->setBounds (rx + 141, y2 + 12, 26, 20); R (B.crushOn); cell (B.crush, rx + 166, y2);
            frame (rf (rx + 210, y2, 70, fh), "CLIP"); B.crunchOn->setBounds (rx + 213, y2 + 12, 26, 20); R (B.crunchOn); cell (B.drive, rx + 238, y2);
            frame (rf (rx + 282, y2, 70, fh), "FILTER"); B.filterOn->setBounds (rx + 285, y2 + 10, 26, 16); R (B.filterOn); B.filterDrums->setBounds (rx + 284, y2 + 28, 28, 13); R (B.filterDrums); cell (B.cutoff, rx + 310, y2);
            if (! pages || pg == 0) paneList.push_back ({ r.toFloat(), c, 9.0f });
        }
        auto bayComps = [this] (int b)
        {
            auto& B = bay[b];
            std::vector<juce::Component*> v { B.mute, B.reset, B.lock, B.name, B.prev, B.next, B.wave, B.seq, B.steps, B.drag, B.divider, B.nudge, B.grid, B.pitch, B.decay, B.level, B.pan, B.cho, B.rev, B.crushOn, B.crush, B.crunchOn, B.drive, B.filterOn, B.filterDrums, B.cutoff, B.fx };
            for (int k = 0; k < kBands; ++k) v.push_back (B.duck[k]);
            return v;
        };
        if (pages) for (int b = 0; b < kBays; ++b) if (bay[b].divider->isVisible()) { bay[b].divider->setVisible (false); pageHidden.push_back (bay[b].divider); }   // PAGES: the bays share the page evenly
        if (pages && pg != 0) { std::vector<juce::Component*> all; for (int b = 0; b < kBays; ++b) for (auto* c : bayComps (b)) all.push_back (c); hideSection (all, bayMarks0, mark()); }
        // the drums and the bottom row are laid out in their usual places first (PAGES then moves them)
        std::array<int, kBays> keepH {}; for (int b = 0; b < kBays; ++b) keepH[(size_t) b] = gBayH[b];
        const int keepTop = gBayTop;
        if (pages) { gBayTop = 0; for (int b = 0; b < kBays; ++b) gBayH[b] = kBayH; }
        const auto drumMarks0 = mark();
        // ---- the drum machine
        const auto dr = drumRect();
        grid->setBounds (gridX(), dr.getY() + kDrumPad, gridW(), kDrums * kLaneH);
        screens.push_back (rf (gridX() - 3, dr.getY() + kDrumPad - 2, gridW() + 6, kDrums * kLaneH + 4));
        for (int ln = 0; ln < kDrums; ++ln)
        {
            auto& L = lane[ln]; const int y = dr.getY() + kDrumPad + ln * kLaneH, rx = rightX() + 4;
            L.lock->setBounds (dr.getX() + 6, y + 2, 20, kLaneH - 4); L.mute->setBounds (dr.getX() + 30, y + 2, 44, kLaneH - 4);   // MUTE right next to the drum's name
            L.pad->setBounds (dr.getX() + 78, y + 2, 68, kLaneH - 4);
            L.sound->setBounds (rx, y + 2, 150, kLaneH - 4); L.level->setBounds (rx + 155, y, 66, kLaneH); L.tune->setBounds (rx + 226, y, 60, kLaneH);
            L.pan->setBounds (rx + 291, y, 52, kLaneH);
        }
        const auto drumMarks1 = mark();
        // ---- the bottom row
        const int by = bottomY(), cyb = by + 19; const auto signFont = font (6.6f, Type::italic, 0.06f);
        auto title = [&] (int x, const juce::String& big, const juce::String& small, Col c)
        {
            signs.push_back ({ big, rf (x + 12, by + 4, 200, 13), 6.6f, c, Just::centredLeft });
            tag (small, RF ((float) x + 12.0f + textWidth (signFont, big) + 8.0f, (float) by + 4.5f, 260.0f, 13.0f), 4.6f, cDim, Just::centredLeft, Type::bold, 0.12f);
        };
        const int rulesX = kM, stutX = kM + 426, spaceX = kM + 878;
        const auto rulesMarks0 = mark();
        title (rulesX, "RULES", "for RANDOM BREAKS", cCyan);
        for (int i = 0; i < 5; ++i) rule[i]->setBounds (rulesX + 8 + i * 58, cyb + 5, 58, 56);
        tag ("SLICE", rf (rulesX + 304, cyb + 6, 34, 24), 5.2f, cDim, Just::centredLeft); ruleSlice->setBounds (rulesX + 340, cyb + 6, 72, 24); ruleNew->setBounds (rulesX + 304, cyb + 37, 108, 24);
        const auto stutMarks0 = mark();
        title (stutX, "STUTTER", "HOLD TO REPEAT", cPink);
        stTarget->setBounds (stutX + 170, by + 2, 84, 15); stScope->setBounds (stutX + 260, by + 3, 446 - 268, 13);
        const int sx = stutX + 8; hold->setBounds (sx - 3, cyb - 1, 94, 68);
        static const char* const rowName[] { "TYPE", "RATE", "SYNC" };
        for (int i = 0; i < 3; ++i) tag (rowName[i], rf (sx + 97, cyb + 1 + i * 22, 32, 20), 5.2f, cDim, Just::centredLeft);
        stType->setBounds (sx + 130, cyb + 1, 82, 20); stRate->setBounds (sx + 130, cyb + 23, 82, 20); stSync->setBounds (sx + 130, cyb + 45, 44, 20);
        stMotion->setBounds (sx + 216, cyb + 5, 54, 56);
        stFilter->setBounds (sx + 276, cyb + 4, 58, 27); stFilterFx->setBounds (sx + 336, cyb + 4, 26, 27); stGate->setBounds (sx + 276, cyb + 35, 58, 27); stGateFx->setBounds (sx + 336, cyb + 35, 26, 27);
        stLink->setBounds (sx + 372, cyb + 4, 58, 58);
        lines.push_back (rf (sx + 363, cyb + 17, 4, 1)); lines.push_back (rf (sx + 363, cyb + 48, 4, 1)); lines.push_back (rf (sx + 367, cyb + 17, 1, 32)); lines.push_back (rf (sx + 367, cyb + 33, 5, 1));
        const auto spaceMarks0 = mark();
        title (spaceX, "SPACE + OUTPUT", "", cViolet);
        const int px = spaceX + 8;
        chorusFx->setBounds (px, cyb + 1, 76, 20); reverbFx->setBounds (px + 80, cyb + 1, 76, 20); limitOn->setBounds (px + 160, cyb + 1, 82, 20); limitFx->setBounds (px + 244, cyb + 1, 26, 20);
        duckFx->setBounds (px, cyb + 23, 270, 20); settings->setBounds (px, cyb + 45, 270, 20);
        for (auto* h : { breaksDir, drumsDir, rescan }) h->setVisible (false);   // in SETTINGS now
        const auto endMarks = mark();

        std::vector<juce::Component*> drumComps { grid }; for (auto& L : lane) for (auto* c : { (juce::Component*) L.lock, (juce::Component*) L.mute, (juce::Component*) L.pad, (juce::Component*) L.sound, (juce::Component*) L.level, (juce::Component*) L.tune, (juce::Component*) L.pan }) drumComps.push_back (c);
        const std::vector<juce::Component*> rulesComps { rule[0], rule[1], rule[2], rule[3], rule[4], ruleSlice, ruleNew };
        const std::vector<juce::Component*> stutComps { hold, stType, stRate, stSync, stMotion, stFilter, stFilterFx, stGate, stGateFx, stLink, stTarget, stScope };
        const std::vector<juce::Component*> spaceComps { chorusFx, reverbFx, limitOn, limitFx, duckFx, settings };
        const RF rulesR = rf (kM, by, 420, kBottomH), stutR = rf (kM + 426, by, 446, kBottomH), spaceR = rf (kM + 878, by, kW - 2 * kM - 878, kBottomH);
        if (! pages)
        {
            paneList.push_back ({ dr.toFloat(), cIce.interpolatedWith (cViolet, 0.4f), 9.0f });
            paneList.push_back ({ rulesR, cCyan, 9.0f }); paneList.push_back ({ stutR, cPink, 9.0f }); paneList.push_back ({ spaceR, cViolet, 9.0f });
        }
        else
        {
            // DRUMS: tall lanes; the names and buttons on the left and the sound, level, tune, pan on the right grow, the grid takes the room between
            if (pg == 1)
            {
                const float fl = 1.9f, fr = 1.35f; const int laneH = (pageBottom - pageTop - 2 * kDrumPad) / kDrums, top = pageTop + kDrumPad;   // the lanes fill the page
                for (int ln = 0; ln < kDrums; ++ln)
                {
                    auto& L = lane[ln]; const float cy0 = (float) (dr.getY() + kDrumPad + ln * kLaneH) + (float) kLaneH * 0.5f, cy1 = (float) (top + ln * laneH) + (float) laneH * 0.5f;
                    const auto TLd = juce::AffineTransform::translation ((float) -dr.getX(), -cy0).scaled (fl).translated ((float) dr.getX(), cy1);
                    const auto TRd = juce::AffineTransform::translation ((float) -(kW - kM), -cy0).scaled (fr).translated ((float) (kW - kM), cy1);
                    for (auto* c : { (juce::Component*) L.lock, (juce::Component*) L.mute, (juce::Component*) L.pad }) extraTf[c] = TLd;
                    for (auto* c : { (juce::Component*) L.sound, (juce::Component*) L.level, (juce::Component*) L.tune, (juce::Component*) L.pan }) extraTf[c] = TRd;
                }
                const float gx = (float) gridX(), gy = (float) (dr.getY() + kDrumPad), gw = (float) gridW(), gh = (float) (kDrums * kLaneH);
                const float nl = (float) dr.getX() + (gx - (float) dr.getX()) * fl + 4.0f, nr = (float) (kW - kM) - ((float) (kW - kM) - (gx + gw)) * fr - 4.0f;
                const auto TG = juce::AffineTransform::translation (-gx, -gy).scaled ((nr - nl) / gw, (float) (kDrums * laneH) / gh).translated (nl, (float) top);
                extraTf[grid] = TG;
                for (size_t i = drumMarks0.sc; i < drumMarks1.sc; ++i) screens[i] = screens[i].transformedBy (TG);
                paneList.push_back ({ rf (kM, pageTop, kW - 2 * kM, kDrums * laneH + 2 * kDrumPad), cIce.interpolatedWith (cViolet, 0.4f), 9.0f });
            }
            else hideSection (drumComps, drumMarks0, drumMarks1);
            // RULES + STUTTER + SPACE: the three panels a third bigger
            if (pg == 2)
            {
                const float sb = 2.2f;   // the three panels one above the other, each more than twice as big
                auto place = [&] (RF from, float nx, float ny, const std::vector<juce::Component*>& comps, Marks a, Marks b, Col c)
                {
                    const auto T = juce::AffineTransform::translation (-from.getX(), -from.getY()).scaled (sb).translated (nx, ny);
                    moveSection (comps, T, a, b); paneList.push_back ({ from.transformedBy (T), c, 9.0f });
                };
                const float rowH = (float) kBottomH * sb + 8.0f;
                place (rulesR, ((float) kW - rulesR.getWidth() * sb) * 0.5f, (float) pageTop, rulesComps, rulesMarks0, stutMarks0, cCyan);
                place (stutR, ((float) kW - stutR.getWidth() * sb) * 0.5f, (float) pageTop + rowH, stutComps, stutMarks0, spaceMarks0, cPink);
                place (spaceR, ((float) kW - spaceR.getWidth() * sb) * 0.5f, (float) pageTop + rowH * 2.0f, spaceComps, spaceMarks0, endMarks, cViolet);
            }
            else { std::vector<juce::Component*> all = rulesComps; all.insert (all.end(), stutComps.begin(), stutComps.end()); all.insert (all.end(), spaceComps.begin(), spaceComps.end()); hideSection (all, rulesMarks0, endMarks); }
            gBayTop = keepTop; for (int b = 0; b < kBays; ++b) gBayH[b] = keepH[(size_t) b];
        }
        fx->setBounds (0, 0, kW, kH); browser->setBounds (0, 0, kW, kH);
        chrome = {};
    }

    // the backdrop and everything that never moves: painted once (per screen scale) into one picture, then only copied
    void renderChrome (float sc)
    {
        chromeScale = sc;
        chrome = juce::Image (juce::Image::RGB, juce::roundToInt ((float) kW * sc), juce::roundToInt ((float) kH * sc), false);
        juce::Graphics g (chrome); g.addTransform (juce::AffineTransform::scale (sc)); g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        art.drawScene (g);
        g.setColour (cInk.withAlpha (gWater ? 0.12f : 0.26f)); g.fillRect (0, 0, kW, kH);   // the night a little darker: the signs carry the light (on paper: a little paler)
        // ---- the glass panels: first every sign's light on the night around it, then the frosted glass itself
        const auto& panes = paneList; const int by = bottomY();
        for (const auto& p : panes) { juce::Path pp; pp.addRoundedRectangle (p.r, p.rad); juce::DropShadow (p.c.withAlpha (0.3f), 7, {}).drawForPath (g, pp); }
        for (size_t i = 0; i < panes.size(); ++i)
        {
            const auto& p = panes[i]; juce::Path pp; pp.addRoundedRectangle (p.r, p.rad);
            {
                juce::Graphics::ScopedSaveState ss (g); g.reduceClipRegion (pp);
                if (art.frost.isValid()) g.drawImage (art.frost, RF (0.0f, 0.0f, (float) kW, (float) kH)); else g.fillAll (cInk);
                g.setColour (cGlass.withAlpha (gWater ? 0.52f : 0.60f)); g.fillRect (p.r);
                g.setGradientFill (juce::ColourGradient (p.c.withAlpha (0.11f), p.r.getX(), p.r.getY(), p.c.withAlpha (0.0f), p.r.getX() + 300.0f, p.r.getY(), false)); g.fillRect (p.r);
                g.setGradientFill (juce::ColourGradient (white (0.075f), p.r.getX(), p.r.getY(), white (0.0f), p.r.getX(), p.r.getY() + juce::jmin (38.0f, p.r.getHeight()), false)); g.fillRect (p.r);
            }
            if (i == 0) g.setGradientFill (juce::ColourGradient::horizontal (cPink.withAlpha (0.8f), p.r.getX(), cCyan.withAlpha (0.8f), p.r.getRight())); else g.setColour (p.c.withAlpha (0.58f));
            g.strokePath (pp, Stroke (1.1f));
        }
        // ---- the dark screens behind the waveforms, the sequences and the drum grid
        for (const auto& r : screens)
        {
            g.setColour (black (0.36f)); g.fillRoundedRectangle (r, 5.0f);
            g.setColour (black (0.4f)); g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
            g.setColour (white (0.06f)); g.fillRect (RF (r.getX() + 5.0f, r.getBottom(), r.getWidth() - 10.0f, 1.0f));
        }
        // ---- each bay's sign: a plate with a neon edge and the bay's letter
        for (int b = 0; b < kBays && (! pagesMode() || pageNo() == 0); ++b)
        {
            const auto r = bayRect (b); const float f = (float) r.getHeight() / (float) kBayH; const Col c = bayCol (b);
            const RF plate = RF ((float) r.getX() + 4.0f, (float) r.getY() + 4.0f, 26.0f, (float) kBayH - 8.0f).transformedBy (juce::AffineTransform::scale (f, f, (float) r.getX(), (float) r.getY()));
            g.setColour (black (0.42f)); g.fillRoundedRectangle (plate, 5.0f);
            juce::Path pp, edge; pp.addRoundedRectangle (plate.reduced (1.0f), 4.5f); Stroke (1.6f).createStrokedPath (edge, pp);
            if (! gWater) juce::DropShadow (c.withAlpha (0.75f), 5, {}).drawForPath (g, edge); else { g.setColour (c.withAlpha (0.22f)); g.fillRoundedRectangle (plate, 5.0f); }
            g.setColour (c.interpolatedWith (hi(), 0.35f)); g.strokePath (pp, Stroke (1.3f));
        }
        // ---- the groups of knobs
        for (const auto& f : frames)
        {
            const float rad = 5.0f; const auto lf = font (4.5f, Type::bold, 0.14f); const float tw = textWidth (lf, f.legend) + 7.0f, lx = f.r.getX() + 7.0f; const auto& r = f.r;
            g.setColour (black (0.2f)); g.fillRoundedRectangle (r, rad);
            juce::Path p; p.startNewSubPath (lx + tw, r.getY()); p.lineTo (r.getRight() - rad, r.getY()); p.quadraticTo (r.getRight(), r.getY(), r.getRight(), r.getY() + rad);
            p.lineTo (r.getRight(), r.getBottom() - rad); p.quadraticTo (r.getRight(), r.getBottom(), r.getRight() - rad, r.getBottom());
            p.lineTo (r.getX() + rad, r.getBottom()); p.quadraticTo (r.getX(), r.getBottom(), r.getX(), r.getBottom() - rad);
            p.lineTo (r.getX(), r.getY() + rad); p.quadraticTo (r.getX(), r.getY(), r.getX() + rad, r.getY()); p.lineTo (lx, r.getY());
            g.setColour (white (0.15f)); g.strokePath (p, Stroke (1.0f));
            g.setColour (f.col.interpolatedWith (cText, 0.4f)); drawText (g, lf, f.legend, RF (lx + 3.5f, r.getY() - 5.0f, tw, 10.0f), Just::centredLeft);
        }
        for (const auto& l : lines) { g.setColour (white (0.16f)); g.fillRect (l); }
        for (const auto& t : tags) { g.setColour (t.col); fitText (g, font (t.cap, t.weight, t.track), t.text, t.r, t.just); }
        for (const auto& sg : signs) neonSign (g, font (sg.cap, Type::italic, 0.06f), sg.text, sg.r, sg.col, sg.just);
        // ---- the wet street under the bottom row: the three signs mirrored in it (the watercolour has its own lake)
        if (gWater || pagesMode()) return;
        const float fy = (float) (by + kBottomH), fh = (float) kH - fy;
        for (size_t i = panes.size() - 3; i < panes.size(); ++i)
        {
            const auto& p = panes[i];
            g.setGradientFill (juce::ColourGradient (p.c.withAlpha (0.55f), 0.0f, fy + 1.0f, p.c.withAlpha (0.0f), 0.0f, fy + 9.0f, false)); g.fillRect (RF (p.r.getX() + 8.0f, fy + 1.0f, p.r.getWidth() - 16.0f, 8.0f));
            for (int k = 0; k < 9; ++k)
            {
                const float u = ((float) k + 0.5f) / 9.0f, sx = p.r.getX() + p.r.getWidth() * u + std::sin ((float) k * 2.4f + p.r.getX() * 0.01f) * 12.0f, sw = 22.0f + 14.0f * std::sin ((float) k * 1.7f + 1.0f);
                g.setGradientFill (juce::ColourGradient (p.c.withAlpha (0.30f), sx, fy, p.c.withAlpha (0.0f), sx, (float) kH, false)); g.fillRect (RF (sx - sw * 0.5f, fy + 2.0f, sw, fh));
            }
        }
        for (int k = 0; k < 5; ++k) { g.setColour (cInk.withAlpha (0.32f)); g.fillRect (RF (0.0f, fy + 5.0f + (float) k * 3.6f + (float) (k * k) * 0.3f, (float) kW, 1.0f)); }   // ripples
    }
    void paint (juce::Graphics& g)
    {
        const float sc = scaleOf (g); if (! chrome.isValid() || std::abs (sc - chromeScale) > 0.01f) renderChrome (sc);
        g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality); g.drawImage (chrome, RF (0.0f, 0.0f, (float) kW, (float) kH));
    }

    // ------------------------------------------------------------------ live: 30 times a second, only what changed is painted again
    void tick()
    {
        ++ticks; const int step = proc.engine.uiStep.load(); const bool running = step >= 0;
        for (int b = 0; b < kBays; ++b)
        {
            auto& B = bay[b]; const bool m = B.muteRaw != nullptr && B.muteRaw->load() > 0.5f, lk = B.lockRaw != nullptr && B.lockRaw->load() > 0.5f;
            B.seq->setStep (step); B.seq->setState (m, lk); B.wave->tick (running, m);
            for (int k = 0; k < kBands; ++k) B.duck[k]->setActivity ((float) juce::jlimit (0, 12, juce::roundToInt (proc.engine.uiDuck[b][k].load() * 0.75f)) / 12.0f);   // 16 dB = the whole bar
        }
        for (int b = 0; b < kBays; ++b) bay[b].filterDrums->setEnabled (proc.apvts.getRawParameterValue (slotPrefix (filterSlot (b)) + "on")->load() > 0.5f);   // +DRUMS greys out while the FILTER is off
        grid->setStep (step); stScope->tick(); tempo->tick(); backHome->setEnabled (proc.canGoHome() || proc.canGoAway());
        { const juce::String t = proc.canGoAway() ? "BACK TO THE BREAK" : "BACK TO MY BEAT"; if (backHome->label != t) { backHome->label = t; backHome->setButtonText (t); backHome->accent = proc.canGoAway() ? cMint : cAmber; backHome->repaint(); } }
        for (int ln = 0; ln < kDrums; ++ln)
        {
            auto& L = lane[ln]; const uint32_t h = proc.engine.drumHits[ln].load(); const bool m = L.muteRaw != nullptr && L.muteRaw->load() > 0.5f;
            if (h != L.hits) { L.hits = h; L.pad->hit(); if (ln == dKick) ghost->nod(); }
            L.pad->tick(); L.pad->setDim (m); grid->setMuted (ln, m);
        }
        meter->tick(); hold->tick();
        const bool host = proc.hostPlayingNow.load(); play->setHost (host); bpm->setHost (host ? juce::String (proc.hostBpmNow.load(), 1) : juce::String());
        undo->setEnabled (proc.canUndo());
        const float t = (float) ticks / 30.0f; rStart->tick (0.5f + 0.5f * std::sin (t * 1.7f)); rCombo->tick (0.5f + 0.5f * std::sin (t * 1.7f + 2.2f)); rDrums->tick (0.5f + 0.5f * std::sin (t * 1.7f + 4.4f));
        ghost->tick(); if (ticks % 2 == 0) logo->tick();
        int sig = 0; for (int k = 0; k < kBands; ++k) { sig = sig * 2 + (bandOnRaw[k] != nullptr && bandOnRaw[k]->load() > 0.5f ? 1 : 0); for (int ln = 0; ln < kDrums; ++ln) sig = sig * 2 + (routed (k, ln) ? 1 : 0); }
        if (sig != routeSig) { routeSig = sig; for (int k = 0; k < kBands; ++k) { const auto name = duckName (k); for (auto& B : bay) { B.duck[k]->label = name; B.duck[k]->repaint(); } } }
        fx->tick();
    }
    bool routed (int k, int ln) const { return routeRaw[k][ln] != nullptr && routeRaw[k][ln]->load() > 0.5f; }
    juce::String duckName (int k) const   // "KICK>LOW": which drums push this band down (as routed on the DUCK page)
    {
        static const char* const drum[] { "KICK", "SNARE", "C HAT", "O HAT", "PERC 1", "PERC 2" };
        int n = 0, first = -1; bool usual = true;
        for (int ln = 0; ln < kDrums; ++ln) { const bool on = routed (k, ln); if (on) { ++n; if (first < 0) first = ln; } usual = usual && on == defaultRoute (k, ln); }
        const bool off = bandOnRaw[k] != nullptr && bandOnRaw[k]->load() < 0.5f;
        const juce::String who = off ? "OFF" : n == 0 ? "NONE" : usual ? drum[k == 0 ? 0 : k == 1 ? 1 : 3] : juce::String (drum[first]) + (n > 1 ? "+" : "");
        return who + ">" + bandName (k);
    }
    // the pattern (or a break, a drum sound, a drawn curve) changed
    void patternChanged()
    {
        const auto& P = proc.pattern();
        for (int b = 0; b < kBays; ++b)
        {
            const auto& br = P.brk[(size_t) b]; const int ci = proc.bayBreakIndex (b); juce::String where;   // under the name: which pack it is from
            if (br != nullptr && ci >= 0) { const auto& e = proc.breakCatalog()[(size_t) ci]; where = e.kind == Processor::classicBreak ? e.section : e.kind == Processor::yourBreak ? juce::String ("YOUR BREAKS") : juce::String(); }
            else if (br != nullptr && br->user) where = "YOUR FILE";
            bay[b].name->show (br != nullptr ? proc.bayBreakName (b) : juce::String ("NO BREAK"), where);
            bay[b].wave->refresh(); bay[b].seq->repaint();
        }
        grid->repaint();
        for (int ln = 0; ln < kDrums; ++ln) lane[ln].sound->setText (P.snd[(size_t) ln] != nullptr ? P.snd[(size_t) ln]->name : juce::String ("NO SOUND"));
        undo->setEnabled (proc.canUndo()); fx->dataChanged(); browser->soundChanged();
    }

    // ------------------------------------------------------------------ breaks, sounds, folders
    static constexpr int kAddItem = 1000001, kFolderItem = 1000002, kYourFolderItem = 1000003, kEmptyItem = 1000004, kNoneItem = 1000005;
    void breakMenu (int b)
    {
        const auto& cat = proc.breakCatalog(); const int cur = proc.bayBreakIndex (b);
        juce::StringArray secs; std::vector<std::vector<int>> members; std::vector<int> kinds;   // the sections, in the order they first appear
        for (int i = 0; i < (int) cat.size(); ++i)
        {
            const auto& e = cat[(size_t) i]; const juce::String key = juce::String (e.kind) + ":" + e.section; int k = secs.indexOf (key);
            if (k < 0) { k = secs.size(); secs.add (key); members.emplace_back(); kinds.push_back (e.kind); }
            members[(size_t) k].push_back (i);
        }
        juce::PopupMenu m;
        auto addSections = [&] (int kind)
        {
            bool any = false;
            for (int k = 0; k < secs.size(); ++k)
            {
                if (kinds[(size_t) k] != kind) continue;
                any = true;
                const auto& mem = members[(size_t) k]; const juce::String sec = secs[k].fromFirstOccurrenceOf (":", false, false), pre = sec + " - "; juce::PopupMenu sub; bool has = false;
                auto shortName = [&] (int idx) { const auto& n = cat[(size_t) idx].name; return n.startsWith (pre) ? n.substring (pre.length()) : n; };
                if (mem.size() <= 40) { for (int idx : mem) { sub.addItem (idx + 1, shortName (idx), true, idx == cur); has = has || idx == cur; } }
                else for (size_t o = 0; o < mem.size(); o += 40)   // a big folder: 40 at a time
                {
                    juce::PopupMenu part; bool hp = false; const size_t end = juce::jmin (mem.size(), o + 40);
                    for (size_t q = o; q < end; ++q) { part.addItem (mem[q] + 1, shortName (mem[q]), true, mem[q] == cur); hp = hp || mem[q] == cur; }
                    sub.addSubMenu (juce::String ((int) o + 1) + " - " + juce::String ((int) end), part, true, std::unique_ptr<juce::Drawable>(), hp); has = has || hp;
                }
                m.addSubMenu (sec, sub, true, std::unique_ptr<juce::Drawable>(), has);
            }
            return any;
        };
        if (std::find (kinds.begin(), kinds.end(), (int) Processor::classicBreak) != kinds.end()) { m.addSectionHeader ("CLASSIC BREAKS"); addSections (Processor::classicBreak); }
        if (std::find (kinds.begin(), kinds.end(), (int) Processor::builtInBreak) != kinds.end()) { m.addSectionHeader ("STAND-IN BREAKS (NO PACK FOUND)"); addSections (Processor::builtInBreak); }
        m.addSectionHeader ("YOUR BREAKS");
        if (! addSections (Processor::yourBreak)) m.addItem (kNoneItem, "(none yet: ADD a file or drop one on a bay)", false);
        m.addSeparator(); m.addItem (kAddItem, "Add a file..."); m.addItem (kEmptyItem, "Empty this bay (no break)", cur >= 0 || proc.pattern().brk[(size_t) b] != nullptr);
        m.addSeparator(); m.addItem (kFolderItem, "Open the classic breaks folder"); m.addItem (kYourFolderItem, "Open your breaks folder");
        showMenu (m, *bay[b].name, [safe = juce::Component::SafePointer<Editor> (&ed), this, b] (int r)
        {
            if (safe == nullptr || r <= 0) return;
            if (r == kAddItem) addFile (b);
            else if (r == kEmptyItem) proc.resetBay (b);
            else if (r == kFolderItem) reveal (proc.breaksFolder(), false);
            else if (r == kYourFolderItem) { proc.yourBreaksFolder().createDirectory(); reveal (proc.yourBreaksFolder(), false); }
            else if (r != kNoneItem) proc.setBayBreak (b, r - 1);
        });
    }
    void stepBreak (int b, int dir)   // the arrows: the break before / after in the list
    {
        const int n = (int) proc.breakCatalog().size(); if (n <= 0) return;
        const int cur = proc.bayBreakIndex (b); proc.setBayBreak (b, cur < 0 ? 0 : ((cur + dir) % n + n) % n);
    }
    void addFile (int b)
    {
        auto start = proc.breaksFolder(); if (! start.isDirectory()) start = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
        chooser = std::make_unique<juce::FileChooser> ("Add a break to bay " + juce::String::charToString ((juce::juce_wchar) ('A' + b)), start, "*.wav;*.aif;*.aiff;*.flac;*.mp3");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe = juce::Component::SafePointer<Editor> (&ed), this, b] (const juce::FileChooser& fc) { if (safe == nullptr) return; const auto f = fc.getResult(); if (f.existsAsFile()) loadFile (b, f); });
    }
    void loadFile (int b, const juce::File& f)
    {
        juce::String err;
        if (f.existsAsFile() && proc.addUserBreak (proc.importBreak (f), b, err)) patternChanged();
        else bay[b].wave->say (err.isNotEmpty() ? err.toUpperCase() : juce::String ("CAN'T READ THAT FILE"));
    }
    void soundMenu (int ln)   // any sound can go on any lane: yours first, then the built-in ones, each by kind (this lane's own kind on top)
    {
        const auto snd = proc.soundsFor (ln); const auto cur = proc.pattern().snd[(size_t) ln]; const auto lib = sharedLibrary();
        const juce::StringArray kinds { drumCategory (ln), "Kick", "Snare", "Clap", "Hat", "Open Hat", "Perc", "Sub", "Tom", "Cymbal", "FX" };
        auto isMine = [&] (int i) { return lib == nullptr || lib->drums.byName (snd[(size_t) i]->name) == nullptr; };
        juce::PopupMenu m;
        for (int group = 0; group < 2; ++group)
        {
            bool header = false; juce::StringArray done;
            auto section = [&] (const juce::String& kind, bool inlineList)
            {
                std::vector<int> v; for (int i = 0; i < (int) snd.size(); ++i) if (isMine (i) == (group == 0) && snd[(size_t) i]->category.equalsIgnoreCase (kind)) v.push_back (i);
                if (v.empty()) return;
                if (! header) { m.addSectionHeader (group == 0 ? "YOUR SOUNDS" : "BUILT-IN"); header = true; }
                for (size_t o = 0; o < v.size(); o += 30)
                {
                    juce::PopupMenu part; bool has = false; const size_t end = juce::jmin (v.size(), o + 30);
                    for (size_t q = o; q < end; ++q) { const bool t = snd[(size_t) v[q]] == cur; part.addItem (v[q] + 1, snd[(size_t) v[q]]->name, true, t); has = has || t; }
                    const juce::String title = kind.toUpperCase() + "S" + (v.size() > 30 ? "  " + juce::String ((int) o / 30 + 1) : juce::String()) + "  (" + juce::String ((int) (end - o)) + ")";
                    if (inlineList && v.size() <= 16) { m.addSectionHeader (kind.toUpperCase() + "S"); for (size_t q = o; q < end; ++q) { const bool t = snd[(size_t) v[q]] == cur; m.addItem (v[q] + 1, snd[(size_t) v[q]]->name, true, t); } }
                    else m.addSubMenu (title, part, true, std::unique_ptr<juce::Drawable>(), has);
                }
            };
            for (int k = 0; k < kinds.size(); ++k) if (! done.contains (kinds[k])) { section (kinds[k], k == 0); done.add (kinds[k]); }
            juce::StringArray others; for (auto& x : snd) if (! kinds.contains (x->category) && ! others.contains (x->category)) others.add (x->category);
            for (auto& k : others) section (k, false);
        }
        showMenu (m, *lane[ln].sound, [safe = juce::Component::SafePointer<Editor> (&ed), this, ln] (int r)
        {
            if (safe == nullptr || r <= 0) return;
            proc.setDrumSound (ln, r - 1); proc.engine.pushPad (ln, 0.9f);   // picked: hear it
        });
    }
    static void reveal (const juce::File& dir, bool drumFolders)   // opens a folder in the Finder, making it first if it is not there yet
    {
        if (! dir.isDirectory())
        {
            dir.createDirectory();
            if (drumFolders) for (auto* n : { "Kicks", "Snares", "Claps", "Hi-Hats", "Open Hats", "Percs" }) dir.getChildFile (n).createDirectory();
        }
        dir.revealToUser();
    }

    // ------------------------------------------------------------------ everything the window holds
    Editor& ed; Processor& proc; Art art; Look look;
    // ---- the PAGES layout (SETTINGS > LAYOUT): the same parts, bigger, one page at a time: BREAKS, DRUMS, RULES + STUTTER + SPACE
    bool pagesMode() const { return proc.apvts.state.getProperty ("ui_layout", "pages").toString() == "pages"; }   // PAGES is the default (BREAKS first)
    int pageNo() const { return juce::jlimit (0, 2, (int) proc.apvts.state.getProperty ("ui_page", 0)); }
    Btn* tabs[3] {};
    std::vector<juce::Component*> pageHidden;   // parts this page hid (shown again on the next layout)
    struct Pane { RF r; Col c; float rad; };
    std::vector<Pane> paneList;
    std::map<juce::Component*, juce::AffineTransform> extraTf; juce::AffineTransform fxTf;   // a bay's parts, scaled with its height (Editor::resized adds the window's scale)
    void readBayHeights()
    {
        auto t = juce::StringArray::fromTokens (proc.apvts.state.getProperty ("bay_h", "").toString(), ",", ""); int sum = 0; bool ok = t.size() == kBays;
        for (int b = 0; b < kBays && ok; ++b) { const int v = t[b].getIntValue(); ok = v >= kBayMinH && v <= kBayMaxH; sum += v; }
        for (int b = 0; b < kBays; ++b) gBayH[b] = ok && sum == kBays * kBayH ? t[b].getIntValue() : kBayH;
    }
    void setBayHeights (std::array<int, kBays> h)
    {
        juce::StringArray t; for (int v : h) t.add (juce::String (v));
        proc.apvts.state.setProperty ("bay_h", t.joinIntoString (","), nullptr); ed.relayout();
    }
    std::array<int, kBays> dragStart {};
    void dragBay (int b, int dy, bool start)
    {   // the edge under bay b: it grows (or shrinks back), the other three share what is left
        if (start) { for (int i = 0; i < kBays; ++i) dragStart[(size_t) i] = gBayH[i]; return; }
        const int total = kBays * kBayH; std::array<int, kBays> h {};
        h[(size_t) b] = juce::jlimit (kBayMinH, juce::jmin (kBayMaxH, total - (kBays - 1) * kBayMinH), dragStart[(size_t) b] + dy);
        const int rest = total - h[(size_t) b]; int others = 0; for (int i = 0; i < kBays; ++i) if (i != b) others += dragStart[(size_t) i];
        int given = 0, last = -1;
        for (int i = 0; i < kBays; ++i) if (i != b) { h[(size_t) i] = juce::jmax (kBayMinH, (int) std::floor ((double) rest * dragStart[(size_t) i] / juce::jmax (1, others))); given += h[(size_t) i]; last = i; }
        if (last >= 0) h[(size_t) last] += rest - given;   // so they add up exactly
        bool same = true; for (int i = 0; i < kBays; ++i) same = same && h[(size_t) i] == gBayH[i];
        if (! same) setBayHeights (h);
    }
    bool stepsOpen (int b) const { return (((int) proc.apvts.state.getProperty ("steps_open", 0)) >> b & 1) != 0; }
    juce::Image chrome; float chromeScale = 0.0f;
    juce::OwnedArray<juce::Component> owned;   // every control (each owns its parameter attachment, so the two die together)
    std::vector<Tag> tags; std::vector<Frame> frames; std::vector<Sign> signs; std::vector<RF> screens, lines;

    LogoView* logo = nullptr; GhostView* ghost = nullptr; PlayBtn* play = nullptr; Knob* bpm = nullptr, *swing = nullptr, *breaks = nullptr, *drums = nullptr, *volume = nullptr;
    HeroBtn* rStart = nullptr, *rCombo = nullptr, *rDrums = nullptr; Btn* undo = nullptr, *resetAll = nullptr; MeterView* meter = nullptr;
    struct BayUi
    {
        juce::Component* divider = nullptr; Btn* steps = nullptr, *drag = nullptr; Btn* lock, *prev, *next, *add, *reset, *mute, *out, *fx, *crunchOn, *filterOn, *crushOn, *filterDrums; BreakBtn* name; WaveView* wave; SeqStrip* seq; Choice* grid;
        Knob* nudge, *pitch, *decay, *level, *pan, *cho, *rev, *duck[kBands], *drive, *cutoff, *crush; std::atomic<float>* muteRaw, *lockRaw;
    };
    BayUi bay[kBays] {};
    struct LaneUi { Btn* lock, *mute; PadBtn* pad; MenuBtn* sound; Knob* level, *tune, *pan; std::atomic<float>* muteRaw; uint32_t hits; };
    LaneUi lane[kDrums] {};
    DrumGrid* grid = nullptr;
    Knob* rule[5] {}; Choice* ruleSlice = nullptr; Btn* ruleNew = nullptr;
    HoldBtn* hold = nullptr; Choice* stType = nullptr, *stRate = nullptr, *stTarget = nullptr; Knob* stMotion = nullptr; StutterScope* stScope = nullptr; Btn* backHome = nullptr; TempoSeg* tempo = nullptr; Btn* dragBeat = nullptr; WavDrag beatDrag, bayDrag[kBays];
    Btn* stSync = nullptr, *stFilter = nullptr, *stGate = nullptr, *stFilterFx = nullptr, *stGateFx = nullptr, *stLink = nullptr;
    Btn* chorusFx = nullptr, *reverbFx = nullptr, *limitOn = nullptr, *limitFx = nullptr, *drumsOut = nullptr, *settings = nullptr, *duckFx = nullptr, *breaksDir = nullptr, *drumsDir = nullptr, *rescan = nullptr;
    FxPanel* fx = nullptr; DrumBrowser* browser = nullptr;
    std::atomic<float>* routeRaw[kBands][kDrums] {}, *bandOnRaw[kBands] {}; int routeSig = -1, ticks = 0;
    ValueBox valueBox;
    std::unique_ptr<juce::FileChooser> chooser;   // kept alive while its window is open
};

Editor::Editor (Processor& p) : juce::AudioProcessorEditor (p), proc (p)
{
    setOpaque (true); setWantsKeyboardFocus (true);
    applyLook (proc.apvts.state.getProperty ("look", "neon").toString() == "water", (int) proc.apvts.state.getProperty ("paper", 1));
    d = std::make_unique<Impl> (*this, p);
    // the window can be dragged bigger or smaller from its corner (or its edges, where the host lets you); everything keeps its shape
    float s0 = proc.uiScale();
    if (! proc.hasUiScale())   // first open: big (1.3x), as long as it fits the screen
        if (auto* disp = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
            s0 = juce::jlimit (1.0f, 1.3f, juce::jmin ((float) disp->userArea.getWidth() * 0.92f / (float) kW, (float) disp->userArea.getHeight() * 0.88f / (float) kH));
    setResizable (true, true);
    setResizeLimits (juce::roundToInt (kW * 0.6f), juce::roundToInt (kH * 0.6f), juce::roundToInt (kW * 2.5f), juce::roundToInt (kH * 2.5f));
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) kW / (double) kH);
    sized = true;
    setSize (juce::roundToInt (kW * s0), juce::roundToInt (kH * s0));   // resized() puts everything in its place
    d->patternChanged(); d->tick();
    proc.addChangeListener (this); startTimerHz (30);
}
void Editor::rebuild()
{   // a new look: every part is made again in it
    stopTimer(); juce::PopupMenu::dismissAllActiveMenus();
    setLookAndFeel (nullptr); d.reset();
    applyLook (proc.apvts.state.getProperty ("look", "neon").toString() == "water", (int) proc.apvts.state.getProperty ("paper", 1));
    d = std::make_unique<Impl> (*this, proc); laidOut = false; resized();
    d->patternChanged(); d->tick(); repaint(); startTimerHz (30);
}
Editor::~Editor()
{
    stopTimer(); proc.removeChangeListener (this);
    juce::PopupMenu::dismissAllActiveMenus();
    setLookAndFeel (nullptr); d.reset();
}
float Editor::scale() const { return juce::jmax (0.1f, (float) getWidth() / (float) kW); }
void Editor::paint (juce::Graphics& g) { g.addTransform (juce::AffineTransform::scale (scale())); d->paint (g); }
void Editor::resized()
{
    if (d == nullptr) return;
    const float s = scale(); const auto t = juce::AffineTransform::scale (s);
    if (! laidOut) { d->layout(); laidOut = true; }   // every part sits at its place in the 1180 x 760 window; the whole window is then scaled
    for (auto* c : getChildren())
        if (dynamic_cast<juce::ResizableCornerComponent*> (c) == nullptr && dynamic_cast<juce::ResizableBorderComponent*> (c) == nullptr)
        {
            const auto it = d->extraTf.find (c);
            c->setTransform (it != d->extraTf.end() ? it->second.followedBy (t) : t);
        }
    if (sized && getWidth() >= juce::roundToInt (kW * 0.6f) && std::abs (s - proc.uiScale()) > 0.001f) proc.setUiScale (s);   // kept with the project
    repaint();
}
void Editor::showFx (int slot)   // (100 + lane opens that lane's drum sound window)
{
    if (slot >= 100) { d->browser->open (slot - 100); return; }
    if (slot < 0) d->fx->close(); else d->fx->open (slot);
}
void Editor::refreshBreakMenus() { d->patternChanged(); }
void Editor::timerCallback() { d->tick(); }
void Editor::changeListenerCallback (juce::ChangeBroadcaster*) { d->patternChanged(); }
} // namespace gbk
