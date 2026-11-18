// Shared/GhostThemes.h: every text / background pair readable in all three themes, opaque surfaces, the toned-down LIGHT,
// the theme cycle, save / load strings and the migrations from older state.
#include "GhostThemes.h"
#include <iostream>
using namespace ghosty::themes;
static int passes = 0, fails = 0;
static void check(bool ok, const juce::String& what) { (ok ? passes : fails)++; std::cout << (ok ? "PASS  " : "FAIL  ") << what << "\n"; }
static juce::String hex(juce::Colour c) { return "#" + c.toDisplayString(false); }

int main() {
    const Theme all[] = { Theme::Dark, Theme::Purple, Theme::Light };
    for (auto t : all) {
        const auto& p = get(t); const auto nm = displayName(t);
        struct N { const char* n; juce::Colour c; };
        const N text[] = { { "ink", p.ink }, { "dim", p.dim } };
        const N bg[] = { { "window", p.window }, { "windowTop", p.windowTop }, { "windowBottom", p.windowBottom }, { "panel", p.panel }, { "card", p.card }, { "cell", p.cell } };
        double lo = 99; juce::String worst;
        for (auto& a : text) for (auto& b : bg) { const double r = contrast(a.c, b.c); if (r < lo) { lo = r; worst = juce::String(a.n) + " on " + b.n; } }
        check(lo >= 4.5, nm + ": every text / background token pair >= 4.5:1 (lowest " + juce::String(lo, 2) + ":1, " + worst + ")");
        double acc = 99; juce::String aw; const N accs[] = { { "accent1", p.accent1 }, { "accent2", p.accent2 }, { "accent3", p.accent3 }, { "violet", p.violet }, { "good", p.good }, { "warn", p.warn }, { "bad", p.bad } };
        double onA = 99; for (auto& a : accs) { onA = juce::jmin(onA, contrast(p.onAccent, a.c)); for (auto* b : { &p.panel, &p.cell, &p.waveBg }) { const double r = contrast(a.c, *b); if (r < acc) { acc = r; aw = a.n; } } }
        if (t == Theme::Light) check(acc >= 3.0, nm + ": accents >= 3:1 on panel / cell / wave lanes (lowest " + juce::String(acc, 2) + ":1, " + aw + ")");
        else std::cout << "      " << nm << ": accents on panel / cell, lowest " << acc << ":1 (" << aw << "), the original neon look\n";
        check(onA >= 3.0, nm + ": onAccent label on every filled accent >= 3:1 (bold text; lowest " + juce::String(onA, 2) + ":1)");
        bool opaque = true; for (auto c : { p.window, p.windowTop, p.windowBottom, p.panel, p.card, p.cell, p.line, p.ink, p.dim, p.onAccent, p.keyWhite, p.keyBlack, p.waveBg }) opaque &= c.isOpaque();
        check(opaque, nm + ": every surface / text token is opaque");
    }
    { const auto& l = get(Theme::Light); int maxCh = 0; for (auto c : { l.window, l.panel, l.card, l.cell, l.keyWhite, l.waveBg, l.onAccent }) maxCh = juce::jmax(maxCh, (int) c.getRed(), (int) c.getGreen(), (int) c.getBlue());
      check(maxCh <= 0xED && l.card != juce::Colours::white, "LIGHT is toned down: no pure white, brightest channel " + juce::String(maxCh) + " / 255 (" + juce::String(maxCh / 2.55, 1) + " %, <= 93 %)");
      check(l.window.getBlue() > l.window.getGreen() && l.window.getRed() > l.window.getGreen(), "LIGHT stays lilac (red, blue > green): window " + hex(l.window)); }
    check(next(Theme::Dark) == Theme::Purple && next(Theme::Purple) == Theme::Light && next(Theme::Light) == Theme::Dark, "cycle: DARK -> PURPLE -> LIGHT -> DARK");
    bool rt = true; for (auto t : all) rt &= fromString(toString(t)) == t && fromInt(toInt(t)) == t && fromMickensIndex(toMickensIndex(t)) == t;
    check(rt && toString(Theme::Purple) == "purple" && fromString("garbage", Theme::Light) == Theme::Light && fromString("1") == Theme::Purple && fromString("WHITE") == Theme::Light,
          "save / load: \"dark\" | \"purple\" | \"light\" round-trip, ints too, unknown -> fallback");
    check(fromLegacyBool(false) == Theme::Dark && fromLegacyBool(true) == Theme::Light && fromMickensIndex(0) == Theme::Purple && fromMickensIndex(1) == Theme::Dark && fromMickensIndex(2) == Theme::Light,
          "migration: old isLight false -> DARK, true -> LIGHT; Mickens' stored 0 / 1 / 2 -> PURPLE / DARK / LIGHT");
    { const auto c = readableOn(juce::Colour(0xffffd23f), get(Theme::Light).cell); check(contrast(c, get(Theme::Light).cell) >= 3.0, "readableOn: neon gold on the LIGHT cell becomes " + hex(c) + " (" + juce::String(contrast(c, get(Theme::Light).cell), 2) + ":1)"); }
    {   // GLASS (round 3): Ghosty Pro's opt-in "liquid glass" LIGHT; off by default, BLACK / PURPLE identical either way
        auto same = [](const Palette& a, const Palette& b) { return a.window == b.window && a.windowTop == b.windowTop && a.panel == b.panel && a.card == b.card && a.cell == b.cell && a.line == b.line
                                                                    && a.ink == b.ink && a.dim == b.dim && a.accent1 == b.accent1 && a.accent2 == b.accent2 && a.accent3 == b.accent3 && a.violet == b.violet && a.waveBg == b.waveBg; };
        const Palette dark0 = get(Theme::Dark), purple0 = get(Theme::Purple), light0 = get(Theme::Light);
        check(! glassLight(), "glass LIGHT is off by default (every plugin but Ghosty Pro keeps the lilac LIGHT)");
        optInGlassLight(true);
        const auto& g = get(Theme::Light);
        check(same(get(Theme::Dark), dark0) && same(get(Theme::Purple), purple0) && ! same(g, light0), "opt-in: BLACK and PURPLE unchanged, LIGHT = the glass palette");
        double lo = 99; juce::String worst;
        for (auto t : { g.ink, g.dim }) for (auto b : { g.window, g.windowTop, g.windowBottom, g.panel, g.card, g.cell, g.waveBg }) { const double r = contrast(t, b); if (r < lo) { lo = r; worst = hex(t) + " on " + hex(b); } }
        check(lo >= 4.5, "glass LIGHT: ink / dim on every surface (incl. the screen) >= 4.5:1 (lowest " + juce::String(lo, 2) + ":1, " + worst + ")");
        double acc = 99, onA = 99; juce::String aw;
        for (auto a : { g.accent1, g.accent2, g.accent3, g.violet, g.good, g.warn, g.bad }) { onA = juce::jmin(onA, contrast(g.onAccent, a)); for (auto b : { g.window, g.panel, g.card }) { const double r = contrast(a, b); if (r < acc) { acc = r; aw = hex(a) + " on " + hex(b); } } }
        check(acc >= 4.5 && onA >= 3.0, "glass LIGHT: deepened accents as text >= 4.5:1 on window / panel / card (lowest " + juce::String(acc, 2) + ":1, " + aw + "), onAccent >= 3:1 (" + juce::String(onA, 2) + ")");
        int maxCh = 0; for (auto c : { g.window, g.windowTop, g.windowBottom, g.panel, g.card, g.cell, g.keyWhite, g.waveBg, g.onAccent }) maxCh = juce::jmax(maxCh, (int) c.getRed(), (int) c.getGreen(), (int) c.getBlue());
        const bool silver = g.window.getBlue() >= g.window.getRed() && std::abs((int) g.window.getRed() - (int) g.window.getGreen()) <= 4;
        check(maxCh <= 0xFA && silver && g.panel.getBrightness() > g.window.getBrightness() && g.card.getBrightness() >= g.panel.getBrightness() && relLum(g.ink) < 0.03,
              "glass LIGHT: off-white / silver window " + hex(g.window) + " (gradient " + hex(g.windowTop) + " -> " + hex(g.windowBottom) + "), panels lighter " + hex(g.panel) + " / " + hex(g.card) + ", dark-slate ink " + hex(g.ink) + ", no pure white (max channel " + juce::String(maxCh) + ")");
        bool opaque = true; for (auto c : { g.window, g.windowTop, g.windowBottom, g.panel, g.card, g.cell, g.line, g.ink, g.dim, g.onAccent, g.waveBg }) opaque &= c.isOpaque();
        check(opaque, "glass LIGHT: every surface / text token opaque");
        optInGlassLight(false);
        check(same(get(Theme::Light), light0), "opt-out: LIGHT is the lilac palette again");
    }
    // the token table for the other plugins
    std::cout << "\nTOKEN          DARK      PURPLE    LIGHT\n";
    auto row = [&](const char* n, juce::Colour Palette::* m) { std::cout << juce::String(n).paddedRight(' ', 14) << " " << hex(get(Theme::Dark).*m) << "  " << hex(get(Theme::Purple).*m) << "  " << hex(get(Theme::Light).*m) << "\n"; };
    row("window", &Palette::window); row("windowTop", &Palette::windowTop); row("windowBottom", &Palette::windowBottom); row("panel", &Palette::panel); row("card", &Palette::card); row("cell", &Palette::cell); row("line", &Palette::line);
    row("ink", &Palette::ink); row("dim", &Palette::dim); row("onAccent", &Palette::onAccent); row("overlay", &Palette::overlay); row("accent1", &Palette::accent1); row("accent2", &Palette::accent2); row("accent3", &Palette::accent3);
    row("violet", &Palette::violet); row("good", &Palette::good); row("warn", &Palette::warn); row("bad", &Palette::bad); row("keyWhite", &Palette::keyWhite); row("keyBlack", &Palette::keyBlack); row("waveBg", &Palette::waveBg); row("waveLo", &Palette::waveLo); row("waveHi", &Palette::waveHi);
    std::cout << "shadow         #" << get(Theme::Dark).shadow.toDisplayString(true) << " #" << get(Theme::Purple).shadow.toDisplayString(true) << " #" << get(Theme::Light).shadow.toDisplayString(true) << "\n";
    std::cout << "\n" << passes << " passed, " << fails << " failed\n";
    return fails ? 1 : 0;
}
