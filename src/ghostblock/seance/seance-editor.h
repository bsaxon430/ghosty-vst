/*
 * SEANCE (GhostBlock) - twelve spirits round a table.
 *
 * The editor: THE TABLE (twelve seats, a thread from one spirit to another = that spirit possesses it = an
 * audio-rate link), a panel for the chosen spirit / thread / the circle (the whole sound), the sound bar, a keyboard.
 * It reads and writes the engine's own patch (patchMain) and talks to the audio thread through the engine's own
 * queues (BEGIN_EDIT / SET_PARAM / END_EDIT), like Poltergeist 2's editors. No engine or DSP code in here.
 */
#pragma once

#include <vector>
#include <memory>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "synth/synth.h"
#include "ghostblock/ui-note-queue.h"

namespace ghostblock::p2
{
struct Morph;
namespace spectral
{
class SpectralFx;
}
namespace sub
{
class SubBass;
}
} // namespace ghostblock::p2

namespace ghostblock::seance
{
struct StepSeq;  // steps.h
struct OutGuard; // out-guard.h
struct BlendGuard; // blend-guard.h
namespace ss = baconpaul::six_sines;
using ghostblock::p2::UiNoteQueue;

class SeanceEditor : public juce::Component, private juce::Timer
{
  public:
    static constexpr int W = 1180, H = 760; // fits a 1440 x 900 MacBook Pro screen with the DAW around it

    SeanceEditor(ss::Patch &patchMain, ss::WavetableHandoff &handoff, ss::Synth::audioToMainQueue_t &atou,
                 ss::Synth::mainToAudioQueue_T &utoa, ss::Synth::audioOutputQueue_t &aor, std::atomic<bool> &editorActive,
                 std::atomic<uint32_t> &uiForceRebuild, ss::Synth::DawStateMain &dawState, const clap_host_t *host,
                 UiNoteQueue &notes, ghostblock::p2::Morph *morph = nullptr, ghostblock::p2::spectral::SpectralFx *spectral = nullptr,
                 ghostblock::p2::sub::SubBass *sub = nullptr, StepSeq *steps = nullptr, OutGuard *guard = nullptr,
                 std::atomic<bool> *seanceHostDirty = nullptr, BlendGuard *blendGuard = nullptr);
    ~SeanceEditor() override;

    void paint(juce::Graphics &) override;
    void resized() override;

    // ---- used by the parts (and the checks)
    ss::Patch &patch;
    ss::WavetableHandoff &handoff;
    void beginEdit(ss::Param &p);
    void setValue(ss::Param &p, float v);
    void endEdit(ss::Param &p);
    void setOnce(ss::Param &p, float v); // begin + set + end
    void status(const juce::String &s);
    void selectSpirit(int op);
    void selectThread(int from, int to);
    void selectCircle();
    void refreshAll();
    void showPage(int page); // 0 the table, 1 FX (the spectral rack after the engine), 2 SUB (the sub-bass under it), 3 CROSS, 4 STEPS
    ghostblock::p2::spectral::SpectralFx *spectral{nullptr};
    ghostblock::p2::sub::SubBass *sub{nullptr};
    StepSeq *steps{nullptr};        // STEPS: the note sequencer in front of the engine (page 4)
    OutGuard *guard{nullptr};       // the output meter and safety limiter
    BlendGuard *blendGuard{nullptr}; // the crossroads' phase guard
    int testPhaseFix(int what); // 0: is it on; 1: click FIX CANCELLING; 2: what the readout says is cancelling (dB x10)
    std::atomic<bool> *seanceHostDirty{nullptr};
    void tellHost(); // a STEPS switch or the limiter changed on the screen: the host re-reads the values
    bool testLimiterOn() const;
    void testClickMeter();
    ghostblock::p2::Morph *morph{nullptr}; // BLEND: four sounds at four corners (page 3)
    void morphEdit(uint32_t paramId, double v, int type);
    void testBlendCorner(int corner, int factoryIndex); // -1 = a copy of the sound on the table
    int testCrossroads(int which);                     // CROSSROADS menu: load a ready-made one (-1 = how many)
    bool testCrossroadsSaveLoad(const juce::String &name); // Save this crossroads, then load it back from YOURS
    int testBlendEnabled(const juce::String &name) const; // 1 live, 0 greyed, -1 no such control on the BLEND page
    void spectralEdit(uint32_t paramId, double v, int type); // 0 begin, 1 value, 2 end
    void subEdit(uint32_t paramId, double v, int type);
    void markStateDirty();
    float opLevel(int op) const { return vu[(size_t)op]; }
    static constexpr int kHist = 4096;
    float histSample(int back) const; // the output, `back` samples ago (0 = newest)

    // ---- test hooks
    void testIdle() { timerCallback(); }
    juce::Component *testTable() const;
    juce::Point<float> testSeatPos(int seat) const; // in the table's own coordinates
    juce::Point<float> testThreadPos(int from, int to) const;
    juce::Point<float> testCentre() const;
    juce::String testPanelTitle() const;
    juce::String testStatus() const;
    int testPage() const;
    bool testHasBeast() const;
    void testFollow(int which, int sourceId); // FOLLOWS: 0 volume, 1 feedback, 2 pitch (the selected spirit), 3 the selected thread's depth
    void testMacroView(bool on);
    bool testMutate(float amount, int what = 31); // SUMMON > Mutate it a little (0.35) / a lot (1); what: Summoner::What bits, -1 = new voices
    void testSetMacroName(int i, const juce::String &name);
    bool testBringBack();          // SUMMON > Bring back the sound before that
    bool testStepsMake(int kind, uint32_t seed, int follow = -1); // STEPS: what picking that pattern does (kinds: step-builtins.h); follow 1 = over your chords
    bool testStepsExact(int note, double loudPercent, double sixteenths); // a note's "Set its loudness and length exactly..."
    int testStepsEnabled(const juce::String &name) const;  // 1 live, 0 greyed, -1 no such control on the STEPS page
    void testStepsClick(float tick, int row);              // a click in the roll at that time and row
    void testStepsDrag(float tick, int row, float toTick, int toRow); // press there, drag to there, let go
    void testStepsTool(int what, int arg); // 0 loop bars, 1 repeat, 2 copy bar to next, 3 copy bar to all, 4 clear bar, 5 draw on lane
    std::vector<uint8_t> testStepsMidi() const; // what DRAG MIDI OUT writes
    juce::String testSummon(int style);
    juce::String testSummonAround(int style, int keep); // SUMMON > Summon around this sound (keep: Summoner::Keep bits)
    int testSearch(const juce::String &text);           // FIND A SOUND: how many match
    int testFavourite(int what);                        // 0 count, 1 toggle the sound on the table // what SUMMON > that character does; the new sound's name
    void testSaveSoundTo(const std::string &path);   // what SAVE writes
    void testLoadSoundFrom(const std::string &path); // what picking it under YOUR SOUNDS does
    bool testLoadWav(int seat, const juce::String &name, const void *data, size_t size); // as "Load a .wav wavetable..."
    bool testLoadShadeTable(int seat, const juce::String &name, const std::string &json); // as picking a file in TABLE FILE
    void testEnvTab(int tab); // the spirit panel's lower tabs: 0 heard, 1 feedback, 2 pitch, 3 body, 4 throat, 5 move
    void testPickSpecies(int seat, int species); // as picking it from the SPECIES menu
    juce::Component *testPageComponent(int page) const;
    bool testSubKnobEnabled(const juce::String &label) const;
    int testFactoryCount() const;
    juce::String testLoadFactory(int index);
    // every control of the panel: its name, where it is (editor coordinates), whether it is shown and live
    struct CtlInfo
    {
        juce::String name;
        juce::Rectangle<int> r;
        bool visible, enabled;
    };
    std::vector<CtlInfo> testControls() const;

    struct Impl;
    std::unique_ptr<Impl> impl;

  private:
    void timerCallback() override;
    void drainAudio();
    void requestFlush();

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
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SeanceEditor)
};
} // namespace ghostblock::seance
