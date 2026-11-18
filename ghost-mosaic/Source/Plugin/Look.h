// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
//
// The GhostBlock look (the Ghost Lab palette): charcoal ground, electric purple, acid green,
// Poppins (SIL OFL 1.1, embedded unmodified).
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace mosaic::look
{
// ---- palette
inline const juce::Colour ground   { 0xff141416 };   // the page
inline const juce::Colour panel    { 0xff1b1b1f };   // cards
inline const juce::Colour panelHi  { 0xff232329 };
inline const juce::Colour line     { 0xff2e2e36 };   // card edges, tracks
inline const juce::Colour ink      { 0xffefeff3 };   // text
inline const juce::Colour inkDim   { 0xff9a9aa6 };
inline const juce::Colour inkFaint { 0xff5c5c68 };
inline const juce::Colour purple   { 0xffa66bff };   // electric purple
inline const juce::Colour green    { 0xffa6f23a };   // acid green
inline const juce::Colour danger   { 0xffff5a6e };
inline const juce::Colour live     { 0xff7df3ff };   // the value in use right now while TERRAIN moves a control (the handle stays at your setting)

// one tint per source sound (the dots in the live view and the list share them)
inline juce::Colour sourceTint (int i)
{
    static const juce::uint32 c[] = { 0xffa66bff, 0xffa6f23a, 0xff6be3ff, 0xffff7ad9, 0xffffc861, 0xff7d8bff, 0xff5cf2b5, 0xffff9a6b };
    return juce::Colour (c[(size_t) ((i % 8) + 8) % 8]);
}

juce::Font font (float height, int weight = 1);   // 0 medium, 1 semibold, 2 bold
juce::Font spaced (float height, int weight = 1, float tracking = 0.12f);

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int h) override;
    juce::Font getLabelFont (juce::Label&) override;
    void drawTooltip (juce::Graphics&, const juce::String&, int w, int h) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String&, juce::Point<int>, juce::Rectangle<int>) override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart, int thumbSize, bool over, bool down) override;
};

// TERRAIN moving an original control (build 15): the screen shows the value in use as a second indicator that follows it.
// The component carries "liveOn" (bool) and "live" (0..1 along the control, or the choice's text in "liveText") as properties.
inline bool isLive (const juce::Component& c) { return (bool) c.getProperties()["liveOn"]; }

// a rounded card with an optional title in spaced capitals
void card (juce::Graphics& g, juce::Rectangle<float> r, bool hi = false);
void title (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> r, juce::Colour c = inkDim, float size = 12.5f);
}
