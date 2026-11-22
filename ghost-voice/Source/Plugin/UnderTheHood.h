// Ghost Voice - AGPL-3.0-only (see LICENSE, NOTICE.md).
//
// UNDER THE HOOD: a page (a tab next to SETTINGS) with every option of Seed-VC's singing conversion under Seed-VC's own name,
// default and range (Engine/SeedVcOptions.h has the table, docs/CONTROLS.md the same as a document). Each control writes the one
// EngineSettings the rest of the window uses, so QUALITY / FOLLOW / PITCH / MATCH RANGE and their rows here are the same values.
// Below the list: the exact command line Seed-VC's inference.py gets, and the job file the helper reads. NATURAL and CLASSIC
// are our own engines, not Seed-VC's, so they are not on this page.
// (Brandon 2026-11-22) UNDER THE HOOD is the ORIGINAL CONTROLS page of every open-source engine: one sub-tab each, laid out like
// that project's own window (UI/HoodOriginals.h): SEED-VC (the list below, unchanged), INTERSECT SOUND, INTERSECT AUTO CHOP,
// OPENVPI PHRASES, FLAYR CHOP. This is the one page where the projects' own names are shown (with ABOUT / CREDITS).
#pragma once
#include "Editor.h"
#include "SeedVcOptions.h"
#include "HoodOriginals.h"

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
    // the sub-tabs: 0 = SEED-VC (this list), then the other engines' own pages
    enum Tab { seedvc = 0, intersectSound, intersectAutoChop, phrases, flayr, kTabs };
    int tab() const { return tabIndex; }
    void showTab (int);
    juce::String tabTitle (int) const;
    hoodui::Panel* panel (int tab) const;          // nullptr for SEED-VC
    juce::String subLine() const;                  // the page heading's line for the sub-tab shown
    juce::String countLine() const;                // the gold line under it
    std::vector<std::pair<juce::String, juce::String>> sideRows() const;   // the left column's facts (empty = the editor's own)
    std::function<void()> onTabChange;             // the editor repaints its heading
private:
    int tabIndex = seedvc;
    std::unique_ptr<hoodui::Panel> others[kTabs];
    juce::TextButton tabs[kTabs];
    juce::Rectangle<int> seedArea;
    void layoutSeed (juce::Rectangle<int>);
    struct Row; struct Content;
    Editor& ed;
    juce::TextEditor search, cmdBox, jobBox;
    juce::TextButton startBtn { "GHOST VOICE START" }, defaultsBtn { "ORIGINAL DEFAULTS" };
    juce::Viewport vp;
    std::unique_ptr<Content> content;
    juce::String cmdShown, jobShown;
    float boxLineH = 0;                            // (polish 2) the boxes' real line height, measured from the job text
    juce::Rectangle<int> boxesArea;               // the command and job boxes with their headers (under the whole rows)
    gv::Job sampleJob() const;
    void filter();
};
}
