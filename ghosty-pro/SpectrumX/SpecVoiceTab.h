#pragma once
// The VOICE tab: the VOICE page (VoicePage, unchanged) + a slim strip under it with SEND TO ARRANGE.
// The page follows Spectrum's DARK / LIGHT toggle through VoicePage::setDark.
#include "SpecProcessor.h"
#if GHOSTY_VOICE
#include "VoicePage.h"

class SpecVoiceTab : public juce::Component, private juce::Timer {
public:
    explicit SpecVoiceTab(SpecProcessor&);
    ~SpecVoiceTab() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void themeChanged();
    void sendToArrange();                       // the current VOICE result becomes an ARRANGE clip (re-renderable)
    VoicePage& page() { return *voicePage; }    // tests
    juce::String status;
private:
    void timerCallback() override;
    SpecProcessor& proc;
    std::unique_ptr<VoicePage> voicePage;
    juce::TextButton arrangeBtn { "SEND TO ARRANGE" };
};
#endif
