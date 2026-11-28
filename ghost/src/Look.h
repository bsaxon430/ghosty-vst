#pragma once
// GHOST - the hardware look: a brushed-aluminium rack unit, black knobs with cream caps and an orange line, amber
// displays, lit push buttons. Everything is drawn by code (no pictures).
#include <juce_gui_extra/juce_gui_extra.h>
#include <map>

namespace ghost::look
{
inline const juce::Colour amber { 0xffff9a2e }, amberDim { 0xff6a3a10 }, ink { 0xff24211c }, inkSoft { 0xff5a544a },
    plateTop { 0xffdcd9d2 }, plateBot { 0xffb3aea4 }, screenBg { 0xff120d08 }, red { 0xffd8341c }, green { 0xff7cff6a };

inline juce::Font label (float h = 11.0f)
{
#if JUCE_MAC
    return juce::Font (juce::FontOptions ("Avenir Next Condensed", h, juce::Font::bold)).withExtraKerningFactor (0.08f);
#else
    return juce::Font (juce::FontOptions (h, juce::Font::bold)).withExtraKerningFactor (0.08f);
#endif
}
inline juce::Font mono (float h = 14.0f)
{
#if JUCE_MAC
    return juce::Font (juce::FontOptions ("Menlo", h, juce::Font::bold));
#else
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), h, juce::Font::bold));
#endif
}
inline juce::Font wordmark (float h)
{
#if JUCE_MAC
    return juce::Font (juce::FontOptions ("Futura", h, juce::Font::bold)).withExtraKerningFactor (0.35f);
#else
    return juce::Font (juce::FontOptions (h, juce::Font::bold)).withExtraKerningFactor (0.35f);
#endif
}

// the faceplate: brushed metal with fine horizontal grain. Drawn once per size into a picture (the grain is a line per
// pixel row; drawing it on every repaint made knobs on top of it slow to move).
inline void plateRaw (juce::Graphics& g, juce::Rectangle<float> r, float corner)
{
    g.setGradientFill (juce::ColourGradient (plateTop, r.getX(), r.getY(), plateBot, r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, corner);
    juce::Random rnd (7);
    for (float y = r.getY() + 1; y < r.getBottom() - 1; y += 1.0f)
    {
        g.setColour (juce::Colours::white.withAlpha (0.025f + 0.04f * rnd.nextFloat()));
        g.drawHorizontalLine ((int) y, r.getX() + 2, r.getRight() - 2);
    }
    g.setColour (juce::Colours::white.withAlpha (0.8f)); g.drawHorizontalLine ((int) r.getY(), r.getX() + corner, r.getRight() - corner);
    g.setColour (juce::Colours::black.withAlpha (0.35f)); g.drawRoundedRectangle (r, corner, 1.0f);
}
inline void plate (juce::Graphics& g, juce::Rectangle<float> r, float corner = 4.0f)
{
    static std::map<juce::String, juce::Image> cache;
    const int w = juce::roundToInt (r.getWidth()), h = juce::roundToInt (r.getHeight());
    if (w <= 0 || h <= 0) return;
    const auto key = juce::String (w) + "x" + juce::String (h) + "/" + juce::String (corner);
    auto& img = cache[key];
    if (img.isNull())
    {
        img = juce::Image (juce::Image::ARGB, w, h, true);
        juce::Graphics ig (img);
        plateRaw (ig, { 0.0f, 0.0f, (float) w, (float) h }, corner);
    }
    g.drawImageAt (img, juce::roundToInt (r.getX()), juce::roundToInt (r.getY()));
}
inline void screw (juce::Graphics& g, juce::Point<float> c, float rad = 6.0f)
{
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffeeeeee), c.x - rad * 0.4f, c.y - rad * 0.4f, juce::Colour (0xff7d7d7d), c.x + rad, c.y + rad, true));
    g.fillEllipse (c.x - rad, c.y - rad, rad * 2, rad * 2);
    g.setColour (juce::Colours::black.withAlpha (0.45f)); g.drawEllipse (c.x - rad, c.y - rad, rad * 2, rad * 2, 0.8f);
    g.drawLine (c.x - rad * 0.6f, c.y + rad * 0.6f, c.x + rad * 0.6f, c.y - rad * 0.6f, 1.4f);
}
// a recessed black window (displays and the screen)
inline void recess (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour fill = screenBg, float corner = 4.0f)
{
    g.setColour (juce::Colours::white.withAlpha (0.55f)); g.drawRoundedRectangle (r.expanded (1.0f).translated (0, 1), corner + 1, 1.0f);
    g.setColour (fill); g.fillRoundedRectangle (r, corner);
    juce::ColourGradient sh (juce::Colours::black.withAlpha (0.8f), r.getX(), r.getY(), juce::Colours::transparentBlack, r.getX(), r.getY() + 8, false);
    g.setGradientFill (sh); g.fillRoundedRectangle (r, corner);
}
inline void engraved (juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, juce::Font f, juce::Justification j = juce::Justification::centred, juce::Colour c = ink)
{
    g.setFont (f);
    g.setColour (juce::Colours::white.withAlpha (0.65f)); g.drawText (t, r.translated (0, 1), j, false);
    g.setColour (c); g.drawText (t, r, j, false);
}
inline void glowText (juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, juce::Font f, juce::Justification j, juce::Colour c = amber)
{
    g.setFont (f);
    g.setColour (c.withAlpha (0.25f)); g.drawText (t, r.translated (0, 1), j, true); g.drawText (t, r.translated (1, 0), j, true);
    g.setColour (c); g.drawText (t, r, j, true);
}
inline void led (juce::Graphics& g, juce::Point<float> c, bool on, juce::Colour col = amber, float rad = 4.0f)
{
    if (on) { g.setColour (col.withAlpha (0.35f)); g.fillEllipse (c.x - rad * 2, c.y - rad * 2, rad * 4, rad * 4); }
    g.setColour (on ? col : col.darker (2.5f)); g.fillEllipse (c.x - rad, c.y - rad, rad * 2, rad * 2);
    g.setColour (juce::Colours::black.withAlpha (0.5f)); g.drawEllipse (c.x - rad, c.y - rad, rad * 2, rad * 2, 0.7f);
}

class Look : public juce::LookAndFeel_V4
{
public:
    Look()
    {
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1d1a16));
        setColour (juce::PopupMenu::textColourId, juce::Colour (0xffe9e3d6));
        setColour (juce::PopupMenu::highlightedBackgroundColourId, amber);
        setColour (juce::PopupMenu::highlightedTextColourId, ink);
        setColour (juce::TextEditor::backgroundColourId, screenBg);
        setColour (juce::TextEditor::textColourId, amber);
        setColour (juce::TextEditor::outlineColourId, juce::Colours::black);
        setColour (juce::TextEditor::focusedOutlineColourId, amber.withAlpha (0.6f));
        setColour (juce::CaretComponent::caretColourId, amber);
        setColour (juce::ListBox::backgroundColourId, screenBg);
        setColour (juce::ScrollBar::thumbColourId, amber.withAlpha (0.5f));
        setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff1d1a16));
        setColour (juce::TooltipWindow::textColourId, juce::Colour (0xffe9e3d6));
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) override
    {
        const bool on = s.isEnabled();
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
        const float size = juce::jmin (r.getWidth(), r.getHeight());
        auto c = r.getCentre(); auto k = juce::Rectangle<float> (size, size).withCentre (c);
        // scale ticks
        g.setColour (ink.withAlpha (on ? 0.55f : 0.2f));
        for (int i = 0; i <= 10; ++i)
        {
            const float a = a0 + (a1 - a0) * (float) i / 10.0f;
            const float r1 = size * 0.5f, r0 = r1 - (i % 5 == 0 ? 4.0f : 2.5f);
            g.drawLine (c.x + r0 * std::sin (a), c.y - r0 * std::cos (a), c.x + r1 * std::sin (a), c.y - r1 * std::cos (a), 1.0f);
        }
        auto skirt = k.reduced (size * 0.1f);
        g.setColour (juce::Colours::black.withAlpha (0.45f)); g.fillEllipse (skirt.translated (0, size * 0.05f));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a443c), skirt.getX(), skirt.getY(), juce::Colour (0xff0c0a08), skirt.getRight(), skirt.getBottom(), false));
        g.fillEllipse (skirt);
        auto cap = skirt.reduced (skirt.getWidth() * 0.2f);
        g.setGradientFill (juce::ColourGradient (on ? juce::Colour (0xfff2efe8) : juce::Colour (0xff8a8780), cap.getX(), cap.getY(), on ? juce::Colour (0xff8c877d) : juce::Colour (0xff55524c), cap.getRight(), cap.getBottom(), false));
        g.fillEllipse (cap);
        const float a = a0 + pos * (a1 - a0);
        const float p0 = skirt.getWidth() * 0.12f, p1 = skirt.getWidth() * 0.46f;
        g.setColour (on ? juce::Colour (0xffe8622c) : juce::Colour (0xff6a6660));
        g.drawLine (c.x + p0 * std::sin (a), c.y - p0 * std::cos (a), c.x + p1 * std::sin (a), c.y - p1 * std::cos (a), juce::jmax (2.0f, size * 0.06f));
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (1.0f);
        const bool lit = b.getToggleState();
        const bool on = b.isEnabled();
        const float push = down ? 1.5f : 0.0f;
        g.setColour (juce::Colours::black.withAlpha (0.55f)); g.fillRoundedRectangle (r.translated (0, 2.0f), 3.0f);
        auto face = r.translated (0, push).withTrimmedBottom (push);
        if (lit && on) g.setGradientFill (juce::ColourGradient (juce::Colour (0xffffbe62), face.getX(), face.getY(), juce::Colour (0xffe07818), face.getX(), face.getBottom(), false));
        else g.setGradientFill (juce::ColourGradient (juce::Colour (on ? 0xff3a352e : 0xff2a2724), face.getX(), face.getY(), juce::Colour (on ? 0xff1f1c18 : 0xff1a1816), face.getX(), face.getBottom(), false));
        g.fillRoundedRectangle (face, 3.0f);
        if (lit && on) { g.setColour (amber.withAlpha (0.25f)); g.drawRoundedRectangle (face.expanded (2.0f), 4.0f, 2.0f); }
        g.setColour (juce::Colours::white.withAlpha (over && on ? 0.25f : 0.12f)); g.drawHorizontalLine ((int) face.getY() + 1, face.getX() + 3, face.getRight() - 3);
    }
    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        const bool lit = b.getToggleState(), on = b.isEnabled();
        g.setFont (label (juce::jmin (13.0f, b.getHeight() * 0.55f)));
        g.setColour (! on ? juce::Colour (0xff5d5850) : lit ? ink : juce::Colour (0xffe2dccf));
        g.drawText (b.getButtonText(), b.getLocalBounds().reduced (3, 0).translated (0, b.isDown() ? 1 : 0), juce::Justification::centred, true);
    }
    juce::Font getPopupMenuFont() override { return label (15.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return label (juce::jmin (13.0f, h * 0.55f)); }
};
} // namespace ghost::look
