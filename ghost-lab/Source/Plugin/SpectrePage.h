#pragma once
// SPECTRE's page (build 17), the page Ghost Lab opens on. Its own layout (not a copy of any other plug-in's window):
//   top      the title, what it does, and the buttons: LIVE (Ghost Lab FX: the track's own audio through it as it plays),
//            LISTEN (your sound round and round through it), PRINT IT (into the lab's history), RESET
//   left     SHIFT and TRANSPOSE: where the partials go
//   middle   the rebuilt sound as it is made (the scope), and the pad: PHASE across, CEPSTRUM up and down, with the
//            partials themselves drawn behind it as they live
//   right    CLOUD and SMOOTH (how they move), then INF, MID and SIDE, PARTIALS and OUTPUT
//   bottom   one line in plain words about whatever your hand is on
// A control that does nothing right now is greyed (SIDE on a mono sound; CLOUD and SMOOTH while INF holds everything).
// AGPL-3.0-only with the rest of Ghost Lab (see LICENSE, NOTICE.md).
#include "../Core/Engine.h"
#include "../Core/Spectre.h"
#include "Theme.h"

namespace glab
{
class Editor;

class SpectrePage : public juce::Component, private juce::Timer
{
public:
    explicit SpectrePage (Editor&);
    ~SpectrePage() override;

    static juce::Colour accent();          // SPECTRE's own colour on the strip of rooms
    void paint (juce::Graphics&) override;
    void resized() override;
    void soundChanged();                   // the lab's sound changed (what is greyed may change)
    void visibilityChanged() override;

    // ---- what the tests (and the page's own parts) press
    float value (int param) const;
    void setValue (int param, float v);    // as if dragged there
    bool controlEnabled (int param) const;
    juce::String greyReason (int param) const;   // why a greyed control does nothing right now
    void pressListen();
    void pressPrint();
    void pressLive();                      // Ghost Lab FX only
    void pressReset();
    void stepTable (int dir);              // WAVETABLE: the next / previous built-in one
    void chooseTable();                    // WAVETABLE: a file of yours
    bool loadTable (const juce::File&);
    static constexpr int kWaveHover = 100;
    bool listening() const;
    juce::String caption() const { return captionText; }
    class Control;

private:
    class Pad; class Scope; class Button; class WaveBox;
    Editor& ed;
    std::unique_ptr<Pad> pad;
    std::unique_ptr<Scope> scope;
    std::unique_ptr<WaveBox> wave;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::OwnedArray<Control> controls;     // the sliders and knobs (not the pad)
    std::unique_ptr<Button> liveBtn, listenBtn, printBtn, resetBtn, freeBtn, snapBtn, linkBtn;
    juce::OwnedArray<Button> modeBtns;
    juce::Rectangle<float> modeLabelR;
    juce::Rectangle<float> titleR, doesR, captionR, leftR, rightR, knobR, mainR;
    juce::String captionText;
    int hoverParam = -1;
    void timerCallback() override;
    void refreshEnabled();
    void setHover (int param);
    Control* controlFor (int param) const;
    friend class Pad; friend class Control; friend class Scope; friend class WaveBox;
};
}
