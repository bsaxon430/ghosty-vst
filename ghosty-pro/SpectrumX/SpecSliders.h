#pragma once
// Spectrum's slider convention (every slider / knob):
//   right-click = type a value (ghosty::TypeIn, one per editor), double-click = back to the default, hover = the value in real units.
//   Linear BARS (BarSlider): mouse-down sets the value at the click position, dragging scrubs from there, Shift = fine (1/10),
//   the mouse wheel = one step. A press-drag-release is one host gesture (one undo step where undo exists).
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>

namespace sp {

class BarSlider : public juce::Slider {
public:
    using juce::Slider::Slider;
    double wheelStep = 0;   // 0 = the interval, or 1 % of the range when there is none

    void mouseDown(const juce::MouseEvent& e) override {
        if (e.mods.isPopupMenu() || !isEnabled()) { juce::Slider::mouseDown(e); return; }   // right-click: TypeIn
        juce::Slider::mouseDown(e);                                                          // opens the host gesture
        dragging = true; x0 = e.position.x; fine = e.mods.isShiftDown();
        if (!fine) setValue(valueAt(e.position.x), juce::sendNotificationSync);             // absolute: the value under the mouse
        v0 = getValue();
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (!dragging) { juce::Slider::mouseDrag(e); return; }
        if (e.mods.isShiftDown() != fine) { fine = e.mods.isShiftDown(); x0 = e.position.x; v0 = getValue(); }   // switching keeps the value (no jump)
        const double w = juce::jmax(1.0, (double) barWidth());
        const double p0 = valueToProportionOfLength(v0), p = p0 + (e.position.x - x0) / w * (fine ? 0.1 : 1.0);
        setValue(proportionOfLengthToValue(juce::jlimit(0.0, 1.0, p)), juce::sendNotificationSync);
    }
    void mouseUp(const juce::MouseEvent& e) override { dragging = false; juce::Slider::mouseUp(e); }   // closes the gesture
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override {
        if (!isEnabled()) return;
        const double d = std::abs(w.deltaX) > std::abs(w.deltaY) ? (w.isReversed ? -w.deltaX : w.deltaX) : (w.isReversed ? -w.deltaY : w.deltaY);
        if (d == 0) return;
        const double step = wheelStep > 0 ? wheelStep : (getInterval() > 0 ? getInterval() : (getMaximum() - getMinimum()) / 100.0);
        setValue(getValue() + (d > 0 ? step : -step), juce::sendNotificationSync);
    }
    double valueAt(float x) { return proportionOfLengthToValue(juce::jlimit(0.0, 1.0, (double) x / juce::jmax(1, barWidth()))); }

private:
    int barWidth() const { return getTextBoxPosition() == juce::Slider::TextBoxRight || getTextBoxPosition() == juce::Slider::TextBoxLeft ? getWidth() - getTextBoxWidth() : getWidth(); }
    bool dragging = false, fine = false; float x0 = 0; double v0 = 0;
};

// hover / popup text in real units
inline void percentText(juce::Slider& s) { s.textFromValueFunction = [&s](double v) { return juce::String(juce::roundToInt(100.0 * (v - s.getMinimum()) / juce::jmax(1e-9, s.getMaximum() - s.getMinimum()))) + " %"; };
    s.valueFromTextFunction = [&s](const juce::String& t) { return s.getMinimum() + t.retainCharacters("0123456789.-").getDoubleValue() / 100.0 * (s.getMaximum() - s.getMinimum()); }; }
inline void panText(juce::Slider& s) { s.textFromValueFunction = [](double v) { const int p = juce::roundToInt(v * 100); return p == 0 ? juce::String("C") : juce::String(std::abs(p)) + (p < 0 ? " L" : " R"); };
    s.valueFromTextFunction = [](const juce::String& t) { const double v = t.retainCharacters("0123456789.").getDoubleValue() / 100.0; return t.containsIgnoreCase("l") ? -v : v; }; }
inline void gainDbText(juce::Slider& s) { s.textFromValueFunction = [](double v) { return v <= 1e-5 ? juce::String("-inf dB") : juce::String(juce::Decibels::gainToDecibels(v), 1) + " dB"; };
    s.valueFromTextFunction = [](const juce::String& t) { return t.containsIgnoreCase("inf") ? 0.0 : juce::Decibels::decibelsToGain(t.retainCharacters("0123456789.-").getDoubleValue()); }; }

} // namespace sp
