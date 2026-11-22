// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
#include "ScopePage.h"

namespace mosaic::scope
{
// Every existing control placed anywhere inside the view: its MOVING NOW marker follows the value in use, and it
// greys while its setting does nothing.
static void syncControls (juce::Component& c, const Engine& e)
{
    for (auto* child : c.getChildren())
    {
        if (auto* row = dynamic_cast<ParamRow*> (child)) row->sync (e);
        else syncControls (*child, e);
    }
}

Page::Page (Processor& p) : m_model (p.engine()), m_ctx { p, p.engine(), m_model }
{
    setComponentID ("scopePage");
    setOpaque (true);                    // (a repaint in here never makes the editor behind it paint)
    m_view = createView (m_ctx);
    if (m_view == nullptr) m_view = createBaselineView (m_ctx);
    m_view->setComponentID ("scopeView");
    addAndMakeVisible (*m_view);
}

Page::~Page()
{
    setOnScreen (false);                 // the tap is detached before the view and the model go
    m_view.reset();
}

void Page::setOnScreen (bool on)
{
    if (on == m_on) return;
    m_on = on;
    if (on)
    {
        m_model.setActive (true);
        m_lastMs = 0;
        syncControls (*m_view, m_ctx.engine);
        m_view->pageShown();
        if (m_view->getWantsKeyboardFocus() && m_view->isShowing()) m_view->grabKeyboardFocus();
    }
    else
    {
        m_view->pageHidden();
        m_model.setActive (false);
    }
}

void Page::tick()
{
    if (! m_on || m_testClock) return;
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double dt = m_lastMs > 0 ? juce::jlimit (0.001, 0.1, (now - m_lastMs) * 0.001) : 1.0 / 60.0;
    m_lastMs = now;
    modelHalf (dt);
    viewHalf (dt);
}

// (tests) The model explains picks on a thread of its own. The harness's frame waits until that thread has nothing
// left to do and then collects (a tick of no time), so every pick in a picture is explained and two runs agree.
void Page::stepModelForTests (double dt)
{
    if (! m_on) return;
    modelHalf (dt);
    m_model.waitUntilExplained (5000);
    m_model.tick (0.0);
}

void Page::stepViewForTests (double dt)
{
    if (m_on) viewHalf (dt);
}

void Page::modelHalf (double dt) { m_model.tick (dt); }

void Page::viewHalf (double dt)
{
    if ((m_frames & 1) == 0) syncControls (*m_view, m_ctx.engine);   // (about 30 times a second, as on the other pages)
    m_view->frame (dt);
    ++m_frames;
}

void Page::paint (juce::Graphics& g) { g.fillAll (look::ground); }

void Page::resized() { m_view->setBounds (getLocalBounds()); }
}
