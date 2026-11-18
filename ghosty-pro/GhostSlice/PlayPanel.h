#pragma once
// The PLAY workspace (phase 10): the arrangement's slices on MIDI keys.
//   top      CHOP mode (REGIONS / EQUAL / MANUAL / TRANSIENT / SYLLABLE / NOTE / BEAT) + PARTS / SENS / DIV | SPLIT @ SLICES |
//            TURN INTO INSTRUMENT | EXPORT MIDI MAP | pad page < >
//   strip    the arrangement with its slice cuts (MANUAL: click = add a cut, drag = move, double-click = remove)
//   pads     4 x 4 pads with the slice thumbnails: click = play (held = gate), select = edit its key; drag a pad out = its WAV
//   key      PITCH / START / ATTACK / DECAY / SUSTAIN / RELEASE / GAIN / PAN / CHOKE | ONE-SHOT or GATE | REVERSE | HOLD VOWEL | REPEAT
//   global   POLY / BEND / VEL>GAIN / VEL>FILTER
//   keyboard C1 upward: mapped keys tinted, sounding keys lit; click = play
#include "SliceProcessor.h"
#include "SliceTheme.h"

namespace gslice::ui
{
class PlayPanel : public juce::Component, private juce::Timer
{
public:
    explicit PlayPanel (SliceProcessor&);
    ~PlayPanel() override;
    void setPalette (const Palette& p);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

    struct Field : juce::Component
    {
        juce::Label cap; juce::Slider s { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
        Field (const juce::String& name, double lo, double hi, double step, const juce::String& suffix, double def = 0.0);
        void resized() override { auto r = getLocalBounds(); cap.setBounds (r.removeFromLeft (capW)); s.setBounds (r); }
        int capW = 70;
    };
    // pads
    struct Pads : juce::Component
    {
        explicit Pads (PlayPanel& p) : panel (p) {}
        PlayPanel& panel;
        juce::Rectangle<float> padRect (int k) const;    // k = 0..15 on the current page
        int padAt (juce::Point<float> p) const;
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        int downPad = -1; bool dragged = false; int heldNote = -1;
    };
    struct Keys : juce::Component
    {
        explicit Keys (PlayPanel& p) : panel (p) {}
        PlayPanel& panel;
        static constexpr int kLow = 36, kHigh = 95;      // C1 .. B5
        juce::Rectangle<float> keyRect (int note) const;
        int noteAt (juce::Point<float> p) const;
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        int held = -1;
    };
    struct Strip : juce::Component
    {
        explicit Strip (PlayPanel& p) : panel (p) {}
        PlayPanel& panel;
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        double hfAt (float x) const;
        float xOf (double hf) const;
        int cutNear (float x) const;                     // MANUAL cut index within 5 px (-1)
        int dragCut = -1; bool moved = false;
    };

    // actions (buttons / tests)
    void setMode (SliceMode m);
    void turnIntoInstrument();
    void exportMappingClicked();
    bool dragPadOut (int slice);                         // the gated WAV drag of one slice
    void selectPad (int slice);
    int selectedSlice() const { return selSlice; }
    const KeySlot* selectedKey() const;
    int noteForSlice (int slice) const;                  // the lowest key playing it (-1)
    int page = 0;
    static constexpr int kPads = 16;

    SliceProcessor& proc;
    Palette P = palette (Theme::Dark);
    juce::TextButton regionsBtn { "REGIONS" }, equalBtn { "EQUAL" }, manualBtn { "MANUAL" }, instrBtn { "TURN INTO INSTRUMENT" }, exportBtn { "EXPORT MIDI MAP" },
                     prevBtn { "<" }, nextBtn { ">" }, oneShotBtn { "ONE-SHOT" }, reverseBtn { "REVERSE" }, vowelBtn { "HOLD VOWEL" };
    juce::ComboBox repeatBox;
    Field equalF { "PARTS", 2, 64, 1, "", 16 };
    // GHOST CHOP (the REGIONS / EQUAL / MANUAL buttons stay for older code paths but the mode box replaces them on screen)
    juce::ComboBox chopBox, divBox;
    Field sensF { "SENS", 0, 100, 1, " %", 50 };
    juce::TextButton splitSlicesBtn { "SPLIT @ SLICES" };
    void splitAtSlices();
    Field pitchF { "PITCH", -24, 24, 1, " st" }, startF { "START", 0, 2000, 1, " ms" }, attackF { "ATTACK", 0, 2000, 1, " ms" }, decayF { "DECAY", 0, 2000, 1, " ms" },
          sustainF { "SUSTAIN", 0, 100, 1, " %", 100 }, releaseF { "RELEASE", 0, 4000, 1, " ms", 30 }, gainF { "GAIN", -48, 12, 0.1, " dB" }, panF { "PAN", -100, 100, 1, "" },
          chokeF { "CHOKE", 0, 8, 1, "" };
    Field polyF { "POLY", 1, 32, 1, "", 16 }, bendF { "BEND", 2, 24, 1, " st", 2 }, velGainF { "VEL>GAIN", 0, 100, 1, " %", 100 }, velFiltF { "VEL>FILTER", 0, 100, 1, " %" };
    Pads pads { *this };
    Keys keys { *this };
    Strip strip { *this };
    juce::Rectangle<int> topArea, settingsArea, settingsTitle;
    std::vector<juce::Component*> controls();            // every control (layout tests)
    std::function<void (const juce::String&)> toast;
    std::unique_ptr<juce::FileChooser> chooser;
private:
    void timerCallback() override;
    void keyChanged (const std::function<void (KeySlot&)>& f, const juce::String& label);
    void globalChanged (const std::function<void (PlayMap&)>& f, const juce::String& label);
    std::vector<Field*> keyFields();
    std::vector<Field*> globalFields();
    int selSlice = 0; bool refreshing = false;
    uint64 lastBits[2] {};
    juce::String lastBank;
};
} // namespace gslice::ui
