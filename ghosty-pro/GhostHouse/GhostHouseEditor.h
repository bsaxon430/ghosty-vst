#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "GhostHouseProcessor.h"
#include "EdgeResize.h"
#include "UserBanksBridge.h"
#include "SoundDrag.h"

// The plugin window: a thin native strip (DRAG MIDI, host sync status, UNLOCK) over the Ghost House page.
class GhostHouseEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit GhostHouseEditor(GhostHouseProcessor&);
    ~GhostHouseEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void sendRestore();   // push the saved pattern into the page

private:
    struct DragOut : juce::Component, juce::SettableTooltipClient {   // ⇣ MIDI / ⇣ WAV: drag the beat (or the chosen channel) into the DAW
        DragOut(GhostHouseEditor& e, bool w) : ed(e), wav(w) { setMouseCursor(juce::MouseCursor::DraggingHandCursor);
            setTooltip(w ? "Drag the beat into FL Studio (or any DAW) as audio (WAV). ALL / CH picks the whole beat or one channel"
                         : "Drag the beat into FL Studio (or any DAW) as a MIDI clip. ALL / CH picks the whole beat or one channel"); }
        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override { dragging = false; }
        void mouseDrag(const juce::MouseEvent&) override;
        GhostHouseEditor& ed; bool wav; bool dragging = false;
    };
    void timerCallback() override;
    void sndHover(const juce::var& v);              // the page: the ⇣ under the pointer (or none)
    juce::File writeSound(const juce::var& v) const; // that sound as a 24-bit WAV, named after it
    std::optional<juce::WebBrowserComponent::Resource> resource(const juce::String& url) const;
    juce::File writeMidi(int onlyCh) const;
    juce::File writeWav(int onlyCh) const;
    juce::String chNames() const;
    void updTarget();
    // the tempo the export is made at: the DAW's (the base; the page's own when there is no DAW tempo) and x the TEMPO multiple
    double baseBpmFor(const ghouse::Pattern& P) const { const double d = proc.dawBpm.load(); return d > 0 ? d : (hostBpm > 0 ? hostBpm : P.bpm); }
    double effBpmFor(const ghouse::Pattern& P) const { return baseBpmFor(P) * P.mul; }
    void stripCheck();   // host test only
    static juce::WebBrowserComponent::Options options(GhostHouseEditor& e);

    GhostHouseProcessor& proc;
    DragOut drag { *this, false }, dragWav { *this, true };
    juce::TextButton target;
    juce::uint32 dragMask = 0;   // dragCh -3: these channels (the CH panel with several picked)
    int dragCh = -1;          // -1 = the whole beat, else one channel (set here or by opening a channel's panel in the page)
    double hostBpm = 0;       // the DAW's tempo, once it has played
    juce::Label status;
    std::unique_ptr<ghosty::UnlockButton> unlock;
    juce::TextButton infoBtn;   // "i": About / Credits (the code, data and sounds by other people, and their licences)
    juce::TextButton ghoBtn;   // GHOST ANIMATION: ON / OFF (next to BYPASS); the page follows it
    bool ghostsOff = false; void updGho();
    std::unique_ptr<ghosty::BypassButton> bypassBtn;   // top-right: mutes the whole plugin (same as FL's own bypass)
    ghosty::userbanks::Bridge<ghouse::SampleStore> banks { proc.kitStore(), [this](const juce::String& id, juce::var v) { if (pageReady) web.emitEventIfBrowserIsVisible(id, v); },
        [](const juce::String& f, juce::MemoryBlock& mb) { int n = 0; const void* d = GhostHouseKit::resource(f, n); if (d == nullptr || n <= 0) return false; mb.replaceAll(d, (size_t) n); return true; } };   // MY BANKS (+ COPY of built-in sounds)
    juce::WebBrowserComponent web;
    bool pageReady = false;
    ghosty::sounddrag::Grip sgrip;   // ⇣: drag a sound from the sound browsers into the DAW (owner 2026-11-02)
    juce::var sgripSnd;              // {cat, s, i, n} of the sound under it
    static constexpr int kStrip = 30;
    int zoomRefW = 0, zoomRefH = 0; double zoomNow = -1;
    ghosty::EdgeResizer edges { *this };   // grab any edge or corner to resize (owner 2026-11-01)   // WebZoom: the page scales as one picture (owner 2026-11-01)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GhostHouseEditor)
};
