#pragma once
#include "SpecProcessor.h"

class SpecEditor : public juce::AudioProcessorEditor, public juce::DragAndDropContainer, public juce::FileDragAndDropTarget, private juce::Timer {
public:
    explicit SpecEditor(SpecProcessor&);
    ~SpecEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray& f) override;
    void filesDropped(const juce::StringArray& f, int, int) override;
    struct Content;
private:
    void timerCallback() override;
    SpecProcessor& proc;
    std::unique_ptr<juce::LookAndFeel_V4> lnf;
    std::unique_ptr<Content> content;
};
