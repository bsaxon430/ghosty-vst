#pragma once
#include "PluginProcessor.h"
#include <juce_audio_utils/juce_audio_utils.h>

namespace gsui {
// GhostBlock palette (same as the Ghost Money app)
const juce::Colour BG { 0xff05040a }, CARD { 0xff0b0a13 }, CARD2 { 0xff110f1c }, INK { 0xfff5f3ff }, GREY { 0xff9ca3af },
                   DIM { 0xff6b7280 }, PURPLE { 0xffa855f7 }, VIOLET { 0xff8b5cf6 }, GREEN { 0xff00ff88 }, RED { 0xfff87171 }, BLUE { 0xff60a5fa };
const juce::Colour GHOST_COL[4] = { juce::Colour(0xfff5f3ff), juce::Colour(0xff5fb24a), juce::Colour(0xff9b74d2), juce::Colour(0xff5bd7f5) };

struct LookAndFeel : juce::LookAndFeel_V4 {
    LookAndFeel();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    void drawComboBox(juce::Graphics&, int w, int h, bool down, int, int, int, int, juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override { return juce::FontOptions(13.0f, juce::Font::bold); }
    juce::Font getTextButtonFont(juce::TextButton&, int h) override { return juce::FontOptions(juce::jmin(13.5f, h * 0.48f), juce::Font::bold); }
};

// A knob with its name under it, attached to one parameter.
struct Knob : juce::Component {
    Knob(const juce::String& name, bool big = false);
    void attach(juce::AudioProcessorValueTreeState& s, const juce::String& id);
    void resized() override;
    std::unique_ptr<juce::Slider> slider;
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    bool big;
};

class GhostField;
} // namespace gsui

class PoltergeistEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit PoltergeistEditor(PoltergeistProcessor&);
    ~PoltergeistEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void selectGhost(int op);
    void doSummon(const juce::String& words);
    void refreshPresetName();

private:
    void timerCallback() override;
    PoltergeistProcessor& proc;
    gsui::LookAndFeel lnf;

    struct Content;
    std::unique_ptr<Content> content;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PoltergeistEditor)
};
