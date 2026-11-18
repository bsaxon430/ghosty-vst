#pragma once
// Ghosty Spectrum GHOST VOICE page: sing a take (YOUR VOICE), add VOICE B (and C) - a recording or a designed identity - blend the
// voice fingerprints (pad + sliders), shape the new voice (VOICE DESIGN), tune / transpose it (PITCH), stack it (STACK), then
// PLAY, A/B, SAVE VOICE, export or drag the WAV out. The engine is Shared/VoiceFusion*, the host Shared/VoiceFusionHost*.
// Designed at 1200 x 750 (Spectrum's page area); lays out at any size >= 1100 x 700. House looks: BLACK / PURPLE / LIGHT.
#include "VoiceFusionHost.h"
#include "GhostThemes.h"
#include <juce_gui_basics/juce_gui_basics.h>

class GhostVoicePage : public juce::Component, public juce::FileDragAndDropTarget, public juce::DragAndDropContainer, private juce::Timer {
public:
    explicit GhostVoicePage(fusion::FusionHost& host);
    ~GhostVoicePage() override;
    // Demo gate - asked with its id ("spec.ghostvoice.*") before every export (DRAG WAV OUT, EXPORT WAV). null = refused (fail closed).
    std::function<bool(juce::Component* ui, const char* id)> allowExport;
    std::function<void()> onSendToArrange;           // the current result -> an ARRANGE clip (the tab wires it)
    std::function<double()> tempoFn;                 // the count-in tempo (Spectrum's tempo); unset = 100 bpm
    void setOwnsTooltips(bool owns);
    void setLook(ghosty::themes::Theme t);
    ghosty::themes::Theme look() const;
    void parentHierarchyChanged() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void fileDragEnter(const juce::StringArray&, int x, int y) override;
    void fileDragMove(const juce::StringArray&, int x, int y) override;
    void fileDragExit(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int x, int y) override;
    // tests / screenshots
    void refreshNow();
    void setKnob(const juce::String& name, double value);
    juce::String knobText(const juce::String& name) const;
    juce::StringArray exportForTest(int which);      // 0 DRAG WAV OUT, 1 EXPORT WAV (save dialog)
    bool saveVoiceForTest(const juce::String& name);
    void pickFactoryVoice(int slot, int index);      // VOICE B / C <- a designed identity
    juce::String lastMessage() const;
    juce::Rectangle<int> boundsOf(const juce::String& name) const;
    void showAbout(bool show);
    struct Impl;
private:
    void timerCallback() override;
    fusion::FusionHost& host;
    std::unique_ptr<Impl> impl;
};
