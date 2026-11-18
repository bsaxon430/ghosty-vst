// Ghost Voice - AGPL-3.0-only (see LICENSE, NOTICE.md).
//
// The window, after Brandon's mockup (2026-11-05): GHOST VOICE header with the six tabs (IDENTITY LAB, VOICE LIBRARY, SING
// ENGINE, TOOLS, SETTINGS, UNDER THE HOOD = the neural engine's own full option list), the left list, the page, and the VOICE LIBRARY strip with the status pill at the bottom.
// Every number on screen is real: file lengths, the settings, and what the voice-print meter measured on the rendered voice. Nothing is
// drawn as a meter unless it was measured; the UNIQUE IDENTITY badge only shows when the measurement passes its rule.
#pragma once
#include "Processor.h"
#include "SplashArt.h"

namespace gv
{
class HoodPage;
class ChopPage;
class Editor : public juce::AudioProcessorEditor, public juce::DragAndDropContainer, private juce::Timer
{
public:
    explicit Editor (Processor&);
    // Brandon 2026-11-06: every plug-in OPENS at the size that fits his 1440x900 MacBook Pro inside FL Studio (about 1200 x 760,
    // never wider than 1240 or taller than 780). Resizable upwards; the default is always this size (nothing remembers a size).
    static constexpr int defaultW = 1200, defaultH = 760, maxW = 2000, maxH = 1400;
    // ONE small ABOUT / CREDITS item names every open-source part, its authors, its licence and its source link (GPLv3 5(d));
    // nowhere else in the window names them.
    static juce::String creditsText();
    void showCredits (bool);
    bool creditsShown() const;
    ~Editor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    enum Page { lab = 0, library = 1, sing = 2, tools = 3, prefs = 4, hood = 5, chop = 6, kPages = 7 };   // CHOP (build 8) opens first
    void showPage (int);
    HoodPage* hoodPage();                   // the UNDER THE HOOD page (tests drive its controls)
    ChopPage* chopPage();                   // the CHOP page (tests drive it)
    juce::String tabsThatDontFit() const;   // build 10: every tab's title and its line under it fit whole (empty = all fit)
    int page() const { return pageIndex; }
    void refresh();                         // follow the session
    Processor& proc;
    const ghosty::themes::Palette& pal() const
    {   // build 12: LIGHT = SPLASH (Brandon's painting), DARK = his dark reference; the art follows (splash::art())
        splash::darkFlag() = proc.theme != ghosty::themes::Theme::Light; return splash::palette();
    }
    void applyTheme();
    juce::String statusText() const;        // the pill at the bottom right
    struct Parts;
    std::unique_ptr<juce::LookAndFeel_V4> lnf;   // gv::Look (Look.h)
    std::unique_ptr<Parts> parts;
    struct PanelRect { juce::Rectangle<float> r; };
    std::vector<PanelRect> panels;           // the panels of the page (painted behind their controls)
    juce::Rectangle<float> viewArea, badgeArea, idArea;
    juce::Rectangle<int> knobRow, toggleArea;    // where the engine's knobs / switches go (IDENTITY LAB)
    void layoutEngineControls();
    float clock = 0;                        // seconds, for the glows
private:
    void timerCallback() override;
    int pageIndex = chop;
    int tick = 0;                           // (was a function-level static: shared by every window)
};
}
