#pragma once
#include "GhostProcessor.h"
#include "LanePanels.h"
#include "Look.h"

namespace ghost
{
// the window a hosted plug-in's own editor lives in, inside GHOST's screen (ArpEZator, Ghosty Beats)
struct HostedView : juce::Component, private juce::ComponentListener
{
    HostedView() = default;
    ~HostedView() override { clear(); }
    void show (juce::AudioPluginInstance* p, const juce::String& missing);
    void clear();
    void resized() override;
    void paint (juce::Graphics&) override;
    juce::AudioPluginInstance* showing = nullptr;
private:
    void componentMovedOrResized (juce::Component&, bool, bool wasResized) override { if (wasResized && ! placing) place(); }
    void place();
    std::unique_ptr<juce::AudioProcessorEditor> ed;
    juce::String message;
    bool placing = false;
};

// a lane's synth in its own window (the synth's own editor, its own size)
struct SynthWindow : juce::DocumentWindow
{
    SynthWindow (const juce::String& title, juce::AudioPluginInstance& p, std::function<void()> closed);
    ~SynthWindow() override;
    void closeButtonPressed() override { if (onClosed) onClosed(); }
    std::function<void()> onClosed;
};

class GhostEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    enum Page { PageArp = 0, PageBeats, PageGraph };
    explicit GhostEditor (GhostProcessor&);
    ~GhostEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    void showPage (Page p);
    void showBrowser (int lane);
    void showFx (int lane);
    void openSynthEditor (int lane);
    void closeSynthEditor (int lane);
    void refreshAll();
    void setStatus (const juce::String& s) { status = s; statusUntil = juce::Time::getMillisecondCounter() + 5000; repaint (topBar); }

private:
    void timerCallback() override;
    void closeOverlays();
    void renderBackground();
    juce::Image bg;
    juce::String shownOpening;
    GhostProcessor& proc;
    look::Look lnf;
    juce::TooltipWindow tips { this, 600 };
    Page page = PageArp;
    juce::TextButton arpBtn { "ARPEZ" }, beatsBtn { "BEATS" }, graphBtn { "GRAPH" }, playBtn { "PLAY" };
    Knob master { "MASTER" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> aMaster;
    HostedView screen;
    std::unique_ptr<Browser> browser;
    std::unique_ptr<FxPanel> fxPanel;
    juce::OwnedArray<LaneModule> laneMods;
    DrumsModule drums;
    std::array<std::unique_ptr<SynthWindow>, kLanes> synthWins;
    std::array<juce::AudioPluginInstance*, kLanes> winPlugin {};
    juce::Rectangle<int> topBar, screenArea, rack;
    int seenVersion = -1;
    bool partsShown = false;
    juce::String status; juce::uint32 statusUntil = 0;
    float vuL = 0, vuR = 0;
    bool lastPlaying = false;
};
} // namespace ghost
