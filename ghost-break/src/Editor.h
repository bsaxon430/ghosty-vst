// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Processor.h"

namespace gbk
{
class Editor final : public juce::AudioProcessorEditor, private juce::Timer, private juce::ChangeListener
{
public:
    explicit Editor (Processor&);
    ~Editor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void showFx (int slot);
    void refreshBreakMenus();
    void rebuild();   // after SETTINGS switches the look
    void relayout() { laidOut = false; resized(); repaint(); }   // after a part opens or closes (a bay's STEPS)
private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    float scale() const;   // the window's size against the 1180 x 760 it is laid out in
    struct Impl; std::unique_ptr<Impl> d;
    Processor& proc; bool laidOut = false, sized = false;
};
} // namespace gbk
