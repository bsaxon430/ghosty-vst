// SPDX-License-Identifier: AGPL-3.0-or-later
// GHOST ATLAS - the window. Everything on it is laid out once, at 1320 x 720, and scaled as one piece to whatever size
// the window is given (it keeps its shape), so it is the same screen on any display.
#pragma once
#include "Processor.h"
#include "../ui/Theme.h"

namespace atlas
{
class Editor final : public juce::AudioProcessorEditor
{
public:
    explicit Editor (Processor&);
    ~Editor() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    // for the screenshots (tests/Tests.cpp): show a page, wait for its picture, optionally with an overlay open
    void showPage (int page);
    void showOverlay (const juce::String& which);
    void settle();
    // for the checks: how much the world moves (0 still, 1 alive, 2 film), and how the film is getting on
    void setMotion (int mode);
    bool filmShowing() const;
    float filmMakeMs() const;
    int filmPictures() const;

private:
    juce::SharedResourcePointer<ui::Kept> kept;      // (first, so that it goes last: after everything that draws with it)
    struct Impl;
    std::unique_ptr<Impl> impl;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Editor)
};
} // namespace atlas
