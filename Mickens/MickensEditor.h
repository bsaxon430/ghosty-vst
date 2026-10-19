#pragma once
#include "MickensProcessor.h"

class MickensEditor : public juce::AudioProcessorEditor, public juce::DragAndDropContainer, private juce::Timer {
public:
    explicit MickensEditor(MickensProcessor&);
    ~MickensEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    struct Content;
private:
    void timerCallback() override;
    MickensProcessor& proc;
    std::unique_ptr<juce::LookAndFeel_V4> lnf;
    std::unique_ptr<Content> content;
    bool shownDark = false;
};
