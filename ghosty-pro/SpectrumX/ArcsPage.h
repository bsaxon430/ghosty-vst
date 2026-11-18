#pragma once
// =====================================================================================================================
// INTEGRATION NOTE (for the Ghosty Spectrum owner) - the ARCS page is self-contained; nothing in the existing tabs changes.
//
//  Files:   ArcsModel.h (page data, stamp tools, clipboard), ArcsFactory.h (factory pages + line patches),
//           ArcsEngine.h/.cpp (wavetables, oversampled FM/drive group, snapshot, voices, streamed offline render),
//           ArcsHost.h/.cpp (processor side: page bank + engine + undo + save/load + MIDI export + render + host tempo),
//           ArcsPage.h/.cpp (the tab UI). Arcs.cmake lists them as ${GHOSTY_ARCS_SOURCES}; when the tab goes in add
//             target_sources(GhostySpectrum PRIVATE ${GHOSTY_ARCS_SOURCES})
//           (they need juce_dsp, juce_audio_formats, juce_gui_basics - already linked by GhostySpectrum).
//
//  Processor (SpecProcessor) - name the member e.g. arcsPage (a member called "arcs" would hide the namespace):
//    member            arcs::ArcsHost arcsPage;                                  // construct on the message thread
//    prepareToPlay     arcsPage.prepare(sampleRate, samplesPerBlock);
//    processBlock      arcsPage.process(buffer, arcs::ArcsHost::readPlayHead(getPlayHead()), true, &midiMessages);
//                      ADDS the page into the buffer (a mono bus gets it folded at -3 dB). Call it where the page should join
//                      the mix, e.g. before Spectrum's master FX. Real-time safe: no allocation, no locks; silent and nearly
//                      free with no lines. midiMessages drives MIDI-in (when the page has it on; notes 0..7 pick page A..H).
//    latency           the page output is delayed by arcs::ArcsHost::latencySamples() (36) samples by its oversampled FM /
//                      drive decimator. Add it to setLatencySamples() if Spectrum reports latency (or ignore it: 0.75 ms).
//    state             save: parent.appendChild(arcsPage.getState(), nullptr);  (ValueTree type "ARCSPAGE", bank in child "BANK")
//                      load: if (auto t = parent.getChildWithName("ARCSPAGE"); t.isValid()) arcsPage.setState(t);
//                      setState is safe from any thread (off the message thread it is applied there asynchronously).
//                      Old sessions without the child keep the default empty page; pages from a newer build are refused.
//    Transport: the page follows the host while it plays (synced pages: ppq; free-time pages: the host's seconds, so tempo
//    changes never jump; count-in is silent), else its own PLAY runs it. Snapshots retired by the audio thread are freed on
//    the message thread by ArcsHost's own 200 ms timer (after a 1 s grace).
//
//  Editor (SpecEditor): add a tab button "ARCS"; on click create
//       auto* p = new arcs::ArcsPage(proc.arcsPage);  p->setBounds(getLocalBounds());  addAndMakeVisible(p);
//       p->onLeave = [..] { remove/hide it };
//    The page is designed at 1200 x 790 (same as the SPECTRAL LAB panel) and scales its canvas with the size.
//    Tooltips: the page makes its own TooltipWindow by default; SpecEditor already has one, so call
//       p->setOwnsTooltips(false);   (before showing it) to avoid tips showing twice.
//    Demo gate: p->allowExport = [&] (juce::Component* ui, const char* id) { return ghosty::exportAllowed (license, ui, id); };
//       every drag-out / save-to-file (MIDI drag + save, audio render + drag, the loop grips) asks it first with its id
//       ("spec.arcs.*"); false = refused with "Exporting is part of the full version". Null = refused (fail closed).
//       p->exportOpen = [&] { return ghosty::exportUnlocked (license); };  the silent check before the loop's background
//       WAV pre-render (null = no pre-render to disk).
//    SEND TO ARRANGE (render to memory, no file):
//       auto req = proc.arcsPage.prepareRender();                          // message thread: freezes the page (cheap)
//       // on a worker:  juce::AudioBuffer<float> buf; std::vector<arcs::RenderNote> notes;
//       //               bool ok = arcs::ArcsHost::renderToBuffer(req, buf, sampleRate, &notes, &abortFlag, &progress);
//       // stereo, one pass of the page (speed lane + reverse, loop ignored, latency removed, 0.1 s tail); notes = one per
//       // audible line / repeat: { pitch, velocity, start, length } in seconds (the same fields as arr::Note).
//       // req.name is the page name, req.bpm the tempo it was rendered at.
//       (blocking shortcut on the message thread: proc.arcsPage.renderToBuffer(buf, sampleRate, &notes))
//    RE-RENDER from ARRANGE: store  proc.arcsPage.renderBlob()  (binary state + tempo) as the clip's SourceRef state, i.e.
//       arr::makePayload(buf, sr, req.name, notes, { "ARCS", req.name, proc.arcsPage.renderBlob() }, req.bpm)
//     and render it later from the blob alone (any thread, no live page needed; a damaged / newer blob renders silence):
//       auto req2 = arcs::ArcsHost::fromState(r.state);   arcs::ArcsHost::renderToBuffer(req2, buf, sr, &notes);
//     (fromState also takes the ValueTree from getState(); an optional bpm overrides the stored tempo)
//    Theme: light by default with a DARK toggle (dark = Spectrum's palette); setDark()/onThemeChanged
//    let the owner sync it, the choice is saved in the page state.
//    Ruler gestures (the same in every GhostBlock product): right-click (two-finger click) = play from here; right-drag = make a
//    loop; left-drag an existing loop's ends = resize, its middle = move; a plain left click never moves anything. Keyboard: the page grabs focus when clicked
//    (D/L/C/E/S tools, space, ctrl+Z/C/V/X/D/A, arrows nudge, Del).
// =====================================================================================================================
#include "ArcsHost.h"
#include "GhostThemes.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace arcs {

class ArcsPage : public juce::Component, private juce::Timer {
public:
    explicit ArcsPage(ArcsHost& host, bool ownsTooltips = true);
    void setOwnsTooltips(bool own);                 // false: no TooltipWindow of its own (the host editor shows the tips)
    bool ownsTooltips() const;
    std::function<bool(juce::Component* ui, const char* id)> allowExport;   // demo gate: asked before every export (null = refused)
    std::function<bool()> exportOpen;               // silent licence check (background pre-renders to disk; null = closed)
    ~ArcsPage() override;
    std::function<void()> onLeave;                  // the BACK button (owner returns to the other tabs)
    std::function<void(bool dark)> onThemeChanged;
    void setDark(bool dark);
    void setLook(ghosty::themes::Theme t);          // the house look (BLACK / PURPLE / LIGHT); Spectrum drives it
    void setThemeButtonVisible(bool v);             // false when the host has the one theme button
    // the loop brace's WAV / MIDI grips (tests and hosts): is the loop's WAV rendered for the current loop; export it (drag = the OS drag, or a test hook)
    bool loopWavReady() const;
    void dragLoopOut(bool midi, std::function<void(const juce::StringArray&)> drag = {});
    bool isDark() const;

    enum Tool { Draw = 0, Line, Curve, Erase, Select };
    void selectTool(int t);
    void setSelection(const std::vector<int>& arcIds);
    std::vector<int> selection() const;
    void refresh();                                 // re-read the page into every control (after an outside change)
    void fitView();                                 // zoom the canvas to the whole page and every line's pitch range
    juce::String status() const;                    // the last status-line message (tests)

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;
    void mouseDown(const juce::MouseEvent&) override;

    // tests: a mouse gesture on the canvas (first point = press, last = release), points given as page position / MIDI
    void testGesture(const std::vector<std::pair<float, float>>& pageMidi, juce::ModifierKeys extraMods = {});
    void testRuler(float fromPos, float toPos, bool rightButton);   // a click (from == to) or drag on the bar ruler
    bool testExport(bool audio, const juce::File& midiTarget = {});   // runs the MIDI save / audio render path; false = refused

    struct Impl;
private:
    void timerCallback() override;
    ArcsHost& host;
    std::unique_ptr<Impl> impl;
};

} // namespace arcs
