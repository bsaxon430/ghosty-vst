#pragma once
// EDIT > REPEAT (phase 11): the strip under the waveform for the selected REPEAT span, and the overlay on the waveform that draws
// the REPEAT spans (with their trigger ticks and the length handle) and the HOLD brackets (phase 12: stable-region handles and the
// held length's right edge). The overlay sits on the WaveView as a child that only takes the mouse on its own handles / tabs.
//   row 1  RATE | STRAIGHT / TRIPLET / DOTTED | RETRIG | PITCH | STEP / RAMP | REVERSE | REMOVE | x
//   row 2  FADE IN | FADE OUT | SWING | RAMP | DECAY | GATE | RATCHET + the 5-point curve
#include "PlayPanel.h"
#include "WaveView.h"

namespace gslice::ui
{
// GHOST RATCHET: 5 points (log2 rate multipliers -2 .. 2), drag a point up / down; double-click = flat
class RatchetCurve : public juce::Component
{
public:
    std::array<float, 5> pts { 0, 0, 0, 0, 0 };
    std::function<void()> onChange, onDragStart, onDragEnd;
    Palette P = palette (Theme::Dark);
    bool active = false;
    juce::Point<float> pointPos (int i) const;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    int dragging = -1;
};

class RepeatStrip : public juce::Component
{
public:
    explicit RepeatStrip (SliceProcessor&);
    void setPalette (const Palette& p);
    void show (int64 repeatId);
    int64 repeatId() const { return rid; }
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
    std::function<void()> onClose;
    std::vector<juce::Component*> controls();
    static constexpr int kHeight = 78;

    using Field = PlayPanel::Field;
    SliceProcessor& proc;
    Palette P = palette (Theme::Dark);
    juce::ComboBox rateBox, modBox, revBox;
    juce::TextButton retrigBtn { "RETRIG" }, rampModeBtn { "STEP" }, ratchetBtn { "RATCHET" }, removeBtn { "REMOVE" }, closeBtn { "X" };
    Field pitchF { "PITCH", -24, 24, 0.5, " st" }, fadeInF { "FADE IN", 0, 50, 0.1, " ms", 1 }, fadeOutF { "FADE OUT", 0, 50, 0.1, " ms", 4 },
          swingF { "SWING", 50, 75, 1, " %", 50 }, rampF { "RAMP", 0.25, 4, 0.01, "x", 1 }, decayF { "DECAY", 0, 100, 1, " %", 100 }, gateF { "GATE", 10, 100, 1, " %", 100 };
    RatchetCurve curve;
    juce::Rectangle<int> titleArea;
private:
    void change (const std::function<void (RepeatOp&)>& f, const juce::String& label);
    std::vector<Field*> fields() { return { &pitchF, &fadeInF, &fadeOutF, &swingF, &rampF, &decayF, &gateF }; }
    int64 rid = 0; bool refreshing = false;
};

class EditOverlay : public juce::Component
{
public:
    EditOverlay (SliceProcessor& p, WaveView& v) : proc (p), view (v) { setInterceptsMouseClicks (true, false); }
    void setPalette (const Palette& p) { P = p; repaint(); }
    bool hitTest (int x, int y) override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    std::function<void (int64)> onSelectRepeat;
    std::function<void (int64 regionId, int64 holdId)> onSelectHold;
    std::function<void (const juce::String&)> toast;
    int64 selRepeat = 0, selHold = 0, selHoldRegion = 0;

    // hit areas (component coordinates)
    juce::Rectangle<float> repeatHandle (const RepeatOp&) const;
    juce::Rectangle<float> repeatTab (const RepeatOp&) const;
    struct HoldHit { int64 region = 0, hold = 0; int what = 0; };   // what: 1 = stable start, 2 = stable end (manual handles), 3 = the held length's right edge, 4 = the tab
    juce::Rectangle<float> holdHandle (const Region&, const HoldOp&, int what) const;
    HoldHit holdAt (juce::Point<float> p) const;
    enum class Drag { None, RepeatLen, HoldA, HoldB, HoldLen };
    Drag drag = Drag::None;
private:
    SliceProcessor& proc; WaveView& view;
    Palette P = palette (Theme::Dark);
    int64 dragRepeat = 0, dragRegion = 0, dragHold = 0; double downHf = 0, start0 = 0, end0 = 0, len0 = 0; bool moved = false;
};
} // namespace gslice::ui
