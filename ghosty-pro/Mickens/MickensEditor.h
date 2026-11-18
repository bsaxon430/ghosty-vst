#pragma once
#include "TypeIn.h"
#include "MickensProcessor.h"
#include "GhostThemes.h"

class MickensEditor : public juce::AudioProcessorEditor, public juce::DragAndDropContainer, private juce::Timer {
public:
    explicit MickensEditor(MickensProcessor&);
    ~MickensEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;
    // tests / screenshots
    void openSearch(int bay, int mode);   // the BREAK SEARCH / BREAK MATCH overlay (mode 0 feel, 1 similar, 2 complement)
    void closeSearch();
    // the palette's contrast in a theme (0 PURPLE, 1 BLACK, 2 LIGHT, 3 GHOST): lowest body-text, secondary-text and accent ratios on its surfaces
    struct Contrast { double body = 0, secondary = 0, accent = 0; juce::String worst; };
    static Contrast contrastOf(int theme);
    static juce::String versionString();   // JucePlugin_VersionString (shown in the header)
    static inline bool freezeAnimation = false;   // tests / screenshots: the logo, unicorns and title stand still (deterministic pixels)
    // GHOST (the 4th look, stored theme 3: liquid glass on the LIGHT base). Tests: the glass surfaces without any text, where the texts sit
    // (+ their colour), and the cached-image build counters (aurora, glass panels, pills / knob caps)
    static constexpr int kLookGhost = 3;
    void paintGhostSurfaces(juce::Graphics&);
    std::vector<std::pair<juce::Rectangle<int>, juce::Colour>> ghostTexts();
    static int ghostBackdropBuilds();
    static int ghostPanelBuilds();
    struct Content;
private:
    void timerCallback() override;
    MickensProcessor& proc;
    std::unique_ptr<juce::LookAndFeel_V4> lnf;
    std::unique_ptr<Content> content;
    ghosty::TypeIn typeIn { *this };   // right-click any knob to type a value
    int shownTheme = 0;
};
