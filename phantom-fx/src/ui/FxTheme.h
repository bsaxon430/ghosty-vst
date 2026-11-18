#pragma once
// BLACK / PURPLE / LIGHT for the shared FX rack UI (Ghost Box and Ghosty Pro's FX page): the GhostBlock house themes of
// Shared/GhostThemes.h (ghosty::themes). ONE switch, read at paint time.
//
//   fxtheme::set (Theme)     the editor calls it whenever its theme changes AND at the top of its paint() (several plugin windows can be
//   fxtheme::set (bool)      open with different themes: the switch is per paint pass), then Panel::themeChanged() after a change.
//                            The bool overload is the old DARK / LIGHT switch (false = Dark, true = Light).
//   fxtheme::T (c)           every colour the fx UI paints goes through it. The rack is written in the original Ghost Box dark colours
//                            ("originals"); T maps them onto the current theme's Palette tokens: the ghost palette to the tokens (bg ->
//                            window, card -> panel, white -> ink, slime / ecto / pink / ice / gold -> accent3 / violet / accent1 / accent2 /
//                            warn), dark surfaces to the theme's opaque surfaces, white / grey text to ink / dim, any other accent kept
//                            but made readable on the panels (readableOn: brightened on the dark looks, deepened on LIGHT). Mapped
//                            colours are remembered both ways, so a colour stored at build time in one theme maps correctly in another.
//   gpfx::ui::col::x         the palette as themed colours (convert to juce::Colour when used); .brighter() = "more prominent" (darker
//                            on LIGHT).
// Message thread only (painting); never used on the audio thread.
#include <juce_gui_basics/juce_gui_basics.h>
#include "GhostArt.h"
#include "GhostThemes.h"
#include <atomic>
#include <unordered_map>
#include <vector>

namespace fxtheme {

using Theme = ghosty::themes::Theme;
// a greyed (disabled) control: drawn at this opacity, still >= 3:1 on its background in every look (ProTests audit), visibly dimmer than enabled
inline constexpr float kDisabledAlpha = 0.62f;
inline std::atomic<int>& themeFlag() { static std::atomic<int> f { 0 }; return f; }
// GHOST (Ghost Box only, GPFX_BOX): the 4th look = liquid glass on the LIGHT base (Shared/GhostGlass.h). The switch holds 3 ("look" 3):
// theme() then reads LIGHT (every light() branch applies) and ghost() is true; the colours map through ghostPalette() in their own slot.
// Ghosty Pro (GPFX_BOX unset) never stores 3 and ghost() is constant false there: its looks are unchanged. The shared Theme enum is untouched.
inline constexpr int kLookGhost = 3;
// PAINTED looks (Phantom FX 0.1.9, PaintedLook.h): look numbers from kFirstPainted up are image looks, one per asset folder
// (DAWN, CEL, ...). Each brings its own flat palette for the code-drawn editors that sit on its slabs; the registry fills this list.
inline constexpr int kFirstPainted = 4, kMaxLooks = 16;
inline std::vector<ghosty::themes::Palette>& paintedPalettes() { static std::vector<ghosty::themes::Palette> v; return v; }
inline int paintedCount() noexcept { return (int) paintedPalettes().size(); }
inline bool painted() noexcept { const int f = themeFlag().load (std::memory_order_relaxed); return f >= kFirstPainted && f - kFirstPainted < paintedCount(); }
// THE DRAWN LOOKS ARE NO LONGER OFFERED (Brandon, 2026-11-10): Phantom FX shows its painted looks only (DAWN, CEL, NEON). BLACK,
// PURPLE, LIGHT and GHOST (looks 0..3) keep all their code, but nothing in the window, in a saved preference or in a project can
// show them: the look button cycles from firstLook() up. They come back only in a build configured with -DPHANTOM_OLD_LOOKS=ON
// (hidden, off), or as the last resort when no painted look could be read. The checks unlock them to measure against BLACK's layout.
#ifndef PHANTOM_OLD_LOOKS
 #define PHANTOM_OLD_LOOKS 0
#endif
inline std::atomic<bool>& oldLooksUnlocked() noexcept { static std::atomic<bool> u { PHANTOM_OLD_LOOKS != 0 }; return u; }
inline bool oldLooksOffered() noexcept { return oldLooksUnlocked().load (std::memory_order_relaxed) || paintedCount() == 0; }
inline int firstLook() noexcept { return oldLooksOffered() ? 0 : kFirstPainted; }
inline Theme theme() noexcept
{
    const int f = themeFlag().load (std::memory_order_relaxed);
    if (f >= kFirstPainted) return f - kFirstPainted < paintedCount() && ! paintedPalettes()[(size_t) (f - kFirstPainted)].isLight ? Theme::Dark : Theme::Light;
    return f == kLookGhost ? Theme::Light : ghosty::themes::fromInt (f);
}
inline bool ghost() noexcept
{
#if GPFX_BOX
    return themeFlag().load (std::memory_order_relaxed) == kLookGhost;
#else
    return false;
#endif
}
inline bool light() noexcept { return theme() == Theme::Light; }
inline void set (Theme t) noexcept { themeFlag().store (ghosty::themes::toInt (t), std::memory_order_relaxed); }
inline void set (bool isLight) noexcept { set (ghosty::themes::fromLegacyBool (isLight)); }   // the old DARK / LIGHT switch
// the raw look (0 BLACK, 1 PURPLE, 2 LIGHT, 3 GHOST in Ghost Box): a window / panel stores it and sets it back at the top of its paint pass
inline int look() noexcept { return themeFlag().load (std::memory_order_relaxed); }
inline void setLook (int l) noexcept
{
#if GPFX_BOX
    themeFlag().store (l == kLookGhost || (l >= kFirstPainted && l - kFirstPainted < paintedCount()) ? l : ghosty::themes::toInt (ghosty::themes::fromInt (l)), std::memory_order_relaxed);
#else
    set (ghosty::themes::fromInt (l));
#endif
}
// GHOST: the flat tokens = the frosted glass as it reads over the aurora (text maths + every flat fill); deep indigo ink, deepened accents
inline ghosty::themes::Palette makeGhostPalette()
{
    using C = juce::Colour; auto p = ghosty::themes::makePalette (Theme::Light);
    p.window = C (0xffebe6f8); p.windowTop = C (0xfff1edfb); p.windowBottom = C (0xffe6f2f0);
    p.panel = C (0xfff0edf9); p.card = C (0xfff7f5fd); p.cell = C (0xffe6e1f4); p.line = C (0xffb8afd8);
    p.ink = C (0xff2a2250); p.dim = C (0xff463e6c); p.onAccent = C (0xffffffff); p.overlay = C (0xff2a2250);
    p.accent1 = C (0xffb01f7c); p.accent2 = C (0xff046a78); p.accent3 = C (0xff0a7247); p.violet = C (0xff5530c8);
    p.good = C (0xff0a7247); p.warn = C (0xff7d5000); p.bad = C (0xffb81c30);
    p.keyWhite = C (0xfff6f4fc); p.keyBlack = C (0xff2a2250);
    p.waveBg = C (0xffe9e5f5); p.waveLo = p.accent2; p.waveHi = C (0xff5530c8); p.shadow = C (0x2e3a2e6b);
    return p;
}
inline const ghosty::themes::Palette& ghostPalette() { static const auto p = makeGhostPalette(); return p; }
inline const ghosty::themes::Palette& pal() { return painted() ? paintedPalettes()[(size_t) (look() - kFirstPainted)] : ghost() ? ghostPalette() : ghosty::themes::get (theme()); }

// WCAG relative luminance / contrast
inline double luminance (juce::Colour c) { return ghosty::themes::relLum (c); }
inline double contrast (juce::Colour a, juce::Colour b) { return ghosty::themes::contrast (a, b); }

struct Maps { std::unordered_map<juce::uint32, juce::uint32> fwd, rev; };   // per theme: RGB (alpha stripped) original -> themed, themed -> original
#if GPFX_BOX
inline constexpr int kSlots = kMaxLooks;   // BLACK, PURPLE, LIGHT, GHOST, then the painted looks
#else
inline constexpr int kSlots = 3;
#endif
inline Maps& mapsSlot (int s) { static Maps m[kSlots]; return m[juce::jlimit (0, kSlots - 1, s)]; }
inline Maps& maps (Theme t) { return mapsSlot (ghosty::themes::toInt (t)); }
inline const ghosty::themes::Palette& palOfSlot (int s) { return s >= kFirstPainted && s - kFirstPainted < paintedCount() ? paintedPalettes()[(size_t) (s - kFirstPainted)] : s == kLookGhost ? ghostPalette() : ghosty::themes::get (ghosty::themes::fromInt (s)); }

inline juce::Colour mapOpaqueP (juce::Colour c, const ghosty::themes::Palette& p);
inline juce::Colour mapOpaque (juce::Colour c, Theme t) { return mapOpaqueP (c, ghosty::themes::get (t)); }   // c with alpha 255
inline juce::Colour mapOpaqueP (juce::Colour c, const ghosty::themes::Palette& p)
{
    const double lt = &p == &ghostPalette() ? 5.4 : 4.5;   // the accents' contrast on LIGHT (GHOST: >= 4.5:1 also on the darkest glass over the aurora)
    // 1) the ghost palette (Shared/GhostArt.h) and the fixed tones the rack uses most
    const std::pair<juce::uint32, juce::Colour> table[] {
        { 0xff0B0C10, p.window }, { 0xff141621, p.panel }, { 0xff1b1e2a, p.cell }, { 0xffF2F4F5, p.ink }, { 0xff7f8496, p.dim },
        { 0xffFFFFFF, p.ink },    { 0xff000000, juce::Colour (0xff000000) },
        { 0xff3CFF8F, ghosty::themes::readableOn (p.accent3, p.cell, p.isLight ? lt : 3.0) }, { 0xffA06BFF, ghosty::themes::readableOn (p.violet, p.cell, p.isLight ? lt : 3.0) },
        { 0xffFF4FD8, ghosty::themes::readableOn (p.accent1, p.cell, p.isLight ? lt : 3.0) }, { 0xff45E6FF, ghosty::themes::readableOn (p.accent2, p.cell, p.isLight ? lt : 3.0) },
        { 0xffFFC35A, ghosty::themes::readableOn (p.warn, p.cell, p.isLight ? lt : 3.0) },
    };
    for (auto& [d, l] : table) if (c.getARGB() == d) return l;
    const float h = c.getHue(), s = c.getSaturation(), v = c.getBrightness();
    if (v < 0.25f)
    {   // a dark surface -> the theme's opaque surfaces: the darkest originals -> window, lighter ones towards cell / line
        const float k = juce::jlimit (0.0f, 1.0f, v / 0.25f);
        auto base = k < 0.5f ? p.window.interpolatedWith (p.panel, k * 2.0f) : p.panel.interpolatedWith (p.isLight ? p.cell.darker (0.06f) : p.line, (k - 0.5f) * 2.0f);
        return s > 0.35f ? base.interpolatedWith (juce::Colour::fromHSV (h, p.isLight ? 0.35f : 0.6f, p.isLight ? 0.85f : base.getBrightness() + 0.05f, 1.0f), 0.18f) : base;
    }
    if (s < 0.28f) return v > 0.72f ? p.ink : p.dim;   // greys: text
    // an accent: same hue; kept on the dark looks (neon) unless it would not read, deepened on LIGHT; >= 3:1 (4.5:1 on LIGHT) on the cells
    juce::Colour a = p.isLight ? juce::Colour::fromHSV (h, juce::jmax (s, 0.55f), juce::jmin (v, 0.80f), 1.0f) : c;
    return ghosty::themes::readableOn (a, p.cell, p.isLight ? lt : 3.0);
}

inline juce::Colour mapToSlot (juce::Colour c, int slot)
{
    auto& m = mapsSlot (slot);
    const juce::uint32 rgb = c.getARGB() | 0xff000000u;
    if (m.rev.count (rgb)) return c;   // already a colour of this theme (stored at build time in this theme)
    juce::uint32 orig = rgb;
    for (int o = 0; o < kSlots; ++o)   // a colour stored while another theme was on: back to its original first
        if (o != slot) { auto& mo = mapsSlot (o); if (auto it = mo.rev.find (rgb); it != mo.rev.end()) { orig = it->second; break; } }
    juce::uint32 out;
    if (auto it = m.fwd.find (orig); it != m.fwd.end()) out = it->second;
    else
    {
        out = mapOpaqueP (juce::Colour (orig), palOfSlot (slot)).getARGB() | 0xff000000u;
        m.fwd[orig] = out;
        if (! m.fwd.count (out)) m.rev[out] = orig;   // (a themed value that is also an original keeps its forward meaning)
    }
    float a = c.getFloatAlpha();
    if (a >= 0.9f && juce::Colour (orig).getBrightness() < 0.25f) a = 1.0f;   // surfaces are opaque (no see-through cards)
    return juce::Colour (out).withAlpha (a);
}
inline juce::Colour mapTo (juce::Colour c, Theme t) { return mapToSlot (c, ghosty::themes::toInt (t)); }
inline juce::Colour map (juce::Colour c) { return mapTo (c, Theme::Light); }   // (old name: the LIGHT mapping)
// back to the original Ghost Box dark colour (for colours stored while another theme was on); unchanged when unknown
inline juce::Colour unmap (juce::Colour c)
{
    const juce::uint32 rgb = c.getARGB() | 0xff000000u;
    for (int o = 0; o < kSlots; ++o) { auto& m = mapsSlot (o); if (auto it = m.rev.find (rgb); it != m.rev.end()) return juce::Colour (it->second).withAlpha (c.getFloatAlpha()); }
    return c;
}
inline juce::Colour T (juce::Colour c) { return painted() ? mapToSlot (c, look()) : ghost() ? mapToSlot (c, kLookGhost) : mapTo (c, theme()); }

// themed colour: holds the DARK value (a plain ARGB number, so the palette below is constant-initialised: no static-init-order
// dependence on other headers' juce::Colour objects, which clang / Apple builds initialised AFTER this palette = all zeros), converts at use
struct C
{
    juce::uint32 argb = 0;
    juce::Colour dark() const { return juce::Colour (argb); }
    operator juce::Colour() const { return T (dark()); }
    juce::Colour get() const { return T (dark()); }
    juce::Colour withAlpha (float a) const { return get().withAlpha (a); }
    juce::Colour withMultipliedAlpha (float a) const { return get().withMultipliedAlpha (a); }
    juce::Colour brighter (float k = 0.4f) const { return light() ? get().darker (k) : get().brighter (k); }   // "more prominent"
    juce::Colour darker (float k = 0.4f) const { return get().darker (k); }
    juce::Colour interpolatedWith (juce::Colour o, float t) const { return get().interpolatedWith (o, t); }
    juce::Colour withBrightness (float b) const { return get().withBrightness (b); }
    juce::Colour contrasting (float k = 1.0f) const { return get().contrasting (k); }
    juce::uint32 getARGB() const { return get().getARGB(); }
    float getFloatAlpha() const { return get().getFloatAlpha(); }
};

// a colour stored in a component at build time (an accent member): converts through T at use, so it follows theme flips
struct Held
{
    juce::Colour v;
    Held() = default;
    Held (juce::Colour c) : v (c) {}
    Held (const C& c) : v (c.dark()) {}
    operator juce::Colour() const { return T (v); }
    juce::Colour get() const { return T (v); }
    juce::Colour withAlpha (float a) const { return get().withAlpha (a); }
    juce::Colour withMultipliedAlpha (float a) const { return get().withMultipliedAlpha (a); }
    juce::Colour brighter (float k = 0.4f) const { return light() ? get().darker (k) : unmapIfNeeded().brighter (k); }
    juce::Colour darker (float k = 0.4f) const { return get().darker (k); }
    juce::Colour interpolatedWith (juce::Colour o, float t) const { return get().interpolatedWith (o, t); }
private:
    juce::Colour unmapIfNeeded() const { return T (v); }
};

// the ghost palette, themed: DARK = exactly Shared/GhostArt.h, LIGHT = the GhostBlock light palette
namespace col {
    // the same values as Shared/GhostArt.h ghosty::col (test [17] checks they match)
    inline constexpr C bg { 0xff0B0C10 }, card { 0xff141621 }, card2 { 0xff1b1e2a }, white { 0xffF2F4F5 }, slime { 0xff3CFF8F },
                       ecto { 0xffA06BFF }, pink { 0xffFF4FD8 }, ice { 0xff45E6FF }, gold { 0xffFFC35A }, dim { 0xff7f8496 };
}

// re-apply every colour stored on a component tree (setColour values) for the current theme; call after set()
inline void retint (juce::Component& c)
{
    auto& props = c.getProperties();
    for (int i = 0; i < props.size(); ++i)
    {
        const auto name = props.getName (i).toString();
        if (! name.startsWith ("jcclr_")) continue;
        const auto v = juce::Colour ((juce::uint32) (juce::int64) props.getValueAt (i));
        const auto t = T (v);
        if (t != v) c.setColour (name.substring (6).getHexValue32(), t);
    }
    if (auto* te = dynamic_cast<juce::TextEditor*> (&c)) te->applyColourToAllText (te->findColour (juce::TextEditor::textColourId), true);   // text keeps its colour per character
    for (auto* k : c.getChildren()) retint (*k);
}

} // namespace fxtheme
