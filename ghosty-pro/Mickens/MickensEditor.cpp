#include "MickensEditor.h"
#include "GhostGlass.h"

// Mr. Mickens UI: pitch-black hardcore panel, neon rainbow trim, two animated unicorns (one throws up a rainbow,
// one poops one) and a cartoon of Mr. Mickens himself as the logo.
namespace {
constexpr int W = 1500, H0 = 1020, H = 1056;   // H0 = the original layout (every existing control); the BAR ruler adds a strip below it
// theme colours: the house palette (Shared/GhostThemes.h), read at paint time. Mickens' stored index: PURPLE (0, default), BLACK (1),
// LIGHT (2). setTheme() copies the palette into the names this file paints with. Every surface is opaque; LIGHT accents are tuned for
// >= 3:1 on its cells.
namespace th = ghosty::themes;
juce::Colour BG, CARD, CARD2, INK, DIM, BGTOP, BGBOT, WAVEBG;
juce::Colour PINK(0xffff2bd6), CYAN(0xff22e6ff), LIME(0xffa6ff2b), GOLD(0xffffd23f), BLOOD(0xffff2650);
juce::Colour HI = juce::Colours::white;     // overlays / playheads / lock outlines (white on the dark looks, ink on LIGHT)
juce::Colour ONTXT;                         // text on an accent-filled (on) button or chip
juce::Colour LINE;                          // rules, knob tracks (LIGHT)
bool LIGHT = false; int artDepth = 0; int THEME = 0;       // artDepth > 0: drawing an illustration (logo, unicorns): its rainbow stays as drawn
// GHOST (Mickens' 4th look, stored index 3): liquid glass on the LIGHT base (Shared/GhostGlass.h). LIGHT is true too (every LIGHT branch applies);
// GLASS adds the aurora backdrop, glass panels / screens / pills / knobs. The shared Theme enum is untouched (GHOST reads Theme::Light).
bool GLASS = false;
constexpr int kLookGhost = 3;
th::Palette makeGhostPalette() {
    using C = juce::Colour; auto p = th::makePalette(th::Theme::Light);
    p.window = C(0xffebe6f8); p.windowTop = C(0xfff1edfb); p.windowBottom = C(0xffe6f2f0);
    p.panel = C(0xfff0edf9); p.card = C(0xfff7f5fd); p.cell = C(0xffe6e1f4); p.line = C(0xffb8afd8);
    p.ink = ghosty::glass::ink(); p.dim = ghosty::glass::ink2(); p.onAccent = C(0xffffffff); p.overlay = ghosty::glass::ink();
    p.accent1 = C(0xff9c1670); p.accent2 = C(0xff00606e); p.accent3 = C(0xff006a3c); p.violet = ghosty::glass::violetDeep();
    p.good = C(0xff006a3c); p.warn = C(0xff6e4700); p.bad = C(0xffa8182a);
    p.keyWhite = C(0xfff6f4fc); p.keyBlack = ghosty::glass::ink();
    p.waveBg = C(0xffe9e5f5); p.waveLo = p.accent2; p.waveHi = p.violet; p.shadow = C(0x2e3a2e6b);
    return p;
}
const th::Palette& ghostPal() { static const auto p = makeGhostPalette(); return p; }
const juce::Colour kGhostRef(0xffd6cfea);   // the darkest the frosted glass gets over the aurora: GHOST accents / rainbow read >= 4.5:1 on it
// the five accents as controls store them (the dark-look values) and as LIGHT draws them
const juce::Colour kDarkAcc[] = { th::get(th::Theme::Dark).accent1, th::get(th::Theme::Dark).accent2, th::get(th::Theme::Dark).accent3, th::get(th::Theme::Dark).warn, th::get(th::Theme::Dark).bad };
const juce::Colour kLightAcc[] = { th::get(th::Theme::Light).accent1, th::get(th::Theme::Light).accent2, th::get(th::Theme::Light).accent3, th::get(th::Theme::Light).warn, th::get(th::Theme::Light).bad };
double contrast(juce::Colour a, juce::Colour b) { return th::contrast(a, b); }
const th::Palette& pal() { return THEME == kLookGhost ? ghostPal() : th::get(th::fromMickensIndex(THEME)); }
th::Theme houseTheme(int look) { return look == kLookGhost ? th::Theme::Light : th::fromMickensIndex(look); }   // the shared tokens a look uses (BYPASS / SYNC)
int nextLook(int look) { return look == 2 ? kLookGhost : look == kLookGhost ? 1 : th::toMickensIndex(th::next(th::fromMickensIndex(look))); }   // BLACK -> PURPLE -> LIGHT -> GHOST -> BLACK
juce::String lookName(int look) { return look == kLookGhost ? juce::String("GHOST") : th::displayName(th::fromMickensIndex(look)); }
// LIGHT: an accent / rainbow colour made readable on the light cells (>= 3:1); the dark looks are untouched
juce::Colour onLight(juce::Colour c) {
    if (!LIGHT) return c;
    if (GLASS) {   // GHOST: the five accents = the deepened glass tokens, any other colour made readable on the darkest glass (>= 4.5:1)
        const juce::Colour ga[] = { ghostPal().accent1, ghostPal().accent2, ghostPal().accent3, ghostPal().warn, ghostPal().bad };
        for (int i = 0; i < 5; ++i) if (kDarkAcc[i].getARGB() == c.getARGB()) return ga[i];
        return th::readableOn(c, kGhostRef, 4.5);
    }
    for (int i = 0; i < 5; ++i) if (kDarkAcc[i].getARGB() == c.getARGB()) return kLightAcc[i];
    return th::readableOn(c, th::get(th::Theme::Light).cell, 3.0);
}
void setTheme(int theme) {
    THEME = theme == kLookGhost ? kLookGhost : juce::jlimit(0, 2, theme); GLASS = THEME == kLookGhost; const auto& P = pal(); LIGHT = P.isLight;
    BG = P.window; CARD = P.panel; CARD2 = P.cell; INK = P.ink; DIM = P.dim; BGTOP = P.windowTop; BGBOT = P.windowBottom; WAVEBG = P.waveBg;
    PINK = P.accent1; CYAN = P.accent2; LIME = P.accent3; GOLD = P.warn; BLOOD = P.bad; HI = P.overlay; ONTXT = P.onAccent; LINE = P.line;
}
struct ArtScope { ArtScope() { ++artDepth; } ~ArtScope() { --artDepth; } };
using APVTS = juce::AudioProcessorValueTreeState;

juce::Colour rainbow(float t, float sat = 0.9f, float bri = 1.0f) { t -= std::floor(t); const auto c = juce::Colour::fromHSV(t, sat, bri, 1.0f); return artDepth > 0 ? c : onLight(c); }

// ---- GHOST glass (message thread only). PERFORMANCE: the aurora once per size, big panels / pills / knob caps as cached images.
namespace gl {
namespace gg = ghosty::glass;
struct ImageCache {
    std::map<std::tuple<int, int, int, int, int>, juce::Image> m; int builds = 0;
    template <typename Fn> void draw(juce::Graphics& g, juce::Rectangle<float> r, int kind, int state, Fn&& paint) {
        const float sc = juce::jmax(1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        const int w = juce::roundToInt(r.getWidth()), h = juce::roundToInt(r.getHeight()); if (w <= 0 || h <= 0) return;
        const auto k = std::make_tuple(kind, w, h, state, juce::roundToInt(sc * 100));
        auto it = m.find(k);
        if (it == m.end()) {
            if (m.size() > 400) m.clear();
            juce::Image img(juce::Image::ARGB, juce::jmax(1, juce::roundToInt(w * sc)), juce::jmax(1, juce::roundToInt(h * sc)), true, juce::SoftwareImageType());
            { juce::Graphics ig(img); ig.addTransform(juce::AffineTransform::scale(sc)); paint(ig, juce::Rectangle<float>(0, 0, (float) w, (float) h)); }
            it = m.emplace(k, img).first; ++builds;
        }
        g.drawImageTransformed(it->second, juce::AffineTransform::scale(1.0f / sc).translated(std::round(r.getX()), std::round(r.getY())));
    }
};
ImageCache& cache() { static ImageCache c; return c; }
gg::Backdrop& backdrop() { static gg::Backdrop b; return b; }
gg::PanelCache& panels() { static gg::PanelCache c; return c; }
// a lit button keeps its colour as tinted glass (deepened until white text reads), an off one is clear glass
juce::Colour litTint(juce::Colour t) { return th::readableOn(t.withAlpha(1.0f), juce::Colours::white, 6.0); }
void pill(juce::Graphics& g, juce::Rectangle<float> bounds, bool on, juce::Colour tint, bool over, bool down, bool enabled) {
    const auto t = litTint(tint);
    cache().draw(g, bounds, 1, (on ? 1 : 0) | (over ? 2 : 0) | (down ? 4 : 0) | (enabled ? 8 : 0) | (on ? (int) (t.getARGB() & 0xffffff) << 4 : 0), [=](juce::Graphics& ig, juce::Rectangle<float> r) {
        if (!on) { gg::pill(ig, r, false, over, down, enabled); return; }
        auto b = gg::pillBody(r); if (down) b = b.translated(0, 0.6f); const float rad = juce::jmin(b.getHeight() * 0.5f, 9.0f);
        gg::shadow(ig, b, rad, down ? 1.5f : 2.6f, 1.3f, 3); gg::body(ig, b, rad, t, over ? 0.96f : 0.92f, 0.3f);
        if (!enabled) { ig.setColour(juce::Colours::white.withAlpha(0.28f)); ig.fillRoundedRectangle(b, rad); } });
}
// a glass screen (waveforms, lanes): a frosted inset, a soft inner shadow at the top, a white lower rim
void screen(juce::Graphics& g, juce::Rectangle<float> r, float radius) {
    radius = juce::jmin(radius, r.getHeight() * 0.5f, r.getWidth() * 0.5f);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xffe4dff3), r.getX(), r.getY(), juce::Colour(0xffedeaf8), r.getX(), r.getBottom(), false)); g.fillRoundedRectangle(r, radius);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0x223a2e6b), r.getX(), r.getY(), juce::Colour(0x003a2e6b), r.getX(), r.getY() + juce::jmin(14.0f, r.getHeight() * 0.35f), false)); g.fillRoundedRectangle(r, radius);
    g.setColour(juce::Colour(0x4a463e6c)); g.drawRoundedRectangle(r.reduced(0.5f), radius, 1.0f);
    g.setColour(juce::Colour(0xccffffff)); g.drawLine(r.getX() + radius, r.getBottom() - 0.6f, r.getRight() - radius, r.getBottom() - 0.6f, 1.2f);
}
// the glossy knob: a frosted track, the violet -> teal value arc, a cached glass cap, an indigo pointer
void knob(juce::Graphics& g, juce::Rectangle<float> c, float a0, float a1, float from, float to, float ang, bool enabled) {
    const float sz = c.getWidth(), thk = juce::jmax(3.0f, sz * 0.09f), rr = sz / 2 - thk / 2;
    juce::Path tr; tr.addCentredArc(c.getCentreX(), c.getCentreY(), rr, rr, 0, a0, a1, true);
    g.setColour(juce::Colour(0x30443a7a)); g.strokePath(tr, juce::PathStrokeType(thk, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (to - from > 0.001f) {
        juce::Path va; va.addCentredArc(c.getCentreX(), c.getCentreY(), rr, rr, 0, from, to, true);
        if (enabled) g.setGradientFill(juce::ColourGradient(gg::violet(), c.getX(), c.getBottom(), gg::teal(), c.getRight(), c.getY(), false)); else g.setColour(juce::Colour(0x60443a7a));
        g.strokePath(va, juce::PathStrokeType(thk, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    const float cr = juce::roundToInt((sz / 2 - thk - 1) * 2.0f) * 0.5f, pad = juce::jmax(2.0f, cr * 0.22f) * 1.6f + 2.0f;
    cache().draw(g, juce::Rectangle<float>(c.getCentreX() - cr - pad, c.getCentreY() - cr - pad, (cr + pad) * 2, (cr + pad) * 2), 2, 0, [cr](juce::Graphics& ig, juce::Rectangle<float> b) {
        const float x = b.getCentreX(), y = b.getCentreY(); const juce::Rectangle<float> cap(x - cr, y - cr, cr * 2, cr * 2);
        gg::shadow(ig, cap, cr, juce::jmax(2.0f, cr * 0.22f), 1.0f, 4);
        ig.setGradientFill(juce::ColourGradient(juce::Colours::white, x - cr * 0.4f, cap.getY() + 1, juce::Colour(0xffdcd4f5), x, cap.getBottom(), false)); ig.fillEllipse(cap);
        ig.setColour(juce::Colour(0xb0ffffff)); ig.drawEllipse(cap.reduced(0.7f), 1.4f);
        ig.setColour(juce::Colour(0x50a06bff)); ig.drawEllipse(cap.reduced(cr * 0.18f), 0.8f); });
    g.setColour(enabled ? gg::violetDeep() : juce::Colour(0x80463e6c));
    g.drawLine({ c.getCentre().getPointOnCircumference(sz * 0.1f, ang), c.getCentre().getPointOnCircumference(cr * 0.82f, ang) }, 2.0f);
}
} // namespace gl

struct LnF : juce::LookAndFeel_V4 {
    LnF() { refresh(); }
    void refresh() {
        // LIGHT: an opaque light scheme for everything JUCE draws itself (sliders, scroll bars, tooltips, alerts, text editors); the
        // dark looks keep JUCE's dark scheme exactly as before
        const auto& P = pal();
        if (LIGHT) setColourScheme({ P.window, P.panel, P.card, P.line, P.ink, P.violet, P.onAccent, P.violet, P.ink });
        else setColourScheme(getDarkColourScheme());
        if (LIGHT) { setColour(juce::TooltipWindow::backgroundColourId, P.card); setColour(juce::TooltipWindow::outlineColourId, P.violet);
                     setColour(juce::Slider::trackColourId, P.violet); setColour(juce::Slider::thumbColourId, P.violet.darker(0.15f)); setColour(juce::Slider::backgroundColourId, CARD2);
                     setColour(juce::ListBox::backgroundColourId, CARD); setColour(juce::ScrollBar::thumbColourId, P.line.darker(0.2f));
                     setColour(juce::TextEditor::highlightColourId, P.card.interpolatedWith(P.violet, 0.25f)); setColour(juce::TextEditor::highlightedTextColourId, INK); setColour(juce::CaretComponent::caretColourId, INK);
                     setColour(juce::PopupMenu::highlightedTextColourId, INK); setColour(juce::Label::outlineWhenEditingColourId, CYAN); setColour(juce::Label::backgroundWhenEditingColourId, P.card); setColour(juce::Label::textWhenEditingColourId, INK); }
        setColour(juce::ComboBox::backgroundColourId, CARD2); setColour(juce::ComboBox::textColourId, INK); setColour(juce::ComboBox::arrowColourId, PINK);
        setColour(juce::PopupMenu::backgroundColourId, CARD2); setColour(juce::PopupMenu::textColourId, INK); setColour(juce::PopupMenu::highlightedBackgroundColourId, LIGHT ? pal().card.interpolatedWith(PINK, 0.22f) : PINK.withAlpha(0.35f));
        setColour(juce::PopupMenu::headerTextColourId, CYAN);
        setColour(juce::TextButton::textColourOffId, INK); setColour(juce::TextButton::textColourOnId, ONTXT); setColour(juce::Label::textColourId, INK);
        setColour(juce::TextEditor::backgroundColourId, CARD2); setColour(juce::TextEditor::textColourId, INK); setColour(juce::TextEditor::outlineColourId, PINK.withAlpha(0.4f));
        setColour(juce::BubbleComponent::backgroundColourId, CARD2); setColour(juce::BubbleComponent::outlineColourId, PINK); setColour(juce::TooltipWindow::textColourId, INK);
        setColour(juce::AlertWindow::backgroundColourId, CARD); setColour(juce::AlertWindow::textColourId, INK);
        if (GLASS) {   // GHOST: white on the lit (tinted glass) buttons, the violet glass menu highlight, violet chevrons
            setColour(juce::TextButton::textColourOnId, juce::Colours::white); setColour(juce::ComboBox::arrowColourId, ghosty::glass::violetDeep());
            setColour(juce::PopupMenu::highlightedBackgroundColourId, ghosty::glass::violetDeep()); setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
            setColour(juce::PopupMenu::backgroundColourId, pal().card); setColour(juce::TooltipWindow::backgroundColourId, pal().card);
        }
    }
    // GHOST only (every other look keeps the code below / LookAndFeel_V4 unchanged)
    void drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float mn, float mx, juce::Slider::SliderStyle st, juce::Slider& s) override {
        if (!GLASS || st != juce::Slider::LinearHorizontal) { juce::LookAndFeel_V4::drawLinearSlider(g, x, y, w, h, pos, mn, mx, st, s); return; }
        const float d = juce::jlimit(10.0f, 16.0f, (float) h - 4.0f); const auto tr = juce::Rectangle<float>((float) x, y + h * 0.5f - 3.0f, (float) w, 6.0f);
        ghosty::glass::track(g, tr, (float) x, pos, s.isEnabled());
        ghosty::glass::thumb(g, { juce::jlimit(x + d * 0.5f - 2.0f, x + w - d * 0.5f + 2.0f, pos), y + h * 0.5f }, d);
    }
    void drawPopupMenuBackground(juce::Graphics& g, int w, int h) override {
        if (!GLASS) { juce::LookAndFeel_V4::drawPopupMenuBackground(g, w, h); return; }
        g.fillAll(pal().card); ghosty::glass::card(g, juce::Rectangle<float>(0, 0, (float) w, (float) h), 6.0f);
    }
    void drawTooltip(juce::Graphics& g, const juce::String& text, int w, int h) override {
        if (!GLASS) { juce::LookAndFeel_V4::drawTooltip(g, text, w, h); return; }
        const auto r = juce::Rectangle<float>(0, 0, (float) w, (float) h); g.fillAll(pal().card); ghosty::glass::card(g, r, 6.0f);
        juce::AttributedString a; a.setJustification(juce::Justification::centred); a.append(text, juce::FontOptions(13.0f, juce::Font::bold), INK);
        juce::TextLayout tl; tl.createLayoutWithBalancedLineLengths(a, 400.0f); tl.draw(g, r);
    }
    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override {
        auto r = b.getLocalBounds().toFloat().reduced(1); const bool on = b.getToggleState();
        const juce::Colour tint = onLight(b.getProperties().contains("tint") ? juce::Colour((juce::uint32) (juce::int64) b.getProperties()["tint"]) : PINK);   // tints are stored as the dark-look colour
        if (GLASS) { gl::pill(g, b.getLocalBounds().toFloat(), on, tint, over, down, b.isEnabled()); return; }   // GHOST: a glass pill (lit = its tint as glass)
        g.setColour(on ? tint.withAlpha(LIGHT ? 1.0f : 0.9f) : CARD2); g.fillRoundedRectangle(r, 6);
        g.setColour((on ? tint.brighter(0.3f) : tint).withAlpha(over ? 1.0f : 0.5f)); g.drawRoundedRectangle(r, 6, down ? 2.0f : 1.1f);
    }
    juce::Font getTextButtonFont(juce::TextButton&, int h) override { return juce::FontOptions(juce::jmin(12.5f, h * 0.5f), juce::Font::bold); }
    void drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& cb) override {
        if (GLASS) {   // GHOST: a clear glass pill, a violet chevron
            const auto r = juce::Rectangle<float>(0, 0, (float) w, (float) h); gl::pill(g, r, false, PINK, cb.isMouseOver(true), false, cb.isEnabled());
            const auto b = ghosty::glass::pillBody(r); juce::Path a; const float ax = b.getRight() - 9.0f, ay = b.getCentreY();
            a.startNewSubPath(ax - 3.5f, ay - 1.8f); a.lineTo(ax, ay + 2.2f); a.lineTo(ax + 3.5f, ay - 1.8f);
            g.setColour(ghosty::glass::violetDeep()); g.strokePath(a, juce::PathStrokeType(1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); return;
        }
        auto r = juce::Rectangle<float>(0, 0, (float) w, (float) h).reduced(1); g.setColour(CARD2); g.fillRoundedRectangle(r, 6);
        g.setColour(CYAN.withAlpha(0.45f)); g.drawRoundedRectangle(r, 6, 1.1f);
        juce::Path a; a.addTriangle((float) w - 15, h * 0.5f - 2, (float) w - 7, h * 0.5f - 2, (float) w - 11, h * 0.5f + 3); g.setColour(CYAN); g.fillPath(a);
    }
    juce::Font getComboBoxFont(juce::ComboBox&) override { return juce::FontOptions(12.0f, juce::Font::bold); }
    void positionComboBoxText(juce::ComboBox& b, juce::Label& l) override { l.setBounds(4, 1, b.getWidth() - 20, b.getHeight() - 2); l.setFont(getComboBoxFont(b)); }
    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) override {
        auto r = juce::Rectangle<float>((float) x, (float) y, (float) w, (float) h).reduced(2); const float sz = juce::jmin(r.getWidth(), r.getHeight());
        auto c = r.withSizeKeepingCentre(sz, sz); const float th = juce::jmax(3.0f, sz * 0.09f), ang = a0 + pos * (a1 - a0);
        if (GLASS) {   // GHOST: a glossy glass knob with the violet -> teal arc
            const bool bp = s.getMinimum() < 0 && s.getMaximum() > 0; const float md = a0 + (a1 - a0) * (float) ((0 - s.getMinimum()) / (s.getMaximum() - s.getMinimum()));
            gl::knob(g, c, a0, a1, bp ? juce::jmin(md, ang) : a0, bp ? juce::jmax(md, ang) : ang, ang, s.isEnabled()); return;
        }
        g.setColour(CARD2); g.fillEllipse(c.reduced(th + 1));
        g.setColour(juce::Colours::black.withAlpha(0.5f)); g.drawEllipse(c.reduced(th + 1), 1.0f);
        juce::Path tr; tr.addCentredArc(c.getCentreX(), c.getCentreY(), sz / 2 - th / 2, sz / 2 - th / 2, 0, a0, a1, true);
        g.setColour(LIGHT ? LINE : juce::Colours::white.withAlpha(0.07f)); g.strokePath(tr, juce::PathStrokeType(th, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        // rainbow arc: the hue follows the knob around
        const bool bip = s.getMinimum() < 0 && s.getMaximum() > 0; const float mid = a0 + (a1 - a0) * (float) ((0 - s.getMinimum()) / (s.getMaximum() - s.getMinimum()));
        const float from = bip ? juce::jmin(mid, ang) : a0, to = bip ? juce::jmax(mid, ang) : ang;
        const int segs = juce::jmax(1, (int) ((to - from) / 0.12f));
        for (int i = 0; i < segs; ++i) {
            const float s0 = from + (to - from) * i / segs, s1 = from + (to - from) * (i + 1) / segs;
            juce::Path ar; ar.addCentredArc(c.getCentreX(), c.getCentreY(), sz / 2 - th / 2, sz / 2 - th / 2, 0, s0, s1 + 0.01f, true);
            g.setColour(rainbow(0.83f - (s0 - a0) / (a1 - a0) * 0.83f)); g.strokePath(ar, juce::PathStrokeType(th, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
        }
        g.setColour(INK); g.drawLine({ c.getCentre().getPointOnCircumference(sz * 0.1f, ang), c.getCentre().getPointOnCircumference(sz / 2 - th - 3, ang) }, 2.0f);
    }
    void drawPopupMenuSectionHeader(juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& t) override {
        g.setColour(CYAN); g.setFont(juce::FontOptions(12.0f, juce::Font::bold)); g.drawText(t.toUpperCase(), area.reduced(8, 0), juce::Justification::bottomLeft);
    }
};

// ----------------------------------------------------------------------------------------- drawings
// the logo: a cute chibi unicorn with a handlebar mustache and round glasses. The lenses are slowly spinning
// pastel hypno-spirals with big sparkles, the horn's rainbow bands crawl up, the curly mane bounces,
// a soft rainbow ribbon pours out of its smile and the air twinkles.
void drawLogo(juce::Graphics& g, juce::Rectangle<float> r, float t) {
    const ArtScope art;
    const float s = r.getWidth() / 100.0f; auto P = [&](float x, float y) { return juce::Point<float>(r.getX() + x * s, r.getY() + y * s); };
    auto R = [&](float x0, float y0, float x1, float y1) { return juce::Rectangle<float>(P(x0, y0), P(x1, y1)); };
    auto star = [&](juce::Point<float> c, float z, juce::Colour col) { juce::Path st; for (int k = 0; k < 8; ++k) { const auto q = c.getPointOnCircumference(k % 2 ? z * 0.35f : z, k * juce::MathConstants<float>::pi / 4); if (k == 0) st.startNewSubPath(q); else st.lineTo(q); } st.closeSubPath(); g.setColour(col); g.fillPath(st); };
    juce::Graphics::ScopedSaveState ss(g);
    juce::Path clip; clip.addRoundedRectangle(r, 16 * s); g.reduceClipRegion(clip);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff3b2466), r.getCentreX(), r.getY(), juce::Colour(0xff1a0f2e), r.getCentreX(), r.getBottom(), false)); g.fillRect(r);
    // twinkles
    juce::Random gr(5); for (int i = 0; i < 18; ++i) { const float x = gr.nextFloat() * 100, y = gr.nextFloat() * 100, tw = 0.5f + 0.5f * std::sin(t * 3 + i * 1.7f);
        star(P(x, y), (0.8f + 1.6f * tw) * s, rainbow(gr.nextFloat(), 0.35f).withAlpha(0.4f + 0.6f * tw)); }
    // curly pastel mane: fat bouncy strands with round ends, all around the head
    for (int i = 0; i < 26; ++i) {
        const float u = i / 25.0f, a = juce::MathConstants<float>::pi * (0.8f + 1.4f * u);
        const float len = 15 + 5 * std::sin(i * 2.1f), bounce = std::sin(t * 4 + i * 0.8f) * 0.08f;
        const auto c = P(50, 46);
        juce::Path m; for (int k = 0; k <= 12; ++k) { const float f = k / 12.0f, rr = (18 + len * f) * s;
            const auto q = c.getPointOnCircumference(rr, a + juce::MathConstants<float>::halfPi + bounce * f + std::sin(f * 9 + i) * 0.12f * f);
            if (k == 0) m.startNewSubPath(q); else m.lineTo(q); }
        g.setColour(rainbow(i / 25.0f * 0.85f + 0.02f, 0.45f)); g.strokePath(m, juce::PathStrokeType(4.2f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    const juce::Colour fur(0xffd9b8ff), furD(0xffb48ae8), furL(0xfff1e4ff);
    // ears
    juce::Path ear; ear.addTriangle(P(27, 32), P(26, 14), P(39, 25)); ear.addTriangle(P(73, 32), P(74, 14), P(61, 25)); g.setColour(fur); g.fillPath(ear);
    juce::Path earIn; earIn.addTriangle(P(29, 29), P(28.5f, 18), P(36, 25)); earIn.addTriangle(P(71, 29), P(71.5f, 18), P(64, 25)); g.setColour(juce::Colour(0xffffb8e6)); g.fillPath(earIn);
    // big round chibi head + a short soft snout
    g.setGradientFill(juce::ColourGradient(furL, r.getX() + 40 * s, r.getY() + 24 * s, furD, r.getX() + 60 * s, r.getY() + 76 * s, false));
    g.fillEllipse(R(25, 22, 75, 70));
    g.setColour(juce::Colour(0xffe8d2ff)); g.fillEllipse(R(35, 56, 65, 78));
    g.setColour(juce::Colour(0xff9a6fd0)); g.fillEllipse(R(42, 62, 46, 65.5f)); g.fillEllipse(R(54, 62, 58, 65.5f));
    // rosy cheeks
    g.setColour(juce::Colour(0x88ff7fc4)); g.fillEllipse(R(26, 52, 35, 58)); g.fillEllipse(R(65, 52, 74, 58));
    // horn: rainbow bands crawling up
    { juce::Path horn; horn.addTriangle(P(44, 25), P(56, 25), P(51, 2)); juce::Graphics::ScopedSaveState hs(g); g.reduceClipRegion(horn);
      for (int i = -2; i < 12; ++i) { const float y = 25 - (i * 3.0f + std::fmod(t * 4, 3.0f)); juce::Path band; band.startNewSubPath(P(40, y + 1.5f)); band.lineTo(P(60, y - 1.0f)); band.lineTo(P(60, y - 4.0f)); band.lineTo(P(40, y - 1.5f)); band.closeSubPath();
          g.setColour(rainbow(i / 7.0f, 0.55f)); g.fillPath(band); }
      g.setColour(juce::Colours::white.withAlpha(0.5f)); g.drawLine({ P(47, 24), P(50.5f, 5) }, 0.9f * s); }
    // round glasses with slow pastel hypno-spirals and big sparkles
    for (int e = 0; e < 2; ++e) {
        const auto c = P(e ? 61.0f : 39.0f, 44); const float rad = 10.0f * s;
        juce::Path lens; lens.addEllipse(c.x - rad, c.y - rad, rad * 2, rad * 2);
        { juce::Graphics::ScopedSaveState es(g); g.reduceClipRegion(lens);
          for (int k = 8; k >= 1; --k) { const float rr = rad * k / 8.0f; g.setColour(rainbow(k / 8.0f - t * 0.25f, 0.45f)); g.fillEllipse(c.x - rr, c.y - rr, rr * 2, rr * 2); }
          const float spin = (e ? -1.0f : 1.0f) * t * 2.2f;
          for (int arm = 0; arm < 3; ++arm) { juce::Path sp; for (int k = 0; k <= 24; ++k) { const float u = k / 24.0f, ang = spin + arm * juce::MathConstants<float>::twoPi / 3 + u * 6.0f;
                  const auto q = c.getPointOnCircumference(rad * u, ang); if (k == 0) sp.startNewSubPath(q); else sp.lineTo(q); }
              g.setColour(juce::Colours::white.withAlpha(0.55f)); g.strokePath(sp, juce::PathStrokeType(1.3f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); }
          g.setColour(juce::Colour(0xff2a1a40)); g.fillEllipse(c.x - 3.2f * s, c.y - 3.2f * s, 6.4f * s, 6.4f * s);       // big pupil
          g.setColour(juce::Colours::white); g.fillEllipse(c.x - 2.6f * s, c.y - 2.8f * s, 2.4f * s, 2.4f * s);          // shine
          g.setColour(juce::Colours::white.withAlpha(0.7f)); g.fillEllipse(c.x - rad * 0.65f, c.y - rad * 0.72f, rad * 0.5f, rad * 0.32f); }
        star(c.translated(rad * 0.62f, -rad * 0.55f), (1.4f + 0.8f * std::sin(t * 5 + e * 2)) * s, juce::Colours::white);
        g.setColour(juce::Colour(0xff8a5530)); g.drawEllipse(c.x - rad, c.y - rad, rad * 2, rad * 2, 2.2f * s);
    }
    g.setColour(juce::Colour(0xff8a5530)); juce::Path br; br.startNewSubPath(P(49, 43)); br.quadraticTo(P(50, 41), P(51, 43)); g.strokePath(br, juce::PathStrokeType(1.8f * s));
    // smile
    juce::Path sm; sm.startNewSubPath(P(44, 72)); sm.quadraticTo(P(50, 76.5f), P(56, 72)); g.setColour(juce::Colour(0xff7a4fb0)); g.strokePath(sm, juce::PathStrokeType(1.4f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // handlebar mustache, curled tips, gently wiggling
    { juce::Path mu; const float w = std::sin(t * 3) * 0.6f;
      mu.startNewSubPath(P(50, 69)); mu.cubicTo(P(45, 67), P(40, 68), P(36, 72)); mu.cubicTo(P(33, 75), P(29, 74 + w), P(29.5f, 71 + w)); mu.cubicTo(P(30, 69.5f + w), P(32, 69.5f + w), P(32, 71 + w));
      mu.cubicTo(P(32, 72.5f), P(34, 72.5f), P(36.5f, 70)); mu.cubicTo(P(40, 66.5f), P(46, 66), P(50, 67.5f));
      mu.cubicTo(P(54, 66), P(60, 66.5f), P(63.5f, 70)); mu.cubicTo(P(66, 72.5f), P(68, 72.5f), P(68, 71 - w)); mu.cubicTo(P(68, 69.5f - w), P(70, 69.5f - w), P(70.5f, 71 - w)); mu.cubicTo(P(71, 74 - w), P(67, 75), P(64, 72));
      mu.cubicTo(P(60, 68), P(55, 67), P(50, 69)); mu.closeSubPath();
      g.setColour(juce::Colour(0xff9a5a2e)); g.fillPath(mu); g.setColour(juce::Colour(0xffc98a52)); g.strokePath(mu, juce::PathStrokeType(0.6f * s)); }
    // a soft rainbow ribbon flowing out of the smile, with sparkles
    for (int i = 0; i < 6; ++i) {
        juce::Path pk; const float x0 = 46.5f + i * 1.4f; pk.startNewSubPath(P(x0, 75));
        for (int k = 1; k <= 8; ++k) { const float y = 75 + k * 3.2f; pk.lineTo(P(x0 + std::sin(t * 3 + k * 0.8f) * 2.0f + k * 1.2f + (i - 2.5f) * k * 0.25f, y)); }
        g.setColour(rainbow(i / 6.0f, 0.55f)); g.strokePath(pk, juce::PathStrokeType(2.0f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    for (int k = 0; k < 6; ++k) { const float u = std::fmod(t * 0.6f + k / 6.0f, 1.0f); star(P(52 + k * 3.0f - 6 + u * 8, 80 + u * 18), (1.0f + u) * s, juce::Colours::white.withAlpha(1 - u)); }
}

// a unicorn: white body, rainbow mane, gold horn, glowing red eye (it's hardcore). facing right unless flipped.
void drawUnicorn(juce::Graphics& g, juce::Point<float> at, float s, bool flip, float t) {
    const ArtScope art;
    juce::Graphics::ScopedSaveState ss(g);
    g.addTransform(juce::AffineTransform::scale(flip ? -s : s, s).translated(at));
    const float bob = std::sin(t * 6.0f) * 1.5f;
    const juce::Colour white(0xfff7f2ff), shade(0xffcfc2e6);
    // legs (trotting)
    for (int i = 0; i < 4; ++i) { const float lx = -26.0f + i * 15.0f, sw = std::sin(t * 8.0f + i * 1.7f) * 4.0f; g.setColour(i % 2 ? shade : white);
        g.fillRoundedRectangle(lx + sw * 0.3f, 8 + bob, 7, 26, 3); g.setColour(juce::Colour(0xff3a2350)); g.fillRoundedRectangle(lx + sw * 0.3f - 0.5f, 31 + bob, 8, 5, 2); }
    // tail: rainbow strands
    for (int i = 0; i < 6; ++i) { juce::Path tl; tl.startNewSubPath(-34, -6 + bob); tl.cubicTo(-50, -14.0f + i * 3, -54, 6.0f + i * 3 + std::sin(t * 5 + i) * 3, -44, 22.0f + i * 2);
        g.setColour(rainbow(i / 6.0f)); g.strokePath(tl, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); }
    // body
    g.setColour(white); g.fillEllipse(-38, -18 + bob, 70, 34);
    g.setColour(shade.withAlpha(0.6f)); g.fillEllipse(-30, 2 + bob, 54, 12);
    // neck + head
    juce::Path neck; neck.startNewSubPath(14, -10 + bob); neck.lineTo(30, -38 + bob); neck.lineTo(42, -32 + bob); neck.lineTo(32, 4 + bob); neck.closeSubPath(); g.setColour(white); g.fillPath(neck);
    g.fillEllipse(26, -46 + bob, 30, 20);
    g.fillRoundedRectangle(44, -40 + bob, 18, 13, 6);   // snout
    g.setColour(juce::Colour(0xffffb3d9)); g.fillEllipse(57, -36 + bob, 3, 3);
    // horn: gold with a twist
    juce::Path horn; horn.addTriangle(36, -44 + bob, 44, -46 + bob, 50, -72 + bob); g.setGradientFill(juce::ColourGradient(kDarkAcc[3], 40, -44 + bob, juce::Colour(0xffff8fe0), 50, -72 + bob, false)); g.fillPath(horn);
    g.setColour(juce::Colour(0xffb8860b)); for (int i = 1; i < 4; ++i) g.drawLine(38.0f + i * 2.5f, -46.0f - i * 6.5f + bob, 44.0f + i * 1.2f, -47.0f - i * 6.0f + bob, 1.0f);
    // ear
    juce::Path ear; ear.addTriangle(30, -44 + bob, 34, -56 + bob, 37, -44 + bob); g.setColour(shade); g.fillPath(ear);
    // mane: rainbow strands down the neck
    for (int i = 0; i < 6; ++i) { juce::Path mn; mn.startNewSubPath(32.0f - i * 1.0f, -44 + bob); mn.cubicTo(20.0f - i * 2, -36.0f + i * 2 + bob, 22.0f - i * 3 + std::sin(t * 6 + i) * 2, -16.0f + i * 3 + bob, 12.0f - i * 2, -6.0f + i * 2 + bob);
        g.setColour(rainbow(i / 6.0f)); g.strokePath(mn, juce::PathStrokeType(3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); }
    // handlebar mustache + round glasses (house style)
    { juce::Path mu; mu.startNewSubPath(50, -30 + bob); mu.cubicTo(54, -33 + bob, 58, -33 + bob, 61, -30 + bob); mu.cubicTo(63, -28 + bob, 64, -30 + bob, 63.5f, -32 + bob);
      mu.cubicTo(65, -29 + bob, 63, -26 + bob, 60, -28 + bob); mu.cubicTo(57, -30 + bob, 53, -30 + bob, 48, -27 + bob); mu.cubicTo(46, -29 + bob, 47, -32 + bob, 50, -30 + bob); mu.closeSubPath();
      g.setColour(juce::Colour(0xff7a4522)); g.fillPath(mu); }
    // big cute eye with a shine, and a rosy cheek
    g.setColour(juce::Colour(0xff2a1a40)); g.fillEllipse(37.5f, -42 + bob, 7.5f, 8.5f);
    g.setColour(juce::Colours::white); g.fillEllipse(38.5f, -41 + bob, 3, 3); g.fillEllipse(42, -37 + bob, 1.4f, 1.4f);
    g.setColour(juce::Colour(0x88ff7fc4)); g.fillEllipse(44, -33 + bob, 7, 4);
    g.setColour(juce::Colour(0xff6b3a1c)); g.drawEllipse(36, -43.5f + bob, 9, 9, 1.6f); g.drawLine(45, -39 + bob, 50, -38 + bob, 1.4f); g.drawLine(36, -39 + bob, 31, -41 + bob, 1.4f);
}

// rainbow puke: a thick arc of stripes from the mouth, plus chunks flying
void drawPuke(juce::Graphics& g, juce::Point<float> from, juce::Point<float> to, float t, float s) {
    const ArtScope art;
    const float lift = 70 * s;
    for (int i = 0; i < 6; ++i) {
        const float off = (i - 2.5f) * 3.2f * s;
        juce::Path p; p.startNewSubPath(from.translated(0, off * 0.4f));
        p.cubicTo(from.translated(40 * s, -lift + off), to.translated(-60 * s, -lift * 0.6f + off), to.translated(0, off));
        juce::PathStrokeType st(3.4f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        g.setColour(rainbow(i / 6.0f)); g.strokePath(p, st);
    }
    // flying chunks travel along the arc
    for (int k = 0; k < 14; ++k) {
        const float u = std::fmod(t * 0.7f + k / 14.0f, 1.0f);
        const auto a = from, b = from.translated(40 * s, -lift), c = to.translated(-60 * s, -lift * 0.6f), d = to;
        const float v = 1 - u; auto pt = a * (v * v * v) + b * (3 * v * v * u) + c * (3 * v * u * u) + d * (u * u * u);
        const float r = (2.0f + (k % 3)) * s; g.setColour(rainbow(k / 14.0f + t * 0.2f).withAlpha(0.95f)); g.fillEllipse(pt.x - r, pt.y - r + std::sin(k + t * 9) * 3 * s, r * 2, r * 2);
    }
    // the splash puddle
    for (int i = 0; i < 6; ++i) { g.setColour(rainbow(i / 6.0f).withAlpha(0.8f)); g.fillEllipse(to.x - (22 - i * 3) * s, to.y - 2 * s + i * 0.6f * s, (44 - i * 6) * s, 6 * s); }
}

// rainbow poop: soft-serve swirls dropping from behind, piling up, with sparkles
void drawPoop(juce::Graphics& g, juce::Point<float> from, float groundY, float t, float s) {
    const ArtScope art;
    for (int k = 0; k < 4; ++k) {   // falling droplets
        const float u = std::fmod(t * 1.1f + k * 0.25f, 1.0f); const float y = from.y + (groundY - from.y - 30 * s) * u * u;
        g.setColour(rainbow(k / 4.0f + t * 0.3f)); g.fillEllipse(from.x - 4 * s, y - 4 * s, 8 * s, 9 * s);
    }
    // the pile: 4 stacked swirls getting smaller, each striped in rainbow
    const float px = from.x - 6 * s;
    for (int i = 0; i < 4; ++i) {
        const float w = (38 - i * 8) * s, h = 10 * s, y = groundY - 9 * s - i * 8.5f * s;
        juce::Path blob; blob.addRoundedRectangle(px - w / 2, y, w, h, h / 2);
        g.setColour(rainbow(0.05f + i * 0.2f + t * 0.1f)); g.fillPath(blob);
        g.setColour(juce::Colours::white.withAlpha(0.35f)); g.fillRoundedRectangle(px - w / 2 + 4 * s, y + 2 * s, w * 0.35f, 2.5f * s, 1.2f * s);
    }
    juce::Path tip; tip.addTriangle(px - 5 * s, groundY - 42 * s, px + 5 * s, groundY - 42 * s, px + 2 * s, groundY - 52 * s); g.setColour(rainbow(0.85f + t * 0.1f)); g.fillPath(tip);
    // sparkles
    for (int k = 0; k < 7; ++k) { const float a = t * 2 + k * 0.9f; const float r = (30 + 12 * std::sin(a * 1.3f)) * s; const auto c = juce::Point<float>(px + std::cos(a) * r, groundY - 26 * s + std::sin(a) * r * 0.5f);
        const float z = (2.5f + 1.5f * std::sin(t * 7 + k)) * s; juce::Path st; st.startNewSubPath(c.x, c.y - z * 2); st.lineTo(c.x + z * 0.5f, c.y - z * 0.5f); st.lineTo(c.x + z * 2, c.y); st.lineTo(c.x + z * 0.5f, c.y + z * 0.5f);
        st.lineTo(c.x, c.y + z * 2); st.lineTo(c.x - z * 0.5f, c.y + z * 0.5f); st.lineTo(c.x - z * 2, c.y); st.lineTo(c.x - z * 0.5f, c.y - z * 0.5f); st.closeSubPath(); g.setColour(rainbow(k / 7.0f, 0.4f)); g.fillPath(st); }
}

// 8-bit jetskis: pixel sprites (a little unicorn rider) racing over scrolling pixel waves
void drawPixelWater(juce::Graphics& g, juce::Rectangle<float> r, float t, float px) {
    const int cols = (int) (r.getWidth() / px) + 1, rows = (int) (r.getHeight() / px);
    const juce::Colour deep(0xff2b4fd8), mid(0xff3f86ff), lite(0xff7fd4ff), foam(0xffeaffff);
    for (int c = 0; c < cols; ++c) {
        const int crest = (int) std::round(1.5f + 1.5f * std::sin((c + t * 14.0f) * 0.35f));
        for (int y = crest; y < rows; ++y) { g.setColour(y == crest ? foam : y < crest + 2 ? lite : y < crest + 4 ? mid : deep); g.fillRect(r.getX() + c * px, r.getY() + y * px, px, px); }
    }
}
void drawJetski(juce::Graphics& g, float x, float y, float px, bool faceRight, float hue, float t) {
    const ArtScope art;
    static const char* const S[] = {
        "..........HH........",
        ".........HHHM.......",
        ".........HEHM.......",
        "..........HHM.......",
        "..........PP........",
        "....Y....PPP........",
        "...YY...PPPPBBBB....",
        "BBBBBBBBBBBBBBBBB...",
        "WWWWWWWWWWWWWWWWBB..",
        ".DDDDDDDDDDDDDDDD...",
    };
    const int w = 20;
    const juce::Colour body = rainbow(hue, 0.75f), shirt = rainbow(hue + 0.5f, 0.6f);
    for (int row = 0; row < 10; ++row) for (int c = 0; c < w; ++c) {
        const char ch = S[row][c]; if (ch == '.') continue;
        juce::Colour col = ch == 'B' ? body : ch == 'W' ? juce::Colours::white : ch == 'D' ? body.darker(0.6f) : ch == 'Y' ? juce::Colour(0xff9ff3ff)
                         : ch == 'H' ? juce::Colour(0xffe2c8ff) : ch == 'E' ? juce::Colour(0xff2a1a40) : ch == 'M' ? rainbow(row / 4.0f + t, 0.6f) : shirt;
        const int cx = faceRight ? (w - 1 - c) : c;
        g.setColour(col); g.fillRect(x + cx * px, y + row * px, px, px);
    }
    // spray out the back
    juce::Random rr((int) (t * 20) + (int) hue * 100);
    for (int i = 0; i < 7; ++i) { const int dx = 18 + rr.nextInt(6), dy = 6 + rr.nextInt(4); const int cx = faceRight ? (w - 1 - dx) : dx;
        g.setColour(juce::Colours::white.withAlpha(0.6f + rr.nextFloat() * 0.4f)); g.fillRect(x + cx * px, y + dy * px, px, px); }
}

void rainbowText(juce::Graphics& g, const juce::String& txt, juce::Rectangle<float> r, float size, float phase) {
    const ArtScope art;
    juce::Font f(juce::FontOptions(size, juce::Font::bold));
    juce::GlyphArrangement ga; ga.addLineOfText(f, txt, r.getX(), r.getCentreY() + size * 0.35f);
    juce::Path p; ga.createPath(p);
    g.setColour(juce::Colours::black); g.strokePath(p, juce::PathStrokeType(5.0f), juce::AffineTransform::translation(2, 3));
    juce::ColourGradient cg(rainbow(phase), r.getX(), 0, rainbow(phase + 0.8f), r.getX() + f.getStringWidthFloat(txt), 0, false);
    for (int i = 1; i < 6; ++i) cg.addColour(i / 6.0, rainbow(phase + i * 0.14f));
    g.setGradientFill(cg); g.fillPath(p);
    g.setColour(juce::Colours::white.withAlpha(0.5f)); g.strokePath(p, juce::PathStrokeType(0.8f));
}

// ----------------------------------------------------------------------------------------- widgets
// real-unit readouts for the knobs (shown under the knob while it is hovered or dragged)
juce::String fmtValue(const juce::String& id, double v) {
    auto db = [](double g) { return g <= 0.0005 ? juce::String("-inf dB") : juce::String(20 * std::log10(g), 1) + " dB"; };
    auto hz = [](double f) { return f >= 1000 ? juce::String(f / 1000, 2) + " kHz" : juce::String(juce::roundToInt(f)) + " Hz"; };
    auto pc = [](double x) { return juce::String(juce::roundToInt(x * 100)) + " %"; };
    const auto s = id.fromFirstOccurrenceOf("_", false, false); const bool bay = id.startsWith("b") && id.length() > 2 && id[2] == '_';
    const juce::String k = bay ? s : id;
    if (k == "lp_cut" || k == "hp_cut" || k == "bp_cut" || k == "nt_cut" || k == "st_cut") return hz(30.0 * std::pow(600.0, v));
    if (k == "cb_cut") return hz(40.0 * std::pow(40.0, v));
    if (k == "level" || k == "brk_lvl" || k == "drm_lvl" || k == "kick_lvl" || k == "snr_lvl" || k == "hat_lvl" || k == "oh_lvl" || k == "volume") return db(v);
    if (k == "pitch" || k == "st_pitch") return juce::String(v, 2) + " st";
    if (k == "remix" || k == "lock") return juce::String(v, 1) + " %";
    if (k == "pan") return std::abs(v) < 0.005 ? juce::String("centre") : juce::String(juce::roundToInt(std::abs(v) * 100)) + (v < 0 ? " % L" : " % R");
    if (k == "nudge") return juce::String((int) std::lround(v)) + " steps";
    if (k == "duck_rel" || k == "ftr_dec") return juce::String(juce::roundToInt(v * 1000)) + " ms";
    if (k == "ftr_amt" || k == "phr_amt") return juce::String(v, 2) + " oct";
    if (k == "bpm") return juce::String(v, 1) + " bpm";
    if (k == "cb_note") return juce::MidiMessage::getMidiNoteName((int) std::lround(v), true, true, 3);
    if (k == "cb_fb") return juce::String(juce::roundToInt(v * 100)) + " %";
    if (k == "decay") return pc(v) + " of a 16th";
    return pc(v);
}
struct Knob : juce::Component, private juce::Slider::Listener {
    juce::Slider s { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox }; juce::String caption, id; bool showValue = false;
    std::unique_ptr<APVTS::SliderAttachment> att;
    Knob(APVTS& st, const juce::String& pid, const juce::String& cap) : caption(cap), id(pid) {
        addAndMakeVisible(s); s.setPopupDisplayEnabled(false, false, nullptr); s.setDoubleClickReturnValue(true, (double) st.getParameter(pid)->convertFrom0to1(st.getParameter(pid)->getDefaultValue()));
        att = std::make_unique<APVTS::SliderAttachment>(st, pid, s); s.addListener(this); s.addMouseListener(this, false);
        s.setTooltip(st.getParameter(pid)->getName(64) + ". Drag; double-click = default; right-click = type a value");
    }
    ~Knob() override { s.removeListener(this); s.removeMouseListener(this); }
    void sliderValueChanged(juce::Slider*) override { if (showValue) repaint(); }
    void mouseEnter(const juce::MouseEvent&) override { showValue = true; repaint(); }
    void mouseExit(const juce::MouseEvent&) override { showValue = false; repaint(); }
    void resized() override { s.setBounds(getLocalBounds().withTrimmedBottom(13)); }
    void paint(juce::Graphics& g) override {
        g.setColour(showValue ? INK : DIM); g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.drawFittedText(showValue ? fmtValue(id, s.getValue()) : caption, getLocalBounds().removeFromBottom(13), juce::Justification::centred, 1);
    }
};
struct Toggle : juce::TextButton {
    std::unique_ptr<APVTS::ButtonAttachment> att;
    Toggle(APVTS& st, const juce::String& id, const juce::String& txt, juce::Colour tint) : juce::TextButton(txt) {
        setClickingTogglesState(true); getProperties().set("tint", (juce::int64) tint.getARGB()); att = std::make_unique<APVTS::ButtonAttachment>(st, id, *this);
    }
};
struct HoldButton : juce::TextButton {
    std::function<void(bool)> onHold; bool latched = false; using juce::TextButton::TextButton;
    void mouseDown(const juce::MouseEvent& e) override { juce::TextButton::mouseDown(e); if (onHold) onHold(true); }
    void mouseUp(const juce::MouseEvent& e) override { juce::TextButton::mouseUp(e); if (onHold && !latched) onHold(false); }
};
// drag a render out: the render runs on a worker thread (the UI never freezes); the drag starts the moment it's ready
struct DragOut : juce::Component, juce::SettableTooltipClient, private juce::Timer {
    juce::String text; std::function<juce::StringArray()> make; juce::ThreadPool& pool; std::atomic<int> state { 0 }; juce::StringArray files; bool wantDrag = false;   // state 0 idle, 1 rendering, 2 ready
    DragOut(const juce::String& t, std::function<juce::StringArray()> f, juce::ThreadPool& tp) : text(t), make(std::move(f)), pool(tp) { setMouseCursor(juce::MouseCursor::DraggingHandCursor); }
    ~DragOut() override { stopTimer(); while (state.load() == 1) juce::Thread::sleep(5); }
    bool compact = false;   // a small grip: "WAV" / ".." / "WAV ok"
    void paint(juce::Graphics& g) override { auto r = getLocalBounds().toFloat().reduced(1); const int st = state.load();
        if (compact) { g.setColour(CARD); g.fillRoundedRectangle(r, 3); g.setColour(INK); g.drawRoundedRectangle(r, 3, 1.0f); g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
            g.drawText(st == 1 ? text + ".." : st == 2 ? text + " ok" : text, r, juce::Justification::centred); return; } g.setColour((st == 1 ? GOLD : LIME).withAlpha(0.13f)); g.fillRoundedRectangle(r, 6); g.setColour((st == 1 ? GOLD : LIME).withAlpha(0.85f)); g.drawRoundedRectangle(r, 6, 1.2f);
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold)); g.drawText(st == 1 ? "RENDERING..." : st == 2 ? text + "  (ready)" : text, r, juce::Justification::centred); }
    std::function<bool()> allowed;   // the demo gate (MickensProcessor::exportAllowed -> ghosty::exportAllowed), asked on the message thread before any render; unset = refused
    void start() { if (state.load() != 0) return; if (!allowed || !allowed()) return; state.store(1); repaint(); pool.addJob([this] { auto f = make(); f.removeEmptyStrings(); files = f; state.store(2); }); startTimerHz(20); }
    void mouseDown(const juce::MouseEvent&) override { wantDrag = false; start(); }
    void mouseDrag(const juce::MouseEvent& e) override { if (e.getDistanceFromDragStart() < 6 || wantDrag) return; wantDrag = true; tryDrag(); }
    void mouseUp(const juce::MouseEvent&) override { wantDrag = false; }
    void tryDrag() { if (!wantDrag || state.load() != 2) return; wantDrag = false; auto f = files; state.store(0); stopTimer(); repaint(); if (!f.isEmpty()) ghosty::exportgate::dragFiles(f, this); }   // gated in start() (the render never ran in demo)
    void timerCallback() override { repaint(); if (state.load() == 2) { if (wantDrag && juce::Desktop::getInstance().getMainMouseSource().isDragging()) tryDrag(); else if (!wantDrag) { stopTimer(); } } }
};
bool isAudio(const juce::String& f) { return juce::File(f).hasFileExtension("wav;aif;aiff;mp3;flac;ogg;m4a"); }

// a combo box bound to a choice parameter
struct Combo : juce::ComboBox {
    std::unique_ptr<APVTS::ComboBoxAttachment> att;
    Combo(APVTS& st, const juce::String& id, const juce::StringArray& items) { addItemList(items, 1); att = std::make_unique<APVTS::ComboBoxAttachment>(st, id, *this); }
};
// stereo peak meter + clip light (the clipper is a safety; the light says you are leaning on it)
struct Meter : juce::Component, juce::SettableTooltipClient, private juce::Timer {
    MickensProcessor& p; float l = 0, r = 0; int lastClips = 0; double clipUntil = 0;
    explicit Meter(MickensProcessor& pp) : p(pp) { startTimerHz(30); setTooltip("Output peak. The red light means the safety clipper (above 0.98) is working: turn VOLUME down. Click to reset."); }
    void timerCallback() override { const float nl = p.engine.meterL.load(), nr = p.engine.meterR.load(); l = juce::jmax(nl, l * 0.85f); r = juce::jmax(nr, r * 0.85f);
        const int c = p.engine.clipCount.load(); if (c != lastClips) { lastClips = c; clipUntil = juce::Time::getMillisecondCounterHiRes() + 1500; } repaint(); }
    void mouseDown(const juce::MouseEvent&) override { clipUntil = 0; repaint(); }
    void paint(juce::Graphics& g) override {
        auto a = getLocalBounds().toFloat(); auto led = a.removeFromTop(12).reduced(2, 1); a.removeFromTop(2);
        const bool clip = juce::Time::getMillisecondCounterHiRes() < clipUntil;
        g.setColour(clip ? BLOOD : CARD2); g.fillRoundedRectangle(led, 4); g.setColour(clip ? juce::Colours::white : DIM); g.setFont(juce::FontOptions(8.5f, juce::Font::bold)); g.drawText("CLIP", led, juce::Justification::centred);
        auto db = [](float v) { return juce::jlimit(0.0f, 1.0f, (20 * std::log10(juce::jmax(1e-5f, v)) + 48) / 48); };
        auto bar = [&](juce::Rectangle<float> rr, float v) { g.setColour(CARD2); g.fillRoundedRectangle(rr, 3); const float h = rr.getHeight() * db(v);
            juce::ColourGradient cg(LIME, 0, rr.getBottom(), BLOOD, 0, rr.getY(), false); cg.addColour(0.75, GOLD); g.setGradientFill(cg); g.fillRoundedRectangle(rr.withTop(rr.getBottom() - h), 3); };
        const float w = (a.getWidth() - 4) / 2; bar(a.removeFromLeft(w).reduced(1, 0), l); a.removeFromLeft(4); bar(a.reduced(1, 0), r);
    }
};

// the break waveform of a bay: slice grid, the slice playing now, drop your own break here
struct WaveView : juce::Component, juce::FileDragAndDropTarget {
    MickensProcessor& p; int bay; const mk::Break* shown = nullptr; std::vector<float> peaks; bool hover = false; juce::String msg; int lastSlice = -2;
    WaveView(MickensProcessor& pp, int b) : p(pp), bay(b) {}
    void refresh() {
        auto br = p.breakAt(p.bayBreak[(size_t) bay]); if (br.get() == shown && !peaks.empty()) return; shown = br.get(); peaks.clear(); if (!br) return;
        const int cols = 280, n = (int) br->data.size(); peaks.assign(cols, 0);
        for (int c = 0; c < cols; ++c) { float m = 0; for (int i = c * n / cols; i < (c + 1) * n / cols; ++i) m = juce::jmax(m, std::abs(br->data[(size_t) i])); peaks[(size_t) c] = m; }
    }
    bool dirty() { const int cur = p.engine.uiSlice[bay].load(); const bool ch = cur != lastSlice || p.breakAt(p.bayBreak[(size_t) bay]).get() != shown; lastSlice = cur; return ch; }
    void paint(juce::Graphics& g) override {
        refresh(); auto r = getLocalBounds().toFloat();
        if (GLASS) gl::screen(g, r, 5); else { g.setColour(WAVEBG); g.fillRoundedRectangle(r, 5); }   // GHOST: a glass screen
        if (!shown || peaks.empty()) { g.setColour(DIM); g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
            g.drawText(p.libReady() ? "pick a break, or drop one here" : "warming up the breaks...", r, juce::Justification::centred); if (hover) { g.setColour(LIME); g.drawRoundedRectangle(r.reduced(1), 5, 2); } return; }
        const int sl = shown->slices, cur = p.engine.uiSlice[bay].load();
        if (cur >= 0 && cur < sl && (p.engine.playing.load() || p.hostRunning.load())) { g.setColour(rainbow(cur / (float) sl).withAlpha(0.3f)); g.fillRect(r.getX() + r.getWidth() * cur / sl, r.getY(), r.getWidth() / sl, r.getHeight()); }
        g.setColour(HI.withAlpha(LIGHT ? 0.10f : 0.06f)); for (int i = 1; i < sl; ++i) g.drawVerticalLine((int) (r.getX() + r.getWidth() * i / sl), r.getY(), r.getBottom());
        g.setColour(HI.withAlpha(LIGHT ? 0.22f : 0.14f)); for (int i = 4; i < sl; i += 4) g.drawVerticalLine((int) (r.getX() + r.getWidth() * i / sl), r.getY(), r.getBottom());
        const float cy = r.getCentreY(), hh = r.getHeight() * 0.46f;
        for (int c = 0; c < (int) peaks.size(); ++c) { const float x = r.getX() + r.getWidth() * c / peaks.size(); g.setColour(rainbow(c / (float) peaks.size() * 0.9f, 0.75f)); g.drawVerticalLine((int) x, cy - peaks[(size_t) c] * hh, cy + peaks[(size_t) c] * hh); }
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        if (shown->user && !shown->tempoFound) { g.setColour(GOLD); g.drawText("no tempo found - FIT TEMPO off", r.reduced(5, 2), juce::Justification::topLeft); }
        if (msg.isNotEmpty()) { g.setColour(BLOOD); g.drawText(msg, r.reduced(5, 2), juce::Justification::bottomLeft); }
        if (hover) { g.setColour(LIME); g.drawRoundedRectangle(r.reduced(1), 5, 2); }
    }
    bool isInterestedInFileDrag(const juce::StringArray& f) override { for (auto& s : f) if (isAudio(s)) return true; return false; }
    void fileDragEnter(const juce::StringArray&, int, int) override { hover = true; repaint(); }
    void fileDragExit(const juce::StringArray&) override { hover = false; repaint(); }
    std::function<void()> onLoaded;
    void filesDropped(const juce::StringArray& f, int, int) override {
        hover = false; msg = {};
        for (auto& s : f) if (isAudio(s)) { juce::String err; if (!p.addUserBreak(juce::File(s), bay, err)) msg = err; else if (onLoaded) onLoaded(); break; }
        repaint();
    }
};

// BREAK DNA lane: every 16th of the live break, coloured by what hits there (kick / snare / ghost / hat / cymbal),
// as tall as its accent, shifted by its micro-timing. A frozen groove shows as a thin rule of ticks along the top.
// LIGHT: ghost is its own opaque blue (a 55 % cyan would fade into the light cells); the legend shows the colours in every look
juce::Colour clsColour(uint8_t c) { return c & mk::kK ? BLOOD : c & mk::kS ? CYAN : c & mk::kG ? (LIGHT ? juce::Colour(0xff2358d6) : CYAN.withAlpha(0.55f)) : c & mk::kH ? LIME : GOLD; }
struct DnaLane : juce::Component, juce::SettableTooltipClient {
    MickensProcessor& p; int b;
    DnaLane(MickensProcessor& pp, int bay) : p(pp), b(bay) { setTooltip("BREAK DNA: what hits on every 16th of this break (kick, snare, ghost, hat, cymbal: colours as in the legend above), bar height = accent, shifted by its micro-timing. Ticks along the top = the frozen groove."); }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat(); if (GLASS) gl::screen(g, r, 4); else { g.setColour(WAVEBG); g.fillRoundedRectangle(r, 4); }
        auto d = p.bayDNA(b);
        g.setFont(juce::FontOptions(8.5f, juce::Font::bold));
        if (!d || d->steps <= 0) { g.setColour(DIM); g.drawText("BREAK DNA", r, juce::Justification::centred); return; }
        const int n = d->steps; const float cw = r.getWidth() / (float) n;
        const bool run = p.engine.playing.load() || p.hostRunning.load(); const int cur = p.engine.uiSlice[b].load();
        if (run && cur >= 0 && cur < n) { g.setColour(HI.withAlpha(0.18f)); g.fillRect(r.getX() + cur * cw, r.getY(), cw, r.getHeight()); }
        g.setColour(HI.withAlpha(LIGHT ? 0.18f : 0.10f)); for (int i = 4; i < n; i += 4) g.drawVerticalLine((int) (r.getX() + i * cw), r.getY(), r.getBottom());
        static const uint8_t bits[] = { mk::kK, mk::kS, mk::kG, mk::kH, mk::kC };
        const float top = p.frozen[(size_t) b] ? 4.0f : 1.0f;
        for (int i = 0; i < n; ++i) {
            const uint8_t c = d->cls[(size_t) i]; if (!c) continue;
            const float x = r.getX() + i * cw + d->offset[(size_t) i] * cw + 1, w = juce::jmax(2.0f, cw - 2);
            const float h = (0.3f + 0.7f * d->accent[(size_t) i]) * (r.getHeight() - top - 2);
            int k = 0; for (auto bt : bits) k += (c & bt) != 0;
            float y = r.getBottom() - 1; const float seg = h / (float) juce::jmax(1, k);
            for (auto bt : bits) if (c & bt) { g.setColour(clsColour(bt)); g.fillRect(x, y - seg, w, seg - 0.5f); y -= seg; }
        }
        if (auto& fz = p.frozen[(size_t) b]) { const float fw = r.getWidth() / (float) juce::jmax(1, fz->steps);
            for (int i = 0; i < fz->steps; ++i) if (const uint8_t c = fz->cls[(size_t) i]) { g.setColour(LIGHT ? clsColour(c).darker(0.3f) : clsColour(c).brighter(0.4f)); g.fillRect(r.getX() + i * fw + fz->offset[(size_t) i] * fw + 1, r.getY() + 1, juce::jmax(2.0f, fw - 2), 2.5f); } }
    }
};
// SEQUENCE strip: the 32 sixteenths this bay will play with the current settings (slice colour by DNA class, brightness =
// level, glyphs for ratchets / reverse / pitch). Click = lock that step exactly as it is now; right-click = unlock.
struct SeqStrip : juce::Component, juce::SettableTooltipClient {
    MickensProcessor& p; int b; mk::StepPlan plans[32]; const mk::BreakDNA* dnaRef = nullptr; int tick = 0;
    SeqStrip(MickensProcessor& pp, int bay) : p(pp), b(bay) { setTooltip("SEQUENCE: what this bay plays on each 16th right now (REMIX, LOCKS, INTERLOCK included). Click a step to LOCK it as it is; right-click to unlock. PRINT locks all 32, CLEAR frees them, MIDI drags the sequence out as a clip (note = C3 + slice)."); }
    void refreshPlans() { p.previewRow(b, plans, 32); }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat(); if (GLASS) gl::screen(g, r, 4); else { g.setColour(WAVEBG); g.fillRoundedRectangle(r, 4); }
        auto br = p.breakAt(p.bayBreak[(size_t) b]); auto d = p.bayDNA(b); if (!br) return;
        const float cw = r.getWidth() / 32.0f; const bool run = p.engine.playing.load() || p.hostRunning.load(); const int st = p.engine.uiStep.load();
        const long stepNow = (long) std::floor(run ? (p.hostRunning.load() ? 0.0 : p.engine.clockBeats()) * 4.0 : -1);
        g.setColour(HI.withAlpha(LIGHT ? 0.18f : 0.10f)); for (int i = 4; i < 32; i += 4) g.drawVerticalLine((int) (r.getX() + i * cw), r.getY(), r.getBottom());
        for (int i = 0; i < 32; ++i) {
            const auto& pl = plans[i]; auto cell = juce::Rectangle<float>(r.getX() + i * cw + 1, r.getY() + 1, cw - 2, r.getHeight() - 2);
            if (pl.slice >= 0) {
                const uint8_t c = pl.cls ? pl.cls : (d ? d->cls[(size_t) (pl.slice % juce::jmax(1, d->steps))] : 0);
                const float lv = juce::jlimit(0.25f, 1.0f, 0.35f + 0.5f * pl.gain);
                g.setColour((c ? clsColour(c) : DIM).withAlpha(lv)); g.fillRoundedRectangle(cell, 2);
                if (pl.reverse) { juce::Path t; t.addTriangle(cell.getX() + 1, cell.getCentreY(), cell.getRight() - 1, cell.getY() + 2, cell.getRight() - 1, cell.getBottom() - 2); g.setColour(juce::Colours::black.withAlpha(0.6f)); g.fillPath(t); }
                if (pl.ratchet > 1) { g.setColour(juce::Colours::white.withAlpha(0.85f)); for (int k = 1; k < pl.ratchet; ++k) g.drawVerticalLine((int) (cell.getX() + cell.getWidth() * k / pl.ratchet), cell.getY() + 2, cell.getBottom() - 2); }
                if (std::abs(pl.semis) > 0.5f) { g.setColour(juce::Colours::white); juce::Path a; const float cx = cell.getCentreX(); if (pl.semis > 0) a.addTriangle(cx - 2.5f, cell.getY() + 5, cx + 2.5f, cell.getY() + 5, cx, cell.getY() + 1); else a.addTriangle(cx - 2.5f, cell.getBottom() - 5, cx + 2.5f, cell.getBottom() - 5, cx, cell.getBottom() - 1); g.fillPath(a); }
            }
            if (p.stepLocked(b, i)) { g.setColour(HI); g.drawRoundedRectangle(cell, 2, 1.4f); }
        }
        if (run && st >= 0) { const int cur = (int) (((stepNow % 32) + 32) % 32); const int shown = p.hostRunning.load() ? st : cur; g.setColour(HI.withAlpha(0.25f)); g.fillRect(r.getX() + shown * cw, r.getY(), cw, r.getHeight()); }
    }
    void mouseDown(const juce::MouseEvent& e) override {
        const int step = juce::jlimit(0, 31, (int) (e.x * 32 / juce::jmax(1, getWidth())));
        if (e.mods.isPopupMenu()) p.unlockStep(b, step); else if (p.stepLocked(b, step)) p.unlockStep(b, step); else p.lockStep(b, step);
        refreshPlans(); repaint();
    }
};

// a small mouse listener: every DNA move gets an undo point before it happens
struct DnaUndoHook : juce::MouseListener { std::function<void()> fn; void mouseDown(const juce::MouseEvent&) override { if (fn) fn(); } };

struct BayPanel : juce::Component {
    MickensProcessor& p; int b; juce::ComboBox brk, grid; WaveView wave; DnaLane dna; SeqStrip seq; juce::TextButton prev { "<" }, next { ">" }, find { "FIND" }, printB { "PRINT" }, clearB { "CLEAR" };
    DragOut dragMidi;
    std::unique_ptr<Knob> nudge, pitch, level, decay, pan, amt, dk, ds, dh, remix, lock; std::unique_ptr<Toggle> rs, rc, mute, fit, freeze, lk[5];
    std::unique_ptr<Combo> mode, role; juce::TextButton dice { "DICE" }, undo { "UNDO" };
    std::unique_ptr<APVTS::ComboBoxAttachment> gridAtt; int builtFor = -1; DnaUndoHook undoHook; int ticks = 0; juce::String bpmText;
    HoldButton hold; juce::TextButton keys { "KEYS" };
    std::function<void(int)> onFind;   // opens the break browser for this bay
    BayPanel(MickensProcessor& pp, int bay, juce::ThreadPool& pool) : p(pp), b(bay), wave(pp, bay), dna(pp, bay), seq(pp, bay),
        dragMidi("MIDI", [this] { return juce::StringArray { p.exportMidi(b, 4).getFullPathName() }; }, pool) {
        dragMidi.allowed = [this] { return p.exportAllowed(&dragMidi, "mick.bay.midi"); }; dragMidi.setComponentID("bayMidi" + juce::String(bay));
        setComponentID("bayPanel" + juce::String(bay));
        auto& st = p.apvts; const juce::String id = "b" + juce::String(b + 1) + "_";
        for (juce::Component* c : std::initializer_list<juce::Component*> { &brk, &wave, &dna, &seq, &prev, &next, &grid, &find, &printB, &clearB, &dragMidi }) addAndMakeVisible(c);
        grid.addItemList({ "1/16", "1/8", "1/4", "1/2", "1 bar" }, 1); gridAtt = std::make_unique<APVTS::ComboBoxAttachment>(st, id + "grid", grid);
        grid.setTooltip("NUDGE moves the start by this much");
        auto k = [&](std::unique_ptr<Knob>& kk, const char* pid, const char* cap) { kk = std::make_unique<Knob>(st, id + pid, cap); addAndMakeVisible(*kk); };
        k(nudge, "nudge", "NUDGE"); k(pitch, "pitch", "PITCH"); k(level, "level", "LEVEL"); k(decay, "decay", "CHOP LEN"); k(pan, "pan", "PAN");
        k(amt, "rnd_amt", "CHOP AMT"); k(dk, "duck_k", "DUCK KICK"); k(ds, "duck_s", "DUCK SNARE"); k(dh, "duck_h", "DUCK HATS");
        k(remix, "remix", "REMIX"); k(lock, "lock", "LOCK");
        rs = std::make_unique<Toggle>(st, id + "rnd_start", "RANDOM START", PINK); rc = std::make_unique<Toggle>(st, id + "rnd_chops", "RANDOM CHOPS", CYAN);
        mute = std::make_unique<Toggle>(st, id + "mute", "MUTE", BLOOD); fit = std::make_unique<Toggle>(st, id + "fit", "FIT TEMPO", LIME);
        rs->setTooltip("Every bar the break starts from a new random spot (on the grid)"); rc->setTooltip("Swaps 16ths for random slices; CHOP AMT says how often");
        fit->setTooltip("On: the break re-pitches to the song tempo (the classic jungle sound). Off: it keeps its own speed."); mute->setTooltip("Mute this bay (2 ms fade, no click)");
        for (auto* t : { rs.get(), rc.get(), mute.get(), fit.get() }) addAndMakeVisible(*t);
        // ---- BREAK DNA + GROOVE LOCK + REMIX
        mode = std::make_unique<Combo>(st, id + "dna_mode", juce::StringArray { "PRESERVE", "REWIRE", "MUTATE" }); addAndMakeVisible(*mode);
        mode->setTooltip("PRESERVE keeps the break's order and only decorates it. REWIRE swaps hits for other hits of the same kind (the skeleton stays). MUTATE rewrites the skeleton itself.");
        role = std::make_unique<Combo>(st, id + "role", juce::StringArray { "FREE", "MAIN", "GHOST", "TEXTURE", "FILLS" }); addAndMakeVisible(*role);
        role->setTooltip("INTERLOCK role: MAIN leads; GHOST answers with ghost notes, TEXTURE lays long quiet hats, FILLS only plays in fills. FREE ignores interlock.");
        remix->s.setTooltip("REMIX 0 = the break as it is. Turning up stacks layers: accents, same-kind swaps (20+), ratchets (40+), reverses + pitch + swing (60+), phrase edits (80+). Turn LOCK down for bigger changes.");
        lock->s.setTooltip("GROOVE LOCK: how strictly the ticked parts of the groove survive the remix (100 = kept exactly, 70 = the default, 0 = anything goes)");
        static const char* lkId[] = { "lk_ks", "lk_pos", "lk_swing", "lk_acc", "lk_ghost" }; static const char* lkTxt[] = { "K/S", "POS", "SWING", "ACCENT", "GHOSTS" };
        static const char* lkTip[] = { "Lock where kicks and snares land", "Lock hit positions (no new hits, no silences)", "Lock the swing (swapped hits keep the groove's micro-timing)", "Lock the accents (swapped hits keep the groove's dynamics)", "Lock the ghost notes' dynamics and placement" };
        for (int i = 0; i < 5; ++i) { lk[i] = std::make_unique<Toggle>(st, id + lkId[i], lkTxt[i], rainbow(0.55f + i * 0.08f)); lk[i]->setTooltip(lkTip[i]); addAndMakeVisible(*lk[i]); }
        freeze = std::make_unique<Toggle>(st, id + "freeze", "FREEZE DNA", GOLD); addAndMakeVisible(*freeze);
        freeze->setTooltip("Keep this break's groove (its DNA) while you load other breaks into the bay: the new break plays the frozen groove with its own sounds");
        for (auto* bt : { &dice, &undo }) addAndMakeVisible(*bt);
        dice.getProperties().set("tint", (juce::int64) GOLD.getARGB()); dice.setTooltip("A new remix seed (the remix pattern changes, the settings stay)"); dice.onClick = [this] { p.dnaDice(b); };
        undo.setTooltip("Take back the last DNA move (mode, remix, lock, dice, freeze)"); undo.onClick = [this] { p.dnaUndo(b); };
        undoHook.fn = [this] { p.dnaPush(b); };
        for (juce::Component* c : { (juce::Component*) mode.get(), (juce::Component*) role.get(), (juce::Component*) freeze.get(), (juce::Component*) &remix->s, (juce::Component*) &lock->s }) c->addMouseListener(&undoHook, true);
        for (auto& t : lk) t->addMouseListener(&undoHook, true);
        printB.setTooltip("PRINT: lock all 32 steps exactly as they play now (then edit single steps by clicking them)"); printB.onClick = [this] { p.printLocks(b); seq.refreshPlans(); seq.repaint(); };
        clearB.setTooltip("Clear every step lock of this bay"); clearB.onClick = [this] { p.clearLocks(b); seq.refreshPlans(); seq.repaint(); };
        dragMidi.setTooltip("Drag 4 bars of this bay's sequence out as a MIDI clip: note = C3 + slice (the KEYS mapping), ratchets as repeated notes, velocity = level");
        static const char* notes[] = { "C2", "C#2", "D2", "D#2" };
        hold.setButtonText(juce::String("HOLD TO PLAY  (") + notes[b] + ")"); hold.getProperties().set("tint", (juce::int64) rainbow(b / 4.0f).getARGB()); addAndMakeVisible(hold);
        hold.setTooltip("Hold: this bay plays its break from its start point, in time, until you let go. MIDI note " + juce::String(notes[b]) + " does the same.");
        hold.onHold = [this](bool on) { p.engine.pushPad(on ? 1 : 2, b, 0.9f); hold.setToggleState(on, juce::dontSendNotification); };
        keys.getProperties().set("tint", (juce::int64) GOLD.getARGB()); addAndMakeVisible(keys);
        keys.setTooltip("Your MIDI keys from C3 up play this bay's slices, one slice per key");
        keys.onClick = [this] { p.setParam("keys_bay", (float) b); };
        find.getProperties().set("tint", (juce::int64) CYAN.getARGB()); find.setTooltip("BREAK SEARCH: find a break by feel, or one that matches / complements this bay's break");
        find.onClick = [this] { if (onFind) onFind(b); };
        brk.onChange = [this] { const int sel = brk.getSelectedId(); if (sel > 0 && sel - 1 != p.bayBreak[(size_t) b]) { p.pickBreak(b, sel - 1); wave.repaint(); } };
        prev.onClick = [this] { step(-1); }; next.onClick = [this] { step(1); };
        wave.onLoaded = [this] { rebuild(); };
        rebuild();
    }
    ~BayPanel() override { for (juce::Component* c : { (juce::Component*) mode.get(), (juce::Component*) role.get(), (juce::Component*) freeze.get(), (juce::Component*) &remix->s, (juce::Component*) &lock->s }) c->removeMouseListener(&undoHook); for (auto& t : lk) t->removeMouseListener(&undoHook); }
    void step(int d) { const int n = p.numBreaks(); if (n == 0) return; p.pickBreak(b, (juce::jmax(0, p.bayBreak[(size_t) b]) + d + n) % n); brk.setSelectedId(p.bayBreak[(size_t) b] + 1, juce::dontSendNotification); wave.repaint(); }
    void rebuild() {
        brk.clear(juce::dontSendNotification); juce::String fam;
        for (int i = 0; i < p.numBreaks(); ++i) { auto br = p.breakAt(i); if (!br) continue; if (br->family != fam) { fam = br->family; brk.addSectionHeading(fam + (br->user || !p.lib || i < (int) p.folderBreaks.size() || i >= (int) p.folderBreaks.size() + p.numBuiltIn() ? "" : "  (" + juce::String(juce::roundToInt(br->bpm)) + ")")); } brk.addItem(br->name, i + 1); }
        if (p.folderBreaks.empty() && p.libReady()) brk.addSectionHeading("Real breaks: put yours in the BREAKS FOLDER (button up top)");
        brk.setSelectedId(p.bayBreak[(size_t) b] + 1, juce::dontSendNotification); builtFor = p.listVersion.load();
    }
    void sync() {
        keys.setToggleState((int) p.apvts.getRawParameterValue("keys_bay")->load() == b, juce::dontSendNotification);
        if (builtFor != p.listVersion.load()) rebuild();
        if (brk.getSelectedId() != p.bayBreak[(size_t) b] + 1) brk.setSelectedId(p.bayBreak[(size_t) b] + 1, juce::dontSendNotification);
        undo.setEnabled(!p.dnaHistory[(size_t) b].empty());
        auto br = p.breakAt(p.bayBreak[(size_t) b]); const juce::String bt = br ? juce::String(juce::roundToInt(br->bpm)) + " BPM" : juce::String(); if (bt != bpmText) { bpmText = bt; repaint(); }
        // repaint the live lanes only when the playhead moved (or every half second), the sequence preview 5x a second
        if (wave.dirty()) { wave.repaint(); dna.repaint(); seq.repaint(); }
        if (++ticks % 6 == 0) { seq.refreshPlans(); seq.repaint(); } if (ticks % 15 == 0) { wave.repaint(); dna.repaint(); }
    }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat().reduced(1);
        if (GLASS) gl::panels().draw(g, r, 12.0f, 0.40f, 0.0f); else { g.setColour(CARD); g.fillRoundedRectangle(r, 10); }   // GHOST: each bay is a glass panel
        const int rl = (int) p.apvts.getRawParameterValue("b" + juce::String(b + 1) + "_role")->load();
        g.setColour(rainbow(b / 4.0f).withAlpha(rl == mk::kMain ? 1.0f : 0.7f)); g.drawRoundedRectangle(r, 10, rl == mk::kMain ? 2.5f : 1.5f);
        g.setColour(rainbow(b / 4.0f)); g.setFont(juce::FontOptions(15.0f, juce::Font::bold)); g.drawText("BAY " + juce::String(b + 1), 12, 8, 60, 22, juce::Justification::centredLeft);
        g.setColour(DIM); g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
        g.drawText(bpmText, bpmX, 8, 56, 22, juce::Justification::centredRight);
        g.drawText("SEQUENCE  click = lock a step", 10, seqLabelY, 180, 12, juce::Justification::centredLeft);
        g.drawText("BREAK DNA  |  GROOVE LOCK", 10, dnaLabelY, 150, 12, juce::Justification::centredLeft);
        // the DNA colour legend
        { static const char* nm[] = { "KICK", "SNARE", "GHOST", "HAT", "CYM" }; static const uint8_t bits[] = { mk::kK, mk::kS, mk::kG, mk::kH, mk::kC }; int x = getWidth() - 10; g.setFont(juce::FontOptions(8.0f, juce::Font::bold));
          for (int i = 4; i >= 0; --i) { const int w = 6 + (int) juce::Font(juce::FontOptions(8.0f, juce::Font::bold)).getStringWidth(nm[i]); x -= w + 7; g.setColour(clsColour(bits[i])); g.fillRoundedRectangle((float) x, dnaLabelY + 3.0f, 6, 6, 1.5f); g.setColour(DIM); g.drawText(nm[i], x + 8, dnaLabelY, w, 12, juce::Justification::centredLeft); } }
        g.drawText("SIDECHAIN (ONLY THIS BAY DUCKS)", 10, duckLabelY, getWidth() - 20, 12, juce::Justification::centredLeft);
    }
    int dnaLabelY = 0, duckLabelY = 0, seqLabelY = 0, bpmX = 0;
    void resized() override {
        auto r = getLocalBounds().reduced(8);
        auto top = r.removeFromTop(26); top.removeFromLeft(62); next.setBounds(top.removeFromRight(24)); prev.setBounds(top.removeFromRight(24)); top.removeFromRight(4); find.setBounds(top.removeFromRight(46)); top.removeFromRight(4); bpmX = top.getRight() - 56; top.removeFromRight(58); brk.setBounds(top);
        r.removeFromTop(4); wave.setBounds(r.removeFromTop(42)); r.removeFromTop(3); dna.setBounds(r.removeFromTop(14)); r.removeFromTop(3); seq.setBounds(r.removeFromTop(18)); r.removeFromTop(2);
        { auto sr = r.removeFromTop(18); seqLabelY = sr.getY() + 3; dragMidi.setBounds(sr.removeFromRight(44)); sr.removeFromRight(3); clearB.setBounds(sr.removeFromRight(48)); sr.removeFromRight(3); printB.setBounds(sr.removeFromRight(48)); }
        r.removeFromTop(4);
        auto row1 = r.removeFromTop(52); const int kw = row1.getWidth() / 5;
        for (auto* k : { nudge.get(), pitch.get(), level.get(), decay.get(), pan.get() }) k->setBounds(row1.removeFromLeft(kw));
        r.removeFromTop(4);
        auto row2 = r.removeFromTop(26); grid.setBounds(row2.removeFromLeft(64)); row2.removeFromLeft(4); const int bw = (row2.getWidth() - 4) / 2; rs->setBounds(row2.removeFromLeft(bw)); row2.removeFromLeft(4); rc->setBounds(row2);
        r.removeFromTop(3);
        auto row3 = r.removeFromTop(26); mute->setBounds(row3.removeFromLeft(64)); row3.removeFromLeft(4); fit->setBounds(row3.removeFromLeft(90)); row3.removeFromLeft(4); keys.setBounds(row3);
        r.removeFromTop(4); dnaLabelY = r.getY(); r.removeFromTop(12);
        auto row4 = r.removeFromTop(24); mode->setBounds(row4.removeFromLeft(96)); row4.removeFromLeft(4); role->setBounds(row4.removeFromLeft(86)); row4.removeFromLeft(4); undo.setBounds(row4.removeFromRight(46)); row4.removeFromRight(4); dice.setBounds(row4.removeFromRight(44)); row4.removeFromRight(4); freeze->setBounds(row4);
        r.removeFromTop(3);
        auto row5 = r.removeFromTop(48); remix->setBounds(row5.removeFromLeft(60)); lock->setBounds(row5.removeFromLeft(60)); row5.removeFromLeft(4);
        { auto tg = row5; const int tw = (tg.getWidth() - 8) / 3; auto ra = tg.removeFromTop(22); tg.removeFromTop(4); auto rb = tg.removeFromTop(22);
          lk[0]->setBounds(ra.removeFromLeft(tw)); ra.removeFromLeft(4); lk[1]->setBounds(ra.removeFromLeft(tw)); ra.removeFromLeft(4); lk[2]->setBounds(ra);
          lk[3]->setBounds(rb.removeFromLeft(tw)); rb.removeFromLeft(4); lk[4]->setBounds(rb.removeFromLeft(tw)); }
        r.removeFromTop(4);
        hold.setBounds(r.removeFromTop(20));
        r.removeFromBottom(2); auto row6 = r.removeFromBottom(56); duckLabelY = row6.getY() - 13;
        const int dw = row6.getWidth() / 4; amt->setBounds(row6.removeFromRight(dw)); for (auto* k : { dk.get(), ds.get(), dh.get() }) k->setBounds(row6.removeFromLeft(dw));
    }
};

// 4 drum lanes x 16 steps. click = on/off, right-click or shift = ghost (soft), drop a sample on a lane name
struct DrumGrid : juce::Component, juce::FileDragAndDropTarget {
    MickensProcessor& p; int dragVal = -1; int hoverLane = -1;
    explicit DrumGrid(MickensProcessor& pp) : p(pp) {}
    static constexpr int nameW = 0;
    juce::Rectangle<float> cell(int d, int s) const { const float w = getWidth() / 16.0f, h = getHeight() / 4.0f; return { s * w, d * h, w, h }; }
    void paint(juce::Graphics& g) override {
        const int st = p.engine.uiStep.load(); const bool run = p.engine.playing.load() || p.hostRunning.load();
        for (int d = 0; d < mk::kDrums; ++d) for (int s = 0; s < mk::kSteps; ++s) {
            auto c = cell(d, s).reduced(2.5f); const int v = p.pattern[(size_t) d][(size_t) s];
            g.setColour(s / 4 % 2 ? CARD2 : CARD2.brighter(0.08f)); g.fillRoundedRectangle(c, 4);
            if (v > 0) { g.setColour(rainbow(s / 16.0f).withAlpha(0.35f + 0.65f * v / 127.0f)); g.fillRoundedRectangle(c, 4); }
            if (run && s == st) { g.setColour(HI.withAlpha(LIGHT ? 0.6f : 0.35f)); g.drawRoundedRectangle(c, 4, 1.6f); }
        }
        static const char* mi[] = { "kick_mute", "snr_mute", "hat_mute", "oh_mute" };
        const bool all = p.apvts.getRawParameterValue("drums_mute")->load() > 0.5f;
        for (int d = 0; d < mk::kDrums; ++d) if (all || p.apvts.getRawParameterValue(mi[d])->load() > 0.5f) { g.setColour(BG.withAlpha(0.6f)); g.fillRect(cell(d, 0).withWidth((float) getWidth())); }
        if (hoverLane >= 0) { g.setColour(LIME); g.drawRect(cell(hoverLane, 0).withWidth((float) getWidth()), 2.0f); }
    }
    int at(juce::Point<int> pt, int& d) const { d = juce::jlimit(0, 3, pt.y * 4 / juce::jmax(1, getHeight())); return juce::jlimit(0, 15, pt.x * 16 / juce::jmax(1, getWidth())); }
    void mouseDown(const juce::MouseEvent& e) override {
        int d; const int s = at(e.getPosition(), d); auto& v = p.pattern[(size_t) d][(size_t) s];
        const bool ghost = e.mods.isPopupMenu() || e.mods.isShiftDown();
        dragVal = v > 0 && !ghost ? 0 : (ghost ? 60 : 118); v = (uint8_t) dragVal; p.publish(); repaint();
    }
    void mouseDrag(const juce::MouseEvent& e) override { int d; const int s = at(e.getPosition(), d); if (p.pattern[(size_t) d][(size_t) s] != dragVal) { p.pattern[(size_t) d][(size_t) s] = (uint8_t) dragVal; p.publish(); repaint(); } }
    bool isInterestedInFileDrag(const juce::StringArray& f) override { for (auto& s : f) if (isAudio(s)) return true; return false; }
    void fileDragMove(const juce::StringArray&, int, int y) override { hoverLane = juce::jlimit(0, 3, y * 4 / juce::jmax(1, getHeight())); repaint(); }
    void fileDragExit(const juce::StringArray&) override { hoverLane = -1; repaint(); }
    std::function<void()> onSample;
    void filesDropped(const juce::StringArray& f, int, int y) override {
        hoverLane = -1; const int d = juce::jlimit(0, 3, y * 4 / juce::jmax(1, getHeight()));
        for (auto& s : f) if (isAudio(s)) { juce::String err; if (p.setUserDrum(d, juce::File(s), err) && onSample) onSample(); break; }
        repaint();
    }
};


// BAR RULER (16 bars): right-click = play from that bar; right-drag (>= 6 px, never a plain click) = pick a LOOP SECTION, shown as a brace
// with MIDI / WAV grips: drag a grip out of the plugin = only those bars (rendered from their own bar, at the current tempo). Double-click clears.
struct BarRuler : juce::Component, juce::SettableTooltipClient {
    static constexpr int kBars = 16;
    MickensProcessor& p; DragOut midiGrip, wavGrip; juce::TextButton loopB { "LOOP" }; int downBar = -1; bool dragging = false; std::function<void(int)> onPlayFrom; std::function<void()> onLoopChanged;
    static constexpr float kLeft = 84.0f;   // the LOOP button sits left of the bars
    BarRuler(MickensProcessor& pp, juce::ThreadPool& pool) : p(pp),
        midiGrip("MIDI", [this] { const int a = p.secA, b = p.secB; return juce::StringArray { p.exportMidiRange(a, b).getFullPathName() }; }, pool),
        wavGrip("WAV", [this] { const int a = p.secA, b = p.secB; return juce::StringArray { p.bounceRange(a, b).getFullPathName() }; }, pool) {
        setComponentID("barRuler");
        for (auto* gp : { &midiGrip, &wavGrip }) { gp->compact = true; addChildComponent(*gp); }
        midiGrip.allowed = [this] { return p.exportAllowed(&midiGrip, "mick.section.midi"); }; midiGrip.setComponentID("sectionMidi");
        wavGrip.allowed = [this] { return p.exportAllowed(&wavGrip, "mick.section.wav"); }; wavGrip.setComponentID("sectionWav");
        addAndMakeVisible(loopB); loopB.setComponentID("loopSection"); loopB.getProperties().set("tint", (juce::int64) juce::Colour(0xff22e6ff).getARGB());
        loopB.setTooltip("LOOP SECTION (key L): play only the braced bars, over and over (own PLAY, the preview and the DAW's playback). Click again (or L) to play everything.");
        loopB.onClick = [this] { toggleLoop(); };
        midiGrip.setTooltip("Drag out: just these bars as MIDI (a track per bay + the drums)"); wavGrip.setTooltip("Drag out: just these bars as WAV (rendered from their own bar, 2 s tail)");
        setTooltip("BARS: right-click = play from that bar (own clock / preview), right-drag = pick bars to export (the brace's MIDI / WAV grips drag them out), double-click = clear");
    }
    float barX(int b) const { return kLeft + (getWidth() - kLeft - 4.0f) * b / kBars; }
    int barAt(float x) const { return juce::jlimit(0, kBars - 1, (int) ((x - kLeft) / juce::jmax(1.0f, getWidth() - kLeft - 4.0f) * kBars)); }
    bool hasSection() const { return p.secA.load() >= 0 && p.secB.load() > p.secA.load(); }
    void toggleLoop() { if (!hasSection()) { p.secLoop = false; } else p.secLoop = !p.secLoop.load(); refreshLoop(); if (onLoopChanged) onLoopChanged(); }
    void refreshLoop() { loopB.setToggleState(p.sectionLooping(), juce::dontSendNotification); loopB.setEnabled(hasSection()); loopB.setButtonText(p.sectionLooping() ? "LOOP ON" : "LOOP"); repaint(); }
    juce::String loopLabel() const { return "LOOP ON bars " + juce::String(p.secA.load() + 1) + "-" + juce::String(p.secB.load()) + " - click LOOP (or L) to play everything"; }
    void layoutGrips() {
        const bool on = hasSection(); midiGrip.setVisible(on); wavGrip.setVisible(on); if (!on) return;
        const float x1 = barX(juce::jmin(kBars, p.secB.load())); const int gw = 34, gh = getHeight() - 6;
        int right = (int) x1 - 2; if (right - 2 * gw - 4 < (int) barX(p.secA.load()) + 40) right = juce::jmin(getWidth() - 2, (int) x1 + 2 + 2 * gw + 4);
        wavGrip.setBounds(right - gw, 3, gw, gh); midiGrip.setBounds(right - 2 * gw - 3, 3, gw, gh);
    }
    void resized() override { loopB.setBounds(4, 3, (int) kLeft - 10, getHeight() - 6); layoutGrips(); }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat(); if (GLASS) gl::panels().draw(g, r, 10.0f, 0.40f, 0.0f); else { g.setColour(CARD); g.fillRoundedRectangle(r, 8); } g.setColour(CYAN.withAlpha(0.55f)); g.drawRoundedRectangle(r.reduced(0.5f), 8, 1.2f);
        const float top = r.getY() + 2, bot = r.getBottom() - 2;
        for (int b = 0; b <= kBars; ++b) { const float x = barX(b); g.setColour(HI.withAlpha(b % 4 == 0 ? 0.35f : 0.15f)); g.drawVerticalLine((int) x, top + (b % 4 == 0 ? 0.0f : 8.0f), bot); }
        // the hint (no brace) or the LOOP label sits in the bars at the right that the brace + grips leave free; bar numbers stop before it
        const juce::String hint = !hasSection() ? juce::String("BARS   right-click: play from that bar   |   right-drag: pick bars to export or loop   |   double-click: clear")
                                : p.sectionLooping() ? loopLabel() : juce::String("drag MIDI / WAV out = just these bars   |   LOOP (or L) = play only them");
        const juce::Font hf(juce::FontOptions(10.0f, juce::Font::bold)); const float hw = hf.getStringWidthFloat(hint) + 16;
        float freeFrom = barX(0); if (hasSection()) freeFrom = juce::jmax(barX(juce::jmin(kBars, p.secB.load())), (float) wavGrip.getRight()) + 8;
        const float hintX = juce::jmax(freeFrom, r.getRight() - hw - 6); const bool hintFits = r.getRight() - 6 - hintX >= hw - 1;
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        if (hasSection()) {
            const int sa = p.secA.load(), sb = p.secB.load(); const bool lp = p.sectionLooping();
            const float x0 = barX(sa), x1 = barX(juce::jmin(kBars, sb));
            g.setColour((lp ? CYAN : DIM).withAlpha(LIGHT ? 1.0f : 0.85f)); g.fillRoundedRectangle(x0 + 1, top + 1, x1 - x0 - 2, bot - top - 2, 4);
            g.setColour(ONTXT); g.drawText((lp ? "LOOP ON bars " : "bars ") + juce::String(sa + 1) + "-" + juce::String(sb), juce::Rectangle<float>(x0 + 6, top, x1 - x0 - 10, bot - top), juce::Justification::centredLeft, true);
        }
        g.setColour(DIM); for (int b = 0; b < kBars; ++b) if ((!hasSection() || b < p.secA.load() || b >= p.secB.load()) && (!hintFits || barX(b) + 14 < hintX) && !(midiGrip.isVisible() && barX(b) + 14 > midiGrip.getX() && barX(b) < wavGrip.getRight()))
            g.drawText(juce::String(b + 1), juce::Rectangle<float>(barX(b) + 4, top, barX(b + 1) - barX(b) - 4, bot - top), juce::Justification::centredLeft, false);
        if (hintFits) { g.setColour(p.sectionLooping() ? CYAN : DIM); g.drawText(hint, juce::Rectangle<float>(hintX, top, r.getRight() - 6 - hintX, bot - top), juce::Justification::centredRight, true); }
    }
    void mouseDown(const juce::MouseEvent& e) override { downBar = barAt(e.position.x); dragging = false; }   // (the LOOP button and grips are children: they get their own clicks)
    void mouseDrag(const juce::MouseEvent& e) override {
        if (!e.mods.isPopupMenu() || e.getDistanceFromDragStart() < 6) return;   // only a right-drag of 6 px or more makes a section
        dragging = true; const int b = barAt(e.position.x); p.secA = juce::jmin(downBar, b); p.secB = juce::jmax(downBar, b) + 1; layoutGrips(); refreshLoop();
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (e.mods.isPopupMenu() && !dragging && e.getDistanceFromDragStart() < 6 && onPlayFrom) onPlayFrom(downBar);   // right-click = play from here
        dragging = false;
    }
    void mouseDoubleClick(const juce::MouseEvent& e) override { if (!e.mods.isPopupMenu()) { p.secA = -1; p.secB = -1; p.secLoop = false; layoutGrips(); refreshLoop(); } }
};

// a linear BAR (the house slider convention): mouse-down sets the value at the click, drag scrubs, Shift = fine (1/10 speed),
// wheel = one step, double-click = default, right-click = type a value (TypeIn), hover = the value in real units
struct BarSlider : juce::Slider {
    double def = 0.5, step = 0.01; std::function<void()> onReset; double fineFrom = 0; float fineX = 0; bool fine = false;
    BarSlider() : juce::Slider(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox) { setPopupDisplayEnabled(true, true, nullptr); setVelocityBasedMode(false); }
    double valueAtX(float x) const { const double p0 = getPositionOfValue(getMinimum()), p1 = getPositionOfValue(getMaximum());
        return juce::jlimit(getMinimum(), getMaximum(), getMinimum() + (getMaximum() - getMinimum()) * (x - p0) / juce::jmax(1.0, p1 - p0)); }
    void mouseDown(const juce::MouseEvent& e) override {
        if (e.mods.isPopupMenu() || !isEnabled()) return;   // right-click belongs to TypeIn
        startedDragging(); fine = e.mods.isShiftDown(); fineFrom = getValue(); fineX = e.position.x;
        if (!fine) setValue(valueAtX(e.position.x), juce::sendNotificationSync);
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (e.mods.isPopupMenu() || !isEnabled()) return;
        if (e.mods.isShiftDown() != fine) { fine = e.mods.isShiftDown(); fineFrom = getValue(); fineX = e.position.x; }   // switching mid-drag keeps the value
        const double span = getPositionOfValue(getMaximum()) - getPositionOfValue(getMinimum());
        if (fine) setValue(juce::jlimit(getMinimum(), getMaximum(), fineFrom + (e.position.x - fineX) / juce::jmax(1.0, span) * (getMaximum() - getMinimum()) * 0.1), juce::sendNotificationSync);
        else setValue(valueAtX(e.position.x), juce::sendNotificationSync);
    }
    void mouseUp(const juce::MouseEvent& e) override { if (!e.mods.isPopupMenu()) stoppedDragging(); juce::Slider::mouseUp(e); }
    void mouseDoubleClick(const juce::MouseEvent&) override { setValue(def, juce::sendNotificationSync); if (onReset) onReset(); }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override {
        const float d = std::abs(w.deltaY) > std::abs(w.deltaX) ? w.deltaY : -w.deltaX; if (d == 0.0f || !isEnabled()) return;
        setValue(juce::jlimit(getMinimum(), getMaximum(), getValue() + (d > 0 ? step : -step) * (w.isReversed ? -1 : 1)), juce::sendNotificationSync);
    }
};

// BREAK SEARCH: find a break by feel (8 sliders, each can be left on ANY), by words, or BREAK MATCH against a bay's break
// (SIMILAR: closest feel + groove; COMPLEMENT: fills its gaps). Built-in families show as one row with their 5 variations as
// chips. Click a row = select (+ audition while the transport is stopped), double-click or LOAD = put it in the bay.
struct SearchPanel : juce::Component, juce::ListBoxModel {
    MickensProcessor& p; int target = 0, mode = 0;   // mode 0 feel search, 1 match similar, 2 match complement
    juce::TextButton modeBtn[3], bayBtn[mk::kBays], loadB { "LOAD INTO BAY" }, audB { "AUDITION" }, closeB { "CLOSE" }, clearB { "ALL ANY" };
    juce::TextEditor words; BarSlider feel[mk::kNumAxes]; juce::TextButton any[mk::kNumAxes]; juce::ListBox list;
    struct Row { std::vector<int> items; int pick = 0; float score = 0; }; std::vector<Row> rows; int sel = -1; int seenVersion = -1;
    std::function<void()> onClose;
    explicit SearchPanel(MickensProcessor& pp) : p(pp) {
        setComponentID("searchPanel");
        static const char* mn[] = { "SEARCH BY FEEL", "MATCH: SIMILAR", "MATCH: COMPLEMENT" };
        for (int i = 0; i < 3; ++i) { modeBtn[i].setButtonText(mn[i]); modeBtn[i].setClickingTogglesState(false); modeBtn[i].getProperties().set("tint", (juce::int64) (i == 0 ? CYAN : i == 1 ? LIME : PINK).getARGB()); addAndMakeVisible(modeBtn[i]); modeBtn[i].onClick = [this, i] { mode = i; refresh(); }; }
        modeBtn[1].setTooltip("Breaks closest to the target bay's break: feel, spectrum and kick/snare groove"); modeBtn[2].setTooltip("Breaks that sit well next to the target bay's break: different density and spectrum, few colliding hits, matching swing");
        for (int b = 0; b < mk::kBays; ++b) { bayBtn[b].setButtonText("BAY " + juce::String(b + 1)); bayBtn[b].getProperties().set("tint", (juce::int64) rainbow(b / 4.0f).getARGB()); addAndMakeVisible(bayBtn[b]); bayBtn[b].onClick = [this, b] { target = b; refresh(); }; }
        addAndMakeVisible(words); words.setTextToShowWhenEmpty("words: jungle, ghosty, sparse, swung, bright, a family name...", DIM); words.setFont(juce::FontOptions(12.0f)); words.onTextChange = [this] { refresh(); };
        for (int a = 0; a < mk::kNumAxes; ++a) {
            auto& s = feel[a]; s.setRange(0, 1, 0.001); s.setValue(0.5, juce::dontSendNotification);
            // real units: percent of the feel (tempo: bpm); typed values read the same way
            s.textFromValueFunction = [](double v) { return juce::String(juce::roundToInt(v * 100)) + " %"; };
            s.valueFromTextFunction = [](const juce::String& t) { return t.retainCharacters("0123456789.-").getDoubleValue() / 100.0; };
            s.setTooltip(juce::String(mk::axisName(a)) + ": click to set, drag to scrub, Shift = fine, wheel = one step, double-click = back to ANY, right-click = type a value");
            s.onValueChange = [this, a] { any[a].setToggleState(false, juce::dontSendNotification); refresh(); }; addAndMakeVisible(s);
            s.onReset = [this, a] { any[a].setToggleState(true, juce::dontSendNotification); refresh(); };   // the default is "ANY" at the middle
            s.setComponentID("feel" + juce::String(a)); any[a].setComponentID("any" + juce::String(a));
            any[a].setButtonText("ANY"); any[a].setClickingTogglesState(true); any[a].setToggleState(true, juce::dontSendNotification); any[a].getProperties().set("tint", (juce::int64) GOLD.getARGB()); addAndMakeVisible(any[a]); any[a].onClick = [this] { refresh(); };
        }
        feel[mk::kAxTempo].textFromValueFunction = [](double v) { return juce::String(juce::roundToInt(80 + v * 100)) + " bpm"; };
        feel[mk::kAxTempo].valueFromTextFunction = [](const juce::String& t) { return (t.retainCharacters("0123456789.").getDoubleValue() - 80.0) / 100.0; };
        addAndMakeVisible(list); list.setModel(this); list.setRowHeight(26); list.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        for (auto* bt : { &loadB, &audB, &closeB, &clearB }) addAndMakeVisible(*bt);
        loadB.getProperties().set("tint", (juce::int64) LIME.getARGB()); loadB.onClick = [this] { load(); };
        audB.getProperties().set("tint", (juce::int64) CYAN.getARGB()); audB.setTooltip("Play the selected break once, in time at the song tempo (the mix ducks while it plays)"); audB.onClick = [this] { auditionSel(); };
        closeB.getProperties().set("tint", (juce::int64) BLOOD.getARGB()); closeB.onClick = [this] { p.stopAudition(); if (onClose) onClose(); };
        clearB.onClick = [this] { for (auto& a : any) a.setToggleState(true, juce::dontSendNotification); refresh(); };
    }
    void open(int bay, int md) { target = juce::jlimit(0, mk::kBays - 1, bay); mode = md; refresh(); setVisible(true); toFront(true); }
    std::array<float, mk::kNumAxes> want() const { std::array<float, mk::kNumAxes> w; for (int a = 0; a < mk::kNumAxes; ++a) w[(size_t) a] = any[a].getToggleState() ? -1.0f : (float) feel[a].getValue(); return w; }
    int selItem() const { if (sel < 0 || sel >= (int) rows.size()) return -1; const auto& r = rows[(size_t) sel]; return r.items.empty() ? -1 : r.items[(size_t) juce::jlimit(0, (int) r.items.size() - 1, r.pick)]; }
    bool transportStopped() const { return !p.hostRunning.load() && !p.engine.playing.load(); }
    void refresh() {
        rows.clear(); auto& ix = p.searchIndex(); seenVersion = p.listVersion.load();
        auto wordsOk = [&](const mk::BreakEntry& e, const juce::StringArray& w) { const auto hay = (e.name + " " + e.family + " " + e.tags.joinIntoString(" ")).toLowerCase(); for (auto& x : w) if (!hay.contains(x)) return false; return true; };
        if (mode == 0) {   // one row per built-in family (its variations as chips), one per folder / dropped break
            std::map<juce::String, int> famRow;
            for (int i : ix.search(want(), words.getText())) { const auto& e = ix.items[(size_t) i]; const bool builtIn = e.tags.contains("built-in");
                if (builtIn) { auto it = famRow.find(e.family); if (it != famRow.end()) { rows[(size_t) it->second].items.push_back(i); continue; } famRow[e.family] = (int) rows.size(); }
                Row r; r.items.push_back(i); rows.push_back(r); } }
        else { auto ref = p.bayDNA(target); const int self = p.bayBreak[(size_t) target];
            if (ref) { auto w = juce::StringArray::fromTokens(words.getText().toLowerCase(), " ,", ""); w.removeEmptyStrings();
                for (auto& r : ix.match(*ref, mode - 1)) { const auto& e = ix.items[(size_t) r.first]; if (e.index == self || !wordsOk(e, w)) continue; Row row; row.items.push_back(r.first); row.score = r.second; rows.push_back(row); } } }
        for (int i = 0; i < 3; ++i) modeBtn[i].setToggleState(i == mode, juce::dontSendNotification);
        for (int b = 0; b < mk::kBays; ++b) bayBtn[b].setToggleState(b == target, juce::dontSendNotification);
        sel = -1; list.updateContent(); list.deselectAllRows(); repaint();
    }
    void auditionSel() { const int it = selItem(); if (it >= 0 && it < (int) p.searchIndex().items.size()) p.audition(p.searchIndex().items[(size_t) it].index); }
    void load() { const int it = selItem(); if (it < 0 || it >= (int) p.searchIndex().items.size()) return; p.stopAudition(); p.pickBreak(target, p.searchIndex().items[(size_t) it].index); if (mode != 0) refresh(); }
    int getNumRows() override { return (int) rows.size(); }
    juce::Rectangle<int> chipArea(int w) const { return juce::Rectangle<int>(w - (mode ? 300 : 180) - 330, 0, 330, 26); }
    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override {
        if (row < 0 || row >= (int) rows.size()) return; auto& ix = p.searchIndex(); const auto& R = rows[(size_t) row]; if (R.items.empty() || R.items[0] >= (int) ix.items.size()) return;
        const auto& e = ix.items[(size_t) R.items[(size_t) juce::jlimit(0, (int) R.items.size() - 1, R.pick)]];
        auto r = juce::Rectangle<int>(0, 0, w, h);
        if (selected) { g.setColour(LIGHT ? pal().card.interpolatedWith(CYAN, 0.22f) : CYAN.withAlpha(0.25f)); g.fillRoundedRectangle(r.toFloat().reduced(1), 5); } else if (row & 1) { g.setColour(LIGHT ? CARD2.withAlpha(0.6f) : juce::Colours::white.withAlpha(0.03f)); g.fillRect(r); }
        const bool grouped = R.items.size() > 1;
        g.setFont(juce::FontOptions(12.5f, juce::Font::bold)); g.setColour(INK); g.drawText(grouped ? e.family : e.name, r.reduced(8, 0).removeFromLeft(210), juce::Justification::centredLeft, true);
        g.setFont(juce::FontOptions(11.0f)); g.setColour(DIM); g.drawText(grouped ? juce::String((int) R.items.size()) + " variations" : e.family, r.reduced(8, 0).withTrimmedLeft(214).removeFromLeft(130), juce::Justification::centredLeft, true);
        g.drawText(juce::String(juce::roundToInt(e.bpm)) + " bpm", r.reduced(8, 0).withTrimmedLeft(348).removeFromLeft(60), juce::Justification::centredLeft);
        juce::StringArray tags = e.tags; tags.removeString("built-in");
        g.setColour(CYAN.withAlpha(0.85f)); g.drawText(tags.joinIntoString("  "), r.reduced(8, 0).withTrimmedLeft(412).withTrimmedRight(grouped ? 520 : (mode ? 300 : 190)), juce::Justification::centredLeft, true);
        if (grouped) { auto ca = chipArea(w); const int cw = ca.getWidth() / (int) R.items.size(); g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
            for (int k = 0; k < (int) R.items.size(); ++k) { auto c = ca.removeFromLeft(cw).reduced(2, 5).toFloat(); const bool on = k == R.pick && selected; g.setColour(on ? LIME.withAlpha(LIGHT ? 1.0f : 0.8f) : CARD2); g.fillRoundedRectangle(c, 4); g.setColour(on ? ONTXT : INK); g.drawText(ix.items[(size_t) R.items[(size_t) k]].name.fromLastOccurrenceOf(" - ", false, false), c, juce::Justification::centred); } }
        if (mode) { const float sc = juce::jlimit(0.0f, 1.0f, R.score); auto bar = r.removeFromRight(124).reduced(8, 9).toFloat(); g.setColour(LIGHT ? CARD2 : juce::Colours::white.withAlpha(0.08f)); g.fillRoundedRectangle(bar, 3);
            g.setColour(mode == 1 ? LIME : PINK); g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * sc), 3); }
        if (e.dna) { auto f = mk::fold32(*e.dna); auto lane = juce::Rectangle<float>((float) w - (mode ? 300.0f : 180.0f), (float) h - 5, 160, 3);
            for (int i = 0; i < 32; ++i) if (f[(size_t) i]) { g.setColour(clsColour(f[(size_t) i]).withAlpha(0.8f)); g.fillRect(lane.getX() + i * 5, lane.getY(), 4.0f, 3.0f); } }
    }
    void listBoxItemClicked(int row, const juce::MouseEvent& e) override {
        if (row < 0 || row >= (int) rows.size()) return; auto& R = rows[(size_t) row];
        if (R.items.size() > 1) { auto ca = chipArea(list.getWidth()); if (ca.contains(e.x, e.y)) { R.pick = juce::jlimit(0, (int) R.items.size() - 1, (e.x - ca.getX()) * (int) R.items.size() / juce::jmax(1, ca.getWidth())); } }
        sel = row; list.repaintRow(row); if (transportStopped()) auditionSel();
    }
    void selectedRowsChanged(int r) override { sel = r; }
    void listBoxItemDoubleClicked(int r, const juce::MouseEvent&) override { sel = r; load(); }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat(); g.setColour(BG); g.fillRoundedRectangle(r, 12); /* opaque: the bays never show through */
        if (GLASS) { g.setColour(pal().card); g.fillRoundedRectangle(r, 12); ghosty::glass::body(g, r, 12, juce::Colours::white, 0.30f); }   // GHOST: an opaque glass card
        g.setColour(CYAN.withAlpha(0.8f)); g.drawRoundedRectangle(r.reduced(0.5f), 12, 1.5f);
        g.setColour(CYAN); g.setFont(juce::FontOptions(15.0f, juce::Font::bold)); g.drawText("BREAK SEARCH  |  BREAK MATCH", 16, 8, 400, 22, juce::Justification::centredLeft);
        g.setColour(DIM); g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
        g.drawText(mode == 0 ? "sliders you move count, ANY ones don't; words filter by name, family and tags. Click = select (auditions while stopped), double-click = load." : "ranked against the target bay's break. Click = select (auditions while stopped), double-click = load.", 16, 30, 760, 14, juce::Justification::centredLeft);
        for (int a = 0; a < mk::kNumAxes; ++a) { auto b = feel[a].getBounds(); g.setColour(any[a].getToggleState() ? DIM : INK); g.drawText(mk::axisName(a), b.getX() - 112, b.getY(), 108, b.getHeight(), juce::Justification::centredRight); }
        g.setColour(DIM); g.drawText(juce::String(rows.size()) + (mode ? " breaks" : " rows"), list.getBounds().withHeight(14).translated(0, -16), juce::Justification::centredLeft);
        if (auto ref = mode ? p.bayDNA(target) : nullptr) { auto br = p.breakAt(p.bayBreak[(size_t) target]); g.setColour(rainbow(target / 4.0f)); g.drawText("target: " + (br ? br->name : juce::String("-")) + "  (" + mk::feelTags(*ref).joinIntoString(", ") + ")", list.getBounds().withHeight(14).translated(0, -16).withTrimmedLeft(90), juce::Justification::centredLeft, true); }
    }
    void resized() override {
        auto r = getLocalBounds().reduced(14); r.removeFromTop(36);
        auto left = r.removeFromLeft(430); r.removeFromLeft(14);
        auto mr = left.removeFromTop(26); const int mw = (mr.getWidth() - 8) / 3; for (int i = 0; i < 3; ++i) { modeBtn[i].setBounds(mr.removeFromLeft(mw)); mr.removeFromLeft(4); }
        left.removeFromTop(6); auto br = left.removeFromTop(24); const int bw = (br.getWidth() - 12) / 4; for (int b = 0; b < mk::kBays; ++b) { bayBtn[b].setBounds(br.removeFromLeft(bw)); br.removeFromLeft(4); }
        left.removeFromTop(8); words.setBounds(left.removeFromTop(26)); left.removeFromTop(8);
        auto cr = left.removeFromTop(20); clearB.setBounds(cr.removeFromRight(70)); left.removeFromTop(2);
        const int rh = juce::jmin(30, left.getHeight() / mk::kNumAxes);
        for (int a = 0; a < mk::kNumAxes; ++a) { auto row = left.removeFromTop(rh); row.removeFromLeft(112); any[a].setBounds(row.removeFromRight(46).reduced(0, 4)); row.removeFromRight(4); feel[a].setBounds(row); }
        auto bottom = r.removeFromBottom(28); closeB.setBounds(bottom.removeFromRight(90)); bottom.removeFromRight(8); loadB.setBounds(bottom.removeFromRight(150)); bottom.removeFromRight(8); audB.setBounds(bottom.removeFromRight(100));
        r.removeFromBottom(8); r.removeFromTop(16); list.setBounds(r);
    }
};

} // namespace

// =====================================================================================================
struct MickensEditor::Content : juce::Component {
    MickensProcessor& p; float phase = 0; int tickN = 0;
    juce::ThreadPool pool { 2 };
    std::unique_ptr<BayPanel> bays[mk::kBays];
    DrumGrid drums; juce::ComboBox drumSel[mk::kDrums]; std::unique_ptr<Knob> drumLvl[mk::kDrums];
    juce::TextButton patBtn[4]; std::unique_ptr<Toggle> laneMute[mk::kDrums], muteAll; HoldButton hit[mk::kDrums]; juce::TextButton play { "PLAY" }, dice { "DICE" }, saveB { "SAVE" }, loadB { "LOAD" }, folderB { "BREAKS FOLDER" }, rescanB { "RESCAN" }, themeB { "BLACK" }, searchB { "SEARCH BREAKS" };
    juce::ComboBox presetBox; juce::TextButton presetPrev { "<" }, presetNext { ">" }, presetSave { "SAVE PRESET" }; int presetBuilt = -1;
    std::function<void()> onTheme;
    juce::ComboBox bars; DragOut dragWav, dragStems; std::unique_ptr<Knob> tempo; std::unique_ptr<BarRuler> ruler;
    std::vector<std::unique_ptr<Knob>> fxKnobs; juce::TextButton stutLatchBtn { "LATCH" }; HoldButton stutHold { "STUTTER (F2)" }; juce::ComboBox stutDiv; std::unique_ptr<APVTS::ComboBoxAttachment> stutDivAtt;
    // Mickens 2
    std::unique_ptr<Combo> fillStyle, fillEvery, fillLen, ilMode, stMode, fltRoute, cbTune, ftrSrc, phrLen, phrShape, morphA, morphB, interp;
    std::unique_ptr<Knob> fillInt, ilDuck, stGate, stDec, stPitch, stMove, fltMorph, cbFb, cbNote, ftrAmt, ftrDec, ftrRes, phrAmt, morphAmt;
    std::unique_ptr<Toggle> fltAuto, morphOn; juce::TextButton storeArm { "STORE" }, fillNow { "FILL NOW (E2)" }, slotBtn[MickensProcessor::kSlots];
    Meter meter; SearchPanel search; ghosty::BypassButton bypassButton; std::unique_ptr<Toggle> playMode;
    int themeUi = -1; ghosty::SyncButton syncButton; juce::Label bpmBox, dawTempo; juce::TextButton tapB { "TAP" }; double taps[8] {}; int nTaps = 0;
    std::unique_ptr<juce::FileChooser> chooser; juce::TooltipWindow tips { this, 600 };
    juce::Rectangle<int> secCrunch, secSpace, secMix, secBrowse, secFilt, secMotion, secStut, secDrums, secFill, secIl, secDeck, header, uniArea;

    ghosty::UnlockButton unlock { p.license };
    std::unique_ptr<Toggle> muteAllB;
    explicit Content(MickensProcessor& pp) : p(pp), drums(pp),
        dragWav("DRAG WAV OUT", [this] { return juce::StringArray { p.bounce(barsNow()).getFullPathName() }; }, pool),
        dragStems("DRAG STEMS OUT", [this] { return p.bounceStems(barsNow()); }, pool), meter(pp), search(pp), bypassButton(*pp.bypassParam), syncButton(pp.sync) {
        dragWav.allowed = [this] { return p.exportAllowed(&dragWav, "mick.mix.wav"); }; dragWav.setComponentID("dragWav");
        dragStems.allowed = [this] { return p.exportAllowed(&dragStems, "mick.stems.wav"); }; dragStems.setComponentID("dragStems");
        addAndMakeVisible(bypassButton); bypassButton.setComponentID("bypassButton"); syncButton.setComponentID("syncButton");   // themed in syncTransportUi()
        auto& st = p.apvts;
        // SYNC TO DAW | OWN + the transport cluster (own BPM box + TAP, or the DAW's tempo read-only; PLAY)
        addAndMakeVisible(syncButton); syncButton.onModeChanged = [this] { syncTransportUi(); };
        syncButton.setTooltip("SYNC: DAW (default) = Mr. Mickens follows the DAW's play button, tempo and song position; you can edit while the whole song plays, "
                              "and SPACE stays with the DAW.\nSYNC: OWN = Mr. Mickens runs by itself: its own tempo (type it in or TAP), its own PLAY, and SPACE "
                              "plays / stops only Mr. Mickens while its window has focus. The DAW's other tracks don't start and the DAW's play button doesn't start it. "
                              "PLAY: ON NOTES still gates it, BYPASS still wins. Saved with the project, never in presets.");
        addChildComponent(bpmBox); bpmBox.setEditable(true, true, false); bpmBox.setJustificationType(juce::Justification::centred);
        bpmBox.setFont(juce::FontOptions(17.0f, juce::Font::bold)); bpmBox.setColour(juce::Label::outlineColourId, CYAN.withAlpha(0.7f));
        bpmBox.setTooltip("Mr. Mickens' own tempo (SYNC: OWN): click and type 20 - 999, Enter to set");
        bpmBox.onTextChange = [this] { const double v = bpmBox.getText().retainCharacters("0123456789.").getDoubleValue(); if (v > 0) p.sync.setOwnBpm(v); bpmBox.setText(juce::String(p.sync.getOwnBpm(), 1), juce::dontSendNotification); };
        addChildComponent(tapB); tapB.getProperties().set("tint", (juce::int64) CYAN.getARGB()); tapB.setTooltip("Tap 2+ times in time to set the own tempo");
        tapB.onClick = [this] { const double now = juce::Time::getMillisecondCounterHiRes(); if (nTaps > 0 && now - taps[nTaps - 1] > 2000.0) nTaps = 0;
            if (nTaps == 8) { for (int i = 1; i < 8; ++i) taps[i - 1] = taps[i]; nTaps = 7; } taps[nTaps++] = now;
            if (nTaps >= 2) { p.sync.setOwnBpm(60000.0 * (nTaps - 1) / (taps[nTaps - 1] - taps[0])); bpmBox.setText(juce::String(p.sync.getOwnBpm(), 1), juce::dontSendNotification); } };
        addChildComponent(dawTempo); dawTempo.setJustificationType(juce::Justification::centred); dawTempo.setFont(juce::FontOptions(12.5f, juce::Font::bold));
        dawTempo.setTooltip("SYNC: DAW - the tempo comes from your project. Switch SYNC to OWN to set Mr. Mickens' own tempo.");
        playMode = std::make_unique<Toggle>(st, "play_mode", "PLAY: ALWAYS", CYAN); addAndMakeVisible(*playMode);
        playMode->setTooltip("PLAY: ALWAYS = the beat runs with the transport, as before.\n"
                             "PLAY: ON NOTES = it runs only while a MIDI note is held (any key), so muting its channel in your DAW (no notes arrive) stops it and the next note "
                             "starts it again, on the song's 16th grid. Draw one long note per bar or pattern; C0 (or any note below C1) only opens the gate.\n"
                             "Rule: every held key opens the gate and still does its own job, so HOLD TO PLAY (C2-D#2) holds its bay exactly as before AND "
                             "keeps the beat open while held. With no note held everything is silent, including the PLAY button and the on-screen pads. "
                             "BYPASS always wins. Saved with the project, never in presets or snapshots.");
        for (int b = 0; b < mk::kBays; ++b) { bays[b] = std::make_unique<BayPanel>(p, b, pool); addAndMakeVisible(*bays[b]); bays[b]->onFind = [this](int bay) { search.open(bay, 1); }; }
        addAndMakeVisible(drums); drums.onSample = [this] { syncDrums(); };
        const char* lv[] = { "kick_lvl", "snr_lvl", "hat_lvl", "oh_lvl" };
        for (int d = 0; d < mk::kDrums; ++d) {
            addAndMakeVisible(drumSel[d]); drumLvl[d] = std::make_unique<Knob>(st, lv[d], ""); addAndMakeVisible(*drumLvl[d]);
            static const char* mi[] = { "kick_mute", "snr_mute", "hat_mute", "oh_mute" };
            laneMute[d] = std::make_unique<Toggle>(st, mi[d], "M", BLOOD); laneMute[d]->setTooltip("Mute this drum (it also stops ducking the breaks)"); addAndMakeVisible(*laneMute[d]);
            static const char* dn[] = { "C1", "D1", "F#1", "A#1" };
            hit[d].setButtonText("HIT"); hit[d].getProperties().set("tint", (juce::int64) CYAN.getARGB()); hit[d].setTooltip("Play this drum now (MIDI note " + juce::String(dn[d]) + ")"); addAndMakeVisible(hit[d]);
            hit[d].onHold = [this, d](bool on) { if (on) p.engine.pushPad(0, d, 0.95f); hit[d].setToggleState(on, juce::dontSendNotification); };
            drumSel[d].onChange = [this, d] { const auto t = drumSel[d].getText(); if (drumSel[d].getSelectedId() == 999 || t.isEmpty()) return; p.drumName[(size_t) d] = t; p.userDrum[(size_t) d] = nullptr; p.publish(); syncDrums(); };
        }
        syncDrums();
        muteAll = std::make_unique<Toggle>(st, "drums_mute", "MUTE ALL", BLOOD); muteAll->setTooltip("Mute every drum (patterns stay)"); addAndMakeVisible(*muteAll);
        const char* pn[] = { "TWO STEP", "HALFTIME", "ROLLER", "CLEAR" };
        for (int i = 0; i < 4; ++i) { patBtn[i].setButtonText(pn[i]); addAndMakeVisible(patBtn[i]); patBtn[i].onClick = [this, i] { p.defaultPattern(i == 3 ? 3 : i); p.publish(); drums.repaint(); }; }
        // header
        for (auto* b : { &play, &dice, &saveB, &loadB }) addAndMakeVisible(*b);
        addChildComponent(unlock); unlock.refresh();
        muteAllB = std::make_unique<Toggle>(st, "mute_all", "MUTE", BLOOD); addAndMakeVisible(*muteAllB);
        muteAllB->setTooltip("Silence Mr. Mickens while your song plays. It keeps following the beat, so un-muting drops back in on time.");
        play.getProperties().set("tint", (juce::int64) LIME.getARGB()); dice.getProperties().set("tint", (juce::int64) GOLD.getARGB());
        play.onClick = [this] { if (p.ownTransport()) p.sync.toggleOwn(0.0); else p.setPlaying(!p.engine.playing.load()); };   // OWN: own clock from bar 1; DAW: the classic preview while the DAW is stopped
        dice.onClick = [this] { p.diceStarts(); }; dice.setTooltip("New start point for every bay (on each bay's grid). Hit it again for a new combo.");
        saveB.onClick = [this] { save(); }; loadB.onClick = [this] { load(); }; saveB.setTooltip("Save the whole setup as a .mickens file"); loadB.setTooltip("Load a .mickens file");
        for (auto* b : { &folderB, &rescanB }) { addAndMakeVisible(*b); b->getProperties().set("tint", (juce::int64) CYAN.getARGB()); }
        folderB.setTooltip("Opens Documents/Ghosty/Mr Mickens/Breaks. Put your own break WAVs there (sub-folders become sections); they show up at the top of every bay's list.");
        folderB.onClick = [this] { p.breaksFolder().startAsProcess(); };
        addAndMakeVisible(themeB); themeB.getProperties().set("tint", (juce::int64) GOLD.getARGB()); themeB.setTooltip("Switch the look: BLACK -> PURPLE -> LIGHT -> GHOST (liquid glass); the button names the next one");
        themeB.onClick = [this] { p.theme = nextLook(p.theme); if (onTheme) onTheme(); };   // house cycle: BLACK -> PURPLE -> LIGHT -> GHOST -> BLACK
        rescanB.setTooltip("Load the Breaks folder again after you add files"); rescanB.onClick = [this] { p.rescanFolder(); };
        // presets
        addAndMakeVisible(presetBox); presetBox.setTextWhenNothingSelected("PRESETS: 40 factory + yours"); presetBox.setTooltip("Factory presets by lens (JUNGLE / HALFTIME / IDM / BASS / PROG / FILTER) and your own from Documents/Ghosty/Mr Mickens/Presets");
        presetBox.onChange = [this] { const int id = presetBox.getSelectedId(); if (id <= 0) return; if (id < 1000) p.loadFactoryPreset(id - 1); else { auto f = p.userPresets(); const int k = id - 1000; if (k < f.size()) p.loadUserPreset(f[k]); } for (auto& b : bays) b->rebuild(); syncDrums(); repaint(); };
        for (auto* b : { &presetPrev, &presetNext, &presetSave }) addAndMakeVisible(*b);
        presetPrev.onClick = [this] { stepPreset(-1); }; presetNext.onClick = [this] { stepPreset(1); };
        presetSave.getProperties().set("tint", (juce::int64) LIME.getARGB()); presetSave.setTooltip("Save the current setup as a preset of your own (shows up at the bottom of the list)"); presetSave.onClick = [this] { savePreset(); };
        rebuildPresets();
        ruler = std::make_unique<BarRuler>(p, pool); addAndMakeVisible(*ruler); ruler->refreshLoop();
        ruler->onLoopChanged = [this] { if (p.sectionLooping()) showToast(ruler->loopLabel()); };
        addChildComponent(toast); toast.setComponentID("toast"); toast.setJustificationType(juce::Justification::centred); toast.setFont(juce::FontOptions(13.0f, juce::Font::bold)); toast.setBounds(450, 116, 600, 30);
        ruler->onPlayFrom = [this](int bar) {   // own clock (or no host): from that bar; DAW stopped: the preview from that bar; DAW playing: the DAW decides
            if (p.ownTransport()) p.sync.startOwn(p.ownStartFor(bar));   // LOOP ON: the loop's own timeline
            else if (!p.hostRunning.load()) { p.engine.restartAt(bar * 4.0); p.engine.playing.store(true); } };
        addAndMakeVisible(bars); bars.addItemList({ "1 BAR", "2 BARS", "4 BARS", "8 BARS" }, 1); bars.setSelectedId(3, juce::dontSendNotification);
        addAndMakeVisible(dragWav); addAndMakeVisible(dragStems); dragWav.setTooltip("Drag a WAV of the mix into your DAW (renders in the background, then the drag starts)"); dragStems.setTooltip("Drag breaks / kick / snare / hats stems into your DAW");
        tempo = std::make_unique<Knob>(st, "bpm", "TEMPO"); addChildComponent(*tempo);   // replaced by the SYNC transport cluster (own BPM box / DAW tempo); kept for old automation tempo->s.setTooltip("Tempo for the standalone app. Inside a DAW, Mr. Mickens always follows your project tempo (drag-outs too).");
        // fx
        const char* ids[] = { "crunch", "bits", "chorus", "reverb", "size", "brk_lvl", "drm_lvl", "duck_rel", "volume",
                              "dry", "res", "lp_cut", "lp_mix", "hp_cut", "hp_mix", "bp_cut", "bp_mix", "nt_cut", "nt_mix", "cb_cut", "cb_mix", "fm_vow", "fm_mix",
                              "st_cut", "st_env", "st_dec" };
        const char* caps[] = { "CRUNCH", "BITS", "CHORUS", "REVERB", "SIZE", "BREAKS", "DRUMS", "DUCK REL", "VOLUME",
                               "DRY", "RESO", "CUTOFF", "MIX", "CUTOFF", "MIX", "CUTOFF", "MIX", "CUTOFF", "MIX", "PITCH", "MIX", "VOWEL", "MIX",
                               "FILTER", "ENV AMT", "ENV DECAY" };
        for (int i = 0; i < 26; ++i) { fxKnobs.push_back(std::make_unique<Knob>(st, ids[i], caps[i])); addAndMakeVisible(*fxKnobs.back()); }
        // LATCH is its own switch (not the stutter itself): off = hold to stutter, release to stop; on = tap on, tap off
        stutLatchBtn.setClickingTogglesState(true); stutLatchBtn.getProperties().set("tint", (juce::int64) PINK.getARGB()); addAndMakeVisible(stutLatchBtn);
        stutLatchBtn.setToggleState(p.stutterLatch, juce::dontSendNotification);
        stutLatchBtn.onClick = [this] { p.stutterLatch = stutLatchBtn.getToggleState(); if (!p.stutterLatch) setStutter(false); };
        addAndMakeVisible(stutHold); stutHold.getProperties().set("tint", (juce::int64) BLOOD.getARGB());
        stutHold.onHold = [this](bool down) {
            if (p.stutterLatch) { if (down) setStutter(!stutterOn()); }   // latched: each press flips it
            else setStutter(down); };                                      // momentary: on while held
        stutHold.setTooltip("Hold to stutter the breaks (LATCH keeps it on). It engages on the next 16th; MIDI F2 does the same, sample-exact.");
        addAndMakeVisible(stutDiv); stutDiv.addItemList({ "1/8", "1/16", "1/32", "1/64", "1/16T" }, 1); stutDivAtt = std::make_unique<APVTS::ComboBoxAttachment>(st, "st_div", stutDiv);
        // ---- Mickens 2: stutter performance
        auto cmb = [&](std::unique_ptr<Combo>& c, const char* id, juce::StringArray items, const char* tip) { c = std::make_unique<Combo>(st, id, items); c->setTooltip(tip); addAndMakeVisible(*c); };
        auto knb = [&](std::unique_ptr<Knob>& k, const char* id, const char* cap, const char* tip) { k = std::make_unique<Knob>(st, id, cap); k->s.setTooltip(tip); addAndMakeVisible(*k); };
        cmb(stMode, "st_mode", { "ROLL", "RAMP", "GLITCH", "BREAKDOWN" }, "ROLL plain repeats. RAMP repeats get shorter (a build). GLITCH every repeat picks its own length, spot and direction. BREAKDOWN repeats get longer, slower, darker and quieter until it fades out by itself.");
        knb(stGate, "st_gate", "GATE", "How much of each repeat sounds"); knb(stDec, "st_vdec", "DECAY", "Each repeat quieter than the last"); knb(stPitch, "st_pitch", "PITCH", "Semitones per repeat (+ rises, - falls)"); knb(stMove, "st_move", "MOVE", "Repeats swing across the stereo field and the filter wanders");
        // fills
        cmb(fillStyle, "fill_style", { "CLASSIC", "JUNGLE", "ROLLING", "GLITCH", "CHAOS" }, "CLASSIC snare run. JUNGLE snare/ghost triplets with a kick drop. ROLLING building ratchets. GLITCH reversed, pitched, cut-up slices. CHAOS anything goes. The downbeat after a fill always lands on a kick.");
        cmb(fillEvery, "fill_every", { "OFF", "4 BARS", "8 BARS", "16 BARS" }, "Automatic fills at the end of every N bars"); cmb(fillLen, "fill_len", { "1 BEAT", "2 BEATS", "1 BAR" }, "How long a fill is (it always ends on the bar line)");
        knb(fillInt, "fill_int", "INTENSITY", "How wild the fill gets");
        fillNow.getProperties().set("tint", (juce::int64) GOLD.getARGB()); fillNow.setTooltip("Fill from the next 16th into the next bar line (MIDI E2 does the same). Pressed inside the last 16th of a bar it fills into the bar after. Needs the beat running."); addAndMakeVisible(fillNow);
        fillNow.onClick = [this] { p.engine.pushPad(4, 0, 1.0f); };
        // interlock
        cmb(ilMode, "il_mode", { "OFF", "COMPLEMENT", "CALL & RESPONSE", "UNISON", "CROSS-RHYTHM", "COLLISION" }, "How the other bays answer the MAIN bay. COMPLEMENT: they rest where it hits. CALL & RESPONSE: they answer every other bar. UNISON: they double its hits. CROSS-RHYTHM: a 3-against-4 layer. COLLISION: they hit with it. Set the roles in each bay.");
        knb(ilDuck, "il_duck", "BAY DUCK", "The other bays dip around the main bay's hits (opens 2 ms before each hit)");
        // filters
        cmb(fltRoute, "flt_route", { "PARALLEL", "SERIAL", "MORPH" }, "PARALLEL: every filter next to the dry break. SERIAL: LP > HP > BP > NOTCH > COMB > FORMANT, each MIX = how much of that stage. MORPH: crossfades through the filters that are ON; the MORPH knob picks where. Switching crossfades (10 ms).");
        knb(fltMorph, "flt_morph", "MORPH", "MORPH routing: where in the chain of switched-on filters you are (automate it!)");
        fltAuto = std::make_unique<Toggle>(st, "flt_auto", "AUTO GAIN", LIME); fltAuto->setTooltip("Keeps the filtered part as loud as the input (peak follower, +/-6 dB). In PARALLEL only the wet sum is corrected, DRY is never touched."); addAndMakeVisible(*fltAuto);
        knb(cbFb, "cb_fb", "FEEDBACK", "Comb feedback (negative = hollow, odd harmonics)"); cmb(cbTune, "cb_tune", { "FREE", "KEY", "TEMPO" }, "FREE: the PITCH knob. KEY: tuned to the NOTE. TEMPO: a power-of-two multiple of the beat, so it rings in time");
        knb(cbNote, "cb_note", "NOTE", "Comb note (KEY tuning)");
        cmb(ftrSrc, "ftr_src", { "OFF", "KICK", "SNARE", "ANY HIT" }, "HIT SWEEP: every kick / snare / any hit (from the main bay's DNA and the drum machine) fires a resonant sweep from the hit's own sample");
        knb(ftrAmt, "ftr_amt", "SWEEP", "Sweep amount in octaves (+ up, - down)"); knb(ftrDec, "ftr_dec", "DECAY", "How fast the sweep falls back"); knb(ftrRes, "ftr_res", "RESO", "Extra resonance while the sweep is open");
        cmb(phrLen, "phr_len", { "OFF", "1 BAR", "2 BARS", "4 BARS", "8 BARS" }, "PHRASE SWEEP: a tempo-synced sweep over the phrase that snaps back at the phrase start"); cmb(phrShape, "phr_shape", { "RISE", "FALL", "ARCH" }, "Sweep shape over the phrase");
        knb(phrAmt, "phr_amt", "AMOUNT", "Phrase sweep amount in octaves");
        cmb(interp, "interp", { "LO-FI", "CLEAN", "HQ" }, "Sample reads: LO-FI = the classic 2-point reads (grit on pitched hats), CLEAN = 4-point (the default), HQ = 8-point sinc. Drag-outs always use HQ unless LO-FI is chosen.");
        // snapshot deck
        for (int i = 0; i < MickensProcessor::kSlots; ++i) { slotBtn[i].setButtonText(juce::String(i + 1)); slotBtn[i].getProperties().set("tint", (juce::int64) rainbow(i / 8.0f).getARGB()); addAndMakeVisible(slotBtn[i]);
            slotBtn[i].setTooltip("Click: recall slot " + juce::String(i + 1) + ". Shift-click, right-click or STORE armed: store everything (knobs, breaks, drum pattern, frozen DNA, step locks) here. Alt-click clears it.");
            slotBtn[i].onClick = [this, i] { const auto m = juce::ModifierKeys::getCurrentModifiers();
                if (m.isAltDown()) { p.clearSlot(i); return; }
                if (m.isShiftDown() || m.isPopupMenu() || storeArm.getToggleState()) { p.storeSlot(i); storeArm.setToggleState(false, juce::dontSendNotification); } else { p.recallSlot(i); for (auto& b : bays) b->rebuild(); syncDrums(); } }; }
        storeArm.setClickingTogglesState(true); storeArm.getProperties().set("tint", (juce::int64) GOLD.getARGB()); storeArm.setTooltip("Arm, then click a slot to store the whole setup there"); addAndMakeVisible(storeArm);
        morphOn = std::make_unique<Toggle>(st, "morph_on", "MORPH", PINK); morphOn->setTooltip("SNAPSHOT MORPH: the MORPH knob glides every knob from slot A to slot B; breaks, patterns and modes switch on the bar line half way. While it is on you hear the slots, not live edits (edit a slot: STORE)."); addAndMakeVisible(*morphOn);
        juce::StringArray sl; for (int i = 0; i < MickensProcessor::kSlots; ++i) sl.add("A: SLOT " + juce::String(i + 1)); cmb(morphA, "morph_a", sl, "Morph from this slot");
        juce::StringArray sl2; for (int i = 0; i < MickensProcessor::kSlots; ++i) sl2.add("B: SLOT " + juce::String(i + 1)); cmb(morphB, "morph_b", sl2, "Morph to this slot");
        knb(morphAmt, "morph", "A > B", "0 = slot A, 1 = slot B. Automate it for whole-setup transitions.");
        // browse
        searchB.getProperties().set("tint", (juce::int64) CYAN.getARGB()); searchB.setTooltip("Search the break list by feel and words, or BREAK MATCH a bay"); addAndMakeVisible(searchB);
        searchB.onClick = [this] { search.open(0, 0); };
        addAndMakeVisible(meter);
        addChildComponent(search); search.onClose = [this] { search.setVisible(false); };
        setSize(W, H);
    }
    bool stutterOn() const { return p.apvts.getRawParameterValue("stutter")->load() > 0.5f; }
    void setStutter(bool on) { if (auto* pr = p.apvts.getParameter("stutter")) if (stutterOn() != on) pr->setValueNotifyingHost(on ? 1.0f : 0.0f); }
    int barsNow() const { static const int B[] = { 1, 2, 4, 8 }; return B[juce::jlimit(0, 3, bars.getSelectedId() - 1)]; }
    void rebuildPresets() {
        presetBox.clear(juce::dontSendNotification); const auto& P = mk::factoryPresets(); juce::String lens;
        for (int i = 0; i < (int) P.size(); ++i) { if (lens != P[(size_t) i].lens) { lens = P[(size_t) i].lens; presetBox.addSectionHeading(lens); } presetBox.addItem(P[(size_t) i].name, i + 1); }
        auto u = p.userPresets(); if (!u.isEmpty()) presetBox.addSectionHeading("YOUR PRESETS"); for (int k = 0; k < u.size(); ++k) presetBox.addItem(u[k].getFileNameWithoutExtension(), 1000 + k);
        presetBuilt = u.size();
        if (p.currentPreset >= 0) presetBox.setSelectedId(p.currentPreset + 1, juce::dontSendNotification); else presetBox.setText(p.presetName, juce::dontSendNotification);
    }
    void stepPreset(int d) { const int n = (int) mk::factoryPresets().size(); const int cur = p.currentPreset; const int next = cur < 0 ? (d > 0 ? 0 : n - 1) : ((cur + d) % n + n) % n; presetBox.setSelectedId(next + 1, juce::sendNotificationSync); }
    void savePreset() {
        chooser = std::make_unique<juce::FileChooser>("Save a preset", p.presetDir().getChildFile("My preset.mickens"), "*.mickens");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this](const juce::FileChooser& fc) {   // NOT-EXPORT: a .mickens preset (parameters + your own dropped samples), allowed in demo
            auto f = fc.getResult(); if (f == juce::File()) return; if (!p.saveUserPreset(f.getFileNameWithoutExtension())) return; if (f.getParentDirectory() != p.presetDir()) p.writeSetup(f.withFileExtension("mickens")); rebuildPresets(); });
    }
    void syncDrums() {
        static const char* lane[] = { "KICK", "SNARE", "HAT", "OPEN HAT" };
        for (int d = 0; d < mk::kDrums; ++d) {
            auto& c = drumSel[d]; c.clear(juce::dontSendNotification); c.addSectionHeading(lane[d]);
            auto ch = MickensProcessor::drumChoices(d); for (int i = 0; i < ch.size(); ++i) c.addItem(ch[i], i + 1);
            if (auto& u = p.userDrum[(size_t) d]) { c.addSectionHeading("Your sample"); c.addItem(u->name, 999); c.setSelectedId(999, juce::dontSendNotification); }
            else { const int i = ch.indexOf(p.drumName[(size_t) d]); if (i >= 0) c.setSelectedId(i + 1, juce::dontSendNotification); else { c.addItem(p.drumName[(size_t) d], 500); c.setSelectedId(500, juce::dontSendNotification); } }
            c.setTooltip(juce::String(lane[d]) + " sound. Drop your own sample on this lane's steps.");
        }
    }
    void save() {
        chooser = std::make_unique<juce::FileChooser>("Save this Mr. Mickens setup", p.saveDir().getChildFile("My breaks.mickens"), "*.mickens");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this](const juce::FileChooser& fc) {   // NOT-EXPORT: a .mickens setup (parameters + your own dropped samples), allowed in demo
            auto f = fc.getResult(); if (f == juce::File()) return; f = f.withFileExtension("mickens");
            p.writeSetup(f); });
    }
    void load() {
        chooser = std::make_unique<juce::FileChooser>("Load a Mr. Mickens setup", p.saveDir(), "*.mickens");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
            if (fc.getResult().existsAsFile() && p.loadSession(fc.getResult())) { for (auto& b : bays) b->rebuild(); syncDrums(); rebuildPresets(); repaint(); } });
    }
    uint32_t lastDrumNames = 0xffffffffu; int lastRoute = -1; bool lastMorph = false; int lastPreset = -2; juce::String lastPresetName;
    // OWN (or no host): editable BPM + TAP; DAW: the project tempo, read-only and greyed
    void syncTransportUi() {
        const bool own = p.ownTransport();
        bpmBox.setVisible(own); tapB.setVisible(own); dawTempo.setVisible(!own);
        if (own && !bpmBox.isBeingEdited()) { const auto t = juce::String(p.sync.getOwnBpm(), 1); if (bpmBox.getText() != t) bpmBox.setText(t, juce::dontSendNotification); }
        if (!own) { const auto t = juce::String(p.currentBpm(), 1) + " BPM\nfrom DAW"; if (dawTempo.getText() != t) dawTempo.setText(t, juce::dontSendNotification); dawTempo.setColour(juce::Label::textColourId, DIM.withAlpha(GLASS ? 1.0f : 0.8f)); }   // (GHOST: full secondary ink on the glass)
        bpmBox.setColour(juce::Label::textColourId, INK); bpmBox.setColour(juce::Label::outlineColourId, CYAN.withAlpha(0.7f));
        if (p.theme != themeUi) { themeUi = p.theme; bypassButton.setTheme(houseTheme(p.theme)); syncButton.setTheme(houseTheme(p.theme));   // the house tokens of this look
            search.words.setTextToShowWhenEmpty("words: jungle, ghosty, sparse, swung, bright, a family name...", DIM); }
    }
    int rulerA = -2, rulerB = -2; bool rulerLoop = false, wasPlaying = false; juce::Label toast; double toastUntil = 0;
    void showToast(const juce::String& t) { toast.setText(t, juce::dontSendNotification); toast.setColour(juce::Label::backgroundColourId, pal().card); toast.setColour(juce::Label::textColourId, INK);
        toast.setColour(juce::Label::outlineColourId, CYAN); toast.setVisible(true); toast.toFront(false); toastUntil = juce::Time::getMillisecondCounterHiRes() + 2600; }
    void toggleLoopKey() { ruler->toggleLoop(); if (p.sectionLooping()) showToast(ruler->loopLabel()); else if (ruler->hasSection()) showToast("LOOP off: everything plays (the brace stays)"); }
    void tick() {
        if (!MickensEditor::freezeAnimation) phase += 1.0f / 30.0f; ++tickN;
        if (p.secA.load() != rulerA || p.secB.load() != rulerB || p.secLoop.load() != rulerLoop) { rulerA = p.secA.load(); rulerB = p.secB.load(); rulerLoop = p.secLoop.load(); ruler->layoutGrips(); ruler->refreshLoop(); }
        {   // LOOP ON: a toast when playback starts
            const bool playingNow = p.hostRunning.load() || p.engine.playing.load() || p.sync.isOwnPlaying();
            if (playingNow && !wasPlaying && p.sectionLooping()) showToast(ruler->loopLabel());
            wasPlaying = playingNow; if (toast.isVisible() && juce::Time::getMillisecondCounterHiRes() > toastUntil) toast.setVisible(false); }   // a loaded session's section
        for (auto& b : bays) b->sync();
        drums.repaint();
        const bool host = p.hostRunning.load(), on = p.engine.playing.load();
        themeB.setButtonText(lookName(nextLook(p.theme)));   // names the look the click switches to
        { const bool onN = p.apvts.getRawParameterValue("play_mode")->load() > 0.5f; const juce::String txt = onN ? (p.gateOpenUi.load() ? "PLAY: ON NOTES  >" : "PLAY: ON NOTES  ||") : "PLAY: ALWAYS"; if (playMode->getButtonText() != txt) playMode->setButtonText(txt); }
        if (p.ownTransport()) { const bool o = p.sync.isOwnPlaying(); play.setButtonText(o ? "STOP" : "PLAY"); play.setToggleState(o, juce::dontSendNotification); }
        else { play.setButtonText(host ? "DAW PLAYING" : (on ? "STOP" : "PLAY")); play.setToggleState(host || on, juce::dontSendNotification); }
        syncTransportUi();
        stutHold.setToggleState(p.engine.uiStutter.load() || stutterOn(), juce::dontSendNotification);
        fillNow.setToggleState(p.engine.uiFill.load(), juce::dontSendNotification);
        if (search.isVisible() && search.seenVersion != p.listVersion.load()) search.refresh();
        // a recalled slot / loaded session can change the drum sounds behind the combos
        { uint32_t h = 0; for (int d = 0; d < mk::kDrums; ++d) h = h * 31u + (uint32_t) p.drumName[(size_t) d].hashCode() + (p.userDrum[(size_t) d] ? 7u : 0u); if (h != lastDrumNames) { lastDrumNames = h; syncDrums(); } }
        if (p.currentPreset != lastPreset || p.presetName != lastPresetName || p.userPresets().size() != presetBuilt) { lastPreset = p.currentPreset; lastPresetName = p.presetName; rebuildPresets(); }
        const int a = (int) p.apvts.getRawParameterValue("morph_a")->load() - 1, bsl = (int) p.apvts.getRawParameterValue("morph_b")->load() - 1; const bool mo = p.morphing(); const int side = p.morphSideUi.load();
        for (int i = 0; i < MickensProcessor::kSlots; ++i) { const bool used = p.slots[(size_t) i].used;
            slotBtn[i].setToggleState(used, juce::dontSendNotification); slotBtn[i].setAlpha(used ? 1.0f : 0.55f);
            slotBtn[i].setButtonText(juce::String(i + 1) + (mo && i == a ? (side == 0 ? " A*" : " A") : mo && i == bsl ? (side == 1 ? " B*" : " B") : juce::String())); }
        // MORPH on: you hear the slots, not live edits -> the live controls grey out and a banner says so
        if (mo != lastMorph) { lastMorph = mo; for (auto& b : bays) { b->setEnabled(!mo); b->setAlpha(mo ? 0.55f : 1.0f); } drums.setEnabled(!mo); drums.setAlpha(mo ? 0.55f : 1.0f); for (auto& k : fxKnobs) k->setEnabled(!mo); repaint(); }
        // controls the current filter route ignores grey out
        const int route = (int) p.apvts.getRawParameterValue("flt_route")->load();
        if (route != lastRoute) { lastRoute = route; auto en = [](juce::Component& c, bool e) { c.setEnabled(e); c.setAlpha(e ? 1.0f : 0.4f); };
            en(*fxKnobs[9], route == mk::kParallel); en(*fltMorph, route == mk::kMorphRoute); for (int i = 0; i < 6; ++i) { fxKnobs[(size_t) (10 + i * 2)]->caption = route == mk::kMorphRoute ? "ON" : "MIX"; fxKnobs[(size_t) (10 + i * 2)]->repaint(); } repaint(secFilt); }
        if (tickN % 2 == 0) repaint(header);
        repaint(secFilt.withHeight(24)); repaint(secStut.withHeight(24)); repaint(secDeck.withHeight(24));
    }
    // GHOST tests: the glass under the texts (no text), and where Content / the bays draw their own texts
    std::vector<std::pair<juce::Rectangle<int>, juce::Colour>> sections() const {
        return { { secDrums, CYAN }, { secFill, GOLD }, { secIl, PINK }, { secDeck, LIME }, { secCrunch, BLOOD }, { secSpace, CYAN }, { secMix, LIME }, { secBrowse, CYAN },
                 { secFilt, PINK }, { secMotion, PINK }, { secStut, GOLD } };
    }
    void ghostSurfaces(juce::Graphics& g) {
        gl::backdrop().draw(g, W, H); gl::panels().draw(g, header.toFloat().reduced(6.0f, 3.0f), 16.0f, 0.40f, 6.0f);
        for (auto& [r, c] : sections()) gl::panels().draw(g, r.toFloat(), 12.0f, 0.38f, 8.0f);
        for (auto& b : bays) gl::panels().draw(g, b->getBounds().toFloat().reduced(1), 12.0f, 0.40f, 0.0f);
        gl::panels().draw(g, ruler->getBounds().toFloat(), 10.0f, 0.40f, 0.0f);
    }
    std::vector<std::pair<juce::Rectangle<int>, juce::Colour>> ghostTexts() const {
        std::vector<std::pair<juce::Rectangle<int>, juce::Colour>> v;
        v.push_back({ { 124, 70, 330, 10 }, DIM }); v.push_back({ { 124, 84, 340, 10 }, DIM }); v.push_back({ { 124, 98, 300, 8 }, DIM });
        for (auto& [r, c] : sections()) { v.push_back({ r.reduced(12, 6).removeFromTop(16).withWidth(juce::jmin(120, r.getWidth() - 24)).reduced(0, 3), c });
                                          v.push_back({ r.reduced(16, 26), DIM }); v.push_back({ r.reduced(16, 26), INK }); }
        for (int b = 0; b < mk::kBays; ++b) { const auto o = bays[b]->getPosition();
            v.push_back({ juce::Rectangle<int>(12, 12, 50, 14) + o, rainbow(b / 4.0f) });
            for (int y : { bays[b]->seqLabelY, bays[b]->dnaLabelY, bays[b]->duckLabelY }) v.push_back({ juce::Rectangle<int>(10, y + 2, 150, 8) + o, DIM });
            v.push_back({ bays[b]->getLocalBounds().reduced(14, 30) + o, INK }); v.push_back({ bays[b]->getLocalBounds().reduced(14, 30) + o, DIM }); }
        return v;
    }
    void section(juce::Graphics& g, juce::Rectangle<int> r, const juce::String& t, juce::Colour c) {
        if (GLASS) gl::panels().draw(g, r.toFloat(), 12.0f, 0.38f, 8.0f); else { g.setColour(CARD); g.fillRoundedRectangle(r.toFloat(), 10); }   // GHOST: a glass panel (cached per size)
        g.setColour(c.withAlpha(0.55f)); g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 10, 1.2f);
        g.setColour(c); g.setFont(juce::FontOptions(12.5f, juce::Font::bold)); g.drawText(t, r.reduced(12, 6).removeFromTop(16), juce::Justification::centredLeft);
    }
    void paint(juce::Graphics& g) override {
        if (GLASS) { gl::backdrop().draw(g, W, H); gl::panels().draw(g, header.toFloat().reduced(6.0f, 3.0f), 16.0f, 0.40f, 6.0f); }   // GHOST: the aurora (cached per size) + the header as glass
        else if (LIGHT) g.fillAll(BG);   // LIGHT: one flat opaque window colour (no translucent glow or star layer)
        else {
        g.setGradientFill(juce::ColourGradient(BGTOP, 0, 0, BGBOT, 0, (float) H0, false)); g.fillAll();   // (the strip below H0 continues in BGBOT)
        // star field + dark glow
        juce::Random r(3); for (int i = 0; i < 160; ++i) { const float x = r.nextFloat() * W, y = r.nextFloat() * H0, tw = 0.25f + 0.25f * std::sin(phase * 2 + i);
            g.setColour(rainbow(r.nextFloat(), 0.3f).withAlpha(tw)); g.fillEllipse(x, y, 1.6f, 1.6f); }
        g.setGradientFill(juce::ColourGradient(PINK.withAlpha(0.10f), (float) W / 2, 0, juce::Colours::transparentBlack, (float) W / 2, 260, false)); g.fillRect(0, 0, W, 260);
        }
        // header: logo, title, the unicorns
        drawLogo(g, juce::Rectangle<float>(14, 6, 100, 100), phase);
        if (GLASS) { g.setColour(juce::Colours::white.withAlpha(0.85f)); g.drawRoundedRectangle(juce::Rectangle<float>(14, 6, 100, 100).expanded(1.0f), 17.0f, 2.0f);   // GHOST: the logo tile outlined on the glass
                     g.setColour(ghosty::glass::ink().withAlpha(0.55f)); g.drawRoundedRectangle(juce::Rectangle<float>(14, 6, 100, 100).expanded(2.5f), 18.0f, 1.0f); }
        rainbowText(g, "MR. MICKENS", { 122, 14, 420, 50 }, 44, phase * 0.15f);
        g.setColour(DIM); g.setFont(juce::FontOptions(11.0f, juce::Font::bold)); g.drawText("DARK HARDCORE BREAK MACHINE  |  MICKENS 2", 124, 68, 340, 14, juce::Justification::centredLeft); g.drawText("100 BREAKS  |  BREAK DNA  |  FILLS  |  STUTTER  |  DECK", 124, 82, 350, 14, juce::Justification::centredLeft);
        g.setFont(juce::FontOptions(9.5f, juce::Font::bold)); g.drawText("knobs: hover = value, right-click = type, dbl-click = default   v" + MickensEditor::versionString(), 124, 96, 312, 12, juce::Justification::centredLeft);
        {
            const auto u = uniArea.toFloat(); const float gy = u.getBottom() - 6;
            g.setColour(LIGHT ? juce::Colour(0xffb9aed3) : juce::Colours::white.withAlpha(0.06f)); g.fillRoundedRectangle(u, 12);   // LIGHT: an opaque lavender sky so the white unicorns read
            if (GLASS) { g.setGradientFill(juce::ColourGradient(juce::Colour(0x283a2e6b), u.getX(), u.getY(), juce::Colour(0x003a2e6b), u.getX(), u.getY() + 16, false)); g.fillRoundedRectangle(u, 12);   // GHOST: the sky as a glass screen
                         g.setColour(juce::Colour(0x60463e6c)); g.drawRoundedRectangle(u.reduced(0.5f), 12, 1.0f); g.setColour(juce::Colour(0xd0ffffff)); g.drawLine(u.getX() + 12, u.getBottom() - 0.6f, u.getRight() - 12, u.getBottom() - 0.6f, 1.2f); }
            const float s = 0.9f; const float trot = std::sin(phase * 1.3f) * 6;
            const juce::Point<float> a(u.getX() + 70 + trot, gy - 36 * s), b(u.getRight() - 64 - trot, gy - 36 * s);
            // left unicorn pukes at the right one; the right one (facing left) poops behind itself
            drawPuke(g, a.translated(60 * s, -36 * s), juce::Point<float>(u.getCentreX() + 20, gy - 2), phase, s);
            drawUnicorn(g, a, s, false, phase);
            drawPoop(g, b.translated(36 * s, -8 * s), gy, phase, s);
            drawUnicorn(g, b, s, true, phase + 1.3f);
            // pixel water + jetskis racing along the bottom of the strip
            juce::Graphics::ScopedSaveState ws(g); g.reduceClipRegion(uniArea);
            const auto water = juce::Rectangle<float>(u.getX(), u.getBottom() - 18, u.getWidth(), 18);
            const float px = 3.0f, span = u.getWidth() + 80;
            drawPixelWater(g, water, phase, px);
            for (int k = 0; k < 3; ++k) {
                const bool right = k != 1; const float sp = 70.0f + k * 25.0f, off = k * 140.0f;
                float x = std::fmod(phase * sp + off, span) - 50; if (!right) x = u.getRight() + 10 - (x + 50);
                const float bobY = std::floor(std::sin(phase * 6 + k) * 1.5f) * px;
                drawJetski(g, std::floor(x / px) * px, water.getY() - 21 + bobY, px, right, k * 0.31f + 0.8f, phase);
            }
        }
        section(g, secDrums, "DRUM MACHINE  |  click = hit, right-click / shift = ghost, drop samples on a lane", CYAN);
        section(g, secFill, "GENERATE FILL", GOLD); section(g, secIl, "INTERLOCK", PINK);
        const bool mo = p.morphing(); const int side = p.morphSideUi.load(), a = (int) p.apvts.getRawParameterValue("morph_a")->load(), b = (int) p.apvts.getRawParameterValue("morph_b")->load();
        section(g, secDeck, mo ? "SNAPSHOT DECK  |  MORPHING: hearing slot " + juce::String(side ? b : a) + (side ? " (B)" : " (A)") + "  |  live edits are not heard: STORE into a slot, or switch MORPH off" : juce::String("SNAPSHOT DECK  |  click = recall, shift-click = store, alt = clear"), mo ? PINK : LIME);
        section(g, secCrunch, "CRUNCH", BLOOD); section(g, secSpace, "SPACE", CYAN); section(g, secMix, "MIX  |  OUT", LIME); section(g, secBrowse, "BROWSE", CYAN);
        const int route = (int) p.apvts.getRawParameterValue("flt_route")->load();
        const char* ft[] = { "FILTERS IN PARALLEL  (each MIX blends it in next to the DRY break)", "FILTERS IN SERIES  (LP > HP > BP > NOTCH > COMB > FORMANT; each MIX = how much of that stage)", "FILTER MORPH  (MORPH crossfades through the filters that are ON)" };
        section(g, secFilt, juce::String(ft[juce::jlimit(0, 2, route)]) + (p.legacy.load() ? "   |   classic session: the new routing engages when you touch it" : ""), PINK);
        section(g, secMotion, "FILTER MOTION  |  HIT SWEEP fires from the main bay's hits + the drums  |  PHRASE SWEEP rides the bars", PINK);
        section(g, secStut, juce::String("STUTTER  (tempo-synced, sample-exact)") + (p.engine.uiStutter.load() ? "   |   ON" : ""), GOLD);
        g.setColour(DIM); g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
        const char* fl[] = { "ROUTING", "DRY / RESO", "LOW-PASS", "HIGH-PASS", "BAND-PASS", "NOTCH", "COMB", "COMB TUNING", "FORMANT" };
        for (int i = 0; i < 9; ++i) g.drawText(fl[i], filtCol[i].getX(), secFilt.getY() + 24, filtCol[i].getWidth(), 12, juce::Justification::centred);
        g.drawText("HIT SWEEP", secMotion.getX() + 14, secMotion.getY() + 22, 300, 12, juce::Justification::centredLeft); g.drawText("PHRASE SWEEP", phrX, secMotion.getY() + 22, 300, 12, juce::Justification::centredLeft); g.drawText("QUALITY", qualX, secMotion.getY() + 22, 100, 12, juce::Justification::centredLeft);
        g.drawText("roles: each bay", secIl.reduced(10, 0).withTrimmedRight(74).withTrimmedTop(secIl.getHeight() - 20).withHeight(12), juce::Justification::centredLeft, true);
        g.drawText("by feel, by words, BREAK MATCH", secBrowse.reduced(10, 0).withTrimmedTop(secBrowse.getHeight() - 20).withHeight(12), juce::Justification::centred, true);
        if (mo) { auto bn = juce::Rectangle<float>(12, 112, W - 24, 24); g.setColour(LIGHT ? PINK : PINK.withAlpha(0.85f)); g.fillRoundedRectangle(bn, 8); g.setColour(juce::Colours::white); g.setFont(juce::FontOptions(12.5f, juce::Font::bold)); g.drawText("MORPHING A > B  -  you hear the slots. Edit a slot: set things up, then STORE it. Or switch MORPH off to edit live.", bn, juce::Justification::centred); }
    }
    juce::Rectangle<int> filtCol[9]; int phrX = 0, qualX = 0;
    void resized() override {
        header = { 0, 0, W, 112 }; uniArea = { 566, 6, 380, 100 }; ruler->setBounds(12, H0, W - 24, H - H0 - 8);   // the BAR ruler: its own strip under everything (nothing above moves)
        { auto h = juce::Rectangle<int>(962, 10, W - 974, 98);
          auto r1 = h.removeFromTop(28); bypassButton.setBounds(r1.removeFromRight(84).withSizeKeepingCentre(84, 26)); r1.removeFromRight(5); playMode->setBounds(r1.removeFromRight(116)); r1.removeFromRight(5); syncButton.setBounds(r1.removeFromRight(96).withSizeKeepingCentre(96, 26)); r1.removeFromRight(6);
          muteAllB->setBounds(r1.removeFromLeft(56)); r1.removeFromLeft(4); dice.setBounds(r1.removeFromLeft(48)); r1.removeFromLeft(4); saveB.setBounds(r1.removeFromLeft((r1.getWidth() - 4) / 2)); r1.removeFromLeft(4); loadB.setBounds(r1);
          h.removeFromTop(6); auto r3 = h.removeFromTop(26); rescanB.setBounds(r3.removeFromRight(76)); r3.removeFromRight(5); folderB.setBounds(r3.removeFromRight(124)); r3.removeFromRight(8);
          presetPrev.setBounds(r3.removeFromLeft(24)); r3.removeFromLeft(3); presetNext.setBounds(r3.removeFromLeft(24)); r3.removeFromLeft(4); presetSave.setBounds(r3.removeFromRight(96)); r3.removeFromRight(4); presetBox.setBounds(r3);
          h.removeFromTop(6); auto r2 = h.removeFromTop(28); themeB.setBounds(r2.removeFromRight(70)); r2.removeFromRight(6); bars.setBounds(r2.removeFromLeft(90)); r2.removeFromLeft(6); dragWav.setBounds(r2.removeFromLeft(140)); r2.removeFromLeft(6); dragStems.setBounds(r2);
          tempo->setBounds(juce::Rectangle<int>(486, 30, 64, 76)); unlock.setBounds(476, 4, 82, 24);
          // the transport cluster (where the TEMPO knob was): PLAY, then the own BPM box + TAP (OWN) or the DAW's tempo (DAW)
          play.setBounds(440, 32, 118, 30); bpmBox.setBounds(440, 68, 68, 30); tapB.setBounds(512, 70, 46, 26); dawTempo.setBounds(436, 66, 126, 36); }
        const int bw = (W - 24 - 3 * 8) / 4, bayH = 448;
        for (int b = 0; b < mk::kBays; ++b) bays[b]->setBounds(12 + b * (bw + 8), 112, bw, bayH);
        search.setBounds(12, 112, W - 24, bayH);
        const int y0 = 112 + bayH + 8, hAll = H0 - 12 - y0;
        // ---- left column: drums, fill + interlock, snapshot deck
        secDrums = { 12, y0, 560, hAll - 8 - 100 - 8 - 92 };
        { auto r = secDrums.reduced(10); r.removeFromTop(24);
          auto btns = r.removeFromBottom(28); muteAll->setBounds(btns.removeFromRight(90)); btns.removeFromRight(6); const int pw = (btns.getWidth() - 18) / 4; for (int i = 0; i < 4; ++i) { patBtn[i].setBounds(btns.removeFromLeft(pw)); btns.removeFromLeft(6); }
          r.removeFromBottom(8);
          auto names = r.removeFromLeft(212); const int lh = names.getHeight() / 4;
          for (int d = 0; d < mk::kDrums; ++d) { auto l = names.removeFromTop(lh).reduced(0, 3); drumLvl[d]->setBounds(l.removeFromRight(40).withTrimmedBottom(-12)); l.removeFromRight(4); laneMute[d]->setBounds(l.removeFromLeft(26).withSizeKeepingCentre(26, 26)); l.removeFromLeft(4); hit[d].setBounds(l.removeFromLeft(34).withSizeKeepingCentre(34, 26)); l.removeFromLeft(4); drumSel[d].setBounds(l.withSizeKeepingCentre(l.getWidth(), 26)); }
          r.removeFromLeft(6); drums.setBounds(r); }
        secFill = { 12, secDrums.getBottom() + 8, 340, 100 }; secIl = { 360, secFill.getY(), 212, 100 };
        { auto r = secFill.reduced(8); r.removeFromTop(20); fillInt->setBounds(r.removeFromRight(62)); r.removeFromRight(4);
          auto r1 = r.removeFromTop(24); fillStyle->setBounds(r1.removeFromLeft(92)); r1.removeFromLeft(4); fillEvery->setBounds(r1.removeFromLeft(82)); r1.removeFromLeft(4); fillLen->setBounds(r1);
          r.removeFromTop(6); fillNow.setBounds(r.removeFromTop(28)); }
        { auto r = secIl.reduced(8); r.removeFromTop(20); ilDuck->setBounds(r.removeFromRight(62)); r.removeFromRight(4); ilMode->setBounds(r.removeFromTop(24)); }
        secDeck = { 12, secFill.getBottom() + 8, 560, 92 };
        { auto r = secDeck.reduced(8); r.removeFromTop(22); r.removeFromBottom(2);
          auto sl = r.removeFromLeft(188); const int sw = (sl.getWidth() - 12) / 4; auto ra = sl.removeFromTop(24); sl.removeFromTop(4); auto rb = sl.removeFromTop(24);
          for (int i = 0; i < 4; ++i) { slotBtn[i].setBounds(ra.removeFromLeft(sw)); ra.removeFromLeft(4); slotBtn[i + 4].setBounds(rb.removeFromLeft(sw)); rb.removeFromLeft(4); }
          r.removeFromLeft(8); auto c1 = r.removeFromLeft(66); storeArm.setBounds(c1.removeFromTop(24)); c1.removeFromTop(4); morphOn->setBounds(c1.removeFromTop(24));
          r.removeFromLeft(8); auto c2 = r.removeFromLeft(110); morphA->setBounds(c2.removeFromTop(24)); c2.removeFromTop(4); morphB->setBounds(c2.removeFromTop(24));
          r.removeFromLeft(6); morphAmt->setBounds(r.removeFromLeft(62).withTrimmedTop(-4)); }
        // ---- right column: crunch / space / mix+meter / browse, filters, filter motion, stutter
        const int fx0 = 580, fxW = W - 12 - fx0;
        secCrunch = { fx0, y0, 140, 104 }; secSpace = { fx0 + 148, y0, 190, 104 }; secMix = { fx0 + 346, y0, 290, 104 }; secBrowse = { fx0 + 644, y0, fxW - 644, 104 };
        auto place = [&](juce::Rectangle<int> sec, int from, int count, int trimRight = 0) { auto r = sec.reduced(8); r.removeFromTop(20); r.removeFromRight(trimRight); const int w = r.getWidth() / count; for (int i = 0; i < count; ++i) fxKnobs[(size_t) (from + i)]->setBounds(r.removeFromLeft(w).withSizeKeepingCentre(juce::jmin(w, 64), juce::jmin(r.getHeight(), 76))); };
        place(secCrunch, 0, 2); place(secSpace, 2, 3); place(secMix, 5, 4, 54);
        meter.setBounds(secMix.getRight() - 58, secMix.getY() + 26, 48, secMix.getHeight() - 34);
        { auto r = secBrowse.reduced(10); r.removeFromTop(22); searchB.setBounds(r.removeFromTop(30)); }
        secFilt = { fx0, y0 + 112, fxW, 146 };
        { auto r = secFilt.reduced(10); r.removeFromTop(28); const int widths[] = { 100, 90, 90, 90, 90, 90, 90, 118, 90 }; int x = r.getX();
          const int total = 848, pad = (r.getWidth() - total) / 8;
          for (int i = 0; i < 9; ++i) { filtCol[i] = { x, r.getY(), widths[i], r.getHeight() }; x += widths[i] + pad; }
          { auto c = filtCol[0]; fltRoute->setBounds(c.removeFromTop(24)); c.removeFromTop(4); fltAuto->setBounds(c.removeFromTop(22)); fltMorph->setBounds(c.withSizeKeepingCentre(60, juce::jmin(c.getHeight(), 60))); }
          for (int i = 1; i <= 6; ++i) { auto c = filtCol[i]; const int kh = c.getHeight() / 2; fxKnobs[(size_t) (9 + (i - 1) * 2)]->setBounds(c.removeFromTop(kh).withSizeKeepingCentre(juce::jmin(c.getWidth(), 56), kh)); fxKnobs[(size_t) (10 + (i - 1) * 2)]->setBounds(c.withSizeKeepingCentre(juce::jmin(c.getWidth(), 56), kh)); }
          { auto c = filtCol[7]; const int kh = c.getHeight() / 2; auto t = c.removeFromTop(kh); cbTune->setBounds(t.removeFromLeft(62).withSizeKeepingCentre(62, 24)); cbFb->setBounds(t.withSizeKeepingCentre(juce::jmin(t.getWidth(), 56), kh)); cbNote->setBounds(c.withSizeKeepingCentre(56, kh)); }
          { auto c = filtCol[8]; const int kh = c.getHeight() / 2; fxKnobs[21]->setBounds(c.removeFromTop(kh).withSizeKeepingCentre(56, kh)); fxKnobs[22]->setBounds(c.withSizeKeepingCentre(56, kh)); } }
        secMotion = { fx0, secFilt.getBottom() + 8, fxW, 80 };
        { auto r = secMotion.reduced(10); r.removeFromTop(34); auto k = r;
          ftrSrc->setBounds(k.removeFromLeft(92).withSizeKeepingCentre(92, 24)); k.removeFromLeft(6); for (auto* kn : { ftrAmt.get(), ftrDec.get(), ftrRes.get() }) kn->setBounds(k.removeFromLeft(60).withTrimmedTop(-10).withTrimmedBottom(-2));
          k.removeFromLeft(30); phrX = k.getX(); phrLen->setBounds(k.removeFromLeft(84).withSizeKeepingCentre(84, 24)); k.removeFromLeft(6); phrShape->setBounds(k.removeFromLeft(76).withSizeKeepingCentre(76, 24)); k.removeFromLeft(6); phrAmt->setBounds(k.removeFromLeft(60).withTrimmedTop(-10).withTrimmedBottom(-2));
          k.removeFromLeft(30); qualX = k.getX(); interp->setBounds(k.removeFromLeft(90).withSizeKeepingCentre(90, 24)); }
        secStut = { fx0, secMotion.getBottom() + 8, fxW, H0 - 12 - (secMotion.getBottom() + 8) };
        { auto r = secStut.reduced(10); r.removeFromTop(18); stutHold.setBounds(r.removeFromLeft(118).reduced(0, 8)); r.removeFromLeft(6); stutLatchBtn.setBounds(r.removeFromLeft(60).reduced(0, 14)); r.removeFromLeft(6);
          stutDiv.setBounds(r.removeFromLeft(72).withSizeKeepingCentre(72, 26)); r.removeFromLeft(6); stMode->setBounds(r.removeFromLeft(104).withSizeKeepingCentre(104, 26)); r.removeFromLeft(6);
          const int w = r.getWidth() / 7; for (auto* k : { stGate.get(), stDec.get(), stPitch.get(), stMove.get(), fxKnobs[23].get(), fxKnobs[24].get(), fxKnobs[25].get() }) k->setBounds(r.removeFromLeft(w).withSizeKeepingCentre(juce::jmin(w, 64), r.getHeight() + 4)); }
    }
};

MickensEditor::MickensEditor(MickensProcessor& p) : juce::AudioProcessorEditor(p), proc(p) {
    shownTheme = p.theme; setTheme(0);   // controls store their tints as the dark-look accents (LIGHT maps them at paint time)
    lnf = std::make_unique<LnF>(); setLookAndFeel(lnf.get());
    content = std::make_unique<Content>(p); addAndMakeVisible(*content);
    setTheme(shownTheme); static_cast<LnF*>(lnf.get())->refresh(); sendLookAndFeelChange();
    content->onTheme = [this] { shownTheme = proc.theme; setTheme(proc.theme); static_cast<LnF*>(lnf.get())->refresh(); sendLookAndFeelChange(); repaint(); };
    setResizable(true, true); setResizeLimits(W * 3 / 5, H * 3 / 5, W * 2, H * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio((double) W / H);
    setSize(W, H); startTimerHz(30);
    setWantsKeyboardFocus(true);   // clicks on non-focusable parts land here, so SPACE reaches keyPressed (SYNC OWN)
}
MickensEditor::~MickensEditor() { stopTimer(); content.reset(); setLookAndFeel(nullptr); }
void MickensEditor::paintGhostSurfaces(juce::Graphics& g) { if (content) content->ghostSurfaces(g); }
std::vector<std::pair<juce::Rectangle<int>, juce::Colour>> MickensEditor::ghostTexts() { return content ? content->ghostTexts() : std::vector<std::pair<juce::Rectangle<int>, juce::Colour>>(); }
int MickensEditor::ghostBackdropBuilds() { return gl::backdrop().builds; }
int MickensEditor::ghostPanelBuilds() { return gl::panels().builds; }
void MickensEditor::paint(juce::Graphics& g) { g.fillAll(BG); }
void MickensEditor::resized() { if (content) content->setTransform(juce::AffineTransform::scale((float) getWidth() / W)); }
juce::String MickensEditor::versionString() {
#if defined(JucePlugin_VersionString)
    return "1 beta";
#elif defined(JUCE_APPLICATION_VERSION_STRING)
    return JUCE_APPLICATION_VERSION_STRING;
#else
    return "dev";
#endif
}
void MickensEditor::openSearch(int bay, int mode) { if (content) content->search.open(bay, mode); }
void MickensEditor::closeSearch() { if (content) content->search.setVisible(false); }
MickensEditor::Contrast MickensEditor::contrastOf(int theme) {
    const int keep = THEME; setTheme(theme); Contrast c { 99, 99, 99, {} };
    const juce::Colour surf[] = { BG, CARD, CARD2 }; const char* sn[] = { "window", "panel", "cell" };
    // accents as drawn: the five named accents, the ghost colour, and the rainbow (bay colours, steps, waveform) across the hue circle
    std::vector<std::pair<juce::String, juce::Colour>> acc { { "pink", PINK }, { "cyan", CYAN }, { "green", LIME }, { "amber", GOLD }, { "red", BLOOD }, { "ghost", clsColour(mk::kG) } };
    for (int i = 0; i < 24; ++i) acc.push_back({ "rainbow " + juce::String(i * 15) + " deg", rainbow(i / 24.0f) });
    for (int k = 0; k < 3; ++k) {
        auto lo = [&](double& m, double v, const juce::String& what) { if (v < m) { m = v; c.worst << what << " on " << sn[k] << " " << juce::String(v, 2) << "; "; } };
        lo(c.body, contrast(INK, surf[k]), "body"); lo(c.secondary, contrast(DIM, surf[k]), "secondary");
        for (auto& a : acc) if (a.second.getAlpha() == 255) lo(c.accent, contrast(a.second, surf[k]), a.first);
    }
    setTheme(keep); return c;
}
// SPACE: in SYNC OWN (or with no host transport) it plays / stops only Mr. Mickens and is consumed; in SYNC TO DAW it is left to the DAW
bool MickensEditor::keyPressed(const juce::KeyPress& k) {
    if (k == juce::KeyPress(juce::KeyPress::spaceKey) && proc.ownTransport()) { proc.sync.toggleOwn(0.0); return true; }
    if ((k.getTextCharacter() == 'l' || k.getTextCharacter() == 'L') && content && proc.secA.load() >= 0 && proc.secB.load() > proc.secA.load()) { content->toggleLoopKey(); return true; }   // LOOP SECTION
    return juce::AudioProcessorEditor::keyPressed(k);
}
void MickensEditor::timerCallback() {
    if (!content) return;
    // a loaded session can switch the theme
    if (shownTheme != proc.theme) { shownTheme = proc.theme; setTheme(shownTheme); static_cast<LnF*>(lnf.get())->refresh(); sendLookAndFeelChange(); repaint(); }
    content->tick();
}
