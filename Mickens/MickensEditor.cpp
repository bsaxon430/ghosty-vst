#include "MickensEditor.h"

// Mr. Mickens UI: pitch-black hardcore panel, neon rainbow trim, two animated unicorns (one throws up a rainbow,
// one poops one) and a cartoon of Mr. Mickens himself as the logo.
namespace {
constexpr int W = 1280, H = 860;
// theme colours (LIGHT is the default; DARK is the pitch-black look). setTheme() swaps them.
juce::Colour BG, CARD, CARD2, INK, DIM, BGTOP, BGBOT, WAVEBG;
void setTheme(bool dark) {
    if (dark) { BG = juce::Colour(0xff07040d); CARD = juce::Colour(0xff110a1b); CARD2 = juce::Colour(0xff1b1129); INK = juce::Colour(0xfff4eefc); DIM = juce::Colour(0xff9a8bb0);
                BGTOP = juce::Colour(0xff0d0717); BGBOT = juce::Colour(0xff050309); WAVEBG = juce::Colour(0xcc000000); }
    else      { BG = juce::Colour(0xff3a2a5a); CARD = juce::Colour(0xff4a3870); CARD2 = juce::Colour(0xff5b4783); INK = juce::Colour(0xfffaf6ff); DIM = juce::Colour(0xffd2c4ea);
                BGTOP = juce::Colour(0xff4b3775); BGBOT = juce::Colour(0xff2e2148); WAVEBG = juce::Colour(0xbf241838); }
}
const juce::Colour PINK(0xffff2bd6), CYAN(0xff22e6ff), LIME(0xffa6ff2b), GOLD(0xffffd23f), BLOOD(0xffff2650);
using APVTS = juce::AudioProcessorValueTreeState;

juce::Colour rainbow(float t, float sat = 0.9f, float bri = 1.0f) { t -= std::floor(t); return juce::Colour::fromHSV(t, sat, bri, 1.0f); }

struct LnF : juce::LookAndFeel_V4 {
    LnF() { refresh(); }
    void refresh() {
        setColour(juce::ComboBox::backgroundColourId, CARD2); setColour(juce::ComboBox::textColourId, INK); setColour(juce::ComboBox::arrowColourId, PINK);
        setColour(juce::PopupMenu::backgroundColourId, CARD2); setColour(juce::PopupMenu::textColourId, INK); setColour(juce::PopupMenu::highlightedBackgroundColourId, PINK.withAlpha(0.35f));
        setColour(juce::PopupMenu::headerTextColourId, CYAN);
        setColour(juce::TextButton::textColourOffId, INK); setColour(juce::TextButton::textColourOnId, BG); setColour(juce::Label::textColourId, INK);
        setColour(juce::TextEditor::backgroundColourId, CARD2); setColour(juce::TextEditor::textColourId, INK); setColour(juce::TextEditor::outlineColourId, PINK.withAlpha(0.4f));
        setColour(juce::BubbleComponent::backgroundColourId, CARD2); setColour(juce::BubbleComponent::outlineColourId, PINK); setColour(juce::TooltipWindow::textColourId, INK);
        setColour(juce::AlertWindow::backgroundColourId, CARD); setColour(juce::AlertWindow::textColourId, INK);
    }
    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override {
        auto r = b.getLocalBounds().toFloat().reduced(1); const bool on = b.getToggleState();
        const juce::Colour tint = b.getProperties().contains("tint") ? juce::Colour((juce::uint32) (juce::int64) b.getProperties()["tint"]) : PINK;
        g.setColour(on ? tint.withAlpha(0.9f) : CARD2); g.fillRoundedRectangle(r, 6);
        g.setColour((on ? tint.brighter(0.3f) : tint).withAlpha(over ? 1.0f : 0.5f)); g.drawRoundedRectangle(r, 6, down ? 2.0f : 1.1f);
    }
    juce::Font getTextButtonFont(juce::TextButton&, int h) override { return juce::FontOptions(juce::jmin(12.5f, h * 0.5f), juce::Font::bold); }
    void drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&) override {
        auto r = juce::Rectangle<float>(0, 0, (float) w, (float) h).reduced(1); g.setColour(CARD2); g.fillRoundedRectangle(r, 6);
        g.setColour(CYAN.withAlpha(0.45f)); g.drawRoundedRectangle(r, 6, 1.1f);
        juce::Path a; a.addTriangle((float) w - 15, h * 0.5f - 2, (float) w - 7, h * 0.5f - 2, (float) w - 11, h * 0.5f + 3); g.setColour(CYAN); g.fillPath(a);
    }
    juce::Font getComboBoxFont(juce::ComboBox&) override { return juce::FontOptions(12.0f, juce::Font::bold); }
    void positionComboBoxText(juce::ComboBox& b, juce::Label& l) override { l.setBounds(4, 1, b.getWidth() - 20, b.getHeight() - 2); l.setFont(getComboBoxFont(b)); }
    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) override {
        auto r = juce::Rectangle<float>((float) x, (float) y, (float) w, (float) h).reduced(2); const float sz = juce::jmin(r.getWidth(), r.getHeight());
        auto c = r.withSizeKeepingCentre(sz, sz); const float th = juce::jmax(3.0f, sz * 0.09f), ang = a0 + pos * (a1 - a0);
        g.setColour(CARD2); g.fillEllipse(c.reduced(th + 1));
        g.setColour(juce::Colours::black.withAlpha(0.5f)); g.drawEllipse(c.reduced(th + 1), 1.0f);
        juce::Path tr; tr.addCentredArc(c.getCentreX(), c.getCentreY(), sz / 2 - th / 2, sz / 2 - th / 2, 0, a0, a1, true);
        g.setColour(juce::Colours::white.withAlpha(0.07f)); g.strokePath(tr, juce::PathStrokeType(th, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
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
    juce::Path horn; horn.addTriangle(36, -44 + bob, 44, -46 + bob, 50, -72 + bob); g.setGradientFill(juce::ColourGradient(GOLD, 40, -44 + bob, juce::Colour(0xffff8fe0), 50, -72 + bob, false)); g.fillPath(horn);
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
struct Knob : juce::Component {
    juce::Slider s { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox }; juce::String caption;
    std::unique_ptr<APVTS::SliderAttachment> att;
    Knob(APVTS& st, const juce::String& id, const juce::String& cap) : caption(cap) {
        addAndMakeVisible(s); s.setPopupDisplayEnabled(true, true, nullptr); s.setDoubleClickReturnValue(true, (double) st.getParameter(id)->convertFrom0to1(st.getParameter(id)->getDefaultValue()));
        att = std::make_unique<APVTS::SliderAttachment>(st, id, s);
    }
    void resized() override { s.setBounds(getLocalBounds().withTrimmedBottom(13)); }
    void paint(juce::Graphics& g) override { g.setColour(DIM); g.setFont(juce::FontOptions(10.0f, juce::Font::bold)); g.drawFittedText(caption, getLocalBounds().removeFromBottom(13), juce::Justification::centred, 1); }
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
struct DragOut : juce::Component {
    juce::String text; std::function<juce::StringArray()> make; bool started = false;
    DragOut(const juce::String& t, std::function<juce::StringArray()> f) : text(t), make(std::move(f)) { setMouseCursor(juce::MouseCursor::DraggingHandCursor); }
    void paint(juce::Graphics& g) override { auto r = getLocalBounds().toFloat().reduced(1); g.setColour(LIME.withAlpha(0.13f)); g.fillRoundedRectangle(r, 6); g.setColour(LIME.withAlpha(0.85f)); g.drawRoundedRectangle(r, 6, 1.2f);
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold)); g.drawText(text, r, juce::Justification::centred); }
    void mouseDown(const juce::MouseEvent&) override { started = false; }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (started || e.getDistanceFromDragStart() < 6) return; started = true;
        auto files = make(); if (!files.isEmpty()) juce::DragAndDropContainer::performExternalDragDropOfFiles(files, false, this);
    }
};
bool isAudio(const juce::String& f) { return juce::File(f).hasFileExtension("wav;aif;aiff;mp3;flac;ogg;m4a"); }

// the break waveform of a bay: slice grid, the slice playing now, drop your own break here
struct WaveView : juce::Component, juce::FileDragAndDropTarget {
    MickensProcessor& p; int bay; const mk::Break* shown = nullptr; std::vector<float> peaks; bool hover = false; juce::String msg;
    WaveView(MickensProcessor& pp, int b) : p(pp), bay(b) {}
    void refresh() {
        auto br = p.breakAt(p.bayBreak[(size_t) bay]); if (br.get() == shown && !peaks.empty()) return; shown = br.get(); peaks.clear(); if (!br) return;
        const int cols = 280, n = (int) br->data.size(); peaks.assign(cols, 0);
        for (int c = 0; c < cols; ++c) { float m = 0; for (int i = c * n / cols; i < (c + 1) * n / cols; ++i) m = juce::jmax(m, std::abs(br->data[(size_t) i])); peaks[(size_t) c] = m; }
    }
    void paint(juce::Graphics& g) override {
        refresh(); auto r = getLocalBounds().toFloat();
        g.setColour(WAVEBG); g.fillRoundedRectangle(r, 5);
        if (!shown || peaks.empty()) { g.setColour(DIM); g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
            g.drawText(p.libReady() ? "pick a break, or drop one here" : "warming up the breaks...", r, juce::Justification::centred); if (hover) { g.setColour(LIME); g.drawRoundedRectangle(r.reduced(1), 5, 2); } return; }
        const int sl = shown->slices, cur = p.engine.uiSlice[bay].load();
        if (cur >= 0 && cur < sl && (p.engine.playing.load() || p.hostRunning.load())) { g.setColour(rainbow(cur / (float) sl).withAlpha(0.3f)); g.fillRect(r.getX() + r.getWidth() * cur / sl, r.getY(), r.getWidth() / sl, r.getHeight()); }
        g.setColour(juce::Colours::white.withAlpha(0.06f)); for (int i = 1; i < sl; ++i) g.drawVerticalLine((int) (r.getX() + r.getWidth() * i / sl), r.getY(), r.getBottom());
        g.setColour(juce::Colours::white.withAlpha(0.14f)); for (int i = 4; i < sl; i += 4) g.drawVerticalLine((int) (r.getX() + r.getWidth() * i / sl), r.getY(), r.getBottom());
        const float cy = r.getCentreY(), hh = r.getHeight() * 0.46f;
        for (int c = 0; c < (int) peaks.size(); ++c) { const float x = r.getX() + r.getWidth() * c / peaks.size(); g.setColour(rainbow(c / (float) peaks.size() * 0.9f, 0.75f)); g.drawVerticalLine((int) x, cy - peaks[(size_t) c] * hh, cy + peaks[(size_t) c] * hh); }
        g.setColour(INK.withAlpha(0.7f)); g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.drawText(juce::String(juce::roundToInt(shown->bpm)) + " BPM  |  drop a break here", r.reduced(5, 2), juce::Justification::bottomRight);
        if (msg.isNotEmpty()) { g.setColour(BLOOD); g.drawText(msg, r.reduced(5, 2), juce::Justification::topLeft); }
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

struct BayPanel : juce::Component {
    MickensProcessor& p; int b; juce::ComboBox brk, grid; WaveView wave; juce::TextButton prev { "<" }, next { ">" };
    std::unique_ptr<Knob> nudge, pitch, level, decay, pan, amt, dk, ds, dh; std::unique_ptr<Toggle> rs, rc, mute, fit;
    std::unique_ptr<APVTS::ComboBoxAttachment> gridAtt; int builtFor = -1;
    BayPanel(MickensProcessor& pp, int bay) : p(pp), b(bay), wave(pp, bay) {
        auto& st = p.apvts; const juce::String id = "b" + juce::String(b + 1) + "_";
        addAndMakeVisible(brk); addAndMakeVisible(wave); addAndMakeVisible(prev); addAndMakeVisible(next); addAndMakeVisible(grid);
        grid.addItemList({ "1/16", "1/8", "1/4", "1/2", "1 bar" }, 1); gridAtt = std::make_unique<APVTS::ComboBoxAttachment>(st, id + "grid", grid);
        grid.setTooltip("NUDGE moves the start by this much");
        auto k = [&](std::unique_ptr<Knob>& kk, const char* pid, const char* cap) { kk = std::make_unique<Knob>(st, id + pid, cap); addAndMakeVisible(*kk); };
        k(nudge, "nudge", "NUDGE"); k(pitch, "pitch", "PITCH"); k(level, "level", "LEVEL"); k(decay, "decay", "CHOP LEN"); k(pan, "pan", "PAN");
        k(amt, "rnd_amt", "CHOP AMT"); k(dk, "duck_k", "DUCK KICK"); k(ds, "duck_s", "DUCK SNARE"); k(dh, "duck_h", "DUCK HATS");
        rs = std::make_unique<Toggle>(st, id + "rnd_start", "RANDOM START", PINK); rc = std::make_unique<Toggle>(st, id + "rnd_chops", "RANDOM CHOPS", CYAN);
        mute = std::make_unique<Toggle>(st, id + "mute", "MUTE", BLOOD); fit = std::make_unique<Toggle>(st, id + "fit", "FIT TEMPO", LIME);
        rs->setTooltip("Every bar the break starts from a new random spot (on the grid)"); rc->setTooltip("Swaps 16ths for random slices; CHOP AMT says how often");
        fit->setTooltip("On: the break re-pitches to the song tempo (the classic jungle sound). Off: it keeps its own speed.");
        for (auto* t : { rs.get(), rc.get(), mute.get(), fit.get() }) addAndMakeVisible(*t);
        brk.onChange = [this] { const int id = brk.getSelectedId(); if (id > 0 && id - 1 != p.bayBreak[(size_t) b]) { p.pickBreak(b, id - 1); wave.repaint(); } };
        prev.onClick = [this] { step(-1); }; next.onClick = [this] { step(1); };
        wave.onLoaded = [this] { rebuild(); };
        rebuild();
    }
    void step(int d) { const int n = p.numBreaks(); if (n == 0) return; p.pickBreak(b, (juce::jmax(0, p.bayBreak[(size_t) b]) + d + n) % n); brk.setSelectedId(p.bayBreak[(size_t) b] + 1, juce::dontSendNotification); wave.repaint(); }
    void rebuild() {
        brk.clear(juce::dontSendNotification); juce::String fam;
        for (int i = 0; i < p.numBreaks(); ++i) { auto br = p.breakAt(i); if (!br) continue; if (br->family != fam) { fam = br->family; brk.addSectionHeading(fam + (br->user || !p.lib || i < (int) p.folderBreaks.size() || i >= (int) p.folderBreaks.size() + p.numBuiltIn() ? "" : "  (" + juce::String(juce::roundToInt(br->bpm)) + ")")); } brk.addItem(br->name, i + 1); }
        if (p.folderBreaks.empty() && p.libReady()) brk.addSectionHeading("Real breaks: put yours in the BREAKS FOLDER (button up top)");
        brk.setSelectedId(p.bayBreak[(size_t) b] + 1, juce::dontSendNotification); builtFor = p.listVersion.load();
    }
    void sync() { if (builtFor != p.listVersion.load()) rebuild(); if (brk.getSelectedId() != p.bayBreak[(size_t) b] + 1) brk.setSelectedId(p.bayBreak[(size_t) b] + 1, juce::dontSendNotification); wave.repaint(); }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat().reduced(1);
        g.setColour(CARD); g.fillRoundedRectangle(r, 10);
        g.setColour(rainbow(b / 4.0f).withAlpha(0.7f)); g.drawRoundedRectangle(r, 10, 1.5f);
        g.setColour(rainbow(b / 4.0f)); g.setFont(juce::FontOptions(15.0f, juce::Font::bold)); g.drawText("BAY " + juce::String(b + 1), 12, 8, 60, 22, juce::Justification::centredLeft);
        g.setColour(DIM); g.setFont(juce::FontOptions(9.5f, juce::Font::bold)); g.drawText("SIDECHAIN (ONLY THIS BAY DUCKS)", 10, getHeight() - 90, getWidth() - 20, 12, juce::Justification::centredLeft);
    }
    void resized() override {
        auto r = getLocalBounds().reduced(8);
        auto top = r.removeFromTop(26); top.removeFromLeft(62); next.setBounds(top.removeFromRight(24)); prev.setBounds(top.removeFromRight(24)); top.removeFromRight(4); brk.setBounds(top);
        r.removeFromTop(6); wave.setBounds(r.removeFromTop(66)); r.removeFromTop(6);
        auto row1 = r.removeFromTop(62); const int kw = row1.getWidth() / 5;
        for (auto* k : { nudge.get(), pitch.get(), level.get(), decay.get(), pan.get() }) k->setBounds(row1.removeFromLeft(kw));
        r.removeFromTop(6);
        auto row2 = r.removeFromTop(26); grid.setBounds(row2.removeFromLeft(64)); row2.removeFromLeft(4); const int bw = (row2.getWidth() - 4) / 2; rs->setBounds(row2.removeFromLeft(bw)); row2.removeFromLeft(4); rc->setBounds(row2);
        r.removeFromTop(4);
        auto row3 = r.removeFromTop(26); mute->setBounds(row3.removeFromLeft(64)); row3.removeFromLeft(4); fit->setBounds(row3.removeFromLeft(90));
        r.removeFromBottom(2); auto row4 = r.removeFromBottom(62); r.removeFromBottom(14);
        const int dw = row4.getWidth() / 4; amt->setBounds(row4.removeFromRight(dw)); for (auto* k : { dk.get(), ds.get(), dh.get() }) k->setBounds(row4.removeFromLeft(dw));
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
            if (run && s == st) { g.setColour(juce::Colours::white.withAlpha(0.35f)); g.drawRoundedRectangle(c, 4, 1.6f); }
        }
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
} // namespace

// =====================================================================================================
struct MickensEditor::Content : juce::Component {
    MickensProcessor& p; float phase = 0;
    std::unique_ptr<BayPanel> bays[mk::kBays];
    DrumGrid drums; juce::ComboBox drumSel[mk::kDrums]; std::unique_ptr<Knob> drumLvl[mk::kDrums];
    juce::TextButton patBtn[4]; juce::TextButton play { "PLAY" }, dice { "DICE STARTS" }, saveB { "SAVE" }, loadB { "LOAD" }, folderB { "BREAKS FOLDER" }, rescanB { "RESCAN" }, themeB { "DARK" };
    std::function<void()> onTheme;
    juce::ComboBox bars; DragOut dragWav, dragStems; std::unique_ptr<Knob> tempo;
    std::vector<std::unique_ptr<Knob>> fxKnobs; std::unique_ptr<Toggle> stutLatch; HoldButton stutHold { "STUTTER" }; juce::ComboBox stutDiv; std::unique_ptr<APVTS::ComboBoxAttachment> stutDivAtt;
    std::unique_ptr<juce::FileChooser> chooser; juce::TooltipWindow tips { this, 600 };
    juce::Rectangle<int> secCrunch, secSpace, secMix, secFilt, secStut, secDrums, header, uniArea;

    explicit Content(MickensProcessor& pp) : p(pp), drums(pp),
        dragWav("DRAG WAV OUT", [this] { return juce::StringArray { p.bounce(barsNow()).getFullPathName() }; }),
        dragStems("DRAG STEMS OUT", [this] { return p.bounceStems(barsNow()); }) {
        auto& st = p.apvts;
        for (int b = 0; b < mk::kBays; ++b) { bays[b] = std::make_unique<BayPanel>(p, b); addAndMakeVisible(*bays[b]); }
        addAndMakeVisible(drums); drums.onSample = [this] { syncDrums(); };
        const char* lv[] = { "kick_lvl", "snr_lvl", "hat_lvl", "oh_lvl" };
        for (int d = 0; d < mk::kDrums; ++d) {
            addAndMakeVisible(drumSel[d]); drumLvl[d] = std::make_unique<Knob>(st, lv[d], ""); addAndMakeVisible(*drumLvl[d]);
            drumSel[d].onChange = [this, d] { const auto t = drumSel[d].getText(); if (drumSel[d].getSelectedId() == 999 || t.isEmpty()) return; p.drumName[(size_t) d] = t; p.userDrum[(size_t) d] = nullptr; p.publish(); syncDrums(); };
        }
        syncDrums();
        const char* pn[] = { "TWO STEP", "HALFTIME", "ROLLER", "CLEAR" };
        for (int i = 0; i < 4; ++i) { patBtn[i].setButtonText(pn[i]); addAndMakeVisible(patBtn[i]); patBtn[i].onClick = [this, i] { p.defaultPattern(i == 3 ? 3 : i); p.publish(); drums.repaint(); }; }
        // header
        for (auto* b : { &play, &dice, &saveB, &loadB }) addAndMakeVisible(*b);
        play.getProperties().set("tint", (juce::int64) LIME.getARGB()); dice.getProperties().set("tint", (juce::int64) GOLD.getARGB());
        play.onClick = [this] { p.setPlaying(!p.engine.playing.load()); };
        dice.onClick = [this] { p.diceStarts(); }; dice.setTooltip("New start point for every bay (on each bay's grid). Hit it again for a new combo.");
        saveB.onClick = [this] { save(); }; loadB.onClick = [this] { load(); };
        for (auto* b : { &folderB, &rescanB }) { addAndMakeVisible(*b); b->getProperties().set("tint", (juce::int64) CYAN.getARGB()); }
        folderB.setTooltip("Opens Documents/Ghosty/Mr Mickens/Breaks. Put your own break WAVs there (sub-folders become sections); they show up at the top of every bay's list.");
        folderB.onClick = [this] { p.breaksFolder().startAsProcess(); };
        addAndMakeVisible(themeB); themeB.getProperties().set("tint", (juce::int64) GOLD.getARGB()); themeB.setTooltip("Switch between the light and the dark look");
        themeB.onClick = [this] { p.darkTheme = !p.darkTheme; if (onTheme) onTheme(); };
        rescanB.setTooltip("Load the Breaks folder again after you add files"); rescanB.onClick = [this] { p.rescanFolder(); };
        addAndMakeVisible(bars); bars.addItemList({ "1 BAR", "2 BARS", "4 BARS", "8 BARS" }, 1); bars.setSelectedId(3, juce::dontSendNotification);
        addAndMakeVisible(dragWav); addAndMakeVisible(dragStems);
        tempo = std::make_unique<Knob>(st, "bpm", "TEMPO"); addAndMakeVisible(*tempo); tempo->s.setTooltip("Tempo when your DAW is stopped (follows the DAW when it plays)");
        // fx
        const char* ids[] = { "crunch", "bits", "chorus", "reverb", "size", "brk_lvl", "drm_lvl", "duck_rel", "volume",
                              "dry", "res", "lp_cut", "lp_mix", "hp_cut", "hp_mix", "bp_cut", "bp_mix", "nt_cut", "nt_mix", "cb_cut", "cb_mix", "fm_vow", "fm_mix",
                              "st_cut", "st_env", "st_dec" };
        const char* caps[] = { "CRUNCH", "BITS", "CHORUS", "REVERB", "SIZE", "BREAKS", "DRUMS", "DUCK REL", "VOLUME",
                               "DRY", "RESO", "CUTOFF", "MIX", "CUTOFF", "MIX", "CUTOFF", "MIX", "CUTOFF", "MIX", "PITCH", "MIX", "VOWEL", "MIX",
                               "FILTER", "ENV AMT", "ENV DECAY" };
        for (int i = 0; i < 26; ++i) { fxKnobs.push_back(std::make_unique<Knob>(st, ids[i], caps[i])); addAndMakeVisible(*fxKnobs.back()); }
        stutLatch = std::make_unique<Toggle>(st, "stutter", "LATCH", PINK); addAndMakeVisible(*stutLatch);
        addAndMakeVisible(stutHold); stutHold.getProperties().set("tint", (juce::int64) BLOOD.getARGB());
        stutHold.onHold = [this](bool on) { if (auto* pr = p.apvts.getParameter("stutter")) pr->setValueNotifyingHost(on ? 1.0f : 0.0f); };
        stutHold.setTooltip("Hold to stutter the breaks (LATCH keeps it on)");
        addAndMakeVisible(stutDiv); stutDiv.addItemList({ "1/8", "1/16", "1/32", "1/64", "1/16T" }, 1); stutDivAtt = std::make_unique<APVTS::ComboBoxAttachment>(st, "st_div", stutDiv);
        setSize(W, H);
    }
    int barsNow() const { static const int B[] = { 1, 2, 4, 8 }; return B[juce::jlimit(0, 3, bars.getSelectedId() - 1)]; }
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
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this](const juce::FileChooser& fc) {
            auto f = fc.getResult(); if (f == juce::File()) return; f = f.withFileExtension("mickens");
            juce::MemoryBlock mb; p.getStateInformation(mb); f.replaceWithData(mb.getData(), mb.getSize()); });
    }
    void load() {
        chooser = std::make_unique<juce::FileChooser>("Load a Mr. Mickens setup", p.saveDir(), "*.mickens");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
            if (fc.getResult().existsAsFile() && p.loadSession(fc.getResult())) { for (auto& b : bays) b->rebuild(); syncDrums(); repaint(); } });
    }
    void tick() {
        phase += 1.0f / 30.0f;
        for (auto& b : bays) b->sync();
        drums.repaint();
        const bool host = p.hostRunning.load(), on = p.engine.playing.load();
        themeB.setButtonText(p.darkTheme ? "LIGHT" : "DARK");
        play.setButtonText(host ? "DAW PLAYING" : (on ? "STOP" : "PLAY")); play.setToggleState(host || on, juce::dontSendNotification);
        stutHold.latched = stutLatch->getToggleState(); stutHold.setToggleState(p.apvts.getRawParameterValue("stutter")->load() > 0.5f, juce::dontSendNotification);
        repaint(header);
    }
    void section(juce::Graphics& g, juce::Rectangle<int> r, const juce::String& t, juce::Colour c) {
        g.setColour(CARD); g.fillRoundedRectangle(r.toFloat(), 10); g.setColour(c.withAlpha(0.55f)); g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 10, 1.2f);
        g.setColour(c); g.setFont(juce::FontOptions(12.5f, juce::Font::bold)); g.drawText(t, r.reduced(12, 6).removeFromTop(16), juce::Justification::centredLeft);
    }
    void paint(juce::Graphics& g) override {
        g.setGradientFill(juce::ColourGradient(BGTOP, 0, 0, BGBOT, 0, (float) H, false)); g.fillAll();
        // star field + dark glow
        juce::Random r(3); for (int i = 0; i < 140; ++i) { const float x = r.nextFloat() * W, y = r.nextFloat() * H, tw = 0.25f + 0.25f * std::sin(phase * 2 + i);
            g.setColour(rainbow(r.nextFloat(), 0.3f).withAlpha(tw)); g.fillEllipse(x, y, 1.6f, 1.6f); }
        g.setGradientFill(juce::ColourGradient(PINK.withAlpha(0.10f), (float) W / 2, 0, juce::Colours::transparentBlack, (float) W / 2, 260, false)); g.fillRect(0, 0, W, 260);
        // header: logo, title, the unicorns
        drawLogo(g, juce::Rectangle<float>(14, 6, 104, 104), phase);
        rainbowText(g, "MR. MICKENS", { 126, 18, 420, 50 }, 46, phase * 0.15f);
        g.setColour(DIM); g.setFont(juce::FontOptions(12.0f, juce::Font::bold)); g.drawText("DARK HARDCORE BREAK MACHINE", 128, 76, 340, 14, juce::Justification::centredLeft); g.drawText("100 BREAKS  |  4 BAYS  |  DRUMS  |  SIDECHAIN", 128, 92, 340, 14, juce::Justification::centredLeft);
        {
            const auto u = uniArea.toFloat(); const float gy = u.getBottom() - 6;
            g.setColour(juce::Colours::white.withAlpha(0.06f)); g.fillRoundedRectangle(u, 12);
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
        section(g, secCrunch, "CRUNCH", BLOOD); section(g, secSpace, "SPACE", CYAN); section(g, secMix, "MIX", LIME);
        section(g, secFilt, "FILTERS IN PARALLEL  (each one's MIX blends it in next to the DRY break)", PINK); section(g, secStut, "STUTTER  (with its own filter + envelope)", GOLD);
        g.setColour(DIM); g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
        const char* fl[] = { "", "LOW-PASS", "HIGH-PASS", "BAND-PASS", "NOTCH", "COMB", "FORMANT" };
        const int fw = (secFilt.getWidth() - 20) / 7; for (int i = 1; i < 7; ++i) g.drawText(fl[i], secFilt.getX() + 10 + i * fw, secFilt.getY() + 24, fw, 12, juce::Justification::centred);
    }
    void resized() override {
        header = { 0, 0, W, 116 }; uniArea = { 560, 6, 380, 104 };
        { auto h = juce::Rectangle<int>(950, 10, W - 960, 100);
          auto r1 = h.removeFromTop(28); play.setBounds(r1.removeFromLeft(100)); r1.removeFromLeft(5); dice.setBounds(r1.removeFromLeft(100)); r1.removeFromLeft(5); saveB.setBounds(r1.removeFromLeft(56)); r1.removeFromLeft(4); loadB.setBounds(r1);
          h.removeFromTop(5); auto r3 = h.removeFromTop(26); themeB.setBounds(r3.removeFromRight(62)); r3.removeFromRight(5); rescanB.setBounds(r3.removeFromRight(70)); r3.removeFromRight(5); folderB.setBounds(r3);
          h.removeFromTop(5); auto r2 = h.removeFromTop(28); bars.setBounds(r2.removeFromLeft(82)); r2.removeFromLeft(6); dragWav.setBounds(r2.removeFromLeft(108)); r2.removeFromLeft(6); dragStems.setBounds(r2);
          tempo->setBounds(juce::Rectangle<int>(484, 30, 64, 76)); }
        const int bw = (W - 24 - 3 * 8) / 4;
        for (int b = 0; b < mk::kBays; ++b) bays[b]->setBounds(12 + b * (bw + 8), 120, bw, 370);
        // bottom: drums left, fx right
        secDrums = { 12, 500, 560, 348 };
        { auto r = secDrums.reduced(10); r.removeFromTop(24);
          auto btns = r.removeFromBottom(28); const int pw = (btns.getWidth() - 18) / 4; for (int i = 0; i < 4; ++i) { patBtn[i].setBounds(btns.removeFromLeft(pw)); btns.removeFromLeft(6); }
          r.removeFromBottom(8);
          auto names = r.removeFromLeft(150); const int lh = names.getHeight() / 4;
          for (int d = 0; d < mk::kDrums; ++d) { auto l = names.removeFromTop(lh).reduced(0, 4); drumLvl[d]->setBounds(l.removeFromRight(40).withTrimmedBottom(-12)); l.removeFromRight(4); drumSel[d].setBounds(l.withSizeKeepingCentre(l.getWidth(), 26)); }
          r.removeFromLeft(6); drums.setBounds(r); }
        const int fx0 = 580, fxW = W - 12 - fx0;
        secCrunch = { fx0, 500, 150, 108 }; secSpace = { fx0 + 158, 500, 200, 108 }; secMix = { fx0 + 366, 500, fxW - 366, 108 };
        secFilt = { fx0, 616, fxW, 140 }; secStut = { fx0, 764, fxW, 84 };
        auto place = [&](juce::Rectangle<int> sec, int from, int count) { auto r = sec.reduced(8); r.removeFromTop(20); const int w = r.getWidth() / count; for (int i = 0; i < count; ++i) fxKnobs[(size_t) (from + i)]->setBounds(r.removeFromLeft(w).withSizeKeepingCentre(juce::jmin(w, 64), juce::jmin(r.getHeight(), 76))); };
        place(secCrunch, 0, 2); place(secSpace, 2, 3); place(secMix, 5, 4);
        { auto r = secFilt.reduced(10); r.removeFromTop(26); const int fw = r.getWidth() / 7;
          for (int c = 0; c < 7; ++c) { auto col = r.removeFromLeft(fw); const int kh = col.getHeight() / 2;
              fxKnobs[(size_t) (9 + c * 2)]->setBounds(col.removeFromTop(kh).withSizeKeepingCentre(juce::jmin(fw, 54), kh)); fxKnobs[(size_t) (10 + c * 2)]->setBounds(col.withSizeKeepingCentre(juce::jmin(fw, 54), kh)); } }
        { auto r = secStut.reduced(10); r.removeFromTop(18); stutHold.setBounds(r.removeFromLeft(130).reduced(0, 6)); r.removeFromLeft(8); stutLatch->setBounds(r.removeFromLeft(70).reduced(0, 12)); r.removeFromLeft(8);
          stutDiv.setBounds(r.removeFromLeft(80).withSizeKeepingCentre(80, 26)); r.removeFromLeft(10); const int w = r.getWidth() / 3; for (int i = 0; i < 3; ++i) fxKnobs[(size_t) (23 + i)]->setBounds(r.removeFromLeft(w).withSizeKeepingCentre(juce::jmin(w, 70), r.getHeight() + 6)); }
    }
};

MickensEditor::MickensEditor(MickensProcessor& p) : juce::AudioProcessorEditor(p), proc(p) {
    shownDark = p.darkTheme; setTheme(shownDark);
    lnf = std::make_unique<LnF>(); setLookAndFeel(lnf.get());
    content = std::make_unique<Content>(p); addAndMakeVisible(*content);
    content->onTheme = [this] { setTheme(proc.darkTheme); static_cast<LnF*>(lnf.get())->refresh(); sendLookAndFeelChange(); repaint(); };
    setResizable(true, true); setResizeLimits(W * 3 / 4, H * 3 / 4, W * 2, H * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio((double) W / H);
    setSize(W, H); startTimerHz(30);
}
MickensEditor::~MickensEditor() { stopTimer(); content.reset(); setLookAndFeel(nullptr); }
void MickensEditor::paint(juce::Graphics& g) { g.fillAll(BG); }
void MickensEditor::resized() { if (content) content->setTransform(juce::AffineTransform::scale((float) getWidth() / W)); }
void MickensEditor::timerCallback() {
    if (!content) return;
    // a loaded session can switch the theme
    if (shownDark != proc.darkTheme) { shownDark = proc.darkTheme; setTheme(shownDark); static_cast<LnF*>(lnf.get())->refresh(); sendLookAndFeelChange(); repaint(); }
    content->tick();
}
