#pragma once
// GHOST - small hardware widgets shared by GHOST and GHOST FX
#include "Look.h"

namespace ghost
{
// a knob with its name engraved under it (value shown while it's turned)
struct Knob : juce::Component
{
    juce::Slider s { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::String name;
    std::function<juce::String (double)> text;
    explicit Knob (const juce::String& n) : name (n)
    {
        addAndMakeVisible (s);
        s.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        s.setVelocityBasedMode (false); s.setMouseDragSensitivity (180);
        s.onDragStart = [this] { repaint(); }; s.onDragEnd = [this] { repaint(); };
        s.onValueChange = [this] { if (s.isMouseButtonDown()) repaint(); };
    }
    void resized() override { s.setBounds (getLocalBounds().withTrimmedBottom (13)); }
    void paint (juce::Graphics& g) override
    {
        const bool showVal = s.isMouseButtonDown() && text;
        look::engraved (g, showVal ? text (s.getValue()) : name, getLocalBounds().removeFromBottom (13), look::label (10.0f), juce::Justification::centred,
                        isEnabled() ? look::ink : look::ink.withAlpha (0.3f));
    }
    void enablementChanged() override { repaint(); }
};

} // namespace ghost
