/*
 * Poltergeist 2 (GhostBlock) - the ghost-machine editor.
 *
 * The same job as ghostblock/p2-editor.h (it edits the engine's own patch through the engine's own queues and
 * talks to MORPH through morphEdit / morphSet), in the painted machine: three pages of one flying machine, every
 * control a part of it. The art and its layout come from resources/ghostblock/machine (ui-layout.json, layers.json).
 *
 * UI and plumbing only. The engine, its DSP, its presets and the patch / state format are untouched.
 */
#pragma once

#include <memory>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "synth/synth.h"
#include "ghostblock/ui-note-queue.h"
#include "ghostblock/morph.h"
#include "ghostblock/spectral/spectral-fx.h" // SPECTRAL FX
#include "ghostblock/sub/sub-bass.h"         // SUB

namespace ghostblock::p2::machine
{
namespace ss = baconpaul::six_sines;
class SpyglassWindow;

class MachineEditor : public juce::Component, private juce::Timer
{
  public:
    static constexpr int W = 1040, H = 744;

    MachineEditor(ss::Patch &patchMain, ss::WavetableHandoff &handoff, ss::Synth::audioToMainQueue_t &atou,
                  ss::Synth::mainToAudioQueue_T &utoa, ss::Synth::audioOutputQueue_t &aor, std::atomic<bool> &editorActive,
                  std::atomic<uint32_t> &uiForceRebuild, ss::Synth::DawStateMain &dawState, const clap_host_t *host,
                  UiNoteQueue &notes, Morph *morph = nullptr, spectral::SpectralFx *spectral = nullptr, // SPECTRAL FX: the rack (the FX page)
                  sub::SubBass *sub = nullptr);                                                         // SUB: the sub-bass layer (its sign and panel)
    ~MachineEditor() override;

    void paint(juce::Graphics &) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent &) override;
    void mouseExit(const juce::MouseEvent &) override;
    void mouseDown(const juce::MouseEvent &) override;
    void mouseDrag(const juce::MouseEvent &) override;
    void mouseUp(const juce::MouseEvent &) override;
    void mouseWheelMove(const juce::MouseEvent &, const juce::MouseWheelDetails &) override;
    bool keyPressed(const juce::KeyPress &) override;
    bool keyStateChanged(bool isKeyDown) override;
    void focusLost(FocusChangeType) override;

    // ---- the engine's side (the same calls the first Poltergeist 2 editor makes)
    ss::Patch &patch;
    Morph *morph{nullptr};
    spectral::SpectralFx *spectral{nullptr};
    sub::SubBass *sub{nullptr};
    void spectralEdit(uint32_t paramId, double v, int type);
    void subEdit(uint32_t paramId, double v, int type); // SUB: 0 begin, 1 value, 2 end
    void subPreset(int preset);                          // SUB: one of its eight presets
    void spectralService(); // after any edit of the rack: flush to the host, build what changed // 0 begin, 1 value, 2 end (the same rules as the first editor's)
    void beginEdit(ss::Param &p);
    void setValue(ss::Param &p, float v);
    void endEdit(ss::Param &p);
    void setOnce(ss::Param &p, float v); // begin + set + end
    void status(const juce::String &s);
    void selectGhost(int op);
    void selectLink(int from, int to);
    void showPage(int pg); // 0 GHOSTS, 1 MAIN, 2 MORPH
    void refreshAll();
    float opLevel(int op) const { return vu[(size_t)op]; }
    static constexpr int kScope = 512, kHist = 4096;
    float histSample(int back) const; // the output, `back` samples ago (0 = newest)
    float scopeSample(int i) const { return histSample(kScope - 1 - i); }
    void markStateDirty();
    void morphEdit(uint32_t paramId, double v, int type); // 0 begin, 1 value, 2 end
    void morphSet(uint32_t paramId, double v)
    {
        morphEdit(paramId, v, 0);
        morphEdit(paramId, v, 1);
        morphEdit(paramId, v, 2);
    }

    // ---- the window's size: the whole machine scales (fixed 1040:744, 75 - 200 %, never bigger than the screen; sizing.h)
    double viewScale() const;                       // component px per design px (the smaller of the two ratios)
    juce::Point<float> toDesign(juce::Point<float> p) const { return p / (float)viewScale(); }
    void setUiScale(double s, bool save);           // the grip, the size menu: asks the host (onRequestSize) and resizes
    void sizeMenu();                                // 75 / 100 / 125 / 150 / 200 %
    std::function<bool(int w, int h)> onRequestSize; // set by the plugin: the CLAP host's gui.request_resize (false = refused)

    // ---- the SPYGLASS: upstream's spectrum analyzer + scope in a window of the machine (spyglass.h)
    void toggleSpyglass();
    void openSpyglass();
    void closeSpyglass(bool rememberClosed); // false: the editor is going away; it opens again with the editor
    bool spyglassOpen() const { return spy != nullptr; }
    void raiseSpyglass(); // in front of the plugin window again (after a press / release here)
    SpyglassWindow *spyglass() { return spy.get(); }
    ss::ui::defaultsProvder_t *analyzerDefaults{nullptr}; // the engine's defaults provider (the analyzer keeps its resolution there), set by the plugin

    // ---- test hooks (the offline checks)
    int testFactoryCount() const;
    juce::String testLoadFactory(int index);
    void testTheme(int t); // 0 STORM (BLACK), 1 DUSK (PURPLE), 2 CLEAR (LIGHT)
    void testIdle() { timerCallback(); }
    void testAdvance(double seconds); // run the machine's clock (30 steps a second), no waiting
    void testEnvTab(int mode);
    juce::String testPanelTitle() const;
    juce::String testStatus() const;
    juce::Point<float> testSocket(int ghost, bool out) const; // the centre of a hose socket
    void testMorphSet(int index);                             // MORPH SETS: apply one
    void testMorphCorner(int corner, int factoryIndex);       // -1 = a copy of this sound
    void testPuck(float x, float y);
    juce::String testLayoutJson() const;                            // the embedded ui-layout.json, as text
    juce::StringArray testControlIds() const;                       // every control the editor built
    juce::Rectangle<float> testHitRect(const juce::String &id) const; // where it takes the pointer
    juce::String testControlState(const juce::String &id) const;    // "page/face/interactive" as it stands now
    juce::String testControlText(const juce::String &id) const;     // its live name | value
    std::vector<uint32_t> testReachableParams();                    // ids of every engine parameter a control can edit
    void testOpenLedger(int mode, int corner = 0);                  // 0 SOUNDS, 1 YOUR SOUNDS, 2 a MORPH corner
    void testCloseLedger(); // (and the SUB panel)
    void testSubPanel(bool open);
    bool testSubPanelOpen() const;
    // the BACK ROOM (backroom.cpp)
    void testBackRoom(bool open);
    bool testBackRoomOpen() const;
    juce::Point<float> testBackCell(int from, int to) const; // design px; to == from: feedback, to < 0: YOU HEAR
    juce::Rectangle<float> testBackDoor(int i) const;        // 0 left eye, 1 right eye, 2 mouth
    juce::Rectangle<float> testBackExit() const;
    // STUDIO VIEW (studio.cpp)
    void testStudio(bool open);
    bool testStudioOpen() const;
    int testStudioTab() const;
    juce::Rectangle<float> testStudioWidget(const juce::String &label, int op, int tab) const; // op -1: not a ghost's; tab -1: the top bar / tabs
    juce::Rectangle<float> testStudioButton() const;                // the way in on the current machine page
    juce::Rectangle<float> testStudioProxy(const juce::String &ctlId) const; // where the studio draws one of the machine's controls
    juce::Rectangle<float> testStudioCell(int from, int to) const;  // WIRING: to == from: feedback, to < 0: HEAR
    juce::Rectangle<float> testStudioPad() const;
    double testLastTickMs() const; // what the last tick (its compose included) cost
    void testStorm(float v, bool strikeNow); // hold the weather at v (0..1); < 0 lets the patch decide again
    void testCalm(bool on);
    int testPage() const;
    bool testSpyglassWillReopen() const { return spyReopen; }
    double testSavedScale() const;
    void testScale(int s);         // build the picture at 1x or 2x (a paint at another scale builds it again)
    void testRun(bool on);         // keep ticking on the real clock without a window
    juce::String testLive() const; // what the governor measured while it ran in real time (ms a second, ambient rate)
    std::vector<uint32_t> testFxReachable();  // ids of every SPECTRAL FX host parameter a control of the FX page can edit
    juce::String testFxEffectCheck();         // puts every effect in a slot and walks its parameters over the bench; "ok ..." or what is missing
    juce::String testPerf(int frames); // composes `frames` ticks and says what they cost

    struct Impl;
    std::unique_ptr<Impl> impl;

  private:
    void timerCallback() override;
    void drainAudio();
    void requestFlush();

    ss::WavetableHandoff &handoff;
    ss::Synth::audioToMainQueue_t &audioToMain;
    ss::Synth::mainToAudioQueue_T &mainToAudio;
    ss::Synth::audioOutputQueue_t &audioOutputRing;
    std::atomic<bool> &editorActive;
    std::atomic<uint32_t> &uiForceRebuild;
    uint32_t lastForceRebuild{0};
    ss::Synth::DawStateMain &dawState;
    const clap_host_t *clapHost{nullptr};
    UiNoteQueue &noteQueue;
    std::array<std::string, ss::numOps> wavetableError{};

    std::array<float, ss::numOps> vu{};
    float vuOut{0};
    int voiceCount{0};
    std::vector<float> scope = std::vector<float>(kHist, 0.f);
    int scopePos{0};
    bool uiDirty{true};
    // the SPYGLASS reads a ring of its own, filled here from the one drain of the synth's output ring (one reader each)
    std::unique_ptr<ss::Synth::audioOutputQueue_t> spyRing; // (1 MB: only while the window is open)
    std::unique_ptr<SpyglassWindow> spy;                    // (after the ring: it goes first)
    float hostSR{48000.f};
    bool spyReopen{false}; // it was open when the editor last closed: open it again once the editor is on screen

    friend struct Impl;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MachineEditor)
};
} // namespace ghostblock::p2::machine
