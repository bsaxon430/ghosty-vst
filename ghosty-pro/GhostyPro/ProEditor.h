#pragma once
#include "TypeIn.h"
#include "ProProcessor.h"
#include "GhostUI.h"
#include "ProLayout2.h"

class ProContent;    // everything you see, laid out at a fixed W x H and scaled as one piece
namespace gpro { class ModUi; }   // MOD (round 3)

// Ghosty family look-and-feel plus the bits the Pro pages need (bipolar sliders, tabs).
class ProLnF : public ghosty::GhostLnF
{
public:
    explicit ProLnF (bool alwaysDark = false);
    // re-sets every colour id from the current pt::pal() (DARK = exactly the original ids); a forceDark LnF always uses DARK
    void applyTheme();
    const bool forceDark;
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    // disabled text stays readable (JUCE halves its alpha; on top of a dimmed control that read as "blacked out")
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawLabel (juce::Graphics&, juce::Label&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void fillTextEditorBackground (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics&, const juce::Path&, juce::Image&) override;   // GLASS (round 3)
};

class ProEditor : public juce::AudioProcessorEditor
{
public:
    static constexpr int W = 1140, H = 866;
    explicit ProEditor (ProProcessor&);
    ~ProEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void showTab (int index);        // 0 SYNTH, 1 MOD, 2 FX, 3 BROWSE, 4 BEND, 5 TRAP (also used by the test harness)
    void setLightTheme (bool light); // the DARK / LIGHT toggle (also used by the test harness); saved with the project, never in a sound
    bool isLightTheme() const;
    void setTheme (int theme);        // 0 BLACK, 1 PURPLE, 2 LIGHT (Shared/GhostThemes.h), 3 GHOST (ProGhostLook.h); the header button cycles them
    int themeIndex() const;
    // test hook: paints the current page alone onto a transparent image; returns the number of pixels inside its cards that
    // are not fully opaque (0 = nothing see-through)
    int seeThroughPixels();
    static juce::String versionString();   // "3.0.0" (JucePlugin_VersionString), shown small under the PRO badge
    juce::Component* contentComponent();
    void showCleanCard (bool show);  // the CLEAN knobs card (also used by the test harness)
    bool keyPressed (const juce::KeyPress&) override;
    void browseTestFilter (const juce::String& text, const juce::StringArray& tags, bool showTagPanel);   // test hook: the BROWSE filter
    juce::String presetNameTooltip();                                                                       // test hook: the header name's tooltip
    struct BrowseEmptyInfo { bool shown = false, initButton = false, legacyButton = false, tagsButton = false; juce::String title, text; juce::Rectangle<int> area; int rows = 0, entries = 0; };
    BrowseEmptyInfo browseEmptyInfo();   // test hook (FACTORY PRESETS OFF): the BROWSE page's empty state; area in editor pixels
    gpro::ModUi* modUi() const;      // MOD (round 3): the modulation UI (test hook)
    // LAYOUT 2 (round 3): 0 CLASSIC (the tabs, as before; the default), 1 STUDIO (one front page). The user's choice lives in their
    // settings file (gpro::layout2, ProLayout2.h), never in a project or a sound; setLayout itself does not save it (tests use it)
    void setLayout (int layout);
    int layout() const;
    // the zoom a window of w x h opens at on a screen with this user area (fits under a host's frame + title bar; 0.6 .. 1)
    static float fitZoomFor (juce::Rectangle<int> userArea, int w, int h);

private:
    ProProcessor& proc;
    ProLnF lnf;
    std::unique_ptr<ProContent> content;
    ghosty::TypeIn typeIn { *this };   // right-click any knob to type a value
    std::unique_ptr<juce::TooltipWindow> tips;   // the ONE tooltip window of this editor (themed by its look-and-feel; the FX rack has none)
};
