#pragma once
// PROCESS LAB panel: a self-contained juce::Component (zoomable waveform with ruler / snap / playhead / fade handles + region select,
// preview lane on the same time scale, op chain list + parameter editors (sliders / toggles / choices / breakpoint envelope),
// PREVIEW (+ AUTO) / APPLY / BOUNCE / BOUNCE->NEW / AGAIN / PER SLICE / UNDO / REDO / DRAG, edit commands, seed, edge crossfade,
// progress + cancel, keyboard shortcuts). See ProcessLab.h for the integration note.
//
// Shortcuts (when the panel has focus): Space play/stop (preview if valid, else the region), P preview, Enter apply, Ctrl/Cmd+B bounce,
// Ctrl/Cmd+R repeat last, Ctrl/Cmd+Z undo, Ctrl/Cmd+Shift+Z or Ctrl/Cmd+Y redo, Ctrl/Cmd+C/X/V copy/cut/paste, Delete delete region,
// Esc cancel + stop, +/- zoom, 0 zoom to fit, A select all.
#include "ProcessLab.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>

namespace plab {

// What a plugin implements to host the lab. The Host must outlive the panel.
struct Host {
    virtual ~Host() = default;
    virtual Audio getBuffer() = 0;                                             // a copy of the host's sound (its own rate + all channels)
    virtual void setBuffer (const Audio& committed) = 0;                       // APPLY / BOUNCE / UNDO / REDO / edits: the WHOLE file (length may change)
    virtual double sampleRate() = 0;                                           // host rate (used for an empty sound)
    virtual void audition (std::shared_ptr<const Audio> clip, bool loop) = 0;  // nullptr = stop (e.g. AuditionPlayer::play / stop)
    // ---- optional
    virtual double tempoBpm() { return 0.0; }                                  // > 0: bar ruler, grid snap, STRETCH TO BARS default
    virtual std::vector<Region> slices() { return {}; }                        // chops / slices of the sound: enables PER SLICE
    virtual bool showsPerSlice() { return true; }                              // false: the host never has slices, PER SLICE is hidden (its place stays empty)
    virtual bool bounceToNew (const Audio&) { return false; }                  // BOUNCE->NEW: hand the processed region over as a new sound
    virtual bool canBounceToNew() { return false; }                            // show BOUNCE->NEW
    // the demo export gate: return ghosty::exportAllowed (licence, ui, "<plugin>.plab.drag"); false = DRAG-out refused with a message.
    // Unset = refused (fail closed): a host that never wired the gate exports nothing.
    virtual bool allowExport (juce::Component* ui) { juce::ignoreUnused (ui); return false; }
    virtual float auditionPosition() { return -1.0f; }                         // 0..1 inside the playing clip (e.g. AuditionPlayer::position()); < 0 = estimate
};

// Colours of the panel. Palette::dark() is the default (the original look); Palette::light() suits light host themes.
// The panel reads it at paint time; setPalette() also installs the panel's own LookAndFeel for its buttons / combos / sliders / labels.
struct Palette {
    // surfaces (all opaque)
    juce::Colour bg, card, cell;                 // window, panels (op list / params), cells (number boxes, slider troughs, progress trough)
    // text
    juce::Colour text, text2, dim;               // body text, secondary text (blurbs / hints / labels), dim (off / disabled marks)
    // accents
    juce::Colour accent, accent2;                // main (title, values, progress), second (purple)
    // waveform view (may stay dark like a screen in light themes)
    juce::Colour waveBg, wave, waveSel, waveText, waveDim, waveGrid;   // background, waveform, waveform inside the selection, info text, ruler ticks, grid lines
    juce::Colour selection, regionEdge, cursor, play, loop, preview, bar;   // selection fill (used at 10 % alpha), edges, cursor, playhead, loop, preview lane, bar ruler
    juce::Colour envLine, envPoint, envPointActive;
    juce::Colour opOn, opOff, opSel, opText, opTextOff;   // op list: enabled dot, disabled dot, selected row (used at 35 % alpha), names
    juce::Colour seed, info, warn, progress;
    // own LookAndFeel (only installed once setPalette() is called)
    juce::Colour button, buttonOn, buttonText, buttonTextOn, comboBg, comboText, comboOutline, sliderTrack, sliderBg,
                 labelBg, labelText, popupBg, popupHighlight, popupText, popupHighlightText, textEditBg, textEditText, textEditHighlight, toggleTick;
    static Palette dark();
    static Palette light();
};
// WCAG contrast ratio of two opaque colours (1..21)
float contrastRatio (juce::Colour a, juce::Colour b);

class ProcessLabPanel : public juce::Component, private juce::Timer {
public:
    explicit ProcessLabPanel (Host& host);
    ~ProcessLabPanel() override;

    void reload();                         // re-read the host's sound (clears undo)
    juce::ValueTree getState() const;      // "PLAB": chain + seed + undo budget + view options
    void setState (const juce::ValueTree& v);
    Lab& lab() { return session; }         // tests / advanced hosts
    int selectedOp() const { return selOp; }
    void selectOp (int i);

    // additions
    void setPalette (const Palette& p);      // light / dark theme; repaints; installs the panel's own LookAndFeel
    const Palette& getPalette() const { return palette; }
    void setView (int startSample, int numSamples);   // zoom / scroll (clipped)
    Region view() const { return { viewStart, viewLen }; }
    enum Snap { SnapOff = 0, SnapZero, SnapTransient, SnapGrid };
    void setSnap (int s) { snapMode = s; }
    int snapPosition (int sample) const;     // the snapped position for `sample` (SnapOff = unchanged)
    bool autoPreviewOn() const { return autoPreview; }
    void setAutoPreview (bool on);
    bool keyPressed (const juce::KeyPress&) override;
    std::vector<int> transients() const;     // onset positions used by SnapTransient

    void paint (juce::Graphics&) override;
    void resized() override;

    class Wave; class OpList; class Params; class EnvEditor; class DragButton;
    friend struct PanelTestAccess;   // tests
private:
    void timerCallback() override;
    void refresh();                        // buttons, list, params after any change
    void addOpMenu();
    void editMenu();
    void moveOp (int delta);
    void playSource(); void playPreview(); void stopAudition();
    void togglePlay();
    juce::File writeDragFile();
    void startedAudition (std::shared_ptr<const Audio> clip, bool loop, bool isPreview);
    float playhead01() const;

    void applyPaletteToChildren();
    Host& host;
    Palette palette = Palette::dark();
    std::unique_ptr<juce::LookAndFeel_V4> ownLnf;
    Lab session;
    int selOp = -1;
    juce::String status { "select a region, build a chain, PREVIEW, then APPLY" };
    bool auditioning = false;
    // view
    int viewStart = 0, viewLen = 0, snapMode = SnapZero;
    // auto preview
    bool autoPreview = true, wasBusy = false; uint32_t seenGen = 0, autoReqGen = 0; double genChangedAt = 0; bool skipAuto = false; int seenUndoDepth = 0;
    // playhead estimate
    double playStartMs = 0, playDurMs = 0; bool playLoop = false, playIsPreview = false;
    mutable std::vector<int> onsetCache; mutable int onsetLen = -1; mutable uint32_t onsetGen = 0;

    std::unique_ptr<Wave> wave;
    std::unique_ptr<OpList> list;
    std::unique_ptr<Params> params;
    std::unique_ptr<juce::TextButton> dragB;   // a DragButton
    juce::TextButton addB { "+ ADD" }, delB { "DEL" }, upB { "UP" }, downB { "DOWN" }, dupB { "DUP" };
    juce::TextButton previewB { "PREVIEW" }, autoB { "AUTO" }, playSrcB { "PLAY SRC" }, playPrevB { "PLAY FX" }, stopB { "STOP" }, loopB { "LOOP" };
    juce::TextButton applyB { "APPLY" }, bounceB { "BOUNCE" }, bounceNewB { "BOUNCE NEW" }, againB { "AGAIN" }, sliceB { "PER SLICE" };
    juce::TextButton undoB { "UNDO" }, redoB { "REDO" }, cancelB { "CANCEL" };
    juce::TextButton seedDown { "<" }, seedUp { ">" }, diceB { "DICE" }, allB { "ALL" }, editB { "EDIT" };
    juce::TextButton zoomIn { "+" }, zoomOut { "-" }, zoomFit { "FIT" };
    juce::ComboBox snapBox;
    juce::Label seedLabel, edgeLabel, startLabel, lenLabel;
    juce::Rectangle<int> progressArea, statusArea, titleArea, edgeTitle, numArea;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProcessLabPanel)
};

} // namespace plab
