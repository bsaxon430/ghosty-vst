#pragma once
// GHOST - the panels on the rack: a lane's module, the drums module, the FX rack of a lane, the sound browser.
#include "GhostProcessor.h"
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
    juce::TextButton prev { "<" }, next { ">" }, dice { "NEXT GOOD" }, fxBtn { "FX" }, editBtn { "EDIT" }, keysBtn { "KEYS" }, muteBtn { "M" };
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

// Poltergeist 2's FX page for one lane: 4 bays (Spectral Corruptor's 38 effects) then WARP (WarpCore), in that rack's order
struct FxPanel : juce::Component, private juce::Timer
{
    FxPanel (GhostProcessor& p, int lane);
    ~FxPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void rebuild();            // after the effect in a bay changes: its knobs
    void sync();               // from the rack (automation, a loaded song)
    std::function<void()> onClose;

private:
    void timerCallback() override;
    sfx::SpectralFx& fx() { return proc.lanes[(size_t) lane].fx; }
    void set (uint32_t id, double v) { fx().setParam (id, v); proc.fxChanged (lane); }
    void pickEffect (int bay);
    GhostProcessor& proc; int lane;
    juce::TextButton close { "CLOSE" }, onBtn { "FX ON" }, warpOn { "WARP ON" }, order { "BAYS > WARP" }, fftBtn { "FFT 2048" };
    Knob mix { "MIX" }, chainMix { "BAYS MIX" };
    struct Bay
    {
        juce::TextButton effect { "EMPTY" }, on { "ON" };
        Knob mix { "MIX" };
        juce::OwnedArray<Knob> knobs;
        juce::OwnedArray<juce::TextButton> choices;   // (a choice / on-off parameter is a button that steps)
    };
    std::array<Bay, sfx::kSlots> bays;
    juce::OwnedArray<Knob> warpKnobs;
    std::array<int, sfx::kSlots> shownType { -1, -1, -1, -1 };
    uint32_t lastChange = 0;
    bool syncing = false;
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
