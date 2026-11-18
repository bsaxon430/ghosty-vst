/*
 * Poltergeist 2 (GhostBlock) - the SPYGLASS window: Six Sines' own spectrum analyzer + waveform scope
 * (ui/spectrum-analyzer.h, SpectrumAnalyzerComponent; upstream edited for colour/look only, marked GHOSTBLOCK) in a frame of the ghost machine.
 *
 * What is ours here is only what is around the upstream component: a borderless, always-on-top desktop window with
 * a painted riveted iron frame (9-slice, common/spyglass/spy-frame.png), a title plate with live Poppins text, a
 * brass close cap, its own move / resize handling (drag the title band / any frame edge / the lee-bottom gusset),
 * and the machine palette pushed through the component's public skin API (setSkin + the shared stylesheet).
 *
 * The audio: the component reads a ring of its own (owned by the editor, fed from the editor's own drain of the
 * synth's output ring), so the synth's ring keeps exactly one reader. See MachineEditor::drainAudio.
 */
#pragma once

#include <functional>
#include <memory>
#include <juce_gui_basics/juce_gui_basics.h>

#include "synth/synth.h"
#include "ui/spectrum-analyzer.h"

namespace ghostblock::p2::machine
{
namespace ss = baconpaul::six_sines;

class SpyglassWindow : public juce::Component
{
  public:
    // the frame's insets (spyglass.py FS: left, top = the title band, right, bottom), design px = logical px
    static constexpr int kL = 18, kT = 34, kR = 18, kB = 18;
    static constexpr int kMinW = 420, kMinH = 300; // (under ~300 the analyzer's fixed-height plots leave no line plot to read)
    static constexpr int kDefW = 676, kDefH = 612;

    SpyglassWindow(ss::Synth::audioOutputQueue_t &ring, float hostSampleRate, ss::ui::defaultsProvder_t *defaults);
    ~SpyglassWindow() override;

    // the analyzer's colours in the machine's palette (smoky glass, amber trace, teal-green waterfall, brass grid)
    static ss::ui::SixSinesSkin machineSkin();

    // what the palette cannot carry, through the analyzer's colour-only Look (a teal scope trace, a glow under the
    // amber line, Poppins plot labels, the right-click menu in our smoky-glass / brass look)
    ss::ui::SpectrumAnalyzerComponent::Look machineLook();
    juce::LookAndFeel *menuLook() { return menuLnf.get(); } // (offline checks)

    void setHostSampleRate(float sr);
    ss::ui::SpectrumAnalyzerComponent *analyzer() { return content.get(); }
    juce::Component *closeCap();

    // put it on the desktop at `where` (screen px; empty = beside `nextTo`), always on top
    void open(juce::Rectangle<int> where, juce::Component *nextTo);

    std::function<void()> onCloseRequested;                    // the close cap (deferred: the owner deletes the window)
    std::function<void(juce::Rectangle<int>)> onBoundsChanged; // moved or resized (screen px): the owner remembers it

    void paint(juce::Graphics &) override;
    void paintOverChildren(juce::Graphics &) override;
    void resized() override;
    void moved() override;
    void mouseDown(const juce::MouseEvent &) override;
    void mouseDrag(const juce::MouseEvent &) override;
    void mouseUp(const juce::MouseEvent &) override;

    // offline checks: everything the window paints, children included, into an image (scale 2 = the Mac's 2x)
    juce::Image snapshot(float scale);

  private:
    struct Cap;
    struct Corner;
    std::unique_ptr<juce::LookAndFeel> menuLnf; // before `content`: it outlives the analyzer that shows menus with it
    std::unique_ptr<ss::ui::SpectrumAnalyzerComponent> content;
    std::unique_ptr<Cap> cap;
    juce::ComponentBoundsConstrainer cons;
    std::unique_ptr<juce::ResizableBorderComponent> border;
    std::unique_ptr<Corner> corner;
    juce::ComponentDragger dragger;
    bool dragging{false};
    juce::Image frame, plate; // 2x
    juce::Font titleFont{juce::FontOptions()}, subFont{juce::FontOptions()};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpyglassWindow)
};
} // namespace ghostblock::p2::machine
