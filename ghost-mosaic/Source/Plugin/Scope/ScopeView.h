// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
//
// The new mode (working name INSIDE): what a design implements.
//
// A design is one class derived from mosaic::scope::View. ActiveView.cpp returns it from createView(), and that is
// the only shared file a design changes (plus its own files in cmake/ScopeSources.cmake). The page that hosts it
// (ScopePage) gives it the whole body of the window between the header and the bottom bar, calls frame() once per
// 60 Hz tick while the mode is on screen, and stops completely when another page shows or the window closes.
//
// The rules a view lives by (studio/RULEBOOK.md):
//  - everything that moves is driven by real engine data: the tap's model (ctx.model) and the engine's existing
//    read-only accessors (ctx.engine). Nothing is invented, and nothing moves while the engine is silent;
//  - time comes from frame (dt), never from the wall clock (the screenshot harness steps frames by hand);
//  - the audio thread is never touched: no engine internals, no locks, nothing but the calls named here;
//  - only existing parameters and engine calls are sent (ctx.processor); viewing options are plain UI state.
#pragma once
#include "Processor.h"
#include "ParamRow.h"
#include "Look.h"
#include "ScopeModel.h"

namespace mosaic::scope
{
// What a view may use. All of it on the message thread.
struct Context
{
    Processor& processor;     // the settings: processor.getParam / setParam, processor.params(), undo, A / B. Existing parameters only.
    const Engine& engine;     // the engine's existing read-only accessors (status, level, pictures of the library and the target,
                              // the values in use). Read-only on purpose. popEvents() is not for views: it has one reader, the editor.
    tap::Model& model;        // the tap's data model, already updated for this frame when frame() is called
};

class View : public juce::Component
{
public:
    explicit View (Context& c) : ctx (c) {}
    ~View() override = default;

    // Once per 60 Hz tick while the mode is on screen, AFTER ctx.model was updated for this tick. dtSeconds is the
    // time since the previous frame (exactly 1/60 in the harness, never more than 0.1). Move fades and glides by dt
    // and repaint what changed: repaint (area) for a part, repaint() for all; nothing changed = no repaint.
    virtual void frame (double dtSeconds) = 0;

    // The words on the header button that opens the mode (the winning design may rename it). Keep it short:
    // it has to fit beside PLAY, TERRAIN and UNDER THE HOOD in a 1000 px window (`studio selftest` checks).
    virtual juce::String buttonName() const { return "INSIDE"; }
    // ...and its tooltip, in plain words.
    virtual juce::String buttonTip() const { return "See inside: how the target is cut into pieces, which piece of your sounds is picked for each one, and why, while it plays."; }

    // The mode came on screen / left it (another page was chosen, or the window closed). The tap is attached just
    // before pageShown() and detached just after pageHidden(): no frame() comes in between, and what was drawn
    // before is old, so start again from an honest "nothing yet" picture in pageShown().
    virtual void pageShown() {}
    virtual void pageHidden() {}

    // A view that wants keys calls setWantsKeyboardFocus (true) in its constructor: the page then gives it the
    // keyboard when the mode comes on screen.

    // An EXISTING control inside the view: the same parameter, names and behaviour as on UNDER THE HOOD (the
    // original's lowercase name, our plain-words line under it, UNDO, A / B, automation, TERRAIN's MOVING NOW
    // marker), greyed with the reason in its tooltip while the setting does nothing. The view owns it; give it
    // bounds in resized() (ParamRow::kHeight tall is what it is drawn for, 260 px or wider). The page keeps every
    // ParamRow inside the view up to date by itself. nullptr: no such setting in this version.
    ParamRow* addControl (const juce::String& paramId)
    {
        auto row = ParamRow::make (ctx.processor, paramId);
        if (row == nullptr) return nullptr;
        addAndMakeVisible (*row);
        return m_controls.add (row.release());
    }

protected:
    Context& ctx;

private:
    juce::OwnedArray<ParamRow> m_controls;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (View)
};

// The design on screen. Defined in ActiveView.cpp: the one function a design replaces.
std::unique_ptr<View> createView (Context&);
// The plain reference view (BaselineView.cpp): what createView returns until a design replaces it, and the example to copy.
std::unique_ptr<View> createBaselineView (Context&);
}
