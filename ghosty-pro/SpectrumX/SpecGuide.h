#pragma once
// HOW IT WORKS for Ghosty Spectrum: the guide overlay (one card per tab + WHAT IS SPECTRUM with a flow diagram), the first-run
// hint bubble each tab shows once, and the small built-in demo material the TRY IT buttons load (generated here: no recordings).
// The editor (SpecEditor) owns one SpecGuide + one FirstRunHint and does the TRY IT loading (it knows the pages).
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "SpecTabs.h"
#include "SpecTheme.h"
#include <functional>
#include <vector>

namespace sp {

struct GuideCard {
    juce::String id, title, role, what;   // role = the one-line label (tab tooltips + first-run hint + card header)
    juce::StringArray steps, gestures;    // a first result in a minute; the mouse / keys
    juce::String out;                     // where the drag-out / export is
    int tab = -1;                         // (int) TabKind, -1 = WHAT IS SPECTRUM
};
const std::vector<GuideCard>& guideCards();          // [0] = WHAT IS SPECTRUM, then one per tab in the tab bar's order
int guideCardFor(TabKind k);                          // the card index of a tab (0 when unknown)
juce::String roleOf(TabKind k);                       // the one-line role label
juce::String firstHintOf(TabKind k);                  // the first-run hint (what to do first)

// ---- TRY IT material (original, generated in code)
std::vector<float> demoVoice(double sampleRate, double seconds);    // a sung "ah - oh - ee" line (pulse source through vowel formants)
juce::AudioBuffer<float> demoClip(int kind, double sampleRate);     // 0 = a plucked 2-bar figure at 120 bpm, 1 = a slow pad swell

class SpecGuide : public juce::Component {
public:
    SpecGuide();
    std::function<void(int card)> onTryIt;            // load that card's demo (the editor switches to the tab)
    std::function<void()> onClose;
    void showCard(int i);
    int card() const { return cur; }
    int numCards() const { return (int) guideCards().size(); }
    // tests: does every line of the card fit its box (layout is in logical 1200 x 830 units, the editor scales it)
    bool textFits(int i) const;
    juce::Rectangle<int> panelArea() const { return getLocalBounds().reduced(46, 34); }
    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& k) override;
    void mouseDown(const juce::MouseEvent& e) override;   // a click outside the panel closes it
    juce::TextButton tryBtn { "TRY IT" }, closeBtn { "CLOSE" }, nextBtn { "NEXT >" }, prevBtn { "< BACK" };
private:
    juce::OwnedArray<juce::TextButton> list;
    int cur = 0;
    juce::Rectangle<int> listArea() const, bodyArea() const;
    int layoutCard(int i, juce::Graphics* g) const;   // draws (g != nullptr) and returns the height the text needs
    void drawFlow(juce::Graphics& g, juce::Rectangle<float> r) const;
};

// the bubble a tab shows the first time it opens; it goes away on GOT IT or the first click on the page
class FirstRunHint : public juce::Component {
public:
    FirstRunHint();
    void showFor(TabKind k);
    TabKind tab() const { return kind; }
    std::function<void()> onDismiss, onMore;
    void paint(juce::Graphics&) override;
    void resized() override;
    juce::TextButton gotIt { "GOT IT" }, more { "HOW IT WORKS" };
private:
    TabKind kind = TabKind::Pads;
};

} // namespace sp
