#pragma once
#include "SpecProcessor.h"
namespace ghosty { class TypeIn; }
namespace sp { class SpecGuide; class FirstRunHint; }
#include "SpecTabs.h"
#include "GhostThemes.h"

class SpecEditor : public juce::AudioProcessorEditor, public juce::DragAndDropContainer, public juce::FileDragAndDropTarget, private juce::Timer {
public:
    explicit SpecEditor(SpecProcessor&);
    ~SpecEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;
    juce::KeyListener* ownSpaceKeys() const;
    // HOW IT WORKS (SpecGuide.h): the overlay, its TRY IT examples, the once-per-tab first-run hint
    void openGuide(int card); void closeGuide(); bool guideOpen() const;
    bool tryIt(int card);                             // load that card's example + go to its tab; true = something is there to hear
    sp::SpecGuide* guidePanel() const { return guide.get(); }
    sp::FirstRunHint* firstRunHint() const { return hint.get(); }
    bool loadCanvasFile(const juce::File& f);         // LOAD (PAINT PADS / LOOP): the canvas, on the tab it was saved on
    juce::Component* content_() const;               // tests          // the SYNC: OWN SPACE listener every page carries (tests replay JUCE's key routing with it)
    bool isInterestedInFileDrag(const juce::StringArray& f) override;
    void filesDropped(const juce::StringArray& f, int, int) override;
    struct Content; struct TabBar;
    void setTheme(bool dark, bool force = false);   // the old two-state call: true = BLACK, false = LIGHT
    void setLook(ghosty::themes::Theme look, bool force = false);   // BLACK / PURPLE / LIGHT for every page (saved with the session)
    static constexpr int kBarH = 40;                  // the tab bar above the 1200 x 790 pages
private:
    void timerCallback() override;
    SpecProcessor& proc;
    std::unique_ptr<juce::LookAndFeel_V4> lnf;
    std::unique_ptr<Content> content;
    std::unique_ptr<TabBar> tabBar;
    std::unique_ptr<ghosty::TypeIn> typeIn;   // right-click any knob / slider to type a value
    std::unique_ptr<juce::TooltipWindow> tips;
    std::unique_ptr<sp::SpecGuide> guide; std::unique_ptr<sp::FirstRunHint> hint; juce::uint32 hintArmedMs = 0;
    void maybeHint(sp::TabKind k); void dismissHint(); void markSeen(sp::TabKind k);
    struct HintWatch : juce::MouseListener { SpecEditor& ed; explicit HintWatch(SpecEditor& e) : ed(e) {} void mouseDown(const juce::MouseEvent&) override; } hintWatch { *this };   // the one tooltip window (pages are told not to make their own)
};
