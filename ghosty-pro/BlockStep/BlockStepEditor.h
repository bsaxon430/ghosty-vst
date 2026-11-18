#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "BlockStepProcessor.h"
#include "UserBanksBridge.h"

// The plugin window: a thin native strip (DRAG MIDI, host sync status, UNLOCK) over the Block Step page.
class BlockStepEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit BlockStepEditor(BlockStepProcessor&);
    ~BlockStepEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void sendRestore();   // push the saved pattern into the page

private:
    struct DragOut : juce::Component, juce::SettableTooltipClient {   // ⇣ MIDI / ⇣ WAV: drag the beat (or the chosen channel) into the DAW
        DragOut(BlockStepEditor& e, bool w) : ed(e), wav(w) { setMouseCursor(juce::MouseCursor::DraggingHandCursor);
            setTooltip(w ? "Drag the beat into FL Studio (or any DAW) as audio (WAV). ALL / CH picks the whole beat or one channel"
                         : "Drag the beat into FL Studio (or any DAW) as a MIDI clip. ALL / CH picks the whole beat or one channel"); }
        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override { dragging = false; }
        void mouseDrag(const juce::MouseEvent&) override;
        BlockStepEditor& ed; bool wav; bool dragging = false;
    };
    void timerCallback() override;
    std::optional<juce::WebBrowserComponent::Resource> resource(const juce::String& url) const;
    juce::File writeMidi(int onlyCh) const;
    juce::File writeWav(int onlyCh) const;
    void updTarget();
    static juce::WebBrowserComponent::Options options(BlockStepEditor& e);

    BlockStepProcessor& proc;
    DragOut drag { *this, false }, dragWav { *this, true };
    juce::TextButton target;
    int dragCh = -1;          // -1 = the whole beat, else one channel (set here or by opening a channel's panel in the page)
    double hostBpm = 0;       // the DAW's tempo, once it has played
    juce::Label status;
    std::unique_ptr<ghosty::UnlockButton> unlock;
    std::unique_ptr<ghosty::BypassButton> bypassBtn;   // top-right: mutes the whole plugin (same as FL's own bypass)
    ghosty::userbanks::Bridge<bstep::SampleStore> banks { proc.kitStore(), [this](const juce::String& id, juce::var v) { if (pageReady) web.emitEventIfBrowserIsVisible(id, v); },
        [](const juce::String& f, juce::MemoryBlock& mb) { int n = 0; const void* d = BlockStepKit::resource(f, n); if (d == nullptr || n <= 0) return false; mb.replaceAll(d, (size_t) n); return true; } };   // MY BANKS (+ COPY of built-in sounds)
    juce::WebBrowserComponent web;
    bool pageReady = false;
    static constexpr int kStrip = 30;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BlockStepEditor)
};
