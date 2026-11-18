#pragma once
// PROCESS page: the shared PROCESS LAB (Shared/ProcessLab*) working on the SPECTRAL LAB's current sound (48 kHz mono).
// APPLY / BOUNCE / UNDO / REDO put the result back as the Lab's source (undoable in the Lab too); BOUNCE NEW makes it the source
// and keeps the sound it came from in the SOURCE menu. The result is then what the Lab plays, sends to ARRANGE / Ghosty Beats and drags out.
// The chain is saved with the project (SpecProcessor::plabState); the audio travels through the Lab's own source (FLAC in the state).
#include "SpecProcessor.h"
#if GHOSTY_PROCESS_LAB
#include "ProcessLabPanel.h"

class LabProcessPage : public juce::Component, private juce::Timer {
public:
    explicit LabProcessPage(SpecProcessor&);
    ~LabProcessPage() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;
    void themeChanged();
    plab::ProcessLabPanel& panel() { return *lab; }   // tests
    plab::Host& exportHost() { return host; }          // tests (the demo gate)
    void reloadIfChanged(bool force = false);
private:
    void timerCallback() override;
    struct Host : plab::Host {
        explicit Host(LabProcessPage& o) : page(o) {}
        LabProcessPage& page;
        plab::Audio getBuffer() override;
        void setBuffer(const plab::Audio& committed) override;
        double sampleRate() override { return lab::SR; }
        bool allowExport(juce::Component* ui) override { return page.proc.allowExport(ui, "spec.plab.drag"); }   // demo: the PROCESS drag is refused
        void audition(std::shared_ptr<const plab::Audio> clip, bool loop) override { if (clip) page.proc.labAudition.play(clip, loop); else page.proc.labAudition.stop(); }
        float auditionPosition() override { return page.proc.labAudition.playing() ? page.proc.labAudition.position() : -1.0f; }
        double tempoBpm() override { return page.proc.tempo(); }
        bool canBounceToNew() override { return true; }
        bool showsPerSlice() override { return false; }   // the Lab's sound has no slices: PER SLICE is hidden here
        bool bounceToNew(const plab::Audio& a) override;
    };
    SpecProcessor& proc; Host host;
    std::unique_ptr<plab::ProcessLabPanel> lab;
    juce::ComboBox sourceBox; juce::TextButton backBtn { "BACK TO THE LAB" };
    juce::uint32 seenRev = 0; int seenGen = -1; juce::String status;
    friend struct Host;
public:
    std::function<void()> onBack;
};
#endif
