#pragma once
// GhostBlock house themes, shared by every plugin: the three looks of Mr. Mickens.
//
//   Theme::Dark    the pitch-black look (Mickens "BLACK")
//   Theme::Purple  the deep purple look (Mickens "PURPLE", the owner's favourite; Mickens' default)
//   Theme::Light   the soft lilac off-white look (Mickens "LIGHT", toned down: no pure white, no surface brighter than 93 %)
//
// Header-only, no allocation after first use, message thread only. Every SURFACE token is opaque (alpha 255): paint panels with them
// directly, never layer translucent full-window glows (that caused a repaint glitch on macOS). The neon accents are tuned per theme.
//
// API
//   const Palette& get(Theme)                 the token set for a theme
//   Theme next(Theme)                         the theme button: DARK -> PURPLE -> LIGHT -> DARK
//   juce::String toString(Theme)              "dark" | "purple" | "light"     (save this)
//   Theme fromString(const juce::String&, Theme fallback = Theme::Dark)
//   int toInt(Theme) / Theme fromInt(int)     0 dark, 1 purple, 2 light (if a plugin stores an int)
//   Theme fromLegacyBool(bool isLight)        old dark/light switches: false -> Dark, true -> Light
//   Theme fromMickensIndex(int) / int toMickensIndex(Theme)   Mr. Mickens' own stored index: 0 PURPLE, 1 BLACK, 2 LIGHT
//   juce::String displayName(Theme)           "BLACK" | "PURPLE" | "LIGHT" (button text; show the NEXT theme's name on the cycle button)
//   double contrast(juce::Colour, juce::Colour)   WCAG contrast ratio (1..21)
//   juce::Colour readableOn(juce::Colour c, juce::Colour bg, double min = 3.0)   darkens (light themes) / brightens (dark themes) c until it reads on bg
//
// TOKENS (Palette): text tokens = ink, dim; background tokens = window, windowTop, windowBottom, panel, card, cell. Every text/background pair
// is >= 4.5:1 in all three themes (Shared/tests/ThemeTests.cpp). onAccent is the label colour on a filled accent button (>= 3:1, bold text).
#include <juce_graphics/juce_graphics.h>
#include <atomic>
#include <cmath>

namespace ghosty::themes {

enum class Theme { Dark = 0, Purple = 1, Light = 2 };

struct Palette {
    Theme theme = Theme::Dark;
    bool isLight = false;
    // surfaces (all opaque)
    juce::Colour window;          // the editor background (flat fill)
    juce::Colour windowTop, windowBottom;   // optional vertical gradient ends of the window (Light: both = window)
    juce::Colour panel;           // section / panel fill
    juce::Colour card;            // raised surfaces: popup menus, tooltips, text-entry boxes, overlays
    juce::Colour cell;            // cells, rows, button off-fill, combo boxes, knob faces, step cells
    juce::Colour line;            // borders, rules, knob tracks
    // text
    juce::Colour ink;             // body text
    juce::Colour dim;             // secondary text, captions
    juce::Colour onAccent;        // text on a filled accent button / chip
    juce::Colour overlay;         // playheads, grid lines, lock outlines: draw with alpha 0.1 .. 0.6 on cells
    // accents (neon on the dark looks, deepened on Light)
    juce::Colour accent1;         // pink
    juce::Colour accent2;         // cyan
    juce::Colour accent3;         // lime / green
    juce::Colour violet;          // purple accent (sliders, selection)
    juce::Colour good, warn, bad; // = green / gold / red
    // instruments
    juce::Colour keyWhite, keyBlack;      // piano-roll / keyboard keys
    juce::Colour waveBg;                  // waveform / lane background (opaque)
    juce::Colour waveLo, waveHi;          // waveform gradient ends (or use a rainbow made readableOn(waveBg))
    juce::Colour shadow;                  // drop shadows only (the one translucent token)
};

inline double relLum(juce::Colour c) {
    auto ch = [](float v) { return v <= 0.03928f ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * ch(c.getFloatRed()) + 0.7152 * ch(c.getFloatGreen()) + 0.0722 * ch(c.getFloatBlue());
}
inline double contrast(juce::Colour a, juce::Colour b) {
    const double x = relLum(a), y = relLum(b);
    return (juce::jmax(x, y) + 0.05) / (juce::jmin(x, y) + 0.05);
}
inline juce::Colour readableOn(juce::Colour c, juce::Colour bg, double minRatio = 3.0) {
    const bool darken = relLum(bg) > 0.18; const float a = c.getFloatAlpha(); auto o = c.withAlpha(1.0f);
    for (int k = 0; k < 32 && contrast(o, bg) < minRatio; ++k)
        o = darken ? juce::Colour::fromHSV(o.getHue(), juce::jmin(1.0f, o.getSaturation() * 1.03f + 0.02f), o.getBrightness() * 0.93f, 1.0f)
                   : juce::Colour::fromHSV(o.getHue(), o.getSaturation() * 0.95f, juce::jmin(1.0f, o.getBrightness() * 1.07f + 0.02f), 1.0f);
    return o.withAlpha(a);
}

inline Palette makePalette(Theme t) {
    using C = juce::Colour; Palette p; p.theme = t; p.isLight = t == Theme::Light;
    switch (t) {
        case Theme::Dark:     // Mr. Mickens BLACK
            p.window = C(0xff07040d); p.windowTop = C(0xff0d0717); p.windowBottom = C(0xff050309);
            p.panel = C(0xff110a1b); p.card = C(0xff1b1129); p.cell = C(0xff1b1129); p.line = C(0xff3a2a55);
            p.ink = C(0xfff4eefc); p.dim = C(0xff9a8bb0); p.onAccent = C(0xff07040d); p.overlay = C(0xffffffff);
            p.accent1 = C(0xffff2bd6); p.accent2 = C(0xff22e6ff); p.accent3 = C(0xffa6ff2b); p.violet = C(0xffa77bff);
            p.good = C(0xffa6ff2b); p.warn = C(0xffffd23f); p.bad = C(0xffff2650);
            p.keyWhite = C(0xffd9d2e6); p.keyBlack = C(0xff110a1b);
            p.waveBg = C(0xff030205);   // Mickens' 80 % black lane over its panel, flattened
            p.waveLo = C(0xff22e6ff); p.waveHi = C(0xffff2bd6); p.shadow = C(0x99000000);
            break;
        case Theme::Purple:   // Mr. Mickens PURPLE
            p.window = C(0xff3a2a5a); p.windowTop = C(0xff4b3775); p.windowBottom = C(0xff2e2148);
            p.panel = C(0xff4a3870); p.card = C(0xff5b4783); p.cell = C(0xff5b4783); p.line = C(0xff7a65a3);
            p.ink = C(0xfffaf6ff); p.dim = C(0xffd2c4ea); p.onAccent = C(0xff3a2a5a); p.overlay = C(0xffffffff);
            p.accent1 = C(0xffff2bd6); p.accent2 = C(0xff22e6ff); p.accent3 = C(0xffa6ff2b); p.violet = C(0xffc4a6ff);
            p.good = C(0xffa6ff2b); p.warn = C(0xffffd23f); p.bad = C(0xffff2650);
            p.keyWhite = C(0xffefe8fb); p.keyBlack = C(0xff2e2148);
            p.waveBg = C(0xff2e2046);   // Mickens' 75 % #241838 lane over its panel, flattened
            p.waveLo = C(0xff22e6ff); p.waveHi = C(0xffff2bd6); p.shadow = C(0x80140c24);
            break;
        case Theme::Light:    // Mr. Mickens LIGHT, toned down (lilac off-white; no channel above 0xED = 93 %)
            p.window = C(0xffd3cddd); p.windowTop = p.window; p.windowBottom = p.window;
            p.panel = C(0xffdcd7e5); p.card = C(0xffe4dfeb); p.cell = C(0xffcbc5d7); p.line = C(0xffaca5bc);
            p.ink = C(0xff1b1830); p.dim = C(0xff4f4a62); p.onAccent = C(0xffe8e4ed); p.overlay = C(0xff1b1830);
            p.accent1 = C(0xffad2d7b); p.accent2 = C(0xff096678); p.accent3 = C(0xff0a7440); p.violet = C(0xff6234e0);
            p.good = C(0xff0a7440); p.warn = C(0xff8c5e00); p.bad = C(0xffbb1f1f);
            p.keyWhite = C(0xffe6e1ec); p.keyBlack = C(0xff2a2440);
            p.waveBg = p.cell; p.waveLo = C(0xff096678); p.waveHi = C(0xffad2d7b); p.shadow = C(0x40231c38);
            break;
    }
    return p;
}

// GLASS (round 3): Ghosty Pro's opt-in "liquid glass" LIGHT. OFF by default: every other plugin (Ghost Box, Spectrum, Poltergeist, Beats,
// Mickens, ArpEZator) never sets it, so their LIGHT is the toned-down lilac above, unchanged. Ghosty Pro sets it once (ProTheme.h) before it
// paints; get(Theme::Light) then returns the glass palette in that binary only. BLACK and PURPLE are the same either way.
//   warm off-white / silver window (#ECEEF2, gradient #F2F3F6 -> #E4E7EC), panels lighter than the window, dark-slate ink,
//   the GhostBlock green / pink / cyan deepened for contrast; waveBg = the frosted display screen (slightly darker inset).
inline std::atomic<bool>& glassFlag() { static std::atomic<bool> f { false }; return f; }
inline void optInGlassLight(bool on = true) { glassFlag().store(on, std::memory_order_relaxed); }
inline bool glassLight() { return glassFlag().load(std::memory_order_relaxed); }
inline Palette makeGlassLightPalette() {
    using C = juce::Colour; Palette p = makePalette(Theme::Light);
    p.window = C(0xffeceef2); p.windowTop = C(0xfff2f3f6); p.windowBottom = C(0xffe4e7ec);
    p.panel = C(0xfff3f4f7); p.card = C(0xfff6f7fa); p.cell = C(0xffe2e5eb); p.line = C(0xffb4bac6);
    p.ink = C(0xff1c2230); p.dim = C(0xff485062); p.onAccent = C(0xfff6f7fa); p.overlay = C(0xff1c2230);
    p.accent1 = C(0xffa8206a); p.accent2 = C(0xff006278); p.accent3 = C(0xff08703f); p.violet = C(0xff5636cc);
    p.good = C(0xff08703f); p.warn = C(0xff835400); p.bad = C(0xffb01c28);
    p.keyWhite = C(0xfff4f5f8); p.keyBlack = C(0xff2a3040);
    p.waveBg = C(0xffd9dfe8); p.waveLo = p.accent2; p.waveHi = p.accent1; p.shadow = C(0x2e283046);
    return p;
}

inline const Palette& get(Theme t) {
    static const Palette d = makePalette(Theme::Dark), pu = makePalette(Theme::Purple), l = makePalette(Theme::Light);
    static const Palette lg = makeGlassLightPalette();     // GLASS (round 3)
    return t == Theme::Purple ? pu : t == Theme::Light ? (glassLight() ? lg : l) : d;
}
inline Theme next(Theme t) { return t == Theme::Dark ? Theme::Purple : t == Theme::Purple ? Theme::Light : Theme::Dark; }
inline int toInt(Theme t) { return (int) t; }
inline Theme fromInt(int i) { return i == 1 ? Theme::Purple : i == 2 ? Theme::Light : Theme::Dark; }
inline juce::String toString(Theme t) { return t == Theme::Purple ? "purple" : t == Theme::Light ? "light" : "dark"; }
inline Theme fromString(const juce::String& s, Theme fallback = Theme::Dark) {
    const auto x = s.trim().toLowerCase();
    if (x == "dark" || x == "black") return Theme::Dark;
    if (x == "purple") return Theme::Purple;
    if (x == "light" || x == "white") return Theme::Light;
    if (x.containsOnly("0123456789") && x.isNotEmpty()) return fromInt(x.getIntValue());
    return fallback;
}
inline Theme fromLegacyBool(bool isLight) { return isLight ? Theme::Light : Theme::Dark; }
// Mr. Mickens stores 0 PURPLE (its default), 1 BLACK, 2 LIGHT (and older sessions only "dark": false = PURPLE, true = BLACK)
inline Theme fromMickensIndex(int i) { return i == 1 ? Theme::Dark : i == 2 ? Theme::Light : Theme::Purple; }
inline int toMickensIndex(Theme t) { return t == Theme::Dark ? 1 : t == Theme::Light ? 2 : 0; }
inline juce::String displayName(Theme t) { return t == Theme::Purple ? "PURPLE" : t == Theme::Light ? "LIGHT" : "BLACK"; }

} // namespace ghosty::themes
