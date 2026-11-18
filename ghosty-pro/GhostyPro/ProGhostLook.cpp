// GHOST LOOK: Ghosty Pro's 4th look - the Pro-only painters (see ProGhostLook.h). Message thread only.
#include "ProGhostLook.h"
#include "ProTheme.h"
#include "ProGlass.h"
#include "ProEditor.h"

namespace pghost
{
static juce::Font uiFont (float h) { return juce::Font (juce::FontOptions (h, juce::Font::bold)); }

void button (juce::Graphics& g, juce::Rectangle<float> r, bool on, bool over, bool down, bool enabled, int accentRole, juce::Colour pink, juce::Colour blue)
{
    const auto tint = on && accentRole == 1 ? pink : on && accentRole == 2 ? blue : gg::violetDeep();
    pill (g, r, r.getHeight() * 0.5f, on, over, down, enabled, tint);
}

void linearSlider (juce::Graphics& g, juce::Rectangle<float> r, float pos, float zeroProp, bool enabled)
{
    const float d = juce::jlimit (10.0f, 16.0f, r.getHeight() - 4.0f);
    const auto tr = juce::Rectangle<float> (r.getX(), r.getCentreY() - 3.0f, r.getWidth(), 6.0f);
    gg::track (g, tr, r.getX() + zeroProp * r.getWidth(), pos, enabled);
    gg::thumb (g, { juce::jlimit (r.getX() + d * 0.5f - 2.0f, r.getRight() - d * 0.5f + 2.0f, pos), r.getCentreY() }, d);
}

bool valueBoxLabel (juce::Graphics& g, juce::Label& l)
{
    if (dynamic_cast<juce::Slider*> (l.getParentComponent()) == nullptr) return false;
    const auto r = l.getLocalBounds().toFloat().reduced (1.0f, 0.5f);
    valueBox (g, r);
    if (! l.isBeingEdited() && ! hideTextForTests())
    {
        g.setColour (l.findColour (juce::Label::textColourId).withMultipliedAlpha (l.isEnabled() ? 1.0f : 0.8f));
        g.setFont (l.getLookAndFeel().getLabelFont (l));
        g.drawFittedText (l.getText(), r.toNearestInt().reduced (3, 0), juce::Justification::centred, 1, 0.8f);
    }
    return true;
}

bool backdropUnder (juce::Graphics& g, juce::Component& c)
{
    if (! on()) return false;
    auto* ed = c.findParentComponentOfClass<ProEditor>();
    auto* content = ed != nullptr ? ed->contentComponent() : nullptr;
    if (content == nullptr || ! content->isParentOf (&c)) { g.fillAll (pt::pal().bg); return true; }
    backdropAt (g, content->getWidth(), content->getHeight(), content->getLocalPoint (&c, juce::Point<int>()));
    return true;
}

void lookButton (juce::Graphics& g, juce::Rectangle<float> r, int nextLook, bool over, bool down)
{
    const auto& c = pt::pal();
    if (on()) pill (g, r, r.getHeight() * 0.5f, false, over, down);
    else progl::pill (g, r, r.getHeight() * 0.5f, false, c.ecto, over || down);     // (LIGHT: its glass pill, as before)
    // a swatch of the NEXT look, then its name
    const float cx = r.getX() + 13, cy = r.getCentreY();
    const juce::Rectangle<float> sw (cx - 6, cy - 6, 12, 12);
    if (nextLook == kLook)
    {   // GHOST: a drop of aurora with a violet rim
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd9c8ff), sw.getX(), sw.getY(), juce::Colour (0xffb9efe2), sw.getRight(), sw.getBottom(), false));
        g.fillEllipse (sw);
        g.setColour (juce::Colours::white.withAlpha (0.8f)); g.fillEllipse (sw.reduced (3.0f).withHeight (3.0f).translated (0, -1.0f));
        g.setColour (gg::violet()); g.drawEllipse (sw, 1.6f);
    }
    else
    {
        const auto& np = ghosty::themes::get (ghosty::themes::fromInt (nextLook));
        g.setColour (np.window); g.fillEllipse (sw);
        g.setColour (np.violet); g.drawEllipse (sw, 1.6f);
    }
    g.setColour (c.white); g.setFont (uiFont (11.0f));
    g.drawText (nextLook == kLook ? name() : ghosty::themes::displayName (ghosty::themes::fromInt (nextLook)), r.withTrimmedLeft (22), juce::Justification::centred);
}

void whiteKey (juce::Graphics& g, juce::Rectangle<float> a, bool down, bool over)
{
    auto k = a.reduced (1.0f, 0.0f).withTrimmedBottom (1.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffbfaff), 0, k.getY(), down ? juce::Colour (0xffd9cff8) : juce::Colour (0xffe9e4f6), 0, k.getBottom(), false));
    g.fillRoundedRectangle (k, 4.0f);
    if (over) { g.setColour (gg::violet().withAlpha (0.12f)); g.fillRoundedRectangle (k, 4.0f); }
    g.setColour (juce::Colours::white.withAlpha (0.9f)); g.drawLine (k.getX() + 1.5f, k.getY() + 2, k.getX() + 1.5f, k.getBottom() - 4, 1.0f);
    g.setColour (juce::Colour (0x40463e6c)); g.drawRoundedRectangle (k, 4.0f, 0.8f);
}
void blackKey (juce::Graphics& g, juce::Rectangle<float> a, bool down, bool over)
{
    auto k = a.withTrimmedBottom (1.0f);
    gg::shadow (g, k, 3.0f, 2.5f, 1.2f, 3);
    g.setGradientFill (juce::ColourGradient (down ? juce::Colour (0xff6d4ae0) : juce::Colour (0xff4a3a8e), 0, k.getY(), juce::Colour (0xff2c2360), 0, k.getBottom(), false));
    g.fillRoundedRectangle (k, 3.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (over ? 0.45f : 0.32f), 0, k.getY(), juce::Colours::white.withAlpha (0.0f), 0, k.getY() + k.getHeight() * 0.55f, false));
    g.fillRoundedRectangle (k.reduced (2.0f, 1.5f).withHeight (k.getHeight() * 0.55f), 2.0f);
    g.setColour (juce::Colour (0x80a59cff)); g.drawRoundedRectangle (k.reduced (0.5f), 3.0f, 0.8f);
}

void bypassPill (juce::Graphics& g, juce::Rectangle<float> r, bool isOn, bool over, bool down)
{
    pill (g, r.reduced (1.0f), r.getHeight() * 0.5f, isOn, over, down, true, juce::Colour (0xff8a4a00));    // bypassed = deep amber glass
    g.setColour (isOn ? juce::Colours::white : gg::ink());
    g.setFont (uiFont (juce::jmin (14.0f, r.getHeight() * 0.53f)));
    g.drawText (isOn ? "BYPASSED" : "BYPASS", r, juce::Justification::centred);
}
} // namespace pghost
