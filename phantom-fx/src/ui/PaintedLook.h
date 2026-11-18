// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX painted looks (0.1.9): image-based looks loaded from asset folders (assets/looks/<look>/, embedded as one zip each).
//
//   A look = a folder of PNG / JPEG sprites in the designer's export structure (ASSETS.md) plus look.json (its name, order,
//   layout numbers from LAYOUT.md, 9-slice insets, anchors, text colours, the flat palette for code-drawn editors on its slabs).
//   Registry finds every embedded "look-<id>.zip": a new folder dropped into assets/looks/ becomes a new look with no code
//   change (tools/pack_look.py packs an export folder; a look.json key that is missing falls back to the CEL look's value).
//
//   fxtheme look numbers: 0 BLACK, 1 PURPLE, 2 LIGHT, 3 GHOST, then 4.. = the painted looks in "order". painted::active()
//   is the look being painted now (nullptr on the four drawn looks). Everything here runs on the message thread only.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PhantomLooksData.h"
#include "GhostArt.h"
#include "FxTheme.h"
#include "pv/Kit.h"
#include <map>

namespace painted
{
using RF = juce::Rectangle<float>;
using RI = juce::Rectangle<int>;
inline constexpr float kW = 1200.0f, kH = 760.0f;
inline constexpr float kEm = 1.448f;   // the bundled typeface's line height per em: text sizes here are the design's px sizes (ASSETS.md "Text")   // the design size every LAYOUT.md number refers to

// PHANTOM_TRACE=1 in the environment: print the stages of a painted look's paint pass (finding a slow or stuck step on a machine)
inline bool tracing() { static const bool t = juce::SystemStats::getEnvironmentVariable ("PHANTOM_TRACE", "").isNotEmpty(); return t; }
inline void trace (const char* what) { if (tracing()) { static double last = juce::Time::getMillisecondCounterHiRes(); const double now = juce::Time::getMillisecondCounterHiRes(); std::fprintf (stderr, "    [paint +%.0f ms] %s\n", now - last, what); std::fflush (stderr); last = now; } }

struct Img
{
    juce::Image im; float scale = 1.0f;
    bool ok() const { return im.isValid(); }
    float w() const { return (float) im.getWidth() / scale; }
    float h() const { return (float) im.getHeight() / scale; }
};

class Look
{
public:
    juce::String id, name; int order = 99; juce::var cfg; const Look* fallback = nullptr;
    Look (const juce::String& lookId, const void* data, size_t size) : id (lookId), zip (new juce::MemoryInputStream (data, size, false), true)
    {
        if (auto* e = zip.getEntry ("look.json")) if (auto s = std::unique_ptr<juce::InputStream> (zip.createStreamForEntry (*e))) cfg = juce::JSON::parse (s->readEntireStreamAsString());
        name = cfg.getProperty ("name", id.toUpperCase()).toString(); order = (int) cfg.getProperty ("order", 99);
        // a look may bring its own typeface (look.json "font": files inside its folder, its line height per em, a size trim)
        auto face = [this] (const juce::String& key) -> juce::Typeface::Ptr
        {
            const auto f = cfg.getProperty ("font", {}).getProperty (juce::Identifier (key), {}).toString(); if (f.isEmpty()) return {};
            if (auto* e = zip.getEntry (f)) if (auto st = std::unique_ptr<juce::InputStream> (zip.createStreamForEntry (*e))) { juce::MemoryBlock mb; st->readIntoMemoryBlock (mb); return juce::Typeface::createSystemTypefaceFor (mb.getData(), mb.getSize()); }
            return {};
        };
        bold = face ("bold"); black = face ("black"); if (black == nullptr) black = bold;
        if (bold != nullptr) { em = (float) (double) cfg.getProperty ("font", {}).getProperty ("em", 1.2); fontSize = (float) (double) cfg.getProperty ("font", {}).getProperty ("size", 1.0); }
    }
    juce::Typeface::Ptr bold, black; float em = 0.0f, fontSize = 1.0f;
    // ---- look.json access ("rack.tiles.cols"); a missing key falls back to the fallback look (CEL)
    juce::var at (const juce::String& path) const
    {
        juce::var v = cfg; juce::StringArray parts; parts.addTokens (path, ".", "");
        // (only a whole section this look.json does not have falls back: an empty "hatch": {} means "this look has none")
        const bool mayFall = fallback != nullptr && fallback != this && ! (parts.size() > 0 && cfg.isObject() && cfg.hasProperty (juce::Identifier (parts[0])) && (parts[0] == "anim" || parts[0] == "slabs" || parts[0] == "visual"));
        for (auto& p : parts) { if (! v.isObject() || ! v.hasProperty (juce::Identifier (p))) return mayFall ? fallback->at (path) : juce::var(); v = v.getProperty (juce::Identifier (p), {}); }
        return v;
    }
    float num (const juce::String& path, float def = 0.0f) const { const auto v = at (path); return v.isVoid() || v.isString() ? def : (float) (double) v; }
    juce::String str (const juce::String& path) const { const auto v = at (path); return v.isString() ? v.toString() : juce::String(); }
    bool has (const juce::String& path) const { const auto v = at (path); return ! v.isVoid() && ! v.isUndefined(); }
    juce::Colour colour (const juce::String& path, juce::Colour def) const { const auto s = str (path); return s.length() == 6 ? juce::Colour ((juce::uint32) (0xff000000u | (juce::uint32) s.getHexValue32())) : def; }
    RF rect (const juce::String& path, RF def = {}) const { const auto v = at (path); if (auto* a = v.getArray()) if (a->size() >= 4) return { (float) (double) (*a)[0], (float) (double) (*a)[1], (float) (double) (*a)[2], (float) (double) (*a)[3] }; return def; }
    float el (const juce::String& path, int i, float def = 0.0f) const { const auto v = at (path); if (auto* a = v.getArray()) if (i < a->size()) return (float) (double) (*a)[i]; return def; }
    bool listHas (const juce::String& path, const juce::String& item) const { const auto v = at (path); if (auto* a = v.getArray()) for (auto& x : *a) if (x.toString() == item) return true; return false; }

    // ---- colours
    juce::Colour cream() const { return colour ("text.cream", juce::Colour (0xfffbf1d6)); }
    juce::Colour ink() const { return colour ("text.ink", juce::Colour (0xff23182f)); }
    juce::Colour sub() const { return colour ("text.sub", juce::Colour (0xffeadbb8)); }
    juce::Colour onPainting() const { return colour ("text.onPainting", ink()); }
    juce::Colour halo() const { return colour ("text.halo", cream()).withAlpha (num ("text.haloAlpha", 0.9f)); }
    bool darkSlab (const juce::String& slab) const { return listHas ("darkSlabs", slab); }
    juce::Colour inkOn (const juce::String& slab) const { return slab.isEmpty() ? onPainting() : darkSlab (slab) ? cream() : ink(); }

    // ---- images (decoded on first use). A sprite is "name@2x.png" (or "@1.5x", or plain 1x), or "name@2x.jpg" for an opaque
    // picture, or "name@2x.jpg" + "name@2x.a.png" = colour as JPEG and transparency as a small mask (tools/pack_look.py)
    const Img& img (const juce::String& n)
    {
        auto it = cache.find (n); if (it != cache.end()) return it->second;
        Img out;
        auto load = [this] (const juce::String& file) -> juce::Image
        {
            if (auto* e = zip.getEntry (file)) if (auto s = std::unique_ptr<juce::InputStream> (zip.createStreamForEntry (*e))) { juce::MemoryBlock mb; s->readIntoMemoryBlock (mb); return juce::ImageFileFormat::loadFrom (mb.getData(), mb.getSize()); }
            return {};
        };
        for (auto* tag : { "@2x", "@1.5x", "" })
        {
            const juce::String stem = n + tag; out.scale = juce::String (tag) == "@2x" ? 2.0f : juce::String (tag) == "@1.5x" ? 1.5f : 1.0f;
            if (zip.getEntry (stem + ".png") != nullptr) out.im = load (stem + ".png");
            else if (zip.getEntry (stem + ".jpg") != nullptr)
            {
                out.im = load (stem + ".jpg");
                if (out.im.isValid() && zip.getEntry (stem + ".a.png") != nullptr)
                {   // colour + mask -> one picture with transparency (stored premultiplied, as every JUCE picture is)
                    const auto mask = load (stem + ".a.png"); juce::Image both (juce::Image::ARGB, out.im.getWidth(), out.im.getHeight(), false);
                    if (mask.getWidth() == both.getWidth() && mask.getHeight() == both.getHeight())
                    {
                        const juce::Image::BitmapData c (out.im, juce::Image::BitmapData::readOnly), a (mask, juce::Image::BitmapData::readOnly); juce::Image::BitmapData d (both, juce::Image::BitmapData::writeOnly);
                        const bool rgb = out.im.getFormat() == juce::Image::RGB, single = mask.getFormat() == juce::Image::SingleChannel, maskRgb = mask.getFormat() == juce::Image::RGB;
                        for (int y = 0; y < d.height; ++y)
                        {
                            auto* dp = (juce::PixelARGB*) d.getLinePointer (y);
                            for (int x = 0; x < d.width; ++x)
                            {
                                juce::uint8 al; if (single) al = *a.getPixelPointer (x, y); else if (maskRgb) al = ((juce::PixelRGB*) a.getPixelPointer (x, y))->getGreen(); else al = ((juce::PixelARGB*) a.getPixelPointer (x, y))->getGreen();   // (an opaque grey picture, whatever form the system's loader hands it over in)
                                juce::uint8 r, g, b; if (rgb) { auto* q = (juce::PixelRGB*) c.getPixelPointer (x, y); r = q->getRed(); g = q->getGreen(); b = q->getBlue(); } else { auto* q = (juce::PixelARGB*) c.getPixelPointer (x, y); r = q->getRed(); g = q->getGreen(); b = q->getBlue(); }   // (opaque, so nothing to un-multiply)
                                dp[x].setARGB (al, r, g, b); dp[x].premultiply();
                            }
                        }
                        out.im = both;
                    }
                }
            }
            if (out.ok()) break;
        }
        return cache.emplace (n, out).first->second;
    }
    bool hasImg (const juce::String& n) { return n.isNotEmpty() && img (n).ok(); }
    // is the picture in the look's folder? (asked without decoding it)
    bool hasFile (const juce::String& n) const { for (auto* tag : { "@2x", "@1.5x", "" }) for (auto* ext : { ".png", ".jpg" }) if (zip.getEntry (n + tag + ext) != nullptr) return true; return false; }
    void drop (const juce::String& n) { cache.erase (n); }

    // ---- drawing. Every sprite is resampled ONCE to the size it is drawn at (times the display's pixel scale) and kept; after
    // that a draw is a plain 1:1 copy. (Scaling a big picture on every paint is what a slow renderer cannot afford.)
    float pixelScale = 1.0f;   // the editor sets it at the top of each paint pass (1, or 2 on a Retina display)
    // part of a picture resampled to dw x dh. Always done by JUCE's own software renderer (the same code on every system, so a
    // Mac does exactly what Linux does here, never a CoreGraphics resample of a big strip), then handed to the system's image
    // type, so drawing it afterwards is a plain copy the system can keep ready.
    static juce::Image resample (const juce::Image& src, juce::Rectangle<int> from, int dw, int dh, bool opaque)
    {
        juce::Image out (opaque ? juce::Image::RGB : juce::Image::ARGB, juce::jmax (1, dw), juce::jmax (1, dh), true, juce::SoftwareImageType());
        { juce::Graphics gg (out); gg.setImageResamplingQuality (juce::Graphics::highResamplingQuality); gg.drawImage (src, 0, 0, out.getWidth(), out.getHeight(), from.getX(), from.getY(), from.getWidth(), from.getHeight()); }
        return juce::NativeImageType().convert (out);
    }
    void clearSized() { sizedCache.clear(); sizedPx = 0; }
    // where a dw x dh pixel picture goes so that it lands on whole pixels (a 1:1 copy, never a resample while painting)
    RF snapped (RF r, int dw, int dh) const { return { std::round (r.getX() * pixelScale) / pixelScale, std::round (r.getY() * pixelScale) / pixelScale, (float) dw / pixelScale, (float) dh / pixelScale }; }
    const juce::Image& sized (const juce::String& key, const juce::Image& src, juce::Rectangle<int> from, int dw, int dh)
    {
        if (auto it = sizedCache.find (key); it != sizedCache.end()) return it->second;
        if (sizedPx > 24000000 || sizedCache.size() > 4000) clearSized();   // (a long resize drag: start again rather than grow)
        const auto out = resample (src, from, dw, dh, ! src.hasAlphaChannel());
        sizedPx += (juce::int64) out.getWidth() * out.getHeight(); return sizedCache.emplace (key, out).first->second;
    }
    // the whole sprite into r
    void draw (juce::Graphics& g, const juce::String& n, RF r, float alpha = 1.0f)
    {
        const auto& i = img (n); if (! i.ok() || r.getWidth() < 0.5f || r.getHeight() < 0.5f) return;
        const int dw = juce::roundToInt (r.getWidth() * pixelScale), dh = juce::roundToInt (r.getHeight() * pixelScale);
        g.setOpacity (alpha);
        if (dw < 1 || dh < 1) { g.setOpacity (1.0f); return; }
        if (dw == i.im.getWidth() && dh == i.im.getHeight()) g.drawImage (i.im, snapped (r, dw, dh), juce::RectanglePlacement::stretchToFit);
        else g.drawImage (sized (n + "|" + juce::String (dw) + "x" + juce::String (dh), i.im, i.im.getBounds(), dw, dh), snapped (r, dw, dh), juce::RectanglePlacement::stretchToFit);
        g.setOpacity (1.0f);
    }
    // the sprite at its own 1x size times k, its point (ax, ay) (1x px) on p
    void drawAt (juce::Graphics& g, const juce::String& n, juce::Point<float> p, float k, float ax, float ay, float alpha = 1.0f)
    { const auto& i = img (n); if (i.ok()) draw (g, n, { p.x - ax * k, p.y - ay * k, i.w() * k, i.h() * k }, alpha); }
    // frame f of a strip (frames side by side, or stacked when vertical) into r
    void frame (juce::Graphics& g, const juce::String& n, int f, int frames, bool vertical, RF r, float alpha = 1.0f)
    {
        const auto& i = img (n); if (! i.ok() || r.getWidth() < 0.5f || r.getHeight() < 0.5f) return;
        const int fw = vertical ? i.im.getWidth() : i.im.getWidth() / frames, fh = vertical ? i.im.getHeight() / frames : i.im.getHeight();
        f = juce::jlimit (0, frames - 1, f);
        const int dw = juce::roundToInt (r.getWidth() * pixelScale), dh = juce::roundToInt (r.getHeight() * pixelScale);
        g.setOpacity (alpha);
        g.drawImage (sized (n + "#" + juce::String (f) + "|" + juce::String (dw) + "x" + juce::String (dh), i.im, { vertical ? 0 : f * fw, vertical ? f * fh : 0, fw, fh }, dw, dh), snapped (r, dw, dh), juce::RectanglePlacement::stretchToFit);
        g.setOpacity (1.0f);
    }
    // 9-slice: the corners keep their shape (scaled down when r is lower than the sprite), the middle stretches. The assembled
    // panel is kept per size, so a repaint is one copy.
    void nine (juce::Graphics& g, const juce::String& n, RF r, float alpha = 1.0f, const juce::String& sliceKey = {}, float cornerScale = 0.0f)
    {
        const auto& i = img (n); if (! i.ok() || r.getWidth() < 2 || r.getHeight() < 2) return;
        const int pw = juce::jmax (1, (int) std::ceil (r.getWidth() * pixelScale)), ph = juce::jmax (1, (int) std::ceil (r.getHeight() * pixelScale));
        const juce::String key = "9|" + n + "|" + sliceKey + "|" + juce::String (juce::roundToInt (r.getWidth() * 2.0f)) + "x" + juce::String (juce::roundToInt (r.getHeight() * 2.0f)) + "|" + juce::String (cornerScale, 3) + "@" + juce::String (pw);
        auto it = sizedCache.find (key);
        if (it == sizedCache.end())
        {
            if (sizedPx > 24000000 || sizedCache.size() > 4000) clearSized();
            juce::Image soft (juce::Image::ARGB, pw, ph, true, juce::SoftwareImageType());
            { juce::Graphics gg (soft); gg.addTransform (juce::AffineTransform::scale (pixelScale)); nineRaw (gg, i, n, { 0.0f, 0.0f, r.getWidth(), r.getHeight() }, sliceKey, cornerScale); }
            sizedPx += (juce::int64) pw * ph; it = sizedCache.emplace (key, juce::NativeImageType().convert (soft)).first;
        }
        g.setOpacity (alpha); g.drawImage (it->second, snapped (r, pw, ph), juce::RectanglePlacement::stretchToFit); g.setOpacity (1.0f);
    }
    void nineRaw (juce::Graphics& g, const Img& i, const juce::String& n, RF r, const juce::String& sliceKey, float cornerScale)
    {
        const juce::String key = "slice." + (sliceKey.isNotEmpty() ? sliceKey : n);
        float l = el (key, 0, 12), t = el (key, 1, 10), rr = el (key, 2, 12), b = el (key, 3, 12);
        const float sc = i.scale; const int iw = i.im.getWidth(), ih = i.im.getHeight();
        const int sl = juce::roundToInt (l * sc), st = juce::roundToInt (t * sc), sr = juce::roundToInt (rr * sc), sb = juce::roundToInt (b * sc);
        float k = juce::jmin (1.0f, r.getHeight() / i.h(), r.getWidth() / i.w());
        if (t + b <= 0.0f) k = juce::jmin (1.0f, r.getHeight() / i.h());
        if (cornerScale > 0.0f) k = cornerScale;
        const float x0 = r.getX(), y0 = r.getY(), x3 = r.getRight(), y3 = r.getBottom();
        float x1 = x0 + l * k, x2 = x3 - rr * k, y1 = y0 + t * k, y2 = y3 - b * k;
        if (x2 < x1) x1 = x2 = (x0 + x3) * 0.5f; if (y2 < y1) y1 = y2 = (y0 + y3) * 0.5f;
        const float dx[4] { x0, x1, x2, x3 }, dy[4] { y0, y1, y2, y3 }; const int sx[4] { 0, sl, iw - sr, iw }, sy[4] { 0, st, ih - sb, ih };
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        for (int a = 0; a < 3; ++a) for (int c = 0; c < 3; ++c)
            if (dx[c + 1] > dx[c] + 0.01f && dy[a + 1] > dy[a] + 0.01f && sx[c + 1] > sx[c] && sy[a + 1] > sy[a])
                g.drawImage (i.im.getClippedImage ({ sx[c], sy[a], sx[c + 1] - sx[c], sy[a + 1] - sy[a] }), RF (dx[c], dy[a], dx[c + 1] - dx[c], dy[a + 1] - dy[a]).expanded (a == 1 || c == 1 ? 0.25f : 0.0f), juce::RectanglePlacement::stretchToFit);
    }
    // 9-slice with the sprite's BODY on r: every stretchable sprite of the export has a soft margin of about 5 px for its shadow
    // or glow (ASSETS.md), and LAYOUT.md's rectangles are the bodies; the margin is drawn around r
    void nineBody (juce::Graphics& g, const juce::String& n, RF r, float alpha = 1.0f, const juce::String& sliceKey = {})
    {
        const auto& i = img (n); if (! i.ok()) return;
        const float ml = num ("margin.l", 5.5f), mt = num ("margin.t", 5.0f), mr = num ("margin.r", 5.0f), mb = num ("margin.b", 5.0f);
        float k = juce::jmin (1.0f, r.getHeight() / juce::jmax (1.0f, i.h() - mt - mb), r.getWidth() / juce::jmax (1.0f, i.w() - ml - mr));
        if (el ("slice." + (sliceKey.isNotEmpty() ? sliceKey : n), 1, 10) + el ("slice." + (sliceKey.isNotEmpty() ? sliceKey : n), 3, 12) <= 0.0f) k = juce::jmin (1.0f, r.getHeight() / juce::jmax (1.0f, i.h() - mt - mb));
        nine (g, n, { r.getX() - ml * k, r.getY() - mt * k, r.getWidth() + (ml + mr) * k, r.getHeight() + (mt + mb) * k }, alpha, sliceKey, k);
    }
    // a named slab from look.json "slabs.<role>" (9-slice, body on r); returns the sprite name ("" when the look has none for that role)
    juce::String slab (juce::Graphics& g, const juce::String& role, RF r, float alpha = 1.0f)
    { const auto n = str ("slabs." + role); if (n.isNotEmpty()) nineBody (g, n, r, alpha); return n; }

    // a panel a component draws under its controls: the named slab sprite (body on r), or, on a look with smoked glass
    // (look.json "glass"), the blurred picture seen through a rounded window with the glass frame on top
    juce::Image glassBlur;   // the page's blurred background at the window's size (the editor sets it at the top of each paint pass)
    void panel (juce::Graphics& g, const juce::Component& c, const juce::String& name, RF r, float alpha = 1.0f);
    // ---- text
    void text (juce::Graphics& g, const juce::String& s, RF r, juce::Justification j, float h, juce::Colour c, bool black = false) const;
    // small text straight on the painting: the look's ink with its halo (ASSETS.md "Text")
    void haloText (juce::Graphics& g, const juce::String& s, RF r, juce::Justification j, float h, juce::Colour c = {}, juce::Colour haloCol = {}) const;
    // text on a paper label sized to the text (centred on r's centre, or left-aligned at r's left)
    // (a look with no label sprite for the role draws the text alone, with its halo; "tag" falls back to "paper")
    void paperText (juce::Graphics& g, const juce::String& s, RF r, float h, bool leftAligned = false, const juce::String& role = "paper", juce::Colour inkOverride = {});

    // the look's accent sprite for a colour: "<prefix><name>" for the nearest accent the look has art for (lime, ice, pink, magenta,
    // orange, violet, gold, red). Ghost Box's slime counts as lime.
    juce::String accentSprite (const juce::String& prefix, juce::Colour c)
    {
        c = fxtheme::unmap (c); const juce::String key = prefix + juce::String::toHexString ((int) c.getARGB());
        if (auto it = accentCache.find (key); it != accentCache.end()) return it->second;
        struct A { const char* n; juce::uint32 argb; };
        static const A as[] { { "lime", 0xffa1e935 }, { "ice", 0xff53d6ea }, { "pink", 0xffed6ec7 }, { "magenta", 0xffff33b4 }, { "orange", 0xfff1824b }, { "violet", 0xffc29de7 }, { "gold", 0xfff7c245 }, { "red", 0xffff2944 } };
        if (c.getSaturation() < 0.18f && hasFile (prefix + "neutral")) return accentCache[key] = prefix + "neutral";   // (a grey control: the look's neutral art when it has one)
        float deg = c.getHue() * 360.0f; if (deg > 100.0f && deg < 165.0f) deg = 90.0f; const bool grey = c.getSaturation() < 0.18f;
        juce::String best; float bd = 1.0e9f;
        for (auto& a : as)
        {
            if (! hasImg (prefix + a.n)) continue;
            const float ah = colour (juce::String ("accents.") + a.n, juce::Colour (a.argb)).getHue() * 360.0f; float d = std::abs (ah - deg); d = juce::jmin (d, 360.0f - d);
            if (grey) d = juce::String (a.n) == "gold" ? 0.0f : juce::String (a.n) == "ice" ? 1.0f : 500.0f + d;
            if (d < bd) { bd = d; best = a.n; }
        }
        if (best.isEmpty() && hasFile (prefix + "neutral")) best = "neutral";
        return accentCache[key] = prefix + best;
    }
    // an accent as the painted looks show it: the unit's own hue in the look's tone (soft cel tones on DAWN / CEL, full neon on NEON)
    static juce::Colour accentColour (juce::Colour c);
    // the accent as glowing text (LED values, menu text on the looks that light them)
    static juce::Colour accentText (juce::Colour c);
    // one digit of a 12 fps table (look.json "anim.tables"): 9 = lit ... 0 = out. "period" longer than the table pads it with lit
    // ticks, so tables with different periods drift against each other (irregular flicker)
    int tableAt (const juce::String& table, int tick, int period = 0) const
    {
        const auto t = str ("anim.tables." + table); if (t.isEmpty()) return 9;
        const int n = juce::jmax (t.length(), period), i = ((tick % n) + n) % n; return i < t.length() ? juce::jlimit (0, 9, (int) (t[i] - '0')) : 9;
    }
    juce::ZipFile zip;
    // the unit visuals' parts (visual/<folder>/manifest.json + pictures), read on first use
    const phantom::pv::SpriteBank& visualBank()
    {
        if (pvBank) return *pvBank;
        pvBank = std::make_unique<phantom::pv::SpriteBank>();
        for (int i = 0; i < zip.getNumEntries(); ++i)
        {
            const auto* e = zip.getEntry (i); if (e == nullptr || ! e->filename.startsWith ("visual/") || ! e->filename.endsWith ("/manifest.json")) continue;
            std::unique_ptr<juce::InputStream> st (zip.createStreamForEntry (i)); if (st == nullptr) continue;
            const auto m = juce::JSON::parse (st->readEntireStreamAsString()); const float pps = (float) (double) m.getProperty ("px_per_su", 2.0);
            const juce::String folder = m.getProperty ("folder", "").toString();
            if (auto* fs = m.getProperty ("files", {}).getArray()) for (auto& f : *fs)
            {
                const juce::String file = f.getProperty ("file", "").toString(), base = file.upToFirstOccurrenceOf ("@", false, false);
                auto arr = [&] (const char* k, int j, float d) { if (auto* a = f.getProperty (k, {}).getArray()) if (j < a->size()) return (float) (double) (*a)[j]; return d; };
                phantom::pv::Sprite sp; const auto& im = img ("visual/" + folder + "/" + base); if (! im.ok()) continue;
                sp.im = im.im; sp.frames = juce::jmax (1, (int) f.getProperty ("frames", 1)); sp.pw = (int) arr ("cell", 0, (float) sp.im.getWidth() / (float) sp.frames); sp.ph = (int) arr ("cell", 1, (float) sp.im.getHeight());
                sp.w = arr ("su", 0, 0); sp.h = arr ("su", 1, 0); sp.ax = arr ("anchor_su", 0, 0); sp.ay = arr ("anchor_su", 1, 0);
                sp.pxPerSu = pps; sp.drawScale = (float) (double) f.getProperty ("draw_scale", 1.0); sp.mask = f.getProperty ("kind", "").toString() == "mask";
                if (auto* zs = f.getProperty ("sizes", {}).getArray()) for (auto& z : *zs) sp.sizes.push_back ((float) (double) z);
                pvBank->sprites[(folder == "_shared" ? base : folder + "/" + base).toStdString()] = sp;
                cache.erase ("visual/" + folder + "/" + base);
            }
        }
        return *pvBank;
    }
    bool hasVisuals() const { return cfg.hasProperty ("visual"); }
private:
    std::unique_ptr<phantom::pv::SpriteBank> pvBank;
    std::map<juce::String, Img> cache; std::map<juce::String, juce::String> accentCache; std::map<juce::String, juce::Image> sizedCache; juce::int64 sizedPx = 0;
};

// every embedded look + the bundled typeface; shared by all open editors (juce::SharedResourcePointer), freed with the last one
struct Registry
{
    std::vector<std::unique_ptr<Look>> looks; juce::Typeface::Ptr bold, black;
    Registry()
    {
        for (int i = 0; i < PhantomLooksData::namedResourceListSize; ++i)
        {
            const char* res = PhantomLooksData::namedResourceList[i]; const juce::String file (PhantomLooksData::getNamedResourceOriginalFilename (res));
            int size = 0; const char* data = PhantomLooksData::getNamedResource (res, size);
            if (file.startsWith ("look-") && file.endsWith (".zip")) looks.push_back (std::make_unique<Look> (file.fromFirstOccurrenceOf ("look-", false, false).upToLastOccurrenceOf (".zip", false, false), data, (size_t) size));
            else if (file == "ZenMaruGothic-Bold.ttf") bold = juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
            else if (file == "ZenMaruGothic-Black.ttf") black = juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
        }
        std::sort (looks.begin(), looks.end(), [] (auto& a, auto& b) { return a->order != b->order ? a->order < b->order : a->id < b->id; });
        while ((int) looks.size() > fxtheme::kMaxLooks - fxtheme::kFirstPainted) looks.pop_back();
        const Look* cel = nullptr; for (auto& l : looks) if (l->id == "cel") cel = l.get();
        auto& pals = fxtheme::paintedPalettes(); pals.clear();
        for (auto& l : looks)
        {
            l->fallback = cel;
            const bool light = (bool) l->at ("palette.light"); auto p = ghosty::themes::makePalette (light ? ghosty::themes::Theme::Light : ghosty::themes::Theme::Dark);
            auto c = [&] (const char* k, juce::Colour d) { return l->colour (juce::String ("palette.") + k, d); };
            p.window = c ("window", p.window); p.windowTop = p.windowBottom = p.window; p.panel = c ("panel", p.panel); p.card = c ("card", p.card); p.cell = c ("cell", p.cell);
            p.line = c ("line", p.line); p.ink = c ("ink", p.ink); p.dim = c ("dim", p.dim); p.overlay = p.ink; p.keyWhite = p.card; p.keyBlack = p.ink; p.waveBg = p.cell;
            pals.push_back (p);
        }
        instance() = this;
    }
    ~Registry() { instance() = nullptr; ghosty::fontOverride() = nullptr; fxtheme::paintedPalettes().clear(); if (fxtheme::look() >= fxtheme::kFirstPainted) fxtheme::setLook (1); }
    static Registry*& instance() { static Registry* r = nullptr; return r; }
    int count() const { return (int) looks.size(); }
    int indexOf (const juce::String& lookId) const { for (int i = 0; i < count(); ++i) if (looks[(size_t) i]->id == lookId) return i; return -1; }
};

// the painted look being painted now (nullptr on BLACK / PURPLE / LIGHT / GHOST)
inline Look* active()
{
    auto* r = Registry::instance(); const int l = fxtheme::look() - fxtheme::kFirstPainted;
    return r != nullptr && l >= 0 && l < r->count() ? r->looks[(size_t) l].get() : nullptr;
}
inline juce::Font pfont (float h, bool black = false)
{
    if (auto* L = active(); L != nullptr && L->bold != nullptr) return juce::Font (juce::FontOptions (black ? L->black : L->bold).withHeight (h * L->em * L->fontSize));
    auto* r = Registry::instance(); auto t = r == nullptr ? juce::Typeface::Ptr() : (black && r->black != nullptr ? r->black : r->bold);
    return t != nullptr ? juce::Font (juce::FontOptions (t).withHeight (h * kEm)) : juce::Font (juce::FontOptions (h, juce::Font::bold));
}
// the typeface follows the look (ghosty::font is the one font source of the rack UI)
inline void syncFont()
{
    auto* r = Registry::instance(); auto* L = active();
    if (r == nullptr || L == nullptr) { ghosty::fontOverride() = nullptr; return; }
    ghosty::fontOverride() = L->bold != nullptr ? L->bold : r->bold; ghosty::fontOverrideScale() = (L->bold != nullptr ? L->em * L->fontSize : kEm) / 1.164f;
}
inline juce::Colour Look::accentColour (juce::Colour c)
{
    c = fxtheme::unmap (c); auto* L = active();
    const float s0 = L ? L->num ("accent.satMin", 0.50f) : 0.50f, s1 = L ? L->num ("accent.satMax", 0.74f) : 0.74f, v0 = L ? L->num ("accent.valMin", 0.86f) : 0.86f, v1 = L ? L->num ("accent.valMax", 0.94f) : 0.94f;
    if (c.getSaturation() < 0.18f) return L != nullptr ? L->colour ("accents.gold", juce::Colour (0xfff7c245)) : juce::Colour (0xfff7c245);
    return juce::Colour::fromHSV (c.getHue(), juce::jlimit (s0, s1, c.getSaturation()), juce::jlimit (v0, v1, c.getBrightness()), 1.0f);
}
inline juce::Colour Look::accentText (juce::Colour c)
{
    auto* L = active(); const auto a = accentColour (c);
    return juce::Colour::fromHSL (a.getHue(), 1.0f, L ? L->num ("accent.lightness", 0.8f) : 0.8f, 1.0f);
}

inline void Look::text (juce::Graphics& g, const juce::String& s, RF r, juce::Justification j, float h, juce::Colour c, bool black) const
{ g.setColour (c); g.setFont (pfont (h, black)); g.drawText (s, r, j, false); }
inline void Look::haloText (juce::Graphics& g, const juce::String& s, RF r, juce::Justification j, float h, juce::Colour c, juce::Colour haloCol) const
{
    if (s.isEmpty()) return;
    g.setFont (pfont (h)); g.setColour (haloCol.isTransparent() ? halo() : haloCol);
    static const float o[4][2] { { -1.1f, -1.1f }, { 1.1f, -1.1f }, { -1.1f, 1.1f }, { 1.1f, 1.1f } };   // (four copies, not eight: text is the dearest thing to draw)
    for (auto& d : o) g.drawText (s, r.translated (d[0], d[1]), j, false);
    g.setColour (c.isTransparent() ? onPainting() : c); g.drawText (s, r, j, false);
}
inline void Look::paperText (juce::Graphics& g, const juce::String& s, RF r, float h, bool leftAligned, const juce::String& role, juce::Colour inkOverride)
{
    auto n = str ("slabs." + role); if (n.isEmpty() && role == "tag") n = str ("slabs.paper");
    if (n.isEmpty()) { haloText (g, s, r, leftAligned ? juce::Justification::centredLeft : juce::Justification::centred, h, inkOverride); return; }
    const auto f = pfont (h); const float tw = juce::GlyphArrangement::getStringWidth (f, s) + h * 1.5f, w = juce::jmin (r.getWidth(), tw);
    const RF p = leftAligned ? r.withWidth (w) : r.withSizeKeepingCentre (w, r.getHeight());
    nineBody (g, n, p);
    g.setColour (inkOverride.isTransparent() ? inkOn (n) : inkOverride); g.setFont (f); g.drawFittedText (s, p.reduced (h * 0.45f, 0).toNearestInt(), juce::Justification::centred, 1, 0.7f);
}

// ---- design space: LAYOUT.md rectangles (1200 x 760) -> a component's own pixels at the editor's current size
struct Map
{
    float sx = 1.0f, sy = 1.0f; juce::Point<float> o;
    Map() = default;
    explicit Map (const juce::Component& c)
    {
        const juce::Component* top = &c; while (top->getParentComponent() != nullptr && dynamic_cast<const juce::AudioProcessorEditor*> (top) == nullptr) top = top->getParentComponent();
        sx = (float) juce::jmax (1, top->getWidth()) / kW; sy = (float) juce::jmax (1, top->getHeight()) / kH;
        o = top == &c ? juce::Point<float>() : top->getLocalPoint (&c, juce::Point<float>());
    }
    float s() const { return juce::jmin (sx, sy); }
    RF f (float x, float y, float w, float h) const { return { x * sx - o.x, y * sy - o.y, w * sx, h * sy }; }
    RF f (RF r) const { return f (r.getX(), r.getY(), r.getWidth(), r.getHeight()); }
    RI r (float x, float y, float w, float h) const { return f (x, y, w, h).toNearestInt(); }
    RI r (RF d) const { return f (d).toNearestInt(); }
    juce::Point<float> p (float x, float y) const { return { x * sx - o.x, y * sy - o.y }; }
    // a w x h (design px) box that keeps its shape (uniform scale), centred on the design point
    RF sq (float cx, float cy, float w, float h) const { const float k = s(); return RF (w * k, h * k).withCentre (p (cx, cy)); }
};

inline void Look::panel (juce::Graphics& g, const juce::Component& c, const juce::String& name, RF r, float alpha)
{
    if (name.isEmpty() || r.isEmpty()) return;
    if (glassBlur.isValid() && listHas ("glass.replaces", name))
    {
        const Map m (c); const float rad = juce::jmin (num ("glass.radius", 8.0f) * m.s(), r.getHeight() * 0.3f);
        { juce::Graphics::ScopedSaveState ss (g); juce::Path p; p.addRoundedRectangle (r, rad); g.reduceClipRegion (p); g.setOpacity (alpha); g.drawImage (glassBlur, RF (-m.o.x, -m.o.y, m.sx * kW, m.sy * kH)); g.setOpacity (1.0f); }
        nineBody (g, str ("glass.frame"), r, alpha); return;
    }
    nineBody (g, name, r, alpha);
}

// ---- the 12 fps tables of ASSETS.md "Animation"
inline int bounceFrame (int tick) { static const int f[42] { 0,0,0,0,0,0,0,0,0,0,1,1,2,2,2,2,2,3,3,0,0,0,0,0,0,0,0,0,0,0,0,1,1,2,2,2,2,2,3,3,0,0 }; return f[((tick % 42) + 42) % 42]; }
inline float lampAlpha (int tick) { static const float a[14] { .75f, .9f, .6f, .95f, .8f, .55f, .9f, .7f, 1.0f, .65f, .85f, .6f, .9f, .75f }; return a[((tick / 3) % 14 + 14) % 14]; }
// one bounce after a trigger: squash, stretch, settle held on twos (the strip's frames 1 1 2 2 2 2 2 3 3), then listening again
inline int bounceAfter (int ticksSinceTrigger) { static const int f[9] { 1, 1, 2, 2, 2, 2, 2, 3, 3 }; return ticksSinceTrigger >= 0 && ticksSinceTrigger < 9 ? f[ticksSinceTrigger] : 0; }
// a unit sprite has one drawing: its key poses are made in code as held squash / stretch steps (x scale, y scale, lift in body heights)
inline void poseOf (int frame, float& kx, float& ky, float& lift)
{
    static const float t[4][3] { { 1.0f, 1.0f, 0.0f }, { 1.10f, 0.86f, -0.07f }, { 0.93f, 1.10f, 0.10f }, { 1.03f, 0.97f, 0.0f } };
    kx = t[frame & 3][0]; ky = t[frame & 3][1]; lift = t[frame & 3][2];
}
// drawn pose strips (six held frames: idle A, idle B, squat, hit, settle, asleep). After a level peak: squat for 2 ticks, hit for 3,
// settle for 2; otherwise the two idle drawings take turns, each held for 9 ticks (every ghost on its own count)
inline constexpr int kPoseFrames = 6, kPoseAsleep = 5;
inline int poseAfter (int ticksSinceTrigger, int tick, int seed) { if (ticksSinceTrigger >= 0 && ticksSinceTrigger < 7) return ticksSinceTrigger < 2 ? 2 : ticksSinceTrigger < 5 ? 3 : 4; return ((tick + seed * 4) / 9) & 1; }
// (a look with no pose strip: the same pose as one of the four squash / stretch steps)
inline int squashOf (int pose) { return pose == 2 ? 1 : pose == 3 ? 2 : pose == 4 ? 3 : 0; }
struct Leaf { RF r; int frame = 0; };
inline Leaf leafAt (int i, int tick)
{
    static const float x0[7] { 840, 900, 1120, 300, 620, 1180, 60 };
    const int tk = (tick / 2) * 2; float ph = (float) i * 0.137f + (float) tk / 42.0f; ph -= std::floor (ph);
    Leaf l; l.frame = ((int) std::floor (ph * 14.0f + (float) i)) % 8;
    l.r = RF (28.0f, 28.0f).withCentre ({ x0[i % 7] + std::sin (ph * 9.42f + (float) i) * 26.0f - ph * 40.0f, 60.0f + ph * 560.0f });
    return l;
}
} // namespace painted
