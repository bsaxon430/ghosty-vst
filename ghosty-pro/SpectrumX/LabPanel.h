#pragma once
// SPECTRAL LAB tab (the UI). Covers the whole Spectrum window while open; the other tabs stay exactly as they were.
#include "SpecProcessor.h"

class LabPanel : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer {
public:
    explicit LabPanel(SpecProcessor&);
    ~LabPanel() override;
    std::function<void(int mode)> onLeave;          // 0 PAINT PADS, 1 PAINT LOOP
    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void fileDragEnter(const juce::StringArray&, int x, int y) override;
    void fileDragMove(const juce::StringArray&, int x, int y) override;
    void fileDragExit(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int, int) override;
    bool keyPressed(const juce::KeyPress&) override;
    void showPage(int p);                           // 0 PAINT 1 IMAGE 2 GHOST 3 SPLIT 4 CHAOS 5 DNA
    void canvasReplaced();                          // the canvas was replaced from outside (TRY IT): fit the view, refresh everything
    void selectTool(int t);                         // 0 PAINT 1 ERASE 2 SMEAR 3 FREEZE 4 SHIFT 5 STRETCH 6 LOCK 7 SELECT
    juce::NamedValueSet probe() const;
    void themeChanged();                            // after DARK / LIGHT              // read-only state for the UI tests (page, tool, zoom window, selection, drop zone ...)
    struct Impl;
private:
    void timerCallback() override;
    SpecProcessor& proc;
    std::unique_ptr<Impl> impl;
};
