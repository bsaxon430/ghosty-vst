#pragma once
#include "BeatsXProcessor.h"

class BeatsXEditor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, public juce::DragAndDropContainer, private juce::Timer {
public:
    explicit BeatsXEditor(BeatsXProcessor&);
    ~BeatsXEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    struct Content;
private:
    void timerCallback() override;
    BeatsXProcessor& proc;
    std::unique_ptr<juce::LookAndFeel_V4> lnf;
    std::unique_ptr<Content> content;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BeatsXEditor)
};
