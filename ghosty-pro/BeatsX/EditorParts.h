#pragma once
// Ghosty Beats editor pieces: palette + look, knobs, the step grid, lane editors, track list, step editor popup,
// drag-out handles and the sound browser. Every edit goes through Ed::edit() (one undo step + publish).
#include "BeatsXProcessor.h"
#include "GhostArt.h"
#include "GhostGlass.h"
#include "GhostyAssets.h"
#include "SumiPaper.h"   // LIGHT look: ink + watercolour on washi
#include <optional>

namespace bx {

// The palette: the GhostBlock house themes (Shared/GhostThemes.h): BLACK (Dark), PURPLE, LIGHT (soft lilac, never pure white).
// applyTheme() copies the theme's tokens into the names this editor paints with (read at paint time, so applyTheme() + a repaint switches
// everything). Surfaces are the tokens as they are (all opaque): BG = window, CARD = panel, CARD2 = cell, POP = card (pop-ups, tooltips,
// call-outs, the RE-CREATE panel), DIM = line, INK / GREY = ink / dim (>= 4.5:1 on every surface), ON = text on a filled accent.
// Accents = the theme's accent tokens; two hues the house palette has no token for keep Beats' own (BLUE, and a mint GREEN distinct from
// the LIME accent) because they tell the 8 track colours apart. Every accent is made readable (>= 4.5:1) on BG, CARD and CARD2 with
// ghosty::themes::readableOn, so accent text and the track colours read in all three looks.
namespace th = ghosty::themes;
inline th::Theme THEME = th::Theme::Dark;
inline juce::Colour BG, CARD, CARD2, POP, INK, GREY, DIM, ON, SHADOW, PURPLE, VIOLET, GREEN, PINK, AMBER, BLUE, RED, CYAN, LIME;
inline bool isDark = true;   // BLACK or PURPLE (a dark background); false = LIGHT
inline bool GLASS = false;                    // the GHOST look is on (see applyLook)
inline bool NEON = false;                     // the NEON look is on (see applyLook)
inline bool SUMI = false;                     // the LIGHT look (ink + watercolour on washi paper) is on (see applyLook)
inline int LOOK = 0;                          // 0 BLACK, 1 PURPLE, 2 LIGHT, 3 GHOST (the stored index)
inline juce::Colour readable(juce::Colour c) { return th::readableOn(th::readableOn(th::readableOn(th::readableOn(th::readableOn(c, CARD2, 4.5), CARD, 4.5), BG, 4.5), CARD2, 4.5), ON, 4.7); }   // (and ON reads on it as a fill)
inline void applyTheme(th::Theme t) {
    const auto& P = th::get(t); THEME = t; isDark = !P.isLight; GLASS = false; NEON = false; SUMI = false; LOOK = th::toInt(t);
    BG = P.window; CARD = P.panel; CARD2 = P.cell; POP = P.card; INK = P.ink; GREY = P.dim; DIM = P.line; ON = P.onAccent; SHADOW = P.shadow;
    PURPLE = readable(P.violet); VIOLET = PURPLE; PINK = readable(P.accent1); CYAN = readable(P.accent2); LIME = readable(P.accent3);
    AMBER = readable(P.warn); RED = readable(P.bad);
    GREEN = readable(P.isLight ? juce::Colour(0xff0B7F46) : juce::Colour(0xff3CFF8F));   // (own hue: mint, the first track's colour)
    BLUE = readable(P.isLight ? juce::Colour(0xff2358D6) : juce::Colour(0xff60A5FA));    // (own hue: no blue token)
}
inline void applyTheme(bool dark) { applyTheme(dark ? th::Theme::Dark : th::Theme::Light); }   // (older callers: dark / light)
// GHOST (Beats-only 4th look, after BLACK / PURPLE / LIGHT): liquid glass on the LIGHT base (Shared/GhostGlass.h). THEME stays Light (the shared
// buttons, the wordmark and the mascot draw their LIGHT forms); GLASS switches the glass painting on. The flat tokens below = the frosted glass as it
// reads over the aurora (contrast maths + every flat fill). The shared Theme enum is untouched: no other plugin changes.
// NEON (Beats-only 5th look, owner 2026-11-11): a faded, hand-painted 1995 cyberpunk city at dusk in the rain. Muted petroleum teal / olive /
// concrete grey / dirty amber over deep charcoal, neon that bleeds instead of glares, slightly lifted blacks, film grain. THEME stays Dark (the
// shared buttons, the wordmark and the mascot draw their dark forms); NEON switches the city backdrop + the faded tokens on. BLACK / PURPLE /
// LIGHT / GHOST are untouched.
enum { kLookGhost = 3, kLookNeon = 4, kLooks = 5 };
// Brandon 2026-11-13: Ghosty Beats keeps TWO looks only: NEON (the rainy 1995 city) and LIGHT (sumi-e ink + watercolour on washi paper,
// SumiPaper.h). BLACK, PURPLE, the old lilac LIGHT and GHOST are gone: anything else (an old saved look) opens as NEON.
enum { kLookLight = 2 };
inline int sanitizeLook(int l) { return l == kLookLight ? kLookLight : kLookNeon; }
inline int nextLook(int l) { return sanitizeLook(l) == kLookNeon ? kLookLight : kLookNeon; }
inline juce::String lookName(int l) { return sanitizeLook(l) == kLookLight ? juce::String("LIGHT") : juce::String("NEON"); }
namespace neon {   // the NEON look's own colours (the city backdrop paints with these too)
    using C = juce::Colour;
    inline C night()    { return C(0xff0a1112); }   // deep charcoal with a teal cast (never pure black: the faded print)
    inline C smog()     { return C(0xff2a3a37); }   // the lit smog low in the sky
    inline C teal()     { return C(0xff4fae9f); }   // petroleum teal
    inline C olive()    { return C(0xff8e9a5c); }   // olive
    inline C amber()    { return C(0xffd29440); }   // dirty sodium amber
    inline C magenta()  { return C(0xffc6587e); }   // a tired pink neon tube
    inline C concrete() { return C(0xff6f7a74); }   // weathered concrete grey
    inline C paper()    { return C(0xffd9dccb); }   // warm faded off-white (the ink)
}
inline void applyLook(int look) {
    look = sanitizeLook(look); NEON = false; SUMI = false;
    if (look == kLookNeon) {
        applyTheme(th::Theme::Dark); NEON = true; LOOK = kLookNeon;
        using C = juce::Colour;
        BG = C(0xff0c1415); CARD = C(0xff121c1d); CARD2 = C(0xff1a2627); POP = C(0xff1e2b2b); DIM = C(0xff34443f); SHADOW = C(0xaa000000);
        INK = neon::paper(); GREY = C(0xff96a08f); ON = C(0xff0b1112);
        PURPLE = readable(neon::teal()); VIOLET = PURPLE; GREEN = readable(neon::olive().brighter(0.25f)); PINK = readable(neon::magenta());
        AMBER = readable(neon::amber()); BLUE = readable(C(0xff6c93a8)); RED = readable(C(0xffc85a44)); CYAN = readable(C(0xff6cc3c0)); LIME = readable(C(0xffb3b866));
        return;
    }
    if (look == kLookLight) {   // LIGHT: warm washi paper, charcoal ink, muted vermilion / indigo / moss / blossom (THEME stays Light: the shared widgets draw their light forms)
        applyTheme(th::Theme::Light); SUMI = true; LOOK = kLookLight;
        using C = juce::Colour;
        // (Brandon 2026-11-14: less blinding: the paper toned down to a soft, warm parchment; text and outlines darker so everything reads clearly)
        BG = C(0xffD6CCB8); CARD = C(0xffDDD4C2); CARD2 = C(0xffCFC4AE); POP = C(0xffE2DACA); DIM = C(0xffA39780); SHADOW = C(0x40261C10);
        INK = C(0xff15130F); GREY = C(0xff463F35); ON = C(0xffF2ECE0);
        PURPLE = readable(C(0xff34507F)); VIOLET = PURPLE; RED = readable(C(0xffC84B31)); GREEN = readable(C(0xff5E7A3A)); PINK = readable(C(0xffB65A6C));
        AMBER = readable(C(0xffA8742A)); BLUE = readable(C(0xff3E6A9A)); CYAN = readable(C(0xff3D7A73)); LIME = readable(C(0xff737E2C));
        return;
    }
    if (look != kLookGhost) { applyTheme(th::fromInt(look)); return; }
    applyTheme(th::Theme::Light); GLASS = true; LOOK = kLookGhost;
    namespace gg = ghosty::glass; using C = juce::Colour;
    BG = C(0xffebe6f8); CARD = C(0xfff0edf9); CARD2 = C(0xffe6e1f4); POP = C(0xfff7f5fd); INK = gg::ink(); GREY = gg::ink2(); DIM = C(0xffb8afd8); ON = C(0xffffffff);
    const auto& P = th::get(th::Theme::Light);
    auto rd = [](juce::Colour c) { return readable(th::readableOn(c, juce::Colour(0xffd0c4da), 4.6)); };   // (also on the darkest glass: a card over the pink aurora orb)
    PURPLE = rd(gg::violetDeep()); VIOLET = PURPLE; PINK = rd(P.accent1); CYAN = rd(P.accent2); LIME = rd(P.accent3);
    AMBER = rd(P.warn); RED = rd(P.bad); GREEN = rd(juce::Colour(0xff0B7F46)); BLUE = rd(juce::Colour(0xff2358D6));
}
struct ThemeInit { ThemeInit() { applyTheme(th::Theme::Dark); } };
inline ThemeInit themeInit;   // the globals hold a valid palette before any editor exists
constexpr int W = 1180, H = 790;
constexpr int kOpenW = 1200, kOpenH = 760;   // the size the window opens at (fits FL's window on a 1440 x 900 MacBook Pro)
inline juce::Colour trackColour(int t) { const juce::Colour C[] = { GREEN, PURPLE, PINK, AMBER, BLUE, CYAN, RED, LIME }; return C[(size_t) (t % 8)]; }
// the ghosty wordmark in the current palette (the shared one is drawn for a dark background)
inline void drawWordmark(juce::Graphics& g, juce::Rectangle<float> r, const juce::String& tag, float glitch) {
    const auto f = juce::Font(juce::FontOptions(r.getHeight() * 0.8f, juce::Font::bold)); g.setFont(f);
    juce::GlyphArrangement ga; ga.addLineOfText(f, "ghost", 0, 0); const float w = ga.getBoundingBox(0, -1, true).getWidth();
    auto drawText = [&](float dx, juce::Colour a, juce::Colour b) { auto rr = r.translated(dx, 0); g.setColour(a); g.drawText("ghost", rr, juce::Justification::centredLeft); g.setColour(b); g.drawText("y", rr.withTrimmedLeft(w), juce::Justification::centredLeft); };
    if (glitch > 0.05f) { drawText(-3.0f * glitch, GREEN.withAlpha(0.7f), GREEN.withAlpha(0.7f)); drawText(3.0f * glitch, PURPLE.withAlpha(0.7f), PURPLE.withAlpha(0.7f)); }
    drawText(0, INK, GREEN);
    if (tag.isNotEmpty()) { juce::GlyphArrangement gb2; gb2.addLineOfText(f, "ghosty", 0, 0); auto tr = juce::Rectangle<float>(r.getX() + gb2.getBoundingBox(0, -1, true).getWidth() + 10.0f, r.getCentreY() - 9, 70, 18);
        g.setColour(PURPLE); g.drawRoundedRectangle(tr, 5, 1.2f); g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold))); g.drawText(tag, tr, juce::Justification::centred); }
}
inline juce::Colour patColour(int p) { return trackColour(p * 3 + 1); }
inline juce::String patName(int p) { return juce::String::charToString((juce::juce_wchar) ('A' + p)); }
inline juce::Font font(float h, bool bold = true) { return juce::FontOptions(h, bold ? juce::Font::bold : juce::Font::plain); }
// GHOSTY BEATS' pixel font (Press Start 2P, OFL; BeatsX/fonts): buttons, tabs and the pattern letters, drawn 80 % wide (its square
// letters are wide; narrowed they stay crisp and every tab fits). Loaded once from the binary data.
inline juce::Typeface::Ptr pixelTypeface() { static juce::Typeface::Ptr* const tf = new juce::Typeface::Ptr (juce::Typeface::createSystemTypefaceFor(GhostyAssets::PressStart2PRegular_ttf, (size_t) GhostyAssets::PressStart2PRegular_ttfSize)); return *tf; }   // never freed on purpose: a static typeface destroyed at host quit crashes FL on close
inline juce::Font pixel(float h, float wide = 0.8f) { return juce::Font(juce::FontOptions(pixelTypeface()).withHeight(h).withHorizontalScale(wide)); }
// the label in the pixel font when it fits the box at >= 7.5 px (80 % wide, then 70 %; else nullopt: the caller keeps the regular
// bold font, nothing is cut off)
inline std::optional<juce::Font> pixelFit(const juce::String& text, juce::Rectangle<int> box, float h) {
    for (float wide : { 0.8f, 0.7f })
        for (float ph = h; ph >= 7.5f; ph -= 0.5f) { const auto f = pixel(ph, wide); if (ghosty::textWidth(f, text) <= (float) box.getWidth() - 2.0f && ph <= (float) box.getHeight()) return f; }
    return std::nullopt;
}
// GHOSTY BEATS logo: the owner's artwork (BeatsX/art/ghosty_beats_logo.png, lettering + headphones ghost + version), drawn as is.
// The 2106 px original is pre-shrunk by halving steps (no aliasing) to the size on screen and cached per pixel size: the header repaints often.
inline const juce::Image& logoArt() { static const juce::Image im = juce::ImageCache::getFromMemory(GhostyAssets::ghosty_beats_logo_png, GhostyAssets::ghosty_beats_logo_pngSize); return im; }
// LIGHT (Brandon 2026-11-14): the same artwork faded like a print on rice paper: colours muted toward the paper, the black outlines softened to
// a warm charcoal, the dark glow made see-through, so it sits lightly on the page instead of shouting.
inline const juce::Image& logoArtLight() {
    static const juce::Image im = [] {
        juce::Image src = logoArt(); if (! src.isValid()) return src;
        juce::Image o = src.convertedToFormat(juce::Image::ARGB); o.duplicateIfShared();
        juce::Image::BitmapData bd(o, juce::Image::BitmapData::readWrite); const juce::Colour paper(0xffE6DECE), ink(0xff2E2722);
        for (int y = 0; y < bd.height; ++y) for (int x = 0; x < bd.width; ++x) {
            const auto c = bd.getPixelColour(x, y); const float a = c.getFloatAlpha(); if (a <= 0.0f) continue;
            const float lum = 0.3f * c.getFloatRed() + 0.59f * c.getFloatGreen() + 0.11f * c.getFloatBlue();
            auto m = c.withAlpha(1.0f).withMultipliedSaturation(0.7f);
            m = lum < 0.18f ? ink.interpolatedWith(m, lum / 0.18f * 0.3f)            // outlines -> soft charcoal
                            : m.interpolatedWith(paper, 0.22f);                      // fills + glow -> pale, washed toward the paper
            const float glow = lum < 0.45f && c.getSaturation() > 0.4f ? 0.45f : 1.0f;   // the dark purple/green glow: half see-through
            bd.setPixelColour(x, y, m.withAlpha(a * glow));
        }
        return o; }();
    return im;
}
inline void drawLogo(juce::Graphics& g, juce::Rectangle<float> r, bool light = false) {
    const auto& src = light ? logoArtLight() : logoArt(); if (! src.isValid()) return;
    const auto fit = juce::RectanglePlacement(juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yTop).appliedTo(src.getBounds().toFloat(), r);
    const float px = g.getInternalContext().getPhysicalPixelScaleFactor();
    const int tw = juce::jmax(8, juce::roundToInt(fit.getWidth() * px)), th = juce::jmax(4, juce::roundToInt(fit.getHeight() * px));
    struct Slot { int w = 0, h = 0; bool light = false; juce::Image im; }; static Slot slots[3]; static int next = 0;   // the header, the about card, one spare
    Slot* hit = nullptr; for (auto& sl : slots) if (sl.w == tw && sl.h == th && sl.light == light && sl.im.isValid()) hit = &sl;
    if (hit == nullptr) {
        juce::Image im = src;
        while (im.getWidth() >= tw * 2) im = im.rescaled(im.getWidth() / 2, im.getHeight() / 2, juce::Graphics::highResamplingQuality);
        hit = &slots[next]; next = (next + 1) % 3; hit->im = im.rescaled(tw, th, juce::Graphics::highResamplingQuality); hit->w = tw; hit->h = th; hit->light = light;
    }
    g.setOpacity(1.0f); g.drawImage(hit->im, fit, juce::RectanglePlacement::stretchToFit);   // (drawImage takes the current colour's alpha)
}
inline juce::Font mono(float h) { return juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), h, juce::Font::plain); }

// ---------------- GHOST: glass pieces. Small shapes the editor paints many times (step cells, rows, pads, pills, knob faces) are rendered ONCE
// per (kind, tint, size, pixel scale) into an image and blitted afterwards: the dense 16-track grid repaints at 30 Hz. Message thread only.
namespace glassui {
namespace gg = ghosty::glass;
enum Kind { Cell = 0, Lit, Row, Pad, PillOff, PillOffOver, PillOn, PillOnOver, KnobFace, Well, Strip };
inline void paintKind(juce::Graphics& g, int kind, juce::Colour tint, juce::Rectangle<float> r, float rad) {
    switch (kind) {
        case Cell:  gg::shadow(g, r, rad, 1.6f, 0.7f, 2); gg::body(g, r, rad, juce::Colours::white, 0.34f, 0.8f); break;
        case Lit: {   // a lit step: the track colour made vivid (the deep text-safe token would read dull under glass), glossy
            const auto v = juce::Colour::fromHSV(tint.getHue(), juce::jmin(1.0f, tint.getSaturation() * 1.15f + 0.05f), juce::jmin(1.0f, tint.getBrightness() * 1.3f + 0.08f), 1.0f);
            gg::shadow(g, r, rad, 2.4f, 1.2f, 3); g.setColour(v.withAlpha(0.35f)); g.fillRoundedRectangle(r.expanded(1.0f), rad + 1.0f); gg::body(g, r, rad, v, 0.95f, 0.38f); break; }
        case Row:   gg::body(g, r, rad, juce::Colours::white, 0.36f, 0.7f); break;
        case Pad:   gg::shadow(g, r, rad, 5.0f, 1.0f, 4); gg::body(g, r, rad, juce::Colours::white, 0.40f); break;
        case PillOff: case PillOffOver: gg::shadow(g, r, rad, 2.6f, 1.3f, 3); gg::body(g, r, rad, juce::Colours::white, kind == PillOffOver ? 0.52f : 0.40f); break;
        case PillOn:  case PillOnOver:  gg::shadow(g, r, rad, 2.6f, 1.3f, 3); gg::body(g, r, rad, tint, kind == PillOnOver ? 0.98f : 0.95f, 0.3f); break;
        case KnobFace: {   // a glossy knob: soft shadow, the glass ring the arc runs in, a pearl dome
            const float d = juce::jmin(r.getWidth(), r.getHeight()); auto c = r.withSizeKeepingCentre(d, d);
            gg::shadow(g, c.reduced(d * 0.16f), d * 0.34f, juce::jmax(2.0f, d * 0.08f), 1.0f, 4);
            auto dome = c.reduced(d * 0.17f);
            g.setGradientFill(juce::ColourGradient(juce::Colours::white, dome.getX() + dome.getWidth() * 0.3f, dome.getY(), juce::Colour(0xffd9d0f3), dome.getCentreX(), dome.getBottom(), false)); g.fillEllipse(dome);
            g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.85f), dome.getCentreX(), dome.getY() + 1, juce::Colour(0x00ffffff), dome.getCentreX(), dome.getCentreY(), false));
            g.fillEllipse(dome.reduced(dome.getWidth() * 0.12f, 0).withHeight(dome.getHeight() * 0.5f).translated(0, 1.5f));
            g.setColour(juce::Colour(0xb0ffffff)); g.drawEllipse(dome.reduced(0.6f), 1.2f);
            g.setColour(juce::Colour(0x50a06bff)); g.drawEllipse(dome.reduced(dome.getWidth() * 0.1f), 0.8f);
            break; }
        case Well:  gg::recessed(g, r, rad); break;
        case Strip: gg::body(g, r, rad, juce::Colours::white, 0.30f, 0.8f); break;
        default: break;
    }
}
struct Sprites {
    int builds = 0;
    void draw(juce::Graphics& g, juce::Rectangle<float> r, float rad, int kind, juce::Colour tint = juce::Colours::white) {
        if (r.getWidth() < 1.0f || r.getHeight() < 1.0f) return;
        const float scale = juce::jlimit(1.0f, 4.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        const int m = 6; const int qw = juce::roundToInt(r.getWidth() * 4), qh = juce::roundToInt(r.getHeight() * 4);
        const Key k { kind, (juce::int64) tint.getARGB(), qw, qh, juce::roundToInt(rad * 4), juce::roundToInt(scale * 100) };
        auto it = map.find(k);
        if (it == map.end()) {
            if (map.size() > 600) map.clear();
            const float w = (float) qw / 4.0f, h = (float) qh / 4.0f;
            juce::Image img(juce::Image::ARGB, juce::jmax(1, (int) std::ceil((w + 2 * m) * scale)), juce::jmax(1, (int) std::ceil((h + 2 * m) * scale)), true, juce::SoftwareImageType());
            { juce::Graphics ig(img); ig.addTransform(juce::AffineTransform::scale(scale)); paintKind(ig, kind, tint, { (float) m, (float) m, w, h }, juce::jmin(rad, w * 0.5f, h * 0.5f)); }
            it = map.emplace(k, img).first; ++builds;
        }
        g.setOpacity(1.0f);   // (an image blit takes the current colour's alpha)
        g.drawImageTransformed(it->second, juce::AffineTransform::scale(1.0f / scale).translated(std::round(r.getX()) - (float) m, std::round(r.getY()) - (float) m));   // (whole pixels: the fast blit)
    }
private:
    using Key = std::tuple<int, juce::int64, int, int, int, int>;
    std::map<Key, juce::Image> map;
};
inline Sprites& sprites() { static Sprites s; return s; }
// a pill button / chip inside its own bounds; on = the tint as glass (ON text reads on it)
inline void pill(juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour tint, bool on, bool over, bool down, bool enabled = true, float maxRad = 1.0e6f) {
    auto r = gg::pillBody(bounds); if (down) r = r.translated(0, 0.6f);
    const float rad = juce::jmin(maxRad, r.getHeight() * 0.5f);
    sprites().draw(g, r, rad, on ? (over ? PillOnOver : PillOn) : (over ? PillOffOver : PillOff), on ? tint : juce::Colours::white);
    if (!on && over) { g.setColour(tint.withAlpha(0.55f)); g.drawRoundedRectangle(r.reduced(0.5f), rad, 1.0f); }
    if (!enabled) { g.setColour(juce::Colours::white.withAlpha(0.28f)); g.fillRoundedRectangle(r, rad); }
}
// the violet -> teal value fill (knob arcs, bars, meters)
inline juce::ColourGradient arcFill(juce::Rectangle<float> c) { return juce::ColourGradient(gg::violet(), c.getX(), c.getBottom(), gg::teal(), c.getRight(), c.getY(), false); }
} // namespace glassui

struct LnF : juce::LookAndFeel_V4 {
    LnF() { applyColours(); }
    void applyColours() {   // called again when the theme switches
        setColour(juce::ComboBox::backgroundColourId, CARD2); setColour(juce::ComboBox::textColourId, INK); setColour(juce::ComboBox::arrowColourId, PURPLE);
        setColour(juce::PopupMenu::backgroundColourId, POP); setColour(juce::PopupMenu::textColourId, INK); setColour(juce::PopupMenu::highlightedBackgroundColourId, PURPLE.withAlpha(0.35f)); setColour(juce::PopupMenu::highlightedTextColourId, INK);
        setColour(juce::TextButton::textColourOffId, INK); setColour(juce::TextButton::textColourOnId, ON); setColour(juce::Label::textColourId, INK);
        setColour(juce::TextEditor::backgroundColourId, CARD2); setColour(juce::TextEditor::textColourId, INK); setColour(juce::TextEditor::outlineColourId, PURPLE.withAlpha(0.4f)); setColour(juce::TextEditor::highlightColourId, PURPLE.withAlpha(0.3f)); setColour(juce::CaretComponent::caretColourId, INK);
        setColour(juce::Slider::textBoxTextColourId, INK); setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack); setColour(juce::Slider::backgroundColourId, CARD2); setColour(juce::Slider::trackColourId, PURPLE.withAlpha(0.45f));
        setColour(juce::ScrollBar::thumbColourId, PURPLE.withAlpha(0.5f)); setColour(juce::ToggleButton::textColourId, INK); setColour(juce::ToggleButton::tickColourId, GREEN); setColour(juce::ToggleButton::tickDisabledColourId, DIM);
        setColour(juce::AlertWindow::backgroundColourId, POP); setColour(juce::AlertWindow::textColourId, INK); setColour(juce::AlertWindow::outlineColourId, PURPLE.withAlpha(0.4f));
        setColour(juce::TooltipWindow::backgroundColourId, POP); setColour(juce::TooltipWindow::textColourId, INK); setColour(juce::TooltipWindow::outlineColourId, PURPLE.withAlpha(0.4f));
        setColour(juce::BubbleComponent::backgroundColourId, POP); setColour(juce::BubbleComponent::outlineColourId, PURPLE.withAlpha(0.5f));
        if (GLASS) {   // GHOST: the popup highlight is the violet glass (white text)
            setColour(juce::PopupMenu::highlightedBackgroundColourId, PURPLE); setColour(juce::PopupMenu::highlightedTextColourId, ON);
        }
    }
    // after a theme switch: every colour a child set at construction time is remapped to its counterpart in the new palette
    static void remap(juce::Component& c, const std::vector<std::pair<juce::Colour, juce::Colour>>& m) {
        auto swap = [&](int id) { if (!c.isColourSpecified(id)) return; const auto cur = c.findColour(id); for (auto& kv : m) if (kv.first.withAlpha(1.0f) == cur.withAlpha(1.0f)) { c.setColour(id, kv.second.withAlpha(cur.getFloatAlpha())); return; } };
        for (int id : { (int) juce::Label::textColourId, (int) juce::Slider::trackColourId, (int) juce::Slider::backgroundColourId, (int) juce::ToggleButton::textColourId, (int) juce::ToggleButton::tickColourId, (int) juce::TextEditor::textColourId }) swap(id);
        if (c.getProperties().contains("tint")) { const auto cur = juce::Colour((juce::uint32) (juce::int64) c.getProperties()["tint"]);   // filled-button tints follow the theme too
            for (auto& kv : m) if (kv.first.withAlpha(1.0f) == cur.withAlpha(1.0f)) { c.getProperties().set("tint", (juce::int64) kv.second.withAlpha(cur.getFloatAlpha()).getARGB()); break; } }
        for (auto* ch : c.getChildren()) remap(*ch, m);
    }
    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override {
        auto r = b.getLocalBounds().toFloat().reduced(1); const bool on = b.getToggleState() || (bool) b.getProperties()["accent"];
        const juce::Colour tint = b.getProperties().contains("tint") ? juce::Colour((juce::uint32) (juce::int64) b.getProperties()["tint"]) : PURPLE;
        if (GLASS) { glassui::pill(g, b.getLocalBounds().toFloat(), tint.withAlpha(1.0f), on, over, down, b.isEnabled(), 9.0f); return; }
        if (NEON) {   // NEON: a dark enamel plate; a lit button is a neon tube: the tint bleeds a soft halo, a pale core line runs along its top
            if (on) { g.setColour(tint.withAlpha(0.10f)); g.fillRoundedRectangle(r.expanded(1.0f), 7); }
            g.setGradientFill(juce::ColourGradient(on ? tint.brighter(0.12f) : CARD2.brighter(0.06f), 0, r.getY(), on ? tint.darker(0.18f) : CARD2.darker(0.12f), 0, r.getBottom(), false)); g.fillRoundedRectangle(r, 5);
            if (on) { g.setColour(neon::paper().withAlpha(0.30f)); g.fillRect(r.getX() + 5.0f, r.getY() + 1.5f, r.getWidth() - 10.0f, 1.0f); }
            g.setColour((on ? tint.brighter(0.25f) : tint).withAlpha(over ? 0.9f : on ? 0.7f : 0.32f)); g.drawRoundedRectangle(r, 5, down ? 2.0f : 1.0f);
            return;
        }
        if (SUMI) {   // LIGHT: a paper tab with a hand-inked outline; a lit button is a watercolour wash with pigment pooled at its edge
            juce::Random rnd((juce::int64) (b.getWidth() * 7919 + b.getHeight() * 104729 + b.getButtonText().hashCode()));
            g.setColour(on ? tint : CARD2.interpolatedWith(CARD, 0.4f)); g.fillRoundedRectangle(r, 3);
            if (on) { g.setColour(tint.darker(0.35f).withAlpha(0.55f)); g.drawRoundedRectangle(r.reduced(1.0f), 3, 1.4f); }
            sumi::inkFrame(g, r.reduced(0.5f), rnd, INK.withAlpha(down ? 1.0f : over ? 0.95f : 0.78f), down ? 2.0f : 1.5f, 0.4f);
            return;
        }
        g.setColour(on ? tint : CARD2); g.fillRoundedRectangle(r, 6);   // (opaque: ON reads on it in every theme)
        g.setColour((on ? tint.brighter(0.3f) : tint).withAlpha(over ? 0.95f : 0.45f)); g.drawRoundedRectangle(r, 6, down ? 2.0f : 1.1f);
    }
    juce::Font getTextButtonFont(juce::TextButton&, int h) override { return font(juce::jmin(12.5f, h * 0.5f)); }
    // a filled (toggled or "accent") button's label is ON, the theme's text-on-accent colour; otherwise the button's own text colour
    void drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) override {
        const bool filled = b.getToggleState() || (bool) b.getProperties()["accent"];
        g.setFont(getTextButtonFont(b, b.getHeight()));
        g.setColour((filled ? ON : b.findColour(juce::TextButton::textColourOffId)).withMultipliedAlpha(b.isEnabled() ? 1.0f : 0.5f));
        const auto box = GLASS ? ghosty::glass::pillBody(b.getLocalBounds().toFloat()).toNearestInt().reduced(4, 0) : b.getLocalBounds().reduced(juce::jmin(4, b.getHeight() / 4) + 2, 1);
        if (const auto pf = pixelFit(b.getButtonText(), box, b.getButtonText().length() <= 2 ? juce::jmin(13.0f, b.getHeight() * 0.46f) : juce::jmin(9.5f, b.getHeight() * 0.36f))) {   // GHOSTY BEATS: the pixel font
            g.setFont(*pf); g.drawText(b.getButtonText(), box.translated(0, 1), juce::Justification::centred, false); return; }
        g.drawFittedText(b.getButtonText(), box, juce::Justification::centred, 2, 0.8f);
    }
    // call-outs (the step editor) use the theme's panel colour, not the stock dark grey
    void drawCallOutBoxBackground(juce::CallOutBox& box, juce::Graphics& g, const juce::Path& path, juce::Image&) override {
        if (GLASS) { g.setColour(SHADOW); g.strokePath(path, juce::PathStrokeType(4.0f)); const auto b = path.getBounds();
                     g.setGradientFill(juce::ColourGradient(juce::Colour(0xfff8f6ff), b.getX(), b.getY(), juce::Colour(0xffeef7f4), b.getRight(), b.getBottom(), false)); g.fillPath(path);
                     g.setColour(juce::Colour(0xe6ffffff)); g.strokePath(path, juce::PathStrokeType(1.4f)); g.setColour(PURPLE.withAlpha(0.35f)); g.strokePath(path, juce::PathStrokeType(0.8f)); (void) box; return; }
        g.setColour(SHADOW); g.strokePath(path, juce::PathStrokeType(4.0f));   // soft edge shadow
        g.setColour(POP); g.fillPath(path); g.setColour(PURPLE.withAlpha(0.45f)); g.strokePath(path, juce::PathStrokeType(1.2f)); (void) box;
    }
    void drawComboBox(juce::Graphics& g, int w, int h, bool down, int, int, int, int, juce::ComboBox& cb) override {
        if (GLASS) {
            const auto r = juce::Rectangle<float>(0, 0, (float) w, (float) h); glassui::pill(g, r, PURPLE, false, cb.isMouseOver(true), down, cb.isEnabled(), 9.0f);
            const auto body = ghosty::glass::pillBody(r); juce::Path a; const float ax = body.getRight() - 8.0f, ay = body.getCentreY();
            a.startNewSubPath(ax - 3.5f, ay - 1.8f); a.lineTo(ax, ay + 2.2f); a.lineTo(ax + 3.5f, ay - 1.8f);
            g.setColour(PURPLE); g.strokePath(a, juce::PathStrokeType(1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); return;
        }
        auto r = juce::Rectangle<float>(0, 0, (float) w, (float) h).reduced(1); g.setColour(CARD2); g.fillRoundedRectangle(r, 6);
        g.setColour(PURPLE.withAlpha(0.5f)); g.drawRoundedRectangle(r, 6, 1.1f);
        juce::Path a; a.addTriangle((float) w - 14, h * 0.5f - 2, (float) w - 7, h * 0.5f - 2, (float) w - 10.5f, h * 0.5f + 3); g.setColour(PURPLE); g.fillPath(a);
    }
    juce::Font getComboBoxFont(juce::ComboBox&) override { return font(12.0f); }
    // ---- GHOST only (every other look takes the LookAndFeel_V4 path unchanged)
    void drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float mn, float mx, juce::Slider::SliderStyle st, juce::Slider& s) override {
        if (!GLASS) { juce::LookAndFeel_V4::drawLinearSlider(g, x, y, w, h, pos, mn, mx, st, s); return; }
        namespace gg = ghosty::glass; const auto tc = s.findColour(juce::Slider::trackColourId);
        if (st == juce::Slider::LinearBar || st == juce::Slider::LinearBarVertical) {   // a recessed glass well, the value as a violet -> teal fill (the text reads over it)
            auto r = juce::Rectangle<float>((float) x, (float) y, (float) w, (float) h); gg::recessed(g, r, juce::jmin(7.0f, r.getHeight() * 0.5f));
            if (tc.getAlpha() > 0) {
                auto f = st == juce::Slider::LinearBar ? r.withRight(juce::jlimit(r.getX(), r.getRight(), pos)) : r.withTop(juce::jlimit(r.getY(), r.getBottom(), pos));
                juce::ColourGradient cg(gg::violet().withAlpha(juce::jmin(0.45f, tc.getFloatAlpha())), r.getX(), 0, gg::teal().withAlpha(juce::jmin(0.45f, tc.getFloatAlpha())), r.getRight(), 0, false);
                g.setGradientFill(cg); g.fillRoundedRectangle(f.reduced(1.5f), juce::jmin(6.0f, f.getHeight() * 0.5f));
            }
            return;
        }
        if (s.isHorizontal()) {
            auto tr = juce::Rectangle<float>((float) x, (float) y + h * 0.5f - 3.5f, (float) w, 7.0f); gg::track(g, tr, (float) x, pos, s.isEnabled());
            gg::thumb(g, { pos, (float) y + h * 0.5f }, juce::jlimit(10.0f, 18.0f, (float) h - 4.0f));
        } else {
            auto tr = juce::Rectangle<float>((float) x + w * 0.5f - 3.5f, (float) y, 7.0f, (float) h);
            g.setColour(juce::Colour(0x30443a7a)); g.fillRoundedRectangle(tr, 3.5f);
            g.setGradientFill(juce::ColourGradient(gg::violet(), 0, tr.getBottom(), gg::teal(), 0, tr.getY(), false)); g.fillRoundedRectangle(tr.withTop(pos), 3.5f);
            gg::thumb(g, { tr.getCentreX(), pos }, juce::jlimit(10.0f, 18.0f, (float) w - 4.0f));
        }
    }
    void drawPopupMenuBackground(juce::Graphics& g, int w, int h) override {
        if (!GLASS) { juce::LookAndFeel_V4::drawPopupMenuBackground(g, w, h); return; }
        g.fillAll(POP); ghosty::glass::card(g, juce::Rectangle<float>(0, 0, (float) w, (float) h), 6.0f);
    }
    void drawTooltip(juce::Graphics& g, const juce::String& text, int w, int h) override {
        if (!GLASS) { juce::LookAndFeel_V4::drawTooltip(g, text, w, h); return; }
        const auto r = juce::Rectangle<float>(0, 0, (float) w, (float) h); g.fillAll(POP); ghosty::glass::card(g, r, 6.0f);
        juce::AttributedString a; a.setJustification(juce::Justification::centred); a.append(text, juce::Font(juce::FontOptions(13.0f, juce::Font::bold)), INK);
        juce::TextLayout tl; tl.createLayoutWithBalancedLineLengths(a, 400.0f); tl.draw(g, r);
    }
    void positionComboBoxText(juce::ComboBox& b, juce::Label& l) override { l.setBounds(4, 1, b.getWidth() - 18, b.getHeight() - 2); l.setFont(font(12.0f)); }
    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) override {
        auto r = juce::Rectangle<float>((float) x, (float) y, (float) w, (float) h).reduced(2); const float sz = juce::jmin(r.getWidth(), r.getHeight());
        auto c = r.withSizeKeepingCentre(sz, sz); const float th = sz > 40 ? 4.0f : 3.0f, ang = a0 + pos * (a1 - a0);
        const bool bip = s.getMinimum() < 0 && s.getMaximum() > 0;
        if (GLASS) {   // GHOST: a glossy pearl knob (cached face) in a glass ring, the value as a violet -> teal arc
            const float tg = th + 0.5f, rr = sz / 2 - tg / 2; glassui::sprites().draw(g, c, sz * 0.5f, glassui::KnobFace);
            juce::Path tr; tr.addCentredArc(c.getCentreX(), c.getCentreY(), rr, rr, 0, a0, a1, true);
            g.setColour(juce::Colour(0x30443a7a)); g.strokePath(tr, juce::PathStrokeType(tg, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            const float from = bip ? (a0 + a1) * 0.5f : a0;
            if (std::abs(ang - from) > 0.01f) { juce::Path ar; ar.addCentredArc(c.getCentreX(), c.getCentreY(), rr, rr, 0, juce::jmin(from, ang), juce::jmax(from, ang), true);
                g.setGradientFill(glassui::arcFill(c)); g.strokePath(ar, juce::PathStrokeType(tg, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); }
            g.setColour(s.isEnabled() ? PURPLE : GREY); g.drawLine({ c.getCentre().getPointOnCircumference(sz * 0.10f, ang), c.getCentre().getPointOnCircumference(sz * 0.29f, ang) }, 2.2f);
            return;
        }
        g.setColour(CARD2); g.fillEllipse(c.reduced(th + 1));
        juce::Path tr; tr.addCentredArc(c.getCentreX(), c.getCentreY(), sz / 2 - th / 2, sz / 2 - th / 2, 0, a0, a1, true);
        g.setColour(INK.withAlpha(0.08f)); g.strokePath(tr, juce::PathStrokeType(th, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const float from = bip ? (a0 + a1) * 0.5f : a0;
        juce::Path ar; ar.addCentredArc(c.getCentreX(), c.getCentreY(), sz / 2 - th / 2, sz / 2 - th / 2, 0, juce::jmin(from, ang), juce::jmax(from, ang), true);
        if (NEON) {   // NEON: the arc is a teal -> amber tube with a faint bleed under it
            g.setColour(PURPLE.withAlpha(0.14f)); g.strokePath(ar, juce::PathStrokeType(th + 4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setGradientFill(juce::ColourGradient(PURPLE, c.getX(), c.getBottom(), AMBER, c.getRight(), c.getY(), false));
        } else if (SUMI) g.setGradientFill(juce::ColourGradient(PURPLE, c.getX(), c.getBottom(), RED, c.getRight(), c.getY(), false));   // LIGHT: indigo -> vermilion ink
        else
        g.setGradientFill(juce::ColourGradient(PURPLE, c.getX(), c.getBottom(), GREEN, c.getRight(), c.getY(), false));
        g.strokePath(ar, juce::PathStrokeType(th, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(INK); g.drawLine({ c.getCentre().getPointOnCircumference(sz * 0.12f, ang), c.getCentre().getPointOnCircumference(sz / 2 - th - 3, ang) }, 1.8f);
    }
};

// ---------------- the house slider (every juce::Slider in Beats: bars and knobs). The GhostBlock convention:
//   RIGHT-CLICK = type a value (ghosty::TypeIn), DOUBLE-CLICK = the default, HOVER / the bar's text = the value in real units,
//   BARS: MOUSE-DOWN sets the value at the click, DRAG scrubs, SHIFT = fine (1/8, relative), WHEEL = one step (knobs: 1 % of the range).
//   onDragStart (this one hides juce::Slider's) = "a gesture starts": called ONCE per gesture (a drag, a double-click, a burst of
//   wheel steps, a typed value), so a model-bound slider pushes exactly one undo step per gesture.
struct Bar : juce::Slider {
    std::function<void()> onDragStart;
    explicit Bar(SliderStyle st = LinearBar, TextEntryBoxPosition tb = TextBoxRight) : juce::Slider(st, tb) {
        juce::Slider::onDragStart = [this] { gesture(); };
        if (isBar()) setTextBoxIsEditable(false);   // a click on a bar sets the value (typing = right-click)
    }
    bool isBar() const { return getSliderStyle() == LinearBar || getSliderStyle() == LinearBarVertical || getSliderStyle() == LinearHorizontal || getSliderStyle() == LinearVertical; }
    void gesture() { const double now = juce::Time::getMillisecondCounterHiRes(); if (now - lastGestureMs > 450.0 && onDragStart) onDragStart(); lastGestureMs = now; }
    void startedDragging() override { ++nest; }
    void stoppedDragging() override { nest = juce::jmax(0, nest - 1); }
    void valueChanged() override { if (nest == 0 && !fine) gesture(); }   // a typed value (TypeIn) or the keyboard: one gesture too
    void mouseDown(const juce::MouseEvent& e) override {
        ignore = e.mods.isPopupMenu(); if (ignore || !isEnabled()) return;   // right-click: ghosty::TypeIn opens the type-in box
        if (isBar() && e.mods.isShiftDown()) { fine = true; ++nest; gesture(); fineX = e.position.x; fineV = getValue(); return; }
        juce::Slider::mouseDown(e);
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (ignore) return;
        if (fine) { const bool vert = getSliderStyle() == LinearBarVertical || getSliderStyle() == LinearVertical; const float len = (float) juce::jmax(1, vert ? getHeight() : getWidth());
                    const float d = vert ? (fineX - e.position.y) : (e.position.x - fineX);
                    setValue(snapValue(juce::jlimit(getMinimum(), getMaximum(), fineV + d / len * (getMaximum() - getMinimum()) / 8.0), absoluteDrag), juce::sendNotificationSync); return; }
        juce::Slider::mouseDrag(e);
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (ignore) { ignore = false; return; }
        if (fine) { fine = false; nest = juce::jmax(0, nest - 1); if (juce::Slider::onDragEnd) juce::Slider::onDragEnd(); return; }
        juce::Slider::mouseUp(e);
    }
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override {
        if (!isEnabled() || !isScrollWheelEnabled() || getMaximum() <= getMinimum()) { juce::Component::mouseWheelMove(e, w); return; }
        if (e.eventTime == lastWheel) return; lastWheel = e.eventTime;
        const float amt = (std::abs(w.deltaX) > std::abs(w.deltaY) ? -w.deltaX : w.deltaY) * (w.isReversed ? -1.0f : 1.0f); if (amt == 0.0f) return;
        const double step = getInterval() > 0 ? getInterval() : (getMaximum() - getMinimum()) / 100.0;
        ++nest; gesture(); setValue(juce::jlimit(getMinimum(), getMaximum(), getValue() + (amt > 0 ? step : -step)), juce::sendNotificationSync); --nest;
        if (juce::Slider::onDragEnd) juce::Slider::onDragEnd();
    }
    void mouseDoubleClick(const juce::MouseEvent& e) override { if (e.mods.isPopupMenu()) return; juce::Slider::mouseDoubleClick(e); }
private:
    double lastGestureMs = -1.0e9; int nest = 0; bool fine = false, ignore = false; float fineX = 0; double fineV = 0; juce::Time lastWheel;
};

// the value a model-bound slider shows on hover / in its text, in real units
enum class Unit { Pct, BiPct, Semis, Ms, Gain, Pan, Centred };
inline void setUnits(juce::Slider& s, Unit u) {
    s.textFromValueFunction = [u](double v) -> juce::String {
        switch (u) {
            case Unit::Pct:     return juce::String(juce::roundToInt(v * 100.0)) + " %";
            case Unit::BiPct:   return (v > 0.0005 ? "+" : "") + juce::String(juce::roundToInt(v * 100.0)) + " %";
            case Unit::Semis:   return (v > 0.0005 ? "+" : "") + juce::String(v, std::abs(v - std::round(v)) < 1e-6 ? 0 : 1) + " st";
            case Unit::Ms:      return juce::String(juce::roundToInt(v)) + " ms";
            case Unit::Gain:    return v <= 0.0005 ? juce::String("-inf dB") : juce::String(juce::Decibels::gainToDecibels(v), 1) + " dB";
            case Unit::Pan:     return std::abs(v) < 0.005 ? juce::String("C") : (v < 0 ? "L " : "R ") + juce::String(juce::roundToInt(std::abs(v) * 100.0));
            case Unit::Centred: return std::abs(v - 0.5) < 0.0025 ? juce::String("0") : (v > 0.5 ? "+" : "") + juce::String(juce::roundToInt((v - 0.5) * 200.0)) + " %";
        }
        return juce::String(v, 2);
    };
    s.valueFromTextFunction = [u](const juce::String& t) -> double {
        const auto lo = t.trim().toLowerCase(); const double x = lo.retainCharacters("-0123456789.").getDoubleValue();
        switch (u) {
            case Unit::Pct: case Unit::BiPct: return x / 100.0;
            case Unit::Centred: return 0.5 + x / 200.0;
            case Unit::Gain: return lo.contains("inf") ? 0.0 : juce::Decibels::decibelsToGain(x);
            case Unit::Pan: return lo.startsWith("c") ? 0.0 : lo.startsWith("l") ? -std::abs(x) / 100.0 : lo.startsWith("r") ? std::abs(x) / 100.0 : x / 100.0;
            default: return x;
        }
    };
}

// knob + label; bound to an APVTS param, or to a getter/setter on the model
struct Knob : juce::Component {
    Bar s { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox }; juce::Label l;
    // bind to a host parameter: double-click = its default; the hover value is the parameter's own text (real units)
    void bind(juce::AudioProcessorValueTreeState& vts, const juce::String& id) {
        att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, id, s);
        if (auto* p = vts.getParameter(id)) s.setDoubleClickReturnValue(true, p->convertFrom0to1(p->getDefaultValue()));
    }
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    explicit Knob(const juce::String& name) {
        s.setPopupDisplayEnabled(true, true, nullptr); addAndMakeVisible(s);
        l.setText(name, juce::dontSendNotification); l.setJustificationType(juce::Justification::centred); l.setFont(font(10.0f));
        l.setColour(juce::Label::textColourId, GREY); l.setInterceptsMouseClicks(false, false); addAndMakeVisible(l);
    }
    void resized() override { auto r = getLocalBounds(); l.setBounds(r.removeFromBottom(13)); s.setBounds(r); }
};

// the editor services every part uses
struct Ed {
    BeatsXProcessor& proc;
    int track = 0;                                  // selected track
    int stepPage = 0;                               // which 16 steps the grid / lanes show
    std::function<void()> refresh;                  // after any edit: repaint + resync controls
    std::function<void(const juce::String&)> say;
    explicit Ed(BeatsXProcessor& p) : proc(p) {}
    int patIdx() const { return proc.currentPattern(); }
    gb::Pattern& pat() { return proc.model.patterns[(size_t) patIdx()]; }
    gb::Track& tr() { return proc.model.tracks[(size_t) track]; }
    template <class F> void edit(F&& f) { proc.pushUndo(); f(); proc.publish(); if (refresh) refresh(); }
    void live() { proc.publish(); if (refresh) refresh(); }   // inside a gesture that already pushed undo
    // one undo step per gesture: repeated ticks of the same control (mouse wheel, key repeat) within half a second share it
    const void* lastGesture = nullptr; juce::uint32 lastGestureMs = 0;
    void gestureUndo(const void* control) { const auto now = juce::Time::getMillisecondCounter(); if (control != lastGesture || now - lastGestureMs > 500) proc.pushUndo(); lastGesture = control; lastGestureMs = now; }
    juce::Component::SafePointer<juce::CallOutBox> stepBox;   // the open step editor (dismissed when the editor closes)
    // the step the playhead is on for track t of the shown pattern (-1 = not playing it)
    int playStep(int t) const {
        const double b = proc.engine.uiBeat.load(); if (b < 0 || proc.engine.uiPattern.load() != patIdx()) return -1;
        const auto& p = proc.model.patterns[(size_t) patIdx()]; long s = (long) std::floor((b - proc.engine.uiOrigin.load()) / gb::stepBeats(p.res) + 1e-9);
        if (proc.model.converge > 0) s %= (long) std::llround(proc.model.converge * 4.0 / gb::stepBeats(p.res));
        return (int) (s % p.lenOf(t));
    }
};

// ---------------- a click-to-set value bar (the step editor's fields): MOUSE-DOWN anywhere sets the value to that exact spot (snapped
// to a whole step; bipolar fields fill from their centre), DRAG keeps scrubbing, SHIFT = fine (1/8 speed, relative), RELEASE = one undo
// step per gesture, RIGHT-CLICK = type a value, DOUBLE-CLICK = the default, the WHEEL = one step. Hover shows the value (it is always drawn).
struct ValueBar : juce::Component {
    int lo = 0, hi = 127, value = 0, def = 0; juce::String suffix;
    std::function<void()> onGestureStart;          // push ONE undo step
    std::function<void(int)> onChange;             // live value while the gesture runs
    std::function<void(const std::function<void()>&)> oneStep;   // a single-shot change wrapped in one undo step (wheel / type / reset)
    bool fine = false; float fineFrom = 0; int fineValue = 0; bool inGesture = false;
    juce::TextEditor typer;
    ValueBar() { setRepaintsOnMouseActivity(true); typer.setJustification(juce::Justification::centred); typer.setInputRestrictions(5, "-0123456789"); addChildComponent(typer);
                 typer.onReturnKey = [this] { commitTyped(); }; typer.onFocusLost = [this] { commitTyped(); }; typer.onEscapeKey = [this] { typer.setVisible(false); }; }
    bool bipolar() const { return lo < 0 && hi > 0; }
    int valueAt(float x) const { const float u = juce::jlimit(0.0f, 1.0f, (x - 1.0f) / juce::jmax(1.0f, (float) getWidth() - 2.0f)); return juce::jlimit(lo, hi, (int) std::lround(lo + u * (hi - lo))); }
    float xOf(int v) const { return 1.0f + (float) (v - lo) / (float) juce::jmax(1, hi - lo) * ((float) getWidth() - 2.0f); }
    void setValueLive(int v) { v = juce::jlimit(lo, hi, v); if (v == value) return; value = v; if (onChange) onChange(v); repaint(); }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat(); g.setColour(CARD2); g.fillRoundedRectangle(r, 4);
        const float x0 = bipolar() ? xOf(0) : 1.0f, xv = xOf(value);
        g.setColour(PURPLE.withAlpha(isMouseOverOrDragging() ? 0.62f : 0.48f)); g.fillRoundedRectangle(juce::Rectangle<float>(juce::jmin(x0, xv), 1.0f, std::abs(xv - x0) + (bipolar() ? 0.0f : 0.0f), r.getHeight() - 2.0f), 3);
        if (bipolar()) { g.setColour(INK.withAlpha(0.35f)); g.fillRect(x0 - 0.5f, 2.0f, 1.0f, r.getHeight() - 4.0f); }
        g.setColour(INK); g.setFont(font(11.0f)); g.drawText(juce::String(value) + suffix, getLocalBounds().reduced(6, 0), juce::Justification::centredRight);
        if (isMouseOverOrDragging()) { g.setColour(PURPLE); g.drawRoundedRectangle(r.reduced(0.5f), 4, 1.0f); }
    }
    void mouseDown(const juce::MouseEvent& e) override {
        if (e.mods.isPopupMenu()) { startTyping(); return; }   // right-click: type
        inGesture = true; if (onGestureStart) onGestureStart();
        fine = e.mods.isShiftDown(); fineFrom = e.position.x; fineValue = value;
        if (!fine) setValueLive(valueAt(e.position.x));   // absolute: the value jumps to where you click
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (!inGesture) return;
        if (e.mods.isShiftDown() != fine) { fine = e.mods.isShiftDown(); fineFrom = e.position.x; fineValue = value; }
        if (fine) setValueLive(fineValue + (int) std::lround((e.position.x - fineFrom) / juce::jmax(1.0f, (float) getWidth() - 2.0f) * (hi - lo) / 8.0f));
        else setValueLive(valueAt(e.position.x));
    }
    void mouseUp(const juce::MouseEvent&) override { inGesture = false; }
    void mouseDoubleClick(const juce::MouseEvent& e) override { if (e.mods.isPopupMenu()) return; inGesture = false; if (value != def) { if (oneStep) oneStep([this] { setValueLive(def); }); else setValueLive(def); } }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override { const int d = (w.deltaY + w.deltaX) > 0 ? 1 : -1; const int v = juce::jlimit(lo, hi, value + d); if (v != value) { if (oneStep) oneStep([this, v] { setValueLive(v); }); else setValueLive(v); } }
    void startTyping() { typer.setBounds(getLocalBounds()); typer.setText(juce::String(value), false); typer.setVisible(true); typer.grabKeyboardFocus(); typer.selectAll(); }
    void commitTyped() { if (!typer.isVisible()) return; typer.setVisible(false); const auto t = typer.getText().trim(); if (t.containsAnyOf("0123456789")) { const int v = juce::jlimit(lo, hi, t.getIntValue()); if (v != value) { if (oneStep) oneStep([this, v] { setValueLive(v); }); else setValueLive(v); } } }
};

// ---------------- step editor popup: every field of one step (grid double-click, tracker double-click). Several targets = the change
// applies to all of them (each field writes its value into every target step).
struct StepEditor : juce::Component {
    Ed& ed; int t, s; std::vector<std::pair<int, int>> targets; juce::OwnedArray<ValueBar> sl; juce::OwnedArray<juce::Label> lb; juce::ToggleButton rev { "reverse" }, lock { "lock" }, tlock { "timing lock" };
    juce::Label hint;
    juce::Component::SafePointer<juce::Component> owner;   // the grid / tracker that opened us: when it is gone (editor closed), every callback returns
    struct F { const char* name; int lo, hi, def; std::function<int(const gb::Step&)> get; std::function<void(gb::Step&, int)> set; };
    std::vector<F> fields;
    StepEditor(Ed& e, int track, int step, juce::Component* own, std::vector<std::pair<int, int>> more = {}) : ed(e), t(track), s(step), owner(own) {
        targets = more; if (targets.empty()) targets.push_back({ t, s });
        setOpaque(true);   // every pixel of the body is painted (nothing behind it shows through)
        fields = {
            { "VELOCITY", 0, 127, 100, [](auto& x) { return (int) x.vel; }, [](auto& x, int v) { x.vel = (uint8_t) v; } },
            { "CHANCE %", 1, 100, 100, [](auto& x) { return (int) x.prob; }, [](auto& x, int v) { x.prob = (uint8_t) v; } },
            { "HITS (ROLL)", 1, 16, 1, [](auto& x) { return (int) x.roll; }, [](auto& x, int v) { x.roll = (uint8_t) v; } },
            { "RETRIG /BEAT", 0, 64, 0, [](auto& x) { return (int) x.rate; }, [](auto& x, int v) { x.rate = (uint8_t) v; } },
            { "ACCEL", -100, 100, 0, [](auto& x) { return (int) x.accel; }, [](auto& x, int v) { x.accel = (int8_t) v; } },
            { "VEL RAMP", -100, 100, 0, [](auto& x) { return (int) x.velRamp; }, [](auto& x, int v) { x.velRamp = (int8_t) v; } },
            { "PITCH", -24, 24, 0, [](auto& x) { return (int) x.pitch; }, [](auto& x, int v) { x.pitch = (int8_t) v; } },
            { "CENTS", -99, 99, 0, [](auto& x) { return (int) x.cents; }, [](auto& x, int v) { x.cents = (int8_t) v; } },
            { "PITCH RAMP", -24, 24, 0, [](auto& x) { return (int) x.pitchRamp; }, [](auto& x, int v) { x.pitchRamp = (int8_t) v; } },
            { "NUDGE %", -50, 50, 0, [](auto& x) { return (int) x.nudge; }, [](auto& x, int v) { x.nudge = (int8_t) v; } },
            { "FLAM ms", 0, 40, 0, [](auto& x) { return (int) x.flam; }, [](auto& x, int v) { x.flam = (uint8_t) v; } },
            { "DECAY %", 5, 100, 100, [](auto& x) { return (int) x.decay; }, [](auto& x, int v) { x.decay = (uint8_t) v; } },
            { "START %", 0, 99, 0, [](auto& x) { return (int) x.offset; }, [](auto& x, int v) { x.offset = (uint8_t) v; } },
            { "CUT %", 0, 100, 0, [](auto& x) { return (int) x.cut; }, [](auto& x, int v) { x.cut = (uint8_t) v; } },
            { "SLICE", 0, 64, 0, [](auto& x) { return (int) x.slice; }, [](auto& x, int v) { x.slice = (uint8_t) v; } },
            { "DESTROY %", 0, 100, 0, [](auto& x) { return (int) x.destroy; }, [](auto& x, int v) { x.destroy = (uint8_t) v; } },
            { "FREEZE x10ms", 0, 255, 0, [](auto& x) { return (int) x.freeze; }, [](auto& x, int v) { x.freeze = (uint8_t) v; } },
            { "BAND REV 1L 2M 4H", 0, 7, 0, [](auto& x) { return (int) x.frev; }, [](auto& x, int v) { x.frev = (uint8_t) v; } },
            { "PAN", -50, 50, 0, [](auto& x) { return (int) x.pan; }, [](auto& x, int v) { x.pan = (int8_t) v; } },
            { "FILTER 1-63 LP 65-127 HP", 0, 127, 0, [](auto& x) { return (int) x.filt; }, [](auto& x, int v) { x.filt = (uint8_t) v; } } };
        const auto& st = cur();
        hint.setText("click anywhere to set  |  drag to scrub (shift = fine)  |  right-click to type  |  double-click resets", juce::dontSendNotification);
        hint.setFont(font(9.5f, false)); hint.setColour(juce::Label::textColourId, INK);   // (small plain text: full ink) hint.setJustificationType(juce::Justification::centred); addAndMakeVisible(hint);
        for (auto& f : fields) {
            auto* l = lb.add(new juce::Label({}, f.name)); l->setFont(font(10.5f)); l->setColour(juce::Label::textColourId, GREY); addAndMakeVisible(l);
            auto* x = sl.add(new ValueBar()); x->lo = f.lo; x->hi = f.hi; x->def = f.def; x->value = juce::jlimit(f.lo, f.hi, f.get(st));
            auto* fp = &f;
            x->onGestureStart = [this] { if (owner) ed.proc.pushUndo(); };
            x->onChange = [this, fp](int v) { if (!owner) return; for (auto& ts : targets) fp->set(this->step(ts), v); ed.live(); };
            x->oneStep = [this](const std::function<void()>& f2) { if (!owner) return; ed.proc.pushUndo(); f2(); };
            addAndMakeVisible(x);
        }
        for (auto* b : { &rev, &lock, &tlock }) { b->setColour(juce::ToggleButton::textColourId, INK); b->setColour(juce::ToggleButton::tickColourId, GREEN); addAndMakeVisible(b); }
        rev.setToggleState(st.reversed(), juce::dontSendNotification); lock.setToggleState(st.locked(), juce::dontSendNotification); tlock.setToggleState(st.timeLocked(), juce::dontSendNotification);
        rev.onClick = [this] { flag(1, rev.getToggleState()); }; lock.onClick = [this] { flag(2, lock.getToggleState()); }; tlock.onClick = [this] { flag(4, tlock.getToggleState()); };
        setSize(320, (int) fields.size() * 22 + 64);
    }
    gb::Step& step(const std::pair<int, int>& ts) { return ed.proc.model.patterns[(size_t) ed.patIdx()].steps[(size_t) ts.first][(size_t) ts.second]; }
    gb::Step& cur() { return step(targets.front()); }
    void flag(int bit, bool on) { if (!owner) return; ed.edit([&] { for (auto& ts : targets) { auto& st = step(ts); st.flags = (uint8_t) (on ? (st.flags | bit) : (st.flags & ~bit)); } }); }
    void paint(juce::Graphics& g) override { g.fillAll(CARD); }   // opaque body in the theme's panel colour (light + dark)
    void resized() override {
        hint.setBounds(4, 2, getWidth() - 8, 16);
        for (int i = 0; i < (int) fields.size(); ++i) { lb[i]->setBounds(4, 20 + i * 22, 118, 20); sl[i]->setBounds(124, 20 + i * 22, getWidth() - 128, 20); }
        const int y = 24 + (int) fields.size() * 22; rev.setBounds(4, y, 90, 24); lock.setBounds(100, y, 80, 24); tlock.setBounds(186, y, 126, 24);
    }
    static void open(Ed& e, int t, int s, juce::Component& near, juce::Rectangle<int> area, std::vector<std::pair<int, int>> targets = {}) {
        if (e.stepBox) e.stepBox->dismiss();
        auto c = std::make_unique<StepEditor>(e, t, s, &near, std::move(targets));
        e.stepBox = &juce::CallOutBox::launchAsynchronously(std::move(c), near.getScreenBounds().getIntersection(area.isEmpty() ? near.getScreenBounds() : area), nullptr);
        e.stepBox->setLookAndFeel(&near.getLookAndFeel());   // the theme's panel colour (released again when the editor closes)
        e.stepBox->setAlpha(1.0f);
    }
};

// ---------------- the step grid: tracks are rows, time runs left -> right, 16 columns per page
struct StepGrid : juce::Component {
    Ed& ed; bool painting = false, erasing = false; uint8_t paintVel = 0; int lastT = -1, lastS = -1;
    // FL-style note editing (Brandon 2026-11-21): click a note = select it (cmd-click adds more); drag = move them; shift-drag = copy them;
    // cmd+C / cmd+V copies and pastes at the spot you clicked; right-click (or right-drag) deletes; delete / backspace deletes the selection
    std::vector<std::pair<int, int>> sel;                                       // selected notes (track, step)
    struct Clip { int dt, ds; gb::Step st; }; static inline std::vector<Clip> clip;   // what cmd+C copied (shared by every Ghosty Beats grid)
    bool dragging = false, dragCopy = false, dragMoved = false; int downT = -1, downS = -1, dragDT = 0, dragDS = 0; gb::Pattern dragOrig; const gb::Pattern* selPat = nullptr;
    bool isSel(int t, int s) const { for (auto& q : sel) if (q.first == t && q.second == s) return true; return false; }
    void validateSel() { if (selPat != &ed.pat()) { sel.clear(); selPat = &ed.pat(); } }   // another pattern: nothing selected
    int curT = 0, curS = 0;   // keyboard cursor
    gb::Pattern previewBuf; bool previewing = false;
    explicit StepGrid(Ed& e) : ed(e) { setWantsKeyboardFocus(true); }
    // keys go to the grid while the mouse is over it, back to the host when it leaves (space etc. reach the DAW)
    void mouseEnter(const juce::MouseEvent&) override { grabKeyboardFocus(); repaint(); }
    void mouseExit(const juce::MouseEvent&) override { if (hasKeyboardFocus(true)) giveAwayKeyboardFocus(); repaint(); }
    float rowH() const { return getHeight() / (float) gb::kTracks; }
    float colW() const { return getWidth() / 16.0f; }
    void paint(juce::Graphics& g) override {
        auto& P = ed.pat(); const float rh = rowH(), cw = colW();
        for (int t = 0; t < gb::kTracks; ++t) {
            if (t == ed.track) { g.setColour(PURPLE.withAlpha(0.09f)); g.fillRect(0.0f, t * rh, (float) getWidth(), rh); }
            const int L = P.lenOf(t), now = ed.playStep(t);
            for (int c = 0; c < 16; ++c) {
                const int s = ed.stepPage * 16 + c;
                auto r = juce::Rectangle<float>(c * cw + 1.5f, t * rh + 1.5f, cw - 3, rh - 3);
                if (s >= L) { g.setColour(DIM.withAlpha(0.35f)); g.fillRoundedRectangle(r, 3); continue; }
                const bool beat = (int) std::floor(s * gb::stepBeats(P.res) + 1e-9) % 2 == 0;
                const auto& st = P.steps[(size_t) t][(size_t) s];
                if (GLASS) {   // GHOST: a glass step cell (cached); a lit step = glossy glass in the track colour, as tall as its velocity
                    glassui::sprites().draw(g, r, 4.0f, glassui::Cell);
                    if (beat) { g.setColour(ghosty::glass::violet().withAlpha(0.07f)); g.fillRoundedRectangle(r, 4.0f); }
                    if (st.vel) {
                        const auto col = trackColour(t); const float v = juce::jmin(1.0f, st.vel / 127.0f);
                        g.setColour(col.withAlpha(0.20f)); g.fillRoundedRectangle(r, 4.0f);
                        juce::Graphics::ScopedSaveState ss(g); g.reduceClipRegion(r.withTrimmedTop(r.getHeight() * (1 - v)).expanded(3.0f, 0.0f).withTrimmedBottom(-3.0f).getSmallestIntegerContainer());
                        glassui::sprites().draw(g, r, 4.0f, glassui::Lit, col);
                    }
                } else g.setColour(INK.withAlpha(beat ? 0.075f : 0.045f)), g.fillRoundedRectangle(r, 3);
                if (st.vel) {
                    const auto col = trackColour(t); const float v = juce::jmin(1.0f, st.vel / 127.0f);
                    if (NEON) {   // NEON: a lit window / tube: the colour bleeds a little past the cell, a pale core line on top, velocity = fill height
                        g.setColour(col.withAlpha(0.10f)); g.fillRoundedRectangle(r.expanded(2.5f), 5); g.setColour(col.withAlpha(0.16f)); g.fillRoundedRectangle(r.expanded(1.0f), 4);
                        g.setColour(col.withAlpha(0.22f)); g.fillRoundedRectangle(r, 3);
                        const auto f = r.withTrimmedTop(r.getHeight() * (1 - v));
                        g.setGradientFill(juce::ColourGradient(col.brighter(0.35f).withAlpha(0.95f), f.getX(), f.getY(), col.darker(0.25f).withAlpha(0.80f + 0.15f * v), f.getX(), f.getBottom(), false)); g.fillRoundedRectangle(f, 3);
                        g.setColour(neon::paper().withAlpha(0.35f)); g.fillRect(f.getX() + 2.0f, f.getY() + 1.0f, f.getWidth() - 4.0f, 1.0f);
                    } else if (SUMI) {   // LIGHT: a watercolour dab: a pale wash over the cell, the hit's pigment as tall as its velocity, darker where it dried
                        g.setColour(col.withAlpha(0.16f)); g.fillRoundedRectangle(r, 2);
                        const auto f = r.withTrimmedTop(r.getHeight() * (1 - v));
                        g.setColour(col.withAlpha(0.62f + 0.33f * v)); g.fillRoundedRectangle(f, 2);
                        g.setColour(col.darker(0.45f).withAlpha(0.6f)); g.drawRoundedRectangle(f.reduced(0.4f), 2, 1.0f);
                    } else if (!GLASS) { g.setColour(col.withAlpha(0.25f)); g.fillRoundedRectangle(r, 3);
                    g.setColour(col.withAlpha(0.55f + 0.45f * v)); g.fillRoundedRectangle(r.withTrimmedTop(r.getHeight() * (1 - v)), 3); }   // velocity = fill height
                    if (st.vel >= 120) { g.setColour(INK); g.fillRect(r.getX() + 2, r.getY() + 1.5f, r.getWidth() - 4, 1.5f); }
                    if (st.roll > 1) { g.setColour(BG.withAlpha(0.85f)); const int n = juce::jmin(8, (int) st.roll); const float dw = juce::jmin(4.0f, (r.getWidth() - 6) / n);
                        for (int k = 0; k < n; ++k) g.fillEllipse(r.getX() + 3 + k * (r.getWidth() - 6) / n, r.getBottom() - dw - 2, dw, dw); }
                    if (st.nudge != 0) { g.setColour(INK); const float nx = r.getCentreX() + st.nudge / 100.0f * r.getWidth(); g.fillEllipse(nx - 2, r.getY() + 3, 4, 4); }   // micro-timing dot
                    if (st.prob < 100) { g.setColour(BG); g.setFont(font(9.0f)); g.drawText(juce::String(st.prob) + "%", r.reduced(1), juce::Justification::centred); }
                    if (st.slice) { g.setColour(BG); g.setFont(font(9.0f)); g.drawText(juce::String(st.slice), r.reduced(2, 1), juce::Justification::topLeft); }
                    if (st.rate || st.velRamp || st.pitchRamp || st.cents || st.offset || st.cut || st.reversed() || st.accel || st.destroy || st.freeze || st.frev) {   // tracker effects marker
                        juce::Path tri; tri.addTriangle(r.getRight() - 7, r.getY(), r.getRight(), r.getY(), r.getRight(), r.getY() + 7); g.setColour(AMBER); g.fillPath(tri); }
                    if (st.flam) { g.setColour(BG); g.drawVerticalLine((int) r.getX() + 3, r.getY() + 3, r.getBottom() - 3); g.drawVerticalLine((int) r.getX() + 5, r.getY() + 3, r.getBottom() - 3); }
                    if (st.pitch) { g.setColour(BG); g.setFont(font(8.5f)); g.drawText((st.pitch > 0 ? "+" : "") + juce::String(st.pitch), r.reduced(2, 1), juce::Justification::bottomRight); }
                }
                if (st.locked() || ed.proc.protectedStep(P, t, s)) { g.setColour((st.locked() ? AMBER : CYAN).withAlpha(st.vel ? 0.9f : 0.3f)); g.drawRoundedRectangle(r.reduced(0.5f), 3, 1.0f); }   // (a locked empty step: faint)   // amber = step lock, cyan = phrase / motif lock
                if (s == now) { if (SUMI) { g.setColour(RED.withAlpha(0.12f)); g.fillRoundedRectangle(r, 2); g.setColour(RED.withAlpha(0.9f)); g.drawRoundedRectangle(r, 2, 1.7f); } else if (NEON) { g.setColour(AMBER.withAlpha(0.18f)); g.fillRoundedRectangle(r.expanded(1.5f), 4); g.setColour(AMBER.withAlpha(0.85f)); g.drawRoundedRectangle(r, 3, 1.6f); } else { g.setColour(INK.withAlpha(0.65f)); g.drawRoundedRectangle(r, 3, 1.6f); } }
                if (hasKeyboardFocus(false) && t == curT && s == curS) { g.setColour(GREEN); g.drawRoundedRectangle(r.expanded(1), 4, 1.4f); }
                if (selPat == &P && isSel(t, s)) { g.setColour(INK.withAlpha(0.25f)); g.fillRoundedRectangle(r, 3); g.setColour(INK); g.drawRoundedRectangle(r.expanded(1.5f), 4, 2.2f); }   // selected
            }
        }
        for (int c = 0; c <= 16; c += 4) { g.setColour(INK.withAlpha(0.16f)); g.drawVerticalLine((int) (c * cw), 0, (float) getHeight()); }
        bool empty = true; for (int t = 0; t < gb::kTracks && empty; ++t) for (int s2 = 0; s2 < P.lenOf(t); ++s2) if (P.steps[(size_t) t][(size_t) s2].vel) { empty = false; break; }
        if (empty) {   // an empty pattern says what it can do
            auto r = getLocalBounds().toFloat().withSizeKeepingCentre(juce::jmin(420.0f, getWidth() - 20.0f), 64.0f);
            g.setColour(CARD.withAlpha(0.85f)); g.fillRoundedRectangle(r, 10); g.setColour(PURPLE.withAlpha(0.5f)); g.drawRoundedRectangle(r, 10, 1.2f);
            g.setColour(INK); g.setFont(font(14.0f)); g.drawText("Click cells to draw a beat", r.withTrimmedBottom(30), juce::Justification::centredBottom);
            g.setColour(GREY); g.setFont(font(12.0f, false)); g.drawText("or drag an audio loop here to turn it into MIDI", r.withTrimmedTop(36), juce::Justification::centredTop);
        }
    }
    bool cellAt(juce::Point<float> p, int& t, int& s) { t = (int) (p.y / rowH()); const int c = (int) (p.x / colW()); s = ed.stepPage * 16 + c; return t >= 0 && t < gb::kTracks && c >= 0 && c < 16 && s < ed.pat().lenOf(t); }
    void select(int t) { if (t != ed.track) { ed.track = t; if (ed.refresh) ed.refresh(); } }
    void mouseDown(const juce::MouseEvent& e) override {
        grabKeyboardFocus(); validateSel(); int t, s; if (!cellAt(e.position, t, s)) return;
        select(t); curT = t; curS = s; dragging = false; painting = false;
        auto& st = ed.pat().steps[(size_t) t][(size_t) s];
        if (e.mods.isAltDown()) { StepEditor::open(ed, t, s, *this, localAreaToGlobal(juce::Rectangle<int>((int) (s % 16 * colW()), (int) (t * rowH()), (int) colW(), (int) rowH()))); return; }
        if (e.mods.isRightButtonDown()) {   // right-click / right-drag deletes
            ed.proc.pushUndo(); erasing = true; painting = true; paintVel = 0; lastT = t; lastS = s;
            sel.erase(std::remove_if(sel.begin(), sel.end(), [&](auto& q) { return q.first == t && q.second == s; }), sel.end()); apply(t, s); return; }
        if (st.vel) {   // a note: select it (cmd = add / remove), then a drag moves the selection (shift-drag copies it)
            if (e.mods.isCommandDown()) { if (isSel(t, s)) sel.erase(std::remove_if(sel.begin(), sel.end(), [&](auto& q) { return q.first == t && q.second == s; }), sel.end()); else sel.push_back({ t, s }); repaint(); return; }
            if (! isSel(t, s)) sel = { { t, s } };
            dragging = true; dragCopy = e.mods.isShiftDown(); dragMoved = false; downT = t; downS = s; dragDT = dragDS = 0; dragOrig = ed.pat();
            ed.proc.auditionTrack(t, st.vel / 127.0f); repaint(); return;
        }
        if (! sel.empty() && ! e.mods.isCommandDown()) { sel.clear(); repaint(); return; }   // with notes selected, a click on an empty spot just deselects (and marks where cmd+V goes)
        ed.proc.pushUndo();
        erasing = false; painting = true; lastT = t; lastS = s;
        paintVel = (uint8_t) (e.mods.isShiftDown() ? 127 : 100);   // click / drag on empty cells draws notes; shift = accent
        apply(t, s);
    }
    void dragTo(int t, int s) {   // move (or copy) the selection by the mouse's offset in cells; the pattern is rebuilt from where the drag started
        int dt = t - downT, ds = s - downS;
        for (auto& q : sel) { dt = juce::jlimit(-q.first, gb::kTracks - 1 - q.first, dt); }
        for (auto& q : sel) { const int len = ed.pat().lenOf(juce::jlimit(0, gb::kTracks - 1, q.first + dt)); ds = juce::jlimit(-q.second, len - 1 - q.second, ds); }
        if (dt == dragDT && ds == dragDS) return;
        if (! dragMoved) { ed.proc.pushUndo(); dragMoved = true; }
        dragDT = dt; dragDS = ds; auto& P = ed.pat(); P.steps = dragOrig.steps;
        if (! dragCopy) for (auto& q : sel) { auto& o = P.steps[(size_t) q.first][(size_t) q.second]; const auto fl = o.flags; o = gb::Step(); o.flags = (uint8_t) (fl & 2); }
        for (auto& q : sel) P.steps[(size_t) (q.first + dt)][(size_t) (q.second + ds)] = dragOrig.steps[(size_t) q.first][(size_t) q.second];
        ed.live(); repaint();
    }
    void apply(int t, int s) {
        auto& st = ed.pat().steps[(size_t) t][(size_t) s];
        if (paintVel == 0) { if (!st.vel) return; const auto fl = st.flags; st = gb::Step(); st.flags = (uint8_t) (fl & 2); }
        else { if (st.vel == paintVel) return; if (!st.vel) { const auto fl = st.flags; st = gb::Step(); st.flags = fl; } st.vel = paintVel; ed.proc.auditionTrack(t, paintVel / 127.0f); }
        ed.live();
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        int t, s;
        if (dragging) { const int c = juce::jlimit(0, 15, (int) (e.position.x / colW())); t = juce::jlimit(0, gb::kTracks - 1, (int) (e.position.y / rowH())); dragTo(t, ed.stepPage * 16 + c); return; }
        if (!painting) return; if (cellAt(e.position, t, s) && t == lastT && s != lastS) { lastS = s; apply(t, s); } }
    void mouseUp(const juce::MouseEvent&) override {
        painting = false;
        if (dragging) { dragging = false; if (dragMoved) { for (auto& q : sel) { q.first += dragDT; q.second += dragDS; } curT = sel.front().first; curS = sel.front().second; select(curT);
                                                       if (ed.say) ed.say(juce::String(dragCopy ? "Copied " : "Moved ") + juce::String((int) sel.size()) + (sel.size() == 1 ? " note." : " notes.")); }
                        repaint(); }
    }
    void mouseDoubleClick(const juce::MouseEvent& e) override { int t, s; if (cellAt(e.position, t, s)) { auto& st = ed.pat().steps[(size_t) t][(size_t) s]; if (!st.vel) ed.edit([&] { st.vel = 100; }); StepEditor::open(ed, t, s, *this, localAreaToGlobal(juce::Rectangle<int>((int) (s % 16 * colW()), (int) (t * rowH()), (int) colW(), (int) rowH()))); } }
    bool keyPressed(const juce::KeyPress& k) override {
        auto& P = ed.pat(); validateSel();
        const bool cmd = k.getModifiers().isCommandDown(); const auto ch = juce::CharacterFunctions::toLowerCase(k.getTextCharacter() ? k.getTextCharacter() : (juce::juce_wchar) k.getKeyCode());
        if (cmd && (ch == 'c' || ch == 'x')) {   // copy / cut the selected notes (or the one under the cursor)
            auto from = sel; if (from.empty() && P.steps[(size_t) curT][(size_t) curS].vel) from = { { curT, curS } };
            if (from.empty()) return true;
            int t0 = gb::kTracks, s0 = gb::kMaxSteps; for (auto& q : from) { t0 = juce::jmin(t0, q.first); s0 = juce::jmin(s0, q.second); }
            clip.clear(); for (auto& q : from) clip.push_back({ q.first - t0, q.second - s0, P.steps[(size_t) q.first][(size_t) q.second] });
            if (ch == 'x') { ed.edit([&] { for (auto& q : from) { auto& o = P.steps[(size_t) q.first][(size_t) q.second]; const auto fl = o.flags; o = gb::Step(); o.flags = (uint8_t) (fl & 2); } }); sel.clear(); }
            if (ed.say) ed.say(juce::String(ch == 'x' ? "Cut " : "Copied ") + juce::String((int) clip.size()) + (clip.size() == 1 ? " note" : " notes") + ": click a spot and press cmd+V.");
            repaint(); return true; }
        if (cmd && ch == 'v') {   // paste: the copied notes' top-left lands on the spot you clicked
            if (clip.empty()) return true; std::vector<std::pair<int, int>> put;
            ed.edit([&] { for (auto& c : clip) { const int t = curT + c.dt, s = curS + c.ds; if (t < gb::kTracks && s < P.lenOf(juce::jmin(t, gb::kTracks - 1))) { P.steps[(size_t) t][(size_t) s] = c.st; put.push_back({ t, s }); } } });
            sel = put; if (ed.say) ed.say("Pasted " + juce::String((int) put.size()) + (put.size() == 1 ? " note." : " notes.")); repaint(); return true; }
        if (cmd && ch == 'a') { sel.clear(); for (int t = 0; t < gb::kTracks; ++t) for (int s = 0; s < P.lenOf(t); ++s) if (P.steps[(size_t) t][(size_t) s].vel) sel.push_back({ t, s }); repaint(); return true; }
        if (k == juce::KeyPress::escapeKey && ! sel.empty()) { sel.clear(); repaint(); return true; }
        if ((k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) && ! sel.empty()) {
            ed.edit([&] { for (auto& q : sel) { auto& o = P.steps[(size_t) q.first][(size_t) q.second]; const auto fl = o.flags; o = gb::Step(); o.flags = (uint8_t) (fl & 2); } }); sel.clear(); repaint(); return true; }
        if (k == juce::KeyPress::leftKey) curS = juce::jmax(0, curS - 1); else if (k == juce::KeyPress::rightKey) curS = juce::jmin(P.lenOf(curT) - 1, curS + 1);
        else if (k == juce::KeyPress::upKey) curT = juce::jmax(0, curT - 1); else if (k == juce::KeyPress::downKey) curT = juce::jmin(gb::kTracks - 1, curT + 1);
        else if (k == juce::KeyPress::returnKey) { auto& st = P.steps[(size_t) curT][(size_t) curS]; ed.edit([&] { if (st.vel) st = gb::Step(); else st.vel = 100; }); }
        else if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) { auto& st = P.steps[(size_t) curT][(size_t) curS]; ed.edit([&] { st = gb::Step(); }); }
        else return false;
        curS = juce::jmin(curS, P.lenOf(curT) - 1); ed.stepPage = curS / 16; select(curT); repaint(); return true;
    }
};

// ---------------- lane editors under the grid (selected track): drawable bars
struct LaneEditor : juce::Component {
    Ed& ed; int lane = 0; bool drawing = false;
    static const juce::StringArray& names() { static juce::StringArray n { "VEL", "NUDGE", "PITCH", "CHANCE", "HITS", "FLAM", "DECAY", "RETRIG", "DESTR", "FREEZE", "PAN", "FILTER" }; return n; }
    explicit LaneEditor(Ed& e) : ed(e) {}
    void range(int& lo, int& hi) const { static const int L[] { 1, -50, -24, 1, 1, 0, 5, 0, 0, 0, -50, 0 }, Hh[] { 127, 50, 24, 100, 16, 40, 100, 32, 100, 120, 50, 127 }; lo = L[lane]; hi = Hh[lane]; }   // FREEZE lane: 0..1200 ms in 10 ms steps
    int get(const gb::Step& s) const { switch (lane) { case 0: return s.vel; case 1: return s.nudge; case 2: return s.pitch; case 3: return s.prob; case 4: return s.roll; case 5: return s.flam; case 6: return s.decay; case 7: return s.rate; case 8: return s.destroy; case 9: return s.freeze; case 10: return s.pan; default: return s.filt; } }
    void set(gb::Step& s, int v) const { switch (lane) { case 0: s.vel = (uint8_t) v; break; case 1: s.nudge = (int8_t) v; break; case 2: s.pitch = (int8_t) v; break; case 3: s.prob = (uint8_t) v; break;
                                                      case 4: s.roll = (uint8_t) v; break; case 5: s.flam = (uint8_t) v; break; case 6: s.decay = (uint8_t) v; break; case 7: s.rate = (uint8_t) v; break; case 8: s.destroy = (uint8_t) v; break; case 9: s.freeze = (uint8_t) v; break; case 10: s.pan = (int8_t) v; break; default: s.filt = (uint8_t) v; } }
    void paint(juce::Graphics& g) override {
        auto R = getLocalBounds().toFloat();
        if (GLASS) glassui::sprites().draw(g, R, 10.0f, glassui::Well); else { g.setColour(CARD2.withAlpha(0.8f)); g.fillRoundedRectangle(R, 8); }
        int lo, hi; range(lo, hi); const float cw = R.getWidth() / 16.0f; const bool bip = lo < 0;
        const float zeroY = bip ? R.getCentreY() : R.getBottom() - 4;
        if (bip) { g.setColour(INK.withAlpha(0.15f)); g.drawHorizontalLine((int) zeroY, 0, R.getRight()); }
        auto& P = ed.pat(); const auto col = trackColour(ed.track); const int L = P.lenOf(ed.track);
        for (int c = 0; c < 16; ++c) {
            const int s = ed.stepPage * 16 + c; if (s >= L) continue;
            const auto& st = P.steps[(size_t) ed.track][(size_t) s];
            auto cell = juce::Rectangle<float>(c * cw + 3, 4, cw - 6, R.getHeight() - 8);
            g.setColour(INK.withAlpha(0.04f)); g.fillRoundedRectangle(cell, 3);
            if (!st.vel && lane != 0) continue;
            const int v = get(st); const float y = juce::jmap((float) v, (float) lo, (float) hi, cell.getBottom(), cell.getY());
            auto bar = bip ? juce::Rectangle<float>(cell.getX(), juce::jmin(y, zeroY), cell.getWidth(), std::abs(zeroY - y)) : juce::Rectangle<float>(cell.getX(), y, cell.getWidth(), cell.getBottom() - y);
            if (GLASS && st.vel && bar.getHeight() >= 3.0f) { ghosty::glass::body(g, bar, 3.0f, col, 0.72f, 0.6f); }
            else { g.setColour(col.withAlpha(st.vel ? 0.8f : 0.2f)); g.fillRoundedRectangle(bar, 2); }
            if (st.vel) { g.setColour(INK.withAlpha(0.8f)); g.setFont(font(9.5f)); g.drawText(lane == 9 ? (v ? juce::String(v * 10) : juce::String("-")) : (lane == 8 ? juce::String(v) + "%" : (lane == 11 ? (v == 0 || v == 64 ? juce::String("-") : (v < 64 ? "LP" + juce::String(v) : "HP" + juce::String(v - 64))) : juce::String(v))), cell.withHeight(12).translated(0, bip && v < 0 ? 0 : cell.getHeight() - 13), juce::Justification::centred); }
        }
        g.setColour(GREY.withAlpha(0.6f)); g.setFont(font(9.5f)); g.drawText(juce::String(hi), 4, 2, 40, 12, juce::Justification::left); g.drawText(juce::String(lo), 4, (int) R.getBottom() - 14, 40, 12, juce::Justification::left);
    }
    void put(juce::Point<float> p) {
        int lo, hi; range(lo, hi); const float cw = getWidth() / 16.0f; const int c = (int) (p.x / cw), s = ed.stepPage * 16 + c;
        if (c < 0 || c >= 16 || s >= ed.pat().lenOf(ed.track)) return;
        auto& st = ed.pat().steps[(size_t) ed.track][(size_t) s];
        if (!st.vel && lane != 0) return;
        const int v = juce::jlimit(lo, hi, (int) std::lround(juce::jmap(p.y, (float) getHeight() - 4, 4.0f, (float) lo, (float) hi)));
        if (get(st) != v) { set(st, v); ed.live(); }
    }
    int defaultValue() const { switch (lane) { case 0: return 100; case 3: return 100; case 4: return 1; case 6: return 100; default: return 0; } }
    int stepAt(float x) const { const int c = (int) (x / (getWidth() / 16.0f)), s = ed.stepPage * 16 + c; return c < 0 || c >= 16 || s >= ed.pat().lenOf(ed.track) ? -1 : s; }
    int fineStep = -1, fineFrom = 0; float fineY = 0;
    juce::TextEditor typer; int typeStep = -1;
    void mouseDown(const juce::MouseEvent& e) override {   // click = the bar jumps to the cursor (one undo step per gesture); shift = fine; right-click = type
        if (e.mods.isPopupMenu()) { startTyping(stepAt(e.position.x)); return; }
        ed.gestureUndo(this); drawing = true;   // (coalesces with a double-click's reset: one undo step)
        if (e.mods.isShiftDown()) { fineStep = stepAt(e.position.x); if (fineStep >= 0) { fineFrom = get(ed.pat().steps[(size_t) ed.track][(size_t) fineStep]); fineY = e.position.y; } return; }
        fineStep = -1; put(e.position);
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (!drawing) return;
        if (fineStep >= 0) { int lo, hi; range(lo, hi); auto& st = ed.pat().steps[(size_t) ed.track][(size_t) fineStep]; if (!st.vel && lane != 0) return;
            const int v = juce::jlimit(lo, hi, fineFrom + (int) std::lround((fineY - e.position.y) / juce::jmax(1.0f, (float) getHeight() - 8) * (hi - lo) / 8.0f)); if (get(st) != v) { set(st, v); ed.live(); } return; }
        put(e.position);
    }
    void mouseUp(const juce::MouseEvent&) override { drawing = false; fineStep = -1; }
    void mouseDoubleClick(const juce::MouseEvent& e) override { if (e.mods.isPopupMenu()) return; const int s = stepAt(e.position.x); if (s < 0) return; auto& st = ed.pat().steps[(size_t) ed.track][(size_t) s]; if ((st.vel || lane == 0) && get(st) != defaultValue()) { ed.gestureUndo(this); set(st, defaultValue()); ed.live(); } }
    void startTyping(int s) {
        if (s < 0) return; const auto& st = ed.pat().steps[(size_t) ed.track][(size_t) s]; if (!st.vel && lane != 0) return;
        if (typer.getParentComponent() != this) { addChildComponent(typer); typer.setJustification(juce::Justification::centred); typer.setFont(font(12.0f)); typer.setInputRestrictions(5, "-0123456789");
            typer.onReturnKey = [this] { commitTyped(); }; typer.onFocusLost = [this] { commitTyped(); }; typer.onEscapeKey = [this] { typer.setVisible(false); }; }
        typeStep = s; const float cw = getWidth() / 16.0f; const int c = s - ed.stepPage * 16;
        typer.setBounds(juce::Rectangle<int>((int) (c * cw), getHeight() / 2 - 13, (int) cw, 26).expanded(10, 0).constrainedWithin(getLocalBounds()));
        typer.setText(juce::String(lane == 9 ? get(st) * 10 : get(st)), false); typer.setVisible(true);   // (FREEZE in ms)
        typer.grabKeyboardFocus(); typer.selectAll();
    }
    void commitTyped() {
        if (!typer.isVisible()) return; typer.setVisible(false); const auto t = typer.getText().trim(); if (typeStep < 0 || typeStep >= ed.pat().lenOf(ed.track) || !t.containsAnyOf("0123456789")) return;
        int lo, hi; range(lo, hi); auto& st = ed.pat().steps[(size_t) ed.track][(size_t) typeStep]; const int v = juce::jlimit(lo, hi, lane == 9 ? (int) std::lround(t.getIntValue() / 10.0) : t.getIntValue());
        if (get(st) != v) { ed.gestureUndo(this); set(st, v); ed.live(); }
    }
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override {
        const int s = stepAt(e.position.x); if (s < 0) return; int lo, hi; range(lo, hi); auto& st = ed.pat().steps[(size_t) ed.track][(size_t) s]; if (!st.vel && lane != 0) return;
        const int v = juce::jlimit(lo, hi, get(st) + ((w.deltaY + w.deltaX) > 0 ? 1 : -1)); if (v != get(st)) { ed.gestureUndo(this); set(st, v); ed.live(); }
    }
};

// ---------------- track list: click = select + hear; drop audio / a sound on a row; M / S; MIDI + WAV drag handles
struct TrackList : juce::Component, juce::DragAndDropTarget {
    Ed& ed; uint32_t last[gb::kTracks] {}; float glow[gb::kTracks] {}; int dropHover = -1; bool handleDrag = false;
    std::function<void(int track, bool midi)> onDragOut;
    std::function<void(int track)> onMenu;
    explicit TrackList(Ed& e) : ed(e) {}
    float rowH() const { return getHeight() / (float) gb::kTracks; }
    juce::Rectangle<float> rowRect(int t) const { return { 0, t * rowH() + 1.5f, (float) getWidth(), rowH() - 3 }; }
    juce::Rectangle<float> midiHandle(int t) const { auto r = rowRect(t); return { r.getRight() - 96, r.getY() + 1, 26, r.getHeight() - 2 }; }
    juce::Rectangle<float> lockBox(int t) const { auto r = rowRect(t); return { r.getRight() - 112, r.getY() + 3, 13, r.getHeight() - 6 }; }   // LINEAR lock lane
    juce::Rectangle<float> wavHandle(int t) const { return midiHandle(t).translated(28, 0); }
    juce::Rectangle<float> muteBox(int t) const { return midiHandle(t).translated(58, 0).withWidth(17); }
    juce::Rectangle<float> soloBox(int t) const { return muteBox(t).translated(19, 0); }
    void paint(juce::Graphics& g) override {
        for (int t = 0; t < gb::kTracks; ++t) {
            const auto& tr = ed.proc.model.tracks[(size_t) t]; auto r = rowRect(t);
            const uint32_t tc = ed.proc.engine.trig[t].load(); if (tc != last[t]) glow[t] = 1.0f; last[t] = tc; glow[t] *= 0.82f;
            if (GLASS) { glassui::sprites().draw(g, r, 5.0f, glassui::Row); if (t == dropHover || t == ed.track) { g.setColour(t == dropHover ? GREEN.withAlpha(0.22f) : PURPLE.withAlpha(0.16f)); g.fillRoundedRectangle(r, 5.0f); } }
            else { g.setColour(t == dropHover ? GREEN.withAlpha(0.3f) : (t == ed.track ? PURPLE.withAlpha(0.25f) : CARD2)); g.fillRoundedRectangle(r, 4); }
            g.setColour(trackColour(t).withAlpha(0.35f + 0.65f * glow[t])); g.fillRoundedRectangle(r.withWidth(4), 2);
            g.setColour(tr.mute ? DIM : INK); g.setFont(font(11.0f));
            juce::String name = tr.sound.isEmpty() ? juce::String("(drop a sample)") : tr.sound;
            g.drawText(juce::String(t + 1).paddedLeft('0', 2) + " " + name, r.withTrimmedLeft(8).withTrimmedRight(116), juce::Justification::centredLeft, true);
            { const bool on = ed.proc.model.linear.on, lk = ed.proc.model.linear.lock >> t & 1;   // LINEAR lock lane: shown dimmed while LINEAR is off (a saved lock is never a surprise)
              if (on || lk) { g.setColour(lk ? AMBER.withAlpha(on ? 1.0f : 0.4f) : DIM.withAlpha(0.45f)); g.fillRoundedRectangle(lockBox(t), 2); g.setColour(lk ? ON.withAlpha(on ? 1.0f : 0.7f) : GREY); g.setFont(font(8.0f)); g.drawText("L", lockBox(t), juce::Justification::centred); } }
            if (tr.sample || !tr.slices.empty()) { g.setColour(tr.slices.empty() ? AMBER : CYAN); g.fillEllipse(r.getRight() - 104, r.getCentreY() - 2, 4, 4); }
            {   // TRACK LEVEL: a thin peak meter along the bottom of the row (the engine's per-track meter, decays by itself)
                const float pk = ed.proc.engine.trackPeak[t].load(); if (pk > 0.0005f) { const float db = juce::Decibels::gainToDecibels(pk, -60.0f), u = juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 66.0f);
                    auto m = juce::Rectangle<float>(r.getX() + 8, r.getBottom() - 3.5f, (r.getWidth() - 110) * u, 2.5f); g.setColour((pk > 0.95f ? RED : (pk > 0.7f ? AMBER : trackColour(t))).withAlpha(0.85f)); g.fillRoundedRectangle(m, 1.2f); } }
            for (auto [b, txt] : { std::pair<juce::Rectangle<float>, const char*> { midiHandle(t), "MIDI" }, { wavHandle(t), "WAV" } }) {
                g.setColour(GREEN.withAlpha(0.12f)); g.fillRoundedRectangle(b, 3); g.setColour(GREEN.withAlpha(0.7f)); g.drawRoundedRectangle(b, 3, 0.8f);
                g.setFont(font(7.5f)); g.drawText(txt, b, juce::Justification::centred); }
            g.setColour(tr.mute ? RED : DIM.withAlpha(0.5f)); g.fillRoundedRectangle(muteBox(t), 3);
            g.setColour(tr.solo ? AMBER : DIM.withAlpha(0.5f)); g.fillRoundedRectangle(soloBox(t), 3);
            g.setFont(font(9.5f)); g.setColour(tr.mute ? ON : GREY); g.drawText("M", muteBox(t), juce::Justification::centred); g.setColour(tr.solo ? ON : GREY); g.drawText("S", soloBox(t), juce::Justification::centred);
        }
    }
    int rowAt(float y) const { return juce::jlimit(0, gb::kTracks - 1, (int) (y / rowH())); }
    void mouseDown(const juce::MouseEvent& e) override {
        const int t = rowAt(e.position.y); auto& tr = ed.proc.model.tracks[(size_t) t]; handleDrag = false;
        if ((ed.proc.model.linear.on || (ed.proc.model.linear.lock >> t & 1)) && lockBox(t).contains(e.position)) { ed.edit([&] { ed.proc.model.linear.lock ^= (uint16_t) (1u << t); }); if (ed.say) ed.say(juce::String("LINEAR lock lane ") + (ed.proc.model.linear.lock >> t & 1 ? "on" : "off") + ": track " + juce::String(t + 1) + (ed.proc.model.linear.lock >> t & 1 ? " always survives a collision." : " competes like the others.")); return; }
        if (muteBox(t).contains(e.position)) { ed.edit([&] { tr.mute = !tr.mute; }); return; }
        if (soloBox(t).contains(e.position)) { ed.edit([&] { tr.solo = !tr.solo; }); return; }
        if (midiHandle(t).contains(e.position) || wavHandle(t).contains(e.position)) { handleDrag = true; return; }
        ed.track = t; if (e.mods.isPopupMenu()) { if (onMenu) onMenu(t); } else ed.proc.auditionTrack(t);
        if (ed.refresh) ed.refresh();
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (!handleDrag || e.getDistanceFromDragStart() < 5) return; handleDrag = false;
        const auto d = e.getMouseDownPosition().toFloat(); const int t = rowAt(d.y);
        if (onDragOut) onDragOut(t, midiHandle(t).contains(d));
    }
    bool isInterestedInDragSource(const SourceDetails& d) override { return d.description.toString().startsWith("sound:"); }
    void itemDragMove(const SourceDetails& d) override { dropHover = rowAt((float) d.localPosition.y); repaint(); }
    void itemDragExit(const SourceDetails&) override { dropHover = -1; repaint(); }
    void itemDropped(const SourceDetails& d) override {
        const int t = rowAt((float) d.localPosition.y); dropHover = -1; const auto name = d.description.toString().fromFirstOccurrenceOf(":", false, false);
        ed.track = t; ed.edit([&] { ed.proc.setTrackSound(t, name); }); ed.proc.auditionTrack(t); if (ed.say) ed.say("Track " + juce::String(t + 1) + " now plays " + name + ".");
    }
};

// ---------------- drag a freshly rendered file out to the DAW (renders in the background while you hover)
// PLAY: ALWAYS | ON NOTES (header, under BYPASS). Two halves; click either (or anywhere to flip). Reads / writes through get / set.
struct PlayModeSwitch : juce::Component, juce::SettableTooltipClient, private juce::Timer {
    std::function<int()> get; std::function<void(int)> set; int shown = -1;
    PlayModeSwitch() { setTooltip("PLAY: ALWAYS = the beat runs whenever the host (or PLAY) runs.\n"
                                  "PLAY: ON NOTES = the beat runs only while a MIDI note is held (any key): draw one long note per bar or pattern. "
                                  "Muting the channel in FL Studio then stops it; the next note starts it again in time.\n"
                                  "In ON NOTES the MIDI notes only start / stop the beat: they do not play the pads or record. The on-screen pads still play. BYPASS wins.");
                       startTimerHz(5); }
    void timerCallback() override { if (get && get() != shown) repaint(); }
    void paint(juce::Graphics& g) override {
        shown = get ? get() : 0; const auto r = getLocalBounds().toFloat().reduced(0.5f); const float rad = r.getHeight() * 0.5f;
        auto half = r.withWidth(r.getWidth() * (shown == 1 ? 0.56f : 0.44f)); if (shown == 1) half.setX(r.getRight() - half.getWidth());
        if (GLASS) { ghosty::glass::recessed(g, r, rad); ghosty::glass::body(g, half.reduced(1.5f), rad - 1.5f, shown == 1 ? AMBER : PURPLE, 0.95f, 0.3f); }
        else {
        g.setColour(CARD2); g.fillRoundedRectangle(r, rad);
        g.setColour(shown == 1 ? AMBER : PURPLE); g.fillRoundedRectangle(half.reduced(1.5f), rad - 1.5f);
        g.setColour(PURPLE.withAlpha(0.45f)); g.drawRoundedRectangle(r, rad, 1.0f); }
        const auto L = r.withWidth(r.getWidth() * 0.44f), R = r.withTrimmedLeft(r.getWidth() * 0.44f);
        g.setFont(font(juce::jmin(10.5f, r.getHeight() * 0.55f)));
        g.setColour(shown == 0 ? ON : GREY); g.drawText("ALWAYS", L, juce::Justification::centred);
        g.setColour(shown == 1 ? ON : GREY); g.drawText("ON NOTES", R, juce::Justification::centred);
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (!set || !get || !getLocalBounds().contains(e.getPosition())) return;
        set(e.x < getWidth() * 0.44f ? 0 : 1); repaint();
    }
};

struct DragOut : juce::Component, private juce::Timer {
    juce::String text, sub; std::function<juce::StringArray()> make; std::function<void()> prepare; std::function<float()> progress; bool started = false, big = false;
    DragOut(const juce::String& t, bool isBig = false) : text(t), big(isBig) { startTimerHz(15); }
    void timerCallback() override { if (isMouseOver() || (progress && progress() < 1.0f)) repaint(); }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat().reduced(1);
        if (GLASS) { ghosty::glass::shadow(g, r, 10.0f, 3.0f, 1.0f, 3); ghosty::glass::body(g, r, 10.0f, juce::Colours::white, isMouseOver() ? 0.55f : 0.44f); g.setColour(GREEN.withAlpha(isMouseOver() ? 0.16f : 0.08f)); g.fillRoundedRectangle(r, 10.0f); }
        else { g.setColour(GREEN.withAlpha(isMouseOver() ? 0.24f : 0.13f)); g.fillRoundedRectangle(r, 8); }
        if (progress) { const float p = progress(); if (p < 1.0f) { g.setColour(GREEN.withAlpha(0.35f)); g.fillRoundedRectangle(r.withWidth(r.getWidth() * p), 8); } }
        g.setColour(GREEN.withAlpha(0.9f)); g.drawRoundedRectangle(r, 8, big ? 1.8f : 1.2f);
        // a little "grab me" arrow
        juce::Path a; const float cx = r.getX() + 16, cy = r.getCentreY(); a.startNewSubPath(cx, cy - 7); a.lineTo(cx, cy + 7); a.startNewSubPath(cx - 5, cy + 2); a.lineTo(cx, cy + 7); a.lineTo(cx + 5, cy + 2);
        g.strokePath(a, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setFont(font(big ? juce::jmin(14.0f, (r.getWidth() - 28) / 7.0f) : 11.0f)); g.drawText(text, r.withTrimmedLeft(24).withTrimmedBottom(sub.isNotEmpty() ? r.getHeight() * 0.38f : 0), juce::Justification::centred);
        if (sub.isNotEmpty()) { g.setColour(GREEN.withAlpha(0.65f)); g.setFont(font(9.0f, false)); g.drawText(sub, r.withTrimmedLeft(22).withTrimmedTop(r.getHeight() * 0.55f), juce::Justification::centred, true); }
    }
    void mouseEnter(const juce::MouseEvent&) override { if (prepare) prepare(); }
    void mouseDown(const juce::MouseEvent&) override { started = false; if (prepare) prepare(); }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (started || e.getDistanceFromDragStart() < 6) return; started = true;
        auto files = make(); if (!files.isEmpty()) ghosty::exportgate::dragFiles(files, this);   // make() asked the demo export gate
    }
};

// ---------------- sound browser: folders + search + list (click = hear, double-click / drag = load), favourites
struct SoundList : juce::Component, juce::TooltipClient {
    Ed& ed; std::vector<int> items; int selected = -1, hover = -1; static constexpr int rowH = 22; int cols = 3;
    int gripDown = -1; bool gripDragged = false, overGrip = false;   // ⇣ (owner 2026-11-02): drag the sound into the DAW as a WAV
    std::function<void(const juce::StringArray&)> performDrag;       // (tests swap it) default: drag the file out of the plugin
    std::function<void(int)> onPick;
    explicit SoundList(Ed& e) : ed(e) {}
    juce::String nameAt(int i) const { return ed.proc.lib->sounds()[(size_t) items[(size_t) i]]->name; }
    juce::Rectangle<int> cell(int i) const { const int cw = getWidth() / cols; return { (i % cols) * cw, (i / cols) * rowH, cw, rowH }; }
    juce::Rectangle<int> star(int i) const { auto c = cell(i); return c.removeFromRight(20); }
    juce::Rectangle<int> grip(int i) const { auto c = cell(i); c.removeFromRight(20); return c.removeFromRight(18); }   // ⇣, left of the star
    juce::String getTooltip() override { return overGrip ? juce::String::fromUTF8("drag \xe2\x87\xa3 into your DAW for the WAV") : juce::String(); }
    void dragOut(int i) {   // the whole sound as a 24-bit WAV, named after it (demo gate)
        if (!ed.proc.exportGate(this, "beats.sound-wav-drag")) { if (ed.say) ed.say("Drag-out is part of the full version (UNLOCK)."); return; }
        const auto f = ed.proc.exportSound(nameAt(i)); if (!f.existsAsFile()) { if (ed.say) ed.say("Can't read that sound."); return; }
        if (performDrag) performDrag({ f.getFullPathName() }); else ghosty::exportgate::dragFiles(ed.proc.license, { f.getFullPathName() }, this);
    }
    void layout() { cols = juce::jmax(3, (getParentWidth() > 0 ? getParentWidth() : getWidth()) / 165); setSize(getParentWidth() > 0 ? getParentWidth() - 10 : getWidth(), juce::jmax(40, ((int) items.size() + cols - 1) / cols * rowH)); repaint(); }
    void paint(juce::Graphics& g) override {
        for (int i = 0; i < (int) items.size(); ++i) {
            auto c = cell(i).reduced(2, 1).toFloat(); const auto& s = ed.proc.lib->sounds()[(size_t) items[(size_t) i]];
            if (GLASS) { glassui::sprites().draw(g, c, 5.0f, glassui::Row); if (i == selected || i == hover) { g.setColour(PURPLE.withAlpha(i == selected ? 0.22f : 0.07f)); g.fillRoundedRectangle(c, 5.0f); } }
            else { g.setColour(i == selected ? PURPLE.withAlpha(0.45f) : (i == hover ? CARD2.brighter(0.15f) : CARD2)); g.fillRoundedRectangle(c, 4); }
            g.setColour(INK); g.setFont(font(11.0f, false)); g.drawText(s->name, c.withTrimmedLeft(6).withTrimmedRight(40), juce::Justification::centredLeft, true);
            { const auto gr = grip(i).toFloat().reduced(2.0f, 3.0f); const bool on = i == hover;   // ⇣: an arrow down onto a line
              g.setColour(GREEN.withAlpha(on ? 0.22f : 0.08f)); g.fillRoundedRectangle(gr, 3.0f); g.setColour(GREEN.withAlpha(on ? 1.0f : 0.55f));
              const float cx = gr.getCentreX(), t = gr.getY() + 3.0f, b = gr.getBottom() - 4.5f; g.drawLine(cx, t, cx, b, 1.4f);
              juce::Path a; a.addTriangle(cx - 3.2f, b - 3.2f, cx + 3.2f, b - 3.2f, cx, b + 0.6f); g.fillPath(a); g.drawLine(cx - 4.0f, b + 2.2f, cx + 4.0f, b + 2.2f, 1.2f); }
            const bool fav = ed.proc.favourites.contains(s->name); g.setColour(fav ? AMBER : DIM); g.setFont(font(13.0f)); g.drawText(fav ? "*" : "+", star(i).toFloat(), juce::Justification::centred);
        }
        if (items.empty()) { g.setColour(GREY); g.setFont(font(12.0f, false)); g.drawText("Nothing here yet.", getLocalBounds(), juce::Justification::centredTop); }
    }
    int at(juce::Point<int> p) const { for (int i = 0; i < (int) items.size(); ++i) if (cell(i).contains(p)) return i; return -1; }
    void mouseMove(const juce::MouseEvent& e) override { const int h = at(e.getPosition()); overGrip = h >= 0 && grip(h).contains(e.getPosition()); setMouseCursor(overGrip ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor); if (h != hover) { hover = h; repaint(); } }
    void mouseExit(const juce::MouseEvent&) override { hover = -1; overGrip = false; repaint(); }
    void mouseUp(const juce::MouseEvent&) override { if (gripDown >= 0 && !gripDragged && ed.say) ed.say(juce::String::fromUTF8("drag \xe2\x87\xa3 into your DAW for the WAV")); gripDown = -1; }
    void mouseDown(const juce::MouseEvent& e) override {
        const int i = at(e.getPosition()); gripDown = -1; gripDragged = false; if (i < 0) return;
        if (grip(i).contains(e.getPosition())) { gripDown = i; selected = i; ed.proc.previewSound(nameAt(i)); if (onPick) onPick(i); repaint(); return; }   // ⇣: a drag = the WAV
        if (star(i).contains(e.getPosition())) { auto& f = ed.proc.favourites; const auto n = nameAt(i); if (f.contains(n)) f.removeString(n); else f.add(n); ed.proc.saveSettings(); repaint(); return; }
        selected = i; ed.proc.previewSound(nameAt(i)); if (onPick) onPick(i); repaint();
    }
    void mouseDoubleClick(const juce::MouseEvent& e) override { const int i = at(e.getPosition()); if (i < 0) return; const auto n = nameAt(i); const int t = ed.track;
        ed.edit([&] { ed.proc.setTrackSound(t, n); }); if (ed.say) ed.say("Track " + juce::String(t + 1) + " now plays " + n + "."); }
    void mouseDrag(const juce::MouseEvent& e) override { if (gripDown >= 0) { if (!gripDragged && e.getDistanceFromDragStart() > 5) { gripDragged = true; dragOut(gripDown); } return; }
        const int i = at(e.getMouseDownPosition()); if (i >= 0 && e.getDistanceFromDragStart() > 6) if (auto* c = juce::DragAndDropContainer::findParentDragContainerFor(this)) if (!c->isDragAndDropActive()) { const auto n = nameAt(i); juce::Image img(juce::Image::ARGB, juce::jmin(260, 34 + (int) (n.length() * 7.2)), 26, true);
            { juce::Graphics g(img); g.setColour(juce::Colour(0xee1b1530)); g.fillRoundedRectangle(img.getBounds().toFloat().reduced(1), 8); g.setColour(juce::Colour(0xff00ff88)); g.drawRoundedRectangle(img.getBounds().toFloat().reduced(1), 8, 1.5f);
              g.setColour(juce::Colours::white); g.setFont(juce::FontOptions(12.5f, juce::Font::bold)); g.drawText(n, img.getBounds().reduced(12, 0), juce::Justification::centredLeft, true); }
            const juce::Point<int> off(-12, -13); c->startDragging("sound:" + n, this, juce::ScaledImage(img), false, &off); } }
};

// ---------------- slice editor: the selected track's sound with its slice lines (click = add, drag = move, right-click = remove)
struct SliceView : juce::Component, juce::SettableTooltipClient {
    Ed& ed; int dragIdx = -1;
    explicit SliceView(Ed& e) : ed(e) { setTooltip("SLICES: click = add a slice line, drag a line = move it, right-click a line = remove it, drag a slice out = that slice as a WAV"); }
    gb::SoundPtr snd() const { const auto& tr = ed.proc.model.tracks[(size_t) ed.track]; return tr.sample ? tr.sample : ed.proc.lib->loaded(tr.sound); }
    void paint(juce::Graphics& g) override {
        auto R = getLocalBounds().toFloat();
        if (GLASS) ghosty::glass::recessed(g, R, 8.0f); else { g.setColour(CARD); g.fillRoundedRectangle(R, 6); }
        const auto s = snd(); if (!s || s->data.empty()) return;
        const int n = (int) s->data.size(), w = getWidth(); const float mid = R.getCentreY(), h = R.getHeight() * 0.45f;
        g.setColour(trackColour(ed.track).withAlpha(0.7f));
        for (int x = 0; x < w; ++x) { const int a = (int) ((juce::int64) x * n / w), b = juce::jmax(a + 1, (int) ((juce::int64) (x + 1) * n / w)); float mx = 0; for (int k = a; k < b && k < n; ++k) mx = juce::jmax(mx, std::abs(s->data[(size_t) k])); g.drawVerticalLine(x, mid - mx * h, mid + mx * h); }
        const auto& sl = ed.proc.model.tracks[(size_t) ed.track].slices;
        for (size_t i = 0; i < sl.size(); ++i) { const float x = (float) sl[i] / n * w; g.setColour(CYAN); g.drawVerticalLine((int) x, 0, R.getBottom()); g.setFont(font(9.0f)); g.drawText(juce::String((int) i + 1), (int) x + 2, 1, 20, 10, juce::Justification::left); }
        if (sl.empty()) { g.setColour(GREY); g.setFont(font(10.0f, false)); g.drawText("CHOP to slice this sound (click to add lines)", R.reduced(4), juce::Justification::centredBottom); }
        else { g.setColour(GREY); g.setFont(font(9.0f, false)); g.drawText("drag a slice out = its WAV", R.reduced(4, 2), juce::Justification::bottomRight); }
    }
    int near(float x) const { const auto s = snd(); if (!s) return -1; const auto& sl = ed.proc.model.tracks[(size_t) ed.track].slices; for (size_t i = 1; i < sl.size(); ++i) if (std::abs((float) sl[i] / s->data.size() * getWidth() - x) < 4) return (int) i; return -1; }
    bool dragPushed = false, pendingAdd = false, exported = false;
    std::function<void(const juce::StringArray&)> performDrag;   // (tests swap it) default: drag the files out of the plugin
    // which slice (1..) is under x (0 = not chopped)
    int sliceAt(float x) const { const auto s = snd(); if (!s) return 0; const auto& sl = ed.proc.model.tracks[(size_t) ed.track].slices; if (sl.empty()) return 0;
                                 const int pos = (int) (x / juce::jmax(1, getWidth()) * s->data.size()); int k = 0; for (size_t i = 0; i < sl.size(); ++i) if (sl[i] <= pos) k = (int) i + 1; return k; }
    void mouseDown(const juce::MouseEvent& e) override {
        const auto s = snd(); if (!s) return; auto& sl = ed.tr().slices; const int i = near(e.position.x); dragPushed = false; pendingAdd = false; exported = false;
        if (e.mods.isPopupMenu()) { if (i > 0) { ed.proc.pushUndo(); sl.erase(sl.begin() + i); ed.live(); } return; }
        if (i > 0) { dragIdx = i; return; }   // the undo step comes with the first real move
        pendingAdd = true;   // a click (no drag) adds a slice line on release; a drag OUT of a slice exports that slice as a WAV
    }
    void addLineAt(float x) {
        const auto s = snd(); if (!s) return; auto& sl = ed.tr().slices; if ((int) sl.size() >= 64) return;
        ed.proc.pushUndo(); if (sl.empty()) sl.push_back(0);
        const int pos = juce::jlimit(1, (int) s->data.size() - 2, (int) (x / getWidth() * s->data.size()));
        sl.insert(std::upper_bound(sl.begin(), sl.end(), pos), pos); ed.live();
    }
    void dragSliceOut(float x) {   // SLICE DRAG-OUT: the slice under the mouse as a WAV (demo gate)
        const auto s = snd(); const int k = sliceAt(x); if (!s || k <= 0) return; const auto& sl = ed.tr().slices; const float n = (float) s->data.size();
        const float a = sl[(size_t) k - 1] / n, b = k < (int) sl.size() ? sl[(size_t) k] / n : 1.0f;
        if (!ed.proc.exportGate(this, "beats.slice-wav-drag")) { if (ed.say) ed.say("Drag-out is part of the full version (UNLOCK)."); return; }
        const auto f = ed.proc.exportSoundPart(ed.track, a, b, "slice " + juce::String(k)); if (!f.existsAsFile()) return;
        if (performDrag) performDrag({ f.getFullPathName() }); else ghosty::exportgate::dragFiles(ed.proc.license, { f.getFullPathName() }, this);
    }
    void mouseUp(const juce::MouseEvent& e) override { if (pendingAdd && !exported) addLineAt(e.position.x); pendingAdd = false; dragIdx = -1; }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (pendingAdd) { if (!exported && e.getDistanceFromDragStart() > 8 && !ed.tr().slices.empty()) { exported = true; dragSliceOut(e.getMouseDownPosition().toFloat().x); } return; }
        const auto s = snd(); if (!s || dragIdx <= 0) return; auto& sl = ed.tr().slices; if (dragIdx >= (int) sl.size()) return;
        const int lo = sl[(size_t) dragIdx - 1] + 64, hi = dragIdx + 1 < (int) sl.size() ? sl[(size_t) dragIdx + 1] - 64 : (int) s->data.size() - 64;
        const int v = juce::jlimit(lo, juce::jmax(lo, hi), (int) (e.position.x / getWidth() * s->data.size())); if (v == sl[(size_t) dragIdx]) return;
        if (!dragPushed) { ed.proc.pushUndo(); dragPushed = true; }
        sl[(size_t) dragIdx] = v; ed.live();
    }
};

// hold-to-use performance button
// PERFORM > STUTTER: one strip [1/4][1/8][1/16][1/32]. Press any segment = stutter at that rate while held; drag across the strip to
// switch the rate live (the segment under the cursor lights up; outside the strip the last rate stays); release = stop. The engine
// switches on the next grid point of the new division, crossfaded. Anything that could leave it hanging (editor closed, focus lost,
// hidden) stops it.
struct StutterStrip : juce::Component {
    std::function<void(int)> onRate;   // 0 = off, 1..4 = 1/4, 1/8, 1/16, 1/32
    int held = 0; bool down = false;
    StutterStrip() { setRepaintsOnMouseActivity(true); }
    ~StutterStrip() override { stop(); }
    static const char* label(int i) { static const char* n[] { "1/4", "1/8", "1/16", "1/32" }; return n[juce::jlimit(0, 3, i)]; }
    juce::Rectangle<float> seg(int i) const { const float w = getWidth() / 4.0f; return { i * w, 0.0f, w, (float) getHeight() }; }
    int segAt(juce::Point<float> p) const { if (!getLocalBounds().toFloat().contains(p)) return -1; return juce::jlimit(0, 3, (int) (p.x / (getWidth() / 4.0f))); }
    void set(int r) { if (r == held) return; held = r; if (onRate) onRate(r); repaint(); }
    void stop() { down = false; if (held) { held = 0; if (onRate) onRate(0); } }
    void paint(juce::Graphics& g) override {
        auto R = getLocalBounds().toFloat().reduced(1);
        if (GLASS) ghosty::glass::recessed(g, R, 9.0f); else { g.setColour(CARD2); g.fillRoundedRectangle(R, 6); }
        const int over = isMouseOver() && !down ? segAt(getMouseXYRelative().toFloat()) : -1;
        for (int i = 0; i < 4; ++i) {
            auto r = seg(i).reduced(i == 0 ? 1.0f : 0.5f, 1.0f); const bool on = held == i + 1;
            if (on && GLASS) ghosty::glass::body(g, r.reduced(1.0f), 7.0f, PINK, 0.95f, 0.3f);
            else if (on) { g.setColour(PINK.withAlpha(0.85f)); g.fillRoundedRectangle(r.reduced(1.0f), 5); }
            else if (i == over) { g.setColour(PINK.withAlpha(0.18f)); g.fillRoundedRectangle(r.reduced(1.0f), 5); }
            if (i > 0) { g.setColour(PINK.withAlpha(0.35f)); g.fillRect(seg(i).getX() - 0.5f, R.getY() + 5, 1.0f, R.getHeight() - 10); }
            g.setColour(on ? ON : INK); g.setFont(font(12.0f)); g.drawText(label(i), seg(i), juce::Justification::centred);
        }
        g.setColour(PINK.withAlpha(0.6f)); g.drawRoundedRectangle(R, 6, 1.1f);
    }
    void mouseDown(const juce::MouseEvent& e) override { down = true; const int i = segAt(e.position); if (i >= 0) set(i + 1); }
    void mouseDrag(const juce::MouseEvent& e) override { if (!down) return; const int i = segAt(e.position); if (i >= 0) set(i + 1); }   // (outside: the last rate stays)
    void mouseUp(const juce::MouseEvent&) override { stop(); repaint(); }
    void focusLost(FocusChangeType) override { if (!isMouseButtonDown()) stop(); }
    void visibilityChanged() override { if (!isVisible()) stop(); }
    void parentHierarchyChanged() override { if (!isShowing()) stop(); }
};

struct HoldButton : juce::TextButton {
    std::function<void(bool)> onHold; using juce::TextButton::TextButton;
    void mouseDown(const juce::MouseEvent& e) override { juce::TextButton::mouseDown(e); setToggleState(true, juce::dontSendNotification); if (onHold) onHold(true); }
    void mouseUp(const juce::MouseEvent& e) override { juce::TextButton::mouseUp(e); setToggleState(false, juce::dontSendNotification); if (onHold) onHold(false); }
};

} // namespace bx
