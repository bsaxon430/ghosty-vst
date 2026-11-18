#pragma once
#include "TypeIn.h"
#include "BeatsXProcessor.h"
namespace ghosty { class WaveRegion; }
namespace bx { struct SliceView; }

class BeatsXEditor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, public juce::DragAndDropContainer, private juce::Timer {
public:
    explicit BeatsXEditor(BeatsXProcessor&);
    ~BeatsXEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragMove(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void startRecreateForTest(const juce::File& f);   // RE-CREATE AS MIDI on a file (as if dropped on the grid)
    bool recreateBusyForTest(); juce::String recreateStatusForTest(); void recreateTickForTest(); void closeRecreateForTest(); void importModuleForTest(const juce::File& f); juce::Component* recreatePanelForTest(); ghosty::WaveRegion* regionForTest(); bx::SliceView* slicesForTest(); void showSlicesForTest(bool on);
    struct Content;
    void showPageForTest(int page);
    void setScriptTextForTest(const juce::String& code);
    void setThemeForTest(bool dark);                 // (true = BLACK, false = LIGHT)
    void setThemeForTest(int theme);                 // 0 BLACK, 1 PURPLE, 2 LIGHT
    bool keyPressForTest(const juce::KeyPress& k);   // the editor's own key handling (SPACE etc.)
    void syncUiForTest();
    juce::String padHelpForTest();                   // the PADS page help as shown now
    void exportModuleForTest(bool it);                // MELODY > Export as XM (false) / IT (true), as the menu does
    juce::Component* partForTest(const juce::String& which);   // "stutter", "melody", "linear", "tracker", "grid", "lanes"                            // run the header's SYNC refresh now (normally the timer)
    juce::TextButton* themeButtonForTest();          // the look button (BLACK -> PURPLE -> LIGHT -> GHOST)
    int lookForTest() const;                         // 0 BLACK, 1 PURPLE, 2 LIGHT, 3 GHOST
    juce::Component* contentForTest();               // the scaled content (every page's controls)
    int glassBuildsForTest(int which) const;         // GHOST caches: 0 aurora builds, 1 panel builds, 2 sprite builds
    void paintGlassSurfacesForTest(juce::Graphics& g);   // GHOST: the content's backdrop + glass cards, no text (content coordinates)
    juce::Rectangle<float> mascotRectForTest() const;
    void refreshLook();   // after a theme switch: the look-and-feel re-reads the palette
private:
    void timerCallback() override;
    BeatsXProcessor& proc;
    std::unique_ptr<juce::LookAndFeel_V4> lnf;
    std::unique_ptr<Content> content;
    ghosty::TypeIn typeIn { *this };
    float scale = 1.0f, offX = 0, offY = 0;   // right-click any knob to type a value
    int sharedTick = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BeatsXEditor)
};
