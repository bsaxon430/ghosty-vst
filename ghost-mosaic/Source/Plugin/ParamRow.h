// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
//
// One setting as a control: the original's own lowercase name, our plain-words name small under it, and the slider,
// number box, menu or switch that is bound to the plug-in's parameter. It is the SAME control wherever it is placed
// (UNDER THE HOOD, a view of the new mode): one parameter and one engine call, so UNDO, A / B, the DAW's automation
// and TERRAIN's MOVING NOW marker work on it everywhere. While its setting does nothing (rule 3: a search style that
// never reads it, say) the row is greyed, its control is switched off, and the tooltip says why.
#pragma once
#include "Processor.h"

namespace mosaic
{
class ParamRow : public juce::Component, public juce::SettableTooltipClient
{
public:
    enum class Style { Slider, Number, NumberInline, Combo, Check, Added };
    static constexpr int kHeight = 35;       // the height a row is designed for

    ParamRow (Processor& p, const ParamInfo& info, Style s, juce::String mainLabel, juce::String subLabel, juce::String prefix = {});

    // The row exactly as UNDER THE HOOD shows this setting: the same names and the same kind of control.
    // nullptr when there is no such setting in this version (the effect's own two in the instrument).
    static std::unique_ptr<ParamRow> make (Processor& p, const juce::String& paramId);
    static juce::String originalName (const juce::String& paramId);      // "num synpases", "fft / mfcc" ...: the original window's own words
    static const char* originalChoices (const juce::String& paramId);    // the original's own menu items (nullptr: ours)
    static Style styleFor (const juce::String& paramId);
    // The plain-words line under the original name: what the control does to the sound, in short plain words. The same
    // line wherever the control is shown (UNDER THE HOOD, the new mode's row of controls, docs/CONTROLS.md).
    static juce::String plainWords (const juce::String& paramId);

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    // Call about 30 times a second while the row is on screen (HoodPage and the new mode's page do it for every row
    // they hold): the MOVING NOW marker follows the value in use, and the row greys while its setting does nothing.
    void sync (const Engine& e);
    void syncLive (const Engine& e);
    void setIdle (const juce::String& why);  // "" = the setting does something; otherwise greyed, with this reason in the tooltip
    bool idle() const { return idleWhy.isNotEmpty(); }
    // GHOST POSTER: a row taller or shorter than kHeight is drawn at that scale (type, handles, boxes). 1 = the design size.
    void setScale (float s);
    float k = 1.0f;

    Processor& proc;
    const ParamInfo& info;
    Style style;
    juce::String mainText, subText, prefixText, idleWhy;
    juce::String idleLine;                   // while greyed: the short reason shown ON SCREEN in place of the plain-words line
    juce::Colour veil;                       // what the row sits on (a card): the greying is a veil of this colour
    int liveIdx = -1; bool liveOn = false; float liveP = -1; juce::String liveText;
    std::unique_ptr<juce::Slider> slider;
    std::unique_ptr<juce::ComboBox> combo;
    std::unique_ptr<juce::ToggleButton> toggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sa;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> ca;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> ba;

private:
    juce::String helpText() const;
    int labelW() const, prefixW() const, toggleW() const;
    juce::Rectangle<int> textArea() const;   // where the name and the plain-words line stand
    void twoLines (juce::Rectangle<int> area, juce::Rectangle<int>& nameAt, juce::Rectangle<int>& subAt) const;
    void applyTooltip();
};

// Rule 3, from the engine map (part 2, "When a control does nothing"): why this setting has no effect on the sound
// right now, in plain words; "" when it does something. It goes by the values the engine is really using (TERRAIN can
// move them). originalNames: say it with the original's lowercase names (UNDER THE HOOD, the new mode) or with ours (PLAY).
juce::String idleReason (Processor& p, const juce::String& paramId, bool originalNames = true);
// The same truth table as one short line for the screen ("NOT USED BY BASIC", "NEEDS NOVELTY ABOVE 0"); "" when it does something.
juce::String idleShort (Processor& p, const juce::String& paramId, bool originalNames = true);
}
