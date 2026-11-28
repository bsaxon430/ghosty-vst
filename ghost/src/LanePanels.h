#pragma once
// GHOST - the panels on the rack: a lane's module, the drums module, the FX rack of a lane, the sound browser.
#include "GhostProcessor.h"
#include "Look.h"
#include "Widgets.h"
#include "FxPanel.h"

namespace ghost
{
// an amber display with a sound name; click = the browser
struct SoundDisplay : juce::Component, juce::SettableTooltipClient
{
    juce::String top, main; bool error = false;
    std::function<void()> onClick;
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        look::recess (g, r);
        auto t = getLocalBounds().reduced (7, 3);
        look::glowText (g, top, t.removeFromTop (12), look::mono (10.0f), juce::Justification::left, look::amber.withAlpha (0.6f));
        look::glowText (g, main, t, look::mono (main.length() > 18 ? 12.0f : 14.0f), juce::Justification::left, error ? juce::Colour (0xffff5a3a) : look::amber);
    }
    void mouseUp (const juce::MouseEvent& e) override { if (onClick && e.mouseWasClicked()) onClick(); }
    void mouseEnter (const juce::MouseEvent&) override { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
};

class GhostEditor;

struct LaneModule : juce::Component, juce::SettableTooltipClient
{
    LaneModule (GhostProcessor& p, GhostEditor& ed, int lane);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();            // the sound name, the lights, what's greyed
    void tickMeters();
    GhostProcessor& proc; GhostEditor& editor; int lane;
    SoundDisplay display;
    juce::TextButton prev { "<" }, next { ">" }, dice { "NEXT GOOD" }, fxBtn { "FX" }, editBtn { "EDIT" }, keysBtn { "KEYS" }, muteBtn { "M" }, unmuteBtn { "PLAY IT" };
    juce::String feed; bool feedWarn = false;   // where this lane's notes come from (ArpEZator / Ghost Graph's part)
    Knob level { "LEVEL" }, tone { "TONE" }, fxMix { "FX MIX" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> aLevel, aTone;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> aMute;
    bool activity = false; float meter = 0;
};

struct DrumsModule : juce::Component
{
    DrumsModule (GhostProcessor& p, GhostEditor& ed);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tickMeters();
    GhostProcessor& proc; GhostEditor& editor;
    Knob level { "LEVEL" };
    juce::TextButton open { "OPEN" }, muteBtn { "M" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> aLevel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> aMute;
    float meter = 0;
};

// every sound of the three synths: search, synth, folder; a click loads it on the lane at once
struct Browser : juce::Component, private juce::ListBoxModel, private juce::Timer
{
    Browser (GhostProcessor& p, int lane);
    void paint (juce::Graphics&) override;
    void resized() override;
    std::function<void()> onClose;
    int lane;

private:
    int getNumRows() override { return (int) shown.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void returnKeyPressed (int row) override { load (row); if (onClose) onClose(); }
    void selectedRowsChanged (int row) override;
    void timerCallback() override;
    void filter();
    void load (int row);
    GhostProcessor& proc;
    juce::TextEditor search;
    juce::TextButton close { "CLOSE" }, fitOnly { "FOR THIS LANE" }, rescan { "LOOK AGAIN" };
    std::array<juce::TextButton, kEngines + 1> engineBtn;
    int engine = -1;   // -1 = all
    juce::ListBox list { "sounds", this };
    std::vector<Sound> all, shown;
    int catVersion = 0;
    juce::String status;
    bool loading = false;
};
} // namespace ghost

#include "Exporter.h"
namespace ghost
{
// a file you drag out of GHOST into FL
struct DragTile : juce::Component, juce::SettableTooltipClient
{
    juce::File file; juce::String text; bool big = false;
    DragTile (const juce::File& f, const juce::String& t, bool large = false) : file (f), text (t), big (large)
    { setMouseCursor (juce::MouseCursor::DraggingHandCursor); setTooltip ("Drag into FL: " + f.getFileName()); }
    void paint (juce::Graphics& g) override
    {
        const bool ok = file.existsAsFile();
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (juce::Colours::black.withAlpha (0.5f)); g.fillRoundedRectangle (r.translated (0, 2), 3.0f);
        g.setGradientFill (juce::ColourGradient (ok ? juce::Colour (0xffffbe62) : juce::Colour (0xff3a352e), r.getX(), r.getY(), ok ? juce::Colour (0xffe07818) : juce::Colour (0xff1f1c18), r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (ok ? look::ink : juce::Colour (0xff6d675d)); g.setFont (look::label (big ? 15.0f : 11.0f));
        g.drawText (text, getLocalBounds().reduced (4, 0), juce::Justification::centred, true);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (file.existsAsFile() && e.getDistanceFromDragStart() > 4 && ! dragging)
        {
            dragging = true;
            juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this, [this] { dragging = false; });
        }
    }
    void mouseUp (const juce::MouseEvent&) override { dragging = false; }
    bool dragging = false;
};

// SEND TO FL: make the files, then drag them into FL
struct SendPanel : juce::Component, private juce::Timer
{
    SendPanel (GhostProcessor& p, Exporter& x);
    ~SendPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    std::function<void()> onClose;
private:
    void timerCallback() override { repaint (getLocalBounds().removeFromTop (110)); }
    void rebuild();
    GhostProcessor& proc; Exporter& ex;
    juce::TextButton make { "MAKE IT" }, close { "CLOSE" }, finder { "SHOW IN FINDER" };
    std::unique_ptr<DragTile> all, mix;
    juce::Viewport view; juce::Component rows;
    juce::OwnedArray<juce::Component> rowParts;
    int shownParts = -1;
};
} // namespace ghost
