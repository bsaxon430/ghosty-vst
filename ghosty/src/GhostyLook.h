#pragma once
// Ghosty Pro's look on Vital's engine: the three looks (GHOST, BLACK, LIGHT = hand-drawn watercolor), its rounded
// cards, pill buttons and white-faced knobs with a violet arc, and the knob that drives one Vital control.
#include "JuceHeader.h"
#include "GhostThemes.h"
#include "GhostOrbit.h"
#include <functional>
#include <string>

class GhostyProcessor;

namespace ghosty {

// 2.0.16: three looks. GHOST (the main one), BLACK, and LIGHT: the hand-drawn watercolor face (pencil lines left in,
// soft washes on rice paper; 2.0.15's SKETCH, renamed). PURPLE and the old flat LIGHT were removed (Brandon 2026-11-12).
enum class Look { Ghost = 0, Black = 1, Light = 2 };
constexpr int kNumLooks = 3;
inline const char* lookName(Look l) { static const char* n[] { "GHOST", "BLACK", "LIGHT" }; return n[juce::jlimit(0, kNumLooks - 1, (int) l)]; }

struct Pal {
  Colour bgTop, bgBottom, card, cardEdge, cardShade, ink, dim, accent, accent2, accent3, accent4, knobFace, knobTrack,
         pill, pillOn, pillInk, pillOnInk, screen, wave1, wave2, keyWhite, keyBlack, good, warn;
  bool glass = false;
  bool sketch = false;   // LIGHT (the watercolor look): paper, pencil and watercolor (Watercolor.h) instead of fills and outlines
  bool neon = false;     // GHOST: near-black panels, thin neon outlines, green primary / purple secondary, edge glow
  Colour green, purple;  // the neon pair (pill tints: SAVE / GHOST green, BYPASS purple)
};
const Pal& pal();
void setLook(Look look);
Look currentLook();
// GHOST ORBIT knob colours for the current look (purple base ring, acid-green pointer / leading ticks / modulation)
ghostorbit::OrbitColours orbitColours();
// the SPIRIT BOARD's colours for the current look
const themes::Palette& boardPalette();

// a rounded card with a coloured title (the sections of every page)
void drawCard(Graphics& g, Rectangle<float> r, const String& title, Colour titleColour);
// the inset "screen" that holds a wave / envelope / LFO drawing
void drawScreen(Graphics& g, Rectangle<float> r);
void drawGlowLine(Graphics& g, const Path& p, Colour a, Colour b, Rectangle<float> r);
void paintBackground(Graphics& g, Rectangle<int> r);

class LnF : public LookAndFeel_V4 {
  public:
    LnF();
    void refresh();
    void drawRotarySlider(Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, Slider&) override;
    void drawButtonBackground(Graphics&, Button&, const Colour&, bool over, bool down) override;
    void drawButtonText(Graphics&, TextButton&, bool over, bool down) override;
    Font getTextButtonFont(TextButton&, int h) override;
    void drawComboBox(Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, ComboBox&) override;
    Font getComboBoxFont(ComboBox&) override;
    void positionComboBoxText(ComboBox&, Label&) override;
    void drawPopupMenuBackground(Graphics&, int w, int h) override;
    void drawLinearSlider(Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos, Slider::SliderStyle, Slider&) override;
    Font getLabelFont(Label&) override;
    static Font font(float h, bool bold = true);
};

// a pill button; 'on' = filled with the accent. tint: 0 plain, 1 green outline, 2 purple outline (the neon look)
class Pill : public TextButton {
  public:
    explicit Pill(const String& text = {}) : TextButton(text) { setClickingTogglesState(false); }
    void setTint(int t) { getProperties().set("tint", t); repaint(); }
    void setDropArrow(bool on) { getProperties().set("arrow", on); repaint(); }
};

// one knob bound to one Vital control: caption above, value pill below, right-click / double-click to type a value
class VKnob : public Component, private Slider::Listener {
  public:
    VKnob(GhostyProcessor& p, std::string control, String caption);
    void resized() override;
    void paint(Graphics&) override;
    void sync();                                  // read the control (timer); never fights a drag
    const std::string& control() const { return control_; }
    void setControl(std::string control);         // rebind (e.g. OSC 1 <-> OSC 2 pages)
    std::function<void(float)> onUserChange;      // after the user moved it
    ghostorbit::OrbitKnob slider;                 // GHOST ORBIT: one gesture per grab, momentum included
    static String displayText(const std::string& control, float value);

  private:
    void sliderValueChanged(Slider*) override;
    GhostyProcessor& proc_;
    std::string control_;
    String caption_;
    Label value_;
    bool setting_ = false;
};

// an on/off pill bound to a Vital switch control
class VSwitch : public Pill {
  public:
    VSwitch(GhostyProcessor& p, std::string control, String text);
    void sync();
    void setControl(std::string control) { control_ = std::move(control); sync(); }
    std::function<void(bool)> onUserChange;
  private:
    GhostyProcessor& proc_;
    std::string control_;
};

// a choice box bound to an indexed Vital control (its own names, Vital's string lookup)
class VChoice : public ComboBox {
  public:
    VChoice(GhostyProcessor& p, std::string control);
    void sync();
    void setControl(std::string control);
  private:
    void fill();
    GhostyProcessor& proc_;
    std::string control_;
    bool setting_ = false;
};

} // namespace ghosty
