#pragma once
// EDIT > HOLD (phase 12): the strip for the selected GHOST HOLD (the handles live on the waveform overlay, RepeatStrip.h).
//   GHOST HOLD (analysis: voiced / f0 / vibrato / unstable) | MODE | DRIFT | VARIATION | SMEAR | AUTO | REMOVE | x
#include "RepeatStrip.h"

namespace gslice::ui
{
class HoldStrip : public juce::Component
{
public:
    explicit HoldStrip (SliceProcessor&);
    void setPalette (const Palette& p);
    void show (int64 regionId, int64 holdId);
    int64 holdId() const { return hid; }
    int64 regionId() const { return rgn; }
    const HoldOp* hold() const;
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
    std::function<void()> onClose;
    std::function<void (const juce::String&)> toast;
    std::vector<juce::Component*> controls();
    static constexpr int kHeight = 44;

    using Field = PlayPanel::Field;
    SliceProcessor& proc;
    Palette P = palette (Theme::Dark);
    juce::ComboBox modeBox;
    juce::TextButton autoBtn { "AUTO" }, removeBtn { "REMOVE" }, closeBtn { "X" };
    Field driftF { "DRIFT", 0, 25, 0.5, " ct", 5 }, variationF { "VARIATION", 0.25, 2, 0.05, "x", 1 }, smearF { "SMEAR", 5, 100, 1, " %", 60 };
    juce::Rectangle<int> titleArea;
    void autoPick();                                    // re-pick the stable region inside the hold's selection
private:
    void change (const std::function<void (HoldOp&)>& f, const juce::String& label);
    int64 hid = 0, rgn = 0; bool refreshing = false;
};
} // namespace gslice::ui
