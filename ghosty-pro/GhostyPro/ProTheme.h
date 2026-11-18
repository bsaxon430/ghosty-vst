#pragma once
// Ghosty Pro's colours: the GhostBlock house themes (Shared/GhostThemes.h, the three looks of Mr. Mickens): BLACK, PURPLE, LIGHT.
// ONE palette at a time, read at paint time on the message thread; every surface colour is OPAQUE. The palette below is Ghosty Pro's
// view of the house tokens (bg = window, card = panel, card2 = cell, white = ink, slime / ecto / pink / ice / gold = the accents),
// with every accent made readable as TEXT on every surface of its theme (>= 4.5:1, GhostThemes' readableOn).
// Graph "screens" (wavetable view, scopes, envelope views, FLOW strip) stay dark in the LIGHT theme, like a display: their paint()
// opens a pt::ScopedDark (BLACK while LIGHT; in BLACK / PURPLE their own dark tokens are used).
// GLASS (round 3): Ghosty Pro's LIGHT is the "liquid glass" palette (opted in below); its screens are light frosted displays instead
// (ScopedDark switches to glassScreenPal(), the materials are GhostyPro/ProGlass.h). BLACK / PURPLE are unchanged.
#include "GhostArt.h"
#include "GhostThemes.h"
#include "ProGhostLook.h"   // GHOST LOOK: the 4th look (liquid glass on the LIGHT base; Ghosty Pro only)

namespace pt
{
using Theme = ghosty::themes::Theme;

struct Palette
{
    Theme theme = Theme::Dark;
    bool dark = true;                 // = not the LIGHT theme (BLACK and PURPLE are dark looks)
    // surfaces                 text                       accents (same role names as ghosty::col)
    juce::Colour bg, card, card2, deep, knobFace, btnTop, btnBot, track;
    juce::Colour white, dim, faint;
    juce::Colour slime, ecto, pink, ice, gold, red, blue, violet;
    juce::Colour onAccent;            // text on a filled accent (an ON button)

    // text at a reduced emphasis: dark looks = ink with alpha (still >= 4.5:1 for a >= 0.6 on their surfaces); LIGHT = an opaque ink / secondary
    juce::Colour txt (float a) const { return dark ? white.withAlpha (juce::jmax (a, 0.72f)) : (a >= 0.8f ? white : dim); }

    static Palette make (Theme t)
    {
        namespace th = ghosty::themes;
        th::optInGlassLight();       // GLASS (round 3): Ghosty Pro's LIGHT = the "liquid glass" palette (Shared/GhostThemes.h; Pro only)
        const auto& h = th::get (t);
        Palette p; p.theme = t; p.dark = ! h.isLight;
        p.bg = h.window; p.card = h.panel; p.card2 = h.cell; p.deep = h.isLight ? h.cell : h.waveBg;
        p.knobFace = h.isLight ? h.card : h.cell; p.btnTop = h.isLight ? h.card : h.cell.brighter (0.12f); p.btnBot = h.cell; p.track = h.line;
        p.white = h.ink; p.dim = h.dim; p.faint = h.line;
        // accents: readable as text on the darkest (LIGHT) / lightest (dark looks) of the surfaces they sit on
        auto rd = [&] (juce::Colour c)
        {
            for (auto s : { h.window, h.panel, h.cell, h.card }) c = th::readableOn (c, s, 4.5);
            return c;
        };
        p.slime = rd (h.accent3); p.ecto = rd (h.violet); p.pink = rd (h.accent1); p.ice = rd (h.accent2); p.gold = rd (h.warn);
        p.red = rd (h.bad); p.blue = rd (h.accent2); p.violet = rd (h.violet);
        p.onAccent = h.onAccent;
        return p;
    }
    std::vector<juce::Colour> all() const { return { bg, card, card2, deep, knobFace, btnTop, btnBot, track, white, dim, faint, slime, ecto, pink, ice, gold, red, blue, violet }; }
};

inline Palette& pal() { static Palette p = Palette::make (Theme::Dark); return p; }
inline const Palette& paletteFor (Theme t)
{
    static const Palette d = Palette::make (Theme::Dark), pu = Palette::make (Theme::Purple), l = Palette::make (Theme::Light);
    return t == Theme::Purple ? pu : t == Theme::Light ? l : d;
}
inline const Palette& darkPal() { return paletteFor (Theme::Dark); }
inline const Palette& ghostPal();
inline void setTheme (Theme t) { if (pal().theme != t || pghost::flag()) { pghost::flag() = false; pal() = paletteFor (t); } }   // (GHOST LOOK: leaves GHOST)
// GHOST LOOK: the look index 0 BLACK, 1 PURPLE, 2 LIGHT, 3 GHOST (= the LIGHT theme + the GHOST palette + pghost::on()); anything else = BLACK
inline int lookFromInt (int i) { return i >= 0 && i <= pghost::kLook ? i : 0; }
inline Theme themeOfLook (int look) { return look == pghost::kLook ? Theme::Light : ghosty::themes::fromInt (look); }
inline void setLook (int look)
{
    look = lookFromInt (look);
    if (look != pghost::kLook) { setTheme (ghosty::themes::fromInt (look)); return; }
    if (! pghost::flag()) { pghost::flag() = true; pal() = ghostPal(); }
}
inline int currentLook() { return pghost::flag() ? pghost::kLook : ghosty::themes::toInt (pal().theme); }

// a colour of one theme -> the same role in another (exact ARGB match; alpha kept); anything else is returned unchanged
inline juce::Colour remap (juce::Colour c, const Palette& from, const Palette& to)
{
    const auto a = from.all(), b = to.all();
    const auto opaque = c.withAlpha ((juce::uint8) 255);
    for (size_t i = 0; i < a.size(); ++i) if (a[i] == opaque) return b[i].withAlpha (c.getAlpha());
    return c;
}
// a colour stored at build time (a card accent, a knob tint): a BLACK-palette colour or a ghosty::col constant -> its role in the current theme
inline juce::Colour themed (juce::Colour c)
{
    const auto& p = pal();
    namespace gc = ghosty::col;
    const auto o = c.withAlpha ((juce::uint8) 255);
    const std::pair<juce::Colour, juce::Colour> legacy[] { { gc::bg, p.bg }, { gc::card, p.card }, { gc::card2, p.card2 }, { gc::white, p.white }, { gc::dim, p.dim },
                                                           { gc::slime, p.slime }, { gc::ecto, p.ecto }, { gc::pink, p.pink }, { gc::ice, p.ice }, { gc::gold, p.gold } };
    for (auto& [from, to] : legacy) if (o == from) return to.withAlpha (c.getAlpha());
    return p.theme == Theme::Dark ? c : remap (c, darkPal(), p);
}

// GLASS (round 3): the palette of a display "screen" in the glass LIGHT look: a light frosted screen (waveBg, a slightly darker inset than
// the panels) with ink / dim / every accent readable on it (>= 4.5:1 on the darkest stop of its gradient, glassScreenBottom()).
inline juce::Colour glassScreenTop()    { return juce::Colour (0xffe2e7ef); }
inline juce::Colour glassScreenBottom() { return juce::Colour (0xffd2d9e3); }
inline const Palette& glassScreenPal()
{
    static const Palette s = []
    {
        namespace th = ghosty::themes;
        auto p = Palette::make (Theme::Light);
        const auto scr = th::get (Theme::Light).waveBg;
        p.bg = p.card = p.card2 = p.deep = scr;
        auto rd = [&] (juce::Colour c) { for (auto b : { glassScreenTop(), glassScreenTop().overlaidWith (juce::Colour (0xff283046).withAlpha (0.07f)), scr, glassScreenBottom() }) c = th::readableOn (c, b, 4.5); return c; };
        p.white = rd (p.white); p.dim = rd (p.dim);
        for (auto* c : { &p.slime, &p.ecto, &p.pink, &p.ice, &p.gold, &p.red, &p.blue, &p.violet }) *c = rd (*c);
        return p;
    }();
    return s;
}

// GHOST LOOK: the GHOST palette (ink / secondary / accents readable as text on the frosted glass at its darkest over the aurora) and its
// screen palette (the glass display screens: pghost::screenTop() .. screenBottom())
inline const Palette& ghostPal()
{
    static const Palette g = []
    {
        namespace th = ghosty::themes;
        auto p = Palette::make (Theme::Light);
        using C = juce::Colour;
        p.bg = C (0xffebe6f8); p.card = C (0xfff0edf9); p.card2 = C (0xffe6e1f4); p.deep = C (0xffeeeaf9);
        p.knobFace = C (0xfff7f5fd); p.btnTop = C (0xfff7f5fd); p.btnBot = C (0xffe6e1f4); p.track = C (0xffc9c0e6);
        p.white = ghosty::glass::ink(); p.dim = ghosty::glass::ink2(); p.faint = C (0xffb8afd8);
        auto refs = pghost::glassRefs(); for (auto c : { p.bg, p.card, p.card2, p.deep }) refs.push_back (c);
        auto rd = [&] (juce::Colour c) { for (int k = 0; k < 3; ++k) for (auto b : refs) c = th::readableOn (c, b, 4.6); return c; };
        p.white = rd (p.white); p.dim = rd (p.dim);
        p.ecto = p.violet = rd (C (0xff5530c8));
        for (auto* c : { &p.slime, &p.pink, &p.ice, &p.gold, &p.red, &p.blue }) *c = rd (*c);
        p.onAccent = C (0xffffffff);
        return p;
    }();
    return g;
}
inline const Palette& ghostScreenPal()
{
    static const Palette s = []
    {
        namespace th = ghosty::themes;
        auto p = ghostPal();
        p.bg = p.card = p.card2 = p.deep = pghost::screenBottom();
        const juce::Colour refs[] { pghost::screenTop(), pghost::screenBottom(), pghost::screenTop().overlaidWith (ghosty::glass::shadowTint().withAlpha (0.13f)), pghost::screenBottom().darker (0.03f) };
        auto rd = [&] (juce::Colour c) { for (int k = 0; k < 3; ++k) for (auto b : refs) c = th::readableOn (c, b, 4.6); return c; };
        p.white = rd (p.white); p.dim = rd (p.dim);
        for (auto* c : { &p.slime, &p.ecto, &p.pink, &p.ice, &p.gold, &p.red, &p.blue, &p.violet }) *c = rd (*c);
        return p;
    }();
    return s;
}

// a dark "screen" inside the LIGHT window: the palette reads BLACK until the scope ends (message thread, paint only)
// GLASS (round 3): with the glass LIGHT on, a screen reads the light frosted glassScreenPal() instead of BLACK
// GHOST LOOK: in GHOST a screen reads ghostScreenPal() and the scope returns to the GHOST palette
struct ScopedDark
{
    explicit ScopedDark (bool active = true) : was (pal().theme), on (active && was == Theme::Light), ghost (pghost::flag())
    { if (on) pal() = ghost ? ghostScreenPal() : ghosty::themes::glassLight() ? glassScreenPal() : darkPal(); }
    ~ScopedDark() { if (on) pal() = ghost ? ghostPal() : paletteFor (was); }
    ScopedDark (const ScopedDark&) = delete; ScopedDark& operator= (const ScopedDark&) = delete;
    const Theme was; const bool on, ghost;
};
} // namespace pt
