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
inline const juce::Colour well     { 0xff0e0e10 };   // a shade of the ground: lists, the wave, number boxes (GHOST POSTER)
inline const juce::Colour band     { 0xff26262d };   // a tint of the card: the header band of a numbered section
inline const juce::Colour purpleLo { 0xff3b2a5c };   // a shade of the purple: quiet fills behind purple things
inline const juce::Colour live     { 0xff7df3ff };   // the value in use right now while TERRAIN moves a control (the handle stays at your setting)

// one tint per source sound (the dots in the live view, the lists and the new mode share them). None of them is the
// accent purple, the acid green or the live cyan (those three mean "what you set", "go / on" and "in use now"), and
// the first nine (the built-in kit has nine sounds) are nine clearly different colours.
inline juce::Colour sourceTint (int i)
{
    static const juce::uint32 c[] = { 0xffff7a5c,     // coral
                                      0xffffc44d,     // amber
                                      0xff5c9dff,     // blue
                                      0xffff6fd8,     // pink
                                      0xff45d9a0,     // mint
                                      0xfff4ee7a,     // pale yellow
                                      0xffb9805e,     // clay
                                      0xffff9db8,     // rose
                                      0xff9fb4dc,     // slate
                                      0xffe05570,     // raspberry
                                      0xffd9b38c,     // sand
                                      0xff3fa7a0 };   // teal
    return juce::Colour (c[(size_t) ((i % 12) + 12) % 12]);
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
    juce::Label* createSliderTextBox (juce::Slider&) override;
};

// ---- GHOST POSTER (the skin): one signature shape, numbered sections, one grid
// The signature shape: a block with its top-right corner cut off. Cards, buttons, menus, switches and handles all use it.
juce::Path cutRect (juce::Rectangle<float> r, float cut);
// A control drawn bigger or smaller than its design size carries "k" (1 = design size): a tall window scales type and handles with it.
float scaleOf (const juce::Component& c);
inline void setScale (juce::Component& c, float k)
{
    c.getProperties().set ("k", (double) k);
    if (auto* box = dynamic_cast<juce::ComboBox*> (&c)) box->resized();   // (a menu keeps its type in its label: tell it now)
}
// the grid every page shares: the gutter between cards and the height of a section's header band, from the window's height
inline int gutterOf (int windowH) { return juce::jlimit (8, 16, windowH / 76); }
inline int headOf (int windowH) { return juce::jlimit (34, 58, windowH / 19); }
// A numbered section: a card with a header band (a lighter tone), its number in a solid block, its name in big type.
// lower = the original window's own lowercase names (UNDER THE HOOD); otherwise spaced capitals.
void zone (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& number, const juce::String& name, int headH, bool hot = false, bool lower = false, juce::Colour accent = purple);

// TERRAIN moving an original control (build 15): the screen shows the value in use as a second indicator that follows it.
// The component carries "liveOn" (bool) and "live" (0..1 along the control, or the choice's text in "liveText") as properties.
inline bool isLive (const juce::Component& c) { return (bool) c.getProperties()["liveOn"]; }

// a rounded card with an optional title in spaced capitals
void card (juce::Graphics& g, juce::Rectangle<float> r, bool hi = false);
void title (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> r, juce::Colour c = inkDim, float size = 12.5f);
}
