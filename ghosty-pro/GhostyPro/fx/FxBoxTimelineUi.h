#pragma once
// GHOST MOTION timeline view: the small interface the MOTION page's TIMELINE view offers outside FxPanel.cpp (the window's tests find
// the view by it: contrast checks, tile drops). Ghost Box only.
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace gpfx {
struct TimelineUiApi
{
    virtual ~TimelineUiApi() = default;
    struct Text { juce::Rectangle<int> r; juce::Colour c; juce::String what; };   // where the view paints a text (its coordinates), in which colour
    virtual std::vector<Text> uiTexts() const = 0;
    virtual bool dropUnit (int unit, juce::Point<int> screenPos) = 0;              // an effect tile dropped at this screen position
    virtual juce::Component& editorComponent() = 0;                                 // the loop editor
    virtual void select (int lane) = 0;
    virtual int selected() const = 0;
};
} // namespace gpfx
