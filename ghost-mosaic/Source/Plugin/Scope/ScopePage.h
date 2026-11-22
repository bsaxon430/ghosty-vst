// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
//
// The page that hosts the new mode's view (page 3 of the editor, beside PLAY, TERRAIN and UNDER THE HOOD).
//
// It owns the tap's data model, the Context and the view, in that order, so the view always goes first and the
// model last. The model belongs here, not to the editor or the processor, because its life is exactly this page's
// time on screen: it is attached when the mode comes on screen and detached when it leaves, a closed window takes
// the page (and so the model) with it, and nothing of it can be left reading the engine behind the user's back.
//
// One frame = model.tick (dt), then view.frame (dt). The editor's 60 Hz timer drives it through tick() while the
// mode is on screen; off screen nothing runs at all. The screenshot harness switches to the test clock and steps
// frames by hand with stepForTests, so pictures do not depend on the wall clock.
#pragma once
#include "ScopeView.h"

namespace mosaic::scope
{
class Page : public juce::Component
{
public:
    explicit Page (Processor& p);
    ~Page() override;

    // ---- the editor's part
    void setOnScreen (bool on);                 // the mode is showing or not: attaches / detaches the tap, tells the view
    bool onScreen() const { return m_on; }
    void tick();                                // the editor's 60 Hz timer: one frame, when on screen (dt from the clock, at most 0.1 s)
    juce::String buttonName() const { return m_view->buttonName(); }
    juce::String buttonTip() const { return m_view->buttonTip(); }

    // ---- tests and the studio harness: frames by hand, no wall clock
    void useTestClock (bool on) { m_testClock = on; m_model.explainEveryPick (on); }   // on: tick() does nothing, only the calls below move the page (and no pick is let go unexplained)
    void stepForTests (double dtSeconds) { stepModelForTests (dtSeconds); stepViewForTests (dtSeconds); }   // exactly one frame
    void stepModelForTests (double dtSeconds);  // the first half of a frame: the model takes in what the engine sent
    void stepViewForTests (double dtSeconds);   // the second half: the view's own work (the harness times it by itself)
    int framesRun() const { return m_frames; }  // how many frames the view has been given, ever
    View& view() { return *m_view; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void modelHalf (double dt);
    void viewHalf (double dt);

    tap::Model m_model;
    Context m_ctx;
    std::unique_ptr<View> m_view;
    bool m_on = false, m_testClock = false;
    double m_lastMs = 0;
    int m_frames = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Page)
};
}
