// Ghost Voice - AGPL-3.0-only (see LICENSE, NOTICE.md).
//
// UNDER THE HOOD: a page (a tab next to SETTINGS) with every option of Seed-VC's singing conversion under Seed-VC's own name,
// default and range (Engine/SeedVcOptions.h has the table, docs/CONTROLS.md the same as a document). Each control writes the one
// EngineSettings the rest of the window uses, so QUALITY / FOLLOW / PITCH / MATCH RANGE and their rows here are the same values.
// Below the list: the exact command line Seed-VC's inference.py gets, and the job file the helper reads. NATURAL and CLASSIC
// are our own engines, not Seed-VC's, so they are not on this page.
#pragma once
#include "Editor.h"
#include "SeedVcOptions.h"

namespace gv
{
class HoodPage : public juce::Component
{
public:
    explicit HoodPage (Editor&);
    ~HoodPage() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void sync();                                   // follow the session (called from Editor::refresh)
    void applyTheme();
    int shownRows() const;                         // rows that pass the search
    juce::String commandText() const;              // what the command box shows
    juce::String jobText() const;                  // what the job box shows
    bool touch (hood::Id, double value);           // drives the real control of that option the way a click would (tests)
private:
    struct Row; struct Content;
    Editor& ed;
    juce::TextEditor search, cmdBox, jobBox;
    juce::TextButton startBtn { "GHOST VOICE START" }, defaultsBtn { "ORIGINAL DEFAULTS" };
    juce::Viewport vp;
    std::unique_ptr<Content> content;
    juce::String cmdShown, jobShown;
    gv::Job sampleJob() const;
    void filter();
};
}
