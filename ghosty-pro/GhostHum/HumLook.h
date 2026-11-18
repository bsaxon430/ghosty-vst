#pragma once
// GHOST HUM - the look (build 2, the owner's pictures): near-black, soft green / teal light wisps and streaks, drifting particles and a
// few faint ghosts behind glass panels with thin glowing green edges; dark-glass pill buttons with green rims (selected = bright green
// with bloom); a thick neon ring with an electric crackle for the record button (teal, with concentric rings, in MELODY); the glowing
// white / cyan ghost logo with a green glow and trailing wisps. Flat: no 3D tilt. Every label is real text and stays readable.
//
// Drawing: juce::Graphics only, own code, no bitmaps, no blur filters. Glow = a few expanded strokes / fills at falling alpha. The static
// part of the backdrop (wisps, streaks) is rendered once per window size into an image; the animated part (particles, ghosts) is a
// handful of small fills per frame. GHOST ANIMATION OFF stops the timer and draws the last frame still.
// Message thread only.
#include <juce_gui_extra/juce_gui_extra.h>
#include "HumGlassKit.h"
#include <functional>
#include <vector>
#include <cmath>

namespace hum::ui {

// ---------------------------------------------------------------- tokens
namespace look {
    const juce::Colour bg        { 0xff04070a };   // near-black with a breath of blue-green
    const juce::Colour glass     { 0xff0a1511 };   // panel body (drawn translucent)
    const juce::Colour cell      { 0xff0e1a16 }, cellHi { 0xff14261f };
    const juce::Colour ink       { 0xffeaf7ef }, dim { 0xff9fb5a8 }, faint { 0xff5e7268 };
    const juce::Colour green     { 0xff3dff8c }, greenDeep { 0xff17a85a }, greenGlow { 0xff8cffc0 }, onGreen { 0xff04130a };
    const juce::Colour teal      { 0xff2ee6d6 }, cyan { 0xff8df7ff }, tealDeep { 0xff149e94 };
    const juce::Colour white     { 0xffffffff };
    inline juce::Colour accent(bool melody) { return melody ? teal : green; }
    inline juce::Colour accentGlow(bool melody) { return melody ? cyan : greenGlow; }
    inline juce::Font font(float h, bool bold = false) { return juce::Font(juce::FontOptions(h, bold ? juce::Font::bold : juce::Font::plain)); }
    inline juce::Font label(float h = 11.0f) { auto f = font(h, true); f.setExtraKerningFactor(0.08f); return f; }
    inline juce::Font techno(float h) { auto f = font(h, true); f.setExtraKerningFactor(0.22f); f.setHorizontalScale(1.08f); return f; }   // the wide "GHOST HUM" title
    inline juce::Font small(float h = 10.0f) { auto f = font(h, true); f.setExtraKerningFactor(0.1f); return f; }
}

// ---------------------------------------------------------------- glow / glass helpers
// a soft outer glow around a rounded rectangle: n rings of falling alpha
inline void glowRect(juce::Graphics& g, juce::Rectangle<float> r, float rad, juce::Colour c, float strength = 1.0f, int rings = 4, float spread = 2.5f) {
    for (int i = rings; i >= 1; --i) { g.setColour(c.withAlpha(juce::jlimit(0.0f, 1.0f, strength * 0.16f / (float) i))); g.drawRoundedRectangle(r.expanded(spread * (float) i), rad + spread * (float) i, spread * 1.2f); }
}
inline void glowEllipse(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float strength = 1.0f, int rings = 5, float spread = 3.0f) {
    for (int i = rings; i >= 1; --i) { g.setColour(c.withAlpha(juce::jlimit(0.0f, 1.0f, strength * 0.14f / (float) i))); g.drawEllipse(r.expanded(spread * (float) i), spread * 1.3f); }
}
inline void glowText(juce::Graphics& g, const juce::String& s, juce::Rectangle<int> r, juce::Justification j, juce::Colour c, juce::Colour glow, float strength = 1.0f, int lines = 1) {
    for (int i = 2; i >= 1; --i) { g.setColour(glow.withAlpha(0.12f * strength / (float) i)); for (int dx = -i; dx <= i; dx += i) for (int dy = -i; dy <= i; dy += i) g.drawFittedText(s, r.translated(dx, dy), j, lines); }
    g.setColour(c); g.drawFittedText(s, r, j, lines);
}
// a glass panel: dark translucent body, a vertical sheen, a thin glowing edge (green by default)
inline void glassPanel(juce::Graphics& g, juce::Rectangle<float> r, float rad = 14.0f, juce::Colour edge = look::green, float edgeStrength = 1.0f, float bodyAlpha = 0.62f) {
    glowRect(g, r, rad, edge, 0.55f * edgeStrength, 3, 2.0f);
    g.setColour(look::glass.withAlpha(bodyAlpha)); g.fillRoundedRectangle(r, rad);
    { juce::ColourGradient sheen(look::white.withAlpha(0.05f), r.getX(), r.getY(), look::white.withAlpha(0.0f), r.getX(), r.getBottom(), false); g.setGradientFill(sheen); g.fillRoundedRectangle(r, rad); }
    g.setColour(edge.withAlpha(0.55f * edgeStrength)); g.drawRoundedRectangle(r.reduced(0.5f), rad, 1.0f);
    g.setColour(look::white.withAlpha(0.06f)); g.drawRoundedRectangle(r.reduced(1.5f), rad - 1.0f, 0.8f);
}
// a dark-glass pill (a button body); `on` = bright green fill with bloom
inline void glassPill(juce::Graphics& g, juce::Rectangle<float> r, bool on, bool over, bool enabled, juce::Colour accent = look::green, float rad = -1.0f) {
    if (rad < 0) rad = juce::jmin(12.0f, r.getHeight() * 0.5f);
    if (on) { glowRect(g, r, rad, accent, enabled ? 1.0f : 0.4f, 4, 2.2f);
        juce::ColourGradient f(accent.brighter(0.15f), r.getX(), r.getY(), accent.darker(0.15f), r.getX(), r.getBottom(), false); g.setGradientFill(f); g.fillRoundedRectangle(r, rad);
        if (!enabled) { g.setColour(look::bg.withAlpha(0.45f)); g.fillRoundedRectangle(r, rad); }
        g.setColour(look::white.withAlpha(0.35f)); g.drawRoundedRectangle(r.reduced(0.5f), rad, 1.0f); return; }
    g.setColour((over && enabled ? look::cellHi : look::cell).withAlpha(0.88f)); g.fillRoundedRectangle(r, rad);
    { juce::ColourGradient sheen(look::white.withAlpha(0.06f), r.getX(), r.getY(), look::white.withAlpha(0.0f), r.getX(), r.getBottom(), false); g.setGradientFill(sheen); g.fillRoundedRectangle(r, rad); }
    g.setColour(accent.withAlpha(enabled ? (over ? 0.85f : 0.55f) : 0.18f)); g.drawRoundedRectangle(r.reduced(0.5f), rad, 1.0f);
}

// ---------------------------------------------------------------- the backdrop (wisps + streaks cached; particles + ghosts animated)
struct Backdrop {
    struct Particle { float x, y, r, speed, phase, drift; bool teal; };
    struct Ghost { float x, y, size, speed, sway, phase, alpha; };
    std::vector<Particle> particles; std::vector<Ghost> ghosts; juce::Image wisps; int cachedW = 0, cachedH = 0; juce::Random rng { 7 };
    Backdrop() { for (int i = 0; i < 70; ++i) particles.push_back({ rng.nextFloat(), rng.nextFloat(), 0.8f + rng.nextFloat() * 1.6f, 0.004f + rng.nextFloat() * 0.012f, rng.nextFloat() * 6.28f, (rng.nextFloat() - 0.5f) * 0.01f, rng.nextBool() && rng.nextBool() });
        const float gx[] { 0.86f, 0.08f, 0.62f, 0.93f, 0.30f, 0.72f }, gy[] { 0.12f, 0.80f, 0.86f, 0.55f, 0.95f, 0.08f }, gs[] { 54, 90, 40, 30, 70, 36 };
        for (int i = 0; i < 6; ++i) ghosts.push_back({ gx[i], gy[i], gs[i], 0.005f + 0.003f * (float) i, 0.012f, (float) i * 1.7f, 0.12f + 0.06f * (float) (i % 2) }); }
    // the static layer: soft ribbons of green / teal light, long thin streaks, a few bright flecks (own fake blur: wide strokes at low alpha)
    void renderWisps(int w, int h) {
        wisps = juce::Image(juce::Image::ARGB, juce::jmax(1, w), juce::jmax(1, h), true); juce::Graphics g(wisps); juce::Random r(12);
        auto ribbon = [&](juce::Point<float> a, juce::Point<float> c1, juce::Point<float> c2, juce::Point<float> b, juce::Colour col, float width, float alpha) {
            juce::Path p; p.startNewSubPath(a); p.cubicTo(c1, c2, b);
            for (int i = 6; i >= 1; --i) { g.setColour(col.withAlpha(alpha * 0.22f / (float) i)); g.strokePath(p, juce::PathStrokeType(width * (float) i * 0.9f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); }
            g.setColour(col.brighter(0.5f).withAlpha(alpha * 0.55f)); g.strokePath(p, juce::PathStrokeType(width * 0.35f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); };
        const float W = (float) w, H = (float) h;
        ribbon({ W * 0.55f, H * 1.05f }, { W * 0.75f, H * 0.75f }, { W * 0.70f, H * 0.45f }, { W * 1.05f, H * 0.20f }, look::green, 11.0f, 0.85f);
        ribbon({ W * 0.62f, H * 1.02f }, { W * 0.86f, H * 0.80f }, { W * 0.82f, H * 0.50f }, { W * 1.02f, H * 0.34f }, look::teal, 6.0f, 0.60f);
        ribbon({ W * 0.70f, H * 1.04f }, { W * 0.80f, H * 0.90f }, { W * 0.92f, H * 0.70f }, { W * 1.04f, H * 0.62f }, look::green, 5.0f, 0.45f);
        ribbon({ -W * 0.05f, H * 0.62f }, { W * 0.12f, H * 0.52f }, { W * 0.05f, H * 0.30f }, { W * 0.24f, H * 0.18f }, look::green, 7.0f, 0.55f);
        ribbon({ W * 0.30f, -H * 0.02f }, { W * 0.42f, H * 0.12f }, { W * 0.62f, H * 0.04f }, { W * 0.80f, H * 0.16f }, look::teal, 4.0f, 0.32f);
        ribbon({ W * 0.05f, H * 1.02f }, { W * 0.20f, H * 0.92f }, { W * 0.35f, H * 1.00f }, { W * 0.48f, H * 0.90f }, look::green, 8.0f, 0.45f);
        ribbon({ -W * 0.02f, H * 0.95f }, { W * 0.10f, H * 0.80f }, { W * 0.06f, H * 0.72f }, { W * 0.18f, H * 0.60f }, look::teal, 4.0f, 0.35f);
        // streaks: long thin diagonal lines of light
        for (int i = 0; i < 26; ++i) { const float x = r.nextFloat() * W, y = r.nextFloat() * H, len = 60.0f + r.nextFloat() * 220.0f, ang = -0.5f + r.nextFloat() * 0.3f;
            const juce::Point<float> a(x, y), b(x + std::cos(ang) * len, y + std::sin(ang) * len); const auto col = (r.nextBool() ? look::green : look::teal).withAlpha(0.05f + r.nextFloat() * 0.07f);
            juce::ColourGradient gr(col.withAlpha(0.0f), a.x, a.y, col.withAlpha(0.0f), b.x, b.y, false); gr.addColour(0.5, col); g.setGradientFill(gr); g.drawLine({ a, b }, 1.0f + r.nextFloat()); }
        // flecks
        for (int i = 0; i < 90; ++i) { const float x = r.nextFloat() * W, y = r.nextFloat() * H, rad = 0.6f + r.nextFloat() * 1.4f; g.setColour((r.nextBool() ? look::greenGlow : look::cyan).withAlpha(0.08f + r.nextFloat() * 0.18f)); g.fillEllipse(x, y, rad, rad); }
        cachedW = w; cachedH = h;
    }
    static void drawGhostShape(juce::Graphics& g, float cx, float cy, float s, float phase, float alpha, juce::Colour body) {
        // a sheet ghost: round head, straight sides, a wavy hem that ripples with the phase; two dark eyes. Drawn with a soft glow.
        juce::Path p; const float w = s, h = s * 1.35f, x0 = cx - w * 0.5f, y0 = cy - h * 0.5f;
        p.startNewSubPath(x0, y0 + w * 0.5f); p.addCentredArc(cx, y0 + w * 0.5f, w * 0.5f, w * 0.5f, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, false);
        p.lineTo(x0 + w, y0 + h * 0.82f); const int waves = 4;
        for (int i = waves; i >= 0; --i) { const float x = x0 + w * (float) i / (float) waves, dip = (i % 2 ? 0.0f : 1.0f) * s * (0.10f + 0.04f * std::sin(phase * 2.0f + (float) i)); if (i == waves) p.lineTo(x, y0 + h * 0.82f + dip); else p.quadraticTo(x + w * 0.5f / (float) waves, y0 + h * 0.90f + (i % 2 ? -dip : dip) * 0.5f, x, y0 + h * 0.82f + dip); }
        p.closeSubPath();
        for (int i = 3; i >= 1; --i) { g.setColour(body.withAlpha(alpha * 0.25f / (float) i)); g.strokePath(p, juce::PathStrokeType(s * 0.06f * (float) i)); }
        g.setColour(body.withAlpha(alpha)); g.fillPath(p);
        g.setColour(look::bg.withAlpha(juce::jmin(1.0f, alpha * 2.5f))); const float ex = s * 0.16f, ey = s * 0.2f; g.fillEllipse(cx - s * 0.22f - ex * 0.5f, y0 + w * 0.45f, ex, ey); g.fillEllipse(cx + s * 0.22f - ex * 0.5f, y0 + w * 0.45f, ex, ey);
    }
    void step(float dt) {   // (only while GHOST ANIMATION is on)
        for (auto& p : particles) { p.y -= p.speed * dt; p.x += p.drift * dt + std::sin(p.phase + p.y * 9.0f) * 0.0006f; p.phase += dt * 1.3f; if (p.y < -0.02f) { p.y = 1.02f; p.x = rng.nextFloat(); } if (p.x < -0.02f) p.x = 1.02f; if (p.x > 1.02f) p.x = -0.02f; }
        for (auto& gh : ghosts) { gh.y -= gh.speed * dt; gh.phase += dt; gh.x += std::sin(gh.phase * 0.5f) * gh.sway * dt; if (gh.y < -0.15f) { gh.y = 1.15f; gh.x = 0.05f + rng.nextFloat() * 0.9f; } }
    }
    void paint(juce::Graphics& g, juce::Rectangle<int> b, float phase) {
        // (the wisps are soft: rendered at most 1100 px wide and stretched, so a live resize stays smooth)
        const int rw = juce::jmin(1100, b.getWidth()), rh = juce::jmax(1, b.getHeight() * rw / juce::jmax(1, b.getWidth()));
        if (wisps.isNull() || cachedW != rw || cachedH != rh) renderWisps(rw, rh);
        g.fillAll(look::bg); g.drawImage(wisps, b.toFloat(), juce::RectanglePlacement::stretchToFit, false);
        const float W = (float) b.getWidth(), H = (float) b.getHeight();
        for (auto& gh : ghosts) drawGhostShape(g, b.getX() + gh.x * W, b.getY() + gh.y * H, gh.size, gh.phase + phase * 0.3f, gh.alpha, look::greenGlow.interpolatedWith(look::white, 0.6f));
        for (auto& p : particles) { const float tw = 0.5f + 0.5f * std::sin(p.phase * 2.0f + phase); const auto c = (p.teal ? look::cyan : look::greenGlow).withAlpha(0.18f + 0.5f * tw);
            const float x = b.getX() + p.x * W, y = b.getY() + p.y * H; g.setColour(c.withAlpha(c.getFloatAlpha() * 0.25f)); g.fillEllipse(x - p.r * 2.0f, y - p.r * 2.0f, p.r * 4.0f, p.r * 4.0f); g.setColour(c); g.fillEllipse(x - p.r, y - p.r, p.r * 2.0f, p.r * 2.0f); }
    }
};

// ---------------------------------------------------------------- the logo: a glowing white / cyan ghost with a green glow and trailing wisps
inline void drawLogoGhost(juce::Graphics& g, juce::Rectangle<float> r, float phase, bool animate) {
    const float s = juce::jmin(r.getWidth(), r.getHeight()) * 0.62f, cx = r.getCentreX() - r.getWidth() * 0.04f, cy = r.getCentreY() - r.getHeight() * 0.10f + (animate ? std::sin(phase * 1.4f) * r.getHeight() * 0.02f : 0.0f);
    // trailing wisps under the hem
    for (int i = 0; i < 3; ++i) { juce::Path p; const float x = cx - s * 0.3f + s * 0.3f * (float) i, y = cy + s * 0.6f; p.startNewSubPath(x, y);
        p.cubicTo(x - s * 0.25f, y + s * 0.3f + std::sin(phase + (float) i) * s * 0.06f, x + s * 0.2f, y + s * 0.55f, x - s * 0.15f, y + s * (0.85f + 0.1f * (float) i));
        for (int k = 3; k >= 1; --k) { g.setColour(look::green.withAlpha(0.10f / (float) k)); g.strokePath(p, juce::PathStrokeType(2.0f * (float) k, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); }
        g.setColour(look::greenGlow.withAlpha(0.45f)); g.strokePath(p, juce::PathStrokeType(1.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); }
    // green glow halo
    const float pulse = animate ? 0.85f + 0.15f * std::sin(phase * 2.0f) : 1.0f;
    for (int i = 5; i >= 1; --i) { g.setColour(look::green.withAlpha(0.09f * pulse / (float) i)); g.fillEllipse(juce::Rectangle<float>(s * (1.0f + 0.22f * (float) i), s * (1.3f + 0.22f * (float) i)).withCentre({ cx, cy })); }
    // the ghost body: white with a cyan tint, soft cyan rim
    juce::Path p; const float w = s, h = s * 1.3f, x0 = cx - w * 0.5f, y0 = cy - h * 0.5f;
    p.startNewSubPath(x0, y0 + w * 0.5f); p.addCentredArc(cx, y0 + w * 0.5f, w * 0.5f, w * 0.5f, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, false);
    p.lineTo(x0 + w, y0 + h * 0.80f); p.quadraticTo(x0 + w * 0.85f, y0 + h * 1.00f, x0 + w * 0.70f, y0 + h * 0.86f); p.quadraticTo(x0 + w * 0.55f, y0 + h * 1.02f, x0 + w * 0.42f, y0 + h * 0.88f); p.quadraticTo(x0 + w * 0.25f, y0 + h * 1.02f, x0 + w * 0.12f, y0 + h * 0.84f); p.quadraticTo(x0 + w * 0.02f, y0 + h * 0.92f, x0, y0 + h * 0.80f); p.closeSubPath();
    for (int i = 3; i >= 1; --i) { g.setColour(look::cyan.withAlpha(0.18f / (float) i)); g.strokePath(p, juce::PathStrokeType(2.2f * (float) i)); }
    { juce::ColourGradient body(look::white, cx, y0, look::cyan.interpolatedWith(look::white, 0.55f), cx, y0 + h, false); g.setGradientFill(body); g.fillPath(p); }
    g.setColour(look::cyan.withAlpha(0.8f)); g.strokePath(p, juce::PathStrokeType(1.0f));
    // eyes (dark, friendly) + a tiny mouth
    g.setColour(juce::Colour(0xff1a2430)); const float ex = s * 0.15f, ey = s * 0.19f; g.fillEllipse(cx - s * 0.2f - ex * 0.5f, y0 + w * 0.42f, ex, ey); g.fillEllipse(cx + s * 0.2f - ex * 0.5f, y0 + w * 0.42f, ex, ey);
    g.setColour(look::white.withAlpha(0.9f)); g.fillEllipse(cx - s * 0.2f - ex * 0.15f, y0 + w * 0.46f, ex * 0.3f, ey * 0.25f); g.fillEllipse(cx + s * 0.2f - ex * 0.15f, y0 + w * 0.46f, ex * 0.3f, ey * 0.25f);
    g.setColour(juce::Colour(0xff1a2430).withAlpha(0.7f)); g.fillEllipse(cx - s * 0.06f, y0 + w * 0.70f, s * 0.12f, s * 0.07f);
}

// ---------------------------------------------------------------- small controls
struct Pill : juce::Button {   // a dark-glass pill; `on` = bright green fill with bloom
    bool on = false, dimmed = false; float fontH = 12.0f; juce::Colour accent = look::green; int lines = 1; float bloom = 0.0f;   // bloom: extra glow rings outside (the TEACH RECORD button)
    explicit Pill(const juce::String& t) : juce::Button(t) {}
    void paintButton(juce::Graphics& g, bool over, bool down) override {
        auto r = getLocalBounds().toFloat().reduced(1.0f + bloom * 6.0f); const bool en = isEnabled() && !dimmed;
        if (bloom > 0) glowRect(g, r, juce::jmin(12.0f, r.getHeight() * 0.5f), accent, bloom, 6, 3.0f);
        glassPill(g, r, on, over, en, accent); if (down && en) { g.setColour(look::ink.withAlpha(0.10f)); g.fillRoundedRectangle(r, juce::jmin(12.0f, r.getHeight() * 0.5f)); }
        g.setFont(look::label(fontH)); const auto txt = getButtonText();
        if (on) { g.setColour(look::onGreen); g.drawFittedText(txt, getLocalBounds().reduced(4, 0), juce::Justification::centred, lines); }
        else glowText(g, txt, getLocalBounds().reduced(4, 0), juce::Justification::centred, en ? look::ink : look::faint, accent, en ? 0.5f : 0.0f);
    }
};
struct Seg : juce::Component, juce::SettableTooltipClient {   // a segmented switch: ITEM | ITEM | ITEM (each item a pill; the selected one glows)
    juce::StringArray items; int sel = 0; std::function<void(int)> onChange; float fontH = 11.0f; std::vector<bool> enabledItem; juce::Colour accent = look::green; int hover = -1;
    Seg(juce::StringArray it = {}) : items(std::move(it)) { enabledItem.assign((size_t) items.size(), true); }
    void setItems(juce::StringArray it) { items = std::move(it); enabledItem.assign((size_t) items.size(), true); repaint(); }
    void set(int i, bool notify = false) { sel = juce::jlimit(0, juce::jmax(0, items.size() - 1), i); repaint(); if (notify && onChange) onChange(sel); }
    int at(juce::Point<int> p) const { return items.isEmpty() ? -1 : juce::jlimit(0, items.size() - 1, p.x * items.size() / juce::jmax(1, getWidth())); }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat().reduced(1.0f); const float rad = juce::jmin(12.0f, r.getHeight() * 0.5f);
        g.setColour(look::cell.withAlpha(0.7f)); g.fillRoundedRectangle(r, rad); g.setColour(accent.withAlpha(0.35f)); g.drawRoundedRectangle(r.reduced(0.5f), rad, 1.0f);
        const float w = r.getWidth() / juce::jmax(1, items.size());
        for (int i = 0; i < items.size(); ++i) { auto c = juce::Rectangle<float>(r.getX() + i * w, r.getY(), w, r.getHeight()).reduced(2.0f); const bool en = enabledItem[(size_t) i];
            if (i == sel) glassPill(g, c, true, false, en, accent, rad - 2.0f); else if (i == hover && en) { g.setColour(look::cellHi.withAlpha(0.7f)); g.fillRoundedRectangle(c, rad - 2.0f); }
            g.setFont(look::label(fontH)); g.setColour(i == sel ? look::onGreen : (en ? look::dim : look::faint)); g.drawFittedText(items[i], c.toNearestInt().reduced(2, 0), juce::Justification::centred, 1); }
    }
    void mouseDown(const juce::MouseEvent& e) override { const int i = at(e.getPosition()); if (i >= 0 && enabledItem[(size_t) i] && i != sel) set(i, true); }
    void mouseMove(const juce::MouseEvent& e) override { const int i = at(e.getPosition()); if (i != hover) { hover = i; repaint(); } }
    void mouseExit(const juce::MouseEvent&) override { hover = -1; repaint(); }
};
struct Dial : juce::Component, juce::SettableTooltipClient {   // a number you drag (BPM, strength, swing): label left, value right; drag up / down, double-click = default, wheel
    juce::String name, unit; double lo = 0, hi = 1, val = 0, def = 0, step = 1; int decimals = 0; std::function<void(double)> onChange; std::function<void()> onEnd; std::function<juce::String(double)> format;
    double dragStart = 0; bool over = false;
    void set(double v, bool notify) { v = juce::jlimit(lo, hi, std::round(v / step) * step); if (v != val) { val = v; repaint(); if (notify && onChange) onChange(val); } }
    juce::String text() const { return format ? format(val) : juce::String(val, decimals) + unit; }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat().reduced(1.0f); const float rad = juce::jmin(12.0f, r.getHeight() * 0.5f); const bool en = isEnabled();
        glassPill(g, r, false, over, en);
        const float frac = (float) ((val - lo) / juce::jmax(1e-9, hi - lo)); g.setColour(look::green.withAlpha(en ? 0.16f : 0.06f)); g.fillRoundedRectangle(r.reduced(2.0f).withWidth(juce::jmax(8.0f, (r.getWidth() - 4.0f) * frac)), rad - 2.0f);
        g.setColour(en ? look::dim : look::faint); g.setFont(look::small(9.5f)); g.drawText(name, getLocalBounds().reduced(10, 0), juce::Justification::centredLeft);
        g.setFont(look::font(13.0f, true)); glowText(g, text(), getLocalBounds().reduced(10, 0), juce::Justification::centredRight, en ? look::ink : look::faint, look::green, en ? 0.6f : 0.0f);
    }
    void mouseEnter(const juce::MouseEvent&) override { over = true; repaint(); }
    void mouseExit(const juce::MouseEvent&) override { over = false; repaint(); }
    void mouseDown(const juce::MouseEvent& e) override { dragStart = val; if (e.getNumberOfClicks() >= 2) { set(def, true); if (onEnd) onEnd(); } }
    void mouseDrag(const juce::MouseEvent& e) override { const double px = (hi - lo) / 200.0; set(dragStart + (e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY()) * px, true); }
    void mouseUp(const juce::MouseEvent&) override { if (onEnd) onEnd(); }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override { set(val + (w.deltaY > 0 ? step : -step) * (std::abs(w.deltaY) > 0.3f ? 4 : 1), true); if (onEnd) onEnd(); }
};
struct Picker : juce::Component, juce::SettableTooltipClient {   // a labelled value that opens a menu (SOUND: PIANO)
    juce::String name; juce::StringArray items; int sel = 0; std::function<void(int)> onChange; bool over = false;
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat().reduced(1.0f); glassPill(g, r, false, over, isEnabled());
        g.setColour(look::dim); g.setFont(look::small(9.5f)); g.drawText(name, getLocalBounds().reduced(10, 0), juce::Justification::centredLeft);
        g.setFont(look::label(11.0f)); glowText(g, sel >= 0 && sel < items.size() ? items[sel] : juce::String(), getLocalBounds().reduced(10, 0).withTrimmedRight(10), juce::Justification::centredRight, look::ink, look::green, 0.5f);
        g.setColour(look::green.withAlpha(0.8f)); juce::Path p; const float x = (float) getWidth() - 12.0f, y = getHeight() * 0.5f; p.addTriangle(x - 3.5f, y - 2.0f, x + 3.5f, y - 2.0f, x, y + 2.5f); g.fillPath(p);
    }
    void mouseEnter(const juce::MouseEvent&) override { over = true; repaint(); }
    void mouseExit(const juce::MouseEvent&) override { over = false; repaint(); }
    void mouseDown(const juce::MouseEvent&) override {
        juce::PopupMenu m; for (int i = 0; i < items.size(); ++i) m.addItem(i + 1, items[i], true, i == sel);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [sp = juce::Component::SafePointer<Picker>(this)](int r) { if (sp && r > 0) { sp->sel = r - 1; sp->repaint(); if (sp->onChange) sp->onChange(sp->sel); } });
    }
};

// ---------------------------------------------------------------- the big record button: a thick neon ring with an electric crackle
// (green in BEATBOX; teal / cyan with concentric rings in MELODY). The crackle is stronger while recording and follows the voice.
// The window's animation timer drives it (advance): with GHOST ANIMATION off it stands still.
struct RecordButton : juce::Component, juce::SettableTooltipClient {
    struct State { bool melody = true, recording = false, busy = false, countingIn = false; int countLeft = 0; float level = 0; double seconds = 0, maxSeconds = 90; bool teach = false; };
    std::function<void()> onTap; std::function<State()> state; float phase = 0; float level = 0; int frame = 0; bool animate = true;
    glass::GlassRing ring;   // build 8: the ring is a glass tube with a little circuit world inside, lit by the recording
    RecordButton() { setMouseCursor(juce::MouseCursor::PointingHandCursor); }
    void advance(float dt) { phase += dt; ++frame; if (state) { const auto st = state(); level = level * 0.75f + st.level * 0.25f; ring.push(st.recording ? level : 0.0f); } repaint(); }
    void paint(juce::Graphics& g) override {
        const State s = state ? state() : State(); const auto r = getLocalBounds().toFloat();
        const float d = juce::jmin(r.getWidth(), r.getHeight()) - 28.0f; auto c = juce::Rectangle<float>(d, d).withCentre(r.getCentre());
        const auto acc = look::accent(s.melody), glow = look::accentGlow(s.melody); const float live = s.recording ? 0.5f + juce::jmin(1.0f, level * 4.0f) : (s.countingIn ? 0.8f : 0.35f);
        const float breathe = animate ? 0.5f + 0.5f * std::sin(phase * 2.0f) : 0.5f;
        // bloom
        for (int i = 6; i >= 1; --i) { g.setColour(acc.withAlpha(juce::jlimit(0.0f, 1.0f, (0.05f + 0.08f * live + 0.02f * breathe) / (float) i))); g.fillEllipse(c.expanded(5.0f * (float) i + live * 6.0f)); }
        // the dark glass face
        g.setColour(look::glass.withAlpha(0.85f)); g.fillEllipse(c); { juce::ColourGradient sheen(look::white.withAlpha(0.06f), c.getX(), c.getY(), look::white.withAlpha(0.0f), c.getX(), c.getBottom(), false); g.setGradientFill(sheen); g.fillEllipse(c); }
        // the ring: a thick glass tube with the living light inside (build 8, HumGlassKit.h) + concentric rings in MELODY
        const float ringW = juce::jmax(9.0f, d * 0.075f);
        glowEllipse(g, c, glow, 0.8f + live, 4, ringW * 0.6f);
        { glass::GlassRing::State gs; gs.phase = phase; gs.level = level; gs.recording = s.recording; gs.busy = s.busy; gs.countingIn = s.countingIn; gs.countLeft = s.countLeft; gs.animate = animate;
          gs.progress = (float) juce::jlimit(0.0, 1.0, s.seconds / juce::jmax(1.0, s.maxSeconds)); ring.draw(g, c, ringW, acc, glow, gs); }
        if (s.melody) { g.setColour(acc.withAlpha(0.45f)); g.drawEllipse(c.reduced(ringW * 1.6f), 1.2f); g.setColour(glow.withAlpha(0.3f)); g.drawEllipse(c.reduced(ringW * 2.5f), 1.0f); g.setColour(acc.withAlpha(0.2f)); g.drawEllipse(c.expanded(ringW * 0.85f), 1.0f); }
        // the electric crackle: short jagged bolts leaving the ring (seeded by the frame so every frame differs; still when not animating)
        { juce::Random cr((juce::int64) frame * 7919 + 17); const int bolts = s.recording ? 14 + (int) (level * 16.0f) : (s.countingIn ? 9 : 7); const float R = d * 0.5f;
          for (int b = 0; b < bolts; ++b) { const float a = cr.nextFloat() * juce::MathConstants<float>::twoPi; const float len = R * (0.08f + cr.nextFloat() * (s.recording ? 0.30f : 0.16f)) * (0.6f + live);
              juce::Path p; juce::Point<float> q = c.getCentre().getPointOnCircumference(R - ringW * 0.5f, a); p.startNewSubPath(q); const int segs = 3 + cr.nextInt(3); const bool inward = cr.nextInt(4) == 0;
              for (int k = 1; k <= segs; ++k) { const float rr = R + (inward ? -1.0f : 1.0f) * len * (float) k / (float) segs; const float ang = a + (cr.nextFloat() - 0.5f) * 0.35f; p.lineTo(c.getCentre().getPointOnCircumference(rr, ang)); }
              const float alpha = 0.35f + 0.55f * cr.nextFloat(); g.setColour(glow.withAlpha(alpha * 0.25f)); g.strokePath(p, juce::PathStrokeType(3.5f)); g.setColour(look::white.interpolatedWith(glow, 0.4f).withAlpha(alpha)); g.strokePath(p, juce::PathStrokeType(1.2f)); } }
        // (the 90 s progress is the light filling the glass ring, clockwise from the top)
        // the words
        juce::String big; if (s.countingIn) big = juce::String(juce::jmax(1, s.countLeft)); else if (s.recording) big = "TAP TO STOP"; else if (s.busy) big = "GHOST IS\nLISTENING..."; else if (s.teach) big = "TAP TO RECORD"; else big = s.melody ? "TAP TO\nRECORD" : "TAP TO\nBEATBOX";
        const float fh = s.countingIn ? d * 0.42f : juce::jmax(12.0f, d * 0.085f); g.setFont(s.countingIn ? look::font(fh, true) : look::label(fh));
        glowText(g, big, c.reduced(d * 0.16f).toNearestInt(), juce::Justification::centred, s.busy ? look::dim : look::ink, glow, s.recording ? 1.2f : 0.7f, s.countingIn ? 1 : 2);
        if (s.recording) { const int sec = (int) s.seconds; g.setFont(look::font(juce::jmax(11.0f, d * 0.07f))); g.setColour(look::ink.withAlpha(0.85f)); g.drawText(juce::String::formatted("%d:%02d", sec / 60, sec % 60), c.withTrimmedTop(d * 0.64f).toNearestInt(), juce::Justification::centredTop); }
        if (s.countingIn) { g.setFont(look::label(juce::jmax(10.0f, d * 0.06f))); g.setColour(look::dim); g.drawText("COUNT-IN", c.withTrimmedTop(d * 0.72f).toNearestInt(), juce::Justification::centredTop); }
    }
    void mouseUp(const juce::MouseEvent& e) override { if (getLocalBounds().toFloat().getCentre().getDistanceFrom(e.getPosition().toFloat()) <= getHeight() * 0.5f && onTap) onTap(); }
};

} // namespace hum::ui
