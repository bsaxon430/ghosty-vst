#pragma once
// Ghosty Spectrum's colours: ONE palette in one of the three house looks (BLACK / PURPLE / LIGHT, Shared/GhostThemes.h), read at paint time by the editor, the tab bar, the look-and-feel
// and the SPECTRAL LAB. Every surface colour is OPAQUE (nothing translucent is ever layered over a background, so no
// renderer / host can show a different colour through it). Accents are tuned per theme for contrast.
#include <juce_gui_basics/juce_gui_basics.h>
#include "GhostThemes.h"
#include <vector>

namespace sp {
using Look = ghosty::themes::Theme;   // the house looks: BLACK (Dark), PURPLE, LIGHT (Shared/GhostThemes.h)
struct Palette {
    bool dark = true;                 // true for BLACK and PURPLE (the dark-surface looks), false for LIGHT
    Look look = Look::Dark;
    juce::Colour bg, panel, panel2, field, line, ink, grey, dim, knobFace, track, canvasEdge;
    juce::Colour purple, violet, green, pink, amber, cyan, red, blue;
    std::vector<juce::Colour> accents() const { return { purple, violet, green, pink, amber, cyan, red, blue }; }
    // every value comes from the shared tokens; accents are made readable on the look's panels (>= 3:1, bold labels)
    static Palette make(Look t) {
        const auto& T = ghosty::themes::get(t); Palette p; p.look = t; p.dark = !T.isLight;
        p.bg = T.window; p.panel = T.panel; p.panel2 = T.card; p.field = T.cell; p.line = T.line; p.ink = T.ink; p.grey = T.dim; p.dim = T.dim;
        p.knobFace = T.cell; p.track = T.line; p.canvasEdge = T.violet;
        auto rd = [&](juce::Colour c) { return ghosty::themes::readableOn(ghosty::themes::readableOn(c, T.panel, 3.0), T.card, 3.0); };
        p.purple = rd(T.violet); p.violet = rd(T.violet.interpolatedWith(T.accent2, 0.25f)); p.green = rd(T.accent3); p.pink = rd(T.accent1);
        p.amber = rd(T.warn); p.cyan = rd(T.accent2); p.red = rd(T.bad); p.blue = rd(T.isLight ? juce::Colour(0xff2456c8) : juce::Colour(0xff6aa8ff));
        return p;
    }
    static Palette make(bool dark) { return make(dark ? Look::Dark : Look::Light); }   // the old two-state call
};
// the label colour for text on a filled accent (a gradient a -> b): the candidate that reads best on both ends (>= 4.5:1 where any can)
inline juce::Colour textOn(juce::Colour a, juce::Colour b, const ghosty::themes::Palette& T) {
    const juce::Colour cand[] = { T.ink, T.onAccent, juce::Colour(0xff0b0814), juce::Colour(0xfff4eefc) };
    juce::Colour best = cand[0]; double bk = -1;
    for (auto c : cand) { const double k = juce::jmin(ghosty::themes::contrast(c, a), ghosty::themes::contrast(c, b)); if (k > bk) { bk = k; best = c; } }
    return best;
}
inline Palette& pal() { static Palette p = Palette::make(Look::Dark); return p; }
inline const ghosty::themes::Palette& tokens() { return ghosty::themes::get(pal().look); }
// the accent that a button / knob was tinted with, in the other theme (tints are stored as ARGB in the component properties)
inline juce::Colour remapAccent(juce::Colour c, const Palette& from, const Palette& to) {
    const auto a = from.accents(), b = to.accents();
    for (size_t i = 0; i < a.size(); ++i) if (a[i].getARGB() == c.getARGB()) return b[i];
    return c;
}
// only Spectrum's own tints are touched: a value is changed only when it IS one of the old palette's 32-bit accents, and pages
// made by other parts (VOICE, ARRANGE, ARCS, PROCESS LAB: property "foreignTheme") are skipped - they may keep anything in "tint"
inline void remapTints(juce::Component& root, const Palette& from, const Palette& to) {
    if (root.getProperties().contains("foreignTheme")) return;
    if (root.getProperties().contains("tint")) {
        const juce::var v = root.getProperties()["tint"];
        if (v.isInt() || v.isInt64()) { const auto raw = (juce::int64) v;
            if (raw >= 0 && raw <= (juce::int64) 0xffffffffLL) { const auto c = juce::Colour((juce::uint32) raw), m = remapAccent(c, from, to); if (m != c) root.getProperties().set("tint", (juce::int64) m.getARGB()); } }
    }
    for (auto* ch : root.getChildren()) remapTints(*ch, from, to);
}
// switch the whole palette (message thread); the caller repaints / re-applies look-and-feel colours
inline void setTheme(Look t, juce::Component* rootToRetint = nullptr) {
    if (pal().look == t) return;
    const auto from = pal(); pal() = Palette::make(t);
    if (rootToRetint) remapTints(*rootToRetint, from, pal());
}
inline void setTheme(bool dark, juce::Component* rootToRetint = nullptr) { setTheme(dark ? Look::Dark : Look::Light, rootToRetint); }
} // namespace sp
