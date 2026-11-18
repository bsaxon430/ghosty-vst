/*
 * Poltergeist 2 (GhostBlock) - the Poltergeist look on the Six Sines engine.
 *
 * This editor replaces the upstream editor only. It reads and writes the engine's own patch (patchMain)
 * and talks to the audio thread through the engine's own queues, exactly like the upstream editor does.
 * The engine, its DSP and its presets are the upstream code, unchanged.
 */
#pragma once

#include <vector>
#include <memory>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

#include "synth/synth.h"
#include "ghostblock/ui-note-queue.h"
#include "ghostblock/morph.h"
#include "ghostblock/spectral/spectral-fx.h" // SPECTRAL FX

namespace ghostblock::p2
{
namespace ss = baconpaul::six_sines;

class Poltergeist2Editor : public juce::Component, private juce::Timer
{
  public:
    static constexpr int W = 1040, H = 744;

    Poltergeist2Editor(ss::Patch &patchMain, ss::WavetableHandoff &handoff,
                       ss::Synth::audioToMainQueue_t &atou, ss::Synth::mainToAudioQueue_T &utoa,
                       ss::Synth::audioOutputQueue_t &aor, std::atomic<bool> &editorActive,
                       std::atomic<uint32_t> &uiForceRebuild, ss::Synth::DawStateMain &dawState,
                       const clap_host_t *host, UiNoteQueue &notes, Morph *morph = nullptr,
                       spectral::SpectralFx *spectral = nullptr); // SPECTRAL FX: the rack (the fourth page)
    ~Poltergeist2Editor() override;

    void paint(juce::Graphics &) override;
    void resized() override;

    // ---- used by the parts (and the tests)
    ss::Patch &patch;
    void beginEdit(ss::Param &p);
    void setValue(ss::Param &p, float v);
    void endEdit(ss::Param &p);
    void setOnce(ss::Param &p, float v); // begin + set + end
    void status(const juce::String &s);
    void selectGhost(int op);
    void selectLink(int from, int to);
    void showPage(int pg);
    void refreshAll();
    float opLevel(int op) const { return vu[(size_t)op]; }
    float scopeSample(int i) const;
    static constexpr int kScope = 512, kHist = 4096;
    float histSample(int back) const; // the output, `back` samples ago (0 = newest)
    Morph *morph{nullptr};
    void markStateDirty(); // tell the host the project changed (MORPH lives in the plugin's state, not in params)
    void morphEdit(uint32_t paramId, double v, int type); // a MORPH param: set it, and tell the host (0 begin, 1 value, 2 end)
    // SPECTRAL FX: the rack, and an edit of one of its host params (set it, tell the host; 0 begin, 1 value, 2 end)
    spectral::SpectralFx *spectral{nullptr};
    void spectralEdit(uint32_t paramId, double v, int type);
    void morphSet(uint32_t paramId, double v) { morphEdit(paramId, v, 0); morphEdit(paramId, v, 1); morphEdit(paramId, v, 2); }

    // ---- test hooks (the offline checks: screenshots, preset loads)
    int testFactoryCount() const;
    juce::String testLoadFactory(int index); // loads factory preset #index, returns its name
    void testTheme(int t);                   // 0 BLACK, 1 PURPLE, 2 LIGHT
    void testIdle() { timerCallback(); }
    void testEnvTab(int mode);
    juce::String testPanelTitle() const;
    juce::String testStatus() const;
    juce::Component *testField() const;
    juce::Point<float> testGhostPos(int g) const;
    void testMorphCorner(int corner, int factoryIndex); // -1 = a copy of this sound
    void testPuck(float x, float y);
    std::vector<uint32_t> testReachableParams() const; // ids of every engine parameter this editor can edit (the machine editor is checked against it)

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
    std::vector<float> scope = std::vector<float>(kHist, 0.f);
    int scopePos{0};
    bool uiDirty{true};

    friend struct Impl;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Poltergeist2Editor)
};
} // namespace ghostblock::p2
