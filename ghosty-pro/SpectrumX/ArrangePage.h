#pragma once
// =====================================================================================================================
// INTEGRATION NOTE (for the Ghosty Spectrum owner; nothing here is wired into the plugin yet, no existing file was edited)
//
//  1. CMake: add the three sources to the plugin target (Arrange.cmake only builds the optional ArrangeTests harness):
//        target_sources(GhostySpectrum PRIVATE SpectrumX/ArrangeModel.cpp SpectrumX/ArrangeEngine.cpp SpectrumX/ArrangePage.cpp)
//     GhostySpectrum already links juce_dsp + signalsmith-stretch, which is all these need.
//  2. Processor (SpecProcessor): own one   arr::Session arrange;
//        prepareToPlay:   arrange.prepare(sampleRate, samplesPerBlock);   // blocks while it re-renders at a new rate (no silent start)
//        processBlock:    arrange.process(buffer, arr::hostPos(getPlayHead(), isNonRealtime()));   // ADDS the arrangement;
//                         real-time safe: no locks, no allocation (it reads an immutable snapshot published by a builder thread).
//                         Offline bounces (isNonRealtime) wait for the exact arrangement instead of playing stand-ins.
//        state:           arrange.getState() / arrange.setState(data, size): safe from ANY thread (hosts save / load off the
//                         message thread; a state set there plays at once and reaches the page on the message thread).
//                         One binary blob (sounds stored once as lossless 24-bit FLAC); keep it binary in the state if possible.
//     While the host plays, Arrange follows the host's position IN BEATS (ppq), so tempo changes / automation stay in sync,
//     and jumps / host loops play exactly. The ARRANGE loop region applies only to the page's own PLAY (and RENDER LOOP).
//     The tempo follows the host; without one, the tempo field is editable.
//  3. Editor (SpecEditor): add an ARRANGE tab holding   arr::ArrangePage page { processor.arrange };   size it to the tab area
//     (designed for 1200 x 700, works down to ~900 x 520). page.setDark(true/false) matches Spectrum's look (light by default).
//  4. CLIP SOURCE interface (ArrangeModel.h), for ARCS / VOICE / PROCESS LAB / SPECTRAL LAB or anything else:
//        processor.arrange.receive(arr::makePayload(buffer, sampleRate, "name", notes, { "ARCS", "name", stateBlob }, bpm));
//     on the message thread. buffer = rendered audio (1-2 ch, any rate); notes = optional arr::Note list (seconds) for MIDI
//     drag-out; bpm > 0 marks a tempo-tagged loop (its clips WARP to the song tempo); { page tag, name, opaque state blob }
//     lets that page re-render the sound later. Re-render registration is PER INSTANCE and RAII:
//        arr::SourceRegistration reg = processor.arrange.registerSource("ARCS", [sp = juce::Component::SafePointer<ArcsPage>(this)]
//            (const arr::SourceRef& r) -> arr::PayloadPtr { return sp ? sp->renderFor(r.state) : nullptr; });
//     keep `reg` as a member of the page (destroying it unregisters and waits for a running call). The inspector's RE-RENDER
//     calls it on the message thread; every duplicate follows.
//  5. SEND TO / RECEIVE FROM Ghosty Beats uses Documents/Ghosty/Shared Sounds (same folder and WAV convention as LabHost).
// =====================================================================================================================
// The page: toolbar, track headers (collapse, name, A = automation lane, M, S, gain, pan, meter), bar ruler (markers strip,
// click / drag = playhead, RIGHT-click = play from here, RIGHT-drag = loop, drag its ends / middle to change it, its WAV / MIDI
// grips drag the loop out), timeline (Ctrl/Cmd + wheel zoom, Alt + wheel track height, wheel /
// Shift + wheel scroll), clips (click / Shift-click / rubber band select, drag = move, Alt-drag = copy, edges = trim, round
// handles = fades, E = gain envelope edit, right-click = menu), keys: Space, S split, Del, Ctrl/Cmd + C X V A D B J, arrows
// nudge (Alt = fine), Q quantise, M mute, L loop, Z zoom to selection, Esc deselect; inspector; drag-out chips.
#include "ArrangeEngine.h"
#include <juce_gui_extra/juce_gui_extra.h>

#include "GhostThemes.h"
namespace ghosty { class TypeIn; }

namespace arr {

// slider value text (bars on the page): pan "C" / "30 L" / "45 R"; dB with "-inf"
juce::String panText(double pan);
double parsePanText(const juce::String& t);
double parseDbText(const juce::String& t, double lo, double hi);

struct Theme {
    bool dark = false; ghosty::themes::Theme look = ghosty::themes::Theme::Light;
    juce::Colour bg, panel, panel2, lane, laneAlt, grid, gridBar, ink, grey, dim, accent, accent2, loop, playhead, sel, meter, hot;
    static Theme make(ghosty::themes::Theme look);   // the house looks (Shared/GhostThemes.h): BLACK / PURPLE / LIGHT
    static Theme make(bool dark) { return make(dark ? ghosty::themes::Theme::Dark : ghosty::themes::Theme::Light); }
    juce::Colour clip(int i) const;
};

juce::String positionString(double beats, int beatsPerBar, double bpm);   // "9.3.1   0:17.250" (bar.beat.16th, min:sec)

class Timeline;
class Inspector;

class ArrangePage : public juce::Component, public juce::DragAndDropContainer, public juce::FileDragAndDropTarget, private juce::Timer {
public:
    explicit ArrangePage(Session& s);
    ~ArrangePage() override;
    void setDark(bool d);
    void setLook(ghosty::themes::Theme t);            // the house look (Spectrum drives it; the page's own DARK button then hides)
    void setThemeButtonVisible(bool v);
    bool isDark() const { return theme.dark; }
    void refresh();                                   // after the model changed from outside (undo, a page sent a sound...)
    static juce::String midiChipHelp(), noMidiClipsText();   // the MIDI chip texts (which clips carry notes)
    static juce::String rulerHint();                  // the gesture hint drawn on the ruler (408 px, 9 pt bold)
    void zoomToFit();
    void setZoom(double pixelsPerBeat, double firstBeat);
    void say(const juce::String& msg);
    // actions (also used by keys / menus / tests); they work on the selection
    void duplicateSelected(); void deleteSelected(); void splitSelectedAt(double beats);
    void copySelected(); void cutSelected(); void paste(); void selectAll(); void nudge(double beats, int tracks);
    void quantiseSelected(); void muteSelected(); void bounceSelected(); void zoomToSelection(); void repeatSelected();
    void setEnvelopeMode(bool on); bool envelopeMode() const { return envMode; }
    void importFiles(const juce::StringArray& files, int track, double beats);
    void addMarkerAt(double beat, const juce::String& name = {});
    // TRANSPORT VIEW (optional; the host editor calls it from its timer). Without it the page behaves as before.
    //   own = Spectrum's SYNC: OWN clock drives Arrange; playing = that clock (OWN) or the host (DAW) is running; bpm = its tempo.
    // In OWN the PLAY button / SPACE call onOwnPlayToggle (start / stop the own clock) instead of Arrange's internal transport;
    // the tempo label reads "OWN <bpm>" (DAW: "DAW <bpm>"), read-only: the tempo is set where the clock lives.
    struct TransportView { bool active = false; bool own = false; bool playing = false; double bpm = 0; };
    void setTransportView(const TransportView& v);
    TransportView transportView() const { return view; }
    std::function<void()> onOwnPlayToggle;
    void togglePlay();                                // what PLAY / SPACE do
    // Demo gate - called with its id ("spec.arrange.*") before every export (MIX / STEMS / MIDI drags, the loop grips, SEND TO
    // BEATS, RENDER to file): wire it to ghosty::exportAllowed (licence, ui, id). null = refused (fail closed).
    // exportOpen: the silent licence check before the background MIX pre-render to disk (null = no pre-render).
    std::function<bool(juce::Component* ui, const char* id)> allowExport;
    std::function<bool()> exportOpen;
    bool exportAllowed(const char* id, bool tellUser = true);
    bool dragLoop(bool midi);                         // the loop brace's WAV / MIDI grip (export-gated; false = refused / not ready yet)
    juce::File writeLoopMidi(const juce::File& dir) const;   // the notes inside the loop, from its start ({} when none)          // asks allowExport; refuses with "Exporting is part of the full version"
    // TOOLTIPS: true (default) = the page shows its own TooltipWindow; set false before showing it when the host editor owns one
    bool ownsTooltips = true;
    void setOwnsTooltips(bool own);
    juce::String statusMessage() const { return status.getText(); }
    // TYPE-IN: true (default) = right-click on the page's sliders opens its own type-a-value box (Shared/TypeIn.h);
    // set false before showing it when the host editor already has a ghosty::TypeIn covering the page (else two boxes open)
    bool ownsTypeIn = true;
    void setOwnsTypeIn(bool own);
    bool hasTypeIn() const { return typeIn != nullptr; }
    void parentHierarchyChanged() override;
    juce::String positionText() const;               // bar.beat.16th  mm:ss.mmm

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    Session& session; Theme theme;
private:
    friend class Timeline; friend class Inspector;
    void timerCallback() override;
    void applyTheme();
    void dragOut(int what);                           // 0 mix, 1 stems, 2 midi
    void fromBeatsMenu();
    void startUserRender(bool stems);
    void edited(const Arrangement& before);           // commit + refresh
    juce::TextButton playBtn { "PLAY" }, loopBtn { "LOOP" }, undoBtn { "UNDO" }, redoBtn { "REDO" }, addTrackBtn { "+ TRACK" }, importBtn { "IMPORT" },
                     fromBeatsBtn { "FROM BEATS" }, toBeatsBtn { "SEND TO BEATS" }, renderBtn { "RENDER" }, themeBtn { "DARK" }, fitBtn { "FIT" };
    juce::ComboBox snapBox; juce::Label tempoLbl, status;
    struct Chip;
    std::unique_ptr<Chip> mixChip, stemChip, midiChip;
    std::unique_ptr<Timeline> timeline; std::unique_ptr<Inspector> inspector;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String statusText; double statusUntil = 0; int dragLabelX = 0;
    bool envMode = false;
    // copy / paste: this page's own (was a process-wide static: a second Spectrum could paste the first one's clips).
    struct ClipBoard; std::unique_ptr<ClipBoard> clip; int pasteTrackIdx = -1;
    // background MIX render (kept ready for drag-out) + user renders
    juce::uint64 seenHash = 0; double hashSince = 0; bool autoRendering = false; int pendingUser = -1;   // -1 none, 0 mix, 1 stems
    juce::File autoDir; juce::uint64 autoRefusedHash = 0;   // the demo: the MIX is not pre-rendered to disk (asked once per edit)
    float masterMeter = 0; std::array<float, kMaxTracks> meters {};
    juce::uint64 seenRevision = 0;
    TransportView view;
    std::unique_ptr<juce::TooltipWindow> tips;
    std::unique_ptr<ghosty::TypeIn> typeIn;
    std::shared_ptr<RenderResult> loopRender;
    void updateTransportUi();
};

} // namespace arr
